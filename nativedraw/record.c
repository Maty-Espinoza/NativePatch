// record.c: frame record. Saves every draw of the frame so the Vita can draw it later with its GPU.
// Included inside capture.c (uses its GE state: reg, vaddr, iaddr, vtr, the VRAM mirror) and only works with
// ux0:data/nativedraw/draw. The capturer calls regDraw/regClut/regCurve/regMemo while walking the list and regFlip on
// each flip; the presenter (translator.c) takes the last complete frame with regTake/regRelease.
// Saved per frame: copied vertices and indices (the game may reuse that memory), the GE state (RS) only when it changes,
// palettes when loaded and the texture rows the draws use (copied to uncached memory the GPU reads; those with PSP
// swizzle end up sorted, with rows in sequence: rgTexCopy; RAM textures stay in a cache between frames: RC). There are RN
// records taking turns: one is written, another
// stays published (rg_pub) and those the GPU may be reading are not touched.
// Complete = every command of the frame was walked and all its draws went through regDraw and fit.
static void clog(const char *fmt, ...);
#include "record.h"
enum { R_MEMO = 1, R_READ = 2, R_LIMIT = 4, R_CURVE = 8 };
// Per-second statistics for the "record:" log line (ga accumulates, gr is the copy being written; us_*: time for
// states, vertices, textures, palettes and DMA copies; gpu: textures written to a record the GPU could be reading;
// o_b/us_o: swizzled bytes sorted and their time, included in us_l). t_*: "texturas:" line (rgTexCopy): copies, bytes,
// us of DMA and of sorting, by source (0 RAM, 1 VRAM); DMA calls; copies by size (<1K, <4K, <16K, <64K, more); swizzled
// bytes by row width (16, 32, 64, 128, 256, 512+ bytes) and compared with the last time that group of 8 rows was copied
// (same, changed, new); unswizzled bytes; distinct textures
// per frame
static struct { uint32_t fr, ok, memo, rd_fail, limit, curve, draws, st, kb, cl, us, d3, clear, blend, atest, swz, fmt[16], us_e, us_v, us_t, us_p, t_b, us_l, stomped, n_late, gpu, o_b, us_o,
  t_n[2], t_kb[2], t_dma[2], t_ord[2], t_calls, t_size[5], t_rb[6], t_same, t_changed, t_fresh, t_lines, t_tex,
  c_hit, c_fill, c_chg, c_rec, c_fail, c_us; } ga, gr;
// RN records: with fewer, the one the GPU just read would at times be the only free one, overwriting its textures
#define RN 4
static RFrame *rf[RN];                       // (cached LPDDR + uncached textures: ~2.4 MB each)
static int rg_w;  // the one being written
static uint32_t rg_seq[RN], rg_s;  // number of each record's turn, and the last one given
static volatile int rg_pub = -1, rg_using = -1;  // last complete; the one the presenter is reading
static volatile int rg_last = -1;  // last one taken by the presenter
// (presenter) Reserves the last complete frame until regRelease; 0 if none or if the capturer is reusing it.
// Notes which one it was (rg_last): the GPU keeps reading it until the scene noted with regGpu finishes.
RFrame *regTake(void) {
  int u = rg_pub;
  if (u < 0) return 0;
  rg_using = u; __sync_synchronize();
  if (rg_w == u || rg_pub != u) { rg_using = -1; return 0; }  // (the capturer chose it just before: left alone)
  rg_last = u;
  return rf[u];
}
void regRelease(void) { rg_using = -1; }
// GPU control: rg_gq[k] is the Vita scene that reads the textures of record k (noted by the presenter with regGpu)
// and *rg_gdone the last scene the GPU finished. Used to count writes to a record still in use.
static volatile uint32_t rg_gq[RN], *volatile rg_gdone;
static uint8_t rg_gv[RN];  // (already counted in this use of the record)
static uint32_t rg_gev;
// (presenter) q: scene that reads the taken record; finish: word where the GPU notes the last finished scene
void regGpu(uint32_t q, volatile uint32_t *finish) { rg_gdone = finish; int u = rg_last; if (u >= 0) rg_gq[u] = q; }
// 1 if the GPU has not finished the scene that reads record k
static int rgBusy(int k) { volatile uint32_t *finish = rg_gdone; return finish && (int)(*finish - rg_gq[k]) < 0; }
// Before writing textures to record k: if the GPU has not finished the scene that reads it, counts it and logs it
// (the first 60 times). Only measures, does not wait.
static void rgGpu(int k, const char *what) {
  volatile uint32_t *finish = rg_gdone;
  if (!finish || rg_gv[k] || (int)(*finish - rg_gq[k]) >= 0) return;
  rg_gv[k] = 1; ga.gpu++;
  if (rg_gev++ < 60) clog("record: textures %s written to record %d with the GPU still reading it (%u scenes left)\n",
                          what, k, rg_gq[k] - *finish);
}
static uint32_t rg_clut, rg_clut_b, rg_t;   // loaded palette (offset + 1) and its size; us of this frame

void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
// Textures with PSP swizzle (blocks of 16 bytes x 8 rows): copied by DMA to rg_ord (OR_N bytes; rg_ordc: cached) and
// the CPU leaves them sorted in the record, so the GPU reads them as a plain texture (filtered if the game asks and
// without computing where each texel is)
#define OR_N (256 << 10)
static uint8_t *rg_ord;
static int rg_ordc;
// Repetition measurement: for each group of 8 rows copied, the PSP address and a hash of its content (and length), in
// a table of RH_N slots by address (a slot taken by another address counts as new).
// Tells how much of what is still copied repeats between frames.
#define RH_N 16384
static struct { uint32_t a, h; } *rg_hash;
// 1 if a DMA copy to cached memory c (from u, uncached) is seen when read later, even if c is already in the
// cache (if the system does not invalidate it, the old data would be read)
static int rgCoherent(uint8_t *c, uint8_t *u) {
  for (uint32_t k = 0; k < 2; k++) {
    for (uint32_t i = 0; i < 4096; i++) u[i] = i * 7 + k;
    sceDmacMemcpy(c, u, 4096);
    for (uint32_t i = 0; i < 4096; i++) if (c[i] != (uint8_t)(i * 7 + k)) return 0;
  }
  return 1;
}
// Copies m bytes from s (PSP memory; pa: its PSP address) to d (record textures), by CPU under 8 KB, where a DMA call
// costs more than the bytes. rb: bytes per row if the texture is swizzled (0: as is); then m is whole groups of 8 rows and each group is sorted: block b (16 bytes wide) of row
// r is at s + b * 128 + r * 16 and goes to d + r * rb + b * 16 (d is written in order: it is uncached memory).
// Notes the t_* measurements of the "texturas:" line.
static void rgTexCopy(uint8_t *d, const void *s, uint32_t m, uint32_t rb, uint32_t pa) {
  uint32_t v = isVram(pa), t0 = now(), td = 0, k;
  ga.t_n[v]++; ga.t_kb[v] += m;
  for (k = 0; k < 4 && m >= 1024u << 2 * k; k++) {}
  ga.t_size[k]++;
  if (!rb) {
    if (m < 8192) sceClibMemcpy(d, s, m); else { sceDmacMemcpy(d, s, m); ga.t_calls++; }
    ga.t_lines += m; ga.t_dma[v] += now() - t0; return;
  }
  for (k = 0; k < 5 && rb > 16u << k; k++) {}
  ga.t_rb[k] += m;
  uint32_t gb = 8 * rb, max = OR_N / gb * gb;
  for (uint32_t o = 0; o < m; o += max) {
    uint32_t l = m - o < max ? m - o : max, t1 = now();
    const uint8_t *src = (const uint8_t *)s + o;
    if (l >= 8192) { sceDmacMemcpy(rg_ord, src, l); src = rg_ord; ga.t_calls++; td += now() - t1; }
    for (uint32_t g = 0; g < l; g += gb) {
      uint32_t h = 2166136261u ^ gb;
      for (uint32_t r = 0; r < 8; r++)
        for (uint32_t b = 0; b < rb; b += 16) {
          const uint32_t *i = (const uint32_t *)(src + g + b * 8 + r * 16);
          uint32_t *x = (uint32_t *)(d + o + g + r * rb + b);
          x[0] = i[0]; x[1] = i[1]; x[2] = i[2]; x[3] = i[3];
          h = ((((h ^ i[0]) * 16777619u ^ i[1]) * 16777619u ^ i[2]) * 16777619u ^ i[3]) * 16777619u;
        }
      if (rg_hash) {  // repetition: same / changed / new compared to the last copy of this group
        uint32_t a = pa + o + g, c = (a >> 7 ^ a >> 17) & (RH_N - 1);
        if (rg_hash[c].a != a) ga.t_fresh += gb; else if (rg_hash[c].h == h) ga.t_same += gb; else ga.t_changed += gb;
        rg_hash[c].a = a; rg_hash[c].h = h;
      }
    }
  }
  uint32_t t = now() - t0;
  ga.t_dma[v] += td; ga.t_ord[v] += t - td;
  ga.o_b += m; ga.us_o += t;
}
// Texture cache between frames, only for RAM textures: an entry keeps a whole texture, sorted, in rc_tx (uncached, read
// by the GPU), and a draw uses it without copying. Bit g of m: group of 8 rows g is valid; s and h: its sample and hash.
// d: groups that changed while a record could still be reading them; they stay out of use until no record reads the
// entry (use: the last record's turn that used it), and then are copied in place. In the meantime the texture goes to
// the record's tx as a normal copy, so no row the GPU may read is ever rewritten.
#define RC_N (4 << 20)  // bytes of textures
#define RC_E 256        // entries
typedef struct { uint32_t a, key, gb, o, use; uint64_t m, d; uint32_t s[64], h[64]; } RC;  // key 0: free
static RC *rc;
static uint8_t *rc_tx;
static uint32_t rc_pos;  // where the next new entry is looked for (the memory is used in a ring)
#define RC_B(e) ((((e)->key & 0x7FFFFFFF) + 63) & ~63u)
static volatile int rg_pend;
// Turn of the oldest record that the presenter or the GPU can still read
static uint32_t rcOldest(void) {
  uint32_t o = rg_seq[rg_w];
  for (int k = 0; k < RN; k++)
    if ((k == rg_pub || k == rg_using || k == rg_pend || rgBusy(k)) && (int)(rg_seq[k] - o) < 0) o = rg_seq[k];
  return o;
}
#define RC_LIVE(e, old) ((int)((e)->use - (old)) >= 0)
// Offset in rc_tx for n bytes, or ~0u: walks the ring skipping the entries still read and, on the first pass, those used
// in the last 32 turns; the entries it lands on are dropped.
static uint32_t rcAlloc(uint32_t n) {
  uint32_t old = rcOldest();
  n = (n + 63) & ~63u;
  for (int pass = 0; pass < 2 && n <= RC_N; pass++) {
    uint32_t p = rc_pos, w = 0;
    while (w < 2) {
      if (p + n > RC_N) { p = 0; w++; continue; }
      RC *x = 0;
      for (int i = 0; i < RC_E && !x; i++) {
        RC *e = &rc[i];
        if (e->key && e->o < p + n && p < e->o + RC_B(e) && (RC_LIVE(e, old) || (!pass && (int)(e->use - rg_s + 32) >= 0))) x = e;
      }
      if (x) { p = x->o + RC_B(x); continue; }
      for (int i = 0; i < RC_E; i++) if (rc[i].key && rc[i].o < p + n && p < rc[i].o + RC_B(&rc[i])) rc[i].key = 0;
      rc_pos = p + n;
      return p;
    }
  }
  return ~0u;
}
// Entry of the texture at a (a new one if it has none), or 0 if there is no room or it is under 8 KB: small textures
// cost little to copy, and a few sampled words would not see their changes. Clears d once nobody reads the entry.
static RC *rcGet(uint32_t a, uint32_t key, uint32_t gb) {
  if (!rc || (key & 0x7FFFFFFF) < 8192) return 0;
  RC *e = 0, *f = 0;
  for (int i = 0; i < RC_E && !e; i++) if (rc[i].key == key && rc[i].a == a && rc[i].gb == gb) e = &rc[i];
  if (e) { if (e->d && !RC_LIVE(e, rcOldest())) e->d = 0; return e; }
  uint32_t o = rcAlloc(key & 0x7FFFFFFF), old = rcOldest();
  if (o == ~0u) { ga.c_fail++; return 0; }
  for (int i = 0; i < RC_E && !f; i++) if (!rc[i].key) f = &rc[i];
  for (int i = 0; i < RC_E && !f; i++) if (!RC_LIVE(&rc[i], old)) f = &rc[i];  // (no free slot: one nobody reads)
  if (!f) { ga.c_fail++; return 0; }
  f->a = a; f->key = key; f->gb = gb; f->o = o; f->m = f->d = 0; f->use = rg_seq[rg_w];
  return f;
}
// Sample of l bytes at p, rows of rb bytes: 8 words read directly, one per row and each in another column
static uint32_t rcSample(const uint8_t *p, uint32_t l, uint32_t rb) {
  uint32_t h = l;
  for (uint32_t i = 0; i < 8; i++) {
    uint32_t o = (i * rb + (i * 5 & 7) * rb / 8) & ~3u;
    if (o + 4 <= l) h = (h ^ *(const uint32_t *)(p + o)) * 16777619u;
  }
  return h;
}
// Hash of the l bytes at p, read through the transit memory
static uint32_t rcHash(const void *p, uint32_t l) {
  if (l >= 8192) sceDmacMemcpy(rg_ord, p, l); else sceClibMemcpy(rg_ord, p, l);
  uint32_t h = l;
  for (uint32_t i = 0; i + 4 <= l; i += 4) h = (h ^ *(const uint32_t *)(rg_ord + i)) * 16777619u;
  return h;
}
// Size of group g of e, and where it starts in the texture
static uint32_t rcLen(RC *e, uint32_t g) { uint32_t n = e->key & 0x7FFFFFFF, o = g * e->gb; return n - o < e->gb ? n - o : e->gb; }
// 1 if a group of [g0, g1) is out of use or changed (then it is marked in d). Every group is sampled; in turns, 1 of
// every 16 groups per frame is also hashed whole, for changes the sample does not see.
static int rcCheck(RC *e, uint32_t g0, uint32_t g1) {
  uint32_t t0 = now(), k = e - rc, r = 0;
  for (uint32_t g = g0; g < g1 && !r; g++) {
    const uint8_t *p = (const uint8_t *)PSP(e->a + g * e->gb);
    if (e->d >> g & 1) r = 1;
    else if ((e->m >> g & 1) && (rcSample(p, rcLen(e, g), e->gb / 8) != e->s[g] || (!((g + k + gen) & 15) && rcHash(p, rcLen(e, g)) != e->h[g]))) {
      e->m &= ~(1ull << g); e->d |= 1ull << g; ga.c_chg++; r = 1;
    }
  }
  ga.c_us += now() - t0;
  return r;
}
// Groups [g0, g1) were just copied to e: notes their sample and hash
static void rcNote(RC *e, uint32_t g0, uint32_t g1) {
  for (uint32_t g = g0; g < g1; g++) {
    const uint8_t *p = (const uint8_t *)PSP(e->a + g * e->gb);
    e->s[g] = rcSample(p, rcLen(e, g), e->gb / 8); e->h[g] = rcHash(p, rcLen(e, g)); e->m |= 1ull << g;
    ga.c_fill += rcLen(e, g);
  }
}
// The GE will write [a, a + n) (block copy): the entries it touches go out of use
static void rcInval(uint32_t a, uint32_t n) {
  for (int i = 0; rc && i < RC_E; i++) {
    RC *e = &rc[i];
    if (e->key && e->a < a + n && a < e->a + (e->key & 0x7FFFFFFF)) { e->d |= e->m; e->m = 0; }
  }
}
// Reserves the RN records: the main part in cached memory and the textures in uncached memory (read by the GPU), and
// the transit memory for sorting swizzled textures. Without memory it turns the record off (reg_on = 0). Without
// memory for the texture cache, every texture goes to the record.
static void regInit(void) {
  for (int i = 0; i < RN; i++) {
    void *p = 0;
    SceUID b = sceKernelAllocMemBlock("nativedraw_record", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                      (sizeof(RFrame) + 0x3FFFF) & ~0x3FFFFu, 0);
    if (b < 0 || sceKernelGetMemBlockBase(b, &p) < 0) { reg_on = 0; clog("frame record: no memory (%08x)\n", b); return; }
    rf[i] = p; rf[i]->nd = rf[i]->ne = rf[i]->na = rf[i]->flags = rf[i]->nt = rf[i]->ntx = 0;
    SceUID t = sceKernelAllocMemBlock("nativedraw_record_tex", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, TX_N, 0);
    if (t < 0 || sceKernelGetMemBlockBase(t, &p) < 0) { reg_on = 0; clog("frame record: no memory for textures (%08x)\n", t); return; }
    rf[i]->tx = p;
  }
  // Transit for swizzled textures: cached if the DMA copy keeps it up to date (rgCoherent); otherwise uncached
  void *p = 0;
  SceUID b = sceKernelAllocMemBlock("nativedraw_order", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, OR_N, 0);
  if (b >= 0 && sceKernelGetMemBlockBase(b, &p) >= 0 && rgCoherent(p, rf[0]->tx)) rg_ord = p, rg_ordc = 1;
  else {
    if (b >= 0) sceKernelFreeMemBlock(b);
    b = sceKernelAllocMemBlock("nativedraw_order", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, OR_N, 0);
    if (b < 0 || sceKernelGetMemBlockBase(b, &p) < 0) { reg_on = 0; clog("frame record: no memory to sort textures (%08x)\n", b); return; }
    rg_ord = p;
  }
  b = sceKernelAllocMemBlock("nativedraw_hashes", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x20000, 0);  // (RH_N x 8 bytes)
  if (b >= 0 && sceKernelGetMemBlockBase(b, &p) >= 0) { rg_hash = p; sceClibMemset(p, 0, 0x20000); }
  void *q = 0;
  b = sceKernelAllocMemBlock("nativedraw_tex_cache", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, RC_N, 0);
  SceUID c = sceKernelAllocMemBlock("nativedraw_tex_index", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (sizeof(RC) * RC_E + 0x3FFFF) & ~0x3FFFFu, 0);
  if (b >= 0 && c >= 0 && sceKernelGetMemBlockBase(b, &p) >= 0 && sceKernelGetMemBlockBase(c, &q) >= 0) {
    rc_tx = p; rc = q; sceClibMemset(rc, 0, sizeof(RC) * RC_E);
  } else clog("texture cache: no memory (%08x %08x)\n", b, c);
  clog("frame record: active (draw), %u x %u KB; swizzled textures sorted by the CPU from memory %s\n", RN,
       (unsigned)(sizeof(RFrame) >> 10), rg_ordc ? "with cache (the DMA keeps it up to date)" : "without cache (the DMA does not keep the cache up to date: slow)");
}
// Diagnostic (once, when the mailbox is found): measures the speed of each copy type (256 KB, CPU or DMA, cached or
// not) using record 2 as test memory, and writes it to the log.
static void regBench(void) {
  static int ran;
  if (ran++ || !rf[2] || !okRam(0x0A200000u)) return;
  const void *ps = (const void *)PSP(0x0A200000u);  // (the VRAM mirror)
  uint8_t *c = rf[2]->a, *u = rf[2]->tx;
  uint32_t n = 256 << 10, t[6], t0;
  t0 = now(); sceClibMemcpy(c, ps, n); t[0] = now() - t0;      // PSP RAM -> cached (CPU)
  t0 = now(); sceDmacMemcpy(u, ps, n); t[1] = now() - t0;      // PSP RAM -> uncached (DMA)
  t0 = now(); sceClibMemcpy(u, c, n); t[2] = now() - t0;       // cached -> uncached (CPU)
  t0 = now(); sceClibMemcpy(c, u, n); t[3] = now() - t0;       // uncached -> cached (CPU)
  t0 = now(); sceDmacMemcpy(u + n, u, n); t[4] = now() - t0;   // uncached -> uncached (DMA)
  t0 = now(); sceClibMemcpy(c + n, c, n); t[5] = now() - t0;   // cached -> cached (CPU)
  for (int i = 0; i < 6; i++) t[i] = t[i] ? n / t[i] : 0;
  clog("memory bench (MB/s): PSP RAM->cached %u, PSP RAM->uncached (DMA) %u, cached->uncached %u, "
       "uncached->cached %u, uncached->uncached (DMA) %u, cached->cached %u\n", t[0], t[1], t[2], t[3], t[4], t[5]);
}
// Copies [a, a + n) of PSP memory to the record's area a (aligned to 16), 4 KB page at a time because VRAM is
// translated to the mirror per page. Returns offset + 1; 0 if it does not fit (R_LIMIT) or cannot be read (R_READ).
static uint32_t rgCopy(uint32_t a, uint32_t n) {
  RFrame *f = rf[rg_w];
  uint32_t o = (f->na + 15) & ~15u;
  if (o + n > RA_N) { f->flags |= R_LIMIT; return 0; }
  for (uint32_t k = 0; k < n;) {
    uint32_t s = vtr(a + k), m = 4096 - ((a + k) & 4095);
    if (m > n - k) m = n - k;
    if (!s || ramEnd(s) < s + m) { f->flags |= R_READ; return 0; }
    sceClibMemcpy(f->a + o + k, (const void *)PSP(s), m);
    k += m;
  }
  f->na = o + n;
  return o + 1;
}
int sceGxmMapMemory(void *base, SceSize size, int attr);
// (translator, with GXM initialized) maps for the GPU, read-only, the textures of all records
int regMapGpu(void) {
  int r = 0;
  for (int i = 0; i < RN && rf[i] && r >= 0; i++) r = sceGxmMapMemory(rf[i]->tx, TX_N, 1);  // (1: read)
  if (r >= 0 && rc_tx) r = sceGxmMapMemory(rc_tx, RC_N, 1);
  return r;
}
// Where to read [a, a + n) from: the same address if it is readable RAM; if VRAM, its copy in the mirror, only if all
// pages have a valid copy and sit contiguous in the same mirror span. 0 if not possible.
static uint32_t rgSrc(uint32_t a, uint32_t n) {
  if (!isVram(a)) return ramEnd(a) >= a + n ? a : 0;
  // draw() already requested it: available from the next frame
  for (uint32_t k = 0; k < n; k += 4096 - ((a + k) & 4095)) if (!vtr(a + k)) return 0;
  uint32_t s = vtr(a);
  return vtr(a + n - 1) == s + n - 1 && ramEnd(s) >= s + n ? s : 0;
}
// Check of a RAM texture across lists: list of its last draw and groups of 8 rows known to be current in that list
static uint32_t rg_tl[TN_N];
static uint64_t rg_tc[TN_N];
// 1 if a group of [g0, g1) copied to texture i before this list differs from memory (not for swizzled ones in the
// record: their copy is sorted; the cache has their sample and hash). The game may rewrite a texture between two lists
// of a frame, keeping its address.
static int rgStale(RFrame *f, uint32_t i, uint32_t g0, uint32_t g1) {
  if (rg_tl[i] != pub_ord) { rg_tl[i] = pub_ord; rg_tc[i] = 0; }
  uint32_t gb = f->tr[i], n = f->tn[i], o, c = f->tc[i];
  if ((n >> 31) && !c) return 0;
  for (uint32_t g = g0; g < g1; g++) {
    if (c) {
      if (rg_tc[i] >> g & 1) continue;
      rg_tc[i] |= 1ull << g;
      if (rcCheck(&rc[c - 1], g, g + 1)) return 1;
      continue;
    }
    if (!(f->tm[i] >> g & 1) || rg_tc[i] >> g & 1 || (o = g * gb) >= n) continue;
    rg_tc[i] |= 1ull << g;
    if (ramEnd(f->ta[i] + o) < f->ta[i] + o + (n - o < gb ? n - o : gb)) continue;
    if (sceClibMemcmp((const void *)PSP(f->ta[i] + o), f->tx + f->to[i] + o, n - o < gb ? n - o : gb)) return 1;
  }
  return 0;
}
// Texture of the current draw (GE registers) in tx or, if from RAM and unchanged, in the texture cache: the first time
// in the frame it takes its place (full size in tx; again, as a new entry in tx, if a RAM texture changed: rgStale); then
// copies only the missing groups of 8 rows of [r0, r1) (r0 >= r1 or
// out of range: whole texture). With swizzle a group of 8
// rows is one contiguous block and sorted takes the same place (rgTexCopy sorts it). RAM ones are copied right away by
// DMA (the game may change them); VRAM ones are marked and regPend copies them from the flip's mirror.
// Returns offset + 1 in tx; 0 if not copied (unsupported format, image drawn in this frame, limit or unreadable).
static uint32_t rgTex(uint32_t r0, uint32_t r1) {
  RFrame *f = rf[rg_w];
  static const uint8_t bp[6] = { 16, 16, 16, 32, 4, 8 };
  uint32_t fmt = reg[0xC3] & 15, ta = TEXADDR(), tbw = reg[0xA8] & 0x7FF, th = 1u << ((reg[0xB8] >> 8) & 15), swz = reg[0xC2] & 1;
  if (fmt > 5 || !tbw || th > 512) return 0;
  uint32_t rb = tbw * bp[fmt] / 8, n = rb * (swz && th < 8 ? 8 : th), key = n | swz << 31, i, vr = isVram(ta);
  if (swz && (rb & 15)) { f->flags |= R_READ; return 0; }
  if (r1 > th || r0 >= r1) r0 = 0, r1 = th;
  for (i = f->nt; i > 0 && !(f->ta[i - 1] == ta && f->tn[i - 1] == key); i--) {}  // (the latest entry of this address)
  i = i ? i - 1 : f->nt;
  int stale = i < f->nt && !vr && rgStale(f, i, r0 / 8, (r1 + 7) / 8);
  if (stale) i = f->nt;
  RC *e = 0;
  // First time in this frame: full size is reserved (rows not copied are not read)
  if (i == f->nt) {
    // Image the GE drew in this frame (intermediate image): not copied
    if (vr) { uint32_t p = (ta & 0x1FFFFF) >> 12; if (rt_pg[p >> 5] >> (p & 31) & 1) return 0; }
    if (!vr && !stale && (e = rcGet(ta, key, 8 * rb)) && rcCheck(e, r0 / 8, (r1 + 7) / 8)) e = 0;
    if (!vr && !e) ga.c_rec++;
    if (f->nt >= TN_N || (!e && f->ntx + n > TX_N)) {
      DEV(static uint32_t lim_n; if (lim_n < 20) { lim_n++; clog("record: texture limit at frame %u: %u textures, %u of %u bytes, %u more wanted\n", gen, f->nt, f->ntx, TX_N, n); })
      f->flags |= R_LIMIT; return 0;
    }
    f->ta[i] = ta; f->tn[i] = key; f->to[i] = e ? (uint32_t)(rc_tx + e->o - f->tx) : f->ntx; f->tm[i] = 0; f->tq[i] = vr ? ~0u : 0; f->tr[i] = 8 * rb; f->nt++;
    f->tc[i] = e ? e - rc + 1 : 0;
    rg_tl[i] = pub_ord; rg_tc[i] = 0;
    if (e) e->use = rg_seq[rg_w]; else f->ntx += (n + 63) & ~63u;
  }
  uint32_t g0 = r0 / 8, g1 = (r1 + 7) / 8, gb = 8 * rb;  // groups and bytes per group
  if (f->tc[i]) {  // what the cache holds of these rows, already checked, needs no copy
    e = &rc[f->tc[i] - 1];
    uint64_t have = e->m & ~f->tm[i] & ((g1 - g0 < 64 ? (1ull << (g1 - g0)) : 0) - 1) << g0;
    f->tm[i] |= have; ga.c_hit += __builtin_popcountll(have) * gb;
  }
  for (uint32_t g = g0; g < g1; g++) {
    if (f->tm[i] >> g & 1) continue;
    uint32_t h = g + 1;
    while (h < g1 && !(f->tm[i] >> h & 1)) h++;  // span of uncopied groups [g, h)
    uint32_t o = g * gb, m = (h * gb > n ? n : h * gb) - o;
    // From VRAM: copied later (regPend)
    if (vr) { if (!rgSrc(ta + o, m)) { f->flags |= R_READ; return 0; } f->dv = 1; }
    else {  // from RAM: now (the game may change it), by DMA
      uint32_t sa = rgSrc(ta + o, m), t0 = now();
      if (!e) rgGpu(rg_w, "from RAM");
      if (!sa) { f->flags |= R_READ; return 0; }
      rgTexCopy(f->tx + f->to[i] + o, (const void *)PSP(sa), m, swz ? rb : 0, ta + o);
      if (e) rcNote(e, g, h);
      ga.t_b += m; ga.us_l += now() - t0;
    }
    f->tm[i] |= ((h - g < 64 ? (1ull << (h - g)) : 0) - 1) << g;
    if (!vr) rg_tc[i] |= ((h - g < 64 ? (1ull << (h - g)) : 0) - 1) << g;
    g = h;
  }
  return f->to[i] + 1;
}
// Pending frame: VRAM textures are copied from the mirror copy the PSP made at that frame's flip (f->tgt): the game
// uploads a frame's sprites after the previous flip, so the copy the list was read with may be one frame old.
// Until that copy finishes, the frame is not published.
static volatile int rg_pend = -1;
static uint32_t rg_pend_t;
// (capturer) If the mirror copy of the pending frame has finished (or 100 ms passed: with whatever is there), copies by
// DMA its VRAM textures (and its PSP image if requested for a dump) and publishes it. If the PSP already rewrote that
// mirror, discards it. Never waits for the PSP.
static void regPend(void) {
  int k = rg_pend;
  if (k < 0) return;
  RFrame *f = rf[k];
  int ready = (int)(SB[0x40] - f->tgt) >= 0;
  if (!ready && now() - rg_pend_t < 100000) return;
  uint32_t b = (ready ? f->tgt : SB[0x40]) & 1, base = b ? MIRROR2 : MIRROR, t0 = now();
  rgGpu(k, "from VRAM");
  for (uint32_t i = 0; i < f->nt; i++) {
    if (!isVram(f->ta[i]) || !f->tm[i]) continue;
    uint32_t n = f->tn[i] & 0x7FFFFFFF, gb = f->tr[i];
    uint64_t m = f->tm[i];
    for (uint32_t g = 0; g < 64 && m >> g; g++) {  // spans of requested groups
      if (!(m >> g & 1)) continue;
      uint32_t h = g + 1;
      while (h < 64 && (m >> h & 1)) h++;
      uint32_t o = g * gb, l = (h * gb > n ? n : h * gb) - o, a = (f->ta[i] + o) & 0x1FFFFF;
      for (uint32_t pg = a >> 12, pe = (a + l - 1) >> 12; pg <= pe; pg++) { uint32_t q = CPY(b, pg); if (q < f->tq[i]) f->tq[i] = q; }
      rgTexCopy(f->tx + f->to[i] + o, (const void *)PSP(base + a), l, f->tn[i] >> 31 ? gb / 8 : 0, f->ta[i] + o);
      ga.t_b += l;
      g = h;
    }
  }
  // Dumps: the PSP image of this same frame, if all its pages come from its flip's copy
  if (f->nimg == ~0u) {
    f->nimg = 0;
    uint32_t a = f->fb & 0x1FFFFF, n = (f->fbw & 0xFFFF) * 272 * ((f->fbw >> 16 & 3) == 3 ? 4 : 2), ok = isVram(f->fb) && f->ntx + n <= TX_N;
    for (uint32_t pg = a >> 12; ok && pg <= (a + n - 1) >> 12; pg++) ok = CPY(b, pg) == (ready ? f->tgt : SB[0x40]);
    if (ok) { sceDmacMemcpy(f->tx + f->ntx, (const void *)PSP(base + a), n); f->nimg = f->ntx + 1; f->ntx += (n + 63) & ~63u; }
  }
  ga.us_l += now() - t0;
  // The PSP already copied to that mirror again: discarded
  if ((int)(SB[0x44] - f->tgt) >= 2) { ga.stomped++; rg_pend = -1; return; }
  if (!ready) ga.n_late++;
  f->mirf = SB[0x40];
  __sync_synchronize();
  rg_pub = k; rg_pend = -1;
}
// Texture rows read by the draw's vertices (only 2D through mode, coordinates in texels): computes the vertex size,
// walks the vertices used (by index if any) and takes min and max v. 1 and [r0, r1) if known;
// 0 if not (3D, no texture, weights, morph, negative v or vertices outside what was copied): then all is copied.
static int rgRows(uint32_t vt, const uint8_t *vb, uint32_t vlen, const uint8_t *ib, uint32_t n, uint32_t *r0, uint32_t *r1) {
  static const uint8_t s4[4] = { 0, 1, 2, 4 }, cs[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
  uint32_t tc = vt & 3, ix = vt >> 11 & 3, s = s4[tc], al = s, sz = 2 * s, q;
  if (!(vt >> 23 & 1) || !tc || (vt >> 9 & 3) || (vt >> 18 & 7)) return 0;  // 3D, no texture, weights or morph
  q = cs[vt >> 2 & 7]; if (q) { sz = (sz + q - 1) & ~(q - 1); sz += q; if (q > al) al = q; }
  q = s4[vt >> 5 & 3]; if (q) { sz = (sz + q - 1) & ~(q - 1); sz += 3 * q; if (q > al) al = q; }
  q = s4[vt >> 7 & 3]; if (q) { sz = (sz + q - 1) & ~(q - 1); sz += 3 * q; if (q > al) al = q; }
  sz = (sz + al - 1) & ~(al - 1);
  int lo = 1 << 30, hi = -1;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t k = !ib ? i : ix == 1 ? ib[i] : ix == 2 ? ((const uint16_t *)ib)[i] : ((const uint32_t *)ib)[i];
    if ((k + 1) * sz > vlen) return 0;
    const uint8_t *p = vb + k * sz;  // (the texture coordinate comes first)
    int v = tc == 1 ? p[1] / 128 : tc == 2 ? ((const uint16_t *)p)[1] : (int)((const float *)p)[1];
    if (tc == 3 && ((const float *)p)[1] < 0) return 0;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  if (hi < 0) return 0;
  *r0 = lo > 0 ? lo - 1 : 0; *r1 = hi + 2;  // (one row of margin per side, for the filter)
  return 1;
}
// (capturer, on each draw, before vaddr/iaddr advance) Adds the draw to the frame: the state (RS) if changed, the
// indices and vertices up to the highest index, and the texture (only the rows the vertices use). If something does
// not fit or cannot be read the frame stays incomplete and stops copying. Finally adds the per-game usage statistics.
static void regDraw(uint32_t n, uint32_t type, uint32_t vt, uint32_t idx) {
  RFrame *f = rf[rg_w];
  f->draws_seen++;
  if (f->flags) return;  // already incomplete: nothing more is spent on this frame
  if (f->nd >= RD_N) { f->flags |= R_LIMIT; return; }
  uint32_t t0 = now(), t1, v[NRS];
  for (int i = 0; i < NRS; i++) v[i] = reg[RS[i]];
  if (!f->ne || sceClibMemcmp(v, f->e[f->ne - 1], sizeof v)) {  // new state
    if (f->ne >= RE_N) { f->flags |= R_LIMIT; return; }
    sceClibMemcpy(f->e[f->ne++], v, sizeof v);
  }
  t1 = now(); ga.us_e += t1 - t0;
  RDraw *d = &f->d[f->nd];
  d->st = f->ne - 1; d->prim = type; d->n = n; d->vt = vt; d->ioff = 0; d->clut = rg_clut; d->clen = rg_clut ? rg_clut_b : 0;
  uint32_t vs = vsize(vt), nv = n;
  if (idx) {  // with indices: copied, and the vertices up to the highest index
    uint32_t ib = idx == 1 ? 1 : idx == 2 ? 2 : 4;
    if (!(d->ioff = rgCopy(iaddr, n * ib))) return;
    const uint8_t *p = f->a + d->ioff - 1;
    uint32_t mx = 0;
    for (uint32_t i = 0; i < n; i++) { uint32_t x = ib == 1 ? p[i] : ib == 2 ? ((const uint16_t *)p)[i] : ((const uint32_t *)p)[i]; if (x > mx) mx = x; }
    nv = mx + 1;
  }
  d->vlen = nv * vs;
  if (!(d->voff = rgCopy(vaddr, d->vlen))) return;
  d->toff = 0;
  uint32_t t2 = now(); ga.us_v += t2 - t1;
  if ((reg[0x1E] & 1) && !(reg[0xD3] & 1)) {  // the texture, after the vertices (they give the rows it uses)
    uint32_t r0 = 0, r1 = 0;
    rgRows(vt, f->a + d->voff - 1, d->vlen, d->ioff ? f->a + d->ioff - 1 : 0, n, &r0, &r1);
    d->toff = rgTex(r0, r1);
  }
  f->nd++;
  uint32_t t3 = now(); ga.us_t += t3 - t2;
  rg_t += t3 - t0;
  // what each game uses (log statistics)
  uint32_t *s = f->e[d->st];
  ga.draws++;
  if (!(vt >> 23 & 1) && !(reg[0xD3] & 1)) ga.d3++;
  if (reg[0xD3] & 1) ga.clear++;
  else {
    if (s[1] & 1) ga.blend++;
    if (s[2] & 1) ga.atest++;
    if (s[0] & 1) { ga.fmt[reg[0xC3] & 0xF]++; if (reg[0xC2] & 1) ga.swz++; }
  }
}
// (capturer, command 0xC4) Palette load: copies it to the record (d & 0x3F blocks of 32 bytes) and it becomes the
// palette of the following draws.
static void regClut(uint32_t d) {
  uint32_t n = (d & 0x3F) * 32;
  rg_clut_b = n;
  uint32_t t0 = now();
  rg_clut = n ? rgCopy(CLUTADDR(), n) : 0;
  if (rg_clut) ga.cl++;
  ga.us_p += now() - t0;
}
// Draws that do not go through regDraw: a curve or the draws of a sublist the sublist memory applied without
// walking. They leave the frame incomplete.
static void regCurve(void) { rf[rg_w]->flags |= R_CURVE; rf[rg_w]->draws_seen++; }
static void regMemo(uint32_t draws) { if (draws) { rf[rg_w]->flags |= R_MEMO; rf[rg_w]->draws_seen += draws; } }
// Part of the frame's commands will never be walked (lost events, cut list): its draws are not even counted, so the
// frame stays incomplete here.
static void regLost(void) { if (reg_on) rf[rg_w]->flags |= R_READ; }
// (capturer, command 0xEA) TRANSFER from s to t, bp bytes per pixel. On the image being drawn the HD surface would lose
// it: it goes in the frame as a draw, a rectangle with no blending whose texture is the source (RAM: copied now; VRAM:
// from the flip's mirror). A VRAM texture the frame already used, or a copy over itself, would be read with the content
// left afterwards: the frame stays incomplete.
static void regXfer(uint32_t s, uint32_t t, uint32_t bp) {
  RFrame *f = rf[rg_w];
  uint32_t sw = reg[0xB3] & 0x7FF, dw = reg[0xB5] & 0x7FF, sx = reg[0xEB] & 0x3FF, sy = reg[0xEB] >> 10 & 0x3FF, dx = reg[0xEC] & 0x3FF,
           dy = reg[0xEC] >> 10 & 0x3FF, w = (reg[0xEE] & 0x3FF) + 1, h = (reg[0xEE] >> 10 & 0x3FF) + 1, r = sy * sw * bp;
  uint32_t sa = (s + r) & ~15u, u = sx + ((s + r) & 15) / bp, da = t + dy * dw * bp, sn = h * sw * bp, dn = h * dw * bp;
  if (!isVram(t)) { rcInval(da, dn); return; }
  for (uint32_t i = 0; i < f->nt; i++)
    if (isVram(f->ta[i]) && f->ta[i] < da + dn && da < f->ta[i] + (f->tn[i] & 0x7FFFFFFF)) f->flags |= R_READ;
  uint32_t st = reg[0x9D] & 0x7FF, fbp = (reg[0xD2] & 3) == 3 ? 4 : 2, o = (t & 0x1FFFFF) - (reg[0x9C] & 0x1FFFF0);
  if (o >= st * fbp * ((reg[0xD5] >> 10 & 0x3FF) + 1)) return;  // not on the image being drawn: the mirror has it
  if (isVram(s)) want(sa, sn, 0);
  if (dw != st || bp != fbp || h > 512 || !sw || (isVram(s) && sa < da + dn && da < sa + sn)) f->flags |= R_READ;
  if (f->flags) return;
  if (f->nd >= RD_N || f->ne >= RE_N || ((f->na + 15) & ~15u) + 32 > RA_N) { f->flags |= R_LIMIT; return; }
  dx += o / fbp % st; dy += o / fbp / st;
  uint32_t v[NRS], lw = 0, lh = 0, toff;
  while (1u << lw < sw) lw++;
  while (1u << lh < h) lh++;
  for (int i = 0; i < NRS; i++) {  // the state of this draw, around the one of the image (0: plain)
    v[i] = reg[RS[i]];
    if ((RS[i] & 0xFC) != 0x9C && RS[i] != 0xD2) reg[RS[i]] = 0;
  }
  reg[0x1E] = 1; reg[0xA0] = sa & 0xFFFFFF; reg[0xA8] = (sa >> 8 & 0xFF0000) | sw; reg[0xB8] = lh << 8 | lw;
  reg[0xC3] = bp == 4 ? 3 : reg[0xD2] & 3; reg[0xC7] = 0x101; reg[0xC9] = 0x103; reg[0xD5] = 0x7FDFF;
  toff = rgTex(0, h);
  uint32_t e[NRS];
  for (int i = 0; i < NRS; i++) { e[i] = reg[RS[i]]; reg[RS[i]] = v[i]; }
  if (!toff) { f->flags |= R_READ; return; }
  sceClibMemcpy(f->e[f->ne++], e, sizeof e);
  uint32_t a = (f->na + 15) & ~15u, c = 0xFFFFFFFFu;
  uint16_t vx[16] = { u, 0, c, c >> 16, dx, dy, 0, 0, u + w, h, c, c >> 16, dx + w, dy + h, 0, 0 };
  sceClibMemcpy(f->a + a, vx, sizeof vx);
  f->na = a + 32;
  RDraw *d = &f->d[f->nd++];
  sceClibMemset(d, 0, sizeof *d);
  d->st = f->ne - 1; d->prim = 6; d->n = 2; d->vt = 0x80011E; d->voff = a + 1; d->vlen = 32; d->toff = toff;
}
// (capturer, on each flip) Closes the frame. fb/fbw: shown image and width | format << 16; draws_counted: those the
// capturer counted. If complete it publishes it (or leaves it pending its VRAM textures, regPend), asks the mirror for
// the PSP image if a dump wants it, and picks another record to write: not the published one, nor the one the presenter
// is reading, nor the pending one, nor (if possible) the last one the presenter took. Then empties the new record and
// copies the loaded palette into it again.
static void regFlip(uint32_t fb, uint32_t fbw, uint32_t draws_counted) {
  RFrame *f = rf[rg_w];
  regPend();
  // Counted draws that did not go through regDraw
  if (f->draws_seen < draws_counted) f->flags |= R_READ;
  ga.fr++; ga.us += rg_t; ga.st += f->ne; ga.kb += f->na; ga.t_tex += f->nt;
  if (f->flags & R_MEMO) ga.memo++;
  if (f->flags & R_READ) ga.rd_fail++;
  if (f->flags & R_LIMIT) ga.limit++;
  if (f->flags & R_CURVE) ga.curve++;
  // Complete: published (or left pending its textures) and writing moves to one nobody uses
  if (!f->flags && f->nd) {
    f->fb = fb & 0x0FFFFFFF; f->fbw = fbw; f->frame = gen; f->mir = mir_u; f->mirf = mir_on ? SB[0x40] : 0;
    ga.ok++;
    __sync_synchronize();
    // A dump wants the PSP image of this frame: its pages are requested from the mirror and the frame waits for its copy
    if ((int)(nd_exact_until - now()) > 0 && isVram(f->fb)) {
      uint32_t a = f->fb & 0x1FFFFF, n = (f->fbw & 0xFFFF) * 272 * ((f->fbw >> 16 & 3) == 3 ? 4 : 2);
      for (uint32_t pg = a >> 12; pg <= (a + n - 1) >> 12 && pg < 512; pg++) want_fb[pg >> 5] |= 1u << (pg & 31);
      f->dv = 1; f->nimg = ~0u;
    } else for (int w = 0; w < 16; w++) want_fb[w] = 0;  // (no longer wanted: stop copying)
    if (f->dv && mir_on) {
      if (rg_pend >= 0) ga.stomped++;  // (the previous one was still waiting: replaced)
      // tgt: the mirror copy the PSP queued on this flip (NativeLink notes it before the event)
      f->tgt = SB[0x44]; rg_pend_t = now(); rg_pend = rg_w;
      regPend();
    } else rg_pub = rg_w;
    // The next one: a free one the GPU finished reading; if none, a free one that is not the last the
    // presenter took; otherwise any free one
    int w = -1;
    for (int i = 0; i < RN && w < 0; i++) if (i != rg_pub && i != rg_using && i != rg_pend && !rgBusy(i)) w = i;
    for (int i = 0; i < RN && w < 0; i++) if (i != rg_pub && i != rg_using && i != rg_pend && i != rg_last) w = i;
    if (w < 0) for (int i = 0; i < RN && w < 0; i++) if (i != rg_pub && i != rg_using && i != rg_pend) w = i;
    if (w < 0) { w = rg_pend; rg_pend = -1; ga.stomped++; }  // (none free: the pending one is lost)
    rg_w = w; rg_gv[w] = 0; rg_seq[w] = ++rg_s;
  }
  // Only what the GE draws from now on is held in HD; any other read of those pages comes from the mirror. A sublist
  // the memory applied did not go through draw(): its marks stay
  if (!(f->flags & R_MEMO)) { sceClibMemset(rt_pg, 0, sizeof rt_pg); last_fb = ~0u; }
  const uint8_t *pal = rg_clut ? f->a + rg_clut - 1 : 0;
  f = rf[rg_w];
  f->nd = f->ne = f->na = f->flags = f->draws_seen = f->nt = f->ntx = f->dv = f->nimg = 0;
  rg_t = 0;
  // The GE keeps the palette it loaded whatever the memory holds now: its copy goes on to the new record (from memory
  // only if there is none)
  if (pal) { sceClibMemmove(f->a, pal, rg_clut_b); f->na = rg_clut_b; rg_clut = 1; }
  else rg_clut = rg_clut_b ? rgCopy(last_clut[0], rg_clut_b) : 0;
}
