// capture_test.c: test of the capturer (capture.c, included below) outside the Vita, on ARM under qemu on the PC.
// Builds synthetic lists in a fake "PSP RAM" (mmap at the Vita addresses), walks them for 8 frames and
// prints the capture counters per frame. The Vita functions are replaced by the ones below. Without arguments it runs
// every case and decides pass/fail by itself (exit code = failed checks): this is what tools/test_ci.sh runs.
// Build (<inc>: folder with only psp2* and vitasdk* of the vitasdk headers; with the whole include it clashes with libc):
// arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I<inc> -o capture_test capture_test.c
// Usage: qemu-arm-static ./capture_test [<mode> [<vram> [<readers>]]]
// - mode: 0 no sublist memory, 1 with memory, 2 checking all. The columns before "|" must be equal.
// - vram: 1 puts the sublists in VRAM (their content in the mirror): in frame 0 they are skipped and requested, from 1 on
//   they must give the same as in RAM. 2: also a page with no copy (cut sublists, without hanging). 3: each frame
//   writes its VRAM sublists only to that frame's mirror and sublist 10 changes in frame 4: the check
//   (mode 2) must detect the change with BAD=0.
// - readers: 1 both readers walk everything before the capturer (same columns as without readers; pre= counts
//   those applied with a reader's check). 2: they run after the capturer and skip all CALLs of the
//   top level (late=) without reading lines. 3: they only do the plan (advance check of what was applied in the
//   previous list) before the capturer.
// Variables: REG=1 frame record (record.c): checks each frame that recorded draws = counted (no memory:
// complete; with memory: incomplete because of the applied sublists). LEVEL=0|1|2 log level (default 2,
// extreme: list life and LATE need it). PVMIN=n minimum plan streak (default 1). LIFE=1 data
// life (samples that change 1 or 2 flips later; "life:" lines). LATE=1 simulates a late sync (line of
// nativedraw_late.log). Always at the end: list life (the main list changes one texture each frame).
// Double mirror: at the end of each frame f the "GE" copies the requested pages to copy f + 1, in mirror (f + 1) & 1,
// used by the next frame (like the flip event). Sublists are written to both mirrors (except vram 3).
// Does not simulate the Vita's notice to the PSP (the mirror copy the Vita announces in SB[0x48]).
#define NP_DEV  // the test needs the extreme level (data and list life, delays)
#include "capture.c"
// regFlip with a presenter that takes each frame at once
static void tFlip(uint32_t a, uint32_t b, uint32_t c) { regFlip(a, b, c); rg_last = rg_pub; }
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
SceUInt32 sceKernelGetProcessTimeLow(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000 + t.tv_nsec / 1000; }
void *sceClibMemset(void *d, int c, SceSize n) { return memset(d, c, n); }
void *sceClibMemcpy(void *d, const void *s, SceSize n) { return memcpy(d, s, n); }
void *sceClibMemmove(void *d, const void *s, SceSize n) { return memmove(d, s, n); }
int sceClibMemcmp(const void *a, const void *b, SceSize n) { return memcmp(a, b, n); }
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
void *sceDmacMemcpy(void *d, const void *s, SceSize n) { return memcpy(d, s, n); }
int sceGxmMapMemory(void *b, SceSize n, int a) { return 0; }
static void *blk[16]; static int nblk;  // blocks for the frame record
int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *i) { return -1; }
SceUID sceKernelAllocMemBlock(const char *n, SceKernelMemBlockType t, SceSize s, SceKernelAllocMemBlockOpt *o) { return nblk < 16 && (blk[nblk] = calloc(1, s)) ? nblk++ : -1; }
int sceKernelGetMemBlockBase(SceUID u, void **b) { if (u < 0 || u >= nblk) return -1; *b = blk[u]; return 0; }
int sceKernelFreeMemBlock(SceUID u) { return 0; }
int sceKernelGetMemBlockInfoByAddr(void *a, SceKernelMemBlockInfo *i) { return -1; }
SceUID emu_th[EMU_N]; int emu_aff[EMU_N], emu_n;

// Address to write a at in the fake RAM (VRAM, in the first mirror)
static uint32_t *W(uint32_t a) { return (uint32_t *)PSP(isVram(a) ? MIRROR + (a & 0x1FFFFF) : a); }
static uint32_t pc_, cur_f, vram;
// Writes command c with data d at pc_ and advances. A list in VRAM is written to both mirrors (with vram 3, only
// to the current frame's).
static void E(uint32_t c, uint32_t d) {
  if (vram == 3 && isVram(pc_)) *(uint32_t *)PSP((cur_f & 1 ? MIRROR2 : MIRROR) + (pc_ & 0x1FFFFF)) = c << 24 | (d & 0xFFFFFF);
  else *W(pc_) = c << 24 | (d & 0xFFFFFF);
  if (isVram(pc_) && vram != 3) *(uint32_t *)PSP(MIRROR2 + (pc_ & 0x1FFFFF)) = c << 24 | (d & 0xFFFFFF);
  pc_ += 4;
}
#define MAIN 0x08800000u
static uint32_t SUBS = 0x08900000u;
#define SHARED 0x08A00000u
// Builds the frame's lists: a shared (nested) sublist, 40 drawing sublists (some change BASE, call
// the shared one or use the entry vertices) and the main list calling them with different textures and colors.
// Some things change per frame to test change detection.
static void build(int frame) {
  cur_f = frame;
  // shared (nested) sublist: sets color/alpha and draws 1 strip with its own vertices
  pc_ = SHARED; E(0x10, 0x080000); E(0x01, 0xB00000); E(0x22, 0x123); E(0x04, 4 << 16 | 8); E(0x0B, 0);
  for (int k = 0; k < 40; k++) {
    pc_ = SUBS + k * 0x200;
    if (k % 3) E(0x01, 0xC00000 + k * 0x100);  // k%3==0 ones draw with the entry vertices (VDEP)
    E(0xC8, k & 3); E(0x3A, 0); for (int i = 0; i < 12; i++) E(0x3B, (k * 7 + i) & 0xFFFFFF);
    E(0x04, 4 << 16 | (6 + k));
    if (k % 5 == 0) { E(0x10, 0x080000); E(0x0A, SHARED & 0xFFFFFF); }  // (BASE 0x08: the shared one is in RAM)
    if (k % 7 == 0) { E(0x10, 0x090000); }      // changes BASE (leaks to the exit)
    E(0x04, 3 << 16 | 3);
    // This one changes from frame 3 on (4 with vram 3; different TEXLEVEL)
    if (k == 10 && frame >= (vram == 3 ? 4 : 3)) E(0xC8, 3);
    E(0x0B, 0);
  }
  pc_ = MAIN;
  E(0x12, 0x00011F); E(0x1E, 1); E(0xD3, 0); E(0x9C, 0x000000); E(0x9D, 0x040000 | 512);
  for (int k = 0; k < 40; k++) {
    E(0x10, SUBS >> 8 & 0x0F0000);
    // Odd texture changes each frame (dependency)
    E(0xA0, 0x100000 + (k & 1 ? frame * 0x1000 : 0)); E(0xA8, 0x090000);
    E(0x01, 0xD00000 + k * 0x40 + (k == 12 && frame >= 5 ? 0x1000 : 0));
    E(0x0A, (SUBS + k * 0x200) & 0xFFFFFF);
    // The same one twice in a row (other entry: vertices)
    if (k == 20) { E(0x10, SUBS >> 8 & 0x0F0000); E(0x0A, (SUBS + k * 0x200) & 0xFFFFFF); }
  }
  E(0x0F, 0); E(0x0C, 0);
}
// Checksum of the GE state (registers, matrices, vaddr/iaddr): must match with or without sublist memory
static uint32_t stateSum(void) { uint32_t h = 0; for (int i = 0; i < 256; i++) h = h * 31 + reg[i]; for (int m = 0; m < 5; m++) for (int i = 0; i < 96; i++) h = h * 31 + mtx[m][i]; return h * 31 + vaddr * 7 + iaddr; }
// Result of one case, in memory shared with the process that compares them. col: columns before "|" per frame, without
// and with state; the rest are totals of what each case must exercise.
typedef struct { char col[8][64]; uint32_t st[8], hit, chk, bad, fail, pre, late, lines, pv, skip, whole, lost, rt1, rtr, wk, xm, pal, tex2, tc, srt, xf, late_pr, want, sametex, ok; } Res;
static Res *res;
// An image drawn on many times counts once: with the list full, anything in target pages would pass for an image
static int rtRepeatOk(void) {
  sceClibMemset(rt_pg, 0, sizeof rt_pg); rt_nb = 0;
  markRt(0x04195000, 0x10000); markRt(0x041FE000, 0x1000);
  for (int i = 0; i < 12; i++) { rtNote(0x04000000); rtNote(0x041FE000); }
  int ok = rt_nb == 2 && !rtRead(0x0419A040) && rtRead(0x041FE000);
  sceClibMemset(rt_pg, 0, sizeof rt_pg); rt_nb = 0;
  return ok;
}
static int wantKeptOk(void) {
  uint32_t on = mir_on, p = 0x19A, q = 0x19B, c0 = CPY(0, p), c1 = CPY(1, p);
  mir_on = 1; sceClibMemset(rt_pg, 0, sizeof rt_pg); sceClibMemset(want_now, 0, sizeof want_now);
  want(0x0419A000, 0x1000, 0);
  want_d[q >> 5] |= 1u << (q & 31);  // (asked for in an earlier frame)
  markRt(0x0419A000, 0x2000);
  int ok = want_d[p >> 5] >> (p & 31) & 1 && !(want_d[q >> 5] >> (q & 31) & 1);
  CPY(0, p) = 0; CPY(1, p) = 5; ok = ok && rgMirrored(0x0419A000, 0x1000);
  CPY(1, p) = 0; ok = ok && !rgMirrored(0x0419A000, 0x1000);
  CPY(0, p) = c0; CPY(1, p) = c1; mir_on = on;
  sceClibMemset(rt_pg, 0, sizeof rt_pg); sceClibMemset(want_d, 0, sizeof want_d); sceClibMemset(want_now, 0, sizeof want_now);
  return ok;
}
// A RAM to VRAM transfer reaches the mirror page by page; one aimed above the real 2 MB of VRAM writes nothing; a block
// over three pages, the last without a copy, fills the first two (joined into one run) and leaves the third alone
static int xferMirrorOk(void) {
  volatile uint32_t *src = (volatile uint32_t *)PSP(0x08900000), *m = (volatile uint32_t *)PSP(MIRROR + 0x190000);
  mir_u = 0; mir_ok[0][0x190 >> 5] |= 1u << (0x190 & 31);
  for (int i = 0; i < 512; i++) src[i] = 0xCA000000u + i;
  reg[0xB3] = reg[0xB5] = 128; reg[0xEB] = reg[0xEC] = 0; reg[0xEE] = 3 | 1 << 10;
  xferMirror(0x08900000, 0x04190000, 4);
  int ok = m[0] == src[0] && m[3] == src[3] && !m[4] && m[128] == src[128] && m[131] == src[131] && !m[256];
  src[0] = 1;
  xferMirror(0x08900000, 0x04390000, 4);
  ok = ok && m[0] != 1;
  mir_ok[0][0x190 >> 5] |= 3u << (0x190 & 31);
  for (int i = 0; i < 3072; i++) src[i] = 0xCB000000u + i;
  reg[0xEE] = 127 | 23 << 10;
  xferMirror(0x08900000, 0x04190000, 4);
  ok = ok && m[0] == src[0] && m[2047] == src[2047] && !m[2048] && !m[3071];
  for (int i = 0; i < 3072; i++) m[i] = 0;
  reg[0xB3] = reg[0xB5] = reg[0xEE] = 0; mir_ok[0][0x190 >> 5] = 0;
  return ok;
}
static int report(const char *name, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", name); return !ok; }
// Frames from..7 of case a equal those of b (state too if st).
static int same(Res *a, Res *b, int from, int st) {
  for (int f = from; f < 8; f++) if (strcmp(a->col[f], b->col[f]) || (st && a->st[f] != b->st[f])) return 0;
  return 1;
}
// rgTexCopy: a swizzled texture of 'groups' groups of 8 rows and a plain one come out right, below and above the
// size that goes by DMA
static int texCopyOk(uint32_t groups) {
  uint32_t rb = 128, m = groups * 8 * rb, ok = 1;
  uint8_t *s = malloc(m), *d = malloc(m);
  rg_ord = malloc(OR_N);
  for (uint32_t i = 0; i < m; i++) s[i] = i * 31 + (i >> 8);
  rgTexCopy(d, s, m, rb, 0x08800000u, -1, rg_ord);
  for (uint32_t g = 0; g < groups; g++)
    for (uint32_t r = 0; r < 8; r++)
      for (uint32_t b = 0; b < rb; b += 16)
        ok &= !memcmp(d + g * 8 * rb + r * rb + b, s + g * 8 * rb + b * 8 + r * 16, 16);
  rgTexCopy(d, s, m, 0, 0x08800000u, -1, rg_ord);
  return ok && !memcmp(d, s, m);
}
// Runs each case in a child process (the capturer's state is global; a hang ends with the alarm) and compares them.
int main(int argc, char **argv);
static int checkAll(void) {
  enum { RAM, MEMO, ALL, RD1, RD2, RD3, V1REF, V1, V2REF, V2, V3REF, V3, REG0, REG1, REG2, N };
  static const int C[N][4] = { {0,0,0,0}, {1,0,0,0}, {2,0,0,0}, {1,0,1,0}, {1,0,2,0}, {1,0,3,0}, {0,1,0,0}, {2,1,0,0}, {0,2,0,0}, {2,2,0,0},
                               {0,3,0,0}, {2,3,0,0}, {0,0,0,1}, {1,0,0,1}, {1,0,0,2} };
  Res *r = mmap(0, N * sizeof(Res), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  int bad = 0;
  for (int i = 0; i < N; i++) {
    if (!fork()) {
      char a[3][4]; for (int k = 0; k < 3; k++) sprintf(a[k], "%d", C[i][k]);
      char *av[] = { "capture_test", a[0], a[1], a[2], 0 };
      if (C[i][3]) setenv("REG", C[i][3] == 2 ? "2" : "1", 1);
      res = r + i; alarm(60); freopen("/dev/null", "w", stdout);
      _exit(main(4, av));
    }
    int st; wait(&st); r[i].ok = WIFEXITED(st) && !WEXITSTATUS(st);
    if (!r[i].ok) { printf("FAIL case %d: the process died or hung\n", i); bad++; }
  }
  bad += report("memory on gives the same as off, and it applies sublists", same(r + MEMO, r + RAM, 0, 1) && r[MEMO].hit && !r[MEMO].bad);
  bad += report("checking all: same, checks them, no MISMATCH", same(r + ALL, r + RAM, 0, 1) && r[ALL].chk && !r[ALL].bad && r[ALL].fail);
  bad += report("readers ahead: same, their checks are used", same(r + RD1, r + RAM, 0, 1) && r[RD1].pre && !r[RD1].bad);
  bad += report("readers behind: same, they skip without reading", same(r + RD2, r + RAM, 0, 1) && r[RD2].late && !r[RD2].lines);
  bad += report("readers plan only: same, the plan is used", same(r + RD3, r + RAM, 0, 1) && r[RD3].pv && !r[RD3].bad);
  bad += report("VRAM sublists: same as RAM from frame 1, nothing skipped", same(r + V1, r + V1REF, 0, 1) && same(r + V1, r + RAM, 1, 0) && !r[V1].skip && !r[V1].bad);
  bad += report("VRAM page without copy: cut sublists, no hang", same(r + V2, r + V2REF, 0, 1) && r[V2].skip && !r[V2].bad);
  bad += report("VRAM per frame: the change is detected, no MISMATCH", same(r + V3, r + V3REF, 0, 1) && r[V3].fail && !r[V3].bad);
  bad += report("frame record: complete without memory; memory waits for frames over the record", same(r + REG0, r + RAM, 0, 1) && r[REG0].whole == 8 && r[REG1].whole == 8 && r[REG2].whole < 8);
  bad += report("frame record: lost events leave the frame incomplete", r[REG0].lost);
  bad += report("frame record: a TRANSFER on the drawn image becomes a draw", r[REG0].xf);
  bad += report("frame record: a texture rewritten between lists gets a new copy", r[REG0].tex2);
  bad += report("texture cache: reused between frames, a change goes to the record, then back", r[REG0].tc);
  bad += report("sorted textures skip what did not change; vertices queued for DMA come out right", r[REG0].srt);
  bad += report("frame record: the loaded palette survives changes in memory", r[REG0].pal);
  bad += report("frame record: an image only counts as drawn in its frame", r[REG0].rt1);
  bad += report("frame record: not recorded while the presenter does not take frames", r[REG0].late_pr);
  bad += report("repeated texture: a new record notes it again, and a new texture after an untextured draw is requested", r[REG0].sametex);
  bad += report("mirror: a texture page of an image stops being requested when no frame reads it, a list page stays", r[REG0].want);
  bad += report("mirror: a RAM to VRAM transfer is applied to it, joined across pages with a copy, nothing above the real VRAM", r[REG0].xm);
  bad += report("texture copy: right by CPU and by DMA", texCopyOk(1) && texCopyOk(32));
  bad += report("an image repeated among the targets does not hide textures in its pages", r[REG0].rtr);
  bad += report("a texture page used this frame stays requested under a target; another mirror's copy counts", r[REG0].wk);
  printf(bad ? "%d FAILED\n" : "all ok\n", bad);
  return bad;
}
// Arguments and environment variables: see the header. Per frame: builds, (readers), walks, simulates the mirror
// copy and prints the counter line; then the optional tests (LATE, LIFE) and the capture log.
int main(int argc, char **argv) {
  if (argc < 2) return checkAll();
  setvbuf(stdout, NULL, _IONBF, 0); int mode = atoi(argv[1]);  // 0 no memory, 1 with memory, 2 checking all
  vram = argc > 2 ? atoi(argv[2]) : 0; int rdr_mode = argc > 3 ? atoi(argv[3]) : 0;
  level = getenv("LEVEL") ? atoi(getenv("LEVEL")) : 2;  // log level: extreme unless LEVEL=0/1
  if (vram) { SUBS = 0x04100000u; mir_on = 1; }
  pv_on = rdr_mode == 3; pv_min = getenv("PVMIN") ? atoi(getenv("PVMIN")) : 1;
  if (mmap((void *)0x70000000, 0x2000000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) { perror("mmap"); return 1; }
  if (mmap((void *)0x72C00000, 0x200000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) { perror("mmap3"); return 1; }
  if (mmap((void *)0x72000000, 0x400000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) { perror("mmap2"); return 1; }
  rlo[0] = 0x08000000; rhi[0] = 0x0B000000; nr = 1; initCls();
  if (getenv("REG")) { reg_on = 1; reg_big = atoi(getenv("REG")) == 2; regInit(); }
  if (mode) { MEMO_N = 512; POOL_N = 8192; memo_sh = 23; memo = calloc(MEMO_N, sizeof(Memo)); pool = calloc(POOL_N, 64); memo_on = 1; memo_chk = mode == 2; }
  for (int f = 0; f < 8; f++) {
    build(f);
    mir_u = f;  // the copy made at the end of the previous frame (on the Vita: the last finished, when the list starts)
    sceClibMemset(&acc, 0, sizeof acc);
    List *l = &q[qt++ & 63]; memset(l, 0, sizeof *l); l->pc = MAIN; l->ord = f + 1; l->lo = ~0u;
    if (rdr_mode == 1 || rdr_mode == 3) pubList(l);
    // The readers go ahead: they walk the whole list (and check what was saved) before the capturer
    if (rdr_mode == 1)
      for (int id = 0; id < 2; id++) { Rdr s; rdrStart(&s, &RR[id]); while (!s.done) rdrWalk(id, &s, 2000); }
    if (rdr_mode == 3)  // only the plan: what was applied in the previous list, before the capturer
      for (int id = 0; id < 2; id++) { Rdr s; rdrStart(&s, &RR[id]); pvRun(id, &RR[id], s.ord); }
    walk();
    if (rdr_mode == 2) {  // the readers arrive later (behind): they must skip everything the capturer already reached
      for (int id = 0; id < 2; id++) { Rdr s; rdrStart(&s, &RR[id]); while (!s.done) rdrWalk(id, &s, 2000); }
      if (res) res->late += lv_late[0] + lv_late[1], res->lines += lv_lines[0] + lv_lines[1];
      printf("   readers behind: late=%u lines=%u\n", lv_late[0] + lv_late[1], lv_lines[0] + lv_lines[1]);
    }
    if (vram) {  // what the PSP GE would do: copy the requested pages (with vram 2, all except 0x102)
      int pl = 0, pd = 0;
      for (int p = 0; p < 512; p++) {
        int w = p >> 5, b = 1 << (p & 31);
        pl += !!(want_l[w] & b); pd += !!(want_d[w] & b);
        if ((want_l[w] | want_d[w]) & b && !(vram == 2 && p == 0x102)) CPY((f + 1) & 1, p) = f + 1;
      }
      SB[0x40] = f + 1;
      printf("   mirror: requested lists=%d data=%d walked=%u skipped=%u cut=%u\n", pl, pd, acc.v_ok, acc.vcalls, acc.v_cut);
    }
    if (res) {
      sprintf(res->col[f], "%u %u %u %u %u %u %u %u", acc.cmds, acc.draws, acc.verts, acc.vbytes, acc.textures, acc.prim[3], acc.prim[4], acc.calls_ram);
      res->st[f] = stateSum(); res->hit += acc.memo_hit; res->chk += acc.memo_chk; res->bad += acc.memo_bad; res->fail += acc.memo_fail;
      res->pre += acc.memo_pre; res->pv += pv_ok[0] + pv_ok[1]; if (f) res->skip += acc.vcalls;
    }
    printf("f%d cmds=%u draws=%u verts=%u vbytes=%u tex=%u prim3=%u prim4=%u calls=%u state=%08x | hit=%u miss=%u dep=%u fail=%u no=%u chk=%u BAD=%u lines=%u skipped=%u pre=%u lv=%u/%u/%u pv=%u/%u/%u\n",
           f, acc.cmds, acc.draws, acc.verts, acc.vbytes, acc.textures, acc.prim[3], acc.prim[4], acc.calls_ram, stateSum(),
           acc.memo_hit, acc.memo_miss, acc.memo_dep, acc.memo_fail, acc.memo_no, acc.memo_chk, acc.memo_bad, acc.memo_lines, acc.memo_cmds, acc.memo_pre,
           lv_lines[0] + lv_lines[1], lv_ok[0] + lv_ok[1], lv_bad[0] + lv_bad[1], pv_pn, pv_ok[0] + pv_ok[1], pv_bad[0] + pv_bad[1]); pv_ok[0] = pv_ok[1] = pv_bad[0] = pv_bad[1] = 0; lv_lines[0] = lv_lines[1] = lv_ok[0] = lv_ok[1] = lv_bad[0] = lv_bad[1] = lv_late[0] = lv_late[1] = 0;
    if (reg_on) {  // close the record as in the flip and check it
      int w = rg_w; RFrame *g = rf[w];
      uint32_t nd = g->nd, ne = g->ne, na = g->na, fl = g->flags, n_seen = g->draws_seen;
      tFlip(0x04000000, 512, acc.draws);
      printf("   record: draws=%u (counted %u, seen %u) states=%u bytes=%u flags=%x %s published=%d\n", nd, acc.draws, n_seen, ne, na, fl,
             !fl && nd == acc.draws ? "COMPLETE" : "incomplete", rg_pub);
      if (res) res->whole += !fl && nd == acc.draws;
    }
    lifeFlip(); lf_ord_cmds = 0;  // as in the flip
    gen++; ntex = 0;
  }
  if (reg_on && res) {
    markRt(0x04100000, 4096); tFlip(0x04000000, 512, 0);
    res->rt1 = !rt_pg[0x100 >> 5];
    // Only the start of an image is held in HD: the rest of its pages comes from the mirror
    markRt(0x04100000, 4096); rt_fb[0] = 0x100000; rt_nb = 1;
    res->rt1 &= rtRead(0x04100000) && !rtRead(0x04100400) && rtPage(0x04100400);
    sceClibMemset(rt_pg, 0, sizeof rt_pg); rt_nb = 0;
    res->rtr = rtRepeatOk(); res->wk = wantKeptOk(); res->xm = xferMirrorOk();
    *W(0x08C30000) = 1; reg[0xA0] = 0xC30000; reg[0xA8] = 0x080000 | 8; reg[0xB8] = 3 << 8 | 3; reg[0xC3] = 3;  // 8x8, 8888
    pub_ord = 100; uint32_t t0 = rgTex(0, 8), t1 = rgTex(0, 8);
    pub_ord = 101; uint32_t t2 = rgTex(0, 8);
    *W(0x08C30000) = 2; pub_ord = 102; uint32_t t3 = rgTex(0, 8);
    res->tex2 = t0 && t0 == t1 && t1 == t2 && t3 != t2 && *(uint32_t *)(rf[rg_w]->tx + t3 - 1) == 2;
    rf[rg_w]->flags = 1;  // (not published)
    tFlip(0x04000000, 512, 0);
    // The same texture in frames in a row: copied once; changed, it goes to the record until nobody reads the entry
    *W(0x08C31000) = 5; reg[0xA0] = 0xC31000; reg[0xA8] = 0x080000 | 256; reg[0xB8] = 3 << 8 | 8;  // 256x8, 8 KB
    uint8_t *p0 = rf[rg_w]->tx + rgTex(0, 8) - 1;
    uint32_t cp = ga.t_n[0];
#define NEXT() do { rf[rg_w]->nd = 1; rf[rg_w]->draws_seen = ~0u; tFlip(0x04000000, 512, 0); } while (0)
    NEXT();
    uint8_t *p1 = rf[rg_w]->tx + rgTex(0, 8) - 1;
    res->tc = p0 == p1 && ga.t_n[0] == cp;
    *W(0x08C31000) = 6; NEXT();
    uint8_t *p2 = p1;  // (a group is sampled 1 frame in 4)
    for (int k = 0; k < 4 && p2 == p1; k++) { p2 = rf[rg_w]->tx + rgTex(0, 8) - 1; if (p2 == p1) { gen++; NEXT(); } }
    res->tc = res->tc && p2 != p1 && *(uint32_t *)p2 == 6;
    for (int k = 0; k < RN + 1; k++) { NEXT(); }
    uint8_t *p3 = rf[rg_w]->tx + rgTex(0, 8) - 1;
    res->tc = res->tc && p3 == p1 && *(uint32_t *)p3 == 6;
    // The sample reads other columns besides the first one, and small textures stay out of the cache
    uint32_t s0 = rcSample((uint8_t *)W(0x08C31000), 8192, 1024);
    W(0x08C31000)[(1024 + 640) / 4] = 7;
    res->tc = res->tc && s0 != rcSample((uint8_t *)W(0x08C31000), 8192, 1024) && !rcGet(0x08C32000, 2048, 256);
    {
      uint32_t m = 16384, same;
      uint8_t *sr = malloc(m), *ds = rf[0]->tx;
      for (uint32_t i = 0; i < m; i++) sr[i] = i * 31;
      rg_ordc = 1; rgTexCopy(ds, sr, m, 128, 0x08900000u, 0, rg_ord); same = ga.t_same;
      rgTexCopy(ds, sr, m, 128, 0x08900000u, 0, rg_ord);
      res->srt = ga.t_same - same == m;
      sr[0] ^= 0xFF; same = ga.t_same; rgTexCopy(ds, sr, m, 128, 0x08900000u, 0, rg_ord);
      res->srt = res->srt && ga.t_same - same == m - 1024 && ds[0] == sr[0];
    }
    {  // vertices that wait: two pieces out of order come out right in area a, and those for the GPU at the end of tx
      RFrame *g = rf[rg_w];
      *W(0x08C34000) = 0x11; *W(0x08C33000) = 0x22; rg_ordc = 1;
      *W(0x08C32000) = 0x33;
      uint32_t fl = g->flags; g->flags = 0;
      uint32_t v1 = rgVLater(0x08C34000, 4, 0, 0), v2 = rgVLater(0x08C33000, 4, 0, 0), v3 = rgVLater(0x08C32000, 24, 24, 1);
      rgVFlush();
      res->srt = res->srt && !(g->flags & R_READ);  // (positions 0: fine)
      *W(0x08C31000 + 16) = 0x6394bdbc;  // (a garbage position, ~5e21, for the GPU: the frame is not drawn)
      rgVLater(0x08C31000, 24, 24, 1); rgVFlush();
      res->srt = res->srt && g->flags & R_READ; g->flags = fl;
      g->nvx -= 48;
      RDraw dv = { .voff = v3 };
      res->srt = res->srt && v1 && v2 && *(uint32_t *)(g->a + v1 - 1) == 0x11 && *(uint32_t *)(g->a + v2 - 1) == 0x22 && !vpn;
      res->srt = res->srt && v3 & VX && g->nvx == 48 && (v3 & ~VX) == TX_N - 48 && *(uint32_t *)RV(g, &dv) == 0x33;  // (for the GPU: end of tx)
      g->nvx = 0;
    }
#undef NEXT
    rf[rg_w]->flags = 1; tFlip(0x04000000, 512, 0);
    RFrame *x = rf[rg_w];  // 64x64 from RAM to (8, 14) of the image being drawn, and one that is not on it
    reg[0x9C] = 0x88000; reg[0x9D] = 512; reg[0xD2] = 3; reg[0xD5] = 479 | 271 << 10; reg[0xB3] = 64 | 0x080000; reg[0xB2] = 0xC30000;
    reg[0xB4] = 0x88000; reg[0xB5] = 512 | 0x040000; reg[0xEB] = 0; reg[0xEC] = 8 | 14 << 10; reg[0xEE] = 63 | 63 << 10;
    regXfer(0x08C30000, 0x04088000, 4);
    int16_t *xv = (int16_t *)(x->a + x->d[0].voff - 1);
    res->xf = x->nd == 1 && !x->flags && x->d[0].toff && xv[4] == 8 && xv[5] == 14 && xv[12] == 72 && xv[13] == 78 && xv[8] == 64 && RE(x->e[0], 0x9C) == 0x88000;
    regXfer(0x08C30000, 0x04154000, 4);
    res->xf = res->xf && x->nd == 1 && !x->flags;
    *W(0x08C20000) = 0x11223344; reg[0xB0] = 0xC20000; reg[0xB1] = 0x080000; last_clut[0] = CLUTADDR(); regClut(1);
    *W(0x08C20000) = 0; tFlip(0x04000000, 512, 0);
    res->pal = *(uint32_t *)(rf[rg_w]->a + rg_clut - 1) == 0x11223344;
    resetQueue(); res->lost = rf[rg_w]->flags != 0;  // events lost in the middle of a frame
    // A presenter that does not take frames: a capturer with time keeps recording; short of time the next frame is only
    // walked, and taking it resumes
    rf[rg_w]->flags = 0; rf[rg_w]->nd = 1; rf[rg_w]->draws_seen = ~0u; tFlip(0x04000000, 512, 0); rg_last = -1;
    rf[rg_w]->nd = 1; rf[rg_w]->draws_seen = ~0u; regFlip(0x04000000, 512, 0);
    res->late_pr = !rf[rg_w]->flags;
    rf[rg_w]->nd = 1; rf[rg_w]->draws_seen = ~0u; rg_short = 1; regFlip(0x04000000, 512, 0);
    res->late_pr &= rf[rg_w]->flags == R_SKIP;
    rg_last = rg_pub; regFlip(0x04000000, 512, 0); res->late_pr &= !rf[rg_w]->flags;
    // All the records in use by the GPU: the next frame is only walked, so none is written
    volatile uint32_t gdone = 0; rg_gdone = &gdone; for (int i = 0; i < RN; i++) rg_gq[i] = 1;
    rf[rg_w]->nd = 1; rf[rg_w]->draws_seen = ~0u; regFlip(0x04000000, 512, 0);
    res->late_pr &= rf[rg_w]->flags == R_SKIP;
    rg_gdone = 0; rf[rg_w]->flags = 0;
    reg[0xA0] = 0x130000; reg[0xA8] = 0x040000 | 64; reg[0xB8] = 6 << 8 | 6; reg[0xC3] = 3; reg[0xC2] = 0; CPY(0, 0x130) = CPY(0, 0x131) = 1;
    uint32_t ts0 = rgTex(0, 8);
    rf[rg_w]->flags = 1; regFlip(0x04000000, 512, 0);  // (incomplete: the same record is used again)
    res->sametex = ts0 && !rf[rg_w]->nt && rgTex(0, 8) && rf[rg_w]->nt == 1;
    int ro = reg_on; reg_on = 0; mir_on = 1; sceClibMemset(want_d, 0, sizeof want_d); sceClibMemset(rt_pg, 0, sizeof rt_pg);
    reg[0x1E] = 1; reg[0xD3] = 0; dirty = C_TEX; draw(3 << 16 | 1);  // textured, untextured, then textured with another texture and nothing dirty
    reg[0x1E] = 0; draw(3 << 16 | 1);
    reg[0x1E] = 1; reg[0xA0] = 0x150000; draw(3 << 16 | 1);
    res->sametex = res->sametex && want_d[0x150 >> 5] >> (0x150 & 31) & 1; reg_on = ro;
    mir_on = 1; want(0x04100000, 4096, 2); want(0x04120000, 4, 1); regFlip(0x04000000, 512, 0);
    res->want = want_l[8] & 1;  // (page 0x100, read in this frame)
    regFlip(0x04000000, 512, 0);
    res->want = res->want && !(want_l[8] & 1) && want_l[9] & 1;
  }
  printf("list life: flip of its frame %u (top %u, sublists %u) | next flip %u (%u, %u) (expected 8 (0, 0) | 7 (7, 0))\n", lf_n[0], lf_top[0], lf_sub[0], lf_n[1], lf_top[1], lf_sub[1]);
  // A sync arriving 100 us after queuing: the Vita is behind (line in nativedraw_late)
  if (getenv("LATE")) {
    build(0); qh = qt = 0; capt = 0;
    uint32_t t = now() - 50, ev[8] = { EV_ENQ, MAIN, 0, 7, t, 99, 0, 0 };
    event(ev);
    uint32_t sy[8] = { EV_DSYNC, 0, 0, 99, t + 100, 0, 0, 0 };
    event(sy);
    printf("late: pending=%u (expected 1)\n", npend);
    walk();
    printf("late: pending=%u capt=%u (expected 0 99) behind=%u\n", npend, capt, acc.pf_late);
    uint32_t sy2[8] = { EV_DSYNC, 0, 0, 99, now() + 5000, 0, 0, 0 };  // another sync later: on time, no line
    event(sy2);
    RG[1].b[RG[1].w % RG[1].n] = 0; printf("late (log):\n%s", RG[1].b);
  }
  if (getenv("LIFE")) {  // data life by frames. 8 vertex samples per frame (the limit)
    smp_r = smp_w; sceClibMemset((void *)&LT, 0, sizeof LT);  // (without the samples from the frames above)
    for (int k = 0; k < 4; k++) smp_f[k] = smp_k[k] = 0;
    for (int k = 0; k < 8 * 64; k++) smpAdd(0x08C00000u + (k / 64) * 0x100, 64, 0);  // frame A: groups 0-7
    for (int g = 2; g < 5; g++) *W(0x08C00000u + g * 0x100) ^= 1;                    // 3 change before flip 1
    gen++; for (int k = 0; k < 4; k++) smp_f[k] = 0; ltFlip();
    for (int g = 5; g < 8; g++) *W(0x08C00000u + g * 0x100 + 8) ^= 1;                // 3 change before flip 2...
    // ...and 1 of them with a range notice
    cw[0].lo = 0x08C00000u + 5 * 0x100; cw[0].hi = cw[0].lo + 0x100; cw[0].t = now(); cw_n = 1;
    cw_all_t = 0;
    gen++; ltFlip();
    printf("life: n=%u changed_1f=%u 2f=%u range_notice=%u all_notice=%u (expected 8 / 3 / 6 / 1 / 0)\n", LT.n[0], LT.ch1[0], LT.ch2[0], LT.warn[0], LT.wall[0]);
    cw_all_t = now() + 1;  // empty the whole cache: covers the others
    for (int k = 0; k < 8 * 64; k++) smpAdd(0x08C10000u + (k / 64) * 0x100, 64, 0);
    for (int g = 0; g < 4; g++) *W(0x08C10000u + g * 0x100) ^= 1;
    cw_all_t = now() | 1;
    gen++; for (int k = 0; k < 4; k++) smp_f[k] = 0; ltFlip(); gen++; ltFlip();
    printf("life: n=%u changed_2f=%u all_notice=%u (expected 16 / 10 / 4)\n", LT.n[0], LT.ch2[0], LT.wall[0]);
  }
  lbuf[lpos] = 0; printf("%s", lbuf);
  return 0;
}
