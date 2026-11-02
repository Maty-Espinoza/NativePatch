#!/usr/bin/env python3
"""Draws the images of a NPTEST run on PPSSPP (tools/emu.sh) as sheets of 30 scenes, with number and name.

  python3 tools/nptest_sheet.py <folder with native_test.log and shots/>

Leaves sheet0.png, sheet1.png... there. Needs numpy and Pillow.
"""
import sys
import numpy as np
from PIL import Image, ImageDraw

B = sys.argv[1]
FMT = {'fb565': 0, 'dither': 0, 'fb5551': 1, 'fb4444': 2}  # the scenes with a 16-bit image
tiles = []
for n, name, *_ in (l.split() for l in open(f'{B}/native_test.log')):
    raw = open(f'{B}/shots/{int(n):03d}.raw', 'rb').read()
    if name in FMT:
        a = np.frombuffer(raw, '<u2').reshape(272, 512)[:, :480].astype(int)
        r, g, b = [((a & 31) * 8, (a >> 5 & 63) * 4, (a >> 11) * 8), ((a & 31) * 8, (a >> 5 & 31) * 8, (a >> 10 & 31) * 8),
                   ((a & 15) * 17, (a >> 4 & 15) * 17, (a >> 8 & 15) * 17)][FMT[name]]
        rgb = np.dstack([r, g, b]).astype('u1')
    else:
        rgb = np.frombuffer(raw, 'u1').reshape(272, 512, 4)[:, :480, :3]
    t = Image.fromarray(rgb).resize((320, 181), Image.NEAREST)
    d = ImageDraw.Draw(t); d.rectangle((0, 0, 150, 11), fill=(0, 0, 0)); d.text((1, 0), f'{n} {name}', fill=(255, 255, 0))
    tiles.append(t)
for p in range(0, len(tiles), 30):
    s = Image.new('RGB', (6 * 322, 5 * 183), (90, 0, 90))
    for k, t in enumerate(tiles[p:p + 30]): s.paste(t, (k % 6 * 322, k // 6 * 183))
    s.save(f'{B}/sheet{p // 30}.png')
