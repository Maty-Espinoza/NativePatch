# Versiones

## Regla (decisión del usuario, 2026-10-29; corregida el mismo día al empezar la etapa 3)
NativeLink (lado PSP) y NativeDraw (lado Vita) son proyectos hermanos: cada uno lleva su propia versión
`0.MENOR.PARCHE` y puede cambiar sin el otro.

- **Hasta el primer release todo es 0.0.x**, toque uno o los dos plugins: cada cambio sube el PARCHE del plugin que
  cambió. **El primer release es NativeLink 0.1.0 + NativeDraw 0.1.0.**
- Después del primer release:

| Cambio | Qué sube | Ejemplo |
|---|---|---|
| Toca **un solo** plugin (no cambia lo que se pasan entre ellos) | el **PARCHE** de ese plugin | NativeDraw 0.1.3 → 0.1.4; NativeLink sigue en 0.1.2 |
| Necesita a **los dos** (buzón compartido, espejo, compuerta, log...) | el **MENOR** de los dos, y el PARCHE vuelve a 0 | NativeLink 0.1.2 + NativeDraw 0.1.4 → 0.2.0 + 0.2.0 |

- **Dos plugins con el mismo MENOR funcionan juntos.** El instalador lleva siempre un par compatible.
- **Versiones de prueba:** un cuarto número sobre la versión que se está preparando (`0.0.3.1`, `0.0.3.2`...). El commit
  que confirma lleva la versión confirmada en la Vita, sin el cuarto número. Cada error arreglado = un commit con su versión.
- **Los commits de prueba también se suben** (decisión del usuario, 2026-10-30): para seguir en otro computador o en otra
  conversación, cada versión de prueba que se manda a la Vita puede ir como commit a `dev`, marcado "(prueba)" con su
  número completo (`NativeDraw 0.0.8.15 (prueba)`). Lo importante son las versiones: que cada commit diga qué versión
  trae y que las confirmadas sigan la numeración sin huecos.
- **Sin versiones perdidas en el repo:** la versión del commit es siempre la que sigue a la última confirmada en GitHub
  (`git log --oneline -- nativedraw/version.h` o `nativelink/log.h`), aunque las pruebas hayan usado otra numeración.
  Ej.: GitHub tenía NativeDraw 0.0.6 y las pruebas llegaron a 0.0.8.12 → el commit es NativeDraw 0.0.7. Los commits
  marcados "(prueba)" no cuentan como versión confirmada.
- **Pruebas de NativeDraw 0.0.8** (confirmado por el usuario): siguen desde **0.0.8.13**, para no
  chocar con los nombres de `vitabridge\backup\` y `vitabridge\stage3_0.0.8.x\`, que ya usaron 0.0.8.1–0.0.8.12
  como pruebas de 0.0.7.
- El primer release (0.1.0) se marca con etiqueta y release en GitHub cuando el usuario lo decida.
  Antes de la 1.0 se quita la opción "Clean test leftovers" del instalador.
- Dónde está el número: NativeLink en `nativelink/log.h` (`NATIVELINK_VERSION`); NativeDraw en `nativedraw/version.h`
  (`NATIVEDRAW_VERSION`). `tools/build.sh` los lee para el instalador (`version.txt` dentro del VPK).
- Etiquetas de git: `nativelink-X.Y.Z` / `nativedraw-X.Y.Z` cuando se publique algo. Las etiquetas `v0.1`…`v0.2` y `0.1`
  del repo son del GePatch original de TheFlow; `gepatch-dev66` es el último estado con los nombres viejos.

## Historial
| NativeLink | NativeDraw | Commit | Qué |
|---|---|---|---|
| 0.0.1 | 0.0.1 | `4faaa2b` | Cambio de nombre y carpetas. Mismo comportamiento que GePatch 0.2-dev66 + gepresent v13.13 |
| 0.0.2 | | `b5d4a56` | NativeLink: se quita lo heredado de GePatch (arquitectura A, menú, volcados) |
| | 0.0.2 | `6992890` | NativeDraw: se quita la presentación vieja, el banco de GXM y los volcados |
| | 0.0.3 | `34dee77` | Etapa 3, paso 3.0: presentador transparente, apagado por defecto (`../../agent/en/stage3-plan.md`) |
| | 0.0.4 | `a0d85da` | Etapa 3, paso 3.1: menú L+R+START dibujado por NativeDraw (por ahora, las versiones) |
| | 0.0.5 | `1ae7191` | Etapa 3, paso 3.2: registro del frame (con `draw`) |
| | 0.0.6 | `da1daa0` | Etapa 3, paso 3.3 (primera versión): la Vita dibuja el registro (Retro City); "tapado" |
| 0.0.3 | 0.0.7 | `f6384f7` | Etapa 3, paso 3.3 en KOF 96 (sombreadores propios, texturas por DMA, volcados) y cuadros rotos arreglados. Pruebas: NativeDraw 0.0.7.1–0.0.8.12 y NativeLink 0.0.3.1–0.0.3.3 (`8573486` era un commit de prueba, 0.0.8.10 + 0.0.3.2) |
| | (pruebas 0.0.8.15–0.0.8.22) | `a045974`…`3ab3b91` | Commits de prueba de la etapa 3: cronómetro de la GPU, texturas ordenadas por la CPU, una escena para la superficie (KOF 96 a 60 fps), Metal Slug XX, diagnóstico del glitch blanco; 0.0.8.22 + NativeLink 0.0.3.4: limpieza del código. El commit que confirme este trabajo será **NativeDraw 0.0.9** |
| (prueba 0.0.3.5) | (prueba 0.0.8.23) | `8cd1ff4`…`5f70fe0` | Compilaciones de desarrollo, commits de prueba: código en inglés, paquete de herramientas listo, mediciones tras `NP_DEV`, las tres pruebas. Debe verse y correr igual |
| (prueba 0.0.3.6) | (prueba 0.0.8.24) | `8cd1ff4`…`b76c030` | Todo el proyecto en inglés: código, nombres de fuentes y herramientas, textos de log, nombres en disco, relay e instalador (nombres antiguos abajo); objetivo de fps por juego en `run_summary.py --check`; el relay parte despierto. Debe verse y andar igual |
| (prueba 0.0.3.6) | (prueba 0.0.8.25–0.0.8.26) | `3d33be9`…`845ff84` | Copias chicas de texturas con CPU y 256 texturas por frame; logs de `vertex_out`, giant draws, frames atrasados y límite de texturas |
| (prueba 0.0.3.6) | (prueba 0.0.8.27) | `3522a99` | Cada juego tiene sus propios logs de NativeDraw |
| (prueba 0.0.3.7–0.0.3.11) | (prueba 0.0.8.28–0.0.8.29) | `f9669cc`…`924332b` | ID del juego sin leer el disco (abre Street Fighter Alpha 3 MAX); el capturador recorre las primeras listas |

Probadas en la Vita con Gran Turismo: `../../changelog/es/etapa3_preparacion.md`.

## Equivalencia con los nombres viejos
Antes del 2026-10-29 el lado PSP se llamaba **GePatch** (`ge_patch.prx`, versiones `0.2-devNN`, con pruebas `devNN.x`) y
el lado Vita **gepresent** (`gepresent.suprx`, versiones `vNN.M`). Los documentos de `../../changelog/es/` usan esos números.

| Viejo | Nuevo |
|---|---|
| GePatch 0.2-dev66 | = NativeLink 0.0.1 |
| gepresent v13.13 | = NativeDraw 0.0.1 |
| `ux0:/pspemu/seplugins/ge_patch.prx` | `ux0:/pspemu/seplugins/nativelink.prx` |
| `ux0:data/gepresent/gepresent.suprx` y sus interruptores | `ux0:data/nativedraw/nativedraw.suprx` (mismos interruptores) |
| `ge_patch.log`, `ge_patch_prev.log` | `nativelink.log`, `nativelink_prev.log` |
| `ge_patch_gepresent.log` | `nativedraw.log` |
| `ge_patch_captura.log` (y `_prev`), `ge_patch_frames.log`, `ge_patch_tarde.log` | `nativedraw_capture.log` (y `_prev`), `nativedraw_frames.log`, `nativedraw_late.log` |

Desde NativeDraw 0.0.8.24 los archivos de interruptores antiguos ya no se leen; "Clean test leftovers" del instalador los borra:

| Antiguo | Nuevo |
|---|---|
| `dibujar`, `espejo_espera`, `medir_ge`, `medir_gpu`, `memo_revisar` | `draw`, `mirror_wait`, `ge_timing`, `gpu_timer`, `memo_check` |
| `nucleo_captura`, `captura_sin_lectores`, `compuerta_sync` | `capture_core`, `capture_no_readers`, `gate_sync` |
| `sin_adelanto`, `sin_captura`, `sin_compuerta`, `sin_espejo`, `sin_libre`, `sin_memoria` | `no_lookahead`, `no_capture`, `no_gate`, `no_mirror`, `no_idle`, `no_memo` |
| `registro` (`minimo`, `mediano`, `extremo`) | `log_level` (`minimum`, `medium`, `extreme`) |
| `volcado` (`desactivado`, `pantallazo`, `frame`, `secuencia`), `volcados/`, `reproducir` | `dump` (`off`, `screenshot`, `frame`, `sequence`), `dumps/`, `replay` |
| `fK_registro.bin`, `fK_reproduccion*.bmp` | `fK_record.bin`, `fK_replay*.bmp` |
| `nativedraw_captura*.log`, `nativedraw_tarde*.log` | `nativedraw_capture*.log`, `nativedraw_late*.log` |
| Relay: `enviar/`, `enviados/`, `recibidos/`, `respuestas/`, `respaldo/`, `bajar_*.txt`, `listar_*.txt`, `vigilar.txt`, `estado.txt`, `etapa<N>_<v>/` | `send/`, `sent/`, `received/`, `responses/`, `backup/`, `get_*.txt`, `list_*.txt`, `watch.txt`, `status.txt`, `stage<N>_<v>/` |

Hitos de la numeración vieja (detalle en `../../changelog/`):

| Versión vieja | Qué |
|---|---|
| GePatch 0.2 (TheFlow) | GePatch original, abandonado |
| dev1–dev60 | Arquitectura A: el PSP dibuja más grande (menú, opciones por juego, volcados .gpd, laboratorio PPSSPP) |
| gepresent v1–v10 | Presentación de la imagen de GePatch con la GPU de la Vita; log y volcados escritos por la Vita |
| gepresent v11 | Etapa 1: banco de pruebas GXM |
| dev61 + v12 | Etapa 2: modo sombra + capturador en la Vita |
| dev62–dev64 + v13.1–v13.6 | Memoria de sublistas, espejo de la VRAM (doble), T4/T6 |
| v13.7 | Banco de núcleos (grabación GXM en varios núcleos) |
| dev65 + v13.9 | Fase 1 de análisis, niveles de registro |
| dev66 + v13.10–v13.12 | Fase 2: compuerta antes de encolar, estadísticas compactas, comprobación por adelantado, MISMATCHES arregladas |
| v13.13 | Ridge Racer a 60 fps: interpretar de a línea, capturador en cualquier núcleo |
| 0.0.4 | 0.0.9 | `c9c50f3` | Etapa 3: KOF 96 a 60 fps, Metal Slug XX a 30 fps, batería sintética NPTEST y comparador, paquete del emulador, arreglos de los volcados de NPTEST. Pruebas: NativeDraw 0.0.8.13–0.0.8.32 y NativeLink 0.0.3.1–0.0.3.11 |
| 0.0.4 | 0.0.10 | `b8f6d11` | Caché de texturas entre frames para texturas de la RAM. Metal Slug XX: capturador de 19,4 a 8,3 ms por frame. Prueba: NativeDraw 0.0.10.1 |
| (prueba 0.0.4.1) | (prueba 0.0.12.1–0.0.12.14) | `9fa9f60`…`35e8e59` | 3D en la Vita con Ridge Racer: menús y selector en HD |
| 0.0.5 | 0.0.12 | (commit "NativeDraw 0.0.12 + NativeLink 0.0.5") | 3D en el traductor, registros reales del GE, capturador y traductor más rápidos |
| 0.0.6 | 0.0.13 | (commit "NativeDraw 0.0.13 + NativeLink 0.0.6") | Perfil del frame, compuerta siempre capaz de volver antes de encolar, listas abandonadas descartadas, log propio fuera del hilo de dibujo, HD solo cuando se sostiene. Pruebas: NativeDraw 0.0.13.1–0.0.13.7, NativeLink 0.0.6.1–0.0.6.2 |
| 0.0.4 | 0.0.11 | (commit "NativeDraw 0.0.11") | Abanicos de triángulos, muestra del caché corregida, texturas chicas fuera del caché, volcados con respaldo a un frame. Pruebas: NativeDraw 0.0.11.1–0.0.11.3 |
| 0.0.6 | 0.0.14 | (commit "NativeDraw 0.0.14") | Carrera de Ridge Racer en HD: cortes de escena, lectura de la superficie y de una imagen en curso, lo que calcula el PSP viene del espejo, el capturador salta frames que nadie toma, frames fallidos guardados. Pruebas: NativeDraw 0.0.14.1–0.0.14.15 |
| 0.0.6 | 0.0.15 | (commit "NativeDraw 0.0.15") | Vuelta de Ridge Racer a 20-25 frames HD por segundo: CPU y GPU en paralelo, draws unidos, vértices con caché, nitro, arreglos del espejo, imágenes intermedias ya no encogidas. Pruebas 0.0.15.1–0.0.15.10 |
| 0.0.6 | 0.0.16 | (commit "NativeDraw 0.0.16") | Ridge Racer sin relojes altos: juego a 60 fps, carrera completa en HD a 21-25 frames HD por segundo; fuga del espejo arreglada, capturador más liviano. Pruebas 0.0.16.1–0.0.16.8 |
| 0.0.6 | 0.0.17 | (commit "NativeDraw 0.0.17") | `nd_fd` (GPU 45 -> 38 ms por frame HD en carrera), bucle NEON para el vértice 3D común (traductor 42 -> 38 ms). Pruebas 0.0.17.1–0.0.17.2 |
| 0.0.6 | 0.0.18 | (commit "NativeDraw 0.0.18") | `nd_fa` (GPU 38 -> 32,5 ms por frame HD en carrera), `vert_split` y `thread_split`. Pruebas 0.0.17.3–0.0.17.6 |
| 0.0.6 | 0.0.19 | (commit "NativeDraw 0.0.19") | `nd_v3`: primera etapa de C.1, la GPU mueve 54 % de los vértices de la carrera (vértices ~19,5 -> 13,1 ms por frame HD). Pruebas 0.0.18.1–0.0.18.8 |
| 0.0.6 | 0.0.20 | (commit "NativeDraw 0.0.20") | `nd_v4`: la GPU ilumina los autos (selección de autos: traductor ~33 -> 16,5 ms por frame HD). Pruebas 0.0.19.1–0.0.19.4 |
| 0.0.6 | 0.0.21 | (commit "NativeDraw 0.0.21") | Un frame se salta solo si al capturador le falta tiempo o la GPU aún lee el registro (selección de autos: ~28 -> ~35 frames HD por segundo). Pruebas 0.0.20.1–0.0.20.3 |
| 0.0.7 | 0.0.22 | (commit "NativeDraw 0.0.22 + NativeLink 0.0.7") | Con `hold_game`, Ridge Racer se mantiene en HD toda la carrera. Dibujos juntos por índices y camino corto en el traductor: público 18,8 -> 25,5 fps, traductor 45 -> 30 ms. `measures.h`, `run_compare.py`. Pruebas 0.0.21.1–0.0.21.6 y NativeLink 0.0.6.1 |
| 0.0.7 | 0.0.23 | (commit "NativeDraw 0.0.23") | Retrovisor en la GPU, en una imagen propia que se pega en la superficie: tramos difíciles 24,5 -> 28,8 fps, público 25,5 -> 28,4 fps. Prueba 0.0.22.1 |
| 0.0.7 | 0.0.24 | (commit "NativeDraw 0.0.24") | Imágenes que solo difuminan a tamaño PSP: carrera normal 29,7 -> 32,4 fps, GPU ~4 ms menos. Logs de escenas y recursos. Pruebas 0.0.23.1-0.0.23.5 |
| 0.0.8 | 0.0.25 | (commit "NativeDraw 0.0.25") | God of War Ghost of Sparta: el capturador lee las sublistas copiadas a la VRAM durante el frame, y un juego ya no hereda el estado del GE del juego anterior. Aún sin HD en el menú ni jugando: pasa el tope de dibujos del registro |
| 0.0.8 | 0.0.26 | (commit "NativeDraw 0.0.26") | Menú y mitad del combate de God of War en HD: registro en el bloque libre, huesos, imágenes intermedias compartidas, vértices de VRAM en la GPU, transferencias por DMA (combate 5,5 -> 6,8 fps con `hold_game`). Pruebas 0.0.25.1-0.0.25.26 |
| 0.0.8 | 0.0.27 | (commit "NativeDraw 0.0.27") | Combate de God of War en HD con el brillo: huesos y luces en la GPU (`nd_v5`), `tx` de 3 MB, colores fijos de mezcla (traductor ~230 -> ~50 ms por frame HD; juego ~8 fps, el capturador es el límite). Pruebas 0.0.26.1-0.0.26.6 |
| 0.0.8 | 0.0.28 | (commit "NativeDraw 0.0.28") | Capturador: texturas en RAM repetidas sin búsqueda, texturas chicas con swizzle ordenadas desde memoria con caché, transferencias por una copia con caché de la que el registro lee los vértices (combate de God of War ~8 -> 11,2 fps, capturador ~125 -> ~94 ms). Prueba 0.0.27.1 |
