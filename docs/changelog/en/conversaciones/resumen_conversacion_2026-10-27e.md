# Conversation summary of 2026-10-27 (e): RR with "by copy" → the cause was the clear (dev58.8)

Continues `resumen_conversacion_2026-10-27d.md`. **Read in this order: `contexto_ia.md`, `resumen_conversacion_2026-10-27c.md`
(test methodology), this file.** This file **replaces item 0 of "Open / next steps" in
`contexto_ia.md`** (and the "Next step" of summary d) and adds dev58.8 to the history.

## State at close
- Code: **GePatch 0.2-dev58.8, no commit**, on top of dev57 `2c4e937` (branch `dev`). Full backup (diff against dev57):
  `~/Downloads/vitabridge/gepatch_dev58.8.patch` (md5 `03d5b64bb23819317cf8c6261ee5705b`). **It does not include
  `tests/replay.c`**: the dev58.7 patch did not have it either (the context said otherwise): that tool is lost.
- `.prx` md5 `6cc477b85dd244f753e30682c649dcd1`, installed in the lab (`gepatch-lab/memstick/seplugins/` and
  `seplugins/versiones/ge_patch_0.2-dev58.8.prx`) and on the Vita (through `vitabridge/enviar/ux0/pspemu/seplugins/`).
- PC tests: **193 checks, 0 failures** (new `test_v2d_sin_clear`).
- **Confirmed by the user with "2D vertices (HUD, sprites)" = "by copy, without the clear":**
  - RR in the Mac's lab: all fine except the cyan textures (known guard band error, **do not touch yet**).
  - **RR and GT on the Vita: fine.** SH and LocoRoco: **not tested**.

## The cause
**The clear at the start of the frame (CLEARMODE 0x701: color, stencil and z, a full-screen 2D sprite) does not clear when
it goes through the plugin's list (BASE_PLUG + CALL).** With the clear detoured, the z-buffer (0x04100000) and the rt1 area
(0x04120000) do not change during the whole frame (old z from 1861 to 65535) and the 3D world fails the depth test; the HUD,
having no z, is visible. With the clear not detoured, the z changes and the world is visible. The exact reason a clear inside a CALL does not
write was not investigated (on the Vita the symptom with "by copy" was different: clean z and the world on the rt); it is not needed for
the fix.

Bisection (all RR in the lab, values of the menu "2D vertices (HUD, sprites)"; logs and dumps in `vitabridge/`):
| Folder | Value | Result |
|---|---|---|
| `etapa6_dev58.7_lab_RR` | by copy | no 3D world |
| `etapa6_dev58.7_lab_RR_v2d4` | test: minimal list | no 3D world → the detour fails, not the copy |
| `etapa6_dev58.7_lab_RR_v2d1` | in game memory | fine (reference) |
| `etapa6_dev58.8_lab_RR_v2d6` | test: minimal list, without the clear | world fine; HUD unscaled and a tint only at the top left (unscaled full-screen filter): expected in that mode |
| `etapa6_dev58.8_lab_RR_v2d5` | **by copy, without the clear** | **all fine** |
| `etapa6_dev58.8_vita_RR_GT` | **by copy, without the clear** | **RR and GT fine on the Vita** (logs, vpjuego, gepresent, one RR dump) |

Discarded along the way: `choques_memo` (RR has ~180,000 every 600 frames in mode 1 as well; GT 0) and errors in the
plugin's lists (the 139 lists of 2D draws in an RR frame are well formed; `BASE_PLUG` always replaces a
VADDR; the rt1 lists are the same in all modes).

## Performance on the Vita (vpjuego, stretches with GE > 3 ms, non-identical scenes)
| Game | dev58.7 | dev58.8 "by copy, without the clear" |
|---|---|---|
| RR (1.5x) | 40.5 fps, GE 10.7 ms, free CPU 25% | 38.9 fps, GE 11.2 ms, free CPU 26% |
| GT (2x) | 35.2 fps, GE 24.2 ms, free CPU 10% | 37.4 fps, GE 22.4 ms, free CPU 9% |
No clear difference. In RR the plugin's walk during a race takes 9–11 ms per frame (log, `stats plugin`).
The Vita's log shows no cut lists and no `CALL a direccion invalida`.

## dev58.8 (what changed compared to dev58.7)
- "2D vertices (HUD, sprites)" (`v2d`, 0–6) adds **"by copy, without the clear"** (`v2d=5`: like 2, but a 2D draw with
  `CLEARMODE & 1` is scaled in place as in mode 1) and **"test: minimal list, without the clear"** (`v2d=6`: like
  4 with the same exception). In the call to `v2dDraw`, `!(opt.v2d >= 5 && (state.ge_cmd[GE_CMD_CLEARMODE] &
  1))` was added; inside `v2dDraw` a local `m` translates 5→2 and 6→4. `GEPATCH_VERSION` = "0.2-dev58.8".

## For version 59 (next conversation)
1. Make **"by copy" always exclude draws in clear mode** (what is `v2d=5` today) and **remove the bisection
   modes** ("copy without applying", "minimal list", and the two "without the clear"): what remains is "no", "in game memory" and
   "by copy". Adjust `test_v2d_sin_clear` to mode 2 and the tests of the removed modes. Version `0.2-dev59` (tests
   `dev59.x`).
2. Test **SH and LocoRoco on the Vita** with "by copy" (and repeat RR and GT with the clean version). One test per message:
   game, option and value as they appear in the menu.
3. Decide whether "by copy" becomes the default value (today the default is "in game memory", `v2d=1`). If it is
   changed, check that the known profiles and the user's `gepatch_<ID>.txt` files do not force another value
   (the lab's `gepatch_ULJS-00001.txt` has `v2d=2`).
4. **Commit** (dev58 + dev59 together in one commit, version confirmed on the Vita) and update `contexto_ia.md`: item 0
   resolved, history (dev58.8 and dev59), menu row in §2, "2D vertices" in §5, `replay.c` with no backup in §2/§6,
   and `errores_conocidos.md`.
5. Next bug: RR's cyan textures (guard band when enlarging textures), which the user left for this version.

## Working notes (this conversation)
- Started from the linked Mac, with `~/Documents/gepatch-lab` and `~/Downloads` connected.
- `device_bash` runs in an arm64 Linux VM on the Mac: python3 with numpy and PIL, git (read-only to GitHub), no apt and no
  `-m32`: it works for `gpd.py` on the dumps (copy in `vitabridge/coleccion/gpd.py`), not for compiling.
- There is no tool to move files between the container and the Mac: what goes to the container (the patch) the user
  **attaches to the chat**; what goes to the Mac (the `.prx`) is delivered with SendUserFile, the user **downloads it to Downloads** and
  then it is copied with `device_bash` (verify md5). Transcribing binaries as base64 text corrupts them.
- Container: `git clone -b dev` of the repo + `git apply` of the patch; `pspdev` (GitHub tarball) + Adrenaline clone
  in `/home/claude/tc`; for the tests, `apt-get update && apt-get install gcc-multilib`.
- The user changes the option in the menu and closes it without saving: that holds for the session, but the `gepatch_<ID>.txt` is left
  with the previous value.
- To draw a dump's big buffer at 1.5x: 720x408 with **pitch 960** (565). `gpd.py imagenes` assumes 960x544.
- The environment's hook asks for a commit and push at the end of every turn: **it is not done** until the version is confirmed on
  the Vita (user's rule), and the session only has read access to the repo.
