# map.ps1 - query the generated .zanmap (repo map, symbol index, call graph)
# 定位只查地图，不读源码。这是"定点"的第一步。
[CmdletBinding(DefaultParameterSetName = 'Find')]
param(
    [Parameter(ParameterSetName = 'Find', Position = 0)]
    [string]$Find,

    [Parameter(ParameterSetName = 'File', Mandatory)]
    [string]$File,

    [Parameter(ParameterSetName = 'Callers', Mandatory)]
    [string]$Callers,

    [Parameter(ParameterSetName = 'Callees', Mandatory)]
    [string]$Callees,

    [int]$Limit = 60,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$MapDir = Join-Path $Root '.zanmap'
$MapTxt = Join-Path $MapDir 'repo.map.txt'
$Graph = Join-Path $MapDir 'callgraph.jsonl'

if (-not (Test-Path $MapTxt)) {
    Write-Error "no .zanmap at $MapDir - generate it:  build\repomap.exe ."
    exit 2
}

function Emit($rows) {
    if ($Json) { $rows | ConvertTo-Json -Depth 4 }
    else { $rows | ForEach-Object { $_ | Format-Table -AutoSize | Out-String -Width 220 } }
}

switch ($PSCmdlet.ParameterSetName) {

    'Find' {
        # symbol or signature search over the repo map; keeps file context per hit
        $hit = @()
        $cur = ''
        $re = if ($Find) { $Find } else { '.' }
        foreach ($line in [System.IO.File]::ReadLines($MapTxt)) {
            if ($line -match '^(\S.*)$') { $cur = $Matches[1]; continue }
            if ($line -match '^\s+(\S.*?)\s+:(\d+)\s*$') {
                $body = $Matches[1]; $ln = [int]$Matches[2]
                if ($body -imatch $re) {
                    $hit += [pscustomobject]@{ file = $cur; line = $ln; symbol = $body }
                    if ($hit.Count -ge $Limit) { break }
                }
            }
        }
        if ($hit.Count -eq 0) { Write-Output "no symbol matches '$Find'"; exit 1 }
        $hit | ForEach-Object { '{0}:{1}  {2}' -f $_.file, $_.line, $_.symbol }
    }

    'File' {
        $want = $File -replace '\\', '/'
        $emit = $false; $rows = @()
        foreach ($line in [System.IO.File]::ReadLines($MapTxt)) {
            if ($line -match '^(\S.*)$') {
                $emit = ($Matches[1] -ieq $want)
                if ($emit) { $rows += [pscustomobject]@{ file = $Matches[1]; line = 0; symbol = '(file)' } }
                continue
            }
            if ($emit -and $line -match '^\s+(\S.*?)\s+:(\d+)\s*$') {
                $rows += [pscustomobject]@{ file = $want; line = [int]$Matches[2]; symbol = $Matches[1] }
                if ($rows.Count -ge $Limit) { break }
            }
        }
        if ($rows.Count -eq 0) { Write-Output "file not in map: $want"; exit 1 }
        $rows | Where-Object { $_.symbol -ne '(file)' } | ForEach-Object { '{0}:{1}  {2}' -f $_.file, $_.line, $_.symbol }
    }

    'Callers' {
        if (-not (Test-Path $Graph)) { Write-Error "no callgraph at $Graph"; exit 2 }
        # 重构后 callgraph 的 id/owner 退了：id 是裸成员名，owner 恒空。
        # 所以 'File.ReadAllText' 和 'ReadAllText' 都按成员名匹配；
        # 代价是不同类的同名成员无法区分——那是索引侧的缺陷，不是查询能补的。
        $id = $Callers
        $member = ($id -split '\.')[-1]
        if ($member -ne $id) { Write-Output "note: callgraph 只用裸成员名，按 '$member' 查（owner 不可用）" }
        $needle = '"' + $member + '"'
        $rows = @()
        $cand = Select-String -Path $Graph -SimpleMatch -Pattern $needle -ErrorAction SilentlyContinue
        foreach ($c in $cand) {
            if ($c.Line -notmatch '"callees"\s*:\s*\[') { continue }
            $arr = [regex]::Match($c.Line, '"callees"\s*:\s*\[([^\]]*)\]').Groups[1].Value
            if ($arr -notmatch [regex]::Escape($needle)) { continue }
            $node = $null
            try { $node = $c.Line | ConvertFrom-Json } catch { continue }
            $rows += [pscustomobject]@{ caller = $node.id; file = $node.file; line = $node.line; call = $node.name }
            if ($rows.Count -ge $Limit) { break }
        }
        if ($rows.Count -eq 0) { Write-Output "no callers of $id"; exit 1 }
        $rows | Sort-Object file, line -Unique | ForEach-Object { '{0}:{1}  {2}  (calls {3})' -f $_.file, $_.line, $_.caller, $id }
    }

    'Callees' {
        if (-not (Test-Path $Graph)) { Write-Error "no callgraph at $Graph"; exit 2 }
        $id = $Callees
        $member = ($id -split '\.')[-1]
        $needle = '"id":"' + $member + '"'
        $row = Select-String -Path $Graph -SimpleMatch -Pattern $needle -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $row) { Write-Output "no node $member"; exit 1 }
        $node = $row.Line | ConvertFrom-Json
        if (-not $node.callees -or $node.callees.Count -eq 0) { Write-Output "$id calls nothing indexed"; exit 0 }
        $node.callees | Select-Object -First $Limit | ForEach-Object { 'calls -> {0}' -f $_ }
    }
}
