// registro.c: registro del frame. Guarda cada draw del frame para que la Vita lo dibuje despues con su GPU.
// Se incluye dentro de captura.c (usa su estado del GE: reg, vaddr, iaddr, vtr, el espejo de la VRAM) y solo trabaja
// con ux0:data/nativedraw/dibujar. El capturador llama regDraw/regClut/regCurva/regMemo mientras recorre la lista y
// regFlip en cada flip; el presentador (traductor.c) toma el ultimo frame completo con regTomar/regSoltar.
// Por frame se guardan: vertices e indices copiados (el juego puede reusar esa memoria), el estado del GE (RS) solo
// cuando cambia, las paletas al cargarse y las filas de textura que usan los draws (copiadas a memoria sin cache que
// lee la GPU; las que tienen swizzle del PSP quedan ordenadas, con las filas seguidas: rgTexCopia). Hay RN registros
// que se turnan: uno se escribe, otro queda publicado (rg_pub) y los que la GPU puede estar leyendo no se tocan.
// Completo = todos los draws contados pasaron por regDraw y entraron. Detalle: docs/dev/etapa3_plan.md.
// TODO:
// - Guardar las matrices (draws 3D).
// - Copiar de la RAM del PSP cuesta ~6 us por draw (KOF 96, ~2,4 ms por frame): abaratar si molesta.
// - Algunos frames de KOF 96 llegan al tope (R_TOPE) en las transiciones.
static void clog(const char *fmt, ...);
#include "registro.h"
enum { R_MEMO = 1, R_LEER = 2, R_TOPE = 4, R_CURVA = 8 };
// Estadisticas por segundo para la linea "registro:" del log (ga se acumula, gr es la copia que se escribe; us_*:
// tiempos de estados, vertices, texturas, paletas y copias por DMA; gpu: texturas escritas en un registro que la
// GPU podia estar leyendo; o_b/us_o: bytes con swizzle ordenados y su tiempo, que va dentro de us_l)
static struct { uint32_t fr, ok, memo, leer, tope, curva, draws, st, kb, cl, us, d3, clear, blend, atest, swz, fmt[16], us_e, us_v, us_t, us_p, t_b, us_l, pis, tarde, gpu, o_b, us_o; } ga, gr;
// RN registros: con menos, el que la GPU acaba de leer seria a veces el unico libre y se sobrescribirian sus texturas
#define RN 4
static RFrame *rf[RN];                       // (LPDDR con cache + texturas sin cache: ~2,4 MB cada uno)
static int rg_w;  // el que se esta escribiendo
static volatile int rg_pub = -1, rg_usando = -1;  // ultimo completo; el que esta leyendo el presentador
static volatile int rg_ult = -1;  // ultimo que tomo el presentador
// (presentador) Reserva el ultimo frame completo hasta regSoltar; 0 si no hay o si el capturador lo esta reusando.
// Anota cual fue (rg_ult): la GPU lo sigue leyendo hasta que termina la escena anotada con regGpu.
RFrame *regTomar(void) {
  int u = rg_pub;
  if (u < 0) return 0;
  rg_usando = u; __sync_synchronize();
  if (rg_w == u || rg_pub != u) { rg_usando = -1; return 0; }  // (el capturador lo eligio justo antes: se deja)
  rg_ult = u;
  return rf[u];
}
void regSoltar(void) { rg_usando = -1; }
// Control de la GPU: rg_gq[k] es la escena de la Vita que lee las texturas del registro k (la anota el presentador con
// regGpu) y *rg_gfin la ultima escena que la GPU termino. Sirve para contar escrituras en un registro todavia en uso.
static volatile uint32_t rg_gq[RN], *volatile rg_gfin;
static uint8_t rg_gv[RN];  // (ya contado en este uso del registro)
static uint32_t rg_gev;
// (presentador) q: escena que lee el registro tomado; fin: palabra donde la GPU anota la ultima escena terminada
void regGpu(uint32_t q, volatile uint32_t *fin) { rg_gfin = fin; int u = rg_ult; if (u >= 0) rg_gq[u] = q; }
// 1 si la GPU todavia no termino la escena que lee el registro k
static int rgOcup(int k) { volatile uint32_t *fin = rg_gfin; return fin && (int)(*fin - rg_gq[k]) < 0; }
// Antes de escribir texturas en el registro k: si la GPU todavia no termino la escena que lo lee, lo cuenta y lo anota
// en el log (las primeras 60 veces). Solo mide, no espera.
static void rgGpu(int k, const char *que) {
  volatile uint32_t *fin = rg_gfin;
  if (!fin || rg_gv[k] || (int)(*fin - rg_gq[k]) >= 0) return;
  rg_gv[k] = 1; ga.gpu++;
  if (rg_gev++ < 60) clog("registro: texturas %s escritas en el registro %d con la GPU sin terminar de leerlo (le faltaban %u escenas)\n",
                          que, k, rg_gq[k] - *fin);
}
static uint32_t rg_clut, rg_clut_b, rg_t;   // paleta cargada (desplazamiento + 1) y su tamano; us de este frame

void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
// Texturas con swizzle del PSP (bloques de 16 bytes x 8 filas): se copian por DMA a rg_ord (OR_N bytes; rg_ordc: con
// cache) y la CPU las deja ordenadas en el registro, asi la GPU las lee como una textura comun (con filtro si el juego
// lo pide y sin calcular donde esta cada texel)
#define OR_N (256 << 10)
static uint8_t *rg_ord;
static int rg_ordc;
// 1 si una copia por DMA a la memoria con cache c (desde u, sin cache) se ve al leerla despues, aunque c ya este en la
// cache (si el sistema no la invalida, se leeria lo de antes)
static int rgCoherente(uint8_t *c, uint8_t *u) {
  for (uint32_t k = 0; k < 2; k++) {
    for (uint32_t i = 0; i < 4096; i++) u[i] = i * 7 + k;
    sceDmacMemcpy(c, u, 4096);
    for (uint32_t i = 0; i < 4096; i++) if (c[i] != (uint8_t)(i * 7 + k)) return 0;
  }
  return 1;
}
// Copia m bytes de s (memoria del PSP) a d (texturas del registro). rb: bytes por fila si la textura tiene swizzle (0:
// tal cual por DMA); entonces m son grupos de 8 filas enteros y cada grupo se ordena: el bloque b (16 bytes de ancho) de
// la fila r esta en s + b * 128 + r * 16 y va a d + r * rb + b * 16 (d se escribe en orden: es memoria sin cache).
static void rgTexCopia(uint8_t *d, const void *s, uint32_t m, uint32_t rb) {
  if (!rb) { sceDmacMemcpy(d, s, m); return; }
  uint32_t gb = 8 * rb, max = OR_N / gb * gb, t0 = now();
  for (uint32_t o = 0; o < m; o += max) {
    uint32_t l = m - o < max ? m - o : max;
    sceDmacMemcpy(rg_ord, (const uint8_t *)s + o, l);
    for (uint32_t g = 0; g < l; g += gb)
      for (uint32_t r = 0; r < 8; r++)
        for (uint32_t b = 0; b < rb; b += 16) {
          const uint64_t *i = (const uint64_t *)(rg_ord + g + b * 8 + r * 16);
          uint64_t *x = (uint64_t *)(d + o + g + r * rb + b);
          x[0] = i[0]; x[1] = i[1];
        }
  }
  ga.o_b += m; ga.us_o += now() - t0;
}
// Reserva los RN registros: la parte principal en memoria con cache y las texturas en memoria sin cache (la lee la
// GPU), y la memoria de paso para ordenar las texturas con swizzle. Sin memoria apaga el registro (reg_on = 0).
static void regInit(void) {
  for (int i = 0; i < RN; i++) {
    void *p = 0;
    SceUID b = sceKernelAllocMemBlock("nativedraw_registro", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                      (sizeof(RFrame) + 0x3FFFF) & ~0x3FFFFu, 0);
    if (b < 0 || sceKernelGetMemBlockBase(b, &p) < 0) { reg_on = 0; clog("registro del frame: sin memoria (%08x)\n", b); return; }
    rf[i] = p; rf[i]->nd = rf[i]->ne = rf[i]->na = rf[i]->flags = rf[i]->nt = rf[i]->ntx = 0;
    SceUID t = sceKernelAllocMemBlock("nativedraw_registro_tex", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, TX_N, 0);
    if (t < 0 || sceKernelGetMemBlockBase(t, &p) < 0) { reg_on = 0; clog("registro del frame: sin memoria para texturas (%08x)\n", t); return; }
    rf[i]->tx = p;
  }
  // Paso de las texturas con swizzle: con cache si la copia por DMA la deja al dia (rgCoherente); si no, sin cache
  void *p = 0;
  SceUID b = sceKernelAllocMemBlock("nativedraw_orden", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, OR_N, 0);
  if (b >= 0 && sceKernelGetMemBlockBase(b, &p) >= 0 && rgCoherente(p, rf[0]->tx)) rg_ord = p, rg_ordc = 1;
  else {
    if (b >= 0) sceKernelFreeMemBlock(b);
    b = sceKernelAllocMemBlock("nativedraw_orden", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, OR_N, 0);
    if (b < 0 || sceKernelGetMemBlockBase(b, &p) < 0) { reg_on = 0; clog("registro del frame: sin memoria para ordenar texturas (%08x)\n", b); return; }
    rg_ord = p;
  }
  clog("registro del frame: activo (dibujar), %u x %u KB; texturas con swizzle ordenadas por la CPU desde memoria %s\n", RN,
       (unsigned)(sizeof(RFrame) >> 10), rg_ordc ? "con cache (el DMA la deja al dia)" : "sin cache (el DMA no deja al dia la cache: lento)");
}
// Diagnostico (una vez, al encontrar el buzon): mide la velocidad de cada tipo de copia (256 KB, CPU o DMA, con o sin
// cache) usando el registro 2 como memoria de prueba, y la escribe en el log.
static void regBanco(void) {
  static int hecho;
  if (hecho++ || !rf[2] || !okRam(0x0A200000u)) return;
  const void *ps = (const void *)PSP(0x0A200000u);  // (el espejo de la VRAM)
  uint8_t *c = rf[2]->a, *u = rf[2]->tx;
  uint32_t n = 256 << 10, t[6], t0;
  t0 = now(); sceClibMemcpy(c, ps, n); t[0] = now() - t0;      // RAM del PSP -> con cache (CPU)
  t0 = now(); sceDmacMemcpy(u, ps, n); t[1] = now() - t0;      // RAM del PSP -> sin cache (DMA)
  t0 = now(); sceClibMemcpy(u, c, n); t[2] = now() - t0;       // con cache -> sin cache (CPU)
  t0 = now(); sceClibMemcpy(c, u, n); t[3] = now() - t0;       // sin cache -> con cache (CPU)
  t0 = now(); sceDmacMemcpy(u + n, u, n); t[4] = now() - t0;   // sin cache -> sin cache (DMA)
  t0 = now(); sceClibMemcpy(c + n, c, n); t[5] = now() - t0;   // con cache -> con cache (CPU)
  for (int i = 0; i < 6; i++) t[i] = t[i] ? n / t[i] : 0;
  clog("banco de memoria (MB/s): RAM del PSP->con cache %u, RAM del PSP->sin cache (DMA) %u, con cache->sin cache %u, "
       "sin cache->con cache %u, sin cache->sin cache (DMA) %u, con cache->con cache %u\n", t[0], t[1], t[2], t[3], t[4], t[5]);
}
// Copia [a, a + n) de la memoria del PSP al area a del registro (alineado a 16), de a pagina de 4 KB porque la VRAM se
// traduce al espejo por pagina. Devuelve desplazamiento + 1; 0 si no entra (R_TOPE) o no se puede leer (R_LEER).
static uint32_t rgCopy(uint32_t a, uint32_t n) {
  RFrame *f = rf[rg_w];
  uint32_t o = (f->na + 15) & ~15u;
  if (o + n > RA_N) { f->flags |= R_TOPE; return 0; }
  for (uint32_t k = 0; k < n;) {
    uint32_t s = vtr(a + k), m = 4096 - ((a + k) & 4095);
    if (m > n - k) m = n - k;
    if (!s || ramEnd(s) < s + m) { f->flags |= R_LEER; return 0; }
    sceClibMemcpy(f->a + o + k, (const void *)PSP(s), m);
    k += m;
  }
  f->na = o + n;
  return o + 1;
}
int sceGxmMapMemory(void *base, SceSize size, int attr);
// (traductor, con GXM iniciado) registra para la GPU, de solo lectura, las texturas de todos los registros
int regMapGpu(void) {
  int r = 0;
  for (int i = 0; i < RN && rf[i] && r >= 0; i++) r = sceGxmMapMemory(rf[i]->tx, TX_N, 1);  // (1: lectura)
  return r;
}
// De donde leer [a, a + n): la misma direccion si es RAM legible; si es VRAM, su copia en el espejo, solo si todas las
// paginas tienen copia valida y quedan seguidas en el mismo tramo del espejo. 0 si no se puede.
static uint32_t rgSrc(uint32_t a, uint32_t n) {
  if (!isVram(a)) return ramEnd(a) >= a + n ? a : 0;
  // draw() ya la pidio: estara desde el frame siguiente
  for (uint32_t k = 0; k < n; k += 4096 - ((a + k) & 4095)) if (!vtr(a + k)) return 0;
  uint32_t s = vtr(a);
  return vtr(a + n - 1) == s + n - 1 && ramEnd(s) >= s + n ? s : 0;
}
// Textura del draw actual (registros del GE) en tx: la primera vez en el frame le reserva su tamano completo; despues
// copia solo los grupos de 8 filas de [r0, r1) que faltan (r0 >= r1 o fuera de rango: toda la textura). Con swizzle un
// grupo de 8 filas es un bloque seguido y ordenado ocupa el mismo lugar (rgTexCopia lo ordena). Las de la RAM se copian
// ya por DMA (el juego las puede cambiar); las de la VRAM se marcan y las copia regPend del espejo del flip.
// Devuelve desplazamiento + 1 en tx; 0 si no se copia (formato no soportado, destino de dibujo, tope o no legible).
static uint32_t rgTex(uint32_t r0, uint32_t r1) {
  RFrame *f = rf[rg_w];
  static const uint8_t bp[6] = { 16, 16, 16, 32, 4, 8 };
  uint32_t fmt = reg[0xC3] & 15, ta = TEXADDR(), tbw = reg[0xA8] & 0x7FF, th = 1u << ((reg[0xB8] >> 8) & 15), swz = reg[0xC2] & 1;
  if (fmt > 5 || !tbw || th > 512) return 0;
  uint32_t rb = tbw * bp[fmt] / 8, n = rb * (swz && th < 8 ? 8 : th), key = n | swz << 31, i, vr = isVram(ta);
  if (swz && (rb & 15)) { f->flags |= R_LEER; return 0; }
  if (r1 > th || r0 >= r1) r0 = 0, r1 = th;
  for (i = 0; i < f->nt && !(f->ta[i] == ta && f->tn[i] == key); i++) {}
  // Primera vez en este frame: se le reserva el tamano completo (las filas que no se copian no se leen)
  if (i == f->nt) {
    // Destino de dibujo del juego (imagen intermedia): no se copia
    if (vr) { uint32_t p = (ta & 0x1FFFFF) >> 12; if (rt_pg[p >> 5] >> (p & 31) & 1) return 0; }
    if (f->nt >= TN_N || f->ntx + n > TX_N) { f->flags |= R_TOPE; return 0; }
    f->ta[i] = ta; f->tn[i] = key; f->to[i] = f->ntx; f->tm[i] = 0; f->tq[i] = vr ? ~0u : 0; f->tr[i] = 8 * rb; f->nt++;
    f->ntx += (n + 63) & ~63u;
  }
  uint32_t g0 = r0 / 8, g1 = (r1 + 7) / 8, gb = 8 * rb;  // grupos y bytes por grupo
  for (uint32_t g = g0; g < g1; g++) {
    if (f->tm[i] >> g & 1) continue;
    uint32_t h = g + 1;
    while (h < g1 && !(f->tm[i] >> h & 1)) h++;  // tramo de grupos sin copiar [g, h)
    uint32_t o = g * gb, m = (h * gb > n ? n : h * gb) - o;
    // De la VRAM: se copia despues (regPend)
    if (vr) { if (!rgSrc(ta + o, m)) { f->flags |= R_LEER; return 0; } f->dv = 1; }
    else {  // de la RAM: ahora (el juego la puede cambiar), por DMA
      uint32_t sa = rgSrc(ta + o, m), t0 = now();
      rgGpu(rg_w, "de la RAM");
      if (!sa) { f->flags |= R_LEER; return 0; }
      rgTexCopia(f->tx + f->to[i] + o, (const void *)PSP(sa), m, swz ? rb : 0);
      ga.t_b += m; ga.us_l += now() - t0;
    }
    f->tm[i] |= ((h - g < 64 ? (1ull << (h - g)) : 0) - 1) << g;
    g = h;
  }
  return f->to[i] + 1;
}
// Frame pendiente: las texturas de la VRAM se copian de la copia del espejo que el PSP hizo en el flip de ese frame
// (f->tgt): el juego sube los sprites de un frame despues del flip anterior, asi que la copia con que se leyo la lista
// puede ser de un frame antes. Mientras esa copia no termina, el frame no se publica.
static volatile int rg_pend = -1;
static uint32_t rg_pend_t;
// (capturador) Si la copia del espejo del frame pendiente ya termino (o pasaron 100 ms: con la que haya), copia por DMA
// sus texturas de la VRAM (y su imagen del PSP si se pidio para un volcado) y lo publica. Si el PSP ya volvio a
// escribir ese espejo, lo descarta. No espera nunca al PSP.
static void regPend(void) {
  int k = rg_pend;
  if (k < 0) return;
  RFrame *f = rf[k];
  int listo = (int)(SB[0x40] - f->tgt) >= 0;
  if (!listo && now() - rg_pend_t < 100000) return;
  uint32_t b = (listo ? f->tgt : SB[0x40]) & 1, base = b ? MIRROR2 : MIRROR, t0 = now();
  rgGpu(k, "de la VRAM");
  for (uint32_t i = 0; i < f->nt; i++) {
    if (!isVram(f->ta[i]) || !f->tm[i]) continue;
    uint32_t n = f->tn[i] & 0x7FFFFFFF, gb = f->tr[i];
    uint64_t m = f->tm[i];
    for (uint32_t g = 0; g < 64 && m >> g; g++) {  // tramos de grupos pedidos
      if (!(m >> g & 1)) continue;
      uint32_t h = g + 1;
      while (h < 64 && (m >> h & 1)) h++;
      uint32_t o = g * gb, l = (h * gb > n ? n : h * gb) - o, a = (f->ta[i] + o) & 0x1FFFFF;
      for (uint32_t pg = a >> 12, pe = (a + l - 1) >> 12; pg <= pe; pg++) { uint32_t q = CPY(b, pg); if (q < f->tq[i]) f->tq[i] = q; }
      rgTexCopia(f->tx + f->to[i] + o, (const void *)PSP(base + a), l, f->tn[i] >> 31 ? gb / 8 : 0);
      ga.t_b += l;
      g = h;
    }
  }
  // Volcados: la imagen del PSP de este mismo frame, si todas sus paginas son de la copia de su flip
  if (f->nimg == ~0u) {
    f->nimg = 0;
    uint32_t a = f->fb & 0x1FFFFF, n = (f->fbw & 0xFFFF) * 272 * ((f->fbw >> 16 & 3) == 3 ? 4 : 2), ok = isVram(f->fb) && f->ntx + n <= TX_N;
    for (uint32_t pg = a >> 12; ok && pg <= (a + n - 1) >> 12; pg++) ok = CPY(b, pg) == (listo ? f->tgt : SB[0x40]);
    if (ok) { sceDmacMemcpy(f->tx + f->ntx, (const void *)PSP(base + a), n); f->nimg = f->ntx + 1; f->ntx += (n + 63) & ~63u; }
  }
  ga.us_l += now() - t0;
  // El PSP ya volvio a copiar en ese espejo: se descarta
  if ((int)(SB[0x44] - f->tgt) >= 2) { ga.pis++; rg_pend = -1; return; }
  if (!listo) ga.tarde++;
  f->espf = SB[0x40];
  __sync_synchronize();
  rg_pub = k; rg_pend = -1;
}
// Filas de la textura que leen los vertices del draw (solo modo through 2D, coordenadas en texels): calcula el tamano
// del vertice, recorre los vertices usados (por indice si hay) y toma la v minima y maxima. 1 y [r0, r1) si se sabe;
// 0 si no (3D, sin textura, pesos, morph, v negativa o vertices fuera de lo copiado): entonces se copia toda.
static int rgFilas(uint32_t vt, const uint8_t *vb, uint32_t vlen, const uint8_t *ib, uint32_t n, uint32_t *r0, uint32_t *r1) {
  static const uint8_t s4[4] = { 0, 1, 2, 4 }, cs[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
  uint32_t tc = vt & 3, ix = vt >> 11 & 3, s = s4[tc], al = s, sz = 2 * s, q;
  if (!(vt >> 23 & 1) || !tc || (vt >> 9 & 3) || (vt >> 18 & 7)) return 0;  // 3D, sin textura, pesos o morph
  q = cs[vt >> 2 & 7]; if (q) { sz = (sz + q - 1) & ~(q - 1); sz += q; if (q > al) al = q; }
  q = s4[vt >> 5 & 3]; if (q) { sz = (sz + q - 1) & ~(q - 1); sz += 3 * q; if (q > al) al = q; }
  q = s4[vt >> 7 & 3]; if (q) { sz = (sz + q - 1) & ~(q - 1); sz += 3 * q; if (q > al) al = q; }
  sz = (sz + al - 1) & ~(al - 1);
  int lo = 1 << 30, hi = -1;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t k = !ib ? i : ix == 1 ? ib[i] : ix == 2 ? ((const uint16_t *)ib)[i] : ((const uint32_t *)ib)[i];
    if ((k + 1) * sz > vlen) return 0;
    const uint8_t *p = vb + k * sz;  // (la coordenada de textura va primero)
    int v = tc == 1 ? p[1] : tc == 2 ? ((const uint16_t *)p)[1] : (int)((const float *)p)[1];
    if (tc == 3 && ((const float *)p)[1] < 0) return 0;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  if (hi < 0) return 0;
  *r0 = lo > 0 ? lo - 1 : 0; *r1 = hi + 2;  // (una fila de margen por lado, por el filtro)
  return 1;
}
// (capturador, en cada draw, antes de que avancen vaddr/iaddr) Agrega el draw al frame: el estado (RS) si cambio, los
// indices y los vertices hasta el mayor indice, y la textura (solo las filas que usan los vertices). Si algo no entra o
// no se lee el frame queda incompleto y deja de copiarse. Al final suma las estadisticas de lo que usa cada juego.
static void regDraw(uint32_t n, uint32_t type, uint32_t vt, uint32_t idx) {
  RFrame *f = rf[rg_w];
  f->draws_vistos++;
  if (f->flags) return;  // ya incompleto: no se gasta mas en este frame
  if (f->nd >= RD_N) { f->flags |= R_TOPE; return; }
  uint32_t t0 = now(), t1, v[NRS];
  for (int i = 0; i < NRS; i++) v[i] = reg[RS[i]];
  if (!f->ne || sceClibMemcmp(v, f->e[f->ne - 1], sizeof v)) {  // estado nuevo
    if (f->ne >= RE_N) { f->flags |= R_TOPE; return; }
    sceClibMemcpy(f->e[f->ne++], v, sizeof v);
  }
  t1 = now(); ga.us_e += t1 - t0;
  RDraw *d = &f->d[f->nd];
  d->st = f->ne - 1; d->prim = type; d->n = n; d->vt = vt; d->ioff = 0; d->clut = rg_clut; d->clen = rg_clut ? rg_clut_b : 0;
  uint32_t vs = vsize(vt), nv = n;
  if (idx) {  // con indices: se copian y los vertices hasta el mayor indice
    uint32_t ib = idx == 1 ? 1 : idx == 2 ? 2 : 4;
    if (!(d->ioff = rgCopy(iaddr, n * ib))) return;
    const uint8_t *p = f->a + d->ioff - 1;
    uint32_t mx = 0;
    for (uint32_t i = 0; i < n; i++) { uint32_t x = ib == 1 ? p[i] : ib == 2 ? ((const uint16_t *)p)[i] : ((const uint32_t *)p)[i]; if (x > mx) mx = x; }
    nv = mx + 1;
  }
  d->vlen = nv * vs;
  if (!(d->voff = rgCopy(vaddr, d->vlen))) return;
  d->toff = 0;
  uint32_t t2 = now(); ga.us_v += t2 - t1;
  if ((reg[0x1E] & 1) && !(reg[0xD3] & 1)) {  // la textura, despues de los vertices (de ellos salen las filas que usa)
    uint32_t r0 = 0, r1 = 0;
    rgFilas(vt, f->a + d->voff - 1, d->vlen, d->ioff ? f->a + d->ioff - 1 : 0, n, &r0, &r1);
    d->toff = rgTex(r0, r1);
  }
  f->nd++;
  uint32_t t3 = now(); ga.us_t += t3 - t2;
  rg_t += t3 - t0;
  // lo que usa cada juego (estadisticas del log)
  uint32_t *s = f->e[d->st];
  ga.draws++;
  if (!(vt >> 23 & 1) && !(reg[0xD3] & 1)) ga.d3++;
  if (reg[0xD3] & 1) ga.clear++;
  else {
    if (s[1] & 1) ga.blend++;
    if (s[2] & 1) ga.atest++;
    if (s[0] & 1) { ga.fmt[reg[0xC3] & 0xF]++; if (reg[0xC2] & 1) ga.swz++; }
  }
}
// (capturador, comando 0xC4) Carga de paleta: la copia al registro (d & 0x3F bloques de 32 bytes) y queda como la
// paleta de los draws siguientes.
static void regClut(uint32_t d) {
  uint32_t n = (d & 0x3F) * 32;
  rg_clut_b = n;
  if (rf[rg_w]->flags) { rg_clut = 0; return; }
  uint32_t t0 = now();
  rg_clut = n ? rgCopy(CLUTADDR(), n) : 0;
  if (rg_clut) ga.cl++;
  ga.us_p += now() - t0;
}
// Draws que no pasan por regDraw: una curva o los draws de una sublista que la memoria de sublistas aplico sin
// recorrer. Dejan el frame incompleto.
static void regCurva(void) { rf[rg_w]->flags |= R_CURVA; rf[rg_w]->draws_vistos++; }
static void regMemo(uint32_t draws) { if (draws) { rf[rg_w]->flags |= R_MEMO; rf[rg_w]->draws_vistos += draws; } }
// (capturador, en cada flip) Cierra el frame. fb/fbw: imagen que se muestra y ancho | formato << 16; draws_contados:
// los que conto el capturador. Si esta completo lo publica (o lo deja pendiente de sus texturas de la VRAM, regPend),
// pide al espejo la imagen del PSP si un volcado la quiere, y elige otro registro para escribir: ni el publicado, ni el
// que lee el presentador, ni el pendiente, ni (si se puede) el ultimo que tomo el presentador. Despues vacia el
// registro nuevo y vuelve a copiar en el la paleta cargada.
static void regFlip(uint32_t fb, uint32_t fbw, uint32_t draws_contados) {
  RFrame *f = rf[rg_w];
  regPend();
  // Draws que no pasaron por regDraw: curvas, listas cortadas
  if (f->draws_vistos < draws_contados) f->flags |= R_LEER;
  ga.fr++; ga.us += rg_t; ga.st += f->ne; ga.kb += f->na;
  if (f->flags & R_MEMO) ga.memo++;
  if (f->flags & R_LEER) ga.leer++;
  if (f->flags & R_TOPE) ga.tope++;
  if (f->flags & R_CURVA) ga.curva++;
  // Completo: se publica (o queda pendiente de sus texturas) y se escribe en otro que nadie use
  if (!f->flags && f->nd) {
    f->fb = fb & 0x0FFFFFFF; f->fbw = fbw; f->frame = gen; f->esp = esp_u; f->espf = esp_on ? SB[0x40] : 0;
    ga.ok++;
    __sync_synchronize();
    // Un volcado pide la imagen del PSP de este frame: se piden sus paginas al espejo y el frame espera su copia
    if ((int)(nd_exacta_hasta - now()) > 0 && isVram(f->fb)) {
      uint32_t a = f->fb & 0x1FFFFF, n = (f->fbw & 0xFFFF) * 272 * ((f->fbw >> 16 & 3) == 3 ? 4 : 2);
      for (uint32_t pg = a >> 12; pg <= (a + n - 1) >> 12 && pg < 512; pg++) want_fb[pg >> 5] |= 1u << (pg & 31);
      f->dv = 1; f->nimg = ~0u;
    } else for (int w = 0; w < 16; w++) want_fb[w] = 0;  // (ya no: se dejan de copiar)
    if (f->dv && esp_on) {
      if (rg_pend >= 0) ga.pis++;  // (el anterior todavia esperaba: se reemplaza)
      // tgt: la copia del espejo que el PSP encolo en este flip (NativeLink la anota antes del evento)
      f->tgt = SB[0x44]; rg_pend_t = now(); rg_pend = rg_w;
      regPend();
    } else rg_pub = rg_w;
    // El siguiente: uno libre que la GPU ya termino de leer; si no hay, uno libre que no sea el ultimo que tomo el
    // presentador; si no, cualquiera libre
    int w = -1;
    for (int i = 0; i < RN && w < 0; i++) if (i != rg_pub && i != rg_usando && i != rg_pend && !rgOcup(i)) w = i;
    for (int i = 0; i < RN && w < 0; i++) if (i != rg_pub && i != rg_usando && i != rg_pend && i != rg_ult) w = i;
    if (w < 0) for (int i = 0; i < RN && w < 0; i++) if (i != rg_pub && i != rg_usando && i != rg_pend) w = i;
    if (w < 0) { w = rg_pend; rg_pend = -1; ga.pis++; }  // (sin uno libre: se pierde el pendiente)
    rg_w = w; rg_gv[w] = 0;
  }
  f = rf[rg_w];
  f->nd = f->ne = f->na = f->flags = f->draws_vistos = f->nt = f->ntx = f->dv = f->nimg = 0;
  rg_t = 0;
  // La paleta cargada sigue valiendo en el GE: se vuelve a copiar en el registro nuevo (como esta ahora en memoria)
  rg_clut = rg_clut_b ? rgCopy(last_clut[0], rg_clut_b) : 0;
}
