# Stage 2: sublist memory (gepresent v12.10–v12.13, 2026-10-28)

Follows `etapa2_avance.md` (shadow mode + capturer, dev61 + v12.9) and `etapa2_diagnostico_v12.11.md` (where the time
goes). Marks: [measured], [deduced]. **Commit:** GePatch 0.2-dev61 (no changes) + **gepresent v12.13**.

## In simple terms
- The Vita reads the game's display lists while it runs (still without drawing). What was expensive was walking them instruction by
  instruction, and GT repeats almost everything from one frame to the next.
- **Sublist memory:** the first time the main list calls a sublist, the Vita walks it and saves a
  copy of its memory chunks and what it leaves done. The following times, if the memory is identical and it is called with the
  same state (texture, vertex type, vertices), it applies what was saved without walking it.
- **Result:** GT runs at 56–60 fps over the whole lap, including the bridge (before 36–40); audio and image fine (user).

## How it works (tools/gepresent/captura.c, section "memoria de sublistas")
- Only for a CALL into RAM from the upper level (sp = 0). Key: (address, BASE, entry OFFSET); up to 8
  versions per key in consecutive slots of a 512-entry table (one per entry state).
- **Record** (`recBegin` / `recLine` / `recEnd`): while walking, every new 64-byte line it touches (including nested
  sublists) is copied into a pool (8192 lines, 512 KB) in up to 32 spans. On return (RET at sp = 0) it
  saves: written registers and their final value (up to 64), matrices touched, VADDR/IADDR (absolute if set inside, or
  how far it advanced), dirty/vset/last primitive/target/palette, up to 32 textures, and the difference of the counters (Stats
  before `chg[]`, without the ones the thread counts: frames, times, events).
- **Entry state** (`recRead`, `recDraw`): registers read before being written inside (0x12 vertex type, 0xD3,
  0x1E, 0x9C/0x9D target, 0xA0/0xA8 texture, 0xB0/0xB1 palette, 0xB2–0xB5 copies; up to 16) and whether it draws with the
  entry vertices/indices. They are checked before using it (`depsOk`).
- **Use** (`memoTry`): same entry state → the lines are compared (word by word, read as always: from the readers'
  cache or from the PSP RAM) with the copy → equal: `memoApply` + add counters, and continue after the
  CALL. Different → it is walked and recorded over (`victim`). Span near the stall (may not be written yet) → it is walked.
- **Check** (always, 1 out of every 32 applicable; all of them with `ux0:data/gepresent/memo_revisar`): what was saved is computed
  and it is also walked; if the final state differs (registers, matrices, matrix indices, vertices/indices) it counts
  `DISTINTAS` and writes `memoria: sublista ... DISTINTA` (the first 10). `ux0:data/gepresent/sin_memoria` turns it off.
- Approximate (statistics only): "changes" and batches of the first draw of an applied sublist depend on the entry state;
  `chg[]` (most frequent "other" registers) does not add up the applied sublists.
- Memory: 1488 KB in a `gepresent_memoria` block (512 × ~1.9 KB table + 512 KB pool). When the pool fills up it is
  emptied entirely (`deposito_vaciado`, ~40 times in a GT lap).
- Log line: `memoria de sublistas (por frame): aplicadas comandos_sin_recorrer (% de todos) lineas_comprobadas |
  nuevas cambiaron otra_entrada sin_guardar | por seg: deposito_vaciado revisadas DISTINTAS`.
- **Test outside the Vita:** `tools/gepresent/prueba_memoria.c` (synthetic lists, ARM under qemu in the container;
  instructions in its header): without memory / with memory / checking all must give the same statistics and the same
  state. It covers: a sublist that changes, a call with other vertices, another texture, the same one twice, nested ones, a BASE that
  leaks out on exit. It found a hang that came from v12.9 (fixed in v12.12): `texAdd` hung with `gen = 0`
  (before the first flip); now `gen` starts at 1.

## Versions
| Version | What | md5 |
|---|---|---|
| v12.10 | processor time per thread (runClocks), batches without breaking at TEXLEVEL | `36a120f4…` |
| v12.11 | diagnostics: second pass every 5 s, hashes of repeated sublists (removed in v12.12) | `562d6354…` |
| v12.12 | sublist memory + 1/32 check + `gen = 1` | `cb12626d…` |
| v12.13 | memory counters fixed (when applying, frames and times were being added) | `d4141c1f…` |

## Results in GT (UCUS-98632), one lap with the bridge [measured]
Logs: `vitabridge/etapa2_v12.10/gt1/`, `etapa2_v12.11/gt1/`, `etapa2_v12.12/gt1/`, `etapa2_v12.13/gt1/`.
| | v12.10 (no memory) | v12.13 (with memory) |
|---|---|---|
| GePatch fps (every ~10 s), the lowest | 37.1 | 55.7 (almost all 59–60) |
| drawsyncs that waited for the Vita / that reached 5 ms | 5122 / 2476 | 523 / 188 |
| average wait per drawsync | 961 µs | 77 µs |
| capture, wall-clock ms per frame during a race (average / worst second) | ~17 / ~29 | ~9.3 / ~16.5 |
| capturer processor (ms per second) | ~700 | ~475 |
- During a race the memory skips **~78 % of the commands** (62–88 %; on the bridge ~55,000 of ~67,000); ~126 sublists
  applied and ~1800 lines checked per frame; they almost never change (`cambiaron` 0) nor are called with another state (~1).
- Check: **~28,700 sublists checked, 0 different**.
- SH was not tested again with the memory (with v12.10 it was already comfortable: ~4.5 ms per frame, no waits).

## What is missing / ideas
1. **VRAM mirror** (GT and RR): GT has 80–140 sublists per frame in the VRAM (today they are skipped), ~95 % of the textures
   and ~80 % of the palettes. Without the mirror GT's capture is incomplete. See `arquitectura_dibujo_en_vita.md` §6.
2. T2 (black box), T4, T6; GXM recording on 2–3 threads with fixed affinity; afterwards, drawing (stage 3).
3. The interpretation loop still costs ~360 ns per command: shortening it (RAM span once per line, fewer
   counters) would lower what is not skipped.
4. The readers (cores 0 and 2) still walk everything, even if the memory skips it: they could walk only what is not
   saved, or compute hashes of lines.
5. For drawing: what is saved per sublist will be what is already translated to GXM. Besides today's entry state, the translation
   will depend on matrices and other registers that the sublist does not set (define when designing stage 3). The vertices and
   textures in RAM are read by the GPU directly, so they do not have to be equal; only their addresses.
6. Unreliable measurements: core 0 in the `procesador` line (the CreateThread hook does not see the threads created
   before gepresent was loaded).
