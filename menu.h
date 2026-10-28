#ifndef MENU_INCLUDED
#define MENU_INCLUDED

#include <psptypes.h>

// Opciones que se pueden cambiar en tiempo de ejecucion desde el menu (L+R+START).
// Sirven para diagnosticar: cada cambio queda en el log con el frame en que se hizo.
typedef struct {
  int omit_fbtex; // draws que leen un framebuffer como textura: 0 leer la memoria, 1 omitir
  int fallback;   // imagen original escalada: 0 auto, 1 siempre, 2 nunca
  int wide1024;   // tratar framebuffers de 1024 de ancho como pantalla
  int dx, dy;     // desplazamiento del area visible dentro del framebuffer del juego (px)
  int v2d;        // duplicar vertices 2D (HUD, menus, sprites)
  int tx_ge;      // copias grandes a la pantalla (videos): escalar con el GE en su lugar de la lista
  int walk_cache; // no recorrer de nuevo sublistas que no cambiaron
  int wc_check;   // comprobar que no cambiaron cada 2^n frames (0: cada frame)
  int vram_fix;   // texturas y copias con la VRAM real del juego -> VRAM falsa (Silent Hill)
  int autocap;    // captura automatica cuando aparece algo raro (max 5 por sesion)
  int cpu_draw;   // lo que la CPU dibuja directo en el fb mostrado (spinner de SH) se pega encima
  int swap;       // pase que se mezcla con la pantalla: se dibuja en el buffer grande guardando la escena en la RAM
  int rt1;        // render targets a escala 1 en la VRAM del z-buffer, copiados a la VRAM falsa al terminar
  int escala;     // escala del buffer grande (indice de scales[] en main.c); se aplica al abrir el juego
  int perfil;     // perfil del recorrido en el log: 0 no, 1 reloj, 2 reloj + contadores del Allegrex
  int espera;     // segundos entre soltar L+R+SELECT y el volcado (0: al instante)
  int sin_efecto; // omitir draws que no pueden cambiar el buffer 565 (color enmascarado y sin z: solo stencil)
  int sombra;     // modo sombra (etapa 2 de la arquitectura B): el juego corre sin cambios y la Vita lee sus listas
} Options;

extern Options opt;
extern const Options opt_default;
extern u32 opt_gen; // cambia con cada cambio en el menu

// Lo llama el flip: lee los botones y maneja el menu. Devuelve 1 si esta abierto; su
// imagen (MENU_W x MENU_H, 565) queda en MENU_BUF y va en (MENU_X, MENU_Y) de la pantalla.
#define MENU_BUF 0x0A900000
#define MENU_W 512
#define MENU_H (16 * 28 + 48) // hasta 27 opciones + linea de ayuda (termina antes del buffer del volcado)
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
void menuSaveGame(void); // guarda las opciones del juego (modo sombra: L+R+START lo apaga)
void memSnapshot(const char *why);
void optChanged(void); // main.c: las decisiones guardadas (texturas) se vuelven a calcular

#endif
