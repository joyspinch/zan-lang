# 验"裸 ctest 也会排队"这一条对外承诺。
#
# 做法：先让一个常驻进程占着共享构建锁，再**裸跑**一次 ctest（不经过
# scripts/test.ps1），看它是不是**等**到锁释放才开跑——而不是抢跑、或直接失败。
#
# 为什么不在 ctest 里注册成用例：它要跑 ctest，套在 ctest 里会变成
# ctest 套 ctest（外层持锁、内层靠"祖先持锁"识别绕开，能跑但没必要）。
# 需要时手动跑：
#   pwsh -File tests\ctest_queue_probe.ps1
# 退出码 0 = 裸 ctest 确实等到了锁。
# 验"裸 ctest 也会排队"：先让一个常驻进程占着共享锁，再裸跑一次 ctest，
# 看它是不是**等**到锁释放才开跑（而不是直接开跑、或直接失败）。
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sched = Join-Path $root 'scripts\sched.ps1'
$enc = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes('Start-Sleep -Seconds 12'))
$holder = Start-Process pwsh -NoNewWindow -PassThru -RedirectStandardOutput "$env:TEMP\h1.out" `
    -RedirectStandardError "$env:TEMP\h1.err" -ArgumentList @('-NoProfile', '-EncodedCommand', $enc)
Start-Sleep -Milliseconds 500
& $sched lock -Name 'build' -OwnerPid "$($holder.Id)" -Note 'ctest-queue-test'
Write-Host "持锁者 pid=$($holder.Id)，现在裸跑 ctest……"
$sw = [Diagnostics.Stopwatch]::StartNew()
Push-Location (Join-Path $root 'build')
$out = & ctest -R 'conformance_pp_endif_sameline' --output-on-failure 2>&1
Pop-Location
$sw.Stop()
$code = $LASTEXITCODE
Write-Host ("ctest 用时 {0:N1}s，exit={1}" -f $sw.Elapsed.TotalSeconds, $code)
$out | Select-Object -Last 6 | ForEach-Object { Write-Host "  $_" }
& $sched unlock -Name 'build' -OwnerPid "$($holder.Id)" | Out-Null
Stop-Process -Id $holder.Id -Force -ErrorAction SilentlyContinue
if ($code -eq 0 -and $sw.Elapsed.TotalSeconds -ge 8) {
    Write-Host 'CTEST_QUEUE_OK：裸 ctest 确实等到了锁' -ForegroundColor Green
    exit 0
}
Write-Host 'CTEST_QUEUE_FAIL：裸 ctest 没等锁（或跑挂了）' -ForegroundColor Red
exit 1
