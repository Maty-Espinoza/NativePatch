// Volcado binario de un frame (ver dump.h). Se arma en un buffer en la RAM libre del plugin
// y se escribe a la tarjeta cuando se llena: durante el volcado el juego va lento (es puntual).
// Con NativeDraw v6 (dev56.5) el buffer va en dos mitades: la Vita escribe una a la tarjeta mientras el PSP llena la
// otra, y el PSP solo espera si la Vita no termino la anterior (antes el juego esperaba cada sceIoWrite).
#include <pspkernel.h>
#include <pspsdk.h>
#include <string.h>
#include <stdio.h>
#include <systemctrl.h>
#include "dump.h"
#include "log.h"

#define DUMP_BUF  0x0A980000 // 512 KB libres despues de la imagen del menu
#define DUMP_SIZE 0x40000 // la mitad: con la Vita se alternan las dos
#define DUMP_MAX  (80 << 20) // con mas que esto ya no se guardan datos de draws (solo comandos)
// Frames por volcado (dev49 grababa 3; se volvio a 1: menos memoria y tiempo con el juego detenido)
#define DUMP_FRAMES 1

int dumping;
DumpInfo dump_info;
static int requested, flips, skipped, fd = -1;
static int vio, vio_first, half; // la Vita escribe el archivo; el primer pedido abre; mitad del buffer en uso
static u32 t_start;
static u8 *buf = (u8 *)(DUMP_BUF | 0x40000000);
static u32 len, total, ncmds, cmds_at = ~0u; // cmds_at: registro CMDS abierto en buf (o ~0)
static u32 dst[32][2], ndst, tex[512][3], ntex;
#define SNAP_BASE 0x0AA00000 // 5 MB libres
#define SNAP_END  0x0AE00000 // (despues: segunda imagen nativa de la presentacion en main.c)
static struct { u32 addr, w, h, bpp, fmt, frame; char what[24]; } snap[16];
static u32 nsnap, snap_next;
static char path[48];
static u32 (*geGetCmd)(int);
static int (*geGetMtx)(int, void *), (*geDrawSync)(int);

static void closeCmds(void) {
  if (cmds_at != ~0u) {
    ((u32 *)(buf + cmds_at))[1] = len - cmds_at - 8;
    cmds_at = ~0u;
  }
}
// Pedido a NativeDraw: escribir esta mitad (banderas 1 abrir, 2 cerrar) y pasar a la otra
static void vitaChunk(u32 flags) {
  if (!vio)
    return;
  if (!vitaWait(VDUMP)) { // no respondio: el resto del volcado se pierde (el archivo queda corto)
    vio = 0;
    skipped++;
    return;
  }
  VDESC[VDUMP + 2] = DUMP_BUF + half * DUMP_SIZE;
  VDESC[VDUMP + 3] = len;
  VDESC[VDUMP + 4] = flags | (vio_first ? 1 : 0);
  vio_first = 0;
  vitaPost(VDUMP);
  half ^= 1;
  buf = (u8 *)((DUMP_BUF + half * DUMP_SIZE) | 0x40000000);
}

static void flush(void) {
  closeCmds();
  if (vio) {
    if (len)
      vitaChunk(0);
  } else if (len && fd >= 0) { // se escribio sin cache: por la direccion normal, sin lineas viejas en la cache
    sceKernelDcacheWritebackInvalidateRange((void *)((u32)buf & 0x0fffffff), len);
    int k1 = pspSdkSetK1(0);
    sceIoWrite(fd, (void *)((u32)buf & 0x0fffffff), len);
    pspSdkSetK1(k1);
  }
  total += len;
  len = 0;
}
static void put(const void *p, u32 n) {
  const u8 *s = p;
  while (n) {
    if (len == DUMP_SIZE)
      flush();
    u32 k = DUMP_SIZE - len < n ? DUMP_SIZE - len : n;
    memcpy(buf + len, s, k);
    len += k; s += k; n -= k;
  }
}

// Un solo hilo a la vez: mientras uno escribe a la tarjeta, otro hilo del juego (el flip)
// escribia en medio de su registro (volcados dev28 danados). El mismo hilo puede volver a entrar.
static int owner, depth;
static void lock(void) {
  int me = sceKernelGetThreadId();
  for (;;) {
    int intr = sceKernelCpuSuspendIntr();
    if (!owner || owner == me) {
      owner = me;
      depth++;
      sceKernelCpuResumeIntr(intr);
      return;
    }
    sceKernelCpuResumeIntr(intr);
    sceKernelDelayThread(100);
  }
}
static void unlock(void) {
  if (--depth == 0)
    owner = 0;
}

void dumpRecord(u32 type, const void *a, u32 na, const void *b, u32 nb) {
  if (!dumping)
    return;
  lock();
  if (!dumping) { // termino mientras se esperaba
    unlock();
    return;
  }
  closeCmds();
  u32 h[2] = { type, na + nb }, zero = 0;
  put(h, 8);
  put(a, na);
  put(b, nb);
  put(&zero, (4 - ((na + nb) & 3)) & 3);
  unlock();
}

// Comando recorrido: direccion, valor del juego y lo que quedo. Bit 0 de la direccion = parche
// posterior de un comando anterior (render target, copia...)
void dumpCmd(u32 *addr, u32 orig) {
  if (!dumping)
    return;
  lock();
  if (!dumping) {
    unlock();
    return;
  }
  if (cmds_at == ~0u || len + 12 > DUMP_SIZE) {
    if (len + 20 > DUMP_SIZE)
      flush();
    cmds_at = len;
    u32 h[2] = { GPD_CMDS, 0 };
    memcpy(buf + len, h, 8);
    len += 8;
  }
  u32 t[3] = { (u32)addr, orig, *(u32 *)((u32)addr & ~3) }; // con cache: ahi escribe el recorrido
  memcpy(buf + len, t, 12);
  len += 12;
  ncmds++;
  unlock();
}

// La VRAM real la escribe el GE: se lee sin cache. La RAM se lee con cache (lo que ve la CPU
// del juego, y los parches del recorrido aun no escritos a memoria)
static u32 uncached(u32 a) { return (a & 0x0f000000) == 0x04000000 ? (a & 0x0fffffff) | 0x40000000 : a; }

void dumpBlob(u32 what, u32 addr, u32 size) {
  if (!dumping || !size)
    return;
  if (total + len + size > DUMP_MAX) {
    skipped++;
    return;
  }
  u32 h[3] = { what, addr, size };
  dumpRecord(GPD_BLOB, h, 12, (void *)uncached(addr), size);
}

// Las texturas y CLUT se repiten mucho en un frame: solo se guardan si cambiaron
void dumpBlobOnce(u32 what, u32 addr, u32 size) {
  if (!dumping || !size)
    return;
  const u32 *p = (const u32 *)uncached(addr);
  u32 sum = size, i;
  for (i = 0; i < size / 4; i += 7)
    sum = (sum << 5 | sum >> 27) ^ p[i];
  for (i = 0; i < ntex; i++)
    if (tex[i][0] == addr && tex[i][1] == size && tex[i][2] == sum)
      return;
  if (ntex < 512) {
    tex[ntex][0] = addr; tex[ntex][1] = size; tex[ntex][2] = sum;
    ntex++;
  }
  dumpBlob(what, addr, size);
}

void dumpTxList(u32 addr) {
  if (!dumping)
    return;
  const u32 *p = (const u32 *)(addr | 0x40000000);
  u32 n = 0, h[2];
  while (n < 400 && (p[n] >> 24) != 0x0B) // hasta el RET
    n++;
  h[0] = addr; h[1] = n + 1;
  dumpRecord(GPD_TXLIST, h, 8, p, (n + 1) * 4);
}

void dumpCopyDst(u32 addr, u32 size) {
  u32 i;
  if (!dumping)
    return;
  for (i = 0; i < ndst; i++)
    if (dst[i][0] == addr) {
      if (size > dst[i][1])
        dst[i][1] = size;
      return;
    }
  if (ndst < 32) {
    dst[ndst][0] = addr; dst[ndst][1] = size;
    ndst++;
  }
}

u32 dumpSnap(u32 w, u32 h, u32 bpp, u32 fmt, const char *what) {
  u32 size = (w * h * bpp + 63) & ~63;
  if (!dumping || nsnap >= 16 || snap_next + size > SNAP_END)
    return 0;
  snap[nsnap].addr = snap_next; snap[nsnap].w = w; snap[nsnap].h = h; snap[nsnap].bpp = bpp;
  snap[nsnap].fmt = fmt; snap[nsnap].frame = log_frame;
  strncpy(snap[nsnap].what, what, sizeof(snap[0].what) - 1);
  snap[nsnap].what[sizeof(snap[0].what) - 1] = 0;
  nsnap++;
  snap_next += size;
  return snap_next - size;
}

static void logTap(const char *s, int n) {
  if (dumping)
    dumpRecord(GPD_EVENT, s, n, NULL, 0);
}

// Espera a que el GE termine lo encolado (como mucho 300 ms: una lista puede esperar un stall)
static int waitGe(void) {
  int i;
  for (i = 0; geDrawSync && i < 300; i++) {
    if (geDrawSync(1) == 0)
      return 1;
    sceKernelDelayThread(1000);
  }
  return 0;
}

// Registros reales del GE (y matrices), los que conoce el recorrido y el estado del plugin
static void regs(u32 when) {
  static u32 r[256 + 148 + 256 + 8 + 1];
  int i;
  r[0] = when;
  for (i = 0; i < 256; i++)
    r[1 + i] = geGetCmd ? geGetCmd(i) : 0;
  float *m = (float *)&r[257];
  memset(m, 0, 148 * 4);
  for (i = 0; geGetMtx && i < 12; i++) // 8 de huesos, mundo, vista, proyeccion, textura
    geGetMtx(i, m + (i < 8 ? i * 12 : i == 8 ? 96 : i == 9 ? 108 : i == 10 ? 120 : 136));
  if (dump_info.shadow)
    memcpy(&r[405], dump_info.shadow, 256 * 4);
  if (dump_info.state)
    dump_info.state(&r[661]);
  dumpRecord(GPD_REGS, r, sizeof(r), NULL, 0);
}

static void mem(u32 kind, u32 addr, u32 size) {
  u32 h[3] = { kind, addr, size };
  if ((addr & 0x0f000000) != 0x04000000) // RAM que pudo escribir el GE: lo de la CPU a memoria y releer
    sceKernelDcacheWritebackInvalidateRange((void *)addr, size);
  dumpRecord(GPD_MEM, h, 12, (void *)uncached(addr), size);
}

static void start(void) {
  requested = 0;
  if (!geGetCmd) {
    geGetCmd = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xDC93CFEF);
    geGetMtx = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x57C8945B);
    geDrawSync = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xB287BD61);
  }
  snprintf(path, sizeof(path), "ms0:/nativelink_%s_%06lu.gpd", game_id[0] ? game_id : "juego", log_frame);
  t_start = sceKernelGetSystemTimeLow();
  half = 0;
  buf = (u8 *)(DUMP_BUF | 0x40000000);
  vio = vitaIo() && vitaWait(VDUMP); // el volcado anterior ya termino de escribirse
  vita_wait_us = 0;
  if (vio) { // nombre sin "ms0:/" (NativeDraw lo escribe en ux0:pspemu/)
    vio_first = 1;
    u32 w[12];
    memset(w, 0, sizeof(w));
    strncpy((char *)w, path + 5, sizeof(w) - 1);
    int i;
    for (i = 0; i < 12; i++)
      VDESC[32 + i] = w[i];
  } else {
    int k1 = pspSdkSetK1(0);
    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    pspSdkSetK1(k1);
  }
  if (!vio && fd < 0) {
    LOG("volcado: no se pudo crear %s\n", path);
    return;
  }
  dumping = 1;
  flips = skipped = 0; len = total = ncmds = ndst = ntex = nsnap = 0; cmds_at = ~0u;
  snap_next = SNAP_BASE;
  log_tap = logTap;
  int idle = waitGe();
  struct { char magic[4]; u32 version; char game[12]; char plugin[16]; u32 frame, passive, idle, opt_size; } h;
  memset(&h, 0, sizeof(h));
  memcpy(h.magic, "GPD1", 4);
  h.version = 1;
  strncpy(h.game, game_id, sizeof(h.game) - 1);
  strncpy(h.plugin, NATIVELINK_VERSION, sizeof(h.plugin) - 1);
  h.frame = log_frame; h.passive = dump_info.passive && *dump_info.passive; h.idle = idle; h.opt_size = dump_info.opt_size;
  dumpRecord(GPD_HEADER, &h, sizeof(h), dump_info.opt, dump_info.opt_size);
  regs(0);
  // VRAM real: 2 MB (0x04200000+ son reflejos de los mismos 2 MB; hasta dev56.6 se guardaban 4 MB: 4 MB de mas por volcado)
  mem(0, 0x04000000, 0x200000);   // (en pasivo: la del juego; activo: buffer grande y rt)
  if (!h.passive)
    mem(1, 0x0A000000, dump_info.fake_vram_used ? dump_info.fake_vram_used : 0x200000); // VRAM falsa
  LOG("volcado: grabando %s (GE %s al empezar)\n", path, idle ? "libre" : "OCUPADO");
}

static void finish(const char *why) {
  lock();
  u32 i, idle = waitGe();
  regs(1);
  mem(2, 0x04000000, 0x200000);
  if (!*dump_info.passive)
    mem(3, 0x0A000000, dump_info.fake_vram_used ? dump_info.fake_vram_used : 0x200000);
  for (i = 0; i < ndst; i++)
    mem(4, dst[i][0], dst[i][1]); // texturas que el juego armo con copias (formulario de Silent Hill)
  for (i = 0; i < nsnap; i++) { // instantaneas a mitad del frame
    u32 sz = snap[i].w * snap[i].h * snap[i].bpp;
    sceKernelDcacheWritebackInvalidateRange((void *)snap[i].addr, sz);
    dumpRecord(GPD_SNAP, &snap[i], sizeof(snap[0]), (void *)snap[i].addr, sz);
  }
  u32 e[4] = { idle, skipped, ncmds, flips };
  dumpRecord(GPD_END, e, 16, why, strlen(why) + 1);
  int by_vita = vio;
  if (vio) {
    closeCmds();
    total += len;
    vitaChunk(2); // la ultima mitad (aunque este vacia) y cerrar
    len = 0;
  } else {
    flush();
    int k1 = pspSdkSetK1(0);
    sceIoClose(fd);
    pspSdkSetK1(k1);
  }
  fd = -1;
  dumping = 0;
  log_tap = NULL;
  unlock();
  // (dos lineas: el LOG corta a 160 caracteres y en dev56.5 se perdia el final)
  LOG("volcado listo: %s (%lu KB, %lu comandos, %d sin guardar, fin: %s)\n", path + 5, total / 1024, ncmds, skipped, why);
  LOG("volcado: %lu ms, lo escribio %s (el PSP espero a la Vita %lu ms)\n", (sceKernelGetSystemTimeLow() - t_start) / 1000,
      by_vita ? "la Vita" : "el PSP", vita_wait_us / 1000);
}

void dumpRequest(void) {
  if (!dumping)
    requested = 1;
}

int dumpOnList(u32 list, u32 stall, int kind) {
  if (dumping && kind != 2 && flips >= DUMP_FRAMES)
    finish(DUMP_FRAMES > 1 ? "frames completos" : "frame completo");
  else if (dumping && flips >= DUMP_FRAMES + 3)
    finish("flips sin lista nueva");
  if (!dumping && requested && kind != 2)
    start();
  if (dumping) {
    u32 r[4] = { list, stall, kind, log_frame };
    dumpRecord(GPD_LIST, r, 16, NULL, 0);
  }
  return dumping;
}

void dumpOnFlip(u32 top, int width, int fmt) {
  if (!dumping)
    return;
  u32 r[4] = { top, width, fmt, log_frame };
  dumpRecord(GPD_FLIP, r, 16, NULL, 0);
  flips++;
}
