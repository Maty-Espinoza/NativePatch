# Resumen de la conversación del 2026-10-27 (e): RR con "por copia" → la causa era el clear (dev58.8)

Continúa `resumen_conversacion_2026-10-27d.md`. **Leer en este orden: `contexto_ia.md`, `resumen_conversacion_2026-10-27c.md`
(metodología de prueba), este archivo.** Este archivo **reemplaza el punto 0 de "Abierto / próximos pasos" de
`contexto_ia.md`** (y el "Próximo paso" del resumen d) y agrega dev58.8 al historial.

## Estado al cerrar
- Código: **GePatch 0.2-dev58.8, sin commit**, sobre dev57 `2c4e937` (rama `dev`). Respaldo completo (diff contra dev57):
  `~/Downloads/vitabridge/gepatch_dev58.8.patch` (md5 `03d5b64bb23819317cf8c6261ee5705b`). **No incluye
  `tests/replay.c`**: el parche de dev58.7 tampoco lo tenía (el contexto decía lo contrario): esa herramienta está perdida.
- `.prx` md5 `6cc477b85dd244f753e30682c649dcd1`, instalado en el laboratorio (`gepatch-lab/memstick/seplugins/` y
  `seplugins/versiones/ge_patch_0.2-dev58.8.prx`) y en la Vita (por `vitabridge/enviar/ux0/pspemu/seplugins/`).
- Tests en PC: **193 comprobaciones, 0 fallas** (nuevo `test_v2d_sin_clear`).
- **Confirmado por el usuario con "Vertices 2D (HUD, sprites)" = "por copia, sin el borrado":**
  - RR en el laboratorio del Mac: todo bien salvo las texturas cyan (error conocido de guard band, **no tocar todavía**).
  - **RR y GT en la Vita: bien.** SH y LocoRoco: **sin probar**.

## La causa
**El clear del inicio del frame (CLEARMODE 0x701: color, stencil y z, un sprite 2D de pantalla completa) no borra cuando
pasa por la lista del plugin (BASE_PLUG + CALL).** Con el clear desviado, el z-buffer (0x04100000) y la zona de rt1
(0x04120000) no cambian en todo el frame (z viejo de 1861 a 65535) y el mundo 3D falla la prueba de profundidad; el HUD,
sin z, se ve. Sin desviar el clear, el z cambia y el mundo se ve. El porqué exacto de que un clear dentro de un CALL no
escriba no se investigó (en la Vita el síntoma con "por copia" era otro: z limpio y mundo en el rt); no hace falta para
el arreglo.

Bisección (todas RR en el laboratorio, valores del menú "Vertices 2D (HUD, sprites)"; logs y volcados en `vitabridge/`):
| Carpeta | Valor | Resultado |
|---|---|---|
| `etapa6_dev58.7_lab_RR` | por copia | sin mundo 3D |
| `etapa6_dev58.7_lab_RR_v2d4` | prueba: lista minima | sin mundo 3D → falla el desvío, no la copia |
| `etapa6_dev58.7_lab_RR_v2d1` | en memoria del juego | bien (referencia) |
| `etapa6_dev58.8_lab_RR_v2d6` | prueba: lista minima, sin el borrado | mundo bien; HUD sin escalar y un tinte solo arriba a la izquierda (filtro de pantalla completa sin escalar): esperado en ese modo |
| `etapa6_dev58.8_lab_RR_v2d5` | **por copia, sin el borrado** | **todo bien** |
| `etapa6_dev58.8_vita_RR_GT` | **por copia, sin el borrado** | **RR y GT bien en la Vita** (logs, vpjuego, gepresent, un volcado de RR) |

Descartado en el camino: `choques_memo` (RR tiene ~180 000 cada 600 frames también en modo 1; GT 0) y errores en las
listas del plugin (las 139 listas de draws 2D de un frame de RR están bien formadas; `BASE_PLUG` reemplaza siempre un
VADDR; las listas de rt1 son iguales en todos los modos).

## Rendimiento en la Vita (vpjuego, tramos con GE > 3 ms, escenas no idénticas)
| Juego | dev58.7 | dev58.8 "por copia, sin el borrado" |
|---|---|---|
| RR (1,5x) | 40,5 fps, GE 10,7 ms, CPU libre 25 % | 38,9 fps, GE 11,2 ms, CPU libre 26 % |
| GT (2x) | 35,2 fps, GE 24,2 ms, CPU libre 10 % | 37,4 fps, GE 22,4 ms, CPU libre 9 % |
Sin diferencia clara. En RR el recorrido del plugin en carrera es de 9–11 ms por frame (log, `stats plugin`).
El log de la Vita no muestra listas cortadas ni `CALL a direccion invalida`.

## dev58.8 (qué cambió respecto de dev58.7)
- "Vertices 2D (HUD, sprites)" (`v2d`, 0–6) suma **"por copia, sin el borrado"** (`v2d=5`: como 2, pero un draw 2D con
  `CLEARMODE & 1` se escala en el lugar como en el modo 1) y **"prueba: lista minima, sin el borrado"** (`v2d=6`: como
  4 con la misma excepción). En la llamada a `v2dDraw` se agregó `!(opt.v2d >= 5 && (state.ge_cmd[GE_CMD_CLEARMODE] &
  1))`; dentro de `v2dDraw` un `m` local traduce 5→2 y 6→4. `GEPATCH_VERSION` = "0.2-dev58.8".

## Para la versión 59 (próxima conversación)
1. Hacer que **"por copia" excluya siempre los draws en modo clear** (lo que hoy es `v2d=5`) y **quitar los modos de
   bisección** ("copia sin aplicar", "lista minima", y los dos "sin el borrado"): quedan "no", "en memoria del juego" y
   "por copia". Ajustar `test_v2d_sin_clear` al modo 2 y los tests de los modos quitados. Versión `0.2-dev59` (pruebas
   `dev59.x`).
2. Probar **SH y LocoRoco en la Vita** con "por copia" (y repetir RR y GT con la versión limpia). Una prueba por mensaje:
   juego, opción y valor como salen en el menú.
3. Decidir si "por copia" pasa a ser el valor por defecto (hoy el defecto es "en memoria del juego", `v2d=1`). Si se
   cambia, revisar que los perfiles conocidos y los `gepatch_<ID>.txt` del usuario no fuercen otro valor
   (`gepatch_ULJS-00001.txt` del laboratorio tiene `v2d=2`).
4. **Commit** (dev58 + dev59 juntos en un commit, versión confirmada en la Vita) y actualizar `contexto_ia.md`: punto 0
   resuelto, historial (dev58.8 y dev59), fila del menú en §2, "Vértices 2D" en §5, `replay.c` sin respaldo en §2/§6,
   y `errores_conocidos.md`.
5. Siguiente bug: texturas cyan de RR (guard band al agrandar texturas), que el usuario dejó para esta versión.

## Notas de trabajo (esta conversación)
- Empezada desde el Mac enlazado, con `~/Documents/gepatch-lab` y `~/Downloads` conectados.
- `device_bash` corre en una VM Linux arm64 del Mac: python3 con numpy y PIL, git (solo lectura a GitHub), sin apt ni
  `-m32`: sirve para `gpd.py` sobre los volcados (copia en `vitabridge/coleccion/gpd.py`), no para compilar.
- No hay herramienta para pasar archivos entre el contenedor y el Mac: lo que va al contenedor (el parche) el usuario lo
  **adjunta al chat**; lo que va al Mac (el `.prx`) se entrega con SendUserFile, el usuario lo **descarga a Descargas** y
  después se copia con `device_bash` (verificar md5). Transcribir binarios en base64 por texto los corrompe.
- Contenedor: `git clone -b dev` del repo + `git apply` del parche; `pspdev` (tarball de GitHub) + clon de Adrenaline
  en `/home/claude/tc`; para los tests, `apt-get update && apt-get install gcc-multilib`.
- El usuario cambia la opción en el menú y lo cierra sin guardar: vale para la sesión, pero el `gepatch_<ID>.txt` queda
  con el valor anterior.
- Para dibujar el buffer grande de un volcado a 1,5x: 720x408 con **pitch 960** (565). `gpd.py imagenes` supone 960x544.
- El hook del entorno pide commit y push al terminar cada turno: **no se hace** hasta que la versión esté confirmada en
  la Vita (regla del usuario), y la sesión solo tiene lectura del repo.
