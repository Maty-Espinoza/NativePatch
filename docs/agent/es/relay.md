# El relevo con la Vita (`tools/vitabridge.py`)

La VM de Claude no llega a la red de la casa (la Vita está en 192.168.0.10): todo pasa por el **relevo**, un script que
corre en el computador del usuario y mueve archivos entre una carpeta compartida (`~/Downloads/vitabridge/`,
`Descargas\vitabridge\` en Windows) y la Vita. En la Vita corre **vitacompanion** (FTP en el puerto 1337 y comandos en
el 1338). Lo pone en marcha el usuario (`workflow.md` §1); Claude solo deja y toma archivos en la carpeta.

## Protocolo de archivos
| Archivo / carpeta | Qué hace |
|---|---|
| `estado.txt` | `vivo <hora>, Vita <ip> (respondio <hora>), despierto hasta <hora>` o `dormido`. Si la primera hora es reciente, el relevo está corriendo |
| `enviar/ux0/...` (o `enviar/ur0/...`) | Se sube a la misma ruta de la Vita y pasa a `enviados/` |
| `bajar_<algo>.txt` | Una línea por archivo: `ux0:/ruta` (va a `recibidos/`) o `ux0:/ruta -> ~/Downloads/vitabridge/...` (escribir `~/Downloads/...` literal: la ruta la expande el relevo en el computador del usuario, no la VM) |
| `listar_<algo>.txt` | Una carpeta de la Vita por línea; el listado queda en `respuestas/` |
| `cmd_<algo>.txt` | Comandos a vitacompanion (puerto 1338): `destroy` cierra las aplicaciones, `reboot` reinicia la Vita; la respuesta queda en `respuestas/` |
| `vigilar.txt` | Solo despierta al relevo (cualquier pedido también lo despierta) |
| `recibidos/` | Lo pedido con `bajar_*` y los logs `ux0:/pspemu/native*.log`, que se bajan solos **al cerrar el juego o Adrenaline**: el relevo despierto mira la carpeta cada 60 s y baja un archivo cuando su tamaño no cambió desde la vez anterior. Durante el juego no baja nada |
| `respaldo/` | Copias de cada versión de prueba (`<plugin>_<versión>.*`, `cambios_<versión>.patch`). No se borra solo |
| `vitabridge.log` | Todo lo que hizo el relevo (subidas, bajadas, comandos, errores, limpieza) |

Los pedidos (`bajar_*.txt`, `cmd_*.txt`...) se escriben con otro nombre (por ejemplo en `vitabridge/tmp_claude/`) y
se renombran al final: el relevo puede tomar un archivo a medio escribir.

## Mandar una versión nueva
- **NativeLink:** `enviar/ux0/pspemu/seplugins/nativelink.prx`. Copia también en `Descargas/nativelink.prx` (pedido del
  usuario). Se carga al abrir el juego siguiente.
- **NativeDraw:** `enviar/ux0/data/nativedraw/nativedraw.suprx`. Lo carga Adrenaline al abrirse: después de subirlo,
  **siempre** mandar `cmd_cerrar.txt` con `destroy` (o `reboot` si hace falta reiniciar). Adrenaline sigue con el plugin
  viejo cargado hasta cerrarse.
- **Instalador** (`NativePatch.vpk`): solo para instalar por primera vez o probar el instalador: se sube a
  `enviar/ux0/data/nativepatch/` y el usuario lo instala con VitaShell. Para las versiones de prueba basta el relevo (los
  plugins ya están activados en `plugins.txt` y `config.txt`).
- **Verificar:** un `bajar_verif.txt` que baje lo subido a `vitabridge/etapa<N>_<versión>/verif/` y comparar el md5.
  `device_commit_files` puede entregar una copia vieja si se recompila con el mismo nombre de archivo: al recompilar,
  mandar una copia con otro nombre y comparar el md5 antes de pedir la prueba.
- **Interruptores** en `ux0:data/nativedraw/`: se cambian subiendo el archivo (`enviar/ux0/data/nativedraw/<nombre>`);
  el relevo no borra en la Vita, así que se apagan con `no` adentro. Lista en `../../dev/es/logs-and-switches.md`. Los
  que solo se leen al abrir Adrenaline necesitan `destroy`.

## Cómo se comporta el relevo (y por qué)
- **Despierto / dormido:** trabaja hasta 30 min después del último pedido o de que un log creció. Despierto pide
  `nosleep on` (la Vita no se duerme sola); dormido no toca la Vita y manda `nosleep off` para que se duerma y ahorre
  batería. Si una prueba empieza más de 30 min después del último pedido, dejar un `vigilar.txt`.
- **Abre el FTP solo para lo que haya que hacer y lo cierra.** El FTP de vitacompanion pierde memoria con cada uso:
  consultarlo cada segundo todo el día colgaba la Vita dentro y fuera de Adrenaline (a las ~600 descargas respondía
  "550 Could not allocate memory").
- **Limpieza automática (desde el 2026-10-31):** una vez por hora borra de `enviados/`, `recibidos/` y `respuestas/`
  lo que tenga más de 7 días (`LIMPIAR_DIAS` en el script), los `.parcial` de más de un día y las carpetas que queden
  vacías. Por eso lo que valga la pena conservar de una prueba se copia antes a `vitabridge/etapa<N>_<versión>/`.
  `respaldo/` y las carpetas `etapaN_*` no se tocan.
- **Si se queda pegado** (el latido de `estado.txt` deja de avanzar): pedir al usuario que cierre la ventana y vuelva a
  abrirlo. "timed out" seguidos = la Vita no responde (dormida o colgada). "Host is down" = el wifi de la Vita se cayó;
  los pedidos quedan esperando y se cumplen solos al volver.
- En Windows el relevo que se abre es la copia en `Descargas\` (`vitabridge.cmd` + `vitabridge.py`): si cambia
  `tools/vitabridge.py`, hay que copiarlo de nuevo ahí (`tools/windows/instalar.sh` lo hace).

## Carpetas de resultados
`vitabridge/etapa<N>_<versión>/<juego><n>/` guarda los logs de cada corrida (`workflow.md` §6) y, dentro, `volcados/`
con las carpetas de volcados bajadas y `verif/` con las verificaciones de md5. Las carpetas de etapas cerradas se borran
cuando ya no sirven (`good-practices.md`).
