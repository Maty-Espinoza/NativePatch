# Adrenaline: how it works and what NativePatch uses

What you need to know about Adrenaline (the one that runs PSP games on the Vita) to understand where NativeLink and
NativeDraw hook in. Sources: Adrenaline 8 code (isage/Adrenaline, commit c8475f5, reviewed on 2026-10-29) and
Adrenaline 7 (TheOfficialFloW/Adrenaline, commit 7d382b7, reviewed on 2026-10-21). How NativePatch uses it:
`how-it-works.md`.

## Adrenaline 8: how it loads plugins (isage/Adrenaline code, commit c8475f5, reviewed on 2026-10-29)
- **PSP plugins:** `cef/core/systemctrl/src/plugin.c` (`loadPlugins`) reads `ms0:/seplugins/plugins.txt`
  (= `ux0:pspemu/seplugins/plugins.txt`), or **`EPIplugins.txt` instead if it exists**. One line per plugin:
  `<runlevel>, <path>, <on|off>` (e.g. `game, ms0:/seplugins/nativelink.prx, on`); `#`, `;` and `//` are comments.
  `game` = games; there are also `vsh` and `pops`. A path without `:` is taken as relative to `ms0:/seplugins/`.
- **`game.txt`/`vsh.txt` are no longer read when loading plugins**: only recovery mode converts them once to
  `plugins.txt` (`cef/core/recovery/src/plugins.c`). Old documents that say "enable in `game.txt`" are from
  Adrenaline 7.
- The XMB plugin manager (`cef/core/xmbctrl/src/plugins.c`) reads and rewrites the same file: the user can
  turn NativeLink on or off from the XMB.
- Holding **L** when opening a game disables all plugins (`g_disable_plugins`).
- **Vita plugins (taiHEN):** `ux0:tai/config.txt` if it exists; otherwise `ur0:tai/config.txt`. The `*PSPEMUCFW` section
  loads plugins into the Adrenaline process (this is where NativeDraw goes). A change in `config.txt` needs a Vita restart
  (or refreshing taiHEN from the HENkaku settings).
- The installer (`instalador/main.c`) follows these rules.

## Adrenaline 8: how it draws the screen (`user/menu.c`, function `AdrenalineDraw`)
- **PSP games:** on every loop it copies the PSP image (0x74000000, 512x272 8888) to its `psp_tex` texture with
  `sceDmacMemcpy` and draws it with vita2d centered, with the scale from its menu (`psp_screen_scale`, 2.0 = full
  screen). NativeDraw hooks exactly this: it recognizes `psp_tex` by its size and replaces it with its double-size surface.
- **PS1 games:** the image comes from the Vita's PS1 emulator in another texture (`pops_tex`, 1024x544, already in 4:3 and with
  the menu's PS1 scale). It does not go through the GE: **NativeLink does not see it and NativeDraw does not touch it.**
- **"Original" filter** (or direct "sharp" scaling at 2x): Adrenaline does not draw with GXM and the presenter cannot
  act. To use NativeDraw you have to choose a filter that uses GXM (Bilinear).
- **Aspect ratio (4:3 / 16:9):** since the NativeDraw surface is exactly double the size of `psp_tex`, Adrenaline places and
  scales it the same way, so its scale, its position and the game's aspect ratio are respected.
- The compiled shaders Adrenaline uses (vita2d, from vita-shader-collection) are the same ones NativeDraw uses
  for the presenter (`nativedraw/sombreadores.h`).

## Architecture (Adrenaline 7, TheOfficialFloW/Adrenaline code, commit 7d382b7)
- The Vita carries the PSP hardware inside its chip: MIPS Allegrex CPU and GE (graphics engine). The game code and NativeLink's run on that real MIPS, not in a software emulator.
- The MIPS has no direct access to the screen, the memory card, the audio or the controls. The Vita's ARM handles all of that through remote calls ("Kermit").
- **Screen:** a thread on the ARM (`user/menu.c`, `AdrenalineDraw`) draws with the Vita's GPU (vita2d):
  - Normal: it copies the PSP framebuffer (480x272, at `0x74000000` on the ARM side) and scales it.
  - If `DRAW_NATIVE` (`0xABCDEF00`) ≠ 0: it uses as texture `NATIVE_FRAMEBUFFER` = `0x0A400000` in the PSP RAM, 960x544 in 565 BGR format, at scale 1.
  - The ARM sees the PSP RAM at `0x68000000 + (addr & 0x0FFFFFFF)`.
  - Reading the native buffer is not synchronized with the game's flips: there can be tearing if the GE writes while the Vita reads.
- **CPU:** Adrenaline sets 333 MHz by default. With the "CPU speed for ISO/UMD" option, `SetSpeed` can lower it (222 MHz, etc.).

## Memory map (PSP addresses)
| Area | Use |
|---|---|
| 0x08000000–0x0C000000 | 64 MB RAM |
| 0x08800000… | Game partition. With "Force high memory layout" it reaches 52 MB → up to 0x0BC00000 |
| 0x0A000000–0x0B000000 | 16 MB that Adrenaline clears at start-up. NativePatch uses this area |
| 0x0B000000–0x0B920000 | flash0 package (`SCE_PSPEMU_EXTRA_MEMORY`) |
| 0x0BD00000 | Scratchpad |
| 0x0BE00000 | VRAM (`SCE_PSPEMU_VRAM`) |

**What NativePatch uses inside 0x0A000000–0x0B000000:** the mailbox, the two VRAM mirrors, the GE lists
for the mirror copies and the log channel (map in `how-it-works.md`). NativeLink reserves the whole area in
partition 11 at start-up.

**Consequences:**
- "Force high memory layout" must be **off**. With 52 MB the game can use 0x0A000000 onward and would collide with
  the plugins' area (NativeLink detects it and stays off).
- The real VRAM (0x04000000) is **2 MB**, not 4: `sceGeEdramSetSize(4 MB)` fails (0x80000104).
- 0x04200000–0x043FFFFF is an alias: the GE draws there over the low 2 MB and reads see the data permuted.

## The channel between the PSP and the ARM

- The PSP fills a `SceKermitRequest` (command + arguments) and calls `sceKermitSendRequest661(req, mode, cmd, nargs, 0, &resp)`. It blocks until the response, using a semaphore.
- On the ARM, a thread waits with `ScePspemuKermitWaitAndGetRequest(mode, &req)` and answers with `ScePspemuKermitSendResponse`. Those functions are at fixed offsets in the ScePspemu module (`user/main.c`, `GetFunctions`).
- Adrenaline uses `KERMIT_MODE_EXTRA_2` for its commands. `KERMIT_MODE_EXTRA_1` is defined but not used (NativePatch does not need it: it communicates through shared memory).
- The ARM reads and writes the PSP RAM at `0x68000000 + addr`. To avoid reading stale data from its cache it uses `ScePspemuConvertAddress(addr, KERMIT_INPUT_MODE, size)` and `ScePspemuWritebackCache` when writing. On the PSP side the cache has to be written back to memory before making a request.

### Logs and other costs
- Writing files from the PSP is done by the Vita: `sceIoWrite` to `ms0:` travels through Kermit (`KERMIT_MODE_MSFS`) to the
  ARM, which writes to ux0 (`ScePspemuRemoteMsfs`). NativeLink avoids even that: it leaves the text in the shared area and
  NativeDraw writes it to the card (`nativelink/log.h`).
- What remains on the PSP is formatting the text (vsnprintf): a few lines every 10 s. Negligible cost.
- There is no second MIPS core available: the PSP's ME is handled by the ARM (`KERMIT_MODE_ME`).
