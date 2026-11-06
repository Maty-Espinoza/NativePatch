// translate_bench.c: performance bench of the translator (nativedraw/translator.c) on the PC: runs the real trFrame on real
// frame records saved by the Vita, with GXM replaced by functions that do nothing, under Callgrind (instructions per frame) on
// an x86 build. It says how much CPU work the translator does per frame, not what the GPU does. Usage: tools/perf/instr.sh
// runs it; alone: translate_bench <fK_record.bin> [repetitions]. With TR_DUMP=<file> it also writes every draw with its vertices and
// state, which tools/perf/tr_check.sh compares between two versions: a faster translator must give the same result. With TR_GV=1
// the draws the record would mark for the GPU (rgVGpu) have their vertices moved to the end of tx, as on the Vita; with TR_TRI=<file>
// it writes each triangle as the GPU receives it, from either path (nd_v3 done here), and tools/perf/tr_gv.sh compares both paths.
#define NP_DEV
#define NP_BENCH
#include "../../nativedraw/presenter.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef __arm__
#define CALLGRIND_TOGGLE_COLLECT
#else
#include <valgrind/callgrind.h>
#endif

// --- Vita functions replaced ---
SceUInt32 sceKernelGetProcessTimeLow(void) { return 0; }
SceUInt64 sceKernelGetProcessTimeWide(void) { return 0; }
int sceKernelGetThreadInfo(SceUID t, SceKernelThreadInfo *i) { return -1; }
SceUID sceKernelGetThreadId(void) { return 0; }
void *sceClibMemset(void *d, int c, SceSize n) { return memset(d, c, n); }
void *sceClibMemcpy(void *d, const void *s, SceSize n) { return memcpy(d, s, n); }
void *sceClibMemmove(void *d, const void *s, SceSize n) { return memmove(d, s, n); }
int sceClibMemcmp(const void *a, const void *b, SceSize n) { return memcmp(a, b, n); }
int sceClibSnprintf(char *b, SceSize n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
void alog(const char *f, ...) { (void)f; }
PStage P[PN]; volatile uint32_t p_on, nd_exact_until, nd_dump_mark;
const char *nlVersion(void) { return 0; }
void regGpu(uint32_t q, volatile uint32_t *finish) {}
uint32_t regState(void) { return 0; }
void capHealth(void) {}
static uint8_t arena[1 << 28]; static uint32_t heap_n, nblk; static void *blk[256];
int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *i) { return -1; }
int sceAppMgrGetBudgetInfo(SceAppMgrBudgetInfo *i) { return -1; }
SceUID sceKernelAllocMemBlock(const char *n, SceKernelMemBlockType t, SceSize s, SceKernelAllocMemBlockOpt *o) {
  if (nblk >= 256 || heap_n + s > sizeof arena) return -1;
  blk[nblk] = arena + heap_n; heap_n += (s + 4095) & ~4095u; return nblk++;
}
int sceKernelGetMemBlockBase(SceUID u, void **b) { if (u < 0 || (uint32_t)u >= nblk) return -1; *b = blk[u]; return 0; }
static uint32_t g_ops, g_uf;  // per-draw GXM calls of the translator (programs, uniforms, textures, streams, draws) and uniform floats written
int sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext *c, void **u) { static float b[256]; *u = b; g_ops++; return 0; }
int sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext *c, void **u) { static float b[256]; *u = b; g_ops++; return 0; }
// What the context holds, for the dump: each program parameter (by program and name) with its last values, vertex stream, programs
static struct { const SceGxmProgram *p; char name[8]; float v[224]; } par[64];
static const void *stream; static uint32_t fprog, tex_w;
static FILE *dmp, *tri;
static const SceGxmVertexProgram *vprog;
static uint32_t gn[5];  // vertices drawn by each GPU vertex program (4: all of nd_v5)
static uint32_t cur_w, set_w, in_rt;  // (in_rt: an intermediate image: the CPU path clips there, nd_v3 leaves it to the GPU, so its triangles differ)
volatile SceUID nd_gfx_uid;
void sceGxmSetVertexProgram(SceGxmContext *c, const SceGxmVertexProgram *p) { vprog = p; g_ops++; }
int sceGxmTextureSetWidth(SceGxmTexture *t, unsigned w) { cur_w = w; return 0; }
int sceGxmSetFragmentTexture(SceGxmContext *c, unsigned i, const SceGxmTexture *t) { set_w = cur_w; g_ops++; return 0; }  // (width in the context: draws wait to be joined)
const SceGxmProgramParameter *sceGxmProgramFindParameterByName(const SceGxmProgram *p, const char *n) {
  for (int i = 0; i < 64; i++) {
    if (par[i].p == p && !strcmp(par[i].name, n)) return (void *)&par[i];
    if (!par[i].p) { par[i].p = p; strncpy(par[i].name, n, 7); return (void *)&par[i]; }
  }
  return 0;
}
int sceGxmSetUniformDataF(void *u, const SceGxmProgramParameter *q, unsigned o, unsigned n, const float *d) { g_ops++; g_uf += n; if (q) memcpy(((typeof(&par[0]))q)->v + o, d, n * 4); return 0; }
int sceGxmShaderPatcherRegisterProgram(SceGxmShaderPatcher *s, const SceGxmProgram *p, SceGxmShaderPatcherId *i) { static uint32_t n; *i = (SceGxmShaderPatcherId)++n; return 0; }
const SceGxmProgram *sceGxmShaderPatcherGetProgramFromId(SceGxmShaderPatcherId i) { return (const SceGxmProgram *)i; }
// A fragment program is its shader, blend and mask
int sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *s, SceGxmShaderPatcherId i, SceGxmOutputRegisterFormat f, SceGxmMultisampleMode m,
    const SceGxmBlendInfo *b, const SceGxmProgram *v, SceGxmFragmentProgram **o) {
  static uint32_t x[128][2], n;
  x[n][0] = (uint32_t)i; memcpy(&x[n][1], b, 4); *o = (void *)x[n++]; return 0;
}
void sceGxmSetFragmentProgram(SceGxmContext *c, const SceGxmFragmentProgram *p) { g_ops++; const uint32_t *x = (const void *)p; fprog = x ? x[0] << 28 ^ x[1] : 0; }
int sceGxmSetVertexStream(SceGxmContext *c, unsigned i, const void *d) { stream = d; g_ops++; return 0; }
int sceGxmTextureInitLinear(SceGxmTexture *t, const void *d, SceGxmTextureFormat f, unsigned w, unsigned h, unsigned m) { tex_w = w << 16 ^ h ^ f; cur_w = w; return 0; }
int sceGxmTextureInitLinearStrided(SceGxmTexture *t, const void *d, SceGxmTextureFormat f, unsigned w, unsigned h, unsigned s) { tex_w = w << 16 ^ h ^ f; cur_w = w; return 0; }
int sceGxmBeginScene(SceGxmContext *context, unsigned int flags, const SceGxmRenderTarget *renderTarget, const SceGxmValidRegion *validRegion, SceGxmSyncObject *vertexSyncObject, SceGxmSyncObject *fragmentSyncObject, const SceGxmColorSurface *colorSurface, const SceGxmDepthStencilSurface *depthStencil) { in_rt = validRegion && colorSurface != &rtb.cs; if (dmp) fprintf(dmp, "S\n"); return 0; }
// Value of parameter p (by name) of the vertex program in the context
static const float *uni(const SceGxmProgramParameter *p) { return ((typeof(&par[0]))p)->v; }
#ifdef GVS
// Which nd_v5 program (skinProg) is in the context; -1 if none
static int skI(void) { for (int k = 0; k < 16; k++) if (gvp[4 + k] && vprog == gvp[4 + k]) return k; return -1; }
// triV for nd_v5 program sk (skinProg): its attributes laid out as in trVerts; components not given are 0, w 1
static void triS(int sk, uint32_t k, float *o, float *ou, float *ov, float *og, float *c) {
  float u, v, g;
  static const uint8_t s4[4] = { 0, 1, 2, 4 }, cs[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
  uint32_t vt = sk >> 3 ? 0x7BE : 0x7B9, wn = (sk & 7) + 1, z = 0, of[5], fn[5] = { wn, 2, 1, 3, 3 }, fs[5] = { 4, s4[vt & 3], cs[vt >> 2 & 7], s4[vt >> 5 & 3], 4 };
  for (int i = 0; i < 5; i++) { z = (z + fs[i] - 1) / fs[i] * fs[i]; of[i] = z; z += fn[i] * fs[i]; }
  const uint8_t *p = (const uint8_t *)stream + k * gsz[sk];
  const float *pf = (const float *)p, *x = (const float *)(p + of[4]), *q = uni(g5);
  float w8[8] = { 0, 0, 0, 1, 0, 0, 0, 1 }, nr[3], t2[2], a[4] = { 0, 0, 0, 1 }, s3[3] = { 0, 0, 0 }, sn[3] = { 0, 0, 0 }, wv[3], d[3] = { 0, 0, 0 }, ps[3] = { 0, 0, 0 }, cv[4];
  for (uint32_t i = 0; i < wn && i < 4; i++) w8[i] = pf[i];
  for (uint32_t i = 0; i < (wn > 4 ? wn - 4 : 1); i++) w8[4 + i] = pf[wn > 4 ? 4 + i : i];
  for (int i = 0; i < 3; i++) nr[i] = (int8_t)p[of[3] + i];
  for (int i = 0; i < 2; i++) t2[i] = (vt & 3) == 2 ? ((const uint16_t *)(p + of[1]))[i] : p[of[1] + i];
  for (int i = 0; i < ((vt >> 2 & 7) == 6 ? 2 : 4); i++) a[i] = p[of[2] + i];
  for (int b = 0; b < 8; b++) for (int r = 0; r < 3; r++) {
    const float *m = q + (31 + b * 3 + r) * 4;
    s3[r] += w8[b] * (x[0] * m[0] + x[1] * m[1] + x[2] * m[2] + m[3]); sn[r] += w8[b] * (nr[0] * m[0] + nr[1] * m[1] + nr[2] * m[2]);
  }
  for (int r = 0; r < 3; r++) wv[r] = sn[0] * q[40 + r * 4] + sn[1] * q[41 + r * 4] + sn[2] * q[42 + r * 4];
  float dd = wv[0] * wv[0] + wv[1] * wv[1] + wv[2] * wv[2], ri = 1 / sqrtf(dd > 1e-30f ? dd : 1e-30f), h[2], l[2];
  for (int r = 0; r < 3; r++) wv[r] *= ri;
  for (int i = 0; i < 2; i++) { h[i] = floorf(a[i] / 16); l[i] = a[i] - h[i] * 16; }
  const float c4[4] = { l[0] / 15, h[0] / 15, l[1] / 15, h[1] / 15 };
  for (int i = 0; i < 4; i++) cv[i] = q[47] > 0 ? a[i] / 255 : c4[i];
  for (int i = 0; i < 4; i++) {
    const float *z = q + 60 + i * 16;
    float nl = wv[0] * z[0] + wv[1] * z[1] + wv[2] * z[2], sp = wv[0] * z[4] + wv[1] * z[5] + wv[2] * z[6], pw = nl >= 0 && sp > 0 ? pow(sp, z[7]) : 0;
    for (int j = 0; j < 3; j++) { d[j] += (nl > 0 ? nl : 0) * z[8 + j]; ps[j] += pw * z[12 + j]; }
  }
  for (int i = 0; i < 4; i++) o[i] = s3[0] * q[i] + s3[1] * q[4 + i] + s3[2] * q[8 + i] + q[12 + i];
  if (q[43] > 0) { u = ((wv[0] * q[52] + wv[1] * q[53] + wv[2] * q[54]) + 1) * .5f * q[55]; v = ((wv[0] * q[56] + wv[1] * q[57] + wv[2] * q[58]) + 1) * .5f * q[59]; }
  else { u = t2[0] * q[16] + q[18]; v = t2[1] * q[17] + q[19]; }
  g = s3[0] * q[20] + s3[1] * q[21] + s3[2] * q[22] + q[23]; g = g < 0 ? 0 : g > 1 ? 1 : g;
  for (int i = 0; i < 4; i++) {
    float y = q[24 + i] + q[28 + i] * cv[i] + (i < 3 ? d[i] * (q[32 + i] + q[35] * cv[i]) + ps[i] * (q[36 + i] + q[39] * cv[i]) : 0);
    c[i] = (y < 0 ? 0 : y > 1 ? 1 : y) * 255;
  }
  *ou = u; *ov = v; *og = g;
}
#else  // (an older translator, built by tr_check.sh)
#define GVS(vt) 0
static uint32_t gsz[1];
static SceGxmShaderPatcherId i5;
static int skI(void) { return -1; }
static void triS(int sk, uint32_t k, float *o, float *ou, float *ov, float *og, float *c) {}
#endif
// One vertex as the GPU's clip stage sees it: position, UV in texels, fog, color (thousandths, so a rounding of 1 passes).
// k: its place in the stream
static void triV(uint32_t k) {
  float o[4], u, v, g, c[4];
  int sk = skI();
  if (sk >= 0) triS(sk, k, o, &u, &v, &g, c);
  else if (vprog == gvp[1] || vprog == gvp[2]) {
    int lf = vprog == gvp[2];
    const uint8_t *p = (const uint8_t *)stream + k * (lf ? 20 : 16);
    const int16_t *x = (const int16_t *)(p + (lf ? 14 : 10)), *nr = (const int16_t *)(p + (lf ? 8 : 4));
    const float *m = uni(g4[0]), *tu = uni(g4[1]), *fg = uni(g4[2]), *nx = uni(g4[3]), *ny = uni(g4[4]), *nz = uni(g4[5]), *ld = uni(g4[6]), *hv = uni(g4[7]), *ba = uni(g4[8]), *cd = uni(g4[9]), *cs = uni(g4[10]), *t0 = uni(g4[11]), *t1 = uni(g4[12]), *t2 = uni(g4[13]), *gu = uni(g4gu);
    float a = lf ? ((const float *)p)[0] : ((const uint16_t *)p)[0], b = lf ? ((const float *)p)[1] : ((const uint16_t *)p)[1];
    float X = x[0], Y = x[1], Z = x[2], n[3] = { nr[0] * nx[0] + nr[1] * nx[1] + nr[2] * nx[2], nr[0] * ny[0] + nr[1] * ny[1] + nr[2] * ny[2], nr[0] * nz[0] + nr[1] * nz[1] + nr[2] * nz[2] };
    tnorm(n);
    for (int i = 0; i < 4; i++) o[i] = X * m[i] + Y * m[4 + i] + Z * m[8 + i] + m[12 + i];
    u = a * tu[0] + tu[2]; v = b * tu[1] + tu[3];
    if (gu[2]) {
      float r0 = nr[0] * t0[0] + nr[1] * t0[1] + nr[2] * t0[2] + t0[3], r1 = nr[0] * t1[0] + nr[1] * t1[1] + nr[2] * t1[2] + t1[3], r2 = nr[0] * t2[0] + nr[1] * t2[1] + nr[2] * t2[2] + t2[3];
      u = r0 / r2 * gu[0]; v = r1 / r2 * gu[1];
    }
    g = X * fg[0] + Y * fg[1] + Z * fg[2] + fg[3]; g = g < 0 ? 0 : g > 1 ? 1 : g;
    float nl = n[0] * ld[0] + n[1] * ld[1] + n[2] * ld[2], sd = n[0] * hv[0] + n[1] * hv[1] + n[2] * hv[2], s = nl >= 0 && sd > 0 ? pow(sd, hv[3]) : 0;
    for (int i = 0; i < 4; i++) { float q = i < 3 ? ba[i] + (nl > 0 ? nl : 0) * cd[i] + s * cs[i] : ba[3]; c[i] = (q < 0 ? 0 : q > 1 ? 1 : q) * 255; }
  } else if (vprog == gvp[0] || vprog == gvp[3]) {  // (gvp[3]: 16-bit UV, color at 4)
    const uint8_t *p = (const uint8_t *)stream + k * 24;
    int h = vprog == gvp[3];
    const float *x = (const float *)(p + 12), *m = uni(g_m), *tu = uni(g_tu), *fg = uni(g_fg), *cm = uni(g_cm), *ca = uni(g_ca);
    float uv[2] = { h ? ((const uint16_t *)p)[0] : ((const float *)p)[0], h ? ((const uint16_t *)p)[1] : ((const float *)p)[1] };
    p += h ? -4 : 0;  // (the color, at 8 or 4)
    for (int i = 0; i < 4; i++) o[i] = x[0] * m[i] + x[1] * m[4 + i] + x[2] * m[8 + i] + m[12 + i];
    u = uv[0] * tu[0] + tu[2]; v = uv[1] * tu[1] + tu[3]; g = x[0] * fg[0] + x[1] * fg[1] + x[2] * fg[2] + fg[3]; g = g < 0 ? 0 : g > 1 ? 1 : g;
    for (int i = 0; i < 4; i++) { c[i] = p[8 + i] * cm[i] + ca[i] * 255; c[i] = c[i] > 255 ? 255 : c[i]; }
  } else {
    const TV *q = (const TV *)stream + k;
    const float *m = uni(n_wvp), a[4] = { q->x, q->y, q->z, q->w };
    for (int i = 0; i < 4; i++) o[i] = a[0] * m[i] + a[1] * m[4 + i] + a[2] * m[8 + i] + a[3] * m[12 + i];
    u = q->u; v = q->v; g = q->f;
    for (int i = 0; i < 4; i++) c[i] = q->c >> i * 8 & 255;
  }
  if (fprog >> 28 == ((uint32_t)ifc & 15)) u = v = 0;  // (no texture: its UV means nothing)
  fprintf(tri, "V %.5g %.5g %.6g %.5g %.3f %.5g %.5g %.4f %.4f %.4f %.4f\n", o[0], o[1], o[2], o[3], u * set_w, v, g, c[0] / 1000, c[1] / 1000, c[2] / 1000, c[3] / 1000);
}
// The triangles of a draw of n vertices in order, each with the winding it has on screen; those with two equal vertices (joined
// strips) draw nothing and are left out
static void triDump(SceGxmPrimitiveType t, const uint16_t *ix, unsigned n) {
  uint32_t sz = vprog == gvp[0] || vprog == gvp[3] ? 24 : vprog == gvp[1] ? 16 : vprog == gvp[2] ? 20 : skI() >= 0 ? gsz[skI()] : sizeof(TV);
  for (unsigned i = 0; i + 2 < n; i += t == SCE_GXM_PRIMITIVE_TRIANGLES ? 3 : 1) {
    unsigned a = t == SCE_GXM_PRIMITIVE_TRIANGLE_FAN ? 0 : i, b = i + 1, c = i + 2;
    if (t == SCE_GXM_PRIMITIVE_TRIANGLE_STRIP && i & 1) { unsigned s = b; b = c; c = s; }
    a = ix[a]; b = ix[b]; c = ix[c];
    const uint8_t *p = stream;
    if (!memcmp(p + a * sz, p + b * sz, sz) || !memcmp(p + b * sz, p + c * sz, sz) || !memcmp(p + a * sz, p + c * sz, sz)) continue;
    fprintf(tri, "D %08x\n", fprog); triV(a); triV(b); triV(c);
  }
}
static uint32_t gxm_calls;
int sceGxmDraw(SceGxmContext *c, SceGxmPrimitiveType t, SceGxmIndexFormat f, const void *ix, unsigned n) {
  if (ix) gxm_calls++, g_ops++;
  if (tri && ix && !in_rt) triDump(t, ix, n);
  for (int i = 0; i < 4; i++) if (vprog == gvp[i]) gn[i] += n;
  if (skI() >= 0) gn[4] += n;
  if (!dmp || !ix) return 0;  // (no ix: the presenter's quad, not the translator's)
  fprintf(dmp, "D %d %u %08x %08x", t, n, fprog, tex_w);
  // (not nd_v5's: an older translator, built by tr_check.sh, has none)
  for (int i = 0; i < 64 && par[i].p; i++) if (strcmp(par[i].name, "wvp") && par[i].p != (const SceGxmProgram *)i5) for (int k = 0; k < 4; k++) fprintf(dmp, " %.5g", par[i].v[k]);
  fputc('\n', dmp);
  for (unsigned i = 0; i < n; i++) {
    const TV *q = (const TV *)stream + ((const uint16_t *)ix)[i];
    fprintf(dmp, "V %.5g %.5g %.6g %.5g %.5g %.5g %.5g %08x\n", q->x, q->y, q->z, q->w, q->u, q->v, q->f, q->c);
  }
  return 0;
}
int sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *s, SceGxmShaderPatcherId i, const SceGxmVertexAttribute *a, unsigned na, const SceGxmVertexStream *st,
    unsigned ns, SceGxmVertexProgram **o) { static int x[32], n; *o = (void *)&x[n++ & 31]; return 0; }

// Reads a dump's record like repLoad, into plain memory
static RFrame *load(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) { perror(path); exit(1); }
  static uint32_t h[16]; uint8_t rs[256];
  RFrame *f = calloc(1, sizeof(RFrame));
  fread(h, 1, sizeof h, fp);
  if (h[0] != 0x47524E44 || h[13] != NRS || h[14] != sizeof(RDraw)) { fprintf(stderr, "%s: not a record of this version\n", path); exit(1); }
  f->frame = h[2]; f->fb = h[3]; f->fbw = h[4]; f->nd = h[5]; f->ne = h[6]; f->na = h[7]; f->nt = h[9]; f->ntx = h[10]; f->mir = h[11]; f->mirf = h[12];
#define RD_(p, n) do { uint32_t n_ = (n); fread(p, 1, n_, fp); if (n_ & 3) fseek(fp, 4 - (n_ & 3), SEEK_CUR); } while (0)
  RD_(rs, NRS); RD_(f->ta, f->nt * 4); RD_(f->tn, f->nt * 4); RD_(f->to, f->nt * 4); RD_(f->tq, f->nt * 4); RD_(f->tm, f->nt * 8);
  RD_(f->d, f->nd * sizeof(RDraw)); RD_(f->e, f->ne * NRS * 4); RD_(f->a, f->na);
  f->tx = calloc(1, TX_N + 0x40000); fread(f->tx, 1, f->ntx, fp);
  fclose(fp);
  return f;
}
// rgVGpu's condition for the types of GVS, with the light registers of the draw's block
static int gvSkin(const RFrame *f, const RDraw *d, const uint32_t *e) {
  if (!d->loff) return !(RE(e, 0xC0) & 3);
  const uint32_t *l = (const uint32_t *)(f->a + d->loff - 1) - 0x53;
  uint32_t g = RE(e, 0xC0) & 3, c1 = RE(e, 0xC1);
  for (int i = 0; i < 4; i++) if (RE(e, 0x17) & RE(e, 0x18 + i) & 1 && (l[0x5F + i] & 0x300 || (l[0x5F + i] & 3) == 2)) return 0;
  return !g || (g == 2 && !(l[0x5F + (c1 & 3)] & 0x300) && !(l[0x5F + (c1 >> 8 & 3)] & 0x300));
}
// Moves the vertices of the draws rgVGpu would mark to the end of tx, as the record does
static void gvMark(RFrame *f) {
  uint32_t b4 = 0, b5 = 0, b9 = 0;
  for (uint32_t i = 0; i < f->nd; i++) {
    RDraw *d = &f->d[i];
    const uint32_t *e = f->e[d->st];
    uint32_t d5 = RE(e, 0xD5), w = RE(e, 0x9D) & 0x7FF, m = d->vlen, vz = d->n ? d->vlen / d->n : 16;
    if (RE(e, 0xD3) & 1) { b4 = RE(e, 0xD4); b5 = d5; b9 = RE(e, 0x9C); }
    if ((!GV3(d->vt) && !GVL(d->vt) && !GVS(d->vt)) || d->ioff || d->prim < 3 || d->prim > 5 || RE(e, 0xD3) & 1 || !(RE(e, 0x50) & 1) ||
        (GV3(d->vt) && (RE(e, 0xC0) & 3 || RE(e, 0x17) & (RE(e, 0x18) | RE(e, 0x19) | RE(e, 0x1A) | RE(e, 0x1B)) & 1)) ||
        (GVL(d->vt) && (RE(e, 0x17) & 1 ? RE(e, 0xC0) & 3 : (RE(e, 0xC0) & 0x303) != 0x301)) || (GVS(d->vt) && !gvSkin(f, d, e)) ||
        ((RE(e, 0xD4) & 0xFFFFF || (d5 & 0x3FF) + 1 < (w < 480 ? w : 480) || (w >= 480 && (d5 >> 10 & 0x3FF) < 271)) &&
         ((RE(e, 0xD4) ^ b4) & 0xFFFFF || (d5 ^ b5) & 0xFFFFF || (RE(e, 0x9C) ^ b9) & 0xFFFFF0))) continue;
    while (m & 15) m += vz;
    if (f->ntx + f->nvx + m > TX_N) continue;
    f->nvx += m;
    memcpy(f->tx + TX_N - f->nvx, f->a + d->voff - 1, d->vlen);
    d->voff = VX | (TX_N - f->nvx);
  }
}
// (TR_STATS=1) What the 3D draws of the frame need from a GPU vertex program, per vertex type: draws, vertices, weights,
// indices, lights on (directional/point/spot, with specular, powered diffuse), generated UV; and how many runs of draws
// would join if only their bone matrices differ (same state, view/projection and lights)
static void stats(RFrame *f) {
  struct { uint32_t vt, d, v, ix, l[5], pt, sp, pw, gen[3], rev; } s[64] = {{0}};
  uint32_t ns = 0, bones = 0, runs = 0, inrun = 0, ub = 0, pw = ~0u, pm = ~0u, pl = ~0u, pst = ~0u, pvt = ~0u, run = 0;
  for (uint32_t i = 0; i < f->nd; i++) {
    RDraw *d = &f->d[i];
    const uint32_t *e = f->e[d->st];
    if (d->vt >> 23 & 1 || RE(e, 0xD3) & 1) { pst = ~0u; continue; }  // (through mode or clear)
    uint32_t k = 0;
    while (k < ns && s[k].vt != d->vt) k++;
    if (k == ns && ns < 64) s[ns++].vt = d->vt;
    if (k == 64) continue;
    s[k].d++; s[k].v += d->n; s[k].ix += d->ioff != 0; s[k].gen[(RE(e, 0xC0) & 3) > 1 ? 2 : RE(e, 0xC0) & 3]++;
    uint32_t nl = 0;
    if (RE(e, 0x17) & 1 && d->loff) {
      const uint32_t *l = (const uint32_t *)(f->a + d->loff - 1) - 0x53;
      for (int j = 0; j < 4; j++) if (RE(e, 0x18 + j) & 1) {
        nl++; s[k].pt += (l[0x5F + j] >> 8 & 3) == 1 ? d->n : 0; s[k].sp += (l[0x5F + j] >> 8 & 3) == 2 ? d->n : 0;
        s[k].pw += (l[0x5F + j] & 3) == 2 ? d->n : 0;
      }
      s[k].rev += RE(e, 0x51) & 1;
    }
    s[k].l[RE(e, 0x17) & 1 ? nl + 1 : 0] += d->n;
    if (d->vt >> 9 & 3) {
      bones += d->woff != pw;
      ub += ((d->vt >> 14 & 7) + 1) * 48;  // (bytes of bone matrices its draw would set: 3 vec4 per weight)
      int join = d->st == pst && d->vt == pvt && d->moff == pm && d->loff == pl && d->woff != pw;
      if (join) { if (!run) runs++, inrun++; run = 1; inrun++; } else run = 0;
    } else run = 0;
    pw = d->woff; pm = d->moff; pl = d->loff; pst = d->st; pvt = d->vt;
  }
  printf("vt      draws  verts  v/draw indexed | lit: off  0L    1L    2L    3L    4L    | point spot powdiff | gen0 gen1 gen2 | rev\n");
  for (uint32_t k = 0; k < ns; k++)
    printf("%06x %6u %6u %6.1f %7u | %5u %5u %5u %5u %5u %5u | %5u %5u %5u | %4u %4u %4u | %u\n", s[k].vt, s[k].d, s[k].v, (float)s[k].v / s[k].d, s[k].ix,
           s[k].l[0], s[k].l[1], s[k].l[2], s[k].l[3], s[k].l[4], s[k].l[5], s[k].pt, s[k].sp, s[k].pw, s[k].gen[0], s[k].gen[1], s[k].gen[2], s[k].rev);
  printf("skinned: bone sets %u, bone bytes per frame %u | runs of draws differing only in bones %u, draws in them %u\n", bones, ub, runs, inrun);
}
int main(int argc, char **argv) {
  if (argc < 2) return 1;
  RFrame *f = load(argv[1]);
  if (getenv("TR_STATS")) { stats(f); return 0; }
  if (getenv("TR_GV")) gvMark(f);
  if (getenv("TR_TRI")) tri = fopen(getenv("TR_TRI"), "w");
  int n = argc > 2 ? atoi(argv[2]) : 20, res = 0;
  static uint32_t fake[64];
  if (getenv("TR_DUMP")) dmp = fopen(getenv("TR_DUMP"), "w");
  if (trInit() < 0) { fprintf(stderr, "trInit failed\n"); return 1; }
  for (int i = 0; i < n; i++) {
    f->frame += 2;
    CALLGRIND_TOGGLE_COLLECT;
    res = trFrame((SceGxmContext *)fake, f, i & 1, (const SceGxmTexture *)fake);
    CALLGRIND_TOGGLE_COLLECT;
    if (dmp) fprintf(dmp, "F %s\n", tnames[res]);
  }
  printf("frames=%d draws=%u result=%s gpu_vertices=%u,%u,%u,%u,%u gxm_draws=%u gxm_ops=%u uniform_floats=%u vertices_in=%u cpu_vertices=%u\n", n, f->nd, tnames[res],
         gn[0] / n, gn[1] / n, gn[2] / n, gn[3] / n, gn[4] / n, gxm_calls / n, g_ops / n, g_uf / n, t_c[TC_VIN] / n, (t_c[TC_VIN] - t_c[TC_VGPU]) / n);
  return 0;
}
