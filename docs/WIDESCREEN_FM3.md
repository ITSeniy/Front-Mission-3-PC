# C2 — Widescreen for Front Mission 3

Status: **Phase 0 landed** (config + plan). Default presentation remains **4:3**.
16:9 is opt-in via launcher / `settings.toml` / `[video] aspect_ratio`.

Framework reference: `psxrecomp-master/WIDESCREEN.md` (squash hack + Tomba hooks).

---

## Why FM3 is harder than Tomba

| Surface | Tomba | Front Mission 3 |
|---|---|---|
| World | Mostly GTE 3D + some 2D backdrop | Mixed: **3D combat/maps** + **very heavy 2D UI** |
| HUD | Thin in-world HUD | Dense menus (wanzer setup, shops, mail, radar, dialog) |
| Menus | Fewer full-screen 2D shells | Long pure-2D flows |
| Main EXE COP2 | high | **~1064 COP2 ops** in main EXE — real GTE use |
| Overlays | backdrop handlers need tags | Many `.BIN` overlays (UI + combat) |

Blind 16:9 (GTE squash only) will:

1. Widen 3D FOV correctly in principle  
2. Stretch or mis-anchor 2D panels unless `hud_sprt_squash` / tags land  
3. Pillarbox FMV (framework already does this — good)  
4. Show empty margins where world-space culls still use 320  

---

## Phases

### Phase 0 — safe opt-in (this commit, **no regen**)

Runtime-only knobs in `game.toml`:

```toml
[video]
aspect_ratio = "4:3"          # ship default — identity

[widescreen]
offer            = true       # launcher shows EXPERIMENTAL 16:9 toggle
hud_sprt_squash  = true       # edge/center proportion for untagged SPRTs
gte_game_mode    = true       # treat GTE-heavy frames as gameplay (vs pure UI)
# no sprite_tag_funcs yet — needs Ghidra + regen
```

**How to try 16:9 without editing game.toml:**

- Launcher → Settings → Aspect ratio → 16:9 (persists `settings.toml`), or  
- CLI/env equivalent if you set `aspect_ratio` in `settings.toml` next to the exe.

At 4:3 every WS path is a no-op (framework identity).

**Expected first look at 16:9:**

- OK-ish: 3D field / city FOV wider  
- Stretched characters/wanzers until `sprite_tag_funcs`  
- Menus better than raw stretch if `hud_sprt_squash` engages  
- FMV stays 4:3 pillarbox  
- Possible pop-in at side edges (cull)  

### Phase 1 — regen hooks (after visual triage)

1. **Ghidra:** find shared per-prim helper after RTPS (Tomba’s was `0x8005E08C` + anchor `0x1F800070`).  
2. Add `sprite_tag_funcs` + `sprite_anchor_addr` → **regen main EXE**.  
3. Enable `[widescreen.cull] auto_screen_x = true` → regen (generic; no site list).  
4. Rebuild overlay cache (`tools/compile_overlays.ps1`) if overlay emit needs WS config.

### Phase 2 — FM3-specific 2D

1. Census full-2D screens (title, setup, mail, map UI) via `ws_census` if debug build.  
2. Only add `[widescreen.backdrop] x_sites` for **verified** world parallax — **not** UI.  
3. Cull `bias_sites` / `range_sites` only if auto_screen_x is not enough.  
4. Consider leaving pure-menu sequences in 4:3 via present path (gte_game_mode already helps).

### Phase 3 — polish / native-wide (later)

Framework native-wide is shelved; not FM3-blocking. Stay on squash hack until
Tomba-class issues are gone.

---

## What not to do

- Do **not** default `aspect_ratio = "16:9"` until Phase 1 tags exist and a playthrough says menus are acceptable.  
- Do **not** set `full_2d = true` — FM3 is not a pure 2D title (would force wrong present).  
- Do **not** copy Tomba `sprite_tag_funcs` addresses — they are SCUS-94236-specific.  
- Do **not** enable backdrop `x_sites` without scene proof (mis-tagging UI splits dialogs).

---

## Validation checklist

| Scene | 4:3 | 16:9 Phase 0 | 16:9 after Phase 1 |
|---|---|---|---|
| BIOS / license | 4:3 | 4:3 (pre-game) | 4:3 |
| FMV intro | 4:3 | 4:3 pillarbox | 4:3 pillarbox |
| Title / main menu | native | SPRT squash attempt | better |
| Wanzer hangar UI | native | may look soft/stretched | target good |
| City / field 3D | native | wider FOV, fat chars | tagged proportions |
| Combat | native | FOV + HUD | tagged + cull |

---

## Debug (RelWithDebInfo + TCP)

If debug tools are on:

- `ws_aspect 16 9` / `ws_aspect 1 1` — live toggle  
- `ws_census on` — prim draw ring  
- `gpu_state` — `ws.*` fields  

Release builds used for FM3 playthrough may lack TCP; use launcher aspect toggle.

---

## Related

- Framework: `psxrecomp-master/WIDESCREEN.md`
- Block C tracker: `docs/BLOCK_C_ENHANCEMENTS.md`
- Tomba reference config: `TombaRecomp-master/game.toml` `[widescreen*]`
