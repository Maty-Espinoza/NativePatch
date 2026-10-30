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
static void clog(const char *fmt, ...);
static const uint8_t RS[] = { 0x1E, 0x21, 0x22, 0x23, 0x24, 0x27, 0x28, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4A, 0x4B, 0x4C, 0x4D, 0x50, 0x55, 0x56, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA8, 0xB8, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9,
  0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE6, 0xE7, 0xE8, 0xE9 };
#define NRS ((int)sizeof RS)
#define RD_N 4096         // draws por frame
#define RE_N 1024         // estados por frame
#define RA_N (512 << 10)  // vertices, indices y paletas por frame
typedef struct { uint16_t st, prim; uint32_t n, vt, voff, vlen, ioff, clut; } RDraw;  // ioff/clut: desplazamiento + 1 (0: no hay)
typedef struct {
  uint32_t frame, fb, fbw, nd, ne, na, flags, draws_vistos;  // flags: motivos de incompleto (R_*)
  RDraw d[RD_N];
  uint32_t e[RE_N][NRS];
  uint8_t a[RA_N] __attribute__((aligned(16)));
} RFrame;
enum { R_MEMO = 1, R_LEER = 2, R_TOPE = 4, R_CURVA = 8 };
static struct { uint32_t fr, ok, memo, leer, tope, curva, draws, st, kb, cl, us, d3, clear, blend, atest, swz, fmt[16]; } ga, gr;  // por seg
static RFrame *rf[3];                        // (LPDDR con cache, ~2,5 MB)
static volatile int rg_pub = -1, rg_usando = -1;  // ultimo completo; el que esta leyendo el dibujo (paso 3.3)
static int rg_w;
static uint32_t rg_clut, rg_clut_b, rg_t;   // paleta cargada (desplazamiento + 1) y su tamano; us de este frame

static void regInit(void) {
  for (int i = 0; i < 3; i++) {
    void *p = 0;
    SceUID b = sceKernelAllocMemBlock("nativedraw_registro", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                      (sizeof(RFrame) + 0x3FFFF) & ~0x3FFFFu, 0);
    if (b < 0 || sceKernelGetMemBlockBase(b, &p) < 0) { reg_on = 0; clog("registro del frame: sin memoria (%08x)\n", b); return; }
    rf[i] = p; rf[i]->nd = rf[i]->ne = rf[i]->na = rf[i]->flags = 0;
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
  d->st = f->ne - 1; d->prim = type; d->n = n; d->vt = vt; d->ioff = 0; d->clut = rg_clut;
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
  f->nd = f->ne = f->na = f->flags = f->draws_vistos = 0;
  rg_t = 0;  // la paleta cargada sigue valiendo en el GE: se vuelve a copiar en el registro nuevo (como esta ahora en memoria)
  rg_clut = rg_clut_b ? rgCopy(last_clut[0], rg_clut_b) : 0;
}
