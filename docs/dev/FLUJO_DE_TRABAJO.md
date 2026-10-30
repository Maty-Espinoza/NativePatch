# Flujo de trabajo (leer al empezar cada conversación)

Cómo se trabaja en NativePatch con Claude: dónde está todo, cómo actualizar el repo, compilar, mandar a la Vita, pedir
pruebas y guardar resultados. Actualizado el 2026-10-29 (**NativeLink 0.0.2 + NativeDraw 0.0.2**, primera versión con los
nombres nuevos; antes GePatch 0.2-dev66 + gepresent v13.13, ver `versiones.md`). Cómo funcionan los plugins:
`funcionamiento.md`. Hoja de ruta: `arquitectura_dibujo_en_vita.md` §6 (siguiente: etapa 3). Pendientes: `TODO.md` (raíz).

## 1. Al empezar una conversación
1. Leer el último `claude/resumen_conversacion_*.md` del proyecto de claude.ai: dice dónde quedó todo y qué sigue. Después,
   los documentos que ese resumen indique (y `TODO.md`). Los resúmenes de antes del 2026-10-30 también están en
   `docs/changelog/conversaciones/`.
2. Ver qué computador está vinculado: `get_device_info` (`platform`: `win32` = PC con Windows, `darwin` = Mac).
3. Pedir acceso a dos carpetas (una sola vez por conversación, con `device_request_folder_access`):
   - el repo: `~/Documents/GitHub` (contiene `NativePatch`);
   - Descargas: `~/Downloads` (ahí están el relevo `vitabridge.py`/`vitabridge.cmd` y la carpeta `vitabridge/`).
   Y permiso de borrar en `~/Documents/GitHub` (`device_request_delete_permission`): **git borra sus `.lock`** y sin
   ese permiso deja el repo trabado.
4. **El proyecto ya está descargado en los dos computadores; no clonarlo de nuevo.** Pero el usuario trabaja a veces en
   el Mac y a veces en Windows: **el repo del computador vinculado puede estar atrasado → actualizarlo (§2) antes de
   tocar nada.** Comprobar con `git status -sb` que no haya cambios sin commit (si los hay, preguntar).

| Qué | Windows (`desktop-19r1h9e`, usuario `spect`) | Mac |
|---|---|---|
| Repo (rama `dev`) | `C:\Users\spect\Documents\GitHub\NativePatch` | `~/Documents/GitHub/NativePatch` |
| En la VM de Claude (`device_bash`) | `$HOME/mnt/GitHub/NativePatch` | `$HOME/mnt/GitHub/NativePatch` |
| Relevo | `Descargas\vitabridge.cmd` (doble clic) | `python3 ~/Documents/GitHub/NativePatch/tools/vitabridge.py` en la Terminal |
| Carpeta del relevo | `Descargas\vitabridge\` | `~/Downloads/vitabridge/` |
| Dónde compila Claude | su contenedor (x86_64): `tools/preparar.sh` | la VM del Mac (ARM64): `tools/mac/preparar_vm.sh` |
| Compilar a mano (usuario) | `NativePatch\tools\windows\compilar.cmd` (WSL; la primera vez `instalar.cmd`) | `sh tools/compilar.sh` |

Remoto: `origin` = https://github.com/Maty-Espinoza/NativePatch (público). Ramas: `dev` (trabajo) y `master` (lo
último confirmado). Ya no se usa el remoto `upstream` (GePatch original; quedan su historia y la etiqueta `gepatch-dev66`).

## 2. Actualizar el repo (pull) y subir commits (push)
**Primero intentar lo normal** en la VM del computador: `cd $HOME/mnt/GitHub/NativePatch && git pull --ff-only`.
La VM a veces **no llega a GitHub** (el proxy responde 403; pasó en Windows y el 2026-10-29 también en el Mac): entonces
se hace por paquete de git:
1. En el contenedor de Claude (sí puede leer el repo público):
   `git clone --branch dev https://github.com/Maty-Espinoza/nativepatch /home/claude/maty-espinoza/nativepatch`
   (si ya existe: `git -C ... fetch origin dev && git -C ... merge --ff-only origin/dev`).
2. `git bundle create /mnt/user-data/outputs/dev.bundle <commit que tiene el PC>..dev` (el commit del PC: `git log -1`
   en la VM).
3. `device_commit_files` → `C:\Users\spect\Downloads\vitabridge\dev.bundle` (el relevo no sube nada fuera de `enviar\`).
4. En la VM: `git fetch $HOME/mnt/Downloads/vitabridge/dev.bundle dev:refs/remotes/origin/dev && git merge --ff-only origin/dev`.

**Push:** la VM no tiene credenciales de GitHub (ni en el Mac ni en Windows). Se sube desde el contenedor: `add_repo`
con `access: "push"`, clonar donde indique, traer los commits (del clon de lectura con `git fetch`, o de la VM con un
paquete al revés: `git bundle create $HOME/mnt/Downloads/vitabridge/subir.bundle origin/dev..dev`, `device_stage_files`,
`git fetch <paquete> dev && git merge --ff-only FETCH_HEAD`) y `git push origin dev` (con `--tags` si hay etiquetas
nuevas). Al usuario no le importa que los commits queden a nombre de Claude; le importa que queden subidos. Si no se
puede, avisar: él puede hacer push desde su Git (Windows tiene Git for Windows; Mac, GitHub Desktop).

**Mientras se prueba (sin commit):** Claude edita en su copia del contenedor, compila ahí y aplica el mismo cambio al
repo del PC con `git diff > /mnt/user-data/outputs/<versión>.patch` → `device_commit_files` a `vitabridge\` → en la VM
`git apply`. Así el repo del PC siempre tiene lo que corre en la Vita y el parche queda de respaldo en `vitabridge/`.

## 3. Compilar
- **Contenedor de Claude:** `T=/home/claude/tc sh tools/preparar.sh` (una vez por sesión, ~3 min) y
  `T=/home/claude/tc sh tools/compilar.sh <versión de prueba>` (en el contenedor `$HOME` es `/root`: pasar `T`). Deja en
  `build/` del repo `nativelink.prx`, `nativedraw.suprx` y `NativePatch.vpk`, copias con el nombre de la versión y
  `cambios_<versión>.patch`. Copiar los que se van a mandar a `/mnt/user-data/outputs/` para `device_commit_files`.
- **VM del Mac:** `sh tools/mac/preparar_vm.sh` y `V=$HOME/mnt/Downloads/vitabridge sh tools/compilar.sh <versión>`
  (con `V`, ya copia a Descargas, `vitabridge/enviar` y `vitabridge/respaldo`).
- **WSL (usuario):** `tools/windows/compilar.cmd [versión]` hace lo mismo con `V` = `Descargas\vitabridge`.
- Versiones: `NATIVELINK_VERSION` en `nativelink/log.h`; `NATIVEDRAW_VERSION` en `nativedraw/version.h`. Reglas en
  `versiones.md` (pruebas con un cuarto número: 0.0.3.1...).
- Los md5 cambian en cada compilación de NativeDraw (lleva fecha y hora) y entre compiladores (contenedor, Mac, WSL): se
  comparan solo contra el binario que se mandó.
- Prueba del capturador fuera de la Vita (qemu): ver la cabecera de `nativedraw/prueba_memoria.c`.

## 4. Mandar a la Vita (relevo)
Lo pone en marcha el usuario (§1). Claude solo trabaja con archivos en la carpeta del relevo:

| Archivo / carpeta | Qué hace |
|---|---|
| `estado.txt` | `vivo <hora>, Vita <ip> (respondio <hora>), despierto hasta <hora>` o `dormido`: si la primera hora es reciente, el relevo está corriendo |
| `vigilar.txt` | solo despierta al relevo (cualquier pedido también lo despierta): para que vuelva a mirar los logs si la prueba empieza más de 30 min después del último pedido |
| `vitabridge.log` | todo lo que hizo (subidas, bajadas, comandos, errores) |
| `enviar/ux0/...` (o `enviar/ur0/...`) | se sube a la misma ruta de la Vita y pasa a `enviados/` |
| `bajar_<algo>.txt` | una línea por archivo: `ux0:/ruta` (va a `recibidos/`) o `ux0:/ruta -> ~/Downloads/vitabridge/...` |
| `listar_<algo>.txt` | una carpeta de la Vita por línea; el listado queda en `respuestas/` |
| `cmd_<algo>.txt` | comandos a vitacompanion (puerto 1338): `destroy` cierra las aplicaciones, `reboot` reinicia la Vita; la respuesta queda en `respuestas/` |
| `recibidos/` | los logs (`ux0:/pspemu/native*.log`) se bajan solos **al cerrar el juego o Adrenaline**: el relevo despierto mira la carpeta cada 60 s y baja un archivo cuando su tamaño no cambió desde la vez anterior. Durante el juego no baja nada |

- NativeLink: `enviar/ux0/pspemu/seplugins/nativelink.prx`. Copia también en `Descargas/nativelink.prx` (pedido del usuario).
- NativeDraw: `enviar/ux0/data/nativedraw/nativedraw.suprx`. Lo carga Adrenaline al abrirse: después de subirlo, mandar
  `cmd_cerrar.txt` con `destroy` (o `reboot` si hace falta reiniciar) para dejar la Vita lista.
- El instalador (`NativePatch.vpk`) solo hace falta para instalar por primera vez o probar el instalador: se sube a
  `enviar/ux0/data/nativepatch/` y el usuario lo instala con VitaShell. Para las versiones de prueba basta el relevo
  (los plugins ya están activados en `plugins.txt` y `config.txt`).
- Respaldo de cada versión de prueba: `vitabridge/respaldo/<plugin>_<versión>.*` y el parche `cambios_<versión>.patch`.
- **Verificar** lo subido: un `bajar_verif.txt` que lo baje a `vitabridge/<etapa>_<versión>/verif/` y comparar md5.
  `device_commit_files` puede entregar una copia vieja si se recompila con el mismo nombre de archivo (pasó con
  v13.11.2): al recompilar, mandar una copia con otro nombre y comparar el md5 en el PC antes de pedir la prueba.
- **Despierto / dormido** (desde el 2026-10-29): el relevo solo trabaja hasta 30 min después del último pedido o de que un
  log creció. Despierto pide `nosleep on` (la Vita no se duerme sola); dormido no toca la Vita y manda `nosleep off`
  para que se duerma y ahorre batería (vitacompanion enciende `nosleep` en cada arranque). Abre el FTP solo para lo que
  haya que hacer y lo cierra. Motivo: el FTP de vitacompanion pierde memoria; con el relevo consultando todo el día
  (cada 15 s) la Vita se colgaba dentro y fuera de Adrenaline. Si una prueba empieza tarde, dejar un `vigilar.txt`.
- El relevo a veces se queda pegado sin marcar errores (el latido de `estado.txt` deja de avanzar): pedir al usuario que
  cierre la ventana y vuelva a abrir `vitabridge.cmd`. "timed out" seguidos = la Vita no responde (dormida o colgada).
- La VM de Claude no llega a la red de la casa (la Vita está en 192.168.0.10): todo pasa por el relevo.
- Interruptores en `ux0:data/nativedraw/` (el relevo no borra: se apagan subiendo el archivo con `no` adentro):
  `registro`, `medir_ge`, `off`, `nucleo_captura` y los de análisis. Lista y detalle en `registros.md`. (Pendiente:
  cambiarlos desde el menú L+R+START que dibujará NativeDraw, `TODO.md`.)

## 5. Pedir pruebas al usuario
- Decir **qué juego, qué hacer y en qué fijarse**, y al final un resumen de qué juego va con qué opción. Las opciones,
  con el nombre que tengan en el menú del plugin (cuando exista) o, mientras tanto, qué archivo se subió.
- Juegos: Gran Turismo (UCUS-98632; la carrera y el puente son lo más pesado), Silent Hill Shattered Memories
  (ULUS-10450; exterior), Ridge Racer (ULJS-00001; lo más pesado es la curva antes de la zona con público, una vez por
  vuelta), LocoRoco (NPEG-00024). L al abrir el juego = NativeLink apagado.
- Después de cada cambio: un resumen simple (qué se intenta, qué hace cada componente), sin detalles técnicos.

## 6. Guardar y leer resultados
- Al terminar cada corrida, **antes de que abra otro juego** (los logs se reemplazan): esperar a que el relevo los baje
  (15–30 s después de cerrar) y copiar de `recibidos/` `nativelink.log`, `nativelink_prev.log`, `nativedraw_captura.log`,
  `nativedraw_captura_prev.log`, `nativedraw.log` (con registro extremo, también `nativedraw_frames.log` y
  `nativedraw_tarde.log`) a `vitabridge/<etapa>_<versión>/<juego><n>/` (p. ej. `etapa3_0.0.3.1/gt1`).
- Resumen sin leer los logs enteros: `python3 tools/resumen_corrida.py <carpeta>` (fps cada 10 s, esperas al sync,
  espejo, segundos donde la Vita no alcanza, DISTINTAS). Con registro extremo:
  `python3 tools/analisis_fase1.py <carpeta> [<carpeta> ...]`. Los dos leen también las carpetas viejas (`ge_patch*.log`).
- Para comparar corridas, el usuario hace siempre la misma carrera con el mismo auto en GT; las diferencias chicas pueden
  ser de cómo maneja. No tiene contador de fps: salen del log. Si no menciona el audio, estuvo bien.
- Los logs se pueden leer desde la VM (`device_bash`, grep/python3) o traerlos al contenedor con `device_stage_files`.

## 7. Reglas del usuario
- Versiones (`versiones.md`): cambio de un plugin = sube su PARCHE; cambio que necesita a los dos = sube el MENOR de los
  dos. Pruebas con un cuarto número; **commit solo con la versión confirmada**. Cada error arreglado = un commit con su
  versión. No hacer commits solo de documentación si se pueden juntar.
- Después de cada cambio: compilar y mandar a la Vita. Las pruebas son solo en la Vita (el laboratorio PPSSPP se quitó
  en 0.0.2: NativeLink no hace nada visible sin la Vita). No hacer procesos que gasten muchos tokens sin preguntar ni
  acciones que no pidió.
- Avisar antes de tocar variables de entorno de Windows. No cambiar los atajos originales (L al abrir = apagado).
- Experimentos con la GPU de la Vita: con cuidado (el banco v11.3 con el escalado de GePatch apagó la Vita entera).
- Al terminar la tarea: actualizar los documentos del repo (`docs/dev/`, `TODO.md`, `CHANGELOG.md`) y del proyecto
  (`claude/`: resumen de la conversación, `TODO.md`, `flujo_de_trabajo.md`), con lo descubierto.
- Código compacto pero entendible. Responder en español.
