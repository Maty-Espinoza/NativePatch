// GEPVM: escena determinista para comparar la Vita (Adrenaline + GePatch) con el modelo
// (PPSSPP con PPSSPP_ADRENALINE=1 y el mismo ge_patch.prx). Cada frame depende solo de su numero.
// Tiene lo que a GePatch le cuesta: render a textura, mezcla alfa, textura con paleta, copia de
// bloque dentro del framebuffer, 3D con profundidad y un recuadro dibujado por la CPU.
// En los frames de CAPTURAS, kcap.prx (kernel) guarda en ms0:/gepvm/ la imagen nativa y la del PSP.
// Va en ms0:/PSP/GAME/GEPVM/ (EBOOT.PBP y kcap.prx). Termina sola tras el ultimo frame.
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspctrl.h>
#include <string.h>
#include <math.h>

PSP_MODULE_INFO("GEPVM", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);

int kcapGuardar(const char *dir, int frame);  // kcap.prx

#define BUF_W 512
#define SCR_W 480
#define SCR_H 272
#define FB0 ((void *)0)
#define FB1 ((void *)0x88000)
#define ZB ((void *)0x110000)
#define RT ((void *)0x154000)  // 128x128 8888: render a textura
#define RT_W 128
#define RT2 ((void *)0x164000) // 64x64 5551: render target despues de la escena (como Ridge Racer)
#define RT2_W 64
static const int CAPTURAS[] = { 5, 30, 60, 90, 120 };
#define N_CAPT (sizeof(CAPTURAS) / sizeof(CAPTURAS[0]))

static unsigned int __attribute__((aligned(16))) list[0x10000];
static u32 __attribute__((aligned(16))) checker[64 * 64];
static u8 __attribute__((aligned(16))) idx8[32 * 32];
static u32 __attribute__((aligned(16))) clut[256];

typedef struct { u16 u, v; u32 color; s16 x, y, z; } VT;   // through: textura + color + posicion
typedef struct { u32 color; s16 x, y, z; } VC;
typedef struct { float u, v; u32 color; float x, y, z; } V3;

static void makeTextures(void) {
  int x, y;
  for (y = 0; y < 64; y++)
    for (x = 0; x < 64; x++)
      checker[y * 64 + x] = ((x ^ y) & 8) ? 0xC0FFFFFF : (0x80000000 | (x * 4) << 8 | (y * 4));
  for (y = 0; y < 32; y++)
    for (x = 0; x < 32; x++)
      idx8[y * 32 + x] = (u8)((x * x + y * y) >> 3);
  for (x = 0; x < 256; x++)
    clut[x] = 0xFF000000 | ((x * 7) & 0xff) << 16 | ((255 - x) << 8) | (x ^ 0x5a);
  sceKernelDcacheWritebackAll();
}

static void sprite(VT *v, int x, int y, int w, int h, int tw, int th, u32 c) {
  v[0].u = 0; v[0].v = 0; v[0].x = x; v[0].y = y; v[0].z = 0; v[0].color = c;
  v[1].u = tw; v[1].v = th; v[1].x = x + w; v[1].y = y + h; v[1].z = 0; v[1].color = c;
}

// Pase 1: triangulos girando en el render target
static void renderTarget(int f) {
  sceGuDrawBufferList(GU_PSM_8888, RT, RT_W);
  sceGuOffset(2048 - RT_W / 2, 2048 - RT_W / 2);
  sceGuViewport(2048, 2048, RT_W, RT_W);
  sceGuScissor(0, 0, RT_W, RT_W);
  sceGuClearColor(0xFF000000 | (f * 3 & 0xff) << 16 | 0x2040);
  sceGuClear(GU_COLOR_BUFFER_BIT);
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDisable(GU_DEPTH_TEST);
  int i;
  VC *v = sceGuGetMemory(3 * 6 * sizeof(VC));
  for (i = 0; i < 6; i++) {
    float a = f * 0.05f + i * 1.0471976f;
    VC *t = v + i * 3;
    t[0].x = 64; t[0].y = 64; t[0].z = 0; t[0].color = 0xFFFFFFFF;
    t[1].x = 64 + (s16)(60 * cosf(a)); t[1].y = 64 + (s16)(60 * sinf(a)); t[1].z = 0; t[1].color = 0xFF0000FF << (i & 1) * 8 | 0xFF000000;
    t[2].x = 64 + (s16)(60 * cosf(a + 0.5f)); t[2].y = 64 + (s16)(60 * sinf(a + 0.5f)); t[2].z = 0; t[2].color = 0xFF00FF00 | i * 40;
  }
  sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 18, 0, v);
}

// Pase 2 (v3): despues de la escena, con la profundidad del buffer grande ya usada (Ridge
// Racer dibuja asi su mapa de entorno). Usa su propia profundidad.
static void renderTarget2(int f, void *screen) {
  sceGuDrawBufferList(GU_PSM_5551, RT2, RT2_W);
  sceGuOffset(2048 - RT2_W / 2, 2048 - RT2_W / 2);
  sceGuViewport(2048, 2048, RT2_W, RT2_W);
  sceGuScissor(0, 0, RT2_W, RT2_W);
  sceGuClearColor(0xFF00A0FF);
  sceGuClearDepth(0);
  // v5: se borra entero solo en el frame 1; despues solo la mitad de arriba. Abajo queda lo de
  // los frames anteriores (en la PSP el fb sigue en la VRAM) y se acumula una estela
  if (f > 1)
    sceGuScissor(0, 0, RT2_W, 32);
  sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
  sceGuScissor(0, 0, RT2_W, RT2_W);
  sceGuDisable(GU_TEXTURE_2D);
  VC *tr = sceGuGetMemory(2 * sizeof(VC));
  tr[0].x = (f * 3) & 63; tr[0].y = 54; tr[0].z = 0; tr[0].color = 0xFF000000 | (f * 0x050301);
  tr[1].x = tr[0].x + 3; tr[1].y = 62; tr[1].z = 0; tr[1].color = tr[0].color;
  sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, tr);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDepthFunc(GU_GEQUAL);
  VC *v = sceGuGetMemory(6 * sizeof(VC));
  int i;
  for (i = 0; i < 2; i++) { // dos triangulos que se cruzan en profundidad
    VC *t = v + i * 3;
    int o = (f * (i ? 3 : -2)) & 31;
    t[0].x = o; t[0].y = 4; t[0].z = i ? 60000 : 1000; t[0].color = i ? 0xFF0000FF : 0xFF00FF00;
    t[1].x = 60 - o; t[1].y = 30; t[1].z = 30000; t[1].color = i ? 0xFF0000FF : 0xFF00FF00;
    t[2].x = o; t[2].y = 60; t[2].z = i ? 1000 : 60000; t[2].color = 0xFFFFFFFF;
  }
  sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 6, 0, v);
  sceGuDisable(GU_DEPTH_TEST);
  // v4: la pantalla de este frame reducida en una esquina (Ridge Racer arma asi su desenfoque)
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(GU_PSM_8888, 0, 0, 0);
  sceGuTexImage(0, 512, 512, BUF_W, (void *)(0x04000000 | (u32)screen));
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);
  sceGuTexFlush();
  struct { u16 u, v; s16 x, y, z; } *sp = sceGuGetMemory(4 * 10);
  sp[0].u = 0; sp[0].v = 0; sp[0].x = 32; sp[0].y = 32; sp[0].z = 0;
  sp[1].u = 480; sp[1].v = 272; sp[1].x = 64; sp[1].y = 50; sp[1].z = 0;
  sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, sp);
  sceGuDisable(GU_TEXTURE_2D);
}

// Cubo con un render target como textura (3D, profundidad)
static void cube(int f, float x, float z, float sc, int psm, void *tex, int tw) {
  static const float p[8][3] = { {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1}, {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} };
  static const int q[6][4] = { {0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {4,5,1,0}, {3,2,6,7} };
  V3 *v = sceGuGetMemory(36 * sizeof(V3));
  int i, k, n = 0;
  static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
  static const float uv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
  for (i = 0; i < 6; i++)
    for (k = 0; k < 6; k++, n++) {
      const float *pp = p[q[i][tri[k]]];
      v[n].u = uv[tri[k]][0]; v[n].v = uv[tri[k]][1];
      v[n].color = 0xFFFFFFFF - i * 0x00102010;
      v[n].x = pp[0]; v[n].y = pp[1]; v[n].z = pp[2];
    }
  sceGumMatrixMode(GU_PROJECTION);
  sceGumLoadIdentity();
  sceGumPerspective(60.0f, 480.0f / 272.0f, 0.5f, 100.0f);
  sceGumMatrixMode(GU_VIEW);
  sceGumLoadIdentity();
  sceGumMatrixMode(GU_MODEL);
  sceGumLoadIdentity();
  ScePspFVector3 pos = { x, x * -0.4f, z };
  ScePspFVector3 rot = { f * 0.021f, f * 0.033f, 0.0f };
  ScePspFVector3 scl = { sc, sc, sc };
  sceGumTranslate(&pos);
  sceGumRotateXYZ(&rot);
  sceGumScale(&scl);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDepthFunc(GU_GEQUAL);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(psm, 0, 0, 0);
  sceGuTexImage(0, tw, tw, tw, (void *)(0x04000000 | (u32)tex));
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
  sceGuTexFilter(GU_LINEAR, GU_LINEAR);
  sceGuTexScale(1.0f, 1.0f);
  sceGuTexOffset(0.0f, 0.0f);
  sceGuTexFlush();
  sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 36, 0, v);
  sceGuDisable(GU_DEPTH_TEST);
}

static void frame(int f, void *draw) {
  int i;
  sceGuStart(GU_DIRECT, list);
  renderTarget(f);

  sceGuDrawBufferList(GU_PSM_8888, draw, BUF_W);
  sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
  sceGuViewport(2048, 2048, SCR_W, SCR_H);
  sceGuScissor(0, 0, SCR_W, SCR_H);
  sceGuClearColor(0xFF302010 + (f & 63));
  sceGuClearDepth(0);
  sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

  // Fondo: degradado con dos triangulos sin textura
  VC *bg = sceGuGetMemory(4 * sizeof(VC));
  bg[0].x = 0; bg[0].y = 0; bg[0].color = 0xFF402000;
  bg[1].x = SCR_W; bg[1].y = 0; bg[1].color = 0xFF004020;
  bg[2].x = 0; bg[2].y = SCR_H; bg[2].color = 0xFF200040;
  bg[3].x = SCR_W; bg[3].y = SCR_H; bg[3].color = 0xFF404040;
  for (i = 0; i < 4; i++) bg[i].z = 0;
  sceGuDisable(GU_TEXTURE_2D);
  sceGuShadeModel(GU_SMOOTH);
  sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, 0, bg);

  cube(f, 0.0f, -4.0f, 1.0f, GU_PSM_8888, RT, RT_W);

  // Render target despues de la escena y un segundo cubo con el, que se cruza con el primero:
  // si se perdiera la profundidad del buffer grande, se veria entero por delante
  renderTarget2(f, draw);
  sceGuDrawBufferList(GU_PSM_8888, draw, BUF_W);
  sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
  sceGuViewport(2048, 2048, SCR_W, SCR_H);
  sceGuScissor(0, 0, SCR_W, SCR_H);
  cube(f, 1.3f, -4.2f, 0.6f, GU_PSM_5551, RT2, RT2_W);

  // Sprites con mezcla alfa (textura en RAM)
  sceGuEnable(GU_TEXTURE_2D);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  sceGuTexMode(GU_PSM_8888, 0, 0, 0);
  sceGuTexImage(0, 64, 64, 64, checker);
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sceGuTexFlush();
  VT *s = sceGuGetMemory(2 * 4 * sizeof(VT));
  for (i = 0; i < 4; i++)
    sprite(s + i * 2, 20 + ((f * (i + 1) * 3) % 380), 20 + i * 55, 64, 48, 64, 64, 0xFFFFFFFFu - (u32)i * 0x30000000u);
  sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 8, 0, s);
  sceGuDisable(GU_BLEND);

  // Textura de 8 bits con paleta
  sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
  sceGuClutLoad(256 / 8, clut);
  sceGuTexMode(GU_PSM_T8, 0, 0, 0);
  sceGuTexImage(0, 32, 32, 32, idx8);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuTexFlush();
  VT *c = sceGuGetMemory(2 * sizeof(VT));
  sprite(c, 400, 200, 64, 64, 32, 32, 0xFFFFFFFF);
  sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, c);

  // Copia de bloque dentro del framebuffer: el render target pegado arriba a la izquierda
  sceGuCopyImage(GU_PSM_8888, 0, 0, 96, 96, RT_W, (void *)(0x04000000 | (u32)RT), 4, 4, BUF_W, (void *)(0x04000000 | (u32)draw));

  sceGuFinish();
  sceGuSync(0, 0);

  // La CPU dibuja un recuadro (como un indicador de carga) en el framebuffer ya dibujado. Por
  // la direccion de sceGeEdramGetAddr, como los juegos (con GePatch es la VRAM falsa). La
  // version 1 usaba 0x44000000 fija: eso escribe en la VRAM real, sobre el buffer grande de
  // GePatch, y ningun plugin lo puede evitar.
  u32 *fb = (u32 *)(((u32)sceGeEdramGetAddr() + (u32)draw) | 0x40000000);
  int x0 = 440 - (f % 16) * 2, y0 = 16, x, y;
  for (y = 0; y < 16; y++)
    for (x = 0; x < 16; x++)
      fb[(y0 + y) * BUF_W + x0 + x] = (x ^ y) & 4 ? 0xFFFFFFFF : 0xFF0000FF;
}

int main(void) {
  SceUID k = sceKernelLoadModule("ms0:/PSP/GAME/GEPVM/kcap.prx", 0, NULL);
  if (k >= 0)
    sceKernelStartModule(k, 0, NULL, NULL, NULL);
  makeTextures();
  sceGuInit();
  sceGuStart(GU_DIRECT, list);
  sceGuDrawBuffer(GU_PSM_8888, FB0, BUF_W);
  sceGuDispBuffer(SCR_W, SCR_H, FB1, BUF_W);
  sceGuDepthBuffer(ZB, BUF_W);
  sceGuDepthRange(65535, 0);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuFrontFace(GU_CW);
  sceGuDisable(GU_CULL_FACE);
  sceGuFinish();
  sceGuSync(0, 0);
  sceDisplayWaitVblankStart();
  sceGuDisplay(GU_TRUE);

  void *draw = FB0;
  int f, c = 0;
  for (f = 1; f <= CAPTURAS[N_CAPT - 1]; f++) {
    frame(f, draw);
    sceDisplayWaitVblankStart();
    draw = sceGuSwapBuffers();
    if (c < (int)N_CAPT && f == CAPTURAS[c]) {
      sceGeDrawSync(0);  // incluidas las listas de GePatch (copia a la imagen nativa)
      sceDisplayWaitVblankStart();
      sceGeDrawSync(0);
      if (k >= 0)
        kcapGuardar("ms0:/gepvm", f);
      c++;
    }
  }
  sceKernelExitGame();
  return 0;
}
