# Conversation summary of 2026-10-27 (d): RR with "by copy" in the lab

Continues `resumen_conversacion_2026-10-27c.md`, which is still entirely valid, including its "Test methodology".
**This file replaces item 0 of "Open / next steps" in `contexto_ia.md`.** Read `contexto_ia.md` first,
then `resumen_conversacion_2026-10-27c.md` and then this one.

## State
- Code: **dev58.7 with no commit**, same as when conversation c closed. No code was changed in this conversation.
  Backup on the Mac: `~/Downloads/vitabridge/gepatch_dev58.7.patch` (includes `tests/replay.c`).
- **Test done: RR in the Mac's lab (PPSSPP) with "2D vertices (HUD, sprites)" = "by copy (dev58)"
  → also fails.** The track and the cars are not visible, same as on the Vita.
  - The user took a couple of captures (L+R+SELECT) during the race. They say **the rearview mirror was drawn fine**; if it
    does not appear in the captures it is because the key combination changed the camera view.
  - **Captures and `ge_patch.log` from that run: not reviewed yet.** They are still on the Mac, in
    `~/Documents/gepatch-lab/memstick/` (log and `.gpd`). Copy them to `~/Downloads/vitabridge/etapa6_dev58.7_lab_RR/`
    before the next run, because the log gets overwritten.

## Conclusion
- **The main hypothesis is discarded** (that the real GE does not ignore the low 16 bits of `BASE_PLUG`): PPSSPP does
  ignore them and it still fails. The problem is in dev58's mechanism with RR, not in a difference between the Vita and the
  emulator.
- The lab reproduces the failure, so **it can be bisected there** without risking the Vita.
- Suspicion: the interaction of the 2D draws' CALL lists with those of rt1 (rearview mirror), which also go through
  `BASE_PLUG + CALL` (`txCall`). Points to check: the order rt1SelfDraw/rt1FromBig → v2dDraw, the shared slots
  of the 512-entry ring (RR uses ~190 per frame), the VADDR/BASE restored on returning from the list and whether a chained
  2D draw crosses a fb change (big buffer ↔ rt1).
- Another thing to verify with the dump: on the Vita the z-buffer was left clean and the rearview mirror showed the track
  from the front (the world was being drawn on the rt). In the lab the user says the rearview mirror looked fine: compare.

## Next step (pending; instructions already given to the user)
Bisection test: **only Ridge Racer, in the Mac's lab, with "2D vertices (HUD, sprites)" =
"test: minimal list"** (`v2d=4`: the CALL detour without a vertex copy).
1. Save the previous run in `~/Downloads/vitabridge/etapa6_dev58.7_lab_RR/`.
2. `PPSSPP_ADRENALINE=estricto bash ~/Documents/gepatch-lab/jugar.sh rr.iso > ~/Documents/gepatch-lab/ppsspp_salida.txt 2>&1`
3. Menu L+R+START: "2D vertices (HUD, sprites)" → "test: minimal list". Touch nothing else (the 1.5x scale comes
   from RR's profile). Save.
4. In the race, check whether the track and the cars appear (the HUD not being enlarged is normal in this mode).
5. Save the log in `~/Downloads/vitabridge/etapa6_dev58.7_lab_RR_v2d4/`.

How to read it:
- **The track is visible with "minimal list"** → the detour works and the copy fails ("by copy" / `v2d=2`). Next:
  "test: copy without applying" (`v2d=3`) and review the list that `v2dDraw` builds with `tests/replay.c` (`W=`) on the
  lab's dump.
- **It is not visible** → the detour itself fails: review how it coexists with rt1 (list above).

## Notes from this conversation
- **The user changes the options from the plugin's menu (L+R+START), also in the lab.** Always give
  the option name and value as they appear in the menu, not the `gepatch_<ID>.txt` file nor the name in
  the code.
- **This conversation ended up linked to the Windows PC** (`desktop-19r1h9e`), where the lab has no ISO and no
  dev58.7 (repo at dev57). The Mac's folders cannot be requested from a conversation linked to another computer.
  If the new conversation is for the Mac's lab, start it from the Mac's app with the Mac linked and
  connect `~/Documents/gepatch-lab` and `~/Downloads`.
- In automatic mode, the first folder access request was rejected without asking the user. If that happens,
  request it again.
