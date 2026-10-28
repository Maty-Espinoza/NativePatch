// captura.c: capturador de la etapa 2 de la arquitectura B (gepresent v12).
// GePatch en modo sombra (dev61) anota en un buzon en la RAM del PSP (0x0A000000, ver "Modo sombra" en main.c) cada
// lista que el juego encola, cada stall, cada sync y cada flip. Este hilo sigue ese buzon y recorre las mismas listas
// leyendo la RAM del PSP, como lo haria el GE, sin dibujar nada: cuenta lo que habria que traducir a GXM (draws,
// cambios reales de estado, lotes que se podrian juntar, texturas) y cuanto tarda el ARM. Publica en el buzon la ultima
// lista capturada (la usa la compuerta del sync de GePatch) y un latido.
// Estadisticas cada segundo en ux0:pspemu/ge_patch_captura.log (la sesion anterior: ge_patch_captura_prev.log).
// Lectura de la lista: ver "Lectura de la RAM del PSP" (v12.9: de a 256 bytes + dos lectores adelantados).
// Lo que esta en la VRAM del PSP no se puede leer (no esta mapeada en Adrenaline): se cuenta y se salta (espejo:
// siguiente paso de la etapa 2).
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdarg.h>

#define PSP(a) (0x68000000u + ((a) & 0x0FFFFFFFu))
#define SB ((volatile uint32_t *)PSP(0x0A000000u))
#define SB_MAGIC 0x424D4F53u
#define SB_N 4096
#define LOGC "ux0:pspemu/ge_patch_captura.log"
enum { EV_ENQ = 1, EV_HEAD, EV_STALL, EV_DSYNC, EV_LSYNC, EV_FLIP };
static inline uint32_t now(void) { return sceKernelGetProcessTimeLow(); }

// --- Lectura de la RAM del PSP ---
// Cada lectura de la RAM del PSP desde el ARM espera mucho (vista sin cache): casi todo el costo del capturador es esa
// espera, no interpretar. Historia: DMA en ventanas de 2 KB ~200 us por copia (v12.2); palabra a palabra ~1,5 us por
// comando (v12.3); de a 64 bytes con NEON ~350 ns (v12.4). v12.9:
// 1. Se traen 256 bytes de una vez (8 cargas NEON seguidas: las esperas se solapan).
// 2. Dos lectores (nucleos 0 y 2, prioridad 0xB0: solo el tiempo que les sobra a esos nucleos) recorren la misma lista
//    por adelantado, repartiendose las sublistas, y dejan las lineas de 64 bytes en una cache compartida; el capturador
//    (nucleo 1) las toma de ahi. Cada linea va marcada con la lista (numero de orden): no se usa de una lista a otra.
//    Lo que esta despues del stall (todavia sin escribir) no se comparte. ux0:data/gepresent/captura_sin_lectores: sin ellos.
// La RAM del PSP puede estar en varios bloques de la Vita (v12.2): hasta 4 tramos legibles.
static uint32_t rlo[4], rhi[4], nr;
static inline uint32_t ramEnd(uint32_t a) {  // fin del tramo legible que contiene a (0: no legible)
  for (uint32_t i = 0; i < nr; i++) if (a >= rlo[i] && a + 4 <= rhi[i]) return rhi[i];
  return 0;
}
static inline int okRam(uint32_t a) { return ramEnd(a) != 0; }
static inline int isVram(uint32_t a) { return (a & 0x0F000000u) == 0x04000000u; }

typedef struct { uint32_t buf[64] __attribute__((aligned(16))); uint32_t a, n, own, hits; } Rd;  // ultimos bytes leidos
static Rd R0, RR[2];  // capturador y lectores
#define CL_N 8192     // lineas de 64 bytes en la cache compartida (512 KB)
#define CL_BUSY 0xFFFFFFFEu
static uint32_t *cl_data;
static volatile uint32_t *cl_tag;
static volatile uint32_t pub_ord, pub_pc, pub_base, pub_stall;  // la lista que recorre el capturador
static inline uint32_t clTag(uint32_t b, uint32_t ord) { return ((b - 0x08000000u) >> 6) << 12 | (ord & 0xFFF); }
static inline uint32_t clSlot(uint32_t b) { uint32_t i = (b - 0x08000000u) >> 6; return (i ^ (i >> 13)) & (CL_N - 1); }
static inline int clOk(uint32_t b) {  // linea completa: antes del stall o lejos de el
  uint32_t st = pub_stall;
  return cl_data && b >= 0x08000000u && (!st || b + 64 <= st || b >= st + 0x40000);
}
static int clGet(uint32_t b, uint32_t ord, uint32_t *dst) {
  if (!clOk(b)) return 0;
  uint32_t i = clSlot(b), t = clTag(b, ord);
  if (cl_tag[i] != t) return 0;
  __sync_synchronize();
  for (int k = 0; k < 16; k++) dst[k] = cl_data[i * 16 + k];
  __sync_synchronize();
  return cl_tag[i] == t;  // si otro hilo la reemplazo mientras se copiaba, no vale
}
static void clPut(uint32_t b, uint32_t ord, const uint32_t *src) {
  if (!clOk(b)) return;
  uint32_t i = clSlot(b), old = cl_tag[i];
  if (old == CL_BUSY || !__sync_bool_compare_and_swap(&cl_tag[i], old, CL_BUSY)) return;  // otro hilo la esta escribiendo
  for (int k = 0; k < 16; k++) cl_data[i * 16 + k] = src[k];
  __sync_synchronize();
  cl_tag[i] = clTag(b, ord);
}
static void neon64(uint32_t *dst, uint32_t b) {
  const void *src = (const void *)PSP(b);
  asm volatile("vld1.32 {d16-d19}, [%1]!\n\tvld1.32 {d20-d23}, [%1]\n\tvst1.32 {d16-d19}, [%0]!\n\tvst1.32 {d20-d23}, [%0]"
               : "+r"(dst), "+r"(src) : : "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "memory");
}
static void neon256(uint32_t *dst, uint32_t b) {  // las 8 cargas antes de guardar nada
  const void *src = (const void *)PSP(b);
  asm volatile("vld1.32 {d0-d3}, [%1]!\n\tvld1.32 {d4-d7}, [%1]!\n\tvld1.32 {d8-d11}, [%1]!\n\tvld1.32 {d12-d15}, [%1]!\n\t"
               "vld1.32 {d16-d19}, [%1]!\n\tvld1.32 {d20-d23}, [%1]!\n\tvld1.32 {d24-d27}, [%1]!\n\tvld1.32 {d28-d31}, [%1]\n\t"
               "vst1.32 {d0-d3}, [%0]!\n\tvst1.32 {d4-d7}, [%0]!\n\tvst1.32 {d8-d11}, [%0]!\n\tvst1.32 {d12-d15}, [%0]!\n\t"
               "vst1.32 {d16-d19}, [%0]!\n\tvst1.32 {d20-d23}, [%0]!\n\tvst1.32 {d24-d27}, [%0]!\n\tvst1.32 {d28-d31}, [%0]"
               : "+r"(dst), "+r"(src) : : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "d10", "d11", "d12",
                 "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23", "d24", "d25", "d26", "d27",
                 "d28", "d29", "d30", "d31", "memory");
}
static uint32_t rdc(Rd *r, uint32_t a, uint32_t ord) {
  if (a - r->a < r->n) return r->buf[(a - r->a) >> 2];
  uint32_t b = a & ~63u, e = ramEnd(a);
  if (b + 64 > e) return *(volatile uint32_t *)PSP(a);  // (final de un tramo)
  if (clGet(b, ord, r->buf)) { r->a = b; r->n = 64; r->hits++; return r->buf[(a - b) >> 2]; }
  uint32_t n = b + 256 <= e ? 256 : 64;
  if (n == 256) neon256(r->buf, b); else neon64(r->buf, b);
  r->a = b; r->n = n; r->own++;
  for (uint32_t k = 0; k < n; k += 64) clPut(b + k, ord, r->buf + k / 4);
  return r->buf[(a - b) >> 2];
}

// --- Estadisticas (por segundo) ---
typedef struct {
  uint32_t t0, frames, lists, cmds, cmds_max, draws, draws_max, us, us_max, own, hits, rown[2];
  uint32_t d3d, dthr, dclear, dempty, prim[8], curves, verts;
  uint32_t ch_tex, ch_mtx, ch_rt, ch_other, ch_none, texnew, batches, batches_cont, rt_switch;
  uint32_t textures, tex_vram, dtex_vram, dvert_vram, vbytes, ibytes, cluts, clut_vram, transfers, tr_vram;
  uint32_t vcalls, vjumps, lost, bad, signals, bjumps, events, stalls, syncs, syncs_waited, sync_wait_us, behind, resyncs;
  uint32_t lsyncs, flips_behind, aborted, yields;
  uint32_t chg[256];  // cambios reales de cada registro "otro" (que impide juntar draws)
} Stats;
static Stats acc, rep;
static volatile int rep_ready;
static uint32_t f_cmds, f_draws, f_us;  // del frame en curso

// --- Estado del GE (sombra) ---
static uint32_t reg[256], mtx[5][96], mi[5];
static uint8_t cls[256];  // 0 no es estado, 1 textura, 2 matriz, 3 destino (rt), 4 otro
enum { C_TEX = 1, C_MTX = 2, C_RT = 4, C_OTHER = 8 };
static uint32_t dirty, vaddr, iaddr, vset, last_prim = 99, last_vend, last_fb;
static uint32_t tset[2048], tgen[2048], gen, ntex, last_clut[2];
static void initCls(void) {
  for (int c = 0x15; c < 256; c++) cls[c] = C_OTHER;
  for (int c = 0xA0; c <= 0xB1; c++) cls[c] = C_TEX;
  for (int c = 0xB8; c <= 0xBF; c++) cls[c] = C_TEX;
  cls[0xC2] = cls[0xC3] = cls[0xC5] = C_TEX;
  cls[0x9C] = cls[0x9D] = cls[0x9E] = cls[0x9F] = cls[0xD2] = C_RT;
  cls[0x12] = C_OTHER;  // tipo de vertice
  static const uint8_t no[] = { 0x2A, 0x2B, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0xB2, 0xB3, 0xB4, 0xB5, 0xC4,
                                0xCB, 0xCC, 0xEA, 0xEB, 0xEC, 0xEE };
  for (unsigned i = 0; i < sizeof no; i++) cls[no[i]] = 0;
}
static int texAdd(uint32_t a) {  // 1 si es una textura nueva en este frame
  if (ntex >= 1500) return 0;
  uint32_t i = ((a >> 4) * 2654435761u) >> 21;
  while (tgen[i] == gen) { if (tset[i] == a) return 0; i = (i + 1) & 2047; }
  tgen[i] = gen; tset[i] = a; ntex++;
  return 1;
}
static uint32_t vsize(uint32_t vt) {  // bytes por vertice (reglas del GE, como PPSSPP)
  static const uint8_t s4[4] = { 0, 1, 2, 4 }, col[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
  uint32_t sz = 0, al = 1, s;
#define ADD(n, size) s = (size); if (s) { sz = (sz + s - 1) & ~(s - 1); sz += (n) * s; if (s > al) al = s; }
  ADD(((vt >> 14) & 7) + 1, s4[(vt >> 9) & 3]);  // pesos
  ADD(2, s4[vt & 3]);                             // coordenadas de textura
  ADD(1, col[(vt >> 2) & 7]);                     // color
  ADD(3, s4[(vt >> 5) & 3]);                      // normal
  ADD(3, s4[(vt >> 7) & 3]);                      // posicion
#undef ADD
  return ((sz + al - 1) & ~(al - 1)) * (((vt >> 18) & 7) + 1);
}
#define TEXADDR() ((reg[0xA0] & 0xFFFFF0) | ((reg[0xA8] << 8) & 0x0F000000))
#define CLUTADDR() ((reg[0xB0] & 0xFFFFF0) | ((reg[0xB1] << 8) & 0x0F000000))

static void draw(uint32_t d) {
  uint32_t n = d & 0xFFFF, type = (d >> 16) & 7, vt = reg[0x12], idx = (vt >> 11) & 3;
  if (!n) { acc.dempty++; return; }
  acc.draws++; f_draws++; acc.prim[type]++; acc.verts += n;
  int clear = reg[0xD3] & 1, tex = (reg[0x1E] & 1) && !clear;
  if (clear) acc.dclear++;
  else if (vt >> 23 & 1) acc.dthr++;
  else acc.d3d++;
  uint32_t fb = (reg[0x9C] & 0xFFFFF0) | ((reg[0x9D] << 8) & 0xFF000000);
  if (fb != last_fb) { acc.rt_switch++; last_fb = fb; }
  if (isVram(vaddr)) acc.dvert_vram++;
  uint32_t vs = vsize(vt), vstart = vaddr;
  if (idx) { acc.ibytes += n * (idx == 1 ? 1 : idx == 2 ? 2 : 4); iaddr += n * (idx == 1 ? 1 : idx == 2 ? 2 : 4); }
  else { acc.vbytes += n * vs; vaddr += n * vs; }
  if (tex) {
    uint32_t ta = TEXADDR();
    if (texAdd(ta)) { acc.textures++; if (isVram(ta)) acc.tex_vram++; }
    if (isVram(ta)) acc.dtex_vram++;
    if (dirty & C_TEX) acc.texnew++;
  }
  if (dirty & C_TEX) acc.ch_tex++;
  if (dirty & C_MTX) acc.ch_mtx++;
  if (dirty & C_RT) acc.ch_rt++;
  if (dirty & C_OTHER) acc.ch_other++;
  if (!dirty) acc.ch_none++;
  // Lote: draws seguidos que se podrian dibujar como uno (mismo estado y mismo tipo de primitiva). "Contiguo": ademas
  // los vertices siguen donde termino el anterior (sin indices nuevos)
  int same = !dirty && type == last_prim;
  if (!same) acc.batches++;
  if (!same || idx || (vset && vstart != last_vend)) acc.batches_cont++;
  last_prim = type; last_vend = vaddr; dirty = vset = 0;
}

// --- Listas en cola (como el GE: una a la vez, en orden) ---
typedef struct { uint32_t pc, stall, qid, ord, off, sp, stk[32][2], prev, n; } List;
#define LIST_MAX 400000  // comandos: mas que eso es una lista que el juego ya reescribio (la Vita iba atrasada)
static List q[64];
static uint32_t qh, qt;  // la primera sin terminar y la siguiente libre
static inline uint32_t rel(List *l, uint32_t d) { return ((((reg[0x10] & 0x0F0000) << 8) | d) + l->off) & 0x0FFFFFFF; }

// Recorre la lista hasta su stall o su fin (1: termino), con un tope de comandos por llamada
static int walkList(List *l) {
  for (int budget = 8000; budget--;) {  // ~3 ms: despues se vuelve al bucle (eventos y pausa)
    if (l->stall && l->pc == l->stall) return 0;
    if (!okRam(l->pc)) { if (isVram(l->pc)) acc.lost++; else acc.bad++; return 1; }
    uint32_t pc = l->pc, op = rdc(&R0, pc, l->ord), c = op >> 24, d = op & 0xFFFFFF, prev = l->prev;
    if (++l->n > LIST_MAX) { acc.aborted++; return 1; }
    l->pc += 4; l->prev = op; acc.cmds++; f_cmds++;
    switch (c) {
    case 0x00: break;
    case 0x01: vaddr = rel(l, d); vset = 1; break;
    case 0x02: iaddr = rel(l, d); break;
    case 0x04: draw(d); break;
    case 0x05: case 0x06: acc.curves++; acc.draws++; f_draws++; dirty = 0; last_prim = 99; break;
    case 0x08: case 0x0A: {
      uint32_t t = rel(l, d & 0xFFFFFC);
      if (!okRam(t)) {  // en la VRAM (Gran Turismo: sublistas) o invalido
        if (isVram(t)) { if (c == 0x0A) acc.vcalls++; else { acc.vjumps++; acc.lost++; return 1; } }
        else { acc.bad++; return 1; }
        break;
      }
      if (c == 0x0A) {
        if (l->sp >= 32) { acc.bad++; return 1; }
        l->stk[l->sp][0] = l->pc; l->stk[l->sp++][1] = l->off;
      }
      l->pc = t;
      break;
    }
    case 0x09: acc.bjumps++; break;  // salto condicional por bounding box: se supone que no salta
    case 0x0B: if (l->sp) { l->sp--; l->pc = l->stk[l->sp][0]; l->off = l->stk[l->sp][1]; } break;
    case 0x0C:  // END: termina la lista, salvo despues de un SIGNAL (el juego la continua)
      if ((prev >> 24) == 0x0E) { acc.signals++; break; }
      return 1;
    case 0x0E: case 0x0F: break;
    case 0x10: reg[0x10] = d; break;
    case 0x13: l->off = d << 8; break;
    case 0x14: l->off = pc; break;
    case 0x2A: case 0x3A: case 0x3C: case 0x3E: case 0x40: {
      int m = c == 0x2A ? 4 : (c - 0x3A) / 2;
      mi[m] = d & 0x7F;
      break;
    }
    case 0x2B: case 0x3B: case 0x3D: case 0x3F: case 0x41: {
      int m = c == 0x2B ? 4 : (c - 0x3B) / 2, max = m == 4 ? 96 : m == 2 ? 16 : 12;
      if (mi[m] < (uint32_t)max) { if (mtx[m][mi[m]] != d) { mtx[m][mi[m]] = d; dirty |= C_MTX; } mi[m]++; }
      break;
    }
    case 0xC4: {  // carga de paleta: cambio solo si es otra paleta u otro tamano
      uint32_t ca = CLUTADDR();
      acc.cluts++; if (isVram(ca)) acc.clut_vram++;
      if (ca != last_clut[0] || d != last_clut[1]) { last_clut[0] = ca; last_clut[1] = d; dirty |= C_TEX; }
      break;
    }
    case 0xEA: {
      acc.transfers++;
      uint32_t s = (reg[0xB2] & 0xFFFFF0) | ((reg[0xB3] << 8) & 0x0F000000), t = (reg[0xB4] & 0xFFFFF0) | ((reg[0xB5] << 8) & 0x0F000000);
      if (isVram(s) || isVram(t)) acc.tr_vram++;
      break;
    }
    default:
      if (cls[c] && reg[c] != d) { dirty |= cls[c]; if (cls[c] == C_OTHER) acc.chg[c]++; }
      reg[c] = d;
    }
  }
  return 0;  // tope: se sigue en la proxima vuelta
}

static uint32_t capt;  // numero de orden de la ultima lista capturada
static void walk(void) {
  while (qh != qt) {
    List *l = &q[qh & 63];
    R0.n = 0;  // lo que se leyo despues del stall anterior puede haber cambiado
    if (l->ord != pub_ord) {  // los lectores empiezan esta lista
      pub_stall = l->stall; pub_pc = l->pc; pub_base = reg[0x10];
      __sync_synchronize();
      pub_ord = l->ord;
    }
    if (!walkList(l)) return;
    capt = l->ord; SB[3] = capt; qh++; acc.lists++;
  }
}

static void resetQueue(void) {
  qh = qt = 0;
  capt = SB[6]; SB[3] = capt;  // lo que se perdio se da por capturado
}

static uint32_t tail, beat, sec_t0;
static void event(volatile uint32_t *e) {
  uint32_t t = e[0] & 0xFF, a = e[1], b = e[2], c = e[3];
  acc.events++;
  if (t == EV_ENQ || t == EV_HEAD) {
    if (qt - qh >= 64) { acc.resyncs++; resetQueue(); }
    if (t == EV_HEAD) qh--;
    List *l = &q[(t == EV_HEAD ? qh : qt++) & 63];
    sceClibMemset(l, 0, sizeof *l);
    l->pc = a & 0x0FFFFFFC; l->stall = b & 0x0FFFFFFF; l->qid = c; l->ord = e[5];
  } else if (t == EV_STALL) {
    acc.stalls++;
    for (uint32_t i = qt; i != qh; i--)
      if (q[(i - 1) & 63].qid == a) {
        q[(i - 1) & 63].stall = b & 0x0FFFFFFF;
        if (q[(i - 1) & 63].ord == pub_ord) pub_stall = b & 0x0FFFFFFF;
        break;
      }
  } else if (t == EV_DSYNC) {
    acc.syncs++;
    if (a) { acc.syncs_waited++; acc.sync_wait_us += a & ~1u; }
    if (qh != qt) acc.behind++;
  } else if (t == EV_LSYNC) {
    acc.lsyncs++;
  } else if (t == EV_FLIP) {
    uint32_t t0 = now(), c0 = acc.cmds;  // lo encolado antes del flip se recorre antes de cerrar el frame
    walk();
    if (acc.cmds != c0) { uint32_t dt = now() - t0; acc.us += dt; f_us += dt; }
    acc.frames++;
    if (qh != qt) acc.flips_behind++;
    if (f_cmds > acc.cmds_max) acc.cmds_max = f_cmds;
    if (f_draws > acc.draws_max) acc.draws_max = f_draws;
    if (f_us > acc.us_max) acc.us_max = f_us;
    f_cmds = f_draws = f_us = 0;
    gen++; ntex = 0;
    uint32_t n = now();
    if (n - sec_t0 >= 1000000) {  // cada segundo: a la linea del log
      static uint32_t last[4];  // contadores de lecturas (acumulados) al segundo anterior
      uint32_t cur[4] = { R0.own, R0.hits, RR[0].own, RR[1].own };
      acc.own = cur[0] - last[0]; acc.hits = cur[1] - last[1]; acc.rown[0] = cur[2] - last[2]; acc.rown[1] = cur[3] - last[3];
      for (int k = 0; k < 4; k++) last[k] = cur[k];
      acc.t0 = n - sec_t0;
      if (!rep_ready) { rep = acc; rep_ready = 1; }
      sceClibMemset(&acc, 0, sizeof acc);
      sec_t0 = n;
    }
  }
}

static void clog(const char *fmt, ...);
static int capThread(SceSize args, void *argp) {
  uint32_t idle_t = 0, on = 0;
  for (;;) {
    if (SB[0] != SB_MAGIC) {  // sin GePatch en modo sombra
      if (on) { clog("buzon de GePatch desaparecio: captura en pausa\n"); on = 0; }
      sceKernelDelayThread(50000);
      continue;
    }
    if (!on) {
      on = 1; tail = SB[2]; resetQueue(); sec_t0 = now(); sceClibMemset(&acc, 0, sizeof acc);
      clog("buzon de GePatch (modo sombra) encontrado: eventos=%u listas=%u -> captura activa\n", tail, SB[6]);
    }
    SB[4] = ++beat;
    uint32_t h = SB[2], work = 0;
    if ((int)(h - tail) < 0 || h - tail > SB_N) {  // se perdieron eventos (o GePatch reinicio el buzon)
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
    else if (now() - idle_t > 300) sceKernelDelayThread(100);  // sin trabajo hace 0,3 ms: dormir un poco (~0,4 ms)
  }
  return 0;
}

// --- Log (otro hilo: escribir a la tarjeta tarda) ---
static char lbuf[8192];
static volatile uint32_t lpos;
static void clog(const char *fmt, ...) {
  char b[512];
  va_list ap; va_start(ap, fmt);
  int n = sceClibSnprintf(b, 16, "%7u ", now() / 1000);
  n += sceClibVsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap);
  if (n > (int)sizeof b - 1) n = sizeof b - 1;
  if (lpos + n > sizeof lbuf) return;
  sceClibMemcpy(lbuf + lpos, b, n); lpos += n;
}
#define PF(x) (r->x / f)
static int repThread(SceSize args, void *argp) {
  uint32_t last_write = 0;
  for (;;) {
    sceKernelDelayThread(200000);
    if (rep_ready) {
      Stats *r = &rep;
      uint32_t f = r->frames ? r->frames : 1, d = r->draws ? r->draws : 1;
      clog("seg: frames=%u listas/f=%u comandos/f=%u (max %u) draws/f=%u (max %u) | ARM us/f=%u (max %u) ns/comando=%u | lecturas/f propias=%u de_lectores=%u (lectores leyeron %u+%u) eventos/f=%u\n",
           r->frames, PF(lists), PF(cmds), r->cmds_max, PF(draws), r->draws_max, PF(us), r->us_max, r->cmds ? (uint32_t)((unsigned long long)r->us * 1000 / r->cmds) : 0, PF(own), PF(hits), PF(rown[0]), PF(rown[1]), PF(events));
      clog("  draws/f: 3d=%u 2d=%u clear=%u vacios=%u | tri=%u tira=%u abanico=%u rect=%u lineas=%u puntos=%u curvas=%u | vertices/f=%u\n",
           PF(d3d), PF(dthr), PF(dclear), PF(dempty), PF(prim[3]), PF(prim[4]), PF(prim[5]), PF(prim[6]), PF(prim[1]) + PF(prim[2]),
           PF(prim[0]), PF(curves), PF(verts));
      clog("  estado (draws/f): textura_nueva=%u cambia_textura=%u matriz=%u destino=%u otro=%u sin_cambio=%u (%u%%) | lotes/f=%u contiguos/f=%u cambios_de_destino/f=%u\n",
           PF(texnew), PF(ch_tex), PF(ch_mtx), PF(ch_rt), PF(ch_other), PF(ch_none), r->ch_none * 100 / d, PF(batches), PF(batches_cont),
           PF(rt_switch));
      clog("  memoria/f: texturas=%u (en vram %u) draws_tex_vram=%u draws_vert_vram=%u | vertices=%u KB indices=%u KB | paletas=%u (vram %u) copias=%u (vram %u)\n",
           PF(textures), PF(tex_vram), PF(dtex_vram), PF(dvert_vram), PF(vbytes) >> 10, PF(ibytes) >> 10, PF(cluts), PF(clut_vram),
           PF(transfers), PF(tr_vram));
      clog("  vram y sync (por seg): sublistas_vram=%u saltos_vram=%u listas_perdidas=%u abortadas=%u invalidas=%u signal=%u bjump=%u | drawsync=%u esperaron=%u (%u us c/u) atrasada_al_sync=%u atrasada_al_flip=%u listsync=%u reinicios=%u pausas=%u\n",
           r->vcalls, r->vjumps, r->lost, r->aborted, r->bad, r->signals, r->bjumps, r->syncs, r->syncs_waited,
           r->syncs_waited ? r->sync_wait_us / r->syncs_waited : 0, r->behind, r->flips_behind, r->lsyncs, r->resyncs, r->yields);
      char top[160]; int k = 0;  // los 10 registros "otro" que mas cambian (comando: cambios por frame)
      for (int j = 0; j < 10; j++) {
        int best = -1;
        for (int c = 0; c < 256; c++) if (r->chg[c] && (best < 0 || r->chg[c] > r->chg[best])) best = c;
        if (best < 0) break;
        k += sceClibSnprintf(top + k, sizeof top - k, " %02x:%u", best, r->chg[best] / f);
        r->chg[best] = 0;
      }
      top[k] = 0;
      clog("  cambios \"otro\" mas frecuentes (comando:por frame):%s\n", top);
      rep_ready = 0;
    }
    if (lpos && (now() - last_write > 5000000 || lpos > sizeof lbuf / 2)) {
      SceUID fd = sceIoOpen(LOGC, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
      uint32_t n = lpos;
      if (fd >= 0) { sceIoWrite(fd, lbuf, n); sceIoClose(fd); }
      lpos = 0;  // (lo que llegue mientras se escribe puede perderse: es estadistica)
      last_write = now();
    }
  }
  return 0;
}

// Lector: recorre la lista que publica el capturador solo para traer sus lineas a la cache compartida (sigue saltos,
// sublistas, BASE y OFFSET; no interpreta draws). Las sublistas del nivel de arriba se reparten entre los lectores.
static int lectorThread(SceSize args, void *argp) {
  int id = *(int *)argp;
  Rd *r = &RR[id];
  uint32_t ord = 0, pc = 0, off = 0, base = 0, sp = 0, stk[32][2], prev = 0, ncall = 0, n = 0, done = 1;
  for (;;) {
    if (pub_ord != ord) {  // lista nueva
      ord = pub_ord;
      __sync_synchronize();
      pc = pub_pc; base = pub_base; off = sp = prev = ncall = n = 0; done = !ord; r->n = 0;
    }
    uint32_t st = pub_stall;
    if (done || (st && pc == st)) { sceKernelDelayThread(100); continue; }
    r->n = 0;  // (el stall pudo moverse)
    for (int k = 0; k < 2000 && pub_ord == ord; k++) {
      if (st && pc == st) break;
      if (!okRam(pc) || ++n > LIST_MAX) { done = 1; break; }
      uint32_t at = pc, op = rdc(r, pc, ord), c = op >> 24, d = op & 0xFFFFFF;
      pc += 4;
      if (c == 0x08 || c == 0x0A) {
        uint32_t t = ((((base & 0x0F0000) << 8) | (d & 0xFFFFFC)) + off) & 0x0FFFFFFF;
        if (!okRam(t)) { if (c == 0x08) done = 1; }  // VRAM: no se puede leer
        else if (c == 0x08) pc = t;
        else if (sp < 32 && (sp || ncall++ % 2 == (uint32_t)id)) { stk[sp][0] = pc; stk[sp++][1] = off; pc = t; }
      } else if (c == 0x0B) { if (sp) { sp--; pc = stk[sp][0]; off = stk[sp][1]; } }
      else if (c == 0x0C) { if ((prev >> 24) != 0x0E) done = 1; }
      else if (c == 0x10) base = d;
      else if (c == 0x13) off = d << 8;
      else if (c == 0x14) off = at;
      prev = op;
    }
  }
  return 0;
}

void capturaStart(void) {
  SceIoStat st;
  sceIoRemove("ux0:pspemu/ge_patch_captura_prev.log");
  sceIoRename(LOGC, "ux0:pspemu/ge_patch_captura_prev.log");
  SceUID r = sceKernelCreateThread("gepresent_captura_log", repThread, 0xB0, 0x2000, 0, 0, NULL);  // primero: que
  if (r >= 0) sceKernelStartThread(r, 0, NULL);                                                      // quede el log
  clog("gepresent v12.9: capturador de la etapa 2; lectura de a 256 bytes (NEON) sin cache\n");
  for (uint32_t a = 0x08000000u; a < 0x0C000000u && nr < 4;) {  // tramos de la RAM del PSP mapeados en este proceso
    SceKernelMemBlockInfo in; in.size = sizeof in;
    int e = sceKernelGetMemBlockInfoByAddr((void *)PSP(a), &in);
    uint32_t base = (uint32_t)in.mappedBase, end = base + in.mappedSize;
    if (e < 0 || end <= PSP(a)) { a += 0x100000; continue; }
    uint32_t lo = base > PSP(a) ? base - 0x68000000u : a, hi = end - 0x68000000u;
    if (hi > 0x0C000000u) hi = 0x0C000000u;
    clog("RAM del PSP legible: %08x-%08x (bloque de la Vita %08x, %u KB)\n", lo, hi, base, in.mappedSize >> 10);
    if (nr && rhi[nr - 1] == lo) rhi[nr - 1] = hi;
    else { rlo[nr] = lo; rhi[nr++] = hi; }
    a = hi;
  }
  initCls();
  if (!okRam(0x0A000000u) || !okRam(0x08800000u)) { clog("la RAM del PSP no cubre el buzon o el juego: sin captura\n"); return; }
  SceUID t = sceKernelCreateThread("gepresent_captura", capThread, 0x61, 0x4000, 0, 0x20000 /* nucleo 1 */, NULL);
  // Prioridad (v12.7): en el nucleo 2 el emulador tiene ScePspemuRemoteMe (Media Engine: decodifica el audio del juego),
  // TitleInfo y AvcAuInput con 0x60. Con 0x40 (v12.4/v12.6) el capturador los tapaba y el audio se cortaba; con 0xA0
  // (v12.5) recibia un tercio del nucleo (~1000 ns por comando). 0x61: justo debajo de ellos. (Audio: nucleo 1, 0x40-0x43;
  // ScePspemuMain y los servicios remotos: nucleo 0. Lista completa en ge_patch_gepresent.log de gepresent v12.6)
  // v12.8: nucleo 1. En el 2 (v12.7) el Media Engine ocupaba mas de la mitad del nucleo en carrera (~900 ns por comando);
  // en el 1 solo estan los hilos de salida de audio (0x40-0x43, trabajos cortos), que siguen pasando antes.
  if (t >= 0) sceKernelStartThread(t, 0, NULL);
  clog("hilos: captura %08x (nucleo 1, prioridad 0x61, debajo de los hilos de audio) log %08x\n", t, r);
  if (sceIoGetstat("ux0:data/gepresent/captura_sin_lectores", &st) >= 0) { clog("lectores: apagados (captura_sin_lectores)\n"); return; }
  void *m = NULL;
  SceUID blk = sceKernelAllocMemBlock("gepresent_lineas", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (CL_N * 68 + 0xFFF) & ~0xFFF, NULL);
  if (blk < 0 || sceKernelGetMemBlockBase(blk, &m) < 0) { clog("lectores: sin memoria para la cache (%08x)\n", blk); return; }
  cl_tag = (volatile uint32_t *)((char *)m + CL_N * 64);
  for (int i = 0; i < CL_N; i++) cl_tag[i] = 0xFFFFFFFFu;
  __sync_synchronize();
  cl_data = m;
  static int ids[2] = { 0, 1 };
  static const int cores[2] = { 0x10000, 0x40000 };  // nucleo 0 (emulador) y 2 (Media Engine): solo lo que sobra
  for (int k = 0; k < 2; k++) {
    SceUID lt = sceKernelCreateThread(k ? "gepresent_lector1" : "gepresent_lector0", lectorThread, 0xB0, 0x2000, 0, cores[k], NULL);
    if (lt >= 0) sceKernelStartThread(lt, sizeof(int), &ids[k]);
    clog("lector %d: %08x (nucleo %d, prioridad 0xB0)\n", k, lt, k ? 2 : 0);
  }
}
