#!/bin/sh
# Instructions the capturer runs per frame in each scene, counted by Callgrind on an x86 build of the bench: the same code
# gives the same number, on any PC and under any load, so a change of 1 % shows. They are x86 instructions: only comparable
# with other runs of this script. Writes "scene=... ir_k/f=..." lines, and with --top the costliest functions of each scene.
# Usage: sh tools/perf/instr.sh [--top] [source folder, default the repo] [out file]
# Needs the apt packages valgrind, libc6-dev-i386 and lib32gcc-13-dev (gcc-multilib would remove the ARM compiler).
set -e
TOP=0; [ "$1" = --top ] && { TOP=1; shift; }
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/../.." && pwd); S=${1:-$R}; O=${2:-/dev/stdout}; B=$R/build/perf; P=$R/tools/perf; W=$B/instr; mkdir -p $B/inc; rm -rf $W; mkdir -p $W
for d in $VH/include/psp2* $VH/include/vitasdk*; do [ -e $B/inc/$(basename $d) ] || ln -s $d $B/inc/; done
[ -e $B/inc/taihen.h ] || ln -s $T/taipkg/include/taihen.h $B/inc/
gcc -m32 -g -O2 -w -I$P/host -I$B/inc -o $W/bench $S/tools/perf/capture_bench.c
: > $W/out
for s in rr_crowd rr_crowd_late rr_race rr_race_late sprites2d sublists images gow_menu_xfer gow_play_xfer; do
  (cd $W && valgrind --tool=callgrind --collect-atstart=no --callgrind-out-file=cg.$s.%p ./bench $s >/dev/null 2>&1)
  f=$(wc -c $W/cg.$s.* | sort -n | tail -2 | head -1 | awk '{print $2}')
  n=$(sed -n 's/^summary: *//p; s/^totals: *//p' $f | head -1)
  echo "scene=$s ir_k/f=$((n / 40000))" >> $W/out
  [ $TOP = 1 ] && callgrind_annotate $f | grep -E '^ *[0-9,]+ \(' | sed -n 2,13p | sed 's/\[.*//' | cut -c1-80
done
# The translator on real frames of Ridge Racer (tools/perf/data, dumps of the Vita)
gcc -m32 -g -O2 -w -I$P/host -I$B/inc -I$S/nativedraw -o $W/trb $S/tools/perf/translate_bench.c $P/host/vita_stubs.c -lm
for g in $P/data/*.bin.gz; do
  s=$(basename $g .bin.gz); gzip -dc $g > $W/$s.bin
  (cd $W && valgrind --tool=callgrind --collect-atstart=no --callgrind-out-file=cg.$s.out ./trb $s.bin 20 >/dev/null 2>&1)
  n=$(sed -n 's/^summary: *//p; s/^totals: *//p' $W/cg.$s.out | head -1)
  echo "scene=tr_$s ir_k/f=$((n / 20000))" >> $W/out
  [ $TOP = 1 ] && callgrind_annotate $W/cg.$s.out | grep -E '^ *[0-9,]+ \(' | sed -n 2,13p | sed 's/\[.*//' | cut -c1-80
done
cat $W/out > $O
