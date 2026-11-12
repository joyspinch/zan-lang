# Compile the actual path methods, without starting the IDE or copying its source tree.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$WorkDir,
    [string]$Compiler = (Join-Path $PSScriptRoot '../../build/zanc.exe')
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source = Join-Path $repo 'src/ide_zan/src/services/ZanIDE.Paths.zan'
$text = [IO.File]::ReadAllText($source)
$methods = foreach ($name in @('HomeDir', 'IsCachePathAbsolute', 'CacheDir')) {
    $match = [regex]::Match($text, "(?ms)^    static (?:string|bool) $name\([^)]*\) \{.*?^    \}")
    if (-not $match.Success) { throw "Actual method not found: $name" }
    $match.Value
}
[IO.Directory]::CreateDirectory($WorkDir) | Out-Null
$probe = Join-Path $WorkDir 'CachePathProbe.zan'
$exe = Join-Path $WorkDir 'CachePathProbe.exe'
$program = @'
using System;
using System.IO;
class ZanIDE {
    [DllImport("crt")]
    static extern string getenv(string name);
__METHODS__
    static void Main() { Console.WriteLine(ZanIDE.CacheDir()); }
}
'@
[IO.File]::WriteAllText($probe, $program.Replace('__METHODS__', ($methods -join "`n")))
& $Compiler $probe --auto-stdlib -o $exe *> (Join-Path $WorkDir 'compile.log')
if ($LASTEXITCODE -ne 0) { throw 'Cache path probe compilation failed; inspect compile.log' }
$names = @('ZAN_IDE_CACHE_DIR', 'LOCALAPPDATA', 'USERPROFILE', 'HOME', 'XDG_CACHE_HOME')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
$profile = Join-Path $WorkDir 'private-profile'
try {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $null) }
    [Environment]::SetEnvironmentVariable('USERPROFILE', $profile)
    [Environment]::SetEnvironmentVariable('HOME', $profile)
    $cases = [Collections.Generic.List[object]]::new()
    if ($IsWindows -or $env:OS -eq 'Windows_NT') {
        [Environment]::SetEnvironmentVariable('LOCALAPPDATA', "$profile/local")
        $cases.Add(@{ name = 'local-appdata'; expected = "$profile/local/ZanIDE/cache" })
        $cases.Add(@{ name = 'local-drive-relative'; expected = "$profile/AppData/Local/ZanIDE/cache"; localValue = 'C:relative' })
        $cases.Add(@{ name = 'local-root-relative'; expected = "$profile/AppData/Local/ZanIDE/cache"; localValue = '\rooted' })
        $cases.Add(@{ name = 'local-unc'; expected = '\\server\share\cache/ZanIDE/cache'; localValue = '\\server\share\cache' })
        $cases.Add(@{ name = 'profile-fallback'; expected = "$profile/AppData/Local/ZanIDE/cache"; clearLocal = $true })
        $cases.Add(@{ name = 'profile-drive-relative'; expected = ([IO.Path]::GetTempPath() + 'ZanIDE/cache'); profileValue = 'C:relative' })
        $cases.Add(@{ name = 'profile-root-relative'; expected = ([IO.Path]::GetTempPath() + 'ZanIDE/cache'); profileValue = '\rooted' })
    } elseif ($IsMacOS) {
        $cases.Add(@{ name = 'mac-cache'; expected = "$profile/Library/Caches/ZanIDE" })
    } else {
        [Environment]::SetEnvironmentVariable('XDG_CACHE_HOME', "$profile/xdg")
        $cases.Add(@{ name = 'xdg-cache'; expected = "$profile/xdg/zan-ide" })
        $cases.Add(@{ name = 'home-fallback'; expected = "$profile/.cache/zan-ide"; clearXdg = $true })
    }
    $cases.Add(@{ name = 'absent-profile-temp'; expected = ([IO.Path]::GetTempPath() + 'ZanIDE/cache'); clearProfile = $true })
    $cases.Add(@{ name = 'explicit-override'; expected = "$profile/explicit-cache"; explicit = $true })
    $cases.Add(@{ name = 'explicit-relative-override'; expected = 'relative-cache'; explicit = $true })
    $rows = foreach ($case in $cases) {
        if ($case.ContainsKey('localValue')) { [Environment]::SetEnvironmentVariable('LOCALAPPDATA', $case.localValue) }
        if ($case.ContainsKey('profileValue')) { [Environment]::SetEnvironmentVariable('USERPROFILE', $case.profileValue) }
        if ($case.clearLocal) { [Environment]::SetEnvironmentVariable('LOCALAPPDATA', $null) }
        if ($case.clearXdg) { [Environment]::SetEnvironmentVariable('XDG_CACHE_HOME', $null) }
        if ($case.clearProfile) {
            foreach ($name in @('LOCALAPPDATA', 'USERPROFILE', 'HOME', 'XDG_CACHE_HOME')) { [Environment]::SetEnvironmentVariable($name, $null) }
        }
        if ($case.explicit) { [Environment]::SetEnvironmentVariable('ZAN_IDE_CACHE_DIR', $case.expected) }
        $actual = (& $exe | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or $actual -cne $case.expected) { throw "Cache path case failed: $($case.name)" }
        @{ case = $case.name; passed = $true }
    }
    @{ source_sha256 = (Get-FileHash $source -Algorithm SHA256).Hash.ToLowerInvariant(); cases = @($rows) } |
        ConvertTo-Json -Depth 5 | Set-Content (Join-Path $WorkDir 'result.json') -Encoding utf8
    Write-Output "ok: IDE cache path defaults, profile fallback, explicit override ($($rows.Count) cases)"
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
