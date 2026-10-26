// kbench.suprx: lado ARM de KBENCH. Se carga en el proceso de Adrenaline (taiHEN, *PSPEMUCFW).
// No usa la biblioteca de taiHEN: busca ScePspemu con sceKernelGetModuleList.
//
// 1. Espera a que ScePspemu esté cargado y a que la RAM del PSP esté mapeada.
// 2. Verifica que los offsets conocidos de ScePspemu parezcan inicios de función o ganchos de taiHEN
//    (si no, no hace nada salvo que exista ux0:data/kbench/forzar.txt).
// 3. Mientras kbench.prx esté presente (KB_PSP_MAGIC), publica un buzón en la RAM del PSP (KB_MBOX)
//    cada 50 ms: el PSP solo envía si lo ve vivo. Sin la marca, solo lee: no toca la memoria del XMB.
// 4. Atiende el canal por memoria compartida (siempre) y, si el PSP lo pide y Adrenaline no está en
//    modo PS1, también KERMIT_MODE_EXTRA_1 (que Adrenaline usa para PS1: ver kbench.h).
// Cada petición deja sus tiempos en un registro de 4096 entradas que el PSP lee con KB_GETLOG.
// Nada de lo que llega del PSP se usa sin validar: direcciones dentro de la RAM del PSP, tamaños
// acotados, recorrido con límite de comandos.
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/power.h>
#include <psp2/compat.h>
#include <psp2/kernel/dmac.h>
#include <stdarg.h>
#include <stddef.h>
#include "kbench.h"
#include "walk.h"

static const uint32_t OFF[4] = {0x6364, 0x6490, 0x64D0, 0x6560};  // Convert, Writeback, Wait, Send (Thumb)
static void *(*pConvert)(uint32_t addr, int mode, uint32_t size);
static int (*pWriteback)(void *addr, int size);
static int (*pWait)(int mode, KbKermitReq **req);
static int (*pSend)(int mode, KbKermitReq *req, uint64_t res);

static uint32_t textAddr, textSize, status = 1;
static volatile uint32_t seen, answered, lastCmd, errors, hbCount, flags, pollUs = 1000, active;
static SceUID kermitTh = -1;
static KbEnt ring[4096];
static volatile uint32_t rcount;
#define NOW() sceKernelGetProcessTimeLow()

static void alog(const char *fmt, ...) {
  char b[256];
  va_list ap; va_start(ap, fmt);
  int n = sceClibSnprintf(b, 16, "%8u ", sceKernelGetProcessTimeLow() / 1000);  // ms del proceso
  n += sceClibVsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof b - 1) n = sizeof b - 1;
  SceUID fd = sceIoOpen("ux0:data/kbench/arm.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) { sceIoWrite(fd, b, n); sceIoClose(fd); }
}

static int existe(const char *p) { SceIoStat st; return sceIoGetstat(p, &st) >= 0; }

static int popsMode(void) {  // 1 si Adrenaline está corriendo un juego de PS1
#ifdef KB_USE_ADRENALINE_H
  SceAdrenaline *a = pConvert(ADRENALINE_ADDRESS, KERMIT_INPUT_MODE, sizeof(SceAdrenaline));
  return a && a->pops_mode;
#else
  return 0;
#endif
}

// Dirección del PSP → puntero del ARM. Valida el rango; con KB_F_DIRECT usa la vista lineal
// CONVERT_ADDRESS de Adrenaline (0x68000000 + dir), sin mantener la caché.
static uint8_t *local;  // 1 MB propio del ARM (con caché) para KB_F_COPY / KB_F_DMA

static uint8_t *map(uint32_t addr, uint32_t size, uint32_t fl, int mode) {
  addr &= 0x0FFFFFFF;
  if (addr < 0x08000000 || size > 0x00400000 || addr + size > 0x0C000000) return NULL;
  if (fl & KB_F_CACHED) {  // vista con caché: hay que invalidar antes de leer y hacer writeback después de escribir
    uint8_t *p = (uint8_t *)(0x59000000 + addr);
    if (mode == KERMIT_INPUT_MODE && (fl & KB_F_INVAL)) sceCompatCache(SCE_COMPAT_CACHE_INVALIDATE, p, size);
    return p;
  }
  if (fl & KB_F_DIRECT) {
    uint8_t *p = (uint8_t *)(0x68000000 + addr);
    return p;  // misma vista que CONVERT_ADDRESS de Adrenaline
  }
  return pConvert(addr, mode, (fl & KB_F_INVAL) ? size : 0);
}

static int kermitThread(SceSize args, void *argp);

static uint64_t handle(uint32_t cmd, const uint32_t *a, KbEnt *e) {
  uint32_t res = 0;
  e->seq = a[0]; e->cmd = cmd;
  switch (cmd) {
  case KB_HELLO: res = KB_VERSION; break;
  case KB_PING: break;
  case KB_READ: {
    uint8_t *p = map(a[1], a[2], a[3], KERMIT_INPUT_MODE);
    if (!p) { res = KB_ERR_MAP; break; }
    if (a[3] & (KB_F_COPY | KB_F_DMA)) {  // t1 incluye la copia a memoria propia
      if (!local || a[2] > 0x100000) { res = KB_ERR_ARGS; break; }
      if (a[3] & KB_F_DMA) sceDmacMemcpy(local, p, a[2]); else sceClibMemcpy(local, p, a[2]);
      p = local;
    }
    e->t1 = NOW();
    res = kbSum((const uint32_t *)p, a[2] / 4);
    e->t2 = NOW();
    break;
  }
  case KB_WRITE: {
    uint8_t *p = map(a[1], a[2], 0, KERMIT_OUTPUT_MODE);
    if (!p) { res = KB_ERR_MAP; break; }
    e->t1 = NOW();
    uint32_t *w = (uint32_t *)p;
    for (uint32_t i = 0; i < a[2] / 4; i++) w[i] = kbPat(a[3], i);
    e->t2 = NOW();
    pWriteback(p, a[2]);
    break;
  }
  case KB_WALK: {  // a1 base, a2 bytes, a3 lista, a4 salida, a5 bytes de salida, a6 flags
    uint32_t base = a[1] & 0x0FFFFFFF, size = a[2], out = a[4] & 0x0FFFFFFF, osz = a[5];
    if (out < base || osz < sizeof(KbWalkStats) || out - base > size || osz > size - (out - base)) { res = KB_ERR_ARGS; break; }
    uint8_t *mem = map(base, size, a[6], KERMIT_INPUT_MODE);
    if (!mem) { res = KB_ERR_MAP; break; }
    uint32_t *o = (uint32_t *)(mem + (out - base));  // la salida va directo a la RAM del PSP
    if (a[6] & (KB_F_COPY | KB_F_DMA)) {             // la lista se recorre en una copia propia
      if (!local || size > 0x100000) { res = KB_ERR_ARGS; break; }
      if (a[6] & KB_F_DMA) sceDmacMemcpy(local, mem, out - base); else sceClibMemcpy(local, mem, out - base);
      o = pConvert(out, KERMIT_OUTPUT_MODE, 0);
      mem = local;
    }
    e->t1 = NOW();
    KbWalkStats st;
    kbWalk(mem, base, out - base, a[3] & 0x0FFFFFFF, o + sizeof st / 4, (osz - sizeof st) / 4, 1000000, &st);
    sceClibMemcpy(o, &st, sizeof st);
    e->t2 = NOW();
    if (a[6] & KB_F_CACHED) sceCompatCache(SCE_COMPAT_CACHE_WRITEBACK, o, osz);
    else if ((a[6] & KB_F_WBACK) && !(a[6] & KB_F_DIRECT)) pWriteback(o, osz);
    res = st.sum;
    break;
  }
  case KB_AFFINITY: res = sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), a[1]); break;
  case KB_PRIORITY: res = sceKernelChangeThreadPriority(sceKernelGetThreadId(), a[1]); break;
  case KB_GETLOG: {
    uint32_t c = rcount, n = c < 4096 ? c : 4096, max = a[2];
    if (n > max) n = max;
    uint8_t *p = n ? map(a[1], n * sizeof(KbEnt), 0, KERMIT_OUTPUT_MODE) : NULL;
    if (n && !p) { res = KB_ERR_MAP; break; }
    for (uint32_t i = 0; i < n; i++) ((KbEnt *)p)[i] = ring[(c - n + i) % 4096];
    if (n) pWriteback(p, n * sizeof(KbEnt));
    if (a[3]) rcount = 0;
    res = n;
    break;
  }
  case KB_SPIN: { uint32_t t = NOW(), us = a[1] > 100000 ? 100000 : a[1]; while (NOW() - t < us); break; }
  case KB_INFO: {
    KbInfo *p = (KbInfo *)map(a[1], sizeof(KbInfo), 0, KERMIT_OUTPUT_MODE);
    if (!p) { res = KB_ERR_MAP; break; }
    p->version = KB_VERSION; p->text_addr = textAddr; p->text_size = textSize; p->mode = KB_MODE;
    for (int i = 0; i < 4; i++) { p->off[i] = OFF[i]; sceClibMemcpy(p->head[i], (void *)(textAddr + OFF[i]), 8); }
    pWriteback(p, sizeof(KbInfo));
    break;
  }
  case KB_SHMPOLL: pollUs = a[1] > 100000 ? 100000 : a[1]; break;
  case KB_KERMIT_ON:
    if (kermitTh >= 0) { res = 1; break; }
    if (popsMode()) { alog("Kermit rechazado: Adrenaline esta en modo PS1\n"); break; }
    kermitTh = sceKernelCreateThread("kb_kermit", kermitThread, 0x10000100, 0x4000, 0, 0, NULL);
    if (kermitTh < 0) { alog("no se pudo crear el hilo de Kermit: %08x\n", kermitTh); break; }
    sceKernelStartThread(kermitTh, 0, NULL);
    res = 1;
    break;
  default: res = KB_ERR_CMD;
  }
  return ((uint64_t)(a[0] ^ KB_ECHO) << 32) | res;
}

static uint64_t atender(uint32_t cmd, const uint32_t *a, uint32_t t0) {
  seen++; lastCmd = cmd;
  KbEnt e = {0};
  e.t0 = e.t1 = e.t2 = t0;
  uint64_t res = handle(cmd, a, &e);
  if (e.t2 == t0) e.t1 = e.t2 = NOW();
  e.res = (uint32_t)res;
  if ((uint32_t)res >= 0xE0000000) errors++;
  e.t3 = NOW();
  if (cmd != KB_GETLOG) ring[rcount++ % 4096] = e;
  return res;
}

static int kermitThread(SceSize args, void *argp) {
  alog("escuchando el modo Kermit %d\n", KB_MODE);
  for (;;) {
    KbKermitReq *r = NULL;
    flags |= KB_MB_KERMIT;
    int ret = pWait(KB_MODE, &r);
    uint32_t t0 = NOW();
    if (ret < 0 || !r) { errors++; alog("KermitWaitAndGetRequest: %08x\n", ret); sceKernelDelayThread(10000); continue; }
    uint32_t a[7];
    for (int i = 0; i < 7; i++) a[i] = (uint32_t)r->args[i];
    if (r->cmd == KB_KERMIT_ON || r->cmd < KB_HELLO || r->cmd > KB_SHMPOLL)
      alog("Kermit: cmd %u desconocido (seq %08x): otro servicio usa el modo %d?\n", r->cmd, a[0], KB_MODE);
    uint64_t res = atender(r->cmd, a, t0);
    pSend(KB_MODE, r, res);
    answered++;
  }
  return 0;
}

// Canal por memoria compartida: sondea el timbre de la petición en la RAM del PSP
static int shmThread(SceSize args, void *argp) {
  uint32_t last = 0, tUlt = NOW();
  alog("canal compartido en %08x\n", KB_SHM_REQ);
  flags |= KB_MB_SHM;
  for (;;) {
    if (!active) { last = 0; sceKernelDelayThread(20000); continue; }
    volatile uint32_t *q = pConvert(KB_SHM_REQ, KERMIT_INPUT_MODE, 48);
    uint32_t d = q ? q[0] : 0;
    if (d && d != last) {
      uint32_t t0 = NOW(), a[8], cmd = q[1];
      for (int i = 0; i < 8; i++) a[i] = q[4 + i];
      last = d;
      uint64_t res = a[0] == d ? atender(cmd, a, t0) : ((uint64_t)(d ^ KB_ECHO) << 32 | KB_ERR_ARGS);
      volatile uint32_t *p = pConvert(KB_SHM_RES, KERMIT_OUTPUT_MODE, 16);
      if (p) {  // datos primero y timbre después, cada uno con su writeback: si no, el PSP puede ver el timbre
                // antes que los datos (pasó en ~0,1% de los pedidos: "respuestas malas")
        p[1] = (uint32_t)res; p[2] = (uint32_t)(res >> 32); pWriteback((void *)p, 16);
        p[0] = d; pWriteback((void *)p, 16);
      }
      answered++;
      tUlt = NOW();
      continue;
    }
    uint32_t us = pollUs;
    if (!us) us = NOW() - tUlt < 5000 ? 0 : 100;  // sondeo continuo solo durante una ráfaga
    if (us) sceKernelDelayThread(us);
  }
  return 0;
}

// Despierta cuando kbench.prx escribe KB_PSP_MAGIC; se duerme (y deja de escribir) si desaparece
static int hbThread(SceSize args, void *argp) {
  uint32_t nonce = 0;
  for (;;) {
    volatile uint32_t *w = pConvert(KB_PSP, KERMIT_INPUT_MODE, 8);
    int vivo = w && w[0] == KB_PSP_MAGIC;
    if (vivo != (int)active || (vivo && w[1] != nonce)) {
      if (vivo) {  // sesión nueva: limpiar el canal antes de atender
        volatile uint32_t *q = pConvert(KB_SHM_REQ, KERMIT_OUTPUT_MODE, 0x80);
        if (q) { for (int i = 0; i < 0x20; i++) q[i] = 0; pWriteback((void *)q, 0x80); }
        nonce = w[1];
        // Autoprueba de escritura ARM -> PSP: una palabra por variante en 0x0A000200 + 4*i.
        // El PSP la vuelca en resumen.txt si no ve el buzón (así se sabe qué vista llega).
        static const uint32_t seg[3] = {0x00000000, 0x80000000, 0xA0000000};
        for (int i = 0; i < 3; i++) {
          volatile uint32_t *t = pConvert((KB_MBOX + 0x200 + 4 * i) | seg[i], KERMIT_OUTPUT_MODE, 4);
          if (t) { *t = 0xC0DE0000 | i; pWriteback((void *)t, 4); }
          alog("  prueba %d (%08x): puntero %08x\n", i, (KB_MBOX + 0x200 + 4 * i) | seg[i], (uint32_t)t);
        }
        volatile uint32_t *d = (volatile uint32_t *)(0x68000000 + KB_MBOX + 0x20C);  // vista directa, sin caché
        *d = 0xC0DE0003;
        alog("  prueba 3 (directa): puntero %08x\n", (uint32_t)d);
      }
      alog("PSP %s (nonce %08x)\n", vivo ? "conectado" : "desconectado", vivo ? nonce : 0);
      active = vivo;
    }
    if (!active) { sceKernelDelayThread(100000); continue; }
    uint32_t f = (flags & ~KB_MB_POPS) | (popsMode() ? KB_MB_POPS : 0);
    flags = f;
    KbMbox m = {KB_MAGIC, KB_VERSION, KB_MODE, status, ++hbCount, NOW(), seen, lastCmd, answered, errors,
                scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(),
                scePowerGetGpuXbarClockFrequency(), textAddr, f};
    void *p = pConvert(KB_MBOX | 0xA0000000, KERMIT_OUTPUT_MODE, sizeof m);  // con segmento, como Adrenaline
    if (p) { sceClibMemcpy(p, &m, sizeof m); pWriteback(p, sizeof m); }
    if (hbCount <= 3) {  // las primeras: dónde escribió y qué se lee de vuelta por las dos vistas
      volatile uint32_t *r = pConvert(KB_MBOX, KERMIT_INPUT_MODE, 16), *dd = (volatile uint32_t *)(0x68000000 + KB_MBOX);
      alog("buzon %u: escrito en %08x; releido %08x %08x (vista directa %08x %08x)\n", hbCount, (uint32_t)p,
           r ? r[0] : 0, r ? r[4] : 0, dd[0], dd[4]);
    }
    sceKernelDelayThread(50000);
  }
  return 0;
}

static SceUID findPspemu(SceKernelModuleInfo *mi) {
  SceUID ids[128]; SceSize n = 128;
  if (sceKernelGetModuleList(0xFF, ids, &n) < 0) return -1;
  for (SceSize i = 0; i < n; i++) {
    mi->size = sizeof *mi;
    if (sceKernelGetModuleInfo(ids[i], mi) >= 0 && !sceClibStrcmp(mi->module_name, "ScePspemu")) return ids[i];
  }
  return -1;
}

// ¿Parece inicio de función? Un gancho de taiHEN (ldr.w pc / b.w) en la primera instrucción, o un
// push (push {..lr}, push.w, stmdb) en alguna de las 3 primeras: en ScePspemu varias funciones
// empiezan con una comparación antes del push (0x64D0: cmp r0, #5; push {r4, lr}).
static int pareceFuncion(uint32_t a) {
  const uint16_t *h = (const uint16_t *)a;
  if (h[0] == 0xF8DF || (h[0] & 0xF800) == 0xF000) return 1;
  for (int i = 0; i < 3; i++)
    if ((h[i] & 0xFF00) == 0xB500 || (h[i] & 0xFF00) == 0xB400 || h[i] == 0xE92D) return 1;
  return 0;
}

static int mainThread(SceSize args, void *argp) {
  sceIoMkdir("ux0:data/kbench", 0777);
  alog("== kbench.suprx %d, modo Kermit %d ==\n", KB_VERSION, KB_MODE);
  SceKernelModuleInfo mi;
  int i;
  for (i = 0; i < 600 && findPspemu(&mi) < 0; i++) sceKernelDelayThread(200000);
  if (i == 600) { alog("ScePspemu no aparecio en 120 s: termino\n"); return sceKernelExitDeleteThread(0); }
  textAddr = (uint32_t)mi.segments[0].vaddr; textSize = mi.segments[0].memsz;
  alog("ScePspemu: .text=%08x tam=%x\n", textAddr, textSize);
  sceKernelDelayThread(2000000);  // que Adrenaline termine de poner sus ganchos
  int bien = textSize > OFF[3] + 8;
  for (i = 0; i < 4 && textSize > OFF[3] + 8; i++) {  // se anotan los 4 aunque uno falle
    uint8_t *h = (uint8_t *)(textAddr + OFF[i]);
    int ok = pareceFuncion(textAddr + OFF[i]);
    alog("  %04x: %02x %02x %02x %02x %02x %02x %02x %02x %s\n", OFF[i], h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7],
         ok ? "" : "<- no parece inicio de funcion");
    bien &= ok;
  }
  if (!bien && !existe("ux0:data/kbench/forzar.txt")) {
    status = 2;
    alog("offsets no reconocidos: no se hace nada (crear ux0:data/kbench/forzar.txt para forzar)\n");
    return sceKernelExitDeleteThread(0);
  }
  pConvert = (void *)(textAddr + OFF[0] + 1);
  pWriteback = (void *)(textAddr + OFF[1] + 1);
  pWait = (void *)(textAddr + OFF[2] + 1);
  pSend = (void *)(textAddr + OFF[3] + 1);
  // Sin más esperas: el ARM solo lee la página hasta que kbench.prx pone su marca, y para entonces
  // la RAM del PSP ya está mapeada (sceKernelFindMemBlockByAddr no la ve desde usuario: 80024501).
  alog("RAM del PSP lista; ARM=%d MHz bus=%d gpu=%d; PS1=%d\n", scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
       scePowerGetGpuClockFrequency(), popsMode());
  SceUID lb = sceKernelAllocMemBlock("kb_local", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x100000, NULL);
  if (lb >= 0) sceKernelGetMemBlockBase(lb, (void **)&local);
  alog("memoria propia de 1 MB: %08x en %08x\n", lb, (uint32_t)local);
  SceUID s = sceKernelCreateThread("kb_shm", shmThread, 0x10000100, 0x4000, 0, 0, NULL);
  SceUID h = sceKernelCreateThread("kb_buzon", hbThread, 0x10000100 + 20, 0x2000, 0, 0, NULL);
  if (s < 0 || h < 0) { alog("no se pudieron crear los hilos: %08x %08x\n", s, h); return sceKernelExitDeleteThread(0); }
  sceKernelStartThread(s, 0, NULL);
  status = 0;
  sceKernelStartThread(h, 0, NULL);
  return sceKernelExitDeleteThread(0);
}

int module_start(SceSize args, void *argp) {
  SceUID th = sceKernelCreateThread("kb_main", mainThread, 0x10000100, 0x4000, 0, 0, NULL);
  if (th >= 0) sceKernelStartThread(th, 0, NULL);
  return SCE_KERNEL_START_SUCCESS;
}
int _start(SceSize args, void *argp) __attribute__((weak, alias("module_start")));
int module_stop(SceSize args, void *argp) { return SCE_KERNEL_STOP_SUCCESS; }
