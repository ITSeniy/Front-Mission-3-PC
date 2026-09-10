# Phase 0 checklist

- [x] Workspace layout + `.gitignore`
- [x] Reproducible vendored framework under `psxrecomp-master/`
- [x] `SCPH1001.BIN` in `psxrecomp-master/bios/` (524288 bytes, local only)
- [x] RmlUi + FreeType pinned as Git submodules
- [x] MSYS2 MinGW toolchain: cmake, ninja, gcc 15.2, SDL2, ccache
- [x] Recompiler built: `psxrecomp-bios`, `psxrecomp-game`, `psxrecomp-toml`
- [x] BIOS C regenerated → `psxrecomp-master/generated/` (dispatch entries: **4439**)
- [x] BIOS-only runtime linked (`psxrecomp___BIOS.exe`, launcher OFF)

## Notes

- Launcher UI: modern GL loaded via `SDL_GL_GetProcAddress` (Block C1). Use
  `-DPSX_LAUNCHER=ON` on MinGW. Game GL renderer uses the same pattern.
- Prefer MinGW shell PATH: `C:\msys64\mingw64\bin` first.
- Regen BIOS: `powershell -File tools/regen_bios.ps1`
- Regen game (Phase 1+): `powershell -File tools/regen.ps1`

## Build recipes used

```powershell
$env:Path = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:Path

# Recompiler
cmake -S psxrecomp-master/recompiler -B psxrecomp-master/recompiler/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build psxrecomp-master/recompiler/build -j

# BIOS
powershell -File tools/regen_bios.ps1

# Runtime (with launcher)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSX_LAUNCHER=ON
cmake --build build --target psx-runtime -j
```
