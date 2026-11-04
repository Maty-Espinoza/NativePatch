#!/usr/bin/env python3
# Compares two translator dumps (translate_bench with TR_DUMP): same draws, state and vertices, floats within a tolerance.
# Usage: tr_diff.py <a> <b>   Exit 1 at the first difference.
import sys
def same(a, b):
    if a == b: return True
    if len(a) == 8 and len(b) == 8 and '.' not in a + b: return False  # (colors and programs, in hex)
    try: x, y = float(a), float(b)
    except ValueError: return False
    return abs(x - y) <= 1e-3 + 1e-3 * max(abs(x), abs(y))
n = 0
with open(sys.argv[1]) as fa, open(sys.argv[2]) as fb:
    for la, lb in zip(fa, fb):
        n += 1
        ta, tb = la.split(), lb.split()
        if len(ta) != len(tb) or not all(same(p, q) for p, q in zip(ta, tb)):
            print(f'line {n} differs:\n  {la.strip()}\n  {lb.strip()}'); sys.exit(1)
    if fa.read(1) or fb.read(1): print(f'different length after line {n}'); sys.exit(1)
print(f'ok   {n} lines equal')
