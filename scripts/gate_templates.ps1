# gate-templates.ps1 - compile every shipped template from a substituted copy.
# 这是仓库里最贵的一类漏改的门禁：改了 stdlib/包的公共面，模板自此编不过。
#
# 配方来自模板自带的声明，不手搓：
#   template.manifest  name/type/target/entry/kind
#   zan.proj           name = {{NAME}} / entry = src/{{NAME}}.html
# 步骤 = IDE 建项目的等价动作：复制 -> App.* 改名 -> {{NAME}} 替换 -> 全源 + 入口一起编。
#
# 用 --emit-lib：跳过链接阶段（GUI 模板的 WinMain 宿主由真实建项目路径提供，
# 不属于"我把 API 改坏了"这一类）。它验证的是 类型/符号/codegen 这一层。
#
#   tools/agent/gate-templates.ps1                 # 全部
#   tools/agent/gate-templates.ps1 -Filter gui/*   # 子集
#   tools/agent/gate-templates.ps1 -DryRun         # 只列清单
[CmdletBinding()]
param(
    [string]$Filter,
    [switch]$DryRun,
    [switch]$Json,
    [switch]$KeepTree,
    [int]$TimeoutSec = 600
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
# 源仓库里编译器在 build/，发行包里在 toolchain/：两处都认；跨平台识别 exe 或 elf/mach-o
$exeExt = if ($IsWindows -or ($env:OS -like "*Windows*")) { '.exe' } else { '' }
$Zanc = Join-Path $Root ("build/zanc" + $exeExt)
if (-not (Test-Path $Zanc)) { $Zanc = Join-Path $Root ("toolchain/zanc" + $exeExt) }
if (-not (Test-Path $Zanc)) { Write-Error "zanc not found (build/ or toolchain/)"; exit 2 }
$TplRoot = Join-Path $Root 'templates'
$Work = Join-Path $Root '_scratch/gate'
$Name = 'GateProj'

if (-not (Test-Path $TplRoot)) { Write-Error "no templates dir at $TplRoot"; exit 2 }
New-Item -ItemType Directory -Force -Path $Work | Out-Null

function Read-Kv([string]$path) {
    $h = @{}
    if (-not (Test-Path $path)) { return $h }
    foreach ($line in [System.IO.File]::ReadLines($path)) {
        if ($line -match '^\s*#' -or $line -notmatch '=') { continue }
        $k = ($line -split '=', 2)[0].Trim()
        $v = ($line -split '=', 2)[1].Trim()
        $h[$k] = $v
    }
    return $h
}

function Invoke-Zanc([string[]]$argv, [string]$out, [int]$timeoutMs) {
    # .NET Process 直连：PS 5.1 下 Start-Process -PassThru 的 ExitCode 取不到，
    # 且必须显式 UTF-8 解码，否则中文诊断变乱码。
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Zanc
    $q = foreach ($a in $argv) { if ($a -match '\s') { '"' + ($a.Trim('"')) + '"' } else { $a } }
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
    if (-not $proc.WaitForExit($timeoutMs)) { try { $proc.Kill() } catch { } ; return [pscustomobject]@{ exit = -1; text = 'TIMEOUT' } }
    $proc.WaitForExit()
    return [pscustomobject]@{ exit = $proc.ExitCode; text = ($so.Result + $se.Result) }
}

# ---- discover ------------------------------------------------------------
$tpls = Get-ChildItem -Recurse -File -Filter 'template.manifest' $TplRoot | ForEach-Object {
    $dir = $_.Directory
    $rel = $dir.FullName.Substring($TplRoot.Length).TrimStart('\', '/') -replace '\\', '/'
    [pscustomobject]@{ dir = $dir.FullName; rel = $rel; manifest = (Read-Kv $_.FullName) }
}
if ($Filter) { $tpls = $tpls | Where-Object { $_.rel -like $Filter } }
if (-not $tpls) { Write-Output 'no templates matched'; exit 0 }

$rows = @()
$failed = 0

foreach ($t in $tpls) {
    $tag = $t.rel -replace '[/\\]', '_'
    $dst = Join-Path $Work $tag

    $entryRel = $t.manifest['entry']
    $proj = Read-Kv (Join-Path $t.dir 'zan.proj')
    if ($proj['entry']) { $entryRel = $proj['entry'] }

    if ($DryRun) {
        $rows += [pscustomobject]@{ template = $t.rel; entry = $entryRel; ok = $true; detail = 'dry-run' }
        continue
    }

    Remove-Item $dst -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item $t.dir $dst -Recurse

    # 改名判据：看模板自己引用什么，而不是套一条死规则。
    #   入口写 {{NAME}}.html        -> 整套 App.* 改名（gui-empty）
    #   源码引用 {{NAME}}Html.Build -> 只把 App.html 改名（gui-html-responsive）
    #   源码引用 AppHtml.Build      -> 不改名（.html 的文件名决定生成类名 <stem>Html）
    $renameAll = ($proj['entry'] -and $proj['entry'].Contains('{{NAME}}'))
    if ($renameAll) {
        Get-ChildItem -Recurse -File $dst | Where-Object { $_.BaseName -eq 'App' } | ForEach-Object {
            Rename-Item $_.FullName (Join-Path $_.DirectoryName ($Name + $_.Extension))
        }
    }
    # {{NAME}} 替换。必须显式 UTF-8 读 + 保留原 BOM：
    # PS 5.1 的 Get-Content -Raw 会把无 BOM 的模板按 ANSI 读，中文串变乱码，
    # 写回后表现为 "unterminated string literal"（整批假红）。
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    Get-ChildItem -Recurse -File $dst | ForEach-Object {
        $ext = $_.Extension.ToLower()
        if (@('.zan', '.html', '.htm', '.proj', '.manifest', '.json', '.css', '.js', '.md', '.txt', '.toml') -contains $ext) {
            $bytes = [System.IO.File]::ReadAllBytes($_.FullName)
            $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
            $src = [System.IO.File]::ReadAllText($_.FullName, [System.Text.Encoding]::UTF8)
            if ($src -and $src.Contains('{{NAME}}')) {
                $enc = if ($hasBom) { New-Object System.Text.UTF8Encoding($true) } else { $utf8NoBom }
                [System.IO.File]::WriteAllText($_.FullName, $src.Replace('{{NAME}}', $Name), $enc)
            }
        }
    }

    # .html 的文件名决定生成类名（<stem>Html）。替换后若源码引用了
    # "<NAME>Html"，而设计稿还叫 App.html，生成的是 AppHtml -> 必然 unresolved call。
    # 所以按"引用什么"补一步改名（gui-html-responsive 就是这种）。
    if (-not $renameAll) {
        $srcZan = Get-ChildItem -Recurse -File -Filter *.zan $dst -ErrorAction SilentlyContinue
        $needNameHtml = $false
        foreach ($z in $srcZan) {
            $t2 = [System.IO.File]::ReadAllText($z.FullName, [System.Text.Encoding]::UTF8)
            if ($t2 -and $t2.Contains($Name + 'Html')) { $needNameHtml = $true; break }
        }
        if ($needNameHtml) {
            Get-ChildItem -Recurse -File $dst -Include *.html, *.htm -ErrorAction SilentlyContinue |
                Where-Object { $_.BaseName -eq 'App' } |
                ForEach-Object { Rename-Item $_.FullName (Join-Path $_.DirectoryName ($Name + $_.Extension)) }
        }
    }

    # 输入：src 下全部 .zan（多文件必须逐一枚举）
    # （编译期 HTML 代码生成已在 f896826d3 废除，HTML 改由运行期通过 Gui.App.LoadHtml 等动态加载，zanc 只接收 .zan 源码）
    $srcDir = Join-Path $dst 'src'
    $scan = if (Test-Path $srcDir) { $srcDir } else { $dst }
    $inputs = @(Get-ChildItem -Recurse -File -Filter *.zan $scan | ForEach-Object { $_.FullName -replace '\\', '/' })
    $inputs = $inputs | Where-Object { $_ } | Sort-Object -Unique
    if ($entryRel) { $entryRel = $entryRel.Replace('{{NAME}}', $Name) }
    if ($inputs.Count -eq 0) {
        $rows += [pscustomobject]@{ template = $t.rel; entry = $entryRel; ok = $false; detail = 'no .zan sources found' }
        $failed++; continue
    }

    $out = Join-Path $Work ($tag + '.dll')
    # 不用 --deny-warnings：基线不干净时它会把 stdlib 既有告警升成错误，
    # 让每个模板都假红（实测 TaskCompletionSource.zan:101 的闭包捕获告警）。
    $argv = @($inputs) + @('--auto-stdlib', '--emit-lib', '-o', ("`"$out`""))
    $r = Invoke-Zanc -argv $argv -out $out -timeoutMs ($TimeoutSec * 1000)

    $bad = ($r.exit -ne 0) -or ($r.text -match '\b[1-9]\d*\s+error\(s\)') -or ($r.text -match '(?m)^\S.*?:\d+:\d+:\s*error:')
    $ok = -not $bad
    if (-not $ok) { $failed++ }
    $detail =
        if ($ok) { (($r.text -split "`n" | Where-Object { $_ -match '^Compiled' } | Select-Object -First 1)) }
        else {
            $errLines = @($r.text -split "`n" | Where-Object { $_ -match 'error' })
            if ($errLines.Count -gt 0) { ($errLines | Select-Object -First 3) -join ' | ' }
            else { ($r.text -split "`n" | Where-Object { $_.Trim() } | Select-Object -First 3) -join ' | ' }
        }
    if (-not $detail) { $detail = if ($ok) { 'ok' } else { 'failed' } }

    $rows += [pscustomobject]@{ template = $t.rel; entry = $entryRel; ok = $ok; detail = $detail.Trim() }
    if (-not $KeepTree) { Remove-Item $dst -Recurse -Force -ErrorAction SilentlyContinue }
}

if ($Json) {
    [pscustomobject]@{ summary = [pscustomobject]@{ total = $rows.Count; failed = $failed }; rows = $rows } | ConvertTo-Json -Depth 4
}
else {
    foreach ($r in $rows) {
        $mark = if ($r.ok) { 'PASS' } else { 'FAIL' }
        Write-Output ("[{0}] {1,-46} {2}" -f $mark, $r.template, (($r.detail -split "`n")[0]))
    }
    Write-Output ''
    Write-Output ("templates={0}  failed={1}" -f $rows.Count, $failed)
}

# ---- 素材与独立性检查 ----------------------------------------------------
# 编译过了不代表画面能看：源码引用的资源可能不存在（运行时静默变色块），
# 素材本身可能是纯色块，模板之间也可能互相引用（那就不是独立项目）。
$checker = Join-Path $Root 'scripts/check_game_templates.py'
if (Get-Command python -ErrorAction SilentlyContinue) {
    $checkOut = & python $checker 2>&1
    $checkExit = $LASTEXITCODE
    $checkOut | ForEach-Object { Write-Output $_ }
    if ($checkExit -ne 0) { $failed++ }
}
else {
    Write-Output 'NOTE: python not found - skipped game template art checks'
}

if ($failed -gt 0) { exit 1 } else { exit 0 }
