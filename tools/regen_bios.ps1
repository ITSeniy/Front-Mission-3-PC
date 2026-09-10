$ErrorActionPreference = 'Stop'

# Windows equivalent of psxrecomp-master/tools/regen_bios.sh
$Root      = Split-Path -Parent $PSScriptRoot
$Framework = Join-Path $Root 'psxrecomp-master'
$Build     = Join-Path $Framework 'recompiler/build'
$Exe       = Join-Path $Build 'psxrecomp-bios.exe'
$Bios      = Join-Path $Framework 'bios/SCPH1001.BIN'
$Seeds     = Join-Path $Framework 'recompiler/seeds/phase2_ghidra_seeds.json'
$Out       = Join-Path $Framework 'generated'

if (!(Test-Path $Exe)) {
    throw "psxrecomp-bios not built: $Exe"
}
if (!(Test-Path $Bios)) {
    throw "BIOS not found: $Bios"
}
if (!(Test-Path $Seeds)) {
    throw "BIOS seeds not found: $Seeds"
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null

Write-Host "regen_bios: emit-full $Bios -> $Out"
& $Exe $Bios $Out --emit-full $Seeds
if ($LASTEXITCODE -ne 0) {
    throw "psxrecomp-bios exited with code $LASTEXITCODE"
}

$fp = Join-Path $Framework 'tools/bios_emitter_fingerprint.sh'
if (Test-Path $fp) {
    # Fingerprint script is bash; skip if no bash, runtime will rebuild when needed.
    $bash = 'C:\msys64\usr\bin\bash.exe'
    if (Test-Path $bash) {
        Push-Location $Framework
        try {
            & $bash tools/bios_emitter_fingerprint.sh | Set-Content -Encoding ascii (Join-Path $Out 'SCPH1001.emitter.sha')
            Write-Host "regen_bios: wrote fingerprint"
        } finally {
            Pop-Location
        }
    } else {
        Write-Host "regen_bios: bash not found; skipped emitter fingerprint"
    }
}

$dispatch = Join-Path $Out 'SCPH1001_dispatch.c'
if (Test-Path $dispatch) {
    $line = Select-String -Path $dispatch -Pattern 'Dispatch entries: \d+' | Select-Object -First 1
    Write-Host "regen_bios: done ($line)"
} else {
    Write-Host "regen_bios: done (no dispatch comment found)"
}
