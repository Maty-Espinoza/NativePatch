# Flujo de trabajo (leer al empezar cada conversación)

Cómo se trabaja en NativePatch con Claude: dónde está todo, cómo actualizar el repo, compilar, mandar a la Vita, pedir
pruebas y guardar resultados. Actualizado el 2026-10-29 (GePatch 0.2-dev66, gepresent v13.10, etapa 2 de la
arquitectura B, fase 2: `etapa2_fase2_compuerta.md`; análisis en `etapa2_fase1_impacto.md`). Lo técnico del plugin está en `CONTEXTO_IA.md` (desactualizado desde dev49) y en los documentos de cada
etapa (`arquitectura_dibujo_en_vita.md` §6 es la hoja de ruta).

## 1. Al empezar una conversación
1. Leer el último `claude/resumen_conversacion_*.md` del proyecto de claude.ai: dice dónde quedó todo y qué sigue. Después,
   los documentos que ese resumen indique (y `claude/TODO.md`).
2. Ver qué computador está vinculado: `get_device_info` (`platform`: `win32` = PC con Windows, `darwin` = Mac).
3. Pedir acceso a dos carpetas (una sola vez por conversación, con `device_request_folder_access`):
   - el repo: `~/Documents/GitHub` (contiene `NativePatch`, `Adrenaline` y en Windows `gepatch-lab`);
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
| Dónde compila Claude | su contenedor (x86_64): `tools/contenedor/` | la VM del Mac (ARM64): `tools/mac/` |
| Compilar a mano (usuario) | `Documents\GitHub\gepatch-lab\compilar.cmd` (WSL) | `make` con pspdev |
| Laboratorio PPSSPP | WSL (`ambiente_windows.md`) | `laboratorio_mac.md` |

Remotos: `origin` = https://github.com/Maty-Espinoza/NativePatch (público), `upstream` = GePatch original de TheFlow
(solo para mirar; no hay copias locales del original, a pedido del usuario).

## 2. Actualizar el repo (pull) y subir commits (push)
**Primero intentar lo normal** en la VM del computador: `cd $HOME/mnt/GitHub/NativePatch && git pull --ff-only`.
En Windows (2026-10-29) la VM **no llega a GitHub** (el proxy responde 403): entonces se hace por paquete de git:
1. En el contenedor de Claude (sí puede leer el repo público):
   `git clone --branch dev https://github.com/Maty-Espinoza/nativepatch /home/claude/maty-espinoza/nativepatch`
   (si ya existe: `git -C ... fetch origin dev && git -C ... merge --ff-only origin/dev`).
2. `git bundle create /mnt/user-data/outputs/dev.bundle <commit que tiene el PC>..dev` (el commit del PC: `git log -1`
   en la VM).
3. `device_commit_files` → `C:\Users\spect\Downloads\vitabridge\dev.bundle` (el relevo no sube nada fuera de `enviar\`).
4. En la VM: `git fetch $HOME/mnt/Downloads/vitabridge/dev.bundle dev:refs/remotes/origin/dev && git merge --ff-only origin/dev`.

**Push:** la VM no tiene credenciales de GitHub (ni en el Mac ni en Windows). El commit se hace en la VM (autor:
`git -c user.name=Maty-Espinoza -c user.email=matias.espinozaq@33sur.cl commit ...`, con las líneas de atribución que pida
la sesión) y se sube desde el contenedor: `add_repo` con `access: "push"`, clonar donde indique, traer el commit con un
paquete al revés (en la VM `git bundle create $HOME/mnt/Downloads/vitabridge/subir.bundle origin/dev..dev`,
`device_stage_files`, en el contenedor `git fetch <paquete> dev && git merge --ff-only FETCH_HEAD`) y `git push origin dev`.
Si no se puede, avisar al usuario: él puede hacer push desde su Git (Windows tiene Git for Windows; Mac, GitHub Desktop).

**Mientras se prueba (sin commit):** Claude edita en su copia del contenedor, compila ahí y aplica el mismo cambio al
repo del PC con `git diff > /mnt/user-data/outputs/<versión>.patch` → `device_commit_files` a `vitabridge\` → en la VM
`git apply`. Así el repo del PC siempre tiene lo que corre en la Vita y el parche queda de respaldo en `vitabridge/`.

## 3. Compilar
- **Contenedor de Claude (desde Windows):** `sh tools/contenedor/preparar.sh` (una vez por sesión, ~3 min, en
  `/home/claude/tc`) y desde la raíz del repo `sh tools/contenedor/compilar.sh <versión>`: deja `ge_patch.prx` y
  `gepresent.suprx` (y copias con el nombre de la versión + el parche) en `/mnt/user-data/outputs/`.
- **VM del Mac:** `sh tools/mac/preparar_vm.sh` y `sh tools/mac/compilar_vm.sh <versión>` (ya copia a Descargas,
  `vitabridge/enviar` y el laboratorio).
- Versión de GePatch: `GEPATCH_VERSION` en `log.h`. Versión de gepresent: el texto de `module_start` en
  `tools/gepresent/gepresent.c` (y los que tengan la versión en `captura.c`/`banco.c`).
- Los md5 cambian en cada compilación de gepresent (lleva fecha y hora) y entre compiladores (contenedor, Mac, WSL): se
  comparan solo contra el binario que se mandó.

## 4. Mandar a la Vita (relevo)
Lo pone en marcha el usuario (§1). Claude solo trabaja con archivos en la carpeta del relevo:

| Archivo / carpeta | Qué hace |
|---|---|
| `estado.txt` | `vivo <hora>, Vita <ip>`: si la hora es reciente, el relevo está corriendo |
| `vitabridge.log` | todo lo que hizo (subidas, bajadas, comandos, errores) |
| `enviar/ux0/...` | se sube a la misma ruta de la Vita y pasa a `enviados/` |
| `bajar_<algo>.txt` | una línea por archivo: `ux0:/ruta` (va a `recibidos/`) o `ux0:/ruta -> ~/Downloads/vitabridge/...` |
| `cmd_<algo>.txt` | comandos a vitacompanion (puerto 1338): `destroy` cierra las aplicaciones, `reboot` reinicia la Vita; la respuesta queda en `respuestas/` |
| `recibidos/` | los logs se bajan solos mientras crecen (`ux0:/pspemu/ge_patch*.log`) |

- GePatch: `enviar/ux0/pspemu/seplugins/ge_patch.prx`. Copia también en `Descargas/ge_patch.prx` (pedido del usuario).
- gepresent: `enviar/ux0/data/gepresent/gepresent.suprx`. Lo carga Adrenaline al abrirse: después de subirlo, mandar
  `cmd_cerrar.txt` con `destroy` (o `reboot` si hace falta reiniciar) para dejar la Vita lista.
- Respaldo de cada versión de prueba: `vitabridge/respaldo/ge_patch_<versión>.prx`,
  `vitabridge/gepresent/gepresent_<versión>.suprx` y el parche `vitabridge/<...>_<versión>.patch`.
- **Verificar** lo subido: un `bajar_verif.txt` que lo baje a `vitabridge/<etapa>_<versión>/verif/` y comparar md5.
- Interruptores en `ux0:data/gepresent/` (el relevo no borra: se apagan subiendo el archivo con otro contenido):
  `banco_nucleos` (v13.7: banco de núcleos; contenido = segundos de espera, `no` lo apaga), `banco_v12` (banco de la
  etapa 1), `medir_ge` (T6), `off` (gepresent no engancha nada). Desde v13.9: `registro` (nivel de registro: `minimo`,
  `mediano` (por defecto) o `extremo`) y los de la fase de análisis `sin_compuerta`, `sin_espejo`, `sin_libre`,
  `sin_captura` (se apagan con `no` adentro). Desde v13.10: `compuerta_sync` (la espera a la Vita vuelve al sync, como
  antes de dev66). Detalle en `registros.md`.
- La VM de Claude no llega a la red de la casa (la Vita está en 192.168.0.10): todo pasa por el relevo.

## 5. Pedir pruebas al usuario
- Decir **qué juego, qué hacer y en qué fijarse**, con las opciones **con el nombre que tienen en el menú del plugin**
  (L+R+START), y al final un resumen de qué juego va con qué opción.
- Juegos: Gran Turismo (UCUS-98632; la carrera y el puente son lo más pesado), Silent Hill Shattered Memories
  (ULUS-10450; exterior), Ridge Racer (ULJS-00001), LocoRoco (NPEG-00024). L al abrir el juego = GePatch apagado.
  El modo sombra viene activado por defecto (no hay que tocar el menú).
- Después de cada cambio: un resumen simple (qué se intenta, qué hace cada componente), sin detalles técnicos.

## 6. Guardar y leer resultados
- Al terminar cada corrida, **antes de que abra otro juego** (los logs se reemplazan): copiar de `recibidos/`
  `ge_patch.log`, `ge_patch_prev.log`, `ge_patch_captura.log`, `ge_patch_captura_prev.log`, `ge_patch_gepresent.log`
  (y `ge_patch_banco.log` si corrió un banco; con registro extremo, `ge_patch_frames.log` y `ge_patch_tarde.log`) a `vitabridge/<etapa>_<versión>/<juego><n>/` (p. ej.
  `etapa2_dev64/gt1`).
- Resumen sin leer los logs enteros: `python3 tools/resumen_corrida.py <carpeta>` (fps cada 10 s, esperas al sync,
  espejo, segundos donde la Vita no alcanza, DISTINTAS y el banco de núcleos). Con registro extremo:
  `python3 tools/analisis_fase1.py <carpeta> [<carpeta> ...]` (costo por frame para el juego y atrasos de la Vita).
- Para comparar corridas, el usuario hace siempre la misma carrera con el mismo auto en GT; las diferencias chicas pueden
  ser de cómo maneja.
- Los logs se pueden leer desde la VM (`device_bash`, grep/python3) o traerlos al contenedor con `device_stage_files`.

## 7. Reglas del usuario
- Versiones de prueba con `.x` (dev65.1, v13.7.1...); **commit solo con la versión confirmada**, con su número sin `.x`.
  Cada error arreglado = un commit con su versión. No hacer commits solo de documentación si se pueden juntar.
- Después de cada cambio: compilar, mandar a la Vita y (si aplica) al laboratorio. No cambiar el ambiente de pruebas
  ni hacer procesos que gasten muchos tokens sin preguntar. No hacer acciones que no pidió.
- Avisar antes de tocar variables de entorno de Windows. No cambiar los atajos originales (L activa/desactiva).
- Experimentos con la GPU de la Vita: solo con el modo sombra o GePatch apagado (con GePatch escalando, el banco v11.3
  apagó la Vita entera).
- Al terminar la tarea: actualizar los documentos del repo (`docs/`) y del proyecto (`claude/`: resumen de la
  conversación, `TODO.md`), con lo descubierto.
- Código compacto pero entendible. Responder en español.
