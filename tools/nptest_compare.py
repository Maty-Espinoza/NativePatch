#!/usr/bin/env python3
"""Compares the Vita image of each NPTEST scene with the PSP's, from the dumps of a run (ms0:/nptest_dumps = y).

  python3 tools/nptest_compare.py <dumps folder> <run folder> <output folder> [--blocks N] [--ref <folder>]

Each dump NNN/ needs f0_normal.bmp, f0_vita.bmp and f0.txt, and is matched to its scene by the bar code of the image
(the Vita's: the normal one can be caught half drawn). The reference is f0_normal.bmp or, with --ref (build/nptest of
tools/emu.sh), the PPSSPP image of each still scene whose hashes equal the Vita's: the exact PSP image. The Vita image is reduced to 480x272 and compared in 4x4 blocks, so fine scaling differences do not count: a block is bad
if a channel's average is off by more than 24. A scene is "same" with at most N bad blocks (default 8, of 8160).
Prints one line per scene and leaves compare.txt and pages page0.png... (PSP | Vita | differences) of what is not "same".
Exits with 1 if a scene that should be drawn in HD (expected "ok") is not "same". Needs numpy and Pillow.
"""
import os, re, sys
import numpy as np
from PIL import Image, ImageDraw

D, RUN, O = sys.argv[1:4]
N = int(sys.argv[sys.argv.index('--blocks') + 1]) if '--blocks' in sys.argv else 8
REF = sys.argv[sys.argv.index('--ref') + 1] if '--ref' in sys.argv else None
ref = {int(s[0]): s[4:6] for s in (l.split() for l in open(f'{REF}/native_test.log'))} if REF else {}
FMT = {'fb565': 0, 'dither': 0, 'fb5551': 1, 'fb4444': 2}  # the scenes with a 16-bit image
scenes = {int(s[0]): s for s in (l.split() for l in open(f'{RUN}/native_test.log'))}
os.makedirs(O, exist_ok=True)

def rgb(p): return np.asarray(Image.open(p).convert('RGB')).astype(int)

def shot(i, name):
    raw = open(f'{REF}/shots/{i:03d}.raw', 'rb').read()
    if name not in FMT: return np.frombuffer(raw, 'u1').reshape(272, 512, 4)[:, :480, :3].astype(int)
    a = np.frombuffer(raw, '<u2').reshape(272, 512)[:, :480].astype(int)
    c = [(a & 31, a >> 5 & 63, a >> 11, 31, 63, 31), (a & 31, a >> 5 & 31, a >> 10 & 31, 31, 31, 31), (a & 15, a >> 4 & 15, a >> 8 & 15, 15, 15, 15)][FMT[name]]
    return np.dstack([c[0] * 255 // c[3], c[1] * 255 // c[4], c[2] * 255 // c[5]])

def scene_id(img): return sum(1 << b for b in range(8) if img[270, b * 8 + 4].min() > 128)

def blocks(a, b):
    d = np.abs((a - b)[:272, :480].reshape(68, 4, 120, 4, 3).astype(float).mean((1, 3)))
    return (d > 24).any(2), d

found = {}
for n in sorted(os.listdir(D)):
    v, p = f'{D}/{n}/f0_vita.bmp', f'{D}/{n}/f0_normal.bmp'
    if 'Vita drawing' in open(f'{D}/{n}/f0.txt', errors='replace').readline() and os.path.exists(v):
        found[scene_id(rgb(v)[::2, ::2])] = n  # (a later dump of a scene replaces an earlier one)
    elif os.path.exists(p): found.setdefault(scene_id(rgb(p)), n)
tiles, bad = [], 0
out = open(f'{O}/compare.txt', 'w')
for i, s in sorted(scenes.items()):
    n = found.get(i)
    if n is None: res, k, tile = 'no dump', 0, None
    else:
        txt = open(f'{D}/{n}/f0.txt', errors='replace').readline()
        a = shot(i, s[1]) if ref.get(i) == s[4:6] and s[4] == s[5] else rgb(f'{D}/{n}/f0_normal.bmp')
        if 'Vita drawing' not in txt or not os.path.exists(f'{D}/{n}/f0_vita.bmp'):
            why = re.search(r'reason: (\w+)', txt)
            res, k, tile = 'normal' + (f' ({why[1]})' if why else ''), 0, None
        else:
            v = rgb(f'{D}/{n}/f0_vita.bmp')
            v = v[:544, :960].reshape(272, 2, 480, 2, 3).mean((1, 3)).astype(int)
            m, d = blocks(a, v)
            k = int(m.sum()); res = 'same' if k <= N else 'DIFF'
            if res == 'DIFF':
                e = np.clip(d * 4, 0, 255).astype('u1').repeat(4, 0).repeat(4, 1)
                tile = np.hstack([a[:272, :480].astype('u1'), v.astype('u1'), e])
            else: tile = None
    ok = s[2] != 'ok' or res == 'same'
    bad += not ok
    line = f"{i:>3} {s[1]:<13} expected {s[2]:<20} image {res:<28} bad blocks {k}{'' if ok else '  FAIL'}"
    print(line); out.write(line + '\n')
    if tile is not None: tiles.append((f'{i} {s[1]}: {k} bad blocks', tile))
for p in range(0, len(tiles), 12):
    pg = Image.new('RGB', (1440, 12 * 288), (40, 40, 40))
    for k, (t, a) in enumerate(tiles[p:p + 12]):
        pg.paste(Image.fromarray(a), (0, k * 288 + 16))
        ImageDraw.Draw(pg).text((2, k * 288 + 2), t, fill=(255, 255, 0))
    pg.save(f'{O}/page{p // 12}.png')
print(f'{sum(1 for s in scenes)} scenes, {bad} FAIL, {len(tiles)} with a different image')
sys.exit(bad > 0)
