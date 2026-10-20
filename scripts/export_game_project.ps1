<#
.SYNOPSIS
    把仓库里的游戏模板导出成仓库外的独立项目。

.DESCRIPTION
    大型游戏（几十 MB 素材、上千文件、自带工具链）不该占着语言仓库：
    仓库负责 SDK、公共包与示例级模板；具体游戏在仓库外自己演进。

    本脚本做五件事：
      1. 复制模板目录到目标路径，按端分目录：
         client/   客户端（源码、资源、数据表、客户端测试）
         server/   服务端（同一游戏的其它目录，如 templates/server/server-legend）
         tests/    跨端用例（同时读客户端与服务端）
      2. 改写文件里对仓库路径的引用（"templates/game/wuwei/data" → "client/data"，
         Python 的 parents[3] → parents[2] 等）；
      3. 模板身份退役：删 template.manifest、zan.proj 的 {{NAME}} 落成实名、
         修正指不到文件的 entry；
      4. 补一份 PROJECT.md（怎么编译、怎么跑测试、怎么同步公共包）；
      5. 可选 git init + 首次提交，让独立项目有自己的历史。

    路径重写的口径：**一切相对项目根**。项目根是所有命令的工作目录，
    所以 "templates/game/legend/data" → "client/data"（不是 "data"）。
    改写规则一律用**单行**替换：多行块的换行符（CRLF/LF）在不同 checkout 下
    会对不上，单行替换跨平台稳。规则带 Scope 时只作用于该目录子树。

    公共包依赖：导出后的项目在仓库外，编译时包从三处解析——
    <项目根>/packages/、<项目根>/.zan-packages/、全局 %LOCALAPPDATA%\Zan\packages。
    需要某个包时把 zan-lang 的 packages/<Name> 复制进 <项目根>/packages/ 即可，
    或用 --package-project 指向 zan-lang 仓库。

.EXAMPLE
    pwsh -File scripts/export_game_project.ps1 -Template game/wuwei -Target D:/project/games/wuwei

.EXAMPLE
    pwsh -File scripts/export_game_project.ps1 -Template game/legend -Target D:/project/games/legend `
        -SiblingPaths templates/server/server-legend -SiblingAlias server `
        -TopTests tests/templates/legend/test_rules_sync.py
#>
[CmdletBinding()]
param(
    # 仓库内的模板目录（相对 templates/），如 game/wuwei、game/legend
    [Parameter(Mandatory = $true)]
    [string]$Template,

    # 导出目标（仓库外），如 D:/project/games/wuwei
    [Parameter(Mandatory = $true)]
    [string]$Target,

    # 配套测试目录（相对仓库根）。留空则用 tests/templates/<模板名>（存在才搬）。
    [string]$Tests = '',

    # 同一游戏的其它目录（相对仓库根），如 templates/server/server-legend
    [string[]]$SiblingPaths = @(),

    # 兄弟目录在目标项目里的别名目录名
    [string]$SiblingAlias = 'server',

    # 放项目顶层 tests/ 的跨端用例（相对仓库根，如 tests/templates/legend/test_rules_sync.py）
    # —— 同时读客户端与服务端的用例不适合塞进某一端
    [string[]]$TopTests = @(),

    # 随项目一起搬走的仓库级脚本，形如 'scripts/foo.py=client/tools/foo.py'
    # （游戏专属的工具跟着游戏走，仓库只留通用工具）。
    # 用分号分隔多条：pwsh -File 传参时逗号不会拆成数组。
    [string]$ExtraFiles = '',

    # 不执行 git init
    [switch]$NoGit,

    # 目标已存在时允许覆盖（默认拒绝，避免覆盖别人的项目）
    [switch]$Force,

    # 仅供脚本自测：允许目标落在仓库内
    [switch]$AllowInsideRepo,

    # 从某个 git 引用取模板源（默认用工作区当前版本）。
    # 模板搬出仓库后，仓库里已没有源目录——这时用打标签的旧提交取源：
    #   git tag archive/game-wuwei-2026-10-20 <删除前的提交>
    #   pwsh -File scripts/export_game_project.ps1 -Template game/wuwei `
    #       -Target D:/project/games/wuwei -SourceRef archive/game-wuwei-2026-10-20
    [string]$SourceRef = ''
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$templateName = Split-Path -Leaf ($Template -replace '/', '\')

function Fail([string]$message) { throw $message }

# 路径统一成 Windows 形式
function Norm([string]$p) { return ($p -replace '/', '\') }

# ---------- 源目录 ----------
# 默认取工作区当前版本；-SourceRef 时从 git 归档取
# （模板搬出仓库后仓库里已无源，用打标签的旧提交取）。
$sourceBase = $repo
$archiveRoot = ''
$testRel = if ($Tests) { $Tests } else { "tests/templates/$templateName" }
if ($SourceRef) {
    $archiveRoot = Join-Path ([IO.Path]::GetTempPath()) ("zan-export-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $archiveRoot | Out-Null
    $archivePaths = @("templates/$Template")
    if (Test-Path (Join-Path $repo (Norm $testRel))) { $archivePaths += $testRel }
    foreach ($sibling in $SiblingPaths) { $archivePaths += $sibling }
    foreach ($top in $TopTests) { $archivePaths += $top }
    foreach ($extra in ($ExtraFiles -split ';' | Where-Object { $_ -ne '' })) { $archivePaths += ($extra -split '=', 2)[0] }
    Push-Location $repo
    try {
        $zip = Join-Path $archiveRoot 'src.zip'
        & git archive --format=zip -o $zip $SourceRef -- $archivePaths | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "git archive 失败（ref=$SourceRef）" }
        Expand-Archive -Path $zip -DestinationPath (Join-Path $archiveRoot 'unz') -Force
        $sourceBase = Join-Path $archiveRoot 'unz'
        Write-Host "   源来自 git 归档：$SourceRef"
    }
    finally { Pop-Location }
}

$source = Join-Path $sourceBase (Norm "templates/$Template")
$testSource = Join-Path $sourceBase (Norm $testRel)
if (-not (Test-Path $source)) { Fail "模板目录不存在：$source" }

$target = [IO.Path]::GetFullPath($Target).TrimEnd('\', '/')
if ($target -eq $source) { Fail '目标与源相同' }
$insideRepo = $target.StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)
if ($insideRepo -and -not $AllowInsideRepo) {
    Fail "目标落在仓库内（$target）。导出目标应当是仓库外的独立项目；自测请加 -AllowInsideRepo。"
}
if (Test-Path $target) {
    if (-not $Force) { Fail "目标已存在：$target（要覆盖请加 -Force）" }
    Write-Host "覆盖既有目录：$target"
}

# ---------- 改写规则 ----------
function Rule([string]$from, [string]$to, [string]$scope = '') {
    $r = @{ From = $from; To = $to }
    if ($scope) { $r['Scope'] = $scope }
    return $r
}

$rules = @()
# 仓库路径 → 项目路径（一律相对项目根）
$rules += Rule "templates/game/$templateName/" 'client/'
$rules += Rule "templates/game/$templateName" 'client'
foreach ($sibling in $SiblingPaths) {
    $sp = ($sibling -replace '\\', '/')
    $rules += Rule "$sp/" "$SiblingAlias/"
    $rules += Rule $sp $SiblingAlias
}
$rules += Rule "tests/templates/$templateName/" 'client/tests/'
$rules += Rule "tests/templates/$templateName" 'client/tests'
# 模板占位符落成实名（服务端 README / config / 源码里都有 {{NAME}}）
$rules += Rule '{{NAME}}' $templateName
# 编译器探测：优先 $env:ZAN_HOME 指向的 zan-lang（编译器与 stdlib 同源），否则 PATH 上的 zanc
$rules += Rule "    `$local = Join-Path `$repo 'build/zanc.exe'" "    `$local = if (`$env:ZAN_HOME) { Join-Path `$env:ZAN_HOME 'build/zanc.exe' } else { '' }"
$rules += Rule "    if (Test-Path `$local) { `$Compiler = `$local } else { `$Compiler = (Get-Command zanc -ErrorAction Stop).Source }" "    if (`$local -and (Test-Path `$local)) { `$Compiler = `$local } else { `$Compiler = (Get-Command zanc -ErrorAction Stop).Source }"
$rules += Rule "    [string]`$Stdlib = ''" "    [string]`$Stdlib = `$env:ZAN_STDLIB"

# 模板专属规则
$profiles = @{
    'wuwei' = @(
        # 仓库根 → 项目根（$project 是 client/，其父目录就是项目根）
        Rule "`$repo = [IO.Path]::GetFullPath((Join-Path `$project '../../..'))" "`$repo = Split-Path -Parent `$project" 'client/*'
        # 输出目录：仓库内是 build/wuwei/release，独立项目里是 <项目根>/build/release
        Rule "    if (Test-Path (Join-Path `$repo 'AGENTS.md')) { `$Output = Join-Path `$repo 'build/wuwei/release' }" "    `$Output = Join-Path `$repo 'build/release'"
        Rule "    else { `$Output = Join-Path `$project 'build/release' }" "    if (`$false) { `$Output = Join-Path `$repo 'build/release' }"
        Rule 'build/wuwei/' 'build/'
        # stdlib：项目根没有 stdlib/，走 $env:ZAN_HOME
        Rule "if (-not `$Stdlib -and (Test-Path (Join-Path `$repo 'stdlib'))) { `$Stdlib = Join-Path `$repo 'stdlib' }" "if (-not `$Stdlib -and `$env:ZAN_HOME) { `$Stdlib = Join-Path `$env:ZAN_HOME 'stdlib' }"
        # client/tests/ 下的用例：parents[3]（仓库根）→ parents[2]（项目根）
        Rule 'parents[3]' 'parents[2]' 'client/tests/*'
        # zan.proj 的 entry 指不到文件（模板里没有 src/main.zan）
        Rule 'entry = src/main.zan' 'entry = src/WuweiApp.BagSlot.zan'
    )
    'legend' = @(
        # 客户端构建脚本：仓库根 → 项目根（client/tools → client → 项目根）
        Rule "`$repo = [IO.Path]::GetFullPath((Join-Path `$PSScriptRoot '../../../..'))" "`$repo = Split-Path -Parent (Split-Path -Parent `$PSScriptRoot)" 'client/tools/*'
        Rule "if (!`$Compiler) { `$Compiler = Join-Path `$repo 'build/zanc.exe' }" "if (!`$Compiler) { `$Compiler = if (`$env:ZAN_HOME) { Join-Path `$env:ZAN_HOME 'build/zanc.exe' } else { (Get-Command zanc -ErrorAction Stop).Source } }"
        # 客户端自检脚本：同上
        Rule "`$root = [IO.Path]::GetFullPath((Join-Path `$PSScriptRoot '../../../..'))" "`$root = Split-Path -Parent (Split-Path -Parent `$PSScriptRoot)" 'client/tools/*'
        Rule '"$root/build/zanc.exe"' '$(if ($env:ZAN_HOME) { "$env:ZAN_HOME/build/zanc.exe" } else { ''zanc'' })'
        # client/tests/ 下的用例：parents[3]（仓库根）→ parents[2]（项目根）
        Rule 'parents[3]' 'parents[2]' 'client/tests/*'
        # server/tools/ 下的脚本：parents[4]（仓库根）→ parents[2]（项目根）
        Rule 'parents[4]' 'parents[2]' 'server/*'
        Rule "SOURCE = ROOT / 'templates' / 'game' / 'legend' / 'data'" "SOURCE = ROOT / 'client' / 'data'" 'server/*'
        # 服务端脚本从 server/tools 往上两级就是项目根
        Rule 'os.path.join(here, "..", "..", "..", "..")' 'os.path.join(here, "..", "..")' 'server/*'
        # 客户端 minimir 脚本从 client/tools/minimir 往上三级就是项目根
        Rule 'os.path.join(here, "..", "..", "..", "..")' 'os.path.join(here, "..", "..", "..")' 'client/*'
        # 跨端用例（项目顶层 tests/）：dirname 四层 → 两层
        # 收尾的右括号也一起减：四层 dirname + 1 层 abspath → 两层 dirname + 1 层 abspath
        Rule "ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(" "ROOT = os.path.dirname(os.path.dirname(" 'tests/*'
        Rule "os.path.abspath(__file__)))))" "os.path.abspath(__file__)))" 'tests/*'
        Rule "SOURCE = os.path.join(ROOT, 'templates', 'game', 'legend', 'data')" "SOURCE = os.path.join(ROOT, 'client', 'data')" 'tests/*'
        Rule "DEST = os.path.join(ROOT, 'templates', 'server', 'server-legend', 'data', 'rules')" "DEST = os.path.join(ROOT, 'server', 'data', 'rules')" 'tests/*'
        # 跨端用例在顶层 tests/，不在客户端套件里
        Rule 'client/tests/test_rules_sync.py' 'tests/test_rules_sync.py' 'server/*'
        # 随项目搬走的素材提取器：默认输出目录落到 client/assets（相对脚本自身）
        Rule 'default=Path("client/assets")' "default=Path(__file__).resolve().parents[1]/'assets'"
        Rule '[string]$OutDir = "client/assets"' "[string]`$OutDir = (Join-Path `$PSScriptRoot '../assets')"
        Rule 'python scripts/extract_legend_assets.py --client $ClientDir --out $OutDir' "python (Join-Path `$PSScriptRoot 'extract_assets.py') --client `$ClientDir --out `$OutDir"
    )
}
$profile = $profiles[$templateName]
if ($profile) { $rules += $profile }

$textExtensions = @('.ps1', '.py', '.zan', '.md', '.json', '.proj', '.manifest', '.css',
    '.html', '.txt', '.js', '.yml', '.yaml', '.toml', '.cfg', '.ini', '.bat', '.cmd', '.sh')

function Copy-Tree([string]$from, [string]$to) {
    if (-not (Test-Path $from)) { return $false }
    New-Item -ItemType Directory -Force -Path $to | Out-Null
    & robocopy $from $to /E /NFL /NDL /NJH /NJS /NP /MT:8 /XD build _scratch .git __pycache__ .pytest_cache | Out-Null
    if ($LASTEXITCODE -ge 8) { Fail "复制失败（robocopy=$LASTEXITCODE）：$from → $to" }
    return $true
}

function Rewrite-Tree([string]$root, [array]$rules) {
    if (-not $rules -or $rules.Count -eq 0) { return 0 }
    $changed = 0
    $utf8 = [Text.UTF8Encoding]::new($false)
    Get-ChildItem $root -Recurse -File | Where-Object {
        $textExtensions -contains $_.Extension.ToLowerInvariant()
    } | ForEach-Object {
        $path = $_.FullName
        $rel = $path.Substring($root.Length).TrimStart('\', '/') -replace '\\', '/'
        $text = [IO.File]::ReadAllText($path)
        $original = $text
        foreach ($rule in $rules) {
            # Scope 缺省 = 全局；给定时只作用于该目录前缀下的文件
            if ($rule.ContainsKey('Scope') -and $rule.Scope -and ($rel -notlike $rule.Scope)) { continue }
            $text = $text.Replace($rule.From, $rule.To)
        }
        if ($text -ne $original) {
            [IO.File]::WriteAllText($path, $text, $utf8)
            $changed++
        }
    }
    return $changed
}

Write-Host "== 导出 $Template → $target"

# ---------- 复制：client/ + server/ + tests/ ----------
$clientDir = Join-Path $target 'client'
$serverDir = Join-Path $target $SiblingAlias
$topTestDir = Join-Path $target 'tests'

Copy-Tree $source $clientDir | Out-Null
Write-Host "   $Template → client/"

if (Test-Path $testSource) {
    Copy-Tree $testSource (Join-Path $clientDir 'tests') | Out-Null
    Write-Host "   $testRel → client/tests/"
}

foreach ($sibling in $SiblingPaths) {
    $siblingSource = Join-Path $sourceBase (Norm $sibling)
    if (-not (Test-Path $siblingSource)) { Fail "兄弟目录不存在：$siblingSource" }
    Copy-Tree $siblingSource $serverDir | Out-Null
    Write-Host "   $sibling → $SiblingAlias/"
}

foreach ($top in $TopTests) {
    $topSource = Join-Path $sourceBase (Norm $top)
    if (-not (Test-Path $topSource)) { Fail "跨端用例不存在：$topSource" }
    New-Item -ItemType Directory -Force -Path $topTestDir | Out-Null
    if ((Get-Item $topSource).PSIsContainer) {
        Copy-Tree $topSource $topTestDir | Out-Null
    }
    else {
        Copy-Item $topSource $topTestDir -Force
        # 同一份文件也出现在客户端套件里 → 客户端那份删掉，避免两处维护
        $dup = Join-Path $clientDir ('tests\' + (Split-Path -Leaf $top))
        if (Test-Path $dup) { Remove-Item $dup -Force }
    }
    Write-Host "   $top → tests/（跨端）"
}

foreach ($extra in ($ExtraFiles -split ';' | Where-Object { $_ -ne '' })) {
    $parts = $extra -split '=', 2
    $extraSource = Join-Path $sourceBase (Norm $parts[0])
    if (-not (Test-Path $extraSource)) { Fail "附加文件不存在：$extraSource" }
    $extraTarget = Join-Path $target (Norm $parts[1])
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $extraTarget) | Out-Null
    Copy-Item $extraSource $extraTarget -Force
    Write-Host "   $($parts[0]) → $($parts[1])"
}

# ---------- 改写路径引用 ----------
$changed = Rewrite-Tree $target $rules
Write-Host "   改写文件数：$changed"

# ---------- 模板身份退役 ----------
# 客户端与服务端都退役：manifest 删掉，zan.proj 的 {{NAME}} 落成实名。
$utf8 = [Text.UTF8Encoding]::new($false)
function Retire-Template([string]$dir, [string]$name) {
    $manifest = Join-Path $dir 'template.manifest'
    if (Test-Path $manifest) { Remove-Item $manifest -Force }
    $proj = Join-Path $dir 'zan.proj'
    if (Test-Path $proj) {
        $text = [IO.File]::ReadAllText($proj)
        $rx = [regex]::new('(?m)^name\s*=\s*.*$')
        $text = $rx.Replace($text, "name = $name", 1)
        [IO.File]::WriteAllText($proj, $text, $utf8)
    }
}
Retire-Template $clientDir $templateName
if (Test-Path $serverDir) { Retire-Template $serverDir "$templateName-server" }

# ---------- 项目说明 ----------
# 用**单引号** here-string（不做插值）写死文案，再用占位符回填：
# 双引号 here-string 里 markdown 代码围栏的第三个反引号会转义换行，
# 把收尾的 "@ 吞进字符串——解析器报 "missing the terminator" 就是这个坑。
$projectDoc = @'
# {{NAME}} —— 独立项目

本项目从 zan-lang 仓库的 `templates/{{TEMPLATE}}` 导出（导出日期：{{DATE}}）。
语言仓库只放 SDK、公共包与**示例级**模板；产品级游戏在这里自己演进。

## 目录

    {{NAME}}/
      client/      客户端：src/ assets/ data/ tools/，客户端用例在 client/tests/
      {{SERVER}}/  服务端：源码、规则表副本、服务端工具
      tests/       跨端用例（同时读客户端与服务端）

所有命令都在**项目根**下执行（路径一律相对项目根）。

## 编译

```powershell
$env:ZAN_HOME = 'D:/project/zan-lang'   # 编译器与 stdlib 同源
pwsh -File client/tools/build.ps1
```

ZAN_HOME 指向 zan-lang 仓库时用它的 `build/zanc.exe` 与 `stdlib/`；
不设则用 PATH 上的 `zanc`。stdlib 也可用 ZAN_STDLIB 单独指定。

## 公共包

包从三处解析：`<项目根>/packages/`、`<项目根>/.zan-packages/`、
全局 `%LOCALAPPDATA%\Zan\packages`。需要某个包时：

```powershell
Copy-Item -Recurse <zan-lang>/packages/<Name> packages/<Name>
```

或编译时加 `--package-project <zan-lang>`（直接吃仓库里的 packages/）。

## 测试

```powershell
pwsh -File client/tools/test.ps1     # 客户端用例（Python 回归 + 无头模型用例）
python tests/test_rules_sync.py      # 跨端用例（规则表同步）
```

## 服务端

服务端规则表由客户端数据单向同步：

```powershell
python {{SERVER}}/tools/sync_rules.py            # 同步
python {{SERVER}}/tools/sync_rules.py --check    # 只校验漂移
```
'@
$projectDoc = $projectDoc.Replace('{{NAME}}', $templateName).
    Replace('{{TEMPLATE}}', $Template).
    Replace('{{DATE}}', (Get-Date -Format 'yyyy-MM-dd')).
    Replace('{{SERVER}}', $SiblingAlias)
[IO.File]::WriteAllText((Join-Path $target 'PROJECT.md'), $projectDoc, [Text.UTF8Encoding]::new($false))

# 独立项目自己的 .gitignore：构建产物与临时目录不进版本库
$gitignore = @'
# 构建产物
build/
# 临时目录（用例的临时数据、崩溃转储）
_scratch/
# Python 缓存
__pycache__/
*.pyc
'@
[IO.File]::WriteAllText((Join-Path $target '.gitignore'), $gitignore, [Text.UTF8Encoding]::new($false))

# ---------- 独立 git 仓库 ----------
if (-not $NoGit) {
    Push-Location $target
    try {
        if (-not (Test-Path (Join-Path $target '.git'))) {
            & git init -q
            & git add -A
            & git commit -q -m "chore: 从 zan-lang 仓库导出为独立项目（模板 $Template）"
            Write-Host "   已 git init + 首次提交"
        }
        else { Write-Host '   目标已有 .git，跳过 git init' }
    }
    finally { Pop-Location }
}

Write-Host ''
Write-Host "导出完成：$target"
Write-Host "下一步："
Write-Host "  1) cd $target"
Write-Host "  2) `$env:ZAN_HOME = 'D:/project/zan-lang'"
Write-Host "  3) pwsh -File client/tools/build.ps1"
Write-Host "  4) pwsh -File client/tools/test.ps1"
