// Menu de diagnostico (L+R+START) e indicador en pantalla.
// Se dibuja por CPU con una fuente 8x16 sobre la imagen de 960x544 que muestra Adrenaline.
// Mientras el menu esta abierto, el juego no recibe los botones (ver ctrl*Patched).
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <string.h>
#include <stdio.h>
#include <systemctrl.h>

#include "log.h"
#include "menu.h"
#include "font.h"

Options opt = { .rt_on = 1, .rt_scale = 1, .omit_fbtex = 1, .fallback = 0, .nop_mark = 1, .walk_new = 1, .v2d = 1 };

// --- Elementos del menu -----------------------------------------------------
enum { ACT_NONE, ACT_CAPTURE, ACT_FLUSH, ACT_CLOSE };
typedef struct {
  const char *name;
  int *val;
  int min, max, step;
  const char *const *labels; // NULL: se muestra el numero
  int action;
} Item;

static const char *const no_si[] = { "no", "si" };
static const char *const fb_modes[] = { "auto", "siempre", "nunca" };
static const char *const nop_modes[] = { "0 (antiguo)", "marca" };
static const char *const auto_si_no[] = { "auto", "si", "no" };
static const char *const walk_modes[] = { "antiguo", "nuevo" };

static const Item items[] = {
  { "Render targets en VRAM alta", &opt.rt_on, 0, 1, 1, no_si },
  { "Escala de render targets", &opt.rt_scale, 1, 2, 1, NULL },
  { "Omitir texturas desde framebuffer", &opt.omit_fbtex, 0, 1, 1, no_si },
  { "Imagen original escalada (videos)", &opt.fallback, 0, 2, 1, fb_modes },
  { "NOP de draws omitidos", &opt.nop_mark, 0, 1, 1, nop_modes },
  { "Recorrido de listas", &opt.walk_new, 0, 1, 1, walk_modes },
  { "Duplicar vertices 2D (HUD, sprites)", &opt.v2d, 0, 1, 1, no_si },
  { "Framebuffer de 1024 como pantalla", &opt.wide1024, 0, 2, 1, auto_si_no },
  { "Desplazamiento X (px del juego)", &opt.dx, -512, 512, 16, NULL },
  { "Desplazamiento Y (px del juego)", &opt.dy, -272, 272, 8, NULL },
  { "Indicador en pantalla", &opt.osd, 0, 1, 1, no_si },
  { "Capturar el proximo frame al log", NULL, 0, 0, 0, NULL, ACT_CAPTURE },
  { "Escribir el log ahora", NULL, 0, 0, 0, NULL, ACT_FLUSH },
  { "Cerrar", NULL, 0, 0, 0, NULL, ACT_CLOSE },
};
#define N_ITEMS (int)(sizeof(items) / sizeof(items[0]))

static int open, sel, block, prev, held;

int menuOpen(void) { return open || block; }

// --- Dibujo -------------------------------------------------------------------
#define W 960
#define H 544

void drawText(u16 *buf, int x, int y, const char *s, u16 fg, u16 bg) {
  for (; *s && x <= W - 8; s++, x += 8) {
    int c = (unsigned char)*s;
    const unsigned char *g = font8x16[(c >= 32 && c < 127 ? c : '?') - 32];
    int r, b;
    for (r = 0; r < 16 && y + r < H; r++) {
      u16 *p = buf + (y + r) * W + x;
      for (b = 0; b < 8; b++)
        p[b] = (g[r] & (0x80 >> b)) ? fg : bg;
    }
  }
}

static void fillRect(u16 *buf, int x, int y, int w, int h, u16 c) {
  int i, j;
  for (j = y; j < y + h && j < H; j++)
    for (i = x; i < x + w && i < W; i++)
      buf[j * W + i] = c;
}

#define C_BG    0x2104 // gris muy oscuro
#define C_SEL   0x8800 // azul (565 en orden BGR del GE: azul en bits altos)
#define C_TEXT  0xffff
#define C_DIM   0xad55
#define C_VAL   0x07ff // amarillo

static void drawMenu(u16 *buf, const char *info) {
  int x = 240, y = 64, w = 480, h = 16 * (N_ITEMS + 4), i;
  char line[64];
  fillRect(buf, x - 8, y - 8, w + 16, h + 16, C_BG);
  drawText(buf, x, y, "GePatch " GEPATCH_VERSION " - diagnostico", C_TEXT, C_BG);
  drawText(buf, x, y + 16, info && *info ? info : "arriba/abajo, izq/der cambia, X activa, O cierra", C_DIM, C_BG);
  for (i = 0; i < N_ITEMS; i++) {
    const Item *it = &items[i];
    int yy = y + 48 + i * 16;
    u16 bg = i == sel ? C_SEL : C_BG;
    fillRect(buf, x - 4, yy, w + 8, 16, bg);
    drawText(buf, x, yy, it->name, C_TEXT, bg);
    if (it->val) {
      if (it->labels)
        snprintf(line, sizeof(line), "%s", it->labels[*it->val - it->min]);
      else
        snprintf(line, sizeof(line), "%d", *it->val);
      drawText(buf, x + w - 8 * (int)strlen(line), yy, line, C_VAL, bg);
    }
  }
}

// --- Entrada ------------------------------------------------------------------
static int (*ctrlPeekPos)(SceCtrlData *, int);
static int (*ctrlPeekNeg)(SceCtrlData *, int);
static int (*ctrlReadPos)(SceCtrlData *, int);
static int (*ctrlReadNeg)(SceCtrlData *, int);

// Mientras el menu esta abierto (y hasta soltar todo al cerrarlo) el juego no ve botones
static int ctrlPeekPosPatched(SceCtrlData *p, int n) {
  int r = ctrlPeekPos(p, n), i;
  if (menuOpen()) for (i = 0; i < r; i++) p[i].Buttons = 0;
  return r;
}
static int ctrlReadPosPatched(SceCtrlData *p, int n) {
  int r = ctrlReadPos(p, n), i;
  if (menuOpen()) for (i = 0; i < r; i++) p[i].Buttons = 0;
  return r;
}
static int ctrlPeekNegPatched(SceCtrlData *p, int n) {
  int r = ctrlPeekNeg(p, n), i;
  if (menuOpen()) for (i = 0; i < r; i++) p[i].Buttons = 0xffffffff;
  return r;
}
static int ctrlReadNegPatched(SceCtrlData *p, int n) {
  int r = ctrlReadNeg(p, n), i;
  if (menuOpen()) for (i = 0; i < r; i++) p[i].Buttons = 0xffffffff;
  return r;
}

void menuInstallCtrlHooks(void) {
  ctrlPeekPos = (void *)FindProc("sceController_Service", "sceCtrl_driver", 0x3A622550);
  ctrlPeekNeg = (void *)FindProc("sceController_Service", "sceCtrl_driver", 0xC152080A);
  ctrlReadPos = (void *)FindProc("sceController_Service", "sceCtrl_driver", 0x1F803938);
  ctrlReadNeg = (void *)FindProc("sceController_Service", "sceCtrl_driver", 0x60B81F86);
  if (!ctrlPeekPos || !ctrlPeekNeg || !ctrlReadPos || !ctrlReadNeg) {
    LOG("menu: no se encontraron las funciones de control (%d%d%d%d), el menu no bloqueara al juego\n",
        !!ctrlPeekPos, !!ctrlPeekNeg, !!ctrlReadPos, !!ctrlReadNeg);
    return;
  }
  sctrlHENPatchSyscall((u32)ctrlPeekPos, ctrlPeekPosPatched);
  sctrlHENPatchSyscall((u32)ctrlPeekNeg, ctrlPeekNegPatched);
  sctrlHENPatchSyscall((u32)ctrlReadPos, ctrlReadPosPatched);
  sctrlHENPatchSyscall((u32)ctrlReadNeg, ctrlReadNegPatched);
}

static u32 readButtons(void) {
  SceCtrlData pad;
  int k1 = pspSdkSetK1(0);
  if (ctrlPeekPos)
    ctrlPeekPos(&pad, 1);
  else
    sceCtrlPeekBufferPositive(&pad, 1);
  pspSdkSetK1(k1);
  return pad.Buttons;
}

static void change(const Item *it, int dir) {
  if (!it->val)
    return;
  int v = *it->val + dir * it->step;
  if (v < it->min) v = it->labels ? it->max : it->min; // las listas dan la vuelta
  if (v > it->max) v = it->labels ? it->min : it->max;
  if (v == *it->val)
    return;
  *it->val = v;
  if (it->val == &opt.rt_scale || it->val == &opt.rt_on)
    rtReset(); // los render targets se reservan segun la escala
  logResetOnce(); // los eventos vuelven a registrarse, para ver en el log el efecto del cambio
  if (it->labels)
    LOG("menu: %s = %s\n", it->name, it->labels[v - it->min]);
  else
    LOG("menu: %s = %d\n", it->name, v);
}

void menuFrame(u16 *buf, const char *osd_text, const char *info) {
  u32 b = readButtons(), pressed = b & ~prev;
  u32 combo = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_START;
  prev = b;

  if (!open) {
    if ((b & combo) == combo && (pressed & combo)) {
      open = block = 1;
      LOG("menu: abierto\n");
    } else if (block && !b) {
      block = 0; // se soltaron los botones con que se cerro: el juego vuelve a verlos
    }
  } else {
    // Izquierda/derecha se repiten al mantenerlas
    u32 lr = b & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT);
    held = lr ? held + 1 : 0;
    if (held > 20 && held % 4 == 0)
      pressed |= lr;
    if (pressed & PSP_CTRL_UP) sel = (sel + N_ITEMS - 1) % N_ITEMS;
    if (pressed & PSP_CTRL_DOWN) sel = (sel + 1) % N_ITEMS;
    if (pressed & PSP_CTRL_LEFT) change(&items[sel], -1);
    if (pressed & PSP_CTRL_RIGHT) change(&items[sel], 1);
    if (pressed & PSP_CTRL_CROSS) {
      const Item *it = &items[sel];
      if (it->action == ACT_CAPTURE) menuRequestCapture();
      else if (it->action == ACT_FLUSH) logFlush();
      else if (it->action == ACT_CLOSE) open = 0;
      else if (it->labels) change(it, 1);
    }
    if (pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_START)) open = 0;
    if (!open)
      LOG("menu: cerrado\n");
  }

  if (!buf)
    return;
  if (opt.osd && osd_text)
    drawText(buf, 8, 8, osd_text, C_VAL, 0);
  if (open)
    drawMenu(buf, info);
}
