# Phase 1 — bootstrap + first binary

## Done

| Step | Result |
|---|---|
| Extract EXE | `fm3/SLUS_010.11` (872448 bytes, `PS-X EXE`) |
| Disc helper | `fm3/fm3.bin` hardlink + `fm3/fm3.cue` (no spaces) |
| Seeds | `seeds/jal_seeds.txt` — **3839** addresses (psxrecomp-toml + after-return) |
| Config | `game.toml` (SLUS-01011, LLE, OpenGL) |
| Regen | **4542** functions → `generated/SLUS_010.11_{full,dispatch}.c` (~70 MB full) |
| Build | `build/Front_Mission_3_Recompiled.exe` (~31 MB), `PSX_LAUNCHER=OFF` |
| Boot smoke | Runs 45s+, BIOS LLE from `0xBFC00000`, disc ID `SLUS-01011` NTSC-U |

## Launch

```powershell
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path
.\build\Front_Mission_3_Recompiled.exe --game game.toml --disc fm3\fm3.cue --bios psxrecomp\bios\SCPH1001.BIN
```

Regen after seed/config changes:

```powershell
powershell -File tools\regen.ps1
# then rebuild
cmake --build build --target psx-runtime -j
```

## Notes / caveats

- Paths with spaces in the disc filename broke an unquoted first launch; use `fm3/fm3.cue`.
- Release build has **no TCP debug server** (`PSX_DEBUG_TOOLS=OFF`). For Phase 2
  bring-up use `-DCMAKE_BUILD_TYPE=RelWithDebInfo`.
- Overlay native cache needs a toolchain under `build/overlay_toolchain` or gcc on
  PATH for compile-overlays; until then overlay gaps use the interpreter.
- Visual confirmation of BIOS logo / license / title is **manual** (Phase 2).

## Recompiler stats (first regen)

- Code size: 850 KB (`0xD4800`)
- Functions: 4542 emitted / 4478 analyzed
- Coverage warnings: many out-of-function jump fallthroughs (normal for first seed pass)
