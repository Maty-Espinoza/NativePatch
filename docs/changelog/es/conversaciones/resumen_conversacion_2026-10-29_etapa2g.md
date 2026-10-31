# Resumen para seguir en otra conversación: etapa 2, grabación en varios núcleos (2026-10-29, séptima conversación)

Leer: **`docs/FLUJO_DE_TRABAJO.md`** (nuevo: cómo se trabaja) → este resumen → `etapa2_nucleos.md` → `TODO.md` →
`arquitectura_dibujo_en_vita.md` (§5, §6).

## Dónde quedamos
- **Commit `2b105cb` en `dev`, con push a GitHub:** gepresent **v13.7** (banco de núcleos) + `docs/etapa2_nucleos.md`
  + `docs/FLUJO_DE_TRABAJO.md` + `tools/contenedor/` (compilar en el contenedor de Claude) + `tools/resumen_corrida.py`
  + confirmación de dev64 en `etapa2_espejo_doble.md` + arquitectura §5. GePatch sigue en **0.2-dev64**. Antes: `5c4d435`.
- Se trabajó **desde Windows**. El repo de Windows quedó al día (`dev...origin/dev` sin diferencias). **El repo del Mac
  está atrasado**: hacer pull ahí antes de trabajar (`FLUJO_DE_TRABAJO.md` §2).
- En la Vita: GePatch dev64 (`32742693…`) y gepresent v13.7 (`4a73f210…`), verificados en
  `vitabridge/etapa2_v13.7/verif`. `ux0:data/gepresent/banco_nucleos` = "no" (banco apagado). Modo sombra por defecto.
  v13.7 = v13.7.1 con otro número (no se corrió aparte).
- Logs (Windows, `Descargas\vitabridge`): `etapa2_dev64/gt1`, `etapa2_dev64/sh1` (confirmación de dev64 + v13.6),
  `etapa2_v13.7.1/gt1` (banco de núcleos con GT, incluye `ge_patch_banco.log`).

## Resultado
- **Confirmación dev64 + v13.6:** GT 59,7–60 fps salvo comienzo de carrera (55–58) y tramo pesado (48–56: ~80 000
  comandos, 16 500 draws, captura 17–19 ms por frame). SH: el juego nunca espera a la Vita (fps = los del original).
- **Banco de núcleos (9000 draws T con GT en carrera):** 1 hilo libre ~32 ms; núcleo 0 ~28 ms; núcleo 1 ~170 ms
  (captura); núcleo 2 ~59 ms (sonido); 2 o 3 hilos sin núcleo fijo **~26 ms**. Grabar se reparte sin bloqueo, pero queda
  ~1,5 núcleo libre. GPU ~16 ms por 9000 draws. **GT no entra grabando draw por draw** → reusar listas grabadas de
  sublistas estables (85 % de los comandos de GT) y juntar draws, antes de lo previsto (etapa 6).
- El banco no frenó el juego (60 frames en cada ronda); los tirones cada ~15 s eran la imagen de Adrenaline esperando
  a la GPU durante cada ronda.
- Nuevo: últimos ~7 s de la vuelta de GT a 40–48 fps (RECTANGLES + sublistas con otra entrada; `TODO.md`).
- DISTINTAS: 2 / 175 172 (dev64), 13 / 134 312 (v13.7.1).

## Lecciones (no repetir)
- La VM de Claude en Windows **no llega a GitHub** (proxy 403): pull y push por paquetes de git (`FLUJO_DE_TRABAJO.md`).
  El push se hace desde el contenedor (`add_repo` con `access: "push"`).
- El proxy del contenedor no deja ver páginas de github.com (listados de releases): vitasdk se baja por nombre fijo
  (`vitasdk-bootstrap-x86_64-linux-gnu.tar.bz2` de la última `sdk-snapshot-*`, con `git ls-remote`).
- Un relevo que se corta durante una bajada deja `.parcial` (pasó con la primera verificación): repetir el pedido.
- Los logs se reemplazan al abrir otro juego: guardarlos al terminar cada corrida.

## Qué queda en la etapa 2 (orden acordado)
1. ~~Espejo estable~~, ~~grabación en 2–3 núcleos~~ (hecho: no alcanza para GT draw por draw).
2. T2 (caja negra) al final. Ridge Racer no se probó en la etapa 2.
3. Antes de dibujar (etapa 3): las DISTINTAS, registrar MIRROR2 para la GPU, repetir los TRANSFER a la VRAM en la Vita.
4. **Decidir con el usuario** si se adelanta la "caché de sublistas grabadas" / juntar draws (necesario para GT) o se
   sigue con la etapa 3 en juegos livianos (Retro City Rampage, menús, SH) y se deja GT para después.

## Reglas de trabajo vigentes (del usuario)
- Versiones de prueba `.x` (siguiente: GePatch dev65.x / gepresent v13.8.x); commit solo con la versión confirmada.
- Al pedir pruebas: qué juego, qué hacer y en qué fijarse; opciones con el nombre del menú. Resumen simple después de
  cada cambio. Logs de cada corrida en `vitabridge/<etapa>_<versión>/<juego><n>/`.
