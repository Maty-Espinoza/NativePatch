#!/bin/sh
# Checks that the capturer of the working tree records the same frames as commit <ref> in every bench scene with the record:
# each complete frame's draws, states, vertices and texture rows (capture_bench with REC_DUMP), ARM build under qemu.
# Usage: sh tools/perf/rec_check.sh <commit>
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/../.." && pwd); B=$R/build/perf; P=$R/tools/perf; W=$B/rcc; mkdir -p $B/inc
for d in $VH/include/psp2* $VH/include/vitasdk*; do [ -e $B/inc/$(basename $d) ] || ln -s $d $B/inc/; done
rm -rf $W; git -C $R worktree prune; git -C $R worktree add -q --detach $W ${1:?usage: rec_check.sh <commit>}
cp $P/capture_bench.c $W/tools/perf/
F="-static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc"
arm-linux-gnueabihf-gcc $F -o $B/rc_old $W/tools/perf/capture_bench.c
arm-linux-gnueabihf-gcc $F -o $B/rc_new $P/capture_bench.c
git -C $R worktree remove --force $W
Q=$(command -v qemu-arm-static || command -v qemu-arm)
bad=0
for s in $(sed -n 's/^  { "\([a-z0-9_]*\)", .*/\1/p' $P/capture_bench.c); do
  rm -f $B/rec.old $B/rec.new
  REC_DUMP=$B/rec.old $Q $B/rc_old $s >/dev/null; REC_DUMP=$B/rec.new $Q $B/rc_new $s >/dev/null
  [ -f $B/rec.new ] || continue
  if cmp -s $B/rec.old $B/rec.new; then echo "$s: record equal ($(($(wc -c < $B/rec.new) >> 10)) KB)"; else echo "$s: record DIFFERS"; bad=1; fi
done
rm -f $B/rec.old $B/rec.new
exit $bad
