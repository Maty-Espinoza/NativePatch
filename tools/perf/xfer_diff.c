// xfer_diff.c: differential check of xferMirror (nativedraw/capture.c) against the original word-by-word copy, under qemu.
// Random RAM to VRAM transfers (2/4 bytes per pixel, strides, offsets, unaligned sources, pages without a copy in the
// mirror, destinations outside the real VRAM, sources near the end of RAM): the candidate must leave both mirrors, the
// readers' cache tags and the read buffer exactly as the reference does.
// Build (after tools/perf/bench.sh made build/perf/inc): arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon
// -mfloat-abi=hard -w -Ibuild/perf/inc -o build/perf/xfer_diff tools/perf/xfer_diff.c (-DCB='"<path>/capture_bench.c"' to
// check another tree); run: qemu-arm-static build/perf/xfer_diff [cases] [seed]
#ifndef CB
#define CB "capture_bench.c"
#endif
#define main bench_main
#include CB
#undef main
static void xferRef(uint32_t s, uint32_t t, uint32_t bp) {
  if (isVram(s) || !isVram(t)) return;
  uint32_t sw = reg[0xB3] & 0xFFFF, dw = reg[0xB5] & 0xFFFF, w = ((reg[0xEE] & 0x3FF) + 1) * bp, h = (reg[0xEE] >> 10 & 0x3FF) + 1;
  uint32_t src = s + ((reg[0xEB] >> 10 & 0x3FF) * sw + (reg[0xEB] & 0x3FF)) * bp, dst = t + ((reg[0xEC] >> 10 & 0x3FF) * dw + (reg[0xEC] & 0x3FF)) * bp;
  for (uint32_t y = 0; y < h; y++, src += sw * bp, dst += dw * bp)
    for (uint32_t o = 0, n; o < w; o += n) {
      n = 4096 - ((dst + o) & 4095);
      if (n > w - o) n = w - o;
      uint32_t ra = vtr(dst + o);
      int real = (dst + o) >> 24 == 4 && !((dst + o) & 0x200000);
      if (!ra || !real || ramEnd(src + o) < src + o + n) continue;
      if ((ra | (src + o) | n) & 3) for (uint32_t i = 0; i < n; i += 2) ((volatile uint16_t *)PSP(ra))[i / 2] = ((volatile uint16_t *)PSP(src + o))[i / 2];
      else for (uint32_t i = 0; i < n; i += 4) ((volatile uint32_t *)PSP(ra))[i / 4] = ((volatile uint32_t *)PSP(src + o))[i / 4];
      for (uint32_t b = ra & ~63u; b < ra + n; b += 64) if (clOk(b)) cl_tag[clSlot(b)] = 0;
    }
  R0.n = 0;
}
static uint32_t rs = 12345;
static uint32_t R(uint32_t n) { rs = rs * 1664525u + 1013904223u; return (rs >> 8) % n; }
#define MSZ 0x200000
int main(int argc, char **argv) {
  int cases = argc > 1 ? atoi(argv[1]) : 3000; if (argc > 2) rs = atoi(argv[2]);
  if (mmap((void *)0x70000000, 0x2000000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED ||
      mmap((void *)0x72000000, 0x400000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED ||
      mmap((void *)0x72C00000, 0x200000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) { perror("mmap"); return 1; }
  rlo[0] = 0x08000000; rhi[0] = 0x0A000000; nr = 1; initCls(); level = 1; mir_on = 1;  // (readable RAM ends at 0x0A000000: sources near the end)
  cl_data = calloc(CL_N, 64); cl_tag = calloc(CL_N, 4);
  for (uint32_t i = 0; i < 0x2000000 / 4; i++) ((uint32_t *)0x70000000)[i] = i * 2654435761u;
  uint8_t *m1 = (uint8_t *)PSP(MIRROR), *m2 = (uint8_t *)PSP(MIRROR2);
  uint8_t *s1 = malloc(MSZ), *s2 = malloc(MSZ), *r1 = malloc(MSZ), *r2 = malloc(MSZ);
  uint32_t *ct = malloc(CL_N * 4), *rt = malloc(CL_N * 4);
  int bad = 0; uint64_t bytes = 0;
  for (int c = 0; c < cases && bad < 5; c++) {
    // state: mirror contents, which pages are valid in each mirror, the copy in use, cache tags
    for (int b = 0; b < 2; b++) for (int w = 0; w < 16; w++) mir_ok[b][w] = R(8) ? (R(65536) << 16 | R(65536)) | (R(4) ? 0xFFFFFFFFu : 0) : 0;
    for (int p = 0; p < 512; p++) { CPY(0, p) = R(4); CPY(1, p) = R(4); }
    mir_u = 1 + R(3);
    for (uint32_t i = 0; i < MSZ / 4; i += 997) { ((uint32_t *)m1)[i] = R(1u << 30); ((uint32_t *)m2)[i] = R(1u << 30); }
    for (int i = 0; i < CL_N; i++) cl_tag[i] = R(3) ? 0xFFFFFFFFu : R(1u << 30) | 1;
    uint32_t bp = R(2) ? 4 : 2, pw = 1 + (R(3) ? R(256) : R(1024)), ph = 1 + (R(4) ? R(64) : R(1024));
    uint32_t sw = R(3) ? pw : pw + R(64), dw = R(4) ? (R(2) ? sw : pw) : pw + R(64);
    if (!R(10)) sw = R(8);  // (degenerate strides)
    uint32_t sx = R(3) ? 0 : R(16), sy = R(3) ? 0 : R(8), dx = R(3) ? 0 : R(16), dy = R(3) ? 0 : R(8);
    if (pw + sx > 1024) sx = 0; if (pw + dx > 1024) dx = 0;
    uint32_t s = R(6) ? 0x08000000 + R(0x1F00000) : 0x0A000000 - R(0x40000);
    uint32_t t = R(8) ? 0x04000000 + R(MSZ) : (R(2) ? 0x04400000 + R(MSZ) : 0x04200000 + R(MSZ));
    if (!R(20)) s = 0x04000000 + R(MSZ);  // (VRAM to VRAM: nothing)
    reg[0xB3] = (s >> 8 & 0x0F0000) | sw; reg[0xB5] = (t >> 8 & 0x0F0000) | dw;
    reg[0xEB] = sy << 10 | sx; reg[0xEC] = dy << 10 | dx; reg[0xEE] = (pw - 1) | (ph - 1) << 10;
    s &= ~15u; t &= ~15u;
    memcpy(s1, m1, MSZ); memcpy(s2, m2, MSZ); memcpy(ct, cl_tag, CL_N * 4);
    uint32_t okb[2][16]; memcpy(okb, (void *)mir_ok, sizeof okb);
    R0.n = 7; xferRef(s, t, bp); uint32_t rn = R0.n;
    memcpy(r1, m1, MSZ); memcpy(r2, m2, MSZ); memcpy(rt, cl_tag, CL_N * 4);
    memcpy(m1, s1, MSZ); memcpy(m2, s2, MSZ); memcpy(cl_tag, ct, CL_N * 4); memcpy((void *)mir_ok, okb, sizeof okb);
    R0.n = 7; xferMirror(s, t, bp);
    int e1 = memcmp(r1, m1, MSZ), e2 = memcmp(r2, m2, MSZ), e3 = memcmp(rt, cl_tag, CL_N * 4), e4 = rn != R0.n;
    if (e1 || e2 || e3 || e4) {
      bad++;
      printf("FAIL case %d: s %08x t %08x bp %u w %u h %u sw %u dw %u sxy %u,%u dxy %u,%u mirror1 %d mirror2 %d tags %d R0 %d\n", c, s, t, bp, pw, ph, sw, dw, sx, sy, dx, dy, !!e1, !!e2, !!e3, e4);
    }
    bytes += (uint64_t)pw * ph * bp;
  }
  printf("%s: %d cases, %d failed, %llu MB requested\n", bad ? "FAIL" : "ok", cases, bad, (unsigned long long)(bytes >> 20));
  return bad != 0;
}
