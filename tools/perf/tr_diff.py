#!/usr/bin/env python3
# Compares two translator dumps (translate_bench with TR_DUMP): same draws, state and vertices, floats within a tolerance.
# Usage: tr_diff.py <a> <b>   Prints the first difference and how many lines differ; exit 1 if any.
import sys
def same(a, b, scale=0):
    if a == b: return True
    if len(a) == 8 and len(b) == 8 and '.' not in a + b: return False  # (colors and programs, in hex)
    try: x, y = float(a), float(b)
    except ValueError: return False
    return abs(x - y) <= 1e-3 + 1e-3 * max(abs(x), abs(y)) + 1e-5 * scale
n = bad = 0
with open(sys.argv[1]) as fa, open(sys.argv[2]) as fb:
    for la, lb in zip(fa, fb):
        n += 1
        ta, tb = la.split(), lb.split()
        # x, y, z and w carry w: a float32 rounding there is relative to the largest of them
        sc = max(abs(float(v)) for v in ta[1:5]) if ta[0] == 'V' and len(ta) > 4 else 0
        if len(ta) != len(tb) or not all(same(p, q, sc if 1 <= k <= 4 else 0) for k, (p, q) in enumerate(zip(ta, tb))):
            if not bad: print(f'line {n} differs:\n  {la.strip()}\n  {lb.strip()}')
            bad += 1
    if fa.read(1) or fb.read(1): print(f'different length after line {n}'); sys.exit(1)
if bad: print(f'{bad} of {n} lines differ'); sys.exit(1)
print(f'ok   {n} lines equal')
