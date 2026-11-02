// registro.h: format of the frame record. Written by the capturer (registro.c, inside captura.c) and read by the
// translator (drawing on the Vita), volcado.c (saves it to the card) and reproduccion.c (loads it from a dump).
// If RFrame or RDraw change, the dump format must be reviewed (volcado.c, reproduccion.c, tools/volcado.py).
// RS: the GE registers saved in each state, in this order (RE(e, 0xDF) = the value of 0xDF in state
// e).
#include <stdint.h>
static const uint8_t RS[] __attribute__((unused)) = { 0x1E, 0x21, 0x22, 0x23, 0x24, 0x27, 0x28, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4A, 0x4B, 0x4C, 0x4D, 0x50, 0x55, 0x56, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA8, 0xB8, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9,
  0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE6, 0xE7, 0xE8, 0xE9 };
#define NRS ((int)sizeof RS)
#define RD_N 4096         // draws per frame
#define RE_N 1024         // states per frame
#define RA_N (512 << 10)  // vertices, indices and palettes per frame
#define TN_N 128          // textures per frame
// A draw: st = index of its state in e[]; prim, n, vt = GE type, count and vertex type; voff/vlen = vertices
// in a[]; ioff/clut/toff: offset + 1 (0: absent) of indices and palette in a[] and of the texture in tx; clen: palette
// bytes.
typedef struct { uint16_t st, prim; uint32_t n, vt, voff, vlen, ioff, clut, clen, toff; } RDraw;
// A recorded frame: draws d[nd], states e[ne], area a[na] (vertices, indices, palettes) and textures in tx (ntx bytes)
typedef struct {
  uint32_t frame, fb, fbw, nd, ne, na, flags, draws_seen, nt, ntx;  // flags: reasons for being incomplete (R_*)
  uint32_t mir, mirf;             // mirror copy the list was read with and last finished at publish time
  uint8_t *tx;                    // copied textures (uncached LPDDR, the GPU reads them from here): ntx bytes
  uint32_t ta[TN_N], tn[TN_N], to[TN_N];  // textures of this frame: address, bytes (| swizzle << 31), offset
  uint64_t tm[TN_N];              // bit g: group of 8 rows g copied or requested (only the rows the draws use)
  uint32_t tq[TN_N];              // the oldest mirror copy its rows came from (0: from RAM)
  uint32_t tr[TN_N];              // bytes of a group of 8 rows
  uint32_t dv, tgt;               // has VRAM textures to copy; mirror copy of its flip
  uint32_t nimg;                  // (dumps) its PSP image (from the mirror, same flip): offset + 1 in tx
  RDraw d[RD_N];
  uint32_t e[RE_N][NRS];
  uint8_t a[RA_N] __attribute__((aligned(16)));
} RFrame;
static inline uint32_t RE(const uint32_t *e, uint32_t r) {  // value of register r in state e (0 if not saved)
  for (int i = 0; i < NRS; i++) if (RS[i] == r) return e[i];
  return 0;
}
#define TX_N (3 << 19)  // texture bytes per frame (1.5 MB)
// registro.c functions used by the presenter
RFrame *regTake(void);  // reserves the last complete frame (0 if none)
int regMapGpu(void);  // (with GXM initialized) maps the copied textures for the GPU
void regRelease(void);  // releases it
void regGpu(uint32_t q, volatile uint32_t *finish);  // scene that reads the textures of the taken record
