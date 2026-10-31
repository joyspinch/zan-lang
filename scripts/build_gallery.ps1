$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# The gallery is a pure Gui stdlib program: on Windows its window shell is the
# Zan-side Win32Shell (Native.zan #if WINDOWS), so the native Win32 GUI
# runtime is enough. The runtime is built as a static
# mingw-ABI archive plus zanc's own link driver, which pulls in the async
# reactor (rt_io) and sync runtime (rt_sync) automatically.

$galleryComponents = "examples\gui_gallery\components"
$failed = $false

try {
    Write-Output "[1/2] Building independent native drivers (static, mingw ABI)..."
    & (Join-Path $PSScriptRoot "build_gui_driver.ps1") -Static `
        -Drivers @("zan_gui", "zan_image", "zan_audio")

    Write-Output "[2/2] Compiling and linking gallery_test.exe..."
    # 入口是 Gallery.zan（Main 在里面），实体在 GalleryModel.zan；
    # 5e8baf34 把旧的 gui_gallery.zan 拆成了这两个，脚本这里跟着改。
    # 入口必须排在命令行第一位（zanc 只给 paths[0] 生成 Main）。
    $entry = Join-Path (Get-Location) "examples\gui_gallery\Gallery.zan"

    $files = @()
    # Gui 包整树（5e8baf34 之后 Widget/Component/Core/Styling/… 分到了
    # 子目录，只取根与 Widget\ 会让 Gui/Styling、Gui/Rendering 这些
    # 命名空间解析不到）。画廊另外用到 Chart / CodeEditor / DataTable /
    # Hmi 四个扩展包，一并带上。
    $files += (Get-ChildItem packages\Zan.Gui\src\Gui -Recurse -Include *.zan).FullName
    foreach ($pkg in @("Zan.Gui.Charts", "Zan.Gui.CodeEditor", "Zan.Gui.DataTable",
                       "Zan.Industrial")) {
        $pkgDir = Join-Path $root "packages\$pkg\src"
        if (Test-Path $pkgDir) {
            $files += (Get-ChildItem $pkgDir -Recurse -Include *.zan).FullName
        }
    }
    $files += (Get-ChildItem $galleryComponents\*.zan).FullName
    # 规范模型（Comp/Demo/…）在 5e8baf34 之后住在 Models\ 目录里，
    # 漏掉它们整个示例都编不过（undefined type 'Comp'）。
    $files += (Get-ChildItem (Join-Path $root "examples\gui_gallery\Models") -Filter *.zan).FullName
    $files += (Join-Path (Get-Location) "examples\gui_gallery\GalleryModel.zan")
    $files += (Join-Path (Get-Location) "examples\gui_gallery\MapChinaData.zan")
    $files += (Join-Path (Get-Location) "stdlib\System\IO\File.zan")
    $files += (Join-Path (Get-Location) "stdlib\System\IO\Directory.zan")
    $files += (Join-Path (Get-Location) "stdlib\System\IO\FileInfo.zan")

    # 源文件列表走 zanc @file 响应文件：Gui 整树 + 四个扩展包有 700 多个
    # .zan，内联传参会撞上 Windows 32K 命令行上限（CreateProcess 直接失败，
    # PowerShell 会报成一句莫名其妙的 "StandardOutputEncoding ..."）。
    # IDE 构建用的是同一套办法（build\ide_files.rsp）。
    $rsp = Join-Path (Get-Location) "build\gallery_files.rsp"
    $files | ForEach-Object { '"' + $_ + '"' } | Set-Content -Path $rsp -Encoding UTF8

    $zanArgs = @()
    $zanArgs += $entry
    $zanArgs += ("@" + $rsp)
    $zanArgs += @("-o", "build/gallery_test.exe", "--subsystem", "windows")
    # Demo/props/events catalog + map geometry (assets/) travel inside the
    # exe; File.ReadAllText falls back to the embedded copy when the loose
    # file is not next to the binary.
    $zanArgs += @("--embed", "examples\gui_gallery\assets=assets")
    $zanArgs += @("--auto-stdlib", "--link-mode", "static")
    # Each selected driver contributes its own system dependency metadata.
    $zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
    $zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
    $zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32",
        "--link-lib", "ole32")
    $zanArgs += @("--link-lib", "user32", "--link-lib", "rpcrt4", "--link-lib", "winpthread")
    $zanArgs += @("--icon", (Join-Path (Get-Location) "assets\zan.ico"))
    # 编译器往 stderr 写的普通诊断行在 $ErrorActionPreference="Stop" 下会被
    # 提升成终止错误（NativeCommandError），一次无害的 warning 就能把构建
    # 打断——只有退出码算数（与 build_ide.ps1 同一处理）。
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $out = & build\zanc.exe @zanArgs 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prevEap
    if ($code -ne 0) {
        $out | Select-Object -Last 40
        throw "GALLERY_LINK_FAILED code=$code"
    }
} catch {
    Write-Output $_
    $failed = $true
}

if ($failed) { exit 1 }
Write-Output "GALLERY_BUILD_OK build\gallery_test.exe"
