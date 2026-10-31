// presentador.c: toma la pantalla de Adrenaline. Engancha sus llamadas a GXM (y la copia de la imagen del PSP) para
// que, en vez de la imagen de 512x272 del PSP, Adrenaline muestre una superficie propia al doble (1024x544, 960x544
// visibles) dibujada en su hilo y su contexto: por el traductor (dibujo de la Vita) o como la imagen normal estirada.
// Como la superficie es el doble exacto de la textura, Adrenaline la ubica y escala igual (se ve un frame mas tarde).
// Tambien dibuja el menu L+R+START y le oculta los botones al juego mientras esta abierto.
// Lo llama nativedraw.c (presStart/presStop); incluye traductor.c, volcado.c y reproduccion.c y lee el registro del
// frame (registro.c). Diseno: docs/dev/etapa3_plan.md y docs/dev/funcionamiento.md.
// TODO:
// - Opciones en el menu L+R+START (hoy solo versiones), incluido el nivel de volcado; guardar en opciones.txt.
// - Linea de puntos fija sobre la fecha del menu de Adrenaline con el presentador: ver si sale sin dibujar.
// - Proponer a Adrenaline un punto de enganche oficial para un presentador externo (no enganchar su hilo de dibujo).
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <taihen.h>
#include <psp2/compat.h>
#include "sombreadores.h"
#include "version.h"
#include "../instalador/font.h"

void alog(const char *fmt, ...);  // nativedraw.c
#define CDRAM SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW
#define PHY SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW
#define UNC SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE
#define RD SCE_GXM_MEMORY_ATTRIB_READ
#define RW (SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)
#define W 960      // lo que se ve
#define H 544
#define LINE 1024  // ancho de la superficie: el doble de los 512 de psp_tex (las columnas 960-1023 quedan negras)
// tiempo del proceso en us
static inline uint32_t now(void) { return sceKernelGetProcessTimeLow(); }

// --- Memoria para la GPU ---
// Nada en CDRAM: en el proceso de Adrenaline hay solo 4 MB y la usan los videos de la Vita. Las superficies van en
// LPDDR fisica contigua, siempre con su z (sin z la GPU no dibuja o se cuelga).
// gmem1: reserva un bloque del tipo pedido (redondeado a 1 MB en PHY y a 256 KB en el resto) y lo registra para la GPU
// con attr (0: no se registra, para la memoria USSE). Devuelve la direccion o 0.
static void *gmem1(uint32_t type, uint32_t size, int attr) {
  size = (size + (type == PHY ? 0xFFFFF : 0x3FFFF)) & ~(type == PHY ? 0xFFFFFu : 0x3FFFFu);
  void *p = 0;
  SceUID b = sceKernelAllocMemBlock("nativedraw_pres", type, size, 0);
  if (b < 0 || sceKernelGetMemBlockBase(b, &p) < 0) return 0;
  if (attr && sceGxmMapMemory(p, size, attr) < 0) { sceKernelFreeMemBlock(b); return 0; }  // (0: memoria USSE)
  return p;
}
static void *gmem(uint32_t size, int attr) { return gmem1(UNC, size, attr); }  // LPDDR sin cache: programas, vertices
static uint32_t probe(uint32_t type) {  // MB que se pueden reservar de una vez (busqueda binaria; para el log)
  uint32_t lo = 0, hi = 64;
  while (lo < hi) {
    uint32_t m = (lo + hi + 1) / 2;
    SceUID b = sceKernelAllocMemBlock("nativedraw_prueba", type, m << 20, 0);
    if (b >= 0) { sceKernelFreeMemBlock(b); lo = m; } else hi = m - 1;
  }
  return lo;
}
// nombre del tipo de memoria del bloque de p (para el log)
static const char *tipo(void *p) {
  SceKernelMemBlockInfo in; in.size = sizeof in;
  if (!p || sceKernelGetMemBlockInfoByAddr(p, &in) < 0) return "-";
  return in.type == CDRAM ? "CDRAM" : in.type == PHY ? "LPDDR fisica" : in.type == UNC ? "LPDDR" : "otra";
}

// --- Programas: patcher propio (como vita2d) con texture_v/texture_f de sombreadores.h ---
// heap/halloc/hfree: memoria de CPU del patcher (32 KB que se reparten en orden y nunca se liberan).
static char heap[0x8000] __attribute__((aligned(16)));
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
static SceGxmFragmentProgram *fp, *fpb;  // fpb: con mezcla (el cuadro del menu es semitransparente)
static const SceGxmProgramParameter *p_wvp;

// Crea el patcher (con sus buffers y memoria USSE propios), registra texture_v/texture_f y arma el programa de vertices
// (x, y, z, u, v en float: 20 bytes) y dos de fragmentos: fp opaco y fpb con mezcla por alfa. Devuelve < 0 si falla.
static int programas(void) {
  SceGxmShaderPatcherParams pp;
  sceClibMemset(&pp, 0, sizeof pp);
  pp.hostAllocCallback = halloc; pp.hostFreeCallback = hfree;
  pp.bufferMem = gmem(0x10000, RD); pp.bufferMemSize = 0x10000;
  pp.vertexUsseMem = gmem(0x10000, 0); pp.vertexUsseMemSize = 0x10000;  // (la memoria USSE se registra aparte)
  pp.fragmentUsseMem = gmem(0x10000, 0); pp.fragmentUsseMemSize = 0x10000;
  if (!pp.bufferMem || !pp.vertexUsseMem || !pp.fragmentUsseMem) return -1;
  int r = sceGxmMapVertexUsseMemory(pp.vertexUsseMem, 0x10000, &pp.vertexUsseOffset);
  if (r >= 0) r = sceGxmMapFragmentUsseMemory(pp.fragmentUsseMem, 0x10000, &pp.fragmentUsseOffset);
  if (r >= 0) r = sceGxmShaderPatcherCreate(&pp, &pat);
  SceGxmShaderPatcherId iv, ifr;
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)texture_v, &iv);
  if (r >= 0) r = sceGxmShaderPatcherRegisterProgram(pat, (const SceGxmProgram *)texture_f, &ifr);
  if (r < 0) return r;
  const SceGxmProgram *pv = sceGxmShaderPatcherGetProgramFromId(iv);
  const SceGxmProgramParameter *pa = sceGxmProgramFindParameterByName(pv, "aPosition"),
                               *pt = sceGxmProgramFindParameterByName(pv, "aTexcoord");
  p_wvp = sceGxmProgramFindParameterByName(pv, "wvp");
  if (!pa || !pt || !p_wvp) return -2;
  SceGxmVertexAttribute at[2] = { { 0, 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 3, sceGxmProgramParameterGetResourceIndex(pa) },
                                  { 0, 12, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2, sceGxmProgramParameterGetResourceIndex(pt) } };
  SceGxmVertexStream st = { 20, SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
  if ((r = sceGxmShaderPatcherCreateVertexProgram(pat, iv, at, 2, &st, 1, &vp)) < 0) return r;
  r = sceGxmShaderPatcherCreateFragmentProgram(pat, ifr, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
                                               0, pv, &fp);  // (sin mezcla: opaco)
  SceGxmBlendInfo bl = { SCE_GXM_COLOR_MASK_ALL, SCE_GXM_BLEND_FUNC_ADD, SCE_GXM_BLEND_FUNC_ADD, SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
                         SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, SCE_GXM_BLEND_FACTOR_ONE, SCE_GXM_BLEND_FACTOR_ZERO };
  if (r >= 0) r = sceGxmShaderPatcherCreateFragmentProgram(pat, ifr, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                           SCE_GXM_MULTISAMPLE_NONE, &bl, pv, &fpb);
  return r;
}

// --- destino: dos superficies alternadas (una la lee Adrenaline mientras se dibuja la otra) + z ---
static SceGxmRenderTarget *rt;
static SceGxmColorSurface cs[2];
static SceGxmDepthStencilSurface ds;
static SceGxmTexture sup[2];
static void *surf[2], *zbuf, *img[4];  // img: copias de la imagen del PSP (512x272 8888), alternadas
static SceGxmTexture itex[4];
// copias en uso: con 4 la GPU casi siempre termino de leer la que se reescribe (el volcado automatico compara con las 4)
static uint32_t n_img = 4;
static float *vb;  // cuadro de pantalla completa: x, y, z, u, v (y desde vb + 20 el del menu)
#define MW 256     // cuadro del menu: textura de 256x128, se dibuja al doble centrado (512x256 de los 960x544)
#define MH 128
static uint32_t *mpix;
static SceGxmTexture mtex;
static uint16_t *ib;

// --- Fin de escenas en la GPU ---
// Cada escena propia, al terminar sus pixeles, escribe su numero (gq) en una palabra libre de la region de
// notificaciones de GXM (gq_fin). Antes de reescribir algo que lee la GPU (vertices y paletas traducidos, la copia
// de la imagen del PSP, las texturas del registro) se compara con el numero de la ultima escena que lo leyo. Para que
// eso casi nunca pase, no se le manda un frame nuevo a la GPU mientras no termine el de hace 2 frames (se repite la
// superficie); si igual pasa, se espera (gEspera) y se cuenta para el log.
static volatile uint32_t *gq_fin;
static uint32_t gq, gq_slot;
static SceGxmNotification gq_n;
// estadisticas para el log de cada 10 s (g_ev: eventos de gEv anotados)
static volatile uint32_t g_an, g_asum, g_amax, g_f1, g_f2, g_psup, g_pimg, g_ev;
// g_atras: frames no mandados por la GPU atrasada; g_esp_us: espera total de gEspera; g_salt: revisiones de la
// superficie (tapado y frames distintos) saltadas porque la GPU no la termino; g_rev_us: la revision de frames
// distintos; g_tr_us: el traductor (trFrame)
static volatile uint32_t g_atras, g_esp_us, g_salt, g_rev_us, g_tr_us;
static uint32_t img_q[4];
// 1 si la GPU todavia no termino la escena q
static inline int gOcupada(uint32_t q) { return gq_fin && (int)(*gq_fin - q) < 0; }
// notificacion para sceGxmEndScene: al terminar, la GPU escribe el numero de la escena (++gq) en gq_fin
static const SceGxmNotification *gFin(void) { if (!gq_fin) return 0; gq_n.address = gq_fin; gq_n.value = ++gq; return &gq_n; }
// anota en el log (los primeros 60) que se reescribio "que" con la GPU sin terminar y cuantas escenas le faltaban
static void gEv(const char *que, uint32_t q) {
  if (g_ev++ < 60) alog("gpu: %s con la GPU sin terminar (le faltaban %u escenas)\n", que, q - *gq_fin);
}
// espera (como mucho 30 ms) a que la GPU termine la escena q y suma lo esperado a g_esp_us
static void gEspera(uint32_t q) {
  uint32_t t0 = now();
  while (gOcupada(q) && now() - t0 < 30000) sceKernelDelayThread(100);
  g_esp_us += now() - t0;
}

// Crea el destino: render target de 1024x544, las dos superficies con su textura, la z, las copias de la imagen del PSP
// (img/itex: las 2 primeras en LPDDR fisica), la textura del menu y los cuadros de vertices (pantalla completa y menu).
// Devuelve < 0 si falla.
static int destino(void) {
  SceGxmRenderTargetParams rp;
  sceClibMemset(&rp, 0, sizeof rp);
  // varias escenas por frame: imagenes intermedias del traductor
  rp.width = LINE; rp.height = H; rp.scenesPerFrame = 8; rp.driverMemBlock = -1;
  int r = sceGxmCreateRenderTarget(&rp, &rt);
  if (r < 0) return r;
  for (int i = 0; i < 2; i++) {
    if (!(surf[i] = gmem1(PHY, LINE * H * 4, RW))) return -3;
    sceClibMemset(surf[i], 0, LINE * H * 4);
    sceGxmColorSurfaceInit(&cs[i], SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                           SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, LINE, H, LINE, surf[i]);
    sceGxmTextureInitLinear(&sup[i], surf[i], SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, LINE, H, 0);
  }
  if (!(zbuf = gmem1(PHY, LINE * H * 4, RW))) return -4;  // z obligatoria (ver arriba)
  sceGxmDepthStencilSurfaceInit(&ds, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, LINE, zbuf, 0);
  sceGxmDepthStencilSurfaceSetBackgroundDepth(&ds, 1.0f);
  for (int i = 0; i < (int)n_img; i++) {
    if (!(img[i] = gmem1(i < 2 ? PHY : UNC, 512 * 272 * 4, RD))) return -6;
    sceGxmTextureInitLinear(&itex[i], img[i], SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, 512, 272, 0);
  }
  if (!(mpix = gmem(MW * MH * 4, RD))) return -7;
  sceGxmTextureInitLinear(&mtex, mpix, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, MW, MH, 0);  // (filtro punto: letras nitidas)
  sceGxmTextureSetMinFilter(&mtex, SCE_GXM_TEXTURE_FILTER_POINT); sceGxmTextureSetMagFilter(&mtex, SCE_GXM_TEXTURE_FILTER_POINT);
  if (!(vb = gmem(8 * 20, RD)) || !(ib = gmem(8, RD))) return -5;
  // pantalla completa: las 480 columnas visibles de la imagen del PSP (de 512) a 960x544; despues, el menu centrado
  const float u1 = 480.0f / 512.0f, z = 0.5f, q[20] = { 0, 0, z, 0, 0, W, 0, z, u1, 0, 0, H, z, 0, 1, W, H, z, u1, 1 };
  sceClibMemcpy(vb, q, sizeof q);
  const float x0 = (W - 2 * MW) / 2, y0 = (H - 2 * MH) / 2, x1 = x0 + 2 * MW, y1 = y0 + 2 * MH,
              m[20] = { x0, y0, z, 0, 0, x1, y0, z, 1, 0, x0, y1, z, 0, 1, x1, y1, z, 1, 1 };
  sceClibMemcpy(vb + 20, m, sizeof m);
  for (int i = 0; i < 4; i++) ib[i] = i;
  return 0;
}

#include "cronometro.c"  // cuanto tarda la GPU en cada escena (medir_gpu)
#include "traductor.c"  // dibuja el registro del frame con la GPU de la Vita

// --- Ganchos (importaciones de AdrenalineUser) y estadisticas ---
static tai_hook_ref_t ref_end, ref_tex, ref_dma;
static SceUID hk_end = -1, hk_tex = -1, hk_dma = -1;
static const SceGxmTexture *psp_tex;    // la textura de 512x272 de Adrenaline (la imagen del juego de PSP)
static volatile uint32_t fr, listo, n_us;  // fr: escenas de Adrenaline terminadas; n_us: de ellas, con la imagen del PSP
static uint32_t usada, dibujada, cur;   // escena en que Adrenaline uso psp_tex / en que se dibujo sup[cur]
static volatile uint32_t n_pres, n_camb, n_neg_j, n_neg_p, us_sum, us_max;  // (se leen en el hilo del registro)
static volatile uint32_t n_rev, n_mal;  // frames de la Vita revisados y distintos de la imagen normal
// falla: 1 se dejo de reemplazar la imagen, 2 ya anotado (el seguro de pantalla negra que lo activaba esta quitado por
// ahora: TODO.md)
static volatile uint32_t falla;
// dib: interruptor dibujar (reemplazar la imagen siempre; si no, solo con el menu abierto); t_on: traductor listo
static int dib, t_on, c_pedido;  // c_pedido: cronometro de la GPU (medir_gpu)
static volatile uint32_t menu, pintado;  // menu abierto; su textura ya tiene el texto
#define ACTIVO (listo && !falla && (dib || menu))  // se reemplaza la imagen del PSP
static void *psp_data;                // datos de psp_tex (destino de la copia de Adrenaline)
static uint32_t copia = ~0u, ki;      // escena en que se copio la imagen a img[ki]
static volatile uint32_t n_dma;

void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
// Gancho a sceDmacMemcpy: la copia de la imagen del PSP que hace Adrenaline al empezar cada vuelta (0x74000000 ->
// psp_tex) va a img[ki], alternada: la GPU lee la imagen para el presentador despues que para Adrenaline y la copia del
// frame siguiente la pisaria. copia = escena en que se copio. Devuelve dst como la original.
static void *hookDma(void *dst, const void *src, SceSize size) {
  if (ACTIVO && dst && dst == psp_data && size == 512 * 272 * 4) {  // la copia de la imagen del PSP: a img[ki]
    ki = (ki + 1) % n_img; copia = fr; n_dma++;
    if (gOcupada(img_q[ki])) { g_pimg++; gEv("imagen normal reescrita", img_q[ki]); gEspera(img_q[ki]); }
    TAI_CONTINUE(void *, ref_dma, img[ki], src, size);
    return dst;
  }
  return TAI_CONTINUE(void *, ref_dma, dst, src, size);
}

static int negro(const uint32_t *p, uint32_t line, uint32_t w, uint32_t h) {  // 1 si 16 muestras (4x4) son casi negras
  for (uint32_t y = 1; y < 5; y++)
    for (uint32_t x = 1; x < 5; x++)
      if ((p[(h * y / 5) * line + w * x / 5] & 0xF0F0F0) != 0) return 0;
  return 1;
}

// Tapado: algo dibujado encima del juego que no esta en el registro (el dialogo del boton PS, avisos del sistema). Cada
// frame se comparan 64 puntos de la imagen normal con la superficie dibujada por la Vita; si mas de la mitad no
// coinciden 6 frames seguidos, se muestra la imagen normal y la Vita sigue dibujando por detras para comparar; con 10
// frames seguidos casi iguales (10 puntos distintos o menos), vuelve el dibujo de la Vita.
// cur_hd: la superficie cur tiene un dibujo de la Vita (no una copia)
static uint32_t cur_hd, sup_dif, sup_ok, tapado;
static int distintos(const uint32_t *pj, const uint32_t *ps) {  // puntos distintos (de 64; tolerancia 40 por canal)
  int k = 0;
  for (int j = 0; j < 8; j++)
    for (int i = 0; i < 8; i++) {
      uint32_t x = 15 + 60 * i, y = 8 + 33 * j, a = pj[y * 512 + x], b = ps[2 * y * LINE + 2 * x];
      for (int c = 0; c < 24; c += 8) {
        int d = (int)(a >> c & 255) - (int)(b >> c & 255);
        if (d > 40 || d < -40) { k++; break; }
      }
    }
  return k;
}

// Gancho a sceGxmSetFragmentTexture: reconoce psp_tex por su tamano (512x272) y, cuando Adrenaline la usa, le pasa la
// superficie dibujada en el frame anterior; sin superficie, la copia propia de este frame (psp_tex ya no se actualiza).
// usada = escena en que se uso psp_tex (+1).
static int hookTex(SceGxmContext *c, unsigned int i, const SceGxmTexture *t) {
  if (t && !psp_tex && sceGxmTextureGetWidth(t) == 512 && sceGxmTextureGetHeight(t) == 272) { psp_tex = t; psp_data = sceGxmTextureGetData(t); }
  if (t && t == psp_tex) {
    usada = fr + 1;
    if (listo && !falla && dibujada == fr && !(tapado && copia == fr)) { t = &sup[cur]; n_camb++; }
    else if (copia == fr) t = &itex[ki];  // (sin superficie: la copia de este frame, psp_tex ya no se actualiza)
  }
  return TAI_CONTINUE(int, ref_tex, c, i, t);
}

#include "volcado.c"  // volcados (L+R+SELECT y automatico)
#include "reproduccion.c"  // dibujar otra vez un frame volcado

static uint32_t g_adr[16];  // bits: palabras de la region de notificaciones que usa Adrenaline (para no usar la misma)
// marca como usada por Adrenaline la palabra de la notificacion n
static void gAdr(const SceGxmNotification *n) {
  volatile uint32_t *nr = sceGxmGetNotificationRegion();
  uint32_t i = n && nr ? (uint32_t)(n->address - nr) : 512;
  if (i < 512) g_adr[i >> 5] |= 1u << (i & 31);
}
// Gancho a sceGxmEndScene: despues de cada escena de Adrenaline en la que uso la imagen del PSP, en su hilo y su
// contexto, prepara la superficie que no se muestra (sup[s]) para el frame siguiente: la reproduccion de un volcado, el
// ultimo frame completo del registro dibujado por el traductor, la repeticion de la superficie anterior o, si no, la
// imagen del PSP estirada al doble con el menu encima. Ademas: seguro de pantalla negra, tapado, revision de frames
// distintos, volcado y estadisticas. Si Adrenaline no uso la imagen del PSP (su menu, PS1, filtro Original), nada.
// fr cuenta las escenas de Adrenaline.
static int hookEnd(SceGxmContext *c, const SceGxmNotification *v, const SceGxmNotification *f) {
  gAdr(v); gAdr(f);
  f = cAdr(f);  // (cronometro: notificacion de fin propia si no trae)
  cAnota(C_ADR, f, usada == fr + 1, 0, 0);
  int r = TAI_CONTINUE(int, ref_end, c, v, f);
  n_us += usada == fr + 1;
  static uint32_t fq[2];  // ultima escena nuestra del frame anterior y del de antes
  if (ACTIVO && usada == fr + 1) {  // en esta escena Adrenaline dibujo la imagen del PSP: la pasamos por la superficie
    uint32_t t0 = now();
    if (gq_fin) {
      uint32_t a = gq - *gq_fin; g_an++; g_asum += a; if (a > g_amax) g_amax = a;
      g_f1 += gOcupada(fq[0]); g_f2 += gOcupada(fq[1]);
    }
    const SceGxmTexture *src = psp_tex;  // la imagen de este frame: la copia propia si la hubo, si no psp_tex
    if (copia == fr) {
      src = &itex[ki];
      sceGxmTextureSetMinFilter(&itex[ki], sceGxmTextureGetMinFilter(psp_tex));  // (los filtros que eligio Adrenaline)
      sceGxmTextureSetMagFilter(&itex[ki], sceGxmTextureGetMagFilter(psp_tex));
    }
    const uint32_t *pj = sceGxmTextureGetData(src);
    // GPU atrasada: sin terminar la escena de hace 2 frames, no se le manda otro; se repite la superficie anterior
    if (gOcupada(fq[1]) && dibujada == fr && vol_on != 1) { g_atras++; dibujada = fr + 1; goto fin; }
    if (vol_on == 1 || vol_on == 3) volFrame(c, pj, dibujada == fr && !(tapado && copia == fr));  // volcado
    int nj = negro(pj, 512, 480, 272), np = dibujada == fr && negro(surf[cur], LINE, W, H);  // np: frame anterior
    n_neg_j += nj; n_neg_p += np;
    // La superficie que se muestra (surf[cur]) solo se lee con la GPU terminada: a medio dibujar no sirve para comparar
    int sup_lista = !gOcupada(fq[0]);
    if (t_on && dib && dibujada == fr && cur_hd && !sup_lista) g_salt++;
    if (t_on && dib && dibujada == fr && cur_hd && sup_lista) {  // tapado: la imagen normal no se parece al dibujo de la Vita
      int k = distintos(pj, surf[cur]);
      if (k >= 32) { sup_ok = 0; if (++sup_dif >= 6 && !tapado) { tapado = 1; } }
      else { sup_dif = 0; if (k <= 10 && tapado && ++sup_ok >= 10) tapado = 0; }
    }
    // Revision (1 de cada 4 frames): bloques del dibujo de la Vita distintos de la imagen normal (volcado.c); 6 u 8 o
    // mas = frame distinto. 255 o mas (la mitad de los 510 bloques) es algo encima de la imagen, no un error del
    // dibujo: no se cuenta.
    if (t_on && dib && dibujada == fr && cur_hd && sup_lista && !tapado && !(fr & 3)) {
      uint32_t t1 = now();
      int n = vol_auto ? volMalos(surf[cur], 0) : volDistintos(pj, surf[cur]);
      n_rev++; if (n >= (vol_auto ? 6 : 8) && n < 255) n_mal++;
      g_rev_us += now() - t1;
    }
    int s = cur ^ 1, hd = 0;
    if (dib && t_on && !menu && rep_on == 2 && repDibujar(c, s, src)) { hd = 1; t_frame = ~0u; }  // reproduccion
    else if (dib && t_on && !menu) {  // el ultimo frame completo del registro, dibujado por la Vita
      RFrame *rg = regTomar();
      int mot = T_NUEVO;
      if (!rg) t_cnt[T_NUEVO]++;
      else if (rg->frame == t_frame) {  // mismo frame: se repite la superficie hasta 2 veces (30 fps); si no, normal
        if (t_ok && t_rep < 2) { t_rep++; hd = 2; mot = 0; t_salto[0]++; } else t_cnt[T_NUEVO]++;
      } else {
        if (t_frame != ~0u && rg->frame > t_frame) { uint32_t g = rg->frame - t_frame; t_salto[g < 5 ? g : 5]++; }
        uint32_t t1 = now();
        int r = trFrame(c, rg, s, src); g_tr_us += now() - t1; t_cnt[r]++; t_frame = rg->frame; t_ok = !r; hd = !r; t_rep = 0; mot = r;
        regGpu(gq, gq_fin);  // la escena que lee las texturas de este registro
      }
      if (vol_on == 1 || vol_on == 3) volTexto(rg, hd == 2 ? "repite el dibujo de la Vita del frame anterior" : hd ? "dibujo de la Vita" :
                                "imagen normal (copia de la imagen del PSP)", mot);
      regSoltar();
    } else if (vol_on == 1 || vol_on == 3) volTexto(0, menu ? "imagen normal con el menu" : "imagen normal (sin dibujar o sin traductor)", -1);
    // hd: 2 se deja la superficie anterior, 1 la dibujo el traductor, 0 la imagen del PSP al doble (abajo)
    if (hd == 2) { dibujada = fr + 1; t_hd++; goto fin; }
    if (hd) { t_hd++; cur_hd = 1; goto listo_hd; }
    t_nat++; cur_hd = 0;
    sceGxmBeginScene(c, 0, rt, 0, 0, 0, &cs[s], &ds);
    sceGxmSetVertexProgram(c, vp);
    sceGxmSetFragmentProgram(c, fp);
    void *u;
    sceGxmReserveVertexDefaultUniformBuffer(c, &u);
    // Ortografica de pixeles a pantalla, guardada como la de vita2d (matrix_init_orthographic: por columnas, el
    // desplazamiento en [12] y [13]; el sombreador hace mul(pos, wvp)).
    static const float wvp[16] = { 2.0f / LINE, 0, 0, 0, 0, -2.0f / H, 0, 0, 0, 0, -2, 0, -1, 1, 1, 1 };
    sceGxmSetUniformDataF(u, p_wvp, 0, 16, wvp);
    sceGxmSetFragmentTexture(c, 0, src);
    sceGxmSetVertexStream(c, 0, vb);
    sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
    if (menu && pintado) {  // el cuadro del menu encima
      sceGxmSetFragmentProgram(c, fpb);
      sceGxmSetFragmentTexture(c, 0, &mtex);
      sceGxmSetVertexStream(c, 0, vb + 20);
      sceGxmDraw(c, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, ib, 4);
    }
    sceGxmEndScene(c, 0, gFin());
    cAnota(C_NORMAL, &gq_n, menu && pintado ? 2 : 1, W * H / 1000, 0);
  listo_hd:
    if (src == &itex[ki]) img_q[ki] = gq;  // escena que lee esa copia
    fq[1] = fq[0]; fq[0] = gq;
    cur = s; dibujada = fr + 1; n_pres++;
    uint32_t d = now() - t0;
    us_sum += d;
    if (d > us_max) us_max = d;
  }
fin:
  fr++;
  return r;
}

// --- Quien dibuja la pantalla (para el error del boton PS) ---
// Ganchos que solo cuentan: escenas y frames mandados a la pantalla por Adrenaline (AdrenalineUser) y por el emulador
// del sistema (ScePspemu). Cada segundo, presThread arma el estado (quien dibujo, si hay un dialogo del sistema abierto,
// superficie de la Vita o imagen normal, tapado) y lo anota cuando cambia y los 3 segundos siguientes.
static tai_hook_ref_t ref_aq, ref_afb, ref_pend, ref_pq, ref_pfb;
static SceUID hk_aq = -1, hk_afb = -1, hk_pend = -1, hk_pq = -1, hk_pfb = -1;
static volatile uint32_t n_aq, n_afb, n_pend, n_pq, n_pfb;
static int hookAq(void *a, void *b, const void *d) { n_aq++; return TAI_CONTINUE(int, ref_aq, a, b, d); }
static int hookAfb(const void *p, int s) { n_afb++; return TAI_CONTINUE(int, ref_afb, p, s); }
static int hookPend(SceGxmContext *c, const SceGxmNotification *v, const SceGxmNotification *f) { n_pend++; return TAI_CONTINUE(int, ref_pend, c, v, f); }
static int hookPq(void *a, void *b, const void *d) { n_pq++; return TAI_CONTINUE(int, ref_pq, a, b, d); }
static int hookPfb(const void *p, int s) { n_pfb++; return TAI_CONTINUE(int, ref_pfb, p, s); }
static int (*dlgCorre)(void);  // sceCommonDialogIsRunning (con esto, Adrenaline deja de dibujar)
// cada 1 s (lo llama presThread): anota el estado si cambio y los 3 s siguientes (como mucho 300 lineas)
static void pantalla(void) {
  static uint32_t a0, u0, aq0, afb0, p0, pq0, pfb0, sig0 = ~0u, extra, lineas;
  uint32_t a = fr - a0, u = n_us - u0, aq = n_aq - aq0, afb = n_afb - afb0, pe = n_pend - p0, pq = n_pq - pq0, pfb = n_pfb - pfb0;
  a0 = fr; u0 = n_us; aq0 = n_aq; afb0 = n_afb; p0 = n_pend; pq0 = n_pq; pfb0 = n_pfb;
  int dl = dlgCorre ? dlgCorre() : -1;
  uint32_t sig = (a > 0) | (u > 0) << 1 | (aq > 0) << 2 | (pe > 0) << 3 | (pq > 0 || pfb > 0) << 4 | (dl > 0) << 5 | tapado << 6 |
                 cur_hd << 7 | menu << 8 | falla << 9;
  if (sig != sig0) extra = 4;
  sig0 = sig;
  if (!extra || lineas >= 300) return;
  extra--; lineas++;
  alog("pantalla: Adrenaline escenas %u (con la imagen del PSP %u), a la pantalla %u (cambios de buffer %u) | ScePspemu escenas %u, "
       "a la pantalla %u (cambios de buffer %u) | dialogo del sistema %s | superficie: %s%s%s%s\n", a, u, aq, afb, pe, pq, pfb,
       dl < 0 ? "?" : dl ? "abierto" : "no", cur_hd ? "dibujo de la Vita" : "imagen normal", tapado ? ", tapado" : "",
       menu ? ", menu L+R+START" : "", falla ? ", falla" : "");
}

// Notificaciones de la GPU (para el error del boton PS): cada 50 ms se comparan las 512 palabras de la region con la
// vez anterior. La primera vez que cambia una palabra que no es nuestra se anota (quien mas la usa: Adrenaline, el
// emulador, el dialogo del sistema). En las nuestras (gq_slot, c_slot) se anota si alguien escribio otro valor: solo
// pueden subir y nunca pasar de lo que mandamos.
static void notifs(volatile uint32_t *nr) {
  static uint32_t ant[512], vista[16], hay, n_log, mal_g, mal_c, ug, uc;
  if (!nr) return;
  for (uint32_t i = 0; i < 512; i++) {
    uint32_t v = nr[i];
    if (hay && v != ant[i] && i != gq_slot && !(c_on && i == c_slot) && !(vista[i >> 5] >> (i & 31) & 1) && n_log < 60) {
      vista[i >> 5] |= 1u << (i & 31); n_log++;
      alog("notificaciones: la palabra %u la usa otro (%08x -> %08x)\n", i, ant[i], v);
    }
    ant[i] = v;
  }
  hay = 1;
  if (gq_fin) {  // la nuestra: no baja ni pasa de gq
    uint32_t v = *gq_fin;
    if (((int)(v - ug) < 0 || (int)(v - gq) > 0) && mal_g++ < 20) alog("notificaciones: la palabra %u (nuestra) tiene %08x: la ultima vista %08x, la ultima mandada %08x\n", gq_slot, v, ug, gq);
    ug = v;
  }
  if (c_on) {
    uint32_t v = *c_adr.address;
    if (((int)(v - uc) < 0 || (int)(v - c_av) > 0) && mal_c++ < 20) alog("notificaciones: la palabra %u (del cronometro) tiene %08x: la ultima vista %08x, la ultima mandada %08x\n", c_slot, v, uc, c_av);
    uc = v;
  }
}

// --- Menu L+R+START ---
static tai_hook_ref_t ref_ctrl;
static SceUID hk_ctrl = -1;
static uint32_t combo0;
// Gancho a sceCompatWriteSharedCtrl (ScePspemu): los botones que la Vita le pasa al PSP. L+R+SELECT pide un volcado;
// L+R+START abre o cierra el menu (solo si se esta viendo la imagen del PSP). Con el menu abierto o con una combinacion
// apretada, el juego recibe "nada apretado".
static int hookCtrl(SceCtrlDataPsp *p) {
  if (p && listo && !falla) {
    static uint32_t cv0;  // L+R+SELECT (0x301): volcado
    uint32_t cv = (p->Buttons & 0x301) == 0x301;
    if (cv && !cv0 && (!vol_on || vol_on == 3) && !vol_req) vol_req = 1;
    cv0 = cv;
    if (cv) { p->Buttons = 0; p->Lx = p->Ly = p->Rx = p->Ry = 128; }
    uint32_t combo = (p->Buttons & 0x308) == 0x308;  // L (0x100) + R (0x200) + START (0x8)
    if (combo && !combo0 && (menu || usada + 10 > fr)) { menu ^= 1; pintado = 0; }  // (solo si se ve la imagen del PSP)
    combo0 = combo;
    if (menu || combo) { p->Buttons = 0; p->Lx = p->Ly = p->Rx = p->Ry = 128; }  // el juego no ve nada apretado
  }
  return TAI_CONTINUE(int, ref_ctrl, p);
}
const char *nlVersion(void);  // nativedraw.c: version de NativeLink (de su log), 0 si no esta cargado

// escribe s en la textura del menu en (x, y) con la fuente 8x16 del instalador (corta en el borde derecho)
static void texto(int x, int y, const char *s, uint32_t col) {
  for (; *s && x + 8 <= MW; s++, x += 8)
    if (*s > 32 && *s < 127)
      for (int r = 0; r < 16; r++)
        for (int b = 0; b < 8; b++)
          if (font8x16[*s - 32][r] & (0x80 >> b)) mpix[(y + r) * MW + x + b] = col;
}
// dibuja el menu en su textura: fondo semitransparente con borde blanco, titulo y versiones (colores 0xAABBGGRR)
static void pintar(void) {
  for (int y = 0; y < MH; y++)
    for (int x = 0; x < MW; x++)
      mpix[y * MW + x] = x < 2 || y < 2 || x >= MW - 2 || y >= MH - 2 ? 0xFFFFFFFFu : 0xD8201008u;
  const char *nl = nlVersion();
  char b[40];
  texto(16, 10, "NativePatch", 0xFF40C8FFu);
  sceClibSnprintf(b, sizeof b, "NativeLink  %s", nl ? nl : "no cargado");
  texto(16, 38, b, 0xFFFFFFFFu);
  texto(16, 58, "NativeDraw  " NATIVEDRAW_VERSION, 0xFFFFFFFFu);
  texto(16, 102, "L+R+START: cerrar", 0xFFA0A0A0u);
}

// Hilo del presentador: engancha las funciones de Adrenaline (reintenta hasta 60 s), espera 30 escenas, crea programas,
// destino y traductor, elige la palabra para el fin de escenas y marca listo. Despues, cada 50 ms: pinta el menu,
// atiende volcados y reproduccion y anota cambios en el log; cada 10 s, las estadisticas.
static int presThread(SceSize args, void *argp) {
  for (int i = 0; i < 600 && (hk_end < 0 || hk_tex < 0); i++) {  // AdrenalineUser se carga despues que NativeDraw
    if (hk_end < 0) hk_end = taiHookFunctionImport(&ref_end, "AdrenalineUser", 0xF76B66BD, 0xFE300E2F, hookEnd);
    if (hk_tex < 0) hk_tex = taiHookFunctionImport(&ref_tex, "AdrenalineUser", 0xF76B66BD, 0x29C34DF5, hookTex);
    if (hk_dma < 0) hk_dma = taiHookFunctionImport(&ref_dma, "AdrenalineUser", 0xA9E5B2F0, 0x9B2FF739, hookDma);
    if (hk_ctrl < 0) hk_ctrl = taiHookFunctionImport(&ref_ctrl, "ScePspemu", 0x0F35909D, 0x2306FFED, hookCtrl);
    if (hk_end < 0 || hk_tex < 0) sceKernelDelayThread(100000);
  }
  alog("presentador: ganchos sceGxmEndScene %08x sceGxmSetFragmentTexture %08x sceDmacMemcpy %08x sceCompatWriteSharedCtrl %08x\n",
       hk_end, hk_tex, hk_dma, hk_ctrl);
  // solo cuentan (quien dibuja la pantalla): sceGxmDisplayQueueAddEntry y sceDisplaySetFrameBuf de los dos y
  // sceGxmEndScene de ScePspemu
  hk_aq = taiHookFunctionImport(&ref_aq, "AdrenalineUser", 0xF76B66BD, 0xEC5C26B5, hookAq);
  hk_afb = taiHookFunctionImport(&ref_afb, "AdrenalineUser", 0x4FAACD11, 0x7A410B64, hookAfb);
  hk_pend = taiHookFunctionImport(&ref_pend, "ScePspemu", 0xF76B66BD, 0xFE300E2F, hookPend);
  hk_pq = taiHookFunctionImport(&ref_pq, "ScePspemu", 0xF76B66BD, 0xEC5C26B5, hookPq);
  hk_pfb = taiHookFunctionImport(&ref_pfb, "ScePspemu", 0x4FAACD11, 0x7A410B64, hookPfb);
  int rd = taiGetModuleExportFunc("SceCommonDialog", 0xE537816C, 0x043A353E, (uintptr_t *)&dlgCorre);
  alog("pantalla: ganchos Adrenaline cola %08x buffer %08x | ScePspemu escenas %08x cola %08x buffer %08x | "
       "sceCommonDialogIsRunning %08x\n", hk_aq, hk_afb, hk_pend, hk_pq, hk_pfb, rd);
  if (rd < 0) dlgCorre = 0;
  if (hk_end < 0 || hk_tex < 0) return sceKernelExitDeleteThread(0);
  while (fr < 30) sceKernelDelayThread(100000);  // GXM ya iniciado por Adrenaline
  int r1 = programas(), r2 = r1 >= 0 ? destino() : 0;
  t_on = r1 >= 0 && r2 >= 0 && dib && trInit() >= 0;  // traductor: solo con dibujar
  alog("presentador: programas %08x, destino %08x (superficies en %s y %s) | libre despues (MB de una vez): CDRAM %u, "
       "LPDDR fisica %u, LPDDR %u\n", r1, r2, tipo(surf[0]), tipo(surf[1]), probe(CDRAM), probe(PHY), probe(UNC));
  if (r1 < 0 || r2 < 0) return sceKernelExitDeleteThread(0);
  volatile uint32_t *nr = sceGxmGetNotificationRegion();  // fin de escenas: la ultima palabra que Adrenaline no usa
  char ua[80]; int ka = 0;
  for (uint32_t i = 0; i < 512; i++) if (g_adr[i >> 5] >> (i & 31) & 1 && ka < 70) ka += sceClibSnprintf(ua + ka, sizeof ua - ka, " %u", i);
  ua[ka] = 0;
  for (gq_slot = 511; gq_slot > 400 && (g_adr[gq_slot >> 5] >> (gq_slot & 31) & 1); gq_slot--) {}
  if (nr) { gq_fin = nr + gq_slot; *gq_fin = 0; gq = 0; }
  alog("gpu: fin de escenas en la notificacion %u (%p); Adrenaline usa:%s\n", gq_slot, gq_fin, ka ? ua : " ninguna");
  if (c_pedido) cInicio(nr, g_adr);
  listo = 1;
  for (uint32_t t = now(), seen = 0, k = 0, aviso = 0, m0 = 0;;) {  // cada 10 s: cuantos frames y cuanto costo
    sceKernelDelayThread(50000);
    if (menu && !pintado) { pintar(); __sync_synchronize(); pintado = 1; }
    if (!(k % 20)) pantalla();
    notifs(nr);
    volHilo();
    repHilo();
    if (menu != m0) { m0 = menu; alog("presentador: menu %s\n", m0 ? "abierto" : "cerrado"); }
    static uint32_t tp0;
    if (tapado != tp0) { tp0 = tapado; alog("traductor: %s\n", tp0 ? "algo tapa la imagen (no esta en el registro): imagen normal" : "vuelve el dibujo de la Vita"); }
    if (aviso < 2 && n_pres >= (aviso ? 60 : 1)) alog("presentador: %s\n", aviso++ ? "60 frames dibujados" : "primer frame dibujado");
    if (falla == 1) { falla = 2; alog("presentador: se deja la imagen normal hasta cerrar Adrenaline\n"); }
    if (++k < 200) continue;
    k = 0;
    uint32_t p = n_pres, m = us_max, s = us_sum, dt = (now() - t) / 1000, nd = n_dma, nr = n_rev, nm = n_mal;
    n_pres = 0; n_dma = 0; us_sum = 0; us_max = 0; t = now(); n_rev = n_mal = 0;
    if (psp_tex && !seen++)
      alog("presentador: textura del juego %p (512x272, formato %08x, filtros %x/%x)\n", psp_tex,
           sceGxmTextureGetFormat(psp_tex), sceGxmTextureGetMinFilter(psp_tex), sceGxmTextureGetMagFilter(psp_tex));
    if (t_on) {
      static uint32_t dr0;  // (t_draws no se borra: el traductor lo usa a mitad de un frame para el cronometro)
      uint32_t h = t_hd, nn = t_nat, dr = t_draws - dr0; t_hd = t_nat = 0; dr0 += dr;
      char b[200]; int k = 0;
      for (int i = 1; i < TN; i++) if (t_cnt[i]) { k += sceClibSnprintf(b + k, sizeof b - k, " %s=%u", tnom[i], t_cnt[i]); t_cnt[i] = 0; }
      b[k] = 0;
      alog("traductor: frames HD %u, con la imagen normal %u%s%s | draws dibujados %u\n", h, nn, k ? " | motivos:" : "", b, dr);
      // Ritmo: por cada frame de Adrenaline que dibujo la Vita, cuantos frames del PSP avanzo desde el anterior
      // (1 = ninguno saltado; 0 = repitio el anterior). Con saltos o repeticiones destella lo que un juego dibuja un
      // frame si y otro no (transparencias de los juegos de Neo Geo).
      alog("traductor: ritmo (frames del PSP que avanzo cada frame dibujado): 0 (repetido) %u, 1 %u, 2 %u, 3 %u, 4 %u, 5 o mas %u\n",
           t_salto[0], t_salto[1], t_salto[2], t_salto[3], t_salto[4], t_salto[5]);
      sceClibMemset((void *)t_salto, 0, sizeof t_salto);
    }
    if (gq_fin) {  // fin de escenas en la GPU
      uint32_t an = g_an, as = g_asum, am = g_amax, f1 = g_f1, f2 = g_f2, ps = g_psup, pi = g_pimg;
      uint32_t at = g_atras, es = g_esp_us, sl = g_salt, rv = g_rev_us, tr = g_tr_us;
      g_an = g_asum = g_amax = g_f1 = g_f2 = g_psup = g_pimg = g_atras = g_esp_us = g_salt = g_rev_us = g_tr_us = 0;
      alog("gpu: al empezar cada frame, escenas nuestras sin terminar: promedio %u.%u, max %u | sin terminar: el frame anterior %u, "
           "el de hace 2 %u | reescritos con la GPU sin terminar: vertices y paletas %u, imagen normal %u (texturas del registro: "
           "en nativedraw_captura.log)\n", an ? as / an : 0, an ? as * 10 / an % 10 : 0, am, f1, f2, ps, pi);
      alog("gpu: frames no mandados por la GPU atrasada %u, esperas a la GPU %u ms | CPU en 10 s: traductor %u ms, "
           "revision de frames distintos %u ms | revisiones de la superficie saltadas (GPU sin terminar) %u\n",
           at, es / 1000, tr / 1000, rv / 1000, sl);
      if (c_on) cInforme(dt);
    }
    alog("presentador: %u frames en %u ms por la superficie (cambios %u en total, %u con copia propia), CPU %u us/frame (max %u) | "
         "casi negros: juego %u, superficie %u | revisados %u, distintos de la imagen normal %u%s\n", p, dt, n_camb, nd, p ? s / p : 0, m, n_neg_j, n_neg_p, nr, nm,
         p ? "" : psp_tex ? " (Adrenaline no uso la imagen del PSP: menu, PS1 o filtro Original)" : " (todavia sin la textura del juego)");
  }
  return 0;
}

static int swOn(const char *p) {  // el archivo existe y no empieza con "no" (como captura.c)
  char b[2] = { 0, 0 };
  SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0);
  if (fd < 0) return 0;
  sceIoRead(fd, b, 2); sceIoClose(fd);
  return !(b[0] == 'n' && b[1] == 'o');
}

// Lo llama nativedraw.c al cargar: lee los interruptores (dibujar, reproducir, volcado), decide cuantas copias de la
// imagen del PSP hacen falta y arranca el hilo del presentador.
void presStart(void) {
  dib = swOn("ux0:data/nativedraw/dibujar");
  c_pedido = swOn("ux0:data/nativedraw/medir_gpu");
  repInicio();
  vol_nivel = volNivel();  // volcados: desactivado, pantallazo, frame, secuencia o auto
  if (vol_nivel == 4 && !dib) vol_nivel = 3;  // (auto compara el dibujo de la Vita: sin dibujar no hay que comparar)
  vol_auto = vol_nivel == 4; vol_nf = vol_nivel <= 2 ? 1 : VN;
  alog("volcado: nivel %s (ux0:data/nativedraw/volcado: desactivado, pantallazo, frame, secuencia o auto)\n", vol_nom[vol_nivel]);
  alog("presentador: %s; menu con L+R+START\n", dib ? "reemplaza la imagen siempre (dibujar)" :
       "reemplaza la imagen solo con el menu abierto (crear ux0:data/nativedraw/dibujar para siempre)");
  SceUID th = sceKernelCreateThread("nativedraw_presentador", presThread, 0xA0, 0x2000, 0, 0, 0);
  if (th >= 0) sceKernelStartThread(th, 0, 0);
}
// Lo llama nativedraw.c al descargar: suelta los ganchos.
void presStop(void) {
  if (hk_end >= 0) taiHookRelease(hk_end, ref_end);
  if (hk_tex >= 0) taiHookRelease(hk_tex, ref_tex);
  if (hk_dma >= 0) taiHookRelease(hk_dma, ref_dma);
  if (hk_ctrl >= 0) taiHookRelease(hk_ctrl, ref_ctrl);
  if (hk_aq >= 0) taiHookRelease(hk_aq, ref_aq);
  if (hk_afb >= 0) taiHookRelease(hk_afb, ref_afb);
  if (hk_pend >= 0) taiHookRelease(hk_pend, ref_pend);
  if (hk_pq >= 0) taiHookRelease(hk_pq, ref_pq);
  if (hk_pfb >= 0) taiHookRelease(hk_pfb, ref_pfb);
}
