#Requires -Version 7.0
# Run: pwsh -NoProfile -File tests/analyze_request_profile_selftest.ps1 [-Large]
[CmdletBinding()]
param([switch]$Large)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$analyzer = Join-Path $root 'scripts/analyze_request_profile.ps1'
$fixture = Join-Path $root ('_scratch/request-profile-selftest-' + [guid]::NewGuid().ToString('N'))

function Assert-Equal($Actual, $Expected, [string]$Context) {
    if ($Actual -cne $Expected) { throw "$Context expected '$Expected', got '$Actual'." }
}

function New-Request([int]$Id, [string]$Route, [int]$Elapsed, [object[]]$Stages) {
    @{
        id = $Id; protocol = 'HTTP/1.1'; method = 'GET'; route = $Route; action = 'Handle'
        start_us = 1000L + $Id * 100; elapsed_us = $Elapsed; status = 200; outcome = 'ok'
        omitted_stages = 0; stages = $Stages
    }
}

function Test-StageReport {
    $first = New-Request 1 '/one' 100 @(
        @{ name = 'cross'; offset_us = 30; elapsed_us = 30; cpu = -1 }
        @{ name = 'dispatch'; offset_us = -5; elapsed_us = 110; cpu = 40; allocated_bytes = 70 }
        @{ name = 'query'; offset_us = 10; elapsed_us = 30; cpu = 8; allocated_bytes = 0 }
        @{ name = 'deep'; offset_us = 34; elapsed_us = 3; cpu = $null; allocated_bytes = 5 }
        @{ name = 'query'; offset_us = 45; elapsed_us = 10; cpu = 0; allocated_bytes = 40 }
        @{ name = 'tie1'; offset_us = 70; elapsed_us = 10 }
        @{ name = 'tie2'; offset_us = 70; elapsed_us = 10 }
        @{ name = 'zero'; offset_us = 60; elapsed_us = 0 }
        @{ name = 'open'; offset_us = 85; elapsed_us = $null; cpu = 3; failure = 'Example' }
        @{ name = 'invalid'; offset_us = 88; elapsed_us = -1; allocated_bytes = -1 }
        @{ name = 'outside'; offset_us = 110; elapsed_us = 3 }
    )
    $first.omitted_stages = 2
    $first.resources = @{
        cpu_unit = 'cycles'; cpu = 70; allocated_bytes = 150; unfinished_tasks = 0
        stage_resources = $true; residual_cpu = 19; residual_allocated_bytes = 35
    }
    $first.waits = @(@{ kind = 'read'; file = 'example.zan'; line = 5; column = 2; offset_us = 10; elapsed_us = 40 })
    $first.execution_slices = @(@{ tid = 3; start_us = 1110; elapsed_us = 10; cpu = 3; allocated_bytes = 0 })
    $second = New-Request 2 '/one' 30 @(
        @{ name = 'dispatch'; offset_us = 5; elapsed_us = 5; cpu = 3; allocated_bytes = $null }
        @{ name = 'query'; offset_us = 10; elapsed_us = 8; allocated_bytes = 2 }
    )
    $second.resources = @{
        cpu_unit = 'ns'; cpu = 3; allocated_bytes = 2; unfinished_tasks = 1
        stage_resources = $true; residual_cpu = 0; residual_allocated_bytes = 0
    }
    $empty = New-Request 3 '/one' 20 @()
    $legacy = New-Request 4 '/two' 20 @(
        @{ name = 'dispatch'; offset_us = 0; elapsed_us = 10 }
        @{ name = 'query'; offset_us = 10; elapsed_us = 2 }
    )
    $retroactive = New-Request 5 '/one' 4 @(@{ name = 'retroactive'; offset_us = 0; elapsed_us = 4 })
    $retroactive.resources = @{ cpu_unit = 'cycles'; cpu = 10; allocated_bytes = 12; stage_resources = $true }
    $unitless = New-Request 6 '/unitless' 6 @(@{ name = 'unitless'; offset_us = 0; elapsed_us = 6; cpu = 10; allocated_bytes = 9 })
    $page = @{
        pid = 42; process_start_unix_ms = 1750000000000; clock_mono_us = 100; clock_unix_ms = 1760000000000
        completed = 6; overwritten = 0; next_before = 0
        requests = @($unitless, $retroactive, $legacy, $empty, $second, $first)
    }
    $pageJson = ConvertTo-Json -InputObject $page -Compress -Depth 8
    $snapshotPath = Join-Path $fixture 'page.json'
    $duplicatePath = Join-Path $fixture 'duplicate.jsonl'
    $tracePath = Join-Path $fixture 'trace.json'
    $samplePath = Join-Path $fixture 'cpu.jsonl'
    [IO.File]::WriteAllText($snapshotPath, $pageJson)
    [IO.File]::WriteAllText($duplicatePath, "`n$pageJson`n")
    [IO.File]::WriteAllText($samplePath, '{"pid":42,"process_start_unix_ms":1750000000000,"tid":3,"mono_us":1115,"stack":["Leaf","Root"]}')
    $report = & $analyzer -SnapshotPath @($snapshotPath, $duplicatePath) -TracePath $tracePath `
        -RequestKey '100:42:1' -CpuSamplePath $samplePath | ConvertFrom-Json -AsHashtable
    Assert-Equal $report.totals.observed_requests 6 'unique requests'
    Assert-Equal $report.totals.duplicate_rows 6 'duplicate requests'
    Assert-Equal $report.totals.open_stages 1 'global open stages'
    Assert-Equal $report.totals.invalid_stage_durations 1 'global invalid stages'
    Assert-Equal $report.cpu_samples.matched_samples 1 'legacy CPU sample attribution'
    Assert-Equal $report.waits[0].p50_us 40 'legacy wait percentile'
    Assert-Equal $report.waits[0].total_us 40 'cumulative wait time'
    Assert-Equal $report.waits[0].mean_us 40 'mean wait time'

    $one = $report.routes | Where-Object route -CEQ '/one'
    $two = $report.routes | Where-Object route -CEQ '/two'
    Assert-Equal $one.count 4 'route requests'
    Assert-Equal $one.error_requests 1 'route errors'
    $wall = $one.stage_wall
    Assert-Equal $wall.request_us 154 'request wall'
    Assert-Equal $wall.covered_us 117 'covered wall'
    Assert-Equal $wall.exclusive_us 100 'exclusive wall'
    Assert-Equal $wall.ambiguous_us 17 'ambiguous overlap wall'
    Assert-Equal $wall.uncovered_us 37 'uncovered wall'
    Assert-Equal $wall.requests_with_ambiguous_wall 1 'requests with ambiguous wall'
    Assert-Equal $wall.omitted_stages 2 'omitted stages'
    Assert-Equal $wall.requests_with_omitted_stages 1 'requests with omissions'
    Assert-Equal $wall.requests_without_stages 1 'requests without stages'
    Assert-Equal $wall.requests_with_open_stages 1 'requests with open stages'
    Assert-Equal $wall.requests_with_invalid_stage_durations 1 'requests with invalid stages'
    Assert-Equal ($one.stages | Measure-Object -Property exclusive_total_us -Sum).Sum 100 'sum of stage-exclusive walls'
    Assert-Equal ($wall.exclusive_us + $wall.ambiguous_us + $wall.uncovered_us) $wall.request_us 'request wall partition'

    $query = $one.stages | Where-Object stage -CEQ 'query'
    Assert-Equal $query.occurrences 3 'query calls'
    Assert-Equal $query.requests 2 'requests with query'
    Assert-Equal $query.error_requests 1 'error requests with query'
    Assert-Equal $query.timed_occurrences 3 'timed queries'
    Assert-Equal $query.request_outcomes[0].requests 2 'query outcome requests'
    Assert-Equal $query.p50_us 10 'query inclusive percentile'
    Assert-Equal $query.inclusive_total_us 48 'query inclusive sum'
    Assert-Equal $query.inclusive_mean_us 16 'query inclusive mean'
    Assert-Equal $query.exclusive_total_us 38 'query exclusive sum'
    Assert-Equal ([math]::Round($query.exclusive_mean_us, 3)) 12.667 'query exclusive mean'
    Assert-Equal $query.resources.cpu.Count 1 'query CPU unit count'
    Assert-Equal $query.resources.cpu[0].unit 'cycles' 'query CPU unit'
    Assert-Equal $query.resources.cpu[0].total 8 'query CPU total'
    Assert-Equal $query.resources.cpu[0].mean 4 'query CPU mean'
    Assert-Equal $query.resources.cpu[0].sampled_occurrences 2 'query CPU calls including zero'
    Assert-Equal $query.resources.cpu[0].sampled_requests 1 'query requests with CPU'
    Assert-Equal $query.resources.allocated_bytes.total 42 'query allocation total'
    Assert-Equal $query.resources.allocated_bytes.mean 14 'query allocation mean'
    Assert-Equal $query.resources.allocated_bytes.sampled_occurrences 3 'query allocation calls including zero'
    Assert-Equal $query.resources.allocated_bytes.sampled_requests 2 'query requests with allocation'

    $dispatch = $one.stages | Where-Object stage -CEQ 'dispatch'
    Assert-Equal $dispatch.inclusive_total_us 115 'raw inclusive wall before clipping'
    Assert-Equal $dispatch.exclusive_total_us 45 'dispatch clipped exclusive wall'
    Assert-Equal $dispatch.resources.cpu.Count 2 'dispatch CPU unit count'
    Assert-Equal $dispatch.resources.cpu[1].unit 'ns' 'dispatch CPU unit ns'
    Assert-Equal $dispatch.resources.cpu[1].total 3 'dispatch CPU ns total'
    Assert-Equal $dispatch.resources.allocated_bytes.sampled_occurrences 1 'dispatch null allocation excluded'
    Assert-Equal ($one.stages | Where-Object stage -CEQ 'deep').exclusive_total_us 3 'innermost in crossing interval'
    Assert-Equal ($one.stages | Where-Object stage -CEQ 'tie1').exclusive_total_us 0 'exact tie gets no exclusive wall'
    Assert-Equal ($one.stages | Where-Object stage -CEQ 'zero').timed_occurrences 1 'zero duration is timed'
    Assert-Equal ($one.stages | Where-Object stage -CEQ 'outside').exclusive_total_us 0 'out-of-request exclusive wall'
    $open = $one.stages | Where-Object stage -CEQ 'open'
    Assert-Equal $open.inclusive_total_us $null 'open stage has no timed total'
    Assert-Equal $open.exclusive_mean_us $null 'open stage has no timed mean'
    Assert-Equal $open.failures[0].failure 'Example' 'legacy stage failure'
    Assert-Equal $open.resources.cpu[0].total 3 'resource coverage independent of time coverage'
    Assert-Equal ($one.stages | Where-Object stage -CEQ 'invalid').resources.allocated_bytes $null 'negative allocation excluded'
    Assert-Equal ($one.stages | Where-Object stage -CEQ 'cross').resources.cpu.Count 0 'negative CPU excluded'
    Assert-Equal ($one.stages | Where-Object stage -CEQ 'retroactive').resources.cpu.Count 0 'retroactive stage has no CPU sample'

    Assert-Equal $one.resources.sampled_requests 3 'existing route resource coverage'
    Assert-Equal $one.resources.allocated_bytes 164 'existing route allocation total'
    Assert-Equal $one.resources.residual_cpu[0].total 19 'explicit residual CPU'
    Assert-Equal $one.resources.residual_cpu[0].sampled_requests 1 'unreported residual skipped'
    Assert-Equal $one.resources.residual_cpu[1].total 0 'measured zero residual CPU'
    Assert-Equal $one.resources.residual_allocated_bytes.sampled_requests 2 'residual allocation coverage'
    Assert-Equal $one.resources.residual_allocated_bytes.mean 17.5 'residual allocation mean'

    $legacyQuery = $two.stages | Where-Object stage -CEQ 'query'
    Assert-Equal $legacyQuery.inclusive_total_us 2 'legacy stage wall'
    Assert-Equal $legacyQuery.resources.cpu.Count 0 'legacy stage has no CPU samples'
    Assert-Equal $legacyQuery.resources.allocated_bytes $null 'legacy stage has no allocation total'
    Assert-Equal $two.Contains('resources') $false 'legacy route keeps optional resources absent'
    $globalQuery = $report.stages | Where-Object stage -CEQ 'query'
    Assert-Equal $globalQuery.requests 3 'legacy action grouping crosses routes'
    Assert-Equal $globalQuery.inclusive_total_us 50 'global query inclusive sum'
    $unitlessStage = ($report.routes | Where-Object route -CEQ '/unitless').stages[0]
    Assert-Equal $unitlessStage.resources.cpu.Count 0 'CPU requires a request unit'
    Assert-Equal $unitlessStage.resources.allocated_bytes.total 9 'allocation independent of CPU unit'

    $trace = Get-Content -LiteralPath $tracePath -Raw | ConvertFrom-Json -AsHashtable
    $traceQuery = $trace.traceEvents | Where-Object { $_['cat'] -ceq 'stage' -and $_.name -ceq 'query' } | Sort-Object ts
    Assert-Equal $traceQuery[0].ts 1110 'existing trace clock'
    Assert-Equal $traceQuery[0].dur 30 'existing trace duration'
    Assert-Equal $traceQuery[0].args.cpu_unit 'cycles' 'trace CPU unit'
    Assert-Equal $traceQuery[0].args.allocated_bytes 0 'trace measured zero allocation'
    Assert-Equal $traceQuery[1].args.cpu 0 'trace measured zero CPU'
    $traceCross = $trace.traceEvents | Where-Object { $_['cat'] -ceq 'stage' -and $_.name -ceq 'cross' }
    Assert-Equal $traceCross.args.Contains('cpu') $false 'trace missing CPU remains absent'
}

function Test-ManyRequests {
    $count = 100000
    $pageSize = 1000
    $path = Join-Path $fixture 'many-pages.jsonl'
    $row = ',"method":"GET","route":"/repeat","action":"Repeat","protocol":"HTTP/1.1","elapsed_us":100,"status":200,"outcome":"ok","omitted_stages":0,"resources":{"cpu":39,"cpu_unit":"cycles","allocated_bytes":143,"stage_resources":true,"residual_cpu":7,"residual_allocated_bytes":11},"stages":[{"name":"receive","offset_us":0,"elapsed_us":10,"cpu":5,"allocated_bytes":10},{"name":"dispatch","offset_us":10,"elapsed_us":65,"cpu":20,"allocated_bytes":100},{"name":"query","offset_us":21,"elapsed_us":10,"cpu":5,"allocated_bytes":10},{"name":"query","offset_us":42,"elapsed_us":8,"cpu":0,"allocated_bytes":0},{"name":"send","offset_us":80,"elapsed_us":20,"cpu":2,"allocated_bytes":12}]}'
    $writer = [IO.File]::CreateText($path)
    try {
        for ($last = $count; $last -gt 0; $last -= $pageSize) {
            $first = [math]::Max(1, $last - $pageSize + 1)
            $next = if ($first -eq 1) { 0 } else { $first }
            $writer.Write('{"pid":42,"clock_mono_us":100,"clock_unix_ms":1760000000000,"completed":100000,"overwritten":0,"next_before":')
            $writer.Write($next)
            $writer.Write(',"requests":[')
            for ($id = $last; $id -ge $first; $id--) {
                if ($id -ne $last) { $writer.Write(',') }
                $writer.Write('{"id":')
                $writer.Write($id)
                $writer.Write(',"start_us":')
                $writer.Write(100 + $id * 100)
                $writer.Write($row)
            }
            $writer.WriteLine(']}')
        }
    } finally {
        $writer.Dispose()
    }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $report = & $analyzer -SnapshotPath $path -Top 1 | ConvertFrom-Json -AsHashtable
    $timer.Stop()
    Assert-Equal $report.totals.snapshots 100 'JSONL pages'
    Assert-Equal $report.totals.observed_requests $count 'repeated route request count'
    Assert-Equal $report.totals.unobserved_requests 0 'full repeated capture'
    $route = $report.routes[0]
    Assert-Equal $route.stage_wall.request_us 10000000 'repeated request wall'
    Assert-Equal $route.stage_wall.exclusive_us 9500000 'repeated exclusive wall'
    Assert-Equal $route.stage_wall.uncovered_us 500000 'repeated uncovered wall'
    Assert-Equal $route.stage_wall.ambiguous_us 0 'repeated ambiguous wall'
    $dispatch = $route.stages | Where-Object stage -CEQ 'dispatch'
    Assert-Equal $dispatch.exclusive_total_us 4700000 'repeated dispatch exclusive wall'
    $query = $route.stages | Where-Object stage -CEQ 'query'
    Assert-Equal $query.occurrences 200000 'repeated query calls'
    Assert-Equal $query.requests $count 'requests with repeated query'
    Assert-Equal $query.inclusive_total_us 1800000 'repeated query inclusive wall'
    Assert-Equal $query.exclusive_mean_us 9 'repeated query exclusive mean'
    Assert-Equal $query.resources.cpu[0].mean 2.5 'repeated query mean CPU'
    Assert-Equal $query.resources.allocated_bytes.total 1000000 'repeated query allocated bytes'
    Assert-Equal $route.resources.residual_cpu[0].total 700000 'repeated residual CPU'
    Write-Output ('100,000-request analyzer check passed in {0:N1} s' -f $timer.Elapsed.TotalSeconds)
}

try {
    [void][IO.Directory]::CreateDirectory($fixture)
    Test-StageReport
    Write-Output 'request stage analyzer regression passed'
    if ($Large) { Test-ManyRequests }
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
