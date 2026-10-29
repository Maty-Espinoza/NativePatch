# Ambiente en Windows (WSL)

Instala lo necesario para compilar NativeLink y NativeDraw desde un PC con Windows, sin tocar las variables de entorno
de Windows (los compiladores quedan dentro de Ubuntu/WSL, en `~/tc`).

Requisitos: WSL con Ubuntu (`wsl --install` en PowerShell como administrador) y Python para Windows (python.org, para
el relevo con la Vita). Git for Windows es opcional (si está, el push usa su inicio de sesión de GitHub).

## Instalar
Bajar `instalar.cmd` (sirve suelto, sin clonar el repo) y doble clic. Pide la clave de Ubuntu y, la primera vez, el
nombre y correo para los commits. Se puede volver a correr: salta lo que ya está.

| Qué | Dónde |
|---|---|
| Repo | `Documents\GitHub\NativePatch` |
| Relevo con la Vita | `Downloads\vitabridge.cmd` + `vitabridge.py`, carpeta `Downloads\vitabridge\` |
| Compiladores (pspdev, vitasdk, Adrenaline para `systemctrl.h`) | WSL `~/tc` (`tools/preparar.sh`) |

## Usar
- `NativePatch\tools\windows\compilar.cmd`: compila y deja `nativelink.prx`, `nativedraw.suprx` y el instalador en
  `build\` del repo, en Descargas y en `vitabridge\enviar\` (se suben a la Vita con el relevo abierto).
- `Downloads\vitabridge.cmd`: relevo con la Vita (busca la Vita en la red; o `vitabridge.cmd 192.168.0.10`). Lo que
  muestra queda también en `vitabridge\vitabridge.log`.
