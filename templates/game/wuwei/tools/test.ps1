# Wuwei test suite: data/pack/asset regressions (Python), headless model
# tests (zanc), and with -Gui a real-window UiDriver run of gameplay.ui.
# The .ui script names buttons as @name; names are resolved in a discovery
# run via the game's "id.<name>" probes, so only numeric clickid commands
# ever reach UiDriver (no reliance on widget allocation order).
param(
    [switch]$Gui,
    [string]$GuiOutput = '',
    [string]$Compiler = $env:ZANC,
    # Optional stdlib override; lets the suite build against a pristine
    # stdlib snapshot while another session's in-flight Gui edits pass by.
    [string]$Stdlib = ''
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$repo = [IO.Path]::GetFullPath((Join-Path $project '../../..'))
$tests = Join-Path $repo 'tests/templates/wuwei'
if (-not $Compiler) {
    $local = Join-Path $repo 'build/zanc.exe'
    if (Test-Path $local) { $Compiler = $local } else { $Compiler = (Get-Command zanc -ErrorAction Stop).Source }
}
$failures = New-Object System.Collections.Generic.List[string]

function Invoke-Step([string]$name, [scriptblock]$body) {
    Write-Host "== $name"
    # Native stderr (unittest writes its summary there) must not trip
    # ErrorActionPreference=Stop; explicit throws below still fail the step.
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $body }
    catch {
        Write-Host "   FAIL: $($_.Exception.Message)"
        $script:failures.Add($name)
    }
    finally { $ErrorActionPreference = $prev }
}

# --- Python regressions: table integrity, packer crypto, asset pipeline.
foreach ($suite in @('test_data.py', 'test_pack.py', 'test_prepare_assets.py')) {
    Invoke-Step $suite { python (Join-Path $tests $suite) 2>&1 | ForEach-Object { Write-Host "   $_" }
        if ($LASTEXITCODE -ne 0) { throw "python $suite exited $LASTEXITCODE" } }
}

# --- Headless model tests: compile against the game sources without main.zan.
# 源要**递归**枚举：Model/ 下的表格读取与存档基座也是源码，漏了就编不过。
# 但要**排除 GUI 接线**：WuweiApp*.zan 是 partial class 的 code-behind，
# 它引用的控件字段由 App.html 生成——设计稿不进编译时，这些字段不存在，
# `Menu` 会落到 stdlib 的 Gui.Widget.Menu，报 "no member 'visible'"。
# 头less 用例只测模型层，不需要界面接线。
$sources = Get-ChildItem (Join-Path $project 'src') -Recurse -File -Filter *.zan |
    Where-Object { $_.Name -ne 'main.zan' -and $_.Name -notlike 'WuweiApp*' } |
    Sort-Object FullName | ForEach-Object { $_.FullName }
foreach ($case in @('model.zan', 'profiles.zan', 'security.zan')) {
    Invoke-Step $case {
        $stem = [IO.Path]::GetFileNameWithoutExtension($case)
        $exe = Join-Path $repo "build/wuwei/tests/$stem.exe"
        New-Item -ItemType Directory -Force (Split-Path -Parent $exe) | Out-Null
        # The cases assert fresh-directory conditions; a stale run's saves
        # would fail them spuriously.
        $dataDir = Join-Path $repo "build/wuwei/tests/$stem-data"
        Remove-Item $dataDir -Recurse -Force -ErrorAction SilentlyContinue
        # 不带设计稿：App.html 生成的 partial class 会把 GUI 宿主拉进无头用例
        # （model.zan 在带设计稿时会以访问冲突退出）。GUI 接线文件已在 $sources
        # 里排除，用例只测模型层。
        $zargs = @((Join-Path $tests $case)) + $sources + @('--auto-stdlib', '-o', $exe)
        if ($Stdlib) { $zargs += @('--stdlib-path', $Stdlib) }
        & $Compiler @zargs
        if ($LASTEXITCODE -ne 0) { throw "zanc $case failed ($LASTEXITCODE)" }
        Push-Location $repo
        try {
            & $exe $dataDir 2>&1 | ForEach-Object { Write-Host "   $_" }
            if ($LASTEXITCODE -ne 0) { throw "$case exited $LASTEXITCODE" }
        } finally { Pop-Location }
    }
}

# --- GUI: protected build + UiDriver full-flow regression.
if ($Gui) {
    # Resolve once at script scope: Invoke-Step bodies run in child scopes,
    # so an assignment inside gui-build would not be visible to gui-gameplay.
    if (-not $GuiOutput) { $GuiOutput = Join-Path $repo 'build/wuwei/gui-test' }
    $GuiOutput = [IO.Path]::GetFullPath($GuiOutput)
    Invoke-Step 'gui-build' {
        if (Test-Path (Join-Path $GuiOutput 'WuweiCultivation.exe')) {
            Remove-Item $GuiOutput -Recurse -Force   # fresh dir: no stale loose folders
        }
        $buildArgs = @{ Compiler = $Compiler; Output = $GuiOutput }
        if ($Stdlib) { $buildArgs.Stdlib = $Stdlib }
        & (Join-Path $PSScriptRoot 'build.ps1') @buildArgs
        if ($LASTEXITCODE -ne 0) { throw "build.ps1 exited $LASTEXITCODE" }
        foreach ($folder in @('data', 'assets', 'skins')) {
            if (Test-Path (Join-Path $GuiOutput $folder)) { throw "GUI test output ships a loose $folder directory" }
        }
    }
    Invoke-Step 'gui-gameplay' {
        $exe = Join-Path $GuiOutput 'WuweiCultivation.exe'
        if (-not (Test-Path $exe)) { throw 'WuweiCultivation.exe not found; gui-build must run first' }
        $run = Join-Path $repo 'build/wuwei/gui-run'
        Remove-Item $run -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force $run | Out-Null
        $save = Join-Path $run 'save'
        New-Item -ItemType Directory -Force $save | Out-Null

        # gameplay.ui keeps its @name click targets: UiDriver resolves them
        # through the game's "id.<name>" probes in the same process, which is
        # the only exact resolution (widget ids drift across process runs).
        $script = Join-Path $run 'verify.ui'
        [IO.File]::Copy((Join-Path $tests 'gameplay.ui'), $script)

        $prev = @{}
        foreach ($key in @('WUWEI_SAVE_DIR', 'ZAN_UI_SCRIPT', 'ZAN_UI_OUT', 'SDL_AUDIODRIVER')) {
            $prev[$key] = [Environment]::GetEnvironmentVariable($key)
        }
        $env:WUWEI_SAVE_DIR = $save
        $env:SDL_AUDIODRIVER = 'dummy'
        try {
            $env:ZAN_UI_SCRIPT = $script
            $env:ZAN_UI_OUT = "$run/verify.out"
            $proc = Start-Process -FilePath $exe -WorkingDirectory $GuiOutput -PassThru
            if (-not $proc.WaitForExit(90000)) {
                try { $proc.Kill() } catch {}
                throw "gui run exceeded 90s and was killed"
            }
            $out = [IO.File]::ReadAllText("$run/verify.out/results.log")
            $done = [regex]::Matches($out, 'DONE pass=(\d+) fail=(\d+)') | Select-Object -Last 1
            if (-not $done) { throw "gui run produced no DONE summary" }
            if ([int]$done.Groups[2].Value -ne 0) {
                $out -split "`n" | Where-Object { $_ -match '^FAIL' } | Select-Object -First 10 |
                    ForEach-Object { Write-Host "   $_" }
                throw "gui run had $($done.Groups[2].Value) failing assertion(s)"
            }
            if ([int]$done.Groups[1].Value -eq 0) { throw 'gui run recorded no assertions' }
        } finally {
            foreach ($key in $prev.Keys) {
                if ($null -ne $prev[$key] -and $prev[$key] -ne '') { Set-Item -Path "env:$key" -Value $prev[$key] }
                else { Remove-Item -Path "env:$key" -ErrorAction SilentlyContinue }
            }
        }
        Write-Host "   gameplay.ui passed: $($done.Groups[1].Value) assertions (saves under $save)"
    }
}

if ($failures.Count -gt 0) {
    Write-Host ("FAILED: " + ($failures -join ', '))
    exit 1
}
Write-Host 'ALL PASS'
