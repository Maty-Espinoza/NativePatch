#!/usr/bin/env python3
"""Lee los volcados binarios de GePatch (.gpd, L+R+SELECT o el menu) y los convierte.

  gpd.py resumen  VOLCADO                 que hay: cabecera, opciones, listas, fbs, draws, eventos
  gpd.py informe  VOLCADO [> salida.txt]  todos los comandos (juego -> plugin) con draws y eventos
  gpd.py imagenes VOLCADO CARPETA [--texturas]
                                          PNG de los fb, el buffer grande, render targets,
                                          destinos de copias (formulario) y texturas usadas
  gpd.py ppsspp   VOLCADO SALIDA.ppdmp [--plugin] [--hasta=N] [--mostrar=ADDR,ANCHO,FMT]
                                          volcado de PPSSPP: de uno PASIVO dibuja el frame original;
                                          con --plugin, lo que ejecuto el GE con los parches
  gpd.py comparar PASIVO ACTIVO CARPETA   alinea los draws de ambos y compara las imagenes
                                          (lo que el juego quiere vs lo que sale con el plugin)

Solo usa la biblioteca estandar de Python 3.
"""
import os, re, struct, sys, zlib, difflib

HEADER, REGS, MEM, LIST, CMDS, DRAW, BLOB, EVENT, FLIP, TXLIST, END, FB, SNAP = range(1, 14)
B_VERTS, B_VERTS_FINAL, B_INDICES, B_TEX, B_CLUT, B_XFER_SRC = range(1, 7)
MEM_KIND = {0: 'vram_real_ini', 1: 'vram_falsa_ini', 2: 'vram_real_fin', 3: 'vram_falsa_fin', 4: 'copia_fin'}
FB_DEC = {0: 'buffer grande', 1: 'render target', 2: 'fb de copia (escala 1)', 3: 'ignorado'}
TEXMODE = {0: 'normal', 1: 'OMITIR', 2: 'textura render target', 3: 'VRAM real -> falsa', 4: 'fb actual',
           5: 'pantalla reducida'}
PRIMS = ['puntos', 'lineas', 'tira_lineas', 'triangulos', 'tira_tri', 'abanico_tri', 'rectangulos', '?']
OPT_NAMES = ['rt_on', 'rt_scale', 'omit_fbtex', 'fallback', 'nop_mark', 'walk_new', 'wide1024', 'dx', 'dy', 'v2d',
             'tx_ge', 'walk_cache', 'wc_check', 'layers', 'vram_fix', 'autocap', 'cpu_draw', 'csrc']  # orden de Options en menu.h

def cmd_names():
    names = {}
    h = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ge_constants.h')
    if os.path.exists(h):  # enum GECommand: valores explicitos o el anterior + 1
        n = -1
        for m in re.finditer(r'GE_CMD_(\w+)\s*(?:=\s*(0x[0-9A-Fa-f]+|\d+))?\s*,', open(h).read()):
            n = int(m.group(2), 0) if m.group(2) else n + 1
            names.setdefault(n, m.group(1))
    return names
NAMES = cmd_names()
def cname(c): return NAMES.get(c, '%02X' % c)

# --- Lectura -----------------------------------------------------------------------------
class Dump:
    def __init__(self, path):
        self.path, data = path, open(path, 'rb').read()
        self.recs, pos, self.lost = [], 0, 0
        while pos + 8 <= len(data):
            t, n = struct.unpack_from('<II', data, pos)
            if not 1 <= t <= 13 or pos + 8 + n > len(data):
                # dev28: otro hilo (el flip) podia escribir en medio de un registro. Se busca
                # desde donde vuelve a haber una cadena valida de registros hasta el final
                nxt = self.resync(data, pos)
                self.lost += nxt - pos
                pos = nxt
                continue
            self.recs.append((t, data[pos + 8:pos + 8 + n]))
            pos += 8 + ((n + 3) & ~3)
        self.truncated = pos != len(data) or self.lost > 0
        h = next(d for t, d in self.recs if t == HEADER)
        self.game = h[8:20].split(b'\0')[0].decode(errors='replace')
        self.plugin = h[20:36].split(b'\0')[0].decode(errors='replace')
        self.frame, self.passive, self.idle, osz = struct.unpack_from('<4I', h, 36)
        self.opt = struct.unpack_from('<%di' % (osz // 4), h, 52)
        self.mem = {}
        for t, d in self.recs:
            if t == MEM:
                kind, addr, size = struct.unpack_from('<3I', d)
                self.mem[(kind, addr)] = d[12:12 + size]
        self.end = next((d for t, d in self.recs if t == END), None)
        self.snaps = []  # instantaneas a mitad del frame: (que, ancho, alto, formato, datos)
        for t, d in self.recs:
            if t == SNAP:
                addr, w, h, bpp, fmt, frame = struct.unpack_from('<6I', d)
                self.snaps.append((d[24:48].split(b'\0')[0].decode(errors='replace'), w, h, fmt, d[48:48 + w * h * bpp]))

    @staticmethod
    def resync(data, pos):
        def chain(p):
            while p + 8 <= len(data):
                t, n = struct.unpack_from('<II', data, p)
                if not 1 <= t <= 13 or p + 8 + n > len(data):
                    return False
                p += 8 + ((n + 3) & ~3)
            return p == len(data)
        for p in range(pos + 4, len(data) - 7, 4):
            t, n = struct.unpack_from('<II', data, p)
            if 1 <= t <= 13 and p + 8 + n <= len(data) and chain(p):
                return p
        return len(data)

    def regs(self, when=0):
        for t, d in self.recs:
            if t == REGS and struct.unpack_from('<I', d)[0] == when:
                w = struct.unpack_from('<669I', d)
                return {'ge': w[1:257], 'mtx': struct.unpack_from('<148f', d, 257 * 4), 'shadow': w[405:661],
                        'state': w[661:669]}
        return None

    def read(self, addr, size, kind_pref=(0, 1, 4)):
        """Bytes de addr en una de las memorias guardadas (inicio por defecto)."""
        for (kind, a), m in self.mem.items():
            if kind in kind_pref and a <= addr and addr + size <= a + len(m):
                return m[addr - a:addr - a + size]
        return None

    def mem_at(self, kind, addr, size):
        for (k, a), m in self.mem.items():
            if k == kind and a <= addr and addr + size <= a + len(m):
                return m[addr - a:addr - a + size]
        return None

def u32s(d, n=None): return struct.unpack_from('<%dI' % (len(d) // 4 if n is None else n), d)

# --- Pixeles y PNG -----------------------------------------------------------------------
def to_rgba(data, fmt, w, h, stride, clut=None, clutfmt=0, swizzled=False):
    """Pixeles del GE (565/5551/4444/8888, CLUT4/8/16/32) a RGBA de 8 bits."""
    bpp = {0: 16, 1: 16, 2: 16, 3: 32, 4: 4, 5: 8, 6: 16, 7: 32}.get(fmt)
    if bpp is None:
        return None
    stride = max(stride, w)
    rowbytes = (stride * bpp + 7) // 8
    need = rowbytes * h
    if len(data) < need:
        data = data + bytes(need - len(data))
    if swizzled:
        data = unswizzle(data, rowbytes, h)
    out = bytearray(w * h * 4)
    def conv(v, f):
        if f == 0: return ((v & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, ((v >> 11) & 31) * 255 // 31, 255)
        if f == 1: return ((v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, ((v >> 10) & 31) * 255 // 31, 255 if v >> 15 else 0)
        if f == 2: return ((v & 15) * 17, ((v >> 4) & 15) * 17, ((v >> 8) & 15) * 17, ((v >> 12) & 15) * 17)
        return (v & 255, (v >> 8) & 255, (v >> 16) & 255, v >> 24)
    pal = None
    if fmt >= 4:
        cf = clutfmt & 3
        n = len(clut or b'') // (4 if cf == 3 else 2)
        pal = [conv(x, cf) for x in struct.unpack_from('<%d%s' % (n, 'I' if cf == 3 else 'H'), clut or b'')] or [(255, 0, 255, 255)]
        shift, mask, off = (clutfmt >> 2) & 31, (clutfmt >> 8) & 255, ((clutfmt >> 16) & 31) * 16
    for y in range(h):
        row = data[y * rowbytes:(y + 1) * rowbytes]
        for x in range(w):
            if fmt < 3 or fmt == 6:
                v = row[2 * x] | row[2 * x + 1] << 8
            elif fmt in (3, 7):
                v = struct.unpack_from('<I', row, 4 * x)[0]
            elif fmt == 5:
                v = row[x]
            else:
                v = (row[x >> 1] >> (4 * (x & 1))) & 15
            if pal is not None:
                c = pal[(((v >> shift) & mask) | off) % len(pal)]
            else:
                c = conv(v, fmt)
            out[4 * (y * w + x):4 * (y * w + x) + 4] = bytes(c)
    return out

def unswizzle(data, rowbytes, h):
    out = bytearray(len(data))
    bw = max(rowbytes // 16, 1)
    src = 0
    for by in range((h + 7) // 8):
        for bx in range(bw):
            for r in range(8):
                y = by * 8 + r
                if y < h:
                    d = y * rowbytes + bx * 16
                    out[d:d + 16] = data[src:src + 16]
                src += 16
    return bytes(out)

def write_png(path, w, h, rgba):
    raw = b''.join(b'\0' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def downscale(rgba, w, h):
    """Promedio 2x2 (para comparar el buffer grande con la imagen original)."""
    out = bytearray((w // 2) * (h // 2) * 4)
    for y in range(h // 2):
        for x in range(w // 2):
            for c in range(4):
                i = ((2 * y) * w + 2 * x) * 4 + c
                out[(y * (w // 2) + x) * 4 + c] = (rgba[i] + rgba[i + 4] + rgba[i + w * 4] + rgba[i + w * 4 + 4]) // 4
    return out

def diff(a, b):
    """Diferencia media por canal (0 = iguales) sin contar el alfa."""
    n = min(len(a), len(b)) // 4
    if not n:
        return None
    s = sum(abs(a[4 * i + c] - b[4 * i + c]) for i in range(0, n, 3) for c in range(3))
    return s / (3 * len(range(0, n, 3)))

# --- Vertices ------------------------------------------------------------------------------
def vlayout(vt):
    """(tamano del vertice, desplazamiento de la posicion, bytes por coordenada) como el GE."""
    comp = [0, 1, 2, 4]
    off, align = 0, 1
    def add(sz, n):
        nonlocal off, align
        if sz:
            off = (off + sz - 1) & ~(sz - 1)
            off += sz * n
            align = max(align, sz)
    add(comp[(vt >> 9) & 3], ((vt >> 14) & 7) + 1)
    add(comp[vt & 3], 2)
    add([0, 0, 0, 0, 2, 2, 2, 4][(vt >> 2) & 7], 1)
    add(comp[(vt >> 5) & 3], 3)
    ps = comp[(vt >> 7) & 3]
    if not ps:
        return off, 0, 0
    off = (off + ps - 1) & ~(ps - 1)
    po = off
    add(ps, 3)
    return (off + align - 1) & ~(align - 1), po, ps

def vbounds(data, vt, first=0, count=None):
    size, po, ps = vlayout(vt)
    if not size or not ps:
        return None
    n = len(data) // size if count is None else count
    xs, ys = [], []
    for i in range(first, min(n, len(data) // size)):
        p = i * size + po
        if ps == 4:
            x, y = struct.unpack_from('<2f', data, p)
        elif ps == 2:
            x, y = struct.unpack_from('<2h', data, p)
        else:
            x, y = struct.unpack_from('<2b', data, p)
        xs.append(x); ys.append(y)
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None

# --- Recorrido de los registros en orden --------------------------------------------------
def walk(dump):
    """Eventos en orden: ('cmd', addr, orig, final, late), ('draw', info, blobs), ('event', txt)..."""
    pending, carry = None, []
    late_idx = dump.plugin == '0.2-dev28'  # ahi los indices se grababan antes del registro de su draw
    for t, d in dump.recs:
        if t == CMDS:
            for i in range(0, len(d) - 11, 12):
                a, o, f = struct.unpack_from('<3I', d, i)
                if pending is not None and not (a & 1) and (o >> 24) in (4, 5, 6):
                    yield ('draw', pending[0], pending[1], (a, o, f))
                    pending = None
                yield ('cmd', a & ~1, o, f, a & 1)
        elif t == DRAW:
            if pending is not None:
                yield ('draw', pending[0], pending[1], None)
            k = u32s(d, 20)
            pending = (dict(zip(['idx', 'addr', 'op', 'vt', 'vaddr', 'iaddr', 'vsize', 'lower', 'upper', 'texmode',
                                 'omit', 'flags', 'fb', 'target', 'scale', 'offx', 'offy', 'tex', 'cur_fb', 'big_fb'], k)), carry)
            carry = []
        elif t == BLOB:
            what, addr, size = struct.unpack_from('<3I', d)
            if late_idx and what == B_INDICES:
                carry.append((what, addr, d[12:12 + size]))
            elif pending is not None:
                pending[1].append((what, addr, d[12:12 + size]))
            else:
                yield ('blob', what, addr, d[12:12 + size])
        elif t == EVENT:
            yield ('event', d.decode(errors='replace').rstrip('\n'))
        elif t == LIST:
            yield ('list',) + u32s(d, 4)
        elif t == FLIP:
            yield ('flip',) + u32s(d, 4)
        elif t == FB:
            yield ('fb',) + u32s(d, 8)
        elif t == TXLIST:
            a, n = u32s(d, 2)
            yield ('txlist', a, u32s(d[8:], n))
    if pending is not None:
        yield ('draw', pending[0], pending[1], None)

def draw_text(dr, blobs):
    f = dr['flags']
    dest = ('ignorado' if f & 2 else 'escala1 %08x' % dr['target'] if f & 8 else 'rt %08x x%d' % (dr['target'], dr['scale'])
            if f & 4 else 'buffer grande')
    s = 'DRAW #%d %s n=%d vt=%06x %s fb=%08x -> %s' % (dr['idx'], PRIMS[(dr['op'] >> 16) & 7] if dr['op'] >> 24 == 4 else
                                                      cname(dr['op'] >> 24), dr['op'] & 0xffff, dr['vt'], '2D' if f & 1 else '3D',
                                                      dr['fb'], dest)
    s += ' tex=%08x' % dr['tex'] if dr['tex'] else ''
    s += ' [%s%s]' % (TEXMODE.get(dr['texmode'], dr['texmode']), ', omitido' if dr['omit'] else '')
    orig = next((b for w, a, b in blobs if w == B_VERTS), None)
    fin = next((b for w, a, b in blobs if w == B_VERTS_FINAL), None)
    if orig and f & 1:
        bb = vbounds(orig, dr['vt'], dr['lower'], dr['upper'])
        if bb:
            s += ' caja=(%g,%g)-(%g,%g)' % bb
        if fin:
            bf = vbounds(fin, dr['vt'], dr['lower'], dr['upper'])
            if bf and bf != bb:
                s += ' -> (%g,%g)-(%g,%g)' % bf
    return s

# --- Comandos ------------------------------------------------------------------------------
def resumen(dump):
    print('%s: %s, GePatch %s, frame %d, %s, GE %s al empezar%s' % (os.path.basename(dump.path), dump.game, dump.plugin,
          dump.frame, 'PASIVO (referencia)' if dump.passive else 'ACTIVO', 'libre' if dump.idle else 'OCUPADO',
          ', ARCHIVO DANADO (%d KB perdidos)' % (dump.lost // 1024) if dump.truncated else ''))
    print('opciones:', ' '.join('%s=%d' % kv for kv in zip(OPT_NAMES, dump.opt)))
    if dump.end:
        idle, skipped, ncmds, flips = u32s(dump.end, 4)
        print('fin: %s; %d comandos, %d flips, %d datos sin guardar por tamano' % (
            dump.end[16:].split(b'\0')[0].decode(), ncmds, flips, skipped))
    counts, fbs, draws, events = {}, {}, {}, []
    for e in walk(dump):
        counts[e[0]] = counts.get(e[0], 0) + 1
        if e[0] == 'fb':
            fbs.setdefault((e[1], e[2], e[3], e[4]), [0, e[5], e[6]])[0] += 1
        elif e[0] == 'draw':
            k = (e[1]['fb'], TEXMODE.get(e[1]['texmode']), bool(e[1]['omit']))
            draws[k] = draws.get(k, 0) + 1
        elif e[0] == 'event':
            events.append(e[1])
    print('registros:', ', '.join('%s=%d' % kv for kv in sorted(counts.items())))
    print('fb usados (fb, ancho, formato del juego, destino: veces):')
    for (a, p, f, dec), (n, tv, tw) in sorted(fbs.items()):
        print('  %08x ancho=%d fmt=%d -> %s %08x ancho %d: %d' % (a, p, f, FB_DEC.get(dec, dec), tv, tw, n))
    print('draws por fb y decision:')
    for (fb, tm, om), n in sorted(draws.items(), key=lambda x: -x[1]):
        print('  fb %08x %-22s %s %d' % (fb, tm, 'omitidos' if om else '        ', n))
    if dump.snaps:
        print('instantaneas:', ', '.join('%s %dx%d' % (w, x, y) for w, x, y, f, d in dump.snaps))
    print('memoria guardada:', ', '.join('%s %08x (%d KB)' % (MEM_KIND.get(k, k), a, len(m) // 1024)
                                         for (k, a), m in sorted(dump.mem.items())))
    print('eventos del log (%d):' % len(events))
    for e in events:
        print('  ' + e)

def informe(dump):
    depth = 0
    for e in walk(dump):
        if e[0] == 'cmd':
            _, a, o, f, late = e
            c = o >> 24
            if late:
                print('        %08x   (parche posterior) %s %06x -> %s %06x' % (a, cname(c), o & 0xffffff, cname(f >> 24),
                                                                          f & 0xffffff))
                continue
            ch = '' if f == o else '  -> %s %06x' % (cname(f >> 24), f & 0xffffff) if f >> 24 != c else '  -> %06x' % (f & 0xffffff)
            print('%s%08x %-18s %06x%s' % ('  ' * depth, a, cname(c), o & 0xffffff, ch))
            if c == 0x0A and f >> 24 == 0x0A and f == o:
                depth = min(depth + 1, 8)
            elif c == 0x0B:
                depth = max(depth - 1, 0)
        elif e[0] == 'draw':
            print('  ' * depth + '>>> ' + draw_text(e[1], e[2]))
        elif e[0] == 'event':
            print('### ' + e[1])
        elif e[0] == 'list':
            print('=== lista %08x stall %08x (%s) frame %d' % (e[1], e[2], ['encolada', 'al frente', 'stall'][e[3]], e[4]))
        elif e[0] == 'flip':
            print('=== FLIP %08x ancho %d fmt %d (frame %d)' % e[1:])
        elif e[0] == 'fb':
            print('--- fb %08x ancho %d fmt %d -> %s %08x ancho %d escala %d' % (e[1], e[2], e[3], FB_DEC.get(e[4], e[4]),
                                                                            e[5], e[6], e[7]))
        elif e[0] == 'txlist':
            print('    lista del plugin %08x:' % e[1])
            for i, w in enumerate(e[2]):
                print('      %08x %-18s %06x' % (e[1] + 4 * i, cname(w >> 24), w & 0xffffff))

# --- Imagenes ------------------------------------------------------------------------------
def save(path, mem, fmt, w, h, stride):
    """Imagen de un buffer: se guarda opaca (el alfa de un fb no es transparencia) y, si es
    8888, su alfa aparte en gris."""
    if mem is None:
        return None
    px = to_rgba(mem, fmt, w, h, stride)
    if px is not None:
        img = bytearray(px)
        img[3::4] = b'\xff' * (len(img) // 4)
        write_png(path, w, h, img)
        if fmt == 3:
            al = bytearray(len(px))
            for c in range(3):
                al[c::4] = px[3::4]
            al[3::4] = b'\xff' * (len(al) // 4)
            write_png(path.replace('.png', '_alfa.png'), w, h, al)
    return px

def copies(dump):
    """Copias (TRANSFER) del frame con sus registros, armadas desde los comandos."""
    regs, out = {}, []
    for e in walk(dump):
        if e[0] != 'cmd' or e[4]:
            continue
        c, d = e[2] >> 24, e[2] & 0xffffff
        regs[c] = d
        if c == 0xEA:  # TRANSFERSTART
            g = lambda k: regs.get(k, 0)
            src = ((g(0xB3) & 0xff0000) << 8) | g(0xB2); dst = ((g(0xB5) & 0xff0000) << 8) | g(0xB4)
            out.append(dict(src=src, srcw=g(0xB3) & 0x7ff, dst=dst, dstw=g(0xB5) & 0x7ff, w=(g(0xEE) & 0x3ff) + 1,
                            h=((g(0xEE) >> 10) & 0x3ff) + 1, bpp=4 if d & 1 else 2,
                            dx=g(0xEC) & 0x3ff, dy=(g(0xEC) >> 10) & 0x3ff))
    return out

def imagenes(dump, outdir, textures=False):
    os.makedirs(outdir, exist_ok=True)
    made = []
    fbs = {}
    for e in walk(dump):
        if e[0] == 'fb':
            fbs.setdefault((e[1], e[2], e[3]), (e[4], e[5], e[6], e[7]))
    for (addr, pitch, fmt), (dec, tv, tw, sc) in sorted(fbs.items()):
        off = addr & 0x3fffff
        bpp = 4 if fmt == 3 else 2
        size = pitch * 272 * bpp
        for when, kr, kf in (('ini', 0, 1), ('fin', 2, 3)):
            if dump.passive:  # el juego dibujo en la VRAM real: es la imagen original
                m = dump.mem_at(kr, 0x04000000 + off, size)
            else:             # lo que el juego ve en su VRAM (copias); lo dibujado esta en el destino
                m = dump.mem_at(kf, 0x0A000000 + off, size)
            name = 'fb_%08x_%s.png' % (addr, when)
            if save(os.path.join(outdir, name), m, fmt, min(pitch, 1024), 272, pitch) is not None:
                made.append(name)
    if not dump.passive:
        for when, k in (('ini', 0), ('fin', 2)):
            name = 'buffer_grande_%s.png' % when
            if save(os.path.join(outdir, name), dump.mem_at(k, 0x04000000, 960 * 544 * 2), 0, 960, 544, 960) is not None:
                made.append(name)
        seen = set()
        for (addr, pitch, fmt), (dec, tv, tw, sc) in fbs.items():
            if dec in (1, 2) and tv not in seen:
                seen.add(tv)
                f = fmt if dec == 2 else 0
                h = 272 * sc
                name = '%s_%08x_de_%08x_fin.png' % ('rt' if dec == 1 else 'fbcopia', tv, addr)
                if save(os.path.join(outdir, name), dump.mem_at(2, tv, tw * h * (4 if f == 3 else 2)), f, tw, h, tw) is not None:
                    made.append(name)
    for c in copies(dump):
        m = dump.mem_at(4, c['dst'], 1) and dump.mem_at(4, c['dst'], ((c['dy'] + c['h'] - 1) * c['dstw'] + c['dx'] + c['w']) * c['bpp'])
        if m:
            name = 'copia_%08x_a_%08x_%dx%d.png' % (c['src'], c['dst'], c['w'], c['h'])
            px = to_rgba(m[(c['dy'] * c['dstw'] + c['dx']) * c['bpp']:], 3 if c['bpp'] == 4 else 0, c['w'], c['h'], c['dstw'])
            write_png(os.path.join(outdir, name), c['w'], c['h'], px)
            made.append(name)
    for i, (what, w, h, fmt, data) in enumerate(dump.snaps):
        name = 'instantanea_%02d_%s.png' % (i, re.sub(r'\W+', '_', what).strip('_'))
        if save(os.path.join(outdir, name), data, fmt, w, h, w) is not None:
            made.append(name)
    if textures:
        regs, clut, done = {}, b'', set()
        for e in walk(dump):
            if e[0] == 'cmd' and not e[4]:
                regs[e[2] >> 24] = e[2] & 0xffffff
            elif e[0] == 'blob' and e[1] == B_CLUT:
                clut = e[3]
            elif e[0] == 'draw':
                for w, a, b in e[2]:
                    if w == B_CLUT:
                        clut = b
                    if (w & 0xff) == B_TEX and (w >> 8) == 0 and a not in done:
                        done.add(a)
                        sz = regs.get(0xB8, 0); tw, th = 1 << (sz & 15), 1 << ((sz >> 8) & 15)
                        stride = regs.get(0xA8, 0) & 0x7ff or tw
                        px = to_rgba(b, regs.get(0xC3, 0) & 15, tw, th, stride, clut, regs.get(0xC5, 0), regs.get(0xC2, 0) & 1)
                        if px:
                            name = 'tex_%08x_%dx%d_f%d.png' % (a, tw, th, regs.get(0xC3, 0) & 15)
                            write_png(os.path.join(outdir, name), tw, th, px)
                            made.append(name)
    print('%d imagenes en %s' % (len(made), outdir))
    for m in made:
        print('  ' + m)

# --- Conversion a volcado de PPSSPP -------------------------------------------------------
CTX_RANGES = [(0x00, 0x02), (0x10, 0x10), (0x12, 0x28), (0x2c, 0x33), (0x36, 0x38), (0x42, 0x4D), (0x50, 0x51),
              (0x53, 0x58), (0x5B, 0xB5), (0xB8, 0xC3), (0xC5, 0xD0), (0xD2, 0xE9), (0xEB, 0xEC), (0xEE, 0xEE),
              (0xF0, 0xF6), (0xF8, 0xF9)]
SKIP_REG = {0x01, 0x02, 0x08, 0x0A, 0x0B, 0x0C, 0x0E, 0x0F, 0x10, 0x13, 0x14, 0x07, 0x09}

def f24(x):
    return struct.unpack('<I', struct.pack('<f', x))[0] >> 8

def zstd_raw(data):
    """Frame de zstd con bloques sin comprimir (no hace falta la biblioteca)."""
    out = bytearray(struct.pack('<IB', 0xFD2FB528, 0xE0) + struct.pack('<Q', len(data)))
    pos = 0
    while True:
        blk = data[pos:pos + 131072]
        pos += len(blk)
        last = pos >= len(data)
        out += struct.pack('<I', (len(blk) << 3) | last)[:3] + blk
        if last:
            return bytes(out)

def tex_bytes(regs, lv):
    bits = [16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8, 0, 0, 0, 0, 0][regs.get(0xC3, 0) & 15]
    sz = regs.get(0xB8 + lv, 0)
    w, h, bw = 1 << (sz & 15), 1 << ((sz >> 8) & 15), regs.get(0xA8 + lv, 0) & 0x7ff
    return bits * (bw * h + max(w - bw, 0)) // 8

def ppsspp(dump, out, plugin=False, upto=None, show=None):
    """plugin: lo que se ejecuto con el plugin (comandos y vertices parcheados, sin sus listas
    propias), para ver en PPSSPP como queda el buffer grande. upto: hasta ese draw. show:
    (direccion, ancho, formato) a mostrar al final en vez del flip del juego."""
    final = {}
    if plugin:  # valor final de cada comando (incluye parches posteriores)
        for e in walk(dump):
            if e[0] == 'cmd':
                final[e[1]] = e[3]
    if not dump.passive and not plugin:
        print('aviso: el volcado es ACTIVO; la VRAM y las texturas de fb son las del plugin. Para la referencia, '
              'volcar con el juego iniciado en modo pasivo (L al arrancar).')
    r = dump.regs(0)
    ge = list(r['ge']) if r and any(r['ge']) else [(i << 24) | v for i, v in enumerate(r['shadow'] if r else [0] * 256)]
    ge = [(i << 24) | (v & 0xffffff) for i, v in enumerate(ge)]
    ctx = [0] * 512
    st = r['state'] if r else [0] * 8
    ctx[5], ctx[6], ctx[7] = st[2], st[3], 0
    k = 17
    for a, b in CTX_RANGES:
        for c in range(a, b + 1):
            ctx[k] = ge[c]; k += 1
    m = r['mtx'] if r else [0.0] * 148
    for num, data, n, base in ((0x2A, 0x2B, 96, 0), (0x3A, 0x3B, 12, 96), (0x3C, 0x3D, 12, 108), (0x3E, 0x3F, 16, 120),
                               (0x40, 0x41, 12, 136)):
        ctx[k] = num << 24; k += 1
        for i in range(n):
            ctx[k] = (data << 24) | f24(m[base + i]); k += 1
    for num in (0x2A, 0x3A, 0x3C, 0x3E, 0x40):
        ctx[k] = num << 24; k += 1
    ctx[k] = 0x0C << 24
    cmds, buf, regs_pend, last = [], bytearray(), [], {}
    def push(data, align=4):
        while len(buf) % align:
            buf.append(0)
        p = len(buf); buf.extend(data); return p
    def emit(t, data, align=4):
        flush(); cmds.append((t, len(data), push(data, align)))
    def flush():
        if regs_pend:
            d = struct.pack('<%dI' % len(regs_pend), *regs_pend); regs_pend.clear()
            cmds.append((1, len(d), push(d)))
    emit(0, struct.pack('<512I', *ctx))
    vram = dump.mem_at(0, 0x04000000, 0x200000)
    if vram:  # VRAM al empezar (lo que ya estaba dibujado): MEMCPYDEST + MEMCPYDATA
        emit(7, struct.pack('<I', 0x04000000)); emit(8, vram, 1)
    regs, texdata, clut_cache, clut_sent = {}, {}, {}, False
    for e in walk(dump):
        if e[0] == 'cmd' and not e[4]:
            op = final.get(e[1], e[3]) if plugin else e[2]; c = op >> 24
            if plugin and c != e[2] >> 24 and c in SKIP_REG:
                continue  # el plugin lo cambio por un salto a su lista: no se reproduce
            regs[c] = op & 0xffffff
            if c == 0xC4:  # LOADCLUT: la paleta va antes. Si no se guardo (era igual), la de antes
                a = ((regs.get(0xB1, 0) & 0x0f0000) << 8) | regs.get(0xB0, 0)
                if not clut_sent:
                    d_ = clut_cache.get(a) or clut_cache.get(0x0A000000 | (a & 0x3fffff))
                    if d_:
                        emit(4, d_, 16)
                clut_sent = False
            if c not in SKIP_REG and c not in (4, 5, 6):
                regs_pend.append(op)
        elif e[0] == 'blob' and e[1] == B_CLUT:
            emit(4, e[3], 16)
            clut_cache[e[2]] = e[3]; clut_sent = True
        elif e[0] == 'blob' and e[1] == B_XFER_SRC:
            emit(5, e[3], 16)
        elif e[0] == 'draw':
            dr, blobs, prim = e[1], e[2], e[3]
            if upto is not None and dr['idx'] > upto:
                break
            for w, a, b in blobs:
                if (w & 0xff) == B_TEX:
                    texdata[(a, len(b))] = b
                elif w == B_CLUT:
                    emit(4, b, 16)
            levels = ((regs.get(0xC2, 0) >> 16) & 7) + 1
            if regs.get(0x1E, 0) & 1 or dr['vt'] & 3:
                for lv in range(levels):
                    a = ((regs.get(0xA8 + lv, 0) & 0x0f0000) << 8) | regs.get(0xA0 + lv, 0)
                    b = next((d for (ta, n), d in texdata.items() if ta == a or ta == (0x0A000000 | (a & 0x3fffff))), None)
                    if b is None and (a & 0x0f000000) == 0x04000000:  # textura en la VRAM: de la VRAM guardada
                        b = dump.mem_at(0, 0x04000000 | (a & 0x1fffff), tex_bytes(regs, lv))
                    if b is None:
                        continue
                    if (a & 0x0f000000) == 0x04000000:
                        emit(0x18 + lv, struct.pack('<IiII', a, regs.get(0xA8 + lv, 0) & 0x7ff, 1, 0) + b, 16)
                    else:
                        emit(0x10 + lv, b, 16)
            ib = next((b for w, a, b in blobs if w == B_INDICES), None)
            vb = next((b for w, a, b in blobs if w == B_VERTS), None)
            if plugin:
                vb = next((b for w, a, b in blobs if w == B_VERTS_FINAL), vb)
            if ib:
                emit(3, ib, 4)
            if vb:
                emit(2, vb, 4)
            regs_pend.append(prim[2] if prim else dr['op'])
        elif e[0] == 'flip' and not show:
            emit(9, struct.pack('<3I', e[1], e[2], e[3]))
    if show:
        emit(9, struct.pack('<3I', *show))
    flush()
    cb = b''.join(struct.pack('<BII', t, n, p) for t, n, p in cmds)
    gid = dump.game.replace('-', '').encode()[:9].ljust(9, b'\0')
    with open(out, 'wb') as f:
        f.write(b'PPSSPPGE' + struct.pack('<I', 6) + gid + b'\0\0\0')
        f.write(struct.pack('<II', len(cmds), len(buf)))
        for blk in (cb, bytes(buf)):
            z = zstd_raw(blk)
            f.write(struct.pack('<I', len(z)) + z)
    print('%s: %d comandos, %d KB de datos. Abrirlo en PPSSPP como si fuera un juego.' % (out, len(cmds), len(buf) // 1024))

# --- Comparacion pasivo / activo ----------------------------------------------------------
def signatures(dump):
    out = []
    for e in walk(dump):
        if e[0] == 'draw':
            dr = e[1]
            tex = dr['tex'] & 0x3fffff if (dr['tex'] & 0x0f000000) in (0x04000000, 0x0A000000) else dr['tex']
            out.append(((dr['op'] >> 16) & 7, dr['op'] & 0xffff, dr['vt'], tex, dr['fb'] & 0x3fffff, dr['flags'] & 1))
            out[-1] = (out[-1], dr)
    return out

def comparar(pas, act, outdir):
    os.makedirs(outdir, exist_ok=True)
    if not pas.passive or act.passive:
        print('aviso: el primero deberia ser pasivo y el segundo activo')
    a, b = signatures(pas), signatures(act)
    sm = difflib.SequenceMatcher(None, [x[0] for x in a], [x[0] for x in b], autojunk=False)
    same = sum(bl.size for bl in sm.get_matching_blocks())
    print('draws: pasivo %d, activo %d, iguales en orden %d (%.0f%%)' % (len(a), len(b), same, 100 * same / max(len(a), 1)))
    print('(si son muy distintos, no es el mismo frame/escena: volcar ambos en el mismo punto)')
    bad = {}
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == 'equal':
            for k in range(i2 - i1):
                dr = b[j1 + k][1]
                if dr['omit'] or dr['texmode'] not in (0, 4):
                    key = (dr['fb'], TEXMODE.get(dr['texmode']), bool(dr['omit']))
                    bad.setdefault(key, []).append(dr['idx'])
        elif op != 'equal':
            print('  distinto: pasivo draws %d-%d, activo %d-%d (%s)' % (i1, i2, j1, j2, op))
    print('draws que el plugin trato distinto (fb, decision): ejemplos de indices')
    for (fb, tm, om), idx in sorted(bad.items()):
        print('  fb %08x %-22s%s: %d draws, p.ej. #%s' % (fb, tm, ' omitidos' if om else '', len(idx), ' #'.join(map(str, idx[:6]))))
    # Imagenes: destinos de copias y fb de pantalla (original vs con el plugin, reducido)
    for c in copies(pas):
        size = ((c['dy'] + c['h'] - 1) * c['dstw'] + c['dx'] + c['w']) * c['bpp']
        ma, mb = pas.mem_at(4, c['dst'], size), act.mem_at(4, c['dst'], size)
        if ma and mb:
            f = 3 if c['bpp'] == 4 else 0
            pa = to_rgba(ma[(c['dy'] * c['dstw'] + c['dx']) * c['bpp']:], f, c['w'], c['h'], c['dstw'])
            pb = to_rgba(mb[(c['dy'] * c['dstw'] + c['dx']) * c['bpp']:], f, c['w'], c['h'], c['dstw'])
            base = os.path.join(outdir, 'copia_%08x' % c['dst'])
            write_png(base + '_original.png', c['w'], c['h'], pa); write_png(base + '_plugin.png', c['w'], c['h'], pb)
            alpha = sum(1 for i in range(3, len(pa), 4) if pa[i] != pb[i]) * 100 // max(len(pa) // 4, 1)
            print('copia -> %08x %dx%d: diferencia color %.1f, alfa distinto en %d%% de los pixeles' % (
                c['dst'], c['w'], c['h'], diff(pa, pb), alpha))
    fb = act.mem_at(2, 0x04000000, 960 * 544 * 2)
    if fb:
        big = downscale(to_rgba(fb, 0, 960, 544, 960), 960, 544)
        write_png(os.path.join(outdir, 'pantalla_plugin_reducida.png'), 480, 272, big)
        disp = [e for e in walk(pas) if e[0] == 'flip']
        if disp:
            top, w, fmt = disp[-1][1:4]
            m = pas.mem_at(2, top, w * 272 * (4 if fmt == 3 else 2))
            if m:
                orig = to_rgba(m, fmt, 480, 272, w)
                write_png(os.path.join(outdir, 'pantalla_original.png'), 480, 272, orig)
                print('pantalla (ultimo flip) original vs plugin reducido: diferencia %.1f' % diff(orig, big))

def main(a):
    if len(a) < 2 or a[0] not in ('resumen', 'informe', 'imagenes', 'ppsspp', 'comparar'):
        print(__doc__); return 1
    if a[0] == 'resumen': resumen(Dump(a[1]))
    elif a[0] == 'informe': informe(Dump(a[1]))
    elif a[0] == 'imagenes': imagenes(Dump(a[1]), a[2], '--texturas' in a)
    elif a[0] == 'ppsspp':
        opt = dict(x.split('=', 1) for x in a[3:] if '=' in x)
        show = tuple(int(v, 16) if i == 0 else int(v) for i, v in enumerate(opt['--mostrar'].split(','))) if '--mostrar' in opt else None
        ppsspp(Dump(a[1]), a[2], '--plugin' in a, int(opt['--hasta']) if '--hasta' in opt else None, show)
    elif a[0] == 'comparar': comparar(Dump(a[1]), Dump(a[2]), a[3])
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
