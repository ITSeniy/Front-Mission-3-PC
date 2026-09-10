[CmdletBinding(PositionalBinding = $false)]
param(
    [switch]$LegacyScheduler,
    [switch]$NativeDebugger,
    [switch]$PrepareOnly,
    [string]$BuildDir = 'build-debug',
    [string]$Game = 'game.toml',
    [string]$Disc = 'fm3\fm3.cue',
    [string]$Bios = 'psxrecomp-master\bios\SCPH1001.BIN',
    [string]$ArtifactsRoot,
    [string]$GdbPath = 'C:\msys64\mingw64\bin\gdb.exe'
)

# Launch an instrumented FM3 build and preserve enough evidence to explain a
# native crash, runtime fatal, or clean-but-unexpected exit. Each invocation
# owns a timestamped directory under build-debug/debug-runs; runtime artifacts
# in build-debug are copied only when this invocation changed them.

$ErrorActionPreference = 'Stop'
$RepoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))

function Resolve-RepoPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description,
        [switch]$Directory
    )

    $candidate = if ([IO.Path]::IsPathRooted($Path)) {
        $Path
    } else {
        Join-Path $RepoRoot $Path
    }
    $candidate = [IO.Path]::GetFullPath($candidate)
    $pathType = if ($Directory) { 'Container' } else { 'Leaf' }
    if (-not (Test-Path -LiteralPath $candidate -PathType $pathType)) {
        throw "$Description not found: $candidate"
    }
    return $candidate
}

function ConvertTo-NativeArgument {
    param([AllowEmptyString()][string]$Value)

    if ($Value.Length -eq 0) {
        return '""'
    }
    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    # Windows CommandLineToArgvW/CRT quoting: double backslashes before a
    # quote and at the end of a quoted argument.
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Get-FileSignature {
    param([Parameter(Mandatory = $true)][IO.FileInfo]$File)
    return '{0}:{1}' -f $File.Length, $File.LastWriteTimeUtc.Ticks
}

function Get-DiagnosticFiles {
    $names = @(
        'psx_last_run_report.json',
        'psx_freeze_heartbeat.json',
        'psx_crash.txt',
        'psx_cps_exit_trace.json',
        'starvation_dump.jsonl',
        'post_training_scheduler_capture.json',
        'crash_with_wtrace_capture.json'
    )

    $files = foreach ($name in $names) {
        Get-Item -LiteralPath (Join-Path $BuildPath $name) -ErrorAction SilentlyContinue
    }
    $files += Get-ChildItem -LiteralPath $BuildPath -File -Filter 'psx_freeze_dump_*.json' -ErrorAction SilentlyContinue
    return @($files | Sort-Object -Property FullName -Unique)
}

function Copy-DiagnosticFile {
    param(
        [Parameter(Mandatory = $true)][IO.FileInfo]$File,
        [switch]$KeepReportVersion
    )

    $signature = Get-FileSignature $File
    if ($InitialSignatures[$File.FullName] -eq $signature -or
        $CopiedSignatures[$File.FullName] -eq $signature) {
        return $false
    }

    $destination = Join-Path $ArtifactPath $File.Name
    try {
        Copy-Item -LiteralPath $File.FullName -Destination $destination -Force
        $CopiedSignatures[$File.FullName] = $signature

        if ($KeepReportVersion) {
            $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
            if (-not $ReportHashes.ContainsKey($hash)) {
                $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
                $versioned = Join-Path $ReportPath ("psx_last_run_report-{0}-{1}.json" -f $stamp, $hash.Substring(0, 12))
                Copy-Item -LiteralPath $destination -Destination $versioned
                $ReportHashes[$hash] = $true
                Write-Host "[diag] captured crash report: $versioned" -ForegroundColor Yellow
            }
        }
        return $true
    } catch {
        # The heartbeat and large freeze dumps can still be in the middle of an
        # atomic replacement. A later poll/final pass will retry them.
        Write-Warning "Could not snapshot $($File.Name): $($_.Exception.Message)"
        return $false
    }
}

function Update-LiveDiagnostics {
    foreach ($name in @('psx_last_run_report.json', 'psx_crash.txt')) {
        $file = Get-Item -LiteralPath (Join-Path $BuildPath $name) -ErrorAction SilentlyContinue
        if ($null -ne $file) {
            $changed = Copy-DiagnosticFile -File $file -KeepReportVersion:($name -eq 'psx_last_run_report.json')
            if ($changed -and $name -eq 'psx_last_run_report.json') {
                $liveSummary = Write-RunSummary -ExitCode $null -State 'process_running'
                Write-Host "[diag] live classification: $($liveSummary.classification)" -ForegroundColor Yellow
            }
        }
    }

    # Fatal-halt builds deliberately stay alive for TCP post-mortem queries.
    # Copy large self-contained dumps once their size/timestamp remain stable
    # for two polls, so the session becomes useful before the process exits.
    $stableCandidates = @(
        Get-ChildItem -LiteralPath $BuildPath -File -Filter 'psx_freeze_dump_*.json' -ErrorAction SilentlyContinue
        Get-Item -LiteralPath (Join-Path $BuildPath 'post_training_scheduler_capture.json') -ErrorAction SilentlyContinue
        Get-Item -LiteralPath (Join-Path $BuildPath 'crash_with_wtrace_capture.json') -ErrorAction SilentlyContinue
    )
    foreach ($file in $stableCandidates) {
        $signature = Get-FileSignature $file
        if ($PendingLiveSignatures[$file.FullName] -eq $signature) {
            [void](Copy-DiagnosticFile -File $file)
        } else {
            $PendingLiveSignatures[$file.FullName] = $signature
        }
    }
}

function Save-FinalDiagnostics {
    foreach ($file in Get-DiagnosticFiles) {
        [void](Copy-DiagnosticFile -File $file -KeepReportVersion:($file.Name -eq 'psx_last_run_report.json'))
    }
}

function Get-JsonHeaderString {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $pattern = '(?m)^\s*"' + [regex]::Escape($Name) + '"\s*:\s*("(?:\\.|[^"\\])*")'
    $match = [regex]::Match($Text, $pattern)
    if (-not $match.Success) {
        return $null
    }
    # Decode only the matched JSON string. PowerShell 5.1 is extremely slow on
    # the multi-megabyte ring arrays in the complete report, but parsing this
    # tiny object is fast and preserves escaped newlines/backslashes exactly.
    $decoded = ('{"value":' + $match.Groups[1].Value + '}') | ConvertFrom-Json
    return [string]$decoded.value
}

function Get-JsonHeaderNumber {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $pattern = '(?m)^\s*"' + [regex]::Escape($Name) + '"\s*:\s*(-?[0-9]+)'
    $match = [regex]::Match($Text, $pattern)
    if (-not $match.Success) {
        return $null
    }
    return [long]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
}

function Write-RunSummary {
    param(
        [AllowNull()][Nullable[int]]$ExitCode,
        [Parameter(Mandatory = $true)][string]$State
    )

    $summary = [ordered]@{
        state = $State
        process_exit_code = $ExitCode
        finished_utc = [DateTime]::UtcNow.ToString('o')
        session_dir = $SessionPath
        report_present = $false
        classification = if ($State -eq 'prepared') { 'not_started' } else { 'no_runtime_report' }
    }

    $stderrFile = Join-Path $SessionPath 'stderr.log'
    if (Test-Path -LiteralPath $stderrFile -PathType Leaf) {
        $stderrTail = @(
            Get-Content -LiteralPath $stderrFile -Tail 40 -ErrorAction SilentlyContinue |
                Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        )
        if ($stderrTail.Count -gt 0) {
            $summary.stderr_tail = $stderrTail
        }
    }

    $reportFile = Join-Path $ArtifactPath 'psx_last_run_report.json'
    if (Test-Path -LiteralPath $reportFile -PathType Leaf) {
        $summary.report_present = $true
        try {
            # Everything needed for triage is emitted before the large ring
            # arrays. Reading a bounded prefix avoids PowerShell 5.1's very
            # slow ConvertFrom-Json path on a full crash report.
            $reportHead = (Get-Content -LiteralPath $reportFile -TotalCount 180) -join "`n"
            $reason = Get-JsonHeaderString -Text $reportHead -Name 'reason'
            if ($null -eq $reason) {
                throw 'The crash report header has no valid JSON reason field.'
            }
            $summary.reason = $reason
            $summary.exit_origin = Get-JsonHeaderString -Text $reportHead -Name 'exit_origin'
            $summary.build = Get-JsonHeaderString -Text $reportHead -Name 'build'
            $summary.frame = Get-JsonHeaderNumber -Text $reportHead -Name 'frame'
            $summary.dispatch_depth = Get-JsonHeaderNumber -Text $reportHead -Name 'dispatch_depth'
            $summary.guest_pc = Get-JsonHeaderString -Text $reportHead -Name 'pc'
            $summary.guest_epc = Get-JsonHeaderString -Text $reportHead -Name 'epc'
            $summary.last_func_addr = Get-JsonHeaderString -Text $reportHead -Name 'last_func_addr'
            $summary.last_store_pc = Get-JsonHeaderString -Text $reportHead -Name 'last_store_pc'

            if ($reportHead -match '(?m)^\s*"seh"\s*:\s*\{') {
                $summary.classification = 'native_exception'
                $summary.seh_code = Get-JsonHeaderString -Text $reportHead -Name 'code'
                $summary.fault_module = Get-JsonHeaderString -Text $reportHead -Name 'module'
                $summary.fault_module_offset = Get-JsonHeaderString -Text $reportHead -Name 'module_offset'
                $summary.fault_access = Get-JsonHeaderString -Text $reportHead -Name 'access'
                $summary.fault_address = Get-JsonHeaderString -Text $reportHead -Name 'fault_addr'
            } elseif ($reason -match '^signal_') {
                $summary.classification = 'native_signal'
            } elseif ($reason -eq 'atexit') {
                if ($ExitCode -eq 0) {
                    $summary.classification = 'clean_exit'
                } elseif (($stderrTail -join "`n") -match '(?i)failed to load --game|TOML syntax error') {
                    $summary.classification = 'config_load_failure'
                } elseif (($stderrTail -join "`n") -match '(?i)psxrecomp: failed to|fatal error') {
                    $summary.classification = 'startup_failure'
                } else {
                    $summary.classification = 'abnormal_exit'
                }
            } elseif ($reason -match '(?i)dispatch|unknown|misaligned') {
                $summary.classification = 'guest_dispatch_fatal'
            } else {
                $summary.classification = 'runtime_fatal'
            }
        } catch {
            $summary.classification = 'invalid_runtime_report'
            $summary.report_parse_error = $_.Exception.Message
        }
    } elseif ($null -ne $ExitCode) {
        $summary.classification = if ($ExitCode -eq 0) { 'clean_exit_without_report' } else { 'native_or_startup_failure' }
    }

    # Materialize a plain hashtable before serialization so PowerShell 5.1 does
    # not walk OrderedDictionary adapter metadata.
    $plainSummary = @{}
    foreach ($entry in $summary.GetEnumerator()) {
        $plainSummary[[string]$entry.Key] = $entry.Value
    }
    $summaryJson = $plainSummary | ConvertTo-Json -Depth 4
    [IO.File]::WriteAllText((Join-Path $SessionPath 'summary.json'), $summaryJson, (New-Object Text.UTF8Encoding($false)))
    return [pscustomobject]$summary
}

$BuildPath = Resolve-RepoPath -Path $BuildDir -Description 'Build directory' -Directory
$GamePath = Resolve-RepoPath -Path $Game -Description 'Game config'
$DiscPath = Resolve-RepoPath -Path $Disc -Description 'Disc image'
$BiosPath = Resolve-RepoPath -Path $Bios -Description 'BIOS image'
$ExePath = Resolve-RepoPath -Path (Join-Path $BuildPath 'Front_Mission_3_Recompiled.exe') -Description 'Debug executable'

if ($NativeDebugger) {
    $GdbPath = Resolve-RepoPath -Path $GdbPath -Description 'GDB executable'
}

if ([string]::IsNullOrWhiteSpace($ArtifactsRoot)) {
    $ArtifactsRoot = Join-Path $BuildPath 'debug-runs'
} elseif (-not [IO.Path]::IsPathRooted($ArtifactsRoot)) {
    $ArtifactsRoot = Join-Path $RepoRoot $ArtifactsRoot
}
$ArtifactsRoot = [IO.Path]::GetFullPath($ArtifactsRoot)
[IO.Directory]::CreateDirectory($ArtifactsRoot) | Out-Null

$SessionId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$SessionPath = Join-Path $ArtifactsRoot $SessionId
$ArtifactPath = Join-Path $SessionPath 'artifacts'
$ReportPath = Join-Path $SessionPath 'reports'
[IO.Directory]::CreateDirectory($ArtifactPath) | Out-Null
[IO.Directory]::CreateDirectory($ReportPath) | Out-Null

$InitialSignatures = @{}
$CopiedSignatures = @{}
$ReportHashes = @{}
$PendingLiveSignatures = @{}
foreach ($file in Get-DiagnosticFiles) {
    $InitialSignatures[$file.FullName] = Get-FileSignature $file
}

$RuntimeArgs = @(
    '--no-launcher',
    '--game', $GamePath,
    '--disc', $DiscPath,
    '--bios', $BiosPath
) + @($args)

$LaunchFile = $ExePath
$LaunchArgs = $RuntimeArgs
if ($NativeDebugger) {
    $gdbCommandsPath = Join-Path $SessionPath 'gdb_commands.txt'
    @(
        'set pagination off',
        'set confirm off',
        'set print thread-events off',
        'handle SIGSEGV stop print pass',
        'handle SIGABRT stop print pass',
        'run',
        'if $_isvoid($_exitcode)',
        '  echo \n===== FM3 NATIVE CRASH SNAPSHOT =====\n',
        '  thread apply all bt full',
        '  info registers',
        '  x/16i $pc-16',
        '  continue',
        'end',
        'if $_isvoid($_exitcode)',
        '  quit 1',
        'end',
        'quit $_exitcode'
    ) | Set-Content -LiteralPath $gdbCommandsPath -Encoding ASCII

    $LaunchFile = $GdbPath
    $LaunchArgs = @(
        '--batch',
        '--quiet',
        '-x', $gdbCommandsPath,
        '--args', $ExePath
    ) + $RuntimeArgs
}

$cacheInfo = @{}
$cachePath = Join-Path $BuildPath 'CMakeCache.txt'
if (Test-Path -LiteralPath $cachePath) {
    foreach ($line in Get-Content -LiteralPath $cachePath) {
        if ($line -match '^(CMAKE_BUILD_TYPE|PSX_DEBUG_TOOLS|CMAKE_C_COMPILER):[^=]*=(.*)$') {
            $cacheInfo[$matches[1]] = $matches[2]
        }
    }
}

$gitRevision = (& git -C $RepoRoot rev-parse --short HEAD 2>$null)
$manifest = [ordered]@{
    session_id = $SessionId
    started_utc = [DateTime]::UtcNow.ToString('o')
    repository = $RepoRoot
    git_revision = if ($LASTEXITCODE -eq 0) { [string]$gitRevision } else { 'unknown' }
    executable = $ExePath
    executable_sha256 = (Get-FileHash -LiteralPath $ExePath -Algorithm SHA256).Hash.ToLowerInvariant()
    executable_size = (Get-Item -LiteralPath $ExePath).Length
    build = $cacheInfo
    scheduler = if ($LegacyScheduler) { 'legacy' } else { 'hle' }
    native_debugger = [bool]$NativeDebugger
    working_directory = $BuildPath
    launch_file = $LaunchFile
    arguments = $LaunchArgs
    environment = [ordered]@{
        PSX_EXIT_HALT = '1'
        PSX_HLE_SCHEDULER = if ($LegacyScheduler) { '0' } else { '1' }
    }
    powershell = $PSVersionTable.PSVersion.ToString()
    machine = [Environment]::MachineName
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $SessionPath 'manifest.json') -Encoding UTF8

Write-Host "Debug session: $SessionPath" -ForegroundColor Cyan
Write-Host "Scheduler: $($manifest.scheduler); native debugger: $NativeDebugger"
if ($PrepareOnly) {
    $prepared = Write-RunSummary -ExitCode $null -State 'prepared'
    Write-Host 'Preparation complete; process was not started.'
    return
}

$psi = New-Object Diagnostics.ProcessStartInfo
$psi.FileName = $LaunchFile
$psi.Arguments = (($LaunchArgs | ForEach-Object { ConvertTo-NativeArgument ([string]$_) }) -join ' ')
$psi.WorkingDirectory = $BuildPath
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.EnvironmentVariables['PATH'] = 'C:\msys64\mingw64\bin;' + $psi.EnvironmentVariables['PATH']
$psi.EnvironmentVariables['PSX_EXIT_HALT'] = '1'
$psi.EnvironmentVariables['PSX_HLE_SCHEDULER'] = if ($LegacyScheduler) { '0' } else { '1' }

$stdoutPath = Join-Path $SessionPath 'stdout.log'
$stderrPath = Join-Path $SessionPath 'stderr.log'

# Keep asynchronous pipe callbacks entirely in .NET. PowerShell 5.1 can leave
# ReadLineAsync tasks pending after the child exits. Dedicated background .NET
# threads drain both pipes without running PowerShell callbacks. Their final
# join is deliberately bounded because a descendant can inherit a pipe handle
# and keep an EOF-based reader blocked forever.
if (-not ('Fm3Debug.ProcessCapture' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;

namespace Fm3Debug
{
    public sealed class ProcessCapture : IDisposable
    {
        private readonly StreamWriter stdout;
        private readonly StreamWriter stderr;
        private readonly object stdoutLock = new object();
        private readonly object stderrLock = new object();
        private Thread stdoutThread;
        private Thread stderrThread;

        public Process Process { get; private set; }

        public ProcessCapture(ProcessStartInfo startInfo, string stdoutPath, string stderrPath)
        {
            stdout = new StreamWriter(stdoutPath, false, new UTF8Encoding(false));
            stderr = new StreamWriter(stderrPath, false, new UTF8Encoding(false));
            Process = new Process();
            Process.StartInfo = startInfo;
        }

        public bool Start()
        {
            bool started = Process.Start();
            if (started)
            {
                stdoutThread = new Thread(PumpStdout);
                stderrThread = new Thread(PumpStderr);
                stdoutThread.IsBackground = true;
                stderrThread.IsBackground = true;
                stdoutThread.Start();
                stderrThread.Start();
            }
            return started;
        }

        private void PumpStdout()
        {
            try
            {
                string line;
                while ((line = Process.StandardOutput.ReadLine()) != null)
                {
                    lock (stdoutLock)
                    {
                        stdout.WriteLine(line);
                        stdout.Flush();
                    }
                    Console.Out.WriteLine(line);
                }
            }
            catch (IOException) { }
            catch (ObjectDisposedException) { }
            catch (InvalidOperationException) { }
        }

        private void PumpStderr()
        {
            try
            {
                string line;
                while ((line = Process.StandardError.ReadLine()) != null)
                {
                    lock (stderrLock)
                    {
                        stderr.WriteLine(line);
                        stderr.Flush();
                    }
                    Console.Error.WriteLine(line);
                }
            }
            catch (IOException) { }
            catch (ObjectDisposedException) { }
            catch (InvalidOperationException) { }
        }

        public void Complete()
        {
            if (stdoutThread != null) stdoutThread.Join(100);
            if (stderrThread != null) stderrThread.Join(100);
            lock (stdoutLock) stdout.Flush();
            lock (stderrLock) stderr.Flush();
        }

        public void Dispose()
        {
            bool stdoutDone = stdoutThread == null || !stdoutThread.IsAlive;
            bool stderrDone = stderrThread == null || !stderrThread.IsAlive;
            lock (stdoutLock)
            {
                if (stdoutDone) stdout.Dispose(); else stdout.Flush();
            }
            lock (stderrLock)
            {
                if (stderrDone) stderr.Dispose(); else stderr.Flush();
            }
            // A background reader can remain blocked if another process
            // inherited the write handle. It is intentionally left for OS
            // cleanup; closing its StreamReader here deadlocks .NET 4.x.
            if (stdoutDone && stderrDone) Process.Dispose();
        }
    }
}
'@
}

$capture = [Fm3Debug.ProcessCapture]::new($psi, $stdoutPath, $stderrPath)
$process = $capture.Process
$exitCode = $null

try {
    if (-not $capture.Start()) {
        throw "Failed to start: $LaunchFile"
    }
    Write-Host "Process id: $($process.Id)"
    Write-Host 'The runtime stays alive on deliberate fatal halts so TCP state remains queryable.'

    $nextDiagPoll = [DateTime]::UtcNow

    while (-not $process.HasExited) {
        if ([DateTime]::UtcNow -ge $nextDiagPoll) {
            Update-LiveDiagnostics
            $nextDiagPoll = [DateTime]::UtcNow.AddMilliseconds(500)
        }
        Start-Sleep -Milliseconds 100
    }

    $capture.Complete()
    $exitCode = $process.ExitCode
} finally {
    $capture.Dispose()
    Save-FinalDiagnostics
    $state = if ($null -eq $exitCode) { 'wrapper_interrupted' } else { 'process_exited' }
    $summary = Write-RunSummary -ExitCode $exitCode -State $state
}

Write-Host "Session complete: $SessionPath" -ForegroundColor Cyan
Write-Host "Classification: $($summary.classification); exit code: $exitCode"
if ($summary.reason) {
    Write-Host "Reason: $($summary.reason)" -ForegroundColor Yellow
}

if ($null -ne $exitCode -and $exitCode -ne 0) {
    exit $exitCode
}
