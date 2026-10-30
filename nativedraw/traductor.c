// traductor.c (NativeDraw 0.0.6, paso 3.3 de docs/dev/etapa3_plan.md): dibuja el registro del frame con la GPU de la Vita.
// Se incluye dentro de presentador.c (usa su superficie, sus programas y su contexto). En el hilo de dibujo de Adrenaline,
// despues de su escena, en vez de copiar la imagen del PSP se dibujan los draws del ultimo frame completo del registro al
// doble (960x544). Primera version: solo lo que necesita Retro City (y lo simple de otros juegos):
//   draws en modo through (2D), rectangulos, triangulos y tiras; con textura 5650/5551/4444/8888/T4/T8 sin swizzle;
//   sin mezcla, sin alfa test, sin color (o color blanco); dibujando en la imagen que se muestra.
// Un frame con algo que no se sabe dibujar se muestra con la imagen normal (la copia de 0.0.3) y el log cuenta el motivo.
// Las texturas salen de la copia del registro (0.0.6.2; antes directo de la RAM del PSP, que el juego ya estaba
// reescribiendo); las paletas se pasan a 8888 en memoria propia; los vertices se decodifican del registro a una tira
// propia (x, y, z, u, v) por draw. Un frame del registro se repite como mucho 2 veces seguidas (el juego a 30 fps); si
// no llega otro, la imagen normal (0.0.6.1 repetia el ultimo para siempre: con el juego en pausa no se veia el dialogo
// del boton PS).
#include "registro.h"
enum { T_OK, T_NUEVO, T_3D, T_PRIM, T_SINTEX, T_FMT, T_SWZ, T_MEZCLA, T_ALFA, T_COLOR, T_DESTINO, T_VERT, T_TOPE, T_TEX, TN };
static const char *tnom[TN] = { "ok", "sin_frame_nuevo", "3d", "primitiva", "sin_textura", "formato", "swizzle", "mezcla",
                                "alfa_test", "color", "otro_destino", "vertice", "tope", "textura_mal" };
static volatile uint32_t t_cnt[TN], t_hd, t_nat, t_us, t_draws;  // (por 10 s, los lee el hilo del presentador)
static uint32_t t_vis[TN];  // primera vez que se vio cada motivo (se anota con detalle una vez)
static float *tv[2];         // vertices traducidos (x, y, z, u, v), uno por superficie
static uint32_t *tpal[2];    // paletas traducidas (8888), 256 entradas por draw, alineadas a 64
#define TV_N (64 << 10)      // vertices por superficie (en floats de 5)
#define TP_N 64              // paletas por superficie
static SceGxmTexture tnegro; // 8x8 negro: para borrar la superficie al empezar
static uint32_t t_frame = ~0u, t_ok, t_rep;  // ultimo frame del registro dibujado, si quedo bien y cuantas veces se repitio

static int trInit(void) {
  static const uint32_t z[64] __attribute__((aligned(16)));  // (negro; la GPU no lo lee de aca: se copia)
  uint32_t *n = gmem(256, RD);
  for (int i = 0; i < 2; i++) {
    if (!(tv[i] = gmem(TV_N * 20, RD)) || !(tpal[i] = gmem(TP_N * 1024, RD))) return -1;
  }
  if (!n) return -1;
  sceClibMemcpy(n, z, sizeof z);
  sceGxmTextureInitLinear(&tnegro, n, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, 8, 8, 0);
  int r1 = sceGxmMapMemory((void *)0x70000000, 0x2000000, RD);  // RAM del PSP (0x08000000-0x0A000000) para la GPU
  int r2 = sceGxmMapMemory((void *)0x72000000, 0x1000000, RD);  // (0x0A000000-0x0B000000: espejos de la VRAM)
  int r3 = regMapGpu();
  alog("traductor: RAM del PSP para la GPU %08x %08x, texturas del registro %08x\n", r1, r2, r3);
  if (r3 < 0) return r3;
  return r1 < 0 ? r1 : 0;
}

static uint32_t pal8888(uint32_t c, uint32_t f) {  // color del PSP (5650/5551/4444/8888, R en los bits bajos) -> 0xAABBGGRR
  uint32_t r, g, b, a;
  if (f == 3) return c;
  if (f == 0) { r = c & 31; g = c >> 5 & 63; b = c >> 11 & 31; return 0xFF000000u | (b * 255 / 31) << 16 | (g * 255 / 63) << 8 | r * 255 / 31; }
  if (f == 1) { r = c & 31; g = c >> 5 & 31; b = c >> 10 & 31; a = c >> 15 ? 255 : 0; return a << 24 | (b * 255 / 31) << 16 | (g * 255 / 31) << 8 | r * 255 / 31; }
  r = c & 15; g = c >> 4 & 15; b = c >> 8 & 15; a = c >> 12 & 15;
  return (a * 17) << 24 | (b * 17) << 16 | (g * 17) << 8 | r * 17;
}

// Lo que se necesita de un draw para dibujarlo; 0 si se puede (si no, el motivo)
static int trCheck(RFrame *f, RDraw *d) {
  const uint32_t *e = f->e[d->st];
  uint32_t vt = d->vt;
  if (RE(e, 0xD3) & 1) return T_PRIM;  // (modo borrar: siguiente version)
  if (!(vt >> 23 & 1)) return T_3D;
  if (d->prim != 3 && d->prim != 4 && d->prim != 6) return T_PRIM;  // triangulos, tira, rectangulos
  if ((vt >> 9 & 3) || (vt >> 18 & 7)) return T_VERT;  // pesos o morph
  if (!(RE(e, 0x1E) & 1) || !(vt & 3)) return T_SINTEX;
  uint32_t fmt = RE(e, 0xC3) & 15;
  if (fmt > 5) return T_FMT;
  if (RE(e, 0xC2) & 1) return T_SWZ;
  if (RE(e, 0x21) & 1) return T_MEZCLA;
  if (RE(e, 0x22) & 1) return T_ALFA;
  if (fmt >= 4 && !d->clut) return T_FMT;
  if (((RE(e, 0x9C) ^ f->fb) & 0x1FFFF0) || (RE(e, 0x9D) & 0x7FF) != (f->fbw & 0xFFFF)) return T_DESTINO;
  uint32_t tf = RE(e, 0xC9) & 7;  // funcion de textura: 0 modular, 3 reemplazar
  if (tf != 0 && tf != 3) return T_COLOR;
  if (tf == 0) {
    if (vt >> 2 & 7) return T_COLOR;  // color por vertice: siguiente version (sombreador con color)
    if ((RE(e, 0x55) & 0xFFFFFF) != 0xFFFFFF || (RE(e, 0x56) & 0xFF) != 0xFF) return T_COLOR;  // color del material
  }
  return T_OK;
}

// Decodifica los vertices del draw (modo through) en tv: x, y en pixeles del PSP al doble; u, v normalizados a tw x th
static int trVerts(RFrame *f, RDraw *d, float *o, float tw, float th, uint32_t *nout) {
  static const uint8_t s4[4] = { 0, 1, 2, 4 }, cs[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
  uint32_t vt = d->vt, tc = vt & 3, ct = vt >> 2 & 7, nt = vt >> 5 & 3, pt = vt >> 7 & 3, sz = 0, al = 1, s;
  uint32_t otc = 0, opos = 0;
#define FLD(n, size, o) s = (size); if (s) { sz = (sz + s - 1) & ~(s - 1); o = sz; sz += (n) * s; if (s > al) al = s; }
  uint32_t dummy __attribute__((unused));
  FLD(2, s4[tc], otc); FLD(1, cs[ct], dummy); FLD(3, s4[nt], dummy); FLD(3, s4[pt], opos);
#undef FLD
  sz = (sz + al - 1) & ~(al - 1);
  if (!sz || !pt) return T_VERT;
  const uint8_t *vb = f->a + d->voff - 1, *ib = d->ioff ? f->a + d->ioff - 1 : 0;
  uint32_t ix = d->vt >> 11 & 3;
  float x[2], y[2], u[2], v[2];
  uint32_t n = 0;
  for (uint32_t i = 0; i < d->n; i++) {
    uint32_t k = !ib ? i : ix == 1 ? ib[i] : ix == 2 ? ((const uint16_t *)ib)[i] : ((const uint32_t *)ib)[i];
    if ((k + 1) * sz > d->vlen) return T_VERT;
    const uint8_t *p = vb + k * sz;
    float px, py, pu, pv;
    if (pt == 1) { px = (int8_t)p[opos]; py = (int8_t)p[opos + 1]; }
    else if (pt == 2) { px = ((const int16_t *)(p + opos))[0]; py = ((const int16_t *)(p + opos))[1]; }
    else { px = ((const float *)(p + opos))[0]; py = ((const float *)(p + opos))[1]; }
    if (tc == 1) { pu = p[otc]; pv = p[otc + 1]; }
    else if (tc == 2) { pu = ((const uint16_t *)(p + otc))[0]; pv = ((const uint16_t *)(p + otc))[1]; }
    else { pu = ((const float *)(p + otc))[0]; pv = ((const float *)(p + otc))[1]; }
    px *= 2; py *= 2; pu /= tw; pv /= th;
    if (d->prim == 6) {  // rectangulo: 2 vertices (esquinas opuestas) -> tira de 4 (un draw de tira por rectangulo: se separan
      x[i & 1] = px; y[i & 1] = py; u[i & 1] = pu; v[i & 1] = pv;  // con dos vertices repetidos al medio)
      if (i & 1) {
        if (n + 6 > TV_N) return T_TOPE;
        float q[6][5] = { { x[0], y[0], .5f, u[0], v[0] }, { x[0], y[0], .5f, u[0], v[0] }, { x[1], y[0], .5f, u[1], v[0] },
                          { x[0], y[1], .5f, u[0], v[1] }, { x[1], y[1], .5f, u[1], v[1] }, { x[1], y[1], .5f, u[1], v[1] } };
        sceClibMemcpy(o + n * 5, q, sizeof q);
        n += 6;
      }
    } else {
      if (n + 1 > TV_N) return T_TOPE;
      float q[5] = { px, py, .5f, pu, pv };
      sceClibMemcpy(o + n * 5, q, sizeof q);
      n++;
    }
  }
  *nout = n;
  return T_OK;
}

static uint16_t *tib;  // 0, 1, 2... (indices para la GPU)

// Dibuja el frame f en la superficie s (en la escena ya empezada). 0: listo; si no, el motivo (no se dibujo nada aun)
static int trFrame(SceGxmContext *c, RFrame *f, int s) {
  for (uint32_t i = 0; i < f->nd; i++) {
    int r = trCheck(f, &f->d[i]);
    if (r) {
      if (!t_vis[r]++) {
        const uint32_t *e = f->e[f->d[i].st];
        alog("traductor: primer frame con '%s': draw %u de %u prim %u vt %06x tex %u fmt %u swz %u mezcla %u alfa %u tfunc %u "
             "fb %06x/%u (se muestra %07x/%u) clear %u\n", tnom[r], i, f->nd, f->d[i].prim, f->d[i].vt, RE(e, 0x1E) & 1,
             RE(e, 0xC3) & 15, RE(e, 0xC2) & 1, RE(e, 0x21) & 1, RE(e, 0x22) & 1, RE(e, 0xC9) & 7, RE(e, 0x9C) & 0xFFFFFF,
             RE(e, 0x9D) & 0x7FF, f->fb, f->fbw & 0xFFFF, RE(e, 0xD3) & 1);
      }
      return r;
    }
  }
  if (!tib) { tib = gmem(TV_N * 2, RD); if (!tib) return T_TOPE; for (uint32_t i = 0; i < TV_N; i++) tib[i] = i; }
  float *o = tv[s];
  uint32_t *pal = tpal[s], np = 0, nv = 0;
  sceGxmBeginScene(c, 0, rt, 0, 0, 0, &cs[s], &ds);
  sceGxmSetVertexProgram(c, vp);
  sceGxmSetFragmentProgram(c, fp);
  void *u;
  sceGxmReserveVertexDefaultUniformBuffer(c, &u);
  static const float wvp[16] = { 2.0f / W, 0, 0, 0, 0, -2.0f / H, 0, 0, 0, 0, -2, 0, -1, 1, 1, 1 };
  sceGxmSetUniformDataF(u, p_wvp, 0, 16, wvp);
  sceGxmSetFragmentTexture(c, 0, &tnegro);  // borrar: cuadro negro de pantalla completa
  sceGxmSetVertexStream(c, 0, vb);
  sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
  static const SceGxmTextureFormat gf[6] = { SCE_GXM_TEXTURE_FORMAT_U5U6U5_BGR, SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR,
    SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ABGR, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, SCE_GXM_TEXTURE_FORMAT_P4_ABGR,
    SCE_GXM_TEXTURE_FORMAT_P8_ABGR };
  int res = T_OK;
  for (uint32_t i = 0; i < f->nd && res == T_OK; i++) {
    RDraw *d = &f->d[i];
    const uint32_t *e = f->e[d->st];
    uint32_t fmt = RE(e, 0xC3) & 15, a8 = RE(e, 0xA8), b8 = RE(e, 0xB8), tbw = a8 & 0x7FF, th = 1u << (b8 >> 8 & 15);
    if (!d->toff || !tbw) { res = T_TEX; break; }  // (sin copia: en la VRAM o no entro; siguiente version)
    SceGxmTexture t;
    if (sceGxmTextureInitLinear(&t, f->tx + d->toff - 1, gf[fmt], tbw, th, 0) < 0) { res = T_TEX; break; }
    if (fmt >= 4) {  // paleta: a 8888, con desplazamiento, mascara y comienzo (0xC5)
      if (np >= TP_N) { res = T_TOPE; break; }
      uint32_t c5 = RE(e, 0xC5), cf = c5 & 3, sh = c5 >> 2 & 31, mk = c5 >> 8 & 0xFF, st = (c5 >> 16 & 31) << 4;
      uint32_t ne = fmt == 4 ? 16 : 256, bpe = cf == 3 ? 4 : 2, max = d->clen / bpe, *pp = pal + np * 256;
      const uint8_t *src = f->a + d->clut - 1;
      for (uint32_t k = 0; k < ne; k++) {
        uint32_t j = ((k >> sh) & mk) | st;
        uint32_t cv = j < max ? (bpe == 4 ? ((const uint32_t *)src)[j] : ((const uint16_t *)src)[j]) : 0;
        pp[k] = pal8888(cv, cf);
      }
      sceGxmTextureSetPalette(&t, pp);
      np++;
    }
    uint32_t tf = RE(e, 0xC6), tw = RE(e, 0xC7);
    sceGxmTextureSetMinFilter(&t, (tf & 7) == 1 ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetMagFilter(&t, (tf >> 8 & 1) ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetUAddrMode(&t, tw & 1 ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
    sceGxmTextureSetVAddrMode(&t, tw >> 8 & 1 ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
    uint32_t n = 0;
    if ((res = trVerts(f, d, o + nv * 5, tbw, th, &n)) || nv + n > TV_N) { if (!res) res = T_TOPE; break; }
    sceGxmSetFragmentTexture(c, 0, &t);
    sceGxmSetVertexStream(c, 0, o + nv * 5);
    sceGxmDraw(c, d->prim == 3 ? SCE_GXM_PRIMITIVE_TRIANGLES : SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16,
               tib, n);
    nv += n; t_draws++;
  }
  sceGxmEndScene(c, 0, 0);  // (si algo fallo a medias, la superficie queda con lo dibujado: no se usa)
  if (res && !t_vis[res]++) alog("traductor: primer frame con '%s' al dibujar\n", tnom[res]);
  return res;
}
