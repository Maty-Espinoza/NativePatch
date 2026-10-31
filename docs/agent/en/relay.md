# The relay with the Vita (`tools/vitabridge.py`)

Claude's VM cannot reach the home network (the Vita is at 192.168.0.10): everything goes through the **relay**, a script
that runs on the user's computer and moves files between a shared folder (`~/Downloads/vitabridge/`,
`Descargas\vitabridge\` on Windows) and the Vita. On the Vita runs **vitacompanion** (FTP on port 1337 and commands on
1338). The user starts it (`workflow.md` §1); Claude only drops and picks up files in the folder.

## File protocol
| File / folder | What it does |
|---|---|
| `estado.txt` | `vivo <time>, Vita <ip> (respondio <time>), despierto hasta <time>` or `dormido`. If the first time is recent, the relay is running |
| `enviar/ux0/...` (or `enviar/ur0/...`) | Is uploaded to the same path on the Vita and moves to `enviados/` |
| `bajar_<something>.txt` | One line per file: `ux0:/path` (goes to `recibidos/`) or `ux0:/path -> ~/Downloads/vitabridge/...` (write `~/Downloads/...` literally: the path is expanded by the relay on the user's computer, not by the VM) |
| `listar_<something>.txt` | One Vita folder per line; the listing ends up in `respuestas/` |
| `cmd_<something>.txt` | Commands to vitacompanion (port 1338): `destroy` closes the applications, `reboot` restarts the Vita; the reply ends up in `respuestas/` |
| `vigilar.txt` | Only wakes the relay (any request also wakes it) |
| `recibidos/` | What was requested with `bajar_*` and the logs `ux0:/pspemu/native*.log`, which are downloaded by themselves **when the game or Adrenaline is closed**: the awake relay looks at the folder every 60 s and downloads a file when its size has not changed since the previous time. During the game it downloads nothing |
| `respaldo/` | Copies of each test version (`<plugin>_<version>.*`, `cambios_<version>.patch`). It is not deleted automatically |
| `vitabridge.log` | Everything the relay did (uploads, downloads, commands, errors, cleanup) |

Requests (`bajar_*.txt`, `cmd_*.txt`...) are written under another name (for example in `vitabridge/tmp_claude/`) and
renamed at the end: the relay may pick up a half-written file.

## Sending a new version
- **NativeLink:** `enviar/ux0/pspemu/seplugins/nativelink.prx`. Also copy to `Descargas/nativelink.prx` (the user's
  request). It is loaded when the next game opens.
- **NativeDraw:** `enviar/ux0/data/nativedraw/nativedraw.suprx`. Adrenaline loads it when it opens: after uploading it,
  **always** send `cmd_cerrar.txt` with `destroy` (or `reboot` if a restart is needed). Adrenaline keeps the old plugin
  loaded until it is closed.
- **Installer** (`NativePatch.vpk`): only to install for the first time or to test the installer: it is uploaded to
  `enviar/ux0/data/nativepatch/` and the user installs it with VitaShell. For test versions the relay is enough (the
  plugins are already enabled in `plugins.txt` and `config.txt`).
- **Verify:** a `bajar_verif.txt` that downloads what was uploaded to `vitabridge/etapa<N>_<version>/verif/` and compare
  the md5. `device_commit_files` may deliver an old copy if it is rebuilt with the same file name: when rebuilding,
  send a copy with another name and compare the md5 before asking for the test.
- **Switches** in `ux0:data/nativedraw/`: they are changed by uploading the file (`enviar/ux0/data/nativedraw/<name>`);
  the relay does not delete on the Vita, so they are turned off with `no` inside. List in `../../dev/en/logs-and-switches.md`.
  Those that are only read when Adrenaline opens need `destroy`.

## How the relay behaves (and why)
- **Awake / asleep:** it works up to 30 min after the last request or after a log grew. Awake, it requests
  `nosleep on` (the Vita does not go to sleep by itself); asleep, it does not touch the Vita and sends `nosleep off` so
  it goes to sleep and saves battery. If a test starts more than 30 min after the last request, leave a `vigilar.txt`.
- **It opens the FTP only for what has to be done and closes it.** vitacompanion's FTP leaks memory with every use:
  querying it every second all day hung the Vita inside and outside Adrenaline (at ~600 downloads it answered
  "550 Could not allocate memory").
- **Automatic cleanup (since 2026-10-31):** once an hour it deletes from `enviados/`, `recibidos/` and `respuestas/`
  whatever is older than 7 days (`LIMPIAR_DIAS` in the script), the `.parcial` files older than a day and the folders
  left empty. That is why what is worth keeping from a test is copied beforehand to `vitabridge/etapa<N>_<version>/`.
  `respaldo/` and the `etapaN_*` folders are not touched.
- **If it gets stuck** (the heartbeat of `estado.txt` stops advancing): ask the user to close the window and open it
  again. Consecutive "timed out" = the Vita does not respond (asleep or hung). "Host is down" = the Vita's wifi dropped;
  the requests stay waiting and are fulfilled by themselves when it comes back.
- On Windows the relay that is opened is the copy in `Descargas\` (`vitabridge.cmd` + `vitabridge.py`): if
  `tools/vitabridge.py` changes, it must be copied there again (`tools/windows/instalar.sh` does it).

## Results folders
`vitabridge/etapa<N>_<version>/<game><n>/` stores the logs of each run (`workflow.md` §6) and, inside it, `volcados/`
with the downloaded dump folders and `verif/` with the md5 verifications. The folders of closed stages are deleted
when they are no longer useful (`good-practices.md`).
