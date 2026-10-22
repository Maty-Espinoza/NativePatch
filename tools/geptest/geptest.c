// Bateria de pruebas del GE en la consola (pspautotests + propias), sin PC ni psplink.
// Plugin de Adrenaline (game.txt) que solo actua dentro de ms0:/PSP/GAME/GEPTEST/:
//  - En el menu (vsh.txt) lanza la prueba anotada en pendiente.txt (ver queue).
//  - El lanzador (GEPTEST/EBOOT.PBP) arranca la secuencia: lee ms0:/geptest/lista.txt
//    ("ruta id" por linea) y ms0:/geptest/estado.txt (siguiente indice) y lanza esa prueba.
//  - Cada prueba es su propio EBOOT (GEPTEST/t/NNN/EBOOT.PBP): proceso limpio, como en psplink.
//    host0:/__testoutput.txt -> ms0:/geptest/out/<id>.txt (y .err, .fin); al terminar
//    (sceKernelExitGame) se guarda la pantalla en <id>.bmp (512x272, 32 bpp, como gentest.py)
//    y se lanza la siguiente.
//  - Vigilante: si una prueba no termina en 60 s queda "cuelga" y se sigue. Si Adrenaline se
//    cae, corriendo.txt dice cual fue: al abrir de nuevo el lanzador queda "caido" y se sigue.
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspinit.h>
#include <pspdisplay.h>
#include <psploadexec_kernel.h>
#include <string.h>
#include <stdio.h>
#include <systemctrl.h>
#include "../../font.h"

PSP_MODULE_INFO("geptest", 0x1007, 1, 0);

#define DIR "ms0:/geptest"
#define WAIT_S 60
static int idx = -1;              // prueba actual (-1: lanzador)
static char id[64];               // "gpu_ge_edram"
static u32 t0;
static SceUID (*ioOpen)(const char *, int, SceMode);
static volatile int done;
static int num(const char *s) { int n = 0; while (*s >= '0' && *s <= '9') n = n * 10 + *s++ - '0'; return n; }

static void put(const char *path, const char *s) {
  SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd >= 0) {
    sceIoWrite(fd, s, strlen(s));
    sceIoClose(fd);
  }
}
static int get(const char *path, char *buf, int n) {
  SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
  int r = fd >= 0 ? sceIoRead(fd, buf, n - 1) : -1;
  if (fd >= 0)
    sceIoClose(fd);
  buf[r > 0 ? r : 0] = 0;
  return r;
}
// Traza paso a paso (se abre y cierra en cada linea: queda escrita aunque Adrenaline se caiga)
static void tr(const char *fmt, u32 a, u32 b) {
  char line[160];
  sprintf(line, "%08lx ", sceKernelGetSystemTimeLow());
  sprintf(line + 9, fmt, a, b);
  strcat(line, "\n");
  SceUID fd = sceIoOpen(DIR "/traza.txt", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
  if (fd >= 0) {
    sceIoWrite(fd, line, strlen(line));
    sceIoClose(fd);
  }
}
static void result(const char *what) {
  char p[128], s[64];
  sprintf(p, DIR "/out/%s.res", id);
  sprintf(s, "%s %lu ms\n", what, (sceKernelGetSystemTimeLow() - t0) / 1000);
  put(p, s);
}

// Linea n de la lista: ruta y id (con '/' -> '_'). 0 si no hay
static char lista[16384];
static int entry(int n, char *path, char *name) {
  if (!lista[0] && get(DIR "/lista.txt", lista, sizeof(lista)) <= 0)
    return 0;
  char *l = lista;
  for (; n > 0 && l; n--)
    if ((l = strchr(l, '\n')))
      l++;
  if (!l || !*l)
    return 0;
  int i = 0;
  while (*l && *l != ' ' && *l != '\n' && i < 127) path[i++] = *l++;
  path[i] = 0;
  if (*l == ' ') l++;
  i = 0;
  while (*l && *l != '\n' && *l != '\r' && i < 63) { name[i] = *l == '/' ? '_' : *l; i++; l++; }
  name[i] = 0;
  return path[0] != 0;
}

static void launch(int n) {
  static char path[128], name[64], s[16];
  if (!entry(n, path, name)) {
    sceIoRemove(DIR "/corriendo.txt");
    put(DIR "/fin.txt", "listo\n");
    sctrlKernelExitVSH(NULL);
    return;
  }
  sprintf(s, "%d\n", n);
  put(DIR "/corriendo.txt", s);
  struct SceKernelLoadExecVSHParam p;
  memset(&p, 0, sizeof(p));
  p.size = sizeof(p);
  p.args = strlen(path) + 1;
  p.argp = path;
  p.key = "game";
  tr("lanzar %ld", n, 0);
  int r = sctrlKernelLoadExecVSHMs2(path, &p);
  tr("LoadExecVSHMs2 devolvio %08lx", r, 0);
}

// Desde un juego (lanzador o prueba) no se lanza directo: en Adrenaline eso lo cerraba. Se
// anota la siguiente en pendiente.txt y se vuelve al menu; este mismo plugin, cargado en el
// menu (vsh.txt), la lanza desde ahi, como cuando el usuario abre un homebrew.
static void (*exitGame)(void);
static void queue(int n) {
  char s[16], path[128], name[64];
  if (entry(n, path, name)) {
    sprintf(s, "%d\n", n);
    put(DIR "/pendiente.txt", s);
  } else {
    put(DIR "/pendiente.txt", "");
    sceIoRemove(DIR "/corriendo.txt");
    put(DIR "/fin.txt", "listo\n");
  }
  tr("pendiente %ld -> menu", n, 0);
}
static int fromMenu(SceSize args, void *argp) {
  char s[16];
  sceKernelDelayThread(4000000); // que termine de cargar el menu
  if (get(DIR "/pendiente.txt", s, sizeof(s)) <= 0 || s[0] < '0' || s[0] > '9') {
    tr("menu: nada pendiente", 0, 0);
    return 0;
  }
  put(DIR "/pendiente.txt", ""); // una sola vez: si Adrenaline se cae no se repite solo
  tr("menu: pendiente %ld", num(s), 0);
  launch(num(s));
  return 0;
}

// Pantalla actual -> BMP 512x272 de 32 bpp de abajo hacia arriba (formato de gentest.py)
static void shot(void) {
  void *top; int bw, fmt;
  static u8 row[512 * 4];
  static const u8 hdr[54] = { 'B', 'M', 0x36, 0x80, 0x08, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
    0, 2, 0, 0, 0x10, 1, 0, 0, 1, 0, 32, 0, 0, 0, 0, 0, 0, 0x80, 0x08, 0 };
  char p[128];
  if (sceDisplayGetFrameBuf(&top, &bw, &fmt, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0 || !top || bw <= 0)
    return;
  u32 base = ((u32)top & 0x0fffffff) | 0x40000000; // sin cache
  sprintf(p, DIR "/out/%s.bmp", id);
  SceUID fd = sceIoOpen(p, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd < 0)
    return;
  sceIoWrite(fd, hdr, sizeof(hdr));
  int x, y;
  for (y = 271; y >= 0; y--) {
    for (x = 0; x < 512; x++) {
      u32 c, r, g, b;
      if (fmt == 3) {
        c = ((u32 *)base)[y * bw + x];
        r = c & 0xff; g = (c >> 8) & 0xff; b = (c >> 16) & 0xff;
      } else {
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

// Barra de estado abajo (16 px): "GEPTEST 003/112 gpu/bounding/count |" con un indicador que
// gira, para ver que sigue andando (las pruebas dejan la pantalla en negro). Se dibuja con
// la CPU encima del fb mostrado; el comparador ignora esas filas.
static int total;
static void barText(u32 base, int bw, int fmt, int x, const char *s) {
  for (; *s && x < 480 - 8; s++, x += 8) {
    const unsigned char *g = font8x16[(*s >= 32 && *s < 127 ? *s : '?') - 32];
    int r, c;
    for (r = 0; r < 16; r++)
      for (c = 0; c < 8; c++) {
        int on = g[r] & (0x80 >> c), o = (256 + r) * bw + x + c;
        if (fmt == 3) ((u32 *)base)[o] = on ? 0xffffffff : 0xff000000;
        else ((u16 *)base)[o] = on ? 0xffff : (fmt == 1 ? 0x8000 : fmt == 2 ? 0xf000 : 0);
      }
  }
}
static int bar(SceSize args, void *argp) {
  static const char spin[] = "|/-\\";
  char s[96];
  int k = 0;
  if (!total) {
    char path[128], name[64];
    while (entry(total, path, name)) total++;
  }
  for (;; k++) {
    void *top; int bw, fmt;
    if (sceDisplayGetFrameBuf(&top, &bw, &fmt, PSP_DISPLAY_SETBUF_IMMEDIATE) >= 0 && top && bw >= 480 && !done) {
      if (idx < 0) sprintf(s, "GEPTEST %d pruebas: iniciando %c", total, spin[k & 3]);
      else sprintf(s, "GEPTEST %03d/%d %s %c", idx + 1, total, id, spin[k & 3]);
      barText(((u32)top & 0x0fffffff) | 0x40000000, bw, fmt, 0, s);
    }
    sceKernelDelayThread(150000);
  }
  return 0;
}

static void next(const char *what) {
  if (done)
    return;
  done = 1;
  tr("prueba %ld: fin (%ld = cuelga)", idx, what[0] == 'c');
  char s[16];
  shot();
  result(what);
  sprintf(s, "%d\n", idx + 1);
  put(DIR "/estado.txt", s);
  queue(idx + 1);
}

// host0:/__testoutput.txt -> ms0:/geptest/out/<id>.txt
static SceUID ioOpenHook(const char *file, int flags, SceMode mode) {
  int k1 = pspSdkSetK1(0);
  SceUID r;
  if (file && !strncmp(file, "host0:", 6)) {
    char p[128];
    const char *ext = strstr(file, "output") ? "txt" : strstr(file, "error") ? "err" : strstr(file, "finish") ? "fin" : "otro";
    sprintf(p, DIR "/out/%s.%s", id, ext);
    r = ioOpen(p, flags, mode);
    tr("host0 -> %ld (1 txt 2 err 3 fin): %08lx", ext[0] == 't' ? 1 : ext[0] == 'e' ? 2 : 3, r);
  } else {
    pspSdkSetK1(k1);
    return ioOpen(file, flags, mode);
  }
  pspSdkSetK1(k1);
  return r;
}

static void exitHook(void) {
  int k1 = pspSdkSetK1(0);
  tr("prueba %ld: ExitGame", idx, 0);
  next("ok");
  pspSdkSetK1(k1);
  tr("prueba %ld: salida normal", idx, 0);
  if (exitGame)
    exitGame(); // la salida normal del juego, al menu (desde el hilo de la prueba)
}

static int watch(SceSize args, void *argp) {
  sceKernelDelayThread(WAIT_S * 1000000);
  next("cuelga");
  tr("prueba %ld: cuelga -> salir al menu", idx, 0);
  sctrlKernelExitVSH(NULL);
  return 0;
}

static int starter(SceSize args, void *argp) {
  char s[16];
  sceKernelDelayThread(500000);
  sceIoMkdir(DIR "/out", 0777);
  int n = get(DIR "/estado.txt", s, sizeof(s)) > 0 ? num(s) : 0;
  if (get(DIR "/corriendo.txt", s, sizeof(s)) > 0) { // Adrenaline se cayo en esa prueba
    char path[128];
    idx = num(s);
    if (entry(idx, path, id)) {
      result("caido");
      n = idx + 1;
      sprintf(s, "%d\n", n);
      put(DIR "/estado.txt", s);
    }
  }
  queue(n); // el lanzador sale solo al menu con sceKernelExitGame (desde un hilo del plugin se caia)
  return 0;
}

int module_start(SceSize args, void *argp) {
  const char *fn = sceKernelInitFileName(), *t;
  // En el menu no hay archivo del juego (en un juego es disc0:/... o ms0:/PSP/GAME/...):
  // ahi se lanza la prueba pendiente. (sceKernelInitKeyConfig no se encontraba por su NID)
  if (!fn || !*fn || !strncmp(fn, "flash0:", 7)) {
    tr("menu: plugin cargado", 0, 0);
    SceUID th = sceKernelCreateThread("geptest_menu", fromMenu, 0x20, 0x10000, 0, NULL);
    if (th >= 0)
      sceKernelStartThread(th, 0, NULL);
    return 0;
  }
  if (!(t = strstr(fn, "/GEPTEST/")))
    return 1; // otro juego: no se queda residente
  t += 9;
  if (!strncmp(t, "t/", 2)) { // una prueba
    char path[128];
    idx = num(t + 2);
    if (!entry(idx, path, id))
      return 1;
    t0 = sceKernelGetSystemTimeLow();
    tr("prueba %ld: module_start", idx, 0);
    ioOpen = (void *)sctrlHENFindFunction("sceIOFileManager", "IoFileMgrForUser", 0x109F50BC);
    u32 ex = sctrlHENFindFunction("sceLoadExec", "LoadExecForUser", 0x05572A5F);
    if (ioOpen)
      sctrlHENPatchSyscall((u32)ioOpen, ioOpenHook);
    exitGame = (void *)ex;
    if (ex)
      sctrlHENPatchSyscall(ex, exitHook);
    sceKernelDcacheWritebackAll();
    sceKernelIcacheClearAll();
    tr("ganchos: open=%08lx exit=%08lx", (u32)ioOpen, ex);
    SceUID th = sceKernelCreateThread("geptest_vigia", watch, 0x10, 0x10000, 0, NULL);
    if (th >= 0)
      sceKernelStartThread(th, 0, NULL);
    th = sceKernelCreateThread("geptest_barra", bar, 0x18, 0x2000, 0, NULL);
    if (th >= 0)
      sceKernelStartThread(th, 0, NULL);
  } else { // el lanzador
    SceUID tb = sceKernelCreateThread("geptest_barra", bar, 0x18, 0x2000, 0, NULL);
    if (tb >= 0)
      sceKernelStartThread(tb, 0, NULL);
    tr("lanzador: module_start", 0, 0);
    SceUID th = sceKernelCreateThread("geptest_inicio", starter, 0x20, 0x4000, 0, NULL);
    if (th >= 0)
      sceKernelStartThread(th, 0, NULL);
  }
  return 0;
}

int module_stop(SceSize args, void *argp) { return 0; }
