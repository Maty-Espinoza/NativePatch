// registro.c (NativeDraw 0.0.5, paso 3.2 de docs/dev/etapa3_plan.md): registro del frame.
// Se incluye dentro de captura.c (usa su estado del GE: reg, vaddr, iaddr, vtr...). Con ux0:data/nativedraw/dibujar, el
// capturador, ademas de contar, guarda cada draw del frame para que la Vita lo pueda dibujar despues (paso 3.3):
// - sus vertices (y sus indices) copiados al momento: el juego puede reusar esa memoria en el frame siguiente;
// - una foto de los registros del GE que importan para dibujar (RS), solo cuando cambia (estado compartido por los draws);
// - las paletas, copiadas cuando el juego las carga (C4).
// Los frames se cierran en cada flip, en 3 registros que se turnan: el ultimo completo queda publicado (rg_pub) mientras
// se escribe otro. Completo = se registraron todos los draws que conto el capturador (las sublistas que la memoria de
// sublistas salta sin recorrer, las curvas y lo que no se pudo leer lo dejan incompleto) y entro en el registro.
// Las matrices (3D) todavia no se guardan: paso 4.
// 0.0.8.6: 4 registros en vez de 3: el ultimo que dibujo la Vita no se vuelve a escribir hasta que dibuje otro (la GPU lo
// lee despues; con 3 se sobrescribian sus texturas a veces antes: los mosaicos de KOF 96, ver docs/dev/etapa3_plan.md).
// 0.0.5.2: el registro en memoria con cache (0.0.5.1 sin cache: 23-30 ms por frame en GT, el capturador se atraso y GT bajo
// a 56-59 fps) y un frame que ya quedo incompleto deja de copiarse (GT casi nunca queda completo: memoria de sublistas).
// 0.0.6.2: tambien las texturas que estan en la RAM se copian al momento (por DMA, a memoria sin cache que lee la GPU):
// la Vita dibuja un frame despues y Retro City ya estaba escribiendo el siguiente en la misma imagen (lineas en lo que
// cambia: el titulo, el texto que parpadea, el HUD). 0.0.7: las que tienen swizzle se copian ya ordenadas (el traductor
// las lee como texturas normales). 0.0.7.3: tambien las de la VRAM, desde el espejo (los sprites de KOF 96 estan ahi).
// 0.0.7.5: de cada textura solo las filas que usan los draws (ver rgTex).
// 0.0.8: las texturas se copian por DMA y sin ordenar el swizzle (lo hace la GPU); las de la VRAM, despues del flip, de
// la copia del espejo de ese flip (ver regPend).
static void clog(const char *fmt, ...);
#include "registro.h"
enum { R_MEMO = 1, R_LEER = 2, R_TOPE = 4, R_CURVA = 8 };
static struct { uint32_t fr, ok, memo, leer, tope, curva, draws, st, kb, cl, us, d3, clear, blend, atest, swz, fmt[16], us_e, us_v, us_t, us_p, t_b, us_l, pis, tarde, reuso, gpu; } ga, gr;  // por seg (us_*: 0.0.7.7)
#define RN 4  // (0.0.8.6: 4; con 3, el que la GPU acababa de usar era el unico libre para el frame siguiente)
static RFrame *rf[RN];                       // (LPDDR con cache + texturas sin cache: ~2,4 MB cada uno)
static int rg_w;
static volatile int rg_pub = -1, rg_usando = -1;  // ultimo completo; el que esta leyendo el dibujo (paso 3.3)
static volatile int rg_ult = -1; static volatile uint32_t rg_ult_t;  // (0.0.8.2) ultimo dibujado y cuando
RFrame *regTomar(void) {  // (presentador) el ultimo frame completo, reservado hasta regSoltar; 0 si no hay
  int u = rg_pub;
  if (u < 0) return 0;
  rg_usando = u; __sync_synchronize();
  if (rg_w == u || rg_pub != u) { rg_usando = -1; return 0; }  // (el capturador lo eligio justo antes: se deja)
  rg_ult = u; rg_ult_t = now();  // (0.0.8.2) el ultimo que leyo el dibujo: la GPU lo lee despues, hasta ~1 frame
  return rf[u];
}
void regSoltar(void) { rg_usando = -1; }
// (0.0.8.10) Medicion: la escena de la Vita que leyo las texturas de cada registro (la escribe el presentador al dibujarlo)
// y la palabra donde la GPU anota la ultima escena terminada. Antes de escribir texturas en un registro se mira si la GPU
// ya lo termino de leer (el contador "reusados" de 0.0.8.2 lo suponia: < 17 ms desde que se tomo).
static volatile uint32_t rg_gq[RN], *volatile rg_gfin;
static uint8_t rg_gv[RN];  // (ya contado en este uso del registro)
static uint32_t rg_gev;
void regGpu(uint32_t q, volatile uint32_t *fin) { rg_gfin = fin; int u = rg_ult; if (u >= 0) rg_gq[u] = q; }
static void rgGpu(int k, const char *que) {
  volatile uint32_t *fin = rg_gfin;
  if (!fin || rg_gv[k] || (int)(*fin - rg_gq[k]) >= 0) return;
  rg_gv[k] = 1; ga.gpu++;
  if (rg_gev++ < 60) clog("registro: texturas %s escritas en el registro %d con la GPU sin terminar de leerlo (le faltaban %u escenas)\n",
                          que, k, rg_gq[k] - *fin);
}
static uint32_t rg_clut, rg_clut_b, rg_t;   // paleta cargada (desplazamiento + 1) y su tamano; us de este frame

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
  clog("registro del frame: activo (dibujar), %u x %u KB\n", RN, (unsigned)(sizeof(RFrame) >> 10));
}
void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
static void regBanco(void) {  // (0.0.7.9) velocidad de cada tipo de copia (una vez, 256 KB): que es lo lento de copiar texturas
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
static uint32_t rgCopy(uint32_t a, uint32_t n) {  // copia [a, a + n) de la memoria del PSP al registro: desplazamiento + 1
  RFrame *f = rf[rg_w];
  uint32_t o = (f->na + 15) & ~15u;
  if (o + n > RA_N) { f->flags |= R_TOPE; return 0; }
  for (uint32_t k = 0; k < n;) {  // de a pagina de 4 KB (la VRAM se traduce al espejo por pagina)
    uint32_t s = vtr(a + k), m = 4096 - ((a + k) & 4095);
    if (m > n - k) m = n - k;
    if (!s || ramEnd(s) < s + m) { f->flags |= R_LEER; return 0; }
    sceClibMemcpy(f->a + o + k, (const void *)PSP(s), m);
    k += m;
  }
  f->na = o + n;
  return o + 1;
}
void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
int sceGxmMapMemory(void *base, SceSize size, int attr);
int regMapGpu(void) {
  int r = 0;
  for (int i = 0; i < RN && rf[i] && r >= 0; i++) r = sceGxmMapMemory(rf[i]->tx, TX_N, 1);  // (1: lectura)
  return r;
}
// (0.0.7.5) De la textura solo se copian los grupos de 8 filas que usan los draws del frame (KOF 96: cada sprite lee unas
// pocas filas de hojas de 512x512 en la VRAM; copiarlas enteras costaba 7-17 ms por frame y el juego bajaba a ~35 fps).
// Con swizzle, un grupo de 8 filas son bloques seguidos en memoria: se lee junto, se ordena en memoria con cache y se
// escribe de una vez (escribir de a 16 bytes en la memoria sin cache era lo lento).
static uint32_t rgSrc(uint32_t a, uint32_t n) {  // de donde leer [a, a + n): la RAM o el espejo de la VRAM; 0 si no se puede
  if (!isVram(a)) return ramEnd(a) >= a + n ? a : 0;
  for (uint32_t k = 0; k < n; k += 4096 - ((a + k) & 4095)) if (!vtr(a + k)) return 0;  // (draw() ya la pidio: desde el frame siguiente)
  uint32_t s = vtr(a);
  return vtr(a + n - 1) == s + n - 1 && ramEnd(s) >= s + n ? s : 0;  // (todo en el mismo tramo del espejo)
}
static uint32_t rgTex(uint32_t r0, uint32_t r1) {  // filas [r0, r1) de la textura del draw: desplazamiento + 1 en tx, 0 si no
  RFrame *f = rf[rg_w];
  static const uint8_t bp[6] = { 16, 16, 16, 32, 4, 8 };
  uint32_t fmt = reg[0xC3] & 15, ta = TEXADDR(), tbw = reg[0xA8] & 0x7FF, th = 1u << ((reg[0xB8] >> 8) & 15), swz = reg[0xC2] & 1;
  if (fmt > 5 || !tbw || th > 512) return 0;
  uint32_t rb = tbw * bp[fmt] / 8, n = rb * (swz && th < 8 ? 8 : th), key = n | swz << 31, i, vr = isVram(ta);
  if (swz && (rb & 15)) { f->flags |= R_LEER; return 0; }
  if (r1 > th || r0 >= r1) r0 = 0, r1 = th;
  for (i = 0; i < f->nt && !(f->ta[i] == ta && f->tn[i] == key); i++) {}
  if (i == f->nt) {  // primera vez en este frame: se le reserva el tamano completo (las filas que no se copian no se leen)
    if (vr) { uint32_t p = (ta & 0x1FFFFF) >> 12; if (rt_pg[p >> 5] >> (p & 31) & 1) return 0; }  // destino de dibujo: imagen intermedia
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
    if (vr) { if (!rgSrc(ta + o, m)) { f->flags |= R_LEER; return 0; } f->dv = 1; }  // (0.0.8) de la VRAM: se copia despues (regPend)
    else {  // de la RAM: ahora (el juego la puede cambiar), por DMA y tal cual (con swizzle lo ordena la GPU)
      uint32_t sa = rgSrc(ta + o, m), t0 = now();
      if (rg_w == rg_ult && t0 - rg_ult_t < 17000) ga.reuso++;
      rgGpu(rg_w, "de la RAM");
      if (!sa) { f->flags |= R_LEER; return 0; }
      sceDmacMemcpy(f->tx + f->to[i] + o, (const void *)PSP(sa), m);
      ga.t_b += m; ga.us_l += now() - t0;
    }
    f->tm[i] |= ((h - g < 64 ? (1ull << (h - g)) : 0) - 1) << g;
    g = h;
  }
  return f->to[i] + 1;
}
// (0.0.8) Las texturas de la VRAM de un frame completo se copian cuando termina la copia del espejo que el PSP hizo en SU
// flip (f->tgt): el juego sube los sprites de un frame despues del flip anterior, asi que la copia con que se leyo la
// lista era de un frame antes (los mosaicos de KOF 96: volcado 013 de 0.0.7.10, dibujado en el PC sale bien con la copia
// siguiente). Por DMA (512 MB/s; la CPU lee la RAM del PSP a 74) y sin ordenar el swizzle (lo hace el sombreador).
// Mientras, el frame queda "pendiente" y no se publica.
static volatile int rg_pend = -1;
static uint32_t rg_pend_t;
static void regPend(void) {  // (capturador) si la copia ya termino: copia las texturas pendientes y publica el frame
  int k = rg_pend;
  if (k < 0) return;
  RFrame *f = rf[k];
  int listo = (int)(SB[0x40] - f->tgt) >= 0;
  if (!listo && now() - rg_pend_t < 100000) return;  // (si no termina en 100 ms, con la que haya)
  uint32_t b = (listo ? f->tgt : SB[0x40]) & 1, base = b ? MIRROR2 : MIRROR, t0 = now();
  rgGpu(k, "de la VRAM");  // (0.0.8.10)
  if (k == rg_ult && t0 - rg_ult_t < 17000) ga.reuso++;  // (0.0.8.2) se escribe el que el dibujo tomo hace < 1 frame (la GPU quiza lo lee)
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
      sceDmacMemcpy(f->tx + f->to[i] + o, (const void *)PSP(base + a), l);
      ga.t_b += l;
      g = h;
    }
  }
  if (f->nimg == ~0u) {  // (0.0.8.4) la imagen del PSP de este mismo frame (la copia del espejo de su flip), para comparar
    f->nimg = 0;
    uint32_t a = f->fb & 0x1FFFFF, n = (f->fbw & 0xFFFF) * 272 * ((f->fbw >> 16 & 3) == 3 ? 4 : 2), ok = isVram(f->fb) && f->ntx + n <= TX_N;
    for (uint32_t pg = a >> 12; ok && pg <= (a + n - 1) >> 12; pg++) ok = CPY(b, pg) == (listo ? f->tgt : SB[0x40]);
    if (ok) { sceDmacMemcpy(f->tx + f->ntx, (const void *)PSP(base + a), n); f->nimg = f->ntx + 1; f->ntx += (n + 63) & ~63u; }
  }
  ga.us_l += now() - t0;
  if ((int)(SB[0x44] - f->tgt) >= 2) { ga.pis++; rg_pend = -1; return; }  // (el PSP ya volvio a copiar en ese espejo: se descarta)
  if (!listo) ga.tarde++;
  f->espf = SB[0x40];
  __sync_synchronize();
  rg_pub = k; rg_pend = -1;
}
// Filas de la textura que leen los vertices del draw (modo through, coordenadas en texels); 0 si no se sabe (se copia toda)
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
static void regDraw(uint32_t n, uint32_t type, uint32_t vt, uint32_t idx) {  // antes de que avancen vaddr/iaddr
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
  if ((reg[0x1E] & 1) && !(reg[0xD3] & 1)) {  // (0.0.7.5) la textura, despues de los vertices: solo las filas que usan
    uint32_t r0 = 0, r1 = 0;
    rgFilas(vt, f->a + d->voff - 1, d->vlen, d->ioff ? f->a + d->ioff - 1 : 0, n, &r0, &r1);
    d->toff = rgTex(r0, r1);
  }
  f->nd++;
  uint32_t t3 = now(); ga.us_t += t3 - t2;
  rg_t += t3 - t0;
  // lo que usa cada juego (para el paso 3.3)
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
static void regClut(uint32_t d) {  // carga de paleta: se copia (bytes = bloques de 32)
  uint32_t n = (d & 0x3F) * 32;
  rg_clut_b = n;
  if (rf[rg_w]->flags) { rg_clut = 0; return; }
  uint32_t t0 = now();
  rg_clut = n ? rgCopy(CLUTADDR(), n) : 0;
  if (rg_clut) ga.cl++;
  ga.us_p += now() - t0;
}
static void regCurva(void) { rf[rg_w]->flags |= R_CURVA; rf[rg_w]->draws_vistos++; }
static void regMemo(uint32_t draws) { if (draws) { rf[rg_w]->flags |= R_MEMO; rf[rg_w]->draws_vistos += draws; } }
static void regFlip(uint32_t fb, uint32_t fbw, uint32_t draws_contados) {  // cierra el frame
  RFrame *f = rf[rg_w];
  regPend();
  if (f->draws_vistos < draws_contados) f->flags |= R_LEER;  // (draws que no pasaron por regDraw: curvas, listas cortadas)
  ga.fr++; ga.us += rg_t; ga.st += f->ne; ga.kb += f->na;
  if (f->flags & R_MEMO) ga.memo++;
  if (f->flags & R_LEER) ga.leer++;
  if (f->flags & R_TOPE) ga.tope++;
  if (f->flags & R_CURVA) ga.curva++;
  if (!f->flags && f->nd) {  // completo: se publica (o queda pendiente de sus texturas) y se escribe en otro que nadie use
    f->fb = fb & 0x0FFFFFFF; f->fbw = fbw; f->frame = gen; f->esp = esp_u; f->espf = esp_on ? SB[0x40] : 0;
    ga.ok++;
    __sync_synchronize();
    if ((reg_diag || (int)(nd_exacta_hasta - now()) > 0) && isVram(f->fb)) {  // (0.0.8.4) se piden las paginas de la imagen que se
      uint32_t a = f->fb & 0x1FFFFF, n = (f->fbw & 0xFFFF) * 272 * ((f->fbw >> 16 & 3) == 3 ? 4 : 2);  // muestra (y se espera su copia);
      for (uint32_t pg = a >> 12; pg <= (a + n - 1) >> 12 && pg < 512; pg++) want_fb[pg >> 5] |= 1u << (pg & 31);  // 0.0.8.7: tambien
      f->dv = 1; f->nimg = ~0u;                                                                       // por un rato, para L+R+SELECT
    } else for (int w = 0; w < 16; w++) want_fb[w] = 0;  // (ya no: se dejan de copiar)
    if (f->dv && esp_on) {
      if (rg_pend >= 0) ga.pis++;  // (el anterior todavia esperaba: se reemplaza)
      f->tgt = SB[0x44]; rg_pend_t = now(); rg_pend = rg_w;  // (la copia que el PSP encolo en este flip: NativeLink la hace antes del evento)
      regPend();
    } else rg_pub = rg_w;
    int w = -1;
    for (int i = 0; i < RN && w < 0; i++) if (i != rg_pub && i != rg_usando && i != rg_pend && i != rg_ult) w = i;  // (0.0.8.6: ni el
    if (w < 0) for (int i = 0; i < RN && w < 0; i++) if (i != rg_pub && i != rg_usando && i != rg_pend) w = i;  // ultimo dibujado)
    if (w < 0) { w = rg_pend; rg_pend = -1; ga.pis++; }  // (sin uno libre: se pierde el pendiente)
    rg_w = w; rg_gv[w] = 0;
  }
  f = rf[rg_w];
  f->nd = f->ne = f->na = f->flags = f->draws_vistos = f->nt = f->ntx = f->dv = f->nimg = 0;
  rg_t = 0;  // la paleta cargada sigue valiendo en el GE: se vuelve a copiar en el registro nuevo (como esta ahora en memoria)
  rg_clut = rg_clut_b ? rgCopy(last_clut[0], rg_clut_b) : 0;
}
