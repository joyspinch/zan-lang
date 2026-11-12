# Shared staging checks; no build, scheduler or lock is acquired here.
function Assert-SdkStagePath([string]$Path, [string]$Root, [string[]]$AllowedParents) {
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $allowed = $false
    foreach ($parent in $AllowedParents) {
        $prefix = [IO.Path]::GetFullPath($parent).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        if ($full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { $allowed = $true }
    }
    if (-not $allowed) { throw "STAGE_FAILED: output must be a dedicated child of $($AllowedParents -join ', '): $full" }
    foreach ($protected in @($Root, (Join-Path $Root 'scripts'), (Join-Path $Root 'stdlib'),
                             (Join-Path $Root 'packages'), (Join-Path $Root 'templates'),
                             (Join-Path $Root 'examples'))) {
        $source = [IO.Path]::GetFullPath($protected).TrimEnd('\', '/')
        if ($full -eq $source -or $source.StartsWith($full + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "STAGE_FAILED: output contains protected source: $full"
        }
    }
    $cursor = $full
    while ($cursor) {
        $item = Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
        if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "STAGE_FAILED: output traverses reparse point: $cursor"
        }
        $cursor = Split-Path -Parent $cursor
    }
    if (Test-Path -LiteralPath $full -PathType Container) {
        $link = Get-ChildItem -LiteralPath $full -Force -Recurse -ErrorAction Stop |
            Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1
        if ($link) { throw "STAGE_FAILED: output contains reparse point: $($link.FullName)" }
    }
    return $full
}

function Assert-SdkFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "STAGE_FAILED: required file missing: $Path" }
}

function Find-HostLlvmAr([string]$buildDir) {
    $candidates = @((Join-Path $buildDir 'llvm-ar.exe'))
    $cache = Join-Path $buildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache -PathType Leaf) {
        foreach ($key in @('LLVM_TOOLS_BINARY_DIR', 'LLVM_DIR', 'CMAKE_C_COMPILER')) {
            $entry = Select-String -LiteralPath $cache -Pattern "^${key}:[^=]*=(.+)$" | Select-Object -First 1
            if (-not $entry) { continue }
            $value = $entry.Matches[0].Groups[1].Value.Trim()
            switch ($key) {
                'LLVM_TOOLS_BINARY_DIR' { $candidates += Join-Path $value 'llvm-ar.exe' }
                'LLVM_DIR' { $candidates += Join-Path $value '../../../bin/llvm-ar.exe' }
                'CMAKE_C_COMPILER' {
                    $parent = Split-Path -Parent $value
                    if ($parent) { $candidates += Join-Path $parent 'llvm-ar.exe' }
                }
            }
        }
    }
    foreach ($prefix in @($env:LLVM_ROOT, $env:LLVM_PATH)) {
        if ($prefix) { $candidates += Join-Path $prefix 'bin/llvm-ar.exe' }
    }
    $onPath = Get-Command llvm-ar.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($onPath) { $candidates += $onPath.Source }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [IO.Path]::GetFullPath($candidate) }
    }
    return $null
}

function Resolve-SdkGdb {
    . (Join-Path $PSScriptRoot 'toolchain_paths.ps1')
    $tried = $null
    $exe = Resolve-ZanExecutable -Name 'gdb' `
        -EnvVars @('ZAN_GDB_DIR', 'ZAN_GDB', 'MINGW_ROOT', 'MINGW_HOME') `
        -Commands @('gdb.exe', 'gdb') -Roots (Get-ZanMinGWRoots) `
        -Relative @('gdb.exe', 'bin\gdb.exe') -Tried ([ref]$tried)
    if (-not $exe) { throw (Format-ZanProbeFailure -Name 'gdb' -EnvVars @('ZAN_GDB_DIR', 'ZAN_GDB') -Tried $tried) }
    # TDM's bin/gdb.exe is a launcher with a baked-in installation path. Its
    # sibling distribution contains the actual executable and runtime DLLs.
    $tdmReal = Join-Path (Split-Path -Parent (Split-Path -Parent $exe)) 'gdb64\bin\gdb.exe'
    if (Test-Path -LiteralPath $tdmReal -PathType Leaf) { return $tdmReal }
    return $exe
}

function Test-SdkGdb([string]$Exe) {
    # Relocated executables must work with no developer toolchain on PATH.
    $oldPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $Exe
        $start.Arguments = '--version'
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $probe = [Diagnostics.Process]::Start($start)
        if (-not $probe.WaitForExit(30000)) {
            Stop-Process -Id $probe.Id -Force
            throw "STAGE_FAILED: bundled gdb timed out: $Exe"
        }
        if ($probe.ExitCode -ne 0) { throw "STAGE_FAILED: bundled gdb is not runnable: $Exe ($($probe.ExitCode))" }
    } finally { $env:PATH = $oldPath }
}
