#!/usr/bin/env python3
"""Resumen corto de una corrida en la Vita (NativeLink + NativeDraw), para no leer los logs enteros.

Uso: python3 resumen_corrida.py <carpeta de la corrida, p. ej. vitabridge/etapa2_dev64/gt1> [umbral de fps]
Lee nativelink.log (una linea de stats cada 600 vblanks = 10 s) y nativedraw_captura.log (cada 1 s); en carpetas
anteriores al cambio de nombre, ge_patch.log y ge_patch_captura.log:
  1. por cada 10 s: fps del juego | esperas al sync (atrasados, >5 ms, us por sync, max) | copias del espejo y KB/frame
  2. segundos de la captura con menos frames que el umbral (58) o mas de 60 000 comandos: donde la Vita no alcanza
  3. total de la revision de la memoria de sublistas (revisadas y DISTINTAS)
  4. si hay ge_patch_banco.log en la carpeta (corridas viejas): promedio por configuracion del banco de nucleos (v13.7)
"""
import os, re, sys
from collections import defaultdict

d = sys.argv[1]
umbral = int(sys.argv[2]) if len(sys.argv) > 2 else 58
VIEJO = {'nativelink.log': 'ge_patch.log', 'nativedraw_captura.log': 'ge_patch_captura.log'}  # antes del cambio de nombre
def leer(f):
    for n in (f, VIEJO.get(f, f)):
        if os.path.exists(os.path.join(d, n)): return open(os.path.join(d, n), errors='replace')
    return []

print('== NativeLink cada 10 s: t fps | sync: atrasados >5ms us/sync max | espejo: copias KB/frame')
filas = {}
for l in leer('nativelink.log'):
    r = filas.setdefault(l[:6], {})
    if m := re.search(r'stats rendimiento: fps=([\d.]+)', l): r['fps'] = m.group(1)
    if m := re.search(r'atrasados=(\d+) \(>5 ms: (\d+)\) espera=(\d+) us/sync \(max (\d+)\)', l): r['sync'] = ' '.join(m.groups())
    if m := re.search(r'copias=(\d+) paginas=\d+ \(KB/frame (\d+)\)', l): r['esp'] = ' '.join(m.groups())
for t, r in filas.items():
    if 'fps' in r: print(t, r['fps'], '|', r.get('sync', '-'), '|', r.get('esp', '-'))

print('\n== captura: segundos con frames < %d o > 60000 comandos (s, frames, comandos/f, draws/f, ARM us/f y max)' % umbral)
rev = dist = 0
for l in leer('nativedraw_captura.log'):
    if m := re.search(r'^\s*(\d+) seg: frames=(\d+) .*?comandos/f=(\d+) .*?draws/f=(\d+) .*?ARM us/f=(\d+) \(max (\d+)\)', l):
        t, f, c, dr, a, amax = map(int, m.groups())
        if f < umbral or c > 60000: print('%5d s f=%d cmd=%d draws=%d ARM=%d max=%d' % (t // 1000, f, c, dr, a, amax))
    if m := re.search(r'revisadas=(\d+) DISTINTAS=(\d+)', l): rev += int(m.group(1)); dist += int(m.group(2))
print('\n== memoria de sublistas: revisadas=%d DISTINTAS=%d' % (rev, dist))

conf = defaultdict(list)
for l in leer('ge_patch_banco.log'):
    if m := re.search(r'nucleos ronda (\d+) (.+?)\s+rep \d+: grabar (\d+) us .*?hilos reloj/procesador ([\d/ ]+) us', l):
        conf[m.group(2)].append((int(m.group(1)), int(m.group(3)), m.group(4)))
if conf:
    print('\n== banco de nucleos: configuracion, grabaciones, us de reloj (mediana, min, max)')
    for k, v in conf.items():
        w = sorted(x[1] for x in v)
        print('%-26s n=%-3d mediana=%-6d min=%-6d max=%d' % (k, len(w), w[len(w) // 2], w[0], w[-1]))
