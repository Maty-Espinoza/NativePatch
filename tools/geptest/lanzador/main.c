// Lanzador de la bateria GEPTEST. El plugin geptest.prx (game.txt) anota la prueba siguiente
// en ms0:/geptest/pendiente.txt; este sale al menu como cualquier homebrew y el plugin,
// cargado en el menu (vsh.txt), la abre desde ahi.
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspiofilemgr.h>
PSP_MODULE_INFO("GEPTEST", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

int main(void) {
  char s[8] = {0};
  int i;
  pspDebugScreenInit();
  pspDebugScreenPrintf("GEPTEST: bateria de pruebas del GE\n\nPreparando...\n");
  for (i = 0; i < 40; i++) { // hasta 4 s a que el plugin anote la pendiente
    sceKernelDelayThread(100000);
    SceUID fd = sceIoOpen("ms0:/geptest/pendiente.txt", PSP_O_RDONLY, 0);
    int n = fd >= 0 ? sceIoRead(fd, s, sizeof(s) - 1) : 0;
    if (fd >= 0) sceIoClose(fd);
    if (n > 0 && s[0] >= '0' && s[0] <= '9') break;
  }
  if (i == 40) {
    pspDebugScreenPrintf("\nNo arranco: falta 'ms0:/seplugins/geptest.prx 1' en game.txt\n");
    for (;;) sceKernelDelayThread(1000000);
  }
  pspDebugScreenPrintf("Prueba %s: volviendo al menu, se abre sola en unos segundos\n", s);
  sceKernelDelayThread(500000);
  sceKernelExitGame();
  return 0;
}
