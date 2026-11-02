// nptest.h: what main.c (runner) gives to scenes.c (scenes). Coordinates are PSP pixels, through mode.
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <string.h>
#define VT (GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)
#define VC (GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)
#define VRAM(p) ((void *)(0x04000000 | (u32)(p)))
#define FB_W 512
#define ZB ((void *)0x110000)
#define RT(i) ((void *)(0x154000 + (i) * 0x10000))  // intermediate images: 128x128 8888, stride 128 (6 at most)
#define CX(i) (8 + (i) % 6 * 78)                    // grid of 64x64 cells, so one scene can show several cases
#define CY(i) (14 + (i) / 6 * 66)
#define WHITE 0xFFFFFFFF
enum { F_SELF = 1, F_NOBG = 2, F_RAW = 4 };  // scene makes its own frames, keeps the old image, no state reset
typedef struct { u16 u, v; u32 c; s16 x, y, z; } Vt;
typedef struct { u32 c; s16 x, y, z; } Vc;
// expect: reason NativeDraw should log (its tnames), or "?" when no code handles the technique yet
typedef struct { const char *name, *expect; void (*fn)(int f); int flags, psm; } Scene;
extern const Scene scenes[];
extern const int nscenes;
extern u32 fb_psm;            // pixel format of the shown image
void *backBuf(void);          // the image being drawn this frame (VRAM offset)
void *frontBuf(void);         // the image shown now
void target(void *p, int psm, int stride);  // draw to p, scissor 128x128
void targetFb(void);          // back to the shown image, full scissor
void begin(void), end(void), flip(void);    // what the runner does around a scene (for F_SELF)
void relist(void);            // ends the list, waits for the GE and opens another
void rect(int x, int y, int w, int h, u32 c);
void spr(int u0, int v0, int u1, int v1, int x, int y, int w, int h, u32 c);
void tex(int psm, int swz, int w, int h, int tbw, const void *p);
