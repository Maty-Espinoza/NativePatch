#ifndef DUMP_H
#define DUMP_H

#include <psptypes.h>

// Volcado binario de un frame (ms0:/gepatch_<ID>_<frame>.gpd) para analizarlo en el PC con
// tools/gpd.py: todos los comandos recorridos (valor del juego y lo que quedo), cada draw
// con la decision del plugin y sus datos (vertices antes y despues, indices, texturas,
// CLUT, origen de copias), las lineas del log, los flips y la VRAM y registros del GE al
// empezar y al terminar. En modo pasivo es la referencia de lo que el juego quiere dibujar
// (se puede convertir a un volcado de PPSSPP y reproducir ahi).
// Formato: registros [u32 tipo][u32 bytes][datos, rellenos a 4 bytes].

enum {
  GPD_HEADER = 1, GPD_REGS, GPD_MEM, GPD_LIST, GPD_CMDS, GPD_DRAW, GPD_BLOB, GPD_EVENT, GPD_FLIP,
  GPD_TXLIST, GPD_END, GPD_FB, GPD_SNAP,
};
// Que es un BLOB (campo 'what'; las texturas llevan el nivel en los bits 8-10)
enum { B_VERTS = 1, B_VERTS_FINAL, B_INDICES, B_TEX, B_CLUT, B_XFER_SRC };

extern int dumping; // 1 mientras se graba un volcado

void dumpRequest(void);                         // L+R+SELECT o el menu: empieza con la proxima lista
int dumpOnList(u32 list, u32 stall, int kind);  // 0 encolar, 1 encolar al frente, 2 stall (antes de recorrer)
void dumpOnFlip(u32 top, int width, int fmt);
void dumpCmd(u32 *addr, u32 orig);              // despues de procesar el comando (*addr = lo que queda)
void dumpRecord(u32 type, const void *a, u32 na, const void *b, u32 nb);
void dumpBlob(u32 what, u32 addr, u32 size);    // copia de la memoria tal como esta ahora
void dumpBlobOnce(u32 what, u32 addr, u32 size); // igual, pero solo si cambio desde la ultima vez
void dumpTxList(u32 addr);                      // lista del plugin (hasta el RET)
void dumpCopyDst(u32 addr, u32 size);           // destino de una copia a la RAM: se guarda al terminar
// Instantanea de un buffer a mitad del frame: lugar en la RAM donde una lista del plugin lo
// copia (con el GE, en su momento); se guarda al terminar. 0 si no queda espacio.
u32 dumpSnap(u32 w, u32 h, u32 bpp, u32 fmt, const char *what);

// Lo que el volcado necesita del plugin (main.c lo llena al iniciar)
typedef struct {
  const void *opt; u32 opt_size;   // opciones del menu
  const u32 *shadow;               // registros del GE segun el recorrido (valores del juego)
  const int *passive;
  u32 fake_vram_used;              // hasta donde uso el juego la VRAM falsa (bytes)
  void (*state)(u32 out[8]);       // base, offset, vaddr, iaddr, framebuf, cur_rt vram, offx, offy
} DumpInfo;
extern DumpInfo dump_info;

#endif
