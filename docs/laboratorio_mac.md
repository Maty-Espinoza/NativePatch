# Laboratorio en el Mac: correr juegos con GePatch y monitorearlos (viabilidad)

Análisis del 2026-10-22, a pedido del usuario. Base: el modelo PPSSPP-Adrenaline (`modelo_adrenaline.md`), que ya reproduce la Vita byte a byte en la escena GEPVM.

## Conclusión: viable, casi todo ya existe en PPSSPP

PPSSPP (upstream, el mismo commit del parche) ya trae:
- **Versión con interfaz para macOS** (SDL) con depurador ImGui: depurador del GE paso a paso, framebuffers, texturas, visor de memoria, hilos.
- **API de depuración por WebSocket** (localhost): `memory.read/write`, `gpu.buffer.screenshot`, `gpu.record.dump` (volcado .ppdmp del frame), `gpu.stats.get/feed`, `game.state.save/load`, `input.buttons.send`, `cpu.stepping/breakpoint`, `hle.thread.list`, `hle.module.list`, `memory.info.*`.
- **Grabación y reproducción de inputs** (`replay.begin/flush/execute`): botones y analógico por frame, más los resultados de las operaciones de archivo. Junto con un savestate al empezar, permite reproducir una sesión de forma determinista.

El parche Adrenaline está en `Core/`, así que también aplica a la versión con interfaz.

## Arquitectura propuesta

1. **Laboratorio** (macOS): PPSSPP-Adrenaline con interfaz y GePatch cargado desde `seplugins/game.txt`, como en la Vita. Los ISO se bajan una vez de la Vita por el relevo (FTP) y quedan en el Mac.
2. **Imagen nativa en la ventana:** cuando DRAW_NATIVE ≠ 0, la ventana muestra los 960x544 de 0x0A400000 (lo mismo que la Vita). Es un cambio chico en la presentación del renderer por software.
3. **Monitor** (script Python como `vitabridge.py`): se conecta al WebSocket y escribe en `~/Downloads/gepatch-lab/sesion-N/`:
   - `frames.jsonl` por frame: fps, tiempo emulado, draws/listas del GE, estadísticas internas de GePatch (bloque `st` publicado en una dirección fija; cambio chico en el plugin), memoria libre por partición y uso de la VRAM.
   - `ge_patch.log` (ya lo escribe el plugin en el memstick del Mac).
   - capturas nativas periódicas y a pedido.
4. **Control por Claude:** como con la Vita, archivos `cmd_*.txt` en la carpeta compartida: savestate, captura, volcado .ppdmp del frame, leer memoria, pausar/avanzar, empezar/terminar grabación de inputs. Claude lee resultados con la conexión al Mac (sondeo cada pocos segundos: no es streaming, pero es casi en vivo).
5. **Inputs y regresión:** cada sesión guarda savestate inicial + replay. Una versión headless en el Mac reproduce la sesión con otra versión de GePatch y compara las imágenes (como `comparar_vm.py`). Los volcados .ppdmp (pocos MB) se pueden reproducir también en el entorno de Claude; los ISO no (límite de 400 MB por archivo y quedan en el Mac).

## Límites

- **Tiempos:** no son los de la Vita ciclo a ciclo. Los fps y el costo del plugin se miden en tiempo emulado; sirven para comparar versiones, no para predecir los fps en la consola. Lo exacto es la imagen y el orden de los comandos del GE.
- **Velocidad:** para ser exacto se usa el intérprete IR y el renderer por software. En un Mac con Apple Silicon se estima que los juegos corren más lento que en tiempo real (hay que medirlo). Se puede ofrecer un modo rápido con JIT, que deja de ser exacto en los reflejos de la VRAM.
- **Fallas de Adrenaline** (c1-2858-3, cierres): solo se avisan, no se reproducen.
- **Requisitos en el Mac:** Xcode Command Line Tools, Homebrew (cmake, pkgconf, sdl3, sdl3_ttf; opcionales fontconfig y fribidi: el PPSSPP del parche usa SDL3) y correr una vez un script de compilación (~15 min).

## Fases

1. Compilación en el Mac + imagen nativa en la ventana + correr SH y RR (1 sesión).
2. Monitor por WebSocket, telemetría y comandos de Claude (1 sesión).
3. Grabación/reproducción de inputs y corridas de regresión entre versiones de GePatch (1 sesión).
4. Opcional: ventana ImGui "GePatch" con el estado del plugin (render targets, intercambio, log).

## Estado (2026-10-22)
- Scripts en el Mac: `~/Documents/gepatch-lab/compilar.sh` (instala dependencias con brew, clona PPSSPP en 1adf3c5, aplica `adrenaline.patch`, `./b.sh --release` → `ppsspp/build/PPSSPPSDL.app`) y `jugar.sh` (`PPSSPP_ADRENALINE=1`, memstick propio con `seplugins/game.txt` y `ge_patch.prx`, renderer por software).
- El primer intento falló por `SDL3_ttf not found`: el script pedía SDL2. Ya corregido (sdl3 + sdl3_ttf). Falta que el usuario lo corra de nuevo.
- Pendiente: bajar las ISO de Silent Hill y Ridge Racer a `~/Documents/gepatch-lab/iso` con el relevo (`bajar_*.txt` con "ruta -> destino"). Antes hay que reiniciar el relevo con `~/Downloads/vitabridge_nuevo.py`, que procesa `listar_iso.txt`.
- Mandos externos: PPSSPP SDL ya los soporta; el replay graba los botones del PSP, no el mando.
