// kernel.c: NativeDraw's kernel half (nativedraw.skprx, loaded by taiHEN from the *KERNEL section of config.txt). The
// PSP's VRAM lives in a 2 MB memory of the Vita's chip (the "compat RAM", the Tachyon eDRAM, at physical address
// 0x1C000000) that no process maps. This module gives it to NativeDraw (user side, in Adrenaline's process): a memory
// block mapped on that physical address in the calling process, or a copy made from a kernel mapping. At boot it only
// registers its exports. Built by nativedraw/build.sh with nativedraw_kernel.yml.
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/threadmgr.h>

#define VRAM_PADDR 0x1C000000u
#define VRAM_SIZE 0x200000u

// Memory block of the given type (a user one) mapped on the compat RAM, in the process that calls: its size is `size`
// bytes (0 or too big: all of it) and its address in that process goes to *ubase. Returns the block's UID for that
// process, or an error.
int ndkVramMap(uint32_t type, uint32_t size, void **ubase) {
  uint32_t state;
  ENTER_SYSCALL(state);
  SceKernelAllocMemBlockKernelOpt opt;
  for (uint32_t i = 0; i < sizeof opt / 4; i++) ((uint32_t *)&opt)[i] = 0;
  opt.size = sizeof opt; opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_PADDR; opt.paddr = VRAM_PADDR;
  if (!size || size > VRAM_SIZE) size = VRAM_SIZE;
  void *base = 0;
  int r = ksceKernelAllocMemBlock("NativeDrawVram", type, size, &opt);
  if (r >= 0) {
    SceUID b = r;
    r = ksceKernelGetMemBlockBase(b, &base);
    if (r >= 0) r = ksceKernelCreateUserUid(ksceKernelGetProcessId(), b);
  }
  if (ubase) ksceKernelCopyToUser(ubase, &base, sizeof base);
  EXIT_SYSCALL(state);
  return r;
}

// Physical address of user address ua (to check what a mapping points to). Returns 0 or an error.
int ndkPaddr(const void *ua, uint32_t *upa) {
  uint32_t state;
  ENTER_SYSCALL(state);
  uintptr_t pa = 0;
  int r = ksceKernelVAtoPA(ua, &pa);
  if (upa) ksceKernelCopyToUser(upa, &pa, sizeof pa);
  EXIT_SYSCALL(state);
  return r;
}

// Copies n bytes of the compat RAM, from offset off, to the user buffer udst, through a kernel mapping made on the
// first call (for when no user mapping works). Returns 0 or an error.
int ndkVramRead(uint32_t off, void *udst, uint32_t n) {
  static volatile uint8_t *kbase;
  uint32_t state;
  ENTER_SYSCALL(state);
  int r = 0;
  if (!kbase) {
    SceKernelAllocMemBlockKernelOpt opt;
    for (uint32_t i = 0; i < sizeof opt / 4; i++) ((uint32_t *)&opt)[i] = 0;
    opt.size = sizeof opt; opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_PADDR; opt.paddr = VRAM_PADDR;
    r = ksceKernelAllocMemBlock("NativeDrawVramK", SCE_KERNEL_MEMBLOCK_TYPE_KERNEL_ROOT_NC_RW, VRAM_SIZE, &opt);
    void *b = 0;
    if (r >= 0) r = ksceKernelGetMemBlockBase(r, &b);
    if (r >= 0) kbase = b;
  }
  if (r >= 0) {
    if (off >= VRAM_SIZE || n > VRAM_SIZE - off) r = -1;
    else r = ksceKernelCopyToUser(udst, (const void *)(kbase + off), n);
  }
  EXIT_SYSCALL(state);
  return r;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp) { return SCE_KERNEL_START_SUCCESS; }
int module_stop(SceSize args, void *argp) { return SCE_KERNEL_STOP_SUCCESS; }
