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
* **A2-3** [ ] 变参：加 `[DllImport(..., Variadic = true)]`。（`params` 已支持，属 C# 级变参，与 FFI 变参是两回事。）
* **A2-4** 🟡 半边完成（2026-08-08 复核）：`signext`/`zeroext` 已按声明类型自动贴 LLVM 属性
  （`irgen.c:419`、`irgen_abi.c:348/350`，commit `e6b01d53`）；**仍缺**：用户可见的
  `stdcall` / `CallConv` 调用约定属性（compiler 内 `stdcall` / `CallConv` 出现 0 次）。
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
  统一的 attach/detach 钩子；GUI 回调线程也还没有真正调用 `zan_thread_detach()`。

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

**选择器**：类型名、`.class`、`#id`，各自可带 `:state` 后缀（hover/active/focus/disabled），
逗号列表。**没有**后代/子/兄弟组合器、属性选择器、`*`、伪元素、层叠优先级
（`Apply` 按 类型 → `.class` → `#id` → 带状态 固定顺序覆盖）。

**at-rule**：只有 `:root` 自定义属性 + `var(--x)`。没有 `@media`/`@import`/`@font-face`；
`@keyframes` 不解析，`animation` 只能引用内置 8 条曲线。

**属性**（约 90 个）：盒模型、背景（含 linear-gradient 2~3 停靠点）、文本、flex 子集、
定位、效果（box-shadow 含 inset/opacity/filter/transform/transition 三件套）、
overflow/visibility/cursor/accent-color。

**取值**：颜色齐全。长度**只有像素**（`12px` = 12、`1.5rem` = 1、`50%` 被当 50；
小数只在 opacity/scale/alpha 有效）。没有 `calc()`、没有 em/rem/vh/vw。

---

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
| A43-B7 | 扩展方法 `this int v` 形参 | 15 | ✅ 已实现（`this int v` + `find_extension_method` 全链路；`tests/conformance/cs_b07_ext_method.zan`）。数字字面量接收者需写 `(5).Twice()`（`5.` 会被词法当作 float，与 C# 一致） |
| A43-B8 | 交错数组 `int[][]`、多维数组 `int[,]` | 24,25 | ✅ 已修（2026-08-08，`cs_b08_arrays.zan`）。`int[][]`（数组的数组）、`int[,]`/`int[,,]`（rank-N 矩形数组）、混合 `int[][,]`（rank-2 数组的 1D 数组）与 `int[,][]`，按 C# 最左 rank 规格最外层折叠；`.Length` 返回元素总数、`.GetLength(d)` 返回第 d 维长度；多下标 `m[i,j]` 按行优先扁平化越界检查；`new int[2,2]{{1,2},{3,4}}` 带维度初始化。**实现**：`type_ref` 加 `array_rank`+`array_element`（parse_type_ref 从右到左折叠最多 16 个 rank 规格）、`new_expr` 加 `array_rank`（sized 层作为最外层包装）、`AST_INDEX.extra` 多下标列表（首下标留在原字段，1D 路径不变）；binder 构造嵌套 TYPE_ARRAY（`binder_type_equal`/`checker_type_equal`/`types_concrete_equal`/`types_equal`/`tuple_sig_type`/`render_type_full` 全部比较 rank）；`zan_mdarray_alloc` 新布局 raw+0=count、raw+8=rank、raw+16=dims[0..rank-1]、data 紧跟其后，`zan_array_len`/`.Length` 可直接复用；`emit_mdarray_elem_ptr` 行优先扁平化，rank>1 的 new/store/incdec/read 路径全部分支到它。leakcheck `-- leak-clean` |
| A43-B9 | 索引器 `public int this[int i]` | 37 | ✅ 已修（`cs_b09_indexer.zan`）。`parser.c:3351` 专解析 `this[...] { get/set }` 与 `=> expr`，降级为 `op_index`/`op_index_set` 协议。**2026-08-27 复核纠正**：清单原写 parser 报错，已过时 |
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
* **A58 · 全量收口执行计划** 🟡（2026-08-27 定序）：批 1（封死静默产错码，含 1.5 有诊断即停 codegen）与批 2（验证基建：arcguard 435 项档、sanitizer 扩容、前端 fuzz 扩容抓出 41KB lexer 栈帧真 bug）完成；批 3-6 未动。正文见下。
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
* **gallery 目录外置化（assets/gallery.json）** ✅（2026-08-31）：84 组件 270 演示卡外置，gui_gallery.zan 1.09→0.67MB。**已知问题**：build_gallery.ps1 手挑 stdlib 文件列表被并行 stdlib 新依赖（System.Json/Mqtt）建挂，待依赖链收口后补文件或改 `--auto-stdlib`。
* **SelectBox 弹层选项选不中（基线命中）** ✅（2026-08-29，commit 5d11b650）：`HitTestFrom(px,py,from)` 基线命中原语，弹层只匹配自己 blocker 之后注册的区域；无头回归 selectbox_popup_hit_test + 有头探针验证。
* **Gui/Chart 后续（2.2.7 对齐轮遗留）**：timeline 子系统、connect() 多图联动 ✅（2026-08-30，ECharts 134/134 全 ✅）。**架构注记 A · 共享坐标系共存层** [ ] 仍开放：跨图族混搭需抽共享坐标系/布局层供多渲染器叠画，重构面大于收益，待真实需求立项。

## A78-3 · 新发现预置 bug：≥64 个泛型实例化堆损坏 —— ⏳ 已登记未修

- **现象**：单模块内同一泛型类/方法实例化到 **63 个通过、64 个起堆损坏**
  （编译器宿主段错误，非确定性、ASLR 相关）。HEAD 编译器与改动后同样炸。
- **探针**：`_scratch/pb64.zan`（63/64 实例化二分）。方向：泛型实例表
  容量/索引 63 边界（某 64 槽表零起步减一之类）。
- **处置**：按 AGENTS.md 规则 10 登记，待专项修复；不绕过。

## A70 · `Thread.Start` 不能携带实例方法组 —— ⏳ 已登记，暂缓修复（2026-08-29）

- **症状**：`Thread.Start(job.Run)`（实例方法组）启动瞬间 SEGFAULT，无诊断；静态方法组同形写法正常（`_scratch/threadjob.zan`）。
- **根因**：委托 ABI 里实例方法组是带 ZAN_CLOSURE_TAG 的堆记录指针（bit0=1），`rt_sync.c` 的 `zan_thread_start` 把参数原样当裸 `void(*)()` 调用即崩；checker 不拦。
- **当前回避（stdlib 两处同构）**：`ImageHttp.zan`（下载）与 `Upload.zan`（上传）线程入口用静态方法组 + 队列传实例。
- **修法建议**：① 运行时 `zan_thread_start` 检查 bit0 解包记录经 trampoline 调用（根治）；② checker 在实参转换点拒绝"实例方法组 → ThreadStart"。

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
| 3.1 | **A52-7** EH 线程表 1024 硬顶动态化；GUI/SDL/外部回调线程接上 `zan_thread_detach()`（A4-2 剩余） | `thread_eh_slots` 扩到 >1024 并发仍跑完；峰值内存不回退 |
| 3.2 | **A52-8** 库内十余处 `abort()` 改为可注册回调 + 错误码出口（OOM、契约违反、slab 一致性） | 新增"宿主接管 OOM 后自行退出"用例；无回调时行为与今天一致 |
| 3.3 | **A52-5** `--publish` 保留低成本安全网（~~需先定性能预算~~ **已实测，见下**） | publish 版本能报 over-release；约定阈值内不退化 |
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
| 4.1 | **A57 遗留** ARC 引用环：`Control.OnChildChanged` 虚钩子取代"子控件事件上挂捕获 this 的闭包"，并全库扫同模式 | `leakcheck_checkbox_group` 转绿；扫描结果登记 |
| 4.2 | 闭包瘦身第二批：`Automation`(61) / `Management`(68) / `Windows`(71) 去 Threading+Diagnostics 税 | 三者文件数各降一档；standard 全绿 |

---

# 遗留专项（未修、需协调或设计决定）

* **leakcheck_checkbox_group 引用环**（A57 顺带定位，未修）：`FormBuilder.zan` 的 `MakeItem` 每个选项泄漏一个闭包（15 选项 15 泄漏）——组持有子 Checkbox、子的 `Change` 事件表持有捕获了组的闭包（ARC 不回收环）；同源 `Gui/Event.zan:142` 的 490 个 `List<Action>`。可选修法：`Control.OnChildChanged` 虚钩子由子控件经 parent 反向通知、彻底不建闭包；会动 `Control.zan`（= A58 4.1），与 GUI 并行编辑协调后做。
* **A64b leakcheck 仍红（待查）**：http_forwarder_keepalive / http_forwarder_tunnel（forwarder 自身协程收尾，文件在另一工作流 WIP）、reflect_members / server_mvc_timezone / sqlserver_tds / tdengine_rest；`db_error_throw` 的 leakcheck 在编译器重链接后 5 对象漂移（DbParams×2 + Model.IsApplied×2 仍可达），通过/失败随构建指纹翻转，待根因。
* **A76 crasher（应用层/悬垂，另行处理）**：① `TlsStream.Close`（TlsStream.zan:909）`SSL_free` 后 wbio 悬垂访问崩溃——SSL_set_bio 转移了 BIO 所有权但 Zan 侧 string 字段仍持指针，需按 Ownership 约定重审 Setup/Close 的 BIO 生命周期；② sqlserver_tds `Program_Live$resume`（pool.Close 场景）协程 resume 路径 null 实例字段，非确定性复现。
* **A67 字节×码点语义冲突（语言级，待定夺）**：string 按字节（索引/NUL 守卫/Length；FbReader/TDS codec 按字节索引），char 按码点（拼接/打印 UTF-8 编码），`s + s[i]` 对非 ASCII 必然膨胀；两侧皆有意设计，修复任何一侧破坏面都大。语言级出路（Rune/ByteAt API、解码式索引等）留待专项。
* **A56 未做（下一批）**：`System.Automation`(61) / `System.Management`(68) / `System.Windows`(71) 仍各带 Threading + Diagnostics 税（= A58 4.2）；`Automation/Window.zan` 与 `Management/Cpu.zan` 疑似只为 Thread.Sleep 付全价，待核。
* **A71 后续路线（按需加载未完部分）**：① globaldce 钉死源逐个核（A75 已退役三表，重钉面需复量）；② auto-embed 泛化——`stdlib/<Ns>/data/` 自动烤进镜像的通用机制（skins 手工、icons 已接）。
* **体积优化线剩余候选（边际收益小）**：desc 记录瘦身、tynames 列表共享；PIC 与 ARC 冗余对两条杠杆已实测证伪。A74 归因的「空窗 ~72% .text 可去死」随 A75 去钉后需复量。
* **A44(genmeta) 备注（已部分过时）**：`build\ZanIDE.exe` 12.23MB vs dist 快照 8.2MB 的增长未追查（2026-08-24 记录；其后 A71-A75 已大幅优化发布体积，数字需重测）。
* **determinism/leakcheck_checkbox_group、determinism_bytebuffer_bounds**：`--emit-ir` 宿主崩溃，zanc_clean（HEAD 基线构建）同样复现，属在途既有问题，待查。

