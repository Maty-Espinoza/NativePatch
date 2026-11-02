/*
 * The minimum of sceGu to build NativeLink's own GE lists: the VRAM mirror block copies (mirFlip in main.c). Writes
 * the GE commands into the list passed to guStart.
 * Based on the PSPSDK (BSD, Copyright (c) 2005 Jesper Svennevid).
 */

#include <pspgu.h>

unsigned int *current = NULL; // next free word of the list

// Adds a GE command: number in the high byte, 24-bit argument
static void sendCommandi(int cmd, int argument)
{
  *(current++) = (cmd << 24) | (argument & 0xffffff);
}

// Block copy (TRANSFER) of a rectangle from src to dest; widths in pixels, psm GU_PSM_8888 = 32 bits per pixel
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

// Closes the list: FINISH + END
int sceGuFinish(void)
{
  sendCommandi(15,0);
  sendCommandi(12,0);
  return 0;
}

// Starts writing into 'list' (cid is unused). Named guStart, not sceGuStart, to avoid clashing with the signature in
// pspgu.h, which differs between SDK versions (void or int).
void guStart(int cid, void* list)
{
  current = (unsigned int*)((unsigned int)list);
}
