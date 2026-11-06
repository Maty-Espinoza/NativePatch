#!/bin/sh
# Builds and runs the memory-reading lab (tools/perf/sync_lab.c) on ARM under qemu, then the timing model
# (tools/perf/sync_model.py) for the games measured on the Vita.
# Needs: tools/setup.sh done and the apt packages qemu-user-static and gcc-arm-linux-gnueabihf (as tools/test_ci.sh).
# Usage: sh tools/perf/sync_lab.sh [check | <scene> [<mode>]]     T=<toolchain folder>
set -e
T=${T:-$HOME/tc}; . $T/env.sh
R=$(cd "$(dirname "$0")/../.." && pwd); B=$R/build/perf; mkdir -p $B/inc
for d in $VH/include/psp2* $VH/include/vitasdk*; do [ -e $B/inc/$(basename $d) ] || ln -s $d $B/inc/; done
arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I$B/inc -o $B/sync_lab $R/tools/perf/sync_lab.c
Q=$(command -v qemu-arm-static || command -v qemu-arm)
$Q $B/sync_lab "$@"
[ $# -eq 0 ] && python3 $R/tools/perf/sync_model.py
exit 0
