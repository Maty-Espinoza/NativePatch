// kbench.prx: lado PSP de KBENCH (plugin de kernel para Adrenaline 8).
//
// Dos modos, según el programa que se arranca:
//  - Con el EBOOT KBENCH: espera kbStart() y corre la batería completa (resumen.txt y un .csv por prueba).
//  - Con un juego: a los 45 s hace pings espaciados como en un frame y lecturas chicas, para medir
//    la latencia con ScePspemu cargado (resumen_juego.txt, juego.csv).
//
// Dos transportes hacia el ARM:
//  - Memoria compartida: el PSP escribe la petición en KB_SHM_REQ y espera la respuesta en KB_SHM_RES.
//    Siempre disponible (no depende de ningún modo Kermit) y nunca bloquea: tiene tiempo límite.
//  - Kermit (KERMIT_MODE_EXTRA_1): se activa a pedido por memoria compartida. Adrenaline usa ese modo
//    para PS1, así que el ARM lo rechaza si está en modo PS1. Si nadie escucha, sceKermitSendRequest
//    bloquea para siempre: por eso cada lote corre en un hilo aparte con vigilante.
//
// Seguridad: nunca se manda una petición si el buzón del ARM no está vivo (si nadie escucha el modo,
// el PSP queda bloqueado para siempre). Cada lote de peticiones corre en un hilo aparte con vigilante:
// si no avanza en 3 s se anota el cuelgue, se deja de usar Kermit y se sigue con lo demás.
// Los resultados se escriben al terminar cada prueba, así un cierre de Adrenaline no borra lo anterior.
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspsysmem_kernel.h>
#include <pspge.h>
#include <psppower.h>
#include <pspinit.h>
#include <string.h>
#include <stdarg.h>
#include <systemctrl.h>
#include "kbench.h"
#include "walk.h"

PSP_MODULE_INFO("KBench", 0x1000, 1, 0);

#ifndef KB_KERMIT_NID
#define KB_KERMIT_NID 0x36666181  // sceKermitSendRequest (libpspkermit_driver.a de Adrenaline 8)
#endif
#define PA(a)  ((u32)(a) & 0x0FFFFFFF)                      // dirección física (0x08xxxxxx)
#define UNC(a) ((void *)(PA(a) | 0xA0000000))               // vista sin caché de kernel
#define KSEG(a) ((void *)(PA(a) | 0x80000000))              // vista con caché de kernel
#define NOW() sceKernelGetSystemTimeLow()

static int (*kermitSend)(KbKermitReq *req, u32 mode, u32 cmd, u32 nargs, u32 is_cb, u64 *resp);
static KbKermitReq g_req[4] __attribute__((aligned(64)));   // una petición por hilo que envía
static u32 g_seqn[4];
static volatile int g_state;          // 0 esperando, 1 corriendo, 2 terminado
static volatile int g_tr = 1;         // transporte: 0 Kermit, 1 memoria compartida
static volatile int g_kdead = 1, g_sdead;  // 1 = ese transporte no se usa más (Kermit empieza apagado)
static volatile u32 g_shmSleep;       // µs entre sondeos del PSP en el canal compartido (0 = continuo)
#define g_dead (g_tr ? g_sdead : g_kdead)
#define SET_DEAD() (*(g_tr ? &g_sdead : &g_kdead) = 1)
#define TR (g_tr ? "mem" : "kermit")
static int g_eboot, g_echoErr, g_fallas;
static char g_prog[128] = "?";
static char g_status[160], g_dir[] = "ms0:/kbench", g_resumen[48] = "ms0:/kbench/resumen.txt";

// ---------- texto ----------
// printf mínimo (%d %u %x %X %s %c, ancho y relleno con 0): no depende de la libc del kernel.
static int kbfmt(char *b, int len, const char *f, va_list ap) {
  int n = 0;
#define PUT(c) do { if (n < len - 1) b[n] = (c); n++; } while (0)
  for (; *f; f++) {
    if (*f != '%') { PUT(*f); continue; }
    f++;
    int zero = 0, w = 0;
    if (*f == '-') f++;  // alineación a la izquierda: se ignora (sin relleno)
    if (*f == '0') { zero = 1; f++; }
    while (*f >= '0' && *f <= '9') w = w * 10 + *f++ - '0';
    while (*f == 'l') f++;
    char t[16]; int k = 0;
    if (*f == 's') { const char *s = va_arg(ap, const char *); if (!s) s = "(null)"; int l = strlen(s); for (; l < w; l++) PUT(' '); while (*s) PUT(*s++); continue; }
    if (*f == 'c') { PUT((char)va_arg(ap, int)); continue; }
    if (*f == '%') { PUT('%'); continue; }
    u32 v = va_arg(ap, u32), neg = 0, base = (*f == 'x' || *f == 'X') ? 16 : 10;
    if (*f == 'd' && (int)v < 0) { neg = 1; v = -(int)v; }
    do { t[k++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    if (neg) { if (zero) PUT('-'); else t[k++] = '-'; w--; }
    for (int i = k; i < w; i++) PUT(zero ? '0' : ' ');
    while (k) PUT(t[--k]);
  }
  b[n < len ? n : len - 1] = 0;
#undef PUT
  return n;
}
static int kbsprintf(char *b, int len, const char *f, ...) { va_list ap; va_start(ap, f); int n = kbfmt(b, len, f, ap); va_end(ap); return n; }

static void fileAppend(const char *path, const char *s, int len, int trunc) {
  SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | (trunc ? PSP_O_TRUNC : PSP_O_APPEND), 0777);
  if (fd >= 0) { sceIoWrite(fd, s, len); sceIoClose(fd); }
}

// Línea al resumen (se escribe al instante) y al estado que muestra el EBOOT
static void out(const char *f, ...) {
  char b[256]; va_list ap; va_start(ap, f);
  int n = kbfmt(b, sizeof b - 1, f, ap); va_end(ap);
  if (n > (int)sizeof b - 2) n = sizeof b - 2;
  fileAppend(g_resumen, b, n, 0);
  int i = 0; for (; i < n && i < (int)sizeof g_status - 1 && b[i] != '\n'; i++) g_status[i] = b[i];
  g_status[i] = 0;
}

// CSV: se acumula en memoria y se baja al archivo cuando se llena y al cerrar
static char g_csv[0x8000], g_csvPath[48];
static int g_csvN;
static void csvOpen(const char *name, const char *cab) {
  kbsprintf(g_csvPath, sizeof g_csvPath, "%s/%s.csv", g_dir, name);
  fileAppend(g_csvPath, cab, strlen(cab), 1);
  g_csvN = 0;
}
static void csvFlush(void) { if (g_csvN) fileAppend(g_csvPath, g_csv, g_csvN, 0); g_csvN = 0; }
static void csv(const char *f, ...) {
  if (g_csvN > (int)sizeof g_csv - 300) csvFlush();
  va_list ap; va_start(ap, f);
  int n = kbfmt(g_csv + g_csvN, sizeof g_csv - g_csvN, f, ap); va_end(ap);
  g_csvN += n;
}

static int cistr(const char *s, const char *t) {  // contiene t, sin distinguir mayúsculas
  for (; s && *s; s++) {
    int i = 0;
    while (t[i] && s[i] && (s[i] | 32) == (t[i] | 32)) i++;
    if (!t[i]) return 1;
  }
  return 0;
}

// ---------- estadísticas ----------
typedef struct { u32 n, min, p50, p90, p99, max, avg; } St;
static void stats(u32 *v, int n, St *o) {  // ordena v
  memset(o, 0, sizeof *o);
  if (n <= 0) return;
  for (int gap = n / 2; gap; gap /= 2)
    for (int i = gap; i < n; i++) { u32 t = v[i]; int j = i; for (; j >= gap && v[j - gap] > t; j -= gap) v[j] = v[j - gap]; v[j] = t; }
  u64 s = 0; for (int i = 0; i < n; i++) s += v[i];
  o->n = n; o->min = v[0]; o->max = v[n - 1]; o->avg = (u32)(s / n);
  o->p50 = v[n / 2]; o->p90 = v[n * 9 / 10]; o->p99 = v[n * 99 / 100];
}
static void outSt(const char *nombre, St *s) {
  out("  %s: n=%u min=%u p50=%u p90=%u p99=%u max=%u prom=%u us\n", nombre, s->n, s->min, s->p50, s->p90, s->p99, s->max, s->avg);
}

// ---------- memoria ----------
static void *kalloc(int part, u32 size, const char *name) {
  SceUID id = sceKernelAllocPartitionMemory(part, name, PSP_SMEM_Low, size + 64, NULL);
  if (id < 0) { out("  sin memoria: %s (%u bytes, particion %d): %08x\n", name, size, part, id); return NULL; }
  return (void *)(((u32)sceKernelGetBlockHeadAddr(id) + 63) & ~63);
}

// Página compartida con el ARM en 0x0A000000. En Adrenaline 8 el homebrew tiene 40 MB de partición 2
// (hasta 0x0B000000) y esa zona es memoria libre que se limpia al cargar el EBOOT: por eso se reserva
// (partición 2 y, si no, la 11) y la marca se escribe con el EBOOT ya corriendo, no en module_start.
static SceUID g_page = -1;
static u32 g_nonce;
static void connectArm(void) {
  volatile u32 *p = UNC(KB_MBOX);
  if (g_page < 0) {
    static const int part[2] = {2, 11};
    for (int i = 0; i < 2 && g_page < 0; i++) {
      g_page = sceKernelAllocPartitionMemory(part[i], "kbench_buzon", PSP_SMEM_Addr, 0x1000, (void *)KB_MBOX);
      kbsprintf(g_status, sizeof g_status, "reserva de la pagina 0x0A000000 en particion %d: %08x", part[i], g_page);
    }
    for (int i = 0; i < 0x400; i++) p[i] = 0;
    g_nonce = NOW() | 1;
  }
  p[(KB_PSP - KB_MBOX) / 4 + 1] = g_nonce;
  p[(KB_PSP - KB_MBOX) / 4] = KB_PSP_MAGIC;
}
static int markOk(void) { return ((volatile u32 *)UNC(KB_PSP))[0] == KB_PSP_MAGIC; }
static void disconnectArm(void) { ((volatile u32 *)UNC(KB_PSP))[0] = 0; }

// ---------- Kermit ----------
static volatile KbMbox *mbox(void) { return (volatile KbMbox *)UNC(KB_MBOX); }

static const char *armCheck(void) {
  volatile KbMbox *m = mbox();
  static int borrada;
  if (!markOk()) connectArm();
  for (int i = 0; i < 300 && m->magic != KB_MAGIC; i++) {  // el ARM despierta cada 100 ms; si Adrenaline recién abrió, tarda unos segundos
    if (!markOk()) { borrada++; connectArm(); }  // alguien limpió la página: se vuelve a marcar
    if (i % 10 == 0) kbsprintf(g_status, sizeof g_status, "esperando al ARM... %d s (marca reescrita %d veces)", i / 10, borrada);
    sceKernelDelayThread(100000);
  }
  if (m->magic != KB_MAGIC) {  // volcado para diagnóstico: buzón, marca del PSP y autoprueba del ARM
    volatile u32 *u = UNC(KB_MBOX);
    sceKernelDcacheInvalidateRange(KSEG(KB_MBOX), 0x400);
    u32 *c = KSEG(KB_MBOX);
    out("volcado de 0x0A000000 (sin cache): buzon %08x %08x %08x %08x; marca %08x %08x; prueba %08x %08x %08x %08x\n",
        u[0], u[1], u[2], u[3], u[0x20], u[0x21], u[0x80], u[0x81], u[0x82], u[0x83]);
    out("  con cache: buzon %08x %08x; marca %08x; prueba %08x %08x %08x %08x\n", c[0], c[4], c[0x20], c[0x80], c[0x81], c[0x82], c[0x83]);
    return "no hay buzon del ARM (kbench.suprx no cargado o sin iniciar)";
  }
  if (m->version != KB_VERSION) return "el ARM tiene otra version de KBENCH";
  if (m->mode != KB_MODE) return "el ARM usa otro modo Kermit (compilar ambos con el mismo ADRENALINE_SRC)";
  if (m->status == 2) return "el ARM no reconoce los offsets de ScePspemu (ver ux0:data/kbench/arm.log)";
  if (m->status != 0) return "el ARM todavia no esta listo";
  u32 c = m->counter;
  sceKernelDelayThread(200000);
  if (m->counter == c) return "el buzon no se actualiza (ARM detenido)";
  return NULL;
}

typedef struct { u32 seq, t0, t3, res; int ret; } Smp;

// Petición por memoria compartida: escribir argumentos, tocar el timbre y esperar el eco.
// g_shmSleep = 0 espera activa (mide la latencia pura); > 0 duerme entre sondeos (libera la CPU).
static int shmReq(u32 cmd, const u32 *a, int na, Smp *s) {
  volatile u32 *q = UNC(KB_SHM_REQ), *p = UNC(KB_SHM_RES);
  q[1] = cmd; q[2] = na + 1; q[4] = s->seq;
  for (int i = 0; i < 7; i++) q[5 + i] = i < na ? a[i] : 0;
  __asm__ volatile("sync");
  s->t0 = NOW();
  q[0] = s->seq;
  while (p[0] != s->seq) {
    if (NOW() - s->t0 > 2000000) { s->t3 = NOW(); s->ret = -3; return -3; }
    if (g_shmSleep) sceKernelDelayThread(g_shmSleep);
  }
  s->t3 = NOW();
  s->res = p[1];
  if (p[2] != (s->seq ^ KB_ECHO)) { g_echoErr++; s->ret = -2; }
  return s->ret;
}

// Una petición por el transporte actual. Mide en el mismo hilo, justo alrededor del envío.
static int kreq(int slot, u32 cmd, const u32 *a, int na, Smp *s) {
  Smp tmp; if (!s) s = &tmp;
  memset(s, 0, sizeof *s);
  if (g_dead) { s->ret = -1; return -1; }
  s->seq = (slot << 28) | (++g_seqn[slot] & 0x0FFFFFFF);
  if (g_tr) {
    if (shmReq(cmd, a, na, s) == -3) { g_sdead = 1; out("memoria compartida: el ARM no respondio en 2 s (cmd %u)\n", cmd); }
    return s->ret;
  }
  sceKernelDcacheInvalidateRange(&g_req[slot], sizeof(KbKermitReq));
  KbKermitReq *u = UNC(&g_req[slot]);
  u->args[0] = s->seq;
  for (int i = 0; i < na; i++) u->args[i + 1] = a[i];
  u64 r = 0;
  s->t0 = NOW();
  s->ret = kermitSend(u, KB_MODE, cmd, na + 1, 0, &r);
  s->t3 = NOW();
  s->res = (u32)r;
  if ((u32)(r >> 32) != (s->seq ^ KB_ECHO)) { g_echoErr++; if (s->ret >= 0) s->ret = -2; }
  return s->ret;
}

// Lote de peticiones en un hilo aparte, con vigilante en el hilo que llama
typedef struct {
  int slot, n, cmd, na; u32 a[6]; u32 spacing;  // spacing: µs entre peticiones
  Smp *s; volatile int prog, fin;
} Batch;

static int batchThread(SceSize args, void *argp) {
  Batch *b = *(Batch **)argp;
  for (int i = 0; i < b->n && !g_dead; i++) {
    if (b->spacing) sceKernelDelayThread(b->spacing);
    kreq(b->slot, b->cmd, b->a, b->na, &b->s[i]);
    b->prog = i + 1;
  }
  b->fin = 1;
  return sceKernelExitDeleteThread(0);
}

static int startBatch(Batch *b, int prio) {
  b->prog = 0; b->fin = 0;
  SceUID th = sceKernelCreateThread("kb_lote", batchThread, prio, 0x2000, 0, NULL);
  if (th < 0) { out("  no se pudo crear el hilo: %08x\n", th); b->fin = 1; return -1; }
  sceKernelStartThread(th, sizeof(Batch *), &b);
  return 0;
}

// Espera uno o dos lotes; si alguno no avanza en 3 s, es un cuelgue
static int waitBatches(Batch **bs, int nb, const char *prueba) {
  int last[2] = {-1, -1}; u32 tl[2] = {NOW(), NOW()};
  for (;;) {
    int fin = 1;
    for (int k = 0; k < nb; k++) {
      if (bs[k]->fin) continue;
      fin = 0;
      if (bs[k]->prog != last[k]) { last[k] = bs[k]->prog; tl[k] = NOW(); }
      else if (NOW() - tl[k] > 3000000) {
        volatile KbMbox *m = mbox();
        SET_DEAD(); g_fallas++;
        out("CUELGA en %s (muestra %d): el ARM recibio %u peticiones y respondio %u, ultimo cmd %u. "
            "No se usa mas Kermit en esta sesion.\n", prueba, bs[k]->prog, m->seen, m->answered, m->last_cmd);
        return -1;
      }
    }
    if (fin) return 0;
    sceKernelDelayThread(20000);
  }
}

static int runBatch(Batch *b, const char *prueba) {
  if (startBatch(b, 0x18) < 0) return -1;
  return waitBatches(&b, 1, prueba);
}

// Trae el registro de tiempos del ARM (y lo vacía)
static int fetchLog(KbEnt *dst, int max) {
  sceKernelDcacheWritebackInvalidateRange(dst, max * sizeof(KbEnt));
  Batch b = {0}; Smp s;
  b.slot = 0; b.n = 1; b.cmd = KB_GETLOG; b.na = 3; b.a[0] = PA(dst); b.a[1] = max; b.a[2] = 1; b.s = &s;
  if (runBatch(&b, "registro del ARM") < 0 || s.ret < 0 || s.res > (u32)max) return 0;
  return s.res;
}

// Escribe en el CSV las muestras unidas con los tiempos del ARM. Devuelve las RTT en rtt[].
static int joinCsv(const char *prueba, Smp *s, int n, KbEnt *e, int ne, u32 *rtt, u32 *svc, u32 x1, u32 x2) {
  KbEnt *el = UNC(e);
  int j = 0, ok = 0, nsvc = 0;
  for (int i = 0; i < n; i++) {
    while (j < ne && el[j].seq != s[i].seq) j++;
    int hay = j < ne;
    u32 r = s[i].t3 - s[i].t0;
    if (s[i].ret >= 0) rtt[ok++] = r;
    KbEnt z = {0}; KbEnt *q = hay ? &el[j] : &z;
    if (hay && svc) svc[nsvc++] = q->t3 - q->t0;
    csv("%s,%d,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", prueba, i, s[i].seq, s[i].ret, s[i].t0, s[i].t3, r,
        q->t0, q->t1, q->t2, q->t3, q->t3 - q->t0, s[i].res, x1, x2);
    if (hay) j++; else j = 0;
  }
  return ok;
}
#define CAB_PING "prueba,i,seq,ret,psp_t0,psp_t3,rtt_us,arm_t0,arm_t1,arm_t2,arm_t3,arm_svc_us,res,x1,x2\n"

// ---------- pruebas ----------
static u8 *g_buf;          // 1 MB: lista sintética y salida
static u32 *g_outP;        // salida del recorrido en el PSP
static KbEnt *g_log;       // 4096 entradas
static Smp *g_smp;         // 2048 muestras
static u32 *g_v, *g_v2;    // 2048 valores para estadísticas
#define BUF_SZ 0x100000
#define NSMP 2048

static int kermitOk(const char *prueba) {
  if (g_dead) { out("%s (%s): omitida, transporte no disponible\n", prueba, TR); return 0; }
  const char *e = armCheck();
  if (e) { out("%s: omitida: %s\n", prueba, e); g_sdead = g_kdead = 1; return 0; }
  return 1;
}

static void shmPoll(u32 us) { u32 a = us; int t = g_tr; g_tr = 1; kreq(0, KB_SHMPOLL, &a, 1, NULL); g_tr = t; }

// Elige el transporte para las pruebas que miden trabajo en el ARM: Kermit si está, si no el compartido
static void useBest(void) {
  if (!g_kdead) { g_tr = 0; return; }
  g_tr = 1; g_shmSleep = 0; shmPoll(0);
}

// Lote de pings y su análisis. El nombre lleva el transporte delante.
static void pingLote(const char *prueba, int n, u32 spacing, u32 x1, u32 x2) {
  char nm[48]; kbsprintf(nm, sizeof nm, "%s_%s", TR, prueba);
  Batch b = {0};
  b.slot = 0; b.n = n; b.cmd = KB_PING; b.spacing = spacing; b.s = g_smp;
  fetchLog(g_log, 1);  // vacía el registro
  if (runBatch(&b, nm) < 0) return;
  int ne = fetchLog(g_log, 4096);
  int ok = joinCsv(nm, g_smp, n, g_log, ne, g_v, g_v2, x1, x2);
  St s; out("%s (x1=%x x2=%x): %d/%d bien\n", nm, x1, x2, ok, n);
  stats(g_v, ok, &s); outSt("ida y vuelta (PSP)", &s);
  stats(g_v2, ne < n ? ne : n, &s); outSt("servicio en el ARM", &s);
}

// Activa Kermit: el ARM crea su hilo (si no está en modo PS1), se espera a que esté escuchando y se
// prueba con un saludo vigilado. Si algo falla, Kermit queda apagado y se sigue con el compartido.
static void __attribute__((unused)) kermitUp(void) {
  if (!kermitSend) { out("Kermit: no se encontro sceKermitSendRequest (NID %08x)\n", KB_KERMIT_NID); return; }
  g_tr = 1;
  Smp s; kreq(0, KB_KERMIT_ON, NULL, 0, &s);
  if (s.ret < 0 || s.res != 1) { out("Kermit: el ARM no lo activo (res=%08x, modo PS1=%d): solo memoria compartida\n", s.res, !!(mbox()->flags & KB_MB_POPS)); return; }
  int i;
  for (i = 0; i < 50 && !(mbox()->flags & KB_MB_KERMIT); i++) sceKernelDelayThread(20000);
  if (i == 50) { out("Kermit: el hilo del ARM no aparece en el buzon\n"); return; }
  sceKernelDelayThread(20000);
  g_tr = 0; g_kdead = 0;
  Batch b = {0}; b.n = 1; b.cmd = KB_HELLO; b.s = &s;
  if (runBatch(&b, "saludo por Kermit") < 0) return;  // runBatch ya apagó Kermit y lo anotó
  out("saludo por Kermit: ret=%08x version=%u rtt=%u us\n", s.ret, s.res, s.t3 - s.t0);
  if (s.ret < 0 || s.res != KB_VERSION) { out("Kermit responde mal: otro servicio podria estar usando el modo %d. Se apaga.\n", KB_MODE); g_kdead = 1; g_fallas++; }
}

static void pInfo(void) {
  volatile KbMbox *m = mbox();
  PspSysmemPartitionInfo pi;
  out("== KBENCH %d (modo Kermit %d) ==\n", KB_VERSION, KB_MODE);
  out("programa: %s\n", g_prog);
  out("PSP: cpu=%d MHz bus=%d MHz\n", scePowerGetCpuClockFrequencyInt(), scePowerGetBusClockFrequencyInt());
  for (int p = 2; p <= 11; p += 9) {
    pi.size = sizeof pi;
    int r = sceKernelQueryMemoryPartitionInfo(p, &pi);
    if (r >= 0) out("particion %d: %08x-%08x (%u KB)\n", p, pi.startaddr, pi.startaddr + pi.memsize, pi.memsize >> 10);
    else out("particion %d: no existe (%08x)\n", p, r);
  }
  out("%s\n", g_status);  // resultado de la reserva del buzón
  out("sceKermitSendRequest: %08x (NID %08x)\n", (u32)kermitSend, KB_KERMIT_NID);
  out("buzon: magic=%08x version=%u modo=%u estado=%u contador=%u recibidas=%u respondidas=%u flags=%x\n",
      m->magic, m->version, m->mode, m->status, m->counter, m->seen, m->answered, m->flags);
  out("ARM: arm=%u MHz bus=%u gpu=%u xbar=%u ScePspemu .text=%08x\n", m->arm_mhz, m->bus_mhz, m->gpu_mhz, m->xbar_mhz, m->text_addr);
  g_tr = 1;
  if (!kermitOk("saludo")) return;
  Smp s;
  kreq(0, KB_HELLO, NULL, 0, &s);
  out("saludo por memoria compartida: ret=%08x version ARM=%u rtt=%u us\n", s.ret, s.res, s.t3 - s.t0);
  if (s.ret < 0 || s.res != KB_VERSION) { out("el saludo fallo: no se usa el ARM\n"); g_sdead = 1; g_fallas++; return; }
  KbInfo *inf = (KbInfo *)g_log;
  sceKernelDcacheWritebackInvalidateRange(inf, sizeof *inf);
  u32 a = PA(inf);
  kreq(0, KB_INFO, &a, 1, &s);
  inf = UNC(inf);
  out("ScePspemu: .text=%08x tam=%x\n", inf->text_addr, inf->text_size);
  for (int i = 0; i < 4; i++)
    out("  offset %04x: %02x %02x %02x %02x %02x %02x %02x %02x\n", inf->off[i], inf->head[i][0], inf->head[i][1],
        inf->head[i][2], inf->head[i][3], inf->head[i][4], inf->head[i][5], inf->head[i][6], inf->head[i][7]);
  // Kermit (modo 14) lo atiende el hilo de PS1 de Adrenaline: no se usa (ver kbench.md)
}

static void pPing(void) {
  csvOpen("ping", CAB_PING);
  g_tr = 0;
  if (!g_kdead && kermitOk("pings")) {
    pingLote("calentamiento", 50, 0, 0, 0);
    pingLote("rafaga", 1000, 0, 0, 0);
    pingLote("cada_2ms", 300, 2000, 0, 0);
    pingLote("cada_16ms", 300, 16667, 0, 0);  // ritmo de un frame
  }
  // Memoria compartida con tres ritmos de sondeo en el ARM (x1 = µs entre sondeos; 0 = continuo en ráfagas)
  static const u32 poll[3] = {0, 100, 1000};
  g_tr = 1; g_shmSleep = 0;
  for (int i = 0; i < 3 && kermitOk("pings compartidos"); i++) {
    shmPoll(poll[i]);
    pingLote("rafaga", 1000, 0, poll[i], 0);
    pingLote("cada_2ms", 300, 2000, poll[i], 0);
    pingLote("cada_16ms", 300, 16667, poll[i], 0);
  }
  shmPoll(1000);
  csvFlush();
}

static void pAfinidad(void) {
  static const u32 mk[4] = {0x70000, 0x10000, 0x20000, 0x40000}, pr[2] = {0x10000100, 64};
  useBest();
  if (!kermitOk("afinidad")) return;
  csvOpen("afinidad", CAB_PING);
  out("afinidad y prioridad del hilo del ARM que atiende (%s) (mascara, prioridad):\n", TR);
  for (int i = 0; i < 4 && !g_dead; i++)
    for (int k = 0; k < 2 && !g_dead; k++) {
      Smp s1, s2;
      kreq(0, KB_AFFINITY, &mk[i], 1, &s1);
      kreq(0, KB_PRIORITY, &pr[k], 1, &s2);
      if (s1.ret < 0 || s2.ret < 0 || (int)s1.res < 0 || (int)s2.res < 0)
        out("  cambio rechazado: afinidad %08x prioridad %08x\n", s1.res, s2.res);
      pingLote("afinidad", 300, 0, mk[i], pr[k]);
    }
  kreq(0, KB_AFFINITY, &mk[0], 1, NULL);
  kreq(0, KB_PRIORITY, &pr[0], 1, NULL);
  csvFlush();
}

static void fillPat(u32 *p, u32 words, u32 seed) { for (u32 i = 0; i < words; i++) p[i] = kbPat(seed, i); }

static void pLectura(void) {
  static const u32 tam[6] = {1024, 4096, 16384, 65536, 262144, 1048576};
  useBest();
  if (!kermitOk("lectura")) return;
  csvOpen("lectura", "prueba,bytes,flags,rep,ret,rtt_us,arm_conv_us,arm_leer_us,arm_svc_us,ok\n");
  out("lectura de la RAM del PSP desde el ARM (KB_READ, por %s):\n", TR);
  for (int t = 0; t < 6 && !g_dead; t++)
    for (int fl = 1; fl >= 0 && !g_dead; fl--) {
      int ok = 0, n = 0;
      fetchLog(g_log, 1);
      for (int r = 0; r < 10 && !g_dead; r++) {
        u32 seed = NOW() + r;
        fillPat((u32 *)g_buf, tam[t] / 4, seed);
        u32 esp = kbSum((u32 *)g_buf, tam[t] / 4);
        sceKernelDcacheWritebackRange(g_buf, tam[t]);
        Batch b = {0}; b.n = 1; b.cmd = KB_READ; b.na = 3; b.a[0] = PA(g_buf); b.a[1] = tam[t]; b.a[2] = fl; b.s = &g_smp[r];
        if (runBatch(&b, "lectura") < 0) break;
        g_smp[r].t0 = g_smp[r].t0; g_v2[r] = g_smp[r].res == esp; n++;
      }
      int ne = fetchLog(g_log, 64);
      KbEnt *e = UNC(g_log);
      for (int r = 0; r < n; r++) {
        KbEnt z = {0}, *q = r < ne ? &e[r] : &z;
        ok += g_v2[r];
        g_v[r] = g_smp[r].t3 - g_smp[r].t0;
        csv("lectura,%u,%d,%d,%d,%u,%u,%u,%u,%u\n", tam[t], fl, r, g_smp[r].ret, g_v[r], q->t1 - q->t0, q->t2 - q->t1, q->t3 - q->t0, g_v2[r]);
      }
      St s; stats(g_v, n, &s);
      out("  %7u bytes %s: datos correctos %d/%d, rtt p50=%u min=%u us\n", tam[t], fl ? "invalidando" : "sin invalidar", ok, n, s.p50, s.min);
      if (fl && ok != n) g_fallas++;
    }
  csvFlush();
}

static void pEscritura(void) {
  static const u32 tam[6] = {1024, 4096, 16384, 65536, 262144, 1048576};
  useBest();
  if (!kermitOk("escritura")) return;
  csvOpen("escritura", "prueba,bytes,rep,ret,rtt_us,arm_escribir_us,arm_wb_us,arm_svc_us,malas_sin_cache,malas_con_cache\n");
  out("escritura del ARM en la RAM del PSP (KB_WRITE + writeback, por %s):\n", TR);
  for (int t = 0; t < 6 && !g_dead; t++) {
    int n = 0, ok = 0;
    fetchLog(g_log, 1);
    for (int r = 0; r < 10 && !g_dead; r++) {
      u32 seed = NOW() ^ (r << 20);
      memset(g_buf, 0, tam[t]);
      sceKernelDcacheWritebackInvalidateRange(g_buf, tam[t]);
      Batch b = {0}; b.n = 1; b.cmd = KB_WRITE; b.na = 3; b.a[0] = PA(g_buf); b.a[1] = tam[t]; b.a[2] = seed; b.s = &g_smp[r];
      if (runBatch(&b, "escritura") < 0) break;
      u32 *un = UNC(g_buf), m1 = 0, m2 = 0;
      for (u32 i = 0; i < tam[t] / 4; i++) m1 += un[i] != kbPat(seed, i);
      sceKernelDcacheInvalidateRange(g_buf, tam[t]);
      for (u32 i = 0; i < tam[t] / 4; i++) m2 += ((u32 *)g_buf)[i] != kbPat(seed, i);
      g_v[r] = m1; g_v2[r] = m2; n++; ok += !m1 && !m2;
    }
    int ne = fetchLog(g_log, 64);
    KbEnt *e = UNC(g_log);
    u32 rt[10];
    for (int r = 0; r < n; r++) {
      KbEnt z = {0}, *q = r < ne ? &e[r] : &z;
      rt[r] = g_smp[r].t3 - g_smp[r].t0;
      csv("escritura,%u,%d,%d,%u,%u,%u,%u,%u,%u\n", tam[t], r, g_smp[r].ret, rt[r], q->t2 - q->t1, q->t3 - q->t2, q->t3 - q->t0, g_v[r], g_v2[r]);
    }
    St s; stats(rt, n, &s);
    out("  %7u bytes: correctas %d/%d, rtt p50=%u min=%u us\n", tam[t], ok, n, s.p50, s.min);
    if (ok != n) g_fallas++;
  }
  csvFlush();
}

// Recorrido de la lista sintética: PSP contra ARM
static u32 g_list, g_bestT, g_pspT;
static int g_best;
static KbWalkStats g_ref;
// Formas de leer la RAM del PSP desde el ARM (lectura y recorrido)
static const struct { const char *nom; u32 read, walk; } VAR[] = {
  {"actual",       KB_F_INVAL,                            KB_F_INVAL | KB_F_WBACK},
  {"con_cache",    KB_F_CACHED | KB_F_INVAL,              KB_F_CACHED | KB_F_INVAL},
  {"copia",        KB_F_COPY,                             KB_F_COPY | KB_F_WBACK},
  {"copia_cache",  KB_F_COPY | KB_F_CACHED | KB_F_INVAL,  KB_F_COPY | KB_F_CACHED | KB_F_INVAL},
  {"dma",          KB_F_DMA,                              KB_F_DMA | KB_F_WBACK},
  {"dma_cache",    KB_F_DMA | KB_F_CACHED | KB_F_INVAL,   0},
};
#define NVAR (int)(sizeof VAR / sizeof VAR[0])

// Lectura de 64 KB y 1 MB con cada forma; verifica la suma
static void pVistas(void) {
  static const u32 tam[2] = {65536, 1048576};
  useBest();
  if (!kermitOk("formas de lectura")) return;
  csvOpen("vistas", "forma,bytes,rep,ret,rtt_us,arm_preparar_us,arm_leer_us,ok\n");
  out("formas de leer la RAM del PSP desde el ARM (5 repeticiones):\n");
  for (int t = 0; t < 2 && !g_dead; t++) {
    fillPat((u32 *)g_buf, tam[t] / 4, 0x1234 + t);
    u32 esp = kbSum((u32 *)g_buf, tam[t] / 4);
    sceKernelDcacheWritebackInvalidateAll();
    for (int k = 0; k < NVAR && !g_dead; k++) {
      int n = 0, ok = 0; u32 v[5];
      fetchLog(g_log, 1);
      for (int r = 0; r < 5 && !g_dead; r++) {
        Batch b = {0}; b.n = 1; b.cmd = KB_READ; b.na = 3; b.a[0] = PA(g_buf); b.a[1] = tam[t]; b.a[2] = VAR[k].read; b.s = &g_smp[r];
        if (runBatch(&b, "formas de lectura") < 0) break;
        v[n] = g_smp[r].t3 - g_smp[r].t0; ok += g_smp[r].ret >= 0 && g_smp[r].res == esp; n++;
      }
      int ne = fetchLog(g_log, 16);
      KbEnt *e = UNC(g_log);
      for (int r = 0; r < n; r++) {
        KbEnt z = {0}, *q = r < ne ? &e[r] : &z;
        csv("%s,%u,%d,%d,%u,%u,%u,%d\n", VAR[k].nom, tam[t], r, g_smp[r].ret, g_smp[r].t3 - g_smp[r].t0, q->t1 - q->t0, q->t2 - q->t1, g_smp[r].res == esp);
      }
      St s1; stats(v, n, &s1);
      out("  %7u bytes %-12s: p50=%u us min=%u us, correctas %d/%d\n", tam[t], VAR[k].nom, s1.p50, s1.min, ok, n);
      if (ok != n) g_fallas++;
    }
  }
  csvFlush();
}

static void pRecorrido(void) {
  out("recorrido de una lista sintetica tipo GT (mismo codigo en PSP y ARM):\n");
  memset(g_buf, 0, BUF_SZ);
  g_list = kbGen(KSEG(g_buf), PA(g_buf), BUF_SZ, 55000, 1234);
  if (!g_list) { out("  kbGen fallo\n"); g_fallas++; return; }
  csvOpen("recorrido", "lado,variante,rep,cpu_mhz,total_us,cmds,ns_por_cmd,arm_conv_us,arm_recorrer_us,arm_wb_us,ok\n");
  u32 outW = (KB_GEN_OUT - sizeof(KbWalkStats)) / 4;
  KbWalkStats st;
  for (int var = 0; var < 3; var++) {  // 0: con caché, 1: sin caché, 2: con caché a 222 MHz
    if (var == 2) { int r = scePowerSetClockFrequency(222, 222, 111); out("  bajar a 222 MHz: %08x\n", r); sceKernelDelayThread(100000); }
    u32 mhz = scePowerGetCpuClockFrequencyInt(), v[5];
    const u8 *mem = var == 1 ? UNC(g_buf) : KSEG(g_buf);
    int reps = var == 1 ? 2 : 5;
    for (int r = 0; r < reps; r++) {
      memset(g_outP, 0, KB_GEN_OUT);
      u32 t = NOW();
      kbWalk(mem, PA(g_buf), BUF_SZ, g_list, g_outP, outW, 1000000, &st);
      v[r] = NOW() - t;
      csv("psp,%s,%d,%u,%u,%u,%u,0,0,0,%d\n", var == 1 ? "sin_cache" : "con_cache", r, mhz, v[r], st.cmds, v[r] * 1000 / st.cmds, st.errors == 0);
    }
    St s; stats(v, reps, &s);
    out("  PSP %s a %u MHz: %u cmds, %u draws, %u us (%u ns por comando)\n", var == 1 ? "sin cache" : "con cache", mhz, st.cmds, st.prims, s.p50, s.p50 * 1000 / st.cmds);
    if (var == 0) { g_ref = st; g_pspT = s.p50; }
    if (var == 2) { int r = scePowerSetClockFrequency(333, 333, 166); out("  volver a 333 MHz: %08x (cpu=%d)\n", r, scePowerGetCpuClockFrequencyInt()); }
  }
  // Salida de referencia para comparar la del ARM
  memset(g_outP, 0, KB_GEN_OUT);
  kbWalk(KSEG(g_buf), PA(g_buf), BUF_SZ, g_list, g_outP, outW, 1000000, &st);
  if (st.errors || st.sum != g_ref.sum) { out("  el recorrido en el PSP no es determinista\n"); g_fallas++; }

  useBest();
  if (kermitOk("recorrido en el ARM")) {
    u8 *outA = g_buf + BUF_SZ - KB_GEN_OUT;
    for (int k = 0; k < NVAR && !g_dead; k++) {
      u32 fl = VAR[k].walk;
      if (!fl) continue;
      int n = 0, okN = 0;
      fetchLog(g_log, 1);
      for (int r = 0; r < 5 && !g_dead; r++) {
        memset(outA, 0, KB_GEN_OUT);
        sceKernelDcacheWritebackInvalidateAll();  // como hace GePatch al final de cada recorrido
        Batch b = {0}; b.n = 1; b.cmd = KB_WALK; b.na = 6;
        b.a[0] = PA(g_buf); b.a[1] = BUF_SZ; b.a[2] = g_list; b.a[3] = PA(outA); b.a[4] = KB_GEN_OUT; b.a[5] = fl; b.s = &g_smp[r];
        if (runBatch(&b, "recorrido ARM") < 0) break;
        KbWalkStats *as = UNC(outA);
        u32 *ao = (u32 *)((u8 *)UNC(outA) + sizeof(KbWalkStats)), mal = 0;
        for (u32 i = 0; i < outW; i++) mal += ao[i] != g_outP[i];
        int ok = g_smp[r].ret >= 0 && g_smp[r].res == g_ref.sum && as->cmds == g_ref.cmds && as->prims == g_ref.prims && !as->errors && !mal;
        if (!ok) out("  ARM %s rep %d: suma %08x/%08x cmds %u errores %u palabras distintas %u\n", VAR[k].nom, r, g_smp[r].res, g_ref.sum, as->cmds, as->errors, mal);
        g_v2[r] = ok; okN += ok; n++;
      }
      int ne = fetchLog(g_log, 64);
      KbEnt *e = UNC(g_log);
      u32 tw[5], tt[5], tc[5];
      for (int r = 0; r < n; r++) {
        KbEnt z = {0}, *q = r < ne ? &e[r] : &z;
        tc[r] = q->t1 - q->t0; tw[r] = q->t2 - q->t1; tt[r] = g_smp[r].t3 - g_smp[r].t0;
        csv("arm_%s,%s,%d,%u,%u,%u,%u,%u,%u,%u,%d\n", TR, VAR[k].nom, r, ((volatile KbMbox *)mbox())->arm_mhz,
            tt[r], g_ref.cmds, tw[r] * 1000 / g_ref.cmds, tc[r], tw[r], q->t3 - q->t2, g_v2[r]);
      }
      St s1, s2, s3; stats(tw, n, &s1); stats(tt, n, &s2); stats(tc, n, &s3);
      out("  ARM %-12s: preparar p50=%u us, recorrer p50=%u us (%u ns por comando), total p50=%u us, correctos %d/%d\n",
          VAR[k].nom, s3.p50, s1.p50, s1.p50 * 1000 / g_ref.cmds, s2.p50, okN, n);
      if (okN != n) g_fallas++;
      else if (n && (!g_bestT || s2.p50 < g_bestT)) { g_bestT = s2.p50; g_best = k; }
    }
    if (g_bestT) out("  mejor forma en el ARM: %s (%u us en total; el PSP tarda %u us)\n", VAR[g_best].nom, g_bestT, g_pspT);
  }
  csvFlush();
}

static void pCache(void) {
  csvOpen("cache", "operacion,us_por_200,ns_por_op\n");
  out("costo de la cache del PSP (200 repeticiones cada una):\n");
  for (int op = 0; op < 5; op++) {
    static const char *nom[5] = {"WritebackInvalidateAll limpia", "WritebackInvalidateAll sucia 32KB",
                                 "WritebackRange 1KB sucio", "WritebackRange 16KB sucio", "InvalidateRange 64KB"};
    sceKernelDcacheWritebackInvalidateAll();
    u32 tot = 0;
    for (int i = 0; i < 200; i++) {
      if (op == 1) memset(g_buf, i, 32768);
      if (op == 2 || op == 3) memset(g_buf, i, op == 2 ? 1024 : 16384);
      u32 t = NOW();
      switch (op) {
      case 0: case 1: sceKernelDcacheWritebackInvalidateAll(); break;
      case 2: sceKernelDcacheWritebackRange(g_buf, 1024); break;
      case 3: sceKernelDcacheWritebackRange(g_buf, 16384); break;
      case 4: sceKernelDcacheInvalidateRange(g_buf, 65536); break;
      }
      tot += NOW() - t;
    }
    csv("%s,%u,%u\n", nom[op], tot, tot * 1000 / 200);
    out("  %s: %u ns\n", nom[op], tot * 1000 / 200);
  }
  csvFlush();
}

// Modelo asíncrono con el GE de verdad: el juego encola con el stall al principio, el ARM recorre,
// y un hilo del PSP sube el stall cuando llega la respuesta. Mientras tanto el hilo del "juego" trabaja.
static volatile u32 g_work;
static void work(u32 units) { u32 x = g_work; for (u32 i = 0; i < units * 1000; i++) x = x * 1664525u + 1013904223u; g_work = x; }

static SceUID g_semGo, g_semFin;
static volatile int g_geId, g_asyncQuit, g_job;  // g_job: 1 = al responder el ARM, subir el stall
static volatile u32 g_tResp, g_tStall, g_geEnd;
static Smp g_asyncSmp;
static u32 g_walkArgs[6];

static int asyncThread(SceSize args, void *argp) {
  while (!g_asyncQuit) {
    if (sceKernelWaitSema(g_semGo, 1, NULL) < 0 || g_asyncQuit) break;
    kreq(1, KB_WALK, g_walkArgs, 6, &g_asyncSmp);
    g_tResp = NOW();
    if (g_job) sceGeListUpdateStallAddr(g_geId, (void *)g_geEnd);
    g_tStall = NOW();
    sceKernelSignalSema(g_semFin, 1);
  }
  return sceKernelExitDeleteThread(0);
}

static void pAsincrono(void) {
  useBest();
  int armOk = kermitOk("asincrono");
  if (!g_list) return;
  int mejor = g_tr;
  out("modelo asincrono con el GE (10 repeticiones por escenario; el ARM usa la forma \"%s\"):\n", VAR[g_best].nom);
  // Lista del GE: 20000 NOP + FINISH + END (no dibuja nada)
  const u32 NG = 20000;
  u32 *ge = (u32 *)(g_buf + BUF_SZ - KB_GEN_OUT - (NG + 8) * 4);
  ge = (u32 *)((u32)ge & ~63);
  u32 *gu = UNC(ge);
  for (u32 i = 0; i < NG; i++) gu[i] = 0;
  gu[NG] = 0x0F000000; gu[NG + 1] = 0x0C000000;
  if ((u8 *)ge < g_buf + 0x60000) { out("  no hay espacio para la lista del GE\n"); return; }
  sceKernelDcacheWritebackInvalidateAll();
  void *gl = (void *)PA(ge);
  g_geEnd = PA(ge) + (NG + 2) * 4;
  // Trabajo del "juego": unidades para ~8 ms
  u32 t = NOW(); work(100); u32 dt = NOW() - t, W = dt ? 8000 * 100 / dt : 1000;
  out("  trabajo del juego: %u unidades = ~8 ms (100 unidades = %u us)\n", W, dt);
  u8 *outA = g_buf + BUF_SZ - KB_GEN_OUT;
  g_walkArgs[0] = PA(g_buf); g_walkArgs[1] = BUF_SZ; g_walkArgs[2] = g_list;
  g_walkArgs[3] = PA(outA); g_walkArgs[4] = KB_GEN_OUT; g_walkArgs[5] = VAR[g_best].walk;  // la forma más rápida medida en pRecorrido
  u32 outW = (KB_GEN_OUT - sizeof(KbWalkStats)) / 4;
  KbWalkStats st;

  g_semGo = sceKernelCreateSema("kb_go", 0, 0, 1, NULL);
  g_semFin = sceKernelCreateSema("kb_fin", 0, 0, 1, NULL);
  g_asyncQuit = 0;
  SceUID th = sceKernelCreateThread("kb_async", asyncThread, 0x18, 0x2000, 0, NULL);
  sceKernelStartThread(th, 0, NULL);

  csvOpen("asincrono", "escenario,rep,total_us,listo_para_ge_us,trabajo_fin_us,ge_fin_us,ok\n");
  static const char *nom[6] = {"solo_trabajo", "solo_ge", "psp_sincrono", "arm_sincrono", "arm_asincrono", "arm_asinc_mem"};
  for (int e = 0; e < 6; e++) {
    // 3 y 4 por el mejor transporte; 5 por memoria compartida (solo si el mejor era Kermit).
    // En los asíncronos por memoria compartida el hilo que espera duerme entre sondeos: si sondeara
    // sin parar, con más prioridad que el juego, le quitaría la CPU que se quiere liberar.
    if (e == 5 && mejor) break;
    g_tr = e == 5 ? 1 : mejor;
    if (e >= 3 && (g_dead || !armOk)) continue;
    g_shmSleep = (e >= 4 && g_tr) ? 50 : 0;
    if (g_tr) shmPoll(0);
    u32 tot[10], listo[10], n = 0;
    for (int r = 0; r < 10 && !(e >= 3 && g_dead); r++) {
      u32 t0 = NOW(), tl = 0, tw = 0, tg = 0, ok = 1;
      int id;
      switch (e) {
      case 0: work(W); tw = NOW(); break;
      case 1: id = sceGeListEnQueue(gl, NULL, -1, NULL); tl = NOW(); sceGeListSync(id, 0); tg = NOW(); ok = id >= 0; break;
      case 2:  // hoy: el hilo del juego recorre antes de encolar
        kbWalk(KSEG(g_buf), PA(g_buf), BUF_SZ, g_list, g_outP, outW, 1000000, &st);
        id = sceGeListEnQueue(gl, NULL, -1, NULL); tl = NOW();
        work(W); tw = NOW(); sceGeListSync(id, 0); tg = NOW(); ok = id >= 0 && st.sum == g_ref.sum; break;
      case 3: {  // el hilo del juego espera al ARM (por el hilo trabajador, con tiempo límite)
        sceKernelDcacheWritebackInvalidateAll();
        g_job = 0;
        sceKernelSignalSema(g_semGo, 1);
        SceUInt to = 3000000;
        if (sceKernelWaitSema(g_semFin, 1, &to) < 0) {
          volatile KbMbox *m = mbox();
          SET_DEAD(); g_fallas++;
          out("CUELGA en arm_sincrono: el ARM recibio %u y respondio %u\n", m->seen, m->answered);
          ok = 0; break;
        }
        id = sceGeListEnQueue(gl, NULL, -1, NULL); tl = NOW();
        work(W); tw = NOW(); sceGeListSync(id, 0); tg = NOW();
        ok = id >= 0 && g_asyncSmp.ret >= 0 && g_asyncSmp.res == g_ref.sum; break;
      }
      case 4: case 5: {  // el juego sigue; el ARM recorre en paralelo
        sceKernelDcacheWritebackInvalidateAll();
        id = sceGeListEnQueue(gl, gl, -1, NULL);  // stall al principio: el GE espera
        if (id < 0) { ok = 0; break; }
        g_geId = id; g_job = 1;
        sceKernelSignalSema(g_semGo, 1);
        work(W); tw = NOW();
        SceUInt to = 3000000;
        if (sceKernelWaitSema(g_semFin, 1, &to) < 0) {  // el ARM no contestó: soltar el GE y parar
          sceGeListUpdateStallAddr(id, (void *)g_geEnd);
          sceGeListSync(id, 0);
          volatile KbMbox *m = mbox();
          SET_DEAD(); g_fallas++;
          out("CUELGA en asincrono: el ARM recibio %u y respondio %u\n", m->seen, m->answered);
          ok = 0; break;
        }
        tl = g_tStall;
        sceGeListSync(id, 0); tg = NOW();
        ok = g_asyncSmp.ret >= 0 && g_asyncSmp.res == g_ref.sum;
        break;
      }
      }
      u32 fin = tg ? tg : tw;
      tot[n] = fin - t0; listo[n] = tl ? tl - t0 : 0; n++;
      csv("%s,%d,%u,%u,%u,%u,%u\n", nom[e], r, fin - t0, tl ? tl - t0 : 0, tw ? tw - t0 : 0, tg ? tg - t0 : 0, ok);
      if (!ok) g_fallas++;
    }
    St s1, s2; stats(tot, n, &s1); stats(listo, n, &s2);
    out("  %-13s (%s) total p50=%u us, GE con la lista lista a los p50=%u us\n", nom[e], e >= 3 ? TR : "-", s1.p50, s2.p50);
  }
  g_shmSleep = 0; g_tr = mejor;
  g_asyncQuit = 1;
  if (!g_dead) { sceKernelSignalSema(g_semGo, 1); sceKernelDelayThread(10000); }
  csvFlush();
}

// Dos hilos del PSP pidiendo a la vez: ¿Kermit encola o se mezclan las respuestas?
static void pConcurrencia(void) {
  g_tr = 0;
  if (g_kdead) { out("concurrencia: omitida (solo aplica a Kermit)\n"); return; }
  if (!kermitOk("concurrencia")) return;
  csvOpen("concurrencia", CAB_PING);
  Batch b1 = {0}, b2 = {0};
  b1.slot = 2; b2.slot = 3; b1.n = b2.n = 200; b1.cmd = b2.cmd = KB_PING; b1.s = g_smp; b2.s = g_smp + 1024;
  int e0 = g_echoErr;
  startBatch(&b1, 0x18); startBatch(&b2, 0x18);
  Batch *bs[2] = {&b1, &b2};
  if (waitBatches(bs, 2, "concurrencia") < 0) { csvFlush(); return; }
  for (int k = 0; k < 2; k++) {
    int ok = joinCsv(k ? "conc_b" : "conc_a", bs[k]->s, 200, g_log, 0, g_v, NULL, k, 0);
    St s; stats(g_v, ok, &s);
    out("concurrencia hilo %d: %d/200 bien\n", k, ok); outSt("ida y vuelta", &s);
  }
  out("  respuestas cruzadas o malas: %d\n", g_echoErr - e0);
  if (g_echoErr != e0) g_fallas++;
  csvFlush();
}

// Vista directa del ARM (0x68000000 + dir): sin ConvertAddress. Puede cerrar Adrenaline si la
// dirección no es la de Adrenaline 8; por eso va al final, con todo lo anterior ya guardado.
static void pDirecta(void) {
  useBest();
  if (!kermitOk("vista directa")) return;
  out("vista directa (experimental):\n");
  fillPat((u32 *)g_buf, 4096, 77);
  u32 esp = kbSum((u32 *)g_buf, 4096);
  sceKernelDcacheWritebackInvalidateAll();
  u32 a[6] = {PA(g_buf), 16384, KB_F_DIRECT};
  Smp s;
  for (int r = 0; r < 3 && !g_dead; r++) {
    kreq(0, KB_READ, a, 3, &s);
    out("  lectura 16 KB: ret=%d res=%08x (esperado %08x) rtt=%u us\n", s.ret, s.res, esp, s.t3 - s.t0);
  }
  if (g_list) {
    u8 *outA = g_buf + BUF_SZ - KB_GEN_OUT;
    kbGen(KSEG(g_buf), PA(g_buf), BUF_SZ, 55000, 1234);
    sceKernelDcacheWritebackInvalidateAll();
    u32 w[6] = {PA(g_buf), BUF_SZ, g_list, PA(outA), KB_GEN_OUT, KB_F_DIRECT};
    for (int r = 0; r < 3 && !g_dead; r++) {
      kreq(0, KB_WALK, w, 6, &s);
      out("  recorrido: ret=%d suma %08x (esperado %08x) rtt=%u us\n", s.ret, s.res, g_ref.sum, s.t3 - s.t0);
    }
  }
}

static int benchThread(SceSize args, void *argp) {
  int flags = *(int *)argp;
  sceIoMkdir(g_dir, 0777);
  fileAppend(g_resumen, "", 0, 1);
  g_buf = kalloc(2, BUF_SZ, "kb_buf");
  g_outP = kalloc(2, KB_GEN_OUT, "kb_out");
  g_log = kalloc(2, 4096 * sizeof(KbEnt), "kb_log");
  g_smp = kalloc(2, NSMP * sizeof(Smp), "kb_smp");
  g_v = kalloc(2, NSMP * 4, "kb_v"); g_v2 = kalloc(2, NSMP * 4, "kb_v2");
  if (!g_buf || !g_outP || !g_log || !g_smp || !g_v || !g_v2) { out("sin memoria: se cancela\n"); g_state = 2; return sceKernelExitDeleteThread(0); }

  pInfo();
  pPing();
  pAfinidad();
  pLectura();
  pEscritura();
  pVistas();
  pRecorrido();
  pCache();
  pAsincrono();
  pConcurrencia();
  if (flags & 1) pDirecta();
  out("== terminado: %d fallas, memoria compartida %s, Kermit %s, respuestas malas %d ==\n", g_fallas,
      g_sdead ? "NO disponible" : "bien", g_kdead ? "NO disponible (o apagado)" : "bien", g_echoErr);
  if (g_kdead) disconnectArm();  // con Kermit vivo el ARM tiene un hilo esperando: se deja el buzón para que siga en orden
  g_state = 2;
  return sceKernelExitDeleteThread(0);
}

// ---------- modo juego ----------
static int juegoThread(SceSize args, void *argp) {
  sceKernelDelayThread(45 * 1000000);
  connectArm();
  kbsprintf(g_resumen, sizeof g_resumen, "%s/resumen_juego.txt", g_dir);
  sceIoMkdir(g_dir, 0777);
  fileAppend(g_resumen, "", 0, 1);
  out("== KBENCH %d en juego (modo Kermit %d) ==\n", KB_VERSION, KB_MODE);
  out("programa: %s\n", g_prog);
  out("PSP: cpu=%d MHz bus=%d MHz\n", scePowerGetCpuClockFrequencyInt(), scePowerGetBusClockFrequencyInt());
  out("%s\n", g_status);
  const int N = 400;
  g_log = kalloc(1, N * sizeof(KbEnt), "kb_log");
  g_smp = kalloc(1, N * sizeof(Smp), "kb_smp");
  u32 *rtt = kalloc(1, N * 9 * 4, "kb_rtt"), *svc = kalloc(1, N * 9 * 4, "kb_svc");
  u8 *rb = kalloc(1, 16384, "kb_rd");
  if (!g_log || !g_smp || !rtt || !svc || !rb) { out("sin memoria en la particion del kernel\n"); return sceKernelExitDeleteThread(0); }
  g_tr = 1;
  if (!kermitOk("juego")) return sceKernelExitDeleteThread(0);
  Smp s; kreq(0, KB_HELLO, NULL, 0, &s);
  if (s.ret < 0 || s.res != KB_VERSION) { out("el saludo fallo: no se usa el ARM\n"); return sceKernelExitDeleteThread(0); }
  // Kermit (modo 14) lo atiende el hilo de PS1 de Adrenaline: no se usa (ver kbench.md)
  csvOpen("juego", CAB_PING);
  // Pings al ritmo de un frame: Kermit (9 lotes de 400 = 1 min) y memoria compartida (sondeo cada 1 ms y continuo)
  for (int t = 0; t < 3; t++) {
    g_tr = t ? 1 : 0;
    if (g_dead) continue;
    if (t) shmPoll(t == 1 ? 1000 : 0);
    int lotes = t ? 2 : 9, nr = 0, ns = 0;
    char nm[32]; kbsprintf(nm, sizeof nm, "%s_16ms_p%u", TR, t == 1 ? 1000 : 0);
    for (int l = 0; l < lotes && !g_dead; l++) {
      Batch b = {0}; b.slot = 0; b.n = N; b.cmd = KB_PING; b.spacing = 16667; b.s = g_smp;
      fetchLog(g_log, 1);
      if (runBatch(&b, nm) < 0) break;
      int ne = fetchLog(g_log, N);
      nr += joinCsv(nm, g_smp, N, g_log, ne, rtt + nr, svc + ns, l, 0);
      ns += ne < N ? ne : N;
    }
    St st; out("%s con el juego corriendo:\n", nm);
    stats(rtt, nr, &st); outSt("ida y vuelta (PSP)", &st);
    stats(svc, ns, &st); outSt("servicio en el ARM", &st);
  }
  if (g_tr) shmPoll(1000);
  // Lecturas de 16 KB (como una lista de tamaño medio) por el mejor transporte
  g_tr = g_kdead ? 1 : 0;
  fillPat((u32 *)rb, 4096, 99);
  u32 esp = kbSum((u32 *)rb, 4096), ok = 0, n = 0;
  sceKernelDcacheWritebackRange(rb, 16384);
  for (int i = 0; i < 30 && !g_dead; i++) {
    sceKernelDelayThread(100000);
    Batch b = {0}; b.n = 1; b.cmd = KB_READ; b.na = 3; b.a[0] = PA(rb); b.a[1] = 16384; b.a[2] = KB_F_INVAL; b.s = &g_smp[i];
    if (runBatch(&b, "lectura en juego") < 0) break;
    rtt[n++] = g_smp[i].t3 - g_smp[i].t0; ok += g_smp[i].res == esp;
    csv("%s_lectura16k,%d,%u,%d,%u,%u,%u,0,0,0,0,0,%u,0,0\n", TR, i, g_smp[i].seq, g_smp[i].ret, g_smp[i].t0, g_smp[i].t3, rtt[n - 1], g_smp[i].res);
  }
  out("lecturas de 16 KB (%s): correctas %u/%u\n", TR, ok, n);
  St st; stats(rtt, n, &st); outSt("ida y vuelta (PSP)", &st);
  out("== terminado: %d fallas, Kermit %s ==\n", g_fallas, g_kdead ? "no disponible" : "bien");
  if (g_kdead) disconnectArm();
  csvFlush();
  return sceKernelExitDeleteThread(0);
}

// ---------- exportadas al EBOOT ----------
static int okUser(const void *p, int len) { return p && len > 0 && !((u32)p & 0x80000000) && PA(p) + len <= 0x0C000000; }

int kbVersion(void) { return KB_VERSION; }

int kbProbe(char *buf, int len) {
  int k1 = pspSdkSetK1(0);
  const char *e = armCheck();
  if (okUser(buf, len)) kbsprintf(buf, len, "%s", e ? e : "ARM listo");
  pspSdkSetK1(k1);
  return e ? 0 : 1;
}

int kbStart(int flags) {
  int k1 = pspSdkSetK1(0), r = -1;
  static int fl;
  if (g_state == 0) {
    fl = flags;
    SceUID th = sceKernelCreateThread("kb_bench", benchThread, 0x20, 0x8000, 0, NULL);
    if (th >= 0) { g_state = 1; sceKernelStartThread(th, sizeof fl, &fl); r = 0; }
  }
  pspSdkSetK1(k1);
  return r;
}

int kbStatus(char *buf, int len) {
  int k1 = pspSdkSetK1(0);
  if (okUser(buf, len)) kbsprintf(buf, len, "%s", g_status);
  pspSdkSetK1(k1);
  return g_state;
}

int module_start(SceSize args, void *argp) {
  for (int i = 0; i < 4; i++) g_seqn[i] = NOW() & 0x0FFFFF00;  // timbres distintos a los de una sesión anterior
  static const char *mods[3] = {"sceKermit_Driver", "sceKermit", "sceKermit_driver"};
  for (int i = 0; i < 3 && !kermitSend; i++) kermitSend = (void *)sctrlHENFindFunction(mods[i], "sceKermit_driver", KB_KERMIT_NID);
  const char *fn = sceKernelInitFileName();
  if (fn) kbsprintf(g_prog, sizeof g_prog, "%s", fn);
  g_eboot = cistr(fn, "KBENCH");
  if (!g_eboot) {  // modo juego solo a pedido: con GePatch activo, 0x0A000000 es su VRAM falsa
    SceIoStat st;
    if (sceIoGetstat("ms0:/kbench/juego.txt", &st) < 0) return 0;
    SceUID th = sceKernelCreateThread("kb_juego", juegoThread, 0x30, 0x4000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
  }
  return 0;
}

int module_stop(SceSize args, void *argp) { return 0; }
