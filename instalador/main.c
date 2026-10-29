// Instalador de NativePatch para la PS Vita (VPK). Lleva adentro nativelink.prx y nativedraw.suprx (los copia
// tools/compilar.sh al armar el VPK) y los deja donde los cargan Adrenaline 8 y taiHEN:
//   - NativeLink (lado PSP): ux0:pspemu/seplugins/nativelink.prx, activado en ux0:pspemu/seplugins/plugins.txt
//     ("game, ms0:/seplugins/nativelink.prx, on"; Adrenaline 8 lee EPIplugins.txt en su lugar si existe).
//   - NativeDraw (lado Vita): ux0:data/nativedraw/nativedraw.suprx, en la seccion *PSPEMUCFW del config.txt de taiHEN
//     (ux0:tai/config.txt si existe; si no, ur0:tai/config.txt). Ese cambio necesita reiniciar la Vita.
// Antes de cambiar un archivo de configuracion deja una copia en ux0:data/nativepatch/respaldo/.
// Actualizar = volver a instalar desde un VPK mas nuevo (buscar actualizaciones en internet: pendiente, ver TODO.md).
// "Limpiar restos de pruebas" borra lo que dejaron en la Vita GePatch, gepresent y las herramientas de prueba del
// desarrollo (0.0.x). Es temporal: se quita antes de la version 1.0.
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include "font.h"

#define VERSION_FILE "ux0:data/nativedraw/version.txt"
#define LINK_DST "ux0:pspemu/seplugins/nativelink.prx"
#define LINK_LINE "game, ms0:/seplugins/nativelink.prx, on"
#define DRAW_DIR "ux0:data/nativedraw"
#define DRAW_DST "ux0:data/nativedraw/nativedraw.suprx"
#define RESPALDO "ux0:data/nativepatch/respaldo"

// --- Pantalla: texto 8x16 al doble (60 columnas x 17 lineas) ---------------------------------------------------
#define W 960
#define H 544
#define COLS 60
#define ROWS 17
static uint32_t *fb;
enum { C_FONDO = 0xFF201810, C_TEXTO = 0xFFFFFFFF, C_TENUE = 0xFFA0A0A0, C_TITULO = 0xFF40D0FF, C_SEL = 0xFF804010,
       C_OK = 0xFF60E060, C_MAL = 0xFF6060FF };  // ABGR

static void putText(int col, int row, uint32_t color, uint32_t fondo, const char *s) {
  for (; *s && col < COLS; s++, col++) {
    int c = (unsigned char)*s;
    const unsigned char *g = font8x16[(c >= 32 && c < 127 ? c : '?') - 32];
    for (int y = 0; y < 32; y++) {
      uint32_t *p = fb + (row * 32 + y) * W + col * 16;
      for (int x = 0; x < 16; x++) p[x] = g[y / 2] & (0x80 >> (x / 2)) ? color : fondo;
    }
  }
}
static void fillRow(int row, uint32_t c) {
  for (int i = row * 32 * W; i < (row + 1) * 32 * W; i++) fb[i] = c;
}
static void clear(void) { for (int i = 0; i < W * H; i++) fb[i] = C_FONDO; }

static uint32_t prev;
static uint32_t buttons(void) {  // botones recien apretados
  SceCtrlData pad;
  sceDisplayWaitVblankStart();
  sceCtrlPeekBufferPositive(0, &pad, 1);
  uint32_t p = pad.buttons & ~prev;
  prev = pad.buttons;
  return p;
}

// --- Registro de lo que se hizo (se muestra despues de cada accion) -------------------------------------------
#define NLOG 200
static char logl[NLOG][COLS + 1];
static uint32_t logc[NLOG];
static int nlog;
static void say(uint32_t color, const char *fmt, ...) {
  if (nlog >= NLOG) return;
  va_list ap; va_start(ap, fmt);
  vsnprintf(logl[nlog], COLS + 1, fmt, ap);
  va_end(ap);
  logc[nlog++] = color;
}

// --- Archivos -----------------------------------------------------------------------------------------------------
static int exists(const char *p) { SceIoStat st; return sceIoGetstat(p, &st) >= 0; }
static int isDir(const char *p) { SceIoStat st; return sceIoGetstat(p, &st) >= 0 && SCE_S_ISDIR(st.st_mode); }
static char *readAll(const char *p, int *n) {  // NULL si no existe; termina en 0
  SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0);
  if (fd < 0) return NULL;
  int sz = sceIoLseek(fd, 0, SCE_SEEK_END);
  sceIoLseek(fd, 0, SCE_SEEK_SET);
  char *b = malloc(sz + 1);
  int r = b && sz >= 0 ? sceIoRead(fd, b, sz) : -1;
  sceIoClose(fd);
  if (r < 0) { free(b); return NULL; }
  b[r] = 0;
  if (n) *n = r;
  return b;
}
static int writeAll(const char *p, const void *d, int n) {
  SceUID fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) return fd;
  int r = sceIoWrite(fd, d, n);
  sceIoClose(fd);
  return r == n ? 0 : -1;
}
static void mkdirs(const char *p) {  // crea las carpetas que falten
  char b[256];
  snprintf(b, sizeof b, "%s", p);
  for (char *q = strchr(b, '/'); q; q = strchr(q + 1, '/')) { *q = 0; sceIoMkdir(b, 0777); *q = '/'; }
  sceIoMkdir(b, 0777);
}
static int copyFile(const char *from, const char *to) {
  int n;
  char *d = readAll(from, &n);
  if (!d) return -1;
  int r = writeAll(to, d, n);
  free(d);
  return r;
}
static const char *nombre(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
// Copia de un archivo de configuracion antes de cambiarlo, en respaldo/: <nombre>_original (la primera vez que se toca;
// no se pisa) y <nombre>_anterior (antes del ultimo cambio). Nombre: ur0:tai/config.txt -> ur0_tai_config.txt
static void respaldo(const char *p) {
  char n[128], to[256];
  int k = 0;
  for (const char *q = p; *q && k < 120; q++) if (!(*q == ':' && q[1] == '/')) n[k++] = *q == '/' || *q == ':' ? '_' : *q;
  n[k] = 0;
  char *ext = strrchr(n, '.');
  if (ext) *ext = 0;
  mkdirs(RESPALDO);
  snprintf(to, sizeof to, RESPALDO "/%s_original.txt", n);
  if (!exists(to)) copyFile(p, to);
  snprintf(to, sizeof to, RESPALDO "/%s_anterior.txt", n);
  if (copyFile(p, to) == 0) say(C_TENUE, "  respaldo: %s_anterior.txt", n);
}
static int removeTree(const char *p) {  // borra una carpeta con todo lo que tiene; devuelve cuantos archivos
  int n = 0;
  SceUID d = sceIoDopen(p);
  if (d >= 0) {
    SceIoDirent e;
    while (sceIoDread(d, &e) > 0) {
      if (!strcmp(e.d_name, ".") || !strcmp(e.d_name, "..")) continue;
      char c[512];
      snprintf(c, sizeof c, "%s/%s", p, e.d_name);
      if (SCE_S_ISDIR(e.d_stat.st_mode)) n += removeTree(c);
      else if (sceIoRemove(c) >= 0) n++;
    }
    sceIoDclose(d);
  }
  sceIoRmdir(p);
  return n;
}

// --- Archivos de texto de configuracion: se procesan linea por linea -------------------------------------------
// keep(linea) devuelve 0 para quitarla; extra se agrega al final (o despues de la cabecera 'after' si existe).
static const char *const *quitarCon;  // quitar las lineas que contengan alguna de estas
static int keepLine(const char *l) {
  for (const char *const *q = quitarCon; q && *q; q++) if (strstr(l, *q)) return 0;
  return 1;
}
static int editText(const char *path, const char *const *quitar, const char *after, const char *add, int *cambio) {
  int n = 0;
  char *t = readAll(path, &n);
  if (!t && !add) return 0;
  if (!t) { t = malloc(1); t[0] = 0; }
  int cap = n + 256, len = 0, quitadas = 0, puesta = !add, agregada = 0;
  char *o = malloc(cap);
  quitarCon = quitar;
  for (char *l = t; *l;) {
    char *e = strchr(l, '\n'), *nx = e ? e + 1 : l + strlen(l);
    int ll = (int)(nx - l);
    char line[512];
    int k = ll < 511 ? ll : 511;
    memcpy(line, l, k); line[k] = 0;
    char *z = line + strlen(line); while (z > line && (z[-1] == '\n' || z[-1] == '\r' || z[-1] == ' ')) *--z = 0;
    int ya = add && !strcmp(line, add);  // la linea que se quiere dejar ya esta: se conserva una sola vez
    if (ya ? puesta : !keepLine(line)) quitadas++;
    else { memcpy(o + len, l, ll); len += ll; if (!e) o[len++] = '\n'; }
    puesta |= ya;
    if (!puesta && after && !strcmp(line, after)) { len += sprintf(o + len, "%s\n", add); puesta = agregada = 1; }
    l = nx;
  }
  if (!puesta) {
    if (len && o[len - 1] != '\n') o[len++] = '\n';
    len += after ? sprintf(o + len, "%s\n%s\n", after, add) : sprintf(o + len, "%s\n", add);
    agregada = 1;
  }
  int changed = len != n || memcmp(o, t, len);
  if (changed) {
    if (exists(path)) respaldo(path);
    int r = writeAll(path, o, len);
    if (r) say(C_MAL, "  %s: ERROR al escribir", path);
    else say(C_OK, "  %s: %s", path, quitadas && agregada ? "linea reemplazada" : quitadas ? "lineas quitadas" : "linea agregada");
    if (cambio && !r) *cambio = 1;
  }
  free(t); free(o);
  return changed;
}
static const char *pluginsTxt(void) {  // Adrenaline 8 lee EPIplugins.txt en vez de plugins.txt si existe
  return exists("ux0:pspemu/seplugins/EPIplugins.txt") ? "ux0:pspemu/seplugins/EPIplugins.txt" : "ux0:pspemu/seplugins/plugins.txt";
}
static const char *taiConfig(void) {  // taiHEN usa ux0:tai/config.txt si existe; si no, el de ur0
  return exists("ux0:tai/config.txt") ? "ux0:tai/config.txt" : "ur0:tai/config.txt";
}

// --- Estado ---------------------------------------------------------------------------------------------------------
static char lleva[64], instalada[64];  // versiones: la de este VPK y la instalada
static int reiniciar;                   // config.txt cambio: hay que reiniciar la Vita
static void leerVersion(const char *p, char *out) {
  char *t = readAll(p, NULL);
  out[0] = 0;
  if (!t) return;
  int k = 0;
  for (char *s = t; *s && k < 60; s++) out[k++] = *s == '\n' ? ' ' : *s == '\r' ? ' ' : *s;
  while (k && out[k - 1] == ' ') k--;
  out[k] = 0;
  free(t);
}
static int lineaEn(const char *path, const char *s) {
  char *t = readAll(path, NULL);
  int r = t && strstr(t, s);
  free(t);
  return r;
}

// --- Restos de las pruebas (GePatch, gepresent y herramientas; temporal, se quita antes de 1.0) --------------------
static const char *const restoDirs[] = { "ux0:data/gepresent", "ux0:data/adrfsr", "ux0:data/kbench", "ux0:data/vprobe",
  "ux0:pspemu/kbench", "ux0:pspemu/vprobe", "ux0:pspemu/gepvm", "ux0:pspemu/geptest", "ux0:pspemu/PSP/GAME/VPROBE",
  "ux0:pspemu/PSP/GAME/KBENCH", "ux0:pspemu/PSP/GAME/GEPTEST", "ux0:pspemu/PSP/GAME/GEPVM", NULL };
static const char *const restoArch[] = { "ux0:pspemu/seplugins/ge_patch.prx", "ux0:pspemu/seplugins/ge_patch_anterior.prx",
  "ux0:pspemu/seplugins/._ge_patch.prx", "ux0:pspemu/seplugins/kbench.prx", "ux0:pspemu/seplugins/vpjuego.prx",
  "ux0:pspemu/seplugins/geptest.prx", NULL };  // (exFAT no distingue mayusculas: tambien borra GE_PATCH.PRX)
static const char *const restoPlug[] = { "ge_patch.prx", "kbench.prx", "vpjuego.prx", "geptest.prx", NULL };  // lineas de plugins.txt
static const char *const restoTai[] = { "ux0:data/gepresent/", "ux0:data/adrfsr/", "ux0:data/kbench/", "ux0:data/vprobe/", NULL };
static int esResto(const char *n) {  // archivos sueltos en ux0:pspemu: logs, volcados y opciones de GePatch
  int l = strlen(n);
  if (!strncmp(n, "._", 2)) n += 2, l -= 2;  // (copias "._" que deja el Finder del Mac)
  return (!strncasecmp(n, "ge_patch", 8) && l > 4 && !strcasecmp(n + l - 4, ".log")) || (l > 4 && !strcasecmp(n + l - 4, ".gpd")) ||
         !strncasecmp(n, "gepatch_", 8);
}
static int restos(int borrar) {  // cuenta (y si borrar, borra) lo que encuentra; deja el detalle en el registro
  int total = 0, sueltos = 0;
  for (const char *const *d = restoDirs; *d; d++)
    if (isDir(*d)) {
      if (borrar) { int n = removeTree(*d); say(isDir(*d) ? C_MAL : C_OK, "  %s (%d archivos)%s", *d, n, isDir(*d) ? ": ERROR" : ""); }
      else say(C_TEXTO, "  carpeta %s", *d);
      total++;
    }
  for (const char *const *f = restoArch; *f; f++)
    if (exists(*f)) {
      if (borrar) { int r = sceIoRemove(*f); say(r < 0 ? C_MAL : C_OK, "  %s%s", *f, r < 0 ? ": ERROR" : ""); }
      else say(C_TEXTO, "  %s", *f);
      total++;
    }
  SceUID d = sceIoDopen("ux0:pspemu");
  if (d >= 0) {
    SceIoDirent e;
    while (sceIoDread(d, &e) > 0)
      if (!SCE_S_ISDIR(e.d_stat.st_mode) && esResto(e.d_name)) {
        char c[300];
        snprintf(c, sizeof c, "ux0:pspemu/%s", e.d_name);
        if (borrar) sceIoRemove(c);
        sueltos++;
      }
    sceIoDclose(d);
  }
  if (sueltos) { say(borrar ? C_OK : C_TEXTO, "  %d logs, volcados .gpd y opciones de GePatch en ux0:pspemu", sueltos); total += sueltos; }
  const char *plug[3] = { pluginsTxt(), "ux0:pspemu/seplugins/game.txt", NULL };
  for (int i = 0; plug[i]; i++)
    if (exists(plug[i])) {
      char *t = readAll(plug[i], NULL);
      int hay = 0;
      for (const char *const *q = restoPlug; t && *q; q++) hay |= strstr(t, *q) != NULL;
      free(t);
      if (hay) { total++; if (borrar) editText(plug[i], restoPlug, NULL, NULL, NULL); else say(C_TEXTO, "  lineas de GePatch o pruebas en %s", nombre(plug[i])); }
    }
  const char *cfg[3] = { "ux0:tai/config.txt", "ur0:tai/config.txt", NULL };
  for (int i = 0; cfg[i]; i++) {
    int hay = 0;
    for (const char *const *q = restoTai; *q; q++) hay |= lineaEn(cfg[i], *q);
    if (hay) { total++; if (borrar) editText(cfg[i], restoTai, NULL, NULL, &reiniciar); else say(C_TEXTO, "  lineas de gepresent o pruebas en %s", cfg[i]); }
  }
  return total;
}

// --- Acciones -------------------------------------------------------------------------------------------------------
static void instalar(void) {
  say(C_TITULO, "Instalar / actualizar: %s", lleva);
  mkdirs("ux0:pspemu/seplugins");
  mkdirs(DRAW_DIR);
  int a = copyFile("app0:nativelink.prx", LINK_DST), b = copyFile("app0:nativedraw.suprx", DRAW_DST);
  say(a ? C_MAL : C_OK, "  NativeLink -> %s%s", LINK_DST, a ? ": ERROR" : "");
  say(b ? C_MAL : C_OK, "  NativeDraw -> %s%s", DRAW_DST, b ? ": ERROR" : "");
  if (a || b) { say(C_MAL, "No se pudo copiar. No se cambio la configuracion."); return; }
  copyFile("app0:version.txt", VERSION_FILE);
  static const char *const viejaLink[] = { "nativelink.prx", NULL };  // la linea de NativeLink, si estaba de otra forma
  const char *p = pluginsTxt();
  if (!lineaEn(p, LINK_LINE)) editText(p, viejaLink, NULL, LINK_LINE, NULL);
  else say(C_TENUE, "  %s: NativeLink ya estaba activado", nombre(p));
  const char *c = taiConfig();
  if (!lineaEn(c, DRAW_DST)) editText(c, NULL, "*PSPEMUCFW", DRAW_DST, &reiniciar);
  else say(C_TENUE, "  %s: NativeDraw ya estaba", c);
  if (exists("ux0:pspemu/seplugins/ge_patch.prx") || isDir("ux0:data/gepresent"))
    say(C_TITULO, "Quedan restos de GePatch/gepresent: ver 'Limpiar restos'.");
  say(C_OK, reiniciar ? "Listo. Reinicia la Vita para cargar NativeDraw." : "Listo. Cierra Adrenaline si estaba abierto.");
}
static void desinstalar(void) {
  say(C_TITULO, "Desinstalar NativeLink y NativeDraw");
  static const char *const link[] = { "nativelink.prx", NULL }, *const draw[] = { DRAW_DST, NULL };
  editText(pluginsTxt(), link, NULL, NULL, NULL);
  editText("ux0:tai/config.txt", draw, NULL, NULL, &reiniciar);
  editText("ur0:tai/config.txt", draw, NULL, NULL, &reiniciar);
  if (exists(LINK_DST)) say(sceIoRemove(LINK_DST) < 0 ? C_MAL : C_OK, "  %s borrado", LINK_DST);
  if (isDir(DRAW_DIR)) say(C_OK, "  %s (%d archivos)", DRAW_DIR, removeTree(DRAW_DIR));
  say(C_OK, reiniciar ? "Listo. Reinicia la Vita." : "Listo.");
}

// Pantalla del registro: arriba/abajo para moverse, O o X para volver
static void verRegistro(const char *pie) {
  int top = 0;
  for (;;) {
    clear();
    for (int i = 0; i < ROWS - 1 && top + i < nlog; i++) putText(0, i, logc[top + i], C_FONDO, logl[top + i]);
    fillRow(ROWS - 1, C_SEL);
    putText(0, ROWS - 1, C_TEXTO, C_SEL, pie);
    uint32_t b;
    while (!(b = buttons()));
    if ((b & SCE_CTRL_DOWN) && top + ROWS - 1 < nlog) top++;
    else if ((b & SCE_CTRL_UP) && top) top--;
    else if (b & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS | SCE_CTRL_START)) { prev |= b; return; }
  }
}
static int confirmar(const char *titulo) {  // muestra el registro (lo que se va a hacer) y pide X
  int top = 0;
  for (;;) {
    clear();
    putText(0, 0, C_TITULO, C_FONDO, titulo);
    for (int i = 0; i < ROWS - 3 && top + i < nlog; i++) putText(0, i + 1, logc[top + i], C_FONDO, logl[top + i]);
    fillRow(ROWS - 1, C_SEL);
    putText(0, ROWS - 1, C_TEXTO, C_SEL, " X: confirmar   O: cancelar   arriba/abajo: ver mas");
    uint32_t b;
    while (!(b = buttons()));
    if ((b & SCE_CTRL_DOWN) && top + ROWS - 3 < nlog) top++;
    else if ((b & SCE_CTRL_UP) && top) top--;
    else if (b & SCE_CTRL_CROSS) return 1;
    else if (b & SCE_CTRL_CIRCLE) return 0;
  }
}

int main(void) {
  SceUID mb = sceKernelAllocMemBlock("pantalla", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 0x200000, NULL);
  if (mb < 0 || sceKernelGetMemBlockBase(mb, (void **)&fb) < 0) { sceKernelExitProcess(0); return 0; }
  SceDisplayFrameBuf df = { sizeof df, fb, W, SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, W, H };
  sceDisplaySetFrameBuf(&df, SCE_DISPLAY_SETBUF_NEXTFRAME);
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
  leerVersion("app0:version.txt", lleva);
  static const char *const op[] = { "Instalar / actualizar", "Desinstalar", "Limpiar restos de pruebas (temporal)",
                                    "Reiniciar la Vita", "Salir" };
  int sel = 0;
  for (;;) {
    leerVersion(VERSION_FILE, instalada);
    int hayLink = exists(LINK_DST), hayDraw = exists(DRAW_DST), plug = lineaEn(pluginsTxt(), LINK_LINE),
        tai = lineaEn(taiConfig(), DRAW_DST), viejo = exists("ux0:pspemu/seplugins/ge_patch.prx") || isDir("ux0:data/gepresent");
    clear();
    putText(0, 0, C_TITULO, C_FONDO, "NativePatch - instalador");
    putText(0, 1, C_TENUE, C_FONDO, "Proyecto en desarrollo (hecho con IA): no listo para jugar.");
    char l[160];  // (putText corta en 60 columnas)
    snprintf(l, sizeof l, "Este VPK:  %s", lleva); putText(0, 3, C_TEXTO, C_FONDO, l);
    snprintf(l, sizeof l, "Instalado: %s", hayLink || hayDraw ? (instalada[0] ? instalada : "(version desconocida)") : "nada");
    putText(0, 4, C_TEXTO, C_FONDO, l);
    snprintf(l, sizeof l, "  NativeLink %s, %s", hayLink ? "copiado" : "falta", plug ? "activado" : "sin activar en plugins.txt");
    putText(0, 5, hayLink && plug ? C_OK : C_TENUE, C_FONDO, l);
    snprintf(l, sizeof l, "  NativeDraw %s, %s", hayDraw ? "copiado" : "falta", tai ? "en config.txt" : "fuera de config.txt");
    putText(0, 6, hayDraw && tai ? C_OK : C_TENUE, C_FONDO, l);
    if (viejo) putText(0, 7, C_MAL, C_FONDO, "  Hay restos de GePatch/gepresent en la Vita.");
    if (reiniciar) putText(0, 8, C_TITULO, C_FONDO, "  Cambio config.txt: reinicia la Vita para que valga.");
    for (int i = 0; i < 5; i++) {
      fillRow(10 + i, i == sel ? C_SEL : C_FONDO);
      putText(1, 10 + i, C_TEXTO, i == sel ? C_SEL : C_FONDO, op[i]);
    }
    putText(0, ROWS - 1, C_TENUE, C_FONDO, "arriba/abajo: elegir   X: aceptar");
    uint32_t b;
    while (!(b = buttons()));
    if (b & SCE_CTRL_UP) sel = (sel + 4) % 5;
    if (b & SCE_CTRL_DOWN) sel = (sel + 1) % 5;
    if (!(b & SCE_CTRL_CROSS)) continue;
    nlog = 0;
    if (sel == 0) { instalar(); verRegistro(" O: volver"); }
    else if (sel == 1) { say(C_TEXTO, "Se borran NativeLink, NativeDraw y sus lineas en plugins.txt"); say(C_TEXTO, "y config.txt (con respaldo)."); if (confirmar("Desinstalar")) { nlog = 0; desinstalar(); verRegistro(" O: volver"); } }
    else if (sel == 2) {
      if (!restos(0)) { say(C_OK, "No hay restos de pruebas."); verRegistro(" O: volver"); }
      else if (confirmar("Se borra esto (restos de GePatch, gepresent y pruebas):")) { nlog = 0; say(C_TITULO, "Limpiar restos de pruebas"); restos(1); say(C_OK, reiniciar ? "Listo. Reinicia la Vita." : "Listo."); verRegistro(" O: volver"); }
    }
    else if (sel == 3) scePowerRequestColdReset();
    else break;
  }
  sceKernelExitProcess(0);
  return 0;
}
