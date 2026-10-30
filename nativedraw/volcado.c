// volcado.c (NativeDraw 0.0.7.4): volcados para ver lo que pasa en pantalla. Se incluye dentro de presentador.c.
// L+R+SELECT guarda 4 frames seguidos (3 desde 0.0.7.9) en ux0:data/nativedraw/volcados/NNN/:
//   fK_normal.bmp  la imagen del PSP en ese frame (480x272)
//   fK_vita.bmp    lo que Adrenaline mostro en ese frame desde la superficie de NativeDraw (960x544); si ese frame no
//                  paso por la superficie, no se guarda
//   fK.txt         como se hizo esa superficie: dibujo de la Vita (y el frame del registro, con la lista de draws) o
//                  imagen normal y por que
// Las imagenes se copian por DMA en el hilo de Adrenaline (con sceGxmFinish antes: ese frame se traba un poco) y se
// escriben a la tarjeta desde el hilo del presentador (unos segundos).
// 0.0.7.5: escribir tardaba ~50 s (se leia de a byte la memoria sin cache) y frenaba al capturador: ahora de a filas
// enteras a memoria con cache y de a 16 filas a la tarjeta. El texto da cada estado una vez, solo con lo que cambio
// (antes se cortaba a los 64 KB en la pelea). Se empieza en el frame que ya tiene su texto (antes f0 salia sin datos).
// 0.0.7.7: en los frames 1 y 2 tambien fK_registro.bin: todo lo que la Vita recibio para dibujar ese frame (draws,
// estados, vertices, paletas y las texturas copiadas), para dibujarlo en el PC con tools/volcado/dibujar.py y comparar con
// la imagen normal. El texto lista ademas cada textura: filas copiadas y de que copia del espejo salieron.
// 0.0.7.8: volcado automatico (ux0:data/nativedraw/volcado_auto): con el archivo, el volcado queda armado todo el juego
// (memoria tomada desde el principio; el texto y el registro de cada frame se preparan siempre, ~1-3 ms del hilo de
// Adrenaline) y se dispara solo cuando la imagen de la Vita y la normal difieren en ese frame (5184 puntos, uno cada 5
// pixeles; entre 12 y la mitad: con mas es un dialogo encima, "tapado"). Asi f0 es el frame con el error, con su registro.
// Como mucho 10 por juego, con 1 s entre uno y otro (0.0.7.8: 3 s). Los registros (.bin) van en hasta 3 bloques de 2 MB de memoria
// fisica: el de f0 y el de f1 (0.0.7.7 pedia 3 MB y solo cabia uno).
// 0.0.7.9: el automatico de 0.0.7.8 tomaba la memoria fisica de las imagenes intermedias (KOF se veia con la imagen
// normal) y escribir el texto de cada frame costaba 2-40 ms del hilo de Adrenaline. Ahora: 3 frames, todos con su
// registro (3 bloques de 2 MB); en cada frame solo se copia el registro y una linea; el texto completo se arma al
// escribir, desde el registro. Las imagenes intermedias se reservan al iniciar el traductor (antes que el volcado).
// 0.0.8.5: NIVELES (como los del registro), en ux0:data/nativedraw/volcado (una palabra; sin el archivo: secuencia):
//   desactivado  L+R+SELECT no hace nada y no se toma memoria
//   pantallazo   L+R+SELECT guarda 1 frame: la imagen del PSP y la de la Vita (fK_normal.bmp, fK_vita.bmp) y una linea
//   frame        1 frame con todo: imagenes, texto, registro (.bin para tools/volcado.py) e imagen intermedia; desde
//                0.0.8.7 tambien la imagen del PSP exacta (se pide al apretar: el PSP la copia ~1,5 s)
//   secuencia    3 frames seguidos con todo (lo de antes)
//   auto         secuencia que ademas se dispara sola cuando algun bloque de 16x16 de la imagen de la Vita no se parece a
//                ninguna de las ultimas 4 imagenes del PSP (0.0.8.8, volMalos: sin costo para el PSP). Toma la memoria
//                desde el principio y gasta unos ms del hilo de Adrenaline por frame. Como mucho 10 por juego.
//                (ux0:data/nativedraw/volcado_auto, de 0.0.7.8, = auto)
// Detalle y como leerlos: docs/dev/volcados.md.
#include <psp2/io/stat.h>
void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
#define VN 3
#define VL 256          // linea de cada frame (como se hizo la superficie)
#define VB_N (2 << 20)  // registro por frame
static volatile uint32_t vol_req, vol_on, vol_k;  // pedido; 0 nada, 3 armado (auto), 1 capturando, 2 para escribir; frame capturado
extern volatile uint32_t nd_exacta_hasta;  // (captura.c)
static uint32_t vol_auto, vol_n, vol_t, vol_dif, vol_t0;   // (0.0.7.8) automatico; cuantos; hora del ultimo; bloques distintos del que disparo
static uint32_t vol_nivel = 3, vol_nf = VN;      // (0.0.8.5) nivel (0 desactivado, 1 pantallazo, 2 frame, 3 secuencia, 4 auto) y frames por volcado
static const char *vol_nom[5] = { "desactivado", "pantallazo", "frame", "secuencia", "auto" };
int volNivel(void) {  // (0.0.8.5) lee ux0:data/nativedraw/volcado (tambien lo usa captura.c)
  char b[16] = { 0 };
  SceUID fd = sceIoOpen("ux0:data/nativedraw/volcado_auto", SCE_O_RDONLY, 0);
  if (fd >= 0) { sceIoRead(fd, b, 2); sceIoClose(fd); if (!(b[0] == 'n' && b[1] == 'o')) return 4; }
  if ((fd = sceIoOpen("ux0:data/nativedraw/volcado", SCE_O_RDONLY, 0)) < 0) return 3;
  sceClibMemset(b, 0, sizeof b); sceIoRead(fd, b, 15); sceIoClose(fd);
  for (int i = 0; i < 5; i++) if (!sceClibStrncmp(b, vol_nom[i], 4)) return i;
  return b[0] == 'n' ? 0 : 3;  // ("no": desactivado)
}
static uint8_t *vol_hd[VN], *vol_ps[VN], *vol_w;  // vol_w: para escribir (con cache, 192 KB)
static char vol_tx[VN][VL], vol_last[VL];           // vol_last: la linea del ultimo frame hecho (se empareja con el siguiente)
static uint32_t vol_ok[VN], vol_lastn, vol_mem, vol_rtfb;
static uint8_t *vol_rt; static SceUID vol_rtblk;  // (0.0.8.3) la imagen intermedia 0 de f0 (la Vita la dibuja al doble)
static SceUID vol_blk[VN + 1], vol_bblk[VN];
static uint8_t *vol_bb[VN];                 // bloques para registros
static uint32_t vol_bbn[VN], vol_bpend = VN;  // bytes en cada uno; el del ultimo frame hecho (VN: ninguno)
static int vol_bf[VN] = { VN, VN, VN };     // bloque del registro de cada frame capturado (VN: sin registro)
static int volUsado(int b) { for (int i = 0; i < (int)vol_k; i++) if (vol_bf[i] == b) return 1; return 0; }
static void volBin(RFrame *f) {  // en el hilo de Adrenaline: copia el registro f a un bloque libre (lo toma el frame que viene)
  vol_bpend = VN;
  int k = 0;
  while (k < VN && (!vol_bb[k] || volUsado(k))) k++;
  if (k == VN || !f) return;
  uint8_t *b = vol_bb[k];
  uint32_t h[16] = { 0x47524E44, 3, f->frame, f->fb, f->fbw, f->nd, f->ne, f->na, f->flags, f->nt, f->ntx, f->esp, f->espf, NRS, sizeof(RDraw), f->nimg }, o = 0;
#define PON(p, n) do { uint32_t n_ = (n); if (o + n_ > VB_N) return; sceClibMemcpy(b + o, p, n_); o += (n_ + 3) & ~3u; } while (0)
  PON(h, sizeof h); PON(RS, NRS);
  PON(f->ta, f->nt * 4); PON(f->tn, f->nt * 4); PON(f->to, f->nt * 4); PON(f->tq, f->nt * 4); PON(f->tm, f->nt * 8);
  PON(f->d, f->nd * sizeof(RDraw)); PON(f->e, f->ne * NRS * 4); PON(f->a, f->na);
#undef PON
  if (o + f->ntx > VB_N) return;
  sceDmacMemcpy(b + o, f->tx, f->ntx);
  vol_bbn[k] = o + f->ntx; vol_bpend = k;
}
// (0.0.7.10) Bloques de 16x16 pixeles (510) cuyo color promedio (16 puntos) difiere en mas de 20 en algun canal. Asi las
// diferencias finas del escalado (sprites encogidos que la Vita muestrea distinto: 0.0.7.9 se disparaba con el logo de
// SNK) casi no cuentan y un sprite con pedazos ajenos si (los volcados con mosaicos dan 69-90 bloques; los buenos, 0).
static uint8_t *vol_nat;          // (0.0.8.4) la imagen del PSP exacta del ultimo frame dibujado (del registro); 0: no hay
static uint32_t vol_natf, vol_natw, vol_natok;  // su formato (0 5650, 1 5551, 2 4444, 3 8888), ancho y si hay
static SceUID vol_natblk;
static uint32_t volPx(const uint8_t *p, uint32_t f, uint32_t i) {  // pixel -> 0xBBGGRR aproximado
  if (f == 3) return ((const uint32_t *)p)[i] & 0xFFFFFF;
  uint32_t c = ((const uint16_t *)p)[i];
  if (f == 0) return (c & 31) << 3 | (c >> 5 & 63) << 10 | (c >> 11 & 31) << 19;
  if (f == 1) return (c & 31) << 3 | (c >> 5 & 31) << 11 | (c >> 10 & 31) << 19;
  return (c & 15) << 4 | (c >> 4 & 15) << 12 | (c >> 8 & 15) << 20;
}
static int volDistintos(const uint32_t *pj, const uint32_t *ps) {
  const uint8_t *nb = vol_natok ? vol_nat : (const uint8_t *)pj;  // (0.0.8.4) con la imagen exacta de ese frame, si la hay
  uint32_t nf = vol_natok ? vol_natf : 3, nw = vol_natok ? vol_natw : 512;
  int k = 0;
  for (uint32_t by = 0; by < 272 / 16; by++)
    for (uint32_t bx = 0; bx < 480 / 16; bx++) {
      int sa[3] = { 0, 0, 0 };
      for (uint32_t y = by * 16 + 2; y < by * 16 + 16; y += 4)
        for (uint32_t x = bx * 16 + 2; x < bx * 16 + 16; x += 4) {
          uint32_t a = volPx(nb, nf, y * nw + x), b = ps[2 * y * LINE + 2 * x];
          for (int c = 0; c < 3; c++) sa[c] += (int)(a >> (8 * c) & 255) - (int)(b >> (8 * c) & 255);
        }
      for (int c = 0; c < 3; c++) if (sa[c] > 20 * 16 || sa[c] < -20 * 16) { k++; break; }
    }
  return k;
}

// (0.0.8.8) Detector del automatico sin tocar al PSP: la imagen de la Vita va 1-2 frames atras de la del PSP, asi que se
// compara cada bloque de 16x16 con las ultimas 4 copias de la imagen del PSP (img[], las hace Adrenaline) y vale la mas
// parecida. Un bloque que no se parece a ninguna es un error de verdad (un sprite con mosaico), no atraso. 0.0.8.4-0.0.8.7
// comparaba con la imagen exacta, pero copiarla le costaba ~1,2 ms por frame al PSP y con eso los mosaicos no salian.
// mejor: si no es 0, la copia (de img[]) que mas se parece en total.
static int volMalos(const uint32_t *ps, int *mejor) {
  int k = 0, tot[4] = { 0, 0, 0, 0 };
  for (uint32_t by = 0; by < 272 / 16; by++)
    for (uint32_t bx = 0; bx < 480 / 16; bx++) {
      int mn = 1 << 30;
      for (uint32_t m = 0; m < n_img; m++) {
        const uint32_t *pj = img[m];
        int sa[3] = { 0, 0, 0 }, d = 0;
        for (uint32_t y = by * 16 + 2; y < by * 16 + 16; y += 4)
          for (uint32_t x = bx * 16 + 2; x < bx * 16 + 16; x += 4) {
            uint32_t a = pj[y * 512 + x], b = ps[2 * y * LINE + 2 * x];
            for (int c = 0; c < 3; c++) sa[c] += (int)(a >> (8 * c) & 255) - (int)(b >> (8 * c) & 255);
          }
        for (int c = 0; c < 3; c++) { int v = sa[c] < 0 ? -sa[c] : sa[c]; if (v > d) d = v; }
        tot[m] += d;
        if (d < mn) mn = d;
      }
      if (mn > 20 * 16) k++;
    }
  if (mejor) { int b = 0; for (uint32_t m = 1; m < n_img; m++) if (tot[m] < tot[b]) b = m; *mejor = b; }
  return k;
}

static void volTexto(RFrame *f, const char *como, int motivo) {  // en el hilo de Adrenaline: como se hizo la superficie (una linea)
  if ((vol_on != 1 && vol_on != 3) || !vol_mem) return;
  vol_lastn = sceClibSnprintf(vol_last, VL, "superficie: %s%s%s | tapado=%u\n", como, motivo > 0 ? ", motivo: " : "", motivo > 0 ? tnom[motivo] : "", tapado);
  volBin(f);
  if (f && f->nimg && vol_nat && motivo <= 0) {  // (0.0.8.4) su imagen del PSP exacta (se compara en el frame que viene)
    vol_natf = f->fbw >> 16 & 3; vol_natw = f->fbw & 0xFFFF;
    sceDmacMemcpy(vol_nat, f->tx + f->nimg - 1, vol_natw * 272 * (vol_natf == 3 ? 4 : 2)); vol_natok = 1;
  } else if (motivo != 0) vol_natok = 0;  // (imagen normal: no hay registro dibujado; "repite": se deja la anterior)
}

static void volFrame(SceGxmContext *c, const void *pj, int con_sup) {  // en el hilo de Adrenaline, al empezar su hookEnd
  if (vol_on == 3 && con_sup && cur_hd && !tapado && (fr & 1)) {  // (0.0.7.8) automatico: si el dibujo de la Vita no coincide (0.0.8.8: con
    int n = now() - vol_t > 1000000 ? volMalos(surf[cur], 0) : 0;      // ninguna de las ultimas imagenes del PSP; 1 de cada 2 frames)
    if (n >= 6 && n < 255) { sceGxmFinish(c); n = volMalos(surf[cur], 0); }  // (se confirma con la GPU ya terminada: 0.0.7.9
    if (n >= 6 && n < 255) { vol_dif = n; vol_k = 0; vol_on = 1; }              // leia la superficie a medio dibujar)
  }
  if (vol_on != 1 || vol_k >= vol_nf || !vol_lastn) return;  // (sin la linea del frame todavia: desde el siguiente)
  if (!vol_k && !vol_dif && vol_nivel >= 2 && !vol_natok && now() - vol_t0 < 500000) return;  // (0.0.8.7: L+R+SELECT espera la imagen exacta, hasta 0,5 s)
  uint32_t k = vol_k;
  vol_bf[k] = vol_bpend; vol_bpend = VN;  // (el registro del frame que se ve)
  sceGxmFinish(c);
  int mej = -1;
  if (vol_auto && con_sup) volMalos(surf[cur], &mej);  // (0.0.8.8) se guarda la imagen del PSP que mas se parece (la del mismo frame)
  sceDmacMemcpy(vol_ps[k], mej >= 0 ? img[mej] : pj, 512 * 272 * 4);
  vol_ok[k] = con_sup;
  if (con_sup) { sceDmacMemcpy(vol_hd[k], surf[cur], LINE * H * 4); sceClibMemcpy(vol_tx[k], vol_last, VL); }
  if (k == 0) { vol_rtfb = vol_rt && rts[0].mem && rts[0].fb != ~0u ? rts[0].fb : ~0u; if (vol_rtfb != ~0u) sceDmacMemcpy(vol_rt, rts[0].mem, LINE * H * 4); }
  else { sceClibSnprintf(vol_tx[k], VL, "superficie: no se uso en este frame (Adrenaline mostro la imagen normal)\n"); vol_bf[k] = VN; }
  if (k == 0 && vol_dif) sceClibSnprintf(vol_tx[0] + sceClibStrnlen(vol_tx[0], VL - 80), 80, "volcado automatico: %u bloques que no se parecen a ninguna imagen del PSP\n", vol_dif);
  if (++vol_k == vol_nf) vol_on = 2;
}

static void bmp(const char *p, const uint8_t *px, uint32_t w, uint32_t h, uint32_t linea) {  // 32 bits, de arriba hacia abajo
  SceUID fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (fd < 0) return;
  uint8_t hd[54] = { 'B', 'M' };
  uint32_t sz = 54 + w * h * 4, v[13] = { sz, 0, 54, 40, w, (uint32_t)-(int)h, 1 | 32 << 16, 0, w * h * 4, 2835, 2835, 0, 0 };
  sceClibMemcpy(hd + 2, v, 52);
  sceIoWrite(fd, hd, 54);
  for (uint32_t y = 0; y < h; y += 16) {  // de a 16 filas: leidas enteras (memoria sin cache), RGBA -> BGRA, a la tarjeta
    uint32_t m = h - y < 16 ? h - y : 16;
    for (uint32_t r = 0; r < m; r++) {
      uint32_t *q = (uint32_t *)(vol_w + r * w * 4);
      sceClibMemcpy(q, px + (y + r) * linea * 4, w * 4);
      for (uint32_t x = 0; x < w; x++) { uint32_t c = q[x]; q[x] = 0xFF000000u | (c & 0xFF) << 16 | (c & 0xFF00) | (c >> 16 & 0xFF); }
    }
    sceIoWrite(fd, vol_w, m * w * 4);
  }
  sceIoClose(fd);
}

// El texto del frame, desde su registro (en el hilo del presentador): texturas, estados (solo lo que cambio) y draws
static void volListado(SceUID fd, const uint8_t *b) {
  const uint32_t *h = (const uint32_t *)b;
  uint32_t nt = h[9], nd = h[5], ne = h[6], o = 64 + ((NRS + 3) & ~3u), k = 0, n = 192 << 10;
  const uint32_t *ta = (const uint32_t *)(b + o), *tn = ta + nt, *tq = tn + 2 * nt;
  const uint64_t *tm = (const uint64_t *)(tq + nt);
  const RDraw *d = (const RDraw *)(tm + nt);
  const uint32_t *e0 = (const uint32_t *)((const uint8_t *)d + ((nd * sizeof(RDraw) + 3) & ~3u));
  char *t = (char *)vol_w;
#define ESC(...) do { k += sceClibSnprintf(t + k, n - k, __VA_ARGS__); if (k > n - 1024) { sceIoWrite(fd, t, k); k = 0; } } while (0)
  ESC("registro: frame %u, draws %u, estados %u, imagen que se muestra %07x ancho %u, incompleto %x, texturas %u (%u KB)\n"
      "espejo: copia usada %u, ultima terminada %u | texturas (direccion, bytes, grupos de 8 filas copiados, copia mas vieja):\n",
      h[2], nd, ne, h[3], h[4] & 0xFFFF, h[8], nt, h[10] >> 10, h[11], h[12]);
  for (uint32_t i = 0; i < nt; i++)
    ESC("  t%u %07x %u%s grupos=%08x%08x copia=%u\n", i, ta[i], tn[i] & 0x7FFFFFFF, tn[i] >> 31 ? " swz" : "", (uint32_t)(tm[i] >> 32), (uint32_t)tm[i], tq[i]);
  ESC("(cada estado se escribe la primera vez, con los registros del GE que cambiaron)\n");
  const uint32_t *ant = 0;
  uint32_t ult = ~0u;
  for (uint32_t i = 0; i < nd; i++) {
    const uint32_t *e = e0 + d[i].st * NRS;
    if (d[i].st != ult) {
      ESC("  estado %u:", d[i].st);
      for (int r = 0; r < NRS; r++) if (!ant || ant[r] != e[r]) ESC(" %02x=%06x", RS[r], e[r] & 0xFFFFFF);
      ESC("\n");
      ant = e; ult = d[i].st;
    }
    ESC("%4u e%u destino=%06x prim=%u n=%u vt=%06x tex=%07x copia=%u paleta=%u\n", i, d[i].st, RE(e, 0x9C) & 0xFFFFFF, d[i].prim, d[i].n,
        d[i].vt, RE(e, 0x1E) & 1 ? texAddr(e) : 0, d[i].toff ? 1 : 0, d[i].clen);
  }
#undef ESC
  if (k) sceIoWrite(fd, t, k);
}

static int volMem(void) {  // toma la memoria del volcado; 0 si no hay
  uint32_t n = 0, ok = 1;
  for (int i = 0; i < (int)vol_nf && ok; i++) {  // superficie de la Vita + imagen del PSP, en un bloque
    void *p = 0;
    vol_blk[n] = sceKernelAllocMemBlock("nativedraw_volcado", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, (LINE * H * 4 + 512 * 272 * 4 + 0x3FFFF) & ~0x3FFFFu, 0);
    if (vol_blk[n] < 0 || sceKernelGetMemBlockBase(vol_blk[n++], &p) < 0) { ok = 0; break; }
    vol_hd[i] = p; vol_ps[i] = (uint8_t *)p + LINE * H * 4;
  }
  void *t = 0;
  if (ok) { vol_blk[n] = sceKernelAllocMemBlock("nativedraw_volcado", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x40000, 0); if (vol_blk[n] < 0 || sceKernelGetMemBlockBase(vol_blk[n++], &t) < 0) ok = 0; }
  if (!ok) { alog("volcado: sin memoria\n"); for (uint32_t i = 0; i < n; i++) if (vol_blk[i] >= 0) sceKernelFreeMemBlock(vol_blk[i]); for (int i = 0; i <= VN; i++) vol_blk[i] = 0; return 0; }
  vol_w = t;
  int nb = 0;
  for (int i = 0; i < (int)vol_nf && vol_nivel >= 2; i++) {  // registros, en memoria fisica (lo que queda despues de las imagenes intermedias); si no hay, sin ellos
    void *q = 0;
    vol_bblk[i] = sceKernelAllocMemBlock("nativedraw_volcado_reg", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, VB_N, 0);
    vol_bb[i] = vol_bblk[i] >= 0 && sceKernelGetMemBlockBase(vol_bblk[i], &q) >= 0 ? q : 0;
    nb += vol_bb[i] != 0;
  }
  void *r = 0, *nn = 0;
  if (vol_nivel >= 2) vol_natblk = sceKernelAllocMemBlock("nativedraw_volcado_nat", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 0x100000, 0);  // (0.0.8.4)
  vol_nat = vol_nivel >= 2 && vol_natblk >= 0 && sceKernelGetMemBlockBase(vol_natblk, &nn) >= 0 ? nn : 0; vol_natok = 0;
  if (vol_nivel >= 2) vol_rtblk = sceKernelAllocMemBlock("nativedraw_volcado_rt", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, (LINE * H * 4 + 0x3FFFF) & ~0x3FFFFu, 0);
  vol_rt = vol_nivel >= 2 && vol_rtblk >= 0 && sceKernelGetMemBlockBase(vol_rtblk, &r) >= 0 ? r : 0;
  vol_bpend = VN; vol_mem = 1;
  alog("volcado: memoria lista, nivel %s (%u frames, %u bloques para registros)\n", vol_nom[vol_nivel], vol_nf, nb);
  return 1;
}
static void volLibre(void) {
  for (int i = 0; i <= VN; i++) if (vol_blk[i] > 0) { sceKernelFreeMemBlock(vol_blk[i]); vol_blk[i] = 0; }
  for (int i = 0; i < VN; i++) { if (vol_bblk[i] > 0) sceKernelFreeMemBlock(vol_bblk[i]); vol_bblk[i] = 0; vol_bb[i] = 0; }
  if (vol_rtblk > 0) sceKernelFreeMemBlock(vol_rtblk);
  if (vol_natblk > 0) sceKernelFreeMemBlock(vol_natblk);
  vol_natblk = 0; vol_nat = 0; vol_natok = 0;
  vol_rtblk = 0; vol_rt = 0; vol_mem = 0;
}
static void volHilo(void) {  // en el hilo del presentador (cada 50 ms)
  if (vol_auto && !vol_on && vol_n < 10) {  // (0.0.7.8) automatico: se arma (la memoria queda tomada)
    if (!vol_mem && !volMem()) { vol_auto = 0; return; }
    vol_k = 0; vol_dif = 0; vol_lastn = 0; vol_on = 3;
    return;
  }
  if (vol_req && (!vol_on || vol_on == 3)) {  // pedido (L+R+SELECT): memoria (se libera al terminar, salvo en automatico)
    vol_req = 0;
    if (!vol_nivel) return;
    if (vol_nivel >= 2) nd_exacta_hasta = now() + 1500000;  // (0.0.8.7) por 1,5 s cada frame trae su imagen del PSP exacta
    vol_t0 = now();
    if (!vol_mem && !volMem()) return;
    vol_k = 0; vol_dif = 0; vol_lastn = 0; vol_on = 1;
    alog("volcado: capturando %u frame(s), nivel %s\n", vol_nf, vol_nom[vol_nivel]);
    return;
  }
  if (vol_on != 2) return;
  char d[64], p[96];
  sceIoMkdir("ux0:data/nativedraw/volcados", 0777);
  int n = 0;
  for (; n < 1000; n++) { sceClibSnprintf(d, sizeof d, "ux0:data/nativedraw/volcados/%03d", n); if (sceIoMkdir(d, 0777) >= 0) break; }
  for (int i = 0; i < (int)vol_nf; i++) {
    sceClibSnprintf(p, sizeof p, "%s/f%d_normal.bmp", d, i); bmp(p, vol_ps[i], 480, 272, 512);
    if (vol_ok[i]) { sceClibSnprintf(p, sizeof p, "%s/f%d_vita.bmp", d, i); bmp(p, vol_hd[i], W, H, LINE); }
    int b = vol_bf[i];
    sceClibSnprintf(p, sizeof p, "%s/f%d.txt", d, i);
    SceUID fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) { sceIoWrite(fd, vol_tx[i], sceClibStrnlen(vol_tx[i], VL)); if (b < VN && vol_bb[b]) volListado(fd, vol_bb[b]); sceIoClose(fd); }
    if (b < VN && vol_bb[b]) {
      sceClibSnprintf(p, sizeof p, "%s/f%d_registro.bin", d, i);
      if ((fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666)) >= 0) { sceIoWrite(fd, vol_bb[b], vol_bbn[b]); sceIoClose(fd); }
    }
  }
  if (vol_rtfb != ~0u) { sceClibSnprintf(p, sizeof p, "%s/f0_intermedia_%06x.bmp", d, vol_rtfb); bmp(p, vol_rt, LINE, H, LINE); }
  for (int i = 0; i < VN; i++) vol_bf[i] = VN;
  vol_n++; vol_t = now();
  if (!vol_auto || vol_n >= 10) volLibre();  // (en automatico la memoria queda para el siguiente)
  vol_on = 0;
  alog("volcado: listo en %s%s\n", d, vol_dif ? " (automatico)" : "");
}
