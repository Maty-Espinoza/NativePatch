// prueba_memoria.c: prueba del capturador (captura.c, incluido abajo) fuera de la Vita, en ARM bajo qemu en el PC.
// Arma listas sinteticas en una "RAM del PSP" falsa (mmap en las direcciones de la Vita), las recorre 8 frames y
// escribe por frame los contadores de la captura. Las funciones de la Vita se reemplazan por las de abajo.
// Compilar (<inc>: carpeta con solo psp2* y vitasdk* de los encabezados de vitasdk; con todo include choca la libc):
// arm-linux-gnueabihf-gcc -static -O2 -mthumb -mfpu=neon -mfloat-abi=hard -w -I<inc> -o prueba prueba_memoria.c
// Uso: qemu-arm-static ./prueba <modo> [<vram> [<lectores>]]
// - modo: 0 sin memoria de sublistas, 1 con memoria, 2 revisando todas. Las columnas antes de "|" deben ser iguales.
// - vram: 1 pone las sublistas en la VRAM (su contenido en el espejo): en el frame 0 se saltan y se piden, desde el 1
//   deben dar lo mismo que en la RAM. 2: ademas una pagina sin copia (sublistas cortadas, sin colgarse). 3: cada frame
//   escribe sus sublistas de la VRAM solo en el espejo de ese frame y la sublista 10 cambia en el frame 4: la revision
//   (modo 2) debe detectar el cambio con BAD=0.
// - lectores: 1 los dos lectores recorren todo antes que el capturador (mismas columnas que sin lectores; pre= cuenta
//   las aplicadas con la comprobacion de un lector). 2: pasan despues del capturador y se saltan todos los CALL del
//   nivel de arriba (tarde=) sin leer lineas. 3: solo hacen el plan (comprobacion por adelantado de lo aplicado en la
//   lista anterior) antes que el capturador.
// Variables: REG=1 registro del frame (registro.c): revisa cada frame que draws registrados = contados (sin memoria:
// completo; con memoria: incompleto por las sublistas aplicadas). NIVEL=0|1|2 nivel de registro (por defecto 2,
// extremo: la vida de la lista y TARDE lo necesitan). PVMIN=n racha minima del plan (por defecto 1). VIDA=1 vida de
// los datos (muestras que cambian 1 o 2 flips despues; lineas "vida:"). TARDE=1 simula un sync atrasado (linea de
// nativedraw_tarde.log). Al final siempre: la vida de la lista (la lista principal cambia una textura en cada frame).
// Espejo doble: al final de cada frame f el "GE" copia las paginas pedidas a la copia f + 1, en el espejo (f + 1) & 1,
// que usa el frame siguiente (como el evento del flip). Las sublistas se escriben en los dos espejos (salvo vram 3).
// No simula el aviso de la Vita al PSP (la copia del espejo que la Vita anuncia en SB[0x48]).
#include "captura.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
SceUInt32 sceKernelGetProcessTimeLow(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000 + t.tv_nsec / 1000; }
void *sceClibMemset(void *d, int c, SceSize n) { return memset(d, c, n); }
void *sceClibMemcpy(void *d, const void *s, SceSize n) { return memcpy(d, s, n); }
int sceClibMemcmp(const void *a, const void *b, SceSize n) { return memcmp(a, b, n); }
int sceClibVsnprintf(char *b, SceSize n, const char *f, va_list ap) { return vsnprintf(b, n, f, ap); }
int sceClibSnprintf(char *b, SceSize n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
SceUID sceIoOpen(const char *p, int f, SceMode m) { return -1; }
int sceIoWrite(SceUID fd, const void *b, SceSize n) { return -1; }
int sceIoClose(SceUID fd) { return 0; }
int sceIoRemove(const char *p) { return 0; }
int sceIoRename(const char *a, const char *b) { return 0; }
int sceIoGetstat(const char *p, SceIoStat *s) { return -1; }
SceUID sceKernelCreateThread(const char *n, SceKernelThreadEntry e, int p, SceSize s, SceUInt a, int c, const SceKernelThreadOptParam *o) { return -1; }
int sceKernelStartThread(SceUID t, SceSize a, void *p) { return 0; }
int sceKernelDelayThread(SceUInt d) { return 0; }
int sceKernelGetThreadInfo(SceUID t, SceKernelThreadInfo *i) { return -1; }
void *sceDmacMemcpy(void *d, const void *s, SceSize n) { return memcpy(d, s, n); }
int sceGxmMapMemory(void *b, SceSize n, int a) { return 0; }
static void *blk[8]; static int nblk;  // bloques para el registro del frame
SceUID sceKernelAllocMemBlock(const char *n, SceKernelMemBlockType t, SceSize s, SceKernelAllocMemBlockOpt *o) { return nblk < 8 && (blk[nblk] = calloc(1, s)) ? nblk++ : -1; }
int sceKernelGetMemBlockBase(SceUID u, void **b) { if (u < 0 || u >= nblk) return -1; *b = blk[u]; return 0; }
int sceKernelGetMemBlockInfoByAddr(void *a, SceKernelMemBlockInfo *i) { return -1; }
SceUID emu_th[EMU_N]; int emu_aff[EMU_N], emu_n;

// Direccion donde escribir a en la RAM falsa (la VRAM, en el primer espejo)
static uint32_t *W(uint32_t a) { return (uint32_t *)PSP(isVram(a) ? MIRROR + (a & 0x1FFFFF) : a); }
static uint32_t pc_, cur_f, vram;
// Escribe el comando c con dato d en pc_ y avanza. Una lista en la VRAM se escribe en los dos espejos (con vram 3, solo
// en el del frame actual).
static void E(uint32_t c, uint32_t d) {
  if (vram == 3 && isVram(pc_)) *(uint32_t *)PSP((cur_f & 1 ? MIRROR2 : MIRROR) + (pc_ & 0x1FFFFF)) = c << 24 | (d & 0xFFFFFF);
  else *W(pc_) = c << 24 | (d & 0xFFFFFF);
  if (isVram(pc_) && vram != 3) *(uint32_t *)PSP(MIRROR2 + (pc_ & 0x1FFFFF)) = c << 24 | (d & 0xFFFFFF);
  pc_ += 4;
}
#define MAIN 0x08800000u
static uint32_t SUBS = 0x08900000u;
#define SHARED 0x08A00000u
// Arma las listas del frame: una sublista compartida (anidada), 40 sublistas que dibujan (algunas cambian BASE, llaman
// a la compartida o usan los vertices de entrada) y la lista principal que las llama con texturas y colores distintos.
// Algunas cosas cambian segun el frame para probar la deteccion de cambios.
static void build(int frame) {
  cur_f = frame;
  // sublista compartida (anidada): fija color/alpha y dibuja 1 tira con vertices propios
  pc_ = SHARED; E(0x10, 0x080000); E(0x01, 0xB00000); E(0x22, 0x123); E(0x04, 4 << 16 | 8); E(0x0B, 0);
  for (int k = 0; k < 40; k++) {
    pc_ = SUBS + k * 0x200;
    if (k % 3) E(0x01, 0xC00000 + k * 0x100);  // las k%3==0 dibujan con los vertices de entrada (VDEP)
    E(0xC8, k & 3); E(0x3A, 0); for (int i = 0; i < 12; i++) E(0x3B, (k * 7 + i) & 0xFFFFFF);
    E(0x04, 4 << 16 | (6 + k));
    if (k % 5 == 0) { E(0x10, 0x080000); E(0x0A, SHARED & 0xFFFFFF); }  // (BASE 0x08: la compartida esta en la RAM)
    if (k % 7 == 0) { E(0x10, 0x090000); }      // cambia BASE (se filtra a la salida)
    E(0x04, 3 << 16 | 3);
    // Esta cambia desde el frame 3 (4 con vram 3; TEXLEVEL distinto)
    if (k == 10 && frame >= (vram == 3 ? 4 : 3)) E(0xC8, 3);
    E(0x0B, 0);
  }
  pc_ = MAIN;
  E(0x12, 0x00011F); E(0x1E, 1); E(0xD3, 0); E(0x9C, 0x000000); E(0x9D, 0x040000 | 512);
  for (int k = 0; k < 40; k++) {
    E(0x10, SUBS >> 8 & 0x0F0000);
    // Textura impar cambia cada frame (dependencia)
    E(0xA0, 0x100000 + (k & 1 ? frame * 0x1000 : 0)); E(0xA8, 0x090000);
    E(0x01, 0xD00000 + k * 0x40 + (k == 12 && frame >= 5 ? 0x1000 : 0));
    E(0x0A, (SUBS + k * 0x200) & 0xFFFFFF);
    // La misma dos veces seguidas (otra entrada: vertices)
    if (k == 20) { E(0x10, SUBS >> 8 & 0x0F0000); E(0x0A, (SUBS + k * 0x200) & 0xFFFFFF); }
  }
  E(0x0F, 0); E(0x0C, 0);
}
// Suma de control del estado del GE (registros, matrices, vaddr/iaddr): debe dar igual con o sin memoria de sublistas
static uint32_t stateSum(void) { uint32_t h = 0; for (int i = 0; i < 256; i++) h = h * 31 + reg[i]; for (int m = 0; m < 5; m++) for (int i = 0; i < 96; i++) h = h * 31 + mtx[m][i]; return h * 31 + vaddr * 7 + iaddr; }
// Argumentos y variables de entorno: ver el encabezado. Por frame: arma, (lectores), recorre, simula la copia del
// espejo y escribe la linea de contadores; despues las pruebas opcionales (TARDE, VIDA) y el log de la captura.
int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0); int mode = atoi(argv[1]);  // 0 sin memoria, 1 con memoria, 2 revisando todas
  vram = argc > 2 ? atoi(argv[2]) : 0; int lec = argc > 3 ? atoi(argv[3]) : 0;
  nivel = getenv("NIVEL") ? atoi(getenv("NIVEL")) : 2;  // nivel de registro: extremo salvo NIVEL=0/1
  if (vram) { SUBS = 0x04100000u; esp_on = 1; }
  pv_on = lec == 3; pv_min = getenv("PVMIN") ? atoi(getenv("PVMIN")) : 1;
  if (mmap((void *)0x70000000, 0x2000000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) { perror("mmap"); return 1; }
  if (mmap((void *)0x72C00000, 0x200000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) { perror("mmap3"); return 1; }
  if (mmap((void *)0x72000000, 0x400000, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) { perror("mmap2"); return 1; }
  rlo[0] = 0x08000000; rhi[0] = 0x0B000000; nr = 1; initCls();
  if (getenv("REG")) { reg_on = 1; regInit(); }
  if (mode) { MEMO_N = 512; POOL_N = 8192; memo_sh = 23; memo = calloc(MEMO_N, sizeof(Memo)); pool = calloc(POOL_N, 64); memo_on = 1; memo_chk = mode == 2; }
  for (int f = 0; f < 8; f++) {
    build(f);
    esp_u = f;  // la copia hecha al final del frame anterior (en la Vita: la ultima terminada, al empezar la lista)
    sceClibMemset(&acc, 0, sizeof acc);
    List *l = &q[qt++ & 63]; memset(l, 0, sizeof *l); l->pc = MAIN; l->ord = f + 1; l->lo = ~0u;
    if (lec == 1 || lec == 3) pubList(l);
    // Los lectores van adelante: recorren toda la lista (y comprueban lo guardado) antes que el capturador
    if (lec == 1)
      for (int id = 0; id < 2; id++) { Lec s; lecStart(&s, &RR[id]); while (!s.done) lecWalk(id, &s, 2000); }
    if (lec == 3)  // solo el plan: lo aplicado en la lista anterior, antes que el capturador
      for (int id = 0; id < 2; id++) { Lec s; lecStart(&s, &RR[id]); pvRun(id, &RR[id], s.ord); }
    walk();
    if (lec == 2) {  // los lectores llegan despues (atrasados): deben saltarse todo lo que el capturador ya alcanzo
      for (int id = 0; id < 2; id++) { Lec s; lecStart(&s, &RR[id]); while (!s.done) lecWalk(id, &s, 2000); }
      printf("   lectores atrasados: tarde=%u lineas=%u\n", lv_late[0] + lv_late[1], lv_lines[0] + lv_lines[1]);
    }
    if (vram) {  // lo que haria el GE del PSP: copiar las paginas pedidas (con vram 2, todas menos la 0x102)
      int pl = 0, pd = 0;
      for (int p = 0; p < 512; p++) {
        int w = p >> 5, b = 1 << (p & 31);
        pl += !!(want_l[w] & b); pd += !!(want_d[w] & b);
        if ((want_l[w] | want_d[w]) & b && !(vram == 2 && p == 0x102)) CPY((f + 1) & 1, p) = f + 1;
      }
      SB[0x40] = f + 1;
      printf("   espejo: pedidas listas=%d datos=%d recorridas=%u saltadas=%u cortadas=%u\n", pl, pd, acc.v_ok, acc.vcalls, acc.v_cut);
    }
    printf("f%d cmds=%u draws=%u verts=%u vbytes=%u tex=%u prim3=%u prim4=%u calls=%u estado=%08x | hit=%u miss=%u dep=%u fail=%u no=%u chk=%u BAD=%u lines=%u saltados=%u pre=%u lv=%u/%u/%u pv=%u/%u/%u\n",
           f, acc.cmds, acc.draws, acc.verts, acc.vbytes, acc.textures, acc.prim[3], acc.prim[4], acc.calls_ram, stateSum(),
           acc.memo_hit, acc.memo_miss, acc.memo_dep, acc.memo_fail, acc.memo_no, acc.memo_chk, acc.memo_bad, acc.memo_lines, acc.memo_cmds, acc.memo_pre,
           lv_lines[0] + lv_lines[1], lv_ok[0] + lv_ok[1], lv_bad[0] + lv_bad[1], pv_pn, pv_ok[0] + pv_ok[1], pv_bad[0] + pv_bad[1]); pv_ok[0] = pv_ok[1] = pv_bad[0] = pv_bad[1] = 0; lv_lines[0] = lv_lines[1] = lv_ok[0] = lv_ok[1] = lv_bad[0] = lv_bad[1] = lv_late[0] = lv_late[1] = 0;
    if (reg_on) {  // cerrar el registro como en el flip y revisarlo
      int w = rg_w; RFrame *g = rf[w];
      uint32_t nd = g->nd, ne = g->ne, na = g->na, fl = g->flags, vis = g->draws_vistos;
      regFlip(0x04000000, 512, acc.draws);
      printf("   registro: draws=%u (contados %u, vistos %u) estados=%u bytes=%u flags=%x %s publicado=%d\n", nd, acc.draws, vis, ne, na, fl,
             !fl && nd == acc.draws ? "COMPLETO" : "incompleto", rg_pub);
    }
    lifeFlip(); lf_ord_cmds = 0;  // como en el flip
    gen++; ntex = 0;
  }
  printf("vida de la lista: flip de su frame %u (arriba %u, sublistas %u) | flip siguiente %u (%u, %u) (esperado 8 (0, 0) | 7 (7, 0))\n", lf_n[0], lf_top[0], lf_sub[0], lf_n[1], lf_top[1], lf_sub[1]);
  // Un sync que llega 100 us despues de encolar: la Vita va atrasada (linea en nativedraw_tarde)
  if (getenv("TARDE")) {
    build(0); qh = qt = 0; capt = 0;
    uint32_t t = now() - 50, ev[8] = { EV_ENQ, MAIN, 0, 7, t, 99, 0, 0 };
    event(ev);
    uint32_t sy[8] = { EV_DSYNC, 0, 0, 99, t + 100, 0, 0, 0 };
    event(sy);
    printf("tarde: pendientes=%u (esperado 1)\n", npend);
    walk();
    printf("tarde: pendientes=%u capt=%u (esperado 0 99) atrasados=%u\n", npend, capt, acc.pf_late);
    uint32_t sy2[8] = { EV_DSYNC, 0, 0, 99, now() + 5000, 0, 0, 0 };  // otro sync despues: a tiempo, sin linea
    event(sy2);
    RG[1].b[RG[1].w % RG[1].n] = 0; printf("tarde (log):\n%s", RG[1].b);
  }
  if (getenv("VIDA")) {  // vida de los datos por frames. 8 muestras de vertices por frame (el tope)
    smp_r = smp_w; sceClibMemset((void *)&LT, 0, sizeof LT);  // (sin las muestras de los frames de arriba)
    for (int k = 0; k < 4; k++) smp_f[k] = smp_k[k] = 0;
    for (int k = 0; k < 8 * 64; k++) smpAdd(0x08C00000u + (k / 64) * 0x100, 64, 0);  // frame A: grupos 0-7
    for (int g = 2; g < 5; g++) *W(0x08C00000u + g * 0x100) ^= 1;                    // 3 cambian antes del flip 1
    gen++; for (int k = 0; k < 4; k++) smp_f[k] = 0; ltFlip();
    for (int g = 5; g < 8; g++) *W(0x08C00000u + g * 0x100 + 8) ^= 1;                // 3 cambian antes del flip 2...
    // ...y 1 de ellos con aviso de rango
    cw[0].lo = 0x08C00000u + 5 * 0x100; cw[0].hi = cw[0].lo + 0x100; cw[0].t = now(); cw_n = 1;
    cw_all_t = 0;
    gen++; ltFlip();
    printf("vida: n=%u cambiaron_1f=%u 2f=%u aviso_rango=%u aviso_todo=%u (esperado 8 / 3 / 6 / 1 / 0)\n", LT.n[0], LT.ch1[0], LT.ch2[0], LT.warn[0], LT.wall[0]);
    cw_all_t = now() + 1;  // vaciar toda la cache: cubre a los demas
    for (int k = 0; k < 8 * 64; k++) smpAdd(0x08C10000u + (k / 64) * 0x100, 64, 0);
    for (int g = 0; g < 4; g++) *W(0x08C10000u + g * 0x100) ^= 1;
    cw_all_t = now() | 1;
    gen++; for (int k = 0; k < 4; k++) smp_f[k] = 0; ltFlip(); gen++; ltFlip();
    printf("vida: n=%u cambiaron_2f=%u aviso_todo=%u (esperado 16 / 10 / 4)\n", LT.n[0], LT.ch2[0], LT.wall[0]);
  }
  lbuf[lpos] = 0; printf("%s", lbuf);
  return 0;
}
