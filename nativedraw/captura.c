// captura.c: capturador de la etapa 2 de la arquitectura B (NativeDraw v12).
// NativeLink en modo sombra (dev61) anota en un buzon en la RAM del PSP (0x0A000000, ver "Modo sombra" en main.c) cada
// lista que el juego encola, cada stall, cada sync y cada flip. Este hilo sigue ese buzon y recorre las mismas listas
// leyendo la RAM del PSP, como lo haria el GE, sin dibujar nada: cuenta lo que habria que traducir a GXM (draws,
// cambios reales de estado, lotes que se podrian juntar, texturas) y cuanto tarda el ARM. Publica en el buzon la ultima
// lista capturada (la usa la compuerta del sync de NativeLink) y un latido.
// Estadisticas cada segundo en ux0:pspemu/nativedraw_captura.log (la sesion anterior: nativedraw_captura_prev.log).
// Lectura de la lista: ver "Lectura de la RAM del PSP" (v12.9: de a 256 bytes + dos lectores adelantados).
// Lo que esta en la VRAM del PSP no se puede leer (no esta mapeada en Adrenaline): se cuenta y se salta (espejo:
// siguiente paso de la etapa 2).
// v12.10: tiempo de procesador que de verdad recibe cada hilo (runClocks) y lotes sin cortar por TEXLEVEL (0xC8).
// v12.11 (diagnostico): donde se va el tiempo (cada 5 s se vuelve a recorrer una lista ya leida, desde la cache) y
// cuantas sublistas se repiten de un frame a otro (para la memoria de sublistas).
// v12.13: estadisticas de la memoria corregidas (frames y tiempos se sumaban de mas al aplicar).
// v12.12: memoria de sublistas (ver "memoria de sublistas"); se quitan la segunda pasada y las huellas de v12.11.
// v13.1: espejo de la VRAM (ver "Espejo de la VRAM"): con NativeLink dev62 se recorren las sublistas que estan en la VRAM.
// v13.2: tabla y deposito de la memoria de sublistas 4 veces mas grandes. v13.3: el capturador marca solo las paginas del
// espejo ya copiadas (el hilo del espejo, con poca prioridad, a veces no recibia tiempo y las sublistas se seguian
// saltando); comparacion de lineas con NEON y tiempo de la comparacion en el log.
// v13.4: los lectores comprueban por adelantado las sublistas guardadas (ver memoVerify): el capturador ya no lee ni
// compara sus lineas (en el puente de GT eso era ~9 de los ~18 ms por frame) y los lectores dejan de recorrerlas.
// v13.5: con NativeLink dev63, T6 (cuando trabaja el GE del PSP), T4 (que escribe la CPU del juego: cache y DMA) y la
// "vida de los datos" (ver smpAdd): cuantos vertices, indices, texturas y paletas cambian 1 y 2 frames despues de leerlos.
// v13.6: con NativeLink dev64, espejo doble (ver "Espejo de la VRAM"): cada lista lee una copia que el GE no esta
// sobrescribiendo; los destinos de copias de bloque (TRANSFER) en la VRAM cuentan como destinos de dibujo.
// v13.9 (fase de analisis, con NativeLink dev65): por que todavia le cuesta al juego. nativedraw_frames.log: una linea por
// frame con lo que le costo al PSP (EV_FRAME: compuerta, espera al GE, CPU libre, espejo, ganchos) y lo que hizo la Vita.
// nativedraw_tarde.log: cada sync en que la Vita iba atrasada, con los tiempos de la lista esperada (ver "Atrasos").
// Interruptores (ux0:data/nativedraw/, se apagan con "no" adentro): sin_compuerta, sin_espejo, sin_libre (los aplica NativeLink,
// SB[0x4A]) y sin_captura (la Vita no recorre nada: solo marca las listas como capturadas).
// v13.10 (fase 2, con NativeLink dev66): la compuerta pasa del sync a "antes de encolar la lista siguiente". La muestra de
// huellas de la lista (lifeTake) se deja en todos los niveles con su numero de orden (SB[0x53]): NativeLink la usa para
// comprobar que el juego no reescribe la lista antes de encolar la siguiente. Interruptor compuerta_sync (SB[0x4A] & 8):
// la compuerta se queda en el sync, como antes (para comparar).
// v13.11 (fase 2, punto 3): cada sublista guardada lleva solo las estadisticas que suma distintas de 0 (indice y valor,
// hasta SN) en vez de las 127 palabras: aplicarla suma unas pocas y lee mucha menos memoria (antes ~7 us por sublista).
// En el nivel minimo solo se suman comandos y draws. (v13.11.1, el capturador leyendo directo sin la cache compartida,
// se descarto: en GT casi 3 veces mas esperas. Ver docs/etapa2_fase2_estadisticas.md)
// v13.12 (fase 2, punto 4): 1) las lineas guardadas de una sublista de la VRAM llevan su direccion de la VRAM (no la del
// espejo en que se grabo) y se traducen al espejo del frame al compararlas (mline): antes se comparaba contra el otro
// espejo, que podia tener una copia vieja (las DISTINTAS, todas de la VRAM). 2) Comprobacion por adelantado: el capturador
// anota en orden las sublistas que aplico (estables: racha >= pv_min); al empezar la lista siguiente los lectores las
// comprueban primero, sin esperar a llegar a su CALL (plan pv). Interruptor sin_adelanto. Ver docs/etapa2_fase2_adelanto.md
// v13.13 (Ridge Racer, que no repite sublistas: todo se interpreta): walkList de a linea de 64 bytes (~1 070 -> ~800 ns por
// comando) y el capturador en cualquier nucleo (nucleo_captura). Ver docs/etapa2_ridge_racer.md
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdarg.h>
#include <arm_neon.h>
#include "version.h"

#define PSP(a) (0x68000000u + ((a) & 0x0FFFFFFFu))
#define SB ((volatile uint32_t *)PSP(0x0A000000u))
#define SB_MAGIC 0x424D4F53u
#define SB_N 4096
#define LOGC "ux0:pspemu/nativedraw_captura.log"
enum { EV_ENQ = 1, EV_HEAD, EV_STALL, EV_DSYNC, EV_LSYNC, EV_FLIP, EV_GEFIN, EV_SYNCEND, EV_CPU, EV_CPUSUM, EV_FRAME, EV_VIDA };  // (7-10: NativeLink dev63; 11: dev65; 12: dev65.2)
static inline uint32_t now(void) { return sceKernelGetProcessTimeLow(); }

// --- Estadisticas (por segundo) ---
typedef struct {
  uint32_t t0, frames, lists, cmds, cmds_max, draws, draws_max, us, us_max, own, hits, rown[2];
  uint32_t d3d, dthr, dclear, dempty, prim[8], curves, verts;
  uint32_t ch_tex, ch_mtx, ch_rt, ch_other, ch_none, texnew, batches, batches_cont, rt_switch;
  uint32_t textures, tex_vram, dtex_vram, dvert_vram, vbytes, ibytes, cluts, clut_vram, transfers, tr_vram;
  uint32_t vcalls, vjumps, lost, bad, signals, bjumps, events, stalls, syncs, syncs_waited, sync_wait_us, behind, resyncs;
  uint32_t lsyncs, flips_behind, aborted, yields;
  uint32_t batches_notl, only_tl, sleep_us, cpu_cap, cpu_lec[2], cpu_core[4];  // v12.10
  uint32_t calls_ram, memo_hit, memo_miss, memo_fail, memo_no, memo_cmds, memo_lines, memo_bad, memo_chk, memo_reset, memo_dep;  // v12.12
  uint32_t v_ok, v_cut, tex_rt, memo_us;  // v13.1: sublistas de la VRAM recorridas desde el espejo, cortadas (pagina sin copia), texturas en un destino
  uint32_t memo_pre, lv_lines, lv_ok, lv_bad, lv_late;  // v13.4: aplicadas con la comprobacion de los lectores; lo que hicieron los lectores
  uint32_t ge_us, ge_runs, ge_max, ge_stall, ge_n, sy_n, sy_us, sy_wait, sy_busy;  // v13.5, T6: GE del PSP y esperas del juego al GE
  uint32_t dc_n, dc_kb, dm_n, dm_kb, cpu_drop, cpu_vram, dc_all;                   // v13.5, T4: escrituras de la CPU del juego
  uint32_t pf_n, pf_us, pf_gate, pf_gate_n, pf_nowait, pf_sync, pf_spin, pf_esp, pf_pg, pf_post, pf_late, pf_late_us;  // v13.9: EV_FRAME y atrasos
  uint32_t eg_n, eg_us;  // v13.10: esperas del juego antes de encolar (compuerta de NativeLink dev66)
  uint32_t rd_us, rd_n;  // v13.9: lecturas propias del capturador cronometradas (1 de 16)
  uint32_t hit_us, hit_n;  // v13.9.4: lineas de la cache compartida cronometradas (1 de 8)
  uint32_t pv_ok, pv_bad, pv_late, pv_plan;  // v13.12: comprobacion por adelantado (lectores) y sublistas del plan
  uint32_t chg[256];  // cambios reales de cada registro "otro" (que impide juntar draws)
} Stats;
static Stats acc, rep;
static volatile int rep_ready;
// v13.9.5: nivel de registro (ux0:data/nativedraw/registro: "minimo", "mediano" o "extremo"; sin archivo: mediano). NativeLink lo
// lee en SB[0x4E]. minimo (para publicar): una linea cada 10 s, sin revision de la memoria de sublistas. mediano (desarrollo):
// lo de siempre por segundo + revision 1/32 + costo por frame para el juego (EV_FRAME, CPU libre del PSP). extremo (analisis):
// ademas nativedraw_frames.log, nativedraw_tarde.log, vida de la lista y de los datos, tiempos de lectura y de la memoria,
// muestreo del espejo y escrituras de la CPU del juego (T4). Ver docs/registros.md.
static uint32_t nivel = 1;
static uint32_t f_cmds, f_draws, f_us;  // del frame en curso

// --- Lectura de la RAM del PSP ---
// Cada lectura de la RAM del PSP desde el ARM espera mucho (vista sin cache): casi todo el costo del capturador es esa
// espera, no interpretar. Historia: DMA en ventanas de 2 KB ~200 us por copia (v12.2); palabra a palabra ~1,5 us por
// comando (v12.3); de a 64 bytes con NEON ~350 ns (v12.4). v12.9:
// 1. Se traen 256 bytes de una vez (8 cargas NEON seguidas: las esperas se solapan).
// 2. Dos lectores (nucleos 0 y 2, prioridad 0xB0: solo el tiempo que les sobra a esos nucleos) recorren la misma lista
//    por adelantado, repartiendose las sublistas, y dejan las lineas de 64 bytes en una cache compartida; el capturador
//    (nucleo 1) las toma de ahi. Cada linea va marcada con la lista (numero de orden): no se usa de una lista a otra.
//    Lo que esta despues del stall (todavia sin escribir) no se comparte. ux0:data/nativedraw/captura_sin_lectores: sin ellos.
// La RAM del PSP puede estar en varios bloques de la Vita (v12.2): hasta 4 tramos legibles.
static uint32_t rlo[4], rhi[4], nr;
static inline uint32_t ramEnd(uint32_t a) {  // fin del tramo legible que contiene a (0: no legible)
  for (uint32_t i = 0; i < nr; i++) if (a >= rlo[i] && a + 4 <= rhi[i]) return rhi[i];
  return 0;
}
static inline int okRam(uint32_t a) { return ramEnd(a) != 0; }
static inline int isVram(uint32_t a) { return (a & 0x0F000000u) == 0x04000000u; }

// --- Espejo de la VRAM (v13.1 con NativeLink dev62; doble desde v13.6 con dev64) ---
// La VRAM del PSP no esta mapeada en Adrenaline. El capturador pide paginas de 4 KB en dos mapas de bits del buzon:
// "listas" (sublistas que el juego llama en la VRAM: GT) y "datos" (texturas y paletas; no las de un destino de dibujo
// o de TRANSFER, que la Vita tendra en HD). En cada flip el GE del PSP copia todas las pedidas (ver espejoFlip en main.c)
// alternando dos espejos: las copias pares a MIRROR y las impares a MIRROR2. Cada lista se lee con la ultima copia ya
// terminada al empezarla (esp_u, sin esperar: v13.6.1 esperaba la del ultimo flip y por la compuerta del sync eso frenaba
// a GT; ademas, si el juego encolo la lista antes del flip, la copia buena es la anterior) y la anota en el buzon
// (SB[0x48]) para que el GE no la pise. Una pagina sirve en un espejo si su ultima copia ahi ya termino (espCheck). El hilo
// del espejo solo cuenta cuantas paginas cambian (por muestreo).
#define MIRROR 0x0A200000u
#define MIRROR2 0x0AC00000u
#define CPY(b, p) SB[((b) ? 0x8400 : 0x200) + (p)]  // ultima copia de la pagina p en el espejo b (la escribe NativeLink)
static uint32_t esp_on, want_l[16], want_d[16], rt_pg[16];  // (del capturador)
static volatile uint32_t esp_ok[2][16], esp_reset;           // paginas con copia valida en cada espejo
static volatile uint32_t esp_u;                               // copia con la que se lee la lista actual (0: ninguna todavia)
static struct { uint32_t chk, nuevas, ch_l, ch_d, us, listas, ant; } ES;  // estadisticas del espejo
static int espCheck(uint32_t b, uint32_t p, uint32_t u) {  // la ultima copia de p en el espejo b ya termino: valida
  uint32_t sq = esp_on ? CPY(b, p) : 0;
  if (!sq || (int)(u - sq) < 0) return 0;
  __sync_fetch_and_or(&esp_ok[b][p >> 5], 1u << (p & 31));
  return 1;
}
static inline uint32_t vtr(uint32_t a) {  // direccion legible para a (la VRAM, en el espejo del frame); 0: no se puede leer
  if (!isVram(a)) return okRam(a) ? a : 0;
  uint32_t o = a & 0x1FFFFF, p = o >> 12, u = esp_u, b = u & 1;
  return esp_ok[b][p >> 5] >> (p & 31) & 1 || espCheck(b, p, u) ? (b ? MIRROR2 : MIRROR) + o : 0;
}
static void want(uint32_t a, uint32_t n, int list) {  // pide las paginas de la VRAM [a, a + n)
  if (!esp_on) return;
  uint32_t o = a & 0x1FFFFF, p = o >> 12, e = (o + (n ? n : 1) - 1) >> 12;
  for (; p <= e && p < 512; p++) {
    uint32_t w = p >> 5, b = 1u << (p & 31);
    if (list) { if (!(want_l[w] & b)) { want_l[w] |= b; SB[0x20 + w] = want_l[w]; } }
    else if (!(rt_pg[w] & b) && !(want_d[w] & b)) { want_d[w] |= b; SB[0x30 + w] = want_d[w]; }
  }
}
static void markRt(uint32_t a, uint32_t n) {  // destino de dibujo: no se pide como dato (se deja de pedir si ya estaba)
  uint32_t o = a & 0x1FFFFF, p = o >> 12, e = (o + n - 1) >> 12;
  for (; p <= e && p < 512; p++) {
    uint32_t w = p >> 5, b = 1u << (p & 31);
    rt_pg[w] |= b;
    if (want_d[w] & b) { want_d[w] &= ~b; if (esp_on) SB[0x30 + w] = want_d[w]; }
  }
}

typedef struct { uint32_t buf[64] __attribute__((aligned(16))); uint32_t a, n, own, hits; } Rd;  // ultimos bytes leidos
static Rd R0, RR[2];  // capturador y lectores
static uint32_t tm_memo, tm_apply;  // (v13.9.4) us del frame en memoTry (todo) y en aplicar lo guardado
static uint32_t runClk(SceUID t);
static SceUID cap_uid;
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
  uint32_t th = nivel >= 2 && r == &R0 && !(r->hits & 7) ? now() : 0;  // (v13.9.4) 1 de cada 8 lineas de la cache compartida, cronometrada
  if (clGet(b, ord, r->buf)) { r->a = b; r->n = 64; r->hits++; if (th) { acc.hit_us += now() - th; acc.hit_n++; } return r->buf[(a - b) >> 2]; }
  uint32_t n = b + 256 <= e ? 256 : 64;
  uint32_t t = nivel >= 2 && r == &R0 && !(r->own & 15) ? now() : 0;  // (v13.9) 1 de cada 16 lecturas del capturador, cronometrada
  if (n == 256) neon256(r->buf, b); else neon64(r->buf, b);
  if (t) { acc.rd_us += now() - t; acc.rd_n++; }
  r->a = b; r->n = n; r->own++;
  for (uint32_t k = 0; k < n; k += 64) clPut(b + k, ord, r->buf + k / 4);
  return r->buf[(a - b) >> 2];
}

// --- Estado del GE (sombra) ---
typedef struct Memo Memo;
static Memo *rec;  // sublista que se esta grabando (memoria de sublistas, v12.12)
static void recDraw(int idx, int tex), recTex(uint32_t ta), recRead(uint32_t c), recVert(uint32_t a, uint32_t n);
static uint32_t reg[256], mtx[5][96], mi[5];
static uint8_t cls[256];  // 0 no es estado, 1 textura, 2 matriz, 3 destino (rt), 4 otro
enum { C_TEX = 1, C_MTX = 2, C_RT = 4, C_OTHER = 8, C_TL = 16 };  // C_TL: TEXLEVEL (0xC8), un "otro" aparte
static uint32_t dirty, vaddr, iaddr, vset, last_prim = 99, last_vend, last_fb;
static uint32_t tset[2048], tgen[2048], gen = 1, ntex, last_clut[2];  // (gen empieza en 1: tgen en 0 = vacio)
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
  cls[0xC8] = C_TL;
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

// --- Vida de los datos (v13.5) ---
// La Vita va a dibujar 1 frame despues de leer la lista, y su GPU leera vertices, texturas y paletas directo de la RAM
// del PSP. Si el juego los reescribe antes, dibujaria otra cosa. Muestra: 1 de cada 64 draws (sus vertices o indices),
// 1 de cada 8 texturas nuevas del frame y 1 de cada 16 cargas de paleta (hasta 8 vertices y 4 de lo demas por frame,
// solo en la RAM). El capturador calcula la huella de los primeros 256 bytes al anotarla y otra vez en los 2 flips
// siguientes (ltFlip: "1 y 2 frames despues", cuando la Vita los estaria dibujando), y cuenta cuantas cambiaron y si
// alguna escritura conocida de la CPU (T4) las cubria: un rango, o "vaciar toda la cache". Las sublistas aplicadas por
// la memoria cuentan con los vertices de su primer draw (sv_a).
// v13.5.2: antes lo revisaba el hilo del espejo a los 17/33 ms: recibia poco tiempo (casi todas llegaban tarde) y le
// quitaba tiempo al lector del nucleo 0 (el puente de GT bajo a 49-56 fps).
typedef struct { uint32_t a, n, kind, g, h0, t0, st, ch1; } Smp;
#define SMP_N 1024
static Smp smp[SMP_N];
static volatile uint32_t smp_w, smp_r;
static uint32_t smp_k[4], smp_f[4];
static const uint8_t smp_rate[4] = { 63, 63, 7, 15 }, smp_max[4] = { 8, 4, 4, 4 };  // (mascaras: 1 de cada 64, 64, 8, 16)
static uint32_t smpHash(uint32_t a, uint32_t n);
static void ltFlip(void);
static void smpAdd(uint32_t a, uint32_t n, uint32_t kind) {  // 0 vertices, 1 indices, 2 textura, 3 paleta
  if (nivel < 2 || (++smp_k[kind] & smp_rate[kind]) || !n || smp_f[kind] >= smp_max[kind] || isVram(a) ||
      ramEnd(a) < a + (n > 256 ? 256 : n) || smp_w - smp_r >= SMP_N) return;
  Smp *x = &smp[smp_w & (SMP_N - 1)];
  x->a = a; x->n = n > 256 ? 256 : n; x->kind = kind; x->g = gen; x->t0 = now(); x->h0 = smpHash(x->a, x->n); x->st = 0;
  smp_w++; smp_f[kind]++;
}
#define CW_N 256  // ultimas escrituras conocidas de la CPU del juego (T4): [lo, hi) y hora de la Vita al leer el evento
static struct { uint32_t lo, hi, t; } cw[CW_N];
static volatile uint32_t cw_n, cw_all_t;  // (cw_all_t: ultimo "vaciar toda la cache")
static struct { volatile uint32_t n[4], ch1[4], ch2[4], warn[4], wall[4], ms1, ms2; } LT;  // vida de los datos (acumulados)

static void draw(uint32_t d) {
  uint32_t n = d & 0xFFFF, type = (d >> 16) & 7, vt = reg[0x12], idx = (vt >> 11) & 3;
  if (!n) { acc.dempty++; return; }
  acc.draws++; f_draws++; acc.prim[type]++; acc.verts += n;
  int clear = reg[0xD3] & 1, tex = (reg[0x1E] & 1) && !clear;
  if (rec) recDraw(idx, tex);  // (antes de que cambien vaddr/iaddr)
  if (rec && tex) recTex(TEXADDR());
  if (clear) acc.dclear++;
  else if (vt >> 23 & 1) acc.dthr++;
  else acc.d3d++;
  uint32_t fb = (reg[0x9C] & 0xFFFFF0) | ((reg[0x9D] << 8) & 0xFF000000);
  if (fb != last_fb) {
    acc.rt_switch++; last_fb = fb;
    uint32_t h = ((reg[0xD6] >> 10) & 0x3FF) + 1, w = reg[0x9D] & 0x7FF;  // alto: hasta el borde de abajo del scissor
    markRt(fb, w * h * ((reg[0xD2] & 3) == 3 ? 4 : 2));
    if (reg[0x23] & 1) markRt(reg[0x9E] & 0xFFFFF0, (reg[0x9F] & 0x7FF) * h * 2);  // z (con prueba de z)
  }
  if (isVram(vaddr)) acc.dvert_vram++;
  static uint32_t vs_vt = ~0u, vs_sz;  // (v13.13) el tamano del vertice cambia poco: se recuerda
  if (vt != vs_vt) { vs_vt = vt; vs_sz = vsize(vt); }
  uint32_t vs = vs_sz, vstart = vaddr;
  if (nivel >= 2) { if (idx) smpAdd(iaddr, n * (idx == 1 ? 1 : idx == 2 ? 2 : 4), 1); else smpAdd(vaddr, n * vs, 0); }
  if (rec && !idx) recVert(vstart, n * vs);
  if (idx) { acc.ibytes += n * (idx == 1 ? 1 : idx == 2 ? 2 : 4); iaddr += n * (idx == 1 ? 1 : idx == 2 ? 2 : 4); }
  else { acc.vbytes += n * vs; vaddr += n * vs; }
  if (tex) {
    uint32_t ta = TEXADDR();
    if (texAdd(ta)) {
      acc.textures++; if (isVram(ta)) acc.tex_vram++;
      static const uint8_t bp[16] = { 16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8, 8, 8, 8, 8, 8 };
      uint32_t w = reg[0xA8] & 0x7FF, h = 1u << ((reg[0xB8] >> 8) & 0xF);
      if (!w) w = 1u << (reg[0xB8] & 0xF);
      if (nivel >= 2) smpAdd(ta, w * h * bp[reg[0xC3] & 0xF] / 8, 2);
    }
    if (isVram(ta)) {
      acc.dtex_vram++;
      uint32_t p = (ta & 0x1FFFFF) >> 12;
      if (rt_pg[p >> 5] >> (p & 31) & 1) acc.tex_rt++;
      else if (dirty & C_TEX || !((want_d[p >> 5] >> (p & 31)) & 1)) {  // bytes del nivel 0 (bpp de cada formato del GE)
        static const uint8_t bpp[16] = { 16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8, 8, 8, 8, 8, 8 };
        uint32_t w = reg[0xA8] & 0x7FF, h = 1u << ((reg[0xB8] >> 8) & 0xF);
        if (!w) w = 1u << (reg[0xB8] & 0xF);
        want(ta, w * h * bpp[reg[0xC3] & 0xF] / 8, 0);
      }
    }
    if (dirty & C_TEX) acc.texnew++;
  }
  if (dirty & C_TEX) acc.ch_tex++;
  if (dirty & C_MTX) acc.ch_mtx++;
  if (dirty & C_RT) acc.ch_rt++;
  if (dirty & (C_OTHER | C_TL)) acc.ch_other++;
  if (dirty == C_TL) acc.only_tl++;
  if (!dirty) acc.ch_none++;
  // Lote: draws seguidos que se podrian dibujar como uno (mismo estado y mismo tipo de primitiva). "Contiguo": ademas
  // los vertices siguen donde termino el anterior (sin indices nuevos)
  int same = !dirty && type == last_prim;
  if (!same) acc.batches++;
  if ((dirty & ~C_TL) || type != last_prim) acc.batches_notl++;  // si TEXLEVEL fuera por draw (no corta el lote)
  if (!same || idx || (vset && vstart != last_vend)) acc.batches_cont++;
  last_prim = type; last_vend = vaddr; dirty = vset = 0;
}

// --- Listas en cola (como el GE: una a la vez, en orden) ---
typedef struct { uint32_t pc, stall, qid, ord, off, sp, stk[32][2], prev, n, ncall, t_enq, t_stall, t_seen, t_start, work, own0, hit0, memo0, ahead, lo, hi, nsub, subs[16], cpu0; } List;  // (v13.9: t_enq/t_stall hora del PSP, t_seen/t_start de la Vita)
#define LIST_MAX 400000  // comandos: mas que eso es una lista que el juego ya reescribio (la Vita iba atrasada)
static List q[64];
static uint32_t qh, qt;  // la primera sin terminar y la siguiente libre
static inline uint32_t rel(List *l, uint32_t d) { return ((((reg[0x10] & 0x0F0000) << 8) | d) + l->off) & 0x0FFFFFFF; }

static void clog(const char *fmt, ...);
static void rput(int k, const char *fmt, ...);  // (v13.9) 0: nativedraw_frames.log, 1: nativedraw_tarde.log
static uint32_t sin_captura, gp_flags;           // (v13.9) interruptores
typedef struct { uint32_t reg[256], mtx[5][96], mi[5], v[8]; } GeState;
static GeState g_tmp;
static void stSave(GeState *g) {
  sceClibMemcpy(g->reg, reg, sizeof reg); sceClibMemcpy(g->mtx, mtx, sizeof mtx); sceClibMemcpy(g->mi, mi, sizeof mi);
  uint32_t v[8] = { dirty, vaddr, iaddr, vset, last_prim, last_vend, last_fb, 0 };
  sceClibMemcpy(g->v, v, sizeof v);
}
static void stLoad(GeState *g) {
  sceClibMemcpy(reg, g->reg, sizeof reg); sceClibMemcpy(mtx, g->mtx, sizeof mtx); sceClibMemcpy(mi, g->mi, sizeof mi);
  dirty = g->v[0]; vaddr = g->v[1]; iaddr = g->v[2]; vset = g->v[3]; last_prim = g->v[4]; last_vend = g->v[5];
  last_fb = g->v[6];
}

// --- v12.12: memoria de sublistas ---
// GT repite ~88 % de sus comandos en sublistas identicas de un frame a otro (v12.11) y lo caro es interpretar comando por
// comando (~360 ns), no leer. Cuando el nivel de arriba llama una sublista (CALL a la RAM), se busca por (direccion, BASE,
// OFFSET de entrada). Si ya se recorrio, se comparan sus lineas de memoria (64 bytes; incluye sus sublistas anidadas) con
// la copia guardada, palabra por palabra: si son iguales, se aplica lo que hizo (registros y matrices a la salida,
// direcciones de vertices/indices, contadores, texturas) sin interpretarla. Si no estaba o cambio, se recorre y se guarda.
// Aproximado (solo estadisticas): "cambios" y lotes del primer draw dependen del estado de entrada; chg[] no se suma.
// Revision: 1 de cada 32 sublistas que se podrian aplicar se recorre igual y se compara el estado que deja con lo
// guardado (DISTINTAS en el log = error de la memoria). ux0:data/nativedraw/memo_revisar: todas.
// ux0:data/nativedraw/sin_memoria: apagada (para comparar).
// v13.2: tabla y deposito 4 veces mas grandes (si hay memoria): con las sublistas de la VRAM GT llama ~250 por frame y
// con 512 lugares y 512 KB el deposito se vaciaba 4-5 veces por segundo (todo se volvia a grabar: 33-38 fps).
static uint32_t MEMO_N = 2048, POOL_N = 32768, memo_sh = 21;  // sublistas guardadas, lineas de 64 bytes del deposito (2 MB)
#define MR 32         // tramos de memoria por sublista
#define MRG 64        // registros que puede dejar cambiados
#define SW (sizeof(Stats) / 4 - 256)  // palabras de Stats que se suman (las anteriores a chg[256], que va al final)
#define SN 48         // (v13.11) estadisticas distintas de 0 que puede sumar una sublista (mas: no se guarda)
enum { M_DRAW = 1, M_VABS = 2, M_IABS = 4, M_CLUT = 8, M_VDEP = 16, M_IDEP = 32 };  // VDEP/IDEP: dibuja con los vertices/indices de entrada
static const uint8_t moff[5] = { 0, 12, 24, 40, 52 }, msz[5] = { 12, 12, 16, 12, 96 };
struct Memo {
  uint32_t t, base, off, pgen, state, fail_gen;  // state: 0 grabando, 1 lista, 2 no se puede guardar (reintento en 64 frames)
  uint32_t nr, ra[MR], rn[MR], rp[MR];            // tramos: primera linea, cantidad, posicion en el deposito
  uint32_t cmds, draws, sn, sv[SN]; uint8_t si[SN]; // lo que suma a las estadisticas: solo lo distinto de 0 (v13.11)
  uint32_t nreg, rv[MRG]; uint8_t rc[MRG];         // registros a la salida
  uint32_t mmask, mi[5], mtx[148];                 // matrices tocadas
  uint32_t flags, vaddr, iaddr, vset, dirty, last_prim, last_fb, clut[2], ntex, tex[32];
  uint32_t v_in, i_in, ndep, dv[16]; uint8_t dc[16];  // estado de entrada que usa (registros que lee antes de escribirlos)
  uint32_t sv_a, sv_n, sv_rel;  // (v13.5) vertices del primer draw sin indices: para la muestra de la vida de los datos
  uint32_t racha;               // (v13.12) veces seguidas que se aplico sin cambios (recBegin la deja en 0)
  volatile uint32_t ver, vok;  // (al final: recBegin no los borra) version (sube al regrabar) y comprobacion de un lector (vokOf)
};
static Memo *memo;
static uint32_t *pool, pool_top, pool_gen = 1, pool_reset, memo_on, memo_chk;
static Memo *chk;  // sublista que se esta revisando
static uint32_t rec_last, rec_v0, rec_i0, rec_d0, rec_touch[8], rec_dep[8], rec_acc0[SW];
static GeState g_memo;

// Cada (direccion, BASE, OFFSET) puede tener varias versiones (una por estado de entrada), en 8 lugares seguidos
static inline uint32_t memoHash(uint32_t t, uint32_t base, uint32_t off) { return ((t >> 2) ^ (base * 0x9E37) ^ (off >> 4)) * 2654435761u >> memo_sh; }
static Memo *victim;  // version que cambio: se graba encima
static Memo *memoSlot(uint32_t t, uint32_t base, uint32_t off) {  // lugar para grabar una sublista
  if (victim) { Memo *v = victim; victim = NULL; return v; }
  uint32_t i = memoHash(t, base, off);
  for (int k = 0; k < 8; k++, i = (i + 1) & (MEMO_N - 1)) if (memo[i].pgen != pool_gen) return &memo[i];
  return &memo[(memoHash(t, base, off) + (gen & 7)) & (MEMO_N - 1)];  // (llenos: se reemplaza uno)
}
static int depsOk(Memo *e) {
  if ((e->flags & M_VDEP && vaddr != e->v_in) || (e->flags & M_IDEP && iaddr != e->i_in)) return 0;
  for (uint32_t i = 0; i < e->ndep; i++) if (reg[e->dc[i]] != e->dv[i]) return 0;
  return 1;
}
static void recAbort(void) { if (rec) { rec->state = 2; rec->fail_gen = gen; rec = NULL; } }
static void recBegin(List *l, uint32_t t) {
  if (pool_reset || pool_top > POOL_N - 64) { pool_gen++; pool_top = 0; pool_reset = 0; acc.memo_reset++; }
  Memo *e = memoSlot(t, reg[0x10], l->off);
  e->ver++; __sync_synchronize();  // primero la version: un lector que la estuviera comprobando la descarta
  sceClibMemset(e, 0, (char *)&e->ver - (char *)e);
  e->t = t; e->base = reg[0x10]; e->off = l->off; e->pgen = pool_gen; e->v_in = vaddr; e->i_in = iaddr;
  rec = e; rec_last = ~0u; rec_v0 = vaddr; rec_i0 = iaddr; rec_d0 = dirty;
  sceClibMemset(rec_touch, 0, sizeof rec_touch); sceClibMemset(rec_dep, 0, sizeof rec_dep);
  sceClibMemcpy(rec_acc0, &acc, sizeof rec_acc0);
}
// (v13.12) donde se lee hoy la linea guardada b: la de la VRAM, en el espejo del frame (0: sin copia)
static inline uint32_t mline(uint32_t b) { return isVram(b) ? vtr(b) : b; }
static void recLine(uint32_t b, uint32_t rb) {  // linea nueva de la sublista (b; rb donde se leyo): al deposito (R0 la tiene)
  Memo *e = rec;
  rec_last = b;
  for (uint32_t i = 0; i < e->nr; i++) if (b - e->ra[i] < e->rn[i] * 64) return;  // ya guardada (sublista llamada 2 veces)
  if (rb < R0.a || rb + 64 > R0.a + R0.n || pool_top >= POOL_N) { if (pool_top >= POOL_N) pool_reset = 1; recAbort(); return; }
  sceClibMemcpy(pool + pool_top * 16, R0.buf + (rb - R0.a) / 4, 64);
  uint32_t n = e->nr - 1;
  if (e->nr && e->ra[n] + e->rn[n] * 64 == b && e->rp[n] + e->rn[n] == pool_top) e->rn[n]++;
  else if (e->nr < MR) { e->ra[e->nr] = b; e->rn[e->nr] = 1; e->rp[e->nr] = pool_top; e->nr++; }
  else { recAbort(); return; }
  pool_top++;
}
static int nearStall(Memo *e, uint32_t st) {  // algun tramo cerca del stall (puede no estar escrito todavia)
  if (!st) return 0;
  for (uint32_t i = 0; i < e->nr; i++) if (!(e->ra[i] + e->rn[i] * 64 <= st || e->ra[i] >= st + 0x40000)) return 1;
  return 0;
}
static void recEnd(List *l) {
  Memo *e = rec;
  rec = NULL;
  if (nearStall(e, l->stall)) { e->state = 2; e->fail_gen = gen; return; }
  static Stats ds;  // (solo se usan las primeras SW palabras)
  uint32_t *a = (uint32_t *)&acc, *td = (uint32_t *)&ds;
  for (int i = 0; i < SW; i++) td[i] = a[i] - rec_acc0[i];
  Stats *d = &ds;
  d->textures = d->tex_vram = 0;  // se vuelven a contar con texAdd al aplicarla
  // (v12.13) lo que no cuenta el recorrido sino el hilo (eventos, tiempos, frames): la grabacion puede cruzar varias vueltas
  d->t0 = d->frames = d->lists = d->cmds_max = d->draws_max = d->us = d->us_max = d->own = d->hits = d->rown[0] = d->rown[1] = 0;
  d->events = d->stalls = d->syncs = d->syncs_waited = d->sync_wait_us = d->behind = d->resyncs = d->lsyncs = d->flips_behind = 0;
  d->yields = d->sleep_us = d->cpu_cap = d->cpu_lec[0] = d->cpu_lec[1] = 0;
  for (int k = 0; k < 4; k++) d->cpu_core[k] = 0;
  d->memo_hit = d->memo_miss = d->memo_fail = d->memo_no = d->memo_cmds = d->memo_lines = d->memo_bad = d->memo_chk = d->memo_reset = d->memo_dep = d->memo_us = 0;
  d->memo_pre = d->lv_lines = d->lv_ok = d->lv_bad = d->lv_late = 0;
  d->ge_us = d->ge_runs = d->ge_max = d->ge_stall = d->ge_n = d->sy_n = d->sy_us = d->sy_wait = d->sy_busy = 0;
  d->dc_n = d->dc_kb = d->dm_n = d->dm_kb = d->cpu_drop = d->cpu_vram = d->dc_all = 0;
  d->hit_us = d->hit_n = 0;
  d->pv_ok = d->pv_bad = d->pv_late = d->pv_plan = 0;
  d->pf_n = d->pf_us = d->pf_gate = d->pf_gate_n = d->pf_nowait = d->pf_sync = d->pf_spin = d->pf_esp = d->pf_pg = d->pf_post = d->pf_late = d->pf_late_us = d->rd_us = d->rd_n = d->eg_n = d->eg_us = 0;
  e->cmds = d->cmds; e->draws = d->draws;
  for (int i = 0; i < SW; i++)
    if (td[i]) {
      if (e->sn >= SN) { static int nl; if (nl++ < 3) clog("memoria: sublista %08x suma mas de %d estadisticas: no se guarda\n", e->t, SN); e->state = 2; e->fail_gen = gen; return; }
      e->si[e->sn] = i; e->sv[e->sn++] = td[i];
    }
  for (int c = 0; c < 256; c++)
    if (rec_touch[c >> 5] >> (c & 31) & 1) {
      if (e->nreg >= MRG) { e->state = 2; e->fail_gen = gen; return; }
      e->rc[e->nreg] = c; e->rv[e->nreg++] = reg[c];
    }
  for (int m = 0; m < 5; m++) if (e->mmask >> m & 1) { e->mi[m] = mi[m]; sceClibMemcpy(e->mtx + moff[m], mtx[m], msz[m] * 4); }
  e->vaddr = e->flags & M_VABS ? vaddr : vaddr - rec_v0;
  e->iaddr = e->flags & M_IABS ? iaddr : iaddr - rec_i0;
  e->vset = vset; e->dirty = e->flags & M_DRAW ? dirty : dirty & ~rec_d0; e->last_prim = last_prim; e->last_fb = last_fb;
  e->clut[0] = last_clut[0]; e->clut[1] = last_clut[1];
  __sync_synchronize();  // (los lectores la leen desde otro nucleo: todo escrito antes de marcarla lista)
  e->state = 1;
}
static void memoApply(Memo *e) {  // estado del GE como si se hubiera recorrido
  for (uint32_t i = 0; i < e->nreg; i++) reg[e->rc[i]] = e->rv[i];
  for (int m = 0; m < 5; m++) if (e->mmask >> m & 1) { mi[m] = e->mi[m]; sceClibMemcpy(mtx[m], e->mtx + moff[m], msz[m] * 4); }
  vaddr = e->flags & M_VABS ? e->vaddr : vaddr + e->vaddr;
  iaddr = e->flags & M_IABS ? e->iaddr : iaddr + e->iaddr;
  if (e->flags & (M_DRAW | M_VABS)) vset = e->vset;
  dirty = e->flags & M_DRAW ? e->dirty : dirty | e->dirty;
  if (e->flags & M_DRAW) { last_prim = e->last_prim; last_fb = e->last_fb; last_vend = vaddr; }
  if (e->flags & M_CLUT) { last_clut[0] = e->clut[0]; last_clut[1] = e->clut[1]; }
}
// CALL del nivel de arriba a la RAM: 1 = aplicada (no se recorre), 0 = recorrerla (grabando o revisando), -1 = recorrerla sin grabar
static inline int lineEq(const uint32_t *a, const uint32_t *b) {  // 64 bytes iguales (NEON)
  uint32x4_t x = veorq_u32(vld1q_u32(a), vld1q_u32(b));
  x = vorrq_u32(x, veorq_u32(vld1q_u32(a + 4), vld1q_u32(b + 4)));
  x = vorrq_u32(x, veorq_u32(vld1q_u32(a + 8), vld1q_u32(b + 8)));
  x = vorrq_u32(x, veorq_u32(vld1q_u32(a + 12), vld1q_u32(b + 12)));
  uint32x2_t y = vorr_u32(vget_low_u32(x), vget_high_u32(x));
  return (vget_lane_u32(y, 0) | vget_lane_u32(y, 1)) == 0;
}
// v13.4: comprobacion por adelantado. Leer las lineas de la RAM del PSP es lo caro (~6,5 us cada 256 bytes): los lectores,
// al llegar a un CALL del nivel de arriba con sublistas guardadas, comparan ellos todas sus lineas con el deposito y dejan
// en vok la lista (numero de orden) y la version comprobadas. El capturador, si vok coincide, la aplica sin leer nada.
// Si el capturador regraba la sublista mientras tanto (ver++), o se vacia el deposito, la comprobacion se descarta.
static volatile uint32_t lv_lines[2], lv_ok[2], lv_bad[2], lv_late[2];  // (acumulados, de cada lector)
// v13.4: reparto de los CALL del nivel de arriba (n-esimo de la lista) entre los lectores segun su ritmo: cada uno toma
// el siguiente libre (antes: uno si, uno no; el lector del nucleo 0 recibe la mitad de tiempo y llegaba tarde a los suyos).
// El capturador marca los que ya alcanzo: los lectores atrasados se los saltan. claim[n] = numero de lista << 2 | quien.
#define CLM 2048
static volatile uint32_t claim[CLM];
static inline void claimSet(uint32_t n, uint32_t ord) { if (n < CLM) claim[n] = ord << 2; }
static int claimTake(uint32_t n, uint32_t ord, int id) {  // 1: este lector se encarga del CALL n
  if (n >= CLM) return n % 2 == (uint32_t)id;
  uint32_t o = claim[n];
  if (o >> 2 == (ord & 0x3FFFFFFF)) { if (!(o & 3)) lv_late[id]++; return 0; }  // ya tomado (si es del capturador: llegamos tarde)
  return __sync_bool_compare_and_swap(&claim[n], o, ord << 2 | (id + 1));
}
static inline uint32_t vokOf(uint32_t ord, uint32_t ver) { return ord << 16 | (ver & 0xFFFF); }
// (v13.12) compara las lineas guardadas de e con las de hoy: 1 iguales, 0 distintas, -1 no se puede (cerca del stall o
// una pagina de la VRAM sin copia en el espejo del frame)
static int memoLines(int id, Rd *r, Memo *e, uint32_t ord) {
  uint32_t nr = e->nr, st = pub_stall;
  if (nr > MR) return -1;
  for (uint32_t j = 0; j < nr; j++) {
    uint32_t a = e->ra[j], n = e->rn[j], p = e->rp[j];
    if (p + n > POOL_N || (st && !(a + n * 64 <= st || a >= st + 0x40000))) return -1;  // (cerca del stall: lo ve el capturador)
    for (uint32_t m = 0; m < n; m++) {
      uint32_t b = mline(a + m * 64);
      if (!b || ramEnd(b) < b + 64) return -1;
      rdc(r, b, ord); lv_lines[id]++;
      if (!lineEq(r->buf + (b - r->a) / 4, pool + (p + m) * 16)) return 0;
    }
  }
  return 1;
}
// Comprueba e para la lista ord y deja la marca (vok): 1 iguales, 0 distintas, 2 ya estaba comprobada, -1 nada
static int memoMark(int id, Rd *r, Memo *e, uint32_t ord) {
  uint32_t pg = pool_gen, v = e->ver;
  __sync_synchronize();
  if (e->pgen != pg || e->state != 1) return -1;
  if (e->vok == vokOf(ord, v)) return 2;
  int g = memoLines(id, r, e, ord);
  __sync_synchronize();
  if (g < 0 || e->ver != v || pool_gen != pg || pub_ord != ord) return -1;  // el capturador la cambio, o ya es otra lista
  e->vok = g ? vokOf(ord, v) : 0;
  return g;
}
static int memoVerify(int id, Rd *r, uint32_t t, uint32_t base, uint32_t off, uint32_t ord) {  // 1: alguna version comprobada
  int ok = 0;
  uint32_t i = memoHash(t, base, off);
  for (int k = 0; k < 8; k++, i = (i + 1) & (MEMO_N - 1)) {
    Memo *e = &memo[i];
    if (e->t != t || e->base != base || e->off != off) continue;
    int g = memoMark(id, r, e, ord);
    if (g > 0) { ok = 1; if (g == 1) lv_ok[id]++; } else if (!g) lv_bad[id]++;
  }
  return ok;
}
// v13.12: comprobacion por adelantado. El capturador anota en pv[pv_c] las sublistas que aplica (en orden, solo las que ya
// salieron iguales pv_min veces seguidas); al empezar la lista siguiente ese es el plan (pv_plan) y los lectores lo
// comprueban antes de recorrer nada, tomando la siguiente libre (pv_i) DESDE EL FINAL (v13.12.2): en orden iban a la par
// del capturador (no son mas rapidos que el) y comparaban las mismas sublistas que el estaba por usar. Desde el final
// comprueban primero lo que el capturador necesita mas tarde y se detienen al encontrarse con el (pv_pos: lo que ya paso).
// Riesgo: si el juego cambia una sublista despues de comprobada y antes de que el capturador llegue, se usa la vieja (GT
// encola la lista completa y no la toca; la revision 1/32 lo veria como DISTINTAS). Interruptor sin_adelanto.
#define PV_N 1024
static Memo *pv[2][PV_N], **pv_plan;
static uint32_t pv_n[2], pv_c, pv_min = 8, pv_on = 1;
static volatile uint32_t pv_pn, pv_i, pv_pos, pv_ok[2], pv_bad[2], pv_late[2];
static void pvRun(int id, Rd *r, uint32_t ord) {  // (lector) el plan de la lista ord
  r->n = 0;
  for (;;) {
    uint32_t i = __sync_fetch_and_add(&pv_i, 1);
    if (i >= pv_pn || pub_ord != ord) return;
    uint32_t k = pv_pn - 1 - i;  // (v13.12.2) desde el final
    if (k < pv_pos) { pv_late[id]++; return; }  // se encontro con el capturador: lo que queda ya lo paso el
    int g = memoMark(id, r, pv_plan[k], ord);
    if (g == 1) pv_ok[id]++; else if (!g) pv_bad[id]++;
  }
}
static int memoTry(List *l, uint32_t t) {
  Memo *e = NULL;
  uint32_t base = reg[0x10], i = memoHash(t, base, l->off), seen = 0, no = 0;
  victim = NULL;
  for (int k = 0; k < 8 && !e; k++, i = (i + 1) & (MEMO_N - 1)) {
    Memo *m = &memo[i];
    if (m->pgen != pool_gen || m->t != t || m->base != base || m->off != l->off) continue;
    seen = 1;
    if (m->state == 2) { if (gen - m->fail_gen < 64) no = 1; else victim = m; }  // (pasado el plazo se reintenta ahi)
    else if (m->state == 1 && depsOk(m)) e = m;
  }
  if (!e) {
    if (no) { acc.memo_no++; return -1; }
    if (seen) acc.memo_dep++; else acc.memo_miss++;  // (seen: ya estaba, con otro estado de entrada: otra version)
    return 0;
  }
  if (nearStall(e, l->stall)) return -1;
  if (e->vok == vokOf(l->ord, e->ver)) { acc.memo_pre++; goto ok; }  // un lector ya la comprobo en esta lista
  static uint32_t n8;
  uint32_t lines = 0, tm = ++n8 & 7 ? 0 : now();  // (tiempo de la comparacion: 1 de cada 8, x8)
#define TM() if (tm) acc.memo_us += (now() - tm) * 8
  for (uint32_t i = 0; i < e->nr; i++)
    for (uint32_t k = 0; k < e->rn[i]; k++) {
      uint32_t b = mline(e->ra[i] + k * 64), *p = pool + (e->rp[i] + k) * 16;
      if (!b || ramEnd(b) < b + 64) { acc.memo_fail++; victim = e; TM(); return 0; }
      rdc(&R0, b, l->ord);
      const uint32_t *s = R0.buf + (b - R0.a) / 4;
      lines++;
      if (!lineEq(s, p)) { acc.memo_lines += lines; acc.memo_fail++; victim = e; TM(); return 0; }
    }
  acc.memo_lines += lines;
  TM();
ok:;
  if (e->racha < 255) e->racha++;
  if (pv_on && e->racha >= pv_min && pv_n[pv_c] < PV_N) { pv[pv_c][pv_n[pv_c]++] = e; pv_pos = pv_n[pv_c]; }  // (v13.12) plan de la lista siguiente
  static uint32_t nchk;
  if (memo_chk || (nivel && !(++nchk & 31))) {  // revision (1 de cada 32, o todas): se calcula lo guardado y se recorre igual para comparar
    stSave(&g_tmp); memoApply(e); stSave(&g_memo); stLoad(&g_tmp);
    chk = e; acc.memo_chk++;
    return -1;
  }
  uint32_t ta = nivel >= 2 ? now() : 0;  // (v13.9.4)
  if (e->sv_n) smpAdd(e->sv_rel ? vaddr + e->sv_a : e->sv_a, e->sv_n, 0);  // (antes de memoApply: vaddr de entrada)
  memoApply(e);
  for (uint32_t i = 0; i < e->ntex; i++) if (texAdd(e->tex[i])) { acc.textures++; if (isVram(e->tex[i])) acc.tex_vram++; else smpAdd(e->tex[i], 256, 2); }
  uint32_t *a = (uint32_t *)&acc;
  if (nivel) for (uint32_t i = 0; i < e->sn; i++) a[e->si[i]] += e->sv[i];
  else { acc.cmds += e->cmds; acc.draws += e->draws; }  // (minimo: solo lo que sale en su linea)
  f_cmds += e->cmds; f_draws += e->draws; l->n += e->cmds;
  acc.memo_hit++; acc.memo_cmds += e->cmds;
  if (ta) tm_apply += now() - ta;
  return 1;
}
static void chkEnd(void) {  // revision: el recorrido debe dejar lo mismo que lo guardado
  Memo *e = chk;
  chk = NULL;
  int bad = 0, c = 0;
  for (c = 0; c < 256; c++) if (reg[c] != g_memo.reg[c]) { bad = 1; break; }
  if (!bad && sceClibMemcmp(mtx, g_memo.mtx, sizeof mtx)) bad = 2;
  if (!bad && sceClibMemcmp(mi, g_memo.mi, sizeof mi)) bad = 3;
  if (!bad && (vaddr != g_memo.v[1] || iaddr != g_memo.v[2])) bad = 4;
  if (!bad) return;
  acc.memo_bad++;
  static int nlog;
  if (nlog++ < 10)
    clog("memoria: sublista %08x (base %06x) DISTINTA al recorrerla: %s %02x guardado=%06x recorrido=%06x | vertices %08x/%08x\n",
         e->t, e->base, bad == 1 ? "registro" : bad == 2 ? "matriz" : bad == 3 ? "indice de matriz" : "direccion", bad == 1 ? c : 0,
         bad == 1 ? g_memo.reg[c] : 0, bad == 1 ? reg[c] : 0, g_memo.v[1], vaddr);
}
static void recRead(uint32_t c) {  // registro leido: si todavia no se escribio dentro, es estado de entrada
  if (rec_touch[c >> 5] >> (c & 31) & 1 || rec_dep[c >> 5] >> (c & 31) & 1) return;
  rec_dep[c >> 5] |= 1u << (c & 31);
  if (rec->ndep >= 16) { recAbort(); return; }
  rec->dc[rec->ndep] = c; rec->dv[rec->ndep++] = reg[c];
}
static void recDraw(int idx, int tex) {
  rec->flags |= M_DRAW;
  if (!(rec->flags & M_VABS)) rec->flags |= M_VDEP;
  if (idx && !(rec->flags & M_IABS)) rec->flags |= M_IDEP;
  static const uint8_t r1[] = { 0x12, 0xD3, 0x1E, 0x9C, 0x9D }, r2[] = { 0xA0, 0xA8 };
  for (unsigned i = 0; i < sizeof r1 && rec; i++) recRead(r1[i]);
  for (unsigned i = 0; i < sizeof r2 && rec && tex; i++) recRead(r2[i]);
}
static void recVert(uint32_t a, uint32_t n) {  // (v13.5) vertices del primer draw: muestra de la vida de los datos
  if (rec->sv_n || !n) return;
  rec->sv_rel = !(rec->flags & M_VABS); rec->sv_a = rec->sv_rel ? a - rec_v0 : a; rec->sv_n = n;
}
static void recTex(uint32_t ta) {
  for (uint32_t i = 0; i < rec->ntex; i++) if (rec->tex[i] == ta) return;
  if (rec->ntex < 32) rec->tex[rec->ntex++] = ta;  // (mas de 32 texturas distintas: el contador de texturas queda corto)
}


// Recorre la lista hasta su stall o su fin (1: termino), con un tope de comandos por llamada
#define UNWIND() (l->sp = 0, l->pc = l->stk[0][0], l->off = l->stk[0][1], recAbort(), chk = NULL)
// v13.13 (Ridge Racer: sin sublistas que saltar, todo se interpreta, ~1 us por comando): funcion aparte (noinline:
// dentro de walk el compilador se quedaba sin registros y en cada comando recalculaba &q[...] y usaba la pila) y de a
// linea de 64 bytes: el stall, la traduccion (vtr), la lectura (rdc), el tope de la lista, recLine y lo/hi se miran una
// vez por linea; dentro de la linea se sigue mientras el recorrido sea seguido (sin saltos) y el bufer de R0 sea el mismo
// (memoTry lo puede reemplazar al comparar una sublista).
static int __attribute__((noinline)) walkList(List *l) {
  uint32_t lpc = 1, lra = 0;  // linea actual (direccion del juego) y donde se lee (la VRAM: en el espejo del frame)
  for (int budget = 8000; budget > 0;) {  // ~3 ms: despues se vuelve al bucle (eventos y pausa)
    if (l->stall && l->pc == l->stall) return 0;
    uint32_t pc = l->pc, ra;  // (ra: donde se lee; la VRAM, en el espejo)
    if ((pc & ~63u) == lpc) ra = lra | (pc & 63);
    else if ((ra = vtr(pc))) { lpc = pc & ~63u; lra = ra & ~63u; }
    if (!ra) {
      if (!isVram(pc)) { acc.bad++; return 1; }
      want(pc, 4, 1);
      if (!l->sp) { acc.lost++; return 1; }
      acc.v_cut++; UNWIND(); continue;  // sublista que sigue en una pagina sin copia: se vuelve al nivel de arriba
    }
    uint32_t op = rdc(&R0, ra, l->ord), ba = R0.a, bn = R0.n, i = (ra - ba) >> 2;
    uint32_t k = ra - ba < bn ? (64 - (pc & 63)) >> 2 : 1;  // comandos seguidos que quedan en la linea (1: palabra suelta)
    if (l->stall && l->stall - pc < k * 4) k = (l->stall - pc) >> 2;  // (el stall dentro de la linea)
    if (l->n + k > LIST_MAX) { acc.aborted++; return 1; }
    if (rec && (pc & ~63u) != rec_last) recLine(pc & ~63u, ra & ~63u);
    uint32_t sp0 = l->sp, top = !sp0 && !isVram(pc);  // (v13.9.2) memoria del nivel de arriba
    if (top && pc < l->lo) l->lo = pc;
    for (;;) {
    uint32_t c = op >> 24, d = op & 0xFFFFFF, prev = l->prev;
    if (top && pc > l->hi) l->hi = pc;
    l->n++; l->pc = pc + 4; l->prev = op; acc.cmds++; f_cmds++; budget--;
#define TOUCH(c) if (rec) rec_touch[(c) >> 5] |= 1u << ((c) & 31)
    switch (c) {
    case 0x00: break;
    case 0x01: vaddr = rel(l, d); vset = 1; if (rec) rec->flags |= M_VABS; break;
    case 0x02: iaddr = rel(l, d); if (rec) rec->flags |= M_IABS; break;
    case 0x04: draw(d); break;
    case 0x05: case 0x06: acc.curves++; acc.draws++; f_draws++; dirty = 0; last_prim = 99; if (rec) recDraw(0, 0); break;
    case 0x08: case 0x0A: {
      uint32_t t = rel(l, d & 0xFFFFFC);
      if (!vtr(t)) {  // en la VRAM sin copia en el espejo (todavia) o invalido
        if (!isVram(t)) { acc.bad++; return 1; }
        want(t, 4096, 1);  // su pagina y la siguiente: desde el frame siguiente se recorre desde el espejo
        if (c == 0x0A) { acc.vcalls++; break; }  // se salta
        acc.vjumps++;
        if (!l->sp) { acc.lost++; return 1; }
        acc.v_cut++; UNWIND(); break;
      }
      if (c == 0x0A && isVram(t)) acc.v_ok++;
      if (c == 0x0A) {
        if (l->sp >= 32) { acc.bad++; return 1; }
        acc.calls_ram++;
        if (!l->sp && l->nsub < 16 && !(l->ncall & 7) && !isVram(t)) l->subs[l->nsub++] = t;  // (v13.9.2) muestra para la vida de la lista
        if (!l->sp) claimSet(l->ncall++, l->ord);
        if (!l->sp && memo_on && !rec && !chk) {  // sublista del nivel de arriba: memoria (v12.12)
          uint32_t tq = nivel >= 2 ? now() : 0;
          int r = memoTry(l, t);
          if (tq) tm_memo += now() - tq;
          if (r > 0) break;  // aplicada: se sigue despues del CALL
          if (r == 0) recBegin(l, t);
        }
        l->stk[l->sp][0] = l->pc; l->stk[l->sp++][1] = l->off;
      }
      l->pc = t;
      break;
    }
    case 0x09: acc.bjumps++; break;  // salto condicional por bounding box: se supone que no salta
    case 0x0B: if (l->sp) { l->sp--; l->pc = l->stk[l->sp][0]; l->off = l->stk[l->sp][1]; if (!l->sp) { if (rec) recEnd(l); else if (chk) chkEnd(); } } break;
    case 0x0C:  // END: termina la lista, salvo despues de un SIGNAL (el juego la continua)
      if ((prev >> 24) == 0x0E) { acc.signals++; break; }
      return 1;
    case 0x0E: case 0x0F: break;
    case 0x10: reg[0x10] = d; TOUCH(0x10); break;
    case 0x13: l->off = d << 8; break;
    case 0x14: l->off = pc; break;
    case 0x2A: case 0x3A: case 0x3C: case 0x3E: case 0x40: {
      int m = c == 0x2A ? 4 : (c - 0x3A) / 2;
      mi[m] = d & 0x7F; if (rec) rec->mmask |= 1 << m;
      break;
    }
    case 0x2B: case 0x3B: case 0x3D: case 0x3F: case 0x41: {
      int m = c == 0x2B ? 4 : (c - 0x3B) / 2, max = m == 4 ? 96 : m == 2 ? 16 : 12;
      if (rec) rec->mmask |= 1 << m;
      if (mi[m] < (uint32_t)max) { if (mtx[m][mi[m]] != d) { mtx[m][mi[m]] = d; dirty |= C_MTX; } mi[m]++; }
      break;
    }
    case 0xC4: {  // carga de paleta: cambio solo si es otra paleta u otro tamano
      if (rec) { recRead(0xB0); recRead(0xB1); }
      uint32_t ca = CLUTADDR();
      acc.cluts++; if (isVram(ca)) { acc.clut_vram++; want(ca, (d & 0x3F) * 32, 0); } else smpAdd(ca, (d & 0x3F) * 32, 3); if (rec) rec->flags |= M_CLUT;
      if (ca != last_clut[0] || d != last_clut[1]) { last_clut[0] = ca; last_clut[1] = d; dirty |= C_TEX; }
      break;
    }
    case 0xEA: {
      for (uint32_t k = 0xB2; k <= 0xB5 && rec; k++) recRead(k);
      acc.transfers++;
      uint32_t s = (reg[0xB2] & 0xFFFFF0) | ((reg[0xB3] << 8) & 0x0F000000), t = (reg[0xB4] & 0xFFFFF0) | ((reg[0xB5] << 8) & 0x0F000000);
      if (isVram(s) || isVram(t)) acc.tr_vram++;
      uint32_t bp = d & 1 ? 4 : 2, w = (reg[0xEE] & 0x3FF) + 1, h = (reg[0xEE] >> 10 & 0x3FF) + 1, sw = reg[0xB3] & 0x7FF, dw = reg[0xB5] & 0x7FF;
      if (isVram(t)) {  // (v13.6) destino en la VRAM: lo tendra la Vita (repite la copia); el origen en la VRAM, del espejo
        markRt(t + ((reg[0xEC] >> 10 & 0x3FF) * dw + (reg[0xEC] & 0x3FF)) * bp, ((h - 1) * dw + w) * bp);
        if (isVram(s)) want(s + ((reg[0xEB] >> 10 & 0x3FF) * sw + (reg[0xEB] & 0x3FF)) * bp, ((h - 1) * sw + w) * bp, 0);
      }
      break;
    }
    default:
      if (cls[c] && reg[c] != d) { dirty |= cls[c]; if (cls[c] & (C_OTHER | C_TL)) acc.chg[c]++; }
      reg[c] = d; TOUCH(c);
    }
    if (--k == 0 || l->pc != pc + 4 || l->sp != sp0 || R0.a != ba || R0.n != bn) break;  // fin de la linea o salto
    pc += 4; op = R0.buf[++i];
    }
  }
  return 0;  // tope: se sigue en la proxima vuelta
}

static uint32_t capt;  // numero de orden de la ultima lista capturada

// --- Atrasos (v13.9) ---
// Cada sync en que la Vita iba atrasada (la lista que el juego espera termino de capturarse despues de que el juego llego
// al sync) va a nativedraw_tarde.log con de donde salio la demora: cuando el juego encolo la lista y cuando la termino de
// escribir (ultimo stall), cuando la Vita empezo a recorrerla, cuanto tardo y cuanto de eso trabajo. Las horas de la Vita
// se pasan a horas del PSP con off: la diferencia minima (en el ultimo segundo) entre la hora de la Vita al leer un evento
// y la que le puso el PSP (lo que tarda un evento en llegar con el capturador libre, decenas de us).
typedef struct { uint32_t ord, qid, cmds, t_enq, t_stall, t_start, t_end, work, own, hit, memo, ahead, cpu; } Done;
static Done done[64];
static uint32_t off, off_min = ~0u, off_ok;
static struct { uint32_t ord, t0, w, kind; } pend[16];
static uint32_t npend;
static inline uint32_t v2p(uint32_t t) { return t - off; }  // hora de la Vita -> hora del PSP
static void lateRec(uint32_t t0, uint32_t w, uint32_t kind, Done *d) {
  int late = (int)(v2p(d->t_end) - t0);
  if (late <= 0 && !w) return;  // llego a tiempo
  acc.pf_late++; if (late > 0) acc.pf_late_us += late;
  rput(1, "%u,%c,%u,%u,%d,%u,%u,%d,%d,%d,%d,%u,%u,%u,%u,%u\n", now() / 1000, kind, w, d->ord, late, d->cmds, d->ahead, (int)(t0 - d->t_enq),
       (int)(t0 - d->t_stall), (int)(v2p(d->t_start) - d->t_enq), (int)(d->t_end - d->t_start), d->work, d->own, d->hit, d->memo, d->cpu);
}
static void syncSeen(uint32_t ord, uint32_t t0, uint32_t w, uint32_t kind) {  // el juego llamo al sync esperando la lista ord
  if (nivel < 2 || !ord || sin_captura || !off_ok) return;
  if ((int)(capt - ord) >= 0) { if (done[ord & 63].ord == ord) lateRec(t0, w, kind, &done[ord & 63]); return; }
  if (npend < 16) { pend[npend].ord = ord; pend[npend].t0 = t0; pend[npend].w = w; pend[npend++].kind = kind; }
}
// (v13.9.2) Vida de la lista: hoy la compuerta hace esperar al juego hasta que la Vita leyo la lista del frame. Si el
// juego no reescribe esa memoria durante el frame siguiente (listas en dos bufferes), la Vita podria tener un frame mas.
// Muestra: 16 lineas repartidas en la memoria del nivel de arriba de la lista mas grande del frame y la primera linea de
// hasta 16 de sus sublistas (1 de cada 8 CALL); huella al terminar de capturarla y otra vez en el flip de su frame y en el
// siguiente (si no cambio en el siguiente, la Vita podria leerla hasta el sync del frame que viene).
typedef struct { uint32_t a[32], h[32], n, ntop, g, st, sq, ord, seen; } Life;
static Life life[4];
static uint32_t lf_ord_cmds, lf_n[2], lf_top[2], lf_sub[2];  // (acumulados) muestras revisadas 1 y 2 flips despues, con cambios
static uint32_t lineHash(uint32_t a) {
  uint32_t b[16] __attribute__((aligned(16))), h = 2166136261u;
  neon64(b, a);
  for (int i = 0; i < 16; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}
static void lifeTake(List *l) {  // al terminar de capturar la lista mas grande del frame
  Life *x = &life[gen & 3];
  if (x->g == gen && l->n < lf_ord_cmds) return;
  lf_ord_cmds = l->n;
  x->n = x->ntop = 0; x->g = gen; x->st = 0;
  if (l->lo < l->hi && ramEnd(l->lo) >= l->hi + 64)
    for (int k = 0; k < 16; k++) x->a[x->n++] = (l->lo + (l->hi - l->lo) / 15 * k) & ~63u;
  x->ntop = x->n;
  for (uint32_t k = 0; k < l->nsub; k++) if (ramEnd(l->subs[k]) >= (l->subs[k] & ~63u) + 64) x->a[x->n++] = l->subs[k] & ~63u;
  for (uint32_t k = 0; k < x->n; k++) x->h[k] = lineHash(x->a[k]);
  if (SB[1] >= 5) {  // (v13.9.3) la muestra para NativeLink dev65.2: la revisa antes de cada encolado y en cada sync (EV_VIDA)
    static uint32_t vseq;
    SB[0x50] = 0; __sync_synchronize();
    for (uint32_t k = 0; k < x->n; k++) { SB[0x60 + k] = x->a[k]; SB[0x80 + k] = x->h[k]; }
    SB[0x51] = x->n; SB[0x52] = x->ntop; __sync_synchronize();
    x->sq = ++vseq; x->ord = l->ord; x->seen = 0;
    SB[0x53] = l->ord; __sync_synchronize();  // (v13.10) de que lista es la muestra
    SB[0x50] = x->sq;
  }
}
// (v13.9.3) respuestas de NativeLink: por tipo (0 antes de encolar, 1 drawsync, 2 listsync) y por cuantas listas se encolaron
// despues de la de la muestra (0, 1, 2; el chequeo del encolado es antes de encolar: 0 = antes de encolar la siguiente): revisadas y con cambios en el nivel de arriba / en las sublistas (la primera de cada una)
static uint32_t vd[3][3][3];
static struct { uint32_t n, sum, max, t_sync; } gap;  // del sync al encolado siguiente (hora del PSP)
static void vidaEv(uint32_t kind, uint32_t sq, uint32_t b, uint32_t ord) {
  for (int j = 0; j < 4; j++) {
    Life *x = &life[j];
    if (x->sq != sq || kind > 2) continue;
    uint32_t d = ord - x->ord, bit = 1u << (kind * 3 + d);
    if (d > 2 || x->seen & bit) return;
    x->seen |= bit;
    vd[kind][d][0]++; vd[kind][d][1] += (b & 0xFF) != 0; vd[kind][d][2] += (b >> 8) != 0;
    return;
  }
}
static void lifeFlip(void) {  // en cada flip (antes de gen++): las muestras de 1 y 2 frames atras
  for (int j = 0; j < 4; j++) {
    Life *x = &life[j];
    uint32_t df = gen - x->g;
    if (!x->n || (df != 0 && df != 1) || x->st > df) continue;  // (df 0: el flip que cierra su frame = "1 frame despues" del sync)
    int top = 0, sub = 0;
    for (uint32_t k = 0; k < x->n; k++) if (lineHash(x->a[k]) != x->h[k]) { if (k < x->ntop) top = 1; else sub = 1; }
    lf_n[df]++; lf_top[df] += top; lf_sub[df] += sub; x->st = df + 1;
  }
}
static void listDone(List *l) {
  Done *d = &done[l->ord & 63];
  d->ord = l->ord; d->qid = l->qid; d->cmds = l->n; d->t_enq = l->t_enq; d->t_stall = l->t_stall; d->t_start = l->t_start;
  d->t_end = now(); d->work = l->work; d->own = R0.own - l->own0; d->hit = R0.hits - l->hit0; d->ahead = l->ahead;
  d->memo = acc.memo_hit >= l->memo0 ? acc.memo_hit - l->memo0 : acc.memo_hit; d->cpu = nivel >= 2 ? runClk(cap_uid) - l->cpu0 : 0;
  for (uint32_t i = 0; i < npend;)
    if ((int)(pend[i].ord - l->ord) <= 0) {
      if (pend[i].ord == l->ord) lateRec(pend[i].t0, pend[i].w, pend[i].kind, d);
      pend[i] = pend[--npend];
    } else i++;
}
static uint32_t lastOrdOf(uint32_t qid) {  // la ultima lista encolada con ese qid (lo que espera sceGeListSync)
  uint32_t o = 0;
  for (int i = 0; i < 64; i++) if (done[i].qid == qid && (int)(done[i].ord - o) > 0) o = done[i].ord;
  for (uint32_t i = qh; i != qt; i++) if (q[i & 63].qid == qid && (int)(q[i & 63].ord - o) > 0) o = q[i & 63].ord;
  return o;
}

static void pubList(List *l) {  // lista nueva para los lectores
  pub_stall = l->stall; pub_pc = l->pc; pub_base = reg[0x10];
  if (esp_on) {  // (v13.6.3) la ultima copia terminada del espejo, para toda la lista (y para el GE: no pisarla)
    uint32_t d = SB[0x40]; ES.listas++; if (d != SB[0x44]) ES.ant++;
    esp_u = d; SB[0x48] = d;
  }
  pv_plan = pv[pv_c]; pv_pn = pv_n[pv_c]; acc.pv_plan += pv_pn; pv_c ^= 1; pv_n[pv_c] = 0; pv_pos = 0; pv_i = 0;  // (v13.12) lo aplicado en la anterior
  __sync_synchronize();
  pub_ord = l->ord;
}
static void walk(void) {
  if (sin_captura) { while (qh != qt) { capt = q[qh & 63].ord; qh++; acc.lists++; } SB[3] = capt; return; }  // (v13.9)
  while (qh != qt) {
    List *l = &q[qh & 63];
    R0.n = 0;  // lo que se leyo despues del stall anterior puede haber cambiado
    if (l->ord != pub_ord) { pubList(l); recAbort(); chk = NULL; }  // los lectores empiezan esta lista (y lo que quedo a medias de la anterior)
    if (!l->t_start) { l->t_start = now() | 1; l->own0 = R0.own; l->hit0 = R0.hits; l->memo0 = acc.memo_hit; l->cpu0 = nivel >= 2 ? runClk(cap_uid) : 0; }
    uint32_t tw = now();
    int fin = walkList(l);
    l->work += now() - tw;
    if (!fin) return;
    recAbort(); chk = NULL;  // la lista termino dentro de una sublista (END, salto a la VRAM...): no se guarda
    lifeTake(l);  // (v13.10: en todos los niveles, para la autocomprobacion de NativeLink dev66; v13.10.3: antes de SB[3], asi NativeLink nunca ve la lista capturada con la muestra de la anterior)
    capt = l->ord; SB[3] = capt; qh++; acc.lists++;
    listDone(l);
  }
}

static void resetQueue(void) {
  qh = qt = 0;
  capt = SB[6]; SB[3] = capt;  // lo que se perdio se da por capturado
}

static uint32_t tail, beat, sec_t0;
static uint32_t pfr[8], lc, s_own, s_hit, s_memo, s_pre, s_late;  // (v13.9) EV_FRAME del frame en curso; ultimos valores de la linea por frame
// v12.10: tiempo de procesador de cada hilo (runClocks, us) para separar "nucleo compartido" de "trabajo por comando"
#define EMU_N 256
extern SceUID emu_th[EMU_N]; extern int emu_aff[EMU_N], emu_n;  // hilos del emulador (nativedraw.c, gancho de CreateThread)
static SceUID cap_uid = -1, lec_uid[2] = { -1, -1 };
static uint32_t runClk(SceUID t) {
  SceKernelThreadInfo in; sceClibMemset(&in, 0, sizeof in); in.size = sizeof in;
  if (t < 0 || sceKernelGetThreadInfo(t, &in) < 0) return 0;
  return (uint32_t)in.runClocks;
}
static void cpuSample(void) {  // diferencias desde el segundo anterior
  static uint32_t lc, ll[2], le[EMU_N];
  uint32_t c = runClk(cap_uid);
  acc.cpu_cap = c - lc; lc = c;
  for (int k = 0; k < 2; k++) { c = runClk(lec_uid[k]); acc.cpu_lec[k] = c - ll[k]; ll[k] = c; }
  for (int i = 0; i < emu_n && i < EMU_N; i++) {
    c = runClk(emu_th[i]);  // (0: el hilo ya no existe)
    int a = emu_aff[i], k = a == 0x10000 ? 0 : a == 0x20000 ? 1 : a == 0x40000 ? 2 : 3;
    if (c && le[i] && c >= le[i]) acc.cpu_core[k] += c - le[i];
    le[i] = c;
  }
}
static uint32_t sync_ord_v;  // (v13.10) la ultima lista que paso por un sync (la que espera la compuerta antes de encolar)
static void event(volatile uint32_t *e) {
  uint32_t t = e[0] & 0xFF, a = e[1], b = e[2], c = e[3], d0 = now() - e[4];
  acc.events++;
  if (d0 < off_min) off_min = d0;  // (v13.9) diferencia de relojes Vita - PSP
  if (!off_ok) { off = d0; off_ok = 1; }
  if (t == EV_ENQ || t == EV_HEAD) {
    if (qt - qh >= 64) { acc.resyncs++; resetQueue(); }
    if (t == EV_HEAD) qh--;
    List *l = &q[(t == EV_HEAD ? qh : qt++) & 63];
    sceClibMemset(l, 0, sizeof *l);
    l->pc = a & 0x0FFFFFFC; l->stall = b & 0x0FFFFFFF; l->qid = c; l->ord = e[5];
    l->t_enq = l->t_stall = e[4]; l->t_seen = now(); l->ahead = qt - qh - 1; l->lo = ~0u;
    uint32_t w = e[0] >> 8 & ~1u;  // (v13.10) NativeLink dev66: lo que espero el juego antes de encolar esta lista
    if (e[0] >> 8) { acc.eg_n++; acc.eg_us += w; syncSeen(sync_ord_v, e[4] - w, w, 'e'); }
    if (gap.t_sync) { uint32_t g = e[4] - gap.t_sync; if (g < 100000) { gap.n++; gap.sum += g; if (g > gap.max) gap.max = g; } gap.t_sync = 0; }
  } else if (t == EV_STALL) {
    acc.stalls++;
    for (uint32_t i = qt; i != qh; i--)
      if (q[(i - 1) & 63].qid == a) {
        q[(i - 1) & 63].stall = b & 0x0FFFFFFF; q[(i - 1) & 63].t_stall = e[4];
        if (q[(i - 1) & 63].ord == pub_ord) pub_stall = b & 0x0FFFFFFF;
        break;
      }
  } else if (t == EV_DSYNC) {
    syncSeen(c, e[4] - (a & ~1u), a & ~1u, 'd'); sync_ord_v = c;
    gap.t_sync = e[4];  // (v13.9.3) despues de la compuerta; la espera al GE va despues  // (v13.9) c: la ultima lista encolada; e[4]: despues de la compuerta
    acc.syncs++;
    if (a) { acc.syncs_waited++; acc.sync_wait_us += a & ~1u; }
    if (qh != qt) acc.behind++;
  } else if (t == EV_LSYNC) {
    syncSeen(lastOrdOf(a), e[4] - (b & ~1u), b & ~1u, 'l'); sync_ord_v = lastOrdOf(a);
    acc.lsyncs++;
  } else if (t == EV_GEFIN) {  // T6: tramo ocupado del GE del PSP (a = inicio, hora del evento = fin)
    uint32_t dt = e[4] - a;
    acc.ge_us += dt; acc.ge_runs++; acc.ge_stall += b; acc.ge_n += c;
    if (dt > acc.ge_max) acc.ge_max = dt;
  } else if (t == EV_SYNCEND) {  // T6: cuanto espero el juego al GE en el sync (a us), si el GE estaba ocupado (b)
    acc.sy_n++; acc.sy_us += a; if (a > 200) acc.sy_wait++; if (b == 1) acc.sy_busy++;
  } else if (t == EV_CPU) {  // T4: escritura de la CPU (dato: tipo)
    uint32_t k = e[0] >> 8 & 0xFF, lo = a & 0x0FFFFFFF, n = k >= 5 ? c : b;
    if (k == 3 || k == 4) { acc.dc_all++; cw_all_t = now(); }
    else {
      if (isVram(lo)) acc.cpu_vram++;
      uint32_t i = cw_n % CW_N;
      cw[i].lo = lo; cw[i].hi = lo + n; cw[i].t = now(); __sync_synchronize(); cw_n++;
    }
  } else if (t == EV_CPUSUM) {  // T4: totales del frame
    acc.dc_n += b; acc.dc_kb += c; acc.dm_n += e[3] & 0xFFFF; acc.dm_kb += e[3] >> 16; acc.cpu_drop += e[0] >> 8;
  } else if (t == EV_VIDA) {  // (v13.9.3)
    vidaEv(e[0] >> 8 & 0xFF, a, b, c);
  } else if (t == EV_FRAME) {  // (v13.9, NativeLink dev65) lo que le costo el frame al juego
    uint32_t x[7] = { a, b, e[0] >> 8 & 0xFF, e[0] >> 16 & 0xFF, c, e[5], e[7] };
    sceClibMemcpy(pfr, x, sizeof x); pfr[7] = e[6];
    acc.pf_n++; acc.pf_us += a; acc.pf_gate += b; acc.pf_gate_n += x[2]; acc.pf_nowait += x[3]; acc.pf_sync += c; acc.pf_spin += e[5];
    acc.pf_esp += e[6] & 0xFFFF; acc.pf_pg += e[6] >> 16; acc.pf_post += e[7];
  } else if (t == EV_FLIP) {
    uint32_t t0 = now(), c0 = acc.cmds;  // lo encolado antes del flip se recorre antes de cerrar el frame
    walk();
    if (acc.cmds != c0) { uint32_t dt = now() - t0; acc.us += dt; f_us += dt; }
    if (nivel >= 2) {  // (v13.9) una linea por frame en nativedraw_frames.log (extremo)
      uint32_t cc = runClk(cap_uid), cal = SB[0x4D], fus = pfr[0] ? pfr[0] : 1;
      uint32_t lib = cal ? (uint32_t)((unsigned long long)pfr[5] * 1000000 / cal / fus) : 0;  // CPU libre del PSP, por mil
      rput(0, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%08x,%u,%u,%u\n", now() / 1000, pfr[0], pfr[1], pfr[2], pfr[3], pfr[4], lib,
           pfr[7] & 0xFFFF, pfr[7] >> 16, pfr[6], f_cmds, f_draws, f_us, cc - lc, acc.memo_hit - s_memo, acc.memo_pre - s_pre,
           R0.own - s_own, R0.hits - s_hit, qt - qh, acc.pf_late - s_late, life[gen & 3].g == gen ? life[gen & 3].a[0] : 0,
           life[gen & 3].g == gen && life[gen & 3].ntop ? (life[gen & 3].a[15] - life[gen & 3].a[0]) >> 10 : 0, tm_memo, tm_apply);
      tm_memo = tm_apply = 0;
      lc = cc; s_own = R0.own; s_hit = R0.hits; s_memo = acc.memo_hit; s_pre = acc.memo_pre; s_late = acc.pf_late;
    }
    sceClibMemset(pfr, 0, sizeof pfr);
    acc.frames++;
    if (qh != qt) acc.flips_behind++;
    if (f_cmds > acc.cmds_max) acc.cmds_max = f_cmds;
    if (f_draws > acc.draws_max) acc.draws_max = f_draws;
    if (f_us > acc.us_max) acc.us_max = f_us;
    f_cmds = f_draws = f_us = 0;
    if (nivel >= 2) lifeFlip();
    lf_ord_cmds = 0;
    gen++; ntex = 0;
    for (int k = 0; k < 4; k++) smp_f[k] = 0;
    ltFlip();
    uint32_t n = now();
    if (n - sec_t0 >= 1000000) {  // cada segundo: a la linea del log
      static uint32_t last[8];  // contadores de lecturas y de los lectores (acumulados) al segundo anterior
      uint32_t cur[8] = { R0.own, R0.hits, RR[0].own, RR[1].own, lv_lines[0] + lv_lines[1], lv_ok[0] + lv_ok[1], lv_bad[0] + lv_bad[1], lv_late[0] + lv_late[1] };
      acc.own = cur[0] - last[0]; acc.hits = cur[1] - last[1]; acc.rown[0] = cur[2] - last[2]; acc.rown[1] = cur[3] - last[3];
      acc.lv_lines = cur[4] - last[4]; acc.lv_ok = cur[5] - last[5]; acc.lv_bad = cur[6] - last[6]; acc.lv_late = cur[7] - last[7];
      for (int k = 0; k < 8; k++) last[k] = cur[k];
      static uint32_t lp[3];  // (v13.12) adelanto
      uint32_t cp[3] = { pv_ok[0] + pv_ok[1], pv_bad[0] + pv_bad[1], pv_late[0] + pv_late[1] };
      acc.pv_ok = cp[0] - lp[0]; acc.pv_bad = cp[1] - lp[1]; acc.pv_late = cp[2] - lp[2];
      for (int k = 0; k < 3; k++) lp[k] = cp[k];
      acc.t0 = n - sec_t0;
      if (nivel) cpuSample();
      if (off_min != ~0u) { off = off_min; off_min = ~0u; }  // (v13.9) reloj: el minimo del ultimo segundo
      if (!rep_ready) { rep = acc; rep_ready = 1; }
      if (rec) { uint32_t *a = (uint32_t *)&acc; for (int i = 0; i < SW; i++) rec_acc0[i] -= a[i]; }  // (v12.13) grabando: que la diferencia siga valiendo
      sceClibMemset(&acc, 0, sizeof acc);
      s_memo = s_pre = s_late = 0;  // (v13.9: acc empieza de nuevo)
      sec_t0 = n;
    }
  }
}

static int capThread(SceSize args, void *argp) {
  uint32_t idle_t = 0, on = 0;
  for (;;) {
    if (SB[0] != SB_MAGIC) {  // sin NativeLink en modo sombra
      if (on) { clog("buzon de NativeLink desaparecio: captura en pausa\n"); on = 0; esp_on = 0; }
      sceKernelDelayThread(50000);
      continue;
    }
    if (!on) {
      on = 1; tail = SB[2]; resetQueue(); sec_t0 = now(); sceClibMemset(&acc, 0, sizeof acc);
      for (int w = 0; w < 16; w++) want_l[w] = want_d[w] = rt_pg[w] = esp_ok[0][w] = esp_ok[1][w] = 0;
      esp_u = 0; esp_reset = 1; esp_on = SB[1] >= 4;
      clog("buzon de NativeLink (modo sombra) encontrado: eventos=%u listas=%u -> captura activa%s\n", tail, SB[6],
           esp_on ? ", con espejo de la VRAM" : " (NativeLink sin espejo doble de la VRAM: hace falta dev64)");
    }
    SB[4] = ++beat;
    uint32_t h = SB[2], work = 0;
    if ((int)(h - tail) < 0 || h - tail > SB_N) {  // se perdieron eventos (o NativeLink reinicio el buzon)
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
    else if (now() - idle_t > 300) { uint32_t s0 = now(); sceKernelDelayThread(100); acc.sleep_us += now() - s0; }  // sin trabajo hace 0,3 ms: dormir un poco (~0,4 ms)
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
// (v13.9) nativedraw_frames.log y nativedraw_tarde.log: anillos de un solo escritor (el capturador); el hilo del log los vacia
static char rb0[65536], rb1[16384];
static struct { char *b; uint32_t n; volatile uint32_t w, r; const char *path; } RG[2] = {
  { rb0, sizeof rb0, 0, 0, "ux0:pspemu/nativedraw_frames.log" }, { rb1, sizeof rb1, 0, 0, "ux0:pspemu/nativedraw_tarde.log" } };
static void rput(int k, const char *fmt, ...) {
  char b[384];
  va_list ap; va_start(ap, fmt);
  int n = sceClibVsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  if (n > (int)sizeof b - 1) n = sizeof b - 1;
  if (n <= 0 || RG[k].w - RG[k].r + n > RG[k].n) return;  // (lleno: se pierde la linea)
  for (int i = 0; i < n; i++) RG[k].b[(RG[k].w + i) % RG[k].n] = b[i];
  __sync_synchronize();
  RG[k].w += n;
}
static void rflush(int k) {
  uint32_t w = RG[k].w, r = RG[k].r;
  if (w == r) return;
  SceUID fd = sceIoOpen(RG[k].path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) {
    uint32_t a = r % RG[k].n, n = w - r, m = RG[k].n - a < n ? RG[k].n - a : n;
    sceIoWrite(fd, RG[k].b + a, m);
    if (n > m) sceIoWrite(fd, RG[k].b, n - m);
    sceIoClose(fd);
  }
  RG[k].r = w;
}
#define PF(x) (r->x / f)
static int repThread(SceSize args, void *argp) {
  uint32_t last_write = 0;
  for (;;) {
    sceKernelDelayThread(200000);
    static uint32_t nrep;
    if (rep_ready && !nivel && ++nrep % 10) rep_ready = 0;  // (v13.9.5) minimo: 1 de cada 10 segundos
    if (rep_ready) {
      Stats *r = &rep;
      uint32_t f = r->frames ? r->frames : 1, d = r->draws ? r->draws : 1;
      clog("seg: frames=%u listas/f=%u comandos/f=%u (max %u) draws/f=%u (max %u) | ARM us/f=%u (max %u) ns/comando=%u | lecturas/f propias=%u de_lectores=%u (lectores leyeron %u+%u) eventos/f=%u\n",
           r->frames, PF(lists), PF(cmds), r->cmds_max, PF(draws), r->draws_max, PF(us), r->us_max, r->cmds ? (uint32_t)((unsigned long long)r->us * 1000 / r->cmds) : 0, PF(own), PF(hits), PF(rown[0]), PF(rown[1]), PF(events));
      if (!nivel) goto sync;  // (minimo: solo esta linea y la de vram y sync)
      clog("  draws/f: 3d=%u 2d=%u clear=%u vacios=%u | tri=%u tira=%u abanico=%u rect=%u lineas=%u puntos=%u curvas=%u | vertices/f=%u\n",
           PF(d3d), PF(dthr), PF(dclear), PF(dempty), PF(prim[3]), PF(prim[4]), PF(prim[5]), PF(prim[6]), PF(prim[1]) + PF(prim[2]),
           PF(prim[0]), PF(curves), PF(verts));
      clog("  estado (draws/f): textura_nueva=%u cambia_textura=%u matriz=%u destino=%u otro=%u sin_cambio=%u (%u%%) | lotes/f=%u contiguos/f=%u sin_cortar_por_texlevel/f=%u solo_texlevel=%u cambios_de_destino/f=%u\n",
           PF(texnew), PF(ch_tex), PF(ch_mtx), PF(ch_rt), PF(ch_other), PF(ch_none), r->ch_none * 100 / d, PF(batches), PF(batches_cont),
           PF(batches_notl), PF(only_tl), PF(rt_switch));
      uint32_t awake = r->t0 > r->sleep_us ? r->t0 - r->sleep_us : 1;  // tiempo que el capturador no estuvo durmiendo
      clog("  procesador (ms por seg): captura=%u de %u despierta (%u%%) trabajando=%u | ns/comando: reloj=%u procesador_max=%u procesador_estimado=%u | lectores=%u+%u | emulador nucleo0=%u nucleo1=%u nucleo2=%u sin_fijo=%u\n",
           r->cpu_cap / 1000, awake / 1000, (uint32_t)((unsigned long long)r->cpu_cap * 100 / awake), r->us / 1000,
           r->cmds ? (uint32_t)((unsigned long long)r->us * 1000 / r->cmds) : 0,
           r->cmds ? (uint32_t)((unsigned long long)r->cpu_cap * 1000 / r->cmds) : 0,
           r->cmds ? (uint32_t)((unsigned long long)r->us * r->cpu_cap / awake * 1000 / r->cmds) : 0,
           r->cpu_lec[0] / 1000, r->cpu_lec[1] / 1000, r->cpu_core[0] / 1000, r->cpu_core[1] / 1000, r->cpu_core[2] / 1000,
           r->cpu_core[3] / 1000);
      clog("  memoria de sublistas (por frame): aplicadas=%u (comprobadas_por_lectores=%u) comandos_sin_recorrer=%u (%u%% de todos) lineas_comprobadas=%u comparar_us=%u | nuevas=%u cambiaron=%u otra_entrada=%u sin_guardar=%u | lectores/f: lineas=%u comprobadas=%u no_iguales=%u tarde=%u | adelanto/f: plan=%u comprobadas=%u no_iguales=%u tarde=%u | por seg: deposito_vaciado=%u revisadas=%u DISTINTAS=%u\n",
           PF(memo_hit), PF(memo_pre), PF(memo_cmds), r->cmds ? (uint32_t)((unsigned long long)r->memo_cmds * 100 / r->cmds) : 0, PF(memo_lines), PF(memo_us),
           PF(memo_miss), PF(memo_fail), PF(memo_dep), PF(memo_no), PF(lv_lines), PF(lv_ok), PF(lv_bad), PF(lv_late), PF(pv_plan), PF(pv_ok), PF(pv_bad), PF(pv_late),
           r->memo_reset, r->memo_chk, r->memo_bad);
      clog("  memoria/f: texturas=%u (en vram %u) draws_tex_vram=%u draws_vert_vram=%u | vertices=%u KB indices=%u KB | paletas=%u (vram %u) copias=%u (vram %u)\n",
           PF(textures), PF(tex_vram), PF(dtex_vram), PF(dvert_vram), PF(vbytes) >> 10, PF(ibytes) >> 10, PF(cluts), PF(clut_vram),
           PF(transfers), PF(tr_vram));
    sync:
      clog("  vram y sync (por seg): sublistas_vram=%u saltos_vram=%u listas_perdidas=%u abortadas=%u invalidas=%u signal=%u bjump=%u | drawsync=%u esperaron=%u (%u us c/u) atrasada_al_sync=%u atrasada_al_flip=%u listsync=%u reinicios=%u pausas=%u\n",
           r->vcalls, r->vjumps, r->lost, r->aborted, r->bad, r->signals, r->bjumps, r->syncs, r->syncs_waited,
           r->syncs_waited ? r->sync_wait_us / r->syncs_waited : 0, r->behind, r->flips_behind, r->lsyncs, r->resyncs, r->yields);
      if (!nivel) goto fin;
      {  // espejo: paginas pedidas / validas (ahora) y lo del ultimo segundo
        static uint32_t lc, lp, lb, lv, le[8];
        uint32_t pl = 0, pd = 0, po0 = 0, po1 = 0, pr = 0, c = SB[0x44], pg = SB[0x45], bz = SB[0x46], vu = SB[0x49];
        for (int i = 0; i < 512; i++) {
          pl += want_l[i >> 5] >> (i & 31) & 1; pd += want_d[i >> 5] >> (i & 31) & 1; pr += rt_pg[i >> 5] >> (i & 31) & 1;
          po0 += esp_ok[0][i >> 5] >> (i & 31) & 1; po1 += esp_ok[1][i >> 5] >> (i & 31) & 1;
        }
        uint32_t e[8] = { ES.chk, ES.nuevas, ES.ch_l, ES.ch_d, ES.us, ES.listas, ES.ant, 0 }, x[8];
        for (int i = 0; i < 8; i++) { x[i] = e[i] - le[i]; le[i] = e[i]; }
        if (esp_on) {
          clog("  espejo: paginas pedidas listas=%u datos=%u validas=%u/%u en_destinos=%u | por seg: copias=%u paginas_copiadas=%u (KB/f %u) ocupado=%u vita_usando=%u | sublistas_vram/f recorridas=%u cortadas=%u texturas_en_destino/f=%u\n",
               pl, pd, po0, po1, pr, c - lc, pg - lp, (pg - lp) * 4 / f, bz - lb, vu - lv, PF(v_ok), PF(v_cut), PF(tex_rt));
          if (nivel >= 2) clog("  espejo (por seg): listas=%u con_la_copia_anterior=%u | muestreo: revisadas=%u nuevas=%u cambiaron listas=%u datos=%u hilo_us=%u\n",
               x[5], x[6], x[0], x[1], x[2], x[3], x[4]);
        }
        lc = c; lp = pg; lb = bz; lv = vu;
      }
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
      if (SB[1] >= 3 && nivel >= 2) {
        clog("  T6 GE del PSP (por frame): ocupado=%u us en %u tramos (el mas largo %u us) en_stall=%u%% | sync/f=%u esperas_al_GE=%u (%u us c/u) GE_ocupado_al_llamar=%u | T4 CPU del juego (por frame): cache=%u (%u KB) dma=%u (%u KB) a_la_vram/seg=%u vaciar_todo/seg=%u sin_anotar/seg=%u\n",
             PF(ge_us), PF(ge_runs), r->ge_max, r->ge_n ? r->ge_stall * 100 / r->ge_n : 0, PF(sy_n), PF(sy_wait),
             r->sy_wait ? r->sy_us / r->sy_wait : 0, PF(sy_busy), PF(dc_n), PF(dc_kb), PF(dm_n), PF(dm_kb), r->cpu_vram, r->dc_all, r->cpu_drop);
        static uint32_t l[22];  // vida de los datos: lo del ultimo segundo (acumulados en LT)
        uint32_t c[22];
        for (int k = 0; k < 4; k++) { c[k] = LT.n[k]; c[4 + k] = LT.ch1[k]; c[8 + k] = LT.ch2[k]; c[12 + k] = LT.warn[k]; c[16 + k] = LT.wall[k]; }
        c[20] = LT.ms1; c[21] = LT.ms2;
        uint32_t x[22]; for (int k = 0; k < 22; k++) { x[k] = c[k] - l[k]; l[k] = c[k]; }
        uint32_t nn = x[0] + x[1] + x[2] + x[3] ? x[0] + x[1] + x[2] + x[3] : 1;
        clog("  vida de los datos (por seg; cambiaron 1 / 2 frames despues; aviso: rango / vaciar_todo): vertices=%u (%u / %u, aviso %u / %u) indices=%u (%u / %u, %u / %u) texturas=%u (%u / %u, %u / %u) paletas=%u (%u / %u, %u / %u) | revisadas a los %u / %u ms | total: vertices %u de %u, texturas %u de %u\n",
             x[0], x[4], x[8], x[12], x[16], x[1], x[5], x[9], x[13], x[17], x[2], x[6], x[10], x[14], x[18], x[3], x[7], x[11], x[15], x[19],
             x[20] / nn / 10, x[21] / nn / 10, LT.ch2[0], LT.n[0], LT.ch2[2], LT.n[2]);
      }
      if (r->pf_n) {  // (v13.9) lo que le costo cada frame al juego (NativeLink dev65) y los syncs en que la Vita iba atrasada
        uint32_t g = r->pf_n, cal = SB[0x4D], ms = r->pf_us / 1000 + 1;
        clog("  impacto en el PSP (por frame del juego): frames=%u frame_us=%u CPU_libre=%u%% compuerta_us=%u (esperas/seg=%u sin_esperar/seg=%u) sync_GE_us=%u ganchos_us=%u espejo_us=%u paginas=%u | Vita atrasada: syncs/seg=%u atraso_us=%u c/u | lectura propia: %u us (%u cronometradas) | linea de la cache compartida: %u ns (%u) | compuerta (v13.10): %s, esperas antes de encolar/seg=%u (%u us c/u)\n",
             g, r->pf_us / g, cal ? r->pf_spin / cal * 100 / ms : 0, r->pf_gate / g, r->pf_gate_n, r->pf_nowait, r->pf_sync / g, r->pf_post / g,
             r->pf_esp / g, r->pf_pg / g, r->pf_late, r->pf_late ? r->pf_late_us / r->pf_late : 0, r->rd_n ? r->rd_us / r->rd_n : 0, r->rd_n, r->hit_n ? r->hit_us * 1000 / r->hit_n : 0, r->hit_n,
             gp_flags & 8 ? "en el sync (compuerta_sync)" : SB[0x54] == 1 ? "antes de encolar" : SB[0x54] == 2 ? "en el sync (la lista cambio 10 veces)" : "en el sync (probando)",
             r->eg_n, r->eg_n ? r->eg_us / r->eg_n : 0);
      }
      if (nivel >= 2) {  // (v13.9.2) vida de la lista (acumulado desde el inicio)
        clog("  vida de la lista (acumulado): revisadas en el flip de su frame=%u (cambio el nivel de arriba %u, sublistas %u) | en el flip siguiente=%u (%u, %u)\n",
             lf_n[0], lf_top[0], lf_sub[0], lf_n[1], lf_top[1], lf_sub[1]);
        clog("  vida de la lista segun NativeLink (acumulado; revisadas, cambio arriba, cambio sublistas): drawsync de su frame %u %u %u | antes de encolar la siguiente %u %u %u | drawsync siguiente %u %u %u | antes de encolar la subsiguiente %u %u %u | listsync %u %u %u / %u %u %u | del drawsync al encolado siguiente: %u us (max %u)\n",
             vd[1][0][0], vd[1][0][1], vd[1][0][2], vd[0][0][0], vd[0][0][1], vd[0][0][2], vd[1][1][0], vd[1][1][1], vd[1][1][2],
             vd[0][1][0], vd[0][1][1], vd[0][1][2], vd[2][0][0], vd[2][0][1], vd[2][0][2], vd[2][1][0], vd[2][1][1], vd[2][1][2],
             gap.n ? gap.sum / gap.n : 0, gap.max);
        gap.n = gap.sum = gap.max = 0;
      }
    fin:
      rep_ready = 0;
    }
    static uint32_t rg_t;  // (v13.9) los anillos: cada 2 s o si van por la mitad
    if (RG[0].w - RG[0].r > RG[0].n / 2 || RG[1].w - RG[1].r > RG[1].n / 2 || now() - rg_t > 2000000) { rflush(0); rflush(1); rg_t = now(); }
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

// Hilo del espejo: marca como validas las paginas que el GE ya copio y cuenta las que cambiaron de una copia a otra
static uint32_t smpHash(uint32_t a, uint32_t n) {  // huella de [a, a + n) (n <= 256) leyendo las lineas que lo cubren
  static uint32_t buf[96] __attribute__((aligned(16)));
  uint32_t b = a & ~63u, e = (a + n + 63) & ~63u, h = 2166136261u;
  if (e > ramEnd(a)) e = ramEnd(a);
  for (uint32_t x = b; x < e; x += 64) neon64(buf + (x - b) / 4, x);
  const uint8_t *p = (const uint8_t *)buf + (a - b);
  for (uint32_t i = 0; i < n && a + i < e; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static int cwCovers(uint32_t a, uint32_t n, uint32_t t0) {  // escritura conocida de la CPU desde t0: 1 un rango, 2 vaciar todo
  uint32_t m = cw_n;
  for (uint32_t k = 0; k < CW_N && k < m; k++) {
    uint32_t i = (m - 1 - k) % CW_N;
    if ((int)(cw[i].t - t0) < 0) break;
    if (cw[i].lo < a + n && a < cw[i].hi) return 1;
  }
  return cw_all_t && (int)(cw_all_t - t0) >= 0 ? 2 : 0;
}
static void ltFlip(void) {  // (capturador, en cada flip) revisa las muestras de 1 y 2 frames atras
  if (nivel < 2) return;
  for (uint32_t i = smp_r; i != smp_w; i++) {
    Smp *x = &smp[i & (SMP_N - 1)];
    uint32_t df = gen - x->g;
    if (x->st == 0 && df >= 1) { x->ch1 = smpHash(x->a, x->n) != x->h0; x->st = 1; LT.ms1 += (now() - x->t0) / 100; }
    else if (x->st == 1 && df >= 2) {
      uint32_t ch2 = smpHash(x->a, x->n) != x->h0, k = x->kind, w;
      LT.n[k]++; LT.ch1[k] += x->ch1; LT.ch2[k] += ch2; LT.ms2 += (now() - x->t0) / 100;
      if ((x->ch1 || ch2) && (w = cwCovers(x->a, x->n, x->t0))) { if (w == 1) LT.warn[k]++; else LT.wall[k]++; }
      x->st = 2;
    }
  }
  while (smp_r != smp_w && smp[smp_r & (SMP_N - 1)].st == 2) smp_r++;
}
static uint32_t medir_ge;  // (v13.5.3) pedirle a NativeLink el hilo que mira el GE (T6): ux0:data/nativedraw/medir_ge
static int espThread(SceSize args, void *argp) {  // (v13.6) huella de 4 paginas pedidas cada 5 ms, por turnos
  static uint32_t seen[512], hs[512], buf[64] __attribute__((aligned(16)));
  uint32_t n = 0, rr = 0;
  for (;;) {
    sceKernelDelayThread(5000);
    if (SB[0] == SB_MAGIC && SB[1] >= 3 && SB[0x47] != medir_ge) SB[0x47] = medir_ge;
    if (SB[0] == SB_MAGIC && SB[1] >= 5 && SB[0x4A] != gp_flags) SB[0x4A] = gp_flags;  // (v13.9) interruptores para NativeLink
    if (SB[0] == SB_MAGIC && SB[1] >= 5 && SB[0x4E] != nivel) SB[0x4E] = nivel;         // (v13.9.5) nivel de registro
    if (esp_reset) { sceClibMemset(seen, 0, sizeof seen); sceClibMemset(hs, 0, sizeof hs); esp_reset = 0; }
    if (!esp_on) continue;
    uint32_t t0 = now(), done = SB[0x40];
    for (uint32_t k = 0, m = 0; k < 512 && m < 4 && nivel >= 2; k++) {  // (v13.9.5: solo en extremo)
      uint32_t p = rr = (rr + 1) & 511, w = p >> 5, b = 1u << (p & 31);
      if (!((want_l[w] | want_d[w]) & b)) continue;
      uint32_t s0 = CPY(0, p), s1 = CPY(1, p), sq = (int)(s1 - s0) > 0 && (int)(done - s1) >= 0 ? s1 : s0;  // la ultima terminada
      if (!sq || sq == seen[p] || (int)(done - sq) < 0) continue;
      seen[p] = sq; m++;
      uint32_t h = 2166136261u | 1, base = (sq & 1 ? MIRROR2 : MIRROR) + p * 4096;
      for (uint32_t o = 0; o < 4096; o += 256) { neon256(buf, base + o); for (int i = 0; i < 64; i++) h = (h ^ buf[i]) * 16777619u; }
      if (!hs[p]) ES.nuevas++;  // (primera vez: no cuenta como cambio)
      else if (h != hs[p]) { if (want_l[w] & b) ES.ch_l++; else ES.ch_d++; }
      hs[p] = h | 1; ES.chk++;
    }
    ES.us += now() - t0;
    if (++n % 20 == 0)  // (por si NativeLink reinicio el buzon sin que el capturador lo notara)
      for (int w = 0; w < 16; w++) { if (SB[0x20 + w] != want_l[w]) SB[0x20 + w] = want_l[w]; if (SB[0x30 + w] != want_d[w]) SB[0x30 + w] = want_d[w]; }
  }
  return 0;
}

// Lector: recorre la lista que publica el capturador solo para traer sus lineas a la cache compartida (sigue saltos,
// sublistas, BASE y OFFSET; no interpreta draws). Las sublistas del nivel de arriba se reparten entre los lectores.
// v13.4: las sublistas del nivel de arriba que ya estan en la memoria no se recorren: se comprueban (memoVerify).
// Cada CALL del nivel de arriba lo toma el primer lector que llega (claimTake), no uno si y uno no.
typedef struct { uint32_t ord, pc, off, base, sp, stk[32][2], prev, ncall, n, done; } Lec;
static void lecStart(Lec *s, Rd *r) {  // lista nueva (la que publico el capturador)
  s->ord = pub_ord;
  __sync_synchronize();
  s->pc = pub_pc; s->base = pub_base; s->off = s->sp = s->prev = s->ncall = s->n = 0; s->done = !s->ord; r->n = 0;
}
static void lecWalk(int id, Lec *s, int budget) {  // hasta budget comandos, el stall o el fin de la lista
  Rd *r = &RR[id];
  uint32_t st = pub_stall;
  r->n = 0;  // (el stall pudo moverse)
  for (int k = 0; k < budget && pub_ord == s->ord && !s->done; k++) {
    if (st && s->pc == st) break;
    uint32_t ra = vtr(s->pc);
    if (!ra && s->sp && isVram(s->pc)) { s->sp = 0; s->pc = s->stk[0][0]; s->off = s->stk[0][1]; continue; }  // (como el capturador)
    if (!ra || ++s->n > LIST_MAX) { s->done = 1; break; }
    uint32_t at = s->pc, op = rdc(r, ra, s->ord), c = op >> 24, d = op & 0xFFFFFF;
    s->pc += 4;
    if (c == 0x08 || c == 0x0A) {
      uint32_t t = ((((s->base & 0x0F0000) << 8) | (d & 0xFFFFFC)) + s->off) & 0x0FFFFFFF;
      if (!vtr(t)) { if (c == 0x08) s->done = 1; }  // VRAM sin copia: no se puede leer
      else if (c == 0x08) s->pc = t;
      else if (s->sp < 32 && (s->sp || claimTake(s->ncall++, s->ord, id))) {
        if (!s->sp && memo_on && memoVerify(id, r, t, s->base, s->off, s->ord)) ;  // comprobada: no hace falta recorrerla
        else { s->stk[s->sp][0] = s->pc; s->stk[s->sp++][1] = s->off; s->pc = t; }
      }
    } else if (c == 0x0B) { if (s->sp) { s->sp--; s->pc = s->stk[s->sp][0]; s->off = s->stk[s->sp][1]; } }
    else if (c == 0x0C) { if ((s->prev >> 24) != 0x0E) s->done = 1; }
    else if (c == 0x10) s->base = d;
    else if (c == 0x13) s->off = d << 8;
    else if (c == 0x14) s->off = at;
    s->prev = op;
  }
}
static int lectorThread(SceSize args, void *argp) {
  int id = *(int *)argp;
  Lec s; s.ord = 0; s.done = 1;
  for (;;) {
    if (pub_ord != s.ord) { lecStart(&s, &RR[id]); if (pv_on) pvRun(id, &RR[id], s.ord); }  // (v13.12) primero el plan
    uint32_t st = pub_stall;
    if (s.done || (st && s.pc == st)) { sceKernelDelayThread(100); continue; }
    lecWalk(id, &s, 2000);
  }
  return 0;
}

static int swOn(const char *p) {  // (v13.9) interruptor: el archivo existe y no empieza con "no" (el relevo no puede borrar)
  char b[2] = { 0, 0 };
  SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0);
  if (fd < 0) return 0;
  sceIoRead(fd, b, 2); sceIoClose(fd);
  return !(b[0] == 'n' && b[1] == 'o');
}
void capturaStart(void) {
  SceIoStat st;
  sceIoRemove("ux0:pspemu/nativedraw_captura_prev.log");
  sceIoRename(LOGC, "ux0:pspemu/nativedraw_captura_prev.log");
  SceUID r = sceKernelCreateThread("nativedraw_captura_log", repThread, 0xB0, 0x2000, 0, 0, NULL);  // primero: que
  if (r >= 0) sceKernelStartThread(r, 0, NULL);                                                      // quede el log
  {  // (v13.9.5) nivel de registro
    char b[8] = { 0 };
    SceUID fd = sceIoOpen("ux0:data/nativedraw/registro", SCE_O_RDONLY, 0);
    if (fd >= 0) { sceIoRead(fd, b, 7); sceIoClose(fd); nivel = b[0] == 'e' ? 2 : b[0] == 'm' && b[1] == 'i' ? 0 : 1; }
  }
  if (nivel >= 2) { uint32_t t0 = now(), x = 0; for (int i = 0; i < 10000; i++) x += now(); clog("(v13.9.4) costo de leer la hora: %u ns (%u)\n", (now() - t0) / 10, x & 1); }
  clog("NativeDraw " NATIVEDRAW_VERSION ": capturador de la etapa 2 (memoria de sublistas, espejo de la VRAM) con registros de la fase de analisis\n");
  static const char *rot[2][2] = { { "ux0:pspemu/nativedraw_frames.log", "ux0:pspemu/nativedraw_frames_prev.log" },
                                   { "ux0:pspemu/nativedraw_tarde.log", "ux0:pspemu/nativedraw_tarde_prev.log" } };
  for (int k = 0; k < 2; k++) { sceIoRemove(rot[k][1]); sceIoRename(rot[k][0], rot[k][1]); }
  if (nivel >= 2) rput(0, "t_ms,frame_us,compuerta_us,esperas,sin_esperar,sync_GE_us,cpu_libre_x1000,espejo_us,espejo_paginas,ganchos_us,"
          "comandos,draws,captura_us,captura_cpu_us,aplicadas,comprobadas_lectores,lecturas_propias,de_lectores,listas_pendientes,syncs_atrasados,"
          "lista_dir,lista_kb,memoria_us,aplicar_us\n");
  if (nivel >= 2) rput(1, "t_ms,tipo,espera_us,lista,atraso_us,comandos,listas_antes,encolada_hace_us,ultimo_stall_hace_us,vita_empezo_tras_us,"
          "vita_recorrio_us,trabajo_us,lecturas_propias,de_lectores,aplicadas,procesador_us\n");
  static const char *sw[4] = { "ux0:data/nativedraw/sin_compuerta", "ux0:data/nativedraw/sin_espejo", "ux0:data/nativedraw/sin_libre", "ux0:data/nativedraw/compuerta_sync" };
  for (int k = 0; k < 4; k++) if (swOn(sw[k])) gp_flags |= 1u << k;
  sin_captura = swOn("ux0:data/nativedraw/sin_captura");
  pv_on = !swOn("ux0:data/nativedraw/sin_adelanto");  // (v13.12)
  clog("registro: %s (ux0:data/nativedraw/registro: minimo, mediano o extremo) | interruptores%s%s%s%s%s%s%s\n", nivel == 2 ? "extremo" : nivel ? "mediano" : "minimo", gp_flags & 1 ? " sin_compuerta" : "", gp_flags & 2 ? " sin_espejo" : "",
       gp_flags & 4 ? " sin_libre" : "", gp_flags & 8 ? " compuerta_sync" : "", sin_captura ? " sin_captura" : "", pv_on ? "" : " sin_adelanto", gp_flags || sin_captura || !pv_on ? "" : ": ninguno (todo normal)");
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
  if (sceIoGetstat("ux0:data/nativedraw/sin_memoria", &st) >= 0) clog("memoria de sublistas: apagada (sin_memoria)\n");
  else {
    void *mm = NULL;
    uint32_t sz = 0;
    SceUID mb = -1;
    for (; MEMO_N >= 512 && !mm; MEMO_N /= 2, POOL_N /= 2, memo_sh++) {  // (si no alcanza la memoria: la mitad)
      sz = (MEMO_N * sizeof(Memo) + POOL_N * 64 + 0xFFF) & ~0xFFF;
      mb = sceKernelAllocMemBlock("nativedraw_memoria", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, sz, NULL);
      if (mb >= 0 && sceKernelGetMemBlockBase(mb, &mm) < 0) mm = NULL;
      if (mm) break;
    }
    if (!mm) clog("memoria de sublistas: sin memoria (%08x)\n", mb);
    else {
      sceClibMemset(mm, 0, MEMO_N * sizeof(Memo));
      memo = mm; pool = (uint32_t *)((char *)mm + MEMO_N * sizeof(Memo));
      memo_chk = sceIoGetstat("ux0:data/nativedraw/memo_revisar", &st) >= 0;
      __sync_synchronize();
      memo_on = 1;
      clog("memoria de sublistas: %u sublistas, deposito de %u KB (%u KB en total)%s\n", MEMO_N, POOL_N * 64 >> 10, sz >> 10,
           memo_chk ? "; REVISION de todas (memo_revisar)" : "; revision de 1 de cada 32");
    }
  }
  // v13.13: el capturador puede correr en cualquiera de los 3 nucleos (antes fijo en el 1, con los hilos de audio: en
  // Ridge Racer el audio usa ~30 % de ese nucleo y el capturador recibia ~65 %; ahora ~100 %). RR: 30 -> 60 fps en el
  // tramo del publico; GT: 58,2 -> 60 en lo mas pesado; sin cortes de audio. Interruptor nucleo_captura ("0", "1" o "2")
  // para fijarlo a un nucleo si algun juego lo necesita (sin archivo o "libre": cualquiera).
  char nc[8] = { 0 }; uint32_t cmask = 0x70000; const char *cname = "libre (0, 1 o 2)";
  SceUID nf = sceIoOpen("ux0:data/nativedraw/nucleo_captura", SCE_O_RDONLY, 0);
  if (nf >= 0) { sceIoRead(nf, nc, 7); sceIoClose(nf); }
  if (nc[0] >= '0' && nc[0] <= '2') { cmask = 0x10000 << (nc[0] - '0'); nc[1] = 0; cname = nc; }
  SceUID t = sceKernelCreateThread("nativedraw_captura", capThread, 0x61, 0x4000, 0, cmask, NULL);
  // Prioridad (v12.7): en el nucleo 2 el emulador tiene ScePspemuRemoteMe (Media Engine: decodifica el audio del juego),
  // TitleInfo y AvcAuInput con 0x60. Con 0x40 (v12.4/v12.6) el capturador los tapaba y el audio se cortaba; con 0xA0
  // (v12.5) recibia un tercio del nucleo (~1000 ns por comando). 0x61: justo debajo de ellos. (Audio: nucleo 1, 0x40-0x43;
  // ScePspemuMain y los servicios remotos: nucleo 0. Lista completa en nativedraw.log de NativeDraw v12.6)
  // v12.8: nucleo 1. En el 2 (v12.7) el Media Engine ocupaba mas de la mitad del nucleo en carrera (~900 ns por comando);
  // en el 1 solo estan los hilos de salida de audio (0x40-0x43, trabajos cortos), que siguen pasando antes.
  if (t >= 0) sceKernelStartThread(t, 0, NULL);
  cap_uid = t;
  clog("hilos: captura %08x (nucleo %s, prioridad 0x61, debajo de los hilos de audio; ux0:data/nativedraw/nucleo_captura) log %08x; hilos del emulador vistos: %d\n", t, cname, r, emu_n);
  SceUID et = sceKernelCreateThread("nativedraw_espejo", espThread, 0xBE, 0x2000, 0, 0x10000 /* nucleo 0 */, NULL);  // (v13.6: debajo de los lectores)
  if (et >= 0) sceKernelStartThread(et, 0, NULL);
  clog("espejo de la VRAM: hilo %08x (nucleo 0, prioridad 0xBE); copias en %08x y %08x\n", et, MIRROR, MIRROR2);
  medir_ge = sceIoGetstat("ux0:data/nativedraw/medir_ge", &st) >= 0;
  clog("T6 (hilo de NativeLink que mira el GE): %s\n", medir_ge ? "pedido (medir_ge)" : "apagado (crear ux0:data/nativedraw/medir_ge para medir)");
  if (sceIoGetstat("ux0:data/nativedraw/captura_sin_lectores", &st) >= 0) { clog("lectores: apagados (captura_sin_lectores)\n"); return; }
  void *m = NULL;
  SceUID blk = sceKernelAllocMemBlock("nativedraw_lineas", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (CL_N * 68 + 0xFFF) & ~0xFFF, NULL);
  if (blk < 0 || sceKernelGetMemBlockBase(blk, &m) < 0) { clog("lectores: sin memoria para la cache (%08x)\n", blk); return; }
  cl_tag = (volatile uint32_t *)((char *)m + CL_N * 64);
  for (int i = 0; i < CL_N; i++) cl_tag[i] = 0xFFFFFFFFu;
  __sync_synchronize();
  cl_data = m;
  static int ids[2] = { 0, 1 };
  static const int cores[2] = { 0x10000, 0x40000 };  // nucleo 0 (emulador) y 2 (Media Engine): solo lo que sobra
  for (int k = 0; k < 2; k++) {
    SceUID lt = sceKernelCreateThread(k ? "nativedraw_lector1" : "nativedraw_lector0", lectorThread, 0xB0, 0x2000, 0, cores[k], NULL);
    if (lt >= 0) sceKernelStartThread(lt, sizeof(int), &ids[k]);
    lec_uid[k] = lt;
    clog("lector %d: %08x (nucleo %d, prioridad 0xB0)\n", k, lt, k ? 2 : 0);
  }
}
