# 自包含工具链（Self-Contained Toolchain）

目标：**发布出去的程序只依赖 zan 本身**——开发者拿到 zan 就能 `zanc app.zan -o app.exe`，
**无需安装 clang / gcc / MSVC / Windows SDK**；编译出来的可执行文件也**只依赖操作系统自带的系统库**。

---

## 1. 结论（Windows，已落地并验证）

- 路线：**MinGW ABI**（授权干净、可合法再分发、天然跨平台），非 MSVC ABI。
- `zanc` 现在把目标对象码以 `x86_64-w64-windows-gnu` ABI 生成，并**在进程内直接调用随包携带的 GNU `ld`**
  链接，链接所用的 MinGW-w64 运行时也随包携带。
- 产物：例如 `hello.exe` 仅 **~141 KB**，运行时只导入系统自带的 `msvcrt.dll` / `kernel32.dll`，
  装机即跑，零外部依赖。
- 验证：在 **PATH 中彻底去掉 clang/gcc/ld** 的情况下，`zanc hello.zan -o hello.exe` 仍能编译并正常运行；
  完整 `ctest` **51/51 全过**（含 GUI 用例，覆盖 user32/gdi32 等 `DllImport` 链接路径），ABI 切换无回归。

---

## 2. 改动点

### 2.1 代码生成（`src/compiler/irgen.c`，`zan_irgen_write_obj`）
Windows 上把默认三元组 `x86_64-pc-windows-msvc` 换成 `x86_64-w64-windows-gnu`
（保留宿主架构前缀，仅替换 vendor/abi），使对象码走 MinGW ABI、可被 GNU ld 链接。

### 2.2 链接（`src/compiler/main.c`，链接阶段）
不再 `system("clang ...")`。改为：
1. 用 `GetModuleFileNameA` 定位 `zanc.exe` 所在目录；
2. 在 `<zanc目录>/toolchain/` 下查找随包携带的 `ld.exe` 与 `mingw/lib` sysroot；
3. 用 `_spawnv` 直接调用该 `ld.exe`（不经 shell，天然规避空格/引号问题）；
4. 系统静态库/导入库之间存在循环引用，故用 `--start-group ... --end-group` 包裹（GNU ld 单遍解析）；
5. 保留 `--stack 268435456`（自举编译器深递归需要 256 MB 栈）；`--publish` 时加 `-s` 去符号。
6. **回退**：若未找到随包工具链，退回到系统 `clang --target=x86_64-w64-windows-gnu`（不破坏开发环境）。

链接命令等价于：
```
ld.exe -m i386pep -Bdynamic --stack 268435456 [-s] -o app.exe \
  <lib>/crt2.o <lib>/crtbegin.o -L<lib> app.o \
  --start-group -lmingw32 -lgcc -lmoldname -lmingwex -lmsvcrt \
                -lkernel32 -ladvapi32 -lshell32 -luser32 [其它 DllImport 库] --end-group \
  <lib>/crtend.o
```

### 2.3 打包（`CMakeLists.txt` + `cmake/bundle_toolchain.cmake`）
`zanc` 构建后（POST_BUILD）自动把工具链装配到 `build/toolchain/`：
- `toolchain/ld.exe` —— GNU ld（binutils，约 1.7 MB，可自由再分发）
- `toolchain/mingw/lib/*` —— MinGW-w64 运行时（CRT 启动对象 + 导入/静态库）+ `libgcc.a`

脚本**幂等**：已存在则跳过（运行时约 90 MB，避免每次增量链接重复拷贝）。
可用 `-DZAN_MINGW_ROOT=<path>` 指定 MinGW 根，`-DZAN_BUNDLE_TOOLCHAIN=OFF` 关闭打包。

---

## 3. 体积

| 组成 | 全量（当前） | 可优化到 |
|---|---|---|
| 编译产物（发给最终用户的 exe） | ~141 KB | — |
| `zanc.exe`（静态链 LLVM） | ~37 MB | ~10–15 MB（动态链 LLVM / 只留 x86 后端） |
| 链接器 `ld.exe` | 1.7 MB | —（已是 GNU ld，比 50 MB 的 ld.lld 小得多） |
| MinGW 运行时 `mingw/lib` | ~81.6 MB | ~10–20 MB（只留常用导入库；见下） |
| `libgcc.a` | 7.4 MB | — |
| **工具链包合计** | **~90 MB + zanc** | **~30 MB 量级** |

> 运行时目前保留**完整导入库**以保证任意 `DllImport` 都能链接。后续可做「按需/精简 sysroot」：
> 只保留 CRT + 常用导入库（kernel32/user32/gdi32/advapi32/shell32/ws2_32/winhttp/ole32/…），
> 可把 81.6 MB 降到约 10–20 MB；风险是若用户 `DllImport` 了未收录的 DLL 则需补库。

---

## 4. Linux / macOS 计划（同一原理，尚未落地）

原理一致：**内置 lld + 链接该平台“人人都有”的系统 libc**，用户机器上除 zan 外什么都不装。

- **Linux**：codegen `x86_64-unknown-linux-gnu`；随包 `ld.lld`（或 GNU ld），**动态链接系统 glibc**
  （每台 Linux 都有 → 等于零外部依赖）。极致单文件可选 **musl 静态链接**（随包 musl + crt）。
  链接需 `crt1.o/crti.o/crtn.o` + `-dynamic-linker /lib64/ld-linux-x86-64.so.2` + `-lc`。
- **macOS**：codegen `arm64/x86_64-apple-macos`；随包 `ld64.lld`，链接系统 `libSystem`
  （在 dyld 共享缓存里，无需装 Xcode）；需处理 SDK 的 `.tbd` 存根 + 内置 ad-hoc 代码签名。
  比前两者“娇气”：最低系统版本、`-platform_version`、签名不可省。

实现落点：`src/compiler/crosscomp.c`（各交叉目标复用同一“内置 linker + 该平台常在的系统 libc”模型）。
Linux/macOS 需在对应平台上实机验证（当前开发机为 Windows，只能设计、无法直接跑测）。

---

## 5. 现状小结

- ✅ Windows：自包含链接**已完成并验证**（stripped-PATH 编译通过、51/51 测试通过）。
- ⬜ Linux/macOS：设计已定，待实机落地。
- ⏭ 之后：回到 **M1 协程运行时**（fiber 调度器 / `Task<T>` / async-await 降级 / 事件循环）。
