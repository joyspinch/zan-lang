<#
.SYNOPSIS
    编译器前端（lexer/parser/nsresolve/binder/checker）变异模糊测试。

.DESCRIPTION
    把 tests/fuzz/fuzz_frontend.c 这个 libFuzzer 入口真正跑起来：

      1. 用 clang -fsanitize=fuzzer,address 编出 harness（ASan 抓内存错误）；
      2. 把 tests/conformance 与 tests/diag 的用例当种子灌进语料目录；
      3. 让 libFuzzer 按覆盖反馈自己变异、跑 N 秒；
      4. 崩了就把 artifact 留在 artifacts 目录并打印路径。

    ⚠ 链接时必须和 zanc 一样带大栈预留（Windows: -Xlinker /STACK:33554432，
    GNU ld: -Wl,--stack,33554432）。zanc 本体在 CMakeLists.txt:192 就是这么链的；
    不带的话 harness 拿到的是默认 1MB 栈，**约 50 层括号嵌套就爆栈**——那是
    假阳性，不是编译器缺陷（解析器本身有 ZAN_PARSER_MAX_EXPR_DEPTH=512 的守卫，
    见 src/compiler/parser.c:1573，真 zanc 上同样输入报的是
    "expression nesting too deep (max 512)"）。

.PARAMETER Seconds
    跑多少秒，默认 300。

.PARAMETER CorpusDir
    语料目录（libFuzzer 会往里加新语料），默认 _scratch/fuzz/corpus。

.PARAMETER ArtifactDir
    崩溃 artifact 落盘目录，默认 _scratch/fuzz/artifacts。

.PARAMETER Clang
    clang 可执行文件路径。默认按顺序探测：
    $env:ZAN_CLANG → mozbuild 的 clang → PATH 上的 clang。

.PARAMETER Reseed
    重建种子语料（默认只补齐缺失的种子，不清空已有语料——语料是资产）。

.PARAMETER MaxLen
    单个输入最大字节数，默认 16384。

.EXAMPLE
    pwsh -ExecutionPolicy Bypass -File scripts\fuzz_frontend.ps1 -Seconds 600
#>
[CmdletBinding()]
param(
    [int]$Seconds = 300,
    [string]$CorpusDir = '',
    [string]$ArtifactDir = '',
    [string]$Clang = '',
    [switch]$Reseed,
    [int]$MaxLen = 16384
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

if (-not $CorpusDir) { $CorpusDir = Join-Path $repo '_scratch\fuzz\corpus' }
if (-not $ArtifactDir) { $ArtifactDir = Join-Path $repo '_scratch\fuzz\artifacts' }

# ── 1. 找 clang ────────────────────────────────────────────────────────────────
function Resolve-Clang {
    param([string]$Explicit)
    if ($Explicit) {
        if (Test-Path $Explicit) { return (Resolve-Path $Explicit).Path }
        throw "指定的 clang 不存在: $Explicit"
    }
    if ($env:ZAN_CLANG -and (Test-Path $env:ZAN_CLANG)) { return (Resolve-Path $env:ZAN_CLANG).Path }
    # mozbuild 的 clang 是本仓库 Windows 工具链的正规来源
    $moz = Join-Path $env:USERPROFILE '.mozbuild\clang\bin\clang.exe'
    if (Test-Path $moz) { return $moz }
    $onPath = Get-Command clang -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    throw '找不到 clang：装 mozbuild LLVM，或用 -Clang / $env:ZAN_CLANG 指定路径'
}
$clang = Resolve-Clang $Clang
Write-Host "[fuzz] clang: $clang"

# ── 2. 探测链接器风格与 ASan 运行库 ───────────────────────────────────────────
$target = (& $clang -dumpmachine 2>$null) -join ''
$isMsvcTarget = ($target -match 'x86_64-pc-windows-msvc') -or ($target -match 'aarch64-pc-windows-msvc')
$onWindows = ($target -match 'windows') -or ($target -eq '')

$linkStack = @()
if ($isMsvcTarget) {
    $linkStack = @('-Xlinker', '/STACK:33554432')
} elseif ($onWindows) {
    $linkStack = @('-Xlinker', '/STACK:33554432')
} else {
    $linkStack = @('-Wl,--stack,33554432')
}

$exeName = if ($onWindows) { 'fuzz_frontend.exe' } else { 'fuzz_frontend' }
$buildDir = Join-Path $repo '_scratch\fuzz'
$harness = Join-Path $buildDir $exeName
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path $CorpusDir | Out-Null
New-Item -ItemType Directory -Force -Path $ArtifactDir | Out-Null

# ── 3. 编译 harness ───────────────────────────────────────────────────────────
$frontendSources = @(
    'src/compiler/arena.c', 'src/compiler/diag.c', 'src/compiler/lexer.c',
    'src/compiler/ast.c', 'src/compiler/parser.c', 'src/compiler/nsresolve.c',
    'src/compiler/binder.c', 'src/compiler/checker.c', 'src/compiler/definite.c',
    'src/compiler/builtin_api.c', 'tests/fuzz/fuzz_frontend.c'
)
$clangArgs = @(
    '-O1', '-g', '-fsanitize=fuzzer,address', '-D_CRT_SECURE_NO_WARNINGS',
    '-I', 'src/compiler', '-I', 'src/common',
    '-o', $harness
) + $linkStack + $frontendSources

Write-Host '[fuzz] 编译 harness ...'
& $clang @clangArgs
if ($LASTEXITCODE -ne 0) { throw "harness 编译失败（exit $LASTEXITCODE）" }

# Windows 下 ASan 是动态库，运行时要在 exe 旁边（或者在 PATH 里）
if ($onWindows) {
    $clangRoot = Split-Path -Parent (Split-Path -Parent $clang)
    # 运行库必须和宿主架构一致（x64 机器上拷 aarch64 的 DLL → 0xC0000135 DLL 找不到）
    $arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'aarch64' } else { 'x86_64' }
    $asanDll = Get-ChildItem -Path (Join-Path $clangRoot 'lib\clang') -Recurse -Filter "clang_rt.asan_dynamic-$arch.dll" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($asanDll) {
        Copy-Item $asanDll.FullName (Join-Path $buildDir $asanDll.Name) -Force
        Write-Host "[fuzz] ASan 运行库: $($asanDll.Name)"
    } else {
        Write-Warning '没找到 clang_rt.asan_dynamic-*.dll——harness 运行时可能报 0xC0000135'
    }
}

# ── 4. 种子语料 ───────────────────────────────────────────────────────────────
$seedCount = 0
if ($Reseed) {
    # 不删语料（语料是资产）——整体挪到带时间戳的备份目录，再开新的
    if (Test-Path $CorpusDir) {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $backup = Join-Path (Split-Path -Parent $CorpusDir) "corpus_prev-$stamp"
        Move-Item -LiteralPath $CorpusDir -Destination $backup
        Write-Host "[fuzz] 旧语料已挪到 $backup"
    }
    New-Item -ItemType Directory -Force -Path $CorpusDir | Out-Null
}
foreach ($dir in @('tests/conformance', 'tests/diag')) {
    $src = Join-Path $repo $dir
    if (-not (Test-Path $src)) { continue }
    $i = 0
    foreach ($f in Get-ChildItem $src -Filter '*.zan') {
        $dest = Join-Path $CorpusDir ('{0:D4}_{1}' -f $i++, $f.Name)
        if (-not (Test-Path $dest)) {
            Copy-Item $f.FullName $dest
            $seedCount++
        }
    }
}
Write-Host "[fuzz] 语料目录: $CorpusDir（新种子 $seedCount 个，总计 $((Get-ChildItem $CorpusDir -File).Count) 个）"

# ── 5. 跑 ────────────────────────────────────────────────────────────────────
$artifactPrefix = Join-Path $ArtifactDir 'crash-'
$fuzzArgs = @(
    $CorpusDir,
    "-max_total_time=$Seconds",
    "-max_len=$MaxLen",
    '-rss_limit_mb=4096',
    '-print_final_stats=1',
    "-artifact_prefix=$artifactPrefix"
)
$env:ASAN_OPTIONS = 'detect_leaks=0:abort_on_error=0'
Write-Host "[fuzz] 跑 $Seconds 秒 ..."

& $harness @fuzzArgs
$code = $LASTEXITCODE

$artifacts = @(Get-ChildItem $ArtifactDir -Filter 'crash-*' -File -ErrorAction SilentlyContinue)
if ($code -ne 0 -or $artifacts.Count -gt 0) {
    Write-Host ''
    Write-Host "[fuzz] ❌ 崩了（exit $code）" -ForegroundColor Red
    foreach ($a in $artifacts) { Write-Host "    artifact: $($a.FullName)" }
    Write-Host '    复现: & <harness> <artifact>'
    exit 1
}
Write-Host "[fuzz] ✅ $Seconds 秒内没有崩溃"
exit 0
