// measures.h: the measurements of NativeDraw, in one place. Development builds only (NP_DEV). Each one is a file in
// ux0:data/nativedraw/ (read when Adrenaline opens; off with no file or with "no" inside) and one line here: to add one, add a
// line and its variable. Included by capture.c and presenter.c. What each log line means: docs/dev/en/logs-and-switches.md.
// X(file, variable)
// Capturer side
#define MEASURES_CAPTURE(X) \
  X(hold_game, hold_game)       /* the game waits for the whole chain: all frames complete and in HD, slower */ \
  X(cap_split, cs_on)           /* times every part of the capturer on every frame */ \
  X(rec_cost, rec_cost)         /* keeps recording an incomplete frame, without publishing it */ \
  X(crowd_log, crowd_on)        /* commands by type, bounding box jumps, what a frame needs from the one before */ \
  X(thread_split, tn_on)        /* core and time of each thread, every second */ \
  X(cpu_verts, cpu_verts)       /* no vertices for the GPU, to compare */ \
  X(no_capture, no_capture)     /* the Vita walks nothing and marks the lists captured */ \
  X(ge_timing, probe_ge)        /* how long the GE works and how long the game waits for it */ \
  X(walk_log, wlog)             /* walk diagnostics: VRAM sublists, transfers, BASE, list limit, up to 4000 lines each */
// Drawing side
#define MEASURES_DRAW(X) \
  X(gpu_timer, c_wanted)        /* how long the GPU takes on each scene */ \
  X(clocks, clk_on)             /* CPU 444 and crossbar 166 */ \
  X(vert_split, v_on)           /* splits the vertex work of 1 HD frame in 8 */ \
  X(no_join, no_join)           /* draws of the GPU path are not joined, to compare */ \
  X(keep_hd, keep_hd)           /* images that only blur stay at double size, to compare */ \
  X(fx_log, fx_log)             /* pixels per effect in the surface, 1 frame in 32 */
// Others that are not on/off, or that stay where they act: log_level, no_lookahead, no_idle, gate_sync, dump.
#define MEASURE_READ(file, var) var = swOn("ux0:data/nativedraw/" #file);

#if defined(NP_DEV) && defined(MEASURES_WALK)
// crowd_log. op_n: commands by type. bb_*: conditional jumps by bounding box, which the capturer does
// not take: bb_t tested, bb_o with the box outside the view (the GE would jump), bb_odd not testable, and what the capturer
// walked inside those jumps (bb_d draws, bb_v vertices, bb_c commands). bs: registers set before the first draw of the frame.
// e1, e2: registers at the end of the last two frames; sk_n: by register, frames that use a value left by the frame before,
// which a frame not walked would lose (sk_f frames, sk_r registers)
static int crowd_on;
static uint32_t op_n[256], bb_t, bb_o, bb_odd, bb_d, bb_v, bb_c, bb_to, bb_sp, bb_c0, bs[8], e1[256], e2[256], sk_n[256], sk_f, sk_r;
static inline float f24(uint32_t d) { union { uint32_t u; float f; } x; x.u = d << 8; return x.f; }
// 1 if every vertex of the bounding box at vaddr falls outside the same plane of the view, in clip space
static int bbOut(void) {
  uint32_t n = reg[7] & 0xFFFF, vt = reg[0x12], fm = vt >> 7 & 3, sz = vsize(vt), ra = vtr(vaddr), o = 0x3F;
  if (!n || n > 32 || !fm || vt & 0x800000 || sz != (fm == 3 ? 12 : fm == 2 ? 6 : 3) || !ra || ramEnd(ra) < ra + n * sz) { bb_odd++; return 0; }
  float w[12], v[12], p[16];
  for (int i = 0; i < 12; i++) { w[i] = f24(mtx[0][i]); v[i] = f24(mtx[1][i]); }
  for (int i = 0; i < 16; i++) p[i] = f24(mtx[2][i]);
  for (uint32_t i = 0; i < n; i++) {
    uint8_t b[12];
    for (uint32_t k = 0; k < sz; k++) b[k] = ((volatile uint8_t *)PSP(ra + i * sz))[k];
    float x, y, z;
    if (fm == 3) { sceClibMemcpy(&x, b, 4); sceClibMemcpy(&y, b + 4, 4); sceClibMemcpy(&z, b + 8, 4); }
    else if (fm == 2) { int16_t s[3]; sceClibMemcpy(s, b, 6); x = s[0] / 32768.0f; y = s[1] / 32768.0f; z = s[2] / 32768.0f; }
    else { x = (int8_t)b[0] / 128.0f; y = (int8_t)b[1] / 128.0f; z = (int8_t)b[2] / 128.0f; }
    float a = w[0] * x + w[3] * y + w[6] * z + w[9], e = w[1] * x + w[4] * y + w[7] * z + w[10], g = w[2] * x + w[5] * y + w[8] * z + w[11];
    float h = v[0] * a + v[3] * e + v[6] * g + v[9], j = v[1] * a + v[4] * e + v[7] * g + v[10], m = v[2] * a + v[5] * e + v[8] * g + v[11];
    float cx = p[0] * h + p[4] * j + p[8] * m + p[12], cy = p[1] * h + p[5] * j + p[9] * m + p[13];
    float cz = p[2] * h + p[6] * j + p[10] * m + p[14], cw = p[3] * h + p[7] * j + p[11] * m + p[15];
    o &= (cx < -cw) | (cx > cw) << 1 | (cy < -cw) << 2 | (cy > cw) << 3 | (cz < -cw) << 4 | (cz > cw) << 5;
    if (!o) return 0;
  }
  return 1;
}
static void bbDone(List *l) {  // the walk left the jump being followed
  if (bb_to && l->sp <= bb_sp && l->pc >= bb_to) { bb_c += f_cmds - bb_c0; bb_to = 0; }
}
static void bbJump(List *l, uint32_t d) {
  bb_t++; bbDone(l);
  if (bb_to || !bbOut()) return;
  uint32_t t = rel(l, d & 0xFFFFFC);
  if (t <= l->pc) { bb_odd++; return; }
  bb_o++; bb_to = t; bb_sp = l->sp; bb_c0 = f_cmds;
}
static void skipNote(void) {  // at the flip
  if (bb_to) { bb_c += f_cmds - bb_c0; bb_to = 0; }
  uint32_t n = 0;
  for (int c = 0; c < 256; c++) if (e1[c] != e2[c] && !(bs[c >> 5] >> (c & 31) & 1)) { sk_n[c]++; n++; }
  sk_f += n > 0; sk_r += n;
  sceClibMemcpy(e2, e1, sizeof e1); sceClibMemcpy(e1, reg, sizeof e1); sceClibMemset(bs, 0, sizeof bs);
}
// Once per second: the 14 largest of a[], as "command:per frame"
static void topLog(const char *t, uint32_t *a, uint32_t fr) {
  char s[160]; int k = 0;
  for (int i = 0; i < 14; i++) {
    int b = 0; for (int c = 1; c < 256; c++) if (a[c] > a[b]) b = c;
    if (!a[b]) break;
    k += sceClibSnprintf(s + k, sizeof s - k, " %02x:%u", b, a[b] / fr); a[b] = 0;
  }
  s[k] = 0; clog("  %s:%s\n", t, s); sceClibMemset(a, 0, 256 * 4);
}
static void crowdLog(uint32_t fr) {
  if (!fr) return;
  topLog("commands by type (per frame)", op_n, fr);
  clog("  bounding boxes (per frame): jumps=%u outside=%u not_testable=%u | walked inside those jumps: commands=%u draws=%u vertices=%u\n",
       bb_t / fr, bb_o / fr, bb_odd / fr, bb_c / fr, bb_d / fr, bb_v / fr);
  clog("  frames that need the frame before: %u of %u, registers %u\n", sk_f, fr, sk_r);
  topLog("registers that need the frame before (per frame x1)", sk_n, 1);
  bb_t = bb_o = bb_odd = bb_d = bb_v = bb_c = sk_f = sk_r = 0;
}
#endif
