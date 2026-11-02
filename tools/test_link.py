#!/usr/bin/env python3
"""Execution test: from the logs of a Vita run, checks that NativeLink and NativeDraw talked to each other.

Usage: python3 test_link.py <run folder> [version.txt]
Reads nativelink.log, nativedraw.log and nativedraw_captura.log. version.txt (build/instalador/version.txt, or the one
inside the installer) makes it also compare the versions with that build. A run with no game (no "juego=" line) only
checks NativeDraw and says the rest was skipped. Exit code 1 if a check fails.
It finds the lines with regular expressions: if the text of a log line changes, it must be changed here.
"""
import os, re, statistics, sys

d = sys.argv[1]
def load(f):
    p = os.path.join(d, f)
    return open(p, errors='replace').read() if os.path.exists(p) else ''
nl, nd, cap = load('nativelink.log'), load('nativedraw.log'), load('nativedraw_captura.log')
nums = lambda rx, text: [float(x) for x in re.findall(rx, text)]
bad = 0
# Prints the result of one check and counts it if it failed.
def check(name, ok, detail=''):
    global bad; bad += not ok
    print('ok  ' if ok else 'FAIL', name, '(%s)' % detail if detail else '')

want = dict(re.findall(r'(Native\w+) (\S+)', open(sys.argv[2]).read())) if len(sys.argv) > 2 else {}
game = 'juego=' in nl
# The last session of each log: nativedraw.log can hold several.
for name, text, rx, needed in (('NativeDraw', nd, r'NativeDraw (\S+) compilado', True), ('NativeLink', nl, r'NativeLink (\S+) \(build', game)):
    v = re.findall(rx, text)
    if needed: check(name + ' version', v and v[-1] == want.get(name, v[-1]), '%s, build %s' % (v[-1] if v else 'not in the log', want.get(name, 'not given')))

if not game: print('--   no game in this run: link checks skipped')
else:
    check('mailbox written by NativeLink', 'NativeLink activo' in nl)
    check('mailbox seen by NativeDraw', 'buzon de NativeLink (modo sombra) encontrado' in cap)
    alive = re.findall(r'stats sombra: .*?vita=(\w+)', nl)
    check('Vita answers the mailbox', alive and alive[-1] == 'si', 'last vita=%s' % (alive[-1] if alive else '?'))
    frames, lists, cmds = nums(r'seg: frames=(\d+)', cap), nums(r'seg: .*?listas/f=(\d+)', cap), nums(r'seg: .*?comandos/f=(\d+)', cap)
    check('frames, lists and commands reach NativeDraw', sum(frames) and sum(lists) and sum(cmds), 'frames %d, lists/f max %d, commands/f max %d' % (sum(frames), max(lists, default=0), max(cmds, default=0)))
    captured, late = nums(r'capturadas=(\d+)', nl), nums(r'\(faltan (-?\d+)\)', nl)
    check('NativeLink sees the lists captured', captured and captured[-1] > 0, 'capturadas %d, missing %d' % (captured[-1] if captured else 0, late[-1] if late else 0))
    lost, resets = nums(r'listas_perdidas=(\d+)', cap), nums(r'reinicios=(\d+)', cap)
    # A few lost lists are normal: a top-level list in VRAM before its first mirror copy.
    check('no events lost', lost and sum(lost) <= sum(captured[-1:]) / 100 and not sum(resets), 'listas_perdidas %d, reinicios %d' % (sum(lost), sum(resets)))
    fps = nums(r'stats rendimiento: fps=([\d.]+)', nl)
    # Rates and not totals: at the minimum log level there is one "seg:" line every 10 s.
    a, b = statistics.median(frames or [0]), statistics.median(fps or [0])
    check('flips agree on both sides (within 20%)', a and b and abs(a - b) <= .2 * b, 'NativeDraw %.1f/s, NativeLink %.1f/s' % (a, b))
print('FAILED: %d' % bad if bad else 'all ok')
sys.exit(1 if bad else 0)
