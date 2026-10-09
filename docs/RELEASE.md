# Zan Release & Versioning Specification

This document defines how Zan is versioned and how the self-contained IDE bundle
(the aardio-style "open the IDE and go" deliverable) is built, laid out, named,
and published across Windows, Linux, and macOS.

The release artifact is **an IDE, not a bare compiler**: the user unzips one
archive, launches `ZanIDE`, and can create → develop → build → debug → publish
projects with no external LLVM, linker, SDK, or PATH setup. `zanc` and the other
CLIs ship *inside* the bundle as tools the IDE drives.

---

## 1. Versioning (SemVer)

Zan uses [Semantic Versioning](https://semver.org/) `MAJOR.MINOR.PATCH`:

- **MAJOR** — breaking language/stdlib/ABI or IDE project-format changes.
- **MINOR** — backward-compatible language/stdlib/IDE features.
- **PATCH** — backward-compatible fixes only.
- Pre-releases: `-alpha.N`, `-beta.N`, `-rc.N` (e.g. `0.2.0-rc.1`).
- Build metadata (optional): `+<gitshortsha>` (e.g. `0.2.0+1a2b3c4`).

The compiler, language, stdlib, and IDE share **one** version number per release.

### 1.1 Single source of truth (resolved)

The version is **single-sourced today** — the earlier duplication is gone:

| Location                     | Current value |
|------------------------------|---------------|
| root `VERSION` file          | `0.2.1` (the only place the number is edited) |

CMake reads the root `VERSION` file and generates `build/generated/zan_version.h`
from `cmake/zan_version.h.in`, injecting `#define ZAN_VERSION "…"` into every
binary — `zanc --version`, the LSP `initialize` result and the IDE About box
all report the same string (`main.c` and `lsp_main.c` both use `ZAN_VERSION`).
Tag releases as `v<VERSION>`.

---

## 2. Target platforms

| Platform      | Triple / arch            | IDE binary   | GUI runtime      | Status |
|---------------|--------------------------|--------------|------------------|--------|
| Windows x64   | `x86_64-pc-windows`      | `ZanIDE.exe` | native Win32 GUI runtime statically linked into `ZanIDE.exe` (no DLL shipped) | **Shipping** |
| Linux x64     | `x86_64-linux-gnu`       | `ZanIDE`     | native X11 GUI runtime, statically linked | Planned |
| Linux arm64   | `aarch64-linux-gnu`      | `ZanIDE`     | native X11 GUI runtime, statically linked | Planned |
| macOS arm64   | `aarch64-apple-darwin`   | `ZanIDE`     | native Cocoa GUI runtime, statically linked | Planned |
| macOS x64     | `x86_64-apple-darwin`    | `ZanIDE`     | native Cocoa GUI runtime, statically linked | Planned |

Notes:
- The **CLIs** (`zanc`, `zan-lsp`, `zan-dap`, `zanfmt`, `zandoc`) build
  natively on all three OSes today — `CMakeLists.txt` already has the non-Windows
  link path (`stdc++ m pthread`). Only the **IDE build recipe** (native GUI runtime
  + linking `ZanIDE.zan`) is currently Windows-only and must be ported.
- `zanc` cross-compiles **user programs** (distinct from IDE distribution above)
  from any host to Linux (`linux-x64`/`linux-musl`/`linux-arm64`/`linux-riscv64`,
  static musl ELF — full runtime), Windows (`win-x64`/`win-arm64`) and macOS
  (`macos-x64`/`macos-arm64`), plus `wasm32` (WASI). Linux cross is unrestricted;
  Windows cross links the bundled `win-<arch>/` runtime objects on demand
  (async-socket, sync, timer, embed); WASI carries runtime restrictions
  (no socket-async; single-threaded); macOS cross links async-socket,
  `AtomicInt`/`SharedTable` and the Cocoa GUI dylib from any host — only
  execution on real Mac hardware remains unverified (it is **not** console-only
  anymore). See `docs/platform-targets.md §2` for the authoritative matrix. This
  is orthogonal to which OS the *IDE itself* runs on.

---

## 3. Bundle layout (per platform, identical logical shape)

> Note: the current publish output is `dist\win-x64` — a flat directory with
> **no** `<version>` subdirectory, and **no `VERSION` file written into it**
> (the release number is read from the repo root `VERSION` by
> `publish_ide.ps1`). The layout below is the logical shape for all platforms.

```
zan-ide-<version>-<os>-<arch>/
  ZanIDE[.exe]              # IDE — the main entry point the user launches
                            # (GUI runtime statically linked in; no dll beside it)
  README.txt                # what's inside + how to run
  toolchain/                # everything the compiler needs, all siblings:
    zanc[.exe]              #   the compiler (the IDE resolves it here)
    zan-lsp[.exe]           #   language server (completion/diagnostics)
    zan-dap[.exe]           #   debug adapter (breakpoints/stepping)
    zanfmt[.exe] zandoc[.exe]
    ld.exe | ld.lld | ld64.lld   # bundled linkers (win native: GNU ld.exe)
    zanrt_*.obj             #   flat runtime objects — 12 zanrt_* (io/io_mt/
                            #   sync/timer/sched_msvc/ide/gallery/...) + zan_embed_api.obj
    linux-musl/ linux-arm64/ linux-riscv64/   # musl cross sysroots (all hosts)
    win-x64/ win-arm64/ macos/                # per-target runtime objects
    zan_gui.lib|.a          #   native GUI runtime for user GUI projects
  stdlib/                   # Zan standard library sources (auto-included)
  templates/                # built-in New Project templates (data-driven)
  examples/                 # curated sample projects opened from the IDE
```

Rules:
- **No nested `toolchain/toolchain`.** `zanc` finds its linker/sysroot/runtime
  objects as its own siblings, exactly as in the build tree.
- The IDE needs **no separate GUI runtime DLL**: the native GUI runtime is
  statically linked into
  `ZanIDE.exe` (`scripts/publish_ide.ps1`; `build_ide.ps1` uses the Win32
  backend, `ZAN_GUI_STATIC`).
- `stdlib/`, `templates/`, `examples/` are copied verbatim; they are
  platform-neutral sources. Native driver bundles under
  `stdlib/**/drivers/<os>-<arch>/` are already multi-platform.

---

## 4. Artifact naming & checksums

- Directory / archive base name: `zan-ide-<version>-<os>-<arch>`
  - `<os>` ∈ `win` | `linux` | `macos`; `<arch>` ∈ `x64` | `arm64`.
- Archive format: `.zip` on Windows, `.tar.gz` on Linux/macOS.
  - e.g. `zan-ide-0.2.0-win-x64.zip`, `zan-ide-0.2.0-linux-x64.tar.gz`,
    `zan-ide-0.2.0-macos-arm64.tar.gz`.
- Every release publishes `SHA256SUMS.txt` covering all archives.
- Optional (recommended for GA): Authenticode sign `ZanIDE.exe`/`zanc.exe` on
  Windows; notarize the `.app`/binaries on macOS.

---

## 5. Build recipes

### 5.1 Windows x64 (shipping)

```powershell
# 1. compiler + CLIs + runtime + bundled linker toolchain
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
      -DCMAKE_LINKER=lld-link -DLLVM_DIR=<llvm>/lib/cmake/llvm `
      -DZAN_MINGW_ROOT=C:/TDM-GCC-64
cmake --build build
# 2. IDE (zanc compiles ZanIDE.zan; GUI runtime statically linked in; also compiles the app icon)
powershell -ExecutionPolicy Bypass -File scripts\build_ide.ps1
# 3. assemble the self-contained bundle into dist\win-x64 (bumps VERSION unless -NoBump)
powershell -ExecutionPolicy Bypass -File scripts\publish_ide.ps1 -SkipBuild
```

Prerequisites: LLVM (for `find_package(LLVM)`), `clang`/`llvm-*` on PATH, and
TDM-GCC (bundled ld).

### 5.2 Linux / macOS (planned — recipe to implement)

1. `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` with system clang +
   LLVM → builds `zanc`, `zan-lsp`, `zan-dap`, `zanfmt`, `zandoc`.
2. Build the native GUI runtime for the platform shell (X11 / Cocoa) →
   `zan_gui.a`.
3. `zanc src/ide_zan/ZanIDE.zan -o build/ZanIDE` linking `zan_gui.a`.
4. A `publish_ide.sh` mirroring `publish_ide.ps1` assembles `dist/` with the
   POSIX file names from §3.

These three shell steps are the only remaining work to make the bundle
cross-platform; the compiler/stdlib/templates/examples are already portable.

---

## 6. Release process checklist

1. `publish_ide.ps1` bumps the root `VERSION` automatically
   (`scripts\bump_version.ps1`; pass `-NoBump` to skip). There is no
   `CHANGELOG.md` to update.
2. Green CI on all target platforms (build + `ctest`).
3. Build the bundle on each platform (§5) and smoke-test:
   launch IDE → New Project (each template) → Build → Run → Debug (breakpoint) →
   Publish. The bundle must work on a machine with **no** dev toolchain.
4. Produce archives (§4) + `SHA256SUMS.txt`; sign/notarize if GA.
5. Tag `v<VERSION>`, push, and attach archives to the release.
6. Verify each archive unzips and launches on a clean VM per OS.

---

## 7. Current deliverable vs. gaps

- **Deliverable now:** `zan-ide-<v>-win-x64` — reproducible clean build via §5.1,
  self-contained (`ZanIDE.exe` with the GUI runtime statically linked + `toolchain/` +
  stdlib/templates/examples), no external toolchain needed to run the IDE or
  build user programs.
- **Gaps to close for a multi-platform release:**
  1. Linux/macOS IDE build recipe + `publish_ide.sh` (§5.2).
  2. `SHA256SUMS` + archive packaging step in the publish scripts (§4).
  3. Per-OS clean-VM smoke test in CI (§6.3).

---

## 附录 A：IDE 发布布局（原 IDE_PUBLISH.md，2026-09-26 并入）

The IDE is released as a **self-contained folder** at the canonical location:

```
<repo>\dist\win-x64
```

Other platforms get their own `dist\<platform>` (e.g. `dist\linux-x64`) when
their builds exist; the publish scripts only rebuild the current platform's
folder.

This is the single, clean release output. It contains **only** the files an end
user needs — nothing else should be dropped here. Re-run the publish script to
refresh it rather than hand-editing.

### How to publish

```powershell
powershell -ExecutionPolicy Bypass -File scripts\publish_ide.ps1
```

The script (`scripts/publish_ide.ps1`):

1. Builds the IDE via `scripts/build_ide.ps1` (pass `-SkipBuild` to package the
   existing `build\` artifacts as-is).
2. Wipes and recreates `dist\`.
3. Copies the IDE, the compiler + bundled linker toolchain, companion CLIs,
   the bundled gdb debugger, the standard library, examples, and templates.
4. Writes a `README.txt` describing usage.

### Layout of `dist\`

| Path            | Purpose                                                              |
| --------------- | -------------------------------------------------------------------- |
| `ZanIDE.exe`    | The IDE.                                                             |
| *(none)*        | No GUI runtime DLL ships: the native runtime is statically linked into `ZanIDE.exe` (see `scripts/build_ide.ps1` / `scripts/publish_ide.ps1`). |
| `toolchain\`    | The compiler and everything it links with, as siblings: `zanc.exe`, `zan-lsp.exe`, `zan-dap.exe`, `zanfmt`/`zandoc`, the bundled linker (`ld.exe` + `mingw\` MinGW-w64 runtime), cross sysroots (`linux-musl\` …), runtime objects (`zanrt_io*`, `zanrt_sync*`), `zan_gui.lib`, and `debugger\bin\gdb.exe`. |
| `stdlib\`       | Standard library sources. `zanc` auto-includes what it needs.        |
| `examples\`     | Sample programs for the IDE's Examples pane (optional).              |
| `templates\`    | Built-in New Project templates (data-driven, `template.manifest`).   |
| `tools\`        | Zan tools for the IDE's Tools panel, plus `zan-mcp.exe` — the MCP server for AI clients. |
| `knowledge\`    | Offline knowledge base for the assistant (`symbols.json`, `gallery.json`), generated from the shipped stdlib. |
| `ai\`           | AI onboarding templates (`AGENTS.md`, `skills\`, `mcp.json`/`cursor.mcp.json`/`vscode.mcp.json`) installed into a project by `tools\zan-mcp.exe --init-agent`; placeholders intact here. |
| `docs\`         | `AI_ONBOARDING.md`, `TOOLING.md`, `ai-assist.md`, `MCP_HOSTING.md`, `AI_DEV_INFRASTRUCTURE.md`. |
| `llms.txt`      | AI entry index of the release, generated by `scripts\gen_ai_index.ps1` from what actually shipped. |
| `AI_README.md`  | How AI uses this SDK (Chinese): setup, MCP tool catalog, skills, knowledge base. |
| `README.CN.txt` | Chinese version of `README.txt`. Both texts are generated by `scripts\publish_ide.ps1` and must be updated together. |
| `README.txt`    | Usage notes.                                                          |

### Why everything lives inside the release folder

The IDE resolves the compiler relative to **its own executable directory**
(`toolchain\zanc.exe`), and `zanc` in turn resolves its linker, sysroots, and
runtime objects as **its own siblings** in that same `toolchain\` folder, and
its stdlib relative to its executable (`<exe_dir>/stdlib` or
`<exe_dir>/../stdlib`).

Because `ZanIDE.exe`, `toolchain\` and `stdlib\` all travel together in
`dist\`, the release is fully relocatable: copy the folder anywhere and the
IDE still finds the compiler, linker, and standard library — no absolute
paths, no registry, no environment variables.

### Runtime prerequisite

**None for normal use.** `zanc` emits object code and links it in-process via
the bundled GNU `ld` + MinGW-w64 runtime in `toolchain\` (see
本文《附录 B：自包含工具链》), and `zan-dap` debugs with the bundled
`toolchain\debugger\bin\gdb.exe`. No LLVM/clang, MSVC, or system gdb install
is required on the target machine.

Fallbacks: if the linker files under `toolchain\` are removed, `zanc` falls
back to a system `clang` on `PATH`; if the bundled gdb is missing, `zan-dap`
falls back to a system/known gdb (`ZAN_GDB`).

---

## 附录 B：自包含工具链（原 SELF_CONTAINED_TOOLCHAIN.md，2026-09-26 并入）

目标：**发布出去的程序只依赖 zan 本身**——开发者拿到 zan 就能 `zanc app.zan -o app.exe`，
**无需安装 clang / gcc / MSVC / Windows SDK**；编译出来的可执行文件也**只依赖操作系统自带的系统库**。

> **路线文档关系**：本文件是自包含工具链的技术设计与落地状态。旧顶层计划
> `PRODUCTION_PLAN.md` / `EXECUTION_PLAN.md`（其"阶段 5（跨平台）"以本文件为
> 技术依据）与历史里程碑 `ROADMAP.md` 均已归档到 `docs/archive/`。

---

### 1. 结论（Windows，已落地并验证）

- 路线：**MinGW ABI**（授权干净、可合法再分发、天然跨平台），非 MSVC ABI。
- `zanc` 现在把目标对象码以 `x86_64-w64-windows-gnu` ABI 生成，并**在进程内直接调用随包携带的 GNU `ld`**
  链接，链接所用的 MinGW-w64 运行时也随包携带。
- 产物：例如 `hello.exe` 仅 **~141 KB**，运行时只导入系统自带的 `msvcrt.dll` / `kernel32.dll`，
  装机即跑，零外部依赖。
- 验证：在 **PATH 中彻底去掉 clang/gcc/ld** 的情况下，`zanc hello.zan -o hello.exe` 仍能编译并正常运行；
  ctest 现按三档套件运行（`smoke` 103 / `standard` 462 / `full` 1170）——文中"完整 ctest **51/51 全过**"
  是 ABI 切换当时的历史快照（含 GUI 用例，覆盖 user32/gdi32 等 `DllImport` 链接路径），现套件规模已大幅增长。

---

### 2. 改动点

#### 2.1 代码生成（`src/compiler/irgen.c`，`zan_irgen_write_obj`）
Windows 上把默认三元组 `x86_64-pc-windows-msvc` 换成 `x86_64-w64-windows-gnu`
（保留宿主架构前缀，仅替换 vendor/abi），使对象码走 MinGW ABI、可被 GNU ld 链接。

#### 2.2 链接（`src/compiler/main.c`，链接阶段）
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

#### 2.3 打包（`CMakeLists.txt` + `cmake/bundle_toolchain.cmake`）
`zanc` 构建后（POST_BUILD）自动把工具链装配到 `build/toolchain/`：
- `toolchain/ld.exe` —— GNU ld（binutils，约 1.7 MB，可自由再分发）
- `toolchain/mingw/lib/*` —— MinGW-w64 运行时（CRT 启动对象 + 导入/静态库）+ `libgcc.a`

脚本**幂等**：已存在则跳过（运行时约 90 MB，避免每次增量链接重复拷贝）。
可用 `-DZAN_MINGW_ROOT=<path>` 指定 MinGW 根，`-DZAN_BUNDLE_TOOLCHAIN=OFF` 关闭打包。

---

### 3. 体积

| 组成 | 全量（当前） | 可优化到 |
|---|---|---|
| 编译产物（发给最终用户的 exe） | ~141 KB | — |
| `zanc.exe`（静态链 LLVM） | ~49.8 MB | ~10–15 MB（动态链 LLVM / 只留 x86 后端） |
| 链接器 `ld.exe` | 1.7 MB | —（已是 GNU ld，比 50 MB 的 ld.lld 小得多） |
| MinGW 运行时 `mingw/lib` | ~81.6 MB | ~10–20 MB（只留常用导入库；见下） |
| `libgcc.a` | 7.4 MB | — |
| **工具链包合计** | **~189 MB**（含跨平台 musl sysroot、CLI 工具与 `zan_gui.lib`） | **~30 MB 量级** |

> 运行时目前保留**完整导入库**以保证任意 `DllImport` 都能链接。后续可做「按需/精简 sysroot」：
> 只保留 CRT + 常用导入库（kernel32/user32/gdi32/advapi32/shell32/ws2_32/winhttp/ole32/…），
> 可把 81.6 MB 降到约 10–20 MB；风险是若用户 `DllImport` 了未收录的 DLL 则需补库。

---

### 4. Linux / macOS（同一原理，已落地）

原理一致：**内置 lld + 链接该平台”人人都有”的系统 libc**，用户机器上除 zan 外什么都不装。

- **Linux**：✅ 已落地。交叉默认产出 **musl 静态 ELF**（`ld.lld -static` + 随包
  `toolchain/linux-{musl,arm64,riscv64}` musl sysroot，含 `crt1.o/crti.o/crtn.o` 与 `libc.a`），
  **零外部依赖**，任何宿主（Windows/macOS/Linux）都能产出。
- **macOS**：✅ 已落地。随包 `ld64.lld` + `toolchain/macos/libSystem.tbd`（MIT 符号存根，
  无需 Apple SDK/Xcode）+ Mach-O 运行时对象（`macos/<arch>/zanrt_{io,io_mt,sync,timer}.o`）；
  arm64 输出由链接器做 **ad-hoc 代码签名**（`LC_CODE_SIGNATURE`）。Cocoa GUI dylib
  （`libzan_gui.dylib`）亦已签入。残留真项：**实机运行验证**与发布分发用的 **Developer ID 公证**。

实现落点：`src/compiler/crosscomp.c`（各交叉目标复用同一”内置 linker + 该平台常在的系统 libc”模型）。
macOS 实机验证需在 Mac 上执行（当前开发机为 Windows，只做链接/结构验证）。

---

### 5. 现状小结

- ✅ Windows：自包含链接**已完成并验证**（stripped-PATH 编译通过，三档 ctest 套件通过）。
- ✅ Linux：静态 musl 自包含链接**已落地**（`ld.lld -static` + 随包 musl sysroot，零外部依赖）。
- ✅ macOS：`ld64.lld` + `libSystem.tbd` + ad-hoc 签名**已落地**；仅剩实机运行验证与 Developer ID 公证。
