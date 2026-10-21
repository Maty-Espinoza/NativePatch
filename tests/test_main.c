// Tests del plugin en el PC. Se incluye main.c entero para poder revisar su estado
// interno. Cada test arma una display list en la memoria simulada de la PSP, la pasa
// por patchGeList como lo haria el GE y revisa como quedaron los comandos y vertices.
// Muchos tests corresponden a un bug que ya se arreglo: si alguno falla, volvio.
#include "../main.c"

extern char test_log[];
extern int test_log_len;
void mapPspMemory(void);

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  FALLA %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// --- Ayudas para armar listas ---------------------------------------------
#define LIST   0x08800000 // listas
#define VERTS  0x08900000 // vertices
static u32 *lp;
static u32 *cmd(u32 c, u32 d) { *lp = (c << 24) | (d & 0xffffff); return lp++; }
static u32 f24(float f) { union { float f; u32 i; } t; t.f = f; return t.i >> 8; } // float del GE (24 bits)
static float fcmd(u32 *p) { union { float f; u32 i; } t; t.i = (*p & 0xffffff) << 8; return t.f; }
static u32 *begin(void) { lp = (u32 *)LIST; return lp; }
static void end(void) { cmd(GE_CMD_FINISH, 0); cmd(GE_CMD_END, 0); }
static void run(void) { resetGeState(); patchGeList((u32 *)LIST, NULL); } // como sceGeListEnQueue
static void setFb(u32 ptr, u32 width) { cmd(GE_CMD_FRAMEBUFPTR, ptr); cmd(GE_CMD_FRAMEBUFWIDTH, (0x0a << 16) | width); }
static int logHas(const char *s) { logFlush(); return strstr(test_log, s) != NULL; }

// Estado limpio entre tests (como al iniciar un juego)
static void reset(void) {
  memset(&state, 0, sizeof(state));
  memset(memo, 0, MEMO_SIZE * sizeof(MemoEntry));
  memset(vmemo, 0, VMEMO_SIZE * sizeof(VMemo));
  memset(rt, 0, sizeof(rt));
  rt_count = 0; rt_next = RT_BASE; rt_enabled = 0;
  tex_patched_any = 0; rt_gen++; memset(&tcache, 0, sizeof(tcache)); tcache.res = 2; // sin cache valida
  game_offx = 2048 - 240; game_offy = 2048 - 136;
  memset((void *)LIST, 0, 0x100000);
  memset((void *)VERTS, 0, 0x10000);
  test_log_len = 0; test_log[0] = 0;
  logResetOnce();
  Options def = { .rt_on = 1, .rt_scale = 1, .omit_fbtex = 1, .fallback = 0, .nop_mark = 1, .walk_new = 1, .v2d = 1, .tx_ge = 1 };
  opt = def;
  passive = 0; walk_now = 1;
  big_transfer = 0; fbtex_seen = 0; vram_hi = 0; copy_vram = 0;
}

// ---------------------------------------------------------------------------
static void test_viewport_scissor_x2(void) {
  reset();
  begin();
  cmd(GE_CMD_OFFSETX, (2048 - 240) << 4); cmd(GE_CMD_OFFSETY, (2048 - 136) << 4);
  u32 *xs = cmd(GE_CMD_VIEWPORTXSCALE, f24(240)), *ys = cmd(GE_CMD_VIEWPORTYSCALE, f24(-136));
  u32 *xc = cmd(GE_CMD_VIEWPORTXCENTER, f24(2048)), *yc = cmd(GE_CMD_VIEWPORTYCENTER, f24(2048));
  u32 *s2 = cmd(GE_CMD_SCISSOR2, (271 << 10) | 479);
  end();
  int i;
  for (i = 0; i < 3; i++) { // la misma lista ejecutada 3 veces: se escala una sola vez
    run();
    CHECK(fcmd(xs) == 480 && fcmd(ys) == -272, "escala viewport %f %f (pasada %d)", fcmd(xs), fcmd(ys), i);
    CHECK(fcmd(xc) == 2048 && fcmd(yc) == 2048, "centro viewport %f %f", fcmd(xc), fcmd(yc));
    CHECK((*s2 & 0x3ff) == 959 && ((*s2 >> 10) & 0x3ff) == 543, "scissor2 %06lx", *s2 & 0xffffff);
  }
}

// Espejo retrovisor: viewport en una zona chica de la pantalla, debe escalarse, no forzarse
static void test_viewport_zona(void) {
  reset();
  begin();
  cmd(GE_CMD_OFFSETX, (2048 - 240) << 4); cmd(GE_CMD_OFFSETY, (2048 - 136) << 4);
  u32 *xs = cmd(GE_CMD_VIEWPORTXSCALE, f24(60)), *xc = cmd(GE_CMD_VIEWPORTXCENTER, f24(2048 - 240 + 240));
  u32 *s1 = cmd(GE_CMD_SCISSOR1, (10 << 10) | 180);
  end();
  run();
  CHECK(fcmd(xs) == 120, "escala %f, esperaba 120 (antes se forzaba 480)", fcmd(xs));
  CHECK(fcmd(xc) == 2048, "centro %f", fcmd(xc));
  CHECK((*s1 & 0x3ff) == 360 && ((*s1 >> 10) & 0x3ff) == 20, "scissor1 %06lx", *s1 & 0xffffff);
}

// La memoria de parches tenia 2048 entradas y Ridge Racer la desbordaba: al reejecutar
// una lista grande, valores ya parcheados se tomaban como originales (escala x4)
static void test_memo_lista_grande(void) {
  reset();
  begin();
  int i;
  for (i = 0; i < 20000; i++)
    cmd(GE_CMD_VIEWPORTXSCALE, f24(240));
  end();
  run(); run();
  int bad = 0;
  for (i = 0; i < 20000; i++)
    if (fcmd((u32 *)LIST + i) != 480)
      bad++;
  CHECK(bad == 0, "%d de 20000 escalas no quedaron en 480", bad);
}

// Vertices 2D: se duplican una vez aunque el juego reutilice el buffer, y de nuevo si lo reescribe
static void test_vertices_2d(void) {
  reset();
  short *v = (short *)VERTS; // formato sin relleno (solo posicion 16 bits): antes se duplicaba cada frame
  v[0] = 0; v[1] = 0; v[2] = 0; v[3] = 64; v[4] = 32; v[5] = 0;
  begin();
  cmd(GE_CMD_BASE, 0x080000);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  run(); run(); run();
  CHECK(v[3] == 128 && v[4] == 64, "tras 3 frames: %d,%d (esperaba 128,64)", v[3], v[4]);
  v[3] = 64; v[4] = 32; // el juego escribe valores nuevos en el mismo buffer
  run();
  CHECK(v[3] == 128 && v[4] == 64, "buffer reescrito: %d,%d", v[3], v[4]);
  v[3] = 480; v[4] = 272; run();
  CHECK(v[3] == 960 && v[4] == 544, "bordes de pantalla: %d,%d", v[3], v[4]);
}

// El GE avanza VADDR tras cada draw, tambien 3D: un draw 2D sin VADDR propio usa los siguientes
static void test_vaddr_avanza_tras_3d(void) {
  reset();
  short *v = (short *)VERTS;
  int i;
  for (i = 0; i < 5 * 3; i++)
    v[i] = 10;
  begin();
  cmd(GE_CMD_BASE, 0x080000);
  cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT); // 3D
  cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT); // 2D, sigue en el vertice 3
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  run();
  CHECK(v[0] == 10 && v[6] == 10, "se modificaron vertices 3D: %d %d", v[0], v[6]);
  CHECK(v[9] == 20 && v[12] == 20, "no se duplico el draw 2D: %d %d", v[9], v[12]);
}

// BJUMP es condicional: los draws que siguen deben parchearse (antes se saltaban)
static void test_bjump_no_salta(void) {
  reset();
  short *v = (short *)VERTS;
  v[0] = 50; v[3] = 60;
  begin();
  cmd(GE_CMD_BASE, 0x080000);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  u32 *bj = cmd(GE_CMD_BJUMP, 0);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  *bj = (GE_CMD_BJUMP << 24) | (((u32)lp - 8) & 0xffffff); // salta al FINISH
  run();
  CHECK(v[0] == 100 && v[3] == 120, "draw tras BJUMP sin parchear: %d %d", v[0], v[3]);
}

// BASE y VADDR se mantienen entre listas (antes se borraban y se parcheaba memoria equivocada)
static void test_estado_entre_listas(void) {
  reset();
  short *v = (short *)VERTS;
  v[0] = 7; v[3] = 9;
  begin();
  cmd(GE_CMD_BASE, 0x080000);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  end();
  run();
  begin(); // segunda lista: sin BASE ni VERTEXTYPE
  cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  run();
  CHECK(v[0] == 14 && v[3] == 18, "segunda lista no uso BASE/VERTEXTYPE anteriores: %d %d", v[0], v[3]);
}

// Textura que es otro framebuffer: se decide al momento del draw. En Ridge Racer la
// textura se fijaba con el buffer de 256 activo y luego volvia al principal.
static void test_textura_fb_al_dibujar(void) {
  reset();
  begin();
  setFb(0x000000, 512);
  setFb(0x088000, 512);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1);
  cmd(GE_CMD_TEXADDR0, 0x088000); cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 512);
  setFb(0x000000, 512);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
  u32 *prim = cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  run();
  CHECK(*prim == NOP_OMIT, "el draw que lee otro framebuffer no se omitio");
}

// Render target: se dibuja en la VRAM alta y la textura se redirige
static void test_render_target(void) {
  reset();
  rt_enabled = 1;
  begin();
  u32 *fp = cmd(GE_CMD_FRAMEBUFPTR, 0x1fe000), *fw = cmd(GE_CMD_FRAMEBUFWIDTH, (0x0a << 16) | 64);
  cmd(GE_CMD_TEXTUREMAPENABLE, 0); // los registros persisten entre listas: el juego fija su textura
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  setFb(0x000000, 512);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1);
  u32 *ta = cmd(GE_CMD_TEXADDR0, 0x1fe000), *tw = cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 64);
  cmd(GE_CMD_TEXSIZE0, 0x0606); cmd(GE_CMD_TEXFORMAT, 0);
  u32 *prim = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  end();
  run();
  CHECK(rt_count == 1, "rt_count=%d", rt_count);
  CHECK((*fp & 0xffffff) == (RT_BASE & 0xffffff) && (*fw & 0xffff) == 64 * RT_SCALE, "fb no redirigido: %06lx %06lx", *fp & 0xffffff, *fw & 0xffffff);
  CHECK(*prim != NOP_OMIT, "el draw que usa el render target se omitio");
  CHECK((*ta & 0xffffff) == (RT_BASE & 0xffffff) && ((*tw >> 16) & 0xff) == 0x04, "textura no redirigida: %06lx %06lx", *ta & 0xffffff, *tw & 0xffffff);
  run(); // reejecutar la lista da el mismo resultado
  CHECK((*ta & 0xffffff) == (RT_BASE & 0xffffff) && *prim != NOP_OMIT, "segunda pasada distinta");
}

// Regresion dev8: una textura normal que cae dentro del rango de un render target no se omite
static void test_textura_dentro_de_rt(void) {
  reset();
  rt_enabled = 1;
  begin();
  setFb(0x195140, 256); // render target de Ridge Racer que comparte zona con texturas
  setFb(0x000000, 512);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1);
  u32 *ta = cmd(GE_CMD_TEXADDR0, 0x19a040);
  cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 256); cmd(GE_CMD_TEXSIZE0, 0x0808);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
  u32 *prim = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  end();
  run();
  CHECK(*prim != NOP_OMIT, "textura de la pista omitida por caer dentro de un render target");
  CHECK((*ta & 0xffffff) == 0x19a040, "textura normal redirigida: %06lx", *ta & 0xffffff);
}

// Un render target al que se le omitio un draw no se usa como textura (contenido a medias)
static void test_rt_incompleto(void) {
  reset();
  rt_enabled = 1;
  begin();
  setFb(0x000000, 512);
  setFb(0x088000, 256);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1);
  cmd(GE_CMD_TEXADDR0, 0x044000); cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 512); // lee la pantalla anterior
  setFb(0x044000, 512); setFb(0x088000, 256); // (registra 0a044000 como framebuffer)
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3); // se omite -> rt incompleto
  setFb(0x000000, 512);
  cmd(GE_CMD_TEXADDR0, 0x088000); cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 256); cmd(GE_CMD_TEXSIZE0, 0x0808);
  u32 *prim = cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  run();
  CHECK(*prim == NOP_OMIT, "se uso un render target incompleto");
}

// Dentro de un render target a escala 1 no se escalan viewport, scissor ni vertices 2D
static void test_rt_escala_1(void) {
  reset();
  rt_enabled = 1;
  short *v = (short *)VERTS;
  v[0] = 0; v[3] = 64;
  begin();
  cmd(GE_CMD_OFFSETX, (2048 - 240) << 4); cmd(GE_CMD_OFFSETY, (2048 - 136) << 4);
  setFb(0x1fe000, 64);
  u32 *xs = cmd(GE_CMD_VIEWPORTXSCALE, f24(32)), *xc = cmd(GE_CMD_VIEWPORTXCENTER, f24(2048 - 240 + 32));
  u32 *s2 = cmd(GE_CMD_SCISSOR2, (63 << 10) | 63);
  cmd(GE_CMD_BASE, 0x080000);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  run();
  if (RT_SCALE == 1) {
    CHECK(fcmd(xs) == 32, "escala en rt %f", fcmd(xs));
    CHECK(fcmd(xc) == 2048 - 480 + 32, "centro en rt %f (debe quedar a 32 px del offset fijo)", fcmd(xc));
    CHECK((*s2 & 0x3ff) == 63, "scissor en rt %ld", *s2 & 0x3ff);
    CHECK(v[3] == 64, "vertice 2D en rt duplicado: %d", v[3]);
  }
}

// Un framebuffer con pitch 0 (al iniciar) no es un render target
static void test_pitch_0(void) {
  reset();
  rt_enabled = 1;
  begin();
  setFb(0x000000, 0);
  end();
  run();
  CHECK(rt_count == 0 && state.ignore_framebuf, "pitch 0 tratado como render target");
}

// Direcciones invalidas y listas en bucle no deben colgar ni leer fuera de memoria
static void test_robustez(void) {
  reset();
  begin();
  cmd(GE_CMD_CALL, 0x001000); // BASE 0 -> 0x00001000, fuera de memoria
  end();
  run();
  CHECK(logHas("CALL a direccion invalida"), "no se registro el CALL invalido");
  begin();
  cmd(GE_CMD_JUMP, LIST & 0xffffff); // salta a si misma para siempre
  cmd(GE_CMD_BASE, 0);
  state.base = 0x08000000;
  resetGeState();
  patchGeList((u32 *)LIST, NULL);
  CHECK(logHas("bucle"), "la lista en bucle no se corto");
}

// La copia al buffer de pantalla alterna dos listas del GE (antes una sola se reescribia
// mientras el GE podia estar leyendola)
static u32 last_enqueued[4]; static int n_enqueued;
static int fakeEnqueue(const void *l, void *s, int c, PspGeListArgs *a) { last_enqueued[n_enqueued++ & 3] = (u32)l; return 0; }
static int fakeSetFrameBuf(void *t, int w, int f, int s) { return 0; }

static void test_copia_listas_alternas(void) {
  reset();
  _sceGeListEnQueue = fakeEnqueue;
  n_enqueued = 0;
  copyFrameBuffer(0); copyFrameBuffer(0); copyFrameBuffer(0);
  CHECK(last_enqueued[0] != last_enqueued[1] && last_enqueued[0] == last_enqueued[2], "listas de copia: %08lx %08lx %08lx",
        last_enqueued[0], last_enqueued[1], last_enqueued[2]);
}

// Estadisticas de rendimiento: 600 frames a 60 fps deben reportar ~60 fps
extern unsigned int test_time;
static void test_fps(void) {
  reset();
  _sceGeListEnQueue = fakeEnqueue;
  _sceDisplaySetFrameBuf = fakeSetFrameBuf;
  memset(&st, 0, sizeof(st));
  log_frame = 0; last_flip = 0; test_time = 1000;
  int i;
  for (i = 0; i < STATS_EVERY; i++) {
    test_time += 16667;
    sceDisplaySetFrameBufPatched(NULL, 512, 3, 1);
  }
  CHECK(logHas("fps=59.9") || logHas("fps=60.0"), "fps mal calculado: %s", strstr(test_log, "fps=") ? strstr(test_log, "fps=") : "(sin linea)");
  CHECK(logHas("bajo60=0 bajo30=0"), "frames lentos mal contados");
}

// Doble buffer: la misma posicion de la lista alterna FBPTR 0x044000 / 0x000000 cada frame.
// El buffer real del plugin tambien es 0x000000: la memoria de parches no debe confundir
// el 0x000000 que escribe el juego con su propio parche del frame anterior.
static void test_doble_buffer_alternado(void) {
  reset();
  u32 ptrs[4] = { 0x044000, 0x000000, 0x044000, 0x000000 };
  int i;
  for (i = 0; i < 4; i++) {
    begin();
    setFb(ptrs[i], 512);
    end();
    run();
    CHECK(state.framebuf == (FAKE_VRAM | ptrs[i]), "frame %d: framebuffer %08lx, el juego uso %08lx", i, state.framebuf, FAKE_VRAM | ptrs[i]);
  }
}

// Lista estatica (el juego no la reescribe): FBPTR quedo parcheado a 0x000000 y su FBWIDTH
// tambien; ahi si hay que confiar en la memoria de parches y recordar el 0x044000 original
static void test_lista_estatica_fb(void) {
  reset();
  begin();
  setFb(0x044000, 512);
  end();
  run(); run(); run();
  CHECK(state.framebuf == (FAKE_VRAM | 0x044000), "lista reutilizada: framebuffer %08lx", state.framebuf);
}

// Un draw anulado no debe "revivir" si el juego luego escribe un NOP real en esa posicion
static void test_nop_no_revive(void) {
  reset();
  begin();
  setFb(0x000000, 512);
  setFb(0x088000, 512);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1);
  cmd(GE_CMD_TEXADDR0, 0x000000); cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 512);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
  u32 *prim = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3); // lee otro fb: se anula
  end();
  run();
  CHECK((*prim >> 24) == GE_CMD_NOP, "el draw no se anulo");
  // Siguiente frame: el juego arma otra lista en la misma memoria; en esa posicion
  // ahora hay un NOP de verdad y la textura es normal (el draw ya no se anularia)
  begin();
  setFb(0x000000, 512);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1);
  cmd(GE_CMD_TEXADDR0, 0x100000); cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 64);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
  while (lp < prim) cmd(GE_CMD_NOP, 0);
  cmd(GE_CMD_NOP, 0);
  end();
  run();
  CHECK(*prim == 0, "un NOP del juego revivio el draw anulado antes: %08lx", *prim);
}

// Modo pasivo: recorre y registra igual, pero no modifica ni un byte de la lista ni de los vertices
static void test_pasivo_no_escribe(void) {
  reset();
  passive = 1;
  short *v = (short *)VERTS;
  v[0] = 10; v[3] = 20;
  begin();
  cmd(GE_CMD_BASE, 0x080000);
  setFb(0x000000, 512);
  setFb(0x1fe000, 64);
  cmd(GE_CMD_VIEWPORTXSCALE, f24(240)); cmd(GE_CMD_SCISSOR2, (271 << 10) | 479);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  static u32 copy[64];
  int n = lp - (u32 *)LIST;
  memcpy(copy, (void *)LIST, n * 4);
  run();
  CHECK(memcmp(copy, (void *)LIST, n * 4) == 0, "el modo pasivo modifico la lista");
  CHECK(v[0] == 10 && v[3] == 20, "el modo pasivo modifico vertices: %d %d", v[0], v[3]);
  CHECK(logHas("fb 0a1fe000 pitch=64 (activo: seria render target)"), "el log pasivo no indica que haria el plugin");
}

// En pasivo las direcciones de VRAM real (0x04...) se registran como VRAM falsa (0x0A...)
static void test_pasivo_normaliza(void) {
  reset();
  passive = 1;
  begin();
  setFb(0x000000, 512);
  setFb(0x044000, 512);
  setFb(0x000000, 512);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1);
  cmd(GE_CMD_TEXADDR0, 0x044000); cmd(GE_CMD_TEXBUFWIDTH0, (0x04 << 16) | 512); // VRAM real
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  end();
  run();
  CHECK(logHas("textura desde fb 0a044000"), "en pasivo no se reconocio la textura desde fb en VRAM real");
}

// Desplazamiento X (Silent Hill: buffer de 1024 que se muestra desde x=480)
static void test_desplazamiento_y_1024(void) {
  reset();
  opt.wide1024 = 1; opt.dx = 480;
  short *v = (short *)VERTS;
  v[0] = 480; v[1] = 0; v[3] = 960; v[4] = 272;
  begin();
  cmd(GE_CMD_OFFSETX, (2048 - 512) << 4); cmd(GE_CMD_OFFSETY, (2048 - 136) << 4);
  u32 *fw = (setFb(0x0d4000, 1024), lp - 1);
  u32 *xc = cmd(GE_CMD_VIEWPORTXCENTER, f24(2048 - 512 + 480 + 240));
  u32 *s1 = cmd(GE_CMD_SCISSOR1, 480);
  cmd(GE_CMD_BASE, 0x080000);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  run();
  CHECK(rt_count == 0 && !state.ignore_framebuf && (*fw & 0xffff) == 960, "fb de 1024 no tratado como pantalla");
  CHECK(fcmd(xc) == 2048, "centro del viewport con desplazamiento: %f", fcmd(xc));
  CHECK((*s1 & 0x3ff) == 0, "scissor con desplazamiento: %ld", *s1 & 0x3ff);
  CHECK(v[0] == 0 && v[3] == 960, "vertices 2D con desplazamiento: %d %d", v[0], v[3]);
}

// Muchos buffers 2D estaticos (Gran Turismo): con 512 entradas en la memoria de vertices
// dos buffers a 2 KB de distancia chocaban y el expulsado se duplicaba otra vez cada frame
static void test_vertices_2d_muchos_buffers(void) {
  reset();
  int i, k, bad = 0;
  begin();
  cmd(GE_CMD_BASE, 0x080000);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  for (k = 0; k < 16; k++) { // 16 buffers separados por 2 KB (misma entrada con 512)
    short *v = (short *)(VERTS + k * 2048);
    v[0] = 0; v[1] = 0; v[2] = 0; v[3] = 64; v[4] = 32; v[5] = 0;
    cmd(GE_CMD_VADDR, (VERTS + k * 2048) & 0xffffff);
    cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  }
  end();
  for (i = 0; i < 5; i++)
    run();
  for (k = 0; k < 16; k++) {
    short *v = (short *)(VERTS + k * 2048);
    bad += v[3] != 128 || v[4] != 64;
  }
  CHECK(bad == 0, "%d de 16 buffers 2D se duplicaron mas de una vez", bad);
  CHECK(st.vevict == 0, "choques en la memoria de vertices: %lu", st.vevict);
  opt.v2d = 0;
  short *v = (short *)VERTS; v[3] = 64; run();
  CHECK(v[3] == 64, "con 'Duplicar vertices 2D = no' se duplicaron igual");
}

// Pasivo: solo se recorre 1 de cada PASSIVE_SAMPLE frames (para no frenar al juego)
static void test_pasivo_muestreo(void) {
  reset();
  passive = 1;
  begin();
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  end();
  int i;
  memset(&st, 0, sizeof(st));
  for (i = 0; i < PASSIVE_SAMPLE * 4; i++) {
    sceGeListEnQueuePatched((void *)LIST, NULL, 0, NULL);
    sceDisplaySetFrameBufPatched((void *)0x04000000, 512, 3, 0);
  }
  CHECK(st.draws >= 3 && st.draws <= 5, "en pasivo se recorrieron %lu de %d frames", st.draws, PASSIVE_SAMPLE * 4);
}

// Silent Hill: el 3D se dibuja en un fb de 512 (8888) y cada frame se pasa con un sprite
// (textura con la direccion de la VRAM real, 0x0404c000) a una mitad de un fb de 1024 que
// se muestra alternando x=0 y x=480. El video se copia (TRANSFER) al fb de 512.
// dev16 trataba el de 1024 como pantalla: el sprite leia el buffer grande y lo dibujaba
// encima de si mismo (glitches); el video seguia en blanco porque se escalaba lo mostrado.
static void test_silent_hill_video(void) {
  reset();
  rt_enabled = 1;
  _sceGeListEnQueue = fakeEnqueue;
  _sceDisplaySetFrameBuf = fakeSetFrameBuf;
  u32 *video = (u32 *)0x0A04C000; // el video ya copiado por el GE (8888, ancho 512)
  int i;
  for (i = 0; i < 512 * 272; i++) video[i] = 0x00ff0000; // azul
  begin();
  setFb(0x04c000, 512);
  cmd(GE_CMD_TRANSFERDST, 0x04c000); cmd(GE_CMD_TRANSFERDSTW, (0x0a << 16) | 512);
  cmd(GE_CMD_TRANSFERSIZE, (271 << 10) | 479); cmd(GE_CMD_TRANSFERSTART, 1);
  u32 *fw = (setFb(0x0d4000, 1024), lp - 1);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1);
  cmd(GE_CMD_TEXADDR0, 0x04c000); cmd(GE_CMD_TEXBUFWIDTH0, (0x04 << 16) | 512); // VRAM real
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  u32 *prim = cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  memset(&st, 0, sizeof(st));
  run();
  CHECK(rt_count == 1 && (*fw & 0xffff) != 960, "el fb de 1024 deberia ser render target (rts %d)", rt_count);
  CHECK(logHas("textura en VRAM real 0404c000"), "no se registro la textura en VRAM real");
  CHECK(logHas("textura desde fb 0a04c000"), "la textura 0x0404c000 no se reconocio como el fb 0a04c000");
  (void)prim;
  sceDisplaySetFrameBufPatched((void *)0x0A0D43C0, 1024, 0, 0);
  CHECK(st.fallback == 1, "no se uso la imagen original escalada");
  CHECK(logHas("imagen original escalada desde 0a04c000 ancho=512 fmt=3"), "se escalo otra cosa que el video copiado");
  u16 px = ((u16 *)DISPLAY_BUFFER)[100 * 960 + 100];
  CHECK(px == to565(0x00ff0000, 3) && px, "pixel del video en pantalla: %04x", px);
}

// Copia grande a la pantalla (video de Silent Hill + "PRESS START" dibujado encima): la
// copia se reemplaza por un CALL a una lista que la hace y la dibuja al doble en su lugar
static void test_copia_escalada_con_ge(void) {
  reset();
  u32 *v = (u32 *)0x09000000;
  begin();
  cmd(GE_CMD_BASE, 0x090000);
  setFb(0x04c000, 512);
  cmd(GE_CMD_VADDR, 0x123400); // VADDR del juego: debe quedar igual despues de la copia
  u32 *p = lp;
  cmd(GE_CMD_TRANSFERSRC, (u32)v & 0xffffff); cmd(GE_CMD_TRANSFERSRCW, (0x09 << 16) | 512); cmd(GE_CMD_TRANSFERSRCPOS, 0);
  cmd(GE_CMD_TRANSFERDST, 0x04c000); cmd(GE_CMD_TRANSFERDSTW, (0x0a << 16) | 512); cmd(GE_CMD_TRANSFERDSTPOS, 0);
  cmd(GE_CMD_TRANSFERSIZE, (271 << 10) | 479);
  u32 *start = cmd(GE_CMD_TRANSFERSTART, 1);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT); // texto encima
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  int pass;
  for (pass = 0; pass < 2; pass++) { // la misma lista dos veces (listas estaticas)
    memset(&st, 0, sizeof(st));
    big_transfer = 0;
    run();
    CHECK(st.tx_ge == 1 && !big_transfer, "pasada %d: la copia no se escalo con el GE (%lu)", pass, st.tx_ge);
    CHECK(p[0] == ((GE_CMD_BASE << 24) | 0x0a0000) && (p[1] >> 24) == GE_CMD_CALL && *start == NOP_MARK,
          "pasada %d: la copia no se reescribio como BASE+CALL: %08lx %08lx %08lx", pass, p[0], p[1], *start);
    CHECK(state.base == 0x09000000, "pasada %d: el recorrido tomo el BASE del plugin (%08lx)", pass, state.base);
    u32 *sub = (u32 *)(0x0A000000 | (p[1] & 0xffffff));
    int k, prim = 0, ret = 0, copy = 0, vaddr = 0;
    for (k = 0; k < 120 && !ret; k++) {
      prim |= (sub[k] >> 24) == GE_CMD_PRIM;
      ret |= (sub[k] >> 24) == GE_CMD_RET;
      copy |= sub[k] == ((GE_CMD_TRANSFERSTART << 24) | 1);
      vaddr |= sub[k] == ((GE_CMD_VADDR << 24) | 0x123400) && sub[k - 1] == ((GE_CMD_BASE << 24) | 0x090000);
    }
    CHECK(prim && ret && copy && vaddr, "la lista del plugin: copia=%d draw=%d RET=%d VADDR del juego=%d", copy, prim, ret, vaddr);
    short *q = (short *)((u32)sub + 448);
    CHECK(q[7] == 960 && q[8] == 544, "el draw de la copia no es al doble: %d,%d", q[7], q[8]);
  }
}

// Copia reducida del frame al fb del juego (Gran Turismo lee el frame anterior como textura)
static void test_copia_reducida(void) {
  reset();
  rt_enabled = 1;
  _sceGeListEnQueue = fakeEnqueue;
  _sceDisplaySetFrameBuf = fakeSetFrameBuf;
  opt.omit_fbtex = 2;
  begin();
  setFb(0x000000, 512); // doble buffer: dibuja en 0x044000 leyendo el frame anterior
  setFb(0x044000, 512);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1);
  cmd(GE_CMD_TEXADDR0, 0x000000); cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 512);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
  u32 *prim = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  end();
  memset(&st, 0, sizeof(st));
  run();
  CHECK((*prim >> 24) == GE_CMD_PRIM, "con 'copia reducida' el draw no deberia omitirse");
  n_enqueued = 0;
  sceDisplaySetFrameBufPatched((void *)0x0A044000, 512, 3, 0);
  CHECK(st.fb_copies == 1 && copy_vram >= RT_BASE, "no se hizo la copia reducida (%lu)", st.fb_copies);
  u32 *l = (u32 *)last_enqueued[0];
  int k, ok = 0;
  for (k = 0; k < 100 && (l[k] >> 24) != GE_CMD_FINISH; k++)
    ok |= l[k] == ((GE_CMD_TRANSFERDST << 24) | 0x044000);
  CHECK(ok, "la lista de copia no escribe en el fb mostrado 0a044000");
  memSnapshot("test");
  CHECK(logHas("memoria (test): vram del juego usada hasta"), "no se registro la memoria");
}

// Menu: abrir con L+R+START, cambiar una opcion, cerrar, y que el juego no vea botones mientras
extern unsigned int test_buttons;
static void test_menu(void) {
  reset();
  test_buttons = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_START;
  menuFrame();
  CHECK(menuOpen(), "el menu no se abrio");
  test_buttons = 0; menuFrame();
  test_buttons = PSP_CTRL_RIGHT; menuFrame(); // primera opcion: render targets si -> no
  CHECK(opt.rt_on == 0 && logHas("menu: Render targets en VRAM alta = no"), "no se cambio la opcion (rt_on=%d)", opt.rt_on);
  CHECK(logHas("rt: se liberaron"), "cambiar render targets no los libero");
  test_buttons = 0; menuFrame();
  test_buttons = PSP_CTRL_DOWN; menuFrame(); test_buttons = 0; menuFrame();
  test_buttons = PSP_CTRL_RIGHT; menuFrame();
  CHECK(opt.rt_scale == 2, "escala de render targets: %d", opt.rt_scale);
  int dibujado = 0, i;
  for (i = 0; i < MENU_W * MENU_H; i++) dibujado += ((u16 *)MENU_BUF)[i] != 0;
  CHECK(dibujado > 1000, "el menu no se dibujo (%d pixeles)", dibujado);
  test_buttons = PSP_CTRL_CIRCLE; menuFrame();
  CHECK(menuOpen(), "al cerrar con O el juego no deberia ver el boton hasta soltarlo");
  test_buttons = 0; menuFrame();
  CHECK(!menuOpen(), "el menu no se cerro");
}

int main(void) {
  mapPspMemory();
  void (*tests[])(void) = {
    test_viewport_scissor_x2, test_viewport_zona, test_memo_lista_grande, test_vertices_2d,
    test_vaddr_avanza_tras_3d, test_bjump_no_salta, test_estado_entre_listas, test_textura_fb_al_dibujar,
    test_render_target, test_textura_dentro_de_rt, test_rt_incompleto, test_rt_escala_1, test_pitch_0,
    test_robustez, test_copia_listas_alternas, test_fps, test_doble_buffer_alternado, test_nop_no_revive, test_lista_estatica_fb, test_pasivo_no_escribe, test_pasivo_normaliza, test_desplazamiento_y_1024, test_vertices_2d_muchos_buffers, test_pasivo_muestreo, test_silent_hill_video, test_copia_escalada_con_ge, test_copia_reducida, test_menu,
  };
  int i;
  for (i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++)
    tests[i]();
  printf("%d comprobaciones, %d fallas\n", checks, fails);
  return fails ? 1 : 0;
}
