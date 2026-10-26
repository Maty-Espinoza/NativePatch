// Pruebas en el PC del generador y del recorrido (gcc -fsanitize=address,undefined).
// Comprueban que la lista sintética se parece a un frame de GT, que el resultado es determinista
// (lo que permite comparar el PSP con el ARM) y que una lista basura nunca lee fuera del buffer.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "walk.h"

static int fallas;
#define OK(c, ...) do { if (!(c)) { fallas++; printf("FALLA %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
  const uint32_t SZ = 0x100000, BASE = 0x08F80000;  // cruza el límite de 16 MB de 0x09000000
  uint8_t *buf = malloc(SZ), *buf2 = malloc(SZ);
  uint32_t outW = KB_GEN_OUT / 4, *out = malloc(KB_GEN_OUT), *out2 = malloc(KB_GEN_OUT);
  KbWalkStats st, st2;
  memset(out, 0, KB_GEN_OUT); memset(out2, 0, KB_GEN_OUT);

  memset(buf, 0, SZ);
  uint32_t list = kbGen(buf, BASE, SZ, 55000, 1234);
  OK(list, "kbGen no cupo");
  uint8_t *o = buf + SZ - KB_GEN_OUT;  // salida donde la pone el PSP
  (void)o;
  kbWalk(buf, BASE, SZ, list, out, outW, 1000000, &st);
  printf("cmds=%u draws=%u 2D=%u verts=%u parches=%u calls=%u errores=%u suma=%08x\n",
         st.cmds, st.prims, st.through, st.verts, st.patches, st.calls, st.errors, st.sum);
  OK(st.errors == 0, "errores=%u", st.errors);
  OK(st.cmds > 50000 && st.cmds < 62000, "cmds=%u", st.cmds);
  OK(st.prims * 3 < st.cmds && st.prims * 7 > st.cmds, "cmds por draw fuera de 3..7: %u/%u", st.cmds, st.prims);
  OK(st.through * 8 > st.prims && st.through * 4 < st.prims, "2D=%u de %u", st.through, st.prims);

  // Determinismo: misma semilla, mismo resultado (bit a bit, también la salida)
  memset(buf2, 0x77, SZ);
  uint32_t list2 = kbGen(buf2, BASE, SZ, 55000, 1234);
  kbWalk(buf2, BASE, SZ, list2, out2, outW, 1000000, &st2);
  OK(list == list2 && !memcmp(&st, &st2, sizeof st) && !memcmp(out, out2, KB_GEN_OUT), "no determinista");
  OK(!memcmp(buf, buf2, SZ - KB_GEN_OUT), "el generador depende del contenido previo del buffer");

  // Límites: lista cortada, buffer chico, maxCmds
  kbWalk(buf, BASE, SZ, list, out, 0, 1000, &st2);
  OK(st2.errors == 1 && st2.cmds == 1000, "maxCmds no corta: %u %u", st2.cmds, st2.errors);
  kbWalk(buf, BASE, 0x28000, list, out, outW, 1000000, &st2);
  OK(st2.errors == 1, "lista fuera del rango no detectada");
  OK(kbGen(buf, BASE, 0x40000, 55000, 1) == 0, "kbGen con buffer chico debía fallar");

  // Basura: nunca leer fuera (ASAN avisa) y siempre terminar
  srand(7);
  for (int t = 0; t < 3000; t++) {
    uint32_t n = 64 + rand() % 4096;
    for (uint32_t i = 0; i < n; i++) ((uint32_t *)buf)[i] = (uint32_t)rand() << 16 ^ rand();
    if (t & 1)  // con comandos de flujo apuntando a cualquier parte
      for (uint32_t i = 0; i < n; i += 7) ((uint32_t *)buf)[i] = (uint32_t)(0x08 + (rand() % 5)) << 24 | (rand() & 0xFFFFFF);
    kbWalk(buf, BASE, n * 4, BASE + (rand() % n) * 4, out, rand() % 64, 20000, &st2);
    OK(st2.cmds <= 20000, "basura: cmds=%u", st2.cmds);
  }

  // Referencia de velocidad en el PC (solo informativo)
  clock_t c0 = clock();
  for (int r = 0; r < 200; r++) kbWalk(buf2, BASE, SZ, list2, out2, outW, 1000000, &st2);
  double ns = (double)(clock() - c0) / CLOCKS_PER_SEC * 1e9 / (200.0 * st2.cmds);
  printf("PC: %.1f ns por comando\n", ns);

  printf(fallas ? "%d FALLAS\n" : "OK\n", fallas);
  return fallas != 0;
}
