// Benchmark en el PC: una lista parecida a la de una carrera de Gran Turismo
// (~10 000 draws 3D con cambios de textura y matrices, ~52 000 comandos).
// Mide el tiempo de patchGeList y cuantas busquedas en la memoria de parches hace.
// No reemplaza medir en la Vita (otra CPU y otra cache), pero compara versiones.
#define main test_main_unused
#include "test_main.c"
#undef main
#include <time.h>

static double timeRun(int reps) {
  struct timespec a, b;
  double best = 1e30;
  int i, round;
  run(); // calentar
  for (round = 0; round < 7; round++) { // la mejor de 7 rondas: el PC puede estar ocupado
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (i = 0; i < reps; i++) run();
    clock_gettime(CLOCK_MONOTONIC, &b);
    double us = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 1e3 / reps;
    if (us < best) best = us;
  }
  return best;
}

// Como en la captura de Gran Turismo: ~1500 sublistas (modelos) con BASE, VADDR, matriz y
// pares TEXLEVEL + PRIM; el 94% de los comandos esta dentro de sublistas
static long buildSublists(void) {
  u32 *subs = (u32 *)(LIST + 0x80000);
  int i, j;
  lp = subs;
  for (i = 0; i < 300; i++) { // 300 modelos distintos
    for (j = 0; j < 12; j++) cmd(GE_CMD_WORLDMATRIXDATA, 0);
    cmd(GE_CMD_BASE, 0x080000);
    cmd(GE_CMD_VADDR, (VERTS + (i & 0x3ff) * 16) & 0xffffff);
    cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT | GE_VTYPE_TC_16BIT);
    cmd(GE_CMD_TEXADDR0, 0x100000 + (i & 0xff) * 0x800);
    for (j = 0; j < 9; j++) {
      cmd(GE_CMD_TEXLEVEL, 0);
      cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLE_STRIP << 16) | 4);
    }
    cmd(GE_CMD_RET, 0);
  }
  u32 *end_subs = lp;
  begin();
  cmd(GE_CMD_BASE, 0x080000);
  setFb(0x000000, 512);
  u32 *s = subs;
  for (i = 0; i < 1500; i++) { // cada modelo se dibuja 5 veces
    u32 *t = subs;
    for (j = 0; j < i % 300; j++) while ((*t++ >> 24) != GE_CMD_RET);
    cmd(GE_CMD_CALL, (u32)t & 0xffffff);
  }
  (void)s; (void)end_subs;
  end();
  return 1500L * (12 + 4 + 18 + 1);
}

int main(void) {
  mapPspMemory();
  reset();
  begin();
  cmd(GE_CMD_BASE, 0x080000);
  setFb(0x000000, 512);
  int i, j;
  for (i = 0; i < 10000; i++) {
    if (i % 4 == 0) { // textura nueva cada 4 draws
      cmd(GE_CMD_TEXADDR0, 0x100000 + (i & 0xff) * 0x800);
      cmd(GE_CMD_TEXBUFWIDTH0, (0x0a << 16) | 64);
      cmd(GE_CMD_TEXSIZE0, 0x0606);
    }
    if (i % 10 == 0)
      for (j = 0; j < 12; j++) cmd(GE_CMD_WORLDMATRIXDATA, 0); // matriz
    cmd(GE_CMD_VERTEXTYPE, GE_VTYPE_POS_16BIT | GE_VTYPE_TC_16BIT);
    cmd(GE_CMD_VADDR, (VERTS + (i & 0x3ff) * 16) & 0xffffff);
    cmd(GE_CMD_PRIM, (GE_PRIM_TRIANGLE_STRIP << 16) | 4);
  }
  end();
  long n = lp - (u32 *)LIST;
  double best = timeRun(40);
  printf("lista plana: %ld comandos, %.0f us por frame en este PC (%.1f ns por comando)\n", n, best, best * 1000 / n);
  reset();
  n = buildSublists();
  opt.walk_cache = 0;
  best = timeRun(40);
  printf("con sublistas: %ld comandos, recorriendo todo %.0f us", n, best);
  opt.walk_cache = 1;
  double cached = timeRun(40);
  printf(", saltando las que no cambian %.0f us (x%.1f)\n", cached, best / cached);
  return 0;
}
