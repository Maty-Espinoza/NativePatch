# Developer documentation (English)

To understand NativePatch without knowing anything beforehand. Spanish version: `../es/` (same file names).

| Order | File | What |
|---|---|---|
| 1 | `overview.md` | What the project is, its two plugins, status, how to install and build, limits, origin |
| 2 | `graphics-basics.md` | The bare minimum of graphics (CPU, GE, display lists, textures, framebuffers) to follow the rest. If you already know, skip it |
| 3 | `how-it-works.md` | How NativeLink and NativeDraw work: mailbox, mirror, gate, capture, frame record, translator, presenter |
| 4 | `findings.md` | What was measured, what was discovered, what was ruled out and what limits it has |
| 5 | `architecture-and-roadmap.md` | The analysis that decided the architecture and the roadmap by stages (kept as it was written) |
| — | `logs-and-switches.md` | What each log writes, line by line, and the options of `ux0:data/nativedraw/options.txt` |
| — | `dumps.md` | Frame dumps: levels, files, how to read them with `tools/dump.py`, replay on the Vita |
| — | `nptest.md` | The synthetic 2D battery: scenes, rare techniques, how to run it on the Vita and on a PC |
| — | `unsupported.md` | The rendering techniques not drawn yet or drawn slowly, with the test that covers each one: the list to design support from |
| — | `adrenaline.md` | What you need to know about Adrenaline: plugin loading, how it draws, memory map, PSP–ARM channel |
| — | `performance-plan.md` | The single list of performance work towards 60 fps, in order, with how each item is verified (bench on the PC or the Vita) |
| — | `versions.md` | Version rules, commit history and equivalence with the old names (GePatch, gepresent) |

How the project is worked on (flow, environments, tests): `../../agent/en/`. Version-by-version history:
`../../changelog/en/`.
