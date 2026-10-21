# 唯一的构建入口（受串行闸保护）
#
# 为什么要有这个脚本：
#   `cmake --build build` 是裸命令，谁都能直接跑。共享一个 build/ 时，
#   两个会话同时重编就会互相踩坏产物——一个会话的链接把 zanc.exe 删了
#   （链接失败会先删输出，见 docs/BUILD_TOOLCHAIN.md），另一个会话的
#   ctest 正用着它，于是成片用例假红。
#
#   AGENTS.md 规则 8 写了"测试期间不得有其他构建"，但那是文字约束，没有
#   机制，执行时必然漏。历史上大家用另一个办法绕：各自建一棵构建树
#   （build_mine、build-zancprobe…）。那是用磁盘换隔离——几百 MB 到 2 GB
#   的重复构建，而且公共包一改副本就漂移。副本没有让共享状态消失，只是
#   把它藏起来了，漂移照旧。
#
#   正确做法是**串行等待**：拿不到共享构建资源就排队等。所以本脚本把
#   "构建"这件事收进一把跨进程互斥锁。
#
# 用法：
#   pwsh -File scripts/build.ps1                      # 构建全部
#   pwsh -File scripts/build.ps1 -Target zanc         # 只构建某个目标
#   pwsh -File scripts/build.ps1 -Configure            # 先重新配置再构建
#   pwsh -File scripts/build.ps1 -SkipGate             # 逃生口（极少用）
#
# 逃生口的正当用途：闸被别的会话占着且那个会话已死（Mutex 会被内核自动
# 释放，所以这基本不会发生）。用了必须在提交信息里写明原因。

param(
    [string]$Target = '',
    [switch]$Configure,
    [switch]$SkipGate
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build'
$gate = Join-Path $PSScriptRoot 'build_gate.ps1'

if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    Write-Host "[build] 未配置，先 cmake -B build" -ForegroundColor Yellow
    $Configure = $true
}

$doConfigure = {
    & cmake -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "cmake configure 失败 exit=$LASTEXITCODE" }
}

$doBuild = {
    if ($Target -ne '') {
        & cmake --build $buildDir --target $Target
    } else {
        & cmake --build $buildDir
    }
    if ($LASTEXITCODE -ne 0) { throw "cmake --build 失败 exit=$LASTEXITCODE" }
}

if ($SkipGate) {
    Write-Host "[build] 跳过串行闸（逃生口）" -ForegroundColor Yellow
    if ($Configure) { & $doConfigure }
    & $doBuild
    exit $LASTEXITCODE
}

# 闸的锁名与 test.ps1 共用同一把 "build" 锁吗？不——用独立的 'compile'
# 锁，因为"编译"和"跑测试"可以并行：测试用的是已经编好的产物，只要没人
# 正在重编就安全。真正互斥的是**两个编译**之间，以及**编译 vs 测试**
# （后者靠 test.ps1 在自己的锁上等待，见 build_gate.ps1 的说明）。
#
# 但这里有个必须堵住的洞：ctest 用 build\zanc.exe，编译会替换它。所以
# 编译与测试必须互斥。这里用同一把锁 'build'，两个脚本都取它：
# 编译持锁期间测试排队，测试持锁期间编译排队。粒度粗一点，但正确性
# 优先——假红的排查成本远高于多等一会儿。
& $gate -Name 'build' -Script {
    if ($Configure) { & $doConfigure }
    & $doBuild
}.GetNewClosure()

Write-Host "[build] OK" -ForegroundColor Green
exit 0
