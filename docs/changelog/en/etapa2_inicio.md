# Getting started with stage 2 of architecture B (summary at the close of stage 1, 2026-10-28)

> **State at the close of the first conversation of stage 2: see `etapa2_avance.md`** (shadow mode + capturer,
> commit dev61 + gepresent v12.9).

Read first: `arquitectura_dibujo_en_vita.md` (the plan; §3 design, §5 budget, §6 stages), `etapa0_resultados.md`
(how much work the games bring) and `etapa1_banco_gxm.md` (how much it costs on the Vita). This document gathers what is needed
to get started.

## In simple terms
- The new idea ("B"): the PSP draws its normal small image, with no GePatch in the middle, and the Vita reads the same
  drawing instructions and draws them again in high resolution with its own graphics card.
- Stage 0 (on the PC): we counted how much each game draws per frame. GT is the heaviest (~9000 draws), RR ~1100,
  SH ~320.
- Stage 1 (on the Vita): we measured how much each draw costs the Vita (~1 µs with no changes, ~2 µs if the image
  or the program changes), that it can prepare them in another thread, that it can read the PSP's memory directly and that it can show its
  own image. It is more than enough for RR and SH. For GT there is an unconfirmed **warning**.
- Stage 2: make the Vita **read the game's real instructions while it runs** (without drawing them yet) and measure
  whether it has the time, with the game at its original speed.

## State at the close of stage 1
| What | State |
|---|---|
| GePatch on the Vita | 0.2-dev57.1 (dev57 + a Windows test line in the log; no commit, patch `vitabridge/gepatch_dev57.1.patch`) |
| gepresent on the Vita | **v9** (md5 `20f3c01d4344df5dba3bc9dc2b6b3172`) |
| `ur0:tai/config.txt` | `*PSPEMUCFW` with gepresent only (adrfsr removed; backup `vitabridge/respaldo/config_con_adrfsr.txt`) |
| adrfsr | out of `config.txt` + `ux0:data/adrfsr/off` |
| `ux0:data/gepresent/banco` | still on the Vita; v9 ignores it. Delete it with VitaShell (the relay only uploads files) |
| Repo (`dev` branch, Windows) | commit of gepresent v11.3 (bench), `tools/carga_vita.py` and the architecture `docs/`. `log.c` is still modified with no commit (Windows test line) |
| Vita build chain | is rebuilt in the assistant's container in ~5 min (`etapa1_banco_gxm.md` §5) |

## Data that stage 2 uses
- **Cost on the Vita** (stage 1, [measured]): draw with no changes ~1.0 µs; new texture (8888 or P4 + palette) ~2 µs; new
  program ~2.2 µs; texture + uniforms ~3–3.5 µs; GPU ~1.2–1.5 µs per draw even if it paints nothing; 2000 quads in 1 draw =
  40 µs of CPU and ~2 ms of GPU. Deferred context: recording costs the same, in another thread; running ~30 µs.
- **Memory** of the Adrenaline process: ~33 MB LPDDR + 4 MB CDRAM, **without** gepresent's 24 MB dump ring
  (with it: 6 + 7 MB). The capture (black box, copies) has to fit there together with what is coming.
- **Clocks:** ARM 444 MHz, GPU 222 MHz.
- **The GPU reads the PSP RAM** (0x08800000 registered with `sceGxmMapMemory`) at the same speed as its own memory.
- **Work per frame of the games** (stage 0): GT ~44,000 commands (up to 76,000), ~9000 draws, ~20 % of its
  commands and ~35 % of its vertices in the VRAM (mirror mandatory, 370–700 KB); RR ~9000 commands, ~1100 draws,
  textures in the VRAM (70–460 KB); SH ~9000 commands, ~320 draws, no mirror.
- **Walk on the ARM** (KBENCH, synthetic): 203 ns per command with prior DMA → GT 9–15 ms per frame without a sublist
  cache; RR and SH ~2 ms.

## [warning] GT
By the calculation done with synthetic quads, GT (~9000 draws, ~5300 with a new texture) might need 17–30 ms of CPU
and 11–14 ms of GPU per frame in draws alone, that is, **it might not reach 60 fps without merging draws**. **It is taken as
neither true nor false** until measured with GT's real lists. Stage 2 has to provide the data to decide:
how many real draws are left after merging consecutive ones with the same state, and whether recording on 2–3 threads with a fixed core
scales.

## What to do in stage 2 (from `arquitectura_dibujo_en_vita.md` §6)
1. **GePatch "shadow mode"** (PSP side): passive (does not touch lists, no fake VRAM or scaling) + the enqueue,
   stall, sync and flip hooks write to a mailbox for the Vita. New GePatch version (dev61.x) with a menu option.
2. **Capturer** (gepresent, Vita side, own thread): follows the list up to the stall reading the PSP RAM (0x70000000
   uncached), walks sublists, records per draw state + vertex range + texture/CLUT. It does not draw.
3. **VRAM mirror** for GT and RR: copy with the GE to RAM (on load + verification; GT's sublists per frame).
   Without the mirror the capturer cannot follow GT's lists.
4. **Telemetry**: T1 (statistics per second, including **batches that could be merged**), T2 (black box: last
   captured frames), T4 (trace of the game's calls), T6 (GE timings per list).
5. **Repeat the recording on 2–3 threads with fixed affinity** to different cores (in the bench it did not scale without affinity).

**Questions it has to answer:** does the capture reach the end before the sync in GT and SH? does the game run at its original fps
with shadow mode (vpjuego)? does GT's mirror stay up to date? how many real GT draws are left after
merging?

## Rules learned in stage 1 (apply in stage 2)
- **Do not run Vita GPU experiments with GePatch active** (bench v11.3 with GePatch at 2x shut down the whole Vita;
  cause unconfirmed). Test new things with GePatch passive or with shadow mode.
- **Do not leave experiments installed between tests**: the bench ran by itself every time Adrenaline was opened. Activate
  by file and remove it when done; before starting, check that the 512x272 texture is visible (without GePatch).
- **Deferred-context memory:** do not recycle it while the context holds it (v11.2 crashed the GPU).
- When changing `config.txt`, restart with `cmd_reinicio.txt` (`reboot`); to close Adrenaline remotely,
  `cmd_cerrar.txt` (`destroy`). If the Vita shuts down abruptly, it asks for date and time when turned on (no data is lost).
- The Vita's plugins are updated by closing Adrenaline from the LiveArea and opening it.
- Logs: save each run in `vitabridge/<etapa>_<versión>/` (the log is replaced or appended to).
- When asking for tests: name the options as in the Adrenaline / plugin menu and say which game to test with each one.

## Inherited pending items (do not block stage 2)
- Black screen during the bench presentation (the game's texture arrived black right in those 10 s; without the bench
  GT's cutscene looks fine). Postponed by the user.
- GePatch/NativePatch license before copying code from PPSSPP.
- `ambientes_de_prueba.md` still describes dev56/gepresent v6–v9 in several tables (current state: this document).
