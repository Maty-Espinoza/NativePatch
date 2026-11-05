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
static uint8_t arena[1 << 28]; static uint32_t heap_n, nblk; static void *blk[256];
SceUID sceKernelAllocMemBlock(const char *n, SceKernelMemBlockType t, SceSize s, SceKernelAllocMemBlockOpt *o) {
  if (nblk >= 256 || heap_n + s > sizeof arena) return -1;
  blk[nblk] = arena + heap_n; heap_n += (s + 4095) & ~4095u; return nblk++;
}
int sceKernelGetMemBlockBase(SceUID u, void **b) { if (u < 0 || (uint32_t)u >= nblk) return -1; *b = blk[u]; return 0; }
int sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext *c, void **u) { static float b[256]; *u = b; return 0; }
int sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext *c, void **u) { static float b[256]; *u = b; return 0; }
// What the context holds, for the dump: each program parameter (by program and name) with its last values, vertex stream, programs
static struct { const SceGxmProgram *p; char name[8]; float v[16]; } par[64];
static const void *stream; static uint32_t fprog, tex_w;
static FILE *dmp, *tri;
static const SceGxmVertexProgram *vprog;
static uint32_t gn[3];  // vertices drawn by each GPU vertex program
static uint32_t cur_w, set_w, in_rt;  // (in_rt: an intermediate image: the CPU path clips there, nd_v3 leaves it to the GPU, so its triangles differ)
volatile SceUID nd_gfx_uid;
void sceGxmSetVertexProgram(SceGxmContext *c, const SceGxmVertexProgram *p) { vprog = p; }
int sceGxmTextureSetWidth(SceGxmTexture *t, unsigned w) { cur_w = w; return 0; }
int sceGxmSetFragmentTexture(SceGxmContext *c, unsigned i, const SceGxmTexture *t) { set_w = cur_w; return 0; }  // (width in the context: draws wait to be joined)
const SceGxmProgramParameter *sceGxmProgramFindParameterByName(const SceGxmProgram *p, const char *n) {
  for (int i = 0; i < 64; i++) {
    if (par[i].p == p && !strcmp(par[i].name, n)) return (void *)&par[i];
    if (!par[i].p) { par[i].p = p; strncpy(par[i].name, n, 7); return (void *)&par[i]; }
  }
  return 0;
}
int sceGxmSetUniformDataF(void *u, const SceGxmProgramParameter *q, unsigned o, unsigned n, const float *d) { if (q) memcpy(((typeof(&par[0]))q)->v + o, d, n * 4); return 0; }
int sceGxmShaderPatcherRegisterProgram(SceGxmShaderPatcher *s, const SceGxmProgram *p, SceGxmShaderPatcherId *i) { static uint32_t n; *i = (SceGxmShaderPatcherId)++n; return 0; }
const SceGxmProgram *sceGxmShaderPatcherGetProgramFromId(SceGxmShaderPatcherId i) { return (const SceGxmProgram *)i; }
// A fragment program is its shader, blend and mask
int sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *s, SceGxmShaderPatcherId i, SceGxmOutputRegisterFormat f, SceGxmMultisampleMode m,
    const SceGxmBlendInfo *b, const SceGxmProgram *v, SceGxmFragmentProgram **o) {
  static uint32_t x[128][2], n;
  x[n][0] = (uint32_t)i; memcpy(&x[n][1], b, 4); *o = (void *)x[n++]; return 0;
}
void sceGxmSetFragmentProgram(SceGxmContext *c, const SceGxmFragmentProgram *p) { const uint32_t *x = (const void *)p; fprog = x ? x[0] << 28 ^ x[1] : 0; }
int sceGxmSetVertexStream(SceGxmContext *c, unsigned i, const void *d) { stream = d; return 0; }
int sceGxmTextureInitLinear(SceGxmTexture *t, const void *d, SceGxmTextureFormat f, unsigned w, unsigned h, unsigned m) { tex_w = w << 16 ^ h ^ f; cur_w = w; return 0; }
int sceGxmBeginScene(SceGxmContext *context, unsigned int flags, const SceGxmRenderTarget *renderTarget, const SceGxmValidRegion *validRegion, SceGxmSyncObject *vertexSyncObject, SceGxmSyncObject *fragmentSyncObject, const SceGxmColorSurface *colorSurface, const SceGxmDepthStencilSurface *depthStencil) { in_rt = validRegion && colorSurface != &rtb.cs; if (dmp) fprintf(dmp, "S\n"); return 0; }
// Value of parameter p (by name) of the vertex program in the context
static const float *uni(const SceGxmProgramParameter *p) { return ((typeof(&par[0]))p)->v; }
// One vertex as the GPU's clip stage sees it: position, UV in texels, fog, color (thousandths, so a rounding of 1 passes).
// k: its place in the stream
static void triV(uint32_t k) {
  float o[4], u, v, g, c[4];
  if (vprog == gvp[1] || vprog == gvp[2]) {
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
  } else if (vprog == gvp[0]) {
    const uint8_t *p = (const uint8_t *)stream + k * 24;
    const float *x = (const float *)(p + 12), *uv = (const float *)p, *m = uni(g_m), *tu = uni(g_tu), *fg = uni(g_fg), *cm = uni(g_cm), *ca = uni(g_ca);
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
  uint32_t sz = vprog == gvp[0] ? 24 : vprog == gvp[1] ? 16 : vprog == gvp[2] ? 20 : sizeof(TV);
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
  if (ix) gxm_calls++;
  if (tri && ix && !in_rt) triDump(t, ix, n);
  for (int i = 0; i < 3; i++) if (vprog == gvp[i]) gn[i] += n;
  if (!dmp || !ix) return 0;  // (no ix: the presenter's quad, not the translator's)
  fprintf(dmp, "D %d %u %08x %08x", t, n, fprog, tex_w);
  for (int i = 0; i < 64 && par[i].p; i++) if (strcmp(par[i].name, "wvp")) for (int k = 0; k < 4; k++) fprintf(dmp, " %.5g", par[i].v[k]);
  fputc('\n', dmp);
  for (unsigned i = 0; i < n; i++) {
    const TV *q = (const TV *)stream + ((const uint16_t *)ix)[i];
    fprintf(dmp, "V %.5g %.5g %.6g %.5g %.5g %.5g %.5g %08x\n", q->x, q->y, q->z, q->w, q->u, q->v, q->f, q->c);
  }
  return 0;
}
int sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *s, SceGxmShaderPatcherId i, const SceGxmVertexAttribute *a, unsigned na, const SceGxmVertexStream *st,
    unsigned ns, SceGxmVertexProgram **o) { static int x[4], n; *o = (void *)&x[n++ & 3]; return 0; }

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
// Moves the vertices of the draws rgVGpu would mark to the end of tx, as the record does
static void gvMark(RFrame *f) {
  uint32_t b4 = 0, b5 = 0, b9 = 0;
  for (uint32_t i = 0; i < f->nd; i++) {
    RDraw *d = &f->d[i];
    const uint32_t *e = f->e[d->st];
    uint32_t d5 = RE(e, 0xD5), w = RE(e, 0x9D) & 0x7FF, m = (d->vlen + 47) / 48 * 48;
    if (RE(e, 0xD3) & 1) { b4 = RE(e, 0xD4); b5 = d5; b9 = RE(e, 0x9C); }
    if ((d->vt != 0x19F && !GVL(d->vt)) || d->ioff || d->prim < 3 || d->prim > 5 || RE(e, 0xD3) & 1 || !(RE(e, 0x50) & 1) ||
        (d->vt == 0x19F && (RE(e, 0xC0) & 3 || RE(e, 0x17) & (RE(e, 0x18) | RE(e, 0x19) | RE(e, 0x1A) | RE(e, 0x1B)) & 1)) ||
        (GVL(d->vt) && (RE(e, 0x17) & 1 ? RE(e, 0xC0) & 3 : (RE(e, 0xC0) & 0x303) != 0x301)) ||
        ((RE(e, 0xD4) & 0xFFFFF || (d5 & 0x3FF) + 1 < (w < 480 ? w : 480) || (w >= 480 && (d5 >> 10 & 0x3FF) < 271)) &&
         ((RE(e, 0xD4) ^ b4) & 0xFFFFF || (d5 ^ b5) & 0xFFFFF || (RE(e, 0x9C) ^ b9) & 0xFFFFF0))) continue;
    f->nvx += m;
    memcpy(f->tx + TX_N - f->nvx, f->a + d->voff - 1, d->vlen);
    d->voff = VX | (TX_N - f->nvx);
  }
}
int main(int argc, char **argv) {
  if (argc < 2) return 1;
  RFrame *f = load(argv[1]);
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
  printf("frames=%d draws=%u result=%s gpu_vertices=%u,%u,%u gxm_draws=%u\n", n, f->nd, tnames[res], gn[0] / n, gn[1] / n, gn[2] / n, gxm_calls / n);
  return 0;
}
