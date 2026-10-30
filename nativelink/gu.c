/*
 * Lo minimo de sceGu para armar las listas propias del GE de NativeLink: las copias de bloque del espejo de la VRAM
 * (espejoFlip en main.c). Escribe los comandos del GE en la lista que se paso a guStart.
 * Basado en el PSPSDK (BSD, Copyright (c) 2005 Jesper Svennevid).
 */

#include <pspgu.h>

unsigned int *current = NULL; // siguiente palabra libre de la lista

// Agrega un comando del GE: numero en el byte alto, argumento de 24 bits
static void sendCommandi(int cmd, int argument)
{
  *(current++) = (cmd << 24) | (argument & 0xffffff);
}

// Copia de bloque (TRANSFER) de un rectangulo de src a dest; anchos en pixeles, psm GU_PSM_8888 = 32 bits por pixel
void sceGuCopyImage(int psm, int sx, int sy, int width, int height, int srcw, void* src, int dx, int dy, int destw, void* dest)
{
  sendCommandi(178,((unsigned int)src) & 0xffffff);
  sendCommandi(179,((((unsigned int)src) & 0xff000000) >> 8)|srcw);
  sendCommandi(235,(sy << 10)|sx);
  sendCommandi(180,((unsigned int)dest) & 0xffffff);
  sendCommandi(181,((((unsigned int)dest) & 0xff000000) >> 8)|destw);
  sendCommandi(236,(dy << 10)|dx);
  sendCommandi(238,((height-1) << 10)|(width-1));
  sendCommandi(234,(psm ^ 0x03) ? 0 : 1);
}

// Cierra la lista: FINISH + END
int sceGuFinish(void)
{
  sendCommandi(15,0);
  sendCommandi(12,0);
  return 0;
}

// Empieza a escribir en 'list' (cid no se usa). Se llama guStart y no sceGuStart para no chocar con la firma de
// pspgu.h, que cambio entre versiones del SDK (void antes, int ahora).
void guStart(int cid, void* list)
{
  current = (unsigned int*)((unsigned int)list);
}
