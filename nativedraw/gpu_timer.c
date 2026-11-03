// gpu_timer.c (included in presenter.c, development builds only): how long the Vita GPU takes on each scene. Only
// with the switch ux0:data/nativedraw/gpu_timer.
// Adrenaline's scenes and ours go through the same context, so the GPU finishes them in the order they were
// sent. Each sceGxmEndScene notes in a queue the scene type, its draws, the send time and the notification the
// GPU writes when done (Adrenaline's, which have none, get our own). A thread checks every ~0.1 ms the
// oldest scene in the queue; when the GPU finishes it, its time is: end time - (send or end of the previous one, the
// later). If the GPU was idle when the scene arrived, that gap is added as idle time. Every 10 s the
// presenter logs the totals per type and, in detail, the scenes following the report.
enum { C_ADR, C_NORMAL, C_TR, C_INT, CN };
static const char *cnames[CN] = { "Adrenaline (screen)", "normal image doubled", "translator (surface)", "translator (intermediate)" };
typedef struct { volatile uint32_t *a; uint32_t v, t, ty, dr, px, sw; } CScene;  // px: thousands of pixels; sw: swizzled draws
#define CQ 64
static CScene cq[CQ];
static volatile uint32_t c_in, c_out, c_on, c_full, c_spins, c_idle;
static SceGxmNotification c_adr;  // the notification given to Adrenaline's scenes (word c_slot)
static uint32_t c_slot, c_seq;
// per type: scenes, GPU us, maximum, draws, us queued (sent with the GPU busy on another), ms histogram
// (<5, 5-10, 10-15, 15-20, 20-30, 30 or more)
static volatile uint32_t c_n[CN], c_us[CN], c_max[CN], c_dr[CN], c_queue[CN], c_h[CN][6], c_px[CN], c_sw[CN];
#define CD 16
static struct { uint8_t ty; uint16_t dr; uint32_t queue, us; } c_det[CD];  // detail: the scenes following the report
static volatile uint32_t c_dn = CD;  // scenes already noted in c_det (CD: full, waiting for the report to print it)
// our scenes over 30 ms (the presenter logs them and requests a dump): the last CL, c_ln in total
#define CL 8
static struct { uint8_t ty; uint16_t dr; uint32_t px, us, queue; } c_slow[CL];
static volatile uint32_t c_ln;

// notes in the queue a scene just sent (n: its end notification; dr: draws; px: thousands of pixels drawn,
// adding up each draw; sw: draws with textures that had swizzle on the PSP)
static void cNote(int ty, const SceGxmNotification *n, uint32_t dr, uint32_t px, uint32_t sw) {
  if (!c_on || !n) return;
  uint32_t i = c_in;
  if (i - c_out >= CQ) { c_full++; return; }  // (the next scene takes its time)
  CScene *e = &cq[i % CQ];
  e->a = n->address; e->v = n->value; e->ty = ty; e->dr = dr; e->px = px; e->sw = sw; e->t = now();
  __sync_synchronize();
  c_in = i + 1;
}
// for Adrenaline's sceGxmEndScene hook: if its scene has no end notification, ours
static const SceGxmNotification *cAdr(const SceGxmNotification *f) {
  if (!c_on || f) return f;
  c_adr.value = ++c_seq;
  return &c_adr;
}
// thread: takes from the queue the scenes the GPU finished and adds up their times
static int cThread(SceSize args, void *argp) {
  uint32_t prev = now();
  for (;;) {
    sceKernelDelayThread(100);
    c_spins++;
    while (c_out != c_in) {
      CScene *e = &cq[c_out % CQ];
      if ((int)(*e->a - e->v) < 0) break;
      uint32_t t = now(), begin = prev;
      if ((int)(e->t - prev) > 0) { c_idle += e->t - prev; begin = e->t; }  // the GPU had nothing: idle gap
      uint32_t us = t - begin, k = e->ty, ms = us / 1000;
      pAdd(P_G_ADR + k, us);
      c_n[k]++; c_us[k] += us; c_dr[k] += e->dr; c_queue[k] += begin - e->t; c_px[k] += e->px; c_sw[k] += e->sw;
      if (us > c_max[k]) c_max[k] = us;
      c_h[k][ms < 20 ? ms / 5 : ms < 30 ? 4 : 5]++;
      if (us > 30000 && k != C_ADR) {
        uint32_t j = c_ln % CL;
        c_slow[j].ty = k; c_slow[j].dr = e->dr; c_slow[j].px = e->px; c_slow[j].us = us; c_slow[j].queue = begin - e->t;
        __sync_synchronize(); c_ln++;
      }
      if (c_dn < CD) { c_det[c_dn].ty = k; c_det[c_dn].dr = e->dr; c_det[c_dn].queue = begin - e->t; c_det[c_dn].us = us; c_dn++; }
      prev = t; c_out++;
    }
  }
  return 0;
}
// called by the presenter when marking ready: picks the word for Adrenaline (the free one below gq_slot; adr: bits of
// those Adrenaline uses) and starts the thread
static void cStart(volatile uint32_t *nr, const uint32_t *adr) {
  if (!nr || !gq_done) return;
  for (c_slot = gq_slot - 1; c_slot > 400 && (adr[c_slot >> 5] >> (c_slot & 31) & 1); c_slot--) {}
  c_adr.address = nr + c_slot; *c_adr.address = 0;
  SceUID th = sceKernelCreateThread("nativedraw_timer", cThread, 0x40, 0x1000, 0, 0, 0);
  if (th >= 0 && sceKernelStartThread(th, 0, 0) >= 0) c_on = 1;
  alog("GPU timer: %s (notification %u for the Adrenaline scenes)\n", c_on ? "on" : "could not create the thread", c_slot);
}
// 10 s report (dt: ms since the previous one): one line per type, the total and the detail of a few consecutive scenes
static void cReport(uint32_t dt) {
  uint32_t tot = 0, oc = c_idle, vu = c_spins, ll = c_full;
  c_idle = c_spins = c_full = 0;
  for (int k = 0; k < CN; k++) {
    uint32_t n = c_n[k], us = c_us[k], mx = c_max[k], dr = c_dr[k], qsum = c_queue[k], px = c_px[k], sw = c_sw[k], h[6];
    for (int j = 0; j < 6; j++) { h[j] = c_h[k][j]; c_h[k][j] = 0; }
    c_n[k] = c_us[k] = c_max[k] = c_dr[k] = c_queue[k] = c_px[k] = c_sw[k] = 0;
    tot += us;
    if (!n) continue;
    alog("gpu times: %s: scenes %u, GPU %u ms (average %u.%02u ms, max %u.%u) | draws per scene %u (swizzled %u), thousands "
         "of pixels per scene %u | in queue average %u.%u ms | ms: <5 %u, 5-10 %u, 10-15 %u, 15-20 %u, 20-30 %u, 30+ %u\n",
         cnames[k], n, us / 1000, us / n / 1000, us / n / 10 % 100, mx / 1000, mx / 100 % 10, dr / n, sw / n, px / n,
         qsum / n / 1000, qsum / n / 100 % 10, h[0], h[1], h[2], h[3], h[4], h[5]);
  }
  alog("gpu times: busy %u ms of %u (%u %%), idle %u ms | queue full %u | thread loops %u (one every %u us)\n",
       tot / 1000, dt, dt ? tot / 10 / dt : 0, oc / 1000, ll, vu, vu ? dt * 1000 / vu : 0);
  if (c_dn == CD) {  // detail of the scenes following the previous report (in the order the GPU finished them)
    char b[400]; int n = 0;
    static const char *abbr[CN] = { "A", "N", "T", "I" };
    for (int i = 0; i < CD && n < (int)sizeof b - 40; i++)
      n += sceClibSnprintf(b + n, sizeof b - n, " %s%u:%u.%u/%u.%u", abbr[c_det[i].ty], c_det[i].dr, c_det[i].us / 1000,
                           c_det[i].us / 100 % 10, c_det[i].queue / 1000, c_det[i].queue / 100 % 10);
    b[n] = 0;
    alog("gpu times: detail (type and draws: GPU ms / queue ms; A Adrenaline, N normal image, T translator, I intermediate):%s\n", b);
  }
  c_dn = 0;
}
