# CLAUDE.md — instructions for the AI working on this repo

Read these two files before doing anything else in every conversation, in this order:
1. `docs/agent/en/good-practices.md` (or `docs/agent/es/good-practices.md`) — rules for comments, dead code, old files,
   versions and documentation. Non-negotiable.
2. `docs/agent/en/workflow.md` — how a conversation starts and ends, repo, build, tests on the Vita, user's rules.

Then the latest conversation summary in the claude.ai project (`claude/resumen_conversacion_*.md`) and `TODO.md`.
Everything else is indexed in `docs/agent/en/README.md`.

## Rules that are easy to break
- Comments say what a function does and how. No version numbers, dates, test results or history in the code; no TODO
  lists in the code (they live in `TODO.md`).
- Delete what is not used (code, switches, files). Git keeps the history.
- Every version sent to the Vita gets a fourth number and a "(test)" commit on `dev`; confirmed versions follow the
  last confirmed one on GitHub (`docs/dev/en/versions.md`).
- After every change: build, send to the Vita, and explain to the user in plain words what was done and how.
- Never change code without a clear cause for the problem (`workflow.md`, "cause first").
- Update the docs in English **and** Spanish at the end of each task; `TODO.md`, `CHANGELOG.md` and the conversation
  summary too.
- Ask before anything that costs many tokens, changes the test environment or was not requested. Answer in Spanish.

## General coding behavior
Think before coding: state assumptions, ask when unclear, prefer the simpler approach. Minimum code that solves the
problem; nothing speculative. Touch only what the task needs; every changed line should trace to the request. Define
how a change will be verified (a build, a log line, a test on the Vita) before making it.
