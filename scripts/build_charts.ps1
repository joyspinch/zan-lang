$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# The charts example is a pure Gui stdlib program: on Windows its window shell
# is the Zan-side Win32Shell (Native.zan #if WINDOWS), so the native Win32 GUI
# runtime is enough -- no extra driver is linked. The runtime is built as a static
# mingw-ABI archive plus zanc's own link driver, which pulls in the async
# reactor (rt_io) and sync runtime (rt_sync) automatically.
#
# Unlike the gallery there is no custom-component scan step: the example uses
# only stdlib widgets, and zanc auto-includes the Chart component sources by
# namespace when the example `using Gui.Component.Chart;`s them.

Write-Output "[1/2] Building native GUI runtime (Win32, static, mingw ABI)..."
clang --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC `
    -c src\runtime\gui_runtime.c -o build\zan_gui_charts_gnu.o
if ($LASTEXITCODE -ne 0) { throw "RUNTIME_COMPILE_FAILED" }
clang++ --target=x86_64-w64-windows-gnu -O2 -DZAN_GUI_STATIC `
    -fno-exceptions -fno-rtti `
    -c src\runtime\gui_runtime_dwrite.cpp -o build\zan_gui_dwrite_charts_gnu.o
if ($LASTEXITCODE -ne 0) { throw "RUNTIME_DWRITE_COMPILE_FAILED" }
llvm-ar rcs build\libzan_gui_charts_gnu.a build\zan_gui_charts_gnu.o build\zan_gui_dwrite_charts_gnu.o
if ($LASTEXITCODE -ne 0) { throw "RUNTIME_LIB_FAILED" }

Write-Output "[2/2] Compiling and linking charts_test.exe..."
$files = @()
$files += (Get-ChildItem packages\Zan.Gui\src\Gui\*.zan).FullName
$files += (Get-ChildItem packages\Zan.Gui\src\Gui\Widget\*.zan).FullName
$chartDir = "packages\Zan.Gui.Charts\src\Gui\Component\Chart"
if (-not (Test-Path $chartDir)) {
    $chartDir = "packages\Zan.Gui.Charts\src\Gui\Component\Chart"
}
$files += (Get-ChildItem $chartDir\*.zan).FullName
$files += (Get-ChildItem examples\gui_charts\*.zan).FullName
$files += (Join-Path (Get-Location) "examples\gui_gallery\MapChinaData.zan")

$zanArgs = @()
$zanArgs += $files
$zanArgs += @("-o", "build\charts_test.exe", "--subsystem", "windows")
# 官方示例全量以 JSON 资源内嵌（options/ 目录递归，含 doc-example/
# 子目录）+ 官方注册表（菜单目录），Zan 侧只做装载与渲染。
$zanArgs += @("--embed", "examples\gui_charts\options")
$zanArgs += @("--embed", "examples\gui_charts\charts-registry.json=charts-registry")
# 地图资产：GeoJSON（USA/world/HK/iceland/ch）+ SVG 地图（echarts
# geo-svg 系列的 registerMap(name, {svg}) 素材），gui_charts 启动时
# RegisterMaps() 按官方地图名注册进 ChartMaps。
$zanArgs += @("--embed", "examples\gui_charts\maps")
$zanArgs += @("--libpath", "build", "--link-lib", "zan_gui_charts_gnu")
# Native Win32 backend needs only the system libs it imports directly (the
# runtime's #pragma libs: dwmapi/user32/gdi32/imm32) plus the reactor deps.
# ole32: WASAPI 音频初始化（CoInitializeEx/CoUninitialize）——缺它链接失败。
$zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
$zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
$zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32")
$zanArgs += @("--link-lib", "user32", "--link-lib", "rpcrt4", "--link-lib", "ole32", "--link-lib", "dwrite")
$zanArgs += @("--icon", (Join-Path (Get-Location) "assets\zan.ico"))
# zanc 的进度杂音走 stderr；EAP=Stop 会把首个 stderr 行升级成
# NativeCommandError 并在 zanc 跑完前掐断管道（exe 不会重链）。
# 这里局部降级到 Continue，让 stderr 落进 $out，成败以 $LASTEXITCODE 判定。
$ErrorActionPreference = "Continue"
$out = & build\zanc.exe @zanArgs 2>&1
$code = $LASTEXITCODE
$ErrorActionPreference = "Stop"
if ($code -ne 0) {
    $out | Select-Object -Last 40
    throw "CHARTS_LINK_FAILED code=$code"
}

Write-Output "CHARTS_BUILD_OK build\charts_test.exe"
