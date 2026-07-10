# Phase 0 checklist

- [x] Workspace layout + `.gitignore`
- [x] `psxrecomp/` junction → `psxrecomp-master`
- [x] `SCPH1001.BIN` in `psxrecomp/bios/` (524288 bytes)
- [x] RmlUi + FreeType cloned into `psxrecomp/lib/`
- [x] MSYS2 MinGW toolchain: cmake, ninja, gcc 15.2, SDL2, ccache
- [x] Recompiler built: `psxrecomp-bios`, `psxrecomp-game`, `psxrecomp-toml`
- [x] BIOS C regenerated → `psxrecomp/generated/` (dispatch entries: **4439**)
- [x] BIOS-only runtime linked (`psxrecomp___BIOS.exe`, launcher OFF)

## Notes

- Launcher UI (`PSX_LAUNCHER=ON`) currently fails to link on MinGW: modern GL
  symbols in `launcher.cpp` (`glCreateShader` …) are not resolved against
  `opengl32`. Work around: `-DPSX_LAUNCHER=OFF`. GL **game** renderer still
  loads symbols dynamically via `SDL_GL_GetProcAddress` in `gpu_gl_renderer.c`.
- Prefer MinGW shell PATH: `C:\msys64\mingw64\bin` first.
- Regen BIOS: `powershell -File tools/regen_bios.ps1`
- Regen game (Phase 1+): `powershell -File tools/regen.ps1`

## Build recipes used

```powershell
$env:Path = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:Path

# Recompiler
cmake -S psxrecomp/recompiler -B psxrecomp/recompiler/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build psxrecomp/recompiler/build -j

# BIOS
powershell -File tools/regen_bios.ps1

# Runtime (BIOS-only smoke)
cmake -S psxrecomp/runtime -B psxrecomp/runtime/build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DPSX_LAUNCHER=OFF
cmake --build psxrecomp/runtime/build --target psx-runtime -j
```
