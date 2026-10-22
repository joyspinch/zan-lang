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
    # 链接 zanc.exe 时它可能正被别的进程执行着（仓库外的游戏项目拿
    # $ZAN_HOME\build\zanc.exe 当编译器）。Windows 上运行中的 exe 不可被
    # 替换，lld 会报 "permission denied" 并**先删掉输出**——于是：
    #   一边是这次构建失败，另一边是下一个调用者拿到"文件不存在"。
    # 占用者通常几百毫秒就退出（一次编译而已），所以等一下重试即可。
    # 只对"被占用"这一种失败重试，真实编译错误照常立刻失败。
    $maxTry = 6
    for ($try = 1; $try -le $maxTry; $try++) {
        # 这一次调用把 ErrorActionPreference 临时降为 Continue：native 命令把
        # stderr 写进管道时，Stop 会把它当成终止性错误直接抛出——那样就永远
        # 走不到下面的"被占用就重试"分支了。构建失败与否由退出码判定。
        $prevEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $out = & cmake --build $buildDir $(if ($Target -ne '') { @('--target', $Target) }) 2>&1
            $rc = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $prevEap
        }
        if ($rc -eq 0) { $out | Write-Host; return }

        $text = ($out | Out-String)
        $busy = ($text -match 'permission denied') -or ($text -match 'unable to remove file') -or
                ($text -match 'being used by another process') -or ($text -match 'text file is busy')
        if (-not $busy -or $try -eq $maxTry) {
            $out | Write-Host
            throw "cmake --build 失败 exit=$rc"
        }
        $out | Select-Object -Last 4 | Write-Host
        Write-Host "[build] zanc.exe 正被占用（第 $try/$maxTry 次），2 秒后重试……" -ForegroundColor Yellow
        Start-Sleep -Seconds 2
    }
}

if ($SkipGate) {
    Write-Host "[build] 跳过串行闸（逃生口）" -ForegroundColor Yellow
    if ($Configure) { & $doConfigure }
    & $doBuild
    exit $LASTEXITCODE
}

# 闸的锁名与 test.ps1 共用同一把 'build' 锁——编译与测试必须互斥：
# 编译持锁时测试排队，测试持锁时编译排队。粒度粗一点，但正确性优先——
# 假红的排查成本远高于多等一会儿。
#
# 退出码经临时文件回传：`& $gate` 在子作用域里跑，$script: 变量穿不过去
# （各自的 script scope），闭包里的 $LASTEXITCODE 同样出不来。
$gate = Join-Path $PSScriptRoot 'build_gate.ps1'
$schedPath = Join-Path $PSScriptRoot 'sched.ps1'
$codeFile = Join-Path $env:TEMP "zan_build_code_$PID.txt"
& $gate -Name 'build' -Script {
    # 顺手回收上一轮留下的副本：过期任务租约、没人引用的仓库外 worktree。
    # 放在闸里跑，跟编译/测试互斥，不会跟别人的构建抢同一个目录。
    if (Test-Path $schedPath) { & $schedPath reap -Apply | Out-Null }
    if ($Configure) { & $doConfigure }
    & $doBuild
    Set-Content -Path $codeFile -Value $LASTEXITCODE -Encoding ASCII
}.GetNewClosure()

$code = 1
if (Test-Path $codeFile) {
    $raw = (Get-Content $codeFile -Raw).Trim()
    if ($raw -match '^\d+$') { $code = [int]$raw }
    Remove-Item -LiteralPath $codeFile -Force -ErrorAction SilentlyContinue
}
if ($code -eq 0) { Write-Host "[build] OK" -ForegroundColor Green }
else { Write-Host "[build] FAILED exit=$code" -ForegroundColor Red }
exit $code

Write-Host "[build] OK" -ForegroundColor Green
exit 0
