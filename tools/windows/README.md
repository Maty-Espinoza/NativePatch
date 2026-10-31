# Windows environment (WSL) / Ambiente en Windows (WSL)

**English.** Installs what is needed to build NativeLink and NativeDraw from a Windows PC without touching Windows
environment variables (the compilers stay inside Ubuntu/WSL, in `~/tc`).
Requirements: WSL with Ubuntu (`wsl --install` in PowerShell as administrator) and Python for Windows (python.org, for
the relay with the Vita). Git for Windows is optional (if present, pushes use its GitHub sign-in).
- Install: download `instalar.cmd` (it works on its own, without cloning the repo) and double-click it. It asks for the
  Ubuntu password and, the first time, the name and e-mail for commits. It can be run again: it skips what is already
  there. It leaves the repo in `Documents\GitHub\NativePatch`, the relay in `Downloads\vitabridge.cmd` + `vitabridge.py`
  (folder `Downloads\vitabridge\`) and the compilers in WSL `~/tc` (`tools/preparar.sh`).
- Use: `NativePatch\tools\windows\compilar.cmd` builds and leaves `nativelink.prx`, `nativedraw.suprx` and the
  installer in the repo's `build\`, in Downloads and in `vitabridge\enviar\` (uploaded to the Vita by the relay).
  `Downloads\vitabridge.cmd` runs the relay (finds the Vita on the network; or `vitabridge.cmd 192.168.0.10`); its output
  also goes to `vitabridge\vitabridge.log`.

**Español.** Instala lo necesario para compilar NativeLink y NativeDraw desde un PC con Windows sin tocar las variables
de entorno de Windows (los compiladores quedan dentro de Ubuntu/WSL, en `~/tc`).
Requisitos: WSL con Ubuntu (`wsl --install` en PowerShell como administrador) y Python para Windows (python.org, para el
relevo con la Vita). Git for Windows es opcional (si está, el push usa su inicio de sesión de GitHub).
- Instalar: bajar `instalar.cmd` (sirve suelto, sin clonar el repo) y doble clic. Pide la clave de Ubuntu y, la primera
  vez, el nombre y correo para los commits. Se puede volver a correr: salta lo que ya está. Deja el repo en
  `Documents\GitHub\NativePatch`, el relevo en `Downloads\vitabridge.cmd` + `vitabridge.py` (carpeta
  `Downloads\vitabridge\`) y los compiladores en WSL `~/tc` (`tools/preparar.sh`).
- Usar: `NativePatch\tools\windows\compilar.cmd` compila y deja `nativelink.prx`, `nativedraw.suprx` y el instalador en
  `build\` del repo, en Descargas y en `vitabridge\enviar\` (se suben a la Vita con el relevo abierto).
  `Downloads\vitabridge.cmd` es el relevo (busca la Vita en la red; o `vitabridge.cmd 192.168.0.10`); lo que muestra
  queda también en `vitabridge\vitabridge.log`.
