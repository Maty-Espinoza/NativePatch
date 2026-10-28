#!/usr/bin/env python3
"""Relevo entre Claude y la PS Vita (correr en la Terminal del Mac o en Windows, en la misma red que la Vita).
  En Windows: doble clic en vitabridge.cmd (usa el Python de Windows; en WSL la busqueda automatica de la Vita no funciona).

  python3 vitabridge.py [IP_DE_LA_VITA] [CARPETA]    (CARPETA: ~/Downloads/vitabridge)
  Sin IP (o con "buscar") busca la Vita en la red local: el puerto 1338 de vitacompanion.

En la Vita: vitacompanion (FTP en 1337 y comandos en 1338: launch, press, wait, kill...).
Claude escribe en CARPETA y el relevo hace el resto:
  enviar/ux0/pspemu/seplugins/ge_patch.prx  -> se sube a ux0:/pspemu/seplugins/ge_patch.prx
  cmd_*.txt   -> cada linea va al puerto 1338; la respuesta queda en respuestas/cmd_*.txt
  bajar_*.txt -> cada linea es una ruta de la Vita (ux0:/...) que se baja a recibidos/, o
                 "ruta -> destino" para dejarla en otra carpeta del Mac (p. ej. ~/Documents/...)
  listar_*.txt -> cada linea es una carpeta de la Vita; el listado queda en respuestas/
  Ademas baja solo lo nuevo de ux0:/pspemu: ge_patch*.log, *.gpd y gepatch_*.png
  estado.txt  -> latido (hora) para saber que el relevo esta vivo
Solo usa la biblioteca estandar. Ctrl+C para terminar.
"""
import ftplib, os, socket, sys, time, fnmatch, shutil
from concurrent.futures import ThreadPoolExecutor

IP = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] != 'buscar' else None
BASE = os.path.expanduser(sys.argv[2] if len(sys.argv) > 2 else '~/Downloads/vitabridge')
WATCH_DIR, PATTERNS = 'ux0:/pspemu', ['ge_patch*.log', '*.gpd', 'gepatch_*.png']
seen = {}  # archivo de la Vita -> tamano ya bajado

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

def step(f):
    # Subidas
    send = os.path.join(BASE, 'enviar')
    for root, _, files in os.walk(send):
        for name in files:
            if name.startswith('.') or name.lower() in ('desktop.ini', 'thumbs.db'):
                continue  # basura del Finder/Explorador: subirla traba el relevo
            local = os.path.join(root, name)
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
    # Comandos y pedidos de archivos, en orden de nombre
    for name in sorted(os.listdir(BASE)):
        path = os.path.join(BASE, name)
        if fnmatch.fnmatch(name, 'cmd_*.txt'):
            res = []
            for line in open(path, encoding='utf-8').read().splitlines():
                if line.strip():
                    log('comando', line)
                    res.append('> ' + line + '\n' + command(line))
            os.makedirs(os.path.join(BASE, 'respuestas'), exist_ok=True)
            open(os.path.join(BASE, 'respuestas', name), 'w', encoding='utf-8').write('\n'.join(res))
            os.remove(path)
        elif fnmatch.fnmatch(name, 'bajar_*.txt'):
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
        elif fnmatch.fnmatch(name, 'listar_*.txt'):
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
    # Lo nuevo de ux0:/pspemu (logs, volcados, capturas)
    for name, size in listing(f, WATCH_DIR).items():
        if any(fnmatch.fnmatch(name, p) for p in PATTERNS) and seen.get(name) != size:
            download(f, WATCH_DIR + '/' + name, os.path.join(BASE, 'recibidos', name))
            seen[name] = size
    open(os.path.join(BASE, 'estado.txt'), 'w', encoding='utf-8').write('vivo %s, Vita %s\n' % (time.strftime('%Y-%m-%d %H:%M:%S'), IP))

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
    log('relevo en', BASE, 'con la Vita en', IP)
    f = None
    first = True
    while True:
        try:
            if f is None:
                f = ftp()
                log('FTP conectado')
                if first:  # lo que ya esta en recibidos/ con el mismo tamano no se baja de nuevo
                    for name, size in listing(f, WATCH_DIR).items():
                        local = os.path.join(BASE, 'recibidos', name)
                        if os.path.exists(local) and os.path.getsize(local) == size:
                            seen[name] = size
                    first = False
            step(f)
        except (OSError, EOFError, ftplib.Error) as e:
            log('error:', e, '(reintento en 5 s)')
            try:
                f and f.close()
            except Exception:
                pass
            f = None
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
