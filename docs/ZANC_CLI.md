# zanc 命令全解 —— 编译 · 发布 · 测试权威参考

一份把 zanc 的命令面说全的文档：全部参数、全部交叉目标、全部环境变量、
各场景的标准配方（可直接复制）、以及反复踩过的坑。会话里"命令搞不清"
时先查这里；本文与 `zanc --help` / `--list-targets` 同步维护，改了参数
解析（src/compiler/main.c）必须同提交更新本文。

--------------------------------------------------------------------------------

## 0. 心智模型

zanc 一条命令 = **编译 + 标准库拉入 + 原生驱动链接**，直接产出可运行文件：

```bash
build/zanc <入口和源文件...> [options] -o <输出>
```

- 不需要先编 .o 再链接；也不用指定 -I/-l（stdlib 与原生驱动按拉入面自动带出）。
- **输入是"文件集合"而不是"工程"**：zanc 只编译命令行上列出的 .zan（加上
  按需拉入的 stdlib/包命名空间）。多文件项目要把 `src/**/*.zan` 全部列上。
- 参数分五类：输出形态（-o/--emit-lib/--publish）、标准库拉入
  （--auto-stdlib/--stdlib-path）、目标平台（--target/--emit-apk）、
  原生链接（-L/--driver-dir/--link-lib）、诊断（--emit-ir/-g/--dump-*）。

## 1. 标准配方（复制即用）

### 1.1 日常开发循环

```bash
# 编译 + 运行（调试默认档，O0）
build/zanc src/main.zan --auto-stdlib -o _scratch/app.exe && _scratch/app.exe

# 多文件项目：显式列出全部源（Git Bash）
build/zanc $(find src -name '*.zan' | sort | tr '\n' ' ') --auto-stdlib -o _scratch/app.exe

# Release 发布档（strip + 优化，默认 -Os）
build/zanc src/main.zan --auto-stdlib --publish -o app.exe
```

### 1.2 GUI 程序

```bash
# 纯逻辑探针（Html.Parse、布局计算等）：与控制台程序无异，无窗口也能跑
build/zanc probe.zan --auto-stdlib -o probe.exe && probe.exe

# 带窗口的 GUI 程序：Windows 上加 windows 子系统
build/zanc $(find src -name '*.zan' | tr '\n' ' ') --auto-stdlib --subsystem windows -o app.exe

# 设计稿入口（.html/.zan 声明式 UI）：**设计稿必须是命令行第一个参数**，
# zanc 从第一份设计文档合成 Main 与控件字段，其后跟全部 .zan 源
build/zanc src/MyApp.html $(find src -name '*.zan' | sort | tr '\n' ' ') \
    --auto-stdlib --subsystem windows -o app.exe
```

### 1.3 交叉编译与移动端

```bash
build/zanc --list-targets                 # 查看全部目标（见 §3）
build/zanc src/main.zan --auto-stdlib --target linux-musl -o app      # musl 静态
build/zanc src/main.zan --auto-stdlib --target android-arm64 --emit-apk \
    --apk-package com.example.app --apk-label 我的应用 -o app.apk
```

`--emit-apk` 一条命令完成 编译→打包 SDLActivity 壳→二进制 AXML→签名
（需要 JAVA_HOME 指向 JDK；`--apk-package`/`--apk-label` 定应用 ID 与名称）。

### 1.4 库与工具通道

```bash
build/zanc src/lib.zan --auto-stdlib --emit-lib -o foo.dll   # 共享库（.so/.dylib 同理）
build/zanc --emit-symbols index.json src/...                 # 类型/成员索引（IDE 补全）后退出
build/zanc --gen-meta meta.json src/...                      # 编译单元元数据 JSON 后退出
```

### 1.5 诊断三板斧（编译器/运行时排查）

```bash
build/zanc probe.zan --auto-stdlib --emit-ir > ir.ll   # 看 IR
build/zanc probe.zan --auto-stdlib --dump-ast          # 看语法树（--dump-tokens 同理）
build/zanc probe.zan --auto-stdlib --time              # 各阶段耗时
```

调试构建（`-g`）自动开启泄漏检测与 ARC 检疫（等于 `--check-leaks
--arc-guard`），程序退出时报告未释放对象并隔离悬垂指针。

## 2. 全参数表（与 --help 一致 + 隐藏参数）

### 输出
| 参数 | 说明 |
|---|---|
| `-o <file>` | 输出文件。**后缀决定产物类型**：`.dll/.so/.dylib`=共享库，`.a/.lib`=静态库，其余=可执行 |
| `--emit-lib` | 强制库输出（无 CRT/入口点） |
| `--publish` | 发布档：优化（默认 -Os）+ strip |
| `-O0..-O3/-Os/-Oz` | 优化档；默认 O0，--publish 默认 Os；-Oz 极限体积（边缘/嵌入式） |
| `-g, --debug` | DWARF 调试信息（强制 O0；自动开 --check-leaks/--arc-guard） |
| `--icon <f.ico>` / `--no-icon` | 嵌入/跳过 Windows 图标 |
| `--embed <p[=n]>` | 把文件/目录烤进可执行资源（可重复） |
| `--emit-apk` | （未列入 help）Android APK 全链路打包；配 `--apk-package`/`--apk-label` |

### 标准库与包
| 参数 | 说明 |
|---|---|
| `--auto-stdlib` | 自动定位 stdlib 与已安装包，**按需拉入**用到的命名空间 |
| `--stdlib-path <dir>` | 指定 stdlib 目录（测试门用；拉入仍按需） |
| `--package-api <url>` / `--package-list-missing` / `--package-install <dir> --package-name <n>` / `--package-scope` / `--package-project` | 包市场：API 地址、缺包诊断、本地包安装与作用域 |

### 目标与运行时
| 参数 | 说明 |
|---|---|
| `--target <name>` | 交叉编译（§3 全表） |
| `--list-targets` | 列出全部目标 |
| `--subsystem <console\|windows>` | PE 子系统；GUI 程序用 windows（仅 Windows） |
| `--async-workers, --mt` | 链入多 worker 协程调度器（worker 数运行期从 ZAN_CO_WORKERS 读，默认=逻辑核数） |
| `--fast-alloc` | 前端 malloc 换每线程小对象分配器（服务端负载，native） |
| `--no-runtime-checks` | 关运行时守卫（如除零） |
| `--strict-runtime` | 守卫失败直接 exit(70)（否则受 ZAN_RT_HARD 控制） |
| `--link-mode <shared\|static>` | 发布时原生驱动链接方式（默认 shared：拷驱动在 exe 旁） |

### 原生链接
| 参数 | 说明 |
|---|---|
| `-L<dir>` / `--libpath <dir>` | 追加原生库搜索目录 |
| `--driver-dir <d>` | 覆盖内建原生驱动目录（测试门指向新编的 zan_gui） |
| `--link-lib <name>` | 额外 `-l<name>` |
| `--link-input <f>` | 额外链接对象/资源/库文件 |

### 编译器内部
| 参数 | 说明 |
|---|---|
| `--dump-tokens` / `--dump-ast` / `--emit-ir` | 前端/IR 诊断 |
| `--check-leaks` / `--arc-guard` | 泄漏报告 / 释放检疫（-g 默认开） |
| `--no-check-leaks` / `--no-arc-guard` | 调试构建里关掉它们 |
| `--no-gen` | 禁用 Zan 脚本代码生成器（bootstrap 用） |
| `--emit-symbols <f>` / `--gen-meta <f>` | 输出索引/元数据后退出（IDE/LSP 用） |
| `--time` / `-q` / `--quiet` | 阶段计时 / 静默进度行 |
| `-D<name>[=v]` | 预处理符号 |
| `@<file>` | 响应文件：从文件读更多参数（一行一个或空白分隔，带空格路径用引号）——**超长命令行/含空格路径用它** |
| `--version/-v`、`--help/-h` | 版本 / 帮助 |

## 3. 交叉编译目标（--list-targets）

| 目标 | triple | 说明 |
|---|---|---|
| win-x64 / win-arm64 | msvc ABI | Windows |
| linux-x64 / linux-arm64 | glibc | Linux |
| linux-musl | musl 静态 | 容器/服务器单文件 |
| linux-riscv64（别名 riscv64） | musl 静态 | RISC-V 64 |
| riscv32（别名 esp32c3） | bare-metal | ESP32-C3/C6；仅 .o 输出 |
| macos-x64 / macos-arm64 | Apple | macOS |
| wasm32 | WASI | 浏览器/Node；EH 已支持；socket 不支持（清晰报错） |
| android-x64 / android-arm64 | bionic API 28+ | 配 --emit-apk 出 APK |
| ohos-x64 / ohos-arm64 | musl 静态 | OpenHarmony |

## 4. 环境变量

### 编译期（zanc）
| 变量 | 作用 |
|---|---|
| `ZAN_LIB_PATH` | 追加原生库搜索目录（win 用 `;` 分隔，unix `:`）——TLS/DB 驱动的导入库不在默认路径时设它 |
| `ZAN_VERBOSE_LINK` | 回显原生链接命令行（查 -L/-l 从哪来） |
| `ZAN_PULLIN_DEBUG` | 打印 stdlib 拉入决策 |
| `ZAN_NO_PULLIN_FILTER` / `ZAN_NO_PRUNE` | 拉入调试：关过滤 / 关裁剪 |
| `ZANC_DUMP_BAD_IR` | LLVM verifier 拒绝时 dump 挂掉函数的 IR（定位 verifier 错误第一工具） |
| `ZANC_TRACE` | 编译器内部跟踪（diag.c 统一出口） |
| `ZAN_TRACE_SYNC` | 打印哪些外部符号把同步/协程运行时拉进来（排查"为什么带上了 co 调度器"） |
| `ZAN_WARN_NARROW` | 收窄转换告警（默认静默） |
| `JAVA_HOME` | --emit-apk 签名用 JDK |
| `ZAN_GENMETA_DUMP` / `ZAN_GEN_REPLY` | genrun 代码生成器调试 |

### 运行期（编译产物读；exe 已定型，env 是唯一现成通道）
| 变量 | 作用 |
|---|---|
| `ZAN_RT_HARD` | 设了它守卫失败即硬退出（默认构建下等效 `--strict-runtime` 的行为） |
| `ZAN_CO_WORKERS` | 多 worker 协程调度器的 worker 数（默认=逻辑核数；需程序以 --mt 构建） |
| `ZAN_GUI_ICONS` | 覆盖图标包目录（发现序：env → exe 旁 icons/ → 内嵌 → stdlib） |
| `ZAN_CEF_*` | CEF 浏览器驱动运行时定位（examples/gui_cef_browser/README.md 全表：RUNTIME/CACHE/MIRROR/LOG/SWITCHES/HELPER* 等一族） |

## 5. 测试命令（本仓库纪律的命令面）

| 层级 | 命令 | 规模/时机 |
|---|---|---|
| 单例 | `cd build && ctest -R <name>` | 最窄验证；新增/受影响用例必跑 |
| 按模式 | `scripts/test.ps1 <tier> -Match "<regex>"` | 再收窄一层 |
| smoke | `scripts/test.ps1 smoke`（75 例） | 编译器/runtime/stdlib 变了才跑 |
| standard | `scripts/test.ps1 standard`（399 例） | 提交编译器/stdlib/runtime 工作前必须过 |
| full | `scripts/test.ps1 full`（1035 例） | 发布门，几十分钟，绝不顺手跑 |
| 模板门 | `cd build && ctest -R templates_build` | 脚手架全部模板逐一编译 |

纪律（AGENTS.md 规则 8 的命令面）：ctest 不是默认验证步骤——平时直接编
受影响程序；**测试运行期间不得并行任何构建**（用例共享 build\zanc.exe
与 stdlib stamp，并发构建会让无关用例成批假红）。

### 新增 conformance 用例的最短路径
1. `tests/conformance/<name>.zan` + 金样 `tests/conformance/<name>.out`
   （金样必须是 **CRLF**——程序 stdout 在 Windows 是 CRLF，LF 金样会全行 diff 不符）；
   放 `tests/conformance/` 即被注册 GLOB 自动收编（helpers/ 子目录不递归，
   多文件输入走 CMakeLists 注册循环的 elseif 链配 ZANC_ARGS）。
2. **重新 configure**（`cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`）
   ——GLOB 在 configure 期展开，不重跑 ctest 看不到新用例。
3. `ctest -R conformance_<name>` 验证。

### 模板门配方（tests/run_templates.cmake 实际做的事）
模板不是"直接编模板树"，而是脚手架后编译：
1. 复制模板树到 build/templates_build/<tpl>/，文本文件内容替换 `{{NAME}}`
   → `TplProbe`；
2. gui 模板把 `src/App.zan`/`src/App.html` 改名为 `src/TplProbe.zan/.html`；
3. 编译命令 = **设计稿 html 在第一个参数** + 全部 src/**/*.zan（排序）+
   `--stdlib-path stdlib` + `--subsystem windows` + `--driver-dir <新编 zan_gui 目录>`。

手工复刻单模板验证时照抄以上配方；只想要"源码能不能编过"的快速信号，
直接 `zanc $(find src -name '*.zan') --auto-stdlib -o out.exe` 也行，
但要记住它**不是**门的完整等价（缺脚手架与 --driver-dir）。

## 6. 已知坑（每条都真踩过）

1. **拉入面决定解析结果**。`--auto-stdlib` 按需拉入：单文件/conformance
   拉入面小；IDE 构建/模板门拉入面大。跨命名空间同名声明的冲突只在
   大拉入面暴露——"单文件全绿、全量构建炸"先查拉入面差异，别急着改代码。
2. **多文件项目必须显式列全源文件**。只传 main.zan 会报"DbContext
   undefined"这类缺类型错——zanc 不扫目录。
3. **设计稿 html 必须是第一个参数**（§1.2）；放后面 = Main/控件字段全缺。
4. **非 publish 链接需要真实导入库**。程序拉了 [DllImport("ssl")] 这类
   原生库时，普通链接要求导入库在搜索路径可达（ZAN_LIB_PATH/-L/驱动目录）；
   只有 `--publish` 静态发布对无主的 DllImport 做 stub 兜底（运行期才报错）。
   门上模板"编译过、链接挂 -lssl"属这一类，先查库可达性。
5. **Windows configure 固定 clang + mozbuild LLVM**。让 CMake 从 PATH 抓
   TDM-GCC 会链不动 zanc，且失败链接会删掉 build\zanc.exe 连坐整个测试套
   （AGENTS.md quickstart / docs/BUILD_TOOLCHAIN.md）。
6. **测试与构建互斥**（§5 纪律）。
7. **新用例要重新 configure** 才进 ctest（§5）。
8. **金样 CRLF**（§5）。
9. **超长命令行用 @file**（§2）：Windows 命令行长度上限下，IDE 发布通道
   就是这么做的；含空格路径加引号。

## 7. 维护

改 `src/compiler/main.c` 的参数解析/加新参数 → 同提交更新本文 §2/§3/§4
与 `zanc --help` 文案（help 文本就在 main.c）。本文与 help 冲突时以
`zanc --help` 为准并修本文。
