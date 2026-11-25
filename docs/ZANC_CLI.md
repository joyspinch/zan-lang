# zanc 命令全解 —— 编译 · 发布 · 测试权威参考

一份把 zanc 的命令面说全的文档：全部参数、全部交叉目标、全部环境变量、
各场景的标准配方（可直接复制）、以及反复踩过的坑。会话里"命令搞不清"
时先查这里；本文与 `zanc --help` / `--list-targets` 同步维护，改了参数
解析（`src/compiler/main_options_parse.c`）或帮助文本
（`src/compiler/main_options_parse.c`）必须同提交更新本文。

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

# Release 发布档（strip + 优化，默认 -O2；要体积给 -Os）
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
分隔，`-` 表示清空），否则只烤基线 porcelain+graphite+navy（浅色/石墨/深蓝，base.css 恒烤）。裸 CLI
与 `--emit-apk` 均按此收敛，与 IDE 暂存发布的 CopyThemeSkins 同规则。

### 1.4 库与工具通道

```bash
build/zanc src/lib.zan --auto-stdlib --emit-lib -o foo.dll   # 共享库（.so/.dylib 同理；src/lib.zan 指你自己的源码）
build/zanc --emit-symbols index.json src/...                 # 类型/成员索引（IDE 补全）后退出
build/zanc --gen-meta meta.json src/...                      # 编译单元元数据 JSON 后退出
```

反射成员表采用宿主编译器的紧凑描述 ABI：TypeInfo 的 15 槽头部与公开 API
语义保持，字段、方法、构造器表改为共享记录池、payload 指针池和每类型 i32
成员索引。跨库传递 TypeInfo 时，消费 helper 使用该表携带的池基址。
**这是内部反射 ABI 变更，旧版预编译 Zan 库及消费方须以同一新版编译器重新构建**；
不能把“公开 API 未变”理解为可混用新旧二进制。当前没有库级反射 ABI 版本协商或
自动转换，旧平铺表与新索引表不可互读。自举编译器仍独立生成和消费其旧表示，
不支持与宿主紧凑表示跨模块交换 TypeInfo 或依赖该成员表的对象动态反射。

### CPU 要求（x86-64）

`--cpu-profile baseline|avx2` 显式选择 x86-64 的 CPU 要求，适用于宿主及
`--target` 指定的 x86-64 目标；其他架构传此选项会报错。

- 不传选项：保持历史默认，桌面 x86-64 使用 AVX2/FMA/BMI/AES 等扩展；
  Android x86-64 保持原来的 SSE4.2/POPCNT/CX16 ABI 基线。
- `baseline`：x86-64 SSE2 基线，关闭 AVX/AVX2/FMA/BMI/AES 等额外扩展。
  Vector128/Vector256 优先使用已有的通用向量和软件降级；不能降级的实际
  硬件 intrinsic 调用在代码生成前明确报错。仅导入标准库或保留未使用声明不报错。
- `avx2`：显式使用现有桌面扩展集合（含 SSE4.2/CRC32/AES/AVX2/FMA/BMI）；
  部署设备必须满足该集合。此选项不做运行时 CPU 自动分派。

```bash
build/zanc app.zan --auto-stdlib --cpu-profile baseline -o app.exe
build/zanc app.zan --target linux-x64 --cpu-profile avx2 --emit-ir > app.ll
```

主模块、优化器及 shard 的 TargetMachine 共用所选特性，`--emit-ir` 头部的
`zanc-target-cpu` / `zanc-target-features` 写出实际选择；外部 LLVM 工具应照用。

### 1.5 诊断三板斧（编译器/运行时排查）

```bash
build/zanc probe.zan --auto-stdlib --emit-ir > ir.ll   # 看 IR
build/zanc probe.zan --auto-stdlib --dump-ast          # 看语法树（--dump-tokens 同理）
build/zanc probe.zan --auto-stdlib --time              # 各阶段耗时
ZAN_LLVM_ARGS="-time-passes" build/zanc probe.zan --auto-stdlib -O2   # LLVM 逐 pass 耗时
```

`ZAN_LLVM_ARGS` 是空格分隔的额外 LLVM 命令行选项（最多 31 个），原样交给
`LLVMParseCommandLineOptions`，用于编译器自身的性能诊断（如 `-time-passes`）。

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

退出报告还会对残留对象做试探删除（S13）：引用计数高于"来自其他残留对象的
强引用数"者是根（栈帧、静态字段、未登记对象持有），根可达的都不算；其余
孤儿只被孤儿引用——ARC 回收不了的环，逐条打印路径，例如
`cycle: Node@a.zan:5:35 .Next -> List<Node>@a.zan:3:57 [0] -> Node@a.zan:5:35`
（`.字段`、`[下标]`、Dict 的 `(key)`/`(value)`）。闭包也在图里：`closure@…`
经 `.this`、`.target`（方法组接收者）或 `.捕获变量名` 走到被捕获对象，捕获盒
显示为 `box@… .value`——"处理器列表 → 闭包 → this → 宿主"这类最常见的环能直接
看到路径。判定口径按退出方式分：
Main 正常返回（静态字段已释放）时残留对象一律算泄漏；`Environment.Exit` 等
在 Main 返回前退出时栈帧与静态字段仍活着，只有孤儿环才算泄漏，报表首行写明
`in unreachable cycles at exit`。

### 1.6 机器可读诊断（`--diag=json`）

给 AI agent 与工具用的诊断通道（`docs/ZAN_STANDARD.md` S1）。stderr 只输出
JSON Lines，每行一个对象；文本格式（默认）不受影响、逐字节不变。

```bash
build/zanc tests/diag/diag_json_missing_semicolon.zan --diag=json -o out 2> diag.jsonl
```

实际输出（每个对象一行，此处为可读性折行）：

```json
{"kind":"diagnostic","severity":"error","code":"E0032","message":"expected ';', got 'int'",
 "file":"tests/diag/diag_json_missing_semicolon.zan","line":9,"col":9,"offset":338,
 "fixits":[{"file":"tests/diag/diag_json_missing_semicolon.zan","line":8,"col":18,"offset":329,"length":0,"text":";"}]}
{"kind":"summary","errors":1,"warnings":0,"suppressed":0}
```

字段契约：

| 字段 | 含义 |
|---|---|
| `kind` | `diagnostic` / `summary`；消费方遇到未知 `kind` 应忽略（向前兼容） |
| `severity` | `error` / `warning` / `note`；被 `--deny-warnings` 升级的告警为 `error` |
| `code` | 诊断编号如 `E0032` / `W2072`，字母按**发射时**级别；未编号为 `null` |
| `message` | 不含 `[E####]` 前缀的消息正文 |
| `file` `line` `col` `offset` | 位置：`line`/`col` 1 起，**`col` 按字节计**；`offset` 0 起字节偏移 |
| `end` | 诊断所在 token 的结束位置（不含）`{line, col, offset}`：按源码就地量出的标识符/数字/字面量/运算符长度；成员诊断落在 `.` 上时延到后面的成员名（区间覆盖 `.名字`）；量不出（跨行）时省略 |
| `fixits[]` | 机器可应用补丁：把 `[offset, offset+length)` 字节替换为 `text`，`length = 0` 即插入；可能为空数组 |
| summary `errors` `warnings` `suppressed` | 计数；`suppressed` = 错误上限与单行级联帽丢弃的条数；进程退出前输出 |

当前带 fix-it 的诊断（文本格式的消息里同样带 "did you mean"，补丁只在 JSON 里）：

| 诊断 | 补丁 |
|---|---|
| E0032 缺 `;` | 插在上一个 token 末尾（上一个 token 是字符串或运算符等量不出长度时不给） |
| E2085 / E2084 成员或方法名不存在 | 换成类型（含基类）里拼写最近的同类成员：调用位置只看方法，取值位置看字段/属性/方法 |
| E1005 未定义的类型 | 换成作用域里拼写最近的类型名（类/结构/接口/枚举/委托） |
| E2073 条件里写成赋值 | 普通 `=` 换成 `==`（复合赋值不给） |
| W2111 类之间的强引用环（S11） | 在回指针字段的类型前插入 `weak ` |

zan-lsp 同口径：诊断的 range 用同一个结束位置，带 `code`（如 `E2085`）；有补丁的
诊断在 `data.fix` 里带上补丁的 LSP range 与 `newText`，`textDocument/codeAction`
据客户端带回的诊断给出首选 quickfix（标题 "Change to 'X'" / "Insert …" /
"Replace with …"）。钉：`lsp_diag_fixit_quickfix`。

拼写建议取编辑距离最小的候选（相邻换位算一次；名字 ≤4 字符最多差 1 处，
更长最多 2 处），**最近的候选有两个就不给补丁**——猜错的补丁比没有更糟；
编译器合成的名字（`get_`/`set_`/`op_`/`__` 开头）不当建议。退出码语义与文本
格式相同。钉：`diag_json_fixit`、`diag_json_class_cycle`、`diag_fixits_apply`
（`tests/test_diag_fixits.py`：逐个夹具把补丁应用回源码再编译必须通过）。

### 1.7 接口摘要（`--summary`，S4）

`zanc <文件|目录...> --summary [--auto-stdlib]` 打印**命令行显式给出的源文件**的
公开面后退出（类型检查之后、不生成代码；有编译错误时不出摘要、退出码 1）。
`--auto-stdlib`/包发现拉进来的库只用于解析，不进摘要。供 AI 一次装入一个包的
接口，替代逐文件翻源码：

```
public class UiEvent  // 控件事件：单一订阅通道。
  // namespace Gui
  public static UiEvent op_add(UiEvent self, Action handler)  // 支持 event += handler（追加，多播）。
  public int Count()  // 全部订阅数（两种处理器形状合计）。
public class OrderLine  // A priced line in an order.
  public static string LoadPrices(string path)  [throws FileNotFoundException; uses File]  // Reads a price list.
```

- 每个类型一行头（修饰、种类、名字与类型参数、基类型）+ 一行 `// namespace`；
  每个公开成员一行 C# 形态签名（方法类型参数、`static`/`virtual`/`override`/
  `async`/`event` 修饰、属性的 `{ get; set; }`）。
- 列出：类型除显式 `private`/`internal` 外都列；成员只列 `public`/`protected`，
  接口与枚举成员全列。属性降解出的 `get_X`/`set_X` 不重复列。
- 文档：声明上方紧邻注释块（`//`、`///`，跳过 `[特性]` 行）的第一句，剥 XML
  标签，截到第一个 `。` 或 `. `。
- 效果列 `[throws …; uses …]`（方法与构造器，有才出）：与 §1.7a `--query` 同一
  口径——可能逃出该成员的异常类型（扣除沿途 catch，rethrow 不计，每个成员最多
  列 6 个、超出记 `...`）与用到的能力（Net/File/Process/Registry/Clipboard/
  Native）。全程序一遍：调用图强连通分量定顺序，集合按方法记、分量内迭代到不动点。
  分配不进摘要（几乎每个方法都分配，只是噪音）；契约列待 P4。
- 首行 `// zan-summary 1: …` 是格式版本；末行给类型/成员计数。
- 实测：`packages/Zan.Gui/src` 460 个类型、6350 个成员（1421 行带效果列），约 1.4 秒。
- 钉：`summary_surface`（`tests/summary/`）。

### 1.7a 符号查询（`--query`，S2）

`zanc <入口...> --auto-stdlib --query <Type.Method | Type.Type | Type | Method>`
在类型检查通过后回答"这个方法会干什么"，不生成代码；每个匹配一行 JSON，末行
`{"kind":"summary","matches":N}`（无匹配退出码 1）：

| 字段 | 内容 |
|---|---|
| `symbol` / `file` / `line` | `Type.Name(参数)` 与声明位置 |
| `allocates` | 方法体里直接的分配：`new` 对象/数组、集合初始化、lambda 闭包 |
| `captures` | 每个 lambda 是否用到 `this`、捕获了哪些外层局部量/形参 |
| `throws` | `direct`：方法体里**没被本方法 catch 住**的 `throw`；`escapes`：可能逃出本方法的异常类型，每种一条真能逃出来的示例链（只走不收住它的调用点）；`escapes_truncated`：超过 32 种时为 true |
| `capabilities` | 可达调用里用到的能力（S7 口径）：`Net`、`File`、`Process`、`Registry`、`Clipboard`、`Native`（标准库之外的 extern/DllImport），每项一条证据链 |
| `calls` / `callers` | 直接调用的方法；全程序里解析到它的调用点（各最多 64 条，`callers_truncated` 标出截断） |
| `reachable_methods` | 经调用图可达的方法数 |

catch 扣除：只有 `try` 体受本 try 的 catch 保护（catch 体与 finally 不受）；catch
基类型也收（`catch (IOException)` 收 `FileNotFoundException`），裸 `catch` 与
`catch (Exception)` 全收；catch 体里 `throw;` 或把捕获变量原样抛出的不算收住。
lambda 体是委托被调用时才执行的代码：里面的调用与 throw 不算外层方法会抛，但
算它用到的能力。调用图来自类型检查写在调用节点上的解析结果；`new T(...)` 连到
T 的同元数构造器；调用解析到虚/抽象/接口方法时同时连到全部 `override` 与接口
实现（按名字 + 形参个数，沿基类与接口继承逐层找）；方法名当值用（`b.Click +=
OnClick`、`Action a = Save;`）按当前类型及基类的同名方法连 deferred 边，与 lambda
同口径。只有经委托变量的调用不连边（目标运行期才定）——所以 `escapes` 仍是上界。单名先当类型名
列出它的全部方法，没有该类型再按方法名找。实测 702 文件的 GUI 程序查询 < 1 秒。
钉：`query_symbol_facts`（`tests/test_query.py`）。

### 1.8 一条命令的检查报告（`zan-check`，S5）

`zan-check <入口.zan|目录...> [--zanc <路径>] [--timeout <秒>] [--no-run]
[--zanc-arg <参数>]... [--out report.json] [-- 程序参数...]` 依次跑完下面几步，向
stdout（与 `--out`）输出**单份 JSON 报告**；退出码 0 = 没有失败步骤（skipped 不算
失败），1 = 有失败，2 = 参数问题。`zan-check` 用 Zan 写（`src/check/ZanCheck.zan`），
随工具链发布在 `toolchain/` 下，不依赖 Python；不给 `--zanc` 时用同目录的 `zanc`，
其次 PATH 上的 `zanc`。程序在当前目录运行。

| 步骤 | 内容 | 失败条件 |
|---|---|---|
| `compile` | `zanc … --auto-stdlib --check-leaks --diag=json`：全部诊断原样收进 `diagnostics`（§1.6，含 fix-it 与 `end`），`errors`/`warnings` 计数；失败时附 `other_output`（非 JSON 输出的最后 20 行） | 编译失败 |
| `leaks` | 运行产物（默认 60 秒超时）并解析退出报告：`leaked`、`sites[{count,site}]`、`cycles[]`（S13 路径）、`mode`（`strict` = Main 正常返回，`early-exit` = 提前退出只计不可达环）；退出码非 0 时附 `output_tail` | 有泄漏、退出码非 0 或超时 |
| `ui` | `zan ui check`（S22，P2） | 未实现，恒 `skipped` |
| `replay` | 回放比对与不变量（S26/S27，P3） | 未实现，恒 `skipped` |

编译失败时后续步骤记 `skipped` 并写明原因，报告形状不变：
`{"version":1, "ok":…, "inputs":[…], "steps":[{"name":…, "status":"pass|fail|skipped", …}]}`。
GUI 或常驻进程用 `--no-run` 只做编译诊断，或用 `--timeout` 给出运行上限（到点杀掉
直接子进程，leaks 步判失败并写明原因）。钉：`zan_check_report`
（`tests/test_zan_check.py`）。

## 2. 全参数表（与 `zanc --help` 同源）

> 分组与 `zanc --help` 一致：**常用 / 发布 / 调试 / 打包 / 内部（未稳定）**。
> 默认值与联动关系以 `src/compiler/main_options_parse.c` 的解析和
> `src/compiler/main.c` 的落值为准；标「(内部/未稳定)」的不是公共契约。

### 2.1 常用（Common）
| 参数 | 取值 / 默认 | 说明 |
|---|---|---|
| `-o <file>` | — | 输出文件。**后缀决定产物类型**：`.dll/.so/.dylib`=共享库，`.a/.lib`=静态库，其余=可执行 |
| `--emit-lib` | 关 | 强制库输出（无 CRT/入口点）；与 `-o` 后缀推断并存时后缀仍决定 shared/static |
| `-O0..-O3/-Os/-Oz` | 默认 `-O2` | 优化档；`--publish` 同样默认 `-O2`（发布要速度；要体积显式给 `-Os`，`-Oz` 极限体积用于边缘/嵌入式）；`-g` 总是强制 `-O0` |
| `--target <name>` | 本机 | 交叉编译（§3 全表） |
| `--list-targets` | — | 列出全部目标后退出 |
| `--subsystem <console\|windows>` | 自动 | PE 子系统（仅 Windows）；GUI 程序自动推断 windows |
| `--stdlib-path <dir>` | 自动定位 | 指定 stdlib 根目录（测试门用；拉入仍按需） |
| `--auto-stdlib` | **开** | 自动定位 stdlib 与已安装包，**按需拉入**用到的命名空间；显式传等于重申默认 |
| `--no-stdlib` | — | 关自动 stdlib **和**包发现；`--no-packages` 只关包 |
| `--no-packages` | — | 只关包发现（stdlib 拉入照常）。回归锁 `conformance_gen_pkg_isolation`：包内库级调用不牵连父编译 |
| `--fast-alloc` | native 默认开 | 小对象分配器（`rt_mem.c`）。ELF/PE 以 `--wrap=malloc/free/calloc/realloc` 接管全镜像；Apple（ld64 无 `--wrap`）改为把 Zan 生成代码的分配调用改名指向 `zan_mem_*`，C 运行时/驱动仍用 libc，跨界的 `free`/`realloc` 由注册的 `zan_mem` malloc zone 回送（交叉 macOS/iOS 在 `<zanc>/macos/<arch>/`、`ios/arm64/` 有 `zanrt_mem.o` 时同样改名链入）。其余交叉目标不链；目标无分配器对象时告警并回退 CRT |
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
| `--time` | — | 打印各阶段耗时与规模统计（含一行 `build cache:` 命中/未命中与耗时） |
| `--no-build-cache` | 缓存**开** | 关闭 LLVM 后端构建缓存（[`BUILD_CACHE.md`](BUILD_CACHE.md)）：优化前模块与最终模块按内容哈希记忆化优化结果和目标文件，重复构建跳过优化与后端，产物与关缓存逐字节相同。键含 zanc 二进制（路径/大小/mtime）、除 `-o`/`--emit-deps`/`--time` 外的全部参数与全部 `ZAN_*` 环境变量；`ZAN_BUILD_CACHE=0` 等效；`--emit-ir` 不走优化表 |
| `-D<name>[=v]` | — | 预处理符号 |
| `@<file>` | — | 响应文件（一行一个或空白分隔，带空格路径用引号，`#` 起注释，最多嵌套 8 层）——**超长命令行/含空格路径用它** |
| （位置参数，文件/目录） | — | `.zan` 文件，或**目录**（递归展开 `*.zan`，按路径排序）。目录输入跳过 `.` 前缀与 `.git/build/publish/bin/obj/node_modules/_scratch/dist/out/vendor/.zan-packages` 等目录；目录里没有 `.zan` 报 "no .zan source files under '...'"。**只收 `.zan`**：显式给 `.html`/`.htm` 会被 "the compile-time HTML channel has been removed" 拒绝——界面用 Gui 组件搭骨架、运行期 `Gui.App.LoadHtml` 装载标记（内嵌资源走 `--embed`） |
| （无旗标，按目标自动） | — | Windows/Linux/macOS 的 x64/arm64 目标自动链入多 worker 协程调度器（`zanrt_io_mt`）；worker 数解析顺序：`System.Threading.AsyncRuntime.SetWorkers`（Main 内设置）→ `ZAN_CO_WORKERS` 环境变量 → 逻辑核数。IO 分片与同步完成快路径同理由 `AsyncRuntime` 优先于 `ZAN_IO_SHARDS`/`ZAN_IO_SYNCFAST` |
| （默认，native） | — | Windows/Linux 原生默认链入小对象分配器（`rt_mem.c`：≤2048B 尺寸分级无锁缓存、跨线程 free 分 8 条带，`ld --wrap` 全图接管 malloc/free/calloc/realloc）。Apple 原生与交叉（bundle 有对象时）经发射期改名 `zan_mem_*` 链入（ld64 无 `--wrap`）；Apple 链接一律 `-dead_strip`，未用到异步的程序不再调 `zan_co_sched_init`、不带 M:N 调度器；Android 交叉永不链（bionic TLS bootstrap 崩溃）；linux-musl 交叉 sysroot 有对象即自动链。Windows 链接自动改走捆绑 GNU ld（lld 的 PE `--wrap` 改写导入槽，程序加载即死 exit 127）。实测 win-x64：spawn k=1 -26%~-31%，k=32 分片后不劣。调试：`ZAN_LINK_ECHO=1` 回显链接命令 |
| `--version, -v` / `--help, -h` | — | 版本 / 帮助；`--version` 输出 `zanc X.Y.Z (gen0)`——代次标记（B-ID189）由自举差分门盖进报告指纹；自举驱动（gen1）同旗标输出 `(gen1)`，字面量由 `bump_version.ps1` 与 VERSION 同步，漂移即门失败 |

### 2.2 发布（Publish）
| 参数 | 取值 / 默认 | 说明 |
|---|---|---|
| `--publish` | 关 | 发布档：默认 `-O2`、整模块优化（跨文件内联与普通构建相同，运行速度 = `-O2`）、链接期 strip（同 `--strip-symbols`）；优化前分片不再默认开（`ZAN_SHARD=1` 显式开，见环境变量表）；`--check-leaks`/`--arc-guard` 默认关（显式传入仍生效）；**over-release 网默认开**（未显式给 `--arc-guard`/`--no-arc-guard` 时）；内建图标默认不嵌（`--default-icon` 可显式开启） |
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
| `--fp-contract` | 关 | 允许把同一表达式里的 `a*b+c` / `a*b-c` / `c-a*b` 融合为一条 FMA（`llvm.fmuladd`，同 clang 默认的 `-ffp-contract=on`：不跨语句）。浮点密集循环可快 20–30%（mandel 0.52 → 0.40s，与 C 持平）；代价是结果末位可能因 CPU 而异，故默认关（默认与 C# 一样跨平台逐位一致）。钉：`case_fp_contract` / `case_fp_contract_flag` |
| `--checked` | 关 | 全局 checked 算术（C# `/checked+` 等价）：全部整型算术溢出抛 `OverflowException`（含 `-` 取负、`++`/`--`、窄化强转）；`unchecked(){ }` 块局部豁免。默认（关）为 C# 默认的 unchecked 环绕语义。钉：`case_checked_arith_default`/`case_checked_arith_flag` |
| `--strict-runtime` | 关 | 守卫失败直接 `exit(70)`；`ZAN_RT_HARD` 非空且非 `0` 等效；`ZAN_RT_HARD=0` 显式压回软模式 |
| `--deny-warnings` | 关 | 将编译警告视为致命错误（阻断构建） |
| `--warn-level <none\|default\|error>` | `default` | `none` 不打印警告；`default` 正常；`error` 等同 `--deny-warnings`；非法值报错退出 2 |
| `--suppress <list>` | — | 按编号抑制**警告**（逗号/空格分隔，字母前缀可省，如 `W0003,0003`）；与 `--deny-warnings`/`--warn-level=error` 同用时警告已升级为错误，不再被抑制 |
| `--diag <text\|json>` | `text` | 编译期诊断输出格式。`json` 为 JSON Lines（字段契约见 §1.6），供 AI/工具直接消费；非法值报错退出 2。钉：`diag_json_fixit` |
| `--dump-tokens` | — | 词法 token 后退出 |
| `--dump-ast` | — | 打印语法树后继续编译 |
| `--emit-ir` | — | IR 到 stdout 后退出，不链接 |
| `--emit-symbols <f>` / `--gen-meta <f>` | — | 输出索引/元数据后退出（IDE/LSP 用） |
| `--summary` | — | 打印显式输入源文件的公开接口摘要后退出（S4，见 §1.7） |
| `--query <sym>` | — | 类型检查后输出方法的分配/捕获/抛出/能力/调用者（JSON Lines）并退出（S2，见 §1.7a） |
| `--emit-deps <f>` | — | 类型检查通过后把实际编进程序的源文件（拉入过滤之后，绝对路径一行一个）写到 `<f>`，照常继续编译（S3，见 §5） |
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
| `--package-project <dir>` | `.` | 包发现/项目级安装的工程根（内部/未稳定）；不改变默认业务资源的归属 |

默认 `assets/` 归第一个用户输入所属工程：从入口目录向上找到最近的 `zan.proj`，遇到 SDK 的 `packages/` 边界即停止；没有工程清单时只取入口同目录的 `assets/`。自动引入的包源码目录和包搜索根不作为业务资源候选，因此包内 `Game/Assets/*.zan` 或 SDK 根目录的图片不会自动混入应用。需要其他目录时显式使用 `--embed <目录>=assets`；显式 `assets` 别名优先，包清单声明的资源和 Gui 皮肤仍按各自契约内嵌。

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
| `ZAN_PULLIN_DEBUG` | 打印 stdlib 拉入决策（`incl <文件> because <名字>`：`controller` 表示按 MVC 控制器常驻拉入） |
| `ZAN_LINK_ECHO` | 回显链接命令（含 Unix 宿主 `cc` 链接，可看分片对象是否在链接行上） |
| `ZAN_CODEGEN_SPLIT` | 内联完成后的模块 ≥20 万条指令时，按 `llvm::SplitModule` 切成多份、多线程并行跑 LLVM 管线的后半段（向量化/展开/收尾清理）与目标文件代码生成（各线程独立 LLVMContext/TargetMachine，额外分区 `<out>.o.p<N>.o` 一并链接）；只影响本机、非交叉、非 `-g`、非库构建，不影响内联与运行速度。`=0` 关闭，`=N` 强制 N 份（上限 16，保持链接命令行短于 cmd.exe 的 8191 字符） |
| `ZAN_OPT_SPLIT` | `=0` 时 LLVM 管线整段在优化阶段对整模块跑完（旧行为，仅供 A/B）。默认直接产出目标文件的构建只在优化阶段跑 `default<ON>` 的前半段（内联 + 函数化简，需整模块），后半段留到目标文件发射：切分时逐分区并行跑，不切分时整模块跑，结果与一段跑相同（`--emit-ir`、`ZAN_OPT_PIPELINE`、`ZAN_DUMP_OPT_IR` 自动走一段） |
| `ZAN_SHARD` / `ZAN_NO_SHARD` | `ZAN_SHARD=1` 打开优化**前**分片（本机非交叉）：峰值内存与编译时间更低（自举编译器约 400MB/22s 对整模块 1.3GB/35s），但跨分片不内联，产物慢 10–20%；`ZAN_SHARD=0` 或 `ZAN_NO_SHARD=1` 关闭。`ZAN_SHARD_MAX_FN` / `ZAN_SHARD_MAX_INSN` 调单片上限 |
| `ZAN_SPLIT_TRACE` | 非空时打印切分阶段各步的进程内存（切分前 / 分区序列化后 / 释放原模块后 / 并行发射后） |
| `ZAN_HUGE_FN` | `-O2`/`-O3` 时，优化前指令数超过此值（默认 4000，`0` 关闭）的函数加 `optsize`，降低往超大分派函数里内联的阈值 |
| `ZAN_DIAG_LIB_BODIES` | `=1` 时普通（封闭世界）构建也为未达到的标准库/`packages/` 方法生成一遍代码以报代码生成诊断（默认只诊断用户源码里未达到的方法；库由自身构建与测试覆盖） |
| `ZAN_EH_FN_MARK` | `=1` 时每个方法/lambda 入口都记临时栈标记、落地垫都回卷到它（旧行为）。默认只有函数体压过临时（`__zan_eh_tmp_push`）的函数才记，其余函数省掉入口线程 EH 状态读取与回卷调用，仅供 A/B |
| `ZAN_NO_LATE_ARC` | `=1` 关闭内联后 ARC 收尾（默认 `-O2`/`-O3`/`-Os`/`-Oz` 开启：retain/release 与临时栈 push/pop 助手在 LLVM 主管线中保持外联，管线后消去同块或经软空守卫分叉的 retain/release 对、窗口内不会展开的 push/pop 对，再按原内联属性内联助手），仅供 A/B |
| `ZAN_LATE_ARC_STATS` | 非空时在 stderr 打印内联后 ARC 收尾消去的 push/pop 对与 retain/release 对数 |
| `ZAN_DUMP_LATE_ARC_IR` | `=<路径>` 落盘内联后 ARC 收尾之前的模块 IR（排查消对是否命中） |
| `ZAN_NO_GUARD_VERSION` | `=1` 关闭软守卫版本化（默认 `-O2`/`-O3` 开启：含软运行时检查的循环克隆出无检查快环，第一个失败的检查从同一站点交给原循环全检查地跑完；循环不变守卫外提到预头部；报告次序与语义不变），仅供 A/B |
| `ZAN_GUARD_VERSION_STATS` | `=1` 打印版本化了多少区域/整环；`=2` 逐个列出函数、种类与指令数 |
| `ZAN_NO_TBAA` | `=1` 关闭字段别名元数据（默认优化构建给经 `emit_field_ptr` 的标量字段读写打按字段字节偏移的 TBAA 标签，告诉 LLVM 同一对象不同偏移的字段互不别名；显式布局结构不打），仅供 A/B 与排查疑似误编译 |
| `ZAN_DLG_PIN_ALL` | `=1` 时每次委托调用都 retain 闭包并压临时栈（旧行为）。默认对函数内从不写、不按 ref 传、不被 lambda 捕获的局部/形参委托省掉这套簿记，仅供 A/B |
| `ZAN_SOFT_SHARED_SCRATCH` | **运行时**环境变量：非空时软失败（try 外空引用继续执行）沿用旧的进程共享 4 KB 暂存区。默认每个出过软失败的线程惰性映射私有暂存（[保护页][头 + 1 MiB][保护页]，超出即触保护页而非写坏相邻内存），线程退出时清零后回池复用，仅供 A/B |
| `ZAN_INLINE_STRLEN` | 非空时恢复字符串长度探测逐点内联展开（默认调用共享内部函数 `__zan_str_len[_arr]`），仅供 IR 体积 A/B |
| `ZAN_LLVM_ARGS` | 空格分隔的额外 LLVM 选项（如 `-time-passes`），编译器性能诊断；设置时自动关闭构建缓存（命中会吞掉旁路输出，`ZAN_DUMP_OPT_IR` 同理） |
| `ZAN_BUILD_CACHE` | `=0`/`off`/`false` 关闭构建缓存（同 `--no-build-cache`） |
| `ZAN_CACHE_DIR` | 构建缓存根目录（条目在其下 `backend/`；`backend/` 建为 0700，条目 0600；POSIX 上 `backend/` 必须归当前用户且不可被组/他人写（自己的会被收紧），其父目录须归当前用户或 root 且不可被他人写（带粘滞位的如 `/tmp` 除外），否则告警并关缓存；读取时条目不是当前用户所有的普通文件或可被他人写即视为未命中重建）；缺省为平台用户缓存目录下 `zan/backend`（macOS `~/Library/Caches`、Linux `$XDG_CACHE_HOME` 或 `~/.cache`、Windows `%LOCALAPPDATA%`） |
| `ZAN_CACHE_PRUNE` | 构建缓存清理策略（LLVM `pruneCache` 语法），缺省 `prune_interval=20m:prune_after=168h:cache_size_bytes=4g` |
| `ZAN_NO_PULLIN_FILTER` / `ZAN_NO_PRUNE` | 拉入调试：关过滤 / 关裁剪 |
| `ZAN_NO_DEV_PRUNE` | `=1` 时普通（非 `-g`、非库）构建恢复旧行为：发射全部方法体、为每个方法生成反射调用桩。默认普通构建与 `--publish` 同用封闭世界规则（按需方法体；反射只对属性访问器、带特性方法、设计稿事件处理器、框架钩子与被 Invoke 点名的方法可调用；计算名 Invoke 只能选中名字在程序中以字符串字面量出现过的方法），开发期能反射调用的就是发布后能调用的；`-g` 仍保留全部方法与反射桩 |
| `ZAN_NO_PUBLISH_REFLECT_DEMAND` | `=1` 时禁用发布反射需求裁剪，恢复全分配点元数据、既有可调用钩子及完整成员签名元数据；仅影响 `--publish` 且非调试、非库构建，用于同源体积对照和回归定位 |
| `ZAN_NO_PUBLISH_VSLOT_DEMAND` | `=1` 时恢复完整虚表槽引用；默认发布构建按实际发射的虚调用槽保留实现。调试、库构建以及外部接口可能供应实例时采用保守保留，用于回归定位 |
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
| `ZAN_CYCLE_AUDIT` | **编译期**：设为非 `0` 时 S11 类型图强环检查（W2111）连标准库与 packages 里的类一起报，用于库审计；默认只报用户代码 |
| `ZAN_LEAK_LOG` | `--check-leaks` 产物退出时把泄漏报表副本追加到此路径（默认工作目录下 `zan_leaks.log`；设为空串只打印不落盘）。测试驱动指向 `build/test-out/leaks/<用例>.log`，不再污染仓库根 |
| `ZAN_RT_HARD` | 非空且非 `0` 时守卫失败硬退出（等效 `--strict-runtime`）；`=0` 显式强制软模式 |
| `ZAN_CO_WORKERS` | 多 worker 协程调度器的 worker 数（native 64-bit 目标默认即多 worker 驱动，无需任何构建旗标；默认=min(逻辑核数，4)，见 `AsyncRuntime.SetWorkers` 优先） |
| `ZAN_CO_QUANTUM_MS` | 协程协作式抢占的时间片（毫秒，默认 2，0=关闭）：编译器在 async 函数的每个循环回边埋 `zan_co_poll`，超片的计算密集帧自动重排到队尾，不再饿死定时器/IO 回调/兄弟协程；片长记账走微秒级单调钟（Windows QPC，POSIX CLOCK_MONOTONIC），2ms 就是真 2ms |
| `ZAN_CO_STACK` | Task.Spawn/Run 有栈 fiber 的栈大小（默认 128KB，范围 64KB~16MB） |
| `ZAN_GUI_ICONS` | 覆盖图标包目录（发现序：env → exe 旁 icons/ → 内嵌 → stdlib） |
| `ZAN_CEF_*` | CEF 浏览器驱动运行时定位（RUNTIME/CACHE/MIRROR/LOG/SWITCHES/HELPER* 等一族） |

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

**受影响用例（S3）**：`python3 scripts/affected.py <改动文件...>`（或 `--git` 取
工作区改动、`--diff x.patch`）列出受影响的 ctest 用例并给出锚定的 `ctest -R`
命令。用例引用的文件 = 注册参数里的源文件/夹具/金样/驱动脚本 + 编译器记下的真实
依赖——`tests/run_case.cmake` 与 `run_case_modes.cmake` 以
`zanc --emit-deps <产物>.deps` 编译，拉入过滤后实际编进程序的每个标准库/包文件都在
里面。默认避开 det 孪生（`--with-det` 包含），超过预算（`--budget`，默认 80）只报
数量；编译器/运行时原生源码、`CMakeLists.txt`、文档不猜名单，在 notes 里说明
怎么办。从未带依赖记录编过的用例只按注册参数匹配（notes 报数量），跑过一次即补齐。
`--json` 输出 `{tests, count, over_budget, det_skipped, ctest_regex, notes}`。

**泄漏门禁（S10）**：经 `tests/run_case.cmake` 的用例默认追加 `--check-leaks` 编译，
退出时仍可达对象数必须为 0——泄漏报表从 stdout 尾部切下单独判定，不进金样对账。
`--publish` / `--no-check-leaks` / `-DLEAK_GATE=OFF` 的用例不设门禁。门禁上线时已有
的泄漏登记在 `tests/leak_baseline.txt`（按产物名），**只减不增**：登记用例不再泄漏会
变红，提醒删行；新用例泄漏要修（`weak` 打环、退订、Dispose），不许登记。
报表里的 `cycle:` 行（见上文 S13）直接给出要打断的环。

### 模板门配方（tests/run_templates.cmake 实际做的事）
模板不是"直接编模板树"，而是脚手架后编译：
1. 复制模板树到 build/templates_build/<tpl>/，文本文件内容替换 `{{NAME}}`
   → `TplProbe`；
2. gui 模板把 `src/App.zan`/`src/App.html` 改名为同名的 `TplProbe.zan` / `TplProbe.html`；
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
`src/compiler/main_options_parse.c` 的 `print_usage()`）。分组顺序固定为
常用 / 发布 / 调试 / 打包 / 内部（未稳定），两侧条目必须一一对应。本文与
help 冲突时以代码解析与 `zanc --help` 为准并修本文。
