#ifndef MENU_INCLUDED
#define MENU_INCLUDED

#include <psptypes.h>

// Opciones que se pueden cambiar en tiempo de ejecucion desde el menu (L+R+START).
// Sirven para diagnosticar: cada cambio queda en el log con el frame en que se hizo.
typedef struct {
  int rt_on;      // dibujar render targets en la VRAM alta (si no: se omiten)
  int rt_scale;   // escala de los render targets: 1 o 2
  int omit_fbtex; // draws que leen un framebuffer como textura: 0 leer la memoria, 1 omitir, 2 copia reducida del frame
  int fallback;   // imagen original escalada: 0 auto, 1 siempre, 2 nunca
  int nop_mark;   // NOP de omision con marca (1) o NOP 0 como antes de dev12 (0)
  int walk_new;   // recorrido de listas nuevo (1) o el de antes de dev12 (0)
  int wide1024;   // tratar framebuffers de 1024 de ancho como pantalla
  int dx, dy;     // desplazamiento del area visible dentro del framebuffer del juego (px)
  int v2d;        // duplicar vertices 2D (HUD, menus, sprites)
  int tx_ge;      // copias grandes a la pantalla (videos): escalar con el GE en su lugar de la lista
  int walk_cache; // no recorrer de nuevo sublistas que no cambiaron
  int layers;     // fb intermedios (nunca mostrados, usados como textura) como render target
  int autocap;    // captura automatica cuando aparece algo raro (max 5 por sesion)
} Options;

extern Options opt;
extern const Options opt_default;
extern u32 opt_gen; // cambia con cada cambio en el menu

// Lo llama el flip: lee los botones y maneja el menu. Devuelve 1 si esta abierto; su
// imagen (MENU_W x MENU_H, 565) queda en MENU_BUF y va en (MENU_X, MENU_Y) de la pantalla.
#define MENU_BUF 0x0A900000
#define MENU_W 512
#define MENU_H (16 * 21 + 48) // hasta 20 opciones + linea de ayuda
#define MENU_X 224
#define MENU_Y 56
int menuFrame(void);
void menuLoad(const char *game);   // opciones guardadas para el juego, si hay
int menuParse(const char *text);   // aplica lineas clave=valor; devuelve cuantas
int menuOpen(void);
void menuInstallCtrlHooks(void);
void drawText(u16 *buf, int x, int y, const char *s, u16 fg, u16 bg);

// Acciones del menu que ejecuta main.c
void menuRequestCapture(void);
void memSnapshot(const char *why);
void rtReset(void);

#endif
