param([string]$Compiler = '', [string]$Output = '')
$ErrorActionPreference = 'Stop'
$template = Split-Path $PSScriptRoot -Parent
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if (!$Compiler) { $Compiler = Join-Path $repo 'build/zanc.exe' }
if (!$Output) { $Output = Join-Path $repo 'build/legend-game' }
$Output = [IO.Path]::GetFullPath($Output)
if ($Output.StartsWith($template + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or $Output -eq $template) { throw 'Build output must be outside the template source directory' }
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$sources = @(
    'main.zan', 'Spec.zan', 'Tables.zan', 'Data.zan', 'Game.zan',
    'Save.zan', 'Net.zan', 'Shell.zan', 'PageMap.zan', 'PageMapInner.zan',
    'PageRedPack.zan', 'LoginWin.zan',
    'PageArena.zan', 'PageBag.zan', 'PageScene.zan', 'PageBossHome.zan', 'PageEscort.zan', 'PageGuild.zan', 'PageRank.zan', 'PageMarket.zan', 'PageMail.zan', 'PageAch.zan', 'PageRecycle.zan', 'PageTujian.zan', 'PageBasha.zan', 'PageAltar.zan', 'PageElement.zan', 'PageTreasure.zan', 'PageMaster.zan', 'PageHelp.zan', 'PageSettings.zan', 'PageSmithy.zan', 'PageCity.zan', 'PageStar.zan', 'PageSupreme.zan', 'PageTower.zan', 'PageLord.zan', 'PageShop.zan', 'PageTitle.zan', 'Slots.zan', 'TableUi.zan'
) | ForEach-Object { "$template/src/$_" }
# 并行会话在改 stdlib/Gui/Component/Chart（`Chart.StackExtentF` 等签名变化还没
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
