# Resumen para seguir en otra conversación: etapa 2, memoria de sublistas (2026-10-28, segunda conversación)

Leer en este orden: este resumen → `etapa2_memoria_sublistas.md` (diseño, números, pendientes) →
`etapa2_diagnostico_v12.11.md` (por qué la memoria) → `etapa2_avance.md` (modo sombra y capturador) →
`arquitectura_dibujo_en_vita.md` (§3 diseño, §5 presupuesto, §6 etapas). La anterior: `resumen_conversacion_2026-10-28_etapa2.md`.

## Dónde quedamos
- **Commit `77908df` en `dev`** (repo de Windows `Documents\GitHub\NativePatch`, **sin push**): gepresent **v12.13**
  (`tools/gepresent/captura.c`, `gepresent.c`, `prueba_memoria.c`) + `docs/etapa2_memoria_sublistas.md`. GePatch sigue
  **0.2-dev61** (commit `81d835b`).
- En la Vita: GePatch dev61 (modo sombra por defecto en todos los juegos), gepresent v12.13 (md5 `d4141c1f…`, verificado
  bajándolo). Sin archivos de opciones de gepresent (`memo_revisar`, `sin_memoria`, `captura_sin_lectores`).
- **Resultado:** GT a 56–60 fps en toda la vuelta, incluido el puente (antes 36–40), audio e imagen bien (usuario). La
  memoria salta ~78 % de los comandos en carrera; 0 errores en ~28 700 sublistas revisadas. La imagen sigue siendo la
  nativa: la Vita todavía no dibuja.

## En simple (qué hace cada pieza)
- **GePatch (PSP), modo sombra:** el juego corre como sin plugin; anota en un buzón lo que le pide a su pintor (GE) y,
  cuando el juego espera al pintor, espera hasta 5 ms a que la Vita termine de leer.
- **gepresent (Vita):** un hilo (núcleo 1) lee y recorre esas instrucciones; dos ayudantes (núcleos 0 y 2) traen la
  memoria por adelantado. **Nuevo:** recuerda las sublistas que ya recorrió y, si no cambiaron, usa lo guardado en vez de
  recorrerlas. 1 de cada 32 se recorre igual para comprobar que lo guardado es exacto.

## Lecciones de esta conversación (no repetir)
- El costo del capturador era **interpretar** (~360 ns de procesador por comando, ~60 instrucciones Thumb), no esperar la
  RAM del PSP (segunda pasada con todo en caché costó casi lo mismo). Compartir el núcleo 1 con el audio solo explica ~20 %.
- GT repite ~88 % de sus comandos en sublistas idénticas (geometría fija; matrices y direcciones van en la lista de arriba).
- Una sublista depende de estado que fija la lista de arriba (tipo de vértice, textura, destino, vértices de entrada): hay
  que comprobarlo además de su memoria; la misma sublista puede llamarse con estados distintos (varias versiones).
- `now()` (sceKernelGetProcessTimeLow) cuesta ~390 ns: no cronometrar por comando.
- Al sumar contadores guardados, no sumar los que cuenta el hilo (frames, tiempos, eventos) y corregir la base si la
  grabación cruza el cambio de segundo (error de v12.12, corregido en v12.13).
- **Probar en el contenedor antes de la Vita:** `prueba_memoria.c` corre `captura.c` en ARM bajo qemu con una RAM del
  PSP falsa (`apt-get install gcc-arm-linux-gnueabihf libc6-dev-armhf-cross qemu-user-static`, compilar `-static`).
  Encontró un cuelgue latente (`texAdd` con `gen = 0`).
- Si Adrenaline no se cierra entre juegos, `ge_patch_captura.log` junta las dos sesiones (buscar "buzon ... encontrado").

## Próximos pasos propuestos
1. **Espejo de la VRAM** (GT, RR): GT tiene 80–140 sublistas por frame en la VRAM (hoy se saltan), ~95 % de texturas y
   ~80 % de paletas ahí. Sin espejo la captura de GT está incompleta (habrá más comandos; la memoria debería absorber
   parte). Es el siguiente paso de §6 de la arquitectura.
2. Probar SH (y RR) con v12.13 para confirmar que nada empeoró.
3. Opcional: bucle de interpretación más corto; que los lectores no recorran lo que la memoria salta.
4. Después: T2 (caja negra), T6, grabación GXM en varios hilos con afinidad fija → etapa 3 (dibujar).

## Reglas de trabajo vigentes (del usuario)
- Versiones de prueba `.x` (siguiente: gepresent v12.14 / v13.x según el cambio; GePatch dev62.x si cambia el PSP);
  commit solo con la versión confirmada y no por cada iteración; compilar y copiar a Descargas/`vitabridge`, verificar
  con md5 bajándolo de la Vita; cerrar Adrenaline con `cmd_cerrar.txt` (`destroy`) para que cargue un gepresent nuevo.
- Al pedir pruebas: qué juego, qué hacer y en qué fijarse; opciones con el nombre del menú.
- Guardar los logs de cada corrida en `vitabridge/<etapa>_<versión>/<juego><n>/` (con `bajar_*.txt` al relevo).
- Resumen simple después de cada cambio, para alguien sin conocimientos técnicos.
- Herramientas: Vita en el contenedor (receta `etapa1_banco_gxm.md` §5, ~5 min); el repo se edita en el PC
  (device_bash) y se pasa con stage/commit; git en el PC necesita permiso de borrar en `Documents\GitHub`.
