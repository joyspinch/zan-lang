# coreforge 构建：GUI 壳（src/*.zan + 设计稿 src/App.html）→ 可执行文件。
#
# 逻辑层在 packages/Zan.Game.Factory（namespace Game.Factory），靠
# `using Game.Factory;` 自动带进编译；公共包住在仓库的 packages/ 下，编译要带
# --package-project 指向包根（仓库内 = 仓库根；项目搬到仓库外后 = zan-lang 仓库，
# 由 $ZAN_HOME 或 -PackageProject 指定）。
param(
    [string]$Compiler = $env:ZANC,
    [string]$Stdlib = '',
    [string]$Output = '',
    [string]$PackageProject = ''
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$repo = [IO.Path]::GetFullPath((Join-Path $project '../../..'))
if (-not $Compiler) {
    $local = Join-Path $repo 'build/zanc.exe'
    if (Test-Path $local) { $Compiler = $local } else { $Compiler = (Get-Command zanc -ErrorAction Stop).Source }
}
if (-not $Stdlib -and (Test-Path (Join-Path $repo 'stdlib'))) { $Stdlib = Join-Path $repo 'stdlib' }
if (-not $PackageProject) { $PackageProject = $repo }
if (-not $Output) { $Output = Join-Path $repo 'build/coreforge' }
$Output = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path $Output | Out-Null
# 入口 main.zan 必须排第一：zanc 只把 Main() 给第一个编译输入（genrun.c 的
# emitMain=paths[0]）。其余 .zan 递归枚举（模板壳按类名分文件），设计稿排最后
# （它生成 partial 类的控件字段）。
$entry = Join-Path $project 'src/main.zan'
$sources = @()
if (Test-Path $entry) { $sources += $entry }
$sources += @(Get-ChildItem (Join-Path $project 'src') -Recurse -File -Filter *.zan |
    Where-Object { $_.FullName -ne $entry } | Sort-Object FullName | ForEach-Object { $_.FullName })
$sources += @(Get-ChildItem (Join-Path $project 'src') -Recurse -File -Include *.html, *.htm |
    Sort-Object FullName | ForEach-Object { $_.FullName })
$zargs = @($sources) + @('--auto-stdlib', '--package-project', $PackageProject)
if ($Stdlib) { $zargs += @('--stdlib-path', $Stdlib) }
$exe = Join-Path $Output 'Coreforge.exe'
$zargs += @('-o', $exe)
& $Compiler @zargs
if ($LASTEXITCODE -ne 0) { throw "zanc failed ($LASTEXITCODE)" }
# 皮肤：游戏用 UseSkin("coreforge") 按 ./skins 解析（相对运行目录），
# 所以皮肤要跟 exe 待在一起。
Copy-Item (Join-Path $project 'skins') $Output -Recurse -Force
Write-Host "Built $exe"
