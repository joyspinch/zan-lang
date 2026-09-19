param(
    [string]$Compiler = $env:ZANC,
    [string]$Stdlib = '',
    [string]$Output = '',
    [switch]$Development
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$repo = [IO.Path]::GetFullPath((Join-Path $project '../../..'))
if (-not $Compiler) {
    $local = Join-Path $repo 'build/zanc.exe'
    if (Test-Path $local) { $Compiler = $local } else { $Compiler = (Get-Command zanc -ErrorAction Stop).Source }
}
if (-not $Stdlib -and (Test-Path (Join-Path $repo 'stdlib'))) { $Stdlib = Join-Path $repo 'stdlib' }
if (-not $Output) {
    if (Test-Path (Join-Path $repo 'AGENTS.md')) { $Output = Join-Path $repo 'build/wuwei/release' }
    else { $Output = Join-Path $project 'build/release' }
}
$Output = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force $Output | Out-Null
$sources = @(Get-ChildItem (Join-Path $project 'src/*.zan') -File | Sort-Object Name | ForEach-Object { $_.FullName })
# 设计稿排在整个源单最后：zanc 只把生成的 Main() 给第一个编译输入
# （genrun.c 的 emitMain=paths[0]），而本游戏自管生命周期、main.zan
# 里手写 Main —— App.html 绝不能当第一输入。
$design = Join-Path $project 'src/App.html'
if (Test-Path $design) { $sources += $design }
$args = @($sources) + @('--auto-stdlib', '--subsystem', 'windows')
if ($Stdlib) { $args += @('--stdlib-path', $Stdlib) }
if ($Development) {
    Write-Host "DBG-DEV-MODE project=$project"
    Copy-Item (Join-Path $project 'data') (Join-Path $Output 'data') -Recurse -Force
} else {
    $packed = Join-Path (Split-Path -Parent $Output) 'protected-tables'
    & python (Join-Path $PSScriptRoot 'pack_data.py') --source (Join-Path $project 'data') --output $packed
    if ($LASTEXITCODE -ne 0) { throw 'Table packing failed. Install Python cryptography first.' }
    $args += @('--publish', '--embed', "$packed=wuwei-data", '--embed', "$(Join-Path $project 'assets')=assets", '--embed', "$(Join-Path $project 'skins')=skins")
    foreach ($folder in @('data', 'assets', 'skins')) {
        if (Test-Path (Join-Path $Output $folder)) { throw "Release output contains a loose $folder directory; choose a fresh output directory to verify embedded resources." }
    }
}
$exe = Join-Path $Output 'WuweiCultivation.exe'
$args += @('-o', $exe)
& $Compiler @args
if ($LASTEXITCODE -ne 0) { throw "zanc failed ($LASTEXITCODE)" }
if ($Development) {
    foreach ($folder in @('assets', 'skins')) {
        $destination = Join-Path $Output $folder
        New-Item -ItemType Directory -Force $destination | Out-Null
        Copy-Item (Join-Path $project "$folder/*") $destination -Recurse -Force
    }
}
Copy-Item (Join-Path $project 'SECURITY.md') $Output -Force
Copy-Item (Join-Path $project 'ASSETS.md') $Output -Force
Copy-Item (Join-Path $project 'README.md') $Output -Force
Write-Host "Built $exe"
