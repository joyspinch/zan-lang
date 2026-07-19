param(
    [ValidateSet("test", "stats", "coverage")]
    [string]$Action = "test"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$GameRoot = Join-Path $Root "stdlib\Game"
$TestRoot = Join-Path $Root "tests\conformance"

function Get-GameStats {
    $areas = @("Foundation", "Board", "Cards", "Arcade2D")
    foreach ($area in $areas) {
        $path = Join-Path $GameRoot $area
        $files = Get-ChildItem -Path $path -Recurse -Filter *.zan
        $lines = 0
        foreach ($file in $files) {
            $lines += (Get-Content -LiteralPath $file.FullName).Count
        }
        [pscustomobject]@{
            Area = $area
            Files = $files.Count
            Lines = $lines
        }
    }
}

function Invoke-GameTests {
    $zanc = Join-Path $Root "build\zanc.exe"
    if (!(Test-Path -LiteralPath $zanc)) {
        throw "zanc was not found: $zanc"
    }

    $cases = Get-ChildItem -Path $TestRoot -Filter "game_*.zan" |
        Sort-Object Name
    $passed = 0
    foreach ($case in $cases) {
        $base = [IO.Path]::GetFileNameWithoutExtension($case.Name)
        $exe = Join-Path $Root ("build\direct_" + $base + ".exe")
        & $zanc $case.FullName -o $exe
        if ($LASTEXITCODE -ne 0) {
            throw "zanc compile failed: $($case.Name)"
        }

        $actual = (& $exe | Out-String)
        if ($LASTEXITCODE -ne 0) {
            throw "Game program failed: $($case.Name)"
        }
        $expected = Get-Content -LiteralPath (
            Join-Path $TestRoot ($base + ".out")) -Raw
        $actualNorm = (($actual -replace "`r`n", "`n").TrimEnd())
        $expectedNorm = (($expected -replace "`r`n", "`n").TrimEnd())
        if ($actualNorm -cne $expectedNorm) {
            Write-Output "--- expected"
            Write-Output $expectedNorm
            Write-Output "--- actual"
            Write-Output $actualNorm
            throw "Game output mismatch: $($case.Name)"
        }
        Write-Output ("PASS zanc " + $case.Name)
        $passed += 1
    }
    Write-Output ("DIRECT_GAME_PASS=" + $passed + "/" + $cases.Count)
}

if ($Action -eq "stats") {
    Get-GameStats | Format-Table -AutoSize
    exit 0
}

if ($Action -eq "coverage") {
    Get-GameStats | Format-Table -AutoSize
    @(
        [pscustomobject]@{
            Area = "Deterministic lifecycle"
            Entry = "FixedStepClock / InputMap / SceneStack"
            Test = "game_foundation"
        }
        [pscustomobject]@{
            Area = "Board, turns and replay"
            Entry = "GridBoard / BoardMatch / GridPathfinder"
            Test = "game_board"
        }
        [pscustomobject]@{
            Area = "Deck-building runtime"
            Entry = "CardDeck / CardBattle"
            Test = "game_cards"
        }
        [pscustomobject]@{
            Area = "Lightweight 2D simulation"
            Entry = "Collision2D / AnimationPlayer / ArcadeWorld"
            Test = "game_arcade2d"
        }
    ) | Format-Table -AutoSize
    exit 0
}

Invoke-GameTests
