# Windows environment (WSL)

Installs what is needed to build NativeLink and NativeDraw from a Windows PC without touching Windows
environment variables (the compilers stay inside Ubuntu/WSL, in `~/tc`).
Requirements: WSL with Ubuntu (`wsl --install` in PowerShell as administrator) and Python for Windows (python.org, for
the relay with the Vita). Git for Windows is optional (if present, pushes use its GitHub sign-in).
- Install: download `install.cmd` (it works on its own, without cloning the repo) and double-click it. It asks for the
  Ubuntu password and, the first time, the name and e-mail for commits. It can be run again: it skips what is already
  there. It leaves the repo in `Documents\GitHub\NativePatch`, the relay in `Downloads\vitabridge.cmd` + `vitabridge.py`
  (folder `Downloads\vitabridge\`) and the compilers in WSL `~/tc` (`tools/setup.sh`).
- Use: `NativePatch\tools\windows\build.cmd` builds and leaves `nativelink.prx`, `nativedraw.suprx` and the
  installer in the repo's `build\`, in Downloads and in `vitabridge\send\` (uploaded to the Vita by the relay).
  `Downloads\vitabridge.cmd` runs the relay (finds the Vita on the network; or `vitabridge.cmd 192.168.0.10`); its output
  also goes to `vitabridge\vitabridge.log`.
