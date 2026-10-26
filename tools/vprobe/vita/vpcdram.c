// vpcdram.suprx (B4, lado ARM): donde vive la VRAM del PSP del lado de la Vita y si el ARM la puede leer.
//
// Plugin de usuario en el proceso de Adrenaline (taiHEN, seccion *PSPEMUCFW de ur0:tai/config.txt).
// No engancha nada y no usa offsets de ScePspemu:
// 1. Espera a que la RAM del PSP este mapeada (vista con cache 0x59000000 + dir, la que usa Adrenaline).
// 2. Solo LEE el buzon 0x0A000000 del PSP hasta ver "VPB4" (lo escribe VPROBE con Triangulo). El XMB usa esa
//    zona, asi que sin la marca no se escribe nada ahi.
// 3. Recorre el espacio de direcciones del proceso con sceKernelGetMemBlockInfoByAddr, anota cada bloque y busca
//    la firma de 16 bytes en los legibles (anota cada bloque ANTES de leerlo: si algo se cae, se sabe donde).
// 4. Si la encuentra: comprueba la segunda firma y el rectangulo que dibujo el GE, mide 1 MB con memcpy y DMA,
//    deja una marca en VRAM+1F8000 y responde en el buzon.
// Log: ux0:pspemu/ge_patch_vpcdram.log (el relevo lo baja solo).
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/dmac.h>
#include <psp2/io/fcntl.h>
#include <psp2/compat.h>
#include <stdarg.h>

#define LOGF "ux0:pspemu/ge_patch_vpcdram.log"
#define PSP(a) ((volatile uint32_t *)(0x59000000 + (a)))  // vista con cache de la RAM del PSP
#define MBOX 0x0A000000u
#define NOW() sceKernelGetProcessTimeLow()

static void alog(const char *fmt, ...) {
  char b[256];
  va_list ap; va_start(ap, fmt);
  int n = sceClibSnprintf(b, 16, "%7u ", NOW() / 1000);
  n += sceClibVsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap);
  if (n > (int)sizeof b - 1) n = sizeof b - 1;
  SceUID fd = sceIoOpen(LOGF, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) { sceIoWrite(fd, b, n); sceIoClose(fd); }
}

static int mapeado(uint32_t a, uint32_t size) {
  SceKernelMemBlockInfo in; in.size = sizeof in;
  if (sceKernelGetMemBlockInfoByAddr((void *)a, &in) < 0) return 0;
  return a >= (uint32_t)in.mappedBase && a + size <= (uint32_t)in.mappedBase + in.mappedSize;
}
static uint32_t rd(uint32_t a, uint32_t n) {  // invalida y lee palabras del buzon del PSP
  sceCompatCache(SCE_COMPAT_CACHE_INVALIDATE, (void *)PSP(a & ~63u), 64 * ((n * 4 + 63) / 64 + 1));
  return *PSP(a);
}
static void wr(uint32_t a, uint32_t v) {
  *PSP(a) = v;
  sceCompatCache(SCE_COMPAT_CACHE_WRITEBACK, (void *)PSP(a & ~63u), 64);
}

typedef struct { uint32_t base, size, type, access, memtype; } Blk;
static Blk blk[256] __attribute__((aligned(64)));
static volatile int dato __attribute__((aligned(64))) = 1;  // .data no vacia y alineada (segmento RW valido)
static int nb;

static void enumera(void) {
  nb = 0;
  for (uint32_t a = 0x40000000; a < 0xE0000000 && nb < 256;) {
    SceKernelMemBlockInfo in; in.size = sizeof in;
    if (sceKernelGetMemBlockInfoByAddr((void *)a, &in) >= 0 && in.mappedSize) {
      uint32_t b = (uint32_t)in.mappedBase, s = in.mappedSize;
      blk[nb++] = (Blk){b, s, in.type, in.access, in.memoryType};
      alog("bloque %08x-%08x %6u KB tipo %08x acceso %x memtipo %x\n", b, b + s, s >> 10, in.type, in.access, in.memoryType);
      a = b + s > a ? ((b + s + 0xFFFF) & ~0xFFFFu) : a + 0x10000;
    } else a += 0x10000;
  }
  alog("%d bloques\n", nb);
}

static int tarea(SceSize args, void *argp) {
  alog("vpcdram: esperando la RAM del PSP mapeada\n");
  for (int i = 0; !mapeado((uint32_t)PSP(MBOX), 4096); i++) {
    if (i == 600) { alog("la RAM del PSP no aparecio en 10 min: termino\n"); return sceKernelExitDeleteThread(0); }
    sceKernelDelayThread(1000000);
  }
  alog("RAM del PSP mapeada; esperando VPROBE (Triangulo)\n");
  for (;;) {
    sceKernelDelayThread(200000);
    if (rd(MBOX, 6) != 0x34425056) continue;  // "VPB4"
    uint32_t f[4];
    for (int i = 0; i < 4; i++) f[i] = ~*PSP(MBOX + 8 + i * 4);
    alog("VPROBE listo, nonce %08x, firma %08x %08x %08x %08x\n", *PSP(MBOX + 4), f[0], f[1], f[2], f[3]);
    enumera();
    uint32_t vram = 0, hits = 0, revisados = 0, tipo = 0;
    for (int i = 0; i < nb; i++) {
      Blk *b = &blk[i];
      // v1 solo miraba bloques de 2 MB o mas y no encontro nada: la VRAM podria estar repartida en bloques de 1 MB
      if (!(b->access & 4) || b->size > 0x08000000 || b->size < 0x10000) continue;  // legibles, de 64 KB a 128 MB
      alog("leyendo bloque %08x (%u KB)\n", b->base, b->size >> 10);
      revisados++;
      volatile uint32_t *w = (uint32_t *)b->base;
      uint32_t n = b->size / 4, ge = 0;
      for (uint32_t j = 0; j + 16 <= n; j += 4) {  // firmas alineadas a 16 bytes
        uint32_t x = w[j];
        if (x == f[0] && w[j + 1] == f[1] && w[j + 2] == f[2] && w[j + 3] == f[3]) {
          uint32_t a = b->base + j * 4, base = a - 0x1F0000;
          hits++;
          int seg = base >= b->base && *(volatile uint32_t *)(base + 0x100000) == (f[0] ^ 0xFFFF0000);
          alog("  firma 1 en %08x -> base %08x; segunda firma %s\n", a, base, seg ? "si" : "no");
          if (seg && !vram) { vram = base; tipo = b->type; }
        } else if (x == (f[0] ^ 0xFFFF0000) && w[j + 1] == (f[1] ^ 0xFFFF0000)) {
          hits++;
          alog("  firma 2 (VRAM+100000) en %08x\n", b->base + j * 4);
        } else if (x == 0xC0FFEE11 && ge < 4) {  // el rectangulo que dibujo el GE: 64 palabras seguidas por linea
          int k = 1;
          while (k < 16 && w[j + k] == 0xC0FFEE11) k++;
          if (k == 16) { ge++; hits++; alog("  rectangulo del GE (16 palabras c0ffee11) en %08x\n", b->base + j * 4); }
        }
      }
    }
    uint32_t tm = 0, td = 0;
    if (vram) {
      SceUID mb = sceKernelAllocMemBlock("vpcdram", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x100000, NULL);
      void *loc = 0;
      if (mb >= 0 && sceKernelGetMemBlockBase(mb, &loc) >= 0) {
        uint32_t t = NOW(); sceClibMemcpy(loc, (void *)vram, 0x100000); tm = NOW() - t;
        t = NOW(); sceDmacMemcpy(loc, (void *)vram, 0x100000); td = NOW() - t;
        sceKernelFreeMemBlock(mb);
      }
      *(volatile uint32_t *)(vram + 0x1F8000) = 0x41524D21;  // "!MRA": marca para el PSP
      alog("VRAM del PSP en %08x (tipo %08x). Leer 1 MB: memcpy %u us, DMA %u us. Marca escrita en +1F8000\n", vram, tipo, tm, td);
    } else alog("no se encontro la VRAM en %u bloques revisados (%u coincidencias sueltas, ver arriba)\n", revisados, hits);
    wr(MBOX + 68, vram); wr(MBOX + 72, tipo); wr(MBOX + 76, tm); wr(MBOX + 80, td);
    wr(MBOX + 84, revisados); wr(MBOX + 88, hits); wr(MBOX + 64, 0x564D5241);  // "ARMV" al final
    while (rd(MBOX, 1) == 0x34425056) sceKernelDelayThread(200000);  // esperar a que VPROBE termine
    alog("VPROBE termino\n");
  }
  return 0;
}

int module_start(SceSize args, void *argp) {
  SceUID th = sceKernelCreateThread("vpcdram", tarea, 0x10000100 + 20, 0x4000, 0, 0, NULL);
  if (th >= 0) sceKernelStartThread(th, 0, NULL);
  return SCE_KERNEL_START_SUCCESS;
}
int _start(SceSize args, void *argp) __attribute__((weak, alias("module_start")));
int module_stop(SceSize args, void *argp) { return SCE_KERNEL_STOP_SUCCESS; }
