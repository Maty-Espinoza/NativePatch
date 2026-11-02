// NPTEST: homebrew that draws one technique per scene, to compare NativeDraw's image with the PSP's and to read the
// reason of each frame that falls to the normal image. Scenes (scenes.c) run in order for 2 s each and the app exits.
// Buttons: RIGHT/LEFT next/previous, CROSS pause on a scene (for a L+R+SELECT dump), START exit. Each scene shows its
// number top right and as a bar code on the bottom line (8 bits, usable in dumps). ms0:/native_test.log gets one line per
// scene: number, name, expected reason, first frame, and a hash of the PSP image at frames 30 and 119 (they must not
// change between versions of the plugins, or with the plugins off).
#include "nptest.h"
#include <pspctrl.h>
#include <stdio.h>
PSP_MODULE_INFO("NPTEST", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(1024);

static u32 __attribute__((aligned(16))) list[0x40000];
static void *fbs[2] = { (void *)0, (void *)0x88000 };
static int back, cur;
u32 fb_psm = GU_PSM_8888;

void *backBuf(void) { return fbs[back]; }
void *frontBuf(void) { return fbs[!back]; }
void target(void *p, int psm, int stride) { sceGuDrawBufferList(psm, p, stride); sceGuScissor(0, 0, 128, 128); }
void targetFb(void) { sceGuDrawBufferList(fb_psm, fbs[back], FB_W); sceGuScissor(0, 0, 480, 272); }
void relist(void) { sceGuFinish(); sceGuSync(0, 0); sceGuStart(GU_DIRECT, list); }

void rect(int x, int y, int w, int h, u32 c) {
  Vc *v = sceGuGetMemory(2 * sizeof(Vc));
  v[0] = (Vc){ c, x, y, 0 }; v[1] = (Vc){ c, x + w, y + h, 0 };
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDrawArray(GU_SPRITES, VC, 2, 0, v);
}
void spr(int u0, int v0, int u1, int v1, int x, int y, int w, int h, u32 c) {
  Vt *v = sceGuGetMemory(2 * sizeof(Vt));
  v[0] = (Vt){ u0, v0, c, x, y, 0 }; v[1] = (Vt){ u1, v1, c, x + w, y + h, 0 };
  sceGuEnable(GU_TEXTURE_2D);
  sceGuDrawArray(GU_SPRITES, VT, 2, 0, v);
}
void tex(int psm, int swz, int w, int h, int tbw, const void *p) {
  sceGuTexMode(psm, 0, 0, swz);
  sceGuTexImage(0, w, h, tbw, p);
  sceGuTexFlush();
}
// Every switch a scene may leave on, back to the plain 2D state
static void reset(void) {
  static const int off[] = { GU_BLEND, GU_ALPHA_TEST, GU_DEPTH_TEST, GU_STENCIL_TEST, GU_COLOR_TEST, GU_COLOR_LOGIC_OP,
                             GU_DITHER, GU_FOG, GU_LIGHTING, GU_CULL_FACE, GU_TEXTURE_2D };
  for (int i = 0; i < sizeof off / sizeof *off; i++) sceGuDisable(off[i]);
  sceGuEnable(GU_SCISSOR_TEST);
  targetFb();
  sceGuPixelMask(0); sceGuDepthMask(GU_TRUE); sceGuShadeModel(GU_SMOOTH);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST); sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  sceGuTexOffset(0, 0); sceGuTexScale(1, 1); sceGuTexLevelMode(GU_TEXTURE_AUTO, 0);
  sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
}
// Digits of 3x5 cells (15 bits, top row first)
static void digits(int n) {
  static const u16 d[10] = { 0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF };
  for (int k = 0, x = 460; k < 3; k++, n /= 10, x -= 16)
    for (int b = 0; b < 15; b++) rect(x + b % 3 * 4, 2 + b / 3 * 4, 4, 4, d[n % 10] >> (14 - b) & 1 ? 0xFF00FFFF : 0xFF000000);
}
void begin(void) {
  sceGuStart(GU_DIRECT, list);
  const Scene *s = &scenes[cur];
  if (s->flags & F_RAW) return;
  reset();
  if (s->flags & F_NOBG) return;
  sceGuClearColor(0xFF402020); sceGuClearDepth(0); sceGuClearStencil(0);
  sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_STENCIL_BUFFER_BIT);
  for (int i = 0; i < 8; i++) rect(0, i * 34, 480, 17, 0xFF607080);
}
void end(void) {
  if (!(scenes[cur].flags & F_RAW)) reset();
  digits(cur);
  for (int b = 0; b < 8; b++) rect(b * 8, 268, 8, 4, cur >> b & 1 ? WHITE : 0xFF000000);
  sceGuFinish();
  sceGuSync(0, 0);
}
void flip(void) {
  sceDisplayWaitVblankStart();
  sceDisplaySetFrameBuf(VRAM(fbs[back]), FB_W, fb_psm, PSP_DISPLAY_SETBUF_NEXTFRAME);
  back ^= 1;
}
// FNV-1a of the image just drawn
static u32 hash(void) {
  int n = fb_psm == GU_PSM_8888 ? 4 : 2;
  u32 h = 2166136261u;
  for (int y = 0; y < 272; y++) {
    const u8 *p = (const u8 *)(0x44000000 | (u32)fbs[back]) + y * FB_W * n;
    for (int x = 0; x < 480 * n; x++) h = (h ^ p[x]) * 16777619u;
  }
  return h;
}
static void logLine(const char *t) {
  SceUID fd = sceIoOpen("ms0:/native_test.log", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
  if (fd >= 0) { sceIoWrite(fd, t, strlen(t)); sceIoClose(fd); }
}

int main(void) {
  sceIoRemove("ms0:/native_test.log");
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
  sceGuInit();
  sceGuStart(GU_DIRECT, list);
  sceGuDrawBuffer(GU_PSM_8888, fbs[0], FB_W);
  sceGuDispBuffer(480, 272, fbs[1], FB_W);
  sceGuDepthBuffer(ZB, FB_W);
  sceGuOffset(2048 - 240, 2048 - 136);
  sceGuViewport(2048, 2048, 480, 272);
  sceGuDepthRange(65535, 0);
  sceGuPatchDivide(4, 4);
  sceGuFinish();
  sceGuSync(0, 0);
  sceGuDisplay(GU_TRUE);
  u32 old = 0, frame = 0;
  int paused = 0;
  for (cur = 0; cur < nscenes && cur >= 0; ) {
    const Scene *s = &scenes[cur];
    u32 h0 = 0, h1 = 0, first = frame;
    int step = 1, f = 0;
    fb_psm = s->psm;
    for (; f < 120 || paused; f++, frame++) {
      if (!(s->flags & F_SELF)) { begin(); s->fn(f); end(); }
      else s->fn(f);
      if (f == 30) h0 = hash();
      if (f == 119) h1 = hash();
      if (!(s->flags & F_SELF)) flip();
      SceCtrlData c;
      sceCtrlPeekBufferPositive(&c, 1);
      u32 press = c.Buttons & ~old;
      old = c.Buttons;
      if (press & PSP_CTRL_CROSS) paused ^= 1;
      if (press & PSP_CTRL_START) step = 1000;
      if (press & PSP_CTRL_LEFT) step = -1;
      if (press & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT | PSP_CTRL_START)) break;
    }
    char t[120];
    snprintf(t, sizeof t, "%d %s %s %lu %08lx %08lx\n", cur, s->name, s->expect, (unsigned long)first, (unsigned long)h0, (unsigned long)h1);
    logLine(t);
    cur += step;
    if (cur < 0) cur = 0;
  }
  sceGuTerm();
  sceKernelExitGame();
  return 0;
}
