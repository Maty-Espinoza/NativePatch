#!/usr/bin/env python3
# Says what limits the HD frames of a run, second by second, from the "bottleneck:" lines of nativedraw.log (development
# builds, `gpu_timer` on for the GPU figures) and the "sec:" and "record:" lines of nativedraw_capture.log.
# The drawing thread's second is split in: translator work, GPU (waits plus frames skipped because it was behind), no new
# frame (the capturer delivered nothing new) and the rest (Adrenaline and waiting for the next frame).
# Usage: bottleneck.py <run folder or log> [from_s to_s]   (only that part of the run, in log seconds)
import os, re, sys

SLOT = 1000 / 60
B = re.compile(r'\s*(\d+) bottleneck: slots (\d+) drawn (\d+) repeated (\d+) behind (\d+) no_record (\d+) normal (\d+) \| ms: translator (\d+) gpu_wait (\d+) \| '
               r'translator ms: check (\d+) textures (\d+) vertices (\d+) gxm (\d+) scenes (\d+) \| gpu ms: adrenaline (\d+) normal (\d+) surface (\d+) intermediate (\d+) idle (\d+)')
N = 'slots drawn repeated behind no_record normal translator gpu_wait check textures vertices gxm scenes adrenaline normal_img surface intermediate idle'.split()
PX = re.compile(r'\s*(\d+) bottleneck pixels .*?surface: fast (\d+) mid (\d+) textured (\d+) color (\d+) base (\d+) \| intermediate: fast (\d+) mid (\d+) textured (\d+) color (\d+)')
WHY = re.compile(r'\s*(\d+) bottleneck why generic .*?: function (\d+) double (\d+) alpha_test (\d+) fog (\d+) blend2x (\d+)')
WN = 'function double alpha_test fog blend2x'.split()
PN = 's_fast s_mid s_textured s_color s_base i_fast i_mid i_textured i_color'.split()

def seconds(path, t0, t1):
    rows, px, why = [], [], []
    for line in open(path, errors='replace'):
        m = B.match(line)
        if m and t0 <= int(m[1]) / 1000 <= t1:
            r = dict(zip(N, map(int, m.groups()[1:]))); r['t'] = int(m[1]) // 1000; rows.append(r); continue
        m = WHY.match(line)
        if m and t0 <= int(m[1]) / 1000 <= t1: why.append(dict(zip(WN, map(int, m.groups()[1:])))); continue
        m = PX.match(line)
        if m and t0 <= int(m[1]) / 1000 <= t1: px.append(dict(zip(PN, map(int, m.groups()[1:]))))
    return rows, px, why

def capture(path):
    # per second: capturer us per frame and record us per frame (and its parts)
    out, t = {}, None
    if not os.path.exists(path): return out
    for line in open(path, errors='replace'):
        m = re.match(r'\s*(\d+) sec: frames=(\d+).*ARM us/f=(\d+)', line)
        if m: t = int(m[1]) // 1000; out[t] = {'fps': int(m[2]), 'cap': int(m[3])}; continue
        m = re.search(r'record: frames=.* us=(\d+) \(states (\d+) vertices (\d+) textures (\d+)', line)
        if m and t in out: out[t].update(rec=int(m[1]), states=int(m[2]), verts=int(m[3]), tex=int(m[4]))
    return out

def cap_split(path):
    # "capture split" lines: us per frame by kind of frame, weighted by frames, in bands of draws per frame
    out = {}
    for line in open(path, errors='replace'):
        m = re.search(r'capture split (\w+): frames=(\d+) commands=(\d+) draws=(\d+) \| us/f: (.*)', line)
        if not m: continue
        v = {k: int(x) for k, x in re.findall(r'(\w+)=(\d+)', m[5])}
        d, n = int(m[4]), int(m[2])
        b = (m[1], 0 if d < 1500 else 1 if d < 3000 else 2)
        o = out.setdefault(b, {'frames': 0, 'commands': 0, 'draws': 0})
        o['frames'] += n; o['commands'] += n * int(m[3]); o['draws'] += n * d
        for k, x in v.items(): o[k] = o.get(k, 0) + n * x
    return out

def show_split(s):
    for (kind, band), o in sorted(s.items()):
        n = o['frames']; g = lambda k: o[k] / n / 1000
        interp = g('walk') - g('hit') - g('own') - g('draw'); stats = g('draw') - g('record')
        print(f'  {kind}, draws {("<1500", "1500-3000", ">3000")[band]}: {n} frames, {o["commands"] / n:.0f} commands, {o["draws"] / n:.0f} draws | ms: total {g("total"):.1f}, cpu {g("cpu"):.1f} | '
              f'interpreter {interp:.1f}, reads shared {g("hit"):.1f} own {g("own"):.1f}, draw() {stats:.1f}, record {g("record"):.1f} (state {g("state"):.1f}, index {g("index"):.1f}, vertices {g("vertices"):.1f}, blocks {g("blocks"):.1f}, rows {g("rows"):.1f}, textures {g("textures"):.1f}), '
              f'events {g("events"):.1f}, flip {g("flip"):.1f}, vflush {g("vflush"):.1f}')

def split(r):
    # ms of the drawing thread's second
    work = max(r['translator'] - r['gpu_wait'], 0)
    gpu = r['gpu_wait'] + r['behind'] * SLOT
    new = (r['repeated'] + r['no_record']) * SLOT
    return {'translator': work, 'GPU': gpu, 'no new frame': new, 'rest': max(1000 - work - gpu - new, 0)}

def verdict(s): return max(('translator', 'GPU', 'no new frame'), key=lambda k: s[k])

def table(title, rows):
    hd = sum(r['drawn'] for r in rows)
    if not hd: print(f'{title}: no HD frames'); return
    n = len(rows); s = [split(r) for r in rows]
    gpu_us = sum(r['surface'] + r['intermediate'] for r in rows)
    print(f'{title}: {n} s, {hd / n:.1f} HD frames/s, one every {1000 * n / hd:.1f} ms')
    print('  drawing thread, ms per HD frame:', ', '.join(f'{k} {sum(x[k] for x in s) / hd:.1f}' for k in s[0]))
    print('  translator, ms per HD frame:', ', '.join(f'{k} {sum(r[k] for r in rows) / hd:.1f}' for k in ('check', 'textures', 'vertices', 'gxm', 'scenes')))
    print(f'  GPU, ms per HD frame: surface {sum(r["surface"] for r in rows) / hd:.1f}, intermediate {sum(r["intermediate"] for r in rows) / hd:.1f}, total {gpu_us / hd:.1f}; '
          f'idle {sum(r["idle"] for r in rows) / n / 10:.0f} %')

def main():
    if len(sys.argv) < 2: sys.exit('usage: bottleneck.py <folder or log> [from_s to_s]')
    p = sys.argv[1]
    d = p if os.path.isdir(p) else os.path.dirname(p)
    if os.path.isdir(p): p = os.path.join(p, 'nativedraw.log')
    t0, t1 = (float(sys.argv[2]), float(sys.argv[3])) if len(sys.argv) > 3 else (0, 1e12)
    rows, px, why = seconds(p, t0, t1)
    rows = [r for r in rows if r['drawn']]
    if not rows: sys.exit('no bottleneck lines with HD frames: is this a development build?')
    table('all', rows)
    by = {}
    for r in rows: by.setdefault(verdict(split(r)), []).append(r)
    print('\nseconds by what limits them:')
    for k, v in sorted(by.items(), key=lambda kv: -len(kv[1])): table(f'{k} limits', v)
    if px:
        print('\nthousands of pixels per frame:')
        print('  ' + ', '.join(f'{k} {sum(x[k] for x in px) / len(px):.0f}' for k in PN))
        tot = sum(sum(x.values()) for x in px)
        print(f'  total {tot / len(px):.0f} ({tot / len(px) / 522:.1f} screens); on the middle program: {sum(x["s_mid"] + x["i_mid"] for x in px) * 100 / max(tot, 1):.0f} %; '
              f'image underneath: {sum(x["s_base"] for x in px) * 100 / max(tot, 1):.0f} %')
    if why: print('  why not the fast program: ' + ', '.join(f'{k} {sum(x[k] for x in why) / len(why):.0f}' for k in WN))
    cp = os.path.join(d, 'nativedraw_capture.log')
    if os.path.exists(cp) and (s := cap_split(cp)):
        print('\ncapturer split, per frame:'); show_split(s)
    cap = capture(cp)
    v = [cap[r['t']] for r in rows if r['t'] in cap] or [c for c in cap.values() if c.get('rec') is not None]
    v = [c for c in v if c.get('rec') is not None]
    if v:
        m = lambda k: sum(c[k] for c in v) / len(v) / 1000
        print(f'\ncapturer: {m("cap"):.1f} ms per game frame (worst second {max(c["cap"] for c in v) / 1000:.1f}), record {m("rec"):.1f} (states {m("states"):.1f}, vertices {m("verts"):.1f}, textures {m("tex"):.1f}); '
              f'seconds with the game below 58 fps: {sum(c["fps"] < 58 for c in v)} of {len(v)}')

if __name__ == '__main__':
    main()
