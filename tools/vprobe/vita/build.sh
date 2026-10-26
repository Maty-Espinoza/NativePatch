#!/bin/sh
# vpcdram.suprx sin vitasdk completo: arm-none-eabi-gcc + vita-toolchain (vita-elf-create, vita-make-fself)
# + stubs generados con vita-libs-gen-2 desde vita-headers/db y taiHEN. Uso: VH=<vita-headers> STUBS=<salida de
# vita-libs-gen-2> sh build.sh
set -e
F="-mcpu=cortex-a9 -mthumb -mfloat-abi=hard -mfpu=neon -O2 -Wall -Wno-format -nostdlib -fno-builtin -ffreestanding"
rm -rf o; mkdir -p o
for f in SceLibKernel_SceLibKernel_sceIoOpen SceIofilemgr_SceIofilemgr_sceIoWrite SceIofilemgr_SceIofilemgr_sceIoClose \
  SceLibKernel_SceLibKernel_sceClibSnprintf SceLibKernel_SceLibKernel_sceClibVsnprintf SceLibKernel_SceLibKernel_sceClibMemcpy \
  SceLibKernel_SceLibKernel_sceKernelGetProcessTimeLow SceSysmem_SceSysmem_sceKernelGetMemBlockInfoByAddr \
  SceSysmem_SceSysmem_sceKernelAllocMemBlock SceSysmem_SceSysmem_sceKernelGetMemBlockBase SceSysmem_SceSysmem_sceKernelFreeMemBlock \
  SceLibKernel_SceLibKernel_sceKernelCreateThread SceLibKernel_SceLibKernel_sceKernelStartThread \
  SceKernelThreadMgr_SceThreadmgr_sceKernelDelayThread SceKernelThreadMgr_SceThreadmgr_sceKernelExitDeleteThread \
  SceCompat_SceCompat_sceCompatCache SceKernelDmacMgr_SceDmacmgr_sceDmacMemcpy; do
  arm-none-eabi-as --defsym GEN_WEAK_EXPORTS=0 $STUBS/$f.S -o o/$f.o
done
arm-none-eabi-gcc $F -I$VH/include -c vpcdram.c -o o/vpcdram.o
arm-none-eabi-gcc $F -Wl,-q -o vpcdram.elf o/*.o -lgcc
vita-elf-create -e exports.yml vpcdram.elf vpcdram.velf
vita-make-fself -c vpcdram.velf vpcdram.suprx
