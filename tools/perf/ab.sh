#!/bin/sh
# Compares the working tree with another commit using the scenes of the working tree: instructions per frame (instr.sh) and the
# deterministic counters. Nothing about the machine enters. The commit must already build on the PC (from NativeDraw 0.0.16.8).
# Usage: sh tools/perf/ab.sh <commit>
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/../.." && pwd); B=$R/build/perf; P=$R/tools/perf; W=$B/ab; mkdir -p $B
rm -rf $W; git -C $R worktree prune; git -C $R worktree add -q --detach $W ${1:?usage: ab.sh <commit>}
cp $P/capture_bench.c $W/tools/perf/
sh $P/instr.sh $W $B/ab_old.txt; sh $P/instr.sh $R $B/ab_new.txt
git -C $R worktree remove --force $W
paste $B/ab_old.txt $B/ab_new.txt | awk '{ split($2, a, "="); split($4, b, "="); printf "%-24s %8d -> %8d  %+.1f%%\n", $1, a[2], b[2], (b[2] - a[2]) * 100 / a[2] }'
