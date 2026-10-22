# Modelo de Adrenaline en PPSSPP

`adrenaline.patch` (sobre hrydgard/ppsspp 1adf3c5) agrega a PPSSPP un modo Adrenaline que se
activa con `PPSSPP_ADRENALINE=1`. Sirve para correr GePatch **desde su .prx**, sin cambios,
como en la Vita:

- Carga los plugins de `ms0:/seplugins/game.txt` ("ruta 1" por línea; `ux0:/pspemu/` = `ms0:/`).
- 64 MB de RAM con la partición de usuario de 32 MB: 0x0A000000+ queda libre para el plugin.
- `sctrlHENFindFunction` también encuentra funciones del sistema (en PPSSPP son HLE): devuelve
  un stub `jr ra; syscall`. `sctrlHENPatchSyscall` hace que las llamadas de los módulos de
  usuario (los ya cargados y los que se carguen después) salten a la función del plugin; las
  del propio plugin siguen yendo a la original, como con la tabla de syscalls del CFW.
- `sceGeEdramSetSize(4 MB)` falla con 0x80000104 (VRAM de 2 MB), `sceGeGetList` (NID de 6.60),
  `sceKernelInitFileName`.
- Imagen nativa: si el plugin deja DRAW_NATIVE (0xABCDEF00) distinto de 0, cada captura de
  PPSSPPHeadless (`--screenshot-save=X.bmp`) deja además `X_nativo.bmp` (960x544 desde 0x0A400000).
- Avisos de lo que en la Vita congela o cierra Adrenaline (por ahora: lista del GE con contexto
  encolada desde un plugin). Con `PPSSPP_ADRENALINE=estricto` la emulación se detiene ahí.

Uso: `modelo.sh PPSSPPHeadless MEMSTICK JUEGO [opciones]`.

## Cómo se comprueba que el modelo da lo mismo que la Vita

`tools/geptest/escena` (GEPVM): homebrew determinista (cada frame depende solo de su número)
con lo que a GePatch le cuesta: render a textura, mezcla alfa, paleta, copia de bloque, 3D con
profundidad y un recuadro que dibuja la CPU. En los frames 5, 30, 60, 90 y 120, `kcap.prx`
(kernel) guarda en `ms0:/gepvm/` la imagen nativa, la del PSP y DRAW_NATIVE. La misma escena,
con el mismo `ge_patch.prx` y el mismo `game.txt`, se corre en la Vita y en el modelo, y
`comparar_vm.py VITA MODELO [REFERENCIA] [informe.html]` mide la diferencia por frame.

También imita: 333 MHz al arrancar (como Adrenaline) y los reflejos 0x04200000 y 0x04600000
de la VRAM permutados para todos los accesos de la CPU, como en la PSP y la Vita (PPSSPP solo lo
hacía en los de 16 bits; `pspautotests/gpu/ge/edramswizzle` pasa de 229 líneas distintas a 0).
Para eso usa el intérprete IR en vez del JIT, que lee la memoria directo.

Corrige además `memset`/`memmove` de sysclib, que en PPSSPP devolvían 0 en vez del destino
(GCC usa ese valor: el código de GePatch compilado así escribía en otro lado).

## Resultado (2026-10-22)

GEPVM con GePatch dev40: las 5 imágenes nativas de la Vita y del modelo son idénticas byte a
byte, y el `ge_patch.log` también (salvo los tiempos medidos).
