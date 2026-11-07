// options.c: the options file of NativeDraw (options.h): ux0:data/nativedraw/options.txt, one "name=value" per line,
// kept in memory from optLoad (called once by nativedraw.c when Adrenaline opens). Only Sce calls, so
// options_test.c can run it on the PC with stubs.
#include <psp2/types.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include "options.h"
#define OPTF "ux0:data/nativedraw/options.txt"
static char opt[2048];
void optLoad(void) {
  SceUID fd = sceIoOpen(OPTF, SCE_O_RDONLY, 0);
  if (fd < 0) return;
  int n = sceIoRead(fd, opt, sizeof opt - 1);
  sceIoClose(fd);
  opt[n > 0 ? n : 0] = 0;
}
const char *optStr(const char *name) {
  int l = sceClibStrnlen(name, 32);
  for (const char *p = opt; *p;) {
    if (!sceClibStrncmp(p, name, l) && p[l] == '=') return p + l + 1;
    while (*p && *p != '\n') p++;
    if (*p) p++;
  }
  return NULL;
}
int optOn(const char *name) {
  const char *v = optStr(name);
  return v && !(v[0] == 'n' && v[1] == 'o');
}
void optSet(const char *name, const char *value) {  // copies every other line, adds name's at the end
  char b[sizeof opt];
  int n = 0, l = sceClibStrnlen(name, 32);
  for (const char *p = opt; *p;) {
    const char *e = p;
    while (*e && *e != '\n') e++;
    if (*e) e++;
    if ((sceClibStrncmp(p, name, l) || p[l] != '=') && n + (e - p) < (int)sizeof b - 64) { sceClibMemcpy(b + n, p, e - p); n += e - p; }
    p = e;
  }
  if (n && b[n - 1] != '\n') b[n++] = '\n';
  n += sceClibSnprintf(b + n, sizeof b - n, "%s=%s\n", name, value);
  sceClibMemcpy(opt, b, n + 1);
  SceUID fd = sceIoOpen(OPTF, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (fd >= 0) { sceIoWrite(fd, b, n); sceIoClose(fd); }
}

