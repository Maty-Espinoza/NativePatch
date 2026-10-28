// Prueba de la memoria de sublistas (v12.12) fuera de la Vita: listas sinteticas en una "RAM del PSP" falsa, bajo qemu.
// arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I<vita-headers>/include -o prueba prueba_memoria.c
// qemu-arm-static ./prueba 0   (0 sin memoria, 1 con memoria, 2 revisando todas): las columnas antes de "|" deben ser iguales.
#include "captura.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
SceUInt32 sceKernelGetProcessTimeLow(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000 + t.tv_nsec / 1000; }
void *sceClibMemset(void *d, int c, SceSize n) { return memset(d, c, n); }
void *sceClibMemcpy(void *d, const void *s, SceSize n) { return memcpy(d, s, n); }
int sceClibMemcmp(const void *a, const void *b, SceSize n) { return memcmp(a, b, n); }
int sceClibVsnprintf(char *b, SceSize n, const char *f, va_list ap) { return vsnprintf(b, n, f, ap); }
int sceClibSnprintf(char *b, SceSize n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
SceUID sceIoOpen(const char *p, int f, SceMode m) { return -1; }
int sceIoWrite(SceUID fd, const void *b, SceSize n) { return -1; }
int sceIoClose(SceUID fd) { return 0; }
int sceIoRemove(const char *p) { return 0; }
int sceIoRename(const char *a, const char *b) { return 0; }
int sceIoGetstat(const char *p, SceIoStat *s) { return -1; }
SceUID sceKernelCreateThread(const char *n, SceKernelThreadEntry e, int p, SceSize s, SceUInt a, int c, const SceKernelThreadOptParam *o) { return -1; }
int sceKernelStartThread(SceUID t, SceSize a, void *p) { return 0; }
int sceKernelDelayThread(SceUInt d) { return 0; }
int sceKernelGetThreadInfo(SceUID t, SceKernelThreadInfo *i) { return -1; }
SceUID sceKernelAllocMemBlock(const char *n, SceKernelMemBlockType t, SceSize s, SceKernelAllocMemBlockOpt *o) { return -1; }
int sceKernelGetMemBlockBase(SceUID u, void **b) { return -1; }
int sceKernelGetMemBlockInfoByAddr(void *a, SceKernelMemBlockInfo *i) { return -1; }
SceUID emu_th[EMU_N]; int emu_aff[EMU_N], emu_n;

static uint32_t *W(uint32_t a) { return (uint32_t *)PSP(a); }
static uint32_t pc_;
static void E(uint32_t c, uint32_t d) { *W(pc_) = c << 24 | (d & 0xFFFFFF); pc_ += 4; }
#define MAIN 0x08800000u
#define SUBS 0x08900000u
#define SHARED 0x08A00000u
static void build(int frame) {
  // sublista compartida (anidada): fija color/alpha y dibuja 1 tira con vertices propios
  pc_ = SHARED; E(0x10, 0x080000); E(0x01, 0xB00000); E(0x22, 0x123); E(0x04, 4 << 16 | 8); E(0x0B, 0);
  for (int k = 0; k < 40; k++) {
    pc_ = SUBS + k * 0x200;
    if (k % 3) E(0x01, 0xC00000 + k * 0x100);  // las k%3==0 dibujan con los vertices de entrada (VDEP)
    E(0xC8, k & 3); E(0x3A, 0); for (int i = 0; i < 12; i++) E(0x3B, (k * 7 + i) & 0xFFFFFF);
    E(0x04, 4 << 16 | (6 + k));
    if (k % 5 == 0) E(0x0A, SHARED & 0xFFFFFF);  // con BASE 0x08 de la lista de arriba
    if (k % 7 == 0) { E(0x10, 0x090000); }      // cambia BASE (se filtra a la salida)
    E(0x04, 3 << 16 | 3);
    if (k == 10 && frame >= 3) E(0xC8, 2);       // esta cambia desde el frame 3
    E(0x0B, 0);
  }
  pc_ = MAIN;
  E(0x12, 0x00011F); E(0x1E, 1); E(0xD3, 0); E(0x9C, 0x000000); E(0x9D, 0x040000 | 512);
  for (int k = 0; k < 40; k++) {
    E(0x10, 0x080000);
    E(0xA0, 0x100000 + (k & 1 ? frame * 0x1000 : 0)); E(0xA8, 0x090000);  // textura impar cambia cada frame (dependencia)
    E(0x01, 0xD00000 + k * 0x40 + (k == 12 && frame >= 5 ? 0x1000 : 0));
    E(0x0A, (SUBS + k * 0x200) & 0xFFFFFF);
    if (k == 20) E(0x0A, (SUBS + k * 0x200) & 0xFFFFFF);  // la misma dos veces seguidas (otra entrada: vertices)
  }
  E(0x0F, 0); E(0x0C, 0);
}
static uint32_t stateSum(void) { uint32_t h = 0; for (int i = 0; i < 256; i++) h = h * 31 + reg[i]; for (int m = 0; m < 5; m++) for (int i = 0; i < 96; i++) h = h * 31 + mtx[m][i]; return h * 31 + vaddr * 7 + iaddr; }
int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0); int mode = atoi(argv[1]);  // 0 sin memoria, 1 con memoria, 2 revisando todas
  if (mmap((void *)0x70000000, 0x2000000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) { perror("mmap"); return 1; }
  if (mmap((void *)0x72000000, 0x100000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) { perror("mmap2"); return 1; }
  rlo[0] = 0x08000000; rhi[0] = 0x0A000000; nr = 1; initCls();
  if (mode) { memo = calloc(MEMO_N, sizeof(Memo)); pool = calloc(POOL_N, 64); memo_on = 1; memo_chk = mode == 2; }
  for (int f = 0; f < 8; f++) {
    build(f);
    sceClibMemset(&acc, 0, sizeof acc);
    List *l = &q[qt++ & 63]; memset(l, 0, sizeof *l); l->pc = MAIN; l->ord = f + 1;
    walk();
    printf("f%d cmds=%u draws=%u verts=%u vbytes=%u tex=%u prim3=%u prim4=%u calls=%u estado=%08x | hit=%u miss=%u dep=%u fail=%u no=%u chk=%u BAD=%u lines=%u saltados=%u\n",
           f, acc.cmds, acc.draws, acc.verts, acc.vbytes, acc.textures, acc.prim[3], acc.prim[4], acc.calls_ram, stateSum(),
           acc.memo_hit, acc.memo_miss, acc.memo_dep, acc.memo_fail, acc.memo_no, acc.memo_chk, acc.memo_bad, acc.memo_lines, acc.memo_cmds);
    gen++; ntex = 0;
  }
  lbuf[lpos] = 0; printf("%s", lbuf);
  return 0;
}
