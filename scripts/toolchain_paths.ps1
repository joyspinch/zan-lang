# Shared probe for external toolchain executables used by the PowerShell
# build scripts. Dot-source it; it defines functions only and prints nothing.
#
# Resolution order (same for every caller, so no script grows its own table):
#   1. explicit environment variables -- a file, a tool directory, or a
#      command name;
#   2. PATH (Get-Command);
#   3. standard install roots, built from the environment bases Windows
#      actually exposes (SystemDrive / ProgramFiles / LOCALAPPDATA / ...),
#      never from one author's drive letter.
#
# On failure callers print Format-ZanProbeFailure: what was searched, and
# which variable to set. New tools should add a Get-Zan<Family>Roots builder
# here instead of pasting another list into a script.

function Get-ZanInstallBases {
    $bases = New-Object System.Collections.Generic.List[string]
    foreach ($name in @('SystemDrive', 'ProgramFiles', 'ProgramW6432',
                        'LOCALAPPDATA', 'USERPROFILE', 'HOME')) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value -and -not $bases.Contains($value)) { $bases.Add($value) }
    }
    return $bases.ToArray()
}

function Get-ZanMinGWRoots {
    $roots = New-Object System.Collections.Generic.List[string]
    foreach ($name in @('ZAN_MINGW_ROOT', 'MINGW_ROOT', 'MINGW_HOME')) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value) { $roots.Add($value) }
    }
    foreach ($base in Get-ZanInstallBases) {
        foreach ($sub in @('msys64\mingw64', 'msys64\ucrt64', 'msys64\clang64',
                           'TDM-GCC-64', 'mingw64', 'mingw32', 'mingw',
                           'scoop\apps\mingw\current', 'Programs\mingw64')) {
            $roots.Add((Join-Path $base $sub))
        }
    }
    return $roots.ToArray()
}

function Get-ZanLLVMRoots {
    $roots = New-Object System.Collections.Generic.List[string]
    foreach ($name in @('ZAN_LLVM_ROOT', 'LLVM_ROOT', 'LLVM_HOME')) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value) { $roots.Add($value) }
    }
    # LLVM_DIR normally points at <root>\lib\cmake\llvm; strip that suffix.
    $llvmDir = [Environment]::GetEnvironmentVariable('LLVM_DIR')
    if ($llvmDir) {
        $root = $llvmDir.TrimEnd('\', '/')
        $root = $root -replace '(?i)[\\/]lib[\\/]cmake[\\/]llvm$', ''
        $roots.Add($root)
    }
    foreach ($base in Get-ZanInstallBases) {
        $roots.Add((Join-Path $base 'LLVM'))
        $roots.Add((Join-Path $base 'msys64\clang64'))
    }
    $userProfile = [Environment]::GetEnvironmentVariable('USERPROFILE')
    $homeDir = [Environment]::GetEnvironmentVariable('HOME')
    foreach ($base in @($userProfile, $homeDir)) {
        if ($base) { $roots.Add((Join-Path $base '.mozbuild\clang')) }
    }
    # Every Visual Studio install reported by vswhere, any edition/year.
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if ($programFilesX86) {
        $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            foreach ($vs in @(& $vswhere -all -products * -property installationPath)) {
                if ($vs) { $roots.Add((Join-Path $vs 'VC\Tools\Llvm\x64')) }
            }
        }
    }
    return $roots.ToArray()
}

function Get-ZanFreeTypeRoots {
    $roots = New-Object System.Collections.Generic.List[string]
    foreach ($name in @('ZAN_FREETYPE_ROOT', 'FREETYPE_SRC', 'FREETYPE_INC')) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value) { $roots.Add($value) }
    }
    foreach ($base in @([Environment]::GetEnvironmentVariable('USERPROFILE'),
                        [Environment]::GetEnvironmentVariable('HOME'))) {
        if (-not $base) { continue }
        foreach ($sub in @('firefox\modules\freetype2', '.mozbuild\firefox\modules\freetype2',
                           'src\firefox\modules\freetype2', 'freetype\modules\freetype2')) {
            $roots.Add((Join-Path $base $sub))
        }
    }
    return $roots.ToArray()
}

# Resolve one executable. -Relative is tried both under an environment value
# (which may be a file, a bin directory, or a root) and under each -Roots entry,
# so a single list covers "root\bin\tool.exe" and "bin-dir\tool.exe" callers.
function Resolve-ZanExecutable {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string[]]$EnvVars = @(),
        [string[]]$Commands = @(),
        [string[]]$Roots = @(),
        [string[]]$Relative = @(),
        [ref]$Tried
    )
    $attempts = New-Object System.Collections.Generic.List[string]

    foreach ($name in $EnvVars) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if (-not $value) { continue }
        $attempts.Add("${name}=${value}")
        if (Test-Path -LiteralPath $value -PathType Leaf) {
            if ($Tried) { $Tried.Value = $attempts }
            return (Get-Item -LiteralPath $value).FullName
        }
        if (Test-Path -LiteralPath $value -PathType Container) {
            foreach ($rel in $Relative) {
                $candidate = Join-Path $value $rel
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    if ($Tried) { $Tried.Value = $attempts }
                    return (Get-Item -LiteralPath $candidate).FullName
                }
            }
        } else {
            $cmd = Get-Command $value -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($cmd -and $cmd.Source) {
                if ($Tried) { $Tried.Value = $attempts }
                return $cmd.Source
            }
        }
    }

    foreach ($command in $Commands) {
        $cmd = Get-Command $command -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($cmd -and $cmd.Source) {
            $attempts.Add("PATH: $($cmd.Source)")
            if ($Tried) { $Tried.Value = $attempts }
            return $cmd.Source
        }
    }

    foreach ($root in $Roots) {
        if (-not $root) { continue }
        foreach ($rel in $Relative) {
            $candidate = Join-Path $root $rel
            $attempts.Add($candidate)
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                if ($Tried) { $Tried.Value = $attempts }
                return (Get-Item -LiteralPath $candidate).FullName
            }
        }
    }

    if ($Tried) { $Tried.Value = $attempts }
    return $null
}

function Format-ZanProbeFailure {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string[]]$EnvVars = @(),
        [string[]]$Tried = @()
    )
    $lines = if ($Tried.Count -gt 0) {
        ($Tried | ForEach-Object { "  - $_" }) -join "`n"
    } else { '  - (no candidates)' }
    $hint = if ($EnvVars.Count -gt 0) {
        "Set $($EnvVars -join ' / ') to the installation root or the executable path."
    } else { 'Install it, or pass an explicit path.' }
    return "$Name not found. Tried:`n$lines`n$hint"
}
