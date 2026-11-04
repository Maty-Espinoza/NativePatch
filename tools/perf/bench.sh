#!/bin/sh
# Builds and runs the capturer bench (capture_bench.c) on ARM under qemu and leaves build/perf/last.json: one entry per
# scene with its counters, the Vita estimate "mem_us" and the CPU time under qemu "qemu_us" (the best of RUNS runs).
# Then compares it with a reference (tools/perf/baseline.json unless REF says another file): compare.py prints the
# table and exits 1 on a regression. --save copies last.json over the reference (only when a change is kept).
# Needs: tools/setup.sh done and the apt packages qemu-user-static and gcc-arm-linux-gnueabihf (as tools/test_ci.sh).
# Usage: sh tools/perf/bench.sh [--save] [scene]     RUNS=n (default 5)  REF=<json>  T=<toolchain folder>
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/../.." && pwd); B=$R/build/perf; P=$R/tools/perf; mkdir -p $B/inc
SAVE=0; for a in "$@"; do [ "$a" = --save ] && SAVE=1; done
SCENE=""; for a in "$@"; do [ "$a" != --save ] && SCENE=$a; done
REF=${REF:-$P/baseline.json}; RUNS=${RUNS:-5}
for d in $VH/include/psp2* $VH/include/vitasdk*; do [ -e $B/inc/$(basename $d) ] || ln -s $d $B/inc/; done
arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc -o $B/capture_bench $P/capture_bench.c
Q=$(command -v qemu-arm-static || command -v qemu-arm)
: > $B/runs.txt
i=0; while [ $i -lt $RUNS ]; do $Q $B/capture_bench $SCENE >> $B/runs.txt; i=$((i + 1)); done
python3 $P/compare.py --collect $B/runs.txt $B/last.json
if [ -f $REF ]; then python3 $P/compare.py $B/last.json $REF; else echo "no reference ($REF): nothing to compare"; fi
[ $SAVE = 1 ] && cp $B/last.json $REF && echo "saved as reference: $REF"
exit 0
