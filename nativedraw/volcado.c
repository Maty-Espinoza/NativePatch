// volcado.c: volcados de frames para depurar lo que se ve en pantalla. Se incluye dentro de presentador.c: el hilo de
// Adrenaline (hookEnd) llama a volTexto y volFrame para capturar cada frame; el hilo del presentador llama a volHilo
// para armar el volcado (L+R+SELECT o automatico) y escribirlo en ux0:data/nativedraw/volcados/NNN/. Guarda por frame
// la imagen del PSP, la de la Vita, un texto con como se hizo y una copia del registro del frame (registro.h) para
// tools/volcado.py y reproduccion.c. Nivel en ux0:data/nativedraw/volcado (volNivel). Detalle: docs/dev/volcados.md.
// TODO:
// - elegir el nivel del volcado desde el menu L+R+START (hoy solo con el archivo)
#include <psp2/io/stat.h>
void *sceDmacMemcpy(void *dst, const void *src, SceSize size);
#define VN 3            // frames por volcado (como maximo)
#define VL 256          // linea de cada frame (como se hizo la superficie)
#define VB_N (2 << 20)  // registro por frame
// vol_req: pedido de L+R+SELECT; vol_on: 0 nada, 3 armado (auto), 1 capturando, 2 para escribir; vol_k: frames listos
static volatile uint32_t vol_req, vol_on, vol_k;
extern volatile uint32_t nd_exacta_hasta;  // (captura.c) hasta cuando el registro trae la imagen exacta del PSP
// si es automatico; volcados hechos; hora del ultimo; bloques malos del que disparo el automatico; hora del pedido
static uint32_t vol_auto, vol_n, vol_t, vol_dif, vol_t0;
// Nivel (0 desactivado, 1 pantallazo, 2 frame, 3 secuencia, 4 auto) y frames por volcado
static uint32_t vol_nivel = 3, vol_nf = VN;
static const char *vol_nom[5] = { "desactivado", "pantallazo", "frame", "secuencia", "auto" };
// Nivel de volcado: 4 si existe ux0:data/nativedraw/volcado_auto (y no dice "no"); si no, la palabra de
// ux0:data/nativedraw/volcado (se comparan 4 letras; "no" = desactivado); sin archivo, 3 (secuencia).
int volNivel(void) {
  char b[16] = { 0 };
  SceUID fd = sceIoOpen("ux0:data/nativedraw/volcado_auto", SCE_O_RDONLY, 0);
  if (fd >= 0) { sceIoRead(fd, b, 2); sceIoClose(fd); if (!(b[0] == 'n' && b[1] == 'o')) return 4; }
  if ((fd = sceIoOpen("ux0:data/nativedraw/volcado", SCE_O_RDONLY, 0)) < 0) return 3;
  sceClibMemset(b, 0, sizeof b); sceIoRead(fd, b, 15); sceIoClose(fd);
  for (int i = 0; i < 5; i++) if (!sceClibStrncmp(b, vol_nom[i], 4)) return i;
  return b[0] == 'n' ? 0 : 3;  // ("no": desactivado)
}
// Memoria de cada frame capturado: vol_hd superficie de la Vita, vol_ps imagen del PSP, vol_tx su linea, vol_ok si hubo
// superficie. vol_w: memoria con cache para escribir (192 KB). vol_last: la linea del ultimo frame hecho (se empareja
// con el siguiente que muestra Adrenaline). vol_rt: la imagen intermedia 0 en f0 (vol_rtfb: su direccion; ~0u: no hay).
static uint8_t *vol_hd[VN], *vol_ps[VN], *vol_w;
static char vol_tx[VN][VL], vol_last[VL];
static uint32_t vol_ok[VN], vol_lastn, vol_mem, vol_rtfb;
static uint8_t *vol_rt; static SceUID vol_rtblk;
static SceUID vol_blk[VN + 1], vol_bblk[VN];
static uint8_t *vol_bb[VN];                 // bloques para registros
static uint32_t vol_bbn[VN], vol_bpend = VN;  // bytes en cada uno; el del ultimo frame hecho (VN: ninguno)
static int vol_bf[VN] = { VN, VN, VN };     // bloque del registro de cada frame capturado (VN: sin registro)
// 1 si el bloque de registro b ya es de un frame capturado
static int volUsado(int b) { for (int i = 0; i < (int)vol_k; i++) if (vol_bf[i] == b) return 1; return 0; }
// En el hilo de Adrenaline: copia el registro f a un bloque libre (lo toma el frame que viene, volFrame). Formato: una
// cabecera de 16 palabras y despues cada arreglo de RFrame alineado a 4 (lo lee reproduccion.c y tools/volcado.py; si
// cambia, cambiar los dos). Si no entra en VB_N queda sin registro.
static void volBin(RFrame *f) {
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
static uint8_t *vol_nat;          // la imagen del PSP exacta del ultimo frame dibujado (del registro); 0: no hay
static uint32_t vol_natf, vol_natw, vol_natok;  // su formato (0 5650, 1 5551, 2 4444, 3 8888), ancho y si hay
static SceUID vol_natblk;
static uint32_t volPx(const uint8_t *p, uint32_t f, uint32_t i) {  // pixel i de p en formato f -> 0xBBGGRR aproximado
  if (f == 3) return ((const uint32_t *)p)[i] & 0xFFFFFF;
  uint32_t c = ((const uint16_t *)p)[i];
  if (f == 0) return (c & 31) << 3 | (c >> 5 & 63) << 10 | (c >> 11 & 31) << 19;
  if (f == 1) return (c & 31) << 3 | (c >> 5 & 31) << 11 | (c >> 10 & 31) << 19;
  return (c & 15) << 4 | (c >> 4 & 15) << 12 | (c >> 8 & 15) << 20;
}
// Cuantos bloques de 16x16 pixeles del PSP (de 510) difieren entre la imagen del PSP (la exacta si la hay; si no, pj,
// 512 de ancho) y la superficie de la Vita ps: color promedio de 16 puntos con mas de 20 de diferencia en algun canal.
// El promedio hace que las diferencias finas del escalado no cuenten y un sprite con pedazos ajenos si.
// La usa el presentador para su estadistica de frames distintos.
static int volDistintos(const uint32_t *pj, const uint32_t *ps) {
  const uint8_t *nb = vol_natok ? vol_nat : (const uint8_t *)pj;
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

// Detector del volcado automatico: cuantos bloques de 16x16 de la superficie de la Vita ps no se parecen (mas de 20 en
// algun canal, como volDistintos) a ninguna de las ultimas n_img copias de la imagen del PSP (img[]). La Vita va 1-2
// frames atras, asi que vale la copia mas parecida de cada bloque: un bloque malo es un error, no atraso. No le cuesta
// nada al PSP. mejor: si no es 0, la copia que mas se parece en total.
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

// En el hilo de Adrenaline, despues de preparar la superficie: anota como se hizo (como, y el motivo del traductor si
// fallo; -1 sin traductor), copia el registro f (volBin) y, si trae la imagen exacta del PSP, la guarda en vol_nat.
static void volTexto(RFrame *f, const char *como, int motivo) {
  if ((vol_on != 1 && vol_on != 3) || !vol_mem) return;
  vol_lastn = sceClibSnprintf(vol_last, VL, "superficie: %s%s%s | tapado=%u\n", como, motivo > 0 ? ", motivo: " : "", motivo > 0 ? tnom[motivo] : "", tapado);
  volBin(f);
  if (f && f->nimg && vol_nat && motivo <= 0) {  // (se compara en el frame que viene)
    vol_natf = f->fbw >> 16 & 3; vol_natw = f->fbw & 0xFFFF;
    sceDmacMemcpy(vol_nat, f->tx + f->nimg - 1, vol_natw * 272 * (vol_natf == 3 ? 4 : 2)); vol_natok = 1;
  } else if (motivo != 0) vol_natok = 0;  // (imagen normal: no hay registro dibujado; "repite": se deja la anterior)
}

// En el hilo de Adrenaline, al empezar su hookEnd, con la superficie que se va a mostrar (surf[cur], si con_sup) y la
// imagen del PSP pj. Armado (auto): 1 de cada 2 frames, con 1 s desde el ultimo volcado, si 6 o mas bloques (y menos de
// 255: un dialogo encima) no se parecen a la imagen del PSP, empieza a capturar. Capturando: copia por DMA las dos
// imagenes y la linea del frame (y en f0 la imagen intermedia 0) hasta tener vol_nf frames; despues vol_on = 2.
static void volFrame(SceGxmContext *c, const void *pj, int con_sup) {
  if (vol_on == 3 && con_sup && cur_hd && !tapado && (fr & 1)) {
    int n = now() - vol_t > 1000000 ? volMalos(surf[cur], 0) : 0;
    // se confirma con la GPU terminada: si no, puede leer la superficie a medio dibujar
    if (n >= 6 && n < 255) { sceGxmFinish(c); n = volMalos(surf[cur], 0); }
    if (n >= 6 && n < 255) { vol_dif = n; vol_k = 0; vol_on = 1; }
  }
  if (vol_on != 1 || vol_k >= vol_nf || !vol_lastn) return;  // (sin la linea del frame todavia: desde el siguiente)
  // (L+R+SELECT espera la imagen exacta, hasta 0,5 s)
  if (!vol_k && !vol_dif && vol_nivel >= 2 && !vol_natok && now() - vol_t0 < 500000) return;
  uint32_t k = vol_k;
  vol_bf[k] = vol_bpend; vol_bpend = VN;  // (el registro del frame que se ve)
  sceGxmFinish(c);
  int mej = -1;
  // se guarda la imagen del PSP que mas se parece (la del mismo frame)
  if (vol_auto && con_sup) volMalos(surf[cur], &mej);
  sceDmacMemcpy(vol_ps[k], mej >= 0 ? img[mej] : pj, 512 * 272 * 4);
  vol_ok[k] = con_sup;
  if (con_sup) { sceDmacMemcpy(vol_hd[k], surf[cur], LINE * H * 4); sceClibMemcpy(vol_tx[k], vol_last, VL); }
  if (k == 0) { vol_rtfb = vol_rt && rts[0].mem && rts[0].fb != ~0u ? rts[0].fb : ~0u; if (vol_rtfb != ~0u) sceDmacMemcpy(vol_rt, rts[0].mem, LINE * H * 4); }
  else { sceClibSnprintf(vol_tx[k], VL, "superficie: no se uso en este frame (Adrenaline mostro la imagen normal)\n"); vol_bf[k] = VN; }
  if (k == 0 && vol_dif) sceClibSnprintf(vol_tx[0] + sceClibStrnlen(vol_tx[0], VL - 80), 80, "volcado automatico: %u bloques que no se parecen a ninguna imagen del PSP\n", vol_dif);
  if (++vol_k == vol_nf) vol_on = 2;
}

// Escribe en p un BMP de 32 bits (de arriba hacia abajo) con w x h pixeles RGBA de px (linea: pixeles por fila).
// Usa vol_w como memoria intermedia (16 filas).
static void bmp(const char *p, const uint8_t *px, uint32_t w, uint32_t h, uint32_t linea) {
  SceUID fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (fd < 0) return;
  uint8_t hd[54] = { 'B', 'M' };
  uint32_t sz = 54 + w * h * 4, v[13] = { sz, 0, 54, 40, w, (uint32_t)-(int)h, 1 | 32 << 16, 0, w * h * 4, 2835, 2835, 0, 0 };
  sceClibMemcpy(hd + 2, v, 52);
  sceIoWrite(fd, hd, 54);
  // de a 16 filas: copiadas enteras (leer px de a pixel sin cache es muy lento), RGBA -> BGRA
  for (uint32_t y = 0; y < h; y += 16) {
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

// En el hilo del presentador: escribe en fd el texto del frame a partir de su registro b (formato de volBin):
// texturas, cada estado la primera vez (solo los registros que cambiaron) y la lista de draws. Arma el texto en vol_w.
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

// Toma la memoria del volcado segun el nivel: imagenes (obligatorias) y, desde "frame", registros, imagen exacta e
// imagen intermedia (opcionales: si no hay, el volcado sale sin ellos). 0 si no hay memoria para las imagenes.
// Los registros van en memoria fisica: las imagenes intermedias del traductor se reservan antes.
static int volMem(void) {
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
  for (int i = 0; i < (int)vol_nf && vol_nivel >= 2; i++) {  // registros
    void *q = 0;
    vol_bblk[i] = sceKernelAllocMemBlock("nativedraw_volcado_reg", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, VB_N, 0);
    vol_bb[i] = vol_bblk[i] >= 0 && sceKernelGetMemBlockBase(vol_bblk[i], &q) >= 0 ? q : 0;
    nb += vol_bb[i] != 0;
  }
  void *r = 0, *nn = 0;
  // imagen exacta del PSP e imagen intermedia
  if (vol_nivel >= 2) vol_natblk = sceKernelAllocMemBlock("nativedraw_volcado_nat", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 0x100000, 0);
  vol_nat = vol_nivel >= 2 && vol_natblk >= 0 && sceKernelGetMemBlockBase(vol_natblk, &nn) >= 0 ? nn : 0; vol_natok = 0;
  if (vol_nivel >= 2) vol_rtblk = sceKernelAllocMemBlock("nativedraw_volcado_rt", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, (LINE * H * 4 + 0x3FFFF) & ~0x3FFFFu, 0);
  vol_rt = vol_nivel >= 2 && vol_rtblk >= 0 && sceKernelGetMemBlockBase(vol_rtblk, &r) >= 0 ? r : 0;
  vol_bpend = VN; vol_mem = 1;
  alog("volcado: memoria lista, nivel %s (%u frames, %u bloques para registros)\n", vol_nom[vol_nivel], vol_nf, nb);
  return 1;
}
static void volLibre(void) {  // libera toda la memoria del volcado
  for (int i = 0; i <= VN; i++) if (vol_blk[i] > 0) { sceKernelFreeMemBlock(vol_blk[i]); vol_blk[i] = 0; }
  for (int i = 0; i < VN; i++) { if (vol_bblk[i] > 0) sceKernelFreeMemBlock(vol_bblk[i]); vol_bblk[i] = 0; vol_bb[i] = 0; }
  if (vol_rtblk > 0) sceKernelFreeMemBlock(vol_rtblk);
  if (vol_natblk > 0) sceKernelFreeMemBlock(vol_natblk);
  vol_natblk = 0; vol_nat = 0; vol_natok = 0;
  vol_rtblk = 0; vol_rt = 0; vol_mem = 0;
}
// En el hilo del presentador (cada 50 ms): en automatico arma el volcado (vol_on = 3, como mucho 10 por juego); con un
// pedido de L+R+SELECT toma la memoria y empieza a capturar; cuando la captura termino (vol_on = 2) escribe los
// archivos en la primera carpeta volcados/NNN libre y libera la memoria (salvo en automatico).
static void volHilo(void) {
  if (vol_auto && !vol_on && vol_n < 10) {  // automatico: se arma (la memoria queda tomada)
    if (!vol_mem && !volMem()) { vol_auto = 0; return; }
    vol_k = 0; vol_dif = 0; vol_lastn = 0; vol_on = 3;
    return;
  }
  // pedido (L+R+SELECT): memoria (se libera al terminar, salvo en automatico)
  if (vol_req && (!vol_on || vol_on == 3)) {
    vol_req = 0;
    if (!vol_nivel) return;
    // por 1,5 s cada frame del registro trae su imagen del PSP exacta
    if (vol_nivel >= 2) nd_exacta_hasta = now() + 1500000;
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
