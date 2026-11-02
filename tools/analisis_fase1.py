#!/usr/bin/env python3
# Summarizes nativedraw_frames.log and nativedraw_tarde.log (NativeDraw extreme record, CSV) of one or more runs.
# Usage: python3 tools/analisis_fase1.py <folder> [<folder> ...] [--tabla]
# Counts only "race" frames (over 30,000 commands). Per folder: fps, slow frames and what each part
# cost the game per frame; with several, one column per folder. --tabla: also every 10 s of each
# run. Log columns: docs/dev/<language>/logs-and-switches.md (language: en or es).
import csv, sys, os, statistics as st

# Rows of CSV d/f with numeric t_ms; integer values become int.
def read_csv(d, f):
    p = os.path.join(d, f)
    if not os.path.exists(p): return []
    return [{k: (int(v) if v.lstrip('-').isdigit() else v) for k, v in r.items()} for r in csv.DictReader(open(p, errors='replace'))
            if r.get('t_ms', '').isdigit()]

# Averages and percentiles of the race frames of a folder: (figures dict, race rows), or None without data.
# Drops frames of 0 us or of 200 ms or more.
def summarize(d):
    F = [r for r in read_csv(d, 'nativedraw_frames.log') if 0 < r['frame_us'] < 200000]
    C = [r for r in F if r['comandos'] > 30000]
    T = [r for r in read_csv(d, 'nativedraw_tarde.log') if r['comandos'] > 30000]
    if not C: return None
    fus = sum(r['frame_us'] for r in C); n = len(C); m = lambda k: sum(r[k] for r in C) / n
    idle = [r['cpu_libre_x1000'] for r in C if r['cpu_libre_x1000'] <= 1000]
    p = lambda v, q: sorted(v)[min(len(v) - 1, int(len(v) * q))] if v else 0
    return {
        'frames de carrera': n, 'fps': n / (fus / 1e6),
        'frames > 17,5 ms (%)': 100 * sum(r['frame_us'] > 17500 for r in C) / n,
        'frames > 20 ms (%)': 100 * sum(r['frame_us'] > 20000 for r in C) / n,
        'frame p99 (ms)': p([r['frame_us'] for r in C], .99) / 1000,
        'espera a la Vita (us/frame)': m('compuerta_us'),
        'syncs con la Vita atrasada (por frame)': m('syncs_atrasados'),
        'espera al GE (us/frame)': m('sync_GE_us'), 'CPU libre del PSP (%)': st.mean(idle) / 10 if idle else 0,
        'espejo: CPU (us/frame)': m('espejo_us'), 'espejo: paginas/frame': m('espejo_paginas'), 'ganchos (us/frame)': m('ganchos_us'),
        'comandos/frame': m('comandos'), 'Vita: captura (us/frame)': m('captura_us'), 'Vita: CPU del capturador (us/frame)': m('captura_cpu_us'),
        'atraso de la Vita, mediana (us)': st.median([r['atraso_us'] for r in T]) if T else 0,
        'atraso p90 (us)': p([r['atraso_us'] for r in T], .9),
        'Vita: recorrer la lista, mediana (us)': st.median([r['vita_recorrio_us'] for r in T]) if T else 0,
        'juego: encolar -> sync, mediana (us)': st.median([r['encolada_hace_us'] for r in T]) if T else 0,
    }, C

folders = [a for a in sys.argv[1:] if not a.startswith('--')]
R = [(d, summarize(d)) for d in folders]
R = [(d, x) for d, x in R if x]
if not R: sys.exit('sin datos')
print(f"{'':42s}" + ''.join(f"{os.path.basename(d.rstrip('/'))[:18]:>19s}" for d, _ in R))
for k in R[0][1][0]:
    print(f"{k:42s}" + ''.join(f"{x[0][k]:19.1f}" for _, x in R))
if '--tabla' in sys.argv:
    for d, (_, C) in R:
        print(f"\n== {d}: cada 10 s (carrera): fps | espera a la Vita us | espera al GE us | CPU libre % | captura us")
        b = {}
        for r in C: b.setdefault(r['t_ms'] // 10000, []).append(r)
        for k in sorted(b):
            v = b[k]; fus = sum(r['frame_us'] for r in v)
            print(f"  {k * 10:5d} s {len(v) / (fus / 1e6):5.1f} | {sum(r['compuerta_us'] for r in v) / len(v):6.0f} | "
                  f"{sum(r['sync_GE_us'] for r in v) / len(v):6.0f} | {sum(r['cpu_libre_x1000'] for r in v) / len(v) / 10:5.1f} | "
                  f"{sum(r['captura_us'] for r in v) / len(v):6.0f}")
