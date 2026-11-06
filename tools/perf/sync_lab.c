// sync_lab.c: lab for how the capturer reads PSP memory, outside the Vita (ARM under qemu, like capture_bench.c).
// The real capturer and frame record (nativedraw/capture.c) walk the lists of a scripted game, fed through the same
// events NativeLink posts, while the lab plays the game, the GE and NativeLink's timing. It answers two questions
// before anything reaches the Vita:
// - correctness: does each published frame hold what the GE really drew? A frame's fingerprint (hash of its draws,
//   states and vertices in the record) is compared with the same frame captured with the game held (reference);
// - photo at the sync (docs/dev/en/performance-plan.md, "Reading PSP memory"): the game's RAM pages the frame will need
//   are copied at the sync into the memory the capturer reads, the game goes on writing its own copy, and the capturer
//   walks the photo afterwards. Pages are predicted from the pages the last walks of the same list touched; a page
//   outside the photo is a miss (read from the game's live RAM, the frame is dropped). The touched pages are found with
//   mprotect: every game RAM page is closed before the walk and opened by the fault handler on first touch.
// Modes: hold (the game waits at the sync until its list is captured: the reference), stream:N (today's gate before
// the enqueue: the capturer has walked N % of the list when the game reaches the sync, then the game rewrites what its
// scene rewrites after a sync), photo (photo at the sync; env PHOTO_K frames of history, PHOTO_GAP pages joined into
// one copy, PHOTO_EXT pages added around each, PHOTO_LISTS list buffers the game rotates through (Ridge Racer moves
// its list start every frame), PHOTO_SLOTS lists the history remembers, as the 4 of fpFlip in capture.c, PHOTO_BYLIST=0
// the history of the last frames whatever their list).
// Scenes: pieces (God of War: sublist pieces built in RAM, copied to VRAM with TRANSFER and called there, 8 VRAM slots
// reused 3 times per frame, the RAM pieces rewritten right after the sync), pieces2 (the same with the pieces in two
// buffers: nothing rewritten before the capture), ring (vertices in a ring buffer that moves every frame).
// Build and run: sh tools/perf/sync_lab.sh. Usage: sync_lab [scene [mode]] (no arguments: every combination, one
// line each); sync_lab check: the claims tools/test_ci.sh keeps (exit code = failed checks).
#define NP_DEV
#include "../../nativedraw/capture.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

// --- Vita functions replaced (as in nativedraw/capture_test.c) ---
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
static void *blk[16]; static int nblk;
int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *i) { return -1; }
SceUID sceKernelAllocMemBlock(const char *n, SceKernelMemBlockType t, SceSize s, SceKernelAllocMemBlockOpt *o) { return nblk < 16 && (blk[nblk] = calloc(1, s)) ? nblk++ : -1; }
int sceKernelGetMemBlockBase(SceUID u, void **b) { if (u < 0 || u >= nblk) return -1; *b = blk[u]; return 0; }
int sceKernelFreeMemBlock(SceUID u) { return 0; }
int sceKernelGetMemBlockInfoByAddr(void *a, SceKernelMemBlockInfo *i) { return -1; }
SceUID emu_th[EMU_N]; int emu_aff[EMU_N], emu_n;

// --- The game's memory ---
#define GRAM 0x08800000u                // game RAM (24 MB), the part the photo covers
#define NPG ((0x0A000000u - GRAM) >> 12)
#define VIEW ((uint8_t *)PSP(GRAM))     // what the capturer reads
static uint8_t *live, vram[0x200000];   // photo mode: the game's own RAM; the real VRAM (written by the "GE")
static int photo;
static uint8_t inph[NPG], touch[NPG];   // page in the photo; touched by the walk of this frame
static uint32_t nmiss, ntouch;
// RAM word a as the game writes it: its own copy in photo mode, the capturer's otherwise
static uint32_t *G(uint32_t a) { return (uint32_t *)(photo ? live + (a - GRAM) : VIEW + (a - GRAM)); }
// First touch of a closed page: opens it; outside the photo it is a miss and gets the game's live content
static void onFault(int s, siginfo_t *si, void *u) {
  uint8_t *a = si->si_addr;
  uint32_t p = (uint32_t)(a - VIEW) >> 12;
  if (a < VIEW || p >= NPG) { signal(SIGSEGV, SIG_DFL); return; }
  mprotect(VIEW + p * 4096, 4096, PROT_READ | PROT_WRITE);
  if (!inph[p]) { memcpy(VIEW + p * 4096, live + p * 4096, 4096); nmiss++; }
  if (!touch[p]) { touch[p] = 1; ntouch++; }
}

// --- The scripted game ---
#define LST 0x08900000u   // main list, two buffers of 16 KB
#define STG 0x08A00000u   // RAM pieces (pieces2: two buffers of 64 KB)
#define VB 0x08B00000u    // vertices of the pieces, two buffers of 32 KB
#define RING 0x08C00000u  // ring scene: vertex ring of 256 KB
#define SLOT 0x04100000u  // 8 VRAM slots of 512 bytes
#define NP 24             // pieces (draws) per frame
#define VT 0x80011Cu      // through mode, 8888 color, 16-bit position: 12 bytes per vertex
static int scene, frames, nlists = 2;
static uint32_t pc_, xs[NP], xd[NP], nx;  // writing position; transfers of the frame (source, slot) for the GE
static void E(uint32_t c, uint32_t d) { *G(pc_) = c << 24 | (d & 0xFFFFFF); pc_ += 4; }
static uint32_t stg(int f, int k) { return STG + (scene == 1 ? (f & 1) * 0x10000 : 0) + k * 512; }
static uint32_t vtx(int f, int k) { return scene == 2 ? RING + (uint32_t)((f * NP + k) * 576) % 0x40000 : VB + (f & 1) * 0x8000 + k * 64; }
static void verts(int f, int k, int n) {  // n vertices whose color and position depend on the frame and the piece
  uint32_t *v = G(vtx(f, k));
  for (int i = 0; i < n; i++) { v[3 * i] = 0xFF000000u | f << 8 | k; v[3 * i + 1] = (k * 16 + i * 4) | (f & 255) << 16; v[3 * i + 2] = 0; }
}
// The RAM pieces of frame f: vertex address, a color of the frame and a strip, then RET
static void pieces(int f) {
  for (int k = 0; k < NP; k++) {
    pc_ = stg(f, k);
    E(0x10, 0x080000); E(0x01, vtx(f, k) & 0xFFFFFF); E(0x55, f << 8 | k); E(0x04, 4 << 16 | 4); E(0x0B, 0);
  }
}
// Frame f: vertices and main list. pieces: each piece is copied to slot k % 8 and called there; ring: the vertices of
// each draw are the next stretch of the ring
static void build(int f) {
  nx = 0;
  for (int k = 0; k < NP; k++) verts(f, k, scene == 2 ? 48 : 4);
  if (scene != 2 && (f == 0 || scene == 1)) pieces(f);  // (pieces: after frame 0, written right after each sync)
  pc_ = LST + (f % nlists) * 0x4000;
  E(0x12, VT); E(0x1E, 0); E(0x9C, 0); E(0x9D, 512); E(0xD2, 3); E(0xD5, 479 | 271 << 10);
  for (int k = 0; k < NP; k++) {
    if (scene == 2) { E(0x10, 0x080000); E(0x01, vtx(f, k) & 0xFFFFFF); E(0x55, f << 8 | k); E(0x04, 4 << 16 | 48); continue; }
    uint32_t s = stg(f, k), d = SLOT + (k % 8) * 512;
    E(0xB2, s & 0xFFFFF0); E(0xB3, (s >> 8 & 0x0F0000) | 128); E(0xB4, d & 0xFFFFF0); E(0xB5, (d >> 8 & 0x0F0000) | 128);
    E(0xEB, 0); E(0xEC, 0); E(0xEE, 127); E(0xEA, 1);
    E(0x10, 0x040000); E(0x0A, d & 0xFFFFFF);
    xs[nx] = s; xd[nx++] = d;
  }
  E(0x0F, 0); E(0x0C, 0);
}
static uint32_t listLen(int f) { return pc_ - (LST + (f % nlists) * 0x4000); }
// What the GE does with the frame's list: its transfers, in order, into the real VRAM
static void geRun(void) { for (uint32_t i = 0; i < nx; i++) memcpy(vram + (xd[i] & 0x1FFFFF), G(xs[i]), 512); }
// The mirror copy NativeLink queues: the requested VRAM pages into copy c (mirror c & 1)
static void geMirror(uint32_t c) {
  for (int p = 0; p < 512; p++)
    if ((SB[0x20 + (p >> 5)] | SB[0x30 + (p >> 5)]) >> (p & 31) & 1) {
      memcpy((void *)PSP((c & 1 ? MIRROR2 : MIRROR) + p * 4096), vram + p * 4096, 4096);
      CPY(c & 1, p) = c;
    }
  SB[0x40] = SB[0x44] = c;
}
static void ev(uint32_t t, uint32_t a, uint32_t b, uint32_t c, uint32_t ord) { uint32_t e[8] = { t, a, b, c, now(), ord, 0, 0 }; event(e); }

// --- Fingerprint of a recorded frame: its draws, the state of each and their vertices ---
static uint32_t fnv(uint32_t h, const void *p, uint32_t n) { const uint8_t *b = p; while (n--) h = (h ^ *b++) * 16777619u; return h; }
static uint32_t print(RFrame *f) {
  uint32_t h = fnv(2166136261u, &f->nd, 4);
  for (uint32_t i = 0; i < f->nd; i++) {
    RDraw *d = &f->d[i];
    h = fnv(h, &d->prim, 2); h = fnv(h, &d->n, 4); h = fnv(h, &d->vt, 4);
    h = fnv(h, RV(f, d), d->vlen); h = fnv(h, f->e[d->st], sizeof f->e[0]);
  }
  return h;
}

// --- One run: scene s, mode m (0 hold, 1 stream, 2 photo), arg = % walked before the sync (stream) ---
#define FN 40
typedef struct { uint32_t fp[FN], pub[FN], miss[FN], held[FN], pages, runs, kb, touched, nf; } Run;
static Run *out;
static uint8_t hist[16][16][NPG];  // pages touched by the last walks of each list remembered (hk: its buffer, hn: walks)
static int hk[16], hn[16];
static void run(int s, int m, int arg) {
  scene = s; frames = s == 2 ? FN : 16; photo = m == 2;
  int K = getenv("PHOTO_K") ? atoi(getenv("PHOTO_K")) : 4, GAP = getenv("PHOTO_GAP") ? atoi(getenv("PHOTO_GAP")) : 4,
      EXT = getenv("PHOTO_EXT") ? atoi(getenv("PHOTO_EXT")) : 8, SL = getenv("PHOTO_SLOTS") ? atoi(getenv("PHOTO_SLOTS")) : 16, sl = 0, rr = 0,
      BY = getenv("PHOTO_BYLIST") ? atoi(getenv("PHOTO_BYLIST")) : 1;
  nlists = getenv("PHOTO_LISTS") ? atoi(getenv("PHOTO_LISTS")) : 2;
  if (K > 16) K = 16;
  if (SL < 1 || SL > 16) SL = 16;
  if (nlists < 1 || nlists > 64) nlists = 2;
  for (int k = 0; k < 16; k++) hk[k] = -1;
  if (photo) {
    live = calloc(1, NPG * 4096);
    struct sigaction sa = { 0 }; sa.sa_sigaction = onFault; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigaction(SIGSEGV, &sa, NULL);
  }
  level = 1; reg_on = 1; regInit(); mir_on = 1; SB[0] = SB_MAGIC; SB[1] = 5;
  int last = -1;
  for (int f = 0; f < frames; f++) {
    uint32_t ord = f + 1, lst = LST + (f % nlists) * 0x4000;
    build(f);
    uint32_t n = listLen(f), stall = m == 1 && arg < 100 ? lst + (n * arg / 100 & ~3u) : 0;
    ev(EV_ENQ, lst, stall, 7, ord);
    if (m == 1) walk();  // (hold walks it all below, photo after the sync)
    geRun();
    int held = 0;
    if (photo) {  // the photo: predicted pages of this list buffer into the capturer's memory, then all closed
      static uint8_t u[NPG];
      int any = 0;
      memset(u, 0, sizeof u); memset(inph, 0, sizeof inph); memset(touch, 0, sizeof touch); nmiss = ntouch = 0;
      for (sl = 0; sl < SL && hk[sl] != (BY ? f % nlists : 0); sl++) {}
      if (sl == SL) { sl = rr++ % SL; hk[sl] = BY ? f % nlists : 0; hn[sl] = 0; }  // (a list not remembered: it takes the oldest place)
      for (int k = 0; k < K && k < hn[sl]; k++) for (uint32_t p = 0; p < NPG; p++) u[p] |= hist[sl][k][p];
      for (uint32_t p = 0; p < NPG; p++)  // each touched page with EXT pages around it (buffers that move)
        if (u[p]) { any = 1; for (int q = (int)p - EXT; q <= (int)p + EXT; q++) if (q >= 0 && q < (int)NPG) inph[q] = 1; }
      held = !any;  // a list buffer never walked: the game waits at the sync (as hold)
      for (uint32_t p = 0, e = 0; p < NPG; p++) {  // runs of pages, joined when they are up to GAP pages apart: one copy each
        if (!inph[p]) continue;
        if (!e || p - e > (uint32_t)GAP) out->runs++;
        else while (e < p) inph[e++] = 1;
        e = p + 1;
      }
      mprotect(VIEW, NPG * 4096, PROT_READ | PROT_WRITE);
      for (uint32_t p = 0; p < NPG; p++) if (inph[p]) { memcpy(VIEW + p * 4096, live + p * 4096, 4096); out->pages++; }
      mprotect(VIEW, NPG * 4096, PROT_NONE);
    }
    if (m != 1) {
      if (photo) geMirror(ord);  // photo: the VRAM copy goes at the sync too
      if (!photo || held) walk();
    }
    if (!photo) ev(EV_LSYNC, 7, 0, 0, 0);
    if (s == 0) pieces(f + 1);  // God of War: the RAM pieces of the next frame, right after the sync
    if (photo && !held) walk();  // (the photo is the frame at its sync: the capturer reads it as if it were on time)
    if (photo) ev(EV_LSYNC, 7, 0, 0, 0);
    ev(EV_STALL, 7, 0, 0, 0);
    if (!photo) geMirror(ord);  // (today: the mirror copy is queued at the flip)
    ev(EV_FLIP, 0x04000000, 512, 0, 0);
    if (photo) {
      mprotect(VIEW, NPG * 4096, PROT_READ | PROT_WRITE);
      memcpy(hist[sl][hn[sl]++ % K], touch, NPG);
      out->miss[f] = held ? 0 : nmiss; out->touched += ntouch;  // (held: the live RAM is the frame's own)
    }
    out->held[f] = held;
    if (rg_pub >= 0 && rg_pub != last) {  // frame f published at its flip: the presenter takes it at once
      out->pub[f] = 1; out->fp[f] = print(rf[rg_pub]);
      last = rg_last = rg_pub;
    }
  }
  out->nf = frames;
  out->kb = out->pages * 4 / (frames ? frames : 1);
}
static Run *fork1(int s, int m, int arg) {
  Run *r = mmap(NULL, sizeof *r, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  memset(r, 0, sizeof *r);
  pid_t p = fork();
  if (!p) { out = r; alarm(60); run(s, m, arg); _exit(0); }
  int st; waitpid(p, &st, 0);
  if (!WIFEXITED(st) || WEXITSTATUS(st)) { printf("scene %d mode %d FAILED (signal %d)\n", s, m, WIFSIGNALED(st) ? WTERMSIG(st) : 0); r->nf = 0; }
  return r;
}
// Frames from 2 on (0 and 1 fill the mirror and the history): right, wrong (published and different from the reference:
// what would be drawn wrong), dropped (not published, or with a miss: the photo design does not draw those), held
typedef struct { int right, wrong, dropped, held, missed; } Sum;
static Sum judge(Run *r, Run *ref) {
  Sum s = { 0 };
  for (uint32_t f = 2; f < r->nf; f++) {
    s.held += r->held[f]; s.missed += r->miss[f] > 0;
    if (!r->pub[f] || r->miss[f]) s.dropped++;
    else if (ref->pub[f] && r->fp[f] == ref->fp[f]) s.right++;
    else s.wrong++;
  }
  return s;
}
static const char *SCN[3] = { "pieces", "pieces2", "ring" };
static void line(int s, const char *mode, Run *r, Run *ref) {
  Sum x = judge(r, ref);
  printf("scene=%s mode=%s frames=%d right=%d wrong=%d dropped=%d held=%d missed=%d", SCN[s], mode, r->nf > 2 ? r->nf - 2 : 0, x.right, x.wrong, x.dropped, x.held, x.missed);
  if (r->runs) printf(" photo_kb/f=%u copies/f=%.1f touched_pages/f=%.1f", r->kb, (double)r->runs / r->nf, (double)r->touched / r->nf);
  printf("\n");
  if (getenv("DETAIL"))  // per frame: published, same as the reference, misses, held
    for (uint32_t f = 0; f < r->nf; f++)
      printf("   f%u pub=%u same=%u miss=%u held=%u fp=%08x ref=%08x\n", f, r->pub[f], r->fp[f] == ref->fp[f], r->miss[f], r->held[f], r->fp[f], ref->fp[f]);
}
static int mode(const char *a, int *arg) { *arg = 100; if (!strncmp(a, "stream", 6)) { *arg = a[6] == ':' ? atoi(a + 7) : 0; return 1; } return !strcmp(a, "photo") ? 2 : 0; }
static int check(void) {
  int bad = 0;
  for (int s = 0; s < 3; s++) {
    Run *ref = fork1(s, 0, 100), *ph = fork1(s, 2, 0), *st = fork1(s, 1, 0);
    Sum h = judge(ref, ref), p = judge(ph, ref), t = judge(st, ref);
    int ok = h.right == (int)ref->nf - 2 && !h.wrong;
    printf("%s %s: hold publishes every frame\n", ok ? "ok  " : "FAIL", SCN[s]); bad += !ok;
    int n = ref->nf - 2;  // (the moving ring misses at its wrap: those frames are dropped, never wrong)
    ok = !p.wrong && p.right + p.dropped == n && p.right >= (s == 2 ? n * 3 / 4 : n - 2);
    printf("%s %s: photo draws nothing wrong (right %d, dropped %d)\n", ok ? "ok  " : "FAIL", SCN[s], p.right, p.dropped); bad += !ok;
    // today: a RAM to VRAM transfer met after the sync drops the frame, whether its source was rewritten or not
    ok = !t.wrong && (s == 2 ? t.right == n : !t.right);
    printf("%s %s: stream:0 %s (right %d, wrong %d, dropped %d)\n", ok ? "ok  " : "FAIL", SCN[s], s == 2 ? "is right" : "drops every frame", t.right, t.wrong, t.dropped);
    bad += !ok;
  }
  // Ridge Racer moves its list start every frame: with 8 list buffers and 4 lists remembered, a history by list leaves
  // every frame waiting for a live walk; the history of the last frames, whatever their list, leaves none and draws
  // nothing wrong
  setenv("PHOTO_LISTS", "8", 1); setenv("PHOTO_SLOTS", "4", 1);
  Run *ref = fork1(0, 0, 100), *bl = fork1(0, 2, 0);
  setenv("PHOTO_BYLIST", "0", 1);
  Run *rc = fork1(0, 2, 0);
  unsetenv("PHOTO_LISTS"); unsetenv("PHOTO_SLOTS"); unsetenv("PHOTO_BYLIST");
  Sum a = judge(bl, ref), c = judge(rc, ref);
  int n = ref->nf - 2, ok = a.held == n && !c.held && !c.wrong && c.right >= n - 2;
  printf("%s pieces, 8 lists: by list %d of %d wait; by recent frames none waits (right %d, wrong %d)\n", ok ? "ok  " : "FAIL", a.held, n, c.right, c.wrong);
  return bad + !ok;
}
int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  if (mmap((void *)0x70000000, 0x2000000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED ||
      mmap((void *)0x72000000, 0x400000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED ||
      mmap((void *)0x72C00000, 0x200000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) { perror("mmap"); return 1; }
  rlo[0] = 0x08000000; rhi[0] = 0x0B000000; nr = 1; initCls();
  if (argc > 1 && !strcmp(argv[1], "check")) return check();
  static const char *modes[] = { "hold", "stream:0", "stream:50", "stream:90", "photo" };
  for (int s = 0; s < 3; s++) {
    if (argc > 1 && strcmp(argv[1], SCN[s])) continue;
    Run *ref = fork1(s, 0, 100);
    for (int i = 0; i < 5; i++) {
      if (argc > 2 && strcmp(argv[2], modes[i])) continue;
      int arg, m = mode(modes[i], &arg);
      line(s, modes[i], m ? fork1(s, m, arg) : ref, ref);
    }
  }
  return 0;
}
