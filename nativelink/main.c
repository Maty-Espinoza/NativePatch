// NativeLink: plugin del lado PSP (corre en el PSP de Adrenaline, junto al juego). No cambia nada de lo que dibuja el
// juego: engancha las funciones con que el juego le pasa sus listas de dibujo al GE y anota en un buzon compartido cada
// encolado, stall, sync y flip. NativeDraw (el plugin del lado Vita) lee ese buzon y recorre las mismas listas desde la RAM
// del PSP. Ademas:
//   - espejo de la VRAM: la Vita no ve la VRAM del PSP; en cada flip el GE copia a la RAM las paginas que pide la Vita;
//   - compuerta: si la Vita va atrasada, el juego la espera (hasta 5 ms) antes de reusar una lista;
//   - telemetria para los registros (T4/T6, CPU libre, vida de la lista) y el log, que escribe NativeDraw (log.c).
// Atajos al abrir un juego: L o SELECT = NativeLink apagado (el juego corre como sin plugin); R = log sincrono.
// Hasta 0.2-dev66 se llamaba GePatch y ademas escalaba la imagen en el PSP (arquitectura A). Ese codigo se quito en 0.0.2:
// esta en la etiqueta gepatch-dev66 del repo (docs/dev/versiones.md). Los numeros "devNN" de los comentarios son de esa
// numeracion vieja.
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspge.h>
#include <pspgu.h>
#include <stdio.h>
#include <string.h>
#include <pspsysmem_kernel.h>
#include <systemctrl.h>

#include "log.h"

PSP_MODULE_INFO("NativeLink", 0x1007, 1, 0);

#define DRAW_NATIVE 0xABCDEF00 // != 0: Adrenaline muestra la imagen de 0x0A400000 en vez de la del juego (no se usa: 0)
#define ZONA 0x0A000000        // 16 MB para los plugins (buzon, espejos, listas propias)
#define STATS_EVERY 600        // frames entre cada linea de estadisticas (~10 s)

static int passive; // L o SELECT al abrir el juego, o el juego ocupa la zona: no se anota nada
static struct { u32 lists, heads, stalls, frames, frame_us, frame_max, slow60, slow30, long_logged; } st; // (log, 10 s)
static u32 last_flip;

int (*_sceGeListUpdateStallAddr)(int qid, void *stall);
int (*_sceGeListEnQueue)(const void *list, void *stall, int cbid, PspGeListArgs *arg);
int (*_sceGeListEnQueueHead)(const void *list, void *stall, int cbid, PspGeListArgs *arg);
int (*_sceDisplaySetFrameBuf)(void *topaddr, int bufferwidth, int pixelformat, int sync);

// --- Buzon compartido con NativeDraw (etapa 2 de la arquitectura B, desde dev61 "modo sombra") ---------------------
// El juego corre como sin plugin (VRAM real, listas sin tocar; Adrenaline muestra su imagen). Los ganchos solo anotan
// lo que el juego le pide al GE y el capturador de NativeDraw recorre las mismas listas leyendo la RAM del PSP.
// Buzon en 0x0A000000 (zona reservada para los plugins, ver zoneCheck), sin cache:
//   +0x00 "SOMB"  +0x04 version  +0x08 eventos escritos (PSP)  +0x0C ultima lista capturada (Vita, numero de orden)
//   +0x10 latido de la Vita  +0x14 eventos leidos (Vita)  +0x18 listas encoladas (PSP)
//   +0x1000 anillo de SB_N eventos de 32 bytes: tipo | dato << 8, a, b, c, hora del PSP (us), orden de la lista
// Compuerta en el sync (modo normal de la arquitectura B): sceGeDrawSync/ListSync esperan hasta 5 ms a que la Vita
// haya capturado las listas que el juego espera. Se mide cuantas veces hace falta esperar y cuanto.
#define SB ((volatile u32 *)(0x0A000000 | 0x40000000))
#define SB_MAGIC 0x424D4F53 // "SOMB"
#define SB_N 4096
enum { EV_ENQ = 1, EV_HEAD, EV_STALL, EV_DSYNC, EV_LSYNC, EV_FLIP, EV_GEFIN, EV_SYNCEND, EV_CPU, EV_CPUSUM, EV_FRAME, EV_VIDA }; // (7-10: dev63; 11: dev65; 12: dev65.2)
static int activo, mode_done; // activo: el juego no se abrio con L o SELECT y la zona esta libre
// dev65 (fase de analisis): cuanto le cuesta al juego cada parte, por frame. EV_FRAME (antes de EV_FLIP): dato = esperas
// en la compuerta | syncs en que la Vita iba atrasada pero no se espero (sin_compuerta) << 8; a = frame (us, flip a flip),
// b = espera en la compuerta (us), c = espera al GE dentro de sceGeDrawSync/ListSync (us); e[5] = vueltas del hilo de CPU
// libre (SB[0x4D]: vueltas por ms = 100 %), e[6] = espejo: CPU del flip (us) | paginas << 16, e[7] = ganchos (us en sbPost).
// Interruptores que pone NativeDraw en SB[0x4A]: 1 sin compuerta (no se espera a la Vita, solo se cuenta), 2 sin espejo, 4
// sin hilo de CPU libre, 16 (0.0.3) espejo completo: todas las paginas de datos pedidas en cada flip, 32 (0.0.3.2,
// prueba) el flip espera a que termine la copia del espejo. Mientras la compuerta espera: SB[0x4B] = lista esperada, SB[0x4C] = hora de inicio.
// dev65.3: nivel de registro de NativeDraw en SB[0x4E] (0 minimo, 1 mediano, 2 extremo; docs/dev/registros.md): EV_FRAME y el hilo
// de CPU libre desde mediano; la vida de la lista (EV_VIDA) y los eventos de la CPU del juego (T4: EV_CPU, EV_CPUSUM) solo en extremo.
static volatile u32 spin, spin_cal;  // hilo de CPU libre (como vpjuego): vueltas y calibracion (vueltas por ms)
static struct { u32 gate_us, gate_n, nowait, sync_us, post_us, esp_us, esp_pages; } fr;              // del frame
static struct { u32 frames, frame_us, spin, gate_us, gate_n, nowait, sync_us, post_us, esp_us, esp_pages; } frs; // (log, 10 s)
static u32 sb_x[3];  // e[5..7] de EV_FRAME
// dev65.2, vida de la lista: NativeDraw deja una muestra de lineas de 64 bytes de la ultima lista que capturo (SB[0x50]
// numero de muestra, 0 mientras la escribe; SB[0x51] cuantas, hasta 32; SB[0x52] cuantas son del nivel de arriba; SB[0x60+k]
// direccion, SB[0x80+k] huella FNV de sus 16 palabras, como lineHash en captura.c). Antes de cada encolado y en cada sync,
// NativeLink rehace las huellas con la cache (lo que ve la CPU del juego) y anota EV_VIDA: dato = 0 encolado / 1 drawsync / 2
// listsync, a = numero de muestra, b = cambiadas del nivel de arriba | de sublistas << 8, c = ultima lista encolada.
static int (*_sceGeGetReg)(int); // (T6, dev63)
static u32 geBusy(void) { // 1 si el GE tiene una lista en curso (desde un gancho: con k1 = 0, dev63.2)
  if (!_sceGeGetReg) return 2;
  int k1 = pspSdkSetK1(0);
  u32 e = _sceGeGetReg(3) & 1;
  pspSdkSetK1(k1);
  return e;
}
static u32 sb_ord; // numero de orden de la ultima lista encolada
static struct { u32 qid, ord; } sb_q[16]; // ultimas listas, para la compuerta de sceGeListSync
static struct { u32 ev, syncs, late, wait_us, wait_max, timeouts, in_sync_us, lsyncs, llate; } sbs;

static void sbPost(u32 type, u32 aux, u32 a, u32 b, u32 c) {
  int intr = pspSdkDisableInterrupts();
  u32 h = SB[2];
  volatile u32 *e = SB + 0x400 + (h % SB_N) * 8;
  e[0] = type | aux << 8; e[1] = a; e[2] = b; e[3] = c; e[4] = sceKernelGetSystemTimeLow();
  if (type <= EV_HEAD) {
    e[5] = SB[6] = ++sb_ord;
    sb_q[sb_ord & 15].qid = c; sb_q[sb_ord & 15].ord = sb_ord;
  } else if (type == EV_FRAME) {
    e[5] = sb_x[0]; e[6] = sb_x[1]; e[7] = sb_x[2];
  }
  asm volatile("sync");
  SB[2] = h + 1;
  sbs.ev++;
  fr.post_us += sceKernelGetSystemTimeLow() - e[4]; // (dev65) costo de anotar
  pspSdkEnableInterrupts(intr);
}
static u32 vidaHash(u32 *ptop, u32 *psub) { // rehace las huellas de la muestra; devuelve su numero (0: no hay o cambio)
  u32 sq = SB[0x50], n = SB[0x51], nt = SB[0x52], k, i, top = 0, sub = 0;
  *ptop = *psub = 0;
  if (!sq || n > 32) return 0;
  for (k = 0; k < n; k++) {
    u32 a = SB[0x60 + k], h = 2166136261u;
    if (a < 0x08000000 || a >= 0x0A000000) continue;
    const u32 *w = (const u32 *)(a & ~63);
    for (i = 0; i < 16; i++) h = (h ^ w[i]) * 16777619u;
    if (h != SB[0x80 + k]) { if (k < nt) top++; else sub++; }
  }
  *ptop = top; *psub = sub;
  return SB[0x50] == sq ? sq : 0; // (si la Vita cambio la muestra mientras tanto, no vale)
}
static void vidaCheck(u32 kind) {
  u32 top, sub, sq;
  if (activo && SB[0x4E] >= 2 && (sq = vidaHash(&top, &sub))) sbPost(EV_VIDA, kind, sq, top | sub << 8, sb_ord);
}
static void sbInit(void) {
  int i;
  for (i = 0; i < 0x50; i++) SB[i] = 0;
  for (i = 0x200; i < 0x400; i++) SB[i] = SB[0x8200 + i] = 0; // (ultima copia de cada pagina en los dos espejos)
  SB[1] = 5; SB[6] = sb_ord; // version 2: con espejo de la VRAM (dev62); 3: con T4/T6 (eventos 7-10, dev63); 4: espejo doble (dev64); 5: EV_FRAME (dev65)
  SB[0x4D] = spin_cal; SB[0x54] = 0;
  SB[0] = SB_MAGIC;
}

static int sbAlive(void) { // el capturador de la Vita responde (su latido cambio hace menos de 0,5 s)
  static u32 hb, hb_t;
  u32 now = sceKernelGetSystemTimeLow();
  if (SB[4] != hb) { hb = SB[4]; hb_t = now; }
  return hb && now - hb_t < 500000;
}
// 0 si la Vita ya capturo hasta la lista 'ord'; si no, los us que se espero (hasta 5 ms) | 1
static u32 sbGate(u32 ord) {
  if (!sbAlive() || (int)(SB[3] - ord) >= 0) return 0;
  if (SB[0x4A] & 1) { fr.nowait++; return 0; } // (dev65) sin compuerta: solo se cuenta
  u32 t0 = sceKernelGetSystemTimeLow(), w;
  SB[0x4B] = ord; SB[0x4C] = t0 | 1;
  do {
    sceKernelDelayThread(50);
    w = sceKernelGetSystemTimeLow() - t0;
  } while ((int)(SB[3] - ord) < 0 && w < 5000);
  SB[0x4B] = SB[0x4C] = 0;
  sbs.timeouts += (int)(SB[3] - ord) < 0;
  sbs.wait_us += w;
  if (w > sbs.wait_max) sbs.wait_max = w;
  fr.gate_us += w; fr.gate_n++;
  return w | 1;
}
// --- dev66 (fase 2): compuerta antes de encolar la lista siguiente ---
// En GT la lista de un frame no cambia hasta que el juego encola la siguiente (fase 1: 0 de 11 436). Entonces el juego
// no tiene que esperar a la Vita en el sync: puede esperar recien antes de encolar la lista siguiente, y la Vita gana
// ~2,4 ms por frame (lo que va del sync al encolado). En el sync solo se anota hasta que lista hay que esperar.
// Autocomprobacion (para cualquier juego): NativeDraw deja una muestra de huellas de la ultima lista que capturo
// (SB[0x50..], SB[0x53] = su numero de orden). Antes de cada encolado, si esa lista ya paso por un sync, NativeLink rehace
// las huellas: si cambiaron, el juego reescribe la lista antes de encolar la siguiente y la Vita podria leer algo que el
// GE no dibujo -> la compuerta vuelve al sync. Empieza en el sync y pasa a "antes de encolar" despues de 60
// comprobaciones limpias seguidas; un cambio la devuelve al sync hasta juntar otras 60 (dev66.2: en GT cambia una vez al
// pasar de la presentacion a la carrera, en la pantalla de carga) y con 10 cambios se queda en el sync hasta cerrar el juego. Interruptor de NativeDraw compuerta_sync (SB[0x4A] & 8): siempre en el
// sync (las comprobaciones siguen, para comparar). SB[0x54] = modo (0 probando, 1 antes de encolar, 2 la lista cambio 10 veces).
// dev66.3: solo vale la muestra de una lista encolada despues del sync anterior (sync_lo, sync_ord]: la de una lista mas vieja
// puede estar en la zona que el juego ya reusa (GT alterna dos zonas) y daba falsos cambios (dev66.2: 10 en una carrera).
static u32 sync_ord, sync_lo, gate_ord, gate_sq, gate_ok; // ultima lista que paso por un sync (y la del sync anterior); la que espera el proximo encolado
static int gate_mode;
static struct { u32 checks, bad, n, us, max; } gts; // (log, 10 s)
static int gateEnq(void) { return gate_mode == 1 && !(SB[0x4A] & 8); }
static void gateSync(u32 ord) { // en el sync: hasta que lista hay que esperar
  if ((int)(ord - sync_ord) > 0) { sync_lo = sync_ord; sync_ord = ord; }
  if (gateEnq()) gate_ord = sync_ord;
}
static u32 gateBeforeEnq(void) { // antes de encolar: espera (si toca) y autocomprobacion; devuelve la espera (us | 1)
  u32 w = 0, sq = SB[0x50], top, sub;
  if (gate_ord) { w = gateEnq() ? sbGate(gate_ord) : 0; gate_ord = 0; }
  if (w) { gts.n++; gts.us += w; if (w > gts.max) gts.max = w; }
  u32 so = SB[0x53];
  if (!sq || sq == gate_sq || !sync_ord || (int)(so - sync_ord) > 0 || (int)(so - sync_lo) <= 0 || !sbAlive()) return w; // ya revisada / no es del ultimo sync
  if (!(sq = vidaHash(&top, &sub))) return w;
  gate_sq = sq; gts.checks++;
  if (top | sub) {
    static u32 bad_total;
    if (gate_mode != 2) LOG("compuerta: la lista %lu cambio antes de encolar la siguiente (%lu lineas arriba, %lu de sublistas; %lu cambios) -> en el sync%s\n",
                            SB[0x53], top, sub, bad_total + 1, bad_total + 1 >= 10 ? " hasta cerrar el juego" : "");
    gts.bad++; gate_ok = 0; gate_mode = ++bad_total >= 10 ? 2 : 0;
  } else if (!gate_mode && ++gate_ok >= 60) {
    gate_mode = 1;
    LOG("compuerta: 60 comprobaciones limpias seguidas -> antes de encolar la lista siguiente\n");
  }
  SB[0x54] = gate_mode;
  return w;
}
int (*_sceGeDrawSync)(int mode);
int (*_sceGeListSync)(int qid, int mode);
int sceGeDrawSyncPatched(int mode) {
  if (!activo || mode)
    return _sceGeDrawSync(mode);
  vidaCheck(1);
  u32 w = gateEnq() ? 0 : sbGate(sb_ord), t0; // (dev66) antes de encolar: aqui solo se anota
  gateSync(sb_ord);
  sbs.syncs++;
  sbs.late += !!w;
  sbPost(EV_DSYNC, mode, w, SB[3], sb_ord);
  u32 busy = geBusy();
  t0 = sceKernelGetSystemTimeLow();
  int r = _sceGeDrawSync(mode);
  u32 dt = sceKernelGetSystemTimeLow() - t0;
  sbs.in_sync_us += dt; fr.sync_us += dt;
  sbPost(EV_SYNCEND, 0, dt, busy, 0); // T6 (dev63): cuanto espero de verdad al GE, y si el GE estaba trabajando
  return r;
}
int sceGeListSyncPatched(int qid, int mode) {
  if (activo && !mode) {
    u32 ord = 0, w;
    int i;
    vidaCheck(2);
    for (i = 0; i < 16; i++)
      if (sb_q[i].qid == (u32)qid && (int)(sb_q[i].ord - ord) > 0)
        ord = sb_q[i].ord;
    w = ord && !gateEnq() ? sbGate(ord) : 0;
    if (ord) gateSync(ord);
    sbs.lsyncs++;
    sbs.llate += !!w;
    sbPost(EV_LSYNC, mode, qid, w, SB[3]);
    u32 busy = geBusy(), t0 = sceKernelGetSystemTimeLow();
    int r = _sceGeListSync(qid, mode);
    u32 dt = sceKernelGetSystemTimeLow() - t0;
    fr.sync_us += dt;
    sbPost(EV_SYNCEND, 0, dt, busy, 1);
    return r;
  }
  return _sceGeListSync(qid, mode);
}

// --- T6 y T4 (dev63, etapa 2): cuando trabaja el GE y que escribe la CPU del juego ---
// T6: un hilo mira el GE cada ~250 us con sceGeGetReg (como vpjuego) solo mientras tiene trabajo; sin listas duerme
// 2 ms. dev63.3: apagado salvo que NativeDraw lo pida (SB[0x47] = 1, archivo ux0:data/nativedraw/medir_ge): despertar
// cada 250 us le quitaba ~2-3 fps a GT en el puente. Por cada tramo en que el GE estuvo ocupado sin pausa anota EV_GEFIN: a = inicio (hora del encolado si el GE
// estaba libre), b = muestras detenido en el stall, c = muestras; la hora del evento es el fin (+-250 us).
// T4: ganchos de las funciones con que el juego deja datos listos para el GE (vaciar la cache de un rango, DMA):
// EV_CPU (dato: 0 WritebackRange, 1 WritebackInvalidateRange, 2 InvalidateRange, 3 WritebackAll, 4
// WritebackInvalidateAll, 5 sceDmacMemcpy, 6 sceDmacTryMemcpy; a = direccion o destino, b = tamano o origen, c =
// tamano del DMA). Hasta 96 por frame (el resto solo se cuenta); en el flip EV_CPUSUM con los totales del frame.
static volatile u32 ge_kick; // hora del ultimo encolado (0: ya visto)
static struct { u32 busy_us, runs, run_max, stall_n, n; } ges;  // (para el log de NativeLink)
static int geThread(SceSize args, void *argp) {
  u32 busy0 = 0, n = 0, nst = 0;
  for (;;) {
    if (!activo || !_sceGeGetReg || SB[0x47] != 1) { // (dev63.3) solo si NativeDraw lo pide: le quitaba ~2-3 fps a GT
      busy0 = ge_kick = 0;
      sceKernelDelayThread(100000);
      continue;
    }
    sceKernelDelayThread(busy0 || ge_kick ? 250 : 2000);
    u32 e = _sceGeGetReg(3), t = sceKernelGetSystemTimeLow();
    if (e & 1) {
      if (!busy0) { busy0 = ge_kick && (int)(t - ge_kick) >= 0 ? ge_kick : t; n = nst = 0; } // (dev63.2: el encolado pudo ser despues de leer t)
      u32 la = _sceGeGetReg(5), st = _sceGeGetReg(6);
      n++;
      if (st && (la & 0x0FFFFFFF) == (st & 0x0FFFFFFF)) nst++;
      ge_kick = 0;
    } else if (busy0 || ge_kick) { // termino (o era tan corta que no se vio: n = 0)
      u32 b0 = busy0 ? busy0 : ge_kick;
      if ((int)(t - b0) < 0) b0 = t;
      sbPost(EV_GEFIN, 0, b0, nst, n);
      ges.busy_us += t - b0; ges.runs++; ges.stall_n += nst; ges.n += n;
      if (t - b0 > ges.run_max) ges.run_max = t - b0;
      busy0 = ge_kick = 0;
    }
  }
  return 0;
}
static int libreThread(SceSize args, void *argp) { // (dev65) ver t4t6Install
  for (;;) {
    if (!activo || SB[0x4A] & 4 || SB[0x4E] < 1) { sceKernelDelayThread(100000); continue; }
    u32 k;
    for (k = 0; k < 100000; k++) { spin++; asm volatile("" ::: "memory"); }
  }
  return 0;
}
static struct { u32 n, kb, dn, dkb, posted, dropped; } t4;  // del frame
static struct { u32 n, kb, dn, dkb; } t4s;                  // (para el log de NativeLink)
static void t4Post(u32 kind, u32 a, u32 b, u32 c) {
  if (SB[0x4E] < 2) return; // (dev65.3: solo en extremo)
  if (t4.posted < 96) { t4.posted++; sbPost(EV_CPU, kind, a, b, c); } else t4.dropped++;
}
static void (*_dcWbR)(const void *, unsigned int), (*_dcWbIR)(const void *, unsigned int), (*_dcIR)(const void *, unsigned int);
static void (*_dcWbA)(void), (*_dcWbIA)(void);
static int (*_dmac)(void *, const void *, unsigned int), (*_dmacTry)(void *, const void *, unsigned int);
static void dcRange(int kind, const void *p, unsigned int n) { if (activo) { t4.n++; t4.kb += n >> 10; t4Post(kind, (u32)p, n, 0); } }
void dcWbRPatched(const void *p, unsigned int n) { _dcWbR(p, n); dcRange(0, p, n); }
void dcWbIRPatched(const void *p, unsigned int n) { _dcWbIR(p, n); dcRange(1, p, n); }
void dcIRPatched(const void *p, unsigned int n) { _dcIR(p, n); dcRange(2, p, n); }
void dcWbAPatched(void) { _dcWbA(); if (activo) { t4.n++; t4Post(3, 0, 0, 0); } }
void dcWbIAPatched(void) { _dcWbIA(); if (activo) { t4.n++; t4Post(4, 0, 0, 0); } }
static void dmacNote(int kind, void *d, const void *s, unsigned int n) { if (activo) { t4.dn++; t4.dkb += n >> 10; t4Post(kind, (u32)d, (u32)s, n); } }
int dmacPatched(void *d, const void *s, unsigned int n) { int r = _dmac(d, s, n); dmacNote(5, d, s, n); return r; }
int dmacTryPatched(void *d, const void *s, unsigned int n) { int r = _dmacTry(d, s, n); dmacNote(6, d, s, n); return r; }
static void t4Flip(void) { // en el flip: totales del frame
  if (SB[0x4E] >= 2) sbPost(EV_CPUSUM, t4.dropped > 0xFFFFFF ? 0xFFFFFF : t4.dropped, t4.n, t4.kb, (t4.dn & 0xFFFF) | (t4.dkb > 0xFFFF ? 0xFFFF : t4.dkb) << 16);
  t4s.n += t4.n; t4s.kb += t4.kb; t4s.dn += t4.dn; t4s.dkb += t4.dkb;
  memset(&t4, 0, sizeof(t4));
}
static void t4t6Install(void) {
  static const char *um[2] = { "sceSystemMemoryManager", "sceKernelUtils" };
  int i, nd = 0, nm = 0;
  _sceGeGetReg = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xB415364D);
  for (i = 0; i < 2 && !_dcWbR; i++) {
    _dcWbR = (void *)FindProc(um[i], "UtilsForUser", 0x3EE30821);
    _dcWbIR = (void *)FindProc(um[i], "UtilsForUser", 0x34B9FA9E);
    _dcIR = (void *)FindProc(um[i], "UtilsForUser", 0xBFA98062);
    _dcWbA = (void *)FindProc(um[i], "UtilsForUser", 0x79D1C3FA);
    _dcWbIA = (void *)FindProc(um[i], "UtilsForUser", 0xB435DEC5);
  }
  if (_dcWbR) { sctrlHENPatchSyscall((u32)_dcWbR, dcWbRPatched); nd++; }
  if (_dcWbIR) { sctrlHENPatchSyscall((u32)_dcWbIR, dcWbIRPatched); nd++; }
  if (_dcIR) { sctrlHENPatchSyscall((u32)_dcIR, dcIRPatched); nd++; }
  if (_dcWbA) { sctrlHENPatchSyscall((u32)_dcWbA, dcWbAPatched); nd++; }
  if (_dcWbIA) { sctrlHENPatchSyscall((u32)_dcWbIA, dcWbIAPatched); nd++; }
  static const char *dm[4] = { "sceDMAManager", "sceDMACManager", "sceDmacManager", "sceDMAC_Driver" }; // (dev63.2: con
  for (i = 0; i < 4 && !_dmac; i++) {                                                                // el primero no se encontro)
    _dmac = (void *)FindProc(dm[i], "sceDmac", 0x617F3FE6);
    _dmacTry = (void *)FindProc(dm[i], "sceDmac", 0xD97F94D8);
    if (_dmac) LOG("T4: sceDmac en %s\n", dm[i]);
  }
  if (_dmac) { sctrlHENPatchSyscall((u32)_dmac, dmacPatched); nm++; }
  if (_dmacTry) { sctrlHENPatchSyscall((u32)_dmacTry, dmacTryPatched); nm++; }
  SceUID th = _sceGeGetReg ? sceKernelCreateThread("NativeLinkGE", geThread, 0x11, 0x800, 0, NULL) : -1;
  if (th >= 0) sceKernelStartThread(th, 0, NULL);
  // (dev65) CPU libre del PSP, como vpjuego: el mismo bucle se cronometra aqui (2 millones de vueltas) y un hilo de
  // prioridad minima lo repite: solo avanza cuando nadie mas quiere la CPU. Solo con NativeLink activo.
  u32 k, tc = sceKernelGetSystemTimeLow();
  for (k = 0; k < 2000000; k++) { spin++; asm volatile("" ::: "memory"); }
  tc = sceKernelGetSystemTimeLow() - tc;
  spin_cal = tc ? 2000000u / (tc / 1000 + 1) : 0;
  SceUID tl = sceKernelCreateThread("NativeLinkLibre", libreThread, 126, 0x400, 0, NULL);
  if (tl >= 0) sceKernelStartThread(tl, 0, NULL);
  LOG("CPU libre: %lu vueltas/ms = 100%% (hilo %08lx)\n", spin_cal, (u32)tl);
  LOG("T4/T6: sceGeGetReg %s, hilo del GE %08lx | ganchos de la cache %d de 5, del DMA %d de 2\n", _sceGeGetReg ? "si" : "NO",
      (u32)th, nd, nm);
}

// --- Espejo de la VRAM (dev62, etapa 2; doble desde dev64) ---
// La Vita no ve la VRAM real del PSP (no esta mapeada en Adrenaline), y GT guarda ahi sublistas, texturas y paletas.
// El capturador pide paginas de 4 KB en dos mapas de bits del buzon ("listas" y "datos") y en cada flip el GE copia
// las pedidas a la RAM. dev64: dos espejos alternados (copias pares en MIRROR, impares en MIRROR2) y en cada copia
// todas las paginas de listas y hasta ESP_DATOS de datos (si hay mas, por turnos). La Vita lee cada lista con la ultima copia
// terminada y anota en SB[0x48] la que esta usando: no se copia encima de esa (asi nunca lee algo que el GE esta
// sobrescribiendo). Una copia final de 16 bytes escribe en SB[0x40] el numero de la copia: la Vita sabe cuando termino.
// Buzon: +0x80 listas y +0xC0 datos (16 palabras cada uno, Vita) | +0x100 ultima copia terminada (GE) | +0x110 copias,
// +0x114 paginas copiadas, +0x118 veces que la copia de hace dos frames seguia en el GE, +0x124 veces que la Vita usaba
// ese espejo (totales, PSP) | +0x120 copia en uso (Vita) | +0x800 y +0x21000: numero de la ultima copia de cada pagina
// en MIRROR y en MIRROR2 (512 palabras cada uno, PSP).
#define MIRROR 0x0A200000
#define MIRROR2 0x0AC00000
#define ESP_LIST 0x0A100000 // dos listas de 8 KB, alternadas (en la zona reservada)
#define ESP_DATOS 64 // paginas de datos por copia (256 KB; dev64.3 las copiaba todas: ~730 KB por frame bajaban GT a ~57 fps)
// 0.0.3: con SB[0x4A] & 16 (lo pone NativeDraw cuando la Vita dibuja frames 2D: KOF 96) se copian todas las pedidas en cada
// flip; de a 64 por turnos, KOF 96 (256 paginas de sprites) veia cada pagina cada 4 frames: sprites con pedazos viejos.
void guStart(int cid, void *list); // gu.c
static int esp_qid[2] = { -1, -1 };
static u32 esp_seq, esp_rr, esp_pages, esp_busy, esp_vita; // (los tres ultimos: totales, para la Vita)
static struct { u32 copies, pages, busy, vita, over, runs_max; } esps;
static void espejoFlip(void) {
  u32 cp[16], wd[16], i, p, w, b, n = 0, any = 0; // (n: paginas de datos de esta copia)
  if (SB[0x4A] & 2) return; // (dev65) sin espejo
  for (i = 0; i < 16; i++) { cp[i] = SB[0x20 + i]; wd[i] = SB[0x30 + i] & ~cp[i]; any |= cp[i] | wd[i]; }
  if (!any) return;
  for (i = 0; i < 512; i++) { // listas: todas; datos: los que entren, empezando donde quedo la ultima vez que no entraron todos
    p = (esp_rr + i) & 511; w = p >> 5; b = 1u << (p & 31);
    if (!(wd[w] & b)) continue;
    if (n >= (SB[0x4A] & 16 ? 512 : ESP_DATOS)) { esp_rr = p; esps.over++; break; }
    cp[w] |= b; n++;
  }
  u32 seq = esp_seq + 1, s = seq & 1, u = SB[0x48];
  if (esp_qid[s] >= 0 && _sceGeListSync(esp_qid[s], 1) > 0) { // la de hace dos frames sigue en el GE
    esps.busy++; SB[0x46] = ++esp_busy;
    return;
  }
  if (u && (int)(esp_seq - u) >= 0 && !((seq ^ u) & 1) && sbAlive()) { // la Vita sigue leyendo ese espejo (la de hace dos frames)
    esps.vita++; SB[0x49] = ++esp_vita;
    return;
  }
  esp_seq = seq;
  u32 rl = ESP_LIST + s * 0x2000, dst = s ? MIRROR2 : MIRROR, runs = 0;
  volatile u32 *last = SB + (s ? 0x8400 : 0x200);
  guStart(0, (void *)(rl | 0xA0000000));
  for (p = 0; p < 512 && runs < 200;) { // tramos contiguos (32 bytes de lista cada uno)
    if (!(cp[p >> 5] >> (p & 31) & 1)) { p++; continue; }
    u32 p0 = p;
    while (p < 512 && cp[p >> 5] >> (p & 31) & 1) last[p++] = seq;
    sceGuCopyImage(GU_PSM_8888, 0, 0, 1024, p - p0, 1024, (void *)(0x04000000 + p0 * 4096), 0, 0, 1024, (void *)(dst + p0 * 4096));
    esps.pages += p - p0; esp_pages += p - p0; runs++;
  }
  if (runs > esps.runs_max) esps.runs_max = runs;
  ((volatile u32 *)((rl + 0x1FF0) | 0xA0000000))[0] = seq; // la copia final la pone en SB[0x40]
  sceGuCopyImage(GU_PSM_8888, 0, 0, 4, 1, 64, (void *)(rl + 0x1FF0), 0, 0, 64, (void *)0x0A000100);
  sceGuFinish();
  esp_qid[s] = _sceGeListEnQueue((void *)rl, NULL, -1, NULL);
  if (SB[0x4A] & 32 && esp_qid[s] >= 0) _sceGeListSync(esp_qid[s], 0);  // (0.0.3.2, prueba) el juego espera la copia: que no
  esps.copies++;                                                         // alcance a cambiar la VRAM mientras se copia
  SB[0x44] = seq; SB[0x45] = esp_pages;
}

// Primer gancho que llama el juego: buzon listo y ID del juego en el log
static void modeDecide(void) {
  mode_done = 1;
  if (passive)
    return;
  logGameId();
  activo = 1;
  *(u32 *)DRAW_NATIVE = 0; // Adrenaline muestra la imagen del juego
  sbInit();
  LOG("NativeLink activo: el juego corre sin cambios; buzon de eventos en 0x0A000000 para el capturador de NativeDraw\n");
}

int sceGeListUpdateStallAddrPatched(int qid, void *stall) {
  int r = _sceGeListUpdateStallAddr(qid, stall);
  st.stalls++;
  if (activo)
    sbPost(EV_STALL, 0, qid, (u32)stall, 0);
  return r;
}

int sceGeListEnQueuePatched(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
  if (!mode_done)
    modeDecide();
  st.lists++;
  u32 gw = activo ? gateBeforeEnq() : 0; // (dev66)
  vidaCheck(0); // (dev65.2) antes de encolar: la lista anterior sigue intacta?
  int r = _sceGeListEnQueue(list, stall, cbid, arg);
  if (activo && r >= 0) {
    sbPost(EV_ENQ, gw, (u32)list, (u32)stall, r); // (dev66: dato = espera antes de encolar, us | 1)
    if (!ge_kick) ge_kick = sceKernelGetSystemTimeLow() | 1;
  }
  return r;
}

int sceGeListEnQueueHeadPatched(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
  if (!mode_done)
    modeDecide();
  st.heads++;
  u32 gw = activo ? gateBeforeEnq() : 0; // (dev66)
  int r = _sceGeListEnQueueHead(list, stall, cbid, arg);
  if (activo && r >= 0) {
    sbPost(EV_HEAD, gw, (u32)list, (u32)stall, r);
    if (!ge_kick) ge_kick = sceKernelGetSystemTimeLow() | 1;
  }
  return r;
}

// Memoria y CPU en este momento (al iniciar y en cada estadistica)
static void memSnapshot(const char *why) {
  static int (*freeTotal)(void), (*freeMax)(void), (*cpu)(void), (*bus)(void), init;
  if (!init) {
    init = 1;
    freeTotal = (void *)FindProc("sceSystemMemoryManager", "SysMemUserForUser", 0xF919F628);
    freeMax = (void *)FindProc("sceSystemMemoryManager", "SysMemUserForUser", 0xA291F107);
    cpu = (void *)FindProc("scePower_Service", "scePower", 0xFDB5BFE9);
    bus = (void *)FindProc("scePower_Service", "scePower", 0xBD681969);
  }
  int k1 = pspSdkSetK1(0);
  int ft = freeTotal ? freeTotal() / 1024 : -1, fm = freeMax ? freeMax() / 1024 : -1;
  int c = cpu ? cpu() : -1, b = bus ? bus() : -1;
  pspSdkSetK1(k1);
  LOG("memoria (%s): ram del juego libre=%d KB (bloque mayor %d KB) | cpu=%d MHz bus=%d MHz\n", why, ft, fm, c, b);
}

int sceDisplaySetFrameBufPatched(void *topaddr, int bufferwidth, int pixelformat, int sync) {
  if (!mode_done)
    modeDecide();
  log_frame++;
  if (log_frame == 30)
    memSnapshot("inicio");
  if (activo) {
    if (SB[0] != SB_MAGIC) // (por si algo limpia la zona, como Adrenaline 8 al cargar el juego)
      sbInit();
    if (VDESC[0] != 0x31565047) // "GPV1": el log lo escribe NativeDraw (buzon del descriptor, log.h)
      VDESC[0] = 0x31565047;
    t4Flip();
    int k1e = pspSdkSetK1(0);
    u32 te = sceKernelGetSystemTimeLow(), pg0 = esp_pages;
    espejoFlip(); // copia de la VRAM pedida por la Vita (dev62)
    fr.esp_us += sceKernelGetSystemTimeLow() - te; fr.esp_pages += esp_pages - pg0;
    pspSdkSetK1(k1e);
    { // (dev65) lo que le costo este frame al juego (ver EV_FRAME)
      static u32 ft, sp0;
      u32 tn = sceKernelGetSystemTimeLow(), sp = spin, fd = ft ? tn - ft : 0;
      ft = tn;
      sb_x[0] = sp - sp0; sb_x[1] = fr.esp_us | (fr.esp_pages > 0xFFFF ? 0xFFFF : fr.esp_pages) << 16; sb_x[2] = fr.post_us;
      sp0 = sp;
      if (fd && fd < 5000000) {
        frs.frames++; frs.frame_us += fd; frs.spin += sb_x[0]; frs.gate_us += fr.gate_us; frs.gate_n += fr.gate_n;
        frs.nowait += fr.nowait; frs.sync_us += fr.sync_us; frs.post_us += fr.post_us; frs.esp_us += fr.esp_us; frs.esp_pages += fr.esp_pages;
      }
      u32 gn = fr.gate_n > 255 ? 255 : fr.gate_n, nw = fr.nowait > 255 ? 255 : fr.nowait, gu = fr.gate_us, su = fr.sync_us;
      memset(&fr, 0, sizeof(fr));
      if (SB[0x4E] >= 1) sbPost(EV_FRAME, gn | nw << 8, fd, gu, su); // (lo que cuesta este sbPost va al frame siguiente)
    }
    sbPost(EV_FLIP, 0, (u32)topaddr, bufferwidth | pixelformat << 16, log_frame);
  }
  // Cada buffer distinto que el juego muestra
  LOG_ONCE((u32)topaddr ^ (bufferwidth << 20) ^ (pixelformat << 30), "display %08lx ancho=%d fmt=%d\n", (u32)topaddr, bufferwidth, pixelformat);

  // Tiempo entre flips (fps)
  u32 now = sceKernelGetSystemTimeLow(), dt = now - last_flip;
  if (last_flip && dt < 5000000) {
    st.frames++;
    st.frame_us += dt;
    if (dt > st.frame_max)
      st.frame_max = dt;
    st.slow60 += dt > 17500; // bajo 60 fps
    st.slow30 += dt > 34000; // bajo 30 fps
    if (dt > 200000 && log_frame > 120 && !st.long_logged) {
      st.long_logged = 1;
      LOG("frame lento: %lu ms\n", dt / 1000);
    }
  }
  last_flip = now;

  if (log_frame % STATS_EVERY == 0) {
    LOG("stats listas=%lu head=%lu stall=%lu\n", st.lists, st.heads, st.stalls);
    memSnapshot("stats");
    if (st.frames) {
      u32 fps10 = st.frames * 100000 / (st.frame_us / 100 + 1); // fps x10, sin division de 64 bits
      LOG("stats rendimiento: fps=%lu.%lu frame_max=%lu ms bajo60=%lu bajo30=%lu\n",
          fps10 / 10, fps10 % 10, st.frame_max / 1000, st.slow60, st.slow30);
    }
    if (activo) {
      u32 s = sbs.syncs ? sbs.syncs : 1;
      LOG("stats sombra: eventos=%lu listas=%lu vita=%s capturadas=%lu (faltan %ld) leidos=%lu\n", sbs.ev, sb_ord,
          sbAlive() ? "si" : "NO", SB[3], (long)(sb_ord - SB[3]), SB[5]);
      LOG("stats sombra sync: drawsync=%lu atrasados=%lu (>5 ms: %lu) espera=%lu us/sync (max %lu) listsync=%lu atrasados=%lu\n",
          sbs.syncs, sbs.late, sbs.timeouts, sbs.wait_us / s, sbs.wait_max, sbs.lsyncs, sbs.llate);
      LOG("stats sombra GE: el juego espera al GE en drawsync %lu us/sync\n", sbs.in_sync_us / s);
      LOG("stats compuerta (dev66): %s%s | esperas antes de encolar=%lu (%lu us c/u, max %lu) | autocomprobaciones=%lu cambiaron=%lu\n",
          gate_mode == 1 ? "antes de encolar" : gate_mode ? "en el sync (la lista cambio 10 veces)" : "en el sync (probando)", SB[0x4A] & 8 ? " [compuerta_sync]" : "",
          gts.n, gts.n ? gts.us / gts.n : 0, gts.max, gts.checks, gts.bad);
      memset(&gts, 0, sizeof(gts));
      if (frs.frames) { // (dev65) impacto en el juego, por frame
        u32 f = frs.frames, ms = frs.frame_us / 1000 + 1;
        LOG("stats impacto (por frame): frame=%lu us CPU_libre=%lu%% compuerta=%lu us (%lu esperas en 10 s, %lu sin esperar) sync_GE=%lu us ganchos=%lu us espejo=%lu us (%lu paginas)\n",
            frs.frame_us / f, spin_cal ? frs.spin / spin_cal * 100 / ms : 0, frs.gate_us / f, frs.gate_n, frs.nowait, frs.sync_us / f,
            frs.post_us / f, frs.esp_us / f, frs.esp_pages / f);
      }
      memset(&frs, 0, sizeof(frs));
      LOG("stats T6 (por frame): GE ocupado %lu us en %lu tramos (el mas largo %lu us), en stall %lu%% | T4 (por frame): cache %lu (%lu KB) dma %lu (%lu KB)\n",
          ges.busy_us / STATS_EVERY, ges.runs / STATS_EVERY, ges.run_max, ges.n ? ges.stall_n * 100 / ges.n : 0,
          t4s.n / STATS_EVERY, t4s.kb / STATS_EVERY, t4s.dn / STATS_EVERY, t4s.dkb / STATS_EVERY);
      memset(&ges, 0, sizeof(ges)); memset(&t4s, 0, sizeof(t4s));
      u32 pl = 0, pd = 0, i;
      for (i = 0; i < 512; i++) pl += SB[0x20 + (i >> 5)] >> (i & 31) & 1, pd += SB[0x30 + (i >> 5)] >> (i & 31) & 1;
      LOG("stats espejo: pedidas listas=%lu datos=%lu (%lu KB) | copias=%lu paginas=%lu (KB/frame %lu)\n", pl, pd, (pl + pd) * 4,
          esps.copies, esps.pages, esps.pages * 4 / STATS_EVERY);
      LOG("stats espejo: tramos_max=%lu ocupado=%lu vita_usando=%lu no_entraron=%lu terminada=%lu/%lu\n", esps.runs_max,
          esps.busy, esps.vita, esps.over, SB[0x40], esp_seq);
      esps.copies = esps.pages = esps.busy = esps.vita = esps.over = esps.runs_max = 0;
      memset(&sbs, 0, sizeof(sbs));
    }
    memset(&st, 0, sizeof(st));
  }

  return _sceDisplaySetFrameBuf(topaddr, bufferwidth, pixelformat, sync);
}

// Adrenaline 8 da al juego (MEMSIZE en el SFO, memoria alta, homebrew de 40 MB) memoria que puede llegar a la zona de los
// plugins, y crea ahi la particion 11, donde carga los plugins de usuario. Si el juego llega a 0x0A000000 NativeLink no
// puede funcionar (1). Si no, se reserva la zona en la particion 11 para que nadie mas la use.
static int zoneCheck(void) {
  PspSysmemPartitionInfo pi;
  pi.size = sizeof(pi);
  int r = sceKernelQueryMemoryPartitionInfo(2, &pi);
  u32 fin = r < 0 ? 0 : (pi.startaddr & 0x0fffffff) + pi.memsize;
  LOG("particion del juego: %08x-%08lx (%u KB) [%08x]\n", pi.startaddr, fin, r < 0 ? 0 : pi.memsize >> 10, r);
  PspSysmemPartitionInfo p11;
  p11.size = sizeof(p11);
  int r11 = sceKernelQueryMemoryPartitionInfo(11, &p11);
  LOG("particion 11: %08x (%u KB, attr %x) [%08x]\n", r11 < 0 ? 0 : p11.startaddr, r11 < 0 ? 0 : p11.memsize >> 10,
      r11 < 0 ? 0 : p11.attr, r11);
  if (fin > ZONA)
    return 1;
  SceUID id = sceKernelAllocPartitionMemory(11, "NativeLink", PSP_SMEM_Addr, 0x01000000, (void *)ZONA);
  LOG("reserva de 0x0A000000-0x0B000000 en la particion 11: %08x%s\n", id,
      id >= 0 ? "" : " (Adrenaline 7, o algo ya esta ahi: un plugin de usuario cargado en la particion 11)");
  return 0;
}

int module_start(SceSize args, void *argp) {
  SceCtrlData pad;
  sceCtrlPeekBufferPositive(&pad, 1);
  // L o SELECT al abrir el juego: apagado (L es el atajo original de GePatch; en Adrenaline 8 mantener L ademas
  // desactiva todos los plugins)
  passive = !!(pad.Buttons & (PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER));
  logInit(pad.Buttons & PSP_CTRL_RTRIGGER);
  if (passive)
    LOG("apagado (L o SELECT al abrir el juego): el juego corre sin cambios y no se anota nada para NativeDraw\n");
  if (zoneCheck() && !passive) {
    passive = 1;
    LOG("AVISO: el juego usa la zona de los plugins (0x0A000000-0x0B000000) -> apagado\n");
  }
  if (!passive) { // el log lo escribe NativeDraw en cuanto responde (log.h)
    VDESC[0] = 0x31565047; VDESC[1] = 0; // "GPV1"
  }

  _sceGeListUpdateStallAddr = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xE0D68148);
  _sceGeListEnQueue = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xAB49E76A);
  _sceGeListEnQueueHead = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x1C0D95A6);
  _sceGeDrawSync = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0xB287BD61);
  _sceGeListSync = (void *)FindProc("sceGE_Manager", "sceGe_driver", 0x03444EB4);
  _sceDisplaySetFrameBuf = (void *)FindProc("sceDisplay_Service", "sceDisplay_driver", 0x289D82FE);
  if (!_sceGeListUpdateStallAddr || !_sceGeListEnQueue || !_sceGeListEnQueueHead || !_sceGeDrawSync || !_sceGeListSync ||
      !_sceDisplaySetFrameBuf) {
    LOG("ERROR: no se encontro alguna funcion del GE o de la pantalla: NativeLink no engancha nada\n");
    return 0;
  }
  sctrlHENPatchSyscall((u32)_sceGeListUpdateStallAddr, sceGeListUpdateStallAddrPatched);
  sctrlHENPatchSyscall((u32)_sceGeListEnQueue, sceGeListEnQueuePatched);
  sctrlHENPatchSyscall((u32)_sceGeListEnQueueHead, sceGeListEnQueueHeadPatched);
  sctrlHENPatchSyscall((u32)_sceGeDrawSync, sceGeDrawSyncPatched);
  sctrlHENPatchSyscall((u32)_sceGeListSync, sceGeListSyncPatched);
  t4t6Install(); // (los ganchos solo anotan si NativeLink esta activo)
  sctrlHENPatchSyscall((u32)_sceDisplaySetFrameBuf, sceDisplaySetFrameBufPatched);

  sceKernelDcacheWritebackInvalidateAll();
  sceKernelIcacheClearAll();
  return 0;
}
