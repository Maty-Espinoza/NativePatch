// KBENCH: protocolo compartido entre el PSP (MIPS, kbench.prx) y el ARM de la Vita (kbench.suprx).
// Todo lo que cruza Kermit o el buzón está definido aquí; los dos lados se compilan con este archivo.
#ifndef KBENCH_H
#define KBENCH_H
#include <stdint.h>

#define KB_VERSION 1
#define KB_MAGIC   0x4E45424Bu   // "KBEN"
#define KB_ECHO    0x5A5A0000u   // la respuesta lleva (seq ^ KB_ECHO) en los 32 bits altos
#define KB_MBOX    0x0A000000u   // página de 4 KB en la RAM del PSP: buzón del ARM (64 bytes) y canal compartido
#define KB_SHM_REQ (KB_MBOX + 0x100) // petición por memoria compartida (la escribe el PSP)
#define KB_PSP     (KB_MBOX + 0x80)  // [KB_PSP_MAGIC, nonce]: lo escribe kbench.prx para despertar al ARM
#define KB_PSP_MAGIC 0x4B505350u     // "PSPK"
// El ARM solo LEE la página mientras no ve KB_PSP_MAGIC: el XMB y los juegos con memoria alta usan
// 0x0A000000, y escribir ahí sin que KBENCH esté corriendo podría romperlos.
#define KB_SHM_RES (KB_MBOX + 0x140) // respuesta (la escribe el ARM)
// Petición compartida (palabras): [0] timbre = seq, [1] cmd, [2] nargs, [3] -, [4..11] args (args[0] = seq)
// Respuesta: [0] timbre = seq ya atendido, [1] resultado, [2] eco (seq ^ KB_ECHO)

// Modo Kermit: KERMIT_MODE_EXTRA_1 (14 en Adrenaline 8 c8475f5; el 13 es UTILITY de Sony).
// OJO: Adrenaline 7 y 8 reasignan ScePspemuRemotePocs (PS1) a este modo. Por eso el ARM solo escucha
// Kermit si el PSP lo pide (KB_KERMIT_ON por memoria compartida) y Adrenaline no está en modo PS1.
// Se toma de adrenaline_vita.h (ADRENALINE_SRC=...); no hay valor por defecto. walk.c no lo necesita.
#ifndef KB_NO_MODE
#ifdef KB_USE_ADRENALINE_H
#include "adrenaline_vita.h"
#define KB_MODE KERMIT_MODE_EXTRA_1
#endif
#ifndef KB_MODE
#error "Falta el modo Kermit: compilar con ADRENALINE_SRC=<raiz de Adrenaline> o KB_MODE=N"
#endif
#endif
#ifndef KERMIT_INPUT_MODE
#define KERMIT_INPUT_MODE  1
#define KERMIT_OUTPUT_MODE 2
#endif

// Petición de Kermit (0x80 bytes); mismo diseño que SceKermitRequest de Adrenaline.
typedef struct {
  uint32_t cmd, sema;
  uint32_t resp, pad;        // puntero del PSP a la respuesta (lo llena el sistema)
  uint64_t args[14];
} KbKermitReq;

// Comandos. args[0] es siempre el número de secuencia; los demás dependen del comando.
enum {
  KB_HELLO = 1,  // -> KB_VERSION
  KB_PING,       // -> 0
  KB_READ,       // a1 dir, a2 bytes, a3 flags            -> suma de verificación (kbSum)
  KB_WRITE,      // a1 dir, a2 bytes, a3 semilla          -> 0 (escribe kbPat)
  KB_WALK,       // a1 base, a2 bytes, a3 lista, a4 salida, a5 bytes de salida, a6 flags -> KbWalkStats.sum
  KB_AFFINITY,   // a1 máscara de núcleos (0x10000 núcleo 0, 0x20000 núcleo 1, 0x40000 núcleo 2, 0x70000 todos)
  KB_PRIORITY,   // a1 prioridad del hilo del ARM
  KB_GETLOG,     // a1 dir, a2 máximo de entradas, a3 1 = vaciar -> entradas copiadas (KbEnt)
  KB_SPIN,       // a1 µs de espera activa en el ARM (máx. 100 ms)
  KB_INFO,       // a1 dir: escribe KbInfo
  KB_SHMPOLL,    // a1 µs entre sondeos del canal compartido (0 = sondeo continuo hasta 5 ms sin peticiones)
  KB_KERMIT_ON,  // (solo por memoria compartida) arranca el hilo de Kermit -> 1 escuchando, 0 rechazado (modo PS1)
};
// KbMbox.flags
#define KB_MB_KERMIT 1  // el hilo de Kermit está esperando peticiones
#define KB_MB_POPS   2  // Adrenaline está en modo PS1 (no se usa Kermit)
#define KB_MB_SHM    4  // el canal compartido está atendiendo
// Flags de KB_READ y KB_WALK
#define KB_F_INVAL  1  // ScePspemuConvertAddress invalida la caché del ARM en todo el rango (lo correcto)
#define KB_F_WBACK  2  // escribir a memoria la salida al terminar (ScePspemuWritebackCache)
#define KB_F_DIRECT 4  // vista directa 0x68000000 + (dir & 0x0FFFFFFF), sin ConvertAddress (experimental)
#define KB_F_CACHED 8  // vista con caché del ARM: 0x59000000 + dir (Adrenaline: 0x8A000000 = 0x63000000) + sceCompatCache
#define KB_F_COPY  16  // copiar primero a memoria propia del ARM con memcpy y trabajar ahí
#define KB_F_DMA   32  // copiar primero a memoria propia del ARM con sceDmacMemcpy y trabajar ahí

// Errores (32 bits bajos de la respuesta)
#define KB_ERR_CMD  0xE0000001u
#define KB_ERR_ARGS 0xE0000002u
#define KB_ERR_MAP  0xE0000003u

// Buzón: lo escribe el ARM cada 50 ms. El PSP solo envía peticiones si lo ve vivo.
typedef struct {
  uint32_t magic, version, mode, status;   // status: 0 listo, 1 iniciando, 2 offsets no reconocidos
  uint32_t counter, arm_time, seen, last_cmd; // seen: peticiones recibidas (sirve para diagnosticar cuelgues)
  uint32_t answered, errors, arm_mhz, bus_mhz;
  uint32_t gpu_mhz, xbar_mhz, text_addr, flags;
} KbMbox;

// Registro por petición en el ARM (tiempos en µs del reloj del ARM)
typedef struct {
  uint32_t seq, cmd;
  uint32_t t0;   // salió de KermitWaitAndGetRequest
  uint32_t t1;   // memoria lista (después de ConvertAddress)
  uint32_t t2;   // trabajo terminado
  uint32_t t3;   // justo antes de KermitSendResponse
  uint32_t res, pad;
} KbEnt;

typedef struct {
  uint32_t version, text_addr, text_size, mode;
  uint32_t off[4];           // offsets usados en ScePspemu
  uint8_t  head[4][8];       // primeros bytes de cada función (para verificar los offsets)
} KbInfo;

typedef struct {
  uint32_t cmds, prims, through, verts, patches, calls, errors, sum;
} KbWalkStats;

// Suma de verificación y patrón usados por READ/WRITE (iguales en los dos lados)
static inline uint32_t kbSum(const volatile uint32_t *p, uint32_t words) {
  uint32_t s = 0;
  for (uint32_t i = 0; i < words; i++) s = ((s << 1) | (s >> 31)) ^ p[i];
  return s;
}
static inline uint32_t kbPat(uint32_t seed, uint32_t i) { return seed + i * 0x9E3779B1u; }

#endif
