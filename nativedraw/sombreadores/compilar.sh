#!/bin/sh
# Compiles the translator shaders (*.cg) to ../sombreadores_nd.h (byte arrays included by traductor.c) with
# Sony's compiler under qemu: JagerDesu/vita-shaders, with memmem(start, 0x300000, ...) in resolve.c and built with
# make CC=arm-linux-gnueabihf-gcc. Chain details: docs/agent/<language>/stage3-plan.md (Sombreadores).
# Usage: SHACC=<vita-shaders folder> sh compilar.sh   (only needed when a .cg changes: the .h is in the repo)
set -e
D=$(cd "$(dirname "$0")" && pwd); O=$D/../sombreadores_nd.h
echo "// Sombreadores del traductor (nativedraw/sombreadores/*.cg), compilados con sombreadores/compilar.sh. No editar." > $O
for s in nd_v nd_ft nd_fc; do
  k=--fragment; [ $s = nd_v ] && k=--vertex
  (cd $SHACC && qemu-arm-static -B 0x100000000 -L /usr/arm-linux-gnueabihf ./shacc $k $D/$s.cg /tmp/$s.gxp)
  echo "static const unsigned char ${s}_gxp[] __attribute__((aligned(16))) = {" >> $O
  od -An -v -tx1 /tmp/$s.gxp | sed 's/ \([0-9a-f][0-9a-f]\)/0x\1,/g;s/^/ /' >> $O
  echo "};" >> $O
done
