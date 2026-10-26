/*
 * Lo minimo de sceGu que usa el plugin para sus listas propias (copia final del frame).
 * Basado en el PSPSDK (BSD, Copyright (c) 2005 Jesper Svennevid).
 */

#include <pspgu.h>

unsigned int *current = NULL;

static void sendCommandi(int cmd, int argument)
{
  *(current++) = (cmd << 24) | (argument & 0xffffff);
}

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

void sceGuTexSync(void)
{
  sendCommandi(204,0);
}

int sceGuFinish(void)
{
  sendCommandi(15,0);
  sendCommandi(12,0);
  return 0;
}

// Se llama guStart y no sceGuStart para no chocar con la firma de pspgu.h,
// que cambio entre versiones del SDK (void antes, int ahora).
void guStart(int cid, void* list)
{
  current = (unsigned int*)((unsigned int)list);
}
