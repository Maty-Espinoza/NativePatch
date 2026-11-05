#!/usr/bin/env python3
# Compares two runs of the same game, zone by zone (seconds grouped by draws per frame), so that a performance change can be judged
# on the Vita. Reads nativedraw_capture.log and nativedraw.log of a run folder; works at the medium log level. For a fair result
# both runs need the same switches: with hold_game every frame is complete and fps and ms show what each stage costs.
# Usage: run_compare.py <run> [--save <file.json>]
#        run_compare.py <reference: folder or .json> <run> [--check]   --check: exit 1 if a zone got more than 10 % worse
import json, re, sys
ZONES = (('race', 300, 1500), ('hard', 1500, 2800), ('crowd', 2800, 99999))
# name, what it is, 1 if more is better
METRICS = (('fps', 'game fps', 1), ('hd', 'HD frames per second', 1), ('cap', 'capturer, ms of processor per frame', 0), ('gate', 'game waits, ms per frame', 0),
           ('tr', 'translator, ms per HD frame', 0), ('gxm', 'GXM, ms per HD frame', 0), ('vert', 'vertices, ms per HD frame', 0), ('tex', 'textures, ms per HD frame', 0),
           ('gpu', 'GPU, ms per HD frame (needs gpu_timer)', 0))
CAP = dict(frames=r'sec: frames=(\d+)', draws=r' draws/f=(\d+) \(max', proc=r'processor \(ms per sec\): capture=(\d+)', gate=r'gate_us=(\d+)')
BN = re.compile(r'\s*(\d+) bottleneck: slots \d+ drawn (\d+) .*?ms: translator (\d+) gpu_wait \d+ \| translator ms: check \d+ textures (\d+) vertices (\d+) gxm (\d+) scenes \d+ \| gpu ms: adrenaline \d+ normal \d+ surface (\d+) intermediate (\d+)')

def read(d):
    sec = {}
    for l in open(d + '/nativedraw_capture.log', errors='replace'):
        m = re.match(r'\s*(\d+)\s', l)
        if not m: continue
        x = sec.setdefault(int(m[1]) // 1000, {})
        for k, p in CAP.items():
            f = re.search(p, l)
            if f: x[k] = int(f[1])
    start = 0
    lines = open(d + '/nativedraw.log', errors='replace').readlines()
    for i, l in enumerate(lines):
        if re.search(r'NativeDraw \S+ built', l): start = i  # (this file is appended to: the last run starts here)
    for l in lines[start:]:
        m = BN.match(l)
        if m:
            x = sec.setdefault(int(m[1]) // 1000, {})
            x.update(zip(('drawn', 'tr', 'tex', 'vert', 'gxm', 'surf', 'inter'), map(int, m.groups()[1:])))
    out = {}
    for name, lo, hi in ZONES:
        s = [x for x in sec.values() if lo <= x.get('draws', 0) < hi and x.get('frames') and 'drawn' in x and 'proc' in x]
        if not s: continue
        n, fr, hd = len(s), sum(x['frames'] for x in s), sum(x['drawn'] for x in s) or 1
        t = lambda k: round(sum(x[k] for x in s) / hd, 1)
        out[name] = dict(seconds=n, fps=round(fr / n, 1), hd=round(sum(x['drawn'] for x in s) / n, 1), cap=round(sum(x['proc'] for x in s) / fr, 1),
                         gate=round(sum(x.get('gate', 0) * x['frames'] for x in s) / fr / 1000, 1), tr=t('tr'), gxm=t('gxm'), vert=t('vert'), tex=t('tex'),
                         gpu=round(sum(x['surf'] + x['inter'] for x in s) / hd, 1))
    return out

# What the console spent while the game drew in HD: average current, mAh and temperature ('resources:' lines of 10 s)
def res(d):
    R = re.compile(r'resources: frames with image underneath (\d+) without (\d+) \| GPU busy (\d+) ms idle (\d+) ms \| battery -?\d+ % (-?\d+) mAh -?\d+ mV current (-?\d+) charging (-?\d+) temp (-?\d+) case (-?\d+)')
    x = [list(map(int, m.groups())) for m in map(R.search, open(d + '/nativedraw.log', errors='replace')) if m]
    x = [v for v in x if v[0] + v[1]]
    if not x: return
    n = len(x)
    print(f"resources {d}: {n * 10} s, frames with image underneath {sum(v[0] for v in x)} without {sum(v[1] for v in x)}, GPU busy {sum(v[2] for v in x) / n / 100:.1f} %, "
          f"current {sum(v[5] for v in x) / n:.0f}, mAh {x[0][4]} -> {x[-1][4]}, temp {x[0][7]} -> {x[-1][7]}, case {x[0][8]} -> {x[-1][8]}{', charging' if any(v[6] for v in x) else ''}")

def load(p):
    return json.load(open(p)) if p.endswith('.json') else read(p)

a = [x for x in sys.argv[1:] if not x.startswith('--')]
if '--save' in sys.argv:
    p = sys.argv[sys.argv.index('--save') + 1]; a.remove(p)
    json.dump(read(a[0]), open(p, 'w'), indent=1); print('saved', p); sys.exit(0)
if len(a) < 2:
    for z, v in read(a[0]).items(): print(z, v)
    res(a[0])
    sys.exit(0)
ref, run, bad = load(a[0]), load(a[1]), 0
for z, _, _ in ZONES:
    if z not in ref or z not in run: print(f'== {z}: not in both runs'); continue
    print(f"== {z} ({ref[z]['seconds']} s against {run[z]['seconds']} s)")
    for k, what, up in METRICS:
        r, n = ref[z][k], run[z][k]
        if not r or not n: continue
        d = (n - r) * 100 / r
        worse = d < -10 if up else d > 10
        bad += worse
        print(f"  {what:42} {r:8} {n:8} {d:+6.1f} %{'  WORSE' if worse else ''}")
print('verdict:', f'{bad} worse' if bad else 'none worse')
for p in a[:2]:
    if not p.endswith('.json'): res(p)
sys.exit(1 if bad and '--check' in sys.argv else 0)
