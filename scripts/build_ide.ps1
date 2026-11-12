# Shared intermediates still require scripts/build_gate.ps1 -Name build.
param(
    [ValidateNotNullOrEmpty()]
    [string]$OutputPath = 'build\ZanIDE.exe',
    [string]$Version,
    [switch]$SkipDesignGeneration
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Resolve relative outputs against the repository, just like the default.
# Only the final artifact is isolated; drivers, embeds and response files stay
# in the shared build tree and must remain under the same build gate.
if ([string]::IsNullOrWhiteSpace($OutputPath)) { throw 'OutputPath must name an .exe file under build/.' }
$exeOut = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath
} else {
    Join-Path $root $OutputPath
}))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $root 'build')) + [IO.Path]::DirectorySeparatorChar
if (-not $exeOut.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetExtension($exeOut) -ine '.exe' -or
    [string]::IsNullOrWhiteSpace([IO.Path]::GetFileNameWithoutExtension($exeOut))) {
    throw 'OutputPath must name an .exe file under build/.'
}
$outputDir = Split-Path -Parent $exeOut
$exeOld = [IO.Path]::ChangeExtension($exeOut, '.prev.exe')
$depsOut = [IO.Path]::ChangeExtension($exeOut, '.deps.txt')
$versionSource = [IO.Path]::ChangeExtension($exeOut, '.IdeVersion.zan')
$cssOut = Join-Path $outputDir 'ide.css'
function Assert-IdeOutputPath {
    param([string]$Path)
    $cursor = $Path
    $buildDirectory = $buildRoot.TrimEnd([IO.Path]::DirectorySeparatorChar)
    while ($cursor -and $cursor.Length -ge $buildDirectory.Length) {
        try {
            $attributes = [IO.File]::GetAttributes($cursor)
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "IDE output path cannot traverse a reparse point: $cursor"
            }
        } catch [IO.FileNotFoundException] {
            # Missing output paths are created only after their ancestors pass.
        } catch [IO.DirectoryNotFoundException] {
            # A missing parent is allowed; continue checking existing ancestors.
        }
        $cursor = Split-Path -Parent $cursor
    }
}
foreach ($filePath in @($exeOut, $exeOld, $depsOut, $versionSource, $cssOut)) {
    Assert-IdeOutputPath $filePath
    if (Test-Path -LiteralPath $filePath -PathType Container) {
        throw "Output file path is a directory: $filePath"
    }
}
if ($PSBoundParameters.ContainsKey('Version')) {
    $buildVersion = $Version
} else {
    $buildVersion = [IO.File]::ReadAllText((Join-Path $root 'VERSION')).Trim()
}
if ($buildVersion -cnotmatch '\A[0-9]+\.[0-9]+\.[0-9]+(?:\.[0-9]+)?\z') {
    throw 'Version must contain three or four numeric components (0..65535).'
}
foreach ($part in ($buildVersion -split '\.')) {
    $component = 0
    if (-not [int]::TryParse($part, [ref]$component) -or $component -gt 65535) {
        throw 'Version must contain three or four numeric components (0..65535).'
    }
}
[IO.Directory]::CreateDirectory($outputDir) | Out-Null
Assert-IdeOutputPath $outputDir

function Write-IdeVersionSource {
    param([string]$TemplatePath, [string]$GeneratedPath, [string]$Number)
    $text = [IO.File]::ReadAllText($TemplatePath)
    $commit = (& git -C $root rev-parse --short=12 HEAD | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -cnotmatch '\A[0-9a-f]{7,40}\z') {
        throw 'Could not read the IDE build commit.'
    }
    $values = [ordered]@{
        Number = $Number
        Date = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ', [Globalization.CultureInfo]::InvariantCulture)
        Commit = $commit
    }
    foreach ($name in $values.Keys) {
        $pattern = '(?m)^(?<prefix>\s*static string ' + $name + '\(\) \{ return ")[^"]*(?<suffix>"; \}\r?)$'
        $matches = [regex]::Matches($text, $pattern)
        if ($matches.Count -ne 1) { throw "IDE version template must contain exactly one $name method." }
        $match = $matches[0]
        $text = $text.Remove($match.Index, $match.Length).Insert($match.Index,
            $match.Groups['prefix'].Value + $values[$name] + $match.Groups['suffix'].Value)
    }
    [IO.File]::WriteAllText($GeneratedPath, $text, [Text.UTF8Encoding]::new($false))
    $hash = (Get-FileHash -LiteralPath $GeneratedPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Output "[version] $Number date=$($values.Date) commit=$commit input=$GeneratedPath template=$TemplatePath sha256=$hash"
}

# ---- Standard Zan GUI project build for the IDE ---------------------------
# src/ide_zan is a normal zan.proj project: the `entry` manifest key names the
# .zan file holding the handwritten Main (IdeEntry, src/IdeForm.zan). The 33
# design documents (.html) are baked into the exe via --embed (compressed +
# encrypted) and projected at runtime through DesignedControl /
# FormBuilder.ProjectInto - the compile-time HTML channel (GenForm) is gone.
# zanc compiles everything, drives the bundled linker, and links the static
# native GUI runtime (Win32 backend) + the embedded skin packs.

$script:embedArgs = @()

Write-Output "IDE_BUILD_START"

# ---- 0) clang on PATH ------------------------------------------------------
# Sandbox/background sessions run with a trimmed PATH that may lack the LLVM
# this repo builds with. The shared probe (scripts\toolchain_paths.ps1) tries
# ZAN_CLANG/LLVM_ROOT/LLVM_HOME, then PATH, then the standard install roots it
# derives from the environment (mozbuild, VS's VC\Tools\Llvm via vswhere,
# Program Files\LLVM), so no candidate table lives here. The script needs
# clang, llvm-ar and llvm-readobj in the same bin directory. NOTE: a bare
# "clang" command name is unreliable in some restricted sessions even with the
# dir prepended, so every native call below uses the full path via $clangExe /
# $clangDir.
. (Join-Path $PSScriptRoot 'toolchain_paths.ps1')

$clangTried = $null
$clangExe = Resolve-ZanExecutable -Name 'clang' `
    -EnvVars @('ZAN_CLANG', 'ZAN_LLVM_ROOT', 'LLVM_ROOT', 'LLVM_HOME') `
    -Commands @('clang.exe', 'clang') `
    -Roots (Get-ZanLLVMRoots) `
    -Relative @('bin\clang.exe', 'clang.exe') `
    -Tried ([ref]$clangTried)
if (-not $clangExe) {
    Write-Output (Format-ZanProbeFailure -Name 'clang' `
        -EnvVars @('ZAN_LLVM_ROOT', 'LLVM_DIR', 'ZAN_CLANG') -Tried $clangTried)
    Write-Output "CLANG_NOT_FOUND"; exit 1
}
$clangDir = Split-Path -Parent $clangExe

# ---- 1) independent native drivers (static, mingw ABI) -------------------
# zanc selects archives from each module's driver directory by live imports.
& (Join-Path $PSScriptRoot "build_gui_driver.ps1") -Static -Clang $clangExe `
    -Drivers @("zan_gui", "zan_image", "zan_audio")

# ---- 2) embedded resources (skins, help topics, ide.css) -------------------
# Everything the IDE reads as data ships inside the exe and is looked up by name
# through zan_embed_*: skin packs, the Help page's topics.json and the
# page-layout stylesheet. Regenerate every build so editing one shows up without
# a manual step.
#
# All three groups go into ONE object on purpose: zan_embed_register replaces the
# registered table, so a second generated object would hide the first one's
# resources (that is how ide.css and topics.json used to need disk copies).
# The 'ide' group is filtered to *.css because assets\ also holds docs\.
& powershell -ExecutionPolicy Bypass -File scripts\gen_embed.ps1 `
    -Group "packages\Zan.Gui\skins=skins;src\ide_zan\assets\docs=docs;src\ide_zan\assets=ide:*.css" `
    -OutC build\embed_gen.c -OutO build\embed_gen.o -Clang $clangExe
if ($LASTEXITCODE -ne 0) { Write-Output "EMBED_GEN_FAILED"; exit 1 }

# ---- 3) sources: entry first, then project + Gui stdlib -------------------
$zanc = if (Test-Path "build\zanc.exe") { "build\zanc.exe" } else { "dist\win-x64\toolchain\zanc.exe" }
Write-Output "[zanc] $zanc"

$files = @()
# IDE sources (the standard project's own code). The IDE's own components
# register their kinds into HeavyControls at startup (IdeBootstrap.Install),
# so no build-time component registry is generated any more.
$files += (Get-ChildItem src\ide_zan\*.zan -Recurse).FullName
# The GUI stdlib is namespaced across subfolders (Gui root + Widget /
# Component / Designer); recurse so every part is compiled.
$files += (Get-ChildItem packages\Zan.Gui\src\Gui -Recurse -Include *.zan).FullName
if (Test-Path packages\Zan.Gui.Designer) {
    $files += (Get-ChildItem packages\Zan.Gui.Designer\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Gui.Charts) {
    $files += (Get-ChildItem packages\Zan.Gui.Charts\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Gui.Browser) {
    $files += (Get-ChildItem packages\Zan.Gui.Browser\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Industrial) {
    $files += (Get-ChildItem packages\Zan.Industrial\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Data) {
    $files += (Get-ChildItem packages\Zan.Data\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Desktop) {
    $files += (Get-ChildItem packages\Zan.Desktop\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Security) {
    $files += (Get-ChildItem packages\Zan.Security\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Net) {
    $files += (Get-ChildItem packages\Zan.Net\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Diagnostics) {
    $files += (Get-ChildItem packages\Zan.Diagnostics\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Text) {
    $files += (Get-ChildItem packages\Zan.Text\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Linq) {
    $files += (Get-ChildItem packages\Zan.Linq\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Scripting) {
    $files += (Get-ChildItem packages\Zan.Scripting\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Ai\src\Ai\Knowledge) {
    $files += (Get-ChildItem packages\Zan.Ai\src\Ai\Knowledge -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Globalization) {
    $files += (Get-ChildItem packages\Zan.Globalization\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.Resources) {
    $files += (Get-ChildItem packages\Zan.Resources\src -Recurse -Include *.zan).FullName
}
if (Test-Path packages\Zan.IO) {
    $files += (Get-ChildItem packages\Zan.IO\src -Recurse -Include *.zan).FullName
}
# Tar (publish archives macOS/Linux output dirs as tar -- the only common
# format that carries the unix mode bit, see ZanIDE.Package.zan) rides in the
# Zan.IO group above alongside Zip/GZip used by Xlsx and MarketplaceInstall.
if (Test-Path packages\Zan.Web) {
    $files += (Get-ChildItem packages\Zan.Web\src -Recurse -Include *.zan).FullName
}
# System pieces the editor/workspace rely on.
$files += (Join-Path (Get-Location) "stdlib\System\IO\File.zan")
$files += (Join-Path (Get-Location) "stdlib\System\IO\Directory.zan")
$files += (Join-Path (Get-Location) "stdlib\System\IO\FileInfo.zan")

# Entry: the handwritten Main (IdeEntry, B-ID44) lives in the file named by
# the manifest's `entry` key. Design documents are NOT compile inputs any
# more - the compile-time HTML channel is gone (zanc rejects .html) - they
# ride in the executable via --embed below and are projected at runtime
# (DesignedControl / FormBuilder.ProjectInto).
$entryRel = ([regex]::Match(
    [System.IO.File]::ReadAllText((Join-Path (Get-Location) "src\ide_zan\zan.proj")),
    '(?m)^\s*entry\s*=\s*(.+?)\s*$')).Groups[1].Value
if ($entryRel -eq "") { Write-Output "PROJECT_ENTRY_MISSING"; exit 1 }
$entry = Join-Path (Get-Location) (Join-Path "src\ide_zan" ($entryRel -replace '/', '\'))
if (!(Test-Path -LiteralPath $entry)) { Write-Output "PROJECT_ENTRY_NOT_FOUND $entry"; exit 1 }
Write-Output "[entry] $entry"

# Every designed document is baked into the exe under the "ide/" prefix
# (matching the Design("...", "ide/...") paths in the generated halves):
# File.ReadAllText resolves embedded-first, and baked payloads are
# compressed + encrypted (no loose HTML ships with the publish).
$designs = @(Get-ChildItem src\ide_zan -Recurse -Include *.html |
    ForEach-Object { $_.FullName })
Write-Output ("[designs] " + $designs.Count + " embedded")
$designs | ForEach-Object {
    $rel = $_.Substring((Join-Path (Get-Location) "src\ide_zan\src\").Length).Replace('\', '/')
    $script:embedArgs += @("--embed", "$_=ide/$rel")
}

# ---- 2.5) 设计稿半件 + 组件注册表（自发现）---------------------------------
# 扫描 .html 设计稿：生成 <Name>.design.zan 半件与 IdeDesignedRegistry.zan。
# 新增组件 = 新增 .html(+code-behind)，下次构建自动进表——没有手工清单。
if (-not $SkipDesignGeneration) {
    python scripts/gen_design_halves.py
    if ($LASTEXITCODE -ne 0) { Write-Output "DESIGN_HALVES_FAILED"; exit 1 }
} else {
    Write-Output '[designs] using existing generated halves and registry'
}

# The source list goes through a zanc @file response file: the explicit list
# crossed the Windows 32K command-line limit when Zan.Security joined the
# package groups ("The filename or extension is too long" from CreateProcess).
# Supply build metadata without rewriting the tracked placeholder or any IDE source.
$versionTemplate = Join-Path $root 'src\ide_zan\src\services\IdeVersion.zan'
Write-IdeVersionSource -TemplatePath $versionTemplate -GeneratedPath $versionSource -Number $buildVersion
$files = @($files | Where-Object { $_ -ne $entry -and $_ -ne $versionTemplate })
$files += $versionSource
$rsp = Join-Path (Get-Location) "build\ide_files.rsp"
$files | ForEach-Object { '"' + $_ + '"' } | Set-Content -Path $rsp -Encoding UTF8

$zanArgs = @()
$zanArgs += $entry
$zanArgs += $script:embedArgs
$zanArgs += ("@" + $rsp)
$zanArgs += @("-o", $exeOut, "--subsystem", "windows")
# Release shape (--publish): Os codegen + the optimization sweep + a stripped
# link. The unoptimized dev default (O0 + fast_codegen) made the exe ~21 MB,
# of which 13.8 MB was .text alone; the same sources publish to ~14 MB. The
# debug ARC guards are off either way (below), so a long-running editor loses
# nothing it was using. Crash line numbers in build\zan_crash.log were the
# -g DWARF line tables: drop the crash-log source resolution for now; module
# +offset still lands there, and ZAN_IDE_ZANC_ARGS="-g" brings the tables back
# for a dedicated debug build when a crash needs triaging.
$zanArgs += @("--publish")
if ($env:IDE_NO_PUBLISH -eq "1") { $zanArgs = $zanArgs -ne "--publish" }
# ...but not the debug ARC guards --publish omits either (they were the -g
# defaults). --arc-guard never
# returns a released object to the allocator (it quarantines it so a stale
# retain/release traps), and --check-leaks adds an atomic pair to every
# allocation and release. In a long-running editor that reads as an
# ever-growing process and a permanent CPU tax. Ask for them explicitly with
# ZAN_IDE_ZANC_ARGS="--arc-guard --check-leaks" when chasing a leak.
$zanArgs += @("--no-arc-guard", "--no-check-leaks")
$zanArgs += @("--auto-stdlib", "--link-mode", "static")
$zanArgs += @("--link-input", (Join-Path (Get-Location) "build\embed_gen.o"))
# Runtime/process system deps; each selected driver adds its own .libs closure.
$zanArgs += @("--link-lib", "ws2_32", "--link-lib", "mswsock")
$zanArgs += @("--link-lib", "psapi", "--link-lib", "advapi32")
$zanArgs += @("--link-lib", "dwmapi", "--link-lib", "gdi32", "--link-lib", "imm32",
    "--link-lib", "ole32")
# (winpthread is not named here: zanc already links it as the static archive
# libwinpthread.a. Naming it makes ld prefer libwinpthread.dll.a instead, and
# the exe then needs libwinpthread_64-1.dll beside it -- a DLL the toolchain
# does not ship, so the IDE would not start on another machine.)
$zanArgs += @("--link-lib", "user32", "--link-lib", "rpcrt4")
$zanArgs += @("--icon", (Join-Path (Get-Location) "assets\zan.ico"))
# Escape hatch for diagnostic builds: ZAN_IDE_ZANC_ARGS="--arc-guard" builds the
# IDE with extra compiler flags without editing this script.
if ($env:ZAN_IDE_ZANC_ARGS) {
    $zanArgs += ($env:ZAN_IDE_ZANC_ARGS -split '\s+' | Where-Object { $_ })
}

# A running IDE holds a lock on its own executable, so the linker cannot
# overwrite it ("Permission denied"). Windows does allow RENAMING a running
# image: park it aside (and clear the previous parked copy) so a rebuild never
# needs the editor to be closed first.
if (Test-Path -LiteralPath $exeOld) {
    Remove-Item -LiteralPath $exeOld -Force -ErrorAction SilentlyContinue
}
if (Test-Path -LiteralPath $exeOut) {
    Move-Item -LiteralPath $exeOut -Destination $exeOld -Force `
        -ErrorAction SilentlyContinue
}

# Under $ErrorActionPreference = "Stop" a redirected native stderr line is
# promoted to a terminating NativeCommandError, so a mere compiler warning
# aborted an otherwise successful build. Only the exit code decides here.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$out = & $zanc @zanArgs 2>&1
$code = $LASTEXITCODE
$ErrorActionPreference = $prevEap
if ($code -ne 0) {
    # Show every diagnostic, not just the tail: a failed build is read from
    # this output and a truncated list hides the first (usually root) error.
    $out | Select-String -Pattern 'error|warning' | ForEach-Object { $_.Line }
    $out | Select-Object -Last 10
    Write-Output "IDE_LINK_FAILED code=$code"
    exit 1
}
if ($env:ZAN_IDE_SHOW_OUT) { $out | ForEach-Object { Write-Output $_ } }

# The stylesheet is baked into the exe (embed_gen above) and read from memory;
# a copy beside the executable would only shadow it, so drop any stale one --
# and the dev build then exercises the same path the release ships.
Remove-Item -LiteralPath $cssOut `
    -Force -ErrorAction SilentlyContinue

# ---- record the non-system DLLs the IDE needs beside it -------------------
# ZanIDE.exe imports more than the Windows API: the runtime objects pull in
# SQLite, and zanc stages driver DLLs (zan_gui, WebView2Loader)
# next to the exe. A release that ships the exe without them dies on launch
# with "cannot find <dll>" on any machine that has no copy on PATH -- which is
# every machine but this one. Read the truth from the exe's own import table
# (llvm-readobj is already required by this script for clang/llvm-ar) and let
# publish_ide.ps1 ship exactly that list.
$sysDll = @(
    'kernel32','kernelbase','user32','gdi32','gdiplus','advapi32','shell32',
    'shlwapi','shcore','ole32','oleaut32','comctl32','comdlg32','crypt32',
    'ws2_32','mswsock','iphlpapi','psapi','rpcrt4','dwmapi','imm32','uxtheme',
    'winmm','version','dbghelp','setupapi','bcrypt','ncrypt','secur32',
    'wintrust','userenv','netapi32','powrprof','ntdll','msvcrt','msimg32',
    'winspool','wtsapi32','dnsapi','normaliz','usp10','opengl32','d3d11',
    'dxgi','avrt','propsys','windowscodecs'
)
# Dynamically loaded ones never show up in the import table (WebView2Loader is
# LoadLibrary'd on demand), so union in whatever zanc reported staging.
$staged = @($out | ForEach-Object {
    $m = [regex]::Match([string]$_, "^\s*bundled driver '[^']+' \S+ (?<f>\S+\.dll)\s*$")
    if ($m.Success) { $m.Groups['f'].Value }
})
$imports = @(& (Join-Path $clangDir "llvm-readobj.exe") --coff-imports $exeOut 2>&1 |
    ForEach-Object {
        $m = [regex]::Match([string]$_, '^\s*Name:\s*(?<f>\S+\.dll)\s*$')
        if ($m.Success) { $m.Groups['f'].Value }
    } | Sort-Object -Unique)
if ($imports.Count -eq 0) {
    Write-Output "IDE_DEPS_WARN: could not read the import table (llvm-readobj missing?); $depsOut left as-is"
} else {
    $deps = @($imports | Where-Object {
        $base = [IO.Path]::GetFileNameWithoutExtension($_).ToLower()
        ($sysDll -notcontains $base) -and ($base -notlike 'api-ms-*') -and
        ($base -notlike 'ext-ms-*')
    })
    $deps = @(($deps + $staged) | Sort-Object -Unique)
    foreach ($dep in $deps) {
        if (-not (Test-Path -LiteralPath (Join-Path $outputDir $dep))) {
            Write-Output "IDE_DEPS_WARN: $dep is imported but not in $outputDir (the release cannot ship it)"
        }
    }
    [System.IO.File]::WriteAllLines($depsOut, $deps)
    Write-Output ("[deps] " + ($deps -join " "))
}

Write-Output "IDE_BUILD_OK $OutputPath"
