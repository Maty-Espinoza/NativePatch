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
  lleva la versión confirmada en la Vita, sin el cuarto número. Cada error arreglado = un commit con su versión.
- El primer release (0.1.0) se marca con etiqueta y release en GitHub cuando el usuario lo decida.
  Antes de la 1.0 se quita la opción "Limpiar restos de pruebas" del instalador.
- Dónde está el número: NativeLink en `nativelink/log.h` (`NATIVELINK_VERSION`); NativeDraw en `nativedraw/version.h`
  (`NATIVEDRAW_VERSION`). `tools/compilar.sh` los lee para el instalador (`version.txt` dentro del VPK).
- Etiquetas de git: `nativelink-X.Y.Z` / `nativedraw-X.Y.Z` cuando se publique algo. Las etiquetas `v0.1`…`v0.2` y `0.1`
  del repo son del GePatch original de TheFlow; `gepatch-dev66` es el último estado con los nombres viejos.

## Historial
| NativeLink | NativeDraw | Commit | Qué |
|---|---|---|---|
| 0.0.1 | 0.0.1 | `4faaa2b` | Cambio de nombre y carpetas. Mismo comportamiento que GePatch 0.2-dev66 + gepresent v13.13 |
| 0.0.2 | | `b5d4a56` | NativeLink: se quita lo heredado de GePatch (arquitectura A, menú, volcados) |
| | 0.0.2 | `6992890` | NativeDraw: se quita la presentación vieja, el banco de GXM y los volcados |
| | 0.0.3 | `34dee77` | Etapa 3, paso 3.0: presentador transparente, apagado por defecto (`docs/dev/etapa3_plan.md`) |
| | 0.0.4 | `a0d85da` | Etapa 3, paso 3.1: menú L+R+START dibujado por NativeDraw (por ahora, las versiones) |
| | 0.0.5 | `1ae7191` | Etapa 3, paso 3.2: registro del frame (con `dibujar`) |
| | 0.0.6 | (este commit) | Etapa 3, paso 3.3 (primera versión): la Vita dibuja el registro (Retro City); "tapado" |

Probadas en la Vita con Gran Turismo: `docs/changelog/etapa3_preparacion.md`.

## Equivalencia con los nombres viejos
Antes del 2026-10-29 el lado PSP se llamaba **GePatch** (`ge_patch.prx`, versiones `0.2-devNN`, con pruebas `devNN.x`) y
el lado Vita **gepresent** (`gepresent.suprx`, versiones `vNN.M`). Los documentos de `docs/changelog/` y los comentarios
del código usan esos números.

| Viejo | Nuevo |
|---|---|
| GePatch 0.2-dev66 | = NativeLink 0.0.1 |
| gepresent v13.13 | = NativeDraw 0.0.1 |
| `ux0:/pspemu/seplugins/ge_patch.prx` | `ux0:/pspemu/seplugins/nativelink.prx` |
| `ux0:data/gepresent/gepresent.suprx` y sus interruptores | `ux0:data/nativedraw/nativedraw.suprx` (mismos interruptores) |
| `ge_patch.log`, `ge_patch_prev.log` | `nativelink.log`, `nativelink_prev.log` |
| `ge_patch_gepresent.log` | `nativedraw.log` |
| `ge_patch_captura.log` (y `_prev`), `ge_patch_frames.log`, `ge_patch_tarde.log` | `nativedraw_captura.log` (y `_prev`), `nativedraw_frames.log`, `nativedraw_tarde.log` |

Hitos de la numeración vieja (detalle en `docs/changelog/`):

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
| dev66 + v13.10–v13.12 | Fase 2: compuerta antes de encolar, estadísticas compactas, comprobación por adelantado, DISTINTAS arregladas |
| v13.13 | Ridge Racer a 60 fps: interpretar de a línea, capturador en cualquier núcleo |
