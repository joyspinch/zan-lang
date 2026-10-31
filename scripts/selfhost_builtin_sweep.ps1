# selfhost_builtin_sweep.ps1 -- measure how much of the C host's builtin
# table (src/compiler/builtin_api.c) the self-hosted compiler can lower.
#
# For every member of every builtin class a tiny probe program is generated
# and compiled with the self-hosted compiler (gen1). The diagnostic tells
# whether the member lowers:
#   "not supported by the self-hosted compiler yet" -> missing
#   "is a built-in type and cannot be used as a value" -> the member is a
#       builtin *property* the self-host does not lower
#   anything else -> the probe itself is wrong (not a self-host gap)
#
# The probes are only compiled, never run: the argument values only have to
# type-check. The result is the capability matrix in docs/selfhost-builtins.md.
#
# Usage:
#   scripts\selfhost_builtin_sweep.ps1 [-Gen1 <gen1.exe>] [-Out <report.txt>]
param(
    [string]$Gen1 = "",
    [string]$Out = "",
    [int]$Jobs = 6
)
$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot\..
$root = (Get-Location).Path
if ($Gen1 -eq "") { $Gen1 = "$root\build\selfhost_diff_gen1.exe" }
if ($Out -eq "") { $Out = "$root\build\selfhost_builtin_matrix.txt" }
if (-not (Test-Path $Gen1)) {
    Write-Host "gen1 not found: $Gen1 (build it with: cmake --build build --target zanrt_selfhost_diff_bundle)"
    exit 1
}
$api = "$root\src\compiler\builtin_api.c"
$work = "$root\build\selfhost_sweep"
$want = @('int', 'long', 'string', 'List', 'Dict', 'StringBuilder', 'Console',
    'Math', 'Convert', 'String', 'File', 'Directory', 'Path', 'Environment',
    'Task', 'BitOperations', 'NativeMemory')

# ---- parse the builtin table ------------------------------------------------
$txt = Get-Content $api -Raw
$arrays = @{}
$re = [regex]'(?s)static const zan_builtin_member_t\s+(members_\w+)\[\]\s*=\s*\{(.*?)\n\};'
foreach ($m in $re.Matches($txt)) {
    $list = @()
    $me = [regex]'\{\s*"([^"]+)"\s*,\s*''(\w)''\s*,\s*"([^"]*)"\s*\}'
    foreach ($e in $me.Matches($m.Groups[2].Value)) {
        $list += [pscustomobject]@{ Name = $e.Groups[1].Value; Kind = $e.Groups[2].Value; Sig = $e.Groups[3].Value }
    }
    $arrays[$m.Groups[1].Value] = $list
}
$classes = @()
foreach ($m in ([regex]'BT\("([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(\d)\s*,\s*(members_\w+)\)').Matches($txt)) {
    if ($want -notcontains $m.Groups[1].Value) { continue }
    $classes += [pscustomobject]@{
        Type = $m.Groups[1].Value; Pub = $m.Groups[2].Value
        Static = ($m.Groups[4].Value -eq '1'); Arr = $m.Groups[5].Value
    }
}

# ---- probe generation -------------------------------------------------------
function Arg-Of([string]$t) {
    switch -Regex ($t) {
        '^(double|float)$' { return '1.5' }
        '^(int|long|short|byte|sbyte|uint|ulong|ushort|object|nint|char)$' { return '3' }
        '^bool$' { return 'true' }
        '^string$' { return '"x"' }
        '^List<.*>$' { return 'new List<string>()' }
        '^Dictionary<.*>$' { return 'new Dictionary<string, int>()' }
        '^object\[\]$' { return 'new object[1]' }
        '^byte\[\]$' { return 'new byte[1]' }
        '^float\[\]$' { return 'new float[1]' }
        '^Vector128$' { return 'Vector128.Zero' }
        '^Vector256$' { return 'Vector256.Zero' }
        '^ConsoleColor$' { return 'ConsoleColor.Red' }
        default { return $null }
    }
}
function Receiver-Of([string]$t) {
    switch ($t) {
        'int' { return '3' }
        'long' { return '3' }
        'short' { return '3' }
        'byte' { return '3' }
        'sbyte' { return '3' }
        'uint' { return '3' }
        'ulong' { return '3' }
        'ushort' { return '3' }
        'float' { return '1.5' }
        'double' { return '1.5' }
        'bool' { return 'true' }
        'char' { return "'a'" }
        'nint' { return '0' }
        'object' { return '1' }
        'string' { return '"x"' }
        'List' { return 'new List<int>()' }
        'Dict' { return 'new Dictionary<string, int>()' }
        'StringBuilder' { return 'new StringBuilder()' }
        default { return $null }
    }
}
function Params-Of([string]$sig) {
    $i = $sig.IndexOf('(')
    if ($i -lt 0) { return @() }
    $j = $sig.LastIndexOf(')')
    if ($j -le $i) { return @() }
    $inner = $sig.Substring($i + 1, $j - $i - 1)
    if ($inner.Trim() -eq '') { return @() }
    $ps = @()
    foreach ($p in $inner.Split(',')) {
        $p = $p.Trim()
        if ($p -match '^params\s') { continue }
        $parts = $p.Split(' ')
        $ps += $parts[0]   # "T item" -> the type is the first token
    }
    return $ps
}

New-Item -ItemType Directory -Force -Path $work | Out-Null
Get-ChildItem $work -Filter *.zan -ErrorAction SilentlyContinue | Remove-Item -Force
$probes = @()
$idx = 0
foreach ($c in $classes) {
    if (-not $arrays.ContainsKey($c.Arr)) { continue }
    foreach ($mem in $arrays[$c.Arr]) {
        $idx++
        $recv = $null
        if (-not $c.Static) {
            $recv = Receiver-Of $c.Type
            if ($null -eq $recv) { continue }
        }
        $expr = $null
        if ($mem.Kind -eq 'P') {
            $expr = if ($c.Static) { "$($c.Pub).$($mem.Name)" } else { "$recv.$($mem.Name)" }
        } else {
            $args = @()
            $skip = $false
            foreach ($p in (Params-Of $mem.Sig)) {
                $a = Arg-Of $p
                if ($null -eq $a) { $skip = $true; break }
                $args += $a
            }
            if ($skip) { continue }
            $call = "$($mem.Name)($($args -join ', '))"
            $expr = if ($c.Static) { "$($c.Pub).$call" } else { "$recv.$call" }
        }
        $src = "$work\p$idx.zan"
        $prog = @"
using System;
using System.Collections.Generic;

class Program {
    static void Main() {
        $expr;
    }
}
"@
        [System.IO.File]::WriteAllText($src, $prog, (New-Object System.Text.UTF8Encoding($false)))
        $probes += [pscustomobject]@{ Idx = $idx; Cls = $c.Pub; Member = $mem.Name; Expr = $expr; Src = $src }
    }
}
Write-Host "probes: $($probes.Count)"

# ---- run the self-host over the probes, a few at a time ---------------------
# static partitioning: each worker takes a slice of the probe list
$chunks = @()
for ($i = 0; $i -lt $Jobs; $i++) { $chunks += , @() }
for ($i = 0; $i -lt $probes.Count; $i++) { $chunks[$i % $Jobs] += $probes[$i] }
$results = @{}
$workers = @()
$hasThreadJob = [bool](Get-Command Start-ThreadJob -ErrorAction SilentlyContinue)
if ($hasThreadJob) {
    for ($w = 0; $w -lt $Jobs; $w++) {
        $chunk = $chunks[$w]
        $workers += Start-ThreadJob -ScriptBlock {
            param($gen1, $work, $chunk)
            $res = @{}
            foreach ($p in $chunk) {
                $err = & $gen1 "$work\s$($p.Idx).ll" "$work\p$($p.Idx).zan" 2>&1 | Out-String
                $rc = $LASTEXITCODE
                $msg = ''
                if ($rc -ne 0) {
                    $line = ($err -split "`n" | Where-Object { $_ -match 'error' } | Select-Object -First 1)
                    if ($null -ne $line) {
                        $msg = (($line -replace '\x1b\[[0-9;]*m', '') -replace '.*error:\s*', '').Trim()
                    } else { $msg = 'rejected' }
                }
                $res[$p.Idx] = $msg
            }
            $res
        } -ArgumentList $Gen1, $work, $chunk -ThrottleLimit $Jobs
    }
    $null = Wait-Job -Job $workers
    foreach ($r in (Receive-Job -Job $workers)) {
        if ($null -eq $r) { continue }
        foreach ($k in $r.Keys) { $results[$k] = $r[$k] }
    }
    Remove-Job -Job $workers
} else {
    foreach ($p in $probes) {
        $err = & $Gen1 "$work\s$($p.Idx).ll" "$work\p$($p.Idx).zan" 2>&1 | Out-String
        $rc = $LASTEXITCODE
        $msg = ''
        if ($rc -ne 0) {
            $line = ($err -split "`n" | Where-Object { $_ -match 'error' } | Select-Object -First 1)
            if ($null -ne $line) {
                $msg = (($line -replace '\x1b\[[0-9;]*m', '') -replace '.*error:\s*', '').Trim()
            } else { $msg = 'rejected' }
        }
        $results[$p.Idx] = $msg
    }
}

# ---- report -----------------------------------------------------------------
$rows = @()
foreach ($p in $probes) {
    $msg = $results[$p.Idx]
    $state = 'ok'
    if ($msg -ne '') {
        if ($msg -match 'not supported by the self-hosted compiler yet') { $state = 'missing' }
        elseif ($msg -match 'built-in type and cannot be used as a value') { $state = 'builtin-property' }
        else { $state = 'probe-bad' }
    }
    $rows += [pscustomobject]@{ Cls = $p.Cls; Member = $p.Member; State = $state; Note = $msg; Expr = $p.Expr }
}
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("self-host coverage of the C host builtin table (src/compiler/builtin_api.c)")
[void]$sb.AppendLine("measurement only -- the probes are compiled, never run")
[void]$sb.AppendLine("")
foreach ($g in ($rows | Group-Object Cls)) {
    $ok = ($g.Group | Where-Object { $_.State -eq 'ok' }).Count
    $miss = ($g.Group | Where-Object { $_.State -eq 'missing' }).Count
    $prop = ($g.Group | Where-Object { $_.State -eq 'builtin-property' }).Count
    $bad = ($g.Group | Where-Object { $_.State -eq 'probe-bad' }).Count
    [void]$sb.AppendLine("== $($g.Name): ok=$ok missing=$miss builtin-property=$prop probe-bad=$bad")
    foreach ($r in $g.Group) {
        $tag = switch ($r.State) { 'ok' { '  ok  ' } 'missing' { ' MISS ' } 'builtin-property' { ' PROP ' } default { ' ???  ' } }
        [void]$sb.AppendLine("$tag $($r.Cls).$($r.Member)   $($r.Expr)   [$($r.Note)]")
    }
    [void]$sb.AppendLine("")
}
[System.IO.File]::WriteAllText($Out, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Host $Out
$rows | Group-Object State | ForEach-Object { Write-Host "$($_.Name): $($_.Count)" }
