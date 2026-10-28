// banco.c (gepresent v11.3): banco de pruebas GXM de la etapa 1 de la arquitectura B (arquitectura_dibujo_en_vita.md §6).
// Apagado por defecto: solo corre si existe ux0:data/gepresent/banco_v12 (v12; antes "banco", que quedo en la Vita) (su contenido = segundos de espera antes de
// empezar; 20 si esta vacio). Todo pasa dentro del proceso de Adrenaline, en su hilo de dibujo, justo despues de su
// escena (gancho a la importacion de sceGxmEndScene), en una superficie propia de 960x544 (8888 + z/stencil):
//   1. memoria que se puede pedir por tipo (CDRAM, LPDDR): el bloque mas grande y el total; relojes del ARM, bus y GPU;
//   2. costo por draw en el contexto inmediato de Adrenaline: A sin cambios, B cambia textura, P cambia textura P4
//      con su paleta, C cambia programa, U cambia uniformes (64 bytes, como una matriz), T textura + uniformes (GT),
//      D lo mismo que A repartido en 8 escenas; v11.2: O = A fuera de la pantalla (solo trabajo de vertices en la
//      GPU), S = A con un solo SetVertexStream por draw, M = los N cuadros en 1 draw (lo maximo que da juntar draws);
//      cada caso N draws de un cuadro chico, 3 veces;
//   3. lo mismo grabado en un contexto diferido desde otro hilo y ejecutado en la escena (lo que haria B); v11.2: T
//      tambien grabado en 2 hilos a la vez (mitad cada uno), para ver si grabar escala con los nucleos;
//   4. la GPU leyendo vertices de la RAM del PSP contra memoria propia (LPDDR y CDRAM);
//   5. presentacion: durante ~10 s Adrenaline muestra la superficie propia (la imagen del juego dibujada por el banco
//      + un cuadro de color que se mueve arriba) en vez de su textura de 512x272. Solo sin GePatch (juego abierto con L).
// v11.1 no pudo crear el contexto diferido ni la memoria propia de la prueba 4 (la memoria del proceso de Adrenaline es
// poca: bloques de 6-7 MB, y el anillo de volcados de gepresent ya usa 24 MB). v11.2: con el banco, gepresent no reserva
// el anillo, y la memoria grande sale de LPDDR fisica contigua si el tipo pedido no alcanza.
// v11.2 hizo caer la GPU ("GPU Driver detects GPU Crash") en el segundo caso del contexto diferido: el pozo se vaciaba
// (pos = 0) antes de cada grabacion, pero el contexto diferido sigue usando los pedazos que ya recibio; el pozo volvia a
// entregar esa misma memoria para otro anillo y la GPU leia comandos pisados. v11.3: el pozo nunca se reusa (12 + 4 MB,
// pedidos antes que el resto) y una lista grabada sin memoria no se ejecuta.
// Resultados: ux0:pspemu/ge_patch_banco.log (el relevo lo baja solo). Tiempos con sceKernelGetProcessTimeLow (us).
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/gxm.h>
#include <psp2/power.h>
#include <taihen.h>
#include <stdarg.h>
#include "banco_shaders.h"

#define BLOG "ux0:pspemu/ge_patch_banco.log"
#define PSP(a) (0x68000000u + ((a) & 0x0FFFFFFFu))
#define N 2000        // draws por caso
#define REP 3
#define NV 4096       // vertices por draw en la prueba de lectura de vertices
#define NVD 100       // draws de esa prueba (100 x 64 KB leidos de cada origen)
#define NTEX 64
#define CDRAM SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW
#define UNC SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE
#define PHY SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW
#define RD SCE_GXM_MEMORY_ATTRIB_READ
#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)

void *memset(void *d, int c, unsigned n) { char *p = d; while (n--) *p++ = c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { char *p = d; const char *q = s; while (n--) *p++ = *q++; return d; }

static void blog(const char *fmt, ...) {
  char b[320];
  va_list ap; va_start(ap, fmt);
  int n = sceClibSnprintf(b, 16, "%7u ", sceKernelGetProcessTimeLow() / 1000);
  n += sceClibVsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap);
  if (n > (int)sizeof b - 1) n = sizeof b - 1;
  SceUID fd = sceIoOpen(BLOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) { sceIoWrite(fd, b, n); sceIoClose(fd); }
}
static uint32_t now(void) { return sceKernelGetProcessTimeLow(); }

// --- memoria para la GPU ---
static void *gmem1(uint32_t type, uint32_t size, int attr) {
  size = (size + (type == PHY ? 0xFFFFF : 0x3FFFF)) & ~(type == PHY ? 0xFFFFFu : 0x3FFFFu);  // 1 MB fisica, 256 KB el resto
  void *p = 0;
  SceUID b = sceKernelAllocMemBlock("gepresent_banco", type, size, 0);
  if (b < 0 || sceKernelGetMemBlockBase(b, &p) < 0) return 0;
  if (attr && sceGxmMapMemory(p, size, attr) < 0) { sceKernelFreeMemBlock(b); return 0; }
  return p;
}
static void *gmem(uint32_t type, uint32_t size, int attr) {  // si el tipo pedido no alcanza: fisica contigua, despues LPDDR
  void *p = gmem1(type, size, attr);
  if (!p && type != PHY) p = gmem1(PHY, size, attr);
  if (!p && type != UNC) p = gmem1(UNC, size, attr);
  return p;
}
static const char *tipo(void *p) {  // de que memoria salio (para el log)
  SceKernelMemBlockInfo in; in.size = sizeof in;
  if (!p || sceKernelGetMemBlockInfoByAddr(p, &in) < 0) return "-";
  return in.type == CDRAM ? "CDRAM" : in.type == PHY ? "LPDDR fisica" : in.type == UNC ? "LPDDR" : "otra";
}
static void gfree(void *p) {
  if (!p) return;
  sceGxmUnmapMemory(p);
  sceKernelFreeMemBlock(sceKernelFindMemBlockByAddr(p, 0));
}
static uint32_t probe(uint32_t type) {  // MB que se pueden pedir de una vez (busqueda binaria, se libera en seguida)
  uint32_t lo = 0, hi = 512;
  while (lo < hi) {
    uint32_t m = (lo + hi + 1) / 2;
    SceUID b = sceKernelAllocMemBlock("gepresent_prueba", type, m << 20, 0);
    if (b >= 0) { sceKernelFreeMemBlock(b); lo = m; } else hi = m - 1;
  }
  return lo;
}
static uint32_t total(uint32_t type) {  // MB en total: bloques de 1 MB hasta que no haya mas (y se liberan)
  SceUID b[256];
  uint32_t n = 0;
  while (n < 256 && (b[n] = sceKernelAllocMemBlock("gepresent_prueba", type, 1 << 20, 0)) >= 0) n++;
  for (uint32_t i = 0; i < n; i++) sceKernelFreeMemBlock(b[i]);
  return n;
}

// --- programas (patcher propio, como vita2d) ---
static char heap[0x20000] __attribute__((aligned(16)));
static uint32_t hpos;
static void *halloc(void *ud, unsigned int n) {
  n = (n + 15) & ~15u;
  if (hpos + n > sizeof heap) return 0;
  hpos += n;
  return heap + hpos - n;
}
static void hfree(void *ud, void *p) {}
static SceGxmShaderPatcher *pat;
static SceGxmVertexProgram *vp;
static SceGxmFragmentProgram *fp[2];
static const SceGxmProgramParameter *pxf;

static int programas(void) {
  SceGxmShaderPatcherParams pp;
  memset(&pp, 0, sizeof pp);
  pp.hostAllocCallback = halloc; pp.hostFreeCallback = hfree;
  pp.bufferMem = gmem(UNC, 0x40000, RD); pp.bufferMemSize = 0x40000;
  pp.vertexUsseMem = gmem(UNC, 0x40000, 0); pp.vertexUsseMemSize = 0x40000;
  pp.fragmentUsseMem = gmem(UNC, 0x40000, 0); pp.fragmentUsseMemSize = 0x40000;
  if (!pp.bufferMem || !pp.vertexUsseMem || !pp.fragmentUsseMem) return -1;
  int r = sceGxmMapVertexUsseMemory(pp.vertexUsseMem, 0x40000, &pp.vertexUsseOffset);
  if (r >= 0) r = sceGxmMapFragmentUsseMemory(pp.fragmentUsseMem, 0x40000, &pp.fragmentUsseOffset);
  if (r >= 0) r = sceGxmShaderPatcherCreate(&pp, &pat);
  SceGxmShaderPatcherId iv, i1, i2;
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)v_gxp, &iv);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)f1_gxp, &i1);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)f2_gxp, &i2);
  if (r < 0) return r;
  const SceGxmProgram *pv = sceGxmShaderPatcherGetProgramFromId(iv);
  pxf = sceGxmProgramFindParameterByName(pv, "xf");
  const SceGxmProgramParameter *pp1 = sceGxmProgramFindParameterByName(pv, "pos"),
    *pp2 = sceGxmProgramFindParameterByName(pv, "uv"), *pp3 = sceGxmProgramFindParameterByName(pv, "col");
  if (!pxf || !pp1 || !pp2 || !pp3) return -2;
  SceGxmVertexAttribute at[3] = {
    { 0, 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2, sceGxmProgramParameterGetResourceIndex(pp1) },
    { 0, 8, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2, sceGxmProgramParameterGetResourceIndex(pp2) },
    { 1, 0, SCE_GXM_ATTRIBUTE_FORMAT_U8N, 4, sceGxmProgramParameterGetResourceIndex(pp3) } };
  SceGxmVertexStream st[2] = { { 16, SCE_GXM_INDEX_SOURCE_INDEX_16BIT }, { 16, SCE_GXM_INDEX_SOURCE_INDEX_16BIT } };
  if ((r = sceGxmShaderPatcherCreateVertexProgram(pat, iv, at, 3, st, 2, &vp)) < 0) return r;
  if ((r = sceGxmShaderPatcherCreateFragmentProgram(pat, i1, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                    SCE_GXM_MULTISAMPLE_NONE, 0, pv, &fp[0])) < 0) return r;
  return sceGxmShaderPatcherCreateFragmentProgram(pat, i2, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                  SCE_GXM_MULTISAMPLE_NONE, 0, pv, &fp[1]);
}

// --- destino, datos de prueba ---
typedef struct { float x, y, u, v; } V;
static SceGxmRenderTarget *rt;
static SceGxmColorSurface cs[2];
static SceGxmDepthStencilSurface ds;
static SceGxmTexture otex[2], tex[NTEX], ptex[NTEX];
static V *vb, *vpos;          // cuadros de los draws (N + pantalla completa + marca) y vertices de la prueba V
static uint32_t *cb;          // color blanco, 16 bytes por vertice (stream 1)
static uint16_t *ib, *mb;     // 0..NV-1; caso M: N cuadros como triangulos
static uint8_t *vsrc[3];      // origenes de la prueba V: LPDDR propia, CDRAM propia, RAM del PSP
static const char *vnom[3] = { "LPDDR propia", "CDRAM propia", "RAM del PSP" };
static void *surf[2], *zbuf;

static void quad(V *v, float x, float y, float w, float h, float u1, float v1) {
  v[0] = (V){ x, y, 0, 0 }; v[1] = (V){ x + w, y, u1, 0 }; v[2] = (V){ x, y + h, 0, v1 }; v[3] = (V){ x + w, y + h, u1, v1 };
}

static int destino(void) {
  SceGxmRenderTargetParams rp;
  memset(&rp, 0, sizeof rp);
  rp.width = 960; rp.height = 544; rp.scenesPerFrame = 8; rp.driverMemBlock = -1;
  int r = sceGxmCreateRenderTarget(&rp, &rt);
  if (r < 0) return r;
  for (int i = 0; i < 2; i++) {
    void *s = gmem(CDRAM, 1024 * 544 * 4, RW);
    if (!s) return -3;
    surf[i] = s;
    sceGxmColorSurfaceInit(&cs[i], SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                           SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, 960, 544, 1024, s);
    sceGxmTextureInitLinear(&otex[i], s, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, 1024, 544, 0);
    sceGxmTextureSetMinFilter(&otex[i], SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(&otex[i], SCE_GXM_TEXTURE_FILTER_LINEAR);
  }
  void *z = zbuf = gmem(CDRAM, 960 * 544 * 4, RW);
  if (!z) return -4;
  sceGxmDepthStencilSurfaceInit(&ds, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, 960, z, 0);
  sceGxmDepthStencilSurfaceSetBackgroundDepth(&ds, 1.0f);
  // texturas: NTEX de 32x32 8888 y NTEX P4 de 32x32 con su paleta de 16 colores (alineada a 64)
  uint32_t *tm = gmem(UNC, NTEX * 4096 + NTEX * 512 + NTEX * 64, RD);
  if (!tm) return -5;
  uint8_t *pd = (uint8_t *)(tm + NTEX * 1024);
  uint32_t *pal = (uint32_t *)(pd + NTEX * 512);
  for (int i = 0; i < NTEX; i++) {
    for (int j = 0; j < 1024; j++) tm[i * 1024 + j] = 0xFF000000u | (i * 0x040404u) | ((j & 32) ? 0x3000 : 0);
    for (int j = 0; j < 512; j++) pd[i * 512 + j] = j * 7 + i;
    for (int j = 0; j < 16; j++) pal[i * 16 + j] = 0xFF000000u | ((j * 0x111111u) ^ (i * 0x0301));
    sceGxmTextureInitLinear(&tex[i], tm + i * 1024, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, 32, 32, 0);
    sceGxmTextureInitLinear(&ptex[i], pd + i * 512, SCE_GXM_TEXTURE_FORMAT_P4_ABGR, 32, 32, 0);
    sceGxmTextureSetPalette(&ptex[i], pal + i * 16);
  }
  // vertices de los draws chicos: cuadros de 16x16 repartidos por la pantalla; despues pantalla completa y marca
  vb = gmem(UNC, (N + 2) * 4 * sizeof(V), RD);
  cb = gmem(UNC, N * 4 * 16, RD);  // un color por vertice (paso 16), para los N cuadros juntos (caso M)
  mb = gmem(UNC, N * 6 * 2, RD);
  ib = gmem(UNC, NV * 2, RD);
  vpos = gmem(UNC, NV * sizeof(V), RD);
  if (!vb || !cb || !ib || !vpos || !mb) return -6;
  for (int i = 0; i < N; i++) {  // caso M: los N cuadros como lista de triangulos
    uint16_t *m = mb + i * 6, v = i * 4;
    m[0] = v; m[1] = v + 1; m[2] = v + 2; m[3] = v + 2; m[4] = v + 1; m[5] = v + 3;
  }
  for (int i = 0; i < N; i++) quad(vb + i * 4, (i * 37) % 944, (i * 23) % 528, 16, 16, 1, 1);
  quad(vb + N * 4, 0, 0, 960, 544, 480.0f / 512.0f, 1);  // la textura de Adrenaline: 480 de sus 512 de ancho
  quad(vb + N * 4 + 4, 0, 0, 64, 64, 1, 1);
  for (int i = 0; i < N * 4 * 4; i++) cb[i] = 0xFFFFFFFFu;
  for (int i = 0; i < NV; i++) { ib[i] = i; vpos[i] = (V){ 4 + (i & 1), 4 + ((i >> 1) & 1), 0, 0 }; }
  return 0;
}

static void setXf(SceGxmContext *c, float dx, float dy) {  // 4 vec4 = 64 bytes de uniformes, como una matriz
  void *u;
  sceGxmReserveVertexDefaultUniformBuffer(c, &u);
  float d[16] = { 2 / 960.0f, -2 / 544.0f, -1 + dx * (2 / 960.0f), 1 - dy * (2 / 544.0f), 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
  sceGxmSetUniformDataF(u, pxf, 0, 16, d);
}

// --- casos ---
enum { A, B, P, C, U, T, D, O, S, M, NC };
static const char *cnom[NC] = { "A sin cambios", "B textura", "P textura P4+paleta", "C programa", "U uniformes 64B",
                                "T textura+uniformes", "D 8 escenas", "O fuera de pantalla", "S un stream por draw",
                                "M todo en 1 draw" };
static void inicio(SceGxmContext *c) {
  sceGxmSetVertexProgram(c, vp);
  sceGxmSetFragmentProgram(c, fp[0]);
  sceGxmSetFragmentTexture(c, 0, &tex[0]);
  sceGxmSetVertexStream(c, 1, cb);
  setXf(c, 0, 0);
}
static void draws(SceGxmContext *c, int k, int n0, int n) {
  for (int i = n0; i < n0 + n; i++) {
    if (k == B || k == T) sceGxmSetFragmentTexture(c, 0, &tex[i & (NTEX - 1)]);
    else if (k == P) sceGxmSetFragmentTexture(c, 0, &ptex[i & (NTEX - 1)]);
    else if (k == C) sceGxmSetFragmentProgram(c, fp[i & 1]);
    if (k == U || k == T) setXf(c, 0, 0);
    sceGxmSetVertexStream(c, 0, vb + i * 4);  // B: cada draw trae sus propios vertices
    if (k != S) sceGxmSetVertexStream(c, 1, cb);  // S: el color quedo puesto una vez (en inicio)
    sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
  }
}
static void escena(SceGxmContext *c, int s) { sceGxmBeginScene(c, 0, rt, 0, 0, 0, &cs[s], &ds); }
static void dec(char *b, uint32_t x) { sceClibSnprintf(b, 16, "%u.%02u", x / 100, x % 100); }  // centesimas

static void inmediato(SceGxmContext *c, int k, int rep) {
  sceGxmFinish(c);  // que la GPU termine lo de Adrenaline: el tiempo de GPU queda solo para este caso
  uint32_t t0 = now();
  escena(c, 0);
  inicio(c);
  if (k == D)
    for (int s = 0; s < 8; s++) {
      if (s) { sceGxmEndScene(c, 0, 0); escena(c, 0); inicio(c); }
      draws(c, A, s * (N / 8), N / 8);
    }
  else if (k == O) { setXf(c, -3000, 0); draws(c, A, 0, N); }  // todos fuera: la GPU solo procesa vertices
  else if (k == M) {
    sceGxmSetVertexStream(c, 0, vb);
    sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, mb, N * 6);
  }
  else draws(c, k, 0, N);
  uint32_t t1 = now();
  sceGxmEndScene(c, 0, 0);
  uint32_t t2 = now();
  sceGxmFinish(c);
  uint32_t t3 = now();
  char a[16], g[16];
  dec(a, (t1 - t0) * 100 / N); dec(g, (t3 - t1) / 10);
  blog("inmediato %-22s rep %d: CPU %s us/draw (%u us), EndScene %u us, GPU hasta terminar %s ms\n", cnom[k], rep, a,
       t1 - t0, t2 - t1, g);
}

// --- contexto diferido: grabado en el hilo del banco, ejecutado en la escena del hilo de Adrenaline ---
// Cada contexto diferido tiene su pozo de memoria (lo que pide GXM al grabar sale de ahi; se vacia antes de cada caso)
typedef struct { uint8_t *p; uint32_t pos, cap, falta; } Pozo;
static SceGxmContext *dctx[2];
static Pozo pozo[2];
static void *dmem(void *args, SceSize req, SceSize *size) {
  Pozo *z = args;
  uint32_t n = req < 0x10000 ? 0x10000 : (req + 0xFFF) & ~0xFFFu;
  if (z->pos + n > z->cap) { z->falta++; return 0; }
  z->pos += n; *size = n;
  return z->p + z->pos - n;
}
static char dhost[2][0x4000] __attribute__((aligned(16)));
static int diferido(int i) {
  Pozo *z = &pozo[i];
  z->cap = (i ? 4 : 12) << 20;  // v11.2: 2000 draws grabados = 320 KB (A); 21 grabaciones en el 0, 3 medias en el 1
  z->p = gmem(UNC, z->cap, RW);
  if (!z->p) return -7;
  SceGxmDeferredContextParams dp;
  memset(&dp, 0, sizeof dp);
  dp.hostMem = dhost[i]; dp.hostMemSize = sizeof dhost[i];
  dp.vdmCallback = dp.vertexCallback = dp.fragmentCallback = dmem;
  dp.callbackData = z;
  int r = sceGxmCreateDeferredContext(&dp, &dctx[i]);  // primero sin anillos iniciales (los pide a dmem)
  if (r < 0) {
    blog("contexto diferido %d sin anillos iniciales: %08x; probando con anillos\n", i, r);
    SceSize n;
    dp.vdmRingBufferMem = dmem(z, 0x10000, &n); dp.vdmRingBufferMemSize = n;
    dp.vertexRingBufferMem = dmem(z, 0x40000, &n); dp.vertexRingBufferMemSize = n;
    dp.fragmentRingBufferMem = dmem(z, 0x40000, &n); dp.fragmentRingBufferMemSize = n;
    r = sceGxmCreateDeferredContext(&dp, &dctx[i]);
  }
  return r;
}
static SceGxmCommandList lista[2];
static volatile int dk = -1, drep, dlisto, drec_us, drec_r, dn, d2_us[2], d2_r[2], d2_go;  // dlisto: 1 grabada, 0 ejecutada
static int grabar1(int i, int k, int n0, int n) {  // (el pozo NO se vacia: el contexto sigue usando lo que recibio)
  int r = sceGxmBeginCommandList(dctx[i]);
  if (r >= 0) {
    inicio(dctx[i]);
    draws(dctx[i], k, n0, n);
    r = sceGxmEndCommandList(dctx[i], &lista[i]);
  }
  return r;
}
static int hilo2(SceSize args, void *argp) {  // segundo grabador: la mitad de T a la vez que el hilo del banco
  for (;;) {
    while (!d2_go) sceKernelDelayThread(200);
    uint32_t t0 = now();
    d2_r[1] = grabar1(1, T, N / 2, N / 2);
    d2_us[1] = now() - t0;
    d2_go = 0;
  }
  return 0;
}
static void grabar(int k, int rep, int hilos) {
  uint32_t t0 = now();
  if (hilos == 2) {
    d2_go = 1;
    uint32_t t1 = now();
    d2_r[0] = grabar1(0, k, 0, N / 2);
    d2_us[0] = now() - t1;
    while (d2_go) ;  // espera activa: el otro hilo termina en milisegundos
    drec_r = d2_r[0] < 0 ? d2_r[0] : d2_r[1];
  } else drec_r = grabar1(0, k, 0, N);
  drec_us = now() - t0; dk = k; drep = rep; dn = hilos;
  __sync_synchronize();
  dlisto = 1;
}
static void ejecutar(SceGxmContext *c) {
  sceGxmFinish(c);
  uint32_t t0 = now();
  escena(c, 0);
  if (pozo[0].falta || pozo[1].falta) drec_r = -8;  // grabada sin memoria: no se ejecuta
  int r = drec_r >= 0 ? sceGxmExecuteCommandList(c, &lista[0]) : drec_r;
  if (r >= 0 && dn == 2) r = sceGxmExecuteCommandList(c, &lista[1]);
  uint32_t t1 = now();
  sceGxmEndScene(c, 0, 0);
  sceGxmFinish(c);
  uint32_t t3 = now();
  char a[16], g[16];
  dec(a, drec_us * 100 / N); dec(g, (t3 - t1) / 10);
  if (dn == 2)
    blog("diferido  %-22s rep %d en 2 hilos: grabar %s us/draw de reloj (%u us; hilos %u y %u us), ejecutar %u us, GPU %s ms [%08x, pozos %u+%u KB%s]\n",
         cnom[dk], drep, a, drec_us, d2_us[0], d2_us[1], t1 - t0, g, r, pozo[0].pos >> 10, pozo[1].pos >> 10,
         pozo[0].falta || pozo[1].falta ? ", SIN MEMORIA" : "");
  else
    blog("diferido  %-22s rep %d: grabar %s us/draw (%u us, otro hilo), ejecutar %u us, GPU %s ms [%08x, pozo %u KB%s]\n",
         cnom[dk], drep, a, drec_us, t1 - t0, g, r, pozo[0].pos >> 10, pozo[0].falta ? ", SIN MEMORIA" : "");
}

// --- prueba V: la GPU leyendo vertices ---
static void pruebaV(SceGxmContext *c, int o, int rep) {
  if (!vsrc[o]) return;
  sceGxmFinish(c);
  uint32_t t0 = now();
  escena(c, 0);
  inicio(c);
  for (int i = 0; i < NVD; i++) {
    sceGxmSetVertexStream(c, 0, vpos);
    sceGxmSetVertexStream(c, 1, vsrc[o] + i * NV * 16);
    sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, NV);
  }
  sceGxmEndScene(c, 0, 0);
  sceGxmFinish(c);
  uint32_t us = now() - t0;
  blog("vertices desde %-12s rep %d: %u vertices (%u KB con paso 16) en %u us enviar + GPU (%u Mvert/s)\n", vnom[o], rep, NVD * NV,
       NVD * NV * 16 >> 10, us, us ? NVD * NV / us : 0);
}

// --- ganchos y fases ---
static tai_hook_ref_t ref_end, ref_tex;
static SceUID hk_end = -1, hk_tex = -1;
static volatile int listo, fase, paso, frames, espera = 20 * 60;
static const SceGxmTexture *psp_tex;
static int cur, pres, pres_cambios;
static uint32_t pres_us;

static int hookTex(SceGxmContext *c, unsigned int i, const SceGxmTexture *t) {
  if (!psp_tex && t && sceGxmTextureGetWidth(t) == 512 && sceGxmTextureGetHeight(t) == 272) psp_tex = t;
  if (pres && t == psp_tex) { t = &otex[cur]; pres_cambios++; }
  return TAI_CONTINUE(int, ref_tex, c, i, t);
}

static void presentar(SceGxmContext *c) {  // dibuja la imagen del juego y la marca en la otra superficie
  uint32_t t0 = now();
  int s = cur ^ 1;
  escena(c, s);
  sceGxmSetVertexProgram(c, vp);
  sceGxmSetFragmentProgram(c, fp[0]);
  setXf(c, 0, 0);
  sceGxmSetFragmentTexture(c, 0, psp_tex);
  sceGxmSetVertexStream(c, 0, vb + N * 4);
  sceGxmSetVertexStream(c, 1, cb);
  sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
  setXf(c, (frames * 4) % 896, 24);
  sceGxmSetFragmentProgram(c, fp[1]);
  sceGxmSetFragmentTexture(c, 0, &tex[(frames / 8) & (NTEX - 1)]);
  sceGxmSetVertexStream(c, 0, vb + N * 4 + 4);
  sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
  sceGxmEndScene(c, 0, 0);
  cur = s;
  pres_us += now() - t0;
}

static int hookEnd(SceGxmContext *c, const SceGxmNotification *v, const SceGxmNotification *f) {
  int r = TAI_CONTINUE(int, ref_end, c, v, f);
  frames++;
  if (!listo) return r;
  if (fase == 0) {
    if (frames < espera) return r;
    blog("empieza: frame %d de Adrenaline, textura del juego (512x272) %s\n", frames, psp_tex ? "vista" : "NO vista");
    fase = 1; paso = 0;
  }
  if (fase == 1) {  // contexto inmediato: un caso por frame
    inmediato(c, paso / REP, paso % REP);
    if (++paso == NC * REP) { fase = dctx[0] ? 2 : 3; paso = 0; }
    return r;  // (fase 2 empieza a contar en el frame siguiente)
  } else if (fase == 2) {  // contexto diferido: el hilo del banco graba, aca se ejecuta
    if (dlisto) {
      ejecutar(c);
      dlisto = 0;
      if (++paso == (D + 2) * REP) { fase = 3; paso = 0; }  // A..T, S y T en 2 hilos, REP veces cada uno
    }
  } else if (fase == 3) {
    pruebaV(c, paso / 2, paso % 2);  // (un origen sin memoria se salta)
    if (++paso == 6) {
      fase = 4; paso = 0;
      if (!psp_tex) { blog("presentacion: no se vio la textura de 512x272 (GePatch prendido o filtro Original): se salta\n"); fase = 5; }
    }
  } else if (fase == 4) {
    if (paso == 0) blog("presentacion: 10 s mostrando la superficie del banco (imagen del juego + cuadro de color arriba)\n");
    presentar(c);
    pres = 1;
    if (paso == 300) {  // ¿la imagen del juego llega a la superficie? (el usuario vio negro en v11.1)
      sceGxmFinish(c);
      const uint32_t *pj = sceGxmTextureGetData(psp_tex), *pb = surf[cur];
      blog("presentacion: pixel (240,136) del juego %08x -> (480,272) de la superficie %08x; (20,20) %08x -> %08x; formato juego %08x\n",
           pj[136 * 512 + 240], pb[272 * 1024 + 480], pj[20 * 512 + 20], pb[40 * 1024 + 40], sceGxmTextureGetFormat(psp_tex));
    }
    if (++paso == 600) {
      pres = 0;
      char a[16];
      dec(a, pres_us * 100 / 600);
      blog("presentacion: %d frames, la textura del juego se cambio %d veces, CPU %s us por frame (escena propia)\n", paso,
           pres_cambios, a);
      fase = 5;
    }
  }
  if (fase == 5) { blog("fin del banco\n"); fase = 6; }
  return r;
}

static int bancoThread(SceSize args, void *argp) {
  while (frames < 30) sceKernelDelayThread(100000);  // GXM ya iniciado por Adrenaline
  blog("relojes MHz: ARM %d, bus %d, GPU %d, GPU xbar %d\n", scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
       scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
  blog("memoria, bloque mas grande / total en bloques de 1 MB (MB): CDRAM %u/%u, LPDDR sin cache %u/%u, LPDDR %u, LPDDR fisica contigua %u/%u\n",
       probe(CDRAM), total(CDRAM), probe(UNC), total(UNC), probe(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW), probe(PHY), total(PHY));
  int r1 = programas(), r2 = r1 >= 0 ? destino() : 0;
  blog("programas %08x, destino %08x (superficies en %s y %s, z en %s)\n", r1, r2, tipo(surf[0]), tipo(surf[1]), tipo(zbuf));
  if (r1 < 0 || r2 < 0) return sceKernelExitDeleteThread(0);
  int r3 = diferido(0), r5 = r3 >= 0 ? diferido(1) : 0;
  blog("contextos diferidos: %08x %08x (pozos en %s, %s)\n", r3, r5, tipo(pozo[0].p), tipo(pozo[1].p));
  if (r3 < 0) dctx[0] = 0;
  if (r5 < 0) dctx[1] = 0;
  SceUID th2 = dctx[1] ? sceKernelCreateThread("gepresent_banco2", hilo2, 0xA0, 0x4000, 0, 0, 0) : -1;
  if (th2 >= 0) sceKernelStartThread(th2, 0, 0);
  else dctx[1] = 0;
  vsrc[0] = gmem1(UNC, NVD * NV * 16, RD);
  if (!vsrc[0]) vsrc[0] = gmem1(PHY, NVD * NV * 16, RD);
  vsrc[1] = gmem1(CDRAM, NVD * NV * 16, RD);
  void *ps = (void *)PSP(0x08800000u);  // RAM del juego, solo lectura para la GPU
  int r4 = sceGxmMapMemory(ps, (NVD * NV * 16 + 0x3FFFF) & ~0x3FFFFu, RD);
  vsrc[2] = r4 >= 0 ? ps : 0;
  if (vsrc[0]) vnom[0] = tipo(vsrc[0]);
  blog("prueba V: %s %p, CDRAM %p, RAM del PSP %08x (registro para la GPU %08x); memoria libre despues: LPDDR %u MB, CDRAM %u MB, fisica %u MB\n",
       vnom[0], vsrc[0], vsrc[1], (uint32_t)ps, r4, total(UNC), total(CDRAM), total(PHY));
  listo = 1;
  if (dctx[0])  // graba cada caso cuando el anterior ya se ejecuto (A..T y S; despues T en 2 hilos)
    for (int j = 0; j <= D + 1; j++)
      for (int rep = 0; rep < REP; rep++) {
        int k = j < D ? j : j == D ? S : T, h = j == D + 1 ? 2 : 1;
        if (h == 2 && !dctx[1]) { k = T; h = 1; }
        while (fase < 2 || dlisto) sceKernelDelayThread(2000);
        grabar(k, rep, h);
      }
  while (fase < 4) sceKernelDelayThread(100000);
  for (int i = 0; i < 2; i++) gfree(vsrc[i]);  // la prueba V ya termino
  if (vsrc[2]) sceGxmUnmapMemory(ps);
  return sceKernelExitDeleteThread(0);
}

void bancoStart(void) {  // desde el hilo de gepresent, con AdrenalineUser ya cargado
  SceUID fd = sceIoOpen("ux0:data/gepresent/banco_v12", SCE_O_RDONLY, 0);
  if (fd < 0) return;
  char b[8] = { 0 };
  sceIoRead(fd, b, 7);
  sceIoClose(fd);
  int s = 0;
  for (int i = 0; b[i] >= '0' && b[i] <= '9'; i++) s = s * 10 + b[i] - '0';
  if (s) espera = s * 60;
  hk_end = taiHookFunctionImport(&ref_end, "AdrenalineUser", 0xF76B66BD, 0xFE300E2F, hookEnd);
  hk_tex = taiHookFunctionImport(&ref_tex, "AdrenalineUser", 0xF76B66BD, 0x29C34DF5, hookTex);
  blog("gepresent v11.3 banco de pruebas GXM (etapa 1), compilado %s %s: espera %d s, ganchos EndScene %08x SetFragmentTexture %08x\n",
       __DATE__, __TIME__, espera / 60, hk_end, hk_tex);
  if (hk_end < 0 || hk_tex < 0) return;
  SceUID th = sceKernelCreateThread("gepresent_banco", bancoThread, 0xA0, 0x4000, 0, 0, 0);
  if (th >= 0) sceKernelStartThread(th, 0, 0);
}

void bancoStop(void) {
  if (hk_end >= 0) taiHookRelease(hk_end, ref_end);
  if (hk_tex >= 0) taiHookRelease(hk_tex, ref_tex);
}
