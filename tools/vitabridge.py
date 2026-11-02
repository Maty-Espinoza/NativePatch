#!/usr/bin/env python3
"""Relay between Claude and the PS Vita: runs on the user's PC (Mac or Windows, with vitabridge.cmd) on the same network
as the Vita, and passes to the Vita what Claude leaves in a shared folder (and brings back what it asks for).

  python3 vitabridge.py [VITA_IP] [FOLDER]    (FOLDER: ~/Downloads/vitabridge)
  Without an IP (or with "buscar") it looks for the Vita on the local /24 networks (port 1338 answering "vitacompanion").
  On Windows use the Windows Python: from WSL the Vita search does not work.

On the Vita: vitacompanion (FTP on 1337 and commands on 1338: launch, press, wait, kill, destroy, reboot...).
What Claude leaves in FOLDER:
  enviar/ux0/<path>  -> uploaded to ux0:/<path> (same for ur0/...) and moved to enviados/
  cmd_*.txt   -> each line goes to port 1338; the answer stays in respuestas/cmd_*.txt
  bajar_*.txt -> each line is a Vita path (ux0:/...) downloaded to recibidos/, or
                 "path -> dest" to leave it in another PC folder (e.g. ~/Documents/...)
  listar_*.txt -> each line is a Vita folder; the listing stays in respuestas/
  vigilar.txt -> only wakes the relay (the above wake it too)
What the relay writes:
  recibidos/  -> besides what was requested, new ux0:/pspemu/native*.log files, but only once they stop growing (when
                 the game or Adrenaline closes): it looks at the folder every 60 s and downloads a file if its size did
                 not change since the previous look.
  estado.txt  -> heartbeat (time), the last time the Vita responded and whether the relay is awake
  vitabridge.log -> copy of everything it shows
Awake / asleep: it only works until 30 min after the last request or since a log grew. Awake it asks the Vita for
"nosleep on"; asleep it leaves it alone and sends "nosleep off" so that it sleeps and saves battery.
It opens the FTP only when there is something to do and closes it when done: vitacompanion's FTP leaks memory on each
use and querying it often ends up hanging the Vita. Details: docs/agent/<language>/testing.md.
Cleanup: once an hour it deletes from enviados/, recibidos/ and respuestas/ whatever is older than CLEAN_DAYS days (and
old .parcial files and folders left empty). Whatever is to be kept from a test is first copied to its
etapaN_<version>/ folder (respaldo/ is not touched).
Standard library only. Ctrl+C to quit.
"""
import ftplib, os, socket, sys, time, fnmatch, shutil
from concurrent.futures import ThreadPoolExecutor

IP = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] != 'buscar' else None
BASE = os.path.expanduser(sys.argv[2] if len(sys.argv) > 2 else '~/Downloads/vitabridge')
WATCH_DIR, PATTERNS = 'ux0:/pspemu', ['native*.log']
seen = {}  # Vita file -> size already downloaded
last_size = {}  # Vita file -> size at the previous look (unchanged means the game no longer writes it)
WATCH_EVERY, AWAKE_FOR = 60, 30 * 60  # look at ux0:/pspemu every 60 s, until 30 min after the last event
REQUESTS = ('cmd_*.txt', 'bajar_*.txt', 'listar_*.txt', 'vigilar*.txt')
CLEAN_DAYS, CLEAN_EVERY = 7, 3600  # delete what is over 7 days old in enviados/, recibidos/ and respuestas/, once an hour
watch_t, vita_t, awake_until, first, clean_t = 0, '-', 0, True, 0

# Shows a line with the time and appends it to FOLDER/vitabridge.log.
def log(*a):
    line = ' '.join([time.strftime('%H:%M:%S')] + [str(x) for x in a])
    print(line, flush=True)
    try:  # copy in FOLDER/vitabridge.log: Claude sees what happened even without seeing the window
        with open(os.path.join(BASE, 'vitabridge.log'), 'a', encoding='utf-8') as f:
            f.write(line + '\n')
    except OSError:
        pass

# New FTP connection to vitacompanion (anonymous, passive mode).
def ftp():
    f = ftplib.FTP()
    f.connect(IP, 1337, timeout=20)
    f.login()
    f.set_pasv(True)
    return f

def vpath(p):  # VitaShell and vitacompanion use /ux0:/...
    p = p.replace('\\', '/')
    return p if p.startswith('/') else '/' + p

# Sends a line to port 1338 and returns the answer until it closes or 3 s without data.
def command(line, ip=None):
    s = socket.create_connection((ip or IP, 1338), timeout=30)
    s.sendall((line.strip() + '\n').encode())
    s.settimeout(3)
    out = b''
    try:
        while True:
            d = s.recv(4096)
            if not d:
                break
            out += d
    except socket.timeout:
        pass
    s.close()
    return out.decode(errors='replace')

# Files (not folders) of a Vita folder: {name: size}, read from the FTP LIST.
def listing(f, d):
    rows = []
    f.retrlines('LIST ' + vpath(d), rows.append)
    out = {}
    for r in rows:
        parts = r.split(None, 8)
        if len(parts) == 9 and not r.startswith('d'):
            try:
                out[parts[8]] = int(parts[4])
            except ValueError:
                pass
    return out

# Downloads a file to <local>.parcial and renames it when done: a cut does not leave a half file with the final name.
def download(f, remote, local):
    os.makedirs(os.path.dirname(local), exist_ok=True)
    tmp = local + '.parcial'
    got = [0, time.time()]
    with open(tmp, 'wb') as out:
        def chunk(d):
            out.write(d)
            got[0] += len(d)
            if time.time() - got[1] > 10:  # large files (ISO): progress every 10 s
                got[1] = time.time()
                log('bajando', remote, '%d MB' % (got[0] >> 20))
        f.retrbinary('RETR ' + vpath(remote), chunk, blocksize=1 << 16)
    os.replace(tmp, local)
    log('bajado', remote, '->', local)

# Files pending upload in FOLDER/enviar/, without hidden files or Finder/Explorer junk.
def uploads():
    send = os.path.join(BASE, 'enviar')
    return [os.path.join(r, n) for r, _, fs in os.walk(send) for n in fs
            if not (n.startswith('.') or n.lower() in ('desktop.ini', 'thumbs.db'))]  # Finder/Explorer junk

# Deletes from enviados/, recibidos/ and respuestas/ files older than CLEAN_DAYS days (and any .parcial over one
# day old), and folders left empty. What was deleted goes to the log.
def cleanup():
    cutoff, n = time.time() - CLEAN_DAYS * 86400, 0
    for d in ('enviados', 'recibidos', 'respuestas'):
        for root, dirs, files in os.walk(os.path.join(BASE, d), topdown=False):
            for f in files:
                p = os.path.join(root, f)
                try:
                    old = os.path.getmtime(p) < cutoff or (f.endswith('.parcial') and os.path.getmtime(p) < time.time() - 86400)
                    if old or f == '.DS_Store':
                        os.remove(p); n += 1
                except OSError:
                    pass
            if root != os.path.join(BASE, d) and not os.listdir(root):
                try:
                    os.rmdir(root)
                except OSError:
                    pass
    if n:
        log('limpieza: %d archivos de mas de %d dias borrados de enviados/, recibidos/ y respuestas/' % (n, CLEAN_DAYS))

# Sends "nosleep on|off" to the Vita (an error is only noted).
def nosleep(state):  # vitacompanion enables it on each boot: the Vita does not sleep by itself and drains the battery
    try:
        log('nosleep', state, '->', command('nosleep ' + state).strip())
    except OSError as e:
        log('nosleep', state, 'no respondio:', e)

# One round of the main loop: cleanup (hourly), decides if awake, opens the FTP only if there are uploads,
# file requests or the logs are due a look (transfer), runs the cmd_*.txt, deletes the served cmd_/vigilar files and
# writes estado.txt.
def step():
    global watch_t, vita_t, awake_until, clean_t
    now = time.time()
    if now - clean_t >= CLEAN_EVERY:
        clean_t = now
        cleanup()
    ups = uploads()
    reqs = [n for n in sorted(os.listdir(BASE)) if any(fnmatch.fnmatch(n, p) for p in REQUESTS)]
    if ups or reqs:
        if not awake_until:
            log('despierto')
            nosleep('on')
        awake_until = now + AWAKE_FOR
    elif awake_until and now >= awake_until:
        awake_until = 0
        log('dormido: 30 min sin pedidos ni logs nuevos')
        nosleep('off')
    watch = awake_until and now - watch_t >= WATCH_EVERY
    if ups or watch or any(not n.startswith(('cmd_', 'vigilar')) for n in reqs):
        f = ftp()
        try:
            transfer(f, ups, reqs, watch)
        finally:
            try:
                f.quit()
            except Exception:
                f.close()
        vita_t = time.strftime('%H:%M:%S')
    for n in reqs:  # commands (port 1338, no FTP) and vigilar
        path = os.path.join(BASE, n)
        if n.startswith('cmd_'):
            res = []
            for line in open(path, encoding='utf-8').read().splitlines():
                if line.strip():
                    log('comando', line)
                    res.append('> ' + line + '\n' + command(line))
            os.makedirs(os.path.join(BASE, 'respuestas'), exist_ok=True)
            open(os.path.join(BASE, 'respuestas', n), 'w', encoding='utf-8').write('\n'.join(res))
            vita_t = time.strftime('%H:%M:%S')
        if n.startswith(('cmd_', 'vigilar')):
            os.remove(path)
    status = 'despierto hasta %s' % time.strftime('%H:%M', time.localtime(awake_until)) if awake_until else 'dormido'
    open(os.path.join(BASE, 'estado.txt'), 'w', encoding='utf-8').write('vivo %s, Vita %s (respondio %s), %s\n' % (
        time.strftime('%Y-%m-%d %H:%M:%S'), IP, vita_t, status))

# Everything using the open FTP: uploads (enviar/ -> enviados/), bajar_*.txt, listar_*.txt and, if watch, the look at
# ux0:/pspemu: a native*.log is downloaded when it has the same size as at the previous look (no longer growing).
def transfer(f, ups, reqs, watch):
    global watch_t, awake_until, first
    send = os.path.join(BASE, 'enviar')
    for local in ups:  # uploads
        rel = os.path.relpath(local, send).split(os.sep)
        remote = rel[0] + ':/' + '/'.join(rel[1:])
        for k in range(2, len(rel)):  # creates the missing folders (already existing: error ignored)
            try:
                f.mkd(vpath(rel[0] + ':/' + '/'.join(rel[1:k])))
            except ftplib.error_perm:
                pass
        with open(local, 'rb') as src:
            f.storbinary('STOR ' + vpath(remote), src)
        log('subido', remote, '(%d bytes)' % os.path.getsize(local))
        done = os.path.join(BASE, 'enviados', *rel)
        os.makedirs(os.path.dirname(done), exist_ok=True)
        shutil.move(local, done)
    for name in reqs:  # file requests, in name order
        path = os.path.join(BASE, name)
        if name.startswith('bajar_'):
            lines = [l for l in open(path, encoding='utf-8').read().splitlines() if l.strip()]
            os.remove(path)  # before downloading: if cut, a large file is not requested again by itself
            for line in lines:
                remote, _, dest = line.partition('->')
                remote = remote.strip()
                dest = os.path.expanduser(dest.strip()) if dest.strip() else os.path.join(BASE, 'recibidos', remote.split('/')[-1])
                try:  # a missing file must not block the relay
                    download(f, remote, dest)
                except ftplib.error_perm as e:
                    log('no se pudo bajar', remote, ':', e)
                    if os.path.exists(dest + '.parcial'):
                        os.remove(dest + '.parcial')
        elif name.startswith('listar_'):
            res = []
            for line in open(path, encoding='utf-8').read().splitlines():
                if line.strip():
                    rows = []
                    try:
                        f.retrlines('LIST ' + vpath(line.strip()), rows.append)
                    except ftplib.error_perm as e:
                        rows.append('error: %s' % e)
                    res.append('> ' + line.strip() + '\n' + '\n'.join(rows))
            os.makedirs(os.path.join(BASE, 'respuestas'), exist_ok=True)
            open(os.path.join(BASE, 'respuestas', name), 'w', encoding='utf-8').write('\n'.join(res) + '\n')
            os.remove(path)
    if not watch:
        return
    # What is new in ux0:/pspemu: only what stopped growing
    watch_t = time.time()
    files = listing(f, WATCH_DIR)
    if first:  # what is already in recibidos/ with the same size is not downloaded again
        for name, size in files.items():
            local = os.path.join(BASE, 'recibidos', name)
            if os.path.exists(local) and os.path.getsize(local) == size:
                seen[name] = size
        first = False
    for name, size in files.items():
        if not any(fnmatch.fnmatch(name, p) for p in PATTERNS) or seen.get(name) == size:
            continue
        awake_until = time.time() + AWAKE_FOR  # a growing or undownloaded log: a game is under test
        if last_size.get(name) == size:  # same as last time: the game no longer writes it
            download(f, WATCH_DIR + '/' + name, os.path.join(BASE, 'recibidos', name))
            seen[name] = size
        last_size[name] = size

def local_nets():
    """/24 networks of this machine (of the interface with a route to the internet and of the local name's IPs), as "a.b.c"."""
    ips = set()
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('8.8.8.8', 53))
        ips.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    try:
        ips.update(socket.gethostbyname_ex(socket.gethostname())[2])
    except OSError:
        pass
    return sorted({ip.rsplit('.', 1)[0] for ip in ips if not ip.startswith('127.')})

# True if vitacompanion answers the version command at that IP.
def is_vita(ip):  # port 1338 open is not enough (VPN, other machines): vitacompanion must answer
    try:
        return 'vitacompanion' in command('version', ip)
    except OSError:
        return False

# Finds the Vita: first the IP saved in ultima_ip.txt; if not, tries port 1338 on .1-.254 of each local network
# (64 at a time) and confirms with is_vita. Saves the IP found.
def find_vita():
    last = os.path.join(BASE, 'ultima_ip.txt')
    if os.path.exists(last):  # first the IP from last time
        ip = open(last, encoding='utf-8').read().strip()
        if is_vita(ip):
            return ip
    def probe(ip):
        try:
            c = socket.create_connection((ip, 1338), timeout=0.6)
            c.close()
            return ip
        except OSError:
            return None
    nets = sorted(local_nets(), key=lambda n: not n.startswith('192.168.'))  # the home network first
    log('redes de este equipo:', nets)
    for net in nets:
        log('buscando la Vita en %s.1-254 (puerto 1338)...' % net)
        with ThreadPoolExecutor(64) as ex:
            found = [ip for ip in ex.map(probe, ['%s.%d' % (net, i) for i in range(1, 255)]) if ip]
        for ip in found:
            log('puerto 1338 abierto en', ip)
            if is_vita(ip):
                open(last, 'w', encoding='utf-8').write(ip)
                return ip
    return None

# Finds the Vita (if no IP was given), creates the folders and repeats step() every 1 s; on a network error retries in
# 5 s and marks it in estado.txt.
def main():
    global IP
    if not IP:
        IP = find_vita()
        if not IP:
            log('No se encontro la Vita. Revisa que este en la misma red y con vitacompanion activo, o pasa la IP:')
            log('  python3 vitabridge.py 192.168.x.y')
            return 1
        try:
            log('vitacompanion responde:', command('version').strip() or '(sin texto)')
        except OSError as e:
            log('no respondio al comando version:', e)
    for d in ('enviar', 'enviados', 'recibidos', 'respuestas'):
        os.makedirs(os.path.join(BASE, d), exist_ok=True)
    log('relevo en', BASE, 'con la Vita en', IP, '(dormido hasta que llegue un pedido; borra lo de mas de %d dias en enviados/, recibidos/ y respuestas/)' % CLEAN_DAYS)
    nosleep('off')
    while True:
        try:
            step()
        except (OSError, EOFError, ftplib.Error) as e:
            log('error:', e, '(reintento en 5 s)')
            open(os.path.join(BASE, 'estado.txt'), 'w', encoding='utf-8').write(  # the heartbeat goes on: the Vita does not respond
                'vivo %s, Vita %s (respondio %s), la Vita no responde (dormida, apagada o fuera de la red)\n' % (
                time.strftime('%Y-%m-%d %H:%M:%S'), IP, vita_t))
            time.sleep(5)
            continue
        time.sleep(1)

if __name__ == '__main__':
    os.makedirs(BASE, exist_ok=True)
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print()
    except Exception:
        import traceback
        log('fallo:', traceback.format_exc())
        raise
