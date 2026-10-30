#!/usr/bin/env python3
"""Relevo entre Claude y la PS Vita (correr en la Terminal del Mac o en Windows, en la misma red que la Vita).
  En Windows: doble clic en vitabridge.cmd (usa el Python de Windows; en WSL la busqueda automatica de la Vita no funciona).

  python3 vitabridge.py [IP_DE_LA_VITA] [CARPETA]    (CARPETA: ~/Downloads/vitabridge)
  Sin IP (o con "buscar") busca la Vita en la red local: el puerto 1338 de vitacompanion.

En la Vita: vitacompanion (FTP en 1337 y comandos en 1338: launch, press, wait, kill...).
Claude escribe en CARPETA y el relevo hace el resto:
  enviar/ux0/pspemu/seplugins/nativelink.prx  -> se sube a ux0:/pspemu/seplugins/nativelink.prx (igual con ur0/...)
  cmd_*.txt   -> cada linea va al puerto 1338; la respuesta queda en respuestas/cmd_*.txt
  bajar_*.txt -> cada linea es una ruta de la Vita (ux0:/...) que se baja a recibidos/, o
                 "ruta -> destino" para dejarla en otra carpeta del Mac (p. ej. ~/Documents/...)
  listar_*.txt -> cada linea es una carpeta de la Vita; el listado queda en respuestas/
  vigilar.txt -> solo despierta al relevo (lo de arriba tambien lo despierta)
  Ademas baja lo nuevo de ux0:/pspemu (native*.log), pero solo cuando el archivo deja de crecer (al cerrar el juego o
  Adrenaline): mira la carpeta cada 60 s y baja un archivo cuando su tamano no cambio desde la vez anterior.
  Solo trabaja "despierto": hasta 30 min despues del ultimo pedido o de que un log crecio. Despierto le pide a la Vita
  que no se duerma (nosleep on); dormido no la toca y la deja dormirse (nosleep off), para ahorrar bateria.
  Abre el FTP solo cuando hay algo que hacer y lo cierra al terminar.
  (Historia: el FTP de vitacompanion pierde memoria. Listando cada 1 s y bajando los logs mientras crecian, a las ~600
  descargas respondia "550 Could not allocate memory" y la Vita no abria programas; el 2026-10-29 se paso a cada 15 s,
  pero con el relevo abierto todo el dia la Vita igual se colgaba, dentro y fuera de Adrenaline.)
  estado.txt  -> latido (hora), la ultima vez que respondio la Vita y si el relevo esta despierto
Solo usa la biblioteca estandar. Ctrl+C para terminar.
"""
import ftplib, os, socket, sys, time, fnmatch, shutil
from concurrent.futures import ThreadPoolExecutor

IP = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] != 'buscar' else None
BASE = os.path.expanduser(sys.argv[2] if len(sys.argv) > 2 else '~/Downloads/vitabridge')
WATCH_DIR, PATTERNS = 'ux0:/pspemu', ['native*.log']
seen = {}  # archivo de la Vita -> tamano ya bajado
last_size = {}  # archivo de la Vita -> tamano en la mirada anterior (si no cambio, el juego ya no lo escribe)
WATCH_EVERY, AWAKE_FOR = 60, 30 * 60  # mirar ux0:/pspemu cada 60 s, hasta 30 min despues de lo ultimo que paso
REQUESTS = ('cmd_*.txt', 'bajar_*.txt', 'listar_*.txt', 'vigilar*.txt')
watch_t, vita_t, awake_until, first = 0, '-', 0, True

def log(*a):
    line = ' '.join([time.strftime('%H:%M:%S')] + [str(x) for x in a])
    print(line, flush=True)
    try:  # copia en CARPETA/vitabridge.log: Claude ve que paso aunque no vea la ventana
        with open(os.path.join(BASE, 'vitabridge.log'), 'a', encoding='utf-8') as f:
            f.write(line + '\n')
    except OSError:
        pass

def ftp():
    f = ftplib.FTP()
    f.connect(IP, 1337, timeout=20)
    f.login()
    f.set_pasv(True)
    return f

def vpath(p):  # VitaShell y vitacompanion usan /ux0:/...
    p = p.replace('\\', '/')
    return p if p.startswith('/') else '/' + p

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

def download(f, remote, local):
    os.makedirs(os.path.dirname(local), exist_ok=True)
    tmp = local + '.parcial'
    got = [0, time.time()]
    with open(tmp, 'wb') as out:
        def chunk(d):
            out.write(d)
            got[0] += len(d)
            if time.time() - got[1] > 10:  # archivos grandes (ISO): avance cada 10 s
                got[1] = time.time()
                log('bajando', remote, '%d MB' % (got[0] >> 20))
        f.retrbinary('RETR ' + vpath(remote), chunk, blocksize=1 << 16)
    os.replace(tmp, local)
    log('bajado', remote, '->', local)

def uploads():
    send = os.path.join(BASE, 'enviar')
    return [os.path.join(r, n) for r, _, fs in os.walk(send) for n in fs
            if not (n.startswith('.') or n.lower() in ('desktop.ini', 'thumbs.db'))]  # basura del Finder/Explorador

def nosleep(state):  # vitacompanion lo enciende en cada arranque: la Vita no se duerme sola y gasta bateria
    try:
        log('nosleep', state, '->', command('nosleep ' + state).strip())
    except OSError as e:
        log('nosleep', state, 'no respondio:', e)

def step():
    global watch_t, vita_t, awake_until, first
    now = time.time()
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
    for n in reqs:  # comandos (puerto 1338, sin FTP) y vigilar
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
    est = 'despierto hasta %s' % time.strftime('%H:%M', time.localtime(awake_until)) if awake_until else 'dormido'
    open(os.path.join(BASE, 'estado.txt'), 'w', encoding='utf-8').write('vivo %s, Vita %s (respondio %s), %s\n' % (
        time.strftime('%Y-%m-%d %H:%M:%S'), IP, vita_t, est))

def transfer(f, ups, reqs, watch):
    global watch_t, awake_until, first
    send = os.path.join(BASE, 'enviar')
    for local in ups:  # subidas
        rel = os.path.relpath(local, send).split(os.sep)
        remote = rel[0] + ':/' + '/'.join(rel[1:])
        for k in range(2, len(rel)):  # crea las carpetas que falten (ya existentes: error que se ignora)
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
    for name in reqs:  # pedidos de archivos, en orden de nombre
        path = os.path.join(BASE, name)
        if name.startswith('bajar_'):
            lines = [l for l in open(path, encoding='utf-8').read().splitlines() if l.strip()]
            os.remove(path)  # antes de bajar: si se corta, un archivo grande no se vuelve a pedir solo
            for line in lines:
                remote, _, dest = line.partition('->')
                remote = remote.strip()
                dest = os.path.expanduser(dest.strip()) if dest.strip() else os.path.join(BASE, 'recibidos', remote.split('/')[-1])
                try:  # un archivo que no existe no debe trabar el relevo
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
    # Lo nuevo de ux0:/pspemu: solo lo que dejo de crecer
    watch_t = time.time()
    files = listing(f, WATCH_DIR)
    if first:  # lo que ya esta en recibidos/ con el mismo tamano no se baja de nuevo
        for name, size in files.items():
            local = os.path.join(BASE, 'recibidos', name)
            if os.path.exists(local) and os.path.getsize(local) == size:
                seen[name] = size
        first = False
    for name, size in files.items():
        if not any(fnmatch.fnmatch(name, p) for p in PATTERNS) or seen.get(name) == size:
            continue
        awake_until = time.time() + AWAKE_FOR  # un log que crece o sin bajar: hay un juego en prueba
        if last_size.get(name) == size:  # igual que la vez anterior: el juego ya no lo escribe
            download(f, WATCH_DIR + '/' + name, os.path.join(BASE, 'recibidos', name))
            seen[name] = size
        last_size[name] = size

def local_nets():
    """Redes /24 de este Mac (por la interfaz con ruta a internet y las de los nombres locales)."""
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

def is_vita(ip):  # el puerto 1338 abierto no basta (VPN, otros equipos): debe responder vitacompanion
    try:
        return 'vitacompanion' in command('version', ip)
    except OSError:
        return False

def find_vita():
    last = os.path.join(BASE, 'ultima_ip.txt')
    if os.path.exists(last):  # primero la IP de la vez anterior
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
    nets = sorted(local_nets(), key=lambda n: not n.startswith('192.168.'))  # la red de la casa primero
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
    log('relevo en', BASE, 'con la Vita en', IP, '(dormido hasta que llegue un pedido)')
    nosleep('off')
    while True:
        try:
            step()
        except (OSError, EOFError, ftplib.Error) as e:
            log('error:', e, '(reintento en 5 s)')
            open(os.path.join(BASE, 'estado.txt'), 'w', encoding='utf-8').write(  # el latido sigue: la Vita no responde
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
