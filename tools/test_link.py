#!/usr/bin/env python3
"""Execution test: from the logs of a Vita run, checks that NativeLink and NativeDraw talked to each other.

Usage: python3 test_link.py <run folder> [version.txt]
Reads nativelink.log, nativedraw.log and nativedraw_capture.log. version.txt (build/installer/version.txt, or the one
inside the installer) makes it also compare the versions with that build. A run with no game (no "game=" line) only
checks NativeDraw and says the rest was skipped. Exit code 1 if a check fails.
It finds the lines with regular expressions: if the text of a log line changes, it must be changed here.
"""
import os, re, statistics, sys

d = sys.argv[1]
def load(f):
    p = os.path.join(d, f)
    return open(p, errors='replace').read() if os.path.exists(p) else ''
nl, nd, cap = load('nativelink.log'), load('nativedraw.log'), load('nativedraw_capture.log')
nums = lambda rx, text: [float(x) for x in re.findall(rx, text)]
bad = 0
# Prints the result of one check and counts it if it failed.
def check(name, ok, detail=''):
    global bad; bad += not ok
    print('ok  ' if ok else 'FAIL', name, '(%s)' % detail if detail else '')

want = dict(re.findall(r'(Native\w+) (\S+)', open(sys.argv[2]).read())) if len(sys.argv) > 2 else {}
game = 'game=' in nl
for name, text, rx, needed in (('NativeDraw', nd, r'NativeDraw (\S+) built', True), ('NativeLink', nl, r'NativeLink (\S+) \(build', game)):
    v = re.findall(rx, text)
    if needed: check(name + ' version', v and v[-1] == want.get(name, v[-1]), '%s, build %s' % (v[-1] if v else 'not in the log', want.get(name, 'not given')))

if not game: print('--   no game in this run: link checks skipped')
else:
    check('mailbox written by NativeLink', 'NativeLink active' in nl)
    check('mailbox seen by NativeDraw', 'NativeLink mailbox (shadow mode) found' in cap)
    check('the logs hold a single game', cap.count('NativeLink mailbox (shadow mode) found') == 1, '%d in the capturer log' % cap.count('NativeLink mailbox (shadow mode) found'))
    alive = re.findall(r'stats shadow: .*?vita=(\w+)', nl)
    check('Vita answers the mailbox', alive and alive[-1] == 'yes', 'last vita=%s' % (alive[-1] if alive else '?'))
    frames, lists, cmds = nums(r'sec: frames=(\d+)', cap), nums(r'sec: .*?lists/f=(\d+)', cap), nums(r'sec: .*?commands/f=(\d+)', cap)
    check('frames, lists and commands reach NativeDraw', sum(frames) and sum(lists) and sum(cmds), 'frames %d, lists/f max %d, commands/f max %d' % (sum(frames), max(lists, default=0), max(cmds, default=0)))
    captured, late = nums(r'captured=(\d+)', nl), nums(r'\(missing (-?\d+)\)', nl)
    check('NativeLink sees the lists captured', captured and captured[-1] > 0, 'captured %d, missing %d' % (captured[-1] if captured else 0, late[-1] if late else 0))
    lost, resets = nums(r'lost_lists=(\d+)', cap), nums(r'resets=(\d+)', cap)
    # A few lost lists are normal: a top-level list in VRAM before its first mirror copy.
    check('no events lost', lost and sum(lost) <= sum(captured[-1:]) / 100 and not sum(resets), 'lost_lists %d, resets %d' % (sum(lost), sum(resets)))
    fps = nums(r'stats performance: fps=([\d.]+)', nl)
    # Means of rates, not totals (minimum log level: one "sec:" line every 10 s) nor medians (menus and gameplay differ).
    a, b = statistics.mean(frames or [0]), statistics.mean(fps or [0])
    check('flips agree on both sides (within 20%)', a and b and abs(a - b) <= .2 * b, 'NativeDraw %.1f/s, NativeLink %.1f/s' % (a, b))
# With draw on, the translator runs in its own thread and its frames reach the screen
th = re.findall(r'translator: thread ([0-9a-f]{8})', nd)
if 'always replaces the image (draw)' in nd:
    hd = nums(r'translator: HD frames (\d+)', nd)
    check('translator thread started and HD frames shown', th and int(th[-1], 16) < 0x80000000 and sum(hd) > 0, 'thread %s, HD frames %d' % (th[-1] if th else 'not in the log', sum(hd)))
print('FAILED: %d' % bad if bad else 'all ok')
sys.exit(1 if bad else 0)
