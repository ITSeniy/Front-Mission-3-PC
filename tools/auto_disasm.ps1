# auto_disasm.ps1
# Полная автоматическая система декомпиляции PSX (EXE + OVL) для Front Mission 3
# Запуск: .\tools\auto_disasm.ps1
# Опционально: .\tools\auto_disasm.ps1 -OnlyExe -NoOverlays

param(
    [switch]$OnlyExe,
    [switch]$NoOverlays,
    [string]$OutputDir = "generated/disasm"
)

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ExePath    = Join-Path $ProjectRoot "fm3/SLUS_010.11"
$DiscPath   = Join-Path $ProjectRoot "Front Mission 3 (USA).cue"
$BiosRef    = Join-Path $ProjectRoot "psxrecomp-master/docs/psx_bios_disasm.txt"

if (-not (Test-Path $ExePath)) {
    Write-Host "❌ EXE не найден: $ExePath" -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Write-Host "🚀 Запуск полной автоматической декомпиляции..." -ForegroundColor Cyan
Write-Host "EXE: $(Split-Path $ExePath -Leaf)" -ForegroundColor Gray

# === Этап 1: Извлечение EXE ===
& "$ProjectRoot/psxrecomp-master/tools/extract_psx_exe.py" $ExePath

# === Этап 2: Декомпиляция EXE (EPC + full) ===
& "$ProjectRoot/psxrecomp-master/tools/crash_epc_disasm.py" --exe $ExePath --output "$OutputDir/exe"

# === Этап 3: Декомпиляция оверлеев (если есть диск) ===
if (-not $NoOverlays -and (Test-Path $DiscPath)) {
    Write-Host "Обнаружен диск, декомпиляция оверлеев..." -ForegroundColor Cyan
    & "$ProjectRoot/psxrecomp-master/tools/crash_overlay_disasm.py" --disc $DiscPath --output "$OutputDir/ovl"
} elseif ($NoOverlays) {
    Write-Host "Оверлеи пропущены по запросу" -ForegroundColor Yellow
} else {
    Write-Host "Диск не найден — оверлеи пропущены" -ForegroundColor Yellow
}

# === Этап 4: Генерация отчёта ===
$Report = @"
# Автоматическая декомпиляция Front Mission 3 (USA)
**Дата:** $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
**EXE:** $(Split-Path $ExePath -Leaf)
**Размер текста:** $(Split-Path $ExePath -Leaf | ForEach-Object { (Get-Item $ExePath).Length } / 1MB) МБ
**Генератор:** auto_disasm.ps1

## Содержимое папки $OutputDir
- exe/          — полная декомпиляция основного EXE (dispatch.c + full.c)
- ovl/          — декомпиляция оверлеев (если диск был)
- disasm_summary.txt — краткий отчёт

**Ссылки на эталон:**
- BIOS disassembly: $BiosRef

---
"@
$Report | Out-File "$OutputDir/disasm_summary.txt" -Encoding UTF8