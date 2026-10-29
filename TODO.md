# TODO: pendientes que no son urgentes

Cosas que no bloquean el trabajo actual pero pueden servir más adelante. Cada una dice de dónde salió (documento en
`docs/` o en el proyecto). Lo que sí falta para cerrar la etapa 2 está en `docs/arquitectura_dibujo_en_vita.md` §6, no
aquí. Actualizado: 2026-10-29 (GePatch 0.2-dev66 + gepresent v13.11, fase 2 en curso). Cómo se trabaja:
`docs/FLUJO_DE_TRABAJO.md`.

## Fase 2: quitar la espera del juego a la Vita (de `etapa2_fase1_impacto.md`, 2026-10-29)
- [x] ~~Mover la compuerta a "antes de encolar la lista siguiente"~~: hecho en dev66 + v13.10 con autocomprobación
      (huellas de la lista). GT: 3 veces menos esperas en la carrera, casi ninguna en la mitad de la vuelta; SH sin
      cambios en la lista. (`etapa2_fase2_compuerta.md`)
- [ ] **Arranque y final de la vuelta de GT** siguen esperando (la Vita necesita más de 16,7 ms por frame). El final quedó
      en 54,7 fps (contra 56,0 con la espera en el sync, una sola corrida): confirmar si es variación. Con v13.11 el
      tramo más pesado (más de 75 mil comandos) subió de 54,3 a 57,3 fps; el final de la vuelta sigue esperando.
- [x] ~~Capturador: leer directo de a 256 B en vez de la caché compartida~~: probado en v13.11.1, **descartado** (casi 3
      veces más esperas en GT). La caché compartida conviene. Idea chica si vuelve a importar: sacar la línea con NEON y
      menos barreras en `clGet`. (`etapa2_fase2_estadisticas.md`)
- [x] ~~Aplicar sublistas sin sumar todas las estadísticas~~: hecho en v13.11 (solo las distintas de 0, hasta 48). GT:
      19,1 → 17,0 ms de Vita por frame en lo más pesado. (`etapa2_fase2_estadisticas.md`)
- [ ] Aplicar una sublista todavía copia sus registros (hasta 64) y matrices: medir cuánto queda con el registro extremo
      (`aplicar_us`) antes de tocarlo.
- [ ] **Cuelgue de la Vita** en la primera corrida de GT con v13.11.1 (~10 s después de largar; la segunda corrida con
      la misma versión terminó bien). Si vuelve a pasar, anotar versión y momento.
- [ ] Revisar sublistas guardadas: ~5,5 ms por frame en carrera. Que los lectores lleguen antes a más sublistas.
- [ ] El capturador pierde ~17 % de su núcleo (el 1, con el audio): probar prioridad / núcleo con cuidado del audio.
- [ ] Corrida `sin_captura` en GT para saber si los frames lentos del final de la vuelta son del juego solo.
- [ ] Probar la compuerta nueva en Ridge Racer y LocoRoco (la autocomprobación solo se vio en GT y SH).

## Rendimiento para dibujar GT (de `etapa2_nucleos.md`, 2026-10-29)
- [ ] **Grabar una vez cada sublista estable y reusar la lista GXM grabada.** Grabar draw por draw no entra para GT ni
      en 3 núcleos (~24 ms de procesador y ~16 ms de GPU por 9000 draws; queda ~1,5 núcleo libre). En GT ~85 % de los
      comandos vienen de sublistas que la captura ya reconoce iguales. A resolver: el estado de entrada que hereda la
      sublista (`otra_entrada`). Probablemente hay que adelantarlo de la etapa 6 a la 3/4 para GT.
- [ ] **Juntar draws** con el mismo estado (etapa 1: 2000 cuadros en 1 draw = 0,02 µs/cuadro, GPU a la mitad).
- [ ] El traductor/grabador tiene que ir en el núcleo 0 (y lo que sobre del 2): el 1 es de la captura (~10–15 % libre).
- [ ] **Final de la vuelta de GT a 40–48 fps** (últimos ~7 s): aparecen ~300 RECTANGLES por frame y ~50 sublistas por
      frame entran con otro estado; la memoria de sublistas se salta 70 % en vez de 85 % y la captura sube a 20–25 ms por
      frame. Ver qué pantalla es (¿llegada?) y si la memoria puede guardar varias entradas por sublista.

## Rendimiento de la captura (gepresent, lado Vita)
- [ ] **Comprobar al empezar cada cuadro las sublistas estables.** Hoy un ayudante las comprueba cuando la lista llega
      al CALL, y solo ~40 % llega a tiempo. Comprobarlas al empezar la lista daría más tiempo, con un riesgo: si el juego
      cambia la sublista en ese cuadro, se usa la vieja (la revisión de 1 de cada 32 lo vería). Solo para sublistas
      estables hace muchos cuadros. Decidido dejarlo para después. (`etapa2_comparar_menos.md`)
- [x] ~~Leer menos de la RAM del PSP~~: probado en v13.8.1/v13.8.2 (no releer líneas ya comprobadas, leer de a 64–256 B):
      ~45 % menos lecturas sin bajar el tiempo; descartado. Medido después: 256 B cuestan 3,0 µs (no 6,5). Ideas
      pendientes si vuelve a importar: vista con caché o DMA (KBENCH: 1,7–4 veces más rápido en bloques grandes).
      (`etapa2_fase1_impacto.md`)
- [ ] **Acortar el bucle de interpretación** (~360 ns por comando): revisar el tramo de RAM una vez por línea y no por
      comando, y usar menos contadores. Bajaría lo que la memoria de sublistas no se salta. (`etapa2_memoria_sublistas.md`)
- [ ] Comparar directo desde la caché compartida, sin copiar la línea a `R0.buf` (ganancia chica).
      (`etapa2_espejo_vram.md`)
- [ ] `memoVerify` compara todas las versiones (hasta 8) de una sublista aunque tengan las mismas líneas: comparar
      una vez por tramo. Hoy en GT casi siempre hay una sola versión, así que no se nota. (`etapa2_comparar_menos.md`)

## Espejo de la VRAM
- [ ] **DISTINTAS con el espejo doble:** la revisión de la memoria de sublistas encontró 1–7 por corrida de GT (≈0,01 %
      de ~50 000 revisadas) desde v13.6; con v13.5 siempre 0. 2026-10-29: 2 de 175 172 (dev64), 13 de 134 312 (v13.7.1
      con el banco), 7 de 49 211 (dev65), 1 de 50 536 (dev66.3), 3 de 51 121 (v13.11.2). **Todas son sublistas en la VRAM (0x041xxxxx).**
      Hipótesis: la memoria guarda las líneas con la dirección del espejo en que se grabó (MIRROR o MIRROR2) y en los
      frames que usan el otro espejo compara contra una copia vieja (que el GE puede estar sobrescribiendo). Arreglo
      posible: guardar la dirección de la VRAM y traducirla con `vtr` al comparar. Arreglar antes de dibujar (etapa 3).
      (`etapa2_espejo_doble.md`)
- [ ] Registrar MIRROR2 (0x0AC00000–0x0AE00000) para la GPU de la Vita antes de dibujar desde el espejo (`mapZonas` en
      gepresent.c registra 0x0A000000–0x0A400000, donde está MIRROR). (`etapa2_espejo_doble.md`)
- [ ] La Vita tendrá que repetir las copias de bloque (TRANSFER) a la VRAM y guardar el resultado: desde v13.6 esos
      destinos ya no se piden al espejo (en GT, casi todas las texturas que cambian; en SH, la copia de su imagen).
      (`etapa2_espejo_doble.md`)
- [ ] Buscar la dirección física de la VRAM desde el kernel de la Vita, para leerla sin espejo. Riesgoso y de baja
      prioridad. (`arquitectura_dibujo_en_vita.md` §7)

## Telemetría (T4/T6, dev63)
- [ ] **T4: no se encontraron las funciones de DMA** (`sceDmacMemcpy`/`sceDmacTryMemcpy`, biblioteca `sceDmac`) con
      ninguno de los nombres de módulo probados: falta el nombre correcto en Adrenaline 8. GT y SH igual se pudieron
      medir sin eso. (`etapa2_t4_t6_vida_datos.md`). Desde dev65 los eventos T4 solo van con registro extremo.
- [ ] T6: "GE ocupado al llamar" al sync sale siempre 0, aunque el juego espera ~0,7 ms (GT) o ~3,7 ms (SH) dentro de
      `sceGeDrawSync`. Entender qué espera ahí (¿el fin de la lista con el GE ya detenido, o los callbacks?).
- [ ] T6 apagado por defecto (le quitaba ~2–3 fps a GT): encender con `ux0:data/gepresent/medir_ge`. Si se quiere
      dejar siempre, muestrear cada ~1 ms o desde otro lado.

## Pruebas y herramientas
- [ ] Las pruebas del PSP (`tests/`) no compilan desde dev61: `asm("sync")` en `sbPost` con el compilador del PC.
      (`etapa2_espejo_vram.md`)
- [ ] En el contenedor, `gcc-multilib` (pruebas del PSP) desinstala `gcc-arm-linux-gnueabihf` (prueba de qemu de
      gepresent): no instalarlos a la vez, o buscar cómo tener los dos. (`resumen_conversacion_2026-10-28_etapa2c.md`)
- [ ] Probar Ridge Racer con el modo sombra y el espejo. Usa texturas en la VRAM, y en la etapa 2 solo se probaron GT y SH.
- [ ] La VM de Claude en el PC con Windows no llega a GitHub (proxy 403): pull y push van por paquetes de git
      (`FLUJO_DE_TRABAJO.md` §2). Ver si en el Mac `git pull` funciona directo. 2026-10-29: el push desde el contenedor
      (`add_repo` con push) funcionó con permiso del usuario; los commits subidos así quedan a nombre de Claude.
- [ ] `resumen_corrida.py` todavía no muestra las líneas de la compuerta de dev66 (modo y esperas antes de encolar):
      hoy se sacan con `grep "stats compuerta" ge_patch.log`.

## Mediciones poco confiables
- [ ] El núcleo 0 de la línea `procesador` del log sale casi vacío: el gancho de CreateThread no ve los hilos que
      Adrenaline creó antes de cargar gepresent. (`etapa2_memoria_sublistas.md`)
- [ ] Estadísticas aproximadas de la memoria de sublistas: "cambios" y lotes del primer draw de una sublista aplicada
      dependen del estado de entrada; `chg[]` no suma las sublistas aplicadas. (`etapa2_memoria_sublistas.md`)
- [ ] vpjuego: "GE en stall" siempre da 0 %. Para los tiempos del GE usar las horas de encolado y FINISH (T6).
      (`arquitectura_dibujo_en_vita.md` §7)
- [ ] Leer la hora en la Vita cuesta 390 ns: el registro extremo completo altera lo que mide (corrida 5 de la fase 1).

## Pendientes heredados
- [ ] Pantalla negra durante la presentación del banco de la etapa 1: la textura del juego llegó negra en esos 10 s.
      Sin el banco, la cinemática de GT se ve bien. Postergado por el usuario. (`etapa1_banco_gxm.md` §3.3)
- [ ] **Licencia de GePatch/NativePatch** antes de copiar código de PPSSPP (GPL 2.0+; Adrenaline es GPLv3).
      `LICENSE.TXT` solo trae el aviso del PSPSDK. (`arquitectura_dibujo_en_vita.md` §3.10)
- [ ] La causa de que la Vita se apagara con el banco v11.3 y GePatch activo a 2x sigue sin confirmar. Mientras tanto,
      lo nuevo de la GPU se prueba con el modo sombra o el pasivo. (`etapa1_banco_gxm.md`)
- [ ] A mediano plazo: proponer a Adrenaline un punto de enganche oficial para un "presentador externo", para no
      depender de enganchar su hilo de dibujo. (`arquitectura_dibujo_en_vita.md` §8)

## Limpieza
- [ ] La Vita queda con `ux0:data/gepresent/banco_nucleos`, `sin_compuerta` y `compuerta_sync` = "no" (apagados); se
      pueden borrar con VitaShell.
- [ ] Paquetes de git sueltos en `Descargas\vitabridge` (`*.bundle`) y los parches de prueba (`*_v13.8.*.patch`,
      `fase1_*.patch`, `fase2_*.patch`): se pueden borrar.
- [ ] Borrar en la Vita `ux0:data/gepresent/banco` (con VitaShell: el relevo solo sube archivos) y los ~100 volcados
      `.gpd` viejos de `ux0:/pspemu` (el relevo los vuelve a bajar en su primera corrida).
- [ ] Borrar a mano las carpetas vacías `C:\Users\spect\GePatch` y `Documents\ambientepsp` del PC con Windows.
      (`ambiente_windows.md`)

## Documentación
- [ ] `ambientes_de_prueba.md` sigue describiendo dev56 y gepresent v6–v9 en varias tablas.
- [ ] `errores_conocidos.md` describe GePatch actual (arquitectura A, dev56.8). Cuando la Vita dibuje (etapa 3 en
      adelante), marcar cuáles desaparecen con B (ver la tabla de `arquitectura_dibujo_en_vita.md` §3.7).
- [ ] `CONTEXTO_IA.md` está desactualizado desde dev49 (lo vigente para trabajar está en `FLUJO_DE_TRABAJO.md`).
- [x] Subir a GitHub el commit de dev64 + v13.6 (hecho desde el Mac).
- [x] Subir a GitHub el commit de dev65 + v13.9 (`6193d07`, desde el contenedor, a nombre de Claude).
- [x] Subir a GitHub el commit de dev66 + v13.10 (desde el contenedor).
- [x] Subir a GitHub el commit de gepresent v13.11 (desde el contenedor).
