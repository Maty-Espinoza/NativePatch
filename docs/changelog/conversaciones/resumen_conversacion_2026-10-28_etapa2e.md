# Resumen para seguir en otra conversación: etapa 2, T4/T6 y vida de los datos (2026-10-28, quinta conversación)

Esta conversación hizo dos pasos: "comparar menos" (gepresent v13.4, ver `resumen_conversacion_2026-10-28_etapa2d.md`
y `etapa2_comparar_menos.md`) y T4/T6 + vida de los datos (este resumen). Leer: este resumen →
`etapa2_t4_t6_vida_datos.md` → `TODO.md` → `arquitectura_dibujo_en_vita.md` (§3.6 coherencia de datos, §6 etapas).

## Dónde quedamos
- **Commit `a619d80` en `dev`** (repo de Windows `Documents\GitHub\NativePatch`, **sin push**): GePatch **0.2-dev63** +
  gepresent **v13.5** + `TODO.md` (nuevo, en la raíz del repo) + `docs/etapa2_t4_t6_vida_datos.md`. Antes: `1dac8e9`
  (gepresent v13.4).
- En la Vita: GePatch dev63 (`23589485…`), gepresent v13.5 (`5967a226…`), los dos verificados bajándolos. Modo sombra
  por defecto. T6 (hilo que mira el GE) apagado: se enciende creando `ux0:data/gepresent/medir_ge`.
- Binarios: `vitabridge/respaldo/ge_patch_dev63{.1,.2,.3,}.prx`, `vitabridge/gepresent/gepresent_v13.5{.1,.2,.3,}.suprx`;
  parches `vitabridge/gepatch_dev63*_gepresent_v13.5*.patch`. Logs: `vitabridge/etapa2_dev63.1/gt1`, `etapa2_dev63.2/gt1`,
  `etapa2_dev63.2/sh1`, `etapa2_dev63.3/gt1`.

## Resultado (la pregunta de los datos)
- GT y SH: vértices, índices y paletas **no cambian durante el frame siguiente**; se reescriben 2 frames después (1–2 % en
  juego, 13 % en menús de GT; 9,5 % de los índices de SH en el exterior). Texturas casi nunca, salvo ~1,5 % de SH en el
  exterior que cambian dentro del mismo frame (¿linterna? hint del usuario).
- GT vacía toda la caché ~3 veces por frame: los avisos de la CPU no dicen qué zona cambió.
- T6: GT ~11,4 ms de GE por frame (margen ~5 ms); SH con el GE siempre ocupado (~20–35 ms por frame).
- **Regla para la etapa 3:** dibujar el frame N durante el frame N+1 (terminar antes del flip N+2); si se atrasa,
  repetir la imagen o hacer esperar al juego; releer las texturas dinámicas de SH. No hace falta copiar vértices.
- fps de GT con dev63.3/v13.5.3 iguales a v13.4 (comienzo 59,8, puente 55–60). 0 DISTINTAS.

## Guía del usuario para leer los logs de SH
SH empieza con un par de cinemáticas, después una escena interior en un cuarto chico, una cinemática dentro del juego y
la escena exterior. Los fps bajan en las cinemáticas; **los últimos ~30 segundos son los más representativos** del juego.

## Lecciones (no repetir)
- Un hilo de alta prioridad en el PSP que despierta cada 250 µs le quita 2–3 fps a GT: la telemetría que corre siempre
  tiene que ser casi gratis; lo demás, a pedido.
- No poner trabajo nuevo en los núcleos/prioridad de los lectores (0xB0, núcleos 0 y 2): les quita tiempo y baja los fps.
- `UtilsForUser` está en `sceSystemMemoryManager`; `sceDmac` no se encontró (módulo desconocido en Adrenaline 8).
- `sceGeGetReg` desde un gancho: con k1 = 0 (aun así "GE ocupado al llamar" da 0: pendiente).
- El `TODO.md` del repo es el de referencia (en el proyecto hay una copia, `claude/TODO.md`).

## Qué queda en la etapa 2 (orden acordado)
1. ~~Punto 4 + T6~~ (hecho) y ~~decidir cómo asegurar los datos~~ (respondido arriba).
2. **Punto 6 + espejo estable:** dos copias alternadas del espejo (la GPU de la Vita no puede leerlo mientras el GE lo
   sobrescribe) y texturas de GT al día (cambian 100–230 páginas por seg en carrera; el repaso de 8 por frame no alcanza).
3. Grabación GXM en 2–3 núcleos fijos (del lado de la Vita), antes de llegar a GT en la etapa 4 (~16 500 draws por frame).
4. T2 (caja negra) al final. Ridge Racer no se probó en la etapa 2.

## Reglas de trabajo vigentes (del usuario)
- Versiones de prueba `.x` (siguiente: GePatch dev64.x / gepresent v13.6.x); commit solo con la versión confirmada.
- Compilar en el contenedor: vita-toolchain (`etapa1_banco_gxm.md` §5) y pspdev (tarball de Linux + clon de
  TheOfficialFloW/Adrenaline para `ADRENALINE=.../cef`). Copiar `ge_patch.prx` a Descargas, `vitabridge\enviar` y
  `gepatch-lab\memstick\seplugins`; gepresent a `vitabridge\enviar\ux0\data\gepresent`. Verificar con md5 bajándolos;
  cerrar Adrenaline con `cmd_cerrar.txt` (`destroy`). No subir nada mientras haya una prueba pendiente.
- Commit desde el PC con Windows (git en `Documents\GitHub`, permiso de borrar en esa carpeta en cada sesión, cambios como
  parche con `git apply`).
- Al pedir pruebas: qué juego, qué hacer y en qué fijarse; opciones con el nombre del menú. Resumen simple después de
  cada cambio. Logs de cada corrida en `vitabridge/<etapa>_<versión>/<juego><n>/`.
