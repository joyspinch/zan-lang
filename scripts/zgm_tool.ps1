param(
    [ValidateSet("audit", "test", "stats")]
    [string]$Action = "audit"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$ZgmRoot = Join-Path $Root "stdlib\Game\Zgm"
$TestRoot = Join-Path $Root "tests\conformance"

function Get-ZgmStats {
    $files = Get-ChildItem -Path $ZgmRoot -Recurse -Filter *.zan
    $lineCount = 0
    foreach ($file in $files) {
        $lineCount += (Get-Content -LiteralPath $file.FullName).Count
    }
    $classes = (
        rg --no-heading --count-matches "^(class|enum|delegate) " $ZgmRoot |
        ForEach-Object {
            $parts = $_ -split ":"
            [int]$parts[$parts.Length - 1]
        } |
        Measure-Object -Sum
    ).Sum
    [pscustomobject]@{
        ZanFiles = $files.Count
        Lines = $lineCount
        PublicTypes = $classes
        ConformanceCases = (
            Get-ChildItem -Path $TestRoot -Filter "zgm_*.zan"
        ).Count
    }
}

function Invoke-ZgmTests {
    $zanc = Join-Path $Root "build\zanc.exe"
    if (!(Test-Path -LiteralPath $zanc)) {
        throw "zanc was not found: $zanc"
    }

    $cases = Get-ChildItem -Path $TestRoot -Filter "zgm_*.zan" |
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
            throw "ZGM program failed: $($case.Name)"
        }

        $expectedPath = Join-Path $TestRoot ($base + ".out")
        $expected = Get-Content -LiteralPath $expectedPath -Raw
        $actualNorm = (($actual -replace "`r`n", "`n").TrimEnd())
        $expectedNorm = (($expected -replace "`r`n", "`n").TrimEnd())
        if ($actualNorm -cne $expectedNorm) {
            throw "ZGM output mismatch: $($case.Name)"
        }

        Write-Output ("PASS zanc " + $case.Name)
        $passed = $passed + 1
    }
    if ($passed -ne $cases.Count) {
        throw "ZGM direct zanc tests failed."
    }
    Write-Output ("DIRECT_ZANC_PASS=" + $passed + "/" + $cases.Count)
}

if ($Action -eq "stats") {
    Get-ZgmStats | Format-List
    exit 0
}

if ($Action -eq "test") {
    Invoke-ZgmTests
    exit 0
}

$legacy = rg -n "Game\.Dm|namespace Game\.Dm|\bDm[A-Z]" `
    $ZgmRoot $TestRoot 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Output $legacy
    throw "Legacy Game.Dm public names remain."
}
if ($LASTEXITCODE -ne 1) {
    throw "rg failed while auditing legacy names."
}

$stats = Get-ZgmStats
$stats | Format-List
Invoke-ZgmTests
