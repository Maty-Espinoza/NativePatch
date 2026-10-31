# Log levels, switches and log lines

What each plugin writes in its logs, what each line means, and which files in `ux0:data/nativedraw/` change its
behavior. The version numbers in parentheses say since when each thing exists (the old ones, `devNN`/`vNN`, are
explained in `versions.md`). Pending item: change the switches from the L+R+START menu (`../../../TODO.md`);
for now they are files.

Logs help to understand what is going on, but they also cost performance. That is why there are three levels. One is
chosen on the Vita with the file `ux0:data/nativedraw/registro` (content `minimo`, `mediano` or `extremo`; **no file: medium**).
NativeDraw reads it when Adrenaline opens and passes it to NativeLink through the mailbox (`SB[0x4E]`: 0, 1 or 2), so it applies to both.
To change it: upload the file with the relay (`enviar/ux0/data/nativedraw/registro`) and close the applications
(`cmd_*.txt` with `destroy`).

| Level | What for | Cost |
|---|---|---|
| **minimum** | the version that gets published | almost nothing |
| **medium** | everyday development (default) | low: counters, one line per second |
| **extreme** | analysis (phase 1 and similar) | noticeable: with everything on (run 5) the slow frames of GT during a race went up from ~17 % to 27 % |

## What each level does
| | minimum | medium | extreme |
|---|---|---|---|
| NativeLink log every 10 s (`nativelink.log`) | yes | yes | yes |
| `nativedraw_captura.log` | 2 lines every 10 s (frames, commands, Vita time, syncs) | all lines, every second | same + mirror (sampling), T4/data lifetime, list lifetime |
| Sublist memory check (1 out of 32, DISTINTAS) | no | yes | yes |
| Per-frame cost for the game (EV_FRAME: gate, wait for the GE, mirror, hooks) | no | yes ("impacto en el PSP" line) | yes |
| Free PSP CPU (lowest-priority thread, like vpjuego) | no | yes | yes |
| Processor of the emulator threads (runClocks, every second) | no | yes | yes |
| `nativedraw_frames.log` (one line per frame, PSP + Vita) | no | no | yes |
| `nativedraw_tarde.log` (every sync with the Vita running late, with its times) | no | no | yes |
| List lifetime (hashes at the flip; NativeLink checks before enqueuing and at every sync: EV_VIDA) | no | no | yes |
| Read times (own 1/16, shared cache 1/8) and sublist memory times (check, apply) | no | no | yes |
| Data lifetime (vertices, textures, palettes 1 and 2 frames later) | no | no | yes |
| Mirror sampling (how many pages change) | no | no | yes |
| T4: notices from the game CPU (cache flush, DMA) to the mailbox | no (only counts) | no (only counts) | yes |

These stay separate, each with its own file: `dibujar` (the stage 3 presenter: with the file it always replaces the image; without it, only with the
L+R+START menu open; it also turns on the frame record and its `registro:` line in `nativedraw_captura.log`; see `../../agent/en/stage3-plan.md`), T6 (`medir_ge`), `memo_revisar` (check all), `sin_memoria`,
`captura_sin_lectores`, `off`, `medir_gpu` (GPU timer) and `reproducir` (`dumps.md`).

## Dumps (NativeDraw 0.0.8.5)
Dump levels in `ux0:data/nativedraw/volcado`: `desactivado`, `pantallazo`, `frame`, `secuencia` (without the file)
or `auto`. What each one saves, how much it costs and how they are read with `tools/volcado.py`: `dumps.md`.

## Analysis phase switches (`ux0:data/nativedraw/`; turned off with `no` inside, the relay does not delete)
| File | What it does |
|---|---|
| `sin_compuerta` | the game does not wait for the Vita at the sync (NativeLink only counts how many times it was late) |
| `sin_espejo` | NativeLink does not copy the VRAM to the mirror |
| `sin_libre` | no free-CPU thread on the PSP |
| `sin_captura` | the Vita does not walk anything: it marks the lists as captured (almost NativeLink alone) |
| `compuerta_sync` | (v13.10) the wait for the Vita stays at the sync, as before dev66 (for comparison); see `etapa2_fase2_compuerta.md` |
| `sin_adelanto` | (v13.12) the readers do not do the look-ahead check of the plan (only on reaching each CALL, as before); see `etapa2_fase2_adelanto.md` |
| `espejo_espera` | (NativeLink 0.0.3) the PSP flip waits for the mirror copy to finish (mailbox switch 32): in KOF 96 the frame's sprites are copied by the time the Vita asks for them. On the user's Vita it is set to `si` |
| `nucleo_captura` | (v13.13) `0`, `1` or `2` pins the capturer to that core (before v13.13 always core 1); no file or `libre`: any of the 3. The `hilos: captura` line of the log says which one was used; see `etapa2_ridge_racer.md` |

## Drawing on the Vita (stage 3, with `dibujar`; `nativedraw.log`, every 10 s)
- `presentador: N frames en T ms por la superficie (cambios, con copia propia), CPU us/frame (max) | casi negros: juego,
  superficie | revisados N, distintos de la imagen normal M`: frames that went through the NativeDraw surface, their
  cost on the Adrenaline thread, and the check of different frames (1 out of 4, only if the GPU has already finished the surface).
- `traductor: frames HD N, con la imagen normal M | motivos: ... | draws dibujados`: how many frames the Vita drew and
  why the others went with the normal image (`sin_frame_nuevo`, `3d`, `primitiva`, `formato`, `mezcla`...).
- `traductor: ritmo (frames del PSP que avanzó cada frame dibujado): 0 (repetido) N, 1 N, 2 N...`: with many repeated frames
  or jumps, Neo Geo games flicker (they do transparencies by drawing something one frame yes and the next no).
- `gpu: al empezar cada frame, escenas nuestras sin terminar: promedio, max | sin terminar: el frame anterior N, el de
  hace 2 N | reescritos con la GPU sin terminar: vértices y paletas N, imagen normal N`: how far behind the Vita's GPU
  is and how many times something was rewritten that it might still be reading (the textures of the frame record, in the
  `registro:` line of `nativedraw_captura.log`: "con la GPU sin terminar (medido)"). It must be 0: otherwise, broken frames.
- `gpu: frames no mandados por la GPU atrasada N, esperas a la GPU N ms | CPU en 10 s: traductor N ms, revisión de frames
  distintos N ms | revisiones de la superficie saltadas (GPU sin terminar) N` (NativeDraw 0.0.7): frames that were not
  sent to the GPU because it had not finished the one from 2 frames ago (the surface is repeated), time spent waiting for it before rewriting, and
  CPU time of the translator and of the check.
- `gpu tiempos: <tipo>: escenas, GPU ms (promedio, max) | draws por escena (con swizzle), miles de píxeles por escena |
  en cola | histograma de ms` and `gpu tiempos: ocupada N ms de M (%)` (timer, `medir_gpu` switch, since
  0.0.8.13): how long the GPU takes for each type of scene (Adrenaline, normal image, translator on the surface, intermediate
  image). `gpu tiempos: detalle`: 16 consecutive scenes (A Adrenaline, N normal image, T translator, I intermediate:
  draws, GPU ms / queue ms).
- `gpu: escena lenta: <tipo>, draws, miles de píxeles, ms` (0.0.8.20): each of our scenes over 30 ms; the first 3
  ask for a dump. `traductor: draw gigante`: the first 40 draws with an area of more than 2 screens.
- Reason `vertice_fuera` (0.0.8.21): a vertex outside ±4096 or NaN (data read too late): the frame goes with the normal image.
- `pantalla:` (0.0.8.13, every 1 s and only if something changes): scenes and frames to the Adrenaline and ScePspemu screen,
  system dialog, surface state. `notificaciones:`: someone else wrote a GXM notification word.
- In `nativedraw_captura.log`, the `texturas (por frame):` line (0.0.8.19): copies, KB and DMA and sorting time for
  RAM and VRAM; DMA calls; copies by size; swizzled KB by row width; KB equal, changed or new
  compared with the last copy of that group of 8 rows (hash per group: what a cache would save); KB without swizzle;
  distinct textures per frame.
- How we know when the GPU finishes: each of our own scenes writes its number into a word of the GXM notifications
  (the last one Adrenaline does not use, counting down from 511; the log notes it at start-up: `gpu: fin de escenas en la
  notificación N`). Reading a surface the GPU has not finished gives false results, and `sceGxmFinish` to wait for it
  costs ~20 ms in KOF: that is why the check is skipped instead of waiting.

## Sublist memory (v13.11)
Each stored sublist adds up only its nonzero statistics (up to 48; with more it is not stored and the log says so). At the
minimum level, when applying a sublist only commands and draws are added up: the rest of its line does not use it. Details in
`etapa2_fase2_estadisticas.md`.
v13.12: the memory line adds `| adelanto/f: plan= comprobadas= no_iguales= tarde=` (the readers' look-ahead check;
`tarde` = times a reader ran into the capturer). See `etapa2_fase2_adelanto.md`.

## Gate (dev66 + v13.10, all levels)
- `nativelink.log`, every 10 s: `stats compuerta (dev66): <modo> | esperas antes de encolar=N (us c/u, max) |
  autocomprobaciones=N cambiaron=N`, and a `compuerta: ...` line every time it changes mode (60 clean checks
  → before enqueuing; the list changed → at the sync).
- `nativedraw_captura.log`, "impacto en el PSP" line (medium and extreme): at the end `compuerta (v13.10): <modo>, esperas
  antes de encolar/seg=N (us c/u)`. The total wait per frame is still in `compuerta_us` (at the sync or before enqueuing).
- Extreme: `nativedraw_tarde.log` also notes the waits before enqueuing, with type `e`.

## Extreme level files
- `nativedraw_frames.log` (CSV, one line per game frame): `frame_us`, `compuerta_us` (wait for the Vita), `esperas`,
  `sin_esperar`, `sync_GE_us` (wait for the GE inside the sync), `cpu_libre_x1000`, `espejo_us` / `espejo_paginas`,
  `ganchos_us`, and from the Vita: `comandos`, `draws`, `captura_us` (clock), `captura_cpu_us` (processor received),
  `aplicadas`, `comprobadas_lectores`, `lecturas_propias`, `de_lectores`, `listas_pendientes`, `syncs_atrasados`,
  `lista_dir`, `lista_kb`, `memoria_us` (all memoTry), `aplicar_us`.
- `nativedraw_tarde.log` (CSV, every sync in which the Vita finished the list after the game reached the sync): `espera_us`,
  `atraso_us`, `comandos`, `listas_antes`, `encolada_hace_us` and `ultimo_stall_hace_us` (relative to the sync),
  `vita_empezo_tras_us` (since the enqueue), `vita_recorrio_us`, `trabajo_us`, `lecturas_propias`, `de_lectores`,
  `aplicadas`, `procesador_us`. The Vita's times are converted to PSP times using the minimum clock difference.
- Summary: `python3 tools/analisis_fase1.py <folder> [<folder> ...] [--tabla]` (one column per run).
