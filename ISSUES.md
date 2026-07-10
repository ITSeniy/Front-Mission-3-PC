# Front Mission 3 Recomp — issues

Working snapshot: `docs/WORKING_STATE.md` (2026-07-10, post-SPU IRQ/capture).

## Open

### #5 Strange / dry sound on PlayStation BIOS logo
**Severity:** player-audible, cosmetic for boot  
**Layer:** framework SPU (`psxrecomp/runtime/src/spu.c`) — not FM3-specific

The classic Sony logo chord is mixed with **hardware reverb** on real PS1.
As of 2026-07-10 the framework SPU has guest-clock timing, reverb, sweeps,
noise, PMON, IRQ9, and capture buffers (see notes below). Retest logo vs
DuckStation if residual dryness remains (deeper reverb/rate edge cases).

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

#### SPU sweeps / noise / PMON (2026-07-10) — landed (B)

| Feature | Behavior |
|---|---|
| **Volume sweeps** | Voice L/R + main L/R: fixed (`bit15=0`) or sweep envelope (DuckStation `VolumeSweep`) |
| **Noise (NON)** | Dr Hell / PCSX-r waveform; `SPUCNT` noise clock; replaces ADPCM when NON[voice] |
| **PMON** | Pitch of voice N modulated by voice N−1 `last_volume` |

#### SPU IRQ9 + capture buffers (2026-07-10) — landed (pre-playthrough audio)

| Feature | Behavior |
|---|---|
| **IRQ address match** | `0x1F801DA4` ×8 vs decode / DMA / FIFO / capture write; needs `SPUCNT.6`; raises `IRQ_SPU` (I_STAT bit9); sticky `SPUSTAT.6` until `SPUCNT.6` cleared |
| **Capture buffers** | Each guest sample: CD L/R pre-vol → RAM `0x000`/`0x200`, voice1/3 `last_volume` → `0x400`/`0x600`; CWA advances by 2 within 1KB banks |
| **SPUSTAT** | Low-6 mirror of SPUCNT + IRQ flag + transfer-ready (`0x400`) + capture-half (`0x800` when CWA≥0x200) |

#### Block C — MDEC residual (2026-07-10)

Pixel path already Beetle-faithful (dequant/IDCT/YUV; `mdec_e2e` 0/256 diffs).
Block C closed the remaining non-streaming gaps:

| Item | State |
|---|---|
| 4bpp / 8bpp mono packing (D6) | Landed |
| Status current-block + cmd mirror (D7/D3) | Landed |
| EOB zig-zag walk (D9) | Landed (one-shot) |
| Streaming 32-word FIFO (D8) | Open (architectural) |

#### Still open

- Retest music fades / percussion / stream-IRQ games vs DuckStation.
- MDEC streaming FIFO (D8) if a title desyncs on DMA pacing.

### #1 Launcher fails to link on MinGW → fixed (Block C1)
Was: `PSX_LAUNCHER=ON` undefined modern GL (`glCreateShader`, …) against
`opengl32`. Fix: load modern GL via `SDL_GL_GetProcAddress` in `launcher.cpp`
(same pattern as `gpu_gl_renderer.c`). Rebuild with `-DPSX_LAUNCHER=ON`.

### #2 Disc path with spaces
Unquoted CLI paths to `Front Mission 3 (USA).cue` can fail early. Prefer
`fm3/fm3.cue` + hardlink `fm3/fm3.bin`.

### #7 Crash / hard exit after training (tutorial complete)
**Severity:** blocks first-city progress  
**Layer:** scene transition — overlay + load + BIOS B0 (LLE)  
**Repro:** finish in-game training / tutorial → process exits (window gone).

#### Forensics (`build/psx_last_run_report.json`, build `f0f1ea9`, frame ~32287)

| Field | Value |
|---|---|
| `reason` / `exit_origin` | `atexit` / **`unknown`** (not SDL close — hard `exit`) |
| Guest PC at dump | `0x00000400` (GPRs look **host-corrupted** `0x00007FF6…` — dump after teardown) |
| EPC | `0x800B8990` region (VSync/timer spin helper — likely last steady PC) |
| `unknown_dispatch` | 0 |
| Stack | max ~7 KB (not host stack blowup) |
| Overlay | `0x8014D000` capture (565 KB); entry **`0x8014DADC` in seeds + executed** |
| Call chain (recent_fn) | `0x80082714 jal 0x8014DADC` → `0x80028D0C` (a0=116,a1=0xAA01) → `0x800837D4` (a0=2845) → … → BIOS thunk `0x800B20C0` → `0xB0` → `0x5E0` → `0x1F10` |

Overlay prologue at `0x8014DADC` (from capture):

```text
addiu a0, zero, 116
jal   0x80028D0C          ; alloc / open style (a1=0xAA01)
...
jal   0x800837D4          ; load path (a0=2845)
jal   0x800AC57C
...
```

Then game hits **BIOS B0** table stubs (`0x800B20C0` family: `li t2,0xB0; jr t2; li t1,N`) under **full LLE** (`bios_hle=false`). Process dies without `psx_fatal_halt` (`fatal: null`).

#### Working theory
Post-training **mode switch** (unload training → load city/setup): overlay at `0x8014D000` runs a multi-file load, then BIOS pad/card/kernel re-init. Failure is either:

1. Host crash/SEH during that window (GL / overlay DLL / BIOS LLE) → untagged `exit`, or  
2. Guest wild jump after a failed load, with atexit snapshot too late to be trustworthy.

Many CRC variants of `0014D000_*.dll` already in `build/cache` — not a “zero coverage” miss.

#### Next debug steps
1. Rebuild **RelWithDebInfo** + `PSX_DEBUG_TOOLS=ON` (TCP 4480) and re-hit training end.  
2. Keep `psx_crash.txt` / SEH dump if generated.  
3. After the bad session: `python psxrecomp/tools/compile_overlays.py --captures build/overlay_captures.json ...` then retry.  
4. Optional A/B: `bios_hle=true` only as a probe (not a long-term fix).  
5. Trace CD open for the resource keyed by **2845** / type **0xAA01**.

## Closed / mitigated

### #3 Boot path not visually validated → mitigated
User playthrough: BIOS screens, cutscenes, and first gameplay reached (2026-07-10).

### #4 Overlay toolchain not bundled → mitigated for dev
gcc path wired; `tools/compile_overlays.ps1` + first cache build (105 DLLs).
Release packaging still needs bundled `overlay_toolchain` (tcc) for end users.
