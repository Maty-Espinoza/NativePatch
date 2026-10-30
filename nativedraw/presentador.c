// presentador.c (NativeDraw 0.0.3, paso 3.0 de docs/dev/etapa3_plan.md): NativeDraw toma la pantalla de Adrenaline.
//
// Como dibuja Adrenaline 8 (user/menu.c de isage/Adrenaline, AdrenalineDraw): en cada vuelta copia la imagen del PSP
// (0x74000000, 512x272 8888) a su textura psp_tex y la dibuja con vita2d con la escala de su menu (psp_screen_scale,
// 2.0 = pantalla completa). Los juegos de PS1 usan otra textura (pops_tex, 1024x544, la imagen ya viene en 4:3) y con el
// filtro "Original" (o el escalado directo de la 8) Adrenaline no dibuja con GXM: en esos casos el presentador no hace nada.
//
// Paso 3.0 (transparente): despues de la escena de Adrenaline, en su hilo y su contexto, se dibuja psp_tex al doble en
// una superficie propia (960x544 visibles de 1024x544, el doble exacto de la textura) y en el frame siguiente se cambia
// psp_tex por esa superficie cuando Adrenaline la usa (sceGxmSetFragmentTexture). Como la superficie es el doble de la
// textura, las coordenadas de Adrenaline sirven igual: se respetan su escala, su posicion y su proporcion. Se ve un
// frame mas tarde, como se vera la imagen HD (la Vita dibuja el frame N mientras el juego arma el N+1).
// 0.0.3.6: la copia de la imagen del PSP que hace Adrenaline al empezar cada vuelta (sceDmacMemcpy 0x74000000 -> psp_tex)
// se desvia a dos texturas propias alternadas: el dibujo de NativeDraw lee la GPU despues que el de Adrenaline y la copia
// del frame siguiente pisaba la textura mientras se leia (una franja de ~20 px mezclada, 0.0.3.5).
// Base: el banco de la etapa 1 (tools/gepresent/banco.c en la etiqueta gepatch-dev66).
// La imagen se reemplaza todo el tiempo solo si existe ux0:data/nativedraw/dibujar (sin "no" adentro); si no, solo
// mientras el menu esta abierto.
//
// Menu L+R+START (0.0.4, paso 3.1): los botones que la Vita le pasa al PSP salen de sceCompatWriteSharedCtrl (importacion
// de ScePspemu, la misma que engancha Adrenaline para su menu). Con L+R+START se abre o se cierra; abierto, el juego recibe
// "nada apretado" y se dibuja encima de la imagen un cuadro con el texto (textura de 256x128 al doble, fuente 8x16 del
// instalador). Por ahora solo muestra las versiones; las opciones que hoy son archivos (registros.md) se agregan despues.
// En PS1 no se abre (Adrenaline no usa la imagen del PSP).
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
static inline uint32_t now(void) { return sceKernelGetProcessTimeLow(); }

// --- memoria para la GPU. Nada en CDRAM: en el proceso de Adrenaline hay solo 4 MB y la usa el video de la Vita (0.0.3.2
// tomo 2,25 MB: la cinematica de GT salio negra y los videos del menu con estatica). Las superficies van en LPDDR fisica
// contigua (sobran ~23 MB), con su z. Sin z la GPU no dibujo (0.0.3.3: superficie negra) o se colgo (0.0.3.4) ---
static void *gmem1(uint32_t type, uint32_t size, int attr) {
  size = (size + (type == PHY ? 0xFFFFF : 0x3FFFF)) & ~(type == PHY ? 0xFFFFFu : 0x3FFFFu);
  void *p = 0;
  SceUID b = sceKernelAllocMemBlock("nativedraw_pres", type, size, 0);
  if (b < 0 || sceKernelGetMemBlockBase(b, &p) < 0) return 0;
  if (attr && sceGxmMapMemory(p, size, attr) < 0) { sceKernelFreeMemBlock(b); return 0; }  // (0: memoria USSE)
  return p;
}
static void *gmem(uint32_t size, int attr) { return gmem1(UNC, size, attr); }  // (programas, vertices)
static uint32_t probe(uint32_t type) {  // MB libres de una vez (busqueda binaria, se libera en seguida)
  uint32_t lo = 0, hi = 64;
  while (lo < hi) {
    uint32_t m = (lo + hi + 1) / 2;
    SceUID b = sceKernelAllocMemBlock("nativedraw_prueba", type, m << 20, 0);
    if (b >= 0) { sceKernelFreeMemBlock(b); lo = m; } else hi = m - 1;
  }
  return lo;
}
static const char *tipo(void *p) {
  SceKernelMemBlockInfo in; in.size = sizeof in;
  if (!p || sceKernelGetMemBlockInfoByAddr(p, &in) < 0) return "-";
  return in.type == CDRAM ? "CDRAM" : in.type == PHY ? "LPDDR fisica" : in.type == UNC ? "LPDDR" : "otra";
}

// --- programas: patcher propio (como vita2d) con texture_v/texture_f ---
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
static void *surf[2], *zbuf, *img[2];  // img: copias de la imagen del PSP (512x272 8888), alternadas
static SceGxmTexture itex[2];
static float *vb;  // cuadro de pantalla completa: x, y, z, u, v (y desde vb + 20 el del menu)
#define MW 256     // cuadro del menu: textura de 256x128, se dibuja al doble centrado (512x256 de los 960x544)
#define MH 128
static uint32_t *mpix;
static SceGxmTexture mtex;
static uint16_t *ib;

static int destino(void) {
  SceGxmRenderTargetParams rp;
  sceClibMemset(&rp, 0, sizeof rp);
  rp.width = W; rp.height = H; rp.scenesPerFrame = 1; rp.driverMemBlock = -1;
  int r = sceGxmCreateRenderTarget(&rp, &rt);
  if (r < 0) return r;
  for (int i = 0; i < 2; i++) {
    if (!(surf[i] = gmem1(PHY, LINE * H * 4, RW))) return -3;
    sceClibMemset(surf[i], 0, LINE * H * 4);
    sceGxmColorSurfaceInit(&cs[i], SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                           SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, W, H, LINE, surf[i]);
    sceGxmTextureInitLinear(&sup[i], surf[i], SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, LINE, H, 0);
  }
  if (!(zbuf = gmem1(PHY, W * H * 4, RW))) return -4;  // (como el banco de la etapa 1 y 0.0.3.2, que funcionaron)
  sceGxmDepthStencilSurfaceInit(&ds, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, W, zbuf, 0);
  sceGxmDepthStencilSurfaceSetBackgroundDepth(&ds, 1.0f);
  for (int i = 0; i < 2; i++) {
    if (!(img[i] = gmem1(PHY, 512 * 272 * 4, RD))) return -6;
    sceGxmTextureInitLinear(&itex[i], img[i], SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, 512, 272, 0);
  }
  if (!(mpix = gmem(MW * MH * 4, RD))) return -7;
  sceGxmTextureInitLinear(&mtex, mpix, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8, MW, MH, 0);  // (filtro punto: letras nitidas)
  sceGxmTextureSetMinFilter(&mtex, SCE_GXM_TEXTURE_FILTER_POINT); sceGxmTextureSetMagFilter(&mtex, SCE_GXM_TEXTURE_FILTER_POINT);
  if (!(vb = gmem(8 * 20, RD)) || !(ib = gmem(8, RD))) return -5;
  const float u1 = 480.0f / 512.0f, z = 0.5f, q[20] = { 0, 0, z, 0, 0, W, 0, z, u1, 0, 0, H, z, 0, 1, W, H, z, u1, 1 };
  sceClibMemcpy(vb, q, sizeof q);
  const float x0 = (W - 2 * MW) / 2, y0 = (H - 2 * MH) / 2, x1 = x0 + 2 * MW, y1 = y0 + 2 * MH,
              m[20] = { x0, y0, z, 0, 0, x1, y0, z, 1, 0, x0, y1, z, 0, 1, x1, y1, z, 1, 1 };
  sceClibMemcpy(vb + 20, m, sizeof m);
  for (int i = 0; i < 4; i++) ib[i] = i;
  return 0;
}

#include "traductor.c"  // (0.0.6) dibuja el registro del frame

// --- ganchos (importaciones de SceGxm en AdrenalineUser) y estadisticas ---
static tai_hook_ref_t ref_end, ref_tex, ref_dma;
static SceUID hk_end = -1, hk_tex = -1, hk_dma = -1;
static const SceGxmTexture *psp_tex;    // la textura de 512x272 de Adrenaline (la imagen del juego de PSP)
static volatile uint32_t fr, listo;     // fr: escenas de Adrenaline terminadas
static uint32_t usada, dibujada, cur;   // escena en que Adrenaline uso psp_tex / en que se dibujo sup[cur]
static volatile uint32_t n_pres, n_camb, n_neg_j, n_neg_p, us_sum, us_max;  // (se leen en el hilo del registro)
static volatile uint32_t solo_mal, falla;
static int dib, t_on;                    // interruptor dibujar (t_on: traductor listo): reemplazar la imagen siempre (si no, solo con el menu)
static volatile uint32_t menu, pintado;  // menu abierto; su textura ya tiene el texto
#define ACTIVO (listo && !falla && (dib || menu))
static void *psp_data;                // datos de psp_tex (destino de la copia de Adrenaline)
static uint32_t copia = ~0u, ki;      // escena en que se copio la imagen a img[ki]
static volatile uint32_t n_dma;

void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
static void *hookDma(void *dst, const void *src, SceSize size) {
  if (ACTIVO && dst && dst == psp_data && size == 512 * 272 * 4) {  // la copia de la imagen del PSP: a img[ki]
    ki ^= 1; copia = fr; n_dma++;
    TAI_CONTINUE(void *, ref_dma, img[ki], src, size);
    return dst;
  }
  return TAI_CONTINUE(void *, ref_dma, dst, src, size);
}  // proteccion: frames seguidos con la superficie negra y el juego no; 1 = se dejo

static int negro(const uint32_t *p, uint32_t line, uint32_t w, uint32_t h) {  // 16 muestras casi negras
  for (uint32_t y = 1; y < 5; y++)
    for (uint32_t x = 1; x < 5; x++)
      if ((p[(h * y / 5) * line + w * x / 5] & 0xF0F0F0) != 0) return 0;
  return 1;
}

// (0.0.6.3) Algo dibujado encima del juego que no esta en el registro (el dialogo del boton PS, avisos del sistema): cada
// frame se comparan 64 puntos de la imagen normal con la superficie dibujada por la Vita; si mas de la mitad no coinciden
// 6 frames seguidos, se muestra la imagen normal ("tapado") y la Vita sigue dibujando por detras para comparar; con 10
// frames seguidos casi iguales, se vuelve al dibujo de la Vita.
static uint32_t cur_hd, sup_dif, sup_ok, tapado, n_tap;  // cur_hd: la superficie cur tiene un dibujo de la Vita (no una copia)
static int distintos(const uint32_t *pj, const uint32_t *ps) {  // puntos que no coinciden (de 64; tolerancia 40 por canal)
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

static int hookTex(SceGxmContext *c, unsigned int i, const SceGxmTexture *t) {
  if (t && !psp_tex && sceGxmTextureGetWidth(t) == 512 && sceGxmTextureGetHeight(t) == 272) { psp_tex = t; psp_data = sceGxmTextureGetData(t); }
  if (t && t == psp_tex) {
    usada = fr + 1;
    if (listo && !falla && dibujada == fr && !(tapado && copia == fr)) { t = &sup[cur]; n_camb++; }  // la superficie del frame anterior
    else if (copia == fr) t = &itex[ki];  // (sin superficie: la copia de este frame, psp_tex ya no se actualiza)
  }
  return TAI_CONTINUE(int, ref_tex, c, i, t);
}

static int hookEnd(SceGxmContext *c, const SceGxmNotification *v, const SceGxmNotification *f) {
  int r = TAI_CONTINUE(int, ref_end, c, v, f);
  if (ACTIVO && usada == fr + 1) {  // en esta escena Adrenaline dibujo la imagen del PSP: la pasamos por la superficie
    uint32_t t0 = now();
    const SceGxmTexture *src = psp_tex;  // la imagen de este frame: la copia propia si la hubo, si no psp_tex
    if (copia == fr) {
      src = &itex[ki];
      sceGxmTextureSetMinFilter(&itex[ki], sceGxmTextureGetMinFilter(psp_tex));  // (los filtros que eligio Adrenaline)
      sceGxmTextureSetMagFilter(&itex[ki], sceGxmTextureGetMagFilter(psp_tex));
    }
    const uint32_t *pj = sceGxmTextureGetData(src);
    int nj = negro(pj, 512, 480, 272), np = dibujada == fr && negro(surf[cur], LINE, W, H);  // (np: la del frame anterior)
    n_neg_j += nj; n_neg_p += np;
    solo_mal = np && !nj ? solo_mal + 1 : 0;
    if (solo_mal >= 30) falla = 1;  // medio segundo dibujando negro sobre una imagen que no lo es: se vuelve a la normal
    if (t_on && dib && dibujada == fr && cur_hd) {  // (0.0.6.3) tapado: la imagen normal no se parece al dibujo de la Vita
      int k = distintos(pj, surf[cur]);
      if (k >= 32) { sup_ok = 0; if (++sup_dif >= 6 && !tapado) { tapado = 1; n_tap++; } }
      else { sup_dif = 0; if (k <= 10 && tapado && ++sup_ok >= 10) tapado = 0; }
    }
    int s = cur ^ 1, hd = 0;
    if (dib && t_on && !menu) {  // (0.0.6) el ultimo frame completo del registro, dibujado por la Vita
      RFrame *rg = regTomar();
      if (!rg) t_cnt[T_NUEVO]++;
      else if (rg->frame == t_frame) {  // el mismo frame: se deja la superficie, como mucho 2 veces (30 fps); si no, normal
        if (t_ok && t_rep < 2) { t_rep++; hd = 2; } else t_cnt[T_NUEVO]++;
      } else { int r = trFrame(c, rg, s); t_cnt[r]++; t_frame = rg->frame; t_ok = !r; hd = !r; t_rep = 0; }
      regSoltar();
    }
    if (hd == 2) { dibujada = fr + 1; t_hd++; goto fin; }
    if (hd) { t_hd++; cur_hd = 1; goto listo_hd; }
    t_nat++; cur_hd = 0;
    sceGxmBeginScene(c, 0, rt, 0, 0, 0, &cs[s], &ds);
    sceGxmSetVertexProgram(c, vp);
    sceGxmSetFragmentProgram(c, fp);
    void *u;
    sceGxmReserveVertexDefaultUniformBuffer(c, &u);
    // Ortografica de pixeles a pantalla, guardada como la de vita2d (matrix_init_orthographic: por columnas, el
    // desplazamiento en [12] y [13]; el sombreador hace mul(pos, wvp)). 0.0.3.1 la tenia por filas: un trapecio de un color.
    static const float wvp[16] = { 2.0f / W, 0, 0, 0, 0, -2.0f / H, 0, 0, 0, 0, -2, 0, -1, 1, 1, 1 };
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
    sceGxmEndScene(c, 0, 0);
  listo_hd:
    cur = s; dibujada = fr + 1; n_pres++;
    uint32_t d = now() - t0;
    us_sum += d;
    if (d > us_max) us_max = d;
  }
fin:
  fr++;
  return r;
}

// --- menu ---
static tai_hook_ref_t ref_ctrl;
static SceUID hk_ctrl = -1;
static uint32_t combo0;
static int hookCtrl(SceCtrlDataPsp *p) {  // botones de la Vita hacia el PSP
  if (p && listo && !falla) {
    uint32_t combo = (p->Buttons & 0x308) == 0x308;  // L (0x100) + R (0x200) + START (0x8)
    if (combo && !combo0 && (menu || usada + 10 > fr)) { menu ^= 1; pintado = 0; }  // (solo si se ve la imagen del PSP)
    combo0 = combo;
    if (menu || combo) { p->Buttons = 0; p->Lx = p->Ly = p->Rx = p->Ry = 128; }  // el juego no ve nada apretado
  }
  return TAI_CONTINUE(int, ref_ctrl, p);
}
const char *nlVersion(void);  // nativedraw.c: version de NativeLink (de su log), 0 si no esta cargado

static void texto(int x, int y, const char *s, uint32_t col) {
  for (; *s && x + 8 <= MW; s++, x += 8)
    if (*s > 32 && *s < 127)
      for (int r = 0; r < 16; r++)
        for (int b = 0; b < 8; b++)
          if (font8x16[*s - 32][r] & (0x80 >> b)) mpix[(y + r) * MW + x + b] = col;
}
static void pintar(void) {  // colores en memoria 0xAABBGGRR
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
  if (hk_end < 0 || hk_tex < 0) return sceKernelExitDeleteThread(0);
  while (fr < 30) sceKernelDelayThread(100000);  // GXM ya iniciado por Adrenaline
  int r1 = programas(), r2 = r1 >= 0 ? destino() : 0;
  t_on = r1 >= 0 && r2 >= 0 && dib && trInit() >= 0;  // (0.0.6) traductor: solo con dibujar
  alog("presentador: programas %08x, destino %08x (superficies en %s y %s) | libre despues (MB de una vez): CDRAM %u, "
       "LPDDR fisica %u, LPDDR %u\n", r1, r2, tipo(surf[0]), tipo(surf[1]), probe(CDRAM), probe(PHY), probe(UNC));
  if (r1 < 0 || r2 < 0) return sceKernelExitDeleteThread(0);
  listo = 1;
  for (uint32_t t = now(), seen = 0, k = 0, aviso = 0, m0 = 0;;) {  // cada 10 s: cuantos frames y cuanto costo
    sceKernelDelayThread(50000);
    if (menu && !pintado) { pintar(); __sync_synchronize(); pintado = 1; }
    if (menu != m0) { m0 = menu; alog("presentador: menu %s\n", m0 ? "abierto" : "cerrado"); }
    static uint32_t tp0;
    if (tapado != tp0) { tp0 = tapado; alog("traductor: %s\n", tp0 ? "algo tapa la imagen (no esta en el registro): imagen normal" : "vuelve el dibujo de la Vita"); }
    if (aviso < 2 && n_pres >= (aviso ? 60 : 1)) alog("presentador: %s\n", aviso++ ? "60 frames dibujados" : "primer frame dibujado");
    if (falla == 1) { falla = 2; alog("presentador: la superficie salio negra 30 frames seguidos y la imagen del juego no: se deja la "
                                      "imagen normal hasta cerrar Adrenaline\n"); }
    if (++k < 200) continue;
    k = 0;
    uint32_t p = n_pres, m = us_max, s = us_sum, dt = (now() - t) / 1000, nd = n_dma;
    n_pres = 0; n_dma = 0; us_sum = 0; us_max = 0; t = now();
    if (psp_tex && !seen++)
      alog("presentador: textura del juego %p (512x272, formato %08x, filtros %x/%x)\n", psp_tex,
           sceGxmTextureGetFormat(psp_tex), sceGxmTextureGetMinFilter(psp_tex), sceGxmTextureGetMagFilter(psp_tex));
    if (t_on) {
      uint32_t h = t_hd, nn = t_nat, dr = t_draws; t_hd = t_nat = t_draws = 0;
      char b[200]; int k = 0;
      for (int i = 1; i < TN; i++) if (t_cnt[i]) { k += sceClibSnprintf(b + k, sizeof b - k, " %s=%u", tnom[i], t_cnt[i]); t_cnt[i] = 0; }
      b[k] = 0;
      alog("traductor: frames HD %u, con la imagen normal %u%s%s | draws dibujados %u\n", h, nn, k ? " | motivos:" : "", b, dr);
    }
    alog("presentador: %u frames en %u ms por la superficie (cambios %u en total, %u con copia propia), CPU %u us/frame (max %u) | "
         "casi negros: juego %u, superficie %u%s\n", p, dt, n_camb, nd, p ? s / p : 0, m, n_neg_j, n_neg_p,
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

void presStart(void) {
  dib = swOn("ux0:data/nativedraw/dibujar");
  alog("presentador: %s; menu con L+R+START\n", dib ? "reemplaza la imagen siempre (dibujar)" :
       "reemplaza la imagen solo con el menu abierto (crear ux0:data/nativedraw/dibujar para siempre)");
  SceUID th = sceKernelCreateThread("nativedraw_presentador", presThread, 0xA0, 0x2000, 0, 0, 0);
  if (th >= 0) sceKernelStartThread(th, 0, 0);
}
void presStop(void) {
  if (hk_end >= 0) taiHookRelease(hk_end, ref_end);
  if (hk_tex >= 0) taiHookRelease(hk_tex, ref_tex);
  if (hk_dma >= 0) taiHookRelease(hk_dma, ref_dma);
  if (hk_ctrl >= 0) taiHookRelease(hk_ctrl, ref_ctrl);
}
