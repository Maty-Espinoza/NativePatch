// scenes.c: NPTEST scenes, one technique each (see main.c). Three groups: what NativeDraw draws today (expect "ok"),
// what it rejects with a known reason, and techniques with no code in the translator yet (expect "?").
#include "nptest.h"
#include <math.h>
#define A16 __attribute__((aligned(16)))
#define N 64
static u32 A16 t8888[N * N], pal[256], pal2[256], t_c[N * N];
static u16 A16 t565[N * N], t5551[N * N], t4444[N * N];
static u8 A16 tp8[N * N], tp4[N * N / 2];
static u32 A16 s8888[N * N], rampal[16];
static u16 A16 s565[N * N];
static u8 A16 sp8[N * N];
static u8 A16 mip1[32 * 32];
static u8 A16 dxt[N * N];
static u32 A16 tall[N * 1024];
static u32 sub[8];
static Vc A16 vbuf[8];
static s16 A16 sub_v[6];
static int ready;

// Swizzles h rows of w bytes (w a power of 2) from in to out: blocks of 16 bytes x 8 rows
static void swz(void *out, const void *in, int w, int h) {
  u32 *d = out;
  for (int by = 0; by < h / 8; by++)
    for (int bx = 0; bx < w / 16; bx++)
      for (int j = 0; j < 8; j++) {
        const u32 *s = (const u32 *)((const u8 *)in + (by * 8 + j) * w + bx * 16);
        for (int k = 0; k < 4; k++) *d++ = s[k];
      }
}
// The textures: an F (not symmetric, flips show), horizontal alpha ramp, color ramp behind it
static void init(void) {
  if (ready++) return;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      int f = (x >= 8 && x < 20 && y >= 8 && y < 56) || (x >= 8 && x < 52 && y >= 8 && y < 18) || (x >= 8 && x < 40 && y >= 26 && y < 36);
      u32 r = f ? 255 : x * 4, g = f ? 255 : y * 4, b = f ? 255 : 128, a = x * 4 + 3, i = y * N + x;
      t8888[i] = a << 24 | b << 16 | g << 8 | r;
      t565[i] = (b >> 3) << 11 | (g >> 2) << 5 | r >> 3;
      t5551[i] = (x >= 32) << 15 | (b >> 3) << 10 | (g >> 3) << 5 | r >> 3;
      t4444[i] = (a >> 4) << 12 | (b >> 4) << 8 | (g >> 4) << 4 | r >> 4;
      tp8[i] = f ? 255 : (x / 4 + y / 4 * 16) & 0xEF;
      tp4[y * N / 2 + x / 2] |= (f ? 15 : (x ^ y) >> 3 & 7) << (x & 1) * 4;
    }
  for (int i = 0; i < 256; i++) {
    pal[i] = 0xFF000000 | (255 - i) << 16 | ((i * 5) & 255) << 8 | i;
    pal2[i] = 0xFF000000 | i << 16 | (255 - i);
  }
  pal[255] = pal[15] = WHITE;
  swz(s8888, t8888, N * 4, N); swz(s565, t565, N * 2, N); swz(sp8, tp8, N, N);
  memset(mip1, 0xF0, sizeof mip1);
  for (int i = 0; i < sizeof dxt; i++) dxt[i] = i * 37;
  for (int y = 0; y < 1024; y++) for (int x = 0; x < N; x++) tall[y * N + x] = 0xFF000000 | (y / 4) << 8 | x * 4;
  memcpy(t_c, t8888, sizeof t_c);
  sceKernelDcacheWritebackAll();
}
static void cell(int i, u32 c) { spr(0, 0, N, N, CX(i), CY(i), N, N, c); }
static void t32(void) { tex(GU_PSM_8888, 0, N, N, N, t8888); }
static void clut(const void *p, int psm, int shift, int mask, int start) {
  sceGuClutMode(psm, shift, mask, start);
  sceGuClutLoad(32, p);
}
static void quad(const Vc *v) { sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_TRIANGLE_STRIP, VC, 4, 0, v); }
static Vc *qv(int x, int y, int s, u32 c0, u32 c1) {
  Vc *v = sceGuGetMemory(4 * sizeof(Vc));
  v[0] = (Vc){ c0, x, y, 0 }; v[1] = (Vc){ c0, x + s, y, 0 }; v[2] = (Vc){ c1, x, y + s, 0 }; v[3] = (Vc){ c1, x + s, y + s, 0 };
  return v;
}
static void texrt(int i) { tex(GU_PSM_8888, 0, 128, 128, 128, VRAM(RT(i))); }
static void rtFill(int i, u32 c, int psm) {
  target(RT(i), psm, 128);
  sceGuClearColor(c); sceGuClear(GU_COLOR_BUFFER_BIT);
  rect(16, 16, 96, 32, 0xFF00FF00); rect(16, 64, 32, 48, 0xFF0000FF);
}

// --- what NativeDraw draws today ---
// Alpha test and blending are set once, in the very first list of the program, and never again
static void firstState(int f) {
  sceGuDrawBufferList(GU_PSM_8888, backBuf(), FB_W);
  if (!f) {
    sceGuEnable(GU_SCISSOR_TEST); sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_ALPHA_TEST); sceGuAlphaFunc(GU_GREATER, 128, 255);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA); sceGuTexFilter(GU_NEAREST, GU_NEAREST); sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  }
  init();
  sceGuClearColor(0xFF402020); sceGuClear(GU_COLOR_BUFFER_BIT);
  t32(); cell(0, WHITE); cell(7, WHITE);
}
static void clearS(int f) {
  sceGuClearColor(0xFF0000FF); sceGuScissor(CX(0), CY(0), CX(0) + N, CY(0) + N); sceGuClear(GU_COLOR_BUFFER_BIT);
  sceGuClearColor(0xFF00FF00); sceGuScissor(CX(1), CY(1), CX(1) + N, CY(1) + N); sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);
  sceGuClearDepth(0x8000); sceGuScissor(0, 0, 480, 272); sceGuClear(GU_DEPTH_BUFFER_BIT);
}
static void prims(int f) {
  static const u8 i8[6] = { 0, 1, 2, 2, 1, 3 };
  static const u16 i16[6] = { 0, 1, 2, 2, 1, 3 };
  static const u32 i32[6] = { 0, 1, 2, 2, 1, 3 };
  Vc *v = qv(CX(0), CY(0), N, 0xFF0000FF, 0xFFFF0000);
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDrawArray(GU_TRIANGLE_STRIP, VC, 4, 0, v);
  sceGuDrawArray(GU_TRIANGLES, VC, 3, 0, qv(CX(1), CY(1), N, 0xFF00FF00, 0xFFFF00FF));
  rect(CX(2), CY(2), N, N, 0xFF00FFFF);
  sceKernelDcacheWritebackAll();
  sceGuDrawArray(GU_TRIANGLES, VC | GU_INDEX_8BIT, 6, i8, qv(CX(3), CY(3), N, 0xFF0000FF, WHITE));
  sceGuDrawArray(GU_TRIANGLES, VC | GU_INDEX_16BIT, 6, i16, qv(CX(4), CY(4), N, 0xFF00FF00, WHITE));
  sceGuDrawArray(GU_TRIANGLES, VC | 3 << 11, 6, i32, qv(CX(5), CY(5), N, 0xFFFF0000, WHITE));
}
static void vertFmts(int f) {
  sceGuEnable(GU_TEXTURE_2D); t32();
  struct { u8 u, v; u16 c; s16 x, y, z; } *a = sceGuGetMemory(2 * 10);
  a[0].u = a[0].v = 0; a[0].c = 0xF81F; a[0].x = CX(0); a[0].y = CY(0); a[0].z = 0;
  a[1].u = a[1].v = N; a[1].c = 0xFFFF; a[1].x = CX(0) + N; a[1].y = CY(0) + N; a[1].z = 0;
  sceGuDrawArray(GU_SPRITES, GU_TEXTURE_8BIT | GU_COLOR_5650 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, a);
  struct { float u, v; u32 c; float x, y, z; } *b = sceGuGetMemory(2 * 24);
  b[0].u = b[0].v = 0; b[0].c = WHITE; b[0].x = CX(1); b[0].y = CY(1); b[0].z = 0;
  b[1].u = b[1].v = N; b[1].c = WHITE; b[1].x = CX(1) + N; b[1].y = CY(1) + N; b[1].z = 0;
  sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, b);
  sceGuDisable(GU_TEXTURE_2D);
  static const int ct[2] = { GU_COLOR_5551, GU_COLOR_4444 };
  static const u16 cv[2] = { 0x83E0, 0xF0F0 };
  for (int k = 0; k < 2; k++) {
    struct { u16 c; s16 x, y, z; } *c = sceGuGetMemory(2 * 8);
    c[0].c = c[1].c = cv[k]; c[0].x = CX(2 + k); c[0].y = CY(2 + k); c[0].z = 0; c[1].x = c[0].x + N; c[1].y = c[0].y + N; c[1].z = 0;
    sceGuDrawArray(GU_SPRITES, ct[k] | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, c);
  }
  struct { s16 x, y, z; } *p = sceGuGetMemory(2 * 6);  // no vertex color: the material color
  p[0].x = CX(4); p[0].y = CY(4); p[0].z = 0; p[1].x = CX(4) + N; p[1].y = CY(4) + N; p[1].z = 0;
  sceGuColor(0xFFFF8000);
  sceGuDrawArray(GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, p);
  struct { s8 x, y, z; } *d = sceGuGetMemory(2 * 4);  // 8-bit position: small, top left
  d[0].x = 60; d[0].y = 2; d[0].z = 0; d[1].x = 100; d[1].y = 10; d[1].z = 0;
  sceGuColor(0xFF00FFFF);
  sceGuDrawArray(GU_SPRITES, GU_VERTEX_8BIT | GU_TRANSFORM_2D, 2, 0, d);
}
static void texFmts(int f) {
  tex(GU_PSM_5650, 0, N, N, N, t565); cell(0, WHITE);
  tex(GU_PSM_5551, 0, N, N, N, t5551); cell(1, WHITE);
  tex(GU_PSM_4444, 0, N, N, N, t4444); cell(2, WHITE);
  t32(); cell(3, WHITE);
  clut(pal, GU_PSM_8888, 0, 0xFF, 0);
  tex(GU_PSM_T4, 0, N, N, N, tp4); cell(4, WHITE);
  tex(GU_PSM_T8, 0, N, N, N, tp8); cell(5, WHITE);
}
static void texSwz(int f) {
  tex(GU_PSM_8888, 1, N, N, N, s8888); cell(0, WHITE);
  tex(GU_PSM_5650, 1, N, N, N, s565); cell(1, WHITE);
  clut(pal, GU_PSM_8888, 0, 0xFF, 0);
  tex(GU_PSM_T8, 1, N, N, N, sp8); cell(2, WHITE);
}
static void filterWrap(int f) {
  static const int w[4][2] = { { GU_CLAMP, GU_CLAMP }, { GU_REPEAT, GU_CLAMP }, { GU_CLAMP, GU_REPEAT }, { GU_REPEAT, GU_REPEAT } };
  t32();
  for (int i = 0; i < 4; i++) {
    sceGuTexWrap(w[i][0], w[i][1]);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST); spr(32, 32, 160, 160, CX(i), CY(i), N, N, WHITE);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR); spr(32, 32, 160, 160, CX(i + 6), CY(i + 6), N, N, WHITE);
  }
  sceGuTexFilter(GU_LINEAR, GU_NEAREST); spr(0, 0, N, N, CX(12), CY(12), 32, 32, WHITE);
  sceGuTexFilter(GU_NEAREST, GU_LINEAR); spr(0, 0, 16, 16, CX(13), CY(13), N, N, WHITE);
}
static void palettes(int f) {
  sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0); sceGuClutLoad(32, pal);
  tex(GU_PSM_T8, 0, N, N, N, tp8); cell(0, WHITE);
  clut(pal, GU_PSM_8888, 4, 0xFF, 0); cell(1, WHITE);
  clut(pal, GU_PSM_8888, 0, 0x0F, 0); cell(2, WHITE);
  clut(pal, GU_PSM_8888, 0, 0x7F, 8); cell(3, WHITE);
  static u16 A16 p565[256];
  for (int i = 0; i < 256; i++) p565[i] = i * 0x0101;
  sceKernelDcacheWritebackRange(p565, sizeof p565);
  clut(p565, GU_PSM_5650, 0, 0xFF, 0); cell(4, WHITE);
  clut(p565, GU_PSM_5551, 0, 0xFF, 0); cell(5, WHITE);
  clut(p565, GU_PSM_4444, 0, 0xFF, 0); cell(6, WHITE);
  clut(pal, GU_PSM_8888, 1, 0x07, 2);
  tex(GU_PSM_T4, 0, N, N, N, tp4); cell(7, WHITE);
}
static void texFuncs(int f) {
  static const int t[6][3] = { { GU_TFX_MODULATE, GU_TCC_RGB, 0 }, { GU_TFX_MODULATE, GU_TCC_RGBA, 0 }, { GU_TFX_REPLACE, GU_TCC_RGB, 0 },
                               { GU_TFX_REPLACE, GU_TCC_RGBA, 0 }, { GU_TFX_MODULATE, GU_TCC_RGBA, 1 }, { GU_TFX_REPLACE, GU_TCC_RGBA, 1 } };
  sceGuEnable(GU_BLEND); t32();
  for (int i = 0; i < 6; i++) {
    sceGuTexFunc(t[i][0], t[i][1]);
    if (t[i][2]) sceGuSendCommandi(0xC9, t[i][0] | t[i][1] << 8 | 1 << 16);
    cell(i, 0xC08080FF);
  }
}
static void blendOps(int f) {
  static const int op[5] = { GU_ADD, GU_SUBTRACT, GU_REVERSE_SUBTRACT, GU_MIN, GU_MAX };
  sceGuEnable(GU_BLEND); t32();
  for (int i = 0; i < 5; i++) { sceGuBlendFunc(op[i], GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); cell(i, WHITE); }
}
static void blendFactors(int f) {
  static const int s[4] = { GU_SRC_COLOR, GU_ONE_MINUS_SRC_COLOR, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA };
  sceGuEnable(GU_BLEND); t32();
  for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
    sceGuBlendFunc(GU_ADD, s[i], s[j], 0, 0);
    spr(0, 0, N, N, CX(i + j * 6), CY(i + j * 6), 40, 40, WHITE);
  }
  sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0); cell(4, 0x80FFFFFF);
  sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0, 0xFFFFFF); cell(5, WHITE);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF); cell(10, WHITE);
}
static void alphaTest(int f) {
  static const int fn[8] = { GU_NEVER, GU_ALWAYS, GU_EQUAL, GU_NOTEQUAL, GU_LESS, GU_LEQUAL, GU_GREATER, GU_GEQUAL };
  sceGuEnable(GU_ALPHA_TEST); t32();
  for (int i = 0; i < 8; i++) { sceGuAlphaFunc(fn[i], 131, 255); cell(i, WHITE); }
}
static void scissorRect(int f) {
  sceGuScissor(CX(0) + 8, CY(0) + 8, CX(0) + 40, CY(0) + 40); t32(); cell(0, WHITE);
  sceGuScissor(0, 0, 480, 272);
}
static void layers(int f) {
  sceGuEnable(GU_BLEND); sceGuEnable(GU_ALPHA_TEST); sceGuAlphaFunc(GU_GREATER, 0, 255); t32();
  for (int i = 0; i < 200; i++) spr(0, 0, N, N, (i * 37 + f * ((i & 3) + 1)) % 440, (i * 53) % 220, N / 2, N / 2, 0xFF000000 | i * 0x050307);
}
static void rtOne(int f) {
  rtFill(0, 0xFF800000 | (f & 255) << 8, GU_PSM_8888); targetFb();
  texrt(0); spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR); spr(0, 0, 128, 128, CX(2), CY(2), 200, 128, WHITE);
}
static void rtTwo(int f) {
  rtFill(0, 0xFF800000, GU_PSM_8888);
  target(RT(1), GU_PSM_8888, 128); sceGuClearColor(0xFF004000); sceGuClear(GU_COLOR_BUFFER_BIT); rect(32, 32, 64, 64, 0xFF0000FF);
  targetFb();
  texrt(0); spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
  texrt(1); spr(0, 0, 128, 128, CX(2), CY(2), 128, 128, WHITE);
}
static void rtFmts(int f) {
  rtFill(0, 0xFF800000, GU_PSM_5650); rtFill(1, 0xFF800000, GU_PSM_5551); targetFb();
  tex(GU_PSM_5650, 0, 128, 128, 128, VRAM(RT(0))); spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
  tex(GU_PSM_5551, 0, 128, 128, 128, VRAM(RT(1))); spr(0, 0, 128, 128, CX(2), CY(2), 128, 128, WHITE);
}
static void fbPlain(int f) { t32(); sceGuEnable(GU_BLEND); cell(0, WHITE); rect(CX(1), CY(1), N, N, 0x80FF00FF); }
static void xferRam(int f) {
  sceGuCopyImage(GU_PSM_8888, 0, 0, N, N, N, t8888, 0, 0, 128, VRAM(RT(0)));
  sceGuTexSync(); texrt(0); spr(0, 0, N, N, CX(0), CY(0), N, N, WHITE);
}
static void xferVram(int f) {
  sceGuCopyImage(GU_PSM_8888, 0, 0, N, N, N, t8888, 0, 0, 128, VRAM(RT(0)));
  sceGuCopyImage(GU_PSM_8888, 0, 0, N, N, 128, VRAM(RT(0)), 0, 0, 128, VRAM(RT(1)));
  sceGuTexSync(); texrt(1); spr(0, 0, N, N, CX(0), CY(0), N, N, WHITE);
}
// The image is announced to the display before its list is sent
static void lateList(int f) {
  flip();
  begin(); t32(); cell(f / 8 % 12, WHITE); end();
}
// --- rejected today, with a known reason ---
static void sync(int f) {
  Vc *v = sceGuGetMemory(2 * sizeof(Vc));
  v[0] = (Vc){ WHITE, CX(0), CY(0), 0 }; v[1] = (Vc){ WHITE, CX(0) + N, CY(0) + N, 0 };
  sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_LINES, VC, 2, 0, v);
}
static void fan(int f) { sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_TRIANGLE_FAN, VC, 4, 0, qv(CX(0), CY(0), N, 0xFF0000FF, 0xFF00FF00)); }
static void points(int f) { sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_POINTS, VC, 4, 0, qv(CX(0), CY(0), N, WHITE, WHITE)); }
static void lineStrip(int f) { sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_LINE_STRIP, VC, 4, 0, qv(CX(0), CY(0), N, WHITE, WHITE)); }
static Vc *grid(int x, int y) {
  Vc *v = sceGuGetMemory(16 * sizeof(Vc));
  for (int i = 0; i < 16; i++) v[i] = (Vc){ 0xFF000000 | i * 0x111111, x + i % 4 * 40, y + i / 4 * 40, 0 };
  return v;
}
static void bezier(int f) { sceGuDisable(GU_TEXTURE_2D); sceGuDrawBezier(VC, 4, 4, 0, grid(CX(0), CY(0))); }
static void spline(int f) { sceGuDisable(GU_TEXTURE_2D); sceGuDrawSpline(VC, 4, 4, 3, 3, 0, grid(CX(0), CY(0))); }
// A 2D sprite drawn the way many menus do: orthographic matrix instead of through mode
static void ortho(int f) {
  static ScePspFMatrix4 id = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } },
                        pr = { { 2.0f / 480, 0, 0, 0 }, { 0, -2.0f / 272, 0, 0 }, { 0, 0, 1, 0 }, { -1, 1, 0, 1 } };
  sceGuSetMatrix(GU_PROJECTION, &pr); sceGuSetMatrix(GU_VIEW, &id); sceGuSetMatrix(GU_MODEL, &id);
  struct { float u, v; float x, y, z; } *b = sceGuGetMemory(2 * 20);
  b[0].u = b[0].v = 0; b[0].x = CX(0); b[0].y = CY(0); b[0].z = 0;
  b[1].u = b[1].v = 1; b[1].x = CX(0) + N; b[1].y = CY(0) + N; b[1].z = 0;
  t32(); sceGuEnable(GU_TEXTURE_2D); sceGuTexScale(1, 1); sceGuColor(WHITE);
  sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 2, 0, b);
}
// A lit, fogged, textured cube turning in perspective, with depth and back faces culled: the common 3D path
static void cube3d(int f) {
  static const signed char q[6][4][3] = { { {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1} }, { {1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1} },
    { {1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1} }, { {-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1} },
    { {-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1} }, { {-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1} } };
  static const signed char nr[6][3] = { {0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0} };
  struct V { float u, v, nx, ny, nz, x, y, z; } *b = sceGuGetMemory(36 * sizeof(struct V));
  static const int ix[6] = { 0, 1, 2, 0, 2, 3 };
  for (int s = 0; s < 6; s++) for (int k = 0; k < 6; k++) {
    const signed char *p = q[s][ix[k]];
    b[s * 6 + k] = (struct V){ ix[k] == 1 || ix[k] == 2, ix[k] >= 2, nr[s][0], nr[s][1], nr[s][2], p[0], p[1], p[2] };
  }
  float n = 1, ff = 20, t = 1.0f / 0.8f, a = f * 0.03f, cs = cosf(a), sn = sinf(a);
  ScePspFMatrix4 pr = { { t * 272 / 480, 0, 0, 0 }, { 0, t, 0, 0 }, { 0, 0, -(ff + n) / (ff - n), -1 }, { 0, 0, -2 * ff * n / (ff - n), 0 } },
                 vw = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, -5, 1 } },
                 md = { { cs, 0, -sn, 0 }, { 0, 1, 0, 0 }, { sn, 0, cs, 0 }, { 0, 0, 0, 1 } };
  sceGuSetMatrix(GU_PROJECTION, &pr); sceGuSetMatrix(GU_VIEW, &vw); sceGuSetMatrix(GU_MODEL, &md);
  ScePspFVector3 lp = { 0.5f, 0.5f, 1 };
  sceGuEnable(GU_DEPTH_TEST); sceGuDepthFunc(GU_GEQUAL); sceGuDepthRange(65535, 0); sceGuClearDepth(0); sceGuClear(GU_DEPTH_BUFFER_BIT);
  sceGuEnable(GU_CULL_FACE); sceGuFrontFace(GU_CW);
  sceGuEnable(GU_LIGHTING); sceGuEnable(GU_LIGHT0); sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &lp);
  sceGuLightColor(0, GU_DIFFUSE, WHITE); sceGuAmbient(0xFF404040); sceGuColor(WHITE);
  sceGuEnable(GU_FOG); sceGuFog(4, 8, 0xFF804020);
  t32(); sceGuEnable(GU_TEXTURE_2D); sceGuTexScale(1, 1); sceGuTexOffset(0, 0); sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
  sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 36, 0, b);
  sceGuDisable(GU_FOG); sceGuDisable(GU_LIGHTING); sceGuDisable(GU_LIGHT0); sceGuDisable(GU_CULL_FACE); sceGuDisable(GU_DEPTH_TEST);
}
static void weights(int f) {
  struct { float w; u32 c; s16 x, y, z, pad; } *v = sceGuGetMemory(2 * 16);
  v[0].w = 1; v[0].c = WHITE; v[0].x = CX(0); v[0].y = CY(0); v[0].z = 0; v[1] = v[0]; v[1].x += N; v[1].y += N;
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDrawArray(GU_SPRITES, GU_WEIGHT_32BITF | GU_WEIGHTS(1) | VC, 2, 0, v);
}
static void morph(int f) {
  Vc *v = sceGuGetMemory(4 * sizeof(Vc));
  v[0] = (Vc){ WHITE, CX(0), CY(0), 0 }; v[1] = v[0]; v[2] = (Vc){ WHITE, CX(0) + N, CY(0) + N, 0 }; v[3] = v[2];
  sceGuDisable(GU_TEXTURE_2D); sceGuMorphWeight(0, 0.5f); sceGuMorphWeight(1, 0.5f);
  sceGuDrawArray(GU_SPRITES, GU_VERTICES(2) | VC, 2, 0, v);
}
static void tfxOne(int m) { sceGuTexFunc(m, GU_TCC_RGBA); sceGuTexEnvColor(0x00FF00); t32(); cell(0, 0xFF0000FF); }
static void decal(int f) { tfxOne(GU_TFX_DECAL); }
static void tfxBlend(int f) { tfxOne(GU_TFX_BLEND); }
static void tfxAdd(int f) { tfxOne(GU_TFX_ADD); }
static void dxt1(int f) { tex(GU_PSM_DXT1, 0, N, N, N, dxt); cell(0, WHITE); }
static void dxt3(int f) { tex(GU_PSM_DXT3, 0, N, N, N, dxt); cell(0, WHITE); }
static void dxt5(int f) { tex(GU_PSM_DXT5, 0, N, N, N, dxt); cell(0, WHITE); }
static void t16(int f) { clut(pal, GU_PSM_8888, 8, 0xFF, 0); tex(GU_PSM_T16, 0, N, N, N, t565); cell(0, WHITE); }
static void t32p(int f) { clut(pal, GU_PSM_8888, 8, 0xFF, 0); tex(GU_PSM_T32, 0, N, N, N, t8888); cell(0, WHITE); }
static void colorTest(int f) {  // chroma key: magenta out
  sceGuEnable(GU_COLOR_TEST); sceGuColorFunc(GU_NOTEQUAL, 0xFF00FF, 0xFFFFFF); rect(CX(0), CY(0), N, N, 0xFFFF00FF); rect(CX(1), CY(1), N, N, 0xFF00FF00);
}
static void logicOp(int f) { sceGuEnable(GU_COLOR_LOGIC_OP); sceGuLogicalOp(GU_XOR); t32(); cell(0, WHITE); }
static void fixGray(int f) { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0x808080, 0x404040); t32(); cell(0, WHITE); }
static void blendAbs(int f) { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ABS, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); t32(); cell(0, WHITE); }
static void blendDst(int f) {
  sceGuClearStencil(0xFF); sceGuScissor(CX(0), CY(0), CX(0) + 32, CY(0) + N); sceGuClear(GU_STENCIL_BUFFER_BIT); sceGuScissor(0, 0, 480, 272);
  sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, 4, 5, 0, 0); t32(); cell(0, WHITE);  // destination alpha: half the cell has it
}
static void blend2x(int f) { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, 6, 7, 0, 0); t32(); cell(0, 0x80FFFFFF); }  // 2 x source alpha
static void wideTarget(int f) { sceGuDrawBufferList(GU_PSM_8888, RT(0), 768); rect(0, 0, 32, 32, WHITE); targetFb(); }
static void threeRt(int f) {  // a bloom chain: more intermediate images than the translator keeps
  rtFill(0, 0xFF800000, GU_PSM_8888);
  for (int i = 1; i < 3; i++) { target(RT(i), GU_PSM_8888, 128); texrt(i - 1); spr(0, 0, 128, 128, 0, 0, 64, 64, WHITE); }
  targetFb(); texrt(2); spr(0, 0, 64, 64, CX(0), CY(0), 128, 128, WHITE);
}
static void rtReread(int f) {  // read an intermediate image and draw on it again in the same frame
  rtFill(0, 0xFF800000, GU_PSM_8888); targetFb();
  texrt(0); spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
  target(RT(0), GU_PSM_8888, 128); rect(0, 0, 128, 64, 0xFF00FFFF); targetFb();
  texrt(0); spr(0, 0, 128, 128, CX(2), CY(2), 128, 128, WHITE);
}
static void offscreen(int f) {  // sprites parked far outside: position over 4096
  t32(); spr(0, 0, N, N, 5000, -4127, N, N, WHITE); cell(0, WHITE);
}
static void manyDraws(int f) { for (int i = 0; i < 5000; i++) rect(i % 400, (i / 400) * 2 + 40, 1, 1, 0xFF000000 | i); }
static void manyTex(int f) {  // more textures per frame than the record keeps
  sceGuEnable(GU_TEXTURE_2D);
  for (int i = 0; i < 300; i++) { tex(GU_PSM_8888, 0, 8, 8, N, (const u8 *)t8888 + (i % 1024) * 16 + 4096); spr(0, 0, 8, 8, i % 50 * 8, 40 + i / 50 * 8, 8, 8, WHITE); }
}
static void tallTex(int f) {  // 512 rows, the most the GE takes
  tex(GU_PSM_8888, 0, N, 512, N, tall); spr(0, 0, N, 512, CX(0), 14, N, 200, WHITE);
}
// --- no code for them in the translator: the image tells what happens ---
static void depth2d(int f) {  // sprite priority by z, drawn front to back
  sceGuEnable(GU_DEPTH_TEST); sceGuDepthFunc(GU_GEQUAL); sceGuDepthMask(GU_FALSE);
  for (int i = 0; i < 3; i++) {
    Vc *v = sceGuGetMemory(2 * sizeof(Vc));
    v[0] = (Vc){ 0xFF0000FF << i * 8 | 0xFF000000, 60 + i * 30, 60 + i * 30, (3 - i) * 10000 }; v[1] = v[0]; v[1].x += 80; v[1].y += 80;
    sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_SPRITES, VC, 2, 0, v);
  }
}
static void stencilMask(int f) {  // circular minimap: stencil written by a shape, then masks the map
  sceGuEnable(GU_STENCIL_TEST); sceGuStencilFunc(GU_ALWAYS, 1, 1); sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE); sceGuPixelMask(0xFFFFFF);
  sceGuEnable(GU_ALPHA_TEST); sceGuAlphaFunc(GU_GREATER, 128, 255); t32(); spr(0, 0, N, N, 80, 60, 128, 128, WHITE);
  sceGuDisable(GU_ALPHA_TEST); sceGuPixelMask(0); sceGuStencilFunc(GU_EQUAL, 1, 1); sceGuStencilOp(GU_KEEP, GU_KEEP, GU_KEEP);
  rect(60, 40, 180, 180, 0xFF00FF00);
}
static void stencilAlpha(int f) {  // alpha left in an image by stencil writes, used as a mask by a later texture
  target(RT(0), GU_PSM_8888, 128); sceGuClearColor(0x00000000); sceGuClearStencil(0); sceGuClear(GU_COLOR_BUFFER_BIT | GU_STENCIL_BUFFER_BIT);
  sceGuEnable(GU_STENCIL_TEST); sceGuStencilFunc(GU_ALWAYS, 0xFF, 0xFF); sceGuStencilOp(GU_REPLACE, GU_REPLACE, GU_REPLACE);
  rect(32, 32, 64, 64, 0xFF00FF00); sceGuDisable(GU_STENCIL_TEST); targetFb();
  sceGuEnable(GU_BLEND); texrt(0); spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
}
static void mipLevel(int f) {  // font kept in a mip level: level 1 forced
  sceGuTexMode(GU_PSM_T8, 1, 0, 0); sceGuTexImage(0, N, N, N, tp8); sceGuTexImage(1, 32, 32, 32, mip1);
  clut(pal, GU_PSM_8888, 0, 0xFF, 0); sceGuTexLevelMode(GU_TEXTURE_CONST, 1); sceGuTexFlush(); cell(0, WHITE);
}
static void uvOffset(int f) { sceGuTexOffset(0.25f, 0.25f); sceGuTexScale(0.5f, 0.5f); t32(); cell(0, WHITE); }
static void dither(int f) {
  static const ScePspIMatrix4 m = { { -4, 0, -3, 1 }, { 2, -2, 3, -1 }, { -3, 1, -4, 0 }, { 3, -1, 2, -2 } };
  sceGuSetDither(&m); sceGuEnable(GU_DITHER); sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_TRIANGLE_STRIP, VC, 4, 0, qv(40, 20, 220, 0xFF202020, 0xFF505050));
}
static void fog(int f) { sceGuFog(0, 1, 0xFF00FF); sceGuEnable(GU_FOG); t32(); cell(0, WHITE); }
static void flat(int f) { sceGuShadeModel(GU_FLAT); quad(qv(CX(0), CY(0), N, 0xFF0000FF, 0xFF00FF00)); }
static void alphaMask(int f) { sceGuEnable(GU_ALPHA_TEST); sceGuAlphaFunc(GU_EQUAL, 0x80, 0x80); t32(); cell(0, WHITE); }
static void pixelMask(int f) {  // per-bit color mask: red not written
  t32(); sceGuPixelMask(0x0000FF); cell(0, WHITE); sceGuPixelMask(0x0F0F0F); cell(1, WHITE);
}
static void rectCorners(int f) {  // the 4 orders of the 2 corners of a rectangle
  t32(); sceGuEnable(GU_TEXTURE_2D);
  for (int i = 0; i < 4; i++) {
    int a = i & 1, b = i >> 1, x = CX(i), y = CY(i);
    Vt *v = sceGuGetMemory(2 * sizeof(Vt));
    v[0] = (Vt){ 0, 0, WHITE, x + a * N, y + b * N, 0 }; v[1] = (Vt){ N, N, WHITE, x + !a * N, y + !b * N, 0 };
    sceGuDrawArray(GU_SPRITES, VT, 2, 0, v);
  }
  spr(N, 0, 0, N, CX(4), CY(4), N, N, WHITE); spr(0, N, N, 0, CX(5), CY(5), N, N, WHITE);
}
static void scissorTri(int f) {  // scissor on triangles and strips, not only rectangles
  sceGuScissor(CX(0) + 8, CY(0) + 8, CX(0) + 40, CY(0) + 40); quad(qv(CX(0), CY(0), N, 0xFF0000FF, 0xFF00FF00));
  sceGuScissor(CX(1) + 8, CY(1) + 8, CX(1) + 40, CY(1) + 40); t32();
  Vt *v = sceGuGetMemory(3 * sizeof(Vt));
  v[0] = (Vt){ 0, 0, WHITE, CX(1), CY(1), 0 }; v[1] = (Vt){ N, 0, WHITE, CX(1) + N, CY(1), 0 }; v[2] = (Vt){ 0, N, WHITE, CX(1), CY(1) + N, 0 };
  sceGuEnable(GU_TEXTURE_2D); sceGuDrawArray(GU_TRIANGLES, VT, 3, 0, v);
}
static void scissorEmpty(int f) { sceGuScissor(100, 100, 100, 100); t32(); cell(0, WHITE); sceGuScissor(0, 0, 0, 0); cell(1, WHITE); }
static void regionClip(int f) { sceGuSendCommandi(0x15, 0); sceGuSendCommandi(0x16, (CX(0) + 32) | (CY(0) + 32) << 10); t32(); cell(0, WHITE); sceGuSendCommandi(0x16, 479 | 271 << 10); }
static void texStride(int f) {  // buffer wider than the texture: repeat wraps at the texture width, not at the buffer's
  sceGuTexWrap(GU_REPEAT, GU_REPEAT); tex(GU_PSM_8888, 0, 32, 32, N, t8888);
  spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
}
static void texNpot(int f) {  // odd buffer width: not a power of two
  tex(GU_PSM_8888, 0, 32, 32, 48, t8888); spr(0, 0, 32, 32, CX(0), CY(0), 96, 96, WHITE);
}
static void atlasBleed(int f) {  // linear filter on an atlas: thin outlines around portraits
  sceGuTexFilter(GU_LINEAR, GU_LINEAR); sceGuEnable(GU_BLEND); t32();
  spr(8, 8, 20, 56, CX(0), CY(0), 48, 192, WHITE); spr(20, 8, 40, 18, CX(1), CY(1), 160, 80, WHITE);
}
static void subRt(int f) {  // texture pointing inside an image: a part of it by address
  rtFill(0, 0xFF800000, GU_PSM_8888); targetFb();
  tex(GU_PSM_8888, 0, 128, 64, 128, (u8 *)VRAM(RT(0)) + 64 * 128 * 4); spr(0, 0, 128, 64, CX(0), CY(0), 128, 64, WHITE);
}
static void rtTall(int f) {  // image taller than the screen
  sceGuDrawBufferList(GU_PSM_8888, RT(0), 128); sceGuScissor(0, 0, 128, 512);
  rect(0, 0, 128, 512, 0xFF800000); rect(0, 400, 128, 64, 0xFF00FF00); targetFb();
  tex(GU_PSM_8888, 0, 128, 512, 128, VRAM(RT(0))); spr(0, 0, 128, 512, CX(0), 14, 64, 250, WHITE);
}
static void rtPrev(int f) {  // intermediate image drawn in the previous frame, not redrawn in this one
  if (!(f & 1)) rtFill(0, 0xFF800000 | (f & 255) << 8, GU_PSM_8888);
  targetFb(); texrt(0); spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
}
static void readFront(int f) {  // the shown image as a texture: motion blur
  sceGuEnable(GU_BLEND); sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB); tex(GU_PSM_8888, 0, 512, 512, FB_W, VRAM(frontBuf())); spr(4, 4, 484, 276, 0, 0, 480, 272, 0xC0FFFFFF);
  sceGuDisable(GU_TEXTURE_2D); rect(f * 4 % 440, 100, 32, 32, WHITE);
}
static void readBack(int f) {  // the image being drawn, read a moment later: a shrunk copy of itself
  t32(); cell(0, WHITE); sceGuTexSync(); tex(GU_PSM_8888, 0, 512, 512, FB_W, VRAM(backBuf())); spr(0, 0, 240, 130, 240, 130, 120, 68, WHITE);
}
static void selfBlur(int f) {  // drawing on an image while reading it
  rtFill(0, 0xFF800000, GU_PSM_8888); sceGuEnable(GU_BLEND); texrt(0); spr(2, 2, 126, 126, 0, 0, 128, 128, 0x80FFFFFF); targetFb(); sceGuDisable(GU_BLEND);
  texrt(0); spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
}
static void strideGap(int f) {  // a small image inside the unused columns of another
  target(RT(0), GU_PSM_8888, 256); sceGuClearColor(0xFF800000); sceGuClear(GU_COLOR_BUFFER_BIT);
  target((u8 *)RT(0) + 128 * 4, GU_PSM_8888, 256); sceGuClearColor(0xFF008000); sceGuClear(GU_COLOR_BUFFER_BIT); targetFb();
  tex(GU_PSM_8888, 0, 256, 128, 256, VRAM(RT(0))); spr(0, 0, 256, 128, CX(0), CY(0), 256, 128, WHITE);
}
static void overlapRt(int f) {  // two images on the same memory, the second moved some rows down
  rtFill(0, 0xFF800000, GU_PSM_8888); target((u8 *)RT(0) + 32 * 128 * 4, GU_PSM_8888, 128); rect(0, 0, 128, 16, 0xFFFFFFFF); targetFb();
  texrt(0); spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
}
static void cast565(int f) {  // drawn as 565, read as 4444
  rtFill(0, 0xFF808080, GU_PSM_5650); targetFb();
  sceGuEnable(GU_BLEND); tex(GU_PSM_4444, 0, 128, 128, 128, VRAM(RT(0))); spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
}
static void castT8(int f) {  // the bytes of a 16-bit image read as palette indices, 2 texels per pixel
  rtFill(0, 0xFF808080, GU_PSM_5650); targetFb();
  clut(pal, GU_PSM_8888, 0, 0xFF, 0); tex(GU_PSM_T8, 0, 256, 128, 256, VRAM(RT(0))); spr(0, 0, 256, 128, CX(0), CY(0), 128, 128, WHITE);
}
static void castT16(int f) {  // a 565 image read as 16-bit indices: green channel through a palette
  rtFill(0, 0xFF808080, GU_PSM_5650); targetFb();
  clut(pal, GU_PSM_8888, 5, 0x3F, 0); tex(GU_PSM_T16, 0, 128, 128, 128, VRAM(RT(0))); spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
}
static void blueAlpha(int f) {  // the alpha of a 4444 image written drawing in 565 with only blue's top bits open
  sceGuCopyImage(GU_PSM_4444, 0, 0, N, N, N, t4444, 0, 0, 128, VRAM(RT(0)));
  target(RT(0), GU_PSM_5650, 128); sceGuPixelMask(0x0FFFFF); rect(0, 0, N, N, 0xFF800000); rect(0, 0, 32, N, 0xFFFF0000); sceGuPixelMask(0); targetFb();
  sceGuEnable(GU_BLEND); tex(GU_PSM_4444, 0, 128, 128, 128, VRAM(RT(0))); spr(0, 0, N, N, CX(0), CY(0), N, N, WHITE);
}
static void paletteRt(int f) {  // palette drawn by the GE: a one-row image loaded as CLUT
  target(RT(0), GU_PSM_8888, 128); for (int i = 0; i < 16; i++) rect(i, 0, 1, 1, 0xFF000000 | ((i * 17 + f * 2) & 255) << 8);
  targetFb(); clut(VRAM(RT(0)), GU_PSM_8888, 0, 0x0F, 0); tex(GU_PSM_T4, 0, N, N, N, tp4); cell(0, WHITE);
}
static void clutVram(int f) {
  sceGuCopyImage(GU_PSM_8888, 0, 0, 256, 1, 256, pal, 0, 0, 256, VRAM(RT(0)));
  sceGuTexSync(); clut(VRAM(RT(0)), GU_PSM_8888, 0, 0xFF, 0); tex(GU_PSM_T8, 0, N, N, N, tp8); cell(0, WHITE);
}
static void clutStale(int f) {  // palette memory changes with no new load: the GE keeps using the one it loaded
  for (int i = 0; i < 16; i++) rampal[i] = f < 60 ? 0xFF00FF00 : 0xFFFF0000;
  sceKernelDcacheWritebackRange(rampal, sizeof rampal);
  sceGuClutMode(GU_PSM_8888, 0, 0x0F, 0);
  if (!f) sceGuClutLoad(2, rampal);
  tex(GU_PSM_T4, 0, N, N, N, tp4); cell(0, WHITE);
}
static void clutTwice(int f) {  // same texture, palette swapped between draws: fades and team colors
  tex(GU_PSM_T8, 0, N, N, N, tp8); clut(pal, GU_PSM_8888, 0, 0xFF, 0); cell(0, WHITE); clut(pal2, GU_PSM_8888, 0, 0xFF, 0); cell(1, WHITE);
}
static void texTwice(int f) {  // same address, new content for each draw of a frame: fonts built on the fly
  for (int k = 0; k < 2; k++) {
    for (int i = 0; i < N * N; i++) t_c[i] = k ? 0xFF0000FF : t8888[i];
    sceKernelDcacheWritebackRange(t_c, sizeof t_c);
    tex(GU_PSM_8888, 0, N, N, N, t_c); cell(k, WHITE); relist(); targetFb();
  }
}
static void xferTwice(int f) {  // the same, the new content coming by a block copy in the same list
  for (int k = 0; k < 3; k++) {
    sceGuCopyImage(GU_PSM_8888, k * 8, 0, 16, 16, N, t8888, 0, 0, 128, VRAM(RT(0)));
    sceGuTexSync(); texrt(0); spr(0, 0, 16, 16, CX(k), CY(k), N, N, WHITE);
  }
}
static void vtxTwice(int f) {  // one vertex buffer rewritten between lists
  for (int k = 0; k < 2; k++) {
    vbuf[0] = (Vc){ 0xFF0000FF << k * 8 | 0xFF000000, CX(k), CY(k), 0 }; vbuf[1] = (Vc){ vbuf[0].c, CX(k) + N, CY(k) + N, 0 };
    sceKernelDcacheWritebackRange(vbuf, sizeof vbuf);
    sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_SPRITES, VC, 2, 0, vbuf); relist(); targetFb();
  }
}
static void vaddrKeep(int f) {  // second draw with no vertex address: the GE goes on from where it stopped
  Vc *v = sceGuGetMemory(4 * sizeof(Vc));
  v[0] = (Vc){ 0xFF0000FF, CX(0), CY(0), 0 }; v[1] = (Vc){ 0xFF0000FF, CX(0) + N, CY(0) + N, 0 };
  v[2] = (Vc){ 0xFF00FF00, CX(1), CY(1), 0 }; v[3] = (Vc){ 0xFF00FF00, CX(1) + N, CY(1) + N, 0 };
  sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_SPRITES, VC, 2, 0, v); sceGuDrawArray(GU_SPRITES, VC, 2, 0, 0);
}
static void primKeep(int f) {  // primitive type 7: keeps the previous one
  Vc *v = sceGuGetMemory(4 * sizeof(Vc));
  v[0] = (Vc){ 0xFF0000FF, CX(0), CY(0), 0 }; v[1] = (Vc){ 0xFF0000FF, CX(0) + N, CY(0) + N, 0 };
  v[2] = (Vc){ 0xFF00FF00, CX(1), CY(1), 0 }; v[3] = (Vc){ 0xFF00FF00, CX(1) + N, CY(1) + N, 0 };
  sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_SPRITES, VC, 2, 0, v); sceGuDrawArray(7, VC, 2, 0, 0);
}
static void rectOdd(int f) {  // odd vertex count; zero-size rectangle
  sceGuDisable(GU_TEXTURE_2D); Vc *v = sceGuGetMemory(5 * sizeof(Vc));
  v[0] = (Vc){ WHITE, CX(0), CY(0), 0 }; v[1] = (Vc){ WHITE, CX(0) + N, CY(0) + N, 0 }; v[2] = (Vc){ WHITE, 200, 200, 0 }; v[3] = v[2]; v[4] = (Vc){ WHITE, 300, 200, 0 };
  sceGuDrawArray(GU_SPRITES, VC, 5, 0, v);
}
static void sublist(int f) {  // a list kept between frames, with its color word patched at frame 60
  static const int vt = GU_VERTEX_16BIT | GU_TRANSFORM_2D;
  if (!f) {
    sub_v[0] = CX(0); sub_v[1] = CY(0); sub_v[2] = 0; sub_v[3] = CX(0) + N; sub_v[4] = CY(0) + N; sub_v[5] = 0;
    sub[0] = 0x55 << 24 | 0x0000FF; sub[1] = 0x12 << 24 | vt; sub[2] = 0x10 << 24 | ((u32)sub_v >> 8 & 0xFF0000);
    sub[3] = 0x01 << 24 | ((u32)sub_v & 0xFFFFFF); sub[4] = 0x04 << 24 | 6 << 16 | 2; sub[5] = 0x0B << 24;
  }
  if (f == 60) sub[0] = 0x55 << 24 | 0x00FF00;
  sceKernelDcacheWritebackAll();
  sceGuDisable(GU_TEXTURE_2D); sceGuColor(WHITE); sceGuCallList(sub);
}
static void bboxJump(int f) {  // object outside the screen: the GE jumps over its draws (red must not show)
  static ScePspFMatrix4 id = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
  sceGuSetMatrix(GU_PROJECTION, &id); sceGuSetMatrix(GU_VIEW, &id); sceGuSetMatrix(GU_MODEL, &id);
  static struct { float x, y, z; } A16 box[8];
  for (int i = 0; i < 8; i++) { box[i].x = 50 + (i & 1); box[i].y = i >> 1 & 1; box[i].z = i >> 2; }
  sceKernelDcacheWritebackRange(box, sizeof box);
  sceGuBeginObject(GU_VERTEX_32BITF, 8, 0, box);
  rect(CX(0), CY(0), N, N, 0xFF0000FF);
  sceGuEndObject();
  rect(CX(1), CY(1), N, N, 0xFF00FF00);
}
static void xferFb(int f) {  // block copy over the image already cleared: backgrounds uploaded from RAM
  sceGuCopyImage(GU_PSM_8888, 0, 0, N, N, N, t8888, CX(0), CY(0), FB_W, VRAM(backBuf()));
  sceGuTexSync(); rect(CX(1), CY(1), N, N, 0xFF00FF00);
}
static void xferIntra(int f) {  // part of the image copied to another place of it
  t32(); cell(0, WHITE); sceGuTexSync();
  sceGuCopyImage(GU_PSM_8888, CX(0), CY(0), N, N, FB_W, VRAM(backBuf()), 240, 100, FB_W, VRAM(backBuf()));
  sceGuTexSync();
}
static void xferSelf(int f) {  // overlapping copy: scrolling the image over itself
  t32(); cell(0, WHITE); sceGuTexSync();
  sceGuCopyImage(GU_PSM_8888, CX(0), CY(0), N, N, FB_W, VRAM(backBuf()), CX(0) + 16, CY(0) + 8, FB_W, VRAM(backBuf()));
  sceGuTexSync();
}
static void texMirror(int f) {  // texture read through another address of the same VRAM
  rtFill(0, 0xFF800000, GU_PSM_8888); targetFb();
  tex(GU_PSM_8888, 0, 128, 128, 128, (u8 *)RT(0) + 0x04400000); spr(0, 0, 128, 128, CX(0), CY(0), 128, 128, WHITE);
}
static void depthTex(int f) {  // the depth buffer as a texture through its unscrambled mirror: fog, depth of field
  sceGuEnable(GU_DEPTH_TEST); sceGuDepthFunc(GU_ALWAYS); sceGuDepthMask(GU_FALSE); sceGuDisable(GU_TEXTURE_2D);
  Vc *v = sceGuGetMemory(4 * sizeof(Vc));
  v[0] = (Vc){ WHITE, 40, 40, 0 }; v[1] = (Vc){ WHITE, 240, 40, 0xFFFF }; v[2] = (Vc){ WHITE, 40, 240, 0 }; v[3] = (Vc){ WHITE, 240, 240, 0xFFFF };
  sceGuDrawArray(GU_TRIANGLE_STRIP, VC, 4, 0, v);
  sceGuDisable(GU_DEPTH_TEST); sceGuDepthMask(GU_TRUE); sceGuTexSync();
  tex(GU_PSM_5650, 0, 512, 512, FB_W, (u8 *)ZB + 0x04600000); spr(0, 0, 480, 272, 250, 40, 200, 200, WHITE);
}
static void cpuRead(int f) {  // the program reads depth from VRAM: lens flare hidden behind a wall
  rect(100, 100, 40, 40, WHITE);
  relist(); targetFb();
  u32 c = *(volatile u32 *)(0x44000000 | ((u32)backBuf() + (120 * FB_W + 120) * 4));
  rect(CX(5), CY(5), N, N, (c & 0xFF) > 200 ? 0xFF00FF00 : 0xFF0000FF);
}
static void cpuPixels(int f) {  // video: pixels written by the CPU, then a list with the HUD on top
  relist(); targetFb();
  u32 *p = (u32 *)(0x44000000 | (u32)backBuf());
  for (int y = 40; y < 200; y++) for (int x = 40; x < 300; x++) p[y * FB_W + x] = 0xFF000000 | ((x + f * 2) & 255) | ((y + f) & 255) << 8;
  t32(); cell(0, WHITE);
}
static void noClear(int f) {  // only what changed is drawn (the last 2 steps: there are 2 images)
  for (int k = f > 0 ? f - 1 : 0; k <= f; k++) rect(k * 4 % 440, 40 + k / 8 * 8, 8, 8, 0xFF000000 | k * 0x030507);
}
static void fps30(int f) { sceDisplayWaitVblankStart(); t32(); cell(f % 12, WHITE); }  // one flip every 2 vblanks
static void singleBuf(int f) {  // one image, never flipped: everything drawn on the shown one
  begin(); t32(); cell(f / 8 % 12, WHITE); end(); sceDisplayWaitVblankStart();
  sceDisplaySetFrameBuf(VRAM(backBuf()), FB_W, GU_PSM_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
}
static void shake(int f) {  // screen shake by moving the shown address, nothing redrawn
  begin(); t32(); cell(0, WHITE); end(); sceDisplayWaitVblankStart();
  sceDisplaySetFrameBuf((u8 *)VRAM(backBuf()) + (f & 8 ? 8 : 0) * FB_W * 4, FB_W, GU_PSM_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
}
static u32 A16 ramfb[FB_W * 272];
static void ramShown(int f) {  // the shown image lives in RAM: video players
  for (int y = 0; y < 272; y++) for (int x = 0; x < 480; x++) ramfb[y * FB_W + x] = 0xFF000000 | ((x + f) & 255) | (y & 255) << 8;
  sceKernelDcacheWritebackAll(); sceDisplayWaitVblankStart();
  sceDisplaySetFrameBuf(ramfb, FB_W, GU_PSM_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
}
static void cache1(int f) {  // a texture with one texel changed at frame 60, in its last row
  if (!f) memcpy(t_c, t8888, sizeof t_c);
  if (f == 60) t_c[N * N - 1] = 0xFF0000FF;
  sceKernelDcacheWritebackRange(t_c, sizeof t_c);
  tex(GU_PSM_8888, 0, N, N, N, t_c); spr(0, 0, N, N, CX(0), CY(0), 192, 192, WHITE);
}
static void cache2(int f) {  // the whole texture changes at frame 60
  for (int i = 0; i < N * N; i++) t_c[i] = f < 60 ? t8888[i] : ~t8888[i] | 0xFF000000;
  sceKernelDcacheWritebackRange(t_c, sizeof t_c);
  tex(GU_PSM_8888, 0, N, N, N, t_c); cell(0, WHITE);
}

#define S(n, e) { #n, e, n, 0, GU_PSM_8888 }
const Scene scenes[] = {
  { "firstState", "ok", firstState, F_RAW, GU_PSM_8888 },  // must stay first: it tests the first lists of the program
  S(sync, "ok"),  // lines, used to line up the logs
  S(clearS, "ok"), S(prims, "ok"), S(fan, "ok"), S(vertFmts, "ok"), S(texFmts, "ok"), S(texSwz, "ok"), S(filterWrap, "ok"), S(palettes, "ok"),
  S(texFuncs, "ok"), S(blendOps, "ok"), S(blendFactors, "ok"), S(alphaTest, "ok"), S(scissorRect, "ok"), S(layers, "ok"),
  S(rtOne, "ok"), S(rtTwo, "ok"), S(rtFmts, "ok"), S(xferRam, "ok"), S(xferVram, "ok"),
  { "fb565", "ok", fbPlain, 0, GU_PSM_5650 }, { "fb5551", "ok", fbPlain, 0, GU_PSM_5551 }, { "lateList", "ok", lateList, F_SELF, GU_PSM_8888 },
  S(points, "primitive"), S(lineStrip, "ok"), S(bezier, "?"), S(spline, "?"), S(ortho, "ok"), S(cube3d, "ok"),
  S(weights, "vertex"), S(morph, "vertex"), S(decal, "ok"), S(tfxBlend, "ok"), S(tfxAdd, "ok"),
  S(dxt1, "format"), S(dxt3, "format"), S(dxt5, "format"), S(t16, "format"), S(t32p, "format"),
  S(colorTest, "color_test_or_logic"), S(logicOp, "color_test_or_logic"), S(fixGray, "blend"), S(blendAbs, "blend"), S(blendDst, "blend"), S(blend2x, "ok"),
  S(wideTarget, "other_target"), S(threeRt, "other_target"), S(rtReread, "?"), S(offscreen, "vertex_out"), S(manyDraws, "?"), S(manyTex, "?"), S(tallTex, "?"),
  S(depth2d, "?"), S(stencilMask, "?"), S(stencilAlpha, "?"), S(mipLevel, "?"), S(uvOffset, "?"), { "dither", "?", dither, 0, GU_PSM_5650 }, S(fog, "?"), S(flat, "?"),
  S(alphaMask, "?"), S(pixelMask, "?"), S(rectCorners, "?"), S(scissorTri, "?"), S(scissorEmpty, "?"), S(regionClip, "?"), S(texStride, "?"), S(texNpot, "?"),
  S(atlasBleed, "?"), { "fb4444", "?", fbPlain, 0, GU_PSM_4444 }, S(subRt, "?"), S(rtTall, "?"), S(rtPrev, "?"), S(readFront, "?"), S(readBack, "?"), S(selfBlur, "?"),
  S(strideGap, "?"), S(overlapRt, "?"), S(cast565, "?"), S(castT8, "?"), S(castT16, "?"), S(blueAlpha, "?"), S(paletteRt, "?"), S(clutVram, "?"), S(clutStale, "?"),
  S(clutTwice, "?"), S(texTwice, "?"), S(xferTwice, "?"), S(vtxTwice, "?"), S(vaddrKeep, "?"), S(primKeep, "?"), S(rectOdd, "?"), S(sublist, "?"), S(bboxJump, "?"),
  S(xferFb, "?"), S(xferIntra, "?"), S(xferSelf, "?"), S(texMirror, "?"), S(depthTex, "?"), S(cpuRead, "?"), S(cpuPixels, "?"),
  { "noClear", "?", noClear, F_NOBG, GU_PSM_8888 }, S(fps30, "?"), { "singleBuf", "?", singleBuf, F_SELF, GU_PSM_8888 }, { "shake", "?", shake, F_SELF, GU_PSM_8888 },
  { "ramShown", "?", ramShown, F_SELF, GU_PSM_8888 }, S(cache1, "?"), S(cache2, "?"),
};
const int nscenes = sizeof scenes / sizeof *scenes;
