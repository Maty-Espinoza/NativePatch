// VPROBE 6: mapa de la VRAM, costo de mover memoria y margen del GE (prueba de bajo costo, ver
// docs/pruebas_hardware_vita.md)
//
// Homebrew suelto: no usa plugins, no toca ur0:tai ni juegos. Lo que necesita modo kernel (leer la RAM
// alta, registros del GE por la API oficial) lo hace con KUBridge (kuKernelPeekw/Memcpy/Call).
// Responde:
//   1. Base E (solo lectura): que dice el driver del GE sobre la EDRAM (tamano de hardware, registros).
//   2. Mapa: la VRAM del PSP es la RAM 0x0BE00000? (CPU y GE) Si no, la busca en 0x0A000000-0x0C000000.
//   3. Costo de mover memoria: TRANSFER del GE, DMA y CPU, entre VRAM y RAM, 64 KB a 1 MB.
//   4. Paginacion simulada: sacar la pantalla 960x544 de la VRAM, dibujar un rt, devolverla.
//   5. Relleno del GE: borrar, texturas (sprite unico / franjas, lineal / swizzle, RAM / VRAM) y las dos
//      operaciones de GePatch (reducir la pantalla a 480x272 y ampliar 480x272 a 960x544).
//   6. 3D al doble: la misma escena con profundidad y textura a 480x272 y a 960x544.
//   7. GE y CPU a la vez: cuanto se frena la CPU (recorrido tipo lista, memcpy) mientras el GE copia o dibuja.
//   8. Ruta E, exploracion de solo lectura: latencia de la VRAM frente a la RAM, mapa de los reflejos
//      0x04200000-0x047FFFFF (CPU y GE) y version del Tachyon.
//   9. Ruta E con escritura de registros (SOLO con Triangulo): forzar el modo de 4 MB de la EDRAM y ver si
//      aparece memoria nueva en 0x04200000. Puede cerrar Adrenaline; restaura los registros al terminar.
// Resultados: ms0:/ge_patch_vprobe.log (el relevo baja solo ge_patch*.log de la raiz de ux0:/pspemu).
// Se escribe linea a linea: si algo cae, queda lo anterior.
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspdmac.h>
#include <pspdisplay.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <systemctrl.h>
#include <kubridge.h>

PSP_MODULE_INFO("VPROBE", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(12 * 1024);

#define VER 6
#define VB 0x04088000u            // zona de pruebas de la VRAM (0x04000000-0x04088000 es el texto en pantalla)
#define VEND 0x04200000u          // de aqui en adelante es el reflejo: no se escribe
#define VTEX 0x04198000u          // textura de 256 KB en la VRAM (despues de la pantalla de pruebas)
#define LOGF "ms0:/ge_patch_vprobe.log"
#define UNC(a) ((a) | 0x40000000u) // vista sin cache para la CPU
#define now sceKernelGetSystemTimeLow

static unsigned int __attribute__((aligned(64))) glist[256 * 1024];  // lista principal (1 MB)
static unsigned int __attribute__((aligned(64))) clist[256 * 1024];  // lista llamada (se arma antes de medir)
static u8 *ram1, *ram2;  // 2 x 2 MB en la RAM del PSP (partición del homebrew)

// ---------- salida ----------
static void out(const char *s) {
  pspDebugScreenPrintf("%s", s);
  SceUID fd = sceIoOpen(LOGF, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
  if (fd >= 0) { sceIoWrite(fd, s, strlen(s)); sceIoClose(fd); }
}
#define OUT(...) do { char _b[256]; snprintf(_b, sizeof _b, __VA_ARGS__); out(_b); } while (0)

// ---------- kernel ----------
static u32 kcall(u32 f, u32 a1, u32 a2) {
  KernelCallArg k = {0};
  k.arg1 = a1; k.arg2 = a2;
  return kuKernelCall((void *)f, &k) < 0 ? 0xDEADBEEF : k.ret1;
}
static u32 kfind(const char *mod, const char *lib, u32 nid) { return sctrlHENFindFunction(mod, lib, nid); }

// ---------- GE ----------
static void geStart(void) { sceGuStart(GU_DIRECT, glist); }
static u32 geEnd(void) {  // termina la lista principal y mide lo que tarda el GE en ejecutarla
  sceKernelDcacheWritebackAll();
  u32 t = now();
  sceGuFinish();
  sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
  return now() - t;
}
// Ejecuta una lista ya armada (clist) desde la principal: mide solo al GE, no a la CPU que arma la lista.
static u32 geRunCall(void) {
  geStart();
  sceGuCallList(clist);
  return geEnd();
}
// Destino del dibujo (modo through). stride = ancho de linea en pixeles (v2 lo fijaba en 1024: el rt de 512
// de la paginacion se dibujaba con otro ancho y salia "SIN DIBUJAR").
static void fbSetup(u32 vaddr, int psm, int w, int h, int stride) {
  sceGuDrawBuffer(psm, (void *)(vaddr - 0x04000000), stride);
  sceGuSendCommandi(0x15, 0);                          // REGION1
  sceGuSendCommandi(0x16, ((h - 1) << 10) | (w - 1));   // REGION2
  sceGuSendCommandi(0xD4, 0);                           // SCISSOR1 (sceGuScissor no envia nada si
  sceGuSendCommandi(0xD5, ((h - 1) << 10) | (w - 1));   // SCISSOR2  GU_SCISSOR_TEST esta apagado: v1 dibujaba 1 px)
  sceGuOffset(2048 - w / 2, 2048 - h / 2);
  sceGuViewport(2048, 2048, w, h);
  sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_ALPHA_TEST); sceGuDisable(GU_BLEND); sceGuDisable(GU_TEXTURE_2D);
  sceGuDepthMask(GU_TRUE);
}
// Borrado propio (sceGuClear usa el tamano de sceGuDispBuffer): modo CLEAR + franjas de 64 px
typedef struct { u32 c; s16 x, y, z, pad; } VC8;
static void clrf(int w, int h, u32 c, int flags) {
  int n = (w + 63) / 64;
  VC8 *v = sceGuGetMemory(n * 2 * sizeof(VC8));
  for (int i = 0; i < n; i++) {
    v[i * 2] = (VC8){c, i * 64, 0, 0, 0};
    v[i * 2 + 1] = (VC8){c, i * 64 + 64 > w ? w : i * 64 + 64, h, 0, 0};
  }
  sceGuSendCommandi(0xD3, (flags << 8) | 1);
  sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, n * 2, 0, v);
  sceGuSendCommandi(0xD3, 0);
}
#define clr(w, h, c) clrf(w, h, c, GU_COLOR_BUFFER_BIT)

// ---------- 1. Base E ----------
static void baseE(void) {
  out("\n[1] Base E: EDRAM segun el driver del GE (solo lectura)\n");
  OUT("modelo (kuKernelGetModel)=%d  sctrlHENGetVersion=%08x  devkit=%08x\n",
      kuKernelGetModel(), sctrlHENGetVersion(), sceKernelDevkitVersion());
  OUT("cpu=%d MHz bus=%d MHz  memoria libre=%d KB (mayor bloque %d KB)\n", scePowerGetCpuClockFrequencyInt(),
      scePowerGetBusClockFrequencyInt(), sceKernelTotalFreeMemSize() >> 10, sceKernelMaxFreeMemSize() >> 10);
  OUT("sceGeEdramGetAddr=%p GetSize=%d KB\n", sceGeEdramGetAddr(), sceGeEdramGetSize() >> 10);
  static const u32 hw[] = {0x244A3948, 0xC576E897, 0x547EC5F0};  // sceGeEdramGetHwSize (6.6x y anterior)
  for (int i = 0; i < 3; i++) {
    u32 f = kfind("sceGE_Manager", "sceGe_driver", hw[i]);
    if (f) { OUT("sceGeEdramGetHwSize (nid %08x)=%d KB\n", hw[i], kcall(f, 0, 0) >> 10); break; }
    if (i == 2) out("sceGeEdramGetHwSize: no encontrada\n");
  }
  u32 getReg = kfind("sceGE_Manager", "sceGe_driver", 0xB415364D);
  if (!getReg) getReg = kfind("sceGE_Manager", "sceGe_driver", 0x1FDB4C62);
  static const char *nom[32] = {"RESET", "UNK004", "EDRAM_HW_SIZE", "EXEC", "UNK104", "LISTADDR", "STALLADDR",
    "RADR1", "RADR2", "VADR", "IADR", "OADR", "OADR1", "OADR2", "UNK300", "INT1", "INT2", "INT3", "INT4",
    "EDRAM_ENABLED_SIZE", "GEOMETRY_CLOCK", "EDRAM_REFRESH1", "EDRAM_UNK10", "EDRAM_REFRESH2", "EDRAM_REFRESH3",
    "EDRAM_UNK40", "EDRAM_UNK50", "EDRAM_UNK60", "EDRAM_TRANS_DISABLE", "EDRAM_TRANS_VALUE", "EDRAM_UNK90", "EDRAM_UNKA0"};
  if (!getReg) out("sceGeGetReg: no encontrada\n");
  else for (int r = 0; r < 32; r++) {
    u32 v = kcall(getReg, r, 0);
    OUT("  reg %2d %-20s = %08x%s\n", r, nom[r], v,
        r == 2 ? "   (bits 0-15 << 10 = tamano de hardware)" : r == 19 ? "   (4 = 2 MB, 2 = 4 MB)" : "");
  }
  // SetSize(4 MB): en uOFW falla antes de tocar nada si la version del Tachyon es <= 0x4FFFFF (PSP 1000).
  u32 setSize = kfind("sceGE_Manager", "sceGe_driver", 0x5BAA5439);
  if (setSize) {
    u32 r = kcall(setSize, 0x400000, 0);
    OUT("sceGeEdramSetSize(4 MB)=%08x%s\n", r, r == 0x80000104 ? " (rechazo por modelo: el chequeo es de software)" : "");
    if (r == 0) { OUT("  ACEPTADO: ahora GetSize=%d KB. Se vuelve a 2 MB: %08x\n", sceGeEdramGetSize() >> 10, kcall(setSize, 0x200000, 0)); }
  }
  u8 a[32]; kuKernelMemcpy(a, (void *)0xABCDE000, 32);  // estructura compartida de Adrenaline (lectura)
  out("RAM 0x0BCDE000:"); for (int i = 0; i < 32; i++) OUT(" %02x", a[i]); out("\n");
}

// ---------- 2. Mapa VRAM <-> RAM ----------
static int sameAt(u32 vramOff, u32 ramAddr) {  // escribe con la CPU en la VRAM y lee la RAM alta con el kernel
  volatile u32 *v = (u32 *)UNC(0x04000000 + vramOff);
  u32 old = *v, pat = 0x5A5A0000 ^ vramOff ^ now();
  *v = pat;
  u32 r = kuKernelPeekw((void *)(0xA0000000 | ramAddr));
  *v = old;
  return r == pat;
}
static void mapa(void) {
  out("\n[2] Mapa VRAM <-> RAM\n");
  static const u32 off[] = {VB - 0x04000000, 0x100000, 0x1FFFF0};
  int ok = 0;
  for (int i = 0; i < 3; i++) {
    int s = sameAt(off[i], 0x0BE00000 + off[i]);
    ok += s;
    OUT("CPU escribe VRAM+%06x -> RAM 0x%08x: %s\n", off[i], 0x0BE00000 + off[i], s ? "IGUAL" : "distinto");
  }
  // Al reves: el kernel escribe la RAM alta y la CPU lee la VRAM (solo si ya se vio que es la VRAM)
  if (ok) {
    volatile u32 *v = (u32 *)UNC(0x04100000);
    u32 old = *v, pat = 0xA5A50000 ^ now();
    kuKernelPokew((void *)0xABF00000, pat);
    OUT("kernel escribe RAM 0x0BF00000 -> VRAM+100000: %s\n", *v == pat ? "IGUAL" : "distinto");
    *v = old;
  }
  // El GE: borra un rectangulo en la VRAM y se lee la RAM alta
  geStart(); fbSetup(0x04100000, GU_PSM_8888, 64, 8, 64);
  clr(64, 8, 0xC0FFEE11);
  geEnd();
  u32 r = kuKernelPeekw((void *)(0xA0000000 | 0x0BF00000));
  OUT("GE borra VRAM+100000 con c0ffee11 -> RAM 0x0BF00000 = %08x: %s\n", r, r == 0xC0FFEE11 ? "IGUAL" : "distinto");
  if (ok == 3) return;
  // No esta en 0x0BE00000: buscar una firma de 16 bytes en 0x0A000000-0x0C000000
  // v3 buscaba desde 0x08000000 y Adrenaline se caia al leer la parte baja (kernel): se vuelve a la zona alta
  out("Buscando la VRAM en la RAM (0x0A000000-0x0C000000)...\n");
  volatile u32 *s = (u32 *)UNC(0x04180000);
  u32 inv[4], sv[4];  // la firma se guarda invertida para no encontrarse a si misma en la pila
  for (int i = 0; i < 4; i++) { sv[i] = s[i]; inv[i] = ~(0x13579BDF * (i + 1) ^ now()); s[i] = ~inv[i]; }
  u32 hits = 0;
  for (u32 a = 0x0A000000; a < 0x0C000000; a += 0x10000) {
    kuKernelMemcpy(ram2, (void *)(0xA0000000 | a), 0x10000);
    u32 *w = (u32 *)ram2;
    for (int j = 0; j < 0x4000 - 3; j++)
      if (w[j] == ~inv[0] && w[j + 1] == ~inv[1] && w[j + 2] == ~inv[2] && w[j + 3] == ~inv[3]) {
        OUT("  firma en RAM 0x%08x (VRAM+180000 -> base 0x%08x)\n", a + j * 4, a + j * 4 - 0x180000);
        hits++;
      }
  }
  for (int i = 0; i < 4; i++) s[i] = sv[i];
  OUT("  %u coincidencias (pila de VPROBE cerca de %p: una coincidencia ahi no cuenta)\n", hits, &hits);
}

// ---------- 3. Copias ----------
static u32 sum(const volatile u32 *p, u32 n) { u32 s = 0; for (u32 i = 0; i < n; i++) s = ((s << 1) | (s >> 31)) ^ p[i]; return s; }
static void fillPat(volatile u32 *p, u32 n, u32 seed) { for (u32 i = 0; i < n; i++) p[i] = seed + i * 0x9E3779B1u; }
static void row(const char *qu, u32 bytes, u32 us, int ok) {
  OUT("%-26s %5u KB %7u us %7.1f MB/s %s\n", qu, bytes >> 10, us, us ? bytes / (float)us : 0, ok ? "ok" : "MAL");
}
// TRANSFER del GE en 32 bits, 512 px de ancho (2 KB por linea)
static u32 geCopy(u32 src, u32 dst, u32 bytes) {
  geStart();
  sceGuCopyImage(GU_PSM_8888, 0, 0, 512, bytes / 2048, 512, (void *)src, 0, 0, 512, (void *)dst);
  sceGuTexSync();
  return geEnd();
}
static void copias(void) {
  out("\n[3] Copias (promedio de 4)\n");
  static const u32 tam[] = {64 << 10, 256 << 10, 1 << 20};
  u32 VA = 0x04100000;  // 1 MB hasta VEND
  for (int i = 0; i < 3; i++) {
    u32 b = tam[i], n = b / 4, t, s0;
    // RAM -> VRAM
    fillPat((u32 *)ram1, n, 1); sceKernelDcacheWritebackInvalidateAll(); s0 = sum((u32 *)ram1, n);
    t = 0; for (int k = 0; k < 4; k++) t += geCopy((u32)ram1, VA, b);
    row("GE RAM->VRAM", b, t / 4, sum((u32 *)UNC(VA), n) == s0);
    // VRAM -> RAM
    memset(ram2, 0, b); sceKernelDcacheWritebackInvalidateAll();
    t = 0; for (int k = 0; k < 4; k++) t += geCopy(VA, (u32)ram2, b);
    sceKernelDcacheWritebackInvalidateAll();
    row("GE VRAM->RAM", b, t / 4, sum((u32 *)ram2, n) == s0);
    // RAM -> RAM
    memset(ram2, 0, b); sceKernelDcacheWritebackInvalidateAll();
    t = 0; for (int k = 0; k < 4; k++) t += geCopy((u32)ram1, (u32)ram2, b);
    sceKernelDcacheWritebackInvalidateAll();
    row("GE RAM->RAM", b, t / 4, sum((u32 *)ram2, n) == s0);
    if (b <= (256 << 10)) {  // VRAM -> VRAM (dentro de la zona de pruebas)
      t = 0; for (int k = 0; k < 4; k++) t += geCopy(VA, VB, b);
      row("GE VRAM->VRAM", b, t / 4, sum((u32 *)UNC(VB), n) == s0);
    }
    // DMA y CPU
    t = now(); for (int k = 0; k < 4; k++) sceDmacMemcpy((void *)VA, ram1, b); t = (now() - t) / 4;
    row("DMA RAM->VRAM", b, t, sum((u32 *)UNC(VA), n) == s0);
    memset(ram2, 0, b); sceKernelDcacheWritebackInvalidateAll();
    t = now(); for (int k = 0; k < 4; k++) sceDmacMemcpy(ram2, (void *)VA, b); t = (now() - t) / 4;
    sceKernelDcacheWritebackInvalidateAll();
    row("DMA VRAM->RAM", b, t, sum((u32 *)ram2, n) == s0);
    t = now(); for (int k = 0; k < 4; k++) memcpy(ram2, ram1, b); t = (now() - t) / 4;
    row("CPU RAM->RAM (cache)", b, t, sum((u32 *)ram2, n) == s0);
    t = now(); for (int k = 0; k < 4; k++) memcpy((void *)UNC(VA), ram1, b); t = (now() - t) / 4;
    row("CPU RAM->VRAM (sin cache)", b, t, sum((u32 *)UNC(VA), n) == s0);
    t = now(); for (int k = 0; k < 4; k++) memcpy(ram2, (void *)UNC(VA), b); t = (now() - t) / 4;
    row("CPU VRAM->RAM (sin cache)", b, t, sum((u32 *)ram2, n) == s0);
  }
}

// ---------- 4. Paginacion simulada ----------
// Pantalla 960x544 565 en VB (lo que hoy es el buffer grande). Un rt de 512x512 565 se dibuja al doble en la
// VRAM que deja libre la pantalla, y despues se devuelve todo.
static u32 sumScr(void) {  // suma de la pantalla 960x544 565 (sin las columnas 960-1023 de relleno)
  u32 s = 0;
  for (int y = 0; y < 544; y++) s = s * 31 + sum((u32 *)UNC(VB + y * 2048), 480);
  return s;
}
static void paginacion(void) {
  out("\n[4] Paginacion simulada (pantalla 960x544 565 = 1020 KB, rt 512x512 565 = 512 KB)\n");
  u32 scr = (u32)ram1, rt = (u32)ram2;  // copia de la pantalla y del rt en la RAM
  geStart(); fbSetup(VB, GU_PSM_5650, 960, 544, 1024); clr(960, 544, 0xFF3366CC); geEnd();
  sceKernelDcacheWritebackInvalidateAll();
  u32 s0 = sumScr();
  memset(ram2, 0, 512 * 512 * 2); sceKernelDcacheWritebackInvalidateAll();
  u32 t[5];
  for (int k = 0; k < 5; k++) {
    geStart();
    sceGuCopyImage(GU_PSM_5650, 0, 0, 960, 544, 1024, (void *)VB, 0, 0, 1024, (void *)scr);  // 1. sacar pantalla
    sceGuTexSync();
    sceGuCopyImage(GU_PSM_5650, 0, 0, 512, 512, 512, (void *)rt, 0, 0, 512, (void *)VB);     // 2. cargar el rt previo
    sceGuTexSync();
    fbSetup(VB, GU_PSM_5650, 512, 512, 512);                                                  // 3. dibujar en el rt
    clr(512, 512, 0xFF00FF00 + k);
    sceGuCopyImage(GU_PSM_5650, 0, 0, 512, 512, 512, (void *)VB, 0, 0, 512, (void *)rt);     // 4. guardar el rt
    sceGuTexSync();
    sceGuCopyImage(GU_PSM_5650, 0, 0, 960, 544, 1024, (void *)scr, 0, 0, 1024, (void *)VB);  // 5. devolver pantalla
    sceGuTexSync();
    t[k] = geEnd();
  }
  sceKernelDcacheWritebackInvalidateAll();
  int ok = sumScr() == s0;
  u16 px = ((u16 *)ram2)[512 * 512 - 1];
  OUT("ciclo completo (2 x 1020 KB + 2 x 512 KB por el GE): %u %u %u %u %u us -> pantalla %s, rt %s\n",
      t[0], t[1], t[2], t[3], t[4], ok ? "intacta" : "DISTINTA", px ? "dibujado entero" : "SIN DIBUJAR");
  // Solo sacar y devolver la pantalla (el caso minimo de cada cambio)
  u32 tt = 0;
  for (int k = 0; k < 4; k++) {
    geStart();
    sceGuCopyImage(GU_PSM_5650, 0, 0, 960, 544, 1024, (void *)VB, 0, 0, 1024, (void *)scr); sceGuTexSync();
    sceGuCopyImage(GU_PSM_5650, 0, 0, 960, 544, 1024, (void *)scr, 0, 0, 1024, (void *)VB); sceGuTexSync();
    tt += geEnd();
  }
  OUT("sacar + devolver pantalla: %u us\n", tt / 4);
  // Variante chica (lo que haria GePatch): sacar solo 512 KB del z, dibujar ahi un rt de 512x512 al doble,
  // guardarlo en la RAM (desde ahi el GE lo lee como textura) y devolver el z.
  u32 Z = 0x04100000, zs = (u32)ram1;
  memset(ram2, 0, 512 * 512 * 2); sceKernelDcacheWritebackInvalidateAll();
  tt = 0;
  for (int k = 0; k < 4; k++) {
    geStart();
    sceGuCopyImage(GU_PSM_5650, 0, 0, 512, 512, 512, (void *)Z, 0, 0, 512, (void *)zs); sceGuTexSync();
    fbSetup(Z, GU_PSM_5650, 512, 512, 512); clr(512, 512, 0xFF00FF00);  // v4: 0xFF0000FF+k daba 0 en 565
    sceGuCopyImage(GU_PSM_5650, 0, 0, 512, 512, 512, (void *)Z, 0, 0, 512, (void *)rt); sceGuTexSync();
    sceGuCopyImage(GU_PSM_5650, 0, 0, 512, 512, 512, (void *)zs, 0, 0, 512, (void *)Z); sceGuTexSync();
    tt += geEnd();
  }
  sceKernelDcacheWritebackInvalidateAll();
  OUT("variante z: sacar 512 KB de z + rt 512x512 + guardar rt + devolver z: %u us (rt %s)\n", tt / 4,
      ((u16 *)ram2)[512 * 512 - 1] ? "dibujado entero" : "SIN DIBUJAR");
}

// ---------- 5. Relleno del GE ----------
typedef struct { u16 u, v; s16 x, y, z; } VT;  // textura 16 bits, posicion 16 bits (10 bytes)
typedef struct { u16 c; s16 x, y, z; } VC;
static void swizzle(u8 *out, const u8 *in, u32 wb, u32 h) {  // bloques de 16 bytes x 8 lineas
  u32 *d = (u32 *)out;
  for (u32 y = 0; y < h; y += 8)
    for (u32 x = 0; x < wb; x += 16)
      for (u32 j = 0; j < 8; j++) { const u32 *p = (const u32 *)(in + (y + j) * wb + x); *d++ = p[0]; *d++ = p[1]; *d++ = p[2]; *d++ = p[3]; }
}
static void texSet(const void *tex, int w, int h, int tbw, int swz, int lin) {
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(GU_PSM_5650, 0, 0, swz);
  sceGuTexImage(0, w, h, tbw, tex);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuTexFilter(lin ? GU_LINEAR : GU_NEAREST, lin ? GU_LINEAR : GU_NEAREST);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  sceGuTexFlush();
}
static void sprite(int u0, int v0, int u1, int v1, int x0, int y0, int x1, int y1) {
  VT *v = sceGuGetMemory(2 * sizeof(VT));
  v[0] = (VT){u0, v0, x0, y0, 0}; v[1] = (VT){u1, v1, x1, y1, 0};
  sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}
// Textura tw x th estirada a la pantalla 960x544, en un sprite o en franjas de 64 px de ancho
static void stretch(int tw, int th, int franjas) {
  if (!franjas) { sprite(0, 0, tw, th, 0, 0, 960, 544); return; }
  for (int x = 0; x < 960; x += 64) {
    int x1 = x + 64 > 960 ? 960 : x + 64;
    sprite(x * tw / 960, 0, x1 * tw / 960, th, x, 0, x1, 544);
  }
}
// Copia escalada de un fb lineal 565 (src, ancho de linea sstride) de sw x sh a dw x dh en el destino actual.
// El GE no acepta texturas de mas de 512: se hace por franjas de destino de 'franja' px y, si sh > 512, en dos
// mitades; cada franja apunta la textura a su esquina (alineada a 16 bytes).
static void blit(u32 src, int sstride, int sw, int sh, int dw, int dh, int franja) {
  int partes = sh > 512 ? 2 : 1;
  for (int p = 0; p < partes; p++) {
    int sy0 = sh * p / partes, sy1 = sh * (p + 1) / partes, dy0 = dh * p / partes, dy1 = dh * (p + 1) / partes;
    for (int dx = 0; dx < dw; dx += franja) {
      int dx1 = dx + franja > dw ? dw : dx + franja, sx0 = dx * sw / dw, sx1 = dx1 * sw / dw;
      texSet((void *)(src + (sy0 * sstride + sx0) * 2), 512, 512, sstride, 0, 1);
      sprite(0, 0, sx1 - sx0, sy1 - sy0, dx, dy0, dx1, dy1);
    }
  }
}
static void fila(const char *nm, u32 us, float px) {
  OUT("%-40s %6u us  %6.1f Mpx/s  (%.1f por frame)\n", nm, us, px / us, 16667.0f / us);
}
static void relleno(void) {
  out("\n[5] Relleno del GE (destino 960x544 565)\n");
  u16 *lin = (u16 *)ram1, *swz = (u16 *)(ram1 + (1 << 20));  // textura 512x256 565, lineal y swizzle
  for (int i = 0; i < 512 * 256; i++) lin[i] = (u16)(i * 37 ^ (i >> 9) * 0x0821);
  swizzle((u8 *)swz, (u8 *)lin, 1024, 256);
  sceKernelDcacheWritebackInvalidateAll();
  memcpy((void *)UNC(VTEX), swz, 512 * 256 * 2);  // la misma textura swizzle en la VRAM
  // Borrar, comprobando que llega a la esquina
  sceGuStart(GU_CALL, clist); fbSetup(VB, GU_PSM_5650, 960, 544, 1024);
  for (int k = 0; k < 10; k++) clr(960, 544, 0xFF000000 | (k + 1) * 0x111111);
  sceGuFinish();
  volatile u16 *last = (u16 *)UNC(VB + 543 * 2048 + 959 * 2);
  *last = 0;
  fila("borrar", geRunCall() / 10, 960 * 544);
  if (!*last) out("  AVISO: el borrado no llego a la esquina 959,543\n");
  static const struct { const char *nm; int franjas, swz, vram, lin, blend, n; } m[] = {
    {"textura: 1 sprite, lineal, RAM (v2)", 0, 0, 0, 0, 0, 2},
    {"textura: franjas 64, lineal, RAM", 1, 0, 0, 0, 0, 10},
    {"textura: franjas 64, swizzle, RAM", 1, 1, 0, 0, 0, 10},
    {"textura: franjas 64, swizzle, VRAM", 1, 1, 1, 0, 0, 10},
    {"textura: franjas, swizzle, RAM, bilineal", 1, 1, 0, 1, 0, 10},
    {"  + mezcla alfa", 1, 1, 0, 1, 1, 10},
  };
  for (int i = 0; i < 6; i++) {
    sceGuStart(GU_CALL, clist); fbSetup(VB, GU_PSM_5650, 960, 544, 1024);
    texSet(m[i].vram ? (void *)VTEX : m[i].swz ? (void *)swz : (void *)lin, 512, 256, 512, m[i].swz, m[i].lin);
    if (m[i].blend) { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); }
    for (int k = 0; k < m[i].n; k++) stretch(512, 256, m[i].franjas);
    sceGuFinish();
    fila(m[i].nm, geRunCall() / m[i].n, 960 * 544);
  }
  // Las dos operaciones de GePatch: reducir la pantalla (VRAM) a 480x272 y ampliar 480x272 (RAM) a 960x544
  sceGuStart(GU_CALL, clist); fbSetup(VTEX, GU_PSM_5650, 480, 272, 512);
  for (int k = 0; k < 4; k++) blit(VB, 1024, 960, 544, 480, 272, 32);
  sceGuFinish();
  fila("GePatch: reducir 960x544 VRAM -> 480x272", geRunCall() / 4, 480 * 272);
  sceGuStart(GU_CALL, clist); fbSetup(VB, GU_PSM_5650, 960, 544, 1024);
  for (int k = 0; k < 4; k++) blit((u32)ram2, 512, 480, 272, 960, 544, 64);
  sceGuFinish();
  fila("GePatch: ampliar 480x272 RAM -> 960x544", geRunCall() / 4, 960 * 544);
  // Triangulos chicos: 20000 de 8x8 px, solo color
  sceGuStart(GU_CALL, clist); fbSetup(VB, GU_PSM_5650, 960, 544, 1024);
  VC *v = sceGuGetMemory(60000 * sizeof(VC));
  for (int i = 0; i < 20000; i++) {
    s16 x = (i * 13) % 950, y = (i * 7) % 534;
    v[i * 3] = (VC){0xF800, x, y, 0}; v[i * 3 + 1] = (VC){0x07E0, x + 8, y, 0}; v[i * 3 + 2] = (VC){0x001F, x, y + 8, 0};
  }
  sceGuDrawArray(GU_TRIANGLES, GU_COLOR_5650 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 60000, 0, v);
  sceGuFinish();
  u32 us = geRunCall();
  OUT("20000 triangulos de 8x8: %u us (%.0f ns por triangulo)\n", us, us * 1000.0f / 20000);
}

// ---------- 6. 3D al doble ----------
// La misma escena (3000 triangulos con textura swizzle y profundidad, ~3 capas) a 480x272 y a 960x544.
// A 960x544 usa toda la VRAM como GePatch (color 0x04000000, z 0x04100000): borra el texto de la pantalla.
static u32 escena(int esc, u32 col, u32 zb, int stride) {
  static u32 seed;
  seed = 12345;
  sceGuStart(GU_CALL, clist);
  fbSetup(col, GU_PSM_5650, 480 * esc, 272 * esc, stride);
  sceGuDepthBuffer((void *)(zb - 0x04000000), stride);
  sceGuDepthMask(GU_FALSE);  // escribir el z (tambien al borrarlo)
  clrf(480 * esc, 272 * esc, 0xFF202020, GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
  sceGuEnable(GU_DEPTH_TEST); sceGuDepthFunc(GU_GEQUAL);
  texSet(ram1 + (1 << 20), 256, 256, 512, 1, 1);  // la textura swizzle de [5] (ancho de linea 512)
  VT *v = sceGuGetMemory(9000 * sizeof(VT));
  for (int i = 0; i < 3000; i++) {
    seed = seed * 1103515245 + 12345; int x = (seed >> 8) % 464, y = (seed >> 20) % 256, z = seed & 0xFFFF;
    seed = seed * 1103515245 + 12345; int u = (seed >> 8) % 240, w = (seed >> 20) % 240;
    v[i * 3] = (VT){u, w, x * esc, y * esc, z};
    v[i * 3 + 1] = (VT){u + 16, w, (x + 16) * esc, y * esc, z};
    v[i * 3 + 2] = (VT){u, w + 16, x * esc, (y + 16) * esc, z};
  }
  sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 9000, 0, v);
  sceGuDisable(GU_DEPTH_TEST);
  sceGuFinish();
  u32 t = 0;
  for (int k = 0; k < 3; k++) t += geRunCall();
  return t / 3;
}
static void tres3d(void) {
  out("\n[6] 3D al doble (3000 triangulos con textura y profundidad, ~3 capas)\n");
  u32 a = escena(1, VB, 0x04100000, 512);
  u32 b = escena(2, 0x04000000, 0x04100000, 960);
  pspDebugScreenClear();
  OUT("480x272: %u us   960x544: %u us   -> al doble cuesta %.2f veces\n", a, b, b / (float)a);
}

// ---------- 7. GE y CPU a la vez ----------
// Trabajo de CPU parecido al recorrido de GePatch (lecturas dispersas en 2 MB con cache) y una copia memcpy,
// solos y mientras el GE trabaja (copias RAM->VRAM o dibujo con textura en la VRAM).
static volatile u32 sink, geFin;
static void onFinish(int a) { geFin = now(); }  // senal FINISH del GE: hora real en que termina la lista
static u32 cpuWalk(void) {
  u32 t = now(), s = 0, i = 1;
  const u32 *b = (const u32 *)ram1;
  for (int k = 0; k < 300000; k++) { i = i * 1103515245 + 12345; s += b[(i >> 9) & 0x7FFFF]; }
  sink = s;
  return now() - t;
}
static u32 cpuCopy(void) { u32 t = now(); memcpy(ram2, ram1, 1 << 20); return now() - t; }
static void geCarga(int tipo) {  // arma en clist ~20 ms de trabajo del GE
  sceGuStart(GU_CALL, clist);
  if (tipo == 0)
    for (int k = 0; k < 8; k++) { sceGuCopyImage(GU_PSM_8888, 0, 0, 512, 256, 512, ram1 + (1 << 20), 0, 0, 512, (void *)0x04100000); sceGuTexSync(); }
  else {
    fbSetup(VB, GU_PSM_5650, 960, 544, 1024);
    texSet((void *)VTEX, 512, 256, 512, 1, 1);
    for (int k = 0; k < 15; k++) stretch(512, 256, 1);
  }
  sceGuFinish();
}
static void contencion(void) {
  out("\n[7] GE y CPU a la vez\n");
  u32 (*job[2])(void) = {cpuWalk, cpuCopy};
  static const char *jn[2] = {"CPU recorrido (300k lecturas)", "CPU memcpy 1 MB"}, *gn[2] = {"GE copia RAM->VRAM", "GE dibuja (textura VRAM)"};
  for (int j = 0; j < 2; j++) {
    u32 solo = 0;
    for (int k = 0; k < 3; k++) solo += job[j]();
    solo /= 3;
    OUT("%s solo: %u us\n", jn[j], solo);
    for (int g = 0; g < 2; g++) {
      geCarga(g);
      u32 gsolo = geRunCall();
      geStart(); sceGuCallList(clist); sceKernelDcacheWritebackAll();
      u32 t0 = now();
      sceGuFinish();  // el GE arranca y la CPU sigue
      u32 c = job[j]();
      sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
      u32 tg = geFin - t0;  // v4 media hasta despues de la CPU: el % del GE no valia
      OUT("  con %-24s CPU %6u us (%+.0f%%)  GE %6u us (solo %u us, %+.0f%%)%s\n", gn[g], c, (c - (float)solo) * 100 / solo,
          tg, gsolo, (tg - (float)gsolo) * 100 / gsolo, c > tg ? "  [el GE termino antes]" : "");
    }
  }
}

// ---------- 8. Ruta E: exploracion de solo lectura ----------
// Latencia: cadena de punteros con saltos de 4 KB (cada lectura depende de la anterior)
static u32 cadena(u32 base, u32 bytes, int pasos) {
  volatile u32 *p = (u32 *)base;
  u32 n = bytes / 4096;
  for (u32 i = 0; i < n; i++) p[i * 1024] = base + ((i * 5 + 3) % n) * 4096;  // ciclo completo (n potencia de 2)
  u32 a = base, t = now();
  for (int k = 0; k < pasos; k++) a = *(volatile u32 *)a;
  sink = a;
  return now() - t;
}
// Cada palabra de la zona de pruebas guarda su propia direccion: leer un reflejo dice a que palabra llega
static void fillZone(void) {
  volatile u32 *z = (u32 *)UNC(0x04000000);
  for (u32 o = VB - 0x04000000; o < 0x200000; o += 4) z[o / 4] = 0xA5000000 | (o >> 2);
}
static u32 readMirror(u32 a, int ge) {  // lee una palabra con la CPU o con el GE (TRANSFER de una linea)
  if (!ge) return *(volatile u32 *)UNC(a);
  ((u32 *)UNC((u32)ram2))[0] = 0;
  geStart(); sceGuCopyImage(GU_PSM_8888, 0, 0, 16, 1, 16, (void *)a, 0, 0, 16, ram2); sceGuTexSync(); geEnd();
  return ((u32 *)UNC((u32)ram2))[0];
}
static const u32 bit[] = {0, 0x4, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000,
                          0x8000, 0x8040, 0x10000, 0x20000, 0x40000};
static void mapLine(u32 w, u32 base, int ge) {
  OUT("reflejo 0x%08x base %06x (%s): ", 0x04000000 + w, base, ge ? "GE " : "CPU");
  for (int i = 0; i < 18; i += ge ? 3 : 1) {
    u32 v = readMirror(0x04000000 + w + (base ^ bit[i]), ge);
    if ((v >> 24) == 0xA5) OUT("%x->%x ", bit[i], ((v & 0xFFFFFF) << 2) ^ base);
    else OUT("%x->?%08x ", bit[i], v);
  }
  out("\n");
}
static void exploraE(void) {
  out("\n[8] Ruta E: exploracion (solo lectura)\n");
  // a) Latencia de una lectura suelta
  int N = 20000;
  // Saltos de 4 KB: con cache, todas las lineas caen en el mismo conjunto de la cache de datos y siempre fallan
  u32 tv = cadena(UNC(0x04100000), 512 << 10, N), tr = cadena(UNC((u32)ram1), 2 << 20, N), tc = cadena((u32)ram1, 2 << 20, N);
  OUT("latencia por lectura: VRAM sin cache %.0f ns, RAM sin cache %.0f ns, RAM con cache (fallo) %.0f ns\n",
      tv * 1000.0f / N, tr * 1000.0f / N, tc * 1000.0f / N);
  u32 t = now(); memcpy(ram2, (void *)UNC((u32)ram1), 1 << 20); t = now() - t;
  OUT("CPU RAM sin cache -> RAM: %.1f MB/s (VRAM sin cache -> RAM fue 37 MB/s en [3])\n", (1 << 20) / (float)t);
  // b) Mapa de los reflejos en tres zonas de la VRAM (v5 solo en 0x100000: no se sabia si el XOR era fijo)
  fillZone();
  static const u32 bases[3] = {0x100000, 0x0C0000, 0x1A0000};
  for (int b = 0; b < 3; b++)
    for (u32 w = 0x200000; w < 0x800000; w += 0x200000) {
      mapLine(w, bases[b], 0);
      if (b == 0) mapLine(w, bases[b], 1);
    }
  out("(formato: bit de direccion cambiado -> bits que cambiaron de verdad respecto de la base; igual = sin permutar)\n");
  // c) Version del Tachyon (el chequeo que hace fallar SetSize(4 MB))
  u32 f = kfind("sceLowIO_Driver", "sceSysreg_driver", 0xE2A5D1EE);
  OUT("sceSysregGetTachyonVersion: %s %08x\n", f ? "" : "(no encontrada)", f ? kcall(f, 0, 0) : 0);
}

// ---------- 9. Ruta E con escritura (solo con Triangulo) ----------
// sceGeEdramSetSize(4 MB) hace dos cosas (uOFW): EDRAM_ENABLED_SIZE = 2 y sceSysregSetAwEdramSize(1). Aqui se
// hacen a mano, sin el chequeo del modelo, y se mira si 0x04200000+X deja de ser un reflejo.
// v5 comparaba 0x04350000 con 0x04150000, pero el reflejo de 0x04200000 cae en X^0x2040 ([8]): el "MEMORIA
// NUEVA" de v5 no valia (antes de forzar ya salia distinto). Ahora: mapa completo antes/durante/despues y
// escritura comparada con X^0x2040.
static void escribeX(const char *cuando) {
  volatile u32 *b = (u32 *)UNC(0x04150000), *bp = (u32 *)UNC(0x04150000 ^ 0x2040), *m = (u32 *)UNC(0x04350000);
  *b = 0x33333333; *bp = 0x55555555; *m = 0x44444444;
  u32 vb = *b, vbp = *bp, vm = *m;
  OUT("%s: base X=%08x  base X^2040=%08x  reflejo 0x04350000=%08x -> %s\n", cuando, vb, vbp, vm,
      vbp == 0x44444444 ? "REFLEJO (escribir 0x04350000 cambio X^2040)" :
      vm == 0x44444444 && vbp == 0x55555555 && vb == 0x33333333 ? "MEMORIA NUEVA (el reflejo guarda su propio valor)" : "otro");
}
static void forzar4MB(void) {
  out("\n[9] Ruta E: forzar el modo de 4 MB (escribe registros; cada paso se anota antes de hacerlo)\n");
  u32 getReg = kfind("sceGE_Manager", "sceGe_driver", 0xB415364D), setReg = kfind("sceGE_Manager", "sceGe_driver", 0x51C8BB60);
  u32 aw = kfind("sceLowIO_Driver", "sceSysreg_driver", 0xF3443B9A);
  OUT("funciones: GetReg %08x SetReg %08x SetAwEdramSize %08x\n", getReg, setReg, aw);
  if (!getReg || !setReg) { out("falta sceGeGetReg/SetReg: no se hace nada\n"); return; }
  u32 orig = kcall(getReg, 19, 0);
  fillZone();
  out("-- antes (modo 2 MB)\n");
  mapLine(0x200000, 0x100000, 0);
  escribeX("escritura antes");
  fillZone();
  out("paso 1: EDRAM_ENABLED_SIZE = 2\n");
  OUT("  SetReg=%08x, ahora reg19=%08x\n", kcall(setReg, 19, 2), kcall(getReg, 19, 0));
  if (aw) { out("paso 2: sceSysregSetAwEdramSize(1)\n"); OUT("  =%08x\n", kcall(aw, 1, 0)); }
  out("-- durante (modo 4 MB forzado)\n");
  for (u32 w = 0x200000; w < 0x800000; w += 0x200000) mapLine(w, 0x100000, 0);
  mapLine(0x200000, 0x100000, 1);
  escribeX("escritura durante");
  out("paso 3: restaurar\n");
  if (aw) kcall(aw, 0, 0);
  kcall(setReg, 19, orig);
  OUT("  reg19=%08x (original %08x)\n", kcall(getReg, 19, 0), orig);
  out("-- despues\n");
  mapLine(0x200000, 0x100000, 0);
  escribeX("escritura despues");
}

// ---------- main ----------
static u32 boton(void) {
  SceCtrlData c; u32 antes = 0xFFFFFFFF;
  for (;;) {
    sceKernelDelayThread(10000);
    sceCtrlReadBufferPositive(&c, 1);
    u32 nuevo = c.Buttons & ~antes; antes = c.Buttons;
    if (nuevo & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)) return nuevo;
  }
}

int main(void) {
  pspDebugScreenInit();
  sceIoRemove(LOGF);
  OUT("VPROBE %d - mapa de la VRAM, costo de mover memoria y margen del GE\n", VER);
  // GePatch cambia sceGeEdramGetAddr a la VRAM falsa: con el cargado, los resultados no valen
  SceModule m;
  if (kuKernelFindModuleByName("GePatch", &m) >= 0) {
    out("\nGePatch esta cargado en este homebrew: los resultados no valdrian.\n"
        "Cierra VPROBE y abrelo manteniendo L (Adrenaline no carga plugins).\n\nO: salir\n");
    while (!(boton() & PSP_CTRL_CIRCLE));
    sceKernelExitGame(); return 0;
  }
  ram1 = memalign(64, 2 << 20); ram2 = memalign(64, 2 << 20);
  if (!ram1 || !ram2) { out("sin memoria\n"); sceKernelDelayThread(5000000); sceKernelExitGame(); return 0; }
  OUT("buffers en la RAM: %p %p\n", ram1, ram2);
  sceGuInit();
  sceGuSetCallback(GU_CALLBACK_FINISH, onFinish);  // la pantalla sigue siendo el texto de pspDebugScreen (0x04000000, 8888)
  out("X: correr (~15 s)\nTriangulo: correr y ademas [9] forzar 4 MB de EDRAM (escribe registros, puede cerrar Adrenaline)\nO: salir\n");
  u32 k = boton();
  if (k & PSP_CTRL_CIRCLE) { sceKernelExitGame(); return 0; }
  pspDebugScreenClear();
  u32 t0 = now();
  baseE(); pspDebugScreenClear(); mapa(); pspDebugScreenClear(); copias();
  pspDebugScreenClear(); paginacion(); relleno(); tres3d(); contencion();
  pspDebugScreenClear(); exploraE();
  if (k & PSP_CTRL_TRIANGLE) forzar4MB();
  OUT("\nListo en %u ms. Resultados: ms0:/ge_patch_vprobe.log\nO: salir\n", (now() - t0) / 1000);
  while (!(boton() & PSP_CTRL_CIRCLE));
  sceKernelExitGame();
  return 0;
}
