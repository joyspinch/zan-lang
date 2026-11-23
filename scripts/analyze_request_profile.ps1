#Requires -Version 7.0

<#
.SYNOPSIS
Analyzes exported Zan.Web RequestProfiler.SnapshotJson files offline.
.DESCRIPTION
Writes a JSON report to stdout. Pass snapshot files positionally or as a
PowerShell array. The report covers completed records in the supplied pages,
with duplicate request keys counted once. Requests that never reached the
profiler or were still in flight are outside the completion counters. Empty
route and action labels are preserved for requests whose route was not
identified. For large runs, export up to 1000 records per page and follow
next_before until zero. Pass the pages as a file array or compact each page to
one line in a .jsonl file. A 100,000-request run can be analyzed from 100 full
pages of one worker. Use totals.observed_requests and totals.unobserved_requests
to verify capture coverage.

Use -TracePath to export the slowest observed request to Perfetto, or pass a
request_key from the report to -RequestKey. Trace timestamps are the original
monotonic microseconds. Stage and wait intervals can overlap. Open waits are
shown at their recorded start and excluded from duration percentiles.
Route resource totals cover sampled requests, with CPU totals grouped by unit.
The existing stages array groups by action and stage name across routes. Each
routes[].stages array gives the same statistics within one method, route and
protocol. Wall means use timed occurrences. Inclusive wall time sums recorded
nonnegative durations. Exclusive wall time partitions the request timeline
using observed closed stage intervals, clipped to the request. Each covered
span goes to the sole active call or to the unique call strictly contained in
all other active calls. A span without such a call is ambiguous. Uncovered
spans have no active closed stage. Open, invalid and omitted stages limit the
coverage of this estimate. Waits do not participate in the partition.

Optional stage cpu and allocated_bytes are exclusive resource measurements
already assigned by the profiler. Only nonnegative samples count, including
measured zero. CPU uses the containing request's resources.cpu_unit. Resource
means use sampled occurrences, and the report counts sampled requests as well.
Optional request resources.residual_cpu and resources.residual_allocated_bytes
are summarized under the corresponding route resource fields. Their means use
sampled requests. The analyzer reads the reported residuals directly.

Pass summary.json files from profile_process.ps1 to -ProcessSummaryPath for
approximate UTC window overlap by PID. The profiler's clock anchor is sampled
once at millisecond resolution. Process counters are whole-window measurements.

CPU attribution uses only exported execution_slices. Each entry has integer
OS tid, absolute start_us, elapsed_us, cpu and allocated_bytes. Positive
elapsed_us forms the interval [start_us, start_us + elapsed_us). The request
may move between threads. Its wall time, stages and waits do not establish CPU
ownership. Overlapping slices on the same thread are ambiguous, even within one
request. Samples with no indexed slice are unmatched. Missing or zero snapshot
process_start_unix_ms makes the page ineligible for attribution. Page exports
of the same profiler should use a consistent process start value for deduplication.
Omitted slices are reported as coverage counters because their time and thread
are unknown. Trace execution events retain the original TID in args.tid and use
resources.cpu_unit as the unit for args.cpu.

CPU sample counts are unweighted. Per-route (method, route, protocol) and
selected-request hotspots use stack[0] as the leaf and refer to complete stacks
by zero-based stack_id in cpu_samples.stacks. Each matched input line counts
once. The slowest request is selected by default for CPU analysis and trace.
Samples are streamed. Memory for aggregation grows with distinct matched stacks
and route/stack pairs.
.PARAMETER Top
Maximum number of entries in each of the slowest and error request lists.
.PARAMETER RequestKey
Selects a request for -TracePath, -CpuSamplePath, or both. Takes the request_key
from the report. Known process instances append :process_start_unix_ms to the
legacy clock_mono_us:pid:id key. The legacy key also works if unique. Append
:0 to select a record without a known process start when the alias is ambiguous.
.PARAMETER CpuSamplePath
UTF-8 JSONL with one sample per nonblank line. For example:
{"pid":412,"process_start_unix_ms":1760000000123,"tid":28,"mono_us":125001,"stack":["Leaf","Root"]}
Use integer JSON numbers (positive Int32 PID, positive Int64 OS TID, Int64
process start, Int64 mono_us) and a nonempty array of nonempty strings in
leaf-to-root order. A process start of zero yields unmatched samples. The
collector and snapshots must use the same host,
process-start timestamp precision and raw monotonic clock. Within each
(pid, process_start_unix_ms, tid), mono_us must be nondecreasing. Threads may
be interleaved. Invalid rows stop analysis with their file and line number.
.EXAMPLE
./scripts/analyze_request_profile.ps1 -SnapshotPath @('worker-a.json', 'worker-b.json') > report.json
.EXAMPLE
./scripts/analyze_request_profile.ps1 -SnapshotPath 'page.json' -TracePath 'request.trace.json' -RequestKey '125000:412:7'
.EXAMPLE
./scripts/analyze_request_profile.ps1 -SnapshotPath 'pages.jsonl' -CpuSamplePath 'cpu.jsonl' -RequestKey '125000:412:7:1760000000123'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0, ValueFromRemainingArguments)]
    [ValidateNotNullOrEmpty()]
    [string[]]$SnapshotPath,

    [ValidateRange(1, 10000)]
    [int]$Top = 20,

    [ValidateNotNullOrEmpty()]
    [string]$TracePath,

    [ValidatePattern('^[0-9]+:[0-9]+:[0-9]+(:-?[0-9]+)?$')]
    [string]$RequestKey,

    [string[]]$ProcessSummaryPath = @(),

    [ValidateNotNullOrEmpty()]
    [string]$CpuSamplePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($RequestKey -and -not ($TracePath -or $CpuSamplePath)) { throw '-RequestKey requires -TracePath or -CpuSamplePath.' }

function Read-JsonDocument([string]$Path) {
    return [Text.Json.JsonDocument]::Parse([IO.File]::ReadAllText($Path))
}

function Add-Percentiles($Summary, [Collections.Generic.List[long]]$Durations) {
    $Durations.Sort()
    $count = $Durations.Count
    $Summary.p50_us = if ($count) { $Durations[[int][math]::Ceiling(0.50 * $count) - 1] }
    $Summary.p95_us = if ($count) { $Durations[[int][math]::Ceiling(0.95 * $count) - 1] }
    $Summary.p99_us = if ($count) { $Durations[[int][math]::Ceiling(0.99 * $count) - 1] }
}

function Add-RequestToGroup($Groups, $Key, $Request) {
    $group = $null
    if (-not $Groups.TryGetValue($Key, [ref]$group)) {
        $group = @{ durations = [Collections.Generic.List[long]]::new(); error_requests = 0 }
        $Groups.Add($Key, $group)
    }
    $group.durations.Add($Request.elapsed_us)
    if ($Request.is_error) { $group.error_requests++ }
}

function Add-RouteResources($Route, [Text.Json.JsonElement]$Resource, [string]$RequestKey) {
    if ($null -eq $Route['resources']) {
        $Route.resources = [ordered]@{
            sampled_requests = 0L; requests_with_unfinished_tasks = 0L; allocated_bytes = 0D
            cpu = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
        }
    }
    $summary = $Route.resources
    $summary.sampled_requests++
    $unfinished = [Text.Json.JsonElement]::new()
    if ($Resource.TryGetProperty('unfinished_tasks', [ref]$unfinished) -and $unfinished.GetInt64() -gt 0) {
        $summary.requests_with_unfinished_tasks++
    }
    $summary.allocated_bytes += $Resource.GetProperty('allocated_bytes').GetInt64()
    $cpu = $Resource.GetProperty('cpu').GetInt64()
    $unit = $Resource.GetProperty('cpu_unit').GetString()
    if ($cpu -ge 0 -and $unit) {
        $sample = $null
        if (-not $summary.cpu.TryGetValue($unit, [ref]$sample)) {
            $sample = [ordered]@{ unit = $unit; sampled_requests = 0L; total = 0D }
            $summary.cpu.Add($unit, $sample)
        }
        $sample.sampled_requests++
        $sample.total += $cpu
    }
    $residual = [Text.Json.JsonElement]::new()
    if ($unit -and $Resource.TryGetProperty('residual_cpu', [ref]$residual) -and
        $residual.ValueKind -ne [Text.Json.JsonValueKind]::Null -and $residual.GetInt64() -ge 0) {
        $summary['residual'] ??= [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
        Add-ResourceSample $summary.residual $unit $residual.GetInt64() $RequestKey
    }
    if ($Resource.TryGetProperty('residual_allocated_bytes', [ref]$residual) -and
        $residual.ValueKind -ne [Text.Json.JsonValueKind]::Null -and $residual.GetInt64() -ge 0) {
        $summary['residual'] ??= [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
        Add-ResourceSample $summary.residual '' $residual.GetInt64() $RequestKey
    }
}

function Find-StageWallOwner($Stages, [Collections.Generic.List[int]]$Active) {
    $owner = $Active[0]
    $candidate = $Stages[$owner]
    $earliestEnd = $candidate.end_us
    $tied = $false
    for ($i = 1; $i -lt $Active.Count; $i++) {
        $index = $Active[$i]
        $stage = $Stages[$index]
        if ($stage.end_us -lt $earliestEnd) { $earliestEnd = $stage.end_us }
        # A unique innermost interval must have both the latest start and earliest end.
        if ($stage.offset_us -gt $candidate.offset_us -or
            ($stage.offset_us -eq $candidate.offset_us -and $stage.end_us -lt $candidate.end_us)) {
            $owner = $index
            $candidate = $stage
            $tied = $false
        } elseif ($stage.offset_us -eq $candidate.offset_us -and $stage.end_us -eq $candidate.end_us) {
            $tied = $true
        }
    }
    if ($tied -or $candidate.end_us -ne $earliestEnd) { return -1 }
    return $owner
}

function Add-StageWall($Wall, $Request, $Stages, $Totals) {
    $events = [Collections.Generic.List[ValueTuple[long,int]]]::new()
    $open = 0
    $invalid = 0
    for ($i = 0; $i -lt $Stages.Count; $i++) {
        $stage = $Stages[$i]
        if ($null -eq $stage.elapsed_us) { $open++; continue }
        if ($stage.elapsed_us -lt 0) { $invalid++; continue }
        if ($stage.elapsed_us -eq 0) { continue }

        # Keep the original bounds for containment and clip only the sweep events.
        $stage.end_us = [decimal]$stage.offset_us + $stage.elapsed_us
        $start = [math]::Max(0L, $stage.offset_us)
        $end = [math]::Min([decimal]$Request.elapsed_us, $stage.end_us)
        if ($end -le $start) { continue }
        $events.Add([ValueTuple]::Create($start, $i + 1))
        $events.Add([ValueTuple]::Create([long]$end, -($i + 1)))
    }

    $covered = 0L
    $ambiguous = 0L
    if ($events.Count -gt 0) {
        $events.Sort()
        $active = [Collections.Generic.List[int]]::new()
        $previous = 0L
        foreach ($event in $events) {
            $position = $event.Item1
            if ($position -gt $previous -and $active.Count -gt 0) {
                $duration = $position - $previous
                $covered += $duration
                $owner = if ($active.Count -eq 1) { $active[0] } else { Find-StageWallOwner $Stages $active }
                if ($owner -ge 0) { $Stages[$owner].exclusive_us += $duration }
                else { $ambiguous += $duration }
            }
            if ($event.Item2 -gt 0) { $active.Add($event.Item2 - 1) }
            else { $null = $active.Remove(-$event.Item2 - 1) }
            $previous = $position
        }
    }

    $Wall.request_us += $Request.elapsed_us
    $Wall.covered_us += $covered
    $Wall.exclusive_us += $covered - $ambiguous
    $Wall.ambiguous_us += $ambiguous
    $Wall.uncovered_us += $Request.elapsed_us - $covered
    $Wall.omitted_stages += $Request.omitted_stages
    if ($Stages.Count -eq 0) { $Wall.requests_without_stages++ }
    if ($Request.omitted_stages -gt 0) { $Wall.requests_with_omitted_stages++ }
    if ($open -gt 0) { $Wall.requests_with_open_stages++ }
    if ($invalid -gt 0) { $Wall.requests_with_invalid_stage_durations++ }
    if ($ambiguous -gt 0) { $Wall.requests_with_ambiguous_wall++ }
    $Totals.open_stages += $open
    $Totals.invalid_stage_durations += $invalid
}

function Add-ResourceSample($Group, [string]$Unit, [long]$Value, [string]$RequestKey) {
    $sample = $null
    if (-not $Group.TryGetValue($Unit, [ref]$sample)) {
        $sample = @{ unit = $Unit; sampled_occurrences = 0L; sampled_requests = 0L; total = 0D; last_request = $null }
        $Group.Add($Unit, $sample)
    }
    $sample.sampled_occurrences++
    if ($sample.last_request -cne $RequestKey) {
        $sample.last_request = $RequestKey
        $sample.sampled_requests++
    }
    $sample.total += $Value
}

function Add-RequestStages($Groups, $Route, $Request, $Stages, [string]$CpuUnit, $Totals) {
    if ($null -eq $Route['stages']) {
        $Route.stages = [Collections.Generic.Dictionary[ValueTuple[string,string],object]]::new()
        $Route.stage_wall = [ordered]@{
            request_us = 0D; covered_us = 0D; exclusive_us = 0D; ambiguous_us = 0D; uncovered_us = 0D
            omitted_stages = 0L; requests_with_omitted_stages = 0L; requests_without_stages = 0L
            requests_with_open_stages = 0L; requests_with_invalid_stage_durations = 0L; requests_with_ambiguous_wall = 0L
        }
    }
    Add-StageWall $Route.stage_wall $Request $Stages $Totals
    $stageGroups = @($Groups, $Route.stages)
    foreach ($stage in $Stages) {
        $key = [ValueTuple]::Create($Request.action, $stage.name)
        foreach ($dictionary in $stageGroups) {
            $group = $null
            if (-not $dictionary.TryGetValue($key, [ref]$group)) {
                $group = @{
                    durations = [Collections.Generic.List[long]]::new(); last_request = $null
                    occurrences = 0L; requests = 0L; error_requests = 0L; open = 0L; invalid = 0L
                    outcomes = [Collections.Generic.Dictionary[string,long]]::new([StringComparer]::Ordinal)
                    failures = [Collections.Generic.Dictionary[string,long]]::new([StringComparer]::Ordinal)
                    inclusive_total_us = 0D; exclusive_total_us = 0D
                    resources = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
                }
                $dictionary.Add($key, $group)
            }
            $group.occurrences++
            if ($group.last_request -cne $Request.request_key) {
                $group.last_request = $Request.request_key
                $group.requests++
                if ($Request.is_error) { $group.error_requests++ }
                $group.outcomes[$Request.outcome]++
            }
            if ($null -ne $stage.failure) { $group.failures[$stage.failure]++ }
            if ($null -eq $stage.elapsed_us) {
                $group.open++
            } elseif ($stage.elapsed_us -lt 0) {
                $group.invalid++
            } else {
                $group.durations.Add($stage.elapsed_us)
                $group.inclusive_total_us += $stage.elapsed_us
                $group.exclusive_total_us += $stage.exclusive_us
            }
            if ($null -ne $stage.cpu -and $stage.cpu -ge 0 -and $CpuUnit) {
                Add-ResourceSample $group.resources $CpuUnit $stage.cpu $Request.request_key
            }
            if ($null -ne $stage.allocated_bytes -and $stage.allocated_bytes -ge 0) {
                Add-ResourceSample $group.resources '' $stage.allocated_bytes $Request.request_key
            }
        }
    }
}

function Get-ResourceSampleSummary($Sample) {
    [ordered]@{
        sampled_occurrences = $Sample.sampled_occurrences; sampled_requests = $Sample.sampled_requests
        total = $Sample.total; mean = $Sample.total / $Sample.sampled_occurrences
    }
}

function Get-ResourceRows($Samples) {
    # Allocation is always in bytes and shares no CPU unit namespace.
    $allocated = $Samples['']
    [ordered]@{
        cpu = @(foreach ($sample in $Samples.Values | Sort-Object -Property unit -CaseSensitive) {
            if (-not $sample.unit) { continue }
            $row = Get-ResourceSampleSummary $sample
            $row.unit = $sample.unit
            $row
        })
        allocated_bytes = if ($null -ne $allocated) { Get-ResourceSampleSummary $allocated }
    }
}

function Get-StageRows($Groups) {
    $rows = foreach ($entry in $Groups.GetEnumerator()) {
        $group = $entry.Value
        $timed = $group.durations.Count
        $summary = [ordered]@{
            action = $entry.Key.Item1; stage = $entry.Key.Item2; occurrences = $group.occurrences
            requests = $group.requests; error_requests = $group.error_requests
            timed_occurrences = $timed; open_occurrences = $group.open
            invalid_duration_occurrences = $group.invalid
        }
        Add-Percentiles $summary $group.durations
        $summary.request_outcomes = @($group.outcomes.GetEnumerator() | Sort-Object Key -CaseSensitive | ForEach-Object {
            [ordered]@{ outcome = $_.Key; requests = $_.Value }
        })
        $summary.failures = @($group.failures.GetEnumerator() | Sort-Object Key -CaseSensitive | ForEach-Object {
            [ordered]@{ failure = $_.Key; occurrences = $_.Value }
        })
        $summary.inclusive_total_us = if ($timed) { $group.inclusive_total_us }
        $summary.inclusive_mean_us = if ($timed) { $group.inclusive_total_us / $timed }
        $summary.exclusive_total_us = if ($timed) { $group.exclusive_total_us }
        $summary.exclusive_mean_us = if ($timed) { $group.exclusive_total_us / $timed }
        $summary.resources = Get-ResourceRows $group.resources
        $summary
    }
    $rows | Sort-Object -Property action, stage -CaseSensitive
}

function Read-CpuSamples([string]$Path, $Timelines, $RequestRoutes, [int]$SelectedIndex, $SelectedCounts, $Summary) {
    foreach ($timeline in $Timelines.Values) { $timeline.intervals.Sort() }

    $lastTimes = [Collections.Generic.Dictionary[ValueTuple[int,long,long],long]]::new()
    $stackIds = [Collections.Generic.Dictionary[string,int]]::new([StringComparer]::Ordinal)
    $stacks = [Collections.Generic.List[string[]]]::new()
    $stackKey = [Text.StringBuilder]::new()
    $lineNumber = 0L
    $reader = [IO.File]::OpenText($Path)
    try {
        while ($null -ne ($line = $reader.ReadLine())) {
            $lineNumber++
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $document = $null
            try {
                $document = [Text.Json.JsonDocument]::Parse($line)
                $sample = $document.RootElement
                $processId = $sample.GetProperty('pid').GetInt32()
                $processStart = $sample.GetProperty('process_start_unix_ms').GetInt64()
                $tid = $sample.GetProperty('tid').GetInt64()
                $time = $sample.GetProperty('mono_us').GetInt64()
                if ($processId -le 0 -or $tid -le 0) { throw 'PID and OS TID must be positive.' }
                $key = [ValueTuple]::Create($processId, $processStart, $tid)
                $previousTime = 0L
                if ($lastTimes.TryGetValue($key, [ref]$previousTime) -and $time -lt $previousTime) {
                    throw 'mono_us must be nondecreasing for each process instance and TID.'
                }
                $lastTimes[$key] = $time

                $requestIndex = -1
                $ambiguous = $false
                $timeline = $null
                if ($Timelines.TryGetValue($key, [ref]$timeline)) {
                    $intervals = $timeline.intervals
                    $next = $timeline.next
                    # Of the intervals started so far, only the two latest ends
                    # are needed to tell whether zero, one or several are live.
                    while ($next -lt $intervals.Count) {
                        $slice = $intervals[$next]
                        if ($slice.Item1 -gt $time) { break }
                        $next++
                        if ($slice.Item2 -gt $timeline.latest_end) {
                            $timeline.second_end = $timeline.latest_end
                            $timeline.latest_end = $slice.Item2
                            $timeline.request_index = $slice.Item3
                        } elseif ($slice.Item2 -gt $timeline.second_end) {
                            $timeline.second_end = $slice.Item2
                        }
                    }
                    $timeline.next = $next
                    if ($time -lt $timeline.second_end) {
                        $ambiguous = $true
                    } elseif ($time -lt $timeline.latest_end) {
                        $requestIndex = $timeline.request_index
                    }
                }

                $stack = $sample.GetProperty('stack')
                if ($stack.GetArrayLength() -eq 0) { throw 'stack must contain at least one frame.' }
                $null = $stackKey.Clear()
                foreach ($frame in $stack.EnumerateArray()) {
                    $name = $frame.GetString()
                    if ([string]::IsNullOrEmpty($name)) { throw 'Stack frames must be nonempty strings.' }
                    if ($requestIndex -ge 0) {
                        # A length prefix preserves frame boundaries for arbitrary symbols.
                        $null = $stackKey.Append($name.Length).Append(':').Append($name)
                    }
                }
                $Summary.total_samples++
                if ($ambiguous) {
                    $Summary.ambiguous_samples++
                } elseif ($requestIndex -lt 0) {
                    $Summary.unmatched_samples++
                } else {
                    $Summary.matched_samples++
                    $stackId = 0
                    $text = $stackKey.ToString()
                    if (-not $stackIds.TryGetValue($text, [ref]$stackId)) {
                        $stackId = $stacks.Count
                        $stackIds.Add($text, $stackId)
                        $stacks.Add(@(foreach ($frame in $stack.EnumerateArray()) { $frame.GetString() }))
                    }
                    $route = $RequestRoutes[$requestIndex]
                    $counts = $route['cpu_sample_counts']
                    if ($null -eq $counts) {
                        $counts = [Collections.Generic.Dictionary[int,long]]::new()
                        $route.cpu_sample_counts = $counts
                    }
                    $counts[$stackId]++
                    if ($requestIndex -eq $SelectedIndex) { $SelectedCounts[$stackId]++ }
                }
            } catch {
                throw "Invalid CPU sample '${Path}:$lineNumber' ($($_.Exception.Message))."
            } finally {
                if ($null -ne $document) { $document.Dispose() }
            }
        }
    } finally {
        $reader.Dispose()
    }
    $Summary.stacks = $stacks.ToArray()
}

function Get-CpuHotspots($Counts, $Stacks) {
    $total = 0L
    $leaves = [Collections.Generic.Dictionary[string,long]]::new([StringComparer]::Ordinal)
    $stackRows = @(if ($null -ne $Counts) {
        foreach ($entry in $Counts.GetEnumerator()) {
            $total += $entry.Value
            $leaves[$Stacks[$entry.Key][0]] += $entry.Value
            [ordered]@{ stack_id = $entry.Key; samples = $entry.Value }
        }
    })
    $leafRows = @(foreach ($entry in $leaves.GetEnumerator()) {
        [ordered]@{ frame = $entry.Key; samples = $entry.Value }
    })
    return [ordered]@{
        matched_samples = $total
        leaf_hotspots = @($leafRows | Sort-Object -Property @{ Expression = 'samples'; Descending = $true }, frame -CaseSensitive)
        stacks = @($stackRows | Sort-Object -Property @{ Expression = 'samples'; Descending = $true }, stack_id)
    }
}

function Add-TraceTrack($Trace, [long]$Tid, [string]$Name) {
    if ($Tid -eq 0) {
        while ($Trace.used_tids.Contains($Trace.next_tid)) { $Trace.next_tid++ }
        $Tid = $Trace.next_tid
        $Trace.next_tid++
    }
    $Trace.events.Add(@{ ph = 'M'; name = 'thread_name'; pid = $Trace.pid; tid = $Tid; args = @{ name = $Name } })
    return $Tid
}

function Add-TraceIntervals($Trace, $Intervals, [long]$OsTid = 0) {
    $laneEnds = [Collections.Generic.List[object]]::new()
    $laneTids = [Collections.Generic.List[long]]::new()
    # Complete events on a track must be disjoint or nested. Sort outer slices
    # first at equal starts, then use another track only for a crossing slice.
    $ordered = $Intervals | Sort-Object -Property @{ Expression = { $_.ts } },
        @{ Expression = { $_.end }; Descending = $true }, @{ Expression = { $_.index } }
    foreach ($slice in $ordered) {
        for ($lane = 0; $lane -lt $laneEnds.Count; $lane++) {
            $ends = $laneEnds[$lane]
            while ($ends.Count -gt 0 -and $ends.Peek() -le $slice.ts) { $null = $ends.Pop() }
            if ($ends.Count -eq 0 -or $slice.end -le $ends.Peek()) { break }
        }
        if ($lane -eq $laneEnds.Count) {
            $laneEnds.Add([Collections.Generic.Stack[long]]::new())
            $tid = if (-not $OsTid) { Add-TraceTrack $Trace 0 "Request lane $($lane + 1)" }
                elseif ($lane -eq 0) { Add-TraceTrack $Trace $OsTid "OS thread $OsTid" }
                else { Add-TraceTrack $Trace 0 "OS thread $OsTid (overlap lane $($lane + 1))" }
            $laneTids.Add($tid)
        }
        if ($slice.dur -gt 0) { $laneEnds[$lane].Push($slice.end) }
        $Trace.events.Add(@{
            ph = 'X'; pid = $Trace.pid; tid = $laneTids[$lane]; name = $slice.name; cat = $slice.cat
            ts = $slice.ts; dur = $slice.dur; args = $slice.args
        })
    }
}

function Write-RequestTrace([string]$Path, $Request, $Stages, $Waits, $ExecutionSlices, [long]$OmittedSlices, [string]$CpuUnit) {
    $events = [Collections.Generic.List[object]]::new()
    $intervals = [Collections.Generic.List[object]]::new()
    $stageStarts = [Collections.Generic.List[object]]::new()
    $waitStarts = [Collections.Generic.List[object]]::new()
    $trace = @{
        events = $events; pid = $Request.pid; next_tid = 1L
        used_tids = [Collections.Generic.HashSet[long]]::new()
    }
    $processId = $Request.pid
    $events.Add(@{ ph = 'M'; name = 'process_name'; pid = $processId; tid = 0; args = @{ name = "Zan.Web PID $processId" } })

    $intervals.Add(@{
        name = "$($Request.method) $($Request.route)".Trim(); cat = 'request'; index = -1
        ts = $Request.start_us; dur = $Request.elapsed_us
        end = $Request.start_us + $Request.elapsed_us; args = $Request
    })
    if (-not $intervals[0].name) { $intervals[0].name = "Request $($Request.id)" }

    foreach ($stage in $Stages) {
        $argsForEvent = @{ offset_us = $stage.offset_us }
        if ($null -ne $stage.failure) { $argsForEvent.failure = $stage.failure }
        if ($null -ne $stage.cpu -and $stage.cpu -ge 0 -and $CpuUnit) {
            $argsForEvent.cpu = $stage.cpu
            $argsForEvent.cpu_unit = $CpuUnit
        }
        if ($null -ne $stage.allocated_bytes -and $stage.allocated_bytes -ge 0) {
            $argsForEvent.allocated_bytes = $stage.allocated_bytes
        }
        $slice = @{
            name = $stage.name; cat = 'stage'; index = $intervals.Count
            ts = $Request.start_us + $stage.offset_us; args = $argsForEvent
        }
        if ($null -eq $stage.elapsed_us -or $stage.elapsed_us -lt 0) {
            if ($null -ne $stage.elapsed_us) { $argsForEvent.recorded_elapsed_us = $stage.elapsed_us }
            $stageStarts.Add($slice)
            continue
        }
        $slice.dur = $stage.elapsed_us
        $slice.end = $slice.ts + $slice.dur
        $intervals.Add($slice)
    }
    foreach ($wait in $Waits) {
        $argsForEvent = @{ file = $wait.file; line = $wait.line; column = $wait.column; offset_us = $wait.offset_us }
        $slice = @{
            name = $wait.kind; cat = 'wait'; index = $intervals.Count
            ts = $Request.start_us + $wait.offset_us; args = $argsForEvent
        }
        if ($null -eq $wait.elapsed_us -or $wait.elapsed_us -lt 0) {
            if ($null -ne $wait.elapsed_us) { $argsForEvent.recorded_elapsed_us = $wait.elapsed_us }
            $waitStarts.Add($slice)
            continue
        }
        $slice.dur = $wait.elapsed_us
        $slice.end = $slice.ts + $slice.dur
        $intervals.Add($slice)
    }

    $executionTracks = [Collections.Generic.Dictionary[long,object]]::new()
    foreach ($slice in $ExecutionSlices) {
        $tid = $slice.args.tid
        $threadIntervals = $null
        if (-not $executionTracks.TryGetValue($tid, [ref]$threadIntervals)) {
            $threadIntervals = [Collections.Generic.List[object]]::new()
            $executionTracks.Add($tid, $threadIntervals)
            $null = $trace.used_tids.Add($tid)
        }
        $threadIntervals.Add($slice)
    }
    Add-TraceIntervals $trace $intervals
    foreach ($track in @(
        @{ name = 'Stage starts without duration'; starts = $stageStarts }
        @{ name = 'Wait starts without duration'; starts = $waitStarts }
    )) {
        if ($track.starts.Count -eq 0) { continue }
        $tid = Add-TraceTrack $trace 0 $track.name
        foreach ($slice in $track.starts) {
            $events.Add(@{ ph = 'i'; s = 't'; pid = $processId; tid = $tid; name = $slice.name; cat = $slice.cat; ts = $slice.ts; args = $slice.args })
        }
    }
    foreach ($entry in $executionTracks.GetEnumerator()) { Add-TraceIntervals $trace $entry.Value $entry.Key }
    $otherData = @{ request_key = $Request.request_key }
    if ($ExecutionSlices.Count -gt 0 -or $OmittedSlices -gt 0) { $otherData.omitted_execution_slices = $OmittedSlices }
    $output = @{ traceEvents = $events.ToArray(); otherData = $otherData }
    ConvertTo-Json -InputObject $output -Depth 8 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Get-ProcessSummaryOverlap([string]$Path, $Workers, $Requests) {
    $resolvedPath = (Resolve-Path -LiteralPath $Path).ProviderPath
    $document = Read-JsonDocument $resolvedPath
    try {
        $summary = $document.RootElement
        $targetPid = $summary.GetProperty('target_pid').GetInt32()
        # JsonElement parses the collector's ISO 8601 timestamps independently of locale.
        $start = $summary.GetProperty('start_utc').GetDateTimeOffset().ToUniversalTime()
        $end = $summary.GetProperty('end_utc').GetDateTimeOffset().ToUniversalTime()
        if ($end -lt $start) { throw 'The UTC capture end precedes the start.' }
    } catch {
        throw "Invalid process summary '$resolvedPath' ($($_.Exception.Message))."
    } finally { $document.Dispose() }

    $matchingWorkers = @($Workers | Where-Object pid -EQ $targetPid)
    $anchors = [Collections.Generic.Dictionary[ValueTuple[long,long],long]]::new()
    foreach ($worker in $matchingWorkers) {
        $key = [ValueTuple]::Create($worker.clock_mono_us, [long]$worker['process_start_unix_ms'])
        $anchors.Add($key, [DateTime]::UnixEpoch.Ticks + 10000L * $worker.clock_unix_ms)
    }
    $overlapping = 0
    $contained = 0
    foreach ($request in $Requests) {
        if ($request.pid -ne $targetPid -or $start -eq $end) { continue }
        $property = $request.PSObject.Properties['process_start_unix_ms']
        $processStart = if ($null -ne $property) { $property.Value } else { 0L }
        $key = [ValueTuple]::Create($request.clock_mono_us, $processStart)
        $requestStart = $anchors[$key] + 10L * ($request.start_us - $request.clock_mono_us)
        $requestEnd = $requestStart + 10L * $request.elapsed_us
        # Treat a zero-duration request as a point inside the half-open window.
        if ($requestStart -lt $end.UtcTicks -and
            ($requestEnd -gt $start.UtcTicks -or ($request.elapsed_us -eq 0 -and $requestStart -eq $start.UtcTicks))) {
            $overlapping++
            if ($requestStart -ge $start.UtcTicks -and $requestEnd -le $end.UtcTicks) { $contained++ }
        }
    }
    return [ordered]@{
        path = $resolvedPath; target_pid = $targetPid
        start_utc = $start.ToString('o', [Globalization.CultureInfo]::InvariantCulture)
        end_utc = $end.ToString('o', [Globalization.CultureInfo]::InvariantCulture)
        matching_profiler_clocks = @($matchingWorkers | ForEach-Object clock_mono_us)
        overlapping_requests = $overlapping; fully_contained_requests = $contained
        no_coverage = $overlapping -eq 0
    }
}

$snapshotFiles = @($SnapshotPath | ForEach-Object { (Resolve-Path -LiteralPath $_).ProviderPath })
$cpuSampleFile = if ($CpuSamplePath) { (Resolve-Path -LiteralPath $CpuSamplePath).ProviderPath }
if ($TracePath) {
    $TracePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($TracePath)
    $inputFiles = $snapshotFiles + @($ProcessSummaryPath | ForEach-Object { (Resolve-Path -LiteralPath $_).ProviderPath })
    if ($inputFiles -contains $TracePath -or $cpuSampleFile -eq $TracePath) { throw 'The trace output path matches an input file.' }
}

$workers = [Collections.Generic.Dictionary[ValueTuple[long,int,long],object]]::new()
$requests = [Collections.Generic.List[object]]::new()
$routes = [Collections.Generic.Dictionary[ValueTuple[string,string,string],object]]::new()
$protocols = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
$outcomes = [Collections.Generic.Dictionary[ValueTuple[string,string],object]]::new()
$stages = [Collections.Generic.Dictionary[ValueTuple[string,string],object]]::new()
$waits = [Collections.Generic.Dictionary[ValueTuple[string,string,string,int,int,string],object]]::new()
$overallDurations = [Collections.Generic.List[long]]::new()
$selectedRequest = $null
$selectedIndex = -1
$selectedRecordedSlices = 0L
$selectedIndexedSlices = 0L
$selectedOmittedSlices = 0L
$traceStages = $null
$traceWaits = $null
$traceExecutionSlices = $null
$traceCpuUnit = $null
if ($cpuSampleFile) {
    $executionTimelines = [Collections.Generic.Dictionary[ValueTuple[int,long,long],object]]::new()
    $requestRoutes = [Collections.Generic.List[object]]::new()
    $cpuSampleReport = [ordered]@{
        path = $cpuSampleFile; total_samples = 0L; matched_samples = 0L; ambiguous_samples = 0L; unmatched_samples = 0L
        recorded_execution_slices = 0L; indexed_execution_slices = 0L; omitted_execution_slices = 0L
        requests_with_indexed_execution_slices = 0L; requests_with_omitted_execution_slices = 0L
    }
}
$totals = [ordered]@{
    snapshots = 0; empty_snapshots = 0; workers = 0; no_coverage = $true
    exported_rows = 0L; duplicate_rows = 0L; observed_requests = 0L; error_requests = 0L
    completed = 0L; overwritten = 0L; overwritten_unobserved = 0L; retained_unobserved = 0L
    unobserved_requests = 0L; omitted_stages = 0L; requests_with_omitted_stages = 0L
    requests_without_stages = 0L; open_stages = 0L; invalid_stage_durations = 0L
    omitted_waits = 0L
}

& {
    foreach ($file in $snapshotFiles) {
        if ([IO.Path]::GetExtension($file) -ieq '.jsonl') {
            $lineNumber = 0
            foreach ($line in [IO.File]::ReadLines($file)) {
                $lineNumber++
                if (-not [string]::IsNullOrWhiteSpace($line)) {
                    @{ Path = "${file}:$lineNumber"; Text = $line }
                }
            }
        } else {
            @{ Path = $file; Text = [IO.File]::ReadAllText($file) }
        }
    }
} | ForEach-Object {
    $path = $_.Path
    $document = $null
    try {
        $totals.snapshots++
        $document = [Text.Json.JsonDocument]::Parse($_.Text)
        $snapshot = $document.RootElement
        $processId = $snapshot.GetProperty('pid').GetInt32()
        $clockMono = $snapshot.GetProperty('clock_mono_us').GetInt64()
        $clockUnix = $snapshot.GetProperty('clock_unix_ms').GetInt64()
        $value = [Text.Json.JsonElement]::new()
        $processStart = if ($snapshot.TryGetProperty('process_start_unix_ms', [ref]$value)) { $value.GetInt64() } else { 0L }
        $completed = $snapshot.GetProperty('completed').GetInt64()
        $overwritten = $snapshot.GetProperty('overwritten').GetInt64()
        $nextBefore = $snapshot.GetProperty('next_before').GetInt64()
        $rows = $snapshot.GetProperty('requests')
        $length = $rows.GetArrayLength()
        if ($processId -le 0 -or $clockMono -lt 0 -or $overwritten -lt 0 -or $completed -lt $overwritten -or $nextBefore -lt 0) {
            throw 'Invalid profiler identity or completion counters.'
        }

        # Rows are newest completion first. next_before is the last row's
        # completion sequence, or zero once the oldest retained row is reached.
        $firstCompletion = $null
        $lastCompletion = $null
        if ($length -gt 0) {
            $firstCompletion = if ($nextBefore) { $nextBefore } else { $overwritten + 1 }
            $lastCompletion = $firstCompletion + $length - 1
            if ($firstCompletion -le $overwritten -or $lastCompletion -gt $completed) {
                throw 'The page falls outside the retained completion range.'
            }
        }
        $workerKey = [ValueTuple]::Create($clockMono, $processId, $processStart)
        $worker = $null
        if (-not $workers.TryGetValue($workerKey, [ref]$worker)) {
            $worker = @{
                pid = $processId; clock_mono_us = $clockMono; clock_unix_ms = $clockUnix; process_start_unix_ms = $processStart
                completed = 0L; overwritten = 0L; omitted_stages = 0L; duplicates = 0L
                pages = [Collections.Generic.List[object]]::new()
                ids = [Collections.Generic.Dictionary[long,long]]::new()
            }
            $workers.Add($workerKey, $worker)
        }
        if ($worker.clock_unix_ms -ne $clockUnix) { throw 'Conflicting UTC anchors for the same profiler identity.' }
        if ($completed -ge $worker.completed) {
            $worker.completed = $completed
            $worker.overwritten = $overwritten
        }
        $worker.pages.Add([ordered]@{
            path = $path; rows = $length; no_coverage = $length -eq 0; completed = $completed
            overwritten = $overwritten; next_before = $nextBefore
            first_completion = $firstCompletion; last_completion = $lastCompletion
        })
        $totals.exported_rows += $length
        if ($length -eq 0) { $totals.empty_snapshots++ }

        $completion = $lastCompletion
        foreach ($row in $rows.EnumerateArray()) {
            $id = $row.GetProperty('id').GetInt64()
            $added = $worker.ids.TryAdd($id, $completion)
            $completion--
            if (-not $added) {
                $worker.duplicates++
                $totals.duplicate_rows++
                continue
            }

            $legacyKey = '{0}:{1}:{2}' -f $clockMono, $processId, $id
            $instanceKey = "${legacyKey}:$processStart"
            $requestData = [ordered]@{
                request_key = if ($processStart) { $instanceKey } else { $legacyKey }
                clock_mono_us = $clockMono; pid = $processId; id = $id
                method = [string]$row.GetProperty('method').GetString()
                route = [string]$row.GetProperty('route').GetString()
                action = [string]$row.GetProperty('action').GetString()
                protocol = [string]$row.GetProperty('protocol').GetString()
                start_us = $row.GetProperty('start_us').GetInt64()
                elapsed_us = $row.GetProperty('elapsed_us').GetInt64()
                status = $row.GetProperty('status').GetInt32()
                outcome = [string]$row.GetProperty('outcome').GetString()
                omitted_stages = $row.GetProperty('omitted_stages').GetInt64()
                is_error = $false
            }
            if ($processStart) { $requestData.process_start_unix_ms = $processStart }
            $request = [pscustomobject]$requestData
            if ($id -le 0 -or $request.elapsed_us -lt 0 -or $request.omitted_stages -lt 0) {
                throw "Invalid request ID, latency or omitted count for request $($request.request_key)."
            }
            $requestStages = [Collections.Generic.List[object]]::new()
            $hasFailure = $false
            $value = [Text.Json.JsonElement]::new()
            foreach ($stage in $row.GetProperty('stages').EnumerateArray()) {
                $elapsed = if ($stage.TryGetProperty('elapsed_us', [ref]$value) -and
                    $value.ValueKind -ne [Text.Json.JsonValueKind]::Null) { $value.GetInt64() }
                $failure = if ($stage.TryGetProperty('failure', [ref]$value)) { $value.GetString() }
                $cpu = if ($stage.TryGetProperty('cpu', [ref]$value) -and
                    $value.ValueKind -ne [Text.Json.JsonValueKind]::Null) { $value.GetInt64() }
                $allocatedBytes = if ($stage.TryGetProperty('allocated_bytes', [ref]$value) -and
                    $value.ValueKind -ne [Text.Json.JsonValueKind]::Null) { $value.GetInt64() }
                if ($null -ne $failure) { $hasFailure = $true }
                $requestStages.Add(@{
                    name = [string]$stage.GetProperty('name').GetString()
                    offset_us = $stage.GetProperty('offset_us').GetInt64()
                    elapsed_us = $elapsed; failure = $failure; exclusive_us = 0L
                    cpu = $cpu; allocated_bytes = $allocatedBytes
                })
            }
            $request.is_error = $request.status -ge 400 -or $hasFailure -or
                $request.outcome -cin @('aborted', 'application_exception', 'cleanup_exception', 'incomplete_head',
                    'pipeline_exception', 'receive_failed', 'rejected', 'send_failed', 'stream_closed', 'stream_reset')
            if ($request.is_error) { $totals.error_requests++ }
            if ($requestStages.Count -eq 0) { $totals.requests_without_stages++ }
            if ($request.omitted_stages -gt 0) { $totals.requests_with_omitted_stages++ }
            $worker.omitted_stages += $request.omitted_stages
            $totals.omitted_stages += $request.omitted_stages
            $requests.Add($request)
            $overallDurations.Add($request.elapsed_us)

            $routeKey = [ValueTuple]::Create($request.method, $request.route, $request.protocol)
            Add-RequestToGroup $routes $routeKey $request
            $route = $routes[$routeKey]
            $cpuUnit = $null
            if ($row.TryGetProperty('resources', [ref]$value) -and $value.ValueKind -ne [Text.Json.JsonValueKind]::Null) {
                Add-RouteResources $route $value $request.request_key
                $cpuUnit = $value.GetProperty('cpu_unit').GetString()
            }
            if ($row.TryGetProperty('omitted_waits', [ref]$value)) { $totals.omitted_waits += $value.GetInt64() }
            Add-RequestToGroup $protocols $request.protocol $request
            $outcomeKey = [ValueTuple]::Create($request.action, $request.outcome)
            $outcomeGroup = $null
            if (-not $outcomes.TryGetValue($outcomeKey, [ref]$outcomeGroup)) {
                $outcomeGroup = @{ requests = 0; error_requests = 0 }
                $outcomes.Add($outcomeKey, $outcomeGroup)
            }
            $outcomeGroup.requests++
            if ($request.is_error) { $outcomeGroup.error_requests++ }
            Add-RequestStages $stages $route $request $requestStages $cpuUnit $totals
            $requestIndex = $requests.Count - 1
            $selectRequest = ($TracePath -or $cpuSampleFile) -and (
                ($RequestKey -and ($RequestKey -ceq $instanceKey -or $RequestKey -ceq $legacyKey)) -or
                (-not $RequestKey -and ($null -eq $selectedRequest -or $request.elapsed_us -gt $selectedRequest.elapsed_us)))
            $selectTrace = $selectRequest -and $TracePath
            if ($selectRequest) {
                if ($RequestKey -and $null -ne $selectedRequest) {
                    throw "Request key '$RequestKey' matches multiple process instances. Specify process_start_unix_ms as the fourth component (0 for legacy pages)."
                }
                $selectedRequest = $request
                $selectedIndex = $requestIndex
            }
            if ($selectTrace) {
                $traceStages = $requestStages
                $traceWaits = [Collections.Generic.List[object]]::new()
                $traceExecutionSlices = [Collections.Generic.List[object]]::new()
                $traceCpuUnit = $cpuUnit
            }
            if ($cpuSampleFile) { $requestRoutes.Add($route) }
            if ($cpuSampleFile -or $selectTrace) {
                $omittedSlices = if ($row.TryGetProperty('omitted_execution_slices', [ref]$value)) { $value.GetInt64() } else { 0L }
                if ($omittedSlices -lt 0) { throw "Invalid omitted_execution_slices for request $($request.request_key)." }
                $recordedSlices = 0L
                $indexedSlices = 0L
                if ($row.TryGetProperty('execution_slices', [ref]$value) -and $value.ValueKind -ne [Text.Json.JsonValueKind]::Null) {
                    foreach ($slice in $value.EnumerateArray()) {
                        $tid = $slice.GetProperty('tid').GetInt64()
                        $start = $slice.GetProperty('start_us').GetInt64()
                        $elapsed = $slice.GetProperty('elapsed_us').GetInt64()
                        $cpu = $slice.GetProperty('cpu').GetInt64()
                        $allocatedBytes = $slice.GetProperty('allocated_bytes').GetInt64()
                        if ($tid -le 0 -or $elapsed -lt 0 -or $start -gt ([long]::MaxValue - $elapsed)) {
                            throw "Invalid execution slice TID or interval for request $($request.request_key)."
                        }
                        $end = $start + $elapsed
                        $recordedSlices++
                        if ($cpuSampleFile -and $processStart -and $elapsed -gt 0) {
                            $threadKey = [ValueTuple]::Create($processId, $processStart, $tid)
                            $timeline = $null
                            if (-not $executionTimelines.TryGetValue($threadKey, [ref]$timeline)) {
                                $timeline = @{
                                    intervals = [Collections.Generic.List[ValueTuple[long,long,int]]]::new()
                                    next = 0; latest_end = [long]::MinValue; second_end = [long]::MinValue; request_index = -1
                                }
                                $executionTimelines.Add($threadKey, $timeline)
                            }
                            $timeline.intervals.Add([ValueTuple]::Create($start, $end, $requestIndex))
                            $indexedSlices++
                        }
                        if ($selectTrace) {
                            $traceExecutionSlices.Add(@{
                                name = 'Request execution'; cat = 'execution'; index = $traceExecutionSlices.Count
                                ts = $start; dur = $elapsed; end = $end
                                args = @{ tid = $tid; cpu = $cpu; cpu_unit = $cpuUnit; allocated_bytes = $allocatedBytes }
                            })
                        }
                    }
                }
                if ($selectRequest) {
                    $selectedRecordedSlices = $recordedSlices
                    $selectedIndexedSlices = $indexedSlices
                    $selectedOmittedSlices = $omittedSlices
                }
                if ($cpuSampleFile) {
                    $cpuSampleReport.recorded_execution_slices += $recordedSlices
                    $cpuSampleReport.indexed_execution_slices += $indexedSlices
                    $cpuSampleReport.omitted_execution_slices += $omittedSlices
                    $route['recorded_execution_slices'] += $recordedSlices
                    $route['indexed_execution_slices'] += $indexedSlices
                    $route['omitted_execution_slices'] += $omittedSlices
                    if ($indexedSlices -gt 0) { $cpuSampleReport.requests_with_indexed_execution_slices++ }
                    if ($omittedSlices -gt 0) { $cpuSampleReport.requests_with_omitted_execution_slices++ }
                }
            }
            if ($row.TryGetProperty('waits', [ref]$value) -and $value.ValueKind -ne [Text.Json.JsonValueKind]::Null) {
                foreach ($wait in $value.EnumerateArray()) {
                    $fileName = [string]$wait.GetProperty('file').GetString()
                    $line = $wait.GetProperty('line').GetInt32()
                    $column = $wait.GetProperty('column').GetInt32()
                    $kind = [string]$wait.GetProperty('kind').GetString()
                    $elapsed = $null
                    if ($wait.TryGetProperty('elapsed_us', [ref]$value) -and $value.ValueKind -ne [Text.Json.JsonValueKind]::Null) {
                        $elapsed = $value.GetInt64()
                    }
                    $key = [ValueTuple]::Create($request.route, $request.action, $fileName, $line, $column, $kind)
                    $group = $null
                    if (-not $waits.TryGetValue($key, [ref]$group)) {
                        $group = @{ durations = [Collections.Generic.List[long]]::new(); open = 0L; invalid = 0L; elapsed_us = 0L }
                        $waits.Add($key, $group)
                    }
                    if ($null -eq $elapsed) {
                        $group.open++
                    } elseif ($elapsed -lt 0) {
                        $group.invalid++
                    } else {
                        $group.durations.Add($elapsed)
                        $group.elapsed_us += $elapsed
                    }
                    if ($selectTrace) {
                        $traceWaits.Add(@{
                            kind = $kind; file = $fileName; line = $line; column = $column
                            offset_us = $wait.GetProperty('offset_us').GetInt64(); elapsed_us = $elapsed
                        })
                    }
                }
            }
        }
    } catch {
        throw "Invalid request snapshot '$path' ($($_.Exception.Message))."
    } finally {
        if ($null -ne $document) { $document.Dispose() }
    }
}

if ($RequestKey -and $null -eq $selectedRequest) {
    throw "Request key '$RequestKey' was not observed in the input snapshots."
}
if ($cpuSampleFile) {
    $selectedCounts = [Collections.Generic.Dictionary[int,long]]::new()
    Read-CpuSamples $cpuSampleFile $executionTimelines $requestRoutes $selectedIndex $selectedCounts $cpuSampleReport
    $executionTimelines.Clear()
    $requestRoutes.Clear()
    $cpuSampleReport.selected_request = if ($null -ne $selectedRequest) {
        $hotspots = Get-CpuHotspots $selectedCounts $cpuSampleReport.stacks
        $hotspots.request_key = $selectedRequest.request_key
        $hotspots.recorded_execution_slices = $selectedRecordedSlices
        $hotspots.indexed_execution_slices = $selectedIndexedSlices
        $hotspots.omitted_execution_slices = $selectedOmittedSlices
        $hotspots
    }
}

$workerRows = @(foreach ($worker in $workers.Values | Sort-Object -Property pid, clock_mono_us, process_start_unix_ms) {
    $recovered = 0L
    foreach ($sequence in $worker.ids.Values) {
        if ($sequence -le $worker.overwritten) { $recovered++ }
    }
    $overwrittenUnobserved = $worker.overwritten - $recovered
    $retainedUnobserved = $worker.completed - $overwrittenUnobserved - $worker.ids.Count
    if ($overwrittenUnobserved -lt 0 -or $retainedUnobserved -lt 0) {
        throw "Inconsistent completion coverage for PID $($worker.pid), clock $($worker.clock_mono_us)."
    }
    $totals.completed += $worker.completed
    $totals.overwritten += $worker.overwritten
    $totals.overwritten_unobserved += $overwrittenUnobserved
    $totals.retained_unobserved += $retainedUnobserved
    $workerRow = [ordered]@{
        pid = $worker.pid; clock_mono_us = $worker.clock_mono_us; clock_unix_ms = $worker.clock_unix_ms
        observed_requests = $worker.ids.Count; no_coverage = $worker.ids.Count -eq 0
        completed = $worker.completed; overwritten = $worker.overwritten; overwritten_observed = $recovered
        overwritten_unobserved = $overwrittenUnobserved; retained_unobserved = $retainedUnobserved
        unobserved_requests = $worker.completed - $worker.ids.Count
        duplicate_rows = $worker.duplicates; omitted_stages = $worker.omitted_stages
        pages = $worker.pages.ToArray()
    }
    if ($worker.process_start_unix_ms) { $workerRow.process_start_unix_ms = $worker.process_start_unix_ms }
    $workerRow
})
$totals.workers = $workers.Count
$totals.observed_requests = $requests.Count
$totals.no_coverage = $requests.Count -eq 0
$totals.unobserved_requests = $totals.completed - $requests.Count
Add-Percentiles $totals $overallDurations

$routeRows = @(foreach ($entry in $routes.GetEnumerator()) {
    $group = $entry.Value
    $summary = [ordered]@{
        method = $entry.Key.Item1; route = $entry.Key.Item2; protocol = $entry.Key.Item3
        count = $group.durations.Count; error_requests = $group.error_requests
    }
    Add-Percentiles $summary $group.durations
    if ($null -ne $group['resources']) {
        $summary.resources = [ordered]@{
            sampled_requests = $group.resources.sampled_requests
            requests_with_unfinished_tasks = $group.resources.requests_with_unfinished_tasks
            allocated_bytes = $group.resources.allocated_bytes
            cpu = @($group.resources.cpu.Values | Sort-Object -Property unit -CaseSensitive)
        }
        if ($null -ne $group.resources['residual']) {
            $residual = Get-ResourceRows $group.resources.residual
            if ($residual.cpu.Count -gt 0) { $summary.resources.residual_cpu = $residual.cpu }
            if ($null -ne $residual.allocated_bytes) { $summary.resources.residual_allocated_bytes = $residual.allocated_bytes }
        }
    }
    $summary.stage_wall = $group.stage_wall
    $summary.stages = @(Get-StageRows $group.stages)
    if ($cpuSampleFile) {
        $hotspots = Get-CpuHotspots $group['cpu_sample_counts'] $cpuSampleReport.stacks
        $hotspots.recorded_execution_slices = $group.recorded_execution_slices
        $hotspots.indexed_execution_slices = $group.indexed_execution_slices
        $hotspots.omitted_execution_slices = $group.omitted_execution_slices
        $summary.cpu_samples = $hotspots
    }
    $summary
})
$protocolRows = @(foreach ($entry in $protocols.GetEnumerator()) {
    $group = $entry.Value
    $summary = [ordered]@{ protocol = $entry.Key; count = $group.durations.Count; error_requests = $group.error_requests }
    Add-Percentiles $summary $group.durations
    $summary
})
$outcomeRows = @(foreach ($entry in $outcomes.GetEnumerator()) {
    [ordered]@{
        action = $entry.Key.Item1; outcome = $entry.Key.Item2
        count = $entry.Value.requests; error_requests = $entry.Value.error_requests
    }
})
$stageRows = @(Get-StageRows $stages)
$waitRows = @(foreach ($entry in $waits.GetEnumerator()) {
    $group = $entry.Value
    $summary = [ordered]@{
        route = $entry.Key.Item1; action = $entry.Key.Item2; file = $entry.Key.Item3
        line = $entry.Key.Item4; column = $entry.Key.Item5; kind = $entry.Key.Item6
        occurrences = $group.durations.Count + $group.open + $group.invalid
        completed_occurrences = $group.durations.Count; open_occurrences = $group.open
        invalid_duration_occurrences = $group.invalid
        total_us = if ($group.durations.Count) { $group.elapsed_us }
        mean_us = if ($group.durations.Count) { $group.elapsed_us / $group.durations.Count }
    }
    Add-Percentiles $summary $group.durations
    $summary
})
$ranked = @($requests | Sort-Object -Property elapsed_us -Descending -Stable)
$overlaps = @(foreach ($path in $ProcessSummaryPath) { Get-ProcessSummaryOverlap $path $workerRows $requests })
if ($TracePath -and $null -ne $selectedRequest) {
    Write-RequestTrace $TracePath $selectedRequest $traceStages $traceWaits $traceExecutionSlices $selectedOmittedSlices $traceCpuUnit
}

$report = [ordered]@{
    percentile_method = 'nearest rank on observed durations in microseconds'
    error_definition = 'HTTP status >= 400, a recorded stage failure, or a recognized failure/rejection outcome'
    coverage_scope = 'Completed profiler records only. Unobserved counts are relative to the newest supplied snapshot per profiler.'
    stage_note = 'Stage durations may overlap. Percentiles use recorded nonnegative durations. Stage request outcomes count each request once per stage name.'
    stage_wall_note = 'Inclusive totals sum recorded nonnegative durations and may overlap. Exclusive totals assign each covered span within the request to the unique innermost observed closed stage. Crossing or tied intervals with no unique innermost stage leave ambiguous wall time. Uncovered wall time has no observed closed stage. Open, invalid and omitted stages limit coverage. Means divide by timed occurrences.'
    stage_resource_note = 'Stage resources are exclusive profiler measurements. CPU is grouped by the request CPU unit. Missing, null and negative values are excluded, while measured zero is included. Stage means divide by sampled occurrences and explicit request residual means divide by sampled requests.'
    wait_note = 'Wait intervals may overlap. Percentiles use recorded nonnegative durations. Omitted waits are counted only in totals.'
    totals = $totals; workers = $workerRows
    routes = @($routeRows | Sort-Object -Property method, route, protocol -CaseSensitive)
    protocols = @($protocolRows | Sort-Object protocol -CaseSensitive)
    outcomes = @($outcomeRows | Sort-Object -Property action, outcome -CaseSensitive)
    stages = @($stageRows | Sort-Object -Property action, stage -CaseSensitive)
    waits = @($waitRows | Sort-Object -Property route, action, file, line, column, kind -CaseSensitive)
    slowest_requests = @($ranked | Select-Object -First $Top)
    error_requests = @($ranked | Where-Object is_error | Select-Object -First $Top)
    process_summary_overlap = $overlaps
    trace = if ($TracePath -and $null -ne $selectedRequest) { @{ path = $TracePath; request_key = $selectedRequest.request_key } }
}
if ($cpuSampleFile) { $report.cpu_samples = $cpuSampleReport }
ConvertTo-Json -InputObject $report -Depth 10
