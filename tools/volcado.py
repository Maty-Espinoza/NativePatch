#!/usr/bin/env python3
"""Dibuja en el PC un frame volcado por NativeDraw (fK_registro.bin) y lo compara con la imagen normal.

  python3 tools/volcado.py <carpeta del volcado> [K] [--texturas-de <otro .bin>] [--salida <carpeta>]

Imita al GE del PSP en 2D (modo through: rectangulos, triangulos y tiras; texturas 5650/5551/4444/8888/T4/T8 con la
copia que hizo el registro; paletas; funcion de textura; alfa test; mezcla; borrados; scissor; imagenes intermedias)
a la resolucion del PSP, empezando la imagen que se muestra desde la normal (como el presentador).
Lo que dice:
  - si el dibujo del PC sale igual a la imagen normal: los datos estan bien y el error es de la Vita (traductor);
  - si sale con los mismos errores que fK_vita.bmp: los datos que recibio la Vita estan mal; los draws culpables y sus
    texturas (con que filas se copiaron y de que copia del espejo) quedan listados.
  - con --texturas-de f2_registro.bin: dibuja f1 con las texturas copiadas en el frame siguiente; si asi sale bien, la
    copia del espejo llego atrasada.
Deja fK_pc.png (normal | PC | Vita reducida | diferencias) en --salida (por defecto, la carpeta del volcado).
Necesita numpy y Pillow. No escribe nada en el repo. Formato del registro y niveles de volcado: docs/dev/volcados.md.
"""
import struct, sys, os
import numpy as np
from PIL import Image

# Lee un fK_registro.bin: cabecera de 16 palabras, registros guardados (RS), tabla de texturas (ta/tn/to/tq/tm), draws
# (d), estados (e: un valor por registro de RS), datos de vertices/indices/paletas (a) y copias de texturas (tx).
# Las posiciones 'off' de los draws son desplazamiento + 1 (0 = no hay).
def leer(path):
    b = open(path, 'rb').read()
    h = struct.unpack_from('<16I', b, 0)
    if h[0] != 0x47524E44: sys.exit(f'{path}: no es un registro de NativeDraw')
    F = dict(ver=h[1], nimg=h[15] if h[1] >= 3 else 0, frame=h[2], fb=h[3], fbw=h[4], nd=h[5], ne=h[6], na=h[7], flags=h[8], nt=h[9], ntx=h[10], esp=h[11], espf=h[12])
    nrs, dsz, o = h[13], h[14], 64
    al = lambda n: (n + 3) & ~3
    F['RS'] = list(b[o:o + nrs]); o += al(nrs)
    nt = F['nt']
    for k in ('ta', 'tn', 'to', 'tq'): F[k] = list(struct.unpack_from(f'<{nt}I', b, o)); o += nt * 4
    F['tm'] = list(struct.unpack_from(f'<{nt}Q', b, o)); o += nt * 8
    F['d'] = []
    for i in range(F['nd']):
        st, prim, n, vt, voff, vlen, ioff, clut, clen, toff = struct.unpack_from('<2H8I', b, o + i * dsz)
        F['d'].append(dict(st=st, prim=prim, n=n, vt=vt, voff=voff, vlen=vlen, ioff=ioff, clut=clut, clen=clen, toff=toff))
    o += al(F['nd'] * dsz)
    F['e'] = [struct.unpack_from(f'<{nrs}I', b, o + i * nrs * 4) for i in range(F['ne'])]; o += F['ne'] * nrs * 4
    F['a'] = b[o:o + F['na']]; o += al(F['na'])
    F['tx'] = bytearray(b[o:o + F['ntx']])
    return F

# La imagen del PSP de ese mismo frame (copia del espejo de su flip), si el registro la trae; None si no.
# Devuelve 272x480 RGB float segun el formato de pixel del framebuffer (565, 5551, 4444 u 8888).
def exacta(F):
    if not F['nimg']: return None
    w, f, o = F['fbw'] & 0xFFFF, F['fbw'] >> 16 & 3, F['nimg'] - 1
    if f == 3: return np.frombuffer(bytes(F['tx'][o:o + w * 272 * 4]), np.uint8).reshape(272, w, 4)[:, :480, :3].astype(np.float32)
    c = np.frombuffer(bytes(F['tx'][o:o + w * 272 * 2]), '<u2').reshape(272, w)[:, :480].astype(np.uint32)
    if f == 0: r, g, b = (c & 31) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c >> 11 & 31) * 255 // 31
    elif f == 1: r, g, b = (c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31
    else: r, g, b = (c & 15) * 17, (c >> 4 & 15) * 17, (c >> 8 & 15) * 17
    return np.stack([r, g, b], 2).astype(np.float32)

# Valor del registro r del GE en el estado e (0 si el registro no se guardo).
def RE(F, e, r): return e[F['RS'].index(r)] if r in F['RS'] else 0

def c8888(c, f):  # color del PSP -> (r, g, b, a) 0..255
    if f == 3: return (c & 255, c >> 8 & 255, c >> 16 & 255, c >> 24 & 255)
    if f == 0: return ((c & 31) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c >> 11 & 31) * 255 // 31, 255)
    if f == 1: return ((c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31, 255 if c >> 15 else 0)
    return ((c & 15) * 17, (c >> 4 & 15) * 17, (c >> 8 & 15) * 17, (c >> 12 & 15) * 17)

# Decodifica los vertices del draw segun su tipo de vertice (vt: formatos de textura, color, normal y posicion, con
# indices de 8/16/32 bits): lista de (x, y, u, v, color). col0 = color del material si el vertice no trae color.
def verts(F, d, col0):
    s4, cs = (0, 1, 2, 4), (0, 0, 0, 0, 2, 2, 2, 4)
    vt = d['vt']; tc, ct, nt, pt = vt & 3, vt >> 2 & 7, vt >> 5 & 3, vt >> 7 & 3
    sz, al, off = 0, 1, {}
    for nm, n, s in (('t', 2, s4[tc]), ('c', 1, cs[ct]), ('n', 3, s4[nt]), ('p', 3, s4[pt])):
        if s: sz = (sz + s - 1) & ~(s - 1); off[nm] = sz; sz += n * s; al = max(al, s)
    sz = (sz + al - 1) & ~(al - 1)
    a, vb = F['a'], d['voff'] - 1
    ix = vt >> 11 & 3
    out = []
    for i in range(d['n']):
        k = i
        if d['ioff']:
            p = d['ioff'] - 1
            k = a[p + i] if ix == 1 else struct.unpack_from('<H' if ix == 2 else '<I', a, p + i * (2 if ix == 2 else 4))[0]
        q = vb + k * sz
        if pt == 1: x, y = struct.unpack_from('<bb', a, q + off['p'])
        elif pt == 2: x, y = struct.unpack_from('<hh', a, q + off['p'])
        else: x, y = struct.unpack_from('<ff', a, q + off['p'])
        u = v = 0
        if tc: u, v = struct.unpack_from(('<BB', '<HH', '<ff')[tc - 1], a, q + off['t'])
        c = col0
        if ct: c = c8888(struct.unpack_from('<I' if ct == 7 else '<H', a, q + off['c'])[0], ct - 4)
        out.append((float(x), float(y), float(u), float(v), c))
    return out

# Textura de un draw, decodificada a RGBA (o a indices + paleta en T4/T8), o una imagen intermedia ya dibujada en el PC
# (rt) si la textura no se copio. ok = False si es una imagen intermedia que el PC no dibujo.
# texturas_de: otro registro del que tomar las filas que se copiaron alli (grupos de 8 filas marcados en tm).
class Tex:
    def __init__(s, F, d, e, fbs, texturas_de):
        s.ok, fmt = True, RE(F, e, 0xC3) & 15
        ta = (RE(F, e, 0xA0) & 0xFFFFF0) | ((RE(F, e, 0xA8) << 8) & 0x0F000000)
        s.tbw, s.th, s.fmt, s.ta = RE(F, e, 0xA8) & 0x7FF, 1 << (RE(F, e, 0xB8) >> 8 & 15), fmt, ta
        s.rt = None
        if not d['toff']:
            s.rt = fbs.get(ta & 0x1FFFF0)
            if s.rt is None: s.ok = False
            return
        bp = (16, 16, 16, 32, 4, 8)[fmt]
        rb = s.tbw * bp // 8
        o = d['toff'] - 1
        data = bytes(F['tx'][o:o + rb * s.th])
        if texturas_de is not None:  # las filas de esta textura, copiadas en otro frame (si alli se copiaron)
            G = texturas_de
            for j in range(G['nt']):
                if G['ta'][j] == ta and (G['tn'][j] & 0x7FFFFFFF) == rb * s.th:
                    m, og, buf = G['tm'][j], G['to'][j], bytearray(data)
                    for g in range(64):
                        if m >> g & 1: buf[g * 8 * rb:(g + 1) * 8 * rb] = G['tx'][og + g * 8 * rb:og + (g + 1) * 8 * rb]
                    data = bytes(buf[:rb * s.th])
        raw = np.frombuffer(data.ljust(rb * s.th, b'\0'), np.uint8).reshape(s.th, rb)
        if 2 <= F['ver'] < 4 and RE(F, e, 0xC2) & 1 and s.th >= 8:  # (versiones 2 y 3: la copia con swizzle va tal cual)
            raw = raw.reshape(s.th // 8, rb // 16, 8, 16).transpose(0, 2, 1, 3).reshape(s.th, rb)
        if fmt == 4: s.idx = np.stack([raw & 15, raw >> 4], 2).reshape(s.th, rb * 2)
        elif fmt == 5: s.idx = raw
        elif fmt == 3: s.rgba = raw.reshape(s.th, s.tbw, 4).astype(np.float32)
        else:
            c = raw.view('<u2').astype(np.uint32)
            s.rgba = np.stack(np.vectorize(lambda x: c8888(int(x), fmt), otypes=[int] * 4)(c), 2).astype(np.float32)
        if fmt >= 4:
            c5 = RE(F, e, 0xC5); cf, sh, mk, st = c5 & 3, c5 >> 2 & 31, c5 >> 8 & 255, (c5 >> 16 & 31) << 4
            bpe = 4 if cf == 3 else 2; mx = d['clen'] // bpe; po = d['clut'] - 1
            pal = []
            for k in range(16 if fmt == 4 else 256):
                j = ((k >> sh) & mk) | st
                v = struct.unpack_from('<I' if bpe == 4 else '<H', F['a'], po + j * bpe)[0] if d['clut'] and j < mx else 0
                pal.append(c8888(v, cf))
            s.pal = np.array(pal, np.float32)
    # (r, g, b, a) para U, V en texels (float); lin: filtro bilineal (solo en imagenes intermedias).
    def get(s, U, V, lin=False):
        if s.rt is not None:
            if not lin: return s.rt[np.clip(np.floor(V).astype(np.int64), 0, 271), np.clip(np.floor(U).astype(np.int64), 0, 511)]
            x, y = U - .5, V - .5; x0, y0 = np.floor(x), np.floor(y); fx, fy = (x - x0)[..., None], (y - y0)[..., None]
            x0, y0 = np.clip(x0.astype(np.int64), 0, 511), np.clip(y0.astype(np.int64), 0, 271)
            x1, y1 = np.clip(x0 + 1, 0, 511), np.clip(y0 + 1, 0, 271)
            r = s.rt
            return (r[y0, x0] * (1 - fx) + r[y0, x1] * fx) * (1 - fy) + (r[y1, x0] * (1 - fx) + r[y1, x1] * fx) * fy
        u, v = np.floor(U).astype(np.int64), np.floor(V).astype(np.int64)
        u = u % max(s.tbw, 1); v = v % s.th  # (repetir)
        if s.fmt >= 4: return s.pal[s.idx[v, u]]
        return s.rgba[v, u]

# Mezcla del GE (si esta activa): factores fuente/destino (con colores fijos) y operacion, sobre colores 0..255.
def blend(F, e, src, dst):
    if not RE(F, e, 0x21) & 1: return src
    df = RE(F, e, 0xDF); sf, dfc, op = df & 15, df >> 4 & 15, df >> 8 & 15
    fa = np.array(c8888(RE(F, e, 0xE0), 3)[:3], np.float32) / 255; fb = np.array(c8888(RE(F, e, 0xE1), 3)[:3], np.float32) / 255
    S, D, sa, da = src[..., :3] / 255, dst[..., :3] / 255, src[..., 3:4] / 255, dst[..., 3:4] / 255
    def fac(f, other, fixed):
        return {0: other, 1: 1 - other, 2: sa, 3: 1 - sa, 4: da, 5: 1 - da, 6: 2 * sa, 7: 1 - 2 * sa, 8: 2 * da, 9: 1 - 2 * da}.get(f, fixed)
    A, B = S * fac(sf, D, fa), D * fac(dfc, S, fb)
    r = [A + B, A - B, B - A, np.minimum(S, D), np.maximum(S, D), np.abs(S - D)][min(op, 5)]
    out = src.copy(); out[..., :3] = np.clip(r, 0, 1) * 255
    return out

# Mascara del alfa test del GE para los alfas a (todo True si esta apagado).
def atest(F, e, a):
    if not RE(F, e, 0x22) & 1: return np.ones(a.shape, bool)
    db = RE(F, e, 0xDB); fn, ref, mk = db & 7, db >> 8 & 255, db >> 16 & 255
    a = a.astype(np.int32) & mk; ref &= mk
    return [a < -1, a > -1, a == ref, a != ref, a < ref, a <= ref, a > ref, a >= ref][fn]

# Dibuja los draws through (2D) del registro: rectangulos, triangulos y tiras, con textura, alfa test, mezcla, borrados
# y scissor. Cada imagen destino es 512x272; la que se muestra empieza desde base. Los draws 3D se saltan.
# Devuelve (imagenes por direccion, ultimo draw que escribio cada pixel, direccion mostrada).
def dibujar(F, base, texturas_de=None):
    fbs, quien = {}, {}
    disp = F['fb'] & 0x1FFFF0
    dest = [RE(F, F['e'][d['st']], 0x9C) & 0x1FFFF0 for d in F['d']]
    if F['d'] and disp not in dest: disp = dest[-1]  # ninguno va a la que se muestra: la del ultimo (como la Vita)
    def fbuf(a):
        if a not in fbs:
            fbs[a] = np.zeros((272, 512, 4), np.float32); fbs[a][..., 3] = 255
            if a == disp: fbs[a][:, :480, :3] = base
            quien[a] = np.full((272, 512), -1, np.int32)
        return fbs[a]
    for i, d in enumerate(F['d']):
        e = F['e'][d['st']]
        if not d['vt'] >> 23 & 1: continue  # 3D: no
        dst, clr = fbuf(dest[i]), RE(F, e, 0xD3)
        mat = c8888((RE(F, e, 0x55) & 0xFFFFFF) | (RE(F, e, 0x56) & 255) << 24, 3)
        V = verts(F, d, mat)
        tex = None
        if not clr & 1 and RE(F, e, 0x1E) & 1 and d['vt'] & 3:
            tex = Tex(F, d, e, fbs, texturas_de)
            if not tex.ok: continue
        d4, d5 = RE(F, e, 0xD4), RE(F, e, 0xD5)
        sx0, sy0, sx1, sy1 = (d4 & 1023, d4 >> 10 & 1023, (d5 & 1023) + 1, (d5 >> 10 & 1023) + 1) if d5 & 0xFFFFF else (0, 0, 512, 272)
        tris = []
        if d['prim'] == 6:
            for k in range(0, len(V) - 1, 2):
                a, b = V[k], V[k + 1]
                tris.append(('r', a, b))
        elif d['prim'] == 3: tris = [('t', V[k], V[k + 1], V[k + 2]) for k in range(0, len(V) - 2, 3)]
        elif d['prim'] == 4: tris = [('t', V[k], V[k + 1], V[k + 2]) for k in range(len(V) - 2)]
        else: continue
        for t in tris:
            xs = [p[0] for p in t[1:]]; ys = [p[1] for p in t[1:]]
            x0, x1 = max(int(np.floor(min(xs))), sx0, 0), min(int(np.ceil(max(xs))), sx1, 512)
            y0, y1 = max(int(np.floor(min(ys))), sy0, 0), min(int(np.ceil(max(ys))), sy1, 272)
            if x0 >= x1 or y0 >= y1: continue
            X, Y = np.meshgrid(np.arange(x0, x1) + .5, np.arange(y0, y1) + .5)
            if t[0] == 'r':
                a, b = t[1], t[2]
                lx0, lx1, ly0, ly1 = min(a[0], b[0]), max(a[0], b[0]), min(a[1], b[1]), max(a[1], b[1])
                m = (X >= lx0) & (X < lx1) & (Y >= ly0) & (Y < ly1)
                U = a[2] + (X - a[0]) * ((b[2] - a[2]) / (b[0] - a[0]) if b[0] != a[0] else 0)
                Vv = a[3] + (Y - a[1]) * ((b[3] - a[3]) / (b[1] - a[1]) if b[1] != a[1] else 0)
                C = np.broadcast_to(np.array(b[4], np.float32), X.shape + (4,))
            else:
                p0, p1, p2 = t[1], t[2], t[3]
                den = (p1[1] - p2[1]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[1] - p2[1])
                if den == 0: continue
                w0 = ((p1[1] - p2[1]) * (X - p2[0]) + (p2[0] - p1[0]) * (Y - p2[1])) / den
                w1 = ((p2[1] - p0[1]) * (X - p2[0]) + (p0[0] - p2[0]) * (Y - p2[1])) / den
                w2 = 1 - w0 - w1
                m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
                U = w0 * p0[2] + w1 * p1[2] + w2 * p2[2]; Vv = w0 * p0[3] + w1 * p1[3] + w2 * p2[3]
                C = (w0[..., None] * np.array(p0[4], np.float32) + w1[..., None] * np.array(p1[4], np.float32) + w2[..., None] * np.array(p2[4], np.float32))
            if not m.any(): continue
            src = np.array(C, np.float32)
            if tex is not None:
                T = tex.get(U, Vv, bool(RE(F, e, 0xC6) & 0x101))
                c9 = RE(F, e, 0xC9); tf, tcc, dbl = c9 & 7, c9 >> 8 & 1, c9 >> 16 & 1
                if tf == 3: rgb = T[..., :3]
                else: rgb = T[..., :3] * src[..., :3] / 255
                if dbl: rgb = np.minimum(rgb * 2, 255)
                al = (T[..., 3] * src[..., 3] / 255 if tf != 3 else T[..., 3]) if tcc else src[..., 3]
                src = np.concatenate([rgb, al[..., None]], 2)
            if clr & 1:
                if not clr >> 8 & 1: continue
            else:
                m &= atest(F, e, src[..., 3])
                src = blend(F, e, src, dst[y0:y1, x0:x1])
            reg = dst[y0:y1, x0:x1]
            reg[m, :3] = src[m, :3]
            quien[dest[i]][y0:y1, x0:x1][m] = i
    return fbs, quien, disp

# Lee los argumentos, dibuja el frame K, imprime las diferencias, los draws culpables y las texturas, y guarda las
# imagenes (fK_pc.png y las intermedias).
def main():
    args = sys.argv[1:]
    if not args: sys.exit(__doc__)
    carp, k, otro, sal = args[0], '1', None, None
    rest = args[1:]
    while rest:
        a = rest.pop(0)
        if a == '--texturas-de': otro = leer(rest.pop(0))
        elif a == '--salida': sal = rest.pop(0)
        else: k = a
    sal = sal or carp
    F = leer(os.path.join(carp, f'f{k}_registro.bin'))
    normal = np.array(Image.open(os.path.join(carp, f'f{k}_normal.bmp')).convert('RGB'), np.float32)[:272, :480]
    vp = os.path.join(carp, f'f{k}_vita.bmp')
    vita = np.array(Image.open(vp).convert('RGB').resize((480, 272), Image.BILINEAR), np.float32) if os.path.exists(vp) else np.zeros_like(normal)
    ex = exacta(F)
    if ex is not None:  # (la imagen normal de la Vita puede ser de otro frame: se compara con la exacta)
        print(f'imagen exacta del frame: si (la normal difiere de ella en {(np.abs(ex - normal) > 40).any(2).mean() * 100:.2f}%: atraso de la Vita)')
        normal = ex
    fbs, quien, disp = dibujar(F, normal, otro)
    pc = fbs.get(disp, np.zeros((272, 512, 4), np.float32))[:, :480, :3]
    dif = (np.abs(pc - normal) > 40).any(2)
    difv = (np.abs(vita - normal) > 60).any(2)
    print(f'frame {F["frame"]}: draws {F["nd"]}, texturas {F["nt"]}, espejo usado {F["esp"]} (ultimo terminado {F["espf"]}), imagen {disp:06x}')
    print(f'pixeles distintos de la normal: PC {dif.mean() * 100:.2f}% | Vita {difv.mean() * 100:.2f}%')
    # culpables: el ultimo draw que escribio cada pixel distinto de la imagen que se muestra
    cul = {}
    q = quien[disp][:, :480]
    for y, x in zip(*np.nonzero(dif)):
        i = q[y, x]
        cul[i] = cul.get(i, 0) + 1
    if cul:
        print('draws con pixeles distintos (draw: pixeles) en la imagen que se muestra:', sorted(cul.items(), key=lambda t: -t[1])[:12])
    for a, Q in quien.items():  # imagenes intermedias: solo cuantos draws (no hay referencia para compararlas)
        if a != disp: print(f'imagen intermedia {a:06x}: {len(set(Q[Q >= 0].tolist()))} draws')
    print('texturas: t direccion bytes grupos_copiados copia_del_espejo')
    for j in range(F['nt']):
        print(f'  t{j} {F["ta"][j]:07x} {F["tn"][j] & 0x7FFFFFFF} {F["tm"][j]:016x} {F["tq"][j]}')
    big = lambda im: Image.fromarray(np.clip(im, 0, 255).astype(np.uint8))
    marca = normal.copy(); marca[dif] = (255, 0, 255)
    out = Image.new('RGB', (960, 544))
    for n, im in enumerate((normal, pc, vita, marca)): out.paste(big(im), ((n & 1) * 480, (n >> 1) * 272))
    for a, b in fbs.items():
        if a != disp: big(b[..., :3]).save(os.path.join(sal, f'f{k}_pc_intermedia_{a:06x}.png'))
    nombre = os.path.join(sal, f'f{k}_pc{"_texturas_de_otro" if otro else ""}.png')
    out.save(nombre)
    print('->', nombre, '(%s | PC / Vita | diferencias del PC en magenta)' % ('exacta' if ex is not None else 'normal'))

if __name__ == '__main__':
    main()
