# Ambientes de compilación y herramientas

Cómo se compilan NativeLink (PSP, MIPS) y NativeDraw (Vita, ARM) en cada lugar, qué hace cada script de `tools/` y
cómo se prueba el capturador fuera de la Vita. Todo lo que necesita PATH queda dentro de una carpeta (`~/tc` o
`/home/claude/tc`): no se tocan variables de entorno del sistema (regla del usuario para Windows).

## Compiladores que hacen falta
| Para | Qué | De dónde |
|---|---|---|
| NativeLink | pspdev (gcc para MIPS, pspsdk) + `systemctrl.h` y `libpspsystemctrl_kernel.a` de Adrenaline | `tools/preparar.sh` los baja (Adrenaline se clona solo para esos dos archivos) |
| NativeDraw | vitasdk (gcc para ARM, vita-toolchain, vita-headers) + paquete `taihen` + stubs generados con `vita-libs-gen-2` | `tools/preparar.sh` |
| Instalador (VPK) | vitasdk (`vita-make-fself`, `vita-mksfoex`, `vita-pack-vpk`) | ídem |
| Sombreadores del traductor (`nativedraw/sombreadores/*.cg`) | compilador de Sony bajo qemu (JagerDesu/vita-shaders) | solo al cambiar un `.cg`; el `.h` compilado va en el repo (`sombreadores/compilar.sh`) |

GCC 15 de vitasdk usa C23 por defecto y `taihen.h` no compila: el envoltorio `arm-none-eabi-gcc` que deja `preparar.sh`
agrega `-std=gnu17`.

## Dónde se compila
| Lugar | Preparar (una vez) | Compilar | Notas |
|---|---|---|---|
| **Contenedor de Claude** (Linux x86_64; el lugar normal) | `T=/home/claude/tc sh tools/preparar.sh` (~3 min; en el contenedor `$HOME` es `/root`, por eso `T`) | `T=/home/claude/tc sh tools/compilar.sh <versión de prueba>` | El contenedor se borra entre sesiones: preparar en cada conversación. El proxy del contenedor solo deja bajar releases de GitHub, no ver sus páginas: `preparar.sh` elige la versión de vitasdk con `git ls-remote` |
| VM Linux ARM64 del Mac (`device_bash`, Ubuntu 22.04 sin root) | `sh tools/mac/preparar_vm.sh` | `V=$HOME/mnt/Downloads/vitabridge sh tools/compilar.sh <versión>` | pspdev para ARM64 pide glibc 2.38: el script usa la de conda-forge con envoltorios por programa (no patchelf: rompe cc1). Se borra con cada sesión |
| WSL del PC con Windows (usuario) | `tools/windows/instalar.cmd` (deja repo, compiladores en `~/tc` de Ubuntu y el relevo en Descargas) | `tools/windows/compilar.cmd [versión]` | Usa `V` = `Descargas\vitabridge` |
| Linux del usuario / Mac nativo | `sh tools/preparar.sh` | `sh tools/compilar.sh` | |
| GitHub Actions (`.github/workflows/ci.yml`) | `tools/preparar.sh` | `tools/compilar.sh` | En cada push; los binarios quedan como artifact. Si falla por descargas de vitasdk, fijar la versión en `preparar.sh` |

## Qué deja `tools/compilar.sh`
- `build/nativelink.prx`, `build/nativedraw.suprx`, `build/NativePatch.vpk` (el instalador con los dos plugins y un
  `version.txt`), más los md5 en pantalla.
- Con versión: copias `build/<plugin>_<versión>.*`, `NativePatch_<versión>.vpk` y `cambios_<versión>.patch` (el
  `git diff`).
- Con `V=<carpeta del relevo>`: los plugins a `V/enviar/ux0/...` (el relevo los sube a la Vita), los plugins y el VPK a la
  carpeta de arriba de `V` (Descargas) y las copias con versión a `V/respaldo/`.
- **Cuidado:** `compilar.sh` no se detiene si NativeDraw no compila (muestra el md5 del `.suprx` anterior). Para ver
  errores de NativeDraw: `cd nativedraw && O=/tmp/nd sh build.sh` con `. $T/env.sh` cargado. Comprobar siempre que el
  md5 cambió.
- Los md5 de NativeDraw cambian en cada compilación (lleva fecha y hora) y entre compiladores: se comparan solo contra
  el binario que se mandó a la Vita.
- `nativedraw/build.sh` enlaza dos veces, la segunda con `-Tdata` una página más allá del código: `vita-elf-create`
  agrega ~1,1 KB al final del segmento de código y fallaba cuando el de datos empezaba muy cerca.
- Versiones: `NATIVELINK_VERSION` en `nativelink/log.h`; `NATIVEDRAW_VERSION` en `nativedraw/version.h`.
  `compilar.sh` las lee para el `version.txt` del instalador.

## Herramientas de `tools/`
| Script | Qué hace |
|---|---|
| `preparar.sh`, `mac/preparar_vm.sh` | Instalan los compiladores (arriba) y escriben `$T/env.sh` |
| `compilar.sh`, `windows/compilar.cmd` | Compilan todo (arriba) |
| `windows/instalar.cmd`, `windows/instalar.sh` | Ambiente completo en Windows + WSL; `instalar.cmd` sirve suelto (baja `instalar.sh` de la rama `dev`) |
| `vitabridge.py`, `windows/vitabridge.cmd` | Relevo con la Vita (`relay.md`) |
| `resumen_corrida.py <carpeta>` | Resumen de una corrida: fps cada 10 s, esperas al sync, espejo, segundos donde la Vita no alcanza, DISTINTAS. Busca las líneas con expresiones regulares: si cambia el texto de un log, cambiarlo ahí también |
| `analisis_fase1.py <carpeta>...` | Resume `nativedraw_frames.log` y `nativedraw_tarde.log` (registro extremo): fps, frames lentos y costo de cada parte por frame |
| `volcado.py <carpeta del volcado> [K]` | Dibuja en el PC un frame volcado imitando al GE y lo compara con la imagen normal y la de la Vita (`../../dev/es/dumps.md`). Necesita numpy y Pillow |

## Prueba del capturador fuera de la Vita (qemu)
`nativedraw/prueba_memoria.c` incluye `captura.c` y lo corre en ARM bajo qemu en el PC con una RAM del PSP falsa: listas
sintéticas, 8 frames, espejo doble simulado, lectores. Sirve para la lógica de la captura (memoria de sublistas,
lectores, comprobación por adelantado, registro del frame); no para lo gráfico. Cómo compilarla y qué modos tiene está
en la cabecera del archivo. Los encabezados se toman de una carpeta con solo `psp2*` y `vitasdk*` de vitasdk (con toda
la carpeta `include` choca la libc de vitasdk con la del PC).

## Proyecto de claude.ai y memoria
- El proyecto "Ge_patch" de claude.ai guarda los resúmenes de conversación (`claude/resumen_conversacion_*.md`), una
  copia de `CLAUDE.md` y algunos documentos del repo. El resumen más nuevo es el punto de partida de cada conversación.
- La memoria de Claude guarda las decisiones del usuario (reglas, preferencias, qué juegos tiene); los detalles técnicos
  van a los documentos del repo, no a la memoria.
