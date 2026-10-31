# 调试版 IDE 构建：出带调试信息的 exe，且**不占用** build\ZanIDE.exe。
#
# 为什么需要它（坑出处）：
#   调试崩溃时要用 file:line，得用 -g 编；但 -g + 不 --publish 的产物
#   是 41.6 MB（.text 30.8 MB + .debug_* 6.4 MB），而发布版只有 18 MB。
#   手改 build_ide.ps1 的环境变量（IDE_NO_PUBLISH=1 / ZAN_IDE_ZANC_ARGS="-g"）
#   重编一次，就把 **常规产物** build\ZanIDE.exe 换成了调试版——
#   之后没人记得切回来，IDE 的体积凭空翻倍，还查不出是谁干的。
#   本脚本把调试版**另存**为 build\ZanIDE.debug.exe，再把发布版原样放回，
#   于是 build\ZanIDE.exe 永远是发布形态。
#
# 用法：
#   pwsh -File scripts/build_ide_debug.ps1                 # 调试版（-g，带 file:line）
#   pwsh -File scripts/build_ide_debug.ps1 -ArcGuard       # 再加 ARC 守卫与泄漏检查
#   pwsh -File scripts/build_ide_debug.ps1 -NoRebuild      # 只做归档/还原，不重编
#
# 产物：
#   build\ZanIDE.debug.exe  调试版（-g，含 DWARF 行表）
#   build\ZanIDE.exe        发布版（发布形态保持不变）

param(
    [switch]$ArcGuard,
    [switch]$NoRebuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$release = Join-Path $root 'build\ZanIDE.exe'
$debug = Join-Path $root 'build\ZanIDE.debug.exe'
$backup = Join-Path $root 'build\ZanIDE.release.bak'

# 调试参数：-g 给 DWARF 行表（崩溃日志能落到 file:line）；
# --arc-guard / --check-leaks 抓 ARC 与泄漏。
$env:IDE_NO_PUBLISH = '1'
$env:ZAN_IDE_ZANC_ARGS = if ($ArcGuard) { '-g --arc-guard --check-leaks' } else { '-g' }

try {
    if (!$NoRebuild) {
        # build_ide.ps1 会覆盖 build\ZanIDE.exe。先把当前的发布版挪走，
        # 调试版编出来后归档，最后把发布版放回去。
        if (Test-Path -LiteralPath $release) {
            Copy-Item -LiteralPath $release -Destination $backup -Force
        }
        & (Join-Path $PSScriptRoot 'build_ide.ps1')
        if ($LASTEXITCODE -ne 0) { throw "调试版构建失败" }
        if (Test-Path -LiteralPath $debug) { Remove-Item -LiteralPath $debug -Force }
        Move-Item -LiteralPath $release -Destination $debug -Force
        Write-Output ("DEBUG_IDE_OK {0}  ({1:N1} MB)" -f $debug, ((Get-Item -LiteralPath $debug).Length / 1MB))
    } elseif (Test-Path -LiteralPath $release) {
        Copy-Item -LiteralPath $release -Destination $debug -Force
        Write-Output ("DEBUG_IDE_OK {0}  (未重编，直接归档现有产物)" -f $debug)
    }
} finally {
    # 还原发布版：调试版不该占着常规产物的位置。
    if (Test-Path -LiteralPath $backup) {
        Copy-Item -LiteralPath $backup -Destination $release -Force
        Remove-Item -LiteralPath $backup -Force
        Write-Output ("RELEASE_RESTORED {0}  ({1:N1} MB)" -f $release, ((Get-Item -LiteralPath $release).Length / 1MB))
    }
    Remove-Item Env:\IDE_NO_PUBLISH -ErrorAction SilentlyContinue
    Remove-Item Env:\ZAN_IDE_ZANC_ARGS -ErrorAction SilentlyContinue
}
