# End-to-end pipeline test: for every disk template under templates\,
# create a project (mirroring the IDE's CreateProjectT/CopyTreeT and the
# templates_build gate), build it with zanc, smoke-run console targets, and
# publish (--publish), reporting a PASS/FAIL summary. Run from the repository
# root:
#   powershell -ExecutionPolicy Bypass -File scripts\e2e_pipeline.ps1
# Narrow to a subset while iterating (wildcards allowed):
#   powershell -ExecutionPolicy Bypass -File scripts\e2e_pipeline.ps1 -Only goldminer
#   ... -Only "gui-web*"
param([string]$Only = "")
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$zanc = Join-Path $root "build\zanc.exe"
if (!(Test-Path $zanc)) { Write-Output "MISSING build\zanc.exe - build the compiler first"; exit 1 }
$stdlib = Join-Path $root "stdlib"
$buildDir = Join-Path $root "build"
$work = Join-Path $root "_scratch\e2e_projects"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force -Path $work | Out-Null

function Read-Manifest($path) {
    $m = @{}
    foreach ($line in Get-Content $path -Encoding UTF8) {
        $eq = $line.IndexOf("=")
        if ($eq -gt 0) { $m[$line.Substring(0, $eq).Trim()] = $line.Substring($eq + 1).Trim() }
    }
    return $m
}

function Copy-TemplateTree($src, $dst, $name) {
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    foreach ($f in Get-ChildItem $src -File -Force) {
        if ($f.Name -eq "template.manifest" -or $f.Name -eq "manifest.txt") { continue }
        $body = Get-Content $f.FullName -Raw -Encoding UTF8
        $body = $body -replace [regex]::Escape("{{NAME}}"), $name
        Set-Content -Path (Join-Path $dst $f.Name) -Value $body -Encoding UTF8 -NoNewline
    }
    foreach ($d in Get-ChildItem $src -Directory -Force) {
        Copy-TemplateTree $d.FullName (Join-Path $dst $d.Name) $name
    }
}

$results = @()
$manifests = Get-ChildItem -Path (Join-Path $root "templates") -Recurse -Filter "template.manifest" | Sort-Object FullName
foreach ($mf in $manifests) {
    $tplDir = $mf.DirectoryName
    $m = Read-Manifest $mf.FullName
    $kind = $m["kind"]; $type = $m["type"]; $target = $m["target"]; $entry = $m["entry"]
    if (-not $entry) { $entry = "src/main.zan" }
    $tplId = Split-Path -Leaf $tplDir
    if ($Only -and $tplId -notlike $Only) { continue }
    $name = "E2E_" + ($tplId -replace "[^A-Za-z0-9]", "_")
    $entry = $entry -replace [regex]::Escape("{{NAME}}"), $name
    $proj = Join-Path $work $name
    Write-Output "=== [$tplId] $($m['name']) -> $proj"

    # 1) create: copy tree with {{NAME}} substituted, rename the GUI pair
    #    src/App.zan + src/App.html to the project name (same as the gate and
    #    the IDE), then resolve the entry the way the gate does.
    Copy-TemplateTree $tplDir $proj $name
    if (!(Test-Path (Join-Path $proj "zan.proj"))) {
        Set-Content -Path (Join-Path $proj "zan.proj") -Encoding UTF8 -Value @(
            "name = $name", "type = $type", "entry = $entry",
            "target = $target", "platform = windows-x64")
    }
    if ($type -eq "gui") {
        foreach ($ext in @("zan", "html")) {
            $app = Join-Path $proj "src\App.$ext"
            if (Test-Path $app) { Move-Item -Path $app -Destination (Join-Path $proj "src\$name.$ext") -Force }
        }
        if (Test-Path (Join-Path $proj "src\$name.html")) { $entry = "src/$name.html" }
    }
    $entryFull = Join-Path $proj ($entry -replace "/", "\")
    if (!(Test-Path $entryFull)) {
        $results += "FAIL create [$tplId]: entry $entry missing"
        continue
    }

    # 2) build: the entry is ALWAYS the first input (zanc emits Main from the
    #    first input); side design documents (.html/.htm next to the
    #    entry, e.g. gui-wechat's runtime windows) ride the same command line
    #    or their Build() references fail to resolve; then every other src
    #    .zan. Same input order as tests/run_templates.cmake.
    $srcDir = Join-Path $proj "src"
    if (!(Test-Path $srcDir)) {
        $results += "SKIP [$tplId]: no src\ tree (nothing to compile)"
        continue
    }
    $inputs = @($entryFull)
    $inputs += @(Get-ChildItem $srcDir -Recurse -File -Include "*.html", "*.htm" |
        Where-Object { $_.FullName -ne $entryFull } | ForEach-Object { $_.FullName })
    $inputs += @(Get-ChildItem $srcDir -Recurse -File -Filter "*.zan" | ForEach-Object { $_.FullName })
    $inputs = @($inputs | Select-Object -Unique)
    $sub = @()
    if ($type -eq "gui") {
        $sub = @("--subsystem", "windows", "--driver-dir", $buildDir)
    }

    if ($target -eq "dll") {
        $lib = Join-Path $proj "build_out.dll"
        & $zanc --stdlib-path $stdlib --emit-lib @inputs -o $lib 2>&1 | Select-Object -Last 3
        if ($LASTEXITCODE -ne 0 -or !(Test-Path $lib)) {
            $results += "FAIL build [$tplId] (library) exit=$LASTEXITCODE"
        } else {
            $results += "PASS [$tplId] library link ok"
        }
        continue
    }

    $exe = Join-Path $proj "build_out.exe"
    & $zanc --stdlib-path $stdlib @inputs -o $exe @sub 2>&1 | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0 -or !(Test-Path $exe)) {
        $results += "FAIL build [$tplId] exit=$LASTEXITCODE"
        continue
    }

    # 3) run: console targets get a 5 s smoke run (servers/GUIs are killed;
    #    a timeout is not a failure for long-running kinds)
    $runOk = "skipped"
    if ($type -eq "console" -and $target -eq "exe") {
        $p = Start-Process -FilePath $exe -WorkingDirectory $proj -PassThru -WindowStyle Hidden
        $exited = $p.WaitForExit(5000)
        if ($exited) {
            $runOk = "exit=$($p.ExitCode)"
            if ($p.ExitCode -ne 0) { $results += "FAIL run [$tplId] exit=$($p.ExitCode)"; continue }
        } else {
            Stop-Process -Id $p.Id -Force
            $runOk = "long-running(killed)"
        }
    }

    # 4) publish: optimized/stripped build into publish\ (mirrors the IDE)
    New-Item -ItemType Directory -Force -Path (Join-Path $proj "publish") | Out-Null
    $pubExe = Join-Path $proj "publish\$name.exe"
    & $zanc --stdlib-path $stdlib --publish @inputs -o $pubExe @sub 2>&1 | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0 -or !(Test-Path $pubExe)) {
        $results += "FAIL publish [$tplId] exit=$LASTEXITCODE"
        continue
    }
    $results += "PASS [$tplId] build+publish ok, run: $runOk"
}

Write-Output ""
Write-Output "===== E2E PIPELINE SUMMARY ====="
$results | ForEach-Object { Write-Output $_ }
$fails = @($results | Where-Object { $_ -like "FAIL*" })
Write-Output "total=$($results.Count) fail=$($fails.Count)"
if ($fails.Count -gt 0) { exit 1 } else { Write-Output "E2E_PIPELINE_OK"; exit 0 }
