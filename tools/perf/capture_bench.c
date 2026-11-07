// capture_bench.c: performance bench of the capturer and the frame record (nativedraw/capture.c, which includes
// record.c) outside the Vita, on ARM under qemu. Not a test: tools/test_ci.sh keeps the capturer correct; this measures
// how much work a change adds or removes before it reaches the Vita.
// qemu does not reproduce the Vita's memory (PSP RAM is uncached from the ARM and that wait is the capturer's main cost), so
// each scene reports two things:
// - counters that do not depend on the machine: 256-byte reads of PSP RAM, shared-cache hits, bytes copied or compared by the
//   CPU from PSP RAM, DMA calls and bytes, clock reads. They are converted to an estimate of Vita microseconds with the
//   unit costs measured on the Vita (docs/dev/en/findings.md): "mem_us" (deterministic: equal runs give equal numbers);
// - the CPU time of the capturer under qemu: "qemu_us" per frame and "qemu_min_us", the fastest frame after the first
//   (interpretation, record bookkeeping, hashes and sorting). It is not Vita time, only comparable between two runs on
//   the same PC (tools/perf/bench.sh takes the best of several).
// Scenes are synthetic lists shaped like the games measured on the Vita (commands per draw, vertices per frame, how the
// state changes, textures in RAM or VRAM, sublists). Each scene runs in a child process (the capturer's state is global).
// Build and run: tools/perf/bench.sh. Usage: capture_bench [scene] (no argument: all scenes, one line each).
#define NP_DEV
#define NP_BENCH
#include "../../nativedraw/capture.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __arm__
#define CALLGRIND_TOGGLE_COLLECT
#else
#include <valgrind/callgrind.h>  // (tools/perf/instr.sh counts only the capturer's part of each frame)
#endif

// --- Counters of what the Vita pays for (filled by the stubs below) ---
static struct { uint64_t ucp, ucmp, ucw, dma_b; uint32_t dma_n, clock; } M;
#define RAM_LO 0x70000000u  // the fake PSP RAM (mmap at the Vita addresses) and its zone and mirrors
static int inRam(const void *p) { uint32_t a = (uint32_t)p; return a - RAM_LO < 0x2000000u || a - 0x72000000u < 0x400000u || a - 0x72C00000u < 0x200000u; }

// --- Vita functions replaced (same as nativedraw/capture_test.c, with counters) ---
SceUInt32 sceKernelGetProcessTimeLow(void) { struct timespec t; M.clock++; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000 + t.tv_nsec / 1000; }
void *sceClibMemset(void *d, int c, SceSize n) { return memset(d, c, n); }
void *sceClibMemcpy(void *d, const void *s, SceSize n) { if (inRam(s)) M.ucp += n; if (inRam(d)) M.ucw += n; return memcpy(d, s, n); }
void *sceClibMemmove(void *d, const void *s, SceSize n) { if (inRam(s)) M.ucp += n; if (inRam(d)) M.ucw += n; return memmove(d, s, n); }
int sceClibMemcmp(const void *a, const void *b, SceSize n) { if (inRam(a) || inRam(b)) M.ucmp += n; return memcmp(a, b, n); }
int sceClibVsnprintf(char *b, SceSize n, const char *f, va_list ap) { return vsnprintf(b, n, f, ap); }
int sceClibSnprintf(char *b, SceSize n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
SceUID sceIoOpen(const char *p, int f, SceMode m) { return -1; }
int sceIoWrite(SceUID fd, const void *b, SceSize n) { return -1; }
int sceIoClose(SceUID fd) { return 0; }
int sceIoRemove(const char *p) { return 0; }
int sceIoRename(const char *a, const char *b) { return 0; }
void alogNew(void) {}
void alog(const char *fmt, ...) {}
int sceIoGetstat(const char *p, SceIoStat *s) { return -1; }
SceUID sceKernelCreateThread(const char *n, SceKernelThreadEntry e, int p, SceSize s, SceUInt a, int c, const SceKernelThreadOptParam *o) { return -1; }
int sceKernelStartThread(SceUID t, SceSize a, void *p) { return 0; }
int sceKernelDelayThread(SceUInt d) { return 0; }
int sceKernelGetThreadInfo(SceUID t, SceKernelThreadInfo *i) { return -1; }
void *sceDmacMemcpy(void *d, const void *s, SceSize n) { M.dma_n++; M.dma_b += n; return memcpy(d, s, n); }
int sceGxmMapMemory(void *b, SceSize n, int a) { return 0; }
static void *blk[16]; static int nblk;
int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *i) { return -1; }
SceUID sceKernelAllocMemBlock(const char *n, SceKernelMemBlockType t, SceSize s, SceKernelAllocMemBlockOpt *o) { return nblk < 16 && (blk[nblk] = calloc(1, s)) ? nblk++ : -1; }
int sceKernelGetMemBlockBase(SceUID u, void **b) { if (u < 0 || u >= nblk) return -1; *b = blk[u]; return 0; }
int sceKernelFreeMemBlock(SceUID u) { return 0; }
int sceKernelGetMemBlockInfoByAddr(void *a, SceKernelMemBlockInfo *i) { return -1; }
SceUID emu_th[EMU_N]; int emu_aff[EMU_N], emu_n;

// --- Unit costs measured on the Vita, in ns (findings.md: "memory bench" line of the capture log, stage 2 reads) ---
// C_UCW_B: a CPU copy writing into PSP RAM, on top of its read; C_XW_B: a byte of xferMirror's copy word by word (read and write)
enum { C_READ256 = 2800, C_HIT = 300, C_UCP_B = 11, C_UCW_B = 3, C_XW_B = 51, C_DMA_CALL = 100000, C_DMA_B = 2, C_CLOCK = 405 };

// --- List generator ---
static uint32_t *W(uint32_t a) { return (uint32_t *)PSP(isVram(a) ? MIRROR + (a & 0x1FFFFF) : a); }
static uint32_t pc_;
static void E(uint32_t c, uint32_t d) {  // command c with data d at pc_ (a VRAM list goes to both mirrors)
  *W(pc_) = c << 24 | (d & 0xFFFFFF);
  if (isVram(pc_)) *(uint32_t *)PSP(MIRROR2 + (pc_ & 0x1FFFFF)) = c << 24 | (d & 0xFFFFFF);
  pc_ += 4;
}
// Deterministic pseudo-random numbers: rnd for addresses (restarted on every frame so the lists repeat), rnd2 for data
static uint32_t rnd_s = 1, rnd2_s = 7;
static uint32_t rnd(uint32_t n) { rnd_s = rnd_s * 1664525u + 1013904223u; return (rnd_s >> 8) % n; }
static uint32_t rnd2(uint32_t n) { rnd2_s = rnd2_s * 1664525u + 1013904223u; return (rnd2_s >> 8) % n; }
#define MAIN 0x08800000u
#define VB 0x08A00000u    // vertices (4 MB)
#define TEX 0x08900000u   // RAM textures and palettes
#define SUBS 0x08E00000u  // sublists
static void vtype(uint32_t vt) { E(0x12, vt); }
// Puts n vertices of vs bytes at a (pseudo-random bytes, uv first: v in [0, rows) for 2D draws)
static void verts(uint32_t a, uint32_t n, uint32_t vs, int thru, uint32_t rows) {
  uint8_t *p = (uint8_t *)W(a);
  for (uint32_t i = 0; i < n * vs; i++) p[i] = rnd2(256);
  if (thru) for (uint32_t i = 0; i < n; i++) { uint16_t *v = (uint16_t *)(p + i * vs); v[0] = rnd2(256); v[1] = rnd2(rows); }
}
// Texture state: address, width w (tbw and log2), height log2, format fmt, swizzle
static void texState(uint32_t a, uint32_t lw, uint32_t lh, uint32_t fmt, int swz) {
  E(0xA0, a & 0xFFFFFF); E(0xA8, (a >> 8 & 0xFF0000) | 1u << lw); E(0xB8, lh << 8 | lw); E(0xC3, fmt); E(0xC2, swz);
}
// Common start of a frame's list: vertex type, texture on, render target, scissor
static void head(uint32_t vt) {
  vtype(vt); E(0x1E, 1); E(0xD3, 0); E(0x9C, 0x000000); E(0x9D, 0x040000 | 512); E(0x9E, 0x088000); E(0x9F, 0x040000 | 512);
  E(0xD4, 0); E(0xD5, 479 | 271 << 10); E(0x16, 0); E(0x21, 0); E(0x22, 0); E(0x23, 1); E(0x15, 0);
}

// Scene 1: 3D race with a crowd, like Ridge Racer's heaviest second (22.9k commands, 3.5k strip draws, 23.6k vertices of
// 20 bytes scattered in RAM, 92 % of draws with the same state, a few VRAM textures, ~230 "other" changes). vt 0x15E:
// uv and position int16, color 8888, normal int16 (20 bytes).
static void crowd(int f, int draws) {
  uint32_t vs = 20, a = VB, nv = 7;
  rnd_s = 1; pc_ = MAIN; head(0x15E);
  E(0x3A, 0); for (int i = 0; i < 12; i++) E(0x3B, 0x3F8000 + i);  // world
  E(0x3E, 0); for (int i = 0; i < 16; i++) E(0x3F, 0x3F8000 + i);  // projection
  texState(0x04100000, 6, 6, 5, 1);  // 64x64 T8 in VRAM
  for (int d = 0; d < draws; d++) {
    // vertices: clusters of ~8 draws close together, far apart from each other (as measured: few DMA-sized runs)
    if (d % 8 == 0) a = VB + (uint32_t)rnd(3000) * 1024u;
    if (!f) verts(a, nv, vs, 0, 0);
    E(0x01, a & 0xFFFFFF); E(0x10, a >> 8 & 0x0F0000);
    if (d % 28 == 0) texState(0x04100000 + (d / 28 % 12) * 0x1000, 6, 6, 5, 1);  // 12 VRAM textures of 4 KB
    if (d % 15 == 0) { E(0xD5, (479 - d % 7) | 271 << 10); E(0xD4, d % 5); E(0xDB, d % 3); }  // "other" changes
    if (d % 34 == 0) { E(0xB0, 0x130000 + (d / 34 % 4) * 0x400); E(0xB1, 0x040000); E(0xC4, 8); }  // palette in VRAM, ~100 per frame
    if (d % 45 == 0) { E(0x12, 0x80011E); E(0x01, (a + 4096) & 0xFFFFFF); E(0x04, 6 << 16 | 2); }  // a 2D rectangle, ~75 per frame
    if (d % 50 == 0) { E(0xC7, 0x101); E(0x47, d % 2); E(0x21, 0); }
    if (d % 100 == 0) { E(0x3A, 0); for (int i = 0; i < 12; i++) E(0x3B, 0x3F8000 + i + f); }  // a moving object
    E(0x12, 0x15E); E(0x1E, 1);  // repeated (same value: the cheap path, as the game does)
    E(0x04, (d % 110 == 0 ? 5 : 4) << 16 | nv);  // strip; one fan in 110
    a += nv * vs + (d % 8 == 7 ? 0 : rnd(4) * 64);
  }
  E(0x0F, 0); E(0x0C, 0);
}
// Scene 2: 2D sprites in through mode, like a NeoGeo or Metal Slug frame: rects with RAM swizzled T8 textures (16 KB,
// cached between frames), palettes loaded every few draws, two textures rewritten per frame. vt 0x80011E (16 bytes).
static void sprites(int f, int draws) {
  uint32_t vs = 16, a = VB;
  if (!f) { uint8_t *p = (uint8_t *)W(TEX); for (uint32_t i = 0; i < 24 * 16384 + 4096; i++) p[i] = rnd2(256); }
  else for (int k = 0; k < 2; k++) { uint8_t *p = (uint8_t *)W(TEX + ((f + k) % 24) * 16384); for (int i = 0; i < 16384; i += 64) p[i] = rnd2(256); }
  rnd_s = 1; pc_ = MAIN; head(0x80011E);
  E(0xB0, (TEX + 24 * 16384) & 0xFFFFFF); E(0xB1, 0x080000); E(0xC5, 0x00FF00);
  for (int d = 0; d < draws; d++) {
    if (!f) verts(a, 2, vs, 1, 64);
    E(0x01, a & 0xFFFFFF); E(0x10, a >> 8 & 0x0F0000);
    if (d % 4 == 0) texState(TEX + (d / 4 % 24) * 16384, 8, 6, 5, 1);  // 256x64 T8 swizzled, 16 KB
    if (d % 4 == 0) E(0xC4, 32);  // palette 8888, 1 KB
    if (d % 9 == 0) { E(0x21, d % 2); E(0x22, 0); E(0x5C, 0xFF0000 | d); }
    E(0x04, 6 << 16 | 2);
    a += 2 * vs;
  }
  E(0x0F, 0); E(0x0C, 0);
}
// Scene 3: sublists repeated between frames, like Gran Turismo (the sublist memory applies most of them; no record, as
// today when a frame exceeds the record). From nativedraw/capture_test.c: 40 sublists, one shared, one changes per frame.
static void sublists(int f, int n) {
  pc_ = SUBS; E(0x10, 0x080000); E(0x01, 0xB00000); E(0x22, 0x123); E(0x04, 4 << 16 | 8); E(0x0B, 0);
  for (int k = 0; k < n; k++) {
    pc_ = SUBS + 0x1000 + k * 0x400;
    if (k % 3) E(0x01, 0xC00000 + k * 0x100);
    E(0xC8, k & 3); E(0x3A, 0); for (int i = 0; i < 12; i++) E(0x3B, (k * 7 + i) & 0xFFFFFF);
    for (int j = 0; j < 20; j++) { E(0x01, 0xC00000 + k * 0x100 + j * 0x40); E(0x04, 4 << 16 | (6 + j)); }
    if (k % 5 == 0) { E(0x10, 0x080000); E(0x0A, SUBS & 0xFFFFFF); }
    if (k % 7 == 0) E(0x10, 0x090000);
    if (k == 10 + f % 5) E(0xC8, f);  // one sublist changes each frame
    E(0x0B, 0);
  }
  pc_ = MAIN; head(0x00011F);
  for (int k = 0; k < n; k++) {
    E(0x10, SUBS >> 8 & 0x0F0000);
    E(0xA0, 0x100000 + (k & 1 ? f * 0x1000 : 0)); E(0xA8, 0x090000);
    E(0x01, 0xD00000 + k * 0x40);
    E(0x0A, (SUBS + 0x1000 + k * 0x400) & 0xFFFFFF);
  }
  E(0x0F, 0); E(0x0C, 0);
}

// Scene 4: each frame draws to another image and reads a page of it as a texture, as a game with moving images would: the pages
// asked for must not pile up over the frames (want= in the output).
static void images(int f, int n) {
  uint32_t fb = 0x100000 + (f % n) * 0x4000, a = VB;
  rnd_s = 1; pc_ = MAIN; head(0x15E);
  E(0x9C, fb & 0xFFFFFF); E(0x9D, (fb >> 8 & 0xFF0000) | 64); E(0xD5, 63 | 63 << 10);
  if (!f) verts(a, 7, 20, 0, 0);
  E(0x01, a & 0xFFFFFF); E(0x10, a >> 8 & 0x0F0000); E(0x1E, 0); E(0x04, 4 << 16 | 7);
  E(0x9C, 0); E(0x9D, 0x040000 | 512); E(0xD5, 479 | 271 << 10); E(0x1E, 1);
  texState(0x04000000 + fb + 0x1000, 6, 6, 3, 0);
  E(0x04, 4 << 16 | 7);
  E(0x0F, 0); E(0x0C, 0);
}

// Scene 5: RAM to VRAM transfers (TRANSFER, 0xEA) like God of War Ghost of Sparta: the 59 transfers of a menu frame as the
// Vita logged them ("vram copy" lines, frame 1452 of the 0.0.25.25 run), rows of 128 pixels of 4 bytes, source and
// destination of the same width, many into the same VRAM slot; after each one a 3D draw takes its vertices from the
// destination, as the game's pieces do (the record copies them from the mirror). n = 97 is the play frame of that run (97
// transfers, 1.35 MB): its shapes were not logged, so it repeats the menu list with copies 1.38 times taller.
static const uint32_t XF[59][3] = {  // source, destination, rows of 512 bytes
  { 0x098ac400, 0x041ec000, 1 }, { 0x095a7b50, 0x0418a000, 129 }, { 0x08559c40, 0x0419a200, 1 }, { 0x0989f5c0, 0x0419a400, 2 },
  { 0x0989fa60, 0x0419a800, 4 }, { 0x0957aa80, 0x0418a000, 34 }, { 0x098b73d0, 0x0418e400, 2 }, { 0x09571090, 0x0418a000, 11 },
  { 0x098b6db0, 0x0418b600, 1 }, { 0x0955f720, 0x0418a000, 135 }, { 0x098b6700, 0x0419ae00, 4 }, { 0x09556c70, 0x0418a000, 23 },
  { 0x098b6250, 0x0418ce00, 1 }, { 0x0986d3c0, 0x0418a000, 301 }, { 0x08559cc0, 0x041afa00, 1 }, { 0x098a2b90, 0x041afc00, 3 },
  { 0x098a3020, 0x041b0200, 2 }, { 0x0955a0b0, 0x0418a000, 40 }, { 0x098b6440, 0x0418f000, 1 }, { 0x098b6630, 0x0418f200, 1 },
  { 0x08557940, 0x0418a000, 16 }, { 0x0989a2c0, 0x0418c000, 35 }, { 0x0989e850, 0x04190600, 3 }, { 0x094ceef0, 0x0418a000, 9 },
  { 0x098b5d20, 0x0418b200, 1 }, { 0x098b5d60, 0x0418b400, 1 }, { 0x094cc3e0, 0x0418a000, 19 }, { 0x098b5bc0, 0x0418c600, 1 },
  { 0x098b5c50, 0x0418c800, 1 }, { 0x095534d0, 0x0418a000, 10 }, { 0x098b6040, 0x0418b400, 1 }, { 0x098b60a0, 0x0418b600, 1 },
  { 0x094c7ff0, 0x0418a000, 31 }, { 0x098b5980, 0x0418de00, 1 }, { 0x098b5af0, 0x0418e000, 1 }, { 0x094c6920, 0x0418a000, 9 },
  { 0x098b5880, 0x0418b200, 1 }, { 0x098b58d0, 0x0418b400, 1 }, { 0x094d05b0, 0x0418a000, 8 }, { 0x098b5e30, 0x0418b000, 1 },
  { 0x098b5e70, 0x0418b200, 1 }, { 0x094d1a70, 0x0418a000, 6 }, { 0x098b5f40, 0x0418ac00, 1 }, { 0x098b5f70, 0x0418ae00, 1 },
  { 0x097758c0, 0x0418a000, 198 }, { 0x085597c0, 0x041a2c00, 3 }, { 0x098a1420, 0x041a3200, 7 }, { 0x098a2130, 0x041a4000, 3 },
  { 0x09572c00, 0x0418a000, 2 }, { 0x098b6ed0, 0x0418a400, 1 }, { 0x098b6f00, 0x0418a600, 1 }, { 0x095734b0, 0x0418a000, 34 },
  { 0x098b6fc0, 0x0418e400, 1 }, { 0x098b7190, 0x0418e600, 1 }, { 0x09554db0, 0x0418a000, 13 }, { 0x098b6140, 0x0418ba00, 1 },
  { 0x098b6180, 0x0418bc00, 1 }, { 0x085ef050, 0x0418a000, 8 }, { 0x0868f060, 0x0418b000, 4 }
};
static void xfers(int f, int n) {
  rnd_s = 1; pc_ = MAIN; head(0x15E); E(0x1E, 0);
  for (int i = 0; i < n; i++) {
    uint32_t s = XF[i % 59][0], d = XF[i % 59][1], h = n == 59 ? XF[i][2] : (XF[i % 59][2] * 138 + 50) / 100;
    if (h > 1024) h = 1024;
    if (!f) { uint8_t *p = (uint8_t *)W(s); for (uint32_t k = 0; k < h * 512; k++) p[k] = rnd2(256); want(d, h * 512, 1); }  // (a game already running: its pages are in the mirror)
    E(0xB2, s & 0xFFFFF0); E(0xB3, (s >> 8 & 0x0F0000) | 128); E(0xB4, d & 0xFFFFF0); E(0xB5, (d >> 8 & 0x0F0000) | 128);
    E(0xEB, 0); E(0xEC, 0); E(0xEE, 127 | (h - 1) << 10); E(0xEA, 1);
    E(0x10, d >> 8 & 0x0F0000); E(0x01, d & 0xFFFFFF); E(0x04, 4 << 16 | h * 512 / 20);
  }
  E(0x0F, 0); E(0x0C, 0);
}
// Scene 6: a God of War Ghost of Sparta combat frame (capture log of the 0.0.25.26 run, combat seconds 320-322: ~115
// transfers of 1.5 MB, ~7,000 3D strips of ~9 vertices that read the destination of the transfer before them piece by piece,
// RAM textures changing every ~47 draws, 30 of them swizzled under 8 KB, copied to the record every frame, the others in the
// texture cache). The transfers of gow_play_xfer; vt 0x15E (20 bytes).
static void combat(int f, int n) {
  rnd_s = 1; pc_ = MAIN; head(0x15E);
  if (!f) { uint8_t *p = (uint8_t *)W(TEX); for (uint32_t k = 0; k < 30 * 4096 + 35 * 16384; k++) p[k] = rnd2(256); }
  for (int i = 0, d = 0; i < n; i++) {
    uint32_t s = XF[i % 59][0], t = XF[i % 59][1], h = (XF[i % 59][2] * 138 + 50) / 100;
    if (!f) { uint8_t *p = (uint8_t *)W(s); for (uint32_t k = 0; k < h * 512; k++) p[k] = rnd2(256); want(t, h * 512, 1); }
    E(0xB2, s & 0xFFFFF0); E(0xB3, (s >> 8 & 0x0F0000) | 128); E(0xB4, t & 0xFFFFF0); E(0xB5, (t >> 8 & 0x0F0000) | 128);
    E(0xEB, 0); E(0xEC, 0); E(0xEE, 127 | (h - 1) << 10); E(0xEA, 1);
    E(0x10, t >> 8 & 0x0F0000);
    for (uint32_t o = 0; o + 180 <= h * 512; o += 180, d++) {
      if (d % 47 == 0) { uint32_t k = d / 47 % 65; if (k < 30) texState(TEX + k * 4096, 6, 6, 5, 1); else texState(TEX + 30 * 4096 + (k - 30) * 16384, 8, 6, 5, 1); }
      E(0x01, (t + o) & 0xFFFFFF); E(0x04, 4 << 16 | 9);
    }
  }
  E(0x0F, 0); E(0x0C, 0);
}
// Scene 7: the same combat frame on the GPU path, as the capture log of the 0.0.28.1 run shows it (combat seconds 190-328, per
// frame: ~7,600 3D strips of ~9 vertices read from the transfer copy, ~200 bytes each, 92 % for the GPU; light block on 93 % of
// the draws; vertex type changes ~500, and ~100-150 each of 0x48, 0x49, 0x21, 0xC9, 0x24, 0xE7, 0x1F, 0x4B; matrices on ~9 %
// of the draws, bones on ~7 %). The transfers and textures of gow_combat, with strips of 7 vertices so that their 1.3 MB give the
// game's draws per frame; the sources hold floats, so every position is valid.
// Groups of 16 draws share a vertex type, from the mix of the dumped combat frame (TR_STATS of tools/perf/data/gow_combat):
// 0x1BE and 0x7BE lit with no light on, 0x87B9 (3 weights) half with three directional lights, 0x19F, and 0x780, which stays
// off the GPU (copied as is). Lighting is on throughout; a material color changes every 400 draws. Same-value commands (BASE, texture
// and shading on) bring it to the game's ~7.5 commands per draw; which ones the game repeats was not logged.
static void gpuCombat(int f, int n) {
  static const uint32_t vts[20] = { 0x1BE, 0x1BE, 0x1BE, 0x1BE, 0x7BE, 0x87B9, 0x1BE, 0x7BE, 0x87B9, 0x19F,
                                    0x1BE, 0x1BE, 0x1BE, 0x1BE, 0x7BE, 0x87B9, 0x1BE, 0x7BE, 0x87B9, 0x780 };
  rnd_s = 1; pc_ = MAIN; head(0x1BE);
  E(0x50, 1); E(0x17, 1); E(0x18, 0); E(0x19, 0); E(0x1A, 0); E(0x5F, 0); E(0x60, 0); E(0x61, 0);
  E(0x3A, 0); for (int i = 0; i < 12; i++) E(0x3B, 0x3F8000 + i);
  E(0x3C, 0); for (int i = 0; i < 12; i++) E(0x3D, 0x3F8000 + i);
  E(0x3E, 0); for (int i = 0; i < 16; i++) E(0x3F, 0x3F8000 + i);
  if (!f) { uint8_t *p = (uint8_t *)W(TEX); for (uint32_t k = 0; k < 30 * 4096 + 35 * 16384; k++) p[k] = rnd2(256); }
  for (int i = 0, d = 0; i < n; i++) {
    uint32_t s = XF[i % 59][0], t = XF[i % 59][1], h = (XF[i % 59][2] * 138 + 50) / 100;
    if (!f) { uint32_t *p = W(s); for (uint32_t k = 0; k < h * 128; k++) p[k] = 0x3F000000 | rnd2(1 << 24); want(t, h * 512, 1); }
    E(0xB2, s & 0xFFFFF0); E(0xB3, (s >> 8 & 0x0F0000) | 128); E(0xB4, t & 0xFFFFF0); E(0xB5, (t >> 8 & 0x0F0000) | 128);
    E(0xEB, 0); E(0xEC, 0); E(0xEE, 127 | (h - 1) << 10); E(0xEA, 1);
    for (uint32_t o = 0;; d++) {
      uint32_t g = d / 16, k = g % 20, vt = vts[k], vs = vsize(vt), nb = vt >> 9 & 3 ? (vt >> 14 & 7) + 1 : 0;
      if (o + 7 * vs > h * 512) break;
      if (d % 16 == 0) {
        vtype(vt);
        if (k % 10 == 5 || k % 10 == 6) for (int j = 0; j < 3; j++) E(0x18 + j, k % 10 == 5);  // (lights on for one 0x87B9 group)
        if (g % 3 == 0) { E(0x48, 0x3F8000 + g % 2); E(0x49, 0x3F8000 + g % 2); E(0x21, g % 2); E(0xC9, g % 2); E(0x24, 0); E(0xE7, g % 2); E(0x1F, g % 2); E(0x4B, g % 2); }
      }
      if (d % 47 == 0) { uint32_t x = d / 47 % 65; if (x < 30) texState(TEX + x * 4096, 6, 6, 5, 1); else texState(TEX + 30 * 4096 + (x - 30) * 16384, 8, 6, 5, 1); }
      if (d % 400 == 0) E(0x55, 0x808080 + d / 400);
      if (nb && d % 6 == 0) { E(0x2A, 0); for (uint32_t j = 0; j < 12 * nb; j++) E(0x2B, 0x3F8000 + (j + d + f) % 64); }
      if (d % 100 == 0) { E(0x3A, 0); for (int j = 0; j < 12; j++) E(0x3B, 0x3F8000 + j + f); }
      E(0x10, (t + o) >> 8 & 0x0F0000); E(0x01, (t + o) & 0xFFFFFF); E(0x1E, 1); E(0x50, 1); E(0x04, 4 << 16 | 7);
      o += 7 * vs;
    }
  }
  E(0x0F, 0); E(0x0C, 0);
}

// late: the presenter takes one frame in three, as on the Vita, so the others are walked without recording
typedef struct { const char *name; void (*build)(int, int); int n, record, memo, readers, vram, late; } Scene;
static const Scene S[] = {
  { "rr_crowd", crowd, 3400, 1, 0, 0, 1, 0 },
  { "rr_crowd_late", crowd, 3400, 1, 0, 0, 1, 1 },
  { "rr_race", crowd, 900, 1, 0, 0, 1, 0 },
  { "rr_race_late", crowd, 900, 1, 0, 0, 1, 1 },
  { "sprites2d", sprites, 300, 1, 0, 0, 0, 0 },
  { "sublists", sublists, 40, 0, 1, 1, 0, 0 },
  { "images", images, 30, 1, 0, 0, 1, 0 },
  { "gow_menu_xfer", xfers, 59, 1, 0, 0, 1, 0 },
  { "gow_play_xfer", xfers, 97, 1, 0, 0, 1, 0 },
  { "gow_combat", combat, 97, 1, 0, 0, 1, 0 },
  { "gow_gpu", gpuCombat, 97, 1, 0, 0, 1, 0 },
};
#define SN (int)(sizeof S / sizeof *S)
#define FRAMES 40
typedef struct { uint32_t cmds, draws, verts, own, hits, complete; uint64_t ucp, ucmp, ucw, xw, dma_b; uint32_t dma_n, clock, qemu_us, qemu_min, ok, want; } Out;

// (REC_DUMP=<file>) Writes frame f of the record: header, draws, states, area, vertices for the GPU and the texture rows it
// holds, with a texture of the cache written as its entry (where the cache sits in memory changes between runs), so
// tools/perf/rec_check.sh can compare two versions of the capturer byte by byte
static FILE *rec_out;
static void recDump(RFrame *f) {
  uint32_t h[13] = { f->frame, f->fb, f->fbw, f->nd, f->ne, f->na, f->flags, f->draws_seen, f->nt, f->ntx, f->nvx, f->mir, f->mirf };
  fwrite(h, 4, 13, rec_out);
  for (uint32_t k = 0; k < f->nd; k++) {
    RDraw d = f->d[k];
    for (uint32_t i = 0; i < f->nt; i++) if (f->tc[i] && d.toff == f->to[i] + 1) d.toff = ~i;
    fwrite(&d, sizeof d, 1, rec_out);
  }
  fwrite(f->e, sizeof *f->e, f->ne, rec_out); fwrite(f->a, 1, f->na, rec_out); fwrite(f->tx + TX_N - f->nvx, 1, f->nvx, rec_out);
  for (uint32_t i = 0; i < f->nt; i++) {
    const uint8_t *p = f->tc[i] ? rc_tx + rc[f->tc[i] - 1].o : f->tx + f->to[i];
    uint32_t n = f->tn[i] & 0x7FFFFFFF, gb = f->tr[i], t[6] = { f->ta[i], f->tn[i], f->tc[i] ? ~0u : f->to[i], gb, f->tq[i], f->tc[i] };
    fwrite(t, 4, 6, rec_out); fwrite(&f->tm[i], 8, 1, rec_out);
    for (uint32_t g = 0; g < 64 && g * gb < n; g++) if (f->tm[i] >> g & 1) fwrite(p + g * gb, 1, n - g * gb < gb ? n - g * gb : gb, rec_out);
  }
}
static uint32_t cpuUs(void) { struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t); return t.tv_sec * 1000000 + t.tv_nsec / 1000; }
// Runs scene s for FRAMES frames through the real event path (enqueue, drawsync, flip) and adds up the counters of all
// frames in *o. The "GE" copies the requested VRAM pages after each flip and the presenter takes every frame.
static void run(const Scene *s, Out *o) {
  if (mmap((void *)0x70000000, 0x2000000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED ||
      mmap((void *)0x72000000, 0x400000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED ||
      mmap((void *)0x72C00000, 0x200000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) { perror("mmap"); exit(1); }
  rlo[0] = 0x08000000; rhi[0] = 0x0B000000; nr = 1; initCls(); level = 1;
  SB[0] = SB_MAGIC; SB[1] = 5;
  if (s->record) { reg_on = 1; regInit(); }
  if (s->memo) { MEMO_N = 512; POOL_N = 8192; memo_sh = 23; memo = calloc(MEMO_N, sizeof(Memo)); pool = calloc(POOL_N, 64); memo_on = 1; }
  if (s->readers) { cl_data = calloc(CL_N, 64); cl_tag = calloc(CL_N, 4); for (int i = 0; i < CL_N; i++) cl_tag[i] = 0xFFFFFFFFu; }
  mir_on = s->vram; pv_on = s->readers;
  if (getenv("REC_DUMP") && s->record) rec_out = fopen(getenv("REC_DUMP"), "wb");
  for (int f = 0; f < FRAMES; f++) {
    s->build(f, s->n);
    SB[0x40] = f; SB[0x44] = f + 1;  // copies finished / requested at this flip
    sec_t0 = now();  // (the per-second statistics reset never lands inside the frame)
    M.clock = 0; M.ucp = M.ucmp = M.ucw = M.dma_b = 0; M.dma_n = 0; uint64_t xw0 = bx_w;
    uint32_t own0 = R0.own, hit0 = R0.hits, t0 = cpuUs(), ts = now();
    CALLGRIND_TOGGLE_COLLECT;
    uint32_t enq[8] = { EV_ENQ, MAIN, 0, 1, ts, f + 1, 0, 0 };
    event(enq);
    if (s->readers) { pubList(&q[qh & 63]); for (int id = 0; id < 2; id++) { Rdr r; rdrStart(&r, &RR[id]); pvRun(id, &RR[id], r.ord); while (!r.done) rdrWalk(id, &r, 4000); } }
    while (qh != qt) walk();  // the capturer loop: keeps walking until the list is done (on time for the flip)
    uint32_t sy[8] = { EV_DSYNC, 0, 0, f + 1, now(), 0, 0, 0 };
    event(sy);
    uint32_t fl[8] = { EV_FLIP, 0x04000000, 512 | 3 << 16, 0, now(), 0, 0, 0 };
    event(fl);
    if (mir_on) {  // what the PSP GE does after the flip
      for (int p = 0; p < 512; p++) if ((want_l[p >> 5] | want_d[p >> 5]) >> (p & 31) & 1) CPY((f + 1) & 1, p) = f + 1;
      SB[0x40] = f + 1;
      if (reg_on) regPend();
    }
    rg_short = s->late;
    CALLGRIND_TOGGLE_COLLECT;
    uint32_t dt = cpuUs() - t0;  // (the fastest frame after the first: least disturbed by the host)
    o->qemu_us += dt; if (f && (!o->qemu_min || dt < o->qemu_min)) o->qemu_min = dt;
    o->own += R0.own - own0; o->hits += R0.hits - hit0; o->ucp += M.ucp; o->ucmp += M.ucmp; o->ucw += M.ucw; o->xw += bx_w - xw0; o->dma_b += M.dma_b; o->dma_n += M.dma_n; o->clock += M.clock;
    o->cmds += acc.cmds; o->draws += acc.draws; o->verts += acc.verts;
    if (reg_on) {
      int c = rg_pub >= 0 && rf[rg_pub]->frame == gen - 1 && !rf[rg_pub]->flags;
      o->complete += c; if (c && rec_out) recDump(rf[rg_pub]);
      if (!s->late || !(f % 3)) rg_last = rg_pub;
    }
    o->want = 0; for (int w = 0; w < 16; w++) o->want += __builtin_popcount(want_l[w] | want_d[w]);
    sceClibMemset(&acc, 0, sizeof acc);
  }
  o->ok = !acc.memo_bad;
  if (rec_out) fclose(rec_out);
}
int main(int argc, char **argv) {
  Out *r = mmap(0, SN * sizeof(Out), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  for (int i = 0; i < SN; i++) {
    if (argc > 1 && strcmp(argv[1], S[i].name)) continue;
    if (!fork()) { alarm(300); run(&S[i], r + i); _exit(0); }
    int st; wait(&st);
    Out *o = r + i;
    if (!WIFEXITED(st)) { printf("scene=%s FAILED\n", S[i].name); continue; }
    double f = FRAMES;
    double mem = (o->own * (double)C_READ256 + o->hits * (double)C_HIT + (o->ucp + o->ucmp) * (double)C_UCP_B + o->ucw * (double)C_UCW_B + o->xw * (double)C_XW_B +
                  o->dma_n * (double)C_DMA_CALL + o->dma_b * (double)C_DMA_B + o->clock * (double)C_CLOCK) / 1000 / f;
    printf("scene=%s frames=%d cmds/f=%u draws/f=%u verts/f=%u own256/f=%u hits/f=%u ucp_kb/f=%.1f ucmp_kb/f=%.1f ucw_kb/f=%.1f xw_kb/f=%.1f dma/f=%.1f dma_kb/f=%.1f clock/f=%u complete=%u want=%u mem_us/f=%.0f qemu_us/f=%.0f qemu_min_us=%u\n",
           S[i].name, FRAMES, (unsigned)(o->cmds / f), (unsigned)(o->draws / f), (unsigned)(o->verts / f), (unsigned)(o->own / f), (unsigned)(o->hits / f),
           o->ucp / 1024 / f, o->ucmp / 1024 / f, o->ucw / 1024 / f, o->xw / 1024 / f, o->dma_n / f, o->dma_b / 1024 / f, (unsigned)(o->clock / f), o->complete, o->want, mem, o->qemu_us / f, o->qemu_min);
  }
  return 0;
}
