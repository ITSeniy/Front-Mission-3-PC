# Block C — Enhancements (after “играется”)

Post-playable polish. Faithful boot path stays default; these are opt-in or
safe quality-of-life knobs. Tracker for Front Mission 3 (SLUS-01011).

| Item | Goal | Status |
|---|---|---|
| **C1 Launcher** | `PSX_LAUNCHER=ON` on MinGW | In progress (GL load via SDL) |
| **C2 Widescreen** | 16:9 where 3D allows; 2D UI strategy | Phase 0: opt-in + hud_sprt/gte_game_mode — `docs/WIDESCREEN_FM3.md` |
| **C3 SSAA / filtering** | Sharper present without breaking UI | Easy knobs in `game.toml` |
| **C4 Turbo loads** | Already on — tune engage / docs | Fine-tune / document |
| **C5 Skip FMV** | Game’s own end-of-movie path | RE done: no static table; START fallback only — see `docs/FMV_SKIP_RE.md` |

---

## C1 — Launcher (MinGW)

**Problem:** `launcher.cpp` used `GL_GLEXT_PROTOTYPES` and linked modern GL
against `opengl32`, which only exports GL 1.1 on Windows → undefined
`__imp_glCreateShader`, etc.

**Fix:** load modern entry points with `SDL_GL_GetProcAddress` (same pattern as
`gpu_gl_renderer.c`). GL 1.1 (`glClear`, `glTexImage2D`, …) still from
`opengl32`.

**Build:**

```powershell
cmake -B build -DPSX_LAUNCHER=ON ...
cmake --build build --target psx-runtime -j
```

Default in framework `runtime.cmake` is `PSX_LAUNCHER=ON`. FM3 historically
configured OFF; after the link fix, leave ON.

---

## C2 — Widescreen (FM3-specific hardness)

**Phase 0 landed** — see **`docs/WIDESCREEN_FM3.md`**.

- Default `aspect_ratio = "4:3"` (identity).
- `[widescreen] offer=true`, `hud_sprt_squash=true`, `gte_game_mode=true` (runtime).
- Launcher 16:9 toggle available (EXPERIMENTAL).
- **Not yet:** `sprite_tag_funcs`, `auto_screen_x`, backdrop sites (need Ghidra + regen).

Framework: `psxrecomp/WIDESCREEN.md`.

---

## C3 — SSAA / filtering

Existing `[video]` knobs (no code required for first pass):

| Key | Recommended FM3 start | Notes |
|---|---|---|
| `supersampling` | `2` | Internal SSAA; 3–4 if GPU holds 60 |
| `antialiasing` | `true` | Linear present filter |
| `texture_filtering` | `"nearest"` | Keep native PS look for 2D UI; try `"bilinear"` later |
| `renderer` | `"opengl"` | Needed for cheap SSAA |

---

## C4 — Turbo loads

Already `turbo_loads = true` in `game.toml`.

Behaviour (`main.cpp` / CD load detector):

- Engages after **sustained** CD data load (`TURBO_LOADS_ENGAGE_FRAMES` ≈ 20
  vblanks) so short seeks do not thrash pacing.
- **Excludes XA/FMV** — guest 1x disc timing for streams.
- Launcher “Turbo loads” persists to `settings.toml` and overrides toml.

Fine-tune later only if engage feels laggy or too aggressive (framework
constant / optional config — not required for v1).

---

## C5 — Skip FMV (`fmv_skip_*`)

Tomba uses a **static** per-movie frame-total table. FM3 does **not** (RE 2026-07-10):

- 26 STRs under `\MV\MOVxx.STR` (no MOV10)
- Player overlay `\YMV.BIN` (path list + PsyQ MDEC strings)
- Frame totals extracted from STR headers — **not** present as a u16 table in EXE/YMV/disc
- End condition is likely stream EOF / PsyQ St\*, not `frame >= total-3`

Full notes + frame inventory: **`docs/FMV_SKIP_RE.md`**.

**Practical skip today:**

```toml
[video]
auto_skip_fmv = false   # true => START injection (no table required)
# do not set fmv_skip_total_table / fmv_skip_movie_id for FM3 yet
```

Manual Start during FMV still works. Table-based instant skip needs live RAM RE
during playback (see FMV_SKIP_RE.md “Next RE steps”).

---

## Order of work

1. **C1** link fix + rebuild with launcher ON  
2. **C3** raise supersampling default to 2 (safe QoL)  
3. **C4** document turbo (done here)  
4. **C5** RE when convenient during playthrough  
5. **C2** after map of 2D vs 3D scenes  

---

## Validation

- Launcher: cold start shows UI; Start game reaches BIOS/logo.  
- SSAA=2: no softlock; title UI still sharp enough.  
- Turbo: non-FMV loads faster wall-clock; XA/intro audio in sync.  
- FMV skip: only enable after addresses verified on intro + one mid-game STR.
