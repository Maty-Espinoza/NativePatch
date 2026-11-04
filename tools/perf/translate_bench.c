// translate_bench.c: performance bench of the translator (nativedraw/translator.c) on the PC: runs the real trFrame on real
// frame records saved by the Vita, with GXM replaced by functions that do nothing, under Callgrind (instructions per frame) on
// an x86 build. It says how much CPU work the translator does per frame, not what the GPU does. Usage: tools/perf/instr.sh
// runs it; alone: translate_bench <fK_record.bin> [repetitions].
#define NP_DEV
#define NP_BENCH
#include "../../nativedraw/presenter.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <valgrind/callgrind.h>

// --- Vita functions replaced ---
SceUInt32 sceKernelGetProcessTimeLow(void) { return 0; }
SceUInt64 sceKernelGetProcessTimeWide(void) { return 0; }
void *sceClibMemset(void *d, int c, SceSize n) { return memset(d, c, n); }
void *sceClibMemcpy(void *d, const void *s, SceSize n) { return memcpy(d, s, n); }
void *sceClibMemmove(void *d, const void *s, SceSize n) { return memmove(d, s, n); }
int sceClibMemcmp(const void *a, const void *b, SceSize n) { return memcmp(a, b, n); }
int sceClibSnprintf(char *b, SceSize n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
void alog(const char *f, ...) { (void)f; }
PStage P[PN]; volatile uint32_t p_on, nd_exact_until, nd_dump_mark;
const char *nlVersion(void) { return 0; }
void regGpu(uint32_t q, volatile uint32_t *finish) {}
static uint8_t arena[1 << 28]; static uint32_t heap_n, nblk; static void *blk[256];
SceUID sceKernelAllocMemBlock(const char *n, SceKernelMemBlockType t, SceSize s, SceKernelAllocMemBlockOpt *o) {
  if (nblk >= 256 || heap_n + s > sizeof arena) return -1;
  blk[nblk] = arena + heap_n; heap_n += (s + 4095) & ~4095u; return nblk++;
}
int sceKernelGetMemBlockBase(SceUID u, void **b) { if (u < 0 || (uint32_t)u >= nblk) return -1; *b = blk[u]; return 0; }
int sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext *c, void **u) { static float b[256]; *u = b; return 0; }
int sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext *c, void **u) { static float b[256]; *u = b; return 0; }
const SceGxmProgramParameter *sceGxmProgramFindParameterByName(const SceGxmProgram *p, const char *n) { static int x; return (void *)&x; }
int sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *s, SceGxmShaderPatcherId i, SceGxmOutputRegisterFormat f, SceGxmMultisampleMode m,
    const SceGxmBlendInfo *b, const SceGxmProgram *v, SceGxmFragmentProgram **o) { static int x; *o = (void *)&x; return 0; }
int sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *s, SceGxmShaderPatcherId i, const SceGxmVertexAttribute *a, unsigned na, const SceGxmVertexStream *st,
    unsigned ns, SceGxmVertexProgram **o) { static int x; *o = (void *)&x; return 0; }

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
  f->tx = calloc(1, f->ntx + 0x40000); fread(f->tx, 1, f->ntx, fp);
  fclose(fp);
  return f;
}
int main(int argc, char **argv) {
  if (argc < 2) return 1;
  RFrame *f = load(argv[1]);
  int n = argc > 2 ? atoi(argv[2]) : 20, res = 0;
  static uint32_t fake[64];
  if (trInit() < 0) { fprintf(stderr, "trInit failed\n"); return 1; }
  for (int i = 0; i < n; i++) {
    f->frame += 2;
    CALLGRIND_TOGGLE_COLLECT;
    res = trFrame((SceGxmContext *)fake, f, i & 1, (const SceGxmTexture *)fake);
    CALLGRIND_TOGGLE_COLLECT;
  }
  printf("frames=%d draws=%u result=%s\n", n, f->nd, tnames[res]);
  return 0;
}
