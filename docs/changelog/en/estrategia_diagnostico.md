# Low-token diagnostic strategy

The user's proposal (prepared with Gemini, 2026-10-24) to avoid recording seconds of gameplay.
Recording a lot uses up context and adds noise. The cause of a visual bug is almost always in
a few commands of one frame, or in the previous frame. Status: implemented in dev49.

| Proposal | Where it ended up |
|---|---|
| 1. 3-frame burst: save N-2, N-1 and N to see the render targets that are drawn one frame before being used (rear-view mirror, shadows, water) | `dump.c` (`DUMP_FRAMES 3`): L+R+SELECT records **3 consecutive frames** (N, N+1, N+2). A circular buffer of the *previous* frames would force recording all the time and the plugin would run slowly; with 3 frames going forward, frame N+1 already sees what N left in its render targets. |
| 2. Lineage of textures and render targets, and viewports with unexpected scale | `gpd.py linaje VOLCADO` |
| 3. Command diff, only the critical registers (VIEWPORT, SCISSOR, REGION, FRAMEBUFPTR, TEXADDR) | `gpd.py diff ACTIVO [PASIVO]`: what the plugin changed by draw ranges (only the changes between ranges) and, with a passive dump of the same point, where the game sent something else |
| 4. Inspector of the fake VRAM and z-buffer (rt1, Z_SAVE) | `gpd.py vram VOLCADO`: fbs and textures in the fake VRAM with collisions, and when the z VRAM was saved or restored relative to the draws with depth |

## Recommended order for a new dump
1. `gpd.py original VOLCADO CARPETA`: the same frame without GePatch. Is the bug the plugin's?
2. `gpd.py linaje VOLCADO`: which fb is involved and from which frame does it come?
3. `gpd.py diff VOLCADO`: what did the plugin change in those draws?
4. `gpd.py vram VOLCADO`: are there collisions in the fake VRAM or a z saved at a bad time?
5. Only then, `informe` (filtered with grep for the draws that came up above) and `imagenes`.

The first case to try it on is Ridge Racer's rear-view mirror, which was left empty by the 3D clipping (dev48).
