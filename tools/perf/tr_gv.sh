#!/bin/sh
# Checks on the real frames of tools/perf/data that the GPU path of the translator (nd_v3) sends the GPU the same triangles as the
# CPU path, on the ARM build under qemu. Usage: sh tools/perf/tr_gv.sh
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/../.." && pwd); B=$R/build/perf; P=$R/tools/perf; mkdir -p $B/inc
[ -e $B/inc/taihen.h ] || ln -s $T/taipkg/include/taihen.h $B/inc/
for d in $VH/include/psp2* $VH/include/vitasdk*; do [ -e $B/inc/$(basename $d) ] || ln -s $d $B/inc/; done
arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc -I$R/nativedraw -o $B/tr_gv $P/translate_bench.c $P/host/vita_stubs.c -lm
Q=$(command -v qemu-arm-static || command -v qemu-arm)
for g in $P/data/*.bin.gz; do
  s=$(basename $g .bin.gz); gzip -dc $g > $B/$s.bin
  TR_TRI=$B/$s.cpu $Q $B/tr_gv $B/$s.bin 1 >/dev/null; TR_GV=1 TR_TRI=$B/$s.gpu $Q $B/tr_gv $B/$s.bin 1
  echo "$s:"; python3 $P/tr_diff.py $B/$s.cpu $B/$s.gpu
done
