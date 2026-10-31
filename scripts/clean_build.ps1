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
# Three lines of junk, three different rules:
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
    'zanc.exe', 'zan-lsp.exe', 'zan-dap.exe', 'zan-pm.exe', 'ZanIDE.exe',
    # 工具链：publish_ide.ps1 / stage_dev_toolchain.ps1 要往 dist 里拷
    'ld.lld.exe', 'ld.exe',
    # 脚本产出的工具（build_repomap / gen_knowledge / build_mge / ...）
    'repomap.exe', 'zan-lsp-client.exe', 'zan-mcp.exe', 'zandoc.exe',
    'zanfmt.exe', 'genknowledge.exe', 'mge.exe', 'tool_genindex.exe',
    # DAP 集成测试的固定目标程序（CMake 自定义命令生成，删了测试就跑不了）
    'dbgtarget.exe', 'dbgtarget_burn.exe',
    # 图表演示程序（build_gallery.ps1 / build_charts.ps1 产出，正在跑的时候别删）
    'gallery_test.exe', 'charts_test.exe'
)

$plan = [ordered]@{}

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

# ---- 5. 截图：每帧一张 PNG，攒起来能到几万个 -------------------------------
if ($Screenshots) {
    $pngs = @(Get-ChildItem -LiteralPath $buildFull -Recurse -File -Filter '*.png' -ErrorAction SilentlyContinue)
    if ($pngs.Count) { $plan['截图（PNG）'] = $pngs }
}

if ($plan.Count -eq 0) {
    Write-Host "build/ is clean -- nothing to sweep."
    exit 0
}

$totalFiles = 0
$totalMB = 0.0
Write-Host ("build/ sweep target: {0}" -f $buildFull)
foreach ($k in $plan.Keys) {
    $files = $plan[$k]
    $mb = ($files | Measure-Object Length -Sum).Sum / 1MB
    $totalFiles += $files.Count
    $totalMB += $mb
    Write-Host ("  {0,-42} {1,6} 个  {2,8:N1} MB" -f $k, $files.Count, $mb)
}
Write-Host ("  {0,-42} {1,6} 个  {2,8:N1} MB" -f '合计', $totalFiles, $totalMB)

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
            Remove-Item -LiteralPath $f.FullName -Force -ErrorAction Stop
            $deleted++
        } catch {
            # in use by a running process, or locked by another session
            $skipped++
        }
    }
}

Write-Host ""
Write-Host ("deleted {0} file(s), skipped {1} (in use)." -f $deleted, $skipped)
exit 0
