// traductor.c: draws with the Vita GPU (GXM), at double size (960x544), a frame of the record (registro.h: GE draws and
// states saved by registro.c in the capturer). Included inside presentador.c, it uses its patcher, surfaces and context;
// the presenter calls trInit at startup and trFrame in the Adrenaline thread, instead of copying the PSP image. If the
// frame has something it cannot draw it returns the reason (T_*) and the presenter shows the normal image.
// Own shaders in sombreadores/*.cg.
#include "registro.h"
#include "sombreadores_nd.h"
// Result of trCheck/trFrame: T_OK or the reason the frame was not drawn (tnames: its name in the log)
enum { T_OK, T_NEW, T_3D, T_PRIM, T_FMT, T_BLEND, T_TFUNC, T_ODD, T_DEST, T_VERT, T_LIMIT, T_TEX, T_PROG, T_OUT, TN };
static const char *tnames[TN] = { "ok", "sin_frame_nuevo", "3d", "primitiva", "formato", "mezcla", "funcion_textura",
                                "test_color_o_logica", "otro_destino", "vertice", "tope", "textura_mal", "programa", "vertice_fuera" };
// counters for the 10 s log (read and cleared by the presenter)
static volatile uint32_t t_cnt[TN], t_hd, t_nat, t_draws;
static volatile uint32_t t_skip[6];  // PSP frames advanced by each drawn frame (0: repeated, 5: 5 or more)
static uint32_t t_seen[TN];   // times each reason was seen (the first one is logged in detail)
typedef struct { float x, y, z, u, v; uint32_t c; } TV;  // translated vertex (24 bytes, the nd_v format)
static TV *tv[2];            // translated vertices, one per surface
static uint32_t *tpal[2];    // translated palettes (8888), aligned to 64: 16 entries (64 bytes) or 256 (1 KB)
#define TV_N (64 << 10)      // vertices per surface (and limit of the 16-bit indices)
#define TP_B (256 << 10)     // palette bytes per surface
// last record frame drawn, whether it came out fine and how many times it was repeated
static uint32_t t_frame = ~0u, t_ok, t_rep;
static uint16_t *tib;        // 0, 1, 2... (indices for the GPU)
// Intermediate image: a game draw to another VRAM image (not the shown one) goes to our own double-size surface
// (1024x544); if later in the same frame a texture is at that address, it is read from that surface.
// fb: image address in VRAM (~0u: free); fr: last frame it was used.
typedef struct { uint32_t fb, fr; void *mem; SceGxmColorSurface cs; } RT;
static RT rts[2];
// Reserves the surface of an intermediate image (alpha 255, like the main surface). Both are reserved in trInit,
// before the dump: otherwise the dump takes the physical memory.
static int rtMem(RT *r) {
  if (!(r->mem = gmem1(PHY, LINE * H * 4, RW))) { alog("traductor: sin memoria para una imagen intermedia\n"); return 0; }
  for (uint32_t k = 0; k < LINE * H; k++) ((uint32_t *)r->mem)[k] = 0xFF000000u;  // (alpha is never written)
  sceGxmColorSurfaceInit(&r->cs, SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                         SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, LINE, H, LINE, r->mem);
  r->fb = ~0u;
  return 1;
}
// The intermediate image of image fb (VRAM offset), marked as used in frame. If none and create, takes one not used
// in this frame (reserving it if needed). 0 if none.
static RT *rtOf(uint32_t fb, uint32_t frame, int create) {
  for (int i = 0; i < 2; i++) if (rts[i].mem && rts[i].fb == fb) { rts[i].fr = frame; return &rts[i]; }
  if (!create) return 0;
  for (int i = 0; i < 2; i++) {
    RT *r = &rts[i];
    if (r->mem && r->fr == frame) continue;  // (already used by another image of this frame)
    if (!r->mem && !rtMem(r)) return 0;
    if (r->fb == ~0u) alog("traductor: imagen intermedia %d (%s) para %06x\n", i, ty(r->mem), fb);
    r->fb = fb; r->fr = frame;
    return r;
  }
  return 0;
}
// Address of texture 0 of state e (GE registers 0xA0 and 0xA8)
static uint32_t texAddr(const uint32_t *e) { return (RE(e, 0xA0) & 0xFFFFF0) | ((RE(e, 0xA8) << 8) & 0x0F000000); }

// --- programs: one vertex program (nd_v) and two fragment programs: nd_fc without texture and nd_ft with texture
// (swizzled ones arrive already sorted from the record). Each fragment program is built once per blend that appears
// (fps, up to 64) ---
static SceGxmVertexProgram *nvp;
static const SceGxmProgram *pft, *pfc, *pv;
static SceGxmShaderPatcherId ift, ifc;
static const SceGxmProgramParameter *n_wvp, *n_tf, *n_atx, *n_atc;
typedef struct { uint32_t key; SceGxmFragmentProgram *p; } FP;
static FP fps[64];
static uint32_t nfps;
// Fragment program for tex (0 no texture, 1 textured) and key (0 no blending; otherwise the 4 bytes of
// SceGxmBlendInfo + 1, see blendKey). Finds it in fps or creates it with the patcher (no alpha write); 0 on failure.
static SceGxmFragmentProgram *fragProg(int tex, uint32_t key) {
  uint32_t k = key << 1 | tex;
  for (uint32_t i = 0; i < nfps; i++) if (fps[i].key == k) return fps[i].p;
  if (nfps >= 64) return 0;
  SceGxmBlendInfo b;
  if (key) sceClibMemcpy(&b, &(uint32_t){ key - 1 }, 4);
  else { sceClibMemset(&b, 0, sizeof b); b.colorFunc = b.alphaFunc = SCE_GXM_BLEND_FUNC_NONE; }
  b.colorMask = SCE_GXM_COLOR_MASK_R | SCE_GXM_COLOR_MASK_G | SCE_GXM_COLOR_MASK_B;  // (PSP alpha = its stencil)
  SceGxmFragmentProgram *p = 0;
  int r = sceGxmShaderPatcherCreateFragmentProgram(pat, tex ? ift : ifc, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                   SCE_GXM_MULTISAMPLE_NONE, &b, pv, &p);
  if (r < 0) { alog("traductor: programa de fragmentos %u %08x: %08x\n", tex, key, r); return 0; }
  fps[nfps].key = k; fps[nfps++].p = p;
  return p;
}

// Prepares the translator (called by presStart, only with dibujar): vertex, palette and index memory, registers the
// shaders and finds their parameters, creates the basic programs, reserves the intermediate images and lets the GPU
// read the PSP RAM and the record textures. 0 if ready; negative otherwise.
static int trInit(void) {
  for (int i = 0; i < 2; i++)
    if (!(tv[i] = gmem(TV_N * sizeof(TV), RD)) || !(tpal[i] = gmem(TP_B, RD))) return -1;
  if (!(tib = gmem(TV_N * 2, RD))) return -1;
  for (uint32_t i = 0; i < TV_N; i++) tib[i] = i;
  SceGxmShaderPatcherId iv;
  int r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_v_gxp, &iv);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_ft_gxp, &ift);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_fc_gxp, &ifc);
  if (r < 0) { alog("traductor: registrar sombreadores %08x\n", r); return r; }
  pv = sceGxmShaderPatcherGetProgramFromId(iv); pft = sceGxmShaderPatcherGetProgramFromId(ift); pfc = sceGxmShaderPatcherGetProgramFromId(ifc);
  const SceGxmProgramParameter *pa = sceGxmProgramFindParameterByName(pv, "aPosition"), *pt = sceGxmProgramFindParameterByName(pv, "aTexcoord"),
                               *pc = sceGxmProgramFindParameterByName(pv, "aColor");
  n_wvp = sceGxmProgramFindParameterByName(pv, "wvp");
  n_tf = sceGxmProgramFindParameterByName(pft, "tf"); n_atx = sceGxmProgramFindParameterByName(pft, "at");
  n_atc = sceGxmProgramFindParameterByName(pfc, "at");
  if (!pa || !pt || !pc || !n_wvp || !n_tf || !n_atx || !n_atc) { alog("traductor: parametros de los sombreadores no encontrados\n"); return -2; }
  SceGxmVertexAttribute at[3] = { { 0, 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 3, sceGxmProgramParameterGetResourceIndex(pa) },
                                  { 0, 12, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2, sceGxmProgramParameterGetResourceIndex(pt) },
                                  { 0, 20, SCE_GXM_ATTRIBUTE_FORMAT_U8N, 4, sceGxmProgramParameterGetResourceIndex(pc) } };
  SceGxmVertexStream st = { sizeof(TV), SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
  if ((r = sceGxmShaderPatcherCreateVertexProgram(pat, iv, at, 3, &st, 1, &nvp)) < 0) { alog("traductor: programa de vertices %08x\n", r); return r; }
  if (!fragProg(0, 0) || !fragProg(1, 0)) return -3;
  for (int i = 0; i < 2; i++) rtMem(&rts[i]);
  // PSP RAM (0x08000000-0x0A000000) for the GPU (fails with 0x72000000)
  int r1 = sceGxmMapMemory((void *)0x70000000, 0x2000000, RD);
  int r3 = regMapGpu();
  alog("traductor: RAM del PSP para la GPU %08x, texturas del registro %08x\n", r1, r3);
  return r3 < 0 ? r3 : 0;
}

// PSP color c in format f (0 5650, 1 5551, 2 4444, 3 8888; R in the low bits) -> 0xAABBGGRR (channels scaled to 0-255)
static uint32_t pal8888(uint32_t c, uint32_t f) {
  uint32_t r, g, b, a;
  if (f == 3) return c;
  if (f == 0) { r = c & 31; g = c >> 5 & 63; b = c >> 11 & 31; return 0xFF000000u | (b * 255 / 31) << 16 | (g * 255 / 63) << 8 | r * 255 / 31; }
  if (f == 1) { r = c & 31; g = c >> 5 & 31; b = c >> 10 & 31; a = c >> 15 ? 255 : 0; return a << 24 | (b * 255 / 31) << 16 | (g * 255 / 31) << 8 | r * 255 / 31; }
  r = c & 15; g = c >> 4 & 15; b = c >> 8 & 15; a = c >> 12 & 15;
  return (a * 17) << 24 | (b * 17) << 16 | (g * 17) << 8 | r * 17;
}

// GE blend of state e (0x21 enable, 0xDF factors and operation, 0xE0/0xE1 fixed colors) -> the 4 bytes of
// SceGxmBlendInfo + 1 (0: no blending); ~0u if it has no equivalent. Fixed colors only work if 0 or white
// (factor ZERO or ONE); destination alpha is not computed (not written).
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

// Checks whether draw d of frame f can be drawn (through mode, primitive, vertices without weights, destination, blend,
// texture format and function). T_OK if so; otherwise the reason. What depends on other draws is checked by trFrame.
static int trCheck(RFrame *f, RDraw *d) {
  const uint32_t *e = f->e[d->st];
  uint32_t vt = d->vt, clr = RE(e, 0xD3) & 1;
  if (!(vt >> 23 & 1)) return T_3D;
  if (d->prim != 3 && d->prim != 4 && d->prim != 6) return T_PRIM;  // triangles, strip, rectangles
  if ((vt >> 9 & 3) || (vt >> 18 & 7)) return T_VERT;  // weights or morph
  if ((RE(e, 0x9D) & 0x7FF) > 512) return T_DEST;
  if (clr) return T_OK;
  if (RE(e, 0x27) & 1 || RE(e, 0x28) & 1) return T_ODD;
  if (blendKey(e) == ~0u) return T_BLEND;
  if ((RE(e, 0x1E) & 1) && (vt & 3)) {
    uint32_t fmt = RE(e, 0xC3) & 15, tf = RE(e, 0xC9) & 7;
    if (fmt > 5 || (fmt >= 4 && !d->clut)) return T_FMT;
    if (tf != 0 && tf != 3) return T_TFUNC;
    // (texture with no copy in the record: trFrame checks whether it is an intermediate image of this frame)
  }
  return T_OK;
}

// Color of a vertex at p with GE color type ct (4 5650, 5 5551, 6 4444, 7 8888) -> 0xAABBGGRR
static uint32_t col8888(const uint8_t *p, uint32_t ct) {
  if (ct == 7) return *(const uint32_t *)p;
  return pal8888(*(const uint16_t *)p, ct - 4);
}

// Decodes the vertices of draw d (through mode) to o: computes the size and position of each field from the GE vertex
// type (with its alignment), follows the indices if any, and leaves x, y doubled and u, v divided by tw x th.
// col0: color if the vertex has none; sc: doubled scissor (x0, y0, x1, y1). Rectangles (2 corners) are clipped to the
// scissor and come out as 4-vertex strips. *nout: vertices written. T_OK, T_VERT (data outside the record), T_LIMIT or
// T_OUT (position outside +-4096: garbage vertices).
static int trVerts(RFrame *f, RDraw *d, TV *o, float tw, float th, uint32_t col0, const float *sc, uint32_t *nout) {
  static const uint8_t s4[4] = { 0, 1, 2, 4 }, cs[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
  uint32_t vt = d->vt, tc = vt & 3, ct = vt >> 2 & 7, nt = vt >> 5 & 3, pt = vt >> 7 & 3, sz = 0, al = 1, s;
  uint32_t otc = 0, ocol = 0, opos = 0, dummy __attribute__((unused));
  // FLD: n components of size bytes, aligned to size (o: offset). Order: texture, color, normal, position
#define FLD(n, size, o) s = (size); if (s) { sz = (sz + s - 1) & ~(s - 1); o = sz; sz += (n) * s; if (s > al) al = s; }
  FLD(2, s4[tc], otc); FLD(1, cs[ct], ocol); FLD(3, s4[nt], dummy); FLD(3, s4[pt], opos);
#undef FLD
  sz = (sz + al - 1) & ~(al - 1);
  if (!sz || !pt) return T_VERT;
  const uint8_t *vb = f->a + d->voff - 1, *ib = d->ioff ? f->a + d->ioff - 1 : 0;  // (offsets + 1; 0: absent)
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
    // position outside what the GE draws (or NaN): data read late, when the game was already rewriting it
    if (!(q.x > -4096 && q.x < 4096 && q.y > -4096 && q.y < 4096)) return T_OUT;
    q.x *= 2; q.y *= 2; q.u /= tw; q.v /= th;
    if (d->prim == 6) {  // rectangle: 2 vertices (opposite corners) -> strip of 4, separated from the next with two
      r[i & 1] = q;      // repeated vertices; the color is the second vertex's (like the GE)
      if (i & 1) {
        if (n + 6 > TV_N) return T_LIMIT;
        for (int axis = 0; axis < 2; axis++) {  // scissor clipping in x and y; u and v are clipped proportionally
          float *a0 = axis ? &r[0].y : &r[0].x, *a1 = axis ? &r[1].y : &r[1].x, *t0 = axis ? &r[0].v : &r[0].u, *t1 = axis ? &r[1].v : &r[1].u;
          float lo = sc[axis], hi = sc[axis + 2], d = *a1 - *a0, p0 = *a0, q0 = *t0, k = d != 0 ? (*t1 - *t0) / d : 0;
          if (d >= 0) { if (*a0 < lo) *a0 = lo; if (*a1 > hi) *a1 = hi; if (*a0 >= *a1) goto outside; }
          else { if (*a1 < lo) *a1 = lo; if (*a0 > hi) *a0 = hi; if (*a1 >= *a0) goto outside; }
          *t0 = q0 + (*a0 - p0) * k; *t1 = q0 + (*a1 - p0) * k;
        }
        uint32_t c = r[1].c;
        TV a = { r[0].x, r[0].y, .5f, r[0].u, r[0].v, c }, b = { r[1].x, r[0].y, .5f, r[1].u, r[0].v, c },
           cc = { r[0].x, r[1].y, .5f, r[0].u, r[1].v, c }, dd = { r[1].x, r[1].y, .5f, r[1].u, r[1].v, c };
        o[n++] = a; o[n++] = a; o[n++] = b; o[n++] = cc; o[n++] = dd; o[n++] = dd;
      }
    outside:;
    } else {
      if (n + 1 > TV_N) return T_LIMIT;
      o[n++] = q;
    }
  }
  *nout = n;
  return T_OK;
}

// Pixels covered by the n translated vertices of a draw (rectangles: each one; the rest: their bounding
// rectangle). For the timer.
static float trArea(const TV *o, uint32_t n, uint32_t prim) {
  float a = 0;
  if (prim == 6) {
    for (uint32_t j = 0; j + 6 <= n; j += 6) { float w = o[j + 5].x - o[j].x, h = o[j + 5].y - o[j].y; a += (w < 0 ? -w : w) * (h < 0 ? -h : h); }
    return a;
  }
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (uint32_t j = 0; j < n; j++) {
    x0 = o[j].x < x0 ? o[j].x : x0; x1 = o[j].x > x1 ? o[j].x : x1;
    y0 = o[j].y < y0 ? o[j].y : y0; y1 = o[j].y > y1 ? o[j].y : y1;
  }
  return n ? (x1 - x0) * (y1 - y0) : 0;
}

// pixels -> screen matrix, by columns (like the presenter's), and alpha test that always passes
static const float tr_wvp[16] = { 2.0f / LINE, 0, 0, 0, 0, -2.0f / H, 0, 0, 0, 0, -2, 0, -1, 1, 1, 1 }, always[4] = { 1, 0, 0, 0 };
// What goes underneath in the surface (scene already open): the normal image base stretched, with the presenter's
// program; without base, full-screen black (4 vertices at o + nv). Returns the vertices used from o.
static uint32_t trBase(SceGxmContext *c, const SceGxmTexture *base, TV *o, uint32_t nv) {
  void *u;
  if (base) {
    sceGxmSetVertexProgram(c, vp); sceGxmSetFragmentProgram(c, fp);
    sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, p_wvp, 0, 16, tr_wvp);
    sceGxmSetFragmentTexture(c, 0, base); sceGxmSetVertexStream(c, 0, vb);
    sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
    sceGxmSetVertexProgram(c, nvp); sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, n_wvp, 0, 16, tr_wvp);
    return nv;
  }
  TV q[4] = { { 0, 0, .5f, 0, 0, 0xFF000000u }, { W, 0, .5f, 0, 0, 0xFF000000u }, { 0, H, .5f, 0, 0, 0xFF000000u }, { W, H, .5f, 0, 0, 0xFF000000u } };
  sceClibMemcpy(o + nv, q, sizeof q);
  sceGxmSetFragmentProgram(c, fragProg(0, 0));
  sceGxmReserveFragmentDefaultUniformBuffer(c, &u);
  sceGxmSetUniformDataF(u, n_atc, 0, 4, always);
  sceGxmSetVertexStream(c, 0, o + nv);
  sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, tib, 4);
  return nv + 4;
}

// Draws frame f of the record to surface s (cs[s]), in the Adrenaline thread. base: the normal image, which goes
// underneath stretched (what the game does not redraw stays as on the PSP); 0: black background. First checks all draws
// (trCheck, destinations and textures with no copy): if one fails, returns the reason without drawing. Then opens
// one scene per destination change (surface s or an intermediate image; if possible, all intermediates first and the
// surface last, in a single scene: each scene reads and writes its whole surface), sets up texture, palette (to 8888 in
// tpal), vertices (trVerts, in tv) and uniforms of each draw, and draws it. T_OK if ready; if it fails midway
// returns the reason and the surface is not used.
static int trFrame(SceGxmContext *c, RFrame *f, int s, const SceGxmTexture *base) {
  uint32_t disp = f->fb & 0x1FFFF0, ifb[2], nif = 0, n_disp = 0;  // disp: image being shown; ifb: intermediate images
  // ord: intermediate images first and then, in a single scene, the surface (see trFrame). Valid only if just the
  // draws to the surface read intermediate images and none is drawn after being read (rd_mask: bit per image)
  uint32_t ord = 1, rd_mask = 0;
  for (uint32_t i = 0; i < f->nd; i++) n_disp += (RE(f->e[f->d[i].st], 0x9C) & 0x1FFFF0) == disp;
  // no draw to the shown image (game behind: the flip has the other one of the double buffer): use the last draw's one
  if (!n_disp && f->nd) disp = RE(f->e[f->d[f->nd - 1].st], 0x9C) & 0x1FFFF0;
  for (uint32_t i = 0; i < f->nd; i++) {
    int r = trCheck(f, &f->d[i]);
    const uint32_t *e0 = f->e[f->d[i].st];
    uint32_t fb = RE(e0, 0x9C) & 0x1FFFF0, k;
    int kd = -1;  // destination intermediate image (-1: the surface)
    if (!r && fb != disp) {
      for (k = 0; k < nif && ifb[k] != fb; k++) {}
      if (k == nif) { if (nif < 2) ifb[nif++] = fb; else r = T_DEST; }
      kd = k;
      if (rd_mask >> k & 1) ord = 0;
    }
    if (!r && !(RE(e0, 0xD3) & 1) && (RE(e0, 0x1E) & 1) && (f->d[i].vt & 3) && !f->d[i].toff) {  // texture with no copy
      uint32_t ta = texAddr(e0);
      // only valid if in VRAM (0x04000000) and an intermediate image of an earlier draw of this frame
      for (k = 0; k < nif && !((ta & 0x0F000000) == 0x04000000 && (ta & 0x1FFFF0) == ifb[k]); k++) {}
      if (k == nif) r = T_TEX;
      else { rd_mask |= 1u << k; if (kd >= 0) ord = 0; }
    }
    if (r) {
      if (!t_seen[r]++) {
        const uint32_t *e = f->e[f->d[i].st];
        alog("traductor: primer frame con '%s': draw %u de %u prim %u vt %06x tex %u fmt %u swz %u mezcla %u (%04x %06x %06x) alfa %u "
             "tfunc %06x fb %06x/%u (se muestra %07x/%u) clear %u ctest %u logica %u\n", tnames[r], i, f->nd, f->d[i].prim, f->d[i].vt,
             RE(e, 0x1E) & 1, RE(e, 0xC3) & 15, RE(e, 0xC2) & 1, RE(e, 0x21) & 1, RE(e, 0xDF) & 0xFFFF, RE(e, 0xE0) & 0xFFFFFF,
             RE(e, 0xE1) & 0xFFFFFF, RE(e, 0x22) & 1, RE(e, 0xC9) & 0xFFFFFF, RE(e, 0x9C) & 0xFFFFFF, RE(e, 0x9D) & 0x7FF, f->fb,
             f->fbw & 0xFFFF, RE(e, 0xD3) & 0xFFFF, RE(e, 0x27) & 1, RE(e, 0x28) & 1);
      }
      return r;
    }
  }
  static uint32_t t_q[2];  // last scene that read the vertices and palettes of each surface
  // if the GPU is still reading them (unfinished scene) it waits
  if (gBusy(t_q[s])) { g_psurf++; gEv("vertices y paletas reescritos", t_q[s]); gWait(t_q[s]); }
  TV *o = tv[s];
  uint32_t *pal = tpal[s], np = 0, nv = 0, pal_prev = ~0u, pn = 0;  // np: words used; pal_prev/pn: the previous palette
  void *u;
  // SCENE: starts a scene on surface surf_tex with the vertex program and the matrix
#define SCENE(surf_tex) do { sceGxmBeginScene(c, 0, rt, 0, 0, 0, surf_tex, &ds); sceGxmSetVertexProgram(c, nvp); \
    sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, n_wvp, 0, 16, tr_wvp); } while (0)
  // OPEN: closes the open scene (if any) and opens one on dst (0: surface s); the first time on the surface,
  // puts the normal image (or black: trBase) underneath
#define OPEN(dst_) do { if (opened) { sceGxmEndScene(c, 0, gEnd()); cNote(cur_rt ? C_INT : C_TR, &gq_n, t_draws - d0, spx / 1000, ssw); } \
    d0 = t_draws; spx = 0; ssw = 0; cur_rt = (dst_); opened = 1; SCENE(cur_rt ? &cur_rt->cs : &cs[s]); \
    if (!cur_rt && !has_base) { has_base = 1; nv = trBase(c, base, o, nv); spx += W * H; } } while (0)
  RT *cur_rt = 0;  // destination of the open scene (0: the shown surface)
  int opened = 0, has_base = 0;
  uint32_t d0 = t_draws, ssw = 0;  // timer: draws at scene start, swizzled draws and scene pixels
  float spx = 0;
  if (!ord) OPEN(0);  // (in game order: the surface first, as on the PSP)
  // GE texture format (0xC3) -> GXM
  static const SceGxmTextureFormat gf[6] = { SCE_GXM_TEXTURE_FORMAT_U5U6U5_BGR, SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR,
    SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ABGR, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, SCE_GXM_TEXTURE_FORMAT_P4_ABGR,
    SCE_GXM_TEXTURE_FORMAT_P8_ABGR };
  int res = T_OK;
  // with ord, two passes: 0 draws to intermediate images, 1 those to the surface
  for (uint32_t pass = 0; pass <= ord && res == T_OK; pass++)
  for (uint32_t i = 0; i < f->nd && res == T_OK; i++) {
    RDraw *d = &f->d[i];
    const uint32_t *e = f->e[d->st];
    uint32_t clr = RE(e, 0xD3), tex = !(clr & 1) && (RE(e, 0x1E) & 1) && (d->vt & 3), fb = RE(e, 0x9C) & 0x1FFFF0;
    RT *dst = fb == disp ? 0 : rtOf(fb, f->frame, 1);
    if (fb != disp && !dst) { res = T_DEST; break; }
    if (ord && (pass ? dst != 0 : dst == 0)) continue;
    if (!opened || dst != cur_rt) OPEN(dst);
    if ((clr & 1) && !(clr >> 8 & 1)) continue;  // depth/stencil-only clear: nothing to see
    uint32_t key = clr & 1 ? 0 : blendKey(e), mat = (RE(e, 0x55) & 0xFFFFFF) | (RE(e, 0x56) & 0xFF) << 24;
    float tw = 1, th = 1;
    SceGxmTexture t;
    if (tex) {
      uint32_t fmt = RE(e, 0xC3) & 15, a8 = RE(e, 0xA8), b8 = RE(e, 0xB8), tbw = a8 & 0x7FF;
      th = 1u << (b8 >> 8 & 15); tw = tbw;
      RT *src = d->toff ? 0 : rtOf(texAddr(e) & 0x1FFFF0, f->frame, 0);
      if (src) {  // intermediate image of this frame: its double-size surface (512 x 272 of the PSP -> 1024 x 544)
        if (sceGxmTextureInitLinear(&t, src->mem, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, LINE, H, 0) < 0) { res = T_TEX; break; }
        tw = LINE / 2; th = H / 2; fmt = 3;
      } else if (!d->toff || sceGxmTextureInitLinear(&t, f->tx + d->toff - 1, gf[fmt], tbw, (uint32_t)th, 0) < 0) { res = T_TEX; break; }
      if (fmt >= 4) {  // palette: to 8888, with shift, mask and start (0xC5)
        uint32_t c5 = RE(e, 0xC5), cf = c5 & 3, sh = c5 >> 2 & 31, mk = c5 >> 8 & 0xFF, st = (c5 >> 16 & 31) << 4;
        uint32_t ne = fmt == 4 ? 16 : 256, bpe = cf == 3 ? 4 : 2, max = d->clen / bpe, *pp = pal + np;
        if ((np + ne) * 4 > TP_B) { res = T_LIMIT; break; }
        const uint8_t *src = f->a + d->clut - 1;
        for (uint32_t k = 0; k < ne; k++) {
          uint32_t j = ((k >> sh) & mk) | st;
          pp[k] = pal8888(j < max ? (bpe == 4 ? ((const uint32_t *)src)[j] : ((const uint16_t *)src)[j]) : 0, cf);
        }
        if (pn == ne && !sceClibMemcmp(pal + pal_prev, pp, ne * 4)) pp = pal + pal_prev;  // same as the last: reused
        else { pal_prev = np; pn = ne; np += ne; }
        sceGxmTextureSetPalette(&t, pp);
      }
      uint32_t tfl = RE(e, 0xC6), twr = RE(e, 0xC7);
      // GE filter and wrap (0xC6, 0xC7)
      sceGxmTextureSetMinFilter(&t, (tfl & 7) == 1 ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT);
      sceGxmTextureSetMagFilter(&t, tfl >> 8 & 1 ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT);
      sceGxmTextureSetUAddrMode(&t, twr & 1 ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
      sceGxmTextureSetVAddrMode(&t, twr >> 8 & 1 ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
    }
    uint32_t n = 0, d4 = RE(e, 0xD4), d5 = RE(e, 0xD5);
    float sc[4] = { 2.0f * (d4 & 0x3FF), 2.0f * (d4 >> 10 & 0x3FF), 2.0f * ((d5 & 0x3FF) + 1), 2.0f * ((d5 >> 10 & 0x3FF) + 1) };
    if (!(d5 & 0xFFFFF)) sc[0] = sc[1] = -1e9f, sc[2] = sc[3] = 1e9f;  // (no saved scissor: not clipped)
    if ((res = trVerts(f, d, o + nv, tw, th, mat, sc, &n))) break;
    if (!n) continue;
    if (nv + n > TV_N) { res = T_LIMIT; break; }
    float ar = trArea(o + nv, n, d->prim);
    if (c_on) { spx += ar; ssw += tex && d->toff && (RE(e, 0xC2) & 1); }
    static uint32_t giant;  // giant draws (over 2 screens at double size): the first 40, in detail
    if (ar > 2.0f * LINE * H && giant < 40) {
      float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
      for (uint32_t j = 0; j < n; j++) {
        const TV *q = &o[nv + j];
        x0 = q->x < x0 ? q->x : x0; x1 = q->x > x1 ? q->x : x1; y0 = q->y < y0 ? q->y : y0; y1 = q->y > y1 ? q->y : y1;
        u0 = q->u < u0 ? q->u : u0; u1 = q->u > u1 ? q->u : u1; v0 = q->v < v0 ? q->v : v0; v1 = q->v > v1 ? q->v : v1;
      }
      giant++;
      alog("traductor: draw gigante (%u): frame %u draw %u de %u prim %u vertices %u->%u area %u miles x %d..%d y %d..%d | tex %u %ux%u fmt %u "
           "swz %u copia %u uv %d..%d %d..%d (milesimas) | destino %s\n", giant, f->frame, i, f->nd, d->prim, d->n, n, (uint32_t)(ar / 1000),
           (int)x0, (int)x1, (int)y0, (int)y1, tex, (uint32_t)tw, (uint32_t)th, RE(e, 0xC3) & 15, RE(e, 0xC2) & 1, d->toff ? 1 : 0,
           (int)(u0 * 1000), (int)(u1 * 1000), (int)(v0 * 1000), (int)(v1 * 1000), dst ? "intermedia" : "superficie");
    }
    SceGxmFragmentProgram *p = fragProg(tex, key);
    if (!p) { res = T_PROG; break; }
    sceGxmSetFragmentProgram(c, p);
    sceGxmReserveFragmentDefaultUniformBuffer(c, &u);
    // uniforms (see sombreadores/*.cg): at = alpha test function (1: always passes) and reference; tf = texture
    // function, TCC and color doubling
    uint32_t db = RE(e, 0xDB), c9 = RE(e, 0xC9);
    float at[4] = { (clr & 1) || !(RE(e, 0x22) & 1) ? 1.0f : (float)(db & 7), (float)(db >> 8 & 0xFF), 0, 0 };
    sceGxmSetUniformDataF(u, tex ? n_atx : n_atc, 0, 4, at);
    if (tex) {
      float tf[4] = { (c9 & 7) == 3 ? 1.0f : 0.0f, (float)(c9 >> 8 & 1), (float)(c9 >> 16 & 1), 0 };
      sceGxmSetUniformDataF(u, n_tf, 0, 4, tf);
      sceGxmSetFragmentTexture(c, 0, &t);
    }
    sceGxmSetVertexStream(c, 0, o + nv);
    sceGxmDraw(c, d->prim == 3 ? SCE_GXM_PRIMITIVE_TRIANGLES : SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, tib, n);
    nv += n; t_draws++;
  }
  if (!has_base && res == T_OK) OPEN(0);  // (frame with no draws to the surface: it still gets the image underneath)
  if (opened) {  // (if something failed midway, the surface keeps what was drawn: not used)
    sceGxmEndScene(c, 0, gEnd());
    cNote(cur_rt ? C_INT : C_TR, &gq_n, t_draws - d0, spx / 1000, ssw);
  }
#undef OPEN
#undef SCENE
  t_q[s] = gq;
  if (res && !t_seen[res]++) alog("traductor: primer frame con '%s' al dibujar\n", tnames[res]);
  return res;
}
