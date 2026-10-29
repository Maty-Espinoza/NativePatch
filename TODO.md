# TODO: pendientes que no son urgentes

Cosas que no bloquean el trabajo actual pero pueden servir más adelante. Cada una dice de dónde salió (documento en
`docs/` o en el proyecto). Actualizado: 2026-10-29 (GePatch 0.2-dev66 + gepresent v13.12: **etapa 2 cerrada**). Lo que
sigue es la preparación para la etapa 3 (abajo) y después la etapa 3 de `docs/arquitectura_dibujo_en_vita.md` §6.
Cómo se trabaja: `docs/FLUJO_DE_TRABAJO.md`.

## Preparación para la etapa 3: nueva identidad del proyecto (pedido del usuario, 2026-10-29)
**Va antes de la etapa 3.** El proyecto ya no tiene relación con el GePatch original, así que se separa de él. Orden
sugerido: 1 y 4 juntos (nombre y versión), después 5 y 6 (limpieza), 2 (README) y al final 3 (instalación).
- [ ] **1. Cambio de nombre**, diciendo expresamente qué hace cada parte:
      - GePatch → **NativeLink**: el plugin que corre en el lado PSP (Adrenaline). Engancha el GE del juego, captura sus
        listas de dibujo y se las pasa a la Vita.
      - gepresent → **NativeDraw**: el plugin del lado Vita. Recibe lo capturado y lo dibuja con la GPU de la Vita.
      Afecta: nombres de archivo (`ge_patch.prx`, `gepresent.suprx`), carpetas en la Vita (`ux0:data/gepresent/`,
      `ux0:/pspemu/seplugins/`), logs (`ge_patch*.log`), textos del menú, `tools/gepresent/`, el relevo, scripts
      (`resumen_corrida.py`, `compilar.sh`...) y los documentos. Ver si conviene dejar una migración (borrar los
      archivos viejos en la Vita) para no quedar con los dos cargados. Los atajos originales no cambian (L).
- [ ] **2. README nuevo:** ya no es un fork de GePatch. Decir que es un proyecto hecho con IA ("vibecoded"), que está en
      proceso (no listo para usar), qué hace cada plugin, qué se necesita (Adrenaline 8) y el crédito a GePatch como
      punto de partida. Resolver antes la licencia (ver "Pendientes heredados": GPL de PPSSPP/Adrenaline).
- [ ] **3. Instalación final (decidir):** dos plugins que el usuario instala por separado y funcionan juntos, o un
      instalador que deja todo de una vez. Opcional: un actualizador. A considerar: NativeDraw lo carga Adrenaline y
      NativeLink va en `seplugins` (dos lugares distintos); un instalador evita versiones mezcladas entre los dos.
- [ ] **4. Versionado nuevo:** el primer release es **NativeLink 0.1** y **NativeDraw 0.1**. Todas las versiones
      intermedias hasta ese release son **0.0.x**. Definir cómo quedan las de prueba (hoy `.x` sobre la versión: p. ej.
      0.0.5.1) y dónde se anota la equivalencia con las viejas (dev66 / v13.12) para no perder la historia de los
      documentos.
- [ ] **5. Limpiar lo heredado de GePatch** que ya no usa la nueva implementación (arquitectura A: escalado en el PSP,
      opciones de menú que ya no aplican, código muerto). Revisar antes qué sigue usando el modo sombra.
- [ ] **6. Limpiar archivos innecesarios del repo:** ambientes compilados, binarios y restos de pruebas (por ejemplo los
      `.o`, `ge_patch.elf` y `ge_patch.prx` que quedan en la raíz al compilar), dejando en su lugar scripts que los
      preparen o instalen (como `tools/contenedor/preparar.sh`). Revisar también `tools/` (adrfsr, bancos, vprobe...)
      para ver qué queda como herramienta y qué se archiva.

## Rendimiento de la captura para el futuro (etapa 2 cerrada con ≥ 58 fps en lo más pesado de GT)
- [ ] **Leer la memoria del PSP más barato** (decidido postergar, 2026-10-29: 58 fps mínimo alcanza por ahora). Es lo
      que limita la revisión de sublistas: ~2 µs por línea para el capturador y los lectores; el capturador sigue
      comparando ~5,5 ms por frame en lo más pesado de GT. Ideas: vista con caché o DMA (KBENCH: 1,7–4 veces más rápido
      en bloques grandes). Medir primero. (`etapa2_fase2_adelanto.md`, `etapa2_fase1_impacto.md`)
- [ ] **Arranque y final de la vuelta de GT** siguen esperando a la Vita (necesita más de 16,7 ms por frame): 58,2 fps
      en lo más pesado con v13.12.
- [ ] El capturador pierde ~17 % de su núcleo (el 1, con el audio): probar prioridad / núcleo con cuidado del audio.
      (punto 5 de la fase 2, no se hizo)
- [ ] Aplicar una sublista todavía copia sus registros (hasta 64) y matrices: medir cuánto queda con el registro extremo
      (`aplicar_us`) antes de tocarlo.
- [ ] Corrida `sin_captura` en GT para saber si los frames lentos del final de la vuelta son del juego solo.
- [ ] **Acortar el bucle de interpretación** (~360 ns por comando): revisar el tramo de RAM una vez por línea y no por
      comando, y usar menos contadores. Bajaría lo que la memoria de sublistas no se salta. (`etapa2_memoria_sublistas.md`)
- [ ] Comparar directo desde la caché compartida, sin copiar la línea a `R0.buf` (ganancia chica).
      (`etapa2_espejo_vram.md`)
- [ ] Idea chica si vuelve a importar: sacar la línea de la caché compartida con NEON y menos barreras en `clGet`.
      (`etapa2_fase2_estadisticas.md`)
- [x] ~~Mover la compuerta a "antes de encolar la lista siguiente"~~: dev66 + v13.10. (`etapa2_fase2_compuerta.md`)
- [x] ~~Capturador: leer directo de a 256 B en vez de la caché compartida~~: probado en v13.11.1, descartado.
- [x] ~~Aplicar sublistas sin sumar todas las estadísticas~~: v13.11. (`etapa2_fase2_estadisticas.md`)
- [x] ~~Comprobar las sublistas estables al empezar cada lista~~: v13.12, los lectores comprueban por adelantado lo
      aplicado en la lista anterior, desde el final (interruptor `sin_adelanto`). 57,3 → 58,2 fps en lo más pesado de
      GT. (`etapa2_fase2_adelanto.md`)
- [x] ~~Leer menos de la RAM del PSP (no releer líneas, de a 64–256 B)~~: v13.8.1/v13.8.2, descartado.

## Probar en otros juegos (después de la etapa 2)
- [ ] Probar gepresent v13.12 en los otros juegos instalados del usuario, en Silent Hill, Ridge Racer y LocoRoco: la
      compuerta nueva (dev66), la comprobación por adelantado y las sublistas de la VRAM solo se vieron en GT (y SH).
- [ ] Desde v13.12 una línea de una sublista de la VRAM sin copia válida en el espejo del frame se regraba en vez de
      compararse con la copia vieja: si en otros juegos aparecen muchas "cambiaron" en sublistas de la VRAM, mirar esto.
- [ ] **Cuelgue de la Vita** en la primera corrida de GT con v13.11.1 (~10 s después de largar). No volvió a pasar con
      v13.12.1 ni v13.12.2. Si vuelve, anotar versión y momento.

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
- [ ] `memoVerify` compara todas las versiones (hasta 8) de una sublista aunque tengan las mismas líneas: comparar
      una vez por tramo. Hoy en GT casi siempre hay una sola versión, así que no se nota. (`etapa2_comparar_menos.md`)

## Espejo de la VRAM
- [x] ~~**DISTINTAS con el espejo doble**~~: arreglado en v13.12. La memoria comparaba las sublistas de la VRAM contra el
      espejo en que se grabaron (en los frames del otro espejo, una copia de un frame atrás). Ahora guarda la dirección
      de la VRAM y la traduce con `vtr` al comparar. 0 DISTINTAS en ~100 000 revisiones (antes 1–7 por corrida).
      (`etapa2_fase2_adelanto.md`)
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
      La prueba de qemu se compila con `-I` a una carpeta con solo `psp2*` y `vitasdk*` de los encabezados de vitasdk
      (con toda la carpeta `include` choca la libc de vitasdk con la del PC).
- [ ] La VM de Claude en el PC con Windows a veces no llega a GitHub (proxy 403; 2026-10-29 pasó las dos cosas): pull y
      push por paquetes de git (`FLUJO_DE_TRABAJO.md` §2). El push desde el contenedor (`add_repo` con push) funciona;
      los commits subidos así quedan a nombre de Claude.
- [ ] `resumen_corrida.py` todavía no muestra las líneas de la compuerta de dev66 ni la del adelanto de v13.12: hoy se
      sacan con `grep "stats compuerta" ge_patch.log` y `grep "adelanto/f" ge_patch_captura.log`.

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
      `LICENSE.TXT` solo trae el aviso del PSPSDK. Resolver antes del README nuevo (preparación etapa 3, punto 2).
      (`arquitectura_dibujo_en_vita.md` §3.10)
- [ ] La causa de que la Vita se apagara con el banco v11.3 y GePatch activo a 2x sigue sin confirmar. Mientras tanto,
      lo nuevo de la GPU se prueba con el modo sombra o el pasivo. (`etapa1_banco_gxm.md`)
- [ ] A mediano plazo: proponer a Adrenaline un punto de enganche oficial para un "presentador externo", para no
      depender de enganchar su hilo de dibujo. (`arquitectura_dibujo_en_vita.md` §8)
- [ ] Recuperación de la Vita (el usuario la notó "ligeramente afectada" el 2026-10-29): pasos ya dados (apagado
      completo → modo seguro → reconstruir base de datos → reinstalar Adrenaline; nunca actualizar el firmware). Cuando
      el usuario quiera. (`resumen_conversacion_2026-10-29_etapa2j.md`)

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
- [x] Subir a GitHub los commits de dev64 + v13.6, dev65 + v13.9, dev66 + v13.10, v13.11 y v13.12.
