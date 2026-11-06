#!/usr/bin/env python3
# Reads the lines of capture_bench (--collect: several runs -> one JSON, the counters must repeat and qemu_us keeps the
# best run) and compares two JSON results scene by scene: the Vita estimate mem_us and the qemu CPU time, with the counters
# behind them. Exit 1 when a scene regressed: mem_us up at all (it is deterministic) or qemu_min_us up more than the tolerance (10 %: between two processes qemu varies ~5 %).
# Usage: compare.py --collect <runs.txt> <out.json> | compare.py <last.json> <ref.json> [--tol 10] | compare.py <last> <ref> --better
import json, re, sys

def parse(path):
    runs = {}
    for line in open(path):
        if not line.startswith('scene='): continue
        d = dict(kv.split('=') for kv in line.split())
        for k, v in d.items():
            if k != 'scene': d[k] = float(v)
        runs.setdefault(d['scene'], []).append(d)
    out = {}
    for s, all_ in runs.items():
        rs = [r for r in all_ if 'frames' in r] or all_[:1]  # (instr.sh adds a line with only the instructions; the translator has nothing else)
        for k in ('cmds/f', 'draws/f', 'verts/f', 'own256/f', 'hits/f', 'ucp_kb/f', 'ucmp_kb/f', 'ucw_kb/f', 'xw_kb/f', 'dma/f', 'dma_kb/f', 'clock/f', 'complete', 'want', 'mem_us/f', 'qemu_us/f', 'qemu_min_us'): rs[0].setdefault(k, 0)
        o = dict(rs[0]); o.update(next((r for r in all_ if 'ir_k/f' in r), {})); o['qemu_us/f'] = min(r['qemu_us/f'] for r in rs); o['qemu_min_us'] = min(r['qemu_min_us'] for r in rs); o['runs'] = len(rs)
        for r in rs:
            for k in r:
                if k not in ('qemu_us/f', 'qemu_min_us', 'scene') and r[k] != rs[0][k]: print(f'WARN {s}: {k} differs between runs ({r[k]} vs {rs[0][k]})')
        out[s] = o
    return out

def compare(last, ref, tol):
    bad = 0
    cols = ['mem_us/f', 'ir_k/f', 'want', 'qemu_min_us', 'qemu_us/f', 'own256/f', 'hits/f', 'ucp_kb/f', 'ucmp_kb/f', 'ucw_kb/f', 'xw_kb/f', 'dma/f', 'dma_kb/f', 'clock/f', 'complete']
    print(f'{"scene":<10}' + ''.join(f'{c:>22}' for c in cols))
    for s, o in last.items():
        r = ref.get(s)
        row = f'{s:<10}'
        for c in cols:
            v = o.get(c, 0); rv = r.get(c) if r else None
            cell = f'{v:g}' if rv is None else f'{v:g} ({v - rv:+g}, {((v - rv) / rv * 100) if rv else 0:+.1f}%)'
            row += f'{cell:>22}'
        print(row)
        if r:
            if o['mem_us/f'] > r['mem_us/f'] + 0.5: print(f'REGRESSION {s}: mem_us/f {r["mem_us/f"]:g} -> {o["mem_us/f"]:g}'); bad += 1
            if 'ir_k/f' not in o and o['qemu_min_us'] > r['qemu_min_us'] * (1 + tol / 100): print(f'REGRESSION {s}: qemu_min_us {r["qemu_min_us"]:g} -> {o["qemu_min_us"]:g} (over {tol}%)'); bad += 1
            if o.get('ir_k/f', 0) > r.get('ir_k/f', 1e12) * 1.01: print(f'REGRESSION {s}: ir_k/f {r["ir_k/f"]:g} -> {o["ir_k/f"]:g}'); bad += 1
            if o.get('want', 0) > r.get('want', 1e12): print(f'REGRESSION {s}: want {r["want"]:g} -> {o["want"]:g}'); bad += 1
            if o['complete'] < r['complete']: print(f'REGRESSION {s}: complete frames {r["complete"]:g} -> {o["complete"]:g}'); bad += 1
    print('verdict: ' + ('REGRESSION' if bad else 'ok (no regression)'))
    return bad

if '--better' in sys.argv:  # names the scenes that improved (mem_us down, or qemu_min down more than the tolerance)
    last, ref = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
    b = [s for s, o in last.items() if s in ref and (o['mem_us/f'] < ref[s]['mem_us/f'] - 0.5 or o.get('ir_k/f', 1e12) < ref[s].get('ir_k/f', 0) * 0.99 or o['qemu_min_us'] < ref[s]['qemu_min_us'] * 0.90)]
    print(' '.join(b)); sys.exit(0)
if sys.argv[1] == '--collect':
    json.dump(parse(sys.argv[2]), open(sys.argv[3], 'w'), indent=1); sys.exit(0)
tol = float(sys.argv[sys.argv.index('--tol') + 1]) if '--tol' in sys.argv else 10
sys.exit(1 if compare(json.load(open(sys.argv[1])), json.load(open(sys.argv[2])), tol) else 0)
