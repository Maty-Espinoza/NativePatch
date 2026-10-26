// EBOOT KBENCH: muestra el estado del ARM, arranca la batería de kbench.prx y muestra el avance.
//
// No enlaza la biblioteca KBench: Adrenaline 8 carga los plugins justo antes de arrancar el EBOOT,
// cuando sus importaciones ya se resolvieron, y quedan sin enlazar. Por eso busca las funciones con
// sctrlHENFindFunction y las llama en modo kernel con kuKernelCall (KUBridge). Si kbench.prx no está
// cargado, intenta cargarlo él mismo desde ms0:/seplugins/. Todo queda en ms0:/kbench/eboot.txt.
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psppower.h>
#include <string.h>
#include <stdio.h>
#include <systemctrl.h>
#include <kubridge.h>

PSP_MODULE_INFO("KBENCH", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(256);  // sin esto el heap toma toda la memoria de usuario y kbench.prx no puede reservar sus 2 MB
#define printf pspDebugScreenPrintf

// NID = primeros 4 bytes del SHA-1 del nombre (psp-build-exports)
enum { F_VERSION, F_PROBE, F_START, F_STATUS };
static const u32 NID[4] = {0x787A8856, 0xCB7406B1, 0xF125595F, 0x67198CD8};
static void *fn[4];
static char diag[1024];
static int nd;

static void note(const char *s) {  // a pantalla y al archivo de diagnóstico
  printf("%s", s);
  int l = strlen(s);
  if (nd + l < (int)sizeof diag) { memcpy(diag + nd, s, l); nd += l; }
  sceIoMkdir("ms0:/kbench", 0777);
  SceUID fd = sceIoOpen("ms0:/kbench/eboot.txt", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd >= 0) { sceIoWrite(fd, diag, nd); sceIoClose(fd); }
}
#define NOTE(...) do { char _b[200]; snprintf(_b, sizeof _b, __VA_ARGS__); note(_b); } while (0)

static int call(int f, u32 a1, u32 a2) {
  KernelCallArg k = {0};
  k.arg1 = a1; k.arg2 = a2;
  int r = kuKernelCall(fn[f], &k);
  return r < 0 ? r : (int)k.ret1;
}

static int find(void) {
  for (int i = 0; i < 4; i++) {
    fn[i] = (void *)sctrlHENFindFunction("KBench", "KBench", NID[i]);
    if (!fn[i]) return 0;
  }
  return 1;
}

static u32 boton(int segundos) {  // espera un botón nuevo; con segundos > 0, devuelve 0 al vencer
  SceCtrlData c; u32 antes = 0xFFFFFFFF, t0 = sceKernelGetSystemTimeLow();
  for (;;) {
    if (segundos > 0 && sceKernelGetSystemTimeLow() - t0 > (u32)segundos * 1000000) return 0;
    sceKernelDelayThread(10000);
    sceCtrlReadBufferPositive(&c, 1);
    u32 nuevo = c.Buttons & ~antes;
    antes = c.Buttons;
    if (nuevo & (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE | PSP_CTRL_CIRCLE)) return nuevo;
  }
}

int main(void) {
  static char b[160];
  pspDebugScreenInit();
  sceCtrlSetSamplingCycle(0);
  scePowerSetClockFrequency(333, 333, 166);
  note("KBENCH 1 - latencia PSP <-> ARM\n\n");

  int ok = find();
  NOTE("kbench.prx cargado por Adrenaline: %s\n", ok ? "si" : "no");
  if (!ok) {  // cargarlo aquí (por ejemplo, si plugins.txt no lo activa para homebrew)
    SceUID m = kuKernelLoadModule("ms0:/seplugins/kbench.prx", 0, NULL);
    int st = 0, r = m >= 0 ? sceKernelStartModule(m, 0, NULL, &st, NULL) : m;
    NOTE("carga manual: load=%08x start=%08x\n", m, r);
    ok = find();
  }
  if (!ok) {
    NOTE("No se encontro la biblioteca KBench (version %p probe %p start %p status %p).\n", fn[0], fn[1], fn[2], fn[3]);
    note("Revisa que exista ms0:/seplugins/kbench.prx.\n\nO: salir\n");
    while (!(boton(0) & PSP_CTRL_CIRCLE));
    sceKernelExitGame(); return 0;
  }
  int v = call(F_VERSION, 0, 0);
  NOTE("version de kbench.prx: %d\n", v);
  if (v != 1) { note("Version distinta: copia el kbench.prx y el EBOOT de la misma compilacion.\n\nO: salir\n"); while (!(boton(0) & PSP_CTRL_CIRCLE)); sceKernelExitGame(); return 0; }

  printf("Consultando al ARM (hasta 30 s)...\n");
  int listo = call(F_PROBE, (u32)b, sizeof b);
  NOTE("CPU %d MHz. ARM: %s\n\n", scePowerGetCpuClockFrequencyInt(), b);
  if (listo != 1) note("Sin ARM solo se miden las pruebas del PSP (recorrido y cache).\nDetalle del ARM: ux0:data/kbench/arm.log\n\n");
  printf("X: correr la bateria (~2 min)\nTriangulo: correr tambien la prueba experimental (vista directa)\nO: salir\n");
  printf("Sin tocar nada, la bateria (X) empieza sola en 10 s.\n");
  u32 k = boton(10);
  if (!k) k = PSP_CTRL_CROSS;
  if (k & PSP_CTRL_CIRCLE) { sceKernelExitGame(); return 0; }
  NOTE("inicio: %08x\n", call(F_START, (k & PSP_CTRL_TRIANGLE) ? 1 : 0, 0));
  int y = pspDebugScreenGetY(), st;
  do {
    sceKernelDelayThread(200000);
    st = call(F_STATUS, (u32)b, sizeof b);
    pspDebugScreenSetXY(0, y + 1);
    printf("%-67.67s\n", b);
  } while (st == 1);
  NOTE("estado final %d: %s\n", st, b);
  printf("\nListo. Resultados en ms0:/kbench/ (resumen.txt y .csv)\nO: salir\n");
  while (!(boton(0) & PSP_CTRL_CIRCLE));
  sceKernelExitGame();
  return 0;
}
