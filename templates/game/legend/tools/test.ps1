param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$source = Join-Path $root 'templates/game/legend/src'
# 逻辑层源码 = src/ 全量（main.zan 除外，它带 GUI 入口 Main；测试自带 Main）。
$libSources = @(Get-ChildItem $source -Recurse -Filter *.zan |
    Where-Object { $_.Name -ne 'main.zan' } |
    Sort-Object FullName | Select-Object -ExpandProperty FullName)
Push-Location $root
try {
    & "$root/build/zanc.exe" "$root/tests/templates/legend/attributes.zan" @libSources --auto-stdlib -o "$root/build/legend-attributes-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Attribute test compilation failed' }
    & "$root/build/legend-attributes-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Attribute runtime regression failed' }
    & "$root/build/zanc.exe" "$root/tests/templates/legend/inventory_progression.zan" @libSources --auto-stdlib -o "$root/build/legend-inventory-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Inventory test compilation failed' }
    & "$root/build/legend-inventory-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Inventory runtime regression failed' }
    & "$root/build/zanc.exe" "$root/tests/templates/legend/map_travel.zan" @libSources --auto-stdlib -o "$root/build/legend-map-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Map test compilation failed' }
    & "$root/build/legend-map-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Map travel runtime regression failed' }
    & "$root/build/zanc.exe" "$root/tests/templates/legend/systems.zan" @libSources --auto-stdlib -o "$root/build/legend-systems-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Systems test compilation failed' }
    & "$root/build/legend-systems-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Systems gameplay regression failed' }
    & "$root/build/zanc.exe" "$root/tests/templates/legend/altar.zan" @libSources --auto-stdlib -o "$root/build/legend-altar-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Altar test compilation failed' }
    & "$root/build/legend-altar-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Altar gameplay regression failed' }
    & $Python "$root/tests/templates/legend/test_extract_wzl.py"
    if ($LASTEXITCODE -ne 0) { throw 'WZL extractor regression failed' }
    & $Python "$root/tests/templates/legend/test_definitions.py"
    if ($LASTEXITCODE -ne 0) { throw 'Definition regression failed' }
} finally { Pop-Location }
