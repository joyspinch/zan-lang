# zanc 命令全解 —— 编译 · 发布 · 测试权威参考

<p align="left">
  <b>简体中文</b> | <a href="ZANC_CLI.en.md">English</a>
</p>

一份把 zanc 的命令面说全的文档：全部参数、全部交叉目标、全部环境变量、
各场景的标准配方（可直接复制）、以及反复踩过的坑。会话里"命令搞不清"
时先查这里；本文与 `zanc --help` / `--list-targets` 同步维护，改了参数
解析（`src/compiler/main_options_parse.c`）或帮助文本
（`src/compiler/main_options.c`）必须同提交更新本文。

--------------------------------------------------------------------------------

## 0. 心智模型

zanc 一条命令 = **编译 + 标准库拉入 + 原生驱动链接**，直接产出可运行文件：

```bash
build/zanc <入口和源文件...> [options] -o <输出>
```

- 不需要先编 .o 再链接；也不用指定 -I/-l（stdlib 默认自动按需拉入，原生驱动按拉入面自动带出）。
- **输入是"文件集合"而不是"工程"**：zanc 只编译命令行上列出的 .zan（加上
  按需拉入的 stdlib/包命名空间）。多文件项目要把 `src/**/*.zan` 全部列上——
  或者**直接给目录**（`zanc src -o app.exe`），编译器自己递归展开 `*.zan`。
- 参数分五组（与 `zanc --help` 分组一致）：**常用**（输入/输出/优化/目标/标准库/
  原生链接）、**发布**（--publish/--strip-symbols/字符串混淆）、**调试**
  （-g/泄漏与运行时守卫/诊断输出）、**打包**（--emit-apk/--emit-ipa 及其身份参数）、
  **内部（未稳定）**（-ferror-limit/包市场安装通道）。

## 1. 标准配方（复制即用）

### 1.1 日常开发循环

```bash
# 编译 + 运行（默认自动拉入标准库，默认 -O2；-g 才强制 O0）
build/zanc src/main.zan -o _scratch/app.exe && _scratch/app.exe

# 多文件项目：把目录交给编译器（递归展开 *.zan，按路径排序，命令行恒长）
build/zanc src -o _scratch/app.exe

# 多文件项目：显式列出全部源（Git Bash）
build/zanc $(find src -name '*.zan' | sort | tr '\n' ' ') -o _scratch/app.exe

# Release 发布档（strip + 优化，默认 -Os）
build/zanc src/main.zan --publish -o app.exe
```

### 1.2 GUI 程序

```bash
# 纯逻辑探针（Html.Parse、布局计算等）：与控制台程序无异，无窗口也能跑
build/zanc probe.zan -o probe.exe && probe.exe

# 带窗口的 GUI 程序：Windows 上自动推断 windows 子系统（无黑框终端，无需手动指定 --subsystem）
build/zanc src -o app.exe

# 编译期 HTML 入口已移除：.html/.htm 位置参数会被拒绝
# （"the compile-time HTML channel has been removed"）。界面用 Gui 组件搭骨架，
# 运行期 Gui.App.LoadHtml 装载标记；要随 exe 发布就用 --embed 烤进去。
build/zanc src -o app.exe
```

### 1.3 交叉编译与移动端

```bash
build/zanc --list-targets                 # 查看全部目标（见 §3）
build/zanc src/main.zan --auto-stdlib --target linux-musl -o app      # musl 静态
build/zanc src/main.zan --auto-stdlib --target android-arm64 --emit-apk app.apk \
    --apk-package com.example.app --apk-label 我的应用 -o app.so
build/zanc src/main.zan --auto-stdlib --target ios-arm64 -o app.ipa
# 或显式指定参数生成免证书可安装的 iOS IPA：
build/zanc src/main.zan --auto-stdlib --target ios-arm64 --emit-ipa app.ipa \
    --ipa-bundle-id com.example.app --ipa-name 我的应用
```

`--emit-apk` 一条命令完成 编译→打包 NativeActivity 壳→二进制 AXML→签名
（需要 JAVA_HOME 指向 JDK；`--apk-package`/`--apk-label` 定应用 ID 与名称）。

**工程键（zan.proj）在所有构建路径一致生效**：`androidPackage`/
`androidLabel`/`androidPermissions`（逗号分隔，裸名自动补
`android.permission.` 前缀）只在缺省时被 CLI 覆盖项顶掉；`skins`/`skinlist`
决定 GUI 皮肤基线烤入——`skins = 1` 时烤 `skinlist` 列出的 pack（逗号
分隔，`-` 表示清空），否则只烤基线 dark+light（base.css 恒烤）。裸 CLI
与 `--emit-apk` 均按此收敛，与 IDE 暂存发布的 CopyThemeSkins 同规则。

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

`--emit-ir` 落出的 IR **自述目标特性**（头部注释 `; zanc-target-cpu:` /
`; zanc-target-features:`），stderr 同时打印对应的 `llc` 命令行。消费方必须
用同样的特性，否则 256 位向量路径在 generic CPU 下无法合法化
（`LLVM ERROR: Do not know how to split this operator's operand!`）：

```bash
llc -mcpu=x86-64 -mattr=+sse3,+ssse3,+sse4.1,+sse4.2,+crc32,+aes,+avx,+avx2,+fma,+bmi \
    -filetype=obj ir.ll -o ir.o
```

（治本——让 IR 在 generic CPU 下也能合法化——尚未做，见项目台账 A438。）

调试构建（`-g`）会强制 `-O0`，并在**未同时给 `--publish`** 时自动开启
泄漏检测与 ARC 检疫（等于 `--check-leaks --arc-guard`），程序退出时报告
未释放对象并隔离悬垂指针。`-g` **不会**自动开启 `--strict-runtime`。
IDE 的 F5 调试是显式传 `-g --arc-guard --check-leaks --strict-runtime`；
项目的 `strictMemory = 1` 让普通构建/运行也使用这组诊断。发布独立选择模式：
Release 使用 `--publish`，Debug 使用 `-g --no-arc-guard --no-check-leaks`。
`--publish` 下这两个诊断默认关闭，但 over-release 网默认开启（除非显式
给了 `--arc-guard` 或 `--no-arc-guard`）；`-g --publish` 仍会按发布链路
strip，通常拿不到可用调试信息，不建议组合。
ARC 检疫会保留已释放的块，内存占用随分配量增长，因此只用于诊断运行，
本地严格内存选项不覆盖任何平台的发布模式。

泄漏排查时再设环境变量 `ZAN_ARC_TRACE=1` 运行该程序，可打印每笔
ARC 事件（alloc/retain/release/release-dyn，含对象地址、分配站点索引、
事件后引用计数、调用点返回地址）；`--check-leaks` 构建才带此探针。
注意泄漏报表的 file:line 站点名按分配形状共享槽位（`-g` 才按
file:line 键控），同形状分配点会互相顶掉名字——定位持有人时以
trace 的事件序为准，不要顺着报表的站点名硬找。

## 2. 全参数表（与 `zanc --help` 同源）

> 分组与 `zanc --help` 一致：**常用 / 发布 / 调试 / 打包 / 内部（未稳定）**。
> 默认值与联动关系以 `src/compiler/main_options_parse.c` 的解析和
> `src/compiler/main.c` 的落值为准；标「(内部/未稳定)」的不是公共契约。

### 2.1 常用（Common）
| 参数 | 取值 / 默认 | 说明 |
|---|---|---|
| `-o <file>` | — | 输出文件。**后缀决定产物类型**：`.dll/.so/.dylib`=共享库，`.a/.lib`=静态库，其余=可执行 |
| `--emit-lib` | 关 | 强制库输出（无 CRT/入口点）；与 `-o` 后缀推断并存时后缀仍决定 shared/static |
| `-O0..-O3/-Os/-Oz` | 默认 `-O2` | 优化档；`--publish` 默认 `-Os`；`-Oz` 极限体积（边缘/嵌入式）。显式 `-O` 覆盖 `--publish` 默认，但 `-g` 总是强制 `-O0` |
| `--target <name>` | 本机 | 交叉编译（§3 全表） |
| `--list-targets` | — | 列出全部目标后退出 |
| `--subsystem <console\|windows>` | 自动 | PE 子系统（仅 Windows）；GUI 程序自动推断 windows |
| `--stdlib-path <dir>` | 自动定位 | 指定 stdlib 根目录（测试门用；拉入仍按需） |
| `--auto-stdlib` | **开** | 自动定位 stdlib 与已安装包，**按需拉入**用到的命名空间；显式传等于重申默认 |
| `--no-stdlib` | — | 关自动 stdlib **和**包发现；`--no-packages` 只关包 |
| `--no-packages` | — | 只关包发现（stdlib 拉入照常）。回归锁 `conformance_gen_pkg_isolation`：包内库级调用不牵连父编译 |
| `--fast-alloc` | native 默认开 | 小对象分配器（`rt_mem.c`）；Apple（macOS/iOS）与交叉目标不链；目标无分配器对象时告警并回退 CRT |
| `--no-fast-alloc` | — | 原生回退 CRT malloc——排查堆问题、或块需交还外部 CRT/驱动边界时用；与 `--fast-alloc` 同现时后写的生效 |
| `-L<dir>` / `--libpath <dir>` | — | 追加原生库搜索目录（可重复） |
| `--driver-dir <d>` | 内建目录 | 覆盖内建原生驱动目录（测试门指向新编的 zan_gui） |
| `--link-lib <name>` | — | 额外 `-l<name>`；名字只允许 `[A-Za-z0-9._+-]`，路径请用 `-L`/`--libpath` |
| `--link-input <f>` | — | 额外链接对象/资源/库文件（可重复） |
| `--embed <p[=n]>` | — | 把文件/目录烤进可执行资源（可重复）；运行期读取**内嵌优先**——烤进的副本是权威，`File.ReadAllText/ReadAllBytes/ReadBytes/GetSize/Exists` 命中内嵌直接返回，盘上同名文件不再覆盖它（发布后界面/资源不落盘、不被旁路文件替换）；资源在 exe 内**加密存放**（压缩后按内容派生密钥流整体 XOR，`strings` 扒不到明文，读取时内存解密、不落临时文件；防静态提取，不对抗逆向专家）；GUI 程序自带 `skins/` 暂存时，仅当目录内含 base.css（完整替身）才跳过 stdlib 皮肤基线自动内嵌，pack-only 暂存照常内嵌基线且暂存包赢重名 |
| `--icon <f>` | 内建图标 | Windows 可执行图标；`--emit-ipa` 也把它当 IPA 图标（缺省按 `assets/icon.png`、`assets/icon.ico`、`icon.png`、`icon.ico` 探测）；嵌入失败告警并回退内建（`--publish` 未给 `--default-icon` 时则无图标） |
| `--no-icon` | — | 不嵌入任何图标（跳过 `--icon` 与内建默认）；仅 Windows PE |
| `--default-icon` | — | 显式嵌入内建图标；只有 `--publish` 需要它（非发布档本来就嵌内建图标） |
| `--quiet, -q` | — | 静默 stdout 进度行（驱动装运、APK 打包、`Compiled N files` 汇总） |
| `--time` | — | 打印各阶段耗时与规模统计 |
| `-D<name>[=v]` | — | 预处理符号 |
| `@<file>` | — | 响应文件（一行一个或空白分隔，带空格路径用引号，`#` 起注释，最多嵌套 8 层）——**超长命令行/含空格路径用它** |
| （位置参数，文件/目录） | — | `.zan` 文件，或**目录**（递归展开 `*.zan`，按路径排序）。目录输入跳过 `.` 前缀与 `.git/build/publish/bin/obj/node_modules/_scratch/dist/out/vendor/.zan-packages` 等目录；目录里没有 `.zan` 报 "no .zan source files under '...'"。**只收 `.zan`**：显式给 `.html`/`.htm` 会被 "the compile-time HTML channel has been removed" 拒绝——界面用 Gui 组件搭骨架、运行期 `Gui.App.LoadHtml` 装载标记（内嵌资源走 `--embed`） |
| （无旗标，按目标自动） | — | Windows/Linux/macOS 的 x64/arm64 目标自动链入多 worker 协程调度器（`zanrt_io_mt`）；worker 数解析顺序：`System.Threading.AsyncRuntime.SetWorkers`（Main 内设置）→ `ZAN_CO_WORKERS` 环境变量 → 逻辑核数。IO 分片与同步完成快路径同理由 `AsyncRuntime` 优先于 `ZAN_IO_SHARDS`/`ZAN_IO_SYNCFAST` |
| （默认，native） | — | Windows/Linux 原生默认链入小对象分配器（`rt_mem.c`：≤2048B 尺寸分级无锁缓存、跨线程 free 分 8 条带，`ld --wrap` 全图接管 malloc/free/calloc/realloc）。Apple 原生不链（ld64 无 `--wrap`）；Android 交叉永不链（bionic TLS bootstrap 崩溃）；linux-musl 交叉 sysroot 有对象即自动链。Windows 链接自动改走捆绑 GNU ld（lld 的 PE `--wrap` 改写导入槽，程序加载即死 exit 127）。实测 win-x64：spawn k=1 -26%~-31%，k=32 分片后不劣。调试：`ZAN_LINK_ECHO=1` 回显链接命令 |
| `--version, -v` / `--help, -h` | — | 版本 / 帮助 |

### 2.2 发布（Publish）
| 参数 | 取值 / 默认 | 说明 |
|---|---|---|
| `--publish` | 关 | 发布档：默认 `-Os`、链接期 strip（同 `--strip-symbols`）、启用 shard 模块优化；`--check-leaks`/`--arc-guard` 默认关（显式传入仍生效）；**over-release 网默认开**（未显式给 `--arc-guard`/`--no-arc-guard` 时）；内建图标默认不嵌（`--default-icon` 可显式开启） |
| `--strip-symbols` | 关 | 链接期 `-s` + ICF + 链接优化；`--publish` 已隐含；单独使用时只加 strip、不改优化档 |
| `--link-mode <shared\|static>` | shared | 发布时原生驱动链接方式：shared 拷驱动到 exe 旁，static 链进 exe |
| `--obfuscate-strings` | 关 | XOR 混淆用户字符串字面量（非 `-g` 时顺带裁剪 stdlib 函数体）；CLI 优先于环境变量 `ZAN_OBF=1`（内部/未稳定） |
| `--no-obfuscate-strings` | — | 强制关闭；与 `--obfuscate-strings` 同现时后写的生效；环境变量 `ZAN_NO_OBF=1` 同效（内部/未稳定） |
| `--hide-diagnostics` | 关 | 运行时守卫报告里的 `file:line:col` 换成 `#站点号`（编译期诊断不变）（内部/未稳定） |
| `--emit-site-map <f>` | — | 落盘 `#站点号 -> file:line:col` 映射，配合 `--hide-diagnostics` 还原位置（内部/未稳定） |

### 2.3 调试（Debug）
| 参数 | 取值 / 默认 | 说明 |
|---|---|---|
| `-g, --debug` | 关 | DWARF 调试信息；强制 `-O0`；**未同时给 `--publish`** 时自动开 `--check-leaks`+`--arc-guard`。**不会**自动开 `--strict-runtime`（IDE F5 是显式传的）。与 `--publish` 同用会按发布链路 strip，通常拿不到可用调试信息，不建议组合 |
| `--check-leaks` | `-g` 且非 publish | 程序退出时报告未释放对象 |
| `--no-check-leaks` | — | 关闭泄漏报告 |
| `--arc-guard` | `-g` 且非 publish | 释放检疫：保留已释放块并捕获悬垂 retain/release（内存随分配量增长，只用于诊断）；显式传入时在 `--publish` 下也生效 |
| `--no-arc-guard` | — | 关检疫，同时关掉 `--publish` 默认的 over-release 网 |
| `--runtime-checks` | **开** | 启用运行时守卫（如数组/切片越界、除零）；发布档也默认开 |
| `--no-runtime-checks` | — | 关运行时守卫；与 `--runtime-checks` 同现时后写的生效 |
| `--checked` | 关 | 全局 checked 算术（C# `/checked+` 等价）：全部整型算术溢出抛 `OverflowException`（含 `-` 取负、`++`/`--`、窄化强转）；`unchecked(){ }` 块局部豁免。默认（关）为 C# 默认的 unchecked 环绕语义。钉：`case_checked_arith_default`/`case_checked_arith_flag` |
| `--strict-runtime` | 关 | 守卫失败直接 `exit(70)`；`ZAN_RT_HARD` 非空且非 `0` 等效；`ZAN_RT_HARD=0` 显式压回软模式 |
| `--deny-warnings` | 关 | 将编译警告视为致命错误（阻断构建） |
| `--warn-level <none\|default\|error>` | `default` | `none` 不打印警告；`default` 正常；`error` 等同 `--deny-warnings`；非法值报错退出 2 |
| `--suppress <list>` | — | 按编号抑制**警告**（逗号/空格分隔，字母前缀可省，如 `W0003,0003`）；与 `--deny-warnings`/`--warn-level=error` 同用时警告已升级为错误，不再被抑制 |
| `--dump-tokens` | — | 词法 token 后退出 |
| `--dump-ast` | — | 打印语法树后继续编译 |
| `--emit-ir` | — | IR 到 stdout 后退出，不链接 |
| `--emit-symbols <f>` / `--gen-meta <f>` | — | 输出索引/元数据后退出（IDE/LSP 用） |
| `--api <symbol>` | — | 从知识库检索 API 符号并打印签名与位置（无需编译，极速响应） |

### 2.4 打包（Packaging）
| 参数 | 取值 / 默认 | 说明 |
|---|---|---|
| `--emit-apk <f>` | — | Android APK 全链路打包；**要求** `--target android-x64` 或 `android-arm64`；按共享库编译（`-o` 给 libmain 的临时路径，打包后删除，最终产物是 `<f>`）；签名需要 Java（`JAVA_HOME` 或 PATH） |
| `--apk-package <id>` | `dev.zan.<入口名>` | APK 包名；CLI 优先于 zan.proj `androidPackage` |
| `--apk-label <name>` | 入口文件名 | APK 显示名；CLI 优先于 zan.proj `androidLabel` |
| `--emit-ipa <f>` | — | iOS IPA 全链路打包；**要求** `--target ios-arm64`；`-o <name>.ipa` 也会自动开启；图标取 `--icon` 或 `assets/icon.png` 等探测 |
| `--ipa-bundle-id <id>` | `dev.zan.<app 名>` | IPA bundle identifier |
| `--ipa-name <name>` | 由 `.ipa` 路径推导 | IPA 内 App 名 |

### 2.5 内部（未稳定）
| 参数 | 取值 / 默认 | 说明 |
|---|---|---|
| `-ferror-limit=N` | 100 | 最多报 N 条错误后停止；`0` = 不限（内部/未稳定） |
| `--package-api <url>` | — | 配置包市场 API（HTTPS；localhost 允许 HTTP）；必须与 `--package-install` 同用（内部/未稳定） |
| `--package-list-missing` | — | 扫描输入并打印缺失命名空间诊断；有缺失退出 3（内部/未稳定） |
| `--package-install <dir>` | — | 本地包安装；必须配 `--package-name`（内部/未稳定） |
| `--package-name <name>` | — | 安装包名（内部/未稳定） |
| `--package-scope <project\|global>` | `project` | 安装作用域（内部/未稳定） |
| `--package-project <dir>` | `.` | 包发现/项目级安装的工程根（内部/未稳定） |

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
| ios-arm64 | Apple iOS | iPhone/iPad；配 --emit-ipa 或 -o <app.ipa> 出免证书 Ad-hoc 签名 IPA |
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
| `ZAN_OBF` / `ZAN_NO_OBF` | `=1` 时默认开/关字符串混淆；CLI `--obfuscate-strings`/`--no-obfuscate-strings` 优先 |
| `ZAN_RT_HARD` | 非空且非 `0`：守卫失败硬退出；`=0`：强制软模式（可覆盖 `--strict-runtime`） |
| `JAVA_HOME` | --emit-apk 签名用 JDK |
| `ZAN_GENMETA_DUMP` / `ZAN_GEN_REPLY` | genrun 代码生成器调试 |

### 运行期（编译产物读；exe 已定型，env 是唯一现成通道）
| 变量 | 作用 |
|---|---|
| `ZAN_RT_HARD` | 非空且非 `0` 时守卫失败硬退出（等效 `--strict-runtime`）；`=0` 显式强制软模式 |
| `ZAN_CO_WORKERS` | 多 worker 协程调度器的 worker 数（native 64-bit 目标默认即多 worker 驱动，无需任何构建旗标；默认=min(逻辑核数，4)，见 `AsyncRuntime.SetWorkers` 优先） |
| `ZAN_CO_QUANTUM_MS` | 协程协作式抢占的时间片（毫秒，默认 2，0=关闭）：编译器在 async 函数的每个循环回边埋 `zan_co_poll`，超片的计算密集帧自动重排到队尾，不再饿死定时器/IO 回调/兄弟协程；片长记账走微秒级单调钟（Windows QPC，POSIX CLOCK_MONOTONIC），2ms 就是真 2ms |
| `ZAN_CO_STACK` | Task.Spawn/Run 有栈 fiber 的栈大小（默认 128KB，范围 64KB~16MB） |
| `ZAN_GUI_ICONS` | 覆盖图标包目录（发现序：env → exe 旁 icons/ → 内嵌 → stdlib） |
| `ZAN_CEF_*` | CEF 浏览器驱动运行时定位（examples/gui_cef_browser/README.md 全表：RUNTIME/CACHE/MIRROR/LOG/SWITCHES/HELPER* 等一族） |

## 5. 测试命令（本仓库纪律的命令面）

| 层级 | 命令 | 规模/时机 |
|---|---|---|
| 单例 | `cd build && ctest -R "^<name>$"` | 最窄验证，秒级；**锚定**正则避开 `case_det_` 孪生（full 档双编译） |
| 按模式 | `scripts/test.ps1 -Match "<regex>"` | 定向跑，**命中面机检 ≤80 条**（超预算拒绝并给收窄提示；不带档位跨档匹配） |
| smoke | `scripts/test.ps1 smoke`（428 例） | 裸跑被预算机检拒绝；仅发布门（`-ReleaseGate`）或用户点名 |
| standard | `scripts/test.ps1 standard`（1653 例） | 仅发布门（`-ReleaseGate`）或用户点名 |
| full | `scripts/test.ps1 full`（2860 例，≈10.6 CPU·h） | 发布门，几十分钟，绝不顺手跑 |
| 模板门 | `cd build && ctest -R templates_build` | 脚手架全部模板逐一编译 |

例数是 2026-11-04 的实测（`ctest -N -L <tier>`），会漂移，现查为准。

纪律（AGENTS.md 规则 8 的命令面）：ctest 不是默认验证步骤，**验收一轮任务
只做一次（收口时）**——编辑循环里直接编受影响程序/跑探针，改几行就重跑一组
是违规（同组 10 分钟内重复跑会被机检拒绝）；单跑受影响用例用锚定正则；命中
面超 80 条先收窄（`tools/agent/verify.ps1 -Paths` 按 import 依赖现推）；整档
smoke/standard/full 会把 CPU 打满几十分钟，**只在用户明确要求或发布门槛时
跑**。**测试运行期间不得并行任何构建**（用例共享 build\zanc.exe 与 stdlib
stamp，并发构建会让无关用例成批假红）。

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
3. **编译期 HTML 入口已移除**：`.html`/`.htm` 位置参数会被直接拒绝，
   不再是"放第一个就能用"；界面用 Gui 组件搭骨架、运行期 `Gui.App.LoadHtml`。
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
9. **超长命令行用 @file 或目录输入**（§2）：Windows 命令行长度上限下，IDE 发布通道
   就是这么做的；含空格路径加引号。**大项目更推荐目录输入**
   （`zanc src -o app.exe`）：编译器自己展开，命令行恒长，枚举规则也只有一份。
   注意"起不来"是静默的——命令行超限时进程根本不启动，报错文本里没有任何
   编译诊断，调用方只看"输出里没有 error"就会误判成编译成功。
10. **`--publish` 驱动装运失败是静默的**。装运循环逐文件拷贝失败无任何
   输出，聚合警告把原因误报为 "no runtime library found / add a
   manifest"。真踩案例：目标目录里有从该目录启动、还没退出的程序
   （Windows 锁住其已加载的 DLL），pq/sqlite3 全报 not bundled 而
   zan_gui（未被任何进程加载）正常装运——杀掉残留进程即恢复。
   "库明明在却报找不到"先查目标目录的文件锁；诊断口径缺口挂
   TASKS.md B-ID52。

## 7. 维护

改参数解析（`src/compiler/main_options_parse.c`）或加新参数 → 同提交更新
本文 §2/§3/§4 与 `zanc --help` 文案（help 在
`src/compiler/main_options.c` 的 `print_usage()`）。分组顺序固定为
常用 / 发布 / 调试 / 打包 / 内部（未稳定），两侧条目必须一一对应。本文与
help 冲突时以代码解析与 `zanc --help` 为准并修本文。
