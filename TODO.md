# TODO: pendientes

Cosas pendientes, de lo más próximo a lo que puede esperar. Cada una dice de dónde salió (documento en `docs/dev/`,
`docs/changelog/` o en el proyecto de claude.ai). Actualizado: 2026-10-29 (**NativeLink 0.0.2 + NativeDraw 0.0.2**:
nombres nuevos, limpieza, instalador; antes GePatch 0.2-dev66 + gepresent v13.13 con la **etapa 2 cerrada**). Lo que
sigue es la etapa 3 de `docs/dev/arquitectura_dibujo_en_vita.md` §6. Cómo se trabaja: `docs/dev/FLUJO_DE_TRABAJO.md`.
Los números viejos (devNN, vNN) se explican en `docs/dev/versiones.md`. Los `etapaN_*.md` citados están en
`docs/changelog/`; `arquitectura_dibujo_en_vita.md` y `registros.md`, en `docs/dev/`.

## Nueva identidad del proyecto (pedido del usuario, 2026-10-29)
- [x] ~~1. Cambio de nombre~~: GePatch → **NativeLink** (lado PSP), gepresent → **NativeDraw** (lado Vita), cada uno en
      su carpeta; archivos, logs, rutas en la Vita, relevo y scripts con los nombres nuevos (0.0.1).
- [x] ~~2. README nuevo~~ (proyecto hecho con IA, en desarrollo, qué hace cada plugin, Adrenaline 8, crédito a GePatch) y
      **licencia GPLv3** (`LICENSE`, `AVISOS_TERCEROS.md`).
- [x] ~~3. Instalación~~: un VPK (`instalador/`, `NativePatch.vpk`) que instala/actualiza, desinstala y limpia restos.
- [x] ~~4. Versionado~~ (`docs/dev/versiones.md`): `0.MENOR.PARCHE` por plugin; un plugin solo = PARCHE, los dos = MENOR.
- [x] ~~5. Quitar lo heredado de GePatch~~ (NativeLink 0.0.2, NativeDraw 0.0.2).
- [x] ~~6. Limpiar el repo~~: salidas de compilación en `build/`, herramientas viejas fuera (etiqueta `gepatch-dev66`),
      ambiente por scripts (`tools/preparar.sh`, `tools/compilar.sh`, `tools/windows/`), documentos en `docs/dev` y
      `docs/changelog`.
- [x] ~~Separar el repo del fork en GitHub~~ (2026-10-29): ya no es fork, rama por defecto `master`, etiqueta
      `gepatch-dev66` subida.
- [x] ~~Probar el instalador en la Vita~~: limpiar restos + instalar funcionaron (`docs/changelog/etapa3_preparacion.md`).
      Falta probar en la Vita "Desinstalar" y reinstalar sobre una instalación hecha (en el PC funcionan).
- [ ] Correr **Ridge Racer** con NativeDraw 0.0.2 (solo se probó GT; la captura no cambió).
- [ ] **Menú L+R+START dibujado por NativeDraw** (decisión del usuario): todas las opciones que hoy son archivos en
      `ux0:data/nativedraw/` (`registro`, `nucleo_captura`, `medir_ge`, `off` y los de análisis, `docs/dev/registros.md`)
      tienen que poder cambiarse desde ese menú. Depende de que NativeDraw dibuje (etapa 3). Ideas: NativeLink detecta los
      botones y bloquea los del juego mientras el menú está abierto (la parte de los botones del menú viejo está en la
      etiqueta `gepatch-dev66`, `menu.c`: `ctrl*Patched`); el estado del menú va por el buzón; NativeDraw lo dibuja encima
      con GXM y guarda las opciones en `ux0:data/nativedraw/opciones.txt`. Las que solo valen al abrir Adrenaline
      (`nucleo_captura`, `off`) lo tienen que decir en el menú.
- [ ] **Actualizador** en el instalador (a futuro): buscar en GitHub (releases) si hay un VPK nuevo y bajarlo.
- [ ] **Antes de la 1.0:** quitar "Limpiar restos de pruebas" del instalador.
- [ ] **Antes del primer release:** quitar `docs/changelog/conversaciones/` (queda `CHANGELOG.md` como resumen) y armar el
      release en GitHub (VPK + plugins sueltos), con etiquetas `nativelink-X.Y.Z` / `nativedraw-X.Y.Z`.
- [ ] Compilación automática de GitHub (`.github/workflows/ci.yml`): hoy compila todo con `tools/preparar.sh` +
      `tools/compilar.sh`; si falla por descargas de vitasdk, fijar la versión.

## Rendimiento de la captura para el futuro (etapa 2 cerrada con ≥ 58 fps en lo más pesado de GT)
- [ ] **Leer la memoria del PSP más barato** (decidido postergar, 2026-10-29: 58 fps mínimo alcanza por ahora). Es lo
      que limita la revisión de sublistas: ~2 µs por línea para el capturador y los lectores; el capturador sigue
      comparando ~5,5 ms por frame en lo más pesado de GT. Ideas: vista con caché o DMA (KBENCH: 1,7–4 veces más rápido
      en bloques grandes). Medir primero. (`docs/changelog/etapa2_fase2_adelanto.md`, `etapa2_fase1_impacto.md`)
- [x] ~~Arranque y final de la vuelta de GT esperando a la Vita~~: con v13.13 GT va a 60,2 fps en lo más pesado
      (13,4 ms de Vita por frame), 0 segundos de carrera bajo 58. (`docs/changelog/etapa2_ridge_racer.md`)
- [x] ~~El capturador pierde ~17 % de su núcleo (el 1, con el audio)~~: v13.13, el capturador puede correr en cualquiera
      de los 3 núcleos (interruptor `nucleo_captura` para fijarlo): recibe ~100 %, sin cortes de audio en RR ni en GT.
- [ ] Aplicar una sublista todavía copia sus registros (hasta 64) y matrices: medir cuánto queda con el registro extremo
      (`aplicar_us`) antes de tocarlo.
- [ ] Corrida `sin_captura` en GT para saber si los frames lentos del final de la vuelta son del juego solo.
- [x] ~~Acortar el bucle de interpretación: revisar el tramo de RAM una vez por línea~~: v13.13, `walkList` de a línea
      de 64 bytes (RR: ~1 070 → ~800 ns por comando). (`docs/changelog/etapa2_ridge_racer.md`)
- [ ] Bucle de interpretación, lo que queda: todavía cuenta por comando `l->n`, `acc.cmds`, `f_cmds`, `l->pc`, `l->prev`
      y `l->hi`; se podrían sumar una vez por línea (cuidando los `return` dentro del `switch`). Las estadísticas de
      `draw` (lotes, cambios de estado) podrían pasar al registro extremo ahora que la etapa 2 terminó.
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
- [ ] Probar NativeDraw (= gepresent v13.13) en los otros juegos instalados del usuario y en Silent Hill (y LocoRoco con el núcleo
      libre): la compuerta nueva (dev66), la comprobación por adelantado, las sublistas de la VRAM y el capturador en
      cualquier núcleo solo se vieron en GT, RR y LocoRoco.
- [ ] Desde v13.12 una línea de una sublista de la VRAM sin copia válida en el espejo del frame se regraba en vez de
      compararse con la copia vieja: si en otros juegos aparecen muchas "cambiaron" en sublistas de la VRAM, mirar esto.
- [x] ~~LocoRoco~~ (2026-10-29, v13.12, `vitabridge/etapa2_v13.12/locoroco1`): 59,9 fps casi siempre, 0 DISTINTAS en 65 330;
      cambia una sublista antes de encolar la siguiente → la compuerta se queda en el sync (correcto). ~4–8 ms de Vita.
- [x] ~~**Ridge Racer** a ~30 fps en el tramo del público~~ (v13.12: 17–21 mil comandos y ~3 700 draws por frame; sin
      sublistas que saltar, ~1 µs por comando, capturador con ~65 % de su núcleo): **60 fps con v13.13** (de a línea +
      capturador en cualquier núcleo), 1 segundo a 55 en toda la carrera. (`docs/changelog/etapa2_ridge_racer.md`)
- [ ] Ridge Racer reescribe 16 líneas de su lista antes de encolar la siguiente, así que la compuerta se queda en el sync
      (menos margen para la Vita). Ver qué cambian: si es algo que la Vita ya leyó, se podría usar la compuerta antes de
      encolar. Va a importar cuando la Vita además dibuje (etapa 3).
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
- [ ] Registrar los espejos (MIRROR 0x0A200000 y MIRROR2 0x0AC00000, 2 MB cada uno) con `sceGxmMapMemory` para que la
      GPU de la Vita pueda leerlos al dibujar. La presentación vieja registraba 0x0A000000–0x0A400000 (`mapZonas`, en la
      etiqueta `gepatch-dev66`); NativeDraw 0.0.2 no registra ninguna zona. (`etapa2_espejo_doble.md`)
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
- [ ] T6 apagado por defecto (le quitaba ~2–3 fps a GT): encender con `ux0:data/nativedraw/medir_ge`. Si se quiere
      dejar siempre, muestrear cada ~1 ms o desde otro lado.

## Pruebas y herramientas
- [ ] La prueba de qemu de NativeDraw (`nativedraw/prueba_memoria.c`) se compila con `-I` a una carpeta con solo `psp2*`
      y `vitasdk*` de los encabezados de vitasdk (con toda la carpeta `include` choca la libc de vitasdk con la del PC).
      Las pruebas del PSP (`tests/`) se quitaron en 0.0.2 (eran de la arquitectura A y no compilaban desde dev61).
- [ ] La VM de Claude en el PC con Windows a veces no llega a GitHub (proxy 403): pull y push por paquetes de git
      (`docs/dev/FLUJO_DE_TRABAJO.md` §2). El push desde el contenedor (`add_repo` con push) funciona; los commits
      subidos así quedan a nombre de Claude.
- [ ] `resumen_corrida.py` todavía no muestra las líneas de la compuerta de dev66 ni la del adelanto de v13.12: hoy se
      sacan con `grep "stats compuerta" nativelink.log` y `grep "adelanto/f" nativedraw_captura.log`.

## Mediciones poco confiables
- [ ] El núcleo 0 de la línea `procesador` del log sale casi vacío: el gancho de CreateThread no ve los hilos que
      Adrenaline creó antes de cargar NativeDraw. (`docs/changelog/etapa2_memoria_sublistas.md`)
- [ ] Estadísticas aproximadas de la memoria de sublistas: "cambios" y lotes del primer draw de una sublista aplicada
      dependen del estado de entrada; `chg[]` no suma las sublistas aplicadas. (`etapa2_memoria_sublistas.md`)
- [ ] vpjuego: "GE en stall" siempre da 0 %. Para los tiempos del GE usar las horas de encolado y FINISH (T6).
      (`arquitectura_dibujo_en_vita.md` §7)
- [ ] Leer la hora en la Vita cuesta 390 ns: el registro extremo completo altera lo que mide (corrida 5 de la fase 1).

## Pendientes heredados
- [ ] Pantalla negra durante la presentación del banco de la etapa 1: la textura del juego llegó negra en esos 10 s.
      El banco se quitó en NativeDraw 0.0.2, pero mirar esto cuando la Vita presente su imagen (etapa 3).
      (`docs/changelog/etapa1_banco_gxm.md` §3.3)
- [x] ~~Licencia~~: GPLv3 (`LICENSE`); lo de PPSSPP (GPL 2.0 o posterior) es compatible. `AVISOS_TERCEROS.md`.
- [ ] La causa de que la Vita se apagara con el banco v11.3 y GePatch activo a 2x sigue sin confirmar (el escalado de
      GePatch ya no existe; tener cuidado con lo nuevo de la GPU igual). (`docs/changelog/etapa1_banco_gxm.md`)
- [ ] A mediano plazo: proponer a Adrenaline un punto de enganche oficial para un "presentador externo", para no
      depender de enganchar su hilo de dibujo. (`docs/dev/arquitectura_dibujo_en_vita.md` §8)
- [ ] Recuperación de la Vita (el usuario la notó "ligeramente afectada" el 2026-10-29): pasos ya dados (apagado
      completo → modo seguro → reconstruir base de datos → reinstalar Adrenaline; nunca actualizar el firmware). Cuando
      el usuario quiera. (`docs/changelog/conversaciones/resumen_conversacion_2026-10-29_etapa2j.md`)

## Limpieza
- [x] ~~En la Vita, los restos de GePatch, gepresent y las pruebas~~: borrados con "Limpiar restos de pruebas" del
      instalador (2026-10-29).
- [ ] Paquetes de git sueltos en `Descargas\vitabridge` (`*.bundle`) y los parches de prueba (`*_v13.8.*.patch`,
      `fase1_*.patch`, `fase2_*.patch`): se pueden borrar.
- [ ] Borrar a mano en el PC con Windows: las carpetas vacías `C:\Users\spect\GePatch` y `Documents\ambientepsp`, y
      `Documents\GitHub\gepatch-lab` (scripts y memstick del laboratorio, que ya no se usa). En WSL, `~/gepatch-lab`
      (PPSSPP compilado) y `~/pspdev` (lo reemplaza `~/tc` de `tools/preparar.sh`). En el Mac, `~/Documents/gepatch-lab`.

## Documentación
- [ ] `ambientes_de_prueba.md` y `errores_conocidos.md` (proyecto de claude.ai) describen GePatch con la arquitectura A.
      Cuando la Vita dibuje (etapa 3 en adelante), revisar cuáles errores de imagen desaparecen con B (tabla de
      `docs/dev/arquitectura_dibujo_en_vita.md` §3.7) y archivarlos.
