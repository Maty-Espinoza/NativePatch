#!/usr/bin/env python3
"""Short summary of a run on the Vita (NativeLink + NativeDraw), to avoid reading the whole logs.

Usage: python3 run_summary.py <run folder> [fps threshold, default 58] [--check [--ref <run folder>] [--target 30|60]]
Reads nativelink.log (one stats line every 600 vblanks = 10 s) and nativedraw_capture.log (every 1 s). Shows:
  1. per 10 s: game fps | waits at the sync (late, >5 ms, us per sync, max) | mirror copies and KB/frame
  2. capture seconds with fewer frames than the threshold or over 60,000 commands: where the Vita falls short
  3. total of the sublist memory check (reviewed and MISMATCHES)
With --check it also judges the run (OK/FAIL per metric, exit code 1 on FAIL; "--" if the log does not have it):
  fps: median of the run (windows outside gameplay do not decide) against the reference: --target, the median of --ref,
       GAME_FPS by the game= line of nativelink.log or, as a guess, 60/30 from the run's median:
       at most 5 fps lower on a 60 fps game, 2 on a 30 fps game
  MISMATCHES: 0 | HD frames: not lower than --ref by 5 points (without --ref: some) | vertex_out: not above --ref (or 0)
It finds the lines with regular expressions: if the text of a log line changes, it must be changed here.
"""
import os, re, statistics, sys

args = sys.argv[1:]
check = '--check' in args
if check: args.remove('--check')
ref = None
if '--ref' in args: i = args.index('--ref'); ref = args[i + 1]; del args[i:i + 2]
target = None
if '--target' in args:
    i = args.index('--target'); target = int(args[i + 1]); del args[i:i + 2]
    if target not in (30, 60): sys.exit('--target must be 30 or 60')
d = args[0]
threshold = int(args[1]) if len(args) > 1 else 58
# Nominal fps of the games the project tests, by the id in the game= line of nativelink.log.
GAME_FPS = {'UCUS-98632': 60, 'ULJS-00001': 60, 'NPUH-10119': 60, 'ULUS-10495': 30, 'ULUS-10450': 30, 'NPUH-10199': 60, 'NPEG-00024': 60}
# Opens file f of folder r; empty list if missing.
def load(f, r=d):
    p = os.path.join(r, f)
    return open(p, errors='replace') if os.path.exists(p) else []

print('== NativeLink every 10 s: t fps | sync: late >5ms us/sync max | mirror: copies KB/frame')
rows = {}
for l in load('nativelink.log'):
    r = rows.setdefault(l[:6], {})
    if m := re.search(r'stats performance: fps=([\d.]+)', l): r['fps'] = m.group(1)
    if m := re.search(r'late=(\d+) \(>5 ms: (\d+)\) wait=(\d+) us/sync \(max (\d+)\)', l): r['sync'] = ' '.join(m.groups())
    if m := re.search(r'copies=(\d+) pages=\d+ \(KB/frame (\d+)\)', l): r['mir'] = ' '.join(m.groups())
for t, r in rows.items():
    if 'fps' in r: print(t, r['fps'], '|', r.get('sync', '-'), '|', r.get('mir', '-'))

print('\n== capture: seconds with frames < %d or > 60000 commands (s, frames, commands/f, draws/f, ARM us/f and max)' % threshold)
checked = differ = 0
for l in load('nativedraw_capture.log'):
    if m := re.search(r'^\s*(\d+) sec: frames=(\d+) .*?commands/f=(\d+) .*?draws/f=(\d+) .*?ARM us/f=(\d+) \(max (\d+)\)', l):
        t, f, c, dr, a, amax = map(int, m.groups())
        if f < threshold or c > 60000: print('%5d s f=%d cmd=%d draws=%d ARM=%d max=%d' % (t // 1000, f, c, dr, a, amax))
    if m := re.search(r'reviewed=(\d+) MISMATCHES=(\d+)', l): checked += int(m.group(1)); differ += int(m.group(2))
print('\n== sublist memory: reviewed=%d MISMATCHES=%d' % (checked, differ))

# Game fps per 10 s of folder r; frames drawn in HD, with the normal image, and with reason vertex_out.
def fpsOf(r): return [float(m.group(1)) for l in load('nativelink.log', r) if (m := re.search(r'stats performance: fps=([\d.]+)', l))]
def hdOf(r):
    hd = nat = out = 0
    for l in load('nativedraw.log', r):
        if m := re.search(r'translator: HD frames (\d+), normal image (\d+)', l): hd += int(m.group(1)); nat += int(m.group(2))
        if m := re.search(r'vertex_out=(\d+)', l): out += int(m.group(1))
    return hd, nat, out
# Prints one metric (ok: True, False or None if not measured) and returns 1 if it failed.
def row(name, ok, detail):
    word, color = ('--', 0) if ok is None else ('OK', 32) if ok else ('FAIL', 31)
    print(('\033[%dm%-4s\033[0m' if sys.stdout.isatty() else '%.0s%-4s') % (color, word), name + ':', detail)
    return ok is False

if check:
    print('\n== check' + (' against ' + ref if ref else ''))
    fps, base, source, bad = fpsOf(d), None, '', 0
    game = [m.group(1) for l in load('nativelink.log') if (m := re.search(r'\bgame=(\S+)', l))]
    if target: base, source = target, '--target'
    elif ref and (rfps := fpsOf(ref)): base, source = statistics.median(rfps), '--ref'
    elif game and game[-1] in GAME_FPS: base, source = GAME_FPS[game[-1]], 'GAME_FPS ' + game[-1]
    elif fps: base, source = 60 if statistics.median(fps) >= 45 else 30, 'GUESS from the median, not a real verdict: use --target or --ref'
    if fps and base:
        run, tol = statistics.median(fps), 5 if base >= 45 else 2
        bad += row('fps', run >= base - tol, 'median %.1f, reference %.1f (%s), at most %d lower' % (run, base, source, tol))
    else: bad += row('fps', False, 'no fps lines in nativelink.log')
    bad += row('MISMATCHES', differ == 0 if checked else None, '%d of %d reviewed' % (differ, checked))
    hd, nat, out = hdOf(d)
    pct = lambda a, b: 100 * a / b if b else 0
    rhd, rnat, rout = hdOf(ref) if ref else (0, 0, 0)
    if hd + nat:
        rp, p = pct(rhd, rhd + rnat), pct(hd, hd + nat)
        bad += row('HD frames', p >= rp - 5 if rhd else hd > 0, '%.1f%% of %d (reference %.1f%%)' % (p, hd + nat, rp))
        bad += row('vertex_out', pct(out, hd + nat) <= pct(rout, rhd + rnat), '%d frames (reference %d)' % (out, rout))
    else: row('HD frames', None, 'no translator lines (without draw)')
    sys.exit(1 if bad else 0)
