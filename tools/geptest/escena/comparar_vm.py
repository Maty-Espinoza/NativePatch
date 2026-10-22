#!/usr/bin/env python3
"""Compara las capturas de la escena GEPVM: Vita contra el modelo (PPSSPP-Adrenaline).

  comparar_vm.py VITA MODELO [REFERENCIA] [informe.html]

VITA y MODELO: carpetas gepvm/ con <frame>_nativo.bmp, <frame>_psp.bmp y <frame>.txt (las
escribe kcap.prx). REFERENCIA (opcional): la escena sin GePatch (lo que deberia verse).
Por cada frame: % de pixeles distintos (tolerancia 8 por canal) en la imagen nativa y en la
del PSP, y si el .txt (DRAW_NATIVE, fb) coincide. El informe muestra las imagenes lado a lado
con un mapa de diferencias.
"""
import base64, io, os, sys
from PIL import Image, ImageChops

TOL = 8

def load(d, name):
    p = os.path.join(d, name) if d else None
    return Image.open(p).convert('RGB') if p and os.path.exists(p) else None

def diff(a, b):
    """(% de pixeles distintos, imagen de diferencias) o (None, None)."""
    if a is None or b is None:
        return None, None
    if a.size != b.size:
        b = b.resize(a.size, Image.NEAREST)
    d = ImageChops.difference(a, b)
    mask = d.point(lambda v: 255 if v > TOL else 0).convert('L').point(lambda v: 255 if v else 0)
    bad = mask.histogram()[255]
    return 100.0 * bad / (a.size[0] * a.size[1]), Image.merge('RGB', (mask, Image.new('L', a.size), Image.new('L', a.size)))

def png(img, w=None):
    if img is None:
        return '<i>falta</i>'
    if w and img.size[0] > w:
        img = img.resize((w, img.size[1] * w // img.size[0]), Image.NEAREST)
    b = io.BytesIO()
    img.save(b, 'PNG')
    return '<img src="data:image/png;base64,%s">' % base64.b64encode(b.getvalue()).decode()

def txt(d, f):
    p = os.path.join(d, f + '.txt')
    return open(p).read().strip() if os.path.exists(p) else None

def main(vita, modelo, ref=None, out=None):
    frames = sorted({n.split('_')[0].split('.')[0] for d in (vita, modelo) for n in os.listdir(d) if n[:3].isdigit()})
    rows = []
    print('%-6s %-10s %-10s %-10s %s' % ('frame', 'nativo', 'psp', 'ref-modelo', 'txt'))
    for f in frames:
        vn, mn = load(vita, f + '_nativo.bmp'), load(modelo, f + '_nativo.bmp')
        vp, mp = load(vita, f + '_psp.bmp'), load(modelo, f + '_psp.bmp')
        rp = load(ref, f + '_psp.bmp')
        dn, imn = diff(vn, mn)
        dp, imp = diff(vp, mp)
        dr, _ = diff(rp.resize((960, 544), Image.NEAREST) if rp else None, mn)
        tv, tm = txt(vita, f), txt(modelo, f)
        same = 'igual' if tv == tm else 'Vita: %s / modelo: %s' % (tv, tm)
        fmt = lambda x: '-' if x is None else '%.2f%%' % x
        print('%-6s %-10s %-10s %-10s %s' % (f, fmt(dn), fmt(dp), fmt(dr), same))
        rows.append((f, dn, dp, dr, same, vn, mn, imn, rp))
    if out:
        with open(out, 'w') as o:
            o.write('<!doctype html><meta charset=utf-8><title>GEPVM Vita vs modelo</title><style>'
                    'body{font:14px system-ui;margin:16px;background:#111;color:#ddd}img{max-width:100%%;image-rendering:pixelated}'
                    '.g{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:8px}h2{margin-top:32px}</style>'
                    '<h1>GEPVM: Vita contra modelo</h1><p>Imagen nativa (960x544) que muestra Adrenaline. '
                    'Rojo: pixeles que difieren en mas de %d por canal.</p>' % TOL)
            for f, dn, dp, dr, same, vn, mn, imn, rp in rows:
                o.write('<h2>frame %s: nativo %s distinto</h2><p>%s</p><div class=g>' % (f, '-' if dn is None else '%.2f%%' % dn, same))
                for t, im in (('Vita', vn), ('modelo', mn), ('diferencias', imn), ('sin GePatch (referencia)', rp)):
                    o.write('<div><b>%s</b><br>%s</div>' % (t, png(im, 480)))
                o.write('</div>')
        print('informe:', out)

if __name__ == '__main__':
    a = sys.argv[1:]
    sys.exit(main(a[0], a[1], a[2] if len(a) > 2 else None, a[3] if len(a) > 3 else None))
