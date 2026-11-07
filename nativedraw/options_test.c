// options_test.c: test of options.c on the PC (tools/test_ci.sh): the Sce file calls go to a temporary file. Checks
// that optStr/optOn read "name=value" lines (missing, "no", prefix names, CRLF) and that optSet rewrites one line and
// keeps the rest. Exit code 0 = pass. Build: gcc -I<psp2 headers> options_test.c
#include "options.c"  // (first: the host's sys/stat.h defines st_ctime, a field name of the psp2 headers)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

static char tmp[64] = "/tmp/options_test_XXXXXX";
SceUID sceIoOpen(const char *p, int f, SceMode m) { return open(tmp, (f & SCE_O_WRONLY ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY), 0666); }
int sceIoRead(SceUID fd, void *b, SceSize n) { return read(fd, b, n); }
int sceIoWrite(SceUID fd, const void *b, SceSize n) { return write(fd, b, n); }
int sceIoClose(SceUID fd) { return close(fd); }
SceSize sceClibStrnlen(const char *s, SceSize n) { return strnlen(s, n); }
int sceClibStrncmp(const char *a, const char *b, SceSize n) { return strncmp(a, b, n); }
void *sceClibMemcpy(void *d, const void *s, SceSize n) { return memcpy(d, s, n); }
int sceClibSnprintf(char *b, SceSize n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void) {
  int fd = mkstemp(tmp);
  const char *text = "# comment\nlog_level=extreme\r\nhold_game=yes\nhold=no\ndraw=\ncpu_verts=no\nreplay=ux0:data/x.bin \ncapture_core=1";
  write(fd, text, strlen(text)); close(fd);
  optLoad();
  CHECK(optOn("hold_game") && !optOn("hold") && optOn("draw"));          // yes, no, empty value = on
  CHECK(!optOn("cpu_verts") && !optOn("fx_log") && !optOn("log"));       // no, missing, prefix of a name
  CHECK(optStr("log_level")[0] == 'e' && optStr("capture_core")[0] == '1');
  CHECK(!strncmp(optStr("replay"), "ux0:data/x.bin", 14));
  optSet("replay", "no");
  CHECK(!optOn("replay") && optOn("hold_game") && optStr("capture_core")[0] == '1');
  memset(opt, 0, sizeof opt); optLoad();  // the file was rewritten the same way
  CHECK(!optOn("replay") && optOn("hold_game") && !optOn("cpu_verts") && optStr("log_level")[0] == 'e');
  CHECK(strstr(opt, "# comment\n") == opt && !strstr(opt, "ux0:data/x.bin"));
  unlink(tmp);
  printf(fails ? "options: %d failures\n" : "ok   options\n", fails);
  return fails != 0;
}
