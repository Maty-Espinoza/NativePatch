// NativeLink log (ms0:/nativelink.log; the previous session stays in nativelink_prev.log). LOG (log.h) leaves each line
// in a RAM buffer and a thread flushes it every ~1 s, so the game never waits for the card. If NativeDraw serves the
// descriptor mailbox (VDESC, log.h), the flush only copies the text to the shared zone and the Vita writes it; if
// not, it is written with sceIo. Used by main.c (LOG, logInit, logGameId); on the Vita side nativedraw.c serves it.
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspinit.h>
#include <pspintrman.h>
#include <string.h>

#include <systemctrl.h>

#include "log.h"

#define LOG_PATH "ms0:/nativelink.log"
#define LOG_PREV "ms0:/nativelink_prev.log"
#define LOG_MAX  (1024 * 1024) // file limit, so as not to fill the card

u32 log_frame; // raised by the flip hook (main.c)

// --- Channel with NativeDraw (see log.h) ---
static int vita_dead; // did not respond: sceIo is used until the game restarts
static int vitaIo(void) { // 1 if NativeDraw writes the log
  return !vita_dead && VDESC[0] == 0x31565047 && VDESC[8] == 0x56474F4C; // "GPV1" and "LOGV"
}
// Waits for NativeDraw to finish the previous request of mailbox 'box' (done == seq). If it does not respond in 2 s,
// returns 0 and stops using the Vita until the game restarts.
static int vitaWait(int box) {
  u32 t0 = sceKernelGetSystemTimeLow();
  while (VDESC[box] != VDESC[box + 1]) {
    if (sceKernelGetSystemTimeLow() - t0 > 2000000) {
      vita_dead = 1;
      return 0;
    }
    sceKernelDelayThread(1000);
  }
  return 1;
}
static void vitaPost(int box) { // posts the request (data already written)
#ifdef __mips__
  asm volatile("sync"); // the data before the request number
#endif
  VDESC[box]++;
}

static char buf[2][8192]; // double buffer: one fills while the other is written
static int cur, len, dropped, written, sync_mode;
static int flushing; // one flush at a time (the log thread and sync mode can coincide)
static u32 seen[256];     // keys already seen by logOnce

// Appends n bytes to the current buffer (if they do not fit, the line is lost and counted); in sync mode flushes at once
void logWrite(const char *s, int n) {
  int intr = sceKernelCpuSuspendIntr();
  if (len + n <= (int)sizeof(buf[0])) {
    memcpy(buf[cur] + len, s, n);
    len += n;
  } else {
    dropped++;
  }
  sceKernelCpuResumeIntr(intr);

  if (sync_mode)
    logFlush();
}

// Swaps buffers and writes the full one: to the Vita (copies the text to VLOG_BUF and posts the request) or, without
// it, to the end of the file with sceIo. Reports lost lines and stops writing at LOG_MAX.
void logFlush(void) {
  int intr = sceKernelCpuSuspendIntr();
  if (flushing) { // another thread is writing: these lines go out in the next flush
    sceKernelCpuResumeIntr(intr);
    return;
  }
  flushing = 1;
  char *p = buf[cur];
  int n = len, d = dropped;
  cur ^= 1;
  len = dropped = 0;
  sceKernelCpuResumeIntr(intr);

  if ((!n && !d) || written >= LOG_MAX) {
    flushing = 0;
    return;
  }

  if (vitaIo() && vitaWait(VLOG)) { // the Vita writes it: here the text is only copied
    char *q = (char *)(VLOG_BUF | 0x40000000);
    int k = d ? sprintf(q, "-- %d lines lost (buffer full)\n", d) : 0;
    memcpy(q + k, p, n);
    k += n;
    written += n;
    if (written >= LOG_MAX)
      k += sprintf(q + k, "-- log stopped: reached %d KB\n", LOG_MAX / 1024);
    VDESC[VLOG + 2] = k;
    vitaPost(VLOG);
    flushing = 0;
    return;
  }
  int k1 = pspSdkSetK1(0);
  SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT, 0777);
  if (fd >= 0) {
    sceIoLseek(fd, 0, PSP_SEEK_END);
    char t[64];
    if (d)
      sceIoWrite(fd, t, sprintf(t, "-- %d lines lost (buffer full)\n", d));
    sceIoWrite(fd, p, n);
    written += n;
    if (written >= LOG_MAX)
      sceIoWrite(fd, t, sprintf(t, "-- log stopped: reached %d KB\n", LOG_MAX / 1024));
    sceIoClose(fd);
  }
  pspSdkSetK1(k1);
  flushing = 0;
}

// Open hash table of 256 slots: 1 if 'key' was absent (and adds it), 0 if present or the table is full
int logOnce(u32 key) {
  if (!key)
    key = 1; // 0 marks an empty slot
  int i = (key * 2654435761u) >> 24, n, res = 0;
  int intr = sceKernelCpuSuspendIntr();
  for (n = 0; n < 256; n++, i = (i + 1) & 255) {
    if (seen[i] == key)
      break;
    if (!seen[i]) {
      seen[i] = key;
      res = 1;
      break;
    }
  }
  sceKernelCpuResumeIntr(intr);
  return res; // if the table fills up, new events are not logged
}

// Game ID to the log, from the game data kept by the kernel (sceKernelGetGameInfo, +0x44 "ULUS10450"; the same
// Adrenaline uses for its titleid). Called from the game's first hook, where reading the disc is not safe: the game
// may not have it ready yet.
void logGameId(void) {
  static char id[11] = "?";
  char *(*info)(void) = (void *)sctrlHENFindFunction("sceSystemMemoryManager", "SysMemForKernel", 0xEF29061C);
  char *g = info ? info() : NULL;
  if (g && g[0x44] >= 'A' && g[0x44] <= 'Z') {
    g += 0x44;
    memcpy(id, g, 4);
    id[4] = '-';
    memcpy(id + 5, g + (g[4] == '-' ? 5 : 4), 5);
    id[10] = 0;
  }
  LOG("game=%s\n", id);
}

static int flushThread(SceSize args, void *argp) { // flushes the log every second
  for (;;) {
    sceKernelDelayThread(1000 * 1000);
    logFlush();
  }
  return 0;
}

// From module_start: rotates the previous log, writes the header (version, build, game file) and, unless in sync
// mode, starts the thread that flushes every second
void logInit(int sync) {
  int k1 = pspSdkSetK1(0);
  sceIoRemove(LOG_PREV);
  sceIoRename(LOG_PATH, LOG_PREV);

  char *file = sceKernelInitFileName();
  pspSdkSetK1(k1);

  sync_mode = sync;
  LOG("NativeLink %s (build %s %s) mode=%s\n", NATIVELINK_VERSION, __DATE__, __TIME__, sync ? "sync" : "async");
  LOG("file=%s\n", file ? file : "?");
  logFlush();

  if (!sync) {
    SceUID th = sceKernelCreateThread("NativeLinkLog", flushThread, 0x70, 0x1000, 0, NULL);
    if (th >= 0)
      sceKernelStartThread(th, 0, NULL);
  }
}
