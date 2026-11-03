// presenter.c: takes over the Adrenaline screen. Hooks its GXM calls (and the PSP image copy) so that, instead of the
// PSP's 512x272 image, Adrenaline shows our own double-size surface (1024x544, 960x544 visible) drawn in its thread and
// context: by the translator (Vita drawing) or as the normal image stretched.
// Since the surface is exactly twice the texture, Adrenaline places and scales it the same way (seen one frame later).
// Also draws the L+R+START menu and hides the buttons from the game while it is open.
// Called by nativedraw.c (presStart/presStop); includes translator.c, dump.c and replay.c and reads the frame
// record (record.c).
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <taihen.h>
#include <psp2/compat.h>
#include <psp2/power.h>
#include "shaders.h"
#include "version.h"
#include "../installer/font.h"

void alog(const char *fmt, ...);  // nativedraw.c
#define CDRAM SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW
#define PHY SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW
#define UNC SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE
#define RD SCE_GXM_MEMORY_ATTRIB_READ
#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)
#define W 960      // what is seen
#define H 544
#define LINE 1024  // surface width: twice the 512 of psp_tex (columns 960-1023 stay black)
// process time in us
static inline uint32_t now(void) { return sceKernelGetProcessTimeLow(); }
#include "prof.h"
DEV(extern volatile SceUID nd_gfx_uid;)

// --- GPU memory ---
// Nothing in CDRAM: the Adrenaline process has only 4 MB and the Vita videos use it. Surfaces go in physically
// contiguous LPDDR, always with their z (without z the GPU does not draw or hangs).
// gmem1: reserves a block of the requested type (rounded to 1 MB in PHY and to 256 KB elsewhere) and maps it for the GPU
// with attr (0: not mapped, for USSE memory). Returns the address or 0.
static void *gmem1(uint32_t type, uint32_t size, int attr) {
  size = (size + (type == PHY ? 0xFFFFF : 0x3FFFF)) & ~(type == PHY ? 0xFFFFFu : 0x3FFFFu);
  void *p = 0;
  SceUID b = sceKernelAllocMemBlock("nativedraw_pres", type, size, 0);
  if (b < 0 || sceKernelGetMemBlockBase(b, &p) < 0) return 0;
  if (attr && sceGxmMapMemory(p, size, attr) < 0) { sceKernelFreeMemBlock(b); return 0; }  // (0: USSE memory)
  return p;
}
static void *gmem(uint32_t size, int attr) { return gmem1(UNC, size, attr); }  // uncached LPDDR: programs, vertices
static uint32_t probe(uint32_t type) {  // MB that can be reserved at once (binary search; for the log)
  uint32_t lo = 0, hi = 64;
  while (lo < hi) {
    uint32_t m = (lo + hi + 1) / 2;
    SceUID b = sceKernelAllocMemBlock("nativedraw_test", type, m << 20, 0);
    if (b >= 0) { sceKernelFreeMemBlock(b); lo = m; } else hi = m - 1;
  }
  return lo;
}
// name of the memory type of p's block (for the log)
static const char *ty(void *p) {
  SceKernelMemBlockInfo in; in.size = sizeof in;
  if (!p || sceKernelGetMemBlockInfoByAddr(p, &in) < 0) return "-";
  return in.type == CDRAM ? "CDRAM" : in.type == PHY ? "LPDDR physical" : in.type == UNC ? "LPDDR" : "other";
}

// --- Programs: own patcher (like vita2d) with texture_v/texture_f from shaders.h ---
// heap/halloc/hfree: patcher CPU memory (32 KB handed out in order and never freed).
static char heap[0x8000] __attribute__((aligned(16)));
static uint32_t hpos;
static void *halloc(void *ud, unsigned int n) {
  n = (n + 15) & ~15u;
  if (hpos + n > sizeof heap) return 0;
  hpos += n;
  return heap + hpos - n;
}
static void hfree(void *ud, void *p) {}
static SceGxmShaderPatcher *pat;
static SceGxmVertexProgram *vp;
static SceGxmFragmentProgram *fp, *fpb;  // fpb: with blending (the menu quad is semitransparent)
static const SceGxmProgramParameter *p_wvp;

// Creates the patcher (with its own buffers and USSE memory), registers texture_v/texture_f and builds the vertex program
// (x, y, z, u, v as float: 20 bytes) and two fragment programs: opaque fp and alpha-blended fpb. Returns < 0 on failure.
static int initProgs(void) {
  SceGxmShaderPatcherParams pp;
  sceClibMemset(&pp, 0, sizeof pp);
  pp.hostAllocCallback = halloc; pp.hostFreeCallback = hfree;
  pp.bufferMem = gmem(0x10000, RD); pp.bufferMemSize = 0x10000;
  pp.vertexUsseMem = gmem(0x10000, 0); pp.vertexUsseMemSize = 0x10000;  // (USSE memory is mapped separately)
  pp.fragmentUsseMem = gmem(0x10000, 0); pp.fragmentUsseMemSize = 0x10000;
  if (!pp.bufferMem || !pp.vertexUsseMem || !pp.fragmentUsseMem) return -1;
  int r = sceGxmMapVertexUsseMemory(pp.vertexUsseMem, 0x10000, &pp.vertexUsseOffset);
  if (r >= 0) r = sceGxmMapFragmentUsseMemory(pp.fragmentUsseMem, 0x10000, &pp.fragmentUsseOffset);
  if (r >= 0) r = sceGxmShaderPatcherCreate(&pp, &pat);
  SceGxmShaderPatcherId iv, ifr;
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)texture_v, &iv);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)texture_f, &ifr);
  if (r < 0) return r;
  const SceGxmProgram *pv = sceGxmShaderPatcherGetProgramFromId(iv);
  const SceGxmProgramParameter *pa = sceGxmProgramFindParameterByName(pv, "aPosition"),
                               *pt = sceGxmProgramFindParameterByName(pv, "aTexcoord");
  p_wvp = sceGxmProgramFindParameterByName(pv, "wvp");
  if (!pa || !pt || !p_wvp) return -2;
  SceGxmVertexAttribute at[2] = { { 0, 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 3, sceGxmProgramParameterGetResourceIndex(pa) },
                                  { 0, 12, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2, sceGxmProgramParameterGetResourceIndex(pt) } };
  SceGxmVertexStream st = { 20, SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
  if ((r = sceGxmShaderPatcherCreateVertexProgram(pat, iv, at, 2, &st, 1, &vp)) < 0) return r;
  r = sceGxmShaderPatcherCreateFragmentProgram(pat, ifr, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
                                               0, pv, &fp);  // (no blending: opaque)
  SceGxmBlendInfo bl = { SCE_GXM_COLOR_MASK_ALL, SCE_GXM_BLEND_FUNC_ADD, SCE_GXM_BLEND_FUNC_ADD, SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
                         SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, SCE_GXM_BLEND_FACTOR_ONE, SCE_GXM_BLEND_FACTOR_ZERO };
  if (r >= 0) r = sceGxmShaderPatcherCreateFragmentProgram(pat, ifr, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                           SCE_GXM_MULTISAMPLE_NONE, &bl, pv, &fpb);
  return r;
}

// --- destination: two alternating surfaces (Adrenaline reads one while the other is drawn) + z ---
static SceGxmRenderTarget *rt;
static SceGxmColorSurface cs[2];
static SceGxmDepthStencilSurface ds;
static SceGxmTexture surf_tex[2];
static void *surf[2], *zbuf, *img[4];  // img: copies of the PSP image (512x272 8888), alternating
static SceGxmTexture itex[4];
// copies in use: with 4 the GPU has almost always finished reading the rewritten one (auto dump compares with all 4)
static uint32_t n_img = 4;
static float *vb;  // full-screen quad: x, y, z, u, v (and from vb + 20 the menu one)
#define MW 256     // menu quad: 256x128 texture, drawn at double size centered (512x256 of the 960x544)
#define MH 128
static uint32_t *mpix;
static SceGxmTexture mtex;
static uint16_t *ib;

// --- Scene end on the GPU ---
// Each scene of ours, when its pixels are done, writes its number (gq) to a free word of the GXM notification region
// (gq_done). Before rewriting something the GPU reads (translated vertices and palettes, the PSP image copy, the record
// textures) it is compared with the number of the last scene that read it. To make that rare, no new frame is sent to
// the GPU until the one from 2 frames ago is finished (the surface is repeated); if it still happens, it waits (gWait)
// and counts it for the log.
static volatile uint32_t *gq_done;
static uint32_t gq, gq_slot;
static SceGxmNotification gq_n;
// statistics for the 10 s log (g_ev: gEv events logged)
static volatile uint32_t g_an, g_asum, g_amax, g_f1, g_f2, g_psurf, g_pimg, g_ev;
// g_behind: frames not sent because the GPU was behind; g_wait_us: total gWait time; g_skip: surface checks (covered and
// different frames) skipped because the GPU had not finished it; g_chk_us: the different-frames check; g_tr_us: the
// translator (trFrame)
static volatile uint32_t g_behind, g_wait_us, g_skip, g_chk_us, g_tr_us;
static uint32_t img_q[4];
// 1 if the GPU has not finished scene q yet
static inline int gBusy(uint32_t q) { return gq_done && (int)(*gq_done - q) < 0; }
// notification for sceGxmEndScene: when done, the GPU writes the scene number (++gq) to gq_done
static const SceGxmNotification *gEnd(void) { if (!gq_done) return 0; gq_n.address = gq_done; gq_n.value = ++gq; return &gq_n; }
// logs (first 60) that "what" was rewritten with the GPU unfinished and how many scenes it had left
static void gEv(const char *what, uint32_t q) {
  if (g_ev++ < 60) alog("gpu: %s with the GPU unfinished (%u scenes left)\n", what, q - *gq_done);
}
// waits (30 ms at most) for the GPU to finish scene q and adds the wait to g_wait_us
static void gWait(uint32_t q) {
  uint32_t t0 = now();
  while (gBusy(q) && now() - t0 < 30000) sceKernelDelayThread(100);
  g_wait_us += now() - t0;
  DEV(if (p_on) pAdd(P_A_WAIT, now() - t0);)
}

// Creates the destination: 1024x544 render target, the two surfaces with their textures, the z, the PSP image copies
// (img/itex: the first 2 in physical LPDDR), the menu texture and the vertex quads (full screen and menu).
// Returns < 0 on failure.
static int initDest(void) {
  SceGxmRenderTargetParams rp;
  sceClibMemset(&rp, 0, sizeof rp);
  // several scenes per frame: translator intermediate images
  rp.width = LINE; rp.height = H; rp.scenesPerFrame = 8; rp.driverMemBlock = -1;
  int r = sceGxmCreateRenderTarget(&rp, &rt);
  if (r < 0) return r;
  for (int i = 0; i < 2; i++) {
    if (!(surf[i] = gmem1(PHY, LINE * H * 4, RW))) return -3;
    sceClibMemset(surf[i], 0, LINE * H * 4);
    sceGxmColorSurfaceInit(&cs[i], SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                           SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, LINE, H, LINE, surf[i]);
    sceGxmTextureInitLinear(&surf_tex[i], surf[i], SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, LINE, H, 0);
  }
  if (!(zbuf = gmem1(PHY, LINE * H * 4, RW))) return -4;  // z is mandatory (see above)
  sceGxmDepthStencilSurfaceInit(&ds, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, LINE, zbuf, 0);
  sceGxmDepthStencilSurfaceSetBackgroundDepth(&ds, 1.0f);
  for (int i = 0; i < (int)n_img; i++) {
    if (!(img[i] = gmem1(i < 2 ? PHY : UNC, 512 * 272 * 4, RD))) return -6;
    sceGxmTextureInitLinear(&itex[i], img[i], SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, 512, 272, 0);
  }
  if (!(mpix = gmem(MW * MH * 4, RD))) return -7;
  sceGxmTextureInitLinear(&mtex, mpix, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, MW, MH, 0);  // (point filter: sharp letters)
  sceGxmTextureSetMinFilter(&mtex, SCE_GXM_TEXTURE_FILTER_POINT); sceGxmTextureSetMagFilter(&mtex, SCE_GXM_TEXTURE_FILTER_POINT);
  if (!(vb = gmem(8 * 20, RD)) || !(ib = gmem(8, RD))) return -5;
  // full screen: the 480 visible columns of the PSP image (of 512) to 960x544; then the centered menu
  const float u1 = 480.0f / 512.0f, z = 0.5f, q[20] = { 0, 0, z, 0, 0, W, 0, z, u1, 0, 0, H, z, 0, 1, W, H, z, u1, 1 };
  sceClibMemcpy(vb, q, sizeof q);
  const float x0 = (W - 2 * MW) / 2, y0 = (H - 2 * MH) / 2, x1 = x0 + 2 * MW, y1 = y0 + 2 * MH,
              m[20] = { x0, y0, z, 0, 0, x1, y0, z, 1, 0, x0, y1, z, 0, 1, x1, y1, z, 1, 1 };
  sceClibMemcpy(vb + 20, m, sizeof m);
  for (int i = 0; i < 4; i++) ib[i] = i;
  return 0;
}

#ifdef NP_DEV
#include "gpu_timer.c"  // how long the GPU takes on each scene (gpu_timer)
#else
#define cNote(ty, n, dr, px, sw) ((void)sizeof((dr) + (px) + (sw)))
#define cAdr(f) (f)
#define c_on 0
#define c_slot 0
#endif
#include "translator.c"  // draws the frame record with the Vita GPU

// --- Hooks (AdrenalineUser imports) and statistics ---
static tai_hook_ref_t ref_end, ref_tex, ref_dma;
static SceUID hk_end = -1, hk_tex = -1, hk_dma = -1;
static const SceGxmTexture *psp_tex;    // Adrenaline's 512x272 texture (the PSP game image)
static volatile uint32_t fr, ready, n_us;  // fr: Adrenaline scenes finished; n_us: those with the PSP image
static uint32_t used, drawn, cur;   // scene in which Adrenaline used psp_tex / in which surf[cur] was drawn
static volatile uint32_t n_pres, n_swap, n_black_j, n_black_p, us_sum, us_max;  // (read in the record thread)
static volatile uint32_t n_chk, n_bad;  // Vita frames checked and different from the normal image
// draw_on: switch draw (always replace the image; otherwise only with the menu open); t_on: translator ready
static int draw_on, t_on DEV(, c_wanted);  // c_wanted: GPU timer (gpu_timer)
static volatile uint32_t menu, painted;  // menu open; its texture already has the text
#define ACTIVE (ready && (draw_on || menu))  // the PSP image is replaced
static void *psp_data;                // psp_tex data (destination of Adrenaline's copy)
static uint32_t copy_fr = ~0u, ki;      // scene in which the image was copied to img[ki]
static volatile uint32_t n_dma;

void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
// Hook on sceDmacMemcpy: the PSP image copy Adrenaline makes at the start of each pass (0x74000000 -> psp_tex) goes to
// img[ki], alternating: the GPU reads the image for the presenter after Adrenaline's read and the next frame's copy would
// overwrite it. copy_fr = scene of the copy. Returns dst like the original.
static void *hookDma(void *dst, const void *src, SceSize size) {
  if (ACTIVE && dst && dst == psp_data && size == 512 * 272 * 4) {  // the PSP image copy: to img[ki]
    ki = (ki + 1) % n_img; copy_fr = fr; n_dma++;
    if (gBusy(img_q[ki])) { g_pimg++; gEv("normal image rewritten", img_q[ki]); gWait(img_q[ki]); }
    DEV(uint32_t tp = PT();)
    TAI_CONTINUE(void *, ref_dma, img[ki], src, size);
    DEV(PA(P_A_DMA, tp);)
    return dst;
  }
  return TAI_CONTINUE(void *, ref_dma, dst, src, size);
}

static int isBlack(const uint32_t *p, uint32_t line, uint32_t w, uint32_t h) {  // 1 if 16 samples (4x4) are almost black
  for (uint32_t y = 1; y < 5; y++)
    for (uint32_t x = 1; x < 5; x++)
      if ((p[(h * y / 5) * line + w * x / 5] & 0xF0F0F0) != 0) return 0;
  return 1;
}

// Covered: something drawn over the game that is not in the record (the PS button dialog, system notices). Each frame,
// 64 points of the normal image are compared with the surface drawn by the Vita; if over half differ for 6 frames in a
// row, the normal image is shown and the Vita keeps drawing behind to compare; after 10 frames in a row almost equal
// (10 points different or fewer), the Vita drawing returns.
// cur_hd: surface cur has a Vita drawing (not a copy)
static uint32_t cur_hd, surf_dif, surf_ok, covered;
static int diffBlocks(const uint32_t *pj, const uint32_t *ps) {  // different points (of 64; tolerance 40 per channel)
  int k = 0;
  for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++) {
      uint32_t x = 15 + 60 * i, y = 8 + 33 * j, a = pj[y * 512 + x], b = ps[2 * y * LINE + 2 * x];
      for (int c = 0; c < 24; c += 8) {
        int d = (int)(a >> c & 255) - (int)(b >> c & 255);
        if (d > 40 || d < -40) { k++; break; }
      }
    }
  return k;
}

// Hook on sceGxmSetFragmentTexture: recognizes psp_tex by its size (512x272) and, when Adrenaline uses it, passes it the
// surface drawn in the previous frame; without a surface, our own copy of this frame (psp_tex is no longer updated).
// used = scene in which psp_tex was used (+1).
static int hookTex(SceGxmContext *c, unsigned int i, const SceGxmTexture *t) {
  if (t && !psp_tex && sceGxmTextureGetWidth(t) == 512 && sceGxmTextureGetHeight(t) == 272) { psp_tex = t; psp_data = sceGxmTextureGetData(t); }
  if (t && t == psp_tex) {
    used = fr + 1;
    if (ready && drawn == fr && !(covered && copy_fr == fr)) { t = &surf_tex[cur]; n_swap++; }
    else if (copy_fr == fr) t = &itex[ki];  // (no surface: this frame's copy, psp_tex is no longer updated)
  }
  return TAI_CONTINUE(int, ref_tex, c, i, t);
}

#include "dump.c"  // dumps (L+R+SELECT and automatic)
#include "replay.c"  // draw a dumped frame again

static uint32_t g_adr[16];  // bits: words of the notification region used by Adrenaline (to avoid using the same one)
// marks the word of notification n as used by Adrenaline
static void gAdr(const SceGxmNotification *n) {
  volatile uint32_t *nr = sceGxmGetNotificationRegion();
  uint32_t i = n && nr ? (uint32_t)(n->address - nr) : 512;
  if (i < 512) g_adr[i >> 5] |= 1u << (i & 31);
}
// Hook on sceGxmEndScene: after each Adrenaline scene that used the PSP image, in its thread and context, prepares the
// surface not being shown (surf[s]) for the next frame: playback of a dump, the last complete frame of the record drawn
// by the translator, a repeat of the previous surface or, otherwise, the PSP image stretched to double size with the menu
// on top. Also: covered check, different-frames check, dump and statistics. If Adrenaline did not use the PSP image (its
// menu, PS1, Original filter), nothing.
// fr counts Adrenaline's scenes.
static int hookEnd(SceGxmContext *c, const SceGxmNotification *v, const SceGxmNotification *f) {
  gAdr(v); gAdr(f);
  f = cAdr(f);  // (timer: our own end notification if it has none)
  cNote(C_ADR, f, used == fr + 1, 0, 0);
#ifdef NP_DEV
  static uint32_t tper;
  uint32_t tp = PT();
  if (tp) nd_gfx_uid = sceKernelGetThreadId();
  if (tp && used == fr + 1) { if (tper) pAdd(P_A_PERIOD, tp - tper); tper = tp; }
#endif
  int r = TAI_CONTINUE(int, ref_end, c, v, f);
  DEV(PA(P_A_END, tp);)
  n_us += used == fr + 1;
  static uint32_t fq[2];  // last scene of ours of the previous frame and the one before
  if (ACTIVE && used == fr + 1) {  // in this scene Adrenaline drew the PSP image: we pass it through the surface
    uint32_t t0 = now();
    if (gq_done) {
      uint32_t a = gq - *gq_done; g_an++; g_asum += a; if (a > g_amax) g_amax = a;
      g_f1 += gBusy(fq[0]); g_f2 += gBusy(fq[1]);
    }
    const SceGxmTexture *src = psp_tex;  // this frame's image: our own copy if there was one, otherwise psp_tex
    if (copy_fr == fr) {
      src = &itex[ki];
      sceGxmTextureSetMinFilter(&itex[ki], sceGxmTextureGetMinFilter(psp_tex));  // (the filters Adrenaline chose)
      sceGxmTextureSetMagFilter(&itex[ki], sceGxmTextureGetMagFilter(psp_tex));
    }
    const uint32_t *pj = sceGxmTextureGetData(src);
    // GPU behind: if the scene from 2 frames ago is unfinished, no other is sent; the previous surface is repeated
    if (gBusy(fq[1]) && drawn == fr && dump_on != 1) { g_behind++; DEV(if (p_on) pAdd(P_F_BEHIND, 0);) drawn = fr + 1; goto finish; }
    if (dump_on == 1 || dump_on == 3) dumpFrame(c, pj, drawn == fr && !(covered && copy_fr == fr));  // dump
    int nj = isBlack(pj, 512, 480, 272), np = drawn == fr && isBlack(surf[cur], LINE, W, H);  // np: previous frame
    n_black_j += nj; n_black_p += np;
    // The surface being shown (surf[cur]) is only read with the GPU finished: half-drawn it is no use for comparing
    int surf_ready = !gBusy(fq[0]);
    if (t_on && draw_on && drawn == fr && cur_hd && !surf_ready) g_skip++;
    if (t_on && draw_on && drawn == fr && cur_hd && surf_ready) {  // covered: normal image differs from the Vita drawing
      int k = diffBlocks(pj, surf[cur]);
      if (k >= 32) { surf_ok = 0; if (++surf_dif >= 6 && !covered) { covered = 1; } }
      else { surf_dif = 0; if (k <= 10 && covered && ++surf_ok >= 10) covered = 0; }
    }
    // Check (1 of every 4 frames): blocks of the Vita drawing different from the normal image (dump.c); 6 to 8 or
    // more = different frame. 255 or more (half of the 510 blocks) is something on top of the image, not a drawing
    // error: not counted.
    if (t_on && draw_on && drawn == fr && cur_hd && surf_ready && !covered && !(fr & 3)) {
      uint32_t t1 = now();
      int n = dump_auto ? dumpBad(surf[cur], 0) : dumpDiff(pj, surf[cur]);
      n_chk++; if (n >= (dump_auto ? 6 : 8) && n < 255) n_bad++;
      g_chk_us += now() - t1;
    }
    DEV(if (p_on) pAdd(P_A_PRE, now() - t0);)
    int s = cur ^ 1, hd = 0;
    if (draw_on && t_on && !menu && rep_on == 2 && repDraw(c, s, src)) { hd = 1; t_frame = ~0u; }  // playback
    else if (draw_on && t_on && !menu) {  // the last complete frame of the record, drawn by the Vita
      RFrame *rg = regTake();
      int cause = T_NEW;
      if (!rg) { t_cnt[T_NEW]++; DEV(if (p_on) pAdd(P_F_NOREC, 0);) }
      else if (rg->frame == t_frame) {  // same frame: the surface is repeated up to 2 times (30 fps); otherwise, normal
        if (t_ok && t_rep < 2) { t_rep++; hd = 2; cause = 0; t_skip[0]++; DEV(if (p_on) pAdd(P_F_REPEAT, 0);) } else t_cnt[T_NEW]++;
      } else {
        if (t_frame != ~0u && rg->frame > t_frame) { uint32_t g = rg->frame - t_frame; t_skip[g < 5 ? g : 5]++; }
        // The Vita drawing shows one frame behind the normal image, so an HD frame right after one that was not
        // drawable looks like a jump back: that frame is only checked (held back) and HD resumes with the next one
        uint32_t t1 = now(), dp, od;
        int chk = !t_can && t_frame != ~0u, r = chk ? trCheckFrame(rg, &dp, &od) : trFrame(c, rg, s, src);
        if (chk && !r) r = T_HOLD;
        g_tr_us += now() - t1; t_cnt[r]++; t_frame = rg->frame; t_can = !r || r == T_HOLD; t_ok = !r; hd = !r; t_rep = 0; cause = r;
        DEV(trReason(rg->frame, r); if (p_on) pAdd(r ? P_F_NORMAL : P_F_DRAWN, 0);)
        if (!r) regGpu(gq, gq_done);  // the scene that reads this record's textures
      }
      if (dump_on == 1 || dump_on == 3) dumpText(rg, hd == 2 ? "repeats the Vita drawing of the previous frame" : hd ? "Vita drawing" :
                                "normal image (copy of the PSP image)", cause);
      regRelease();
    } else if (dump_on == 1 || dump_on == 3) dumpText(0, menu ? "normal image with the menu" : "normal image (no draw or no translator)", -1);
    // hd: 2 keeps the previous surface, 1 drawn by the translator, 0 the PSP image at double size (below)
    if (hd == 2) { drawn = fr + 1; t_hd++; goto finish; }
    if (hd) { t_hd++; cur_hd = 1; goto ready_hd; }
    t_nat++; cur_hd = 0;
    sceGxmBeginScene(c, 0, rt, 0, 0, 0, &cs[s], &ds);
    sceGxmSetVertexProgram(c, vp);
    sceGxmSetFragmentProgram(c, fp);
    void *u;
    sceGxmReserveVertexDefaultUniformBuffer(c, &u);
    // Pixel-to-screen orthographic, stored like vita2d's (matrix_init_orthographic: by columns, the offset in [12] and
    // [13]; the shader does mul(pos, wvp)).
    static const float wvp[16] = { 2.0f / LINE, 0, 0, 0, 0, -2.0f / H, 0, 0, 0, 0, -2, 0, -1, 1, 1, 1 };
    sceGxmSetUniformDataF(u, p_wvp, 0, 16, wvp);
    sceGxmSetFragmentTexture(c, 0, src);
    sceGxmSetVertexStream(c, 0, vb);
    sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
    if (menu && painted) {  // the menu quad on top
      sceGxmSetFragmentProgram(c, fpb);
      sceGxmSetFragmentTexture(c, 0, &mtex);
      sceGxmSetVertexStream(c, 0, vb + 20);
      sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
    }
    sceGxmEndScene(c, 0, gEnd());
    cNote(C_NORMAL, &gq_n, menu && painted ? 2 : 1, W * H / 1000, 0);
  ready_hd:
    if (src == &itex[ki]) img_q[ki] = gq;  // scene that reads that copy
    fq[1] = fq[0]; fq[0] = gq;
    cur = s; drawn = fr + 1; n_pres++;
    uint32_t d = now() - t0;
    DEV(if (p_on) pAdd(P_A_TOTAL, d);)
    us_sum += d;
    if (d > us_max) us_max = d;
  }
finish:
  fr++;
  return r;
}

// --- Who draws the screen (for the PS button error) ---
// Hooks that only count: scenes and frames sent to the screen by Adrenaline (AdrenalineUser) and by the system emulator
// (ScePspemu). Every second, presThread builds the state (who drew, whether a system dialog is open, Vita surface or
// normal image, covered) and logs it when it changes and for the next 3 seconds.
static tai_hook_ref_t ref_aq, ref_afb, ref_pend, ref_pq, ref_pfb;
static SceUID hk_aq = -1, hk_afb = -1, hk_pend = -1, hk_pq = -1, hk_pfb = -1;
static volatile uint32_t n_aq, n_afb, n_pend, n_pq, n_pfb;
static int hookAq(void *a, void *b, const void *d) { n_aq++; return TAI_CONTINUE(int, ref_aq, a, b, d); }
static int hookAfb(const void *p, int s) { n_afb++; return TAI_CONTINUE(int, ref_afb, p, s); }
static int hookPend(SceGxmContext *c, const SceGxmNotification *v, const SceGxmNotification *f) { n_pend++; return TAI_CONTINUE(int, ref_pend, c, v, f); }
static int hookPq(void *a, void *b, const void *d) { n_pq++; return TAI_CONTINUE(int, ref_pq, a, b, d); }
static int hookPfb(const void *p, int s) { n_pfb++; return TAI_CONTINUE(int, ref_pfb, p, s); }
static int (*dlgRunning)(void);  // sceCommonDialogIsRunning (with this, Adrenaline stops drawing)
// every 1 s (called by presThread): logs the state if it changed and for the next 3 s (300 lines at most)
static void screenLog(void) {
  static uint32_t a0, u0, aq0, afb0, p0, pq0, pfb0, sig0 = ~0u, extra, nlines;
  uint32_t a = fr - a0, u = n_us - u0, aq = n_aq - aq0, afb = n_afb - afb0, pe = n_pend - p0, pq = n_pq - pq0, pfb = n_pfb - pfb0;
  a0 = fr; u0 = n_us; aq0 = n_aq; afb0 = n_afb; p0 = n_pend; pq0 = n_pq; pfb0 = n_pfb;
  int dl = dlgRunning ? dlgRunning() : -1;
  uint32_t sig = (a > 0) | (u > 0) << 1 | (aq > 0) << 2 | (pe > 0) << 3 | (pq > 0 || pfb > 0) << 4 | (dl > 0) << 5 | covered << 6 |
                 cur_hd << 7 | menu << 8;
  if (sig != sig0) extra = 4;
  sig0 = sig;
  if (!extra || nlines >= 300) return;
  extra--; nlines++;
  alog("screen: Adrenaline scenes %u (with the PSP image %u), to the screen %u (buffer changes %u) | ScePspemu scenes %u, "
       "to the screen %u (buffer changes %u) | system dialog %s | surface: %s%s%s\n", a, u, aq, afb, pe, pq, pfb,
       dl < 0 ? "?" : dl ? "open" : "no", cur_hd ? "Vita drawing" : "normal image", covered ? ", covered" : "",
       menu ? ", menu L+R+START" : "");
}

// GPU notifications (for the PS button error): every 50 ms the 512 words of the region are compared with the previous
// time. The first time a word that is not ours changes it is logged (who else uses it: Adrenaline, the emulator, the
// system dialog). For ours (gq_slot, c_slot) it logs if someone wrote another value: they can only rise and never
// exceed what we sent.
static void notifs(volatile uint32_t *nr) {
  static uint32_t prior[512], viewed[16], found, n_log, bad_g DEV(, bad_c), ug DEV(, uc);
  if (!nr) return;
  for (uint32_t i = 0; i < 512; i++) {
    uint32_t v = nr[i];
    if (found && v != prior[i] && i != gq_slot && !(c_on && i == c_slot) && !(viewed[i >> 5] >> (i & 31) & 1) && n_log < 60) {
      viewed[i >> 5] |= 1u << (i & 31); n_log++;
      alog("notifications: word %u is used by another (%08x -> %08x)\n", i, prior[i], v);
    }
    prior[i] = v;
  }
  found = 1;
  if (gq_done) {  // ours: does not go down or exceed gq
    uint32_t v = *gq_done;
    if (((int)(v - ug) < 0 || (int)(v - gq) > 0) && bad_g++ < 20) alog("notifications: word %u (ours) has %08x: last seen %08x, last sent %08x\n", gq_slot, v, ug, gq);
    ug = v;
  }
#ifdef NP_DEV
  if (c_on) {
    uint32_t v = *c_adr.address;
    if (((int)(v - uc) < 0 || (int)(v - c_seq) > 0) && bad_c++ < 20) alog("notifications: word %u (the timer's) has %08x: last seen %08x, last sent %08x\n", c_slot, v, uc, c_seq);
    uc = v;
  }
#endif
}

// --- L+R+START menu ---
static tai_hook_ref_t ref_ctrl;
static SceUID hk_ctrl = -1;
static uint32_t combo0;
// Hook on sceCompatWriteSharedCtrl (ScePspemu): the buttons the Vita passes to the PSP. L+R+SELECT requests a dump;
// L+R+START opens or closes the menu (only while the PSP image is being seen). With the menu open or a combination
// held, the game receives "nothing pressed".
static int hookCtrl(SceCtrlDataPsp *p) {
  if (p && ready) {
    static uint32_t cv0;  // L+R+SELECT (0x301): dump
    uint32_t cv = (p->Buttons & 0x301) == 0x301;
    if (cv && !cv0 && (!dump_on || dump_on == 3) && !dump_req) dump_req = 1;
    cv0 = cv;
    if (cv) { p->Buttons = 0; p->Lx = p->Ly = p->Rx = p->Ry = 128; }
    uint32_t combo = (p->Buttons & 0x308) == 0x308;  // L (0x100) + R (0x200) + START (0x8)
    if (combo && !combo0 && (menu || used + 10 > fr)) { menu ^= 1; painted = 0; }  // (only if the PSP image is seen)
    combo0 = combo;
    if (menu || combo) { p->Buttons = 0; p->Lx = p->Ly = p->Rx = p->Ry = 128; }  // the game sees nothing pressed
  }
  return TAI_CONTINUE(int, ref_ctrl, p);
}
const char *nlVersion(void);  // nativedraw.c: NativeLink version (from its log), 0 if not loaded

// writes s to the menu texture at (x, y) with the installer's 8x16 font (clips at the right edge)
static void drawText(int x, int y, const char *s, uint32_t col) {
  for (; *s && x + 8 <= MW; s++, x += 8)
    if (*s > 32 && *s < 127)
      for (int r = 0; r < 16; r++)
        for (int b = 0; b < 8; b++)
          if (font8x16[*s - 32][r] & (0x80 >> b)) mpix[(y + r) * MW + x + b] = col;
}
// draws the menu to its texture: semitransparent background with white border, title and versions (colors 0xAABBGGRR)
static void paintMenu(void) {
  for (int y = 0; y < MH; y++)
    for (int x = 0; x < MW; x++)
      mpix[y * MW + x] = x < 2 || y < 2 || x >= MW - 2 || y >= MH - 2 ? 0xFFFFFFFFu : 0xD8201008u;
  const char *nl = nlVersion();
  char b[40];
  drawText(16, 10, "NativePatch", 0xFF40C8FFu);
  sceClibSnprintf(b, sizeof b, "NativeLink  %s", nl ? nl : "not loaded");
  drawText(16, 38, b, 0xFFFFFFFFu);
  drawText(16, 58, "NativeDraw  " NATIVEDRAW_VERSION, 0xFFFFFFFFu);
  drawText(16, 102, "L+R+START: close", 0xFFA0A0A0u);
}

// Presenter thread: hooks the Adrenaline functions (retries for up to 60 s), waits 30 scenes, creates programs,
// destination and translator, picks the scene-end word and marks ready. Then, every 50 ms: paints the menu, serves
// dumps and playback and logs changes; every 10 s, the statistics.
static int presThread(SceSize args, void *argp) {
  for (int i = 0; i < 600 && (hk_end < 0 || hk_tex < 0); i++) {  // AdrenalineUser loads after NativeDraw
    if (hk_end < 0) hk_end = taiHookFunctionImport(&ref_end, "AdrenalineUser", 0xF76B66BD, 0xFE300E2F, hookEnd);
    if (hk_tex < 0) hk_tex = taiHookFunctionImport(&ref_tex, "AdrenalineUser", 0xF76B66BD, 0x29C34DF5, hookTex);
    if (hk_dma < 0) hk_dma = taiHookFunctionImport(&ref_dma, "AdrenalineUser", 0xA9E5B2F0, 0x9B2FF739, hookDma);
    if (hk_ctrl < 0) hk_ctrl = taiHookFunctionImport(&ref_ctrl, "ScePspemu", 0x0F35909D, 0x2306FFED, hookCtrl);
    if (hk_end < 0 || hk_tex < 0) sceKernelDelayThread(100000);
  }
  alog("presenter: hooks sceGxmEndScene %08x sceGxmSetFragmentTexture %08x sceDmacMemcpy %08x sceCompatWriteSharedCtrl %08x\n",
       hk_end, hk_tex, hk_dma, hk_ctrl);
  // only count (who draws the screen): sceGxmDisplayQueueAddEntry and sceDisplaySetFrameBuf of both and
  // sceGxmEndScene of ScePspemu
  hk_aq = taiHookFunctionImport(&ref_aq, "AdrenalineUser", 0xF76B66BD, 0xEC5C26B5, hookAq);
  hk_afb = taiHookFunctionImport(&ref_afb, "AdrenalineUser", 0x4FAACD11, 0x7A410B64, hookAfb);
  hk_pend = taiHookFunctionImport(&ref_pend, "ScePspemu", 0xF76B66BD, 0xFE300E2F, hookPend);
  hk_pq = taiHookFunctionImport(&ref_pq, "ScePspemu", 0xF76B66BD, 0xEC5C26B5, hookPq);
  hk_pfb = taiHookFunctionImport(&ref_pfb, "ScePspemu", 0x4FAACD11, 0x7A410B64, hookPfb);
  int rd = taiGetModuleExportFunc("SceCommonDialog", 0xE537816C, 0x043A353E, (uintptr_t *)&dlgRunning);
  alog("screen: hooks Adrenaline queue %08x buffer %08x | ScePspemu scenes %08x queue %08x buffer %08x | "
       "sceCommonDialogIsRunning %08x\n", hk_aq, hk_afb, hk_pend, hk_pq, hk_pfb, rd);
  if (rd < 0) dlgRunning = 0;
  if (hk_end < 0 || hk_tex < 0) return sceKernelExitDeleteThread(0);
  while (fr < 30) sceKernelDelayThread(100000);  // GXM already initialized by Adrenaline
  int r1 = initProgs(), r2 = r1 >= 0 ? initDest() : 0;
  t_on = r1 >= 0 && r2 >= 0 && draw_on && trInit() >= 0;  // translator: only with draw
  alog("presenter: programs %08x, target %08x (surfaces in %s and %s) | free afterwards (MB in one go): CDRAM %u, "
       "LPDDR physical %u, LPDDR %u\n", r1, r2, ty(surf[0]), ty(surf[1]), probe(CDRAM), probe(PHY), probe(UNC));
  if (r1 < 0 || r2 < 0) return sceKernelExitDeleteThread(0);
  volatile uint32_t *nr = sceGxmGetNotificationRegion();  // scene end: the last word Adrenaline does not use
  char ua[80]; int ka = 0;
  for (uint32_t i = 0; i < 512; i++) if (g_adr[i >> 5] >> (i & 31) & 1 && ka < 70) ka += sceClibSnprintf(ua + ka, sizeof ua - ka, " %u", i);
  ua[ka] = 0;
  for (gq_slot = 511; gq_slot > 400 && (g_adr[gq_slot >> 5] >> (gq_slot & 31) & 1); gq_slot--) {}
  if (nr) { gq_done = nr + gq_slot; *gq_done = 0; gq = 0; }
  alog("gpu: end of scenes at notification %u (%p); Adrenaline uses:%s\n", gq_slot, gq_done, ka ? ua : " none");
  DEV(if (c_wanted) cStart(nr, g_adr);)
  ready = 1;
  for (uint32_t t = now(), seen = 0, k = 0, warned = 0, m0 = 0;;) {  // every 10 s: how many frames and what they cost
    sceKernelDelayThread(50000);
    if (menu && !painted) { paintMenu(); __sync_synchronize(); painted = 1; }
    if (!(k % 20)) screenLog();
    notifs(nr);
#ifdef NP_DEV
    static uint32_t ln0, vl;  // slow scenes already logged and dumps requested because of them
    while (ln0 != c_ln) {
      uint32_t j = ln0++ % CL;
      alog("gpu: slow scene: %s, %u draws, %u thousand pixels, %u.%u ms of GPU (%u.%u ms in queue)\n", cnames[c_slow[j].ty], c_slow[j].dr,
           c_slow[j].px, c_slow[j].us / 1000, c_slow[j].us / 100 % 10, c_slow[j].queue / 1000, c_slow[j].queue / 100 % 10);
      if (vl < 3 && dump_level == 4 && !dump_req && (!dump_on || dump_on == 3)) { vl++; dump_req = 1; alog("dump: requested by a slow scene (%u of 3)\n", vl); }
    }
#endif
    dumpTick();
    repTick();
    if (menu != m0) { m0 = menu; alog("presenter: menu %s\n", m0 ? "open" : "closed"); }
    static uint32_t tp0;
    if (covered != tp0) { tp0 = covered; alog("translator: %s\n", tp0 ? "something covers the image (not in the record): normal image" : "the Vita drawing is back"); }
    if (warned < 2 && n_pres >= (warned ? 60 : 1)) alog("presenter: %s\n", warned++ ? "60 frames drawn" : "first frame drawn");
    if (++k < 200) continue;
    k = 0;
    DEV(alog("clocks: GPU %d MHz (crossbar %d), CPU %d, bus %d\n", scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency(),
             scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency());)
    uint32_t p = n_pres, m = us_max, s = us_sum, dt = (now() - t) / 1000, nd = n_dma, nr = n_chk, nm = n_bad;
    n_pres = 0; n_dma = 0; us_sum = 0; us_max = 0; t = now(); n_chk = n_bad = 0;
    if (psp_tex && !seen++)
      alog("presenter: game texture %p (512x272, format %08x, filters %x/%x)\n", psp_tex,
           sceGxmTextureGetFormat(psp_tex), sceGxmTextureGetMinFilter(psp_tex), sceGxmTextureGetMagFilter(psp_tex));
    if (t_on) {
      static uint32_t dr0;  // (t_draws is not cleared: the translator uses it mid-frame for the timer)
      uint32_t h = t_hd, nn = t_nat, dr = t_draws - dr0; t_hd = t_nat = 0; dr0 += dr;
      char b[200]; int k = 0;
      for (int i = 1; i < TN; i++) if (t_cnt[i]) { k += sceClibSnprintf(b + k, sizeof b - k, " %s=%u", tnames[i], t_cnt[i]); t_cnt[i] = 0; }
      b[k] = 0;
      alog("translator: HD frames %u, normal image %u%s%s | draws drawn %u\n", h, nn, k ? " | reasons:" : "", b, dr);
      DEV(if (h) alog("translator: us per HD frame: all %u | check %u, textures %u, vertices %u, GXM %u, scene changes %u\n", t_us[5] / h, t_us[0] / h, t_us[1] / h, t_us[2] / h, t_us[3] / h, t_us[4] / h);
          for (int i = 0; i < 6; i++) t_us[i] = 0;)
      // Pacing: for each Adrenaline frame drawn by the Vita, how many PSP frames advanced since the previous one
      // (1 = none skipped; 0 = repeated the previous one). With skips or repeats, what a game draws every other
      // frame flashes.
      alog("translator: pace (PSP frames advanced per drawn frame): 0 (repeated) %u, 1 %u, 2 %u, 3 %u, 4 %u, 5 or more %u\n",
           t_skip[0], t_skip[1], t_skip[2], t_skip[3], t_skip[4], t_skip[5]);
      sceClibMemset((void *)t_skip, 0, sizeof t_skip);
    }
    if (gq_done) {  // scene end on the GPU
      uint32_t an = g_an, as = g_asum, am = g_amax, f1 = g_f1, f2 = g_f2, ps = g_psurf, pi = g_pimg;
      uint32_t at = g_behind, wait_t = g_wait_us, sl = g_skip, rv = g_chk_us, tr = g_tr_us;
      g_an = g_asum = g_amax = g_f1 = g_f2 = g_psurf = g_pimg = g_behind = g_wait_us = g_skip = g_chk_us = g_tr_us = 0;
      alog("gpu: at each frame start, our unfinished scenes: average %u.%u, max %u | unfinished: previous frame %u, "
           "2 frames ago %u | rewritten with the GPU unfinished: vertices and palettes %u, normal image %u (record textures: "
           "in nativedraw_capture.log)\n", an ? as / an : 0, an ? as * 10 / an % 10 : 0, am, f1, f2, ps, pi);
      alog("gpu: frames not sent because the GPU was behind %u, waits for the GPU %u ms | CPU in 10 s: translator %u ms, "
           "different-frame check %u ms | surface checks skipped (GPU unfinished) %u\n",
           at, wait_t / 1000, tr / 1000, rv / 1000, sl);
      DEV(if (c_on) cReport(dt);)
    }
    alog("presenter: %u frames in %u ms through the surface (changes %u total, %u with own copy), CPU %u us/frame (max %u) | "
         "nearly black: game %u, surface %u | checked %u, different from the normal image %u%s\n", p, dt, n_swap, nd, p ? s / p : 0, m, n_black_j, n_black_p, nr, nm,
         p ? "" : psp_tex ? " (Adrenaline did not use the PSP image: menu, PS1 or Original filter)" : " (no game texture yet)");
  }
  return 0;
}

static int swOn(const char *p) {  // the file exists and does not start with "no" (like capture.c)
  char b[2] = { 0, 0 };
  SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0);
  if (fd < 0) return 0;
  sceIoRead(fd, b, 2); sceIoClose(fd);
  return !(b[0] == 'n' && b[1] == 'o');
}

// Called by nativedraw.c on load: reads the switches (draw, replay, dump), decides how many PSP image copies
// are needed and starts the presenter thread.
void presStart(void) {
  draw_on = swOn("ux0:data/nativedraw/draw");
  DEV(c_wanted = swOn("ux0:data/nativedraw/gpu_timer");)
  repStart();
  dump_level = dumpLevel();  // dumps: off, screenshot, frame, sequence or auto
  if (dump_level == 4 && !draw_on) dump_level = 3;  // (auto needs the Vita drawing to compare)
  dump_auto = dump_level == 4; dump_nf = dump_level <= 2 ? 1 : VN;
  alog("dump: level %s (ux0:data/nativedraw/dump: off, screenshot, frame, sequence or auto)\n", dump_names[dump_level]);
  alog("presenter: %s; menu with L+R+START\n", draw_on ? "always replaces the image (draw)" :
       "replaces the image only with the menu open (create ux0:data/nativedraw/draw for always)");
  SceUID th = sceKernelCreateThread("nativedraw_presenter", presThread, 0xA0, 0x2000, 0, 0, 0);
  if (th >= 0) sceKernelStartThread(th, 0, 0);
}
// Called by nativedraw.c on unload: releases the hooks.
void presStop(void) {
  if (hk_end >= 0) taiHookRelease(hk_end, ref_end);
  if (hk_tex >= 0) taiHookRelease(hk_tex, ref_tex);
  if (hk_dma >= 0) taiHookRelease(hk_dma, ref_dma);
  if (hk_ctrl >= 0) taiHookRelease(hk_ctrl, ref_ctrl);
  if (hk_aq >= 0) taiHookRelease(hk_aq, ref_aq);
  if (hk_afb >= 0) taiHookRelease(hk_afb, ref_afb);
  if (hk_pend >= 0) taiHookRelease(hk_pend, ref_pend);
  if (hk_pq >= 0) taiHookRelease(hk_pq, ref_pq);
  if (hk_pfb >= 0) taiHookRelease(hk_pfb, ref_pfb);
}
