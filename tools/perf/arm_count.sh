#!/bin/sh
# Real ARM instructions per frame of the translator on the real frames, counted under qemu one instruction at a time (slow). The
# x86 count (instr.sh) does not see NEON. --top also ranks functions and lines. Usage: sh tools/perf/arm_count.sh [--top]
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/../.." && pwd); B=$R/build/perf; P=$R/tools/perf; mkdir -p $B/inc
arm-linux-gnueabihf-gcc -g -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc -I$R/nativedraw -o $B/tr_count $P/translate_bench.c $P/host/vita_stubs.c -lm
Q=$(command -v qemu-arm-static || command -v qemu-arm)
cnt() { $Q -one-insn-per-tb -d nochain,exec -D /dev/stdout $B/tr_count $1 $2 2>/dev/null | grep -c '^Trace'; }
for g in $P/data/*.bin.gz; do
  s=$(basename $g .bin.gz); gzip -dc $g > $B/$s.bin
  a=$(cnt $B/$s.bin 1); b=$(cnt $B/$s.bin 2); echo "tr_$s arm_k/f=$(((b - a) / 1000))"
  if [ "$1" = --top ]; then
    $Q -one-insn-per-tb -d nochain,exec -D /dev/stdout $B/tr_count $B/$s.bin 3 2>/dev/null | awk -F'[/ ]' '/^Trace/{c[$5]++} END{for(k in c)print k,c[k]}' > $B/pc.txt
    python3 $P/arm_lines.py $B/pc.txt $B/tr_count $R/nativedraw/translator.c
  fi
done
