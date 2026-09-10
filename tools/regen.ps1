$ErrorActionPreference = 'Stop'

$Root      = Split-Path -Parent $PSScriptRoot
$Framework = Join-Path $Root 'psxrecomp-master'
$Tool      = Join-Path $Framework 'recompiler/build/psxrecomp-game.exe'
$Config    = Join-Path $Root 'game.toml'

if (!(Test-Path $Tool)) {
    throw "psxrecomp-game not built: $Tool`nBuild with: cmake -S psxrecomp-master/recompiler -B psxrecomp-master/recompiler/build ..."
}
if (!(Test-Path $Config)) {
    throw "game.toml not found: $Config (Phase 1)"
}

Push-Location $Root
try {
    & $Tool --config $Config
    if ($LASTEXITCODE -ne 0) {
        throw "psxrecomp-game exited with code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
