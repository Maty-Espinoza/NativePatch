// VPROBE 9: bateria de referencia del GE y la VRAM (grupo A) y lado PSP de la prueba B4
// (docs/pruebas_hardware_vita.md). Con X corre la bateria; con Triangulo, la B4.
//
// Homebrew suelto: no usa plugins, no toca ur0:tai ni juegos. Quedan solo las mediciones que siguen sirviendo
// para disenar GePatch; lo ya respondido o descartado (VRAM dentro de la RAM, modo de 4 MB, reflejos,
// intercambiar la pantalla entera, sprite unico con textura lineal) esta en vprobe6_ruta_e.c y en el documento.
//   [1] Copias de 1 MB entre la VRAM y la RAM con el GE y con DMA.
//   [2] Paginacion de rt al doble en el espacio del z, con los tamanos del censo A2 (tools/censo.py):
//       64x64 8888 (RR), 256x256 565 y 256x256 8888 (RR, SH).
//   [3] Relleno: borrar, texturas por franjas (lineal/swizzle, RAM/VRAM, bilineal, mezcla) y las dos copias
//       escaladas de GePatch (reducir la pantalla, ampliar 480x272).
//   [4] 3D al doble: la misma escena con textura y profundidad a 480x272 y a 960x544.
//   [5] GE y CPU a la vez: cuanto se frenan entre si por el bus de la RAM.
//   [6] (v9) Texturas en la RAM contra la VRAM mientras la CPU trabaja: la VRAM falsa de GePatch deja todas
//       las texturas del juego en la RAM.
//   [7] (v9) Pasada final a 1,5x: ampliar 720x408 a 960x544 con el GE y copiarla a la RAM (solo PSP), contra
//       copiar 720x408 a la RAM y que la GPU de la Vita la escale (gepresent).
//   [8] (v9) Costo de sceKernelDcacheWritebackInvalidateAll() para el que usa la cache despues (GePatch lo
//       llama en cada encolado y cada stall).
//   Con Cuadrado corre solo [6]-[8].
//   B4 (Triangulo, necesita vita/vpcdram.suprx cargado en Adrenaline): el PSP deja una firma en la VRAM y
//       avisa por un buzon en 0x0A000000; el ARM busca la firma en sus bloques de memoria, mide cuanto tarda
//       en leer la VRAM y deja una marca en ella. Responde donde vive la VRAM del lado ARM y si el ARM (y la
//       GPU de la Vita) la puede leer directo.
// Resultados: ms0:/ge_patch_vprobe.log (el relevo baja solo ge_patch*.log de la raiz de ux0:/pspemu).
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspdmac.h>
#include <pspdisplay.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <systemctrl.h>
#include <kubridge.h>

PSP_MODULE_INFO("VPROBE", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(12 * 1024);

#define VER 9
#define VB 0x04088000u            // zona de pruebas de la VRAM (0x04000000-0x04088000 es el texto en pantalla)
#define VEND 0x04200000u          // de aqui en adelante es el reflejo: no se escribe
#define VTEX 0x04198000u          // textura de 256 KB en la VRAM (despues de la pantalla de pruebas)
#define LOGF "ms0:/ge_patch_vprobe.log"
#define UNC(a) ((a) | 0x40000000u) // vista sin cache para la CPU
#define now sceKernelGetSystemTimeLow

static unsigned int __attribute__((aligned(64))) glist[256 * 1024];  // lista principal (1 MB)
static unsigned int __attribute__((aligned(64))) clist[256 * 1024];  // lista llamada (se arma antes de medir)
static u8 *ram1, *ram2;  // 2 x 2 MB en la RAM del PSP (partición del homebrew)

// ---------- salida ----------
static void out(const char *s) {
  pspDebugScreenPrintf("%s", s);
  SceUID fd = sceIoOpen(LOGF, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
  if (fd >= 0) { sceIoWrite(fd, s, strlen(s)); sceIoClose(fd); }
}
#define OUT(...) do { char _b[256]; snprintf(_b, sizeof _b, __VA_ARGS__); out(_b); } while (0)

// ---------- GE ----------
static void geStart(void) { sceGuStart(GU_DIRECT, glist); }
static u32 geEnd(void) {  // termina la lista principal y mide lo que tarda el GE en ejecutarla
  sceKernelDcacheWritebackAll();
  u32 t = now();
  sceGuFinish();
  sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
  return now() - t;
}
// Ejecuta una lista ya armada (clist) desde la principal: mide solo al GE, no a la CPU que arma la lista.
static u32 geRunCall(void) {
  geStart();
  sceGuCallList(clist);
  return geEnd();
}
// Destino del dibujo (modo through). stride = ancho de linea en pixeles (v2 lo fijaba en 1024: el rt de 512
// de la paginacion se dibujaba con otro ancho y salia "SIN DIBUJAR").
static void fbSetup(u32 vaddr, int psm, int w, int h, int stride) {
  sceGuDrawBuffer(psm, (void *)(vaddr - 0x04000000), stride);
  sceGuSendCommandi(0x15, 0);                          // REGION1
  sceGuSendCommandi(0x16, ((h - 1) << 10) | (w - 1));   // REGION2
  sceGuSendCommandi(0xD4, 0);                           // SCISSOR1 (sceGuScissor no envia nada si
  sceGuSendCommandi(0xD5, ((h - 1) << 10) | (w - 1));   // SCISSOR2  GU_SCISSOR_TEST esta apagado: v1 dibujaba 1 px)
  sceGuOffset(2048 - w / 2, 2048 - h / 2);
  sceGuViewport(2048, 2048, w, h);
  sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_ALPHA_TEST); sceGuDisable(GU_BLEND); sceGuDisable(GU_TEXTURE_2D);
  sceGuDepthMask(GU_TRUE);
}
// Borrado propio (sceGuClear usa el tamano de sceGuDispBuffer): modo CLEAR + franjas de 64 px
typedef struct { u32 c; s16 x, y, z, pad; } VC8;
static void clrf(int w, int h, u32 c, int flags) {
  int n = (w + 63) / 64;
  VC8 *v = sceGuGetMemory(n * 2 * sizeof(VC8));
  for (int i = 0; i < n; i++) {
    v[i * 2] = (VC8){c, i * 64, 0, 0, 0};
    v[i * 2 + 1] = (VC8){c, i * 64 + 64 > w ? w : i * 64 + 64, h, 0, 0};
  }
  sceGuSendCommandi(0xD3, (flags << 8) | 1);
  sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, n * 2, 0, v);
  sceGuSendCommandi(0xD3, 0);
}
#define clr(w, h, c) clrf(w, h, c, GU_COLOR_BUFFER_BIT)

// ---------- 3. Copias ----------
static u32 sum(const volatile u32 *p, u32 n) { u32 s = 0; for (u32 i = 0; i < n; i++) s = ((s << 1) | (s >> 31)) ^ p[i]; return s; }
static void fillPat(volatile u32 *p, u32 n, u32 seed) { for (u32 i = 0; i < n; i++) p[i] = seed + i * 0x9E3779B1u; }
static void row(const char *qu, u32 bytes, u32 us, int ok) {
  OUT("%-26s %5u KB %7u us %7.1f MB/s %s\n", qu, bytes >> 10, us, us ? bytes / (float)us : 0, ok ? "ok" : "MAL");
}
// TRANSFER del GE en 32 bits, 512 px de ancho (2 KB por linea)
static u32 geCopy(u32 src, u32 dst, u32 bytes) {
  geStart();
  sceGuCopyImage(GU_PSM_8888, 0, 0, 512, bytes / 2048, 512, (void *)src, 0, 0, 512, (void *)dst);
  sceGuTexSync();
  return geEnd();
}
static void copias(void) {
  out("\n[1] Copias de 1 MB (promedio de 4)\n");
  u32 VA = 0x04100000, b = 1 << 20, n = b / 4, t, s0;  // VA: 1 MB hasta VEND
  fillPat((u32 *)ram1, n, 1); sceKernelDcacheWritebackInvalidateAll(); s0 = sum((u32 *)ram1, n);
  t = 0; for (int k = 0; k < 4; k++) t += geCopy((u32)ram1, VA, b);
  row("GE RAM->VRAM", b, t / 4, sum((u32 *)UNC(VA), n) == s0);
  memset(ram2, 0, b); sceKernelDcacheWritebackInvalidateAll();
  t = 0; for (int k = 0; k < 4; k++) t += geCopy(VA, (u32)ram2, b);
  sceKernelDcacheWritebackInvalidateAll();
  row("GE VRAM->RAM", b, t / 4, sum((u32 *)ram2, n) == s0);
  memset(ram2, 0, b); sceKernelDcacheWritebackInvalidateAll();
  t = 0; for (int k = 0; k < 4; k++) t += geCopy((u32)ram1, (u32)ram2, b);
  sceKernelDcacheWritebackInvalidateAll();
  row("GE RAM->RAM", b, t / 4, sum((u32 *)ram2, n) == s0);
  t = now(); for (int k = 0; k < 4; k++) sceDmacMemcpy((void *)VA, ram1, b); t = (now() - t) / 4;
  row("DMA RAM->VRAM", b, t, sum((u32 *)UNC(VA), n) == s0);
  memset(ram2, 0, b); sceKernelDcacheWritebackInvalidateAll();
  t = now(); for (int k = 0; k < 4; k++) sceDmacMemcpy(ram2, (void *)VA, b); t = (now() - t) / 4;
  sceKernelDcacheWritebackInvalidateAll();
  row("DMA VRAM->RAM", b, t, sum((u32 *)ram2, n) == s0);
}

// ---------- 2. Paginacion de rt al doble ----------
// Como lo haria GePatch: el rt al doble se dibuja en el espacio del z de la pantalla (0x04100000, 1 MB). Se
// saca esa parte del z a la RAM, se dibuja el rt, se guarda en la RAM (desde ahi el GE lo lee como textura) y
// se devuelve el z. Tamanos al doble de los rt del censo A2.
static void paginacion(void) {
  out("\n[2] Paginacion de rt al doble en el espacio del z\n");
  static const struct { const char *nm; int w, h, psm, bpp; } rt[] = {
    {"64x64 8888 (RR)       -> 128x128", 128, 128, GU_PSM_8888, 4},
    {"256x256 565           -> 512x512", 512, 512, GU_PSM_5650, 2},
    {"256x256 8888 (RR, SH) -> 512x512", 512, 512, GU_PSM_8888, 4},
  };
  u32 Z = 0x04100000, zs = (u32)ram1, dst = (u32)ram2;
  for (int i = 0; i < 3; i++) {
    int w = rt[i].w, h = rt[i].h, p = rt[i].psm;
    u32 tt = 0;
    memset(ram2, 0, w * h * rt[i].bpp); sceKernelDcacheWritebackInvalidateAll();
    for (int k = 0; k < 4; k++) {
      geStart();
      sceGuCopyImage(p, 0, 0, w, h, w, (void *)Z, 0, 0, w, (void *)zs); sceGuTexSync();    // sacar el z
      fbSetup(Z, p, w, h, w); clr(w, h, 0xFF00FF00);                                      // dibujar el rt
      sceGuCopyImage(p, 0, 0, w, h, w, (void *)Z, 0, 0, w, (void *)dst); sceGuTexSync();   // guardar el rt
      sceGuCopyImage(p, 0, 0, w, h, w, (void *)zs, 0, 0, w, (void *)Z); sceGuTexSync();   // devolver el z
      tt += geEnd();
    }
    sceKernelDcacheWritebackInvalidateAll();
    u32 last = rt[i].bpp == 4 ? ((u32 *)ram2)[w * h - 1] : ((u16 *)ram2)[w * h - 1];
    OUT("%s (%4d KB): %5u us  %s\n", rt[i].nm, w * h * rt[i].bpp >> 10, tt / 4, last ? "ok" : "SIN DIBUJAR");
  }
}

// ---------- 3. Relleno del GE ----------
typedef struct { u16 u, v; s16 x, y, z; } VT;  // textura 16 bits, posicion 16 bits (10 bytes)
typedef struct { u16 c; s16 x, y, z; } VC;
static void swizzle(u8 *out, const u8 *in, u32 wb, u32 h) {  // bloques de 16 bytes x 8 lineas
  u32 *d = (u32 *)out;
  for (u32 y = 0; y < h; y += 8)
    for (u32 x = 0; x < wb; x += 16)
      for (u32 j = 0; j < 8; j++) { const u32 *p = (const u32 *)(in + (y + j) * wb + x); *d++ = p[0]; *d++ = p[1]; *d++ = p[2]; *d++ = p[3]; }
}
static void texSet(const void *tex, int w, int h, int tbw, int swz, int lin) {
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(GU_PSM_5650, 0, 0, swz);
  sceGuTexImage(0, w, h, tbw, tex);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuTexFilter(lin ? GU_LINEAR : GU_NEAREST, lin ? GU_LINEAR : GU_NEAREST);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  sceGuTexFlush();
}
static void sprite(int u0, int v0, int u1, int v1, int x0, int y0, int x1, int y1) {
  VT *v = sceGuGetMemory(2 * sizeof(VT));
  v[0] = (VT){u0, v0, x0, y0, 0}; v[1] = (VT){u1, v1, x1, y1, 0};
  sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}
// Textura tw x th estirada a la pantalla 960x544, en un sprite o en franjas de 64 px de ancho
static void stretch(int tw, int th, int franjas) {
  if (!franjas) { sprite(0, 0, tw, th, 0, 0, 960, 544); return; }
  for (int x = 0; x < 960; x += 64) {
    int x1 = x + 64 > 960 ? 960 : x + 64;
    sprite(x * tw / 960, 0, x1 * tw / 960, th, x, 0, x1, 544);
  }
}
// Copia escalada de un fb lineal 565 (src, ancho de linea sstride) de sw x sh a dw x dh en el destino actual.
// El GE no acepta texturas de mas de 512: se hace por franjas de destino de 'franja' px y, si sh > 512, en dos
// mitades; cada franja apunta la textura a su esquina (alineada a 16 bytes).
static void blit(u32 src, int sstride, int sw, int sh, int dw, int dh, int franja) {
  int partes = sh > 512 ? 2 : 1;
  for (int p = 0; p < partes; p++) {
    int sy0 = sh * p / partes, sy1 = sh * (p + 1) / partes, dy0 = dh * p / partes, dy1 = dh * (p + 1) / partes;
    for (int dx = 0; dx < dw; dx += franja) {
      int dx1 = dx + franja > dw ? dw : dx + franja, sx0 = dx * sw / dw, sx1 = dx1 * sw / dw;
      texSet((void *)(src + (sy0 * sstride + sx0) * 2), 512, 512, sstride, 0, 1);
      sprite(0, 0, sx1 - sx0, sy1 - sy0, dx, dy0, dx1, dy1);
    }
  }
}
static void fila(const char *nm, u32 us, float px) {
  OUT("%-40s %6u us  %6.1f Mpx/s  (%.1f por frame)\n", nm, us, px / us, 16667.0f / us);
}
static void relleno(void) {
  out("\n[3] Relleno del GE (destino 960x544 565)\n");
  u16 *lin = (u16 *)ram1, *swz = (u16 *)(ram1 + (1 << 20));  // textura 512x256 565, lineal y swizzle
  for (int i = 0; i < 512 * 256; i++) lin[i] = (u16)(i * 37 ^ (i >> 9) * 0x0821);
  swizzle((u8 *)swz, (u8 *)lin, 1024, 256);
  sceKernelDcacheWritebackInvalidateAll();
  memcpy((void *)UNC(VTEX), swz, 512 * 256 * 2);  // la misma textura swizzle en la VRAM
  // Borrar, comprobando que llega a la esquina
  sceGuStart(GU_CALL, clist); fbSetup(VB, GU_PSM_5650, 960, 544, 1024);
  for (int k = 0; k < 10; k++) clr(960, 544, 0xFF000000 | (k + 1) * 0x111111);
  sceGuFinish();
  volatile u16 *last = (u16 *)UNC(VB + 543 * 2048 + 959 * 2);
  *last = 0;
  fila("borrar", geRunCall() / 10, 960 * 544);
  if (!*last) out("  AVISO: el borrado no llego a la esquina 959,543\n");
  static const struct { const char *nm; int franjas, swz, vram, lin, blend, n; } m[] = {
    {"textura: franjas 64, lineal, RAM", 1, 0, 0, 0, 0, 10},
    {"textura: franjas 64, swizzle, RAM", 1, 1, 0, 0, 0, 10},
    {"textura: franjas 64, swizzle, VRAM", 1, 1, 1, 0, 0, 10},
    {"textura: franjas, swizzle, RAM, bilineal", 1, 1, 0, 1, 0, 10},
    {"  + mezcla alfa", 1, 1, 0, 1, 1, 10},
  };
  for (int i = 0; i < 5; i++) {
    sceGuStart(GU_CALL, clist); fbSetup(VB, GU_PSM_5650, 960, 544, 1024);
    texSet(m[i].vram ? (void *)VTEX : m[i].swz ? (void *)swz : (void *)lin, 512, 256, 512, m[i].swz, m[i].lin);
    if (m[i].blend) { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); }
    for (int k = 0; k < m[i].n; k++) stretch(512, 256, m[i].franjas);
    sceGuFinish();
    fila(m[i].nm, geRunCall() / m[i].n, 960 * 544);
  }
  // Las dos operaciones de GePatch: reducir la pantalla (VRAM) a 480x272 y ampliar 480x272 (RAM) a 960x544
  sceGuStart(GU_CALL, clist); fbSetup(VTEX, GU_PSM_5650, 480, 272, 512);
  for (int k = 0; k < 4; k++) blit(VB, 1024, 960, 544, 480, 272, 32);
  sceGuFinish();
  fila("GePatch: reducir 960x544 VRAM -> 480x272", geRunCall() / 4, 480 * 272);
  sceGuStart(GU_CALL, clist); fbSetup(VB, GU_PSM_5650, 960, 544, 1024);
  for (int k = 0; k < 4; k++) blit((u32)ram2, 512, 480, 272, 960, 544, 64);
  sceGuFinish();
  fila("GePatch: ampliar 480x272 RAM -> 960x544", geRunCall() / 4, 960 * 544);
  // Triangulos chicos: 20000 de 8x8 px, solo color
  sceGuStart(GU_CALL, clist); fbSetup(VB, GU_PSM_5650, 960, 544, 1024);
  VC *v = sceGuGetMemory(60000 * sizeof(VC));
  for (int i = 0; i < 20000; i++) {
    s16 x = (i * 13) % 950, y = (i * 7) % 534;
    v[i * 3] = (VC){0xF800, x, y, 0}; v[i * 3 + 1] = (VC){0x07E0, x + 8, y, 0}; v[i * 3 + 2] = (VC){0x001F, x, y + 8, 0};
  }
  sceGuDrawArray(GU_TRIANGLES, GU_COLOR_5650 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 60000, 0, v);
  sceGuFinish();
  u32 us = geRunCall();
  OUT("20000 triangulos de 8x8: %u us (%.0f ns por triangulo)\n", us, us * 1000.0f / 20000);
}

// ---------- 4. 3D al doble ----------
// La misma escena (3000 triangulos con textura swizzle y profundidad, ~3 capas) a 480x272 y a 960x544.
// A 960x544 usa toda la VRAM como GePatch (color 0x04000000, z 0x04100000): borra el texto de la pantalla.
static u32 escena(int esc, u32 col, u32 zb, int stride) {
  static u32 seed;
  seed = 12345;
  sceGuStart(GU_CALL, clist);
  fbSetup(col, GU_PSM_5650, 480 * esc, 272 * esc, stride);
  sceGuDepthBuffer((void *)(zb - 0x04000000), stride);
  sceGuDepthMask(GU_FALSE);  // escribir el z (tambien al borrarlo)
  clrf(480 * esc, 272 * esc, 0xFF202020, GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
  sceGuEnable(GU_DEPTH_TEST); sceGuDepthFunc(GU_GEQUAL);
  texSet(ram1 + (1 << 20), 256, 256, 512, 1, 1);  // la textura swizzle de [5] (ancho de linea 512)
  VT *v = sceGuGetMemory(9000 * sizeof(VT));
  for (int i = 0; i < 3000; i++) {
    seed = seed * 1103515245 + 12345; int x = (seed >> 8) % 464, y = (seed >> 20) % 256, z = seed & 0xFFFF;
    seed = seed * 1103515245 + 12345; int u = (seed >> 8) % 240, w = (seed >> 20) % 240;
    v[i * 3] = (VT){u, w, x * esc, y * esc, z};
    v[i * 3 + 1] = (VT){u + 16, w, (x + 16) * esc, y * esc, z};
    v[i * 3 + 2] = (VT){u, w + 16, x * esc, (y + 16) * esc, z};
  }
  sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 9000, 0, v);
  sceGuDisable(GU_DEPTH_TEST);
  sceGuFinish();
  u32 t = 0;
  for (int k = 0; k < 3; k++) t += geRunCall();
  return t / 3;
}
static void tres3d(void) {
  out("\n[4] 3D al doble (3000 triangulos con textura y profundidad, ~3 capas)\n");
  u32 a = escena(1, VB, 0x04100000, 512);
  u32 b = escena(2, 0x04000000, 0x04100000, 960);
  pspDebugScreenClear();
  OUT("480x272: %u us   960x544: %u us   -> al doble cuesta %.2f veces\n", a, b, b / (float)a);
}

// ---------- 5. GE y CPU a la vez ----------
// Trabajo de CPU parecido al recorrido de GePatch (lecturas dispersas en 2 MB con cache) y una copia memcpy,
// solos y mientras el GE trabaja (copias RAM->VRAM o dibujo con textura en la VRAM).
static volatile u32 sink, geFin;
static void onFinish(int a) { geFin = now(); }  // senal FINISH del GE: hora real en que termina la lista
static u32 cpuWalk(void) {
  u32 t = now(), s = 0, i = 1;
  const u32 *b = (const u32 *)ram1;
  for (int k = 0; k < 300000; k++) { i = i * 1103515245 + 12345; s += b[(i >> 9) & 0x7FFFF]; }
  sink = s;
  return now() - t;
}
static u32 cpuCopy(void) { u32 t = now(); memcpy(ram2, ram1, 1 << 20); return now() - t; }
static void geCarga(int tipo) {  // arma en clist ~20 ms de trabajo del GE
  sceGuStart(GU_CALL, clist);
  if (tipo == 0)
    for (int k = 0; k < 8; k++) { sceGuCopyImage(GU_PSM_8888, 0, 0, 512, 256, 512, ram1 + (1 << 20), 0, 0, 512, (void *)0x04100000); sceGuTexSync(); }
  else {
    fbSetup(VB, GU_PSM_5650, 960, 544, 1024);
    texSet(tipo == 2 ? (void *)(ram1 + (1 << 20)) : (void *)VTEX, 512, 256, 512, 1, 1);  // swizzle (ver [3])
    for (int k = 0; k < 15; k++) stretch(512, 256, 1);
  }
  sceGuFinish();
}
static void contencion(int v9) {
  out(v9 ? "\n[6] Texturas en la RAM contra la VRAM, con la CPU trabajando\n" : "\n[5] GE y CPU a la vez\n");
  u32 (*job[2])(void) = {cpuWalk, cpuCopy};
  static const char *jn[2] = {"CPU recorrido (300k lecturas)", "CPU memcpy 1 MB"},
    *gn[3] = {"GE copia RAM->VRAM", "GE dibuja (textura VRAM)", "GE dibuja (textura RAM)"};
  for (int j = 0; j < 2; j++) {
    u32 solo = 0;
    for (int k = 0; k < 3; k++) solo += job[j]();
    solo /= 3;
    OUT("%s solo: %u us\n", jn[j], solo);
    for (int g = v9; g < 2 + v9; g++) {
      geCarga(g);
      u32 gsolo = geRunCall();
      geStart(); sceGuCallList(clist); sceKernelDcacheWritebackAll();
      u32 t0 = now();
      sceGuFinish();  // el GE arranca y la CPU sigue
      u32 c = job[j]();
      sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
      u32 tg = geFin - t0;  // v4 media hasta despues de la CPU: el % del GE no valia
      OUT("  con %-24s CPU %6u us (%+.0f%%)  GE %6u us (solo %u us, %+.0f%%)%s\n", gn[g], c, (c - (float)solo) * 100 / solo,
          tg, gsolo, (tg - (float)gsolo) * 100 / gsolo, c > tg ? "  [el GE termino antes]" : "");
    }
  }
}

// ---------- 7. Pasada final a 1,5x ----------
// Imagen de 720x408 (ancho de linea 768, 565) en la VRAM. Solo PSP: ampliarla a 960x544 en la VRAM (franjas de
// 64, bilineal) y copiar eso a la RAM para Adrenaline. Con gepresent: copiar 720x408 tal cual y que escale la Vita.
// Usa toda la VRAM (borra el texto de la pantalla, como [4]).
static u32 xfer(u32 src, int sstride, u32 dst, int dstride, int w, int h, int n) {
  geStart();
  for (int k = 0; k < n; k++) { sceGuCopyImage(GU_PSM_5650, 0, 0, w, h, sstride, (void *)src, 0, 0, dstride, (void *)dst); sceGuTexSync(); }
  return geEnd() / n;
}
static void final15(void) {
  out("\n[7] Pasada final a 1,5x (720x408 -> pantalla de 960x544)\n");
  u32 D = 0x04000000, S = 0x04100000, R = (u32)ram2;  // destino 960x544 (1020 KB), origen 720x408 (612 KB)
  u16 *s = (u16 *)UNC(S);
  for (int i = 0; i < 768 * 408; i++) s[i] = (u16)(i * 29 ^ (i >> 7) * 0x0841);
  sceGuStart(GU_CALL, clist); fbSetup(D, GU_PSM_5650, 960, 544, 960);
  for (int k = 0; k < 4; k++) blit(S, 768, 720, 408, 960, 544, 64);
  sceGuFinish();
  u32 amp = geRunCall() / 4;
  u32 cp960 = xfer(D, 960, R, 960, 960, 544, 4);
  sceGuStart(GU_CALL, clist); fbSetup(D, GU_PSM_5650, 960, 544, 960);
  for (int k = 0; k < 4; k++) { blit(S, 768, 720, 408, 960, 544, 64); sceGuTexSync(); sceGuCopyImage(GU_PSM_5650, 0, 0, 960, 544, 960, (void *)D, 0, 0, 960, (void *)R); sceGuTexSync(); }
  sceGuFinish();
  u32 todo = geRunCall() / 4;
  u32 cp720 = xfer(S, 768, R, 720, 720, 408, 4);
  u32 cp600 = xfer(S, 640, R, 600, 600, 340, 4);
  pspDebugScreenClear();
  OUT("hoy (2x): copiar 960x544 VRAM->RAM            %5u us\n", cp960);
  OUT("1,5x solo PSP: ampliar 720x408 -> 960x544     %5u us\n", amp);
  OUT("1,5x solo PSP: ampliar + copiar (una lista)   %5u us\n", todo);
  OUT("1,5x con gepresent: copiar 720x408 VRAM->RAM  %5u us\n", cp720);
  OUT("1,25x con gepresent: copiar 600x340           %5u us\n", cp600);
}

// ---------- 8. Vaciar toda la cache ----------
// Un trabajo de CPU que cabe en la cache de datos (12 KB, lee y escribe) se repite 2000 veces, solo y vaciando
// toda la cache entre una vuelta y otra. La diferencia por llamada es lo que GePatch le cobra al juego en cada
// encolado o stall (la llamada mas volver a llenar la cache).
static u32 cacheJob(int vaciar) {
  static u32 __attribute__((aligned(64))) ws[3072];
  u32 t = now(), s = 0;
  for (int k = 0; k < 2000; k++) {
    for (int i = 0; i < 3072; i += 4) { s += ws[i]; ws[i + 1] = s; }
    if (vaciar == 1) sceKernelDcacheWritebackInvalidateAll();
    if (vaciar == 2) sceKernelDcacheWritebackInvalidateRange(ws, 1024);  // vaciar solo 1 KB (rango tocado)
  }
  sink = s;
  return now() - t;
}
static void cacheAll(void) {
  out("\n[8] Vaciar la cache de datos (2000 vueltas de un trabajo de 12 KB)\n");
  cacheJob(0);
  u32 a = cacheJob(0), b = cacheJob(1), c = cacheJob(2);
  OUT("solo %u us, vaciando todo %u us (%.1f us por llamada), vaciando 1 KB %u us (%.1f us por llamada)\n",
      a, b, (b - (float)a) / 2000, c, (c - (float)a) / 2000);
}

// ---------- B4: lado PSP ----------
// Buzon en 0x0A000000 (pagina reservada en la particion del homebrew, como KBENCH). Palabras:
//   [0] "VPB4" mientras el PSP espera  [1] nonce  [2..5] firma invertida (para que no aparezca tal cual en la RAM)
//   [16] "ARMV" cuando el ARM termino  [17] dir. ARM de la VRAM (0 = no encontrada)  [18] tipo de bloque
//   [19] us para leer 1 MB con memcpy  [20] us con DMA  [21] bloques revisados  [22] coincidencias
#define B4_MBOX 0x0A000000u
static void b4psp(void) {
  out("\n[B4] Lado PSP: firma en la VRAM para que el ARM la busque (vpcdram.suprx)\n");
  SceUID blk = sceKernelAllocPartitionMemory(2, "vpb4", PSP_SMEM_Addr, 4096, (void *)B4_MBOX);
  OUT("reserva del buzon en 0x0A000000: %08x\n", blk);
  if (blk < 0) { out("no se pudo reservar 0x0A000000: no se hace la prueba\n"); return; }
  volatile u32 *mb = (u32 *)UNC(B4_MBOX);
  for (int i = 0; i < 32; i++) mb[i] = 0;
  u32 n = now(), f[4];
  for (int i = 0; i < 4; i++) f[i] = (0x56504234 ^ (n * (i + 3))) | 1;
  volatile u32 *v1 = (u32 *)UNC(0x041F0000), *v2 = (u32 *)UNC(0x04100000), *marca = (u32 *)UNC(0x041F8000);
  for (int i = 0; i < 4; i++) { v1[i] = f[i]; v2[i] = f[i] ^ 0xFFFF0000; }
  *marca = 0;
  geStart(); fbSetup(0x04180000, GU_PSM_8888, 64, 8, 64); clr(64, 8, 0xC0FFEE11); geEnd();  // lo dibuja el GE
  mb[1] = n; for (int i = 0; i < 4; i++) mb[2 + i] = ~f[i];
  mb[0] = 0x34425056;  // "VPB4": desde aqui el ARM puede escribir en el buzon
  out("firma en VRAM+1F0000 (y XOR en +100000), rectangulo del GE en +180000. Esperando al ARM (60 s)...\n");
  int ok = 0;
  for (int s = 0; s < 60 && !ok; s++) {
    sceKernelDelayThread(1000000);
    ok = mb[16] == 0x564D5241;  // "ARMV"
    pspDebugScreenPrintf(".");
  }
  out("\n");
  if (!ok) out("el ARM no respondio en 60 s: revisa que vpcdram.suprx este en ur0:tai/config.txt (*PSPEMUCFW) y ux0:pspemu/ge_patch_vpcdram.log\n");
  else {
    OUT("ARM: VRAM en %08x (tipo de bloque %08x), bloques revisados %u, coincidencias %u\n", mb[17], mb[18], mb[21], mb[22]);
    if (mb[17]) OUT("ARM lee 1 MB de la VRAM: memcpy %u us, DMA %u us\n", mb[19], mb[20]);
    OUT("marca del ARM en VRAM+1F8000: %08x -> %s\n", *marca, *marca == 0x41524D21 ? "el ARM escribe en la VRAM y el PSP lo ve" : "no llego");
  }
  mb[0] = 0;
  sceKernelFreePartitionMemory(blk);
}

// ---------- main ----------
static u32 boton(void) {
  SceCtrlData c; u32 antes = 0xFFFFFFFF;
  for (;;) {
    sceKernelDelayThread(10000);
    sceCtrlReadBufferPositive(&c, 1);
    u32 nuevo = c.Buttons & ~antes; antes = c.Buttons;
    if (nuevo & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE | PSP_CTRL_SQUARE)) return nuevo;
  }
}

int main(void) {
  pspDebugScreenInit();
  sceIoRemove(LOGF);
  OUT("VPROBE %d - bateria de referencia del GE y la VRAM\n", VER);
  // GePatch cambia sceGeEdramGetAddr a la VRAM falsa: con el cargado, los resultados no valen
  SceModule m;
  if (kuKernelFindModuleByName("GePatch", &m) >= 0) {
    out("\nGePatch esta cargado en este homebrew: los resultados no valdrian.\n"
        "Cierra VPROBE y abrelo manteniendo L (Adrenaline no carga plugins).\n\nO: salir\n");
    while (!(boton() & PSP_CTRL_CIRCLE));
    sceKernelExitGame(); return 0;
  }
  ram1 = memalign(64, 2 << 20); ram2 = memalign(64, 2 << 20);
  if (!ram1 || !ram2) { out("sin memoria\n"); sceKernelDelayThread(5000000); sceKernelExitGame(); return 0; }
  OUT("buffers en la RAM: %p %p\n", ram1, ram2);
  sceGuInit();
  sceGuSetCallback(GU_CALLBACK_FINISH, onFinish);  // la pantalla sigue siendo el texto de pspDebugScreen (0x04000000, 8888)
  out("X: bateria A (~15 s)   Cuadrado: solo [6]-[8] (v9)   Triangulo: prueba B4 con el ARM (vpcdram.suprx)   O: salir\n");
  u32 k = boton();
  if (k & PSP_CTRL_CIRCLE) { sceKernelExitGame(); return 0; }
  pspDebugScreenClear();
  u32 t0 = now();
  OUT("modelo %d, cpu %d MHz, bus %d MHz, VRAM %d KB\n", kuKernelGetModel(), scePowerGetCpuClockFrequencyInt(),
      scePowerGetBusClockFrequencyInt(), sceGeEdramGetSize() >> 10);
  if (k & PSP_CTRL_TRIANGLE) b4psp();
  else if (k & PSP_CTRL_SQUARE) { contencion(1); final15(); cacheAll(); }
  else { copias(); paginacion(); relleno(); tres3d(); contencion(0); contencion(1); final15(); cacheAll(); }
  OUT("\nListo en %u ms. Resultados: ms0:/ge_patch_vprobe.log\nO: salir\n", (now() - t0) / 1000);
  while (!(boton() & PSP_CTRL_CIRCLE));
  sceKernelExitGame();
  return 0;
}
