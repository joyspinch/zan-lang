# gen_catalogs.ps1 -- Generate machine-readable package & example catalogs for AI
#   <OutDir>\packages-catalog.json  All available packages, exported namespaces & drivers
#   <OutDir>\examples-catalog.json  Curated examples & templates capability map
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\gen_catalogs.ps1 [-OutDir knowledge]

param(
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $root 'knowledge' }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }
$OutDir = (Resolve-Path $OutDir).Path

Write-Output "[Catalog] Scanning packages in packages/ ..."
$packagesDir = Join-Path $root 'packages'
$packageEntries = @()

if (Test-Path $packagesDir) {
    Get-ChildItem -Path $packagesDir -Directory | ForEach-Object {
        $pkgDir = $_.FullName
        $pkgName = $_.Name
        $pkgManifest = Join-Path $pkgDir 'zan.pkg'
        $pkgReadme = Join-Path $pkgDir 'README.md'
        
        $desc = ""
        $ver = "1.0.0"
        $license = "MIT"
        
        if (Test-Path $pkgManifest) {
            $raw = Get-Content $pkgManifest -Raw
            try {
                $j = $raw | ConvertFrom-Json
                if ($j.description) { $desc = [string]$j.description }
                if ($j.version) { $ver = [string]$j.version }
                if ($j.license) { $license = [string]$j.license }
            } catch {
                foreach ($line in ($raw -split "`r?`n")) {
                    $trim = $line.Trim()
                    if ($trim -match '^version\s*=\s*"(.*)"') { $ver = $Matches[1] }
                    elseif ($trim -match '^description\s*=\s*"(.*)"') { $desc = $Matches[1] }
                    elseif ($trim -match '^license\s*=\s*"(.*)"') { $license = $Matches[1] }
                }
            }
        }
        
        if (-not $desc -and (Test-Path $pkgReadme)) {
            $lines = Get-Content $pkgReadme | Where-Object { $_.Trim() -ne "" -and -not ($_.StartsWith('#')) }
            if ($lines.Count -gt 0) { $desc = $lines[0].Trim() }
        }
        
        # Discover exported namespaces under src/
        $namespaces = @()
        $srcDir = Join-Path $pkgDir 'src'
        if (Test-Path $srcDir) {
            Get-ChildItem -Path $srcDir -Recurse -Filter '*.zan' | ForEach-Object {
                $dirRel = $_.DirectoryName.Substring($srcDir.Length).Trim('\', '/')
                if ($dirRel) {
                    $ns = $dirRel.Replace('\', '.').Replace('/', '.')
                    if (-not ($namespaces -contains $ns)) {
                        $namespaces += $ns
                    }
                }
            }
        }
        if ($namespaces.Count -eq 0) {
            $namespaces = @($pkgName)
        }
        
        # Check native drivers
        $hasDrivers = $false
        $platforms = @()
        Get-ChildItem -Path $pkgDir -Recurse -Filter 'driver.manifest' -ErrorAction SilentlyContinue | ForEach-Object {
            $hasDrivers = $true
            $parent = $_.Directory.FullName
            foreach ($plat in @('win-x64', 'win-arm64', 'linux-x64', 'linux-arm64', 'macos-x64', 'macos-arm64', 'android-arm64', 'android-x64')) {
                if (Test-Path (Join-Path $parent $plat)) {
                    if (-not ($platforms -contains $plat)) { $platforms += $plat }
                }
            }
        }
        
        $packageEntries += [PSCustomObject]@{
            name = $pkgName
            version = $ver
            description = $desc
            license = $license
            namespaces = $namespaces
            has_native_drivers = $hasDrivers
            platforms = $platforms
        }
    }
}

$packagesJsonPath = Join-Path $OutDir 'packages-catalog.json'
$packageEntries | ConvertTo-Json -Depth 5 | Set-Content $packagesJsonPath -Encoding UTF8
$pkgCount = $packageEntries.Count
Write-Output "KNOWLEDGE_PACKAGES_OK -> $packagesJsonPath ($pkgCount packages)"

Write-Output "[Catalog] Scanning examples and templates ..."
$exampleEntries = @()
$examplesDir = Join-Path $root 'examples'

if (Test-Path $examplesDir) {
    Get-ChildItem -Path $examplesDir -Directory | ForEach-Object {
        $exDir = $_.FullName
        $exId = $_.Name
        $readme = Join-Path $exDir 'README.md'
        $title = $exId
        $desc = ""
        
        if (Test-Path $readme) {
            $lines = Get-Content $readme
            foreach ($line in $lines) {
                $trim = $line.Trim()
                if ($trim.StartsWith('# ')) {
                    $title = $trim.Substring(2).Trim()
                } elseif (-not $desc -and $trim -ne "" -and -not ($trim.StartsWith('#')) -and -not ($trim.StartsWith('`'))) {
                    $desc = $trim
                }
            }
        }
        
        # Find primary entry source file
        $entry = ""
        $cands = @('src/main.zan', 'src/App.zan', 'main.zan', 'App.zan', "src/$exId.zan")
        foreach ($cand in $cands) {
            if (Test-Path (Join-Path $exDir $cand)) {
                $entry = "examples/$exId/$cand"
                break
            }
        }
        if (-not $entry) {
            $firstZan = Get-ChildItem -Path $exDir -Recurse -Filter '*.zan' | Select-Object -First 1
            if ($firstZan) {
                $entry = $firstZan.FullName.Substring($root.Length).Trim('\', '/').Replace('\', '/')
            }
        }
        
        # Infer tags
        $tags = @($exId)
        if ($exId -like '*gui*' -or $exId -like '*charts*' -or $exId -like '*gallery*') { $tags += 'gui' }
        if ($exId -like '*net*' -or $exId -like '*http*') { $tags += 'network' }
        if ($exId -like '*db*' -or $exId -like '*sql*') { $tags += 'database' }
        if ($exId -like '*audio*') { $tags += 'media' }
        if ($exId -like '*format*' -or $exId -like '*crypto*') { $tags += 'data' }
        
        $exampleEntries += [PSCustomObject]@{
            id = $exId
            name = $title
            description = $desc
            entry = $entry
            readme = "examples/$exId/README.md"
            tags = $tags
        }
    }
}

$examplesJsonPath = Join-Path $OutDir 'examples-catalog.json'
$exampleEntries | ConvertTo-Json -Depth 5 | Set-Content $examplesJsonPath -Encoding UTF8
$exCount = $exampleEntries.Count
Write-Output "KNOWLEDGE_EXAMPLES_OK -> $examplesJsonPath ($exCount examples)"
