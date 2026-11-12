# Stage the same toolchain for a development IDE and a published SDK.
# Plan and validate every required input before touching the destination.
param(
    [string]$Build = '',
    [string]$Dest = '',
    [switch]$IncludeDebugger,
    [switch]$PreflightOnly,
    [string]$InputList = ''
)
$ErrorActionPreference = 'Stop'
# pwsh -> Python -> Windows PowerShell can inherit pwsh's incompatible Utility
# module first on PSModulePath. Load the selected host's own hashing commands.
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'sdk_stage.ps1')
if (-not $Build) { $Build = Join-Path $root 'build' }
if (-not $Dest) { $Dest = Join-Path $Build 'toolchain' }
$Build = [IO.Path]::GetFullPath($Build)
if (-not (Test-Path -LiteralPath $Build -PathType Container)) { throw "STAGE_FAILED: no build directory: $Build" }
$destFull = Assert-SdkStagePath $Dest $root @((Join-Path $root 'build'), (Join-Path $root 'dist'), (Join-Path $root '_scratch'))
$actions = [Collections.Generic.List[object]]::new()
$missing = [Collections.Generic.List[string]]::new()

function Add-StageInput([string]$Source, [string]$Name, [switch]$Directory, [switch]$Required) {
    $kind = if ($Directory) { 'Container' } else { 'Leaf' }
    if (-not $Source -or -not (Test-Path -LiteralPath $Source -PathType $kind)) {
        if ($Required) { $missing.Add($Name) }
        return
    }
    $full = [IO.Path]::GetFullPath($Source)
    if ($full -eq $destFull -or $full.StartsWith($destFull + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        ($Directory -and $destFull.StartsWith($full + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))) {
        throw "STAGE_FAILED: source/destination overlap: $full -> $destFull"
    }
    $actions.Add([pscustomobject]@{ source = $full; name = $Name; directory = [bool]$Directory })
}
foreach ($exe in @('zanc.exe', 'zan-lsp.exe', 'zan-dap.exe', 'zanfmt.exe', 'zandoc.exe', 'ld.exe', 'ld.lld.exe')) {
    Add-StageInput (Join-Path $Build $exe) $exe -Required
}
Add-StageInput (Join-Path $Build 'mingw') 'mingw' -Directory -Required
Add-StageInput (Find-HostLlvmAr $Build) 'llvm-ar.exe' -Required
foreach ($runtime in @('zanrt_timer.obj', 'zanrt_io.obj', 'zanrt_io_mt.obj', 'zanrt_sync.obj', 'zanrt_file.obj',
                        'zanrt_decimal.obj', 'zan_embed_api.obj', 'zan_inflate.obj', 'libzancrypto.a')) {
    Add-StageInput (Join-Path $Build $runtime) $runtime -Required
}
foreach ($sub in @('linux-musl', 'linux-arm64', 'linux-riscv64', 'win-x64', 'win-arm64', 'wasm32',
                   'riscv64', 'macos', 'ios', 'ohos-x64', 'ohos-arm64')) {
    $src = Join-Path $Build $sub
    if (-not (Test-Path -LiteralPath $src -PathType Container)) { $src = Join-Path (Join-Path $root 'toolchain') $sub }
    # The advertised Linux x64 target is part of the Windows SDK contract.
    Add-StageInput $src $sub -Directory -Required:($sub -eq 'linux-musl')
}
foreach ($rt in (Get-ChildItem -LiteralPath $Build -File | Where-Object {
    ($_.Name -like 'zanrt_*' -or $_.Name -like 'zan_inflate*' -or $_.Name -like 'zan_embed_api*' -or $_.Name -like 'libzancrypto*') -and
    $_.Extension -in '.o', '.obj', '.a'
})) { Add-StageInput $rt.FullName $rt.Name }
$owners = @{
    zan_gui = 'packages/Zan.Gui/src/Gui/drivers'
    zan_image = 'packages/Zan.Drawing/src/System/Drawing/Imaging/drivers'
    zan_audio = 'packages/Zan.Desktop/src/System/Media/drivers'
    zan_game = 'packages/Zan.Game/src/Game/Graphics/drivers'
}
foreach ($driver in @('zan_gui', 'zan_image', 'zan_audio', 'zan_game')) {
    foreach ($name in @("$driver.dll", "$driver.lib", "lib$driver.dll.a")) { Add-StageInput (Join-Path $Build $name) $name }
    $owner = Join-Path (Join-Path $root $owners[$driver]) 'win-x64'
    $name = "$driver.bundle"
    $bundle = Join-Path $Build $name
    if (-not (Test-Path -LiteralPath $bundle -PathType Leaf)) { $bundle = Join-Path $owner $name }
    Add-StageInput $bundle $name -Required
    $runtimeDll = Join-Path $Build "$driver.dll"
    if (-not (Test-Path -LiteralPath $runtimeDll -PathType Leaf)) { $runtimeDll = Join-Path $owner "$driver.dll" }
    Add-StageInput $runtimeDll "$driver.dll" -Required
    if (Test-Path -LiteralPath $bundle -PathType Leaf) {
        foreach ($entry in (Get-Content -LiteralPath $bundle)) {
            if ($entry -match '^\s*([^#@\s]+)(?:\s+if\s+\S+)?\s*$') {
                $payloadName = $Matches[1]
                if ([IO.Path]::GetFileName($payloadName) -ne $payloadName) { throw "STAGE_FAILED: unsafe bundle payload: $payloadName" }
                $payload = Join-Path $Build $payloadName
                if (-not (Test-Path -LiteralPath $payload -PathType Leaf)) { $payload = Join-Path $owner $payloadName }
                Add-StageInput $payload $payloadName -Required
            }
        }
    }
}
if ($IncludeDebugger) {
    $gdb = Resolve-SdkGdb
    Add-StageInput $gdb 'debugger\bin\gdb.exe' -Required
    $gdbBin = Split-Path -Parent $gdb
    foreach ($dll in (Get-ChildItem -LiteralPath $gdbBin -Filter '*.dll' -File)) {
        Add-StageInput $dll.FullName (Join-Path 'debugger\bin' $dll.Name)
    }
    foreach ($support in @('Lib', 'DLLs')) {
        Add-StageInput (Join-Path $gdbBin $support) (Join-Path 'debugger\bin' $support) -Directory
    }
    Add-StageInput (Join-Path (Split-Path -Parent $gdbBin) 'share') 'debugger\share' -Directory
}
Add-StageInput (Join-Path $root 'assets\zan.ico') 'zan.ico' -Required
if ($missing.Count) { throw "STAGE_FAILED: missing required inputs in ${Build}: $($missing -join ', ')" }
# A directory existing is insufficient: the bundled linker/sysroot must be usable.
foreach ($file in @('lib\crt2.o', 'lib\libmingw32.a', 'lib\libkernel32.a')) { Assert-SdkFile (Join-Path (Join-Path $Build 'mingw') $file) }
$linux = $actions | Where-Object { $_.name -eq 'linux-musl' } | Select-Object -First 1
foreach ($file in @('libc.a', 'crt1.o', 'crti.o', 'crtn.o', 'zanrt_timer.o', 'zanrt_io.o', 'zanrt_io_mt.o', 'zanrt_sync.o',
                    'zanrt_file.o', 'zanrt_decimal.o', 'zan_embed_api.o', 'zan_inflate.o', 'libzancrypto.a')) {
    Assert-SdkFile (Join-Path $linux.source $file)
}
# Inventory expands actual directories, not an independently maintained file list.
$inputs = @($actions | ForEach-Object {
    if ($_.directory) { Get-ChildItem -LiteralPath $_.source -Recurse -File | Select-Object -ExpandProperty FullName }
    else { $_.source }
} | Sort-Object -Unique)
if ($InputList) {
    $listFull = Assert-SdkStagePath $InputList $root @((Join-Path $root '_scratch'), (Join-Path $root 'build\test-out'))
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $listFull) | Out-Null
    $mappings = @($actions | ForEach-Object {
        $action = $_
        if ($action.directory) {
            Get-ChildItem -LiteralPath $action.source -Recurse -File | ForEach-Object {
                [pscustomobject]@{ source = $_.FullName; destination = (Join-Path $action.name $_.FullName.Substring($action.source.Length + 1)) }
            }
        } else { [pscustomobject]@{ source = $action.source; destination = $action.name } }
    })
    ConvertTo-Json -InputObject $mappings -Depth 5 | Set-Content -LiteralPath $listFull -Encoding UTF8
}
if ($PreflightOnly) {
    if ($IncludeDebugger) {
        $probe = Join-Path (Join-Path $root '_scratch') ('sdk-gdb-probe-' + [Guid]::NewGuid().ToString('N'))
        try {
            New-Item -ItemType Directory -Path $probe -Force | Out-Null
            foreach ($action in ($actions | Where-Object { $_.name -like 'debugger\*' })) {
                $target = Join-Path $probe $action.name.Substring('debugger\'.Length)
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                if ($action.directory) { Copy-Item -LiteralPath $action.source -Destination $target -Recurse -Force }
                else { Copy-Item -LiteralPath $action.source -Destination $target -Force }
            }
            Test-SdkGdb (Join-Path $probe 'bin\gdb.exe')
        } finally { if (Test-Path -LiteralPath $probe) { Remove-Item -LiteralPath $probe -Recurse -Force } }
    }
    Write-Output "STAGE_PREFLIGHT_OK ($($inputs.Count) input files)"
    return
}
New-Item -ItemType Directory -Force -Path $destFull | Out-Null
$copied = 0
foreach ($action in $actions) {
    $dst = Join-Path $destFull $action.name
    if ($action.directory) {
        & robocopy $action.source $dst /MIR /XJ /NJH /NJS /NFL /NDL /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "STAGE_FAILED: robocopy $($action.source) -> $dst ($LASTEXITCODE)" }
    } else {
        # Future mtimes and equal lengths do not imply equal bytes.
        if ((Test-Path -LiteralPath $dst -PathType Leaf) -and
            (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $action.source -Algorithm SHA256).Hash) { continue }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        Copy-Item -LiteralPath $action.source -Destination $dst -Force
    }
    $copied++
}
if ($IncludeDebugger) { Test-SdkGdb (Join-Path $destFull 'debugger\bin\gdb.exe') }
foreach ($obsolete in @('pkg_stub.exe', 'pack_single.ps1')) { Remove-Item -LiteralPath (Join-Path $destFull $obsolete) -Force -ErrorAction SilentlyContinue }
Write-Output "STAGE_TOOLCHAIN_OK -> $destFull ($copied item(s) updated)"
