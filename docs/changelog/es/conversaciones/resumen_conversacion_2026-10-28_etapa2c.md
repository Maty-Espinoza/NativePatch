# Resumen para seguir en otra conversación: etapa 2, espejo de la VRAM (2026-10-28, tercera conversación)

Leer en este orden: este resumen → `etapa2_espejo_vram.md` (diseño, números, pendientes) → `etapa2_memoria_sublistas.md`
→ `etapa2_avance.md` → `arquitectura_dibujo_en_vita.md` (§3.5 VRAM, §6 etapas). La anterior:
`resumen_conversacion_2026-10-28_etapa2b.md`.

## Dónde quedamos
- **Commit `054f7e1` en `dev`** (repo de Windows `Documents\GitHub\NativePatch`, **sin push**): GePatch **0.2-dev62**
  (`main.c`, "Espejo de la VRAM") + gepresent **v13.3** (`tools/gepresent/captura.c`, `gepresent.c`, `prueba_memoria.c`)
  + `docs/etapa2_espejo_vram.md`.
- En la Vita: GePatch dev62 (md5 `d44ba997…`, verificado bajándolo), gepresent v13.3 (`282b3a74…`, verificado). Modo
  sombra por defecto. Sin archivos de opciones de gepresent.
- Binarios: `vitabridge/respaldo/ge_patch_dev62.1.prx`, `ge_patch_dev62.prx`; `vitabridge/gepresent/gepresent_v13.{1,2,3}.suprx`.
  Parche: `vitabridge/gepatch_dev62_gepresent_v13.3.patch`. Logs: `vitabridge/etapa2_v13.{1,2,3}/gt1/`, `etapa2_v13.3/sh1/`.
- **Resultado:** la Vita lee GT completo (100–140 sublistas por frame desde el espejo, ninguna saltada, 0 DISTINTAS).
  GT 59,4–59,9 fps casi toda la vuelta, inicio de carrera 56–58, puente ~52. SH igual que antes. Audio e imagen bien.

## En simple (qué hace cada pieza)
- **GePatch (PSP):** la Vita le pide trozos de 4 KB de la memoria de video; en cada cambio de cuadro el pintor del PSP
  los copia a memoria normal (el "espejo"): instrucciones en cada cuadro, texturas la primera vez y después de a poco.
- **gepresent (Vita):** lee las instrucciones que están en la memoria de video desde el espejo; un hilo de poca
  prioridad mide cuánto cambia el espejo. La memoria de sublistas creció 4 veces (GT ahora llama ~250 por cuadro).

## Lecciones (no repetir)
- La copia del espejo es barata (el juego espera al GE 0,1–0,3 ms por sync): lo que bajó los fps en v13.1 fue la
  memoria de sublistas llena (se vaciaba 4–5 veces por segundo).
- No dejar que algo necesario dependa de un hilo de prioridad 0xB0: a veces no recibe tiempo (v13.2 seguía saltando
  sublistas porque solo ese hilo marcaba páginas válidas).
- Comparar las líneas de las sublistas guardadas cuesta ~2 µs por línea: ~9 de los ~18 ms del puente.
- La lista del espejo no puede ir en RENDER_LIST + 0x8000 (TX_RING): va en 0x0A100000.
- GT cambia texturas en la VRAM durante la carrera (100–230 páginas por seg); SH pide 1 MB de páginas que cambian en
  cada copia (destinos escritos con TRANSFER).
- Las pruebas del PSP (`tests/`) no compilan desde dev61 (`asm("sync")`). En el contenedor, `gcc-multilib` desinstala
  `gcc-arm-linux-gnueabihf` (la prueba de qemu); no instalar los dos a la vez.
- Si Adrenaline no se cierra entre juegos, los logs juntan las sesiones (pasó con SH).

## Próximos pasos propuestos
1. Comparar menos en la memoria de sublistas (lectores que verifican por adelantado, comparar desde la caché
   compartida): es lo que falta para el puente de GT a 60.
2. SH: marcar destinos de TRANSFER y dejar de pedir páginas que cambian siempre.
3. Después: T2 (caja negra), T4, T6, grabación GXM en varios hilos → etapa 3 (dibujar).

## Reglas de trabajo vigentes (del usuario)
- Versiones de prueba `.x` (siguiente: gepresent v13.4 / GePatch dev63.x); commit solo con la versión confirmada.
- Compilar en el contenedor (pspdev + vita-toolchain, receta `etapa1_banco_gxm.md` §5) y copiar a Descargas,
  `vitabridge\enviar` y `gepatch-lab\memstick\seplugins`; verificar con md5 bajándolo de la Vita; cerrar Adrenaline con
  `cmd_cerrar.txt` (`destroy`) para cargar un gepresent nuevo.
- El usuario pidió hacer el commit y la revisión desde su PC con Windows: git en `Documents\GitHub` (permiso de borrar
  aprobado en esa sesión), cambios pasados como parche (`git apply`).
- Al pedir pruebas: qué juego, qué hacer y en qué fijarse; opciones con el nombre del menú. Resumen simple después de
  cada cambio. Logs de cada corrida en `vitabridge/<etapa>_<versión>/<juego><n>/`.
