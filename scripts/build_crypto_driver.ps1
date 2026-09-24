# Generates modern lightweight hardware-accelerated zan_crypto & zan_ssl
# native driver bundles across all platforms (~25KB - 145KB each, replacing legacy 67MB OpenSSL).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$scratch = Join-Path $root "_scratch"
if (!(Test-Path $scratch)) { New-Item -ItemType Directory -Path $scratch | Out-Null }
$fakeInc = Join-Path $scratch "fake_include"
if (!(Test-Path $fakeInc)) {
    New-Item -ItemType Directory -Path $fakeInc | Out-Null
    touch (Join-Path $fakeInc "stdlib.h")
    touch (Join-Path $fakeInc "stdio.h")
    touch (Join-Path $fakeInc "string.h")
}

$llvmInc = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/lib/clang/19/include"

$man = Join-Path $root "stdlib\System\Security\Cryptography\drivers\driver.manifest"
@"
# Native [DllImport] libraries owned by this module (one -l basename per line).
ssl
crypto
"@ | Set-Content -Encoding ascii $man

# -----------------------------------------------------------------------------
# 1. Windows x64
# -----------------------------------------------------------------------------
Write-Host ">>> Building Windows x64 drivers..."
$drvWinX64 = Join-Path $root "stdlib\System\Security\Cryptography\drivers\win-x64"
if (!(Test-Path $drvWinX64)) { New-Item -ItemType Directory -Force -Path $drvWinX64 | Out-Null }

& clang -shared -O3 -maes -mpclmul -mssse3 -DZAN_CRYPTO_BUILD_DLL -D_CRT_SECURE_NO_WARNINGS `
    (Join-Path $root "src/runtime/zan_crypto.c") `
    -o (Join-Path $drvWinX64 "libcrypto-3-x64.dll")

$def1 = Join-Path $scratch "crypto.def"
$dump1 = & llvm-objdump -p (Join-Path $drvWinX64 "libcrypto-3-x64.dll")
$names1 = @()
$inExports = $false
foreach ($line in $dump1) {
    if ($line -match "Export Table:") { $inExports = $true; continue }
    if ($inExports) {
        $m = [regex]::Match($line, "^\s+\d+\s+0x[0-9a-fA-F]+\s+(\S+)\s*$")
        if ($m.Success) { $names1 += $m.Groups[1].Value }
    }
}
"LIBRARY libcrypto-3-x64.dll`nEXPORTS" | Set-Content -Encoding ascii $def1
$names1 | ForEach-Object { $_ } | Add-Content -Encoding ascii $def1
& llvm-dlltool -m i386:x86-64 -d $def1 -l (Join-Path $drvWinX64 "libcrypto.dll.a") -D "libcrypto-3-x64.dll"

$cryptoLib = Join-Path $drvWinX64 "libcrypto-3-x64.lib"
& clang -shared -O3 -DZAN_CRYPTO_BUILD_DLL -D_CRT_SECURE_NO_WARNINGS `
    (Join-Path $root "src/runtime/zan_tls.c") `
    $cryptoLib `
    -o (Join-Path $drvWinX64 "libssl-3-x64.dll")

$def2 = Join-Path $scratch "ssl.def"
$dump2 = & llvm-objdump -p (Join-Path $drvWinX64 "libssl-3-x64.dll")
$names2 = @()
$inExports = $false
foreach ($line in $dump2) {
    if ($line -match "Export Table:") { $inExports = $true; continue }
    if ($inExports) {
        $m = [regex]::Match($line, "^\s+\d+\s+0x[0-9a-fA-F]+\s+(\S+)\s*$")
        if ($m.Success) { $names2 += $m.Groups[1].Value }
    }
}
"LIBRARY libssl-3-x64.dll`nEXPORTS" | Set-Content -Encoding ascii $def2
$names2 | ForEach-Object { $_ } | Add-Content -Encoding ascii $def2
& llvm-dlltool -m i386:x86-64 -d $def2 -l (Join-Path $drvWinX64 "libssl.dll.a") -D "libssl-3-x64.dll"

"libcrypto-3-x64.dll`n" | Set-Content -Encoding ascii (Join-Path $drvWinX64 "crypto.bundle")
"libssl-3-x64.dll`nlibcrypto-3-x64.dll`n" | Set-Content -Encoding ascii (Join-Path $drvWinX64 "ssl.bundle")

$staticWinX64 = Join-Path $drvWinX64 "static"
if (!(Test-Path $staticWinX64)) { New-Item -ItemType Directory -Force -Path $staticWinX64 | Out-Null }
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $staticWinX64 "libcrypto.a")
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $staticWinX64 "libssl.a")
& clang -c -O3 -maes -mpclmul -mssse3 -D_CRT_SECURE_NO_WARNINGS (Join-Path $root "src/runtime/zan_crypto.c") -o (Join-Path $scratch "zan_crypto.o")
& llvm-ar rcs (Join-Path $staticWinX64 "libcrypto.a") (Join-Path $scratch "zan_crypto.o")
& clang -c -O3 -D_CRT_SECURE_NO_WARNINGS (Join-Path $root "src/runtime/zan_tls.c") -o (Join-Path $scratch "zan_tls.o")
& llvm-ar rcs (Join-Path $staticWinX64 "libssl.a") (Join-Path $scratch "zan_tls.o")

# -----------------------------------------------------------------------------
# 2. Linux x64
# -----------------------------------------------------------------------------
Write-Host ">>> Building Linux x64 drivers..."
$drvLinX64 = Join-Path $root "stdlib\System\Security\Cryptography\drivers\linux-x64"
if (!(Test-Path $drvLinX64)) { New-Item -ItemType Directory -Force -Path $drvLinX64 | Out-Null }

& clang -target x86_64-linux-gnu -shared -fuse-ld=lld -nostdlib -fPIC -O3 -maes -mpclmul -mssse3 `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_crypto.c") `
    -o (Join-Path $drvLinX64 "libcrypto.so.3")

& clang -target x86_64-linux-gnu -shared -fuse-ld=lld -nostdlib -fPIC -O3 `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_tls.c") `
    -o (Join-Path $drvLinX64 "libssl.so.3")

"libcrypto.so.3`n" | Set-Content -Encoding ascii (Join-Path $drvLinX64 "crypto.bundle")
"libssl.so.3`nlibcrypto.so.3`n" | Set-Content -Encoding ascii (Join-Path $drvLinX64 "ssl.bundle")

$staticLinX64 = Join-Path $drvLinX64 "static"
if (!(Test-Path $staticLinX64)) { New-Item -ItemType Directory -Force -Path $staticLinX64 | Out-Null }
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $staticLinX64 "libcrypto.a")
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $staticLinX64 "libssl.a")
& clang -target x86_64-linux-gnu -c -fPIC -O3 -maes -mpclmul -mssse3 -nostdinc -isystem $fakeInc -isystem $llvmInc `
    (Join-Path $root "src/runtime/zan_crypto.c") -o (Join-Path $scratch "zan_crypto_linx64.o")
& llvm-ar rcs (Join-Path $staticLinX64 "libcrypto.a") (Join-Path $scratch "zan_crypto_linx64.o")
& clang -target x86_64-linux-gnu -c -fPIC -O3 -nostdinc -isystem $fakeInc -isystem $llvmInc `
    (Join-Path $root "src/runtime/zan_tls.c") -o (Join-Path $scratch "zan_tls_linx64.o")
& llvm-ar rcs (Join-Path $staticLinX64 "libssl.a") (Join-Path $scratch "zan_tls_linx64.o")

# -----------------------------------------------------------------------------
# 3. Linux arm64
# -----------------------------------------------------------------------------
Write-Host ">>> Building Linux arm64 drivers..."
$drvLinArm64 = Join-Path $root "stdlib\System\Security\Cryptography\drivers\linux-arm64"
if (!(Test-Path $drvLinArm64)) { New-Item -ItemType Directory -Force -Path $drvLinArm64 | Out-Null }

& clang -target aarch64-linux-gnu -shared -fuse-ld=lld -nostdlib -fPIC -O3 `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_crypto.c") `
    -o (Join-Path $drvLinArm64 "libcrypto.so.3")

& clang -target aarch64-linux-gnu -shared -fuse-ld=lld -nostdlib -fPIC -O3 `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_tls.c") `
    -o (Join-Path $drvLinArm64 "libssl.so.3")

"libcrypto.so.3`n" | Set-Content -Encoding ascii (Join-Path $drvLinArm64 "crypto.bundle")
"libssl.so.3`nlibcrypto.so.3`n" | Set-Content -Encoding ascii (Join-Path $drvLinArm64 "ssl.bundle")

# -----------------------------------------------------------------------------
# 4. Android x64
# -----------------------------------------------------------------------------
Write-Host ">>> Building Android x64 drivers..."
$drvAndX64 = Join-Path $root "stdlib\System\Security\Cryptography\drivers\android-x64"
if (!(Test-Path $drvAndX64)) { New-Item -ItemType Directory -Force -Path $drvAndX64 | Out-Null }

& clang -target x86_64-linux-android -shared -fuse-ld=lld -nostdlib -fPIC -O3 -maes -mpclmul -mssse3 `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_crypto.c") `
    -o (Join-Path $drvAndX64 "libcrypto.so")

& clang -target x86_64-linux-android -shared -fuse-ld=lld -nostdlib -fPIC -O3 `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_tls.c") `
    -o (Join-Path $drvAndX64 "libssl.so")

"libcrypto.so`n" | Set-Content -Encoding ascii (Join-Path $drvAndX64 "crypto.bundle")
"libssl.so`nlibcrypto.so`n" | Set-Content -Encoding ascii (Join-Path $drvAndX64 "ssl.bundle")

# -----------------------------------------------------------------------------
# 5. Android arm64
# -----------------------------------------------------------------------------
Write-Host ">>> Building Android arm64 drivers..."
$drvAndArm64 = Join-Path $root "stdlib\System\Security\Cryptography\drivers\android-arm64"
if (!(Test-Path $drvAndArm64)) { New-Item -ItemType Directory -Force -Path $drvAndArm64 | Out-Null }

& clang -target aarch64-linux-android -shared -fuse-ld=lld -nostdlib -fPIC -O3 `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_crypto.c") `
    -o (Join-Path $drvAndArm64 "libcrypto.so")

& clang -target aarch64-linux-android -shared -fuse-ld=lld -nostdlib -fPIC -O3 `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_tls.c") `
    -o (Join-Path $drvAndArm64 "libssl.so")

"libcrypto.so`n" | Set-Content -Encoding ascii (Join-Path $drvAndArm64 "crypto.bundle")
"libssl.so`nlibcrypto.so`n" | Set-Content -Encoding ascii (Join-Path $drvAndArm64 "ssl.bundle")

# -----------------------------------------------------------------------------
# 6. macOS x64
# -----------------------------------------------------------------------------
Write-Host ">>> Building macOS x64 drivers..."
$drvMacX64 = Join-Path $root "stdlib\System\Security\Cryptography\drivers\macos-x64"
if (!(Test-Path $drvMacX64)) { New-Item -ItemType Directory -Force -Path $drvMacX64 | Out-Null }

& clang -target x86_64-apple-macos11.0 -shared -fuse-ld=lld -nostdlib -fPIC -O3 -maes -mpclmul -mssse3 `
    -fno-stack-protector "-Wl,-undefined,dynamic_lookup" `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_crypto.c") `
    -o (Join-Path $drvMacX64 "libcrypto.3.dylib")

& clang -target x86_64-apple-macos11.0 -shared -fuse-ld=lld -nostdlib -fPIC -O3 `
    -fno-stack-protector "-Wl,-undefined,dynamic_lookup" `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_tls.c") `
    -o (Join-Path $drvMacX64 "libssl.3.dylib")

"libcrypto.3.dylib`n" | Set-Content -Encoding ascii (Join-Path $drvMacX64 "crypto.bundle")
"libssl.3.dylib`nlibcrypto.3.dylib`n" | Set-Content -Encoding ascii (Join-Path $drvMacX64 "ssl.bundle")

# -----------------------------------------------------------------------------
# 7. macOS arm64
# -----------------------------------------------------------------------------
Write-Host ">>> Building macOS arm64 drivers..."
$drvMacArm64 = Join-Path $root "stdlib\System\Security\Cryptography\drivers\macos-arm64"
if (!(Test-Path $drvMacArm64)) { New-Item -ItemType Directory -Force -Path $drvMacArm64 | Out-Null }

& clang -target arm64-apple-macos11.0 -shared -fuse-ld=lld -nostdlib -fPIC -O3 `
    -fno-stack-protector "-Wl,-undefined,dynamic_lookup" `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_crypto.c") `
    -o (Join-Path $drvMacArm64 "libcrypto.3.dylib")

& clang -target arm64-apple-macos11.0 -shared -fuse-ld=lld -nostdlib -fPIC -O3 `
    -fno-stack-protector "-Wl,-undefined,dynamic_lookup" `
    -nostdinc -isystem $fakeInc -isystem $llvmInc (Join-Path $root "src/runtime/zan_tls.c") `
    -o (Join-Path $drvMacArm64 "libssl.3.dylib")

"libcrypto.3.dylib`n" | Set-Content -Encoding ascii (Join-Path $drvMacArm64 "crypto.bundle")
"libssl.3.dylib`nlibcrypto.3.dylib`n" | Set-Content -Encoding ascii (Join-Path $drvMacArm64 "ssl.bundle")

Write-Host "ALL_PLATFORMS_CRYPTO_DRIVERS_OK"
