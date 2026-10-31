# Flujo de trabajo (leer al empezar cada conversación)

Cómo se trabaja en NativePatch con Claude: dónde está todo, cómo actualizar y subir el repo, cómo pedir pruebas y
guardar resultados, y las reglas del usuario. Compilar: `environment.md`. Mandar a la Vita y traer logs: `relay.md`.
Reglas de limpieza: `good-practices.md`. Cómo funcionan los plugins: `../../dev/es/how-it-works.md`.

## 1. Al empezar una conversación
1. Leer `good-practices.md` y este documento. Después, el último `claude/resumen_conversacion_*.md` del proyecto de
   claude.ai: dice dónde quedó todo, qué está en la Vita y qué sigue. Luego lo que ese resumen indique y
   `../../../TODO.md`.
2. Ver qué computador está vinculado: `get_device_info` (`platform`: `win32` = PC con Windows, `darwin` = Mac). Si no hay
   ninguno vinculado, las herramientas `mcp__remote-devices__*` no aparecen: pedirle al usuario que vincule el
   computador desde la app de escritorio.
3. Pedir acceso a la carpeta del relevo (una vez por conversación, con `device_request_folder_access`): `~/Downloads/vitabridge`.
   Es la única carpeta del computador del usuario que hace falta para las pruebas. Si la tarea toca el repo del
   usuario (ver 4), también `~/Documents/GitHub`.
4. **El repo de trabajo de Claude es un clon en su contenedor**: `add_repo` (acceso `push`) y `git clone --branch dev
   https://github.com/Maty-Espinoza/nativepatch /home/claude/nativepatch`. Ahí se edita, se compila y se hace push.
   Los repos del Mac (`~/Documents/GitHub/NativePatch`) y del PC con Windows (`C:\Users\spect\Documents\GitHub\NativePatch`)
   son del usuario: pueden estar atrasados y no hace falta tocarlos salvo que él lo pida; se actualizan con
   `git pull --ff-only` (o por paquete de git si la VM de su computador no llega a GitHub, ver §2).

| Qué | Windows (`desktop-19r1h9e`, usuario `spect`) | Mac |
|---|---|---|
| Repo del usuario (rama `dev`) | `C:\Users\spect\Documents\GitHub\NativePatch` | `~/Documents/GitHub/NativePatch` |
| En la VM de Claude (`device_bash`), si se pide acceso | `$HOME/mnt/GitHub/NativePatch` | `$HOME/mnt/GitHub/NativePatch` |
| Relevo (lo abre el usuario) | `Descargas\vitabridge.cmd` (doble clic) | `python3 ~/Documents/GitHub/NativePatch/tools/vitabridge.py` en la Terminal |
| Carpeta del relevo | `Descargas\vitabridge\` | `~/Downloads/vitabridge/` (en la VM: `$HOME/mnt/vitabridge`) |
| Compilar a mano (usuario) | `NativePatch\tools\windows\compilar.cmd` (WSL; la primera vez `instalar.cmd`) | `sh tools/compilar.sh` |

Remoto: `origin` = https://github.com/Maty-Espinoza/NativePatch (público). Ramas: `dev` (trabajo) y `master` (lo
último confirmado). La etiqueta `gepatch-dev66` es el último estado con los nombres viejos.

## 2. Pull y push
- **Push:** desde el contenedor de Claude, con el clon de `add_repo` (acceso push): `git push origin dev` (`--tags` si
  hay etiquetas nuevas). Al usuario no le importa que los commits queden a nombre de Claude; le importa que queden
  subidos para seguir en otra conversación o en otro computador. Si no se puede, avisar: él puede hacer push desde su
  Git (Windows: Git for Windows; Mac: GitHub Desktop).
- **El repo del usuario atrasado:** `git pull --ff-only` en la VM de su computador. La VM a veces **no llega a GitHub**
  (el proxy responde 403): entonces se hace por paquete de git: en el contenedor `git bundle create dev.bundle <commit que
  tiene el usuario>..dev`, se lleva a `~/Downloads/vitabridge/` (`device_commit_files`) y en la VM
  `git fetch $HOME/mnt/vitabridge/dev.bundle dev:refs/remotes/origin/dev && git merge --ff-only origin/dev`.
  Sin permiso de borrado en la carpeta del repo, git deja `.lock` que lo traban (`device_request_delete_permission`).
- **Cambios del usuario sin commit:** antes de tocar su repo, `git status -sb`; si hay cambios, preguntar.
- **Mientras se prueba:** cada versión de prueba que se manda a la Vita se sube como commit a `dev` marcado "(prueba)"
  con su número completo (decisión del usuario, 2026-10-30). Lo importante es que cada commit diga qué versión trae.

## 3. Compilar y mandar a la Vita
- Compilar: `environment.md` (en el contenedor: `T=/home/claude/tc sh tools/preparar.sh` una vez por sesión y
  `T=/home/claude/tc sh tools/compilar.sh <versión de prueba>`).
- Mandar: `relay.md`. En resumen: los binarios van a `vitabridge/enviar/ux0/...`, NativeDraw necesita cerrar Adrenaline
  (`cmd_*.txt` con `destroy`), y se verifica el md5 de lo que llegó a la Vita antes de pedir la prueba.
- **Después de cada cambio se compila y se manda a la Vita** (regla del usuario). Las pruebas son solo en la Vita: no hay
  laboratorio en el PC desde NativeLink 0.0.2 (NativeLink no hace nada visible sin la Vita). La prueba del capturador con
  qemu (`environment.md`) sirve para la lógica de la captura, no para lo gráfico.

## 4. Pedir pruebas al usuario
- Decir **qué juego, qué hacer y en qué fijarse**, y al final un resumen de qué juego va con qué opción. Las opciones,
  con el nombre que tengan en el menú del plugin (cuando exista) o, mientras tanto, qué archivo se subió a
  `ux0:data/nativedraw/`.
- Juegos: Gran Turismo (UCUS-98632; la carrera y el puente son lo más pesado; el usuario hace siempre la misma carrera
  con el mismo auto), Silent Hill Shattered Memories (ULUS-10450; exterior), Ridge Racer (ULJS-00001; lo más pesado es
  la curva antes del público, una vez por vuelta), LocoRoco (NPEG-00024), Retro City Rampage DX (NPUH-10199), KOF 96
  (NPUH-10119, NeoGeo Station), Metal Slug XX (ULUS-10495). L al abrir el juego = NativeLink apagado.
- El usuario no tiene contador de fps: los fps salen de los logs. Si no menciona el audio, estuvo bien.
- Después de cada cambio, explicarle **qué se hizo y cómo se implementó**, en simple (qué se intenta, qué hace cada
  componente), sin detalles técnicos innecesarios: quiere aprender para dar mejores órdenes e ideas.

## 5. Primero el origen (regla del usuario, 2026-10-30)
**Nunca se hace un cambio si no está claro el origen del problema.** Cuando lo que dejó una prueba (logs, volcados, lo
que contó el usuario) no alcanza para explicarlo:
1. **Decir qué información faltó** para saber la causa.
2. **Ver si se puede obtener.** Si se puede, se agrega primero (una línea de log, un contador, un volcado con más datos)
   y se vuelve a probar, sin arreglar nada todavía.
3. **Si no se puede así, investigar cómo**: documentación y código de referencia (Adrenaline, PPSSPP, vitasdk) y, si hace
   falta, programas chicos que hagan la prueba (en el PC con qemu, como `nativedraw/prueba_memoria.c`, o un banco en la
   Vita).
4. **Recién con el origen claro se hace el cambio**, y la prueba siguiente confirma que era eso.
Una sospecha razonable no basta: se dice como sospecha y se confirma con datos antes de cambiar el código.

## 6. Guardar y leer resultados
- Al terminar cada corrida, **antes de que el usuario abra otro juego** (los logs se reemplazan): esperar a que el
  relevo los baje (15–30 s después de cerrar el juego) y copiar de `recibidos/` `nativelink.log`, `nativelink_prev.log`,
  `nativedraw_captura.log`, `nativedraw_captura_prev.log`, `nativedraw.log` (con registro extremo, también
  `nativedraw_frames.log` y `nativedraw_tarde.log`) a `vitabridge/etapa<N>_<versión>/<juego><n>/` (p. ej.
  `etapa3_0.0.8.22/metalslug1`). El relevo borra lo de `recibidos/` a los 7 días: lo que no se copió se pierde.
- **Verificar la versión en los logs antes de analizar:** `head -1 nativelink.log`, la primera línea de
  `nativedraw_captura.log` y la última línea "NativeDraw 0.0.x ... compilado" de `nativedraw.log` (este archivo se
  agrega, no se reemplaza: el análisis empieza en esa línea). El "prev" del log del capturador solo rota al abrir
  Adrenaline: si el usuario cambia de juego sin cerrar Adrenaline, los dos quedan en el mismo log.
- Resumen sin leer los logs enteros: `python3 tools/resumen_corrida.py <carpeta>`. Con registro extremo:
  `python3 tools/analisis_fase1.py <carpeta> [<carpeta> ...]`. Las líneas del log: `../../dev/es/logs-and-switches.md`.
- Errores visuales: volcados (`../../dev/es/dumps.md`) y `tools/volcado.py` para dibujar el frame en el PC y saber si el
  error está en los datos o en el dibujo de la Vita. **Videos solo cuando sean necesarios** (Vitastream + OBS): primero
  logs y volcados.
- Los logs se pueden leer desde la VM del computador (`device_bash`, grep/python3) o traerlos al contenedor
  (`device_stage_files`).

## 7. Al terminar la tarea
1. Compilar, mandar a la Vita y dejar la Vita lista (cerrar Adrenaline con `destroy` si cambió NativeDraw).
2. Actualizar los documentos del repo afectados, **en inglés y en español**: `docs/dev/`, `docs/agent/` (incluido
   `stage3-plan.md` si cambió algo de la etapa), `TODO.md` (quitar lo hecho, agregar lo nuevo), `CHANGELOG.md` (al
   confirmar una versión) y `versions.md` (fila del historial).
3. Commit con la versión y push a `dev` (§2).
4. Escribir el resumen de la conversación en el proyecto de claude.ai (`claude/resumen_conversacion_<fecha>_<etapa>.md`):
   estado (commits, qué hay en la Vita, interruptores), qué se hizo, qué sigue, pendientes.
5. Memoria: anotar las decisiones nuevas del usuario (no los detalles técnicos: eso va a los documentos).

## 8. Reglas del usuario
- Versiones (`../../dev/es/versions.md`): cambio de un plugin = sube su PARCHE; cambio que necesita a los dos = sube el
  MENOR de los dos. Pruebas con un cuarto número y commit "(prueba)". Cada error arreglado = un commit con su versión.
  No deben quedar versiones perdidas: el commit confirmado lleva la que sigue a la última confirmada en GitHub.
- Después de cada cambio: compilar y mandar a la Vita. No hacer procesos que gasten muchos tokens ni cambios al
  ambiente de pruebas sin preguntar; no hacer acciones que no pidió.
- Avisar antes de tocar variables de entorno de Windows. No cambiar los atajos originales (L al abrir = apagado,
  L+R+START menú, L+R+SELECT volcado).
- Experimentos con la GPU de la Vita: con cuidado (un experimento de la etapa 1 apagó la Vita entera). Si algo falla,
  el usuario cierra el juego.
- Cachés "en todos lados": evitar rehacer lo que no cambió; evitar copias y dibujos completos siempre que se pueda.
- Código compacto pero entendible; comentarios según `good-practices.md`. Responder en español.
