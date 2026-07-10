# Front Mission 3 Recomp — issues

## Open

### #5 Strange / dry sound on PlayStation BIOS logo
**Severity:** player-audible, cosmetic for boot  
**Layer:** framework SPU (`psxrecomp/runtime/src/spu.c`) — not FM3-specific

The classic Sony logo chord is mixed with **hardware reverb** on real PS1.
PSXRecomp's SPU is intentionally a compact model: ADPCM voices + ADSR + CD/XA
input work; **reverb, noise, pitch-mod, volume sweeps, SPU IRQ are not
implemented** (see `accuracy/axis5_spu.md` §2.2).

Symptoms: logo (and other reverb-heavy cues) sound "thin", dry, or slightly
wrong in pitch/envelope vs DuckStation/hardware. Audio is also **host-pulled**
(SDL queue) rather than guest-clocked every 768 SPU cycles → subtle timing
drift vs video.

**Fix path:** implement reverb (and eventually guest-clocked SPU) in the
framework. No safe per-game config workaround. Do **not** stub reverb in
game.toml.

### #6 Slightly lagging cutscenes (STR / MDEC + XA)
**Severity:** mild, playable  
**Layer:** framework present path + MDEC/CD (not FM3-specific)

#### Root cause (confirmed in code, 2026-07-10)

15-bit frames present **directly from the GL FBO** (cheap blit).  
**24-bit FMV** (GP1 depth24) **cannot** use that path:

1. Every MDEC frame is uploaded via GP0 A0 → previously also staged into the
   GL FBO (double `TexSubImage` + FBO quads) even though present never used it.
2. Present falls through to a **CPU** path: nested
   `gpu_display_pixel_argb` over the full display, then texture upload + swap.

That is the main host cost during STR playback. Overlay interpreter was a
secondary factor (mitigated earlier with `build/cache`).

#### Mitigations applied (framework)

| Change | Effect |
|---|---|
| `gpu_fill_display_argb` bulk scanout | Faster 24/15-bit CPU present (kept) |
| Overlay cache (105 DLLs) | Less interpreter during game modes |
| `turbo_loads` | Faster non-XA loads only |

**Regression (2026-07-10):** GL-skip alone wiped FMV via `ensure_cpu` readback.

**Safe re-land (stutter pass):**
| Change | Why safe |
|---|---|
| Skip GL A0 staging while `depth24` | CPU VRAM still updated |
| Skip `gl_renderer_sync_cpu` on `depth24` present | Prevents FBO wipe of A0 |
| Full CPU→GL resync on next 15-bit present | Restores FBO after movie |
| Skip re-present when MDEC count + display rect unchanged | FMV ~15fps on 60Hz vblank — was re-uploading same frame 4× |
| Faster bulk 24bpp scanout | Present path only |

**Rebuild:** `cmake --build build --target psx-runtime -j`

#### Audio crackle pass (2026-07-10)

Micro-stutter + crackle was largely **SDL/XA underrun** when FMV host work
exceeded one vblank: pump only queued ~735 samples/vblank, so a 25–40 ms spike
emptied the device buffer.

| Change | Effect |
|---|---|
| Adaptive queue top-up (target 120 ms, max 250 ms) | Refills after spikes |
| Trailing pump at end of every vblank present | Covers drain during present |
| SDL device period 2048 samples | Longer hardware buffer |
| Hold last XA sample on CD-ring underrun | Less click than silence gaps |

`PSX_VSYNC` does not affect FMV much: pacing is wall-clock + guest, not swap.

#### Guest-clock SPU (2026-07-10) — landed

Beetle-style sample clock: **1 stereo sample / 768 guest cycles** via
`spu_advance()` from `psx_advance_cycles` device path. Host `spu_render`
only drains the guest output ring (hold-last on underrun).

| File | Change |
|---|---|
| `runtime/src/spu.c` | `spu_advance`, out-ring, mix on guest time |
| `runtime/include/spu.h` | API |
| `runtime/src/psx_cycles.c` | call `spu_advance` in `advance_devices` |

Still missing vs full hardware SPU: reverb, noise, PMON, volume sweeps,
SPU IRQ, capture buffers (see `accuracy/axis5_spu.md` §2.2+).

**Lag fix (same day):** guest out-ring could grow without bound when the host
SDL queue was already full or was filled with hold-samples. Cap lag at
~40–80 ms (drop oldest), host pump only drains real guest samples, SDL
target ~50 ms.

#### FMV skip flash (2026-07-10)

Manual Start-skip tears down 24-bit MDEC; for a few frames VRAM is still
24-bit packed data but GP1 already reports 15-bit → rainbow garbage (user
screenshot). Fix: blank present while Start is held in depth24, and ~6–10
frames after leaving depth24.

#### SPU reverb (2026-07-10) — landed for BIOS logo / ambience

DuckStation/psx-spx reverb unit: FIR halfband 44.1↔22.05, IIR/comb/APF
network in SPU RAM work area (mBASE), EON voice sends, CD reverb bit,
vLOUT/vROUT. Expect richer PlayStation logo chord and hall tails.

#### Still open

- Retest BIOS logo reverb quality vs DuckStation.
- Noise / PMON / volume sweeps / SPU IRQ still missing.
- MDEC whole-frame decode remains architectural.

### #1 Launcher fails to link on MinGW
`PSX_LAUNCHER=ON` → undefined modern GL symbols (`glCreateShader`, …) from
`launcher.cpp`. Workaround: `-DPSX_LAUNCHER=OFF`. Game GL renderer is fine
(dynamic load via `SDL_GL_GetProcAddress`).

### #2 Disc path with spaces
Unquoted CLI paths to `Front Mission 3 (USA).cue` can fail early. Prefer
`fm3/fm3.cue` + hardlink `fm3/fm3.bin`.

## Closed / mitigated

### #3 Boot path not visually validated → mitigated
User playthrough: BIOS screens, cutscenes, and first gameplay reached (2026-07-10).

### #4 Overlay toolchain not bundled → mitigated for dev
gcc path wired; `tools/compile_overlays.ps1` + first cache build (105 DLLs).
Release packaging still needs bundled `overlay_toolchain` (tcc) for end users.
