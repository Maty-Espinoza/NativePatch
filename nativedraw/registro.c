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
// 0.0.5.2: el registro en memoria con cache (0.0.5.1 sin cache: 23-30 ms por frame en GT, el capturador se atraso y GT bajo
// a 56-59 fps) y un frame que ya quedo incompleto deja de copiarse (GT casi nunca queda completo: memoria de sublistas).
// 0.0.6.2: tambien las texturas que estan en la RAM se copian al momento (por DMA, a memoria sin cache que lee la GPU):
// la Vita dibuja un frame despues y Retro City ya estaba escribiendo el siguiente en la misma imagen (lineas en lo que
// cambia: el titulo, el texto que parpadea, el HUD).
static void clog(const char *fmt, ...);
#include "registro.h"
enum { R_MEMO = 1, R_LEER = 2, R_TOPE = 4, R_CURVA = 8 };
static struct { uint32_t fr, ok, memo, leer, tope, curva, draws, st, kb, cl, us, d3, clear, blend, atest, swz, fmt[16]; } ga, gr;  // por seg
static RFrame *rf[3];                        // (LPDDR con cache, ~2,5 MB)
static int rg_w;
static volatile int rg_pub = -1, rg_usando = -1;  // ultimo completo; el que esta leyendo el dibujo (paso 3.3)
RFrame *regTomar(void) {  // (presentador) el ultimo frame completo, reservado hasta regSoltar; 0 si no hay
  int u = rg_pub;
  if (u < 0) return 0;
  rg_usando = u; __sync_synchronize();
  if (rg_w == u || rg_pub != u) { rg_usando = -1; return 0; }  // (el capturador lo eligio justo antes: se deja)
  return rf[u];
}
void regSoltar(void) { rg_usando = -1; }
static uint32_t rg_clut, rg_clut_b, rg_t;   // paleta cargada (desplazamiento + 1) y su tamano; us de este frame

static void regInit(void) {
  for (int i = 0; i < 3; i++) {
    void *p = 0;
    SceUID b = sceKernelAllocMemBlock("nativedraw_registro", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                      (sizeof(RFrame) + 0x3FFFF) & ~0x3FFFFu, 0);
    if (b < 0 || sceKernelGetMemBlockBase(b, &p) < 0) { reg_on = 0; clog("registro del frame: sin memoria (%08x)\n", b); return; }
    rf[i] = p; rf[i]->nd = rf[i]->ne = rf[i]->na = rf[i]->flags = rf[i]->nt = rf[i]->ntx = 0;
    SceUID t = sceKernelAllocMemBlock("nativedraw_registro_tex", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, TX_N, 0);
    if (t < 0 || sceKernelGetMemBlockBase(t, &p) < 0) { reg_on = 0; clog("registro del frame: sin memoria para texturas (%08x)\n", t); return; }
    rf[i]->tx = p;
  }
  clog("registro del frame: activo (dibujar), 3 x %u KB\n", (unsigned)(sizeof(RFrame) >> 10));
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
  for (int i = 0; i < 3 && rf[i] && r >= 0; i++) r = sceGxmMapMemory(rf[i]->tx, TX_N, 1);  // (1: lectura)
  return r;
}
static uint32_t rgTex(void) {  // copia la textura del draw (si esta en la RAM): desplazamiento + 1 en tx, 0 si no
  RFrame *f = rf[rg_w];
  static const uint8_t bp[6] = { 16, 16, 16, 32, 4, 8 };
  uint32_t fmt = reg[0xC3] & 15, ta = TEXADDR(), tbw = reg[0xA8] & 0x7FF, th = 1u << ((reg[0xB8] >> 8) & 15);
  if (fmt > 5 || isVram(ta) || !tbw) return 0;
  uint32_t n = tbw * th * bp[fmt] / 8;
  for (uint32_t i = 0; i < f->nt; i++) if (f->ta[i] == ta && f->tn[i] >= n) return f->to[i] + 1;
  if (f->nt >= 64 || f->ntx + n > TX_N || ramEnd(ta) < ta + n) { f->flags |= R_TOPE; return 0; }
  sceDmacMemcpy(f->tx + f->ntx, (const void *)PSP(ta), n);
  f->ta[f->nt] = ta; f->tn[f->nt] = n; f->to[f->nt++] = f->ntx;
  f->ntx += (n + 63) & ~63u;
  return f->to[f->nt - 1] + 1;
}
static void regDraw(uint32_t n, uint32_t type, uint32_t vt, uint32_t idx) {  // antes de que avancen vaddr/iaddr
  RFrame *f = rf[rg_w];
  f->draws_vistos++;
  if (f->flags) return;  // ya incompleto: no se gasta mas en este frame
  if (f->nd >= RD_N) { f->flags |= R_TOPE; return; }
  uint32_t t0 = now(), v[NRS];
  for (int i = 0; i < NRS; i++) v[i] = reg[RS[i]];
  if (!f->ne || sceClibMemcmp(v, f->e[f->ne - 1], sizeof v)) {  // estado nuevo
    if (f->ne >= RE_N) { f->flags |= R_TOPE; return; }
    sceClibMemcpy(f->e[f->ne++], v, sizeof v);
  }
  RDraw *d = &f->d[f->nd];
  d->st = f->ne - 1; d->prim = type; d->n = n; d->vt = vt; d->ioff = 0; d->clut = rg_clut; d->clen = rg_clut ? rg_clut_b : 0;
  d->toff = (reg[0x1E] & 1) && !(reg[0xD3] & 1) ? rgTex() : 0;
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
  f->nd++;
  rg_t += now() - t0;
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
  rg_clut = n ? rgCopy(CLUTADDR(), n) : 0;
  if (rg_clut) ga.cl++;
}
static void regCurva(void) { rf[rg_w]->flags |= R_CURVA; rf[rg_w]->draws_vistos++; }
static void regMemo(uint32_t draws) { if (draws) { rf[rg_w]->flags |= R_MEMO; rf[rg_w]->draws_vistos += draws; } }
static void regFlip(uint32_t fb, uint32_t fbw, uint32_t draws_contados) {  // cierra el frame
  RFrame *f = rf[rg_w];
  if (f->draws_vistos < draws_contados) f->flags |= R_LEER;  // (draws que no pasaron por regDraw: curvas, listas cortadas)
  ga.fr++; ga.us += rg_t; ga.st += f->ne; ga.kb += f->na;
  if (f->flags & R_MEMO) ga.memo++;
  if (f->flags & R_LEER) ga.leer++;
  if (f->flags & R_TOPE) ga.tope++;
  if (f->flags & R_CURVA) ga.curva++;
  if (!f->flags && f->nd) {  // completo: se publica y se escribe en otro que nadie este usando
    f->fb = fb & 0x0FFFFFFF; f->fbw = fbw; f->frame = gen;
    ga.ok++;
    __sync_synchronize();
    rg_pub = rg_w;
    for (int i = 0; i < 3; i++) if (i != rg_pub && i != rg_usando) { rg_w = i; break; }
  }
  f = rf[rg_w];
  f->nd = f->ne = f->na = f->flags = f->draws_vistos = f->nt = f->ntx = 0;
  rg_t = 0;  // la paleta cargada sigue valiendo en el GE: se vuelve a copiar en el registro nuevo (como esta ahora en memoria)
  rg_clut = rg_clut_b ? rgCopy(last_clut[0], rg_clut_b) : 0;
}
