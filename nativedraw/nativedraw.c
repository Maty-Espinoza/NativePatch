// nativedraw.c: startup of NativeDraw (nativedraw.suprx), the Vita side plugin.
// User plugin in the Adrenaline process (taiHEN, section *PSPEMUCFW of ur0:tai/config.txt). NativeLink (PSP side)
// notes in a mailbox in PSP RAM what the game asks of the GE; capture.c walks and records it, and
// presenter.c draws it with the Vita GPU. This file: startup (module_start), own log (alog), the NativeLink log
// (written by the Vita, so the PSP does not wait for the card), the NativeLink version for the menu and the hook
// that notes the emulator threads. With ux0:data/nativedraw/off nothing is hooked.
// Logs: ux0:pspemu/nativedraw.log (this one), nativedraw_capture.log (capture.c) and nativelink.log (NativeLink's).
// NativeLink log channel (page 0x0A7FF000 of PSP RAM, see nativelink/log.h): +0 "GPV1" (NativeLink),
// +0x20 "LOGV" (this plugin: serves the mailbox), +0x40 seq, done, bytes (text at 0x0A7FA000).
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <taihen.h>
#include <stdarg.h>
#include "version.h"

#define LOGF "ux0:pspemu/nativedraw.log"
#define LOGP "ux0:pspemu/nativedraw_prev.log"
#define PSP(a) (0x68000000u + ((a) & 0x0FFFFFFFu))  // uncached view of PSP RAM (CONVERT_ADDRESS)
#define DESC ((volatile uint32_t *)PSP(0x0A7FF000u))

static int nlog, writer;  // (writer: the writer thread runs; otherwise the logs are written directly)
// Own log (ux0:pspemu/nativedraw.log): each line with the ms since the process started; ~1000 lines at most.
// The line goes to a ring (aring, aw/ar = bytes written/read) and the writer thread moves it to the card: writing
// there blocks for tens of ms and some callers run in Adrenaline's drawing thread. A line that does not fit is dropped.
static char aring[0x10000];
static volatile uint32_t aw, ar;
static volatile int alock, flock;  // (several threads log at once; the writer thread and alogNew both flush)
static void aFlush(void) {  // moves what the ring holds to the file
  while (__sync_lock_test_and_set(&flock, 1)) sceKernelDelayThread(100);
  uint32_t w = aw;
  __sync_synchronize();
  if (ar != w) {
    SceUID fd = sceIoOpen(LOGF, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    while (ar != w) {
      uint32_t at = ar % sizeof aring, k = w - ar;
      if (k > sizeof aring - at) k = sizeof aring - at;
      if (fd >= 0) sceIoWrite(fd, aring + at, k);
      ar += k;
    }
    if (fd >= 0) sceIoClose(fd);
  }
  __sync_lock_release(&flock);
}
void alog(const char *fmt, ...) {
  if (nlog++ > 4000) return;
  char b[512];
  va_list ap; va_start(ap, fmt);
  int n = sceClibSnprintf(b, 16, "%7u ", sceKernelGetProcessTimeLow() / 1000);
  n += sceClibVsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap);
  if (n > (int)sizeof b - 1) { n = sizeof b - 1; b[n - 1] = '\n'; }  // (cut: the newline is kept)
  if (!writer) {
    SceUID fd = sceIoOpen(LOGF, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) { sceIoWrite(fd, b, n); sceIoClose(fd); }
    return;
  }
  while (__sync_lock_test_and_set(&alock, 1)) sceKernelDelayThread(50);
  if (aw - ar + n <= sizeof aring) {
    for (int i = 0; i < n; i++) aring[(aw + i) % sizeof aring] = b[i];
    __sync_synchronize();
    aw += n;
  }
  __sync_lock_release(&alock);
}
static volatile int datum __attribute__((aligned(64))) = 1;  // non-empty .data (valid RW segment)
// No libc: the compiler may generate calls to these two (structure copies in capture.c)
void *memset(void *d, int c, unsigned n) { char *p = d; while (n--) *p++ = c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { char *p = d; const char *q = s; while (n--) *p++ = *q++; return d; }

// --- NativeLink log: seq is raised by the PSP, done is matched by this plugin when copying ---
#define LOGV 0x56474F4Cu
#define VLOG 16
void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
// The text is copied by DMA to a ring (lring, lw/lr = bytes written/read) and a thread writes it to the card
// later: the PSP does not wait for the write (opening, writing and closing is slow).
static char lring[0x40000];
static volatile uint32_t lw, lr;
// Copies n bytes from PSP RAM (a) to the ring, waiting if full.
static void logPut(uint32_t a, uint32_t n) {
  while (n) {
    while (lw - lr >= sizeof lring) sceKernelDelayThread(1000);
    uint32_t at = lw % sizeof lring, k = sizeof lring - at, room = sizeof lring - (lw - lr);
    if (k > room) k = room;
    if (k > n) k = n;
    sceDmacMemcpy(lring + at, (void *)PSP(a), k);
    a += k; lw += k; n -= k;
  }
}
static void logFlushOut(void) {  // (writer thread) moves what is in the ring to nativelink.log
  if (lr == lw) return;
  SceUID fd = sceIoOpen("ux0:pspemu/nativelink.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  while (lr != lw) {
    uint32_t at = lr % sizeof lring, k = lw - lr;
    if (k > sizeof lring - at) k = sizeof lring - at;
    if (fd >= 0) sceIoWrite(fd, lring + at, k);
    lr += k;
  }
  if (fd >= 0) sceIoClose(fd);
}
static int writerThread(SceSize args, void *argp) {  // writer thread: empties both rings every 2 ms
  for (;;) { logFlushOut(); aFlush(); sceKernelDelayThread(2000); }
  return 0;
}
static uint32_t nlogs, ram_ok;  // log chunks served; PSP RAM is already mapped
// NativeLink version for the menu: the first line of nativelink.log ("000000 NativeLink X.Y.Z (build ..."). The PSP
// writes that line directly to the card (before the Vita serves the log), so it is read from the file. Only
// if NativeLink is active ("GPV1" in the mailbox): otherwise the file is from the previous game.
static char nl_ver[16];
const char *nlVersion(void) {
  if (!ram_ok || DESC[0] != 0x31565047) return 0;
  char b[96] = { 0 };
  SceUID fd = sceIoOpen("ux0:pspemu/nativelink.log", SCE_O_RDONLY, 0);
  if (fd < 0) return 0;
  sceIoRead(fd, b, sizeof b - 1); sceIoClose(fd);
  for (int i = 0; i + 12 < (int)sizeof b - 1; i++)
    if (!sceClibStrncmp(b + i, "NativeLink ", 11)) {
      int k = 0;
      for (i += 11; k < 15 && b[i] && b[i] != ' ' && b[i] != '\n'; ) nl_ver[k++] = b[i++];
      nl_ver[k] = 0;
      return k ? nl_ver : 0;
    }
  return 0;
}
// Serves the log mailbox: if NativeLink raised seq, moves its bytes (text at 0x0A7FA000, up to 16 KB) to the ring (or
// directly to the file without writer thread) and replies by matching done.
static void serveLog(void) {
  uint32_t seq = DESC[VLOG];
  if (seq == DESC[VLOG + 1]) return;
  __sync_synchronize();
  uint32_t n = DESC[VLOG + 2];
  if (n > 0x4000) n = 0x4000;
  if (writer) logPut(0x0A7FA000u, n);
  else {
    SceUID fd = sceIoOpen("ux0:pspemu/nativelink.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) { sceIoWrite(fd, (void *)PSP(0x0A7FA000u), n); sceIoClose(fd); }
  }
  if (!nlogs++) alog("NativeLink log: written by the Vita (first chunk %u bytes)\n", n);
  DESC[VLOG + 1] = seq;
}

void capStart(void);  // capture.c
void presStart(void), presStop(void);  // presenter.c

// Main thread: starts the presenter, waits until PSP RAM is mapped, starts the capturer and from then on
// serves the NativeLink log (every 0.2 ms if there is work, otherwise every 2 ms).
static int mainThread(SceSize args, void *argp) {
  SceIoStat st;
  if (sceIoGetstat("ux0:data/nativedraw/off", &st) >= 0) {
    alog("ux0:data/nativedraw/off exists: nothing is hooked\n");
    return sceKernelExitDeleteThread(0);
  }
  presStart();  // presenter and L+R+START menu
  // PSP RAM is mapped into the process after the plugin loads: reading it earlier closes Adrenaline.
  // Waits for the block to exist.
  for (int mapped = 0;;) {  // only writes if NativeLink left its mark (the zone belongs to the XMB outside games)
    SceKernelMemBlockInfo in; in.size = sizeof in;
    uint32_t a = (uint32_t)DESC;
    if (!mapped && (sceKernelGetMemBlockInfoByAddr((void *)a, &in) < 0 || a < (uint32_t)in.mappedBase ||
                    a + 16 > (uint32_t)in.mappedBase + in.mappedSize)) {
      sceKernelDelayThread(100000);
      continue;
    }
    if (!mapped) capStart();  // the capture thread waits for the NativeLink mailbox
    mapped = ram_ok = 1;  // (checked once: PSP RAM stays mapped while the process lives)
    int busy = 0;
    if (DESC[0] == 0x31565047) {  // "GPV1"
      if (DESC[8] != LOGV) DESC[8] = LOGV;
      busy = DESC[VLOG] != DESC[VLOG + 1];
      serveLog();
    }
    sceKernelDelayThread(busy ? 200 : 2000);
  }
}

// Hook on ScePspemu's sceKernelCreateThread: logs each thread the emulator creates (priority, cores) and
// keeps it in emu_th/emu_aff so capture.c can measure how much CPU each one uses. Does not see threads created before
// NativeDraw loads.
static tai_hook_ref_t ref_ct;
static SceUID hk_ct = -1;
SceUID emu_th[256]; int emu_aff[256], emu_n;  // (read by capture.c: EMU_N must be equal)
static SceUID hookCT(const char *name, SceKernelThreadEntry entry, int prio, int stack, SceUInt attr, int aff,
                     const SceKernelThreadOptParam *opt) {
  SceUID r = TAI_CONTINUE(SceUID, ref_ct, name, entry, prio, stack, attr, aff, opt);
  alog("ScePspemu thread: %s priority %x cores %x stack %x attributes %x -> %08x\n", name ? name : "?", prio, aff, stack, attr, r);
  if (r >= 0 && emu_n < 256) { emu_th[emu_n] = r; emu_aff[emu_n] = aff; emu_n++; }
  return r;
}

// Starts a new log: the current one becomes nativedraw_prev.log and the new one begins with the version line
// --- Spare block ---
// ScePspemu maps a block of 128 MB of uncached LPDDR that nothing uses, while the memory the process can reserve runs out
// at about 54 MB. What the GPU reads goes here: ndExoGet hands out pieces of it, skipping the first 32 MB as a margin.
#define EXO_SKIP (32 << 20)
static uint32_t exo_b, exo_p, exo_e;
static volatile int exo_lock, exo_done;
// Looks for the block once: 30 s at most, ScePspemu maps it several seconds after this plugin starts
static void exoFind(void) {
  for (int t = 0; t < 300 && !exo_b; t++) {
    for (uint32_t a = 0x74000000; a < 0x7E000000; a += 0x100000) {
      SceKernelMemBlockInfo in;
      sceClibMemset(&in, 0, sizeof in); in.size = sizeof in;
      if (sceKernelGetMemBlockInfoByAddr((void *)a, &in) < 0 || in.mappedSize < (64 << 20) || in.type != SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE) continue;
      exo_b = (uint32_t)in.mappedBase; exo_p = exo_b + EXO_SKIP; exo_e = exo_b + in.mappedSize;
      break;
    }
    if (!exo_b) sceKernelDelayThread(100000);
  }
  alog("spare block: %08x, %u MB usable\n", exo_b, exo_b ? (exo_e - exo_p) >> 20 : 0);
}
// n bytes of the block aligned to 256 KB, 0 if there is no block or no room
void *ndExoGet(uint32_t n) {
  while (__sync_lock_test_and_set(&exo_lock, 1)) sceKernelDelayThread(100);
  if (!exo_done) { exoFind(); exo_done = 1; }
  uint32_t a = (exo_p + 0x3FFFF) & ~0x3FFFFu;
  void *r = exo_b && a + n <= exo_e ? (void *)a : 0;
  if (r) exo_p = a + n;
  __sync_lock_release(&exo_lock);
  return r;
}
// (GXM initialized) Maps the usable part for the GPU
int ndExoMap(void) {
  ndExoGet(0);
  if (!exo_b) return 0;
  int r = sceGxmMapMemory((void *)(exo_b + EXO_SKIP), exo_e - exo_b - EXO_SKIP, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
  alog("spare block: mapped for the GPU %08x\n", r);
  return r;
}
#ifdef NP_DEV
// Reserves a block and logs its name, type and size (the sizing probes stay out of the log)
SceUID ndAlloc(const char *name, SceKernelMemBlockType type, SceSize size, SceKernelAllocMemBlockOpt *opt) {
  SceUID b = sceKernelAllocMemBlock(name, type, size, opt);
  if (sceClibStrncmp(name, "nativedraw_test", 16)) alog("memory block: %s type %08x %u KB -> %08x\n", name, type, size >> 10, b);
  return b;
}
#endif
void alogNew(void) {
  if (writer) aFlush();
  sceIoRemove(LOGP);
  sceIoRename(LOGF, LOGP);
  nlog = 0;
  alog("NativeDraw " NATIVEDRAW_VERSION " built %s %s (datum %d)\n", __DATE__, __TIME__, datum);
}

void _start() __attribute__((weak, alias("module_start")));
// Plugin startup: NativeLink log writer thread, emulator threads hook and main thread.
int module_start(SceSize args, void *argp) {
  alogNew();
  SceUID wt = sceKernelCreateThread("nativedraw_writer", writerThread, 0xA0, 0x2000, 0, 0, NULL);
  if (wt >= 0 && sceKernelStartThread(wt, 0, NULL) >= 0) writer = 1;
  alog("NativeLink log: %s\n", writer ? "ring + writer thread" : "direct write");
  hk_ct = taiHookFunctionImport(&ref_ct, "ScePspemu", 0xCAE9ACE6, 0xC5C11EE7, hookCT);
  alog("ScePspemu sceKernelCreateThread hook: %08x\n", hk_ct);
  SceUID th = sceKernelCreateThread("nativedraw", mainThread, 0x60, 0x2000, 0, 0, NULL);
  if (th >= 0) sceKernelStartThread(th, 0, NULL);
  return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize args, void *argp) {  // releases the hook and shuts down the presenter
  if (hk_ct >= 0) taiHookRelease(hk_ct, ref_ct);
  presStop();
  return SCE_KERNEL_STOP_SUCCESS;
}
