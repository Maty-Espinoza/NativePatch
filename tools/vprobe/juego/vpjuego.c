// vpjuego.prx (B1): cuanto trabaja el GE y cuanto le sobra a la CPU en un juego real, con o sin GePatch.
//
// Plugin de kernel para juegos (plugins.txt). No engancha nada: solo mira.
// - Un hilo muestrea el GE cada ~250 us con sceGeGetReg (API oficial, solo lectura):
//   EXEC bit 0 = el GE tiene una lista. Si ademas la direccion actual es la de stall, el GE esta esperando al
//   juego (o a GePatch), no trabajando. Ocupado = lista en curso y no detenido en el stall.
// - Cuenta los flips mirando sceDisplayGetFrameBuf (cambia la direccion mostrada).
// - Un hilo de prioridad minima cuenta vueltas de un bucle: lo que avanza es CPU libre (calibrado al iniciar).
// Cada 2 s escribe una linea en ms0:/ge_patch_vpjuego_<ID>_<con|sin>.log (el relevo lo baja solo; un archivo
// por juego y por GePatch cargado o no, para que una corrida no pise a la otra).
// v1 (2026-10-26): "GE por frame" salia 100 veces mas grande y la calibracion de la CPU libre incluia la lectura
// del reloj en cada vuelta (daba ~300 %).
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspinit.h>
#include <psppower.h>
#include <psploadcore.h>
#include <string.h>
#include <stdio.h>
#include <systemctrl.h>

PSP_MODULE_INFO("VPJuego", 0x1007, 1, 0);

static char logf[64] = "ms0:/ge_patch_vpjuego.log";
#define now sceKernelGetSystemTimeLow
static int (*getReg)(int);
static volatile u32 spin, fin;
static u32 vueltasPorMs;  // calibracion del hilo de CPU libre

static void lg(const char *s) {
  SceUID fd = sceIoOpen(logf, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
  if (fd >= 0) { sceIoWrite(fd, s, strlen(s)); sceIoClose(fd); }
}
#define LG(...) do { char _b[256]; snprintf(_b, sizeof _b, __VA_ARGS__); lg(_b); } while (0)

static int libre(SceSize a, void *p) {  // prioridad minima: solo corre cuando nadie mas quiere la CPU
  while (!fin) { spin++; __asm__ volatile("" ::: "memory"); }
  return 0;
}

static int muestreo(SceSize a, void *p) {
  sceKernelDelayThread(3000000);  // que arranque el juego
  SceModule *gp = sceKernelFindModuleByName("GePatch");
  char id[11] = "juego";  // como GePatch: el ID sale de UMD_DATA.BIN (ISO/UMD)
  SceUID fd = sceIoOpen("disc0:/UMD_DATA.BIN", PSP_O_RDONLY, 0);
  if (fd >= 0) { if (sceIoRead(fd, id, 10) == 10) id[10] = 0; sceIoClose(fd); }
  snprintf(logf, sizeof logf, "ms0:/ge_patch_vpjuego_%s_%s.log", id, gp ? "con" : "sin");
  sceIoRemove(logf);
  LG("vpjuego 2 | juego %s | GePatch: %s | cpu %d MHz | CPU libre: %u vueltas/ms = 100 %%\n",
     id, gp ? "cargado" : "no", scePowerGetCpuClockFrequencyInt(), vueltasPorMs);
  lg("seg  fps   GE ocupado  GE en stall  GE por frame  CPU libre  muestras (cada us)\n");
  void *last = 0;
  u32 t0 = now(), s0 = spin, n = 0, busy = 0, stall = 0, flips = 0, seg = 0;
  for (;;) {
    sceKernelDelayThread(250);
    n++;
    u32 e = getReg(3);
    if (e & 1) {
      u32 la = getReg(5), st = getReg(6);
      if (st && (la & 0x0FFFFFFF) == (st & 0x0FFFFFFF)) stall++; else busy++;
    }
    void *top; int w, f;
    if (sceDisplayGetFrameBuf(&top, &w, &f, 0) >= 0 && top != last) { flips++; last = top; }
    u32 dt = now() - t0;
    if (dt >= 2000000) {
      seg += 2;
      // en decimas, sin coma flotante (la libc de kernel no la imprime)
      u32 fps10 = (u32)((u64)flips * 10000000 / dt), ocup10 = busy * 1000 / n, st10 = stall * 1000 / n;
      u32 libre10 = vueltasPorMs ? (u32)((u64)(spin - s0) * 1000 / vueltasPorMs * 1000 / dt) : 0;  // (ms libres / ms) * 1000
      u32 gems100 = fps10 ? (u32)((u64)ocup10 * 1000 / fps10) : 0;  // ms de GE por frame * 100 (ocup/1000 * 10000/fps10 ms)
      LG("%3u %3u.%u %6u.%u %% %8u.%u %% %8u.%02u ms %6u.%u %% %7u (%u)\n", seg, fps10 / 10, fps10 % 10, ocup10 / 10, ocup10 % 10,
         st10 / 10, st10 % 10, gems100 / 100, gems100 % 100, libre10 / 10, libre10 % 10, n, dt / n);
      t0 = now(); s0 = spin; n = busy = stall = flips = 0;
    }
  }
  return 0;
}

int module_start(SceSize args, void *argp) {
  getReg = (void *)sctrlHENFindFunction("sceGE_Manager", "sceGe_driver", 0xB415364D);
  if (!getReg) { lg("vpjuego: no se encontro sceGeGetReg; no se mide\n"); return 0; }
  // Calibracion: el mismo bucle que el hilo de CPU libre, 2 millones de vueltas (antes de que arranque el juego)
  u32 t = now();
  for (int k = 0; k < 2000000; k++) { spin++; __asm__ volatile("" ::: "memory"); }
  t = now() - t;
  vueltasPorMs = t ? (u32)(2000000ULL * 1000 / t) : 0;
  SceUID a = sceKernelCreateThread("vp_muestreo", muestreo, 16, 0x2000, 0, NULL);
  SceUID b = sceKernelCreateThread("vp_libre", libre, 126, 0x1000, 0, NULL);
  if (a >= 0) sceKernelStartThread(a, 0, NULL);
  if (b >= 0) sceKernelStartThread(b, 0, NULL);
  return 0;
}

int module_stop(SceSize args, void *argp) { fin = 1; return 0; }
