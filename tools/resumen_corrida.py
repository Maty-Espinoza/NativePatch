#!/usr/bin/env python3
"""Resumen corto de una corrida en la Vita (NativeLink + NativeDraw), para no leer los logs enteros.

Uso: python3 resumen_corrida.py <carpeta de la corrida> [umbral de fps, 58 por defecto]
Lee nativelink.log (una linea de stats cada 600 vblanks = 10 s) y nativedraw_captura.log (cada 1 s). Muestra:
  1. por cada 10 s: fps del juego | esperas al sync (atrasados, >5 ms, us por sync, max) | copias del espejo y KB/frame
  2. segundos de la captura con menos frames que el umbral o mas de 60 000 comandos: donde la Vita no alcanza
  3. total de la revision de la memoria de sublistas (revisadas y DISTINTAS)
Busca las lineas con expresiones regulares: si cambia el texto de una linea del log, hay que cambiarla aca.
"""
import os, re, sys

d = sys.argv[1]
umbral = int(sys.argv[2]) if len(sys.argv) > 2 else 58
# Abre el archivo f de la carpeta; lista vacia si no esta.
def leer(f):
    p = os.path.join(d, f)
    return open(p, errors='replace') if os.path.exists(p) else []

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
