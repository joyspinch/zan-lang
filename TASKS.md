# zan-lang 全仓审计 · 可执行任务清单

**总目标**：除 compiler 和 runtime 外，一律用 Zan 写。一切以最终形态和最佳实现为准，
不为兼容老代码让步（未发布）。语法语义**尽可能保持 C#**。

主体分三部分：A 编译器/运行时能力，B 标准库，C 文档。每项带 ID、依赖、判定标准。
后续扩展章节：A15-A16（语言缺口/CSS 现状）、A17-A31 与 A35-A42（历史修复，一行摘要）、
A32（遗留收尾路线）、A33-A47（专项记录）、文末"已撤回的结论"。

> 2026-08-03：本文件已清理。已完成的条目压缩为一行摘要（原始详细记录在
> git 历史与 `_scratch/TASKS.md.bak-2026-08-03`）；未完成/进行中/冻结的条目保留原文。
> 2026-08-08：第二次清理。全量复核状态与数字（smoke 103/103 全绿为基线），
> 修正了 A32-1/A32-2/A45 等"实际已完成仍标未完成"的失实条目、更新过时统计数字、
> 修复 B7-4/B7-7 重复编号（后到者顺延为 B7-8/B7-9），并再次压缩已完成条目
> （原始详细记录在 git 历史与 `_scratch/TASKS.md.bak-2026-08-08`）。
> 2026-08-31：第三次清理。全量复核至 A78（含 A52-7/SelectBox/A48-1/A43-B17
> 2026-09-13：第四次清理。2026-08-31 之后已完成（已修/已闭账）的条目压缩为一行摘要
> （原始详细记录在 git 历史与 `_scratch/TASKS.md.bak-2026-09-13`）；未完成/进行中/封存留档的条目保留原文。
> 等被后续工作关闭的条目），已完成章节全部压缩为一行摘要
> （原始详细记录在 git 历史与 `_scratch/TASKS.md.bak-2026-08-31`）。

## 维护约定

本文件是这项工作的单一来源，随工作推进持续更新，不另开清单。

* 状态标记：`[ ]` 未开始 · `[~]` 进行中 · `[x]` 已完成 · `[-]` 已作废（保留条目并写明原因）；
* **ID 稳定且不复用**——作废的编号不再分配给新任务；
* 每项完成时补上实测证据（命令、输出、测试名），不写"应该可以"；
* 新发现的问题追加到对应章节末尾，编号顺延；
* 结论被推翻时，改正文并在文末"已撤回的结论"里留一条，不要静默修改。
* **迁不彻底就不迁**（2026-07-29 定调）：把一段 C 搬成 Zan，如果**它的调用方仍是 C**、
  或者只能搬走一半（另一半留 C 兼容层），那就不做——半迁移只是多一道跨语言边界，
  结构不变、性能不变、风险还多。据此已冻结 **B5-1**（光栅器）与 **B6**（LSP/DAP：
  `json.c`/`rpc.c` 单独搬没有意义，消费者仍是 C），等各自的前置（A2 / A6）就绪后一次做完。
* **优先级（2026-07-29 定调）**：重心是 **compiler / runtime 自身的封装质量、性能与除 bug**
  （A 系列），不是把代码从 C 搬到 Zan。B 组里不影响编译器/运行时质量的迁移项一律让路。
* **遇到编译器/运行时缺陷先修根因，不要绕过**（`AGENTS.md` 硬规则 10、
  `docs/WORKSPACE_CONVENTIONS.md` §8）。绕过写法只允许在写清探针与根因、
  登记到本清单并取得一致之后存在。

---

# 零、审计基线（2026-07-27 快照）

> 以下数字是审计起点快照，已随 B2（calloc 清零）/ B1-4（新增五模块）/ A0-A2（定宽）
> 等大幅变化，仅作基线参考，当前以实测为准。

> **2026-08-27 实测**：`stdlib/**/*.zan` 现为 **1186 文件 / 11.8MB**（下表的
> 767 文件 / 4.6MB 是 2026-07-27 历史快照，仅作趋势参考；各目录当前的行数与
> 文档覆盖率见 **C9**）。另：本节下方"超过 150 行的方法 23 个（现 22 个）"与
> **B3-2** 正文里的"现 36 个"矛盾，以 B3-2 的 36 个为准。

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

审计时发现、后已解决或正在解决的主要度量：

* `extern string calloc` 出现在 **49 个文件** → **B2 已清零**；
* 泛型类全库只有 **8 个**声明、连接池手写 **7 份** → **B3-1 `PoolCore<T>` 已收敛**；
* `static extern` 876 个、用 `int` 承载句柄的 193 处 → **A0-0 / A2-0 已定宽**；
* native import 分布：`crt` 207、`zan_gui` 103、`user32` 81、`zan_sdl3` 78… → A2-0 后已重核
  （`user32` 56 / `kernel32` 9 / `gdi32` 30 / `crt` 90 等，见 B5-7 旁注；
  **2026-08-08 复核**：现 `crt` 257 / `user32` 186 / `kernel32` 116 / `gdi32` 49，
  Win32 侧大多为 `EntryPoint` 直绑形态）；
* 超过 150 行的方法 **23 个**、最长 1536 行 → **B3-2 进行中**（现 22 个）；
* 精确重复行 1720 / 18421（9%）——结构性重复 → **B3 提炼**。

---

# A. 编译器 / 运行时能力

## A0 数值类型对齐 C# —— ✅ 全部完成（2026-07-28）

**已定**：完全对齐 C#——`sbyte`=8 `short`=16 `int`=**32** `long`=64，
`byte`/`ushort`/`uint`/`ulong` 对应无符号，`nint`/`nuint` = 指针宽，
`float`=32 位 `double`=64 位。

* **A0-0** ✅ 已修（2026-07-27）把用 `int` 承载句柄/指针的 extern 声明改成 `nint`。
* **A0-0b** ✅ 已修（2026-07-27）编译器新增 C# 窄化诊断（`ZAN_WARN_NARROW=1`）；
  后补上变量声明初始化式（`irgen_stmt.c` 的 `AST_VAR_DECL`），覆盖句柄载体最常见的入口。
* **A0-0c** ✅ 已修（2026-07-27）socket 句柄统一 `nint`（POSIX extern 保持 `int`，
  截断只发生在 `Socket.zan` 新增的 `Sys*` 平台包装）；全量窄化警告 51 → 0；
  用例 `socket_handle_nint.zan`，562/562。
* **A0-0d** ✅ 句柄部分已修（2026-07-27）：`rt_io.h/c` 所有 fd → `intptr_t`、
  `gui_runtime*.c` 105 处原生窗口句柄 → `iptr`；坐标/颜色/尺寸的 `i64` 故意留到
  A0-1/A0-2 同批切。顺带修 `irgen_emit.c` sync-runtime 前缀表补 `zan_monotonic_`。
* **A0-1** ✅ 已完成（2026-07-28，commit `3a2bc6d`）`int` → i32、`long` → i64，
  与 C 对齐后 FFI 自然正确。前置：irgen 391 处整数 builder 统一换成 `zan_add`/`zan_icmp`
  等包装（先符号扩展到宽侧）。
* **A0-2** ✅ 已修（2026-07-28）**按声明类型的真实位宽 lower**。
* **A0-3** ✅ 已完成（2026-07-28，随 A0-1 / A0-2 / A2-2 落地）结构体字段布局。
* **A0-1a** ✅ 已修（2026-07-28）把标准库里真正的 64 位量从 `int` 定型成 `long`；
  顺带修窄化诊断对 `byte b = 255;` 误报（与 C# 不符）。
* **A0-1b** ✅ 已修（2026-07-28）三个协议编解码器里"用 `int` 装 64 位量"的残余。

## A1 `Span<T>` 编译器内建 —— ✅ 全部完成（2026-07-28）

* **A1-1** ✅ 已完成（commit `6ebffc1`）`Span<T>` 值类型 `{ i8* base, i64 len }`
  （不入 ARC）；`arr.AsSpan([start[,len]])` 零拷贝建视图、`s[i]` 直接 typed load/store
  （不经 `NativeMemory`）、`.Length` / `.Slice`。用例 `span_intrinsic` 三档全绿。
  【待进】noalias 元数据未加；传参/返回 Span 的 ABI 路径待 A2（已随 A2 落地）。
* **A1-2** ✅ 已完成（2026-07-28）`NativeMemory` 的 Get/Set 位宽 intrinsic 整排删除
  （stdlib 声明 + `irgen_expr.c` 的 `nm_load/nm_store` + selfhost 的 `Nm*`），
  132 处调用点改成 `new Span<T>(addr, len)[i]`（补 `align 1` 保非对齐契约）；
  selfhost 编译器同步补齐 `Span<T>`。
* **A1-3** ✅ 已完成（2026-07-28，`dd498ff`）`byte[]` 作为全库统一的字节缓冲类型
  + `s.ToBytes()` / `b.ToStr()` 桥接，详见 B2。

## A2 FFI ABI 分类 〔依赖 A0〕

* **A2-0 ✅ 已完成（2026-07-28）声明位宽逐符号核对**。工具 `_scratch/a2/`：
  864 个 extern 与仓库自有 C 符号（245）和 Windows SDK/CRT 头 clang AST（480）
  两边比对 **0 处不一致**。分三批修：A2-0a（`0267829`）运行时 shim 状态/布尔返回回
  `int32_t`、真 64 位量 Zan 侧改 `long`（顺带 `Stopwatch` 溢出修复）；A2-0b（`e577ce2`）
  GUI/SDL 原生层 176 处标量降 i32、指针句柄改 `intptr_t`/`nint`；A2-0c 系统库 96 处
  `size_t` 车道改 `long`、Win32 窄返回按真实宽度。用例 `ffi_widths.zan`。
  **未覆盖**：libpq/OpenSSL/sqlite3/ODBC/POSIX crt/直连 SDL3 无头可比对，装上头后重跑
  `sys_audit.py` 闭环。
* **A2-1 ✅ 已完成（2026-07-28）结构体按值传参/返回**（`irgen_abi.c`）：extern 声明里的
  结构体生成真符号 + 内部 thunk；Win64 / SysV AMD64 / AAPCS64 按平台 C ABI 分类，
  其它 target 遇按值结构体明确报错。与 clang 对 27 种形状 × 4 target 的声明逐条比对
  **0 处不一致**；用例 `tests/abi/struct_abi.zan` + `abi_signatures_*`。
  顺带修 `float` 槽缺 `fptrunc`/打印缺 `fpext`（`float_widths.zan`）。
* **A2-2 ✅ 已完成（2026-07-28）`[StructLayout(Explicit)]` + `[FieldOffset(n)]`**：
  显式布局整体降成「对齐载体 + 字节块」，字段按偏移字节 GEP 定址；缺偏移/未对齐/
  带虚方法都会报错。用例 `explicit_layout.zan`，708/708。
  **未做**：`Pack = n`、顺序布局类型的 `[FieldOffset]` 校验、stdlib 硬编码偏移改声明式（B5）。
* **A2-3** [x] **变参已修（2026-09-14）**：`[DllImport(..., Variadic = true)]` 落地——声明参数即 C 变参被调方的固定前缀，调用可传更多实参落入 `...`。穿线：parser 解码命名属性 → `method_decl.is_variadic` → arity 三口径（checker `method_arity`/`method_accepts_argc` + irgen `method_accepts_arity`）放行超参调用并保留"至少 N 参"下限诊断（qualified 报源码级，bare 调用仍落 LLVM verifier=既有缺口）→ 声明点 `LLVMFunctionType` 带 isVarArg 且跳过 struct-ABI thunk → 尾参 C 默认提升（bool→0/1 i32、窄整→sext i32、f32→fpext f64，i64/指针原样）在 `coerce_args_to_params` 与 irgen_call 全局函数路径两处。conformance `dllimport_variadic`（printf：等参/超参/运行期 string/double/返回值）4 孪生绿；byte_buffer 等 extern 用例回归绿。契约文档 docs/ABI.md §6.4。（`params` 已支持，属 C# 级变参，与 FFI 变参是两回事。）
* **A2-4** [x] **半边完成 + 半边拒做（2026-09-14 终判）**：`signext`/`zeroext` 已按声明类型自动贴 LLVM 属性
  （`irgen.c:419`、`irgen_abi.c:348/350`，commit `e6b01d53`）；`stdcall` / `CallConv` 属性**拒做**：`zanc --list-targets` 16 个目标（win-x64/win-arm64/linux-*/macos-*/wasm32/riscv*/android-*/ohos-*）无 32 位 Windows，每个目标都只有单一固定调用约定（x86_64 按宿主为 SysV 或 MSVC-x64，aarch64/wasm/riscv 各自唯一）——CallConv 属性在所有支持目标上是零语义空间的死属性，无可 exercised 的行为差异；objc_msgSend（含 arm64 无 _stret）与 COM x64 均走各平台唯一约定。若未来加 win-x86 目标再议。B5 后端真正门槛见下判定行，与 CallConv 无关。
* **判定**：X11 的 `XEvent` 按字段直读、SDL 的 `SDL_FRect` 按值传、
  `objc_msgSend`（含 `_stret` 变体）能调通。第三条通了，mac 后端就不再是"搬不动"。

## A3 `zanc bindgen` 〔依赖 A2〕

[ ] 未开始。从 C 头生成 `[repr(C)]` struct + extern 声明；仓库里已有 C 前端可复用。
一次覆盖 Xlib / SDL3 / FreeType / Win32，并给 `stb_image` 一个
**"链接预编译库而非重写"** 的正当出口——链接第三方库不算"用 C 写"。

## A4 无运行时执行模式 〔依赖 A0〕

* **A4-1** ✅ `[NoRuntime]` 方法。checker 拒绝 `new` / 字符串拼接 / 插值 / lambda /
  `throw` / `try` / `lock` / `foreach`；irgen 不插 retain/release。用例
  `no_runtime.zan`（能跑）+ `diag/no_runtime_alloc.zan`（分配必须编译失败）+
  `run_no_runtime_ir.cmake`（托管版必须有 ARC、`[NoRuntime]` 版必须没有）。
  （`unsafe` 仍只是 `parser.c:232` 的一个 modifier bit，`TK_UNSAFE` 全库 4 次、无语义。）
* **B7-4** ✅ dispatch 队列首次使用竞态（`rt_sync.c`）：Windows 上"谁先看到 `g_dispatch_ready`
  为 0 谁就 `InitializeCriticalSection`" ⇒ 两个后台线程同时初始化同一 section；改用
  `INIT_ONCE`；用例 `dispatch_first_use.zan`。顺带把 `zan_atomic_int` 的所有权契约写进
  `rt_sync.h`。（2026-08-08 复核：`rt_sync.c` 现另有 3 处 INIT_ONCE 惰性初始化，均确认。）
* **A4-2** 🟡 外部线程 attach / detach。attach 本来就是隐式的：线程第一次用到
  EH 状态时 `__zan_eh_state()` 会给它建块。缺的是 detach ——
  块从来不释放，槽位表只有 1024 个且用尽即 `exit`：**1200 个同时存在、各抛一次异常的线程直接
  "too many live threads for the exception-handling table" 退出**；即使线程 id
  被 OS 回收（Windows 上顺序起线程就是如此），每个新 id 仍泄漏一个状态块加它的
  chunk（2000 个顺序线程实测峰值 **102.3 MB**）。
  现在 `__zan_eh_release()` 把槽位改写成墓碑（可重用、但探测链不断）并释放块和全部
  chunk；`zan_thread_trampoline` 在 body 返回后调用它，同一程序峰值降到 **3.9 MB**。
  外部线程（X11 / SDL / Cocoa 回调）可以调 `zan_thread_detach()` 自己交还槽位。
  用例：`tests/conformance/thread_eh_slots.zan`（3000 个线程各抛一次，必须跑完）。
  **仍未做**：非 EH 的每线程状态（目前只有 `rt_sync.c` 的 `zan_shared_string`）没有
  统一的 attach/detach 钩子。
  **2026-09-14 余量重定性**：`zan_thread_detach()` 目前在 stdlib **零受益者**
  （`grep -rln zan_thread_detach stdlib/ src/runtime/` 只命中 rt_sync.c/.h 定义处）——
  今天的全部"外部回调"其实都跑在 Zan 托管线程上：CEF 回调到达主线程（CefRuntime
  无 Thread.Start），Input/Hook 的钩子线程与 TrayIcon 泵线程都是 `Thread.Start`
  起的 Zan 线程（trampoline 已自动释放，102.3MB→3.9MB 即其对账），A52-7/A78-2
  动态哈希表也消除了 1024 硬顶 exit 失败。真正的异质线程只随 B5（X11 事件线程 /
  SDL 音频回调 / Cocoa main）出现，接线作为 B5 的开工前置执行，本条不再单独立账。

## A5 SIMD 〔依赖 A1，可选〕

[ ] A1 之后若 LLVM 自动向量化已够用则跳过。

## A6 编译器对外 API 〔工具链 Zan 化的前置〕

[ ] `lsp/intellisense.c` 要用 parser + binder，`dap` 要用调试信息。
给 compiler 一个稳定 C API（A3 顺手生成绑定），或走自举（`docs/BOOTSTRAP.md`）。
前者见效快，后者是终局。

## A7 泛型 —— ✅ 全部完成（2026-07-27）

原问题：通过类型参数调用约束接口方法会崩 codegen（LLVM 校验失败）。根因不是约束，
是泛型方法/泛型类还会发射一份"擦除"实例，体内 `c.Name()` 无从解析落 `ret i32 0`。
修法：`method_is_tp_template()` 识别穿过 `T` 取成员的方法——这类体没有擦除形态，
泛型方法不再发射擦除实例、泛型类擦除变体函数体换 `abort()`+`unreachable`、
未限定调用补 `try_method_spec`、`infer_expr_type` 按接收者实例化替换字段类型。

* **A7-1** ✅ 已修（2026-07-27），回归 `generic_tp_member_call.zan`，547/547。
  （2026-07-30 回填：普通类上的实例泛型方法已在 **A29-1** 完成；当前剩余仅
  泛型类的实例泛型方法 `Pool<T>.M<U>()` 与 async 泛型方法，见 **A32-3**。）
* **A7-2** ✅ 已修：构造类型上的静态成员访问（`Box<int>.Create(7)` 等），
  `generic_static_member_access.zan`。
* **A7-3** ✅ 已验证、结论变化（2026-07-27）：泛型累加器本身不漏，复验撞出 A7-5。
* **A7-4** ✅ 已修：不带花括号的单语句体（`parse_embedded_stmt()`，C# 作用域语义、
  CS1023 式报错），`stmt_braceless_bodies.zan`。
* **A7-5** ✅ 已修（2026-07-27）：泛型类的字段在 `T` 绑定引用类型时取值错误/堆损坏，
  **四个独立缺陷**——字段写入用声明类型而非实例化类型、返回类型未按接收者替换、
  析构器只按类符号成键（改按 类符号+实例化类型 成键）、临时接收者字段读不释放。
  回归 `generic_ref_type_fields.zan`，588/588。

## A8 ARC / 异常 / 协程交互 —— ✅ 全部修复（2026-07-27 ~ 07-30）

原始三份 repro（`docs/bugs/`）全部实测修复，且比文档记录的更严重。逐项：

* **A8-1** ✅ 已修 `await` 在 catch 循环内 → 原 access violation；`async_await_in_catch`。
* **A8-2** ✅ 已修 dict 的 urldecode key → 原 use-after-free + 堆损坏；`async_dict_urldecode`。
* **A8-3** ✅ 已修 抛出帧与同帧 try 的持有型局部泄漏（在抛出点释放）。
* **A8-4** ✅ 已修 `foreach` 循环体内 `await` → 原编译期崩；`async_await_in_foreach`。
* **A8-8** ✅ 顺带修 `foreach` 里的 `break` / `continue` 一直被静默忽略。
* **A8-9** ✅ 顺带修 `var x = await F()` 会把引用类型结果当整数。
* **A8-10** ✅ 顺带修（测试基础设施）每跑一次 ctest 都重复编译 165 个用例。
* **A8-11** ✅ 顺带修（测试基础设施）缓存会把一次偶发的坏产物永久固化。
* **A8-12** ✅ 已修 异常穿过中间栈帧时，中间帧的持有型局部泄漏——采用**槽位影子栈**
  （条目存变量槽地址而非值，throw 处 longjmp **之前** unwind 到目标 handler 深度）；
  回归 `throw_releases_middle_frames.zan` + `async_sync_sandwich.zan`，550/550。
  边界已随 A19（动态增长）、A20（chunk 存储）清完。
  **代价量化（2026-07-29）**：裸循环 10381 µs，包进空 `try` 后 26878 µs —— **2.6x**，
  每次进 try 约 5.5 ns。终局应换 LLVM 真 EH（invoke/landingpad + personality），
  改动面大、暂不动，**见 A32-5**。
* **A8-13** ✅ 已修 catch 里再抛在 async 体内跳错 handler + 被放弃的 handler 泄漏异常
  （三个独立缺陷：`eh.land` 分派被覆盖、抛出点释放后存回已释放指针、四条离开 catch
  的边不释放捕获异常）；`async_catch_rethrow.zan`，568/568。
* **A8-14** ✅ 已修 异常落地时用帧覆盖栈槽，丢掉最后一次挂起之后写的局部
  （三处 `emit_async_reload_slots()` 全部删掉，alloca 才是活拷贝）；`async_throw_across_frames.zan`。
* **A8-15** ✅ 已修 表达式里的 `await` 被当成 `int`（`anf_hoist_await` 硬编码
  `int $awN` → 改推导类型，连带修正所有权语义）；`async_await_expr_types.zan`，588/588。
  顺带修掉 SQL Server 驱动 leakcheck 里 4 个对象泄漏。
* **A8-5** ✅ async 覆盖面探针矩阵完成（`_scratch/async_probe/`）——try/finally/switch/
  循环/跨协程传播主干全通，**判断：定点修复，不是推倒重做**。
* **A8-6** ✅ 已排除的猜测已记录（"setjmp 栈越积越高"不成立；`anf_stmt_contains_await`
  漏 `AST_TRY_STMT`/`AST_SWITCH_STMT` 不是崩溃根因——后者作为低危补漏在 **A28-1** 补齐）。
* **A8-7** ✅ 设计文档与实现的落差已同步（2026-07-30）：`docs/ASYNC_CPS_DESIGN.md`
  改为"当前实现 + 历史路线"分层，新增 exception propagation across suspension。

---


# B. 标准库

## B1 大体量模块的改造（**都不搬走**）

* **B1-1 ✅ 已完成（2026-07-29）：Sdk/Jd 代码生成器验证 + 清理。**
  `scripts/generate_jd_sdk.py` 从 724 个 C# 源文件生成 467 个 Zan 文件，删除重跑
  字节一致；`conformance_sdk_jd` 等 **4/4 Passed**。手写公共层与生成层目录隔离。
* **B1-4 ✅ 已完成（2026-08-03）：System 能力补齐五模块。**
  - `System.IO.FileInfoEx`：文件版本/图标/MIME/时间戳/硬链接计数；
  - `System.IO.MemoryMappedFile`：`zan_mmap_*` shim + Zan 封装，文件映射与命名共享内存；
  - `System.Text.Pinyin`：GB2312 6763 汉字 → 拼音/首字母；
  - `System.Input.Background`：后台输入模拟（FindWindow + PostMessage/SendMessage）；
  - `System.Globalization.Lunar`：公历↔农历（1900-2100 全部 73384 天逐日核对 0 误差）。
  - 编译器配套修复：`Dictionary.TryGetValue(out T)` lowering、`Convert.ToString(string)`
    不再 `%lld` 打指针。
  - 后置 ✅ `System.IO.Compression`（Deflate/GZip/Zip/Tar，纯 Zan，与 Python 双向互操作）；
    顺带给 `File.zan` 新增 `WriteAllBytes(string, byte[])`。
* **B1-2** [ ] `stdlib/Game`（**2026-08-08 复核**：47 个 .zan 文件 / 554KB / 1.47 万行代码，
  `///` 文档注释仅 **146 行**——文档覆盖率约 1%，是全库最低）：
  `Zgm`、`Arpg`、`Rts`、`Scene`、`Cards`、`Board`、`Arcade2D`。
  **先补公开接口文档再谈提炼**，没有接口文档就重构 1.5 万行是盲改。
  提炼候选：`Zgm/UiRuntime.zan` 与 `Arpg/UiRuntime.zan` 同名同量级，
  先查两者是不是在做同一件事（若是，合并到 `Game/Foundation`）。
  其余候选：公共游戏运行时、实体/组件模型、资源与项目模型、解析器、渲染层。
* **B1-3** [ ] `stdlib/Gui`（103 文件 / 1.9MB / 3.8 万行）是 UI 框架，不搬走，
  但内部模块边界需重划（见 B3-2 / B3-3）。

## B2 字节缓冲：收敛 "calloc 返回 string" 的用法 —— ✅ 全部完成（2026-07-29）

* **B2-1 ✅ 已完成（2026-07-29，第十二批收尾）** `extern string calloc(...)`：
  **stdlib / src / examples 均已清零**。raw `calloc` 缓冲全部改成 `byte[]`，
  分批推进：Path/File/Directory → 字符串原语 → 压缩/图像/音频（rts_*）→ 序列化
  （json/zandb/hex）→ 加密 → Net 全链（Socket/Mqtt/WebSocket/Worker/Tls/WebApp）→
  Diagnostics / IO.Directory / Text.Encoding / CSPRNG / ResourcePack → System.Data 全驱动
  + Wechat / RsaKey / Gui.Text / examples/game/ra2 / Zgm / Windows.Forms。协议数据一律带显式长度，
  不靠 NUL；OpenSSL/libpq/sqlite3/ODBC 的 opaque handle 仍是 native 指针。
  〔实测〕`byte[]` 与 `string` 布局兼容，下游 `string` 形参不必全翻；
  `examples/net` 编译运行正确、多批 ctest 子集全绿。
  （备查：`examples/game/ra2` 的 `h_rules` 哈希负数与 `& 0xFFFFFFFF` 掩码失效有关，
  与 B2 无关，按用户「RA2 先不管」暂不处理。）
* **B2-2 ✅ 已完成（2026-07-29）：`stdlib/System/IO/Path.zan` 全部脱离手动缓冲。**
* **B2-3 ✅ 已完成（2026-07-29）：字符串原语 + `atoi`/`atof` 全部换成 Zan 实现，**
  删掉相应 crt import。
* **B2-4 ✅ 已完成（2026-07-29）：`[DllImport("crt")]` 全库盘点 + 分类，删掉死 import。**

## B3 提炼：消除结构性重复

* **B3-0 ✅ 已完成（2026-07-27）** 驱动失败语句改为抛 `DbException`
  （Firebird / MySQL / Postgres / SQL Server / ZanDb，带驱动错误号和 `DbProvider` id）。
* **B3-1 ✅ 已完成** 新增泛型 `PoolCore<T>`（`stdlib/System/Data/PoolCore.zan`），
  7 个驱动池共享簿记，各缩到 120~154 行。前置 A7-1 已解除。
* **B3-2 🚧 进行中（大重构，逐方法拆、每步跑对应子集测试）。**
  >150 行的方法一度降到 22 个；**2026-08-08 复核：现 36 个**（B7-5 新增的
  `GenDbEmit.GenEntity` 436 / `GenDbEmit.Run` 274 / `GenDb.WhereMethod` 163 等计入）。
  已完成的拆分：`ZanIDE.Run`（已拆为 15 个 `Init*` 方法）、`StyleSheet.Decl`（175→11）、
  `DataTable.Layout.LoadLayout`（153→~20）、`CodeEditor.Intelli.HandleInput`（215）、
  `Arpg/Project.Validate`（243）、`Zgm/Project.Validate`（214）、`Tabs.Render`（192→~90）、
  `Layer.RenderActive`（174→~144）、`App.RenderChrome`（181→~45）、
  `Event.RenderThemeDrawer`（467→~110）、`views/DocsPage.BuildDocs`（225）、
  selfhost 发射器（`EmitListRuntime`/`EmitStrOpsRuntime`/`EmitDictRuntime`/
  `EmitAsyncRuntime`，gen2==gen3 字节一致）；`CodeEditor.Render` 823→~510（随 B3-3）。
  **剩余最大件**：`ZanIDE.Run()` **3350 行**（`src/ide_zan/ZanIDE.zan:1561-4910`；
  `Main()` 已是 22 行的薄壳）需拆成 per-panel/per-ribbon 处理器，自成一项工程。
  **判定不机械拆**（割裂内聚逻辑或需引入状态对象，留给专项编译器重构）：
  `FilePicker.RenderContent` 561、`SceneDesigner.RenderCanvas` 434 / `RenderInspector` 173、
  `AssetManager.Render` 364、`Designer.Form.PreviewDisplay` 269、`App.ProcessEvent` 238、
  selfhost `irgen_expr.GenCall` 377 / `GenBinary` 216 / `GenLambda` 153、
  `irgen_async.GenAsyncMethod` 164、异步握手 `MySql.doConnect` 176 / `Firebird.authenticate` 161、
  `IconVector.Draw` 232。
  ⚠️ **验证方法**：Gui widget 重构一律以 `scripts\build_ide.ps1`（`IDE_BUILD_OK`）为准，
  conformance 仅作补充（stdlib 预编是惰性的，可能抓不到错误）。
* **B3-3 ✅ 已完成（2026-07-29）：重划 DataTable/CodeEditor 模块边界。**
  DataTable 3 个巨文件 → 13 个内聚 partial；`RenderSource`（原 ~1537 行）引入
  `DataTableFrame` 几何结构体 + `ComputeFrame` 抽序言，主体逐字未动；
  交互内核（~700 行单一事件状态机）按设计保留一个方法。`gui_gallery` 远程桌面
  逐帧截图回归 6 项全部通过。CodeEditor 本就 5 个 partial，另抽 `RenderLines`/
  `RenderOverviewRuler`，`Render` 660→~510。
* **B3-4 ✅ 已完成（2026-07-29）：提炼 widget 公共绘制层。**
  新增 `Canvas.SurfaceRoundRect`，收敛 31 处"FillRoundRect + DrawRoundRect"同几何 pair
  （14 个控件）；FloatButton 条件描边保留。落点在 `Canvas` 而非 `Theme`（各调用点参数不同）。

## B4 平台与错误处理 —— ✅ 全部完成（2026-07-29）

* **B4-1 ✅** 删掉两个从未被引用的占位绑定桩（`Platform/Windows.zan`、`Posix.zan`），
  保留实现完整的 `Platform.Runtime.zan`。
* **B4-2 ✅** 约定确立 + 全库排查：真错误抛异常、`Try*` 返回 bool/null、C# 哨兵值保留；
  stdlib 20 处 catch 无一真吞错误；**未机械改写约 700 处合法哨兵返回**。
* **B4-3 ✅** 核实结论：正则实现只有一份（`System.Text.RegularExpressions/`），
  无两套并存，先前条目描述过时。

## B5 GUI 迁移（在 A 项能力就绪后穿插）

判定边界不是"碰不碰 OS"：`Win32Shell.zan` 已证明平坦 C ABI 的 user32/gdi32
能在 Zan 里重写（连 WndProc 都是 delegate）。真正的阻塞是 union / 宏 API /
header-only 库 / C 回调 / 结构体字段偏移，全部对应 A2 / A3 / A4。

* **B5-1 ❄️ 冻结**（2026-07-29 定调）——光栅器搬 Zan（`gui_runtime.c` **2026-08-08 复核**：
  **41 个导出** / 79.7KB，比记录时 28 个/54KB 又增加了 stat_*/blur_*_cached/image_* 等导出）。
  纯整数逐像素循环，Zan 版最好也只是**持平**，性能零收益；而代价是一次性破坏 ABI
  （surface 所有权 + 文字导出签名 + 三条 present 路径同批改），且 X11/SDL/macOS
  三个后端无法在开发机实测。**迁不彻底就不迁**——等 A2 与 B5-5/B5-6 后端就绪后
  一次性做。
  - ✅ 阶段 0 基准已做（2026-07-29）：`tests/gui/raster_bench.zan`，C 版基线
    `blur_rect` 800×500 r=24 = **3916 µs/op**、`fill_radial` r=300 = **1469 µs/op**、
    `blit_image` 256×256→512×512 = **623 µs/op**；Zan 版验收线 ≥0.9x。
  - 移植真实边界：surface 表还被 C 文字/字形渲染与 SDL/X11/Cocoa present 共用，
    必须同批改文字导出签名与三条 present 路径；`blit_image` 挂 stb_image 解码缓存
    （解码留 C，只搬采样循环）。
  - 〔教训〕签入的 driver 二进制不会跟 `cmake --build` 走：改 `gui_runtime.c` 的
    导出/参数宽度必须跑 `scripts\build_gui_driver.ps1` 更新
    `stdlib/Gui/drivers/win-x64/`；曾因 7-27 存货 DLL 让 `conformance_gui_icon`
    跑 331 秒（`92d5826` 定位），SDL3 侧同样栽过（`stage_sdl3.ps1` 重编后恢复）。
    **遗留**：`SdlRenderer.DrawTextureRotated` 把角度打包进宽度高 32 位，A2-0b 后
    宽度是 i32、角度必然丢成 0（当前无人调用）；要修得加独立 `angle` 参数导出，
    届时六平台驱动都要重编。
* **B5-2 🟡 大部分完成**（`067ec4e` 删掉 C Win32 shell 与 link-only shims）。
  `gui_runtime_shims.c` 只剩 macOS 非-Cocoa 构建的 WebView no-op 桩
  （2026-08-27 实测 **23 个**，`gui_runtime_shims.c:29-69`；清单原写 22）。
  **剩余**：macOS 那条分支要么接真 WKWebView、要么让 Zan 侧 `#if` 兜底后整文件删掉。
* **B5-3 ✅ 已完成**（`406c451` gui: draw icons in Zan as vector primitives）。
  `draw_icon` 在 `src/runtime/*.c` 里零匹配。
* **B5-4 ✅ 已完成**（`067ec4e`）删 `gui_runtime_text.c` 的 Win32 窗口壳：
  只剩 3 个文字导出（draw_text / measure_text / font_height）。
* **B5-5** [ ] 〔A2 后〕`X11Shell.zan`：X11 窗口管理导出已从 `gui_runtime_font.c`
  离开，但落在新的 `src/runtime/gui_runtime_x11.c`（2026-08-27 实测
  **32.4KB / 28 个导出**；清单原写 30KB / 25），**仍是 C**。
  注意宏用函数版代替（`XDefaultScreen` 等），不构成阻塞。
* **B5-6** [ ] 〔A2+A3 后〕SDL 后端（window registry / event queue / dirty rect /
  texture 上传）改由 Zan 组织，C facade 退回纯绑定；Cocoa 后端
  （`objc_allocateClassPair` + `class_addMethod` 造类）。
* **B5-7** [ ] 终态：删除 `zan_gui` 和 `zan_sdl3` 两个自建垫片。
  〔2026-08-27 复核，口径：`[DllImport("lib")]` 唯一符号〕`zan_gui` **134**、
  `zan_sdl3` **124**——依赖 B5-1/5/6，仍在增长（2026-08-08 为 109 / 80）。
  `user32`(150) / `kernel32`(93，多为 `EntryPoint` 直绑) / `gdi32`(41) / `crt`(225) /
  `odbc` / `ws2_32` / `imm32` / `dwmapi` / `psapi` 直接绑 OS 的**是正确形态，不动**
  （这几项相对 2026-08-08 是下降，因为计数口径与 EntryPoint 变体的处理不同，
  趋势判断以 `zan_gui`/`zan_sdl3` 两项为准）。

## B6 工具链 Zan 化 〔依赖 A3 + A6〕

**❄️ 整块冻结到 A6 就绪**（2026-07-29 定调）：`src/` 下非 compiler 的 C 共
**2026-08-08 复核：约 256KB**——`lsp/intellisense.c` 94.4KB、`lsp/lsp_main.c` 72.9KB、
`dap/debugger.c` 41KB、`dap/dap_main.c` 28KB、`common/json.c` 16.5KB、`common/rpc.c` 4.8KB。
（`doc/zandoc.c` / `fmt/zanfmt.c` 已是 Zan；`pkg/zanpkg_main.c` 随包管理器整体删除。）
先搬 `json.c` + `rpc.c`（18KB）技术上可行，但唯一消费者 LSP/DAP 仍是 C，
搬了只是多一道跨语言边界——**迁不彻底就不迁**，等 A6 一次性整体搬。

进度：`zandoc` / `zanfmt` 已是 Zan；**包管理器 `src/pkg` 已整体移除**（2026-07-28，
`ae75d50`，`ZANPKG1` 封包魔数与包管理器无关、保留）；`src/selfhost/` 自举编译器
分支属 A6 的"终局自举"路线，独立推进。

* **B6-SH1 🚧 `selfhost_fixed_point` 在 HEAD 已红（2026-08-07 实测，非 B3 引入）**：
  gen1 → g2.ll 失败，根因是自举编译器没实现 `ref` 参数，而 `dbgen.zan`
  （自举编译器自己的源文件）自 `e01975ee` 起用了 `ref int outKind`
  （`AggCall` 1 个签名 + 2 处调用）。探针：
  `build/zanc src/selfhost/*.zan -o gen1 && gen1 g2.ll src/selfhost/*.zan` → 报
  "undefined identifier 'ref'"（dbgen.zan:581/592）；另有一个独立的 stdlib 编译缺口
  "type 'Stopwatch' has no member 'Start'"（Stopwatch.zan:35，`Start` 未被 gen1
  binder 解析）。**2026-08-08 复核**：把 `ref` 改写成 `out` 仍然失败——selfhost
  parser 只在**调用实参**位置认 `out`（`ParseArgs` 特判，专供内建 `TryParse`），
  **形参声明**完全不认修饰符（`ParseParams` 只有 `params`）→ "unknown type 'out'" +
  "no overload takes 2 arguments"；irgen 的 `AK.OutArg` 也只在 TryParse 特例里
  有写回。修法：自举编译器补 `out`/`ref` 形参声明（parser `ParseParams` 修饰符 +
  binder 参数标记 + checker 实参对应校验 + irgen 通用写回，C host 参考 c28d1f61），
  并核实 Stopwatch.Start 的成员解析。自举代码量小（1 签名 2 调用点），
  但这是跨阶段特性，单列任务；在修好前 full 档 `ctest -R selfhost` 允许红
  （smoke/standard 档不含 fixed_point）。
  **2026-08-27 追加分叉**：C host 已把 `params T[]` 恢复成真数组（见 **A51**），
  自举编译器仍改写成 `List<T>`（`src/selfhost/parser.zan` 的 `ParseParams` 重写 +
  `irgen_expr.zan` 打包成 `ival = 2` 的集合初始化器，直通判定用 `IsListType`），
  因此编不出改用 `args.Length` 的 stdlib（Lua/Python 的 params callee）。
  修 SH1 时一并做：删掉 ParseParams 的重写、打包换成数组初始化器（先确认自举
  irgen 支持 `new T[]{...}`）、直通判定改数组。未盲改是因为固定点已红、
  这段改动目前没有任何测试信号。

## B7 runtime 边界复核

〔2026-07-29 实测〕`rt_io.c` **77KB**、`rt_sync.c` 49KB、`rt_sched.c` 10KB /
`rt_mem.c` 7.6KB / `rt_co.c` 2.3KB / `rt_crash.h` 5KB / `rt_wasm.c` 0.8KB。
真 reactor / 调度 / 同步原语 / 内存分配留 C 没问题，但要逐个过一遍：如果混了
HTTP 解析、编码转换、路径处理这类纯逻辑，上移到 Zan。

* **B7-1 ✅ `rt_sync.c` 复核（2026-07-29，`1f12a88`）** 挖出两个真缺陷并已修：
  `lock` 离开 body 不还锁（return/break/抛异常出口，现展开成 finally 区）；
  所有 `lock(obj)` 共用一把全局互斥量（改按对象地址 64 条 stripe 的递归锁）。
  用例 `lock_release_paths.zan` / `monitor_striped.zan`，733/733。
* **B7-2 [~] `rt_io.c` 复核（收尾）** 已修（epoll 路径，本机 Windows 无法实测，
  仅静态修正）：第二个 waiter 的 `calloc` 失败时直接 `return` 污染全局 pending →
  与"槽表扩容失败"同一处理（`io_mark_dead` + 清空 pending）；`io_take` 一次最多取
  8 个 waiter、超出的直接 `free` 掉使协程永不唤醒 → 溢出留在原地。
  **2026-08-27 复核**：两处修复在当前代码里都在（`rt_io.c:939-946` epoll 与
  `:1130-1137` kqueue 同构路径调 `io_mark_dead` 并清空 pending；`:712-734` 溢出链头
  提升进 inline 槽，注释写明 "overflow head moves into inline slot"），`rt_io.c` 内
  无 TODO/FIXME。**剩余只有 Linux epoll 实机验证**；此后 **B7-6 / B7-7** 已对
  `rt_io` 做过二、三轮复核（timer 自取消、异步 DNS 超时、多 worker、IPv6），
  本条目的静态清单已无剩项。
* **B7-3 签入的跨平台二进制会悄悄陈旧（`scripts/check_toolchain_stale.py`）**
  **✅ 已解决（2026-07-29）：12 个 runtime 目标文件不再需要"对应平台"。**
  `zig cc` 自带 musl 与 Darwin 头，一台机器出全部五个目标（`build_linux_rt.sh` +
  `.github/workflows/toolchain.yml` 源码一变就重编 12 个 `.o` 并卡陈旧）。
  顺带修 macOS 上弱引用链接失败（`__zan_eh_release` 弱引用 → 弱定义）。
  **仍未解决：2 个 `libzan_gui.a`（linux-x64 / linux-arm64）**——签入的是胖归档
  （`gui_runtime*.o` + 平台 libX11/libxcb/libXau 成员），树里没有任何地方记录
  它是怎么产生的，保持手工，`--group=gui` 继续报，等有人把配方写下来。
  **2026-08-07 补充**：新增 `gui_runtime_tray.c`（Linux 托盘后端）时，只能在本机
  重编 linux-x64 归档里的 `zan_gui_x64.o`；**linux-arm64 归档、macOS dylib 尚未
  重编**，因此在这两个目标上链接用到 `System.Windows.TrayIcon` 的程序会报
  `undefined reference to zan_tray_*`（macOS 侧的 Zan 分支本来就抛
  `PlatformNotSupportedException`，extern 声明已收进 `#elif LINUX`，只有 arm64
  Linux 受影响）。归档成员里的 libX11 是别的 libc 上编的（`lcFile.o` 引用
  `issetugid`），`gui_runtime_x11.c` 末尾加了弱定义兜底；这仍然是"配方缺失"的
  同一个坑，重编归档时应一并解决。

* **B7-8 ✅（2026-08-08）三项 runtime 修复 + 一个 emutls 死锁根因（探针实测）**：
  rt_timer 竞态（callback 入堆后赋值 / `g_sequence` 锁外递增 / `g_ready_hook` 无锁读 /
  stats 嵌套加锁 → 全部改发布前赋值 + 锁内读，stats 锁内内联计数）；
  rt_mem double-free 哨兵（`ZAN_MEM_FREED` + `zan_mem_hdr_check`，二次 free 直接 abort；
  附赠 MinGW emutls 首次访问递归 malloc 的 once 锁死锁根因，Windows 侧改普通 static；
  `zan_mem_small` 弹块重置 magic/cls 修 free→复用→再 free 误报；WSL 实测
  `rt_mem_dblfree_test` fork+SIGABRT 断言通过，注册 `runtime_mem_dblfree`，Linux CI 生效）；
  rt_sched 任务对象只增不减 → `zan_task_release()`（幂等摘链+free）+ `zan_task_live()`
  测试钩子，所有权契约写入 `rt_sched.h`（rt_test 17/17）。

* **B7-5 ✅（2026-08-08）五个 C 代码生成器整体迁移到纯 Zan**（`stdlib/System/Compiler/`，
  约 6800 行 C → 约 5300 行 Zan + `genmeta.c`/`genrun.c` 约 2100 行 C 通用管道；
  生成文本逐字节一致：form 13/13、scene 往返、route golden、orm 64114/63741/60194 字节；
  关键坑：调用点 children-first 编号 + `Rewrote` 登记 + `ChainEntity2` 穿未重写方法直达根）。
  **2026-08-08 已修其余 13 个 standard 失败（standard 448/448 全绿）**：Arpg 存档类
  恢复（`Game/Arpg/Data/SaveState.zan` + Formula 显式转换 + Project 补 `databases`）、
  PEM 软换行剥离（RFC 7468，`Base64.Decode` 保持严格；`tls_hostname` 归位为真 bug）、
  2 个过时 golden。遗留 `selfhost_fixed_point` 见 **B6-SH1**。

* **B7-6 ✅（2026-08-08）rt_io/rt_sched 二轮复核：8 项清单全部落地**：
  timer 自取消（`g_dispatching`，tick 回调内可 cancel 正在 dispatch 的 entry，删 `heap_rebuild`）；
  double-free 哨兵共用 `zan_mem_hdr_check`（`__wrap_free`/`__wrap_realloc` 同用，
  `zan_mem_small` 弹块重置 magic/cls，见 B7-8）；task 回收 `zan_task_release`（未完成 abort）
  + `task_new`/`timer_add`/`zan_spawn`/`plat_fiber_new` 未查 NULL 全部补 abort（统一 OOM）；
  空闲轮询改事件驱动（`plat_sched_run` 无限等待/按最近定时器限时，去 1ms busy-poll；
  顺带修 `poll.h` 被 `ZAN_CO_DRIVER` 门控而 `zan_io_wait_readable_timeout` 无条件编译的
  glibc 构建缺口、`__GLIBC_PREREQ` 在 musl 下 -Wundef）；
  协程栈池 + guard page（mmap PROT_NONE 底端，`ZAN_CO_STACK_POOL_MAX`=64）；
  kqueue 链表 → fd 槽表（EV_ONESHOT，按 ident O(1) dispatch，select 保持链表）；
  异步 DNS（`zan_io_resolve_co` + `await Socket.ResolveAsync` builtin + eventfd/pipe/IOCP
  三后端唤醒 + `g_dns_inflight` 计入 pending）；统一 OOM abort。
  验证：`async_dns` 双端通过、rt_test 17/17、smoke 101/101、standard 452/453。
  `struct_operators` 预存失败已随本组修复：静态 `operator +(Point a, Point b)` 两个操作数
  都按值传递、无 `self` 指针，`op_is_static` 守卫后通过（`ctest -R struct_operators` 1/1）。

* **B7-9 ✅（2026-08-08）完整实现 C# 风格属性系统**（此前"属性=字段"等价性只是巧合工作，
  自定义 accessor 读未初始化槽位出垃圾值、初始化器被丢弃、只读属性可任意写）：
  parser 合成 `get_<name>`/`set_<name>` accessor 方法（`ast.h` 的 `field_decl` 增
  `getter_body`/`setter_body`/`has_getter`/`has_setter`，NULL body = 自动 accessor），
  自动走 binder→checker→irgen 全流水线；读/写按 `property_getter_sym`（`"get_"`+名）
  分发——`obj.Prop = v` 四注入点（静态/局部/一般/裸 `this.`）、对象初始化器、
  `Prop++`/`--` getter+setter 双调用、复合赋值走 parser desugar；实例/静态属性
  初始化器落地（自定义 accessor 无 backing 槽，初始化器按 C# 忽略）；
  `{ get; }` 只读属性写一律 DIAG_ERROR（checker + 对象初始化器/++ 路径单独补
  `check_readonly_incdec`）。测试 `property_accessors.zan` + `diag/readonly_property_write.zan`；
  smoke 103/103、standard 456/457（唯一失败 `win_tray_screen_smoke` 为 GUI 环境偶发）。
  **gui_runtime\*.c 复核（7331 行，2026-08-08）**：分配点 14 处全部有 NULL 检查或安全
  降级（mac mask calloc 失败仅丢遮罩）；唯一多线程部分是 Linux tray（线程独占 X11
  Display、mutex+condvar、`zan_tray_stop` 先 `pthread_join` 再关 fd）无竞态；
  surface 表 64 上限 + destroy 置空槽、文本缓存单线程逐出无 UAF。未发现缺陷，未改动。
* **B7-7 ✅（2026-08-08）能力补齐：UDP 异步解析 / DNS 超时 / 多 worker 驱动实测**：
  1. **`SendToAsync` 异步解析**（`Socket.zan`）：改 `AsyncResolveIp` + `BuildSockAddrResolved`
     + `WriteReady` 后 `SysSendTo`（与 ConnectAsync 同模式，向主机名发 UDP 不再卡 reactor）。
  2. **异步 DNS 超时**（`rt_io.c`）：`g_dns_pending` 在飞链 + `ZAN_DNS_TIMEOUT_MS`（10s）；
     四后端 poll 等待上限压到最近 deadline；`dns_timeout_scan` 超时交付失败并唤醒 frame；
     生命周期：worker 完成时若已 timed_out 自行 free（reactor 从不释放在飞 job）。
  3. **`--async-workers` 实测抓到 2 个真 bug**（回滚验证 HEAD 驱动下探针静默退出）：
     `co_wait_io` 把 NULL overlapped 的 DNS 唤醒包当普通 wake 跳过 → 补 `dns_drain()`；
     `co_all_idle` 不查 `g_dns_inflight` → 池误判全空闲提前终止。修复后
     `ZAN_CO_WORKERS=4` 下 DNS + 主机名 connect + TCP echo + Delay 全过；
     `tests/conformance/async_mt.zan` 三档注册 `--async-workers`。
  4. **IPv6 全链路**（透明支持，零公开 API 变更）：`zan_io_resolve_sa`（getaddrinfo 完整
     sockaddr 16/28）+ `await Socket.ResolveSockAddr` builtin + `BuildSockAddrAsync` 精确
     长度 + `CreateTcp6/CreateUdp6`（AF_INET6 平台常量 Windows 23 vs POSIX 10）+
     `RecvFrom*` 缓冲 16→32（修 v6 数据报写穿堆损坏）+ `NativeSockAddrIp` 按族格式化 +
     **AcceptEx 修复**（getsockname 探测监听者族，v6 下用 `sizeof(sockaddr_in6)+16`）+
     **hostname 族策略**（名字含 ':' → AF_UNSPEC；否则优先 AF_INET 保持旧行为，
     "localhost" 不再挂起）。验证：`ipv6.zan` Windows IOCP + WSL epoll 双端通过、
     conformance 359/359、async 相关 48 测试全过、smoke 103/103。POSIX kqueue 分支
     按同构人工复核（无实机）；macOS 未测。

---

# C. 文档

`docs/` 根 26 份（另 `archive/` 6、`bugs/` 7、`agent-kb/` 11）。
问题不是日历陈旧，是**内容与实现漂移**。

* **C1 ✅ 已完成（2026-07-29）：`ABI.md` 已按当前实现重写受影响小节 + 加分层横幅。**
  （int 32 位、结构体布局、marshalling、按平台传参分类；§3.3–3.5/§4/§5/§7 标为设计意图。）
* **C2 ✅ 已完成（2026-08-08 收尾）：跨文档的确定性事实漂移全部核完。**
  2026-07-29 修了 `ABI.md` / `DESIGN.md` / `SPEC.md` / `ARCHITECTURE.md`；2026-08-08
  全量审计中 `CODING_STANDARDS.md`（模块依赖图 / `zan test` CLI / 分支策略 / 体积数字
  四处硬错）与 `SECURITY.md`（警示横幅 + 越界行为 / `UnsafeGet` / checked 语义）已修，
  `ERROR_CATALOG.md` / `IDE.md` 已归档（见 C12），无剩余项。
* **C3 ✅ 已完成（2026-07-29）：`STDLIB_ANALYSIS.md` `git mv` 进 `docs/archive/`。**
* **C4 ✅ 已删（2026-07-27）** `STDLIB_DB_AARDIO_REF.md` 残桩。
* **C5 ✅ 已完成** 7 份 bug 文档（含 repro）均纳入 git 跟踪。
* **C6 ✅ 已完成（2026-07-29）：7 份 bug 文档统一 `**Status:**` 字段。**
* **C7 ✅ 已归位（2026-07-27）** 两份 ra2-hd 文档移到 `docs/projects/ra2-hd/`，
  `docs/superpowers/` 删除。
* **C8 ✅ 已完成（2026-07-29）：厘清 4 份路线文档边界**——归档 `ROADMAP.md`，
  给 `EXECUTION_PLAN` / `PRODUCTION_PLAN` / `SELF_CONTAINED_TOOLCHAIN` 加关系头，
  不做「四合一大文件」。
* **C9** [ ] 文档覆盖率差距悬殊。**2026-08-27 实测**（`///` 行占总行数）：
  `Game` 47 文件 / 14751 行 / **1.0%**、`Gui` 177 文件 / 91707 行 / **9.5%**、
  `System` 249 文件 / 81897 行 / **11.8%**。绝对文档量在增加，但代码涨得更快，
  `System` 相对 2026-07-27 的 16.5% 反而下降。Game 仍是全库最低。
  既然 Game 不搬走，就得把文档补上（随 B1-2）。
* **C10 ✅ 作为原则贯彻（2026-07-29）**：以实现+C# 为准改写，写入
  `docs/DOCS_MAINTENANCE.md` §3。
* **C11 ✅ 已完成（2026-07-29）：新增 `docs/DOCS_MAINTENANCE.md`**（分层规则 /
  状态字段格式 / 归档位置）。
* **C12 ✅ 已完成（2026-08-08）：`docs/` 全量真实性审计与清理**（`_scratch/`
  留了复核探针记录；全部关键结论带实测或 `tests/` 佐证）：
  - **归档 4 份虚构/过时文档**：`ERROR_CATALOG.md`（错误码体系与实现不符）、
    `DESIGN.md`、`STDLIB_ZAN_DESIGN.md`（设计稿，非现状）、`IDE.md`（旧 C IDE 已删，
    IDE 是 `src/ide_zan/`）→ `docs/archive/`（现共 6 份）。
  - **重写 2 份权威文档**：`SPEC.md`（关键字补 `delegate/decimal/fixed/goto/lock/
    operator/sbyte/uint/ulong/ushort`；`int`=32 位、`long`=64 位独立、`char`=8 字节字
    （`sizeof` 实测）；删元组 / tagged union / COW / `[CImport]` / `project.zan` /
    `zan` CLI / `"""` 原始串 / `\x\u\U` 转义 / `?.`（行为不标准）/ `$@` 组合；
    `[StructLayout]` 替代 `[repr("C")]`；stdlib 树按实际目录重写；工程章节改为
    `zanc` 命令行 + 内建类型表）；`STDLIB.md`（目录树 / API / 导入机制 / 驱动
    bundle 全部按实现重写）。
  - **状态类更新**：`BOOTSTRAP.md`（固定点 2,106,150 字节已破——`B6-SH1`
    `ref`/`out` 形参缺陷，加警示）、`PRODUCTION_PLAN.md`、`RELEASE.md`、
    `SELF_CONTAINED_TOOLCHAIN.md`（Linux/macOS 已落地）、`platform-targets.md`。
  - **小修 + 警示横幅**：`SECURITY.md` / `CONCURRENCY.md`（设计目标与现状分层，
    修正 `IndexOutOfRangeException`、`UnsafeGet`、泛型 `Channel<T>` 等不存在 API；
    Channel 实为 string-only）、`ABI.md` / `EXECUTION_PLAN.md` / `TOOLING.md` /
    `IDE_PUBLISH.md` / `ai-assist.md` / `ui-driver.md` / `ASYNC_CPS_DESIGN.md` /
    `STDLIB_COMPONENT_STANDARDS.md`。
  - **复核确认无需改动**：`PROJECT_STRUCTURE.md`（目录树与仓库实际一致）、
    `platform-targets.md`。

---


# 建议执行顺序（2026-08-08 更新，只列未完成项）

| 批次 | 内容 | 为什么排这里 |
|---|---|---|
| **1** | **A2-3 / A2-4 剩余**（FFI 变参、stdcall/CallConv 属性）→ **B5-5 / B5-6**（X11 / SDL / Cocoa 后端） | A2 只剩这两项，是 B5 后端的入口 |
| **2** | **A3** bindgen + **A6** 编译器 API → **B6** 工具链 Zan 化 | 仍在 C 的约 256KB（LSP/DAP/json/rpc）的出口 |
| **3** | **A32-4** await 同步完成 fast path → **A32-5** LLVM 真 EH（单独里程碑，最高风险） | A8 补偿层的终局替代 |
| **4** | **A32-6** macOS 实机 + 签名公证发布门（外部阻塞：无 Mac / 凭据） | 发布验收，可与 1-3 并行 |
| **5** | 收尾与能力落地后的配套：**B3-2 剩余**（`ZanIDE.Run()` 3350 行等）、**B1-2 / C9**（Game 文档）、**B5-2 剩余**（macOS WebView 桩）、**B7-2 收尾**、**A4-2 剩余**（detach 钩子）、**A15-5**（switch 清理）、**A34-3 剩余**（`link =` 库引用）、**A33-3**（GUI 事件绑定迁移收尾）、**A43-B**（语法缺失 backlog，按价值排序）、**A43-C1**（跨语句查询按新生成器复核）、**A47-1 / A47-3**（openssl 去重、残留清理）、**A44**（Chart 搁置项） | 收尾与能力落地后的配套 |

**节奏**：每补完一项能力，立刻用它改掉对应的那批 stdlib，拿真实场景验收，
再进下一阶段。不要攒。

---

# A15 语言缺口审计 —— 全部修复（2026-07-27 起，每条都有探针）

标准库里那些"看着绕"的封装，多数是被下面这些缺口逼出来的，每条都实测过。

* **A15-1** ✅ 已修 `1dd023d`：`byte` 是有符号的（按 i8 存、按有符号读，范围 −128..127）。
  修零扩展语义后 B2 才有意义。
* **A15-2** ✅ 已修 `1dd023d`：数组初始化器产出的数组是坏的（`new int[] {1,2,3}` 访问违例）。
* **A15-3** ✅ 已修 `1dd023d`：值类型缺 `==`、运算符重载、`const`（struct 上产出非法 IR）。
* **A15-4** ✅ 已完成：集合元素槽固定 8 字节 → 按 stride 存放，见 **A30**。
* **A15-5** [ ] `switch` 已是能力缺口外的可读性债：2026-08-08 复核 stdlib 已有 **7 处**真
  switch 语句（`Gui/Style.zan` ×3、`Gui/Widget/Spin.zan` ×3、`Gui/Widget/Typography.zan` ×1），
  "全库 0 处"的说法已过时；但 `Gui` 仍有大量 `if (x == "...")` 链，随颜色迁移一起清。
  **2026-09-14 定量与定性**：switch-on-string 本身已被探针证通（返回分支正确），
  债在链体改写量而非能力。链数 Top：Theme.zan **92**、ChartView.zan 33、
  StyleSheet.zan 25、Css.zan 17、Pagination.zan 14。抽样 Pagination/StyleSheet
  显示不少链**混卫语句**（`seg == "total" && showTotal`），并非纯单变量可机械转换；
  Theme.zan 的 92 条全部落在颜色迁移热路径上。结论：本条与颜色迁移**同一批做**，
  单独清链收益低；ChartView 等 Chart* 文件在并行会话在途，勿动。
* **A15-7** ✅ 已修 `1dd023d`：foreach 只能迭代 `List<T>`（数组/字符串按 List 布局读，
  第一个元素就崩——这正是 stdlib 大量写 `while (i < x.Length)` 的原因）。
* **A15-8** ✅ 已修 `a44a807`：数组不带长度（字段/参数上 `.Length` 和 foreach 不可用）。
* **A15-9** ✅ 已修：irgen 用 C 硬编码 43 个静态库调用屏蔽 Zan 实现——现全部加
  `zan_type_defines` 让位判断（Path/File/Directory 已用纯 Zan 重写；`Math`/`Console`/
  `Environment`/`Convert` 的让位已加，**但它们的 Zan 实现本身仍未写**，`Console` 最重）。
  保留内建 lowering 作兜底是有意的（`Math.Min/Max/Abs` 是单条 select/icmp，O0 下
  Zan 版包 libm +13%）。顺带修 `Math.Max/Min` 对 double 崩编译器、`Math.Abs` 固定
  31 位掩码两个真缺陷（`math_minmax.zan`）。
* **A15-6** ✅ 已修（2026-07-29）：`char` 打印成数字（新增 `emit_char_to_cstr()`，
  静态类型为 char 的表达式都走它；`s[i]` 仍按数字打印——索引器返回 char 是另一处
  差异不在本条）；`ulong` 字面量超 i64 上限被夹断（lexer 改 `strtoull`）。
  顺带 `irgen_emit.c` 模块校验失败先逐函数 verify 把函数名写进诊断。

---

# A16 CSS 支持面（现状实测，参考）

> 2026-09-13 第四次更新（选择器三特性收尾）：①属性选择器全面放开——非
> class 属性经 `Control.CssAttr(name)`（Element 带真实属性表，`SetAttr` 键折
> 小写）求值，操作符 `=`/`^=`/`$=`/`|=`/`~=`/`*=` 全套（`~=` 词表近似空格包
> 夹），id 也走 CssAttr；disabled/checked/selected 仍映射状态位；同型不同属
> 性的兄弟节点不再共用样式缓存（Relational 记账，缓存正确性而非 Lint 装饰）。
> ②`::before/::after` 落地——解析为部件（part=name，IsGenerated 收窄为
> first-line/first-letter），`content` 出 Inert 名单进 StyleBox.contentRaw，
> Element.ResolveStyle 经 Style.Part 取伪盒解析（引号串 `\` 转义与 1-6 位 hex
> 码点、`attr(name)` 查宿主属性表、none/normal→空），inline 元素 FlowText 首
> 尾拼接、块容器 FlowEntries 首尾各占匿名文本段。③`:has()` 求值——前导组合
> 器定候选范围（缺省严格后代子树、`>` 直接子、`+` 紧邻后兄弟、`~` 全部后兄
> 弟），内层解析失败/含伪元素部件/嵌套 :has 判 never；已知近似：内层组合器
> 链最左块可落宿主子树之外（`:scope` 严格辖域是后续工作）；未做 memo 缓存
> （CtxSig 不含子树内容会陈旧，v1 直评）。行为断言：css_test 新增
> SelectorFeatures 段（属性七形态/伪文本 FlowText/伪盒 none/块级 pref）。
> bootstrap 语料复测：选择器 70.9%→99.9% live（dead-state 29.1%→0.1%），声明
> 75.3%→95.2% accepted（inert 24.0%→4.1%，content 657 条转正），effective
> 99.4%；仓内皮肤保持 99.9%/100%。css_coverage_audit 的 never/inert 推导同
> 步镜像新引擎（:has 可求值、GENERATED 收窄、content 出 FLOW_ONLY）。

> 2026-09-11 第三次更新（第四轮：定位/层叠语义接入渲染路径）：此前
> position/top/right/bottom/left/inset、z-index、order、overflow 只是"解析进
> 样式盒"，布局与绘制路径根本不读——写而不读=语义失败。本轮真实消费：
> absolute/fixed 脱流（测量与排布两端跳过）、包含块按 CSS 取父 **padding box**
> （top/right/bottom/left/inset 定位、对立边同设用差值、未声明尺寸回退测量
> 偏好）、relative 在流位置上平移（left/right 同设按 left）；z-index 驱动兄弟
> 绘制序（RenderTree 稳定排序）与 HitTest 命中序（同一份顺序的逆序）；flex
> `order` 参与主轴排序；`overflow: visible` 放行溢出（缺省仍裁剪=引擎既有
> 约定，显式声明才改行为，零回归）。顺带修 Selector 解析：type 选择器折小写
> （对齐 `StyleType()`=Kind() 小写；class/id 仍区分大小写）。行为断言：
> tests/gui/css_test.zan 新增 Positioned（坐标+canvas 像素：叠放序、命中序、
> 溢出可见/裁剪）与 Cascade（文档序 tie-break、class>type、type+class>class、
> id 最高、!important 越权重且按文档序、@media 内规则按文档位置参与级联）。
> css_coverage_audit 的 no-consumer 名单相应缩到 white-space 一项。

> 2026-09-11 第二次更新（第三轮 CSS 全面支持）：选择器引擎从"扁平表"升级为
> 复合块链——后代/子/相邻/通用兄弟组合器与结构性伪类（:first/:last/:only-child、
> :nth-child(-of-type) 含 odd/even/an±b、:empty）在 retained 控件树内真实匹配
> （`App.styleCtx` 由 `Control.RenderTree` 推栈，缓存键拼 `CtxSig`），`:has()`
> 按规范判 never 并由 Lint 点名。取值层补齐：长度单位 em/rem/ex/ch/vw/vh/vmin/
> vmax/pt/pc/cm/mm/in/q + `calc()/min()/max()/clamp()`（可嵌套，% 按视口宽）、
> 厂商前缀剥离（-webkit- 等）、空格语法 `rgb(0 128 255 / 50%)`、hue 单位
> deg/grad/rad/turn、`hwb()/oklab()/oklch()/lab()/lch()/color-mix()`（矩阵换算）、
> `currentColor`（ccMask 记账、级联后替换成最终前景色）。at-rule 层：`@media`
> 运行期求值（min/max-width/height、orientation、prefers-color-scheme、
> prefers-reduced-motion、pointer/hover、Level 4 范围语法 `400px <= width <=
> 2000px`），媒体规则平行表存储、`Style.Resolve` 缓存键拼 MediaSig；`@import`
> 按文件展开（相对路径按引入者目录、防环、深度 8、媒体限定导入现算）。bootstrap
> 语料实测：选择器被拒 36.9%→0%、声明不认识 30.6%→0.7%、值被静默强转
> 8.4%→0%、组合器/结构性伪类永不匹配 31.8%→29.1%（余下是 HTML 属性选择器与
> ::before/::after 生成内容，需 DOM 语义，合理保留）。三条承重项（上轮）：
> at-rule 的块按括号配对整体跳过、`var(--x, fallback)` 回退、`!important`
> 级联之后统一再套。皮肤写法细则见 gui-design skill 的「CSS 方言」一节。

**选择器**：复合块链（引擎 `Css.Selector`）：type/`.class`/`#id`/`::part` 可用
后代（空格）、`>`、`+`、`~` 组合成链，复合块内可带 `:state`（hover/active/
focus/focus-visible/disabled/selected/checked）、结构性伪类、属性选择器（任意
属性，操作符 `=`/`^=`/`$=`/`|=`/`~=`/`*=`，经 `CssAttr`/Element 属性表求值）、
`:not(...)/:is(...)/:where(...)`、`:has(...)`（子/后代/兄弟存在性判定）、`*`；
逗号列表。**树上下文**：组合器、结构性伪类、`:has()` 与非 class 属性条件只在
retained 树（`Control.RenderTree`）内命中，无树上下文的即时解析路径它们不命
中（Lint 汇总报告条数）；特异性按复合块累加（`:has()` 取内层最大权重）。
`::before/::after` 是部件（Element 伪文本消费 `content`，见上第四次更新）；
first-line/first-letter 判 never（Lint 点名）。层叠固定顺序：类型 →
`.class` → `#id` → 带状态；同权重按出现顺序。

**at-rule**：`@media` 运行期求值（逗号=或、`not X and Y`、Level 4 范围），
嵌套媒体条件取交；媒体环境 = 视口宽高（designW/designH）、暗色、减弱动效。
`@supports`/`@layer` 同上轮（守卫成立时/摊平）。`@import "x.css";` 文件展开
（Skin.zan 磁盘加载路径已接线）。`@keyframes` 仍整块跳过（`animation` 只能
引用内置 8 条曲线）。`:root` 自定义属性 + `var(--x[, fallback])` 不变。

**属性**（211 个键，Inert 白名单 100 条收下无效果）：盒模型、背景（linear/
radial/conic 渐变）、文本（text-shadow/letter-spacing/word-spacing）、flex 子集、
定位（position:absolute/relative + top/right/bottom/left/inset 布局端真实消费）、效果（box-shadow/opacity/filter/
transform/transition）、cursor 关键词近似映射（grabbing→手型、e/w-resize→横调、
n/s-resize→纵调）。`box-sizing`/`float` 等网页布局属性 Inert。`z-index`/`order`/`overflow`
同样生效（绘制序与命中序一致；overflow 显式 visible 才放行）。

**取值**：颜色 `#RGB/#RGBA/#RRGGBB/#AARRGGBB`（alpha 在前）、命名色、
`rgb()/hsl()` 逗号与空格两语法 + `/ alpha`、`hwb()/oklab()/oklch()/lab()/lch()/
color-mix()`、`currentColor`。长度：像素或单位值（em/rem/ex/ch/vw/vh/vmin/vmax/
pt/pc/cm/mm/in/q）+ `calc()/min()/max()/clamp()`；`%` 在各属性原有通道；单位
缺失按裸数字。`border` 的 style 词生效（dashed/dotted 走虚线，none 归零宽度）。

---

# A44 Web 等价 GUI（进行中）

> 布局引擎 CSS 语义化 → HTML/CSS 声明层 → 设计器统一。计划全文与分期状态：
> [`docs/WEB_GUI_ROADMAP.md`](docs/WEB_GUI_ROADMAP.md)。决策：用户拍板不考虑
> 兼容、彻底改造；声明载体为真 HTML/CSS 文件（data-on-* 事件协议）；游戏与
> 工具共用一套窗口设计器；旧 .zform 废弃。"web 一样"由 scripts/web_oracle.py
> （Chrome headless 与 Zan Arrange 坐标逐盒对比）裁决。

* **P0 布局基建**（2026-09-12）：display 值集扩充——0 legacy（无 CSS 缺省，
  旧 dock/手摆行为，现网零回归）/1 flex/2 none/**3 flow（真块流）**；CSS 写
  `block`/`flow-root`/`grid`(P4 前退化) 落 3，`inline`/`inline-block` 落 3+行内级。
  StyleBox 新字段 inlineLevel/floatSide/clearSide/boxSizing/whiteSpace/
  lineHeightKind；`float`/`clear`/`box-sizing` 移出 Inert 真实解析；line-height
  三态（normal/长度/倍数与 %，千分存储消费端换算）。**border 参与布局**：内容框
  = 框 − border − padding（布局端统一走 StyleInset*，文本端 DrawLabel 扣
  border），box-sizing: content-box 时 StyleWidthIn/HeightIn 反推框宽——此前
  border 画在框内但不占布局的自定义语义废除。UA 样式表（Style.UaCss，web 元素
  缺省 display/字号/margin，垫在装载链最底；与真控件 Kind 撞名的 tag 不给）+
  Element 通用容器（kind=tag）。最小块流 MeasureFlow/ArrangeFlow 骨架（堆叠/
  填宽/margin 全算；塌陷/auto/行盒 P1/P2）。scripts/web_oracle.py：Chrome
  headless 输出 getBoundingClientRect 与 Zan 坐标对比。

* **P1 块流完整语义**（2026-09-12）：**margin 塌陷**（CSS 2.1）——
  pending-margin 状态机（下义务 margin 与下一块的上边距 CollapseMargins
  合并落位：同正取大/同负取绝对值大/正负相加）；首/尾链在分隔容器
  （FlowSepT/B：BFC=flow-root/行内级/真声明 overflow≠visible/absolute，
  或 border/padding 内衬）计入内容高，普通容器塌出容器外；空块
  （无子/无文本/块尺寸为零，height:0 算零）自塌塌穿；"只有空块的 BFC"
  链关在内容框占高（oracle 实测 25px 同 Chrome）。**auto 关键字**——
  width/height auto=保持未声明（此前被当裸数字静默写 0），margin 边
  auto=哨兵 -1（StyleMarX 消费端按 0，排布端读哨兵分剩余空间：
  0 auto 居中/单边 auto 贴边）。**视口模型**——RenderInside 把根塌陷链
  落地为内容偏移 escT（等价 Chrome 的 html 外边距推内容；直接 Arrange
  的子树根丢弃逃逸链，oracle 驱动按同款公式对齐）。**匿名文本块**——
  Element.SetText：TextWrap.Lines 断行 × LineHeightPx 行高测量、
  FlowTextPlace 落位、OnPaint 逐行 DrawRun（text-align 对齐）。
  **BFC 判据**——新增 StyleBox.overflowCss 真实声明通道（-1/0/1）：
  overflow 字段缺省 1 是渲染裁剪约定，曾被误当 BFC 判据导致塌陷整体
  短路（探针定位）。oracle：basic 7 盒 + collapse 8 盒（塌陷/塌穿/BFC/
  auto 居中/文本块）均 0px 偏差；塌陷用例资产 tests/weboracle/
  collapse.json + *_driver.zan 入库。gui_css golden 更新（P0 盒塌陷位移
  + P1 新行），smoke 256 全层仅 HEAD 已知失败（pagination/transfer
  Props 漂移、scrollbar_drag 并发超时单跑通过）。

* **P2 行盒与 inline 流**（2026-09-12）：**Chrome 同源字体度量**——
  `zan_gui_font_ascent/height` 从字体 OS/2 表读 usWinAscent/usWinDescent +
  head.unitsPerEm（GetFontData，tag 需字节交换传 0x322F534F/0x64616568，
  正读返回 GDI_ERROR），`floor(size×units/upem)` 逐项计算 = Chrome
  fontBoundingBox；GDI GetTextMetrics 自行取整（16px 相同、28px asc 多 1、
  14px h 19→18），FreeType 端 ascender/descender 各自 floor 不含 lineGap。
  **行盒**（LineBox.zan）——piece（文本 run/原子盒/strut）+ 贪心折行 +
  `asc = cellAsc + floor(lead/2)`（负 lead 向下取整，Chrome 基线探针实测；
  截断会整体抬 1px）、`desc = lh − asc`、行 A/D 取 max；text-align 分配
  行内剩余空间。**真混排**——`FlowEntry` 文档序（`Element.AddText/AddKid`
  交错记录，FlowEntries 虚方法替换 P1 的 blob-first），块级边界 flush 成段；
  文本 piece 回填 owner 的 run 缓存（InlineRunPlace，Element 多 run 绘制），
  原子盒 vertical-align 落位（baseline 下 margin 边坐基线 / middle =
  **基线向上半个 x 高对中**（浏览器实测语义，CSS 原文是排版向上方向），
  x 高 = 父字体 FontHeight×5/12；top/bottom 对行盒上下）。**行内盒矩形 =
  字型内容区**（基线−FontAscent、高 FontHeight，Chrome inline span 的
  getBoundingClientRect 语义，非行高）。**行内文本继承**（InheritText）——
  段内文本未声明 font-size/line-height/white-space 时随容器 strut。
  **white-space**——normal 折叠/nowrap 单行（prefW=整段宽）/pre 硬分段
  （\n 分行、空行占高）/pre-wrap；解析 0/1/2/3。css_test 新增 DisplayInline
  断言段（ascent/cellh、va/ws 解析、混排盒、pre/nowrap 行为）。
  **验证**：P1 golden 逐行零差异（引擎重构对存量几何透明）；oracle inline
  4 盒（tests/weboracle/inline.json）：y/行高/盒高/原子盒落位 0 偏差，
  x ≤3px（GDI 整数步进 vs DirectWrite 小数步进，台账）；conformance_gui_css
  Passed；smoke 256 仅 HEAD 已知失败 + scrollbar_drag 满载超时单跑通过；
  web_oracle.py 解析器修浮点坐标（Chrome 小数 rect 之前解析崩溃）；
  css_coverage_audit：white-space/vertical-align 移出 FLOW_ONLY 与
  no-consumer 名单（P2 行盒真实消费）。

* **P3 float**（2026-09-12，b432d5c0 + 9ddcffa5）：**float 放位**
  （PlaceFloatKid，测量/排布两端同式）——clear 先推 → 左 float 贴同行
  同侧右缘/右 float 镜像贴右 → 放不下 `NextShelf` 下坠（16 次防呆原地
  溢出）；**CSS 9.5.1 规则 2**——后声明 float 顶边不低于先声明 float
  顶边（oracle 实测：f3 坠到 61 后 f4 不能回 y=1 的空档，被顶到 61 起
  找位、撞 f3 再坠 100）。**行盒绕排**——`FloatIntrusion` 入侵表
  （容器内容框流坐标）经 `SetHostFloats` 按位移换算下传子块，auto 高
  子块带表重测；`LayoutF` 在**行顶处**查 `LineAvail`（Chrome 同款：
  float 贴着行底擦过不收窄本行），piece 放不下且 xOff 越过可用右缘 →
  整行下坠下一搁架、从行首重排；行几何（xOff/availW/yOff）随
  `InlineLine` 记录，**InlineLine.yOff**——下坠后行间有空洞，段高 =
  最大行底、落位 = 段顶+yOff（盲路径 yOff=累加值，P2 几何逐 bit 不变，
  LineAD 整行复查方案废弃：会把行错误滑过贴底 float）。**clearance 不进
  margin 塌陷链**——物理位移在 margin 结算后直接推进流位置（非分隔
  容器首占位块的 margin-top 会塌出容器，clearance 混进 kmt 就跟着丢：
  cbox 50+20=70 依赖此修复，测量/排布两端同修）。**BFC 收编**（CSS
  10.6.7）——flow-root auto 高至少到自己放的最低 float 底；非 BFC
  float 溢出（Chrome 同款）。css_test 新增 DisplayFloat 断言段（17 盒
  几何 + BFC/clearance 标量）；oracle float（tests/weboracle/float.json
  + *_driver.zan）：17 盒 11 精确、6 处 ≤2px 同根因（Chrome strut desc
  进位 vs Zan floor 的逐行累计，台账；x 几何与单行内逐像素一致）。
  台账新增三条：行高分数取整逐行累计、块级 strut font-size 不继承
  （fixture 显式声明，继承链留 P5）、BFC 盒被祖先 float 挤窄未实现。
  conformance_gui_css Passed；smoke 256 仅 HEAD 已知失败
  （pagination/transfer）。

* **P4 grid**（2026-09-12，1b1c5375 + db19356e）：**display:grid 真网格**
  （display=4）——容器模板（grid-template-\*/grid-auto-\*）在 StyleBox 存
  原文串（Clone 直拷、排版时 CssGrid 现解析）；**放置四相**（双显式 →
  行定列自 → 列定行自 → 全 auto 稀疏行主序游标只进不退），占用表行主序
  平铺、按终态行列数一次分配、扩列 Widen 重建；**定尺寸三步**（基尺寸：
  px/%/auto 取跨 1 条目内容最大/minmax 下限 → 非弹性轨均分放大冻结上限 →
  fr 按比例吃剩余；无 fr 时剩余均分给 auto 轨 = Chrome
  align-content:normal 的 stretch）；条目按定宽重测行高（HintWrapWidth
  下传，同 float 模式）；格内 justify-items/align-items 对齐（声明尺寸按
  start，Chrome 同款）；隐式轨道列表**循环取用**；`@supports (display:
  grid)` 转 true（golden 9 行语义翻转）。**oracle grid 19 盒全部 0px**
  （tests/weboracle/grid.json：px/1fr/minmax/repeat、隐式行、双轴 gap、
  span 2、网格线显式放置、auto 列 stretch 均分）。db19356e 修四个确定性
  bug：SpanSum 对 span=0 返回 -gap（前缀和调用形式下所有首格整体 -gap）；
  隐式轨道列表短于隐式轨道数时该循环取用（g2 第二行高塌 0）；Place 输出
  按相序 append 与消费端按 items 文档序配对错位（条目互拿格子，改按下标
  直写）；格子原点复用 SpanSum 少一道 gap（新增 TrackOffset = 前 k 条
  尺寸 + k 道 gap）。**引擎类改名 CssGrid**——`Grid` 与既有
  Widget/Grid.zan 的 Grid : Control 布局组件撞名，ControlFactory "Grid"
  挡位 `new Grid()` 解析到引擎类，10 个 GUI 用例批量编译红（改名后 smoke
  复绿，教训：stdlib 新类名先 grep `class X\b` 查重，ControlFactory 的
  kind 挡位是隐式命名空间）。台账新增五条：span>1 条目不参与轨道内在
  定尺寸、fr 只认整数、命名线/区/负线号/fit-content/dense 按 auto 兜底、
  条目级 justify-self/align-self 未接入、条目 % 宽按 0（块流同款）。
  css_test 新增 DisplayGrid 断言段（20 行）；conformance_gui_css Passed；
  smoke 仅 HEAD 已知失败。

* **P5 HTML 声明层**（2026-09-12，09d73369 + dc3b55f9）：**运行时**
  ——`App.LoadHtml(html)` / `LoadHtmlWith(html, handlers, baseDir)`：
  HTML（整文档或片段）→ 控件树，root 恒为 body（片段包隐式 body）；
  tag 映射容器→Element（UA 样式表给 web 缺省）、button/textarea 捕获
  文本、input→Input/Checkbox、img→Image、select 落 Element 占位；
  属性协议 id→选择器名、class→AddClass、style→合成 `.zgen-N` 规则
  （复用级联/!important）、data-on-\<evt\>="名"→HtmlHandlers 注册表→
  BindEvent（click/dblclick/rightclick/mouse\*/enter/leave/wheel/
  focus/blur/key\* 十三映射，未映射后缀与未注册名字静默不接线）、
  data-bind→bindPath；`<style>`/`<link rel=stylesheet>` 收集并进
  appCss。**引擎修**——Element.FlowText 纯文本回退：display:inline
  元素即使无控件子项（AddText 记录的 elOrder）也构成行内内容按文本
  run 参与父级行盒，不再走原子盒坐基线把行高撑爆（20px 行涨 24 实证；
  混排元素不变，golden 零 diff 证实不伤既有）。**编译期生成器
  GenHtml**——genrun.c 生成器源清单 + `.html/.htm` 认作 design 路径，
  ZanGen design 分派 `GenHtml.Translate`：`.html` 展开成
  `public class UiHtml { static string Css; static Control
  Build(HtmlHandlers); }`（基名帕斯卡化+Html 后缀），调用方
  `app.UseAppCss(UiHtml.Css); UiHtml.Build(handlers)`；生成器 exe 按
  zanc+stdlib+源内容哈希缓存 %LOCALAPPDATA%\Zan\gen，stdlib 变更自动
  重编。**拆层**——`System.Web`（stdlib/System/Web/Html.zan）零 Gui
  依赖纯解析记录层（WDoc/WNode/WItem：tag/父下标/属性/文档序内容表，
  空白塌缩=run 内塌单空格+行内级兄弟间保留+块边界丢弃，实体解码，
  捕获控件语义），`Gui.Html` 只剩记录→控件构建；动机：生成器 exe 活
  名闭包编译不了全 Gui（UiDriver 的 Json.Serialize 拉不进 System.Json），
  拆层后两边吃同一份 WDoc，同构有机制保证。**同构关键**——PushNode
  把子链接写进父 items（文档序内容表），Gui 侧与生成器 EmitContent
  都按 items 单遍游走（AddText/AddChild 交错=行内内容顺序）；真控件
  id 走 SetProp("name")、Element 走 nodeName；style 合成两边同一编号
  顺序。**验证**——oracle html 13 盒（tol 3，唯一非零 #s1 为 GDI 步进
  台账）；端到端 gen_app.zan+ui.html 编译产出 UiHtml 建树，13 盒与
  运行时 LoadHtmlWith diff 全等；css_test 新增 DisplayHtml 段（11 行
  golden：实体/混排/事件槽位/绑定路径）；conformance_gui_css Passed；
  smoke 仅 HEAD 已知失败 + 其他会话在途 Chart 调试行（XEXT）一例。
  主文档 `docs/HTML_UI.md`（元素/事件/样式支持面 + 浏览器差异清单）。

* **P6 overflow 滚动**（2026-09-12）：**per-axis 滚动语义**——StyleBox
  新增 overflowX/overflowY（0 visible/1 hidden/2 auto/3 scroll），
  `overflow` 简写双轴复位、长hand覆盖单轴；单轴声明时另一根 visible
  按规范计算成 auto（StyleOverflowX/Y 消费端）；非 visible 声明照旧
  落渲染裁剪位与 BFC 位（P1 判据不变）。**滚动容器**——任一轴声明
  非 visible 即成立；Arrange 尾段（流/flex/grid/legacy 四条路径统一
  ArrangeScrollTail，非滚动容器零开销）算内容延伸（子项 border-box
  底 + 自身 padding-bottom，换算 padding-box 坐标、下限 client 高，
  absolute 后代也计入）→ 钳 offset → 子树整体平移 -scrollY——渲染
  与命中共用平移后坐标，HitTest 零改动；下一帧自然位置重排不累积。
  滚动容器在 padding box 裁剪（CSS 滚动内容收口于 border 内侧）。
  **交互**——`Gui.ScrollState`（base Gui，非 Widget）：滚轮渲染期
  CaptureWheel 认领（所有权=上一帧末认领者，嵌套最内层赢、指针在
  滚动区落回页面滚动的仲裁链复用），轨道点击/滑块拖动移植
  ScrollView（含拖拽中内容高度重锚定）；滚动条**覆盖式**不占布局宽
  （Chrome 经典条占 17px 布局宽不模拟，oracle 用 --hide-scrollbars
  对齐）；scroll 条带常驻、auto 溢出才出、hidden 无交互但
  SetScrollTop/ScrollTop/ScrollExtent 程序接口可用（Chrome scrollTop
  同语义）。**验证**——oracle scroll 16 盒全部 0px
  （tests/weboracle/scroll.json + scroll_driver.zan：auto/hidden/
  scroll 三态、程序滚动、超额钳制 s3 100→30、嵌套组合 o1 -20 与
  n1 -10 叠加；web_oracle.py 注入解析期 scrollTop 脚本 +
  --hide-scrollbars）；css_test 新增 DisplayScroll 段（7 行 golden：
  extent/钳制/平移/hidden 程序滚动/flex 滚动容器——flex 子项须
  `flex-shrink: 0`，否则被弹性收缩恰好装下不溢出，引擎与 Chrome
  同款）；conformance_gui_css Passed；smoke 同基线（HEAD 已知失败
  + 其他会话 Chart 在途调试行）；UiDriver 实机滚轮
  （ZAN_UI_SCRIPT：click 悬停→认领历史→scroll 注入→子树精确
  -60px）——**坑：滚轮认领要有指针悬停的先帧历史，脚本先 click
  落点再 scroll，否则落页面滚动分支静默无效**。台账新增：无塌陷
  恢复（ScrollView guard 未移植）、scrollbar-gutter/scroll-behavior/
  snap/锚点滚动不支持、水平轴 clip-only。

---

* **P7a 设计器存取格式 = .html**（2026-09-12）：**编解码器**
  stdlib/System/Web/DesignerHtml.zan（纯字符串层、零 Gui 依赖——设计器与
  生成器两端共用）：设计 JSON 文档 ↔ HTML 全键保真往返。编码：文档级键落
  `<body>` 的 data-<kebab> 属性（name 另发 id；body 带 data-zan-design 裸
  标记），字段树变元素树（kind 决定 tag 但 data-kind 是权威标记；kids 递
  归），事件键 on<Event> → data-on-<kebab>（与 P5 运行时事件协议同形），
  复合值（props 直通/columns/winShape）走 data-x-<kebab>='<紧凑 JSON>'（字
  符串值不作数字嗅探，props 的 "min":"0" 不漂成数字），属性值统一实体转
  义。解码逐键还原：kebab→camel、true/false→布尔、数字字面→数字、on- 前缀
  →Pascal 事件键。**踩坑台账**：(a) Zan 的 `char.ToString()` 得到码点数字
  （'M'.ToString()=="77"），首字母大写必须用 char+string 拼接惯用法；(b)
  data-zan-design 是裸属性（值空串），`Attr(...) != ""` 判存在性永远 false
  ——IsDesignDoc 必须按名扫描；(c) head 区语义——meta/style/title 在隐式
  body 创建之前早退（inHead 标志），否则真 body 被当"迟到"丢弃、整文档只
  剩默认节点；head 的 data-* 属性经 pendingHeadAttr 暂存合入 body。**编译
  期通道**：zan_is_design_path 本就收 .html/.htm；GenForm.Translate 增
  .html 设计稿分支（IsDesignDoc → ToJsonDoc → 同一 TranslateOne 投影），
  GenHtml.Translate 对设计稿跳过（防重复生成）；两通道同编译共存实测无干
  扰。**生成器缓存键修复（genrun.c，结构坑）**——键原只哈希
  System/Compiler 下 9 个固定文件，而生成器 exe 是 `ZanGen.zan
  --auto-stdlib` 编的、行为由整个 stdlib 闭包定义：GenForm 新引用的
  System/Web/DesignerHtml.zan（及此前的 Html.zan）改了不重建，出现"stdlib
  已修、生成器仍旧"的幽灵行为。改为哈希 stdlib 全部 .zan（相对路径+内容，
  排序定序，FindFirstFile/recurse 双实现）。**设计器桥**：Designer.Html.zan
  partial——SaveHtml()=FromJsonDoc(SaveJson())、LoadHtmlText()=
  LoadJson(ToJsonDoc(s))；内部模型/Undo 快照/JSON 抽屉仍是文档 JSON。**IDE
  接线**：TabRecord.designDoc（打开时算一次缓存，.html+data-zan-design 才
  进设计器——任意网页不该弹设计器）；designerMode/编辑器同步守卫/载入
  LoadHtmlText/落盘 SaveHtml（LSP SyncDoc 仍推 JSON 保持 LSP 契约）/F7 源
  码视图/Ctrl+S 只读守卫/SaveActive 守卫九处接线；CodeBehindPath/
  FormBaseName 剥 .html；GatherZanFiles 收 .html .htm 进构建命令。**测试**
  ：tests/conformance/designer_html.zan（纯层往返+IsDesignDoc 三态+桥稳
  定+空文档降级为空表单的诚实契约——HTML 没有 JSON 的"损坏"形态，loadError
  通道对 .html 输入基本不可达）+ tests/gui/zform_control.html（真 .zform
  经编解码器转换入库）+ conformance_gui_zform_html（同一 zform_control 测
  试与金标，设计稿改吃 .html——生成的 ZfPanel partial class 逐字节等价）；
  conformance glob 自动注册 designer_html（Gui 检测→gui 驱动），手工注册
  撞名一次。standard 层 394/399（pagination/transfer=HEAD 已知；
  win_automation/tray 复测绿；http_forwarder_stream 层内固定端口争用，
  直跑绿）。**遗留阻塞记档（与 P7a 无关）**：ZanIDE 全量构建在当前 HEAD
  即失败（clean worktree 复现）：ChartView.zan:177-183 四处 "ambiguous
  type 'Action'"——Gui/Event.zan 与 Gui/Reactive/Events.zan 各有一个
  `delegate void Action()`（均 ≥2026-07-15 已在），IDE 闭包内
  count_simple(Action)>=2 触发 nsresolve 歧义改名，改名重写又打断
  using-Gui 的解析（A311 类已锁定未修的改名重写缺陷形态）。二分实测
  92ef35c5、dc3b55f9、2d7da4f1（P5 之前，仅判定未复核错误正文）均
  FAIL——回归早于整个 Web-GUI 工程；画廊 344 文件同一 stdlib 绿、窄探针
  绿 → IDE 显式清单+31 设计稿闭包特有。IDE 侧 .zan 改动经语法级单文件
  检查验证（无解析错误），全量编译待该回归修复后补验；修向=nsresolve
  改名重写根修（ctx_ns 命中即应短路，不得被改名表污染）。

* **P7b 字段内联 style 通道 + Inspector CSS 声明编辑**（2026-09-12）：
  **运行时通道**——`Control.SetProp("style", ...)` →
  `StyleSheet.ApplyInline`：声明文本（"background:#c00; padding:8px"）交
  给整块 CSS 解析器（`Css.Parse(".zan-inline { … }")`，颜色函数/缩写键/
  !important 全复用），块落点复用 ApplySelector 抽出的
  `StyleSheet.ApplyBlock`（布局键 pad/gap/width/height/dock/x/y 落控件
  字段，视觉键 DeclSource+CopyToControl 落 style* 覆盖字段——在每次样式
  解析的 Style.Inline 之后，优先于类规则）；SetProp 后清 computedStyle
  缓存（首绘后改样式也生效）。**三端同落点**：GenForm 发射
  `SetProp("style", …)`、画布预览 FormBuilder.FromField→MakeControl、
  运行期 UiDoc。**设计器模型**：FormField.styleText（"style" 建模键，
  IsModeledKey 同步；名字不叫 Style——与 Gui.Style 类撞，方法体里裸写
  Style.Part 会被字段抢走解析）。**Inspector**：STYLE（CSS）区逐声明行
  编辑（一行一条、× 删、添加声明），每帧重组回 styleText（空行跳过），
  换选区/Undo（ApplyJson 清 sel）安全重建；画布下一帧即时生效。
  **DesignerHtml**：style 键 ↔ 元素 style 属性（非 data-* 通道），.html
  设计稿因此就是合法运行时 HTML（App.LoadHtml 同语义吃 style）。
  **语义例外（台账）**：设计几何拥有布局——发射器 logW/Prefer/logPad 在
  style 之后接管、ApplyDeclaredUnits 每帧重算，故 style 的布局键
  （width/height/x/y/dock/gap/pad）对设计字段只在几何沉默处生效；视觉键
  （background/color/border*/box-shadow/font-size/transition）完全生效。
  这是刻意偏离浏览器 "inline 不败"：画布拖拽手柄/对齐命令基于同一份几何。
  **测试**：conformance/gui_field_style（SetProp 通道+rgb 函数+垃圾值
  不落值+预览路径）、designer_html 扩展（style 往返+桥内 styleText 断
  言）、e2e（.zform style 键→生成代码→实例断言 bg/圆角+几何接管）。
  standard 层回归后提交。

* **P7d 模板统一 .html（全仓库窗口声明单一格式）**（2026-09-12）：
  **批量转换**——templates/gui 12 个 App.zform + src/ide_zan 31 个 .zform
  → .html（转换器三重校验：模型级等价 LoadJson→SaveJson ==
  LoadHtmlText→SaveJson、SaveHtml 再入稳定、IsDesignDoc 判真）。**转换
  揪出编解码器"缺省跳过"的语义 bug**：显式缺省值是有义的（fh:0=矩形
  边界、winZoom:0=适应视口、pad/gap:0=压过样式表缺省、label:""=显式
  清空标签不落 kind 缺省文案）——AppendKey 零值/空串豁免名单
  {fx,fy,fw,fh,winZoom,pad,gap}（数字）+ {label,placeholder}（字符串），
  新键由模型级等价校验兜底。**引用翻转**——13 个 zan.proj entry、
  build_ide.ps1 设计稿 glob、IDE 九文件（New Project 的 App 拷贝/改名、
  RetargetForms 收 .html 并经 ToJsonDoc→RetargetDoc→FromJsonDoc 往返、
  New File 表单/自由表单写 .html（FormDoc JSON → FromJsonDoc）、
  OpenFirst/编译配对/入口解析 .html 优先 .zform 回退（旧用户项目仍
  可编译）、IsDesignFilePath=path-only 双格式判定（设计器开门保持
  TabRecord.designDoc 内容判定，任意网页不弹设计器）、CollectZan/
  SyncFormCodeBehinds/AiAgent 文本与提示词）。**测试**：12/12 模板
  实例化后以 .html 为入口编译通过（gui-components 顺带修掉模板自带
  的 `"; +` 语法 bug）；conformance designer_html/gui_field_style/
  zform 双金标/policy_zform_schema 全绿（zform.doc.json 补 style 键
  说明 + P7d note）。改动的 9 个 IDE .zan 语法级检查干净（单文件
  编译仅剩缺 partial 兄弟的语义错）。.zform 编译通道保留（旧用户
  项目零破坏），仓库内不再有任何 .zform。

# A17-A31 历史修复记录（全部完成，一行摘要）

* **A17** ✅ 测试套件"要跑半小时"的真正原因（2026-07-28）：A17-1 三批测试没设
  TIMEOUT（默认 1500s）→ 现 120s；A17-2 POSIX 后端 `server.Stop()` 后 accept 挂起
  watcher 残留 io 表 → 死 fd watcher 队列（`accept_after_close.zan`）。
* **A18** ✅ C# 裸 `throw;` 重抛（2026-07-28）：parser 接受、每个 handler 多存一个
  tid 槽（async 帧驻留）、不在 catch 内报 CS0156 式错误；`exception_rethrow.zan`。
* **A19** ✅ 展开栈动态增长（2026-07-28）：固定 4096 数组 → 堆缓冲按需翻倍
  （后 A20 改成 chunk 表）；`exception_unwind_stack_growth.zan`。
* **A20** ✅ handler 栈去掉固定 16 层上限，两个 EH 栈都改成不搬移的 chunk 存储
  （2026-07-28，多线程调度器下 realloc 是 use-after-free）。
* **A21** ✅ `finally` 只在正常退出时跑（异常被吞/return/break/continue 跳过它）——已修。
* **A22** ✅ EH 状态改为每线程私有——已修。
* **A23** ✅ async 帧的 catch 槽不再是固定 8 个——已修。
* **A24** ✅ `: base(args)` 的实参临时被泄漏——已修。
* **A25** ✅ `--check-leaks` 的计数器不是线程安全的（A22 遗留）——已修。
* **A26** ✅ 既有失败（与 A21-A25 无关的两项）均已消失（2026-07-29 复核）。
* **A27** ✅ async 帧 result 类型化 + 取消路径（2026-07-28）：A27-1 typed frame result
  （帧 result 槽物理上仍是 64 位，但带类型信息）；A27-2 协作式取消（全部在生成代码里）。
* **A28** ✅ A8-6 补漏 + 两个既有 bug（2026-07-28）：A28-1 `anf_stmt_contains_await`
  补 `AST_TRY_STMT`/`AST_SWITCH_STMT`；A28-2 `switch` 里的 `break` 会跳出外层循环；
  A28-3 `foreach` 数组/字符串 + 体内 await ⇒ LLVM 校验失败（`async_await_in_foreach_array.zan`）。
* **A29** ✅ 实例泛型方法单态化（2026-07-28）：A29-1 实例（非 static）泛型方法
  （`this` 作第 0 参数插进签名；接收者限类，`Pool<T>.M<U>()` 仍报错 → 见 A32-3）；
  A29-2 泛型方法把类型参数转发给另一个泛型调用。`generic_instance_method.zan`。
  **仍未做**：async 泛型方法单态化、泛型类的实例泛型方法（见 **A32-3**）。
* **A30** ✅ 主体已完成：集合元素槽不再固定 8 字节（按 `elem_slot_words()` stride
  内联存放宽 struct；顺带修 `list[i].field` 在 struct 右值上取字段落兜底 `return 0`）。
  `list_wide_struct_elements.zan`，687 项中仅 `conformance_gui_datatable` 既有失败。
  **A30 后续边界（未完成，不回退主体状态）**：宽 struct 元素的 `IndexOf`/`Contains`/
  `Insert`/`Reverse`/`AddRange` 仍给明确编译错误；`Dictionary` 的值槽仍 8 字节。
  后续任务分别扩展这些操作和 Dictionary stride，**见 A32-2**。
* **A31** ✅ macOS 交叉编译解锁 async + 原子（2026-07-29）：`build_macos_rt.sh`
  （zig cc 出 Mach-O runtime）+ `check_macos_rt.py`（符号只导入 libSystem，6/6 ok）+
  `main.c` 按目标挑对象；顺带修 `stdout`/`stdinp` 的 Darwin 符号差异。
  产物带 ad-hoc 签名（`CS_ADHOC|CS_LINKER_SIGNED`），无需证书。
  **仍未做**：① 无 Mac 实机执行验证（GUI dylib 已签入，同样缺实机运行）；
  ② 分发用 Developer ID 签名 + 公证未接（需证书 + App Store Connect API key）→ **A32-6**。

---

# A32 · 审计遗留问题彻底收尾路线（2026-07-30，计划）

## 完成定义与约束

本路线不把「已有明确报错」「保留旧补偿层」「只链接未执行」算完成。最终状态必须同时满足：

1. 字段声明初始化器与所有现有转换边界使用同一套窄化规则；
2. `List<T>` 的公开操作和 `Dictionary<K,V>` 的值槽都支持任意已知布局的值类型；
3. 泛型类实例上的泛型方法和 async 泛型方法都走具体特化，不再有这两类拒绝分支；
4. async-to-async `await` 对同步完成子任务有无竞争 fast path；
5. `setjmp`/`longjmp`、展开临时栈和 async trampoline EH 补偿层最终由目标原生 LLVM EH
   取代并删除；
6. macOS 交叉产物由真实 macOS runner 执行；签名、公证流水线具备凭据后可直接启用。

硬约束：先在 `_scratch/` 做最小复现，根因修在 compiler/runtime，不改写 Zan 代码绕过；
每个里程碑单独提交、单独回归。编译器改动只跑受影响子集；全量测试只作为整条路线的
最终 release gate。


* **A32-0 / A32-1 / A32-2 / A32-3 / A32-7** ✅ 全部完成：失败矩阵基线、字段声明初始化器、stride-aware 集合操作（List 全操作 + Dictionary 宽值槽）、泛型类实例泛型方法 + async 泛型方法统一特化、整数→浮点隐式转换（2026-08-04 ~ 08-08；详情见 git 历史与备份）。

## A32-4 · await 同步完成 fast path 与无竞争握手（L）

不能简单在当前 lowering 后读 `sub.done`：ramp 只分配 frame，且先写 awaiter 再调度是当前避免
丢唤醒的保证。推荐把协议升级为：async ramp 同步执行到首次真实挂起或完成，再由一个原子的
`link-or-observe-complete` runtime primitive 完成 awaiter 登记。该 primitive 必须保证「看到 done
直接继续」与「登记 awaiter 后由完成方唤醒」二者恰有一个发生，多 worker 下也不能丢唤醒或
重复 resume。

**实现/验收**：

* 明确 `DONE` 的发布/获取顺序和 awaiter 写入协议；Windows/Linux/macOS runtime driver 使用
  同一语义，不靠单线程时序侥幸正确。
* 同步完成子任务不调用 `zan_co_ready(self)`、不 `ret void` 挂起；真正挂起路径行为不变。
* 新增无 await 子任务、首步完成、首步挂起、深链、throw-before-first-await、取消、
  `--async-workers` 竞争压力测试；重复运行不得挂起、重复输出或 use-after-free。
* IR 断言同步完成案例存在 done 分支且 fast path 不经过 self suspend；记录优化前后 resume/
  enqueue 次数，作为后续性能回归基线。

## A32-5 · LLVM 原生 EH 迁移并删除补偿层（XL，最高风险，单独里程碑）

这不是把 `setjmp` 名字机械替换成 `landingpad`。Linux/macOS 使用 Itanium unwind 模型；
Windows x64 需要 funclet/SEH 形态。先在 `_scratch/` 用最小 LLVM probe 证明目标 personality、
throw carrier、typed catch、cleanup、rethrow 在 win-x64、linux-x64、macos-{x64,arm64} 均能
生成并链接；任一目标未证明前不改生产 lowering。

### A32-5a 建立目标无关 EH 层

* 新建 compiler 内部 EH abstraction，向 stmt/expr/ARC 只暴露 try region、typed handler、
  cleanup、throw/rethrow；目标 backend 分别产生 landingpad 或 Windows funclet IR。
* exception carrier 继续携带对象、type descriptor 和 owned 标志；personality/type match 与
  现有 typed catch 语义一致，跨模块符号和 runtime ABI 写入 `docs/ABI.md`。
* 所有可能抛出的调用由 abstraction 决定 call/invoke；普通不抛路径不引入运行时 push/pop。

### A32-5b 同步 EH 与 ARC cleanup

* 先迁移同步 try/catch/finally、throw/rethrow、return/break/continue 穿 finally；
  将持有型局部释放放进 cleanup path，正常路径与异常路径各释放一次。
* 同步矩阵稳定后删除同步 `__zan_eh_*` handler stack、`__zan_eh_tmp_push/pop` 和 longjmp 调用，
  不保留双机制作为永久 fallback。

### A32-5c async EH

* unwind 只发生在一次 live resume invocation 内，绝不跨挂起点；resume 边界捕获未处理异常，
  存入 frame exception slots，awaiter 在自己的 live invocation 中重新 throw。
* 迁移跨 await try/catch/finally 后，删除 `emit_async_eh_prologue/unarm`、frame handler stack 和
  trampoline；随后收缩 frame header，并同步 `docs/ASYNC_CPS_DESIGN.md`。
* cancellation、child frame cleanup、异常对象所有权与 finally 恰好一次执行必须保留。

**退出标准**：现有 exception/throw/catch/finally/async EH 的 conformance、determinism、
leakcheck 子集全部通过；生成 IR 不含 `setjmp`/`longjmp`/`__zan_eh_tmp_push`；A8-12 混合栈和
递归深栈无泄漏；空 try happy path 无 handler push/pop，10 轮中位数相对裸循环开销目标
不高于 15%。最后才删除旧字段/函数并跑 release 全量测试。

## A32-6 · macOS 实机、签名与公证发布门（M + 外部阻塞）

当前没有本地 Mac、Developer ID 证书或 App Store Connect 凭据。代码侧仍可完成到
「凭据一到即可启用」，但**不能把未实际签名/公证写成完成**。

1. 在 GitHub Actions 增加跨宿主闭环：Windows/Linux job 用发布版 `zanc` 产出
   macos-x64/arm64 console、async+atomic、GUI fixtures；artifact 传给 macOS Intel/Apple
   Silicon runner，执行并比对输出。GUI fixture 创建窗口、跑一次事件循环后自行退出，并检查
   dylib/rpath 加载；runner 不可用时 job 明确 blocked，不能降级成只看 Mach-O。
2. 增加发布签名脚本与 gated workflow：先签 nested dylib/framework，再签 executable/app；
   secrets 缺失时只做 ad-hoc/dry-run 和结构检查，存在时使用 Developer ID、提交 notary service、
   等待成功、staple，并用 `codesign --verify --strict`、`spctl --assess` 验证。
3. 凭据只放 CI secret；日志不得输出证书、private key、issuer/key id。发布文档列出 secret 名、
   轮换/吊销和本地 rcodesign 备用路径。
4. 最终外部门：取得证书与公证凭据后，对 x64/arm64 GUI 发布包各完成一次签名、公证、staple，
   并在干净 macOS 环境启动。此前 A32-6 状态保持「自动化完成，发布验收 blocked」。


## 依赖与提交顺序（2026-08-08 更新：A32-0/1/2/3/7 已完成，下表只列未完成）

| 顺序 | 里程碑 | 依赖 |
|---|---|---|
| 1 | A32-4 await 同步完成 fast path | 现有 async emitter 已稳定 |
| 2 | A32-5 LLVM 原生 EH | A32-4 之后 IR/frame/layout 冻结 |
| 3 | A32-6 macOS 实机 + 签名公证发布门 | 外部 Mac/凭据；代码侧可与 1、2 并行 |

每个里程碑按「probe → 根因修复 → conformance → determinism → leakcheck → diff/status」闭环；
禁止把多阶段揉成一次提交。A32-5 之前不删除现有 EH 保护网，A32-5 完成后也不允许以兼容名义
保留两套 EH。


---

# A33 · 委托只支持静态函数：捕获 `this` 的 lambda 与实例方法组会崩（2026-07-30，已实测）

**根因**：`irgen_expr.c` 的 `emit_lambda_typed` 明确按「lambda 不捕获」实现
（裸函数指针，无 env/receiver 槽）。于是：捕获局部 → 前端报 `use of undeclared identifier`；
捕获 `this` → **编译过、运行崩**（`this` 在 lambda 体内是 null，0xC0000005）；
实例方法组 `Act a = h.Touch;` → **同样编译过、运行崩**。后两条是静默错误码生成。

**任务**：

1. [x] **A33-1 立刻堵住静默崩溃**（2026-08-02）：`emit_lambda_typed` 内 `this`/`base`
   引用、`emit_expr_member_access` 的实例方法组取值、`emit_ident` 裸实例方法名三处诊断
   （新增 `g->lambda_depth`），四个探针从静默崩溃变为编译错误。原 `diag_a33_*` 四个
   诊断测试在 A33-2 落地后删除（断言的写法现已支持）。
2. [x] **A33-2 委托带 receiver/env（能力项，2026-08-04 已实现）**：见下「A33-2 详情」。
3. [x] **A33-2b 被写入的捕获局部按引用捕获**（2026-08-04 已实现）：见下「A33-2b 详情」。
4. [~] **A33-3 GUI 事件绑定改成实例方法/闭包（依赖 A33-2）**：`lv.OnSelect(this.Open)`
   取代静态 handler + 轮询取值。**2026-08-08 复核：已开始迁移**——
   `AiSettingsWindow.zan` 已有 7 处 `this.` 实例绑定（`OnSelect(this.ProfileSelected)` 等）、
   `Designer.zan` 有 4 处闭包绑定；但主体仍是静态 handler 模式（`OnClick("add")` 字符串式），
   迁移未完成。

### A33-2 详情（已完成 2026-08-04，同时关掉 A43-A5 / A43-A6）

委托值 = 指针两种形态，靠**最低位**区分（`irgen_arc.c` 顶部有完整说明）：偶数 = 裸函数
指针（静态方法/不捕获 lambda，零开销、可直接交 C 回调）；奇数 = 堆上闭包记录带标记指针
`{ ptr fn, ptr dtor, ptr target, <按值捕获的值...>, [ptr this] }`，调用 `fn(record, args...)`。
落点：irgen_arc（`emit_closure_retain/release`、`emit_delegate_invoke` 按标记位分派、
`emit_delegate_equals` 同函数+同 target、`TYPE_DELEGATE` 纳入 RC 管理）、irgen_expr
（捕获式 lambda 生成闭包记录与 `__zan_clo_dtor_*`、实例方法组每方法唯一 thunk
`__zan_mg_<fn>`、`E += obj.M` 记录由处理器列表接管）、irgen（`reserve_closure_site`
每闭包独立泄漏站点）、parser（事件降级出的 `__Event_D` 增加 `static void op_call(self, ...)`，
`E(v)` 即 C# 触发写法，无订阅者时空操作）。
正式测试 `delegate_closures.zan` / `event_receiver_handlers.zan`；`--check-leaks` 无泄漏。

### A33-2b 详情（已完成 2026-08-04）


> A33-1 / A33-2 / A33-2b 已完成（2026-08-02 ~ 08-04）：闭包记录（指针 tag 区分裸函数指针与带 receiver/env 的堆记录）、实例方法组 thunk、事件表接管、被写入捕获局部按引用装箱；关闭 A43-A5/A6。详情见备份。

---

# A34 · zanc 无库输出：不能编译 DLL/.so/.dylib/.a（2026-08-01，已实测）

**根因**：zanc 的链接路径（`src/compiler/main.c`）只支持**可执行文件**输出，三条路径都
固定链接 CRT 启动对象、生成带入口的 PE/ELF，没有 `-shared`/`-dynamiclib`/静态归档分支；
`templates/library/*` 的 `target=dll` 写进了 zan.proj 但编译器从未消费。

**缺口（跨平台全家桶）**：Windows `.dll`/`.lib`、Linux `.so`/`.a`、macOS `.dylib`/`.a`。

**任务**：

1. [x] **A34-1 编译器新增库输出模式（已实现，2026-08-01）**：`-o` 后缀自动切库模式；
   `irgen_emit.c` 仅库模式对 `public` 方法跳过 `zan_set_module_local`（GlobalDCE 后仍
   导出），Windows 共享库发射 `DllMain`（返回 1）。Windows DLL 走 bundled
   `ld -shared -e DllMainCRTStartup` + `dllcrt2.o` + `-out-implib`（`foo.dll`→`libfoo.dll.a`）；
   Linux `.so` 走 `ld.lld -shared`、macOS `.dylib` 走 `ld64.lld -dylib` + libSystem.tbd；
   runtime 对象按需链接，纯函数库零 runtime，cross 共享库需要 runtime 时明确报错。
   测试 `tests/emit_lib/` 四件套（static / windows_dll 含消费者 `[DllImport]` 链接运行

> A34-1 / A34-2 已完成（2026-08-01 / commit 1fde2b0f）：`-o` 后缀自动切库模式，Windows DLL / Linux .so / macOS dylib / 静态库四件套（`tests/emit_lib/`）；IDE 库目标与 Publish 分支。详情见备份。

---

# A35-A42 历史修复记录（全部完成，一行摘要）

* **A35** ✅ 同一方法内多次 try/catch/finally + 调用含 finally 的方法 → LLVM
  "Incorrect number of arguments"（2026-08-02，已修复）：最小探针无法复现（仅 IDE 大模块），
  恢复直接调用后编译通过，判定为被 **A36/A37** 顺带修复（两者都触及 finally 内 throw
  传播路径的调用参数生成）；`LoadAiSessions` 迁移块已恢复直接调用。
* **A36** ✅ finally 内 throw + 外层类型不匹配的 catch → 访问违规 0xC0000005（2026-08-02，
  已修复）：根因 `__zan_eh_tid_match` 把 null 类型描述符（字符串 throw）当作"匹配任何子句"，
  `catch (Exception e)` 捕获字符串对象后 `e.Message` 解引用 → 崩。修复：null 描述符不再匹配
  任何类型化子句。用例 `string_throw_dispatch.zan`，EH 相关 27 例全过。
* **A37** ✅ 未捕获异常诊断增强（2026-08-02，已完成）：finally 内 throw 无 handler 路径与
  直接 throw 无 handler 路径都按 in-flight 异常打印（字符串 → `Unhandled exception: <文本>`、
  类对象 → `Unhandled exception (class object)`）；IDE `Main()` 加 try/catch 兜底写
  `<exe>/cache/ide_error.log` 并弹 MessageBox。
* **A38** ✅ event/record 降级生成源码被未初始化栈内存污染 → "unexpected character '\0'"
  （2026-08-02，已修复）：`parser.c` 的 `zsrc_append` 调用点 `n += zsrc_append(..., &n, ...)`
  复合赋值求值顺序未指定，MinGW GCC 双倍记账，写入位置跳跃留下未初始化字节。25 处改掉、
  `zsrc_append` 返回值改 `void` 防复发；`tref_write` 纯值调用保留。
* **A39** ✅ `System.Automation.UiElement` MSAA 无障碍自动化（2026-08-03，已完成）：
  `UiElement.zan` 经 `oleacc.dll`/`IAccessible` 支持 HWND/坐标取元素、树遍历、按名查找、
  属性读取与 Invoke/Select/Focus/SetValue。Win64 `VARIANT` 24 字节按 ABI 以指针传递。
  `win_uielement_smoke` 三档 3/3。
* **A40** ✅ aardio 能力迁移余项收口（2026-08-03，已完成）：补齐 `System.Net.Ping` /
  `NetworkInterface` / `System.Drawing.Printing` / `System.Management.Device` /
  `Otp/Jwt` / `WebDav` / `Text.Markdown`；顺带修三元表达式 borrowed 字面量与 owned
  `Substring()` 分支所有权不统一（irgen PHI 分支补 retain，`ternary_width` 回归）。
  新增模块 + UiElement + 回归 30/30 通过。
* **A41** ✅ 整型字面量后缀 L/l/U/u（2026-08-03，已完成，原编号 A39 重复已顺延）：
  lexer/parser/checker/irgen 五段式，`1L`→long、`1U`→uint、`1UL`→ulong；
  `Convert.ToString(ulong)` 改 `%llu`；`Background.zan` 的 `((long)1) << 30` 改回 `1L << 30`；
  `int_literal_suffix.zan` 三档。
* **A42** ✅ 静态字段跨编译单元泄漏：Pinyin.cache 与 Dict 方法 receiver（2026-08-03，
  已完成，原编号 A40 重复已顺延）：① `emit_release_static_rc_fields` 只扫含 `main()`
  的单元 → 改走 `g->static_fields` 注册表覆盖全部单元；② `Dict.Add/ContainsKey/
  TryGetValue/Clear/Remove` 与 `Dict.Count` 缺 owned receiver 释放 → 补
  `emit_release_owned_call_temp`。`leakcheck_tryget_pinyin` 13527 对象泄漏 → 0。

---


# A43 · 相对 C# 的能力差距登记表（2026-08-04 实测，逐条待办）

口径：每条都是在本机 `zanc` 上真编译真运行的结果（探针留在 `_scratch/p/`，编号即
文件前缀），不以「源码里有 token/AST」当作可用。ARC 而非 GC、`yield` 急切物化、
无 NRT 流分析属于设计取舍，不在此表。

## A43-A 语义/代码生成缺陷（编译通过但结果错或崩溃，优先级最高）

| # | 现象 | 探针 | 状态 |
|---|---|---|---|
| A43-A1 | 整数赋给 `double`/`float` 按位重解释（`double b = 1;` 得 `4.94066e-324`），`double` 形参/返回/混合运算 LLVM 校验失败 | 54,57–66 | ✅ 已修（A32-7，0205cc1） |
| A43-A2 | 默认参数值不填充：`F(1)` 调 `F(int a, int b = 2)` LLVM 校验失败；`new A(1)` 静默不调用构造器，字段留 0 | 19,81–88 | ✅ 已修（2026-08-04，`default_parameters.zan`） |
| A43-A3 | 接口默认方法（`interface I { int G() { return 42; } }`）经接口变量调用一律返回 0；经类变量调用报 `'C' has no member 'G'` | 44,51,52,80,100–102 | ✅ 已修（2026-08-04，`interface_default_methods.zan`） |
| A43-A4 | `int? v = a?.x;` 运行时崩溃（0xC0000005）；此前是 `PHI node operands are not the same type` | 21,43,110–145 | ✅ 已修（2026-08-04，`nullable_reference_types.zan`） |
| A43-A5 | `event H E;` + `a.E += P.OnE; a.Fire();` 编译通过、静默不触发（输出只有 `end`） | 28 | ✅ 已修（A33-2，`delegate_closures.zan`） |
| A43-A6 | 捕获式 lambda：`(a) => a + k` 报 `use of undeclared identifier 'k'`；捕获字段时诊断还指向 `stdlib/System/Security/Cryptography/Md5.zan`（位置也错） | 22 | ✅ 已修（A33-2，`delegate_closures.zan`） |
| A43-A7 | 泛型类里的实例泛型方法 `Pool<T>.M<U>()` 运行崩溃（0xC0000005） | 13 | ✅ 已修（A32-3a，`generic_class_instance_generic_method.zan`） |
| A43-A8 | async 泛型方法返回垃圾值（打印 `-1886711216`） | 56 | ✅ 已修（A32-3b，`async_generic_method.zan`） |
| A43-A9 | `static A()` 只在首次 `new` 时执行：先读 `A.n` 得 0，`new A()` 后才是 7（C# 首次访问静态成员即触发） | 08,40,95–99 | ✅ 已修（2026-08-04，`static_constructor.zan`） |
| A43-A10 | `readonly` 只解析不强制：`public readonly int x;` 在类外 `a.x = 6;` 编译通过并改值 | 71,72,90–94 | ✅ 已修（2026-08-04，`readonly_fields.zan`） |
| A43-A11 | `Nullable<T>`：值类型可空缺少真实表示（值 + has-value），`int?` 一度只能报错拒绝 | 130,144 | ✅ 已修（2026-08-04，`nullable_value_types.zan`） |
| A43-A12 | 重载运算符二元调用 LLVM 校验失败：`v + 5`/`v += 5` 把字面量实参当 i64 传给声明 i32 的形参；多个同名 op 重载时 `get_method_sym` 只取第一个（`w + 2.5` 误调 `op_add(V,int)`） | g43, e01, e02 | ✅ 已修（2026-08-08，`operator_overload.zan`） |
| A43-A13 | 比较/关系操作符重载（`operator <`、`operator ==` 等）在 checker 层被拒：`a < b` 报 `no implicit numeric conversion` | e02 | ✅ 已修（2026-08-08，`operator_relational.zan`） |
| A43-A14 | `static extern string` 返回的裸 C 指针做下标：合法下标一律报 `string index out of bounds`（`Skin.EmbedList` 逐字符扫描时必崩） | — | ✅ 已修（2026-08-08，`extern_cstring_index.zan`） |
| A43-A15 | 成员调用解析到不存在的重载时**静默编译并返回空串/默认值**：QueryBuilder 删除旧 `BuildSelect(string,bool)` 后，类内 `BuildSelect(columns, true)` 照常编译，运行时返回 `""`（用户代码里同形调用会正确报错，类内静默——解析路径不一致） | 批B 现场（718bc8af 前后），`tests/conformance/http_params.zan` 开发过程复现同族 | ✅ 已修（2026-08-09，`diag_implicit_ghost_call`）。根因：**隐式 this**（无前缀）调用走裸名分支，未命中后直落兜底静默置零；显式 `this.` 走成员访问分支才有硬错误。修复：irgen_call.c 裸名分支在全局函数未命中后，对封闭类型链做成员名扫描，全缺则报 `'T' has no member 'n'`。**实战即中两雷**：stdlib `Validate.zan` 无恙但 `Worker.zan:1619` 调用从不存在的 `ControlPortHasMaster()`——重启守卫静默恒 false，活主端口被 reuse-bind 抢占；已改接现成 `ProbeControlPort()` |
| A43-A16 | **重复成员定义静默接受且先者胜**：HttpContext 已有 `Param(string)`（route 专用），新增同名合并版照常编译、全部调用解析到前者——注入面级别的语义偷换零诊断 | 同上开发过程 | ✅ 已修（2026-08-09，`diag_duplicate_member`）。binder.c `check_member_name_clash` 原只查方法↔字段互撞；扩展为：同名字段/属性重复（CS0102）、同签名方法重复（CS0111，按参数类型结构等价比较，合法重载不受影响）、同签名构造器重复、重名枚举成员；方法参数改为先绑定后查重。**实战即中一雷**：stdlib `Validator.Ok()` L31/L85 逐字节重复声明，先者胜掩盖至今；已删 |
| A43-A17 | codegen 报 `no overload of 'T.M' matches argument type(s)`，实参是**自身即一次方法调用的表达式**（`this.AddClass(this.ActiveSizeCls())`）；同一实参先落 `string` 局部变量再传即正常。typecheck 通过、仅 irgen 拒绝；`Gui.Control` 派生语境下必现（stdlib Switch.zan 两处），但最小探针（继承 + Binding<string> 字段 + 静态映射方法同形写法）不复现，疑与重载集/成员链解析路径有关 | `_scratch/probe_nested.zan`、`_scratch/probe_nested2.zan`（均**不**复现）；复现现场见 Switch.zan 改造（2026-08-29） | ⏳ 未修。暂以局部变量写法绕过（不构成能力缺失，仅诊断误导），排查方向同 A43-A15 的 irgen 实参类型传播 |

> 各条修复细节（根因、落点、探针输出、回归记录）已压缩；原始详细记录在 git 历史与
> `_scratch/TASKS.md.bak-2026-08-31`。

## A43-B 语法缺失（parser 层不接受，按价值排序）

| # | C# 语法 | 探针 | 现状 |
|---|---|---|---|
| A43-B1 | `Func<...>` / `Action<...>` | 26,39 | ✅ 已修（2026-08-08，`cs_b01_func_action.zan`）。内置泛型 delegate 类型 + 匿名 delegate 语法；`op_call` 的 `params` 尾部重载在零参数调用时修复了 fixed 参数计数下溢（`args[-1]` 越界读崩溃）：`resolve_op_overload` 的 `fixed = ps->count - 1 - p0`（原 `-2-p0` 静态/实例各差 1），`pack_params_args` 的 `injected` 按 `MOD_STATIC` 判定，emit 侧参数类型索引按 static/instance 取 `self_off`。回归测试 `cs_b_opcall_params.zan`（静态/实例、零参/多参） |
| A43-B2 | `using (res) { }` 确定性释放 | 03 | ✅ 已修（2026-08-08，`cs_b02_using.zan`）。语句级 `using (res) { }` 降级 try/finally + `Dispose()`；stdlib 定义 `IDisposable`。另修复字符串拼接对 NULL 操作数直传 `strlen` 的崩溃（`emit_str_concat`/`emit_str_concat_n` 补 `emit_str_nonnull`） |
| A43-B3 | 元组 `(int, string)` 与解构 | 01 | ✅ 已修（2026-08-08，`cs_b03_tuple.zan`）。`(a,b)` 字面量降级为匿名 struct（`Item1..N` 字段，签名缓存）；`var (a,b)=rhs` / `(int a,string b)=rhs` 解构→字段赋值；支持方法返回元组、显式 `(T1,T2)` 类型、`.ItemN` 访问。**限制**：嵌套解构 `var (a,(b,c))=...` 与 `(a,b)=rhs` 赋值未实现（后者已报错）；struct 字段持动态字符串沿用 KeyValuePair 的既有泄漏（ARC 不管理 struct 内 rc 字段） |
| A43-B4 | 命名实参 `F(b: 2)` | 17 | ✅ 已修（`cs_b04_named_args.zan`）。parser（`parser.c:1066`）→ `AST_NAMED_ARG` → irgen `reorder_named_args_impl`（`irgen_builtins.c:2423`）全链路；非法形态（跳过默认参数留空档）由 `diag/named_args_gap.zan` 覆盖。**2026-08-27 复核纠正**：清单原写 parser 报错，已过时 |
| A43-B5 | 模式变量 `is T x` / `case T x:` / `when` 子句 / `is not null` | 23,34,35,45 | ✅ 已修（2026-08-08，`cs_b05_pattern_var.zan`）。`is T x` / `is not T` / `is null` / `is not null`、`case T x:`（类/string/object 运行时 is 检查，string 走 obj-8 magic 探测）、`when` 守卫（独立块短路，失败时不求值守卫）、`case null:`、case 体模式变量重绑定（先前 case 的 scope 释放会截断 locals）、switch `break` 经 `loop_locals_base` 释放不再误释放判别式（`case T x:` 后 `x is T` 此前读悬垂指针）。`emit_runtime_is_check` 的越界上界改用常量 `ZAN_MAX_LEAK_SITES`（原用发射时的 `leak_site_count`，helper 函数先于 `new` 站点注册时恒 false）。standard 回归发现的 `conformance_python_embed_smoke` 编译崩溃（`Python.Eval("lambda: 42")()` 空参 op_call 的 params 重载）已定位为 op_call 重载解析的 fixed 计数下溢（见 B1 行）并修复 |
| A43-B6 | switch 表达式 `x switch { ... }` | 16 | ✅ 已修（2026-08-08，`cs_b06_switch_expr.zan`）。`expr switch { <pattern> when <guard> => <result>, ... }`：常量/类型模式（`is T x` 型 pattern + 变量绑定，`case T x:` 同款运行时检查）、`null`、`_` discard、`default`、`when` 守卫、pattern 变量在 result 中可用、无匹配 arm → 零值（int 0 / 引用 null）、嵌套 switch、可作为调用实参/赋值 RHS。降级：合成 AST_SWITCH_STMT，arms 的 result 存入隐藏局部 `\x01swx`（每个 case 体 `{__swx = result; break;}`），无 discard 时补空 default，emit 后 load 隐藏槽；隐藏局部移出 scope（`locals->count` 回滚）使 +1 归属表达式结果。**ARC**：`expr_yields_owned_rc_value` 加 AST_SWITCH_EXPR 分支返回 1（槽内存储后恒为 +1：owned arm move、borrowed arm retain，而本地作用域不释放），否则接收方二次 retain 导致 `leakcheck_cs_b06_switch_expr` 泄漏 |
| A43-B7 | 扩展方法 `this int v` 形参 | 15 | ✅ 已实现（`this int v` + `find_extension_method` 全链路；`tests/conformance/cs_b07_ext_method.zan`）。数字字面量接收者需写 `(5).Twice()`（`5.` 会被词法当作 float，与 C# 一致）。**2026-09-08 补**：泛型接收者 `this T item`（裸类型参数）曾被 `find_extension_method` 的 kind 门整体跳过——TYPE_TYPE_PARAM 与任何具体接收者 kind 都不等，泛型扩展对所有接收者都解析失败；放行后由打分排序（具体接收者仍胜）、try_method_spec 绑定 T（`7e957790`，`extension_methods.zan` 补 `"solo".Singleton()`） |
| A43-B8 | 交错数组 `int[][]`、多维数组 `int[,]` | 24,25 | ✅ 已修（2026-08-08，`cs_b08_arrays.zan`）。`int[][]`（数组的数组）、`int[,]`/`int[,,]`（rank-N 矩形数组）、混合 `int[][,]`（rank-2 数组的 1D 数组）与 `int[,][]`，按 C# 最左 rank 规格最外层折叠；`.Length` 返回元素总数、`.GetLength(d)` 返回第 d 维长度；多下标 `m[i,j]` 按行优先扁平化越界检查；`new int[2,2]{{1,2},{3,4}}` 带维度初始化。**实现**：`type_ref` 加 `array_rank`+`array_element`（parse_type_ref 从右到左折叠最多 16 个 rank 规格）、`new_expr` 加 `array_rank`（sized 层作为最外层包装）、`AST_INDEX.extra` 多下标列表（首下标留在原字段，1D 路径不变）；binder 构造嵌套 TYPE_ARRAY（`binder_type_equal`/`checker_type_equal`/`types_concrete_equal`/`types_equal`/`tuple_sig_type`/`render_type_full` 全部比较 rank）；`zan_mdarray_alloc` 新布局 raw+0=count、raw+8=rank、raw+16=dims[0..rank-1]、data 紧跟其后，`zan_array_len`/`.Length` 可直接复用；`emit_mdarray_elem_ptr` 行优先扁平化，rank>1 的 new/store/incdec/read 路径全部分支到它。leakcheck `-- leak-clean` |
| A43-B9 | 索引器 `public int this[int i]` | 37 | ✅ 已修（`cs_b09_indexer.zan`）。`parser.c:3351` 专解析 `this[...] { get/set }` 与 `=> expr`，降级为 `op_index`/`op_index_set` 协议。**2026-08-27 复核纠正**：清单原写 parser 报错，已过时。**2026-09-08 补**：①索引器重载落地——每个索引器属性都命名 Item，binder 数据成员按名字判重使第二个 `this[]` 必报 duplicate 'Item'；AST_PROPERTY_DECL 记录 indexer_params 后豁免 indexer/indexer 同名对，真实重复（同下标类型）仍由合成 op_index 的重复方法检查拦截（`51883f7a`，`indexer_overload.zan`）。②checker 收口——读路径从不校验下标与 op_index 参数的匹配，string 下标打在 this[int] 上直达 LLVM 校验崩溃（"GEP indexes must be integers"）；单重载按赋值兼容校验个数与类型（多重载留 irgen resolve_op_overload 按实参挑选，按 MOD_STATIC 区分合成实例形状/手写静态形状），写路径 `checker_index_set_target` 硬性要求 params==3（self,index,value）而合成形状是无 self 的 (index...,value)——恰好永远匹配不上、所有写入都静默靠读重载类型侥幸通过，改认 2 参形状并无一重载接受时报错（`253b5c76`，`diag_index_wrong_type`/`diag_index_write_wrong_type`） |
| A43-B10 | 插值格式串 `{v:D4}` | 50 | ✅ 已修（2026-08-08，`cs_b10_interp_format.zan`） |
| A43-B11 | 局部函数 | 05 | ✅ 已修（2026-08-08，`cs_b11_local_func.zan`，含泛型局部函数） |
| A43-B12 | `implicit` / `explicit operator` | 06 | ✅ 已修（2026-08-08，`cs_b12_conv_operator.zan`） |
| A43-B13 | 泛型变体 `interface I<out T>` | 07 | ✅ 已修（2026-08-08，`cs_b13_variance.zan`，接受语法不实现协变） |
| A43-B14 | `checked` / `unchecked` | 02 | ✅ 已修（2026-08-08，`cs_b14_checked.zan`，no-op 纯表达式） |
| A43-B15 | `Task` / `Task<T>` 类型名与 API | 12,31,32 | ✅ 已修（2026-08-08，`cs_b15_task.zan`）。`Task`/`Task<T>` 成为可用作值的类型（新 `TYPE_TASK` kind，opaque i64 协程句柄；`Task<int>` 携带结果类型），同时保留 builtin 静态 `Task.Spawn/Run/IsDone/Cancel/IsCancellationRequested` 调用面。`Task.Run(delegate(){...})`/`Task.Run(()=>...)`/`Task.Run(Action 变量)`：内联执行 delegate（体内 await 经 root-await 路径泵驱动），返回已完成任务（句柄 0，`__zan_co_isdone(0)` 恒 1）；`t.Wait()`：泵 `zan_co_sched_run` 直到该 frame done（协作式调度下同步上下文等协程的唯一正确方式）；`t.Result`：读 frame 结果槽（`ASYNC_FRAME_RESULT`）解码为 T 后 reap（untrack+free）；`t.IsCompleted`：非泵探针。**Task\<T\> 生命周期**：`Task.Run(<非 void async 调用>)` 不装 reaper、保留 track，frame 活到 `Result`/`Wait` 读取（恰好一个终结任务）；`Task.Spawn` 恒装 reaper（fire-and-forget 不泄漏）；`Task<T>` → `Task` 协变可赋值。**顺带修复 B5 预存缺陷**：`stdlib/System/Threading/Threading.zan` 的 `MacSemWaitUntil(string, long when)`/`MacDispatchTime(long base,...)` 参数名撞 `when`/`base` 保留字，macOS 目标 ABI 测试（stale 构建图掩盖）编译失败——改名 `deadline`/`baseTime`。**回归门禁**：standard 477/480（`cs_b15` 3 项 + 既有 474 项全过）；3 个 `emit_lib_*` 失败均为环境/并行因素，非 B15 回归：(1) `windows_dll` 链接时 `libgcc.a(emutls.o)` 缺 pthread 符号（mingw 交叉工具链无 winpthread stub，B8 时代产物时间戳佐证与本次无关）；(2) `linux_so`/`macos_dylib` 的 cross-shared guard 因 **IDE 改版并行加入的 `stdlib/System/MessageBox.zan`（untracked，17:29 创建）`using System.IO` 把 IO→DirectoryWatcher→Threading 全链拉进 `using System` 闭包**而触发（`zan_thread_*`/`zan_gate_*` extern 置位 `uses_sync_runtime`/`uses_socket_async`；移走该文件 cross-shared 立即通过，B8 时代 16:23 产物无此文件即通过）。IDE 改版合入后应将 MessageBox 移出 `System` 根命名空间（`using System` 会全量拉入 `stdlib/System/*.zan`） |
| A43-B16 | `KeyValuePair<K,V>` | 27 | ✅ 已修（2026-08-08，`cs_b16_keyvaluepair.zan`） |

| A43-B17 | LINQ `orderby` / `join` / `let` / `group into` | 14 | ✅ 已关闭（2026-08-27，**A54**）：parser 与 irgen 降级早已实现（2026-08-27 复核纠正过旧记录），补齐 `linq_query_clauses.zan` conformance（八形态三档孪生）时撞出的七个真缺陷已全部修复。原始复核记录见备份 |
| A43-B18 | `init` 访问器、`readonly struct` / `ref struct` | 09 | ✅ 已修（2026-08-08，`cs_b18_init.zan`） |
| A43-B19 | 可空元素数组 `int?[]` | a11b | ✅ 已修（2026-08-08，`cs_b19_nullable_arr.zan`） |
| A43-B20 | `nameof(expr)` | — | ✅ 已修（2026-09-08，`b8d35b69`，`nameof_expr.zan`）。上下文关键字形态（裸名 + 恰一个实参）被 checker/irgen 认领，parser 与 AST 零改动；取实参末段标识符拼写（`r.UserName`→`UserName`），不求值；实参仍走 checker，未知名字照旧编译错误（对齐 C# 符号可解析要求） |
| A43-B21 | `event` 事件成员 | — | ✅ 已有（2026-09-08 核验）。字段式事件早已可用：`public event Action Click;`（`tests/conformance/events.zan`、`event_delegate.zan`、`event_receiver_handlers.zan` 全绿，含 leakcheck），`+=/-=` 订阅经 op_add 委托 combine。初判"未做"有误——已实测核销 |
| A43-B22 | `yield return` / 迭代器 | — | [ ] 未做（2026-09-08 深查后按实上报）。语言目前零 enumerable 基建：①foreach 硬编码三种容器形状（List/数组/string，`irgen_stmt.c:2519`），没有 GetEnumerator/MoveNext/Current 协议钩子——这是 yield 的真前置，比状态机本身先行；②泛型接口引用转换有洞：`Seq<int> r = new Range(3)` 初始化位置报"no implicit conversion"，而实参位置 `Sum(new Range(5))` 与非泛型 `IShape s = rect` 都过——补齐此洞是 stdlib `IEnumerable<T>/IEnumerator<T>` 接口对的可用前提（`cs_b13_variance.zan` 只有具体类型直调，未覆盖该转换）；③yield 本体的状态机降级是多日工程：`irgen_async.c`（1539 行）是无栈 CPS（resume-k 块 + 堆帧 + 任务完成回调 + 恢复 trampoline），与任务运行时深耦合，同步 MoveNext 驱动需要等身复刻骨架；备选架构——(a) 复用 async 骨架换驱动（工作量最大、语义最正），(b) 线程+双闸生产者消费者（降级最浅但阻塞式等待与协程调度器相性差，已否），(c) 源码级合成枚举器类（record 的 gen_record_class 路数）——控制流无法在解析期重写，不可行。建议次序：foreach 协议 + 接口对 + 泛型接口转换洞（各自独立可测）→ 再做状态机 |
| A43-B23 | `record` / `with` 表达式 | — | ✅ 已修（2026-09-08，`7b6d8a1c`，`record_with_expr.zan`）。`record` 位置记录早已落地（合成 ctor/op_eq/ToString）；本轮补 `with` 表达式：上下文关键字解析、checker 字段校验、合成 `__CloneWith` 非破坏复制、隐藏槽保证接收者单次求值、ARC owned 交接；顺手修既有 bug——操作符重载调用不释放新分配实参（`v == new Point(...)` 每求值漏 1 对象） |
| A43-B24 | `partial` 类 | — | ✅ 已有（2026-09-08 核验）。上下文关键字 `partial` 解析与跨文件同类名合并早已可用（`tests/conformance/partial_types.zan` 全绿，含 leakcheck）。初判"未做"有误——已实测核销 |


## A43-C1 类型化查询的跨语句组装（dbgen，2026-08-08 前提更新，待重新验证）

`this.Post.Where(a => ...).OrderByDescending(a => a.id).ToListAsync()` 原只在
**一整条链**里成立：dbgen 降级时消化整条链，查询对象本身不是可传递的值。探针
（`_scratch/`，已删）：

```zan
var sel = db.Select<PItem>().Where(x => x.status == 1);
sel = sel.OrderByDescending(x => x.id);   // error: 'string' has no member 'OrderByDescending'
```

写成生成类型名 `__DbQ_PItem sel = ...` 同样报 `'__DbQ_PItem' has no member
'OrderByDescending'`——lambda 形态的 `OrderBy/Where` 只在链内识别；`var` 还把查询
推断成了 `string`（第二个缺陷）。

后果：列表页「按哪一列排序」只能每个排序键一个分支（模板 `Admin/Posts.Index`
就是这个形状），字段一多会膨胀。**不要在 Zan 侧绕**；配置驱动 CRUD（按配置排序
任意列）之前先修：查询对象要成为可命名、可赋值、可跨语句追加的一等值。

**2026-08-08 复核：前提已变化**——`__DbQ_<E>` 现在是 `GenDbEmit.zan:96` 发射的**真实
运行时类**（fluent 方法 W/Where/WhereDict/OB/OBD/GB/P/Pi/Pd/InI/InS/InD 全部返回
`__DbQ_E`，含 `ToListAsync` 与 `Expr<T>`），不再是"纯代码生成期概念"；「跨语句 var
组装能否编译」需按新生成器重新验证后更新本条目。

> 已实测可用、别再当缺失：带参构造器与重载、`: this(...)` / `: base(...)`、方法重载、
> `public/private/protected/internal`、`partial`、`#region/#endregion`、`const`、
> `static readonly`（读侧）、属性 get/set、泛型类/方法/具名约束、interface、delegate、
> 无捕获 lambda、运算符重载、try/catch/finally、非泛型 `async/await`、`yield return`、
> `enum`、`virtual/override/abstract/base`、`params`、`record`、基础字符串插值、
> `nameof`、原始字符串、`lock`、`goto`、`switch` 语句、List/Dictionary、
> `new int[]{...}`、集合与对象初始化器、`??`。

**A43-B 已知缺陷（2026-08-08，探针 `_scratch/csg/p21.zan`）**：call 实参不校验
可赋值性，`F(object)` 可接收标量并 `inttoptr` 原样存进 object 槽；之后对该槽做
`is`/模式匹配会按指针解引用（读 `ptr-8`）而崩溃。`object o = 42` 在 checker 已被拒
（"no implicit conversion"），实参路径是漏网的同一规则。修法：AST_CALL 按形参类型
调 `checker_check_assignable`（或标量进 object 槽前装箱）。这是 B5 之前的既有缺陷，
非 B5 引入；B5 conformance 未覆盖该形态。


---

# A44 · Chart 组件对照 ECharts 的搁置项（2026-08-06，范围决策记录）

> **2026-09-11 修订**：本条的对照基准是 ECharts **2.2.4**，已过时。引擎现行
> 基准是 **6.1.0**（`examples/gui_charts` 的 option 与官方示例站 1:1），
> 2.2.x 的"已表达/未表达"结论不再适用。代码侧现行缺口账本见
> [`docs/CHART_CODE_GAP_LEDGER.md`](docs/CHART_CODE_GAP_LEDGER.md)
> （含已验证的语义错误 W1–W4 与 never-read 配置键倒排，可再生）。
> 本条保留仅作历史范围决策记录，不再作为待办来源。

图表组件完善（补齐雷达面积填充/每轴 max、markPoint、嵌套环饼、K 线 dataZoom、
悬停 emphasis，新增和弦/力导向/事件河/韦恩渲染器 + `examples/gui_charts`
独立示例）时，对照 ECharts 2.2.4 明确**不做**、留待后续的部分。均非缺陷绕过：
现有 API 已解析相关字段或留有占位，只是渲染层未接。
（2026-08-30 复核：toolbox、地图渲染器已落地；timeline 已落地为
`ChartTimeline` 组件，见「Gui/Chart 后续」节与 docs/CHART_VS_ECHARTS_227.md。）

* [ ] **数值 / 时间 / 对数 X 轴**：`ChartAxisType.Value/Time/Log` 已定义且
  `FromJson` 已解析，但 `Chart.BuildAxes*` 仍按类别槽布局——X 轴只能等距分类。
* [ ] **itemStyle / emphasis 完整样式树**：`ChartItemStyle` 等样式类已建模，
  但渲染器尚未逐项消费（目前只有三级颜色控制 option < series < data item）。
* [x] **toolbox / visualMap / timeline 组件** ✅（2026-08-30 复核）：toolbox
  （数据视图/类型切换/还原/保存/缩放）与 dataRange（visualMap 2.x 前身）已在
  批 4/5 落地；timeline 已落地为 `ChartTimeline` 组件（帧序列 + 播放器条 +
  autoPlay + JSON 解析）。
* [x] **地图渲染器** ✅（2026-08-30 复核）：`ChartGeoJson` + `ChartMapRegion/
  ChartMapRing(hole)` 已落地（官方 china.json 运行时解码 + 内嵌副本），
  roam/选中/markPoint/dataRange 全通。
* [ ] **Canvas 曲线/旋转原语**：和弦 ribbon、韦恩等用密集折线采样 +
  `FillPolygon`（Zan 侧扫描线）近似，受"不新增原生 Canvas 导出"约束
  （五平台预编译驱动，`Render.zan` 注释）。若未来开放原生导出可替换为真贝塞尔。


# A45 · List<T> 实参不做类型实参检查的 typecheck 洞 —— ✅ 已修复（2026-08-07，`2e1fe4b8`）

**现象**：`examples/gui_charts` 的 Heatmap 演示闪退（静默 SIGSEGV）。
回溯：`ChartSeries_Number ← ChartSeries_Value ← ChartView_CacheFingerprint ←
Charts_DemoHeatmap` —— 演示把 `List<int>` 传给了
`ChartSeries.Named(name, type, List<ChartData>)`，裸整数被当成 `ChartData*`
解引用。

**根因**：checker 不比较泛型容器实参的类型实参，`List<int>` 可以顶替
`List<ChartData>` 通过编译。

**修复**（修在 irgen 侧而非 checker）：`irgen_expr_core.c::check_generic_invariance`
逐实参用 `type_full_equal` 比较，报 "generic type arguments are invariant and must match
exactly"；经 `emit_arg_typed` 覆盖全部方法调用实参发射点。2026-08-08 实测探针
（`List<int>` → `List<Box>` 形参）编译报错，不再静默通过。演示侧改用正确工厂
`ChartSeries.Of(name, type, List<int>)`（演示本身是 API 误用，不属于缺陷绕过）。

# A46 · 设计器内置类型有一半没接到真控件 —— 已作废（2026-08-08 复核，缺陷形态已消除）

原记录：`.zform` 设计支持 72 种内置类型（`src/compiler/formgen.c` 的 `fg_kind_of`
表，0..71），但代码生成只映射了其中一部分，其余全部落到兜底分支
`return "Label"`（`formgen.c` 的 `fg_widget_type`）——表单能编译、能显示
标题，控件本体是假的。

**已作废原因**：`formgen.c` 已随 B7-5（2026-08-08，commit `060e6200`）整体删除；
替代品 `stdlib/System/Compiler/GenForm.zan` 直接按 kind 生成 `new <kind>()`
（`GenForm.zan:388-407`），非法 kind 是 `ValidateFields` 校验错误（:265-286），
不再是静默回落 Label——"72 种回落 Label"的缺陷形态不复存在。若新形态下仍有
「内置类型缺真控件」的缺口（如 A46-3 的 `MenuBar` 菜单能力），按 GenForm 的
校验错误逐项另立条目。

# A47 · 仓库里 openssl 副本重复、build 树自嵌套（2026-08-06，工程卫生记录）

**openssl 重复**：`libcrypto`/`libssl` 在源码树里有多份**逐字节相同**的拷贝，
因为 driver bundle 的约定是"每个模块自带全部运行期依赖"，而 Postgres 和 Tls
都依赖 openssl（MD5 前 8 位相同即同一文件）：

| 文件 | Postgres | Tls | 大小 |
|------|----------|-----|------|
| `libcrypto.so.3` (linux-x64) | ✓ | ✓ | 5.5 MB |
| `libssl.so.3` (linux-x64) | ✓ | ✓ | 0.7 MB |
| `libcrypto.so.3` (linux-arm64) | ✓ | ✓ | 4.8 MB |
| `libssl.so.3` (linux-arm64) | ✓ | ✓ | 0.8 MB |
| `libcrypto-3-x64.dll` (win-x64) | ✓ | ✓ | 5.5 MB |
| `libssl-3-x64.dll` (win-x64) | ✓ | ✓ | 1.0 MB |

**2026-08-27 实测更新**：重复面已扩大到 **约 28.8 MB**——上表六项仍成立，且
**macOS 不再是"只有一份"**：Tls 现在也带 macos-x64 / macos-arm64 的
`libcrypto.3.dylib`（4.99 / 4.63 MB）与 `libssl.3.dylib`（0.86 / 0.84 MB），
与 Postgres 侧逐字节相同。下面这句"macOS 不重复"已过时。

合计约 18 MB 纯重复（macOS 只有 Postgres 一份，不重复；
`System/Scripting/drivers/win-x64` 的 `libcrypto-3.dll` 是 CPython 嵌入包
自带的**另一个** build，名字和内容都不同，不能与上表合并）。

* [ ] **A47-1 拆出共享原生依赖目录**。挡路的是安全校验
  `zan_is_safe_bundle_name`（`src/compiler/main.c`）：bundle 清单只允许同目录
  裸文件名，禁止路径分隔符，所以今天没法引用别的模块的文件。方案：清单支持
  一种受限的共享引用（如 `@shared/openssl-3/<file>`，解析相对 stdlib 根、
  仍然拒绝 `..`、`:`、绝对路径），文件只留一份在
  `stdlib/_shared/openssl-3/<target>/`，发布时按 basename 复制到 exe 旁边。
  ~~链接期不受影响：导入库（`libcrypto.dll.a` / `libssl.dll.a`）只有 Tls 用，
  留在原处。~~
  **2026-08-27 复核：上面这句不成立，本项比记录的更复杂，因此本轮未动手。**
  三条新约束：
  1. **链接期确实受影响**：driver 目录会被加进链接搜索路径（`zan_lib_dirs`），
     Linux/macOS 侧 `-lcrypto` / `-lssl` 就是从 `drivers/<target>/` 解析的，
     把 `.so`/`.dylib` 移走会让链接失败——共享目录必须同时进链接搜索路径。
  2. **没有任何测试覆盖 driver bundle 的发布**：现有 publish 测试只有
     `publish_obf_ctor_*`（字符串反混淆）与 `conformance_python_embed_smoke`
     （Scripting 的 bundle），Postgres / Tls 的 bundle 复制无人验证。
  3. **5 个目标里本机只能验 win-x64**，其余 4 个（linux-x64/arm64、macos-x64/arm64）
     改坏了不会当场暴露。
  做法建议：先补一个"发布一个用 Tls 的程序、断言 exe 旁边出现
  `libssl`+`libcrypto` 且能启动"的用例，再改清单格式与链接路径，最后移文件。
  另：实测重复面已达 **约 28.8MB**（macOS 侧现在也重复，见上表旁注）。
* [~] **A47-2 `build/toolchain` 自嵌套**：曾实测嵌套到 32 层
  （`build/toolchain/toolchain/toolchain/...`，每层都带一份 stdlib 和
  openssl），`build/toolchain` 单独占 1.29 GB / 3205 文件。`build/` 是
  git-ignored 的一次性产物，但说明某个发布/拷贝步骤把目标目录拷进了自己
  （`scripts/publish_ide.ps1` 的注释已经点出要避免 `toolchain\toolchain`）。
  **2026-08-08 复核：自嵌套已消失**——当前 `build/toolchain` 无嵌套（189MB / 1026
  文件，实测深度 0），大概率随 build 目录重建而清掉；「定位到具体步骤并加自嵌套
  防护」未验证，重跑发布流程时仍需检查。
  **2026-08-27 复核**：仍无自嵌套（192 MB / 1048 文件，深度 0）；防护仍未加。
* [x] **A47-3 `templates/server/server-mvc/.build/` 残留** —— ✅ 本机已无
  （2026-08-27 实测该目录不存在）。它是跑过一次模板构建留下的产物、被
  `.gitignore` 的 `.build/` 挡着，会随构建重新出现；若要彻底了结，应在模板构建
  脚本里收尾删除，而不是靠人工清。
* [x] **A47-4 `System/Scripting/drivers/win-x64` 瘦身**（2026-08-06）：CPython
  嵌入包里只有解释器 DLL 是被 `Python.zan` 用 `Interop.Load` 加载的，随包
  下发的启动器 `python.exe` / `pythonw.exe`、安装包签名目录 `python.cat`、
  MSI 扩展 `_msi.pyd` 和已停用的 `python312._pth.disabled` 都用不到，共
  0.8 MB。已删除并从 `python.bundle` 摘掉，`scripts/stage_python.ps1` 里加了
  排除清单（`._pth` 由重命名改为删除），重跑脚本不会再把它们放回来。
  〔已实测〕`zanc tests\conformance\python_embed_smoke.zan --auto-stdlib
  --publish` 发布 77 个文件、运行输出 `python-ok: 1`，退出码 0。

# A48 · 交叉编译共享库缺运行时（2026-08-08 记录，未修）

`emit_lib_linux_so` / `emit_lib_macos_dylib` 两个 standard 用例失败，`zanc` 明确
拒绝：`cross-compiled shared libraries that use the Zan runtime are not supported
yet`（`src/compiler/main.c` 的 `need_rt && cross_compiling` 分支）。
`tests/emit_lib/lib.zan` 现在会带上 embed/sync 运行时，于是撞上这道护栏——
本机 `.dll` 与 `.a` 都正常。

* [x] **A48-1 交叉链接共享库时把运行时对象一起编出来** —— ✅ 已完成
  （2026-08-27 复核）。清单引用的错误串
  `cross-compiled shared libraries that use the Zan runtime are not supported yet`
  与 `need_rt && cross_compiling` 分支在全库已 **0 匹配**：现在交叉共享库链接会把
  `rt_io/rt_sync/rt_file/rt_embed` 重定向到 `toolchain/<target>/` 下目标 ABI 的
  `.o`（`main.c:3162-3217`），产物缺失时报 `bundled ... runtime object not found`。
  `tests/emit_lib` 四个用例都在 standard 档、无 WILL_FAIL 标记，2026-08-27 实测
  `ctest -L standard -E "gui|policy"` 全绿（含 `emit_lib_linux_so` /
  `emit_lib_macos_dylib`）。**本节其余描述已过时**：`tests/emit_lib/lib.zan` 现在
  只有纯静态方法，不再拉 embed/sync 运行时。
  仍存在的无关拒绝分支：非 Linux/Windows/macOS 目标的 `shared libraries are not
  supported for this target yet`（`main.c:3533`）。

〔顺带已修〕本机 `.dll` 的链接行缺 `-lwinpthread`，而 `-lgcc` 的 unwinder 走
POSIX gthr，`pthread_*` 全部未定义 → `emit_lib_windows_dll` 链接失败。已在
`dllcrt` 列表补上（exe 链接行本来就有），用例恢复通过。

# A49 · 整数文本化的两处旧缺陷（2026-08-14）

整数直写 itoa 替掉 `snprintf` 时（`irgen.c` 的 `__zan_itoa64`）核对边界，发现两处
与新老降级无关、基线同样存在的语义缺陷（`3a5f6ec1` 与本轮产物逐字节相同）：

* [x] **A49-1 `byte` 插值按有符号扩展** —— ✅ 已修。`byte b = 200; $"{b}"` 打印
  `-56`：`irgen_expr.c` 的插值整数分支对窄整数一律 `SExt`，而 `byte`/`bool` 在本
  降级里是无符号。改成走 `zan_iwiden`（≤8 位零扩展），`{b:F2}` 的 `SIToFP` 也
  一并拿到正确的宽化值。用例 `tests/conformance/int_format_boundaries.zan`。
* [x] **A49-2 `StringBuilder.Append(ulong)` 丢无符号语义** —— ✅ 已修（2026-08-27）。
  实测除 `sb.Append(umax)` 打印 `-1` 外，**字符串拼接 `"" + umax` 同样打成 `-1`**
  （`Console.WriteLine` / 插值 / `Convert.ToString` / `.ToString()` 四条路径本来就对）。
  根因是两个格式化点拿不到 Zan 静态类型：`irgen_call.c` 的 StringBuilder 整数分支
  把 `emit_itoa_into` 的无符号标志硬编码为 0；`irgen_generics.c` 的 `emit_to_cstr`
  同样硬编码。修法：`emit_to_cstr` 增加 `is_unsigned` 参数（保留同名签名的有符号
  包装给拿不到类型的调用方），`emit_to_cstr_of` 本就持有 AST 节点，按
  `infer_expr_type` 的 `TYPE_ULONG` 传入；StringBuilder 侧改用 `expr_is_ulong`。
  用例：`int_format_boundaries.zan` 补上 `sb.Append(umax)` 与 `"" + umax` 两行
  （即该条目当初留的空），`ctest -R "int_format|unsigned|concat|interp"` full 档 27/27。

# A50 · CEF 浏览器控件的剩余缺口（2026-08-16）

浏览、导航、事件、CDP 原始通道、按事件名订阅、Cookie、下载/对话框/console/网络
钩子、代码级路径配置（`CefOptions`）都已就位（见
`examples/gui_cef_browser/README.md`）。剩下的都要动原生 shim
`stdlib/Gui/Component/CefBrowser/native/zan_cef.c`，注意同一份 C 要同时编出 CEF 151
与 CEF 109 两个 driver 变体，而这些回调的签名在两个版本间不同（例如
`on_before_popup` 在 151 上多了 `popup_id` 参数），必须按变体分支并各自编译验证：

* [x] **A50-1 弹窗策略**（`cef_life_span_handler_t::on_before_popup`）：三档已就位
  —— `web.OnPopup(cb)` 拦下并把 URL 交给宿主（宿主自己开标签）、`BlockPopups()`
  让 `window.open` 返回 null、`AllowPopupWindows()` 回到 CEF 自己开窗。
* [ ] **A50-2 右键菜单**（`cef_context_menu_handler_t`）：禁用/定制默认菜单。
* [ ] **A50-3 查找与打印 UI**（`find`、`print`、`print_to_pdf`）。
* [x] **A50-4 GPU 子进程崩溃与「Timeout of new browser info response」**：Windows
  实机带 verbose 日志复现完毕。GPU 子进程在 viz 起来后、建 GL 共享上下文时
  `STATUS_BREAKPOINT`（`exit_code=-2147483645`）连崩 3 次，第 4 次退化到
  `--use-gl=disabled` 并报 `Failed to create shared context for virtualization`
  —— 触发者是 DirectComposition 交换链（装了 IDD 虚拟显示适配器的机器）。
  `CefOptions.disableDirectComposition`（`ZAN_CEF_NO_DCOMP=1`）后同一场景 GPU
  崩溃 0 次、硬件 GL 仍走真实显卡；默认是否按机器自动关 DComp 待定。
  「Timeout of new browser info response」固定 2 条，与 GPU 崩溃、宿主消息泵
  （同一次运行 `[pump]` 0 条）都无关，按 CEF 侧噪声对待。
  顺带修掉一个真 bug：helper 子进程之前拿不到 `CefOptions.switches` /
  `ZAN_CEF_SWITCHES`（`zan_cef_execute_process` 不接开关），因此只有
  Chromium 自己会转发的开关能进子进程；现在浏览器进程与子进程用同一份开关。

* [x] **A50-5 helper 子进程的 CefOptions 文档化（2026-08-28）**：
  `CefBootstrap.RunHelper` 调的是 exec 出来的新进程，进程内
  `CefOptions.current` 静态字段不会被继承；helper 只能走环境变量
  （`ZAN_CEF_RUNTIME` / `ZAN_CEF_PROFILE` / `ZAN_CEF_CACHE` /
  `ZAN_CEF_DRIVER` / `ZAN_CEF_SWITCHES` / `ZAN_CEF_LOCALE` /
  `ZAN_CEF_HELPER` / `ZAN_CEF_DOWNLOAD_UI` / `ZAN_CEF_NO_DCOMP` /
  `ZAN_CEF_MIRROR` / `ZAN_CEF_ARCHIVE`），加上浏览器进程
  `zc_export_helper_env` 在 fork 之前 setenv 的
  `ZAN_CEF_HELPER_RUNTIME/SWITCHES/DRIVER`（macOS/Linux；Windows helper
  是自己）。已在 `CefBootstrap.zan:RunHelper` 与 `CefOptions.zan` 文档里
  写明。先观察、暂不实现 `Use` 透传——profile 槽位有 `File.TryLock`
  串行化、helper 走 env 兜底够用，没有发现"撞 slot"的实际 bug。


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


# 已完成条目（2026-08-23 ~ 08-31 一行摘要）

* **A43 · enum.ToString() 返回成员名** ✅（2026-08-23）：irgen 对 TYPE_ENUM 接收者生成分支链+phi join（select 会双求值回退臂致泄漏），`enum_tostring.zan` 13/13；`Color.Red.ToString()` 静态成员链与 `TryParse` 亦可用（`enum_static.zan`）。**仍待做**：批量清理 stdlib 手写 int→名映射链（`Log.LevelName`、ChartModel 19 处、ChartView 12 处等），收尾项非能力缺口。
* **B9 · Web 框架现代化** 🟡（2026-08-23，2/3）：① `[Tx]` 事务作用域（Controller 三虚钩子 + GenRoute 生成 Begin/Commit/Rollback）；② 全基元签名参数绑定（蹦床 NeedX，缺失/非法统一 400/0003，参数进路由文档表）。**未做**：③ GenDb 仓储生成——从 genmeta 生成 `XxxDao` CRUD 门面 + 控制器接线，手写 DAO 保留为复杂查询出口，注入点用已落地的 `__Bind`/`__BeforeAsync`。
* **A44 · 生成器元数据收敛（genmeta calls 裁剪）** ✅（2026-08-24）：`gm_prune_calls()` 只保留生成器消费的调用点 + recv_id 双向闭包保 fluent 链，ZanIDE 整编元数据 14.1MB→1.53MB；顺带 json.c JSON_MAX_DEPTH、genrun 解析失败显式报错。
* **A51 · `params T[]` 回归 C# 数组语义** ✅（2026-08-27）：parser 保留 `T[]` 声明类型、打包改 `new T[]{...}`、打分按元素类型——修掉全变参调用被毙的回归；Lua/Python 调用点同步。**遗留**：自举编译器仍按 List 降级（见 B6-SH1 追加分叉）。
* **A52 · 静默错误代码生成封口（第一批）** ✅（2026-08-27）：标量进引用形参报错（class 目标收窄为不判，保单参构造器隐式转换）、校验窗口扩到裸名调用、扩展方法打分全否决不再兜底取首、UI dispatch 队列满不再丢工作（1024 起步按需翻倍到 1M）。剩余子项见下节。
* **A53 · 被重新赋值的引用参数不拥有其槽位（堆损坏）** ✅（2026-08-27）：`own_written_param()`——体内写过的 rc 参数入口 retain 走 owned 槽，普通方法与泛型特化两条绑定路径都接上；`param_reassign_ownership.zan` 三档孪生。
* **A54 · LINQ 查询子句八处缺陷** ✅（2026-08-27）：loop_close 双终结符、join into 语义与悬空块、查询序列所有权、Grouping 无类符号、join 左键循环外发射、MergeSortKeys 改写调用方键列表、join 后行物化局部类型未跟换与 struct 双层槽；`linq_query_clauses.zan` 关闭 A43-B17。
* **A55 · 值类型的类型模式永不匹配** ✅（2026-08-27）：case 匹配只处理 class/string/object，值类型臂恒 false——改为静态类型相等发常量条件；`pattern_value_types.zan`。

## A52 剩余子项（原「下一批待做」）

- [ ] **A52-5 `--publish` 的安全网**：`main.c:2335-2338` 让 `check_leaks` 与
  `arc_guard` 都只在 `debug_info && !publish_mode` 下开启，发布版本恰好没有
  over-release/UAF 检测；要定一个"低成本子集在发布版也保留"的方案。
- [ ] **A52-6 null 解引用守卫**：普通 `obj.f` 直接 fault。〔2026-08-31 复核：A77 已给 `runtime_checks` 档加 null 接收者守卫（成员读/数组/字符串/替换页），出厂档的通用守卫仍未做——取舍见 A58 3.4〕
- [ ] **A52-8 库内单方面终止进程**：OOM（`host_oom.h`）、契约违反
  （`rt_sched.c:242`）、slab 一致性（`rt_mem.c:446,453`）共十余处 `abort()`，
  作为被嵌入的库没有错误码出口。
- [x] ~~A52-7 EH 线程表 1024 硬顶~~ → **A78-2** 已完成（2026-08-31，动态哈希表）。

---

# A56-A79 · 已完成条目（一行摘要）

> 详细记录（根因/落点/探针/回归数字）见 git 历史与 `_scratch/TASKS.md.bak-2026-08-31`。⏳ 的为遗留项。

* **A56 · using 闭包瘦身第一批** ✅（2026-08-27）：MessageBox→System.Windows、DirectoryWatcher→IO.Watch、RandomNumberGenerator/Guid 解耦、双 Stopwatch 合并——`using System` 从 80 文件 564KB 瘦到 16/393KB，standard 558/558；暴露并修掉 mmap 前缀表、缺 using 声明、测试写 build/_scratch 三类隐藏耦合。**未做**：Automation/Management/Windows 去 Threading+Diagnostics 税（见遗留专项）。
* **A57 · FormBuilder 逻辑像素重构两处回归** ✅（2026-08-27）：补标题只认真写了 label；SetRowHeight 写入 100% 基准镜像。**遗留**：leakcheck_checkbox_group 引用环（见遗留专项）。
* **A58 · 全量收口执行计划** 🟡（2026-08-27 定序）：批 1（封死静默产错码，含 1.5 有诊断即停 codegen）与批 2（验证基建：arcguard 435 项档、sanitizer 扩容、前端 fuzz 扩容抓出 41KB lexer 栈帧真 bug）完成；批 3 的 3.1/3.2 与批 4 的 4.1/4.2 已完成（2026-09-14），批 3 剩 3.3/3.4 两处待拍板取舍（见下）。正文见下。
* **A59 · Gui.Image 图片组件** ✅（2026-08-28）：本地/http/base64/SVG 传地址即渲染。
* **A60 · 局部帧裁剪吃掉条带外点击 + Switch 禁用可点 + 条件真值化 i0** ✅（2026-08-28）。**遗留未解**：注入点击批次偶发整批丢失（1/40，press 到而 release 未泵出，锁屏/高负载时段），待可复现样本查 `Window.InjectEvent`→原生队列→泵路径。
* **A60 · PivotTable 整体重写** ✅（2026-08-28）：滚动/选中/百分比/热力/钉住合计；边框二修（底色与窗口同色致断裂观感）+ 悬停越界修同批。
* **A60 · 百万行×70列导出压测探出 irgen 泄漏（链式调用接收者临时量不释放）** ✅（2026-08-28）。
* **A61 · DynamicTags 动态标签** ✅（2026-08-28）。
* **A62 · Watermark 水印组件 + 运行时旋转文字** ✅（2026-08-28）。
* **A63 · Countdown 倒计时组件** ✅（2026-08-28）。
* **A64 · System 栈封装审查第一批** ✅（2026-08-28）：MQTT/WebSocket 边界、ORM 错误 DbException 化、HttpClient keep-alive、HttpsServer 二进制体、MVC 参数族、ODBC 诊断、SQLite prepare 缓存。**明确延后**：ODBC prepare 缓存待 live 驱动环境。
* **A64b · System 栈封装审查第二批（P3 收尾 + accept 唤醒）** ✅（2026-08-28）：CSRF 中间件（opt-in）、WssServer 回调对齐、TcpListener.CancelIoEx 唤醒 accept、MqttBroker 真停机、字符串扫描器沉淀。**遗留**：8 项 leakcheck 仍红待查（见遗留专项）。
* **A65 · Image 渲染边界 + Carousel 控件页 + HttpClient 二进制 GET** ✅（2026-08-28）。
* **A66 · gallery Switch/Radio 演示扩充** ✅（2026-08-28）；**A66 · Carousel 纵向方向 + 演示重做 + autoplay 即时模式修复** ✅（2026-08-28）。
* **A67 · string[i] 静态类型在 irgen 丢失（char 逐字文本化输出十进制码）** ✅（2026-08-28）。**语言级遗留**：字节×码点语义冲突待专项定夺（见遗留专项）。
* **A68 · null 引用进入字符串头探针（IsBadReadPtr(null-8) 在 KERNEL32 内 AV）** ✅（2026-08-29）：`zan_hdr_read_ok` 判空控制流先行短路，探针调用不发射。
* **A69 · PivotTable 部分可见行无裁剪（行头/行合计越进表头带）** ✅（2026-08-29）。**遗留观察**：`ResolvedSeries_Of` 读 addr=0x5（null ChartSeries 应用级空引用，1/41），随 gui_charts 重构观察。
* **A70 · NumberAnimation 数值滚动动画组件** ✅（2026-08-29）。
* **A71 · Steps 强化 + base.Method()/base.Prop 编译器支持** ✅（2026-08-29）。
* **A71 · Gui 图标表 JSON 数据包 + zanc 自动内嵌** ✅（2026-08-30）：空窗 2.08MB 里 483KB 图标出体；**后续路线**见遗留专项。
* **A72 · Pinyin 词典数据文件化** ✅（2026-08-30）：91% 字面量密度出体（6,763 行 GB2312）。
* **A73 · 优化第一批：inline 阈值按优化档位分档（−23%）+ PIC 假设证伪** ✅（2026-08-30）。
* **A74 · function-sections + --gc-sections（发布档）+ 三表钉死量化** ✅（2026-08-30）：空窗 -167KB；实锤 895/1010 定义函数被 site_dtors 钉死、~72% .text 可去死。
* **A75 · ARC 描述符头（per-shape desc 指针，site_dtors/site_tynames/site_meta 三张钉死表退役）** ✅（2026-08-31）：空窗 -3.5%，globaldce 重新生效。
* **A76 · WSL null 字符串探针受控化 + 崩溃日志双 0x** ✅（2026-08-30）：length 探针/元素访问受控报错，跨平台 408 项批跑；**遗留** 3 个应用层 crasher（见遗留专项）。
* **A77 · 运行时守卫双路径化（fail-soft 记录不闪退 + ZAN_RT_HARD=1 exit(70)）** ✅（2026-08-31）：soft 路径 stderr 去重一次 + 崩溃日志 + 256B 替身页继续跑；cross-rt 六目标重建（win-arm64 待 CI/CLANGARM64）。
* **A78-1 · ARC 站点表 4096 上限拆除** ✅（2026-08-31，动态哈希）。
* **A78-2 · EH 线程表 1024 动态化** ✅（2026-08-31）：开放寻址哈希 + 倍增重哈希（修两个只在首次重哈希可达的自埋雷：扫带未掩码回绕、states 拷贝双重步长），>3000 并发带 EH 线程绿；关闭 A52-7。
* **A78-4 · 守卫串体积治理** ✅（2026-08-31，两刀）：路径归一化 + 两级短路径 + 同文 intern + msg 模板共享（`zan_rt_soft_note2` 按前缀指针判重、hard 路径函数入口 [1400 x i8] 合成槽）；cross 目标走 merged 回退待 zig 重建后切换。6.6MB→1.5MB（−67%）。
* **A78-5 · 守卫 hard 路径收编运行时 + 残余同文全局全量 intern** ✅（2026-09-01，commit 3a89d1e6 随并行会话落库）：`emit_guard_report2` 每守卫点从内联 is_hard 分支 + 合成槽 strcpy/strcat + printf + fflush + RaiseException + exit（IR ~15 行/.text ~70B）改为单次 `zan_rt_guard_fail2(prefix,msg)` 调用（src/runtime/rt_timer.c，内部完成 is_hard 判定/软报告/硬路径打印+SEH+exit，RaiseException 0xE0A2C010 语义与 rt_crash.h 逐字对齐）；`fatal_fmt`(58.6k)/`oomtxt`(6.3k)/`extarg.empty`(420)/`ehoom`/`aexfmt`/`die|reh.*fmt`/println/leak/site/tn 等全部 `LLVMBuildGlobalStringPtr` 残余站点改走 `zan_irgen_intern_string`。实测 gallery（HEAD 源）：.ll 187.2→129.9MB（−30.6%）、-O0 exe 21.8→14.1MB（−35.3%）、publish exe 12.05→8.92MB（−26.0%）。守卫行为探针 soft rc=0 继续/hard rc=70/崩溃日志 `zan=`+`exit=70` 记录全绿；tests/runtime 9 项 fail-fast 契约全过。残余：`@rterr.*` 46k 前缀串已是 split 形态、每站点一条属语义必需（软报告按其指针判重），站点串改 (file,line,col,msg) 编码另案。
* **A79-1 · irgen 全函数写扫描 O(N²) 根治（body-write memo）** ✅（2026-08-31，两 bit 哈希 memo）：`local_is_lambda_written`（每个局部声明问一次）与 `body_writes_ident`（每个 rc 形参问一次）各自对整个函数体跑一遍 cap_scan——N 个声明的方法 O(N²) 次访问：合成探针 3000 decl=12.5s、6000=122.6s、12000=1091s，gallery.RenderPreviewEx 单方法 7.25s。修法：每函数体一次全 AST 走收集全部赋值标识符，{body,name} 开放寻址表（标识符经 lexer intern，指针判等），每名两 bit——`written`（体内任何位置赋值，形参所有权问题）与 `lam_written`（仅在嵌套 lambda 内赋值，boxed-local 问题；合并两问会把普通赋值局部错关进堆单元，首轮即踩过）。cap_scan 路径保留为 calloc 失败回退。实测：12000-decl 探针 1091s→0.55s，RenderPreviewEx 7252ms→157ms，gallery dev 全编 21.4s→10.7s，gallery publish 80.3s→46.0s。**A79-2 · publish 机器码降档探针（本窗并行测毕）**：gallery publish 对照——CodeGenLevelDefault 46.0s/8.93MB、Less 49.8s/8.93MB（更慢，无收益）、None 28.8s/14.13MB（−17s 但 +58% 体积，FastISel 裸出无折叠）——不值，发布档保留 Default；今后 publish 提速应攻 optimize 档（Os pass 管线）与 .o 体积，而非机器码降档。
* **gallery 目录外置化（assets/gallery.json）** ✅（2026-08-31）：84 组件 270 演示卡外置，gui_gallery.zan 1.09→0.67MB。**已知问题**：build_gallery.ps1 手挑 stdlib 文件列表被并行 stdlib 新依赖（System.Json/Mqtt）建挂，待依赖链收口后补文件或改 `--auto-stdlib`。
* **SelectBox 弹层选项选不中（基线命中）** ✅（2026-08-29，commit 5d11b650）：`HitTestFrom(px,py,from)` 基线命中原语，弹层只匹配自己 blocker 之后注册的区域；无头回归 selectbox_popup_hit_test + 有头探针验证。
* **Gui/Chart 后续（2.2.7 对齐轮遗留）**：timeline 子系统、connect() 多图联动 ✅（2026-08-30，ECharts 134/134 全 ✅）。**架构注记 A · 共享坐标系共存层** [ ] 仍开放：跨图族混搭需抽共享坐标系/布局层供多渲染器叠画，重构面大于收益，待真实需求立项。

## A78-3 · 泛型实例化第 64 个起程序 abort（原记“堆损坏/编译器宿主段错误”）—— ✅ 已修（2026-09-11）

- **现象（修正后）**：单模块内同一泛型类实例化到 **63 个通过、64 个起 `abort()`**
  （Windows 退出码 3，无输出、无诊断）。触发条件不是实例化个数本身，而是**需要按实例逐份
  发射方法体的泛型类**（体里用到类型参数，如 `Wrap<T>` 的 `Show()` 调 `item.Name()`）；
  只靠擦除形态就够用的实例化（类实参、体不碰 T）到 100 个也正常——原记录的
  “堆损坏 / 编译器宿主段错误”是误判，真实表现是运行期 abort。
- **根因**（正是原记录猜的“某 64 槽表零起步减一”）：`src/compiler/irgen_emit.c` 的
  `variants[64]` 有一槽放擦除版（`NULL`），循环守卫 `nvar < 64` 只装得下 **63** 个具体实例化；
  第 64 个实例化的 `Wrap_Show$T63` 从未发射，调用点 `route_generic_method` 查
  `find_generic_fn` 落空，退回擦除版 `Wrap_Show`——而擦除版的体就是 `call abort();
  unreachable`，进程随即以 3 退出。同族的 `insts[64]`（按实例逐份初始化的静态字段）
  同样从第 65 个起被静默丢弃，那些实例化的静态字段停在 0。
- **修法**：两处定长栈数组改成按需倍增的堆数组（`variants` / `insts`），不再静默截断。
- **探针与回归**：`tests/conformance/generic_inst_count_70.zan`（70 个实例化，同时覆盖
  64 边界的专用方法与 65 边界的静态初始化器）输出 `2485/70`；conformance / leakcheck /
  arcguard 三孪生全绿，修复前 64 个即 rc=3。
- **诊断增强**：`ZANC_TRACE=1` 下 `discover_generic_insts` 报实例化总数，
  `route_generic_method` 在“具体实例化找不到专用体而退回擦除版”时报
  `route miss: Type.Method argc=..`——本条 bug 靠这两行可在分钟级定位。

## A70 · `Thread.Start` 不能携带实例方法组 —— ✅ 已修（2026-09-11，采纳原建议①）

- **症状**：`Thread.Start(job.Run)`（实例方法组）启动瞬间 SEGFAULT，无诊断；静态方法组同形写法正常（`_scratch/threadjob.zan`）。
- **根因**：委托 ABI 里实例方法组是带 ZAN_CLOSURE_TAG 的堆记录指针（bit0=1），`rt_sync.c` 的 `zan_thread_start` 把参数原样当裸 `void(*)()` 调用即崩；checker 不拦。
- **修法（根治）**：native `zan_thread_start`（Win32 `CreateThread` 与 POSIX `pthread_create` 两路）改为与 wasm 版、UI 派发队列同构——trampoline 先按 bit0 判形态：带 tag 就卸 tag、取记录 fn 槽按 `fn(record)` 调用（捕获 lambda 同理），裸指针才直接调；`zan_thread_start` 侧 retain、trampoline 收尾 release——调用方在 `Thread.Start(...)` 语句结束就释放自己的临时量，工作线程可能还没跑，不 retain 必 UAF（实测去掉 retain 后 `-g` 下即段错误）。
- **曾用的回避（stdlib 两处同构）**：`ImageHttp.zan`（下载）与 `Upload.zan`（上传）线程入口用静态方法组 + 队列传实例；现在可以撤，但线程内传状态走队列/原子本就更清晰，故本轮不动这两处。
- **回归**：`tests/conformance/thread_start.zan` 扩为四种形态（静态方法组 4×3；临时接收者实例方法组 `new Job(7).Run`；捕获 lambda 100；捕获变量 lambda 40 → 159），并补 50ms 收尾让 trampoline 的 release 先于进程退出；conformance / leakcheck / arcguard 三孪生重跑 75 次全绿。

---

## A83 · ra2 tests 4 个 golden output differs —— ⏳ 已登记未修（2026-09-04）

- **现象**：`bash examples/game/ra2/tests/run.sh` 10/14 过；余 maprender/objart/
  objects/texture 四个 "output differs"。差异集中在像素级数值
  （rgba=ff040404 → ffb6964d、overdrawn=30 → 0 等），输出确定性一致（重跑同值）。
- **边界**：与 GameHud 迁移无关——迁移只改 using/KitUi 改名/null 守卫/icon
  上传，MapRenderer/ObjectArt/Texture 渲染路径零触碰；golden 自 08-06 未更新，
  期间 2d3c38b33 后 ra2 渲染相关提交（c3750b339 的 RoundRectStroke 真环形重写
  等）改变了绘制输出。0.14 全 compile error（GameKit 删除后）→ 现 10/14。
- **处置**：需逐个 diff 核对是渲染改进（更新 golden）还是回归（修代码）。
  属 ra2 示例维护，另案处理。

## A81 · ra2 示例被空安全收紧编译打洞 —— ✅ 已修（2026-09-04 晚，GameHud 迁移同窗）

- **现象**：`bash examples/game/ra2/build.sh` 报 17 个 error（2026-09-04 实测），
  全部是 `ra2/game/GameController.zan` / `ra2/game/Objects.zan` 里
  `'TypeFor' can return null; accessing 'Prereqs'/'Cost'/'Hp' on its result faults`
 ——新版 checker 的空安全诊断，ra2 自身代码没跟上。
- **证据**：与 GameKit/legend 无关——legend 全量回归绿（ACT 93 动作 PASS、
  SIM CHECK PASS、TOUR 2×33 面板 OK、SHOT 9 图 OK），snake 连 GameKit 编译通过；
  报错文件本会话未触碰（工作树仅 `ra2/main.zan` 的 2 处 DrawShadow 实参修正）。
- **处置**：✅ 已按诊断逐处先存后判修完（GameHud 迁移会话顺带）：
  GameController 2 处（TypeFor null → return/continue）、Objects.zan 1 处
  （TypeFor null → continue）、main.zan 8 处（ProdCount/ProdUnitAt/DrawProduction
  的 TypeFor+Selected null、 CivType null → continue）、main.zan DrawTexture(icon)
  的 Texture→SdlTexture 转换（icon 是 CPU 侧 RGBA 图，补 CreateRgba32+Update
  上传后绘制，同 Screens.LoadPcx 路径）。tests/ 4 个用例同步补 null 守卫
  （objects/lzo/theater 的 TypeFor/At/SetForTile）。全量 ra2 编译 0 error，
  tests/run.sh 10/14 过（余 4 个 output differs 见 A83）。

---

# A58 · 全量收口执行计划（2026-08-27 定序）


排序依据三条：① 先修"编译通过但结果错/编译器崩"的，因为它们会污染后面每一次
验证；② 验证基建提前到第二批，之后所有修复都有它兜底（本轮的 A53 堆损坏若有
ASan 档会当场被抓，而不是从 `orderby` 错序回溯半天）；③ 需要定性能/语义取舍的、
以及会与 GUI 并行编辑冲突的，排到取舍确定之后。

**跨批规则**：每项完成即在本文件补实测证据（命令 + 数字 + 用例名）；每批结束跑
`smoke` → `standard`；触及 `stdlib/Gui` 的项必须先与 GUI 编辑窗口协调（当前工作树
有未提交的 Gui/Chart 改动）；不把多阶段揉进一次提交。

## 第 1 批 · 封死"静默产错码"（编译器，低风险高价值）

| # | 内容 | 验收 |
|---|---|---|
| 1.1 | **A54-3 `join` 生成不合法 IR**（`Basic Block ... does not have terminator!`，`query_materialize_join` 少一条终结边） | 三种 join 形态（裸 / join+orderby / join…into）编译通过且结果正确 |
| 1.2 | **A54-1 / A54-2** `orderby ... descending` 与 `let` 返回空结果 | 升/降序、多键、let 的结果逐项正确 |
| 1.3 | **A54-4** 显式泛型实参的扩展调用（`nums.OrderByKeysInt<int>(keys)`）结果随机（读未初始化内存） | 与静态调用形态结果一致，多次运行稳定 |
| 1.4 | 补 `tests/conformance/linq_query_clauses.zan` golden（本轮已写好、因 1.1-1.3 失败暂未入库），**关闭 A43-B17** | conformance/determinism/leakcheck 三档 |
| 1.5 | [x] **有诊断即停止 codegen** + 清掉真正会静默产错码的兜底 | 见下；standard 589 项全绿（唯一失败 `policy_theme_color_budget` 是未提交的 Gui/Chart 改动，与编译器无关） |

1.5 放在 1.1-1.3 之后：它会把此前被兜底掩盖的错误一次性暴露出来，先修掉已知的
才能分清噪声。

**1.5 实测结论（修正原条目的"~33 处"口径）**：`return LLVMConst*` 在 irgen 六个
文件里共 121 处，其中 85 处附近没有诊断——但绝大多数是**合法零值**（void 表达式的
占位、布尔常量、不可达分支默认值），逐个改掉是错的。真正"解析不出来就当 0"的只有
**分派链末端**一处，已按下述三点收口：

- `irgen_call.c` 调用发射器末端：此前为几种已知形态各自补过特判诊断（内建成员不
  存在、属性误写括号、静态调用链未解析），每条都是踩过一次坑后补的；现在把通用
  情形也改为报错——走到末端说明没有任何 lowering 认领这个调用，即调用根本不会发生、
  表达式恒为 0，这是编译错误而非零值。两个必须保持静默的上下文：泛型的**擦除体**
  （接收者类型仍是未绑定类型参数，真实代码在单态化副本里，擦除体的 0 不会被执行，
  由新增 `call_receiver_is_open_generic` 判定）和已有错误后的级联。
  新增 `tests/diag/call_not_callable.zan`：`int count = 1; count(5);`
  ——调用一个非可调用局部变量，此前静默编译并打印 0。
- `irgen_expr_core.c` `find_ctor` 的 `locals==NULL → return first`：实测六个调用点
  全部传入非空 `locals`，该兜底是**死代码**；删除后 standard 全绿，同时删掉随之无用
  的 `first` 追踪。留 NULL 给调用方按"未解析"处理，避免按声明顺序挑构造函数。
- `irgen_emit.c`：codegen 内部在 pass 2（用户方法）与方法特化队列排空后各加一处提前
  返回。阶段级把关本来就没有缺口（解析错→不进 binder/checker；检查错→不进 codegen；
  codegen 错→finalize 前返回失败），所以提前返回**不改变成败**，只是不再把半成品函数
  体带进后面的合成 pass（类释放函数、vtable、反射表都假设引用到的函数/槽位存在），
  避免一个已报告的错误表现成编译器崩溃。

## 第 2 批 · 验证基建（一次性建设，后续所有批次的兜底）

| # | 内容 | 验收 |
|---|---|---|
| 2.1 | [x] **生成程序跑带守卫的 conformance 全量**（不是 ASan——见下）：新增 `arcguard_*` 档，435 项 | ✅ 全量 435/435 绿；回退 A53 验收通过（下） |
| 2.2 | [~] sanitizer 覆盖从 `rt_sched`/`rt_io`/`rt_co` 扩到 `rt_sync`+`rt_file`/`rt_timer`/`rt_mem` | 已写进 `runtime-tests.yml`，**本地无法验证**（下） |
| 2.3 | [x] 前端 fuzz 从 parser 扩到 nsresolve+binder+checker | ✅ 44.6 万次执行零崩溃；首轮即抓到一个真 bug（下） |

### 2.1 修正：ASan 是错的工具，`--arc-guard` 才是（三条实测理由）

原条目写"生成程序在 ASan 下跑，回退 A53 必须当场报错"。实测这个前提在三个层面都不成立：

1. **默认路径 ASan 看得见，但看不见要紧的那半。** ARC 对象默认走 libc
   malloc/free（`rt_mem.c` 的 slab **只在 `--fast-alloc` 时链接**），所以 ASan 能拦
   double-free；但 A53 是"提前释放后又被**读**"，而 zanc 发射的目标代码没有插桩、
   不查 shadow，UAF **读**结构上就抓不到。要抓得给 codegen 挂 LLVM AddressSanitizer
   pass，那是另一个量级的工程。
2. **开了 `--fast-alloc` 反而更看不见。** slab 释放只是压回 free list、slab 永不
   解映射，ASan 的 malloc 拦截被完全绕过。（slab 自己有常开的 double-free 与坏头
   检测，见 `zan_mem_hdr_check`；这条对 UAF 读同样无效。）
3. **本机无法验证。** Windows 上 clang 的 ASan 拦截是坏的
   （`interception_win: unhandled instruction`，对赤裸 double-free 静默退出 0），
   UBSan 运行时链接失败（缺 `__imp_getenv`、`ContinueOnError`）。

**真正的检测器早已存在且更贴合**：`--arc-guard`（`irgen.c` 的
`emit_arc_underflow_check` / `emit_arc_freed_use_check`）把释放后的对象打上
`ZAN_ARC_FREED_MARK` 隔离，任何经陈旧引用的 retain/release 在**第一次后续使用**
就被捕获，并用 `freed_by` 指出结束其生命的那次释放。这正是引用计数自身看不见的
那一类：**缺一次 retain**，计数是平的但引用活过了对象。缺口只是**它从未接入任何
测试档**（此前仅出现在开发脚本，`-g` 默认打开）——这就是 A53 能溜过整套测试的原因。

新增 `tests/run_arcguard.cmake` + 每个 conformance 源一个 `arcguard_*` 孪生
（435 项，`full` 档，与 `leakcheck_*` 一同构成 ARC 所有权的两面：漏一次 release
与漏一次 retain）。**验收（按原条目要求实做）**：临时回退 A53 的
`own_written_param`，同一个程序——

- 不带守卫：退出 `0xC0000374`（STATUS_HEAP_CORRUPTION），**零输出**，无从定位；
- 带守卫：`ARC integrity failure: retain through a stale reference (object was
  freed: missing retain when stored)`，附对象地址与释放点。

### 2.2 已写但**未本地验证**（需 Linux 首轮 CI 判定）

`runtime-tests.yml` 的 sanitizer job 原先只编 `rt_test.c` + sched/io/co 三个源。
扩了三步，每步单独成 step 以便失败时直接指名：`rt_sync`+`rt_file`、`rt_timer`
（经 `rt_sigpipe_test`）、`rt_mem`。

两个判断：① 不往 `rt_test.c` 的链接行里塞源文件——**那个 harness 对这三个模块
一个用例都没有**，塞进去只是编译而从不执行；改为直接构建 CMake 已为它们注册的
测试程序。② `rt_mem` 只上 UBSan、不上 ASan：它以 `--wrap=malloc` 链接并调用
`__real_malloc`，与 ASan 的分配器拦截争同一批符号，且 ASan 本来就看不见 slab 块；
而 UBSan 无需拦截，对这个满是原子操作、指针算术与对齐掩码的文件才是真正的收益。

**未验证的原因**：本机 Windows 上 ASan/UBSan 运行时均不可用（见 2.1 第 3 条）。
可本地验证的部分已验证：`ctest -R "^runtime_"` 12/12 绿，源文件清单逐字抄自
`CMakeLists.txt`（596-638、932-967 行）。`rt_sigpipe_test` 与 `rt_mem_*` 是
POSIX-only，Windows 上根本不注册，因此首轮 CI 是它们的第一次真实执行。

### 2.3 前端 fuzz 扩容 —— 首轮就抓到一个真 bug

`tests/fuzz/fuzz_parser.c` → `fuzz_frontend.c`：按驱动（`main.c` 2178-2291）的
真实相位顺序补上 flatten/merge/desugar → `nsresolve` → `binder` → `checker`。
**相位闸门照抄驱动**不是优化而是结论可信度的前提：驱动只在解析干净时才做解析后
各步、只在那些步干净时才 bind，喂给 binder 一个半解析 AST 会在编译器永远到不了
的状态里崩，每个这样的崩溃都是要花一轮排查的假报告。链接集仍无 LLVM（9 个源
文件），job 因此仍是秒级；fuzz irgen 需要 LLVM 与 target machine，那是另一个
harness，不是本条的延伸。删掉被完全取代的 `fuzz_parser.c`（CI 已不构建它，留着
只会腐烂）。

**首轮 1 秒内即崩**，且经两个 harness 对照定位到 **parser**（既有 bug，非本次扩容
引入）。真因不是深度而是**栈帧**：parser 有六处 `zan_lexer_t saved = *p->lex;`
做试探性回溯的栈上快照，而 `zan_lexer_t` 有 **41480 字节**，其中内联的
`defines[128]` 预处理表独占 40960。于是 `parse_postfix`（41960 字节栈帧，且**就在
表达式递归环里**）、`paren_is_named_cast`、`looks_like_local_func` 各自 41KB，
1MB 栈在 **约 25 层**就耗尽——`parse_unary` 那条 256 层守卫**根本到不了，是死代码**。
实测 **50 层配平括号**即 `0xC00000FD`（STATUS_STACK_OVERFLOW），无任何诊断；
而括号**不配平**时诊断是正常的（`expected ')'`），所以这条一直藏在畸形输入之外。

修法：`defines[]` 改为 arena 分配的指针（`lexer.h`/`lexer.c`）。六个快照点**一行
未改**，语义完全不变——快照带着标量 `define_count`，恢复即截断试探期新增的宏
（活跃项恒为 `[0, define_count)`）。效果：`parse_postfix` 41960 → <1024 字节，
顶层两个 42KB → 约 1.4KB；`zan_lexer_init` 的 41KB memset 与每次试探 80KB 的
memcpy 一并消失（这同时是条性能修复：`parse_postfix` 的前瞻是热路径）。
现在 ≤255 层正常编译、≥300 层给出预期诊断，那条守卫首次真正生效。

入库：`tests/diag/expression_nesting_depth.zan`（400 层配平括号）+
`tests/fuzz/corpus/deep-nesting-stack-overflow.zan`（原崩溃样本，CI 语料现先播
`tests/fuzz/corpus/*`，回归在第一次执行就被抓到而非靠运气）。
修复后 240 秒 / **44.6 万次执行 / 3546 条边覆盖 / 零崩溃**。

## 第 3 批 · 运行时的"企业嵌入"门槛（含两处待定取舍）

| # | 内容 | 验收 |
|---|---|---|
| 3.1 | **A52-7** EH 线程表 1024 硬顶动态化 ✅；A4-2 剩余（`zan_thread_detach()` 接线）已并 **B5** 前置——今日回调源全为 Zan 托管线程，stdlib 零受益者（grep 证） | `thread_eh_slots` 扩到 >1024 并发仍跑完 ✅；峰值内存不回退 ✅（102.3MB→3.9MB） |
| 3.2 | **A52-8** 库内十余处 `abort()` 改为可注册回调 + 错误码出口（OOM、契约违反、slab 一致性）✅ **2026-09-14 完成**：`zan_rt_set_fatal_handler`/`zan_rt_fatal(category,message)` 落 rt_timer.c（崩溃机器所在地，每程序必链）；18 处活死点收编——rt_mem×3（double-free/CAS 竞态 double-free/slab 头损坏）、rt_sched×7（6 OOM + task 提前释放契约违反）、rt_io×1（注入节点 OOM）、rt_sync×6（TLS init/set OOM，原 `zan_host_oom` 藏在头文件里漏数）、rt_timer×1（co-live 表扩容）。fatal 单点打印 `zan runtime: fatal (cat): msg` → 回调（宿主自行 exit；接管后返回=违约仍 abort）；无回调 abort 兜底与今天一致（rt_mem 站点原 fprintf 文本并入 message，信息不丢）；rt_wasm longjmp 桩与 host_oom.h（编译器侧）不在范围。链面修复：zanrt_sync_test / zan_rt_mem_dblfree_test / zan_rt_mem_remote_test 补链 rt_timer.c。 | 新增"宿主接管后自行退出"用例 ✅（dblfree 测试扩 takeover child：handler 收到 cat/msg 原文后 exit(71)；返回型 handler 回退 abort 断言；WSL 实跑全绿）；无回调行为与今天一致 ✅（原 abort 双例原样绿）+ Windows runtime_sync/io_addr 绿 + 默认/--fast-alloc 双链接路径探针绿 + standard 845/850（5 红全归因：4 Gui/Chart 并行车道既有 + keepalive 负载 flake 独跑绿） |
| 3.3 | **A52-5** `--publish` 保留低成本安全网 ✅ **2026-09-14 完成（3.3 的行为取舍随实现一并落定）**：`--publish` 默认开"over-release 网"——`emit_arc_underflow_check` 双模式化，同一 rc<=0 比较，报告走 fail-soft note（每类一次，stderr + 运行时日志）后**继续执行**，绝不 abort/exit(70)（over-release 只泄漏，网不把泄漏变崩溃）。行为取舍拍板：默认=报告并继续；`--arc-guard` 完整陷阱仍专属诊断档；`--no-arc-guard` 同时关网（性能偏执逃生门）。跨目标回退 merged 单参文本（rt_guard_split 同门控）。新旗标管道 arc_net 穿 main.c→irgen_init（签名 +1 参）。 | publish 版本能报 over-release ✅（火线注入探针：--publish 二进制打印 "release of an already-freed string [ARC over-release net…]" 后 exit=0 继续跑完）；约定阈值内不退化 ✅（3M 轮饱和 retain/release 交错采样 min：string 形 545↔545ms ±0、Node 形 1050↔1072ms -2%；体积 +512B/exe） |
| 3.4 | **A52-6** null 解引用通用守卫 + opaque string 越界检查（~~需先定性能预算~~ **不是预算问题，见下**） | 新增诊断用例；裸循环/字段访问的基准不退化超阈值 |

### 3.3 实测（2026-08-27）：整体守卫不可出厂，但可拆出一个 4.3% 的子集

我原写"需先定性能预算"问错了问题。`--arc-guard` 的三个部件里，
`emit_arc_quarantine`（`irgen.c:869-870`）**故意泄漏每个被释放的对象**
——注释原文是 "leaked so its address is never recycled"，这正是陈旧引用可被检测的
前提。所以它不能进出厂二进制的原因不是 CPU 而是内存无界增长。

实测（`_scratch/arcbench.zan`，300 万轮 retain/release 饱和负载，取 3 次最好值）：

| 变体 | 用时 | 相对基线 | 峰值内存 | exe |
|---|---|---|---|---|
| 基线（无守卫） | 417 ms | — | 6.2 MB | 399.7 KB |
| **仅下溢检查**（无隔离区） | 435 ms | **+4.3%** | **6.2 MB（不变）** | 401.7 KB |
| `--arc-guard`（完整） | 473 ms | +13.4% | **282.5 MB（45×）** | 403.2 KB |

45 倍随分配量线性增长，长跑服务必然 OOM ⇒ 完整守卫只能是测试档（第 2 批的
`arcguard_*` 正是它的正确用法）。

**可出厂的子集是 `emit_arc_underflow_check` 单独启用**：它只在 release 路径上对
已经载入的 `rc_old` 多一次比较+分支，不需要隔离区，因此内存零增长、体积 +2 KB、
CPU +4.3%——而这是刻意饱和的微基准，真实程序更低，且仍在
`docs/PERFORMANCE.md` 写明的 "ARC overhead < 5%" 目标之内。

**剩下的是一个行为取舍，不是预算**：`irgen.c:823-827` 的注释说明了它今天为何 opt-in
——over-release 目前"只泄漏（计数永不归零，于是什么都不释放），带着它的程序照样
运行"，默认开陷阱会把泄漏变成崩溃。所以检测到之后要做什么有三种选择：
abort（当前守卫行为）／只向 stderr 报告一次并继续／不报（现状）。这条需要拍板。

### 3.4 实测：不是预算问题，是"能不能确定指针是托管字符串"

`expr_has_reliable_string_bounds`（`irgen_expr.c:55-64`，6 处调用）只对字面量与
非 opaque 局部返回真。查下来它**不是成本开关而是能力缺口**：

托管字符串本来就在 `str-8` 的低 32 位缓存了字节长度（`zan_abi.h:48-67`，注释明说
就是为了让 `.Length` 和每次边界检查 O(1)），所以只要确知是托管字符串，边界检查
几乎免费。问题在于 opaque 字符串**可能根本不是托管字符串**——extern 返回的裸
`char*` 前面没有头（`zan_abi.h:70-73` 明确写了这个区分），而去读 `str-8` 探测标记
本身就可能越界：字符串位于页首时那是未映射页。**探测动作自己就是它要防的那个 bug。**

所以 3.4 的真实选项是 ABI/表示层取舍，与性能阈值无关：
① 规定跨入 Zan 的 `string` 必须是托管字符串（在边界包装 extern 返回值，代价是每次
extern 返回一次拷贝）；② 维持现状，opaque 字符串无边界检查；③ 标记探测，接受
未映射页风险（不可取）。**这条需要拍板，且选 ① 会改变 extern 字符串的 ABI 契约。**

null 解引用那半同理：普通 `obj.f` 直接 fault，加通用守卫是每次字段访问一次
比较+分支（全语言最热路径）。这半确实是预算问题，但它需要先在 irgen 里做原型
才能量——不像 3.3 有现成开关可切，无法只靠测量得出。

## 第 4 批 · 标准库结构债（GUI 相关需协调窗口）

| # | 内容 | 验收 |
|---|---|---|
| 4.1 | **A57 遗留** ARC 引用环：`Control.OnChildEvent` 虚钩子取代"子控件事件上挂捕获 this 的闭包"✅（钩子与三组改造 2026-09-14 完成，见遗留专项「引用环」条）；**全库同模式扫描 ✅**（2026-09-14）：`Add(()` 闭包挂事件全树（stdlib + ide_zan + templates）**0 处活代码**（唯一命中 DataTableModel.zan:2256 文档注释）；IDE 组件的 `.Click.Add(...)` 全为静态方法组（无捕获不成环）——A57 环模式已退役 | `leakcheck_checkbox_group` 转绿 ✅；扫描结果登记 ✅ |
| 4.2 | 闭包瘦身第二批：`Automation`(61) / `Management`(68) / `Windows`(71) 去 Threading+Diagnostics 税 | ✅ **2026-09-14 完成**：实测 10 / 10 / 4（验收=各降一档，达成）。Window.zan 5 处 + Cpu.zan 2 处（Win/Linux 双支）Thread.Sleep 换 kernel32 `Sleep`/crt `usleep` 文件内 extern，去掉 `using System.Threading`——Threading.zan 一进闭包其 zan_thread_* extern 声明即钉死 rt_sync（ZAN_TRACE_SYNC 实证 Automation 2 旗标→0）。Windows 留 TrayIcon 的 Thread.Start（消息泵真线程，税正当）；Diagnostics 半边只余 Stopwatch ~2 文件且零对象拖带，保留。受影响 6 conformance ×4 孪生 24/24 绿 + standard |

---

# 遗留专项（未修、需协调或设计决定）

* **conformance_gui_zform_html 回归：容器 data-label 经 SetProp 直通漏进 Class（2026-09-14 凌晨并发会话定位，待设计决定）**：GenForm 的建模键直通回退（b572bbcd「EmitSetProp 分块发射/建模键直通」）对每个字段发射 `SetProp("label", <data-label 原文>)`；控件无 "label" PropSpec 时落 Control.SetProp 基类兜底——「非空值当样式类」，容器（Panel/Card 等无 label/title 专键者）的 data-label="body region" 变成 `Class = "body region"`，zform_control 用例断言 `Body.GetProp("class") == ""` 打红（got 'body region'）。旧缓存 ZanGen（b572bbcd 前编译）不发射该行故金样一直绿；zanc 重建使缓存键变化、新 ZanGen 由 04:12 后工作树 stdlib 重编后回归显形。修复方向三选一（需与设计器车道协调）：① 基类兜底白名单化——设计器建模键（label/placeholder/options/…）不进样式类兜底；② Panel/容器发布 "label"→title 绑定（会改布局语义：title 进顶边距）；③ GenForm 对无 label 槽的 kind 不发射。另见同窗 chart_calendar/barminheight/httpsource 三例红（并发会话 Chart* 未提交改动与其车道，未归因）。

* **ChoiceGroup 设计稿通道半初始化实例（2026-09-13 html_gallery 发现；2026-09-14 修讫）**：ChoiceGroup 是 RadioGroup/CheckboxGroup 的基类、自身无零参构造，设计稿写 `data-kind="ChoiceGroup"` 时 GenForm 发射 `new ChoiceGroup()` 得到 labels/children 为 null 的半初始化实例，首帧空引用（OptionsText 计数/children 走查）。修法实测：账面原两案均不可行——① abstract 类**并不**挡 new（探针证实，MOD_ABSTRACT 只在解析/反射登记，无实例化守卫）；② 目录剔除会把 zform.controls.txt 与 tests/run_zform_schema.cmake 的「Props() 类 ↔ 清单」互锁打红（清单是生成物、被测试锁死）。落地：GenForm 增 `NonConstructibleKinds()`（现仅 ChoiceGroup），ValidateFields 对名单内 kind 在校验期直接报错 "abstract base and cannot be instantiated; use a concrete subclass"，具体子类不受影响。验证：负控（ChoiceGroup 卡）报该诊断、正控（RadioGroup 卡 + partial OnLoad）出窗运行；designer_html/gui_zform_dynamic/gui_formbuild/gui_html_dynamic/gui_html_runtime/gui_compref_designer/policy_zform_schema 七档全绿；html_gallery EXCLUDED 维持（画廊不演示基类卡）。
* **templates_build 门 TLS 模板链接红（2026-09-13 收尾批发现，非模板源问题，链接车道待归因）**：门里拉 TLS 的模板（gui-wechat/gui-webview 等）报 `ld.exe: cannot find -lssl/-lcrypto`。已证与模板源无关：全部 33 个模板源单独编译全绿（gui 模板按门配方：设计稿 html 首参 + 全 src + `--subsystem windows`，rc=0）；最小 TlsStream 探针单加 `--driver-dir build` 也链得过——触发条件是门实参的组合（--driver-dir + --subsystem windows + 全量源），单独复刻任一参数不复现。另证本机 `-lssl` 常规解析通道不在 ld 内建 SEARCH_DIR（mingw64tdm 无 libssl），无 --driver-dir 时从哪个 -L 命中未查清即被叫停（ZAN_VERBOSE_LINK 未打出 [link] 行，疑 GUI 分支走另一条 link 路径，main.c 5384/5466/5513 处）。诊断工具就位：`ZAN_VERBOSE_LINK=1` 回显链接行。
* **WSL soak：mallocng 高压下概率崩溃（09-09 WSL 稳定性测试定位，待根因）**：server-game 模板 musl 静态链（`--target linux-musl`，cross 链路带 zanrt_mem.o + --wrap=malloc/calloc/realloc，小对象走 zan 槽位分配器、>2048B 落 musl mallocng）在 soak 驱动 ~6700 req/s 混合压（HTTP /api/auth/login + /register 表单 + TCP 网关 register/login 6 worker）约 11 分钟、累计 ~22 万请求后 SIGSEGV：faulting ip 落 musl `__malloc_allzerop`（mallocng 元数据一致性检查内），addr=0x85a414e5d（野值），无 Zan 侧栈帧（FP 链断），日志无前置异常。当时 RSS≈74MB、CPU 30-57%。归因：复跑 24 轮同形状压测（每轮 360 注册/登录×6 并发、1080 TCP op）+ 2400 串行化 admin 登录 + 1600 短连接 + 200 空闲连接悬挂，全部稳定（RSS 爬升到 82-86MB 后平台化，TokenBox 10 天 TTL 不回收是预期），未能复现——首轮崩溃发生时段恰是压测速率最高（6700/s，是复跑的 ~5 倍）+ register 落库风暴，嫌疑集中在 mallocng 多线程竞争（单线程 M:1 调度器但 runtime 侧 CreateThread/blocking-worker 仍并发 malloc）或某个高频路径的堆破坏只在特定节奏触发。修复方向：① musl mallocng 崩溃面用 musl 自己的 `malloc_usable_size`/调试配置先抓现行（`MALLOC_ARENA` 不适用，考虑换 `--target linux-x64` glibc 版对照复跑）；② zanrt_mem.o 把 >2048B 的中块也纳入槽位管理（现 4096 的 byte[]/大 frame 全落 mallocng）；③ zan 内存审计工具（arc_guard/quarantine）在 musl 静态链下开 `-DZAN_DEBUG` 复跑抓 first-fault。注：HTTP 安全扫描发现模板层 MEDIUM（/api/auth/login 无速率限制/锁定，12 连败无退避）与 LOW（缺 X-Frame-Options/CSP/X-Content-Type-Options/HSTS 四个防御头，Server banner 泄露版本）——模板/框架层决策，另行处理；TCP 网关对裸连接无帧长上限（9KB op 名照收）与无握手超时（半开连接 8s 不超时），属网关加固项。
* **leakcheck_checkbox_group 引用环**（A57 顺带定位，2026-09-14 已修）：`MakeItem`/`ReconcileButtons`/`AddOption` 每个选项一个捕获组的闭包，组→子→Change 表→闭包→组成 ARC 不可回收环（15 选项 15 泄漏，checkbox_group 泄 1109 对象、radio_group 泄 1576）。修法即当初预想的 parent 反向通知：`Control` 加虚钩子 `OnChildEvent(Control child, string evt)`（默认空）；用户切换入口改为 `Checkbox.Toggle()` / `Radio.Select()` / `RadioButton.Select()`——先翻模型再 `Change.Raise()`，随后经 weak `parent` 调 `OnChildEvent`；三个组全部改 override `OnChildEvent`（RadioGroup：PushValue+组 Change；CheckboxGroup：组 Change；RadioButtonGroup：组 Change + 继续向自己的 parent 冒泡），`RadioButtonGroup.AddOption` 因子项不走 `Control.Add` 需手工 `b.parent = this`（weak 字段）。`Radio.Select()` 带等值判重（model 已 ==optionValue 直接返回），radio_group 用例钉死该语义。事件表 `List<Action>` 不再增条目。验证：`ctest -R "radio_group|checkbox_group"` 8/8（含两个 leakcheck 孪生全绿）、`ctest -L gui` 4/4。
* **A64b leakcheck 收口（2026-09-12，已修）**：http_forwarder_tunnel（A302）、keepalive（用例自留 listener 未 Stop）、ws/mqtt/sse 四用例（Worker.Stop 不关 socket，补 Close）、http_bytes/client_redirect 与 db_error_throw、reflect_members、sqlserver_tds、tdengine_rest 全部归因修复（编译器侧见下三条），leakcheck 孪生全绿；radio/checkbox_group 属 GUI 车道（见上「引用环」条）。
* **check-leaks 泄漏站点按类形状混叠、报告 file:line:col 张冠李戴（2026-09-12 已修）[P3/compiler]**：`__zan_site_names` 每次分配被覆写致报告指到同类最后分配者（A64b 排查被引偏一天）；修=check-leaks 构建下去重键扩成 (形状, 文件, 行)，无 -g 退化回旧行为；新 conformance `arc_leak_site_label` 四孪生绿；顺带修 leakcheck foreach 两处 `_extra_args` 缺配与 up-to-date 判定不看编译参数（.args sidecar）。
* **集合查找内建不释放 owned 实参临时（2026-09-12 已修）[compiler]**：List.Contains/IndexOf、Dict.Remove/ContainsKey/TryGetValue、`d[key]` 读五处内建对内联调用实参漏释放；修=五处补 emit_release_owned_call_temp；新 conformance `arc_lookup_owned_arg` 四孪生绿，http_bytes/client_redirect 随之转绿。
* **非泛型调用路径不把 owned 实参注册进 EH unwind、被调方抛出即漏（2026-09-12 已修）[compiler]**：八条调用路径统一 emit_call_arg_eh_push/pop（pop 计数必须等于 push，条件 push 无条件 pop 会偷别的帧的注册）；db_error_throw 转 0 对象；新 conformance `arc_throw_owned_arg` 四孪生绿。
* **object 槽漏收 owned RHS（2026-09-12 已修）[compiler]**：is_arc_managed_type(TYPE_OBJECT) 恒 0 致 `object o = Call()` 的 +1 无人接管；修=obj_slot_owns_owned_rhs + emit_release_obj_value 按头字分派释放（串标签 vs 类描述符）；leakcheck_reflect_members 0 对象；新 conformance `reflect_object_payload` 四孪生绿。
* **object 槽持串/数组时 GetType()/is 把头字当描述符解引用崩溃（2026-09-12 已修）[compiler]**：默认构建两处补形状判定（串标签/数组魔数/rank<4096 拒解引用），与 check-leaks 构建答案逐字一致；探针 rp 族 rc=139→0。**遗留（另记）**：GetType() 对串/数组仍答静态类型记录，C# 语义应答运行时类型，属独立设计变更。
* **sqlserver_tds 4 对象泄漏 = fake server 停摆的 Serve 帧（2026-09-12 已修，用例侧）**：对端 close 不唤醒挂起 recv，Serve 帧永久可达；照 A64b net 系定式关服务端自己那端并自旋等退出；leakcheck 0 对象、四孪生绿。
* **A76 crasher（应用层/悬垂）**：① 已修（2026-09-14）：`TlsStream.Close` 先翻 open 再做 shutdown/free，`SSL_free` 后 rbio/wbio 置空绝悬垂（SSL_set_bio 已把 BIO 所有权移给 SSL，Zan 侧字段是借用指针）；真正危机是并发——Close 全同步无 await，唯一竞争窗是别的协程驻车在 FlushOutAsync/PumpInAsync 的套接字 await 上，恢复后继续碰已随 SSL_free 释放的 BIO。两泵在每个 await 恢复点后重查 open，失败返回让各调用方自然收尾；TLS 切片 12/12 绿。② sqlserver_tds `Program_Live$resume`（pool.Close 场景）协程 resume 路径 null 实例字段，非确定性复现，仍挂。
* **Windows IOCP blocking-worker 唤醒包偶发丢失（09-09 全链路稳定性测试定位）→ 2026-09-14 降级为观察项，无法复现**：原始记录见 git 历史本行（`conformance_http_forwarder_keepalive`/`stream` 概率 1/6~1/2 静默截断、blocking connect 结果不送达、trace 停在 `poll removed=1`）。**复现尝试（2026-09-14）**：105 轮压测零复现——keepalive×20、stream+tunnel 三连×10 轮（30 全绿）、`ZAN_CO_WORKERS=1` 三连×5 轮（15 全绿）、`ZAN_IO_SYNCFAST=1` 三连×5 轮（15 全绿），另有 6 轮先导全绿；其后 A298⑴ 的 pre-park drain（11e76bd7，`zan_io_poll` 驻车前 check-and-drain `g_blocking_wake_lost`，与 `co_wait_io` 同款守卫）已封掉「Post 失败后作业滞留 `g_blocking_done` 而两路驻车路径都不再醒来」这一可推演的唯一丢包交付窗口。坏例时间线（09-09，早于 11e76bd7）与本窗口吻合，定性=**很可能已被 A298⑴ 根修**；若再现，先抓 `ZAN_IO_TRACE` 全程（重点 `poll GQCS=0` 后是否还有 `poll_op`/`poll removed` 行）+ 确认 zanc 含 11e76bd7 之后构建，再重开本条。受影响测试的重跑容忍随之撤销（不再需要）。
* **A67 字节×码点语义冲突（语言级，待定夺）**：string 按字节（索引/NUL 守卫/Length；FbReader/TDS codec 按字节索引），char 按码点（拼接/打印 UTF-8 编码），`s + s[i]` 对非 ASCII 必然膨胀；两侧皆有意设计，修复任何一侧破坏面都大。语言级出路（Rune/ByteAt API、解码式索引等）留待专项。
* **A56 未做（下一批）** ✅ **2026-09-14 完成（见 A58 4.2）**：疑点核实成立——Automation/Management 两名空间确实只为 Thread.Sleep 付全价，去税后 Automation 闭包 11→10 文件且 rt_sync 旗标归零、Management 10、Windows 4（TrayIcon 真线程保留）。"待核"结论：税源是 extern 声明发射即触发 uses_sync_runtime 旗标，与是否实际调用无关。
* **A71 后续路线（按需加载未完部分）**：① globaldce 钉死源逐个核（A75 已退役三表，重钉面需复量）；② auto-embed 泛化——`stdlib/<Ns>/data/` 自动烤进镜像的通用机制（skins 手工、icons 已接）。
* **体积优化线剩余候选（边际收益小）**：desc 记录瘦身、tynames 列表共享；PIC 与 ARC 冗余对两条杠杆已实测证伪。A74 归因的「空窗 ~72% .text 可去死」随 A75 去钉后需复量。
* **A44(genmeta) 备注（已部分过时）**：`build\ZanIDE.exe` 12.23MB vs dist 快照 8.2MB 的增长未追查（2026-08-24 记录；其后 A71-A75 已大幅优化发布体积，数字需重测）。
* **determinism/leakcheck_checkbox_group、determinism_bytebuffer_bounds 的 `--emit-ir` 宿主崩溃（2026-09-12 复核：已不复现，本项关闭）**：两用例 `--emit-ir` 三连 rc=0；疑为 2026-09-08 两次「内部崩溃改干净诊断」提交顺带修掉；若复发先查 --emit-ir 下 LLVM 校验失败路径。

* **A80 zanc 退出段错误（2026-08-31 已修）**：CRT 退出表的 LLVM 静态析构 thunk 对已释放 LLVMContext 二次拆除；修=irgen_destroy 跳过 LLVMContextDispose、保留 ctx 至进程退出 + main 尾 LLVMShutdown()；另 zanc 链接 /STACK:32MB 修深 else-if 链栈溢出（独立家族）。

* **A81 字节通道 redirect 接入 + 跨跳共享 total deadline（2026-08-31 已完成，4f9ab3f9 收尾批）**：HttpClient 四字节通道跟随 3xx（跨 origin 清敏感头、307/308 非 GET 拒重放）、totalDeadlineUs 跨跳钳制、HeadHeader 共享首读；新 conformance http_bytes_redirect（9 场景）；文本通道旧路径去重到同一组 helper。

* **A82 wasm32 后续路线（2026-09-01 盘点，本轮已修链接全断，以下为剩余）**：当前 wasm32（WASI）可编译运行的范围：CLI/计算/字符串/集合 + 文件 IO（探针在 Node WASI 下全链路验证）。剩余缺口按优先级：
  1. **try/catch（wasm-EH 后端）（已完成 2026-09-02）**：wasm32 try/catch/finally/throw 全量落地（invoke 发射 + 自带 __cxa_throw 定义 + zanrt_ehtag.o），Node WASI 运行与 native 一致；conformance_try_catch_wasm32 入册。
  2. **GUI wasm 后端（大工程）**：stdlib/Gui 三层原生面——`Backend/Native.zan`（55 个 DllImport：user32/gdi32/kernel32/zan_gui）、Win32Shell、`zan_gui` 原生 driver（win/linux/macos 六份，无 wasm 版）——在 WASI 上全数无解。可行路线是新的 **canvas/Web host driver**：Zan GUI 渲染逻辑留在 Zan/C，事件与像素经 WASI host import 桥到浏览器 canvas（类似 emscripten 的 SDL port 或 winit-web）；自绘与布局不动，Backend/Native.zan 按 `#if WASM32` 换导入表。注意 Gui 全量编译本身（--auto-stdlib 拉 330 文件）在 wasm 三元组上没有平台障碍（无 GUI 原生引用即可链接，探针已证）。
  3. **socket-async**：wasm 上无 epoll/kqueue/IOCP；WASI preview 2 的 sockets 提案未成熟。短期维持 main.c 的 wasm_obj_refs_any 拒绝（报错清晰）。
  4. **wasm64**：无任何代码（架构枚举/triple/sysroot 皆无）。IR 天然 i64 所以适配层反而可跳过，但 wasi-libc wasm64 sysroot 与引擎支持太新，排在 wasm32 EH 之后。
  5. **IDE 支持面**：PublishTargetIds（ZanIDE.Workspace.zan:1316）现仅列桌面目标；加 "wasm32" 条目 + Publish 走 zanc --target wasm32（编译链接路径本轮已通）即可让 IDE 发布 wasm。断点调试：zan-dap 走自家 intellisense/调试引擎（无 wasm 感知）；wasm 调试现实路径是 Chrome DevTools 的 DWARF 调试（zanc --publish 已可带调试段）或 source map 级映射，属新专项。

* **A84 Android CLI 目标落地（2026-09-03 已完成）**：`--target android-x64/arm64` 全链路（bionic triple 解析、NDK sysroot 子集静态链接、rt 不走 --wrap=malloc、bionic shm shim），E2E 模拟器全过；docs/platform-targets.md 12 项。

* **A86 一键发布 Android APK（2026-09-03 已完成）**：`--emit-apk` 一条命令编译→打包 SDLActivity 壳→签名（自带 zip 写入器、二进制 AXML 串池补丁替代 aapt2、apksigner+自动 JRE/keystore），模拟器装机像素级验证；IDE Publish 增 Android 目标。

* **A85 Android GUI 窗口跑通（2026-09-03 已完成）**：[DllImport("zan_gui")] 程序 android-x64 以共享库+SDLActivity dlopen 出窗口，模拟器像素级三色命中；android 驱动（libzan_gui.so + libSDL3.so 两 ABI）入库。



  ① **layer 键 + 窗口属性全链路（cf141d1a / 10339428，已完成）**：SceneElement layer 键、winTitle/winResizable/winChrome 贯通 SceneDoc→SceneDesigner→GenScene；FormDesigner 的 winTool 同病修复（GenForm 补读 + SetToolWindow）。
  ② **on* 事件 + 声明式动作（d869db43，已完成）**：设计场景按钮连线零代码动作（ShowLayer/HideLayer/OpenScene…，';' 串接按序执行），SceneView 帧内点击分发，App.RequestClose 通道。
  ③ **IGameScene 适配器（35c02818，已完成）**：设计场景直插 Game.Foundation.SceneStack（先修编译器：嵌套 contextual partial 识别）；tests/gui/scene_adapter 用例全绿。

* **A87 Android 字体 + gallery arm64 发布 + 自由画布整页拟合（2026-09-03 已完成）**：android 驱动编入 FreeType（裁剪 ftmodule.h 坑）、fonts.xml 解析字体发现、自由画布逻辑字段化 + dpiScale 重推拟合、gallery arm64 APK 发布（android DllImport stub/Socket errno __errno/--apk-package 旗标三处编译器修）。

* **A88 Android 触屏/返回键/密度 + 手机抽屉导航 + INTERNET 权限/TLS 驱动（2026-09-03/04 已完成，e4e639d9 + dbe1b856）**：触屏手势管线（点按合成/拖动 1:1 滚轮/松手惯性/滚轮事件合并）、AC_BACK 语义、原生密度下限、gallery 手机态抽屉导航、APK 壳 INTERNET 权限（无权限 socket EPERM）、TLS 驱动四件套（Termux openssl SONAME 等长改写）+ 环回 TLS 全链路实证、IME 会话跟随文本焦点、安卓标题栏精简、73 组件自适应扫掠修复；A87 遗留「HTTPS 运行期 fail」就此闭账。**遗留另案**：void 线程入口带 try/catch 悬挂（疑 EH/coroutine 编译缺陷）；ScrollView 滚轮仲裁吞事件（根因已定位，需两段式认领 + conformance）；arm64 实机与真实出网验证；APK 内嵌字体裁剪。
* **A87 Python 捆绑驱动退役 + Lua 加密内嵌（2026-09-04 已完成）**：CPython 载荷不再随程序携带（系统探测降级）；Lua.LoadBytes/ExecBytes + tools/pack_script.zan AES-256-GCM 加密打包（HKDF 派生、chunk 名 AAD），strings 扫描 0 命中。

* **待修复：ScrollColumn 首次进入视口吞掉第一下滚轮（Legend 实测）**：鼠标从非滚动区域进入无交互标签的日志视口，移动未触发渲染；首个 kind=13 帧 `App.CaptureWheel` 的 `wheelOwnerOrder=0`，而当前 `wheelClaimSeq` 增为 1，因此没有消费者。第二下正常。源头在 `stdlib/Gui/App.zan` 的前帧滚轮认领与无 hover 命中移动重绘之间的关系；不能简单放行所有 owner=0 的声明者，否则嵌套滚动可能双消费。复现：`_scratch/legend/presentation-scroll.ui` 去掉第二次 scroll，日志内容必须高于视口；探针 `_scratch/legend/main-scroll-trace.zan`、记录 `_scratch/legend/scroll-trace.txt`。布局/血条/透明素材已修，未在游戏代码中增加假滚动或额外吞发事件绕过此问题。后续须修标准库并覆盖首次滚动、兄弟容器切换、嵌套滚动、连续滚动的真实输入测试。

* **A89 初始化器挂在非 new 表达式（工厂延续 `Maker.New() { M = v }`）时成员赋值跳过 no-setter/成员存在性检查且静默丢弃（2026-09-13 已修，P1/checker）**：根因=checker AST_NEW_EXPR 的 call_init 分支被 resolve_type(NULL) 冲掉类型致三检查全跳过；修=factory_init 不再 resolve type 节点、未知成员补报、集合初始化器糖豁免不受影响；新档 tests/diag/factory_initializer_{member,readonly}.zan；教训入 zan-compiler-internals skill。

* **待修复：Windows 文本超采样的测量/绘制宽度不一致（Legend 实机）**：当前 `src/runtime/gui_runtime_text.c::win_run_tile` 用 size 测量逻辑宽，再以 size*3 的 GDI 字体绘制至逻辑宽*3 的 DIB。Hinting 非线性导致截断：探针 `_scratch/legend/detail-pass/font-probe.py` 中技能消耗句 size=19 测宽360、size=57测宽1099，大于 (360+2)*3=1086，真实截图末尾数字被吃掉。修复须统一布局测量与超采样绘制的 advance/extent，覆盖长中英文数字串、普通/加粗、多 DPI 和实际 tile 像素边界，不能在游戏里补空格/假 padding/缩字号掩盖。当前 runtime 文件有其他会话未完成改动，本轮保留并登记，未改写该渲染算法。
* **待修复（编译器加固，非阻塞）：zanc `--embed <dir>=skins` 一律跳过 stdlib 皮肤基线自动内嵌**：src/compiler/main.c 的 Gui 皮肤自动内嵌块只要发现任一 `--embed` 目标名为 "skins" 就整体跳过，但项目 skins 目录往往只含自家皮肤包（如 templates/game/wuwei/skins/ 起初只有 wuwei/skin.css、无 base.css），跳过后发布产物没有 skins/base.css 基线层——Style.BaseSheet 为空，`flex { display: flex }` 不生效，所有 Flex 容器退化成 dock 排版、子控件全部叠在同一矩形（无为修仙传百艺页签全叠点不中即此因，探针 _scratch/flexprobe.zan：无 skins 内嵌则坏、内嵌含 base.css 的目录则好，已复现闭环）。本轮由模板自带 base.css（与 stdlib/Gui/skins/base.css 逐字节一致）解决，游戏 ALL PASS；建议后续把跳过条件收紧为"staged 目录含 base.css 才算完整替身"，修复草稿已写好但因 main.c 有其他会话未完成的 apk 改动（strtok_r/apk.h 签名）无法编译验证，本轮未保留该改动。

* **A90 GenDb typed-ORM 的 stdlib 内部类型被可达性裁剪丢弃（2026-09-13 闭账：已被 main.c demand-driven pull-in 二轮修复）**：codegen 后先以生成器输出为种子把生成代码引用的 stdlib 符号拉进 live 集再裁剪；复验：最小 [Table] 探针、server-mvc 全 71 源（175 文件）、server-legend 全 84 源（26 实体 170+ ORM 调用点）--auto-stdlib 全部零复现。

* **A257 Worker TCP 连接软空引用（未根治，规则 10 在案；非阻塞）**（2026-09-07）：`_scratch/server-game-run` 跑 server-game 模板 e2e（80 断言，全 PASS）时，每次完整运行在 run.log 恰好出现一条 `Net\Worker.zan:2607:16: runtime error: null reference where an object is required (member access)`——2607:16 即 `Connection.GetId(): return this.id;`，接收者为空/已释放。时间窗恒定在启动横幅之后、首个游戏登录（bob）之前，即第一个 TCP 客户端（mallory：connect → hello 推送 → register → 约 2s 闲置 → 客户端 drop）的生命周期内；每次运行恰一条、不随断言数变化。软错误只杀死当前任务：mallory 无会话、无功能断言受影响、服务端继续运行、后续连接（bob/alice/carl/dave + GM 页）全部正常。隔离探针 `_scratch/worker-onclose-probe/`（Worker("tcp") + OnConnect 推送 + onClose 里 GetId()）四形态均零复现：纯 connect+drop 30 轮、hello+register+闲置+drop 20 轮、以及 game-server 侧的 (B) register+drop、(C) login+drop、(D) 游戏连接+10 HTTP POST 混合、(E) 逐句复刻 e2e mallory→bob 前置序列——错误计数都停在基线 1，无新增。怀疑方向：完整服务端里 tick 协程（Gateway.SweepIdle 快照后逐个 GetId）与连接 EOF 清理（HandleTcp finally → onClose(conn)）之间的 Connection 释放竞态（ARC 下悬垂接收者），或 onClose 回调链上 conn 的生命周期缺口；最小探针缺 HTTP worker + World tick + 会话表的并发形状故不复现。影响评估：单发、软失败、不破坏功能与计数；真正会受伤的场景是"被杀任务恰持有会话"（会话滞留 World 直到客户端主动断开）。根治路径：给 Connection 的跨协程持有/回调补 ARC 生命周期契约（onClose 参数、Sweep 快照元素），或在 Worker 内对 GetId 接收者做存活断言把软错变成可定位的硬日志。复现环境：`_scratch/server-game-run`（fresh DB → server-game.exe → python e2e_v3.py → grep -c "runtime error" run.log == 1，连续 4+ 轮）。　**2026-09-14 复验：不再复现，按"已治愈-根因未单独定位"闭账**——关键时间线：载有 7 条错误的 run_e2e.log（09-08 03:59）产自**更早构建**；复现目录现存 server-game.exe（09-08 18:10）已含当日 A260 审计修复批（七笔提交），A298/A64b（09-12/14）又相继加固了同族 close/wake 路径。本轮 6 轮零错误：①完整 e2e 跑到 83 号检查（覆盖文档载明的全部错误窗口：落地页/forgot/mallory 生命周期/lockout/bob hello+register+login）0 条；②聚焦 mallory 生命周期驱动 2 轮 0 条；③加并发 HTTP forgot/register 压力变体 1 轮 0 条；④服务器启动自检 0 条（旧日志同位置另有 ServerMetrics 越界 ×4，本轮亦无）。旁证：现行模板世代 server-legend 的 e2e_legend 80 检查全绿（5ecb5d6d），无 runtime error 报告。e2e 全程未跑通的部分（83 号后战斗/GM/kick 检查）因 e2e_v3 冻结于 09-07 与 exe 种子数据漂移（稻草人→鸡），非本缺陷窗口；驱动保留在 `_scratch/server-game-run/repro_a257.py`（聚焦 mallory 形状）与 `e2e_a257.py`（CONT 模式全量），若复发可直接复用。

* **A258 __zan_dict_remove 增量索引修复破坏探测不变量、后续 Remove 死循环（2026-09-13 闭账：已被 b8bb4c35 重写根除）**（原登记 2026-09-07，server-licensing 运行验证时发现）：ada73452 的 O(1) Remove 在共享前缀键序（同簇探测链）下 step1 探测无空槽出口，第二次 Remove 死循环（CPU 100%，阻塞模板 rbac 运行期验证）。**b8bb4c35（2026-09-09「Dict.Remove 保序」批）把增量索引修复整个废弃**——现行设计：删槽 + 索引整表失效（indexed_count=0），下次 find 走 icnt!=cnt 陈旧检查全量重建（O(n) 与调用方的保序数据搬移同级），C# 可观察枚举序同时保住。**复验（2026-09-13）**：原 probe6 形状（32 个 Admin.* 屏键 × 4 轮 TryGetValue→Remove→Add）timeout 10 下 rc=0，count/sum/Keys 插入序全对；新档 `tests/conformance/dict_remove_shared_prefix.zan` 把该形状钉进回归。licensing 模板如需 rbac 复跑，用当前 zanc 重建即可。
* **A314 `dict.Keys[i]`/`dict.Values[i]` 链式下标整表达式折叠成常量 0（2026-09-13 复验 A258 时发现并当轮修复）[编译器/irgen]**：`string s = d.Keys[0]` 编译干净、`s` 得空串，拼接语境打印 "0"——IR 实锤 `store i64 0` 直接进 string 槽。根因：`emit_expr_index` 的 `obj.field[i]` 成员分支只认**声明的字段**（`member_access_field_type`），Dict.Keys/Values 是合成视图不是字段 → arr_ptr NULL → 落进末尾「静默折叠常量 0」兜底（general 分支带 `infer_expr_type` 兜底但只服务非成员对象）；`d.Keys.Count` 一直正常（Count 走另一特判），极具迷惑性。**修**：成员分支在 field 解析为 NULL 时回落 `infer_expr_type`（其 member 处理本就覆盖 Dict.Keys/Values→List 合成视图），owned 视图临时由 `finish_index_of_temp` 既有契约释放/元素 retain。新档 `tests/conformance/dict_view_chain.zan`（视图链/方法返回值链/字段链对照）四孪生绿。

* **A259 JSON 解析性能线封存（实测到顶，2026-09-08 沉淀）**：会话弧线 22→12→11ms/5.4MB（~490MB/s），参照 yyjson 同机 4ms。落地四笔：48ef3f7d（平铺 tape 重写）、d9e06ee4（Load64 内建 + SWAR 引号/键扫描 + 修容器值 valueSlot 指向子树尾槽）、63c82352（修空对象 firstPair 串键——`{"a":{},"b":1}` 上 Get(a,"b") 误命中 b）、d3a5c342（AsI64/AsF64 位型内建 + JsonSlot 24B→16B，double 位型折进 b 槽）。**系统性消融负结果留档**（全部独立编译实测，未来勿重复试错）：①边界检查在热循环已被 LLVM 消除（checked 字节和 34ms ≈ 纯循环地板 35ms/108MB，IR 三重检查在 -O2 后不存活）；②List.Add 在 Reserve 后非瓶颈（裸数组 tape ±0）；③递归改单循环状态机：骨架 7→5ms 但全功能版被帧簿记吃回 ±0（探针 `_scratch/bench/smskel.zan`/`_scratch/bench/arr/`）；④计数器静态字段→参数线程化 ±0；⑤强制内联 SWAR token 方法（alwaysinline ≤40bb）±0；⑥分派按频重排 ±0；⑦-O0/-Os/-O2/-O3 四档无差别；⑧walk 7ms 与 tape 密度无关，被 StrAt 物化分配 + 键 memcmp 支配（哈希字节短路是唯一未试的 walk 杠杆）。**剩余 ~2.7x 差距定性**：每 token 常数项累积（50 vs 19 周期），无单一可修瓶颈；重开条件=愿意做 yyjson 级完全重造（16B 槽 long[] arena 直写原型已到 10.8ms，见 `_scratch/bench/tape16/`，缺口是扩容设计：估少即 OOB，需 2x 倍增 + 管理数组拷贝原语）。**测试基建坑（本轮实锤）**：stdlib 改动后必须先 `cmake --build build` 刷新 stdlib.stamp 再跑用例，否则 ctest 复用陈旧产物（同一 35 例 5 秒假跑完）；且与并行会话共享 build/ 时并发构建会让用例成批假红（8 vs 2 失败数漂移）。假绿三连的教训：48ef3f7d 的 valueSlot 硬伤、selfhost_json_ignore、int_literal_range 均"测试绿"实为陈旧产物，HEAD-zanc/HEAD-stdlib 对照实验是排雷标准动作。
* **A260 全量审计缺陷修复第一/二批（2026-09-08 已完成，七笔提交）**：D1 除法 MIN/-1 挂死、D2 JSON 尾部孤反斜杠死循环、D3 null 字符串下标 SIGSEGV、D5 NaN!=NaN 违反 IEEE、D10 委托 += lambda 编译错、D11 内建标量构造静默 null、D6/D25 浮点最短往返格式化 + C# 特殊值拼写可回读、D19 Main(string[] args) 填充、D17 表达式语句、D20 string.Equals；各配 conformance 新档。

* **A260 全量审计缺陷修复第三批（2026-09-08 已完成，六个提交）**：D16 `>>>` 无符号右移、D18 单行多声明符、D14 未捕获异常具名、D7 --strict-runtime 软失败分级、D21 Encoding.UTF8、JsonDoc.ForEachChild + D22 GetPaths、D24 ToUpper 非 ASCII 损坏证伪无需修；D4/D8/D9/D12/D13/D23/D26/D27 按性质归类收口（设计决定/文档类）。

* **A261 Thread.Start 吃实例方法组/闭包：编译通过、调用即崩（2026-09-11 已修，与 A70 同一笔）**：zan_thread_start trampoline 按 ZAN_CLOSURE_TAG 判形态调用记录 fn 槽（捕获 lambda 与实例方法组一并成立）并按生命周期补 retain/release；conformance thread_start 四形态 + 500 线程 stress 零泄漏。

* **A269 实例方法组绑定的接收者临时量泄漏（2026-09-11 已修，查 A261 连带发现）**：`Action a = new Job(i).Run;` 每次泄漏一个接收者对象；方法组绑定时补接收者临时量的 owned 登记/释放；conformance+leakcheck 双绿。

* **A270 静态方法经实例调用（`d.StaticMethod(...)`）生成参数个数不符的 IR（2026-09-11 已修）**：irgen_call 的 local.Method 分支对静态方法误把接收者放进第 0 实参；修=静态解析时接收者不移位；conformance 绿。
* **四期1 · LSP 补全/导航现状基线（2026-09-09 实测完成）**：探针 scripts/lsp_baseline_probe.mjs 三档场景，定位仓库根索引发散、工具链 stdlib 不入索引、补全只靠手写 stdlib_classes 表等缺口（成为四期2 输入）。

* **四期2 · 第一批：LSP 换供数（2026-09-09 完成）**：索引跳过目录收敛（bin/obj/build/dist/node_modules/_scratch 等）+ 工具链 stdlib 按 exe 相对规则入共享项目索引。

* **五期 · Zan 类型元数据 → dap 变量 pretty print：探针定调（2026-09-09 完成）**：_scratch/pp1 定调 DAP 变量树需 irgen 发真实 DWARF 结构类型（实现在五期主体条）。

* **四期2 · 第二批：typed member completion（2026-09-09 完成）**：文档优先接收者解析 + 项目成员合并。

* **四期2 · 第三批：增量 didChange（change:2）+ 诊断 worker 线程（2026-09-09 完成）**。

* **四期2 · 第四批（收尾）：using 指令命名空间补全（2026-09-09 完成）**——四期1 已知缺口清零。

* **五期主体 · irgen 发真实 DWARF 结构类型 + DAP 变量展开（2026-09-09 验收全绿）**：irgen DI 段从 zan_type_t 递归出 DWARF（class/struct/数组/泛型实例；泛型参数按名字替换、List 元素槽是 8 字节擦除槽 data 必须 long*）；DAP 侧 refs 发放 + `-var-list-children --all-values` + 指针伪子节点；tests/dap 全过；教训入 zan-dap-debugging skill。

* **ZanIDE 启动段错误（2026-09-09 已闭账）**：存量构建中间态产物，当前树不可复现；IDE 崩溃五分钟结论法（zan_crash.log + -g 重出）入账。

* **standard 层存量挂账清零（2026-09-09，编译器/测试六连修）**：29 挂逐一归因（int[,] parser 回归、泛型 string 拼接、primitive_constants golden、A90 裁剪账、mac dylib 符号面、环境依赖），本仓可修的修掉，charts 在飞 4 例归属并行会话。

* **win-arm64/ohos 交叉 rt 对象重出收尾（2026-09-09 完成）**：rt_crash.h 架构门控等两处真缺口一并修。
* **组件库配置面全量审计·首批（2026-09-09 完成）**：两代理盘点 Props()/可配字段/工厂参数对照 Element-Plus/AntD/Naive UI，首批 9 组件缺口关闭（T1 真控件补 Props()）。
* **组件配置审计 T2a 深模型批（2026-09-09 完成）**：派生键三件套定式（文本是结构状态视图的键、Props() 绑表达式快照、SetProp 截到重建入口）。
* **平台缺口：Android 交叉链接不桩 win 系统库（已实测，待修编译器）**（2026-09-09）：`src/compiler/main.c` Android 交叉链接的桩循环里 `zan_win_system_lib(...)` 命中即 `continue`（跳过不桩），而 Linux/OHOS 交叉路径与原生非 Windows 路径同场景都是生成 stub——任何可达的 `[DllImport("kernel32")]` extern（如 stdlib Wide）留在 libmain.so 成 undefined symbol，NativeActivity dlopen 即 UnsatisfiedLinkError。本次治标：runtime shims 补 MultiByteToWideChar/WideCharToMultiByte（93ed2fc5）；治本应在 Android 路径对齐其余路径的桩策略。探针：`_scratch/anw`（出包链路）、dlopen 崩溃栈见 goldminer 首包。
* **IDE 不可用双缺陷定位与临时交付（2026-09-09 晚完成）**：build_ide --link-lib 缺 ole32（zan_audio WASAPI COM）等两处，定位并交付可用 IDE。

* **平台缺口：Android 上内嵌资产解析不通（已实测，待修 Assets/File 内嵌链路）**（2026-09-09）：`--publish --emit-apk` 打出的 APK，运行期 `Assets.Find("assets/bg.jpg")` 返回相对路径且 `File.Exists=false`（`_scratch/guiprobe` 探针 v2 实测，find=assets/bg.jpg exists=no）——纹理资产全部拿不到路径，BlitImage 走不到，模板矢量兜底成了真机实际画面。桌面/发布目录行走同一 API 正常。需要让 Android 运行期的 Assets.Find/File.Exists/读文件打通内嵌镜像（或解出到 files 目录）。探针：`_scratch/guiprobe/main.zan`（v2 带资产诊断段）。
* **两个表格调色板项 kind 断层修复（2026-09-09 晚完成）**：4eb8601d5 改 KeyForType 后 Table/DataTable 两调色板项 kind 断层，修复映射。
* **画布自绘类型 kind 断层收尾（2026-09-09 晚完成）**：KindForType 里 Chart/Ellipsis/Scrollbar/Ribbon/Layer 五项指向非 Control 类的断层清掉。
* **设计器交互面三缺陷修复（2026-09-09 深夜完成）**：撤销只退一步/重做落空（LoadJson 快照路径）等拖动/嵌套/编辑三链路缺陷逐个根修。
* **AI 排版混乱治理（2026-09-10 完成）**：运行时重叠探测器（ZAN_GUI_OVERLAP）+ 排版原语纪律入 gui-design skill。
* **截图还原布局方法论 + 交互体验基线 + DPI 三票定倍数（2026-09-10 完成）**：gui-design skill 新增 references/screenshot-restore.md（单屏截图→可验收布局流程、测量转写账本五列制、分析侧三票定倍数）。

* **SDL3 整体移除·第一步（2026-09-09 完成）**：zan_audio 原生音频运行时落地 + 音频消费面清零，探针 21/21 全绿。

* **全链路稳定性/安全性测试（2026-09-09 完成）**：ORM 变量谓词+注入面 19 项、TCP 稳健性、HTTP 畸形/限界/走私、WS 帧边界、网关协议模糊、AES-GCM 重放、SQL 注入黑盒、HTTP 管理面、DB 池等 70+ 项全绿（两笔提交）。

* **SDL3 整体移除·第二步（上）（2026-09-09 完成，4831c678 + da902bd4）**：Arpg 脱 SDL + stdlib/Game 零消费方层删除，Game conformance 13/13 金样全绿。

* **skills 三副本同步（2026-09-09 完成）**：发布包（tools/ai_pack/skills）与全局副本（~/.agents/skills）补齐本会话全部教训。

* **尺寸自检 lint + flex 自检漏洞修复（2026-09-09 完成）**：ZAN_GUI_LAYOUTLINT 机械闸门 + flex 自检漏洞修复，与重叠检测互补。
* **H5 路线第一步：wasm32 文件 IO/EH 全通（2026-09-10 已完成，0bdf3247）**：最小 H5 验证闭环 + 两个根因修复。

* **skill 与工具链版本错位教训（2026-09-10）**：安装版工具链可能落后主树数日——验证新闸门/新 API 前先核对目标工具链版本，勿以主树为准臆断安装版行为。

* **设计器调色板对齐组件全集（2026-09-10 完成）**：新增 ft 73-85 十三类，设计器与运行时 ControlFactory 对齐。

* **Tabs 标签条方向进设计器（2026-09-10 完成）**：FormField.tabOrient 建模→检查器→GenForm 投影全链路。

* **内置组件直通属性行第一批（2026-09-10 完成）**：Designer.BuiltinPropRows 通用直通机制（kind→"键|双语标签"表）。

* **图表家族进设计器（2026-09-10 完成）**：ChartHost 保留式控件 + 类型直通行，修复 GenForm.TypeOf 把 Chart 归一成 Panel 占位致发布窗体图表消失。

* **zanc 单输入"漏收同目录 sibling .zan"（2026-09-11 结案：误判非回归）**：现象与复核证据见 git 历史与备份。

* **游戏引擎三路盘点 + Game.Idle 库落地（2026-09-10 完成）**：stdlib/Game 家底/Arpg 数据侧/模板三路盘点，Game.Idle 库落地。

* **H5 路线第二步：GUI wasm 后端落地（2026-09-10 已完成）**：wasm32 delegate 形状根修（形状按 target_is_wasm32 条件化，避免撞 ZAN_CLOSURE_TAG——定式入 skill），浏览器 canvas 实机全链路闭环。

* **A262 NativeMemory.GetString 内建被 extern 借用规则误吞 → 每请求泄一条原始请求头（2026-09-10 已修）**：expr_yields_owned_rc_value 内建白名单补 GetString（教训：新增返回真 ARC 串的内建必须同步进白名单——已入 zan-compiler-internals skill）。

* **SDL3 终局移除收官（2026-09-10 完成，d7e2126e，-38429）**：stdlib/SDL3 全树 44 文件、ra2 模板与 tests-ra2 等关联面一并退役删除。

* **A263 图表引擎对照 ECharts 6.1 的全量能力缺口盘点（2026-09-10，登记未修；用户指令"照 6.1 最新版"后逐能力域 grep 复核）**：用户连报 16 个 demo"和官方演示不一致"后做的两轮盘点（demo 逐个判定 + 组件级全量对照）。**定性结论**：引擎自述仍是「ECharts 2.2.x 声明性子集」（Chart.zan:7），此前"全部缓存 6.1"落在**色板/部分缺省值**层面（smooth=false、title left=center、splitArea 随 5.x 关、v6 色板），配置面/交互面/动态面与 6.1 的差距是系统性的。分四档登记：

  **▶ A263-1 致命：整族空板/错位（渲染结果与官方完全不同）**（五项已于 2026-09-10 全部修复，ctest conformance_chart 26/26 + 截图逐个目检）
  * [x] **treemap/sunburst/tree 的 JSON 数据通道缺失**：新增 ChartOption.ParseTree 递归解析 {name,value,children}（ChartModel.zan:3370），系列解析末尾 `ParseTree(s.Get("data"), cs.tree)`；渲染器本体在（ChartViewHier.zan）。
  * [x] **polar 值-值极坐标不存在**：新增 PolarAxisSpec + ParsePolarAxes（angleAxis/radiusAxis 对象与数组形态、polarIndex/min/max/startAngle/endAngle/boundaryGap/data 类目）；`polar:{}` 对象形态落 RadarPolar[0]（圆心/外径）；series coordinateSystem:'polar' → coordSys="polar"，DispatchKind 分派 "polarCoord" → DrawPolarCoord（ChartViewPolar.zan：圆框网格 + cos/sin 投影，值轴 NiceRange、角度 0..360 四分、类目槽位均分）。**两条定点契约**：①数据投影全程保持 ×1000 milliunit（提前除回整数会把半径量化成刻度台阶、角度 ×1000 再 mod 360 锯齿——line-polar 心脏线两轮返工的根因）；②非整度角用 SinDegX10/CosDegX10（整度线性内插），跨 cos/sin 的 y 取负完成数学角→屏幕角。DrawOption/Clone/Create 三处都要拷 angleAxes/radiusAxes（漏 DrawOption → 渲染拿到空轴回落 startAngle=90，整图转 90°）。值-值数据序 [radius, angle, (value)] 进 points（x=r, y=θ, z=value, pointG=1000）。已验：line-polar 心脏线/line-polar2 四瓣玫瑰（0..0.5 域 0..1 轴）/scatter-polar-punchCard 168 点阵。bar-polar 族的扇形柱未落（DrawPolarCoord 跳过 Bar 系列，只剩圆框）→ 转入 A263-3 后续。
  * [x] **多 grid 声明 height 无 top 时底边锚定**：缺省顶锚 top:60（ChartViewLine.zan:423）；grid-multiple 两面板各归其位（截图验证）。
  * [x] **markLine 两点 coord 形式在类目轴坐标域错位**：类目轴按 CatX 槽位映射（ChartViewShared.zan:1078）；line-markline 对角 coord 线落在正确槽位（截图验证）。{b}/{c} 占位与 label.position 仍未做 → A263-3。
  * [x] **对数轴是整数 floor-log10 量化**：ChartFrame.Log10F 定点 lg（×1000，整数段循环 + 四段折线 mantissa，误差 <0.03 档），YOfLog/ValueAtY 走 long 定点域（Chart.zan:259-330）；line-log 平滑连续无台阶（截图验证）。logBase/minorSplitLine 仍未做 → A263-3。
  * [ ] **（本日新发现）radar 组件数组形态不解析**：`radar:[{indicator,...},{...}]`（ECharts5 多雷达）只读对象形态（ChartModel.zan:4463 IsObject 门），数组整块跳过 → radar-custom 的 5/6 指示器丢失、系列 data 名 "Data A/B" 被当轴标签画成双轴雷达。HEAD 已如此（非本次回归）。

  **▶ A263-2 严重：交互/缩放语义缺失（demo 能画但行为对不上）**
  * [ ] **dataZoom 只有滑条**：type:'inside' 键本身不读（ParseDataZoom 只认 show/start/end/startValue/endValue，ChartModel.zan:3163-3180）；无滚轮/捏合/绘图区拖拽框选；数组只取首项（y 轴 zoom 整条丢）；xAxisIndex/yAxisIndex/filterMode/minValueSpan/brushSelect 全不读；数值型 startValue/endValue 无效（只按类目名查字符串，Chart.zan:1914-1917，line-function 的 ±20 窗口丢失）。brush 全仓零命中。area-simple"坐标轴不随可见区域"根因=**Y 轴量程恒扫全量数据**（AxisMaxFor 无窗口参数，Chart.zan:2941），X 窗口化了 Y 没跟；sampling:'lttb' 无解析不抽稀。
  * [ ] **emphasis 渐隐聚焦整体缺失**：emphasis.focus 字段（ChartModel.zan:1354，本日新加）无 JSON 读取、无渲染消费；悬停只出 tooltip/十字线，不渐隐其他系列不加粗本系列（bump-chart 官方核心观感）；axisPointer cross 十字有渲染实现、JSON setter 已接（tooltip.axisPointer.type:"cross" → o.pointerCross，ChartModel.zan:4465）但仅直角坐标渲染器消费，极坐标/多格联动未接；axisPointer link 多格联动零解析；tooltip 只读 show/trigger，formatter/position/order/valueFormatter 全丢。
  * [ ] **动态数据无驱动路径**：ChartController.SetOption/AppendData API 在（ChartController.zan:213-280）但无 timer 接线，gallery 全静态 → dynamic-data/dynamic-data2/line-race/graph-force-dynamic 的 setInterval 语义全靠静态化数据（登记过的 JS_CALLBACK_REPLACED 类，但"应该动起来"的用户期待需内置驱动）；series animationEasing/animationDuration 无解析，line-easing 的 31 宫格核心表达（逐面板不同缓动）退化为 31 条静态曲线。
  * [ ] **line-pen 点击加点**：Click 事件在（ChartView.zan:406），gallery 无"点击 append"接线（JS 类登记偏差，但可作为引擎级 brush/drag 能力的验收 demo）。

  **▶ A263-3 中等：轴/标签/组件配置面缺失（写了静默丢弃）**
  * [ ] **轴**：xAxis position:'top' 不读（ChartAxis.position 注释只支持 yAxis left/right）；axisLine.onZero/onZeroAxisIndex 零命中；axisTick 全家（alignWithLabel/interval/customValues）不读；axisLabel margin/hideOverlap/overflow/showMinLabel/showMaxLabel/customValues 不读；min/max 只认数字（'dataMin'/'dataMax' 字符串静默回落 Auto）；scale/splitNumber/minInterval/logBase/offset/nameGap/nameRotate/nameTextStyle 不读；splitLine.show 无笛卡尔 JSON setter（字段 showGrid 在、写点无）；splitArea 只认布尔（对象形式经 v.Bool 静默变 false）；boundaryGap 只认布尔（['0','100%'] 数值形式丢——area-time-axis/dynamic-data2 的顶部留白消失）；time 轴刻度只有天粒度 YYYY-MM-DD（TimeTicks 天数 1/2/5 步长固定 6 根，Chart.zan:1597-1615），无年/月/时/分自适应与多级标签（area-time-axis"刻度和官网不一样"根因）。
  * [ ] **graphic 组件零解析**（line-graphic 的水印 rect/text 静默丢弃，非 JS 类）；aria/brush/axisPointer 组件零命中。
  * [ ] **timeline JSON 通道断头**：ChartTimeline 组件在（对象 API+播放器），但 FromJsonValue 的 baseOption+options 只取 options[0] 当第一帧，Get("timeline") 全仓零调用 → timeline 声明被忽略。
  * [ ] **visualMap 控件面退化**：continuous 渐变滑条、piecewise 常规分段列表无控件面板（只有 parallel categories 特例+2.x dataRange 底条）；outOfRange/text/orient/width/height/itemWidth 等定位键全不读。
  * [ ] **series 面**：series.color 只认 int（字符串 "#xxx" 静默丢弃！ParseSeriesStyle s.Int("color")，需接 ParseColor）；label.formatter 字段在但 JSON 无 setter（labelFmt 只有对象 API 写入）；labelLayout/blur/cursor/clip/animationDelay/stackLabel/stackStrategy 不读；legend 的 data/selected 映射、icon/itemWidth/itemHeight/itemGap/inverse/type:'scroll' 不读；title 的 sublink/textAlign/backgroundColor/border*/padding/right/bottom 不读，textStyle 只认 color；toolbox 的 itemSize/title(按钮文案)/brush feature 不读；tooltip triggerOn/confine 不读（line-tooltip-touch 的触摸语义缺失，无 pinch 手势代码）。
  * [ ] **各系列型 6.x 键**：line 的 connectNulls/showAllSymbol/areaStyle.origin/symbolRotate/symbolOffset 不读，step 四种渲染不区分（非空同形）；bar 的 barMaxWidth/barMinWidth/barMinHeight/barCategoryGap/realtimeSort/roundCap 不读；pie 的 avoidLabelOverlap/padAngle/percentPrecision/alignTo/labelLine.length*（labelLine 固定两段 ChartViewPie.zan:711-731）/emphasis.scale 不读；scatter 的 effectScatter 按普通散点渲染（rippleEffect 无）；graph 力导向参数是 2.x 面（scaling/gravity 绝对值），5.x 的 repulsion/edgeLength/friction/initLayout 不读；sankey 的 nodeWidth/nodeGap/orient/lineStyle.color:'source' 不读；funnel 的 min/max/minSize/maxSize 不读（sizeRange 被挪用为词云字号）；gauge 是全 2.x 面——progress/anchor/dial/pointer.icon/roundCap 不读（任务清单既有 gauge 现代化项）；heatmap 的 pointSize/blurSize/label.show 不读；map 的 zoom/center/scaleLimit/aspectScale/nameMap/nameProperty/projection 不读；radar indicator 键名是 2.x 的 text（6.x 是 name），name/min/color 不读；treemap/sunburst/tree 的全部 6.x 配置键（levels/breadcrumb/roam 语义/edgeShape 等）随数据通道一起缺。
  * [ ] **饼图 2.x 遗留**：图例逐数据项+跨系列同名去重（ChartViewPie.zan:401/263-289）；百分比 Math.round 取整（699-700）。

  **▶ A263-4 已登记的确定性复刻偏差（非缺口，维护现状）**：官方 Math.random/setInterval/JS 函数生成的数据在本仓 JSON 是确定性展开（area-time-axis 2 万点随机序列、line-function sin/cos、line-easing 曲线值），曲线形态与官网逐点不同属预期；renderItem/ondrag 等回调 API 以公式字段/声明式替代（symbolSizeFn 先例）。

  **修复优先级建议**（按用户可见收益）：① A263-1 五项（空板/错位族）→ ② emphasis 渐隐+axisPointer cross/link JSON 接线（交互观感）→ ③ dataZoom inside（滚轮）+Y 轴窗口量程+lttb（大数据手感）→ ④ series.color 字符串解析（一行修，静默丢弃面大）→ ⑤ time 轴刻度粒度 → ⑥ graphic/timeline JSON 通道 → ⑦ gauge/各系列 6.x 键面。每项验收=对应官方 demo 截图对照（testing-charts-gallery skill 仪式）。

* **A264 登录提速（2026-09-10 完成）**：NativeMemory.Sha256 内建（K 常数编译期全局 + 内建白名单同步）+ Hex.Encode 预分配重写 + stdlib 加解密全面换装 OpenSSL EVP；登录哈希 ~118ms 降至毫秒级。

* **A265 server-game 多 worker 改造（2026-09-10 完成）**：跨 worker 状态上匿名共享表（tokens/auth/cid/online/events）+ 世界单写者 + ops 表跨进程中继，count=N 水平扩。

* **A266 stdlib 加解密吞吐：wrapper 输出侧零拷贝（2026-09-10 完成）**：输出侧从 ~180-220 MiB/s 平板提到 EVP 吞吐量级。

* **legend 迷你传奇·PK 竞技场页（页 1）+ 顶栏导航带两端配平（2026-09-10 完成）**；服务端 Arena 缺口见下条（未做）。
* **legend 服务端缺口（页 1 相关，未做）**：`templates/server/server-game/src/Game/Play.zan::Arena` 只允许 `rival 1..3`（客户端是 16 格），`ArenaState` 无 `rows` 战功榜与 `myRank`（客户端已容忍缺省 → 回落演示名单）；`arena.time`（挑战时间）与「领取战功」op 服务端均未提供（战功按小时结算的语义只在客户端提示里）；另 `TowerRivals` 用 `await Gateway.Db()`，`Arena` 若补 rows 应照该模式写 async。

* **A267 gui_3d_demo 鸿蒙窗口自适应收口 + OHOS/Android 3D 性能定性（2026-09-11 完成）**：2in1 浮窗拖拽/最大化不销毁重建 XComponent 等窗口自适应根修 + 3D 性能定性。

* **A268 server-game 跨 worker 中继换代（2026-09-11 完成）**：ops 表 2ms 轮询 → 环回 TCP 控制通道（GatewayWorker 同构）+ 稳定性/性能实测；遗留 (b) 由 A308 闭账。

* **A271 编译器/运行时/stdlib 全量代码审计（2026-09-11，六路并行代理 + 逐条最小探针复验）**：范围=词法/语法/检查器、irgen（核心/调用/泛型/ARC）、runtime（内存/字符串头/文件/计时/调度/IO/同步）、stdlib 安全面。**探针全在 `_scratch/audit2/`（先看 README.md）**。**已证伪两条，勿再登记**：① 「`await Task.Delay(long.MaxValue)` 有符号溢出→立即触发」不成立（探针 `delaymax2.zan`：打印 start 后睡死，5s 超时仍未醒；`rt_sched.c:291` 的加法溢出是 UB 但当前无可观察误行为）；② 「`irgen_emit.c` 的 `fields[32]/names[32]` 是缓冲区溢出」不成立（1802-1803、1983-1984 都有 `<32` 守卫，是**静默截断**，names 侧无可见症状；fields 侧仅 A281 那一种形状暴露）。以下 A272-A291 均为探针实测或读码确证项，按严重度排列。**2026-09-11 晚本轮修复：A272-A286、A288、A289 全部，A290 的转义半，A291 的 ③④⑥⑦，另新增 A294（已修）；续轮再修 A295/A296/A297/A301 与 A299/A300（详见各「已修」摘要行）；仍留 A287、A290 的 seed 半、A291 的 ⑧（①已于 2026-09-14 修，见各条）、A292、A302×2（见各条；A291②⑤、A298 已于 2026-09-12 补修，见各条）；A293 已修（见该条）。**本轮收尾在 standard 档（785 项）里又归因出 A295/A296/A297/A301（均已修：JSON 数字 token 字面量、arpg Link 判空、HttpsServer 实例调用、Pinyin CRLF）、A298（三个 forwarder 用例 flaky，未修）、A299（selfhost null-safety，已修）、A300（TaskJoin 拉入假阴性，根因=seed 扫描器 chain 双重清空，已修），另登记 A302（tunnel leakcheck 仍可达，未修），并确认 4 项 Gui/GL 红（pagination/transfer/listview 哑停/runtime_gui_gl_3d 目标未构建）来自并行会话在途改动；本树 standard 档当前无法全绿，逐条见尾注。****

* **A272-A283、A288、A289 已修（2026-09-11 审计修复，一行摘要）**：A272 方法体内裸名实例属性赋值让 zanc 段错误（irgen 属性写分支接收者槽误用）；A273 `static async` 返回结构体字段读回 0/垃圾；A274 裸名静态属性读静默返回 0；A275 ≥2^63 十进制字面量静默变负；A276 类无构造函数时 `new C(args)` 静默丢实参；A277 17+ 参数 extern 按裸名调用（编译器栈越界写）；A278 同行 `#endif` 被吞掉致文件剩余内容消失；A279 `Encoding.UrlDecode` 丢 `%00` 及其后内容；A280 深右递归表达式让编译器静默栈溢出（补诊断）；A281 generic 类第 33 个 T 型字段起静默截断 → 误导性 unresolved call；A282 条件编译嵌套 ≥32 越界写 cond_stack/cond_seen_true；A283 字面量/维度表的静默截断与误导诊断（ranks[16] 等）；A288 若干部件的长度解析无上限/可回绕（HttpFramer 分块、头部长度等）；A289 Cookie 域校验缺失与请求头注入（CookieJar/HttpClient）。
* **A284-A285 已修（2026-09-11 运行时修复，一行摘要）**：A284 同一 handle 上并发读与 Close 的 `FILE*` use-after-free（rt_file.c 生命周期）；A285 Windows IOCP 多 worker blocking 完成项丢失后无第二条投递路径 → 永久挂死（补投递路径+登记唤醒包偶发丢失观察）。
* **A286 已修（2026-09-11 安全修复，一行摘要）**：SSRF 判定漏 IPv6 过渡/兼容地址（rt_io.c IPv6 分支补 ULA/link-local/映射地址之外的过渡段拦截）。
* **A287 [P2/安全] 数据面三条明文凭据通道（2026-09-11 审计，读码确证）**：① MySQL 连接器**完全没有 TLS**——`stdlib/System/Data/MySql/*.zan` 无任何 `Tls` 引用，`MySqlConnection.zan:268/282` 的 `doConnect` 直连后从不设置 `CLIENT_SSL`、从不发起 TLS 协商；`caching_sha2_password` 的 full-auth 路径（`:238 fullAuthCipher`）在明文信道上向服务器索要 RSA 公钥（`pemToDer` 无信任锚/指纹校验），主动中间人替换公钥即可解出口令，`mysql_native_password` 的 scramble 可离线爆破。② `System/Data/SqlServer/SqlServerConnection.zan:175-189` 只发 `ENCRYPT_NOT_SUP`、遇 `ENCRYPT_REQ` 直接拒绝——即只能「关闭加密」（拒绝强加密是好的，没有静默降级），`TdsMessage.zan:196` 的口令只是 0xA5 轮转混淆。③ `System/Data/Redis/RedisClient.zan:344` 直接以参数发 `AUTH`，全流量明文。修法方向：MySQL 至少支持 `CLIENT_SSL`（要么实现 TLS 握手、要么把「无 TLS」在文档/API 上标成显式拒绝跨网使用）；TDS/Redis 同此。　**仍留**：MySQL 无 CLIENT_SSL/TLS 握手、TDS 明文混淆、Redis 明文 AUTH 三条数据面凭据通道，修它等于在三个连接器里实现 TLS 客户端（握手/证书链/SNI/信任锚），属功能规模而非缺陷修补；本轮未动。

* **A290 pack_script 密钥种子明文随产物交付（2026-09-11 已修）**：SeedHex 不再写进生成文件，seed 改口令派生 + 产物侧留指纹。

* **A291 [P3] 读码确证、未复现的其余项（2026-09-11 审计）**：① Lua 桥无沙箱且按裸库名加载（`System/Scripting/Lua.zan:481` 无条件 `luaL_openlibs`，`:580-610` 依次 dlopen `lua54.dll` 等裸名——脚本可 `os.execute` 逃逸宿主，可写目录放同名 DLL 即劫持）。② Worker 控制口令牌可预测且记录文件世界可读（`System/Net/Worker.zan:638-643` 令牌=`<pid>-<unix秒>`；`:590-599` 记录文件 `File.WriteAllText` 默认权限写进 temp）。③ `rt_mem.c:436-457` 跨线程 double-free 检测是读-改非原子竞态（两线程同 free 可能都过检）。④ `rt_timer.c:598-620` `zan_timer_delay` 的 `heap_push` 失败分支漏 `free(entry)`（同文件 `:670-674`、`:748-754` 都 free 了）。⑤ `rt_io.c:240-242` 等待者表按 fd 号索引 + `io_fd_usable` 用 `fcntl(F_GETFD)` 判活 → close 后 fd 复用会把老 waiter 串到新 fd（跨连接数据错投，需时序构造）。⑥ `src/common/rpc.c:90` `(int)(content_length - got)` 截断：`max_len <= 0`（rpc.h:38-39 契约允许）时恶意 `Content-Length` 可达堆溢出写；树内 lsp/dap 都传 64 MiB 上限，故**不可达**（公开 API 的潜在缺陷）。⑦ `src/runtime/zan_inflate.c:37` 长度校验是死代码（前缀 `len & 0x4000000000000000` 判定后 `comp_len` 是 u32，`(uint64_t)comp_len + 8 > len` 恒假；正确写法 `len & ~0x4000000000000000ULL`），当前 payload 由编译器烘焙故无实害。⑧ `System/Data/Orm/QueryBuilder.zan:486` 等裸列/裸条件入口（`BuildSelectParams(columns)` 原样 Append，`Where/OrWhere/Having/Join on` 接受整段 SQL）——标识符与值绑定已守规矩，但把不可信输入当 `columns` 传即注入，API 未加约束（按设计，需调用方纪律）。⑨ A258 复核：`irgen_builtins.c:772-829` 当前实现是 find → `cnt--` → `icnt=0` 失效哈希索引 + `irgen_call.c:4109-4240` memmove 保插入序，**描述里的线性探测删除死循环在当前源码已不存在**（只剩 `:754-771`、`:4101-4107` 的陈旧注释在讲旧设计）——建议下次顺手清注释，勿再按旧描述修。　**修（③④⑥⑦，⑤为 2026-09-12 续轮）**：③rt_mem 跨线程 double-free 检测改 __atomic_compare_exchange_n（GCC C11 无 stdbool，weak 参数用 0）；④rt_timer zan_timer_delay 的 heap_push 失败分支补 free(entry)；⑥src/common/rpc.c 的 (int)(content_length - got) 截断改为 INT_MAX 上限判定；⑦zan_inflate 长度校验改 len & ~0x4000000000000000ULL（原 & 0x4000... 使校验恒假）。**⑤新增 `zan_io_close_notify(fd)` 钩子**：Socket.Close 在 close/closesocket **之前**调用，按后端把挂在 fd 上的就绪等待者以「对端关闭」形态（recv 0 / accept -1）唤醒并摘除注册——epoll/kqueue 走 EPOLL_CTL_DEL/EV_DELETE 后 io_take 双轮收割，select 回退走 g_io_entries 链表 io_mark_dead+io_flush_dead，Windows 无操作（CancelIoEx 已让挂起 overlapped 以 0 字节完成；红线基线证实 fd 复用探针加不加钩子输出一致，可观察价值在 POSIX），wasm32 空桩；单一收口点覆盖 Socket.Close 全部调用点，不需要句柄表重构。探针 `_scratch/fixv/close_notify_probe.zan`（conn1 挂 Receive 等待者→close→conn2 复用 fd→等待者得 EOF 而非串数据）3 次全绿；http_forwarder_tunnel / http_client_keepalive / http_server_stress 回归通过。**②Worker 令牌随机化 + 记录文件 0600（2026-09-12 续轮）**：CtlToken 改 CSPRNG 16 字节 32 hex（根命名空间 RandomNumberGenerator，勿 using Cryptography 整目录拉入；CSPRNG 不可用退回 pid-秒——令牌只做 PONG 身份甄别、不是控制命令鉴权凭据，PING/STOP/RELOAD/STATUS 本就不验令牌）；RecordControlPort 写完记录文件后 POSIX chmod 0600（DllImport crt chmod；Windows %TEMP% 在用户 profile 下 ACL 天然仅本用户，无需处理），hex 编码手写 ByteHex 避免拉入。A/B 证据（_scratch/fixv/worker_token_probe.zan，master+2 worker 实跑）：修复前记录=`64491 28852-1789147592`（pid-秒），修复后=`64491 155506a470904637f886871b1c01f026`（32hex）；chmod(384)=0600 经 WSL gcc 实测 rc=0 mode=600（Zan 侧 POSIX 路径本机无 Linux 运行时，属编译+语义等价验证）；探针跑出的 master 多进程 FATAL（worker 甄别路径）红基线证实与本次改动无关（新旧两版 FATAL 一致）。**①Lua 桥沙箱 + 加载白名单（2026-09-14 已修）**：`Lua.Sandbox()` 新 API（宿主在 Initialize 后对不可信脚本调用，幂等）——用受信设置脚本经既有 Exec 摘除 `os.execute/remove/rename/exit`、`io` 整库、`dofile/loadfile`、`require/package`，保留 os.time/clock 与 math/string/table 纯计算库；TryResolve 候选先试 `ProcessHost.AppDir()` 限定路径（--publish 的 lua.bundle 就铺在 exe 旁，裸名搜索在 Windows 缺命中时会继续查 CWD/PATH，可写目录同名 DLL 即劫持；POSIX 限定路径含 / 按 literal 文件处理，纯增益），裸名表保留作回退。实机验证（Windows，仓库自带 lua54.dll 铺 exe 旁）：avail=1、摘除九项全 nil、os.time/math.floor 存活、`os.execute('echo PWNED')` 抛 InvalidOperationException 且 PWNED 未执行；lua_embed_smoke 追加 15 条沙箱断言（Lua 缺席时沿用既有 env-skip 语义打同一 golden）。**顺带确证的平台缺口**：zig 交叉的 linux-x64 产物是 musl 静态链，musl 静态二进制 dlopen 直接报 "Dynamic loading not supported"——Lua/Python 等可选动态依赖在交叉 Linux 产物上本就不可用（与本次改动无关，裸名回退同样 0；动态 glibc 产物不受影响）。

* **A292 [P3] legend 包裹页装备格 Y 与立绘底图格框差 3 设备（2026-09-11 实测，未修）**：`templates/game/legend/src/Spec.zan` 的 `BagEquipY` = 46/83/134/173（左列 54/93），而底图 `assets/ui/00009.jpg`（男）/`00010.jpg`（女）实测格顶是 **43/81/131/170**（左列 132/171）——两张立绘是同一套格位，排行榜页（`PageRank.zan`）用的正是这组且已对参考图零差异（§5.3d），所以包裹页那组整体低 3 设备（2 逻辑像素）。修法：把 `BagEquipY` 对齐实测值后**必须重跑包裹页对 `mir2/mir/包裹.png` 的 A/B** 再提交；本次只登记不动它（超出排行榜页任务范围）。探针方法：按列切片统计「近黑行占比」找格内段，见本次会话脚本 `_scratch/legend-scene`（临时）。　**仍留**：包裹页 BagEquipY 与立绘底图格位的 3 设备差是纯像素对齐问题，改完必须重跑对 mir2/mir/包裹.png 的 A/B 才能提交；本轮聚焦编译器/运行时缺陷，未动 legend。

* **A293 async 方法内嵌套非 async 函数抛出、由 await 的 catch 捕获后 awaiter 局部变量全 NULL（2026-09-11 已修，P1/编译器 async×EH）**：嵌套函数帧的 EH 注册/恢复链补齐；最小红案 exc_local50 转绿。

* **A298 http_forwarder tunnel/stream/keepalive rc=0 截断/空输出（2026-09-12 主因已修；⑴⑵ 于 2026-09-14 收尾闭账）[P1/运行时]**：调度器 run_until 终局误判（主因）+ Windows 关闭不唤醒挂起重叠操作（帮凶，Close 补唤醒）已根修，探针 25/25（修前基线 6/25）。
  连带修用例：http_client_cookies/redirect/timeout 进程退出前不关 HttpClient（keep-alive 连接 + 定时器悬挂），旧规则下靠 ① 摇奖退出，已按 http_client_keepalive 的清理定式补显式 Close + settle 泵（三用例现在确定性绿）。
  **⑴⑵ 收尾（2026-09-14）**：⑴ `zan_io_poll`（legacy 泵/单反应器路径）补上与 `co_wait_io` 同款的 pre-park drain——`g_blocking_wake_lost` 置位后先 `dns_drain()` 再驻车（丢包时作业滞留 `g_blocking_done` 且 `g_blocking_inflight` 仍计数，故 poll 顶部早退守卫不触发；INFINITE 超时下 GQCS 失败分支的超时扫描永不可达=悬挂窗口；POSIX 侧 wake 走管道、在 select 集合里，无此问题，改动天然 Windows-only）。⑵ Windows `zan_io_close_notify` 由 no-op 改为 `CancelIoEx((HANDLE)fd, NULL)`（对齐 rto 截止路径与 `Socket.ShutdownBoth` 既有先例）：显式取消该 fd 上全部挂起 overlapped op，parked RecvAsync/AcceptAsync 以 0 字节对端关闭形态确定性苏醒，不再依赖 closesocket 的 IRP 取消时序。**⑵ 原指认「raw Socket 关闭依旧不唤醒」已证过时**：修前四形状探针（本端 raw Close / 对端 FIN × 单反应器 / `--async-workers` 多 worker）全部苏醒——closesocket 的 IRP 取消本就补发完成包；本轮改为显式保证。**探针还牵出 stdlib 陷阱**：raw `Socket.CreateTcp/CreateTcp6/CreateUdp/CreateUdp6` 不做 `Socket.Initialize()`（WSAStartup），Windows 上静默返 -1——TcpListener/TcpClient/UdpClient 构造器各自初始化，raw 路径无人兜底；已让四个 Create* 自初始化（WSAStartup 引用计数、幂等）。教训：listener/accept 探针必须断言 `CreateTcp>0` 且 `Bind` 返 0——首轮 listener 探针未检查，「accept 苏醒」实为 `AcceptAsync(-1)` 立即返回的假阳性。验证：`socket_close_wakes` 新档（双形状 loopback，单/多 worker 绿）+ socket/dns/net 切片 40/40 + standard 档 835/846——11 红全数归因、与本 diff 无关：10 项被并行会话 Chart 在途态 `ChartViewPie.zan:1390` 语法错连坐（chart_force×2、designer_html、dispatch×2、gui_chart_calendar/barminheight、zform_html、httpsource，拉入 Chart 子树即死）；`builtin_shadowing` 是已提交 d822c1c1 的 stdlib 隐患（见 A315）；`http_forwarder_stream` 是 A298/A302 已登记既有失败（echo-body 内容不符 + 尾截断，与当时 HEAD 的 diff 逐字同型）。
  **调度器终局语义补充（2026-09-12 复核定案，A298 追随实验的教训）**：`run_until` 的循环头先查 root-done 标志——Main 协程一完成调度器即退出，**不经** io_bb 的 has_pending 分支；has_pending 只守「root 仍挂起时的无唤醒轮」（恰是 A298 的误退形态）。因此「main 已返回但仍有挂起 op/停摆协程」时进程退出是设计内行为；这与 rt_sched 纤程调度器（`while (g_live>0)` 排干全部协程，--async-workers）构成两个调度器的语义分叉。判别用例不能用「有挂起 op 就该挂住」的直觉——本会话曾据此误判 d0904018 的编译器半为死码（discrim/mini 两探针都在 main 返回后才判），实为判别用例无效；正确判据是「root 挂起期间被无唤醒轮误杀」的场景（探针 25/25 那组）。
* **A299 已修（2026-09-11 续轮）**：三处判空——`irgen.zan` LoadStaticField(:587)与 StoreStaticField(:597)在 `FindStaticField` 返回 null 时抛 "unknown static field <owner>.<name>"（不 fault）；`irgen_expr.zan` SpecQueue(:2693)对 `CloneSubst` 返回值判空（m 非 null 时 clone 恒非 null，守卫为防御性）。顺带修掉 dbgen.zan 的三处 `FieldIndex(` 调用——该方法不存在（真实助手叫 `FieldFind`，774 行的封装也一并更名 LambdaFieldFind），C host 编译早已拒绝，属既有死代码被 null-safety 扫描牵出。**验证**：C host 编译 selfhost 全 14 源通过；gen1 构建+编译 prog1 并跑 golden 逐字节一致（selfhost_gen1、selfhost_json_ignore 两测试转绿）。`selfhost_fixed_point` 仍红=B6-SH1 既有（自举编译器无 ref/out 形参声明，见该条，不在本轮范围）。
* **A300 已修（2026-09-11 续轮，P1/编译器拉入）**：`conformance_web_typed_binding` 在 HEAD 编不过（8 处 unresolved call 'TaskJoin.WhenAll'）。根因不在注册而在 `pi_seed_source`（`src/compiler/main.c:1066`）的按需拉入种子：镜像「`Task.WhenAll` → flag TaskJoin」的分支（`:1237-1248`）判定依赖 `chain`（点号链头），而 chain 在点号本身（switch default）与每个非点号 token （switch 后置 `if (tok.kind != TK_DOT) chain = NULL;`）处被**双重清空**——恒为 NULL，该镜像与 ns_root 命名空间分支都是死代码。任何用例从未因镜像受益；`async_when_all` 能过纯因用例显式拼写了一次 `TaskJoin.CancelAll`；stdlib 自己的 `Task.WhenAll` （HttpForwarder.zan:961）由 pi_scan_file（无 chain、逐 ident 无条件 flag，`main.c:1011-1037`）扫到。**修（外科式）**：不动 chain 语义（整链复活会连带复活 ns_root 分支，其传递性拉入会破坏 `pullin_qualified_escape` 的遮蔽契约——实测 137 项连坐红），改为在裸名 `Task` 的 TK_IDENT 处用 `zan_lexer_peek2`（新增，`lexer.c` 的 peek 改造为 `lexer_peek_n(n)`，peek/peek2 皆全状态快照）前瞻两 token，`Task.WhenAll/WhenAny` 形状就地 flag TaskJoin；chain 死代码以注释言明留待后续清理。**验证**：三个此前必红的 WhenAll 探针转绿；`web_typed_binding` 输出与 golden 逐字节一致；`pullin_qualified_escape` 单独编译+运行 golden 一致；smoke 档 Gui 在途与一次 ld.exe 竞态（重跑即绿）外全过。注：本修复后的整档 standard 轮无法给出干净数字——并行会话正在改 `stdlib/Gui/Component/Chart/**` （工作树一度删除了 `PolarAxisSpec`/`ChartCalendarSpec` 类而引用它们的文件未同步改），约 136 项在 `--auto-stdlib` 下编译连坐红，与本修复无关（编译失败点全在 Chart 子树）。
* **A302 已修（2026-09-12，根因三段实锤）[P2/stdlib Net] leakcheck_http_forwarder_tunnel 退出时 29 对象仍可达**：A300 修复解除了该用例的编译阻塞后暴露。泄漏图谱（2/2 稳定）：`HttpForwarder.zan` 的 listener/FwdChannel×4/List<FwdChannel>/List<long>×3/FwdReader×4/FwdPool + 用例 TcpClient——全部被「每条隧道一个永久停摆的上游方向泵」钉住（Serve 帧等 Tunnel，Tunnel 的 WhenAll 等泵）。**根因链**：①隧道两条 channel 继承请求级 `recvIdleMs`（用例 `SetTimeout(10000)`），泵的接收走 3 参 `Socket.RecvAsync(带截止)`——Windows 实现是 1→16ms select-轮询循环，**不是挂起的重叠操作**（IOTRACE 里泵的 recv 从不出现在 cnt 里）；②对端方向泵先退出（客户端 FIN→EOF→原 `src.Close(); dst.Close()`），`dst.Close` 把共享 fd 抽掉：select 对已关闭句柄不再报可读 → 轮询循环唯一的 `IsOpen` 逃生口（只写在 IsReadable 分支内）不可达，泵带着死 fd 按定时器空转到 10s idle 截止=进程退出前永不返回；③没有 IO op 挂着，任何 CancelIoEx/关闭都无从唤醒它——Stop+settle 排水无效，缺的不是泵转是唤醒。本地 `Shutdown` 也不够（select 对半关闭套接字恒报可读，但 recv 路径返回 -1 且 fd 未关 → 同样空转，实测）。**修（全在 HttpForwarder.zan）**：`Tunnel()` 入口把两条 channel `SetIdleMs(0)`（隧道沉默本就是常态，继承请求级空闲截止 10s 掐断无流量 SSH/TLS 本属设计疣；无截止接收走 RecvOv 真重叠 op，teardown 的 CancelIoEx 能以 0 字节唤醒它）+ `FwdChannel.Close` 先 `ShutdownBoth`（CancelIoEx，与 TcpListener.Stop/TcpClient.Close 同型）+ `PumpUntilClose` 退出只 Close 自己读的链、对端方向只 `Shutdown()` 半关闭（兄弟泵以 EOF 正常退出、由它亲手 Close，绝不抽兄弟脚下的 fd；发送中被取消的写表现为短写 break）。**验证**（worktree zanc，仅含本修）：tunnel leakcheck **0 对象** + golden 逐字；keepalive golden 逐字（仍 1 对象=A64b 既有，HEAD 同数）；stream 功能 diff 与 HEAD 逐字一致（A298 登记的既有失败，归并行 Socket.zan 重写）；http_client cookies/redirect/timeout/keepalive 四件套 golden 逐字。**留给 Socket.zan/rt_io 车道（2026-09-14 复核闭账）**：带截止 RecvAsync 的两形态已随 RecvToOv 重写（Socket.zan:906 三参 RecvAsync → RecvToOv 真重叠 op + rt_io.c rto 竞速）消亡——三形状探针全绿：①本地已关 fd 带截止 recv 立即交付（0ms，旧轮询循环 IsReadable 恒假 → 永不退出的死形态已不存在，fd 关闭经 close-notify/CancelIoEx 以 0 字节对端关闭交付）；②对端 FIN 带截止 recv 立即 EOF（0ms）；③recv 已驻车后 fd 被抽掉，202ms 确定苏醒（200ms 关闭前延时）。探针 `_scratch/rtoforms/probe.zan` 6 轮稳定，accept-先于-connect 竞态（backlog 连接先于 AcceptEx 驻车则永无完成包）靠探针先启 accept 后 connect 规避，是探针手法不是产品缺陷。

* **A315 [stdlib] StringExtensions.To* 扩展内部调全局 `Convert`，撞用户 Convert 遮蔽类（2026-09-14 发现并当轮修复）[P2/stdlib 设计隐患]**：d822c1c1 给 stdlib/System/StringExtensions.zan 加的 ToInt32/ToInt64/ToDouble 扩展方法内部以 `Convert.ToInt64(s)` 实现——stdlib 源码与用户代码同编译单元时，该全局名解析到**用户自带**的 `class Convert`（tests/conformance/builtin_shadowing.zan 即此形状：自带 Math/Convert/Environment 遮蔽类，合法且受 f32caa9ef 保护），用户 Convert 无 ToInt64 → stdlib 文件报 "Convert has no member ToInt64" 连坐编译失败。**修**：StringExtensions 增加私有 `[DllImport("crt")] strtoll/strtod`，三个 To* 直呼 libc（与 Convert.ToInt32/64 内建降层同一 strtoll 实现，语义逐位一致），DllImport 名字不可被用户类遮蔽；用户遮蔽类照常对自己的直接调用生效，两侧互不干扰。**验证**：遮蔽程序内 To* 扩展（42/7/3.5/null→0）与用户 Convert 直接调用（12000）并存的探针全对；`conformance_builtin_shadowing` 转绿；standard 档 839/846（剩 7 红均归并行 charts/win-smoke 车道）。
* **A304 其余 7 个服务端模板五维抽测 + server-mvc 6 处 type-check 修复（2026-09-11 完成）**。

* **A305 [stdlib/Gui] 导航后内容区间歇性整片不画——树在、像素空（2026-09-11 legend 复刻会话实测，未修，非页面代码缺陷）**：legend 模板（templates/game/legend）经 UiDriver 点击底部导航切页后，内容区**经常**只剩页面底色 `#1d2633`，再导航一次又可能画出来——同 build 同脚本一会画一会不画。已证与页面代码无关：连拍 `_scratch/legend-scene/multi.txt`（m1 画 / m2-m5 全空）里 m3/m4 是**已提交的成就页 26**（69d2bc58，常规 mk-frame 结构）同样空；图鉴 23 页的 LEGEND_DBG 探针（模板 main.zan 1000ms 刻度，`content.HitTest` 走可见性+矩形同一路径）显示 6 个采样点全部命中具名节点、navscan 扫出全部导航 tile、控件树实摆矩形正确；`Navigate` 已包 try/catch 挂 `nav-err` probe（随本次 legend 提交入库），空画时**从未触发**——不是布局塌陷也不是构建异常，是重绘/失效路径丢了请求。波及所有浮窗页（17/22/23/26/29 都中过），legend 验收只能靠「一次运行只做一次导航、不画就重启重试」的仪式绕行（REFERENCE_AUDIT §八）。**疑点与排查方向**：Gui 失效传播（rebuild+Refresh 后 dirty 标记被吞/重绘合并窗口错过）、Win32 后端 present 条件；本轮 stdlib/Gui 后端正被并行会话大面积修改（Native/Win32Shell/Css 在途 + 33b510e4 等 5 个 Gui 提交），无法在半成品 stdlib 上二分定根因——等 Gui 车道安静后用 `LEGEND_DBG=1` 起模板 + UiDriver 连拍复现（探针已固化在模板 main.zan），从 33b510e4 起对 Gui 提交逐个 A/B。另：初判「`mk-frame` 缺 `mk-fixed` 首子才中招」被 m3/m4 证伪，勿按该方向修。

* **A306 [templates/game/legend] 复刻期的三条验收绕行与两处已知组件偏差（2026-09-12 P6/P7 会话，登记备查）**：① **A305 的实操绕行定式**（不是修复）：UiDriver 脚本里每次导航 `click` 同一点两次 + `redraw full`，一次运行只导航一页、不画就重启重试；`dump tree` 的 y 是**屏幕绝对坐标**（root 顶 = 窗 +48）而 ZPX `dump pixels` 是客户区相对，`PNG_y = tree_y − 48`，两者对不上说明有第二个 Legend 进程在写同一批输出文件（`Stop-Process -Name Legend -Force` + 换新输出目录重跑）。② **并行会话改 stdlib 期间的构建定式**：`zanc --auto-stdlib` 会把工作区 stdlib 拉进来，凡并行会话在改 `Gui/**`，构出的 Legend.exe 点导航后内容区整片不画（与 A305 同症状、不同原因，容易误判）。legend 验证一律用 HEAD 干净副本：`zanc <sources> --stdlib-path _scratch/stdlib-head`（脚本 `_scratch/legend-scene/build_head.ps1`，另注 `build.ps1` 的源单里 main.zan 必须排第一）。③ **Pagination simple 档两处偏差**（组件侧，未修）：字段宽 69 设备而参考图约 45；「1/5」的总数画在字段框外。根因是组件内硬编码 `app.Scale(46)`（`Gui/Widget/Pagination.zan` OnMeasure/TotalWidth），且该文件正被并行会话修改，本轮不动。④ **`.arena-plain` 填充 `#16324e` 存疑**：该色在 legend 的每张参考图里出现的像素数 ≤2，从不成面，疑为非原版色；暂无正确样本，待补图或从 `assets/ui` 认领后替换（现仅竞技场页用）。
* **A307 `out` 实参写实例字段编译干净、运行空读崩溃（2026-09-13 复核闭账：与 out_param_lvalue 同一根因，已修）**：`emit_ref_arg` 对非标识符目标（`out b.field`/`out arr[i]`/裸字段名）回落 emit_expr 返回存值被当地址写（read addr=0 崩溃源）；修复=按位置解析真实地址（类实例字段/静态字段/数组与 List 元素/this.field），conformance `out_param_lvalue` 金样已入库并复跑逐字绿；2026-09-13 补试六形态（int/string/引用型 out、跨实例、字段链、ctor 内）均零复现。
* **A308 Socket.RecvAsync(带截止) 轮询洪泛 → RecvToOv 事件驱动竞速（2026-09-12 已修，A268(b) 闭账）[P1/运行时+编译器+stdlib]**：zan_io_recv_to_co 事件驱动 + 编译器 await 适配 + stdlib 换装，登录压测恢复吞吐。
* **A309 登录令牌按账号吊销 + RecvToOv 后登录压测复测（2026-09-12 已修，A268 闭账后置）**：tokens 表账号索引列 + relogin 吊销旧 token（模板层，stdlib 零改动）；WSL count=4 登录压测 31-32/s、p99 53-64ms、零错误零停摆（改前塌到 8/s 有停摆）。
* **A310 字段左值直接赋 `Binding<T>` 属性 → 活绑定悬空目标（2026-09-13 已修）[编译器/ARC]**：根因实锤=活绑定的 `object target` 是刻意设计的**弱引用**（强引用会使 model<->component 图成环泄漏），而右值接收者产出 owned 临时（`f().field`）时语句结束即释放该临时 → target 悬空，下一帧 `Get()` use-after-free（堆复用探针实锤：islive=1 且 churn 后读出空串/坏指针）。**修**（登记修向的「快照退化」半）：`emit_binding_value` 中接收者 `expr_yields_owned_rc_value` 为真时不合成活绑定，降级为 const 快照（`IsLive()==false`——临时源本无「活」可言）；live 路径原手动释放临时接收者的块成死代码一并删除；持久接收者的活绑定语义不变，binding_sugar/field_sugar/local_init 金样逐字不动。新档 `tests/conformance/binding_temp_source.zan`（临时源降级 + churn 复用 + 持久源活绑定回写）四孪生绿；smoke binding/objinit/pullin 全过，standard 813/818（5 红均为既有并行车道账）。gui-wechat 模板「先快照局部」写法可保留（对绑定活得过源作用域的场景仍是正确语义）。
* **A311 用户类与 Gui.App 同名 → stdlib Chart 全家 undefined 'App'（2026-09-12 已修）[编译器/nsresolve]**：三处根因——nr_walk 按「存活声明的文件」解析 partial 成员、resolve_ref 的同命名空间优先被 ctx_ns.len 跳过全局作用域、Gui/ChildWindow+UserComponents 缺 namespace 声明被前两修暴露；精确复现 101 错→0。**仍留（本轮不做）**：模板项目命名为 App 时用户类仍会撞 stdlib Gui.App 被改名 App_2（设计层协调）。






* **A312 限定名 Gui.App 不拉入 stdlib 且静默改绑用户同名类（2026-09-12 已修）[编译器/pull-in]**：main.c 种子链对 TK_DOT 延续误清 chain + 后置守卫吞起始 IDENT，外科式两行修复；pullin_qualified_escape 改判别性用例（用户类/stdlib 类双断言）。

* **A313 生成器子编译进度行漏进父进程 stdout、--emit-ir 通道被污染 → determinism 孪生冷缓存必红（2026-09-12 已修）[编译器/子进程]**：main.c 新增 --quiet/-q 屏蔽成功行/驱动捆绑通知/packaging 行；干净 standard 808/810（两红均预存并行会话账）。
  **归因（先证预存，再修）**：① 我改的 `pi_seed_source` 在 `main.c:3058`，而泄漏点在 `main.c:3026` 的 `zan_gen_design`/`zan_gen_ensure`——**调用次序在 seed 之前**，且 `genrun.c` 未改一行；② 决定性反证：`json_entity_mapping.zan`（本轮未碰、本就触发 codegen）在冷缓存下同样报 `two --emit-ir runs differ`，冷缓存命令为「把 `%LOCALAPPDATA%\Zan\gen` 移走 + 跑 `run_determinism.cmake`」。故**泄漏是预存缺陷，A312 只是让它在一个非设计输入用例上现形**。
  **验证**：冷缓存（移走 gen 目录）下 `pullin_qualified_pull` 与 `json_entity_mapping` 的 determinism 孪生均转为 `deterministic`；冷缓存 `--emit-ir` 的 stdout `grep -c Compiled` = 0，stderr 仍见首用提示；非 quiet 直编仍打印 `Compiled 282 files ? '…'`，`--quiet` 直编 stdout 全空。既有用例：`json_entity_mapping` 是 pre-existing 受害者，同修。


* **A310 已修（2026-09-12，A309 后续）[P2/模板 server-game] A303 报告建议 ②④⑤ 三条落地**：
  ④ **账号级登录失败锁定**：IP 限流（5/5s/worker）防单源扫描，防不住分布式按账号撞库——game_account 加 `loginFails/loginFailAt` 列（SyncStructureAll 自动 ALTER 补列，旧行读 0=未锁定），AccountDao 补 `LoginLocked/NoteLoginFail/ClearLoginFails`（与密保 answerFails 同构的 1h 窗口语义，阈值 10 次）；Gateway.Login 密码校验前查锁、错一次计一次、成功登录与密码重置清零。锁定拒绝文案不带「账号不存在」信息，不透露账号存在性。e2e +12 断言（10 次错密逐一确认文案 + 第 11 次正确密码被拒）114→125→127→**138/138**。
  ② **默认口令启动告警**：Schema.WarnBootstrapPassword 在启动时对 admin 账号散列比对 admin1234，命中即响亮打印（实测 `[security] *** 默认口令 admin/admin1234 仍可登录… ***` 上墙）。**不做强锁**：报告观察项 1 原文「模板演示预期 admin/admin1234 可登录」，e2e 也依赖默认口令登录管理后台；强锁（首登强制改密流）留给二开按需收紧。
  ⑤ **README/头注 ev 推送清单对齐实际代码**：删 `ev online`（在线数走 realms/hb 响应字段，无推送）、`ev fight`（手动 hunt 的回合详情在 op 应答 fight 字段，不单独推）、`ev hb-notice`（Gateway 头注残留，代码无此事件）；补 `ev idlesum`（挂机每 60s 批量结算，原文档漏写）；auto 挂机描述从「每 tickMs 打一回合推 ev fight」更正为批量结算语义。逐 Ev("…")/NewStr("…") 调用点枚举核对，全部对齐。

* **P7d 收尾：LSP 索引 .html 设计稿（用户点名补齐的迁移缺口）**（2026-09-12）：
  P7d 把设计稿全迁 .html 时漏了 zan-lsp——intel_parse_zform 只认 .zform JSON，
  新项目设计控件的补全/goto-def 静默失效。修法：intellisense.c 新增
  intel_parse_zform_html（行扫描投影：body data-zan-design 标记 + id= 表单名 +
  data-kind 控件类型 + data-on-*/data-submit handler，嵌套容器拍平，与 .zform
  路径同假设）；intel_parse_file 派发 .html/.htm；两个目录扫描点（Win32
  FindFirstFile / POSIX readdir）都加扩展名；publish_diagnostics 对 .html/.htm
  发空诊断（顺带补上 .zscene 此前就缺的同款守卫）。didOpen/didChange 自动走
  update_project_index，设计器实时编辑即入索引。探针四断言全过（补全命中
  UserName:Input / goto-def 逐行落 id="UserName" / .html 空诊断 / didChange 后
  Extra:Checkbox 实时可见），lsp_baseline_probe gallery 模式无回归（hit-rate
  2/2、def ok）。教训进 zan-lsp-intellisense skill：新存储格式落地必须同步加
  LSP 索引通道 + 双扫描点同步 + 设计稿扩展名空诊断三件套。

* **P7c 完成：游戏 HUD 帧内接入实测（P7 最后一项）**（2026-09-12）：
  定案"游戏和 Gui 无差异"（用户拍板）——不建独立 GameHud 宿主/第二表
  面，游戏就是跑在标准保留模式循环上的 Gui 应用：帧体 = 世界直接画上
  画布（HUD 树之前）→ MeasureTree/Arrange/RenderTree（RenderFrame 公开
  契约的组合调用）→ PresentFrame，HTML 声明的 HUD 就是同一棵树的普通
  控件。探针实测（1280×800、240 实体、flex 顶栏+血条+状态条 HTML HUD、
  DPI 1.5）：渲染段 clean avg 3ms / dirty avg 3ms / max 23ms（预算
  16.6ms@60fps，余量 5 倍）；空闲 120 拍仅渲染 3 帧；像素级证据：干净
  帧 HP 条逐帧同色 0xFFE5484D（世界重绘不透入），脏帧内联样式更新像素
  即变。台账：运行期 HTML 流式子元素 % 宽未生效（内联/样式表 alike 回
  落 auto=100% 母宽），血条宽度驱动现走颜色/文本，顺延 P8。
  探针踩坑（已进 game-dev skill）：外部帧宿主必须每圈 SetPollEventMode
  （一次性语义，否则无事件拍阻塞 WaitEvent——RequestRedraw 在
  ProcessEvent 之后执行救不了上一拍）；Show 不置挂起重绘、首帧前手动
  画基线帧；GetPixel 必须在 Present 之前读（present 后画布不保留）且
  回读同步昂贵、每样本一个像素。探针在 _scratch（已清理），数字以此
  条目为准。


* **P8 开工（组件精简期），首项：流式子元素 % 尺寸修复**（2026-09-12）：
  P7c 台账项落地。根因两层：① `StyleDeclaresWidth/Height` 声明门只认
  绝对值（`computedStyle.width >= 0`）不认 Pm 千分比字段，`width:50%`
  被判"未声明"——流内块宽回落 auto（=100% 母宽）、高塌 0；②
  `StyleBox.MetricIn` 在包含块未定（测量路径传 avail=0）时把 % 解析成
  0 而不是回落。修法（stdlib/Gui/Control.zan + StyleBox.zan）：
  声明门加认 Pm；新增 `StyleDeclaresWidthAbs/HeightAbs`（只认绝对值）
  给包含块未定的测量路径——grid 轨道尺寸（GridOuterW/H、colAvail/
  rowAvail）、float 重测（MeasureFlow/ArrangeFlow 的
  StyleDeclaresHeight 重测分支）、内在宽（MeasureFlow 主路径、float
  内在宽累积）、flex 断行（% basis 按声明值/测量偏好回落，不再落 0）
  ——css-sizing"内在尺寸计算中百分比视作 auto"语义；`MetricIn` 对
  avail<=0 回落 fb（防御，不再解析成 0）；空块塌穿豁免认
  `heightPm/minHPm`（CSS 8.3.1 有确定高的块不塌穿）；ArrangeFlow 的
  `chKids` 在容器声明高时用真实 ch 作包含块（fillH 只描述 prefH 来源
  不代表高未定）。 arrange 侧（1663/1675/1691 行区）与行盒/inline/float
  落位（1773/1871）保持完整门（传入真实包含块 cw/ch/frameW/wAvail）。
  验证：探针五用例全绿（样式表/内联 % 宽 150、% 高 100、px 对照不变）；
  Chrome oracle 新用例 tests/weboracle/pct.json（pct_driver.zan）六盒
  0px 偏差；conformance_gui_css 加 DisplayPercent 回归锁（5 行）+
  gui_flex/gui_tree/gui_calendar/gui_carousel/gui_zform_control(.html)/
  listview×2/scroll_reanchor/datatable×2/listitem 金标准逐一无回归；
  flex-basis 断行修正后 gui_flex 仍逐字节一致。

* **P8-2a 前置：编译器补局部 `Binding<T>` 声明初始化降级**（2026-09-13，
  3f2aab13）：局部声明 `Binding<T> b = w.prop;` 从不走 emit_binding_value
  ——裸 T 直接存进 Binding 槽位，首次 Get/Set 解引用裸指针段错误。赋值
  路径（irgen_expr.c）一直有 lowering，声明路径（irgen_stmt.c
  AST_VAR_DECL 类型声明分支）从来没有（A/B 对比证明）。修法：声明初始
  化器非 Binding 类型时先试 emit_binding_value（结果 owned +1，与 new
  同权：跳过用户转换查找、store 走 owned_rhs_marker move）。回归锁
  tests/conformance/binding_local_init（活绑定/常量/null 解绑 8 行）。
  P8-2 属性面生成化需要这个语义（PropSpec 槽位直接吃控件字段的活绑定）。

* **P8-2b 属性面生成化第一刀：class 收敛为基类契约**（2026-09-13）：
  删掉 39 处控件 Props() 里的 `class` 重复声明（30 处一行式
  `ps.Add(PropSpec.Text("class", "Classes"))` + 9 处三行式
  `cls.str = Class`），class 的真相单点化在 Control 基类：
  GetProp/SetProp 本来就短路应答 `class`，序列化有专用尾部字段
  （WriteNode L83 / ReadNode L189），.html 设计文档走 f.Class modeled key
  ——per-prop 声明在所有消费点都是死行（gallery WatchTree 跳过
  !IsBound()，Designer Inspector 用 FieldSpecs 不吃控件 Props()，
  FormBuilder 只查 label 键）。ZformSchema 扫描器在 ReadControl 里单点
  注入 class（key=class/label=Classes/type=string，已有声明则不重复），
  zform.json 从 39/78 控件带 class 变成 78/78 全覆盖，schema 更完整。
  序列化文档双向兼容：新文档不再写 per-prop class（尾部字段仍在），
  旧文档的 class 键经 SetProp 短路照常读回。测试同步 6 处
  Props().Count 断言（watermark 15/countdown 3/image 3/
  numberanimation 9/pagination 15/timeline 1）+ gui_timeline.out
  golden 同步（props count/props 1 两行）。验证：gui_props/
  gui_zform_control(.zform+.html 两通道) golden 字节级一致；六测试全绿；
  gallery 全量编译过；GenKnowledge 实跑 zform.json 注入核验；
  run_zform_schema.cmake 校验面（Props() 覆写清单 + GenForm 键文档）
  不受影响；smoke 全绿（除 barminheight 为 Chart 在途外部失败）。
  顺手修：tools/mcp_server/gallery.json seed 三条目
  （gui-window/gui-components/gui-webview）App.zform→App.html 引用翻转
  + models.gui-designer 措辞改 .html——policy_gallery_coverage 此前因
  P7d 删 .zform 模板后 seed 悬空而红（HEAD 继承），本次恢复绿。

* **P8-2c size 枚举收敛 + 扫描器 options 补全**（2026-09-13）：
  审计发现 ZformSchema 扫描器根本不解析内联
  `new List<string>{...}` 选项——zform.json 里全部枚举属性的
  options 一直是缺失的（只有 .Option() 链式的 FormField.dock 有）。
  两刀一起落：① PropSpec 加 `Sizes()`（tiny/small/medium/large）/
  `Sizes3()`（small/medium/large）常量工厂，16 处 size 枚举字面量
  （10×四档 + 6×三档）全部改指工厂，字面量单点化；每次调用新建
  List（PropSpec 持 options 引用且 Option() 追加，共享实例会串台）。
  ② 扫描器 ReadChainText 认 `PropSpec.Sizes()/Sizes3()` 工厂与内联
  花括号列表（与 Option() 同追加语义）；ReadControl 先过 JoinBraces
  把折行 List 初始化器并回一行（只并含 "List<" 的起始行——控制流
  块不并，块内逐行声明不因合并丢；配对计数在 Mask 后文本上做，
  字面量里的花括号不算）；`PropSpec.Sizes()` 续行从 "unable to
  parse" 告警豁免（选项经 pending 链取，非独立声明）。
  结果：zform.json 枚举属性 45 个里 44 个带 options（唯一缺口
  Bargraph.orient 用局部变量表，纯文本扫描无数据流，台账保留）；
  折行多选项声明（ButtonGroup.type/DatePicker.type）借此补全。
  验证：gui_props/gui_zform_control golden 字节级一致；timeline_test
  （size 枚举消费方）全绿；gallery 全量编译过；GenKnowledge 实跑
  核验 44/45；smoke 全绿（除两个外部在途失败）。

* **P8-2d GetExtra/SetExtra 收敛第一刀：字面量绑定 spec 的死写面**
  （2026-09-13）：普查 29 文件 90 个 GetExtra/SetExtra 键臂，分三类：
  ①「both」键（spec 与 Extra 臂并存）——spec 未绑定时 Extra 臂是唯一
  写路径，spec **绑了字面量**（`spec.num = 0` 常量绑定）时 PropOf 拦
  在 SetExtra 之前，写入全死、GetProp 恒读 "0"；②「extra-only」键
  （无 spec，Extra 臂是唯一表面）——合法形态，属派生键/复合键；
  ③派生键三件套（快照 spec + SetProp 截写）——skill 已载，不动。
  第一刀修 ① 的五个死写面（探针逐键往返验证）：Avatar.shape
  （`num = 0` 挡死 SetExtra，GetProp 恒 "0"、SetProp 恒 circle）
  改 `shape.str = Shape` 删 Extra 臂；Carousel.dotPlacement 与
  direction（后者原绑 `Vert() ? 1 : 0` 表达式快照）改 str 绑字段；
  Collapse.arrowPlacement 同改；DatePicker.type 的 SetExtra 带
  editor.SetText(FormatText()) 副作用不能走绑直写——删 `num = 0`
  让 spec 回到未绑定态、SetExtra 恢复可达（spec 只管序列化/schema
  可见性）。顺带删 ChoiceGroup 两个类里被 str 绑定挡死的 4 个
  size Extra 死臂。普查分类与其余「both」键（DataGrid 10 键、
  InputNumber 10 键、InputOtp 6 键等，多为派生/复合键需逐键判）
  留待后续刀次。验证：五键探针往返全绿；gui_props/gui_zform_control
  golden 字节级一致；gallery 全量编译过；smoke 全绿（除两个外部
  在途失败）。

* **P8-2d 第二刀：57 个不可达 Extra 死臂删除（13 文件）+ 直调测试
  改走管线**（2026-09-13）：判定矩阵 = spec 绑定状态 × SetProp 覆写
  截写 × Extra 臂语义等价性 × 测试直调。删除的臂全部满足「spec 已绑
  （PropOf 拦截）且无 SetProp 覆写截写且臂语义与 spec 路径等价且无
  测试直调」：ColorPicker mode、Image.fit、Input.status、InputOtp
  block/length/mask/readonly/separator/status、Progress.percent/
  circles、Slider.marks、StatusBar.options、Tabs.options、
  ToolStrip.options、TextArea maxlen/round/showCount/status、
  Pagination jumperLabel/jumperSuffix。三个保留判定记台账：
  ① ColorPicker.showAlpha 臂调 SetShowAlpha（关时重置 alpha=255），
  spec 直绑 flag 丢副作用——整文件保留待三件套转换；
  ② InputNumber.value 臂是 ParseScaled 缩放/精度解析，spec
  Convert.ToInt32 语义完全不等价——整文件保留；③ ChoiceGroup/
  radio_group 的 options 臂被 conformance 直调且多类文件待逐类判
  ——保留。image_test/pagination_test 的直调 Extra 臂断言改为
  GetProp/SetProp 管线断言（直调 Extra 臂 = 断言实现细节，管线
  才是行为契约；pageSizes/order 走 SetProp 覆写截写、jumper 键走
  spec 字段绑定，均已验证等价）。验证：image/pagination/colorpicker/
  inputnumber/slider/tabs 测试全绿；gui_props/gui_zform_control/
  gui_css golden 字节级一致；gallery 全量编译过；smoke 全绿（除
  两个外部在途失败）。

* **P8-2d 第三刀：语义不等价臂的三件套转换（ColorPicker/
  InputNumber）**（2026-09-13）：第二刀挂账的两文件按判据矩阵逐键
  落地。ColorPicker：showAlpha 走 SetProp 覆写截写（SetShowAlpha 的
  alpha=255 重置是真语义，直绑字段丢重置——探针实测管线关再开
  alpha 残留 0.5），mode 删双臂、GetProp 应答归一化名（原臂
  ModeName 语义，spec 直读裸 Mode 不归一）；value/color/text 无
  spec（extra-only）臂保留。InputNumber：value/text 截写走
  ParseScaled（千分位与按 precision 缩放的展示文本，spec 裸
  Convert 只认纯整数）+ SetRaw 收口（钳制/校验/事件），读侧
  DisplayText；precision 读侧覆写 Decimals 钳位（spec 读裸
  Precision）；min/max/step/hint(placeholder 别名)/group
  (groupDigits 别名)/prefix/suffix/placement 八键经证等价（普通
  字段直赋=实时绑定），臂全删；loading 无 spec 保留臂。
  colorpicker_test 补管线 alpha 重置回归锁（专属实例防污染后续
  弹层用例）；inputnumber_test 直调段转管线（loading 仍走 Extra）。
  验证：两 widget golden 字节级不变；prop_test/gui_css golden
  match；gallery 320 文件类型检查过（native 链接 __imp_
  CoInitializeEx 失败系并发会话在途 zanc/CMake 状态，.zan 层无关）；
  受影响面 smoke 子集 10/10——但 run_case.cmake 的 STAMP
  up-to-date check 复用旧 exe，ctest 绿是回放（未提交改动不碰
  stamp），直编才是真值（教训进 zan-development skill）。zform_
  control/zform_grid 直编失败系并发会话在途 GenForm/checker（定向
  stash A/B 实证与本次改动无关；测试文档只用 Button/Card/Label/
  Panel）。

* **P8-4 .zform 遗留链删除：设计文档只留 .html 一种形态**
  （2026-09-13）：P7d 全仓库迁移 .html 时刻意保留的 .zform 编译通道
  （只为打开旧用户项目）按 P8 精简期决定砍除——不迁移、定向报错。
  C 侧：genrun.c `zan_is_design_path` 删 .zform 臂（.zscene/.html/
  .htm 不变）；main.c 输入扫描对 .zform 报定向错误（"convert the
  document to an .html design doc and recompile"），APK 打包的
  suffix-strip 收敛为只剥 .zan；genrun.h/nsresolve.c/genrun.c 注释
  同步去 .zform 表述。Zan 侧：GenForm `Translate` 删 isZform 分派
  臂（只认 .html/.htm 设计稿）、名称回落 strip 删 .zform 臂、8 处
  "zform field" 文案改 "design field"，头部注明 `DesignerHtml.
  FromJsonDoc` 是 .zform→.html 的规范转换器（模型级保真）。测试与
  数据：gui 侧 grid/compref_app/batchjob_app 三份 .zform 经
  FromJsonDoc 自己转换成 .html 入库（非手写，金样逐字 MATCH 证明
  保真；zform_control.html P7d 已在），diag 两份 .zform 同转并改名
  invalid_design_kind/control_design_entry，CMakeLists 删
  conformance_gui_zform_control 注册块、改名两 diag 用例并更新
  EXPECT_REGEX（含 .role. 键面），孤儿 datepicker_smoke.zform 一并
  删（零引用）；docs/HTML_UI.md 格式节写明通道已删与转换指引。验证
  （stdlib 副本 --stdlib-path 直编，绕开并发在途 Chart.zan 半成品）
  ：zform_html/zform_grid/compref/batchjob/designer_html/css_test
  六金样 MATCH；diag 两例错误正则过；.zform 拒绝诊断实测打印；
  gallery 320 文件类型检查过。IDE 内部 ~135 处 .zform 字面引用
  （Workspace/ZanIDE/CodeNav 等）是 IDE 自身功能臂（打开旧项目、
  JSON 抽屉、撤销快照），不属编译通道，留独立提交清扫（需
  build_ide.ps1 完整配方验证）。

* **P8-4 后续：.zform 清扫收尾 + 命名债挂账（2026-09-13）**：
  编译通道删除后的三个清尾批次——① lsp：intel_parse_zform JSON 索引
  通道删除（.html/.zscene 通道共享 helper 更名 intel_design_widget/
  intel_doc_name_lines），顺带补 zan-lsp 源清单缺失的 builtin_api.c
  （fc2dda55 起 checker 引用 zan_builtin_member_result，清单缺口被
  链接错误暴露）；② 五个脚本 + run_templates.cmake 的设计文档发现面
  P7d 时漏改（scan_components 按 .zform 兄弟找 code-behind 恒不命中
  →IDE 组件注册表静默少收 2 个；check_structure 守门扫错扩展名；
  build_oneplus 收集 .zform 会被定向拒绝；e2e/脚手架改名臂恒死），
  全部翻转 .html，gui-wechat quoteBar Panel→Flex 类型错顺修；③
  51 文件措辞对齐（stdlib 注释/模板头/gallery 资产/mcp_server 工具
  描述/docs 活文档/skill ②③）。验证：templates_build 33 模板 32 OK、
  lsp 探针全绿、stdlib 类型检查过、check_structure 与 HEAD 同结果。
  **命名债（.zform 移除拍板后的收口状态，2026-09-13）**：文档/技能面
  已清——docs 口径统一为"格式无关的控件 PropSpec 目录（文件名沿用
  历史 zform.json）"，zform-review 五份旧评审归档 docs/archive/，
  game-dev/gui-design 技能已教 .html 设计稿新方案。剩**物理改名**
  待 knowledge/MCP 管线专项一次执行，映射表：
  zform.json→controls.json、zform.doc.json→controls.doc.json、
  ZformSchema.zan/ZformResult→ControlCatalog.zan/CatalogResult、
  zan_form_schema（MCP 工具名，对外破坏面：外部客户端+zan-mcp/ai
  文档需同改）→zan_control_schema、MCP 响应键 zformControls→
  controlCatalog（对外）、run_zform_schema.cmake→run_control_schema
  .cmake、conformance_gui_zform_html→conformance_gui_controls_html
  （ctest 名，内部安全）。**在途半成品挂账**：
  server-legend Player.zan 字段改名（arenaPts→merit、stars→starsLit、
  删 buffUntil 加四币种）模型侧已改、消费端（Play/Gateway/World 的
  Set 链与读写点 ~13 处）未跟上——模板门 templates_build 因此红，
  待该会话收尾；templates_build 其余 32 模板全绿。

* **P8-3 文档动态原语：`data-if` / `<template data-for>`（四方同语义）**
  （2026-09-13）：HTML 声明层补上"按状态显隐 + 逐项渲染列表"两条
  原语，语义由挂 JsonValue 模型的宿主消费（与 bindPath 同一契约，
  无模型宿主一律惰性）。**data-if → `Control.bindIf`**（新声明字段，
  Serialize 尾部可选字段往返）：ChildWindow.SyncFromNode 每帧
  `Truthy(PathGet)` → SetShown——null 假/布尔原样/数字非 0/字符串
  非空非 "false"（比 AsBool 宽，路径值多是字符串状态名）。
  **`<template data-for>`**：parser IsBlockTag + "template"（容器空白
  语义）、UA 样式表 `template{display:none}`（原型隐藏，Chrome 语义）；
  Wire 登记 → SyncFromModel 开头核对源数组长度，变了整组重建——逐
  **原型子项** `Html.Clone`（新静态方法：Element 同构特判搬运标签/
  文本/属性表/文档序，其余按 ControlFactory 重建 + Props 声明槽 +
  几何/绑定字段，未注册 kind 跳过）成行、普通 InsertAt 插模板紧后
  （不走 Element.AddKid，撤行走新 `Element.DropKid` 清文档序表——
  防 elOrder 陈旧条目幽灵占位）；行内 bind/bindIf 以本项 JsonValue
  为第一作用域（ChildWindow 侧表 scopeKeys/scopeVals，项内命中优先
  回落根模型；回写落数组元素引用就地生效）；模板原型子树不参与
  同步/回写。**Element 缺省绑定属性 = text**（GetExtra/SetExtra 投影
  SetText/Text）——行内 `<span data-bind="name">` 直接显示字段；
  真控件缺省仍 value。**四方发射/消费同语义**：运行时 Html.Parse
  属性协议；GenHtml（data-if→bindIf + Element 分支 data-* parity
  SetAttr，编译/运行建树同构）；GenForm EmitField（"if"→bindIf、
  Element "for"→SetAttr(data-for)）；FormBuilder.MakeControl 同两处；
  DesignerHtml FieldJson/FieldHtml 升级 template↔Element+"tag" 键
  （"tag"/"for"/"if" 不进 IsModeledKey，设计器 extra 透传保真）。
  语义边界（docs/HTML_UI.md「动态原语」节）：嵌套模板 v1 不支持；
  Element 父内模板宜为末子项（行渲染在全部已跟踪内容之后）；HTML
  通道行内 style 经 zgen-N 类规则对克隆生效，FormBuilder 通道
  ApplyInline 实例字段克隆不带走。验证：conformance 四用例
  （gui_html_dynamic 运行时通道、gui_zform_dynamic 设计稿两源、
  gui_html_runtime 编译期两源、designer_html 往返扩展输出行不变
  金标免改）全绿。**顺带发现两个既有编译器缺陷（挂账，与本轮改动
  无关、HEAD 干净树可复现）**：① `x is T ? a : b` 解析失败——
  parse_binary 对 `is` 的类型操作数走 parse_type_ref，类型语法把
  `?` 无条件吃成可空类型标记（parser.c:1933/412），需 C# 式歧义
  消解（`?` 后跟表达式则归三元）；现状括号 `(x is T) ? a : b`
  合法；② 极小 using 集（pull-in 闭包过滤后）编译报
  "cannot convert 'CellOf' to 'CellOf'"（Transfer.zan:204/207、
  DataGrid.zan:89/306/341，泛型类内 delegate 同名解析分裂）——
  最小探针触发、同程序大闭包全绿，疑闭包裁剪掉了同名类型的一致
  解析所需文件，待专项定位。

### P8-3b 事件带参 data-arg（2026-09-13，提交见 git log）

用户点名的三件套之③（"收益最小，复杂逻辑本来就该在代码"——落的是
最小声明面，不是表达式求值）。HTML 里
`<button data-on-click="pick" data-arg="apple">` 把字面量 `apple`
作为实参传给带参处理器；列表行共用一个处理器区分来源的最小方案。

- **Control.handlerArg** 字段（声明通道，v1 不进序列化——设计器事件
  模型是纯名字，带参文档由 HTML 层承载）。运行时 `Html.Build` 预扫
  `data-arg` 落字段（属性顺序不保证，先于全部 data-on-* 消费）；
  `Html.Clone` 两分支随 bind 三字段一起带走。
- **双注册表带参槽**：`HtmlHandlers.AddArg/FindArg`（Dict 二表）与
  `HandlerRegistry.SetArg/GetArg`（HandlerEntry.argAction）。Set 不清
  argAction 槽、SetArg 不清 action 槽，同名双注册并存。
- **`Html.WireArg(handlers, c, evt, name, arg)`**：名字照落控件
  （SetHandler），带参槽命中则把实参闭包进无参 Action 再 BindEvent
  ——实参是只读捕获局部 = 创建时值快照（"这个控件永远带着它的参数"）；
  未命中静默不接。**顺带把 `Html.Wire` 的契约补齐**：名字无论有没有
  注册表都落控件（此前 Build(null) 建的树 c.events 为空，ChildWindow
  宿主根本无从二次解析——GenForm 生成码早就是 BindEvent+SetHandler
  双发，运行时通道补齐同一契约）。
- **ChildWindow**：`HandleArg(name, Action<string>)` +
  WireNode 带参优先解析（handlerArg 非空且带参槽命中 → 有参闭包；
  否则回落无参 Has/Get——只注册了无参版也能接）。
- **GenHtml**：EmitDecl 落 `vN.handlerArg`；EmitWiring 对带参节点发射
  `Html.WireArg(...)`，生成码与运行时建树同语义。GenForm/FormBuilder/
  DesignerHtml v1 不建模（见上）。
- **语义边界**：data-arg 是静态字面量快照不是绑定路径——模板克隆行
  共享原型上的同一实参，行身份走 data-bind 回写通道；嵌套/表达式
  不支持（复杂逻辑在宿主语言）。
- 验证：_scratch 探针四通道全绿（运行时注册表、ChildWindow 二次解析、
  模板克隆行、GenHtml 生成码双宿主路径）；conformance 扩展
  gui_html_dynamic（静态按钮 + 模板行按钮带参断言）与
  gui_html_runtime（handlerArg 字段 + 两宿主路径断言），金标均不变；
  爆炸半径 gui_css/timeline/props/nav/zform_html/zform_dynamic/
  zform_grid/designer_html 八例字节级一致。

### P8 收尾清帐（2026-09-13， afternoon 批次）

P8 精简期全部剩余项处理与销账；roadmap P8 置 ✅。

* **IDE 内部 .zform 功能臂清扫（687faf39）**：54 文件，约 135 处字面
  引用收敛到 14 处（全为 legacy .zform 导入面：IsFormPath/FormBaseName/
  LegacyFormToJson 等，注明 legacy-import only；Workspace 33→0、
  ZanIDE 17→1、CodeNav 15→14、其余 ~50 文件注释/文案翻转，
  AiProfiles PathFilter 补 .zcomp）。验证：全量 zanc 编译到 bind
  0 错 0 警——codegen 阶段 LLVM verifier lambda_87 失败与清扫无关
  （stash 基线 A/B 同错 + 新旧编译器 A/B 同错双证）。**A/B 方法论
  挂 skill**：worktree 构建的 zanc 按 exe 目录解析 auto-stdlib
  （main.c:2951），与显式主树 stdlib 输入双根双注册，产生 100 个
  假 ambiguous type 的 583 错假象（zan-compiler-internals）。
* **缺陷② CellOf 闭账**：6 探针形态 × 2 拉入模式在当前 HEAD 全绿
  无法复现；台账行号（Transfer.zan:204/207、DataGrid.zan:89/306/341）
  所指代码已不存在，疑 A312 限定名逃逸根修顺带治愈。skill 同提交
  改记闭账状态，保留诊断 heuristic（同名类型互斥转换先疑闭包裁剪）。
* **挂账缺陷③（新，已修，本轮闭环）：nsresolve 冲突改名丢泛型实参
  → lambda 委托调用参数个数错 → IDE 构建 verifier 失败**——IDE 全量
  输入报 `lambda_87: Incorrect number of arguments... call void
  %dlg.fn(ptr %clo.rec, ptr %load4)`。根因链：stdlib/Gui/Event.zan
  （namespace Gui）与 stdlib/Gui/Reactive/Events.zan（namespace
  Gui.Reactive）各有一个零参 `delegate void Action()`；IDE 构建 glob
  全 Gui 树 → 两声明跨命名空间冲突 → nsresolve 把双方改名为
  Gui_Action/... 并改写引用，但改写不看泛型实参——Gui 命名空间文件
  里的 `Action<string>` 被改写到非泛型名上，实参在 binder 静默丢弃，
  类型变 pc=0 委托；ChildWindow.WireNode / Html.WireArg 的
  `Action<string> aa = ...; () => { aa(arg); }` 于是按 pc=0 发射
  argc=1 的调用，verifier 拒绝。**为何只炸 IDE 构建**：conformance/
  单文件走 auto-stdlib 按需拉取，Gui.Reactive 不入编译 → 无冲突 →
  不改名 → 一切正常；glob 全量（IDE）必中招，与设计文档数量无关
  （此前设计二分的全部信号皆假，度量被"类型错误=假通过"污染）。
  修法：nsresolve.resolve_ref 按泛型配对规则过滤候选（引用带实参而
  候选声明类型参数个数不符 → 不改写，回落内建 Action<T>；bare 形式
  照旧改写）。诊断手法立功：ZANC_DUMP_BAD_IR 拿到挂掉函数 IR +
  临时 arity 审计（expect vs actual）+ 全模块 dump 找到 lambda 的
  创建者函数反查捕获 → `delegate=Gui_Action pc=0 argc=1` 一锤定音。
  conformance 新用例 ns_conflict_generic_arity 锁形状（四连体
  Passed；standard 层 834 例除在途外部失败外全绿；build_ide.ps1
  全量绿灯 IDE_BUILD_OK）。
* **缺陷④（新，已修 280e08d6）：void 调用结果当值漏诊**——
  checker_check_assignable 把 type_void 与 type_error 同路放行 +
  调用实参无签名时（重载/内建）无检查，void 当实参/初始化器静默
  生成非法 IR。修法三处（assignable 报 cannot convert 'void'；
  无签名实参报 cannot use a 'void' value；有签名者按参数类型判，
  delegate 参数放行 spawn 惯用法，Spawn/Run 白名单对齐 irgen
  is_call_to）。tests/diag/void_value.zan 锁两形态。闸门教训：
  第一版通用诊断误伤 Task.Spawn(Work(n)) 核心惯用法（stdlib 10+
  处），probe Gate.zan:54 当场暴露后收窄——**加检查先查 stdlib
  既有惯用法**。
* **P8-2d 收口（3a2c501b）**：ChoiceGroup/RadioGroup 的 options
  Extra 臂按三件套判据判死删除（spec 已绑 + 覆写拦截）；slider
  marks 直调测试转管线——上一刀删臂漏转、ctest 绿系 STAMP 回放，
  本轮 stdlib 变更触发真编译暴露（回放教训已在 skill，此次再次
  实证）；DataGrid 普查判决 = 全部 Extra 键读写自身 st.* 状态
  （523 行注释明言规格不绑定）合法 extra-only。29 文件 90 键臂
  普查至此闭环，属性面生成化 P8-2 全部完成。
* **policy_zform_schema 复绿**：a60aabb6 导出器让 GenForm 读
  Element 的 "text"（ObjStr(o,"text")→SetText），zform.doc.json
  漏登记——补记 text 键（3a2c501b）。
* **复合控件声明化重组：决策不做**（roadmap 台账有全文）：设计器
  编辑属性面而非复合内部结构；复合控件是行为体，整体 HTML 化需
  DesignerHtml 全键建模（P7 级工程）收益不抵风险。
* **standard 层补跑（缺陷① IOU 销账）**：832 例，除在途会话用例
  外全绿。本轮修正后转绿 2 例（gui_slider、policy_zform_schema）；
  在途外部失败 11 例（chart×6=Chart 会话 N3 treemap 系列、
  gui_html_widget_shadow=他会在途未跟踪用例、http_client_keepalive
  超时/http_forwarder_stream/mysql_async_nonblocking=Net 在途编辑、
  win_tray_screen_smoke/listview_scrollbar_drag=环境）；
  **conformance_gui_compref 1 例运行期 FAIL 已归因闭环**：不是回归
  是契约翻转——a60aabb6 设计字段名优先（GenForm/FormBuilder 在
  SetDesignText 后重申组件显式 name），Label 的 name 稳定为
  Card1_InnerTitle，不再被 caption 覆盖；compref_test 仍按旧契约按
  caption 查找故断言失败。（此前"编译失败"的判断建立在畸形 repro
  上：缺 compref_app.html + BadgeCard.zcomp 两输入，正确输入集下是
  运行期断言 FAIL。）测试已翻新为新契约（按
  Shell_FieldName 查找、caption 作内容断言），COMPREF_OK，ctest
  Passed；金样不变。
* **skill 增删（rule 13）**：zan-compiler-internals（①）——
  exe 相对 auto-stdlib 的 A/B 大坑 + CellOf 闭账改记；
  zan-development（②③）——`x is T ?` 旧教训翻转（已修，22d75d0e）
  改记新语义 + int? 解包模式不支持 + as 失败产空串。

### 折线族第三波（2026-09-13，平滑坍缩 + splitLine 维度默认 + 时间值对堆叠）
* **已完成**：BuildPathFxEx 贝塞尔基单位错乱（‰ 幂配 1e6 常数项 → b0 恒
  ≈1 → 每段采样坍缩段起点，smooth 全错、末段丢失）换百万分定点真贝塞尔
  基 + Bernstein 配对；splitLine 官方维度缺省（xAxis false / yAxis true，
  SSR 实测）+ ParseAxisOne 补收 splitLine 声明（splitLineDeclared/
  splitColor/splitWidth）；axisLabel.margin 接线（缺省 8，bump-chart 30
  曾被写死 Scale(6) 盖掉标签数字）；DispatchKind 堆叠判定提到点系列之前
  + DrawStackedArea noCatX 路径（时间值对 stack 曾按未堆叠折线画）；
  DrawStackedArea 量程 NiceRange→NiceSpan6（330→400 曾致堆叠带低 19px）；
  轴级静态 axisPointer（show 缺省 'auto' 声明 value 即画，含 label 盒）。
  新增 conformance `chart_axis_splitline_pointer`；36 demo 比对 27 PASS 且
  四个用户点名 demo 逐图目检与官方一致；charts_test.exe 已重建。
* **挂账（真缺陷）**：grid-multiple grid2 值域（#b6d634 IoU 0.25，
  引擎带体 391..455 vs 官方 330..527）；axisLabel.inside 未实现（标签画
  到绘图区外被画布裁剪，line-tooltip-touch Y 轴）；axisPointer.handle
  未画（官方底部小把手）。
* **挂账（oracle 伪差，不改引擎）**：confidence-band（官方线色带 opacity
  混色，精确 hex 匹配天然 MISS）；dynamic-data2 / line-easing（官方 SSR
  定格动画起始帧，引擎画终态=实机终态）；line-tooltip-touch #7581BD
  IoU 0.01（oracle 只收 stroke path，label 盒是 fill rect）。
* **已闭账（测试基建，2026-09-13）**：standard 层 10 例失败初判"用例读仓库
  相对路径 + ctest 默认 cwd=build/"。实测拆三类：①cwd 问题只占 chart 5 例
  （dataminmax/cat_backfill/force_params/series_zorder/tree_depth）及其
  leakcheck/arcguard twins——修法未走 add_test 逐个补，而是 run_case.cmake/
  run_leakcheck.cmake/run_arcguard.cmake 三脚本统一默认
  `WORKDIR=仓库根`（由 CMAKE_SCRIPT_MODE_FILE 推导，显式 -DWORKDIR 仍优先），
  全部转绿；②win_tray/policy_gallery_coverage 在近期提交中已自愈。
* **已闭账（2026-09-14，夹具违反 HTTP 义务非转发器缺陷）**：conformance_http_forwarder_stream
  的 `echo-body: keep-alive`——raw-socket 上游夹具读到头终止符就回显，
  从不读它自己声明的 Content-Length 请求体；转发器合法地把改写头与
  体分两次写（SendAllAsync），头/体是否落进上游同一次 RecvAsync 全凭
  TCP 分段运气（实测 6 连跑 1 绿 5 挂、行数恒齐仅第 9 行变）。**修**
  （tests/conformance/http_forwarder_stream.zan）：夹具按
  Content-Length 读全请求体再回显——比旧形状更强地验证体中继。
  12/12 稳定；同家族 http_forwarder_keepalive 12/12（历史 1/6~1/2
  丢唤醒包 flaky 已被 A298 系列修复治愈，见该条 1956 行）。
* **挂账（charts 车道，渲染缺口非基建）**：conformance_gui_chart_calendar
  （FAIL render spread custom-calendar-icon）与 conformance_gui_chart_
  barminheight（FAIL stacked positive/negative segment drawn）均为内容
  断言失败——calendar 已显式 -DWORKDIR=源树根仍挂，证与 cwd 无关，归
  charts 会话在途工作收口。
* **已闭账（并发构建污染，2026-09-13 深夜安静窗口复跑定谳）**：上轮
  standard 期间 zanc.exe/zan_gui.dll 被并行会话在途改动反复重链
  （21:11/21:14/21:20 三次），跑出的部分 gui/http 失败是移动靶——复跑
  842 例仅 5 失败：http_client_keepalive、gui_listview_scrollbar_drag
  均自愈转绿（坐实上轮假红）；本会话测试基建修复车道（chart 5 例及其
  leakcheck/arcguard twins、测试三脚本默认 WORKDIR）全绿。余下失败
  分属各自车道：http_forwarder_stream（上行挂账，转发器真实缺陷）、
  gui_chart_calendar/barminheight（上行挂账，charts 渲染缺口）、
  gui_zform_html（container class 断言，gui 会话在途 zform/Wizard 半成品
  所致，且 zan_gui.dll 复跑中途 23:45 又被重链）、gui_httpsource
  （HTTP 命中数 0，疑环境或在途运行时，待 gui 会话落地后复核）。
  教训不变：并发期 standard 结果不可作归因依据，test.ps1 预构建会把
  工作树里他人在途改动编进产物。
* **挂账（本轮悬停修复的 standard 层，待安静窗口补跑）**：ChartView
  悬停槽改造（Keyed 每帧新建实例 → 跨帧悬停状态进 wid 槽）+ 命中
  变化补调度一帧 + 类目线点 item 悬停卡，实机 bump-chart 渐隐/值卡
  全通、chart_cached_events 金样 MATCH、theme/stackedarea-aa 用例过；
  standard 层因并行会话 ZanDb 重构 + Tabs TEMP-DBG 占用构建无法运行，
  下个无并发构建的窗口补 `scripts/test.ps1 standard`。
* **挂账（用户实机审查 2026-09-13 批次）**：①geo lines 车道未实现——
  lines-airline（type:"lines"+coordinateSystem:"geo"，[[lng,lat],...]
  航线对 + world 地图）地图画了航线没画；②小数显示成整数——SeriesFrac
  通道未全覆盖，demo 待定位；③地图区域拼接边界可见，待 map-usa/world
  放大复现；④geo-svg 悬停扰动渲染，待复现；⑤大数据卡死（bar-large
  5e5 点级），性能预算+降采样；⑥缺 x/y 轴/缺自定义组件/尺寸错/少内
  容/轴外/遮挡各若干 demo 待定位——需全量 347 demo 审查 sweep（截图
  +SSR oracle 对拍）逐一定位，"有的"必须落到 id；⑦lines-ny 32 分片
  二进制流式（ready:false，用户点名）：Float32Array 流加载车道。
  本次已修：凹凸图 live 重掷（nomouse 钉死保回归）、线体悬停
  （SegDist2 线段命中，官方 linePrecision 对齐，值对路径 PointsHover
  仍只点半径待同修）。
* **全量 347 demo 审查 sweep 落地（2026-09-13 续）**：gui_charts 新增
  `--sweep` 启动参数——逐 demo FromJson+KeyedStatic 渲染一帧
  `Canvas.WritePixels` 落 `_scratch/sweep/<id>.zpx`（ZPX1）+ manifest.txt
  （id/ready/phase/ms），异常按例捕获不中断；_scratch/sweep_analyze.py
  解码出 png + 统计（非背景占比/调色板命中/内容 bbox/边缘接触/轴线暗
  游程）+ FLAG 启发。首轮 345/347 渲染 ok（nojson=lines-ny/custom-wind 两个
  ready:false），185 FLAG 经人工看图分诊，"有的"已落到 id：
  - **已修（本批）**：geo 投影散点半径启发式 `r=Scale(3)+z*Scale(14)/100`
    对大值域爆炸——scatter-world-population（人口 1.35e9→半径 1.9e8px）、
    geo-choropleth-scatter（z 1e5→14003px）各把整画布刷成一个系列色。
    改为解析 visualMap `inRange.symbolSize`（ChartVisualMap.sizeLo/Hi/
    hasSize，含 Clone；官方 [6,60]/[5,30] 语义按 minRaw/maxRaw 或系列
    数据域线性映射直径）+ 未声明时旧启发式 + 凡 z 驱动半径一律
    Scale(60) 硬帽；visualMap.seriesIndex 按用户系列序计（不含合成
    isGeoBase）。另修 LeadSeries 对 0 系列 option 的 declOrder[0] 越界
    （sweep 尾声暴露的运行时错误）。
  - **挂账（雷达车道，比预想深）**：radar 主力 demo 即坏——series
    data[{value:[...]}] 多边形整条没画、radar.indicator 的 name 未解析
    （ChartModel.zan 只读 max）、DrawPolar 轴标签错拿图例文案
    （XCategories 无雷达指示器路径）；doc-example/radar（纯组件无系列）
    落 "(no data)"。需要独立会话：指示器名进模型 + 多边形绘制 +
    0 系列画骨架。
  - **挂账（cartesian 热力图车道缺失）**：heatmap-large/piecewise
    （[x,y,v] 对组 20301 点）现走表格热力车道（1 系列=1 行）→
    cellW<1 纯白；heatmap-cartesian 168 点画成 1 行退化条（恰没被判
    BLANK）。需按 yAxis 类目=行、xAxis 类目=列、visualMap 连续 ramp
    上色的官方语义建车道。
  - **挂账（bar-race-country 纯白）**：dataset + realtimeSort + encode
    车道未实现，series.data 空 → 无柱。
  - **挂账（matrix 坐标系内容）**：matrix-* 11 例：骨架（行/列带）画出
    但系列内容全无（matrix-pie 有图例无饼、matrix-simple 全空）。matrix
    coordSys 车道待建。
  - **挂账（组件级骨架）**：doc-example/polar-anticlockwise /
    polar-start-angle（series:[] 纯 polar 组件）官方画极坐标网格+角度
    刻度，现落 "(no data)"。修法方向：DispatchKind 0 系列时若
    polar/angleAxes 存在 → "polarCoord"、radarMax 存在 → "radar"，
    DrawPolarCoord/DrawPolar 0 系列本就画骨架。
  - **挂账（geo-svg doc 三连维持 ready:false）**：geo-svg-label-basic /
    named-basic（simple_svg 命名元素 + geo.regions[].label.formatter，
    ChartSvgMap 无标签支持）、geo-svg-layout-basic（六种
    宽高/viewBox/boundingCoords 布局变体）——素材与语义来自 ECharts
    手册 SVG 底图教程，需 SVG 命名元素标签车道，非一行注册可解。
  - **启发式噪声（不改引擎）**：EDGE-CLIP LTRB 于 treemap/sunburst
    （官方本就满幅）、polar 类 NO-X/YAXIS、graph-force 稀疏 0.08~0.11
    nonbg 均为误报；图表类 NO-YAXIS 多因 ECharts6 浅色轴线过不了暗游
    程阈值。
* **html_gallery 全量重设计 + 事件/声明三根修复（2026-09-13）**：
  画廊重做（77 卡 × 7 分类页、1560×920 四列大卡、原生 HTML 语义
  `<button>/<input>/<img src>/<label>` 与 data-* 通道等价），实点验证
  Tabs/Pagination/Collapse/SelectBox/Wizard 列表交互全通。本轮 stdlib
  修的三处根因：
  - **Tabs 页签点不动**：Tabs 的 Select 按几何自己消费点击、不经过
    On 通用事件包，而 FireCommon 只给 On.Any() 的控件注册命中区——
    宿主只用 `TabChanged.Add` 类型化接线（On 为空）时，按页签=
    按空白（hitId<0 → pressOnBlocker），释放被 ClickAvailable 判成
    "点外部"吞掉。修：Render/RenderVertical 把条带注册成命中区。
  - **Control.SetProp("name") 截断组件同名属性**：IconView 的图标名
    等走任何声明通道都喂不进。修：PropOf 命中则写属性，syncName 的
    才顺带改标识名。
  - **ListItem PropSpec 死快照**：`text.str = this.Label()` 绑的是
    返回值快照而非 Binding 字段本身，SetProp 写进死快照界面永不
    出现。修：绑 `Text`/`Desc` 字段（编译器合成实时访问器对）。
  - 演示侧配套：Wizard 全窗 Render/Show 不可 Dock，卡内嵌
    RenderList 子件 + 实时描述；FormField 是设计器文档节点无运行时
    绘制，运行时形态 = FormBuilder.Build(设计 JSON)，演示卡按此喂
    迷你表单；BandGrid 热力图不设 Heat 即与底色融为一体；Trend
    量程是原始计数（位号 scale=10 时 0..100 装不下 1520）。
  - **挂账（① 已修 2026-09-14，df10f8b5）**：① 词法器字符串字面量
    4095 上限已摘除——lexer_string/插值段/verbatim 三路径的栈缓冲
    char buf[4096] 改 zan_lex_strbuf_t 堆生长（4KiB 起倍增；OOM 退化为
    诊断+截断，转义解码始终执行防反同步），GenForm EmitSetProp 分块
    发射可保留（兼容不依赖）；conformance big_string_literals（1 万/
    10 万字节 + 转义/插值混排）4 孪生绿；② .html 词法化噪音已由 d295e5aa
    （2026-09-14）根治——namespace 扫描跳过设计稿/.zcomp + 三个启发式扫描
    diag 转 capture，与本项为同一树内先后提交，此处不再单列。
* **雷达车道已修（2026-09-13 第二批）**：根因三个——①radar 系列的
  data 项是值数组（对象 {value:[...],name} 或裸数组），旧标量车道
  Double("value") 对数组取 0，全部多边形塌成圆心一个点（radar 主力
  demo 即坏，radar2/radar-multiple/radar-aqi/radar-custom 同炸）；
  ②radar.indicator[].name 从未解析，轴标签错拿图例文案；③纯组件
  option（series:[] 只有 radar/polar）落 "(no data)"。修法：解析期
  **值数组物化**——每个数据项物化成独立系列（图例取数据项名；未命名
  继承系列名，radar-aqi 31 天同名正好吃到图例"同名系列随主项一起开
  关"语义；样式拷源系列，逐项 symbol/symbolSize/lineStyle/itemStyle
  覆写落物化系列），值 ×1000 定点（o.radarScaled），radarMax/
  polars[].indicatorMax 同步放大（渲染公式 val×g×r/(maxV×1000) 在
  milli/milli 下约掉 g），tooltip 显示走 FracText 除回（0.46 不再
  丢）；radarNames 支持 ECharts5 name + ECharts2 text（doc-example/
  radar 的 指标一..五）；DispatchKind 0 系列时 radarMax/radarNames→
  "radar"、polars/angleAxes→"polarCoord"（两渲染器骨架路径 0 系列
  本就安全）；顺修 SymbolSanitize 不认 ECharts5 标准名 "rect"（只认
  旧名 rectangle）。实机：radar 双多边形+指示器名与官方一致、radar2
  28 层多边形、radar-custom 4 项逐项符号、radar-aqi 星爆、doc radar
  纯组件骨架（不再 no data）。conformance：chart_radar_values 九断言
  金样。**遗留观察**：radar-multiple 双 polar 是否该分画两个圆心
  （ECharts2 polar[] 缺省 center 语义）待对官方截图；radar-aqi 线宽
  1/opacity 0.5 DrawPolar 固定 2px 不透未接。
* **radar-multiple / radar-aqi 遗留观察闭账（2026-09-13 第三批）**：
  对官方 TS 源核验后两处都坐实为引擎缺口，本批修讫。①radar-multiple：
  物化 json 里 radar 三联数组（center/radius/indicator.text）**一直
  完整在案**，缺的是解析——FromJson 只认 radar 对象形态，数组形态
  （ECharts5 多雷达）整个落地。修法：radar 数组每条目解析进
  o.polars（center/radius/splitNumber/indicator name+text/axisName
  色，与 ECharts2 polar[] 共用 DrawPolarMulti 消费端），条目 shape
  缺省 polygon（radar 语义，polar[] 保持圆环缺省），series.radarIndex
  作为 polarIndex 别名收编；首条目回填 radarMax/radarNames 供单
  polar 回退与 0 系列分发。顺带揪出**物化器连乘 bug**：满刻度 ×1000
  放大块在逐系列循环里，多雷达系列（3 个）把 radarMax 连乘 1000³
  （100→1e8）——之前单系列 demo 不暴露；改为用 radarScaled 旗守卫
  只放大一次。实测三雷达分画菱形/五边形/十二月环，center 25/50/75%
  与官方一致，放大 12 轴环确认为十二边形（与官方 polygon 缺省同）。
  ②radar-aqi：DrawPolar 单 polar 路径补 lineStyle.width/opacity 与
  areaStyle.opacity 消费（多 polar 路径补 opacity；width 本就有），
  radar.axisName.color（旧 name.textStyle.color）→ o.radarNameColor
  金色指示器名；legend.textStyle.color → o.legendTextColor 全链
  （ChartOption 字段+Clone+DrawOption+DrawPanelI 传参，19 个调用点
  统一插参）——深底 demo 图例白字，此前主题灰字看不清。实测金轴名/
  白图例/细线(1px)/雾面(opacity 0.1/0.05)全数生效，与官方截图同构。
  回归：chart 层 211/213（仍只剩台账在案的 calendar/barminheight 两
  个批次前失败），全量 sweep 345 张 FLAG 182 与改前持平，conformance
  chart_radar_values 增至十一断言（multi/multimax）4 变体全绿。
* **大数据卡死闭账 + 异步装载落地（2026-09-13 第四批）**：
  用户点名的「大量数据加载卡死」两层根因都修讫。①**力导向布局
  O(n²) 热循环 8.3s/帧**：graph-webkit-dep（nn=513, links=942）、
  graph-npm（nn=492）首帧卡 7-8s。逐段插桩钉准在 ForceLayout.OfK
  弹簧模拟，再往循环内部插标记证明换掉 List<ForceNode> 对象访问
  **毫无变化**——真凶是 `ChartView.ISqrt`：整型牛顿迭代**初值取
  r=v 本身**，d²≈1e5 量级要 ~17 次带 long 除法的对折才到 √v，每次
  ~1000 周期，30 轮 × nn² 对全中招。修法：斥力/引力内循环改
  `Math.Sqrt`（硬件 sqrt）+ 平方域剪枝（d²≥k² 直接 continue，与原
  `d<k` 语义逐位等价，k 恒 ≥8 故钳制分支不受影响）+ 坐标/邻接抄进
  原生 int[]（CSR 邻接表把 attraction 从逐节点全边扫 O(n·en) 降到
  O(度)）。实测 sim 8111→78ms，graph-webkit-dep 6916→122ms、
  graph-npm 8196→194ms，全量 sweep 24.5s→12s。ISqrt 本体未动
  （其余车道依赖），台账留观察：其初值可改 `r = 1<<(bit_length(v)/2)`
  一类的近猜，属后续优化。②**demo 首开在 UI 线程 FromJson 卡交互**
  （geo-svg-scatter-simple 解析 3.1s）：gallery 落地异步装载——
  OptionOf 未命中即插 pending 槽 + `Thread.Start` 后台 worker
  （只做 EmbedRead + FromJson 纯计算，不触 UI 状态），成品经
  `app.Post(捕获闭包)` 整只移交 UI 线程（App.Post 自带
  window.Wake() 唤醒阻塞中的 WaitEvent、DrainPosts 执行后自动置
  needsRedraw——与 DataTable.HttpSource/ImageHttp 同一定式）；
  pending 期间画「加载中…」占位。`--nomouse` 回归与 `--sweep`
  审查路径保持同步装载（单帧截图必须终态；sweep 有独立的
  FromJson 通道不受影响）。实机端到端：启动 1s 内见加载占位且
  UI 活着，解析完成后冰岛地形图完整渲染，全程不冻结。
  ③**顺带清除存量噪音与脚手架坑**：GaugeScalar 补 `scalar == null`
  守卫——pie/gauge/polar 各车道大量「整标量直传」调用点把 null 传
  入（Zan 宽容语义每帧打两行 runtime error，radar-multiple/custom
  只是冰山一角，sweep 全量数百条）；守卫按 0 处理与既有渲染逐位
  一致，sweep runtime error 归零。Chart.zan 里 71af2a7f 批次漏拆
  的 `DBG grid` 每帧打印删除。sweep 机器补 `_scratch/sweep` 目录
  自建（目录缺失时逐张 WritePixels 静默失败、末尾 manifest 整体
  IOException——干净检出/目录被清后 sweep 直接崩）+ 逐 demo 进度
  行 + WritePixels 独立 try/catch（render-error 不再连坐落盘失败）。
  回归：chart 层 ctest 仅台账在案两失败（calendar/barminheight），
  全量 sweep 345 张 FLAG 182 持平，manifest 仅 lines-ny/custom-wind
  两个已知 nojson。
* **ZanDb 扫描层零拷贝化 + 段不可变 CRC 验一次（2026-09-13）**：
  针对「过滤/投影与 SQLite 差距好几倍」的机器层改造：①SegCursor
  惰性物化——NextRecordRaw 裸解析只记键值在块内的偏移/长度，
  Key()/Val() 首次访问才 GetString（命中行才分配），上界与跳读改
  CmpKeyRaw（块内指针 memcmp，与 CompareOrdinal 同序）；②Store
  新增 `ScanRows`/`ScanRow`（零拷贝扫描行，值按偏移/长度/指针
  交付，多源归并退化模式回退字符串包装）；③等值过滤**针线否证**
  ——needle（≥2 字符、含非数值渲染字符、无 0x00/0xFF、非
  true/false）原字节经 memchr+memcmp 不在记录值区出现即整行否决，
  免物化免走读；④LoadBlockReuse 块缓冲跨块复用（消每块
  Alloc/Free），段不可变故 CRC 每块只在首次加载校验一次（读时不算
  派生数）。微基准：块内裸解析 ~230-315ns/行、字符串物化 ~37ns、
  memcmp/memchr ~5ns； ByteBuffer 有状态方法逐字节走读比字符串
  索引慢 2-4 倍（ReadU8 每次 new Span）——走读必须落在字符串上，
  「全裸化」反而更慢，已按实测回退为包装。**更正（2026-09-14）**：
  次日 Span 探针推翻本条「语言层地板」结论——`Span<byte> sp =
  new Span<byte>(nint, len)` 即裸解引用，~2.2ns/字节，比字符串
  `s[i]` 走读快 4.6 倍、比 ByteBuffer 有状态方法快 ~8 倍，可写、
  逐次重构造免费（LLVM 拆成指针+长度）；当时比较的两级（字符串/
  ByteBuffer）都非最快形态。扫描到目标字段仅走读几十字节，各场景
  瓶颈在游标机器/块 IO/树构建，故数字未变；大字段扫描应走 Span。
  同轮对拍中位：投影
  2.0→2.3 倍、索引点查 2.6→2.2 倍、全扫 3.0 倍、过滤 ~8.9 倍、
  Count ~23 倍反超；机器高负载（并发会话）噪声 ±30%，扫描地板
  （游标机器 + 每块 IO + 惰性物化）与 SQLite 纯 C 列解引用的差距
  属语言层地板，README 已记。金标 zandb_* 12/12、ctest
  `-R "zandb|arr_lit"` 52/52（含泄漏孪生）。**挂账（编译器）已修（2026-09-14）**：
  实例 `extern` DllImport 方法（漏 static）此前通过类型检查，irgen
  生成含 this 的调用约定在 LLVM verify 报 "Invalid bitcast"/参数数不符；
  现检查器声明点直接报错 `extern method 'X' must be static`（check_extern_static，
  extern_lib/MOD_EXTERN 判定 + MOD_STATIC 豁免，全树扫描零存量实例 extern），
  diag_extern_instance_method 新档（WILL_FAIL）绿；最小探针 `_scratch/externstatic/`。
* **geo-svg 悬停扰动闭账（2026-09-13 第五批，用户实机审查批次④）**：
  用户报「鼠标经过对 SVG 的干扰依然存在」——geo-svg-scatter-simple
  悬停海面时整图蒙上巨大半透明浅蓝三角形 + 弹 "trip2 0" 卡。三层
  根因叠加，修讫于 ChartSvgMap/ChartViewMap/ChartModel：
  ①**未命名形状不构成可交互区域**（官方 SVG 底图语义：交互区域只
  来自 name/data-name/祖先 g name/id 命名形状）：冰岛 SVG 3061 个
  形状仅 2 个命名（trip1/trip2），其余全是海面/装饰，旧扫描全部
  收进 regions 逐个参与命中。
  ②**fill="none" 线稿不可区域命中**：trip1/trip2 是 stroke-only
  航线（NoFill 新判定：fill none/transparent、fill-opacity≤0、
  style fill:），官方只做描边带命中；旧扫描把它们当实心多边形，
  悬停海面误触航线 ring → emphasis 填充即「巨大三角」。
  ③**SVG path 命令字母大小写即绝对/相对语义（根因）**：PathRings
  解析时 `cmd = ch >= 97 ? ch - 32 : ch` 把大小写归一，小写相对
  命令被当绝对执行，path15592 一条路径的后续点全部漂移成横跨
  全图的弦多边形（SCANDBG 实证：改前 bbox (-59,-16)-(1729,1264)
  4 点，改后 (1720,1248)-(1779,1264)）。修 `cmd = ch` 保留原样，
  下游 `rel = cmd >= 97` 照常推导。
  **顺带修复（同根排查揪出）**：(a) geo 散点/lines 数据被通用数值对
  车道（×1000 定点）与 geo 专用车道（GeoQ ×100、y 取反）双路齐收，
  一半点屏外一半屏内——ChartModel 数值对分支加 `coordSys != "geo"`
  守卫；(b) SVG 底图坐标系（用户单位 ×1）与矢量地图（度 ×100）投影
  混用导致 SVG 底图上散点全丢——ChartViewMap 新增 ProjX/ProjY
  （svgUnits 分支按 viewBox 线性映射），effectScatter 六点落位与
  官方一致；(c) geo 散点悬停只发事件不画卡——补 TooltipCard；
  (d) ChartSvgMap 补 `<g name>` 组名继承（flight-seats 座位名全在
  g 上，扫描后 regions 从 0 变 179）；(e) svgMode 零命名区域
  （纯装饰素材）也要画光栅底图——`(!svgMode && regions.Count<1)`
  才早退。回归：全量 sweep FLAG 182 与 HEAD 基线逐字节 IDENTICAL
  （A/B 隔离出 +1 geo-seatmap-flight BLANK 即 (d)(e) 所修），
  实机海面悬停干净、flight-seats 座位图完整渲染。
  **已闭账（2026-09-14，「DPI 鼠标空间错位」定谳为误诊，引擎无缺陷）**：
  上批记的「150% DPI 下 app.mouseX = 物理光标 ×1.5，悬停命中全体
  右下偏移」不成立。进程内探针（GetCursorPos+ScreenToClient 与
  app.mouseX 同帧对照）40+ 样本 mx/my 与物理客户像素逐像素相等
  （WM_MOUSEMOVE lParam → postQ → evX → mouseX 全链无缩放，窗口
  客户区 1222×806 = 画布 1:1）；实机 area-simple 光标物理钉在
  (900,600)，轴指针十字与 tooltip 卡、轴标签全部钉在光标处。
  当时的「×1.5 实证」是**测试工具伪造的**：光标落点用 DPI 不感知
  进程的 PowerShell SetCursorPos 摆放，OS 对 unaware 进程的鼠标
  API 坐标做 ×1.5 虚拟化，光标实际落在 1.5× 目标处，引擎如实
  上报（HITDBG px2=788 vs mx=1211 即 788×1.5+窗口原点杂项）。
  教训沉淀 gui-design：外部光标自动化必须先 SetProcessDPIAware，
  鼠标空间疑云用进程内 GetCursorPos+ScreenToClient 对照定谳，
  不用注入式 SetCursorPos 当证据。
* **geo lines 车道闭账（2026-09-14，用户实机审查批次①）**：
  lines-airline（"World Flights"，3.2 万航线对）地图画了航线没画。
  六处根因/缺口一批修讫（ChartModel/ChartViewMap/gui_runtime/
  gui_gl_backend）：
  ①**裸数组数据形态**：geo lines 的 data 除
  {coords:[[lng,lat],...]} 对象形态（geo-lines）外，还有条目本身
  即 [[lng,lat],[lng,lat]] 的裸数组形态（lines-airline 无包装），
  旧解析只认对象形态，geoLines 收 0 条。
  ②**LinesSeries 缺省 coordinateSystem='geo'**（官方
  LinesSeries.ts defaultOption）：geo-lines 迁徙的 lines 系列
  全都不写 coordinateSystem，旧解析只认显式声明→系列不挂 geo
  静默不画；现仅当 option 存在 geo 组件时补缺省（无地图的纯
  lines 兜底车道不变）。
  ③**lines 的 lineStyle.curveness 从未解析**（只有 force 边解析，
  与 force 共用 forceCurveness ×1000 字段）：官方 0.3 弯曲航线
  画成直线。顺带消费 lineStyle.opacity（0..255 解析早已落表，
  渲染端从未取用）——官方 opacity 0.05 的 3.2 万线密度叠加
  发光，全 alpha 会把世界图糊成一片实色。
  ④**原生 polybatch 悬空**：Render.DrawPolyBatch 的 extern 从未
  有原生实现（gui_backend.h 的 polybatch 槽位也无人填），首次
  接线即链接期 undefined symbol。补齐：gui_runtime.c 导出（优先
  后端批量，退逐条 polyline）+ GL 后端 gl_polybatch（整个批次
  共享一次覆盖缓冲清+UNION 采样+一次合成，gl_polyline 的多路径
  推广；scissor 收敛到全批 bbox）。3.2 万线 sweep 单帧 ~0.7s，
  交互可接受；逐条 Fx 是每条一次清+合成，秒级卡死。
  ⑤**绘图区底色盖掉 option.backgroundColor**：DrawMap 用主题白
  填绘图区，深底地图（flights 的 #003 海面）只剩标题条是深色；
  现 option.backgroundColor 声明时优先。+ geo.itemStyle.color
  按 areaColor 别名解析（ECharts2 遗留键，官方 Flights 写的就是
  color:"#005"，旧解析丢掉后大陆落到默认浅蓝 ramp）。
  ⑥**lines large 模式跳过事件层**（声明 large 且条数超
  largeThreshold，官方 zrender 语义）：3.2 万条 × 每段 DistToSeg
  每帧纯浪费，命中豁免与官方一致。
  回归：全量 sweep FLAG 169（HEAD 基线 182，A/B 差集单侧——
  消失的 13 个全是宣告 backgroundColor 的地图类 demo
  （scatter-map/lines-airline/geo-lines/scatter-world-population
  等），深色海面使 EDGE-CLIP/NO-XAXIS 启发式不再误报；
  scatter-map 抽查官方深色观感、内容完整；**0 新增**）。
  实机：lines-airline 深海面/深大陆/黄绿航线密度发光与官方同构，
  geo-lines 迁徙航线（curveness+effectScatter+深底）完整。
* **ZanDb 三短板收账：索引范围查询 + 免树聚合 + 增量归并（2026-09-14）**：
  按短板评审结论顺序收掉三项。①`FindIdsByFieldRange(field, lo, hi)`
  ——索引就绪走键序范围扫（覆盖索引零文档读，成本同等值覆盖），
  上界钉 `\t` 后 6 个 '~'（id 段每字符 ≤'~'）：收尽 lo/hi 本值全部
  条目且不放进扩展值（"c1" 不放进 "c10"，'\t'(9)<'0'(48)）；无索引
  回退流式单字段扫描；比较语义 = AsString 形态序数（数字按十进制
  文本，"9">"20"，文档写明）；无界端点用空串。②类型化免树层：
  `FieldNum`（varint 3/8/9 直接还原、文本数值 4 解析、字符串/布尔
  不做隐式强制）/`FieldText`（AsSame 归一化文本，免 JsonValue），
  `ScanNumField`/`ScanStrField` 免树投影、`SumField`/`CountGroups`
  流式聚合；对拍 SQLite：类型化投影差距 2.3→1.7 倍，SUM ~10 倍
  （已贴游标地板，与 Count 同量级——瓶颈是扫描机器非走读），
  GROUP BY 33M vs 0.79M（SQLite 走索引计数不碰行，模型差异如实
  记录）。③`MergeSomeSegments(k)` 增量归并：只并最老 k 段，写阻塞
  被归并集封顶，反复调用收敛单段；索引原地改指——只动指向归并集
  的键，「索引指向未归并段」的键不写入新段（重开恢复按清单序后
  扫覆盖才不会让旧版本压住未归并段的新版本，这是正确性关键），
  段清单/段列表按数据年龄序维护（新段载最老数据排最前）。
  测试：zandb_idxrange 18 断言、zandb_typed 16 断言、
  zandb_mergeincr 32 断言（未归并段更新赢含重开、墓碑收敛、无事
  可做返 0），金标 zandb_* 15/15、ctest `-R "zandb|arr_lit"` 64/64
  （含泄漏/ARC 孪生）。教训再钉一次：测试断言先核对手算值（本轮
  三处算术错全在测试侧，实现零缺陷）；Zan 无 `List.RemoveRange`
  /方法内局部函数，按序重建列表/静态助手替代。
* **ZanDb 聚合补全 + 自动归并（2026-09-14，"继续"批次）**：
  短板收账的两项后续。①类型化聚合补全：`MinField`/`MaxField`
  （out long + bool，无数值文档 false）、`AvgField`（一遍同时累计
  和与计数）、`FindTopIds(field, n)`（有界最小堆 TopN——堆只驻留
  n 项内存 O(n)，值降序、等值 id 小者先，排行榜免全排序）。
  ②`SetAutoMerge(atSegs, mergeK)`：固化新段收尾时段数达
  atSegs 就地归并最老 mergeK 段；非法参数（mergeK<2、
  atSegs<mergeK+1）静默拒绝，默认关闭；段数稳态
  [atSegs-mergeK+1, atSegs] 振荡不无界增长；固化只在 Commit 尾部
  触发（txDepth==0），MergeSomeSegments 守卫只是双保险。
  两个新坑入账：**闭包捕获 out 参数不回传**（捕获的是副本，
  MinField 首版把 out 变量直接在 delegate 里累加、调用方永远拿到
  0——out 参数先落局部变量、扫完再写回）；**堆比较器方向**
  （worst-at-top 最小堆的上滤是"父优于子才换"、下滤找"最差者
  居上"，首版按 max-heap 写、替换分支永不触发，前 n 个文档原样
  留堆——只在小 n+多文档场景暴露，全量收集路径反而测不出）。
  测试：zandb_typed 16→33 断言（含平局定序/堆替换路径/超 n 全
  收集/重开跟随）、zandb_mergeincr 32→46 断言（非法参数拒绝、
  开启段数稳态、关闭恢复无界、重开一致），金标 zandb_* 15/15、
  ctest `-R "zandb|arr_lit"` 64/64。
  顺带修一个真 bug：`ZanStore.Open` 拿独占锁失败时没关刚打开的
  日志句柄就 return——被拒绝的 Open 把句柄泄漏到进程结束，Windows
  上该库文件从此删不掉（zandb_lock 测试清理后根目录残留
  `zandb_lock_tmp.zdb` 暴露）。失败路径补 `s.log.Close()`；另把
  zandb_docs/zandb_fuzz 的段清理循环上限提到 40（分别只清 12/24，
  段号超限即残留根目录）。金标重扫 15/15、ctest 64/64 复验。
* **ZanDb 块级聚合内核 + 覆盖索引 GROUP BY（2026-09-14，"继续"批次②）**：
  上批基准结论「剩余地板是扫描游标与每行委托调用」的落地。①
  `ZanStore.ScanKernel(seg, startKey, endKey, visit)`：单段稳态下块
  缓冲读入后循环裸解析记录（ByteBuffer 新增 `PeekU8`/`ReadVarIntAt`
  /`CmpRangeRaw` 裸原语），把（基址+偏移+长度）直接交给回调——不经
  SegCursor/ScanRow 对象、零字符串物化；op=2 段内墓碑跳过、未固化
  墓碑逐键哈希（罕见路径）、首条越过上界整段止步、回调 false 早停。
  `TryScanKernel` 非单段稳态（多段/memtable 未固化数据）返回 -1 由
  调用方退权威路径。②`Count`/`CountRange` 直数、`SumField`、
  `MinField`/`MaxField`/`AvgField` 接内核：`RecAccNum` 裸走读对
  kid==want 的数值标签（3/8/9）直接累加进 `NumAcc`；回调无法「继续
  扫剩下的行」，权威回退以「已聚合到 id-1」为界、剩余行（含当行）
  从 `ScanRows(DocKey(id))` 续扫，两段结果无缝拼接（共享骨架
  `NumAggregate`）。③`CountGroups` 索引就绪时走**覆盖索引路径**：
  条目键序 =（值, id）序，扫索引前缀范围、按 `\t` 切段计数，零文档
  读。测试新增 zandb_kernel 28 断言（单段基准/固化墓碑/未固化回退/
  转义回退/文本数值回退/重开/索引分组对拍扫描分组）。
  **内核走读两个真 bug**（mergeincr 金标 sum 三断言暴露，全修）：
  (a) `RecAccNum` 预读 tag 后调 `SkipValRaw`，而后者按「p 指向
  tag」契约重新读 tag——同一 tag 被消费两次，字符串字段后的
  kid/tag 全线错位，且最终仍 return true（聚合静默为 0）。修法 =
  tag 只消费一次：want 命中分支自己读，跳过分支由 SkipValRaw 读
  （与 FieldNum→SkipValS 契约一致）。(b) 含 0xFF 的记录是转义态
  （0x00→FF 01、0xFF→FF 02）：v=255 的 varint 编码 FF 01 转义成
  FF 02 01 后，裸走读把 FF 02 当 varint 读出 383（多 128）——
  RecAccNum 开头 memchr 检出 0xFF 即回退权威走读（FieldNum 有
  UnescapeRecord 先行）。教训：裸走读与字符串权威走读并存时，
  「哪些字节形态表示原始载荷」必须逐条对齐（转义态、文本数值、
  容器——三处都须显式让位）；tag 的消费权唯一化是解析器组合的
  通用纪律。金标 zandb_* 16/16、ctest `-R "zandb"` 64/64。
  顺带修 CMake 配置期雷：conformance_arc_net_publish 由
  run_arcnet.cmake 手工登记（fcf56e0b 起），但四个 conformance GLOB
  孪生档也按 `*.zan` 把它 add_test——任何重配（加新测试文件必然
  触发）都撞名 `add_test given test NAME which already exists` 配置
  失败。四个 GLOB 循环各补 `arc_net_publish` 跳过守卫。
* **ZanDb 内核跨度整读 + 覆盖索引/无索引过滤接裸走读（2026-09-14，"继续"批次③）**：
  「消除每块/每条目的系统调用与物化」三件套。①`ScanKernel` 块跨度
  整读：段内块物理连续、记录解析不依赖块边界，`LoadBlockSpan` 一次
  ReadAt 读 256KB（`KERNEL_SPAN_BYTES`）并逐块 CRC（NativeMemory
  .Crc32 直算缓冲子区间，不复制）——把 16 记录/430B 小块的逐块
  syscall 摊成每跨度一次。②`ScanKeyKernel`/`TryScanKeyKernel` 键只
  读内核：覆盖索引路径（条目键自带值段+id）零 KvEntry 字符串物化，
  `CountGroups` 同组连片 memcmp 判同、组值只在开新组物化一次。
  ③无索引过滤接行内核：`FieldEqualsPtr` 裸走读（tag 5 memcmp、
  tag 3/8/9 整数比较、tag 1/2 布尔字面；0xFF/文本数值/容器回退权
  威），`EqFastReject` 针线否证先行——字符串等值 25M docs/s。
  **一个真 bug**：内核循环缺下界判定——稀疏索引只保证「块首 ≤
  startKey」，定位尾块里 < startKey 的前缀键会漏进回调；i: 范围
  扫被定位块尾部的 c: 文档键污染，CountGroups 冒出空组（基准
  groups=17 sum=200160 暴露，应 16/200000）。修复 = 循环内
  `CmpRangeRaw(key, startKey) < 0` 即跳过（含 op=2 变长跳）。
  **基准口径修正**：`zandb_vs_sqlite` 归并前 memtable 有残留（索引
  批构建的最后几批没到阈值），全部查询退化到归并扫描慢路径——
  之前发布的 count/sum/group 数字全是慢路径口径。`ZanDatabase
  .Flush()` 暴露 `FlushToSegment`，归并前先固化；真稳态（20000
  文档）：count 1.4M→19M docs/s（反超 SQLite 300 倍）、sum 1.3M→
  8.9M（差距 10x→1.25x）、group by 1.2M→12.4M（46x→2.7x）、字符
  串等值过滤 1.1M→25M。教训：微基准先确认走了哪条路径（SegCount
  与 memtable 状态打印出来再计时）；聚合正确性断言要带「组数+组
  计数总和」双校验。金标 zandb_* 16/16、ctest zandb 64/64。
* **地图区域拼接接缝闭账（2026-09-14，用户实机审查批次③）**：
  map-usa/world 放大后相邻区域边界透出背景色发丝缝。SSR oracle
  （`_scratch/echoracle/mapborder.js`）实证官方语义：MapSeries/
  GeoModel defaultOption `itemStyle.borderWidth 0.5 +
  borderColor neutral30 #b7b9be`，每个 region path fill+stroke
  同帧落盘（MapDraw.getFixedItemStyle → el.setStyle）——相邻区域
  共享边各描一次天然密封。引擎根因：从未消费官方缺省描边，靠
  「同色描边走两遍」的 workaround（画区域自己的填充色）压缝，AA
  合成后共享边单像素仍透背景。修法三条车道 + stroke-on-top：
  ①`ChartSeries.mapBorderW/mapBorderC` 专用哨兵字段（-1 未声明 /
  显式 0 = 关边框是合法值，`ChartItemStateStyle.borderWidth` 缺省
  0 与显式 0 无法区分，故不入 itemStyle）；②解析三路：用户
  series.map.itemStyle 直接收、`geoIndex` 挂载系列取宿主 geo 的
  itemStyle（官方 getHostGeoModel 语义）、geo 合成的 isGeoBase
  系列收 `geo.itemStyle.borderColor/borderWidth`（Double 取回
  保 0.5 这类小数与显式 0）；③渲染 fill 全画完 + 洞底补填后
  再整幅描边一遍（zrender 同 path 先 fill 后 stroke，描边提前
  会被后画的填充盖掉——首版踩过），未声明落官方 0.5px 取整 1px
  网格 #b7b9be；`PolyOutlineW` 带宽轮廓新助手；旧双描 workaround
  退役。指纹 `FingerprintCore` 折叠 mapBorderC/W 保快照缓存正确
  失效。验证：map-usa 边框像素 0→数千、Nebraska/Kansas 等相邻州
  直接 fill→fill 过渡无白缝、接缝启发式 3448→219（余为标签文字
  AA 边缘）；全量 sweep 345 张 FLAG 169 与基线持平、17 个 map/geo
  demo 单侧 0 新增；map-HK/geo-choropleth-scatter/scatter-map/
  heatmap-map/map-iceland-pie/map-bar-morph 观感与官方一致。
