# Versions

## Rule (the user's decision, 2026-10-29; corrected the same day when stage 3 started)
NativeLink (PSP side) and NativeDraw (Vita side) are sibling projects: each one carries its own version
`0.MINOR.PATCH` and can change without the other.

- **Until the first release everything is 0.0.x**, whether one or both plugins are touched: each change raises the PATCH of the plugin that
  changed. **The first release is NativeLink 0.1.0 + NativeDraw 0.1.0.**
- After the first release:

| Change | What goes up | Example |
|---|---|---|
| Touches **only one** plugin (what they pass to each other does not change) | the **PATCH** of that plugin | NativeDraw 0.1.3 → 0.1.4; NativeLink stays at 0.1.2 |
| Needs **both** (shared mailbox, mirror, gate, log...) | the **MINOR** of both, and the PATCH goes back to 0 | NativeLink 0.1.2 + NativeDraw 0.1.4 → 0.2.0 + 0.2.0 |

- **Two plugins with the same MINOR work together.** The installer always carries a compatible pair.
- **Test versions:** a fourth number on top of the version being prepared (`0.0.3.1`, `0.0.3.2`...). The commit
  that confirms carries the version confirmed on the Vita, without the fourth number. Each bug fixed = one commit with its version.
- **Test commits are pushed too** (the user's decision, 2026-10-30): to continue on another computer or in another
  conversation, each test version sent to the Vita can go as a commit to `dev`, marked "(test)" with its full
  number (`NativeDraw 0.0.8.15 (test)`). What matters are the versions: that each commit says which version it
  carries and that the confirmed ones follow the numbering without gaps.
- **No lost versions in the repo:** the commit's version is always the one that follows the last one confirmed on GitHub
  (`git log --oneline -- nativedraw/version.h` or `nativelink/log.h`), even if the tests used a different numbering.
  E.g.: GitHub had NativeDraw 0.0.6 and the tests reached 0.0.8.12 → the commit is NativeDraw 0.0.7. Commits
  marked "(test)" do not count as a confirmed version.
- **NativeDraw 0.0.8 tests** (confirmed by the user): they continue from **0.0.8.13**, so as not to
  clash with the names in `vitabridge\respaldo\` and `vitabridge\etapa3_0.0.8.x\`, which already used 0.0.8.1–0.0.8.12
  as tests of 0.0.7.
- The first release (0.1.0) is marked with a tag and release on GitHub when the user decides.
  Before 1.0 the "Clean test leftovers" option is removed from the installer.
- Where the number lives: NativeLink in `nativelink/log.h` (`NATIVELINK_VERSION`); NativeDraw in `nativedraw/version.h`
  (`NATIVEDRAW_VERSION`). `tools/compilar.sh` reads them for the installer (`version.txt` inside the VPK).
- Git tags: `nativelink-X.Y.Z` / `nativedraw-X.Y.Z` when something is published. The `v0.1`…`v0.2` and `0.1` tags
  in the repo belong to the original GePatch by TheFlow; `gepatch-dev66` is the last state with the old names.

## History
| NativeLink | NativeDraw | Commit | What |
|---|---|---|---|
| 0.0.1 | 0.0.1 | `4faaa2b` | Rename and folders. Same behavior as GePatch 0.2-dev66 + gepresent v13.13 |
| 0.0.2 | | `b5d4a56` | NativeLink: what was inherited from GePatch is removed (architecture A, menu, dumps) |
| | 0.0.2 | `6992890` | NativeDraw: the old presentation, the GXM test bench and the dumps are removed |
| | 0.0.3 | `34dee77` | Stage 3, step 3.0: transparent presenter, off by default (`../../agent/en/stage3-plan.md`) |
| | 0.0.4 | `a0d85da` | Stage 3, step 3.1: L+R+START menu drawn by NativeDraw (for now, the versions) |
| | 0.0.5 | `1ae7191` | Stage 3, step 3.2: frame record (with `dibujar`) |
| | 0.0.6 | `da1daa0` | Stage 3, step 3.3 (first version): the Vita draws the frame record (Retro City); "covered" |
| 0.0.3 | 0.0.7 | `f6384f7` | Stage 3, step 3.3 in KOF 96 (own shaders, textures through DMA, dumps) and broken frames fixed. Tests: NativeDraw 0.0.7.1–0.0.8.12 and NativeLink 0.0.3.1–0.0.3.3 (`8573486` was a test commit, 0.0.8.10 + 0.0.3.2) |
| | (tests 0.0.8.15–0.0.8.22) | `a045974`…`3ab3b91` | Stage 3 test commits: GPU timer, textures sorted by the CPU, one scene for the surface (KOF 96 at 60 fps), Metal Slug XX, diagnosis of the white glitch; 0.0.8.22 + NativeLink 0.0.3.4: code cleanup. The commit that confirms this work will be **NativeDraw 0.0.9** |

Tested on the Vita with Gran Turismo: `../../changelog/en/etapa3_preparacion.md`.

## Equivalence with the old names
Before 2026-10-29 the PSP side was called **GePatch** (`ge_patch.prx`, versions `0.2-devNN`, with tests `devNN.x`) and
the Vita side **gepresent** (`gepresent.suprx`, versions `vNN.M`). The documents in `../../changelog/en/` use those numbers.

| Old | New |
|---|---|
| GePatch 0.2-dev66 | = NativeLink 0.0.1 |
| gepresent v13.13 | = NativeDraw 0.0.1 |
| `ux0:/pspemu/seplugins/ge_patch.prx` | `ux0:/pspemu/seplugins/nativelink.prx` |
| `ux0:data/gepresent/gepresent.suprx` and its switches | `ux0:data/nativedraw/nativedraw.suprx` (same switches) |
| `ge_patch.log`, `ge_patch_prev.log` | `nativelink.log`, `nativelink_prev.log` |
| `ge_patch_gepresent.log` | `nativedraw.log` |
| `ge_patch_captura.log` (and `_prev`), `ge_patch_frames.log`, `ge_patch_tarde.log` | `nativedraw_captura.log` (and `_prev`), `nativedraw_frames.log`, `nativedraw_tarde.log` |

Milestones of the old numbering (details in `../../changelog/`):

| Old version | What |
|---|---|
| GePatch 0.2 (TheFlow) | Original GePatch, abandoned |
| dev1–dev60 | Architecture A: the PSP draws larger (menu, per-game options, .gpd dumps, PPSSPP lab) |
| gepresent v1–v10 | Presentation of the GePatch image with the Vita's GPU; log and dumps written by the Vita |
| gepresent v11 | Stage 1: GXM test bench |
| dev61 + v12 | Stage 2: shadow mode + capturer on the Vita |
| dev62–dev64 + v13.1–v13.6 | Sublist memory, VRAM mirror (double), T4/T6 |
| v13.7 | Core bench (GXM recording on several cores) |
| dev65 + v13.9 | Analysis phase 1, log levels |
| dev66 + v13.10–v13.12 | Phase 2: gate before enqueuing, compact statistics, look-ahead check, DISTINTAS fixed |
| v13.13 | Ridge Racer at 60 fps: interpret line by line, capturer on any core |
