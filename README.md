# GE Patch Plugin (NativePatch)

Plugin para [Adrenaline](https://github.com/TheOfficialFloW/Adrenaline) que muestra los juegos de PSP
en la PS Vita a resolución nativa (960x544). Este fork revive el GePatch original de TheFlow
(abandonado en v0.2) y corrige sus errores. Versión actual: 0.2-dev49 (rama `dev`).

- **Compilar:** `make ADRENALINE=<ruta a Adrenaline/cef>` (toolchain pspdev). Tests en el PC: `make -C tests`.
- **Instalar:** `ux0:/pspemu/seplugins/ge_patch.prx` y en `game.txt`: `ms0:/seplugins/ge_patch.prx 1`.
- **En el juego:** L al arrancar = modo pasivo (sin cambios, como referencia); L+R+START = menú de opciones;
  L+R+SELECT = captura en el log y volcado `.gpd` de 3 frames. El log queda en `ms0:/ge_patch.log`.
- **Documentación:** [`docs/CONTEXTO_IA.md`](docs/CONTEXTO_IA.md) (arquitectura, memoria, estado, cómo
  continuar) y los demás documentos de `docs/`. Herramientas en `tools/` (`gpd.py` para los volcados,
  modelo de Adrenaline en PPSSPP, pruebas del GE, laboratorio en el Mac, relevo con la Vita).

---

Historial del GePatch original (TheFlow):


This is an experimental plugin for [Adrenaline](https://github.com/TheOfficialFloW/Adrenaline) that allows you to play a few games in native resolution.

## Compatibility List

Please help testing games and filling out the [spreadsheet](https://docs.google.com/spreadsheets/d/1aZlmKwELcdpCb9ezI5iRfgcX9hoGxgL4tNC-673aKqk/edit#gid=0).

## Changelog v0.2

- Added ability to skip GE patches by holding L while starting a game.
- Changed framebuffer copy algorithm.
- Changed behavior of sync opcode.
- Disabled forced dithering again.

## Changelog v0.19.1

- Removed optimization introduced earlier since it's not working.
- Forced dithering on.

## Changelog v0.19

- Fixed a small bug that was introduced earlier.
- Fixed a few bugs that caused certain games to crash.

## Changelog v0.18.1

**This must be used with Adrenaline-7, not Adrenaline-6.9!**

- Fixed bug that enables more games to render without smear.

## Changelog v0.18

**This must be used with Adrenaline-7, not Adrenaline-6.9!**

- Changed fake vram address to allow more games to work.
- Added patch to allow games to use more memory of fake vram to store textures.
- Added optimization to prevent double patching of vertices. May increase performance in some games and prevent overzoomed textures.

## Changelog v0.17.1

- Fixed indexed draws which caused some games to render at 480x272 only.

## Changelog v0.17

- Fixed artifacts, flickering and black screens in some games.
- Fixed some regressions introduced in earlier versions.

## Changelog v0.16

- Added behavior of signal commands.
- Optimized draws to ignored framebuffers.

## Changelog v0.15

- Switched to using dfs algorithm to traverse the display list.
- Fixed a few commands and changed their stopping criteras.
- Added indexed draws support.

## Changelog v0.14

- Fixed another issue that causes games to show black screen only.

## Changelog v0.13

- Fixed issue where some games would render a black screen only.
- Fixed issue where some games would crash because vertices were updated multiple times.

## Changelog v0.12

- Fixed issue where some games would be inverted or upsidedown.

## Changelog v0.11

- Fixed issue where black rectangles would cover the screen in lots of games.

## Desarrollo

- **Compilar:** necesita el [SDK de PSP](https://github.com/pspdev/pspdev) y el código de [Adrenaline](https://github.com/TheOfficialFloW/Adrenaline) clonado al lado de este repo (`../Adrenaline`). Luego `make`, o `make DEST=/Volumes/<vita>/pspemu/seplugins` para copiar el plugin al terminar.
- **Tests:** `make -C tests` corre en el PC sin el SDK (necesita gcc con 32 bits: `apt install gcc-multilib`). Arman display lists en una memoria simulada de la PSP y revisan cómo las parchea el plugin. Varios corresponden a bugs ya arreglados: si uno falla, ese bug volvió. GitHub los corre en cada push.
- **Botones al iniciar el juego:** L = modo pasivo (el juego corre sin cambios, pero se registra el mismo log para comparar), R = log sin buffer (para cuelgues), TRIANGULO = NOP 0 antiguo, CIRCULO = recorrido de listas antiguo.
- **Durante el juego:** L+R+START abre el menu de diagnostico (render targets y su escala, texturas desde framebuffer: leer memoria / omitir / copia reducida, imagen original para videos, NOP y recorrido antiguos, duplicar vertices 2D, framebuffer de 1024, desplazamiento X/Y, memoria y CPU al log). L+R+SELECT captura un frame en detalle al log. Con "Force high memory layout" activo en Adrenaline el juego puede pisar la memoria del plugin.
- **Log:** `ms0:/ge_patch.log` (en la Vita, `ux0:pspemu/ge_patch.log`); la sesion anterior queda en `ge_patch_prev.log`. Cada cambio del menu queda registrado con su frame.

## Installation

- Before you start make sure that you have

  - Adrenaline 7 or higher.
  - The option `Recovery Menu->Advanced->Advanced configuration->Force high memory layout` **DISABLED**.
  - All plugins in `ux0:pspemu/seplugins/game.txt` and `ux0:pspemu/seplugins/vsh.txt` disabled (you can gradually enable them if you think they should not interfere with GePatch. Please be aware that plugins that print stuff to the screen may not be visible with GePatch since the framebuffer is redirected.

- Download [ge_patch.prx](https://github.com/TheOfficialFloW/GePatch/releases) and copy it to `ux0:pspemu/seplugins/`.

- Write this line to `ux0:pspemu/seplugins/game.txt` (`ux0:pspemu` is mounted as `ms0:` in the PSP emu):

  ```
  ms0:/seplugins/ge_patch.prx 1
  ```

  You can also do the same change in file `ux0:pspemu/seplugins/vsh.txt` to get a XMB in higher resolution, but be aware that the VSH menu will be invisible.

## Known Issues

Some games may:

- Not display cutscenes.
- Have a black screen.
- Not display all textures.
- Contain clipping/culling.

## Donation

If you like my work and want to support future projects, you can make a donation:

- via bitcoin `361jRJtjppd2iyaAhBGjf9GUCWnunxtZ49`
- via [paypal](https://www.paypal.me/flowsupport/20)
- via [patreon](https://www.patreon.com/TheOfficialFloW)
