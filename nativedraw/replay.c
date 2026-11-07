// replay.c: draws again on the Vita, with the same translator (trFrame), a frame saved by a dump
// (fK_record.bin, dumpBin format in dump.c). Used to separate an error that depends on timing (changing data,
// races with the GPU) from one that always appears with the same data (translator, shaders).
// Included inside presenter.c: presStart calls repStart, the presenter thread repTick, the translator thread records the
// frame (trThread) and the Adrenaline thread runs it and calls repDraw. Usage: replay=<.bin path> in options.txt. Leaves fK_replay.bmp and
// fK_replay_intermediate.bmp next to the .bin and sets the option to "no" (once per game).
static char rep_path[128];
// rep_on: what the replay is doing (REP_READY: loaded, drawn by the Adrenaline thread; REP_DRAWN: drawn, to write)
enum { REP_OFF, REP_LOAD, REP_READY, REP_DRAWN };
static volatile uint32_t rep_on;
// rep_f: the loaded frame; rep_hd and rep_rt: copies of the surface and of the first surface of intermediate images after drawing it
static RFrame *rep_f;
static uint8_t *rep_hd, *rep_rt;
static SceUID rep_blk[2];

// Reads the path from the replay option (up to the end of its line, trimming trailing blanks); if present and not "no",
// rep_on = REP_LOAD.
static void repStart(void) {
  const char *v = optStr("replay");
  if (!v) return;
  int n = 0;
  while (v[n] && v[n] != '\n' && n < (int)sizeof rep_path - 1) { rep_path[n] = v[n]; n++; }
  while (n > 0 && (rep_path[n - 1] == '\r' || rep_path[n - 1] == ' ')) n--;
  rep_path[n] = 0;
  if (n > 4 && !(rep_path[0] == 'n' && rep_path[1] == 'o')) { rep_on = REP_LOAD; alog("replay: %s\n", rep_path); }
}

// In the presenter thread: reads the .bin into its own RFrame (header, checked against the record.h limits, and each
// array aligned to 4) and the textures into memory the GPU reads. On success, rep_on = REP_READY; otherwise rep_on = REP_OFF.
static void repLoad(void) {
  SceUID fd = sceIoOpen(rep_path, SCE_O_RDONLY, 0);
  if (fd < 0) { alog("replay: could not open %s\n", rep_path); rep_on = REP_OFF; return; }
  void *p = 0, *q = 0;
  rep_blk[0] = sceKernelAllocMemBlock("nativedraw_rep", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (sizeof(RFrame) + 0x3FFFF) & ~0x3FFFFu, 0);
  rep_blk[1] = sceKernelAllocMemBlock("nativedraw_rep_img", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 0x500000, 0);
  if (rep_blk[0] < 0 || rep_blk[1] < 0 || sceKernelGetMemBlockBase(rep_blk[0], &p) < 0 || sceKernelGetMemBlockBase(rep_blk[1], &q) < 0) {
    alog("replay: no memory\n"); sceIoClose(fd); rep_on = REP_OFF; return;
  }
  rep_f = p; rep_hd = q; rep_rt = (uint8_t *)q + 0x240000;
  uint8_t *tx = gmem(TX_N + 0x40000, RD);  // (the GPU reads the textures from here)
  uint32_t h[16];
  sceIoRead(fd, h, sizeof h);
  RFrame *f = rep_f;
  int ok = h[0] == 0x47524E44 && h[13] == NRS && h[14] == sizeof(RDraw) && h[5] <= RD_N && h[6] <= RE_N && h[7] <= RA_N && h[9] <= TN_N && tx;
  if (ok) {
    f->frame = h[2]; f->fb = h[3]; f->fbw = h[4]; f->nd = h[5]; f->ne = h[6]; f->na = h[7]; f->flags = h[8]; f->nt = h[9]; f->ntx = h[10];
    f->mir = h[11]; f->mirf = h[12]; f->nimg = h[1] >= 3 ? h[15] : 0;  // (h[1]: format version)
    uint8_t rs[64];
    // READ: n bytes to dst and skips the padding up to a multiple of 4 (like PUT in dumpBin)
#define READ(dst, n) do { uint32_t n_ = (n); if (n_ && sceIoRead(fd, dst, n_) != (int)n_) ok = 0; if (n_ & 3) { uint8_t z[4]; sceIoRead(fd, z, 4 - (n_ & 3)); } } while (0)
    READ(rs, NRS);
    READ(f->ta, f->nt * 4); READ(f->tn, f->nt * 4); READ(f->to, f->nt * 4); READ(f->tq, f->nt * 4); READ(f->tm, f->nt * 8);
    READ(f->d, f->nd * sizeof(RDraw)); READ(f->e, f->ne * NRS * 4); READ(f->a, f->na);
#undef READ
    if (ok && f->ntx <= TX_N + 0x40000 && sceIoRead(fd, tx, f->ntx) != (int)f->ntx) ok = 0;
    for (uint32_t i = 0; ok && h[1] < 4 && i < f->nt; i++) if (f->tn[i] >> 31) ok = 0;  // (before format 4: swizzled unsorted)
    f->tx = tx; sceClibMemset(f->tc, 0, sizeof f->tc);
  }
  sceIoClose(fd);
  if (!ok) { alog("replay: the file is not a record of this version\n"); rep_on = REP_OFF; return; }
  alog("replay: frame %u loaded (%u draws, %u textures)\n", f->frame, f->nd, f->nt);
  rep_on = REP_READY;
}

// In the Adrenaline thread, once the scenes the translator thread recorded for the loaded frame on surface s were run (r: the
// translator's result): waits for the GPU and copies the surface and the first surface of intermediate images (rep_on = REP_DRAWN).
static void repDraw(SceGxmContext *c, int s, int r) {
  sceGxmFinish(c);
  sceDmacMemcpy(rep_hd, surf[s], LINE * H * 4);
  if (rsf[0].mem) sceDmacMemcpy(rep_rt, rsf[0].mem, LINE * H * 4);
  alog("replay: drawn (translator result: %s)\n", tnames[r]);
  rep_on = REP_DRAWN;
}

// In the presenter thread: loads the .bin when the translator is ready and, once drawn, writes the two BMPs
// next to the .bin (fK_record.bin -> fK_replay*.bmp), writes "no" to the path file and frees the memory.
static void repTick(void) {
  if (rep_on == REP_LOAD && t_on) repLoad();
  if (rep_on != REP_DRAWN) return;
  char p[160];
  static uint8_t w[1024 * 4 * 16];  // (bmp() uses dump_w, absent with no dump running: lend this)
  uint8_t *vw = dump_w;
  dump_w = w;
  int n = sceClibStrnlen(rep_path, sizeof rep_path);
  if (n > 11 && !sceClibStrncmp(rep_path + n - 11, "_record.bin", 11)) n -= 11;
  sceClibSnprintf(p, sizeof p, "%.*s_replay.bmp", n, rep_path); bmp(p, rep_hd, W, H, LINE);
  sceClibSnprintf(p, sizeof p, "%.*s_replay_intermediate.bmp", n, rep_path); bmp(p, rep_rt, LINE, H, LINE);
  dump_w = vw;
  optSet("replay", "no");  // (not again at the next start)
  for (int i = 0; i < 2; i++) if (rep_blk[i] > 0) sceKernelFreeMemBlock(rep_blk[i]);
  alog("replay: done in %s\n", p);
  rep_on = REP_OFF;
}
