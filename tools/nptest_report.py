#!/usr/bin/env python3
"""Reports a NPTEST run on the Vita, scene by scene: what NativeDraw did against what the scene expects.

  python3 tools/nptest_report.py <run folder> [native_test.log of another run, to compare hashes]

The run folder has native_test.log and nativedraw.log (a development build: lines "result at frame"). Scene "sync" is the
first fall to lines, so it gives the distance between the program's frames and the record's. A scene's result is the
one in force at each of its frames; expected "?" only informs. Exits with 1 if an expected result does not match.
"""
import re, sys

D = sys.argv[1]
scenes = [l.split() for l in open(f'{D}/native_test.log')]
res = [(int(m[1]), m[2]) for m in re.finditer(r'result at frame (\d+): (\w+)', open(f'{D}/nativedraw.log').read())]
first = {s[1]: int(s[3]) for s in scenes}
off = next(f for f, r in res if r == 'primitive') - first['sync']
other = {s[0]: s[4:6] for s in (l.split() for l in open(sys.argv[2]))} if len(sys.argv) > 2 else {}
ends = [int(s[3]) for s in scenes[1:]] + [10**9]
bad = 0
for s, end in zip(scenes, ends):
    a, b = int(s[3]) + off + 4, end + off - 2  # (a few frames of margin at each side of the change of scene)
    now = [r for f, r in res if f <= a][-1:]
    seen = dict.fromkeys(now + [r for f, r in res if a < f <= b])
    got = '+'.join(seen) or '-'
    ok = s[2] == '?' or list(seen) == [s[2]]
    bad += not ok
    h = '' if s[0] not in other else ' hash-differs' if other[s[0]] != s[4:6] else ''
    print(f"{s[0]:>3} {s[1]:<13} expected {s[2]:<20} got {got:<24}{'' if ok else ' FAIL'}{h}")
print(f'offset {off}, {bad} FAIL')
sys.exit(bad > 0)
