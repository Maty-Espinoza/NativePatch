# Documentation for the AI (and for anyone who wants to work the same way)

This folder is Claude's working context in NativePatch: how work is done, what tools exist, how to build, how to test on
the Vita and which rules are not negotiable. The repo root has a `CLAUDE.md` that requires reading this at the start of
every conversation. The documentation for humans is in `../../dev/`.

## What to read and in what order
1. **`good-practices.md`** — always, before touching anything: comments, dead code, old files, versions,
   documentation. It is short.
2. **`workflow.md`** — how a conversation starts and ends, where the repo is, pull/push, how to ask the user for tests,
   how to save results and the user's rules.
3. The latest conversation summary of the claude.ai project (`claude/resumen_conversacion_*.md`) and `../../../TODO.md`:
   they say where everything stands and what comes next.
4. Depending on the task: `environment.md` (building: container, Mac VM, WSL, CI), `relay.md` (sending files to the Vita
   and bringing back logs), `stage3-plan.md` (plan and results of the current stage), and from `../../dev/en/`:
   `how-it-works.md`, `logs-and-switches.md`, `dumps.md`, `findings.md`, `versions.md`.

## Files
| File | What |
|---|---|
| `good-practices.md` | Rules so that code, comments, files and documentation do not get messy |
| `workflow.md` | Complete workflow with the user and with the repo |
| `environment.md` | Build environments and tools (`tools/`), capturer test with qemu |
| `relay.md` | The relay with the Vita (`tools/vitabridge.py`): file protocol, what to do and what not to do |
| `stage3-plan.md` | Stage 3: plan, steps, what was learned in each test version and status |

## Languages
The documents in `docs/dev/` and `docs/agent/` exist in English (`en/`) and Spanish (`es/`) with the same file names.
Spanish is the working language with the user; English is the reference for third parties. When one is changed, the
other is changed in the same task (`good-practices.md`). `TODO.md`, `CHANGELOG.md` and `README.md` in the root exist
only in English (`README.es.md` is the Spanish version of the README).
