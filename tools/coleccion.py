#!/usr/bin/env python3
"""Coleccion de capturas de prueba: un frame por escena y por juego, para ver de un vistazo si todo se dibuja bien.

  coleccion.py inventario CARPETA          juego, version, frame, pasivo/activo y draws de cada volcado .gpd
  coleccion.py armar LISTA CARPETA SALIDA [--ppsspp=PPSSPPHeadless] [--solo=ID,ID]
      LISTA (coleccion.txt): una escena por linea, campos separados por |
          id | juego | volcado activo | volcado pasivo (o -) | estado | descripcion
        estado: ok (se ve bien), bug (error conocido, se anota en la descripcion) o ? (sin revisar)
      Para cada escena deja en SALIDA/<id>/:
        con_gepatch.png   lo que dibujo GePatch (buffer grande al final del frame, a su escala)
        original.png      el frame sin GePatch: el fb del volcado PASIVO, o con --ppsspp el mismo frame del
                          volcado ACTIVO dibujado por PPSSPPHeadless (gpd.py original)
      y SALIDA/hoja.png (todas las escenas en miniatura: original | GePatch) y SALIDA/index.html.

La idea: cada version nueva se prueba volviendo a capturar las mismas escenas (ver coleccion.txt, "como llegar")
y armando la coleccion otra vez; una escena "ok" que deja de verse bien es una regresion.
Solo usa la biblioteca estandar de Python 3 (y gpd.py, en la misma carpeta).
"""
import os, sys, glob, shutil, struct, html
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpd

BIG_H = {960: 544, 864: 490, 768: 436, 720: 408, 672: 382, 576: 328}  # escalas de la opcion "escala"

def big_size(dump):
    """Ancho y alto del buffer grande del volcado (desde dev55 depende de la escala)."""
    for e in gpd.walk(dump):
        if e[0] == 'fb' and e[4] == 0 and e[6] in BIG_H:  # destino buffer grande: (dec, vram, ancho, escala)
            return e[6], BIG_H[e[6]]
    return 960, 544

def screen_fb(dump):
    """El fb de la pantalla: el de mas draws que van al buffer grande (o al ultimo flip, en un pasivo)."""
    fbs, cnt = {}, {}
    for e in gpd.walk(dump):
        if e[0] == 'fb':
            fbs[e[1]] = (e[2], e[3])
        elif e[0] == 'draw' and not e[1]['flags'] & 14 and e[1]['fb'] in fbs:
            cnt[e[1]['fb']] = cnt.get(e[1]['fb'], 0) + 1
    if not cnt:
        return None
    fb = max(cnt, key=cnt.get)
    return (fb,) + fbs[fb]

def rgba_big(dump):
    w, h = big_size(dump)
    m = dump.mem_at(2, 0x04000000, w * h * 2)
    return (w, h, gpd.to_rgba(m, 0, w, h, w)) if m else None

def rgba_passive(dump):
    s = screen_fb(dump)
    if not s:
        return None
    fb, pitch, fmt = s
    m = dump.mem_at(2, 0x04000000 + (fb & 0x3fffff), pitch * 272 * (4 if fmt == 3 else 2))
    px = gpd.to_rgba(m, fmt, 480, 272, pitch) if m else None
    return (480, 272, px) if px else None

def opaque(px):
    px = bytearray(px)
    px[3::4] = b'\xff' * (len(px) // 4)
    return px

def resize(px, w, h, nw, nh):
    """Vecino mas cercano (miniaturas y comparacion a 480x272)."""
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        sy = y * h // nh
        for x in range(nw):
            i = (sy * w + x * w // nw) * 4
            out[(y * nw + x) * 4:(y * nw + x) * 4 + 4] = px[i:i + 4]
    return out

def read_png(path):
    """PNG RGBA de 8 bits sin entrelazar (los que escribe gpd.py)."""
    import zlib
    data = open(path, 'rb').read()
    pos, w, h, idat = 8, 0, 0, b''
    while pos < len(data):
        n, t = struct.unpack_from('>I4s', data, pos)
        d = data[pos + 8:pos + 8 + n]
        if t == b'IHDR':
            w, h = struct.unpack_from('>II', d)
        elif t == b'IDAT':
            idat += d
        pos += 12 + n
    raw, out, stride, prev = zlib.decompress(idat), bytearray(), w * 4, bytearray(w * 4)
    for y in range(h):
        f, row = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = row[i - 4] if i >= 4 else 0
            b = prev[i]; c = prev[i - 4] if i >= 4 else 0
            if f == 1: row[i] = (row[i] + a) & 255
            elif f == 2: row[i] = (row[i] + b) & 255
            elif f == 3: row[i] = (row[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += row; prev = row
    return w, h, out

def pct_diff(a, b):
    """% de pixeles (a 480x272) con alguna componente a mas de 24 de distancia."""
    bad = sum(1 for i in range(0, len(a) // 4) if max(abs(a[4 * i + c] - b[4 * i + c]) for c in range(3)) > 24)
    return 100.0 * bad / (len(a) // 4)

def read_list(path):
    rows = []
    for line in open(path, encoding='utf-8'):
        line = line.split('#', 1)[0].strip()
        if line:
            f = [x.strip() for x in line.split('|')]
            rows.append(dict(zip(['id', 'juego', 'activo', 'pasivo', 'estado', 'desc'], f + [''] * (6 - len(f)))))
    return rows

def armar(lista, carpeta, salida, pp=None, solo=None):
    os.makedirs(salida, exist_ok=True)
    rows, thumbs = read_list(lista), []
    for r in rows:
        if solo and r['id'] not in solo:
            continue
        d = os.path.join(salida, r['id'])
        os.makedirs(d, exist_ok=True)
        r['dif'] = None
        cg = og = None
        act = os.path.join(carpeta, r['activo']) if r['activo'] not in ('', '-') else None
        pas = os.path.join(carpeta, r['pasivo']) if r['pasivo'] not in ('', '-') else None
        if act and os.path.exists(act):
            da = gpd.Dump(act)
            r['version'], r['frame'] = da.plugin, da.frame
            b = rgba_big(da)
            if b:
                gpd.write_png(os.path.join(d, 'con_gepatch.png'), b[0], b[1], opaque(b[2]))
                cg = resize(opaque(b[2]), b[0], b[1], 480, 272)
            if pp and not pas:
                if gpd.original(da, d, pp) == 0 and os.path.exists(os.path.join(d, 'sin_gepatch.png')):
                    shutil.copy(os.path.join(d, 'sin_gepatch.png'), os.path.join(d, 'original.png'))
        if pas and os.path.exists(pas):
            p = rgba_passive(gpd.Dump(pas))
            if p:
                gpd.write_png(os.path.join(d, 'original.png'), 480, 272, opaque(p[2]))
        if os.path.exists(os.path.join(d, 'original.png')):
            ow, oh, og = read_png(os.path.join(d, 'original.png'))
            og = resize(og, ow, oh, 480, 272)
        if cg and og and not pas:  # mismo frame: la diferencia dice algo (con un pasivo es otro frame)
            r['dif'] = pct_diff(og, cg)
        thumbs.append((r, og, cg))
        print('%-14s %-10s %-6s %s%s' % (r['id'], r['juego'], r['estado'], 'GePatch ' if cg else '', 'original' if og else ''))
    # Hoja: por escena, original | GePatch a 240x136, dos escenas por fila
    tw, th, cols = 240, 136, 2
    W, H = cols * (2 * tw + 8), ((len(thumbs) + cols - 1) // cols) * (th + 8)
    sheet = bytearray(b'\x20\x20\x20\xff' * (W * H))
    for k, (r, og, cg) in enumerate(thumbs):
        x0, y0 = (k % cols) * (2 * tw + 8), (k // cols) * (th + 8)
        for j, img in enumerate((og, cg)):
            if img is None:
                continue
            t = resize(img, 480, 272, tw, th)
            for y in range(th):
                o = ((y0 + y) * W + x0 + j * tw) * 4
                sheet[o:o + tw * 4] = t[y * tw * 4:(y + 1) * tw * 4]
        col = {'ok': b'\x30\xc0\x30\xff', 'bug': b'\xe0\x30\x30\xff'}.get(r['estado'], b'\xe0\xc0\x30\xff')
        for y in range(th):  # franja de color del estado
            o = ((y0 + y) * W + x0) * 4
            sheet[o:o + 12] = col * 3
    gpd.write_png(os.path.join(salida, 'hoja.png'), W, H, sheet)
    with open(os.path.join(salida, 'index.html'), 'w', encoding='utf-8') as f:
        f.write('<!doctype html><meta charset="utf-8"><title>Coleccion GePatch</title><style>body{font:14px sans-serif;'
                'background:#1e1e1e;color:#ddd}td{padding:6px;vertical-align:top}img{width:480px}.ok{color:#5c5}.bug{color:#e55}'
                '</style><h1>Coleccion de capturas</h1><p>Original (sin GePatch) | GePatch. Verde: se ve bien; rojo: error'
                ' conocido; amarillo: sin revisar.</p><table>')
        for r, og, cg in thumbs:
            dif = '' if r['dif'] is None else '<br>%.1f%% de pixeles distintos' % r['dif']
            f.write('<tr><td><b>%s</b><br>%s<br><span class="%s">%s</span><br>%s frame %s%s<br>%s</td><td>%s</td><td>%s</td></tr>' % (
                html.escape(r['id']), html.escape(r['juego']), html.escape(r['estado']), html.escape(r['estado']),
                html.escape(r.get('version', '')), r.get('frame', ''), dif, html.escape(r['desc']),
                '<img src="%s/original.png">' % r['id'] if og else '(sin original)',
                '<img src="%s/con_gepatch.png">' % r['id'] if cg else '(sin captura)'))
        f.write('</table>')
    print('hoja: %s  pagina: %s' % (os.path.join(salida, 'hoja.png'), os.path.join(salida, 'index.html')))

def inventario(carpeta):
    for f in sorted(glob.glob(os.path.join(carpeta, '*.gpd'))):
        try:
            d = gpd.Dump(f)
            nd = sum(1 for t, x in d.recs if t == gpd.DRAW)
            print('%s %s %s frame=%d %s draws=%d' % (os.path.basename(f), d.game, d.plugin, d.frame,
                                                     'PASIVO' if d.passive else 'activo', nd))
        except Exception as e:
            print(os.path.basename(f), 'ERROR', e)

def main(a):
    opts = {k[2:]: v for k, _, v in (x.partition('=') for x in a if x.startswith('--'))}
    a = [x for x in a if not x.startswith('--')]
    if len(a) == 2 and a[0] == 'inventario':
        return inventario(a[1])
    if len(a) == 4 and a[0] == 'armar':
        return armar(a[1], a[2], a[3], opts.get('ppsspp'), set(opts['solo'].split(',')) if 'solo' in opts else None)
    print(__doc__)
    return 1

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]) or 0)
