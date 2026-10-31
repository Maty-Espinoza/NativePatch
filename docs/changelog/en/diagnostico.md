# GePatch diagnostics: plan and binary dump (.gpd)

Written on 2026-10-21, after dev27. Reason: the Silent Hill fixes (form, spinner, door) were being made blind, with incomplete text captures and verbal descriptions of the image.

## What information was missing
1. **Reference:** what should be visible at each step of the frame (form, spinner, door). Only the user's description existed.
2. **Actual result of the plugin:** images of the big buffer, the render targets, the temporary buffer and the textures the game builds with copies (for example, the form texture at 097f6960).
3. **The complete frame:**
   - The text capture is cut at the flip, but Silent Hill keeps walking the same list after the flip.
   - The capture only records "interesting" state changes and does not save the data.
4. **Data of each draw:** vertices before and after doubling them, indices, textures, palette (CLUT) and origin of the copies.
5. **Plugin decisions per draw and per fb**, together with the position in the frame of each log line.
6. **Initial state of the frame:** real GE registers (`sceGeGetCmd`), matrices (`sceGeGetMtx`) and VRAM.

## How to tell an error from the normal flow
- **Two dumps of the same point in the game:**
  - One **passive** (L at start-up): the game runs unpatched and is the reference.
  - One **active**: with the plugin.
- `gpd.py comparar` aligns the draws of both dumps, lists the draws the plugin treated differently and compares images (copy destinations, screen).
- **Criterion:** a log event is only an error if it coincides with an image difference against the reference.
- **Reference PPSSPP:**
  - `gpd.py ppsspp` converts the passive dump to `.ppdmp`. Verified: PPSSPPHeadless with `--graphics=software` draws the form frame the same as the Vita.
  - With `--plugin --hasta=N --mostrar=ADDR,ANCHO,FMT`, PPSSPP runs what the plugin sent, without its own lists.
- **Snapshots (SNAP):** the plugin's lists copy intermediate buffers to RAM with the GE. They are used to separate at which step something breaks.

## .gpd format (dump.h / dump.c)
- File `ms0:/gepatch_<ID>_<frame>.gpd`. It is generated with L+R+SELECT or with the menu option "Volcar el proximo frame (.gpd)".
- **Start and end:** it starts at the next enqueued list and ends at the first new list after a flip.
- **Structure:** records `[u32 type][u32 bytes][data]`.
- **Record types:**
  - HEADER, REGS (start and end).
  - MEM:
    - Real VRAM at the start and at the end.
    - Fake VRAM at the start and at the end.
    - Destinations of the copies to RAM.
  - LIST, CMDS, DRAW, BLOB, EVENT, FLIP, FB, TXLIST, END, SNAP.
- **dev28 errors:**
  - Threads writing at the same time.
  - Indices out of order.
  - CLUT not repeated.

  All fixed. `gpd.py` also reads the damaged dev28 files.

## Bridge to the console (2026-10-21)
- **On the Vita:** vitacompanion 1.06 with FTP on port 1337 and commands on 1338 (`launch`, `kill`, `press`, `release`, `wait`, `screen`, `reboot`).
- **On the Mac:** `tools/vitabridge.py`, running in the user's Terminal. It finds the Vita on the network by itself (the user's is at 192.168.0.15).
- **Shared folder:** `~/Downloads/vitabridge`:
  - `enviar/` → uploaded to the Vita.
  - `cmd_*.txt` → sent to port 1338.
  - `bajar_*.txt` → downloads those paths.
  - `recibidos/` → logs, `.gpd` and downloaded files.
  - `estado.txt` → heartbeat.
- **Tested:**
  - Uploading the plugin works.
  - The commands work.
  - A simulated L+R+SELECT with `press l1;press r1;press select;wait 2s;release all` reaches Adrenaline and triggers the dump.
- **Limit:** Claude has no access to the local network (isolated VM; the proxy rejects 192.168.x.x), so the relay has to be run by the user.

## Key finding: the PSP VRAM on the Vita is 2 MB (dumps 8675 and 005349)
- `sceGeEdramSetSize(4 MB)` returns 0x80000104: there is no extra VRAM.
- **0x04200000–0x043FFFFF is a mirror** of the low 2 MB:
  - What the GE **draws** there lands linearly at `address − 2 MB`: 0x042xxxxx overwrites the big buffer and 0x043xxxxx overwrites the z-buffer.
  - What is **read** there (copies, textures, CPU) sees a permuted mirror (`address ^ 0x8040` in the temporary buffer 0x04310000 ↔ 0x04110000).
- **Consequence:** all the previous render targets and the temporary buffer overlapped with the big buffer or the z-buffer:
  - SH form with scrambled letters;
  - background glitches with "Intermediate fbs";
  - probably GT reflections and mirrors.
- dev31 (z-buffer at 0x04200000) made everything worse because the z-buffer was written on top of the big buffer. That confirmed the diagnosis.
- The real 2 MB are full: big buffer 1020 KB + z-buffer 1020 KB. **There is no VRAM left for render targets.**
- **dev32:**
  - probeVram does a draw test; if it detects the mirror, it turns off the render targets.
  - The temporary buffer (screen copies, form) uses the z-buffer area (0x04100000), which is free during the copy.
  - "Intermediate fbs = 1" was removed by default for SH.
- **dev33 (confirmed on the console, commit e4530d5):** the downscale did not write alpha. On the PSP GE the destination alpha is only written through the stencil; now the downscale uses stencil REPLACE 0xff and then restores the game's. Before, the form copy was left with z-buffer leftovers in the alpha (0/92/105/106) and the game drew it semi-transparent. Dump 4830: alpha 255 and clean form.
- **Open:** how to do the intermediate fbs (SH door, 0a170000; GT reflections) without free VRAM. Options:
  - borrow the z-buffer when the pass does not use depth;
  - draw at scale 1 in the fake VRAM (RAM);
  - copy to RAM.

## Other findings in SH
### Form
- The original texture is opaque (alpha 255), so transparency was not the cause.
- The doubled text in the big buffer is perfect (snapshot "screen before copy"). It broke when passing through the temporary buffer in the nonexistent VRAM (dev32) and through the unwritten alpha (dev33). **Solved.**

### Spinner
- It is drawn by the CPU in the displayed fb (0a0d4000, 1024 wide).
- Since dev29 it is pasted on top of the big buffer and then that area is cleared. Pending confirmation.

### Scale 1 copy fb (dev27)
It was left as an option turned off: the frames that do not copy lost draws.

## dev45 (2026-10-22): dev44 dumps of RR 1922 and SH 6442
### Ridge Racer: purple stripes across the whole screen
- **Cause (confirmed in the dump):** the blur draws the render target 0a088000 (scale 1, in the z VRAM) reading from itself. `rt1SelfTex` patched the **game's** TEXADDR/TEXBUFWIDTH words (08bce218/21c) to 0x04100000. The next draw, the full-screen glow on the big buffer (#2321, additive blending 0xaa), inherited that texture and added the z-buffer read as 565 → purple stripes.
- **Fix:** `rt1SelfDraw`. Only that draw becomes BASE + CALL to a plugin list: TEXSYNC, texture in the z VRAM, the draw and, at the end, the texture in the fake VRAM (where the rt is left when closing it). The game's words are not touched. There is a new test (171 checks).

### Silent Hill: the car without textures for a few seconds
- **Cause:** in dev44, if the blending of pass 0a170000 could not be swapped, the pass stayed out for 10 s. In the log: it failed at frame 6185 (at the start of the scene) and came back at 6785. That pass draws the car (textures 08bb3600/08bb4780…).
- **Fix:**
  - The wait grows 2, 4, 8… frames for each consecutive failure (up to 10 s) and resets when the blending works.
  - A copy with black color (fade from black) is no longer confused with "not a copy" (`fullCopy`).
  - The reason for the failure is left in the log ("intercambio: la mezcla no se puede hacer: …") with auto-capture.

### Open
- SH purple squares: they do not appear in the dumps. The spinner that the CPU pastes (440,232) looks fine in fb 0a0d4000. A capture is needed.

## dev46–dev48 (2026-10-23)
### Silent Hill: purple squares (dev46, confirmed in the lab)
- **Capture:** lab dump 10518, with the rectangles at (0,0)–(240,136) and (256,16)–(376,84).
- **Cause:** the light pass is drawn by swap in the big buffer. Two draws (#403 and #404) read the pass itself as a texture, at the game address 0x04170000; on the Vita that address is the z-buffer.
- **Fix:** texmode 9. The screen that reads itself takes the texture from the big buffer at double size, in quarters (`rt1FromBig`, now with float positions and textures and up to 12 vertices).

### Ridge Racer: road that is not drawn (dev47–dev48)
- **Cause (confirmed in the model):** the GE discards a whole triangle if one vertex falls outside 0..4096. At double size, the off-screen margin drops from ~1800 to ~780 game px. Replaying the original frame with only the viewport x2 in PPSSPPHeadless gives the same water wedge.
- **dev47:** subdivision into 4. It was not enough: the piece with the vertex that goes out never enters the range. Also, the asphalt extends behind the camera; at 1x the GE clips it at the near plane, but at double size the clip points fall outside.
- **dev48:** exact clipping (Sutherland–Hodgman) against the near plane (`z >= -w`, with the game's depth clip), `w > 0` and the safe range 128..3968. At the exact edge the GE also discarded, because it computes with 24-bit floats. It was first tried with a Python simulation on dump 3624, and the road comes back.
- **Pending:** the rearview mirror (third-person camera) was left empty with dev48. Do not solve yet.

## dev49 (2026-10-24): tools and cleanup
- 3-frame dump and `gpd.py linaje / diff / vram` (see `estrategia_diagnostico.md`).
- **Code removed** (never activated on the Vita, or replaced):
  - double-size render targets in the high VRAM;
  - intermediate fbs as render target;
  - scale 1 copy fb (csrc);
  - downscaled copy to the displayed fb (`tex_fb` = 2);
  - old list walk and NOP 0;
  - 4 MB VRAM test.
- The `.prx` went down from 142 KB to 117 KB and the test scene gives the same in the model.

## Viability
- **With the addon alone it is still viable.** The GE is real hardware; the real limit is the 2 MB VRAM, which forces the use of RAM for everything that is not the big buffer and the z-buffer.
- **Recommended path:**
  1. Passive and active dumps.
  2. Automatic comparison.
  3. Headless PPSSPP as reference.
  4. Tests on the console through the relay.
- **Discarded because of cost:** emulating the GE with the Vita GPU.

## Commit rule (requested by the user)
A commit is only pushed when a confirmed error is fixed, not on every iteration.
