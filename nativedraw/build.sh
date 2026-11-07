#!/bin/sh
# Builds nativedraw.suprx without the full vitasdk: arm-none-eabi-gcc + vita-toolchain + vita-libs-gen-2 stubs
# (vita-headers/db) and libtaihen_stub.a from the vitasdk taihen package (NOT hand-made taiHEN stubs: with wrong NIDs
# the plugin does not load and gives no warning). Also builds the kernel half, nativedraw.skprx (kernel.c, exports in
# kernel.yml), and the stubs nativedraw.suprx calls it with (weak: the user plugin loads without it).
# Normally called by tools/build.sh (which prepares the variables).
# Usage: VH=<vita-headers> STUBS=<vita-libs-gen-2 output> TAI=<taihen package> [O=<output folder>] [PROD=1] sh build.sh
# PROD=1: production build, without the development measurements (NP_DEV).
set -e
F="-mcpu=cortex-a9 -mthumb -mfloat-abi=hard -mfpu=neon -O2 -Wall -Wno-format -nostdlib -fno-builtin -ffreestanding"
[ -n "$PROD" ] || F="$F -DNP_DEV"
O=${O:-o}; rm -rf $O; mkdir -p $O
# Kernel half: linked with the vitasdk ForDriver stubs; vita-elf-export gives the NID table vita-libs-gen makes the user stubs from
arm-none-eabi-gcc $F -D__VITA_KERNEL__ -I$VH/include -c kernel.c -o $O/kernel.o
arm-none-eabi-gcc $F -Wl,-q -o $O/kernel.elf $O/kernel.o -L$VH/lib -lSceSysmemForDriver_stub -lSceThreadmgrForDriver_stub -lgcc
vita-elf-create -e kernel.yml $O/kernel.elf $O/kernel.velf
vita-make-fself -c $O/kernel.velf $O/nativedraw.skprx
vita-elf-export k $O/kernel.elf kernel.yml $O/kernel_db.yml
vita-libs-gen $O/kernel_db.yml $O/kstubs >/dev/null
# (development: capture.c and presenter.c reserve memory through ndAlloc, which logs each block)
[ -n "$PROD" ] || A=-DsceKernelAllocMemBlock=ndAlloc
arm-none-eabi-gcc $F -I$VH/include -I$TAI/include -c nativedraw.c -o $O/nativedraw.o
for c in capture presenter; do arm-none-eabi-gcc $F $A -I$VH/include -I$TAI/include -c $c.c -o $O/$c.o; done
# Stubs: each missing function comes from the first $STUBS file that has it, in this library order
arm-none-eabi-ld -r -o $O/all.o $O/nativedraw.o $O/capture.o $O/presenter.o
for s in $(arm-none-eabi-nm -u $O/all.o | awk '{print $2}' | grep -v -E '^(__|tai|mem)' | sort -u); do
  ok=
  if [ -f $O/kstubs/NativeDrawKernel_NativeDrawKernel_$s.S ]; then  # (the kernel half: weak, so the plugin loads without it)
    arm-none-eabi-as --defsym GEN_WEAK_EXPORTS=1 $O/kstubs/NativeDrawKernel_NativeDrawKernel_$s.S -o $O/s_$s.o; continue
  fi
  for lib in SceLibKernel_SceLibKernel SceIofilemgr_SceIofilemgr SceSysmem_SceSysmem SceKernelThreadMgr_SceThreadmgr \
             SceKernelDmacMgr_SceDmacmgr SceGxm_SceGxm ScePower_ScePower SceAppMgr_SceAppMgrUser; do
    if [ -f $STUBS/${lib}_$s.S ]; then arm-none-eabi-as --defsym GEN_WEAK_EXPORTS=0 $STUBS/${lib}_$s.S -o $O/s_$s.o; ok=1; break; fi
  done
  [ -n "$ok" ] || { echo "no stub: $s"; exit 1; }
done
arm-none-eabi-gcc $F -Wl,-q -o $O/nativedraw.elf $O/all.o $O/s_*.o $TAI/lib/libtaihen_stub.a -lgcc
# vita-elf-create adds ~1.1 KB at the end of the code segment and fails if the data segment starts too close: it is
# linked again with the data one page (4 KB) after the end of the code, rounded to a page.
dat=$(arm-vita-eabi-readelf -lW $O/nativedraw.elf | awk '$1 == "LOAD" { print $3, $6; exit }' | { read a m; printf '0x%x' $(( (a + m + 0x1fff) & ~0xfff )); })
arm-none-eabi-gcc $F -Wl,-q -Wl,-Tdata=$dat -o $O/nativedraw.elf $O/all.o $O/s_*.o $TAI/lib/libtaihen_stub.a -lgcc
vita-elf-create -e exports.yml $O/nativedraw.elf $O/nativedraw.velf
vita-make-fself -c $O/nativedraw.velf $O/nativedraw.suprx
