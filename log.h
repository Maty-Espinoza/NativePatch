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

#define GEPATCH_VERSION "0.2-dev64"

extern u32 log_frame;
extern u32 log_us; // tiempo escribiendo el log, en us (lo lee y lo pone en 0 el perfil)
extern u32 log_io_psp; // escrituras del log que hizo el PSP con sceIo (sin gepresent v6)

void logInit(int sync);
void logWrite(const char *s, int n);
void logFlush(void);
void logGameId(void);
extern char game_id[11]; // ID del juego (ULUS-10450...), desde el frame 30
void logEager(int on);
int logOnce(u32 key); // 1 la primera vez que se ve 'key', 0 las siguientes
void logResetOnce(void);
extern void (*log_tap)(const char *s, int n); // copia de cada linea (volcado binario)

// Canal con gepresent (plan A, paso 1, dev56.5): la Vita escribe el log y los volcados. Va en la pagina del
// descriptor de la presentacion (PRES_DESC en main.c), sin cache; cada buzon tiene seq (lo sube el PSP al dejar
// un pedido) y done (lo iguala gepresent al terminarlo).
//   +0x20 "LOGV": gepresent v6 atiende los buzones (lo repone siempre, como "PRES")
//   +0x40 log:      seq, done, bytes                  (texto en VLOG_BUF, 16 KB)
//   +0x60 volcado:  seq, done, direccion, bytes, banderas (1 abrir, 2 cerrar); +0x80 nombre del archivo (48 bytes)
// Sin gepresent v6 (o si no responde en 2 s) el PSP escribe con sceIo, como antes.
#define VDESC ((volatile u32 *)(0x0A7FF000 | 0x40000000))
#define VLOG 16
#define VDUMP 24
#define VLOG_BUF 0x0A7FA000
int vitaIo(void);        // 1 si gepresent escribe los archivos
int vitaWait(int box);   // espera a que gepresent termine el pedido anterior del buzon (0 si no responde)
extern u32 vita_wait_us; // tiempo total esperando a gepresent (us)
void vitaPost(int box);  // deja el pedido (los datos ya escritos)

#define LOG(...) do { \
  char _b[160]; \
  int _n = snprintf(_b, sizeof(_b), "%06lu ", log_frame); \
  _n += snprintf(_b + _n, sizeof(_b) - _n, __VA_ARGS__); \
  logWrite(_b, _n < (int)sizeof(_b) ? _n : (int)sizeof(_b) - 1); \
} while (0)

// Para eventos que se repiten cada frame: solo se escribe la primera vez por 'key'
#define LOG_ONCE(key, ...) do { if (logOnce(key)) LOG(__VA_ARGS__); } while (0)

#endif
