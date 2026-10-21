#ifndef MENU_H
#define MENU_H

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
} Options;

extern Options opt;

// Lo llama el flip: lee los botones, maneja el menu y lo dibuja sobre la imagen.
// buf: imagen de 960x544 en 565 (con cache) o NULL. Devuelve 1 si dibujo (hay que
// escribir la cache a memoria).
int menuFrame(u16 *buf);
int menuOpen(void);
void menuInstallCtrlHooks(void);
void drawText(u16 *buf, int x, int y, const char *s, u16 fg, u16 bg);

// Acciones del menu que ejecuta main.c
void menuRequestCapture(void);
void memSnapshot(const char *why);
void rtReset(void);

#endif
