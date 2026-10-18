# verify.ps1 - zan-lang 的判定器级联。
# 改动路径 -> 必跑检查 + 该跑哪组 ctest（规则表在 verify.rules.json）。
#
#   tools/agent/verify.ps1 -Paths stdlib/System/IO/File.zan
#   tools/agent/verify.ps1 -Paths src/compiler/checker.c -DryRun
#   tools/agent/verify.ps1 -All -DryRun
#   tools/agent/verify.ps1 -Paths packages/... -Json
#   tools/agent/verify.ps1 -Paths tests/conformance/x.zan -RunTests   # 真跑 target 化测试
#
# 设计约束：
#   * 只跑 A 档能判的（fmt / compile）。C/C++ 改动给重建命令，不假装能单文件判。
#   * ctest 只给"已验证命中"的正则 + 命令，绝不自动跑 standard/full（发布门）。
#   * 退出码：0 全过；1 有 A 档失败；2 环境/参数问题。
[CmdletBinding()]
param(
    [string[]]$Paths,
    [switch]$All,
    [switch]$DryRun,
    [switch]$Json,
    [string[]]$Only,
    [switch]$RunTests,
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$RulesPath = Join-Path $PSScriptRoot 'verify.rules.json'
if (-not (Test-Path $RulesPath)) { Write-Error "missing $RulesPath"; exit 2 }
$Rules = [System.IO.File]::ReadAllText($RulesPath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json

function Resolve-Tool([string]$rel, [string]$fallback) {
    $p = Join-Path $Root $rel
    if (Test-Path $p) { return $p }
    $f = Join-Path $Root $fallback
    if (Test-Path $f) { return $f }
    return $null
}
$Zanc = Resolve-Tool $Rules.tools.zanc 'toolchain\zanc.exe'
$Zanfmt = Resolve-Tool $Rules.tools.zanfmt 'toolchain\zanfmt.exe'
$TestEntry = Join-Path $Root $Rules.tools.testEntry

function GlobToRegex([string]$g) {
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append('^')
    $i = 0
    while ($i -lt $g.Length) {
        $c = $g[$i]
        if ($c -eq [char]'*') {
            if (($i + 1) -lt $g.Length -and $g[$i + 1] -eq [char]'*') {
                if (($i + 2) -lt $g.Length -and $g[$i + 2] -eq [char]'/') { [void]$sb.Append('(?:.*/)?'); $i += 3; continue }
                [void]$sb.Append('.*'); $i += 2; continue
            }
            [void]$sb.Append('[^/]*'); $i++; continue
        }
        elseif ($c -eq [char]'?') { [void]$sb.Append('[^/]'); $i++; continue }
        else { [void]$sb.Append([regex]::Escape([string]$c)); $i++; continue }
    }
    [void]$sb.Append('$')
    return $sb.ToString()
}

function Rel([string]$full) { $full.Substring($Root.Length).TrimStart('\', '/') -replace '\\', '/' }

function Invoke-Tool {
    param([string]$Exe, [string[]]$Argv, [int]$TimeoutMs)
    # .NET Process 直连：PS 5.1 下 Start-Process -PassThru 的 ExitCode 取不到，
    # 而判定器靠退出码，取不到就等于全红。显式 UTF-8 解码，否则中文诊断变乱码。
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $q = foreach ($a in $Argv) { if ($a -match '\s') { '"' + ($a.Trim('"')) + '"' } else { $a } }
    $psi.Arguments = ($q -join ' ')
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()
    $so = $proc.StandardOutput.ReadToEndAsync()
    $se = $proc.StandardError.ReadToEndAsync()
    if (-not $proc.WaitForExit($TimeoutMs)) { try { $proc.Kill() } catch { } ; return [pscustomobject]@{ exit = -1; text = 'TIMEOUT' } }
    $proc.WaitForExit()
    return [pscustomobject]@{ exit = $proc.ExitCode; text = ($so.Result + $se.Result) }
}

# ---- ctest 探测：只用 -N（列清单），绝不真跑 ---------------------------------
$BuildDir = Join-Path $Root 'build'
function Get-CtestProbe([string]$re) {
    if (-not (Test-Path (Join-Path $BuildDir 'CTestTestfile.cmake'))) { return $null }
    $r = Invoke-Tool -Exe 'ctest' -Argv @('--test-dir', $BuildDir, '-C', 'Release', '-N', '-R', $re) -TimeoutMs 120000
    $names = @([regex]::Matches($r.text, 'Test\s+#\d+:\s*(\S+)') | ForEach-Object { $_.Groups[1].Value })
    return [pscustomobject]@{ regex = $re; count = $names.Count; sample = @($names | Select-Object -First 4) }
}

# tests/<dir>/... 的正则靠探测确定，而不是我猜前缀
function Resolve-CtestFromPath([string]$rel) {
    $seg = ($rel -split '/')[1]
    if (-not $seg) { return $null }
    $cands = @("^$seg`_", "^conformance_$seg`_", [regex]::Escape($seg), "^$seg")
    foreach ($c in $cands) {
        $p = Get-CtestProbe $c
        if ($p -and $p.count -gt 0) { return $p }
    }
    return [pscustomobject]@{ regex = $cands[0]; count = 0; sample = @() }
}

# ---- targets -------------------------------------------------------------
$targets = @()
if ($All) {
    $skip = $Rules.skipDirs
    $targets = Get-ChildItem -Recurse -File -Include *.zan,*.c,*.h,*.cpp,*.m $Root -ErrorAction SilentlyContinue |
        Where-Object { $skip -notcontains ((Rel $_.FullName) -split '/')[0] } |
        ForEach-Object { Rel $_.FullName }
}
elseif ($Paths) {
    foreach ($p in $Paths) {
        $rel = if ([System.IO.Path]::IsPathRooted($p)) { Rel $p } else { $p -replace '\\', '/' }
        if (-not (Test-Path (Join-Path $Root $rel))) { Write-Warning "not found: $rel"; continue }
        $targets += $rel
    }
}
else {
    if (-not (Test-Path (Join-Path $Root '.git'))) { Write-Error "no -Paths/-All and not a git repo"; exit 2 }
    $targets = & git -C $Root diff --name-only HEAD 2>$null
    if (-not $targets) { Write-Output 'nothing changed'; exit 0 }
}

$targets = @($targets | Sort-Object -Unique | Where-Object { $_ })
if ($targets.Count -eq 0) { Write-Output 'no targets'; exit 0 }

# ---- rule matching -------------------------------------------------------
$ruleRe = @()
foreach ($r in $Rules.rules) { $ruleRe += [pscustomobject]@{ re = (GlobToRegex $r.glob); checks = $r.checks; rule = $r } }
function Rule-For([string]$rel) {
    foreach ($r in $ruleRe) { if ($rel -match $r.re) { return $r } }
    return $null
}

# ---- run -----------------------------------------------------------------
$Work = Join-Path $Root '_scratch\verify'
if (-not $DryRun) { New-Item -ItemType Directory -Force -Path $Work | Out-Null }

$report = @()
$fails = 0
$escalations = @()
$ctestPlans = @{}

foreach ($rel in $targets) {
    $ruleHit = Rule-For $rel
    if (-not $ruleHit) { continue }
    $checks = @($ruleHit.checks)
    if ($Only) { $checks = @($checks | Where-Object { $Only -contains $_ }) }
    $abs = Join-Path $Root ($rel -replace '/', '\')
    $results = @()

    foreach ($ck in $checks) {
        switch ($ck) {

            'fmt' {
                # 不用 zanfmt --check：它在 Windows 检出（core.autocrlf 把 .zan 变 CRLF，
                # 而 .gitattributes 规定提交 LF）上拿 CRLF 原文与自身 LF 输出逐字节比，
                # 于是全仓 100% 假红（实测 8356/8356）。这里自己做裁决：
                # 归一化行尾后比较 zanfmt --stdout 的结果，只差换行不算不格式。
                if ($DryRun) { $results += [pscustomobject]@{ check = 'fmt'; ok = $true; detail = 'will compare zanfmt --stdout against file (EOL-normalized)' }; break }
                if (-not $Zanfmt) { $results += [pscustomobject]@{ check = 'fmt'; ok = $false; detail = 'zanfmt not found (build/ or toolchain/)' }; break }
                $r = Invoke-Tool -Exe $Zanfmt -Argv @('--stdout', ($abs -replace '\\', '/')) -TimeoutMs 120000
                if ($r.exit -ne 0) {
                    $results += [pscustomobject]@{ check = 'fmt'; ok = $false; detail = (($r.text -split "`n")[0]) }
                    break
                }
                $orig = [System.IO.File]::ReadAllText($abs, [System.Text.Encoding]::UTF8)
                $a = ($orig -replace "`r`n", "`n").TrimEnd()
                $b = ($r.text -replace "`r`n", "`n").TrimEnd()
                $ok = ($a -eq $b)
                $detail = 'ok'
                if (-not $ok) {
                    $la = $a -split "`n"; $lb = $b -split "`n"
                    $n = [Math]::Min($la.Count, $lb.Count)
                    $detail = 'needs formatting'
                    for ($i = 0; $i -lt $n; $i++) {
                        if ($la[$i] -ne $lb[$i]) {
                            $detail = ("needs formatting @L{0}: -{1} +{2}" -f ($i + 1), $la[$i].Trim(), $lb[$i].Trim())
                            break
                        }
                    }
                }
                $results += [pscustomobject]@{ check = 'fmt'; ok = $ok; detail = $detail }
            }

            'compile' {
                # 编译单元不是"这一个文件"：zanc 不自动拉取同项目兄弟 .zan，
                # 多文件项目单入口编译必报 undefined type。先按单文件编，失败再按
                # 所属模块整体编一次——不靠猜布局，靠重试定真相。
                # 也不用 --deny-warnings：基线不干净时它会把 stdlib 既有告警升成
                # 错误，与本次改动无关（实测 Zan.Net 的 TaskCompletionSource 告警）。
                $units = @()
                $units += , @($rel)
                $seg = $rel -split '/'
                $modRoot = $null
                if ($seg.Count -ge 3 -and ($seg[0] -eq 'packages' -or $seg[0] -eq 'tools')) {
                    $modRoot = ($seg[0..1] -join '/')
                }
                elseif ($seg.Count -ge 2) { $modRoot = ($seg[0..($seg.Count - 2)] -join '/') }
                if ($modRoot) {
                    $modAbs = Join-Path $Root ($modRoot -replace '/', '\')
                    if (Test-Path $modAbs) {
                        $mods = @(Get-ChildItem -Recurse -File -Filter *.zan $modAbs | ForEach-Object { Rel $_.FullName })
                        if ($mods.Count -gt 1) { $units += , $mods }
                    }
                }
                if ($DryRun) {
                    $mtxt = '0'
                    if ($units.Count -gt 1) { $mtxt = [string]$units[1].Count }
                    $results += [pscustomobject]@{ check = 'compile'; ok = $true; detail = ("will compile single file, then module ({0} files) on failure" -f $mtxt) }
                    break
                }
                if (-not $Zanc) { $results += [pscustomobject]@{ check = 'compile'; ok = $false; detail = 'zanc not found (build/ or toolchain/)' }; break }

                $ok = $false
                $detail = 'failed'
                $usedUnit = 0
                foreach ($unit in $units) {
                    $usedUnit++
                    $absUnit = @($unit | ForEach-Object { (Join-Path $Root ($_ -replace '/', '\')) -replace '\\', '/' })
                    $isEntry = $false
                    foreach ($u in $absUnit) {
                        try {
                            if (Select-String -Path ($u -replace '/', '\') -Pattern 'static\s+(?:async\s+)?[A-Za-z_]\w*\s+Main\s*\(' -Quiet -ErrorAction Stop) { $isEntry = $true; break }
                        } catch { }
                    }
                    $base = if ($unit.Count -eq 1) { [System.IO.Path]::GetFileNameWithoutExtension($absUnit[0]) } else { Split-Path -Leaf $modRoot }
                    $out = if ($isEntry) { Join-Path $Work "$base.exe" } else { Join-Path $Work "$base.dll" }
                    $argv = @($absUnit) + @('--auto-stdlib')
                    if (-not $isEntry) { $argv += @('--emit-lib') }
                    $argv += @('-o', $out)
                    $r = Invoke-Tool -Exe $Zanc -Argv $argv -TimeoutMs ($TimeoutSec * 1000)
                    $bad = ($r.exit -ne 0) -or ($r.text -match '\b[1-9]\d*\s+error\(s\)') -or ($r.text -match '(?m)^\S.*?:\d+:\d+:\s*error:')
                    $ok = -not $bad
                    $warnTxt = ''
                    $wm = [regex]::Match($r.text, '(\d+)\s+warning\(s\)')
                    if ($wm.Success -and [int]$wm.Groups[1].Value -gt 0) { $warnTxt = (" [既有告警 {0} 条，仅提示]" -f $wm.Groups[1].Value) }
                    if ($ok) {
                        $c = (($r.text -split "`n" | Where-Object { $_ -match '^Compiled' } | Select-Object -First 1))
                        $mnote = ''
                        if ($usedUnit -gt 1) { $mnote = (" (模块整体编译 {0} 文件)" -f $unit.Count) }
                        $detail = ("{0}{1}{2}" -f $c, $warnTxt, $mnote)
                        break
                    }
                    $detail = (((($r.text -split "`n" | Where-Object { $_ -match 'error' } | Select-Object -First 2) -join ' | ')) + $warnTxt)
                }
                if (-not $detail) { $detail = if ($ok) { 'ok' } else { 'failed' } }
                $results += [pscustomobject]@{ check = 'compile'; ok = $ok; detail = $detail.Trim() }
            }

            'rebuild' {
                $cmd = 'cmake --build build --target zanc'
                $results += [pscustomobject]@{ check = 'rebuild'; ok = $true; detail = "C/C++ 源改动：$cmd（单文件编译无判定意义）" }
                $escalations += [pscustomobject]@{ when = $rel; do = 'rebuild target then rerun tests'; command = $cmd }
            }

            'impact' {
                if ($DryRun) { $results += [pscustomobject]@{ check = 'impact'; ok = $true; detail = 'will run impact.ps1 -Path' }; break }
                $ps = Join-Path $PSScriptRoot 'impact.ps1'
                $r = Invoke-Tool -Exe 'powershell' -Argv @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $ps, '-Path', $abs, '-Quiet', '-Limit', '10') -TimeoutMs 600000
                $cross = ($r.exit -eq 1)
                $results += [pscustomobject]@{ check = 'impact'; ok = $true; detail = $(if ($cross) { 'CROSS-BOUNDARY consumers exist（见 impact.ps1 输出）' } else { 'contained' }) }
                if ($cross) { $escalations += [pscustomobject]@{ when = $rel; do = '公共面跨边界：跑模板验收门'; command = 'tools/agent/gate-templates.ps1' } }
            }

            'ctest' {
                $probe = $null
                if ($ruleHit.rule.ctestFromPath) { $probe = Resolve-CtestFromPath $rel }
                elseif ($ruleHit.rule.ctest) { $probe = Get-CtestProbe $ruleHit.rule.ctest }
                if (-not $probe) {
                    $results += [pscustomobject]@{ check = 'ctest'; ok = $true; detail = 'build/ 未配置 ctest，跳过测试建议' }
                    break
                }
                if ($probe.count -eq 0) {
                    $results += [pscustomobject]@{ check = 'ctest'; ok = $false; detail = ("映射失效：正则 '$($probe.regex)' 命中 0 条测试（测试改名了？）") }
                    break
                }
                $cmd = "scripts/test.ps1 smoke -Match `"$($probe.regex)`""
                $results += [pscustomobject]@{ check = 'ctest'; ok = $true; detail = ("{0} 条命中 -> {1}" -f $probe.count, $cmd) }
                $ctestPlans[$rel] = $probe
                if ($RunTests -and -not $DryRun) {
                    $rr = Invoke-Tool -Exe 'powershell' -Argv @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $TestEntry, 'smoke', '-Match', $probe.regex) -TimeoutMs 3600000
                    $tOk = ($rr.exit -eq 0)
                    $results += [pscustomobject]@{ check = 'ctest-run'; ok = $tOk; detail = $(if ($tOk) { 'TEST_OK' } else { (($rr.text -split "`n" | Where-Object { $_ -match 'Fail|FAIL|TEST_FAIL' } | Select-Object -First 2) -join ' | ') }) }
                }
            }

            'templates' {
                $results += [pscustomobject]@{ check = 'templates'; ok = $true; detail = 'Windows: tools/agent/gate-templates.ps1 | macOS: scripts/test_all_templates.py' }
            }
        }
    }

    $bad = @($results | Where-Object { -not $_.ok })
    if ($bad.Count -gt 0) { $fails++ }
    $report += [pscustomobject]@{ path = $rel; checks = $checks; results = $results; ok = ($bad.Count -eq 0) }
}

$policy = $Rules.policy
$summary = [pscustomobject]@{
    root         = $Root
    mode         = $(if ($All) { 'all' } elseif ($Paths) { 'paths' } else { 'git' })
    dryRun       = [bool]$DryRun
    targets      = $report.Count
    failed       = $fails
    policy       = $policy.routine
    escalationHint = @($escalations | Sort-Object command -Unique)
}

if ($Json) {
    [pscustomobject]@{ summary = $summary; report = $report } | ConvertTo-Json -Depth 6
}
else {
    foreach ($t in $report) {
        $mark = if ($t.ok) { 'PASS' } else { 'FAIL' }
        Write-Output ("[{0}] {1}" -f $mark, $t.path)
        foreach ($r in $t.results) {
            $m = if ($r.ok) { 'ok  ' } else { 'ERR ' }
            Write-Output ("       {0} {1,-12} {2}" -f $m, $r.check, (($r.detail -split "`n")[0]))
        }
    }
    Write-Output ''
    $dryMark = ''
    if ($DryRun) { $dryMark = '  (dry-run)' }
    Write-Output ("targets={0}  failed={1}  mode={2}{3}" -f $summary.targets, $summary.failed, $summary.mode, $dryMark)
    Write-Output ("policy: " + $summary.policy)
    foreach ($e in $summary.escalationHint) { Write-Output ("ESCALATE: {0}" -f $e.command) }
}

if (-not $DryRun) { Remove-Item $Work -Recurse -Force -ErrorAction SilentlyContinue }
if ($fails -gt 0) { exit 1 } else { exit 0 }
