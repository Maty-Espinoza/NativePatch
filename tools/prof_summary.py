#!/usr/bin/env python3
# Summary of the "prof" lines of nativedraw_capture.log (extreme log level, nativedraw/prof.h): for each stage, how many
# times per second, average and worst time and ms per second, ranked within each group, plus the frame budget of the game.
# Usage: prof_summary.py <run folder or log> [from_s to_s]   (from_s, to_s: only that part of the run, in log seconds)
import os, re, sys

def read(path, t0, t1):
    st, secs = {}, set()
    for line in open(path, errors='replace'):
        m = re.match(r'\s*(\d+) prof (\w+):(.*)', line)
        if not m or not t0 <= int(m[1]) / 1000 <= t1: continue
        secs.add((int(m[1]) // 1000))
        for name, n, avg, mx in re.findall(r' (\w+)=(\d+)/(\d+)/(\d+)', m[3]):
            s = st.setdefault((m[2], name), [0, 0, 0, 0])
            s[0] += int(n); s[1] += int(n) * int(avg); s[2] = max(s[2], int(mx)); s[3] += 1
    return st, max(len(secs), 1)

def main():
    if len(sys.argv) < 2: sys.exit(__doc__ or 'usage: prof_summary.py <folder or log> [from_s to_s]')
    p = sys.argv[1]
    if os.path.isdir(p): p = os.path.join(p, 'nativedraw_capture.log')
    t0, t1 = (float(sys.argv[2]), float(sys.argv[3])) if len(sys.argv) > 3 else (0, 1e12)
    st, secs = read(p, t0, t1)
    if not st: sys.exit('no prof lines: was the log level extreme?')
    print(f'{secs} s')
    g = lambda grp, name: st.get((grp, name), [0, 0, 0, 0])
    f = g('psp', 'frame')
    if f[0]:
        print(f'game: {f[0] / secs:.1f} fps, frame {f[1] / f[0] / 1000:.1f} ms (worst {f[2] / 1000:.1f})')
    for grp in dict.fromkeys(k[0] for k in st):
        print(f'\n{grp}{"" if grp in ("cpu", "frames") else " (sorted by ms per second)"}')
        rows = sorted(((k[1], v) for k, v in st.items() if k[0] == grp), key=lambda r: -r[1][1])
        for name, (n, us, mx, _) in rows:
            print(f'  {name:20} {n / secs:8.1f}/s  avg {us / n / 1000 if n else 0:8.2f} ms  max {mx / 1000:8.2f} ms  {us / secs / 1000:8.1f} ms/s')

if __name__ == '__main__':
    main()
