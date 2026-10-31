# Lab on the Mac: running games with GePatch and monitoring them (feasibility)

Analysis of 2026-10-22, at the user's request. Base: the PPSSPP-Adrenaline model (`modelo_adrenaline.md`), which already reproduces the Vita byte for byte in the GEPVM scene.

## Conclusion: feasible, almost everything already exists in PPSSPP

PPSSPP (upstream, the same commit as the patch) already includes:
- **Version with an interface for macOS** (SDL) with an ImGui debugger: step-by-step GE debugger, framebuffers, textures, memory viewer, threads.
- **WebSocket debugging API** (localhost): `memory.read/write`, `gpu.buffer.screenshot`, `gpu.record.dump` (.ppdmp dump of the frame), `gpu.stats.get/feed`, `game.state.save/load`, `input.buttons.send`, `cpu.stepping/breakpoint`, `hle.thread.list`, `hle.module.list`, `memory.info.*`.
- **Input recording and replay** (`replay.begin/flush/execute`): buttons and analog stick per frame, plus the results of file operations. Together with a savestate at the start, it allows replaying a session deterministically.

The Adrenaline patch is in `Core/`, so it also applies to the version with an interface.

## Proposed architecture

1. **Lab** (macOS): PPSSPP-Adrenaline with an interface and GePatch loaded from `seplugins/game.txt`, as on the Vita. The ISOs are downloaded once from the Vita through the relay (FTP) and stay on the Mac.
2. **Native image in the window:** when DRAW_NATIVE ≠ 0, the window shows the 960x544 at 0x0A400000 (the same as the Vita). It is a small change in the presentation of the software renderer.
3. **Monitor** (Python script like `vitabridge.py`): it connects to the WebSocket and writes to `~/Downloads/gepatch-lab/sesion-N/`:
   - `frames.jsonl` per frame: fps, emulated time, GE draws/lists, GePatch internal statistics (`st` block published at a fixed address; small change in the plugin), free memory per partition and VRAM usage.
   - `ge_patch.log` (the plugin already writes it on the Mac's memstick).
   - periodic and on-demand native screenshots.
4. **Control by Claude:** as with the Vita, `cmd_*.txt` files in the shared folder: savestate, screenshot, .ppdmp dump of the frame, read memory, pause/advance, start/stop input recording. Claude reads results through the connection to the Mac (polling every few seconds: it is not streaming, but it is almost live).
5. **Inputs and regression:** each session saves an initial savestate + replay. A headless version on the Mac replays the session with another version of GePatch and compares the images (like `comparar_vm.py`). The .ppdmp dumps (a few MB) can also be replayed in Claude's environment; the ISOs cannot (400 MB per file limit, and they stay on the Mac).

## Limits

- **Timing:** it is not the Vita's cycle by cycle. The fps and the cost of the plugin are measured in emulated time; they are useful to compare versions, not to predict the fps on the console. What is exact is the image and the order of the GE commands.
- **Speed:** to be exact, the IR interpreter and the software renderer are used. On a Mac with Apple Silicon the games are estimated to run slower than real time (it has to be measured). A fast mode with JIT can be offered, which stops being exact in the VRAM mirrors.
- **Adrenaline failures** (c1-2858-3, crashes): they are only reported, not reproduced.
- **Requirements on the Mac:** Xcode Command Line Tools, Homebrew (cmake, pkgconf, sdl3, sdl3_ttf; optional fontconfig and fribidi: the patch's PPSSPP uses SDL3) and running a build script once (~15 min).

## Phases

1. Build on the Mac + native image in the window + run SH and RR (1 session).
2. WebSocket monitor, telemetry and Claude's commands (1 session).
3. Input recording/replay and regression runs between GePatch versions (1 session).
4. Optional: "GePatch" ImGui window with the plugin state (render targets, swap, log).

## Status (2026-10-22)
- Scripts on the Mac: `~/Documents/gepatch-lab/compilar.sh` (installs dependencies with brew, clones PPSSPP at 1adf3c5, applies `adrenaline.patch`, `./b.sh --release` → `ppsspp/build/PPSSPPSDL.app`) and `jugar.sh` (`PPSSPP_ADRENALINE=1`, own memstick with `seplugins/game.txt` and `ge_patch.prx`, software renderer).
- Built and working (2026-10-23): Silent Hill and Ridge Racer run the same as on the Vita, apart from some performance. The first attempt had failed with `SDL3_ttf not found` (the script asked for SDL2).
- `jugar.sh` opens with `--debugger=45678` (PPSSPP WebSocket API).
- The dumps (L+R+SELECT) and the log stay in `memstick/`.
- External controllers: PPSSPP SDL already supports them; the replay records the PSP's buttons, not the controller.
