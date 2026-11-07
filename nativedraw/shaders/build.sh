#!/bin/sh
# Compiles the translator shaders (*.cg) to ../shaders_nd.h (byte arrays included by translator.c) with
# Sony's compiler under qemu: JagerDesu/vita-shaders, with memmem(start, 0x300000, ...) in resolve.c and built with
# make CC=arm-linux-gnueabihf-gcc. Chain details: docs/agent/en/stage3-plan.md (Shaders). nd_v5_b<NB>l<NL> is nd_v5.cg with those
# defines (bones and lights its loops go through).
# Usage: SHACC=<vita-shaders folder> sh build.sh   (only needed when a .cg changes: the .h is in the repo)
set -e
D=$(cd "$(dirname "$0")" && pwd); O=$D/../shaders_nd.h
echo "// Translator shaders (nativedraw/shaders/*.cg), compiled with shaders/build.sh. Do not edit." > $O
for s in nd_v nd_v3 nd_v4 nd_v5 nd_v5_b4l0 nd_v5_b0l1 nd_ft nd_fc nd_fm nd_fd nd_fa nd_f2 nd_fr; do
  k=--fragment; case $s in nd_v*) k=--vertex;; esac
  c=$D/$s.cg
  case $s in nd_v5_*) v=${s#nd_v5_b}; c=/tmp/$s.cg; printf '#define NB %s\n#define NL %s\n' ${v%l*} ${v#*l} | cat - $D/nd_v5.cg > $c;; esac
  (cd $SHACC && qemu-arm-static -B 0x100000000 -L /usr/arm-linux-gnueabihf ./shacc $k $c /tmp/$s.gxp)
  echo "static const unsigned char ${s}_gxp[] __attribute__((aligned(16), unused)) = {" >> $O
  od -An -v -tx1 /tmp/$s.gxp | sed 's/ \([0-9a-f][0-9a-f]\)/0x\1,/g;s/^/ /' >> $O
  echo "};" >> $O
done
