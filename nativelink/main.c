// NativeLink: PSP-side plugin. Adrenaline loads it with the game (seplugins/plugins.txt). It does not change what the
// game draws: it hooks the functions the game uses to hand its lists to the GE (enqueue, stall, sync) and the flip, and
// notes each call in a mailbox in PSP RAM (SB, 0x0A000000). NativeDraw (Vita side) reads the mailbox and walks the same
// lists from PSP RAM. It also provides:
//   - VRAM mirror: the Vita cannot see PSP VRAM; on each flip the GE copies the pages the Vita asks for to RAM;
//   - gate: if the Vita is behind, the game waits for it (up to 5 ms) before reusing a list;
//   - telemetry for the records (T4/T6, free CPU, cost per frame, list life) and the log (log.c).
// Buttons when opening a game: L or SELECT = NativeLink off (the game runs as if without plugin); R = synchronous log.
// Mailbox and shared zone map: docs/dev/<language>/how-it-works.md; levels and switches: docs/dev/<language>/logs-and-switches.md.
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspge.h>
#include <pspgu.h>
#include <stdio.h>
#include <string.h>
#include <pspsysmem_kernel.h>
#include <systemctrl.h>

#include "log.h"

PSP_MODULE_INFO("NativeLink", 0x1007, 1, 0);

#define DRAW_NATIVE 0xABCDEF00 // != 0: Adrenaline shows the image at 0x0A400000 instead of the game's
#define ZONE 0x0A000000        // 16 MB for the plugins (mailbox, mirrors, own lists)
#define STATS_EVERY 600        // frames between stats lines (~10 s)

static int passive; // L or SELECT when opening the game, or the game uses the zone: nothing is noted
static struct { u32 lists, heads, stalls, frames, frame_us, frame_max, slow60, slow30, long_logged; } st; // (log)
static u32 last_flip;

// Original functions (called by the hooks)
int (*_sceGeListUpdateStallAddr)(int qid, void *stall);
int (*_sceGeListEnQueue)(const void *list, void *stall, int cbid, PspGeListArgs *arg);
int (*_sceGeListEnQueueHead)(const void *list, void *stall, int cbid, PspGeListArgs *arg);
int (*_sceDisplaySetFrameBuf)(void *topaddr, int bufferwidth, int pixelformat, int sync);

// --- Mailbox shared with NativeDraw ----------------------------------------------------------------------------------
// The game runs as if without plugin (real VRAM, lists untouched; Adrenaline shows its image). The hooks only note
// what the game asks of the GE and NativeDraw's capturer walks the same lists reading PSP RAM.
// 32-bit words at 0x0A000000 (zone reserved for the plugins, see zoneCheck), uncached:
//   SB[0] "SOMB"  SB[1] version  SB[2] events written (PSP)  SB[3] last list captured (Vita, sequence number)
//   SB[4] Vita heartbeat  SB[5] events read (Vita)  SB[6] lists enqueued (PSP)
//   SB[0x400] ring of SB_N 8-word events: type | datum << 8, a, b, c, PSP time (us), list sequence number
//   (enqueues only) or extra (EV_FRAME, 3 words)
// EV_FRAME (before each EV_FLIP), what each part costs the game in the frame: datum = waits at the gate |
// syncs with the Vita behind that did not wait (sin_compuerta) << 8; a = frame (us, flip to flip), b = gate wait
// (us), c = wait for the GE inside sceGeDrawSync/ListSync (us); e[5] = free-CPU thread spins
// (SB[0x4D]: spins per ms = 100 %), e[6] = mirror: flip CPU (us) | pages << 16, e[7] = hooks (us in sbPost).
// Switches NativeDraw sets in SB[0x4A]: 1 sin_compuerta (does not wait for the Vita, only counts), 2 no
// mirror, 4 no free-CPU thread, 8 gate always at the sync, 16 full mirror (all requested data pages on
// every flip), 32 the flip waits for the mirror copy to finish.
// SB[0x4E] = NativeDraw record level (0 minimal, 1 medium, 2 extreme): EV_FRAME and the free-CPU thread from
// medium up; list life (EV_LIFE) and the game CPU events (T4: EV_CPU, EV_CPUSUM) only at extreme.
#define SB ((volatile u32 *)(0x0A000000 | 0x40000000))
#define SB_MAGIC 0x424D4F53 // "SOMB"
#define SB_N 4096
// Ring event types (NativeDraw uses the same numbers)
enum { EV_ENQ = 1, EV_HEAD, EV_STALL, EV_DSYNC, EV_LSYNC, EV_FLIP, EV_GEDONE, EV_SYNCEND, EV_CPU, EV_CPUSUM, EV_FRAME, EV_LIFE };
// active: not opened with L or SELECT and the zone is free; mode_done: modeDecide already ran
static int active, mode_done;
static volatile u32 spin, spin_cal;  // free-CPU thread: spins and calibration (spins per ms)
// Frame cost to the game: fr, the current frame (EV_FRAME); frs, the sum for the log
static struct { u32 gate_us, gate_n, nowait, sync_us, post_us, mir_us, mir_pages; } fr;
static struct { u32 frames, frame_us, spin, gate_us, gate_n, nowait, sync_us, post_us, mir_us, mir_pages; } frs;
static u32 sb_x[3];  // e[5..7] of EV_FRAME
// List life: NativeDraw leaves a sample of 64-byte lines of the last list it captured (SB[0x50] sample number,
// 0 while writing it; SB[0x51] how many, up to 32; SB[0x52] how many are top level; SB[0x53]
// list sequence number; SB[0x60+k] address, SB[0x80+k] FNV hash of its 16 words, like lineHash in captura.c).
// Before each enqueue and on each sync, NativeLink recomputes the hashes through the cache (what the game CPU sees) and
// notes EV_LIFE: datum = 0 enqueue / 1 drawsync / 2 listsync, a = sample number, b = changed top-level lines |
// changed sublist lines << 8, c = last list enqueued. The same sample serves the gate's
// self-check.
static int (*_sceGeGetReg)(int); // NULL if not found (no T6)
// 1 if the GE has a list in progress, 0 if not, 2 if there is no sceGeGetReg. From a hook it needs k1 = 0.
static u32 geBusy(void) {
  if (!_sceGeGetReg) return 2;
  int k1 = pspSdkSetK1(0);
  u32 e = _sceGeGetReg(3) & 1;
  pspSdkSetK1(k1);
  return e;
}
static u32 sb_ord; // sequence number of the last list enqueued
static struct { u32 qid, ord; } sb_q[16]; // last lists, for the sceGeListSync gate
static struct { u32 ev, syncs, late, wait_us, wait_max, timeouts, in_sync_us, lsyncs, llate; } sbs; // (log)

// Notes an event in the ring: fills the entry, then raises SB[2] (the Vita only reads up to SB[2]). Enqueues get a
// sequence number (SB[6]) and are stored in sb_q; EV_FRAME also carries sb_x. Interrupts are off because it is
// called by hooks on any thread and by the T6 thread.
static void sbPost(u32 type, u32 aux, u32 a, u32 b, u32 c) {
  int intr = pspSdkDisableInterrupts();
  u32 h = SB[2];
  volatile u32 *e = SB + 0x400 + (h % SB_N) * 8;
  e[0] = type | aux << 8; e[1] = a; e[2] = b; e[3] = c; e[4] = sceKernelGetSystemTimeLow();
  if (type <= EV_HEAD) {
    e[5] = SB[6] = ++sb_ord;
    sb_q[sb_ord & 15].qid = c; sb_q[sb_ord & 15].ord = sb_ord;
  } else if (type == EV_FRAME) {
    e[5] = sb_x[0]; e[6] = sb_x[1]; e[7] = sb_x[2];
  }
  asm volatile("sync"); // the entry before the counter
  SB[2] = h + 1;
  sbs.ev++;
  fr.post_us += sceKernelGetSystemTimeLow() - e[4]; // cost of noting (e[7] of EV_FRAME)
  pspSdkEnableInterrupts(intr);
}
// Recomputes the hashes of NativeDraw's sample and counts the lines that changed (*ptop top level, *psub
// sublists). Returns the sample number, or 0 if there is no sample or the Vita changed it during the check.
static u32 lifeHash(u32 *ptop, u32 *psub) {
  u32 sq = SB[0x50], n = SB[0x51], nt = SB[0x52], k, i, top = 0, sub = 0;
  *ptop = *psub = 0;
  if (!sq || n > 32) return 0;
  for (k = 0; k < n; k++) {
    u32 a = SB[0x60 + k], h = 2166136261u;
    if (a < 0x08000000 || a >= 0x0A000000) continue;
    const u32 *w = (const u32 *)(a & ~63);
    for (i = 0; i < 16; i++) h = (h ^ w[i]) * 16777619u;
    if (h != SB[0x80 + k]) { if (k < nt) top++; else sub++; }
  }
  *ptop = top; *psub = sub;
  return SB[0x50] == sq ? sq : 0; // (if the Vita changed the sample meanwhile, it is not valid)
}
// At extreme level: notes EV_LIFE (kind: 0 enqueue, 1 drawsync, 2 listsync) if there is a valid sample
static void lifeCheck(u32 kind) {
  u32 top, sub, sq;
  if (active && SB[0x4E] >= 2 && (sq = lifeHash(&top, &sub))) sbPost(EV_LIFE, kind, sq, top | sub << 8, sb_ord);
}
// Zeroes the mailbox (header, switches, mirror maps) and writes "SOMB" last, once it is ready
static void sbInit(void) {
  int i;
  for (i = 0; i < 0x50; i++) SB[i] = 0;
  for (i = 0x200; i < 0x400; i++) SB[i] = SB[0x8200 + i] = 0; // (last copy of each page in both mirrors)
  SB[1] = 5; SB[6] = sb_ord; // mailbox version: NativeDraw enables each part according to this number
  SB[0x4D] = spin_cal; SB[0x54] = 0;
  SB[0] = SB_MAGIC;
}

static int sbAlive(void) { // the Vita capturer responds (its heartbeat changed less than 0.5 s ago)
  static u32 hb, hb_t;
  u32 now = sceKernelGetSystemTimeLow();
  if (SB[4] != hb) { hb = SB[4]; hb_t = now; }
  return hb && now - hb_t < 500000;
}
// Gate: waits (in 50 us steps, up to 5 ms) until the Vita has captured list 'ord'. Returns 0 if no wait was needed
// (already captured, the Vita does not respond, or sin_compuerta); otherwise the us waited | 1.
static u32 sbGate(u32 ord) {
  if (!sbAlive() || (int)(SB[3] - ord) >= 0) return 0;
  if (SB[0x4A] & 1) { fr.nowait++; return 0; } // sin_compuerta: only counts
  u32 t0 = sceKernelGetSystemTimeLow(), w;
  do {
    sceKernelDelayThread(50);
    w = sceKernelGetSystemTimeLow() - t0;
  } while ((int)(SB[3] - ord) < 0 && w < 5000);
  sbs.timeouts += (int)(SB[3] - ord) < 0;
  sbs.wait_us += w;
  if (w > sbs.wait_max) sbs.wait_max = w;
  fr.gate_us += w; fr.gate_n++;
  return w | 1;
}
// --- Gate before enqueuing the next list -------------
// If the game does not rewrite a list until it enqueues the next one, there is no need to wait for the Vita at the
// sync: waiting just before the next enqueue is enough, and the Vita gains the time from the sync to the enqueue. The
// sync only notes which list to wait for (gateSync).
// Self-check (valid for any game): before each enqueue, if NativeDraw's sample (SB[0x50..], see above) is of a list
// already past a sync, its hashes are recomputed: if they changed, the game rewrites the list before enqueuing the
// next one and the Vita could read something the GE did not draw -> the gate returns to the sync.
// Mode (gate_mode, copied to SB[0x54]): 0 at the sync, testing; goes to 1 (before enqueue) after 60 clean checks in a
// row; a change returns it to 0 and 10 changes leave it at 2 (at the sync until the game closes).
// compuerta_sync (SB[0x4A] & 8): always at the sync, but the checks continue (for comparison).
// Only the sample of a list enqueued after the previous sync counts (sync_lo, sync_ord]: an older list may be in
// an area the game already reuses and would give false changes.
// sync_ord / sync_lo: last list past a sync and the one of the previous sync; gate_ord: the one the next enqueue
// waits for; gate_sq: last sample checked; gate_ok: clean checks in a row.
static u32 sync_ord, sync_lo, gate_ord, gate_sq, gate_ok;
static int gate_mode;
static struct { u32 checks, bad, n, us, max; } gts; // (log)
static int gateEnq(void) { return gate_mode == 1 && !(SB[0x4A] & 8); } // 1: the wait goes before enqueuing
static void gateSync(u32 ord) { // at the sync: which list to wait for
  if ((int)(ord - sync_ord) > 0) { sync_lo = sync_ord; sync_ord = ord; }
  if (gateEnq()) gate_ord = sync_ord;
}
static u32 gateBeforeEnq(void) { // before enqueuing: wait (if due) and self-check; returns the wait (us | 1)
  u32 w = 0, sq = SB[0x50], top, sub;
  if (gate_ord) { w = gateEnq() ? sbGate(gate_ord) : 0; gate_ord = 0; }
  if (w) { gts.n++; gts.us += w; if (w > gts.max) gts.max = w; }
  u32 so = SB[0x53];
  // leaves if the sample was already checked or is not of a list after the previous sync
  if (!sq || sq == gate_sq || !sync_ord || (int)(so - sync_ord) > 0 || (int)(so - sync_lo) <= 0 || !sbAlive()) return w;
  if (!(sq = lifeHash(&top, &sub))) return w;
  gate_sq = sq; gts.checks++;
  if (top | sub) {
    static u32 bad_total;
    if (gate_mode != 2) LOG("compuerta: la lista %lu cambio antes de encolar la siguiente (%lu lineas arriba, %lu de sublistas; %lu cambios) -> en el sync%s\n",
                            SB[0x53], top, sub, bad_total + 1, bad_total + 1 >= 10 ? " hasta cerrar el juego" : "");
    gts.bad++; gate_ok = 0; gate_mode = ++bad_total >= 10 ? 2 : 0;
  } else if (!gate_mode && ++gate_ok >= 60) {
    gate_mode = 1;
    LOG("compuerta: 60 comprobaciones limpias seguidas -> antes de encolar la lista siguiente\n");
  }
  SB[0x54] = gate_mode;
  return w;
}
// Hooks of sceGeDrawSync and sceGeListSync (only mode 0, the waiting one): check list life, go through the
// gate (unless it goes before enqueuing), note the sync, call the original and note EV_SYNCEND with the time waited
// for the GE.
int (*_sceGeDrawSync)(int mode);
int (*_sceGeListSync)(int qid, int mode);
int sceGeDrawSyncPatched(int mode) {
  if (!active || mode)
    return _sceGeDrawSync(mode);
  lifeCheck(1);
  u32 w = gateEnq() ? 0 : sbGate(sb_ord), t0; // with the gate before enqueuing, this only notes
  gateSync(sb_ord);
  sbs.syncs++;
  sbs.late += !!w;
  sbPost(EV_DSYNC, mode, w, SB[3], sb_ord);
  u32 busy = geBusy();
  t0 = sceKernelGetSystemTimeLow();
  int r = _sceGeDrawSync(mode);
  u32 dt = sceKernelGetSystemTimeLow() - t0;
  sbs.in_sync_us += dt; fr.sync_us += dt;
  sbPost(EV_SYNCEND, 0, dt, busy, 0); // how long it really waited for the GE, and whether the GE was working at the call
  return r;
}
int sceGeListSyncPatched(int qid, int mode) {
  if (active && !mode) {
    u32 ord = 0, w;
    int i;
    lifeCheck(2);
    for (i = 0; i < 16; i++) // ord: the last list enqueued with that qid
      if (sb_q[i].qid == (u32)qid && (int)(sb_q[i].ord - ord) > 0)
        ord = sb_q[i].ord;
    w = ord && !gateEnq() ? sbGate(ord) : 0;
    if (ord) gateSync(ord);
    sbs.lsyncs++;
    sbs.llate += !!w;
    sbPost(EV_LSYNC, mode, qid, w, SB[3]);
    u32 busy = geBusy(), t0 = sceKernelGetSystemTimeLow();
    int r = _sceGeListSync(qid, mode);
    u32 dt = sceKernelGetSystemTimeLow() - t0;
    fr.sync_us += dt;
    sbPost(EV_SYNCEND, 0, dt, busy, 1);
    return r;
  }
  return _sceGeListSync(qid, mode);
}

// --- T6 and T4 telemetry: when the GE works and what the game CPU leaves ready for it ----
// T6: a thread looks at the GE every ~250 us with sceGeGetReg only while it has work; with no lists it sleeps 2 ms. Off
// unless NativeDraw asks for it (SB[0x47] = 1, file medir_ge). For each
// stretch the GE was busy without pause it notes EV_GEDONE: a = start (enqueue time if the GE was idle),
// b = samples stopped at the stall, c = samples; the event time is the end (+-250 us).
// T4: hooks of the functions the game uses to leave data ready for the GE (flushing a cache range, DMA):
// EV_CPU (datum: 0 WritebackRange, 1 WritebackInvalidateRange, 2 InvalidateRange, 3 WritebackAll, 4
// WritebackInvalidateAll, 5 sceDmacMemcpy, 6 sceDmacTryMemcpy; a = address or dest, b = size or source, c =
// DMA size). Only at extreme level and up to 96 per frame (the rest are only counted); on the flip EV_CPUSUM with the
// frame totals (datum = not noted, a = cache calls, b = KB, c = DMA calls | KB << 16).
static volatile u32 ge_kick; // time of the last enqueue (0: already seen)
static struct { u32 busy_us, runs, run_max, stall_n, n; } ges;  // (log)
// T6 thread. GE registers: 3 (bit 0) = running a list, 5 = current list address, 6 = stall.
static int geThread(SceSize args, void *argp) {
  u32 busy0 = 0, n = 0, nst = 0;
  for (;;) {
    if (!active || !_sceGeGetReg || SB[0x47] != 1) { // only if NativeDraw asks for it (medir_ge)
      busy0 = ge_kick = 0;
      sceKernelDelayThread(100000);
      continue;
    }
    sceKernelDelayThread(busy0 || ge_kick ? 250 : 2000);
    u32 e = _sceGeGetReg(3), t = sceKernelGetSystemTimeLow();
    if (e & 1) {
      // stretch start: the enqueue time, unless it is later than t (the enqueue came after reading t)
      if (!busy0) { busy0 = ge_kick && (int)(t - ge_kick) >= 0 ? ge_kick : t; n = nst = 0; }
      u32 la = _sceGeGetReg(5), st = _sceGeGetReg(6);
      n++;
      if (st && (la & 0x0FFFFFFF) == (st & 0x0FFFFFFF)) nst++;
      ge_kick = 0;
    } else if (busy0 || ge_kick) { // finished (or was so short it was not seen: n = 0)
      u32 b0 = busy0 ? busy0 : ge_kick;
      if ((int)(t - b0) < 0) b0 = t;
      sbPost(EV_GEDONE, 0, b0, nst, n);
      ges.busy_us += t - b0; ges.runs++; ges.stall_n += nst; ges.n += n;
      if (t - b0 > ges.run_max) ges.run_max = t - b0;
      busy0 = ge_kick = 0;
    }
  }
  return 0;
}
// PSP free CPU: lowest-priority thread that spins (spin); it only advances when nobody else wants the CPU. A frame's
// spins against the calibration (spin_cal, from t4t6Install) give the free %. From medium up, unless sin_libre.
static int libreThread(SceSize args, void *argp) {
  for (;;) {
    if (!active || SB[0x4A] & 4 || SB[0x4E] < 1) { sceKernelDelayThread(100000); continue; }
    u32 k;
    for (k = 0; k < 100000; k++) { spin++; asm volatile("" ::: "memory"); }
  }
  return 0;
}
static struct { u32 n, kb, dn, dkb, posted, dropped; } t4;  // of the frame
static struct { u32 n, kb, dn, dkb; } t4s;                  // (log)
static void t4Post(u32 kind, u32 a, u32 b, u32 c) { // notes EV_CPU: only at extreme level and up to 96 per frame
  if (SB[0x4E] < 2) return;
  if (t4.posted < 96) { t4.posted++; sbPost(EV_CPU, kind, a, b, c); } else t4.dropped++;
}
// Originals of UtilsForUser (cache) and sceDmac
static void (*_dcWbR)(const void *, unsigned int), (*_dcWbIR)(const void *, unsigned int), (*_dcIR)(const void *, unsigned int);
static void (*_dcWbA)(void), (*_dcWbIA)(void);
static int (*_dmac)(void *, const void *, unsigned int), (*_dmacTry)(void *, const void *, unsigned int);
// T4 hooks: call the original, then count (and note) the call
static void dcRange(int kind, const void *p, unsigned int n) { if (active) { t4.n++; t4.kb += n >> 10; t4Post(kind, (u32)p, n, 0); } }
void dcWbRPatched(const void *p, unsigned int n) { _dcWbR(p, n); dcRange(0, p, n); }
void dcWbIRPatched(const void *p, unsigned int n) { _dcWbIR(p, n); dcRange(1, p, n); }
void dcIRPatched(const void *p, unsigned int n) { _dcIR(p, n); dcRange(2, p, n); }
void dcWbAPatched(void) { _dcWbA(); if (active) { t4.n++; t4Post(3, 0, 0, 0); } }
void dcWbIAPatched(void) { _dcWbIA(); if (active) { t4.n++; t4Post(4, 0, 0, 0); } }
static void dmacNote(int kind, void *d, const void *s, unsigned int n) { if (active) { t4.dn++; t4.dkb += n >> 10; t4Post(kind, (u32)d, (u32)s, n); } }
int dmacPatched(void *d, const void *s, unsigned int n) { int r = _dmac(d, s, n); dmacNote(5, d, s, n); return r; }
int dmacTryPatched(void *d, const void *s, unsigned int n) { int r = _dmacTry(d, s, n); dmacNote(6, d, s, n); return r; }
static void t4Flip(void) { // on the flip: frame totals
  if (SB[0x4E] >= 2) sbPost(EV_CPUSUM, t4.dropped > 0xFFFFFF ? 0xFFFFFF : t4.dropped, t4.n, t4.kb, (t4.dn & 0xFFFF) | (t4.dkb > 0xFFFF ? 0xFFFF : t4.dkb) << 16);
  t4s.n += t4.n; t4s.kb += t4.kb; t4s.dn += t4.dn; t4s.dkb += t4.dkb;
  memset(&t4, 0, sizeof(t4));
}
// Finds sceGeGetReg and the cache and DMA functions and hooks those it finds; starts the T6 thread and the free-CPU
// thread (after calibrating it). What it found goes to the log.
static void t4t6Install(void) {
  static const char *um[2] = { "sceSystemMemoryManager", "sceKernelUtils" };
  int i, nd = 0, nm = 0;
  _sceGeGetReg = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xB415364D);
  for (i = 0; i < 2 && !_dcWbR; i++) {
    _dcWbR = (void *)FindProc(um[i], "UtilsForUser", 0x3EE30821);
    _dcWbIR = (void *)FindProc(um[i], "UtilsForUser", 0x34B9FA9E);
    _dcIR = (void *)FindProc(um[i], "UtilsForUser", 0xBFA98062);
    _dcWbA = (void *)FindProc(um[i], "UtilsForUser", 0x79D1C3FA);
    _dcWbIA = (void *)FindProc(um[i], "UtilsForUser", 0xB435DEC5);
  }
  if (_dcWbR) { sctrlHENPatchSyscall((u32)_dcWbR, dcWbRPatched); nd++; }
  if (_dcWbIR) { sctrlHENPatchSyscall((u32)_dcWbIR, dcWbIRPatched); nd++; }
  if (_dcIR) { sctrlHENPatchSyscall((u32)_dcIR, dcIRPatched); nd++; }
  if (_dcWbA) { sctrlHENPatchSyscall((u32)_dcWbA, dcWbAPatched); nd++; }
  if (_dcWbIA) { sctrlHENPatchSyscall((u32)_dcWbIA, dcWbIAPatched); nd++; }
  // possible sceDmac module names (tried in order)
  static const char *dm[4] = { "sceDMAManager", "sceDMACManager", "sceDmacManager", "sceDMAC_Driver" };
  for (i = 0; i < 4 && !_dmac; i++) {
    _dmac = (void *)FindProc(dm[i], "sceDmac", 0x617F3FE6);
    _dmacTry = (void *)FindProc(dm[i], "sceDmac", 0xD97F94D8);
    if (_dmac) LOG("T4: sceDmac en %s\n", dm[i]);
  }
  if (_dmac) { sctrlHENPatchSyscall((u32)_dmac, dmacPatched); nm++; }
  if (_dmacTry) { sctrlHENPatchSyscall((u32)_dmacTry, dmacTryPatched); nm++; }
  SceUID th = _sceGeGetReg ? sceKernelCreateThread("NativeLinkGE", geThread, 0x11, 0x800, 0, NULL) : -1;
  if (th >= 0) sceKernelStartThread(th, 0, NULL);
  // Free-CPU calibration: the thread's same loop, timed here (2 million spins) = spins per ms
  u32 k, tc = sceKernelGetSystemTimeLow();
  for (k = 0; k < 2000000; k++) { spin++; asm volatile("" ::: "memory"); }
  tc = sceKernelGetSystemTimeLow() - tc;
  spin_cal = tc ? 2000000u / (tc / 1000 + 1) : 0;
  SceUID tl = sceKernelCreateThread("NativeLinkLibre", libreThread, 126, 0x400, 0, NULL);
  if (tl >= 0) sceKernelStartThread(tl, 0, NULL);
  LOG("CPU libre: %lu vueltas/ms = 100%% (hilo %08lx)\n", spin_cal, (u32)tl);
  LOG("T4/T6: sceGeGetReg %s, hilo del GE %08lx | ganchos de la cache %d de 5, del DMA %d de 2\n", _sceGeGetReg ? "si" : "NO",
      (u32)th, nd, nm);
}

// --- VRAM mirror ---------
// The Vita cannot see real PSP VRAM (Adrenaline does not map it) and some games keep sublists, textures and
// palettes there. NativeDraw requests 4 KB pages in two mailbox bitmaps (SB[0x20..0x2F] "lists" and SB[0x30..0x3F]
// "data") and on each flip the GE copies the requested ones to RAM. Two alternating mirrors: even copies in MIRROR, odd
// in MIRROR2. Each copy has all list pages and up to MIR_DATA data pages (if more, in turns); with
// SB[0x4A] & 16 (NativeDraw sets it when the Vita draws 2D frames) all go, so that none goes stale.
// The Vita reads each list with the last finished copy and notes in SB[0x48] the one it is using: that one is not
// copied over (so it never reads something the GE is overwriting). A final 16-byte copy writes in SB[0x40] the copy
// number: that is how the Vita knows it finished. PSP: SB[0x44] copy requested, SB[0x45] pages copied, SB[0x46] times
// the copy from two frames ago was still in the GE, SB[0x49] times the Vita was using that mirror (totals); SB[0x200..]
// and SB[0x8400..] (512 words each): number of the last copy of each page in MIRROR and MIRROR2.
#define MIRROR 0x0A200000
#define MIRROR2 0x0AC00000
#define MIR_LIST 0x0A100000 // two alternating 8 KB lists (in the reserved zone)
#define MIR_DATA 64 // data pages per copy (256 KB)
void guStart(int cid, void *list); // gu.c
static int mir_qid[2] = { -1, -1 };
static u32 mir_seq, mir_rr, mir_pages, mir_busy, mir_vita; // (the last three: totals, for the Vita)
static struct { u32 copies, pages, busy, vita, over, runs_max; } mirs; // (log)
// On the flip: picks the pages to copy, joins contiguous ones into runs and builds an own GE list with one block copy
// per run (from VRAM to the mirror in turn) plus the final copy that marks SB[0x40]. Does not copy if the copy from
// two frames ago (same mirror) is still in the GE or the Vita is still reading that mirror.
static void mirFlip(void) {
  u32 cp[16], wd[16], i, p, w, b, n = 0, any = 0; // (n: data pages of this copy)
  if (SB[0x4A] & 2) return; // sin_espejo
  for (i = 0; i < 16; i++) { cp[i] = SB[0x20 + i]; wd[i] = SB[0x30 + i] & ~cp[i]; any |= cp[i] | wd[i]; }
  if (!any) return;
  for (i = 0; i < 512; i++) { // lists: all; data: those that fit, starting where the last overflow stopped
    p = (mir_rr + i) & 511; w = p >> 5; b = 1u << (p & 31);
    if (!(wd[w] & b)) continue;
    if (n >= (SB[0x4A] & 16 ? 512 : MIR_DATA)) { mir_rr = p; mirs.over++; break; }
    cp[w] |= b; n++;
  }
  u32 seq = mir_seq + 1, s = seq & 1, u = SB[0x48];
  if (mir_qid[s] >= 0 && _sceGeListSync(mir_qid[s], 1) > 0) { // the one from two frames ago is still in the GE
    mirs.busy++; SB[0x46] = ++mir_busy;
    return;
  }
  if (u && (int)(mir_seq - u) >= 0 && !((seq ^ u) & 1) && sbAlive()) { // the Vita is still reading that mirror
    mirs.vita++; SB[0x49] = ++mir_vita;
    return;
  }
  mir_seq = seq;
  u32 rl = MIR_LIST + s * 0x2000, dst = s ? MIRROR2 : MIRROR, runs = 0;
  volatile u32 *last = SB + (s ? 0x8400 : 0x200);
  guStart(0, (void *)(rl | 0xA0000000));
  for (p = 0; p < 512 && runs < 200;) { // contiguous runs (32 list bytes each)
    if (!(cp[p >> 5] >> (p & 31) & 1)) { p++; continue; }
    u32 p0 = p;
    while (p < 512 && cp[p >> 5] >> (p & 31) & 1) last[p++] = seq;
    sceGuCopyImage(GU_PSM_8888, 0, 0, 1024, p - p0, 1024, (void *)(0x04000000 + p0 * 4096), 0, 0, 1024, (void *)(dst + p0 * 4096));
    mirs.pages += p - p0; mir_pages += p - p0; runs++;
  }
  if (runs > mirs.runs_max) mirs.runs_max = runs;
  ((volatile u32 *)((rl + 0x1FF0) | 0xA0000000))[0] = seq; // the final copy puts it in SB[0x40]
  sceGuCopyImage(GU_PSM_8888, 0, 0, 4, 1, 64, (void *)(rl + 0x1FF0), 0, 0, 64, (void *)0x0A000100);
  sceGuFinish();
  mir_qid[s] = _sceGeListEnQueue((void *)rl, NULL, -1, NULL);
  if (SB[0x4A] & 32 && mir_qid[s] >= 0) _sceGeListSync(mir_qid[s], 0);  // espejo_espera: the game waits for the copy
  mirs.copies++;                                                         // (it cannot change the VRAM meanwhile)
  SB[0x44] = seq; SB[0x45] = mir_pages;
}

// First hook the game calls: game ID to the log, normal Adrenaline image and mailbox ready
static void modeDecide(void) {
  mode_done = 1;
  if (passive)
    return;
  logGameId();
  active = 1;
  *(u32 *)DRAW_NATIVE = 0; // Adrenaline shows the game's image
  sbInit();
  LOG("NativeLink activo: el juego corre sin cambios; buzon de eventos en 0x0A000000 para el capturador de NativeDraw\n");
}

// Stall and enqueue hooks: call the original and note the event. Before enqueuing they go through the gate
// (gateBeforeEnq) and the event datum is the time waited there (us | 1).
int sceGeListUpdateStallAddrPatched(int qid, void *stall) {
  int r = _sceGeListUpdateStallAddr(qid, stall);
  st.stalls++;
  if (active)
    sbPost(EV_STALL, 0, qid, (u32)stall, 0);
  return r;
}

int sceGeListEnQueuePatched(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
  if (!mode_done)
    modeDecide();
  st.lists++;
  u32 gw = active ? gateBeforeEnq() : 0;
  lifeCheck(0); // before enqueuing: is the previous list still intact?
  int r = _sceGeListEnQueue(list, stall, cbid, arg);
  if (active && r >= 0) {
    sbPost(EV_ENQ, gw, (u32)list, (u32)stall, r);
    if (!ge_kick) ge_kick = sceKernelGetSystemTimeLow() | 1;
  }
  return r;
}

int sceGeListEnQueueHeadPatched(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
  if (!mode_done)
    modeDecide();
  st.heads++;
  u32 gw = active ? gateBeforeEnq() : 0;
  int r = _sceGeListEnQueueHead(list, stall, cbid, arg);
  if (active && r >= 0) {
    sbPost(EV_HEAD, gw, (u32)list, (u32)stall, r);
    if (!ge_kick) ge_kick = sceKernelGetSystemTimeLow() | 1;
  }
  return r;
}

// Memory and CPU right now (at start and on each stats line)
static void memSnapshot(const char *why) {
  static int (*freeTotal)(void), (*freeMax)(void), (*cpu)(void), (*bus)(void), init;
  if (!init) {
    init = 1;
    freeTotal = (void *)FindProc("sceSystemMemoryManager", "SysMemUserForUser", 0xF919F628);
    freeMax = (void *)FindProc("sceSystemMemoryManager", "SysMemUserForUser", 0xA291F107);
    cpu = (void *)FindProc("scePower_Service", "scePower", 0xFDB5BFE9);
    bus = (void *)FindProc("scePower_Service", "scePower", 0xBD681969);
  }
  int k1 = pspSdkSetK1(0);
  int ft = freeTotal ? freeTotal() / 1024 : -1, fm = freeMax ? freeMax() / 1024 : -1;
  int c = cpu ? cpu() : -1, b = bus ? bus() : -1;
  pspSdkSetK1(k1);
  LOG("memoria (%s): ram del juego libre=%d KB (bloque mayor %d KB) | cpu=%d MHz bus=%d MHz\n", why, ft, fm, c, b);
}

// Flip hook: restores the mailbox if something erased it, closes the frame (T4 totals, mirror copy, EV_FRAME,
// EV_FLIP), measures fps and every STATS_EVERY frames writes the stats to the log and zeroes them.
int sceDisplaySetFrameBufPatched(void *topaddr, int bufferwidth, int pixelformat, int sync) {
  if (!mode_done)
    modeDecide();
  log_frame++;
  if (log_frame == 30)
    memSnapshot("inicio");
  if (active) {
    if (SB[0] != SB_MAGIC) // (in case something clears the zone, as Adrenaline 8 does when loading the game)
      sbInit();
    if (VDESC[0] != 0x31565047) // "GPV1": NativeDraw writes the log (descriptor mailbox, log.h)
      VDESC[0] = 0x31565047;
    t4Flip();
    int k1e = pspSdkSetK1(0);
    u32 te = sceKernelGetSystemTimeLow(), pg0 = mir_pages;
    mirFlip(); // VRAM copy requested by the Vita
    fr.mir_us += sceKernelGetSystemTimeLow() - te; fr.mir_pages += mir_pages - pg0;
    pspSdkSetK1(k1e);
    { // what this frame cost the game (see EV_FRAME)
      static u32 ft, sp0;
      u32 tn = sceKernelGetSystemTimeLow(), sp = spin, fd = ft ? tn - ft : 0;
      ft = tn;
      sb_x[0] = sp - sp0; sb_x[1] = fr.mir_us | (fr.mir_pages > 0xFFFF ? 0xFFFF : fr.mir_pages) << 16; sb_x[2] = fr.post_us;
      sp0 = sp;
      if (fd && fd < 5000000) {
        frs.frames++; frs.frame_us += fd; frs.spin += sb_x[0]; frs.gate_us += fr.gate_us; frs.gate_n += fr.gate_n;
        frs.nowait += fr.nowait; frs.sync_us += fr.sync_us; frs.post_us += fr.post_us; frs.mir_us += fr.mir_us; frs.mir_pages += fr.mir_pages;
      }
      u32 gn = fr.gate_n > 255 ? 255 : fr.gate_n, nw = fr.nowait > 255 ? 255 : fr.nowait, gu = fr.gate_us, su = fr.sync_us;
      memset(&fr, 0, sizeof(fr));
      if (SB[0x4E] >= 1) sbPost(EV_FRAME, gn | nw << 8, fd, gu, su); // (the cost of this sbPost goes to the next frame)
    }
    sbPost(EV_FLIP, 0, (u32)topaddr, bufferwidth | pixelformat << 16, log_frame);
  }
  // Every distinct buffer the game displays
  LOG_ONCE((u32)topaddr ^ (bufferwidth << 20) ^ (pixelformat << 30), "display %08lx ancho=%d fmt=%d\n", (u32)topaddr, bufferwidth, pixelformat);

  // Time between flips (fps)
  u32 now = sceKernelGetSystemTimeLow(), dt = now - last_flip;
  if (last_flip && dt < 5000000) {
    st.frames++;
    st.frame_us += dt;
    if (dt > st.frame_max)
      st.frame_max = dt;
    st.slow60 += dt > 17500; // under 60 fps
    st.slow30 += dt > 34000; // under 30 fps
    if (dt > 200000 && log_frame > 120 && !st.long_logged) {
      st.long_logged = 1;
      LOG("frame lento: %lu ms\n", dt / 1000);
    }
  }
  last_flip = now;

  if (log_frame % STATS_EVERY == 0) {
    LOG("stats listas=%lu head=%lu stall=%lu\n", st.lists, st.heads, st.stalls);
    memSnapshot("stats");
    if (st.frames) {
      u32 fps10 = st.frames * 100000 / (st.frame_us / 100 + 1); // fps x10, without 64-bit division
      LOG("stats rendimiento: fps=%lu.%lu frame_max=%lu ms bajo60=%lu bajo30=%lu\n",
          fps10 / 10, fps10 % 10, st.frame_max / 1000, st.slow60, st.slow30);
    }
    if (active) {
      u32 s = sbs.syncs ? sbs.syncs : 1;
      LOG("stats sombra: eventos=%lu listas=%lu vita=%s capturadas=%lu (faltan %ld) leidos=%lu\n", sbs.ev, sb_ord,
          sbAlive() ? "si" : "NO", SB[3], (long)(sb_ord - SB[3]), SB[5]);
      LOG("stats sombra sync: drawsync=%lu atrasados=%lu (>5 ms: %lu) espera=%lu us/sync (max %lu) listsync=%lu atrasados=%lu\n",
          sbs.syncs, sbs.late, sbs.timeouts, sbs.wait_us / s, sbs.wait_max, sbs.lsyncs, sbs.llate);
      LOG("stats sombra GE: el juego espera al GE en drawsync %lu us/sync\n", sbs.in_sync_us / s);
      LOG("stats compuerta: %s%s | esperas antes de encolar=%lu (%lu us c/u, max %lu) | autocomprobaciones=%lu cambiaron=%lu\n",
          gate_mode == 1 ? "antes de encolar" : gate_mode ? "en el sync (la lista cambio 10 veces)" : "en el sync (probando)", SB[0x4A] & 8 ? " [compuerta_sync]" : "",
          gts.n, gts.n ? gts.us / gts.n : 0, gts.max, gts.checks, gts.bad);
      memset(&gts, 0, sizeof(gts));
      if (frs.frames) { // impact on the game, per frame
        u32 f = frs.frames, ms = frs.frame_us / 1000 + 1;
        LOG("stats impacto (por frame): frame=%lu us CPU_libre=%lu%% compuerta=%lu us (%lu esperas en 10 s, %lu sin esperar) sync_GE=%lu us ganchos=%lu us espejo=%lu us (%lu paginas)\n",
            frs.frame_us / f, spin_cal ? frs.spin / spin_cal * 100 / ms : 0, frs.gate_us / f, frs.gate_n, frs.nowait, frs.sync_us / f,
            frs.post_us / f, frs.mir_us / f, frs.mir_pages / f);
      }
      memset(&frs, 0, sizeof(frs));
      LOG("stats T6 (por frame): GE ocupado %lu us en %lu tramos (el mas largo %lu us), en stall %lu%% | T4 (por frame): cache %lu (%lu KB) dma %lu (%lu KB)\n",
          ges.busy_us / STATS_EVERY, ges.runs / STATS_EVERY, ges.run_max, ges.n ? ges.stall_n * 100 / ges.n : 0,
          t4s.n / STATS_EVERY, t4s.kb / STATS_EVERY, t4s.dn / STATS_EVERY, t4s.dkb / STATS_EVERY);
      memset(&ges, 0, sizeof(ges)); memset(&t4s, 0, sizeof(t4s));
      u32 pl = 0, pd = 0, i;
      for (i = 0; i < 512; i++) pl += SB[0x20 + (i >> 5)] >> (i & 31) & 1, pd += SB[0x30 + (i >> 5)] >> (i & 31) & 1;
      LOG("stats espejo: pedidas listas=%lu datos=%lu (%lu KB) | copias=%lu paginas=%lu (KB/frame %lu)\n", pl, pd, (pl + pd) * 4,
          mirs.copies, mirs.pages, mirs.pages * 4 / STATS_EVERY);
      LOG("stats espejo: tramos_max=%lu ocupado=%lu vita_usando=%lu no_entraron=%lu terminada=%lu/%lu\n", mirs.runs_max,
          mirs.busy, mirs.vita, mirs.over, SB[0x40], mir_seq);
      mirs.copies = mirs.pages = mirs.busy = mirs.vita = mirs.over = mirs.runs_max = 0;
      memset(&sbs, 0, sizeof(sbs));
    }
    memset(&st, 0, sizeof(st));
  }

  return _sceDisplaySetFrameBuf(topaddr, bufferwidth, pixelformat, sync);
}

// Adrenaline 8 can give the game memory that reaches the plugin zone (MEMSIZE in the SFO, high memory, 40 MB
// homebrew) and creates partition 11 there, where it loads user plugins. Returns 1 if the game reaches 0x0A000000
// (NativeLink cannot work); otherwise reserves the zone in partition 11 so nobody else uses it and returns 0.
static int zoneCheck(void) {
  PspSysmemPartitionInfo pi;
  pi.size = sizeof(pi);
  int r = sceKernelQueryMemoryPartitionInfo(2, &pi);
  u32 finish = r < 0 ? 0 : (pi.startaddr & 0x0fffffff) + pi.memsize;
  LOG("particion del juego: %08x-%08lx (%u KB) [%08x]\n", pi.startaddr, finish, r < 0 ? 0 : pi.memsize >> 10, r);
  PspSysmemPartitionInfo p11;
  p11.size = sizeof(p11);
  int r11 = sceKernelQueryMemoryPartitionInfo(11, &p11);
  LOG("particion 11: %08x (%u KB, attr %x) [%08x]\n", r11 < 0 ? 0 : p11.startaddr, r11 < 0 ? 0 : p11.memsize >> 10,
      r11 < 0 ? 0 : p11.attr, r11);
  if (finish > ZONE)
    return 1;
  SceUID id = sceKernelAllocPartitionMemory(11, "NativeLink", PSP_SMEM_Addr, 0x01000000, (void *)ZONE);
  LOG("reserva de 0x0A000000-0x0B000000 en la particion 11: %08x%s\n", id,
      id >= 0 ? "" : " (Adrenaline 7, o algo ya esta ahi: un plugin de usuario cargado en la particion 11)");
  return 0;
}

// Startup: reads the buttons (L/SELECT off, R synchronous log), checks the shared zone, finds the GE and display
// functions and hooks them. The hooks are installed even if NativeLink is off: then they only call the original.
int module_start(SceSize args, void *argp) {
  SceCtrlData pad;
  sceCtrlPeekBufferPositive(&pad, 1);
  // L or SELECT when opening the game: off (on Adrenaline 8 holding L also disables all plugins)
  passive = !!(pad.Buttons & (PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER));
  logInit(pad.Buttons & PSP_CTRL_RTRIGGER);
  if (passive)
    LOG("apagado (L o SELECT al abrir el juego): el juego corre sin cambios y no se anota nada para NativeDraw\n");
  if (zoneCheck() && !passive) {
    passive = 1;
    LOG("AVISO: el juego usa la zona de los plugins (0x0A000000-0x0B000000) -> apagado\n");
  }
  if (!passive) { // NativeDraw writes the log as soon as it responds (log.h)
    VDESC[0] = 0x31565047; VDESC[1] = 0; // "GPV1"
  }

  _sceGeListUpdateStallAddr = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xE0D68148);
  _sceGeListEnQueue = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xAB49E76A);
  _sceGeListEnQueueHead = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x1C0D95A6);
  _sceGeDrawSync = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xB287BD61);
  _sceGeListSync = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x03444EB4);
  _sceDisplaySetFrameBuf = (void *)FindProc("sceDisplay_Service", "sceDisplay_driver", 0x289D82FE);
  if (!_sceGeListUpdateStallAddr || !_sceGeListEnQueue || !_sceGeListEnQueueHead || !_sceGeDrawSync || !_sceGeListSync ||
      !_sceDisplaySetFrameBuf) {
    LOG("ERROR: no se encontro alguna funcion del GE o de la pantalla: NativeLink no engancha nada\n");
    return 0;
  }
  sctrlHENPatchSyscall((u32)_sceGeListUpdateStallAddr, sceGeListUpdateStallAddrPatched);
  sctrlHENPatchSyscall((u32)_sceGeListEnQueue, sceGeListEnQueuePatched);
  sctrlHENPatchSyscall((u32)_sceGeListEnQueueHead, sceGeListEnQueueHeadPatched);
  sctrlHENPatchSyscall((u32)_sceGeDrawSync, sceGeDrawSyncPatched);
  sctrlHENPatchSyscall((u32)_sceGeListSync, sceGeListSyncPatched);
  t4t6Install(); // (the hooks only note if NativeLink is active)
  sctrlHENPatchSyscall((u32)_sceDisplaySetFrameBuf, sceDisplaySetFrameBufPatched);

  sceKernelDcacheWritebackInvalidateAll();
  sceKernelIcacheClearAll();
  return 0;
}
