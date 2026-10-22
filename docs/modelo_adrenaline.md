# Modelo PPSSPP-Adrenaline: GePatch corriendo dentro de PPSSPP

Complementa `modelo_vita.md` (2026-10-22): corrige su límite "el plugin no corre dentro de PPSSPP". Ahora corre desde su `.prx`, sin cambios.

**Modelo PPSSPP-Adrenaline** (`tools/ppsspp-vita/adrenaline.patch`, rama `vita-model` sobre hrydgard/ppsspp 1adf3c5; se activa con `PPSSPP_ADRENALINE=1`):
- Carga los plugins de `ms0:/seplugins/game.txt`.
- 64 MB de RAM con partición de usuario de 32 MB.
- `sctrlHENFindFunction` encuentra también las funciones del sistema (stub HLE) y `sctrlHENPatchSyscall` redirige las importaciones de los módulos de usuario (las del plugin siguen a la original, como en el CFW).
- `sceGeEdramSetSize(4 MB)` = 0x80000104, `sceGeGetList` (NID 6.60), `sceKernelInitFileName`.
- Las capturas de headless dejan además `X_nativo.bmp` (lo que muestra la Vita: 960x544 de 0x0A400000 si DRAW_NATIVE ≠ 0).
- Aviso (o parada con `PPSSPP_ADRENALINE=estricto`) ante una lista con contexto encolada desde un plugin.
- Primera corrida: GePatch dev40 arranca, encuentra y parcha todas sus funciones, escribe `ms0:/ge_patch.log` y dibuja la imagen nativa.

**Verificación contra la Vita: escena GEPVM** (`tools/geptest/escena`):
- Homebrew determinista: render a textura, mezcla alfa, paleta, copia de bloque, cubo 3D con profundidad y un recuadro dibujado por la CPU.
- En los frames 5, 30, 60, 90 y 120, `kcap.prx` (kernel) guarda en `ms0:/gepvm/` la imagen nativa, la del PSP y DRAW_NATIVE.
- Se corre igual en la Vita y en el modelo (mismo `ge_patch.prx`) y `comparar_vm.py` mide la diferencia por frame, con informe HTML.
- GePatch dev40 en la escena: el cubo con render a textura no aparece (render targets desactivados → draws omitidos), falta la copia del render target y el recuadro de la CPU (fb 8888) sale como líneas punteadas. Son bugs reales: la Vita muestra exactamente lo mismo.

## Resultado de la primera comparación (2026-10-22)
- **Las 5 imágenes nativas de la Vita y del modelo son idénticas byte a byte** (frames 5, 30, 60, 90 y 120), y el `ge_patch.log` también, salvo los tiempos medidos.
- Para llegar ahí se ajustaron dos cosas del modelo después de la primera corrida en la consola:
  - Adrenaline arranca a 333 MHz (bus 166); PPSSPP a 222.
  - Reflejos de la VRAM: en la PSP y la Vita la CPU ve 0x04200000 y 0x04600000 permutados en todos los accesos; PPSSPP solo en los de 16 bits. Ahora se aplica a todos (`edramswizzle` pasa de 229 líneas distintas a 0) y el `probeVram` de GePatch da los mismos mensajes que en la Vita. Para eso el modelo usa el intérprete IR, porque el JIT lee la memoria directo.

**Consecuencia:** los bugs de GePatch que se vean en el modelo con la escena se pueden depurar en el Mac, sin la consola.

**Siguiente:**
- Compilar el mismo parche en la versión con interfaz para el Mac y correr juegos mirando la imagen nativa en tiempo real.
- Agregar casos a la escena para cada bug conocido (pasadas de 1024 de ancho de SH, mapa de entorno de Ridge Racer) y usarla como prueba de regresión.

## dev41: arreglos de GePatch hechos con el modelo (2026-10-22, sin probar aún en la Vita)
- **Render targets a escala 1 en la VRAM del z-buffer** (opción "Render targets a escala 1", activa por defecto): un fb de ancho menor a 480 que no es la pantalla se dibuja a escala 1, en el formato del juego, en 0x04100000, solo si desde el último flip nada usó la profundidad del buffer grande. Al salir de él, las palabras FRAMEBUFPTR/WIDTH del fb siguiente pasan a un CALL que lo copia a su lugar en la VRAM falsa; las texturas y copias que lo leen van a la VRAM falsa. En GEPVM vuelve a aparecer el cubo. Candidato para el mapa de entorno de Ridge Racer (fb de 256).
- **Copias chicas a la pantalla** (hasta 8 por frame): se dibujan al doble en el buffer grande en su lugar de la lista, leyendo de la RAM. La copia original del plugin (`txEmitOrig`) ahora usa la VRAM falsa: antes una copia con direcciones de la VRAM real escribía sobre el buffer grande.
- **Dibujos de la CPU** también en fb mostrados de 480/512 (antes solo los de 1024 de SH), en 565 y 8888, con el recuadro exacto (antes iba con margen negro) y saltando las zonas donde cayeron copias del juego.
- GEPVM v2 dibuja el recuadro de la CPU con la dirección de `sceGeEdramGetAddr`, como los juegos. La v1 escribía en 0x44000000 fija, es decir en la VRAM real sobre el buffer grande, cosa que ningún plugin puede evitar.
- Resultado en el modelo: la imagen nativa difiere de la escena sin GePatch en ~2,5 % de los píxeles (bordes por la resolución distinta y bandas por el 565 del buffer grande).
- **Bug del modelo encontrado de paso:** el `memset`/`memmove` de sysclib en PPSSPP devolvía 0 en vez del destino; GCC usa ese valor y el borrado del dibujo de la CPU fallaba. Corregido en `adrenaline.patch`.
