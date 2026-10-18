# impact.ps1 - who consumes this symbol / file, grouped by area, across the WHOLE repo.
# 这一条对应历史上最贵的一类漏改：改了 stdlib/包的公共面，模板与示例里的调用者没跟上。
[CmdletBinding(DefaultParameterSetName = 'Symbol')]
param(
    [Parameter(ParameterSetName = 'Symbol', Mandatory, Position = 0)]
    [string]$Symbol,

    [Parameter(ParameterSetName = 'Path', Mandatory)]
    [string[]]$Path,

    [int]$Limit = 40,
    [switch]$Json,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$SkipDirs = @('.git', '_scratch', 'build', 'dist', '.zanmap', '.zan-packages', 'node_modules', 'Testing', 'logs', 'video', 'zan-site', 'deps')

function Get-ZanFiles {
    Get-ChildItem -Recurse -File -Filter *.zan $Root | Where-Object {
        $p = $_.FullName.Substring($Root.Length).TrimStart('\', '/') -replace '\\', '/'
        $top = ($p -split '/')[0]
        $SkipDirs -notcontains $top
    }
}

function Get-Area([string]$rel) {
    # 必须锚定在路径首段：包里也有 src/ 目录（packages/X/src/...），
    # 不锚定就会把整个包的消费方错报成 src-other -> 分组与跨边界判定全错。
    $p = '\' + ($rel -replace '/', '\')
    if ($p -match '^\\src\\compiler\\') { return 'src-compiler' }
    if ($p -match '^\\src\\runtime\\') { return 'src-runtime' }
    if ($p -match '^\\src\\') { return 'src-other' }
    if ($p -match '^\\packages\\') { return 'packages' }
    if ($p -match '^\\templates\\') { return 'templates' }
    if ($p -match '^\\examples\\') { return 'examples' }
    if ($p -match '^\\stdlib\\') { return 'stdlib' }
    if ($p -match '^\\tools\\') { return 'tools' }
    if ($p -match '^\\tests\\') { return 'tests' }
    return 'other'
}

$Files = Get-ZanFiles

function Find-Consumers([string]$member, [string]$excludeRel) {
    # word-ish match: .Member( or Member( -- avoids matching substrings like MemberX
    $pat = '(?<![\w.])' + [regex]::Escape($member) + '\s*\('
    $hits = @()
    foreach ($f in $Files) {
        $rel = $f.FullName.Substring($Root.Length).TrimStart('\', '/')
        if ($excludeRel -and ($rel -ieq $excludeRel)) { continue }
        $m = Select-String -Path $f.FullName -Pattern $pat -ErrorAction SilentlyContinue
        if ($m) {
            foreach ($x in $m) {
                $hits += [pscustomobject]@{
                    area = (Get-Area $rel); file = $rel; line = $x.LineNumber
                    text = $x.Line.Trim()
                }
            }
        }
    }
    return $hits
}

function Report([string]$name, [string]$defRel, $hits) {
    $byArea = $hits | Group-Object area | Sort-Object Name
    $cross = $false
    foreach ($g in $byArea) {
        if ($g.Name -ne (Get-Area $defRel) -and $g.Name -ne 'other') { $cross = $true }
    }
    [pscustomobject]@{
        symbol = $name; definedIn = $defRel; total = $hits.Count
        crossBoundary = $cross
        byArea = @($byArea | ForEach-Object { [pscustomobject]@{ area = $_.Name; count = $_.Count } })
        hits = @($hits | Select-Object -First $Limit)
    }
}

$results = @()

if ($PSCmdlet.ParameterSetName -eq 'Symbol') {
    $member = ($Symbol -split '\.')[-1]
    $results += Report $Symbol '' (Find-Consumers $member '')
}
else {
    foreach ($p in $Path) {
        $rel = $p
        if ([System.IO.Path]::IsPathRooted($p)) { $rel = $p.Substring($Root.Length).TrimStart('\', '/') }
        $rel = $rel -replace '/', '\'
        $f = $Files | Where-Object { $_.FullName.Substring($Root.Length).TrimStart('\', '/') -ieq $rel } | Select-Object -First 1
        if (-not $f) { Write-Warning "not a zan source in repo: $rel"; continue }
        # symbols defined here, from the repo map
        $mapTxt = Join-Path $Root '.zanmap\repo.map.txt'
        $want = $rel -replace '\\', '/'
        $names = @(); $in = $false
        if (Test-Path $mapTxt) {
            foreach ($line in [System.IO.File]::ReadLines($mapTxt)) {
                if ($line -match '^(\S.*)$') { $in = ($Matches[1] -ieq $want); continue }
                if ($in -and $line -match '^\s+.*\b([A-Za-z_][A-Za-z0-9_]*)\s*\(') { $names += $Matches[1] }
            }
        }
        $names = $names | Sort-Object -Unique
        if ($names.Count -eq 0) {
            # no map entry: fall back to the file's own type names
            $txt = Get-Content $f.FullName -Raw
            $names = [regex]::Matches($txt, '\b(?:class|interface|struct|enum)\s+([A-Za-z_]\w*)') | ForEach-Object { $_.Groups[1].Value }
        }
        foreach ($n in ($names | Select-Object -First 12)) {
            $hits = Find-Consumers $n $rel
            if ($hits.Count -gt 0) { $results += Report $n $rel $hits }
        }
    }
}

if ($results.Count -eq 0) { if (-not $Quiet) { Write-Output 'no consumers found' }; exit 0 }

if ($Json) {
    $results | ConvertTo-Json -Depth 5
}
else {
    foreach ($r in $results) {
        $flag = if ($r.crossBoundary) { '  <<< CROSS-BOUNDARY' } else { '' }
        Write-Output ''
        Write-Output ("### {0}   total={1}{2}" -f $r.symbol, $r.total, $flag)
        if ($r.definedIn) { Write-Output ("    def: {0}" -f $r.definedIn) }
        foreach ($a in $r.byArea) { Write-Output ("    {0,-12} {1}" -f $a.area, $a.count) }
        foreach ($h in $r.hits) { Write-Output ("      {0}:{1}" -f $h.file, $h.line) }
    }
}

if ($results | Where-Object { $_.crossBoundary }) { exit 1 } else { exit 0 }
