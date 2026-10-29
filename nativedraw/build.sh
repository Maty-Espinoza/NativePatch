#!/bin/sh
# Compila nativedraw.suprx sin el vitasdk completo: arm-none-eabi-gcc + vita-toolchain + stubs de vita-libs-gen-2
# (vita-headers/db) y libtaihen_stub.a del paquete taihen de vitasdk (NO stubs de taiHEN hechos a mano: con NIDs
# equivocados el plugin no carga y no avisa). Normalmente lo llama tools/compilar.sh (que prepara las variables).
# Uso: VH=<vita-headers> STUBS=<salida de vita-libs-gen-2> TAI=<paquete taihen> [O=<carpeta de salida>] sh build.sh
set -e
F="-mcpu=cortex-a9 -mthumb -mfloat-abi=hard -mfpu=neon -O2 -Wall -Wno-format -nostdlib -fno-builtin -ffreestanding"
O=${O:-o}; rm -rf $O; mkdir -p $O
for c in nativedraw captura; do arm-none-eabi-gcc $F -I$VH/include -I$TAI/include -c $c.c -o $O/$c.o; done
# Stubs: cada funcion que falta sale del primer archivo de $STUBS que la tenga, en este orden de bibliotecas
arm-none-eabi-ld -r -o $O/todo.o $O/nativedraw.o $O/captura.o
for s in $(arm-none-eabi-nm -u $O/todo.o | awk '{print $2}' | grep -v -E '^(__|tai|mem)' | sort -u); do
  ok=
  for lib in SceLibKernel_SceLibKernel SceIofilemgr_SceIofilemgr SceSysmem_SceSysmem SceKernelThreadMgr_SceThreadmgr \
             SceKernelDmacMgr_SceDmacmgr; do
    if [ -f $STUBS/${lib}_$s.S ]; then arm-none-eabi-as --defsym GEN_WEAK_EXPORTS=0 $STUBS/${lib}_$s.S -o $O/s_$s.o; ok=1; break; fi
  done
  [ -n "$ok" ] || { echo "sin stub: $s"; exit 1; }
done
arm-none-eabi-gcc $F -Wl,-q -o $O/nativedraw.elf $O/todo.o $O/s_*.o $TAI/lib/libtaihen_stub.a -lgcc
vita-elf-create -e exports.yml $O/nativedraw.elf $O/nativedraw.velf
vita-make-fself -c $O/nativedraw.velf $O/nativedraw.suprx
