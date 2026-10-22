// Tests del plugin en el PC. Se incluye main.c entero para poder revisar su estado
// interno. Cada test arma una display list en la memoria simulada de la PSP, la pasa
// por patchGeList como lo haria el GE y revisa como quedaron los comandos y vertices.
// Muchos tests corresponden a un bug que ya se arreglo: si alguno falla, volvio.
#include <stdlib.h>
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
  vram_swz = 0; depth_vram = 0x04100000; rt1_pending = z_used = tx_small = 0; ge_fbs = 0;
  memset(rt, 0, sizeof(rt));
  rt_count = 0; rt_next = RT_BASE; rt_hi_next = RT_HI_BASE; rt_enabled = 0;
  tex_patched_any = 0; rt_gen++; memset(&tcache, 0, sizeof(tcache)); tcache.res = 2; // sin cache valida
  game_offx = 2048 - 240; game_offy = 2048 - 136;
  memset((void *)LIST, 0, 0x100000);
  memset((void *)VERTS, 0, 0x10000);
  test_log_len = 0; test_log[0] = 0;
  logResetOnce();
  opt = opt_default;
  opt.autocap = 0; cap_request = 0; capturing = 0; // las capturas automaticas cambian el recorrido
  passive = 0; walk_now = 1;
  big_transfer = 0; fbtex_seen = 0; vram_hi = 0; copy_vram = 0; tx_patched_any = 0;
  memset(wc, 0, WC_SIZE * sizeof(WalkCache)); memset(recs, 0, sizeof(recs)); nrec = 0;
  swap_fb = -1; swap_on = swap_done = swap_flips = 0;
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
  rt_enabled = 1;
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
    CHECK(start[-1] == ((GE_CMD_BASE << 24) | 0x0a0000) && (*start >> 24) == GE_CMD_CALL && p[0] >> 24 == GE_CMD_TRANSFERSRC,
          "pasada %d: la copia no se reescribio como BASE+CALL: %08lx %08lx %08lx", pass, p[0], start[-1], *start);
    CHECK(state.base == 0x09000000, "pasada %d: el recorrido tomo el BASE del plugin (%08lx)", pass, state.base);
    u32 *sub = (u32 *)(0x0A000000 | (*start & 0xffffff));
    int k, prim = 0, ret = 0, copy = 0, vaddr = 0;
    for (k = 0; k < 120 && !ret; k++) {
      prim |= (sub[k] >> 24) == GE_CMD_PRIM;
      ret |= (sub[k] >> 24) == GE_CMD_RET;
      copy |= sub[k] == ((GE_CMD_TRANSFERSTART << 24) | 1);
      vaddr |= sub[k] == ((GE_CMD_VADDR << 24) | 0x123400) && sub[k - 1] == ((GE_CMD_BASE << 24) | 0x090000);
    }
    CHECK(prim && ret && copy && vaddr, "la lista del plugin: copia=%d draw=%d RET=%d VADDR del juego=%d", copy, prim, ret, vaddr);
    short *q = (short *)((u32)sub + 512) + 14 * 10; // ultima de 15 franjas de 32 texeles
    CHECK(q[5] == 480 && q[7] == 960 && q[8] == 544, "el draw de la copia no es al doble: %d -> %d,%d", q[5], q[7], q[8]);
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

// Sublistas sin cambios: la segunda vez no se recorren y el estado queda igual que al recorrerlas
static u32 *sub(u32 at, int through, u32 tex) {
  u32 *save = lp, *s = (u32 *)at;
  lp = s;
  cmd(GE_CMD_VERTEXTYPE, (through ? GE_VTYPE_THROUGH : 0) | GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1);
  cmd(GE_CMD_TEXADDR0, tex & 0xffffff); cmd(GE_CMD_TEXBUFWIDTH0, ((tex >> 8) & 0x0f0000) | 64);
  cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  cmd(GE_CMD_RET, 0);
  lp = save;
  return s;
}
static void callList(u32 at, int n) {
  int i;
  begin();
  setFb(0x044000, 512);
  setFb(0x000000, 512);
  cmd(GE_CMD_BASE, 0x080000);
  for (i = 0; i < n; i++)
    cmd(GE_CMD_CALL, at & 0xffffff);
  end();
}
static void test_sublistas_sin_cambios(void) {
  reset();
  u32 at = LIST + 0x8000;
  u32 *s = sub(at, 0, 0x09100000);
  callList(at, 3);
  memset(&st, 0, sizeof(st));
  run();
  CHECK(st.wc_hits == 2 && st.draws == 6, "esperaba 2 saltadas y 6 draws: %lu %lu", st.wc_hits, st.draws);
  u32 va = state.vertex_addr, ta = state.ge_cmd[GE_CMD_TEXADDR0], vt = state.vertex_type;
  opt.walk_cache = 0; memset(&st, 0, sizeof(st));
  run();
  CHECK(st.wc_hits == 0 && st.draws == 6, "sin la opcion se saltaron %lu", st.wc_hits);
  CHECK(state.vertex_addr == va && state.ge_cmd[GE_CMD_TEXADDR0] == ta && state.vertex_type == vt,
        "el estado tras saltar difiere del recorrido: %08lx/%08lx %06lx/%06lx", va, state.vertex_addr, ta, state.ge_cmd[GE_CMD_TEXADDR0]);
  opt.walk_cache = 1; memset(&st, 0, sizeof(st));
  s[5] = (GE_CMD_PRIM << 24) | (GE_PRIM_TRIANGLES << 16) | 6; // el juego cambio la sublista (otro frame)
  log_frame++;
  run();
  CHECK(st.wc_miss == 1 && st.wc_hits == 2, "sublista cambiada: miss=%lu hits=%lu", st.wc_miss, st.wc_hits);
  CHECK(state.vertex_addr == VERTS + 9 * 6, "VADDR tras la sublista cambiada: %08lx", state.vertex_addr);

  // Con un draw 2D o una textura desde fb hay que recorrerla siempre
  reset();
  sub(at, 1, 0x09100000); callList(at, 3);
  memset(&st, 0, sizeof(st)); run();
  CHECK(st.wc_hits == 0, "una sublista con draws 2D se salto");
  reset();
  sub(at, 0, 0x0A044000); callList(at, 3);
  memset(&st, 0, sizeof(st)); run();
  CHECK(st.wc_hits == 0 && st.skipped == 6, "sublista con textura desde fb: hits=%lu omitidos=%lu", st.wc_hits, st.skipped);
}

// Silent Hill: formulario dibujado en un fb de 512 que nunca se muestra (0x170000) y que
// luego se pega en la pantalla (0x04c000, que a su vez se copia al fb mostrado de 1024).
// Ambos iban al mismo buffer grande y el formulario quedaba vacio.
static void test_fb_intermedio(void) {
  reset();
  rt_enabled = 1;
  opt.layers = 1;
  opt.autocap = 1; // de paso: la deteccion pide una captura automatica
  _sceGeListEnQueue = fakeEnqueue;
  _sceDisplaySetFrameBuf = fakeSetFrameBuf;
  int frame;
  u32 *w170 = NULL, *w4c = NULL;
  for (frame = 0; frame < 3; frame++) {
    begin();
    cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
    w170 = (setFb(0x170000, 512), lp - 1);               // formulario
    cmd(GE_CMD_TEXTUREMAPENABLE, 0);
    cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
    w4c = (setFb(0x04c000, 512), lp - 1);                // pantalla: pega el formulario
    cmd(GE_CMD_TEXTUREMAPENABLE, 1);
    cmd(GE_CMD_TEXADDR0, 0x170000); cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 512);
    cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
    setFb(0x0d4000, 1024);                               // copia al fb mostrado
    cmd(GE_CMD_TEXADDR0, 0x04c000); cmd(GE_CMD_TEXBUFWIDTH0, (0x04 << 16) | 512);
    cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
    end();
    run();
    u16 *sp = (u16 *)0x0A0D43C0; // spinner de la CPU: 20x20 en (440,233) de la mitad mostrada
    int yy, xx;
    for (yy = 233; yy < 253; yy++)
      for (xx = 440; xx < 460; xx++)
        sp[yy * 1024 + xx] = 0xffff;
    sceDisplaySetFrameBufPatched((void *)0x0A0D43C0, 1024, 0, 0);
    CHECK(sp[240 * 1024 + 450] == 0, "frame %d: el dibujo de la CPU no se borro del fb mostrado", frame);
  }
  CHECK(logHas("fb 0a170000: intermedio"), "el fb del formulario no se detecto como intermedio");
  CHECK(!logHas("fb 0a04c000: intermedio"), "la pantalla se tomo como intermedia");
  CHECK((*w170 & 0xff0000) == 0x040000 && (*w170 & 0xffff) == 512, "el formulario no va a un render target: %08lx", *w170);
  CHECK((*w4c & 0xffff) == 960, "la pantalla dejo de ir al buffer grande: %08lx", *w4c);
  CHECK(logHas("captura automatica 1/5") && logHas("== captura frame"), "no hubo captura automatica");
  capturing = 0;
}

// Opciones por juego: archivo clave=valor; valores fuera de rango o claves raras se ignoran
static void test_opciones_juego(void) {
  reset();
  int n = menuParse("tex_fb=2\nsublistas=0\r\ndx=480\nrt_escala=9\nnada=1\n");
  CHECK(n == 3 && opt.omit_fbtex == 2 && opt.walk_cache == 0 && opt.dx == 480 && opt.rt_scale == 1,
        "menuParse: n=%d tex_fb=%d sublistas=%d dx=%d rt_escala=%d", n, opt.omit_fbtex, opt.walk_cache, opt.dx, opt.rt_scale);
  opt = opt_default;
}

// Sublista con otra dentro (Gran Turismo): se guarda si la de dentro tambien, y deja de
// saltarse si la de dentro cambia
static void test_sublistas_anidadas(void) {
  reset();
  u32 child = LIST + 0x9000, parent = LIST + 0x8000;
  u32 *c = sub(child, 0, 0x09100000);
  u32 *save = lp;
  lp = (u32 *)parent;
  cmd(GE_CMD_BASE, 0x080000);
  cmd(GE_CMD_CALL, child & 0xffffff);
  cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  cmd(GE_CMD_RET, 0);
  lp = save;
  callList(parent, 3);
  memset(&st, 0, sizeof(st));
  run();
  CHECK(st.wc_hits == 2 && st.draws == 9, "anidadas: esperaba 2 saltadas y 9 draws: %lu %lu", st.wc_hits, st.draws);
  u32 va = state.vertex_addr;
  c[5] = (GE_CMD_PRIM << 24) | (GE_PRIM_TRIANGLES << 16) | 6; // cambia la de dentro (otro frame)
  log_frame++;
  memset(&st, 0, sizeof(st));
  run();
  CHECK(st.wc_miss >= 1 && st.draws == 9 && state.vertex_addr == va + 3 * 6, "la de dentro cambio y se salto igual: miss=%lu va=%08lx/%08lx",
        st.wc_miss, state.vertex_addr, va + 18);
}

// Silent Hill usa la VRAM real fija (0x04...). Texturas y copias que no son fbs se llevan a
// la VRAM falsa; una copia desde la pantalla (formulario del celular) se toma del buffer
// grande reducido, porque su fb en la VRAM falsa esta vacio.
static void test_vram_real(void) {
  reset();
  rt_enabled = 1;
  begin();
  setFb(0x04c000, 512);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1);
  cmd(GE_CMD_TEXADDR0, 0x190000); u32 *tbw = cmd(GE_CMD_TEXBUFWIDTH0, (0x04 << 16) | 256);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  // copia de una textura a la VRAM real
  cmd(GE_CMD_TRANSFERSRC, 0x100000); cmd(GE_CMD_TRANSFERSRCW, (0x09 << 16) | 256); cmd(GE_CMD_TRANSFERSRCPOS, 0);
  cmd(GE_CMD_TRANSFERDST, 0x1a0000); u32 *dstw = cmd(GE_CMD_TRANSFERDSTW, (0x04 << 16) | 256); cmd(GE_CMD_TRANSFERDSTPOS, 0);
  cmd(GE_CMD_TRANSFERSIZE, (63 << 10) | 63); cmd(GE_CMD_TRANSFERSTART, 1);
  // copia desde la pantalla (256x256 en 0,0) a una textura
  u32 *p = lp;
  cmd(GE_CMD_TRANSFERSRC, 0x04c000); cmd(GE_CMD_TRANSFERSRCW, (0x04 << 16) | 512); cmd(GE_CMD_TRANSFERSRCPOS, 0);
  cmd(GE_CMD_TRANSFERDST, 0x190000); cmd(GE_CMD_TRANSFERDSTW, (0x04 << 16) | 256); cmd(GE_CMD_TRANSFERDSTPOS, 0);
  cmd(GE_CMD_TRANSFERSIZE, (255 << 10) | 255); cmd(GE_CMD_TRANSFERSTART, 1);
  end();
  memset(&st, 0, sizeof(st));
  run();
  CHECK((*tbw & 0xff0000) == 0x0a0000 && (*tbw & 0xffff) == 256, "la textura en VRAM real no se llevo a la falsa: %08lx", *tbw);
  CHECK((*dstw & 0xff0000) == 0x0a0000, "la copia a la VRAM real no se llevo a la falsa: %08lx", *dstw);
  CHECK(st.tx_screen == 1 && (p[7] >> 24) == GE_CMD_CALL, "la copia desde la pantalla no se reemplazo (%lu)", st.tx_screen);
  u32 *sub = (u32 *)(0x0A000000 | (p[7] & 0xffffff));
  int k, src_tmp = 0, dst_ok = 0, ret = 0;
  for (k = 0; k < 200 && !ret; k++) {
    src_tmp |= sub[k] == ((GE_CMD_TRANSFERSRC << 24) | (copy_vram & 0xffffff));
    dst_ok |= sub[k] == ((GE_CMD_TRANSFERDSTW << 24) | (0x0a << 16) | 256);
    ret |= (sub[k] >> 24) == GE_CMD_RET;
  }
  CHECK(src_tmp && dst_ok && ret, "lista de la copia desde la pantalla: desde temporal=%d destino falso=%d RET=%d", src_tmp, dst_ok, ret);
}

// Silent Hill (log dev23): copia del formulario desde la pantalla a la RAM, con los
// registros en otro orden que sceGuCopyImage (antes no se reconocia la copia)
static void test_copia_formulario(void) {
  reset();
  rt_enabled = 1;
  begin();
  setFb(0x000000, 480); // otro fb justo antes: la copia es del de 0x04c000, no de este
  setFb(0x04c000, 512);
  cmd(GE_CMD_TRANSFERDST, 0x7f6960); cmd(GE_CMD_TRANSFERDSTW, (0x09 << 16) | 256);
  cmd(GE_CMD_TRANSFERSRC, 0x04c000); cmd(GE_CMD_TRANSFERSRCW, (0x0a << 16) | 512);
  cmd(GE_CMD_TRANSFERSIZE, (255 << 10) | 255); cmd(GE_CMD_TRANSFERSRCPOS, 0); u32 *reg = cmd(GE_CMD_TRANSFERDSTPOS, 0);
  cmd(GE_CMD_TEXFLUSH, 0); cmd(GE_CMD_NOP, 0); // otros comandos antes del TRANSFERSTART
  u32 *start = cmd(GE_CMD_TRANSFERSTART, 1);
  end();
  memset(&st, 0, sizeof(st));
  run();
  CHECK(st.tx_screen == 1 && (*start >> 24) == GE_CMD_CALL && (*reg >> 24) == GE_CMD_BASE, "la copia del formulario no se reemplazo (%lu)", st.tx_screen);
  CHECK(logHas("copia desde la pantalla 256x256 (0,0) -> 097f6960"), "no se registro la copia del formulario");
  // La reduccion escribe el alfa (stencil REPLACE 0xff) y luego lo deja como estaba
  u32 *l = (u32 *)(0x0A000000 | (*start & 0xffffff)), on = 0, rep = 0, back = 0;
  for (; (*l >> 24) != GE_CMD_RET; l++) {
    on |= *l == ((GE_CMD_STENCILTESTENABLE << 24) | 1);
    rep |= *l == ((GE_CMD_STENCILOP << 24) | 0x020202);
    back |= rep && (*l >> 24) == GE_CMD_STENCILOP && *l != ((GE_CMD_STENCILOP << 24) | 0x020202);
  }
  CHECK(on && rep && back, "la reduccion no escribe el alfa o no restaura el stencil (%lu %lu %lu)", on, rep, back);
}

// Gran Turismo alterna dos fb: las sublistas guardadas sirven en ambos
static void test_sublistas_doble_buffer(void) {
  reset();
  u32 at = LIST + 0x8000;
  sub(at, 0, 0x09100000);
  int i;
  for (i = 0; i < 3; i++) {
    begin();
    setFb(i & 1 ? 0x044000 : 0x000000, 512);
    cmd(GE_CMD_BASE, 0x080000);
    cmd(GE_CMD_CALL, at & 0xffffff);
    end();
    memset(&st, 0, sizeof(st));
    run();
  }
  CHECK(st.wc_hits == 1, "con el fb alternado no se salto la sublista (%lu)", st.wc_hits);
}

// Silent Hill (dev26): el formulario se dibujaba en el buffer grande (565, sin alfa) y la
// copia salia sin transparencia y con texto cortado. Desde el segundo frame ese tramo va a
// escala 1 y en el formato del juego al buffer temporal, y la copia se toma de ahi.
static u32 *formFrame(u32 **fmt, u32 **fbp, u32 **start, int copy) {
  begin();
  *fmt = cmd(GE_CMD_FRAMEBUFPIXFORMAT, 3);
  *fbp = lp; setFb(0x04c000, 512);
  cmd(GE_CMD_SCISSOR1, 0); cmd(GE_CMD_SCISSOR2, (255 << 10) | 255);
  cmd(GE_CMD_BASE, 0x080000);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2); // texto del formulario
  if (copy) {
    cmd(GE_CMD_TRANSFERSRC, 0x04c000); cmd(GE_CMD_TRANSFERSRCW, (0x04 << 16) | 512); cmd(GE_CMD_TRANSFERSRCPOS, 0);
    cmd(GE_CMD_TRANSFERDST, 0x7f6960); cmd(GE_CMD_TRANSFERDSTW, (0x09 << 16) | 256); cmd(GE_CMD_TRANSFERDSTPOS, 0);
    cmd(GE_CMD_TRANSFERSIZE, (255 << 10) | 255);
  }
  *start = copy ? cmd(GE_CMD_TRANSFERSTART, 1) : NULL;
  u32 *sc = cmd(GE_CMD_SCISSOR2, (271 << 10) | 479);
  cmd(GE_CMD_VADDR, (VERTS + 0x100) & 0xffffff);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2); // escena
  end();
  return sc;
}
static void test_formulario_escala1(void) {
  reset();
  rt_enabled = 1;
  opt.csrc = 1; // opcion (apagada por defecto desde dev29)
  short *v = (short *)VERTS, *e = (short *)(VERTS + 0x100);
  v[0] = v[1] = v[2] = 0; v[3] = 200; v[4] = 100; v[5] = 0; // x, y, z
  e[0] = e[1] = e[2] = 0; e[3] = 480; e[4] = 272; e[5] = 0;
  u32 *fmt, *fbp, *start, *sc;
  formFrame(&fmt, &fbp, &start, 1);
  memset(&st, 0, sizeof(st));
  run(); // frame 1: se reduce el buffer grande (como antes) y se anota el fb
  CHECK(st.tx_screen == 1 && v[3] == 400, "frame 1: copia=%lu formulario x=%d", st.tx_screen, v[3]);
  CHECK(logHas("el juego copia desde el"), "no se anoto el fb de copia");
  int f;
  for (f = 0; f < 2; f++) {
    sc = formFrame(&fmt, &fbp, &start, 1);
    run();
    CHECK(*fbp == ((GE_CMD_FRAMEBUFPTR << 24) | (copy_vram & 0xffffff)) && (fbp[1] & 0xffff) == 512,
          "frame %d: el formulario no va al buffer temporal: %08lx %08lx", f + 2, *fbp, fbp[1]);
    CHECK((*fmt & 0xffffff) == 3, "frame %d: formato %06lx (debe quedar el del juego)", f + 2, *fmt & 0xffffff);
    CHECK(v[3] == 200 && v[4] == 100, "frame %d: vertices del formulario %d,%d (escala 1)", f + 2, v[3], v[4]);
    CHECK(e[3] == 960 && e[4] == 544, "frame %d: escena %d,%d (x2)", f + 2, e[3], e[4]);
    CHECK((*sc & 0x3ff) == 959, "frame %d: scissor de la escena %06lx", f + 2, *sc & 0xffffff);
    CHECK((*start >> 24) == GE_CMD_CALL, "frame %d: la copia no se reemplazo", f + 2);
    u32 *sub = (u32 *)(0x0A000000 | (*start & 0xffffff));
    int k, from = 0, big = 0, f565 = 0, ret = 0;
    for (k = 0; k < 200 && !ret; k++) {
      from |= sub[k] == ((GE_CMD_TRANSFERSRC << 24) | (copy_vram & 0xffffff));
      big |= sub[k] == ((GE_CMD_FRAMEBUFPTR << 24) | (VRAM_DRAW_BUFFER_OFFSET & 0xffffff));
      f565 |= sub[k] == ((GE_CMD_FRAMEBUFPIXFORMAT << 24) | PIXELFORMAT);
      ret |= (sub[k] >> 24) == GE_CMD_RET;
    }
    CHECK(from && big && f565 && ret, "frame %d: lista de la copia: desde temporal=%d vuelve al buffer grande=%d 565=%d RET=%d",
          f + 2, from, big, f565, ret);
  }
  CHECK(logHas("se toma del buffer temporal (escala 1)"), "no se registro la copia desde el buffer temporal");
  // El juego deja de copiar: tras 3 listas con draws perdidos se vuelve a la pantalla
  for (f = 0; f < 4; f++) {
    formFrame(&fmt, &fbp, &start, 0);
    run();
  }
  CHECK(logHas("no hubo copia") && *fbp == (GE_CMD_FRAMEBUFPTR << 24) && (*fmt & 0xffffff) == PIXELFORMAT,
        "sin copia no se volvio al buffer grande: %08lx fmt %06lx", *fbp, *fmt & 0xffffff);
}

// Silent Hill: un fb intermedio (render target) que lee la pantalla recien dibujada. Antes
// se omitia y el render target quedaba incompleto (spinner y puerta sin dibujar)
static void test_rt_lee_pantalla(void) {
  reset();
  opt.cpu_draw = 1; // apagado por defecto desde dev38
  memset(&st, 0, sizeof(st));
  rt_enabled = 1;
  opt.layers = 1;
  _sceGeListEnQueue = fakeEnqueue;
  _sceDisplaySetFrameBuf = fakeSetFrameBuf;
  u32 *prim = NULL, *prev = NULL, *use = NULL, *comp = NULL;
  int frame;
  for (frame = 0; frame < 4; frame++) {
    begin();
    setFb(0x04c000, 512);                                 // escena
    cmd(GE_CMD_TEXTUREMAPENABLE, 0);
    cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
    cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
    setFb(0x170000, 512);                                 // fb del spinner: lee la escena
    cmd(GE_CMD_TEXTUREMAPENABLE, 1);
    cmd(GE_CMD_TEXFORMAT, 3);
    cmd(GE_CMD_TEXADDR0, 0x04c000); prev = cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 512);
    prim = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
    setFb(0x04c000, 512);                                 // la pantalla usa el spinner
    cmd(GE_CMD_TEXADDR0, 0x170000); cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 512); cmd(GE_CMD_TEXSIZE0, 0x0909);
    use = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
    setFb(0x0d4000, 1024);
    cmd(GE_CMD_TEXADDR0, 0x04c000); cmd(GE_CMD_TEXBUFWIDTH0, (0x04 << 16) | 512);
    comp = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
    end();
    run();
    u16 *sp = (u16 *)0x0A0D43C0; // spinner de la CPU: 20x20 en (440,233) de la mitad mostrada
    int yy, xx;
    for (yy = 233; yy < 253; yy++)
      for (xx = 440; xx < 460; xx++)
        sp[yy * 1024 + xx] = 0xffff;
    sceDisplaySetFrameBufPatched((void *)0x0A0D43C0, 1024, 0, 0);
    CHECK(sp[240 * 1024 + 450] == 0, "frame %d: el dibujo de la CPU no se borro del fb mostrado", frame);
  }
  CHECK(logHas("fb 0a170000: intermedio"), "el fb del spinner no se detecto como intermedio");
  CHECK((*comp >> 24) != GE_CMD_CALL, "la copia al fb mostrado (1024) tambien reducia la pantalla: trabajo de mas cada frame");
  CHECK(st.cpu_overlays > 0, "no se pego lo que dibuja la CPU en el fb mostrado (spinner)");
  u16 *o = (u16 *)OVL_BUF;
  int q, white = 0;
  for (q = 0; q < 2 * OVL_MAX * 2 * OVL_MAX; q++)
    white += o[q] == 0xffff;
  CHECK(white == 20 * 20 * 4, "el spinner no quedo al doble en OVL_BUF (%d pixeles)", white);
  CHECK((*prim >> 24) == GE_CMD_CALL && (*prev >> 24) == GE_CMD_BASE, "el draw que lee la pantalla no se reemplazo: %08lx", *prim);
  CHECK(*use != NOP_OMIT, "el spinner (render target) no se uso en la pantalla");
  u32 *sub = (u32 *)(0x0A000000 | (*prim & 0xffffff));
  int k, tex = 0, draw = 0, orig = 0, ret = 0;
  for (k = 0; k < 200 && !ret; k++) {
    tex |= sub[k] == ((GE_CMD_TEXADDR0 << 24) | (copy_vram & 0xffffff));
    draw |= sub[k] == ((GE_CMD_PRIM << 24) | (GE_PRIM_TRIANGLES << 16) | 3);
    orig |= sub[k] == ((GE_CMD_TEXBUFWIDTH0 << 24) | (0x0a << 16) | 512);
    ret |= (sub[k] >> 24) == GE_CMD_RET;
  }
  CHECK(tex && draw && orig && ret, "lista del draw: textura temporal=%d draw=%d palabra original=%d RET=%d", tex, draw, orig, ret);
  // Con la opcion apagada, el draw y la palabra de antes vuelven a ser los del juego
  opt.layers = 0; opt_gen++; rt_gen++;
  memset(&tcache, 0, sizeof(tcache)); tcache.res = 2;
  state.fb_layer[findFramebuf(0x0a170000)] = 0;
  run();
  CHECK(*prev == ((GE_CMD_TEXBUFWIDTH0 << 24) | (0x0a << 16) | 512), "la palabra de antes no se restauro: %08lx", *prev);
  capturing = 0;
}

// Volcado binario: una lista como la de Silent Hill (formulario, copia, escena) queda en un
// .gpd con su cabecera, comandos, draws con datos, flip, memoria y fin (lo lee tools/gpd.py)
static void test_volcado(void) {
  reset();
  rt_enabled = 1;
  _sceGeListEnQueue = fakeEnqueue;
  _sceDisplaySetFrameBuf = fakeSetFrameBuf;
  dumpSetup();
  short *v = (short *)VERTS, *e = (short *)(VERTS + 0x100);
  v[0] = v[1] = v[2] = 0; v[3] = 200; v[4] = 100; v[5] = 0;
  e[0] = e[1] = e[2] = 0; e[3] = 480; e[4] = 272; e[5] = 0;
  u32 *fmt, *fbp, *start;
  formFrame(&fmt, &fbp, &start, 1);
  dumpRequest();
  sceGeListEnQueuePatched((void *)LIST, NULL, 0, NULL);
  CHECK(dumping, "no empezo el volcado");
  sceDisplaySetFrameBufPatched((void *)0x0A0D43C0, 1024, 0, 0);
  sceGeListEnQueuePatched((void *)LIST, NULL, 0, NULL); // lista nueva tras el flip: termina
  CHECK(!dumping && logHas("volcado listo"), "el volcado no termino");
  FILE *f = fopen(getenv("GPD_OUT") ? getenv("GPD_OUT") : "volcado_test.gpd", "rb");
  CHECK(f != NULL, "no se escribio el archivo");
  if (!f)
    return;
  static u8 data[16 << 20];
  int n = fread(data, 1, sizeof(data), f), pos = 0, count[16] = { 0 }, ncmd = 0, blobs = 0;
  fclose(f);
  while (pos + 8 <= n) {
    u32 type = *(u32 *)(data + pos), size = *(u32 *)(data + pos + 4);
    if (type < 16)
      count[type]++;
    if (type == GPD_CMDS)
      ncmd += size / 12;
    if (type == GPD_BLOB)
      blobs++;
    pos += 8 + ((size + 3) & ~3);
  }
  CHECK(pos == n, "registros mal formados: %d de %d bytes", pos, n);
  CHECK(count[GPD_HEADER] == 1 && !memcmp(data + 8, "GPD1", 4), "cabecera");
  CHECK(count[GPD_REGS] == 2 && count[GPD_MEM] >= 4 && count[GPD_END] == 1, "registros=%d memoria=%d fin=%d",
        count[GPD_REGS], count[GPD_MEM], count[GPD_END]);
  CHECK(count[GPD_DRAW] == 2 && blobs >= 4 && ncmd >= 20, "draws=%d datos=%d comandos=%d", count[GPD_DRAW], blobs, ncmd);
  CHECK(count[GPD_FLIP] == 1 && count[GPD_FB] >= 1 && count[GPD_TXLIST] == 1 && count[GPD_EVENT] >= 1,
        "flip=%d fb=%d lista del plugin=%d eventos=%d", count[GPD_FLIP], count[GPD_FB], count[GPD_TXLIST], count[GPD_EVENT]);
  CHECK(count[GPD_SNAP] == 2, "instantaneas=%d (antes de la copia y reducida)", count[GPD_SNAP]);
  rename(getenv("GPD_OUT") ? getenv("GPD_OUT") : "volcado_test.gpd", "volcado_activo.gpd");
  capturing = 0;
}

// Volcado en modo pasivo: un frame simple (limpia en azul, rectangulo rojo) para probar la
// conversion a PPSSPP con tools/gpd.py (queda en volcado_pasivo.gpd)
static void test_volcado_pasivo(void) {
  reset();
  passive = 1;
  _sceGeListEnQueue = fakeEnqueue;
  _sceDisplaySetFrameBuf = fakeSetFrameBuf;
  dumpSetup();
  struct { u32 c; short x, y, z, pad; } *v = (void *)VERTS; // color 8888 + posicion 16 bits
  v[0].c = 0xffff0000; v[0].x = 0; v[0].y = 0; v[1].c = 0xffff0000; v[1].x = 480; v[1].y = 272;      // azul
  v[2].c = 0xff0000ff; v[2].x = 100; v[2].y = 60; v[3].c = 0xff0000ff; v[3].x = 300; v[3].y = 200;   // rojo
  begin();
  cmd(GE_CMD_FRAMEBUFPIXFORMAT, 0); cmd(GE_CMD_FRAMEBUFPTR, 0); cmd(GE_CMD_FRAMEBUFWIDTH, 512);
  cmd(GE_CMD_SCISSOR1, 0); cmd(GE_CMD_SCISSOR2, (271 << 10) | 479);
  cmd(GE_CMD_REGION1, 0); cmd(GE_CMD_REGION2, (271 << 10) | 479);
  cmd(GE_CMD_OFFSETX, 2048 << 4); cmd(GE_CMD_OFFSETY, 2048 << 4);
  cmd(GE_CMD_TEXTUREMAPENABLE, 0); cmd(GE_CMD_ALPHABLENDENABLE, 0); cmd(GE_CMD_ZTESTENABLE, 0);
  cmd(GE_CMD_BASE, 0x080000);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | (7 << 2) | GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  cmd(GE_CMD_CLEARMODE, (1 << 8) | 1);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  cmd(GE_CMD_CLEARMODE, 0);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2);
  end();
  dumpRequest();
  sceGeListEnQueuePatched((void *)LIST, NULL, 0, NULL);
  sceDisplaySetFrameBufPatched((void *)0x04000000, 512, 0, 0);
  sceGeListEnQueuePatched((void *)LIST, NULL, 0, NULL);
  CHECK(!dumping && logHas("volcado listo"), "el volcado pasivo no termino");
  rename(getenv("GPD_OUT") ? getenv("GPD_OUT") : "volcado_test.gpd", "volcado_pasivo.gpd");
  passive = 0;
}

// La VRAM real de la Vita es de 2 MB (dev31): sin render targets; el buffer temporal comparte
// la VRAM del z-buffer, y el z-buffer queda donde estaba
// Silent Hill (puerta, linterna): escena en 0a04c000, pase de luz en 0a170000 y la mezcla
// (escena x luz). Sin VRAM para render targets: frame 1 lo aprende (mezcla omitida); desde
// el frame 2 la escena se guarda en la RAM al entrar al pase y la mezcla la dibuja encima.
static u32 *paseFrame(u32 **enter, u32 **mix) {
  struct { u16 u, v; u32 c; short x, y, z; } *v = (void *)(VERTS + 0x1000);
  int k;
  for (k = 0; k < 32; k++) { // 16 franjas de 32 px, uv = posicion
    v[k].u = v[k].x = (k + 1) / 2 * 32; v[k].v = v[k].y = k & 1 ? 272 : 0; v[k].c = 0xffffffff; v[k].z = 0;
  }
  short *e = (short *)VERTS;
  e[0] = e[1] = e[2] = 0; e[3] = 480; e[4] = 272; e[5] = 0;
  begin();
  setFb(0x04c000, 512);
  cmd(GE_CMD_TEXTUREMAPENABLE, 0); cmd(GE_CMD_ALPHABLENDENABLE, 0);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT); cmd(GE_CMD_BASE, 0x080000); cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2); // la escena
  setFb(0x170000, 512); *enter = lp - 1;
  cmd(GE_CMD_VADDR, VERTS & 0xffffff);
  cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 2); // el pase de luz
  setFb(0x04c000, 512);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1); cmd(GE_CMD_TEXADDR0, 0x170000); cmd(GE_CMD_TEXBUFWIDTH0, (0x04 << 16) | 512);
  cmd(GE_CMD_ALPHABLENDENABLE, 1); cmd(GE_CMD_BLENDMODE, 0x0a0); cmd(GE_CMD_BLENDFIXEDB, 0);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_TC_16BIT | GE_VTYPE_COL_8888 | GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_VADDR, (VERTS + 0x1000) & 0xffffff); cmd(GE_CMD_IADDR, 0);
  *mix = cmd(GE_CMD_PRIM, (GE_PRIM_RECTANGLES << 16) | 32);
  end();
  return lp;
}
static void test_pase_intercambio(void) {
  reset();
  vram_swz = 1; // VRAM de 2 MB: sin render targets
  u32 *enter, *mix;
  paseFrame(&enter, &mix);
  run();
  CHECK(*mix == NOP_OMIT && logHas("pase que se mezcla con la pantalla"), "frame 1: mezcla %08lx (debe omitirse y aprenderse)", *mix);
  if (getenv("VERLOG")) { logFlush(); printf("%s\n", test_log); }
  int f;
  for (f = 0; f < 2; f++) {
    swap_done = 0; // flip
    paseFrame(&enter, &mix);
    run();
    CHECK((enter[-1] >> 24) == GE_CMD_BASE && (*enter >> 24) == GE_CMD_CALL, "frame %d: entrada al pase %08lx %08lx", f + 2, enter[-1], *enter);
    u32 *l = (u32 *)(0x0A000000 | (*enter & 0xffffff)), save = 0, big = 0, k;
    for (k = 0; k < 40 && (l[k] >> 24) != GE_CMD_RET; k++) {
      save |= l[k] == ((GE_CMD_TRANSFERDST << 24) | (LAYER_SAVE & 0xffffff));
      big |= l[k] == ((GE_CMD_FRAMEBUFPTR << 24) | (VRAM_DRAW_BUFFER_OFFSET & 0xffffff));
    }
    CHECK(save && big, "frame %d: la entrada no guarda la escena (%lu) o no fija el buffer grande (%lu)", f + 2, save, big);
    CHECK((*mix >> 24) == GE_CMD_CALL, "frame %d: la mezcla no se reemplazo (%08lx)", f + 2, *mix);
    l = (u32 *)(0x0A000000 | (*mix & 0xffffff));
    u32 bm = 0, tex = 0, prims = 0;
    for (k = 0; k < 120 && (l[k] >> 24) != GE_CMD_RET; k++) {
      bm |= l[k] == ((GE_CMD_BLENDMODE << 24) | 0x00a);
      tex |= l[k] == ((GE_CMD_TEXADDR0 << 24) | (LAYER_SAVE & 0xffffff));
      prims += (l[k] >> 24) == GE_CMD_PRIM;
    }
    CHECK(bm && tex && prims == 32, "frame %d: mezcla: factores intercambiados=%lu escena guardada=%lu draws=%lu", f + 2, bm, tex, prims);
  }
}

static void test_vram_2mb(void) {
  reset();
  vram_swz = 1; rt_enabled = 0;
  begin();
  u32 *z = cmd(GE_CMD_ZBUFPTR, 0x088000);
  setFb(0x0d4000, 1024);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_TEXTUREMAPENABLE, 0);
  u32 *p = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  end();
  run();
  CHECK((*z & 0xffffff) == 0x100000, "z-buffer: %06lx", *z & 0xffffff);
  CHECK(!rtFindTex(0x0a0d4000) && *p == NOP_OMIT, "con 2 MB no deberia haber render targets");
  CHECK(copyVram() && copy_vram == 0x04100000, "buffer temporal en %08lx (deberia compartir el z-buffer)", copy_vram);
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

// VRAM de 2 MB: render target a escala 1 en la VRAM del z-buffer, copiado a la VRAM falsa al
// salir (en las palabras del fb siguiente), y la textura que lo lee va a la VRAM falsa (GEPVM)
static void test_rt_escala1_z(void) {
  reset();
  vram_swz = 1; rt_enabled = 0; z_used = 0;
  begin();
  cmd(GE_CMD_FRAMEBUFPIXFORMAT, 3);
  u32 *rp = lp; cmd(GE_CMD_FRAMEBUFPTR, 0x154000); cmd(GE_CMD_FRAMEBUFWIDTH, (0x04 << 16) | 128);
  cmd(GE_CMD_SCISSOR2, (127 << 10) | 127);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_TEXTUREMAPENABLE, 0);
  u32 *p1 = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  cmd(GE_CMD_FRAMEBUFPIXFORMAT, 3);
  u32 *back = lp; setFb(0, 512);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1); cmd(GE_CMD_TEXADDR0, 0x154000); u32 *tw = cmd(GE_CMD_TEXBUFWIDTH0, (0x04 << 16) | 128);
  cmd(GE_CMD_TEXFORMAT, 3);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT | GE_VTYPE_TC_16BIT);
  u32 *p2 = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  end();
  run();
  // Entrada: las palabras del fb pasan a BASE + CALL (fija tambien su profundidad)
  CHECK((rp[0] >> 24) == GE_CMD_BASE && (rp[1] >> 24) == GE_CMD_CALL && rp[-1] == ((GE_CMD_FRAMEBUFPIXFORMAT << 24) | 3),
        "rt a escala 1: entrada %08lx %08lx formato %08lx", rp[0], rp[1], rp[-1]);
  u32 *e = (u32 *)(0x0A000000 | (rp[1] & 0xffffff)), fbp = 0, zb = 0, sv = 0, ld = 0, k;
  for (k = 0; k < 40 && (e[k] >> 24) != GE_CMD_RET; k++) {
    fbp |= e[k] == ((GE_CMD_FRAMEBUFPTR << 24) | 0x100000);
    zb |= (e[k] >> 24) == GE_CMD_ZBUFPTR && (e[k] & 0xffffff) > 0x100000;
    sv |= e[k] == ((GE_CMD_TRANSFERDST << 24) | (Z_SAVE & 0xffffff));
    ld |= e[k] == ((GE_CMD_TRANSFERSRC << 24) | 0x154000) && e[k + 1] == ((GE_CMD_TRANSFERSRCW << 24) | (0x0a << 16) | 128);
  }
  CHECK(fbp && zb && !sv && ld, "entrada: fb en la VRAM del z=%lu profundidad propia=%lu carga lo anterior=%lu (sin guardar el z: %lu)", fbp, zb, ld, sv);
  CHECK(*p1 != NOP_OMIT, "el draw del render target se omitio");
  CHECK((back[0] >> 24) == GE_CMD_BASE && (back[1] >> 24) == GE_CMD_CALL, "salida del rt: %08lx %08lx (deberia ser BASE + CALL)", back[0], back[1]);
  u32 *l = (u32 *)(0x0A000000 | (back[1] & 0xffffff)), dst = 0, big = 0, fmt = 0;
  for (k = 0; k < 40 && (l[k] >> 24) != GE_CMD_RET; k++) {
    dst |= l[k] == ((GE_CMD_TRANSFERDST << 24) | 0x154000) && l[k + 1] == ((GE_CMD_TRANSFERDSTW << 24) | (0x0a << 16) | 128);
    big |= l[k] == ((GE_CMD_FRAMEBUFPTR << 24) | 0);
    fmt |= l[k] == ((GE_CMD_FRAMEBUFPIXFORMAT << 24) | PIXELFORMAT);
  }
  CHECK(dst && big && fmt, "salida del rt: copia a la VRAM falsa=%lu buffer grande=%lu formato 565=%lu", dst, big, fmt);
  CHECK(*p2 != NOP_OMIT && ((*tw >> 16) & 0xff) == 0x0a, "textura del rt: draw %08lx, ancho %08lx (deberia leer la VRAM falsa)", *p2, *tw);
  // Con profundidad ya usada en el buffer grande (Ridge Racer): se guarda la VRAM del z antes
  reset();
  vram_swz = 1; rt_enabled = 0; z_used = 1;
  begin();
  rp = lp; setFb(0x154000, 256);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  p1 = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  back = lp; setFb(0, 512);
  end();
  run();
  CHECK(*p1 != NOP_OMIT && (rp[1] >> 24) == GE_CMD_CALL, "rt con el z ocupado: draw %08lx entrada %08lx", *p1, rp[1]);
  e = (u32 *)(0x0A000000 | (rp[1] & 0xffffff)); sv = 0;
  for (k = 0; k < 40 && (e[k] >> 24) != GE_CMD_RET; k++)
    sv |= e[k] == ((GE_CMD_TRANSFERDST << 24) | (Z_SAVE & 0xffffff));
  e = (u32 *)(0x0A000000 | (back[1] & 0xffffff)); u32 rs = 0, zbig = 0;
  for (k = 0; k < 40 && (e[k] >> 24) != GE_CMD_RET; k++) {
    rs |= e[k] == ((GE_CMD_TRANSFERSRC << 24) | (Z_SAVE & 0xffffff));
    zbig |= e[k] == ((GE_CMD_ZBUFPTR << 24) | 0x100000);
  }
  CHECK(sv && rs && zbig, "z ocupado: guarda=%lu restaura=%lu profundidad del buffer grande=%lu", sv, rs, zbig);
  // El rt se lee a si mismo (Ridge Racer): solo ese draw lee la VRAM del z; las palabras de
  // textura del juego quedan como estaban (dev44 las dejaba en el z: franjas moradas despues)
  reset();
  vram_swz = 1; rt_enabled = 0; z_used = 0;
  begin();
  setFb(0x154000, 128);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_THROUGH | GE_VTYPE_POS_16BIT);
  cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  cmd(GE_CMD_TEXTUREMAPENABLE, 1); u32 *ta = cmd(GE_CMD_TEXADDR0, 0x154000); tw = cmd(GE_CMD_TEXBUFWIDTH0, (0x04 << 16) | 128);
  cmd(GE_CMD_TEXFORMAT, 3);
  cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT | GE_VTYPE_TC_16BIT);
  p2 = cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLES << 16) | 3);
  setFb(0, 512);
  end();
  run();
  u32 self = 0, after = 0;
  if ((p2[0] >> 24) == GE_CMD_CALL)
    for (l = (u32 *)(0x0A000000 | (p2[0] & 0xffffff)), k = 0; k < 20 && (l[k] >> 24) != GE_CMD_RET; k++) {
      self |= (l[k] >> 24) == GE_CMD_TEXADDR0 && (l[k] & 0xffffff) == 0x100000 && (l[k + 1] & 0xff0000) == 0x040000;
      after |= self && l[k] == ((GE_CMD_TEXADDR0 << 24) | 0x154000) && (l[k + 1] & 0xff0000) == 0x0a0000;
    }
  CHECK(self && after && *ta == ((GE_CMD_TEXADDR0 << 24) | 0x154000) && ((*tw >> 16) & 0xff) == 0x04,
        "rt que se lee a si mismo: draw en el z=%lu despues la VRAM falsa=%lu, textura del juego %08lx %08lx", self, after, *ta, *tw);
}

int main(void) {
  mapPspMemory();
  void (*tests[])(void) = {
    test_viewport_scissor_x2, test_viewport_zona, test_memo_lista_grande, test_vertices_2d,
    test_vaddr_avanza_tras_3d, test_bjump_no_salta, test_estado_entre_listas, test_textura_fb_al_dibujar,
    test_render_target, test_textura_dentro_de_rt, test_rt_incompleto, test_rt_escala_1, test_pitch_0,
    test_robustez, test_copia_listas_alternas, test_fps, test_doble_buffer_alternado, test_nop_no_revive, test_lista_estatica_fb, test_pasivo_no_escribe, test_pasivo_normaliza, test_desplazamiento_y_1024, test_vertices_2d_muchos_buffers, test_pasivo_muestreo, test_silent_hill_video, test_copia_escalada_con_ge, test_copia_reducida, test_sublistas_sin_cambios, test_sublistas_anidadas, test_sublistas_doble_buffer, test_vram_real, test_copia_formulario, test_fb_intermedio, test_opciones_juego, test_formulario_escala1, test_rt_lee_pantalla, test_volcado, test_volcado_pasivo, test_vram_2mb, test_pase_intercambio, test_rt_escala1_z, test_menu,
  };
  int i;
  for (i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++)
    tests[i]();
  printf("%d comprobaciones, %d fallas\n", checks, fails);
  return fails ? 1 : 0;
}
