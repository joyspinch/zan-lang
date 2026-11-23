#Requires -Version 7.2
[CmdletBinding(DefaultParameterSetName = 'Duration')]
param(
    [Parameter(Mandatory)]
    [ValidateRange(1, 2147483647)]
    [int]$TargetPid,

    [Parameter(Mandatory, ParameterSetName = 'Duration')]
    [ValidateRange(1, 86400)]
    [int]$DurationSec,

    [Parameter(Mandatory, ParameterSetName = 'Workload')]
    [string]$WorkloadExe,
    [Parameter(ParameterSetName = 'Workload')]
    [string[]]$WorkloadArgs = @(),
    [Parameter(ParameterSetName = 'Workload')]
    [ValidateRange(1, 86400)]
    [int]$WorkloadTimeoutSec = 300,

    [ValidateSet('CPU', 'CPU+VirtualAllocation', 'Counters')]
    [string]$Profile = 'CPU',
    [ValidateRange(20, 5000)]
    [int]$SampleMs = 250,
    [string]$OutputDir = (Join-Path $PSScriptRoot '..\build\profile')
)

$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'This collector requires Windows.' }

function Invoke-TraceTool([string]$Executable, [string[]]$Arguments) {
    # Some Windows Performance Toolkit versions emit diagnostics on stderr.
    $ErrorActionPreference = 'Continue'
    $PSNativeCommandUseErrorActionPreference = $false
    $output = & $Executable @Arguments 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw "$(Split-Path -Leaf $Executable) exited with code ${code}: $($output -join ' ')"
    }
    Write-Verbose ($output -join "`n")
}

function Write-TraceReports([string]$Xperf, [string]$EtlPath, [string]$RunDir, [int]$ProcessId,
    [string]$StartUtc, [string]$EndUtc, [bool]$VirtualAllocation) {
    $range = @('-range')
    foreach ($timestamp in $StartUtc, $EndUtc) {
        $utc = [DateTime]::Parse($timestamp, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
        $range += $utc.ToString("yyyy/MM/dd:HH:mm:ss.fffffff'+UTC'", [Globalization.CultureInfo]::InvariantCulture)
    }

    Invoke-TraceTool $Xperf @('-i', $EtlPath, '-o', (Join-Path $RunDir 'trace-stats.txt'), '-a', 'tracestats', '-timespan', 'actual')
    Invoke-TraceTool $Xperf (@('-i', $EtlPath, '-o', (Join-Path $RunDir 'target-cpu-stacks.html'), '-symbols',
        '-a', 'stack', '-pid', "$ProcessId", '-event', 'Sampled Profile', '-butterfly', '1') + $range)
    Invoke-TraceTool $Xperf @('-i', $EtlPath, '-o', (Join-Path $RunDir 'system-cpu.txt'), '-a', 'profile', '-detail')
    Invoke-TraceTool $Xperf @('-i', $EtlPath, '-o', (Join-Path $RunDir 'system-scheduling.txt'), '-a', 'cswitch', '-process', '-thread')
    if ($VirtualAllocation) {
        Invoke-TraceTool $Xperf (@('-i', $EtlPath, '-o', (Join-Path $RunDir 'virtualalloc.txt'), '-symbols',
            '-a', 'virtualalloc', '-pid', "$ProcessId", '-stacks', 'sc', '-top', '100') + $range)
    }
}

function Get-ResourceSample([Diagnostics.Process]$Process, [Diagnostics.Stopwatch]$Clock) {
    $Process.Refresh()
    if ($Process.HasExited) { throw "Target process $TargetPid exited during capture." }
    return [pscustomobject]@{
        timestamp_utc = [DateTime]::UtcNow.ToString('o')
        elapsed_ms = [math]::Round($Clock.Elapsed.TotalMilliseconds, 3)
        user_cpu_ms = $Process.UserProcessorTime.TotalMilliseconds
        kernel_cpu_ms = $Process.PrivilegedProcessorTime.TotalMilliseconds
        working_set_bytes = $Process.WorkingSet64
        private_bytes = $Process.PrivateMemorySize64
        virtual_bytes = $Process.VirtualMemorySize64
        thread_count = $Process.Threads.Count
        handle_count = $Process.HandleCount
    }
}

function Read-WorkloadResult([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw 'The workload did not write its JSON result to ZAN_PROFILE_RESULT.'
    }
    $result = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -AsHashtable -NoEnumerate
    if ($result -isnot [Collections.IDictionary] -or
        ($result.completed -isnot [long] -and $result.completed -isnot [int]) -or
        ($result.errors -isnot [long] -and $result.errors -isnot [int]) -or
        ($result.elapsed_ms -isnot [double] -and $result.elapsed_ms -isnot [long] -and $result.elapsed_ms -isnot [int]) -or
        -not [double]::IsFinite($result.elapsed_ms) -or $result.elapsed_ms -le 0 -or
        $result.completed -lt 0 -or $result.errors -lt 0 -or ($result.completed -eq 0 -and $result.errors -eq 0)) {
        throw 'Workload result requires nonnegative integer completed/errors and finite positive elapsed_ms.'
    }
    return $result
}

$recordEtw = $Profile -ne 'Counters'
if ($recordEtw) {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'WPR kernel capture requires an elevated PowerShell. Use -Profile Counters for process counters only.'
        }
    } finally {
        $identity.Dispose()
    }
    $wpr = (Get-Command wpr.exe -CommandType Application).Source
    $xperf = Get-Command xperf.exe -CommandType Application -ErrorAction SilentlyContinue
}

$runWorkload = $PSCmdlet.ParameterSetName -eq 'Workload'
if ($runWorkload) {
    $workloadPath = (Resolve-Path -LiteralPath $WorkloadExe).ProviderPath
    if (-not (Test-Path -LiteralPath $workloadPath -PathType Leaf)) { throw 'WorkloadExe must be an executable file.' }
}
$target = Get-Process -Id $TargetPid
$workload = $null
$recording = $false
$runId = [guid]::NewGuid().ToString('N')
$instance = "zan-$runId"
$runDir = Join-Path ($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)) $runId
$etlPath = Join-Path $runDir 'trace.etl'
$resourcePath = Join-Path $runDir 'process-resources.csv'
$resultPath = Join-Path $runDir 'workload.json'
$summaryPath = Join-Path $runDir 'summary.json'
$samples = [Collections.Generic.List[object]]::new()
$clock = [Diagnostics.Stopwatch]::new()
$workloadExit = $null

try {
    # Hold the original process handle so a recycled PID cannot enter the sample.
    $null = $target.Handle
    $targetName = $target.ProcessName
    New-Item -ItemType Directory -Path $runDir | Out-Null
    Write-Host "Profile directory: $runDir"
    if ($recordEtw) {
        $wprArgs = @('-start', 'CPU.Verbose')
        if ($Profile -eq 'CPU+VirtualAllocation') { $wprArgs += @('-start', 'VirtualAllocation.Verbose') }
        Invoke-TraceTool $wpr ($wprArgs + @('-filemode', '-recordtempto', $runDir, '-instancename', $instance))
        $recording = $true
    }

    $clock.Start()
    $samples.Add((Get-ResourceSample $target $clock))
    if ($runWorkload) {
        $launch = [Diagnostics.ProcessStartInfo]::new($workloadPath)
        $launch.WorkingDirectory = $PWD.ProviderPath
        $launch.UseShellExecute = $false
        foreach ($argument in $WorkloadArgs) { $launch.ArgumentList.Add($argument) }
        $launch.Environment['ZAN_PROFILE_RESULT'] = $resultPath
        $workload = [Diagnostics.Process]::Start($launch)
        $limitMs = $WorkloadTimeoutSec * 1000.0
    } else {
        $limitMs = $DurationSec * 1000.0
    }

    while ($true) {
        $remainingMs = [math]::Ceiling($limitMs - $clock.Elapsed.TotalMilliseconds)
        if ($remainingMs -le 0) {
            if ($runWorkload -and -not $workload.HasExited) { throw 'Workload timed out.' }
            break
        }
        $interval = [int][math]::Min($SampleMs, $remainingMs)
        if ($runWorkload) {
            if ($workload.WaitForExit($interval)) { break }
        } else {
            Start-Sleep -Milliseconds $interval
        }
        if ($clock.Elapsed.TotalMilliseconds -ge $limitMs) { continue }
        $samples.Add((Get-ResourceSample $target $clock))
    }
    $samples.Add((Get-ResourceSample $target $clock))
    if ($runWorkload) { $workloadExit = $workload.ExitCode }
    if ($recording) {
        Invoke-TraceTool $wpr @('-stop', $etlPath, 'Zan process profile', '-skipPdbGen', '-instancename', $instance)
        $recording = $false
        if (-not (Test-Path -LiteralPath $etlPath -PathType Leaf) -or (Get-Item -LiteralPath $etlPath).Length -eq 0) {
            throw 'WPR exited successfully without saving a nonempty ETL.'
        }
    }
} finally {
    try {
        if ($recording) {
            try { Invoke-TraceTool $wpr @('-cancel', '-instancename', $instance) }
            catch { Write-Warning "Could not cancel WPR instance ${instance}: $_" }
        }
        if ($null -ne $workload) {
            try {
                if (-not $workload.HasExited) {
                    $workload.Kill($true)
                    if (-not $workload.WaitForExit(5000)) { Write-Warning 'Workload process did not exit after termination.' }
                }
            } catch {
                if (-not $workload.HasExited) { Write-Warning "Could not stop workload process: $_" }
            } finally { $workload.Dispose() }
        }
        if ($samples.Count -gt 0) {
            $samples | Export-Csv -LiteralPath $resourcePath -NoTypeInformation -Encoding utf8
        }
    } finally {
        $target.Dispose()
    }
}

$result = $null
if ($runWorkload) {
    if ($workloadExit -ne 0) { throw "Workload exited with code $workloadExit. See $runDir." }
    $result = Read-WorkloadResult $resultPath
}
$first = $samples[0]
$last = $samples[$samples.Count - 1]
$userMs = $last.user_cpu_ms - $first.user_cpu_ms
$kernelMs = $last.kernel_cpu_ms - $first.kernel_cpu_ms
$cpuMs = $userMs + $kernelMs
$completed = if ($null -ne $result) { $result.completed }
$summary = [ordered]@{
    target_pid = $TargetPid
    target_name = $targetName
    profile = $Profile
    start_utc = $first.timestamp_utc
    end_utc = $last.timestamp_utc
    duration_ms = $last.elapsed_ms - $first.elapsed_ms
    sample_count = $samples.Count
    sample_interval_ms = $SampleMs
    user_cpu_ms = $userMs
    kernel_cpu_ms = $kernelMs
    cpu_ms = $cpuMs
    cpu_us_per_completion = if ($completed -gt 0 -and $result.errors -eq 0) { [math]::Round(1000.0 * $cpuMs / $completed, 3) }
    sampled_peak_working_set_bytes = ($samples | Measure-Object working_set_bytes -Maximum).Maximum
    sampled_peak_private_bytes = ($samples | Measure-Object private_bytes -Maximum).Maximum
    private_bytes_delta = $last.private_bytes - $first.private_bytes
    sampled_peak_virtual_bytes = ($samples | Measure-Object virtual_bytes -Maximum).Maximum
    completed = $completed
    errors = if ($null -ne $result) { $result.errors }
    workload_ms = if ($null -ne $result) { $result.elapsed_ms }
    completions_per_second = if ($null -ne $result) { [math]::Round(1000.0 * $completed / $result.elapsed_ms, 2) }
    trace = if ($recordEtw) { 'trace.etl' }
}
$summary | ConvertTo-Json | Set-Content -LiteralPath $summaryPath -Encoding utf8

if ($recordEtw -and $null -ne $xperf) {
    Write-TraceReports $xperf.Source $etlPath $runDir $TargetPid $first.timestamp_utc $last.timestamp_utc ($Profile -eq 'CPU+VirtualAllocation')
}

Write-Output $summaryPath
