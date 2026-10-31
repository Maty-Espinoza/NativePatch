// reproduccion.c: vuelve a dibujar en la Vita, con el mismo traductor (trFrame), un frame guardado por un volcado
// (fK_registro.bin, formato de volBin en volcado.c). Sirve para separar un error que depende del momento (datos que
// cambian, carreras con la GPU) de uno que sale siempre con los mismos datos (traductor, sombreadores).
// Se incluye dentro de presentador.c: presStart llama a repInicio, el hilo del presentador a repHilo y el de Adrenaline
// a repDibujar. Uso: ux0:data/nativedraw/reproducir con la ruta del .bin. Deja fK_reproduccion.bmp y
// fK_reproduccion_intermedia.bmp junto al .bin y escribe "no" en el archivo (una vez por juego).
// Detalle: docs/dev/volcados.md.
static char rep_ruta[128];
// rep_on: 0 nada, 1 por cargar, 2 cargado (lo dibuja el hilo de Adrenaline), 3 dibujado (por escribir)
static volatile uint32_t rep_on;
// rep_f: el frame cargado; rep_hd y rep_rt: copias de la superficie y de la imagen intermedia 0 despues de dibujarlo
static RFrame *rep_f;
static uint8_t *rep_hd, *rep_rt;
static SceUID rep_blk[2];

// Lee la ruta de ux0:data/nativedraw/reproducir (sin espacios ni saltos al final); si hay una y no es "no", rep_on = 1.
static void repInicio(void) {
  SceUID fd = sceIoOpen("ux0:data/nativedraw/reproducir", SCE_O_RDONLY, 0);
  if (fd < 0) return;
  int n = sceIoRead(fd, rep_ruta, sizeof rep_ruta - 1);
  sceIoClose(fd);
  while (n > 0 && (rep_ruta[n - 1] == '\n' || rep_ruta[n - 1] == '\r' || rep_ruta[n - 1] == ' ')) n--;
  rep_ruta[n > 0 ? n : 0] = 0;
  if (n > 4 && !(rep_ruta[0] == 'n' && rep_ruta[1] == 'o')) { rep_on = 1; alog("reproduccion: %s\n", rep_ruta); }
}

// En el hilo del presentador: lee el .bin a un RFrame propio (cabecera, revisada contra los topes de registro.h, y cada
// arreglo alineado a 4) y las texturas a memoria que lee la GPU. Si sale bien, rep_on = 2; si no, rep_on = 0.
static void repCargar(void) {
  SceUID fd = sceIoOpen(rep_ruta, SCE_O_RDONLY, 0);
  if (fd < 0) { alog("reproduccion: no se pudo abrir %s\n", rep_ruta); rep_on = 0; return; }
  void *p = 0, *q = 0;
  rep_blk[0] = sceKernelAllocMemBlock("nativedraw_rep", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (sizeof(RFrame) + 0x3FFFF) & ~0x3FFFFu, 0);
  rep_blk[1] = sceKernelAllocMemBlock("nativedraw_rep_img", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 0x500000, 0);
  if (rep_blk[0] < 0 || rep_blk[1] < 0 || sceKernelGetMemBlockBase(rep_blk[0], &p) < 0 || sceKernelGetMemBlockBase(rep_blk[1], &q) < 0) {
    alog("reproduccion: sin memoria\n"); sceIoClose(fd); rep_on = 0; return;
  }
  rep_f = p; rep_hd = q; rep_rt = (uint8_t *)q + 0x240000;
  uint8_t *tx = gmem(TX_N + 0x40000, RD);  // (la GPU lee las texturas de aca)
  uint32_t h[16];
  sceIoRead(fd, h, sizeof h);
  RFrame *f = rep_f;
  int ok = h[0] == 0x47524E44 && h[13] == NRS && h[14] == sizeof(RDraw) && h[5] <= RD_N && h[6] <= RE_N && h[7] <= RA_N && h[9] <= TN_N && tx;
  if (ok) {
    f->frame = h[2]; f->fb = h[3]; f->fbw = h[4]; f->nd = h[5]; f->ne = h[6]; f->na = h[7]; f->flags = h[8]; f->nt = h[9]; f->ntx = h[10];
    f->esp = h[11]; f->espf = h[12]; f->nimg = h[1] >= 3 ? h[15] : 0;  // (h[1]: version del formato)
    uint8_t rs[64];
    // LEE: n bytes a dst y salta el relleno hasta multiplo de 4 (como PON en volBin)
#define LEE(dst, n) do { uint32_t n_ = (n); if (n_ && sceIoRead(fd, dst, n_) != (int)n_) ok = 0; if (n_ & 3) { uint8_t z[4]; sceIoRead(fd, z, 4 - (n_ & 3)); } } while (0)
    LEE(rs, NRS);
    LEE(f->ta, f->nt * 4); LEE(f->tn, f->nt * 4); LEE(f->to, f->nt * 4); LEE(f->tq, f->nt * 4); LEE(f->tm, f->nt * 8);
    LEE(f->d, f->nd * sizeof(RDraw)); LEE(f->e, f->ne * NRS * 4); LEE(f->a, f->na);
#undef LEE
    if (ok && f->ntx <= TX_N + 0x40000 && sceIoRead(fd, tx, f->ntx) != (int)f->ntx) ok = 0;
    for (uint32_t i = 0; ok && h[1] < 4 && i < f->nt; i++) if (f->tn[i] >> 31) ok = 0;  // (antes de la 4: con swizzle sin ordenar)
    f->tx = tx;
  }
  sceIoClose(fd);
  if (!ok) { alog("reproduccion: el archivo no es un registro de esta version\n"); rep_on = 0; return; }
  alog("reproduccion: frame %u cargado (%u draws, %u texturas)\n", f->frame, f->nd, f->nt);
  rep_on = 2;
}

// En el hilo de Adrenaline, en vez del frame del juego: dibuja el frame cargado en la superficie s (base: la imagen
// normal, debajo), espera a la GPU y copia la superficie y la imagen intermedia 0. 1 si lo dibujo (rep_on = 3).
static int repDibujar(SceGxmContext *c, int s, const SceGxmTexture *base) {
  if (rep_on != 2) return 0;
  int r = trFrame(c, rep_f, s, base);
  sceGxmFinish(c);
  sceDmacMemcpy(rep_hd, surf[s], LINE * H * 4);
  if (rts[0].mem) sceDmacMemcpy(rep_rt, rts[0].mem, LINE * H * 4);
  alog("reproduccion: dibujado (resultado del traductor: %s)\n", tnom[r]);
  rep_on = 3;
  return 1;
}

// En el hilo del presentador: carga el .bin cuando el traductor esta listo y, despues de dibujado, escribe los dos BMP
// junto al .bin (fK_registro.bin -> fK_reproduccion*.bmp), escribe "no" en el archivo de la ruta y libera la memoria.
static void repHilo(void) {
  if (rep_on == 1 && t_on) repCargar();
  if (rep_on != 3) return;
  char p[160];
  static uint8_t w[1024 * 4 * 16];  // (bmp() usa vol_w, que sin volcado en curso no existe: se presta este)
  uint8_t *vw = vol_w;
  vol_w = w;
  int n = sceClibStrnlen(rep_ruta, sizeof rep_ruta);
  if (n > 13 && !sceClibStrncmp(rep_ruta + n - 13, "_registro.bin", 13)) n -= 13;
  sceClibSnprintf(p, sizeof p, "%.*s_reproduccion.bmp", n, rep_ruta); bmp(p, rep_hd, W, H, LINE);
  sceClibSnprintf(p, sizeof p, "%.*s_reproduccion_intermedia.bmp", n, rep_ruta); bmp(p, rep_rt, LINE, H, LINE);
  vol_w = vw;
  SceUID fd = sceIoOpen("ux0:data/nativedraw/reproducir", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (fd >= 0) { sceIoWrite(fd, "no\n", 3); sceIoClose(fd); }
  for (int i = 0; i < 2; i++) if (rep_blk[i] > 0) sceKernelFreeMemBlock(rep_blk[i]);
  alog("reproduccion: listo en %s\n", p);
  rep_on = 0;
}
