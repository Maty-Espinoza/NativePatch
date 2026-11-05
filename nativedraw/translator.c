// translator.c: draws with the Vita GPU (GXM), at double size (960x544), a frame of the record (record.h: GE draws and
// states saved by record.c in the capturer). Included inside presenter.c, it uses its patcher, surfaces and context;
// the presenter calls trInit at startup and trFrame in the Adrenaline thread, instead of copying the PSP image. If the
// frame has something it cannot draw it returns the reason (T_*) and the presenter shows the normal image.
// Own shaders in shaders/*.cg.
#include <arm_neon.h>
#include "record.h"
#include "shaders_nd.h"
// Result of trCheck/trFrame: T_OK or the reason the frame was not drawn (tnames: its name in the log)
enum { T_OK, T_NEW, T_PRIM, T_FMT, T_BLEND, T_TFUNC, T_ODD, T_DEST, T_VERT, T_LIMIT, T_TEX, T_PROG, T_OUT, T_MASK, T_HOLD, TN };
static const char *tnames[TN] = { "ok", "no_new_frame", "primitive", "format", "blend", "texture_function",
                                "color_test_or_logic", "other_target", "vertex", "limit", "bad_texture", "program", "vertex_out", "mask", "held_back" };
// counters for the 10 s log (read and cleared by the presenter)
static volatile uint32_t t_cnt[TN], t_hd, t_nat, t_draws;
static volatile uint32_t t_skip[6];  // PSP frames advanced by each drawn frame (0: repeated, 5: 5 or more)
DEV(static volatile uint32_t t_us[6];)  // us in trFrame: check, textures and palettes, vertices, GXM setup, scene changes, all
// Vertices and draws in detail: see the translator detail lines of the log
DEV(enum { TC_VIN, TC_VOUT, TC_D3, TC_CUT, TC_FLAT, TC_SCIS, TC_LIT, TC_SAME, TC_VFAST, TC_VTEX, TC_VCOL, TC_FOG, TC_ATEST, TC_BLEND, TC_VGPU, TC_L0, TC_LPT, TC_LSP, TC_ENV, TC_NC };)
DEV(static volatile uint32_t t_c[TC_NC];)
// Split of the vertex work (switch vert_split), on 1 HD frame in 8, in two kinds of frame that alternate: [0] as it is, [1] reading each
// draw's data first (VS_TOUCH), so trVerts finds it in the cache. Clock reads add to each part; AGAIN repeats trVerts at once, with
// data and code in the cache. VS_FR frames, VS_ALL us in trFrame, VS_RUN us the thread really ran.
DEV(enum { VS_STATE, VS_FAST, VS_SLOW, VS_BIG, VS_TOUCH, VS_AGAIN_F, VS_AGAIN_S, VS_TRIS, VS_REST, VS_COPY, VS_NF, VS_NS, VS_NB, VS_TKB, VS_DRAWS, VS_FR, VS_ALL, VS_RUN, VS_N };
    static int v_on, v_fast;
    static volatile uint32_t v_us[2][VS_N];)
// thousands of pixels sent per program on the sampled frames, surface [0] and intermediate images [1] (before the z test)
DEV(enum { BP_FAST, BP_MID, BP_TEX, BP_COL, BP_BASE, BP_N };
    static volatile uint32_t b_px[2][BP_N], b_pxn;
    enum { BW_FUNC, BW_DOUBLE, BW_ATEST, BW_FOG, BW_BLEND2X, BW_N };  // why a textured draw is not on the fast program
    static volatile uint32_t b_why[BW_N], b_fr;
    // on the sampled frames, 3D on the surface by effect (bit 0 blended, 1 fog, 2 reads an intermediate image): thousands of pixels, draws
    static volatile uint32_t b_fx[8], b_fxd[8], b_fxn, b_lit[3];)  // b_lit: what nd_v4 lights: pixels, draws, vertices
static uint32_t t_seen[TN];   // times each reason was seen (the first one is logged in detail)
typedef struct { float x, y, z, w, u, v, f; uint32_t c; } TV;  // translated vertex (32 bytes, the nd_v format); f: fog (1: none)
static TV *tv[2];            // translated vertices, one per surface
static TV ts[1 << 12];       // a draw being translated: cached, because reading tv back is slow
static uint32_t *tpal[2];    // translated palettes (8888), aligned to 64: 16 entries (64 bytes) or 256 (1 KB)
#define TV_N (64 << 10)      // vertices per surface (and limit of the 16-bit indices)
#define TP_B (256 << 10)     // palette bytes per surface
#define C_RGB (SCE_GXM_COLOR_MASK_R | SCE_GXM_COLOR_MASK_G | SCE_GXM_COLOR_MASK_B)  // alpha is never written
// last record frame seen, whether it was drawn fine, how many times it was repeated and when one was last not drawable
static uint32_t t_frame = ~0u, t_ok, t_rep, t_bad;
static uint32_t t_mark[RD_N / 32 + 1];  // a bit per draw not done
static uint32_t t_prevd = ~0u, t_prev2 = ~0u;  // images shown by the last frames drawn, the latest first: t_prevd's surface is the one not being drawn
DEV(static void failNote(RFrame *f, uint32_t i, int r);)  // dump.c
static uint16_t *tib;        // 0, 1, 2... (indices for the GPU)
#define TG_N (64 << 10)      // indices of the joined draws of the GPU path, per surface
static uint16_t *tgi[2];
// Intermediate image: a game draw to another VRAM image (not the shown one) goes to our own double-size surface
// (1024x544); if later in the same frame a texture is at that address, it is read from that surface.
// fb: image address in VRAM (~0u: free); fr: last frame it was used.
// vr: what the frame draws on it (its scenes touch only that). sh: 1 if the image is kept at PSP size, half of its coordinates. have, dirty: for an image read while being drawn, the part
// already in rtc and the part drawn since (x0, y0, x1, y1).
typedef struct { uint32_t fb, fr, sh; void *mem; SceGxmColorSurface cs; SceGxmValidRegion vr; float have[4], dirty[4]; } RT;
// Scenes on different render targets may run at the same time: this object keeps those of a frame in order, because a
// scene reads what the one before drew
static SceGxmSyncObject *t_sync;
static RT rts[2], rtc;  // rtc: copy of the part of an image that a draw reads while drawing on that same image
// rtb: the boxed stretch of a frame (trBoxed): draws t_b0 to t_b1 - 1 go here, moved so the corner t_bx, t_by of the box is at
// 0, 0, and vr is the box: the edge of the image clips them, on the GPU. Then the box is pasted on the surface
static RT rtb;
static uint32_t t_b0, t_b1;
static float t_bx, t_by;
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

// --- programs: one vertex program (nd_v) and five fragment programs: nd_fc without texture, nd_ft with texture, nd_fm, its fast path,
// nd_fd, in between (modulate with doubling or fog, no alpha test), and nd_fa, nd_fd with a minimum alpha
// (swizzled ones arrive already sorted from the record). Each fragment program is built once per blend that appears
// (fps, up to 64) ---
static SceGxmVertexProgram *nvp, *gvp[3];  // gvp: the 3D vertices of the record as they are: nd_v3, nd_v4 with 16-bit UV, with float UV
static const SceGxmProgram *pft, *pfc, *pfm, *pfd, *pfa, *pv;
static SceGxmShaderPatcherId ift, ifc, ifm, ifd, ifa;
static const SceGxmProgramParameter *g_m, *g_tu, *g_fg, *g_cm, *g_ca, *g4[14], *g4gu, *n_wvp, *n_tf, *n_tm, *n_td, *n_ta, *n_atx, *n_atc, *n_env, *n_fgx, *n_fgc, *n_fgd, *n_fga;
typedef struct { uint64_t key; SceGxmFragmentProgram *p; } FP;
static FP fps[128];
static uint32_t nfps;
// Fragment program for tex (0 no texture, 1 textured, 2 fast textured, 3 nd_fd, 4 nd_fa), key (0 no blending; otherwise the 4 bytes of SceGxmBlendInfo + 1,
// see blendKey) and mask (SCE_GXM_COLOR_MASK_*, see colorMask). Finds it in fps or creates it with the patcher; 0 on
// failure.
static SceGxmFragmentProgram *fragProg(int tex, uint32_t key, uint32_t mask) {
  uint64_t k = (uint64_t)key << 8 | mask << 3 | tex;
  for (uint32_t i = 0; i < nfps; i++) if (fps[i].key == k) return fps[i].p;
  if (nfps >= 128) return 0;
  SceGxmBlendInfo b;
  if (key) sceClibMemcpy(&b, &(uint32_t){ key - 1 }, 4);
  else { sceClibMemset(&b, 0, sizeof b); b.colorFunc = b.alphaFunc = SCE_GXM_BLEND_FUNC_NONE; }
  b.colorMask = mask;
  SceGxmFragmentProgram *p = 0;
  int r = sceGxmShaderPatcherCreateFragmentProgram(pat, tex == 4 ? ifa : tex == 3 ? ifd : tex == 2 ? ifm : tex ? ift : ifc, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                   SCE_GXM_MULTISAMPLE_NONE, &b, pv, &p);
  if (r < 0) { alog("translator: fragment program %u %08x: %08x\n", tex, key, r); return 0; }
  fps[nfps].key = k; fps[nfps++].p = p;
  return p;
}

#ifdef NP_DEV
// Logs what the clock costs and the speed of a word read and written in cached memory and in the vertex memory (tv)
static void trBench(void) {
  static uint32_t cm[1024];
  volatile uint32_t *m[2] = { cm, (volatile uint32_t *)tv[0] }, x = 0;
  uint32_t t0 = now(), rd[2], wr[2];
  for (int i = 0; i < 1000; i++) x += now();
  uint32_t clk = now() - t0;
  for (int k = 0; k < 2; k++) {
    t0 = now();
    for (int p = 0; p < 16; p++) for (int i = 0; i < 1024; i++) m[k][i] = i;
    wr[k] = now() - t0; t0 = now();
    for (int p = 0; p < 16; p++) for (int i = 0; i < 1024; i++) x += m[k][i];
    rd[k] = now() - t0;
  }
  alog("translator: speed: clock %u ns | ns per word: cached read %u write %u, vertex memory read %u write %u\n", clk, rd[0] * 1000 / 16384, wr[0] * 1000 / 16384,
       rd[1] * 1000 / 16384, wr[1] * 1000 / 16384);
}
#endif

// Prepares the translator (called by presStart, only with draw): vertex, palette and index memory, registers the
// shaders and finds their parameters, creates the basic programs, reserves the intermediate images and lets the GPU
// read the PSP RAM and the record textures. 0 if ready; negative otherwise.
static int trInit(void) {
  for (int i = 0; i < 2; i++)
    if (!(tv[i] = gmem(TV_N * sizeof(TV), RD)) || !(tpal[i] = gmem(TP_B, RD))) return -1;
  if (!(tib = gmem(TV_N * 2, RD))) return -1;
  if (!(tgi[0] = gmem(TG_N * 4, RD))) return -1;
  tgi[1] = tgi[0] + TG_N;
  for (uint32_t i = 0; i < TV_N; i++) tib[i] = i;
  SceGxmShaderPatcherId iv;
  int r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_v_gxp, &iv);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_ft_gxp, &ift);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_fc_gxp, &ifc);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_fm_gxp, &ifm);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_fd_gxp, &ifd);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_fa_gxp, &ifa);
  if (r < 0) { alog("translator: register shaders %08x\n", r); return r; }
  pv = sceGxmShaderPatcherGetProgramFromId(iv); pft = sceGxmShaderPatcherGetProgramFromId(ift); pfc = sceGxmShaderPatcherGetProgramFromId(ifc); pfm = sceGxmShaderPatcherGetProgramFromId(ifm);
  const SceGxmProgramParameter *pa = sceGxmProgramFindParameterByName(pv, "aPosition"), *pt = sceGxmProgramFindParameterByName(pv, "aTexcoord"),
                               *pc = sceGxmProgramFindParameterByName(pv, "aColor");
  n_wvp = sceGxmProgramFindParameterByName(pv, "wvp");
  pfd = sceGxmShaderPatcherGetProgramFromId(ifd); n_td = sceGxmProgramFindParameterByName(pfd, "tf"); n_fgd = sceGxmProgramFindParameterByName(pfd, "fog");
  pfa = sceGxmShaderPatcherGetProgramFromId(ifa); n_ta = sceGxmProgramFindParameterByName(pfa, "tf"); n_fga = sceGxmProgramFindParameterByName(pfa, "fog");
  n_tf = sceGxmProgramFindParameterByName(pft, "tf"); n_atx = sceGxmProgramFindParameterByName(pft, "at");
  n_atc = sceGxmProgramFindParameterByName(pfc, "at"); n_tm = sceGxmProgramFindParameterByName(pfm, "tf"); n_env = sceGxmProgramFindParameterByName(pft, "env");
  n_fgx = sceGxmProgramFindParameterByName(pft, "fog"); n_fgc = sceGxmProgramFindParameterByName(pfc, "fog");
  if (!pa || !pt || !pc || !n_wvp || !n_tf || !n_atx || !n_atc || !n_env || !n_fgx || !n_fgc || !n_tm || !n_td || !n_fgd || !n_ta || !n_fga) { alog("translator: shader parameters not found\n"); return -2; }
  SceGxmVertexAttribute at[3] = { { 0, 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 4, sceGxmProgramParameterGetResourceIndex(pa) },
                                  { 0, 16, SCE_GXM_ATTRIBUTE_FORMAT_F32, 3, sceGxmProgramParameterGetResourceIndex(pt) },
                                  { 0, 28, SCE_GXM_ATTRIBUTE_FORMAT_U8N, 4, sceGxmProgramParameterGetResourceIndex(pc) } };
  SceGxmVertexStream st = { sizeof(TV), SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
  if ((r = sceGxmShaderPatcherCreateVertexProgram(pat, iv, at, 3, &st, 1, &nvp)) < 0) { alog("translator: vertex program %08x\n", r); return r; }
  SceGxmShaderPatcherId ig;
  if ((r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_v3_gxp, &ig)) < 0) { alog("translator: register nd_v3 %08x\n", r); return r; }
  const SceGxmProgram *pg = sceGxmShaderPatcherGetProgramFromId(ig);
  g_m = sceGxmProgramFindParameterByName(pg, "m"); g_tu = sceGxmProgramFindParameterByName(pg, "tu"); g_fg = sceGxmProgramFindParameterByName(pg, "fg");
  g_cm = sceGxmProgramFindParameterByName(pg, "cm"); g_ca = sceGxmProgramFindParameterByName(pg, "ca");
  SceGxmVertexAttribute ag[3] = { { 0, 12, SCE_GXM_ATTRIBUTE_FORMAT_F32, 3, sceGxmProgramParameterGetResourceIndex(sceGxmProgramFindParameterByName(pg, "aPosition")) },
                                  { 0, 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2, sceGxmProgramParameterGetResourceIndex(sceGxmProgramFindParameterByName(pg, "aTexcoord")) },
                                  { 0, 8, SCE_GXM_ATTRIBUTE_FORMAT_U8N, 4, sceGxmProgramParameterGetResourceIndex(sceGxmProgramFindParameterByName(pg, "aColor")) } };
  SceGxmVertexStream sg = { 24, SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
  r = sceGxmShaderPatcherCreateVertexProgram(pat, ig, ag, 3, &sg, 1, &gvp[0]);
  alog("translator: GPU vertex program %08x\n", r);
  if (r < 0 || !g_m || !g_tu || !g_fg || !g_cm || !g_ca) gvp[0] = 0;
  SceGxmShaderPatcherId i4;
  if ((r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)nd_v4_gxp, &i4)) < 0) { alog("translator: register nd_v4 %08x\n", r); return r; }
  const SceGxmProgram *p4 = sceGxmShaderPatcherGetProgramFromId(i4);
  static const char *const n4[14] = { "m", "tu", "fg", "nx", "ny", "nz", "ld", "hv", "ba", "cd", "cs", "t0", "t1", "t2" };
  int ok4 = !!(g4gu = sceGxmProgramFindParameterByName(p4, "gu"));
  for (int i = 0; i < 14; i++) ok4 = (g4[i] = sceGxmProgramFindParameterByName(p4, n4[i])) && ok4;
  uint32_t rp = sceGxmProgramParameterGetResourceIndex(sceGxmProgramFindParameterByName(p4, "aPosition")), rn = sceGxmProgramParameterGetResourceIndex(sceGxmProgramFindParameterByName(p4, "aNormal")),
           ru = sceGxmProgramParameterGetResourceIndex(sceGxmProgramFindParameterByName(p4, "aTexcoord"));
  SceGxmVertexAttribute a1[3] = { { 0, 10, SCE_GXM_ATTRIBUTE_FORMAT_S16, 3, rp }, { 0, 4, SCE_GXM_ATTRIBUTE_FORMAT_S16, 3, rn }, { 0, 0, SCE_GXM_ATTRIBUTE_FORMAT_U16, 2, ru } },
                        a2[3] = { { 0, 14, SCE_GXM_ATTRIBUTE_FORMAT_S16, 3, rp }, { 0, 8, SCE_GXM_ATTRIBUTE_FORMAT_S16, 3, rn }, { 0, 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2, ru } };
  SceGxmVertexStream s1 = { 16, SCE_GXM_INDEX_SOURCE_INDEX_16BIT }, s2 = { 20, SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
  int r1v = sceGxmShaderPatcherCreateVertexProgram(pat, i4, a1, 3, &s1, 1, &gvp[1]), r2v = sceGxmShaderPatcherCreateVertexProgram(pat, i4, a2, 3, &s2, 1, &gvp[2]);
  alog("translator: GPU lit vertex programs %08x %08x\n", r1v, r2v);
  if (r1v < 0 || !ok4) gvp[1] = 0;
  if (r2v < 0 || !ok4) gvp[2] = 0;
  if (!fragProg(0, 0, C_RGB) || !fragProg(1, 0, C_RGB)) return -3;
  for (int i = 0; i < 2; i++) rtMem(&rts[i]);
  rtMem(&rtc); rtMem(&rtb);
  if ((r = sceGxmSyncObjectCreate(&t_sync)) < 0) { alog("translator: sync object %08x\n", r); return r; }
  // PSP RAM (0x08000000-0x0A000000) for the GPU (fails with 0x72000000)
  int r1 = sceGxmMapMemory((void *)0x70000000, 0x2000000, RD);
  int r3 = regMapGpu();
  alog("translator: PSP RAM for the GPU %08x, record textures %08x\n", r1, r3);
  DEV(trBench();)
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

// Gray of a blend src * gray + dst * gray (fixed colors 0xE0 and 0xE1 equal, not 0 or white), 0 if it is not one. GXM has no
// fixed factors: the vertex color carries it, gray in rgb for the source and in alpha for the destination (see trCheck).
static uint32_t grayFix(const uint32_t *e) {
  uint32_t df = RE(e, 0xDF), fa = RE(e, 0xE0) & 0xFFFFFF, g = fa & 255;
  return (df & 0xFFF) == 0xAA && fa == (RE(e, 0xE1) & 0xFFFFFF) && fa == g * 0x10101 && g && g != 255 ? g : 0;
}
// 1 if color test of state e changes nothing: it rejects black, and black added to the destination leaves it the same
static int ctestNoop(const uint32_t *e) {
  return (RE(e, 0xD8) & 3) == 3 && !(RE(e, 0xD9) & 0xFFFFFF) && (RE(e, 0xDA) & 0xFFFFFF) == 0xFFFFFF && RE(e, 0x21) & 1 &&
         !(RE(e, 0xDF) >> 8 & 15) && (RE(e, 0xDF) >> 4 & 15) == 10 && (RE(e, 0xE1) & 0xFFFFFF) == 0xFFFFFF;
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
  if (grayFix(e)) { gs = SCE_GXM_BLEND_FACTOR_ONE; gd = SCE_GXM_BLEND_FACTOR_SRC_ALPHA; goto ready; }
  if (fa <= 0x010101) fa = 0;  // (1 of 255 at most: nothing, there is no factor for it)
  if (fb <= 0x010101) fb = 0;
  if (op == 3 || op == 4) gs = SCE_GXM_BLEND_FACTOR_ONE; else if (s < 4 || s == 6 || s == 7) gs = fs[s]; else if (s == 10 && (fa == 0 || fa == 0xFFFFFF)) gs = fa ? SCE_GXM_BLEND_FACTOR_ONE : SCE_GXM_BLEND_FACTOR_ZERO; else return ~0u;
  if (op == 3 || op == 4) gd = SCE_GXM_BLEND_FACTOR_ONE; else if (d < 4 || d == 6 || d == 7) gd = fd[d]; else if (d == 10 && (fb == 0 || fb == 0xFFFFFF)) gd = fb ? SCE_GXM_BLEND_FACTOR_ONE : SCE_GXM_BLEND_FACTOR_ZERO; else return ~0u;
ready:
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

// 1 if image ta (in VRAM) has an intermediate image held from this or an earlier frame
static int rtKept(uint32_t ta) {
  for (int i = 0; i < 2; i++) if (rts[i].mem && rts[i].fb == (ta & 0x1FFFF0) && (ta & 0x0F000000) == 0x04000000) return 1;
  return 0;
}
// Checks whether draw d of frame f can be drawn (primitive, vertex type, destination, blend,
// texture format and function). T_OK if so; otherwise the reason. What depends on other draws is checked by trFrame.
static int trCheck(RFrame *f, RDraw *d) {
  const uint32_t *e = f->e[d->st];
  uint32_t vt = d->vt, clr = RE(e, 0xD3) & 1, afn, aref;
  if (!d->prim || d->prim > 6) return T_PRIM;  // lines, line strip, triangles, strip, fan, rectangles
  if ((vt >> 18 & 7) || ((vt >> 9 & 3) && (vt >> 23 & 1))) return T_VERT;  // morph, or weights without transform
  if ((RE(e, 0x9D) & 0x7FF) > 512) return T_DEST;
  if (colorMask(e) == ~0u) return T_MASK;
  if (clr) return T_OK;
  if ((RE(e, 0x27) & 1 && !ctestNoop(e)) || RE(e, 0x28) & 1) return T_ODD;
  if ((RE(e, 0x22) & 1) && !alphaFn(e, &afn, &aref)) return T_MASK;
  if (blendKey(e) == ~0u) return T_BLEND;
  // (the gray blend uses the vertex alpha: the source's alpha must be 1, as in intermediate images, and modulated)
  if (grayFix(e) && hasTex(e, d) && ((d->toff && !rtKept(texAddr(e))) || (RE(e, 0xC9) & 7))) return T_BLEND;
  if (hasTex(e, d)) {
    uint32_t fmt = RE(e, 0xC3) & 15, tf = RE(e, 0xC9) & 7;
    if (fmt > 5 || (fmt >= 4 && !d->clut)) return T_FMT;
    if (tf > 4) return T_TFUNC;
    // (texture with no copy in the record: trFrame checks whether it is an intermediate image of this frame)
  }
  return T_OK;
}

// 1 if draw d goes to an image other than the displayed one that the translator cannot draw: wider than 512, or a state with
// no equivalent. It is left out: only what reads that image can miss it
static int trSkip(RFrame *f, RDraw *d) {
  int r = trCheck(f, d);
  return r == T_DEST || r == T_ODD || r == T_MASK || r == T_BLEND;
}
// Color of a vertex at p with GE color type ct (4 5650, 5 5551, 6 4444, 7 8888) -> 0xAABBGGRR
static uint32_t col8888(const uint8_t *p, uint32_t ct) {
  if (ct == 7) return *(const uint32_t *)p;
  return pal8888(*(const uint16_t *)p, ct - 4);
}

#ifdef NP_DEV
// Why the vertices of a 3D draw do not go through nd_v3: other vertex type, primitive, lights on, generated UV, flat color,
// scissor, not marked by the record, refused here. The first that applies. wr: draws with a partial scissor, by rectangle
// and target (surface or intermediate image); wv: their vertices, wo: those that only the scissor keeps out of nd_v3.
enum { WN_FMT, WN_PRIM, WN_LIGHT, WN_GEN, WN_FLAT, WN_SCIS, WN_MARK, WN_HERE, WN_N, WR_N = 16 };
static volatile uint32_t wn[WN_N];
static struct { uint32_t d4, d5, dst, draws, wv, wo; } wr[WR_N];
static volatile uint32_t wr_n, wr_lost;
static void trWhy(const RDraw *d, const uint32_t *e, int ps, int dst, int lit, uint32_t gen) {
  uint32_t r = d->vt != 0x19F && !GVL(d->vt) ? WN_FMT : d->prim < 3 || d->prim > 5 ? WN_PRIM : lit ? WN_LIGHT : gen ? WN_GEN : !(RE(e, 0x50) & 1) ? WN_FLAT : ps ? WN_SCIS :
               d->voff & VX ? WN_HERE : WN_MARK, i, d4 = RE(e, 0xD4) & 0xFFFFF, d5 = RE(e, 0xD5) & 0xFFFFF;
  wn[r] += d->n;
  if (!ps) return;
  for (i = 0; i < wr_n && !(wr[i].d4 == d4 && wr[i].d5 == d5 && wr[i].dst == (uint32_t)dst); i++);
  if (i == WR_N) { wr_lost += d->n; return; }
  if (i == wr_n) { wr[i].d4 = d4; wr[i].d5 = d5; wr[i].dst = dst; wr_n++; }
  wr[i].draws++; wr[i].wv += d->n; wr[i].wo += r == WN_SCIS ? d->n : 0;
}
#endif

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
  trRaw("translator: vertex_out draw", RV(f, d), d->vlen);
}
#endif

// --- 3D: what the GE does per vertex, done here. T3 holds a draw's data, with the matrices already multiplied ---
typedef struct { uint32_t on, type, comp; float p[3], a[3], sd[3], cut, ex, ca[3], cd[3], cs[3]; } LT;
typedef struct {
  float m[16], w[12], wv[12];              // model -> clip, -> world, -> eye
  float us, vs, uo, vo, tw, th, tm[12];    // UV scale and offset, texture size, texture matrix
  float sx, sy, sz, tx, ty, tz;            // viewport less offset
  float f1, f2, em[4], am[4], ad[4], as[4], ac[4], sp;  // fog; emissive, ambient, diffuse, specular, scene ambient colors
  uint32_t gen, prj, ls0, ls1, upd, fog, lit, rev, nl;  // 0xC0, 0xC1, 0x53, on/off switches, lights on
  uint32_t ei[4], ai[4], lc;                            // lit with no lights: emissive (0-255) and scene ambient (0-256) per channel, color
  float b[96];                                          // the 8 bones, 12 values each (vertices with weights)
  uint32_t kw, km, kl; const uint32_t *ke;              // what t3Set last read
  LT l[4];
} T3;
static T3 t3;
static float f24(uint32_t v) { union { uint32_t u; float f; } x = { v << 8 }; return x.f; }
#ifdef __arm__
static inline float tsqrt(float x) { __asm__("vsqrt.f32 %0, %1" : "=t"(x) : "t"(x)); return x; }
#else  // (the bench on the PC)
static inline float tsqrt(float x) { return __builtin_sqrtf(x); }
#endif
static float tpow(float b, int e) { float r = 1; for (; e > 0; e >>= 1, b *= b) if (e & 1) r *= b; return r; }
static void tnorm(float *v) { float d = v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; if (d > 0) { d = 1 / tsqrt(d); v[0] *= d; v[1] *= d; v[2] *= d; } }
static void rgbf(uint32_t c, float *o) { const float k = 1 / 255.0f; o[0] = (c & 255) * k; o[1] = (c >> 8 & 255) * k; o[2] = (c >> 16 & 255) * k; o[3] = (c >> 24 & 255) * k; }
// Lit color with no lights on, for material color m (0xAABBGGRR): emissive + scene ambient * material
static uint32_t t3Flat(const T3 *t, uint32_t m) {
  uint32_t r = 0;
  for (int k = 0; k < 4; k++) { uint32_t v = t->ei[k] + ((m >> k * 8 & 255) * t->ai[k] >> 8); r |= (v > 255 ? 255 : v) << k * 8; }
  return r;
}
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
  if (d->vt >> 9 & 3) for (int i = 0; i < 96; i++) t->b[i] = f24(w[12 + i]);  // (behind the world matrix in the same block)
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
  t->nl = 0;
  for (int i = 0; i < 4; i++) {
    LT *q = &t->l[i];
    float s[4];
    q->on = RE(e, 0x18 + i) & 1; t->nl += q->on; q->type = l[0x5F + i] >> 8 & 3; q->comp = l[0x5F + i] & 3;
    for (int k = 0; k < 3; k++) { q->p[k] = f24(l[0x63 + i * 3 + k]); q->sd[k] = f24(l[0x6F + i * 3 + k]); q->a[k] = f24(l[0x7B + i * 3 + k]); }
    q->ex = f24(l[0x87 + i]); q->cut = f24(l[0x8B + i]);
    rgbf(l[0x8F + i * 3] & 0xFFFFFF, s); sceClibMemcpy(q->ca, s, 12);
    rgbf(l[0x90 + i * 3] & 0xFFFFFF, s); sceClibMemcpy(q->cd, s, 12);
    rgbf(l[0x91 + i * 3] & 0xFFFFFF, s); sceClibMemcpy(q->cs, s, 12);
    if (!q->type) tnorm(q->p);
    tnorm(q->sd);
  }
  for (int k = 0; k < 4; k++) { t->ei[k] = t->em[k] * 255 + .5f; t->ai[k] = t->ac[k] * 256 + .5f; }
  t->lc = t3Flat(t, (l[0x55] & 0xFFFFFF) | l[0x58] << 24);
}
// The only light on of t, if the vertex shader can do it: directional, without powered diffuse. 0 if not.
static const LT *t3One(const T3 *t) {
  const LT *r = 0;
  for (int i = 0; i < 4; i++) if (t->l[i].on) { if (r) return 0; r = &t->l[i]; }
  return r && !r->type && r->comp != 2 ? r : 0;
}
// Uniforms of nd_v3 (lv 0) or nd_v4 (lv: GVL) for a draw of t with texture size tw x th and material color mat, in the default buffer u; wm: the scene's matrix.
// What trVerts does per vertex, as numbers: m is wm x viewport x t->m, column by column (model -> surface clip); 16-bit values count
// 1 / 32768, as on the GE.
static void t3Uni(void *u, const T3 *t, float tw, float th, const float *wm, int lv, uint32_t mat) {
  const float k16 = 1 / 32768.0f;
  float m[16], e[4], kz = t->sz / -131072.0f, kt = .5f - t->tz / 131072.0f, ku = t->tw / tw, kv = t->th / th, kp = lv ? k16 : 1, kx = lv == 1 ? k16 : 1;
  for (int j = 0; j < 4; j++) {
    const float *c = t->m + j * 4;
    e[0] = 2 * (t->sx * c[0] + t->tx * c[3]); e[1] = 2 * (t->sy * c[1] + t->ty * c[3]); e[2] = kz * c[2] + kt * c[3]; e[3] = c[3];
    for (int i = 0; i < 4; i++) m[j * 4 + i] = (e[0] * wm[i] + e[1] * wm[4 + i] + e[2] * wm[8 + i] + e[3] * wm[12 + i]) * (j < 3 ? kp : 1);
  }
  float tu[4] = { t->us * ku * kx, t->vs * kv * kx, t->uo * ku, t->vo * kv }, fg[4] = { 0, 0, 0, 1 }, cm[4] = { 1, 1, 1, 1 }, ca[4] = { 0, 0, 0, 0 };
  if (t->fog) { fg[0] = t->wv[2] * t->f2 * kp; fg[1] = t->wv[5] * t->f2 * kp; fg[2] = t->wv[8] * t->f2 * kp; fg[3] = (t->wv[11] + t->f1) * t->f2; }
  if (lv) {  // (t3Light with no vertex color and the light's direction and half vector already fixed, or t3Gen from the normal)
    const LT *l = t->lit ? t3One(t) : 0;
    float s = t->rev && l ? -1 : 1, nx[4] = { t->w[0] * s, t->w[3] * s, t->w[6] * s, 0 }, ny[4] = { t->w[1] * s, t->w[4] * s, t->w[7] * s, 0 }, nz[4] = { t->w[2] * s, t->w[5] * s, t->w[8] * s, 0 };
    float ld[4] = { 0, 0, 1, 0 }, hv[4] = { 0, 0, 1, 1 }, ba[4], cd[4] = { 0, 0, 0, 0 }, cs[4] = { 0, 0, 0, 0 };
    float gu[4] = { ku, kv, !l, 0 }, t0[4] = { t->tm[0] * k16, t->tm[3] * k16, t->tm[6] * k16, t->tm[9] }, t1[4] = { t->tm[1] * k16, t->tm[4] * k16, t->tm[7] * k16, t->tm[10] },
          t2[4] = { t->tm[2] * k16, t->tm[5] * k16, t->tm[8] * k16, t->tm[11] };
    rgbf(mat, ba);
    if (l) {
      sceClibMemcpy(ld, l->p, 12); sceClibMemcpy(hv, l->p, 12); hv[2] += 1; hv[3] = (int)(t->sp + .5f);
      tnorm(hv);
      for (int k = 0; k < 4; k++) ba[k] = t->em[k] + t->ac[k] * t->am[k] + (k < 3 ? l->ca[k] * t->am[k] : 0);
      for (int k = 0; k < 3; k++) { cd[k] = l->cd[k] * t->ad[k]; cs[k] = l->comp == 1 ? l->cs[k] * t->as[k] : 0; }
    }
    const float *v[14] = { m, tu, fg, nx, ny, nz, ld, hv, ba, cd, cs, t0, t1, t2 };
    sceGxmSetUniformDataF(u, g4[0], 0, 16, m);
    for (int i = 1; i < 14; i++) sceGxmSetUniformDataF(u, g4[i], 0, 4, v[i]);
    sceGxmSetUniformDataF(u, g4gu, 0, 4, gu);
    return;
  }
  for (int k = 0; t->lit && k < 4; k++) {  // (lit with no lights: see t3Flat)
    cm[k] = t->upd & 1 ? t->ai[k] / 256.0f : 0;
    ca[k] = (t->upd & 1 ? t->ei[k] : t->lc >> k * 8 & 255) / 255.0f;
  }
  sceGxmSetUniformDataF(u, g_m, 0, 16, m); sceGxmSetUniformDataF(u, g_tu, 0, 4, tu); sceGxmSetUniformDataF(u, g_fg, 0, 4, fg);
  sceGxmSetUniformDataF(u, g_cm, 0, 4, cm); sceGxmSetUniformDataF(u, g_ca, 0, 4, ca);
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
  uint32_t vt = d->vt, tc = vt & 3, ct = vt >> 2 & 7, nt = vt >> 5 & 3, pt = vt >> 7 & 3, wt = vt >> 9 & 3, wn = (vt >> 14 & 7) + 1, sz = 0, al = 1, s;
  static uint32_t lvt = ~0u, lsz, lt, lc, lp, ln;  // the layout of the last vertex type
  uint32_t otc = 0, ocol = 0, opos = 0, onrm = 0, ow;
  if (vt == lvt) { sz = lsz; otc = lt; ocol = lc; opos = lp; onrm = ln; }
  else {
  // FLD: n components of size bytes, aligned to size (o: offset). Order: weights (at 0), texture, color, normal, position
#define FLD(n, size, o) s = (size); if (s) { sz = (sz + s - 1) & ~(s - 1); o = sz; sz += (n) * s; if (s > al) al = s; }
  FLD(wn, s4[wt], ow); FLD(2, s4[tc], otc); FLD(1, cs[ct], ocol); FLD(3, s4[nt], onrm); FLD(3, s4[pt], opos);
#undef FLD
  sz = (sz + al - 1) & ~(al - 1);
  lvt = vt; lsz = sz; lt = otc; lc = ocol; lp = opos; ln = onrm;
  }
  if (!sz || !pt) return T_VERT;
  const uint8_t *vb = RV(f, d), *ib = d->ioff ? f->a + d->ioff - 1 : 0;  // (offsets + 1; 0: absent)
  uint32_t ix = d->vt >> 11 & 3, n = 0;
  float itw = 1 / tw, ith = 1 / th;
  // The common 3D vertex: float position and texture coordinates, 8888 color, no lights on and no generated texture coordinates. The
  // four columns of the matrix and the viewport go in NEON vectors: x, y, z and w come out together, in the order of TV
  if (t && tc == 3 && ct == 7 && !nt && pt == 3 && !wt && !t->nl && !t->gen && d->prim != 6) {
    if (d->n > TV_N) return T_LIMIT;
    const float *m = t->m;
    float32x4_t c0 = vld1q_f32(m), c1 = vld1q_f32(m + 4), c2 = vld1q_f32(m + 8), c3 = vld1q_f32(m + 12);
    const float vs[4] = { t->sx * 2, t->sy * 2, t->sz / -131072.0f, 0 }, vt4[4] = { t->tx * 2, t->ty * 2, .5f - t->tz / 131072.0f, 1 };
    float32x4_t sv = vld1q_f32(vs), tv4 = vld1q_f32(vt4);
    DEV(v_fast = 1;)
    float us = t->us, uo = t->uo, vsc = t->vs, vo = t->vo, ku = t->tw * itw, kv = t->th * ith;
    int flat = t->lit && (t->upd & 1), fog = t->fog, lit = t->lit;
    for (uint32_t i = 0; i < d->n; i++) {
      uint32_t k = !ib ? i : ix == 1 ? ib[i] : ix == 2 ? ((const uint16_t *)ib)[i] : ((const uint32_t *)ib)[i];
      if ((k + 1) * sz > d->vlen) return T_VERT;
      const uint8_t *p = vb + k * sz;
      const float *x = (const float *)(p + opos), *uv = (const float *)(p + otc);
      float32x4_t c = vmlaq_n_f32(vmlaq_n_f32(vmlaq_n_f32(c3, c0, x[0]), c1, x[1]), c2, x[2]);
      float w = vgetq_lane_f32(c, 3);
      if (!(w > -1e9f && w < 1e9f)) { DEV(trOutLog(f, d, i, k, sz, p);) return T_OUT; }
      vst1q_f32(&o[n].x, vmlaq_n_f32(vmulq_f32(c, sv), tv4, w));
      o[n].u = (uv[0] * us + uo) * ku; o[n].v = (uv[1] * vsc + vo) * kv;
      uint32_t cl = *(const uint32_t *)(p + ocol);
      o[n].c = flat ? t3Flat(t, cl) : lit ? t->lc : cl;
      float g = 1;
      if (fog) { g = (x[0] * t->wv[2] + x[1] * t->wv[5] + x[2] * t->wv[8] + t->wv[11] + t->f1) * t->f2; g = g < 0 ? 0 : g > 1 ? 1 : g; }
      o[n++].f = g;
    }
    *nout = n;
    return T_OK;
  }
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
      if (wt) {  // position and normal: the sum of the bones, each by its weight
        float sx[3] = { 0, 0, 0 }, sn[3] = { 0, 0, 0 };
        for (uint32_t b = 0; b < wn; b++) {
          const uint8_t *q8 = p + b * s4[wt];
          float g = wt == 3 ? *(const float *)q8 : wt == 2 ? *(const uint16_t *)q8 * ks[2] : *q8 * ks[1];
          const float *m = t->b + b * 12;
          for (int r = 0; r < 3; r++) {
            sx[r] += g * (x[0] * m[r] + x[1] * m[3 + r] + x[2] * m[6 + r] + m[9 + r]);
            sn[r] += g * (n[0] * m[r] + n[1] * m[3 + r] + n[2] * m[6 + r]);
          }
        }
        sceClibMemcpy(x, sx, 12); sceClibMemcpy(n, sn, 12);
      }
      if (tc) {
        const uint8_t *b = p + otc;
        u = tc == 3 ? ((const float *)b)[0] : tc == 2 ? ((const uint16_t *)b)[0] * ks[2] : b[0] * ks[1];
        v = tc == 3 ? ((const float *)b)[1] : tc == 2 ? ((const uint16_t *)b)[1] * ks[2] : b[1] * ks[1];
      }
      if (t->lit || t->gen == 2) {
        if (t->rev) n[0] = -n[0], n[1] = -n[1], n[2] = -n[2];
        if (t->nl || t->gen == 2) {
          for (int r = 0; r < 3; r++) nw[r] = n[0] * t->w[r] + n[1] * t->w[3 + r] + n[2] * t->w[6 + r];
          tnorm(nw);
        }
      }
      if (t->lit && t->nl) { t3World(t, x, wp); q.c = t3Light(t, wp, nw, q.c, ct != 0); }
      else if (t->lit) q.c = ct && t->upd & 1 ? t3Flat(t, q.c) : t->lc;
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
    if (!t && !(d->prim == 5 && pt == 2) && !(q.x > -4096 && q.x < 4096 && q.y > -4096 && q.y < 4096)) { DEV(trOutLog(f, d, i, k, sz, p);) return T_OUT; }
    q.x *= 2; q.y *= 2; q.u *= itw; q.v *= ith;
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

#ifdef NP_DEV
// Reads one word per cache line of draw d's vertices and indices (vert_split). Returns the KB read
static uint32_t trTouch(RFrame *f, const RDraw *d) {
  static const uint8_t is[4] = { 0, 1, 2, 4 };
  const volatile uint32_t *p = (const uint32_t *)((uint32_t)RV(f, d) & ~31u), *e = (const uint32_t *)(RV(f, d) + d->vlen);
  uint32_t b = (e - p) * 4;
  for (; p < e; p += 8) (void)*p;
  if (d->ioff) {
    p = (const uint32_t *)((uint32_t)(f->a + d->ioff - 1) & ~31u); e = (const uint32_t *)(f->a + d->ioff - 1 + d->n * is[d->vt >> 11 & 3]);
    b += (e - p) * 4;
    for (; p < e; p += 8) (void)*p;
  }
  return b;
}
#endif

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

// Which edges of scissor sc vertex v is beyond, one bit each; 16 if it is behind the camera
static inline uint32_t trOut(const TV *v, const float *sc) {
  float w = v->w;
  return !(w > 1e-4f) ? 16 : (v->x < sc[0] * w) | (v->y < sc[1] * w) << 1 | (v->x > sc[2] * w) << 2 | (v->y > sc[3] * w) << 3;
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
    uint32_t oa = trOut(&t[0], sc), ob = trOut(&t[1], sc), oc = trOut(&t[2], sc);
    if (oa & ob & oc) continue;  // (all beyond the same edge)
    if (oa | ob | oc) m += trClipTri(t, sc, o + n + m);
    else { sceClibMemcpy(o + n + m, t, sizeof t); m += 3; }  // (all inside)
  }
  sceClibMemmove(o, o + n, m * sizeof(TV));
  *np = m; *prim = 3;
  return T_OK;
}

// Lines (strip: line strip) of *np vertices at o, room for room: GXM lines need another mode, so each segment becomes two
// triangles one PSP pixel wide (2 at double size), left in o as a triangle list. x and y carry their w. Segments with a
// point behind the camera are dropped. T_OK or T_LIMIT.
static int trLines(TV *o, uint32_t *np, uint32_t room, int strip) {
  uint32_t n = *np, m = 0;
  for (uint32_t i = 0; i + 1 < n; i += strip ? 1 : 2) {
    TV a = o[i], b = o[i + 1];
    if (a.w <= 0 || b.w <= 0) continue;
    if (n + m + 6 > room) return T_LIMIT;
    float dx = b.x / b.w - a.x / a.w, dy = b.y / b.w - a.y / a.w, d = dx * dx + dy * dy;
    if (d <= 0) continue;
    d = 1 / tsqrt(d); dx *= d; dy *= d;
    TV q[4] = { a, a, b, b };
    q[0].x -= dy * a.w; q[0].y += dx * a.w; q[1].x += dy * a.w; q[1].y -= dx * a.w;
    q[2].x -= dy * b.w; q[2].y += dx * b.w; q[3].x += dy * b.w; q[3].y -= dx * b.w;
    TV *t = o + n + m;
    t[0] = q[0]; t[1] = q[1]; t[2] = q[2]; t[3] = q[2]; t[4] = q[1]; t[5] = q[3];
    m += 6;
  }
  sceClibMemmove(o, o + n, m * sizeof(TV));
  *np = m;
  return T_OK;
}

// Texture of tw x th texels (0xB8) in a buffer of tbw at src, narrower than the buffer, and t made with the buffer's
// width: repeat and clamp would act at the buffer's edge. If the n vertices at o leave tw, or there are none (o = 0: the GPU
// reads them), copies its rows without the gap to dst (room words), narrows t and rescales u. Returns the words used (0: not
// needed), ~0u if it does not fit.
static uint32_t trNarrow(SceGxmTexture *t, const uint8_t *src, uint32_t fmt, uint32_t tbw, uint32_t tw, uint32_t th, TV *o, uint32_t n, uint32_t *dst, uint32_t room) {
  static const uint8_t bp[6] = { 16, 16, 16, 32, 4, 8 };
  float k = (float)tbw / tw;
  uint32_t out = !o, ri = tbw * bp[fmt] / 8, ro = ((tw + 7) & ~7u) * bp[fmt] / 8, w;  // (GXM stride: 8 texels)
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
#ifdef NP_DEV
// Pixels of the 3D triangles at o (x and y carry their w), clamped to the screen: one side only if the draw culls, since half
// of them would not be drawn. Triangles behind the camera are left out.
static float trPx3(const TV *o, uint32_t n, uint32_t prim, int cull) {
  float s[2] = { 0, 0 };
  for (uint32_t i = 0; i + 2 < n; i += prim == 3 ? 3 : 1) {
    const TV *t[3] = { o + (prim == 5 ? 0 : i), o + i + 1, o + i + 2 };
    float x[3], y[3];
    int ok = 1;
    for (int j = 0; j < 3; j++) {
      if (t[j]->w <= 0) { ok = 0; break; }
      float a = t[j]->x / t[j]->w, b = t[j]->y / t[j]->w;
      x[j] = a < 0 ? 0 : a > LINE ? LINE : a; y[j] = b < 0 ? 0 : b > H ? H : b;
    }
    if (!ok) continue;
    float a = ((x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0])) * .5f;
    if (prim == 4 && (i & 1)) a = -a;
    s[a < 0] += a < 0 ? -a : a;
  }
  return cull ? (s[0] > s[1] ? s[0] : s[1]) : s[0] + s[1];
}
#endif
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
// tr_wvp for a scene with valid region r (0: the whole surface): the GPU's viewport covers only the region
static void trWvp(float *m, const SceGxmValidRegion *r) {
  sceClibMemcpy(m, tr_wvp, sizeof tr_wvp);
  if (r) { m[0] = 2.0f / (r->xMax + 1); m[5] = -2.0f / (r->yMax + 1); }
}
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

// Checks whether frame f can be drawn: every draw (trCheck), the destinations (at most 2 intermediate images that do
// not share memory) and the textures with no copy: the shown image read while drawing an intermediate one, or an
// intermediate image in its format, of this frame (it may be the one being drawn) or kept from an earlier one. T_OK or
// the reason, logging the first draw with each reason. Leaves in *disp_p the image being shown and in *ord_p how many
// draws from the start can be reordered, intermediate images first and the surface after them in a single scene: up to
// the first that needs the game's order (reads the surface, or draws on an image already read). Without that order a
// surface scene would end right after its clear, and its depth does not survive to the next.
static int trCheckFrame(RFrame *f, uint32_t *disp_p, uint32_t *ord_p) {
  // disp: image being shown; ifb, iend, ipsm: intermediate images, where their memory ends, their format
  uint32_t disp = f->fb & 0x1FFFF0, ifb[2], iend[2], ipsm[2], nif = 0, n_disp = 0;
  uint32_t ord = f->nd, rd_mask = 0;  // (rd_mask: bit per intermediate image read by a draw)
  for (uint32_t i = 0; i < f->nd && !n_disp; i++) n_disp = (RE(f->e[f->d[i].st], 0x9C) & 0x1FFFF0) == disp;
  // no draw to the shown image (game behind: the flip has the other one of the double buffer): use the last draw's one
  if (!n_disp && f->nd) disp = RE(f->e[f->d[f->nd - 1].st], 0x9C) & 0x1FFFF0;
  // From the end: a draw on an image nobody reads later is not done. What it leaves is for the PSP (say a palette the game
  // builds with the GE), and the Vita gets that from the mirror
  sceClibMemset(t_mark, 0, sizeof t_mark);
  uint32_t live[8], nl = 0;
  for (uint32_t i = f->nd; i-- > 0;) {
    RDraw *d = &f->d[i];
    // (same state as the draw after it: same answer)
    if (i + 1 < f->nd && d[1].st == d->st && !d[1].toff == !d->toff && d[1].vt == d->vt && !(t_mark[(i + 1) >> 5] >> ((i + 1) & 31) & 1)) continue;
    const uint32_t *e = f->e[d->st];
    uint32_t fb = RE(e, 0x9C) & 0x1FFFF0, k;
    for (k = 0; k < nl && live[k] != fb; k++) {}
    if (fb != disp && k == nl && nl < 8) { t_mark[i >> 5] |= 1u << (i & 31); continue; }
    uint32_t ta = texAddr(e);
    if (hasTex(e, d) && !d->toff && (ta & 0x0F000000) == 0x04000000 && (ta & 0x1FFFF0) != disp) {
      for (k = 0; k < nl && live[k] != (ta & 0x1FFFF0); k++) {}
      if (k == nl && nl < 8) live[nl++] = ta & 0x1FFFF0;
    }
  }
  const RDraw *cd = 0;
  for (uint32_t i = 0; i < f->nd; i++) {
    if (t_mark[i >> 5] >> (i & 31) & 1) continue;
    const RDraw *dk = &f->d[i];  // (what trCheck looks at is usually the same as in the last draw checked)
    if (cd && cd->st == dk->st && cd->prim == dk->prim && cd->vt == dk->vt && !cd->toff == !dk->toff && !cd->clut == !dk->clut) continue;  // (everything below would give the same)
    int r = trCheck(f, &f->d[i]);
    cd = dk;
    const uint32_t *e0 = f->e[f->d[i].st];
    uint32_t fb = RE(e0, 0x9C) & 0x1FFFF0, k;
    int kd = -1;  // destination intermediate image (-1: the surface)
    if (fb != disp && trSkip(f, &f->d[i])) { DEV(static int once; if (!once++) alog("translator: draws to image %06x/%u are left out, it cannot be drawn\n", fb, RE(e0, 0x9D) & 0x7FF);) continue; }
    if (!r && fb != disp) {
      for (k = 0; k < nif && ifb[k] != fb; k++) {}
      if (k == nif) {
        uint32_t end = fb + (RE(e0, 0x9D) & 0x7FF) * ((RE(e0, 0xD2) & 3) == 3 ? 4 : 2) * ((RE(e0, 0xD5) >> 10 & 0x3FF) + 1);
        // images sharing memory: each surface would miss what is drawn on the other
        for (uint32_t j = 0; j < nif; j++) if (fb < iend[j] && ifb[j] < end) r = T_DEST;
        if (nif < 2) { ipsm[nif] = RE(e0, 0xD2) & 3; iend[nif] = end; ifb[nif++] = fb; } else r = T_DEST;
      }
      kd = k;
      if (rd_mask >> k & 1 && i < ord) ord = i;
    }
    if (!r && hasTex(e0, &f->d[i]) && !f->d[i].toff) {  // texture with no copy
      uint32_t ta = texAddr(e0), vr = (ta & 0x0F000000) == 0x04000000, old = 0;
      if (vr && (ta & 0x1FFFF0) == disp && (RE(e0, 0xC3) & 15) == (f->fbw >> 16 & 3) && kd >= 0) { if (i < ord) ord = i; }  // (cuts the surface's scene)
      else if (vr && ((ta & 0x1FFFF0) == t_prevd || (ta & 0x1FFFF0) == t_prev2) && (RE(e0, 0xC3) & 15) == (f->fbw >> 16 & 3)) {}  // (an image of a frame before)
      else {
        for (k = 0; k < nif && !(vr && (ta & 0x1FFFF0) == ifb[k]); k++) {}
        if (k == nif) for (int j = 0; j < 2; j++) old |= vr && rts[j].mem && rts[j].fb == (ta & 0x1FFFF0);
        // not read in another format: its bytes are not in the surface
        if (!old && (k == nif || (RE(e0, 0xC3) & 15) != ipsm[k])) r = T_TEX;
        else if (!old) { rd_mask |= 1u << k; if (kd >= 0 && i < ord) ord = i; }
      }
    }
    if (r) {
      DEV(failNote(f, i, r);)
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
  *disp_p = disp; *ord_p = ord;
  return T_OK;
}

#ifdef NP_DEV
// Census of a frame (extreme log level): looks at all its draws, not only up to the first problem. Every 10 s it
// counts: frames with each reason, what the textures with no copy are, and the distinct chains of images. A chain is the
// frame's draws grouped in runs: target, how many draws, whether they use depth and which image they read.
enum { TC_SHOWN, TC_FORMAT, TC_SELF, TC_INSIDE, TC_UNKNOWN, TC_N };
static const char *cnames_tex[TC_N] = { "shown_image", "other_format", "itself", "inside_an_image", "unknown" };
static uint32_t cs_fr, cs_r[TN], cs_tex[TC_N], cs_many, cs_wide, cs_sig[48], cs_cnt[48], cs_nsig;
static void trCensus(RFrame *f) {
  typedef struct { uint32_t fb, w, psm, end; } TG;
  typedef struct { uint32_t fb, w, psm, n, z, ta, fmt; } RN;
  static RN run[32];
  TG tg[8];
  uint32_t disp = f->fb & 0x1FFFF0, ntg = 0, nrun = 0, mask = 0, tmask = 0, n_disp = 0, wide = 0;
  for (uint32_t i = 0; i < f->nd; i++) n_disp += (RE(f->e[f->d[i].st], 0x9C) & 0x1FFFF0) == disp;
  if (!n_disp && f->nd) disp = RE(f->e[f->d[f->nd - 1].st], 0x9C) & 0x1FFFF0;
  for (uint32_t i = 0; i < f->nd; i++) {
    RDraw *d = &f->d[i];
    const uint32_t *e = f->e[d->st];
    uint32_t fb = RE(e, 0x9C) & 0x1FFFF0, w = RE(e, 0x9D) & 0x7FF, psm = RE(e, 0xD2) & 3, ta = 0, fmt = 0, k;
    int r = trCheck(f, d);
    if (r) mask |= 1u << r;
    wide |= w > 512;
    for (k = 0; k < ntg && tg[k].fb != fb; k++) {}
    if (k == ntg && ntg < 8) { tg[ntg].fb = fb; tg[ntg].w = w; tg[ntg].psm = psm; tg[ntg++].end = fb + w * (psm == 3 ? 4 : 2) * ((RE(e, 0xD5) >> 10 & 0x3FF) + 1); }
    if (hasTex(e, d) && !d->toff) {
      int c = TC_UNKNOWN;
      ta = texAddr(e); fmt = RE(e, 0xC3) & 15;
      uint32_t o = ta & 0x1FFFF0;
      for (uint32_t j = 0; j < ntg; j++)
        if (o >= tg[j].fb && o < tg[j].end) {
          c = o != tg[j].fb ? TC_INSIDE : fmt != tg[j].psm ? TC_FORMAT : o == disp ? TC_SHOWN : tg[j].fb == fb ? TC_SELF : TC_N;
          break;
        }
      if (c < TC_N) tmask |= 1u << c;
    }
    RN *q = nrun ? &run[nrun - 1] : 0;
    if (!q || q->fb != fb || q->ta != ta || q->fmt != fmt) {
      if (nrun == 32) continue;
      q = &run[nrun++]; q->fb = fb; q->w = w; q->psm = psm; q->n = 0; q->z = 0; q->ta = ta; q->fmt = fmt;
    }
    q->n++; q->z |= !(RE(e, 0xD3) & 1) && (RE(e, 0x23) & 1);
  }
  cs_fr++; cs_many += ntg > 3; cs_wide += wide;
  for (int i = 1; i < TN; i++) cs_r[i] += mask >> i & 1;
  for (int i = 0; i < TC_N; i++) cs_tex[i] += tmask >> i & 1;
  uint32_t sig = nrun * 16777619u;
  for (uint32_t i = 0; i < nrun; i++) sig = (sig ^ run[i].fb ^ run[i].ta << 5 ^ run[i].fmt << 26 ^ run[i].z << 31) * 16777619u;
  uint32_t k;
  for (k = 0; k < cs_nsig && cs_sig[k] != sig; k++) {}
  if (k < cs_nsig) { cs_cnt[k]++; return; }
  if (cs_nsig == 48) return;
  cs_sig[k] = sig; cs_cnt[k] = 1; cs_nsig++;
  char b[440]; int n = sceClibSnprintf(b, sizeof b, "%u", f->nd);
  for (uint32_t i = 0; i < nrun && n < (int)sizeof b - 40; i++) {
    n += sceClibSnprintf(b + n, sizeof b - n, " %06x/%u/%u:%u%s", run[i].fb, run[i].w, run[i].psm, run[i].n, run[i].z ? "z" : "");
    if (run[i].ta) n += sceClibSnprintf(b + n, sizeof b - n, "<%06x.%u", run[i].ta & 0xFFFFFF, run[i].fmt);
  }
  alog("census: chain %u at frame %u (shown %06x): %s\n", k, f->frame, disp, b);
}
// Every 10 s (presenter thread): what the census counted, then clears it
static void trCensusLog(void) {
  char b[400]; int n = 0;
  for (int i = 1; i < TN; i++) if (cs_r[i]) n += sceClibSnprintf(b + n, sizeof b - n, " %s=%u", tnames[i], cs_r[i]);
  n += sceClibSnprintf(b + n, sizeof b - n, " | no copy:");
  for (int i = 0; i < TC_N; i++) if (cs_tex[i]) n += sceClibSnprintf(b + n, sizeof b - n, " %s=%u", cnames_tex[i], cs_tex[i]);
  alog("census: frames %u, over 3 targets %u, target over 512 wide %u | frames with each reason:%s\n", cs_fr, cs_many, cs_wide, b);
  n = 0;
  for (uint32_t i = 0; i < cs_nsig && n < 380; i++) if (cs_cnt[i]) { n += sceClibSnprintf(b + n, sizeof b - n, " %u=%u", i, cs_cnt[i]); cs_cnt[i] = 0; }
  if (n) alog("census: chains seen:%s\n", b);
  cs_fr = cs_many = cs_wide = 0;
  sceClibMemset(cs_r, 0, sizeof cs_r); sceClibMemset(cs_tex, 0, sizeof cs_tex);
}
#endif

// Leaves in each intermediate image of frame f the region its draws touch, so its scenes do not load and store the whole
// surface: the corners of rectangles with 16-bit positions, the whole image for the rest. An image of half the screen or less made
// only of those rectangles, reading no texture of the record, only shrinks and blurs what is on screen: it stays at PSP size
static void trRegions(RFrame *f, uint32_t disp) {
  uint32_t bx[2] = { 0, 0 }, by[2] = { 0, 0 }, soft[2] = { 1, 1 };
  for (uint32_t i = 0; i < f->nd; i++) {
    RDraw *d = &f->d[i];
    const uint32_t *e = f->e[d->st];
    uint32_t fb = RE(e, 0x9C) & 0x1FFFF0, vt = d->vt, x = 2 * (RE(e, 0x9D) & 0x7FF), y = 2 * ((RE(e, 0xD5) >> 10 & 0x3FF) + 1), tc = vt & 3, k;
    if (fb == disp || t_mark[i >> 5] >> (i & 31) & 1) continue;
    RT *r = rtOf(fb, f->frame, 1);
    if (!r) continue;
    k = r - rts;
    if (d->prim == 6 && !d->ioff && vt >> 23 & 1 && (vt >> 7 & 3) == 2 && !(vt >> 2 & 7) && !(vt >> 5 & 3) && (tc == 0 || tc == 2) && d->n == 2) {
      const int16_t *p = (const int16_t *)(f->a + d->voff - 1) + (tc ? 2 : 0);
      uint32_t st = tc ? 5 : 3, w = p[0] > p[st] ? p[0] : p[st], h = p[1] > p[st + 1] ? p[1] : p[st + 1];
      if (2 * w + 2 < x) x = 2 * w + 2;
      if (2 * h + 2 < y) y = 2 * h + 2;
      if (d->toff) soft[k] = 0;
    } else soft[k] = 0;
    if (x > bx[k]) bx[k] = x;
    if (y > by[k]) by[k] = y;
  }
  for (int i = 0; i < 2; i++) {
    rts[i].sh = soft[i] && bx[i] && bx[i] <= LINE / 2 && by[i] <= H / 2 + 8 DEV(&& !keep_hd);
    rts[i].vr.xMax = ((bx[i] > LINE ? LINE : bx[i] ? bx[i] : LINE) >> rts[i].sh) - 1; rts[i].vr.yMax = ((by[i] > H ? H : by[i] ? by[i] : H) >> rts[i].sh) - 1;
    for (int k = 0; k < 4; k++) rts[i].have[k] = rts[i].dirty[k] = k < 2 ? 1e9f : -1e9f;
  }
}

// Leaves in b the box (x0, y0, x1, y1) of the rectangles of clear d, which has only 16-bit positions. 0 if d is another kind of draw
static int trClearBox(const RFrame *f, const RDraw *d, int *b) {
  if (d->vt != 0x800100 || d->prim != 6 || d->ioff || !(RE(f->e[d->st], 0xD3) & 1)) return 0;
  const int16_t *p = (const int16_t *)(f->a + d->voff - 1);  // (x, y, z)
  b[0] = b[1] = 1 << 14; b[2] = b[3] = 0;
  for (uint32_t k = 0; k < d->n; k++, p += 3) {
    b[0] = p[0] < b[0] ? p[0] : b[0]; b[1] = p[1] < b[1] ? p[1] : b[1]; b[2] = p[0] > b[2] ? p[0] : b[2]; b[3] = p[1] > b[3] ? p[1] : b[3];
  }
  return 1;
}
// Looks in the first ord draws of frame f (they can be reordered) for a boxed stretch: on the shown image, behind one partial
// scissor, a clear of color and depth that covers the box and at least one 3D draw after it. Nothing in it needs what the
// surface holds, so it can be drawn before the surface. Only the first one in a frame.
static void trBoxed(RFrame *f, uint32_t disp, uint32_t ord) {
  t_b0 = t_b1 = 0;
  if (!rtb.mem) return;
  for (uint32_t i = 0; i < ord; i++) {
    const RDraw *d = &f->d[i];
    int b[4];
    if (!trClearBox(f, d, b)) continue;
    const uint32_t *e = f->e[d->st];
    uint32_t d4 = RE(e, 0xD4) & 0xFFFFF, d5 = RE(e, 0xD5) & 0xFFFFF, x0 = d4 & 0x3FF, y0 = d4 >> 10, x1 = d5 & 0x3FF, y1 = d5 >> 10, j = i + 1, n3 = 0;
    if ((RE(e, 0xD3) & 0x501) != 0x501 || (RE(e, 0x9C) & 0x1FFFF0) != disp || (!d4 && x1 >= 479 && y1 >= 271)) continue;
    if (b[0] > (int)x0 || b[1] > (int)y0 || b[2] <= (int)x1 || b[3] <= (int)y1) continue;
    for (; j < ord; j++) {
      const uint32_t *q = f->e[f->d[j].st];
      if ((RE(q, 0xD4) & 0xFFFFF) != d4 || (RE(q, 0xD5) & 0xFFFFF) != d5 || (RE(q, 0x9C) & 0x1FFFF0) != disp) break;
      n3 += !(f->d[j].vt >> 23 & 1);
    }
    if (!n3) continue;
    t_b0 = i; t_b1 = j; t_bx = 2.0f * x0; t_by = 2.0f * y0;
    rtb.vr.xMax = 2 * (x1 + 1 - x0) - 1; rtb.vr.yMax = 2 * (y1 + 1 - y0) - 1;
    return;
  }
}

// Draws frame f of the record to surface s (cs[s]), in the Adrenaline thread. base: the normal image, which goes
// underneath stretched (what the game does not redraw stays as on the PSP); 0: black background. First checks the frame
// (trCheckFrame): if a draw fails, returns the reason without drawing. Then opens
// one scene per destination change (surface s or an intermediate image; if possible, all intermediates first and the
// surface last, in a single scene: each scene reads and writes its whole surface), sets up texture, palette (to 8888 in
// tpal), vertices (trVerts, in tv) and uniforms of each draw, and draws it. T_OK if ready; if it fails midway
// returns the reason and the surface is not used.
// The fields that make two draws share their state
static inline int gSame(const RDraw *a, const RDraw *b) {
  return a->st == b->st && a->vt == b->vt && a->prim == b->prim && a->toff == b->toff && a->clut == b->clut && a->clen == b->clen &&
         a->woff == b->woff && a->moff == b->moff && a->loff == b->loff;
}
// Index of the vertex at offset vo from base gb, by vertex size vz (16, 20 or 24): 0 if it is not a whole number of vertices.
// With no division: a multiplication by the inverse of 3 or 5 leaves a small number only if the division is exact
static inline int gOff(uint32_t vo, uint32_t gb, uint32_t vz, uint32_t *ix) {
  uint32_t b = vo - gb, x;
  if (vo < gb) return 0;
  if (vz == 16) { *ix = b >> 4; return !(b & 15); }
  if (vz == 24) { x = (b >> 3) * 0xAAAAAAABu; *ix = x; return !(b & 7) && x <= 0x55555555u; }
  x = (b >> 2) * 0xCCCCCCCDu; *ix = x; return !(b & 3) && x <= 0x33333333u;
}
// Writes the triangles of a draw of nt triangles starting at vertex ix as indices; returns how many
static inline uint32_t gAdd(uint16_t *o, uint32_t ix, uint32_t prim, uint32_t nt) {
  uint16_t *p = o;
  if (prim == 3) for (uint32_t k = 0; k < nt * 3; k++) *p++ = ix + k;
  else if (prim == 5) for (uint32_t k = 0; k < nt; k++) { p[0] = ix; p[1] = ix + k + 1; p[2] = ix + k + 2; p += 3; }
  else for (uint32_t k = 0; k < nt; k++) { p[0] = ix + k; p[1] = ix + k + 1 + (k & 1); p[2] = ix + k + 2 - (k & 1); p += 3; }
  return p - o;
}
static int trFrame(SceGxmContext *c, RFrame *f, int s, const SceGxmTexture *base) {
  t3.ke = 0;
  DEV(uint32_t t_ss = 0; c_fr++;)
  DEV(static uint32_t fxn; int sfx = fx_log && !(++fxn & 31); b_fr++; b_fxn += sfx;)
  DEV(uint32_t ta = now(), tb, ut[6]; sceClibMemcpy(ut, (void *)t_us, sizeof ut);
      static uint32_t hdn; int smp = !(++hdn & 3), pb = BP_COL, why = 0; float bpx[2][BP_N] = { { 0 } }, bwy[BW_N] = { 0 };)
  uint32_t disp, ord;
  int r = trCheckFrame(f, &disp, &ord);
  if (r) return r;
  DEV(t_us[0] += now() - ta; if (p_on) pAdd(P_T_CHECK, now() - ta);)
  trRegions(f, disp);
  trBoxed(f, disp, ord);
  // Depth lives in one scene: if the surface is drawn with depth after its scene was cut, it is stored and loaded. The other
  // scenes use the same memory without storing it, so they do not touch it
  SceGxmDepthStencilSurface dss = ds;
  int zcut = 0;
  for (uint32_t i = ord; i < f->nd && !zcut; i++) {
    const uint32_t *e = f->e[f->d[i].st];
    zcut = (RE(e, 0x9C) & 0x1FFFF0) == disp && !(RE(e, 0xD3) & 1) && RE(e, 0x23) & 1;
  }
  if (zcut) sceGxmDepthStencilSurfaceSetForceStoreMode(&dss, SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED);
  static uint32_t t_q[2];  // last scene that read the vertices and palettes of each surface
  // if the GPU is still reading them (unfinished scene) it waits
  if (gBusy(t_q[s])) { g_psurf++; gEv("vertices and palettes rewritten", t_q[s]); gWait(t_q[s]); }
  TV *o = tv[s];
  uint32_t *pal = tpal[s], np = 0, nv = 0, pal_prev = ~0u, pn = 0;  // np: words used; pal_prev/pn: the previous palette
  void *u;
  // CTAG: tag of the scene being closed, for the GPU timer
#define CTAG() (c_tag = cur_rt == &rtb ? CK_BOX : cur_rt ? cur_rt->fb | CK_IMG : t_ss++ ? CK_SURF2 : CK_SURF)
  // SCENE: starts a scene on surface surf_tex with the vertex program and the matrix
#define SCENE(surf_tex, reg) do { DEV(h_at = 5;) sceGxmBeginScene(c, 0, cur_rt ? rt_i : rt, reg, 0, t_sync, surf_tex, cur_rt ? &ds : &dss); DEV(h_at = 4;) t_zs = t_st = t_sr = t_cl = ~0u; zsSet(c, 0, 0); sceGxmSetVertexProgram(c, nvp); gv_cur = 0; \
    trWvp(wm, reg); if (cur_rt == &rtb) { wm[12] -= t_bx * wm[0]; wm[13] -= t_by * wm[5]; } if (cur_rt && cur_rt->sh) { wm[0] /= 2; wm[5] /= 2; } sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, n_wvp, 0, 16, wm); } while (0)
  // OPEN: closes the open scene (if any) and opens one on dst (0: surface s); the first time on the surface,
  // puts the normal image (or black: trBase) underneath
#define OPEN(dst_) do { RUN(); pd = 0; if (opened) { SURF_WAIT(); DEV(h_at = 6;) sceGxmEndScene(c, 0, gEnd()); DEV(h_at = 4; CTAG();) cNote(cur_rt ? C_INT : C_TR, &gq_n, t_draws - d0, spx / 1000, ssw); } \
    d0 = t_draws; spx = 0; ssw = 0; cur_rt = (dst_); opened = 1; \
    if (!cur_rt && has_base && zcut) sceGxmDepthStencilSurfaceSetForceLoadMode(&dss, SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_ENABLED); \
    SCENE(cur_rt ? &cur_rt->cs : &cs[s], cur_rt ? &cur_rt->vr : 0); \
    if (!cur_rt && !has_base) { has_base = 1; nv = trBase(c, base, o, nv); spx += W * H; DEV(bpx[0][BP_BASE] += W * H;) } } while (0)
  // SURF_WAIT: the surface s is being sampled for the screen until the previous frame's scenes end, and its scene must not start before
#define SURF_WAIT() do { if (!cur_rt) gWait(fq[0]); } while (0)
  // GRUN: sends the pending draw of the GPU path, triangles of indices gs to gn on the vertices of the record at gb
#define GRUN() do { if (gn > gs) { sceGxmSetVertexStream(c, 0, f->tx + gb); \
    sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, gi + gs, gn - gs); gs = gn; } } while (0)
  // RUN: sends the pending draw, the n vertices at rv in primitive rp; draws with the state already set wait in it to be joined
#define RUN() do { GRUN(); if (rn) { sceGxmSetVertexStream(c, 0, o + rv); \
    sceGxmDraw(c, rp == 3 ? SCE_GXM_PRIMITIVE_TRIANGLES : rp == 5 ? SCE_GXM_PRIMITIVE_TRIANGLE_FAN : SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, tib, rn); rn = 0; } } while (0)
  uint32_t rn = 0, rv = 0, rp = 0, key = 0, gn = 0, gs = 0, gb = 0, gz = 0, ix = 0;
  const RDraw *gd = 0;  // last draw joined by indices
  uint16_t *gi = tgi[s];
  float wm[16];    // the scene's matrix
  int gv_cur = 0;  // the program in the context is gvp
  const RDraw *pd = 0;  // last draw whose state is set in the context, which the next one may share (t, tw, th included)
  SceGxmTexture t;
  float tw = 1, th = 1;
  TV tl;  // last vertex of the pending draw
  RT *cur_rt = 0;  // destination of the open scene (0: the shown surface)
  int opened = 0, has_base = 0;
  uint32_t d0 = t_draws, ssw = 0;  // timer: draws at scene start, swizzled draws and scene pixels
  float spx = 0;
  if (!ord) OPEN(0);  // (all in game order: the surface first, as on the PSP)
  // GE texture format (0xC3) -> GXM
  static const SceGxmTextureFormat gf[6] = { SCE_GXM_TEXTURE_FORMAT_U5U6U5_BGR, SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR,
    SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ABGR, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, SCE_GXM_TEXTURE_FORMAT_P4_ABGR,
    SCE_GXM_TEXTURE_FORMAT_P8_ABGR };
  int res = T_OK;
  // VT(k): adds the time since the last mark to part k of the vertex split
#define VT(k) do { if (vm) { uint32_t x_ = now(); vu[k] += x_ - vc; vc = x_; } } while (0)
  DEV(static uint32_t vsn; uint32_t vc = 0, vu[VS_N] = { 0 }, tkb = 0, vr0 = 0;
      int vm = v_on && !(++vsn & 7) ? 1 + (vsn >> 3 & 1) : 0;
      if (vm) vr0 = thrRun();)
  static struct { uint32_t clut, c5, ne, off; } pc[128];  // palettes translated in this frame, by loaded palette and 0xC5
  sceClibMemset(pc, 0, sizeof pc);
  float sc[4];  // scissor of the state set (a draw sharing it keeps it, like mat)
  uint32_t mat = 0;
  // three passes: of the first ord draws, 0 those to intermediate images and 1 those to the surface; 2 the rest, in order
  for (uint32_t pass = ord ? 0 : 2; pass < 3 && res == T_OK; pass++)
  for (uint32_t i = pass < 2 ? 0 : ord, ne = pass < 2 ? ord : f->nd; i < ne && res == T_OK; i++) {
    DEV(h_at = 4; h_i = i;)
    RDraw *d = &f->d[i];
    if (t_mark[i >> 5] >> (i & 31) & 1) continue;
    // Short path: same state as the draw before, already joined by indices: only its indices are added
    if (gd && d == gd + 1 && pd == gd && i != ord && gSame(gd, d) && gn > gs DEV(&& !sfx)) {
      uint32_t n = d->n, nt = gd->prim == 3 ? n / 3 : n - 2, vo = d->voff & ~VX, ix;
      if (d->voff & VX && n >= 3 && gn + nt * 3 <= TG_N && gOff(vo, gb, gz, &ix) && ix + n <= 0xFFFF) {
        gn += gAdd(gi + gn, ix, d->prim, nt);
        pd = gd = d; t_draws++;
        DEV(t_c[TC_VIN] += n; t_c[TC_VOUT] += n; t_c[TC_VGPU] += n; t_c[TC_D3]++; t_c[TC_LIT] += t3.lit ? n : 0; t_c[TC_SAME]++;)
        continue;
      }
    }
    gd = 0;
    const uint32_t *e = f->e[d->st];
    uint32_t clr = RE(e, 0xD3), tex = hasTex(e, d), fb = RE(e, 0x9C) & 0x1FFFF0, w3d = !(d->vt >> 23 & 1);
    if (fb != disp && trSkip(f, d)) continue;
    RT *dst = fb == disp ? (i >= t_b0 && i < t_b1 ? &rtb : 0) : rtOf(fb, f->frame, 1);
    if (fb != disp && !dst) { res = T_DEST; break; }
    if (pass == 1 && i == t_b0 && t_b1) {  // the box goes on the surface where the game drew it
      if (nv + 4 > TV_N) { res = T_LIMIT; break; }
      if (!opened || cur_rt) OPEN(0); else RUN();
      SceGxmTexture ts;
      float *q = (float *)(o + nv), x1 = t_bx + rtb.vr.xMax + 1, y1 = t_by + rtb.vr.yMax + 1, u1 = (rtb.vr.xMax + 1.0f) / LINE, v1 = (rtb.vr.yMax + 1.0f) / H;
      const float qv[20] = { t_bx, t_by, .5f, 0, 0, x1, t_by, .5f, u1, 0, t_bx, y1, .5f, 0, v1, x1, y1, .5f, u1, v1 };
      sceClibMemcpy(q, qv, sizeof qv); nv += 4;
      sceGxmTextureInitLinear(&ts, rtb.mem, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, LINE, H, 0);
      zsSet(c, 0, 0); sceGxmSetCullMode(c, SCE_GXM_CULL_NONE); t_cl = SCE_GXM_CULL_NONE;
      sceGxmSetVertexProgram(c, vp); sceGxmSetFragmentProgram(c, fp);
      sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, p_wvp, 0, 16, wm);
      sceGxmSetFragmentTexture(c, 0, &ts); sceGxmSetVertexStream(c, 0, q);
      sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
      sceGxmSetVertexProgram(c, nvp); gv_cur = 0; pd = 0;
      sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, n_wvp, 0, 16, wm);
    }
    if (i < ord ? pass != (dst ? 0u : 1u) : pass != 2) continue;
    DEV(tb = now();)
    if (!opened || dst != cur_rt) OPEN(dst);
    DEV(t_us[4] += now() - tb; tb = now();)
    if ((clr & 1) && !(clr & 0x700)) continue;  // clear of nothing
    int self = 0;  // (the draw reads the image it draws on)
    uint32_t extra = 0;
    int same = pd && !(clr & 1) && pd->st == d->st && pd->vt == d->vt && pd->prim == d->prim && pd->toff == d->toff && pd->clut == d->clut &&
               pd->clen == d->clen && pd->woff == d->woff && pd->moff == d->moff && pd->loff == d->loff;
    if (!same) key = clr & 1 ? 0 : blendKey(e);
    if (tex && !same) {
      uint32_t fmt = RE(e, 0xC3) & 15, a8 = RE(e, 0xA8), b8 = RE(e, 0xB8), tbw = a8 & 0x7FF;
      th = 1u << (b8 >> 8 & 15); tw = tbw;
      uint32_t tv = texAddr(e) & 0x0F1FFFF0;
      int shown = !d->toff && tv == (0x04000000 | disp), prev = !d->toff && (tv == (0x04000000 | t_prevd) || tv == (0x04000000 | t_prev2)) && !rtKept(tv);
      RT *src = (d->toff && !(key && grayFix(e))) || shown || prev ? 0 : rtOf(texAddr(e) & 0x1FFFF0, f->frame, 0);
      if (src || shown || prev) {  // intermediate or shown image: its double-size surface (512 x 272 of the PSP -> 1024 x 544)
        self = src && src == dst;
        if (sceGxmTextureInitLinear(&t, shown ? surf[s] : prev ? surf[s ^ 1] : src->mem, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, LINE, H, 0) < 0) { res = T_TEX; break; }
        tw = src && src->sh ? LINE : LINE / 2; th = src && src->sh ? H : H / 2; fmt = 3;
      } else if (!d->toff || sceGxmTextureInitLinear(&t, f->tx + d->toff - 1, gf[fmt], tbw, (uint32_t)th, 0) < 0) { res = T_TEX; break; }
      if (fmt >= 4) {  // palette: to 8888, with shift, mask and start (0xC5)
        uint32_t c5 = RE(e, 0xC5), cf = c5 & 3, sh = c5 >> 2 & 31, mk = c5 >> 8 & 0xFF, st = (c5 >> 16 & 31) << 4;
        uint32_t ne = fmt == 4 ? 16 : 256, bpe = cf == 3 ? 4 : 2, max = d->clen / bpe, *pp;
        typeof(&pc[0]) q = &pc[(d->clut >> 4 ^ c5 ^ ne) & 127];
        if (q->clut == d->clut && q->c5 == c5 && q->ne == ne) pp = pal + q->off;  // already translated in this frame
        else {
          static uint32_t pbs[2][256];  // (built in cached memory: the GPU's is slow to read; the last one is kept to compare)
          static uint32_t pi;
          uint32_t *pb = pbs[pi ^= 1];
          if ((np + ne) * 4 > TP_B) { res = T_LIMIT; break; }
          const uint8_t *src = f->a + d->clut - 1;
          for (uint32_t k = 0; k < ne; k++) {
            uint32_t j = ((k >> sh) & mk) | st;
            pb[k] = pal8888(j < max ? (bpe == 4 ? ((const uint32_t *)src)[j] : ((const uint16_t *)src)[j]) : 0, cf);
          }
          if (pn != ne || sceClibMemcmp(pbs[pi ^ 1], pb, ne * 4)) {  // (same as the last: reused)
            sceClibMemcpy(pal + np, pb, ne * 4);
            pal_prev = np; pn = ne; np += ne;
          }
          pp = pal + pal_prev;
          q->clut = d->clut; q->c5 = c5; q->ne = ne; q->off = pal_prev;
        }
        sceGxmTextureSetPalette(&t, pp);
      }
      uint32_t tfl = RE(e, 0xC6), twr = RE(e, 0xC7);
      // GE filter and wrap (0xC6, 0xC7)
      sceGxmTextureSetMinFilter(&t, tfl & 1 ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT);
      sceGxmTextureSetMagFilter(&t, tfl >> 8 & 1 ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT);
      sceGxmTextureSetUAddrMode(&t, twr & 1 ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
      sceGxmTextureSetVAddrMode(&t, twr >> 8 & 1 ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
    }
    DEV(t_us[1] += now() - tb; tb = now(); vc = tb;)
    uint32_t n = 0;
    if (!same) {
      uint32_t d4 = RE(e, 0xD4), d5 = RE(e, 0xD5);
      mat = (RE(e, 0x55) & 0xFFFFFF) | (RE(e, 0x58) & 0xFF) << 24;
      sc[0] = 2.0f * (d4 & 0x3FF); sc[1] = 2.0f * (d4 >> 10 & 0x3FF); sc[2] = 2.0f * ((d5 & 0x3FF) + 1); sc[3] = 2.0f * ((d5 >> 10 & 0x3FF) + 1);
      if (!(d5 & 0xFFFFF)) sc[0] = sc[1] = -1e9f, sc[2] = sc[3] = 1e9f;  // (no saved scissor: not clipped)
      uint32_t rg = RE(e, 0x16);  // the region's end (never set: 0) is one more scissor edge
      if (rg & 0xFFFFF) {
        float rx = 2.0f * ((rg & 0x3FF) + 1), ry = 2.0f * ((rg >> 10 & 0x3FF) + 1);
        if (rx < sc[2]) sc[2] = rx;
        if (ry < sc[3]) sc[3] = ry;
      }
      if (w3d && dst == &rtb) sc[0] = sc[1] = -1e9f, sc[2] = sc[3] = 1e9f;  // (the edge of the image is the scissor)
      if (w3d) t3Set(f, d, e, &t3);
    }
    // Vertices the GPU reads as they are: no translated vertices, and the state below uses none of them
    int cov = sc[0] <= 0 && sc[1] <= 0 && sc[2] >= (dst ? dst->vr.xMax + 1 : W) && sc[3] >= (dst ? dst->vr.yMax + 1 : H);  // (nothing for the scissor to clip)
    int gl = GVL(d->vt), gnr = gl && !t3.lit && t3.gen == 1 && t3.prj == 3, lok = gl ? gnr || (t3.lit && t3One(&t3)) : !(t3.lit && t3.nl);  // (lok: lights and UV are what its program does)
    int gv = d->voff & VX && gvp[gl] && w3d && !self && cov && (!t3.gen || gnr) && lok && !grayFix(e) ? gl + 1 : 0;  // (gv: its program + 1)
    uint32_t prim = d->prim;
    TV *w = ts;
    float ar = 0;
    int keep = 1;  // (0: the next draw cannot share t)
    DEV(if (w3d && !gv && !(clr & 1)) trWhy(d, e, !cov, dst != 0, !lok, t3.gen);)
    if (gv) {
      n = d->n;
      if (tex && d->toff && (1u << (RE(e, 0xB8) & 15)) < tw) {  // (the vertices are not seen here: always narrowed; the UV follows tw)
        uint32_t k = trNarrow(&t, f->tx + d->toff - 1, RE(e, 0xC3) & 15, tw, 1u << (RE(e, 0xB8) & 15), th, 0, 0, pal + np, TP_B / 4 - np);
        if (k == ~0u) { res = T_LIMIT; break; }
        np += k; tw = 1u << (RE(e, 0xB8) & 15);
      }
      if (dst) { dst->dirty[0] = dst->dirty[1] = 0; dst->dirty[2] = LINE; dst->dirty[3] = H; }
      DEV(t_c[TC_VIN] += n; t_c[TC_VOUT] += n; t_c[TC_VGPU] += n; t_c[TC_D3]++; t_c[TC_LIT] += t3.lit ? n : 0; t_c[TC_SAME] += same;)
      goto state;
    }
    // (each vertex can become at most 22 when clipped; a draw that big goes straight to tv)
    w = d->n * 22 + 8 <= sizeof ts / sizeof *ts ? ts : o + nv;
    uint32_t room = w == ts ? sizeof ts / sizeof *ts : TV_N - nv;
    DEV(VT(VS_STATE); if (vm == 2) { tkb += trTouch(f, d); VT(VS_TOUCH); } v_fast = 0;)
    if ((res = trVerts(f, d, w, tw, th, mat, sc, &n, w3d ? &t3 : 0))) {
      DEV(failNote(f, i, res);)
      break;
    }
    DEV(if (vm) { VT(w != ts ? VS_BIG : v_fast ? VS_FAST : VS_SLOW); vu[w != ts ? VS_NB : v_fast ? VS_NF : VS_NS] += d->n; vu[VS_DRAWS]++;
          if (w == ts) { uint32_t z; trVerts(f, d, w, tw, th, mat, sc, &z, w3d ? &t3 : 0); VT(v_fast ? VS_AGAIN_F : VS_AGAIN_S); } })
    DEV(if (w3d && t3.lit) for (int i = 0; i < 4; i++) if (t3.l[i].on) { t_c[TC_L0] += d->n; t_c[TC_LPT] += t3.l[i].type ? d->n : 0; t_c[TC_LSP] += t3.l[i].comp == 1 ? d->n : 0; }
        t_c[TC_ENV] += w3d && t3.gen == 2 ? d->n : 0;
        t_c[TC_VIN] += d->n; t_c[TC_D3] += w3d; t_c[TC_LIT] += w3d && t3.lit ? d->n : 0;
        t_c[TC_SCIS] += w3d && (sc[0] > 0 || sc[1] > 0 || sc[2] < W || sc[3] < H); t_c[TC_FLAT] += !(clr & 1) && !(RE(e, 0x50) & 1);
        uint32_t nb = n;)
    if (nv + n > TV_N) { res = T_LIMIT; break; }
    if (dst && !w3d && !(clr & 1)) for (uint32_t j = 0; j < n; j++) if (w[j].y > H) res = T_DEST;  // (past the surface's rows)
    DEV(if (res) failNote(f, i, res);)
    if (res) break;
    if (prim < 3 && (res = trLines(w, &n, room, prim == 2))) break;
    if (prim < 3) prim = 3;
    DEV(VT(VS_REST);)
    if (prim != 6 && (res = trTris(w, &n, room, &prim, sc, (clr & 1) || !(RE(e, 0x50) & 1), w3d))) break;
    DEV(VT(VS_TRIS);)
    DEV(t_c[TC_VOUT] += n; t_c[TC_CUT] += n != nb; t_c[TC_SAME] += same;)
    if (!n) { pd = 0; continue; }  // (t, tw and th are this draw's now, not those of the state in the context)
    if (tex && d->toff && (1u << (RE(e, 0xB8) & 15)) < tw) {
      uint32_t k = trNarrow(&t, f->tx + d->toff - 1, RE(e, 0xC3) & 15, tw, 1u << (RE(e, 0xB8) & 15), th, w, n, pal + np, TP_B / 4 - np);
      if (k == ~0u) { res = T_LIMIT; break; }
      np += k; keep = !k; same = same && !k;  // (narrowed: the texture in the context is no longer this one)
    }
    uint32_t gy = clr & 1 ? 0 : grayFix(e);
    if (gy) for (uint32_t j = 0; j < n; j++) {
      uint32_t k = w[j].c;
      w[j].c = gy << 24 | ((k >> 16 & 255) * gy / 255) << 16 | ((k >> 8 & 255) * gy / 255) << 8 | (k & 255) * gy / 255;
    }
    if (self) {  // read from rtc: copied there, in a scene of its own, unless it already holds this part as it is now
      float b[4] = { 1e9f, 1e9f, -1e9f, -1e9f }, *hv = dst->have, *dy = dst->dirty;
      for (uint32_t j = 0; j < n; j++) {
        float x = w[j].u * LINE, y = w[j].v * H;
        b[0] = x < b[0] ? x : b[0]; b[1] = y < b[1] ? y : b[1]; b[2] = x > b[2] ? x : b[2]; b[3] = y > b[3] ? y : b[3];
      }
      b[0] = b[0] < 2 ? 0 : (int)b[0] - 2; b[1] = b[1] < 2 ? 0 : (int)b[1] - 2;
      b[2] = b[2] + 3 > LINE ? LINE : (int)b[2] + 3; b[3] = b[3] + 3 > H ? H : (int)b[3] + 3;
      if (!(b[0] >= hv[0] && b[1] >= hv[1] && b[2] <= hv[2] && b[3] <= hv[3]) || !(b[0] >= dy[2] || b[2] <= dy[0] || b[1] >= dy[3] || b[3] <= dy[1])) {
        if (nv + n + 4 > TV_N) { res = T_LIMIT; break; }
        SceGxmTexture ts;
        SceGxmValidRegion vr = { (uint32_t)b[2] - 1, (uint32_t)b[3] - 1 };
        float *q = (float *)(o + nv + n), u0 = b[0] / LINE, v0 = b[1] / H, u1 = b[2] / LINE, v1 = b[3] / H;
        const float qv[20] = { b[0], b[1], .5f, u0, v0, b[2], b[1], .5f, u1, v0, b[0], b[3], .5f, u0, v1, b[2], b[3], .5f, u1, v1 };
        sceClibMemcpy(q, qv, sizeof qv); extra = 4;
        RUN();
        sceGxmTextureInitLinear(&ts, dst->mem, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, LINE, H, 0);
        sceGxmEndScene(c, 0, gEnd()); DEV(CTAG();) cNote(C_INT, &gq_n, t_draws - d0, spx / 1000, ssw); opened = 0;
        sceGxmBeginScene(c, 0, rt_c, &vr, 0, t_sync, &rtc.cs, &ds);
        t_zs = t_st = t_sr = t_cl = ~0u; zsSet(c, 0, 0); sceGxmSetCullMode(c, SCE_GXM_CULL_NONE);
        sceGxmSetVertexProgram(c, vp); sceGxmSetFragmentProgram(c, fp);
        float wm[16]; trWvp(wm, &vr);
        sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, p_wvp, 0, 16, wm);
        sceGxmSetFragmentTexture(c, 0, &ts); sceGxmSetVertexStream(c, 0, q);
        sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
        sceGxmEndScene(c, 0, gEnd()); DEV(c_tag = dst->fb | CK_COPY;) cNote(C_INT, &gq_n, 1, 0, 0);
        OPEN(dst);
        sceClibMemcpy(hv, b, sizeof b); dy[0] = dy[1] = 1e9f; dy[2] = dy[3] = -1e9f;
      }
      sceGxmTextureSetData(&t, rtc.mem);
      keep = 0;
    }
    if (dst) {  // what this draw changes of the image
      float *dy = dst->dirty;
      if (w3d) { dy[0] = dy[1] = 0; dy[2] = LINE; dy[3] = H; }
      else for (uint32_t j = 0; j < n; j++) {
        const TV *q = &w[j];
        float qx = dst->sh ? q->x / 2 : q->x, qy = dst->sh ? q->y / 2 : q->y;
        dy[0] = qx < dy[0] ? qx : dy[0]; dy[2] = qx > dy[2] ? qx : dy[2];
        dy[1] = qy < dy[1] ? qy : dy[1]; dy[3] = qy > dy[3] ? qy : dy[3];
      }
    }
    DEV(VT(VS_REST); t_us[2] += now() - tb; tb = now();)
    ar = w3d ? 0 : trArea(w, n, prim);
    if (c_on) { spx += ar; ssw += tex && d->toff && (RE(e, 0xC2) & 1); }
    static uint32_t giant;  // giant draws (over 2 screens at double size): the first 40, in detail
    if (ar > 2.0f * LINE * H && giant < 40) {
      float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
      for (uint32_t j = 0; j < n; j++) {
        const TV *q = &w[j];
        x0 = q->x < x0 ? q->x : x0; x1 = q->x > x1 ? q->x : x1; y0 = q->y < y0 ? q->y : y0; y1 = q->y > y1 ? q->y : y1;
        u0 = q->u < u0 ? q->u : u0; u1 = q->u > u1 ? q->u : u1; v0 = q->v < v0 ? q->v : v0; v1 = q->v > v1 ? q->v : v1;
      }
      giant++;
      alog("translator: giant draw (%u): frame %u draw %u of %u prim %u vertices %u->%u area %u thousand x %d..%d y %d..%d | tex %u %ux%u fmt %u "
           "swz %u copy %u uv %d..%d %d..%d (thousandths) | target %s\n", giant, f->frame, i, f->nd, d->prim, d->n, n, (uint32_t)(ar / 1000),
           (int)x0, (int)x1, (int)y0, (int)y1, tex, (uint32_t)tw, (uint32_t)th, RE(e, 0xC3) & 15, RE(e, 0xC2) & 1, d->toff ? 1 : 0,
           (int)(u0 * 1000), (int)(u1 * 1000), (int)(v0 * 1000), (int)(v1 * 1000), dst ? "intermediate" : "surface");
      DEV(alog("translator: giant draw (%u) vt %06x\n", giant, d->vt); trRaw("translator: giant draw bytes", RV(f, d), d->vlen);)
    }
  state:;
    int nu = !same;  // (gv: its uniforms must be set)
    if (gv != gv_cur) {
      RUN();
      sceGxmSetVertexProgram(c, gv ? gvp[gv - 1] : nvp);
      gv_cur = gv; nu = 1;
      if (!gv) { sceGxmReserveVertexDefaultUniformBuffer(c, &u); sceGxmSetUniformDataF(u, n_wvp, 0, 16, wm); }
    }
    if (same) goto ready;  // (the state is already set)
    RUN();
    uint32_t afn = 1, aref = 0, c9 = RE(e, 0xC9), b2 = !(clr & 1) && blend2x(e);
    if (!(clr & 1) && (RE(e, 0x22) & 1)) alphaFn(e, &afn, &aref);
    int fast = tex && !(c9 & 0x10007) && afn == 1 && !b2 && !(w3d && t3.fog);
    int mid = tex && !fast && !(c9 & 7) && afn == 1 && !b2;
    int mina = tex && !(c9 & 7) && (afn == 6 || afn == 7) && !b2;  // (afn 6 greater, 7 greater or equal: the rounded alpha passes from aref +- half a step up)
    DEV(why = !tex || fast || mid || mina ? 0 : !!(c9 & 7) << BW_FUNC | !!(c9 & 0x10000) << BW_DOUBLE | (afn != 1) << BW_ATEST | (w3d && t3.fog) << BW_FOG | b2 << BW_BLEND2X;
        pb = fast ? BP_FAST : !tex ? BP_COL : mid || mina ? BP_MID : BP_TEX;
        t_c[fast ? TC_VFAST : tex ? TC_VTEX : TC_VCOL] += n; t_c[TC_FOG] += w3d && t3.fog; t_c[TC_ATEST] += afn != 1; t_c[TC_BLEND] += key != 0;)
    int kind = fast ? 2 : mid ? 3 : mina ? 4 : tex;
    SceGxmFragmentProgram *p = fragProg(kind, key, colorMask(e));
    if (!p) { res = T_PROG; break; }
    sceGxmSetFragmentProgram(c, p);
    sceGxmReserveFragmentDefaultUniformBuffer(c, &u);
    // uniforms (see shaders/*.cg): at = alpha test function (1: always passes) and reference; tf = texture
    // function, TCC and color doubling
    float tf[4] = { (float)(c9 & 7), (float)(c9 >> 8 & 1), (float)(c9 >> 16 & 1), 0 };
    if (fast) sceGxmSetUniformDataF(u, n_tm, 0, 4, tf);
    else if (mid || mina) {
      float fg[4];
      tf[2] += 1;  // (a factor here: 1 or 2)
      if (mina) tf[0] = (aref + (afn == 6 ? .5f : -.5f)) / 255;
      sceGxmSetUniformDataF(u, mid ? n_td : n_ta, 0, 4, tf);
      rgbf(RE(e, 0xCF) & 0xFFFFFF, fg);
      sceGxmSetUniformDataF(u, mid ? n_fgd : n_fga, 0, 4, fg);
    } else {
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
    zsSet(c, e, gv ? 0 : w[n - 1].c >> 24);  // (the value only counts in clears)
    // GE cull (0x1D on, 0x9B which winding on screen goes); not in clears or rectangles
    uint32_t cl = w3d && d->prim > 2 && prim != 6 && !(clr & 1) && (RE(e, 0x1D) & 1) ? (RE(e, 0x9B) & 1 ? SCE_GXM_CULL_CCW : SCE_GXM_CULL_CW) : SCE_GXM_CULL_NONE;
    if (cl != t_cl) { t_cl = cl; sceGxmSetCullMode(c, cl); }
  ready:;
    DEV(if (sfx && !dst && w3d && !(clr & 1)) {  // (the GPU path has no vertices here: they are done once more, as the CPU path)
          uint32_t z = n, cu = d->prim > 2 && prim != 6 && (RE(e, 0x1D) & 1), k = (key != 0) | (t3.fog != 0) << 1 | (tex && !d->toff) << 2;
          if (!gv || (d->n * 22 + 8 <= sizeof ts / sizeof *ts && !trVerts(f, d, ts, tw, th, mat, sc, &z, &t3))) { float a = trPx3(gv ? ts : w, z, prim, cu) / 1000; b_fx[k] += a; b_fxd[k]++; if (gv > 1) { b_lit[0] += a; b_lit[1]++; b_lit[2] += d->n; } }
        })
    if (gv) {
      if (nu) { sceGxmReserveVertexDefaultUniformBuffer(c, &u); t3Uni(u, &t3, tw, th, wm, gl, mat); }
      // Same state as the pending draw and same vertex size: its triangles are added by indices to the pending draw, on the
      // vertices where the record has them. The base goes below the first draw because the record fills downwards
      uint32_t vo = d->voff & ~VX, vz = gv == 1 ? 24 : gv == 2 ? 16 : 20, nt = prim == 3 ? n / 3 : n - 2;
      if (gn > gs && (gz != vz || !gOff(vo, gb, vz, &ix) || ix + n > 0xFFFF)) GRUN();
      if (n < 3 || gn + nt * 3 > TG_N DEV(|| no_join)) {
        GRUN();
        sceGxmSetVertexStream(c, 0, f->tx + vo);
        sceGxmDraw(c, prim == 3 ? SCE_GXM_PRIMITIVE_TRIANGLES : prim == 5 ? SCE_GXM_PRIMITIVE_TRIANGLE_FAN : SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, tib, n);
      } else {
        if (gn == gs) { ix = vo / vz < 32000 ? vo / vz : 32000; gb = vo - ix * vz; gz = vz; }
        gn += gAdd(gi + gn, ix, prim, nt);
        gd = d;
      }
      pd = d; t_draws++;
      DEV(t_us[3] += now() - tb;)
      continue;
    }
    DEV(if (smp) { float a = w3d ? trPx3(w, n, prim, d->prim > 2 && prim != 6 && !(clr & 1) && (RE(e, 0x1D) & 1)) : ar;
          bpx[dst != 0][pb] += a; for (int k = 0; k < BW_N; k++) if (why >> k & 1) bwy[k] += a; })
    // Joined to the pending draw when it can be: a triangle strip needs repeated vertices in between (which draw nothing) and to
    // start on an even vertex, so its winding does not flip
    uint32_t pad = 0;
    if (nv + n + 3 > TV_N) { res = T_LIMIT; break; }
    if (same && rn && rp == prim && prim != 5 && w == ts) {
      if (prim == 4) { pad = 2 + (rn & 1); o[nv] = tl; o[nv + 1] = w[0]; o[nv + 2] = w[0]; }
    } else { RUN(); rv = nv; rp = prim; }
    DEV(if (vm) vc = now();)
    if (w == ts) sceClibMemcpy(o + nv + pad, ts, n * sizeof(TV));
    DEV(VT(VS_COPY);)
    tl = w[n - 1];
    rn += pad + n; nv += pad; pd = keep && !(clr & 1) ? d : 0;
    nv += n + extra; t_draws++;
    DEV(t_us[3] += now() - tb;)
  }
  if (!has_base && res == T_OK) OPEN(0);  // (frame with no draws to the surface: it still gets the image underneath)
  RUN();
  if (opened) {  // (if something failed midway, the surface keeps what was drawn: not used)
    sceGxmSetCullMode(c, SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthFunc(c, SCE_GXM_DEPTH_FUNC_ALWAYS);  // the context is Adrenaline's: its defaults back
    sceGxmSetFrontDepthWriteEnable(c, SCE_GXM_DEPTH_WRITE_ENABLED);
    sceGxmSetFrontStencilFunc(c, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0, 0);
    SURF_WAIT();
    DEV(tb = PT();)
    DEV(h_at = 6;)
    sceGxmEndScene(c, 0, gEnd());
    DEV(PA(P_T_END, tb); CTAG();)
    cNote(cur_rt ? C_INT : C_TR, &gq_n, t_draws - d0, spx / 1000, ssw);
  }
#undef VT
#undef OPEN
#undef SURF_WAIT
#undef RUN
#undef GRUN
#undef SCENE
#undef CTAG
  t_q[s] = gq;
  DEV(if (smp) { for (int a = 0; a < 2; a++) for (int i = 0; i < BP_N; i++) b_px[a][i] += bpx[a][i] / 1000; for (int k = 0; k < BW_N; k++) b_why[k] += bwy[k] / 1000; b_pxn++; })
  if (res == T_OK && disp != t_prevd) { t_prev2 = t_prevd; t_prevd = disp; }
  DEV(if (vm) { vu[VS_TKB] = tkb >> 10; vu[VS_FR] = 1; vu[VS_ALL] = now() - ta; vu[VS_RUN] = thrRun() - vr0; for (int i = 0; i < VS_N; i++) v_us[vm - 1][i] += vu[i]; })
  DEV(t_us[5] += now() - ta;
      for (int i = 1; p_on && i < 6; i++) if ((int)(t_us[i] - ut[i]) >= 0) pAdd(P_T_CHECK + i, t_us[i] - ut[i]);)  // (t_us is zeroed every 10 s)
  if (res && !t_seen[res]++) alog("translator: first frame with '%s' on drawing\n", tnames[res]);
  return res;
}
