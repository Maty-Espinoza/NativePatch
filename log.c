#include <pspsdk.h>
#include <pspkernel.h>
#include <pspinit.h>
#include <pspintrman.h>
#include <string.h>

#include "log.h"

#define LOG_PATH "ms0:/ge_patch.log"
#define LOG_PREV "ms0:/ge_patch_prev.log"
#define LOG_MAX  (1024 * 1024) // tope del archivo, para no llenar la tarjeta

u32 log_frame;

static char buf[2][8192]; // doble buffer: se llena uno mientras se escribe el otro
static int cur, len, dropped, written, sync_mode;
static u32 seen[256];     // keys ya vistas por logOnce

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

void logFlush(void) {
  int intr = sceKernelCpuSuspendIntr();
  char *p = buf[cur];
  int n = len, d = dropped;
  cur ^= 1;
  len = dropped = 0;
  sceKernelCpuResumeIntr(intr);

  if ((!n && !d) || written >= LOG_MAX)
    return;

  int k1 = pspSdkSetK1(0);
  SceUID fd = sceIoOpen(LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT, 0777);
  if (fd >= 0) {
    sceIoLseek(fd, 0, PSP_SEEK_END);
    char t[64];
    if (d)
      sceIoWrite(fd, t, sprintf(t, "-- %d lineas perdidas (buffer lleno)\n", d));
    sceIoWrite(fd, p, n);
    written += n;
    if (written >= LOG_MAX)
      sceIoWrite(fd, t, sprintf(t, "-- log detenido: llego a %d KB\n", LOG_MAX / 1024));
    sceIoClose(fd);
  }
  pspSdkSetK1(k1);
}

int logOnce(u32 key) {
  if (!key)
    key = 1; // 0 marca casilla vacia
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
  return res; // si la tabla se llena, no se loguean eventos nuevos
}

static int flushThread(SceSize args, void *argp) {
  for (;;) {
    sceKernelDelayThread(1000 * 1000);
    logFlush();
  }
  return 0;
}

void logInit(int sync) {
  int k1 = pspSdkSetK1(0);
  sceIoRemove(LOG_PREV);
  sceIoRename(LOG_PATH, LOG_PREV);

  // ID del juego (solo existe en ISOs/UMD; en juegos PSN queda '?', pero el archivo lo dice)
  char id[11] = "?";
  SceUID fd = sceIoOpen("disc0:/UMD_DATA.BIN", PSP_O_RDONLY, 0);
  if (fd >= 0) {
    if (sceIoRead(fd, id, 10) == 10)
      id[10] = 0;
    sceIoClose(fd);
  }
  char *file = sceKernelInitFileName();
  pspSdkSetK1(k1);

  sync_mode = sync;
  LOG("GePatch %s (build %s %s) modo=%s\n", GEPATCH_VERSION, __DATE__, __TIME__, sync ? "sync" : "async");
  LOG("juego=%s archivo=%s\n", id, file ? file : "?");
  logFlush();

  if (!sync) {
    SceUID th = sceKernelCreateThread("GePatchLog", flushThread, 0x70, 0x1000, 0, NULL);
    if (th >= 0)
      sceKernelStartThread(th, 0, NULL);
  }
}
