// Ayudante de kernel de la escena GEPVM: guarda lo que se ve, igual en la Vita y en el modelo.
//  <dir>/<frame>_nativo.bmp  la imagen nativa de 960x544 (565) de 0x0A400000, si DRAW_NATIVE != 0
//  <dir>/<frame>_psp.bmp     el framebuffer del PSP que se muestra (480x272)
//  <dir>/<frame>.txt         DRAW_NATIVE, direccion, ancho y formato del framebuffer
// Se lee por los reflejos sin cache de usuario (0x4...) y no por 0xA...: la misma memoria en
// los dos lados sin depender del segmento de kernel sin cache.
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("kcap", 0x1006, 1, 0);

#define NATIVE ((volatile u16 *)0x4A400000)
#define DRAW_NATIVE (*(volatile u32 *)0x4BCDEF00)
static u8 row[960 * 3];

static void bmp(const char *path, u32 base, int w, int h, int stride, int fmt) {
  SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd < 0) return;
  u32 data = w * 3 * h, hdr[13] = { 0, 0, 54, 40, w, h, 1 | (24 << 16), 0, data, 0, 0, 0, 0 };
  u8 h0[2] = { 'B', 'M' };
  hdr[0] = 54 + data;
  sceIoWrite(fd, h0, 2);
  sceIoWrite(fd, hdr, 52);
  int x, y;
  for (y = h - 1; y >= 0; y--) {
    for (x = 0; x < w; x++) {
      u32 c, r, g, b;
      if (fmt == 3) { c = ((volatile u32 *)base)[y * stride + x]; r = c & 0xff; g = (c >> 8) & 0xff; b = (c >> 16) & 0xff; }
      else {
        c = ((volatile u16 *)base)[y * stride + x];
        if (fmt == 0) { r = (c & 31) << 3; g = ((c >> 5) & 63) << 2; b = ((c >> 11) & 31) << 3; r |= r >> 5; g |= g >> 6; b |= b >> 5; }
        else if (fmt == 1) { r = (c & 31) << 3; g = ((c >> 5) & 31) << 3; b = ((c >> 10) & 31) << 3; }
        else { r = (c & 15) * 17; g = ((c >> 4) & 15) * 17; b = ((c >> 8) & 15) * 17; }
      }
      row[x * 3] = b; row[x * 3 + 1] = g; row[x * 3 + 2] = r;
    }
    sceIoWrite(fd, row, w * 3);
  }
  sceIoClose(fd);
}

int kcapGuardar(const char *dir, int frame) {
  int k1 = pspSdkSetK1(0);
  char p[96], t[128];
  void *top = 0;
  int bw = 0, fmt = 0;
  u32 on = DRAW_NATIVE;
  sceIoMkdir(dir, 0777);
  if (on) {
    sprintf(p, "%s/%03d_nativo.bmp", dir, frame);
    bmp(p, (u32)NATIVE, 960, 544, 960, 0);
  }
  sceDisplayGetFrameBuf(&top, &bw, &fmt, PSP_DISPLAY_SETBUF_IMMEDIATE);
  if (top && bw > 0) {
    sprintf(p, "%s/%03d_psp.bmp", dir, frame);
    bmp(p, ((u32)top & 0x0fffffff) | 0x40000000, 480, 272, bw, fmt);
  }
  sprintf(p, "%s/%03d.txt", dir, frame);
  sprintf(t, "nativo=%08lx fb=%08lx ancho=%d formato=%d\n", on, (u32)top, bw, fmt);
  SceUID fd = sceIoOpen(p, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd >= 0) { sceIoWrite(fd, t, strlen(t)); sceIoClose(fd); }
  pspSdkSetK1(k1);
  return on;
}

int module_start(SceSize args, void *argp) { return 0; }
int module_stop(void) { return 0; }
