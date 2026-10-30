// traductor.c: dibuja con la GPU de la Vita (GXM), al doble (960x544), un frame del registro (registro.h: draws y
// estados del GE que guardo registro.c en el capturador). Se incluye dentro de presentador.c y usa su patcher, sus
// superficies y su contexto; el presentador llama a trInit al iniciar y a trFrame en el hilo de Adrenaline, en vez de
// copiar la imagen del PSP. Si el frame trae algo que no sabe dibujar devuelve el motivo (T_*) y el presentador muestra
// la imagen normal. Sombreadores propios en sombreadores/*.cg. Detalle: docs/dev/etapa3_plan.md (paso 3.3).
// TODO:
// - 3D (sin modo through), profundidad y stencil, test de color y operaciones logicas
// - mezcla con el alfa del destino o con factores al doble (hoy: motivo "mezcla")
// - recortar al scissor tambien triangulos y tiras (hoy solo los rectangulos)
// - imagen intermedia dibujada en un frame y usada como textura en otro (hoy: imagen normal)
// - texturas con swizzle con filtro bilineal (el sombreador nd_fs lee sin filtro)
// - repetir las copias de bloque (TRANSFER) del GE a la VRAM
// - juntar draws con el mismo estado (menos llamadas a la GPU)
#include "registro.h"
#include "sombreadores_nd.h"
// Resultado de trCheck/trFrame: T_OK o el motivo por el que el frame no se dibujo (tnom: su nombre en el log)
enum { T_OK, T_NUEVO, T_3D, T_PRIM, T_FMT, T_MEZCLA, T_TFUNC, T_RARO, T_DESTINO, T_VERT, T_TOPE, T_TEX, T_PROG, TN };
static const char *tnom[TN] = { "ok", "sin_frame_nuevo", "3d", "primitiva", "formato", "mezcla", "funcion_textura",
                                "test_color_o_logica", "otro_destino", "vertice", "tope", "textura_mal", "programa" };
// contadores del log de cada 10 s (los lee y borra el presentador)
static volatile uint32_t t_cnt[TN], t_hd, t_nat, t_draws;
static volatile uint32_t t_salto[6];  // frames del PSP que avanzo cada frame dibujado (0: repetido, 5: 5 o mas)
static uint32_t t_vis[TN];   // veces que se vio cada motivo (el primero se anota con detalle en el log)
typedef struct { float x, y, z, u, v; uint32_t c; } TV;  // vertice traducido (24 bytes, el formato de nd_v)
static TV *tv[2];            // vertices traducidos, uno por superficie
static uint32_t *tpal[2];    // paletas traducidas (8888), alineadas a 64: 16 entradas (64 bytes) o 256 (1 KB)
#define TV_N (64 << 10)      // vertices por superficie (y tope de los indices de 16 bits)
#define TP_B (256 << 10)     // bytes de paletas por superficie (KOF 96 carga ~260 paletas de 16 colores por frame)
// ultimo frame del registro dibujado, si quedo bien y cuantas veces se repitio
static uint32_t t_frame = ~0u, t_ok, t_rep;
static uint16_t *tib;        // 0, 1, 2... (indices para la GPU)
// Imagen intermedia: un draw del juego a otra imagen de la VRAM (no la que se muestra) va a una superficie propia al
// doble (1024x544); si despues, en el mismo frame, una textura esta en esa direccion, se lee de esa superficie.
// fb: direccion de la imagen en la VRAM (~0u: libre); fr: ultimo frame en que se uso.
typedef struct { uint32_t fb, fr; void *mem; SceGxmColorSurface cs; } RT;
static RT rts[2];
// Reserva la superficie de una imagen intermedia (alfa 255, como la superficie principal). Las dos se reservan en
// trInit, antes que el volcado: si no, el volcado se queda con la memoria fisica.
static int rtMem(RT *r) {
  if (!(r->mem = gmem1(PHY, LINE * H * 4, RW))) { alog("traductor: sin memoria para una imagen intermedia\n"); return 0; }
  for (uint32_t k = 0; k < LINE * H; k++) ((uint32_t *)r->mem)[k] = 0xFF000000u;  // (el alfa no se escribe nunca)
  sceGxmColorSurfaceInit(&r->cs, SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                         SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, LINE, H, LINE, r->mem);
  r->fb = ~0u;
  return 1;
}
// La imagen intermedia de la imagen fb (desplazamiento en la VRAM) y la marca como usada en frame. Si no hay y crear,
// toma una que no se haya usado en este frame (la reserva si hace falta). 0 si no hay.
static RT *rtDe(uint32_t fb, uint32_t frame, int crear) {
  for (int i = 0; i < 2; i++) if (rts[i].mem && rts[i].fb == fb) { rts[i].fr = frame; return &rts[i]; }
  if (!crear) return 0;
  for (int i = 0; i < 2; i++) {
    RT *r = &rts[i];
    if (r->mem && r->fr == frame) continue;  // (ya la usa otra imagen de este frame)
    if (!r->mem && !rtMem(r)) return 0;
    if (r->fb == ~0u) alog("traductor: imagen intermedia %d (%s) para %06x\n", i, tipo(r->mem), fb);
    r->fb = fb; r->fr = frame;
    return r;
  }
  return 0;
}
// Direccion de la textura 0 del estado e (registros 0xA0 y 0xA8 del GE)
static uint32_t texAddr(const uint32_t *e) { return (RE(e, 0xA0) & 0xFFFFF0) | ((RE(e, 0xA8) << 8) & 0x0F000000); }

// --- programas: uno de vertices (nd_v) y tres de fragmentos: nd_fc sin textura, nd_ft con textura, nd_fs con textura
// con swizzle. Cada programa de fragmentos se arma una vez por cada mezcla que aparece (fps, hasta 64) ---
static SceGxmVertexProgram *nvp;
static const SceGxmProgram *pft, *pfc, *pfs, *pv;
static SceGxmShaderPatcherId ift, ifc, ifs;
static const SceGxmProgramParameter *n_wvp, *n_tf, *n_atx, *n_atc, *n_tfs, *n_ats, *n_sws;  // (*s: los de nd_fs)
typedef struct { uint32_t key; SceGxmFragmentProgram *p; } FP;
static FP fps[64];
static uint32_t nfps;
// Programa de fragmentos para tex (0 sin textura, 1 con textura, 2 con swizzle) y key (0 sin mezcla; si no, los 4 bytes
// de SceGxmBlendInfo + 1, ver blendKey). Lo busca en fps o lo crea con el patcher (sin escribir el alfa); 0 si falla.
static SceGxmFragmentProgram *fragProg(int tex, uint32_t key) {
  uint32_t k = key << 2 | tex;
  for (uint32_t i = 0; i < nfps; i++) if (fps[i].key == k) return fps[i].p;
  if (nfps >= 64) return 0;
  SceGxmBlendInfo b;
  if (key) sceClibMemcpy(&b, &(uint32_t){ key - 1 }, 4);
  else { sceClibMemset(&b, 0, sizeof b); b.colorFunc = b.alphaFunc = SCE_GXM_BLEND_FUNC_NONE; }
  b.colorMask = SCE_GXM_COLOR_MASK_R | SCE_GXM_COLOR_MASK_G | SCE_GXM_COLOR_MASK_B;  // (alfa del PSP = su stencil)
  SceGxmFragmentProgram *p = 0;
  int r = sceGxmShaderPatcherCreateFragmentProgram(pat, tex == 2 ? ifs : tex ? ift : ifc, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                   SCE_GXM_MULTISAMPLE_NONE, &b, pv, &p);
  if (r < 0) { alog("traductor: programa de fragmentos %u %08x: %08x\n", tex, key, r); return 0; }
  fps[nfps].key = k; fps[nfps++].p = p;
  return p;
}

// Prepara el traductor (lo llama presStart, solo con dibujar): memoria de vertices, paletas e indices, registra los
// sombreadores y busca sus parametros, crea los programas basicos, reserva las imagenes intermedias y deja a la GPU
// leer la RAM del PSP y las texturas del registro. 0 si quedo listo; negativo si no.
static int trInit(void) {
  for (int i = 0; i < 2; i++)
    if (!(tv[i] = gmem(TV_N * sizeof(TV), RD)) || !(tpal[i] = gmem(TP_B, RD))) return -1;
  if (!(tib = gmem(TV_N * 2, RD))) return -1;
  for (uint32_t i = 0; i < TV_N; i++) tib[i] = i;
  SceGxmShaderPatcherId iv;
  int r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_v_gxp, &iv);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_ft_gxp, &ift);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_fc_gxp, &ifc);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_fs_gxp, &ifs);
  if (r < 0) { alog("traductor: registrar sombreadores %08x\n", r); return r; }
  pv = sceGxmShaderPatcherGetProgramFromId(iv); pft = sceGxmShaderPatcherGetProgramFromId(ift); pfc = sceGxmShaderPatcherGetProgramFromId(ifc);
  pfs = sceGxmShaderPatcherGetProgramFromId(ifs);
  const SceGxmProgramParameter *pa = sceGxmProgramFindParameterByName(pv, "aPosition"), *pt = sceGxmProgramFindParameterByName(pv, "aTexcoord"),
                               *pc = sceGxmProgramFindParameterByName(pv, "aColor");
  n_wvp = sceGxmProgramFindParameterByName(pv, "wvp");
  n_tf = sceGxmProgramFindParameterByName(pft, "tf"); n_atx = sceGxmProgramFindParameterByName(pft, "at");
  n_atc = sceGxmProgramFindParameterByName(pfc, "at");
  n_tfs = sceGxmProgramFindParameterByName(pfs, "tf"); n_ats = sceGxmProgramFindParameterByName(pfs, "at"); n_sws = sceGxmProgramFindParameterByName(pfs, "sw");
  if (!pa || !pt || !pc || !n_wvp || !n_tf || !n_atx || !n_atc || !n_tfs || !n_ats || !n_sws) { alog("traductor: parametros de los sombreadores no encontrados\n"); return -2; }
  SceGxmVertexAttribute at[3] = { { 0, 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 3, sceGxmProgramParameterGetResourceIndex(pa) },
                                  { 0, 12, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2, sceGxmProgramParameterGetResourceIndex(pt) },
                                  { 0, 20, SCE_GXM_ATTRIBUTE_FORMAT_U8N, 4, sceGxmProgramParameterGetResourceIndex(pc) } };
  SceGxmVertexStream st = { sizeof(TV), SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
  if ((r = sceGxmShaderPatcherCreateVertexProgram(pat, iv, at, 3, &st, 1, &nvp)) < 0) { alog("traductor: programa de vertices %08x\n", r); return r; }
  if (!fragProg(0, 0) || !fragProg(1, 0) || !fragProg(2, 0)) return -3;
  for (int i = 0; i < 2; i++) rtMem(&rts[i]);
  // RAM del PSP (0x08000000-0x0A000000) para la GPU (con 0x72000000 falla)
  int r1 = sceGxmMapMemory((void *)0x70000000, 0x2000000, RD);
  int r3 = regMapGpu();
  alog("traductor: RAM del PSP para la GPU %08x, texturas del registro %08x\n", r1, r3);
  return r3 < 0 ? r3 : 0;
}

// Color del PSP c en formato f (0 5650, 1 5551, 2 4444, 3 8888; R en los bits bajos) -> 0xAABBGGRR (canales a 0-255)
static uint32_t pal8888(uint32_t c, uint32_t f) {
  uint32_t r, g, b, a;
  if (f == 3) return c;
  if (f == 0) { r = c & 31; g = c >> 5 & 63; b = c >> 11 & 31; return 0xFF000000u | (b * 255 / 31) << 16 | (g * 255 / 63) << 8 | r * 255 / 31; }
  if (f == 1) { r = c & 31; g = c >> 5 & 31; b = c >> 10 & 31; a = c >> 15 ? 255 : 0; return a << 24 | (b * 255 / 31) << 16 | (g * 255 / 31) << 8 | r * 255 / 31; }
  r = c & 15; g = c >> 4 & 15; b = c >> 8 & 15; a = c >> 12 & 15;
  return (a * 17) << 24 | (b * 17) << 16 | (g * 17) << 8 | r * 17;
}

// Mezcla del GE del estado e (0x21 activa, 0xDF factores y operacion, 0xE0/0xE1 colores fijos) -> los 4 bytes de
// SceGxmBlendInfo + 1 (0: sin mezcla); ~0u si no tiene equivalente. Los colores fijos solo sirven si son 0 o blanco
// (factor ZERO u ONE); el alfa del destino no se calcula (no se escribe).
static uint32_t blendKey(const uint32_t *e) {
  if (!(RE(e, 0x21) & 1)) return 0;
  uint32_t df = RE(e, 0xDF), s = df & 15, d = df >> 4 & 15, op = df >> 8 & 15;
  static const uint8_t fs[6] = { SCE_GXM_BLEND_FACTOR_DST_COLOR, SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR, SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
                                 SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, 0, 0 },
                       fd[6] = { SCE_GXM_BLEND_FACTOR_SRC_COLOR, SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
                                 SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, 0, 0 },
                       fo[5] = { SCE_GXM_BLEND_FUNC_ADD, SCE_GXM_BLEND_FUNC_SUBTRACT, SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT,
                                 SCE_GXM_BLEND_FUNC_MIN, SCE_GXM_BLEND_FUNC_MAX };
  uint32_t gs, gd, fa = RE(e, 0xE0) & 0xFFFFFF, fb = RE(e, 0xE1) & 0xFFFFFF;
  if (s < 4) gs = fs[s]; else if (s == 10 && (fa == 0 || fa == 0xFFFFFF)) gs = fa ? SCE_GXM_BLEND_FACTOR_ONE : SCE_GXM_BLEND_FACTOR_ZERO; else return ~0u;
  if (d < 4) gd = fd[d]; else if (d == 10 && (fb == 0 || fb == 0xFFFFFF)) gd = fb ? SCE_GXM_BLEND_FACTOR_ONE : SCE_GXM_BLEND_FACTOR_ZERO; else return ~0u;
  if (op > 4) return ~0u;
  SceGxmBlendInfo b;
  sceClibMemset(&b, 0, sizeof b);
  b.colorFunc = b.alphaFunc = fo[op]; b.colorSrc = gs; b.colorDst = gd; b.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE; b.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;
  uint32_t k;
  sceClibMemcpy(&k, &b, 4);
  return k + 1;
}

// Revisa si el draw d del frame f se sabe dibujar (modo through, primitiva, vertices sin pesos, destino, mezcla,
// formato y funcion de textura). T_OK si se puede; si no, el motivo. Lo que depende de otros draws lo mira trFrame.
static int trCheck(RFrame *f, RDraw *d) {
  const uint32_t *e = f->e[d->st];
  uint32_t vt = d->vt, clr = RE(e, 0xD3) & 1;
  if (!(vt >> 23 & 1)) return T_3D;
  if (d->prim != 3 && d->prim != 4 && d->prim != 6) return T_PRIM;  // triangulos, tira, rectangulos
  if ((vt >> 9 & 3) || (vt >> 18 & 7)) return T_VERT;  // pesos o morph
  if ((RE(e, 0x9D) & 0x7FF) > 512) return T_DESTINO;
  if (clr) return T_OK;
  if (RE(e, 0x27) & 1 || RE(e, 0x28) & 1) return T_RARO;
  if (blendKey(e) == ~0u) return T_MEZCLA;
  if ((RE(e, 0x1E) & 1) && (vt & 3)) {
    uint32_t fmt = RE(e, 0xC3) & 15, tf = RE(e, 0xC9) & 7;
    if (fmt > 5 || (fmt >= 4 && !d->clut)) return T_FMT;
    if (tf != 0 && tf != 3) return T_TFUNC;
    // (textura sin copia en el registro: trFrame mira si es una imagen intermedia de este frame)
  }
  return T_OK;
}

// Color de un vertice en p con el tipo de color ct del GE (4 5650, 5 5551, 6 4444, 7 8888) -> 0xAABBGGRR
static uint32_t col8888(const uint8_t *p, uint32_t ct) {
  if (ct == 7) return *(const uint32_t *)p;
  return pal8888(*(const uint16_t *)p, ct - 4);
}

// Decodifica los vertices del draw d (modo through) a o: calcula el tamano y la posicion de cada campo segun el tipo de
// vertice del GE (con su alineacion), sigue los indices si hay, y deja x, y al doble y u, v divididos por tw x th.
// col0: color si el vertice no trae; sc: scissor al doble (x0, y0, x1, y1). Los rectangulos (2 esquinas) se recortan al
// scissor y salen como tiras de 4 vertices. *nout: vertices escritos. T_OK, T_VERT (datos fuera del registro) o T_TOPE.
static int trVerts(RFrame *f, RDraw *d, TV *o, float tw, float th, uint32_t col0, const float *sc, uint32_t *nout) {
  static const uint8_t s4[4] = { 0, 1, 2, 4 }, cs[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
  uint32_t vt = d->vt, tc = vt & 3, ct = vt >> 2 & 7, nt = vt >> 5 & 3, pt = vt >> 7 & 3, sz = 0, al = 1, s;
  uint32_t otc = 0, ocol = 0, opos = 0, dummy __attribute__((unused));
  // FLD: n componentes de size bytes, alineados a size (o: desplazamiento). Orden: textura, color, normal, posicion
#define FLD(n, size, o) s = (size); if (s) { sz = (sz + s - 1) & ~(s - 1); o = sz; sz += (n) * s; if (s > al) al = s; }
  FLD(2, s4[tc], otc); FLD(1, cs[ct], ocol); FLD(3, s4[nt], dummy); FLD(3, s4[pt], opos);
#undef FLD
  sz = (sz + al - 1) & ~(al - 1);
  if (!sz || !pt) return T_VERT;
  const uint8_t *vb = f->a + d->voff - 1, *ib = d->ioff ? f->a + d->ioff - 1 : 0;  // (desplazamientos + 1; 0: no hay)
  uint32_t ix = d->vt >> 11 & 3, n = 0;
  TV r[2];
  for (uint32_t i = 0; i < d->n; i++) {
    uint32_t k = !ib ? i : ix == 1 ? ib[i] : ix == 2 ? ((const uint16_t *)ib)[i] : ((const uint32_t *)ib)[i];
    if ((k + 1) * sz > d->vlen) return T_VERT;
    const uint8_t *p = vb + k * sz;
    TV q = { 0, 0, .5f, 0, 0, ct ? col8888(p + ocol, ct) : col0 };
    if (pt == 1) { q.x = (int8_t)p[opos]; q.y = (int8_t)p[opos + 1]; }
    else if (pt == 2) { q.x = ((const int16_t *)(p + opos))[0]; q.y = ((const int16_t *)(p + opos))[1]; }
    else { q.x = ((const float *)(p + opos))[0]; q.y = ((const float *)(p + opos))[1]; }
    if (tc == 1) { q.u = p[otc]; q.v = p[otc + 1]; }
    else if (tc == 2) { q.u = ((const uint16_t *)(p + otc))[0]; q.v = ((const uint16_t *)(p + otc))[1]; }
    else if (tc == 3) { q.u = ((const float *)(p + otc))[0]; q.v = ((const float *)(p + otc))[1]; }
    q.x *= 2; q.y *= 2; q.u /= tw; q.v /= th;
    if (d->prim == 6) {  // rectangulo: 2 vertices (esquinas opuestas) -> tira de 4, separada de la siguiente con dos
      r[i & 1] = q;      // vertices repetidos; el color es el del segundo vertice (como el GE)
      if (i & 1) {
        if (n + 6 > TV_N) return T_TOPE;
        for (int ej = 0; ej < 2; ej++) {  // recorte al scissor en x y en y; u y v se recortan en proporcion
          float *a0 = ej ? &r[0].y : &r[0].x, *a1 = ej ? &r[1].y : &r[1].x, *t0 = ej ? &r[0].v : &r[0].u, *t1 = ej ? &r[1].v : &r[1].u;
          float lo = sc[ej], hi = sc[ej + 2], d = *a1 - *a0, p0 = *a0, q0 = *t0, k = d != 0 ? (*t1 - *t0) / d : 0;
          if (d >= 0) { if (*a0 < lo) *a0 = lo; if (*a1 > hi) *a1 = hi; if (*a0 >= *a1) goto fuera; }
          else { if (*a1 < lo) *a1 = lo; if (*a0 > hi) *a0 = hi; if (*a1 >= *a0) goto fuera; }
          *t0 = q0 + (*a0 - p0) * k; *t1 = q0 + (*a1 - p0) * k;
        }
        uint32_t c = r[1].c;
        TV a = { r[0].x, r[0].y, .5f, r[0].u, r[0].v, c }, b = { r[1].x, r[0].y, .5f, r[1].u, r[0].v, c },
           cc = { r[0].x, r[1].y, .5f, r[0].u, r[1].v, c }, dd = { r[1].x, r[1].y, .5f, r[1].u, r[1].v, c };
        o[n++] = a; o[n++] = a; o[n++] = b; o[n++] = cc; o[n++] = dd; o[n++] = dd;
      }
    fuera:;
    } else {
      if (n + 1 > TV_N) return T_TOPE;
      o[n++] = q;
    }
  }
  *nout = n;
  return T_OK;
}

// --- prueba del sombreador con swizzle (trPrueba, una vez al empezar a dibujar): separa un error de nd_fs de uno de
// los datos. Dibuja en la imagen intermedia 1 una textura T4 de 512x512 conocida (entera 1:1 y los texels 384-511 al
// doble, para ver la precision), la lee de vuelta y cuenta los pixeles distintos; repite despues de cambiar la textura
// por DMA (si la GPU usara una copia vieja en su cache, saldria el dibujo anterior). Resultado en el log ---
void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
// Indice de paleta (0-15) del texel (u, v) en la version k del dibujo de prueba
static uint32_t prIx(uint32_t u, uint32_t v, uint32_t k) { return (u * 7 + v * 13 + (u >> 5) * 5 + k * 9) & 15; }
// Arma en t la T4 512x512 con swizzle del PSP (bloques de 32 texels x 8 filas)
static void prT4(uint8_t *t, uint32_t k) {
  for (uint32_t v = 0; v < 512; v++)
    for (uint32_t u = 0; u < 512; u += 2) {
      uint32_t o = (((v / 8) * 16 + u / 32) * 8 + v % 8) * 16 + (u % 32) / 2;
      t[o] = prIx(u, v, k) | prIx(u + 1, v, k) << 4;
    }
}
static void trPrueba(SceGxmContext *c) {  // (no libera su memoria)
  static int hecho;
  if (hecho++ || !rts[1].mem) return;
  uint8_t *t4 = gmem(0x20000, RD), *cpu = gmem(0x20000, RD);
  uint32_t *pal = gmem(0x1000, RD);
  TV *q = gmem(0x1000, RD);
  if (!t4 || !cpu || !pal || !q) { alog("traductor: prueba del sombreador: sin memoria\n"); return; }
  for (uint32_t i = 0; i < 16; i++) pal[i] = 0xFF000000u | (i * 16) << 16 | (255 - i * 16) << 8 | 0x40;
  static const float wvp[16] = { 2.0f / LINE, 0, 0, 0, 0, -2.0f / H, 0, 0, 0, 0, -2, 0, -1, 1, 1, 1 };
  // uniformes de nd_fs: alfa test que siempre pasa, reemplazar con el alfa de la textura, 512x512 de 32 texels/16 B
  static const float siempre[4] = { 1, 0, 0, 0 }, rep[4] = { 1, 1, 0, 0 }, sw[4] = { 512, 512, 32, 0 };
  void *u;
  SceGxmTexture tx;
  sceGxmTextureInitLinear(&tx, t4, SCE_GXM_TEXTURE_FORMAT_P4_ABGR, 512, 512, 0);
  sceGxmTextureSetPalette(&tx, pal);
  sceGxmTextureSetMinFilter(&tx, SCE_GXM_TEXTURE_FILTER_POINT); sceGxmTextureSetMagFilter(&tx, SCE_GXM_TEXTURE_FILTER_POINT);
  // cuadros: 0 la textura entera 1:1 en (0,0); 1 los texels 384-511 al doble en (520,0)
  TV v8[8] = { { 0, 0, .5f, 0, 0, ~0u }, { 512, 0, .5f, 1, 0, ~0u }, { 0, 512, .5f, 0, 1, ~0u }, { 512, 512, .5f, 1, 1, ~0u },
               { 520, 0, .5f, .75f, .75f, ~0u }, { 776, 0, .5f, 1, .75f, ~0u }, { 520, 256, .5f, .75f, 1, ~0u }, { 776, 256, .5f, 1, 1, ~0u } };
  sceClibMemcpy(q, v8, sizeof v8);
  uint32_t err[2][2] = { { 0, 0 }, { 0, 0 } }, prim[2] = { 0, 0 };
  for (uint32_t k = 0; k < 2; k++) {  // k = 1: la textura cambiada por DMA despues del primer dibujo
    prT4(cpu, k);
    sceDmacMemcpy(t4, cpu, 0x20000);
    sceGxmBeginScene(c, 0, rt, 0, 0, 0, &rts[1].cs, &ds);
    sceGxmSetVertexProgram(c, nvp); sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, n_wvp, 0, 16, wvp);
    sceGxmSetFragmentProgram(c, fragProg(2, 0));
    for (int r = 0; r < 2; r++) {
      sceGxmReserveFragmentDefaultUniformBuffer(c, &u);
      sceGxmSetUniformDataF(u, n_ats, 0, 4, siempre); sceGxmSetUniformDataF(u, n_tfs, 0, 4, rep); sceGxmSetUniformDataF(u, n_sws, 0, 4, sw);
      sceGxmSetFragmentTexture(c, 0, &tx);
      sceGxmSetVertexStream(c, 0, q + 4 * r);
      sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, tib, 4);
    }
    sceGxmEndScene(c, 0, 0);
    sceGxmFinish(c);
    const uint32_t *m = rts[1].mem;
    for (uint32_t y = 0; y < 512; y++)
      for (uint32_t x = 0; x < 776; x++) {
        int r = x < 512 ? 0 : x >= 520 && y < 256 ? 1 : -1;
        if (r < 0) continue;
        uint32_t tu = r ? 384 + (x - 520) / 2 : x, tv = r ? 384 + y / 2 : y, e = pal[prIx(tu, tv, k)] & 0xFFFFFF, g = m[y * LINE + x] & 0xFFFFFF;
        if (g != e) { if (!err[k][r]++ && !prim[k]) prim[k] = x | y << 16; }
      }
  }
  alog("traductor: prueba del sombreador con swizzle (T4 512x512): 1:1 %u y %u errores de 262144, al doble %u y %u de 65536 "
       "(antes / despues de cambiar la textura; primer error en %08x %08x)%s\n", err[0][0], err[1][0], err[0][1], err[1][1], prim[0], prim[1],
       err[0][0] | err[0][1] | err[1][0] | err[1][1] ? "" : ": bien");
}

// Dibuja el frame f del registro en la superficie s (cs[s]), en el hilo de Adrenaline. base: la imagen normal, que va
// debajo estirada (lo que el juego no redibuja queda como en el PSP); 0: fondo negro. Primero revisa todos los draws
// (trCheck, destinos y texturas sin copia): si alguno no se puede, devuelve el motivo sin dibujar nada. Despues abre
// una escena por cada cambio de destino (la superficie s o una imagen intermedia), arma textura, paleta (a 8888 en
// tpal), vertices (trVerts, en tv) y uniformes de cada draw, y lo dibuja. T_OK si quedo listo; si falla a medias
// devuelve el motivo y la superficie no se usa.
static int trFrame(SceGxmContext *c, RFrame *f, int s, const SceGxmTexture *base) {
  trPrueba(c);
  uint32_t disp = f->fb & 0x1FFFF0, ifb[2], nif = 0, en = 0;  // disp: imagen que se muestra; ifb: imagenes intermedias
  for (uint32_t i = 0; i < f->nd; i++) en += (RE(f->e[f->d[i].st], 0x9C) & 0x1FFFF0) == disp;
  // ningun draw a la imagen que se muestra (juego atrasado: el flip trae la otra del doble buffer): vale la del ultimo
  if (!en && f->nd) disp = RE(f->e[f->d[f->nd - 1].st], 0x9C) & 0x1FFFF0;
  for (uint32_t i = 0; i < f->nd; i++) {
    int r = trCheck(f, &f->d[i]);
    const uint32_t *e0 = f->e[f->d[i].st];
    uint32_t fb = RE(e0, 0x9C) & 0x1FFFF0, k;
    if (!r && fb != disp) {
      for (k = 0; k < nif && ifb[k] != fb; k++) {}
      if (k == nif) { if (nif < 2) ifb[nif++] = fb; else r = T_DESTINO; }
    }
    if (!r && !(RE(e0, 0xD3) & 1) && (RE(e0, 0x1E) & 1) && (f->d[i].vt & 3) && !f->d[i].toff) {  // textura sin copia
      uint32_t ta = texAddr(e0);
      // solo vale si esta en la VRAM (0x04000000) y es una imagen intermedia de un draw anterior de este frame
      for (k = 0; k < nif && !((ta & 0x0F000000) == 0x04000000 && (ta & 0x1FFFF0) == ifb[k]); k++) {}
      if (k == nif) r = T_TEX;
    }
    if (r) {
      if (!t_vis[r]++) {
        const uint32_t *e = f->e[f->d[i].st];
        alog("traductor: primer frame con '%s': draw %u de %u prim %u vt %06x tex %u fmt %u swz %u mezcla %u (%04x %06x %06x) alfa %u "
             "tfunc %06x fb %06x/%u (se muestra %07x/%u) clear %u ctest %u logica %u\n", tnom[r], i, f->nd, f->d[i].prim, f->d[i].vt,
             RE(e, 0x1E) & 1, RE(e, 0xC3) & 15, RE(e, 0xC2) & 1, RE(e, 0x21) & 1, RE(e, 0xDF) & 0xFFFF, RE(e, 0xE0) & 0xFFFFFF,
             RE(e, 0xE1) & 0xFFFFFF, RE(e, 0x22) & 1, RE(e, 0xC9) & 0xFFFFFF, RE(e, 0x9C) & 0xFFFFFF, RE(e, 0x9D) & 0x7FF, f->fb,
             f->fbw & 0xFFFF, RE(e, 0xD3) & 0xFFFF, RE(e, 0x27) & 1, RE(e, 0x28) & 1);
      }
      return r;
    }
  }
  static uint32_t t_q[2];  // ultima escena que leyo los vertices y paletas de cada superficie
  // si la GPU todavia los lee (escena sin terminar) se espera
  if (gOcupada(t_q[s])) { g_psup++; gEv("vertices y paletas reescritos", t_q[s]); gEspera(t_q[s]); }
  TV *o = tv[s];
  uint32_t *pal = tpal[s], np = 0, nv = 0, pant = ~0u, pn = 0;  // np: palabras usadas; pant/pn: la paleta anterior
  void *u;
  // matriz pixeles -> pantalla, por columnas (como la del presentador)
  static const float wvp[16] = { 2.0f / LINE, 0, 0, 0, 0, -2.0f / H, 0, 0, 0, 0, -2, 0, -1, 1, 1, 1 };
  static const float siempre[4] = { 1, 0, 0, 0 };  // alfa test: siempre pasa
  // ESCENA: empieza una escena en la superficie sup con el programa de vertices y la matriz
#define ESCENA(sup) do { sceGxmBeginScene(c, 0, rt, 0, 0, 0, sup, &ds); sceGxmSetVertexProgram(c, nvp); \
    sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, n_wvp, 0, 16, wvp); } while (0)
  RT *cur_rt = 0;  // destino de la escena abierta (0: la superficie que se muestra)
  uint32_t d0 = t_draws;  // draws al empezar la escena (cronometro)
  ESCENA(&cs[s]);
  if (base) {  // debajo, la imagen normal estirada con el programa del presentador
    sceGxmSetVertexProgram(c, vp); sceGxmSetFragmentProgram(c, fp);
    sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, p_wvp, 0, 16, wvp);
    sceGxmSetFragmentTexture(c, 0, base); sceGxmSetVertexStream(c, 0, vb);
    sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
    sceGxmSetVertexProgram(c, nvp); sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, n_wvp, 0, 16, wvp);
  } else {  // borrar: negro de pantalla completa
    TV q[4] = { { 0, 0, .5f, 0, 0, 0xFF000000u }, { W, 0, .5f, 0, 0, 0xFF000000u }, { 0, H, .5f, 0, 0, 0xFF000000u }, { W, H, .5f, 0, 0, 0xFF000000u } };
    sceClibMemcpy(o, q, sizeof q); nv = 4;
    sceGxmSetFragmentProgram(c, fragProg(0, 0));
    sceGxmReserveFragmentDefaultUniformBuffer(c, &u);
    sceGxmSetUniformDataF(u, n_atc, 0, 4, siempre);
    sceGxmSetVertexStream(c, 0, o);
    sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, tib, 4);
  }
  // formato de textura del GE (0xC3) -> GXM
  static const SceGxmTextureFormat gf[6] = { SCE_GXM_TEXTURE_FORMAT_U5U6U5_BGR, SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR,
    SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ABGR, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, SCE_GXM_TEXTURE_FORMAT_P4_ABGR,
    SCE_GXM_TEXTURE_FORMAT_P8_ABGR };
  int res = T_OK;
  for (uint32_t i = 0; i < f->nd && res == T_OK; i++) {
    RDraw *d = &f->d[i];
    const uint32_t *e = f->e[d->st];
    uint32_t clr = RE(e, 0xD3), tex = !(clr & 1) && (RE(e, 0x1E) & 1) && (d->vt & 3), fb = RE(e, 0x9C) & 0x1FFFF0;
    RT *dst = fb == disp ? 0 : rtDe(fb, f->frame, 1);
    if (fb != disp && !dst) { res = T_DESTINO; break; }
    if (dst != cur_rt) {
      sceGxmEndScene(c, 0, gFin()); cAnota(cur_rt ? C_INT : C_TR, &gq_n, t_draws - d0); d0 = t_draws;
      ESCENA(dst ? &dst->cs : &cs[s]); cur_rt = dst;
    }
    if ((clr & 1) && !(clr >> 8 & 1)) continue;  // borrado solo de profundidad/stencil: nada que ver
    uint32_t key = clr & 1 ? 0 : blendKey(e), mat = (RE(e, 0x55) & 0xFFFFFF) | (RE(e, 0x56) & 0xFF) << 24;
    float tw = 1, th = 1, sw[4] = { 0, 0, 0, 0 };
    SceGxmTexture t;
    if (tex) {
      uint32_t fmt = RE(e, 0xC3) & 15, a8 = RE(e, 0xA8), b8 = RE(e, 0xB8), tbw = a8 & 0x7FF;
      th = 1u << (b8 >> 8 & 15); tw = tbw;
      if (d->toff && (RE(e, 0xC2) & 1)) {  // con swizzle: la copia esta tal cual; el sombreador nd_fs busca cada texel
        static const uint8_t tpb[6] = { 8, 8, 8, 4, 32, 16 };  // texels por 16 bytes de cada formato
        if (th < 8) th = 8;
        sw[0] = tw; sw[1] = th; sw[2] = tpb[fmt];
        tex = 2;
      }
      RT *src = d->toff ? 0 : rtDe(texAddr(e) & 0x1FFFF0, f->frame, 0);
      if (src) {  // imagen intermedia de este frame: su superficie al doble (512 x 272 del PSP -> 1024 x 544)
        if (sceGxmTextureInitLinear(&t, src->mem, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, LINE, H, 0) < 0) { res = T_TEX; break; }
        tw = LINE / 2; th = H / 2; fmt = 3;
      } else if (!d->toff || sceGxmTextureInitLinear(&t, f->tx + d->toff - 1, gf[fmt], tbw, (uint32_t)th, 0) < 0) { res = T_TEX; break; }
      if (fmt >= 4) {  // paleta: a 8888, con desplazamiento, mascara y comienzo (0xC5)
        uint32_t c5 = RE(e, 0xC5), cf = c5 & 3, sh = c5 >> 2 & 31, mk = c5 >> 8 & 0xFF, st = (c5 >> 16 & 31) << 4;
        uint32_t ne = fmt == 4 ? 16 : 256, bpe = cf == 3 ? 4 : 2, max = d->clen / bpe, *pp = pal + np;
        if ((np + ne) * 4 > TP_B) { res = T_TOPE; break; }
        const uint8_t *src = f->a + d->clut - 1;
        for (uint32_t k = 0; k < ne; k++) {
          uint32_t j = ((k >> sh) & mk) | st;
          pp[k] = pal8888(j < max ? (bpe == 4 ? ((const uint32_t *)src)[j] : ((const uint16_t *)src)[j]) : 0, cf);
        }
        if (pn == ne && !sceClibMemcmp(pal + pant, pp, ne * 4)) pp = pal + pant;  // igual a la anterior: se reusa
        else { pant = np; pn = ne; np += ne; }
        sceGxmTextureSetPalette(&t, pp);
      }
      uint32_t tfl = RE(e, 0xC6), twr = RE(e, 0xC7);
      // filtro y repeticion del GE (0xC6, 0xC7); con swizzle, siempre sin filtro (nd_fs busca un solo texel)
      sceGxmTextureSetMinFilter(&t, (tfl & 7) == 1 && tex != 2 ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT);
      sceGxmTextureSetMagFilter(&t, (tfl >> 8 & 1) && tex != 2 ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT);
      sceGxmTextureSetUAddrMode(&t, twr & 1 ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
      sceGxmTextureSetVAddrMode(&t, twr >> 8 & 1 ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
    }
    uint32_t n = 0, d4 = RE(e, 0xD4), d5 = RE(e, 0xD5);
    float sc[4] = { 2.0f * (d4 & 0x3FF), 2.0f * (d4 >> 10 & 0x3FF), 2.0f * ((d5 & 0x3FF) + 1), 2.0f * ((d5 >> 10 & 0x3FF) + 1) };
    if (!(d5 & 0xFFFFF)) sc[0] = sc[1] = -1e9f, sc[2] = sc[3] = 1e9f;  // (sin scissor guardado: no se recorta)
    if ((res = trVerts(f, d, o + nv, tw, th, mat, sc, &n))) break;
    if (!n) continue;
    if (nv + n > TV_N) { res = T_TOPE; break; }
    SceGxmFragmentProgram *p = fragProg(tex, key);
    if (!p) { res = T_PROG; break; }
    sceGxmSetFragmentProgram(c, p);
    sceGxmReserveFragmentDefaultUniformBuffer(c, &u);
    // uniformes (ver sombreadores/*.cg): at = funcion del alfa test (1: siempre pasa) y referencia; tf = funcion de
    // textura, TCC y color al doble
    uint32_t db = RE(e, 0xDB), c9 = RE(e, 0xC9);
    float at[4] = { (clr & 1) || !(RE(e, 0x22) & 1) ? 1.0f : (float)(db & 7), (float)(db >> 8 & 0xFF), 0, 0 };
    sceGxmSetUniformDataF(u, tex == 2 ? n_ats : tex ? n_atx : n_atc, 0, 4, at);
    if (tex) {
      float tf[4] = { (c9 & 7) == 3 ? 1.0f : 0.0f, (float)(c9 >> 8 & 1), (float)(c9 >> 16 & 1), 0 };
      sceGxmSetUniformDataF(u, tex == 2 ? n_tfs : n_tf, 0, 4, tf);
      if (tex == 2) sceGxmSetUniformDataF(u, n_sws, 0, 4, sw);
      sceGxmSetFragmentTexture(c, 0, &t);
    }
    sceGxmSetVertexStream(c, 0, o + nv);
    sceGxmDraw(c, d->prim == 3 ? SCE_GXM_PRIMITIVE_TRIANGLES : SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, tib, n);
    nv += n; t_draws++;
  }
  sceGxmEndScene(c, 0, gFin());  // (si algo fallo a medias, la superficie queda con lo dibujado: no se usa)
  cAnota(cur_rt ? C_INT : C_TR, &gq_n, t_draws - d0);
  t_q[s] = gq;
  if (res && !t_vis[res]++) alog("traductor: primer frame con '%s' al dibujar\n", tnom[res]);
  return res;
}
