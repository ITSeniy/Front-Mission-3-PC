# Front Mission 3 Recomp (PC)

Static recompilation of **Front Mission 3 (USA, SLUS-01011)** with
[PSXRecomp](https://github.com/mstan/psxrecomp). Scaffolded after
[TombaRecomp](https://github.com/mstan/TombaRecomp).

This repository holds game-specific config, seeds, tools, build glue, and the
exact vendored PSXRecomp source snapshot used by the current runtime.
It does **not** ship the disc image, BIOS, or generated game C.

## Status

**Working snapshot (2026-07-10)** — boots to gameplay with polished FMV/audio
path. Pre-playthrough SPU stack landed; formal Playthrough A in progress.
Details: `docs/WORKING_STATE.md`.

| Milestone | State |
|---|---|
| Framework + toolchain + BIOS | Done (Phase 0) |
| `game.toml` + JAL seeds (3839) | Done |
| Game regen (4542 funcs) | Done |
| `Front_Mission_3_Recompiled.exe` | Done (~31 MB) |
| Boot smoke (BIOS LLE + disc SLUS-01011) | Done |
| FMV present + guest-clock SPU | Done |
| SPU reverb / sweeps / noise / PMON | Done |
| SPU IRQ9 + capture buffers | Done |
| Playthrough A (logo → FMV → menu → gameplay) | In progress |
| Block C enhancements (launcher/WS/SSAA/turbo/FMV skip) | C1/C3/C4 done; C2 WS Phase 0; C5 RE notes — `docs/BLOCK_C_ENHANCEMENTS.md` |

### Run

```powershell
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path
.\build\Front_Mission_3_Recompiled.exe --game game.toml --disc fm3\fm3.cue --bios psxrecomp-master\bios\SCPH1001.BIN
```

### After playing (faster overlays / less FMV hitch)

Play sessions write `build/overlay_captures.json`. Compile them to native
DLLs (needs MinGW `gcc`):

```powershell
powershell -File tools\compile_overlays.ps1
# then restart the game — loads build/cache
```

Known rough edges: residual logo/FMV polish vs DuckStation — see `ISSUES.md`
#5 / #6.

### Post-training scheduler test

The debug launcher creates an isolated evidence directory for every run under
`build-debug/debug-runs/<UTC timestamp>/`. It records the exact binary hash,
arguments, debug environment, separate stdout/stderr logs, exit code, a concise
`summary.json`, and every crash/freeze artifact changed by that run. It also
keeps the process alive when the former hard-exit path publishes `PC=0`, so its
TCB/scheduler state can be captured before teardown:

```powershell
powershell -File tools\run_debug.ps1
# Finish the tutorial. When the runtime halts, leave it open and run separately:
python tools\capture_scheduler.py
```

The capture is written to
`build-debug/post_training_scheduler_capture.json`. It includes the scheduler,
thread state, the final three frames of IRQ context (`ra_saved`, `ra_exit`,
`same_thread`, and `restored`), and write provenance for the complete current-TCB
context (`wtrace_all_dump`) together with its live RAM contents. For the
legacy-scheduler A/B probe, launch with `tools\run_debug.ps1 -LegacyScheduler`.

For an unexplained native crash, add `-NativeDebugger`. The same wrapper runs
the game under the bundled MSYS2 GDB and writes a full all-thread native
backtrace, registers, and fault-site disassembly to the session logs:

```powershell
powershell -File tools\run_debug.ps1 -NativeDebugger
```

Use `-PrepareOnly` to validate paths/build metadata and create a manifest
without starting the game.

## Requirements

- CMake ≥ 3.20, C/C++ toolchain (MSYS2 MinGW-w64 **or** MSVC)
- SDL2 (runtime)
- Python 3
- Legally obtained `SCPH1001.BIN` and Front Mission 3 (USA) disc image

## Quick layout

```
psxrecomp-master/   # tracked framework snapshot (RmlUi/FreeType are submodules)
game.toml           # Phase 1
seeds/              # function-start seeds
tools/              # extract / regen helpers
fm3/                # local EXE + disc paths (gitignored)
generated/          # local recompiler output (gitignored)
```

## License

Game assets and BIOS remain copyright of their respective owners and are not
included. Framework license: see `psxrecomp-master/LICENSE`.

## Русский

Проект статической рекомпиляции Front Mission 3 для ПК на базе PSXRecomp с собственными инструментами и исследованиями runtime.

MIT относится только к авторскому коду; лицензии сторонних компонентов сохраняются.
