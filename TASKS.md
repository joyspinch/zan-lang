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
* **A0-1 ✅ 已完成（2026-07-28，commit `3a2bc6d`）** `int` → i32、`long` → i64。C 的 `int` 也是 32 位，**对齐后 FFI 自然正确**。
  **前置已落地（2026-07-27）**：`map_type` 一改，i32 的 `int` 就会和 i64 的长度/计数/
  句柄/运行时 helper 混在同一个二元运算里，LLVM 直接拒绝
  （`%argi = add i32 %load5, i64 1`）。irgen 里 391 处整数 builder 已统一换成
  `zan_add` / `zan_icmp` 等包装（`irgen.c` 顶部），先把较窄的一侧符号扩展到较宽的一侧，
  否则每个 lowering 点都要手工扩展。`int` 仍是 i64，这批包装在当前位宽下是恒等变换。
  **实测**：把 `map_type` 的 `TYPE_INT` 临时改成 i32 后，编译器自身、全部 Zan 工具链
  （zanfmt / zandoc）都能编过，`int_width` / `numeric_cast` 输出正确。
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

  **✅ 落地纪要（2026-07-28）**：`map_type` 的 `TYPE_INT` 正式改为 i32（`long` 保持 i64、ARGB 保留负值对齐 C#）。i32 收窄暴露并已修复的回归：集合槽按源类型做符号/零扩展、`List.RemoveAt` 索引先符号扩展再入 i64 槽、`Gate`/`AsyncGate` 句柄（64 位堆指针）改 `long`（修 Firebird 并发池崩溃）、switch 内含 await 的 case 标签改用常量位宽转换（修 async 漏终结符崩溃）、crypto(Bits/Sha512/AesGcm/BigInt) 与 Firebird(FbWire/FbSql) 的 64 位量迁 `long`、JSON 新增 `FK_LONG` + `AsLong/Long/LongOf`。golden IR 与 gui_css/json_entity_mapping golden 已重刷。焦点子集 203/203 通过。
  **A0-2 / A0-3 已完成**（FFI 按声明位宽 lower 见下条；结构体布局随 A0-2 + A2-2 落地，见 A0-3）。
* **A0-2** ✅ 已修（2026-07-28）**按声明类型的真实位宽 lower**。
  `map_type`（`irgen.c`）现在给出 `sbyte/byte`=i8、`short/ushort`=i16、
  `int/uint`=i32、`long/ulong`=i64、`nint/nuint`=指针宽；
  从内存里取回窄值时新增 `promote_loaded()`，**按 Zan 声明类型**而不是 LLVM 位宽
  决定 sext/zext（局部变量、隐式字段、静态字段、对象字段、Span 下标、数组元素、
  调用返回值七条 load 路径）。显式 cast 也按目标声明类型截断/掩码。
  - `Span<T>` 现在按元素真实宽度做 GEP/load/store（`Span<ushort>` 步长 2 字节），
    并保持 `align 1` 的非对齐裸内存契约；A1-2 期间 `Span<short> + & 65535` 的
    临时绕行（`Interop.zan`、`native_memory.zan`）已删除，直接用 `Span<ushort>`。
  - 顺带修的**真实缺陷**（都是 A0-1 把 `int` 收窄成 32 位后暴露出来的）：
    * 值结构体构造 `S s = new S();` 会把 8 字节指针存进结构体槽，单 i32 字段的
      结构体被踩坏（`zan_store_fit()` 现在先按目标结构体类型 load 再 store）。
    * 构造函数实参没有按形参位宽归一化，`base(c * 2)` 传 i64 给 i32 形参导致
      LLVM verify 失败（`irgen_emit.c` / `irgen_stmt.c` 现在走 `coerce_args_to_params`）。
    * `DeterministicRandom.state`（`Game/Foundation/Timing.zan`）是 64 位 LCG 却声明
      `int`，i32 之后生成器失效 → `long`。
    * `SharedTable.NativeExtremeAt` 的 `keepLarger` 声明成 `int`，运行时是
      `int64_t` → `long`（ABI 位宽不符）。
    * ODBC / SQLite 句柄载体仍是 `int`：`DbConnection.handle/env/stmt`、
      `SqliteConnection.handle/stmt`、两处 `HandleFromBuf()` 在 `int` 里拼 64 位
      （`<< 32` 以上全被截断）→ 句柄截断成 32 位后传给 C，必然访问违例。
      现在句柄一律 `nint`、`HandleFromBuf()` 用 `long` 拼装返回 `nint`，
      `calloc(long, long)`（`size_t`）、`sqlite3_last_insert_rowid` 返回 `long`。
  - 新增用例 `tests/conformance/unsigned_widths.zan`：`Span<ushort>/Span<uint>` 步长与
    字节序、`sbyte/byte/ushort/uint` 静态字段、含窄字段的值结构体按值返回、
    `uint[]` / `List<uint>` 元素、`(uint)/(ushort)/(byte)/(sbyte)/(short)` 显式 cast。
  - **验证**：`ctest -j8` **695/695 通过**（含 selfhost gen1 / fixed point）。
  - 遗留：`zan_iwiden()` 仍只看 LLVM 位宽（i8 分不出 `byte`/`sbyte`、i32 分不出
    `int`/`uint`），泛型 erased 槽与部分聚合边界还是靠 64 位寄存器形态兜底；
    真正按声明类型贯通泛型/集合边界归 A2/A3。
* **A0-3** ✅ 已完成（2026-07-28，随 A0-1 / A0-2 / A2-2 落地）结构体字段布局。
  `register_struct_type`（现在在 `irgen.c:2017` 一带）仍然对每个字段走 `map_type`，
  但 `map_type` 自 A0-2 起按声明类型给真实位宽（`int`→i32、`short`→i16…），
  所以顺序布局的字段宽度与 C 一致；LLVM 对非 packed 结构体的填充/对齐本来就同 C，
  A2-1 的「27 种形状 × 4 target 与 clang 声明零差异」即是这条的验证。
  显式布局（`[StructLayout(Explicit)]` + `[FieldOffset(n)]`，含 union）见 A2-2。
  **仍未做**（归 A2-2 尾部）：`Pack = n` packed 布局、`[FieldOffset]` 用在顺序布局
  类型上的校验。
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
* **A0-1b ✅ 已修（2026-07-28）** 三个协议编解码器里"用 `int` 装 64 位量"的残余，
  由 `conformance_sqlserver_tds` / `conformance_mysql_stmt_binary` /
  `conformance_ws_accept` 三个红灯暴露：
  - **TDS**：`TdsReader.UIntLE/IntLE` → `long`（8 字节 bigint、MONEY、PLP 长度都超
    i32），`TdsBuf.U64LE(long)`、`TdsMessage.DoubleBits` → `long`、`IntParam(long)`；
    `TdsValue.Ieee` 的尾数、`Money`/`Scaled` 的单位、`TimeOfDay` 的 tick（scale 7 时
    一天是 8.64e11）、`LimbsToDecimal` 的 `(rem << 32) | limb` 全部改 `long`，
    日期/分钟/偏移这类真 32 位量在调用点显式 `(int)` 收窄。
  - **MySQL**：`readBinValue` 的 LONG/LONGLONG/DOUBLE 累加与 `ieeeToText` 尾数改
    `long`（`bits | (b << 56)` 在 i32 下移位越界，DOUBLE 一律解出 0）；
    `readLenenc` 的 8 字节形式同理 → `long`，三个调用点显式 `(int)`。
  - **SHA-1**（`System.Text.Encoding`）：`Rotl32` 与 h0..h4 / w[] / a..e 改 `long` 车道。
    `int` 车道下 `m >> (32 - n)` 是算术右移，会把符号位灌进旋转结果，摘要全错。
  〔已实测〕`ctest -R 'mysql|sqlserver|tds|ws_accept|websocket|encoding|base64|sha|http'`
  27/27 通过；三个用例的 `ZAN_WARN_NARROW=1` 扫描 0 警告。
  **遗留**：`System.Security.Cryptography` 的 Md5/Sha1/Sha256/Sm3 仍把 32 位车道放在
  `int` 里（`int h1 = 4023233417;`）。结果是对的——`& 4294967295` 现在是 `long`
  字面量，整条表达式在 64 位里算完再截回——但每个单元会带 26 处窄化警告。
  要么显式 `(int)`，要么整体改 `long` 车道，未做。

## A1 `Span<T>` 编译器内建 〔依赖 A0〕

* **A1-1 ✅ 已完成（2026-07-28，commit `6ebffc1`）** `Span<T>` 为值类型 `{ i8* base, i64 len }`（`TYPE_STRUCT`，不入 ARC）；`arr.AsSpan([start[,len]])` 对紧凑数组零拷贝建视图，`s[i]` lower 成 `getelementptr T` + typed load/store（不经 `NativeMemory`），支持 `.Length` / `.Slice(start[,len])`，byte/int/long 宽度均验证。新增 conformance/determinism/leakcheck 用例 `span_intrinsic`（均绿）。
  【待进】noalias 元数据未加（LLVM C API 不直接暴露，待后续）；传参/返回 Span 的 ABI 路径待 A2。
  **必须是编译器内建**，做成库类型 LLVM 看不穿，别名分析依然失败。
* **A1-2 ✅ 已完成（2026-07-28）** `NativeMemory` 的 `GetByte/GetU16/GetU32/GetI64/
  SetByte/SetU16/SetU32/SetI64` 整排 intrinsic 已删（stdlib 声明、`irgen_expr.c`
  的 `nm_load/nm_store`、selfhost 的 `NmLoad/NmStore/NmGet/NmSet`），不留兼容层。
  132 处调用点（Win32Shell 70 / Interop 24 / WebView2 12 / native_memory 用例 12 /
  RegexProgram 10 / Stream 4）改成 `new Span<T>(addr, len)[i]`；`Span` 元素读写补上
  `align 1`，保住原 intrinsic 的非对齐裸内存契约。
  顺带给 selfhost 编译器补齐 `Span<T>`（binder/checker 识别 + irgen 按元素宽度
  align-1 load/store，span 值只带基址、不做边界检查），否则 gen1 编译不了改过的
  stdlib；`EmitCrc32Helper` 的 32 位车道改 `long`（`int` 已是 i32，`3988292384`
  会截断，导致 g2/g3 的 CRC 表不一致、fixed point 失败）。
  `regex|native_memory|span|gui|interop|webview|stream|zandb|respack|http` +
  `selfhost` 共 53+2 个用例全绿。
  **遗留（A0-2）**：`map_type()` 把 `sbyte/ushort/uint/ulong` 一律映射成 i64，
  所以 `Span<ushort>` 实际按 8 字节步长读写。这批 u16 场景先用
  `Span<short> + & 65535` 绕过，无符号原生宽度待 A0-2 修。
* **A1-3** ✅ 已完成（2026-07-28，`dd498ff`）`byte[]` 作为全库统一的字节缓冲类型
  + `s.ToBytes()` / `b.ToStr()` 桥接，详见 B2 章开头。
* **判定**：Zan 版 `blur_rect` / `fill_radial` / `blit_image` 对 C 版基准 ≥0.9x。
  达不到先修优化管线，不要往下推进。

## A2 FFI ABI 分类 〔依赖 A0〕

现在 extern 声明的全部逻辑是 `irgen_emit.c:294-307`：
逐个 `map_type` 后 `LLVMFunctionType(rt, pt, pc, 0)`，`IsVarArg` 恒为 0，无任何 ABI 处理。

* **A2-0 ✅ 已完成（2026-07-28）声明位宽逐符号核对**。工具 `_scratch/a2/`：
  `extern_audit.py` 按 `DllImport` 的 `EntryPoint` 抽出 stdlib 全部 **864 个 extern**
  （带 `#if WINDOWS / LINUX || MACOS` 分支归属），与两个来源比对——
  仓库自己定义的 C 符号（`src/`、`stdlib/**/native/`，245 个），以及
  Windows SDK + CRT 头的 clang AST（`sys_audit.py`，480 个，typedef 递归展开到
  基础类型）。两边现在都是 **0 处不一致**。分三批修：
  - **A2-0a**（`0267829`）运行时 shim：状态/布尔类返回在 C 侧回到 `int32_t`
    （io ready/alive/connect_status、dispatch_post、thread_start、SharedTable
    set/delete/exists/match/destroy），真 64 位量在 Zan 侧改 `long`
    （send/recv 字节数、`zan_monotonic_us`）。顺带 `Stopwatch.GetMicroseconds()`
    在 i32 下 35 分钟溢出、`GetMilliseconds()` 截断 `GetTickCount64`，都改 `long`。
  - **A2-0b**（`e577ce2`）GUI/SDL 原生层 176 处：C 侧标量降到 `i32`，
    SDL_Window/Renderer/Texture 与 GPU device/texture 这些**指针句柄**改
    `intptr_t`，Zan 侧对应字段/返回值改 `nint`（`SdlWindow`/`SdlRenderer`/
    `SdlTexture`/`SdlGpu`/`SdlGpuTexture`），tick/timestamp 两侧保持 64 位。
    另修 `zan_gui_poll_event` 多声明 6 个参数、`zan_gui_sleep_ms` 应为 `void`、
    `zan_gui_destroy_surface` 应返回 `int`。
  - **A2-0c** 系统库 96 处：`size_t` 车道（`calloc`/`malloc`/`memcpy`/`memset`/
    `memchr`/`strncpy` 的长度、`strlen` 的返回、`fread`/`fwrite` 的
    size/count/返回）在 Zan 侧改 `long`，30 个吃这些返回值的调用点补 `(int)`；
    `gethostbyname`/`memset` 返回指针改 `nint`；Win32 窄返回按真实宽度声明
    （`RegisterClassEx*`→`ushort`(ATOM)、`GetKeyState`→`short`、
    `htons`/`ntohs`→`ushort`、`SetLayeredWindowAttributes` 的 alpha→`byte`、
    `WSAStartup` 的 version→`ushort`、`GlobalAlloc` 的 SIZE_T→`long`）。
  - 用例：`tests/conformance/ffi_widths.zan`（指针句柄往返、size_t 车道、
    64 位单调钟）。`ctest -j8` 695+3 全绿。
  - **未覆盖**：libpq(24)、OpenSSL(25)、sqlite3(17)、ODBC(24)、POSIX-only 的
    crt(46) 与直连 SDL3 的声明没有对应头可比对，只做了人工抽查；
    这些库的头装上后重跑 `sys_audit.py` 即可闭环。

* **A2-1 ✅ 已完成（2026-07-28）结构体按值传参/返回**（`src/compiler/irgen_abi.c`）。
  声明里出现结构体的 extern 会生成两个函数：真符号按平台 C ABI 的寄存器/内存形态
  声明，外加一个内部 thunk 保留 Zan 侧签名做转换，调用点不变。
  - **Win64**：1/2/4/8 字节整体塞进 `i8/i16/i32/i64`，其余按指针间接传、
    返回走 `sret`（Win64 无 `byval`）。
  - **SysV AMD64**：前 16 字节按 eightbyte 分 INTEGER / SSE，
    INTEGER 用实际占用宽度（`i64`/`i32`/`i24`，尾部 padding 不进寄存器），
    SSE 按 `double` / `<2 x float>` / `float`；>16 字节走 `byval(T) align 8`，
    返回走 `sret`。
  - **AAPCS64**：HFA（≤4 个同种浮点）传 `[N x float]`/`[N x double]`
    （非 Apple 带 `alignstack(8)`）、返回直接用结构体类型；否则 ≤8 字节 `i64`
    （返回用实际宽度）、≤16 字节 `[2 x i64]`、更大间接 + `sret`。
  - 其它 target（如 riscv64）遇到按值结构体报错，不再静默生成错误调用。
  - **对照验证**：27 种形状 × take/make × 4 个 target 的声明，与 clang 对同样 C
    签名生成的声明逐条比对，**0 处不一致**（工具 `_scratch/a3/`）。
    Win64 上还跑了真实链接的往返用例（`_scratch/a3/probe.zan` + `abi_probe.dll`）：
    改之前 `p8_sum` 返回 3、`p12_sum` 直接访问违例。
  - 用例：`tests/abi/struct_abi.zan` + `tests/abi/expected/<target>.decls`，
    ctest 里是 `abi_signatures_{win-x64,linux-x64,linux-arm64,macos-arm64}`。
  - **顺带修掉的既有 bug**：`float` 是 f32 存储而字面量/算术是 double，
    但存进 float 槽时没有 `fptrunc`（写进去的是 f64 位型，打印出来是 0）、
    打印时没有 `fpext`（LLVM verify 直接失败）、`float op double` 两边类型不一致。
    见 `tests/conformance/float_widths.zan`。
* **A2-2 ✅ 已完成（2026-07-28）`[StructLayout(Explicit)]` + `[FieldOffset(n)]`**。
  - **顺序布局**（默认，即 `[StructLayout]` / `[StructLayout(LayoutKind.Sequential)]`）：
    LLVM 对非 packed 结构体的填充/对齐本来就和 C 一致，所以不需要额外代码；
    A2-1 的 27 种形状 × 4 target 与 clang 声明零差异即是这条的验证。
  - **显式布局**：`[StructLayout(LayoutKind.Explicit)]` 的类型整体降成一个
    「对齐载体 + 字节块」（`%struct.Event = { i64, [8 x i8] }`），每个字段按自己的
    `[FieldOffset(n)]` 用字节 GEP 定址，再按字段类型重新 typed（否则存的时候会按
    值自身的宽度盖掉邻居）。**union 没有新增类型 kind**——两个字段写同一个偏移就是
    union，可以表达 `SDL_Event` / `XEvent` / `DEBUG_EVENT`。
  - 缺 `[FieldOffset]`、偏移未按字段对齐、以及 explicit 类型带虚方法（vptr 没有
    偏移可放）都会报错，不再静默生成错位的访问。
  - 用例：`tests/conformance/explicit_layout.zan`（int/byte 重叠、带洞的 tag +
    payload、long/int 对/float 三者共用同一偏移）。`ctest -j8` 708/708。
  - **未做**：`Pack = n`（packed 布局）、`[FieldOffset]` 用于顺序布局类型的校验、
    以及把 stdlib 里硬编码的 `SDL_Event`/`MSG` 偏移改成声明式（属于 B5）。
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
  这只是把类型在 await 站点补回来，帧 result 槽当时仍是硬编码 `i64`；
  typed frame result 已在 **A27-1** 做完（存取两端都按声明类型编解码）。
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
> 仍未做完、不要因为这些用例通过就认为已完成的：await 与 ARC 在异常退出时的所有权、
> `anf_stmt_contains_await` 的 try/switch 补漏（A8-6）。
> （typed frame result 与取消路径已分别在 A27-1 / A27-2 完成。）

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
**与 A0（int=32）直接冲突**。**已修（A27）**：`ASYNC_CPS_DESIGN.md` 增补了
"Result slot"（按声明类型编解码）与 "Cancellation" 两节，帧布局说明也补了 header
后半段（cancel / child / lnext）。EH 与 await 交互的落差仍未在文档里补齐。

---

# B. 标准库

## B1 大体量模块的改造（**都不搬走**）

* **B1-1 ✅ 已完成（2026-07-29）：Sdk/Jd 代码生成器验证 + 清理。**
  `scripts/generate_jd_sdk.py`（362 行）从 `stdlib/Sdk/jos-net-open-api-sdk-2.0/`（724 个 C# 文件，
  未纳入 git）读取原版 SDK，生成 `Domain/`（212）+ `Api/`（255）共 467 个 Zan 文件。
  **验证**：删除 `Domain/` + `Api/` → 重跑生成器 → 467/467 文件与原始输出**字节一致** →
  `conformance_sdk_jd` / `sdk_jd_api` / `sdk_jd_client` / `sdk_jd_modules` **4/4 Passed**。
  手写公共层（`JdClient`/`JdSign`/`JdRequest`/`JdResponse`/`JdException`，5 文件 ~20KB）
  已与生成层目录隔离（生成器不碰根目录文件）。
  删除空目录 `Sdk/Tb`。`Sdk/jos-net-open-api-sdk-2.0` 实为 724 个 C# 源文件（非空），
  是生成器的输入源，保留不删。
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

**A1-3 ✅ 已完成（2026-07-28）：字节缓冲类型与 string 桥接就位。**
`byte[]` 现在是"自有字节缓冲"的正式类型：`new byte[n]` 归零、带长度、
元素就是 i8、指针指向首元素（16 字节头在前），所以可以原样传给声明为
`string`（`char*`）的 extern 和 Zan 函数。新增两个编译器内建完成与文本的互转：

* `s.ToBytes()` → `byte[]`，复制 `s` 的字节（不含结尾 NUL）；
* `b.ToStr()` / `b.ToStr(off, len)` → 由 `zan_rt_str_alloc` 分配的**受 ARC 管理**的
  string（尾部补 NUL），因此**不能**再对它调 `free`；`free` 仍然只用于
  `calloc` 出来的裸缓冲区（`ffi_free_consumes_string` 的语义不变）。

用例 `tests/conformance/byte_buffer.zan`（含 conformance / determinism / leakcheck 三档）。

**B2-1 第一批 ✅ 已完成：Aes / AesGcm / ResourcePack 这一条链已改为 `byte[]`。**
`Aes` 的 `inv` / 轮密钥 / state / 输出缓冲、`AesGcm` 的 blk / lb / j0 / ctr / 密文明文，
以及 `ResourcePack` 的密文列表与 `Read()` 返回值全部换成 `byte[]`，
两个类里的 `extern string calloc` / `extern void free` 声明连同 **23 处 `free()`**
一起删除（其中 `WechatPayV2` 对 `Aes.DecryptBlockEcb` 结果本来就漏了 free，
换成 `byte[]` 后由 ARC 负责）。`ResourcePack.Read()` 返回 `byte[]`，
调用方用 `.ToStr(0, len)` 取文本。
输入参数仍声明为 `string`（`char*` 边界），`byte[]` 可直接传入，
所以这次改造不需要动 key / iv / data 这些只读入参。

**B2-1 第二批 ✅ 已完成（2026-07-29）：摘要 / 编码链 → `byte[]`。**
把"哈希/编码返回裸 `calloc` 缓冲、调用方负责 `free`"这条链整体切成 `byte[]`
（返回值受 ARC 管理，删掉全部对应 `free`）。

* **哈希原语**：`Sha1` / `Sha256` / `Sha512` / `Md5` / `Sm3` 的 `Hash(...)` 由
  `string`→`byte[]`；各类删掉 `[DllImport("crt")] extern string calloc` /
  `extern void free`，工作缓冲 `calloc(plen+1,1)`→`new byte[plen]`、输出
  `calloc(n+1,1)`→`new byte[n]`，删中间 `free(m)`；`wBE` / `wLE` 写入辅助的形参
  `string`→`byte[]`。
* **HMAC**：`Hmac.Compute` / `Sha256Mac` / `hashOf` → `byte[]`；`k0` / `inner` /
  `outer` 用 `new byte[]`，删掉 ipad/opad/inner-hash 那批 `free`。
* **Hex / Base64**：`Hex.Decode` / `Base64.Decode` → `byte[]`（删各自的
  `extern string calloc`）；`Encode(buf, len)` 签名不变（入参按 `char*` 只读缓冲、
  返回 `string`）。
* **Hkdf.Extract → `byte[]`**（就是 HMAC-SHA256 结果）；`Expand` 内部 `t` / `msg`
  改 `byte[]`、删中间 `free` 链。`Expand` / `Derive` 的**最终返回 `okm` 仍是
  `calloc` 出的 `string`**（调用方按裸缓冲取用），留待后续批次，不在本批。
* **Rsa / Sm2 的摘要局部**：`Rsa.hashOf` → `byte[]`，`EncryptOaep` / `mgf1` /
  `emsaPkcs1Sha256` 里由哈希得来的 `lHash` / `h` 随之改、删其 `free`；`Sm2` 里
  `Sm3.Hash` 结果 `za` / `eh` / `c3` / `h` → `byte[]`、删其 `free`。这两类**自身的
  填充 / 密文输出缓冲仍是 `calloc`**（`Rsa` 的 OAEP/PKCS1 缓冲、`Sm2` 的密文），
  本批只切摘要那一段。
* **静态字段**：`Aes.sbox` / `Sm4.sbox` 由 `string`→`byte[]`（与本就是 `byte[]` 的
  `Aes.inv` 对齐）。

**调用点与 `free` 归属**（每批都连调用方一起改，这是本批的重点）：

* **MySql**（`MySqlConnection`）：`scramble41` / `scrambleNative` 的 `a` / `b` / `c`
  （Sha256/Sha1 链）→ `byte[]`，删 6 处 `free(a/b/c)`；`pemToDer` → `byte[]`，
  其中 `der`（`Base64.Decode`）→ `byte[]`，删 full-auth 路径两处 `free(der)`。
  自有 `calloc` 缓冲（`cat` / `tok` / `b64`）仍保留并照旧 `free`。
* **Firebird**（`FbSrp` / `FbWire`）：`Sha1Of` / `Sha256Of` 从
  `FbBytes.Own(Sha1.Hash(...), 20)` 改为 `FbBytes.CopyOf(...)`——摘要现在是 ARC 管理的
  `byte[]`，不能再让 `FbBytes` 夺走所有权去 `free`。为此 `FbWire.zan` 新增
  `FbBytes.CopyOf(src, len)`：把调用方仍持有的缓冲拷进受计数的块。
* **Wechat**：`JdSign` / `CheckSignature` / `WXBizMsgCrypt.MsgSignature` 的 `digest`
  （Sha1/Md5）→ `byte[]`，直接喂 `Hex.Encode`；`WechatPayV2.Sign`（含 HMAC 分支）的
  `digest`、`DecodeRefundReqInfo` 的 `md5` / `encrypted`、`WechatPayV3Client` 的
  `encrypted`、`WXBizMsgCrypt` 的 `aesKey` 字段 / `key` / `cipher` → `byte[]`；
  对密文按块切片由 `.Substring(off,16)` 改为 `byte[]` 上的 `.ToStr(off,16)`。
* **ResourcePack**：`nameHash` 的 `h`（Sha256）、writer 的 `nBuf` / `dBuf`、verify 的
  `nBuf` / `eBuf`（`Hex.Decode`）→ `byte[]`，删对应 `free`。
* **examples**（ra2）：`MapPack.Decode` 的 `raw`（`Base64.Decode`）→ `byte[]` 删
  `free`；`WsKey.ModulusBytes` 的 `blob` → `byte[]` 删 `free`。
* **tests**：`byte_buffer` / `crypto_digests` / `respack_roundtrip` 里 `Hex.Decode`
  结果 → `byte[]`、打印改 `.ToStr()`，删示例里的 `free(shareA/shareB)`。

〔已实测〕`ctest --test-dir build`（conformance / determinism / leakcheck 三档都跑）：
`byte_buffer`、`encoding_base64`、`crypto_digests`、`respack_roundtrip`、
`mysql_auth_vectors`、`mysql_stmt_binary`、`firebird_wire`、
`sdk_wechat`(+`_crypt`/`_mp_modules`/`_product_modules`/`_special_modules`/`_tenpay`)
**全部 Passed，leakcheck 无泄漏**。ra2 示例按 `examples/game/ra2/build.sh` 全量
（76 文件）编译通过。

**B2-1 第三批 ✅ 已完成（2026-07-29）：Hkdf 链 → `byte[]`。**
第二批把 `Hkdf.Extract` 切成 `byte[]`，但 `Expand` / `Derive` 的最终 `okm` 仍是
`calloc` 出的 `string`（当时留作后续）。本批把这条链收尾：

* `Hkdf.Expand` / `Derive` → `byte[]`：`okm` 由 `calloc(outLen+1,1)` 改成
  `new byte[outLen]`；`Hkdf` 不再需要 `[DllImport("crt")] extern string calloc` /
  `extern void free`，两条 import 一并删除（`Expand` 内部的 `t` / `msg` 第二批已是
  `byte[]`）。
* **调用点与 `free` 归属**（`Hkdf.Derive` 全库仅 `ResourcePack` 两处用）：
  `ResourcePack.indexKey` / `entryKey` 返回类型 `string`→`byte[]`；writer 的 `ik` /
  `ek`、reader `Open` 的 `ik`、`Read` 的 `ek` 全部改 `byte[]`，删掉对应的
  `free(ik)` / `free(ek)`（派生密钥现在由 ARC 管理）。`AesGcm.Encrypt` / `Decrypt`
  的 `key` 仍是 `string`（`char*` 只读边界），`byte[]` 直接传入不用改签名。
  `entryKey` 里的 `info` 仍是自有 `calloc` 缓冲，照旧 `free(info)`。

〔已实测〕`respack_roundtrip` conformance / determinism / leakcheck 三档全 **Passed，
无泄漏**。

**B2-1 第四批 ✅ 已完成（2026-07-29）：Rsa 内部填充缓冲 → `byte[]`（自成一批，
无外部调用点变化）。**

* `Rsa.zan` 里所有 `calloc` 出的填充缓冲全部改 `new byte[...]`：`mgf1` 的
  `mask`/`buf`、`EncryptOaep`/`DecryptOaep` 的 `db`/`maskedDb`/`maskedSeed`/`seed`/
  `outb`、`EncryptPkcs1`/`DecryptPkcs1` 的 `em`/`outb`、`emsaPkcs1Sha256` 的 `em`、
  `sha256DigestInfo` 的 `di`。相应删掉这些缓冲的 `free`。**`Rsa` 不再调用
  `calloc`，删掉 `[DllImport("crt")] extern string calloc`。**
* **`free` 归属**：`ModExp` 仍返回 `BigInt.ToBytesBE` 的 `calloc` 字符串（`BigInt`
  未改），所以 `DecryptOaep` / `DecryptPkcs1` 的 `free(em)` 和 `VerifyPkcs1Sha256`
  的 `free(em2)`（都是 `ModExp` 结果）**保留**，`extern void free` 仍留着。等 B2-1
  的 BigInt 批把 `ToBytesBE` 翻成 `byte[]` 时再一并删掉这三处 `free` 和 import。
* **返回类型**：`DecryptOaep` / `DecryptPkcs1` / `mgf1` / `emsaPkcs1Sha256` /
  `sha256DigestInfo` → `byte[]`；`EncryptOaep` / `EncryptPkcs1` / `SignPkcs1Sha256`
  仍返回 `string`（`ModExp`/BigInt 结果，调用方照旧 `free`）。`DecryptOaep`/
  `DecryptPkcs1` 全库无调用点，故本批不触碰任何外部文件。

〔已实测〕`cmake --build` 重建 stdlib（stamp 刷新）后重跑，强制重新编译（非缓存）：
`crypto_digests` / `mysql_auth_vectors` / `respack_roundtrip` / `sdk_wechat_tenpay`
（后三者经 RsaKey/MySql 认证/微信支付实际走 RSA）conformance / determinism /
leakcheck **12/12 全 Passed，无泄漏**。

**B2-1 第五批 ✅ 已完成（2026-07-29）：Sm2 全部字节缓冲 → `byte[]`（`calloc` 与
`free` import 双双删除）。**

* `Sm2.zan` 里所有 `calloc` 缓冲改 `new byte[...]`：`fromInt` 的 `b`、`computeE`
  的 `zin`/`ein`、`kdf` 的 `outb`/`buf`、`Encrypt`/`Decrypt` 的 `z`/`outb`/`m`/
  `c3in`、`sliceBE` 的 `b`。**Sm2 不再有任何 `calloc`/`free`，两条 import 全删。**
* **返回类型**：`Encrypt` / `Decrypt` / `kdf` / `sliceBE` → `byte[]`；`writeBE32`
  的 `dst` 保持 `string`（`char*` 只读边界，`byte[]` 直接写入）。`Sm2.Encrypt` /
  `Decrypt` 全库无调用点、无测试引用，故本批不触碰外部文件。
* **验证难点**：Sm2 目前无任何测试且无 stdlib/examples 调用点，`cmake --build` 只
  stamp 不编译它。为此写了 `_scratch/sm2_check.zan`（encrypt→decrypt 往返 +
  sign/verify 往返），`build\zanc.exe` 编译 30 文件通过，运行输出 `ENC_OK` /
  `SIG_OK`；scratch 用后即删，未留产物。

〔遗留〕`writeBE32` 里 `v.ToBytesBE(32)` 仍是未释放的 `calloc` 字符串（`BigInt`
本批未动，这是既有小泄漏）。待 `BigInt.ToBytesBE → byte[]` 批一并消除。

**B2-1 第六批 ✅ 已完成（2026-07-29）：Sm4 全部字节缓冲 → `byte[]`（`calloc` 与
`free` import 双双删除）。**

* `Sm4.zan` 里所有 `calloc` 缓冲改 `new byte[...]`：`EncryptBlock`/`DecryptBlock`
  的 `o`、`EncryptCbc`/`DecryptCbc` 的 `outb`/`prev`/`blk`/`ob`。删掉循环内
  `free(blk)`/`free(ob)` 和 `free(prev)`。**Sm4 不再有任何 `calloc`/`free`，两条
  import 全删。**
* **返回类型**：`EncryptBlock` / `DecryptBlock` / `EncryptCbc` / `DecryptCbc` →
  `byte[]`；`cryptBlock` 的 `out16` 保持 `string`（`char*` 写边界，`byte[]` 直写）。
  Sm4 全库无调用点、无测试引用，本批不触碰外部文件。
* **验证**：同 Sm2，Sm4 无测试无调用点。写 `_scratch/sm4_check.zan`：GB/T
  32907-2016 单块已知答案（key=plaintext=`0123456789abcdeffedcba9876543210` →
  `681edf34d206965e86b3e94f536e4246` ✔）+ 块解密往返（`BLK_OK`）+ CBC/PKCS#7
  往返（`CBC_OK`）。`build\zanc.exe` 编译 30 文件通过；scratch 用后即删。

**B2-1 第七批 ✅ 已完成（2026-07-29）：`BigInt.ToBytesBE → byte[]`，收尾整条
Cryptography 链（横跨 5 个文件的调用点，并清掉 `Rsa` 最后的 `free` import）。**

* `BigInt.zan`：`ToBytesBE` 返回 `byte[]`，`outb = new byte[outLen]`，删 `BigInt`
  唯一的 `extern string calloc`。**`BigInt` 从此无 `calloc`。**
* `Rsa.zan`：`ModExp` 及 `EncryptOaep`/`EncryptPkcs1`/`SignPkcs1Sha256`（都 `return
  ModExp(...)`）返回类型 `string → byte[]`；`DecryptOaep`/`DecryptPkcs1` 的局部
  `em`、`VerifyPkcs1Sha256` 的 `em2` 由 `string` 改 `byte[]`，删掉这三处 `free`。
  **`Rsa` 现在无任何 `calloc`/`free`，删掉最后的 `extern void free` import。**
* **调用点（5 处）**：
  - `RsaKey.SignSha256`/`EncryptOaepSha1`：`sig`/`encrypted` → `byte[]`，直传
    `Base64.Encode`（原本就没 `free`，是既有泄漏，本批顺带修掉）。
  - `MySqlConnection.fullAuthCipher`：返回 `byte[]`，`cph` → `byte[]`；调用方
    `doConnect` 的 `cipher` → `byte[]`，删 `free(cipher)`。
  - `ResourcePack`（writer）：`sig` → `byte[]`，删 `free(sig)`（reader 侧 `sig` 来自
    `File.ReadBytes`、仍是 `calloc`，`free` 保留，不动）。
  - `FbSrp.Minimal`：`raw` → `byte[]`，删 `FbBytes.Free(raw)`。
  - `WsKey`（ra2 示例）：`plain` → `byte[]`，删 `free(plain)`。
* 顺带消除第五批遗留的 `Sm2.writeBE32` 里 `v.ToBytesBE(32)` 未释放 `calloc` 小泄漏
  （现在是 ARC `byte[]`）。

〔已实测〕`cmake --build` 重刷 stdlib stamp 后，`firebird_wire`（走 FbSrp）/
`mysql_auth_vectors`（走 OAEP 全认证）/ `respack_roundtrip`（走 Pkcs1 签验）/
`sdk_wechat_tenpay`（走 RSA）conformance / determinism / leakcheck **12/12 全
Passed，无泄漏**（determinism_wechat 17s、确系重新编译非缓存）。
另：`DecryptOaep`/`DecryptPkcs1`/`EncryptPkcs1` 全库无调用点无测试（死代码），为
验证 `em` 翻 `byte[]`，用本机 openssl 生成 1024-bit 测试密钥写
`_scratch/rsa_check.zan` 做 OAEP + PKCS#1 加解密往返 → `OAEP_OK` / `PKCS1_OK`；
ra2 示例 76 文件编译通过（验证 `WsKey`）；scratch 用后即删。

**B2-1 第八批 ✅ 已完成（2026-07-29）：IO 链——`File.ReadBytes → byte[]`，
`ReadAllText`/`Copy` 内部缓冲改 `byte[]`，`File` 从此无 `calloc`/`free`；连带把
`ResourcePack` reader、Game.Rts / ra2 的二进制读取链一起翻过来。**

* `stdlib/System/IO/File.zan`：删掉 `extern string calloc` / `extern void free`
  两个 import。`ReadAllText`：`buf = new byte[size]`，按 `fread` 实际读到的字节数
  `buf.ToStr(0, got)` 返回（原来直接返回 `calloc` 缓冲，靠 NUL 结尾）。`Copy`：
  `buf = new byte[len]`，删 `free(buf)`。`ReadAllLines`：删 `free(content)`
  （`ReadAllText` 现在返回 ARC 串）。**`ReadBytes` 返回类型 `string → byte[]`**，
  `new byte[count]` 零初始化、自带长度、能保住内嵌 NUL，调用方不再释放。
* `stdlib/System/Resources/ResourcePack.zan`（reader）：`hdr`/`idxCt`/`sig`/`ct`
  改 `byte[]`，删掉这四处 `free`（`packId`/`idxIv`/`idxTag`/`nh`/`iv`/`tg` 仍是
  自己 `calloc` 的小缓冲，所有权不变，`free` 保留）。
* `Game.Rts.Formats`：`Binary.FromFile`、`Pal.FromFile` 的 `buf → byte[]`
  （`FromBuffer`/`Parse` 形参仍是 `string`，直接接 `byte[]`）。
* ra2 示例（同一条链，按用户要求一并改）：`Mix.zan` 的 `data` 字段 → `byte[]`、
  `ReadFile`/`ReadEntry` 返回 `byte[]`（miss 返回 `new byte[0]`）、`FromBuffer`
  形参 → `byte[]`、`CharOf` 用 `new byte[1].ToStr(0,1)`、`ComputeHeaderLength`/
  `ParseEncrypted` 的临时缓冲 → `byte[]`，**`Mix` 无 `calloc`**（`free(key)` 保留，
  那是 `WsKey.Decrypt` 的 `calloc` 结果）；`Close()` 改成把 `data` 置 null；
  `Vfs.Read → byte[]`；`AssetDb`（4 处 `raw` + 删 3 处 `free`，`extern free` 已成
  死 import 一并删）、`Theater`（删 `free(raw)` 2 处，`ei`/`idx` 来自 `TmpTemplate`
  的 `calloc`，`free` 保留）、`Screens.LoadPcx`（删 `free(raw)` 与死 import）、
  `tests/mix.zan`（`BuildMix → byte[]`，删 `calloc`，`ini.Substring → ini.ToStr`）。
* `src/ide_zan/AssetManager.zan`：`packAddTree`/`PackAssets` 的 `data → byte[]`
  （writer 的 `Add` 不接管释放，原本也没 `free`）。
* `tests/conformance/io_errors.zan`：`ReadBytes` 的返回变量 → `byte[]`。

〔已实测〕`byte[]` 与 `string` 的布局兼容，因此**不必把下游 `string data` 形参/
字段全部翻掉**：探针 `_scratch/b28_probe*.zan` 验证 `byte[]` 传给 `string` 形参、
存进 `string` 字段、以及在其上调 `.Length`/`Substring`/`IndexOf` 全部正确
（`"abcdef".ToBytes()` → `len=6 sub=bcd idx=2`），200 次循环 `--check-leaks`
无泄漏（说明 `string` 槽位释放 `byte[]` 也走对了引用计数路径）。
全库确认无任何地方 `free()` `ReadAllText`/`ReadBytes` 的返回值。
targeted：`io_errors` / `respack_roundtrip` / `rts_{ini,pal,shp,tmp,csf}_parse` 的
conformance / determinism / leakcheck **21/21 Passed**；随后跑全量 `ctest`
（711 项，`-j 8` 有 35 项因端口/临时文件互抢而 Fail，逐个串行重跑 **35/35
Passed**）；ra2 示例 76 文件、ZanIDE 186 文件编译通过；
`examples/game/ra2/tests/mix.zan --check-leaks` 运行正确、无泄漏。
（注：该用例三个哈希与 `expected_mix.out` 不符、`h_rules` 还是负数，说明
`& 0xFFFFFFFF` 掩码在某次改动后失效——**与本批无关**：`_scratch/b28_hash.zan`
把新旧两版 `CharOf`（`calloc` vs `new byte[1].ToStr`）并排跑，
`old_lmd == new_lmd == 272478703`、`old_rules == new_rules == -414863318`，
逐字相同。按用户「RA2 先不管」暂不处理，记在这里备查。）

**B2-1 第九批 ✅ 已完成（2026-07-29）：Net 全链 + Diagnostics / IO.Directory /
Text.Encoding / CSPRNG / ResourcePack writer —— raw `calloc` 缓冲 → `byte[]`。**

* `Net/Sockets`：`Socket`（`WSAStartup`/`ioctlsocket`/`setsockopt` 的 `optval`、
  `BuildSockAddr → byte[]`、`Accept`/`RecvFrom`/`Send*` 的收发缓冲）、
  `AsyncSocket.Receive`、`TcpClient`（死 import）——三个文件都无 `calloc`/`free`。
  接收路径统一 `buf.ToStr(0, n)`，长度由 `recv` 返回值决定，不靠 NUL。
* `Net/Mqtt`：`MqttBroker` / `MqttClient` 的读缓冲与 CONNECT/PUBLISH/SUBSCRIBE/
  UNSUBSCRIBE/PING/DISCONNECT 报文缓冲全部 `byte[]`，发送仍带显式 `offset` 长度；
  取 topic/payload 用 `body.ToStr(off, len)`。
* `Net/WebSocket`：`WsFrame.Encode() → byte[]`（原来 `calloc` 出去从没人释放，
  是真泄漏）、掩码解码缓冲 → `byte[]` + `ToStr`；`WsReader`/`WsWriter` 的
  `buf`/`tmp` 字段 → `byte[]`（`Ensure` 扩容不再 `free`）；`WssServer` 的 `plain`、
  `WssClient.SendFrame` 的整帧缓冲同改。
* `Net/Worker`：`WSAPROTOCOL_INFOA` 句柄传递缓冲（372 字节）、`RecvExact(dest)`
  形参 → `byte[]`。`Net/Tls/TlsStream`：`scratch` 字段 → `byte[]`，`RecvAsync`
  用 `ToStr(0, n)`。`Web/WebApp.MakeBuffer → byte[]`（注释早说是 ARC，实际不是）。
* `Diagnostics`：`ProcessHost.Env`/`SelfExe`（`SelfExe` 现在按 `GetModuleFileNameA`/
  `readlink` 的返回长度截断，失败返回 `""`）、`ServerMetrics` 的 timeval /
  `GetProcessTimes` / `PROCESS_MEMORY_COUNTERS` 缓冲与 `LeInt(byte[] ...)`。
* `IO/Directory`：`WIN32_FIND_DATAW` 缓冲 → `byte[]`，`winName(byte[])`。
* `Text/Encoding`：`IntToString` / `CharFromCode` / `Sha1(msg)` 的临时缓冲，
  `Sha1(..., byte[] digest)`，`WebSocketAccept` 不再 `calloc`/`free`。
* `Security/Cryptography/RandomNumberGenerator.GetBytes → byte[]`：连带 17 个
  调用点（Wechat 5 个、`WssClient`、`ResourcePack`、`RsaKey`、`FbSrp`、
  `MySqlConnection.genSeed`、`AssetManager`）。**注意**：`FbSrp.Create` 的
  `FbBytes.Free(r)` 与 `WXBizMsgCrypt.RandAscii` 的 `free(rnd)` 必须一并删掉，
  否则 ARC 缓冲被 C `free` 掉 → 堆损坏（实测 `conformance_firebird_wire` /
  `conformance_sdk_wechat_crypt` 退出码 `0xC0000374`，删掉后 6/6 Passed）。
* `Resources/ResourcePack`（writer 侧收尾）：`idx`/`hdr`/`tag`/`idxTag` 缓冲、
  `nameHash → byte[]`、`MixKey → byte[]`、`entryKey`/`indexKey` 形参、
  `PackIndexEntry` 的 `hash`/`iv`/`tag` 字段、`storeBE32/64(byte[] ...)`、
  `writeMagic(byte[])`——两个类都无 `calloc`/`free`。`AssetManager` 生成的
  `AssetPack.zan` 模板同步改成 `byte[] a/b/k`（`Hex.Decode` 早已返回 `byte[]`）。
* 测试同步：`tests/conformance/ws_accept.zan`、`respack_roundtrip.zan` 的
  `calloc` 缓冲改 `new byte[]`（`digest`/`blob`/`bad`/翻位缓冲）。
* 顺带修 `src/runtime/gui_runtime_mac.m`：`zan_gui_wake` 前向声明写成 `i64`、
  定义是 `i32`，Xcode clang 直接报 `conflicting types` —— macOS GUI driver 的
  CI job（`drivers.yml`）就是卡在这一步失败的。

〔实测〕`examples/net` 的 `mqtt_pubsub` / `websocket_chat` / `tcp_echo` /
`udp_messaging` / `sse_events` 编译并运行正确；`ctest -R
'socket|udp|tcp|http|websocket|mqtt|net|web|zgm'` **115/115 Passed**、
`-R 'ws_accept|encoding|directory|io_|metric|process|http|web|worker|mqtt|tls|
wss|dir_|file|path|text'` **39/39 Passed**、`-R 'respack|ws_accept|wechat|mysql|
firebird|rsa|crypt|hkdf|aes|sha|hex|encoding|resource|asset|ide'` 48 项在修掉上面
那两处 double-free 后全绿。
（既有问题，与本批无关：`examples/net/{http_server,http_client,worker_server}.zan`
编译失败 `undefined type 'HttpClient'`——`HttpClient` 在 `System.Net.Http.Client`
命名空间，示例只 `using System.Net.Http;`。）

**B2-1 第十批 ✅ 已完成（2026-07-29）：`System.Data` 全驱动 + Wechat / RsaKey /
Gui.Text / Game.Rts / Zgm / Windows.Forms —— stdlib 里最后一批 `calloc`-as-string。
至此 `stdlib/**/*.zan` 中 `extern string calloc` 归零。**

* `System/Data`：`SqliteConnection`（`ppDb`/`errmsg`/`ppStmt`/`pzTail` 句柄槽位，
  native handle 仍由 `HandleFromBuf` 读出）、`DbConnection`（ODBC 的 `pEnv`/`pDbc`/
  `pStmt`/`pRows`/`pOutLen`/`pInd`/`pcc`/`nameBuf` 等出参缓冲）、`Redis/RedisClient`
  （`inbuf`/`tmp` 字段与扩容，取值 `inbuf.ToStr(start, len)`）、
  `Postgres/PostgresConnection`（`BuildParamBlock` 的 `char**` 块与各参数缓冲改由
  `List<byte[]> keepAlive` 持有，libpq 调用期间保持引用，删掉 `FreeParamBlock`）、
  `SqlServer/TdsCodec`（`TdsBuf.Alloc → byte[]`、`TdsBytes.data → byte[]`、
  `Release()` 只置 null；`Narrow`/`Ucs2` 走 `ToStr`）、`SqlServer/SqlServerConnection`
  （`recvExact(byte[] dst)`、TDS 包头/包体）、`MySql/MySqlConnection`
  （`readPacket() → byte[]`、`recvExact`、握手/认证/`COM_STMT_*` 全部报文缓冲、
  `scramble41`/`scrambleNative`/`authToken → byte[]`、`pemToDer`/`fullAuthCipher`
  的 DER/模数/指数缓冲）、`Firebird/FbWire`（`FbBytes.data → byte[]`、`FbBuf.Alloc`、
  `FbReader.Text` 用 `ToStr`）、`Firebird/FirebirdConnection`（`recvExact`/`sendPacket`/
  `recvInt`）。**协议数据一律带显式长度**，不靠 NUL；OpenSSL/libpq/sqlite3/ODBC 的
  opaque handle 仍是 native 指针，没有动。
* `Sdk/Wechat`：`WXBizMsgCrypt` 的 `aesIv` 字段、CBC 加解密缓冲与
  `CbcEncryptNoPad`/`CbcDecryptNoPad → byte[]`，`BytesToString` 直接 `ToStr`；
  `TenPay/WechatPayV2.DecodeRefundReqInfo` 的明文缓冲。
* `Security/Cryptography/RsaKey`：`modulus`/`publicExponent`/`privateExponent`
  字段与 `ReadIntegerSafe → byte[]`（`Rsa.*` 早就收 `byte[]`）。
* `Gui/Text.ByteChar`：`calloc`+`memset` → `new byte[2]` + `ToStr(0, 1)`
  （连 `memset` 这个 extern 一起删）。
* `Game/Rts/Formats`：`Csf.ReadAscii`/`ReadValue`（UTF-8 转码缓冲 → `ToStr`）、
  `Shp.DecodeFrame → byte[]`、`Tmp.DecodeTile → byte[]`；`Game/Zgm/NetRuntime` 的
  `ZgmMessage.Encode() → byte[]`、`Decode` 的 body 缓冲、两个 runtime 的 `recvBuf`。
  调用方同步：`examples/game/ra2` 的 `Shp`/`Tmp`(含 `DecodeExtra`)/`Theater`
  （删掉 `Theater.free`）、`tests/conformance/rts_{shp,tmp}_parse`、`zgm_net_runtime`。
* `System/Windows/Forms.zan` 与 `Windows/Forms/Forms.zan`（两份同源）：
  `GetWindowTextA` 出参缓冲按返回长度 `ToStr`，消息循环的 `MSG` 缓冲 → `byte[]`。

〔实测〕全量 `ctest -j4` **711/711 Passed**（159 s）；分组
`sqlserver|tds|postgres|pg_` 9/9、`firebird` 3/3、`mysql` 6/6、
`wechat|rsa|gui_text` 19/19、`rts_` 15/15、`zgm` 84/84 全绿；
`examples/db/mysql_crud.zan`(52 files)、`examples/db/odbc_multidb.zan`(29)、
RA2 demo(76 files) 编译通过。

**B2-1 第十一批 ✅ 已完成（2026-07-29）：`src/ide_zan` + RA2 示例 —— 生产代码里
最后一批 `calloc`-as-string。**

* `src/ide_zan/LspSession` / `DebugSession`：`WSAStartup` 的 WSADATA、`sockaddr_in`
  （`BuildAddr → byte[]`）、`FIONBIO` 的 flag、`SO_RCVTIMEO` 的 DWORD/`timeval`、
  `recv` 缓冲全部 `byte[]`；收到的字节按 `recv` 返回值 `ToStr(0, n)`。
  `ZanIDE.ExePath`：路径缓冲 `byte[]`，按 `GetModuleFileNameA`/`readlink` 的返回
  长度截断（macOS 的 `_NSGetExecutablePath` 不报长度，改为定位 NUL）。
  〔实测〕`scripts/build_ide.ps1` → `IDE_BUILD_OK`；`ctest -j4` 711/711 Passed。
* `examples/game/ra2`：`Format80.Decompress`/`Lzo.Decompress`/`MapPack.Decode`/
  `DecodeChunks` → `byte[]`（`DecompressInto` 的 `dest` 形参同改）、`MapFile` 不再
  `free` 解压结果、`Pcx` 的 `indices`/`palette` 字段、`Texture.pixels`/`Pixels()`、
  `WsKey.ModulusBytes`/`Decrypt → byte[]`（`Mix` 侧删掉 `free(key)`）、`Csf` 的
  ASCII/UTF-16→UTF-8 缓冲；7 个 tests 的合成缓冲同步。
  〔实测〕RA2 demo 76 files 编译通过；`examples/game/ra2/tests/run.sh`
  **11 passed, 1 failed** —— 失败的只有既有的 `mix` 哈希差异（见上文 B2-1 第八批
  的记录：新旧两版 `CharOf` 逐字相同，`expected_mix.out` 本就对不上，用户已明确
  「RA2 先不管」）。

* **B2-1 ✅ 已完成（2026-07-29，第十一批收尾）** `extern string calloc(...)`：**stdlib / src / examples 均已清零**，
  只剩 `tests/` 里少数刻意保留的 native-ABI 用例（`ffi_free_consumes_string` 专测
  `free` 消费 string 的语义，`firebird_wire`/`mysql_stmt_binary`/`odbc_buffers`/
  `sqlserver_tds`/`rts_*`/`zandb_p6`/`selfhost/prog1` 等自建 C 缓冲的测试）
  （第二批消掉 Sha1 / Sha256 / Sha512 / Md5 / Sm3 / Hmac / Hex / Base64 八个，
  第三批 `Hkdf`，第四批 `Rsa`，第五批 `Sm2`，第六批 `Sm4`，第七批 `BigInt`）。
  **`Cryptography` 目录整条 `calloc` 链清完**（`Rsa` 也无 `free` 了）。剩下的都在
  `Net`（TcpClient / Socket / UdpClient / WebSocket / TlsStream / Mqtt）、
  `Data`（几乎每个驱动）、`IO`（File / Directory / Path）。
  按"一条调用链一批"继续：改一个类的内部缓冲很容易，难的是它的返回值
  被谁 `free`——所以每批都要连调用方一起改（Aes、摘要链、Hkdf、Rsa、Sm2、Sm4、
  BigInt 即是范例）。

〔已实测〕**这个惯用法是有意设计、有 conformance 覆盖的，不是内存安全 bug。**
`tests/conformance/ffi_free_consumes_string.zan` 专门测它：`calloc` 返回的 buffer
可以 `buf[0] = 65` 直接写，`free(buf)` 会消费它并把变量（含字段别名）置 null。
实测 `Path.Combine` / `GetFileName` / `GetExtension`，并对结果串做
`Substring` / `IndexOf` / 拼接 / 当 Dictionary key / 200 次循环 `--check-leaks`，
全部正确、`Length` 正确、无泄漏报告。**所以这是提炼问题不是安全问题。**

* **B2-2 ✅ 已完成（2026-07-29）：`stdlib/System/IO/Path.zan` 全部脱离手动缓冲。**
  复核发现字符串操作方法（`GetFileName` / `GetExtension` / `GetDirectoryName` /
  `ChangeExtension` / `Normalize` …）此前已用 Zan 的 `LastIndexOf` / `Substring`
  重写过（见 `path_api.zan` 注释「irgen used to lower these calls itself」），
  唯一残留 `calloc` 是 `GetTempPath` 的 Windows 分支：
  `string buf = calloc(4096,1); WinGetTempPath(4096, buf); return buf;`
  ——OS 把路径写进 buffer，返回时靠 strlen 截断、且 buffer 从不 `free`（既有泄漏）。
  改为 `byte[] buf = new byte[4096]; int n = WinGetTempPath(4096, buf);
  return buf.ToStr(0, n);`——按 API 返回的字符数精确截断，不再泄漏。
  **删掉 `Path` 的 `extern string calloc`，`Path` 从此零手动分配。**
  〔已实测〕重刷 stamp 后 `path_api` conformance/determinism/leakcheck **3/3 全
  Passed 无泄漏**；`GetTempPath` 无测试覆盖，用 `_scratch/temp_check.zan` 直测得
  `C:\Users\QQ\AppData\Local\Temp\`（`Length=31`、无尾部 NUL 垃圾），scratch 即删。
  `extern string calloc` 文件数 41 → **40**。

  **「同类 Directory/Process」改并入 B2-3**：`Directory.zan` / `Process.zan` 的残留
  `calloc` 不是纯字符串处理，而是宽字符互操作（`winWide`/`winUtf8` 的 UTF-16↔UTF-8
  转换缓冲、`WIN32_FIND_DATAW` 结构缓冲）和 `strcpy`/`strcat`/`strlen` 拼接，
  与 B2-3 的字符串原语替换深度耦合、无法当作「纯 Zan string API 重写」单独抽出，
  故随 B2-3 一起处理（届时 `winWide`/`GetCurrentDirectory`/`winList` 的 OS 写入
  缓冲同样按 `byte[]`+`ToStr` 收口）。
* **B2-3 ✅ 已完成（2026-07-29）：字符串原语 + `atoi`/`atof` 全部换成 Zan 实现，
  Directory/Process 宽字符互操作按 `byte[]`+`ToStr` 收口。**

  **数值解析（`atoi`/`atof` → 纯 Zan）**：以 `Encoding.ParseInt` / `Encoding.ParseDouble`
  为唯一实现（原本只是 `atoi`/`atof` 的薄包装），改写成纯 Zan 解析（按 C `atoi`/`atof`
  语义：跳前导空白、可选符号、十进制数字；`ParseDouble` 另支持小数与 `e`/`E` 指数）。
  其余调用点全部改走这两个 helper 并删掉各自的 `extern`：
  `Json.zan`（`GetInt`/`GetDouble`，删 `atoi`/`atof`/**未用的 `strncpy`**，加
  `using System.Text`）、`DbResult.zan:GetDouble`（删 `atof`）、
  `Postgres/PostgresConnection.zan`（4 处 `atoi(PQcmdTuples)`，删 `atoi`）、
  `Platform/Windows.zan`（`atoi`/`atof` 是**无调用的死 import**，直接删）。
  `DbResult.GetInt` 仍用 `Convert.ToInt`（编译器 intrinsic，非本批要删的 `atoi` extern），
  不在本批范围，保留。
  〔实测〕`_scratch/b23_check.zan`：`ParseInt("  -17abc")=-17`、`ParseInt("+9")=9`、
  `ParseInt("")=0`、`ParseDouble("-2.5e3")=-2500`、`ParseDouble("1.5E-2")=0.015`，与
  atoi/atof 一致。

  **字符串原语 + 宽字符缓冲（Directory / Process）**：
  - `Process.zan`：`WinSpawn` 的 `cmdline` 从 `calloc`+`strcpy`+`strcat` 改成 Zan 拼接
    `"cmd.exe /c " + command`；`STARTUPINFOA`/`PROCESS_INFORMATION`/退出码等结构缓冲
    从 `calloc string` 改 `byte[]`（OS 直接写入，`si[0]=104` 取代 `memset`）；
    `WinCapture` 的临时目录改调已 Zan 化的 `Path.GetTempPath()`（顺带去重、消掉一处
    从不 free 的泄漏）；POSIX `RunCapture` 读缓冲改 `byte[]`+`ToStr(0,n)`。
    **`Process` 从此零 `calloc`/`free`**，删掉 `strcpy`/`strcat`/`strlen`/`memset`/
    `WinGetTempPath`/`calloc`/`free` 七个 import。
  - `Directory.zan`：`winWide` 返回 `byte[]`（UTF-16 缓冲）、`winUtf8` 内部改 `byte[]`
    并按 `count-1` 精确 `ToStr`（消掉之前 winUtf8 结果从不 free 的泄漏）、
    `GetCurrentDirectory` 的 Windows 宽缓冲与 POSIX `getcwd` 缓冲都改 `byte[]`
    （POSIX 手动扫 NUL 求长度）；`winList` 的 `pattern` 与 POSIX 探测路径 `full`
    改成 Zan 拼接（`path + "\\*"` / `path + "/" + name`），删 `strcpy`/`strcat`/`strlen`。
    **唯一保留的 `calloc` 是 `WIN32_FIND_DATAW` 结构缓冲**（592 字节、`FindFirstFileW`
    直接写入的原生结构，非文本，`winName` 靠字节偏移 `Substring(44,520)` 取 `cFileName`）
    ——这属于 B2-4「真结构互操作」范畴，留给结构布局能力就绪后再处理，故 `Directory`
    仍持 `calloc`/`free` 两个 import。
  〔实测〕重刷 stamp 后 `crossplat_stdlib`/`io_errors`/`json_entity_mapping`/`json_errors`/
  `encoding_base64`/`ws_accept`/`db_null_distinct`/`db_pool`/`pg_params_syntax`
  三档共 **27/27 全 Passed 无泄漏**；`_scratch/b23_check.zan` 直测
  `Directory.GetCurrentDirectory/GetFiles/GetDirectories` 与
  `Process.RunCapture("echo …")` 全部正确、`--check-leaks` 无报告。
  `extern string calloc` 文件数（stdlib）40 → **39**（`Process` 出表，`Directory` 因
  结构缓冲仍在表）。
* **B2-4 ✅ 已完成（2026-07-29）：`[DllImport("crt")]` 全库盘点 + 分类，删掉死 import。**
  实测全库 `[DllImport("crt")]` 共 **322 条**（不是先前估的 207），按用途分四类：

  1. **真 libc / OS 原语（保留）**——无法用 Zan 表达、长期保留：
     文件 I/O（`fopen`/`fclose`/`fread`/`fwrite`/`fputs`/`fgets`/`fgetc`/`fputc`/`fseek`/
     `ftell`/`fflush`/`remove`/`rename`/`unlink`/`open`/`close`/`read`/`write`/`lseek`/
     `pread`/`pwrite`/`fsync`/`_commit`/`_flushall`/`ftruncate`/`_locking`/`flock`/`ioctl`/
     `fcntl`/`readlink`/`getcwd`/`chdir`/`mkdir`/`rmdir`/`opendir`/`closedir`/`readdir` 及
     `_read`/`_write`/`_lseeki64`/`_fileno` 等 Windows 变体）、进程（`system`/`popen`/
     `pclose`/`_popen`/`_pclose`/`getpid`/`exit`/`kill`/`sleep`/`usleep`/`ptrace`/`getuid`）、
     环境与时间（`getenv`/`time`/`gettimeofday`/`clock_gettime`）、套接字（`socket`/`bind`/
     `listen`/`accept`/`connect`/`send`/`recv`/`sendto`/`recvfrom`/`setsockopt`/`shutdown`/
     `htons`/`htonl`/`ntohs`/`inet_addr`/`gethostbyname`）、线程同步原语（`pthread_mutex_*`/
     `sem_*`/`dispatch_*`）、控制台（`puts`）。
  2. **Zan C 运行时自有导出（保留，非 libc）**——`zan_file_*`/`zan_io_socket_*`/
     `zan_shared_table_*`/`zan_atomic_int_*`/`zan_dispatch_*`/`zan_gate_*`/`zan_thread_start`/
     `zan_monotonic_us`。它们是运行时导出，标 `"crt"` 只因与运行时同库链接；属运行时职责，
     保留（library 名标注更准确的问题留给 ABI/文档层，不在本批）。
  3. **`NativeMemory` intrinsic（已收编，勿散用）**——`NativeMemory.{Alloc,Free,Copy,Fill,
     Compare,GetString,PutString,Crc32}`，文件头已注明「每个方法都是编译器 intrinsic，irgen
     直接 lower 成单条 libc 调用，named library 从不真正链接」，是 raw off-heap 内存的规范入口。
     散落各文件的裸 `calloc`/`free`/`memcpy`/`memset`/`memchr` extern 属**应逐步收编到
     `NativeMemory` 或 `byte[]`/ARC** 的一类（与 B2-1 的 calloc→byte[] 主线同源）。
  4. **应 Zan 化的字符串/数值（B2-3 已删）**——`strcpy`/`strcat`/`strncpy`/`strrchr`/`atoi`/
     `atof` 全部已删；仅剩 `strlen`（9 处），用于取 null-terminated FFI 串或 Zan 串的**字节
     长度**（`Encoding.GetByteCount` 等），与 `.Length`（字符数）语义不同，合理保留、逐处再议。

  **本批安全动作**：删掉 9 个「只声明、全库从未调用」的死 crt import——
  `File.zan` `fgetc`/`fgets`、`Socket.zan` `ioctl`/`memcpy`、`TcpClient.zan` `free`、
  `UdpClient.zan` `calloc`、`PageFile.zan` `TruncateFd`(ftruncate)、
  `Json.zan` `calloc`/`free`（B2-3 删 `strncpy` 后遗留的孤儿）。
  〔实测〕重刷 stamp 后 `crossplat_stdlib`/`io_errors`/`file_info`/`json`×2/`socket_ready`/
  `socket_handle_nint`/`net_errors`/`redis_client`/`sse_stream`/`http`×2/`async socket`×2/
  `zandb_p0..p6` 三档共 **60+ 用例全 Passed 无泄漏**。
  `[DllImport("crt")]` 总数 322 → **313**；`extern string calloc` 文件数 39 → **37**。

  **剩余（后续/依赖能力）**：第 3 类的 68 处 `calloc` / 48 处 `free` / `memcpy`·`memset`·
  `memchr` 收编到 `NativeMemory` 或 `byte[]` 是 B2-1 主线的长尾，逐文件按所有权判定推进
  （部分裸缓冲是真 FFI 边界需保留，如 `Directory` 的 `WIN32_FIND_DATAW`）；第 2 类的
  运行时 library 名标注、`strlen` 逐处复核留待各自专项。
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

* **B3-1** ✅ 已完成（新增泛型 `PoolCore<T>`，`stdlib/System/Data/PoolCore.zan`）。
  7 个驱动池（`DbPool` / `FirebirdPool` / `MySqlPool` / `PgPool` / `SqlitePool` /
  `SqlServerPool` / `TDenginePool`）曾逻辑完全一致（懒增长到 maxSize、`Gate` 事件
  驱动等待、死连接剔除，字段与注释复制 7 遍）。现在共享簿记（free list、
  live/waiting 计数、唤醒 gate、closed 标志）收敛到 `PoolCore<T>`，各驱动池只保留
  自己的 endpoint 参数和开连接方式并 delegate 给 core，各缩到 120~154 行。
  `PoolCore` 从不碰连接本身（既不探活也不关闭），因而对任意连接类型（含接口
  类型的 `DbPool`）都适用；驻留仍留在驱动池里（协作式单线程调度，free list 只在
  await 点之间访问，无需锁）。前置 A7-1（实例泛型方法单态化）已解除。
* **B3-2 🚧 进行中（大重构，逐方法拆、每步跑对应子集测试）。**
  〔2026-07-29 重新盘点〕全库 `.zan`（stdlib + selfhost + ide_zan）实测 **30 个**方法 >150 行
  （旧条目「23 个/最长 1536 行 DataTable.Render」已过时——`DataTable.Render` 早已拆到 <150 行）。
  当前实际最长（Top 前列）：
  - `src/ide_zan/ZanIDE.zan:1029 Main()` **3745**（IDE 主循环 + 命令分发，最应拆）
  - `Gui/Widget/CodeEditor.Render.zan:229 Render()` 823（→ B3-3 一起）
  - `Gui/Widget/FilePicker.zan:492 RenderContent()` 561
  - `Gui/Event.zan:991 RenderThemeDrawer()` 467
  - `src/ide_zan/SceneDesigner.zan:1163 RenderCanvas()` 434
  - `src/selfhost/irgen_expr.zan:1931 GenCall()` 377 等（selfhost 属编译器域，拆需保自举 gen2==gen3，谨慎）
  完整清单见 `_scratch/scan_methods.ps1` 的扫描输出。按「先纯 GUI、后 IDE、最后 selfhost」推进，
  每个拆分**行为等价**（提取子方法/按阶段分组），每步跑对应 conformance 子集。
  - **✅ 首拆 `Gui/StyleSheet.zan:Decl`**（175 行的扁平 CSS 属性 if 链）→ 保留原分组注释，
    按组抽出 `DeclFill`/`DeclText`/`DeclBorderBox`/`DeclBoxMetrics`/`DeclLayout`/`DeclMotion`
    六个 `bool` 辅助方法，`Decl` 变为顺序链（各组 key 互斥、首个命中即 return，行为等价）；
    `Decl` 从 175 行降到 11 行。`conformance_gui_css` 1/1 Passed。
  - **✅ `Gui/Widget/DataTable.Layout.zan:LoadLayout`**（153 行）→ 按 JSON 段抽 7 个静态
    辅助（`LoadColumnState`/`LoadColumnOrder`/`LoadSortKeys`/`LoadGrouping`/`LoadCollapsedBands`/
    `LoadExpandedRows`/`LoadViewToggles`），主方法降到 ~20 行；验证/EnsureLayoutSlots/dirty 顺序不变。
    `conformance_gui_datatable` 1/1 Passed（+css/codeeditor 3/3）。
  - **✅ `Gui/Widget/CodeEditor.Intelli.zan:HandleInput`**（215 行）→ 按事件类型拆
    `HandleCharKey`（kind==6：字符/Ctrl 快捷/编辑键）与 `HandleNavKey`（kind==4：导航/选择/多光标），
    `HandleInput` 变为 2 分支派发。`conformance_gui_codeeditor` 1/1 Passed。
  - **✅ `Game/Arpg/Project.zan:Validate`**（243 行）→ 10 个独立集合校验循环各抽为
    `ValidateMaps`/`ValidateActors`/`ValidateItems`/`ValidateSkills`/`ValidateBuffs`/`ValidateWindows`/
    `ValidateGrowth`/`ValidatePrefabs`/`ValidateMapPortals`/`ValidateDefaultPlayer`；`Validate` 变为顺序调用。
    `arpg_ui_runtime` 三档 3/3 Passed。
  - **✅ `Game/Zgm/Project.zan:Validate`**（214 行）→ 6 个段抽为 `ValidateApp`/`ValidateMaps`/
    `ValidateRoles`/`ValidateEquipSets`/`ValidateSkills`/`ValidateWindows`。
    `zgm_project_validation` 三档 3/3 Passed。
  - **🚧 `Gui/Widget/CodeEditor.Render.zan:Render`**（823 行，部分）→ 已抽出独立的自动补全弹窗段
    （补全列表 + kind 过滤条 + 描述面板，167 行）为 `RenderAcPopup(...)`；Render 降到 ~656 行。
    余下的点击/悬停事件段含 `return id;` 提前返回（与主渲染控制流耦合），需转成
    「helper 返回 bool、Render 据此 return」的信号式改写——这部分随 **B3-3** CodeEditor 模块重划一并做。
    `conformance_gui_codeeditor` 1/1 Passed。
  - **✅ `Gui/Widget/Tabs.zan:Render`**（192）→ 抽 `RenderTabStrip`（滚动视口内的每标签绘制+点击/右键菜单）、
    `RenderAddButton`（尾部“+”）、`RenderOverflowChevrons`（溢出左右箭头）；主方法降到 ~90 行。
  - **✅ `Gui/Widget/Layer.zan:RenderActive`**（174）→ 交互段含 `return;`（关闭）留在主方法，
    尾部 body/footer 绘制段抽为 `RenderLayerBody`；主方法降到 ~144 行。
  - **✅ `Gui/App.zan:RenderChrome`**（181）→ 标题栏按钮子系统（布局→hover→字形→hit region→点击，
    唯一外部输入 `c/t/W/hbar`）抽为 `RenderCaptionButtons`，返回 theme 按钮 focus id 供抽屉锚定；
    主方法降到 ~45 行。
  - **✅ `Gui/Event.zan:RenderThemeDrawer`**（467）→ 按抽屉分区拆：`DrawSkinGrid`/`DrawAccentRow`/
    `DrawOpacityChips`/`DrawWallpaperSection`（各推进并返回 `yy`）+ `DrawMotionTab` + `HandleDrawerInput`
    （点击/外部按下/Esc 派发）+ `DrawerContentH`（滚动内容高度）；共享列表构造抽为静态
    `DrawerAccents`/`DrawerOpLevels`/`DrawerKinds`。主方法降到 ~110 行。
  - 以上 Tabs/Layer/App/Event 四项：`cmake --build build` OK，`gui` 8 档（runtime_gui + 7 conformance）100% Passed。
    ⚠️ **验证方法修正**：`cmake --build build` 的 stdlib 预编是**惰性**的——未被任一 conformance
    引用到的 stdlib 方法不会深度编译，`RenderThemeDrawer` 拆分中一个 `i = 0`（应为 `int i = 0`）
    的错误 cmake 与 gui conformance 全都没抓到，是 `scripts\build_ide.ps1`（深度编译整个 Gui）
    抓到的。**此后 Gui widget 重构一律以 build_ide.ps1（`IDE_BUILD_OK`）为准**，conformance 仅作补充。
  - **✅ `views/DocsPage.zan:BuildDocs`**（225，ide_zan）→ 按内容分区拆 `BuildLanguageDocs`/
    `BuildIoAndBasicsDocs`/`BuildWidgetDocs`/`BuildProjectAndErrorDocs`，各自重算所需分类标签；
    条目顺序（决定侧栏分组与索引）完全保留。`IDE_BUILD_OK`。
  - **✅ selfhost 运行时发射器**（`src/selfhost/irgen.zan` + `irgen_async.zan`）——纯 `H("…")` IR
    文本发射、无共享局部量，按发射的 LLVM 函数分组拆：
    - `EmitListRuntime`（225）→ `EmitListRuntimeCore` + `EmitListRuntimeOps`
    - `EmitStrOpsRuntime`（291）→ `EmitStrOpsSearch` + `EmitStrOpsTransform` + `EmitStrOpsSplitJoin`
    - `EmitDictRuntime`（287）→ `EmitDictRuntimeCore` + `EmitDictRuntimeMutate` + `EmitDictRuntimeQuery`
    - `EmitAsyncRuntime`（282）→ `EmitAsyncUnwindClock` + `EmitAsyncPumpReady` + `EmitAsyncSchedRun`
    `selfhost_gen1` + `selfhost_fixed_point` 均 Passed（gen2==gen3 **字节一致**，行为等价确证）。
  - `IconVector.Draw`（232）为扁平几何/codepoint dispatch、共享大量局部量，强拆反降可读性，**不拆**。
  - **判定不机械拆（会割裂内聚逻辑或需引入状态对象/多值返回，留给 B3-3 / 专项编译器重构）**：
    - 紧耦合渲染/事件大方法（局部量/按钮 id/`action`/`return id` 跨绘制段与输入段流动，无对口 conformance）：
      `CodeEditor.Render` 658 尾段、`FilePicker.RenderContent` 561、`SceneDesigner.RenderCanvas` 434 /
      `RenderInspector` 173、`AssetManager.Render` 364、`Designer.Form.PreviewDisplay` 269、
      `App.ProcessEvent` 238 → **随 B3-3**。
    - selfhost 表达式代码生成 `irgen_expr.GenCall` 377 / `GenBinary` 216 / `GenLambda` 153、
      `irgen_async.GenAsyncMethod` 164：各算子分支共享 IR 构建器的寄存器/临时量计数状态，强拆到
      150 行以下要么割裂算子 dispatch、要么仍略超阈值——**留给专项编译器重构**。
    - 异步协议序列 `MySql.doConnect` 176 / `Firebird.authenticate` 161：单条状态机式握手流程，不拆。
  - 剩余最大件 `ZanIDE.Main` 3745（ide_zan 顶层事件/面板巨方法）需拆成 per-panel/per-ribbon 处理器，
    自成一项工程，单独推进。
* **B3-3 ✅ 已完成（2026-07-29）：重划 DataTable/CodeEditor 模块边界。**
  Zan 用 `partial class`，故重划=把两个巨文件按职责切成内聚 partial（`DataTable.Xxx.zan`），
  静态方法调用点 `DataTable.Xxx(...)` 不受影响；每步以 `build_ide.ps1`（`IDE_BUILD_OK`）为准，
  datatable conformance 兜底。
  - **DataTable ✅ 文件重划完成**：原 3 个巨文件（`DataTable.zan` 3525 行 143KB +
    `DataTable.Render.zan` 3345 行 167KB + `DataTable.Layout.zan`）→ 13 个内聚 partial：
    - 数据/逻辑：`DataTable.zan`（598，类头 + selection/clipboard/nav 核心）、`.Value`（值解析/格式/日期）、
      `.Edit`（单元格编辑）、`.Filter`（过滤谓词）、`.Rows`（行生命周期/undo）、`.Columns`（列模型/band/pin/freeze）、
      `.Compute`（set-pass/分组/主从/显示列/汇总/列统计）、`.Sort`（条件着色 + 排序）、`.Layout`（布局持久化，原有）。
    - 渲染/UI：`DataTable.Render.zan`（1864，单元格绘制 + 冻结几何 + 分组面板 + 主渲染 `RenderSource`）、
      `.Overlays`（右键菜单/图表/列头菜单）、`.FilterUI`（DevExpress 筛选弹窗 + set-filter 菜单）。
    - 结构体：`DataTableModel.zan`（原有）。全部 `IDE_BUILD_OK`、datatable conformance Passed。
  - **CodeEditor ✅**：本就已分 5 个内聚 partial（`.zan` 缓冲/编辑、`.Render` 渲染/高亮、`.Completion` 补全、
    `.Intelli` 签名/片段/输入、`.Symbols` 符号索引），无巨文件需重划。另将 `CodeEditor.Render` 中两个
    自足的 void 绘制段抽为 `RenderLines`（可见行窗口）+ `RenderOverviewRuler`（右缘概览尺），
    `Render` 由 660 行降到 ~510；`IDE_BUILD_OK`。
  - **`DataTable.RenderSource`（原 ~1537 行，全仓最长方法）→ 引入 `DataTableFrame` 几何结构体。**
    新增 `class DataTableFrame`（`DataTableModel.zan`，28 个 int/List<int> 几何字段：`vorder`/`colX`/
    `colBand`/`headerH`/`titleTop`/`firstDataX`/`bandX`/`bandW`/`pinnedW`/`pinnedRW`/`maxScrollX`/… ）与
    `DataTable.ComputeFrame(...)`——把渲染序言（EnsureInit/SyncBindings/Recompute + 表头/band 度量 +
    行号/选择槽宽 + 冻结带宽 + 逐列屏幕 x + 画分组面板）整段抽成一次算好、返回 frame 的纯序言方法。
    `RenderSource` 首行改为 `f = ComputeFrame(...)` 再 unpack 局部量，**主体 1400 行逐字未动**（零行为改动、
    编译兜底任何漏引用）。`IDE_BUILD_OK`、datatable conformance Passed。
    - 交互内核（约 700 行：`ptrOi`/`ptrCol` 命中 + `ek` 事件逐段消费的状态机 + 键盘导航/编辑/拖拽/框选）
      是**单一事件状态机**（`ek` 被各段递进置 0、early-return 敏感、命中区 AllocId 顺序敏感），
      机械再拆多个 helper 需按引用穿 `ek` 且极易引入隐性回归，故**按设计保留为一个方法**，不强拆。
    - 尾部若干独立绘制段（summary/status/scrollbar/wheel，纯绘制 + 自持 AllocId）可后续按需再抽为
      `RenderXxx(f, …)`，属可选收尾。
    - **可视/交互回归验证 ✅**：编译 `gui_gallery` 示例，在远程 Windows 桌面实际启动并逐帧截图——
      (1) 1M 行 DataTable 完整渲染（表头/行号/复选框/70 列数据/summary Σ/avg/min/max/横向滚动条）无异常；
      (2) 列头右键菜单（Sort/Freeze/Group/Filter/Min/Max/Count/Chart）弹出定位正确；
      (3) Escape 关闭模态覆盖（modal early-return 路径）；
      (4) 鼠标点击单元格→行选中高亮正确移动（pointer hit + selection 路径）；
      (5) 图表叠加层（Chart · Col 7 柱状图）渲染正确（Overlays partial 提取后的完整路径）；
      (6) 应用全程 Responding=True，无崩溃/挂起。
      结合 `IDE_BUILD_OK` + `datatable conformance Passed`，判定无回归。
* **B3-4 ✅ 已完成（2026-07-29）：提炼 widget 公共绘制层。**
  重复的“填充圆角矩形 + 同几何描边”惯用法（`c.FillRoundRect(...)` 紧跟
  `c.DrawRoundRect(...)`）全库共 32 处。新增共享原语
  `Canvas.SurfaceRoundRect(x, y, w, h, radius, fill, border, borderThickness)`
  （Render.zan，紧邻 DrawRoundRect），把 **31 处**几何完全一致的 pair 收敛成单次调用；
  FloatButton 那处描边是条件分支（`if (type != 1)`），保留不动。
  涉及 14 个控件（Chart/ChartView/CodeEditor.Render/CodeEditor/Dock/Dropdown/Layer/
  ListView/Pagination/Panel/Popover/TextArea/Tooltip/VirtualList）。
  **落点说明（偏离原计划的 `Theme`）**：这些调用点的 fill/border/radius 各不相同、
  且多处用 `pal.*`（CodeEditor）或自定义 alpha（ChartView 箱线图），并非主题默认值，
  放数据类 `Theme` 上不合适；`Canvas` 才是 FillRoundRect/DrawRoundRect 所在层，
  其 DrawRoundRect 文档本就写“hug a FillRoundRect of the same geometry”，合并原语是自然延伸。
  实测：`conformance_gui_*` 7/7 + `zgm_widget`（conformance/determinism/leakcheck）6/6
  全 Passed 无泄漏；stdlib 重编无 error。

## B4 平台与错误处理

* **B4-1 ✅ 已完成（2026-07-29）：删掉两个占位级绑定桩，保留完整的 `Platform.Runtime`。**
  `stdlib/Platform` 原有三文件：`Windows.zan`（`class Win32`：MessageBox/GetTickCount/…）、
  `Posix.zan`（`class Posix`：open/read/write/…）、`Runtime.zan`（`class Runtime`：平台探测
  GetPlatform/IsWindows/IsLinux/IsMacOS/GetProcessId，`#if` 编译期定向）。全库盘点确认
  **没有任何代码引用 `Win32.` 或 `Posix.`**——各模块（Directory/Process/File/Socket…）都在
  自己文件里 `#if WINDOWS` 直接绑所需 API，这两个类是从没被用过的重复绑定桩（其 externs 恰是
  B2-4 死 import 扫描里 `Platform` 那批）。按「要么充实、要么删掉别留半成品」及「最佳实现、不为
  兼容老代码让步」——**`git rm` 删掉 `Windows.zan` / `Posix.zan`**（后者顺带清掉 B2-3 里
  `Windows.zan` 的死 `atoi`/`atof`，现整文件移除）。`Runtime.zan` 是实现完整、语义正确的公共
  探测工具（非半成品，只是暂无内部调用方，属正常公共 API），**保留**。删文件后 `cmake -B build`
  重新配置刷新 `GLOB_RECURSE`（否则 ninja 报缺失依赖）、重编工具链，`io_errors`/`path_api`/
  `regex` 三档 **9/9 全 Passed 无泄漏**。
* **B4-2 ✅ 约定确立 + 全库排查完成（2026-07-29）：按 C# 语义收敛，异常面已达标。**
  统一约定（对齐 C#，非机械改写）：
  (1) **真·错误**（打不开文件、连接失败、协议破坏、解析非法）→ 抛异常
      （`FileNotFoundException`/`IOException`/`ArgumentException`/`DbException`…）；
  (2) `Try*` 系（`TryParse`/`SysFile.Write:bool`…）→ 返回 `bool`/`null`，是合法契约，保留；
  (3) `IndexOf→-1`、`Find→null`、`Exists→false` 等 **C# 本就用哨兵**的 API → 保留，不动。
  〔全库排查〕stdlib 共 **20 处 catch**，逐个核对后**无一处真吞错误**——唯一疑似
  （`Gui/Native.zan` `SysFile.Write`）是文档明写「Returns true on success」的 bool-result
  Try-API，属约定 (2)，保留。IO 层（`System/IO/File.zan`）已全部在失败处 `throw`
  （`ReadAllText`/`WriteAllText`/`Copy`/`GetSize`/`ReadBytes`… 均抛 `FileNotFoundException`/
  `IOException`/`ArgumentException`），`Exists` 返回 `false` 属约定 (3)。DB 层此前 B3-0 已转
  `DbException`。结论：异常约定已贯穿、达标；**未机械改写约 700 处合法哨兵返回**
  （用户明确要求「留合法哨兵值」）。后续若在 B1 大模块逐文件走查时发现个别真吞错误点，
  就地按约定 (1) 转抛异常并补测。
* **B4-3 ✅ 已完成（2026-07-29）：核实结论——不存在两套并存，命名空间正确，无需动。**
  正则实现就在 `stdlib/System/Text/RegularExpressions/`（`Match.zan` 2.7KB /
  `Regex.zan` 9.2KB / `RegexProgram.zan` 26.3KB，合计 ~38KB=先前所说「37KB/3 文件」），
  `namespace System.Text.RegularExpressions`（标准 C# 命名空间），**没有**独立的顶层
  `stdlib/RegularExpressions` 目录（顶层只有 `System`/`Game`/`Gui`/`Platform`/`Sdk`/`SDL3`），
  也没有第二份并存实现；全库唯一引用点 `tests/conformance/regex.zan` 用的正是
  `using System.Text.RegularExpressions;`。先前条目描述（「`System/Text` 无 Regex」「顶层
  `stdlib/RegularExpressions`」）是过时信息。〔实测〕`regex` conformance/determinism/leakcheck
  **3/3 全 Passed 无泄漏**。

## B5 GUI 迁移（在 A 项能力就绪后穿插）

判定边界不是"碰不碰 OS"：`Win32Shell.zan` 已证明平坦 C ABI 的 user32/gdi32
能在 Zan 里重写（连 WndProc 都是 delegate）。真正的阻塞是 union / 宏 API /
header-only 库 / C 回调 / 结构体字段偏移，全部对应 A2 / A3 / A4。

* **B5-1**（未开始，〔A0+A1 已就绪，可以动手〕；2026-07-29 核实 `gui_runtime.c`
  仍有 **28 个导出** / 54KB）光栅器搬 Zan：`gui_runtime.c` 的 19 个导出
  （surface / clip / 全部图元 / blur / snapshot）。**关键一刀是 surface 所有权**——
  改由 Zan 持有后，present 签名从 `(hwnd, surface_id)` 改为 `(ptr, w, h, stride)`，
  `internal_surface_data/clip` 两个内部口子随之删除，其余全是跟随项。
  动手前先给 `blur_rect` / `fill_radial` / `blit_image` 做基准，
  它们是玻璃主题下单帧最贵的循环。
  - **✅ 阶段 0 基准已做（2026-07-29）**：`_scratch/b51_bench.zan`（`zanc` 编译，
    1600×900 surface，QueryPerformanceCounter 计时）。**C 版基线**：
    `blur_rect` 800×500 r=24 = **3916 µs/op**（20 次）、
    `fill_radial` r=300 = **1469 µs/op**（200 次）、
    `blit_image` 256×256→512×512 = **623 µs/op**（200 次，解码已缓存）、
    参考项 `fill_rect` 300×200 = **5 µs/op**（20000 次）。
    Zan 版验收线（≥0.9x）：blur ≤ 4351、radial ≤ 1632、blit ≤ 692 µs/op。
  - **移植的真实边界**（读代码后确认，排期要按这个来）：`gui_runtime.c` 的
    surface 表还被 **C 文字/字形渲染**（`gui_runtime_text.c:167`、
    `gui_runtime_font.c:150/269` 直接取 `g_surfaces[id]` 并用 `set_pixel`/clip）
    和 **SDL / X11 / Cocoa present** 共用，`zan_gui_internal_surface_data/clip`
    就是给 Cocoa 那条路开的口子。所以「surface 所有权归 Zan」必须同一批改掉
    文字导出的签名（`surface_id` → `ptr,w,h,stride,clip*`）与三条 present 路径；
    `blit_image` 还挂着 C 里的 stb_image 解码缓存（解码留 C，只搬采样循环）。
    Windows present 已经只要 `pixels/w/h`（`Win32Shell.Blit`），不构成阻塞。
* **B5-2 🟡 大部分完成**（`067ec4e` 删掉 C Win32 shell 与 link-only shims）。
  〔2026-07-29 核实〕`gui_runtime_shims.c` 从 160 行降到 3.5KB，里面**只剩
  macOS 非-Cocoa（SDL windowing）构建的 22 个 WebView no-op 桩**（`#if defined(__APPLE__)
  && !defined(ZAN_GUI_COCOA)`）。Windows 已改由 Zan 直驱 WebView2 COM（`c168ffc`，
  `stdlib/Gui/WebView2.zan`），其余平台走 `WebViewBackend` 自带兜底；`write_file`
  已不在 shim 里。**剩余**：macOS 那条分支要么接真 WKWebView、要么让 Zan 侧 `#if` 兜底后整文件删掉。
* **B5-3 ✅ 已完成**（`406c451` gui: draw icons in Zan as vector primitives）。
  〔核实〕`draw_icon` 在 `src/runtime/*.c` 里已**零匹配**。
* **B5-4 ✅ 已完成**（`067ec4e`）删 `gui_runtime_text.c` 的 Win32 窗口壳。
  〔核实〕`gui_runtime_text.c` 现在 11.9KB、**只剩 3 个文字导出**（`draw_text` /
  `measure_text` / `font_height`），文件头已注明「窗口类/WndProc/事件队列/present/
  剪贴板/IME/组合玻璃住在 `stdlib/Gui/Win32Shell.zan`」；`Win32Shell.zan` 里
  clipboard / ime_pos / write_file 共 26 处实现。
* **B5-5** 〔A2 后〕`X11Shell.zan`：X11 窗口管理导出 **已从 `gui_runtime_font.c` 离开**
  （该文件现在 `XOpenDisplay`/`XCreateWindow` 零匹配，结构债已修），但落在新的
  `src/runtime/gui_runtime_x11.c`（30KB / **25 个导出**），**仍是 C，本条未完成**。
  注意 `DefaultScreen` / `DefaultVisual` / `DefaultDepth` / `RootWindow` 是宏，
  用函数版 `XDefaultScreen` / `XDefaultVisual` / `XDefaultDepth` / `XRootWindow` 代替，
  这一条不构成阻塞。
* **B5-6** 〔A2+A3 后〕SDL 后端（window registry / event queue / dirty rect /
  texture 上传）改由 Zan 组织，C facade 退回纯绑定；Cocoa 后端
  （`objc_allocateClassPair` + `class_addMethod` 造类）。
* **B5-7** 终态：删除 `zan_gui` 和 `zan_sdl3` 两个自建垫片。
  〔2026-07-29 重新计数（stdlib+src+examples 的 `[DllImport("x")]`）〕`zan_gui` **108**、
  `zan_sdl3` **78**——未减少，本条未开始（依赖 B5-1 / B5-5 / B5-6）。
  `user32`(现 56) / `kernel32`(现 9) / `gdi32`(现 30) / `crt`(现 90) / `odbc` / `ws2_32` /
  `imm32` / `dwmapi` / `psapi` 这些直接绑 OS 的**是正确形态，不动**。

## B6 工具链 Zan 化 〔依赖 A3 + A6〕

`src/` 下非 compiler 的 C。〔2026-07-29 重新度量〕仍是 C 的共 **约 230KB**：
`lsp/intellisense.c` 84KB、`lsp/lsp_main.c` 67KB、`dap/debugger.c` 41KB、
`dap/dap_main.c` 28KB、`common/json.c` 15KB、`common/rpc.c` 3KB。
（`doc/zandoc.c` / `fmt/zanfmt.c` 已是 Zan；`pkg/zanpkg_main.c` 随包管理器整体删除。）
按规则这些既不是 compiler 也不是 runtime，全都该是 Zan。

进度：
* `zandoc` / `zanfmt` 已是 Zan（`src/doc` / `src/fmt` 由 `zanc` 从 `.zan` 源构建）。
* **包管理器 `src/pkg` 已整体移除**（2026-07-28，`ae75d50`）：`src/pkg/zanpkg.zan`、
  CMake 的 `zanpkg` 目标、`tool_zanpkg` 测试与 `tests/run_zanpkg.cmake` 一并删除。
  `release.yml`、`docs/{RELEASE,ROADMAP,PRODUCTION_PLAN,IDE_PUBLISH}.md`、
  `scripts/{package_bundle,publish_ide}` 里对 zanpkg CLI 的引用也已全部清除；包管理
  不再走独立 CLI 这套模式（IDE 内包商店的分发机制另定）。注意 `scripts/pkg_stub.c`、
  `scripts/pack_single.ps1`、`scripts/package_games.ps1` 里的 `ZANPKG1` 是**自包含
  单 exe 的封包尾部魔数**，与包管理器无关，保留。
* 仍在 C：`lsp/intellisense.c`、`lsp/lsp_main.c`、`dap/debugger.c`、`dap/dap_main.c`、
  `common/json.c`、`common/rpc.c`（依赖 A6 编译器对外 API）。
* 另有 `src/selfhost/`（lexer/parser/binder/checker/irgen*/main 等一整套 `.zan`）是
  **自举编译器**分支，属 A6 的"终局自举"路线，独立于上面这批工具链迁移在推进。

## B7 runtime 边界复核

〔2026-07-29 实测大小〕`rt_io.c` **77KB**、`rt_sync.c` 49KB、`rt_sched.c` 10KB /
`rt_mem.c` 7.6KB / `rt_co.c` 2.3KB / `rt_crash.h` 5KB / `rt_wasm.c` 0.8KB。真 reactor / 调度 / 同步原语 / 内存分配留 C 没问题，
但要逐个过一遍：如果混了 HTTP 解析、编码转换、路径处理这类纯逻辑，上移到 Zan。

---

# C. 文档

`docs/` 40 份。全部在最近 3 周内动过，所以问题**不是日历陈旧，是内容与实现漂移**。

* **C1 ✅ 已完成（2026-07-29）：`ABI.md` 已按当前实现重写受影响小节 + 加分层横幅。**
  复核发现原清单**部分已过时**——实现已经演进（`[FieldOffset(n)]` 显式布局、
  SysV/Win64 的 `sret`/`byval` 分类都已在 `irgen_abi.c` 实装，非"不存在"）。
  按当前源码 + 探针核实并重写：§3.1 基元表（**`int` 实测 32 位**——探针
  `2147483647+1 → -2147483648` 环绕、`long` 不环绕；`float` 32 位、`double` 64 位、
  `char` 降 i64、无 `int32`/`float32` 类型）、§3.2 结构体布局（默认顺序=C 布局，
  `[StructLayout(Explicit)]`+`[FieldOffset(n)]` 显式布局/联合，显式布局禁虚方法）、
  §6 marshalling（删 `int32`/`float32` 行、修 `float`、`string` 返回值=**接管指针非深拷**）
  与 §6.2 结构体传参（按平台 SysV/Win64 分类）。§1.1 新增"当前实现 / 目标设计"分层：
  §3.3–3.5/§4/§5/§7 标为设计意图、本轮未复验。原始记录（已过时，供对照）：
  ~~`:38` float=8 字节 double；`:67` `[repr("C")]` 无布局控制代码；`:249` string 返回值拷贝；
  列不存在的 `int32`/`float32`；`:251/:254-260/:24` 结构体传参/sret 描述不存在的行为~~。
* **C2 🟡 部分完成（2026-07-29）：已修跨文档的确定性事实漂移。**
  `ABI.md` 见 C1。`DESIGN.md`（`int x=42 //64-bit` → 32-bit；`float //64-bit` → 32-bit）、
  `SPEC.md`（"整型字面量默认 int 64 位" / "L→long 与 int 同为 64 位" → int 32 位、long 64 位）、
  `ARCHITECTURE.md`（"single-pass" → multi-pass：binder 实测有 Pass 1/2/3 `resolve_bases`；
  词法器确为单遍流式，保留）已按实现改正。**剩余**：`CODING_STANDARDS.md` /
  `ERROR_CATALOG.md` / `IDE.md` / `SECURITY.md` 尚需逐份深核（未发现明显数值/结构性漂移，
  但未逐条走读）。
* **C3 ✅ 已完成（2026-07-29）：`STDLIB_ANALYSIS.md` `git mv` 进 `docs/archive/`。**
  这是早期能力盘点，结论大部分过时；当前覆盖面以 `stdlib/` 与 `docs/STDLIB.md` 为准。
  归档横幅重写为"已归档不再维护 + 更正（Regex 已存在，非"仍缺"）+ 唯一仍成立短板
  （失败静默/Result）在 B4-2 跟踪"，原正文保留。不再用横幅代替维护。
* **C4** ✅ 已删（2026-07-27）`STDLIB_DB_AARDIO_REF.md` 那 2 行只是一句
  "已被 `STDLIB_ZAN_DESIGN.md` 取代"，接替者本身已在开头写明这段历史，残桩删掉。
* **C5** ✅ 已完成。3 份 bug 文档（`await-in-catch-loop.md`、
  `dict-urldecode-key-corruption.md`、`throw-leaks-live-locals.md`）及各自的
  `repro_*.zan` 均已纳入 git 跟踪（配合 A8）。另外 `docs/bugs/` 又新增了
  `catch-no-type-matching.md`、`cross-module-base-class-field-binding.md`、
  `O0-list-index-write-stack-leak.md` 三份（含 repro），一并已入 git。
* **C6 ✅ 已完成（2026-07-29）：7 份 bug 文档统一 `**Status:**` 字段。**
  标题下第一行统一 `**Status:** Fixed (日期) — <task ref>`：await-in-catch-loop(A8-1)、
  dict-urldecode(A8-2)、throw-leaks(A8-3+A8-12)、catch-no-type-matching、cross-module
  （后两者已有 Resolution，补顶行）、O0-list-index、generics-uniform-repr（原 prose 转粗体，
  并补记"通过类型参数调用约束接口方法"的崩溃已在 A7-1 修复，非仍崩）。七份现全为 Fixed。
  格式规则写入 `docs/DOCS_MAINTENANCE.md`（见 C11）。
* **C7** ✅ 已归位（2026-07-27）两份 ra2-hd 文档移到项目文档层：
  `docs/projects/ra2-hd/2026-07-26-ra2-hd-plan.md` 和
  `.../2026-07-26-ra2-hd-design.md`（`git mv` 保留历史，计划里指向 spec 的
  链接同步改了），空掉的 `docs/superpowers/` 删除。语言规范层只剩
  `SPEC.md` / `ABI.md` 这类。
* **C8 ✅ 已完成（2026-07-29）：厘清 4 份路线文档边界，归档过时的那份。**
  逐份核实后判定：**不做「四合一大文件」**——`EXECUTION_PLAN`/`PRODUCTION_PLAN`/
  `SELF_CONTAINED_TOOLCHAIN` 三份是**当前有效且互补**的层次（顶层执行索引 → ZanIDE 生产
  可用详情 → 工具链技术设计），强行合并会丢技术细节、反而更乱；真正的问题是「有一份过时的
  重叠文档 + 三份关系没写明」。因此：
  (1) **归档 `ROADMAP.md`**（`git mv docs/ROADMAP.md docs/archive/ROADMAP.md` + 顶部横幅）——
      它是项目最初的里程碑计划（M1 最小编译器 → M9），复选框从未随实现更新（M1“Hello World
      编译器”早已远超、已自举，且 §M9 仍指向**已删除的** `src/ide/debugger.c`），属历史文档，
      按 C11 归档；横幅指明现状看 `SPEC/STDLIB/ARCHITECTURE`、执行计划看另三份。
      〔核查〕主仓文档无任何指向 `docs/ROADMAP.md` 的链接（仅 `AotAnywhere/` 子项目有自己的
      同名 ROADMAP，无关），移动无断链。
  (2) **给三份现存文档各加「路线文档关系」头**：`EXECUTION_PLAN.md`（标注为顶层索引）、
      `PRODUCTION_PLAN.md`（标注为 Workstream A 详情来源）、`SELF_CONTAINED_TOOLCHAIN.md`
      （标注为被生产计划阶段 5 引用的技术设计），互相点名，边界从此清晰。
  （原条目所说「43/48/68 行、合并成一份、把 TASKS 折进去」是过时设想；本轮按最终形态改为
  分层保留 + 归档过时件。）
* **C9** 文档覆盖率差距悬殊：`System` 16.5%、`Gui` 9.8%、`Game` **0.7%**。
  既然 Game 不搬走，就得把文档补上（随 B1-2）。
* **C10 ✅ 作为原则贯彻（2026-07-29）**：本轮 C1/C2/C3 均以实现+C# 为准改写
  （int 32 位与 C# 对齐、Regex 已存在纠正、single-pass 纠正）。作为持续原则写入
  `docs/DOCS_MAINTENANCE.md` §3，后续每次动文档都适用。
* **C11 ✅ 已完成（2026-07-29）：新增 `docs/DOCS_MAINTENANCE.md`。**
  定义分层规则（当前实现 / 目标设计 / 历史记录）、目标设计须标 A 编号并随能力落地同步、
  以实现和 C# 为准（C10）、bug 状态字段格式（C6）、归档位置（`docs/`/`docs/projects/`/`docs/archive/`）。
  `ABI.md` §1.1 是分层落地范例。

---

# 建议执行顺序

| 批次 | 内容 | 为什么排这里 |
|---|---|---|
| **0** | ~~A8-1~~ ✅ / ~~A8-2~~ ✅ / ~~A8-3~~ ✅ / ~~A8-4~~ ✅ / ~~A8-12~~ ✅ / ~~A8-13~~ ✅ / ~~A8-14~~ ✅ / ~~A8-15~~ ✅（catch 内再抛跳错 handler、抛出点双重释放、被放弃 handler 泄漏异常、异常落地用帧覆盖栈槽、表达式里的 await 被当 `int`；+ 顺带修掉 A8-8 foreach break/continue、A8-9 `var`+await 类型、A8-10 测试重复编译） | **三份 repro 全部实测仍成立，两个比文档记的更严重**。ARC 与异常/协程交互不可靠 ⇒ 任何 Zan 上层代码都不可信，而"上层全用 Zan"是整个计划的地基 |
| **0.5** | ~~**A7-1**~~ ✅（修约束接口方法调用的 codegen 崩溃）/ ~~A7-2~~ ✅（构造类型的静态成员访问）/ ~~A7-4~~ ✅（无花括号单语句体）；~~**A7-5**~~ ✅（泛型字段在 `T` 为引用类型时取值错误/堆损坏，四个缺陷） | 已全部实测修复；A7-5 曾阻塞 B3-1，现已解除 |
| **1** | ~~**A0-0**（句柄型 extern 改 `nint`）~~ ✅ 已完成（330 处 / 26 文件，553/553）；**A0-0c**（socket 句柄统一 `nint`）已完成（562/562，窄化警告 51 → 0）、**A0-0d** 句柄部分已完成（`rt_io` → `intptr_t`，`zan_gui_*` 句柄 → `iptr`；位宽部分并入 A0-1/A0-2）；余下 C4 / C5 / C7（删残桩、提交游离文档、游戏计划归到项目文档层） | A0-0 在 int 还是 64 位时零语义变化，是切 int=32 的安全前置。清理放这里而不是更早：`ABI.md` 这类失真文档在 A2 落地前仍是唯一的目标 ABI 参照，**先实现、后按实现重写文档**，不要先清场 |
| **2** | ~~**A0-1 / A0-2 / A0-3**（int=32 + FFI 位宽 + 结构体布局）~~ ✅ 全部完成（2026-07-28） | 一切 FFI 和编解码工作的前置 |
| **3** | ~~**A1** `Span<T>`（A1-1/A1-2/A1-3 ✅）~~ + **B2** ✅ 生产代码 calloc-as-string 已清零（十一批，只剩 tests 里刻意保留的 native-ABI 用例） | 能力与清理配对落地 |
| **4** | B5-1 ~ B5-4：~~B5-3 draw_icon~~ ✅ / ~~B5-4 Win32 死代码~~ ✅ / B5-2 🟡 只剩 macOS WebView 桩；**B5-1 光栅器仍未开始**（`gui_runtime.c` 28 个导出） | A0+A1 就绪即可做，GUI 侧收益最大 |
| **5** | **A2**（A2-0/A2-1/A2-2 ✅；仅剩 **A2-3** 变参 / **A2-4** signext+callconv）→ B5-5 / B5-6（X11 / SDL / Cocoa） | 大工程，前面跑通再上 |
| **6** | **A3** bindgen + **A6** 编译器 API → **B6** 工具链 Zan 化 | 仍在 C 的约 230KB（LSP/DAP/json/rpc）的出口 |
| **7** | A4 / A5、~~B1-1 Sdk 生成器~~ ✅、B1-2 Game 文档与提炼、B3 提炼（B3-0/B3-1 ✅、B3-2 🚧）、~~B4 错误约定~~ ✅（B4-1/2/3）、B7 runtime 复核、C 组文档（C1/C3/C4/C5/C6/C7/C8/C10/C11 ✅，剩 **C2 四份** 与 **C9**） | 收尾 |

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
A15-6（`char` 按字符打印、`ulong` 十进制字面量不再夹断）已修，
新增 `char_and_ulong_text` conformance。A15-4 已完成（元素槽按 stride，见 A30）；
**A15-5（全库 0 处 `switch`）仍未做**。**

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

## A15-4 集合元素槽固定 8 字节 — ✅ 已完成（见 A30）

原状：`List<T>` / `Dictionary` 的元素槽是裸 `i64`，struct 元素要打包/解包
（`33a66df`），超过 8 字节的只能报错。现已按 stride 存放（`0fc30b4`），
证据与遗留限制（`Dictionary` 值槽仍 8 字节、宽 struct 的 `IndexOf`/`Insert` 等
给明确编译错误）见 **A30**。

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

## A15-6 其它已确认的小差异 — ✅ 已修（2026-07-29）

* ~~`char` 打印成数字（`'A'` → `65`），C# 打印字符。~~ 已修：新增
  `emit_char_to_cstr()`（`irgen_generics.c`，把码点按 UTF-8 编成 ARC 串），
  凡是静态类型为 `char` 的表达式都走它——`Console.WriteLine` / `Console.Write`
  （`irgen_call.c`）、`+` 拼接与多段拼接 `emit_str_concat_n()`、字符串插值
  （`irgen_expr.c`）、`String.Format` 的字面量展开。`(int)c` 与
  `int n = c;` 仍然是码点，整数/`ulong` 的既有格式化路径没有变动。
  注意 `s[i]` 目前的静态类型不是 `char`（仍是整数），所以按数字打印，
  这属于「索引器返回 `char`」的另一处 C# 差异，不在本条范围内。
* ~~`ulong` 字面量超过 i64 上限会被夹到 `9223372036854775807`。~~ 已修：
  `lexer.c` 的十进制路径原先只用 `strtoll`，溢出时被 `LLONG_MAX` 夹断；
  现在检查 `errno == ERANGE` 后改用 `strtoull` 保留位模式（十六/八/二进制
  路径本来就是这么做的）。
* 〔测试〕`tests/conformance/char_and_ulong_text.zan`：ASCII / 非 ASCII
  （`€`、`ñ`）字符的打印、写、拼接、插值、`String.Format`，`(int)` 仍是码点，
  以及 `ulong.MaxValue`、`10^19`、两者相减与 `long.MaxValue` 的十进制字面量。
  conformance / determinism / leakcheck 3/3 Passed。
* 〔顺带〕`irgen_emit.c` 的模块校验失败现在会先逐函数 `LLVMVerifyFunction`，
  把出错函数名写进诊断（原来只有一条 `ret i32 0` 却不知道在哪个函数）；
  靠它定位到 B2-1 第十批漏改的 `FbBytes.Text()`（`byte[]` 上调
  `Substring` → 走了 fallback），以及 `tests/conformance/mysql_auth_vectors.zan`
  还在对已经是 ARC `byte[]` 的返回值调 `free()`（堆损坏 0xc0000374）。
  两处都已修，`firebird_wire` / `mysql_auth_vectors` 的 6 个测试恢复 Passed。
* struct 与整数不兼容此前只有无行号的 LLVM 校验失败——已修（`94cf827`）。
* ~~数组是裸缓冲区，长度只在声明处记住~~ 已修，见 A15-8。

## 建议顺序

1. ~~A15-1 `byte` 零扩展 + A15-2 数组初始化器~~ 已完成（`1dd023d`），
   顺带修掉 A15-7 foreach 和数组元素赋值的宽度截断；
2. ~~A15-3 struct `==` / 运算符重载 / `const`~~ 已完成（`1dd023d`）；
3. 用修好的语言能力改正封装：`byte[]` 取代 string/NativeMemory 字节缓冲
   （B2 的 49 处）、`foreach` 取代下标 while 循环；
4. ~~A15-4 元素槽 + 数组长度头（与 A1 `Span<T>` 合并）~~ 已完成（A30 / A15-8）；
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
* ~~`try { throw ... } finally { ... }`（无 catch）**吞异常**~~ —— 见 A21，已修。

---

## A21 — `finally` 只在正常退出时跑：异常被吞、return/break/continue 跳过它（已修）

现象（`_scratch/fin1.zan`，修前）：
```
inner try
inner finally        <- 只有这一层跑了
outer caught u       <- Boom 被吞掉，外层根本没收到
ret 7                <- return 直接走，finally 没跑
loop 0
loop finally 0       <- break/continue 那两轮的 finally 全没跑
done
```

根因（`src/compiler/irgen_stmt.c`，`AST_TRY_STMT` 降级）：`finally_body` 只在
`try.end` 这一个基本块前发射，而 `try.end` 只有**正常落出**才会到。其它三条出路
都绕开它：
* 没有匹配 clause 的异常走 `rethrow_bb` → 直接 longjmp 到外层 handler；
  无 catch clause 的 `try/finally` 更糟——`catch_bb` 释放掉异常后 br 到 `try.end`，
  等于把异常吞了当没抛过，函数照常返回。
* `return` 直接发 `ret`；
* `break`/`continue` 直接 br 到循环出口/步进块。

这套降级是 setjmp/longjmp，没有 landingpad 可以挂 cleanup，所以 C# 的「每条出路
都跑 finally」只能**在每条出路上内联一份 finally 体**：
* `g->finallys[]`（`irgen.h`，深度上限 `ZAN_MAX_FINALLY_DEPTH`，超出报编译错误而不是
  静默不跑）记录当前正在发射的 try 的 finally 体，`in_try_body` 标记当前发的是被保护体
  还是 catch/finally 体——前者的 throw 由本 try 自己的 handler 接住（finally 由
  handler 路径跑），后者的 throw 已经离开本区域，必须在 throw 点先跑本层 finally。
* `emit_pending_finallys()`：`return` 跑全部；`break`/`continue` 跑
  `finally_loop_base` 以上的（即在本循环内进入的那些）。发射第 i 份时把栈截断到 i，
  所以 finally 里的 return 只跑更外层的 finally，不会自我递归。
* `emit_finally_on_exception_path()` + `emit_eh_propagate_tail()`：rethrow 路径和
  「无 catch clause」路径先跑本层 finally，再把在飞异常交给下一个外层 handler
  （没有则报 unhandled 退出），不再落到会释放异常的 catch 收尾。
  finally 体自己可能 try/catch，会覆盖 `__zan_eh_exc/_owned/_tid`，所以进 finally 前
  存一份、出来再恢复。
* async 体里这份存档不能放 alloca：finally 里 `await` 会从 `$resume` 返回，
  alloca 内容作废。新增帧字段 `ASYNC_FRAME_FINEXC/_OWNED/_TID`
  （`[ZAN_MAX_FINALLY_DEPTH]`，按 finally 区域深度索引）。
* async 帧的 await 子槽是按扫描到的 await 数量分配的，finally 体被内联多份后
  子槽会不够（症状是 `Invalid indices for GEP pointer type!`）。
  `async_scan_stmt` 现在按份数扫 finally 体：份数 = 正常出口 + 异常出口 +
  被保护体/handler 体里的 return/break/continue/throw 条数（`async_count_transfers`，
  取上界，多算只多占帧字节）。

实测（Windows）：
* 新增 `tests/conformance/exception_finally_paths.zan`（无 catch 的穿透、不匹配
  clause、两层嵌套 finally 顺序、catch 里 throw 触发本层 finally、return 值在
  finally 前求值、catch 里 return、循环里 break/continue、finally 内分配对象）
  23 行输出全对，`--check-leaks` 干净。
* 新增 `tests/conformance/async_finally_paths.zan`（finally 体内 `await` 的异常
  穿透、`return await` + finally、循环 continue + finally）输出全对，
  `--check-leaks` 干净。
* 子集 `ctest -R 'exception|throw|catch|async|try|await|leakcheck|generic|finally' -j8`
  = **289/290，131.8 秒**；唯一失败 `conformance_async_concurrent_echo` 是 -j8 高负载
  下的端口/调度抖动（单独连跑 5 轮 9/9 全过）。A20 时失败的
  `leakcheck_sdk_wechat_product_modules` 现在也过了（另一个会话已修）。

---

## A22 — EH 状态改为每线程私有（已修）

现象：多线程程序里的 try/catch 会崩。新增 `tests/conformance/exception_threads.zan`
（4 个 `Thread.Start` 线程 + 主线程，各自 150 轮：6 层递归 try/catch + 逐层裸
`throw;` 重抛 + 异常穿过 finally，每次校验接住的就是自己抛的那个 `Boom.code`）在
改动前的编译器上 5 跑 4 崩：

```
run 0 exit=-1073740940   (0xC0000374 堆损坏)
run 1 exit=-1073741819   (0xC0000005 访问违例)
run 2 exit=0
run 3 exit=-1073741819
run 4 exit=-1073741811
```

根因：A20 把 handler 栈和展开栈换成了不搬移的 chunk 存储，解决了内存搬移，但
`__zan_eh_top / __zan_eh_exc / __zan_eh_exc_owned / __zan_eh_exc_tid /
__zan_eh_tmps_top` 以及两张 chunk 表仍是**进程级全局**。多线程同时抛：
* A 线程的 `throw` 读到 B 线程的 `top`，longjmp 进 B 的 `jmp_buf`——跳进一个不属于
  本线程的栈帧；
* catch 绑到别的线程的异常对象，`owned` / `tid` 也串线；
* 一个线程的展开栈项被另一个线程 pop / unwind，释放别人还在用的对象。

改法（`src/compiler/irgen_builtins.c`）：全部 EH 状态收进一个每线程的状态块
`zan.eh.state`（top / tmps_top / exc_owned / exc / exc_tid + 两张 chunk 表），
由新的 `__zan_eh_state()` 按 OS 线程 id 在开放寻址表里找：
* 线程 id 取 `GetCurrentThreadId`（Windows）/ `pthread_self`（POSIX），key 为
  `id + 1`，0 表示空槽；
* 空槽用一次 `cmpxchg` 抢占，抢到的线程 `calloc` 状态块并把 `top` 初始化成 -1，
  此后该槽只有属主线程读写，状态块指针本身不需要同步；
* 状态块永不释放（线程 id 被 OS 回收后复用同一块），槽用尽或分配失败**显式 abort**，
  不会静默让两个线程共用一个 handler 栈——那正是本条要消灭的破坏。
* 之前试过的编译器生成的原生 TLS 在自带 ld.lld + mingw 这条链上首次访问就崩（A20
  记录），所以走这张表而不是 `thread_local`。

配套：状态块指针和字段地址在**每个函数的 entry block** 里只求值一次（`irgen.h` 的
`eh_state_owner/_cached/_fields` 缓存）。EH 降级会把这些指针交给 catch/end 块，而
async 的 CPS 切分会把这些块挪出定义点的支配范围——和 `emit_entry_alloca` 是同一个
理由。chunk 访问函数改成 `(i8* state, i32 idx)` 两参形式。

`ZAN_EH_CHUNKS` 1024 → 64：容量是**每线程**的，仍有 4096 个活 handler /
256K 条展开项，同时把每个状态块的固定部分压到 1 KB 出头。

实测（Windows）：
* `exception_threads` 新编译器 5 跑 5 过，`ok 1500 / bad 0`（5 个参与者 × 150 轮 × 2 项）。
* 单线程变体（去掉 4 个 `Thread.Start`）`--check-leaks` 干净。

顺带发现（**未修**，另记）：`--check-leaks` 的分配追踪本身不是线程安全的。纯分配、
完全不抛异常的探针 `_scratch/thr_alloc.zan`（5 线程 × 5000 个 `List<string>`）就会
随机报 27 / 60 / 320 个"still reachable"，同一个二进制每次数字都不一样。所以
`exception_threads` 暂不注册 leakcheck 变体（`CMakeLists.txt` 的 `_no_leakcheck`），
否则这个门禁就是抛硬币。追踪表要改成线程安全后再把它放回去。

## A23 — async 帧的 catch 槽不再是固定 8 个（已修）

现象：一个 async 函数体里同时活着的 try 超过 8 个就崩。新增
`tests/conformance/async_handler_slots.zan`（12 个顺序 try/catch，每个 handler 内
先 `await` 再读回自己的异常；外加 11 层嵌套 try，最内层抛出后每层 `await` 完再裸
`throw;` 逐层重抛）。把容量重新卡回 8（`_scratch` 里临时改 `a_handler_cap`）复现旧
行为：

```
12 boom1..boom12
exit=-1073741819   (0xC0000005，Nested() 的第 9 层起写出帧外)
```

根因：`ASYNC_MAX_HANDLERS 8` 同时被用作三处：帧里 `HSTACK / CEXC / CEXC_OWNED /
CEXC_TID` 四个数组的**长度**、catch 槽选择的**上界**、以及 resume 时重新武装
handler 的**钳位**。async body 的 try 数量是编译期已知的，没有理由取一个猜的常数：
超过 8 之后要么索引出帧（内存破坏），要么静默退回 alloca——而 alloca 在 `await`
之后就是垃圾，catch 会读到已经失效的异常指针。

改法：`async_scan_t` 增加 `try_count`（async 扫描已经会把 finally 体按发射份数重复
遍历，所以计数和发射出的 try 数一致），`irgen_emit.c` 用它构造四个数组的长度
（LLVM 不接受长度 0 的成员，所以无 try 的函数体仍留 1 个不用的槽），并把容量随
`method_body_work_t.handler_cap` 传进 resume body 的发射，`g->current_async_handler_cap`
在每个 async 发射路径上保存/恢复。所有 GEP 的数组类型、switch case 数、边界检查和
重新武装的钳位都改读这个 per-frame 容量；`ASYNC_MAX_HANDLERS` 已从编译器里删除。
`zan.co.header`（跨帧共享的前缀类型）随之截到 `HSTACK` 之前——per-handler 数组不再
是共享前缀，而 header 本来也只用来访问它前面的字段。

实测（Windows）：`async_handler_slots` 输出全对、`--check-leaks` 干净、
`--async-workers 1/2/4` 都是同一结果；
`ctest -R 'exception|throw|catch|try|await|async|thread|ctor|inherit|arc|leakcheck_generic'`
= **132/132，8.7 秒**。

## A24 — `: base(args)` 的实参临时被泄漏（已修）

现象：**每一个**用计算出来的 rc 参数构造的派生类实例都会漏掉那个参数。最小复现
`_scratch/four.zan`：`class D1 : B1 { D1(int c) : base("tag" + Convert.ToString(c)) }`
—— `new B1("plain" + ...)` 干净，`new D1(1)` 报 1 个 still reachable；派生类**自己**
声明的 string 字段正常释放，只有经 base 初始化器传下去的那个漏。

根因（`src/compiler/irgen_emit.c` 的 base construction 段）：`: base(args)` 的实参
只是 `emit_expr` 出来直接塞进 `cargs`，从来没有走普通调用点的
`emit_release_owned_call_temp`。base ctor 会 `zan_rt_str_retain` 它存下来的值，于是
实参表达式产生的那个 +1 没人放手，对象死后引用计数仍是 1。IR 里看得很清楚：
`D1_ctor` 里 `%str.raw1 = call ptr @zan_rt_str_alloc(...)` 之后只有
`call void @B1_ctor(ptr %this.base, ptr %str.raw1)`，没有对应的 release。

这不是异常/async 的问题，是构造链的 ARC 漏洞，之所以在 A23 这轮暴露：新用例用了
`class Boom : Exception { Boom(int c) : base("boom" + ...) }`，leakcheck 立刻报数。

改法：base ctor 调用后按实参逐个 `emit_release_owned_call_temp`，和普通调用点完全
一致（局部变量实参、借用返回值等由该函数自己判定，不会误放手）。

实测：新增 `tests/conformance/base_ctor_arg_release.zan`（200 轮 × 三种形状：
string base 实参、`List<string>` 字段 + string base 实参、三层继承链各自转发自己的
临时）输出正确、`--check-leaks` 干净；修改前同一用例每轮每个实例漏 1 个。
子集 `ctest -R 'leakcheck|oop|class|inherit|virtual|poly|struct|generic|sqlserver|http|socket'`
= 250 个里 248 过，2 个失败是既有的 WeChat SDK codegen 错误
（`leakcheck_sdk_wechat_product_modules` / `leakcheck_sdk_wechat_tenpay`：
`LLVM verification failed: Incorrect number of arguments`，另一个会话正在改的
`stdlib/Sdk/Wechat/*`，与本条无关）。

## A25 — `--check-leaks` 的计数器不是线程安全的（已修，A22 的遗留项）

现象：多线程程序即使一个对象都没漏，`--check-leaks` 也会随机报数。探针
`_scratch/thr_alloc.zan`（5 个线程 × 5000 个 `List<string>`，全程不抛异常）同一个
二进制每跑一次报的数字都不一样：27 / 60 / 320 / ……

根因：`__zan_live` 和 `__zan_site_live[site]` 的加减是普通的
load → add → store 三件套（`src/compiler/irgen.c` 里 `zan_rt_alloc` /
`zan_rt_release` / `zan_rt_str_alloc` / `zan_rt_str_release` 四处）。对象的引用计数
本身早就是 `atomicrmw`，唯独这两个统计量不是，于是两个线程同时分配就丢一次更新，
最终净值偏正——报成"泄漏"。偏负的情况同样存在，那会**漏报**真实泄漏。

改法：新增 `emit_leak_counter_add()`，四处都改成 `atomicrmw add/sub`
（monotonic：只关心计数值本身，报告在 atexit 里读）。

实测：`thr_alloc` 8 跑 8 次都只有 `25000` 一行、无泄漏行；
`tests/conformance/exception_threads.zan --check-leaks` 5 跑 5 次
`ok 1500 / bad 0` 且无泄漏行。于是把 A22 里临时摘掉的 leakcheck 变体放回去
（删掉 `CMakeLists.txt` 的 `_no_leakcheck`），`leakcheck_exception_threads` 现在是
正常门禁。子集 `ctest -R 'exception|thread|leakcheck|arc|generic'` = 239 个里 237 过，
2 个失败仍是既有的 WeChat SDK codegen 错误（见 A24）。

## A26 — 既有失败，与 A21-A25 无关（✅ 两项均已消失，2026-07-29 复核）

〔复核〕`ctest -R 'conformance_gui_datatable|leakcheck_sdk_wechat_product_modules|
leakcheck_sdk_wechat_tenpay'` **3/3 Passed**，全量 `ctest -j4` 也是 711/711。
两项都不是靠改测试消掉的：`tests/conformance/gui_datatable.out` 与
`tests/gui/datatable_test.zan` 的最后一次改动都还是 `00035b4`（早于本轮），
说明 5 个断言是被后续的 DataTable 重划（B3-3）/编译器修复真正修好的；
Wechat 那两个 leakcheck 则随生成脚本那边收尾后参数个数对上了。下面是原始记录：

1. `conformance_gui_datatable`：554 行输出里 5 个断言输出 0 应为 1（行 306 / 328 /
   331 / 438 / 457）。用改动前的编译器（`d1f804a^`）重编同一程序，失败完全一样，
   所以是既有的 GUI DataTable 行为问题，不是 EH / async / ARC 这几轮引入的。属于 B5
   GUI 迁移的范围。
2. `leakcheck_sdk_wechat_product_modules` / `leakcheck_sdk_wechat_tenpay`：编译期就
   失败，`LLVM verification failed: Incorrect number of arguments passed to called
   function`（如 `WechatWorkChatApi_CreateChatAsync`）。`stdlib/Sdk/Wechat/*` 和
   `scripts/generate_wechat_*_apis.py` 正在被另一个会话改动（工作树里是未提交状态），
   生成出来的调用点和声明的参数个数不一致。等那边收尾，不在本轮范围内。

---

## A27 — async 帧 result 类型化 + 取消路径（2026-07-28，已做完）

* **A27-1 ✅ typed frame result**（原 A8-7 遗留，A0 前置）。帧 result 槽物理上仍是 64 位
  （它在共享 header 的固定偏移上，宽度不能按函数变），但不再当成 `i64` **值**用：
  完成侧按被调用方**声明返回类型**编码，await 侧按同一类型解码——有符号/bool 符号扩展、
  无符号零扩展、指针 `ptrtoint`/`inttoptr`、`double` `bitcast`、`float` 先 `bitcast` 到
  i32 再零扩展。return 表达式先转成声明类型再入槽，所以 `async double F() { return 3; }`
  存的是 double 而不是整数位型。原来的一律符号扩展会吃掉无符号值的最高位、按错误宽度读
  `float`，并且在 `int` 变成 32 位后会静默截断。
  证据：`tests/conformance/async_result_types.zan`（负 int / 大 long / `uint` 最大值 /
  byte / short / bool / double / `double` 返回整数字面量 / string / 对象 / `var` 推断）。

* **A27-2 ✅ 取消路径**（原 A8 结尾记的空缺）。协作式，全部在生成代码里（驱动本身就是
  编译器生成的，运行时没有可以持有这些状态的地方）：
  - 帧头新增 `cancel`(i32) / `child`(i8*) / `lnext`(i8*)；`child` 在 await 站点写入、
    在每次 resume 开头清掉。
  - `__zan_co_cancel(handle)` 置位后沿 `child` 链向下传播——取消一个协程也会取消它正在
    等的那个。
  - `Task.Spawn` 的句柄比协程活得久，所以 detached 帧挂进内部 `__zan_co_live` 链
    （`lnext`），`Task.Cancel` 先在链上查；已被 `__zan_co_reap` 回收的句柄是安全 no-op
    （reap 先摘链再 free）。活帧下面的 `child` 链天然还活着：挂起的 awaiter 持有子帧。
  - 被取消的协程不是被异步杀死：body 在**进入时**（还没被 step 过就被取消 ⇒ 整个 body
    不执行）和**每个含 await 的语句边界之后**检查标志，命中就跳到正常完成路径——持有型
    局部照常释放、awaiter 照常唤醒、帧只释放一次。`finally`/`catch` 清理区内不插检查，
    取消不会绕过清理。代价：挂在 timer / socket 上的协程要等那次等待结束才看得见取消。
  - 语言面：`Task.Spawn(asyncCall)` 返回句柄（丢弃即原来的 fire-and-forget）、
    `Task.Cancel(handle)`、`Task.IsCancellationRequested()`（协程外返回 0）。
    **不引入** `OperationCanceledException`——被取消的协程是"完成"，不是"抛出"。
  证据：`tests/conformance/async_cancel.zan`（step 前取消 / 经 await 链传播到子协程 /
  标志可观测 / 跨 await 的持有型局部在取消时被释放 / 对已回收句柄重复取消）。
  对照组（同一程序去掉 `Task.Cancel` 调用）输出 `never_hit=2 marks=11 unreachable`，
  说明用例不是空过。

* **未做**：挂在 timer/IO 上的即时取消（需要 timer/reactor 侧可撤销的注册）、
  取消时的非 void 返回值语义（目前返回槽保持零值/默认）、`CancellationToken` 那套
  结构化传播。

---

## A28 — A8-6 补漏，外加由此暴露的两个既有 bug（2026-07-28，已做完）

* **A28-1 ✅ A8-6 `anf_stmt_contains_await` 补 `AST_TRY_STMT`/`AST_SWITCH_STMT`**
  （连带 `AST_CATCH_CLAUSE`/`AST_SWITCH_CASE`）。只影响**不带花括号**的单语句体
  （`if (c) try { ... await ... } catch ...`）：判定为"不含 await"就不会包块，
  ANF 提升出来的临时声明会落到体外。
  证据：`tests/conformance/async_await_in_braceless_body.zan`
  （try/switch 分别作为 if / while / foreach 的无花括号体）。

* **A28-2 ✅〔顺带发现，既有严重 bug〕`switch` 里的 `break` 会跳出外层循环**。
  `AST_SWITCH_STMT` 的降级从来没有设置自己的 `g->break_target`，所以 case 里的
  `break` 直接跳到外层 while/for/foreach 的出口块——**任何"循环里套 switch"的代码
  都只跑了一轮**，而且是静默错误结果，不报错。与 async 无关，同步代码一样中招。
  修法：switch 进入时保存并改写 `break_target`（`continue` 保持指向循环，不动），
  string switch 与整数 switch 两条路径都改。
  证据：`tests/conformance/switch_break_in_loop.zan`
  （while / foreach / string switch / switch 内 `continue`）。修复前
  `while=1(i=0)`、`foreach=1`，修复后 `213 / 71 / 8 / 102`。

* **A28-3 ✅〔顺带发现〕`foreach` 遍历数组/字符串 + 体内 await ⇒ LLVM 校验失败**
  （`Instruction does not dominate all uses: %arr.len`）。List 的 count 是每轮
  从对象里重读的，数组长度和 `strlen` 却是在循环前置块算一次存寄存器；await 把循环
  切到另一次 `$resume` 调用里，那个寄存器既不支配条件块也活不过挂起。
  修法：async 体内（已有 `col_slot` 的情况）在条件块里从重新载入的集合指针重算长度。
  证据：`tests/conformance/async_await_in_foreach_array.zan`（数组 / 字符串 / 嵌套 +
  `continue`）。既有的 `async_await_in_foreach` 只覆盖了 `List<T>`，所以一直没暴露。

> 全量 ctest 678 项，仅 `conformance_gui_datatable` 一项失败（A26 既有，B5 GUI）。

---

## A29 — 实例泛型方法单态化（A7-1 收尾，2026-07-28）

* **A29-1 ✅ 实例（非 static）泛型方法现在能单态化**。此前 `get_or_create_method_spec`
  只接受 static 方法，实例的"模板"方法（体内穿过 `T` 取成员）直接报编译错误。
  现在特化副本把 `this` 作为第 0 个参数插到签名前面，`emit_method_spec_body`
  绑定 `this`（`g->current_this`），调用点三条路径都接了单态化：
  局部变量接收者 `p.M<T>(x)`、任意表达式接收者 `p.Self().M<T>(x)`、
  以及同类内不带接收者的 `M<T>(x)`（用隐式 `this`）。
  接收者限定为**类**（struct 接收者要按地址传，这条路径没建模），
  **声明类型本身是泛型**（`Pool<TConn>.M<T>()`）仍然报错——擦除的类变体拿不出
  `this` 的布局，需要把类实例化也折进 mangling，留作后续。

* **A29-2 ✅ 顺带修：泛型方法把自己的类型参数转发给另一个泛型调用**
  （`Both<T>(T c) { return this.Describe<T>(c); }`）。这种体同样**没有擦除形态**，
  但模板扫描只看"是否穿过 T 取成员"，于是照样发射了一份擦除实例，里面那个
  `Describe<T>` 无从解析，落到 `emit_expr` 兜底 `ret i32 0` → LLVM 校验失败。
  现在 `tp_scan_expr` 把"调用的显式类型实参里出现自己的类型参数"也算作模板标记。

  证据：`tests/conformance/generic_instance_method.zan`（conformance / determinism /
  leakcheck 三份）。全量 ctest 678 项，仍只有 `conformance_gui_datatable` 一项既有失败。

> 仍未做：**async 泛型方法**的单态化（CPS 帧机制按符号建帧，特化副本要连帧类型、
> ramp/$resume 一起复制），以及**泛型类的实例泛型方法**（见上）。

---

# A30 · 集合元素槽不再固定 8 字节（A15-4，已完成）

`List<T>` 的数据缓冲区仍是 `i64*`，但一个元素占**整数个 8 字节字**：标量/引用元素
1 个字，值 struct 占 `ceil(sizeof/8)` 个字并**内联**存放（原来是打包进单个 i64，
超过 8 字节直接报编译错误）。容量、`realloc` 字节数、以及每一处槽地址计算都按
`elem_slot_words()` 的 stride 缩放（`slot_word_index()`）。

* 覆盖到的路径：`new List<T>()` 与带初始化列表的 `new List<T>{...}` 的首次分配与
  逐项写入、`Add`（含扩容 `cap*stride*8`）、`list[i]` 读、`list[i] = v` 三条写路径
  （局部 / 隐式 `this.field` / `obj.field`）、`foreach`、LINQ 查询取元素、
  `RemoveAt`（按字整体左移 + 尾部清零 stride 个字）、`Clear`。
* **顺带修**：`list[i].field` 这类"在 struct 右值上取字段"一直落到
  `emit_expr_member_access` 的兜底 `return 0`——`a[0].x` 静默返回 0（与元素宽度无关，
  1 个 int 的 struct 也中招）。现在对寄存器里的 struct 右值走 `extractvalue`。
* 仍受限（给明确编译错误，不产生错 IR）：宽 struct 元素的 `IndexOf` / `Contains` /
  `Insert` / `Reverse` / `AddRange`（这些按单字做相等比较或整字搬运），
  以及 `Dictionary` 的值槽仍是 8 字节。

证据：`tests/conformance/list_wide_struct_elements.zan`（12 字节 P3 与 32 字节 Big：
10 个元素的下标/字段访问、foreach 求和、扩容、整元素覆盖写、`RemoveAt` 左移、
`Clear`）。全量 ctest 687 项，仍只有 `conformance_gui_datatable` 一项既有失败。

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

# A31 · macOS 交叉编译解锁 async + 原子（2026-07-29，已实测）

**动机**：`docs/platform-targets.md` 原写「macOS 交叉 = console/compute only」，
`main.c` 对 `rt_io_obj || irgen.uses_sync_runtime` 直接 `error:` 退出，原因是
`zanrt_io.o`/`zanrt_sync.o` 只为 linux-musl 预编译过，没有 Mach-O ABI 版本。

**做法**（对齐 AotAnywhere 的思路：宿主机不需要 Apple SDK / 不需要 Mac）：

* `scripts/build_macos_rt.sh` —— 用 `zig cc -target {aarch64,x86_64}-macos.11.0`
  编 `src/runtime/rt_io.c`（走文件里已有的 kqueue 分支）与 `rt_sync.c`，产出
  `toolchain/macos/<arch>/zanrt_io.o` / `zanrt_io_mt.o`（`-DZAN_CO_DRIVER`，
  `--async-workers` 用）/ `zanrt_sync.o`。zig 自带 Darwin libc 头，所以这一步在
  Linux/Windows 上都能跑。`.11.0` 后缀让 `LC_BUILD_VERSION minos` 与 zanc 传给
  ld64.lld 的 `-platform_version macos 11.0` 一致，否则每次链接都告警。
* `scripts/check_macos_rt.py` —— 解析这 6 个对象的 undefined 符号，断言全部落在
  `toolchain/macos/libSystem.tbd` 的导出表内（唯一例外 `_zan_co_ready`，由被编译
  程序自身的内联协程驱动提供）。这样「stub 少一个符号」在我们这里就暴露，而不是
  等用户链接时才炸。〔实测〕6/6 ok。
* `main.c`：macOS 交叉分支不再硬报错，按目标架构挑
  `<zanc>/macos/{arm64,x64}/zanrt_io{,_mt}.o`、`zanrt_sync.o` 加入 ld64.lld 命令行，
  对象缺失时报明确错误并指向 `scripts/build_macos_rt.sh`；`rt_sync_obj` 的
  「仅 Linux 可交叉」判断放行 macOS。CMake 的 `copy_directory toolchain/macos`
  已经递归，子目录自动随 zanc 分发。

**顺带修掉一个更基础的 bug**：macOS 交叉连 `Console.WriteLine` 的 hello world 都
链不上 —— irgen 为非 Windows 目标发 `stdout`/`stdin` 全局，但 Darwin libSystem 只导出
`__stdoutp`/`__stdinp`（`_stdout` 在 libSystem.tbd 里确实不存在，已核对）。加
`irgen_t.target_is_macos`（由 target triple 含 `apple`/`darwin` 判定，本机 macOS
编译走 `#ifdef __APPLE__`），`irgen_emit.c` 的 setvbuf 与 `irgen_call.c` 的
`Console.ReadLine` 按平台选名。**所以此前文档说的「console 可交叉」其实也是不成立的**，
现在才真正成立。

**验证**：
* `macos-arm64` / `macos-x64` 各链成功：hello world（12 文件）与 async socket +
  `AtomicInt` 探针（21 文件）全部 `Compiled ... ->`，无告警。
* 产物 Mach-O 复核（`_scratch/mac_probe_arm64`）：`magic feedfacf`、
  `cputype 0x100000c`(arm64)、`filetype 2`(MH_EXECUTE)、`LC_BUILD_VERSION plat=1
  minos=0xb0000`、`LC_MAIN`、`LC_LOAD_DYLIB /usr/lib/libSystem.B.dylib`、
  **`LC_CODE_SIGNATURE` 存在且 CodeDirectory flags=0x20002 =
  `CS_ADHOC|CS_LINKER_SIGNED`** —— 即 ld64.lld 已自动打 ad-hoc 签名，这正是
  Apple Silicon 执行未签名 Mach-O 会被内核拒绝所要求的，不需要任何证书。
* 回归：本机 Windows 与 `--target linux-musl` 的 hello/async 探针照旧（linux 产物在
  Linux 上实跑输出 `hi from mac`）；`conformance_(console|io_|hello|string|threading|async_)`
  **31/31 Passed**。

**仍未做**（不算在本项内）：① 没有 Mac 机器，未在真实 macOS x64/arm64 上执行产物，
只做到「链接 + Mach-O/签名结构」级别验证；② GUI 交叉仍不可用（只有 libSystem stub，
Cocoa/CoreText/QuartzCore/IOSurface/WebKit 都没有 `.tbd`，且 `gui_runtime_mac.m` 是
Objective-C，需要 Mac runner 编出对象），见 `docs/platform-targets.md` §4；
③ 分发用 Developer ID 签名 + 公证（需要证书 + App Store Connect API key，
可用 rcodesign 在非 Mac 上做）未接。
