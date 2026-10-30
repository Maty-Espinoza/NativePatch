#!/bin/sh
# Compila los sombreadores del traductor (*.cg) a ../sombreadores_nd.h con el compilador de Sony bajo qemu
# (JagerDesu/vita-shaders con memmem(start, 0x300000, en resolve.c; make CC=arm-linux-gnueabihf-gcc).
# Uso: SHACC=<carpeta de vita-shaders> sh compilar.sh   (solo hace falta al cambiar un .cg: el .h va en el repo)
set -e
D=$(cd "$(dirname "$0")" && pwd); O=$D/../sombreadores_nd.h
echo "// Sombreadores del traductor (nativedraw/sombreadores/*.cg), compilados con sombreadores/compilar.sh. No editar." > $O
for s in nd_v nd_ft nd_fc nd_fs; do
  k=--fragment; [ $s = nd_v ] && k=--vertex
  (cd $SHACC && qemu-arm-static -B 0x100000000 -L /usr/arm-linux-gnueabihf ./shacc $k $D/$s.cg /tmp/$s.gxp)
  echo "static const unsigned char ${s}_gxp[] __attribute__((aligned(16))) = {" >> $O
  od -An -v -tx1 /tmp/$s.gxp | sed 's/ \([0-9a-f][0-9a-f]\)/0x\1,/g;s/^/ /' >> $O
  echo "};" >> $O
done
