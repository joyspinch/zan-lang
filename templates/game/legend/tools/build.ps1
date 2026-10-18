param([string]$Compiler = '', [string]$Output = '')
$ErrorActionPreference = 'Stop'
$template = Split-Path $PSScriptRoot -Parent
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if (!$Compiler) { $Compiler = Join-Path $repo 'build/zanc.exe' }
if (!$Output) { $Output = Join-Path $repo 'build/legend-game' }
$Output = [IO.Path]::GetFullPath($Output)
if ($Output.StartsWith($template + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or $Output -eq $template) { throw 'Build output must be outside the template source directory' }
New-Item -ItemType Directory -Force -Path $Output | Out-Null
# 递归扫描 src/：分部类与实体已按类名分文件（LegendApp.Page*.zan / Models/*.zan），
# 逐文件硬编码清单在改名后必然失配，这里不再维护清单。
$sources = @(Get-ChildItem "$template/src" -Recurse -Filter *.zan |
    Sort-Object FullName | Select-Object -ExpandProperty FullName)
# 并行会话在改 packages/Zan.Gui.Charts/src/Gui/Component/Chart（`Chart.StackExtentF` 等签名变化还没
# 落完）；--auto-stdlib 看到 `using Gui;` 会把整张 Chart 拉进编译。legend 本身
# 不用 Chart，复用 _scratch/stdlib-noc（HEAD 干净副本）做局部旁路。
$nocRoot = Join-Path $repo '_scratch/stdlib-noc'
$useNoc = (Test-Path $nocRoot) -and $env:LEGEND_STDLIB_HEAD -eq '1'
if ($useNoc) {
    & $Compiler @sources --stdlib-path $nocRoot -o "$Output/Legend.exe"
} else {
    & $Compiler @sources --auto-stdlib -o "$Output/Legend.exe"
}
if ($LASTEXITCODE -ne 0) { throw 'Legend compilation failed' }
foreach ($dir in @('assets', 'skins', 'data')) { Copy-Item -LiteralPath "$template/$dir" -Destination $Output -Recurse -Force }
Write-Host "Built: $Output/Legend.exe"
