# zan-lang 全仓审计 · 可执行任务清单

**总目标**：除 compiler 和 runtime 外，一律用 Zan 写。一切以最终形态和最佳实现为准，
不为兼容老代码让步（未发布）。语法语义**尽可能保持 C#**。

三部分合并：A 编译器/运行时能力，B 标准库，C 文档。每项带 ID、依赖、判定标准。

**本清单里凡标注〔已实测〕的结论都在机器上跑过验证；未标注的是静态分析结论。**
早期草稿中的错误结论已作废，见文末"已撤回的结论"。

## 维护约定

本文件是这项工作的单一来源，随工作推进持续更新，不另开清单。

* 状态标记：`[ ]` 未开始 · `[~]` 进行中 · `[x]` 已完成 · `[-]` 已作废（保留条目并写明原因）；
* **ID 稳定且不复用**——作废的编号不再分配给新任务；
* 每项完成时补上实测证据（命令、输出、测试名），不写"应该可以"；
* 新发现的问题追加到对应章节末尾，编号顺延；
* 结论被推翻时，改正文并在文末"已撤回的结论"里留一条，不要静默修改。
* **遇到编译器/运行时缺陷先修根因，不要绕过**（`AGENTS.md` 硬规则 10、
  `docs/WORKSPACE_CONVENTIONS.md` §8）。绕过写法只允许在写清探针与根因、
  登记到本清单并取得一致之后存在。

---

# 零、审计数据

**代码量**（`stdlib/**/*.zan`，767 文件 4.6MB）：

| 顶层目录 | 总行 | 代码行 | 文档注释 | 文档覆盖率 | 层级定位 |
|---|---|---|---|---|---|
| `Gui` | 48568 | 38392 | 3773 | 9.8% | UI 框架 |
| `System` | 38388 | 28821 | 4766 | 16.5% | **语言核心标准库** |
| `Game` | 23044 | 20903 | 142 | **0.7%** | 项目级库 |
| `Sdk` | 16346 | 11024 | 1941 | 17.6% | 项目级库（生成代码为主） |
| `SDL3` | 881 | 656 | 50 | 7.6% | 绑定层 |
| `Platform` | 169 | 112 | 20 | 17.9% | 占位 |

分层而非取舍：`System` 是语言核心标准库；`Gui` / `Game` / `Sdk` 是项目级库和框架，
**都要用，都留在仓库里**，问题在封装质量不在位置。

**其它度量**：

* 精确重复行 1720 / 18421（9%）—— 不是行级复制粘贴，是**结构性重复**；
* 超过 150 行的方法 **23 个**，最长 **1536 行**（`Widget/DataTable.Render.zan:327`）；
* `extern string calloc` 出现在 **49 个文件**里；
* 泛型类全库只有 **8 个**声明，而连接池被手写了 **7 份**；
* stdlib 里 `static extern` 声明 **876** 个，其中返回 `int` 的 **577** 个，
  用 `int` 承载句柄型参数的 **193** 处；
* native import 分布：`crt` 207、`zan_gui` 103、`user32` 81、`zan_sdl3` 78、
  `kernel32` 73、`gdi32` 38、`odbc` 24、`imm32` 4、`ws2_32` 2、`dwmapi` 1、`psapi` 1；
* `docs/` 40 份，全部在最近 3 周内动过——问题不是日历陈旧，是**内容与实现漂移**。

---

# A. 编译器 / 运行时能力

## A0 数值类型对齐 C# 〔无依赖〕

**已定**：完全对齐 C#——`sbyte`=8 `short`=16 `int`=**32** `long`=64，
`byte`/`ushort`/`uint`/`ulong` 对应无符号，`nint`/`nuint` = 指针宽，
`float`=32 位 `double`=64 位。

〔已实测〕**无符号和窄类型的运算语义目前是正确的**，不需要重做。
`build/conf_unsigned_types.exe` 输出与 `.out` 完全一致：`uint` 32 位回绕、
`ushort` 16 位回绕、`sbyte` 从 8 位符号扩展、`ulong` 的无符号除/模/右移/比较
在整个 u64 范围内都对。实现策略是**存储用 i64、在运算处截断掩码**。
所以 A0 的内容收窄到 **FFI 边界位宽**和**结构体布局**。

* **A0-0** ✅ 已修（2026-07-27）把用 `int` 承载句柄/指针的 extern 声明改成 `nint`，
  详见下面「A0-0 实测结果」。
* **A0-0b** ✅ 已修（2026-07-27）编译器新增 C# 窄化诊断（`ZAN_WARN_NARROW=1`），
  用它把"用 `int` 变量接住 64 位/指针宽值"的载体精确炸出来。
* **A0-0c** ✅ 已修（2026-07-27）socket 句柄的类型。Windows `SOCKET` 是 `UINT_PTR`、
  POSIX fd 是 32 位 `int`，原先整个 `Socket` API 一律 `int sock`。现在与 C# 一致：
  Zan 侧统一 `nint`（对应 `SafeSocketHandle`/`IntPtr`），POSIX extern 保持 `int`。
  截断只发生在 `Socket.zan` 新增的一层 `Sys*` 平台包装里
  （`SysClose` / `SysShutdown` / `SysBind` / `SysListen` / `SysAccept` / `SysConnect` /
  `SysSetSockOpt` / `SysSendTo` / `SysRecvFrom`）：Windows 分支直接转发整个 `SOCKET`，
  POSIX 分支写 `closesocket((int)s)` 这样的显式 `(int)`。共享调用点因此不用再按平台分叉。
  改动覆盖 `Socket` / `AsyncSocket` / `TcpClient` / `TcpListener` / `UdpClient` /
  `Worker` / `HttpServer` / `HttpsServer` / `TlsStream` / `WebSocket` / `WssServer` /
  `WebApp` / `Sse` / `Net` / `RpcServer` / `MqttBroker` / `MqttClient` /
  `Redis` / `MySql` / `SqlServer` / `Firebird` / `TDengine` / `Zgm.NetRuntime`
  以及 tests / examples / templates 里的调用点。
  〔已实测〕`ZAN_WARN_NARROW=1` 全量扫描（`tests/conformance` + `tests/golden/cases` +
  `examples` + `templates` + `src/ide_zan` 每个文件各编一次）：改前 51 处窄化警告，
  改后 **0 处**（`Socket.zan:316` 那条跟踪标记随之消失，因为它现在是 `nint clientSock`）。
  真正是 32 位 fd / 数据库自有句柄的地方（`PageFile`、`Postgres` 等）**保持 `int`**，
  不跟着 socket 一起改。
  新增用例 `tests/conformance/socket_handle_nint.zan`：回环 TCP 全程只用 `nint` 句柄、
  `List<nint>` 里存连接、指针宽值 `0x1_0000_0001` 不丢高位。562/562 通过。
* **A0-0b 增补** ✅ 窄化诊断补上变量声明初始化式（`irgen_stmt.c` 的 `AST_VAR_DECL`）。
  原先只覆盖赋值和实参，而 `int fd = Foo()` 正是句柄载体最常见的入口，
  A0-0c 的 51 处里绝大多数只有加上这条才会报出来。
* **A0-0d** ✅ 句柄部分已修（2026-07-27），位宽部分按下面的理由留到 A0-1/A0-2。
  我们自己的 C 垫片签名原先一律 `int64_t`（`zan_io_*`）/ `i64`（`zan_gui_*`），
  等于把"Zan int 恰好 64 位"写死进 ABI。现在：
  - `rt_io.h` / `rt_io.c`：所有 `fd`（socket 句柄）→ `intptr_t`，
    `zan_io_accept_co` 的出参 → `intptr_t *`，`flags` / `interest` / `write_ready` /
    `port` → `int32_t`，字节数与 `timeout_ms` → `int64_t`，
    `poll` / `pump` / `pump_timeout` / `has_pending` / `set_nonblocking` 的返回 → `int32_t`。
    内部 `io_register` 也按 `intptr_t` 传递，只在真正按 32 位 fd 索引的
    epoll / kqueue / select 后端上显式 `(int)`——原先 Windows 的 IOCP 路径
    是 `io_register((int)fd, ...)`，把 `SOCKET` 截成 32 位。
  - `gui_runtime*.c` / `.m`：新增 `typedef intptr_t iptr`，105 处原生窗口句柄
    （`hwnd_val`、`zan_gui_create_window` / `zan_gui_event_hwnd` 的返回值）→ `iptr`。
    HWND / X11 `Window` / `NSWindow*` 都是指针宽。
  - 坐标 / 颜色 / 尺寸那批 `i64` **故意不动**：Zan 的 `int` 今天仍然 lower 成 i64，
    现在把它们钉成 `int32_t` 会立刻造成反向的 ABI 不匹配。它们要和 A0-1
    （`int`→i32）、A0-2（按声明位宽 lower）同一批切，那时才是零窗口的。
  - 顺带修：`irgen_emit.c` 的 sync-runtime 触发前缀表补上 `zan_monotonic_`，
    否则只 import `zan_monotonic_us` 的程序不会链接 `zanrt_sync`，
    链接期报 `undefined reference to 'zan_monotonic_us'`。
* **A0-1** `int` → i32、`long` → i64。C 的 `int` 也是 32 位，**对齐后 FFI 自然正确**。
  **前置已落地（2026-07-27）**：`map_type` 一改，i32 的 `int` 就会和 i64 的长度/计数/
  句柄/运行时 helper 混在同一个二元运算里，LLVM 直接拒绝
  （`%argi = add i32 %load5, i64 1`）。irgen 里 391 处整数 builder 已统一换成
  `zan_add` / `zan_icmp` 等包装（`irgen.c` 顶部），先把较窄的一侧符号扩展到较宽的一侧，
  否则每个 lowering 点都要手工扩展。`int` 仍是 i64，这批包装在当前位宽下是恒等变换。
  **实测**：把 `map_type` 的 `TYPE_INT` 临时改成 i32 后，编译器自身、全部 Zan 工具链
  （zanfmt / zandoc / zanpkg）都能编过，`int_width` / `numeric_cast` 输出正确。
  **剩下的**：

  **A0-1 / A0-2 实测（2026-07-27）**：已把 Zan 侧 `extern` 与 C 侧 `EXPORT`
  签名逐个对照过一遍，规模是确定的：
  * **23 处窗口句柄**：C 侧是 `iptr`，Zan 侧却写 `int`（`zan_gui_create_window`
    的返回、`zan_gui_present` / `show_window` / `set_title` … 的 hwnd 参数）。
    **已改成 `nint` 并提交**——`nint` 在两种宽度下都是指针宽，与 A0-1 无关，
    先落地不会影响现状。
  * **383 处坐标 / 尺寸 / 颜色**：C 侧 `i64`，Zan 侧 `int`，要一起改成 `int32_t`。
    改法是机械的（按 Zan 声明逐参数替换），改完编译器和 GUI 全部编过，
    `gui_icon` / `gui_stack` / `gui_webview_interop` 输出正确。
  * **真正的阻塞不在 FFI，在标准库把 `int` 当 64 位量用**：`map_type` 一改 i32，
    `ByteBuffer.ReadU64()` 返回 `int` 就把 `9000000000` 截断
    （`conformance_native_memory` 的 `bb-u64` 失败），ZanDb 的页 id / 偏移
    （`BTree.zan` 里 `int nxt = pg.ReadU64();` 等数十处）同理。这类截断
    **是静默的**，只有恰好有断言的用例会红。
  * 因此 A0-1 的实际工作量是：**先把标准库里承载 64 位量的 `int` 全部改成
    `long`**（ByteBuffer 的 U64 系列、ZanDb 页 id/偏移、文件偏移、时间戳），
    再同一批切 map_type + 383 处 C 签名 + 重新生成 `golden_structs_ir`
    （IR 里 `int` 字段会从 i64 变 i32，golden 必须一起更新）。
    `gui_css` / `gui_datatable` 在 i32 下也失败，已定位（见下）。

  **第二轮实测（2026-07-27，i32 临时切换）**：
  * 标准库非 ZanDb 的 64 位量已改 `long` 并提交（`818cac5`）：ByteBuffer 的
    U64/VarInt、NativeMemory 的 GetI64/SetI64、DateTime.epoch（顺带 Y2038）、
    TimeSpan.total、WebView2 事件 token、Win32Shell 的性能计数器。
    切 i32 后 `conformance_native_memory` 已通过。
  * **不定宽存储是 i32 下的堆破坏源**：不透明指针让宽度不匹配的 store 逃过
    verifier，`obj.field = 2` 会对 4 字节字段发 `store i64 2`，多写 4 字节。
    两个 int 字段的类一释放就破坏堆（`DataTableState.Create()` 直接崩）。
    已修：irgen 的 store 统一走 `zan_store_fit`（`9189e7b`），i64 下是恒等。
  * **`gui_css` 剩余失败是语义问题，不是 bug**：ARGB 颜色存在 `int` 里，
    i32 下 `0xFF282830` 变成负数（golden 写的是 4280296496）。
    两条路：颜色改 `uint`（Gui 下 287 处声明，混色代码的移位语义要一起过），
    或按 .NET `Color.ToArgb()` 的做法保留 `int`（负值）并更新 golden。
    **待定，需要决定。**
  * `gui_datatable` 在 store 修复后从第 10 行推进到第 333 行，剩余崩溃点
    （`UndoDepth` / `Undo` 附近）尚未复现出最小用例。
  * 当前仓库状态：`map_type` 仍是 i64，383 处 C 签名改动未提交。

  **颜色的最终形态（2026-07-27 决定）**：不把颜色改成 `uint`，改成 CSS 化的
  值类型 `Gui.Color`（`f632a44`）——内部一个 packed `uint argb`，
  `Color.Parse("#d92b2b" / "rgba() " / "transparent")`、`ToCss()`、
  `ToArgb()` 只在渲染 / 平台边界出现、`Color.None()`/`IsNone()` 取代 alpha=0
  哨兵。这样 A0-1 切 i32 后 `gui_css` 的有符号问题不复存在。
  前置已完成：
  * 值类型此前不可用，两个缺陷已修（`33a66df`）：返回的 struct 当接收者要落
    临时槽；集合槽是 8 字节，struct 元素要打包/解包，超宽的给出诊断。
    新增 `tests/conformance/value_structs`。
  * struct 与整数不兼容此前只有 LLVM 校验失败（无行号），已改成带位置的
    类型错误（`94cf827`），迁移时靠它扫残留点。
  尚未做的迁移（一整批，必须一次做完）：`Canvas` 的绘制参数（1223 处调用，
  连 tests/examples 是 1634 处）、`Theme` 的颜色 token、`StyleBox` 的 11 个
  颜色字段（175 处引用），以及 `App.LerpColor` / `Fx` / `Effects` /
  `ChartView` 里的颜色算术改成 `Color` 上的方法。

* **A0-2** FFI 边界按声明类型的真实位宽 lower。现在 `map_type`（`irgen.c:1601`）对
  `int`/`uint`/`long`/`nint` 一律给 i64，于是：传参时把 64 位塞给期望 32 位的 C 函数；
  取返回值时按 64 位读，而 C 只保证低 32 位有效，**高位是未定义的**
  （今天在 x86-64 上大多碰巧能跑，属于潜伏问题）。
  这正是 `SDL3/Native.zan` 类注释写明的垫片存在理由：
  "Zan integers are 64-bit while many SDL C parameters are 32-bit"。
* **A0-3** 结构体字段布局：`register_struct_type`（`irgen.c:1843`）对每个字段直接
  `map_type`，所以 `int` 字段占 **8 字节**，`[repr(C)]` 下与 C 不符。
  A0-1 之后自动修复大半，剩余交给 A2-2。
* **判定**：`unsigned_types` / `int_width` / `numeric_cast` 三个 conformance 保持通过
  （`int_width.zan` 里 `int n = s.Length`、`i < xs.Count` 这类 i64→i32 隐式窄化
  要一并处理）；新增 extern 位宽用例：调 C 侧 `int32_t` 收发函数，值域两端不失真。

### A0 必须分两步走

〔已实测〕193 处 extern 用 `int` 承载句柄。样本：

```zan
static extern int GetModuleHandleA(int reserved);   // 返回 HMODULE
static extern int SetWindowTextA(int hwnd, string text);
static extern int zan_gui_present(int hwnd, int surfaceId);
```

这些今天**只是因为 `int`=64 位才碰巧正确**。直接切 int=32 会把 64 位句柄静默截断，
是最坏的一类失败：不报错、随机崩。

1. **第一步（零风险）**：所有句柄/指针型 extern 的 `int` 改成 `nint`。
   在 `int` 还是 64 位时**语义完全不变**，纯粹是把"碰巧对"变成"明确对"。→ 已完成，见下。
2. **第二步**：再切 `int`=32。

同时 `zan_gui_*` 那 103 处 C 导出目前是 i64 签名，切换前 C 侧签名要先明确成
`intptr_t` / `int32_t`，不能依赖"Zan int 恰好是 64 位"。

### A0-0 实测结果〔2026-07-27，✅ 已完成第一步〕

**改了什么**：26 个 `.zan` 文件、**330 处声明**。分类不是机械替换——只改真正承载
指针宽/64 位值的位置，真正的 32 位 C `int`（BOOL / DWORD 错误码 / 消息号 / 样式 /
坐标 / 长度 / flags / 列号）**保持 `int`**，这正是 int=32 之后会自然正确的部分。

| 类别 | 处理 | 例子 |
|---|---|---|
| Win32 句柄 | → `nint` | `HWND` `HDC` `HBRUSH` `HGDIOBJ` `HMODULE` `HANDLE`：`CreateWindowExA` `GetDC` `SelectObject` `WinCreateMutex` `OpenProcess` |
| C 运行库指针 | → `nint` | `FILE*`（`fopen`/`fclose`/`fseek`/`fread`/`fwrite`/`fgets`/`plat_popen`）、`DIR*`（`opendir`/`readdir`/`closedir`）、`memcpy` 返回值 |
| 数据库句柄 | → `nint` | `sqlite3*` `sqlite3_stmt*` `PGconn*` `PGresult*` `SQLHANDLE`（含 `SQLAllocHandle` 的 out 参数） |
| Windows `SOCKET` | → `nint` | `ws2_32` 那组（`UINT_PTR`）；**POSIX fd 保持 `int`** |
| 64 位值 | → `long` | `time`（`time_t`）、`GetTickCount64`、`sqlite3_last_insert_rowid`、`sqlite3_bind_int64` |

**故意不改的**：
* **POSIX 的 `socket`/`accept`/`bind`/`connect` 家族保持 `int`**——POSIX fd 本来就是
  32 位 `int`，改成 `nint` 反而会把 32 位返回值当 64 位读，正是要消灭的那类 bug。
* OpenSSL / BIO 那组用 `string` 承载 `SSL*`/`BIO*` 指针——这是另一个 FFI 设计问题
  （`calloc` 返回 `string` 的同一惯用法），归 A1/A2，不在这里顺手改。

**载体变量**（`ZAN_WARN_NARROW=1` 扫描 241 个 examples+conformance 编译单元得到
26 处，全部改对）：`File.zan` 9 处 `int fp` → `nint fp`、`PageFile.fp` 字段、
`Directory.handle`、`PgConnection.handle` 字段 + 8 处 `int res = PQ*` + `ReadTuples(nint)`、
`ServerMetrics` 2 处 `GetCurrentProcess`、`DbRegistry` 的 `OpenProcess`、
`ResourcePack` 的 `fopen`、`Socket.Resolve` 的 `gethostbyname`。

顺带修的**真实缺陷**（不是位宽整理，是本来就错）：
* `Random.state` 是 64 位 LCG（`state * 6364136223846793005`）却声明成 `int`，
  int=32 之后整个生成器失效。已改 `long state` / `long NextRaw()`，
  `Next()` 按 C# 语义返回 `(int)(r & 2147483647)`。
* `Guard.baseTick/baseQpc/qpcFreq` 是 `GetTickCount64`/QPC 的 64 位读数却是 `int`，
  `Le64()` 也返回 `int`。已改 `long`。

**验证**：`ctest -j8` **553/553 通过**（新增 `ffi_nint_handle` ×conformance/determinism/leakcheck，
覆盖 `nint` 返回值、`nint` 参数、`nint` 字段、经 Zan 函数往返、别名相等、`--check-leaks` 干净）。

### A0-0b 窄化诊断〔2026-07-27，✅ 已完成〕

**为什么需要**：Zan 此前**完全没有窄化检查**——`long a=7; int b=a;` 和
`double d=1.5; int c=d;` 都静默通过，而 C# 这两个都是编译错误（CS0266）。
`checker.c` 只有 463 行、根本不做赋值/初始化的转换检查，真正的类型推断在 irgen。
这意味着 int=32 一旦切换，"用 `int` 变量接住 64 位句柄"的地方会**静默截断且不报错**。

**实现**：`irgen_expr_core.c` 新增 `conv_rank()` / `warn_narrowing()`，
按 C# 规则判定（只有目标能容纳源的全部取值才是隐式转换；`nint` 视为指针宽，
`nint → int` 属于窄化）。挂了三个点：变量初始化（`irgen_stmt.c`）、
赋值（`emit_expr_assignment`）、实参传递（`emit_arg_typed`）。
`ZAN_WARN_NARROW=1` 开启，**在切 int=32 的同一次改动里改成默认报错**。

**尚未挂的点**（记下来，别当已完成）：`return` 语句还没查——irgen 里没有
"当前方法返回类型"的 `zan_type_t`，需要先补 `g->current_fn_ret_zan_type`。
字段赋值经由 `assign_lhs_type` 已覆盖，但字段**初始化器**未覆盖。

### A0-1 的实际工作量（扫描发现）

除了句柄，stdlib 里还有一批"用 `int` 装 64 位量"的地方，int=32 会直接坏：
`ServerMetrics.LeInt()` 读 8 字节却返回 `int`（CPU 100ns 计时、RSS 字节数）、
`Guard` 的 QPC 计算（已修）、各处 `Le64` 风格的手写小端解码。
这些不是 FFI 声明问题，是**语义类型选错**，要在 A0-1 里逐个定型。

* **A0-1a** ✅ 已修（2026-07-28）把标准库里真正的 64 位量从 `int` 定型成 `long`，
  并按数据模型（不是按"消警告"）决定每一处到底几位：
  - **文件位置/大小是 64 位**：`PageFile` 的 `SeekFd`/`PReadFd`/`PWriteFd`/`TruncateFd`
    改成真实 ABI（`_lseeki64`/`lseek`/`pread`/`pwrite`/`ftruncate` 的 `off_t`/`__int64`），
    `ReadAt`/`ReadRaw`/`WriteAt` 的 offset、`Append`/`Size` 的返回值 → `long`；
    字节数仍是 `int`（缓冲区长度本来就是 `int` 上界）。
  - **共享内存/原子量是 64 位**：`rt_sync.c` 一直是 `int64_t` 值 + 64 位句柄，
    `AtomicInt` / `SharedTable` 的 Zan 声明改成 `nint handle` + `long` 值，
    上层 `RouteStats` / `RouteTable` / `PermTable` / `Router` / `Worker` 跟着走 `long`
    （路由哈希是 64 位；`auth`/`perm`/`menu`/`mask` 这类应用级标志仍是 `int`，
    读出时显式 `(int)` 收窄）。
  - **事务号是 64 位、页号是 32 位**：ZanDB 元页把 txn/root/free/pageCount 都写成 u64，
    但页号 2^31 × 4 KiB 已是 8 TiB，且它同时是 `List`/缓存的下标。因此
    `Pager.txnid` / `CommitId` / `OldestSnapshot` / `freeTxns` → `long`，
    页号保持 `int`，所有从盘上读回的页号统一过 `Pager.PageIdOf(long)`——
    越界即判定为损坏（返回 0），不做静默截断。偏移一律 `(long)pid * PAGE_SIZE`。
  - **`ByteBuffer.ReadVarInt` 的移位是真 bug**：`(b & 127) << shift` 在 i32 下
    shift ≥ 32 会丢高位，改成 `(long)(b & 127) << shift`。
  - 其余：`DateTime.Pad2(long)`、`HttpClient` 的 `fopen` 结果 → `nint`、
    `ServerMetrics` 的累计/峰值毫秒 → `long`、`Collection` 的 varint 计数显式 `(int)`。
  〔已实测〕`ZAN_WARN_NARROW=1` 全量扫描（`tests/conformance` + `examples` +
  `templates` 297 个单元，另加 `tests` 其余 + `src/ide_zan` + `src/selfhost` 66 个）：
  改前 24 处唯一窄化警告，改后 **0 处**。
  `ctest -R 'const_narrowing|byte_unsigned|array_init_foreach|zandb|web_route|web_perm|http_client|datetime|timespan|metrics'`
  42/42 通过。
* **A0-1a 编译器修复**：窄化诊断原先对 `byte b = 255;` 也报警，这与 C# 不符——
  C# 有**隐式常量表达式转换**（编译期已知且能容纳就不需要 cast）。
  按"先修编译器、不要绕过"的规则，在 `irgen_expr_core.c` 里补 `const_int_expr()` /
  `const_fits()`：常量折叠字面量与 `+ - * / << & | ~` 的常量表达式，能容纳就不报。
  回归用例 `tests/conformance/const_narrowing.zan`（`byte`/`sbyte`/`short`/`ushort`/
  `uint` 常量初始化、常量表达式 `1 << 7`、静态字段、`new byte[] { 0, 128, 255 }`）。

## A1 `Span<T>` 编译器内建 〔依赖 A0〕

* **A1-1** `Span<T>` = `(nint base, int length)`，编译期知道 `T` 宽度；
  `s[i]` 直接 lower 成 `getelementptr T` + load/store，不经过 `NativeMemory`；带 noalias。
  **必须是编译器内建**，做成库类型 LLVM 看不穿，别名分析依然失败。
* **A1-2** **删除** `NativeMemory` 的 `GetByte/GetU16/GetU32/GetI64/SetByte/…`
  整排 intrinsic，不留兼容层。
* **A1-3** 提供 `byte[]` / `Span<byte>` 作为全库统一的字节缓冲类型（配合 B2）。
* **判定**：Zan 版 `blur_rect` / `fill_radial` / `blit_image` 对 C 版基准 ≥0.9x。
  达不到先修优化管线，不要往下推进。

## A2 FFI ABI 分类 〔依赖 A0〕

现在 extern 声明的全部逻辑是 `irgen_emit.c:294-307`：
逐个 `map_type` 后 `LLVMFunctionType(rt, pt, pc, 0)`，`IsVarArg` 恒为 0，无任何 ABI 处理。

* **A2-1** 结构体按值传参/返回：SysV AMD64 / Win64 / AAPCS64 的 eightbyte 分类，
  `byval` / `sret` attribute。（四项里唯一的大工程，先做 Win64 + SysV）
* **A2-2** `[repr(C)]` 布局 + `[StructLayout(Explicit)]` + `[FieldOffset(n)]`。
  **union 不新增类型 kind**——所有字段 `FieldOffset(0)` 的 explicit struct 即 union，
  用来表达 `SDL_Event`(128B) / `XEvent`(192B) / `DEBUG_EVENT`。
  偏移本该由编译器从 repr(C) 算出，而不是在 Zan 里硬编码常量。
* **A2-3** 变参：加 `[DllImport(..., Variadic = true)]`。
* **A2-4** `signext` / `zeroext` + 调用约定属性
  （全 compiler 里 `stdcall` / `CallConv` 出现 0 次）。
* **判定**：X11 的 `XEvent` 按字段直读、SDL 的 `SDL_FRect` 按值传、
  `objc_msgSend`（含 `_stret` 变体）能调通。第三条通了，mac 后端就不再是"搬不动"。

## A3 `zanc bindgen` 〔依赖 A2〕

从 C 头生成 `[repr(C)]` struct + extern 声明；仓库里已有 C 前端可复用。
一次覆盖 Xlib / SDL3 / FreeType / Win32，并给 `stb_image` 一个
**"链接预编译库而非重写"** 的正当出口——链接第三方库不算"用 C 写"。

## A4 无运行时执行模式 〔依赖 A0〕

* **A4-1** `[NoRuntime]` 函数：checker 禁止分配 / ARC / throw，irgen 不插 retain-release。
  （`unsafe` 现在只是 `parser.c:232` 的一个 modifier bit，`TK_UNSAFE` 全库出现 4 次、无语义）
  这是 `rt_crash` 那类"进程已损坏时还要执行"的代码唯一的表达方式。
* **A4-2** 外部线程 attach / detach 运行时。X11 / SDL / Cocoa 都会从非 Zan 线程回调进来。

## A5 SIMD 〔依赖 A1，可选〕

A1 之后若 LLVM 自动向量化已够用则跳过。

## A6 编译器对外 API 〔工具链 Zan 化的前置〕

`lsp/intellisense.c` 要用 parser + binder，`dap` 要用调试信息。
给 compiler 一个稳定 C API（A3 顺手生成绑定），或走自举（`docs/BOOTSTRAP.md`）。
前者见效快，后者是终局。

## A7 泛型：约束接口方法调用会崩 codegen 〔**已实测**，阻塞 B3-1〕

`docs/bugs/generics-uniform-repr.md` 标注 FIXED（2026-07-23，方法级单态化）。
实测：第 5 条（Dictionary 装泛型类值）**已修**，`d["x"].Get()` 返回 7 正确；
第 6 条（`foreach (Dictionary.Keys)`）**已修**，正常输出。

**但撞出一个未记录的 codegen 崩溃**：通过类型参数调用约束接口的方法就挂。

```zan
interface IConn { string Name(); }
class MyConn : IConn { string Name() { return "mine"; } }

static string CallIt<T>(T c) where T : IConn { return c.Name(); }   // ← 崩
```

```
error: LLVM verification failed: Function return type does not match
operand type of return inst!
  ret i32 0
 ptr
```

缩小范围后（探针 `_scratch/pg3.zan` `pg4.zan` `pg5.zan`）：

| 形态 | 结果 |
|---|---|
| 泛型类 + 接口约束，**不调**约束方法（`pg4`） | 正常 |
| 泛型类 + 接口约束，**调**约束方法（`pg3`） | **崩** |
| 泛型**方法** + 接口约束，调约束方法（`pg5`） | **崩** |

现有 `tests/conformance/generic_constraints.zan` 给的是**假信心**：它只存取 `T`，
取出来赋给具体类型变量后才调 `Area()`，**从未通过类型参数调用接口方法**。

* **A7-1 ✅ 已修（2026-07-27）**。
  **原来记的根因是错的，作废**：不是"`where` 约束没被挂到 `T` 上"——去掉约束
  （`static string CallIt<T>(T c) { return c.Plain(); }`）、换成类约束、甚至只读
  **字段**（`return c.id;`）**一样崩**。约束与此无关。

  **真实根因**：泛型方法/泛型类除了每个实例化的单态副本，**还会再发射一份"擦除"
  实例**（`emit_user_methods` Pass A/B 的 `cur_variant == NULL` 变体）。擦除体里
  `c.Name()` / `this.item.Name()` 的接收者静态类型就是 `T`，成员无从解析，
  于是落到 `emit_expr` 的兜底 `ret i32 0`，与声明的 `ptr` 返回类型冲突
  → LLVM 校验失败。**只要源码里存在这种泛型体，整个程序就编译不过**，
  哪怕它从未被调用。

  修法（`irgen_emit.c` / `irgen_call.c` / `irgen_expr_core.c`）：
  1. 新增 `method_is_tp_template()` / `class_member_uses_tp()`：扫描方法体，判断它
     是否穿过一个"声明类型是类型参数"的值（参数、局部、`T` 字段、`this.T字段`）
     去取成员。这类体**没有擦除形态**，只能单态化。
  2. 泛型**方法**：这类方法不再发射擦除实例，`get_or_create_method_spec` 对它们
     跳过 `mspec_bind_worthwhile` 启发式（否则 `CallIt<int>` 会无处可去）。
     非 static / async 的这类方法暂时给出明确编译错误，取代原来的 IR 崩溃。
  3. 泛型**类**：擦除变体的函数**仍然声明**（保持所有引用可链接），但函数体换成
     `abort()` + `unreachable`；每个具体实例化的变体照常发射真实体。
  4. 未限定调用 `CallIt(c)`（同类内不带类名）此前**根本没走单态化路径**，
     补上 `try_method_spec`。
  5. `infer_expr_type` 的字段访问此前直接返回字段的**声明**类型，`Pool<Conn>.item`
     仍是 `T`；现在按接收者实例化替换（`subst_type_param` + `concretize`）。

  实测：`CallIt<Conn>(c)` / 类型推断的 `CallIt(o)` / `Sum<T>` 循环内调约束方法 /
  `FieldOf<T>` 读字段 / `Pool<Conn>.Describe()` / `Pool<Other>.Score(4)` 全部正确。
  新增回归 `tests/conformance/generic_tp_member_call.zan`（含 conformance /
  determinism / leakcheck 三份）。全量 `ctest -j8`：**547/547 通过，37.23 s**。

  **仍未覆盖的边界**（不要当已完成）：实例方法和 async 泛型方法的单态化尚未实现，
  这类体现在是明确报错而不是编译成垃圾；B3-1 的 `DbPool<TConn>` 若需要
  **实例**泛型方法，得先把 spec 扩展到带 `this` 的形态。
* **A7-2 ✅ 已修**（构造类型上的静态成员访问）。`Box<int>.Create(7)` /
  `Pair<int, string>.Of(3, "three")` / `Counter<double>.Twice(21)` 现在都解析并
  绑定到对应实例化。回归：`tests/conformance/generic_static_member_access.zan`
  （conformance / determinism / leakcheck 三份全绿）。
* **A7-3 ⚠️ 已验证，但结论变了**（2026-07-27 复验，`_scratch/pc.zan`）。
  泛型累加器**本身**不漏：`Acc<string>` 在循环里反复 `Set()` 输出正确、
  `--check-leaks` 干净；`string acc = acc + x` 的自累加也干净。
  但复验时撞到一个更严重的问题，见 **A7-5**：`T` 绑定到**引用类型**时，
  泛型字段的读取根本不对（不是泄漏，是取值错误 / 堆损坏）。
* **A7-4 ✅ 已修**（不带花括号的单语句体）。`if`/`else`/`while`/`for`/`foreach`/`do`
  的语句体现在走 `parse_embedded_stmt()`：接受任意单语句，并按 C# 的作用域语义包一层
  block；和 C# 一样，声明不能当嵌入语句（CS1023），会给出"用花括号括起来"的报错。
  回归：`tests/conformance/stmt_braceless_bodies.zan`（含 `else` 悬挂、`for`/`foreach`
  单语句体、单语句体里的 `break`/`continue`、以及单语句体里的 `await`）。
* **A7-5 ✅ 已修（2026-07-27）泛型类的字段在 `T` 绑定引用类型时取值错误 / 堆损坏**。
  `_scratch/pd.zan`：`class Acc<T> { T cur; void Set(T v){this.cur=v;} T Get(){return this.cur;} }`
  修复前 `Acc<Node>`（`Node` 是 class）直接 `a.cur` 以 `0xC0000374` 退出，
  `a.Get().tag` 打印 `0`；`Acc<List<string>>` 的 `.Count` 打印 `0`；`Acc<string>` 正常。
  **四个独立缺陷**：
  1. **字段写入用的是声明类型而不是实例化后的类型**（`irgen_expr.c` 的三条
     `obj.Field = v` 路径）。字段声明成 `T`，`is_rc_managed_type(T)` 为假 ⇒ 降级成裸指针
     store，调用方交过来的 +1 被丢掉，对象在字段还指着它的时候就被释放。
     修法：新增 `field_store_type()`＝`concretize(subst_type_param(字段类型, 接收者类型))`。
  2. **返回类型是类型参数的方法，其返回类型没有按接收者替换**（`irgen_expr_core.c`
     的 `infer_expr_type` 实例调用分支）⇒ `a.Get().tag` 找不到 `Node` 的字段，
     把擦除结果当数字用。修法：`subst_type_param_deep` + `concretize`。
  3. **每个泛型类只有一份析构器**（`class_release` 只按类符号做键）⇒ 走到 `T` 字段时
     类型仍是 `T`，一律跳过，字段持有的整个对象图泄漏。修法：析构器改为按
     （类符号，实例化类型）成键，新增 `site_inst[]` 记录每个分配点的实例化类型，
     `emit_all_class_releases` 为每个真实分配过的实例化各发射一份（`Acc<Node>` 与
     `Acc<string>` 因此是两个 `__zan_release_Acc_*`）。
  4. **从临时接收者上读字段不释放那个临时对象**（`Make().name` / `a.Get().tag`）。
     这是 `finish_index_of_temp`（`Split(",")[0]`）的成员访问版，此前缺失。修法：
     新增 `finish_member_of_temp()` + 分类器 `expr_member_of_owned_temp()`：
     rc 字段先 retain、随后释放容器，并把该成员表达式报告为"持有结果"，由消费方释放。
  回归：`tests/conformance/generic_ref_type_fields.zan`（`Acc<Node>` 的字段/方法/链式读、
  循环内反复覆盖、`Acc<string>` / `Acc<List<string>>` / `Acc<int>`、`Pair<Node,int>`、
  临时接收者的字段读），conformance / determinism / leakcheck 三份全绿；
  全量 `ctest -j8` **588/588**。
  **注意**：这一项解掉了 B3-1 的阻塞——`DbPool<TConn>` 的 `TConn` 字段现在按真实类型
  管理所有权。仍未做的是**实例泛型方法的单态化**（A7-1 结尾那条），与本项无关。

## A8 ARC / 异常 / 协程交互 〔**已实测，三份 repro 全部仍成立**，2026-07-27〕

`docs/bugs/` 三份 repro 全跑过，**结论：三个都还在，且两个比文档记录的更严重**。
这一组是**最高优先级**——ARC 与异常/协程的交互不可靠，意味着任何用 Zan 写的上层代码
都不可信，而"上层一律用 Zan 重写"正是整个计划的地基。

* **A8-1 ✅ 已修（2026-07-27）** `repro_await_in_catch_loop` → 原为 access violation `0xC0000005`。
  探针矩阵把形态收窄了（见 A8-5）：**`await` 出现在 `catch` 块里就崩，与循环无关**。
  **根因（两处，都是"catch 状态放在栈上，而 await 会让本次 `$resume` 返回"）**：
  1. catch 捕获的异常对象及其 owned 标志存在栈 alloca（`eh.exc.slot` / `eh.excown.slot`），
     恢复后 catch 收尾去释放一个已作废的指针。
     修法：改为帧驻留，新增 `ASYNC_FRAME_CEXC` / `ASYNC_FRAME_CEXC_OWNED` 两个数组字段，
     按 try 的编译期 handler id 索引（`irgen_async.c` / `irgen_emit.c`）。
  2. `catch (Exception e)` 的**变量绑定 `e` 本身**也是 `emit_entry_alloca` 的栈槽
     （`irgen_stmt.c` catch body），await 之后再读 `e.Message` 就是野指针。
     修法：async 扫描把 catch 变量登记成 storage-only 帧局部（异常对象由帧的 per-handler
     槽持有，绑定只借用，不参与 ARC 释放），emitter 优先用该帧槽。
  证据：`tests/conformance/async_await_in_catch.zan`（循环内多轮 throw→catch→await，
  再加一轮 catch 内先 await 后读 `e.Message`）输出
  `caught boom0/1/2 → late boom9 → total=4`，退出码 0，leakcheck 干净。
* **A8-2 ✅ 已修（2026-07-27）** `repro_dict_urldecode_key` → 原为 **use-after-free + 堆损坏
  `0xC0000374`**（某个键的内容变成程序自己上一行的输出文本 `k[5]=[k[1]=[method]]`）。
  **文档记的复现条件是错的**：与 `UrlDecode`、`Split`、`Substring` 都无关。
  最小化探针（`_scratch/async_probe/d1..d8`）证明触发条件只有一个——
  **Dictionary 是从一个方法里 `return` 出来的**。同样的循环写在 `Main` 里全部正常（d1–d5），
  一旦挪进工厂方法就必坏，连纯 `Substring` 当键也坏（d8）。
  **根因（设计缺口，不是补丁点）**：Dict 被**刻意排除在 rc 集合之外**（`is_rc_collection_type`
  原注释："Dict is deliberately excluded (still header-less)"）。没有 rc 头 ⇒ 一个 dict 的
  所有权无法表达：函数退出时 `emit_release_dict_local` 无条件释放它的全部 key/value，
  于是调用方拿到的是一堆悬空指针；若改成不释放，则必然泄漏（实测确实报 14 个对象）。
  **修法（补能力，不降级）**：**让 Dict 成为和 List / StringBuilder 同级的 rc 集合**——
  `new Dict` 改走 `emit_alloc_rc_collection`（coll_kind 3，带 16 字节 rc 头；`zan_rt_alloc`
  不清零，所以哈希索引三个字段显式置 0），新增 kind 3 的 per-site 析构（先释放 key/value，
  再 free keys/values/index 三个缓冲，最后 `zan_rt_release`），`is_rc_collection_type` 纳入
  Dict；随之删掉 `local_is_dict` / `emit_release_dict_local` 这条特例路径——dict 局部从此
  走普通 ARC 路径（`return d` 自动 retain、赋值自动 retain/release、作为字段/元素也正确）。
  证据：`tests/conformance/dict_returned_from_method.zan`（工厂方法返回
  `Dictionary<string,string>`，以及返回 `Dictionary<string, List<string>>` 验证值也被正确
  retain）输出正确、`--check-leaks` 干净；原 repro 七个键全部正确、退出码 0、无泄漏；
  全量 541/541 通过。
* **A8-3 ✅ 已修（2026-07-27，抛出帧与同帧 try；跨帧部分见 A8-12，也已修）**
  `repro_throw_leaks_locals` → 原泄漏 6 个对象（`JsonValue.zan:39/60/62/63`）。
  **根因**：`AST_THROW_STMT` 只做"存异常 + longjmp"，**从不释放本帧的持有型局部**
  （`irgen_stmt.c`）。longjmp 跳过了 throw 与 handler 之间所有作用域退出代码，
  这些局部的 release 永远不会执行。
  **修法**：新增 `g->throw_locals_base` —— 记录"longjmp 会跳过其 release 的第一个局部"：
  函数内有外层 try 时是该 try body 的 locals 起点（body 内的局部由 throw 站点释放，
  try 之外的局部照常在函数出口释放，**不会双重释放**，因为 `throw` 是 noreturn、
  正常路径的 release 只在未抛出时到达）；无外层 try 时是 0（整帧被放弃）。
  throw 站点在 longjmp 前 `emit_release_owned_locals_range(g, locals, base)`。
  嵌套函数 / lambda 体进入时该基准归零并在退出时恢复。
  证据：`tests/conformance/throw_releases_locals.zan`（抛出帧带局部、同帧 try 内抛出且
  try 外局部仍存活、循环内反复 throw/catch）输出正确、`--check-leaks` 干净；
  原 repro 现在 0 泄漏；全量 541/541 通过。
* **A8-4 ✅ 已修（2026-07-27）**〔**新发现，无既有记录**〕`foreach` 循环体内 `await` → 原为**编译期崩**：
  `LLVM verification failed: Instruction does not dominate all uses!`
  （`%cnt37`/`%data38` 在被 resume 分支使用的位置不被支配）。
  **根因**：foreach 把集合的 `data`/`cnt` 缓存成 SSA 临时量放在循环前置块，而 resume 是
  直接跳进 cond/body 的——前置块不执行，这些值既不支配使用点、也活不过挂起；
  索引和迭代变量同样是栈槽。
  **修法（帧驻留，不降级）**：给 async 体内每个 foreach 分配稳定 id，把集合指针、索引、
  迭代变量登记为帧驻留槽（集合与迭代变量是 storage-only，不参与协程清理的 ARC 释放，
  元素归集合所有）；cond/body 各自从帧槽重新读集合再取 `count`/`data`，
  集合表达式仍只求值一次。
  证据：`tests/conformance/async_await_in_foreach.zan`（外层 foreach+await、嵌套 foreach、
  `continue`/`break`）输出与期望逐行一致，退出码 0，leakcheck 干净。

* **A8-8**〔**顺带发现并已修**，2026-07-27〕**`foreach` 里的 `break` / `continue` 一直被静默忽略**——
  与 async 无关，**同步代码同样错**。`AST_FOREACH_STMT` 的降级从未设置
  `g->break_target` / `g->continue_target`（`while`/`for` 都设了），所以 foreach 内的
  `break`/`continue` 要么跳到**外层**循环，要么（无外层循环时）什么都不做直接往下走。
  实测 `_scratch/async_probe/a17_sync_foreach_breakcont.zan`：`continue` 版本本应 4 却输出 6，
  `break` 版本本应 3 却输出 6。
  修法：新增 `fe.step` 块承载索引自增并作为 `continue` 目标（直接跳 cond 会在同一元素上死循环），
  `end` 块作为 `break` 目标，同时保存/恢复外层目标与 `loop_locals_base`。
  修后 `cont=4 / brk=3`，async 版 `afterContinue=6 / afterBreak=11` 均正确。

* **A8-9**〔**顺带发现并已修**，2026-07-27〕**`var x = await F()` 会把引用类型结果当整数**。
  `string s = await Echo("hi")` 正常，`var s = await Echo("hi")` 打印指针数值。
  **根因（两处）**：
  1. ANF 规范化把任何 await 初始化器提升成一个**恒定声明为 `int` 的临时量**
     （`anf_hoist_await`），结果类型被抹掉；显式类型的声明靠自己的类型转换把 i64 转回指针，
     `var` 没有类型可依据。修法：`T x = await E;` 本身就在语句位置，不再提升
     （与既有的 `await E;` 表达式语句同一处理，注释里已写明"提升会抹掉真实结果类型"）。
  2. await 站点本身也不还原类型：帧的 result 槽是裸 `i64`，`await` 直接返回这份 i64。
     修法：新增 `coerce_await_result()`，按被 await 调用的声明返回类型把 i64 还原为
     指针 / double（`irgen_expr.c`，三个返回点统一）。
  这只是把类型在 await 站点补回来，**帧 result 槽仍是硬编码 `i64`**——typed frame result
  仍是 A0 落地时必须做的事（见 A8-7）。
  证据：`tests/conformance/async_var_locals_across_await.zan`
  （`var` 的 string/int/double/bool、跨两次 await）全部正确。

* **A8-10**〔**顺带发现并已修**，2026-07-27，测试基础设施〕**每跑一次 ctest 都要把 165 个
  conformance 程序全部重新编译+链接三遍**（conformance / leakcheck 各一次编译，determinism
  两次 IR），即便源文件和编译器都没变——纯粹的浪费。
  修法：测试产物是 (源文件, zanc, stdlib) 的纯函数，`tests/run_case.cmake` /
  `run_leakcheck.cmake` / `run_determinism.cmake` 增加 up-to-date 判定，
  CMake 侧用 `stdlib.stamp`（依赖全部 `stdlib/**/*.zan`）把 stdlib 的时间戳带进比较。
  实测：改动后重复跑同一份构建 **64.5s → 22.1s**，538/538 仍全绿。

* **A8-11**〔**顺带发现并已修**，2026-07-27，测试基础设施〕**缓存会把一次偶发的坏产物永久固化**。
  A8-2 修完后的第一次全量出现 125 个 leakcheck 失败（`Access violation` / `unknown error`），
  但同一份源码手工重编即通过——即那批 exe 是构建期偶发（并行链接/杀软介入）产生的坏映像。
  由于 A8-10 的 up-to-date 判定只看时间戳，坏 exe 比 zanc 新 ⇒ 之后每次 ctest 都直接复用它，
  失败**稳定复现**且永不自愈（删掉 exe 后立刻全绿）。
  修法：`run_case.cmake` / `run_leakcheck.cmake` 在"程序非零退出 / 输出不符 / 报泄漏"时
  先 `file(REMOVE ${OUT_EXE})` 再 FATAL——失败绝不复用产物。
  未收尾：并行编译偶发坏映像的**根因未定位**（16 路并行重复编译同一文件 15/15 正常，
  无法稳定复现；`.zan-cache` 未启用，临时 `.o` 按输出名唯一，暂列为观察项）。

* **A8-12 ✅ 已修（2026-07-27）异常穿过中间栈帧时，中间帧的持有型局部泄漏**。
  A8-3 修好的是"抛出帧本身"和"同帧 try"；`A → B → C(throw)` 里 `B`（既没抛也没接）
  的局部 release 被 longjmp 跳过。原始实测 `_scratch/async_probe/t1_throw_locals.zan`：
  `Middle()` 的 `Node m` 与 `List<string> l` 各泄漏 1 个。

  **采用的是路线 1 的修正版（槽位影子栈），不是原来写的"值影子栈"**：条目存的是
  **变量槽地址**而不是值，所以重新赋值不需要同步更新条目；释放时从槽里读当前值并把
  槽置 null，因此正常路径的作用域退出 release 仍然正确（它只在没有异常时才执行到）。

  实现（`irgen_builtins.c` / `irgen_generics.c` / `irgen_stmt.c` / `irgen.c`）：
  1. 复用现有的 `__zan_eh_tmps` 展开栈：条目低位打 tag 区分"临时对象指针"和
     "变量槽"（rc 对象和 alloca 至少 2 字节对齐，低位必为 0）。
  2. 持有型局部在声明处 `emit_eh_tmp_push_slot(slot)` 登记，`local_var_t.eh_slot`
     记录已登记；作用域退出的三个 release 函数在释放时配套弹栈，栈深度回到进入
     作用域时的位置（循环体每轮平衡）。
  3. **释放时机在 throw 处，而不是 catch 处**——这是关键：longjmp 之后中间帧的栈内存
     位于新栈顶之下，`__zan_eh_tmp_unwind` 自己的调用就会把它覆盖掉。新增
     `__zan_eh_tmpmarks[]`（与 `__zan_eh_bufs` 平行，try 入口写入本 handler 的展开栈
     深度），throw 在 longjmp **之前**调用 `emit_eh_unwind_to_handler(top)` 释放到目标
     handler 的深度，此时所有中间帧都还活着。catch 侧原有的 unwind 保留（此时已是
     no-op，async 重入路径仍需要它）。
  4. A8-3 在抛出点的本帧 release 现在**只对 async 体保留**（async 局部驻留堆帧，由帧链
     释放）；普通帧交给展开栈，避免同一对象被释放两次。

  新增回归 `tests/conformance/throw_releases_middle_frames.zan`（conformance /
  determinism / leakcheck 三份），覆盖：一层中间帧、两层中间帧且中间帧局部被重新赋值、
  中间帧局部声明在循环体内、中间帧 catch 后再 throw、以及**不抛异常时同样的帧只清理
  一次**。原始探针 `t1_throw_locals.zan` 现在 `--check-leaks` 干净。
  全量 `ctest -j8`：**550/550 通过，71.42 s**。

  **仍未完成的边界**（不要当已完成）：
  * ~~展开栈仍是固定 4096 条的静态数组~~ → **已修（2026-07-28），见 A19**。
  * async 体的局部仍走 A8-3 的抛出点释放 + 帧链释放，没有登记到展开栈；
    "async 帧夹在同步中间帧之间"的混合形态尚未单独构造用例验证。
  * 这仍然是 setjmp/longjmp EH 之上的补偿机制，happy path 上每个持有型局部多一次
    push、一次 pop。**终局仍应换成 LLVM 真 EH（invoke/landingpad + personality）**：
    零开销 happy path，中间帧 cleanup 交给 unwinder，同时能解决 A8-7 记录的其他 EH
    设计落差。与 A0 的 typed async frame 一并规划。

* **A8-13 ✅ 已修（2026-07-27）catch 里再抛在 async 体内跳错 handler + 被放弃的 handler 泄漏异常**。
  三个独立缺陷，`_scratch/{b,c,d,e,f,g}*.zan` 探针矩阵定位：
  1. **async 恢复后 catch 内 throw 会跳回刚离开的那个 try**（`e4` 打印两次 `inner catch`
     后 `Unhandled exception`）。根因：`emit_async_eh_prologue` 的 `eh.land` 用
     `eh.co.id` 分派，而那个 alloca 在重新武装循环里被**最后一个** handler 覆盖，
     longjmp 落到哪个 buf 无关。修法：`eh.land` 从 `__zan_eh_top` 反算被跳到的
     handler 序号 `k = top - entry - 2`，取 `hstack[k]` 作分派 id，并把
     `hcount` 截到 `k+1`（丢掉 k 内层那些已失效的 handler）。
  2. **async 抛出点释放本帧局部后，把已释放的指针存回帧槽**，`co.exc` 再释放一次
     ⇒ 双重释放、堆损坏（`d7` 静默无输出）。修法：`emit_clear_owned_locals_range`
     在 A8-3 的释放后把这些槽清零（release(null) 是 no-op）。
  3. **`return` / `break` / `continue` / `throw` 离开 catch 体时不释放它捕获的异常**
     （同步与 async 都漏，`g1` / `f4`）。handler 入口把异常的 +1 转给自己（`exc.push`），
     只有 try 尾部的 `exc.hrel` 会还——而这几条边正好跳过它。修法：`g->catch_cleanups`
     记录当前所在的每个 catch 的异常槽与 owned 槽，上述四条边按
     `loop_catch_base` / `throw_catch_base` / 0 为界补发释放；因为 handler 体内声明的
     局部压在异常之上，不能 `tmp_pop`，新增 `__zan_eh_tmp_drop(obj)` 按身份注销那一条。
  证据：`tests/conformance/async_catch_rethrow.zan`（跨协程 catch 后再抛、同帧嵌套
  try 再抛、try 前持有托管局部 + 从 catch 里 return、循环内 catch 的 break/continue）
  输出逐行正确、`--check-leaks` 干净；全量 **568/568** 通过。
  ~~**仍缺**：C# 的裸 `throw;`（不带表达式的重抛）解析器仍不接受~~ → **已修，见 A18**。

* **A8-14 ✅ 已修（2026-07-27）异常落地时用帧覆盖栈槽，丢掉最后一次挂起之后写的局部**。
  `_scratch/p2.zan` 复现：async 体里 `Bag resp = await Trip();` 之后由**同步 callee**
  抛出，`resp` 指向的对象图（Bag + 两个 List）全部泄漏；输出仍然正确，所以只有
  `--check-leaks` 能看见。根因：`emit_async_eh_prologue` 的 `eh.land`、
  `emit_async_exc_epilogue` 的 `co.exc`、以及 `irgen_stmt.c` try 降级的 `catch_bb`
  三处都在落地时 `emit_async_reload_slots()`，把**堆帧**拷回栈 alloca。可是帧只在挂起点
  更新，`resp` 是最后一次 await **之后**赋的值，只存在 alloca 里 ⇒ 落地时被帧里的旧值
  （通常是 null）覆盖，之后 `emit_async_complete` 的释放遍历读到的就是被覆盖的槽。
  longjmp 只会落到**发起抛出的那次 invocation** 自己武装的 handler（挂起时
  `emit_async_eh_unarm` 会把已结束 invocation 的 handler 弹掉），而该 invocation 的
  state block 进入时已经 reload 过一次，所以 alloca 才是活的那份拷贝：**三处 reload 全部删掉**。
  这不只是泄漏——`catch` 里读最后一次挂起之后赋的值也会读回旧值（见新用例的
  `guarded bad=after-await/...`，修复前拿到的是 `start`）。
  证据：`tests/conformance/async_throw_across_frames.zan`（await 结果 + 同步 callee 抛出、
  try 跨挂起后在本帧 catch、两层 async 帧之间抛出、连抛三次不累积），
  conformance / determinism / leakcheck 三份全绿。

* **A8-15 ✅ 已修（2026-07-27）表达式里的 `await` 被当成 `int`，字符串/集合/对象结果被按整数使用**。
  `Console.WriteLine("b=" + await F())`（`F` 返回 `string`）打印的是指针值。根因：
  `anf_hoist_await` 把表达式中的 await 提升成临时局部时**硬编码 `int $awN`**
  （A8-9 只修了 `var x = await F()` 这一种直接形态）。修法：临时局部改为推导
  （`var_decl.type = NULL`，走 A8-9 已经打通的推导路径）。
  连带修正所有权：`expr_yields_owned_rc_value` 原本把 `$awN` 标识符当"单次使用的移动"
  （因为它以前是 `int`，不持有所有权）；现在临时局部有了正确类型、会在作用域退出时释放，
  再当移动就会**提前释放**（`return await this.MakeAsync();` 崩溃，0xC0000005 / 0xC0000374）。
  改成按普通局部处理：读它是借用、赋值方自己 retain。
  副作用：这条同时修掉了 SQL Server 驱动 leakcheck 里 4 个对象的泄漏（`DbResult` 的
  `List<DbRow>` / `List<string>`、`TdsMessage`）——那不是驱动的问题，是这个 `int` 临时局部。
  证据：`tests/conformance/async_await_expr_types.zan`（string / int / `List<string>` /
  class 的 await 参与拼接、成员访问、算术、索引；`return await f() + await g()`），
  全量 **588/588** 通过。

### A8-5 async 覆盖面探针矩阵〔已实测，`_scratch/async_probe/`〕

| 形态 | 结果 |
|---|---|
| `try` 体内 await | 通过（`r=2`） |
| `try` 体内 await + 外层 while | 通过（`r=3`） |
| 嵌套 try + 多个 await | 通过（`r=3`） |
| `finally` 内 await | 通过（`r=2`） |
| `switch` case 内 await | 通过（`r=6`） |
| 纯 while 循环内 await | 通过（`r=3`） |
| async 子任务 throw、被 awaiter 捕获 | 通过（`r=7`） |
| `catch` 内 await（无循环 / 有循环 / await 后再读 `e.Message`） | 修复后通过（A8-1） |
| `foreach` 内 await（嵌套、`break`、`continue`） | 修复后通过（A8-4 / A8-8） |
| `var` 局部跨 await（string / int / double / bool） | 修复后通过（A8-9） |

> 探针脚本 `a1`–`a17` 全部通过（2026-07-27 重跑）。已固化为三个 conformance 用例：
> `async_await_in_catch` / `async_await_in_foreach` / `async_var_locals_across_await`，
> 三类测试（conformance / determinism / leakcheck）各一份，**538/538 全绿**。
> 仍未做完、不要因为这些用例通过就认为已完成的：typed frame result（仍是 `i64`）、
> 取消（cancellation）路径、await 与 ARC 在异常退出时的所有权、`anf_stmt_contains_await`
> 的 try/switch 补漏（A8-6）。

**由此得出的判断**：async lowering **不是整体不成熟**——try/finally/switch/循环/跨协程
异常传播这些主干形态都是通的，说明状态机变换的骨架是对的。坏的是两个具体形态：
catch 块内的挂起、foreach 的降级与状态切分冲突。所以是**定点修复，不是推倒重做**。

### A8-6 已排除的猜测〔避免重复排查〕

* ~~"全局 setjmp handler 栈在挂起时不回退，越积越高"~~ —— **不成立**。
  `emit_async_eh_unarm`（`irgen_async.c:102-108`）在挂起/完成时把 `__zan_eh_top`
  恢复到本次 `$resume` 进入时的 `eh.co.entry`，注释也写明了理由（jmp_buf 记录的是栈帧，
  invocation 返回后就不可用）。设计上已经考虑过，别再往这个方向查。
* ~~"`anf_stmt_contains_await` 漏了 `AST_TRY_STMT`/`AST_SWITCH_STMT` 是崩溃根因"~~ ——
  **不是根因**。该函数（`irgen_async.c:344-369`）确实漏了这两个 kind，但它只用于
  `anf_normalize_body` 判断**非块的单语句体**是否需要包块；try/switch 体本身总是块，
  正常路径不受影响。仍是一处应当补齐的缺陷（形如 `if (c) switch (...) { ... await ... }`
  会漏规范化），但按**低危补漏**处理，不要当成 A8-1 的解释。

### A8-7 设计文档与实现的落差〔文档待修，归入 C 组〕

`docs/ASYNC_CPS_DESIGN.md` 的分阶段计划里，S4 才"broaden 到 await in if/loop"，
**try/catch 从头到尾没有出现**——即 await 与异常处理的交互从设计阶段就不在覆盖范围内。
实现后来补了协程 EH（trampoline + 帧内 hstack 重新 arm），但设计文档没同步。
另外 frame 布局写死 `i64 result` / "all scalars are i64 in this backend"，
**与 A0（int=32）直接冲突**，A0 落地时必须一并改。

---

# B. 标准库

## B1 大体量模块的改造（**都不搬走**）

* **B1-1** `stdlib/Sdk/Jd`（428 文件 / 519KB / 1.1 万行）从命名看是生成器直出
  （`Domain` 212 文件，还有 `Test774`、`GetThirdPdfByOrderIdForVender`、
  `UserRelatedRpcI18nService` 这类名字）。改造方向是**建立/恢复代码生成器让它可再生**，
  而不是手工维护 1.1 万行：从官方 API 描述生成 + 一层手写的公共层
  （HTTP / JSON / 签名认证 / 重试 / 错误映射 / 分页）。生成代码与手写封装分目录隔离。
  **判定**：删掉生成目录重跑生成器，`conf_sdk_jd` / `conf_sdk_jd_api` /
  `conf_sdk_jd_client` 三个用例依旧通过。
  另有两个空目录 `Sdk/jos-net-open-api-sdk-2.0`、`Sdk/Tb`，删掉。
  （注：`stdlib/Sdk/` 目前整个未纳入 git。）
* **B1-2** `stdlib/Game`（75 文件 / 788KB / 2.1 万行代码，**只有 142 行文档注释**）：
  `Zgm` 380KB、`Arpg` 275KB、`Rts`、`Scene`、`Cards`、`Board`、`Arcade2D`。
  文档覆盖率 0.7% 是全库最低——**先补公开接口文档再谈提炼**，
  没有接口文档就重构 2 万行是盲改。
  提炼候选：`Zgm/UiRuntime.zan` 65KB 与 `Arpg/UiRuntime.zan` 67KB 同名同量级，
  先查两者是不是在做同一件事（若是，合并到 `Game/Foundation`）。
  其余候选：公共游戏运行时、实体/组件模型、资源与项目模型、解析器、渲染层。
* **B1-3** `stdlib/Gui`（103 文件 / 1.9MB / 3.8 万行）是 UI 框架，不搬走，
  但内部模块边界需重划（见 B3-2 / B3-3）。

## B2 字节缓冲：收敛 "calloc 返回 string" 的用法 〔依赖 A1〕

〔已实测〕**这个惯用法是有意设计、有 conformance 覆盖的，不是内存安全 bug。**
`tests/conformance/ffi_free_consumes_string.zan` 专门测它：`calloc` 返回的 buffer
可以 `buf[0] = 65` 直接写，`free(buf)` 会消费它并把变量（含字段别名）置 null。
实测 `Path.Combine` / `GetFileName` / `GetExtension`，并对结果串做
`Substring` / `IndexOf` / 拼接 / 当 Dictionary key / 200 次循环 `--check-leaks`，
全部正确、`Length` 正确、无泄漏报告。**所以这是提炼问题不是安全问题。**

* **B2-1** `extern string calloc(...)` 出现在 **49 个文件**里，覆盖
  `Cryptography`（Aes / Sha1 / Sha256 / Sha512 / Md5 / Hmac / Rsa / Sm2 / Sm3 / Sm4 /
  Base64 / Hex / BigInt / Hkdf）、`Net`（TcpClient / Socket / UdpClient / WebSocket /
  TlsStream / Mqtt）、`Data`（几乎每个驱动）、`IO`（File / Directory / Path）。
  它能跑，但**全库把 `string` 当可变字节缓冲区用**是没有字节缓冲类型逼出来的；
  A1 的 `Span<byte>` / `byte[]` 到位后换掉。
* **B2-2** `stdlib/System/IO/Path.zan` 是纯字符串处理，却整个建在
  `strrchr` / `strcpy` / `strcat` / `calloc` 上（`GetFileName` 用
  `strlen(lastBslash) < strlen(lastSlash)` 比长度判断哪个分隔符靠后）。
  逻辑正确，但用 Zan 的 `IndexOf` / `LastIndexOf` / `Substring` 写会短很多、
  不需要手动管理缓冲区。同类：`Directory.zan`（拼路径）、`Process.zan`（拼命令行）。
* **B2-3** `strcat` / `strcpy` / `strrchr` / `strncpy` / `strlen` 换成 Zan string API；
  `atoi` / `atof`（`DbResult.zan:115`、`Windows.zan`）换成 Zan 实现的数值解析。
* **B2-4** `crt` 那 207 个 import 要分三类处理：真 libc 原语（`fopen`/`read`/`opendir`，
  **保留**）、被当 intrinsic 用的 `NativeMemory`（A1 后删）、
  本该用 Zan 写的字符串/数值函数（B2-3 删）。
* **判定**：`extern string calloc` 命中数显著下降；
  `--check-leaks` 下 Path / Directory / Process 用例无泄漏。

## B3 提炼：消除结构性重复

* **B3-0 ✅ 已完成（2026-07-27）驱动的失败语句改为抛 `DbException`**。
  此前服务端拒绝的语句返回一个空 `DbResult`、把原因留在 `GetError()` 里，
  调用方忘了检查就会拿着"从来不存在的行"继续跑。现在 Firebird / MySQL /
  Postgres / SQL Server / ZanDb 一律抛 `DbException`，带驱动自己的错误号和
  `DbProvider` id（handler 不必解析消息就能判断是哪个驱动），`GetError()`
  仍然报告最后一次失败。`DbProvider.Unknown` 表示"根本没到服务端"的失败。
  回归：`firebird_wire` / `sqlserver_tds` 已改为断言抛出。

* **B3-1** **7 个连接池**（`DbPool` / `FirebirdPool` / `MySqlPool` / `PgPool` /
  `SqlitePool` / `SqlServerPool` / `TDenginePool`，合计 39KB）逻辑完全一致：
  懒增长到 maxSize、`Gate` 事件驱动等待、死连接剔除。字段一模一样
  （`idle` / `liveCount` / `waiting` / `gate` / `closed`），连注释都复制了 7 遍
  （`// coroutines currently parked in AcquireAsync`）。
  `PgPool` 自己的文档注释写明了原因："Same contract as DbPool … **but typed**,
  so the coroutine query API stays reachable"——即 `IDbExecutor` 只声明同步
  `Query` / `Execute`，异步方法穿不过接口。
  **合并成 `DbPool<TConn>` + `IAsyncDbConnection`。前置是 A7-1**，
  因为它正需要"通过类型参数调用约束接口方法"这个当前会崩的能力。
* **B3-2** 23 个超过 150 行的方法，最长 **1536 行**
  （`Widget/DataTable.Render.zan:327`），其次 829 行（`CodeEditor.Render.zan:229`）、
  561 行（`FilePicker.zan:492`）、467 行（`Gui/Event.zan:991`）。
  按渲染阶段拆分并提取共享绘制原语。
* **B3-3** `Widget/DataTable` 一个控件 310KB（`DataTable.zan` 143KB +
  `DataTable.Render.zan` 167KB），`CodeEditor` 215KB 分散在 4 个文件。重划模块边界。
* **B3-4** 提炼 widget 公共绘制层：`c.FillRoundRect(x, y, w, h,
  t.borderRadiusMedium, t.bgPrimary)` 这类主题化绘制在各控件里重复出现，
  收敛成 `Theme` 上的具名原语。

## B4 平台与错误处理

* **B4-1** `stdlib/Platform`（Windows / Posix 各约 1KB）是占位级，
  而各模块自己在做 `#if WINDOWS` 分支。要么充实它，要么删掉别留半成品。
* **B4-2** 全库系统性短板：**失败静默返回空串/0，没有贯穿的异常或 Result 约定**。
  定一个统一的错误约定并落实。
* **B4-3** `System/Text` 无 Regex，但 `stdlib/RegularExpressions` 有 37KB/3 文件——
  核实是不是两套并存或挂在错误的命名空间下。

## B5 GUI 迁移（在 A 项能力就绪后穿插）

判定边界不是"碰不碰 OS"：`Win32Shell.zan` 已证明平坦 C ABI 的 user32/gdi32
能在 Zan 里重写（连 WndProc 都是 delegate）。真正的阻塞是 union / 宏 API /
header-only 库 / C 回调 / 结构体字段偏移，全部对应 A2 / A3 / A4。

* **B5-1** 〔A0+A1 后〕光栅器搬 Zan：`gui_runtime.c` 的 19 个导出
  （surface / clip / 全部图元 / blur / snapshot）。**关键一刀是 surface 所有权**——
  改由 Zan 持有后，present 签名从 `(hwnd, surface_id)` 改为 `(ptr, w, h, stride)`，
  `internal_surface_data/clip` 两个内部口子随之删除，其余全是跟随项。
  动手前先给 `blur_rect` / `fill_radial` / `blit_image` 做基准，
  它们是玻璃主题下单帧最贵的循环。
* **B5-2** 删 `gui_runtime_shims.c` 全部桩（160 行，**零真运行时**，
  存在理由只是"让链接过得去"；24 个 WebView 桩在 Windows 上已无调用方）。
  Zan 侧 `#if` 兜底。其中 `write_file` 实际调 `fopen`/`fwrite`/`fclose`，
  归入系统 IO，不要留在 GUI shim 里。
* **B5-3** 搬 `gui_runtime_font.c` 的 `draw_icon`（约 300 行纯矢量）+ 位图字体。
* **B5-4** 删 `gui_runtime_text.c:325-1360` 的 Win32 窗口壳——
  未提交的 `Native.zan` 已把 `#if WINDOWS` 全线切到 `Win32Shell.zan`，
  **这段在 Windows 上已是死代码**。Windows 只剩 `set_clipboard` / `get_clipboard` /
  `set_ime_pos` / `write_file` 4 个走 C，补进 `Win32Shell.zan` 即清零。
* **B5-5** 〔A2 后〕`X11Shell.zan`：先搬**错放在 `gui_runtime_font.c:769-1058`**
  的那 15 个 X11 窗口管理导出（最容易，顺手修掉"窗口管理住在字体文件里"的结构债）。
  注意 `DefaultScreen` / `DefaultVisual` / `DefaultDepth` / `RootWindow` 是宏，
  用函数版 `XDefaultScreen` / `XDefaultVisual` / `XDefaultDepth` / `XRootWindow` 代替，
  这一条不构成阻塞。
* **B5-6** 〔A2+A3 后〕SDL 后端（window registry / event queue / dirty rect /
  texture 上传）改由 Zan 组织，C facade 退回纯绑定；Cocoa 后端
  （`objc_allocateClassPair` + `class_addMethod` 造类）。
* **B5-7** 终态：删除 `zan_gui`(103 处 import) 和 `zan_sdl3`(78 处) 两个自建垫片。
  `user32`(81) / `kernel32`(73) / `gdi32`(38) / `odbc`(24) / `ws2_32` / `imm32` /
  `dwmapi` / `psapi` 这些直接绑 OS 的**是正确形态，不动**。

## B6 工具链 Zan 化 〔依赖 A3 + A6〕

`src/` 下非 compiler 的 C，**工具链一共 265KB，比 GUI runtime 还多**：
`lsp/intellisense.c` 82KB、`lsp/lsp_main.c` 65KB、`dap/debugger.c` 40KB、
`dap/dap_main.c` 27KB、`common/json.c` 15KB、`doc/zandoc.c` 14KB、
`pkg/zanpkg_main.c` 12KB、`fmt/zanfmt.c` 7KB、`common/rpc.c` 3KB。
按规则这些既不是 compiler 也不是 runtime，全都该是 Zan。

## B7 runtime 边界复核

`rt_io.c` 67KB、`rt_sync.c` 49KB、`rt_sched.c` / `rt_mem.c` / `rt_co.c` /
`rt_crash.h` / `rt_wasm.c` 26KB。真 reactor / 调度 / 同步原语 / 内存分配留 C 没问题，
但要逐个过一遍：如果混了 HTTP 解析、编码转换、路径处理这类纯逻辑，上移到 Zan。

---

# C. 文档

`docs/` 40 份。全部在最近 3 周内动过，所以问题**不是日历陈旧，是内容与实现漂移**。

* **C1** `ABI.md`（最后更新 07-05，最旧一批）已确认多处与实现不符：
  * `:38` 称 `float` 是 8 字节 double —— 实际 `TYPE_FLOAT` → 32 位 `LLVMFloat`；
  * `:67` 承诺 `[repr("C")]` 保证 C 布局 —— 实现里没有任何布局控制代码；
  * `:249` marshalling 表写 `string`（返回值）= "Copy to managed string"
    ——〔已实测〕**不是拷贝**（若是拷贝，`calloc` 的零缓冲区会变成空串，
    而 `ffi_free_consumes_string.zan` 和 `Path.zan` 都能正常工作）；
  * 同一张表列了语言中**不存在**的 `int32` / `float32` 类型；
  * `:251` / `:254-260` 的结构体按值传参表、`:24` 的 sret 描述，
    都描述了 `irgen_emit.c` 里**不存在**的行为。
  → 全文按实现重写，并明确分层"当前实现 / 目标设计"。**A2 完成后目标态才会变成真的**。
* **C2** 07-05 后再没动过的一批（`ABI.md` / `ARCHITECTURE.md` /
  `CODING_STANDARDS.md` / `DESIGN.md` / `ERROR_CATALOG.md` / `IDE.md` /
  `SECURITY.md`）逐份核实。这批是初稿之后从未回头的，漂移风险最高。
* **C3** `STDLIB_ANALYSIS.md` 顶上贴了"本文档大部分结论已过时，仅作历史参考"的
  横幅，正文却原样保留。**要么按现状重写，要么移进 `docs/archive/`**——
  不要用横幅代替维护。
* **C4** ✅ 已删（2026-07-27）`STDLIB_DB_AARDIO_REF.md` 那 2 行只是一句
  "已被 `STDLIB_ZAN_DESIGN.md` 取代"，接替者本身已在开头写明这段历史，残桩删掉。
* **C5** 3 份 bug 文档未纳入 git（`await-in-catch-loop.md`、
  `dict-urldecode-key-corruption.md`、`throw-leaks-live-locals.md`），
  连同各自的 `repro_*.zan` 一起提交（配合 A8）。
* **C6** `docs/bugs/*.md` 需要统一的状态字段。`generics-uniform-repr.md` 标了
  `Status: FIXED`，其余几份没有——读者无法判断哪些还有效。
  且这份 FIXED 需要按 A7 的实测结果更新：5/6 条确实已修，
  但要补记"通过类型参数调用约束接口方法仍崩"。
* **C7** ✅ 已归位（2026-07-27）两份 ra2-hd 文档移到项目文档层：
  `docs/projects/ra2-hd/2026-07-26-ra2-hd-plan.md` 和
  `.../2026-07-26-ra2-hd-design.md`（`git mv` 保留历史，计划里指向 spec 的
  链接同步改了），空掉的 `docs/superpowers/` 删除。语言规范层只剩
  `SPEC.md` / `ABI.md` 这类。
* **C8** 路线类文档有 4 份且边界不清：`ROADMAP.md` 383 行、
  `EXECUTION_PLAN.md` 43 行、`PRODUCTION_PLAN.md` 48 行、
  `SELF_CONTAINED_TOOLCHAIN.md` 68 行。合并成一份，并把本清单并进去。
* **C9** 文档覆盖率差距悬殊：`System` 16.5%、`Gui` 9.8%、`Game` **0.7%**。
  既然 Game 不搬走，就得把文档补上（随 B1-2）。
* **C10** 文档内容一律**以实现和 C# 为准**，不以现有 doc 为准。
  已知不合理的描述直接按实现重写或删除，不反过来让实现去迁就文档。
  同时反向检查：文档里偏离 C# 习惯的设计，优先按 C# 靠拢。
* **C11** 建立维护规则：每份文档按"当前实现 / 目标设计 / 历史记录"分层归档，
  目标设计类文档必须写明依赖哪个能力项（A 编号），能力落地时同步更新。

---

# 建议执行顺序

| 批次 | 内容 | 为什么排这里 |
|---|---|---|
| **0** | ~~A8-1~~ ✅ / ~~A8-2~~ ✅ / ~~A8-3~~ ✅ / ~~A8-4~~ ✅ / ~~A8-12~~ ✅ / ~~A8-13~~ ✅ / ~~A8-14~~ ✅ / ~~A8-15~~ ✅（catch 内再抛跳错 handler、抛出点双重释放、被放弃 handler 泄漏异常、异常落地用帧覆盖栈槽、表达式里的 await 被当 `int`；+ 顺带修掉 A8-8 foreach break/continue、A8-9 `var`+await 类型、A8-10 测试重复编译） | **三份 repro 全部实测仍成立，两个比文档记的更严重**。ARC 与异常/协程交互不可靠 ⇒ 任何 Zan 上层代码都不可信，而"上层全用 Zan"是整个计划的地基 |
| **0.5** | ~~**A7-1**~~ ✅（修约束接口方法调用的 codegen 崩溃）/ ~~A7-2~~ ✅（构造类型的静态成员访问）/ ~~A7-4~~ ✅（无花括号单语句体）；~~**A7-5**~~ ✅（泛型字段在 `T` 为引用类型时取值错误/堆损坏，四个缺陷） | 已全部实测修复；A7-5 曾阻塞 B3-1，现已解除 |
| **1** | ~~**A0-0**（句柄型 extern 改 `nint`）~~ ✅ 已完成（330 处 / 26 文件，553/553）；**A0-0c**（socket 句柄统一 `nint`）已完成（562/562，窄化警告 51 → 0）、**A0-0d** 句柄部分已完成（`rt_io` → `intptr_t`，`zan_gui_*` 句柄 → `iptr`；位宽部分并入 A0-1/A0-2）；余下 C4 / C5 / C7（删残桩、提交游离文档、游戏计划归到项目文档层） | A0-0 在 int 还是 64 位时零语义变化，是切 int=32 的安全前置。清理放这里而不是更早：`ABI.md` 这类失真文档在 A2 落地前仍是唯一的目标 ABI 参照，**先实现、后按实现重写文档**，不要先清场 |
| **2** | **A0-1 / A0-2 / A0-3**（int=32 + FFI 位宽 + 结构体布局） | 一切 FFI 和编解码工作的前置 |
| **3** | **A1** `Span<T>` + **B2**（用它清掉 49 处 calloc-as-string） | 能力与清理配对落地 |
| **4** | B5-1 ~ B5-4（光栅器、shims、draw_icon、Win32 死代码） | A0+A1 就绪即可做，GUI 侧收益最大 |
| **5** | **A2** FFI ABI → B5-5 / B5-6（X11 / SDL / Cocoa） | 大工程，前面跑通再上 |
| **6** | **A3** bindgen + **A6** 编译器 API → **B6** 工具链 Zan 化 | 265KB C 的出口 |
| **7** | A4 / A5、B1-1 Sdk 生成器、B1-2 Game 文档与提炼、B3 提炼、B4 错误约定、B7 runtime 复核、C1/C2/C3/C6/C8/C9/C10/C11 文档重写 | 收尾 |

**节奏**：每补完一项能力，立刻用它改掉对应的那批 stdlib，拿真实场景验收，
再进下一阶段。不要攒——否则能力做完了才发现设计不趁手。

**风险提示**：A0 / A1 / A2 动的是 irgen 核心，改错了是**静默的错误结果**
而不是编译失败。现有 conformance 测试 190 个，开工前先确认对数值运算和 FFI
的覆盖够不够，不够先补测试。`generic_constraints.zan` 就是覆盖不足给出假信心的实例。

---

# A15 语言缺口审计（2026-07-27，实测，全部有探针）

**状态：A15-1 / A15-2 / A15-3 已修并推送（`1dd023d`），新增
`byte_unsigned`、`array_init_foreach`、`struct_operators` 三个 conformance。
A15-7（foreach 只能迭代 List）是修 A15-2 时实测发现的，一并修掉。
A15-8（数组没有长度，字段/参数上 `.Length` 和 foreach 都不可用）已修
（`a44a807`），A15-9（irgen 用 C 硬编码 43 个静态库调用，屏蔽 Zan 实现）
已按 Path/File/Directory 处理（`b0f74ac`、`1cd90dd`）。
A15-4 / A15-5 / A15-6 未做。**

标准库里那些"看着绕"的封装，多数不是风格问题，是被下面这些缺口逼出来的。
每条都用最小程序实测过，不是读代码推断的。

## A15-1 `byte` 是有符号的（**根因级**）— 已修 `1dd023d`

```zan
byte b = 255;   Console.WriteLine(b);    // -1，C# 是 255
byte[] a = new byte[2]; a[0] = 200;
Console.WriteLine((int)a[0]);            // -56，C# 是 200
```

`byte` 按 i8 存、按有符号读，取值范围实际是 −128..127。`ushort`/`uint` 正常，
只有 8 位的坏了。后果直接可量：**整个 stdlib 里 `byte[]` 出现 0 次**，
而 53 个文件自己拿 `string` / `NativeMemory` 手搓字节缓冲（B2 记的 49 处
calloc-as-string 就是这个）。修好 `byte` 的零扩展语义，B2 才有意义——否则
`Span<byte>` 做出来也没人敢用。

## A15-2 数组初始化器产出的数组是坏的 — 已修 `1dd023d`

```zan
int[] b = new int[] { 1, 2, 3 };
Console.WriteLine(b.Length);   // 访问违例退出（0xC0000005）
```

`new int[3]` + 逐个赋值正常，只有带初始化器的形式坏。`List<T>` 的集合
初始化器（`new List<string> { "a", "b" }`）是好的。

## A15-3 值类型缺三样：`==`、运算符重载、`const` — 已修 `1dd023d`

```zan
p == q                          // LLVM: Invalid operand types for ICmp（无行号）
static V operator +(V l, V r)   // struct 上：Integer arithmetic operators only
                                // work with integral types；同样的代码在 class 上正常
const int K = 7;                // 解析错误 "expected type"；static readonly 可用
```

`Color` 这类值类型直接受影响：现在只能 `IsNone()`，写不了
`if (bg == Color.None())`。运算符重载在 class 上可用、在 struct 上产出非法
IR，是 irgen 的分支缺失。

## A15-4 集合元素槽固定 8 字节

`List<T>` / `Dictionary` 的元素槽是裸 `i64`，所以 struct 元素要打包/解包
（`33a66df`），超过 8 字节的只能报错。真正的修法是按元素大小/stride 存，
和 A1 `Span<T>` 一起做。

## A15-5 `switch` 全库 0 处使用

`switch (string)` 实测可用，但 stdlib 里 `switch` 出现 **0 次**，单是 Gui
就有 871 处 `if (x == "...")` 链（`StyleSheet.DeclX` 的属性分派最典型）。
这是历史惯性不是能力缺口，属于可读性/性能债，随颜色迁移一起清。

## A15-7 foreach 只能迭代 `List<T>` — 已修 `1dd023d`

修 A15-2 时实测发现，比 A15-2 影响更大：

```zan
foreach (int v in intArray)  // 访问违例
foreach (char c in "ab")     // 访问违例
foreach (T v in list)        // 正常
foreach (string k in d.Keys) // 正常
```

foreach 把任何集合都按 List 布局读（count 取字段 0、data 取字段 2），数组和
字符串都不是这个布局，于是从无关内存里读出计数和数据指针，第一个元素就崩。
**这正是标准库大量写 `while (i < x.Length)` 而不是 `foreach` 的原因**，
不是风格偏好。现在数组和字符串各按自己的布局迭代；长度在循环处不可知的
数组（字段、参数）给出诊断而不是崩溃——真正的修法是给数组加长度头，
与 A1 `Span<T>` 一起做。

## A15-8 数组不带长度 — 已修 `a44a807`

数组曾是裸 calloc 缓冲，长度存在声明处旁边的一个 alloca 里，于是 `.Length`
只在局部变量上可用：数组**字段和参数**根本没有长度，foreach 它们是编译错误。
**这就是 stdlib 到处 `while (i < n)` 并额外传一个 count 参数的原因。**
现在数组在缓冲区前面多分配 16 字节存元素个数，程序拿到的指针仍指向第一个
元素，所以传给 C 的行为不变；`.Length` 对任意数组表达式读这个头，foreach
用它做上界。

## A15-9 irgen 用 C 硬编码库调用，屏蔽 Zan 实现 — Path/File/Directory 已修

irgen 按"类名 + 方法名"直接 lower 了 **43 个**静态库调用
（`Path` 7、`File` 8、`Directory` 6、`Math` 8、`Console` 8、`Environment` 3、
`Convert`/`String`/`Task` 各 1），于是同名的 Zan 实现根本不可达——
`stdlib/System/IO/Path.zan` 整个是死代码，改它没有任何效果，而 C 版的 bug
（`GetDirectoryName("c.txt")` 返回整个路径、`ChangeExtension` 无扩展名时崩溃）
在 Zan 里修不掉。这是"用 Zan 替代 C"这条主线上最直接的一处倒挂。

现在这些位置在程序确实定义了该方法时让位给真实定义，没有 stdlib 时仍走内建
兜底。`Path`（已用纯 Zan 重写，去掉 6 个 crt import）、`File`、`Directory`
共 21 处已处理；`Math` / `Console` / `Environment` / `Convert` 在 stdlib 里
没有对应的 Zan 类，要先写出来才能同样处理。

## A15-6 其它已确认的小差异

* `char` 打印成数字（`'A'` → `65`），C# 打印字符。
* `ulong` 字面量超过 i64 上限会被夹到 `9223372036854775807`。
* struct 与整数不兼容此前只有无行号的 LLVM 校验失败——已修（`94cf827`）。
* ~~数组是裸缓冲区，长度只在声明处记住~~ 已修，见 A15-8。

## 建议顺序

1. ~~A15-1 `byte` 零扩展 + A15-2 数组初始化器~~ 已完成（`1dd023d`），
   顺带修掉 A15-7 foreach 和数组元素赋值的宽度截断；
2. ~~A15-3 struct `==` / 运算符重载 / `const`~~ 已完成（`1dd023d`）；
3. 用修好的语言能力改正封装：`byte[]` 取代 string/NativeMemory 字节缓冲
   （B2 的 49 处）、`foreach` 取代下标 while 循环；
4. A15-4 元素槽 + 数组长度头（与 A1 `Span<T>` 合并）；
5. A15-5 用 `switch` 重写属性分派（与颜色迁移同批）。

---

# A16 CSS 支持面（现状实测）

**选择器**：类型名、`.class`、`#id`，各自可带 `:state` 后缀
（hover / active / focus / disabled），逗号选择器列表。
**没有**后代/子/兄弟组合器、属性选择器、`*`、伪元素，也没有真正的层叠优先级
——`Apply` 按 类型 → `.class` → `#id` → 带状态 的固定顺序依次覆盖。

**at-rule**：只有 `:root` 自定义属性 + `var(--x)`。没有 `@media`、`@import`、
`@font-face`；`@keyframes` 不解析，`animation: <name> ...` 只能引用内置的
8 条曲线（spin / pulse / breath / shimmer / float / glow / aurora / motes）。

**属性**（约 90 个）：盒模型（margin/padding/border 及四边分解/radius/width/
height/min/max）、背景（color/image/linear-gradient 2~3 停靠点）、
文本（color/font/font-size/font-weight/text-align/text-transform/
text-overflow/letter-spacing/line-height/white-space）、
flex 子集（display/flex-direction/justify-content/align-items/flex-wrap/
flex-grow/gap/order）、定位（position/dock/top/left/bottom/right/z-index）、
效果（box-shadow 含 inset/opacity/filter/backdrop-filter/transform:
translate|translateX|translateY|scale|rotate/transition 三件套）、
overflow/visibility/cursor/accent-color。

**取值**：颜色齐全（`#rgb`/`#rgba`/`#rrggbb`/`#aarrggbb`/`rgb()`/`rgba()`/
`hsl()`/`hsla()`/`0x`/十进制/约 60 个具名色/`transparent`）。长度**只有像素**：
`Num()` 遇到第一个非数字就停，所以 `12px` = 12、`1.5rem` = 1、`50%` 在多数属性
上被当成 50；比例值走 `Perm()`（千分数），因此小数只在 opacity/scale/alpha
这些地方有效。没有 `calc()`、没有 em/rem/vh/vw。

---

# A17 测试套件"要跑半小时"的真正原因（2026-07-28，已实测已修）

不是缓存也不是编译慢：单个用例编译 0.03–0.4 秒（`build\zanc.exe
tests\conformance\http_server_stress.zan --auto-stdlib` = 0.41 秒）。两条原因：

* **A17-1** ✅ 已修。`conformance` / `determinism` / `leakcheck` 这三批（每批约 200 个，
  由 `tests/conformance/*.zan` glob 生成）**都没设 TIMEOUT**，ctest 的默认值是 1500 秒，
  所以任何一个挂死的用例独占 25 分钟。现在三批都是 `TIMEOUT 120`
  （`CMakeLists.txt` 的三个 `foreach`），`selfhost_*`（600 / 1200）和
  `runtime_gui_*`（15 / 20）保持各自原值。
  〔已实测〕`build/CTestTestfile.cmake` 里每条都带 `TIMEOUT "120"`。
* **A17-2** ✅ 已修。挂死是 runtime 的真 bug，**只发生在 POSIX 后端**
  （epoll / kqueue / select；Windows 的 IOCP 会把被取消的 AcceptEx 以错误完成，
  所以在 Windows 上看不到）。`server.Stop()` 关掉监听 socket 后，仍挂在 accept 上的
  watcher 留在 io 表里：
  - epoll：关 fd 会**静默地**把它移出 epoll 集，`epoll_wait` 永远不再报告它，
    协程永久停摆；
  - select 回退：把已成 `-1` 的 fd 塞进 `FD_SET` 是未定义行为（`-1` 会写到
    `fd_set` 存储之外），实测表现为 select 去等 fd 0（stdin），永不返回。

  修法（`src/runtime/rt_io.c`）：新增"死 fd watcher"队列。注册时和轮询前
  用 `zan_io_socket_alive`（POSIX 一次 `fcntl(F_GETFD)`，Windows `getsockopt(SO_TYPE)`）
  校验 fd；无效的 watcher 不进 `fd_set`/epoll，而是立刻以**失败**唤醒
  （accept 出参 `-1`、recv 字节数 `0`）。三个后端都覆盖：epoll 在无事件返回和
  阻塞前扫 slot 表，kqueue / select 同样扫 entry 链。
  配套 stdlib：`Socket.AcceptAsync` / `AsyncSocket.Accept` 的 POSIX 重试循环
  原先是 `while(true) { await ReadReady; if (accept >= 0) return; }`，
  只修 runtime 会把"永久挂死"变成"忙等"，所以循环里补一条
  `if (!Socket.IsOpen(sock)) return -1;`（`Socket.IsOpen` = 新的
  `zan_io_socket_alive` 导入）。`HttpServer.Start` 的 `while (this.running)`
  于是能正常退出。
  〔已实测〕探针 `_scratch/io_dead_fd_probe.c`（直接驱动 rt_io，Linux）：
  HEAD 基线上 epoll 与 select 两个后端都 `timeout 15` 超时（退出码 124）；
  修后两个后端都是 `closed-listener: woke=1 accepted=-1` /
  `invalid-fd recv: woke=1 n=0` /  `live accept: woke=1 accepted=ok`。
  新增用例 `tests/conformance/accept_after_close.zan`（监听 socket 在 accept
  挂起期间被 `Stop()` 关掉，期望输出 `woke` / `failed`）。
  Windows 全量子集 `ctest -R 'accept_after_close|http|async|socket|tcp|udp|websocket|sse|net'`
  = **102/102 通过，8.5 秒**（含 `conformance_http_server_stress` 1.16 秒）。
  POSIX 侧由 CI 的 `build-unix` 全量 ctest 覆盖。

---

# A18 C# 裸 `throw;`（重抛）（2026-07-28，已实测已修）

A8-13 结尾遗留的最后一条：`throw;` 此前解析器直接拒绝，只能写 `throw e;`——
而那**不是**重抛：它会把 tid 换成静态类型、并把异常当成一次新的抛出，
外层按派生类型写的 `catch` 就会漏掉。

实现（只动了 5 个文件）：

1. `parser.c`：`throw` 后紧跟 `;` 时 `throw_stmt.value = NULL`（其余各遍历器
   对表达式本来就是 null-safe，逐个核对过）。
2. **每个 handler 多存一个 tid 槽**（`irgen.h` 的 `catch_cleanups.tid_slot`）。
   同步体是 entry alloca；async 体新增帧字段 `ASYNC_FRAME_CEXC_TID`
   （`[ASYNC_MAX_HANDLERS x i8*]`，`irgen_expr_core.c` 枚举 + `irgen_emit.c` 布局），
   与 A8-1 的 `CEXC` / `CEXC_OWNED` 同样按 try 的编译期 handler id 索引。
   **必须帧驻留**：catch 里 `await` 之后、或 catch 里嵌套 try 抛过一次之后，
   全局 `__zan_eh_exc_tid` 已被覆盖，只有 handler 自己存的那份还是原始动态类型。
3. `irgen_stmt.c` 的 `AST_THROW_STMT`：`value == NULL` 时把 handler 的
   异常指针 / owned 标志 / tid 原样写回在飞全局，然后走与普通 throw 完全相同的
   展开 + longjmp 尾段（`goto throw_unwind`）。所有权：handler 不再持有那个 +1，
   所以 `__zan_eh_tmp_drop` 注销它在异常临时栈上的那条、并把 `owned_slot` 清零，
   否则 `emit_release_active_catch_excs` 和临时栈展开会在外层 handler 手里把它释放掉。
4. `throw;` 不在 catch 内（包括 catch 里嵌套的 lambda 体——按 handler 槽所属函数判定）
   给出明确编译错误 `a rethrow (\`throw;\`) is only valid inside a catch block`，
   对应 C# CS0156，而不是生成垃圾 IR。

实测（Windows，`build/zanc.exe`）：

* 新增 `tests/conformance/exception_rethrow.zan`：基类 clause 重抛后**外层派生类
  clause 命中**（不是基类 clause）、重抛时持有托管局部、同帧嵌套 try 内层重抛、
  循环内连续重抛 3 次、async 体 catch 重抛、**catch 里先 `await` 再嵌套抛一次
  scratch 异常然后 `throw;`**（专门盯 tid 被覆盖那条）。9 行输出逐行正确，
  `--check-leaks` 干净。
* `throw;` 写在 try 体里 → 上述编译错误，exit=1。
* 子集 `ctest -R 'exception|throw|catch|async|try|await|generic|leakcheck' -j8`
  = **276/276 通过，45.62 秒**（含 conformance / determinism / leakcheck 三份
  `exception_rethrow`）。按 AGENTS 规则 8 没跑全量。

仍缺（不要当已完成）：`finally` 里的隐式重抛路径没有单独构造用例；
`throw;` 与 `finally` 组合、以及跨协程边界重抛（子任务 catch 后 `throw;`
再由 awaiter 捕获）只被上面的 async 用例部分覆盖。

---

# A19 展开栈改成动态增长（2026-07-28，已实测已修）

A8-12 遗留的边界：`__zan_eh_tmps` 是**固定长度全局数组**（256 → 4096），
`__zan_eh_tmp_push` 越界就静默丢弃条目。栈深度是「递归深度 × 每帧持有型局部数」，
编译期根本不知道上界，所以固定容量必然能被真实程序压爆，压爆的后果是
longjmp 跳过那些帧时它们的对象**全部泄漏**。

修法（`irgen_builtins.c`）：改成堆缓冲 + 按需翻倍——
`i8** __zan_eh_tmps` / `i32 __zan_eh_tmps_top` / `i32 __zan_eh_tmps_cap`，
新增 `__zan_eh_tmp_grow()`（首次 256，之后 ×2，走 `realloc`）。
`push` 在 `top >= cap` 时先 grow 再存；`pop` / `unwind` / `drop` 的边界检查
从编译期常量改成读 `cap`。只有 **realloc 失败**才退回「丢条目」，即 OOM 下泄漏而不是崩。

实测（Windows）：新增 `tests/conformance/exception_unwind_stack_growth.zan`
（1500 层递归 × 每帧 3 个 `List<string>` = 4500 条，底层抛出、顶层捕获，
之后再做一次浅层抛出验证栈已回到 handler 深度）。
* 把 grow 临时改成「cap 到 4096 就不再增长」以复现旧行为：
  `zan: memory leak detected: 542 object(s) still reachable at exit`
  （三处 `List<string>` 各 135/136 条）。
* 动态增长后同一用例 `--check-leaks` **干净**，输出 `caught bottom` /
  `caught again bottom` / `done`。
* 子集 `ctest -R 'exception|throw|catch|async|try|await|leakcheck|generic' -j8`
  = **280/280 通过，46.44 秒**。

仍未完成（同 A8-12 尾部）：async 体局部仍未登记到展开栈；终局仍应换成
LLVM 真 EH（invoke/landingpad + personality），届时这套 push/pop 补偿整体删除。

> 后续修正（A20）：这里的 `realloc` 增长形态在多线程调度器下是 use-after-free，
> 已整体换成「固定 chunk 表 + 按需 malloc chunk」，`__zan_eh_tmps_cap` /
> `__zan_eh_tmp_grow` 已删除。上面的泄漏复现与用例仍然有效。

---

# A20 handler 栈去掉固定 16 层上限，两个 EH 栈都改成不搬移的 chunk 存储（2026-07-28，已实测已修）

`__zan_eh_bufs` 此前是 `[16 x [1024 x i8]]` 的静态全局，且 **没有任何边界检查**。
handler 深度是**运行时**嵌套数（当前已进入的 try 数），不是源码嵌套层数——
一个带 try/catch 的递归函数递归到第 17 层就写到数组之外。后果不是泄漏而是
内存破坏 / 语义错：`throw` 会 longjmp 到某个更外层的 handler，本该接住它的
catch 被跳过。

为什么不能照 A19 那样 `realloc` 增长：**EH 状态是进程级全局，而调度器是多线程的**
（`zan_co_sched_run` 起 N 个 worker 线程各自 step 协程）。实测证据：先按
「两条并行数组各自 realloc」改完，`leakcheck_sqlserver_tds` 用新编译器跑 8 次崩 3 次，
gdb 栈顶正是 `__zan_eh_tmp_grow` 里的 `realloc` → `RtlFreeHeap`（另一个线程仍持有旧缓冲），
旧编译器 8/8 通过。另外 armed 的 `jmp_buf` 本身**永远不能被搬走**。
顺带排除了 `thread_local`：把这些全局标成 TLS 后**每个**生成程序在第一次 TLS 访问
（`_tls_index` + `gs:0x58`）就崩，说明 zanc 自带的 ld.lld + mingw runtime 这条链上
生成代码的原生 TLS 目前不可用，需单独修（见下）。

修法（`irgen_builtins.c`，两个栈共用）：`get_eh_chunk_fn` 生成
`i8* __zan_eh_slot(i32)` / `i8* __zan_eh_tmp_slot(i32)`——固定的
`[1024 x i8*]` chunk 表 + 首次使用时 `calloc` 一个 chunk，用
**cmpxchg 发布**（抢输的线程 free 掉自己那块从未公开的内存，改用赢家的）。
已发布的 chunk 永不 free / 永不搬移，所以发出去的地址（含 armed 的 jmp_buf）
终生有效，别的 worker 线程并发读也安全。handler 槽 1040 字节 = 1024 的 jmp_buf +
展开栈 mark（16 字节对齐），每 chunk 64 槽；展开栈每 chunk 4096 条。
上限从 16 变成 65536 个活 handler / 4M 条展开项，超过或 chunk 分配失败是
**明确 abort**，不再静默丢弃。删掉了 `__zan_eh_marks` / `__zan_eh_cap` /
`__zan_eh_tmps_cap` / `__zan_eh_tmp_grow` / `__zan_eh_slots_ensure`。

实测（Windows）：新增 `tests/conformance/exception_handler_depth.zan`——
`Relay(40)` 递归 40 层、每层各有自己的 try/catch（第 20 层换抛新异常，其余用裸 `throw;`
逐层重抛）+ 20 层静态嵌套 try 的 `Nested()` + 浅层抛出验证栈已复原 + 再跑一次深层
验证 chunk 复用。
* 旧的固定 16 层实现：同用例 access violation（第 17 个 handler 写越界）。
* chunk 版：输出 `caught relayed at 20` / `nested 20` / `caught shallow` /
  `caught relayed at 20` / `done` 全对，`--check-leaks` 干净。
* 子集 `ctest -R 'exception|throw|catch|async|try|await|leakcheck|generic' -j8`
  = **283/284，30.24 秒**；唯一失败 `leakcheck_sdk_wechat_product_modules` 是
  另一个会话正在改的 Wechat stdlib（`WechatApiTransport_RequestRawAsync` 实参个数不匹配），
  与 EH 无关。改之前失败的 `leakcheck_sqlserver_tds` /
  `leakcheck_http_client_timeout` 现在连跑 6 轮 100% 通过。

仍未完成（**不要当已解决**）：
* **多线程共享 EH 状态的语义问题**：`__zan_eh_top` / `__zan_eh_exc` /
  `__zan_eh_tmps_top` 仍是进程级全局，A 线程 throw 理论上能 longjmp 到 B 线程的
  handler。chunk 存储只消除了内存安全回归，没有解决「每个协程应有独立 handler 栈」。
  出路：协程/帧持有 EH 状态，或修好生成代码的 TLS。
* async 帧的 catch 槽仍是固定 `ASYNC_MAX_HANDLERS`（8）。
* `try { throw ... } finally { ... }`（无 catch）**吞异常**：finally 只跑最内层一层，
  函数直接返回 0，异常没有继续往外传（C# 语义是 finally 跑完继续传播）。独立 bug，单独修。

---

# 已撤回的结论（早期草稿中的错误，勿再引用）

1. ~~"无符号/窄类型只是语法别名，IR 层全塌成 i64，语义是假的"~~ ——
   只看了 `map_type` 的存储类型就倒推，**错**。实测运算语义正确（见 A0）。
2. ~~"`Path.zan` 的 calloc-as-string 是堆越界写 + 永久泄漏的内存安全 bug"~~ ——
   **错**。这是有 conformance 覆盖的有意设计，实测无越界无泄漏（见 B2）。
3. ~~"`bool` 映射成 i1 导致结构体布局错误"~~ —— **错**。
   i1 在 LLVM 结构体里占 1 字节，与 C `_Bool` 一致。真正的布局问题是 `int` 占 8 字节。
4. ~~"引入 `i8/i16/i32/u32` 显式位宽命名，删掉 `uint`/`ulong`/`ushort`/`sbyte`"~~ ——
   与"尽可能保持 C# 语法"冲突，**作废**。保留 C# 名字，改宽度语义。
5. ~~"把 `Sdk` 和 `Game` 移出仓库，真正的标准库只有 `System`"~~ ——
   **作废**。两者都是项目要用的，问题在封装质量不在位置；改为分层 + 改造封装。
6. ~~"macOS / stb_image / rt_crash 永远搬不动"~~ —— 表述不当。
   它们各自对应明确的能力缺口（A2 结构体 ABI 与变参 / A3 bindgen 链接预编译库 /
   A4 `[NoRuntime]`），是 backlog 不是永久限制。
7. ~~"async/await 整体是过渡实现，需要按成熟语言的状态机重做"~~ —— **过度判断**。
   探针矩阵（A8-5）显示 try / finally / switch / while / 嵌套 try / 跨协程异常传播
   全部通过，状态机骨架是对的。坏的是 catch 内挂起、foreach 降级两个具体形态，
   按定点修复处理。
8. ~~"协程 EH 的全局 setjmp 栈在挂起时不回退、越积越高"~~ —— **不成立**，见 A8-6。

> **已核实为正确、不需再查的项**（避免重复排查）：
> `unsigned_types` 的全部数值语义；`Path.zan` 的 calloc-as-string 惯用法；
> 泛型第 5 条（Dictionary 装泛型类值）与第 6 条（`foreach Dictionary.Keys`）；
> async 的 try 体 / finally / switch / while / 嵌套 try / 跨协程异常传播。
