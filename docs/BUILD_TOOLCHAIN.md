# 构建工具链（每个平台一份固定配置）

<p align="left">
  <b>简体中文</b> | <a href="BUILD_TOOLCHAIN.en.md">English</a>
</p>

配错工具链的代价不是"编译失败"这么轻：CMake 缓存一旦被写成错误的编译器，
`cmake --build build --target zanc` 会在链接阶段失败，而 ninja 会先删除输出，
于是 `build\zanc.exe` 直接消失，所有测试与 IDE 构建一起挂掉。所以每个平台的
配置命令固定写在这里，照抄即可，不要临时"找一个能用的编译器"。

## Windows x64（唯一受支持的配置）

- 编译器：**clang / clang++**（Visual Studio 2022 自带的 LLVM，19.x）
- LLVM 开发包：`C:\Users\<用户名>\.mozbuild\clang\lib\cmake\llvm`（MSVC ABI 构建，
  内含 `LLVMConfig.cmake`；VS 自带的那份没有它）
- 生成器：Ninja

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
    "-DLLVM_DIR=C:\Users\<用户名>\.mozbuild\clang\lib\cmake\llvm"
cmake --build build
```

不要用的东西：

- **TDM-GCC（`C:\TDM-GCC-64`）**。它在 PATH 上，`cmake -B build` 不显式指定编译器时
  会被选中，然后两处必然失败：`src/compiler/` 的 C 代码按 C17 编译（gcc 10 拒绝
  C23 写法），以及链接 MSVC ABI 构建的 LLVM 静态库（`undefined reference to
  ?...@@Z` 一大片 MSVC 名字修饰）。注意它只影响**编译 zanc 本身**；组装
  `mingw/` 链接包时 TDM-GCC 与 MSYS2/MinGW 一样是合法候选。
- `LibXml2 not found` 是可选依赖的提示，不影响本项目。

换编译器必须重配：CMake 不允许在既有缓存里改 `CMAKE_C_COMPILER`。

```powershell
Remove-Item build\CMakeCache.txt -Force
Remove-Item build\CMakeFiles -Recurse -Force
```

## 外部工具链怎么被找到

CMake 侧统一走 [`cmake/ZanToolchain.cmake`](../cmake/ZanToolchain.cmake)，
PowerShell 侧统一走 [`scripts/toolchain_paths.ps1`](../scripts/toolchain_paths.ps1)；
其他脚本（`.cmd` / `.sh` / `.py` / `.mjs`）各自只保留与自身平台相关的一小段探测。
优先级一致：**显式覆盖 → PATH → 环境给出的标准安装根**（由
`SystemDrive` / `ProgramFiles` / `ProgramW6432` / `LOCALAPPDATA` / `USERPROFILE`
组合，不写死盘符）。找不到时打印试过的位置和应该设置的变量。

| 用途 | 环境变量 / cache 覆盖 | 自动探测 |
|---|---|---|
| 组装 `mingw/` 链接包（`bundle_toolchain.cmake`、`CMakeLists.txt`） | `ZAN_MINGW_ROOT`（CMake cache 或环境；空 = 自动）、`MINGW_ROOT`、`MINGW_HOME` | PATH 上的 `x86_64-w64-mingw32-gcc` / `gcc`，再枚举 MSYS2 `mingw64/ucrt64/clang64`、TDM-GCC、`mingw64`、scoop、用户目录 |
| `scripts/build_ide.ps1` 的 clang | `ZAN_CLANG`、`ZAN_LLVM_ROOT`、`LLVM_ROOT`、`LLVM_HOME` | PATH，再枚举 `LLVM_DIR` 推导根、`~/.mozbuild/clang`、vswhere 报告的每个 VS 的 `VC\Tools\Llvm\x64`、`Program Files\LLVM` |
| `scripts/publish_ide.ps1` 打包 gdb | `ZAN_GDB_DIR`、`ZAN_GDB`、`MINGW_ROOT`、`MINGW_HOME` | PATH，再枚举 MinGW 根（同上） |
| zan-dap 运行时找 gdb（`src/dap/debugger.c`） | `ZAN_GDB` / `ZAN_GDB_EXE`、`ZAN_GDB_DIR`、`MINGW_ROOT`、`MINGW_HOME` | zan-dap 同目录，再 PATH，再枚举标准 MinGW 根 |
| FreeType 源码（wasm32 / OHOS 驱动） | `ZAN_FREETYPE_ROOT`、`FREETYPE_SRC`、`FREETYPE_INC` | `%USERPROFILE%\firefox\modules\freetype2` 等用户级 checkout |
| HarmonyOS SDK（IDE 发布、`build_gui_ohos.sh`） | `ZAN_OHOS_SDK`、`DEVECO_SDK_HOME`、`ZAN_DEVECO_HOME`、`DEVECO_ROOT` | Windows `%LOCALAPPDATA%\Huawei\DevEcoStudio\sdk`、`%ProgramFiles%\Huawei\DevEco Studio\sdk`；macOS `/Applications/...`、`~/Library/Huawei/...` |
| CEF headers（`cmake/ZanCef.cmake`） | `ZAN_CEF_CDN`（主）、`ZAN_CEF_CDN_FALLBACK`（备）、`ZAN_CEF_HEADERS_DIR`、`ZAN_CEF_API_VERSION`、`ZAN_CEF_CACHE` | 运行时缓存目录；CEF 版本仍从 `CefRuntime.zan` 的固定表解析 |
| release.yml 的 diaguids.lib | `ZAN_DIAGUIDS` | 解压后的 LLVM 目录递归查找，再经 `vswhere` 遍历所有 VS 安装 |

release.yml 的 LLVM 版本不再写在 workflow 里，而是从 `deps/checksums.txt` 中
`# LLVM <version>` 那一行推导，版本与校验和始终同步一处。

## 自举与交叉链接

- 自举闭环（gen0→gen3 字节一致）用 `scripts\bootstrap.bat`，其中 gen2 由 **clang**
  链接 LLVM IR，同样要求 clang 在 PATH 上。
- zanc 自己的链接 ABI 是 `x86_64-w64-windows-gnu`（自带 mingw 工具链包），
  所以给 IDE 编译的原生运行时也用
  `clang --target=x86_64-w64-windows-gnu`（见 `scripts\build_ide.ps1`），
  这跟上面构建 zanc 用的 MSVC ABI 是两条独立的链，不要混用。
- Windows 开发/发布工具链同时分发 `ld.lld.exe`、`ld.exe` 和 `mingw/`；原生链接
  优先 LLVM 的 MinGW/PE 链接器 `ld.lld.exe`，缺少时才回退 GNU `ld.exe`。
  GNU ld 对包含数万独立 COFF 节的发布对象可能同时报 `IMAGE_REL_AMD64_REL32`
  溢出和对象内已定义符号的 `undefined reference`。升级编译器时需连同链接器
  一起更新，只有新的 zanc.exe 而沿用旧工具链并不能修复这类链接失败。

## Linux / macOS

系统 clang 或 gcc + **LLVM 15 或更新**的开发包，`find_package(LLVM)` 能自动找到：

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

LLVM 15 是硬性下限：irgen 按不透明指针（opaque pointers）生成 IR，用 LLVM 14
及更早的类型化指针会在几乎每处泛型/闭包/字段存取上被 verifier 拒绝
（`Stored value type does not match pointer operand type`），不是个别用例的问题。
Ubuntu 22.04 自带的 `llvm-14-dev` 因此不可用，装 `llvm-15-dev clang-15` 并显式指定：

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/usr/lib/llvm-15/cmake \
  -DCMAKE_C_COMPILER=clang-15 -DCMAKE_CXX_COMPILER=clang++-15
```

## 测试分层

见 `AGENTS.md` 第 8 条：`ctest -L smoke`（每次编辑）、`-L standard`（提交前）、
`-L full`（发布）。测试与构建不能并行：它们共享 `build\zanc.exe` 和 stdlib stamp。
