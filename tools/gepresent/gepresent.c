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
//   +0x20 "LOGV" (v6, este plugin): atiende los buzones del log y de los volcados (ver serveLog/serveDump)
// Log: ux0:pspemu/ge_patch_gepresent.log
//
// v12 (etapa 2 de la arquitectura B): capturador en captura.c. Con GePatch en modo sombra (dev61) sigue el buzon de
// eventos y recorre las listas del juego desde la Vita, sin dibujar; estadisticas en ux0:pspemu/ge_patch_captura.log.
//
// v11 (etapa 1 de la arquitectura B): banco de pruebas GXM en banco.c, apagado por defecto (ux0:data/gepresent/banco).
//
// v6 (plan A, paso 1): la Vita escribe el log de GePatch y sus volcados. GePatch deja el texto o un pedazo del
// volcado en su RAM y un pedido en el descriptor; este plugin lo escribe en ux0:pspemu/ y lo marca hecho. El PSP no
// espera cada escritura a la tarjeta (en un volcado eran ~30 escrituras de 512 KB con el juego detenido).
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
  if (nlog++ > 1000) return;  // (las lineas "imagen" se cortan en 120)
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

// --- Buzones de GePatch (log.h): seq lo sube el PSP, done lo iguala este plugin al terminar ---
#define LOGV 0x56474F4Cu
#define VLOG 16
#define VDUMP 24
static SceUID dump_fd = -1;
static uint32_t nlogs, ndumps;

// v8: el volcado se copia a la RAM de la Vita (anillo de hasta 24 MB) y el pedido se marca hecho en seguida; otro hilo
// lo escribe a la tarjeta despues (la tarjeta escribe ~4 MB/s: en v7 el PSP esperaba ~3,4 s por volcado). Si no hay
// memoria se escribe como en v7 (directo desde la RAM del PSP); si el anillo se llena, el PSP espera a que se vacie.
static uint8_t *ring;
static uint32_t ring_cap;
static volatile uint32_t wpos, rpos, wclose;  // bytes puestos y escritos (crecen siempre); cerrar al vaciar
static uint32_t dump_bytes, dump_wait_us, dump_t0, dump_write_us;
static void writeFrom(SceUID fd, uint32_t a, uint32_t n) {  // directo desde la RAM del PSP (v7)
  if (n) sceIoWrite(fd, (void *)PSP(a), n);
}
void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
// v9: copia por DMA desde la vista sin cache de la RAM del PSP (KBENCH: 7 ms por MB contra 35 leyendo palabra a palabra)
static void ringPut(uint32_t a, uint32_t n) {  // espera si no hay lugar en el anillo
  while (n) {
    while (wpos - rpos >= ring_cap) sceKernelDelayThread(1000);
    uint32_t at = wpos % ring_cap, k = ring_cap - at, room = ring_cap - (wpos - rpos);
    if (k > room) k = room;
    if (k > n) k = n;
    sceDmacMemcpy(ring + at, (void *)PSP(a), k);
    a += k; wpos += k; n -= k;
  }
}
// v9: el log tambien va a un anillo y lo escribe el hilo escritor. En v8 cada pedazo del log se escribia a la tarjeta
// en el hilo que atiende los pedidos (abrir, escribir, cerrar: decenas de ms), y durante una captura el log se vacia
// muy seguido: los pedidos del volcado esperaban detras (el PSP espero 2-3,7 s aunque copiar tomaba ~0,6 s).
static char lring[0x40000];
static volatile uint32_t lw, lr;
static void logPut(uint32_t a, uint32_t n) {
  while (n) {
    while (lw - lr >= sizeof lring) sceKernelDelayThread(1000);
    uint32_t at = lw % sizeof lring, k = sizeof lring - at, room = sizeof lring - (lw - lr);
    if (k > room) k = room;
    if (k > n) k = n;
    sceDmacMemcpy(lring + at, (void *)PSP(a), k);
    a += k; lw += k; n -= k;
  }
}
static void logFlushOut(void) {  // en el hilo escritor
  if (lr == lw) return;
  SceUID fd = sceIoOpen("ux0:pspemu/ge_patch.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  while (lr != lw) {
    uint32_t at = lr % sizeof lring, k = lw - lr;
    if (k > sizeof lring - at) k = sizeof lring - at;
    if (fd >= 0) sceIoWrite(fd, lring + at, k);
    lr += k;
  }
  if (fd >= 0) sceIoClose(fd);
}
static int writerThread(SceSize args, void *argp) {  // escribe el anillo a la tarjeta
  for (;;) {
    logFlushOut();
    if (dump_fd >= 0 && rpos != wpos) {
      uint32_t at = rpos % ring_cap, k = wpos - rpos;
      if (k > ring_cap - at) k = ring_cap - at;
      if (k > 0x40000) k = 0x40000;
      uint32_t t = sceKernelGetProcessTimeLow();
      sceIoWrite(dump_fd, ring + at, k);
      dump_write_us += sceKernelGetProcessTimeLow() - t;
      rpos += k;
    } else if (wclose && dump_fd >= 0 && rpos == wpos) {
      sceIoClose(dump_fd); dump_fd = -1; wclose = 0; ndumps++;
      alog("volcado escrito: %u KB; el PSP espero %u ms (copia a la RAM de la Vita); a la tarjeta %u ms despues de %u ms\n",
           dump_bytes >> 10, dump_wait_us / 1000, dump_write_us / 1000, (sceKernelGetProcessTimeLow() - dump_t0) / 1000);
    } else {
      sceKernelDelayThread(2000);
    }
  }
  return 0;
}
static void ringInit(void) {
  for (uint32_t mb = 24; mb >= 8 && !ring; mb -= 8) {
    SceUID b = sceKernelAllocMemBlock("gepresent_volcado", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, mb << 20, NULL);
    if (b >= 0 && sceKernelGetMemBlockBase(b, (void **)&ring) >= 0) ring_cap = mb << 20;
    else ring = NULL;
  }
  SceUID th = ring ? sceKernelCreateThread("gepresent_escritor", writerThread, 0xA0, 0x2000, 0, 0, NULL) : -1;
  if (th >= 0) sceKernelStartThread(th, 0, NULL);
  else ring = NULL;
  alog("volcados: %s (%u MB en la RAM de la Vita)\n", ring ? "anillo + hilo escritor" : "sin memoria: escritura directa", ring_cap >> 20);
}

static void serveLog(void) {
  uint32_t seq = DESC[VLOG];
  if (seq == DESC[VLOG + 1]) return;
  __sync_synchronize();
  uint32_t n = DESC[VLOG + 2];
  if (n > 0x4000) n = 0x4000;
  if (ring) logPut(0x0A7FA000u, n);  // con el hilo escritor
  else {
    SceUID fd = sceIoOpen("ux0:pspemu/ge_patch.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) { writeFrom(fd, 0x0A7FA000u, n); sceIoClose(fd); }
  }
  if (!nlogs++) alog("log de GePatch: lo escribe la Vita (primer pedazo %u bytes)\n", n);
  DESC[VLOG + 1] = seq;
}
static void serveDump(void) {
  uint32_t seq = DESC[VDUMP];
  if (seq == DESC[VDUMP + 1]) return;
  __sync_synchronize();
  uint32_t a = DESC[VDUMP + 2], n = DESC[VDUMP + 3], f = DESC[VDUMP + 4], t = sceKernelGetProcessTimeLow();
  if (f & 1) {
    char name[48], path[64];
    for (int i = 0; i < 12; i++) ((uint32_t *)name)[i] = DESC[32 + i];
    name[47] = 0;
    for (int i = 0; name[i]; i++) if (name[i] == '/' || name[i] == ':') name[i] = '_';  // solo un nombre
    sceClibSnprintf(path, sizeof path, "ux0:pspemu/%s", name);
    while (ring && (dump_fd >= 0 || wclose)) sceKernelDelayThread(2000);  // el anterior termina de escribirse
    if (dump_fd >= 0) sceIoClose(dump_fd);
    rpos = wpos = 0;
    dump_bytes = dump_wait_us = dump_write_us = 0; dump_t0 = t;
    dump_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    alog("volcado %s: %08x\n", path, dump_fd);
  }
  int ok = dump_fd >= 0 && n && a >= 0x08000000u && a + n <= 0x0C000000u;
  if (ok && ring) ringPut(a, n);
  else if (ok) writeFrom(dump_fd, a, n);
  dump_bytes += n;
  dump_wait_us += sceKernelGetProcessTimeLow() - t;
  if ((f & 2) && dump_fd >= 0) {
    if (ring) wclose = 1;  // lo cierra el hilo escritor al vaciar el anillo
    else {
      sceIoClose(dump_fd); dump_fd = -1; ndumps++;
      alog("volcado escrito: %u KB en %u ms (directo)\n", dump_bytes >> 10, (sceKernelGetProcessTimeLow() - dump_t0) / 1000);
    }
  }
  DESC[VDUMP + 1] = seq;
}

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

void bancoStart(void), bancoStop(void);  // banco.c
void capturaStart(void);                  // captura.c

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
  bancoStart();  // v11: solo si existe ux0:data/gepresent/banco_v12 (v12: el archivo "banco" viejo se ignora)
  // La RAM del PSP se mapea en el proceso despues de cargar el plugin: leerla antes cierra Adrenaline
  // (C2-12828-1 en v4). Se espera a que el bloque exista, como vpcdram.
  for (int mapped = 0;;) {  // solo escribe si GePatch dejo su marca (la zona es del XMB fuera de los juegos)
    SceKernelMemBlockInfo in; in.size = sizeof in;
    uint32_t a = (uint32_t)DESC;
    if (!mapped && (sceKernelGetMemBlockInfoByAddr((void *)a, &in) < 0 || a < (uint32_t)in.mappedBase ||
                    a + 16 > (uint32_t)in.mappedBase + in.mappedSize)) {
      sceKernelDelayThread(100000);
      continue;
    }
    if (!mapped) capturaStart();  // v12: el hilo de captura espera el buzon del modo sombra
    mapped = 1;  // (v9: se comprueba una vez; la RAM del PSP queda mapeada mientras viva el proceso)
    int busy = 0;
    if (DESC[0] == 0x31565047) {
      if (DESC[2] != 0x53455250) DESC[2] = 0x53455250;  // "PRES"
      if (DESC[8] != LOGV) DESC[8] = LOGV;               // v6: log y volcados
      busy = DESC[VLOG] != DESC[VLOG + 1] || DESC[VDUMP] != DESC[VDUMP + 1];
      serveLog();
      serveDump();
    }
    sceKernelDelayThread(busy ? 200 : 2000);  // durante una captura los pedidos llegan seguidos
  }
}

// v12.6 (diagnostico): cada hilo que crea el emulador (ScePspemu) con su prioridad y nucleos, para elegir donde corre
// el capturador sin quitarle tiempo al audio (v12.4 con prioridad 0x40 en el nucleo 2 lo cortaba)
static tai_hook_ref_t ref_ct;
static SceUID hk_ct = -1;
SceUID emu_th[256]; int emu_aff[256], emu_n;  // v12.10: captura.c mide cuanto procesador usa cada uno
static SceUID hookCT(const char *name, SceKernelThreadEntry entry, int prio, int stack, SceUInt attr, int aff,
                     const SceKernelThreadOptParam *opt) {
  SceUID r = TAI_CONTINUE(SceUID, ref_ct, name, entry, prio, stack, attr, aff, opt);
  alog("hilo de ScePspemu: %s prioridad %x nucleos %x pila %x atributos %x -> %08x\n", name ? name : "?", prio, aff, stack, attr, r);
  if (r >= 0 && emu_n < 256) { emu_th[emu_n] = r; emu_aff[emu_n] = aff; emu_n++; }
  return r;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
  alog("gepresent v13.5 (v11.3 + capturador de la etapa 2 con memoria de sublistas, espejo de la VRAM, T4/T6 y vida de los datos, activo con GePatch en modo sombra) compilado %s %s (dato %d)\n", __DATE__, __TIME__, dato);
  SceIoStat bst;  // v11.2: con el banco no se reserva el anillo de volcados (24 MB que el banco necesita)
  if (sceIoGetstat("ux0:data/gepresent/banco_v12", &bst) < 0) ringInit();
  else alog("banco de pruebas activo: sin anillo de volcados (se escriben directo)\n");
  hk_ct = taiHookFunctionImport(&ref_ct, "ScePspemu", 0xCAE9ACE6, 0xC5C11EE7, hookCT);
  alog("gancho de sceKernelCreateThread de ScePspemu: %08x\n", hk_ct);
  SceUID th = sceKernelCreateThread("gepresent", hookThread, 0x60, 0x2000, 0, 0, NULL);
  if (th >= 0) sceKernelStartThread(th, 0, NULL);
  return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize args, void *argp) {
  if (hk_w >= 0) taiHookRelease(hk_w, ref_w);
  if (hk_h >= 0) taiHookRelease(hk_h, ref_h);
  if (hk_ct >= 0) taiHookRelease(hk_ct, ref_ct);
  bancoStop();
  return SCE_KERNEL_STOP_SUCCESS;
}
