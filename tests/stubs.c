// Funciones falsas del sistema de la PSP para correr el plugin en el PC.
// La memoria de la PSP se mapea en sus direcciones reales (ver mapPspMemory).
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "psp_stub.h"

// --- Log: lo que el plugin escribe a ms0:/ge_patch.log queda aqui para revisarlo
char test_log[1 << 20];
int test_log_len;

int pspSdkSetK1(int k1) { return 0; }
unsigned int sceKernelCpuSuspendIntr(void) { return 0; }
void sceKernelCpuResumeIntr(unsigned int f) {}
SceUID sceIoOpen(const char *file, int flags, int mode) { return strstr(file, "ge_patch") ? 3 : -1; }
int sceIoClose(SceUID fd) { return 0; }
int sceIoRead(SceUID fd, void *d, SceSize s) { return -1; }
int sceIoWrite(SceUID fd, const void *d, SceSize s) {
  if (test_log_len + s < sizeof(test_log)) {
    memcpy(test_log + test_log_len, d, s);
    test_log_len += s;
    test_log[test_log_len] = 0;
  }
  return s;
}
long long sceIoLseek(SceUID fd, long long o, int w) { return 0; }
int sceIoRemove(const char *f) { return 0; }
int sceIoRename(const char *a, const char *b) { return 0; }
char *sceKernelInitFileName(void) { return "test"; }
SceUID sceKernelCreateThread(const char *n, int (*e)(SceSize, void *), int p, int s, unsigned int a, void *o) { return -1; }
int sceKernelStartThread(SceUID t, SceSize l, void *a) { return 0; }
int sceKernelDelayThread(SceUID d) { return 0; }
unsigned int test_time;
unsigned int sceKernelGetSystemTimeLow(void) { return test_time; }
void sceKernelDcacheWritebackInvalidateAll(void) {}
void sceKernelDcacheWritebackInvalidateRange(const void *p, unsigned int s) {}
void sceKernelDcacheWritebackRange(const void *p, unsigned int s) {}
void sceKernelIcacheClearAll(void) {}
unsigned int test_buttons;
int sceCtrlPeekBufferPositive(SceCtrlData *pad, int c) { memset(pad, 0, sizeof(*pad)); pad->Buttons = test_buttons; return 1; }
unsigned int FindProc(const char *m, const char *l, unsigned int nid) { return 0; }
void sctrlHENPatchSyscall(unsigned int a, void *n) {}
int sceGuFinish(void) { return 0; }
void sceGuCopyImage(int psm, int sx, int sy, int w, int h, int sw, void *s, int dx, int dy, int dw, void *d) {}
void guStart(int cid, void *list) {}

// VRAM real (0x04000000, 4 MB) y RAM (0x08000000-0x0A900000: juego, VRAM falsa,
// buffer de pantalla, memoria de parches y lista de copia del plugin)
void mapPspMemory(void) {
  struct { unsigned long addr, size; } r[] = { { 0x04000000, 0x400000 }, { 0x08000000, 0x2900000 } };
  int i;
  for (i = 0; i < 2; i++) {
    void *p = mmap((void *)r[i].addr, r[i].size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void *)r[i].addr) {
      fprintf(stderr, "no se pudo mapear %08lx (compilar con -m32 -pie)\n", r[i].addr);
      exit(2);
    }
  }
}
