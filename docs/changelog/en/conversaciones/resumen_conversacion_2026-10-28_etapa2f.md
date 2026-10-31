# Summary to continue in another conversation: stage 2, VRAM double mirror (2026-10-28/29, sixth conversation)

Read: this summary → `etapa2_espejo_doble.md` → `TODO.md` → `arquitectura_dibujo_en_vita.md` (§3.5, §6).

## Where we left off
- **Commit `5c4d435` on `dev`, pushed to GitHub**: GePatch **0.2-dev64** + gepresent **v13.6** + `docs/etapa2_espejo_doble.md`
  + `tools/mac/` (build in the Mac's VM) + `TODO.md` and `arquitectura_dibujo_en_vita.md` updated. Before: `a619d80`.
- The commit was made **on the Mac** (`~/Documents/GitHub/NativePatch`), not on Windows: the Windows repo is behind
  (run `git pull` there). The push was done by Claude from its container (with the repo added to the session); the Mac's
  VM has no GitHub credentials.
- On the Vita: GePatch dev64 (`32742693…`) and gepresent v13.6 (`5a6e524e…`), verified in `vitabridge/enviados`. Shadow
  mode by default. **A run with the final numbers is missing** (it is the same code as dev64.5 + v13.6.5).
- Test versions: `vitabridge/respaldo/ge_patch_dev64.{1..5}.prx`, `vitabridge/gepresent/gepresent_v13.6.{1..5}.suprx`,
  patches `vitabridge/gepatch_dev64.*_gepresent_v13.6.*.patch`. Logs: `vitabridge/etapa2_dev64.{1,2,3,4}/gt1`,
  `etapa2_dev64.4/sh1`, `etapa2_dev64.5/sh1`.

## Result
- Two alternating mirrors (0x0A200000 and 0x0AC00000). Each list is read from the last finished copy and the Vita
  announces which one it uses (`SB[0x48]`): the GE never overwrites it. All sublists and up to 64 pages (256 KB) of
  textures per copy.
- The targets of block copies (TRANSFER) in the VRAM are treated as render targets: the Vita will repeat them. In GT
  they are almost all the textures that change during a race; in SH, the per-frame copy of its 480x272 image (1 MB that
  was being copied in vain).
- fps: GT bridge **58.5** (before 57.8), rest 59.7–59.9; SH outdoors **43.4** (before 40.8; the user thinks it did not
  reach 50, as always). SH no longer copies anything.
- Pending: 1–7 "DISTINTAS" per GT run in the sublist memory check (≈0.01 %), before always 0.

## Lessons (do not repeat)
- The test outside the Vita (`prueba_memoria.c`) does not simulate the Vita's notice to the PSP: dev64.1 hung at
  start-up (it announced copy -1) and the simulation gave good results.
- **Nothing in the capturer may wait for the PSP's GE**: through the sync gate, the game waits for the Vita (dev64.2
  lowered the GT bridge to 55.7 fps by waiting for the flip copy).
- Copying ~730–850 KB per frame with the GE takes 2–4 fps away from GT at the start of the race; ~540 KB does not.
- GT enqueues part of the next frame before the flip: "the frame's copy" is not the last flip's.

## New environment: build in the Mac's VM (`tools/mac/`)
- `sh tools/mac/preparar_vm.sh` (~2 min, in `$HOME/tc` of the VM, deleted with the session): pspdev ARM64 + glibc 2.39
  from conda-forge with wrappers (patchelf broke cc1); vitasdk "core" aarch64-linux-gnu from vitasdk/autobuilds; GCC 15
  with `-std=gnu17` (because of taihen.h). Then `sh tools/mac/compilar_vm.sh [nombre]` builds and copies to Downloads,
  `vitabridge/enviar` and the lab.
- The VM has no sudo nor Alpine/Debian/Ubuntu ports (proxy); it does have GitHub, pypi, conda-forge and archive.ubuntu.com.
- Git on the Mac: no author configured (use `-c user.name=Maty-Espinoza -c user.email=...`) and no credentials to push.
  Delete permission in `Documents/GitHub` in every session (git deletes its `.lock` files).
- Relay: the repo's version is preferable (`python3 ~/Documents/GitHub/NativePatch/tools/vitabridge.py`); the one in
  Downloads is older.

## What is left in stage 2 (agreed order)
1. ~~Point 6 + stable mirror~~ (done).
2. GXM recording on 2–3 fixed cores (on the Vita side), before GT in stage 4 (~16,500 draws per frame).
3. T2 (black box) last. Ridge Racer was not tested in stage 2.
4. Before drawing (stage 3): the DISTINTAS, register MIRROR2 for the GPU, repeat the TRANSFERs to the VRAM on the Vita.

## Working rules in force (from the user)
- Test versions `.x` (next: GePatch dev65.x / gepresent v13.7.x); commit only with the confirmed version.
- When asking for tests: which game, what to do and what to look at; options with the menu name. Simple summary after
  every change. Logs of each run in `vitabridge/<etapa>_<versión>/<juego><n>/`.
