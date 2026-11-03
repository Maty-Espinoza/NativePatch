// translator.c: draws with the Vita GPU (GXM), at double size (960x544), a frame of the record (record.h: GE draws and
// states saved by record.c in the capturer). Included inside presenter.c, it uses its patcher, surfaces and context;
// the presenter calls trInit at startup and trFrame in the Adrenaline thread, instead of copying the PSP image. If the
// frame has something it cannot draw it returns the reason (T_*) and the presenter shows the normal image.
// Own shaders in shaders/*.cg.
#include "record.h"
#include "shaders_nd.h"
// Result of trCheck/trFrame: T_OK or the reason the frame was not drawn (tnames: its name in the log)
enum { T_OK, T_NEW, T_PRIM, T_FMT, T_BLEND, T_TFUNC, T_ODD, T_DEST, T_VERT, T_LIMIT, T_TEX, T_PROG, T_OUT, T_MASK, TN };
static const char *tnames[TN] = { "ok", "no_new_frame", "primitive", "format", "blend", "texture_function",
                                "color_test_or_logic", "other_target", "vertex", "limit", "bad_texture", "program", "vertex_out", "mask" };
// counters for the 10 s log (read and cleared by the presenter)
static volatile uint32_t t_cnt[TN], t_hd, t_nat, t_draws;
static volatile uint32_t t_skip[6];  // PSP frames advanced by each drawn frame (0: repeated, 5: 5 or more)
DEV(static volatile uint32_t t_us[6];)  // us in trFrame: check, textures and palettes, vertices, GXM setup, scene changes, all
static uint32_t t_seen[TN];   // times each reason was seen (the first one is logged in detail)
typedef struct { float x, y, z, w, u, v, f; uint32_t c; } TV;  // translated vertex (32 bytes, the nd_v format); f: fog (1: none)
static TV *tv[2];            // translated vertices, one per surface
static uint32_t *tpal[2];    // translated palettes (8888), aligned to 64: 16 entries (64 bytes) or 256 (1 KB)
#define TV_N (64 << 10)      // vertices per surface (and limit of the 16-bit indices)
#define TP_B (256 << 10)     // palette bytes per surface
#define C_RGB (SCE_GXM_COLOR_MASK_R | SCE_GXM_COLOR_MASK_G | SCE_GXM_COLOR_MASK_B)  // alpha is never written
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
  if (!(r->mem = gmem1(PHY, LINE * H * 4, RW))) { alog("translator: no memory for an intermediate image\n"); return 0; }
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
    if (r->fb == ~0u) alog("translator: intermediate image %d (%s) for %06x\n", i, ty(r->mem), fb);
    r->fb = fb; r->fr = frame;
    return r;
  }
  return 0;
}
// Address of texture 0 of state e (GE registers 0xA0 and 0xA8)
static uint32_t texAddr(const uint32_t *e) { return (RE(e, 0xA0) & 0xFFFFF0) | ((RE(e, 0xA8) << 8) & 0x0F000000); }

// --- programs: one vertex program (nd_v) and three fragment programs: nd_fc without texture, nd_ft with texture and nd_fm, its fast path
// (swizzled ones arrive already sorted from the record). Each fragment program is built once per blend that appears
// (fps, up to 64) ---
static SceGxmVertexProgram *nvp;
static const SceGxmProgram *pft, *pfc, *pfm, *pv;
static SceGxmShaderPatcherId ift, ifc, ifm;
static const SceGxmProgramParameter *n_wvp, *n_tf, *n_tm, *n_atx, *n_atc, *n_env, *n_fgx, *n_fgc;
typedef struct { uint32_t key; SceGxmFragmentProgram *p; } FP;
static FP fps[64];
static uint32_t nfps;
// Fragment program for tex (0 no texture, 1 textured, 2 fast textured), key (0 no blending; otherwise the 4 bytes of SceGxmBlendInfo + 1,
// see blendKey) and mask (SCE_GXM_COLOR_MASK_*, see colorMask). Finds it in fps or creates it with the patcher; 0 on
// failure.
static SceGxmFragmentProgram *fragProg(int tex, uint32_t key, uint32_t mask) {
  uint32_t k = key << 2 | mask << 3 | tex;  // (key's low byte is 0 or 1)
  for (uint32_t i = 0; i < nfps; i++) if (fps[i].key == k) return fps[i].p;
  if (nfps >= 64) return 0;
  SceGxmBlendInfo b;
  if (key) sceClibMemcpy(&b, &(uint32_t){ key - 1 }, 4);
  else { sceClibMemset(&b, 0, sizeof b); b.colorFunc = b.alphaFunc = SCE_GXM_BLEND_FUNC_NONE; }
  b.colorMask = mask;
  SceGxmFragmentProgram *p = 0;
  int r = sceGxmShaderPatcherCreateFragmentProgram(pat, tex == 2 ? ifm : tex ? ift : ifc, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                   SCE_GXM_MULTISAMPLE_NONE, &b, pv, &p);
  if (r < 0) { alog("translator: fragment program %u %08x: %08x\n", tex, key, r); return 0; }
  fps[nfps].key = k; fps[nfps++].p = p;
  return p;
}

// Prepares the translator (called by presStart, only with draw): vertex, palette and index memory, registers the
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
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_fm_gxp, &ifm);
  if (r < 0) { alog("translator: register shaders %08x\n", r); return r; }
  pv = sceGxmShaderPatcherGetProgramFromId(iv); pft = sceGxmShaderPatcherGetProgramFromId(ift); pfc = sceGxmShaderPatcherGetProgramFromId(ifc); pfm = sceGxmShaderPatcherGetProgramFromId(ifm);
  const SceGxmProgramParameter *pa = sceGxmProgramFindParameterByName(pv, "aPosition"), *pt = sceGxmProgramFindParameterByName(pv, "aTexcoord"),
                               *pc = sceGxmProgramFindParameterByName(pv, "aColor");
  n_wvp = sceGxmProgramFindParameterByName(pv, "wvp");
  n_tf = sceGxmProgramFindParameterByName(pft, "tf"); n_atx = sceGxmProgramFindParameterByName(pft, "at");
  n_atc = sceGxmProgramFindParameterByName(pfc, "at"); n_tm = sceGxmProgramFindParameterByName(pfm, "tf"); n_env = sceGxmProgramFindParameterByName(pft, "env");
  n_fgx = sceGxmProgramFindParameterByName(pft, "fog"); n_fgc = sceGxmProgramFindParameterByName(pfc, "fog");
  if (!pa || !pt || !pc || !n_wvp || !n_tf || !n_atx || !n_atc || !n_env || !n_fgx || !n_fgc || !n_tm) { alog("translator: shader parameters not found\n"); return -2; }
  SceGxmVertexAttribute at[3] = { { 0, 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 4, sceGxmProgramParameterGetResourceIndex(pa) },
                                  { 0, 16, SCE_GXM_ATTRIBUTE_FORMAT_F32, 3, sceGxmProgramParameterGetResourceIndex(pt) },
                                  { 0, 28, SCE_GXM_ATTRIBUTE_FORMAT_U8N, 4, sceGxmProgramParameterGetResourceIndex(pc) } };
  SceGxmVertexStream st = { sizeof(TV), SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
  if ((r = sceGxmShaderPatcherCreateVertexProgram(pat, iv, at, 3, &st, 1, &nvp)) < 0) { alog("translator: vertex program %08x\n", r); return r; }
  if (!fragProg(0, 0, C_RGB) || !fragProg(1, 0, C_RGB)) return -3;
  for (int i = 0; i < 2; i++) rtMem(&rts[i]);
  // PSP RAM (0x08000000-0x0A000000) for the GPU (fails with 0x72000000)
  int r1 = sceGxmMapMemory((void *)0x70000000, 0x2000000, RD);
  int r3 = regMapGpu();
  alog("translator: PSP RAM for the GPU %08x, record textures %08x\n", r1, r3);
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
// (factor ZERO or ONE); destination alpha is not computed (not written). MIN and MAX ignore the factors on the GE and
// take ONE in GXM. Doubled alpha factors (6, 7) become SRC_ALPHA ones and the shader doubles alpha (blend2x), so they
// cannot mix with plain alpha factors.
static uint32_t blendKey(const uint32_t *e) {
  if (!(RE(e, 0x21) & 1)) return 0;
  uint32_t df = RE(e, 0xDF), s = df & 15, d = df >> 4 & 15, op = df >> 8 & 15;
  static const uint8_t fs[8] = { SCE_GXM_BLEND_FACTOR_DST_COLOR, SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR, SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
                                 SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, 0, 0, SCE_GXM_BLEND_FACTOR_SRC_ALPHA, SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
                       fd[8] = { SCE_GXM_BLEND_FACTOR_SRC_COLOR, SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
                                 SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, 0, 0, SCE_GXM_BLEND_FACTOR_SRC_ALPHA, SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
                       fo[5] = { SCE_GXM_BLEND_FUNC_ADD, SCE_GXM_BLEND_FUNC_SUBTRACT, SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT,
                                 SCE_GXM_BLEND_FUNC_MIN, SCE_GXM_BLEND_FUNC_MAX };
  uint32_t gs, gd, fa = RE(e, 0xE0) & 0xFFFFFF, fb = RE(e, 0xE1) & 0xFFFFFF;
  if (op == 3 || op == 4) gs = SCE_GXM_BLEND_FACTOR_ONE; else if (s < 4 || s == 6 || s == 7) gs = fs[s]; else if (s == 10 && (fa == 0 || fa == 0xFFFFFF)) gs = fa ? SCE_GXM_BLEND_FACTOR_ONE : SCE_GXM_BLEND_FACTOR_ZERO; else return ~0u;
  if (op == 3 || op == 4) gd = SCE_GXM_BLEND_FACTOR_ONE; else if (d < 4 || d == 6 || d == 7) gd = fd[d]; else if (d == 10 && (fb == 0 || fb == 0xFFFFFF)) gd = fb ? SCE_GXM_BLEND_FACTOR_ONE : SCE_GXM_BLEND_FACTOR_ZERO; else return ~0u;
  if (op > 4 || ((s == 2 || s == 3) && (d == 6 || d == 7)) || ((d == 2 || d == 3) && (s == 6 || s == 7))) return ~0u;
  SceGxmBlendInfo b;
  sceClibMemset(&b, 0, sizeof b);
  b.colorFunc = b.alphaFunc = fo[op]; b.colorSrc = gs; b.colorDst = gd; b.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE;
  b.alphaDst = op == 3 || op == 4 ? SCE_GXM_BLEND_FACTOR_ONE : SCE_GXM_BLEND_FACTOR_ZERO;
  uint32_t k;
  sceClibMemcpy(&k, &b, 4);
  return k + 1;
}

// 1 if the shader must double alpha for the blend of state e (see blendKey)
static int blend2x(const uint32_t *e) {
  uint32_t df = RE(e, 0xDF), s = df & 15, d = df >> 4 & 15, op = df >> 8 & 15;
  return (RE(e, 0x21) & 1) && op < 3 && (s == 6 || s == 7 || d == 6 || d == 7);
}

// GE alpha test of state e, (alpha & mask) FUNC (ref & mask) (0xDB), as what the shaders compare, the whole alpha: *fn
// and *ref. A mask of high bits makes values into ranges, which fit one comparison unless EQUAL or NOTEQUAL are in the
// middle. 0 if there is no equivalent.
static int alphaFn(const uint32_t *e, uint32_t *fn, uint32_t *ref) {
  uint32_t db = RE(e, 0xDB), f = db & 7, m = db >> 16 & 0xFF, lo = ~m & 0xFF, r = db >> 8 & m, hi = r + lo;
  if (f > 1 && lo & (lo + 1)) return 0;
  *fn = f; *ref = f == 5 || f == 6 ? hi : r;
  if ((f == 2 || f == 3) && lo) {
    if (r == 0) { *fn = f == 2 ? 5 : 6; *ref = hi; }
    else if (hi == 255) *fn = f == 2 ? 7 : 4;
    else return 0;
  }
  return 1;
}

// Channels the draw of state e writes: GE color mask (0xE8, bits set are not written) and, in a clear, its color bit.
// ~0u if the mask is not whole channels: GXM masks by channel, per bit needs reading the destination in a shader.
static uint32_t colorMask(const uint32_t *e) {
  uint32_t m = RE(e, 0xE8), clr = RE(e, 0xD3), g = 0;
  for (int i = 0; i < 3; i++) {
    uint32_t b = m >> i * 8 & 0xFF;
    if (!b) g |= SCE_GXM_COLOR_MASK_R << i; else if (b != 0xFF) return ~0u;
  }
  return (clr & 1) && !(clr >> 8 & 1) ? SCE_GXM_COLOR_MASK_NONE : g;
}

// 1 if draw d of state e reads a texture: with the texture on, and UVs in the vertex or generated (0xC0)
static int hasTex(const uint32_t *e, const RDraw *d) {
  return !(RE(e, 0xD3) & 1) && (RE(e, 0x1E) & 1) && ((d->vt & 3) || (!(d->vt >> 23 & 1) && (RE(e, 0xC0) & 3)));
}

// Checks whether draw d of frame f can be drawn (primitive, vertices without weights, destination, blend,
// texture format and function). T_OK if so; otherwise the reason. What depends on other draws is checked by trFrame.
static int trCheck(RFrame *f, RDraw *d) {
  const uint32_t *e = f->e[d->st];
  uint32_t vt = d->vt, clr = RE(e, 0xD3) & 1, afn, aref;
  if (d->prim < 3 || d->prim > 6) return T_PRIM;  // triangles, strip, fan, rectangles
  if ((vt >> 9 & 3) || (vt >> 18 & 7)) return T_VERT;  // weights or morph
  if ((RE(e, 0x9D) & 0x7FF) > 512) return T_DEST;
  if (colorMask(e) == ~0u) return T_MASK;
  if (clr) return T_OK;
  if (RE(e, 0x27) & 1 || RE(e, 0x28) & 1) return T_ODD;
  if ((RE(e, 0x22) & 1) && !alphaFn(e, &afn, &aref)) return T_MASK;
  if (blendKey(e) == ~0u) return T_BLEND;
  if (hasTex(e, d)) {
    uint32_t fmt = RE(e, 0xC3) & 15, tf = RE(e, 0xC9) & 7;
    if (fmt > 5 || (fmt >= 4 && !d->clut)) return T_FMT;
    if (tf > 4) return T_TFUNC;
    // (texture with no copy in the record: trFrame checks whether it is an intermediate image of this frame)
  }
  return T_OK;
}

// Color of a vertex at p with GE color type ct (4 5650, 5 5551, 6 4444, 7 8888) -> 0xAABBGGRR
static uint32_t col8888(const uint8_t *p, uint32_t ct) {
  if (ct == 7) return *(const uint32_t *)p;
  return pal8888(*(const uint16_t *)p, ct - 4);
}

#ifdef NP_DEV
// Logs in hex up to 24 words of the n bytes at p, after the text what
static void trRaw(const char *what, const uint8_t *p, uint32_t n) {
  char b[220];
  uint32_t k = 0;
  b[0] = 0;
  for (uint32_t i = 0; i + 4 <= n && i < 96; i += 4) k += sceClibSnprintf(b + k, sizeof b - k, " %08x", *(const uint32_t *)(p + i));
  alog("%s:%s\n", what, b);
}
// Each change of a frame's result (ok or the reason for the normal image), with its record frame; the first 400
static void trReason(uint32_t frame, int r) {
  static int last = -1;
  static uint32_t cnt;
  if (r != last && cnt < 400) { cnt++; alog("translator: result at frame %u: %s\n", frame, tnames[r]); }
  last = r;
}
// First 40 vertices rejected as T_OUT: which draw and vertex, and the bytes of the vertex and of the draw
static void trOutLog(RFrame *f, RDraw *d, uint32_t i, uint32_t k, uint32_t sz, const uint8_t *p) {
  static uint32_t cnt;
  if (cnt >= 40) return;
  cnt++;
  alog("translator: vertex_out (%u): frame %u draw %u of %u prim %u vt %06x vertex %u of %u index %u size %u\n", cnt, f->frame, (uint32_t)(d - f->d),
       f->nd, d->prim, d->vt, i, d->n, k, sz);
  trRaw("translator: vertex_out vertex", p, sz);
  trRaw("translator: vertex_out draw", f->a + d->voff - 1, d->vlen);
}
#endif

// --- 3D: what the GE does per vertex, done here. T3 holds a draw's data, with the matrices already multiplied ---
typedef struct { uint32_t on, type, comp; float p[3], a[3], sd[3], cut, ex, ca[3], cd[3], cs[3]; } LT;
typedef struct {
  float m[16], w[12], wv[12];              // model -> clip, -> world, -> eye
  float us, vs, uo, vo, tw, th, tm[12];    // UV scale and offset, texture size, texture matrix
  float sx, sy, sz, tx, ty, tz;            // viewport less offset
  float f1, f2, em[4], am[4], ad[4], as[4], ac[4], sp;  // fog; emissive, ambient, diffuse, specular, scene ambient colors
  uint32_t gen, prj, ls0, ls1, upd, fog, lit, rev;      // 0xC0, 0xC1, 0x53, and on/off switches
  uint32_t kw, km, kl; const uint32_t *ke;              // what t3Set last read
  LT l[4];
} T3;
static T3 t3;
static float f24(uint32_t v) { union { uint32_t u; float f; } x = { v << 8 }; return x.f; }
static inline float tsqrt(float x) { __asm__("vsqrt.f32 %0, %1" : "=t"(x) : "t"(x)); return x; }
static float tpow(float b, int e) { float r = 1; for (; e > 0; e >>= 1, b *= b) if (e & 1) r *= b; return r; }
static void tnorm(float *v) { float d = v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; if (d > 0) { d = 1 / tsqrt(d); v[0] *= d; v[1] *= d; v[2] *= d; } }
static void rgbf(uint32_t c, float *o) { o[0] = (c & 255) / 255.0f; o[1] = (c >> 8 & 255) / 255.0f; o[2] = (c >> 16 & 255) / 255.0f; o[3] = (c >> 24 & 255) / 255.0f; }
// Fills t for draw d of state e, reading only what changed since the last draw (kw, km, kl, ke; trFrame clears ke):
// matrices (a 4x3 block is 3 words per basis vector and translation) and their product, registers, and with the block
// of 0x53-0x9A materials and lights.
static void t3Set(RFrame *f, RDraw *d, const uint32_t *e, T3 *t) {
  if (d->woff == t->kw && d->moff == t->km && d->loff == t->kl && e == t->ke) return;
  const uint32_t *w = (const uint32_t *)(f->a + d->woff - 1), *v = (const uint32_t *)(f->a + d->moff - 1);
  float vm[12], p[16];
  for (int i = 0; i < 12; i++) { t->w[i] = f24(w[i]); vm[i] = f24(v[i]); t->tm[i] = f24(v[28 + i]); }
  for (int i = 0; i < 16; i++) p[i] = f24(v[12 + i]);
  for (int c = 0; c < 4; c++) {
    const float *a = t->w + c * 3;
    for (int r = 0; r < 3; r++) t->wv[c * 3 + r] = a[0] * vm[r] + a[1] * vm[3 + r] + a[2] * vm[6 + r] + (c == 3 ? vm[9 + r] : 0);
    const float *b = t->wv + c * 3;
    for (int r = 0; r < 4; r++) t->m[c * 4 + r] = b[0] * p[r] + b[1] * p[4 + r] + b[2] * p[8 + r] + (c == 3 ? p[12 + r] : 0);
  }
  t->kw = d->woff; t->km = d->moff;
  int ne = e != t->ke, nl = ne || d->loff != t->kl;
  t->ke = e; t->kl = d->loff;
  if (!ne) goto lights;
  t->sx = f24(RE(e, 0x42)); t->sy = f24(RE(e, 0x43)); t->sz = f24(RE(e, 0x44));
  t->tx = f24(RE(e, 0x45)) - (RE(e, 0x4C) & 0xFFFF) / 16.0f; t->ty = f24(RE(e, 0x46)) - (RE(e, 0x4D) & 0xFFFF) / 16.0f; t->tz = f24(RE(e, 0x47));
  t->us = f24(RE(e, 0x48)); t->vs = f24(RE(e, 0x49)); t->uo = f24(RE(e, 0x4A)); t->vo = f24(RE(e, 0x4B));
  uint32_t b8 = RE(e, 0xB8), c0 = RE(e, 0xC0), c1 = RE(e, 0xC1);
  t->tw = 1u << (b8 & 15); t->th = 1u << (b8 >> 8 & 15);
  t->gen = c0 & 3; t->prj = c0 >> 8 & 3; t->ls0 = c1 & 3; t->ls1 = c1 >> 8 & 3;
  t->fog = RE(e, 0x1F) & 1; t->f1 = f24(RE(e, 0xCD)); t->f2 = f24(RE(e, 0xCE));
  t->lit = !(RE(e, 0xD3) & 1) && (RE(e, 0x17) & 1); t->rev = RE(e, 0x51) & 1;
lights:
  if (!d->loff || !nl) return;
  const uint32_t *l = (const uint32_t *)(f->a + d->loff - 1) - 0x53;  // (l[r]: the value of register r)
  t->upd = l[0x53] & 7; t->sp = f24(l[0x5B]);
  rgbf(l[0x54] & 0xFFFFFF, t->em); rgbf((l[0x55] & 0xFFFFFF) | l[0x58] << 24, t->am); rgbf(l[0x56] & 0xFFFFFF, t->ad); rgbf(l[0x57] & 0xFFFFFF, t->as);
  rgbf((l[0x5C] & 0xFFFFFF) | l[0x5D] << 24, t->ac);
  for (int i = 0; i < 4; i++) {
    LT *q = &t->l[i];
    float s[4];
    q->on = RE(e, 0x18 + i) & 1; q->type = l[0x5F + i] >> 8 & 3; q->comp = l[0x5F + i] & 3;
    for (int k = 0; k < 3; k++) { q->p[k] = f24(l[0x63 + i * 3 + k]); q->sd[k] = f24(l[0x6F + i * 3 + k]); q->a[k] = f24(l[0x7B + i * 3 + k]); }
    q->ex = f24(l[0x87 + i]); q->cut = f24(l[0x8B + i]);
    rgbf(l[0x8F + i * 3] & 0xFFFFFF, s); sceClibMemcpy(q->ca, s, 12);
    rgbf(l[0x90 + i * 3] & 0xFFFFFF, s); sceClibMemcpy(q->cd, s, 12);
    rgbf(l[0x91 + i * 3] & 0xFFFFFF, s); sceClibMemcpy(q->cs, s, 12);
    if (!q->type) tnorm(q->p);
    tnorm(q->sd);
  }
}
// World position of model point p
static void t3World(const T3 *t, const float *p, float *o) {
  for (int r = 0; r < 3; r++) o[r] = p[0] * t->w[r] + p[1] * t->w[3 + r] + p[2] * t->w[6 + r] + t->w[9 + r];
}
// Direction from world point wp to light l (normalized); *dist: its distance
static void t3Dir(const LT *l, const float *wp, float *o, float *dist) {
  *dist = 1;
  for (int k = 0; k < 3; k++) o[k] = l->type ? l->p[k] - wp[k] : l->p[k];
  if (!l->type) return;
  *dist = tsqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
  tnorm(o);
}
// GE lighting of a vertex: world point wp, unit normal n, vertex color vc (has: the vertex has a color). Returns the color
static uint32_t t3Light(const T3 *t, const float *wp, const float *n, uint32_t vc, int has) {
  float c[4], o[4], sum[3] = { 0, 0, 0 }, vf[4];
  rgbf(vc, vf);
  const float *mac = has && (t->upd & 1) ? vf : t->am, *mdc = has && (t->upd & 2) ? vf : t->ad, *msc = has && (t->upd & 4) ? vf : t->as;
  for (int k = 0; k < 4; k++) c[k] = t->em[k] + t->ac[k] * mac[k];
  for (int i = 0; i < 4; i++) {
    const LT *l = &t->l[i];
    float L[3], dist, k = 1;
    if (!l->on) continue;
    t3Dir(l, wp, L, &dist);
    if (l->type) { float den = l->a[0] + l->a[1] * dist + l->a[2] * dist * dist; k = den > 0 ? 1 / den : 0; if (k > 1) k = 1; }
    if (l->type == 2) {
      float s = l->sd[0] * L[0] + l->sd[1] * L[1] + l->sd[2] * L[2];
      k *= s >= l->cut ? tpow(s, (int)(l->ex + .5f)) : 0;
    }
    float nl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2], dd = l->comp == 2 && nl > 0 ? tpow(nl, (int)(t->sp + .5f)) : nl;
    for (int j = 0; j < 3; j++) c[j] += k * (l->ca[j] * mac[j] + (dd > 0 ? dd * l->cd[j] * mdc[j] : 0));
    if (l->comp == 1 && nl >= 0) {
      float h[3] = { L[0], L[1], L[2] + 1 };
      tnorm(h);
      float s = n[0] * h[0] + n[1] * h[1] + n[2] * h[2];
      if (s > 0) { s = tpow(s, (int)(t->sp + .5f)) * k; for (int j = 0; j < 3; j++) sum[j] += s * l->cs[j] * msc[j]; }
    }
  }
  for (int j = 0; j < 3; j++) c[j] += sum[j];
  for (int k = 0; k < 4; k++) o[k] = c[k] < 0 ? 0 : c[k] > 1 ? 255 : c[k] * 255 + .5f;
  return (uint32_t)o[0] | (uint32_t)o[1] << 8 | (uint32_t)o[2] << 16 | (uint32_t)o[3] << 24;
}
// Generated UV of a vertex, in 0-1: texture matrix (gen 1) or environment map (gen 2, from two lights' directions).
// x: model position, uv: its UV, n: model normal (nw: in world, unit)
static void t3Gen(const T3 *t, const float *x, float u0, float v0, const float *n, const float *nw, float *u, float *v) {
  if (t->gen == 2) {
    float wp[3], L[3], dist;
    t3World(t, x, wp);
    t3Dir(&t->l[t->ls0], wp, L, &dist); *u = (nw[0] * L[0] + nw[1] * L[1] + nw[2] * L[2] + 1) / 2;
    t3Dir(&t->l[t->ls1], wp, L, &dist); *v = (nw[0] * L[0] + nw[1] * L[1] + nw[2] * L[2] + 1) / 2;
    return;
  }
  float s[3] = { x[0], x[1], x[2] }, r[3];
  if (t->prj == 1) { s[0] = u0; s[1] = v0; s[2] = 0; }
  else if (t->prj >= 2) { sceClibMemcpy(s, n, 12); if (t->prj == 2) tnorm(s); }
  for (int k = 0; k < 3; k++) r[k] = s[0] * t->tm[k] + s[1] * t->tm[3 + k] + s[2] * t->tm[6 + k] + t->tm[9 + k];
  float q = r[2] != 0 ? 1 / r[2] : 1;
  *u = r[0] * q; *v = r[1] * q;
}

// Decodes the vertices of draw d to o: computes the size and position of each field from the GE vertex
// type (with its alignment), follows the indices if any, and leaves x, y doubled and u, v divided by tw x th.
// t (0: through mode): the draw's 3D data; each vertex is transformed, lit and given its UV and fog like on the GE, and x,
// y, z keep their clip w, so the GPU clips and corrects perspective. Rectangles are divided by w here.
// col0: color if the vertex has none; sc: doubled scissor (x0, y0, x1, y1). Rectangles (2 corners) are clipped to the
// scissor and come out as 4-vertex strips. *nout: vertices written. T_OK, T_VERT (data outside the record), T_LIMIT or
// T_OUT (position outside +-4096: garbage vertices). The GE's z (integer, 0-65535) comes out as 0.5 down to 0: with the
// matrix tr_wvp, clip z from 0 to almost 1, growing with the GE's, so the GE comparisons apply as they are.
static int trVerts(RFrame *f, RDraw *d, TV *o, float tw, float th, uint32_t col0, const float *sc, uint32_t *nout, const T3 *t) {
  static const uint8_t s4[4] = { 0, 1, 2, 4 }, cs[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
  uint32_t vt = d->vt, tc = vt & 3, ct = vt >> 2 & 7, nt = vt >> 5 & 3, pt = vt >> 7 & 3, sz = 0, al = 1, s;
  uint32_t otc = 0, ocol = 0, opos = 0, onrm = 0;
  // FLD: n components of size bytes, aligned to size (o: offset). Order: texture, color, normal, position
#define FLD(n, size, o) s = (size); if (s) { sz = (sz + s - 1) & ~(s - 1); o = sz; sz += (n) * s; if (s > al) al = s; }
  FLD(2, s4[tc], otc); FLD(1, cs[ct], ocol); FLD(3, s4[nt], onrm); FLD(3, s4[pt], opos);
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
    TV q = { 0, 0, .5f, 1, 0, 0, 1, ct ? col8888(p + ocol, ct) : col0 };
    if (t) {
      float x[3], n[3] = { 0, 0, 1 }, nw[3], wp[3], u = 0, v = 0;
      static const float ks[4] = { 0, 1 / 128.0f, 1 / 32768.0f, 1 };
      for (int a = 0; a < 3; a++) {
        const uint8_t *b = p + opos + a * s4[pt];
        x[a] = pt == 3 ? *(const float *)b : (pt == 2 ? *(const int16_t *)b : *(const int8_t *)b) * ks[pt];
        if (nt) { b = p + onrm + a * s4[nt]; n[a] = nt == 3 ? *(const float *)b : (nt == 2 ? *(const int16_t *)b : *(const int8_t *)b) * ks[nt]; }
      }
      if (tc) {
        const uint8_t *b = p + otc;
        u = tc == 3 ? ((const float *)b)[0] : tc == 2 ? ((const uint16_t *)b)[0] * ks[2] : b[0] * ks[1];
        v = tc == 3 ? ((const float *)b)[1] : tc == 2 ? ((const uint16_t *)b)[1] * ks[2] : b[1] * ks[1];
      }
      if (t->lit || t->gen == 2) {
        if (t->rev) n[0] = -n[0], n[1] = -n[1], n[2] = -n[2];
        for (int r = 0; r < 3; r++) nw[r] = n[0] * t->w[r] + n[1] * t->w[3 + r] + n[2] * t->w[6 + r];
        tnorm(nw);
      }
      if (t->lit) { t3World(t, x, wp); q.c = t3Light(t, wp, nw, q.c, ct != 0); }
      if (t->gen) t3Gen(t, x, u, v, n, nw, &u, &v);
      else { u = u * t->us + t->uo; v = v * t->vs + t->vo; }
      q.u = u * t->tw; q.v = v * t->th;
      const float *m = t->m;
      float cx = x[0] * m[0] + x[1] * m[4] + x[2] * m[8] + m[12], cy = x[0] * m[1] + x[1] * m[5] + x[2] * m[9] + m[13],
            cz = x[0] * m[2] + x[1] * m[6] + x[2] * m[10] + m[14], w = x[0] * m[3] + x[1] * m[7] + x[2] * m[11] + m[15];
      if (t->fog) {
        float g = (x[0] * t->wv[2] + x[1] * t->wv[5] + x[2] * t->wv[8] + t->wv[11] + t->f1) * t->f2;
        q.f = g < 0 ? 0 : g > 1 ? 1 : g;
      }
      if (!(w > -1e9f && w < 1e9f)) { DEV(trOutLog(f, d, i, k, sz, p);) return T_OUT; }
      float wd = 1;
      if (d->prim == 6) { if (w <= 0) continue; wd = 1 / w; }
      q.x = (t->sx * cx + t->tx * w) * wd; q.y = (t->sy * cy + t->ty * w) * wd;
      q.z = (.5f * w - (t->sz * cz + t->tz * w) / 131072.0f) * wd; q.w = w * wd;
    }
    else if (pt == 1) { q.x = (int8_t)p[opos]; q.y = (int8_t)p[opos + 1]; }
    else if (pt == 2) { q.x = ((const int16_t *)(p + opos))[0]; q.y = ((const int16_t *)(p + opos))[1]; q.z = .5f - ((const uint16_t *)(p + opos))[2] / 131072.0f; }
    else {
      q.x = ((const float *)(p + opos))[0]; q.y = ((const float *)(p + opos))[1];
      float z = ((const float *)(p + opos))[2];
      q.z = .5f - (z >= 65535 ? 65535 : z > 0 ? (int)z : 0) / 131072.0f;
    }
    if (t) {}
    else if (tc == 1) { q.u = p[otc] / 128.0f; q.v = p[otc + 1] / 128.0f; }  // (8 bits: 128 = 1 texel, also in through mode)
    else if (tc == 2) { q.u = ((const uint16_t *)(p + otc))[0]; q.v = ((const uint16_t *)(p + otc))[1]; }
    else if (tc == 3) { q.u = ((const float *)(p + otc))[0]; q.v = ((const float *)(p + otc))[1]; }
    // position outside what the GE draws (or NaN): data read late, when the game was already rewriting it
    if (!t && !(q.x > -4096 && q.x < 4096 && q.y > -4096 && q.y < 4096)) { DEV(trOutLog(f, d, i, k, sz, p);) return T_OUT; }
    q.x *= 2; q.y *= 2; q.u /= tw; q.v /= th;
    if (d->prim == 6) {  // rectangle: 2 vertices (opposite corners) -> strip of 4, separated from the next with two
      r[i & 1] = q;      // repeated vertices; color and z are the second vertex's (like the GE). Corners given along
                         // the other diagonal (rot) rotate the UVs: u follows y and v follows x
      if (i & 1) {
        if (n + 6 > TV_N) return T_LIMIT;
        int rot = (r[0].x - r[1].x) * (r[0].y - r[1].y) < 0;
        for (int axis = 0; axis < 2; axis++) {  // scissor clipping in x and y; u and v are clipped proportionally
          float *a0 = axis ? &r[0].y : &r[0].x, *a1 = axis ? &r[1].y : &r[1].x, *t0 = axis != rot ? &r[0].v : &r[0].u, *t1 = axis != rot ? &r[1].v : &r[1].u;
          float lo = sc[axis], hi = sc[axis + 2], d = *a1 - *a0, p0 = *a0, q0 = *t0, k = d != 0 ? (*t1 - *t0) / d : 0;
          if (d >= 0) { if (*a0 < lo) *a0 = lo; if (*a1 > hi) *a1 = hi; if (*a0 >= *a1) goto outside; }
          else { if (*a1 < lo) *a1 = lo; if (*a0 > hi) *a0 = hi; if (*a1 >= *a0) goto outside; }
          *t0 = q0 + (*a0 - p0) * k; *t1 = q0 + (*a1 - p0) * k;
        }
        uint32_t c = r[1].c;
        float z = r[1].z;
        float fg = r[1].f;
        TV a = { r[0].x, r[0].y, z, 1, r[0].u, r[0].v, fg, c }, b = { r[1].x, r[0].y, z, 1, r[!rot].u, r[rot].v, fg, c },
           cc = { r[0].x, r[1].y, z, 1, r[rot].u, r[!rot].v, fg, c }, dd = { r[1].x, r[1].y, z, 1, r[1].u, r[1].v, fg, c };
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

// Clips triangle p to scissor sc edge by edge (Sutherland-Hodgman) in clip space (x, y are x * w, y * w), interpolating
// the new vertices, and writes the polygon left to o as triangles. Edge 4 first cuts what is behind the camera, where
// the scissor comparison does not hold (w = 1 never fails it). Returns the vertices written (21 at most).
static uint32_t trClipTri(const TV *p, const float *sc, TV *o) {
  TV a[8], b[8], *in = a, *out = b;
  uint32_t m = 3, n = 0;
  sceClibMemcpy(a, p, 3 * sizeof(TV));
  for (int ei = 0; ei < 5; ei++) {  // edges: behind, then in sc order left, top, right, bottom
    uint32_t k = 0;
    int e = ei - 1;
    float s = e < 2 ? 1 : -1;
    for (uint32_t i = 0; i < m; i++) {
      const TV *c = &in[i], *x = &in[(i + 1) % m];
      float dc = e < 0 ? c->w - 1e-4f : s * ((e & 1 ? c->y : c->x) - sc[e] * c->w), dx = e < 0 ? x->w - 1e-4f : s * ((e & 1 ? x->y : x->x) - sc[e] * x->w);
      if (dc >= 0) out[k++] = *c;
      if ((dc >= 0) != (dx >= 0)) {
        float t = dc / (dc - dx);
        TV q = { c->x + (x->x - c->x) * t, c->y + (x->y - c->y) * t, c->z + (x->z - c->z) * t, c->w + (x->w - c->w) * t, c->u + (x->u - c->u) * t,
                 c->v + (x->v - c->v) * t, c->f + (x->f - c->f) * t, 0 };
        for (int sh = 0; sh < 32; sh += 8) {
          float c0 = c->c >> sh & 255, c1 = x->c >> sh & 255;
          q.c |= (uint32_t)(c0 + (c1 - c0) * t + .5f) << sh;
        }
        out[k++] = q;
      }
    }
    m = k;
    TV *sw = in; in = out; out = sw;
  }
  for (uint32_t i = 2; i < m; i++) { o[n++] = in[0]; o[n++] = in[i - 1]; o[n++] = in[i]; }
  return n;
}

// Triangles, strip or fan (*prim 3, 4 or 5) of *np vertices at o, room for room: if a vertex is outside the scissor or flat is
// set (each triangle takes the color of its last vertex), rewrites them as a clipped triangle list (*prim = 3) and
// updates *np. With w3d (3D draw) only a scissor smaller than the surface is clipped here: the GPU clips the rest. Odd
// triangles of a strip are swapped to keep their winding. T_OK or T_LIMIT.
static int trTris(TV *o, uint32_t *np, uint32_t room, uint32_t *prim, const float *sc, int flat, int w3d) {
  uint32_t n = *np, m = 0, cut = flat;
  if (!w3d || sc[0] > 0 || sc[1] > 0 || sc[2] < W || sc[3] < H)
    for (uint32_t i = 0; i < n; i++) cut |= o[i].x < sc[0] * o[i].w || o[i].y < sc[1] * o[i].w || o[i].x > sc[2] * o[i].w || o[i].y > sc[3] * o[i].w;
  if (!cut) return T_OK;
  for (uint32_t i = 0; i + 2 < n; i += *prim == 3 ? 3 : 1) {
    if (n + m + 21 > room) return T_LIMIT;
    int sw = *prim == 4 && (i & 1);
    TV t[3] = { o[*prim == 5 ? 0 : sw ? i + 1 : i], o[sw ? i : i + 1], o[i + 2] };
    if (flat) t[0].c = t[1].c = t[2].c;
    m += trClipTri(t, sc, o + n + m);
  }
  sceClibMemmove(o, o + n, m * sizeof(TV));
  *np = m; *prim = 3;
  return T_OK;
}

// Texture of tw x th texels (0xB8) in a buffer of tbw at src, narrower than the buffer, and t made with the buffer's
// width: repeat and clamp would act at the buffer's edge. If the n vertices at o leave tw, copies its rows without the
// gap to dst (room words), narrows t and rescales u. Returns the words used (0: not needed), ~0u if it does not fit.
static uint32_t trNarrow(SceGxmTexture *t, const uint8_t *src, uint32_t fmt, uint32_t tbw, uint32_t tw, uint32_t th, TV *o, uint32_t n, uint32_t *dst, uint32_t room) {
  static const uint8_t bp[6] = { 16, 16, 16, 32, 4, 8 };
  float k = (float)tbw / tw;
  uint32_t out = 0, ri = tbw * bp[fmt] / 8, ro = ((tw + 7) & ~7u) * bp[fmt] / 8, w;  // (GXM stride: 8 texels)
  for (uint32_t i = 0; i < n; i++) out |= o[i].u < 0 || o[i].u * k > 1;
  if (!out) return 0;
  if ((w = (ro * th + 63) / 4 & ~15u) > room) return ~0u;
  for (uint32_t y = 0; y < th; y++) sceClibMemcpy((uint8_t *)dst + y * ro, src + y * ri, ro < ri ? ro : ri);
  sceGxmTextureSetWidth(t, tw); sceGxmTextureSetData(t, dst);
  for (uint32_t i = 0; i < n; i++) o[i].u *= k;
  return w;
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
  TV q[4] = { { 0, 0, .5f, 1, 0, 0, 1, 0xFF000000u }, { W, 0, .5f, 1, 0, 0, 1, 0xFF000000u }, { 0, H, .5f, 1, 0, 0, 1, 0xFF000000u }, { W, H, .5f, 1, 0, 0, 1, 0xFF000000u } };
  sceClibMemcpy(o + nv, q, sizeof q);
  sceGxmSetFragmentProgram(c, fragProg(0, 0, C_RGB));
  sceGxmReserveFragmentDefaultUniformBuffer(c, &u);
  sceGxmSetUniformDataF(u, n_atc, 0, 4, always);
  sceGxmSetUniformDataF(u, n_fgc, 0, 4, always);
  sceGxmSetVertexStream(c, 0, o + nv);
  sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, tib, 4);
  return nv + 4;
}

// GE comparison (0-7) -> GXM depth function >> 22, which is also its stencil function >> 25
static const uint8_t gcmp[8] = { 0, 7, 2, 5, 1, 3, 4, 6 };
static uint32_t t_zs, t_st, t_sr, t_cl;  // depth state, stencil state, stencil reference and cull in the context; ~0u: unknown
// Sets depth and stencil of the draw of state e (0: no tests or writes, for what goes underneath), if they changed. As
// on the GE, without the depth test nothing is written; a clear writes depth and stencil if their bits are on (stencil:
// sv, its alpha). The surface's stencil plays the GE's alpha; there is none in 565 and the color mask's alpha (0xE9)
// is its write mask. Depth and stencil live in one scene only.
static void zsSet(SceGxmContext *c, const uint32_t *e, uint32_t sv) {
  uint32_t clr = e ? RE(e, 0xD3) : 0, z = SCE_GXM_DEPTH_FUNC_ALWAYS, w = SCE_GXM_DEPTH_WRITE_DISABLED;
  if (clr & 1) { if (clr >> 10 & 1) w = SCE_GXM_DEPTH_WRITE_ENABLED; }
  else if (e && (RE(e, 0x23) & 1)) { z = (uint32_t)gcmp[RE(e, 0xDE) & 7] << 22; if (!(RE(e, 0xE7) & 1)) w = SCE_GXM_DEPTH_WRITE_ENABLED; }
  if ((z | w) != t_zs) { t_zs = z | w; sceGxmSetFrontDepthFunc(c, z); sceGxmSetFrontDepthWriteEnable(c, w); }
  static const uint8_t gop[8] = { 0, 1, 2, 5, 3, 4, 0, 0 };  // GE stencil operation (0-5, others keep) -> GXM
  uint32_t f = 7, sf = 0, zf = 0, zp = 0, cm = 0, wm = 0, r = 0, on = e && (RE(e, 0xD2) & 3);
  if (e && !(clr & 1) && (RE(e, 0x24) & 1)) {
    uint32_t dc = RE(e, 0xDC), dd = RE(e, 0xDD);
    f = gcmp[dc & 7]; r = dc >> 8 & 0xFF; cm = dc >> 16 & 0xFF;
    sf = gop[dd & 7]; zf = gop[dd >> 8 & 7]; zp = gop[dd >> 16 & 7];
    if (on) wm = ~RE(e, 0xE9) & 0xFF;
  } else if (on && (clr & 0x201) == 0x201) { sf = zf = zp = SCE_GXM_STENCIL_OP_REPLACE; r = sv; wm = ~RE(e, 0xE9) & 0xFF; }
  uint32_t k = f | sf << 3 | zf << 6 | zp << 9 | cm << 12 | wm << 20;
  if (k != t_st) { t_st = k; sceGxmSetFrontStencilFunc(c, f << 25, sf, zf, zp, cm, wm); }
  if (r != t_sr) { t_sr = r; sceGxmSetFrontStencilRef(c, r); }
}

// Draws frame f of the record to surface s (cs[s]), in the Adrenaline thread. base: the normal image, which goes
// underneath stretched (what the game does not redraw stays as on the PSP); 0: black background. First checks all draws
// (trCheck, destinations and textures with no copy): if one fails, returns the reason without drawing. Then opens
// one scene per destination change (surface s or an intermediate image; if possible, all intermediates first and the
// surface last, in a single scene: each scene reads and writes its whole surface), sets up texture, palette (to 8888 in
// tpal), vertices (trVerts, in tv) and uniforms of each draw, and draws it. T_OK if ready; if it fails midway
// returns the reason and the surface is not used.
static int trFrame(SceGxmContext *c, RFrame *f, int s, const SceGxmTexture *base) {
  t3.ke = 0;
  DEV(uint32_t ta = now(), tb;)
  // disp: image being shown; ifb, iend, ipsm: intermediate images, where their memory ends, their format
  uint32_t disp = f->fb & 0x1FFFF0, ifb[2], iend[2], ipsm[2], nif = 0, n_disp = 0;
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
      if (k == nif) {
        uint32_t end = fb + (RE(e0, 0x9D) & 0x7FF) * ((RE(e0, 0xD2) & 3) == 3 ? 4 : 2) * ((RE(e0, 0xD5) >> 10 & 0x3FF) + 1);
        // images sharing memory: each surface would miss what is drawn on the other
        for (uint32_t j = 0; j < nif; j++) if (fb < iend[j] && ifb[j] < end) r = T_DEST;
        if (nif < 2) { ipsm[nif] = RE(e0, 0xD2) & 3; iend[nif] = end; ifb[nif++] = fb; } else r = T_DEST;
      }
      kd = k;
      if (rd_mask >> k & 1) ord = 0;
    }
    if (!r && hasTex(e0, &f->d[i]) && !f->d[i].toff) {  // texture with no copy
      uint32_t ta = texAddr(e0);
      // only valid if in VRAM (0x04000000) and an intermediate image of an earlier draw of this frame
      for (k = 0; k < nif && !((ta & 0x0F000000) == 0x04000000 && (ta & 0x1FFFF0) == ifb[k]); k++) {}
      // nor read in another format (its bytes are not in the surface) or while being drawn
      if (k == nif || (RE(e0, 0xC3) & 15) != ipsm[k] || (int)k == kd) r = T_TEX;
      else { rd_mask |= 1u << k; if (kd >= 0) ord = 0; }
    }
    if (r) {
      if (!t_seen[r]++) {
        const uint32_t *e = f->e[f->d[i].st];
        alog("translator: first frame with '%s': draw %u of %u prim %u vt %06x tex %u fmt %u swz %u blend %u (%04x %06x %06x) alpha %u "
             "tfunc %06x fb %06x/%u (displayed %07x/%u) clear %u ctest %u logic %u\n", tnames[r], i, f->nd, f->d[i].prim, f->d[i].vt,
             RE(e, 0x1E) & 1, RE(e, 0xC3) & 15, RE(e, 0xC2) & 1, RE(e, 0x21) & 1, RE(e, 0xDF) & 0xFFFF, RE(e, 0xE0) & 0xFFFFFF,
             RE(e, 0xE1) & 0xFFFFFF, RE(e, 0x22) & 1, RE(e, 0xC9) & 0xFFFFFF, RE(e, 0x9C) & 0xFFFFFF, RE(e, 0x9D) & 0x7FF, f->fb,
             f->fbw & 0xFFFF, RE(e, 0xD3) & 0xFFFF, RE(e, 0x27) & 1, RE(e, 0x28) & 1);
      }
      return r;
    }
  }
  DEV(t_us[0] += now() - ta;)
  static uint32_t t_q[2];  // last scene that read the vertices and palettes of each surface
  // if the GPU is still reading them (unfinished scene) it waits
  if (gBusy(t_q[s])) { g_psurf++; gEv("vertices and palettes rewritten", t_q[s]); gWait(t_q[s]); }
  TV *o = tv[s];
  uint32_t *pal = tpal[s], np = 0, nv = 0, pal_prev = ~0u, pn = 0;  // np: words used; pal_prev/pn: the previous palette
  void *u;
  // SCENE: starts a scene on surface surf_tex with the vertex program and the matrix
#define SCENE(surf_tex) do { sceGxmBeginScene(c, 0, rt, 0, 0, 0, surf_tex, &ds); t_zs = t_st = t_sr = t_cl = ~0u; zsSet(c, 0, 0); sceGxmSetVertexProgram(c, nvp); \
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
    uint32_t clr = RE(e, 0xD3), tex = hasTex(e, d), fb = RE(e, 0x9C) & 0x1FFFF0, w3d = !(d->vt >> 23 & 1);
    RT *dst = fb == disp ? 0 : rtOf(fb, f->frame, 1);
    if (fb != disp && !dst) { res = T_DEST; break; }
    if (ord && (pass ? dst != 0 : dst == 0)) continue;
    DEV(tb = now();)
    if (!opened || dst != cur_rt) OPEN(dst);
    DEV(t_us[4] += now() - tb; tb = now();)
    if ((clr & 1) && !(clr & 0x700)) continue;  // clear of nothing
    uint32_t key = clr & 1 ? 0 : blendKey(e), mat = (RE(e, 0x55) & 0xFFFFFF) | (RE(e, 0x58) & 0xFF) << 24;
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
      sceGxmTextureSetMinFilter(&t, tfl & 1 ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT);
      sceGxmTextureSetMagFilter(&t, tfl >> 8 & 1 ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT);
      sceGxmTextureSetUAddrMode(&t, twr & 1 ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
      sceGxmTextureSetVAddrMode(&t, twr >> 8 & 1 ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
    }
    DEV(t_us[1] += now() - tb; tb = now();)
    uint32_t n = 0, d4 = RE(e, 0xD4), d5 = RE(e, 0xD5);
    float sc[4] = { 2.0f * (d4 & 0x3FF), 2.0f * (d4 >> 10 & 0x3FF), 2.0f * ((d5 & 0x3FF) + 1), 2.0f * ((d5 >> 10 & 0x3FF) + 1) };
    if (!(d5 & 0xFFFFF)) sc[0] = sc[1] = -1e9f, sc[2] = sc[3] = 1e9f;  // (no saved scissor: not clipped)
    uint32_t rg = RE(e, 0x16);  // the region's end (never set: 0) is one more scissor edge
    if (rg & 0xFFFFF) {
      float rx = 2.0f * ((rg & 0x3FF) + 1), ry = 2.0f * ((rg >> 10 & 0x3FF) + 1);
      if (rx < sc[2]) sc[2] = rx;
      if (ry < sc[3]) sc[3] = ry;
    }
    if (w3d) t3Set(f, d, e, &t3);
    if ((res = trVerts(f, d, o + nv, tw, th, mat, sc, &n, w3d ? &t3 : 0))) break;
    if (nv + n > TV_N) { res = T_LIMIT; break; }
    if (dst && !(clr & 1)) for (uint32_t j = 0; j < n; j++) if (o[nv + j].w > 0 && o[nv + j].y > H * o[nv + j].w) res = T_DEST;  // (past the surface's rows)
    if (res) break;
    uint32_t prim = d->prim;
    if (prim != 6 && (res = trTris(o + nv, &n, TV_N - nv, &prim, sc, (clr & 1) || !(RE(e, 0x50) & 1), w3d))) break;
    if (!n) continue;
    if (tex && d->toff && (1u << (RE(e, 0xB8) & 15)) < tw) {
      uint32_t w = trNarrow(&t, f->tx + d->toff - 1, RE(e, 0xC3) & 15, tw, 1u << (RE(e, 0xB8) & 15), th, o + nv, n, pal + np, TP_B / 4 - np);
      if (w == ~0u) { res = T_LIMIT; break; }
      np += w;
    }
    DEV(t_us[2] += now() - tb; tb = now();)
    float ar = w3d ? 0 : trArea(o + nv, n, prim);
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
      alog("translator: giant draw (%u): frame %u draw %u of %u prim %u vertices %u->%u area %u thousand x %d..%d y %d..%d | tex %u %ux%u fmt %u "
           "swz %u copy %u uv %d..%d %d..%d (thousandths) | target %s\n", giant, f->frame, i, f->nd, d->prim, d->n, n, (uint32_t)(ar / 1000),
           (int)x0, (int)x1, (int)y0, (int)y1, tex, (uint32_t)tw, (uint32_t)th, RE(e, 0xC3) & 15, RE(e, 0xC2) & 1, d->toff ? 1 : 0,
           (int)(u0 * 1000), (int)(u1 * 1000), (int)(v0 * 1000), (int)(v1 * 1000), dst ? "intermediate" : "surface");
      DEV(alog("translator: giant draw (%u) vt %06x\n", giant, d->vt); trRaw("translator: giant draw bytes", f->a + d->voff - 1, d->vlen);)
    }
    uint32_t afn = 1, aref = 0, c9 = RE(e, 0xC9), b2 = !(clr & 1) && blend2x(e);
    if (!(clr & 1) && (RE(e, 0x22) & 1)) alphaFn(e, &afn, &aref);
    int fast = tex && !(c9 & 0x10007) && afn == 1 && !b2 && !(w3d && t3.fog);
    SceGxmFragmentProgram *p = fragProg(fast ? 2 : tex, key, colorMask(e));
    if (!p) { res = T_PROG; break; }
    sceGxmSetFragmentProgram(c, p);
    sceGxmReserveFragmentDefaultUniformBuffer(c, &u);
    // uniforms (see shaders/*.cg): at = alpha test function (1: always passes) and reference; tf = texture
    // function, TCC and color doubling
    float tf[4] = { (float)(c9 & 7), (float)(c9 >> 8 & 1), (float)(c9 >> 16 & 1), 0 };
    if (fast) sceGxmSetUniformDataF(u, n_tm, 0, 4, tf);
    else {
      float at[4] = { (float)afn, (float)aref, b2 ? 1.0f : 0.0f, 0 }, fg[4], env[4];
      sceGxmSetUniformDataF(u, tex ? n_atx : n_atc, 0, 4, at);
      rgbf(RE(e, 0xCF) & 0xFFFFFF, fg);
      sceGxmSetUniformDataF(u, tex ? n_fgx : n_fgc, 0, 4, fg);
      if (tex) {
        sceGxmSetUniformDataF(u, n_tf, 0, 4, tf);
        rgbf(RE(e, 0xCA) & 0xFFFFFF, env);
        sceGxmSetUniformDataF(u, n_env, 0, 4, env);
      }
    }
    if (tex) {
      sceGxmSetFragmentTexture(c, 0, &t);
    }
    zsSet(c, e, o[nv + n - 1].c >> 24);
    // GE cull (0x1D on, 0x9B which winding on screen goes); not in clears or rectangles
    uint32_t cl = w3d && prim != 6 && !(clr & 1) && (RE(e, 0x1D) & 1) ? (RE(e, 0x9B) & 1 ? SCE_GXM_CULL_CCW : SCE_GXM_CULL_CW) : SCE_GXM_CULL_NONE;
    if (cl != t_cl) { t_cl = cl; sceGxmSetCullMode(c, cl); }
    sceGxmSetVertexStream(c, 0, o + nv);
    sceGxmDraw(c, prim == 3 ? SCE_GXM_PRIMITIVE_TRIANGLES : prim == 5 ? SCE_GXM_PRIMITIVE_TRIANGLE_FAN : SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, tib, n);
    nv += n; t_draws++;
    DEV(t_us[3] += now() - tb;)
  }
  if (!has_base && res == T_OK) OPEN(0);  // (frame with no draws to the surface: it still gets the image underneath)
  if (opened) {  // (if something failed midway, the surface keeps what was drawn: not used)
    sceGxmSetCullMode(c, SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthFunc(c, SCE_GXM_DEPTH_FUNC_ALWAYS);  // the context is Adrenaline's: its defaults back
    sceGxmSetFrontDepthWriteEnable(c, SCE_GXM_DEPTH_WRITE_ENABLED);
    sceGxmSetFrontStencilFunc(c, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0, 0);
    sceGxmEndScene(c, 0, gEnd());
    cNote(cur_rt ? C_INT : C_TR, &gq_n, t_draws - d0, spx / 1000, ssw);
  }
#undef OPEN
#undef SCENE
  t_q[s] = gq;
  DEV(t_us[5] += now() - ta;)
  if (res && !t_seen[res]++) alog("translator: first frame with '%s' on drawing\n", tnames[res]);
  return res;
}
