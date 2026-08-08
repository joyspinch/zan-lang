# 构建工具链（每个平台一份固定配置）

配错工具链的代价不是"编译失败"这么轻：CMake 缓存一旦被写成错误的编译器，
`cmake --build build --target zanc` 会在链接阶段失败，而 ninja 会先删除输出，
于是 `build\zanc.exe` 直接消失，所有测试与 IDE 构建一起挂掉。所以每个平台的
配置命令固定写在这里，照抄即可，不要临时"找一个能用的编译器"。

## Windows x64（唯一受支持的配置）

- 编译器：**clang / clang++**（Visual Studio 2022 自带的 LLVM，19.x）
- LLVM 开发包：`C:\Users\QQ\.mozbuild\clang\lib\cmake\llvm`（MSVC ABI 构建，
  内含 `LLVMConfig.cmake`；VS 自带的那份没有它）
- 生成器：Ninja

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
    "-DLLVM_DIR=C:\Users\QQ\.mozbuild\clang\lib\cmake\llvm"
cmake --build build
```

不要用的东西：

- **TDM-GCC（`C:\TDM-GCC-64`）**。它在 PATH 上，`cmake -B build` 不显式指定编译器时
  会被选中，然后两处必然失败：`src/compiler/` 的 C 代码按 C17 编译（gcc 10 拒绝
  C23 写法），以及链接 MSVC ABI 构建的 LLVM 静态库（`undefined reference to
  ?...@@Z` 一大片 MSVC 名字修饰）。
- `LibXml2 not found` 是可选依赖的提示，不影响本项目。

换编译器必须重配：CMake 不允许在既有缓存里改 `CMAKE_C_COMPILER`。

```powershell
Remove-Item build\CMakeCache.txt -Force
Remove-Item build\CMakeFiles -Recurse -Force
```

## 自举与交叉链接

- 自举闭环（gen0→gen3 字节一致）用 `scripts\bootstrap.bat`，其中 gen2 由 **clang**
  链接 LLVM IR，同样要求 clang 在 PATH 上。
- zanc 自己的链接 ABI 是 `x86_64-w64-windows-gnu`（自带 mingw 工具链包），
  所以给 IDE 编译的原生运行时也用
  `clang --target=x86_64-w64-windows-gnu`（见 `scripts\build_ide.ps1`），
  这跟上面构建 zanc 用的 MSVC ABI 是两条独立的链，不要混用。

## Linux / macOS

系统 clang 或 gcc + 系统 LLVM 开发包即可，`find_package(LLVM)` 能自动找到：

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 测试分层

见 `AGENTS.md` 第 8 条：`ctest -L smoke`（每次编辑）、`-L standard`（提交前）、
`-L full`（发布）。测试与构建不能并行：它们共享 `build\zanc.exe` 和 stdlib stamp。
