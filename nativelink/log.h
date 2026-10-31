// Log de NativeLink (implementacion en log.c, lo incluye main.c) y version del plugin. Tambien define el descriptor
// compartido con NativeDraw (VDESC), por el que la Vita escribe el log a la tarjeta.
#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <psptypes.h>

// Log en ms0:/nativelink.log (la sesion anterior queda en nativelink_prev.log). Las lineas van a un buffer en RAM y un
// hilo las escribe cada ~1 s, asi el hilo del juego nunca espera a la tarjeta. Mantener R al abrir el juego activa el
// modo sincrono: cada linea se escribe al instante (mas lento, pero no se pierde nada si el juego se cuelga).
// Cada linea parte con el numero de frame (cuantas veces se llamo sceDisplaySetFrameBuf).

#define NATIVELINK_VERSION "0.0.3.4" // tools/compilar.sh lo lee de aqui

extern u32 log_frame;

void logInit(int sync);
void logWrite(const char *s, int n);
void logFlush(void);
void logGameId(void);
int logOnce(u32 key); // 1 la primera vez que se ve 'key', 0 las siguientes

// Canal con NativeDraw: la Vita escribe el log, asi el PSP no espera a la tarjeta. Va en una pagina sin cache de la
// zona de los plugins; el buzon tiene seq (lo sube el PSP al dejar un pedido) y done (lo iguala NativeDraw al
// terminarlo).
//   +0x00 "GPV1": NativeLink esta (lo repone en cada flip: Adrenaline 8 limpia la zona al cargar el juego)
//   +0x20 "LOGV": NativeDraw atiende el buzon (lo repone siempre)
//   +0x40 log: seq, done, bytes (texto en VLOG_BUF, 16 KB)
// Sin NativeDraw (o si no responde en 2 s) el PSP escribe con sceIo en ms0:/nativelink.log.
#define VDESC ((volatile u32 *)(0x0A7FF000 | 0x40000000))
#define VLOG 16
#define VLOG_BUF 0x0A7FA000

// Escribe una linea con formato printf (hasta 160 bytes con el numero de frame; lo que sobra se corta)
#define LOG(...) do { \
  char _b[256]; \
  int _n = snprintf(_b, sizeof(_b), "%06lu ", log_frame); \
  _n += snprintf(_b + _n, sizeof(_b) - _n, __VA_ARGS__); \
  logWrite(_b, _n < (int)sizeof(_b) ? _n : (int)sizeof(_b) - 1); \
} while (0)

// Para eventos que se repiten cada frame: solo se escribe la primera vez por 'key'
#define LOG_ONCE(key, ...) do { if (logOnce(key)) LOG(__VA_ARGS__); } while (0)

#endif
