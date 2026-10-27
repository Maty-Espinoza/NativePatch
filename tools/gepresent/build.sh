#!/bin/sh
# gepresent.suprx sin vitasdk completo (igual que tools/vprobe/vita/build.sh): arm-none-eabi-gcc + vita-toolchain +
# stubs de vita-libs-gen-2 (vita-headers/db) y libtaihen_stub.a del paquete taihen de vitasdk.
# Uso: VH=<vita-headers> STUBS=<salida de vita-libs-gen-2> TAI=<paquete taihen> sh build.sh
# (Con el vitasdk completo bastaria: arm-vita-eabi-gcc ... -ltaihen_stub -lSceGxm_stub -lSceLibKernel_stub ...)
set -e
F="-mcpu=cortex-a9 -mthumb -mfloat-abi=hard -mfpu=neon -O2 -Wall -Wno-format -nostdlib -fno-builtin -ffreestanding"
rm -rf o; mkdir -p o
for f in SceLibKernel_SceLibKernel_sceIoOpen SceLibKernel_SceLibKernel_sceIoGetstat SceSysmem_SceSysmem_sceKernelGetMemBlockInfoByAddr SceIofilemgr_SceIofilemgr_sceIoWrite SceIofilemgr_SceIofilemgr_sceIoClose \
  SceLibKernel_SceLibKernel_sceClibSnprintf SceLibKernel_SceLibKernel_sceClibVsnprintf \
  SceLibKernel_SceLibKernel_sceKernelGetProcessTimeLow SceLibKernel_SceLibKernel_sceKernelCreateThread SceSysmem_SceSysmem_sceKernelAllocMemBlock SceSysmem_SceSysmem_sceKernelGetMemBlockBase SceKernelDmacMgr_SceDmacmgr_sceDmacMemcpy \
  SceLibKernel_SceLibKernel_sceKernelStartThread SceKernelThreadMgr_SceThreadmgr_sceKernelDelayThread \
  SceKernelThreadMgr_SceThreadmgr_sceKernelExitDeleteThread \
  SceGxm_SceGxm_sceGxmTextureGetData SceGxm_SceGxm_sceGxmTextureInitLinear SceGxm_SceGxm_sceGxmMapMemory SceGxm_SceGxm_sceGxmTextureSetWidth \
  SceGxm_SceGxm_sceGxmTextureSetHeight SceGxm_SceGxm_sceGxmTextureSetFormat SceGxm_SceGxm_sceGxmTextureGetFormat \
  SceGxm_SceGxm_sceGxmTextureSetMinFilter SceGxm_SceGxm_sceGxmTextureSetMagFilter \
  SceGxm_SceGxm_sceGxmTextureGetMinFilter SceGxm_SceGxm_sceGxmTextureGetMagFilter; do
  arm-none-eabi-as --defsym GEN_WEAK_EXPORTS=0 $STUBS/$f.S -o o/$f.o
done
arm-none-eabi-gcc $F -I$VH/include -I$TAI/include -c gepresent.c -o o/gepresent.o
arm-none-eabi-gcc $F -Wl,-q -o gepresent.elf o/*.o $TAI/lib/libtaihen_stub.a -lgcc
vita-elf-create -e exports.yml gepresent.elf gepresent.velf
vita-make-fself -c gepresent.velf gepresent.suprx
