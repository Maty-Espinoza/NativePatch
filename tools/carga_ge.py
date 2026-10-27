#!/usr/bin/env python3
"""Que parte del trabajo del GE podria tomar la GPU de la Vita (analisis sobre volcados .gpd).

  carga_ge.py VOLCADO [VOLCADO...] [--detalle]     resumen por volcado; con varios, tabla final

Para cada frame clasifica los draws que van al buffer grande y estima el area que rellena cada uno (px a escala 1,
transformando los vertices como el GE: mundo, vista, proyeccion y viewport; a 2x son 4 veces mas):
  - "cuerpo": hasta el ultimo draw con prueba de profundidad del frame. Comparte el z-buffer, que esta en la VRAM
    real que el ARM no ve: solo puede dibujarlo el GE;
  - "cola": lo que viene despues (HUD, menus, fundidos), sin z. Es lo unico que la Vita podria dibujar encima de la
    imagen que ya presenta, sin tocar el GE.
De la cola dice de que depende cada draw (textura en la RAM o en la VRAM falsa, formato, CLUT, swizzle, mezcla, prueba
de alfa, funcion de textura, proyeccion ortogonal o perspectiva). Usa gpd.py (misma carpeta)."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpd

PANT = 480 * 272
TEXFMT = ['5650', '5551', '4444', '8888', 'clut4', 'clut8', 'clut16', 'clut32', 'dxt1', 'dxt3', 'dxt5']
TEXFUNC = ['modula', 'calca', 'mezcla', 'reemplaza', 'suma']

def f24(d): return struct.unpack('<f', struct.pack('<I', (d & 0xffffff) << 8))[0]

def clip_poly(pts, w, h):
    """Recorta un poligono (x, y) al rectangulo 0..w, 0..h (Sutherland-Hodgman)."""
    def clip(pts, inside, inter):
        out = []
        for i, p in enumerate(pts):
            q = pts[i - 1]
            if inside(p):
                if not inside(q): out.append(inter(q, p))
                out.append(p)
            elif inside(q):
                out.append(inter(q, p))
        return out
    def ix(x0): return lambda q, p: (x0, q[1] + (p[1] - q[1]) * (x0 - q[0]) / (p[0] - q[0]))
    def iy(y0): return lambda q, p: (q[0] + (p[0] - q[0]) * (y0 - q[1]) / (p[1] - q[1]), y0)
    for inside, inter in ((lambda p: p[0] >= 0, ix(0)), (lambda p: p[0] <= w, ix(w)),
                          (lambda p: p[1] >= 0, iy(0)), (lambda p: p[1] <= h, iy(h))):
        pts = clip(pts, inside, inter)
        if len(pts) < 3: return []
    return pts

def poly_area(pts):
    a = 0
    for i in range(len(pts)):
        x0, y0 = pts[i - 1]; x1, y1 = pts[i]
        a += x0 * y1 - x1 * y0
    return abs(a) / 2

SC = [0, 0, 480, 272]  # scissor vigente (x0 y0 x1 y1), lo pone analizar()
def clip_sc(pts):
    out = clip_poly([(x - SC[0], y - SC[1]) for x, y in pts], SC[2] - SC[0], SC[3] - SC[1])
    return out
def tri_area(p): return poly_area(clip_sc(list(p)))

class Xform:
    """Matrices y viewport del GE para llevar un vertice 3D a pixeles."""
    def __init__(self):
        self.m = {0x3A: [0.0] * 12, 0x3C: [0.0] * 12, 0x3E: [0.0] * 16}
        self.n = {0x3A: 0, 0x3C: 0, 0x3E: 0}
        self.vp = [1.0, 1.0, 1.0, 0.0, 0.0, 0.0]  # xs ys zs xc yc zc (0x42-0x47)
        self.off = [0, 0]
    def cmd(self, c, d):
        if c in self.n: self.n[c] = d % len(self.m[c])
        elif c - 1 in self.n:
            m = self.m[c - 1]; m[self.n[c - 1]] = f24(d); self.n[c - 1] = (self.n[c - 1] + 1) % len(m)
        elif 0x42 <= c <= 0x47: self.vp[c - 0x42] = f24(d)
        elif c in (0x4C, 0x4D): self.off[c - 0x4C] = (d & 0xffff) / 16.0
    def ortho(self):
        p = self.m[0x3E]
        return abs(p[3]) < 1e-6 and abs(p[7]) < 1e-6 and abs(p[11]) < 1e-6 and abs(p[15] - 1) < 1e-3
    def apply(self, v):
        w, vw, p = self.m[0x3A], self.m[0x3C], self.m[0x3E]
        x, y, z = v
        x, y, z = (w[0] * x + w[3] * y + w[6] * z + w[9], w[1] * x + w[4] * y + w[7] * z + w[10],
                   w[2] * x + w[5] * y + w[8] * z + w[11])
        x, y, z = (vw[0] * x + vw[3] * y + vw[6] * z + vw[9], vw[1] * x + vw[4] * y + vw[7] * z + vw[10],
                   vw[2] * x + vw[5] * y + vw[8] * z + vw[11])
        cx, cy, cw = (p[0] * x + p[4] * y + p[8] * z + p[12], p[1] * x + p[5] * y + p[9] * z + p[13],
                      p[3] * x + p[7] * y + p[11] * z + p[15])
        if cw <= 1e-6: return None
        return (self.vp[3] + self.vp[0] * cx / cw - self.off[0], self.vp[4] + self.vp[1] * cy / cw - self.off[1])

def vertices(dr, blobs, xf, through):
    """Posiciones en pixeles (o None) de los vertices que usa el draw, en el orden del draw."""
    verts = next((b for w, a, b in blobs if w == gpd.B_VERTS), None)
    if not verts: return None
    vt, n = dr['vt'], dr['op'] & 0xffff
    size, po, ps = gpd.vlayout(vt)
    if not size or not ps: return None
    it = (vt >> 11) & 3
    if it:
        idx = next((b for w, a, b in blobs if w == gpd.B_INDICES), None)
        if not idx or it == 3: return None
        f = 'B' if it == 1 else 'H'
        ids = struct.unpack_from('<%d%s' % (min(n, len(idx) // it), f), idx)
    else:
        ids = range(n)
    out = []
    for i in ids:
        p = i * size + po
        if p + 3 * ps > len(verts): return None
        if ps == 4: x, y, z = struct.unpack_from('<3f', verts, p)
        elif ps == 2:
            x, y, z = struct.unpack_from('<3h', verts, p)
            if not through: x, y, z = x / 32768.0, y / 32768.0, z / 32768.0
        else:
            x, y, z = struct.unpack_from('<3b', verts, p)
            if not through: x, y, z = x / 128.0, y / 128.0, z / 128.0
        out.append((x, y) if through else xf.apply((x, y, z)))
    return out

def area(prim, pts):
    a = 0
    if prim == 6:
        for i in range(0, len(pts) - 1, 2):
            p, q = pts[i], pts[i + 1]
            if p and q: a += poly_area(clip_sc([p, (q[0], p[1]), q, (p[0], q[1])]))
    elif prim == 3:
        for i in range(0, len(pts) - 2, 3):
            if all(pts[i:i + 3]): a += tri_area(pts[i:i + 3])
    elif prim == 4:
        for i in range(len(pts) - 2):
            if all(pts[i:i + 3]): a += tri_area(pts[i:i + 3])
    elif prim == 5:
        for i in range(1, len(pts) - 1):
            t = [pts[0], pts[i], pts[i + 1]]
            if all(t): a += tri_area(t)
    return a

def analizar(path, detalle=False):
    d = gpd.Dump(path)
    regs, xf, main, rts, sin_area, detras = {}, Xform(), [], 0, 0, [0]
    r0 = d.regs(0)  # registros y matrices al empezar el frame (lo que el juego dejo de frames anteriores)
    if r0:
        for c, v in enumerate(r0['ge']):
            regs[c] = v & 0xffffff
            if 0x42 <= c <= 0x47 or c in (0x4C, 0x4D): xf.cmd(c, v & 0xffffff)
        xf.m[0x3A], xf.m[0x3C], xf.m[0x3E] = list(r0['mtx'][96:108]), list(r0['mtx'][108:120]), list(r0['mtx'][120:136])
    for e in gpd.walk(d):
        if e[0] == 'cmd':
            c, v = e[2] >> 24, e[2] & 0xffffff
            regs[c] = v
            xf.cmd(c, v)
        elif e[0] == 'draw':
            dr, blobs = e[1], e[2]
            f = dr['flags']
            if f & 2: continue
            if f & 12: rts += 1; continue
            r = regs
            clear = r.get(0xD3, 0) & 1
            through = bool(f & 1)
            info = dict(idx=dr['idx'], through=through, clear=clear, omit=dr['omit'], texmode=dr['texmode'],
                        z=bool(r.get(0x23, 0) & 1) and not clear, blend=bool(r.get(0x21, 0) & 1),
                        atest=bool(r.get(0x22, 0) & 1), stencil=bool(r.get(0x24, 0) & 1), n=dr['op'] & 0xffff,
                        prim=(dr['op'] >> 16) & 7, tex=bool(r.get(0x1E, 0) & 1), texfmt=r.get(0xC3, 0) & 0xf,
                        swz=r.get(0xC2, 0) & 1, texfunc=r.get(0xC9, 0) & 7, ortho=through or xf.ortho(),
                        texaddr=((r.get(0xA8, 0) & 0xff0000) << 8) | r.get(0xA0, 0), texw=1 << (r.get(0xB8, 0) & 0xf), texh=1 << ((r.get(0xB8, 0) >> 8) & 0xf), skin=((dr['vt'] >> 9) & 3) != 0,
                        light=bool(r.get(0x17, 0) & 1), regs=dict(r) if detalle else None)
            SC[0], SC[1] = r.get(0xD4, 0) & 0x3ff, (r.get(0xD4, 0) >> 10) & 0x3ff
            SC[2], SC[3] = min(480, (r.get(0xD5, 0) & 0x3ff) + 1), min(272, ((r.get(0xD5, 0) >> 10) & 0x3ff) + 1)
            if SC[2] <= SC[0] or SC[3] <= SC[1]: SC[:] = [0, 0, 480, 272]
            info['sc'] = tuple(SC)
            if clear:
                info['area'] = (SC[2] - SC[0]) * (SC[3] - SC[1])
            else:
                pts = vertices(dr, blobs, xf, through)
                info['area'] = area(info['prim'], pts) if pts else None
                if pts is None: sin_area += 1
                elif detalle and not through and pts and all(p is None for p in pts) and detras[0] < 3:
                    detras[0] += 1
                    print('    (draw #%d: todos los vertices con w<=0; proj %s vista %s)' % (dr['idx'], ['%.3g' % v for v in xf.m[0x3E]], ['%.3g' % v for v in xf.m[0x3C]]))
            main.append(info)
    last_z = max([i for i, m in enumerate(main) if m['z']], default=-1)
    cuerpo, cola = main[:last_z + 1], [m for m in main[last_z + 1:] if not m['clear']]
    def suma(ms): return sum(m['area'] or 0 for m in ms)
    res = dict(file=os.path.basename(path), game=d.game, plugin=d.plugin, pasivo=d.passive, frame=d.frame, draws=len(main),
               rt=rts, z=sum(1 for m in main if m['z']), clears=sum(1 for m in main if m['clear']),
               area=suma(main), area_clears=suma([m for m in main if m['clear']]), area_cuerpo=suma([m for m in cuerpo if not m['clear']]), area_cola=suma(cola),
               area_cuerpo_noz=suma([m for m in cuerpo if not m['z'] and not m['clear']]),
               cola=len(cola), cola_fb=sum(1 for m in cola if m['texmode']), cola_omit=sum(1 for m in cola if m['omit']),
               cola_persp=sum(1 for m in cola if not m['ortho']), sin_area=sin_area)
    print('%s: %s GePatch %s frame %d%s' % (res['file'], d.game, d.plugin, d.frame, ' PASIVO' if d.passive else ''))
    print('  draws al buffer grande: %d (con z %d, clears %d, through %d, sin area calculable %d); a render targets: %d' % (
        res['draws'], res['z'], res['clears'], sum(1 for m in main if m['through']), sin_area, rts))
    print('  relleno estimado: %.1f pantallas = clears %.1f + cuerpo %.1f (de el, sin z %.1f) + cola %.2f' % (
        res['area'] / PANT, res['area_clears'] / PANT, res['area_cuerpo'] / PANT, res['area_cuerpo_noz'] / PANT, res['area_cola'] / PANT))
    print('  cola (despues del ultimo draw con z): %d draws, %.2f pantallas (%.0f%% del relleno sin clears); %d leen un fb, %d omitidos, %d con perspectiva' % (
        res['cola'], res['area_cola'] / PANT, 100.0 * res['area_cola'] / max(1, res['area'] - res['area_clears']),
        res['cola_fb'], res['cola_omit'], res['cola_persp']))
    kinds = {}
    for m in cola:
        src = ('VRAMfalsa' if 0x04000000 <= m['texaddr'] < 0x04800000 else 'RAM') if m['tex'] else 'sin textura'
        k = (src, TEXFMT[m['texfmt']] if m['tex'] and m['texfmt'] < 11 else '-', 'swz' if m['tex'] and m['swz'] else 'lin',
             TEXFUNC[m['texfunc']] if m['tex'] and m['texfunc'] < 5 else '-', 'mezcla' if m['blend'] else '-',
             'atest' if m['atest'] else '-', 'stencil' if m['stencil'] else '-', 'persp' if not m['ortho'] else 'orto',
             gpd.PRIMS[m['prim']])
        kinds.setdefault(k, [0, 0]); kinds[k][0] += 1; kinds[k][1] += m['area'] or 0
    for k, (n, a) in sorted(kinds.items(), key=lambda x: -x[1][1])[:15]:
        print('    %4d draws %5.2f pant.  %s' % (n, a / PANT, ' '.join(k)))
    grandes = sorted([m for m in cuerpo if not m['clear'] and (m['area'] or 0) > PANT / 3], key=lambda m: -(m['area'] or 0))
    if grandes:
        print('  draws grandes del cuerpo (> 1/3 de pantalla): ' + ', '.join(
            '#%d %.2f%s%s%s' % (m['idx'], (m['area'] or 0) / PANT, ' z' if m['z'] else '', ' fb' if m['texmode'] else '',
                               ' ' + TEXFMT[m['texfmt']] if m['tex'] and m['texfmt'] < 11 else '') for m in grandes[:10]))
    if detalle:
        for m in [x for x in main if x['clear']] + grandes[:6]:
            print('    cuerpo/clear #%d %s n=%d area %.3f D3=%06x z=%d tex=%s sc=%s' % (m['idx'], gpd.PRIMS[m['prim']], m['n'],
                  (m['area'] or 0) / PANT, m['regs'].get(0xD3, 0), m['z'], '%08x %s %dx%d %s' % (m['texaddr'], TEXFMT[m['texfmt']] if m['texfmt'] < 11 else '?',
                  m['texw'], m['texh'], 'swz' if m['swz'] else 'lin') if m['tex'] else '-', m['sc']))
        for m in cola:
            print('    #%d %s n=%d area %.3f %s %s %s sc=%s' % (m['idx'], gpd.PRIMS[m['prim']], m['n'], (m['area'] or 0) / PANT,
                  'tex %08x %s %dx%d %s' % (m['texaddr'], TEXFMT[m['texfmt']] if m['texfmt'] < 11 else '?', m['texw'], m['texh'], 'swz' if m['swz'] else 'lin') if m['tex'] else 'sin tex', 'orto' if m['ortho'] else 'persp',
                  ' '.join('%s=%06x' % (gpd.cname(c), m['regs'].get(c, 0)) for c in (0xDC, 0xDD, 0xE8, 0xE9, 0xDF, 0xDB, 0xD3, 0xDE, 0xE7)), m['sc']))
    return res

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    rs = [analizar(p, '--detalle' in sys.argv) for p in args]
    if len(rs) > 1:
        print('\n%-30s %-4s %5s %5s %6s %7s %7s %5s %7s %5s' % ('volcado', 'jue', 'draws', 'con z', 'clears', 'cuerpo', 'noZ', 'cola', 'areaC', '%cola'))
        for r in rs:
            print('%-30s %-4s %5d %5d %6d %7.1f %7.1f %5d %7.2f %5.0f' % (r['file'][8:30], r['game'][:4], r['draws'], r['z'], r['clears'],
                  r['area_cuerpo'] / PANT, r['area_cuerpo_noz'] / PANT, r['cola'], r['area_cola'] / PANT,
                  100.0 * r['area_cola'] / max(1, r['area'] - r['area_clears'])))
