#!/usr/bin/env python3
"""Short summary of a run on the Vita (NativeLink + NativeDraw), to avoid reading the whole logs.

Usage: python3 resumen_corrida.py <run folder> [fps threshold, default 58] [--check [--ref <run folder>]]
Reads nativelink.log (one stats line every 600 vblanks = 10 s) and nativedraw_captura.log (every 1 s). Shows:
  1. per 10 s: game fps | waits at the sync (late, >5 ms, us per sync, max) | mirror copies and KB/frame
  2. capture seconds with fewer frames than the threshold or over 60,000 commands: where the Vita falls short
  3. total of the sublist memory check (checked and DISTINTAS)
With --check it also judges the run (OK/FAIL per metric, exit code 1 on FAIL; "--" if the log does not have it):
  fps: median of the run against the reference (median of --ref, or nominal 60/30 taken from the run's best windows):
       at most 5 fps lower on a 60 fps game, 2 on a 30 fps game
  DISTINTAS: 0 | HD frames: not lower than --ref by 5 points (without --ref: some) | vertice_fuera: not above --ref (or 0)
It finds the lines with regular expressions: if the text of a log line changes, it must be changed here.
"""
import os, re, statistics, sys

args = sys.argv[1:]
check = '--check' in args
if check: args.remove('--check')
ref = None
if '--ref' in args: i = args.index('--ref'); ref = args[i + 1]; del args[i:i + 2]
d = args[0]
threshold = int(args[1]) if len(args) > 1 else 58
# Opens file f of folder r; empty list if missing.
def load(f, r=d):
    p = os.path.join(r, f)
    return open(p, errors='replace') if os.path.exists(p) else []

print('== NativeLink cada 10 s: t fps | sync: atrasados >5ms us/sync max | espejo: copias KB/frame')
rows = {}
for l in load('nativelink.log'):
    r = rows.setdefault(l[:6], {})
    if m := re.search(r'stats rendimiento: fps=([\d.]+)', l): r['fps'] = m.group(1)
    if m := re.search(r'atrasados=(\d+) \(>5 ms: (\d+)\) espera=(\d+) us/sync \(max (\d+)\)', l): r['sync'] = ' '.join(m.groups())
    if m := re.search(r'copias=(\d+) paginas=\d+ \(KB/frame (\d+)\)', l): r['mir'] = ' '.join(m.groups())
for t, r in rows.items():
    if 'fps' in r: print(t, r['fps'], '|', r.get('sync', '-'), '|', r.get('mir', '-'))

print('\n== captura: segundos con frames < %d o > 60000 comandos (s, frames, comandos/f, draws/f, ARM us/f y max)' % threshold)
checked = differ = 0
for l in load('nativedraw_captura.log'):
    if m := re.search(r'^\s*(\d+) seg: frames=(\d+) .*?comandos/f=(\d+) .*?draws/f=(\d+) .*?ARM us/f=(\d+) \(max (\d+)\)', l):
        t, f, c, dr, a, amax = map(int, m.groups())
        if f < threshold or c > 60000: print('%5d s f=%d cmd=%d draws=%d ARM=%d max=%d' % (t // 1000, f, c, dr, a, amax))
    if m := re.search(r'revisadas=(\d+) DISTINTAS=(\d+)', l): checked += int(m.group(1)); differ += int(m.group(2))
print('\n== memoria de sublistas: revisadas=%d DISTINTAS=%d' % (checked, differ))

# Game fps per 10 s of folder r; frames drawn in HD, with the normal image, and with reason vertice_fuera.
def fpsOf(r): return [float(m.group(1)) for l in load('nativelink.log', r) if (m := re.search(r'stats rendimiento: fps=([\d.]+)', l))]
def hdOf(r):
    hd = nat = out = 0
    for l in load('nativedraw.log', r):
        if m := re.search(r'traductor: frames HD (\d+), con la imagen normal (\d+)', l): hd += int(m.group(1)); nat += int(m.group(2))
        if m := re.search(r'vertice_fuera=(\d+)', l): out += int(m.group(1))
    return hd, nat, out
# Prints one metric (ok: True, False or None if not measured) and returns 1 if it failed.
def row(name, ok, detail):
    word, color = ('--', 0) if ok is None else ('OK', 32) if ok else ('FAIL', 31)
    print(('\033[%dm%-4s\033[0m' if sys.stdout.isatty() else '%.0s%-4s') % (color, word), name + ':', detail)
    return ok is False

if check:
    print('\n== check' + (' against ' + ref if ref else ''))
    fps, base, bad = fpsOf(d), None, 0
    if ref and (rfps := fpsOf(ref)): base = statistics.median(rfps)
    elif fps: base = 60 if sorted(fps)[len(fps) * 9 // 10] >= 45 else 30
    if fps and base:
        run, tol = statistics.median(fps), 5 if base >= 45 else 2
        bad += row('fps', run >= base - tol, 'median %.1f, reference %.1f, at most %d lower' % (run, base, tol))
    else: bad += row('fps', False, 'no fps lines in nativelink.log')
    bad += row('DISTINTAS', differ == 0 if checked else None, '%d of %d checked' % (differ, checked))
    hd, nat, out = hdOf(d)
    pct = lambda a, b: 100 * a / b if b else 0
    rhd, rnat, rout = hdOf(ref) if ref else (0, 0, 0)
    if hd + nat:
        rp, p = pct(rhd, rhd + rnat), pct(hd, hd + nat)
        bad += row('HD frames', p >= rp - 5 if rhd else hd > 0, '%.1f%% of %d (reference %.1f%%)' % (p, hd + nat, rp))
        bad += row('vertice_fuera', pct(out, hd + nat) <= pct(rout, rhd + rnat), '%d frames (reference %d)' % (out, rout))
    else: row('HD frames', None, 'no traductor lines (without dibujar)')
    sys.exit(1 if bad else 0)
