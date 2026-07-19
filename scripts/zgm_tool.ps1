param(
    [ValidateSet("audit", "test", "stats", "coverage")]
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

function Get-ZgmCoverage {
    $areas = @(
        [pscustomobject]@{
            Area = "App 配置与系统事件"
            Status = "Zan 原生完成"
            Entry = "AppConfig / ZgmEvents"
            Tests = "zgm_app_config, zgm_ui_events"
        }
        [pscustomobject]@{
            Area = "地图、角色、战斗、AI"
            Status = "Zan 原生完成"
            Entry = "ZgmWorld / ZgmCamera / ZgmPath"
            Tests = "zgm_map_runtime, zgm_ai_runtime, zgm_skill_runtime"
        }
        [pscustomobject]@{
            Area = "窗口、控件、节点、预制体"
            Status = "Zan 原生完成"
            Entry = "ZgmUiRuntime / Widget / Node / Prefab"
            Tests = "zgm_ui_* , zgm_widgets, zgm_nodes, zgm_prefab_*"
        }
        [pscustomobject]@{
            Area = "富文本、模板、补间、菜单、全局"
            Status = "Zan 原生完成"
            Entry = "RichText / TemplateVars / Tween / Menu / Globals"
            Tests = "zgm_text, zgm_tween, zgm_engine_runtime"
        }
        [pscustomobject]@{
            Area = "SQLite、本地存档、项目校验"
            Status = "Zan 本土化完成"
            Entry = "Game.Zgm.Data / ZgmProject.Validate"
            Tests = "zgm_database, zgm_save_repository, zgm_project_validation"
        }
        [pscustomobject]@{
            Area = "TCP、WebSocket、服务端基础类型"
            Status = "已有基础实现"
            Entry = "NetRuntime / ServerConfig / ServerEvents"
            Tests = "zgm_net, zgm_net_runtime"
        }
        [pscustomobject]@{
            Area = "Lua/App.lua 与 Lua/Server.lua 兼容层"
            Status = "明确不做"
            Entry = "使用 Zan 原生 API 替代"
            Tests = "不适用"
        }
        [pscustomobject]@{
            Area = "真实渲染器、音频后端、可视化编辑器"
            Status = "适配器边界"
            Entry = "消费 ZgmWorld / ZgmUiRuntime 状态"
            Tests = "不属于 Game.Zgm 核心"
        }
    )
    $areas | Format-Table -AutoSize
}

if ($Action -eq "stats") {
    Get-ZgmStats | Format-List
    exit 0
}

if ($Action -eq "coverage") {
    Get-ZgmStats | Format-List
    Get-ZgmCoverage
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
