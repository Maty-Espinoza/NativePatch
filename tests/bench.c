// Benchmark en el PC: una lista parecida a la de una carrera de Gran Turismo
// (~10 000 draws 3D con cambios de textura y matrices, ~52 000 comandos).
// Mide el tiempo de patchGeList y cuantas busquedas en la memoria de parches hace.
// No reemplaza medir en la Vita (otra CPU y otra cache), pero compara versiones.
#define main test_main_unused
#include "test_main.c"
#undef main
#include <time.h>

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
  printf("comandos en la lista: %ld\n", (long)(lp - (u32 *)LIST));
  struct timespec a, b;
  int reps = 50;
  run(); // calentar
  clock_gettime(CLOCK_MONOTONIC, &a);
  for (i = 0; i < reps; i++) run();
  clock_gettime(CLOCK_MONOTONIC, &b);
  double us = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 1e3 / reps;
  printf("patchGeList: %.0f us por frame en este PC (%.1f ns por comando)\n", us, us * 1000 / (lp - (u32 *)LIST));
  return 0;
}
