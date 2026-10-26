// Recorrido de listas del GE y generador de listas sintéticas (ver walk.h).
// Sin libc: se compila igual para el PSP (kernel), el ARM (suprx sin newlib) y el PC (tests).
#include "walk.h"

// Tamaño de un vértice según VERTEXTYPE (reglas del GE: cada componente se alinea a su tamaño
// y el vértice completo al componente más grande). *posOff = desplazamiento de la posición.
static uint32_t vsize(uint32_t vt, uint32_t *posOff) {
  static const uint8_t sz[4] = {0, 1, 2, 4};
  uint32_t s = 0, mx = 1, c = (vt >> 2) & 7, ce = c == 7 ? 4 : (c >= 4 ? 2 : 0);
#define ADD(e, n) if (e) { s = (s + (e) - 1) & ~((e) - 1); s += (e) * (n); if ((e) > mx) mx = (e); }
  ADD(sz[(vt >> 9) & 3], ((vt >> 14) & 7) + 1);  // pesos
  ADD(sz[vt & 3], 2);                            // coordenadas de textura
  ADD(ce, 1);                                    // color
  ADD(sz[(vt >> 5) & 3], 3);                     // normal
  uint32_t pe = sz[(vt >> 7) & 3];
  s = pe ? (s + pe - 1) & ~(pe - 1) : s;
  *posOff = s;
  ADD(pe, 3);                                    // posición
#undef ADD
  return (s + mx - 1) & ~(mx - 1);
}

static int patched(uint32_t c) {  // comandos que GePatch reescribe
  switch (c) {
  case 0x9C: case 0x9D: case 0x42: case 0x43: case 0x44: case 0x45:
  case 0xD4: case 0xD5: case 0x15: case 0x16: case 0xA0: case 0xA8: return 1;
  }
  return 0;
}

uint32_t kbWalk(const uint8_t *mem, uint32_t memBase, uint32_t memSize, uint32_t list,
                uint32_t *out, uint32_t outWords, uint32_t maxCmds, KbWalkStats *st) {
  uint32_t pc = list, base = 0, vaddr = 0, iaddr = 0, vt = 0, tex = 0, sp = 0, n = 0, op = 0;
  uint32_t stack[16], tcache[64], sum = 0x811C9DC5u;
  for (int i = 0; i < 64; i++) tcache[i] = 0;
  st->cmds = st->prims = st->through = st->verts = st->patches = st->calls = st->errors = 0;
#define IN(a, len) ((a) >= memBase && (a) - memBase <= memSize && (len) <= memSize - ((a) - memBase))
  for (;;) {
    if (n >= maxCmds || (pc & 3) || !IN(pc, 4)) { st->errors++; break; }
    uint32_t w = *(const uint32_t *)(mem + (pc - memBase)), c = w >> 24, a = w & 0xFFFFFF;
    pc += 4; n++; sum = (sum ^ w) * 16777619u;
    if (patched(c)) {  // tabla de parches: dirección y palabra original, como la MEMO de GePatch
      st->patches++;
      if (op + 2 <= outWords) { out[op++] = pc - 4; out[op++] = w; }
      if (c == 0xA0) tex = (tex & 0xFF000000) | a;
      if (c == 0xA8) {  // caché de texturas (texCheck)
        tex = (tex & 0xFFFFFF) | ((a << 8) & 0xFF000000);
        uint32_t k = (tex >> 8) & 63;
        if (tcache[k] != tex) tcache[k] = tex;
      }
    }
    switch (c) {
    case 0x10: base = (a << 8) & 0x0F000000; break;  // BASE
    case 0x01: vaddr = base | a; break;              // VADDR
    case 0x02: iaddr = base | a; break;              // IADDR
    case 0x12: vt = a; break;                        // VERTEXTYPE
    case 0x04: {                                     // PRIM
      uint32_t cnt = a & 0xFFFF, po, s = vsize(vt, &po), idx = (vt >> 11) & 3;
      st->prims++; st->verts += cnt;
      if ((vt & (1u << 23)) && ((vt >> 7) & 3) == 2) {  // 2D con posición de 16 bits: duplicar x,y
        if (idx || !IN(vaddr, cnt * s)) { st->errors++; goto fin; }
        const uint8_t *v = mem + (vaddr - memBase) + po;
        st->through++;
        for (uint32_t i = 0; i < cnt; i++, v += s) {
          uint32_t x = (uint16_t)((v[0] | v[1] << 8) * 2), y = (uint16_t)((v[2] | v[3] << 8) * 2);
          if (op < outWords) out[op++] = x | y << 16;
        }
      }
      if (idx) iaddr += cnt * (idx == 1 ? 1 : idx == 2 ? 2 : 4); else vaddr += cnt * s;
      break;
    }
    case 0x08: pc = (base | a) & ~3u; break;         // JUMP
    case 0x0A:                                       // CALL
      if (sp >= 16) { st->errors++; goto fin; }
      stack[sp++] = pc; pc = (base | a) & ~3u; st->calls++; break;
    case 0x0B:                                       // RET
      if (!sp) { st->errors++; goto fin; }
      pc = stack[--sp]; break;
    case 0x0C: goto fin;                             // END
    }
  }
fin:
#undef IN
  (void)iaddr;
  st->cmds = n; st->sum = sum;
  return sum;
}

// ---- Generador ----
static uint32_t rng;
static uint32_t rnd(uint32_t n) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) % n; }

#define SUB_OFF  0x20000   // 32 sublistas de 1 KB
#define MAIN_OFF 0x28000
#define VT2D     0x800102u // through, posición s16, textura s16 (10 bytes)
#define VT3D     0x000183u // posición float, textura float (20 bytes)

uint32_t kbGen(uint8_t *buf, uint32_t bufAddr, uint32_t bufSize, uint32_t targetCmds, uint32_t seed) {
  static const uint8_t estado[] = {0x1D, 0x1E, 0x50, 0x53, 0x5C, 0x5D, 0xC7, 0xCF, 0xDE, 0xE0, 0xE6};
  if (bufSize < MAIN_OFF + 0x1000 + KB_GEN_OUT) return 0;
  uint32_t *w = (uint32_t *)buf, subLen[32], i;
  rng = seed;
  for (i = 0; i < (bufSize - KB_GEN_OUT) / 4; i++) w[i] = 0;  // todo determinista (0 = NOP)
#define AT(off) (bufAddr + (off))
#define E(c, a) (L[n++] = ((uint32_t)(c) << 24) | ((a) & 0xFFFFFF))
#define BASEOF(addr) E(0x10, (((addr) >> 24) & 0xF) << 16)

  // Vértices 2D (64 KB): x 0..479, y 0..271, u/v cualquiera
  for (i = 0; i < 0x10000 / 10; i++) {
    uint16_t *v = (uint16_t *)(buf + i * 10);
    v[0] = rnd(256); v[1] = rnd(256); v[2] = rnd(480); v[3] = rnd(272); v[4] = 0;
  }
  // Sublistas de "modelos": ~4,4 comandos por draw 3D
  for (int k = 0; k < 32; k++) {
    uint32_t *L = w + (SUB_OFF + k * 0x400) / 4, n = 0;
    BASEOF(AT(0x10000));
    E(0x12, VT3D);
    for (int d = 0; d < 40 && n < 256 - 10; d++) {
      if (!rnd(3)) { uint32_t t = 0x09000000 + rnd(0x400000); E(0xA0, t); E(0xA8, ((t >> 8) & 0xFF0000) | 256); }
      if (!rnd(4)) E(0x12, VT3D);
      for (uint32_t r = rnd(4); r; r--) E(estado[rnd(sizeof estado)], rnd(0x1000000));
      E(0x01, AT(0x10000 + rnd(0x10000 - 12 * 20)));
      E(0x04, (4 << 16) | (3 + rnd(10)));            // tira de triángulos de 3..12 vértices
    }
    E(0x0B, 0);                                      // RET
    subLen[k] = n;
  }
  // Lista principal
  uint32_t *L = w + MAIN_OFF / 4, n = 0, cap = (bufSize - KB_GEN_OUT - MAIN_OFF) / 4 - 64;
  uint32_t ejec = 0, v2 = 0;
  E(0x9C, 0x000000); E(0x9D, 0x000200);                            // fb y ancho
  E(0x42, 0x43F00000 >> 8); E(0x43, 0xC3880000 >> 8); E(0x44, 0); // viewport (escala)
  E(0x45, 0x47000000 >> 8); E(0x46, 0x45000000 >> 8); E(0x47, 0x45000000 >> 8);
  E(0x15, 0); E(0x16, (271 << 10) | 479); E(0xD4, 0); E(0xD5, (271 << 10) | 479);
  ejec = n;
  while (ejec < targetCmds && n < cap - 64) {
    int k = rnd(32);
    uint32_t s = AT(SUB_OFF + k * 0x400);
    BASEOF(s); E(0x0A, s); ejec += 2 + subLen[k];
    for (uint32_t j = rnd(17); j && n < cap - 64; j--) {             // HUD en 2D, ~8 por modelo
      if (v2 + 4 * 10 > 0x10000) v2 = 0;
      E(0x12, VT2D); BASEOF(AT(v2)); E(0x01, AT(v2));
      uint32_t t = 0x09400000 + rnd(0x100000); E(0xA0, t); E(0xA8, ((t >> 8) & 0xFF0000) | 128);
      E(0x04, (4 << 16) | 4); v2 += 40; ejec += 7;
    }
    if (!rnd(20)) { E(0x42, 0x43F00000 >> 8); E(0x43, 0xC3880000 >> 8); E(0xD4, 0); E(0xD5, (271 << 10) | 479); ejec += 4; }
  }
  E(0x0F, 0); E(0x0C, 0);                                           // FINISH, END
  return AT(MAIN_OFF);
#undef AT
#undef E
#undef BASEOF
}
