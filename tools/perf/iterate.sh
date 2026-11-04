#!/bin/sh
# One step of the performance loop (docs/agent/en/perf-loop.md): judges the working tree. Runs the bench against the
# reference (bench.sh) and the capturer test (correctness, the "capturer" part of tools/test_ci.sh), then prints the
# verdict: KEEP (faster somewhere, nowhere slower, test ok) or REVERT. --keep after a KEEP saves the new reference.
# Usage: sh tools/perf/iterate.sh [--keep]     RUNS, REF and T as in bench.sh
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/../.." && pwd); B=$R/build/perf; P=$R/tools/perf; mkdir -p $B/inc
REF=${REF:-$P/baseline.json}
echo "== bench"
bench=0; sh $P/bench.sh || bench=1
echo "== capturer test"
test=0
(cd $R/nativedraw && arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc -o $B/capture_test capture_test.c) || test=1
$(command -v qemu-arm-static || command -v qemu-arm) $B/capture_test | tail -1 | grep -q "all ok" || test=1
better=$(python3 $P/compare.py $B/last.json $REF --better 2>/dev/null || true)
echo "== verdict"
if [ $test = 1 ]; then echo "REVERT: the capturer test fails (the change breaks the capture)"; exit 1; fi
if [ $bench = 1 ]; then echo "REVERT: regression in the bench (see the table above)"; exit 1; fi
if [ -z "$better" ]; then echo "NO CHANGE: nothing got faster (keep only if the change has another purpose)"; exit 2; fi
echo "KEEP: faster in $better"
[ "$1" = --keep ] && cp $B/last.json $REF && echo "reference updated: $REF"
exit 0
