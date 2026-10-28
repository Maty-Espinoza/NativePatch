#!/usr/bin/env python3
"""Etapa 0 de la arquitectura B (claude/arquitectura_dibujo_en_vita.md): cuanto trabajo tendria la Vita si dibujara
ella cada frame del juego. Sale de los volcados .gpd que ya hay; no necesita la Vita.

  carga_vita.py VOLCADO [VOLCADO...] [--detalle]   un bloque por volcado; con varios, tabla final por juego

Solo cuenta lo que mando el JUEGO (valor original de cada comando; los parches del plugin no cuentan). Todo el volcado
es una unidad y las cifras se dividen por los flips (SH dibuja casi todo su frame despues del flip, en la misma lista):
  - draws (primitiva, 2D/3D, clears) y comandos;
  - cambios de estado reales antes de cada draw (valor distinto al vigente), agrupados como los pagaria GXM:
    destino (escena nueva), programa/estado fijo, textura, constantes (matrices, viewport, colores, luces...);
  - texturas distintas (direccion, tamano, formato, CLUT): cantidad, KB, formatos, swizzle, RAM o VRAM;
  - KB de vertices e indices (sumando cada draw y sin repetir), cargas de CLUT, copias de bloque (TRANSFER);
  - de donde sale cada textura que esta en la VRAM (que el ARM no ve): fb o z dibujado antes en el frame, TRANSFER
    del frame, fb que el juego dibuja en otro momento (lo tendria el administrador de fb), sin cambios en todo el
    volcado (subida antes: espejo una vez) o desconocido (cambia sin que el GE la escriba: CPU o DMA);
  - datos del juego que viven en la VRAM: listas, vertices, indices, CLUT (la Vita tampoco los ve);
  - triangulos 3D que el GE descarta por el guard band (algun vertice fuera de 0..4096) a 1x y a 2x.
En un volcado ACTIVO el juego ve su VRAM en la VRAM falsa (0x0A000000-0x0A3FFFFF): ahi se cuenta como VRAM."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpd, carga_ge

# Grupos de registros segun lo que costarian en GXM (el resto: programa o estado fijo)
FLOW = {0, 1, 2, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xC, 0xE, 0xF, 0x10, 0x13, 0x14, 0xC4, 0xCB, 0xCC,
        0xB2, 0xB3, 0xB4, 0xB5, 0xEA, 0xEB, 0xEC, 0xEE}
MTX = {0x2A, 0x2B} | set(range(0x3A, 0x42))
RT = {0x9C, 0x9D, 0x9E, 0x9F, 0xD2}
TEX = set(range(0xA0, 0xB2)) | set(range(0xB8, 0xC0)) | {0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC8, 0xD0}
UNIF = (set(range(0x42, 0x4E)) | set(range(0x2C, 0x34)) | set(range(0x54, 0x5E)) | set(range(0x63, 0x9B)) |
        set(range(0xD4, 0xD8)) | set(range(0xE0, 0xE6)) | set(range(0xF0, 0xFA)) | {0x15, 0x16, 0xCA, 0xCD, 0xCE, 0xCF, 0xD9, 0xDA})
def grupo(c): return 'mtx' if c in MTX else 'rt' if c in RT else 'tex' if c in TEX else 'cte' if c in UNIF else 'prog'

TEXBITS = [16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8]
TEXFMT = carga_ge.TEXFMT
PRIMS = ['puntos', 'lineas', 'tira_lin', 'tri', 'tira_tri', 'abanico', 'rect', '?']

def vram(a, activo):
    """Desplazamiento en la VRAM que ve el juego, o None si a esta en la RAM."""
    a &= 0x0fffffff
    if 0x04000000 <= a < 0x04800000 or (activo and 0x0A000000 <= a < 0x0A400000):
        return a & 0x3fffff
    return None

def toca(rs, lo, hi): return any(a < hi and lo < b for a, b in rs)

def analizar(path, detalle=False):
    d = gpd.Dump(path)
    act = not d.passive
    regs, xf, mt = {}, carga_ge.Xform(), {}
    r0 = d.regs(0)
    if r0:
        for c, v in enumerate(r0['shadow'] if act and any(r0['shadow']) else r0['ge']):
            regs[c] = v & 0xffffff
            if 0x42 <= c <= 0x47 or c in (0x4C, 0x4D): xf.cmd(c, v & 0xffffff)
        xf.m[0x3A], xf.m[0x3C], xf.m[0x3E] = list(r0['mtx'][96:108]), list(r0['mtx'][108:120]), list(r0['mtx'][120:136])
    # Todo lo que el GE dibuja en el volcado (para "fb que el juego dibuja en otro momento")
    fb_todos = []
    ini_k, fin_k, base = (1, 3, 0x0A000000) if act else (0, 2, 0x04000000)

    frames, F = [], None
    def nuevo():
        return dict(cmds=0, cmds_estado=0, cambios=0, draws=0, prims={}, through=0, clears=0, listas=0, escenas=0,
                    con={'rt': 0, 'prog': 0, 'tex': 0, 'cte': 0, 'mtx': 0}, sin_cambio=0, tex={}, vkb=0, vrangos=[],
                    ikb=0, clut=0, clutkb=0, xfer=0, xferkb=0, origen={}, enVram={'listas': 0, 'vert': 0, 'ind': 0, 'clut': 0},
                    gb1=0, gb2=0, tris=0, cerca=0, sin_pos=0, nada=0, vdat=[], escritos=[], zs=[], xfers=[], flips=0, desconocidas=[])
    F, pend, fb_prev = nuevo(), set(), None
    for e in gpd.walk(d):
        k = e[0]
        if k == 'flip':  # no se corta: SH dibuja casi todo su frame despues del flip, en la misma lista
            F['flips'] += 1
        elif k == 'list':
            F['listas'] += 1
        elif k == 'cmd':
            a, o, late = e[1], e[2], e[4]
            if late: continue
            c, v = o >> 24, o & 0xffffff
            F['cmds'] += 1
            if vram(a, act) is not None: F['enVram']['listas'] += 1; F['vdat'].append((vram(a, act), vram(a, act) + 4))
            if c == 0xEA:  # TRANSFER: destino y origen como los arma el GE
                bpp = 4 if v & 1 else 2
                sw, dw = regs.get(0xB3, 0) & 0x7ff, regs.get(0xB5, 0) & 0x7ff
                src = ((regs.get(0xB3, 0) & 0xff0000) << 8) | regs.get(0xB2, 0)
                dst = ((regs.get(0xB5, 0) & 0xff0000) << 8) | regs.get(0xB4, 0)
                sz = regs.get(0xEE, 0); w, h = (sz & 0x3ff) + 1, ((sz >> 10) & 0x3ff) + 1
                dp = regs.get(0xEC, 0); dx, dy = dp & 0x3ff, (dp >> 10) & 0x3ff
                F['xfer'] += 1; F['xferkb'] += w * h * bpp / 1024.0
                vo = vram(dst, act)
                if vo is not None:
                    F['xfers'].append((vo + (dy * dw + dx) * bpp, vo + ((dy + h - 1) * dw + dx + w) * bpp))
                continue
            if c == 0xC4 and v & 0x3f:
                F['clut'] += 1; F['clutkb'] += (v & 0x3f) * 32 / 1024.0
                co = vram(((regs.get(0xB1, 0) & 0xff0000) << 8) | regs.get(0xB0, 0), act)
                if co is not None: F['enVram']['clut'] += 1; F['vdat'].append((co, co + (v & 0x3f) * 32))
            xf.cmd(c, v)
            if c in FLOW: continue
            F['cmds_estado'] += 1
            if c in (0x2B, 0x3B, 0x3D, 0x3F, 0x41):  # dato de matriz: cambia si difiere del que habia en esa casilla
                n = mt.get(c - 1, 0); key = (c, n)
                if mt.get(key) != v: mt[key] = v; pend.add('mtx'); F['cambios'] += 1
                mt[c - 1] = n + 1
            elif c in (0x2A, 0x3A, 0x3C, 0x3E, 0x40):
                mt[c] = v & 0xff
            elif regs.get(c) != v:
                pend.add(grupo(c)); F['cambios'] += 1
            regs[c] = v
        elif k == 'draw':
            dr, blobs = e[1], e[2]
            r, vt, prim = regs, dr['vt'], (dr['op'] >> 16) & 7
            through, clear = (vt >> 23) & 1, r.get(0xD3, 0) & 1
            F['draws'] += 1; F['through'] += through; F['clears'] += clear
            pn = PRIMS[prim] if dr['op'] >> 24 == 4 else 'bezier' if dr['op'] >> 24 == 5 else 'spline'
            F['prims'][pn] = F['prims'].get(pn, 0) + 1
            for g in pend: F['con'][g] += 1
            if not pend - {'cte', 'mtx'}: F['sin_cambio'] += 1
            if not pend: F['nada'] += 1
            pend = set()
            # destino: fb y z (VRAM), filas del scissor
            fbp, fbw = r.get(0x9C, 0) & 0x3ffff0, r.get(0x9D, 0) & 0x7fc
            bpp = 4 if (r.get(0xD2, 0) & 3) == 3 else 2
            y0, y1 = (r.get(0xD4, 0) >> 10) & 0x3ff, ((r.get(0xD5, 0) >> 10) & 0x3ff) + 1
            if fbp != fb_prev: F['escenas'] += 1; fb_prev = fbp
            rng = (fbp + y0 * fbw * bpp, fbp + y1 * fbw * bpp)
            F['escritos'].append(rng); fb_todos.append(rng)
            zw = (clear and r.get(0xD3, 0) & 0x400) or (not clear and r.get(0x23, 0) & 1 and not r.get(0xE7, 0) & 1)
            if zw:
                zbp, zbw = r.get(0x9E, 0) & 0x3ffff0, r.get(0x9F, 0) & 0x7fc
                F['zs'].append((zbp + y0 * zbw * 2, zbp + y1 * zbw * 2))
            # vertices e indices
            vb = next((b for w, a, b in blobs if w == gpd.B_VERTS), None)
            vbytes = dr['vsize'] * dr['upper'] if dr['vsize'] else len(vb or b'')
            F['vkb'] += vbytes / 1024.0; F['vrangos'].append((dr['vaddr'], dr['vaddr'] + vbytes))
            vo = vram(dr['vaddr'], act)
            if vo is not None: F['enVram']['vert'] += 1; F['vdat'].append((vo, vo + vbytes))
            it = (vt >> 11) & 3
            if it:
                F['ikb'] += (dr['op'] & 0xffff) * (1 << (it - 1)) / 1024.0
                io = vram(dr['iaddr'], act)
                if io is not None: F['enVram']['ind'] += 1; F['vdat'].append((io, io + (dr['op'] & 0xffff) * (1 << (it - 1))))
            # textura (todos los niveles)
            if r.get(0x1E, 0) & 1 and not clear:
                fmt = r.get(0xC3, 0) & 0xf
                niveles = ((r.get(0xC2, 0) >> 16) & 7) + 1
                cl = ((r.get(0xB1, 0) & 0xff0000) << 8) | r.get(0xB0, 0) if 4 <= fmt <= 7 else 0
                for n in range(niveles):
                    ta = ((r.get(0xA8 + n, 0) & 0x0f0000) << 8) | r.get(0xA0 + n, 0)
                    sz, bw = r.get(0xB8 + n, 0), r.get(0xA8 + n, 0) & 0x7ff
                    tw, th = 1 << (sz & 0xf), 1 << ((sz >> 8) & 0xf)
                    nb = TEXBITS[fmt] * max(bw, 1) * th // 8 if fmt < 11 else 0
                    key = (ta, tw, th, fmt, cl, r.get(0xC2, 0) & 1)
                    if key in F['tex']: continue
                    vo = vram(ta, act)
                    org = None
                    if vo is not None:
                        lo, hi = vo, vo + nb
                        org = ('rt del frame' if toca(F['escritos'], lo, hi) else 'z del frame' if toca(F['zs'], lo, hi)
                               else 'TRANSFER del frame' if toca(F['xfers'], lo, hi) else None)
                        if not org:
                            m0, m1 = d.mem_at(ini_k, base + lo, nb), d.mem_at(fin_k, base + lo, nb)
                            org = 'fija en el volcado' if m0 is not None and m0 == m1 else 'desconocida' if m0 is not None else 'sin datos'
                        F['origen'][org] = F['origen'].get(org, 0) + 1
                        if org in ('fija en el volcado', 'desconocida', 'sin datos'): F['vdat'].append((lo, hi))
                    F['tex'][key] = (nb, vo is not None, org, n)
            # guard band (solo 3D sin huesos ni morph: las posiciones se transforman como el GE)
            if not through and not clear and prim in (3, 4, 5, 6):
                if (vt >> 9) & 3 or (vt >> 18) & 7:
                    F['sin_pos'] += 1
                else:
                    pts = carga_ge.vertices(dr, blobs, xf, False)
                    if pts is None: F['sin_pos'] += 1
                    else: gband(F, prim, pts, xf.off)
    if F['draws'] or F['cmds']: frames.append(F)
    # fb que el juego dibuja en otro momento del volcado: lo tendria el administrador de framebuffers
    for F in frames:
        for key, (nb, env, org, n) in list(F['tex'].items()):
            if org in ('fija en el volcado', 'desconocida') and toca(fb_todos, vram(key[0], act), vram(key[0], act) + nb):
                F['origen'][org] -= 1; F['origen']['fb de otro momento'] = F['origen'].get('fb de otro momento', 0) + 1
                F['tex'][key] = (nb, env, 'fb de otro momento', n)
            elif org == 'desconocida':
                F['desconocidas'].append(key + (nb,))
    # Datos del juego en la VRAM que la Vita tendria que recibir por espejo: KB y cuantos cambian dentro del volcado
    for F in frames:
        F['vdat'] = merge(F['vdat'])
        F['vdatkb'] = sum(b - a for a, b in F['vdat']) / 1024.0
        F['vdatcambia'] = sum(b - a for a, b in F['vdat'] if d.mem_at(ini_k, base + a, b - a) != d.mem_at(fin_k, base + a, b - a)) / 1024.0
    return d, frames

def merge(rs, gap=0):
    out = []
    for a, b in sorted(rs):
        if out and a <= out[-1][1] + gap: out[-1][1] = max(out[-1][1], b)
        else: out.append([a, b])
    return [tuple(x) for x in out]

def gband(F, prim, pts, off):
    """Triangulos que el GE descarta por tener algun vertice fuera de 0..4096 (1x) y con la escala al doble (2x)."""
    if prim == 3: tris = [pts[i:i + 3] for i in range(0, len(pts) - 2, 3)]
    elif prim == 4: tris = [pts[i:i + 3] for i in range(len(pts) - 2)]
    elif prim == 5: tris = [[pts[0], pts[i], pts[i + 1]] for i in range(1, len(pts) - 1)]
    else: tris = [pts[i:i + 2] for i in range(0, len(pts) - 1, 2)]
    for t in tris:
        F['tris'] += 1
        if any(p is None for p in t): F['cerca'] += 1; continue  # cruza el plano cercano: se recorta, no se descarta
        fuera = lambda s: any(not (0 <= off[0] + s * x < 4096 and 0 <= off[1] + s * y < 4096) for x, y in t)
        if fuera(1): F['gb1'] += 1
        if fuera(2): F['gb2'] += 1

def resumen(d, frames, detalle=False):
    full = frames
    n = max(1, sum(F['flips'] for F in frames))
    def avg(f): return sum(f(F) for F in full) / float(n)
    print('%s: %s GePatch %s %s, %d flip(s): las cifras son por flip' % (os.path.basename(d.path), d.game, d.plugin,
          'PASIVO' if d.passive else 'activo', n))
    if d.truncated: print('  (archivo danado: %d KB perdidos)' % (d.lost // 1024))
    res = dict(game=d.game, file=os.path.basename(d.path), n=n)
    res['draws'] = avg(lambda F: F['draws']); res['cmds'] = avg(lambda F: F['cmds'])
    res['conest'] = avg(lambda F: F['draws'] - F['sin_cambio'])
    res['escenas'] = avg(lambda F: F['escenas'])
    res['tex'] = avg(lambda F: len(F['tex'])); res['texkb'] = avg(lambda F: sum(v[0] for v in F['tex'].values()) / 1024.0)
    res['texvram'] = avg(lambda F: sum(1 for v in F['tex'].values() if v[1]))
    res['desc'] = avg(lambda F: F['origen'].get('desconocida', 0))
    res['vkb'] = avg(lambda F: F['vkb']); res['vkbu'] = avg(lambda F: union(F['vrangos']) / 1024.0)
    res['gb1'] = avg(lambda F: F['gb1']); res['gb2'] = avg(lambda F: F['gb2']); res['tris'] = avg(lambda F: F['tris'])
    res['nada'] = avg(lambda F: F['nada']); res['vdatkb'] = avg(lambda F: F['vdatkb']); res['vdatc'] = avg(lambda F: F['vdatcambia'])
    res['vdat'] = full[-1]['vdat']
    res['envram'] = avg(lambda F: F['enVram']['listas']); res['vvram'] = avg(lambda F: F['enVram']['vert'])
    print('  por flip: %.0f comandos (%.0f de estado, %.0f cambian algo), %.1f listas, %.0f draws (%.0f 2D, %.0f clears), %.1f escenas (cambios de destino)' % (
        res['cmds'], avg(lambda F: F['cmds_estado']), avg(lambda F: F['cambios']), avg(lambda F: F['listas']), res['draws'],
        avg(lambda F: F['through']), avg(lambda F: F['clears']), res['escenas']))
    pr = {}
    for F in full:
        for p, c in F['prims'].items(): pr[p] = pr.get(p, 0) + c
    print('  primitivas: ' + ', '.join('%s %.0f' % (p, c / float(n)) for p, c in sorted(pr.items(), key=lambda x: -x[1])))
    print('  draws con cambio antes (de un draw a otro): destino %.0f, programa/estado fijo %.0f, textura %.0f, constantes %.0f, matrices %.0f; solo constantes o nada: %.0f' % tuple(
        [avg(lambda F, g=g: F['con'][g]) for g in ('rt', 'prog', 'tex', 'cte', 'mtx')] + [avg(lambda F: F['sin_cambio'])]))
    print('  draws que necesitan otro programa/estado o textura (lo caro en GXM): %.0f de %.0f; sin ningun cambio (se juntan con el anterior): %.0f -> %.0f lotes' % (
        res['conest'], res['draws'], res['nada'], res['draws'] - res['nada']))
    fm = {}
    for F in full:
        for (ta, tw, th, fmt, cl, swz), (nb, env, org, lv) in F['tex'].items():
            k = '%s%s%s' % (TEXFMT[fmt] if fmt < 11 else '?', '/swz' if swz else '', '/VRAM' if env else '')
            fm[k] = fm.get(k, 0) + 1
    print('  texturas distintas: %.1f (%.0f KB), en la VRAM %.1f; formatos: %s' % (res['tex'], res['texkb'], res['texvram'],
          ', '.join('%s %.1f' % (k, c / float(n)) for k, c in sorted(fm.items(), key=lambda x: -x[1])[:8])))
    org = {}
    for F in full:
        for o, c in F['origen'].items(): org[o] = org.get(o, 0) + c
    if org: print('  origen de las texturas en la VRAM: ' + ', '.join('%s %.1f' % (o, c / float(n)) for o, c in sorted(org.items(), key=lambda x: -x[1]) if c))
    print('  vertices %.0f KB (sin repetir %.0f KB), indices %.0f KB, CLUT %.1f cargas (%.1f KB), TRANSFER %.1f (%.0f KB)' % (
        res['vkb'], res['vkbu'], avg(lambda F: F['ikb']), avg(lambda F: F['clut']), avg(lambda F: F['clutkb']),
        avg(lambda F: F['xfer']), avg(lambda F: F['xferkb'])))
    print('  datos del juego en la VRAM (la Vita no los ve): comandos %.0f, draws con vertices %.0f, con indices %.0f, cargas de CLUT %.0f' % (
        res['envram'], res['vvram'], avg(lambda F: F['enVram']['ind']), avg(lambda F: F['enVram']['clut'])))
    print('  VRAM que la Vita necesitaria por espejo (listas, vertices, indices, CLUT, texturas que no dibujo el GE): %.0f KB en %d tramos; cambian dentro del volcado %.0f KB' % (
        res['vdatkb'], len(res['vdat']), res['vdatc']))
    print('  guard band: %.0f triangulos 3D; descartados a 1x %.0f, a 2x %.0f; cruzan el plano cercano %.0f; draws 3D sin calcular (huesos, morph, sin datos) %.0f' % (
        res['tris'], res['gb1'], res['gb2'], avg(lambda F: F['cerca']), avg(lambda F: F['sin_pos'])))
    if detalle:
        vistas = set()
        for F in full:
            for ta, tw, th, fmt, cl, swz, nb in F['desconocidas']:
                if ta in vistas: continue
                vistas.add(ta)
                print('    desconocida %08x %dx%d %s %d KB' % (ta, tw, th, TEXFMT[fmt] if fmt < 11 else '?', nb // 1024))
    return res

def union(rs):
    tot, hi = 0, -1
    for a, b in sorted(rs):
        if b <= hi: continue
        tot += b - max(a, hi); hi = b
    return tot

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    rs, ds = [], []
    for p in args:
        d, fr = analizar(p, '--detalle' in sys.argv)
        rs.append(resumen(d, fr, '--detalle' in sys.argv))
        d.recs, d.mem = None, {k: v for k, v in d.mem.items() if k[0] == 1}  # para comparar volcados basta la VRAM falsa inicial
        ds.append((d, fr))
    if len(rs) > 1:
        print('\ncaros = draws con otro programa/estado o textura; lotes = draws que no se pueden juntar con el anterior;')
        print('espejo = KB de la VRAM que la Vita necesitaria (listas, vertices, CLUT, texturas no dibujadas por el GE);')
        print('igual = % de ese espejo identico en el volcado anterior del mismo juego (misma direccion); cmdV = comandos en la VRAM')
        print('%-18s %-6s %5s %6s %5s %5s %4s %5s %6s %4s %4s %6s %6s %5s %5s %6s %5s %5s %5s' % ('volcado', 'vers', 'draws', 'cmds', 'caros',
              'lotes', 'esc', 'tex', 'texKB', 'vram', 'desc', 'vertKB', 'unicKB', 'cmdV', 'drVV', 'espejo', 'igual', 'gb1x', 'gb2x'))
        prev = {}
        for r, (d, fr) in zip(rs, ds):
            F, igual = fr[0], '-'
            p = prev.get(d.game)
            if p is not None and F['vdat'] and not d.passive and not p.passive:
                tot = sum(b - a for a, b in F['vdat'])
                same = sum(b - a for a, b in F['vdat'] if d.mem_at(1, 0x0A000000 + a, b - a) == p.mem_at(1, 0x0A000000 + a, b - a))
                igual = '%d%%' % (100 * same // max(1, tot))
            prev[d.game] = d
            print('%-18s %-6s %5.0f %6.0f %5.0f %5.0f %4.1f %5.1f %6.0f %4.0f %4.0f %6.0f %6.0f %5.0f %5.0f %6.0f %5s %5.0f %5.0f' % (r['file'][8:-4],
                  d.plugin[4:10], r['draws'], r['cmds'], r['conest'], r['draws'] - r['nada'], r['escenas'], r['tex'], r['texkb'], r['texvram'],
                  r['desc'], r['vkb'], r['vkbu'], r['envram'], r['vvram'], r['vdatkb'], igual, r['gb1'], r['gb2']))
