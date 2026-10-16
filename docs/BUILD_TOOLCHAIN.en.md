# Build Toolchains (Platform-Specific Fixed Configurations)

<p align="left">
  <a href="BUILD_TOOLCHAIN.md">简体中文</a> | <b>English</b>
</p>

Configuring the wrong compiler toolchain causes severe side effects: if the CMake cache records an incompatible compiler, `cmake --build build --target zanc` will fail during linking, causing Ninja to delete output binaries (`build\zanc.exe`), which in turn invalidates all test suites and IDE builds. Use the exact configuration commands documented below.

--------------------------------------------------------------------------------

## Windows x64 (Primary Supported Configuration)

- **C/C++ Compiler**: **clang / clang++** (LLVM bundled with Visual Studio 2022, version 19.x).
- **LLVM Development Package**: `C:\Users\<username>\.mozbuild\clang\lib\cmake\llvm` (Built with MSVC ABI, containing `LLVMConfig.cmake`; the stock VS LLVM lacks this file).
- **Generator**: Ninja.

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
    "-DLLVM_DIR=C:\Users\<username>\.mozbuild\clang\lib\cmake\llvm"
cmake --build build
```

### Things to Avoid:

- **TDM-GCC (`C:\TDM-GCC-64`)**: If present in `PATH`, plain `cmake -B build` will accidentally pick it up. It will inevitably fail in two places: compiling C code under C17 (GCC 10 rejects C23 syntax), and linking MSVC-mangled symbols from LLVM static libraries (`undefined reference to ?...@@Z`).
- `LibXml2 not found` is merely an optional dependency notice and can be safely ignored.

To switch compilers, you must purge the CMake cache:
```powershell
Remove-Item build\CMakeCache.txt -Force
Remove-Item build\CMakeFiles -Recurse -Force
```

--------------------------------------------------------------------------------

## Bootstrap & Cross-Linking

- **Self-hosting bootstrap** (gen0 -> gen3 byte-for-byte reproducibility) uses `scripts\bootstrap.bat`, where gen2 links LLVM IR via **clang** (requires `clang` in `PATH`).
- `zanc`'s target ABI is `x86_64-w64-windows-gnu` (bundled with MinGW toolchain headers). Compiling the native runtime for ZanIDE also requires `clang --target=x86_64-w64-windows-gnu` (see `scripts\build_ide.ps1`). This is separate from the MSVC ABI used to build `zanc` itself.
- Toolchains distribute `ld.lld.exe`, `ld.exe`, and `mingw/`. Native linking prefers `ld.lld.exe`, falling back to GNU `ld.exe` only when absent. Upgrading the compiler requires upgrading linkers simultaneously.

--------------------------------------------------------------------------------

## Linux / macOS

Use system Clang or GCC with **LLVM 15 or newer** development packages. `find_package(LLVM)` will detect it automatically:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**LLVM 15 is a hard requirement**: `irgen` emits LLVM IR assuming opaque pointers. Using LLVM 14 or earlier will cause the LLVM verifier to reject almost all generic types, closures, and field accesses (`Stored value type does not match pointer operand type`). For Ubuntu 22.04 (which ships with `llvm-14-dev`), install `llvm-15-dev clang-15` explicitly:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/usr/lib/llvm-15/cmake \
  -DCMAKE_C_COMPILER=clang-15 -DCMAKE_CXX_COMPILER=clang++-15
```

--------------------------------------------------------------------------------

## Testing Discipline

See `AGENTS.md` Rule 8 and `docs/ZANC_CLI.en.md` §5. Tests and builds must not run concurrently as they share `build\zanc.exe` and standard library build stamps.
