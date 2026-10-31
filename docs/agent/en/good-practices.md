# Good practices (read before touching the repo)

Rules so that NativePatch's code, comments, files and documentation do not get messy again. They are mandatory for every
conversation with Claude and also apply to anyone who collaborates. If a rule clashes with what the user asks for at the
moment, the user wins; afterwards this list is updated.

## 1. Code comments
- A comment says **what** the function or block does and **how** it does it. Nothing else.
- Every file starts with a short comment: what it is, where it is called from, what it includes. No history.
- **Forbidden in the code:** version numbers (`0.0.8.16`, `dev66`, `v13.10`), dates, "it used to be done like…", "in
  test X this happened…", measurement results, game names as a justification, "(test)". All of that goes to the
  documentation (`docs/`) or to `CHANGELOG.md`.
- **No TODO lists in the code.** Pending items go to `TODO.md`, which is the only list. A loose one-line `TODO:` is only
  accepted if the pending item is so local that it makes no sense outside the code, and even then it is also noted in
  `TODO.md`.
- Comments are touched **only when what the code does changes**. They are not rewritten "to improve them" and what was
  discovered while testing is not added to them.
- Log texts are part of the behavior, not comments: no versions or history inside them ("stats
  compuerta:" and not "stats compuerta (dev66):"). If a script reads them (`tools/*.py`), change both.
- Language: the code and its comments are in Spanish, without accents (accents break some compilers and
  terminals). Function and variable names are short and consistent with the file.

## 2. Dead code and simplicity
- What is not used gets deleted: functions, variables, switches, parameters, `if` branches that can no longer happen,
  code files that nobody includes anymore. Git keeps the history; the code does not.
- A switch or test mode that has served its purpose is removed in the same task that discarded it (or in the next
  confirmed version). It is not left "just in case".
- Compact code, but understandable to a human reading it for the first time. Simplicity and readability are preferred
  over textbook good practices (constants for everything, layers of abstraction) when those only add noise.
- Nothing speculative: no options, flexibility or handling of impossible errors are added "for later".
- Before changing something you must understand the origin of the problem (`workflow.md`, "cause first" rule).
- After every change: build without warnings (`-Wall` is already on in both plugins) and test on the Vita.

## 3. Repo files
- **What is not used stays out of the repo.** Tools from finished experiments, one-off scripts, dumps,
  binaries: they are deleted. If it is worth remembering that they existed, one line in `CHANGELOG.md` or in
  `docs/changelog/` and the git tag where they were left (like `gepatch-dev66`).
- Build outputs go to `build/` (ignored by git). Binaries and `.o` files are never committed.
- Documents have a single place: current ones in `docs/dev/` and `docs/agent/`; history in `docs/changelog/`. No new
  folders and no "v2" copies of a document are created: the existing one is edited.
- File names are in English and lowercase, with hyphens (`how-it-works.md`); the same names in `en/` and `es/`.
- When finishing a task, check with `git status` that no loose files (patches, logs, tests) are left in the repo.

## 4. Relay folder (`vitabridge/`) and the Vita
- The relay only deletes what is older than 7 days in `enviados/`, `recibidos/` and `respuestas/` (`relay.md`). What is
  worth keeping from a test is copied **beforehand** to `vitabridge/etapaN_<version>/<game><n>/`.
- No git bundles, patches or experiment folders from another era are left in `vitabridge/`: they are deleted when
  closing a stage.
- On the Vita, test files (old switches, logs, dumps) are removed when they stop being used; the
  installer has "Clean up test leftovers" for that (until before 1.0).

## 5. Versions and commits
- Rules are in `../../dev/en/versions.md`. Summary: until the first release everything is `0.0.x`; every version sent to
  the Vita carries a fourth number (`0.0.8.22`) and goes into the repo as a commit marked "(test)"; the confirmed version
  goes without the fourth number and is the one that follows the last one confirmed on GitHub (not the numbering of the
  tests). Every bug fixed = one commit with its version. No versions should be lost.
- A commit says which version it brings and what changed, in one line. The detail goes to `CHANGELOG.md` when the
  version is confirmed.
- Documentation-only commits are not made if they can be merged with the change they accompany.
- The version number lives in a single place per plugin: `nativelink/log.h` and `nativedraw/version.h`.

## 6. Documentation
- `docs/dev/` is for humans (what it is, how it works, what was measured, limits); `docs/agent/` for the AI (how work
  is done); `docs/changelog/` is an archive and is not updated. `TODO.md` is the only list of pending items;
  `CHANGELOG.md`, the per-version summary.
- When finishing each task, the affected documents are updated **in English and in Spanish**, with the findings and
  decisions (not in the code). If something was left pending, it goes to `TODO.md`.
- Current documents are written for someone who knows nothing about the project: short sentences, no unexplained
  jargon, no version history unless it matters for understanding the present.
- Conversation summaries (`claude/resumen_conversacion_*.md` in the claude.ai project) say where everything stands and
  what comes next; those before 2026-10-30 are also in `docs/changelog/*/conversaciones/`. They are removed from the
  repo before the first release.

## 7. How to maintain this
- If a rule here no longer serves or one is missing, this file is changed (in both languages) in the same task.
- `CLAUDE.md` in the repo root requires reading this file at the start; that requirement is not removed.
