// NativePatch installer (Vita app, NativePatch.vpk): install/update, uninstall and clean test leftovers.
// The VPK carries nativelink.prx, nativedraw.suprx and version.txt (added by tools/compilar.sh) and leaves them where
// Adrenaline 8 and taiHEN load them:
//   - NativeLink: ux0:pspemu/seplugins/nativelink.prx, enabled with LINK_LINE in plugins.txt (or EPIplugins.txt).
//   - NativeDraw: ux0:data/nativedraw/nativedraw.suprx, under *PSPEMUCFW in taiHEN's config.txt (needs a reboot).
// Before changing a config file it leaves a copy in ux0:data/nativepatch/respaldo/.
// Updating = installing from a newer VPK.
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
#define BACKUP "ux0:data/nativepatch/respaldo"

// --- Screen: 8x16 text doubled (60 columns x 17 lines) ---------------------------------------------------------
#define W 960
#define H 544
#define COLS 60
#define ROWS 17
static uint32_t *fb;
enum { C_BG = 0xFF201810, C_TEXT = 0xFFFFFFFF, C_DIM = 0xFFA0A0A0, C_TITLE = 0xFF40D0FF, C_SEL = 0xFF804010,
       C_OK = 0xFF60E060, C_BAD = 0xFF6060FF };  // ABGR

// Writes s from cell (col, row) with the doubled 8x16 font (16x32 pixels); cuts at COLS columns.
static void putText(int col, int row, uint32_t color, uint32_t bg, const char *s) {
  for (; *s && col < COLS; s++, col++) {
    int c = (unsigned char)*s;
    const unsigned char *g = font8x16[(c >= 32 && c < 127 ? c : '?') - 32];
    for (int y = 0; y < 32; y++) {
      uint32_t *p = fb + (row * 32 + y) * W + col * 16;
      for (int x = 0; x < 16; x++) p[x] = g[y / 2] & (0x80 >> (x / 2)) ? color : bg;
    }
  }
}
// Paints a whole text row with color c (background of the selected option and of the footer).
static void fillRow(int row, uint32_t c) {
  for (int i = row * 32 * W; i < (row + 1) * 32 * W; i++) fb[i] = c;
}
static void clear(void) { for (int i = 0; i < W * H; i++) fb[i] = C_BG; }

// Waits for vblank and returns the newly pressed buttons (prev: those held at the previous call).
static uint32_t prev;
static uint32_t buttons(void) {
  SceCtrlData pad;
  sceDisplayWaitVblankStart();
  sceCtrlPeekBufferPositive(0, &pad, 1);
  uint32_t p = pad.buttons & ~prev;
  prev = pad.buttons;
  return p;
}

// --- Log of what was done (shown after each action) -----------------------------------------------------------
#define NLOG 200
static char logl[NLOG][COLS + 1];
static uint32_t logc[NLOG];
static int nlog;
// Adds a line (printf format, cut at COLS) with its color to the log; does nothing if full.
static void say(uint32_t color, const char *fmt, ...) {
  if (nlog >= NLOG) return;
  va_list ap; va_start(ap, fmt);
  vsnprintf(logl[nlog], COLS + 1, fmt, ap);
  va_end(ap);
  logc[nlog++] = color;
}

// --- Files --------------------------------------------------------------------------------------------------------
static int exists(const char *p) { SceIoStat st; return sceIoGetstat(p, &st) >= 0; }
static int isDir(const char *p) { SceIoStat st; return sceIoGetstat(p, &st) >= 0 && SCE_S_ISDIR(st.st_mode); }
// Reads the whole file into a new block (malloc, 0-terminated; *n = length). NULL if missing or on failure.
static char *readAll(const char *p, int *n) {
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
// Creates or replaces the file with the n bytes of d. 0 if all was written; negative on failure.
static int writeAll(const char *p, const void *d, int n) {
  SceUID fd = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) return fd;
  int r = sceIoWrite(fd, d, n);
  sceIoClose(fd);
  return r == n ? 0 : -1;
}
static void mkdirs(const char *p) {  // creates the missing folders, one per '/' of the path
  char b[256];
  snprintf(b, sizeof b, "%s", p);
  for (char *q = strchr(b, '/'); q; q = strchr(q + 1, '/')) { *q = 0; sceIoMkdir(b, 0777); *q = '/'; }
  sceIoMkdir(b, 0777);
}
// Copies a file reading it entirely into memory. 0 on success.
static int copyFile(const char *from, const char *to) {
  int n;
  char *d = readAll(from, &n);
  if (!d) return -1;
  int r = writeAll(to, d, n);
  free(d);
  return r;
}
// File name, without the folder.
static const char *baseName(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
// Copy of a config file before changing it, in respaldo/: <name>_original (the first time it is touched; never
// overwritten) and <name>_anterior (before the last change). Name: ur0:tai/config.txt -> ur0_tai_config.txt
static void backup(const char *p) {
  char n[128], to[256];
  int k = 0;
  for (const char *q = p; *q && k < 120; q++) if (!(*q == ':' && q[1] == '/')) n[k++] = *q == '/' || *q == ':' ? '_' : *q;
  n[k] = 0;
  char *ext = strrchr(n, '.');
  if (ext) *ext = 0;
  mkdirs(BACKUP);
  snprintf(to, sizeof to, BACKUP "/%s_original.txt", n);
  if (!exists(to)) copyFile(p, to);
  snprintf(to, sizeof to, BACKUP "/%s_anterior.txt", n);
  if (copyFile(p, to) == 0) say(C_DIM, "  respaldo: %s_anterior.txt", n);
}
static int removeTree(const char *p) {  // deletes a folder with everything in it; returns how many files
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

// --- Text config files: processed line by line -----------------------------------------------------------------
static const char *const *rmWith;  // remove lines containing any of these (NULL-terminated list)
static int keepLine(const char *l) {  // 0 if the line must be removed
  for (const char *const *q = rmWith; q && *q; q++) if (strstr(l, *q)) return 0;
  return 1;
}
// Rewrites a text file: removes lines containing any text of rm_list and keeps line add just once
// (kept even if it matches rm_list). If missing, add goes right after header after; without header,
// at the end (preceded by the header if after is not NULL). add NULL: only removes. Writes only if the content changes,
// with backup() first and a line in the log; *did_change = 1 if written. Returns whether the content changed.
static int editText(const char *path, const char *const *rm_list, const char *after, const char *add, int *did_change) {
  int n = 0;
  char *t = readAll(path, &n);
  if (!t && !add) return 0;
  if (!t) { t = malloc(1); t[0] = 0; }
  int cap = n + 256, len = 0, removed = 0, placed = !add, added = 0;
  char *o = malloc(cap);
  rmWith = rm_list;
  for (char *l = t; *l;) {
    char *e = strchr(l, '\n'), *nx = e ? e + 1 : l + strlen(l);
    int ll = (int)(nx - l);
    char line[512];
    int k = ll < 511 ? ll : 511;
    memcpy(line, l, k); line[k] = 0;
    char *z = line + strlen(line); while (z > line && (z[-1] == '\n' || z[-1] == '\r' || z[-1] == ' ')) *--z = 0;
    int is_add = add && !strcmp(line, add);  // the line to keep is already there: the first one stays
    if (is_add ? placed : !keepLine(line)) removed++;
    else { memcpy(o + len, l, ll); len += ll; if (!e) o[len++] = '\n'; }
    placed |= is_add;
    if (!placed && after && !strcmp(line, after)) { len += sprintf(o + len, "%s\n", add); placed = added = 1; }
    l = nx;
  }
  if (!placed) {
    if (len && o[len - 1] != '\n') o[len++] = '\n';
    len += after ? sprintf(o + len, "%s\n%s\n", after, add) : sprintf(o + len, "%s\n", add);
    added = 1;
  }
  int changed = len != n || memcmp(o, t, len);
  if (changed) {
    if (exists(path)) backup(path);
    int r = writeAll(path, o, len);
    if (r) say(C_BAD, "  %s: ERROR al escribir", path);
    else say(C_OK, "  %s: %s", path, removed && added ? "linea reemplazada" : removed ? "lineas quitadas" : "linea agregada");
    if (did_change && !r) *did_change = 1;
  }
  free(t); free(o);
  return changed;
}
static const char *pluginsTxt(void) {  // Adrenaline 8 reads EPIplugins.txt instead of plugins.txt if it exists
  return exists("ux0:pspemu/seplugins/EPIplugins.txt") ? "ux0:pspemu/seplugins/EPIplugins.txt" : "ux0:pspemu/seplugins/plugins.txt";
}
static const char *taiConfig(void) {  // taiHEN uses ux0:tai/config.txt if it exists; otherwise the one in ur0
  return exists("ux0:tai/config.txt") ? "ux0:tai/config.txt" : "ur0:tai/config.txt";
}

// --- State ----------------------------------------------------------------------------------------------------------
static char bundled[64], installed[64];  // versions: this VPK's and the installed one
static int need_reboot;                   // config.txt changed: the Vita must reboot
// Reads a version.txt into out on one line (up to 60 characters, line breaks as spaces); "" if missing.
static void readVersion(const char *p, char *out) {
  char *t = readAll(p, NULL);
  out[0] = 0;
  if (!t) return;
  int k = 0;
  for (char *s = t; *s && k < 60; s++) out[k++] = *s == '\n' ? ' ' : *s == '\r' ? ' ' : *s;
  while (k && out[k - 1] == ' ') k--;
  out[k] = 0;
  free(t);
}
// 1 if the file exists and contains text s (anywhere, not only as a whole line).
static int hasLine(const char *path, const char *s) {
  char *t = readAll(path, NULL);
  int r = t && strstr(t, s);
  free(t);
  return r;
}

// --- Test leftovers (GePatch, gepresent and development tools) -----------------------------------------------------
// Folders, files, and line texts of plugins.txt/game.txt (leftPlug) and config.txt (leftTai) deleted by
// "Limpiar restos".
static const char *const leftDirs[] = { "ux0:data/gepresent", "ux0:data/adrfsr", "ux0:data/kbench", "ux0:data/vprobe",
  "ux0:pspemu/kbench", "ux0:pspemu/vprobe", "ux0:pspemu/gepvm", "ux0:pspemu/geptest", "ux0:pspemu/PSP/GAME/VPROBE",
  "ux0:pspemu/PSP/GAME/KBENCH", "ux0:pspemu/PSP/GAME/GEPTEST", "ux0:pspemu/PSP/GAME/GEPVM", NULL };
static const char *const leftFiles[] = { "ux0:pspemu/seplugins/ge_patch.prx", "ux0:pspemu/seplugins/ge_patch_anterior.prx",
  "ux0:pspemu/seplugins/._ge_patch.prx", "ux0:pspemu/seplugins/kbench.prx", "ux0:pspemu/seplugins/vpjuego.prx",
  "ux0:pspemu/seplugins/geptest.prx", NULL };  // (exFAT is case-insensitive: also deletes GE_PATCH.PRX)
static const char *const leftPlug[] = { "ge_patch.prx", "kbench.prx", "vpjuego.prx", "geptest.prx", NULL };
static const char *const leftTai[] = { "ux0:data/gepresent/", "ux0:data/adrfsr/", "ux0:data/kbench/", "ux0:data/vprobe/", NULL };
static int isLeftover(const char *n) {  // loose files in ux0:pspemu: GePatch logs, dumps and options
  int l = strlen(n);
  if (!strncmp(n, "._", 2)) n += 2, l -= 2;  // ("._" copies left by the Mac Finder)
  return (!strncasecmp(n, "ge_patch", 8) && l > 4 && !strcasecmp(n + l - 4, ".log")) || (l > 4 && !strcasecmp(n + l - 4, ".gpd")) ||
         !strncasecmp(n, "gepatch_", 8);
}
// Finds the leftovers: folders and files of the lists, loose files of ux0:pspemu (isLeftover), leftPlug lines
// in plugins.txt and game.txt, and leftTai lines in both config.txt. With erase, also deletes them (lines with
// editText, with backup). Returns how many it found and leaves the detail in the log.
static int leftovers(int erase) {
  int total = 0, loose = 0;
  for (const char *const *d = leftDirs; *d; d++)
    if (isDir(*d)) {
      if (erase) { int n = removeTree(*d); say(isDir(*d) ? C_BAD : C_OK, "  %s (%d archivos)%s", *d, n, isDir(*d) ? ": ERROR" : ""); }
      else say(C_TEXT, "  carpeta %s", *d);
      total++;
    }
  for (const char *const *f = leftFiles; *f; f++)
    if (exists(*f)) {
      if (erase) { int r = sceIoRemove(*f); say(r < 0 ? C_BAD : C_OK, "  %s%s", *f, r < 0 ? ": ERROR" : ""); }
      else say(C_TEXT, "  %s", *f);
      total++;
    }
  SceUID d = sceIoDopen("ux0:pspemu");
  if (d >= 0) {
    SceIoDirent e;
    while (sceIoDread(d, &e) > 0)
      if (!SCE_S_ISDIR(e.d_stat.st_mode) && isLeftover(e.d_name)) {
        char c[300];
        snprintf(c, sizeof c, "ux0:pspemu/%s", e.d_name);
        if (erase) sceIoRemove(c);
        loose++;
      }
    sceIoDclose(d);
  }
  if (loose) { say(erase ? C_OK : C_TEXT, "  %d logs, volcados .gpd y opciones de GePatch en ux0:pspemu", loose); total += loose; }
  const char *plug[3] = { pluginsTxt(), "ux0:pspemu/seplugins/game.txt", NULL };
  for (int i = 0; plug[i]; i++)
    if (exists(plug[i])) {
      char *t = readAll(plug[i], NULL);
      int found = 0;
      for (const char *const *q = leftPlug; t && *q; q++) found |= strstr(t, *q) != NULL;
      free(t);
      if (found) { total++; if (erase) editText(plug[i], leftPlug, NULL, NULL, NULL); else say(C_TEXT, "  lineas de GePatch o pruebas en %s", baseName(plug[i])); }
    }
  const char *cfg[3] = { "ux0:tai/config.txt", "ur0:tai/config.txt", NULL };
  for (int i = 0; cfg[i]; i++) {
    int found = 0;
    for (const char *const *q = leftTai; *q; q++) found |= hasLine(cfg[i], *q);
    if (found) { total++; if (erase) editText(cfg[i], leftTai, NULL, NULL, &need_reboot); else say(C_TEXT, "  lineas de gepresent o pruebas en %s", cfg[i]); }
  }
  return total;
}

// --- Actions --------------------------------------------------------------------------------------------------------
// Copies both plugins and version.txt, enables NativeLink in plugins.txt (replaces another nativelink.prx line) and
// adds NativeDraw under *PSPEMUCFW in config.txt. If a plugin cannot be copied, the config is left alone.
static void install(void) {
  say(C_TITLE, "Instalar / actualizar: %s", bundled);
  mkdirs("ux0:pspemu/seplugins");
  mkdirs(DRAW_DIR);
  int a = copyFile("app0:nativelink.prx", LINK_DST), b = copyFile("app0:nativedraw.suprx", DRAW_DST);
  say(a ? C_BAD : C_OK, "  NativeLink -> %s%s", LINK_DST, a ? ": ERROR" : "");
  say(b ? C_BAD : C_OK, "  NativeDraw -> %s%s", DRAW_DST, b ? ": ERROR" : "");
  if (a || b) { say(C_BAD, "No se pudo copiar. No se cambio la configuracion."); return; }
  copyFile("app0:version.txt", VERSION_FILE);
  static const char *const oldLink[] = { "nativelink.prx", NULL };  // the NativeLink line, if it was written differently
  const char *p = pluginsTxt();
  if (!hasLine(p, LINK_LINE)) editText(p, oldLink, NULL, LINK_LINE, NULL);
  else say(C_DIM, "  %s: NativeLink ya estaba activado", baseName(p));
  const char *c = taiConfig();
  if (!hasLine(c, DRAW_DST)) editText(c, NULL, "*PSPEMUCFW", DRAW_DST, &need_reboot);
  else say(C_DIM, "  %s: NativeDraw ya estaba", c);
  if (exists("ux0:pspemu/seplugins/ge_patch.prx") || isDir("ux0:data/gepresent"))
    say(C_TITLE, "Quedan restos de GePatch/gepresent: ver 'Limpiar restos'.");
  say(C_OK, need_reboot ? "Listo. Reinicia la Vita para cargar NativeDraw." : "Listo. Cierra Adrenaline si estaba abierto.");
}
// Removes the plugin lines (plugins.txt and both config.txt) and deletes nativelink.prx and the whole
// ux0:data/nativedraw folder (switches and dumps too). The backups stay.
static void uninstall(void) {
  say(C_TITLE, "Desinstalar NativeLink y NativeDraw");
  static const char *const link[] = { "nativelink.prx", NULL }, *const draw[] = { DRAW_DST, NULL };
  editText(pluginsTxt(), link, NULL, NULL, NULL);
  editText("ux0:tai/config.txt", draw, NULL, NULL, &need_reboot);
  editText("ur0:tai/config.txt", draw, NULL, NULL, &need_reboot);
  if (exists(LINK_DST)) say(sceIoRemove(LINK_DST) < 0 ? C_BAD : C_OK, "  %s borrado", LINK_DST);
  if (isDir(DRAW_DIR)) say(C_OK, "  %s (%d archivos)", DRAW_DIR, removeTree(DRAW_DIR));
  say(C_OK, need_reboot ? "Listo. Reinicia la Vita." : "Listo.");
}

// Shows the log with a footer text: up/down to move, O, X or START to return.
static void showLog(const char *footer) {
  int top = 0;
  for (;;) {
    clear();
    for (int i = 0; i < ROWS - 1 && top + i < nlog; i++) putText(0, i, logc[top + i], C_BG, logl[top + i]);
    fillRow(ROWS - 1, C_SEL);
    putText(0, ROWS - 1, C_TEXT, C_SEL, footer);
    uint32_t b;
    while (!(b = buttons()));
    if ((b & SCE_CTRL_DOWN) && top + ROWS - 1 < nlog) top++;
    else if ((b & SCE_CTRL_UP) && top) top--;
    else if (b & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS | SCE_CTRL_START)) { prev |= b; return; }
  }
}
static int confirm(const char *title) {  // shows the log (what will be done): 1 with X, 0 with O
  int top = 0;
  for (;;) {
    clear();
    putText(0, 0, C_TITLE, C_BG, title);
    for (int i = 0; i < ROWS - 3 && top + i < nlog; i++) putText(0, i + 1, logc[top + i], C_BG, logl[top + i]);
    fillRow(ROWS - 1, C_SEL);
    putText(0, ROWS - 1, C_TEXT, C_SEL, " X: confirmar   O: cancelar   arriba/abajo: ver mas");
    uint32_t b;
    while (!(b = buttons()));
    if ((b & SCE_CTRL_DOWN) && top + ROWS - 3 < nlog) top++;
    else if ((b & SCE_CTRL_UP) && top) top--;
    else if (b & SCE_CTRL_CROSS) return 1;
    else if (b & SCE_CTRL_CIRCLE) return 0;
  }
}

// Own screen in CDRAM (960x544 ABGR, 2 MB). Menu loop: shows the VPK version, what is installed and whether a reboot
// is pending; X runs the selected option, which fills the log and shows it (those that delete ask to confirm first).
int main(void) {
  SceUID mb = sceKernelAllocMemBlock("pantalla", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 0x200000, NULL);
  if (mb < 0 || sceKernelGetMemBlockBase(mb, (void **)&fb) < 0) { sceKernelExitProcess(0); return 0; }
  SceDisplayFrameBuf df = { sizeof df, fb, W, SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, W, H };
  sceDisplaySetFrameBuf(&df, SCE_DISPLAY_SETBUF_NEXTFRAME);
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
  readVersion("app0:version.txt", bundled);
  static const char *const op[] = { "Instalar / actualizar", "Desinstalar", "Limpiar restos de pruebas (temporal)",
                                    "Reiniciar la Vita", "Salir" };
  int sel = 0;
  for (;;) {
    readVersion(VERSION_FILE, installed);
    int hasLink = exists(LINK_DST), hasDraw = exists(DRAW_DST), plug = hasLine(pluginsTxt(), LINK_LINE),
        tai = hasLine(taiConfig(), DRAW_DST), stale = exists("ux0:pspemu/seplugins/ge_patch.prx") || isDir("ux0:data/gepresent");
    clear();
    putText(0, 0, C_TITLE, C_BG, "NativePatch - instalador");
    putText(0, 1, C_DIM, C_BG, "Proyecto en desarrollo (hecho con IA): no listo para jugar.");
    char l[160];  // (putText cuts at 60 columns)
    snprintf(l, sizeof l, "Este VPK:  %s", bundled); putText(0, 3, C_TEXT, C_BG, l);
    snprintf(l, sizeof l, "Instalado: %s", hasLink || hasDraw ? (installed[0] ? installed : "(version desconocida)") : "nada");
    putText(0, 4, C_TEXT, C_BG, l);
    snprintf(l, sizeof l, "  NativeLink %s, %s", hasLink ? "copiado" : "falta", plug ? "activado" : "sin activar en plugins.txt");
    putText(0, 5, hasLink && plug ? C_OK : C_DIM, C_BG, l);
    snprintf(l, sizeof l, "  NativeDraw %s, %s", hasDraw ? "copiado" : "falta", tai ? "en config.txt" : "fuera de config.txt");
    putText(0, 6, hasDraw && tai ? C_OK : C_DIM, C_BG, l);
    if (stale) putText(0, 7, C_BAD, C_BG, "  Hay restos de GePatch/gepresent en la Vita.");
    if (need_reboot) putText(0, 8, C_TITLE, C_BG, "  Cambio config.txt: reinicia la Vita para que valga.");
    for (int i = 0; i < 5; i++) {
      fillRow(10 + i, i == sel ? C_SEL : C_BG);
      putText(1, 10 + i, C_TEXT, i == sel ? C_SEL : C_BG, op[i]);
    }
    putText(0, ROWS - 1, C_DIM, C_BG, "arriba/abajo: elegir   X: aceptar");
    uint32_t b;
    while (!(b = buttons()));
    if (b & SCE_CTRL_UP) sel = (sel + 4) % 5;
    if (b & SCE_CTRL_DOWN) sel = (sel + 1) % 5;
    if (!(b & SCE_CTRL_CROSS)) continue;
    nlog = 0;
    if (sel == 0) { install(); showLog(" O: volver"); }
    else if (sel == 1) { say(C_TEXT, "Se borran NativeLink, NativeDraw y sus lineas en plugins.txt"); say(C_TEXT, "y config.txt (con respaldo)."); if (confirm("Desinstalar")) { nlog = 0; uninstall(); showLog(" O: volver"); } }
    else if (sel == 2) {
      if (!leftovers(0)) { say(C_OK, "No hay restos de pruebas."); showLog(" O: volver"); }
      else if (confirm("Se borra esto (restos de GePatch, gepresent y pruebas):")) { nlog = 0; say(C_TITLE, "Limpiar restos de pruebas"); leftovers(1); say(C_OK, need_reboot ? "Listo. Reinicia la Vita." : "Listo."); showLog(" O: volver"); }
    }
    else if (sel == 3) scePowerRequestColdReset();
    else break;
  }
  sceKernelExitProcess(0);
  return 0;
}
