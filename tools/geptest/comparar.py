#!/usr/bin/env python3
"""Compara la bateria GEPTEST a tres bandas: PSP real (.expected de pspautotests), Vita
(resultados de la consola) y PPSSPP (PPSSPPHeadless con los mismos .prx).

  comparar.py LISTA PSPAUTOTESTS VITA PPSSPP [SALIDA.html]

LISTA: geptest/lista.txt. VITA y PPSSPP: carpetas con <id>.txt, <id>.bmp (y en la Vita
<id>.res: "ok/cuelga/caido N ms"); id = ruta con '_' (gpu_ge_edram).
Texto: se compara linea a linea sin el prefijo [x]/[r] (si hubo un cambio de hilo depende
del tiempo). Imagen: % de pixeles distintos (ignorando las 16 filas de abajo, donde la Vita
tiene la barra de estado) con tolerancia de 8 por canal (16 bits).
"""
import html, os, struct, sys

def lines(path):
    try:
        out = []
        for l in open(path, errors='replace').read().replace('\r', '').split('\n'):
            l = l.rstrip()
            if l.startswith('Screenshot saved to:'):  # lo agrega PPSSPPHeadless
                continue
            if l[:4] in ('[x] ', '[r] '):
                l = l[4:]
            out.append(l)
        while out and not out[-1]:
            out.pop()
        return out
    except OSError:
        return None

def bmp(path):
    """BMP de 32 o 24 bits -> (ancho, alto, filas de arriba a abajo de tuplas rgb)."""
    try:
        d = open(path, 'rb').read()
    except OSError:
        return None
    if d[:2] != b'BM':
        return None
    off, w, h, bpp = struct.unpack_from('<I', d, 10)[0], *struct.unpack_from('<iiHH', d, 18)[:2], struct.unpack_from('<H', d, 28)[0]
    bp = bpp // 8
    stride = (w * bp + 3) & ~3
    rows = []
    for y in range(abs(h)):
        r = d[off + y * stride: off + y * stride + w * bp]
        rows.append([(r[i + 2], r[i + 1], r[i]) for i in range(0, len(r), bp)])
    if h > 0:
        rows.reverse()
    return w, abs(h), rows

def img_diff(a, b, skip_bottom=16):
    if not a or not b:
        return None
    w, h = min(a[0], b[0], 480), min(a[1], b[1], 272) - skip_bottom
    bad = 0
    for y in range(h):
        ra, rb = a[2][y], b[2][y]
        for x in range(w):
            if max(abs(ra[x][i] - rb[x][i]) for i in range(3)) > 8:
                bad += 1
    return 100.0 * bad / (w * h)

def text_diff(a, b):
    """Lineas distintas (de las del primero) o None si falta alguno."""
    if a is None or b is None:
        return None
    n = max(len(a), len(b))
    return sum(1 for i in range(n) if (a[i] if i < len(a) else None) != (b[i] if i < len(b) else None))

def main(lista, auto, vita, pp, out_html=None):
    rows = []
    for line in open(lista):
        if not line.strip():
            continue
        t = line.split()[1]
        tid = t.replace('/', '_')
        exp = lines(os.path.join(auto, 'tests', t + '.expected'))
        v, p = lines(os.path.join(vita, tid + '.txt')), lines(os.path.join(pp, tid + '.txt'))
        res = open(os.path.join(vita, tid + '.res')).read().split()[0] if os.path.exists(os.path.join(vita, tid + '.res')) else '?'
        eb = bmp(os.path.join(auto, 'tests', t + '.expected.bmp'))
        vb, pb = bmp(os.path.join(vita, tid + '.bmp')), bmp(os.path.join(pp, tid + '.bmp'))
        rows.append(dict(t=t, res=res, n=len(exp or []),
                         vp=text_diff(exp, v), pp=text_diff(exp, p), vpp=text_diff(v, p),
                         ivp=img_diff(eb, vb) if eb else None, ipp=img_diff(eb, pb, 0) if eb else None,
                         ivpp=img_diff(vb, pb), exp=exp, v=v, p=p))
    f = lambda x: '-' if x is None else ('%.1f%%' % x if isinstance(x, float) else str(x))
    print('%-34s %-7s %5s | %-9s %-9s %-9s | %-7s %-7s %-7s' % ('prueba', 'vita', 'lin', 'PSP-Vita', 'PSP-PPSS', 'Vita-PPSS',
                                                               'img P-V', 'img P-PP', 'img V-PP'))
    for r in rows:
        print('%-34s %-7s %5d | %-9s %-9s %-9s | %-7s %-7s %-7s' % (r['t'][4:], r['res'], r['n'], f(r['vp']), f(r['pp']), f(r['vpp']),
                                                                   f(r['ivp']), f(r['ipp']), f(r['ivpp'])))
    ok = lambda x: x == 0
    print('\nresumen (texto): Vita = PSP en %d de %d; PPSSPP = PSP en %d; Vita = PPSSPP en %d' % (
        sum(ok(r['vp']) for r in rows), len(rows), sum(ok(r['pp']) for r in rows), sum(ok(r['vpp']) for r in rows)))
    print('estado en la Vita: ' + ', '.join('%s=%d' % (k, sum(r['res'] == k for r in rows)) for k in sorted({r['res'] for r in rows})))
    if out_html:
        with open(out_html, 'w') as o:
            o.write('<!doctype html><meta charset=utf-8><title>GEPTEST</title><style>body{font:13px monospace}'
                    'table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:2px 6px}.d{background:#fdd}</style>')
            o.write('<h1>GEPTEST: PSP real vs Vita vs PPSSPP</h1>')
            for r in rows:
                if r['vp'] == 0 and r['vpp'] == 0:
                    continue
                o.write('<h3>%s (%s)</h3><table><tr><th>#</th><th>PSP</th><th>Vita</th><th>PPSSPP</th></tr>' % (r['t'], r['res']))
                e, v, p = r['exp'] or [], r['v'] or [], r['p'] or []
                for i in range(max(len(e), len(v), len(p))):
                    c = [x[i] if i < len(x) else '' for x in (e, v, p)]
                    if c[0] == c[1] == c[2]:
                        continue
                    o.write('<tr><td>%d</td>%s</tr>' % (i + 1, ''.join('<td class=%s>%s</td>' % ('d' if k and c[k] != c[0] else '', html.escape(c[k])) for k in range(3))))
                o.write('</table>')
        print('detalle:', out_html)

if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:6]))
