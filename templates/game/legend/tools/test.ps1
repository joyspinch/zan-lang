param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$source = Join-Path $root 'templates/game/legend/src'
# 逻辑层源码 = src/ 去掉两样与引擎自检无关的东西：
#   1. main.zan —— GUI 入口（自带 Main），自检程序也自带 Main；
#   2. LegendApp.*.zan —— 分部类页面，引用 main.zan 里声明的控件字段，
#      少了 main.zan 必然报"xxx is not a known variable"（踩过：整条门
#      100+ 报错、连 DataRow 都跟着报 undefined，根因就是这两个）。
# 自检文件放**最后一个**参数（入口选择看第一入口，实测放最前会走偏）。
$libSources = @(Get-ChildItem $source -Recurse -Filter *.zan |
    Where-Object { $_.Name -ne 'main.zan' -and $_.Name -notlike 'LegendApp.*.zan' } |
    Sort-Object FullName | Select-Object -ExpandProperty FullName)
Push-Location $root
try {
    & "$root/build/zanc.exe" @libSources "$root/tests/templates/legend/attributes.zan" --auto-stdlib -o "$root/build/legend-attributes-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Attribute test compilation failed' }
    & "$root/build/legend-attributes-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Attribute runtime regression failed' }
    & "$root/build/zanc.exe" @libSources "$root/tests/templates/legend/inventory_progression.zan" --auto-stdlib -o "$root/build/legend-inventory-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Inventory test compilation failed' }
    & "$root/build/legend-inventory-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Inventory runtime regression failed' }
    & "$root/build/zanc.exe" @libSources "$root/tests/templates/legend/map_travel.zan" --auto-stdlib -o "$root/build/legend-map-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Map test compilation failed' }
    & "$root/build/legend-map-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Map travel runtime regression failed' }
    & "$root/build/zanc.exe" @libSources "$root/tests/templates/legend/systems.zan" --auto-stdlib -o "$root/build/legend-systems-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Systems test compilation failed' }
    & "$root/build/legend-systems-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Systems gameplay regression failed' }
    & "$root/build/zanc.exe" @libSources "$root/tests/templates/legend/altar.zan" --auto-stdlib -o "$root/build/legend-altar-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Altar test compilation failed' }
    & "$root/build/legend-altar-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Altar gameplay regression failed' }
    & $Python "$root/tests/templates/legend/test_extract_wzl.py"
    if ($LASTEXITCODE -ne 0) { throw 'WZL extractor regression failed' }
    & $Python "$root/tests/templates/legend/test_definitions.py"
    if ($LASTEXITCODE -ne 0) { throw 'Definition regression failed' }
} finally { Pop-Location }
