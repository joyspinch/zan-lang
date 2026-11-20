# Sweep build/: the test artifacts that pile up there.
#
# Why this exists: every conformance / arc / leakcheck case compiles its own
# executable and every ctest run leaves a LastTest.log.tmpXXXXXX behind. After
# a few hundred runs build\ held 20094 files / 9.9 GB -- of which only a
# handful are real products. The cases are re-runnable: the golden lives in
# tests\, the executable is a disposable cache entry.
#
#   powershell -File scripts\clean_build.ps1                    # dry-run
#   powershell -File scripts\clean_build.ps1 -Apply
#   powershell -File scripts\clean_build.ps1 -Apply -Screenshots  # also the PNGs
#
# Five lines of junk, five different rules:
#
#   1. build\cases\      the case cache. Reused between runs (run_case.cmake
#                         skips the compile when the exe is newer than the
#                         source / compiler / stdlib stamp), so it is swept by
#                         RECENCY, not deleted outright: keep what was touched
#                         recently, keep the total under a size budget, drop
#                         the rest. The warm working set survives; the stale
#                         tail does not.
#   2. build\ root       legacy case executables from before the relocation to
#                         build\cases\. Nothing new lands here any more, so
#                         these are pure leftovers -- but the root also holds
#                         the real products and the toolchain, so it is swept
#                         against an explicit keep list, never by pattern.
#   3. ctest leftovers   one LastTest.log.tmpXXXXXX per run, never cleaned by
#                         ctest itself.
#   4. build\test-out\   one timestamped directory per acceptance / publish
#                         verification run, each holding a full publish tree
#                         (ZanIDE.exe alone is ~90MB). Nothing rotated them:
#                         748 directories / 14GB by 2026-11. Swept by run age
#                         (TestOutKeepDays), whole directory at a time.
#   5. experiment dirs   selfhost_illir* / selfhost_sweep* (selfhost emitter IR
#                         audits and builtin sweeps), selfhost_repo (the
#                         audit_selfhost_ir_golden.py IR dump) and ab (a full
#                         reference-compiler build tree per ab_compiler.ps1
#                         run) are regenerable one-off output. 24GB in 28
#                         directories by 2026-11. Swept by experiment age.
#                         build\cef-headers\ is a 1.9GB download cache and is
#                         deliberately NOT swept.
#
# Rules:
# - Real products, the toolchain and every non-executable (runtime .a / .lib /
#   .o, the bundled compiler) are NEVER touched.
# - A file that is in use cannot be replaced on Windows, so deleting a running
#   executable simply fails and is skipped -- an active session is never
#   interrupted.
# - Dry-run is the default; -Apply is what deletes. The dry-run output IS
#   the review, so deleting needs no further confirmation.

param(
    [switch]$Apply,
    [switch]$Screenshots,
    [int]$KeepDays = 7,        # 用例缓存的保质期：超过就当冷缓存丢掉
    [int]$MaxCacheMB = 512,     # 用例缓存的体积上限：超了就从最旧的开始扔
    [int]$TestOutKeepDays = 2,  # build\test-out\ 的每个运行目录保留几天
    [int]$ExperimentKeepDays = 7, # selfhost_* / ab 一次性实验目录保留几天
    [string]$Build = (Join-Path $PSScriptRoot "..\build")
)

$ErrorActionPreference = 'Continue'

$buildFull = [IO.Path]::GetFullPath($Build)
if (-not (Test-Path -LiteralPath $buildFull)) {
    Write-Host "build/ not found: $buildFull"
    exit 0
}

# build\ 根里必须留下的东西。分类写清楚，改的人能一眼看出为什么。
$keepExe = @(
    # 真产物（编译器 / 语言服务 / 调试适配器 / IDE）
    'zanc.exe', 'zanc-selfhost.exe', 'zan-mvc.exe', 'zan-lsp.exe', 'zan-dap.exe', 'zan-pm.exe', 'ZanIDE.exe',
    # 工具链：publish_ide.ps1 / stage_dev_toolchain.ps1 要往 dist 里拷
    'ld.lld.exe', 'ld.exe', 'llvm-nm.exe',
    # 脚本产出的工具（build_repomap / gen_knowledge / build_mge / ...）
    'repomap.exe', 'zan-lsp-client.exe', 'zan-mcp.exe', 'zandoc.exe',
    'zanfmt.exe', 'genknowledge.exe', 'mge.exe', 'tool_genindex.exe',
    # DAP 集成测试的固定目标程序（CMake 自定义命令生成，删了测试就跑不了）
    'dbgtarget.exe', 'dbgtarget_burn.exe',
    # 图表演示程序（build_gallery.ps1 / build_charts.ps1 产出，正在跑的时候别删）
    'gallery_test.exe', 'charts_test.exe',
    # 自举测试/门禁目标程序（run_selfhost_diff / run_fixedpoint 运行期间不可删）
    'selfhost_diff_gen1.exe', 'selfhost_gen1.exe', 'gen1.exe', 'gen2.exe'
)

$plan = [ordered]@{}

# 目录的体积和年龄都不能直接读：DirectoryInfo.Length 不存在，目录 mtime 也不
# 会因子目录写入而更新（NTFS 只更新直接子项所在目录）。所以按树内最新的文件
# 时间算年龄——活跃会话正在写的目录不会被误判成旧货。
function Get-TreeBytes([System.IO.FileSystemInfo]$item) {
    if ($item -is [System.IO.FileInfo]) { return $item.Length }
    $sum = (Get-ChildItem -LiteralPath $item.FullName -Recurse -File -Force -ErrorAction SilentlyContinue |
            Measure-Object Length -Sum).Sum
    if ($null -eq $sum) { 0 } else { [int64]$sum }
}

function Get-NewestWriteTime([System.IO.FileSystemInfo]$item) {
    if ($item -is [System.IO.FileInfo]) { return $item.LastWriteTime }
    # 只用树内文件的时间：目录自身 mtime 会被子项创建刷新，拿它判定会把
    # 刚被别人 Set-Content 碰过的旧目录当天货（探针实测过一次）。
    $newest = $null
    foreach ($f in (Get-ChildItem -LiteralPath $item.FullName -Recurse -File -Force -ErrorAction SilentlyContinue)) {
        if ($null -eq $newest -or $f.LastWriteTime -gt $newest) { $newest = $f.LastWriteTime }
    }
    if ($null -eq $newest) { return $item.LastWriteTime }   # 空目录只能退回目录时间
    return $newest
}

# ---- 1. build\cases\ 的用例缓存：按最近使用清扫，不是全删 ----------------
$casesDir = Join-Path $buildFull 'cases'
if (Test-Path -LiteralPath $casesDir) {
    $cache = @(Get-ChildItem -LiteralPath $casesDir -File -ErrorAction SilentlyContinue |
               Sort-Object LastWriteTime -Descending)
    if ($cache.Count) {
        $cutoff = (Get-Date).AddDays(-$KeepDays)
        $keep = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
        $drop = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
        $budget = [int64]$MaxCacheMB * 1MB
        $used = [int64]0
        foreach ($f in $cache) {
            if ($f.LastWriteTime -lt $cutoff) { $drop.Add($f); continue }
            if (($used + $f.Length) -gt $budget) { $drop.Add($f); continue }
            $used += $f.Length
            $keep.Add($f)
        }
        if ($drop.Count) {
            $plan["用例缓存（build\cases\ 冷掉的 + 超体积上限的）"] = @($drop.ToArray())
        }
        Write-Host ("[cache] build\cases\ 保留 {0} 个 / {1:N1} MB，清掉 {2} 个" -f `
                    $keep.Count, ($used / 1MB), $drop.Count)
    }
}

# ---- 2. build\ 根的历史遗留用例 exe --------------------------------------
# 用例产物已经全部收进 build\cases\，这里剩下的 exe 只有两种：真产物（保留
# 清单里）和历史遗留的一次性用例产物（清掉）。CMake 自己 link 的目标
# （tests\ 里的 C 测试程序）有同名 .dir 中间态，删掉只会让下次构建重链，
# 没必要动它。
$rootExe = @(Get-ChildItem -LiteralPath $buildFull -File -Filter '*.exe' -ErrorAction SilentlyContinue |
             Where-Object {
                 $keepExe -notcontains $_.Name -and
                 -not (Test-Path -LiteralPath (Join-Path $buildFull "CMakeFiles\$($_.BaseName).dir"))
             })
if ($rootExe.Count) { $plan['build\ 根的历史遗留用例可执行文件'] = $rootExe }

# ---- 3. ctest 的临时日志残留 ---------------------------------------------
$tmpLogs = @(Get-ChildItem -LiteralPath (Join-Path $buildFull 'Testing\Temporary') -File -Filter 'LastTest.log.tmp*' -ErrorAction SilentlyContinue)
if ($tmpLogs.Count) { $plan['ctest 残留的临时日志'] = $tmpLogs }

# ---- 4. 自举对拍的中间产物：.exe 是一次性的，.ll 是排查"两边为什么不一样"
#         的线索，失败用例的 .ll 由 scripts\test.ps1 保留。
$diffDirs = @(Get-ChildItem -LiteralPath $buildFull -Directory -Filter 'selfhost_diff*' -ErrorAction SilentlyContinue)
$diffExe = @($diffDirs | ForEach-Object {
    Get-ChildItem -LiteralPath $_.FullName -File -Filter '*.exe' -ErrorAction SilentlyContinue
})
if ($diffExe.Count) { $plan['自举对拍的一次性可执行文件'] = $diffExe }

# ---- 5. build\test-out\：每次验收/发布验证一个带时间戳的目录 ---------------
# 目录里是完整 publish 产物（ZanIDE.exe 单个 ~90MB），2026-11 已累积到 748 个
# 目录 / 14GB。按运行时间轮转整目录，保留最近 TestOutKeepDays 天（当前会话的
# 证据一定在内），更早的运行产物都是历史记录，要复查就跑一遍。
$testOutDir = Join-Path $buildFull 'test-out'
if (Test-Path -LiteralPath $testOutDir) {
    $testOutCutoff = (Get-Date).AddDays(-$TestOutKeepDays)
    $staleRuns = @(Get-ChildItem -LiteralPath $testOutDir -Directory -ErrorAction SilentlyContinue |
                   Where-Object { (Get-NewestWriteTime $_) -lt $testOutCutoff })
    if ($staleRuns.Count) { $plan["验收产物（build\test-out\ 超过 $TestOutKeepDays 天的运行）"] = $staleRuns }
    Write-Host ("[test-out] 保留 {0} 个运行目录，清掉 {1} 个" -f `
                ((Get-ChildItem -LiteralPath $testOutDir -Directory -ErrorAction SilentlyContinue).Count - $staleRuns.Count),
                $staleRuns.Count)
}

# ---- 6. 一次性实验目录：自举 IR / 内置函数矩阵 / 参照编译器 A/B -------------
# 这些目录都能重跑重建，且一次批量实验就是 1-2GB；2026-11 累积 24GB / 28 个目录。
# cef-headers 是下载缓存，不在名单里。
$expPatterns = @('selfhost_illir*', 'selfhost_sweep*', 'selfhost_repo', 'ab')
$expCutoff = (Get-Date).AddDays(-$ExperimentKeepDays)
$expDirs = @($expPatterns | ForEach-Object {
    Get-ChildItem -LiteralPath $buildFull -Directory -Filter $_ -ErrorAction SilentlyContinue
} | Where-Object { (Get-NewestWriteTime $_) -lt $expCutoff })
if ($expDirs.Count) { $plan["一次性实验目录（超过 $ExperimentKeepDays 天的）"] = @($expDirs) }

# ---- 7. 截图：每帧一张 PNG，攒起来能到几万个 -------------------------------
if ($Screenshots) {
    $pngs = @(Get-ChildItem -LiteralPath $buildFull -Recurse -File -Filter '*.png' -ErrorAction SilentlyContinue)
    if ($pngs.Count) { $plan['截图（PNG）'] = $pngs }
}

if ($plan.Count -eq 0) {
    Write-Host "build/ is clean -- nothing to sweep."
    exit 0
}

$totalItems = 0
$totalMB = 0.0
Write-Host ("build/ sweep target: {0}" -f $buildFull)
foreach ($k in $plan.Keys) {
    $items = $plan[$k]
    $mb = 0.0
    foreach ($it in $items) { $mb += (Get-TreeBytes $it) / 1MB }
    $totalItems += $items.Count
    $totalMB += $mb
    Write-Host ("  {0,-42} {1,6} 个  {2,8:N1} MB" -f $k, $items.Count, $mb)
}
Write-Host ("  {0,-42} {1,6} 个  {2,8:N1} MB" -f '合计', $totalItems, $totalMB)

if (-not $Apply) {
    Write-Host ""
    Write-Host "dry-run. add -Apply to delete."
    exit 0
}

$deleted = 0
$skipped = 0
foreach ($k in $plan.Keys) {
    foreach ($f in $plan[$k]) {
        try {
            if ($f -is [System.IO.DirectoryInfo]) {
                Remove-Item -LiteralPath $f.FullName -Recurse -Force -ErrorAction Stop
            } else {
                Remove-Item -LiteralPath $f.FullName -Force -ErrorAction Stop
            }
            $deleted++
        } catch {
            # in use by a running process, or locked by another session
            $skipped++
        }
    }
}

Write-Host ""
Write-Host ("deleted {0} entry(ies), skipped {1} (in use)." -f $deleted, $skipped)
exit 0
