#!/usr/bin/env python3
# Fase de analisis (desde GePatch dev65 + gepresent v13.9): resume nativedraw_frames.log y nativedraw_tarde.log (antes
# ge_patch_frames.log y ge_patch_tarde.log) de una o varias
# corridas. Uso: python3 tools/analisis_fase1.py <carpeta> [<carpeta> ...] [--tabla]
# "Carrera" = frames con mas de 30 000 comandos (GT en pista). Por carpeta: fps, frames lentos y lo que le costo cada
# parte al juego por frame; con varias, una columna por carpeta. --tabla: ademas, cada 10 s de cada corrida.
import csv, sys, os, statistics as st

def leer(d, f):
    p = os.path.join(d, f)
    if not os.path.exists(p): p = os.path.join(d, f.replace('nativedraw_', 'ge_patch_'))  # corridas viejas
    if not os.path.exists(p): return []
    return [{k: (int(v) if v.lstrip('-').isdigit() else v) for k, v in r.items()} for r in csv.DictReader(open(p, errors='replace'))
            if r.get('t_ms', '').isdigit()]

def resumen(d):
    F = [r for r in leer(d, 'nativedraw_frames.log') if 0 < r['frame_us'] < 200000]
    C = [r for r in F if r['comandos'] > 30000]
    T = [r for r in leer(d, 'nativedraw_tarde.log') if r['comandos'] > 30000]
    if not C: return None
    fus = sum(r['frame_us'] for r in C); n = len(C); m = lambda k: sum(r[k] for r in C) / n
    lib = [r['cpu_libre_x1000'] for r in C if r['cpu_libre_x1000'] <= 1000]
    p = lambda v, q: sorted(v)[min(len(v) - 1, int(len(v) * q))] if v else 0
    return {
        'frames de carrera': n, 'fps': n / (fus / 1e6),
        'frames > 17,5 ms (%)': 100 * sum(r['frame_us'] > 17500 for r in C) / n,
        'frames > 20 ms (%)': 100 * sum(r['frame_us'] > 20000 for r in C) / n,
        'frame p99 (ms)': p([r['frame_us'] for r in C], .99) / 1000,
        'espera a la Vita (us/frame)': m('compuerta_us'),
        'syncs con la Vita atrasada (por frame)': m('syncs_atrasados'),
        'espera al GE (us/frame)': m('sync_GE_us'), 'CPU libre del PSP (%)': st.mean(lib) / 10 if lib else 0,
        'espejo: CPU (us/frame)': m('espejo_us'), 'espejo: paginas/frame': m('espejo_paginas'), 'ganchos (us/frame)': m('ganchos_us'),
        'comandos/frame': m('comandos'), 'Vita: captura (us/frame)': m('captura_us'), 'Vita: CPU del capturador (us/frame)': m('captura_cpu_us'),
        'atraso de la Vita, mediana (us)': st.median([r['atraso_us'] for r in T]) if T else 0,
        'atraso p90 (us)': p([r['atraso_us'] for r in T], .9),
        'Vita: recorrer la lista, mediana (us)': st.median([r['vita_recorrio_us'] for r in T]) if T else 0,
        'juego: encolar -> sync, mediana (us)': st.median([r['encolada_hace_us'] for r in T]) if T else 0,
    }, C

carpetas = [a for a in sys.argv[1:] if not a.startswith('--')]
R = [(d, resumen(d)) for d in carpetas]
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
