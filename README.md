# Front Mission 3 Recomp (PC)

Static recompilation of **Front Mission 3 (USA, SLUS-01011)** with
[PSXRecomp](https://github.com/mstan/psxrecomp). Scaffolded after
[TombaRecomp](https://github.com/mstan/TombaRecomp).

This repository will hold game-specific config, seeds, tools, and build glue.
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

### Run

```powershell
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path
.\build\Front_Mission_3_Recompiled.exe --game game.toml --disc fm3\fm3.cue --bios psxrecomp\bios\SCPH1001.BIN
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

## Requirements

- CMake ≥ 3.20, C/C++ toolchain (MSYS2 MinGW-w64 **or** MSVC)
- SDL2 (runtime)
- Python 3
- Legally obtained `SCPH1001.BIN` and Front Mission 3 (USA) disc image

## Quick layout

```
psxrecomp/          # framework (junction → psxrecomp-master)
game.toml           # Phase 1
seeds/              # function-start seeds
tools/              # extract / regen helpers
fm3/                # local EXE + disc paths (gitignored)
generated/          # local recompiler output (gitignored)
```

## License

Game assets and BIOS remain copyright of their respective owners and are not
included. Framework license: see `psxrecomp/LICENSE`.
