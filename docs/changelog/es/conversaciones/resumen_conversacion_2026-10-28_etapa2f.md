# Resumen para seguir en otra conversación: etapa 2, espejo doble de la VRAM (2026-10-28/29, sexta conversación)

Leer: este resumen → `etapa2_espejo_doble.md` → `TODO.md` → `arquitectura_dibujo_en_vita.md` (§3.5, §6).

## Dónde quedamos
- **Commit `5c4d435` en `dev`, con push a GitHub**: GePatch **0.2-dev64** + gepresent **v13.6** + `docs/etapa2_espejo_doble.md`
  + `tools/mac/` (compilar en la VM del Mac) + `TODO.md` y `arquitectura_dibujo_en_vita.md` actualizados. Antes: `a619d80`.
- El commit se hizo **en el Mac** (`~/Documents/GitHub/NativePatch`), no en Windows: el repo de Windows está atrasado
  (hacer `git pull` ahí). El push lo hizo Claude desde su contenedor (con el repo agregado a la sesión); la VM del Mac no
  tiene credenciales de GitHub.
- En la Vita: GePatch dev64 (`32742693…`) y gepresent v13.6 (`5a6e524e…`), verificados en `vitabridge/enviados`. Modo
  sombra por defecto. **Falta una corrida con los números finales** (son el mismo código que dev64.5 + v13.6.5).
- Versiones de prueba: `vitabridge/respaldo/ge_patch_dev64.{1..5}.prx`, `vitabridge/gepresent/gepresent_v13.6.{1..5}.suprx`,
  parches `vitabridge/gepatch_dev64.*_gepresent_v13.6.*.patch`. Logs: `vitabridge/etapa2_dev64.{1,2,3,4}/gt1`,
  `etapa2_dev64.4/sh1`, `etapa2_dev64.5/sh1`.

## Resultado
- Dos espejos alternados (0x0A200000 y 0x0AC00000). Cada lista se lee con la última copia terminada y la Vita avisa cuál
  usa (`SB[0x48]`): el GE nunca la pisa. Todas las sublistas y hasta 64 páginas (256 KB) de texturas por copia.
- Los destinos de copias de bloque (TRANSFER) en la VRAM se tratan como destinos de dibujo: la Vita los repetirá. En GT
  son casi todas las texturas que cambian en carrera; en SH, la copia por frame de su imagen 480x272 (1 MB que se copiaba
  en vano).
- fps: GT puente **58,5** (antes 57,8), resto 59,7–59,9; SH exterior **43,4** (antes 40,8; el usuario cree que no llegó a
  50, como siempre). SH ya no copia nada.
- Pendiente: 1–7 "DISTINTAS" por corrida de GT en la revisión de la memoria de sublistas (≈0,01 %), antes siempre 0.

## Lecciones (no repetir)
- La prueba fuera de la Vita (`prueba_memoria.c`) no simula el aviso de la Vita al PSP: dev64.1 se trabó al arrancar
  (anunciaba la copia -1) y la simulación daba bien.
- **Nada del capturador puede esperar al GE del PSP**: por la compuerta del sync, el juego espera a la Vita (dev64.2 bajó
  el puente de GT a 55,7 fps esperando la copia del flip).
- Copiar ~730–850 KB por frame con el GE le quita 2–4 fps a GT al comienzo de la carrera; ~540 KB, no.
- GT encola parte del frame siguiente antes del flip: "la copia del frame" no es la del último flip.

## Ambiente nuevo: compilar en la VM del Mac (`tools/mac/`)
- `sh tools/mac/preparar_vm.sh` (~2 min, en `$HOME/tc` de la VM, se borra con la sesión): pspdev ARM64 + glibc 2.39 de
  conda-forge con envoltorios (patchelf rompía cc1); vitasdk "core" aarch64-linux-gnu de vitasdk/autobuilds; GCC 15 con
  `-std=gnu17` (por taihen.h). Después `sh tools/mac/compilar_vm.sh [nombre]` compila y copia a Descargas,
  `vitabridge/enviar` y el laboratorio.
- La VM no tiene sudo ni Alpine/Debian/ports de Ubuntu (proxy); sí GitHub, pypi, conda-forge y archive.ubuntu.com.
- Git en el Mac: sin autor configurado (usar `-c user.name=Maty-Espinoza -c user.email=...`) y sin credenciales para push.
  Permiso de borrar en `Documents/GitHub` en cada sesión (git borra sus `.lock`).
- Relevo: conviene la versión del repo (`python3 ~/Documents/GitHub/NativePatch/tools/vitabridge.py`); la de Descargas es
  más vieja.

## Qué queda en la etapa 2 (orden acordado)
1. ~~Punto 6 + espejo estable~~ (hecho).
2. Grabación GXM en 2–3 núcleos fijos (del lado de la Vita), antes de GT en la etapa 4 (~16 500 draws por frame).
3. T2 (caja negra) al final. Ridge Racer no se probó en la etapa 2.
4. Antes de dibujar (etapa 3): las DISTINTAS, registrar MIRROR2 para la GPU, repetir los TRANSFER a la VRAM en la Vita.

## Reglas de trabajo vigentes (del usuario)
- Versiones de prueba `.x` (siguiente: GePatch dev65.x / gepresent v13.7.x); commit solo con la versión confirmada.
- Al pedir pruebas: qué juego, qué hacer y en qué fijarse; opciones con el nombre del menú. Resumen simple después de
  cada cambio. Logs de cada corrida en `vitabridge/<etapa>_<versión>/<juego><n>/`.
