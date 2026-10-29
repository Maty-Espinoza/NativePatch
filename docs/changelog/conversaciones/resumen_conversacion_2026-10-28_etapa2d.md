# Resumen para seguir en otra conversación: etapa 2, comprobación por los lectores (2026-10-28, cuarta conversación)

Leer en este orden: este resumen → `etapa2_comparar_menos.md` (diseño, números, ideas) → `etapa2_espejo_vram.md` →
`etapa2_memoria_sublistas.md` → `arquitectura_dibujo_en_vita.md` (§6 etapas). La anterior:
`resumen_conversacion_2026-10-28_etapa2c.md`.

## Dónde quedamos
- **Commit `1dac8e9` en `dev`** (repo de Windows `Documents\GitHub\NativePatch`, **sin push**): **gepresent v13.4**
  (`tools/gepresent/captura.c`, `gepresent.c`, `prueba_memoria.c`) + `docs/etapa2_comparar_menos.md`. GePatch sin
  cambios (0.2-dev62).
- En la Vita: GePatch dev62 (`d44ba997…`), gepresent v13.4 (`dc016491…`, verificado bajándolo). Modo sombra por defecto.
- Binarios: `vitabridge/gepresent/gepresent_v13.4{.1,.2,}.suprx`; parches `vitabridge/gepresent_v13.4{.1,.2,}.patch`.
  Logs: `vitabridge/etapa2_v13.4.1/gt1/`, `etapa2_v13.4.1/sh1/`, `etapa2_v13.4.2/gt1/`.
- **Resultado:** GT en el puente 57–60 fps (v13.3 ~52), comienzo de la carrera 59,4, captura ~15,5 ms por frame en el
  puente (antes 18,3); 0 DISTINTAS en ~53 000 revisadas. SH igual que antes (26–60 según la escena).

## En simple (qué hace cada pieza)
- **gepresent (Vita), capturador:** reusa las sublistas guardadas sin recorrerlas; si un ayudante ya las comprobó en esta
  lista, ni siquiera las lee.
- **Ayudantes (lectores, núcleos 0 y 2):** al llegar a una sublista guardada comparan sus líneas con lo guardado y dejan
  una marca; se reparten las sublistas según su ritmo y se saltan las que el capturador ya pasó.

## Lecciones (no repetir)
- Lo caro de "comparar" era leer la RAM del PSP (~6,5 µs cada 256 bytes sin caché), no comparar.
- Los lectores tienen poco tiempo (~140 ms/s en el núcleo 0, ~280 en el 2): un reparto fijo deja atrasado al del núcleo 0.
  Aun con reparto por ritmo, solo ~40 % de las sublistas llega comprobada a tiempo.
- La cadena de compilación del contenedor (`etapa1_banco_gxm.md` §5, ~5 min) reproduce los binarios anteriores salvo la
  fecha de compilación (9 bytes). Para comprobar que dos binarios tienen el mismo código: compilar los `.o` y comparar
  `.text` con `objcopy -O binary -j .text`.
- Para el commit desde la VM del PC hay que pedir el permiso de borrar en `Documents\GitHub` en cada sesión.

## Próximos pasos propuestos
1. (Decidido dejarlo para después) comprobar al empezar cada cuadro las sublistas estables: más rápido, con el riesgo de
   usar una versión vieja si el juego la cambia en ese cuadro.
2. SH: marcar destinos de TRANSFER y dejar de pedir páginas del espejo que cambian siempre.
3. T2 (caja negra), T4, T6, grabación GXM en varios hilos → etapa 3 (dibujar).

## Reglas de trabajo vigentes (del usuario)
- Versiones de prueba `.x` (siguiente: gepresent v13.5.x / GePatch dev63.x); commit solo con la versión confirmada.
- Compilar en el contenedor y copiar a `vitabridge\enviar` (y Descargas / memstick del laboratorio para ge_patch.prx);
  verificar con md5 bajándolo de la Vita; cerrar Adrenaline con `cmd_cerrar.txt` (`destroy`) para cargar un gepresent
  nuevo. No subir nada a la Vita mientras el usuario tiene una prueba pendiente con la versión anterior.
- Commit y revisión desde el PC con Windows (git en `Documents\GitHub`, cambios como parche con `git apply`).
- Al pedir pruebas: qué juego, qué hacer y en qué fijarse; opciones con el nombre del menú. Resumen simple después de
  cada cambio. Logs de cada corrida en `vitabridge/<etapa>_<versión>/<juego><n>/`.
