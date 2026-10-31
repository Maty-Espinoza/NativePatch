# Conversation summary of 2026-10-27/28 (f): FSR as an independent plugin (adrfsr) and GePatch dev60.2

Separate conversation from the dev58/dev59 line (summaries c, d, e). It starts from the theoretical analysis in
`analisis_escalado_frame_vita.md` (§1–§9) and from the FSR simulation on captures (§9).

## State at close (2026-10-28 08:18)
- **User's decision:** GePatch is the real scaling; FSR is a separate patch (fast upscaling of the original image).
  **They are not mixed**: FSR was removed from GePatch.
- **GePatch 0.2-dev60.2, no commit** = **dev59.13** (another line, no commit; base dev57 `2c4e937`) + a single change: **passive
  mode is activated again only with L** when opening the game (dev54 had added SELECT; the user asked to go back to the original).
  On Adrenaline 8, holding L when opening the game disables all PSP plugins: in practice L = GePatch does not load.
  Patch against dev57: `vitabridge/gepatch_dev60.2.patch` (md5 `2f36396d…`). `.prx` md5 `b20d9867e9816906f19ce80d1e95674c`,
  installed with `instalar.sh 0.2-dev60.2` (Downloads, Vita, lab, `versiones/`). Tests: 205 checks, 0 failures.
- **dev60.1 discarded** (FSR in the scale selector: "+ FSR" variants, "FSR only", START when opening, FSR mark in the
  descriptor). It remains as a reference in `vitabridge/gepatch_dev60.1.patch`. If dev59 continues in another conversation, the only
  dev60.2 change to carry over is the passive key.
- **adrfsr 3b** on the Vita (`ux0:data/adrfsr/adrfsr.suprx` md5 `29e32f09f65b93ddad9d3df1425678f4`, verified in `enviados/`;
  copy `vitabridge/adrfsr/adrfsr_3b.suprx`; source `tools/adrfsr/adrfsr.c` in the Mac's clone, no commit): **independent
  of GePatch**. If the image is already 960 wide or more (GePatch at 2x) it does not touch it. It is a **Vita plugin** (not a PSP one):
  listed in `*PSPEMUCFW` of `ur0:tai/config.txt` (the config downloaded from the Vita = the uploaded one; backup of the previous one in
  `vitabridge/adrfsr/config_antes_adrfsr.txt`).
- **adrfsr 1, 2 and 3 never loaded**: the taiHEN library was built by hand with wrong NIDs (taiHEN's are not the SHA1 of the
  name: library `taihen` = 0x203D1A28, `taiHookFunctionImportForUser` = 0x53636E5D). That is why there was no adrfsr log (the
  relay does download from `ux0:/data/`: tested with `fsr.cg`). 3b = the same code as 3 linked with the official taiHEN package.
  The same would have happened with gepresent v10 (discarded untested).
- **gepresent v9** (v10 with FSR inside was discarded).
- **Nothing FSR-related has been tested on the Vita yet** (the user has not tried adrfsr 3b).

## What the GT test with dev60.1 showed (log `recibidos/ge_patch.log`, 08:00)
- The user chose "FSR only" in the menu in the middle of the session (frame 2460) and did not reopen the game (nor save): the scale is only
  applied when the game is opened, so **GePatch stayed active at 2x**. The errors they saw were GePatch's at 2x, not FSR's.
- Lesson: an option that only changes when the game is reopened, mixed with options that change instantly, causes confusion. With FSR
  separate from GePatch the problem disappears.

## What was done in the conversation
1. Theoretical analysis (`analisis_escalado_frame_vita.md`).
2. FSR simulation on the Mac (`coleccion/fsr_sim.py`, `coleccion/fsr/<scene>/`). No visual verdict from the user.
3. gepresent v10 (FSR inside gepresent): discarded.
4. **adrfsr** (Vita plugin, FSR 1.0 EASU), shader compiled on the PC.
5. FSR as an Adrenaline filter: prepared in `tools/adrfsr/filtro_adrenaline/`, Adrenaline not compiled.
6. dev60.1 (FSR in GePatch's scale): discarded. dev60.2 (passive with L).

## adrfsr (simple summary)
- Adrenaline draws the game's image as a texture stretched over the whole screen with its filter's shader. adrfsr
  intercepts three steps of that draw (vertex program, texture, draw) and, if the texture is the game's, swaps the
  shader for FSR right before drawing. Cost to the PSP: zero.
- Game image: the 512x272 texture (the PSP's normal screen, visible 480x272) or, with GePatch at scale < 2, the texture
  over the PSP's RAM (visible according to GePatch's descriptor). Reads cropped to the visible part.
- **Intended use: GePatch off** (open the game with L, or `ge_patch.prx` set to `off` in `plugins.txt`) and **Adrenaline
  filter set to "Bilinear"** (on "Original" without GePatch Adrenaline does not draw with the GPU and adrfsr has nowhere to act).
- `ux0:data/adrfsr/off` turns it off (close and open Adrenaline); `ux0:data/adrfsr/fsr.gxp` replaces the included shader;
  log in `ux0:data/adrfsr/adrfsr.log`.
- It has no shortcut to turn it on/off in the game (it was offered to the user; they choose the buttons if they want it).

## FSR as an Adrenaline filter (research)
- Adrenaline 8 uses `libvitashaders.a` from VitaArchive/vita-shader-collection (its CI compiles the `.cg` files with Sony's
  compiler under qemu). Adding the filter = PR to vita-shader-collection (`fsr_v.cg`, `fsr_f.cg`, ready and they compile) + PR to
  isage/Adrenaline (`adrenaline_fsr.patch`, option "AMD FSR 1.0", ~10 lines). Compatible licenses (MIT / GPLv3).
- With GePatch at scale < 2 that filter would receive a wrong `IN.texture_size` (gepresent hooks GetWidth). Without GePatch
  (the intended use) there is no problem.

## Build chain in the container
- **Mac ↔ container files: `device_stage_files` (Mac → `/mnt/user-data/uploads/`) and `device_commit_files` (from
  `/mnt/user-data/outputs/` → Mac).** Note: when re-sending a file with the **same name** in `outputs/`,
  `device_commit_files` delivered the previous version (it happened with `ge_patch.prx`): use a new name per version and verify
  the md5 on the Mac.
- GePatch: `git clone -b dev https://github.com/Maty-Espinoza/NativePatch` (anonymous read) + `git apply` of the patch;
  pspdev from `https://github.com/pspdev/pspdev/releases/latest/download/pspdev-ubuntu-latest-x86_64.tar.gz` +
  `TheOfficialFloW/Adrenaline` (`make ADRENALINE=.../cef`); tests: `apt-get install gcc-multilib`, `make` in `tests/`.
- Vita: `apt-get install gcc-arm-none-eabi cmake libzip-dev libyaml-dev qemu-user-static gcc-arm-linux-gnueabihf
  libc6-dev-armhf-cross`; libelf 0.8.13 from `http://archive.ubuntu.com/ubuntu/pool/main/libe/libelf/libelf_0.8.13.orig.tar.gz`
  (`--prefix=deps --disable-shared`, `ln -s . deps/include/libelf/libelf`); vita-toolchain (`git submodule update --init`,
  `PKG_CONFIG_PATH=deps/lib/pkgconfig cmake .. -DUSE_BUNDLED_ENDIAN_H=ON`); stubs `vita-libs-gen-2 -yml=vita-headers/db/360`.
- **taiHEN: use the official package** `https://github.com/vitasdk/packages/releases/download/master/taihen.tar.xz` (it downloads fine;
  `include/taihen.h`, `lib/libtaihen_stub.a`). **Do not generate the stubs by hand**: the plugin does not load and gives no message.
  Verify: `arm-none-eabi-objdump -s -j .vitalink.fstubs.taihen x.elf` must show `281a3d20 5d6e6353`.
- Cg shaders → gxp: `JagerDesu/vita-shaders`, in `resolve.c` `memmem(start, -1,` → `memmem(start, 0x300000,`; `make
  CC=arm-linux-gnueabihf-gcc`; `qemu-arm-static -B 0x100000000 -L /usr/arm-linux-gnueabihf ./shacc --fragment x.cg x.gxp`.
- adrfsr: `VH=tc/vita-headers STUBS=tc/stubs TAI=tc/taipkg SHACC=tc/vita-shaders sh build.sh` in `tools/adrfsr`.

## What remains to be tested (in Adrenaline's menu, "Graphics Filtering" = Bilinear)
1. Close Adrenaline from the LiveArea and open it (loads adrfsr 3b; config.txt already lists it).
2. **Gran Turismo opened with L held** (GePatch does not load): does it look sharper than Bilinear alone? Any errors?
3. Compare without FSR: upload `ux0:data/adrfsr/off`, close and open Adrenaline, same game with L.
4. Ridge Racer the same.
5. Download adrfsr's log (`bajar_*.txt` with `ux0:/data/adrfsr/adrfsr.log`): it should say "ganchos …" and "primer draw con FSR".
   **Only when the user says they tested** (they asked not to look for logs beforehand).
6. Afterwards: RCAS (sharpening), in-game shortcut if the user wants it, PR to Adrenaline.

## Where everything is
| What | Where |
|---|---|
| GePatch dev60.2 | `vitabridge/gepatch_dev60.2.patch` (against dev57; contains dev59.13) |
| GePatch dev60.1 (discarded) | `vitabridge/gepatch_dev60.1.patch` |
| adrfsr code | Mac clone `tools/adrfsr/` (no commit) |
| Adrenaline filter | `tools/adrfsr/filtro_adrenaline/` |
| adrfsr binaries | `vitabridge/adrfsr/adrfsr_3b.suprx` (1, 2 and 3 do not load); on the Vita `ux0:data/adrfsr/` |
| config.txt | `vitabridge/adrfsr/config_adrfsr.txt` (current) and `config_antes_adrfsr.txt` |
| FSR simulation | `vitabridge/coleccion/fsr_sim.py`, `coleccion/fsr/` |
| Analysis | `claude/analisis_escalado_frame_vita.md` |
