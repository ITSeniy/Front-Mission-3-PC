$ErrorActionPreference = 'Stop'

# Compile captured overlays from the last play session into build/cache.
# Run from project root. Requires MinGW gcc + built psxrecomp-game.

$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$env:Path = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:Path

$Captures = Join-Path $Root 'build/overlay_captures.json'
if (!(Test-Path $Captures)) {
    throw "No captures at $Captures - play the game first so overlays are recorded."
}

$Recompiler = Join-Path $Root 'psxrecomp/recompiler/build/psxrecomp-game.exe'
if (!(Test-Path $Recompiler)) {
    throw "Recompiler not built: $Recompiler"
}

$OutDir = Join-Path $Root 'build/cache'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

python (Join-Path $Root 'psxrecomp/tools/compile_overlays.py') `
    --captures $Captures `
    --game-toml (Join-Path $Root 'game.toml') `
    --recompiler $Recompiler `
    --runtime-include (Join-Path $Root 'psxrecomp/runtime/include') `
    --out-dir $OutDir `
    --gcc 'C:/msys64/mingw64/bin/gcc.exe'

Write-Host "Done. Restart the game so it picks up build/cache."
