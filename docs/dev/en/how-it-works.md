# How NativeLink and NativeDraw work

Reference for the current version (NativeLink 0.0.3, NativeDraw 0.0.8.x). For the general idea and the project status:
`overview.md`. Roadmap: `architecture-and-roadmap.md` §6. Logs and switches: `logs-and-switches.md`. What was measured
and what limits were found: `findings.md`. How each piece was reached, version by version: `../../changelog/`.

## In simple terms
- The PS Vita has PSP hardware inside. Adrenaline runs PSP games on that hardware: the PSP's processor runs the game and
  its graphics chip (the **GE**) draws the usual small image (480x272).
- **NativeLink** is a plugin that runs on the PSP, next to the game. It changes nothing of what the game draws: every time
  the game sends the GE a list of drawing instructions, NativeLink notes it in a "notebook" (the **mailbox**) that
  lives in a memory the Vita can also read.
- **NativeDraw** is a Vita plugin. It reads the notebook and walks the same instructions from the Vita (the
  **capturer**). It saves what it needs to draw each frame (the **frame record**) and draws it again with
  the Vita's GPU at double size (the **translator**). The resulting image replaces the PSP's on the screen
  (the **presenter**).
- The Vita cannot see the PSP's video memory (VRAM), and some games keep instructions and textures there: on every
  frame NativeLink asks the GE to copy to normal memory the pieces the Vita asked for (the **mirror**).
- If the Vita falls behind, NativeLink makes the game wait a little (up to 5 ms) before it reuses a list, so the
  Vita does not read instructions that have already changed (the **gate**).
- If a frame has something the Vita does not know how to draw yet, the game's normal image is shown: never worse than
  without the plugin.

## Pieces and where each one is
| Piece | File | Runs on |
|---|---|---|
| GE hooks, mailbox, mirror, gate, telemetry | `nativelink/main.c` | PSP (MIPS), inside the game |
| NativeLink log (the text is written to the card by NativeDraw) | `nativelink/log.c`, `log.h` | PSP |
| The GE's own lists (mirror copies) | `nativelink/gu.c` | PSP |
| Start-up, NativeLink log, emulator threads | `nativedraw/nativedraw.c` | Vita (ARM), Adrenaline process |
| Capturer, readers, sublist memory, mirror requests, capture logs | `nativedraw/captura.c` | Vita |
| Frame record (what each frame needs to be drawn) | `nativedraw/registro.c`, `registro.h` | Vita (inside `captura.c`) |
| Presenter: takes Adrenaline's screen, L+R+START menu | `nativedraw/presentador.c` | Vita, Adrenaline's drawing thread |
| Translator: draws the record with the Vita's GPU | `nativedraw/traductor.c`, `sombreadores/` | Vita (inside `presentador.c`) |
| GPU timer, dumps, dump replay | `cronometro.c`, `volcado.c`, `reproduccion.c` | Vita (inside `presentador.c`) |
| Test of the capturer off the Vita (qemu) | `nativedraw/prueba_memoria.c` | PC |
| Installer (`NativePatch.vpk`) | `instalador/main.c` | Vita (app) |
| PC tools: relay with the Vita, log summaries, dump drawing | `tools/` | PC |

## How they are loaded
- **NativeLink:** Adrenaline 8 loads it when a game is opened if it is enabled in `ux0:pspemu/seplugins/plugins.txt`
  (`game, ms0:/seplugins/nativelink.prx, on`; see `adrenaline.md`). Opening the game while holding **L** or SELECT leaves it
  off (with L, Adrenaline 8 also turns off all plugins). Hold **R** when opening: synchronous log (every line at once,
  for hangs).
- **NativeDraw:** taiHEN loads it in the Adrenaline process (`*PSPEMUCFW` section of `config.txt`), when Adrenaline
  is opened. For a new version to load, Adrenaline has to be closed. A change in `config.txt` requires restarting
  the Vita.
- If `ux0:data/nativedraw/off` exists, NativeDraw does nothing. Without `ux0:data/nativedraw/dibujar`, NativeDraw captures and
  measures but only replaces the image while the L+R+START menu is open.

## Shared memory (PSP area 0x0A000000–0x0B000000)
Adrenaline clears that area when the game starts and NativeLink reserves it in partition 11 (`zoneCheck`); if the game
occupies it, NativeLink stays off. The Vita sees it at `0x68000000 + (address & 0x0FFFFFFF)` (uncached).

| Address | What |
|---|---|
| 0x0A000000 | Mailbox (`SB`, see below) |
| 0x0A100000 | Two 8 KB GE lists for the mirror copies (alternating) |
| 0x0A200000 | VRAM mirror 1 (`MIRROR`, 2 MB: even copies) |
| 0x0A7FA000 | NativeLink log text (16 KB) |
| 0x0A7FF000 | Log descriptor: +0 "GPV1" (NativeLink), +0x20 "LOGV" (NativeDraw), +0x40 seq, done, bytes |
| 0x0AC00000 | VRAM mirror 2 (`MIRROR2`, 2 MB: odd copies) |

### Mailbox (`SB`, 32-bit words)
| Word | Who writes | What |
|---|---|---|
| 0 | PSP | "SOMB" (restored on every flip if something erased it) |
| 1 | PSP | mailbox version (5) |
| 2 | PSP | events written |
| 3 | Vita | last list captured (sequence number) |
| 4 | Vita | heartbeat |
| 5 | Vita | events read |
| 6 | PSP | lists enqueued (sequence number) |
| 0x20–0x2F / 0x30–0x3F | Vita | 4 KB VRAM pages requested: "lists" / "data" (bitmaps) |
| 0x40 | GE | last mirror copy finished |
| 0x44, 0x45, 0x46, 0x49 | PSP | copy requested, pages copied, times the GE was still busy, times the Vita was using that mirror |
| 0x47 | Vita | 1 = turn on T6 (`medir_ge`) |
| 0x48 | Vita | mirror copy in use (not overwritten) |
| 0x4A | Vita | switches: 1 no gate, 2 no mirror, 4 no free CPU, 8 gate at the sync, 16 full mirror (NativeDraw sets it when the record gives complete 2D frames), 32 the PSP's flip waits for the mirror copy (`espejo_espera`) |
| 0x4D | PSP | free CPU calibration |
| 0x4E | Vita | log level (0 minimum, 1 medium, 2 extreme) |
| 0x50–0x53, 0x60+, 0x80+ | Vita | sample of hashes of the last list captured (list lifetime and gate self-check) |
| 0x54 | PSP | gate mode (0 testing, 1 before enqueuing, 2 at the sync until the game is closed) |
| 0x200–0x3FF / 0x8400–0x85FF | PSP | number of the last copy of each page in each mirror |
| 0x400 + 8·k | PSP | ring of 4096 events of 32 bytes: type \| data << 8, a, b, c, PSP time, sequence, extra |

Events: 1 enqueue, 2 enqueue at the front, 3 stall, 4 DrawSync, 5 ListSync, 6 flip, 7 end of a GE segment (T6), 8 end
of a sync, 9 CPU write (T4), 10 T4 summary, 11 frame cost for the game, 12 list lifetime.

## One frame, step by step
1. The game enqueues a list (`sceGeListEnQueue`). Before letting it through, NativeLink waits for the Vita if needed
   (gate), notes the event with the list's sequence number and lets it continue.
2. The game updates the stall (how far it has written) and calls the syncs: NativeLink notes each one.
3. NativeDraw's capturer reads the event and walks the list from the PSP's RAM as the GE would (it follows jumps,
   sublists, BASE/OFFSET). It keeps the GE state, counts what would have to be drawn and, with `dibujar`, adds each draw
   to the frame record: the state, the copied vertices and indices, the palette and the texture rows the vertices use
   (copied by DMA to memory the GPU can read). When it finishes it publishes the list's number in `SB[3]`.
4. At the flip (`sceDisplaySetFrameBuf`), NativeLink notes the event, enqueues the mirror copy with the requested pages and
   writes its statistics to the log every 600 frames (~10 s). The capturer closes the frame: if the record is
   complete (all draws went through it) it publishes it; if it is missing VRAM textures, it waits for the mirror copy of
   that flip and then publishes it.
5. The presenter, hooked at the end of each Adrenaline scene, takes the last complete frame published and the translator
   draws it with the Vita's GPU on its own 1024x544 surface (960x544 visible), with the normal image
   stretched underneath (what the game does not redraw stays as on the PSP). In Adrenaline's next scene, when
   Adrenaline is about to draw the PSP's image, that surface is passed to it instead. The image appears one frame after
   the PSP's.
6. If the frame has something the translator does not know how to draw (3D, a blend with no equivalent, a texture with no copy...),
   the normal image is shown and the reason is counted in the log (`traductor:` in `nativedraw.log`).

## Vita side: threads
| Thread | Core / priority | What it does |
|---|---|---|
| capturer | any of the 3 (0x61; `nucleo_captura` pins it) | follows the mailbox, walks each list and builds the frame record |
| reader 0 and 1 | 0 and 2 (0xB0, only what is left over) | fetch ahead the 64-byte lines of the list into a shared cache and check stored sublists |
| mirror | 0 (0xBE) | passes the switches and the level to NativeLink; at extreme, measures how much the mirror changes |
| log | (0xB0) | writes `nativedraw_captura.log` every second |
| writer | (0xA0) | writes the NativeLink log to the card |
| presenter | (0xA0) | hooks Adrenaline, paints the menu, handles dumps and replay, writes `nativedraw.log` every 10 s |
| GPU timer | (0x40) | with `medir_gpu`: measures how long the GPU takes in each scene |
| Adrenaline's drawing thread | Adrenaline's own | the translator and the presenter proper run here (in the GXM hooks) |

- **Reading the PSP's RAM is the expensive part** (~2 µs per uncached line): hence the look-ahead readers and the reads of
  256 bytes at a time with NEON.
- **Sublist memory:** the capturer stores each sublist it walked (its lines and what it added). If in the next frame
  it is identical (same lines and same input state), it adds what was stored without walking it. The readers
  check it beforehand (even when the list starts, "look-ahead check"). 1 in 32 is walked anyway to
  verify that what was stored is exact (if not, the log says **DISTINTAS**: it must be 0). A sublist applied this way does not go through
  the frame record: that frame stays incomplete and goes with the normal image (which is why today the Vita draws 2D games, which
  hardly use sublists).
- **Interpreting line by line:** the checks (stall, limit, address translation) are done once per line of 16
  commands, not per command.

## PSP side: gate
- The wait for the Vita goes **before enqueuing the next list** (the game does not rewrite a list until it enqueues the
  next one). To verify this in any game, NativeLink redoes the hashes the Vita left: if the list changed
  before enqueuing the next one, the wait goes back to the sync (60 clean checks to go back; 10 changes = at the sync
  until the game is closed).
- The wait is up to 5 ms; if the Vita does not respond (heartbeat stopped for 0.5 s), it does not wait.

## Vita side: drawing
- **Frame record** (`registro.c`): 4 records that take turns (one is being written, another stays published and the ones the
  GPU may be reading are not touched). Per frame: draws, GE states (only when they change), copied vertices and indices,
  palettes and the texture rows the draws use. Textures with the PSP's "swizzle" (16-byte blocks
  x 8 rows) are left ordered by the CPU when copying them, so the GPU reads them as a common texture. Textures from RAM are
  copied right away (the game may change them); those from VRAM are copied after the flip, from the mirror copy of
  that flip.
- **Translator** (`traductor.c`): only 2D in "through" mode (coordinates in pixels): rectangles, triangles and strips;
  5650/5551/4444/8888 textures and 4- and 8-bit palettes; clears; color per vertex or from the material; modulate and replace
  texture functions; alpha test; blending (the combinations GXM has); scissor on the rectangles; **intermediate
  images** (a game draws into a hidden VRAM image and then stretches it: each one goes to its own surface at double size,
  up to 2). It first checks all the draws of the frame; if any cannot be done, it draws nothing and returns the
  reason. Its own shaders are in `nativedraw/sombreadores/*.cg`, compiled to `sombreadores_nd.h`.
- **Presenter** (`presentador.c`): hooks `sceGxmSetFragmentTexture`, `sceGxmEndScene` and `sceDmacMemcpy` of
  AdrenalineUser. It recognizes Adrenaline's 512x272 texture and replaces it with its surface, which is exactly double:
  Adrenaline places and scales it as always (its scale, position and aspect ratio are respected). The copy of the PSP's
  image that Adrenaline makes is diverted to 4 alternating copies of its own (the GPU reads it for NativeDraw later than for
  Adrenaline). Everything in LPDDR memory (never CDRAM, which the Vita's videos use) and always with z.
- **Synchronization with the GPU:** each scene of its own writes its number in a free word of the GXM notifications
  when it finishes. Another frame is not sent to the GPU if the one from 2 frames ago has not finished (the surface is repeated), and before
  rewriting vertices, palettes or the copy of the normal image it waits for the scene that was reading them to finish (at
  most 30 ms). This way there are no broken frames of one frame.
- **"Covered":** the PS button dialog and the PSP's system notices are drawn on top of the game without going through the
  lists. Every frame, 64 points of the normal image are compared with the Vita's drawing; if they do not look alike for 6 frames
  in a row, the normal image is shown until they look alike again.
- **L+R+START menu:** NativeDraw draws it on top of the image (a 256x128 box at double size); while it is open the
  game receives no buttons. Today it shows the versions; the options are still files in `ux0:data/nativedraw/`.
- **Dumps and replay:** `dumps.md`.
