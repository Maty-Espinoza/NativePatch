// dump.c: frame dumps to debug what is seen on screen. Included inside presenter.c: the Adrenaline thread
// (hookEnd) calls dumpText and dumpFrame to capture each frame; the presenter thread calls dumpTick
// to build the dump (L+R+SELECT or automatic) and write it to ux0:data/nativedraw/dumps/NNN/. Saves per frame
// the PSP image, the Vita image, a text with how it was made and a copy of the frame record (record.h) for
// tools/dump.py and replay.c. Level in ux0:data/nativedraw/dump (dumpLevel).
#include <psp2/io/stat.h>
void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
#define VN 3            // frames per dump (at most)
#define VL 256          // line of each frame (how the surface was made)
#define VB_N (2 << 20)  // record per frame
// dump_req: L+R+SELECT request; dump_on: 0 nothing, 3 armed (auto), 1 capturing, 2 to write; dump_k: frames ready
static volatile uint32_t dump_req, dump_on, dump_k;
extern volatile uint32_t nd_exact_until;  // (capture.c) until when the record carries the exact PSP image
DEV(extern volatile uint32_t nd_dump_mark;)  // (capture.c) the running program asked for a dump
// whether automatic; dumps made; time of the last; bad blocks of the one that triggered the automatic; request time
static uint32_t dump_auto, dump_n, dump_t, dump_dif, dump_t0;
// Level (0 off, 1 screenshot, 2 frame, 3 sequence, 4 auto) and frames per dump
static uint32_t dump_level = 3, dump_nf = VN;
static const char *dump_names[5] = { "off", "screenshot", "frame", "sequence", "auto" };
// Dump level: the word in ux0:data/nativedraw/dump (3 letters are compared; "no" = disabled); without
// file, 3 (sequence).
int dumpLevel(void) {
  char b[16] = { 0 };
  SceUID fd = sceIoOpen("ux0:data/nativedraw/dump", SCE_O_RDONLY, 0);
  if (fd < 0) return 3;
  sceIoRead(fd, b, 15); sceIoClose(fd);
  for (int i = 0; i < 5; i++) if (!sceClibStrncmp(b, dump_names[i], 3)) return i;
  return b[0] == 'n' ? 0 : 3;  // ("no": disabled)
}
// Memory of each captured frame: dump_hd Vita surface, dump_ps PSP image, dump_tx its line, dump_ok whether there was a
// surface. dump_w: cached memory for writing (192 KB). dump_last: the line of the last frame made (paired with the
// next one Adrenaline shows). dump_rt: intermediate image 0 in f0 (dump_rtfb: its address; ~0u: none).
static uint8_t *dump_hd[VN], *dump_ps[VN], *dump_w;
static char dump_tx[VN][VL], dump_last[VL];
static uint32_t dump_ok[VN], dump_lastn, dump_mem, dump_rtfb;
static uint8_t *dump_rt; static SceUID dump_rtblk;
static SceUID dump_blk[VN + 1], dump_bblk[VN];
static uint8_t *dump_bb[VN];                 // blocks for records
static uint32_t dump_bbn[VN], dump_bpend = VN;  // bytes in each; that of the last frame made (VN: none)
static int dump_bf[VN] = { VN, VN, VN };     // record block of each captured frame (VN: no record)
// 1 if record block b already belongs to a captured frame
static int dumpUsed(int b) { for (int i = 0; i < (int)dump_k; i++) if (dump_bf[i] == b) return 1; return 0; }
// Copies record f to b. Format: a header of 16 words and then each RFrame array aligned to 4 (read by replay.c and
// tools/dump.py; if it changes, change both). Textures in the texture cache go after tx, so it stands alone. Returns the
// bytes, 0 if it does not fit in VB_N.
static uint32_t recBin(RFrame *f, uint8_t *b) {
  static uint32_t to[TN_N];
  uint32_t ntx = f->ntx;
  for (uint32_t i = 0; i < f->nt; i++) {
    to[i] = f->to[i];
    if (f->tc[i]) { to[i] = ntx; ntx += ((f->tn[i] & 0x7FFFFFFF) + 63) & ~63u; }
  }
  uint32_t na = (f->na + 15) & ~15u, vx = TX_N - f->nvx;  // (vertices in tx go after area a)
  if (na + f->nvx > RA_N) return 0;
  uint32_t h[16] = { 0x47524E44, 4, f->frame, f->fb, f->fbw, f->nd, f->ne, na + f->nvx, f->flags, f->nt, ntx, f->mir, f->mirf, NRS, sizeof(RDraw), f->nimg }, o = 0;
#define PUT(p, n) do { uint32_t n_ = (n); if (o + n_ > VB_N) return 0; sceClibMemcpy(b + o, p, n_); o += (n_ + 3) & ~3u; } while (0)
  PUT(h, sizeof h); PUT(RS, NRS);
  PUT(f->ta, f->nt * 4); PUT(f->tn, f->nt * 4); PUT(to, f->nt * 4); PUT(f->tq, f->nt * 4); PUT(f->tm, f->nt * 8);
  RDraw *dd = (RDraw *)(b + o);
  PUT(f->d, f->nd * sizeof(RDraw)); PUT(f->e, f->ne * NRS * 4); PUT(f->a, na); PUT(f->tx + vx, f->nvx);
  for (uint32_t j = 0; j < f->nd; j++) if (dd[j].voff & VX) dd[j].voff = na + (dd[j].voff & ~VX) - vx + 1;
#undef PUT
  if (o + ntx > VB_N) return 0;
  sceDmacMemcpy(b + o, f->tx, f->ntx);
  for (uint32_t i = 0; i < f->nt; i++) {
    if (!f->tc[i]) continue;
    sceDmacMemcpy(b + o + to[i], f->tx + f->to[i], f->tn[i] & 0x7FFFFFFF);
    for (uint32_t j = 0; j < f->nd; j++) if (dd[j].toff == f->to[i] + 1) dd[j].toff = to[i] + 1;
  }
  return o + ntx;
}
// In the Adrenaline thread: copies record f to a free block (taken by the coming frame, dumpFrame); if it does not fit,
// the frame stays without a record.
static void dumpBin(RFrame *f) {
  dump_bpend = VN;
  int k = 0;
  while (k < VN && (!dump_bb[k] || dumpUsed(k))) k++;
  if (k == VN || !f) return;
  uint32_t n = recBin(f, dump_bb[k]);
  if (n) { dump_bbn[k] = n; dump_bpend = k; }
}
#ifdef NP_DEV
// Failed frames: when a draw of a frame cannot be done, that same frame is still in memory, so it is saved at once and
// without asking, to ux0:data/nativedraw/fails/NNN_reason/ (record, normal image and a report of the draw). Memory is
// reserved at start. One frame per kind of failure, at most FL_R per reason.
#define FL_R 4
static void bmp(const char *p, const uint8_t *px, uint32_t w, uint32_t h, uint32_t stride);
static uint8_t *fl_b, *fl_img, fl_row[480 * 16 * 4];
static uint32_t fl_n, fl_sig[64], fl_nsig, fl_nr[TN], fl_lent;
static volatile uint32_t fl_busy;
static int fl_r;
static char fl_t[2048];
static void failInit(void) {
  void *p = 0;
  SceUID b = sceKernelAllocMemBlock("nativedraw_fail", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, VB_N, 0);
  if (b >= 0 && sceKernelGetMemBlockBase(b, &p) >= 0) fl_b = p;
  p = 0;
  b = sceKernelAllocMemBlock("nativedraw_fail_img", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, (512 * 272 * 4 + 0x3FFFF) & ~0x3FFFFu, 0);
  if (b >= 0 && sceKernelGetMemBlockBase(b, &p) >= 0) fl_img = p;
  alog("fail: frames saved on failure: %s, normal image %s\n", fl_b ? "yes" : "no memory", fl_img ? "yes" : "no memory");
}
// (Adrenaline thread) Draw i of frame f failed with reason r: saves the frame if this kind of failure is new
static void failNote(RFrame *f, uint32_t i, int r) {
  if (!fl_b || fl_busy || fl_lent || fl_nsig >= 64 || fl_nr[r] >= FL_R) return;
  RDraw *d = &f->d[i];
  const uint32_t *e = f->e[d->st];
  uint32_t sig = (r * 16777619u ^ (RE(e, 0x9C) & 0x1FFFF0) ^ d->prim << 26 ^ (RE(e, 0xC3) & 15) << 22) * 16777619u ^ (!d->toff && hasTex(e, d)) << 21;
  for (uint32_t k = 0; k < fl_nsig; k++) if (fl_sig[k] == sig) return;
  if (!(fl_n = recBin(f, fl_b))) return;
  fl_sig[fl_nsig++] = sig; fl_nr[r]++; fl_r = r;
  if (fl_img) sceDmacMemcpy(fl_img, img[ki], 512 * 272 * 4);
  int n = sceClibSnprintf(fl_t, sizeof fl_t, "reason %s | frame %u, draw %u of %u | prim %u, vertices %u, vt %06x | target %06x/%u | texture %07x, has a copy %d\nregisters:",
                          tnames[r], f->frame, i, f->nd, d->prim, d->n, d->vt, RE(e, 0x9C) & 0xFFFFFF, RE(e, 0x9D) & 0x7FF, texAddr(e), d->toff != 0);
  for (int k = 0; k < NRS && n < (int)sizeof fl_t - 16; k++) n += sceClibSnprintf(fl_t + n, sizeof fl_t - n, " %02x=%06x", RS[k], e[k] & 0xFFFFFF);
  sceClibSnprintf(fl_t + n, sizeof fl_t - n, "\n");
  __sync_synchronize();
  fl_busy = 1;
}
// (presenter thread) Writes the saved frame, unless a dump is using the memory
static void failTick(void) {
  if (!fl_busy || dump_mem) return;
  char d[64], p[96];
  sceIoMkdir("ux0:data/nativedraw/fails", 0777);
  for (int n = 0; n < 1000; n++) { sceClibSnprintf(d, sizeof d, "ux0:data/nativedraw/fails/%03d_%s", n, tnames[fl_r]); if (sceIoMkdir(d, 0777) >= 0) break; }
  sceClibSnprintf(p, sizeof p, "%s/f0_record.bin", d);
  SceUID fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (fd >= 0) { sceIoWrite(fd, fl_b, fl_n); sceIoClose(fd); }
  sceClibSnprintf(p, sizeof p, "%s/f0.txt", d);
  if ((fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666)) >= 0) { sceIoWrite(fd, fl_t, sceClibStrnlen(fl_t, sizeof fl_t)); sceIoClose(fd); }
  if (fl_img) { sceClibSnprintf(p, sizeof p, "%s/f0_normal.bmp", d); uint8_t *w = dump_w; dump_w = fl_row; bmp(p, fl_img, 480, 272, 512); dump_w = w; }
  alog("fail: frame saved in %s\n", d);
  __sync_synchronize();
  fl_busy = 0;
}
#endif
static uint8_t *dump_nat;          // the exact PSP image of the last drawn frame (from the record); 0: none
static uint32_t dump_natf, dump_natw, dump_natok;  // format (0 5650, 1 5551, 2 4444, 3 8888), width, present
static SceUID dump_natblk;
static uint32_t dumpPx(const uint8_t *p, uint32_t f, uint32_t i) {  // pixel i of p in format f -> approximate 0xBBGGRR
  if (f == 3) return ((const uint32_t *)p)[i] & 0xFFFFFF;
  uint32_t c = ((const uint16_t *)p)[i];
  if (f == 0) return (c & 31) << 3 | (c >> 5 & 63) << 10 | (c >> 11 & 31) << 19;
  if (f == 1) return (c & 31) << 3 | (c >> 5 & 31) << 11 | (c >> 10 & 31) << 19;
  return (c & 15) << 4 | (c >> 4 & 15) << 12 | (c >> 8 & 15) << 20;
}
// How many 16x16 PSP pixel blocks (of 510) differ between the PSP image (the exact one if any; otherwise pj,
// 512 wide) and Vita surface ps: average color of 16 points with over 20 difference in any channel.
// The average makes fine scaling differences not count while a sprite with foreign pieces does.
// Used by the presenter for its different-frames statistic.
static int dumpDiff(const uint32_t *pj, const uint32_t *ps) {
  const uint8_t *nb = dump_natok ? dump_nat : (const uint8_t *)pj;
  uint32_t nf = dump_natok ? dump_natf : 3, nw = dump_natok ? dump_natw : 512;
  int k = 0;
  for (uint32_t by = 0; by < 272 / 16; by++)
    for (uint32_t bx = 0; bx < 480 / 16; bx++) {
      int sa[3] = { 0, 0, 0 };
      for (uint32_t y = by * 16 + 2; y < by * 16 + 16; y += 4)
        for (uint32_t x = bx * 16 + 2; x < bx * 16 + 16; x += 4) {
          uint32_t a = dumpPx(nb, nf, y * nw + x), b = ps[2 * y * LINE + 2 * x];
          for (int c = 0; c < 3; c++) sa[c] += (int)(a >> (8 * c) & 255) - (int)(b >> (8 * c) & 255);
        }
      for (int c = 0; c < 3; c++) if (sa[c] > 20 * 16 || sa[c] < -20 * 16) { k++; break; }
    }
  return k;
}

// Automatic dump detector: how many 16x16 blocks of Vita surface ps do not resemble (over 20 in any
// channel, like dumpDiff) any of the last n_img copies of the PSP image (img[]). The Vita runs 1-2 frames
// behind, so the closest copy of each block counts: a bad block is an error, not lag. It costs the PSP
// nothing. best_p: if not 0, the copy that resembles it most overall.
static int dumpBad(const uint32_t *ps, int *best_p) {
  int k = 0, tot[4] = { 0, 0, 0, 0 };
  for (uint32_t by = 0; by < 272 / 16; by++)
    for (uint32_t bx = 0; bx < 480 / 16; bx++) {
      int mn = 1 << 30;
      for (uint32_t m = 0; m < n_img; m++) {
        const uint32_t *pj = img[m];
        int sa[3] = { 0, 0, 0 }, d = 0;
        for (uint32_t y = by * 16 + 2; y < by * 16 + 16; y += 4)
          for (uint32_t x = bx * 16 + 2; x < bx * 16 + 16; x += 4) {
            uint32_t a = pj[y * 512 + x], b = ps[2 * y * LINE + 2 * x];
            for (int c = 0; c < 3; c++) sa[c] += (int)(a >> (8 * c) & 255) - (int)(b >> (8 * c) & 255);
          }
        for (int c = 0; c < 3; c++) { int v = sa[c] < 0 ? -sa[c] : sa[c]; if (v > d) d = v; }
        tot[m] += d;
        if (d < mn) mn = d;
      }
      if (mn > 20 * 16) k++;
    }
  if (best_p) { int b = 0; for (uint32_t m = 1; m < n_img; m++) if (tot[m] < tot[b]) b = m; *best_p = b; }
  return k;
}

// In the Adrenaline thread, after preparing the surface: notes how it was made (how, and the translator reason if it
// failed; -1 without translator), copies record f (dumpBin) and, if it carries the exact PSP image, keeps it in dump_nat.
static void dumpText(RFrame *f, const char *how, int reason) {
  if ((dump_on != 1 && dump_on != 3) || !dump_mem) return;
  dump_lastn = sceClibSnprintf(dump_last, VL, "surface: %s%s%s | covered=%u\n", how, reason > 0 ? ", reason: " : "", reason > 0 ? tnames[reason] : "", covered);
  dumpBin(f);
  if (f && f->nimg && dump_nat && reason <= 0) {  // (compared in the coming frame)
    dump_natf = f->fbw >> 16 & 3; dump_natw = f->fbw & 0xFFFF;
    sceDmacMemcpy(dump_nat, f->tx + f->nimg - 1, dump_natw * 272 * (dump_natf == 3 ? 4 : 2)); dump_natok = 1;
  } else if (reason != 0) dump_natok = 0;  // (normal image: no record drawn; "repeats": the previous one is kept)
}

// In the Adrenaline thread, at the start of its hookEnd, with the surface to be shown (surf[cur], if has_surf) and the
// PSP image pj. Armed (auto): 1 of every 2 frames, 1 s after the last dump, if 6 or more blocks (and fewer than
// 255: a dialog on top) do not resemble the PSP image, starts capturing. Capturing: copies by DMA both
// images and the frame line (and in f0 intermediate image 0) until it has dump_nf frames; then dump_on = 2.
static void dumpFrame(SceGxmContext *c, const void *pj, int has_surf) {
  if (dump_on == 3 && has_surf && cur_hd && !covered && (fr & 1)) {
    int n = now() - dump_t > 1000000 ? dumpBad(surf[cur], 0) : 0;
    // confirmed with the GPU finished: otherwise it may read the surface half drawn
    if (n >= 6 && n < 255) { sceGxmFinish(c); n = dumpBad(surf[cur], 0); }
    if (n >= 6 && n < 255) { dump_dif = n; dump_k = 0; dump_on = 1; }
  }
  if (dump_on != 1 || dump_k >= dump_nf || !dump_lastn) return;  // (without the frame line yet: from the next one)
  // (L+R+SELECT waits for the exact image, up to 0.5 s)
  if (!dump_k && !dump_dif && dump_level >= 2 && !dump_natok && now() - dump_t0 < 500000) return;
  uint32_t k = dump_k;
  dump_bf[k] = dump_bpend; dump_bpend = VN;  // (the record of the frame seen)
  sceGxmFinish(c);
  int best_k = -1;
  // saves the PSP image that resembles it most (the one of the same frame)
  if (dump_auto && has_surf) dumpBad(surf[cur], &best_k);
  sceDmacMemcpy(dump_ps[k], best_k >= 0 ? img[best_k] : pj, 512 * 272 * 4);
  dump_ok[k] = has_surf;
  if (has_surf) { sceDmacMemcpy(dump_hd[k], surf[cur], LINE * H * 4); sceClibMemcpy(dump_tx[k], dump_last, VL); }
  if (k == 0) {  // (the second one if there is one: it is drawn last in the frame)
    RT *ri = rts[1].mem && rts[1].fb != ~0u ? &rts[1] : &rts[0];
    dump_rtfb = dump_rt && ri->mem && ri->fb != ~0u ? ri->fb : ~0u;
    if (dump_rtfb != ~0u) sceDmacMemcpy(dump_rt, ri->mem, LINE * H * 4);
  }
  else { sceClibSnprintf(dump_tx[k], VL, "surface: not used in this frame (Adrenaline showed the normal image)\n"); dump_bf[k] = VN; }
  if (k == 0 && dump_dif) sceClibSnprintf(dump_tx[0] + sceClibStrnlen(dump_tx[0], VL - 80), 80, "automatic dump: %u blocks that do not resemble any PSP image\n", dump_dif);
  if (++dump_k == dump_nf) dump_on = 2;
}

// Writes to p a 32-bit top-down BMP with w x h RGBA pixels from px (stride: pixels per row).
// Uses dump_w as intermediate memory (16 rows).
static void bmp(const char *p, const uint8_t *px, uint32_t w, uint32_t h, uint32_t stride) {
  SceUID fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (fd < 0) return;
  uint8_t hd[54] = { 'B', 'M' };
  uint32_t sz = 54 + w * h * 4, v[13] = { sz, 0, 54, 40, w, (uint32_t)-(int)h, 1 | 32 << 16, 0, w * h * 4, 2835, 2835, 0, 0 };
  sceClibMemcpy(hd + 2, v, 52);
  sceIoWrite(fd, hd, 54);
  // 16 rows at a time: copied whole (reading px per pixel uncached is very slow), RGBA -> BGRA
  for (uint32_t y = 0; y < h; y += 16) {
    uint32_t m = h - y < 16 ? h - y : 16;
    for (uint32_t r = 0; r < m; r++) {
      uint32_t *q = (uint32_t *)(dump_w + r * w * 4);
      sceClibMemcpy(q, px + (y + r) * stride * 4, w * 4);
      for (uint32_t x = 0; x < w; x++) { uint32_t c = q[x]; q[x] = 0xFF000000u | (c & 0xFF) << 16 | (c & 0xFF00) | (c >> 16 & 0xFF); }
    }
    sceIoWrite(fd, dump_w, m * w * 4);
  }
  sceIoClose(fd);
}

// In the presenter thread: writes to fd the text of the frame from its record b (dumpBin format):
// textures, each state the first time (only the registers that changed) and the draw list. Builds the text in dump_w.
static void dumpList(SceUID fd, const uint8_t *b) {
  const uint32_t *h = (const uint32_t *)b;
  uint32_t nt = h[9], nd = h[5], ne = h[6], o = 64 + ((NRS + 3) & ~3u), k = 0, n = 192 << 10;
  const uint32_t *ta = (const uint32_t *)(b + o), *tn = ta + nt, *tq = tn + 2 * nt;
  const uint64_t *tm = (const uint64_t *)(tq + nt);
  const RDraw *d = (const RDraw *)(tm + nt);
  const uint32_t *e0 = (const uint32_t *)((const uint8_t *)d + ((nd * sizeof(RDraw) + 3) & ~3u));
  char *t = (char *)dump_w;
#define EMIT(...) do { k += sceClibSnprintf(t + k, n - k, __VA_ARGS__); if (k > n - 1024) { sceIoWrite(fd, t, k); k = 0; } } while (0)
  EMIT("record: frame %u, draws %u, states %u, displayed image %07x width %u, incomplete %x, textures %u (%u KB)\n"
      "mirror: copy used %u, last finished %u | textures (address, bytes, groups of 8 rows copied, oldest copy):\n",
      h[2], nd, ne, h[3], h[4] & 0xFFFF, h[8], nt, h[10] >> 10, h[11], h[12]);
  for (uint32_t i = 0; i < nt; i++)
    EMIT("  t%u %07x %u%s groups=%08x%08x copy=%u\n", i, ta[i], tn[i] & 0x7FFFFFFF, tn[i] >> 31 ? " swz" : "", (uint32_t)(tm[i] >> 32), (uint32_t)tm[i], tq[i]);
  EMIT("(each state is written the first time, with the GE registers that changed)\n");
  const uint32_t *prior = 0;
  uint32_t last_st = ~0u;
  for (uint32_t i = 0; i < nd; i++) {
    const uint32_t *e = e0 + d[i].st * NRS;
    if (d[i].st != last_st) {
      EMIT("  state %u:", d[i].st);
      for (int r = 0; r < NRS; r++) if (!prior || prior[r] != e[r]) EMIT(" %02x=%06x", RS[r], e[r] & 0xFFFFFF);
      EMIT("\n");
      prior = e; last_st = d[i].st;
    }
    EMIT("%4u e%u target=%06x prim=%u n=%u vt=%06x tex=%07x copy=%u palette=%u\n", i, d[i].st, RE(e, 0x9C) & 0xFFFFFF, d[i].prim, d[i].n,
        d[i].vt, RE(e, 0x1E) & 1 ? texAddr(e) : 0, d[i].toff ? 1 : 0, d[i].clen);
  }
#undef EMIT
  if (k) sceIoWrite(fd, t, k);
}

#ifdef NP_DEV
// With no memory for a dump: stops drawing, waits for the GPU and saves as they are, in ux0:data/nativedraw/, the surface, the PSP
// image and the corner of each intermediate image (peek<n>_*.bmp)
static void rtPeek(void) {
  static uint8_t row[W * 16 * 4];
  static uint32_t n;
  char p[64];
  uint8_t *w = dump_w;
  rt_hold = 1;
  sceKernelDelayThread(100000);
  for (int i = 0; i < 100 && gBusy(fq[0]); i++) sceKernelDelayThread(10000);
  dump_w = row; n++;
  sceClibSnprintf(p, sizeof p, "ux0:data/nativedraw/peek%u_surface.bmp", n); bmp(p, surf[cur], W, H, LINE);
  sceClibSnprintf(p, sizeof p, "ux0:data/nativedraw/peek%u_psp.bmp", n); bmp(p, sceGxmTextureGetData(&itex[ki]), 480, 272, 512);
  for (int i = 0; i < 2; i++) if (rts[i].mem && rts[i].fb != ~0u) { sceClibSnprintf(p, sizeof p, "ux0:data/nativedraw/peek%u_rt_%06x.bmp", n, rts[i].fb); bmp(p, rts[i].mem, 480, 272, LINE); }
  dump_w = w; rt_hold = 0;
  alog("dump: peek %u saved\n", n);
}
#endif
// Takes the dump memory by level: images (mandatory) and, from "frame", records, exact image and
// intermediate image (optional: if none, the dump comes out without them). Without memory for several frames it tries
// with one. 0 if no memory for the images.
// Records go in physical memory: the translator's intermediate images are reserved earlier.
static int dumpAlloc(void) {
  uint32_t n = 0, ok = 1;
#ifdef NP_DEV
  if (exo_d) {  // (test) everything from the area tested by exoTest; nothing to free
    uint8_t *p = (uint8_t *)exo_d;
    for (int i = 0; i < (int)dump_nf; i++) { dump_hd[i] = p; dump_ps[i] = p + LINE * H * 4; p += (LINE * H * 4 + 512 * 272 * 4 + 0xFFF) & ~0xFFFu; }
    dump_w = p; p += 0x40000;
    for (int i = 0; i < (int)dump_nf && dump_level >= 2; i++) { dump_bb[i] = p; dump_bblk[i] = 0; p += VB_N; }
    dump_rt = dump_level >= 2 ? p : 0; p += LINE * H * 4;
    dump_nat = dump_level >= 2 ? p : 0; dump_natok = 0;
    dump_bpend = VN; dump_mem = 1;
    alog("dump: memory ready in the test area %08x, level %s (%u frames)\n", exo_d, dump_names[dump_level], dump_nf);
    return 1;
  }
#endif
  for (int i = 0; i < (int)dump_nf && ok; i++) {  // Vita surface + PSP image, in one block
    void *p = 0;
    dump_blk[n] = sceKernelAllocMemBlock("nativedraw_dump", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, (LINE * H * 4 + 512 * 272 * 4 + 0x3FFFF) & ~0x3FFFFu, 0);
    if (dump_blk[n] < 0 || sceKernelGetMemBlockBase(dump_blk[n++], &p) < 0) { ok = 0; break; }
    dump_hd[i] = p; dump_ps[i] = (uint8_t *)p + LINE * H * 4;
  }
  void *t = 0;
  if (ok) { dump_blk[n] = sceKernelAllocMemBlock("nativedraw_dump", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x40000, 0); if (dump_blk[n] < 0 || sceKernelGetMemBlockBase(dump_blk[n++], &t) < 0) ok = 0; }
  if (!ok) { alog("dump: no memory\n"); for (uint32_t i = 0; i < n; i++) if (dump_blk[i] >= 0) sceKernelFreeMemBlock(dump_blk[i]); for (int i = 0; i <= VN; i++) dump_blk[i] = 0; if (dump_nf > 1) { dump_nf = 1; return dumpAlloc(); } return 0; }
  dump_w = t;
  int nb = 0;
  for (int i = 0; i < (int)dump_nf && dump_level >= 2; i++) {  // records
    void *q = 0;
    dump_bblk[i] = sceKernelAllocMemBlock("nativedraw_dump_rec", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, VB_N, 0);
    dump_bb[i] = dump_bblk[i] >= 0 && sceKernelGetMemBlockBase(dump_bblk[i], &q) >= 0 ? q : 0;
    DEV(if (!dump_bb[i] && !i && fl_b && !fl_busy) { dump_bb[0] = fl_b; dump_bblk[0] = 0; fl_lent = 1; })  // (the block of failed frames)
    nb += dump_bb[i] != 0;
  }
  void *r = 0, *nn = 0;
  // intermediate image and exact PSP image
  if (dump_level >= 2) dump_rtblk = sceKernelAllocMemBlock("nativedraw_dump_rt", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, (LINE * H * 4 + 0x3FFFF) & ~0x3FFFFu, 0);
  if (dump_level >= 2 && dump_rtblk < 0) dump_rtblk = sceKernelAllocMemBlock("nativedraw_dump_rt", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, (LINE * H * 4 + 0xFFFFF) & ~0xFFFFFu, 0);
  dump_rt = dump_level >= 2 && dump_rtblk >= 0 && sceKernelGetMemBlockBase(dump_rtblk, &r) >= 0 ? r : 0;
  if (dump_level >= 2) dump_natblk = sceKernelAllocMemBlock("nativedraw_dump_nat", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 0x100000, 0);
  dump_nat = dump_level >= 2 && dump_natblk >= 0 && sceKernelGetMemBlockBase(dump_natblk, &nn) >= 0 ? nn : 0; dump_natok = 0;
  dump_bpend = VN; dump_mem = 1;
  alog("dump: memory ready, level %s (%u frames, %u blocks for records)\n", dump_names[dump_level], dump_nf, nb);
  return 1;
}
static void dumpFree(void) {  // frees all the dump memory
  for (int i = 0; i <= VN; i++) if (dump_blk[i] > 0) { sceKernelFreeMemBlock(dump_blk[i]); dump_blk[i] = 0; }
  for (int i = 0; i < VN; i++) { if (dump_bblk[i] > 0) sceKernelFreeMemBlock(dump_bblk[i]); dump_bblk[i] = 0; dump_bb[i] = 0; }
  if (dump_rtblk > 0) sceKernelFreeMemBlock(dump_rtblk);
  if (dump_natblk > 0) sceKernelFreeMemBlock(dump_natblk);
  dump_natblk = 0; dump_nat = 0; dump_natok = 0;
  dump_rtblk = 0; dump_rt = 0; dump_mem = 0;
  DEV(fl_lent = 0;)
}
// In the presenter thread (every 50 ms): in automatic it arms the dump (dump_on = 3, at most 10 per game); with an
// L+R+SELECT request it takes the memory and starts capturing; when capture ends (dump_on = 2) it writes the
// files to the first free dumps/NNN folder and frees the memory (except in automatic).
static void dumpTick(void) {
  DEV(failTick();)
  DEV(if (nd_dump_mark) { nd_dump_mark = 0; if (!dump_on || dump_on == 3) dump_req = 1; })
  if (dump_auto && !dump_on && dump_n < 10) {  // automatic: armed (the memory stays taken)
    if (!dump_mem && !dumpAlloc()) { dump_auto = 0; return; }
    dump_k = 0; dump_dif = 0; dump_lastn = 0; dump_on = 3;
    return;
  }
  // request (L+R+SELECT): memory (freed at the end, except in automatic)
  if (dump_req && (!dump_on || dump_on == 3)) {
    dump_req = 0;
    if (!dump_level) return;
    // for 1.5 s each record frame carries its exact PSP image
    if (dump_level >= 2) nd_exact_until = now() + 1500000;
    dump_t0 = now();
    if (!dump_mem && !dumpAlloc()) { DEV(rtPeek();) return; }
    dump_k = 0; dump_dif = 0; dump_lastn = 0; dump_on = 1;
    alog("dump: capturing %u frame(s), level %s\n", dump_nf, dump_names[dump_level]);
    return;
  }
  if (dump_on != 2) return;
  char d[64], p[96];
  sceIoMkdir("ux0:data/nativedraw/dumps", 0777);
  int n = 0;
  for (; n < 1000; n++) { sceClibSnprintf(d, sizeof d, "ux0:data/nativedraw/dumps/%03d", n); if (sceIoMkdir(d, 0777) >= 0) break; }
  for (int i = 0; i < (int)dump_nf; i++) {
    sceClibSnprintf(p, sizeof p, "%s/f%d_normal.bmp", d, i); bmp(p, dump_ps[i], 480, 272, 512);
    if (dump_ok[i]) { sceClibSnprintf(p, sizeof p, "%s/f%d_vita.bmp", d, i); bmp(p, dump_hd[i], W, H, LINE); }
    int b = dump_bf[i];
    sceClibSnprintf(p, sizeof p, "%s/f%d.txt", d, i);
    SceUID fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) { sceIoWrite(fd, dump_tx[i], sceClibStrnlen(dump_tx[i], VL)); if (b < VN && dump_bb[b]) dumpList(fd, dump_bb[b]); sceIoClose(fd); }
    if (b < VN && dump_bb[b]) {
      sceClibSnprintf(p, sizeof p, "%s/f%d_record.bin", d, i);
      if ((fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666)) >= 0) { sceIoWrite(fd, dump_bb[b], dump_bbn[b]); sceIoClose(fd); }
    }
  }
  if (dump_rtfb != ~0u) { sceClibSnprintf(p, sizeof p, "%s/f0_intermediate_%06x.bmp", d, dump_rtfb); bmp(p, dump_rt, LINE, H, LINE); }
  for (int i = 0; i < VN; i++) dump_bf[i] = VN;
  dump_n++; dump_t = now();
  if (!dump_auto || dump_n >= 10) dumpFree();  // (in automatic the memory stays for the next one)
  dump_on = 0;
  alog("dump: done in %s%s\n", d, dump_dif ? " (automatic)" : "");
}
