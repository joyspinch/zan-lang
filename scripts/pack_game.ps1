# pack_game.ps1 -- 把一个 GameKit 游戏工程打包成可分发目录：
#   dist/<name>/game.exe        游戏可执行文件
#   dist/<name>/assets.zrp      加密资源包（AES-256-GCM 逐条目，可选 RSA 签名）
#   dist/<name>/zrp.keys/*      主密钥 XOR 分片（绝不进 git；见下）
# 并删除 dist 内的明文 assets/（-KeepPlain 保留以便调试）。
#
# 资源包消费端：examples/game/common/GameKit.zan 的 PackedAssets
# （条目名 = 工程相对路径，主密钥 = 两份分片 XOR 重组）。
# 加密本体在 stdlib 的 ResourcePackWriter（tools/packtool/PackTool.zan
# 的 CLI 壳），本脚本只做文件收集与参数拼装，不实现任何加密。
#
# 用法示例：
#   scripts\pack_game.ps1 -Project examples/game/snake                # 全默认
#   scripts\pack_game.ps1 -Project examples/game/legend -SignKeyPem k.pem -KeepPlain
#
# 密钥策略：
#   -KeyHex 直接给 64 hex（复现/测试）；否则若 zrp.keys/<name>.share_[ab].hex
#   已存在则复用；否则用 tools/packtool 自动生成（写 zrp.keys/<name>.share_[ab].hex）。
#   zrp.keys/ 已被 .gitignore 忽略——分片是唯一的解密凭据，丢了包就废了。
#
# 签名：-SignKeyPem <PEM>（PKCS#1/PKCS#8 私钥，RsaKey 可解析的格式）时对
# pack 的 index 签名；不传则包无签名（PackedAssets.Open 不校验签名，两态兼容）。
# 提取 hex：本脚本用 packtool 的 --keyhex 通道传递，PEM→n/d hex 的换算由
# openssl 完成（仓库工具链已带；无 openssl 时跳过签名并告警）。
param(
    [Parameter(Mandatory = $true)]
    [string]$Project,          # 游戏工程目录（含 main.zan / assets/）
    [string]$KeyHex = '',      # 64 hex 主密钥（可选；不给则自动/复用分片）
    [string]$SignKeyPem = '',  # RSA 私钥 PEM（可选；给了对 index 签名）
    [string]$OutDir = '',      # 输出目录（默认 dist/<name>/）
    [string]$Entry = 'main.zan',
    [switch]$KeepPlain,        # 保留 dist 内明文 assets/（调试用）
    [switch]$SkipBuild         # 只重打包资源，不重编 exe
)

$ErrorActionPreference = "Stop"
$root = (Split-Path -Parent $PSScriptRoot) -replace '\\', '/'
Set-Location $root

# PATHEXT 守卫：继承到残缺 PATHEXT（如仅 ".CPL"，pwsh 在空值时的派生结果）
# 的宿主里，pwsh 会把 .exe 当"文档"走 ShellExecute 异步启动——& 不阻塞、
# $LASTEXITCODE 永不赋值、stdout 丢失，[2/4] 只能看到空失败。确保含 .EXE。
if (($env:PATHEXT -eq $null) -or ($env:PATHEXT -notlike '*.EXE*')) {
    $env:PATHEXT = '.COM;.EXE;.BAT;.CMD;.VBS;.JS;.WSF;.MSC;.CPL'
}

if (-not (Test-Path (Join-Path $Project $Entry))) {
    Write-Output "PACK_FAIL: no $Entry in $Project"
    exit 2
}
# zanc/packtool 都不接受反斜杠路径（zan 目录 API 对 '\'-路径会抛
# "Unhandled exception"），所有传给 zan 工具链的路径统一正斜杠。
# （Join-Path 产物如 'examples/game/snake\assets' 会踩中这个坑。）
$Project  = ($Project  -replace '\\', '/')
$OutDir   = ($OutDir   -replace '\\', '/')
$SignKeyPem = ($SignKeyPem -replace '\\', '/')
$name = Split-Path -Leaf $Project
if ($OutDir -eq '') { $OutDir = "dist/$name" }
$assetsDir = "$Project/assets"
$keysDir = "$root/zrp.keys"
New-Item -ItemType Directory -Force -Path $OutDir, $keysDir | Out-Null

# ---- [0] packtool 就绪 ------------------------------------------------------
$packtool = 'build/packtool.exe'
if (-not (Test-Path $packtool)) {
    Write-Output "[0/4] Building tools/packtool ..."
    build/zanc.exe tools/packtool/PackTool.zan --auto-stdlib -o $packtool
    if ($LASTEXITCODE -ne 0) { Write-Output "PACK_FAIL: packtool build"; exit 1 }
}

# ---- [1] 密钥：给定的 KeyHex > 既有分片 > packtool 自动生成 -----------------
# 分片路径也归一成正斜杠（它们要作为 --share-a/-b 传进 zan 工具链）。
$shareA = "$keysDir/$name.share_a.hex"
$shareB = "$keysDir/$name.share_b.hex"
$keyArgs = @()
if ($KeyHex -ne '') {
    $keyArgs = @('--keyhex', $KeyHex)
    Write-Output "[1/4] Using caller-provided key (not persisted)"
} elseif ((Test-Path $shareA) -and (Test-Path $shareB)) {
    $keyArgs = @('--share-a', $shareA, '--share-b', $shareB)
    Write-Output "[1/4] Reusing key shares zrp.keys/$name.share_[ab].hex"
} else {
    $keyArgs = @()   # packtool 自动生成并写 <out>.share_[ab].hex
    Write-Output "[1/4] No key found: packtool will generate shares into zrp.keys/"
}

# ---- [2] 打资源包 -----------------------------------------------------------
$zrp = "$OutDir/assets.zrp"
$manifest = "$OutDir/assets.manifest.json"
$signArgs = @()
if ($SignKeyPem -ne '') {
    $openssl = Get-Command openssl -ErrorAction SilentlyContinue
    if ($null -eq $openssl) {
        Write-Output "WARN: openssl not found; packing UNSIGNED"
    } else {
        # PEM → PKCS#1 派生的模数/私钥指数 hex（RSA 原语吃 n/d 两个大端整数）。
        $asn1 = openssl rsa -in $SignKeyPem -noout -modulus 2>$null
        if ($LASTEXITCODE -ne 0) { Write-Output "PACK_FAIL: bad PEM"; exit 1 }
        $nHex = ($asn1 -replace '^Modulus=', '').Trim()
        # d = 私钥指数：openssl 不直接输出，走 asn1parse 抠 PKCS#1 的第 4 个
        # INTEGER 过于脆弱——改为要求 PEM 已是 PKCS#1 并用 -text 提取。
        $text = openssl rsa -in $SignKeyPem -noout -text 2>$null
        $joined = ($text -join ' ')
        if ($joined -match 'privateExponent:\s*([0-9a-f:\s]+?)publicExponent') {
            $dHex = ($Matches[1] -replace '[\s:]', '')
            $signArgs = @('--sign-n', $nHex, '--sign-d', $dHex)
            Write-Output "[2/4] Signing index (RSA modulus $($nHex.Length / 2) bytes)"
        } else {
            Write-Output "WARN: could not extract privateExponent; packing UNSIGNED"
        }
    }
}

Write-Output "[2/4] Packing assets -> $zrp"
& $packtool $assetsDir --out $zrp --manifest $manifest @keyArgs @signArgs
if ($LASTEXITCODE -ne 0) { Write-Output "PACK_FAIL: packtool"; exit 1 }
# packtool 自动生成的分片搬到统一目录，避免散在 dist 里被一起分发。
if (Test-Path "$zrp.share_a.hex") {
    Move-Item -Force "$zrp.share_a.hex" $shareA
    Move-Item -Force "$zrp.share_b.hex" $shareB
    Write-Output "      shares -> zrp.keys/$name.share_[ab].hex (git-ignored)"
}

# ---- [3] 编译/复用 exe ------------------------------------------------------
$exe = "$OutDir/$name.exe"
if (-not $SkipBuild) {
    Write-Output "[3/4] Compiling $name ..."
    # 源文件作为独立 argv 逐个传给 zanc（空格拼接成单个字符串会被 zanc
    # 当成一个路径："cannot open file 'a.zan b.zan'"）。
    $srcs = @((Get-ChildItem -Path $Project -Filter *.zan | ForEach-Object { ($_.FullName -replace '\\', '/') }))
    $srcs += "$root/examples/game/common/GameKit.zan"
    build/zanc.exe @srcs --auto-stdlib -o $exe
    if ($LASTEXITCODE -ne 0) { Write-Output "PACK_FAIL: zanc"; exit 1 }
} elseif (-not (Test-Path $exe)) {
    Write-Output "PACK_FAIL: -SkipBuild but $exe missing"
    exit 1
}

# ---- [4] 清明文 -------------------------------------------------------------
if (-not $KeepPlain) {
    Write-Output "[4/4] Removing plaintext assets from dist ..."
    Remove-Item -Recurse -Force "$OutDir/assets" -ErrorAction SilentlyContinue
} else {
    Write-Output "[4/4] Keeping plaintext assets (-KeepPlain)"
}

Write-Output "PACK_OK: $OutDir"
Write-Output "  game:   $exe"
Write-Output "  pack:   $zrp ($((Get-Item $zrp).Length) bytes)"
Write-Output "  keys:   zrp.keys/$name.share_[ab].hex -- KEEP SAFE, NOT IN GIT"
