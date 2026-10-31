# A/B 对照编译器：从任意 git ref 编一份独立的 zanc，用来回答
# "这个改动之前/之后行为到底差在哪"。
#
# 为什么需要它（坑出处）：
#   修编译器 bug 时，"修复前会红、修复后会绿"这句话必须**真的量过**，
#   不能靠推理。量它的办法是拿两个编译器编同一份代码。
#   现造这个量具的代价是：worktree 检出 1.4 万个文件 + 两次完整 LLVM
#   编译 ≈ 27 分钟——比修复本身贵三倍，于是每轮任务都在犹豫要不要量，
#   最后就只凭推理收工（然后在下一轮翻车）。
#   本脚本把这套量具固化下来：第一次建树，之后**增量复用**
#   （源码没动时 ninja 空跑，几秒出结果）。
#
# 用法：
#   pwsh -File scripts/ab_compiler.ps1 -Ref ae2b26588^      # 编那个 ref
#   pwsh -File scripts/ab_compiler.ps1 -Ref HEAD            # 编当前
#   pwsh -File scripts/ab_compiler.ps1 -Ref ae2b26588^ -Clean   # 重建
#
# 产物：
#   _scratch\ab\<ref 净化后的名字>\build\zanc.exe
#   同目录下有 stdlib/ 与 packages/ 的目录联接（--auto-stdlib 找的是
#   **编译器同级**的 stdlib/，或者它的上一级；联接省得每次手工摆）。
#
# 注意：这份编译器**只用来量行为**，不要拿它去跑共享测试套件——
# 它连的是 _scratch 里的 stdlib 副本，与工作区的 stdlib 会漂移。

param(
    [Parameter(Mandatory = $true)][string]$Ref,
    [switch]$Clean,
    [int]$TimeoutSec = 3600
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# ref 名净化成目录名：refs/heads/x、ae2b26588^ 之类都要能落成合法路径。
$safe = ($Ref -replace '[^A-Za-z0-9._-]', '_')
if ($safe.Length -gt 60) { $safe = $safe.Substring(0, 60) }
# 量具的落点很讲究：
#   不能放 _scratch/——那里会被 clean_scratch.ps1（7 天以上）与
#   sched.ps1 reap 当作一次性产物回收，下一轮任务就得重造一遍
#   （worktree 检出 1.4 万文件 + 两次完整 LLVM 编译 ≈ 27 分钟）。
#   也不能放仓库外——reap 会清"仓库外没人引用的 worktree 副本"。
#   build/ 是 git-ignored 的构建区，不会被人扫，也不在回收范围内。
$base = Join-Path $root "build\ab\$safe"
$wt = Join-Path $base 'wt'
$bd = Join-Path $base 'build'

if ($Clean -and (Test-Path -LiteralPath $base)) {
    Remove-Item -LiteralPath $base -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $base | Out-Null

# ---- 工具链参数从主 build 的 CMakeCache 里抄 --------------------------------
# 硬编码 LLVM 路径在新机器上必然失效；主 build 的 cache 是唯一真相源。
$cache = Join-Path $root 'build\CMakeCache.txt'
if (!(Test-Path -LiteralPath $cache)) {
    throw "主 build 的 CMakeCache.txt 不存在——先在主仓跑一次 cmake -B build"
}
$cacheTxt = Get-Content -LiteralPath $cache -Raw
function CacheVal($key) {
    $m = [regex]::Match($cacheTxt, "(?m)^$key[^=]*=(.*)$")
    if (!$m.Success) { return '' }
    return $m.Groups[1].Value.Trim()
}
$llvmDir = CacheVal 'LLVM_DIR'
$cc = CacheVal 'CMAKE_C_COMPILER'
$cxx = CacheVal 'CMAKE_CXX_COMPILER'
$ld = CacheVal 'CMAKE_LINKER'
if ($llvmDir -eq '') { throw "主 build 的 CMakeCache 里没有 LLVM_DIR" }
if ($cc -eq '' -or $cxx -eq '') {
    # cache 里没有编译器条目就退到 PATH 里的 clang——主 build 一定是
    # 用 clang 配的（仓库的工具链就是它），裸名交给 cmake 自己找。
    $cc = 'clang'
    $cxx = 'clang++'
}

# ---- worktree ----------------------------------------------------------------
# 判据不能是"目录在不在"——目录在但检出没落地时，stdlib 联接会指向空目录，
# 编译器连 stdlib 都找不到（报的是 "undefined type 'Exception'" 这种跟被测
# 代码毫不相干的错）。判据是"wt/stdlib 在不在"。
if (!(Test-Path -LiteralPath (Join-Path $wt 'stdlib'))) {
    if (Test-Path -LiteralPath $wt) {
        # 半成品：目录在、检出没落地。清掉重来。
        & git -C $root worktree remove --force $wt 2>&1 | Out-Null
        if (Test-Path -LiteralPath $wt) {
            Remove-Item -LiteralPath $wt -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    & git -C $root worktree prune
    & git -C $root worktree add --detach $wt $Ref
    if ($LASTEXITCODE -ne 0) { throw "worktree add 失败: $Ref" }
}
if (!(Test-Path -LiteralPath (Join-Path $wt 'stdlib'))) {
    throw "worktree 检出不完整（$wt 下没有 stdlib）: $Ref"
}

# ---- 配置 + 构建（增量复用：源码没动就是 ninja 空跑） -------------------------
# -S 必须指向 worktree：漏了它 cmake 会拿**当前目录**当源码目录，于是编出来
# 的是当前工作树而不是 $Ref —— 那还量什么"改动之前"？量的还是"之后"。
# 旧实现没给 -S，产物里 CMakeCache 的 CMAKE_HOME_DIRECTORY 指的是主仓，
# 整把量具一直是坏的（头一次用就撞上）。
if (Test-Path -LiteralPath (Join-Path $bd 'CMakeCache.txt')) {
    $cached = ''
    foreach ($line in Get-Content -LiteralPath (Join-Path $bd 'CMakeCache.txt')) {
        if ($line -like 'CMAKE_HOME_DIRECTORY:*') { $cached = $line.Split('=', 2)[1] }
    }
    if ($cached -and ([System.IO.Path]::GetFullPath($cached)) -ne
        ([System.IO.Path]::GetFullPath($wt))) {
        # 缓存指向别的源码目录：清掉重来，别拿旧缓存接着编。
        Remove-Item -LiteralPath $bd -Recurse -Force -ErrorAction SilentlyContinue
    }
}
if (!(Test-Path -LiteralPath (Join-Path $bd 'build.ninja'))) {
    # 用参数数组而不是裸展开：LINKER 的路径带空格
    #（C:/Program Files/...），裸展开会被原生命令拆成两个参数。
    $cfgArgs = @(
        '-S', $wt,
        '-B', $bd, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_C_COMPILER=$cc", "-DCMAKE_CXX_COMPILER=$cxx",
        "-DCMAKE_LINKER=$ld", "-DLLVM_DIR=$llvmDir"
    )
    & cmake @cfgArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake 配置失败" }
}
& cmake --build $bd --target zanc
if ($LASTEXITCODE -ne 0) { throw "构建 zanc 失败" }

$zanc = Join-Path $bd 'zanc.exe'
if (!(Test-Path -LiteralPath $zanc)) { throw "产物没出来: $zanc" }

# ---- stdlib / packages 联接 ---------------------------------------------------
# --auto-stdlib 找的是**编译器同级**的 stdlib/（或它的上一级）。
# 没有它，编译任何用到 using 的代码都报"undefined type"这类不相干的错。
foreach ($pair in @(@('stdlib', 'stdlib'), @('packages', 'packages'))) {
    $link = Join-Path $bd $pair[0]
    $target = Join-Path $wt $pair[1]
    if ((Test-Path -LiteralPath $link) -and !(Get-Item -LiteralPath $link).Target) {
        Remove-Item -LiteralPath $link -Recurse -Force
    }
    if (!(Test-Path -LiteralPath $link)) {
        New-Item -ItemType Junction -Path $link -Target $target | Out-Null
    }
}

Write-Output ("AB_ZANC_OK {0}" -f $zanc)
Write-Output ("AB_REF {0}" -f $Ref)
# 源码目录一并打出来：量出来的差异到底来自哪个树，一眼可查
# （旧实现漏了 cmake 的 -S，编的是当前工作树，这行能当场戳穿）。
Write-Output ("AB_SRC {0}" -f $wt)
