# Laboratorio en el Mac

PPSSPP con interfaz y el parche del modelo de Adrenaline (`../ppsspp-vita/adrenaline.patch`),
para jugar con GePatch cargado desde `memstick/seplugins/game.txt`, como en la Vita.

Carpeta de trabajo: copiar estos scripts y `../ppsspp-vita/adrenaline.patch` a una carpeta
(el usuario usa `~/Documents/gepatch-lab`) con `memstick/seplugins/{ge_patch.prx,game.txt}` e `iso/`.

- `bash compilar.sh`: instala las dependencias con Homebrew (cmake, pkgconf, sdl3, sdl3_ttf,
  fontconfig, fribidi), clona PPSSPP en el commit del parche, lo aplica y compila
  (`ppsspp/build/PPSSPPSDL.app`). La primera vez tarda unos 15–20 minutos.
- `bash jugar.sh "juego.iso"`: abre el juego con `PPSSPP_ADRENALINE=1`, el renderer por software
  (el que imita al GE byte a byte) y la API de depuración en `localhost:45678`.

Los volcados (L+R+SELECT) y `ge_patch.log` quedan en `memstick/`. Más detalle en `docs/laboratorio_mac.md`.
