# Exercise the real IDE window, Start page and theme drawer in one owned session.
param(
    [string]$Executable = 'build/cases/ide-retained-input.exe',
    [switch]$ProjectOverflowOnly
)
# UiDriver supports startup client-dev only. Later sizes use the owned HWND;
# synthetic ev/WM_SIZE would change layout without resizing the native surface.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = [IO.Path]::GetFullPath((Join-Path $root $Executable))
if (!(Test-Path -LiteralPath $exe)) { throw "IDE executable missing: $exe" }
$work = Join-Path $root '_scratch/ide-theme-drawer'
$cfg = Join-Path $work 'cfg'
$cache = Join-Path $cfg 'cache'
$out = Join-Path $root 'build/test-out/ide-theme-drawer'
if ($ProjectOverflowOnly) {
    $work = Join-Path $work 'project-overflow'
    $cfg = Join-Path $work 'cfg'
    $cache = Join-Path $cfg 'cache'
    $out = Join-Path $out 'project-overflow'
}
$project = Join-Path $work 'project'
function Get-ProjectWorkers {
    return @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -in @('zanc.exe', 'cmd.exe') -and $_.CommandLine -and
        ($_.CommandLine.Contains($project) -or $_.CommandLine.Contains($project.Replace('\', '/')))
    })
}
$skins = @('porcelain', 'graphite', 'navy') # Skin.Names --order: 0, 1, 2.
$standard = @{ w = 1950; h = 1260 }
# Native desktop DPI, matching App.Show; ThemeJson.scale is checked against it.
if ($null -eq ('IdeThemeProbeNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class IdeThemeProbeNative {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L, T, R, B; }
    public delegate bool EnumProc(IntPtr hwnd, IntPtr state);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern uint GetDpiForSystem();
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(
        IntPtr hwnd, System.Text.StringBuilder title, int capacity);
    public static string Title(IntPtr hwnd) {
        var title = new System.Text.StringBuilder(512); GetWindowText(hwnd, title, title.Capacity);
        return title.ToString();
    }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr state);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref Point point);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hwnd, uint command);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after,
        int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    public static IntPtr[] Windows(uint pid) {
        var found = new List<IntPtr>();
        EnumWindows((hwnd, state) => {
            uint owner; GetWindowThreadProcessId(hwnd, out owner);
            if (owner == pid && IsWindowVisible(hwnd)) found.Add(hwnd);
            return true;
        }, IntPtr.Zero);
        return found.ToArray();
    }
}
'@
}
$dpiContext = [IdeThemeProbeNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
$dpi = [int][IdeThemeProbeNative]::GetDpiForSystem()
$small = @{ w = [int][Math]::Floor(900 * $dpi / 96); h = [int][Math]::Floor(820 * $dpi / 96) }
$narrow = @{ w = [int][Math]::Floor(600 * $dpi / 96); h = $small.h }
$projectSize = @{ w = [int][Math]::Floor(560 * $dpi / 96); h = $small.h }
$projectChildStages = @('project-settings', 'project-short-top', 'project-short-bottom')
$stages = @('before', 'appearance', 'effects', 'closed', 'skin-zero', 'skin-one',
    'closed-again', 'navy', 'porcelain-return', 'graphite-return', 'small', 'small-news', 'small-home',
    'small-overflow', 'small-overflow-closed', 'narrow', 'narrow-overflow', 'narrow-closed',
    'small-closed', 'restored', 'start-command', 'command-closed')
$fullStages = @('before', 'closed', 'closed-again', 'navy', 'porcelain-return',
    'graphite-return', 'small', 'small-home', 'small-overflow-closed', 'narrow', 'narrow-closed',
    'small-closed', 'restored', 'command-closed')
if ($ProjectOverflowOnly) {
    $stages = @('project-ready', 'project-overflow', 'project-settings', 'project-short-top',
        'project-short-bottom', 'project-settings-closed', 'project-overflow-again', 'project-extensions',
        'project-extensions-closed')
    $fullStages = @()
}
$artifacts = @($stages) + @('appearance-theme', 'effects-theme', 'changed',
    'project-settings-child', 'project-short-top-child', 'project-short-bottom-child')
New-Item -ItemType Directory -Force -Path $out | Out-Null
# Keep one previous set of diagnostic logs; regenerate this probe's stage dumps.
$evidenceNames = @('results.log', 'process.txt', 'stdout.log', 'stderr.log', 'ide_error.log',
    'zan_leaks.log', 'zan_crash.log')
$prior = @($evidenceNames | ForEach-Object { Join-Path $out $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
if ($prior.Count -gt 0) {
    $previousOut = Join-Path $out 'previous'
    New-Item -ItemType Directory -Path $previousOut -Force | Out-Null
    foreach ($name in $evidenceNames) {
        $old = Join-Path $previousOut $name
        if (Test-Path -LiteralPath $old -PathType Leaf) { Remove-Item -LiteralPath $old -Force }
    }
    foreach ($artifact in $prior) { Move-Item -LiteralPath $artifact -Destination $previousOut }
}
foreach ($name in $artifacts) {
    foreach ($extension in @('.json', '.theme.json', '.bin', '.bin.size', '.ready', '.native.json', '.png', '.hwnd')) {
        $artifact = Join-Path $out ($name + $extension)
        if (Test-Path -LiteralPath $artifact -PathType Leaf) { Remove-Item -LiteralPath $artifact -Force }
    }
}
if (Test-Path -LiteralPath $cfg) { Remove-Item -LiteralPath $cfg -Force -Recurse }
New-Item -ItemType Directory -Force -Path $cache | Out-Null
$script = Join-Path $work 'script.txt'
$commands = [Collections.Generic.List[string]]::new()
function Add-Commands([string[]]$lines) { foreach ($line in $lines) { $commands.Add($line) } }
function Add-Snapshot([string]$name, $size = $standard) {
    # Explicit full-client coordinates include the actual caption. The default
    # dump starts below titlebarH and cannot prove a whole-window contract.
    Add-Commands @('redraw full', "dump tree $name.json", "dump theme $name.theme.json",
        "dump pixels $name.bin 0 0 $($size.w) $($size.h)", "dump probe probe.stage $name.ready", 'wait 2000')
}
Add-Commands @("client-dev $($standard.w) $($standard.h)", 'wait 5000')
# The host clicks the corresponding retained control through its native HWND
# after the before/appearance/effects/closed markers.
Add-Snapshot 'before'
Add-Snapshot 'appearance'
Add-Snapshot 'effects'
Add-Snapshot 'closed'
Add-Commands @('clickid @theme-tab-0', 'wait 250',
    'clickid @theme-skin-0', 'wait 500')
Add-Snapshot 'skin-zero'
Add-Commands @('clickid @theme-skin-1', 'wait 500')
Add-Snapshot 'skin-one'
Add-Commands @('clickid @theme-drawer-close', 'wait 500')
Add-Snapshot 'closed-again'
foreach ($change in @(@{ name = 'navy'; index = 2 }, @{ name = 'porcelain-return'; index = 0 },
        @{ name = 'graphite-return'; index = 1 })) {
    Add-Commands @("clickid @theme-skin-$($change.index)",
        'wait 500', 'clickid @theme-drawer-close', 'wait 500')
    Add-Snapshot $change.name
}
# PS resizes the owned native window during graphite-return's bounded wait.
Add-Commands @('wait 500')
Add-Snapshot 'small' $small
# PS wheels the actual Start viewport down, then back to the quick actions.
Add-Commands @('wait 500')
Add-Snapshot 'small-news' $small
Add-Commands @('wait 500')
Add-Snapshot 'small-home' $small
# Commands hidden from the 900-pixel band must remain available in overflow.
Add-Commands @('clickid @ide-ribbon-overflow', 'wait 500')
Add-Snapshot 'small-overflow' $small
Add-Commands @('keydown 27', 'keyup 27', 'wait 500')
Add-Snapshot 'small-overflow-closed' $small
# PS narrows the real client, then restores the 900-pixel case.
Add-Commands @('wait 500')
Add-Snapshot 'narrow' $narrow
Add-Commands @('clickid @ide-ribbon-overflow', 'wait 500')
Add-Snapshot 'narrow-overflow' $narrow
Add-Commands @('keydown 27', 'keyup 27', 'wait 500')
Add-Snapshot 'narrow-closed' $narrow
Add-Commands @('wait 500')
Add-Snapshot 'small-closed' $small
# PS restores the original native client during small-closed's wait.
Add-Commands @('wait 500')
Add-Snapshot 'restored'
Add-Commands @('clickid @start-new-file', 'wait 750')
Add-Snapshot 'start-command'
# PS validates the real owned WizardHost and requests its normal WM_CLOSE.
Add-Commands @('wait 500')
Add-Snapshot 'command-closed'
Add-Commands @('quit')
if ($ProjectOverflowOnly) {
    # Restore through the ordinary IDE startup contract. ConfigWindow may fill
    # manifest defaults, so the disposable project must belong to this run.
    if (Test-Path -LiteralPath $project) {
        if (@(Get-ProjectWorkers).Count -ne 0) { throw "Previous project symbol worker is still using inputs: $project" }
        Remove-Item -LiteralPath $project -Force -Recurse
    }
    New-Item -ItemType Directory -Path $project | Out-Null
    [IO.File]::WriteAllText((Join-Path $project 'zan.proj'),
        "name = RibbonOverflowProbe`nkind = tool`ntype = gui`nentry = Program.zan`ntarget = exe`n")
    [IO.File]::WriteAllText((Join-Path $project 'Program.zan'),
        "using System;`nclass Program { static void Main() { Console.WriteLine(`"RIBBON_OVERFLOW_PROBE`"); } }`n")
    [IO.File]::WriteAllText((Join-Path $cfg 'lastproject.txt'), $project)
    $commands.Clear()
    Add-Commands @("client-dev $($projectSize.w) $($projectSize.h)", 'wait 5000')
    Add-Snapshot 'project-ready' $projectSize
    Add-Snapshot 'project-overflow' $projectSize
    foreach ($stage in $projectChildStages) {
        Add-Commands @("dump child $stage-child", "dump probe probe.stage $stage.ready", 'wait 2000')
    }
    Add-Snapshot 'project-settings-closed' $projectSize
    Add-Snapshot 'project-overflow-again' $projectSize
    Add-Snapshot 'project-extensions' $projectSize
    Add-Commands @('wait 500')
    Add-Snapshot 'project-extensions-closed' $projectSize
    Add-Commands @('quit')
}
[IO.File]::WriteAllLines($script, $commands)
$previous = @{}
foreach ($key in @('ZAN_UI_SCRIPT', 'ZAN_UI_OUT', 'ZAN_IDE_CONFIG_DIR', 'ZAN_IDE_CACHE_DIR')) {
    $previous[$key] = [Environment]::GetEnvironmentVariable($key)
}
$process = $null
$runtimeArtifacts = @{}
foreach ($name in @('zan_leaks.log', 'zan_crash.log')) {
    $runtimeArtifacts[$name] = Test-Path -LiteralPath (Join-Path $root $name)
}
function Write-ProcessRecord([IntPtr]$window) {
    [IO.File]::WriteAllText((Join-Path $out 'process.txt'),
        "PID=$($process.Id)`nHWND=$($window.ToInt64())`nEXE=$exe`n" +
        "EXE_MTIME_UTC=$($exeInfo.LastWriteTimeUtc.ToString('o'))`nEXE_BYTES=$($exeInfo.Length)`n" +
        "SCRIPT=$script`nSCRIPT_MTIME_UTC=$($scriptInfo.LastWriteTimeUtc.ToString('o'))`n" +
        "SCRIPT_BYTES=$($scriptInfo.Length)`nSTART_UTC=$($started.ToString('o'))`n" +
        "PROCESS_START_UTC=$($processStarted.ToString('o'))`n")
}
function Require-NoStartupError {
    $errorPath = Join-Path $cache 'ide_error.log'
    if (!(Test-Path -LiteralPath $errorPath)) { return }
    $errorFile = Get-Item -LiteralPath $errorPath
    if ($errorFile.Length -eq 0) { return }
    $message = [IO.File]::ReadAllText($errorPath).Trim()
    if ($message -eq '') { return }
    Copy-Item -LiteralPath $errorPath -Destination (Join-Path $out 'ide_error.log') -Force
    if ($errorFile.LastWriteTimeUtc -lt $started) { throw 'Stale startup error in the isolated IDE cache' }
    throw "IDE startup failed (PID=$($process.Id), error UTC=$($errorFile.LastWriteTimeUtc.ToString('o'))): $message"
}
function Read-SessionResults {
    $path = Join-Path $out 'results.log'
    if (!(Test-Path -LiteralPath $path)) { throw "UiDriver did not create results.log for PID=$($process.Id)" }
    if ((Get-Item -LiteralPath $path).LastWriteTimeUtc -lt $started) { throw 'Stale UiDriver results.log' }
    $stream = [IO.FileStream]::new($path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader = $null
    try {
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true)
        $text = $reader.ReadToEnd()
    } finally {
        if ($null -ne $reader) { $reader.Dispose() } else { $stream.Dispose() }
    }
    $header = '# UiDriver session: ' + $script + ' ('
    if (!$text.StartsWith($header, [StringComparison]::Ordinal) -or
        $text -notmatch ('\A' + [regex]::Escape($header) + '\d+ lines\)\r?\n')) {
        throw "UiDriver session does not match the launched script/PID=$($process.Id)"
    }
    return $text
}
function Find-Nodes($node, [string]$name = '', [string]$kind = '') {
    if ($null -eq $node -or $node.visible -ne 1) { return }
    if (($name -eq '' -or $node.name -eq $name) -and ($kind -eq '' -or $node.kind -eq $kind)) { $node }
    foreach ($child in $node.kids) { Find-Nodes $child $name $kind }
}
function Require-Node($node, [string]$name) {
    $found = @(Find-Nodes $node $name)
    if ($found.Count -ne 1) { throw "Expected one visible real tree node: $name" }
    return $found[0]
}
function Send-StartWheel([IntPtr]$window, [string]$stage, [int]$delta) {
    $tree = [IO.File]::ReadAllText((Join-Path $out ($stage + '.json'))) | ConvertFrom-Json
    $page = Require-Node $tree.root 'ide-start-page'
    # The page gutter reaches the outer scroll viewport.
    Send-NativeWheel $window ($page.x + 2) ($page.y + [int][Math]::Floor($page.h / 2)) $delta
}
function Send-NativeWheel([IntPtr]$window, [int]$x, [int]$y, [int]$delta) {
    Require-OwnedWindow $window
    $point = [IdeThemeProbeNative+Point]::new()
    $point.X = $x
    $point.Y = $y
    if (![IdeThemeProbeNative]::ClientToScreen($window, [ref]$point)) { throw 'ClientToScreen failed' }
    $position = (($point.Y -band 65535) -shl 16) -bor ($point.X -band 65535)
    foreach ($step in 1..12) {
        if (![IdeThemeProbeNative]::PostMessage($window, 0x20A, [IntPtr]($delta * 65536), [IntPtr]$position)) {
            throw 'Could not wheel the owned Start viewport'
        }
    }
}
function Send-NativeClick([IntPtr]$window, [int]$x, [int]$y) {
    $client = Read-NativeClient $window
    if ($x -lt 0 -or $x -ge $client.w -or $y -lt 0 -or $y -ge $client.h) {
        throw "Click outside the owned client: $x,$y"
    }
    $position = ($y -shl 16) -bor $x
    foreach ($message in @(0x200, 0x201, 0x202)) {
        $buttons = if ($message -eq 0x201) { 1 } else { 0 }
        if (![IdeThemeProbeNative]::PostMessage($window, $message, [IntPtr]$buttons, [IntPtr]$position)) {
            throw "Could not post pointer event to owned window: $message"
        }
    }
}
function Require-OwnedWindow([IntPtr]$window) {
    [uint32]$windowProcess = 0
    [void][IdeThemeProbeNative]::GetWindowThreadProcessId($window, [ref]$windowProcess)
    if ($window -eq [IntPtr]::Zero -or $windowProcess -ne $process.Id -or $process.HasExited) {
        throw "Window does not belong to the live probe process: $window/$windowProcess"
    }
}
function Read-NativeClient([IntPtr]$window) {
    Require-OwnedWindow $window
    $rect = [IdeThemeProbeNative+Rect]::new()
    if (![IdeThemeProbeNative]::GetClientRect($window, [ref]$rect)) { throw 'GetClientRect failed' }
    return @{ w = $rect.R - $rect.L; h = $rect.B - $rect.T }
}
function Read-ProjectClient([string]$stage, [IntPtr]$parent) {
    $prefix = Join-Path $out ($stage + '-child')
    foreach ($extension in @('.json', '.bin', '.hwnd')) {
        if ((Get-Item -LiteralPath ($prefix + $extension)).LastWriteTimeUtc -lt $started) {
            throw "Stale project child artifact: $stage$extension"
        }
    }
    $window = [IntPtr]([long][IO.File]::ReadAllText($prefix + '.hwnd'))
    $client = Read-NativeClient $window
    $title = [IdeThemeProbeNative]::Title($window)
    if ($window -eq $parent -or $title -notin @('项目配置', 'Project')) {
        throw "Unexpected project configuration HWND: $window/$title"
    }
    $data = [IO.File]::ReadAllBytes($prefix + '.bin')
    if ($data.Length -ne (24 + $client.w * $client.h * 4) -or
        [Text.Encoding]::ASCII.GetString($data, 0, 4) -ne 'ZPX1' -or
        [BitConverter]::ToInt32($data, 12) -ne $client.w -or
        [BitConverter]::ToInt32($data, 16) -ne $client.h) {
        throw "Project client canvas and HWND size differ: $stage"
    }
    $tree = [IO.File]::ReadAllText($prefix + '.json') | ConvertFrom-Json
    $done = @($tree.inputTargets | Where-Object { $_.name -eq 'Done' -and $_.pointerHit -eq 1 })
    if ($done.Count -ne 1) { throw "Done is not reachable in the project child: $stage" }
    $scroll = @(Find-Nodes $tree.root '' 'ScrollColumn')
    if ($scroll.Count -ne 1 -or $scroll[0].h -le 0 -or $scroll[0].y + $scroll[0].h -gt $done[0].y) {
        throw "Project body and Done row do not have separate visible bounds: $stage"
    }
    $row = Require-Node $tree.root 'DoneRow'
    $tabs = Require-Node $tree.root 'Tabs'
    if ($row.y + $row.h -gt $client.h -or $tabs.y + $tabs.h -gt $scroll[0].y) {
        throw "Project tabs or Done row extend beyond their allotted region: $stage"
    }
    $childDpi = [int][IdeThemeProbeNative]::GetDpiForWindow($window)
    if ($childDpi -ne $dpi) { throw "Unexpected native DPI for project child: $childDpi/$dpi" }
    [pscustomobject]@{ pid = $process.Id; hwnd = $window.ToInt64(); exe = $exe; title = $title;
        processStartTicks = $processStarted.Ticks; exeMtimeTicks = $exeInfo.LastWriteTimeUtc.Ticks;
        scriptMtimeTicks = $scriptInfo.LastWriteTimeUtc.Ticks;
        w = $client.w; h = $client.h; dpi = $childDpi;
        capturedUtc = [DateTime]::UtcNow.ToString('o') } | ConvertTo-Json |
        Set-Content -LiteralPath ($prefix + '.native.json') -Encoding utf8
    return @{ window = $window; size = $client; tree = $tree; done = $done[0]; scroll = $scroll[0] }
}
function Set-NativeClient([IntPtr]$window, $size) {
    $client = Read-NativeClient $window
    $outer = [IdeThemeProbeNative+Rect]::new()
    if (![IdeThemeProbeNative]::GetWindowRect($window, [ref]$outer)) { throw 'GetWindowRect failed' }
    $width = $size.w + ($outer.R - $outer.L) - $client.w
    $height = $size.h + ($outer.B - $outer.T) - $client.h
    # SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE. Never select by title or resize another IDE.
    if (![IdeThemeProbeNative]::SetWindowPos($window, [IntPtr]::Zero, 0, 0, $width, $height, 0x16)) {
        throw 'SetWindowPos failed'
    }
    $actual = Read-NativeClient $window
    if ($actual.w -ne $size.w -or $actual.h -ne $size.h) {
        throw "Native client resize mismatch: $($actual.w)x$($actual.h), expected $($size.w)x$($size.h)"
    }
}
try {
    $env:ZAN_UI_SCRIPT = $script
    $env:ZAN_UI_OUT = $out
    $env:ZAN_IDE_CONFIG_DIR = $cfg
    $env:ZAN_IDE_CACHE_DIR = $cache
    $exeInfo = Get-Item -LiteralPath $exe
    $scriptInfo = Get-Item -LiteralPath $script
    # Capture the lower bound before launch: startup logs may precede its return.
    $started = [DateTime]::UtcNow
    $process = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru -NoNewWindow `
        -RedirectStandardOutput (Join-Path $out 'stdout.log') -RedirectStandardError (Join-Path $out 'stderr.log')
    $processHandle = $process.Handle
    $processStarted = $process.StartTime.ToUniversalTime()
    $window = [IntPtr]::Zero
    Write-ProcessRecord $window
    if ($processStarted -lt $started -or $process.Path -ine $exe) {
        throw 'Launched process identity does not match the requested executable/time'
    }
    $deadline = $started.AddSeconds(120)
    $projectNormal = $null
    $projectTop = $null
    foreach ($stage in $stages) {
        $ready = Join-Path $out ($stage + '.ready')
        # dump probe writes this marker after all three stage artifacts. Polling
        # is bounded by both the owned process lifetime and the overall budget.
        while (!(Test-Path -LiteralPath $ready)) {
            Require-NoStartupError
            if ($process.HasExited) { throw "IDE exited before stage $stage (PID=$($process.Id), exit=$($process.ExitCode))" }
            if ([DateTime]::UtcNow -gt $deadline) {
                $sessionState = if (Test-Path -LiteralPath (Join-Path $out 'results.log')) { 'results.log exists' }
                    else { 'UiDriver did not create results.log' }
                throw "IDE stage timed out: $stage (PID=$($process.Id); $sessionState; see process.txt)"
            }
            Start-Sleep -Milliseconds 20
        }
        Require-NoStartupError
        $null = Read-SessionResults
        if ($stage -in $projectChildStages) {
            $frame = Read-ProjectClient $stage $window
            $done = $frame.done
            $viewport = $frame.scroll
            if ($stage -eq 'project-settings') {
                $projectNormal = $frame
                $skinsInView = Require-Node $frame.tree.root 'SkinList'
                if ($skinsInView.h -le 0 -or $skinsInView.kids.Count -lt 3) {
                    throw 'Project skin choices have no arranged content'
                }
                $short = @{ w = $frame.size.w; h = [int][Math]::Floor($frame.size.h * 0.7) }
                Set-NativeClient $frame.window $short
            } else {
                if ($frame.window -ne $projectNormal.window -or $frame.size.h -ge $projectNormal.size.h) {
                    throw "Project viewport was not shortened on the same HWND: $stage"
                }
                if ($stage -eq 'project-short-top') {
                    $projectTop = $frame
                    Send-NativeWheel $frame.window ($viewport.x + [int][Math]::Floor($viewport.w / 2)) `
                        ($viewport.y + [int][Math]::Floor($viewport.h / 2)) -120
                } else {
                    foreach ($property in @('x', 'y', 'w', 'h')) {
                        if ($done.$property -ne $projectTop.done.$property) {
                            throw 'Done moved during project configuration scroll'
                        }
                    }
                    $last = @($frame.tree.inputTargets | Where-Object {
                        $_.name -eq 'ObfStringsOn' -and $_.pointerHit -eq 1
                    })
                    $oldTop = Require-Node $projectTop.tree.root 'ProjectName'
                    $newTop = Require-Node $frame.tree.root 'ProjectName'
                    if ($last.Count -ne 1 -or $newTop.y -ge $oldTop.y -or
                        $last[0].y -lt $viewport.y -or $last[0].y + $last[0].h -gt $viewport.y + $viewport.h) {
                        throw 'Project wheel input did not bring the last setting into view'
                    }
                    Send-NativeClick $frame.window ($done.x + [int][Math]::Floor($done.w / 2)) `
                        ($done.y + [int][Math]::Floor($done.h / 2))
                }
            }
            continue
        }
        foreach ($extension in @('.json', '.theme.json', '.bin', '.ready')) {
            $file = Get-Item -LiteralPath (Join-Path $out ($stage + $extension))
            if ($file.LastWriteTimeUtc -lt $started) { throw "Stale stage evidence: $stage$extension" }
        }
        if ($window -eq [IntPtr]::Zero) {
            $windows = @([IdeThemeProbeNative]::Windows([uint32]$process.Id) | Where-Object {
                [IdeThemeProbeNative]::GetWindow($_, 4) -eq [IntPtr]::Zero
            })
            if ($windows.Count -ne 1) { throw 'Expected one owned top-level IDE window' }
            $window = $windows[0]
            Write-ProcessRecord $window
        }
        $client = Read-NativeClient $window
        $nativeDpi = [int][IdeThemeProbeNative]::GetDpiForWindow($window)
        if ($nativeDpi -ne $dpi) { throw "Native DPI changed: $nativeDpi/$dpi" }
        $size = if ($ProjectOverflowOnly) { $projectSize }
            elseif ($stage -like 'narrow*') { $narrow }
            elseif ($stage -like 'small*') { $small } else { $standard }
        if ($client.w -ne $size.w -or $client.h -ne $size.h) {
            throw "Unexpected native client size at ${stage}: $($client.w)x$($client.h), expected $($size.w)x$($size.h) (HWND=$window, DPI=$nativeDpi)"
        }
        [pscustomobject]@{ pid = $process.Id; hwnd = $window.ToInt64(); exe = $exe;
            processStartUtc = $processStarted.ToString('o'); processStartTicks = $processStarted.Ticks;
            exeMtimeUtc = $exeInfo.LastWriteTimeUtc.ToString('o'); exeMtimeTicks = $exeInfo.LastWriteTimeUtc.Ticks;
            scriptMtimeUtc = $scriptInfo.LastWriteTimeUtc.ToString('o'); scriptMtimeTicks = $scriptInfo.LastWriteTimeUtc.Ticks;
            stage = $stage; w = $client.w; h = $client.h; dpi = $nativeDpi;
            capturedUtc = [DateTime]::UtcNow.ToString('o') } | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $out ($stage + '.native.json')) -Encoding utf8
        $nativeTarget = switch ($stage) {
            'project-ready' { 'ide-ribbon-overflow' }
            'project-overflow' { 'ribbon-overflow-command-12' }
            'project-settings-closed' { 'ide-ribbon-overflow' }
            'project-overflow-again' { 'ribbon-overflow-command-15' }
            'before' { 'window-theme' }
            'appearance' { 'theme-tab-1' }
            'effects' { 'theme-drawer-close' }
            'closed' { 'window-theme' }
            'closed-again' { 'window-theme' }
            'navy' { 'window-theme' }
            'porcelain-return' { 'window-theme' }
        }
        if ($nativeTarget -or $stage -in @('graphite-return', 'small-overflow-closed', 'narrow-closed', 'small-closed', 'small', 'small-news',
                'start-command', 'project-extensions') -and
            ([DateTime]::UtcNow - (Get-Item -LiteralPath $ready).LastWriteTimeUtc).TotalMilliseconds -gt 1500) {
            throw "Missed bounded native-action window for stage $stage"
        }
        if ($nativeTarget) {
            $tree = [IO.File]::ReadAllText((Join-Path $out ($stage + '.json'))) | ConvertFrom-Json
            $targets = @($tree.inputTargets | Where-Object { $_.name -eq $nativeTarget -and $_.pointerHit -eq 1 })
            if ($targets.Count -ne 1) { throw "Expected reachable control at ${stage}: $nativeTarget" }
            $target = $targets[0]
            Send-NativeClick $window ($target.x + [int][Math]::Floor($target.w / 2)) `
                ($target.y + [int][Math]::Floor($target.h / 2))
        }
        if ($stage -eq 'graphite-return') { Set-NativeClient $window $small }
        if ($stage -eq 'small-overflow-closed') { Set-NativeClient $window $narrow }
        if ($stage -eq 'narrow-closed') { Set-NativeClient $window $small }
        if ($stage -eq 'small-closed') { Set-NativeClient $window $standard }
        if ($stage -eq 'small') { Send-StartWheel $window $stage -120 }
        if ($stage -eq 'small-news') { Send-StartWheel $window $stage 120 }
        if ($stage -eq 'start-command') {
            $children = @([IdeThemeProbeNative]::Windows([uint32]$process.Id) | Where-Object {
                $_ -ne $window -and [IdeThemeProbeNative]::Title($_) -in @('New File', '新建文件')
            })
            # ChildWindow.OpenHost creates a separate App, without a Win32
            # GW_OWNER contract. Bind its HWND to this PID and actual wizard title.
            if ($children.Count -ne 1) { throw 'Start New File did not open one real owned WizardHost' }
            Require-OwnedWindow $children[0]
            $childClient = Read-NativeClient $children[0]
            if ($childClient.w -le 0 -or $childClient.h -le 0) { throw 'Start wizard has no visible client' }
            if (![IdeThemeProbeNative]::PostMessage($children[0], 0x10, [IntPtr]::Zero, [IntPtr]::Zero)) {
                throw 'Could not close the owned Start wizard'
            }
        }
        if ($stage -eq 'command-closed' -or $stage -like 'project-*-closed') {
            $children = @([IdeThemeProbeNative]::Windows([uint32]$process.Id) | Where-Object { $_ -ne $window })
            if ($children.Count -ne 0) { throw "Owned child window remained visible after normal close: $stage" }
        }
        if ($stage -eq 'project-extensions') {
            $children = @([IdeThemeProbeNative]::Windows([uint32]$process.Id) | Where-Object {
                $_ -ne $window -and [IdeThemeProbeNative]::Title($_) -in @('扩展', 'Extensions')
            })
            if ($children.Count -ne 1) { throw 'Overflow command did not open one native Extensions window' }
            $childClient = Read-NativeClient $children[0]
            if ($childClient.w -le 0 -or $childClient.h -le 0) { throw 'Extensions window has no visible client' }
            if (![IdeThemeProbeNative]::PostMessage($children[0], 0x10, [IntPtr]::Zero, [IntPtr]::Zero)) {
                throw 'Could not close the Extensions window'
            }
        }
    }
    if (!$process.WaitForExit(10000)) { Require-NoStartupError; throw 'IDE did not quit after the final stage' }
    Require-NoStartupError
    if ($process.ExitCode -ne 0) { throw "IDE exited with $($process.ExitCode)" }
    foreach ($identity in @($exeInfo, $scriptInfo)) {
        $current = Get-Item -LiteralPath $identity.FullName
        if ($current.LastWriteTimeUtc -ne $identity.LastWriteTimeUtc -or $current.Length -ne $identity.Length) {
            throw "Run input changed during the session: $($identity.FullName)"
        }
    }
    $diagnostics = [IO.File]::ReadAllText((Join-Path $out 'stdout.log')) +
        [IO.File]::ReadAllText((Join-Path $out 'stderr.log'))
    if ($diagnostics -match 'fatal\(|runtime error|arc.guard|strict.runtime|\[leak|memory leak detected') {
        throw 'IDE runtime diagnostics detected; inspect stdout.log and stderr.log'
    }
    $log = Read-SessionResults
    if ($log -notmatch 'DONE pass=\d+ fail=0 command_errors=0' -or
        $log -match 'ASSERT FAIL|COMMAND ERROR') { throw 'UiDriver failed; inspect results.log' }
    function Read-Tree([string]$name) {
        return [IO.File]::ReadAllText((Join-Path $out ($name + '.json'))) | ConvertFrom-Json
    }
    function Require-Target($tree, [string]$name) {
        $targets = @(Get-Targets $tree | Where-Object { $_.name -eq $name -and $_.pointerHit -eq 1 })
        if ($targets.Count -ne 1) { throw "Expected one reachable retained target: $name" }
    }
    function Read-Pixels([string]$name) {
        $bytes = [IO.File]::ReadAllBytes((Join-Path $out ($name + '.bin')))
        if ($bytes.Length -lt 24 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'ZPX1') {
            throw "Invalid ZPX1 pixel dump: $name"
        }
        $width = [BitConverter]::ToUInt32($bytes, 12)
        $height = [BitConverter]::ToUInt32($bytes, 16)
        $bpp = [BitConverter]::ToUInt32($bytes, 20)
        if ($width -eq 0 -or $height -eq 0 -or $bpp -ne 4 -or
            $bytes.LongLength -ne 24 + [long]$width * $height * 4) {
            throw "Incomplete ZPX1 BGRA pixel dump: $name"
        }
        return [pscustomobject]@{
            name = $name; bytes = $bytes
            x = [int][BitConverter]::ToUInt32($bytes, 4)
            y = [int][BitConverter]::ToUInt32($bytes, 8)
            w = [int]$width; h = [int]$height
        }
    }
    function Get-Pixel($dump, [int]$x, [int]$y) {
        $column = $x - $dump.x
        $row = $y - $dump.y
        if ($column -lt 0 -or $row -lt 0 -or $column -ge $dump.w -or $row -ge $dump.h) {
            throw "Pixel outside $($dump.name) dump: ($x,$y)"
        }
        # ZPX1 is top-down native u32: little-endian BGRA bytes / numeric ARGB.
        return [BitConverter]::ToUInt32($dump.bytes, (24 + ($row * $dump.w + $column) * 4))
    }
    function Get-OpaqueColor([long]$color) {
        return [uint32](($color -band 0xFFFFFFL) -bor 0xFF000000L)
    }
    function Test-PixelColor([uint32]$pixel, [uint32]$expected) {
        if (($pixel -shr 24) -ne 255) { return $false }
        foreach ($shift in @(0, 8, 16)) {
            $actualChannel = [int](($pixel -shr $shift) -band 255)
            $expectedChannel = [int](($expected -shr $shift) -band 255)
            if ([Math]::Abs($actualChannel - $expectedChannel) -gt 2) { return $false }
        }
        return $true
    }
    function Require-Geometry($node, $parent, [string]$context) {
        if (($node.PSObject.Properties['visible'] -and $node.visible -ne 1) -or
            $node.w -le 0 -or $node.h -le 0 -or
            $node.x -lt $parent.x -or $node.y -lt $parent.y -or
            $node.x + $node.w -gt $parent.x + $parent.w -or
            $node.y + $node.h -gt $parent.y + $parent.h) {
            throw "Missing, zero, or clipped visible geometry: $context"
        }
    }
    function Require-ColorPatch($dump, $rect, [uint32]$expected, [string]$context) {
        # Sample the interior, avoiding borders and antialiased edges.
        foreach ($row in 1..3) {
            foreach ($column in 1..3) {
                $x = [int]$rect.x + [int][Math]::Floor(($rect.w - 1) * $column / 4)
                $y = [int]$rect.y + [int][Math]::Floor(($rect.h - 1) * $row / 4)
                $pixel = Get-Pixel $dump $x $y
                if (!(Test-PixelColor $pixel $expected)) {
                    throw ("{0}: expected opaque ARGB {1:X8}, got {2:X8} at ({3},{4})" -f
                        $context, $expected, $pixel, $x, $y)
                }
            }
        }
    }
    function Require-Disjoint($a, $b, [string]$context) {
        if ($a.x -lt $b.x + $b.w -and $b.x -lt $a.x + $a.w -and
            $a.y -lt $b.y + $b.h -and $b.y -lt $a.y + $a.h) { throw "Overlapping geometry: $context" }
    }
    function Test-Contained($node, $parent) {
        return $node.w -gt 0 -and $node.h -gt 0 -and $node.x -ge $parent.x -and $node.y -ge $parent.y -and
            $node.x + $node.w -le $parent.x + $parent.w -and $node.y + $node.h -le $parent.y + $parent.h
    }
    function Get-Targets($tree) {
        # Adapters may expose an endpoint twice. The stable observation is its
        # identity within this frame and rectangle, never an ID across frames.
        return @($tree.inputTargets | Sort-Object -Property id, x, y, w, h -Unique)
    }
    function Require-ChildrenInside($node, [string]$context) {
        # List rows have their own real clipping/scroll viewport. Validate the
        # list box here; the selected news view separately proves visible rows.
        if ($node.kind -eq 'ListView') { return }
        foreach ($child in $node.kids) {
            if ($child.visible -eq 1 -and $child.w -gt 0 -and $child.h -gt 0) {
                Require-Geometry $child $node "$context/$($child.kind):$($child.name)"
                Require-ChildrenInside $child "$context/$($child.name)"
            }
        }
    }
    function Require-Start($tree, $page, $theme, [bool]$newsView = $false) {
        $recent = Require-Node $page 'start-recent-card'
        $newsCard = Require-Node $page 'start-news-card'
        $recents = Require-Node $recent 'start-recents'
        $news = Require-Node $newsCard 'start-news'
        $quick = Require-Node $recent 'start-quick'
        $unit = [double]$theme.scale / 1000
        foreach ($card in @($recent, $newsCard)) {
            if ($card.w -lt 240 * $unit) { throw "Start section squeezed below usable width: $($card.name)" }
            if ($card.x -lt $page.x -or $card.x + $card.w -gt $page.x + $page.w) {
                throw "Start section overflows horizontally: $($card.name)"
            }
        }
        Require-Disjoint $recent $newsCard 'Start sections'
        Require-Geometry $recents $recent 'Start recent list'
        Require-Geometry $quick $recent 'Start quick commands'
        Require-Disjoint $recents $quick 'Start empty state / quick commands'
        if ($recents.h -lt $theme.fontSizeMedium + 2 * $theme.paddingSmall) {
            throw 'Start empty/recent list has no independent readable area'
        }
        Require-Geometry $news $newsCard 'Start news list'
        if ($news.w -lt 200 * $unit) { throw 'Start news text column is unusably narrow' }
        foreach ($card in @($recent, $newsCard)) { Require-ChildrenInside $card $card.name }
        if ($newsView) {
            $rows = @(Find-Nodes $news '' 'NewsRow' | Where-Object { Test-Contained $_ $page })
            if ($rows.Count -eq 0) { throw 'Scrolling Start did not reveal a complete news row' }
            return
        }
        Require-Geometry $quick $page 'Start quick commands in visible viewport'
        $buttons = @()
        foreach ($name in @('start-new-project', 'start-new-file', 'start-open-file', 'start-open-project')) {
            $button = Require-Node $quick $name
            Require-Geometry $button $quick "Start command $name"
            Require-Geometry $button $page "Visible Start command $name"
            Require-Target $tree $name
            Require-ChildrenInside $button $name
            $buttons += $button
        }
        for ($i = 0; $i -lt $buttons.Count; $i++) {
            for ($j = $i + 1; $j -lt $buttons.Count; $j++) {
                Require-Disjoint $buttons[$i] $buttons[$j] 'Start command buttons'
            }
        }
        $wrapped = $newsCard.y -ge $recent.y + $recent.h
        if (!$wrapped) { Require-Geometry $newsCard $page 'Start news card in visible viewport' }
    }
    function Require-RibbonLayout($tree, $dock, $page) {
        $targets = Get-Targets $tree
        $ribbonRect = [pscustomobject]@{ x = $tree.root.x; y = $tree.root.y;
            w = $tree.root.w; h = $dock.y - $tree.root.y; visible = 1 }
        if ($ribbonRect.h -le 0) { throw 'Ribbon has no content region above DockHost' }
        $pageNodeKeys = @{}
        foreach ($node in @(Find-Nodes $page)) {
            $pageNodeKeys["$($node.kind)|$($node.name)|$($node.x)|$($node.y)|$($node.w)|$($node.h)"] = $true
        }
        $ribbonTargets = @($targets | Where-Object {
            $_.y -ge $ribbonRect.y -and $_.y -lt $dock.y -and
            !$pageNodeKeys.ContainsKey("$($_.kind)|$($_.name)|$($_.x)|$($_.y)|$($_.w)|$($_.h)")
        })
        if ($ribbonTargets.Count -lt 3) { throw 'Actual Ribbon commands are missing' }
        foreach ($target in $ribbonTargets) {
            Require-Geometry $target $ribbonRect 'Ribbon command within content'
            Require-ChildrenInside $target 'Ribbon command content'
            if ($target.pointerHit -ne 1) { throw 'Visible Ribbon command is silently unreachable' }
        }
    }
    function Require-WindowLayout($tree, $dump, $theme, [bool]$newsView = $false) {
        $shell = Require-Node $tree.root 'ide-shell-root'
        $dock = Require-Node $shell 'ide-workspace-dock'
        $status = Require-Node $shell 'ide-status-bar'
        $page = Require-Node $shell 'ide-start-page'
        Require-Geometry $tree.root $dump 'Form content within native client'
        foreach ($node in @($shell, $dock, $status, $page)) {
            Require-Geometry $node $tree.root "Form/$($node.name)"
        }
        Require-Disjoint $dock $status 'workspace / status'
        if ($dock.y + $dock.h -ne $status.y) { throw 'Dock and status leave an unowned vertical gap' }
        Require-ChildrenInside $status 'StatusBar'
        Require-Target $tree 'ide-lsp-status'
        $targets = Get-Targets $tree
        $captionTheme = @($targets | Where-Object { $_.name -eq 'window-theme' })
        if ($captionTheme.Count -ne 1) { throw 'Missing real caption theme button' }
        $caption = @($targets | Where-Object { $_.kind -eq 'Button' -and
            $_.y -eq $captionTheme[0].y -and $_.h -eq $captionTheme[0].h } | Sort-Object x)
        if ($caption.Count -ne 5) { throw 'Expected five actual caption buttons' }
        foreach ($button in $caption) {
            Require-Geometry $button $dump 'actual caption button'
            if ($button.pointerHit -ne 1) { throw 'Actual caption button is not reachable' }
            Require-ChildrenInside $button 'caption'
        }
        for ($i = 1; $i -lt $caption.Count; $i++) {
            Require-Disjoint $caption[$i - 1] $caption[$i] 'caption buttons'
        }
        Require-RibbonLayout $tree $dock $page
        $contents = @($dock.kids | Where-Object { $_.visible -eq 1 -and $_.name -eq 'dockcontent' })
        if ($contents.Count -lt 3) { throw 'Expected document and visible side/bottom dock surfaces' }
        foreach ($content in $contents) { Require-Geometry $content $dock 'Dock content' }
        $document = @($contents | Where-Object { Test-Contained $page $_ })
        if ($document.Count -ne 1) { throw 'Start page is outside its actual document pane' }
        # IDE explicitly reserves at least half the workspace width and 60% of
        # its height for the document, including the real document tabs.
        if ($document[0].w + 1 -lt $dock.w * 0.50 -or $document[0].h + 1 -lt $dock.h * 0.60) {
            throw 'Dock panels consumed the reserved Start document area (50% width / 60% height)'
        }
        $bottoms = @($contents | Where-Object { $_.x -eq $document[0].x -and $_.y -gt $document[0].y })
        foreach ($bottom in $bottoms) {
            if ($bottom.h -gt $dock.h * 0.40 + 1) { throw 'Bottom dock exceeds 40% of the workspace' }
            Require-Disjoint $page $bottom 'Start page / bottom dock'
        }
        Require-Start $tree $page $theme $newsView
    }
    function Require-OpaqueRegion($dump, $rect, [string]$context) {
        # Dense, distributed samples catch desktop holes in the document, each
        # dock, Ribbon and status; one opaque midpoint cannot establish this.
        foreach ($row in 1..11) {
            foreach ($column in 1..9) {
                $x = [int]$rect.x + [int][Math]::Floor(($rect.w - 1) * $column / 10)
                $y = [int]$rect.y + [int][Math]::Floor(($rect.h - 1) * $row / 12)
                $pixel = Get-Pixel $dump $x $y
                if (($pixel -shr 24) -ne 255) {
                    throw ("{0}: nonopaque ARGB {1:X8} at ({2},{3}) in {4}" -f $context, $pixel, $x, $y, $dump.name)
                }
            }
        }
    }
    function Get-SurfaceWitnesses($tree, $dump, $theme) {
        $shell = Require-Node $tree.root 'ide-shell-root'
        $dock = Require-Node $shell 'ide-workspace-dock'
        $page = Require-Node $shell 'ide-start-page'
        $status = Require-Node $shell 'ide-status-bar'
        Require-OpaqueRegion $dump $tree.root 'whole content'
        $captionTheme = @(Get-Targets $tree | Where-Object { $_.name -eq 'window-theme' })[0]
        $caption = [pscustomobject]@{ x = $tree.root.x; y = $captionTheme.y;
            w = $tree.root.w; h = $tree.root.y - $captionTheme.y }
        Require-OpaqueRegion $dump $caption 'actual caption interior'
        foreach ($region in @($page, $dock, $status)) { Require-OpaqueRegion $dump $region $region.name }
        $contents = @($dock.kids | Where-Object { $_.visible -eq 1 -and $_.name -eq 'dockcontent' })
        $witnesses = @()
        $seen = @{}
        foreach ($content in $contents) {
            $document = Test-Contained $page $content
            $key = if ($document) { 'document' } else {
                (@($content.kids | Where-Object { $_.visible -eq 1 } | ForEach-Object {
                    "$($_.kind)/$($_.name)"
                } | Sort-Object) -join '|')
            }
            if ($key -eq '' -or $seen.ContainsKey($key)) { throw "Ambiguous dock surface identity: $($dump.name)/$key" }
            $seen[$key] = $true
            Require-OpaqueRegion $dump $content 'dock surface'
            # Dockgroup's empty surface is --dock-bg = theme.bgTertiary.
            # Start cards cover its document; the page's left padding is the
            # real blank gutter, independent of card widths or wrapping.
            $expected = Get-OpaqueColor $theme.bgTertiary
            if ($document) {
                $gutter = [pscustomobject]@{ x = $page.x + 1; y = $page.y + $theme.paddingSmall;
                    w = [Math]::Max(1, [Math]::Floor($theme.paddingSmall / 3));
                    h = $page.h - 2 * $theme.paddingSmall }
                Require-ColorPatch $dump $gutter $expected 'Start document gutter restored'
                $witnesses += [pscustomobject]@{ key = $key; color = $expected }
                continue
            }
            # Search a fixed grid, retaining independent blank rows. Text and
            # controls are excluded by the resolved surface RGB comparison.
            $matches = @()
            foreach ($row in 1..7) {
                foreach ($column in 1..5) {
                    $x = [int]$content.x + [int][Math]::Floor(($content.w - 1) * $column / 6)
                    $y = [int]$content.y + [int][Math]::Floor(($content.h - 1) * $row / 8)
                    if (Test-PixelColor (Get-Pixel $dump $x $y) $expected) {
                        $matches += [pscustomobject]@{ x = $x; y = $y; color = $expected; row = $row }
                    }
                }
            }
            # The document is covered by Start cards, but it still has a page
            # gutter. Other docks must expose their real semantic blank fill.
            if (@($matches.row | Select-Object -Unique).Count -lt 2) {
                throw "No distributed resolved-color surface in actual dock: $($dump.name) @($($content.x),$($content.y))"
            }
            $witnesses += [pscustomobject]@{ key = $key; color = $expected }
        }
        return $witnesses
    }
    function Require-Surface($tree, $dump, $theme) {
        $targets = @($tree.inputTargets | Where-Object { $_.name -like 'theme-*' -and $_.pointerHit -eq 1 })
        if ($targets.Count -eq 0 -or $theme.paddingLarge -le 0) {
            throw "Missing drawer bounds/theme padding: $($dump.name)"
        }
        # The controls end at the inner right edge of the Drawer. Its padding
        # gives a blank strip without assuming a drawer width or a card layout.
        $controlRight = [int](($targets | ForEach-Object { $_.x + $_.w } | Measure-Object -Maximum).Maximum)
        $left = $controlRight + 1
        $right = [Math]::Min($dump.x + $dump.w - 2, $controlRight + [int]$theme.paddingLarge - 1)
        $top = [Math]::Max($dump.y + [int]$theme.paddingLarge,
            [int](($targets | Measure-Object -Property y -Minimum).Minimum))
        $bottom = $dump.y + $dump.h - [int]$theme.paddingLarge - 1
        if ($right -le $left -or $bottom -le $top) {
            throw "No blank drawer padding to inspect: $($dump.name)"
        }
        # base.css --drawer-bg is App.WithAlpha(theme.bgPrimary, 255).
        # Alpha alone is insufficient: a mask over opaque content is opaque too.
        $expected = Get-OpaqueColor $theme.bgPrimary
        foreach ($row in 1..9) {
            foreach ($column in 1..3) {
                $x = $left + [int][Math]::Floor(($right - $left) * $column / 4)
                $y = $top + [int][Math]::Floor(($bottom - $top) * $row / 10)
                $pixel = Get-Pixel $dump $x $y
                if (!(Test-PixelColor $pixel $expected)) {
                    throw ("Drawer surface shows mask/background in {0}: expected {1:X8}, got {2:X8} at ({3},{4})" -f
                        $dump.name, $expected, $pixel, $x, $y)
                }
            }
        }
    }
    function Require-Previews($tree, $dump, $candidateThemes) {
        $cards = @($tree.inputTargets | Where-Object { $_.name -match '^theme-skin-\d+$' -and $_.pointerHit -eq 1 })
        if ($cards.Count -lt 2) { throw "Missing visible skin cards: $($dump.name)" }
        foreach ($card in $cards) {
            $index = $card.name.Substring('theme-skin-'.Length)
            $context = "$($dump.name)/$($card.name)"
            Require-Geometry $card $dump $context
            $previews = @($card.kids | Where-Object { $_.name -eq "theme-skin-preview-$index" })
            if ($previews.Count -ne 1) { throw "Missing retained preview: $context" }
            $preview = $previews[0]
            Require-Geometry $preview $card "$context/preview"
            $blocks = @($preview.kids)
            if ($blocks.Count -ne 3) { throw "Expected sidebar/content/accent preview blocks: $context" }
            $labels = @($card.kids | Where-Object { $_.kind -eq 'Label' -and $_.visible -eq 1 })
            if ($labels.Count -ne 1) { throw "Missing skin label: $context" }
            Require-Geometry $labels[0] $card "$context/label"
            if ($labels[0].y -lt $preview.y + $preview.h) {
                throw "Preview overlaps the skin label: $context"
            }
            $colors = @()
            for ($i = 0; $i -lt $blocks.Count; $i++) {
                $block = $blocks[$i]
                Require-Geometry $block $preview "$context/block-$i"
                $color = Get-Pixel $dump ([int]$block.x + [int][Math]::Floor($block.w / 2)) `
                    ([int]$block.y + [int][Math]::Floor($block.h / 2))
                Require-ColorPatch $dump $block $color "$context/block-$i"
                $colors += $color
            }
            if (@($colors | Select-Object -Unique).Count -lt 2) {
                throw "Preview has no distinct painted color blocks: $context"
            }
            if ($candidateThemes.ContainsKey($index)) {
                $candidate = $candidateThemes[$index]
                Require-ColorPatch $dump $blocks[0] (Get-OpaqueColor $candidate.bgSecondary) "$context/sidebar"
                Require-ColorPatch $dump $blocks[2] (Get-OpaqueColor $candidate.primary) "$context/accent"
                # The top padding belongs to the preview's own bgPrimary fill.
                $firstBlockY = [int](($blocks | Measure-Object -Property y -Minimum).Minimum)
                $background = [pscustomobject]@{
                    x = $preview.x; y = $preview.y; w = $preview.w; h = $firstBlockY - $preview.y
                }
                if ($background.h -le 0) { throw "No preview background area: $context" }
                Require-ColorPatch $dump $background (Get-OpaqueColor $candidate.bgPrimary) "$context/background"
            }
        }
    }
    function Require-Shadow($dump) {
        $middleY = $dump.y + [int][Math]::Floor($dump.h / 2)
        # IdeForm declares a ten-pixel shadow band. x=5 remains outside its
        # content at either supported DPI; the center must still be opaque.
        $outerAlpha = (Get-Pixel $dump ($dump.x + 5) $middleY) -shr 24
        $innerAlpha = (Get-Pixel $dump ($dump.x + [int][Math]::Floor($dump.w / 2)) $middleY) -shr 24
        if ($outerAlpha -le 0 -or $outerAlpha -ge 240 -or $innerAlpha -ne 255) {
            throw "Real IDE shadow band/content alpha invalid: $($dump.name) ($outerAlpha/$innerAlpha)"
        }
    }
    function Require-DrawerWindowEdges($closed, $opened) {
        if ($closed.w -ne $opened.w -or $closed.h -ne $opened.h) {
            throw 'Drawer open changed the window dimensions'
        }
        $right = $closed.x + $closed.w - 1
        $bottom = $closed.y + $closed.h - 1
        $middleX = $closed.x + [int][Math]::Floor($closed.w / 2)
        $middleY = $closed.y + [int][Math]::Floor($closed.h / 2)
        foreach ($point in @(@($closed.x, $bottom), @($right, $bottom),
                @(($closed.x + 5), $middleY), @(($right - 4), $middleY),
                @($middleX, ($bottom - 4)))) {
            $before = Get-Pixel $closed $point[0] $point[1]
            $after = Get-Pixel $opened $point[0] $point[1]
            if ($before -ne $after) {
                throw ('Drawer altered shadow or transparent corner at ({0},{1}): {2:X8} -> {3:X8}' -f
                    $point[0], $point[1], $before, $after)
            }
        }
        $chromeX = $closed.x + [int][Math]::Floor($closed.w / 3)
        $native = Read-Tree ($closed.name + '.native')
        $chromeY = $closed.y + [int]($native.dpi / 8) + 4
        if ((Get-Pixel $closed $chromeX $chromeY) -ne (Get-Pixel $opened $chromeX $chromeY)) {
            throw 'Drawer changed the caption surface'
        }
    }
    function Read-Theme([string]$name) { return Read-Tree ($name + '.theme') }
    function Require-Skin($theme, [int]$index) {
        $skin = $skins[$index]
        if ($theme.glass -ne 0 -or $theme.gradient -ne 0) { throw "Unexpected glass/gradient in $skin" }
        $css = [IO.File]::ReadAllText((Join-Path $root "packages/Zan.Gui/skins/$skin/skin.css"))
        foreach ($field in @(@('bgPrimary', 'bg-primary'), @('bgSecondary', 'bg-secondary'),
                @('bgTertiary', 'bg-tertiary'), @('primary', 'primary'))) {
            $pattern = '--' + $field[1] + '\s*:\s*#([0-9a-fA-F]{6})\s*;'
            $match = [regex]::Match($css, $pattern)
            if (!$match.Success) { throw "Missing opaque palette token $($field[1]) in $skin" }
            $expected = [Convert]::ToUInt32(('FF' + $match.Groups[1].Value), 16)
            $actual = [uint32]([long]$theme.($field[0]) -band 0xFFFFFFFFL)
            if ($actual -ne $expected) { throw "Resolved theme does not match $skin/$($field[0])" }
        }
    }
    function Require-ClosedDrawer($tree, [string]$context) {
        Require-Target $tree 'window-theme'
        $drawerTargets = @(Get-Targets $tree | Where-Object { $_.name -like 'theme-*' -and $_.pointerHit -eq 1 })
        if ($drawerTargets.Count -ne 0) { throw "Closed drawer still receives input: $context" }
    }
    function Require-RibbonOverflow([string]$stage, [int[]]$actions, [int[]]$disabled = @()) {
        $tree = Read-Tree $stage
        $dump = Read-Pixels $stage
        $theme = Read-Theme $stage
        $targets = Get-Targets $tree
        $commands = @($targets | Where-Object {
            $_.kind -eq 'Button' -and $_.name -like 'ribbon-overflow-command-*' -and $_.pointerHit -eq 1
        } | Sort-Object y)
        $enabled = @($actions | Where-Object { $_ -notin $disabled })
        if ($commands.Count -ne $enabled.Count) { throw "Missing complete enabled Ribbon overflow command set: $stage" }
        for ($i = 0; $i -lt $enabled.Count; $i++) {
            $name = "ribbon-overflow-command-$($enabled[$i])"
            Require-Target $tree $name
            if ($commands[$i].name -ne $name) { throw "Ribbon overflow command order changed: $stage" }
        }
        foreach ($action in $disabled) {
            if (@($targets | Where-Object { $_.name -eq "ribbon-overflow-command-$action" }).Count -ne 0) {
                throw "Project-only command became an input endpoint without a project: $stage/$action"
            }
        }
        foreach ($command in $commands) {
            Require-Geometry $command $tree.root 'overflow command within Form content'
            Require-ChildrenInside $command 'actual overflow command content'
        }
        for ($i = 1; $i -lt $commands.Count; $i++) {
            Require-Disjoint $commands[$i - 1] $commands[$i] 'overflow command rows'
        }
        if (@(Get-Targets $tree | Where-Object { $_.name -eq 'window-theme' -and $_.pointerHit -eq 1 }).Count -ne 0) {
            throw "Overflow menu did not own the current input session: $stage"
        }
        # Snapshots expose routed command endpoints, not the popup root. Sample
        # its actual blank padding beside and beyond those rows to catch a
        # transparent or clipped menu even when button geometry is valid.
        $first = $commands[0]
        $last = $commands[-1]
        # Disabled rows remain painted but deliberately have no input endpoint.
        # The project run observes every row directly; startup also samples the
        # full menu padding using the same measured row height and model order.
        $top = $first.y - [Array]::IndexOf($actions, $enabled[0]) * $first.h
        for ($i = 0; $i -lt $enabled.Count; $i++) {
            $row = $commands[$i]
            if ($row.h -ne $first.h -or $row.y -ne $top + [Array]::IndexOf($actions, $enabled[$i]) * $first.h) {
                throw "Ribbon overflow rows do not preserve the full command layout: $stage"
            }
        }
        $background = Get-OpaqueColor $theme.bgSecondary
        foreach ($strip in @(
                [pscustomobject]@{ x = $first.x - 3; y = $top; w = 2; h = $last.y + $last.h - $top },
                [pscustomobject]@{ x = $first.x; y = $top - 3; w = $first.w; h = 2 },
                [pscustomobject]@{ x = $last.x; y = $last.y + $last.h + 1; w = $last.w; h = 2 })) {
            Require-ColorPatch $dump $strip $background "$stage/menu padding"
        }
    }
    foreach ($stage in $stages) {
        if ($stage -in $projectChildStages) { continue }
        $native = Read-Tree ($stage + '.native')
        $dump = Read-Pixels $stage
        $theme = Read-Theme $stage
        if ($native.pid -ne $process.Id -or $native.hwnd -ne $window.ToInt64() -or
            $native.exe -ne $exe -or $dump.x -ne 0 -or $dump.y -ne 0 -or
            $dump.w -ne $native.w -or $dump.h -ne $native.h -or
            [Math]::Abs($theme.scale - $native.dpi * 1000 / 96) -gt 10) {
            throw "Unbound PID/HWND/native canvas/DPI evidence: $stage"
        }
        if ([long]$native.processStartTicks -ne $processStarted.Ticks -or
            [long]$native.exeMtimeTicks -ne $exeInfo.LastWriteTimeUtc.Ticks -or
            [long]$native.scriptMtimeTicks -ne $scriptInfo.LastWriteTimeUtc.Ticks) {
            throw "Stage evidence does not match the current process/input timestamps: $stage"
        }
        foreach ($extension in @('.json', '.theme.json', '.bin', '.ready', '.native.json')) {
            $file = Get-Item -LiteralPath (Join-Path $out ($stage + $extension))
            if ($file.LastWriteTimeUtc -lt $started) { throw "Stale stage evidence: $stage$extension" }
        }
    }
    if ($ProjectOverflowOnly) {
        foreach ($stage in @('project-ready', 'project-settings-closed', 'project-extensions-closed')) {
            $tree = Read-Tree $stage
            Require-ClosedDrawer $tree $stage
            Require-Target $tree 'ide-ribbon-overflow'
            Require-Geometry $tree.root (Read-Pixels $stage) 'project Form within native client'
            Require-Shadow (Read-Pixels $stage)
            Require-Skin (Read-Theme $stage) 0
        }
        Require-RibbonOverflow 'project-overflow' @(12, 15, 13, 20, 14, 18, 17)
        Require-RibbonOverflow 'project-overflow-again' @(12, 15, 13, 20, 14, 18, 17)
    } else {
        $candidateThemes = @{ '0' = (Read-Theme 'skin-zero'); '1' = (Read-Theme 'skin-one'); '2' = (Read-Theme 'navy') }
        foreach ($index in 0..2) { Require-Skin $candidateThemes[[string]$index] $index }
        $witnesses = @{}
        foreach ($stage in $fullStages) {
            $tree = Read-Tree $stage
            $dump = Read-Pixels $stage
            $theme = Read-Theme $stage
            Require-ClosedDrawer $tree $stage
            Require-WindowLayout $tree $dump $theme
            Require-Shadow $dump
            $witnesses[$stage] = @(Get-SurfaceWitnesses $tree $dump $theme)
        }
        # Re-sample each frame's real surface. Native resizing can redistribute a
        # splitter pixel, moving text onto a formerly blank absolute coordinate.
        foreach ($pair in @(@('closed-again', 'graphite-return'), @('graphite-return', 'restored'),
                @('restored', 'command-closed'))) {
            $source = @($witnesses[$pair[0]])
            $destination = @($witnesses[$pair[1]])
            if ($source.Count -ne $destination.Count) { throw 'Dock surfaces changed across a skin/resize round trip' }
            foreach ($surface in $source) {
                $matched = @($destination | Where-Object { $_.key -eq $surface.key })
                if ($matched.Count -ne 1 -or $matched[0].color -ne $surface.color) {
                    throw "Surface color was not restored after skin/resize round trip: $($pair -join ' -> ')/$($surface.key)"
                }
            }
        }
        $appearance = Read-Tree 'appearance'
        Require-DrawerWindowEdges (Read-Pixels 'before') (Read-Pixels 'appearance')
        Require-DrawerWindowEdges (Read-Pixels 'before') (Read-Pixels 'effects')
        foreach ($name in @('theme-drawer-close', 'theme-tab-0', 'theme-tab-1', 'theme-skin-0', 'theme-skin-1', 'theme-skin-2')) {
            Require-Target $appearance $name
        }
        $cards = @(Get-Targets $appearance | Where-Object { $_.name -match '^theme-skin-\d+$' })
        if ($cards.Count -ne 3) { throw 'Expected exactly the new porcelain/graphite/navy skin catalog' }
        foreach ($index in 0..2) {
            $card = @($cards | Where-Object { $_.name -eq "theme-skin-$index" })[0]
            $label = @($card.kids | Where-Object { $_.kind -eq 'Label' })
            if ($label.Count -ne 1 -or $label[0].name -ne @('浅色', '石墨', '深蓝')[$index]) {
                throw "Unexpected skin catalog identity at index $index"
            }
        }
        foreach ($stage in @('appearance', 'skin-zero', 'skin-one')) {
            $tree = Read-Tree $stage; $dump = Read-Pixels $stage
            Require-Surface $tree $dump (Read-Theme $stage)
            Require-Previews $tree $dump $candidateThemes
        }
        $effects = Read-Tree 'effects'
        foreach ($name in @('theme-drawer-close', 'theme-tab-0', 'theme-tab-1', 'theme-animations')) {
            Require-Target $effects $name
        }
        Require-Surface $effects (Read-Pixels 'effects') (Read-Theme 'effects')
        Require-Skin (Read-Theme 'porcelain-return') 0
        foreach ($stage in @('closed-again', 'graphite-return', 'small', 'small-news', 'small-home',
                'small-overflow', 'small-overflow-closed', 'narrow', 'narrow-overflow', 'narrow-closed',
                'small-closed', 'restored', 'command-closed')) {
            Require-Skin (Read-Theme $stage) 1
        }
        $newsTree = Read-Tree 'small-news'
        Require-WindowLayout $newsTree (Read-Pixels 'small-news') (Read-Theme 'small-news') $true
        foreach ($stage in @('small', 'small-home', 'small-overflow-closed', 'small-closed')) {
            Require-Target (Read-Tree $stage) 'ide-ribbon-overflow'
        }
        # The narrower native client keeps the same retained overflow contract.
        foreach ($stage in @('narrow', 'narrow-closed')) {
            $tree = Read-Tree $stage
            $shell = Require-Node $tree.root 'ide-shell-root'
            $dock = Require-Node $shell 'ide-workspace-dock'
            $page = Require-Node $shell 'ide-start-page'
            Require-Geometry $tree.root (Read-Pixels $stage) 'narrow Form within native client'
            Require-RibbonLayout $tree $dock $page
            Require-ClosedDrawer $tree $stage
            Require-Target $tree 'ide-ribbon-overflow'
        }
        Require-RibbonOverflow 'small-overflow' @(14, 18, 17)
        Require-RibbonOverflow 'narrow-overflow' @(15, 13, 20, 14, 18, 17) @(15)
    }
} finally {
    if ($null -ne $process) {
        if (!$process.HasExited) { $process.Kill($true); $process.WaitForExit() }
        $process.Dispose()
    }
    if ($dpiContext -ne [IntPtr]::Zero) {
        [void][IdeThemeProbeNative]::SetThreadDpiAwarenessContext($dpiContext)
    }
    foreach ($key in $previous.Keys) { [Environment]::SetEnvironmentVariable($key, $previous[$key]) }
    foreach ($name in $runtimeArtifacts.Keys) {
        $artifact = Join-Path $root $name
        if (!$runtimeArtifacts[$name] -and (Test-Path -LiteralPath $artifact)) {
            Move-Item -LiteralPath $artifact -Destination (Join-Path $out $name) -Force
        }
    }
    $startupError = Join-Path $cache 'ide_error.log'
    if (Test-Path -LiteralPath $startupError) {
        Copy-Item -LiteralPath $startupError -Destination (Join-Path $out 'ide_error.log') -Force
    }
    foreach ($path in @($script, $cfg)) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force -Recurse }
    }
    if ($ProjectOverflowOnly -and (Test-Path -LiteralPath $project)) {
        # Symbol indexing starts a detached compiler. Never remove its inputs
        # merely because the main IDE has exited, or touch another run's worker.
        $workerDeadline = [DateTime]::UtcNow.AddSeconds(20)
        do {
            $workers = @(Get-ProjectWorkers)
            if ($workers.Count -eq 0) { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $workerDeadline)
        if ($workers.Count -ne 0) { throw "Owned project symbol worker did not finish; kept inputs: $project" }
        Remove-Item -LiteralPath $project -Force -Recurse
    }
    if ((Test-Path -LiteralPath $work) -and @(Get-ChildItem -LiteralPath $work -Force).Count -eq 0) {
        Remove-Item -LiteralPath $work -Force
    }
}
if ($ProjectOverflowOnly) { Write-Output 'IDE_PROJECT_OVERFLOW_PROBE_PASS' }
else { Write-Output 'IDE_THEME_DRAWER_PROBE_PASS' }
