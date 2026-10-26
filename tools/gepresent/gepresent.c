// gepresent.suprx: presentacion de la imagen de GePatch con la GPU de la Vita.
//
// Plugin de usuario en el proceso de Adrenaline (taiHEN, seccion *PSPEMUCFW de ur0:tai/config.txt). Con
// DRAW_NATIVE != 0, Adrenaline 8 dibuja cada vblank una textura GXM de 960x544 565 puesta directamente sobre la
// RAM del PSP (0x0A400000) a escala 1 (user/menu.c, AdrenalineDraw). vita2d calcula el tamano del cuadro con
// sceGxmTextureGetWidth/Height: este plugin engancha esas dos funciones y, cuando se pide el tamano de esa
// textura, la cambia por la imagen que indica el descriptor de GePatch (direccion, tamano, ancho de linea,
// formato) y devuelve el tamano que la estira a toda la pantalla. Asi:
//   - GePatch puede dibujar a menos de 960x544 (escala por juego) y la Vita la amplia sin costo para el PSP;
//   - doble buffer: GePatch alterna dos imagenes y el GE publica la terminada (sin tearing);
//   - videos e imagenes de la CPU: se muestra el fb del juego tal cual, escalado por la Vita.
// No toca Adrenaline ni usa offsets: si el descriptor no esta, la textura queda como la creo Adrenaline.
// Los ganchos van en las IMPORTACIONES del modulo de Adrenaline (AdrenalineUser), no en el codigo de SceGxm: la v1
// enganchaba las exportaciones y la Vita se apagaba al abrir Adrenaline (GetWidth y GetHeight son funciones de un
// par de instrucciones, una al lado de la otra; el salto del gancho de una pisaba a la otra).
// Si existe ux0:data/gepresent/off no se engancha nada.
// La GPU solo lee memoria registrada con sceGxmMapMemory: vita2d registro solo 0x0A400000 (960x544). v2 apuntaba la
// textura a otras direcciones y la GPU se caia ("GPU Driver detects GPU Crash"). v3 registra la VRAM falsa
// (0x0A000000, 4 MB) y el segundo buffer (0x0AE00000, 1 MB) y no acepta imagenes fuera de esas zonas. Cambiar el
// formato con SetFormat falla (0x805B0009): la textura se rehace con sceGxmTextureInitLinear.
// Descriptor (RAM del PSP 0x0A7FF000, ver PRES_DESC en main.c):
//   +0 "GPV1" (GePatch)  +4 'K' + contador (lo escribe este plugin)  +8 "PRES" (este plugin, en cuanto ve la
//   marca: GePatch lo espera antes del primer recorrido para elegir una escala < 2)  +0x10 dir, ancho, alto,
//   linea | formato << 16
// Log: ux0:pspemu/ge_patch_gepresent.log
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/gxm.h>
#include <taihen.h>
#include <stdarg.h>

#define LOGF "ux0:pspemu/ge_patch_gepresent.log"
#define PSP(a) (0x68000000u + ((a) & 0x0FFFFFFFu))  // vista sin cache de la RAM del PSP (CONVERT_ADDRESS)
#define DESC ((volatile uint32_t *)PSP(0x0A7FF000u))
#define NATIVE PSP(0x0A400000u)

static int nlog;
static void alog(const char *fmt, ...) {
  if (nlog++ > 200) return;
  char b[256];
  va_list ap; va_start(ap, fmt);
  int n = sceClibSnprintf(b, 16, "%7u ", sceKernelGetProcessTimeLow() / 1000);
  n += sceClibVsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap);
  if (n > (int)sizeof b - 1) n = sizeof b - 1;
  SceUID fd = sceIoOpen(LOGF, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) { sceIoWrite(fd, b, n); sceIoClose(fd); }
}

static tai_hook_ref_t ref_w, ref_h;
static SceUID hk_w = -1, hk_h = -1;
static const SceGxmTexture *native;          // la textura nativa de Adrenaline (la de 0x0A400000)
static SceGxmTextureFormat fmt0;             // como la creo Adrenaline, para volver a ella
static SceGxmTextureFilter min0, mag0;
static uint32_t cur[4], out_w = 960, out_h = 544, ack;
static volatile int dato __attribute__((aligned(64))) = 1;  // .data no vacia (segmento RW valido)

static const SceGxmTextureFormat fmts[4] = {   // formatos del PSP; sin alfa (el sombreador de vita2d mezcla)
  SCE_GXM_TEXTURE_FORMAT_U5U6U5_BGR, SCE_GXM_TEXTURE_FORMAT_X1U5U5U5_1BGR,
  SCE_GXM_TEXTURE_FORMAT_X4U4U4U4_1BGR, SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1BGR };

// Zonas que la GPU puede leer (direcciones del PSP): la de Adrenaline y las que registra este plugin
static const uint32_t zona[3][2] = { { 0x0A400000, 0x0A4FF000 }, { 0x0A000000, 0x0A400000 }, { 0x0AE00000, 0x0AF00000 } };
static int zonas = 1;  // cuantas de esas estan registradas
static void mapZonas(void) {
  for (int i = 1; i < 3; i++) {
    int r = sceGxmMapMemory((void *)PSP(zona[i][0]), zona[i][1] - zona[i][0], SCE_GXM_MEMORY_ATTRIB_READ);
    alog("registro para la GPU de %08x-%08x: %08x\n", zona[i][0], zona[i][1], r);
    if (r < 0) break;
    zonas = i + 1;
  }
}
static int enZona(uint32_t a, uint32_t fin) {
  for (int i = 0; i < zonas; i++)
    if (a >= zona[i][0] && fin <= zona[i][1]) return 1;
  return 0;
}

// Aplica el descriptor a la textura nativa (solo si cambio) y deja el tamano de pantalla en out_w/out_h
static void apply(SceGxmTexture *t) {
  uint32_t d[4] = { DESC[4], DESC[5], DESC[6], DESC[7] };
  uint32_t a = d[0], w = d[1], h = d[2], st = d[3] & 0xffff, f = (d[3] >> 16) & 0xff, bpp = f == 3 ? 4 : 2;
  int ok = DESC[0] == 0x31565047 && a >= 0x08000000 && w && h && w <= 1024 && h <= 1024 && st >= w && st <= 1024 &&
           !(st & 7) && f < 4 && enZona(a, a + st * h * bpp);
  if (!ok) { a = 0x0A400000; w = st = 960; h = 544; f = 0; }  // como lo dejo Adrenaline
  if (a != cur[0] || w != cur[1] || h != cur[2] || ((f << 16) | st) != cur[3]) {
    int r1 = sceGxmTextureInitLinear(t, (void *)PSP(a), ok ? fmts[f] : fmt0, st, h, 0), r2 = 0, r3 = 0, r4 = 0;
    int esc = w != 960 || h != 544;  // escalada: filtro lineal; a 960x544 el filtro que eligio Adrenaline
    sceGxmTextureSetMinFilter(t, esc ? SCE_GXM_TEXTURE_FILTER_LINEAR : min0);
    sceGxmTextureSetMagFilter(t, esc ? SCE_GXM_TEXTURE_FILTER_LINEAR : mag0);
    out_w = st * 960 / w; out_h = 544;
    if (nlog < 120 || !ok)
      alog("imagen %08x %ux%u linea %u formato %u -> pantalla %ux%u%s [%x %x %x %x]\n", a, w, h, st, f, out_w, out_h,
           ok ? "" : " (sin descriptor: la de Adrenaline)", r1, r2, r3, r4);
    cur[0] = a; cur[1] = w; cur[2] = h; cur[3] = (f << 16) | st;
  }
  if (ok) DESC[1] = 0x4B000000 | (++ack & 0xffffff);  // "K" + contador: GePatch sabe que hay presentacion
}

static int isNative(const SceGxmTexture *t) {
  if (t == native) return 1;
  if (native || sceGxmTextureGetData(t) != (void *)NATIVE) return 0;
  native = t;
  fmt0 = sceGxmTextureGetFormat(t); min0 = sceGxmTextureGetMinFilter(t); mag0 = sceGxmTextureGetMagFilter(t);
  cur[0] = 0x0A400000; cur[1] = cur[2] = 0;
  mapZonas();  // en el hilo de dibujo de Adrenaline, con GXM ya iniciado
  alog("textura nativa de Adrenaline en %p (formato %08x, filtros %x %x)\n", t, fmt0, min0, mag0);
  return 1;
}

static unsigned int hookW(const SceGxmTexture *t) {
  if (!isNative(t)) return TAI_CONTINUE(unsigned int, ref_w, t);
  apply((SceGxmTexture *)t);
  return out_w;
}
static unsigned int hookH(const SceGxmTexture *t) {
  if (t != native) return TAI_CONTINUE(unsigned int, ref_h, t);
  return out_h;
}

static int hookThread(SceSize args, void *argp) {
  SceIoStat st;
  if (sceIoGetstat("ux0:data/gepresent/off", &st) >= 0) {
    alog("ux0:data/gepresent/off existe: no se engancha nada\n");
    return sceKernelExitDeleteThread(0);
  }
  int r_w = 0, r_h = 0;  // AdrenalineUser lo carga el kernel de Adrenaline despues de arrancar el proceso
  for (int i = 0; i < 600 && (hk_w < 0 || hk_h < 0); i++) {
    if (hk_w < 0) hk_w = r_w = taiHookFunctionImport(&ref_w, "AdrenalineUser", 0xF76B66BD, 0x126A3EB3, hookW);
    if (hk_h < 0) hk_h = r_h = taiHookFunctionImport(&ref_h, "AdrenalineUser", 0xF76B66BD, 0x5420A086, hookH);
    if (hk_w < 0 || hk_h < 0) sceKernelDelayThread(100000);
  }
  alog("ganchos (importaciones de AdrenalineUser) GetWidth %08x GetHeight %08x\n", r_w, r_h);
  if (hk_w < 0 || hk_h < 0)
    return sceKernelExitDeleteThread(0);
  // La RAM del PSP se mapea en el proceso despues de cargar el plugin: leerla antes cierra Adrenaline
  // (C2-12828-1 en v4). Se espera a que el bloque exista, como vpcdram.
  for (;;) {  // solo escribe si GePatch dejo su marca (la zona es del XMB fuera de los juegos)
    SceKernelMemBlockInfo in; in.size = sizeof in;
    uint32_t a = (uint32_t)DESC;
    if (sceKernelGetMemBlockInfoByAddr((void *)a, &in) < 0 || a < (uint32_t)in.mappedBase || a + 16 > (uint32_t)in.mappedBase + in.mappedSize) {
      sceKernelDelayThread(100000);
      continue;
    }
    if (DESC[0] == 0x31565047 && DESC[2] != 0x53455250) DESC[2] = 0x53455250;  // "PRES"
    sceKernelDelayThread(2000);
  }
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
  alog("gepresent v5 (PRES esperando a que la RAM del PSP este mapeada) compilado %s %s (dato %d)\n", __DATE__, __TIME__, dato);
  SceUID th = sceKernelCreateThread("gepresent", hookThread, 0x60, 0x2000, 0, 0, NULL);
  if (th >= 0) sceKernelStartThread(th, 0, NULL);
  return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize args, void *argp) {
  if (hk_w >= 0) taiHookRelease(hk_w, ref_w);
  if (hk_h >= 0) taiHookRelease(hk_h, ref_h);
  return SCE_KERNEL_STOP_SUCCESS;
}
