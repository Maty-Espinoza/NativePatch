// record.h: format of the frame record. Written by the capturer (record.c, inside capture.c) and read by the
// translator (drawing on the Vita), dump.c (saves it to the card) and replay.c (loads it from a dump).
// If RFrame or RDraw change, the dump format must be reviewed (dump.c, replay.c, tools/dump.py).
// RS: the GE registers saved in each state, in this order (RE(e, 0xDF) = the value of 0xDF in state
// e).
#include <stdint.h>
static const uint8_t RS[] __attribute__((unused)) = { 0x16, 0x1E, 0x21, 0x22, 0x23, 0x24, 0x27, 0x28, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4A, 0x4B, 0x4C, 0x4D, 0x50, 0x55, 0x56, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA8, 0xB8, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9,
  0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE6, 0xE7, 0xE8, 0xE9,
  0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1D, 0x1F, 0x51, 0x58, 0x9B, 0xC0, 0xC1, 0xCD, 0xCE, 0xCF };
// RI[r]: position + 1 of register r in RS (0: not saved). Must match RS.
static const uint8_t RI[256] __attribute__((unused)) = {
  [0x16] = 1, [0x1E] = 2, [0x21] = 3, [0x22] = 4, [0x23] = 5, [0x24] = 6, [0x27] = 7, [0x28] = 8, [0x42] = 9,
  [0x43] = 10, [0x44] = 11, [0x45] = 12, [0x46] = 13, [0x47] = 14, [0x48] = 15, [0x49] = 16, [0x4A] = 17,
  [0x4B] = 18, [0x4C] = 19, [0x4D] = 20, [0x50] = 21, [0x55] = 22, [0x56] = 23, [0x9C] = 24, [0x9D] = 25,
  [0x9E] = 26, [0x9F] = 27, [0xA0] = 28, [0xA8] = 29, [0xB8] = 30, [0xC2] = 31, [0xC3] = 32, [0xC5] = 33,
  [0xC6] = 34, [0xC7] = 35, [0xC9] = 36, [0xCA] = 37, [0xD2] = 38, [0xD3] = 39, [0xD4] = 40, [0xD5] = 41,
  [0xD6] = 42, [0xD7] = 43, [0xD8] = 44, [0xD9] = 45, [0xDA] = 46, [0xDB] = 47, [0xDC] = 48, [0xDD] = 49,
  [0xDE] = 50, [0xDF] = 51, [0xE0] = 52, [0xE1] = 53, [0xE6] = 54, [0xE7] = 55, [0xE8] = 56, [0xE9] = 57,
  [0x17] = 58, [0x18] = 59, [0x19] = 60, [0x1A] = 61, [0x1B] = 62, [0x1D] = 63, [0x1F] = 64, [0x51] = 65,
  [0x58] = 66, [0x9B] = 67, [0xC0] = 68, [0xC1] = 69, [0xCD] = 70, [0xCE] = 71, [0xCF] = 72,
};
#define NRS ((int)sizeof RS)
#define RD_N 4096         // draws per frame
#define RE_N 1024         // states per frame
#define RA_N (512 << 10)  // vertices, indices and palettes per frame
#define TN_N 256          // textures per frame
// A draw: st = index of its state in e[]; prim, n, vt = GE type, count and vertex type; voff/vlen = vertices
// in a[], offset + 1, or with VX their offset in tx, where the GPU reads them (RV); ioff/clut/toff: offset + 1 (0: absent) of indices and palette in a[] and of the texture in tx; clen: palette
// bytes. 3D, also offset + 1 in a[] (GE words): woff world (12), moff view, projection and texture (12 + 16 + 12), loff
// registers 0x53-0x9A (72; 0: no lighting or environment map).
typedef struct { uint16_t st, prim; uint32_t n, vt, voff, vlen, ioff, clut, clen, toff, woff, moff, loff; } RDraw;
// A recorded frame: draws d[nd], states e[ne], area a[na] (vertices, indices, palettes) and textures in tx (ntx bytes)
typedef struct {
  uint32_t frame, fb, fbw, nd, ne, na, flags, draws_seen, nt, ntx;  // flags: reasons for being incomplete (R_*)
  uint32_t nvx;                   // bytes at the end of tx taken by vertices the GPU reads
  uint32_t mir, mirf;             // mirror copy the list was read with and last finished at publish time
  uint8_t *tx;                    // copied textures (uncached LPDDR, the GPU reads them from here): ntx bytes
  uint32_t ta[TN_N], tn[TN_N], to[TN_N];  // textures of this frame: address, bytes (| swizzle << 31), offset
  uint64_t tm[TN_N];              // bit g: group of 8 rows g copied or requested (only the rows the draws use)
  uint32_t tq[TN_N];              // the oldest mirror copy its rows came from (0: from RAM)
  uint32_t tr[TN_N];              // bytes of a group of 8 rows
  uint16_t tc[TN_N];              // texture cache entry + 1 (0: its rows are in tx); to[] then reaches the cache from tx
  uint32_t dv, tgt;               // has VRAM textures to copy; mirror copy of its flip
  uint32_t nimg;                  // (dumps) its PSP image (from the mirror, same flip): offset + 1 in tx
  RDraw d[RD_N];
  uint32_t e[RE_N][NRS];
  uint8_t a[RA_N] __attribute__((aligned(16)));
} RFrame;
static inline uint32_t RE(const uint32_t *e, uint32_t r) {  // value of register r in state e (0 if not saved)
  return RI[r] ? e[RI[r] - 1] : 0;
}
#define GVL(vt) ((vt) == 0x142 ? 1 : (vt) == 0x143 ? 2 : 0)  // lit vertices nd_v4 reads: 1 16-bit UV, 2 float UV
#define VX 0x80000000u  // (RDraw.voff) the vertices are in tx
static inline const uint8_t *RV(const RFrame *f, const RDraw *d) {  // vertices of draw d
  return d->voff & VX ? f->tx + (d->voff & ~VX) : f->a + d->voff - 1;
}
#define TX_N (3 << 19)  // texture bytes per frame (1.5 MB)
// record.c functions used by the presenter
RFrame *regTake(void);  // reserves the last complete frame (0 if none)
int regMapGpu(void);  // (with GXM initialized) maps the copied textures for the GPU
void regRelease(void);  // releases it
void regGpu(uint32_t q, volatile uint32_t *finish);  // scene that reads the textures of the taken record
