#!/bin/sh
# gepresent.suprx sin vitasdk completo (igual que tools/vprobe/vita/build.sh): arm-none-eabi-gcc + vita-toolchain +
# stubs de vita-libs-gen-2 (vita-headers/db) y libtaihen_stub.a del paquete taihen de vitasdk (NO stubs de taiHEN
# hechos a mano: con NIDs equivocados el plugin no carga y no avisa).
# Uso: VH=<vita-headers> STUBS=<salida de vita-libs-gen-2> TAI=<paquete taihen> sh build.sh
# Sombreadores del banco (v11): banco_shaders.h ya viene generado. Para regenerarlo desde banco_*.cg:
#   SHACC=<JagerDesu/vita-shaders compilado> sh build.sh shaders
# (Con el vitasdk completo bastaria: arm-vita-eabi-gcc ... -ltaihen_stub -lSceGxm_stub -lScePower_stub ...)
set -e
if [ "$1" = shaders ]; then
  D=$(pwd)
  for s in v f1 f2; do
    k=--fragment; [ $s = v ] && k=--vertex
    (cd $SHACC && qemu-arm-static -B 0x100000000 -L /usr/arm-linux-gnueabihf ./shacc $k $D/banco_$s.cg $D/banco_$s.gxp)
  done
  { echo "// Sombreadores del banco (gepresent v11), compilados con el compilador de Sony (JagerDesu/vita-shaders bajo qemu)"
    echo "// desde banco_v.cg, banco_f1.cg y banco_f2.cg. Regenerar: ver build.sh."
    for s in v f1 f2; do
      echo "static const unsigned char ${s}_gxp[] __attribute__((aligned(16))) = {"
      od -An -v -tx1 banco_$s.gxp | sed 's/ \([0-9a-f][0-9a-f]\)/0x\1,/g;s/^/ /'
      echo "};"
    done; } > banco_shaders.h
  rm -f banco_*.gxp
  exit 0
fi
F="-mcpu=cortex-a9 -mthumb -mfloat-abi=hard -mfpu=neon -O2 -Wall -Wno-format -nostdlib -fno-builtin -ffreestanding"
rm -rf o; mkdir -p o
arm-none-eabi-gcc $F -I$VH/include -I$TAI/include -c gepresent.c -o o/gepresent.o
arm-none-eabi-gcc $F -I$VH/include -I$TAI/include -c banco.c -o o/banco.o
# Stubs: cada funcion que falta sale del primer archivo de $STUBS que la tenga, en este orden de bibliotecas
arm-none-eabi-ld -r -o o/todo.o o/gepresent.o o/banco.o
for s in $(arm-none-eabi-nm -u o/todo.o | awk '{print $2}' | grep -v -E '^(__|tai|mem)' | sort -u); do
  ok=
  for lib in SceLibKernel_SceLibKernel SceIofilemgr_SceIofilemgr SceSysmem_SceSysmem SceKernelThreadMgr_SceThreadmgr \
             SceKernelDmacMgr_SceDmacmgr SceGxm_SceGxm ScePower_ScePower; do
    if [ -f $STUBS/${lib}_$s.S ]; then arm-none-eabi-as --defsym GEN_WEAK_EXPORTS=0 $STUBS/${lib}_$s.S -o o/s_$s.o; ok=1; break; fi
  done
  [ -n "$ok" ] || { echo "sin stub: $s"; exit 1; }
done
arm-none-eabi-gcc $F -Wl,-q -o gepresent.elf o/todo.o o/s_*.o $TAI/lib/libtaihen_stub.a -lgcc
vita-elf-create -e exports.yml gepresent.elf gepresent.velf
vita-make-fself -c gepresent.velf gepresent.suprx
