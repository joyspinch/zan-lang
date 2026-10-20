<#
.SYNOPSIS
    生成游戏素材图（gpt-image-2），并做抠图/裁剪/缩放后处理。

.DESCRIPTION
    密钥只从 D:/project/secrets.local.ps1 取（不落盘、不进仓库）。
    风格统一靠 -Style 前缀：同一批模板素材共用一个 STYLE 串，
    色调与画法才成一套。

.EXAMPLE
    # 背景（不抠图，缩到 1024 宽）
    pwsh -File scripts\gen_game_art.ps1 -Prompt "a wide empty canyon at dusk" `
        -Out templates\game\platformer\assets\bg_sky.png -Size 1536x1024 -MaxSize 1024

.EXAMPLE
    # 精灵（绿幕抠成透明 + 去透明边 + 缩到 256）
    pwsh -File scripts\gen_game_art.ps1 -Prompt "a small armored robot hero" `
        -Out templates\game\platformer\assets\hero.png -Chroma 0,255,0 -Trim -MaxSize 256
#>
param(
    # 画面描述（英文，风格词放前面）
    [Parameter(Mandatory = $true)]
    [string]$Prompt,

    # 输出文件（png）
    [Parameter(Mandatory = $true)]
    [string]$Out,

    # 生图尺寸：1024x1024 / 1536x1024 / 1024x1536
    [string]$Size = '1024x1024',

    # 风格前缀（拼在 Prompt 前面）；留空则自动取共享风格串
    [string]$Style = '',

    # 品类限定词（arcade/card/tactics/puzzle/idle）；留空则不加
    [string]$Genre = '',

    # 共享风格文件（STYLE 行 + GENRES 行）
    [string]$StyleFile = 'assets/game/style.md',

    # 绿幕色 'R,G,B'：给了就抠成透明
    [string]$Chroma = '',

    # 绿幕容差
    [int]$Tolerance = 70,

    # 裁掉透明边
    [switch]$Trim,

    # 长边缩放到这个像素（0 = 不缩）
    [int]$MaxSize = 0,

    # 失败重试次数
    [int]$Retries = 3,

    # 密钥来源（默认 D:/project/secrets.local.ps1）
    [string]$Secrets = 'D:/project/secrets.local.ps1'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path $Secrets)) { throw "找不到密钥文件：$Secrets" }
. $Secrets
if (-not $env:OPENAI_API_KEY) { throw '密钥文件里没有 OPENAI_API_KEY' }
if (-not $env:OPENAI_BASE_URL) { throw '密钥文件里没有 OPENAI_BASE_URL' }

$script = Join-Path $repo 'scripts\gen_game_art.py'
$outPath = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $repo $Out }

# 共享风格：统一风格靠共享串，不靠各模板互相抄素材（assets/game/style.md）
$styleFilePath = if ([IO.Path]::IsPathRooted($StyleFile)) { $StyleFile } else { Join-Path $repo $StyleFile }
$sharedStyle = ''
$genres = @{}
if (Test-Path $styleFilePath) {
    foreach ($line in [IO.File]::ReadAllLines($styleFilePath, [Text.Encoding]::UTF8)) {
        if ($line -match '^\s*STYLE:\s*(.+?)\s*$') { $sharedStyle = $Matches[1] }
        elseif ($line -match '^\s*GENRES:\s*(.+?)\s*$') {
            foreach ($pair in $Matches[1] -split '\|') {
                $kv = $pair -split '=', 2
                if ($kv.Count -eq 2) { $genres[$kv[0].Trim()] = $kv[1].Trim() }
            }
        }
    }
}
$styleParts = @()
if ($Style) { $styleParts += $Style }
elseif ($sharedStyle) { $styleParts += $sharedStyle }
if ($Genre -and $genres.ContainsKey($Genre)) { $styleParts += $genres[$Genre] }
$fullPrompt = if ($styleParts.Count -gt 0) { ($styleParts -join '. ') + '. ' + $Prompt } else { $Prompt }

$args = @(
    $script,
    '--prompt', $fullPrompt,
    '--out', $outPath,
    '--size', $Size,
    '--retries', "$Retries"
)
if ($Chroma) { $args += @('--chroma', $Chroma, '--tolerance', "$Tolerance") }
if ($Trim) { $args += @('--trim') }
if ($MaxSize -gt 0) { $args += @('--max', "$MaxSize") }

& python @args
if ($LASTEXITCODE -ne 0) { throw "生图失败（exit=$LASTEXITCODE）" }
