// registro.h: el registro del frame (registro.c, lo escribe el capturador) que lee el presentador (paso 3.3).
// RS: los registros del GE que se guardan en cada estado, en este orden (RE(e, 0xDF) = el valor de 0xDF en el estado e).
#include <stdint.h>
static const uint8_t RS[] __attribute__((unused)) = { 0x1E, 0x21, 0x22, 0x23, 0x24, 0x27, 0x28, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4A, 0x4B, 0x4C, 0x4D, 0x50, 0x55, 0x56, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA8, 0xB8, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9,
  0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE6, 0xE7, 0xE8, 0xE9 };
#define NRS ((int)sizeof RS)
#define RD_N 4096         // draws por frame
#define RE_N 1024         // estados por frame
#define RA_N (512 << 10)  // vertices, indices y paletas por frame
typedef struct { uint16_t st, prim; uint32_t n, vt, voff, vlen, ioff, clut, clen, toff; } RDraw;  // ioff/clut/toff: desplazamiento + 1 (0: no hay); clen: bytes de la paleta; toff: en tx
typedef struct {
  uint32_t frame, fb, fbw, nd, ne, na, flags, draws_vistos, nt, ntx;  // flags: motivos de incompleto (R_*)
  uint8_t *tx;                    // (0.0.6.2) texturas copiadas (LPDDR sin cache, la GPU las lee de aca): ntx bytes
  uint32_t ta[64], tn[64], to[64];  // texturas ya copiadas en este frame: direccion, bytes, desplazamiento
  RDraw d[RD_N];
  uint32_t e[RE_N][NRS];
  uint8_t a[RA_N] __attribute__((aligned(16)));
} RFrame;
static inline uint32_t RE(const uint32_t *e, uint32_t r) {  // valor del registro r en el estado e (0 si no se guarda)
  for (int i = 0; i < NRS; i++) if (RS[i] == r) return e[i];
  return 0;
}
#define TX_N (1 << 20)  // bytes de texturas por frame
RFrame *regTomar(void);
int regMapGpu(void);  // (presentador, con GXM iniciado) registra las texturas copiadas para la GPU
void regSoltar(void);
