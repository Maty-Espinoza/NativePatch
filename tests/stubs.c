#define _GNU_SOURCE
// Funciones falsas del sistema de la PSP para correr el plugin en el PC.
// La memoria de la PSP se mapea en sus direcciones reales (ver mapPspMemory).
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "psp_stub.h"

// --- Log: lo que el plugin escribe a ms0:/ge_patch.log queda aqui para revisarlo
char test_log[1 << 20];
int test_log_len;

int pspSdkSetK1(int k1) { return 0; }
unsigned int sceKernelCpuSuspendIntr(void) { return 0; }
int sceKernelGetThreadId(void) { return 1; }
void sceKernelCpuResumeIntr(unsigned int f) {}
// Los volcados (.gpd) van a un archivo del PC: tests/volcado_test.gpd (para probar tools/gpd.py)
static FILE *dump_fp;
SceUID sceIoOpen(const char *file, int flags, int mode) {
  if (strstr(file, ".gpd")) {
    dump_fp = fopen(getenv("GPD_OUT") ? getenv("GPD_OUT") : "volcado_test.gpd", "wb");
    return dump_fp ? 4 : -1;
  }
  return strstr(file, "ge_patch") ? 3 : -1;
}
int sceIoClose(SceUID fd) { if (fd == 4 && dump_fp) { fclose(dump_fp); dump_fp = NULL; } return 0; }
int sceIoRead(SceUID fd, void *d, SceSize s) { return -1; }
int sceIoWrite(SceUID fd, const void *d, SceSize s) {
  if (fd == 4)
    return dump_fp ? (int)fwrite(d, 1, s, dump_fp) : -1;
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

// VRAM real (0x04000000, 4 MB) y RAM (0x08000000-0x0B000000: juego, VRAM falsa,
// buffer de pantalla, memorias del plugin, sus listas e imagen del menu). Como en la PSP,
// la misma memoria se ve sin cache en +0x40000000 (el plugin lee/escribe por ahi).
void mapPspMemory(void) {
  // + la pagina de la bandera DRAW_NATIVE de Adrenaline (0xABCDEF00)
  struct { unsigned long addr, size; int alias; } r[] = { { 0x04000000, 0x400000, 1 }, { 0x08000000, 0x3000000, 1 }, { 0xABCDE000, 0x2000, 0 } };
  int i, k;
  for (i = 0; i < 3; i++) {
    int fd = memfd_create("psp", 0);
    if (fd < 0 || ftruncate(fd, r[i].size) < 0) { perror("memfd"); exit(2); }
    for (k = 0; k <= r[i].alias; k++) {
      unsigned long a = r[i].addr + k * 0x40000000;
      void *p = mmap((void *)a, r[i].size, PROT_READ | PROT_WRITE,
                     MAP_SHARED | (getenv("TEST_MAP_FIXED") ? MAP_FIXED : MAP_FIXED_NOREPLACE), fd, 0);
      if (p != (void *)a) {
        fprintf(stderr, "no se pudo mapear %08lx (compilar con -m32 -pie)\n", a);
        exit(2);
      }
    }
  }
}
