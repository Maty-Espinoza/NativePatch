// nativedraw.suprx: NativeDraw, el plugin del lado Vita.
//
// Plugin de usuario en el proceso de Adrenaline (taiHEN, seccion *PSPEMUCFW de ur0:tai/config.txt). Trabaja junto a
// NativeLink (el plugin del lado PSP): NativeLink anota en un buzon de la RAM del PSP todo lo que el juego le pide al GE, y
// el capturador de NativeDraw (captura.c) recorre las mismas listas desde la Vita. Por ahora (etapa 2 de la arquitectura B)
// solo las captura y mide; en la etapa 3 las va a dibujar con la GPU de la Vita (presentador.c: desde 0.0.3 toma la
// pantalla de Adrenaline, apagado por defecto).
// Este archivo: arranque, log propio, el log de NativeLink (lo escribe la Vita, asi el PSP no espera a la tarjeta) y el
// gancho que anota los hilos del emulador (captura.c mide cuanto procesador usa cada uno).
// Si existe ux0:data/nativedraw/off no se engancha nada.
// Logs: ux0:pspemu/nativedraw.log (este), nativedraw_captura.log (captura.c) y nativelink.log (el de NativeLink).
// Hasta v13.13 se llamaba gepresent y ademas presentaba la imagen escalada de GePatch (arquitectura A), tenia el banco de
// pruebas de GXM (etapa 1) y escribia los volcados .gpd. Eso se quito en 0.0.2: esta en la etiqueta gepatch-dev66 del repo
// (docs/dev/versiones.md). Los numeros "vNN" de los comentarios son de esa numeracion vieja.
//
// Canal del log de NativeLink (pagina 0x0A7FF000 de la RAM del PSP, ver nativelink/log.h): +0 "GPV1" (NativeLink),
// +0x20 "LOGV" (este plugin: atiende el buzon), +0x40 seq, done, bytes (texto en 0x0A7FA000).
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <taihen.h>
#include <stdarg.h>
#include "version.h"

#define LOGF "ux0:pspemu/nativedraw.log"
#define PSP(a) (0x68000000u + ((a) & 0x0FFFFFFFu))  // vista sin cache de la RAM del PSP (CONVERT_ADDRESS)
#define DESC ((volatile uint32_t *)PSP(0x0A7FF000u))

static int nlog;
void alog(const char *fmt, ...) {
  if (nlog++ > 1000) return;
  char b[256];
  va_list ap; va_start(ap, fmt);
  int n = sceClibSnprintf(b, 16, "%7u ", sceKernelGetProcessTimeLow() / 1000);
  n += sceClibVsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap);
  if (n > (int)sizeof b - 1) n = sizeof b - 1;
  SceUID fd = sceIoOpen(LOGF, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) { sceIoWrite(fd, b, n); sceIoClose(fd); }
}
static volatile int dato __attribute__((aligned(64))) = 1;  // .data no vacia (segmento RW valido)
// Sin libc: el compilador puede generar llamadas a estas dos (copias de estructuras en captura.c)
void *memset(void *d, int c, unsigned n) { char *p = d; while (n--) *p++ = c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { char *p = d; const char *q = s; while (n--) *p++ = *q++; return d; }

// --- Log de NativeLink: seq lo sube el PSP, done lo iguala este plugin al copiarlo ---
#define LOGV 0x56474F4Cu
#define VLOG 16
void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
// (v9) El texto se copia por DMA a un anillo y un hilo lo escribe a la tarjeta despues: el PSP no espera la escritura
// (abrir, escribir y cerrar toma decenas de ms).
static char lring[0x40000];
static volatile uint32_t lw, lr;
static int writer;  // 1 si el hilo escritor corre (si no, se escribe directo)
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
static void logFlushOut(void) {  // en el hilo escritor
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
static int writerThread(SceSize args, void *argp) {
  for (;;) { logFlushOut(); sceKernelDelayThread(2000); }
  return 0;
}
static uint32_t nlogs, ram_ok;
// Version de NativeLink para el menu: la primera linea de nativelink.log ("000000 NativeLink 0.0.2 (build ..."). Esa linea
// la escribe el PSP directo a la tarjeta (antes de que la Vita atienda el log), asi que se lee del archivo. Solo si
// NativeLink esta activo ("GPV1" en el buzon): si no, el archivo es del juego anterior.
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
  if (!nlogs++) alog("log de NativeLink: lo escribe la Vita (primer pedazo %u bytes)\n", n);
  DESC[VLOG + 1] = seq;
}

void capturaStart(void);  // captura.c
void presStart(void), presStop(void);  // presentador.c (0.0.3)

static int mainThread(SceSize args, void *argp) {
  SceIoStat st;
  if (sceIoGetstat("ux0:data/nativedraw/off", &st) >= 0) {
    alog("ux0:data/nativedraw/off existe: no se engancha nada\n");
    return sceKernelExitDeleteThread(0);
  }
  presStart();  // (0.0.3) presentador y menu L+R+START (0.0.4)
  // La RAM del PSP se mapea en el proceso despues de cargar el plugin: leerla antes cierra Adrenaline
  // (C2-12828-1 en v4). Se espera a que el bloque exista, como vpcdram.
  for (int mapped = 0;;) {  // solo escribe si NativeLink dejo su marca (la zona es del XMB fuera de los juegos)
    SceKernelMemBlockInfo in; in.size = sizeof in;
    uint32_t a = (uint32_t)DESC;
    if (!mapped && (sceKernelGetMemBlockInfoByAddr((void *)a, &in) < 0 || a < (uint32_t)in.mappedBase ||
                    a + 16 > (uint32_t)in.mappedBase + in.mappedSize)) {
      sceKernelDelayThread(100000);
      continue;
    }
    if (!mapped) capturaStart();  // (v12) el hilo de captura espera el buzon de NativeLink
    mapped = ram_ok = 1;  // (v9: se comprueba una vez; la RAM del PSP queda mapeada mientras viva el proceso)
    int busy = 0;
    if (DESC[0] == 0x31565047) {  // "GPV1"
      if (DESC[8] != LOGV) DESC[8] = LOGV;
      busy = DESC[VLOG] != DESC[VLOG + 1];
      serveLog();
    }
    sceKernelDelayThread(busy ? 200 : 2000);
  }
}

// v12.6 (diagnostico): cada hilo que crea el emulador (ScePspemu) con su prioridad y nucleos, para elegir donde corre
// el capturador sin quitarle tiempo al audio (v12.4 con prioridad 0x40 en el nucleo 2 lo cortaba)
static tai_hook_ref_t ref_ct;
static SceUID hk_ct = -1;
SceUID emu_th[256]; int emu_aff[256], emu_n;  // v12.10: captura.c mide cuanto procesador usa cada uno
static SceUID hookCT(const char *name, SceKernelThreadEntry entry, int prio, int stack, SceUInt attr, int aff,
                     const SceKernelThreadOptParam *opt) {
  SceUID r = TAI_CONTINUE(SceUID, ref_ct, name, entry, prio, stack, attr, aff, opt);
  alog("hilo de ScePspemu: %s prioridad %x nucleos %x pila %x atributos %x -> %08x\n", name ? name : "?", prio, aff, stack, attr, r);
  if (r >= 0 && emu_n < 256) { emu_th[emu_n] = r; emu_aff[emu_n] = aff; emu_n++; }
  return r;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
  alog("NativeDraw " NATIVEDRAW_VERSION " (antes gepresent v13.13) compilado %s %s (dato %d)\n", __DATE__, __TIME__, dato);
  SceUID wt = sceKernelCreateThread("nativedraw_escritor", writerThread, 0xA0, 0x2000, 0, 0, NULL);
  if (wt >= 0 && sceKernelStartThread(wt, 0, NULL) >= 0) writer = 1;
  alog("log de NativeLink: %s\n", writer ? "anillo + hilo escritor" : "escritura directa");
  hk_ct = taiHookFunctionImport(&ref_ct, "ScePspemu", 0xCAE9ACE6, 0xC5C11EE7, hookCT);
  alog("gancho de sceKernelCreateThread de ScePspemu: %08x\n", hk_ct);
  SceUID th = sceKernelCreateThread("nativedraw", mainThread, 0x60, 0x2000, 0, 0, NULL);
  if (th >= 0) sceKernelStartThread(th, 0, NULL);
  return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize args, void *argp) {
  if (hk_ct >= 0) taiHookRelease(hk_ct, ref_ct);
  presStop();
  return SCE_KERNEL_STOP_SUCCESS;
}
