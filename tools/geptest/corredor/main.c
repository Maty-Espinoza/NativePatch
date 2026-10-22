// GEPTEST en un solo proceso: carga cada prueba (pspautotests recompiladas con common
// -DGEPTEST_INPROC, ver armar.py) como modulo, espera que termine, guarda la pantalla y
// descarga el modulo. Antes cada prueba era un EBOOT y Adrenaline se reiniciaba entre una y
// otra (~25 s por prueba).
//  ms0:/geptest/lista.txt   "ms0:/PSP/GAME/GEPTEST/m/<id>.prx gpu/ge/edram" por linea
//  ms0:/geptest/out/<id>.{txt,err,fin} los escribe la prueba; .bmp y .res el corredor
//  estado.txt / corriendo.txt: para seguir donde quedo si Adrenaline se cae
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

PSP_MODULE_INFO("GEPTEST", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(1024);

#define DIR "ms0:/geptest"
#define WAIT_US (60 * 1000000)
static char lista[16384], cur_id[64];
static int total, cur = -1;
static int xlat; // traduccion de direcciones de la VRAM al empezar (las pruebas exact/* la cambian)
int sceGeEdramSetAddrTranslation(int value);
static PspGeContext ge0; // estado del GE al empezar: cada prueba lo encuentra como en un proceso nuevo

static void put(const char *path, const char *s) {
  SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd >= 0) { sceIoWrite(fd, s, strlen(s)); sceIoClose(fd); }
}
static int get(const char *path, char *buf, int n) {
  SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
  int r = fd >= 0 ? sceIoRead(fd, buf, n - 1) : -1;
  if (fd >= 0) sceIoClose(fd);
  buf[r > 0 ? r : 0] = 0;
  return r;
}
// Linea n de la lista: ruta del .prx e id ('/' -> '_')
static int entry(int n, char *path, char *id) {
  char *l = lista;
  for (; n > 0 && l; n--)
    if ((l = strchr(l, '\n'))) l++;
  if (!l || !*l) return 0;
  int i = 0;
  while (*l && *l != ' ' && *l != '\n' && i < 127) path[i++] = *l++;
  path[i] = 0;
  if (*l == ' ') l++;
  for (i = 0; *l && *l != '\n' && *l != '\r' && i < 63; l++) id[i++] = *l == '/' ? '_' : *l;
  id[i] = 0;
  return path[0] != 0;
}

// Pantalla actual -> BMP 512x272 de 32 bpp de abajo hacia arriba (como gentest.py)
static void shot(const char *id) {
  void *top; int bw, fmt;
  static u8 row[512 * 4];
  static const u8 hdr[54] = { 'B', 'M', 0x36, 0x80, 0x08, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
    0, 2, 0, 0, 0x10, 1, 0, 0, 1, 0, 32, 0, 0, 0, 0, 0, 0, 0x80, 0x08, 0 };
  char p[128];
  if (sceDisplayGetFrameBuf(&top, &bw, &fmt, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0 || !top || bw <= 0)
    return;
  u32 base = ((u32)top & 0x0fffffff) | 0x40000000;
  sprintf(p, DIR "/out/%s.bmp", id);
  SceUID fd = sceIoOpen(p, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd < 0) return;
  sceIoWrite(fd, hdr, sizeof(hdr));
  int x, y;
  for (y = 271; y >= 0; y--) {
    for (x = 0; x < 512; x++) {
      u32 c, r, g, b;
      if (fmt == 3) { c = ((u32 *)base)[y * bw + x]; r = c & 0xff; g = (c >> 8) & 0xff; b = (c >> 16) & 0xff; }
      else {
        c = ((u16 *)base)[y * bw + x];
        if (fmt == 0) { r = (c & 31) << 3; g = ((c >> 5) & 63) << 2; b = ((c >> 11) & 31) << 3; }
        else if (fmt == 1) { r = (c & 31) << 3; g = ((c >> 5) & 31) << 3; b = ((c >> 10) & 31) << 3; }
        else { r = (c & 15) << 4; g = ((c >> 4) & 15) << 4; b = ((c >> 8) & 15) << 4; }
      }
      row[x * 4] = b; row[x * 4 + 1] = g; row[x * 4 + 2] = r; row[x * 4 + 3] = 0;
    }
    sceIoWrite(fd, row, sizeof(row));
  }
  sceIoClose(fd);
}

static int threads(SceUID *ids, int max) {
  int n = 0;
  sceKernelGetThreadmanIdList(SCE_KERNEL_TMID_Thread, ids, max, &n);
  return n < max ? n : max;
}
static int inList(SceUID id, SceUID *ids, int n) {
  int i;
  for (i = 0; i < n; i++) if (ids[i] == id) return 1;
  return 0;
}

static void runOne(int i) {
  static SceUID before[256], after[256];
  char path[128], base[128], res[160], s[16];
  if (!entry(i, path, cur_id)) return;
  cur = i;
  sprintf(s, "%d\n", i);
  put(DIR "/corriendo.txt", s);
  sprintf(base, DIR "/out/%s", cur_id);
  int nb = threads(before, 256), k;
  SceUID fd0 = sceIoOpen(DIR "/lista.txt", PSP_O_RDONLY, 0); // primer descriptor libre
  if (fd0 >= 0) sceIoClose(fd0);
  // Como en un proceso nuevo: la VRAM en cero (las pruebas leen lo que dibujan)
  memset((void *)0x44000000, 0, 0x200000);
  sceKernelDcacheWritebackInvalidateAll();
  sceGeRestoreContext(&ge0); // registros del GE (dither, region, recorte...) como al arrancar
  u32 t0 = sceKernelGetSystemTimeLow();
  SceUID mod = sceKernelLoadModule(path, 0, NULL);
  int st = 0, r = mod, waited = 0, main_thid = -1;
  if (mod >= 0) {
    r = sceKernelStartModule(mod, strlen(base) + 1, base, &st, NULL);
    // El hilo principal de la prueba (crt0: "user_main") es el nuevo con ese nombre
    int na = threads(after, 256);
    for (k = 0; k < na; k++) {
      SceKernelThreadInfo ti;
      memset(&ti, 0, sizeof(ti));
      ti.size = sizeof(ti);
      if (!inList(after[k], before, nb) && sceKernelReferThreadStatus(after[k], &ti) >= 0 && !strcmp(ti.name, "user_main"))
        main_thid = after[k];
    }
    if (main_thid >= 0) {
      SceUInt to = WAIT_US;
      waited = sceKernelWaitThreadEnd(main_thid, &to);
    }
  }
  u32 ms = (sceKernelGetSystemTimeLow() - t0) / 1000;
  shot(cur_id);
  // Limpieza: hilos que dejo la prueba, callbacks del GE, lo que quede en la cola del GE
  int na = threads(after, 256), killed = 0;
  for (k = 0; k < na; k++)
    if (!inList(after[k], before, nb) && sceKernelTerminateDeleteThread(after[k]) >= 0) killed++;
  for (k = 0; k < 16; k++)
    sceGeUnsetCallback(k);
  if (sceGeDrawSync(1) != 0)
    sceGeBreak(1, NULL);
  sceGeEdramSetAddrTranslation(xlat); // estado global del GE: como estaba
  // libcglue no cierra los archivos de freopen: si quedan, tras ~30 pruebas no hay mas
  for (k = fd0; fd0 >= 0 && k < fd0 + 32; k++)
    sceIoClose(k);
  int stop = mod >= 0 ? sceKernelStopModule(mod, 0, NULL, NULL, NULL) : 0;
  int unl = mod >= 0 ? sceKernelUnloadModule(mod) : 0;
  const char *what = mod < 0 ? "carga" : main_thid < 0 ? "sin_hilo" : waited < 0 ? "cuelga" : "ok";
  sprintf(res, "%s %lu ms carga=%08x inicio=%08x espera=%08x hilos=%d stop=%08x descarga=%08x libre=%dKB bloque=%dKB\n",
          what, ms, mod, r, waited, killed, stop, unl, sceKernelTotalFreeMemSize() / 1024, sceKernelMaxFreeMemSize() / 1024);
  sprintf(path, DIR "/out/%s.res", cur_id);
  SceUID probe = sceIoOpen(DIR "/lista.txt", PSP_O_RDONLY, 0); // si este numero crece, quedan archivos abiertos
  sprintf(res + strlen(res) - 1, " fd=%d\n", probe);
  if (probe >= 0) sceIoClose(probe);
  put(path, res);
  sprintf(s, "%d\n", i + 1);
  put(DIR "/estado.txt", s);
}

int main(void) {
  char s[16], path[128], id[64];
  sceIoMkdir(DIR "/out", 0777);
  sceGeSaveContext(&ge0);
  xlat = sceGeEdramSetAddrTranslation(0);
  sceGeEdramSetAddrTranslation(xlat);
  if (get(DIR "/lista.txt", lista, sizeof(lista)) <= 0) {
    sceKernelExitGame();
  }
  while (entry(total, path, id)) total++;
  int n = get(DIR "/estado.txt", s, sizeof(s)) > 0 ? atoi(s) : 0;
  if (n >= total) n = 0; // termino la vez anterior: de nuevo
  if (get(DIR "/corriendo.txt", s, sizeof(s)) > 0 && s[0] >= '0' && s[0] <= '9') { // Adrenaline se cayo ahi
    int c = atoi(s);
    if (entry(c, path, id)) {
      sprintf(path, DIR "/out/%s.res", id);
      put(path, "caido\n");
      n = c + 1;
    }
  }
  for (; n < total; n++)
    runOne(n);
  sceIoRemove(DIR "/corriendo.txt");
  put(DIR "/estado.txt", "");
  put(DIR "/fin.txt", "listo\n");
  sceKernelExitGame();
  return 0;
}
