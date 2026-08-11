# Scheduler throughput baseline for the multi-worker coroutine driver.
#
# A single run of the socket benchmark varies by ~25% on a loaded desktop, which
# is more than most scheduler changes are worth -- so every configuration is run
# -Repeats times and reported by its median. With -Stats the driver's per-worker
# counters (ZAN_CO_STATS) come back too, so a throughput change can be
# attributed to LIFO hits / steals / injector traffic instead of guessed at.
#
# Build the servers first (from the repo root):
#   build\zanc.exe _scratch\netbench\srvmt.zan --auto-stdlib -o _scratch\netbench\srvmt_st.exe
#   build\zanc.exe _scratch\netbench\srvmt.zan --auto-stdlib --async-workers -o _scratch\netbench\srvmt_mt.exe
#   build\zanc.exe _scratch\netbench\srvmt.zan --auto-stdlib --async-workers --fast-alloc -o _scratch\netbench\srvmt_mtfa.exe
#   build\zanc.exe _scratch\netbench\cli.zan   --auto-stdlib -o _scratch\netbench\cli.exe
#
# Example:
#   powershell -File scripts\bench_sched.ps1 -Servers srvmt_st.exe,srvmt_mt.exe `
#              -WorkerList 1,2,4,8,16 -Repeats 5 -Stats -Csv build\bench_sched.csv
#
# cli.exe cannot saturate the server (it costs more CPU per message than the
# server does), so throughput measured with it says more about the client than
# about the scheduler. For scheduler work drive it with the C load generator
# instead -- one message per send() is the wakeup-per-message case that stresses
# the scheduler, and us/msg below is then the number to compare:
#   gcc -O2 -o _scratch\netbench\loadgen.exe _scratch\netbench\loadgen.c -lws2_32
#   powershell -File scripts\bench_sched.ps1 -Client loadgen.exe -Clients 1 `
#              -ClientArgs "8 32 2200000 55801 1" -WorkerList 1,2,4,8 -Stats

param(
    [string]  $Root       = "_scratch\netbench",
    [string]  $Servers    = "srvmt_mt.exe",   # comma-separated, like -WorkerList
    [string]  $Client     = "cli.exe",
    [string]  $ClientArgs = "",       # e.g. loadgen.exe: "8 32 2200000 55801 1"
    [int]     $Clients    = 4,
    [string]  $WorkerList = "1,2,4,8,16",
    [int]     $Repeats    = 5,
    [int]     $Timeout    = 60,
    [switch]  $Stats,
    [string]  $Csv        = ""
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path $Root).Path
$procNames = @("srvmt_st", "srvmt_mt", "srvmt_mtfa", "srvmt2k_st", "srvmt2k_mt",
               "srv_mt", "srv", "cli", "cli_mt", "loadgen")

function Stop-Leftovers {
    Get-Process $procNames -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 400
}

# One measurement: start the server, start $Clients load generators, wait for the
# server to print its result and exit. Returns msg/s, server CPU seconds and the
# COSTAT lines (empty unless -Stats).
function Invoke-Run {
    param([string]$Server, [int]$Workers)

    Stop-Leftovers   # a process left over from an aborted run still holds its log
    $log = Join-Path $root "bench_srv.log"
    $err = Join-Path $root "bench_srv.err"
    Get-ChildItem (Join-Path $root "bench_srv.*"), (Join-Path $root "bench_cli.log*") `
        -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

    $env:ZAN_CO_WORKERS = "$Workers"
    if ($Stats) { $env:ZAN_CO_STATS = "1" } else { Remove-Item Env:\ZAN_CO_STATS -ErrorAction SilentlyContinue }

    $srv = Start-Process -FilePath (Join-Path $root $Server) -PassThru -WindowStyle Hidden `
                         -RedirectStandardOutput $log -RedirectStandardError $err
    Start-Sleep -Milliseconds 800
    for ($i = 0; $i -lt $Clients; $i++) {
        $cargs = @{}
        if ($ClientArgs -ne "") { $cargs["ArgumentList"] = $ClientArgs.Split(" ") }
        Start-Process -FilePath (Join-Path $root $Client) -WindowStyle Hidden @cargs `
                      -RedirectStandardOutput (Join-Path $root ("bench_cli.log" + $i)) | Out-Null
    }
    # WaitForExit on the object (not Wait-Process) keeps the process handle open, so
    # the CPU time below is still readable after the server has exited.
    $srv.WaitForExit($Timeout * 1000) | Out-Null
    $cpu = 0.0
    try { $srv.Refresh(); $cpu = $srv.TotalProcessorTime.TotalSeconds } catch {}
    $alive = -not $srv.HasExited
    Stop-Leftovers

    $out = (Get-Content $log -ErrorAction SilentlyContinue) -join " "
    $msgps = 0.0
    if ($out -match "msgps=([0-9.eE+]+)") { $msgps = [double]$Matches[1] }
    # Server CPU per message is the metric that survives a load generator too
    # weak to saturate: it says what the runtime spends per message regardless
    # of how fast the messages arrive.
    $msgs = 0.0
    if ($out -match "msgs=([0-9]+)") { $msgs = [double]$Matches[1] }
    $costat = @()
    if ($Stats) {
        $costat = @(Get-Content $err -ErrorAction SilentlyContinue |
                    Where-Object { $_ -match "^COSTAT" })
    }
    return [pscustomobject]@{
        MsgPerSec = $msgps
        Cpu       = $cpu
        CpuUsPerMsg = if ($msgs -gt 0) { $cpu * 1e6 / $msgs } else { 0.0 }
        TimedOut  = $alive
        Line      = $out
        CoStat    = $costat
    }
}

function Get-Median {
    param([double[]]$Values)
    $s = $Values | Sort-Object
    $n = $s.Count
    if ($n -eq 0) { return 0.0 }
    if ($n % 2 -eq 1) { return $s[[int](($n - 1) / 2)] }
    return ($s[$n / 2 - 1] + $s[$n / 2]) / 2.0
}

$rows = @()
Stop-Leftovers
foreach ($server in $Servers.Split(",")) {
    if (-not (Test-Path (Join-Path $root $server))) {
        Write-Output "SKIP $server (not built)"
        continue
    }
    # ZAN_CO_WORKERS only means something for a server built with --async-workers;
    # the single-threaded driver is measured once.
    $points = if ($server -match "_mt") { $WorkerList.Split(",") } else { @("1") }
    foreach ($w in $points) {
        $workers = [int]$w
        $samples = @(); $cpus = @(); $upm = @(); $timeouts = 0; $bad = 0; $last = $null
        for ($r = 0; $r -lt $Repeats; $r++) {
            $run = Invoke-Run -Server $server -Workers $workers
            if ($run.TimedOut) { $timeouts++ }
            if ($run.MsgPerSec -gt 0) { $samples += $run.MsgPerSec; $cpus += $run.Cpu; $upm += $run.CpuUsPerMsg }
            else { $bad++; Write-Output ("  no result: " + $run.Line) }
            $last = $run
        }
        $med = Get-Median $samples
        $row = [pscustomobject]@{
            Server   = $server
            Workers  = $workers
            Runs     = $samples.Count
            Median   = [math]::Round($med, 0)
            Min      = [math]::Round(($samples | Measure-Object -Minimum).Minimum, 0)
            Max      = [math]::Round(($samples | Measure-Object -Maximum).Maximum, 0)
            CpuMed   = [math]::Round((Get-Median $cpus), 2)
            CpuUsMsg = [math]::Round((Get-Median $upm), 3)
            TimedOut = $timeouts
            NoResult = $bad
        }
        $rows += $row
        Write-Output ("{0} workers={1} median={2} msg/s (min={3} max={4}) cpu={5}s {6}us/msg runs={7} timeouts={8}" -f `
                      $row.Server, $row.Workers, $row.Median, $row.Min, $row.Max, $row.CpuMed, $row.CpuUsMsg, $row.Runs, $row.TimedOut)
        if ($Stats -and $last) { $last.CoStat | ForEach-Object { Write-Output ("  " + $_) } }
    }
}

if ($Csv -ne "") {
    $rows | Export-Csv -NoTypeInformation -Encoding ascii -Path $Csv
    Write-Output ("csv=" + $Csv)
}
