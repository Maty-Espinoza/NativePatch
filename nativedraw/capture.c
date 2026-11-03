// capture.c: NativeDraw capturer (Vita side). Follows the mailbox NativeLink writes in PSP RAM (SB, at
// 0x0A000000: queued lists, stalls, syncs, flips) and walks each list reading PSP RAM the way the GE would:
// tracks GE state, counts draws, state changes and textures, and feeds the frame record (record.c, included
// here) drawn by the presenter. Publishes in the mailbox the last captured list (SB[3], awaited by the
// NativeLink gate) and a heartbeat (SB[4]). Also: read-ahead readers, sublist memory, requests to the VRAM
// mirror and the logs nativedraw_capture.log and, in development builds (NP_DEV), nativedraw_frames.log and
// nativedraw_late.log.
// Started by nativedraw.c (capStart); capture_test.c includes it to test it on the PC.
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdarg.h>
#include <arm_neon.h>
#include "version.h"

// PSP(a): Vita address where Adrenaline maps PSP RAM address a
#define PSP(a) (0x68000000u + ((a) & 0x0FFFFFFFu))
#define SB ((volatile uint32_t *)PSP(0x0A000000u))  // NativeLink mailbox (word map in nativelink/main.c)
#define SB_MAGIC 0x424D4F53u
#define SB_N 4096  // events in the mailbox ring (8 words each, from SB[0x400])
#define LOGC "ux0:pspemu/nativedraw_capture.log"
// Mailbox event types (must match NativeLink)
enum { EV_ENQ = 1, EV_HEAD, EV_STALL, EV_DSYNC, EV_LSYNC, EV_FLIP, EV_GEDONE, EV_SYNCEND, EV_CPU, EV_CPUSUM, EV_FRAME, EV_LIFE };
static inline uint32_t now(void) { return sceKernelGetProcessTimeLow(); }  // Vita time in us

// --- Statistics ---
// acc accumulates for one second; at the flip it is copied to rep and the log thread writes it. All fields are
// uint32_t: recEnd and memoTry walk them as a word array (chg[] must stay last).
typedef struct {
  uint32_t t0, frames, lists, cmds, cmds_max, draws, draws_max, us, us_max, own, hits, rown[2];
  uint32_t d3d, dthr, dclear, dempty, prim[8], curves, verts;
  uint32_t ch_tex, ch_mtx, ch_rt, ch_other, ch_none, texnew, batches, batches_cont, rt_switch;
  uint32_t textures, tex_vram, dtex_vram, dvert_vram, vbytes, ibytes, cluts, clut_vram, transfers, tr_vram;
  uint32_t vcalls, vjumps, lost, bad, signals, bjumps, events, syncs, syncs_waited, sync_wait_us, behind, resyncs;
  uint32_t lsyncs, flips_behind, aborted;
  uint32_t batches_notl, only_tl, sleep_us, cpu_cap, cpu_rdr[2], cpu_core[4];  // processor time of each thread
  uint32_t calls_ram, memo_hit, memo_miss, memo_fail, memo_no, memo_cmds, memo_lines, memo_bad, memo_chk, memo_reset, memo_dep;
  // VRAM sublists walked from the mirror, cut short (page without copy), textures on a render target
  uint32_t v_ok, v_cut, tex_rt, memo_us;
  // applied with the readers' check; what the readers did
  uint32_t memo_pre, lv_lines, lv_ok, lv_bad, lv_late;
  // T6: PSP GE and game waits on the GE
  uint32_t ge_us, ge_runs, ge_max, ge_stall, ge_n, sy_n, sy_us, sy_wait, sy_busy;
  uint32_t dc_n, dc_kb, dm_n, dm_kb, cpu_drop, cpu_vram, dc_all;                   // T4: game CPU writes
  // EV_FRAME and delays
  uint32_t pf_n, pf_us, pf_gate, pf_gate_n, pf_nowait, pf_sync, pf_spin, pf_mir, pf_pg, pf_post, pf_late, pf_late_us;
  uint32_t eg_n, eg_us;  // game waits before queueing (NativeLink gate)
  uint32_t rd_us, rd_n;  // capturer's own timed reads (1 in 16)
  uint32_t hit_us, hit_n;  // timed shared cache lines (1 in 8)
  uint32_t pv_ok, pv_bad, pv_late, pv_plan;  // read-ahead check (readers) and plan sublists
  uint32_t chg[256];  // real changes of each "other" register (what prevents merging draws)
} Stats;
static Stats acc, rep;
static volatile int rep_ready;
// Log level (ux0:data/nativedraw/log_level): 0 minimum, 1 medium (no file), 2 extreme (only in development builds).
// NativeLink reads it at SB[0x4E].
static uint32_t level = 1;
#ifdef NP_DEV
#define EXTREME (level >= 2)
#else
#define EXTREME 0
#endif
static uint32_t f_cmds, f_draws, f_us;  // of the current frame

// --- PSP RAM reads ---
// PSP RAM is uncached from the ARM: each read waits a long time and nearly all the capturer's cost is that
// wait, not interpreting. Therefore:
// 1. 256 bytes are fetched at once (8 consecutive NEON loads: the waits overlap).
// 2. Two readers (cores 0 and 2, low priority: only those cores' spare time) walk the same list
//    ahead of time, sharing out the sublists, and leave the 64-byte lines in a shared cache from which the
//    capturer takes them. Each line is tagged with the list (sequence number): it is not reused across lists.
//    What lies past the stall (not written yet) is not shared.
// PSP RAM may span several Vita blocks: up to 4 readable ranges [rlo, rhi).
static uint32_t rlo[4], rhi[4], nr;
static inline uint32_t ramEnd(uint32_t a) {  // end of the readable range containing a (0: not readable)
  for (uint32_t i = 0; i < nr; i++) if (a >= rlo[i] && a + 4 <= rhi[i]) return rhi[i];
  return 0;
}
static inline int okRam(uint32_t a) { return ramEnd(a) != 0; }
static inline int isVram(uint32_t a) { return (a & 0x0F000000u) == 0x04000000u; }

// --- VRAM mirror ---
// PSP VRAM is not mapped in Adrenaline. The capturer requests 4 KB pages in two mailbox bitmaps:
// "lists" (sublists the game calls in VRAM) and "data" (textures and palettes; not those of an image the GE drew in the
// current frame, which the Vita holds in HD). At each flip the PSP GE copies all requested pages (mirFlip in
// NativeLink) alternating two mirrors: even copies to MIRROR and odd ones to MIRROR2. Each list is read with the last
// copy finished when it starts (mir_u, without waiting for it: if the game queued the list before the flip, the right
// copy is the previous one) and notes it in the mailbox (SB[0x48]) so the GE does not overwrite it. A page is usable in
// a mirror if its last copy there has finished (mirCheck).
#define MIRROR 0x0A200000u
#define MIRROR2 0x0AC00000u
// last copy of page p in mirror b (written by NativeLink)
#define CPY(b, p) SB[((b) ? 0x8400 : 0x200) + (p)]
// Bitmaps of 512 pages (2 MB): requested as list, as data, images drawn in this frame (regFlip empties it) and the
// displayed image
// (want_fb, requested by record.c for dumps)
static uint32_t mir_on, want_l[16], want_d[16], rt_pg[16], want_fb[16];
static volatile uint32_t mir_ok[2][16], mir_reset;           // pages with a valid copy in each mirror
// copy the current list is read with (0: none yet)
static volatile uint32_t mir_u;
static struct { uint32_t chk, fresh, ch_l, ch_d, us, nlists, prior; } MS;  // mirror statistics
// Marks page p valid in mirror b if its last copy there is finished as of copy u
static int mirCheck(uint32_t b, uint32_t p, uint32_t u) {
  uint32_t sq = mir_on ? CPY(b, p) : 0;
  if (!sq || (int)(u - sq) < 0) return 0;
  __sync_fetch_and_or(&mir_ok[b][p >> 5], 1u << (p & 31));
  return 1;
}
// Readable address for a: RAM as is; VRAM, in the mirror of copy mir_u if that page has a valid copy.
// 0: cannot be read
static inline uint32_t vtr(uint32_t a) {
  if (!isVram(a)) return okRam(a) ? a : 0;
  uint32_t o = a & 0x1FFFFF, p = o >> 12, u = mir_u, b = u & 1;
  return mir_ok[b][p >> 5] >> (p & 31) & 1 || mirCheck(b, p, u) ? (b ? MIRROR2 : MIRROR) + o : 0;
}
// Requests VRAM pages [a, a + n) from the mirror as list or as data (render targets are not requested as data)
static void want(uint32_t a, uint32_t n, int list) {
  if (!mir_on) return;
  uint32_t o = a & 0x1FFFFF, p = o >> 12, e = (o + (n ? n : 1) - 1) >> 12;
  for (; p <= e && p < 512; p++) {
    uint32_t w = p >> 5, b = 1u << (p & 31);
    if (list) { if (!(want_l[w] & b)) { want_l[w] |= b; SB[0x20 + w] = want_l[w]; } }
    else if (!(rt_pg[w] & b) && !(want_d[w] & b)) { want_d[w] |= b; SB[0x30 + w] = want_d[w] | want_fb[w]; }
  }
}
// Marks VRAM [a, a + n) as render target: not requested as data (and the request is dropped if already made)
static void markRt(uint32_t a, uint32_t n) {
  uint32_t o = a & 0x1FFFFF, p = o >> 12, e = (o + n - 1) >> 12;
  for (; p <= e && p < 512; p++) {
    uint32_t w = p >> 5, b = 1u << (p & 31);
    rt_pg[w] |= b;
    if (want_d[w] & b) { want_d[w] &= ~b; if (mir_on) SB[0x30 + w] = want_d[w] | want_fb[w]; }
  }
}

// Thread read buffer: the last n bytes read from address a; own reads of PSP RAM,
// hits lines taken from the shared cache
typedef struct { uint32_t buf[64] __attribute__((aligned(16))); uint32_t a, n, own, hits; } Rd;
static Rd R0, RR[2];  // capturer and readers
static uint32_t tm_memo, tm_apply;  // us of the frame in memoTry (all) and in applying what was saved (extreme level)
static uint32_t runClk(SceUID t);
static SceUID cap_uid;
// Cache shared by readers and capturer: CL_N 64-byte lines (cl_data) each with a tag (cl_tag:
// RAM line and list sequence number; CL_BUSY while a thread writes it)
#define CL_N 8192     // 64-byte lines in the shared cache (512 KB)
#define CL_BUSY 0xFFFFFFFEu
static uint32_t *cl_data;
static volatile uint32_t *cl_tag;
static volatile uint32_t pub_ord, pub_pc, pub_base, pub_stall;  // the list the capturer is walking (pubList)
static inline uint32_t clTag(uint32_t b, uint32_t ord) { return ((b - 0x08000000u) >> 6) << 12 | (ord & 0xFFF); }
static inline uint32_t clSlot(uint32_t b) { uint32_t i = (b - 0x08000000u) >> 6; return (i ^ (i >> 13)) & (CL_N - 1); }
// Line b can be shared: it is in RAM and before the stall or more than 256 KB from it (what follows may not
// be written yet)
static inline int clOk(uint32_t b) {
  uint32_t st = pub_stall;
  return cl_data && b >= 0x08000000u && (!st || b + 64 <= st || b >= st + 0x40000);
}
// Copies to dst line b of list ord if it is in the shared cache (1) or not (0). Lock-free: rereads the tag
// after copying
static int clGet(uint32_t b, uint32_t ord, uint32_t *dst) {
  if (!clOk(b)) return 0;
  uint32_t i = clSlot(b), t = clTag(b, ord);
  if (cl_tag[i] != t) return 0;
  __sync_synchronize();
  for (int k = 0; k < 16; k++) dst[k] = cl_data[i * 16 + k];
  __sync_synchronize();
  return cl_tag[i] == t;  // if another thread replaced it during the copy, it is invalid
}
// Stores line b of list ord in the shared cache; does nothing if another thread is writing that slot
static void clPut(uint32_t b, uint32_t ord, const uint32_t *src) {
  if (!clOk(b)) return;
  uint32_t i = clSlot(b), old = cl_tag[i];
  // another thread is writing it
  if (old == CL_BUSY || !__sync_bool_compare_and_swap(&cl_tag[i], old, CL_BUSY)) return;
  for (int k = 0; k < 16; k++) cl_data[i * 16 + k] = src[k];
  __sync_synchronize();
  cl_tag[i] = clTag(b, ord);
}
// Copies 64 bytes of PSP RAM (PSP address b) to dst with two NEON loads
static void neon64(uint32_t *dst, uint32_t b) {
  const void *src = (const void *)PSP(b);
  asm volatile("vld1.32 {d16-d19}, [%1]!\n\tvld1.32 {d20-d23}, [%1]\n\tvst1.32 {d16-d19}, [%0]!\n\tvst1.32 {d20-d23}, [%0]"
               : "+r"(dst), "+r"(src) : : "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "memory");
}
// Copies 256 bytes of PSP RAM to dst: the 8 NEON loads go before any store so the waits overlap
static void neon256(uint32_t *dst, uint32_t b) {
  const void *src = (const void *)PSP(b);
  asm volatile("vld1.32 {d0-d3}, [%1]!\n\tvld1.32 {d4-d7}, [%1]!\n\tvld1.32 {d8-d11}, [%1]!\n\tvld1.32 {d12-d15}, [%1]!\n\t"
               "vld1.32 {d16-d19}, [%1]!\n\tvld1.32 {d20-d23}, [%1]!\n\tvld1.32 {d24-d27}, [%1]!\n\tvld1.32 {d28-d31}, [%1]\n\t"
               "vst1.32 {d0-d3}, [%0]!\n\tvst1.32 {d4-d7}, [%0]!\n\tvst1.32 {d8-d11}, [%0]!\n\tvst1.32 {d12-d15}, [%0]!\n\t"
               "vst1.32 {d16-d19}, [%0]!\n\tvst1.32 {d20-d23}, [%0]!\n\tvst1.32 {d24-d27}, [%0]!\n\tvst1.32 {d28-d31}, [%0]"
               : "+r"(dst), "+r"(src) : : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "d10", "d11", "d12",
                 "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d25", "d26", "d27",
                 "d28", "d29", "d30", "d31", "memory");
}
// Reads word a of list ord with buffer r: from the buffer if present; otherwise fetches its line from the shared cache
// or reads 256 bytes of PSP RAM (64 at the end of a range) and shares them. Leaves the line in r->buf
static uint32_t rdc(Rd *r, uint32_t a, uint32_t ord) {
  if (a - r->a < r->n) return r->buf[(a - r->a) >> 2];
  uint32_t b = a & ~63u, e = ramEnd(a);
  if (b + 64 > e) return *(volatile uint32_t *)PSP(a);  // (end of a range)
  // 1 in 8 shared cache lines, timed
  uint32_t th = EXTREME && r == &R0 && !(r->hits & 7) ? now() : 0;
  if (clGet(b, ord, r->buf)) { r->a = b; r->n = 64; r->hits++; if (th) { acc.hit_us += now() - th; acc.hit_n++; } return r->buf[(a - b) >> 2]; }
  uint32_t n = b + 256 <= e ? 256 : 64;
  // 1 in 16 capturer reads, timed
  uint32_t t = EXTREME && r == &R0 && !(r->own & 15) ? now() : 0;
  if (n == 256) neon256(r->buf, b); else neon64(r->buf, b);
  if (t) { acc.rd_us += now() - t; acc.rd_n++; }
  r->a = b; r->n = n; r->own++;
  for (uint32_t k = 0; k < n; k += 64) clPut(b + k, ord, r->buf + k / 4);
  return r->buf[(a - b) >> 2];
}

// --- GE state ---
// Copy of the state the PSP GE would have: last value of each command (reg), the 5 matrices with their load index
// (mtx, mi), vertex and index addresses, and which class of state changed since the last draw (dirty)
typedef struct Memo Memo;
static Memo *rec;  // sublist being recorded (sublist memory)
static void recDraw(int idx, int tex), recTex(uint32_t ta), recRead(uint32_t c), recVert(uint32_t a, uint32_t n);
static uint32_t reg[256], mtx[5][96], mi[5];
static uint8_t cls[256];  // state class of each command (C_*; 0: not state)
enum { C_TEX = 1, C_MTX = 2, C_RT = 4, C_OTHER = 8, C_TL = 16 };  // C_TL: TEXLEVEL (0xC8), a separate "other"
static uint32_t dirty, vaddr, iaddr, vset, last_prim = 99, last_vend, last_fb;
// Textures seen in the frame (hash table: tgen == gen marks a slot used in this frame); gen: frame number
static uint32_t tset[2048], tgen[2048], gen = 1, ntex, last_clut[2];  // (gen starts at 1: tgen 0 = empty)
// Fills cls: which commands are texture state, render target state or "other" (those that break a draw batch)
static void initCls(void) {
  for (int c = 0x15; c < 256; c++) cls[c] = C_OTHER;
  for (int c = 0xA0; c <= 0xB1; c++) cls[c] = C_TEX;
  for (int c = 0xB8; c <= 0xBF; c++) cls[c] = C_TEX;
  cls[0xC2] = cls[0xC3] = cls[0xC5] = C_TEX;
  cls[0x9C] = cls[0x9D] = cls[0x9E] = cls[0x9F] = cls[0xD2] = C_RT;
  cls[0x12] = C_OTHER;  // vertex type
  static const uint8_t no[] = { 0x2A, 0x2B, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0xB2, 0xB3, 0xB4, 0xB5, 0xC4,
                                0xCB, 0xCC, 0xEA, 0xEB, 0xEC, 0xEE };
  for (unsigned i = 0; i < sizeof no; i++) cls[no[i]] = 0;
  cls[0xC8] = C_TL;
}
// Notes texture a in the frame table: 1 if new in this frame (limit of 1500 per frame)
static int texAdd(uint32_t a) {
  if (ntex >= 1500) return 0;
  uint32_t i = ((a >> 4) * 2654435761u) >> 21;
  while (tgen[i] == gen) { if (tset[i] == a) return 0; i = (i + 1) & 2047; }
  tgen[i] = gen; tset[i] = a; ntex++;
  return 1;
}
// Bytes per vertex of format vt (GE rules, as in PPSSPP): each component aligned to its size, the total to the
// largest, times the morph count
static uint32_t vsize(uint32_t vt) {
  static const uint8_t s4[4] = { 0, 1, 2, 4 }, col[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
  uint32_t sz = 0, al = 1, s;
#define ADD(n, size) s = (size); if (s) { sz = (sz + s - 1) & ~(s - 1); sz += (n) * s; if (s > al) al = s; }
  ADD(((vt >> 14) & 7) + 1, s4[(vt >> 9) & 3]);  // weights
  ADD(2, s4[vt & 3]);                             // texture coordinates
  ADD(1, col[(vt >> 2) & 7]);                     // color
  ADD(3, s4[(vt >> 5) & 3]);                      // normal
  ADD(3, s4[(vt >> 7) & 3]);                      // position
#undef ADD
  return ((sz + al - 1) & ~(al - 1)) * (((vt >> 18) & 7) + 1);
}
#define TEXADDR() ((reg[0xA0] & 0xFFFFF0) | ((reg[0xA8] << 8) & 0x0F000000))
#define CLUTADDR() ((reg[0xB0] & 0xFFFFF0) | ((reg[0xB1] << 8) & 0x0F000000))

#ifdef NP_DEV
// --- Data lifetime (extreme level) ---
// The Vita draws 1 frame after reading the list, and its GPU reads vertices, textures and palettes directly from PSP
// RAM. If the game rewrites them first, it draws something else. Sample: 1 in 64 draws (their vertices or indices),
// 1 in 8 new textures of the frame and 1 in 16 palette loads (up to 8 vertex samples and 4 of the rest per frame,
// RAM only). The capturer hashes the first 256 bytes when noting a sample and again at the next 2
// flips (ltFlip), and counts how many changed and whether a known game CPU write (T4) covered them: a range, or
// "flush the whole cache". Sublists applied by the memory count with the vertices of their first draw (sv_a).
// Checked by the capturer and not the mirror thread: that one gets little time and takes it from the core 0 reader.
// A sample: [a, a + n), type, frame (g), hash and time when noted, checks done (st) and whether it changed at flip 1
typedef struct { uint32_t a, n, kind, g, h0, t0, st, ch1; } Smp;
#define SMP_N 1024  // sample ring [smp_r, smp_w)
static Smp smp[SMP_N];
static volatile uint32_t smp_w, smp_r;
static uint32_t smp_k[4], smp_f[4];
// rate (masks: 1 in 64, 64, 8, 16) and per-frame limit of each sample type
static const uint8_t smp_rate[4] = { 63, 63, 7, 15 }, smp_max[4] = { 8, 4, 4, 4 };
static uint32_t smpHash(uint32_t a, uint32_t n);
static void ltFlip(void);
// Notes [a, a + n) (kind: 0 vertices, 1 indices, 2 texture, 3 palette) as a sample if the rate and frame limit
// allow it; extreme level and RAM only (no VRAM)
static void smpAdd(uint32_t a, uint32_t n, uint32_t kind) {
  if (level < 2 || (++smp_k[kind] & smp_rate[kind]) || !n || smp_f[kind] >= smp_max[kind] || isVram(a) ||
      ramEnd(a) < a + (n > 256 ? 256 : n) || smp_w - smp_r >= SMP_N) return;
  Smp *x = &smp[smp_w & (SMP_N - 1)];
  x->a = a; x->n = n > 256 ? 256 : n; x->kind = kind; x->g = gen; x->t0 = now(); x->h0 = smpHash(x->a, x->n); x->st = 0;
  smp_w++; smp_f[kind]++;
}
#define CW_N 256  // latest known game CPU writes (T4): [lo, hi) and Vita time when the event was read
static struct { uint32_t lo, hi, t; } cw[CW_N];
static volatile uint32_t cw_n, cw_all_t;  // (cw_all_t: last "flush the whole cache")
// data lifetime (accumulated)
static struct { volatile uint32_t n[4], ch1[4], ch2[4], warn[4], wall[4], ms1, ms2; } LT;
#else
#define smpAdd(a, n, kind) ((void)sizeof(n))
#endif

static int rg_chg = 1;  // some state register changed since the last recorded draw
static int reg_on, reg_big;  // frame record (switch draw); last frame had more draws than a record holds
// Until when (Vita time) each frame carries the exact PSP image: set by dump.c for an
// L+R+SELECT dump and used by record.c
volatile uint32_t nd_exact_until;
DEV(volatile uint32_t nd_dump_mark;)  // a GE NOP carrying 0x4E5044 asks for a dump (NPTEST); dumpTick takes it
static void regDraw(uint32_t n, uint32_t type, uint32_t vt, uint32_t idx), regClut(uint32_t d), regMemo(uint32_t draws);
// PRIM (command 0x04, d: count and primitive type): records the draw, counts its class and the state changes since
// the previous one, advances vaddr/iaddr, notes new textures, requests VRAM textures from the mirror and marks the
// render target so it is not requested as data
static void draw(uint32_t d) {
  uint32_t n = d & 0xFFFF, type = (d >> 16) & 7, vt = reg[0x12], idx = (vt >> 11) & 3;
  if (!n) { acc.dempty++; return; }
  if (reg_on) regDraw(n, type, vt, idx);  // frame record
  acc.draws++; f_draws++; acc.prim[type]++; acc.verts += n;
  int clear = reg[0xD3] & 1, tex = (reg[0x1E] & 1) && !clear;
  if (rec) recDraw(idx, tex);  // (before vaddr/iaddr change)
  if (rec && tex) recTex(TEXADDR());
  if (clear) acc.dclear++;
  else if (vt >> 23 & 1) acc.dthr++;
  else acc.d3d++;
  uint32_t fb = (reg[0x9C] & 0xFFFFF0) | ((reg[0x9D] << 8) & 0xFF000000);
  if (fb != last_fb) {
    acc.rt_switch++; last_fb = fb;
    uint32_t h = ((reg[0xD6] >> 10) & 0x3FF) + 1, w = reg[0x9D] & 0x7FF;  // height: to the bottom edge of the scissor
    markRt(fb, w * h * ((reg[0xD2] & 3) == 3 ? 4 : 2));
    if (reg[0x23] & 1) markRt(reg[0x9E] & 0xFFFFF0, (reg[0x9F] & 0x7FF) * h * 2);  // z (with z test)
  }
  if (isVram(vaddr)) acc.dvert_vram++;
  static uint32_t vs_vt = ~0u, vs_sz;  // vertex size rarely changes: it is remembered
  if (vt != vs_vt) { vs_vt = vt; vs_sz = vsize(vt); }
  uint32_t vs = vs_sz, vstart = vaddr;
  if (EXTREME) { if (idx) smpAdd(iaddr, n * (idx == 1 ? 1 : idx == 2 ? 2 : 4), 1); else smpAdd(vaddr, n * vs, 0); }
  if (rec && !idx) recVert(vstart, n * vs);
  if (idx) { acc.ibytes += n * (idx == 1 ? 1 : idx == 2 ? 2 : 4); iaddr += n * (idx == 1 ? 1 : idx == 2 ? 2 : 4); }
  else { acc.vbytes += n * vs; vaddr += n * vs; }
  if (tex) {
    uint32_t ta = TEXADDR();
    if (texAdd(ta)) {
      acc.textures++; if (isVram(ta)) acc.tex_vram++;
      static const uint8_t bp[16] = { 16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8, 8, 8, 8, 8, 8 };
      uint32_t w = reg[0xA8] & 0x7FF, h = 1u << ((reg[0xB8] >> 8) & 0xF);
      if (!w) w = 1u << (reg[0xB8] & 0xF);
      if (EXTREME) smpAdd(ta, w * h * bp[reg[0xC3] & 0xF] / 8, 2);
    }
    if (isVram(ta)) {
      acc.dtex_vram++;
      uint32_t p = (ta & 0x1FFFFF) >> 12;
      if (rt_pg[p >> 5] >> (p & 31) & 1) acc.tex_rt++;
      // if not a render target: requested from the mirror (level 0 bytes per the bpp of each GE format)
      else if (dirty & C_TEX || !((want_d[p >> 5] >> (p & 31)) & 1)) {
        static const uint8_t bpp[16] = { 16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8, 8, 8, 8, 8, 8 };
        uint32_t w = reg[0xA8] & 0x7FF, h = 1u << ((reg[0xB8] >> 8) & 0xF);
        if (!w) w = 1u << (reg[0xB8] & 0xF);
        want(ta, w * h * bpp[reg[0xC3] & 0xF] / 8, 0);
      }
    }
    if (dirty & C_TEX) acc.texnew++;
  }
  if (dirty & C_TEX) acc.ch_tex++;
  if (dirty & C_MTX) acc.ch_mtx++;
  if (dirty & C_RT) acc.ch_rt++;
  if (dirty & (C_OTHER | C_TL)) acc.ch_other++;
  if (dirty == C_TL) acc.only_tl++;
  if (!dirty) acc.ch_none++;
  // Batch: consecutive draws that could be drawn as one (same state and primitive type). "Contiguous": also
  // the vertices continue where the previous one ended (no new indices)
  int same = !dirty && type == last_prim;
  if (!same) acc.batches++;
  if ((dirty & ~C_TL) || type != last_prim) acc.batches_notl++;  // if TEXLEVEL were per draw (does not break the batch)
  if (!same || idx || (vset && vstart != last_vend)) acc.batches_cont++;
  last_prim = type; last_vend = vaddr; dirty = vset = 0;
}

#include "record.c"  // frame record: uses the code above

// --- Queued lists (like the GE: one at a time, in order) ---
// A list queued by the game: position (pc, CALL stack stk/sp, BASE/OFFSET), how far it is written (stall),
// commands walked (n), times (t_enq/t_stall PSP, t_start Vita), counters at start (own0, hit0,
// memo0, cpu0) and the list lifetime sample (top-level lo/hi, subs)
typedef struct { uint32_t pc, stall, qid, ord, off, sp, stk[32][2], prev, n, ncall, t_enq, t_stall, t_start, work, own0, hit0, memo0, ahead, lo, hi, nsub, subs[16], cpu0; } List;
#define LIST_MAX 400000  // commands: more than that is a list the game already rewrote (the Vita was behind)
static List q[64];
static uint32_t qh, qt;  // the first unfinished one and the next free one
// Absolute address of a jump/CALL/VADDR/IADDR: BASE (reg 0x10) + d + list OFFSET
static inline uint32_t rel(List *l, uint32_t d) { return ((((reg[0x10] & 0x0F0000) << 8) | d) + l->off) & 0x0FFFFFFF; }

static void clog(const char *fmt, ...);
DEV(static void rput(int k, const char *fmt, ...);)  // 0: nativedraw_frames.log, 1: nativedraw_late.log
static uint32_t no_capture, gp_flags;           // switches (gp_flags goes to NativeLink in SB[0x4A])
// Copy of the GE state (for the sublist memory check)
typedef struct { uint32_t reg[256], mtx[5][96], mi[5], v[8]; } GeState;
static GeState g_tmp;
// Saves to g / restores from g the GE state (registers, matrices and addresses)
static void stSave(GeState *g) {
  sceClibMemcpy(g->reg, reg, sizeof reg); sceClibMemcpy(g->mtx, mtx, sizeof mtx); sceClibMemcpy(g->mi, mi, sizeof mi);
  uint32_t v[8] = { dirty, vaddr, iaddr, vset, last_prim, last_vend, last_fb, 0 };
  sceClibMemcpy(g->v, v, sizeof v);
}
static void stLoad(GeState *g) {
  rg_chg = 1; sceClibMemcpy(reg, g->reg, sizeof reg); sceClibMemcpy(mtx, g->mtx, sizeof mtx); sceClibMemcpy(mi, g->mi, sizeof mi);
  dirty = g->v[0]; vaddr = g->v[1]; iaddr = g->v[2]; vset = g->v[3]; last_prim = g->v[4]; last_vend = g->v[5];
  last_fb = g->v[6];
}

// --- Sublist memory ---
// Many games repeat identical sublists between frames and the expense is interpreting command by command, not reading.
// When the top level calls a sublist (CALL to RAM), it is looked up by (address, BASE, entry OFFSET). If
// already walked, its memory lines (64 bytes; nested sublists included) are compared with the copy saved in
// the pool: if equal, its effects are applied (registers and matrices at exit, vertex/index
// addresses, counters, textures) without interpreting it. If missing or changed, it is walked and recorded.
// Approximate (statistics only): "changes" and first-draw batches depend on entry state; chg[] is not added.
// Check: 1 in 32 applicable sublists is walked anyway and the state it leaves is compared with what was
// saved (MISMATCHES in the log = memory error; memo_check: all; no_memo: off).
// Maximum sizes; capStart halves them while memory is short. With less, the pool empties repeatedly
// in games with many sublists and everything is recorded again.
// saved sublists, 64-byte pool lines (2 MB)
static uint32_t MEMO_N = 2048, POOL_N = 32768, memo_sh = 21;
#define MR 32         // memory ranges per sublist
#define MRG 64        // registers it may leave changed
#define SW (sizeof(Stats) / 4 - 256)  // Stats words that are added (those before chg[256], which goes last)
#define SN 48         // nonzero statistics a sublist may add (more: not saved)
// VDEP/IDEP: draws with the entry vertices/indices
enum { M_DRAW = 1, M_VABS = 2, M_IABS = 4, M_CLUT = 8, M_VDEP = 16, M_IDEP = 32 };
static const uint8_t moff[5] = { 0, 12, 24, 40, 52 }, msz[5] = { 12, 12, 16, 12, 96 };  // each matrix in Memo.mtx
// A saved sublist: key (t, base, off), its lines in the pool, what it adds, the state it leaves and the entry
// state it depends on. Valid if pgen == pool_gen (emptying the pool invalidates them all)
struct Memo {
  // state: 0 recording, 1 ready, 2 cannot be saved (retry in 64 frames)
  uint32_t t, base, off, pgen, state, fail_gen;
  uint32_t nr, ra[MR], rn[MR], rp[MR];            // ranges: first line, count, pool position
  // what it adds to the statistics: nonzero values only (index, value)
  uint32_t cmds, draws, sn, sv[SN]; uint8_t si[SN];
  uint32_t nreg, rv[MRG]; uint8_t rc[MRG];         // registers at exit
  uint32_t mmask, mi[5], mtx[148];                 // matrices touched
  uint32_t flags, vaddr, iaddr, vset, dirty, last_prim, last_fb, clut[2], ntex, tex[32];
  // entry state used (registers read before being written)
  uint32_t v_in, i_in, ndep, dv[16]; uint8_t dc[16];
  uint32_t sv_a, sv_n, sv_rel;  // vertices of the first non-indexed draw: for the data lifetime sample
  uint32_t streak;               // consecutive times applied without changes (recBegin resets it)
  // (last: recBegin does not clear them) version (raised on rerecording) and reader check (vokOf)
  volatile uint32_t ver, vok;
};
static Memo *memo;
// Pool: saved 64-byte lines (pool_top: next free; pool_gen rises when emptied)
static uint32_t *pool, pool_top, pool_gen = 1, pool_reset, memo_on, memo_chk;
static Memo *chk;  // sublist being checked
// Recording in progress: last saved line, entry vaddr/iaddr/dirty, registers written and read before
// being written (bitmaps) and acc at start (to compute what it adds)
static uint32_t rec_last, rec_v0, rec_i0, rec_d0, rec_touch[8], rec_dep[8], rec_acc0[SW];
static GeState g_memo;  // state the sublist under check should leave

// Each (address, BASE, OFFSET) may have several versions (one per entry state), in 8 consecutive slots
static inline uint32_t memoHash(uint32_t t, uint32_t base, uint32_t off) { return ((t >> 2) ^ (base * 0x9E37) ^ (off >> 4)) * 2654435761u >> memo_sh; }
static Memo *victim;  // changed version: recorded over
// Slot to record sublist (t, base, off): the changed version (victim), or a free one of its 8 slots, or one
// of them chosen by frame
static Memo *memoSlot(uint32_t t, uint32_t base, uint32_t off) {
  if (victim) { Memo *v = victim; victim = NULL; return v; }
  uint32_t i = memoHash(t, base, off);
  for (int k = 0; k < 8; k++, i = (i + 1) & (MEMO_N - 1)) if (memo[i].pgen != pool_gen) return &memo[i];
  return &memo[(memoHash(t, base, off) + (gen & 7)) & (MEMO_N - 1)];  // (full: one is replaced)
}
// 1 if the entry state e uses (vertices, indices, read registers) equals the current one
static int depsOk(Memo *e) {
  if ((e->flags & M_VDEP && vaddr != e->v_in) || (e->flags & M_IDEP && iaddr != e->i_in)) return 0;
  for (uint32_t i = 0; i < e->ndep; i++) if (reg[e->dc[i]] != e->dv[i]) return 0;
  return 1;
}
// Aborts the recording in progress: that sublist stays "cannot be saved" for 64 frames
static void recAbort(void) { if (rec) { rec->state = 2; rec->fail_gen = gen; rec = NULL; } }
// Starts recording sublist t: finds a slot (empties the pool if nearly full or requested) and notes the entry
// state and current counters
static void recBegin(List *l, uint32_t t) {
  if (pool_reset || pool_top > POOL_N - 64) { pool_gen++; pool_top = 0; pool_reset = 0; acc.memo_reset++; }
  Memo *e = memoSlot(t, reg[0x10], l->off);
  e->ver++; __sync_synchronize();  // version first: a reader checking it discards it
  sceClibMemset(e, 0, (char *)&e->ver - (char *)e);
  e->t = t; e->base = reg[0x10]; e->off = l->off; e->pgen = pool_gen; e->v_in = vaddr; e->i_in = iaddr;
  rec = e; rec_last = ~0u; rec_v0 = vaddr; rec_i0 = iaddr; rec_d0 = dirty;
  sceClibMemset(rec_touch, 0, sizeof rec_touch); sceClibMemset(rec_dep, 0, sizeof rec_dep);
  sceClibMemcpy(rec_acc0, &acc, sizeof rec_acc0);
}
// Where saved line b is read now: VRAM lines are saved with their VRAM address and read in the
// frame's mirror (0: no copy)
static inline uint32_t mline(uint32_t b) { return isVram(b) ? vtr(b) : b; }
// New line of the sublist being recorded (b; rb where it was read, held in R0): copies it to the pool and appends it
// to the last range if contiguous, or opens another. Out of room: aborts the recording
static void recLine(uint32_t b, uint32_t rb) {
  Memo *e = rec;
  rec_last = b;
  // already saved (sublist called twice)
  for (uint32_t i = 0; i < e->nr; i++) if (b - e->ra[i] < e->rn[i] * 64) return;
  if (rb < R0.a || rb + 64 > R0.a + R0.n || pool_top >= POOL_N) { if (pool_top >= POOL_N) pool_reset = 1; recAbort(); return; }
  sceClibMemcpy(pool + pool_top * 16, R0.buf + (rb - R0.a) / 4, 64);
  uint32_t n = e->nr - 1;
  if (e->nr && e->ra[n] + e->rn[n] * 64 == b && e->rp[n] + e->rn[n] == pool_top) e->rn[n]++;
  else if (e->nr < MR) { e->ra[e->nr] = b; e->rn[e->nr] = 1; e->rp[e->nr] = pool_top; e->nr++; }
  else { recAbort(); return; }
  pool_top++;
}
// 1 if any range of e is less than 256 KB past stall st (may not be written yet)
static int nearStall(Memo *e, uint32_t st) {
  if (!st) return 0;
  for (uint32_t i = 0; i < e->nr; i++) if (!(e->ra[i] + e->rn[i] * 64 <= st || e->ra[i] >= st + 0x40000)) return 1;
  return 0;
}
// Finishes recording (RET to the top level): saves what it added to acc (nonzero only), the registers and
// matrices it left, and vaddr/iaddr (absolute or relative to entry); marks it ready. If it does not fit, it stays
// "cannot be saved"
static void recEnd(List *l) {
  Memo *e = rec;
  rec = NULL;
  if (nearStall(e, l->stall)) { e->state = 2; e->fail_gen = gen; return; }
  static Stats ds;  // (only the first SW words are used)
  uint32_t *a = (uint32_t *)&acc, *td = (uint32_t *)&ds;
  for (int i = 0; i < SW; i++) td[i] = a[i] - rec_acc0[i];
  Stats *d = &ds;
  d->textures = d->tex_vram = 0;  // counted again with texAdd when applied
  // what the thread counts, not the walk (events, times, frames): recording can span several passes
  d->t0 = d->frames = d->lists = d->cmds_max = d->draws_max = d->us = d->us_max = d->own = d->hits = d->rown[0] = d->rown[1] = 0;
  d->events = d->syncs = d->syncs_waited = d->sync_wait_us = d->behind = d->resyncs = d->lsyncs = d->flips_behind = 0;
  d->sleep_us = d->cpu_cap = d->cpu_rdr[0] = d->cpu_rdr[1] = 0;
  for (int k = 0; k < 4; k++) d->cpu_core[k] = 0;
  d->memo_hit = d->memo_miss = d->memo_fail = d->memo_no = d->memo_cmds = d->memo_lines = d->memo_bad = d->memo_chk = d->memo_reset = d->memo_dep = d->memo_us = 0;
  d->memo_pre = d->lv_lines = d->lv_ok = d->lv_bad = d->lv_late = 0;
  d->ge_us = d->ge_runs = d->ge_max = d->ge_stall = d->ge_n = d->sy_n = d->sy_us = d->sy_wait = d->sy_busy = 0;
  d->dc_n = d->dc_kb = d->dm_n = d->dm_kb = d->cpu_drop = d->cpu_vram = d->dc_all = 0;
  d->hit_us = d->hit_n = 0;
  d->pv_ok = d->pv_bad = d->pv_late = d->pv_plan = 0;
  d->pf_n = d->pf_us = d->pf_gate = d->pf_gate_n = d->pf_nowait = d->pf_sync = d->pf_spin = d->pf_mir = d->pf_pg = d->pf_post = d->pf_late = d->pf_late_us = d->rd_us = d->rd_n = d->eg_n = d->eg_us = 0;
  e->cmds = d->cmds; e->draws = d->draws;
  for (int i = 0; i < SW; i++)
    if (td[i]) {
      if (e->sn >= SN) { static int nl; if (nl++ < 3) clog("memory: sublist %08x adds more than %d stats: not saved\n", e->t, SN); e->state = 2; e->fail_gen = gen; return; }
      e->si[e->sn] = i; e->sv[e->sn++] = td[i];
    }
  for (int c = 0; c < 256; c++)
    if (rec_touch[c >> 5] >> (c & 31) & 1) {
      if (e->nreg >= MRG) { e->state = 2; e->fail_gen = gen; return; }
      e->rc[e->nreg] = c; e->rv[e->nreg++] = reg[c];
    }
  for (int m = 0; m < 5; m++) if (e->mmask >> m & 1) { e->mi[m] = mi[m]; sceClibMemcpy(e->mtx + moff[m], mtx[m], msz[m] * 4); }
  e->vaddr = e->flags & M_VABS ? vaddr : vaddr - rec_v0;
  e->iaddr = e->flags & M_IABS ? iaddr : iaddr - rec_i0;
  e->vset = vset; e->dirty = e->flags & M_DRAW ? dirty : dirty & ~rec_d0; e->last_prim = last_prim; e->last_fb = last_fb;
  e->clut[0] = last_clut[0]; e->clut[1] = last_clut[1];
  __sync_synchronize();  // (readers read it from another core: everything is written before marking it ready)
  e->state = 1;
}
// Leaves the GE state as if sublist e had been walked
static void memoApply(Memo *e) {
  rg_chg = 1;
  for (uint32_t i = 0; i < e->nreg; i++) reg[e->rc[i]] = e->rv[i];
  for (int m = 0; m < 5; m++) if (e->mmask >> m & 1) { mi[m] = e->mi[m]; sceClibMemcpy(mtx[m], e->mtx + moff[m], msz[m] * 4); }
  vaddr = e->flags & M_VABS ? e->vaddr : vaddr + e->vaddr;
  iaddr = e->flags & M_IABS ? e->iaddr : iaddr + e->iaddr;
  if (e->flags & (M_DRAW | M_VABS)) vset = e->vset;
  dirty = e->flags & M_DRAW ? e->dirty : dirty | e->dirty;
  if (e->flags & M_DRAW) { last_prim = e->last_prim; last_fb = e->last_fb; last_vend = vaddr; }
  if (e->flags & M_CLUT) { last_clut[0] = e->clut[0]; last_clut[1] = e->clut[1]; }
}
static inline int lineEq(const uint32_t *a, const uint32_t *b) {  // 64 equal bytes (NEON)
  uint32x4_t x = veorq_u32(vld1q_u32(a), vld1q_u32(b));
  x = vorrq_u32(x, veorq_u32(vld1q_u32(a + 4), vld1q_u32(b + 4)));
  x = vorrq_u32(x, veorq_u32(vld1q_u32(a + 8), vld1q_u32(b + 8)));
  x = vorrq_u32(x, veorq_u32(vld1q_u32(a + 12), vld1q_u32(b + 12)));
  uint32x2_t y = vorr_u32(vget_low_u32(x), vget_high_u32(x));
  return (vget_lane_u32(y, 0) | vget_lane_u32(y, 1)) == 0;
}
// Check by the readers. Reading PSP RAM lines is the expense: the readers, on reaching a top-level CALL
// with saved sublists, compare all its lines with the pool themselves and leave in vok the list
// (sequence number) and version checked. The capturer, if vok matches, applies it without reading anything.
// If the capturer rerecords the sublist meanwhile (ver++), or the pool is emptied, the check is discarded.
static volatile uint32_t lv_lines[2], lv_ok[2], lv_bad[2], lv_late[2];  // (accumulated, per reader)
// Sharing of top-level CALLs (n-th of the list) among readers by pace: each takes the
// next free one. The capturer marks those it has reached: lagging readers skip them.
// claim[n] = list number << 2 | who (0 capturer, 1 and 2 readers).
#define CLM 2048
static volatile uint32_t claim[CLM];
// the capturer reached CALL n
static inline void claimSet(uint32_t n, uint32_t ord) { if (n < CLM) claim[n] = ord << 2; }
// 1: reader id takes CALL n of list ord (marks it with CAS); 0: another already took it
static int claimTake(uint32_t n, uint32_t ord, int id) {
  if (n >= CLM) return n % 2 == (uint32_t)id;
  uint32_t o = claim[n];
  // already taken (if by the capturer: too late)
  if (o >> 2 == (ord & 0x3FFFFFFF)) { if (!(o & 3)) lv_late[id]++; return 0; }
  return __sync_bool_compare_and_swap(&claim[n], o, ord << 2 | (id + 1));
}
static inline uint32_t vokOf(uint32_t ord, uint32_t ver) { return ord << 16 | (ver & 0xFFFF); }  // checked mark
// Compares saved lines of e with today's (reader id, buffer r): 1 equal, 0 different, -1 impossible (near
// the stall or a VRAM page without copy in the frame's mirror)
static int memoLines(int id, Rd *r, Memo *e, uint32_t ord) {
  uint32_t nr = e->nr, st = pub_stall;
  if (nr > MR) return -1;
  for (uint32_t j = 0; j < nr; j++) {
    uint32_t a = e->ra[j], n = e->rn[j], p = e->rp[j];
    // (near the stall: the capturer sees it)
    if (p + n > POOL_N || (st && !(a + n * 64 <= st || a >= st + 0x40000))) return -1;
    for (uint32_t m = 0; m < n; m++) {
      uint32_t b = mline(a + m * 64);
      if (!b || ramEnd(b) < b + 64) return -1;
      rdc(r, b, ord); lv_lines[id]++;
      if (!lineEq(r->buf + (b - r->a) / 4, pool + (p + m) * 16)) return 0;
    }
  }
  return 1;
}
// Checks e for list ord and leaves the mark (vok): 1 equal, 0 different, 2 already checked, -1 nothing
static int memoMark(int id, Rd *r, Memo *e, uint32_t ord) {
  uint32_t pg = pool_gen, v = e->ver;
  __sync_synchronize();
  if (e->pgen != pg || e->state != 1) return -1;
  if (e->vok == vokOf(ord, v)) return 2;
  int g = memoLines(id, r, e, ord);
  __sync_synchronize();
  // the capturer changed it, or it is another list now
  if (g < 0 || e->ver != v || pool_gen != pg || pub_ord != ord) return -1;
  e->vok = g ? vokOf(ord, v) : 0;
  return g;
}
// (reader) Checks all saved versions of sublist (t, base, off) for list ord; 1: one ended up
// checked (the reader need not walk it)
static int memoVerify(int id, Rd *r, uint32_t t, uint32_t base, uint32_t off, uint32_t ord) {
  int ok = 0;
  uint32_t i = memoHash(t, base, off);
  for (int k = 0; k < 8; k++, i = (i + 1) & (MEMO_N - 1)) {
    Memo *e = &memo[i];
    if (e->t != t || e->base != base || e->off != off) continue;
    int g = memoMark(id, r, e, ord);
    if (g > 0) { ok = 1; if (g == 1) lv_ok[id]++; } else if (!g) lv_bad[id]++;
  }
  return ok;
}
// Read-ahead check. The capturer notes in pv[pv_c] the sublists it applies (in order, only those that
// came out equal pv_min times in a row); when the next list starts that is the plan (pv_plan) and the readers
// check it before walking anything, taking the next free one (pv_i) FROM THE END: in order they would pace the
// capturer comparing what it is about to use. From the end they first check what the capturer needs
// last and stop when they meet it (pv_pos: what it already passed).
// Risk: if the game changes a sublist after the check and before the capturer arrives, the old one is used
// (the 1/32 check would show it as MISMATCHES). Switch no_lookahead.
#define PV_N 1024
static Memo *pv[2][PV_N], **pv_plan;
static uint32_t pv_n[2], pv_c, pv_min = 8, pv_on = 1;
static volatile uint32_t pv_pn, pv_i, pv_pos, pv_ok[2], pv_bad[2], pv_late[2];
// (reader) Checks the plan of list ord from the end until meeting the capturer or the list changing
static void pvRun(int id, Rd *r, uint32_t ord) {
  r->n = 0;
  for (;;) {
    uint32_t i = __sync_fetch_and_add(&pv_i, 1);
    if (i >= pv_pn || pub_ord != ord) return;
    uint32_t k = pv_pn - 1 - i;  // from the end
    if (k < pv_pos) { pv_late[id]++; return; }  // met the capturer: it already passed the rest
    int g = memoMark(id, r, pv_plan[k], ord);
    if (g == 1) pv_ok[id]++; else if (!g) pv_bad[id]++;
  }
}
// Top-level CALL to sublist t: looks for a saved version with the same entry state, compares its
// lines (or uses a reader's check) and if equal applies it: adds what was saved and leaves the exit state.
// 1 = applied (not walked), 0 = walk recording, -1 = walk without recording (or checking it, with chk)
static int memoTry(List *l, uint32_t t) {
  Memo *e = NULL;
  uint32_t base = reg[0x10], i = memoHash(t, base, l->off), seen = 0, no = 0;
  victim = NULL;
  for (int k = 0; k < 8 && !e; k++, i = (i + 1) & (MEMO_N - 1)) {
    Memo *m = &memo[i];
    if (m->pgen != pool_gen || m->t != t || m->base != base || m->off != l->off) continue;
    seen = 1;
    if (m->state == 2) { if (gen - m->fail_gen < 64) no = 1; else victim = m; }  // (past the deadline, retried there)
    else if (m->state == 1 && depsOk(m)) e = m;
  }
  if (!e) {
    if (no) { acc.memo_no++; return -1; }
    if (seen) acc.memo_dep++; else acc.memo_miss++;  // (seen: already there, with another entry state: another version)
    return 0;
  }
  if (nearStall(e, l->stall)) return -1;
  if (e->vok == vokOf(l->ord, e->ver)) { acc.memo_pre++; goto ok; }  // a reader already checked it in this list
  static uint32_t n8;
  uint32_t lines = 0, tm = ++n8 & 7 ? 0 : now();  // (comparison time: 1 in 8, x8)
#define TM() if (tm) acc.memo_us += (now() - tm) * 8
  for (uint32_t i = 0; i < e->nr; i++)
    for (uint32_t k = 0; k < e->rn[i]; k++) {
      uint32_t b = mline(e->ra[i] + k * 64), *p = pool + (e->rp[i] + k) * 16;
      if (!b || ramEnd(b) < b + 64) { acc.memo_fail++; victim = e; TM(); return 0; }
      rdc(&R0, b, l->ord);
      const uint32_t *s = R0.buf + (b - R0.a) / 4;
      lines++;
      if (!lineEq(s, p)) { acc.memo_lines += lines; acc.memo_fail++; victim = e; TM(); return 0; }
    }
  acc.memo_lines += lines;
  TM();
ok:;
  if (e->streak < 255) e->streak++;
  // plan of the next list
  if (pv_on && e->streak >= pv_min && pv_n[pv_c] < PV_N) { pv[pv_c][pv_n[pv_c]++] = e; pv_pos = pv_n[pv_c]; }
  static uint32_t nchk;
  // check (1 in 32, or all): what was saved is computed and it is walked anyway to compare
  if (memo_chk || (level && !(++nchk & 31))) {
    stSave(&g_tmp); memoApply(e); stSave(&g_memo); stLoad(&g_tmp);
    chk = e; acc.memo_chk++;
    return -1;
  }
  uint32_t ta = EXTREME ? now() : 0;
  if (e->sv_n) smpAdd(e->sv_rel ? vaddr + e->sv_a : e->sv_a, e->sv_n, 0);  // (before memoApply: entry vaddr)
  memoApply(e);
  if (reg_on) regMemo(e->draws);  // its draws skip the frame record: the frame stays incomplete
  for (uint32_t i = 0; i < e->ntex; i++) if (texAdd(e->tex[i])) { acc.textures++; if (isVram(e->tex[i])) acc.tex_vram++; else smpAdd(e->tex[i], 256, 2); }
  uint32_t *a = (uint32_t *)&acc;
  if (level) for (uint32_t i = 0; i < e->sn; i++) a[e->si[i]] += e->sv[i];
  else { acc.cmds += e->cmds; acc.draws += e->draws; }  // (minimum: only what appears in its line)
  f_cmds += e->cmds; f_draws += e->draws; l->n += e->cmds;
  acc.memo_hit++; acc.memo_cmds += e->cmds;
  if (ta) tm_apply += now() - ta;
  return 1;
}
// End of the chk check: the walk must leave the same state as saved (g_memo); otherwise MISMATCH to the log
static void chkEnd(void) {
  Memo *e = chk;
  chk = NULL;
  int bad = 0, c = 0;
  for (c = 0; c < 256; c++) if (reg[c] != g_memo.reg[c]) { bad = 1; break; }
  if (!bad && sceClibMemcmp(mtx, g_memo.mtx, sizeof mtx)) bad = 2;
  if (!bad && sceClibMemcmp(mi, g_memo.mi, sizeof mi)) bad = 3;
  if (!bad && (vaddr != g_memo.v[1] || iaddr != g_memo.v[2])) bad = 4;
  if (!bad) return;
  acc.memo_bad++;
  static int nlog;
  if (nlog++ < 10)
    clog("memory: sublist %08x (base %06x) MISMATCH when walked: %s %02x saved=%06x walked=%06x | vertices %08x/%08x\n",
         e->t, e->base, bad == 1 ? "register" : bad == 2 ? "matrix" : bad == 3 ? "matrix index" : "address", bad == 1 ? c : 0,
         bad == 1 ? g_memo.reg[c] : 0, bad == 1 ? reg[c] : 0, g_memo.v[1], vaddr);
}
// Register c read by the sublist being recorded: if not yet written inside it, it is entry state (up to 16)
static void recRead(uint32_t c) {
  if (rec_touch[c >> 5] >> (c & 31) & 1 || rec_dep[c >> 5] >> (c & 31) & 1) return;
  rec_dep[c >> 5] |= 1u << (c & 31);
  if (rec->ndep >= 16) { recAbort(); return; }
  rec->dc[rec->ndep] = c; rec->dv[rec->ndep++] = reg[c];
}
// Draw inside the sublist being recorded: depends on entry vertices/indices if it did not set them, and on the
// registers the draw uses (vertex type, clear, texture, render target)
static void recDraw(int idx, int tex) {
  rec->flags |= M_DRAW;
  if (!(rec->flags & M_VABS)) rec->flags |= M_VDEP;
  if (idx && !(rec->flags & M_IABS)) rec->flags |= M_IDEP;
  static const uint8_t r1[] = { 0x12, 0xD3, 0x1E, 0x9C, 0x9D }, r2[] = { 0xA0, 0xA8 };
  for (unsigned i = 0; i < sizeof r1 && rec; i++) recRead(r1[i]);
  for (unsigned i = 0; i < sizeof r2 && rec && tex; i++) recRead(r2[i]);
}
// Vertices of the first non-indexed draw of the sublist being recorded: data lifetime sample when applied
static void recVert(uint32_t a, uint32_t n) {
  if (rec->sv_n || !n) return;
  rec->sv_rel = !(rec->flags & M_VABS); rec->sv_a = rec->sv_rel ? a - rec_v0 : a; rec->sv_n = n;
}
// Texture used by the sublist being recorded (up to 32 distinct): counted with texAdd when applied
static void recTex(uint32_t ta) {
  for (uint32_t i = 0; i < rec->ntex; i++) if (rec->tex[i] == ta) return;
  // (more than 32 distinct textures: the texture counter comes up short)
  if (rec->ntex < 32) rec->tex[rec->ntex++] = ta;
}


// Returns to the top level of the list (abandons the current sublist and its recording or check)
#define UNWIND() (l->sp = 0, l->pc = l->stk[0][0], l->off = l->stk[0][1], recAbort(), chk = NULL)
// Interprets list l like the GE up to its stall or end (1: finished), with a command limit per call. Goes in
// 64-byte lines: stall, translation (vtr), read (rdc), list limit, recLine and lo/hi are looked at once
// per line; inside the line it continues while the walk is sequential (no jumps) and the R0 buffer stays the
// same (memoTry may replace it when comparing a sublist). Top-level CALL: sublist memory.
// noinline: inside walk the compiler runs out of registers and the per-command loop uses the stack.
static int __attribute__((noinline)) walkList(List *l) {
  uint32_t lpc = 1, lra = 0;  // current line (game address) and where it is read (VRAM: in the frame's mirror)
  for (int budget = 8000; budget > 0;) {  // then back to the loop (events and pause)
    if (l->stall && l->pc == l->stall) return 0;
    uint32_t pc = l->pc, ra;  // (ra: where it is read; VRAM, in the mirror)
    if ((pc & ~63u) == lpc) ra = lra | (pc & 63);
    else if ((ra = vtr(pc))) { lpc = pc & ~63u; lra = ra & ~63u; }
    if (!ra) {
      regLost();
      if (!isVram(pc)) { acc.bad++; return 1; }
      want(pc, 4, 1);
      if (!l->sp) { acc.lost++; return 1; }
      acc.v_cut++; UNWIND(); continue;  // sublist continuing into a page without copy: back to the top level
    }
    uint32_t op = rdc(&R0, ra, l->ord), ba = R0.a, bn = R0.n, i = (ra - ba) >> 2;
    // consecutive commands left in the line (1: single word)
    uint32_t k = ra - ba < bn ? (64 - (pc & 63)) >> 2 : 1;
    if (l->stall && l->stall - pc < k * 4) k = (l->stall - pc) >> 2;  // (the stall inside the line)
    if (l->n + k > LIST_MAX) { acc.aborted++; regLost(); return 1; }
    if (rec && (pc & ~63u) != rec_last) recLine(pc & ~63u, ra & ~63u);
    uint32_t sp0 = l->sp, top = !sp0 && !isVram(pc);  // top level in RAM: lo/hi range for the list lifetime
    if (top && pc < l->lo) l->lo = pc;
    for (;;) {
    uint32_t c = op >> 24, d = op & 0xFFFFFF, prev = l->prev;
    if (top && pc > l->hi) l->hi = pc;
    l->n++; l->pc = pc + 4; l->prev = op; acc.cmds++; f_cmds++; budget--;
#define TOUCH(c) if (rec) rec_touch[(c) >> 5] |= 1u << ((c) & 31)
    switch (c) {
    case 0x00: DEV(if (d == 0x4E5044) nd_dump_mark = 1;) break;
    case 0x01: vaddr = rel(l, d); vset = 1; if (rec) rec->flags |= M_VABS; break;
    case 0x02: iaddr = rel(l, d); if (rec) rec->flags |= M_IABS; break;
    case 0x04: draw(d); break;
    case 0x05: case 0x06: acc.curves++; acc.draws++; f_draws++; dirty = 0; last_prim = 99; if (rec) recDraw(0, 0); if (reg_on) regCurve(); break;
    case 0x08: case 0x0A: {
      uint32_t t = rel(l, d & 0xFFFFFC);
      if (!vtr(t)) {  // in VRAM without copy in the mirror (yet) or invalid
        regLost();
        if (!isVram(t)) { acc.bad++; return 1; }
        want(t, 4096, 1);  // its page and the next: from the next frame on it is walked from the mirror
        if (c == 0x0A) { acc.vcalls++; break; }  // skipped
        acc.vjumps++;
        if (!l->sp) { acc.lost++; return 1; }
        acc.v_cut++; UNWIND(); break;
      }
      if (c == 0x0A && isVram(t)) acc.v_ok++;
      if (c == 0x0A) {
        if (l->sp >= 32) { acc.bad++; regLost(); return 1; }
        acc.calls_ram++;
        // sample for the list lifetime
        if (!l->sp && l->nsub < 16 && !(l->ncall & 7) && !isVram(t)) l->subs[l->nsub++] = t;
        if (!l->sp) claimSet(l->ncall++, l->ord);
        if (!l->sp && memo_on && (!reg_on || reg_big) && !rec && !chk) {  // top-level sublist: sublist memory, unless a frame fits the record
          uint32_t tq = EXTREME ? now() : 0;
          int r = memoTry(l, t);
          if (tq) tm_memo += now() - tq;
          if (r > 0) break;  // applied: continue after the CALL
          if (r == 0) recBegin(l, t);
        }
        l->stk[l->sp][0] = l->pc; l->stk[l->sp++][1] = l->off;
      }
      l->pc = t;
      break;
    }
    case 0x09: acc.bjumps++; break;  // conditional jump by bounding box: assumed not taken
    case 0x0B: if (l->sp) { l->sp--; l->pc = l->stk[l->sp][0]; l->off = l->stk[l->sp][1]; if (!l->sp) { if (rec) recEnd(l); else if (chk) chkEnd(); } } break;
    case 0x0C:  // END: ends the list, except after a SIGNAL (the game continues it)
      if ((prev >> 24) == 0x0E) { acc.signals++; break; }
      return 1;
    case 0x0E: case 0x0F: break;
    case 0x10: reg[0x10] = d; TOUCH(0x10); break;
    case 0x13: l->off = d << 8; break;
    case 0x14: l->off = pc; break;
    case 0x2A: case 0x3A: case 0x3C: case 0x3E: case 0x40: {
      int m = c == 0x2A ? 4 : (c - 0x3A) / 2;
      mi[m] = d & 0x7F; if (rec) rec->mmask |= 1 << m;
      break;
    }
    case 0x2B: case 0x3B: case 0x3D: case 0x3F: case 0x41: {
      int m = c == 0x2B ? 4 : (c - 0x3B) / 2, max = m == 4 ? 96 : m == 2 ? 16 : 12;
      if (rec) rec->mmask |= 1 << m;
      if (mi[m] < (uint32_t)max) { if (mtx[m][mi[m]] != d) { mtx[m][mi[m]] = d; dirty |= C_MTX; } mi[m]++; }
      break;
    }
    case 0xC4: {  // palette load: a change only if another palette or size
      if (rec) { recRead(0xB0); recRead(0xB1); }
      uint32_t ca = CLUTADDR();
      acc.cluts++; if (isVram(ca)) { acc.clut_vram++; want(ca, (d & 0x3F) * 32, 0); } else smpAdd(ca, (d & 0x3F) * 32, 3); if (rec) rec->flags |= M_CLUT;
      if (ca != last_clut[0] || d != last_clut[1]) { last_clut[0] = ca; last_clut[1] = d; dirty |= C_TEX; }
      if (reg_on) regClut(d);
      break;
    }
    case 0xEA: {
      for (uint32_t k = 0xB2; k <= 0xB5 && rec; k++) recRead(k);
      acc.transfers++;
      uint32_t s = (reg[0xB2] & 0xFFFFF0) | ((reg[0xB3] << 8) & 0x0F000000), t = (reg[0xB4] & 0xFFFFF0) | ((reg[0xB5] << 8) & 0x0F000000);
      if (isVram(s) || isVram(t)) acc.tr_vram++;
      if (reg_on) regXfer(s, t, d & 1 ? 4 : 2);
      break;
    }
    default:
      if (cls[c] && reg[c] != d) { rg_chg = 1; dirty |= cls[c]; if (cls[c] & (C_OTHER | C_TL)) acc.chg[c]++; }
      reg[c] = d; TOUCH(c);
    }
    if (--k == 0 || l->pc != pc + 4 || l->sp != sp0 || R0.a != ba || R0.n != bn) break;  // end of line or jump
    pc += 4; op = R0.buf[++i];
    }
  }
  return 0;  // limit: continue on the next pass
}

static uint32_t capt;  // sequence number of the last captured list

#ifdef NP_DEV
// --- Delays (extreme level) ---
// Each sync where the Vita was behind (the list the game waits on finished capture after the game
// reached the sync) goes to nativedraw_late.log with the source of the delay: when the game queued the list and when
// it finished writing it (last stall), when the Vita began walking it, how long it took and how much was work. Vita
// times become PSP times with off: the minimum difference (in the last second) between the Vita time when
// reading an event and the one the PSP gave it (how long an event takes to arrive with the capturer free).
// Data of the last 64 captured lists (by sequence number & 63)
typedef struct { uint32_t ord, qid, cmds, t_enq, t_stall, t_start, t_end, work, own, hit, memo, ahead, cpu; } Done;
static Done done[64];
static uint32_t off, off_min = ~0u, off_ok;
static struct { uint32_t ord, t0, w, kind; } pend[16];  // syncs waiting for a list not yet captured
static uint32_t npend;
static inline uint32_t v2p(uint32_t t) { return t - off; }  // Vita time -> PSP time
// If list d finished after the sync (PSP time t0) or the game waited (w us), writes the line of
// nativedraw_late.log (kind: 'd' drawsync, 'l' listsync, 'e' before queueing)
static void lateRec(uint32_t t0, uint32_t w, uint32_t kind, Done *d) {
  int late = (int)(v2p(d->t_end) - t0);
  if (late <= 0 && !w) return;  // arrived in time
  acc.pf_late++; if (late > 0) acc.pf_late_us += late;
  rput(1, "%u,%c,%u,%u,%d,%u,%u,%d,%d,%d,%d,%u,%u,%u,%u,%u\n", now() / 1000, kind, w, d->ord, late, d->cmds, d->ahead, (int)(t0 - d->t_enq),
       (int)(t0 - d->t_stall), (int)(v2p(d->t_start) - d->t_enq), (int)(d->t_end - d->t_start), d->work, d->own, d->hit, d->memo, d->cpu);
}
// The game called the sync waiting for list ord: if captured, it is checked now; if not, it stays pending for listDone
static void syncSeen(uint32_t ord, uint32_t t0, uint32_t w, uint32_t kind) {
  if (level < 2 || !ord || no_capture || !off_ok) return;
  if ((int)(capt - ord) >= 0) { if (done[ord & 63].ord == ord) lateRec(t0, w, kind, &done[ord & 63]); return; }
  if (npend < 16) { pend[npend].ord = ord; pend[npend].t0 = t0; pend[npend].w = w; pend[npend++].kind = kind; }
}
#endif
// List lifetime: the gate makes the game wait until the Vita read the frame's list. If the game does not
// rewrite that memory during the next frame (lists in two buffers), the Vita could have one more frame.
// Sample: 16 lines spread over the top-level memory of the frame's largest list and the first line
// of up to 16 of its sublists (1 in 8 CALLs); hash when its capture ends and again at the flip of its frame and
// at the next one. The same sample goes to the mailbox: NativeLink checks it before queueing the next list to decide
// where to place the gate.
typedef struct { uint32_t a[32], h[32], n, ntop, g, st, sq, ord, seen; } Life;
static Life life[4];
// (accumulated) samples checked 1 and 2 flips later, with changes
static uint32_t lf_ord_cmds DEV(, lf_n[2], lf_top[2], lf_sub[2]);
static uint32_t lineHash(uint32_t a) {  // FNV-1a hash of 64-byte line a of PSP RAM
  uint32_t b[16] __attribute__((aligned(16))), h = 2166136261u;
  neon64(b, a);
  for (int i = 0; i < 16; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}
// When a list finishes capture: if it is the largest of the frame, takes its sample and publishes it in the mailbox
// (SB[0x50..0x53], 0x60+ addresses, 0x80+ hashes; SB[0x50] is written last as sample number)
static void lifeTake(List *l) {
  Life *x = &life[gen & 3];
  if (x->g == gen && l->n < lf_ord_cmds) return;
  lf_ord_cmds = l->n;
  x->n = x->ntop = 0; x->g = gen; x->st = 0;
  if (l->lo < l->hi && ramEnd(l->lo) >= l->hi + 64)
    for (int k = 0; k < 16; k++) x->a[x->n++] = (l->lo + (l->hi - l->lo) / 15 * k) & ~63u;
  x->ntop = x->n;
  for (uint32_t k = 0; k < l->nsub; k++) if (ramEnd(l->subs[k]) >= (l->subs[k] & ~63u) + 64) x->a[x->n++] = l->subs[k] & ~63u;
  for (uint32_t k = 0; k < x->n; k++) x->h[k] = lineHash(x->a[k]);
  if (SB[1] >= 5) {  // the sample for NativeLink: it checks it before each queueing and at each sync (EV_LIFE)
    static uint32_t vseq;
    SB[0x50] = 0; __sync_synchronize();
    for (uint32_t k = 0; k < x->n; k++) { SB[0x60 + k] = x->a[k]; SB[0x80 + k] = x->h[k]; }
    SB[0x51] = x->n; SB[0x52] = x->ntop; __sync_synchronize();
    x->sq = ++vseq; x->ord = l->ord; x->seen = 0;
    SB[0x53] = l->ord; __sync_synchronize();  // which list the sample belongs to
    SB[0x50] = x->sq;
  }
}
#ifdef NP_DEV
// NativeLink replies to the sample (EV_LIFE), accumulated: vd[kind][d][...] with kind 0 before queueing, 1
// drawsync, 2 listsync; d = lists queued after the sampled one (0, 1, 2; before queueing: 0 = before
// queueing the next); [0] checked, [1] with top-level changes, [2] with sublist changes
static uint32_t vd[3][3][3];
static struct { uint32_t n, sum, max, t_sync; } gap;  // from the sync to the next queueing (PSP time)
// Notes an EV_LIFE reply (kind, sample number sq, b: top changes | sublists << 8, ord: last queued);
// each combination counts once per sample
static void lifeEv(uint32_t kind, uint32_t sq, uint32_t b, uint32_t ord) {
  for (int j = 0; j < 4; j++) {
    Life *x = &life[j];
    if (x->sq != sq || kind > 2) continue;
    uint32_t d = ord - x->ord, bit = 1u << (kind * 3 + d);
    if (d > 2 || x->seen & bit) return;
    x->seen |= bit;
    vd[kind][d][0]++; vd[kind][d][1] += (b & 0xFF) != 0; vd[kind][d][2] += (b >> 8) != 0;
    return;
  }
}
// At each flip (before gen++): recomputes the hashes of the samples of the closing frame and the previous one
static void lifeFlip(void) {
  for (int j = 0; j < 4; j++) {
    Life *x = &life[j];
    uint32_t df = gen - x->g;
    // (df 0: the flip closing its frame = "1 frame later" than the sync)
    if (!x->n || (df != 0 && df != 1) || x->st > df) continue;
    int top = 0, sub = 0;
    for (uint32_t k = 0; k < x->n; k++) if (lineHash(x->a[k]) != x->h[k]) { if (k < x->ntop) top = 1; else sub = 1; }
    lf_n[df]++; lf_top[df] += top; lf_sub[df] += sub; x->st = df + 1;
  }
}
// List finished capture: saves its data in done[] and checks the pending syncs that waited on it
static void listDone(List *l) {
  Done *d = &done[l->ord & 63];
  d->ord = l->ord; d->qid = l->qid; d->cmds = l->n; d->t_enq = l->t_enq; d->t_stall = l->t_stall; d->t_start = l->t_start;
  d->t_end = now(); d->work = l->work; d->own = R0.own - l->own0; d->hit = R0.hits - l->hit0; d->ahead = l->ahead;
  d->memo = acc.memo_hit >= l->memo0 ? acc.memo_hit - l->memo0 : acc.memo_hit; d->cpu = level >= 2 ? runClk(cap_uid) - l->cpu0 : 0;
  for (uint32_t i = 0; i < npend;)
    if ((int)(pend[i].ord - l->ord) <= 0) {
      if (pend[i].ord == l->ord) lateRec(pend[i].t0, pend[i].w, pend[i].kind, d);
      pend[i] = pend[--npend];
    } else i++;
}
static uint32_t lastOrdOf(uint32_t qid) {  // the last list queued with that qid (what sceGeListSync waits on)
  uint32_t o = 0;
  for (int i = 0; i < 64; i++) if (done[i].qid == qid && (int)(done[i].ord - o) > 0) o = done[i].ord;
  for (uint32_t i = qh; i != qt; i++) if (q[i & 63].qid == qid && (int)(q[i & 63].ord - o) > 0) o = q[i & 63].ord;
  return o;
}

#endif
// Publishes list l for the readers: start, stall, BASE, the mirror copy it is read with (also to the GE, so
// it does not overwrite it) and the read-ahead check plan (what was applied in the previous list). pub_ord goes last
static void pubList(List *l) {
  pub_stall = l->stall; pub_pc = l->pc; pub_base = reg[0x10];
  if (mir_on) {  // the last finished mirror copy, for the whole list
    uint32_t d = SB[0x40]; MS.nlists++; if (d != SB[0x44]) MS.prior++;
    mir_u = d; SB[0x48] = d;
  }
  pv_plan = pv[pv_c]; pv_pn = pv_n[pv_c]; acc.pv_plan += pv_pn; pv_c ^= 1; pv_n[pv_c] = 0; pv_pos = 0; pv_i = 0;
  __sync_synchronize();
  pub_ord = l->ord;
}
// Walks queued lists in order until one sits at its stall or the limit is spent; on finishing each one takes the
// list lifetime sample and publishes in SB[3] that it is captured. no_capture: only marks them captured
static void walk(void) {
  if (no_capture) { while (qh != qt) { capt = q[qh & 63].ord; qh++; acc.lists++; } SB[3] = capt; return; }
  while (qh != qt) {
    List *l = &q[qh & 63];
    R0.n = 0;  // what was read past the previous stall may have changed
    // the readers start this list (and what was left half done of the previous one)
    if (l->ord != pub_ord) { pubList(l); recAbort(); chk = NULL; }
    DEV(if (!l->t_start) { l->t_start = now() | 1; l->own0 = R0.own; l->hit0 = R0.hits; l->memo0 = acc.memo_hit; l->cpu0 = level >= 2 ? runClk(cap_uid) : 0; })
    DEV(uint32_t tw = now();)
    int finish = walkList(l);
    DEV(l->work += now() - tw;)
    if (!finish) return;
    recAbort(); chk = NULL;  // the list ended inside a sublist (END, jump to VRAM...): not saved
    // at all levels (NativeLink uses it) and before SB[3]: never the captured list with the stale sample
    lifeTake(l);
    capt = l->ord; SB[3] = capt; qh++; acc.lists++;
    DEV(listDone(l);)
  }
}

// Empties the queue (lost events or mailbox reset): everything queued so far counts as captured
static void resetQueue(void) {
  qh = qt = 0;
  regLost();
  capt = SB[6]; SB[3] = capt;  // what was lost counts as captured
}

static uint32_t tail, beat, sec_t0;
// EV_FRAME of the current frame; last values of the per-frame line
DEV(static uint32_t pfr[8], lc, s_own, s_hit, s_memo, s_pre, s_late;)
// Processor time of each thread (runClocks, us) to separate "shared core" from "per-command work"
#define EMU_N 256
// emulator threads (nativedraw.c, CreateThread hook)
extern SceUID emu_th[EMU_N]; extern int emu_aff[EMU_N], emu_n;
static SceUID cap_uid = -1, rdr_uid[2] = { -1, -1 };
static uint32_t runClk(SceUID t) {  // processor us received by thread t (0 if it does not exist)
  SceKernelThreadInfo in; sceClibMemset(&in, 0, sizeof in); in.size = sizeof in;
  if (t < 0 || sceKernelGetThreadInfo(t, &in) < 0) return 0;
  return (uint32_t)in.runClocks;
}
// Processor in the last second: capturer, readers and emulator threads summed by pinned core (3: unpinned)
static void cpuSample(void) {
  static uint32_t lc, ll[2], le[EMU_N];
  uint32_t c = runClk(cap_uid);
  acc.cpu_cap = c - lc; lc = c;
  for (int k = 0; k < 2; k++) { c = runClk(rdr_uid[k]); acc.cpu_rdr[k] = c - ll[k]; ll[k] = c; }
  for (int i = 0; i < emu_n && i < EMU_N; i++) {
    c = runClk(emu_th[i]);  // (0: the thread no longer exists)
    int a = emu_aff[i], k = a == 0x10000 ? 0 : a == 0x20000 ? 1 : a == 0x40000 ? 2 : 3;
    if (c && le[i] && c >= le[i]) acc.cpu_core[k] += c - le[i];
    le[i] = c;
  }
}
DEV(static uint32_t sync_ord_v;)  // the last list that went through a sync (the one the gate waits on before queueing)
// Processes a mailbox event (e[0] type and data, e[1..3] a b c, e[4] PSP time, e[5..7] extra): queue a list,
// move its stall, syncs, PSP telemetry (T4, T6, EV_FRAME, EV_LIFE) and the flip, which walks what is queued, closes the
// frame and once per second passes acc to rep for the log
static void event(volatile uint32_t *e) {
  uint32_t t = e[0] & 0xFF, a = e[1], b = e[2], c = e[3];
  DEV(uint32_t d0 = now() - e[4];)
  acc.events++;
  DEV(if (d0 < off_min) off_min = d0;)  // Vita - PSP clock difference
  DEV(if (!off_ok) { off = d0; off_ok = 1; })
  if (t == EV_ENQ || t == EV_HEAD) {
    if (qt - qh >= 64) { acc.resyncs++; resetQueue(); }
    if (t == EV_HEAD) qh--;
    List *l = &q[(t == EV_HEAD ? qh : qt++) & 63];
    sceClibMemset(l, 0, sizeof *l);
    l->pc = a & 0x0FFFFFFC; l->stall = b & 0x0FFFFFFF; l->qid = c; l->ord = e[5];
    l->t_enq = l->t_stall = e[4]; l->ahead = qt - qh - 1; l->lo = ~0u;
    uint32_t w = e[0] >> 8 & ~1u;  // what the game waited at the gate before queueing this list
    if (e[0] >> 8) { acc.eg_n++; acc.eg_us += w; DEV(syncSeen(sync_ord_v, e[4] - w, w, 'e');) }
    DEV(if (gap.t_sync) { uint32_t g = e[4] - gap.t_sync; if (g < 100000) { gap.n++; gap.sum += g; if (g > gap.max) gap.max = g; } gap.t_sync = 0; })
  } else if (t == EV_STALL) {
    for (uint32_t i = qt; i != qh; i--)
      if (q[(i - 1) & 63].qid == a) {
        q[(i - 1) & 63].stall = b & 0x0FFFFFFF; q[(i - 1) & 63].t_stall = e[4];
        if (q[(i - 1) & 63].ord == pub_ord) pub_stall = b & 0x0FFFFFFF;
        break;
      }
  } else if (t == EV_DSYNC) {
    DEV(syncSeen(c, e[4] - (a & ~1u), a & ~1u, 'd'); sync_ord_v = c;)
    DEV(gap.t_sync = e[4];)  // c: the last queued list; e[4]: after the gate (the GE wait comes later)
    acc.syncs++;
    if (a) { acc.syncs_waited++; acc.sync_wait_us += a & ~1u; }
    if (qh != qt) acc.behind++;
  } else if (t == EV_LSYNC) {
    DEV(syncSeen(lastOrdOf(a), e[4] - (b & ~1u), b & ~1u, 'l'); sync_ord_v = lastOrdOf(a);)
    acc.lsyncs++;
#ifdef NP_DEV
  } else if (t == EV_GEDONE) {  // T6: busy GE stretch of the PSP (a = start, event time = end)
    uint32_t dt = e[4] - a;
    acc.ge_us += dt; acc.ge_runs++; acc.ge_stall += b; acc.ge_n += c;
    if (dt > acc.ge_max) acc.ge_max = dt;
  } else if (t == EV_SYNCEND) {  // T6: how long the game waited on the GE at the sync (a us), if the GE was busy (b)
    acc.sy_n++; acc.sy_us += a; if (a > 200) acc.sy_wait++; if (b == 1) acc.sy_busy++;
  } else if (t == EV_CPU) {  // T4: CPU write (data: type)
    uint32_t k = e[0] >> 8 & 0xFF, lo = a & 0x0FFFFFFF, n = k >= 5 ? c : b;
    if (k == 3 || k == 4) { acc.dc_all++; cw_all_t = now(); }
    else {
      if (isVram(lo)) acc.cpu_vram++;
      uint32_t i = cw_n % CW_N;
      cw[i].lo = lo; cw[i].hi = lo + n; cw[i].t = now(); __sync_synchronize(); cw_n++;
    }
  } else if (t == EV_CPUSUM) {  // T4: frame totals
    acc.dc_n += b; acc.dc_kb += c; acc.dm_n += e[3] & 0xFFFF; acc.dm_kb += e[3] >> 16; acc.cpu_drop += e[0] >> 8;
  } else if (t == EV_LIFE) {  // NativeLink checked the list lifetime sample
    lifeEv(e[0] >> 8 & 0xFF, a, b, c);
#endif
  } else if (t == EV_FRAME) {  // what the frame cost the game
    uint32_t x[7] = { a, b, e[0] >> 8 & 0xFF, e[0] >> 16 & 0xFF, c, e[5], e[7] };
    DEV(sceClibMemcpy(pfr, x, sizeof x); pfr[7] = e[6];)
    acc.pf_n++; acc.pf_us += a; acc.pf_gate += b; acc.pf_gate_n += x[2]; acc.pf_nowait += x[3]; acc.pf_sync += c; acc.pf_spin += e[5];
    acc.pf_mir += e[6] & 0xFFFF; acc.pf_pg += e[6] >> 16; acc.pf_post += e[7];
  } else if (t == EV_FLIP) {
    uint32_t t0 = now(), c0 = acc.cmds;  // what was queued before the flip is walked before closing the frame
    walk();
    if (acc.cmds != c0) { uint32_t dt = now() - t0; acc.us += dt; f_us += dt; }
#ifdef NP_DEV
    if (level >= 2) {  // one line per frame in nativedraw_frames.log (extreme)
      uint32_t cc = runClk(cap_uid), cal = SB[0x4D], fus = pfr[0] ? pfr[0] : 1;
      // PSP free CPU, per thousand
      uint32_t idle_pct = cal ? (uint32_t)((unsigned long long)pfr[5] * 1000000 / cal / fus) : 0;
      rput(0, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%08x,%u,%u,%u\n", now() / 1000, pfr[0], pfr[1], pfr[2], pfr[3], pfr[4], idle_pct,
           pfr[7] & 0xFFFF, pfr[7] >> 16, pfr[6], f_cmds, f_draws, f_us, cc - lc, acc.memo_hit - s_memo, acc.memo_pre - s_pre,
           R0.own - s_own, R0.hits - s_hit, qt - qh, acc.pf_late - s_late, life[gen & 3].g == gen ? life[gen & 3].a[0] : 0,
           life[gen & 3].g == gen && life[gen & 3].ntop ? (life[gen & 3].a[15] - life[gen & 3].a[0]) >> 10 : 0, tm_memo, tm_apply);
      tm_memo = tm_apply = 0;
      lc = cc; s_own = R0.own; s_hit = R0.hits; s_memo = acc.memo_hit; s_pre = acc.memo_pre; s_late = acc.pf_late;
    }
    // frames where the game outran the capturer: gate at its 5 ms limit or capture longer than a 30 fps frame
    static uint32_t late_n;
    if ((pfr[1] >= 4500 || f_us >= 33000) && late_n < 400) { late_n++; clog("late frame %u: gate %u us, capture %u us, draws %u\n", gen, pfr[1], f_us, f_draws); }
    sceClibMemset(pfr, 0, sizeof pfr);
#endif
    // Real GE registers (NativeLink's snapshot): a value that stayed the same between two snapshots and differs from the
    // capture replaces it, because the game can set state in lists read too late to see it
    static uint32_t snap, snr[NRS];
    if (SB[0xA0] && SB[0xA0] != snap) {
      snap = SB[0xA0];
      for (int i = 0; i < NRS; i++) {
        uint32_t v = SB[0x100 + RS[i]] & 0xFFFFFF;
        if (v == snr[i] && reg[RS[i]] != v) {
          DEV(static uint32_t nsn; if (nsn < 40) { nsn++; clog("register %02x: capture %06x real %06x (frame %u)\n", RS[i], reg[RS[i]], v, gen); })
          reg[RS[i]] = v; rg_chg = 1;
        }
        snr[i] = v;
      }
    }
    // closes the frame record (a: displayed image, b: width | format << 16)
    if (reg_on) { regFlip(a, b, f_draws); reg_big = f_draws > RD_N; }
    acc.frames++;
    if (qh != qt) acc.flips_behind++;
    if (f_cmds > acc.cmds_max) acc.cmds_max = f_cmds;
    if (f_draws > acc.draws_max) acc.draws_max = f_draws;
    if (f_us > acc.us_max) acc.us_max = f_us;
    f_cmds = f_draws = f_us = 0;
    DEV(if (level >= 2) lifeFlip();)
    lf_ord_cmds = 0;
    gen++; ntex = 0;
    DEV(for (int k = 0; k < 4; k++) smp_f[k] = 0;)
    DEV(ltFlip();)
    uint32_t n = now();
    if (n - sec_t0 >= 1000000) {  // each second: to the log line
      static uint32_t last[8];  // read and reader counters (accumulated) at the previous second
      uint32_t cur[8] = { R0.own, R0.hits, RR[0].own, RR[1].own, lv_lines[0] + lv_lines[1], lv_ok[0] + lv_ok[1], lv_bad[0] + lv_bad[1], lv_late[0] + lv_late[1] };
      acc.own = cur[0] - last[0]; acc.hits = cur[1] - last[1]; acc.rown[0] = cur[2] - last[2]; acc.rown[1] = cur[3] - last[3];
      acc.lv_lines = cur[4] - last[4]; acc.lv_ok = cur[5] - last[5]; acc.lv_bad = cur[6] - last[6]; acc.lv_late = cur[7] - last[7];
      for (int k = 0; k < 8; k++) last[k] = cur[k];
      static uint32_t lp[3];  // same for the read-ahead check
      uint32_t cp[3] = { pv_ok[0] + pv_ok[1], pv_bad[0] + pv_bad[1], pv_late[0] + pv_late[1] };
      acc.pv_ok = cp[0] - lp[0]; acc.pv_bad = cp[1] - lp[1]; acc.pv_late = cp[2] - lp[2];
      for (int k = 0; k < 3; k++) lp[k] = cp[k];
      acc.t0 = n - sec_t0;
      if (level) cpuSample();
      DEV(if (off_min != ~0u) { off = off_min; off_min = ~0u; })  // clock: the minimum of the last second
      if (!rep_ready) { rep = acc; gr = ga; rep_ready = 1; }
      // the record yields complete 2D frames (drawn by the Vita): NativeLink copies all requested VRAM at each flip
      if (reg_on) {
        if (ga.ok * 2 > ga.fr && ga.d3 * 10 < (ga.draws ? ga.draws : 1)) gp_flags |= 16; else gp_flags &= ~16u;
      }
      sceClibMemset(&ga, 0, sizeof ga);
      // recording: keep the difference valid
      if (rec) { uint32_t *a = (uint32_t *)&acc; for (int i = 0; i < SW; i++) rec_acc0[i] -= a[i]; }
      sceClibMemset(&acc, 0, sizeof acc);
      DEV(s_memo = s_pre = s_late = 0;)  // (acc starts over)
      sec_t0 = n;
    }
  }
}

void alogNew(void);  // nativedraw.c
static volatile int logs_new;  // the capturer asks the log thread for new logs (a new game) and waits for it
// Capturer thread: waits for the NativeLink mailbox, gives the heartbeat (SB[4]), processes new ring events
// (SB[2] head, own tail; SB[5] processed), walks the lists and sleeps briefly if there is no work
static int capThread(SceSize args, void *argp) {
  uint32_t idle_t = 0, on = 0;
  for (;;) {
    if (SB[0] != SB_MAGIC) {  // no NativeLink in shadow mode
      if (on) { clog("NativeLink mailbox gone: capture paused\n"); on = 0; mir_on = 0; }
      sceKernelDelayThread(50000);
      continue;
    }
    if (!on) {
      static uint32_t sessions;  // the mailbox comes back with each game: every game after the first starts new logs
      if (sessions++) { logs_new = 1; while (logs_new) sceKernelDelayThread(10000); }
      // the ring starts at 0 with the mailbox: the first lists are walked too, they may set state the game never repeats
      on = 1; tail = 0; resetQueue(); sec_t0 = now(); sceClibMemset(&acc, 0, sizeof acc);
      for (int w = 0; w < 16; w++) want_l[w] = want_d[w] = rt_pg[w] = mir_ok[0][w] = mir_ok[1][w] = 0;
      mir_u = 0; mir_reset = 1; mir_on = SB[1] >= 4;
      clog("NativeLink mailbox (shadow mode) found: events=%u lists=%u -> capture active%s\n", tail, SB[6],
           mir_on ? ", with VRAM mirror" : " (NativeLink without VRAM mirror)");
      if (reg_on) regBench();  // measures once the speed of the frame record copies
    }
    SB[4] = ++beat;
    if (reg_on) regPend();  // VRAM textures of a frame waiting for the mirror copy of its flip
    uint32_t h = SB[2], work = 0;
    if ((int)(h - tail) < 0 || h - tail > SB_N) {  // events were lost (or NativeLink reset the mailbox)
      acc.resyncs++; tail = h; resetQueue();
    }
    uint32_t t0 = now();
    while (tail != h) { event(SB + 0x400 + (tail % SB_N) * 8); tail++; work = 1; }
    SB[5] = tail;
    uint32_t c0 = acc.cmds;
    walk();
    if (acc.cmds != c0) work = 1;
    uint32_t dt = now() - t0;
    if (work) { acc.us += dt; f_us += dt; idle_t = now(); }
    // idle for 0.3 ms: sleep briefly
    else if (now() - idle_t > 300) { uint32_t s0 = now(); sceKernelDelayThread(100); acc.sleep_us += now() - s0; }
  }
  return 0;
}

// --- Log (separate thread: writing to the card is slow) ---
static char lbuf[8192];
static volatile uint32_t lpos;
// Appends a line with the time (ms) to the nativedraw_capture.log buffer; dropped if it does not fit
static void clog(const char *fmt, ...) {
  char b[1024];
  va_list ap; va_start(ap, fmt);
  int n = sceClibSnprintf(b, 16, "%7u ", now() / 1000);
  n += sceClibVsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap);
  if (n > (int)sizeof b - 1) { n = sizeof b - 1; b[n - 1] = '\n'; }  // (cut: the newline is kept)
  if (lpos + n > sizeof lbuf) return;
  sceClibMemcpy(lbuf + lpos, b, n); lpos += n;
}
#ifdef NP_DEV
// nativedraw_frames.log and nativedraw_late.log: single-writer rings (the capturer); the log thread drains them
static char rb0[65536], rb1[16384];
static struct { char *b; uint32_t n; volatile uint32_t w, r; const char *path; } RG[2] = {
  { rb0, sizeof rb0, 0, 0, "ux0:pspemu/nativedraw_frames.log" }, { rb1, sizeof rb1, 0, 0, "ux0:pspemu/nativedraw_late.log" } };
// Appends a line to ring k (0 frames, 1 late)
static void rput(int k, const char *fmt, ...) {
  char b[384];
  va_list ap; va_start(ap, fmt);
  int n = sceClibVsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  if (n > (int)sizeof b - 1) n = sizeof b - 1;
  if (n <= 0 || RG[k].w - RG[k].r + n > RG[k].n) return;  // (full: the line is dropped)
  for (int i = 0; i < n; i++) RG[k].b[(RG[k].w + i) % RG[k].n] = b[i];
  __sync_synchronize();
  RG[k].w += n;
}
// Appends to its file what ring k holds (in two parts if it wraps)
static void rflush(int k) {
  uint32_t w = RG[k].w, r = RG[k].r;
  if (w == r) return;
  SceUID fd = sceIoOpen(RG[k].path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) {
    uint32_t a = r % RG[k].n, n = w - r, m = RG[k].n - a < n ? RG[k].n - a : n;
    sceIoWrite(fd, RG[k].b + a, m);
    if (n > m) sceIoWrite(fd, RG[k].b, n - m);
    sceIoClose(fd);
  }
  RG[k].r = w;
}
#endif
#define PF(x) (r->x / f)  // per-frame statistic
// Log thread (low priority): every 0.2 s, if the capturer left a second ready (rep), writes it as lines of
// text by level; drains the rings every 2 s (or at half full) and nativedraw_capture.log every 5 s (or at
// half of the buffer)
// Moves what is pending in lbuf to the capturer log
static void logWrite(void) {
  SceUID fd = sceIoOpen(LOGC, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  uint32_t n = lpos;
  if (fd >= 0) { sceIoWrite(fd, lbuf, n); sceIoClose(fd); }
  lpos = 0;  // (what arrives during the write may be lost: it is statistics)
}
// The current capturer logs become *_prev.log
static void logsRotate(void) {
  sceIoRemove("ux0:pspemu/nativedraw_capture_prev.log");
  sceIoRename(LOGC, "ux0:pspemu/nativedraw_capture_prev.log");
#ifdef NP_DEV
  static const char *rot[2][2] = { { "ux0:pspemu/nativedraw_frames.log", "ux0:pspemu/nativedraw_frames_prev.log" },
                                   { "ux0:pspemu/nativedraw_late.log", "ux0:pspemu/nativedraw_late_prev.log" } };
  for (int k = 0; k < 2; k++) { sceIoRemove(rot[k][1]); sceIoRename(rot[k][0], rot[k][1]); }
#endif
}
// First lines of the capturer logs
static void logsHead(void) {
  clog("NativeDraw " NATIVEDRAW_VERSION ": capturer (sublist memory, VRAM mirror, frame record)\n");
#ifdef NP_DEV
  if (level >= 2) rput(0, "t_ms,frame_us,gate_us,waits,no_wait,sync_GE_us,cpu_idle_x1000,mirror_us,mirror_pages,hooks_us,"
          "commands,draws,capture_us,capture_cpu_us,applied,checked_readers,own_reads,from_readers,pending_lists,late_syncs,"
          "list_addr,list_kb,memory_us,apply_us\n");
  if (level >= 2) rput(1, "t_ms,type,wait_us,list,delay_us,commands,lists_before,enqueued_ago_us,last_stall_ago_us,vita_started_after_us,"
          "vita_walked_us,work_us,own_reads,from_readers,applied,processor_us\n");
#endif
}
static int repThread(SceSize args, void *argp) {
  uint32_t last_write = 0;
  for (;;) {
    sceKernelDelayThread(200000);
    if (logs_new) {  // new game: what is pending goes to the old logs
      logWrite(); DEV(rflush(0); rflush(1);)
      logsRotate(); logsHead(); alogNew();
      logs_new = 0;
    }
    static uint32_t nrep;
    if (rep_ready && !level && ++nrep % 10) rep_ready = 0;  // minimum: 1 in 10 seconds
    if (rep_ready) {
      Stats *r = &rep;
      uint32_t f = r->frames ? r->frames : 1, d = r->draws ? r->draws : 1;
      clog("sec: frames=%u lists/f=%u commands/f=%u (max %u) draws/f=%u (max %u) | ARM us/f=%u (max %u) ns/command=%u | reads/f own=%u from_readers=%u (readers read %u+%u) events/f=%u\n",
           r->frames, PF(lists), PF(cmds), r->cmds_max, PF(draws), r->draws_max, PF(us), r->us_max, r->cmds ? (uint32_t)((unsigned long long)r->us * 1000 / r->cmds) : 0, PF(own), PF(hits), PF(rown[0]), PF(rown[1]), PF(events));
      if (!level) goto sync;  // (minimum: only this line and the vram and sync one)
      if (reg_on) {  // frame record
        uint32_t g = gr.fr ? gr.fr : 1, dd = gr.draws ? gr.draws : 1;
        clog("  record: frames=%u complete=%u incomplete: memory=%u not_read=%u limit=%u curves=%u | per frame: draws=%u states=%u KB=%u palettes=%u us=%u (states %u vertices %u textures %u [copied %u KB by DMA in %u us, of them swizzled %u KB sorted in %u us; pending lost %u, without the copy of their flip %u, with the GPU unfinished (measured) %u] palettes %u) | of the draws: 3d=%u%% clear=%u%% blend=%u%% alpha_test=%u%% swizzle=%u%% | draws per sec with texture 5650/5551/4444/8888/T4/T8/T16/T32/DXT1/3/5: %u %u %u %u %u %u %u %u %u %u %u\n",
             gr.fr, gr.ok, gr.memo, gr.rd_fail, gr.limit, gr.curve, gr.draws / g, gr.st / g, gr.kb / g >> 10, gr.cl / g, gr.us / g,
             gr.us_e / g, gr.us_v / g, gr.us_t / g, gr.t_b / g >> 10, gr.us_l / g, gr.o_b / g >> 10, gr.us_o / g, gr.stomped, gr.n_late, gr.gpu, gr.us_p / g,
             gr.d3 * 100 / dd, gr.clear * 100 / dd, gr.blend * 100 / dd, gr.atest * 100 / dd, gr.swz * 100 / dd,
             gr.fmt[0], gr.fmt[1], gr.fmt[2], gr.fmt[3], gr.fmt[4], gr.fmt[5], gr.fmt[6], gr.fmt[7], gr.fmt[8], gr.fmt[9], gr.fmt[10]);
        clog("  textures (per frame): from RAM %u copies %u KB (DMA %u us, sort %u us) | from VRAM %u copies %u KB (DMA %u us, sort %u us) | "
             "DMA calls %u | copies of <1K %u, 1-4K %u, 4-16K %u, 16-64K %u, 64K+ %u | KB swizzled by row bytes: 16 %u, 32 %u, "
             "64 %u, 128 %u, 256 %u, 512+ %u | swizzled KB against the last copy: same %u, changed %u, new %u | KB not swizzled %u | "
             "distinct textures %u\n", gr.t_n[0] / g, gr.t_kb[0] / g >> 10, gr.t_dma[0] / g, gr.t_ord[0] / g, gr.t_n[1] / g, gr.t_kb[1] / g >> 10,
             gr.t_dma[1] / g, gr.t_ord[1] / g, gr.t_calls / g, gr.t_size[0] / g, gr.t_size[1] / g, gr.t_size[2] / g, gr.t_size[3] / g, gr.t_size[4] / g,
             gr.t_rb[0] / g >> 10, gr.t_rb[1] / g >> 10, gr.t_rb[2] / g >> 10, gr.t_rb[3] / g >> 10, gr.t_rb[4] / g >> 10, gr.t_rb[5] / g >> 10,
             gr.t_same / g >> 10, gr.t_changed / g >> 10, gr.t_fresh / g >> 10, gr.t_lines / g >> 10, gr.t_tex / g);
        clog("  texture cache (per frame): reused %u KB, copied %u KB, changed groups %u, textures in the record %u, no room %u, check %u us\n",
             gr.c_hit / g >> 10, gr.c_fill / g >> 10, gr.c_chg / g, gr.c_rec / g, gr.c_fail / g, gr.c_us / g);
      }
      clog("  draws/f: 3d=%u 2d=%u clear=%u empty=%u | tri=%u strip=%u fan=%u rect=%u lines=%u points=%u curves=%u | vertices/f=%u\n",
           PF(d3d), PF(dthr), PF(dclear), PF(dempty), PF(prim[3]), PF(prim[4]), PF(prim[5]), PF(prim[6]), PF(prim[1]) + PF(prim[2]),
           PF(prim[0]), PF(curves), PF(verts));
      clog("  state (draws/f): new_texture=%u texture_change=%u matrix=%u target=%u other=%u no_change=%u (%u%%) | batches/f=%u contiguous/f=%u not_cut_by_texlevel/f=%u only_texlevel=%u target_changes/f=%u\n",
           PF(texnew), PF(ch_tex), PF(ch_mtx), PF(ch_rt), PF(ch_other), PF(ch_none), r->ch_none * 100 / d, PF(batches), PF(batches_cont),
           PF(batches_notl), PF(only_tl), PF(rt_switch));
      uint32_t awake = r->t0 > r->sleep_us ? r->t0 - r->sleep_us : 1;  // time the capturer was not sleeping
      clog("  processor (ms per sec): capture=%u of %u awake (%u%%) working=%u | ns/command: clock=%u processor_max=%u processor_estimated=%u | readers=%u+%u | emulator core0=%u core1=%u core2=%u unpinned=%u\n",
           r->cpu_cap / 1000, awake / 1000, (uint32_t)((unsigned long long)r->cpu_cap * 100 / awake), r->us / 1000,
           r->cmds ? (uint32_t)((unsigned long long)r->us * 1000 / r->cmds) : 0,
           r->cmds ? (uint32_t)((unsigned long long)r->cpu_cap * 1000 / r->cmds) : 0,
           r->cmds ? (uint32_t)((unsigned long long)r->us * r->cpu_cap / awake * 1000 / r->cmds) : 0,
           r->cpu_rdr[0] / 1000, r->cpu_rdr[1] / 1000, r->cpu_core[0] / 1000, r->cpu_core[1] / 1000, r->cpu_core[2] / 1000,
           r->cpu_core[3] / 1000);
      clog("  sublist memory (per frame): applied=%u (checked_by_readers=%u) commands_not_walked=%u (%u%% of all) lines_checked=%u compare_us=%u | new=%u changed=%u other_entry=%u not_saved=%u | readers/f: lines=%u checked=%u not_equal=%u late=%u | lookahead/f: plan=%u checked=%u not_equal=%u late=%u | per sec: pool_emptied=%u reviewed=%u MISMATCHES=%u\n",
           PF(memo_hit), PF(memo_pre), PF(memo_cmds), r->cmds ? (uint32_t)((unsigned long long)r->memo_cmds * 100 / r->cmds) : 0, PF(memo_lines), PF(memo_us),
           PF(memo_miss), PF(memo_fail), PF(memo_dep), PF(memo_no), PF(lv_lines), PF(lv_ok), PF(lv_bad), PF(lv_late), PF(pv_plan), PF(pv_ok), PF(pv_bad), PF(pv_late),
           r->memo_reset, r->memo_chk, r->memo_bad);
      clog("  memory/f: textures=%u (in vram %u) draws_tex_vram=%u draws_vert_vram=%u | vertices=%u KB indices=%u KB | palettes=%u (vram %u) copies=%u (vram %u)\n",
           PF(textures), PF(tex_vram), PF(dtex_vram), PF(dvert_vram), PF(vbytes) >> 10, PF(ibytes) >> 10, PF(cluts), PF(clut_vram),
           PF(transfers), PF(tr_vram));
    sync:
      clog("  vram and sync (per sec): vram_sublists=%u vram_jumps=%u lost_lists=%u aborted=%u invalid=%u signal=%u bjump=%u | drawsync=%u waited=%u (%u us each) behind_at_sync=%u behind_at_flip=%u listsync=%u resets=%u\n",
           r->vcalls, r->vjumps, r->lost, r->aborted, r->bad, r->signals, r->bjumps, r->syncs, r->syncs_waited,
           r->syncs_waited ? r->sync_wait_us / r->syncs_waited : 0, r->behind, r->flips_behind, r->lsyncs, r->resyncs);
      if (!level) goto finish;
      {  // mirror: requested / valid pages (now) and the last second's figures
        static uint32_t lc, lp, lb, lv, le[8];
        uint32_t pl = 0, pd = 0, po0 = 0, po1 = 0, pr = 0, c = SB[0x44], pg = SB[0x45], bz = SB[0x46], vu = SB[0x49];
        for (int i = 0; i < 512; i++) {
          pl += want_l[i >> 5] >> (i & 31) & 1; pd += want_d[i >> 5] >> (i & 31) & 1; pr += rt_pg[i >> 5] >> (i & 31) & 1;
          po0 += mir_ok[0][i >> 5] >> (i & 31) & 1; po1 += mir_ok[1][i >> 5] >> (i & 31) & 1;
        }
        uint32_t e[8] = { MS.chk, MS.fresh, MS.ch_l, MS.ch_d, MS.us, MS.nlists, MS.prior, 0 }, x[8];
        for (int i = 0; i < 8; i++) { x[i] = e[i] - le[i]; le[i] = e[i]; }
        if (mir_on) {
          clog("  mirror: pages requested lists=%u data=%u valid=%u/%u in_targets=%u | per sec: copies=%u pages_copied=%u (KB/f %u) busy=%u vita_using=%u | vram_sublists/f walked=%u cut=%u textures_in_target/f=%u\n",
               pl, pd, po0, po1, pr, c - lc, pg - lp, (pg - lp) * 4 / f, bz - lb, vu - lv, PF(v_ok), PF(v_cut), PF(tex_rt));
          if (EXTREME) clog("  mirror (per sec): lists=%u with_previous_copy=%u | sampling: reviewed=%u new=%u changed lists=%u data=%u thread_us=%u\n",
               x[5], x[6], x[0], x[1], x[2], x[3], x[4]);
        }
        lc = c; lp = pg; lb = bz; lv = vu;
      }
      char top[160]; int k = 0;  // the 10 most changing "other" registers (command: changes per frame)
      for (int j = 0; j < 10; j++) {
        int best = -1;
        for (int c = 0; c < 256; c++) if (r->chg[c] && (best < 0 || r->chg[c] > r->chg[best])) best = c;
        if (best < 0) break;
        k += sceClibSnprintf(top + k, sizeof top - k, " %02x:%u", best, r->chg[best] / f);
        r->chg[best] = 0;
      }
      top[k] = 0;
      clog("  most frequent \"other\" changes (command:per frame):%s\n", top);
#ifdef NP_DEV
      if (SB[1] >= 3 && level >= 2) {
        clog("  T6 PSP GE (per frame): busy=%u us in %u runs (longest %u us) in_stall=%u%% | sync/f=%u waits_for_GE=%u (%u us each) GE_busy_at_call=%u | T4 game CPU (per frame): cache=%u (%u KB) dma=%u (%u KB) to_vram/sec=%u flush_all/sec=%u not_noted/sec=%u\n",
             PF(ge_us), PF(ge_runs), r->ge_max, r->ge_n ? r->ge_stall * 100 / r->ge_n : 0, PF(sy_n), PF(sy_wait),
             r->sy_wait ? r->sy_us / r->sy_wait : 0, PF(sy_busy), PF(dc_n), PF(dc_kb), PF(dm_n), PF(dm_kb), r->cpu_vram, r->dc_all, r->cpu_drop);
        static uint32_t l[22];  // data lifetime: the last second's figures (accumulated in LT)
        uint32_t c[22];
        for (int k = 0; k < 4; k++) { c[k] = LT.n[k]; c[4 + k] = LT.ch1[k]; c[8 + k] = LT.ch2[k]; c[12 + k] = LT.warn[k]; c[16 + k] = LT.wall[k]; }
        c[20] = LT.ms1; c[21] = LT.ms2;
        uint32_t x[22]; for (int k = 0; k < 22; k++) { x[k] = c[k] - l[k]; l[k] = c[k]; }
        uint32_t nn = x[0] + x[1] + x[2] + x[3] ? x[0] + x[1] + x[2] + x[3] : 1;
        clog("  data life (per sec; changed 1 / 2 frames later; notice: range / flush_all): vertices=%u (%u / %u, notice %u / %u) indices=%u (%u / %u, %u / %u) textures=%u (%u / %u, %u / %u) palettes=%u (%u / %u, %u / %u) | reviewed at %u / %u ms | total: vertices %u of %u, textures %u of %u\n",
             x[0], x[4], x[8], x[12], x[16], x[1], x[5], x[9], x[13], x[17], x[2], x[6], x[10], x[14], x[18], x[3], x[7], x[11], x[15], x[19],
             x[20] / nn / 10, x[21] / nn / 10, LT.ch2[0], LT.n[0], LT.ch2[2], LT.n[2]);
      }
#endif
      if (r->pf_n) {  // what each frame cost the game and the syncs where the Vita was behind
        uint32_t g = r->pf_n, cal = SB[0x4D], ms = r->pf_us / 1000 + 1;
        clog("  PSP impact (per game frame): frames=%u frame_us=%u CPU_idle=%u%% gate_us=%u (waits/sec=%u no_wait/sec=%u) sync_GE_us=%u hooks_us=%u mirror_us=%u pages=%u | Vita behind: syncs/sec=%u delay_us=%u each | own read: %u us (%u timed) | shared cache line: %u ns (%u) | gate: %s, waits before enqueue/sec=%u (%u us each)\n",
             g, r->pf_us / g, cal ? r->pf_spin / cal * 100 / ms : 0, r->pf_gate / g, r->pf_gate_n, r->pf_nowait, r->pf_sync / g, r->pf_post / g,
             r->pf_mir / g, r->pf_pg / g, r->pf_late, r->pf_late ? r->pf_late_us / r->pf_late : 0, r->rd_n ? r->rd_us / r->rd_n : 0, r->rd_n, r->hit_n ? r->hit_us * 1000 / r->hit_n : 0, r->hit_n,
             gp_flags & 8 ? "at the sync (gate_sync)" : SB[0x54] == 1 ? "before enqueue" : SB[0x54] == 2 ? "at the sync (the list changed 10 times)" : "at the sync (probing)",
             r->eg_n, r->eg_n ? r->eg_us / r->eg_n : 0);
      }
#ifdef NP_DEV
      if (level >= 2) {  // list lifetime (accumulated since start)
        clog("  list life (accumulated): reviewed at the flip of its frame=%u (top level changed %u, sublists %u) | at the next flip=%u (%u, %u)\n",
             lf_n[0], lf_top[0], lf_sub[0], lf_n[1], lf_top[1], lf_sub[1]);
        clog("  list life per NativeLink (accumulated; reviewed, top changed, sublists changed): drawsync of its frame %u %u %u | before enqueuing the next %u %u %u | next drawsync %u %u %u | before enqueuing the one after %u %u %u | listsync %u %u %u / %u %u %u | from drawsync to next enqueue: %u us (max %u)\n",
             vd[1][0][0], vd[1][0][1], vd[1][0][2], vd[0][0][0], vd[0][0][1], vd[0][0][2], vd[1][1][0], vd[1][1][1], vd[1][1][2],
             vd[0][1][0], vd[0][1][1], vd[0][1][2], vd[2][0][0], vd[2][0][1], vd[2][0][2], vd[2][1][0], vd[2][1][1], vd[2][1][2],
             gap.n ? gap.sum / gap.n : 0, gap.max);
        gap.n = gap.sum = gap.max = 0;
      }
#endif
    finish:
      rep_ready = 0;
    }
#ifdef NP_DEV
    static uint32_t rg_t;  // the rings: every 2 s or at half full
    if (RG[0].w - RG[0].r > RG[0].n / 2 || RG[1].w - RG[1].r > RG[1].n / 2 || now() - rg_t > 2000000) { rflush(0); rflush(1); rg_t = now(); }
#endif
    if (lpos && (now() - last_write > 5000000 || lpos > sizeof lbuf / 2)) {
      logWrite();
      last_write = now();
    }
  }
  return 0;
}

#ifdef NP_DEV
// FNV-1a hash of [a, a + n) (n <= 256) of PSP RAM, reading the 64-byte lines covering it
static uint32_t smpHash(uint32_t a, uint32_t n) {
  static uint32_t buf[96] __attribute__((aligned(16)));
  uint32_t b = a & ~63u, e = (a + n + 63) & ~63u, h = 2166136261u;
  if (e > ramEnd(a)) e = ramEnd(a);
  for (uint32_t x = b; x < e; x += 64) neon64(buf + (x - b) / 4, x);
  const uint8_t *p = (const uint8_t *)buf + (a - b);
  for (uint32_t i = 0; i < n && a + i < e; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
// Was there a known game CPU write over [a, a + n) since t0: 1 a covering range, 2 "flush the whole
// cache", 0 none. Walks the last CW_N from newest to oldest
static int cwCovers(uint32_t a, uint32_t n, uint32_t t0) {
  uint32_t m = cw_n;
  for (uint32_t k = 0; k < CW_N && k < m; k++) {
    uint32_t i = (m - 1 - k) % CW_N;
    if ((int)(cw[i].t - t0) < 0) break;
    if (cw[i].lo < a + n && a < cw[i].hi) return 1;
  }
  return cw_all_t && (int)(cw_all_t - t0) >= 0 ? 2 : 0;
}
// (capturer, at each flip) Data lifetime: recomputes the hash of samples 1 and 2 frames back, counts those that
// changed and whether a known CPU write explains it; frees those already checked twice
static void ltFlip(void) {
  if (level < 2) return;
  for (uint32_t i = smp_r; i != smp_w; i++) {
    Smp *x = &smp[i & (SMP_N - 1)];
    uint32_t df = gen - x->g;
    if (x->st == 0 && df >= 1) { x->ch1 = smpHash(x->a, x->n) != x->h0; x->st = 1; LT.ms1 += (now() - x->t0) / 100; }
    else if (x->st == 1 && df >= 2) {
      uint32_t ch2 = smpHash(x->a, x->n) != x->h0, k = x->kind, w;
      LT.n[k]++; LT.ch1[k] += x->ch1; LT.ch2[k] += ch2; LT.ms2 += (now() - x->t0) / 100;
      if ((x->ch1 || ch2) && (w = cwCovers(x->a, x->n, x->t0))) { if (w == 1) LT.warn[k]++; else LT.wall[k]++; }
      x->st = 2;
    }
  }
  while (smp_r != smp_w && smp[smp_r & (SMP_N - 1)].st == 2) smp_r++;
}
static uint32_t probe_ge;  // asks NativeLink for the thread watching the GE (T6): ux0:data/nativedraw/ge_timing
#endif
// Mirror thread (every 5 ms): passes ge_timing, the switches (SB[0x4A]) and the level (SB[0x4E]) to NativeLink; at
// extreme level hashes 4 requested pages per turn and counts those that changed between copies; every
// 0.1 s rewrites the requests in the mailbox
static int mirThread(SceSize args, void *argp) {
  DEV(static uint32_t seen[512], hs[512], buf[64] __attribute__((aligned(16)));)
  uint32_t n = 0 DEV(, rr = 0);
  for (;;) {
    sceKernelDelayThread(5000);
    DEV(if (SB[0] == SB_MAGIC && SB[1] >= 3 && SB[0x47] != probe_ge) SB[0x47] = probe_ge;)
    if (SB[0] == SB_MAGIC && SB[1] >= 5 && SB[0x4A] != gp_flags) SB[0x4A] = gp_flags;  // switches for NativeLink
    if (SB[0] == SB_MAGIC && SB[1] >= 5 && SB[0x4E] != level) SB[0x4E] = level;         // log level
    DEV(if (mir_reset) { sceClibMemset(seen, 0, sizeof seen); sceClibMemset(hs, 0, sizeof hs); mir_reset = 0; })
    if (!mir_on) continue;
#ifdef NP_DEV
    uint32_t t0 = now(), done = SB[0x40];
    for (uint32_t k = 0, m = 0; k < 512 && m < 4 && level >= 2; k++) {  // (extreme only)
      uint32_t p = rr = (rr + 1) & 511, w = p >> 5, b = 1u << (p & 31);
      if (!((want_l[w] | want_d[w]) & b)) continue;
      // the newest finished copy of p, from either mirror
      uint32_t s0 = CPY(0, p), s1 = CPY(1, p), sq = (int)(s1 - s0) > 0 && (int)(done - s1) >= 0 ? s1 : s0;
      if (!sq || sq == seen[p] || (int)(done - sq) < 0) continue;
      seen[p] = sq; m++;
      uint32_t h = 2166136261u | 1, base = (sq & 1 ? MIRROR2 : MIRROR) + p * 4096;
      for (uint32_t o = 0; o < 4096; o += 256) { neon256(buf, base + o); for (int i = 0; i < 64; i++) h = (h ^ buf[i]) * 16777619u; }
      if (!hs[p]) MS.fresh++;  // (first time: does not count as a change)
      else if (h != hs[p]) { if (want_l[w] & b) MS.ch_l++; else MS.ch_d++; }
      hs[p] = h | 1; MS.chk++;
    }
    MS.us += now() - t0;
#endif
    if (++n % 20 == 0)  // (in case NativeLink reset the mailbox without the capturer noticing)
      for (int w = 0; w < 16; w++) { if (SB[0x20 + w] != want_l[w]) SB[0x20 + w] = want_l[w]; if (SB[0x30 + w] != (want_d[w] | want_fb[w])) SB[0x30 + w] = want_d[w] | want_fb[w]; }
  }
  return 0;
}

// Reader: walks the list published by the capturer only to bring its lines to the shared cache (follows jumps,
// sublists, BASE and OFFSET; does not interpret draws). Each top-level CALL is taken by the first reader to arrive
// (claimTake); if the sublist is already in memory it is not walked: it is verified (memoVerify).
typedef struct { uint32_t ord, pc, off, base, sp, stk[32][2], prev, ncall, n, done; } Rdr;  // a reader's walk
static void rdrStart(Rdr *s, Rd *r) {  // starts the list published by the capturer
  s->ord = pub_ord;
  __sync_synchronize();
  s->pc = pub_pc; s->base = pub_base; s->off = s->sp = s->prev = s->ncall = s->n = 0; s->done = !s->ord; r->n = 0;
}
// Continues walk s of reader id up to budget commands, the stall, the list end or another list being published
static void rdrWalk(int id, Rdr *s, int budget) {
  Rd *r = &RR[id];
  uint32_t st = pub_stall;
  r->n = 0;  // (the stall may have moved)
  for (int k = 0; k < budget && pub_ord == s->ord && !s->done; k++) {
    if (st && s->pc == st) break;
    uint32_t ra = vtr(s->pc);
    // sublist in a VRAM page without copy: back to the top level, like the capturer
    if (!ra && s->sp && isVram(s->pc)) { s->sp = 0; s->pc = s->stk[0][0]; s->off = s->stk[0][1]; continue; }
    if (!ra || ++s->n > LIST_MAX) { s->done = 1; break; }
    uint32_t at = s->pc, op = rdc(r, ra, s->ord), c = op >> 24, d = op & 0xFFFFFF;
    s->pc += 4;
    if (c == 0x08 || c == 0x0A) {
      uint32_t t = ((((s->base & 0x0F0000) << 8) | (d & 0xFFFFFC)) + s->off) & 0x0FFFFFFF;
      if (!vtr(t)) { if (c == 0x08) s->done = 1; }  // VRAM without copy: unreadable
      else if (c == 0x08) s->pc = t;
      else if (s->sp < 32 && (s->sp || claimTake(s->ncall++, s->ord, id))) {
        // checked: no need to walk it
        if (!s->sp && memo_on && memoVerify(id, r, t, s->base, s->off, s->ord)) ;
        else { s->stk[s->sp][0] = s->pc; s->stk[s->sp++][1] = s->off; s->pc = t; }
      }
    } else if (c == 0x0B) { if (s->sp) { s->sp--; s->pc = s->stk[s->sp][0]; s->off = s->stk[s->sp][1]; } }
    else if (c == 0x0C) { if ((s->prev >> 24) != 0x0E) s->done = 1; }
    else if (c == 0x10) s->base = d;
    else if (c == 0x13) s->off = d << 8;
    else if (c == 0x14) s->off = at;
    s->prev = op;
  }
}
// Reader thread (argp: its number): with each new list first checks the read-ahead plan and then walks it;
// sleeps 0.1 ms if it reached the stall or the end
static int rdrThread(SceSize args, void *argp) {
  int id = *(int *)argp;
  Rdr s; s.ord = 0; s.done = 1;
  for (;;) {
    if (pub_ord != s.ord) { rdrStart(&s, &RR[id]); if (pv_on) pvRun(id, &RR[id], s.ord); }  // the plan first
    uint32_t st = pub_stall;
    if (s.done || (st && s.pc == st)) { sceKernelDelayThread(100); continue; }
    rdrWalk(id, &s, 2000);
  }
  return 0;
}

// Switch p: the file exists and does not start with "no" ("no" inside turns it off without deleting it)
static int swOn(const char *p) {
  char b[2] = { 0, 0 };
  SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0);
  if (fd < 0) return 0;
  sceIoRead(fd, b, 2); sceIoClose(fd);
  return !(b[0] == 'n' && b[1] == 'o');
}
// Startup (called by nativedraw.c): rotates the logs, starts the log thread, reads the level and switches from
// ux0:data/nativedraw/, finds the readable PSP RAM ranges, reserves sublist memory and the shared
// cache (each one turns itself off if memory is short) and starts the capturer, the mirror thread and both readers
void capStart(void) {
  SceIoStat st;
  logsRotate();
  SceUID r = sceKernelCreateThread("nativedraw_capture_log", repThread, 0xB0, 0x2000, 0, 0, NULL);  // first: so the
  if (r >= 0) sceKernelStartThread(r, 0, NULL);                                                      // log survives
  {  // log level
    char b[8] = { 0 };
    SceUID fd = sceIoOpen("ux0:data/nativedraw/log_level", SCE_O_RDONLY, 0);
    if (fd >= 0) { sceIoRead(fd, b, 7); sceIoClose(fd); level = DEV(b[0] == 'e' ? 2 :) b[0] == 'm' && b[1] == 'i' ? 0 : 1; }
  }
  if (EXTREME) { uint32_t t0 = now(), x = 0; for (int i = 0; i < 10000; i++) x += now(); clog("clock read cost: %u ns (%u)\n", (now() - t0) / 10, x & 1); }
  logsHead();
  static const char *sw[] = { "ux0:data/nativedraw/no_gate", "ux0:data/nativedraw/no_mirror" DEV(, "ux0:data/nativedraw/no_idle", "ux0:data/nativedraw/gate_sync") };
  for (int k = 0; k < (int)(sizeof sw / sizeof *sw); k++) if (swOn(sw[k])) gp_flags |= 1u << k;
  if (swOn("ux0:data/nativedraw/mirror_wait")) gp_flags |= 32;  // the PSP flip waits for the mirror copy
  DEV(no_capture = swOn("ux0:data/nativedraw/no_capture");)
  DEV(pv_on = !swOn("ux0:data/nativedraw/no_lookahead");)
  reg_on = swOn("ux0:data/nativedraw/draw");  // frame record
  if (reg_on) regInit();
  clog("log level: %s (ux0:data/nativedraw/log_level: minimum, medium or extreme) | switches%s%s%s%s%s%s%s%s\n", level == 2 ? "extreme" : level ? "medium" : "minimum", gp_flags & 1 ? " no_gate" : "", gp_flags & 2 ? " no_mirror" : "",
       gp_flags & 4 ? " no_idle" : "", gp_flags & 8 ? " gate_sync" : "", gp_flags & 32 ? " mirror_wait" : "", no_capture ? " no_capture" : "", pv_on ? "" : " no_lookahead", gp_flags || no_capture || !pv_on ? "" : ": none (all normal)");
  for (uint32_t a = 0x08000000u; a < 0x0C000000u && nr < 4;) {  // PSP RAM ranges mapped in this process
    SceKernelMemBlockInfo in; in.size = sizeof in;
    int e = sceKernelGetMemBlockInfoByAddr((void *)PSP(a), &in);
    uint32_t base = (uint32_t)in.mappedBase, end = base + in.mappedSize;
    if (e < 0 || end <= PSP(a)) { a += 0x100000; continue; }
    uint32_t lo = base > PSP(a) ? base - 0x68000000u : a, hi = end - 0x68000000u;
    if (hi > 0x0C000000u) hi = 0x0C000000u;
    clog("PSP RAM readable: %08x-%08x (Vita block %08x, %u KB)\n", lo, hi, base, in.mappedSize >> 10);
    if (nr && rhi[nr - 1] == lo) rhi[nr - 1] = hi;
    else { rlo[nr] = lo; rhi[nr++] = hi; }
    a = hi;
  }
  initCls();
  if (!okRam(0x0A000000u) || !okRam(0x08800000u)) { clog("PSP RAM does not cover the mailbox or the game: no capture\n"); return; }
  if (sceIoGetstat("ux0:data/nativedraw/no_memo", &st) >= 0) clog("sublist memory: off (no_memo)\n");
  else {
    void *mm = NULL;
    uint32_t sz = 0;
    SceUID mb = -1;
    for (; MEMO_N >= 512 && !mm; MEMO_N /= 2, POOL_N /= 2, memo_sh++) {  // (if memory is short: half)
      sz = (MEMO_N * sizeof(Memo) + POOL_N * 64 + 0xFFF) & ~0xFFF;
      mb = sceKernelAllocMemBlock("nativedraw_memory", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, sz, NULL);
      if (mb >= 0 && sceKernelGetMemBlockBase(mb, &mm) < 0) mm = NULL;
      if (mm) break;
    }
    if (!mm) clog("sublist memory: out of memory (%08x)\n", mb);
    else {
      sceClibMemset(mm, 0, MEMO_N * sizeof(Memo));
      memo = mm; pool = (uint32_t *)((char *)mm + MEMO_N * sizeof(Memo));
      memo_chk = sceIoGetstat("ux0:data/nativedraw/memo_check", &st) >= 0;
      __sync_synchronize();
      memo_on = 1;
      clog("sublist memory: %u sublists, pool of %u KB (%u KB total)%s\n", MEMO_N, POOL_N * 64 >> 10, sz >> 10,
           memo_chk ? "; CHECK of all (memo_check)" : "; check of 1 in 32");
    }
  }
  // The capturer runs on any of the 3 cores (pinned to one it gets less time: it shares it with audio or the
  // Media Engine). Switch capture_core ("0", "1" or "2") pins it if a game needs that.
  char nc[8] = { 0 }; uint32_t cmask = 0x70000; const char *cname = "free (0, 1 or 2)";
  SceUID nf = sceIoOpen("ux0:data/nativedraw/capture_core", SCE_O_RDONLY, 0);
  if (nf >= 0) { sceIoRead(nf, nc, 7); sceIoClose(nf); }
  if (nc[0] >= '0' && nc[0] <= '2') { cmask = 0x10000 << (nc[0] - '0'); nc[1] = 0; cname = nc; }
  SceUID t = sceKernelCreateThread("nativedraw_capture", capThread, 0x61, 0x4000, 0, cmask, NULL);
  // Priority 0x61: just below the emulator threads at 0x60 (ScePspemuRemoteMe: Media Engine, decodes the
  // game audio; TitleInfo, AvcAuInput) and the audio output ones (0x40-0x43). Higher cuts the audio; much
  // lower starves the capturer.
  if (t >= 0) sceKernelStartThread(t, 0, NULL);
  cap_uid = t;
  clog("threads: capture %08x (core %s, priority 0x61, below the audio threads; ux0:data/nativedraw/capture_core) log %08x; emulator threads seen: %d\n", t, cname, r, emu_n);
  // mirror thread: priority 0xBE, below the readers
  SceUID et = sceKernelCreateThread("nativedraw_mirror", mirThread, 0xBE, 0x2000, 0, 0x10000 /* core 0 */, NULL);
  if (et >= 0) sceKernelStartThread(et, 0, NULL);
  clog("VRAM mirror: thread %08x (core 0, priority 0xBE); copies at %08x and %08x\n", et, MIRROR, MIRROR2);
#ifdef NP_DEV
  probe_ge = sceIoGetstat("ux0:data/nativedraw/ge_timing", &st) >= 0;
  clog("T6 (NativeLink thread watching the GE): %s\n", probe_ge ? "requested (ge_timing)" : "off (create ux0:data/nativedraw/ge_timing to measure)");
#endif
  if (sceIoGetstat("ux0:data/nativedraw/capture_no_readers", &st) >= 0) { clog("readers: off (capture_no_readers)\n"); return; }
  void *m = NULL;
  SceUID blk = sceKernelAllocMemBlock("nativedraw_lines", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (CL_N * 68 + 0xFFF) & ~0xFFF, NULL);
  if (blk < 0 || sceKernelGetMemBlockBase(blk, &m) < 0) { clog("readers: no memory for the cache (%08x)\n", blk); return; }
  cl_tag = (volatile uint32_t *)((char *)m + CL_N * 64);
  for (int i = 0; i < CL_N; i++) cl_tag[i] = 0xFFFFFFFFu;
  __sync_synchronize();
  cl_data = m;
  static int ids[2] = { 0, 1 };
  static const int cores[2] = { 0x10000, 0x40000 };  // core 0 (emulator) and 2 (Media Engine): spare time only
  for (int k = 0; k < 2; k++) {
    SceUID lt = sceKernelCreateThread(k ? "nativedraw_reader1" : "nativedraw_reader0", rdrThread, 0xB0, 0x2000, 0, cores[k], NULL);
    if (lt >= 0) sceKernelStartThread(lt, sizeof(int), &ids[k]);
    rdr_uid[k] = lt;
    clog("reader %d: %08x (core %d, priority 0xB0)\n", k, lt, k ? 2 : 0);
  }
}
