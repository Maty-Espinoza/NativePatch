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
u32 log_us; // tiempo escribiendo el log (perfil de main.c)

// --- Canal con gepresent (ver log.h) ---
u32 log_io_psp; // escrituras del log hechas por el PSP (sceIo); las de la Vita no cuentan
static int vita_dead; // no respondio: hasta reiniciar el juego se escribe con sceIo
int vitaIo(void) {
  return !vita_dead && VDESC[0] == 0x31565047 && VDESC[8] == 0x56474F4C; // "GPV1" y "LOGV"
}
u32 vita_wait_us; // tiempo esperando a gepresent (el volcado lo pone en 0 al empezar)
int vitaWait(int box) {
  u32 t0 = sceKernelGetSystemTimeLow();
  while (VDESC[box] != VDESC[box + 1]) {
    if (sceKernelGetSystemTimeLow() - t0 > 2000000) {
      vita_dead = 1;
      return 0;
    }
    sceKernelDelayThread(1000);
  }
  vita_wait_us += sceKernelGetSystemTimeLow() - t0;
  return 1;
}
void vitaPost(int box) {
#ifdef __mips__
  asm volatile("sync"); // los datos antes que el numero de pedido
#endif
  VDESC[box]++;
}

static char buf[2][8192]; // doble buffer: se llena uno mientras se escribe el otro
static int cur, len, dropped, written, sync_mode, eager;
static int flushing; // un solo volcado a la vez (el hilo del log y el modo captura podian chocar)
static u32 seen[256];     // keys ya vistas por logOnce

void (*log_tap)(const char *s, int n);
void logWrite(const char *s, int n) {
  if (log_tap)
    log_tap(s, n);
  int intr = sceKernelCpuSuspendIntr();
  if (len + n <= (int)sizeof(buf[0])) {
    memcpy(buf[cur] + len, s, n);
    len += n;
  } else {
    dropped++;
  }
  sceKernelCpuResumeIntr(intr);

  if (sync_mode || (eager && len > (int)sizeof(buf[0]) / 2))
    logFlush();
}

// Durante una captura se escriben muchas lineas: volcar apenas el buffer va por la mitad
void logEager(int on) {
  eager = on;
  if (!on)
    logFlush();
}

void logFlush(void) {
  int intr = sceKernelCpuSuspendIntr();
  if (flushing) { // otro hilo esta escribiendo: estas lineas salen en el proximo volcado
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

  u32 t0 = sceKernelGetSystemTimeLow();
  if (vitaIo() && vitaWait(VLOG)) { // lo escribe la Vita: aqui solo se copia el texto
    char *q = (char *)(VLOG_BUF | 0x40000000);
    int k = d ? sprintf(q, "-- %d lineas perdidas (buffer lleno)\n", d) : 0;
    memcpy(q + k, p, n);
    k += n;
    written += n;
    if (written >= LOG_MAX)
      k += sprintf(q + k, "-- log detenido: llego a %d KB\n", LOG_MAX / 1024);
    VDESC[VLOG + 2] = k;
    vitaPost(VLOG);
    log_us += sceKernelGetSystemTimeLow() - t0;
    flushing = 0;
    return;
  }
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
  log_us += sceKernelGetSystemTimeLow() - t0;
  log_io_psp++;
  pspSdkSetK1(k1);
  flushing = 0;
}

// Olvida los eventos ya registrados: vuelven a aparecer en el log (tests, o tras cambiar opciones)
void logResetOnce(void) {
  memset(seen, 0, sizeof(seen));
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

// ID del juego desde el disco (ISO/UMD). Se llama unos frames despues de iniciar,
// porque en module_start el disco aun no esta montado. En juegos PSN no existe.
char game_id[11] = "?";
void logGameId(void) {
  char *id = game_id;
  int k1 = pspSdkSetK1(0);
  SceUID fd = sceIoOpen("disc0:/UMD_DATA.BIN", PSP_O_RDONLY, 0);
  if (fd >= 0) {
    if (sceIoRead(fd, id, 10) == 10)
      id[10] = 0;
    sceIoClose(fd);
  }
  pspSdkSetK1(k1);
  LOG("juego=%s\n", id);
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

  char *file = sceKernelInitFileName();
  pspSdkSetK1(k1);

  sync_mode = sync;
  LOG("GePatch %s (build %s %s) modo=%s\n", GEPATCH_VERSION, __DATE__, __TIME__, sync ? "sync" : "async");
  LOG("archivo=%s\n", file ? file : "?");
  logFlush();

  if (!sync) {
    SceUID th = sceKernelCreateThread("GePatchLog", flushThread, 0x70, 0x1000, 0, NULL);
    if (th >= 0)
      sceKernelStartThread(th, 0, NULL);
  }
}
