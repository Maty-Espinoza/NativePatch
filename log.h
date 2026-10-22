#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <psptypes.h>

// Log liviano para ms0:/ge_patch.log (la sesion anterior queda en ge_patch_prev.log).
// Las lineas van a un buffer en RAM y un hilo las escribe a la tarjeta cada ~1 s,
// asi el hilo del juego nunca espera por la memory stick.
// Mantener R al iniciar el juego activa el modo sincrono: escribe cada linea al
// instante (mas lento, pero no se pierde nada si el juego se cuelga).
// L+R+SELECT durante el juego: captura detallada de un frame (draws y estado del GE).
// Cada linea parte con el numero de frame (cuantas veces se llamo sceDisplaySetFrameBuf).

#define GEPATCH_VERSION "0.2-dev36"

extern u32 log_frame;

void logInit(int sync);
void logWrite(const char *s, int n);
void logFlush(void);
void logGameId(void);
extern char game_id[11]; // ID del juego (ULUS-10450...), desde el frame 30
void logEager(int on);
int logOnce(u32 key); // 1 la primera vez que se ve 'key', 0 las siguientes
void logResetOnce(void);
extern void (*log_tap)(const char *s, int n); // copia de cada linea (volcado binario)

#define LOG(...) do { \
  char _b[160]; \
  int _n = snprintf(_b, sizeof(_b), "%06lu ", log_frame); \
  _n += snprintf(_b + _n, sizeof(_b) - _n, __VA_ARGS__); \
  logWrite(_b, _n < (int)sizeof(_b) ? _n : (int)sizeof(_b) - 1); \
} while (0)

// Para eventos que se repiten cada frame: solo se escribe la primera vez por 'key'
#define LOG_ONCE(key, ...) do { if (logOnce(key)) LOG(__VA_ARGS__); } while (0)

#endif
