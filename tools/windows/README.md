# Ambiente en Windows (WSL)

Instala todo lo necesario para trabajar en NativePatch desde un PC con Windows, sin tocar las variables de entorno de
Windows (el compilador y PPSSPP quedan dentro de Ubuntu/WSL).

Requisitos: WSL con Ubuntu (`wsl --install` en PowerShell como administrador) y Python para Windows (python.org, para
el relevo con la Vita). Git for Windows es opcional (si está, el push usa su inicio de sesión de GitHub).

## Instalar
Bajar `instalar.cmd` (sirve suelto, sin clonar el repo) y doble clic. Pide la clave de Ubuntu y, la primera vez, el
nombre y correo para los commits. Con `instalar.cmd laboratorio` compila también PPSSPP-Adrenaline (20–40 min más).

Queda:

| Qué | Dónde |
|---|---|
| Repo y Adrenaline | `Documents\GitHub\NativePatch`, `Documents\GitHub\Adrenaline` |
| Scripts y laboratorio | `Documents\GitHub\gepatch-lab\` (`compilar.cmd`, `jugar.cmd`, `memstick\`, `iso\`) |
| Relevo con la Vita | `Downloads\vitabridge.cmd` + `vitabridge.py`, carpeta `Downloads\vitabridge\` |
| Compilador del PSP | WSL `~/pspdev` |
| PPSSPP (laboratorio) | WSL `~/gepatch-lab/ppsspp/build/{PPSSPPSDL,PPSSPPHeadless}` |

## Usar
- `gepatch-lab\compilar.cmd`: compila y copia `ge_patch.prx` a Descargas, a `vitabridge\enviar\` (se sube a la Vita
  con el relevo abierto) y al laboratorio.
- `Downloads\vitabridge.cmd`: relevo con la Vita (busca la Vita en la red; o `vitabridge.cmd 192.168.0.10`). Lo que
  muestra queda también en `vitabridge\vitabridge.log`.
- `gepatch-lab\jugar.cmd rr.iso`: abre un juego en el laboratorio (ISO en `gepatch-lab\iso\`).

Se puede volver a correr: salta lo que ya está (repos, pspdev, PPSSPP) y vuelve a copiar los scripts, el relevo y un `ge_patch.prx` recién compilado.
