#!/bin/sh
# Checks that the translator of the working tree gives the same draws, state and vertices as commit <ref> on the real frames of
# tools/perf/data, both in the x86 build and in the ARM build under qemu (the one with the real NEON code).
# Usage: sh tools/perf/tr_check.sh <commit>
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/../.." && pwd); B=$R/build/perf; P=$R/tools/perf; W=$B/trc; mkdir -p $B/inc
[ -e $B/inc/taihen.h ] || ln -s $T/taipkg/include/taihen.h $B/inc/
rm -rf $W; git -C $R worktree prune; git -C $R worktree add -q --detach $W ${1:?usage: tr_check.sh <commit>}
cp $P/translate_bench.c $W/tools/perf/
F="-g -O2 -w -I$P/host -I$B/inc"
gcc -m32 $F -I$W/nativedraw -o $B/tr_old $W/tools/perf/translate_bench.c $P/host/vita_stubs.c -lm
gcc -m32 $F -I$R/nativedraw -o $B/tr_new $P/translate_bench.c $P/host/vita_stubs.c -lm
arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$P/host -I$B/inc -I$R/nativedraw -o $B/tr_arm $P/translate_bench.c $P/host/vita_stubs.c -lm
git -C $R worktree remove --force $W
Q=$(command -v qemu-arm-static || command -v qemu-arm)
for g in $P/data/*.bin.gz; do
  s=$(basename $g .bin.gz); gzip -dc $g > $B/$s.bin
  TR_DUMP=$B/$s.old $B/tr_old $B/$s.bin 2 >/dev/null; TR_DUMP=$B/$s.new $B/tr_new $B/$s.bin 2 >/dev/null; TR_DUMP=$B/$s.arm $Q $B/tr_arm $B/$s.bin 2 >/dev/null
  echo "$s x86:"; python3 $P/tr_diff.py $B/$s.old $B/$s.new
  echo "$s ARM:"; python3 $P/tr_diff.py $B/$s.old $B/$s.arm
done
