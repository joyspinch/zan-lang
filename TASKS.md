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
focus/focus-visible/disabled/selected/checked）、结构性伪类、`[class*="frag"]`
属性选择器、`:not(...)/:is(...)/:where(...)`、`*`；逗号列表。**树上下文**：
组合器与结构性伪类只在 retained 树（`Control.RenderTree`）内命中，无树上下文
的即时解析路径它们不命中（Lint 汇总报告条数）；特异性按复合块累加。
`:has()` 与 `::before/::after` 判 never（Lint 点名）。层叠固定顺序：类型 →
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

* **WSL soak：mallocng 高压下概率崩溃（09-09 WSL 稳定性测试定位，待根因）**：server-game 模板 musl 静态链（`--target linux-musl`，cross 链路带 zanrt_mem.o + --wrap=malloc/calloc/realloc，小对象走 zan 槽位分配器、>2048B 落 musl mallocng）在 soak 驱动 ~6700 req/s 混合压（HTTP /api/auth/login + /register 表单 + TCP 网关 register/login 6 worker）约 11 分钟、累计 ~22 万请求后 SIGSEGV：faulting ip 落 musl `__malloc_allzerop`（mallocng 元数据一致性检查内），addr=0x85a414e5d（野值），无 Zan 侧栈帧（FP 链断），日志无前置异常。当时 RSS≈74MB、CPU 30-57%。归因：复跑 24 轮同形状压测（每轮 360 注册/登录×6 并发、1080 TCP op）+ 2400 串行化 admin 登录 + 1600 短连接 + 200 空闲连接悬挂，全部稳定（RSS 爬升到 82-86MB 后平台化，TokenBox 10 天 TTL 不回收是预期），未能复现——首轮崩溃发生时段恰是压测速率最高（6700/s，是复跑的 ~5 倍）+ register 落库风暴，嫌疑集中在 mallocng 多线程竞争（单线程 M:1 调度器但 runtime 侧 CreateThread/blocking-worker 仍并发 malloc）或某个高频路径的堆破坏只在特定节奏触发。修复方向：① musl mallocng 崩溃面用 musl 自己的 `malloc_usable_size`/调试配置先抓现行（`MALLOC_ARENA` 不适用，考虑换 `--target linux-x64` glibc 版对照复跑）；② zanrt_mem.o 把 >2048B 的中块也纳入槽位管理（现 4096 的 byte[]/大 frame 全落 mallocng）；③ zan 内存审计工具（arc_guard/quarantine）在 musl 静态链下开 `-DZAN_DEBUG` 复跑抓 first-fault。注：HTTP 安全扫描发现模板层 MEDIUM（/api/auth/login 无速率限制/锁定，12 连败无退避）与 LOW（缺 X-Frame-Options/CSP/X-Content-Type-Options/HSTS 四个防御头，Server banner 泄露版本）——模板/框架层决策，另行处理；TCP 网关对裸连接无帧长上限（9KB op 名照收）与无握手超时（半开连接 8s 不超时），属网关加固项。
* **leakcheck_checkbox_group 引用环**（A57 顺带定位，未修）：`FormBuilder.zan` 的 `MakeItem` 每个选项泄漏一个闭包（15 选项 15 泄漏）——组持有子 Checkbox、子的 `Change` 事件表持有捕获了组的闭包（ARC 不回收环）；同源 `Gui/Event.zan:142` 的 490 个 `List<Action>`。可选修法：`Control.OnChildChanged` 虚钩子由子控件经 parent 反向通知、彻底不建闭包；会动 `Control.zan`（= A58 4.1），与 GUI 并行编辑协调后做。
* **A64b leakcheck 仍红（待查）**：http_forwarder_keepalive / http_forwarder_tunnel（forwarder 自身协程收尾，文件在另一工作流 WIP）、reflect_members / server_mvc_timezone / sqlserver_tds / tdengine_rest；`db_error_throw` 的 leakcheck 在编译器重链接后 5 对象漂移（DbParams×2 + Model.IsApplied×2 仍可达），通过/失败随构建指纹翻转，待根因。
* **A76 crasher（应用层/悬垂，另行处理）**：① `TlsStream.Close`（TlsStream.zan:909）`SSL_free` 后 wbio 悬垂访问崩溃——SSL_set_bio 转移了 BIO 所有权但 Zan 侧 string 字段仍持指针，需按 Ownership 约定重审 Setup/Close 的 BIO 生命周期；② sqlserver_tds `Program_Live$resume`（pool.Close 场景）协程 resume 路径 null 实例字段，非确定性复现。
* **Windows IOCP blocking-worker 唤醒包偶发丢失（09-09 全链路稳定性测试定位，先于本次会话，待根因）**：`conformance_http_forwarder_keepalive` / `http_forwarder_stream` 在 Windows 上概率性（约 1/6~1/2 次）中途静默截断（exit=0、金样 18 行只出 8/11/14 行不等），或转发器 `ConnectAsync` 的 blocking-worker 结果永不送达（实测 1/6：`NativeConnectSockAddr` 经 `zan_rt_blocking_co` 提交后协程永不恢复，`DBG connected-up` 永不打印，整程序 10s 后靠上游 idle 超时才走完）；带 ZAN_IO_TRACE 的坏例 trace 都停在 `poll removed=1` 后不再有任何 reactor 活动——怀疑 DNS/完成唤醒包（`PostQueuedCompletionStatus(key=-2, lpOverlapped=NULL)` → `zan_io_poll` 的 `dns_drain` 路径）与 GQCS 超时路径竞态丢包。对照实验：纯串行 connect 150/150、单 RecvOv 挂起 + connect 150/150、双 AcceptEx + RecvOv + connect 200/200 都不复现，坏例只在 HttpForwarder 完整形状（2 监听 AcceptEx + 下游 RecvOv + 上游 blocking connect + 定时器轮询混跑）出现。归因证据：rt_io.c 最后一次改动是 09-05 的 0e55108a（本会话两提交 454c72f0/fc97defd 未触碰 rt_io/rt_co/DNS 唤醒），复现所用 build/zanc.exe 是 09:35 编译——早于本会话首个提交 12:54，故非本次改动引入。修复方向：给 `dns_wake_notify` 的包加序号/与 `g_blocking_done` 快照配对，或 blocking-worker 完成路径改走与 IO op 同形的 real-overlapped 包，消除 NULL-overlapped 特判路径。受影响测试需先容忍重跑（ctest RESOURCE_LOCK 已防同源并发，但单例本身翻车）。
* **A67 字节×码点语义冲突（语言级，待定夺）**：string 按字节（索引/NUL 守卫/Length；FbReader/TDS codec 按字节索引），char 按码点（拼接/打印 UTF-8 编码），`s + s[i]` 对非 ASCII 必然膨胀；两侧皆有意设计，修复任何一侧破坏面都大。语言级出路（Rune/ByteAt API、解码式索引等）留待专项。
* **A56 未做（下一批）**：`System.Automation`(61) / `System.Management`(68) / `System.Windows`(71) 仍各带 Threading + Diagnostics 税（= A58 4.2）；`Automation/Window.zan` 与 `Management/Cpu.zan` 疑似只为 Thread.Sleep 付全价，待核。
* **A71 后续路线（按需加载未完部分）**：① globaldce 钉死源逐个核（A75 已退役三表，重钉面需复量）；② auto-embed 泛化——`stdlib/<Ns>/data/` 自动烤进镜像的通用机制（skins 手工、icons 已接）。
* **体积优化线剩余候选（边际收益小）**：desc 记录瘦身、tynames 列表共享；PIC 与 ARC 冗余对两条杠杆已实测证伪。A74 归因的「空窗 ~72% .text 可去死」随 A75 去钉后需复量。
* **A44(genmeta) 备注（已部分过时）**：`build\ZanIDE.exe` 12.23MB vs dist 快照 8.2MB 的增长未追查（2026-08-24 记录；其后 A71-A75 已大幅优化发布体积，数字需重测）。
* **determinism/leakcheck_checkbox_group、determinism_bytebuffer_bounds**：`--emit-ir` 宿主崩溃，zanc_clean（HEAD 基线构建）同样复现，属在途既有问题，待查。

* **A80 zanc 退出段错误（已修复：LLVMContext 双重拆除，保留 ctx 绕开）**（2026-08-31）：大型 GUI 模块（--auto-stdlib 拉入 Gui 全量，约 330 文件）编译完成后进程退出时段错误，`Compiled 330 files` 之后；gui-empty 模板对（zform+zan）符号化构建下 100% 复现，console 小程序不复现；fd19375a 记录的「链接完成后退出时段错误」即此。cdb（`_NO_DEBUG_HEAP=1`，debug heap 完全掩盖此崩溃）实锤根因：CRT 退出表里有一条 LLVM 静态注册的析构 thunk（`zanc!LLVMStopMultithreaded+0x10: lea rcx,[静态 LLVMContext]; jmp llvm::LLVMContext::~LLVMContext`，静态本体无符号、近邻符号 `_OptionsStorage+0x28`，pImpl 为垃圾/悬垂堆），main() 里 `LLVMContextDispose` 释放过整个堆图后，退出表 dtor 对同图用户再走 `~LLVMContextImpl → User::dropAllReferences` 写 `mov [r9],r8` 到 MEM_RESERVE 未提交页 → c0000005。实证链：`LLVMShutdown()` 清 ManagedStatic 注册表后仍 12/12 崩（崩溃者不是 ManagedStatic 形态）；对照实验 8/8 崩 vs 保留 ctx 20/20 零崩，`ASLR/堆布局`、模板大小只是触发概率因子。修复（irgen.c zan_irgen_destroy）：跳过 `LLVMContextDispose`、保留 context 存活到进程退出（有界泄漏、编译器进程短命），main() 成功路径尾部 `LLVMShutdown()` 收 ManagedStatic。回归：gui-empty 对 20/20、gui-components/dashboard/free/hmi/ribbon/sidebar 各 1 次、模板对全零退出；cdb 复跑无 AV。ASan 零复现疑因分配器差异不暴露未提交页写入；根治需 LLVM 侧退出表禁注册（上游 C++ 语义，超出本仓库），现状即最稳工程解。先于本项发现的 **确定性** c00000fd 家族（emit_stmt 单帧 ~6KB × stdlib 深 else-if 链 Theme.SetToken(100)/IconVector.Draw(256) 超 1MB 默认栈，zform 模板 100% 复现）已以 zanc 链接选项 /STACK:32MB 修复，与本 UAF 无关。

* **A81 字节通道 redirect 接入 + 跨跳共享 total deadline（已完成，4f9ab3f9 的收尾批）**（2026-08-31）：4f9ab3f9 只给文本通道（RequestAsync）做了 redirect 策略与逐跳预算，三个字节通道仍是缺口——SendBytesAsync 原样返回 3xx（后续 GetAsync/Binary 消费方会把 3xx HTML 当正文）、UploadFileBytesAsync 与两个文件下载对 3xx 行为不一致。本批三面收口（全部在 HttpClient.zan）：① **跨跳共享总预算**：`totalDeadlineUs` 字段（Socket.NativeMonotonicUs() 时基）+ `ArmTotalBudget()`（幂等武装，RequestAsync/SendBytesAsync/UploadFileBytesAsync/两个 Download 入口调用；无 policy 客户端保持旧"每跳各自 timeout"语义），`BudgetMin`/`TotalBudget` 把每个阶段预算钳到 total 剩余时间，重定向派生的下一跳客户端继承同一 `totalDeadlineUs`——多跳链、慢速滴流都无法把一次外呼拖过 policy total。② **字节通道跟随**：`HttpBytesDriver` async delegate + `FollowBytesRedirects` 循环（Location CRLF 注入显式拒绝、ExternalTarget 严格解析 + canonical 防环、301/302/303 POST→GET 且实体丢弃——multipart 无法降级重放、307/308 非 GET/HEAD 拒绝重放、跨 origin 清 Authorization/Cookie/Proxy-Authorization），SendBytesAsync 拆出 `SendBytesOnceAsync` 静态驱动、上传通道接 `UploadBytesOnceAsync`；两个下载方法（DownloadRange/BinaryToFileAsync）因哨兵返回值（-1/-2，不抛异常）与本地状态（have/progressFile）无法走 delegate 驱动，改为 `*OnceAsync` 单次尝试体 + 3xx 递归下一跳（Range 头由递归体从磁盘重建，防环 visited 列表跨跳共享）。③ **新增 HeadHeader**：下载通道头部与正文共享一次手写读取（不走 HttpResponse.Parse），重定向 Location 由该 helper 按行首大小写不敏感解析。新增 tests/conformance/http_bytes_redirect（9 场景：字节 GET 跟随+跨 origin 清敏感头、POST 301→空 GET、POST 307 拒绝、上传 307 拒绝、二进制下载跟随落盘、无 policy 时 3xx 拒绝落盘且文件不建、下载环拒绝、CRLF guard、Range 下载跟随）。回归：conformance_http_bytes_redirect + http_client_redirect/http_client_keepalive/http_client_timeout/http_client_binary/http_client_cookies/http_framing/http_upload_bytes/download_job_model/policy_external_target/policy_http_client/policy_http_media/sdk_wechat_tenpay/gui_image/gui_upload 全绿（keepalive 曾与并行在途进程挤 120s 超时一次，隔离复跑 3 次均 0.6s 过）。已收尾：RequestAsync 旧路径去重到同一组 helper（5b2e4296 之后的 dedup 提交）——跳数/防环/动作改写/下一跳派生全部走 RedirectWanted/RedirectReplay/RedirectNextClient，附带补齐文本通道 Location CRLF 注入拒绝（字节通道先有，文本通道此前漏了），"unsafe redirect method" 消息统一为 "unsafe redirect replay"。

* **A82 wasm32 后续路线（2026-09-01 盘点，本轮已修链接全断，以下为剩余）**：当前 wasm32（WASI）可编译运行的范围：CLI/计算/字符串/集合 + 文件 IO（探针在 Node WASI 下全链路验证）。剩余缺口按优先级：
  1. **try/catch（wasm-EH 后端）（已完成 2026-09-02）**：wasm32 try/catch/finally/throw 全量落地，编译+链接+Node WASI 运行输出与 native 一致（跨函数 unwind、嵌套 try、typed catch、finally 全验，探针 `_scratch/wasm_try_*.zan`）。实现：irgen.h 增 wasm_lpad_stack/wasm_try_depth/in_wasm_throw_op/wasm_eh_* 字段与 target_is_wasm；irgen.c zan_call2 是唯一调用咽喉点，wasm+try 体内所有 zan_call2 调用改发 invoke（unwind 边指向当前 try 的 catchswitch lpad；文件静态 s_current_irgen 单例喂给它——450+ 调用点零改动；两个豁免：`__zan_eh_state_fast` 纯读永不上抛（invoke 会把 entry 块从中间劈开，后面的 field GEP 全部悬挂在终结符后），helper 函数体（__zan_eh_* out-of-line 构造）借用 builder 时不得引用 main 的 lpad——按「调用块父函数 == lpad 父函数」门控）；irgen_stmt.c AST_TRY_STMT 的 wasm 分支改发 lpad（catchswitch within none → catchpad catch-all → catchret 回 catch 块），try 入口 br 必须在 emit_wasm_lpad 返回后重新定位回入口块（lpad 构造会把 builder 停在 catchpad 块尾）；throw 走 throw_unwind/propagate_tail 同一 wasm 分支：同函数内有活动 pad → invoke `__cxa_throw(obj,null,null)`（LLVM 禁止 invoke intrinsic，而 WasmEHPrepare 并不重写 __cxa_throw——探针证明符号原样保留，所以必须自带定义）；栈内无 pad（异常逃逸本帧）→ 纯 call `llvm.wasm.throw(0, obj)` 内建 intrinsic（直接落 throw 指令，引擎穿透到调用方的 invoke 区域）；`toolchain/wasm32/zanrt_ehtag.o`（mozbuild clang `-fexceptions -mllvm -wasm-enable-eh` 编译，zig 的 clang 无法产出，sanity: `__cpp_exception` tag 由 __builtin_wasm_throw 隐式定义）定义 tag + __cxa_throw 真身，main.c 仅在 wasm_eh_used 时链接它。codegen 三件套缺一不可：LLVMParseCommandLineOptions(["--wasm-enable-eh"])（main() 一进就喂）+ TM feature `+exception-handling,+reference-types` + personality 函数必须叫 `__gxx_wasm_personality_v0`。新增 conformance_try_catch（native 跑语义）+ conformance_try_catch_wasm32（套件内 wasm32 编译+链接断言）。文档 platform-targets.md 同步。
  2. **GUI wasm 后端（大工程）**：stdlib/Gui 三层原生面——`Backend/Native.zan`（55 个 DllImport：user32/gdi32/kernel32/zan_gui）、Win32Shell、`zan_gui` 原生 driver（win/linux/macos 六份，无 wasm 版）——在 WASI 上全数无解。可行路线是新的 **canvas/Web host driver**：Zan GUI 渲染逻辑留在 Zan/C，事件与像素经 WASI host import 桥到浏览器 canvas（类似 emscripten 的 SDL port 或 winit-web）；自绘与布局不动，Backend/Native.zan 按 `#if WASM32` 换导入表。注意 Gui 全量编译本身（--auto-stdlib 拉 330 文件）在 wasm 三元组上没有平台障碍（无 GUI 原生引用即可链接，探针已证）。
  3. **socket-async**：wasm 上无 epoll/kqueue/IOCP；WASI preview 2 的 sockets 提案未成熟。短期维持 main.c 的 wasm_obj_refs_any 拒绝（报错清晰）。
  4. **wasm64**：无任何代码（架构枚举/triple/sysroot 皆无）。IR 天然 i64 所以适配层反而可跳过，但 wasi-libc wasm64 sysroot 与引擎支持太新，排在 wasm32 EH 之后。
  5. **IDE 支持面**：PublishTargetIds（ZanIDE.Workspace.zan:1316）现仅列桌面目标；加 "wasm32" 条目 + Publish 走 zanc --target wasm32（编译链接路径本轮已通）即可让 IDE 发布 wasm。断点调试：zan-dap 走自家 intellisense/调试引擎（无 wasm 感知）；wasm 调试现实路径是 Chrome DevTools 的 DWARF 调试（zanc --publish 已可带调试段）或 source map 级映射，属新专项。

* **A84 Android CLI 目标落地（已完成 2026-09-03）**：`zanc --target android-x64 / android-arm64` 全链路（编译+静态链接+模拟器运行验证）。① crosscomp：`ZAN_OS_ANDROID` 枚举（parse_os 在 linux 之前判 "android"——bionic triple `*-linux-android28` 含 "linux" 字样，顺序反了会被吞）+ 两个 target 条目（API 28：rt_sync.c 的 glob() 需要 bionic 28）；main.c 三处接线：zan_driver_subdir 返回 `android-<arch>`、rt_timer 的 target_rt_sub、同步运行时门（android 与 linux 同待遇，bionic 有 pthread/epoll）；平台宏 ANDROID=1 且 LINUX=1（Android 是 Linux 系，只为差异点留 ANDROID 判据），探针验证 `#if ANDROID` 生效。② main.c android 链接分支（仿 linux-musl 的 `ld.lld -static` 模式）：链接 `toolchain/android-<arch>/` 提交的 NDK sysroot 子集（crtbegin_static.o + crtend_android.o + libc.a/libm.a/libdl.a + libclang_rt.builtins.a，NDK 27 r27c 提取；libc.a 经 `llvm-objcopy --decompress-debug-sections` 整档解压——NDK 的 bionic libc.a 带 zstd 压缩调试段，lld 无 zstd 支持时报 ELFCOMPRESS_ZSTD 拒链；注意必须整档 objcopy，逐成员解压再 llvm-ar 重打包会丢 scudo 弱符号定义导致 `scudo::PageSizeCached` 未定义）。**rt 对象不走 --wrap=malloc**：rt_mem 小对象分配器 wrap bionic 内部 malloc/free 会在 bionic TLS bootstrap 期段错误（musl 无此问题，探针实锤 min 程序 rc=139 vs 去 rt_mem 后 rc=42），android 路径不链 zanrt_mem.o，`--fast-alloc` 在此目标无效。③ rt_sync.c `__ANDROID__` shim：bionic 不实现 shm_open/shm_unlink（`_POSIX_SHARED_MEMORY_OBJECTS = MISSING`），以 `$ZAN_SHM_DIR`（默认 /data/local/tmp，app 嵌入可指向自身 files 目录）下的常规文件兜底同名语义，匿名表 shm_open+即 unlink 的 fd 存活语义保持。④ rt 对象构建：zig cc 无 bionic target，`build_cross_rt.cmd` 增 NDK 块（`ANDROID_NDK` 环境变量或 SDK 下最新 ndk/ 自动探测），NDK clang `-target <arch>-linux-android28 -g0 -fPIC` 编译（rt_io.c 带 `ZAN_IO_STACKLESS_ONLY`，与 linux sysroot 同配方——不带该 flag 的 rt_io.o 引用 rt_sched 的 zan_io_resume，链接全 stdlib 程序时炸 undefined symbol）；check_toolchain_stale.py 登记（runtime 组 4 项 manual + NDK 子集 6×2 项空源 manual）。⑤ CMake：android sysroot 子集 copy_directory 进 cross_sysroots staging；`conformance_try_catch_android`（套件内 --target android-x64 编译+链接断言，sysroot 存在性门控）。E2E：API 35 x86_64 模拟器（AVD zan35，headless）跑 try_catch（typed catch/finally/跨函数 unwind 5 行全对 rc=0）、文件读写、AtomicInt、Thread.Sleep、`#if ANDROID` 宏探针全部通过；net 类探针（HttpClient/Worker）被并行会话在途的 stdlib 改动阻塞（linux-musl 同样类型错误，非 android 特有），threads 闭包探针在 musl 上同崩（探针自身问题，非 android 特有）——两者均另案。arm64 仅链接验证（与 macOS 同策略，手上模拟器为 x86_64）。docs/platform-targets.md 目标表 10→12 项 + Android 小节改写 + §6 顺序更新。GUI/Play Store 打包（gradle 工程 + zan_gui Android 后端）为 A82-2 后续。

* **A86 一键发布 Android APK（已完成 2026-09-03）**：`zanc --publish --target android-x64 --emit-apk app.apk` 一条命令编译→链 libmain.so→打包 SDLActivity 壳→签名，全程无 Android SDK/NDK/gradle/aapt2。① src/compiler/apk.c（新）：自带 zip 写入器（STORED 原生库数据偏移 4 字节对齐；resources.arsc 不压缩存储且对齐——Android 11+ 硬性要求；deflate 条目用手写 stored-block deflate：BTYPE=00 块=头字节+LEN+NLEN(取反)+原文，首版漏 NLEN 被 apksig 抓 "Data malformed"）；**二进制 AXML 字符串池补丁**替代 aapt2——manifest 是预编译模板（toolchain/apk-shell/AndroidManifest.xml.bin，ResStringPool UTF-16LE：type/hdrSize/size/stringCount/styleCount/flags/stringsStart/stylesStart 28 字节头，每条字符串=u16 长度+数据+u16 NUL），按值定位占位串 "dev.zan.app"/"Zan App" 重建池、改根 chunk 文件长度，aapt2 dump badging 验证通过；签名走随包分发的 apksigner.jar + 发现的 Java（JAVA_HOME→PATH；Windows 找不到时经 zan_http_get 自动下载 Adoptium 便携 JRE 到 %USERPROFILE%\.zan，非 Windows 给出明确安装指引）；debug.keystore 缺失时自动 keytool 生成（PKCS12，android/android，CN=Zan_Debug；system() 调 cmd /c 时整条命令要再加一层引号——cmd 的外层引号剥离怪癖）。**打包资产入库 toolchain/apk-shell/**：AndroidManifest.xml.bin（模板）+ resources.arsc（40 字节空表）+ classes.dex（SDL3 Java 侧 94KB，一次性预编译）+ apksigner.jar（1MB）。② main.c 接线：`--emit-apk <file.apk>` 隐含 --emit-lib 且强制 lib_shared（.apk 后缀不得读成静态库）+ android 目标门；打包钩子挂在驱动 bundle 拷贝之后——包名/标签取自输入文件名（dev.zan.<base>，非法字符折叠下划线），extras 从各驱动 .bundle 清单收集 .so 进 lib/<abi>/，打包后删除中间 libmain.so。③ CMake：apk.c 入 zanc 源表；toolchain/apk-shell 随 cross_sysroots copy_directory 暂存到 zanc 旁。④ IDE：PublishTargetIds 增 android-x64/android-arm64（标签 "Android x64/ARM64 (signed APK)"），Publish 步骤生成 `--target android-<abi> --emit-apk publish/android-<abi>/<name>.apk`——发布对话框勾选即出可装 APK。**调试三部曲**（都在 apksigner 的异常里现形）：CD 漏写 csize（"Compressed size mismatch LFH: 2183, CD: 0"）→ stored deflate 漏 NLEN（"Data of entry malformed"）→ 文件首字节不能是对齐垫片（"INSTALL_PARSE_FAILED_NOT_APK: Failed to load asset path"——首条目前不垫，保 'PK' 魔数在偏移 0）。E2E：zanc 一键出的 x64 APK 装进 API 35 模拟器，logcat Running/Finished SDL_main 齐全，窗口像素与探针色三色命中（蓝 236 万 px/绿条 bbox 与窗口布局吻合）；arm64 APK 构建签名同链路（实机验证仍另案）。IDE 全量编译 IDE_BUILD_OK。docs/platform-targets.md 目标表与 Android 小节更新。

* **A85 Android GUI 窗口跑通（已完成 2026-09-03）**：`[DllImport("zan_gui")]` 程序在 android-x64 上 SDLActivity 壳内出窗口、渲染、干净退出，模拟器像素级验证。① zanc android 链接三分支：无 DllImport 维持 `ld.lld -static`（A84 不变）；有 DllImport 驱动库时走 **动态 pie**（`ld.lld -pie` + crtbegin_dynamic.o，`-l<驱动>` 收集自 irgen.extern_lib_count（跳过 zan_win_system_lib），--start-group/-L toolchain 目录闭合，toolchain 增 libc/libm/liblog/libdl.so 四个 NDK stub .so（只承载 DT_NEEDED/符号表让 lld 解析 `-l` 引用，运行期由 APK nativeLibraryDir 的真库绑定）+ SDL_main.o 包装器（`SDL_main(argc,argv){return main(argc,argv);}`——irgen_emit 发的是 `main`，SDLActivity dlopen 后调的是 `SDL_main`）+ crtbegin_dynamic.o；有驱动时不再链 zanrt_* 静态组而链 stub .so 同名动态依赖）。② **`--emit-lib` android 共享库分支**：`zanc --emit-lib --target android-x64 -o libmain.so` → `ld.lld -shared -o libmain.so sys3/SDL_main.o <obj> -L dirs <rt_io/sync/file/embed/timer 对象> -l<驱动> stub .so + libclang_rt.builtins.a`——SDL_Init(SDL_INIT_VIDEO) 无 JVM/Activity 必段错误（控制台直跑 rc=139 实锤），所以 GUI 程序的正确形态就是共享库 + `org.libsdl.app.SDLActivity`（SDL3 AAR classes.jar，manifest meta-data `android.app.lib_name=main`）dlopen。③ gui_runtime.c：原子分层呈现段（GDI 专用）加 `#ifdef _WIN32` 门控——linux/android SDL 后端编译才不引用 GDI 类型。④ stdlib/Gui/drivers/android-{x64,arm64}/：libzan_gui.so（gui_runtime_sdl.c 交叉编译，`zan_gui` manifest 已在）+ libSDL3.so（AAR prefab 逐 ABI 提取——arm64 用 x64 库会被 lld 以 "incompatible with aarch64linux" 拒链）+ zan_gui.bundle（--publish 随程序拷贝）。toolchain/android-<arch>/ 增 6 个文件（gitignore 命中 *.o/*.so 需 add -f；stdlib/Gui/drivers/** 被 gitignore 白名单行覆盖正常 add）。⑤ 无 gradle APK 壳验证链：aapt2 link（Theme.NoTitleBar.Fullscreen）→ d8（android-35 jar）+ python zipfile 注入 classes.dex 与 lib/<abi>/*.so → zipalign → debug.keystore + apksigner。E2E（API 35 x86_64 模拟器）：logcat "Running main function SDL_main from library .../libmain.so" → 窗口循环（create_window/show_window/create_surface/clear/fill_rect/present × 300 帧 16ms 轮询）→ "Finished main function" 干净退出；**像素级确认**：运行中 screencap 与探针画的深蓝底(0x203860)/绿条(0x30C060)/金片(0xF0C040)逐色匹配（蓝 236 万 px 全屏、绿条 bbox x20-299 y226-265 与 320x240 窗口布局吻合）——此前两次截图全黑是因为 60 帧探针 1.05s 即退出、screencap 时序赶不上，本轮加渲染拉长到 300 帧后命中。静态 CLI 回归：try_catch 仍 rc=0 全对。arm64 仅链接验证（与 macOS 同策略）。CMake 无需改动（toolchain/ 整目录 copy_directory 自动带上新文件）。剩余（另案）：IDE Publish 打包进 APK 的完整发布链、arm64 实机、OHOS/iOS。docs/platform-targets.md Android 小节改写。



  ① **layer 键 + 窗口属性全链路（cf141d1a / 10339428）**：SceneElement 增 layer 键（建模、ToJson 写回、IsModeledKey 吸收），SceneDoc 增 OpenLayer/CloseLayer/ToggleLayer/LayerOpen/SetLayersActive（';'-连接的 openLayers 运行集，交互过滤）；窗口属性 winTitle/winResizable/winChrome 贯通 SceneDoc 模型（带默认值 1 的读回）→ SceneDesigner 检查器（标题 Input/可缩放/标题栏 toggle）→ GenScene Run() 投影（SetResizable(false)/SetChromeVisible(false)/CenterWindow|SetWindowPos(winPosX,winPosY)）。**FormDesigner 同病修复**：检查器 winTool 行写了 winTool 键但 GenForm 从不读——GenForm 补读 + App/Window 新增 SetToolWindow/ToolWindow（Win32Shell 实现：WS_EX_TOOLWINDOW 0x80 经 GetWindowLongPtrW/SetWindowLongPtrW(-20) 翻转 + SetWindowPos FRAMECHANGED 广播，toolHwnd 登记表幂等，ForgetCaptionButtons 一并清）；policy_zform_schema 门要求生成器读的键必须在 zform.doc.json 有文档，补 winTool 一行（教训：第一次用 python json.dump 整文件重排 447 行，git checkout 回退后改用外科手术式单行 Edit）。回归：conformance_game_scene 增 win-defaults/win-title-rt/win-resizable-rt/win-chrome-rt/win-defaults-parsed 断言全绿。
  ② **on* 事件 + 声明式动作（d869db43）**：按钮点击在设计器里连线、零代码弹层切换。SceneElement 增 actions（on* 动态键对象，与 Gui.Designer 的 on<Event> 同形状）：On(evt,actions)/OnAction(evt) 链式 API，OnKeysOf 提取 + ToJson 原样写回 + IsModeledKey 吸收 on* 前缀（不进 extra）。动作串语义：ShowLayer:x / HideLayer:x|self / ToggleLayer:x|self / CloseAllLayers / OpenScene:目标 立即生效，未知方法名查 SceneDoc.SetHandler 注册表（与 Gui.HandlerRegistry 同契约：设计只存名字，业务代码注册 Action），未注册静默跳过；多个动作用 ';' 串接按序执行。SceneDoc.RunActions/RunOneAction 解释器 + pendingScene 排队（RunActions 不建窗口，生成的主循环帧尾查 PendingScene → RequestClose 换场景）——App 新增 RequestClose（post WM_CLOSE 等效点关闭按钮，经 WM_DESTROY→kind8 正常退出，供所有场景/自绘退出按钮复用）。SceneView.HandleClicks：帧内 Render 后分发，ElementHit（visible+层开）+ Resolve 几何 + Ui.ClickedIn，z 降序只喂最顶层一次；层过滤要 SetLayersActive(true) 才生效。GenScene：onClick 投影 On("Click",..)，Run() 开 SetLayersActive + 循环内 HandleClicks + PendingScene 消费；SceneDesigner 检查器「点击」行（占位 'ShowLayer:bag / OnBuy'，选区同步 + AnyInputFocused 门控）；IDE GenSceneFile 镜像逐行同步；LSP 契约注释补 on* 提及。回归：conformance_game_scene 增 8 断言全绿。
  ③ **IGameScene 适配器（35c02818）**：设计场景直插 Game.Foundation.SceneStack。**先修编译器**（rule 10）：GenScene 生成的适配器是嵌套 `partial class <Name>Scene`，而 contextual `partial` 只在顶层声明路径识别（parser.c 顶层 peek），成员路径 parse_modifiers 不认 → `expected member name`；parser.c 成员位补同款 peek-TK_CLASS/STRUCT/INTERFACE 识别，嵌套 partial 分部经 flatten（成员型 hoist 到单元级）→ merge_partials 正常合并（partial_types.zan 增嵌套用例 +42 行 golden）。适配器契约（GenScene 发射 + IDE GenSceneFile 镜像逐行同步）：Name/Host(app)/Enter（重建 SceneDoc + SetLayersActive + OnSceneEnter 业务钩子，IDE 业务壳带空实现可删）/Exit/Pause/Resume/FixedUpdate 转发业务钩子；Update(int) 帧体 = needsRedraw 守卫 + BeginFrame/Render/HandleClicks/PresentFrame，无 host 窗口安全空转；**Doc() 访问器**（本批补齐的关键缺口——SetHandler 注册点击处理必须在活文档上，没有它 on* 方法名动作用适配器语境无法接线）；PendingScene() 把 OpenScene 排队目标交给宿主（Replace/Pop + ClearPendingScene 消费）。全链路探针（.zscene→生成→SceneStack Push/Pop/名字/钩子计数）跑通。新增 tests/gui/scene_adapter.zscene + scene_adapter_test.zan（注册 conformance_gui_scene_adapter：IGameScene 实现、Push/Enter/Pop/Pause/Resume、层过滤初始关闭、Doc().SetHandler+RunActions 触发、OpenScene 排队/消费、无 host Update 空转、二推 hook 计数==2）。回归：partial_types/game_scene/game_foundation/gui_zform_control/gui_zform_grid/gui_scene_adapter 6 项全绿；smoke 212 串行 209 过（cef_profile/watermark/sparse_page 基线既有）；standard 692 项 6 败——3 项基线 + byte_buffer/forwarder_keepalive/forwarder_stream 属并行会话在途（irgen_expr.c ToStr 字节长 stamp、HTTP 桥接，非本批触碰，HEAD 树同态）。

* **A87 Android 字体 + gallery arm64 发布 + 自由画布整页拟合（已完成 2026-09-03）**：① **乱码根因与修复**：android 驱动 libzan_gui.so 此前未编 FreeType，全部文本落 6×10 ASCII 点阵，非 ASCII 一律画 '?'（gui_runtime_font.c bitmap_draw_text 的 `cp>=32&&cp<=127?cp:'?'`）。修复：FreeType 2.14.3（firefox vendor 树 /d/project/firefox/modules/freetype2）NDK clang 交叉编译成静态库链入两个 ABI 的驱动（`-DZAN_GUI_FREETYPE`，DT_NEEDED 与旧库完全一致 {libSDL3,liblog,libm,libdl,libc}，无新增动态依赖）。**关键坑——ftmodule.h**：默认 ftmodule.h 登记全部 20 个模块，只编 11 个模块时未编的 t1/t42/pfr/pcf/bdf/cid/raster1/sdf/svg 驱动类符号（t1_driver_class 等）在 ftinit 里悬空，dlopen 直接 "cannot locate symbol"；在 `<自定义include>/freetype/config/ftmodule.h` 放裁剪版（autofit/tt/cff/psaux/psnames/pshinter/sfnt/smooth 八项）以 `-I` 前置覆盖即可。**字体发现（ROM 自定义字体吃得到）**：Android 无 fontconfig——gui_runtime.c 的 fontconfig include 与 gui_runtime_font.c 的 FcInit/FcFontMatch 路径全部 `#if !defined(__ANDROID__)` 门控；__ANDROID__ 分支先解析 `/system/etc/fonts.xml`（AOSP 标 deprecated 但仍要求厂商维护，MIUI/HarmonyOS 换默认字体改的就是它）：第一个无 lang 的 family 为设备默认 face（family 内选 weight 400 直立项——厂商各 weight 是不同文件，"取第一个"会拿到 Thin；嵌套 `<axis>` 子元素剔掉后取文本路径），lang 含 zh 的 family 作 CJK 回退（带 index 属性，AOSP zh-Hans=NotoSansCJK ttc idx2）；解析失败回落 AOSP 写死链（Roboto→DroidSans→NotoSansCJK idx2）；CJK 回退链＝fonts.xml zh face→NotoSerifCJK→DroidSansFallback，缓存进 g_ft_fb[]，与 fontconfig 版同一 ft_face_for_cp 契约。解析器先做成 _scratch/ft_xml_probe.c 宿主探针：真机拉回的 fonts.xml（78KB）出 Roboto+NotoSansCJK idx2，MIUI 风格合成样本（各 weight 异文件+zh-Hans idx1+zh-Hant）正确选中 MiSans-Regular idx1——用户/厂商改了系统字体也能跟上。② **自由画布整页拟合（"组件只有一半"）**：Android 上 SDL 窗口恒为全 activity 表面（1080×2201），请求的 1000×520 无效；420dpi content scale 让 dpiScale=262，设计宽 2620px 裁掉六成。两层修复：(a) GenForm.zan 自由画布发射从构建期 `Place/Prefer(f*__dp/100)` 物理像素改为逻辑字段 `logX/logY/logPlaceSet/logW/logH`——ApplyDeclaredUnits 每次测量前按当前 app.Scale() 重解，DPI 变化即时跟随；`int __dp = Canvas.GetDpiScale()` 发射删除（ctl/window 两分支）。(b) App.zan 增 designW/designH（ctor 记录请求逻辑尺寸），Show() 在客户区放不下 `design×dpiScale` 时重推 dpiScale=min(realW×100/designW, realH×100/designH)，clamp [50,400]，只在放不下时介入（桌面行为不变），为可读性宁可滚动不缩小到 50 以下。验证：FormDemo x64 APK 装模拟器，六卡片三列整页呈现、中文全部正常。③ **gallery_test 安卓 arm64 发布**：按 scripts/build_gallery.ps1 同款文件清单（Gui 全集−ProjectComponents + Widget + components + 扫描注册表 + gui_gallery + MapChinaData，-DZAN_PROJECT_COMPONENTS，--embed examples/gui_gallery/assets=assets）`--publish --target android-arm64 --emit-apk`。三处编译器/stdlib 阻塞修就（rule 10）：(a) **android DllImport stub 策略**：bionic 无 openssl，gallery 经 ImageHttp→HttpClient→TlsStream 拉 [DllImport("ssl"/"crypto")] 直接链接失败——main.c 在驱动注册表发现后、发射前增 android 交叉桩：既不在 sysroot 子集（liblog/libdl stub .so）也无 android 驱动 .so 的 DllImport 库 `zan_irgen_stub_extern_lib + zan_irgen_drop_extern_lib`（与 linux/macOS 交叉同政策，警告明示运行期调用将失败）；**坑：drop 会压缩 extern_libs 数组，倒序迭代**，正序会跳过紧跟的下一条（ssl 剔了 crypto 漏网的实锤）。(b) **Socket.zan errno**：`#if LINUX` 在 android 上也命中（ANDROID 定义 LINUX=1），glibc 的 `__errno_location` 进了 libmain.so，dlopen 报 "cannot locate symbol"（bionic 只有 `__errno`）——补 `#if ANDROID` 分支（EntryPoint=__errno）置于 LINUX 之前。(c) **--apk-package/--apk-label**：包名取自第一个输入文件，多文件程序（首个是 stdlib/Gui/App.zan）得到 dev.zan.App 这种错包名——补两个显式旗标覆盖，缺省派生不变。E2E：arm64 APK 装进 x86_64 API 35 模拟器（lib 翻译层）跑通：组件树/语义按钮/代码高亮/中英文渲染全部正常，包名 dev.zan.gallery 标签 "Zan Gallery"（aapt dump badging 验证）。④ 回归：zform 系 8 项全过（diag_invalid_zform_kind/diag_control_zform_entry/conformance_gui_zform_control/conformance_gui_zform_grid/policy_zform_schema/policy_gallery_coverage/policy_gallery_demo_wiring/gui_datatable_colchooser）。遗留（另案）：APK 内嵌字体的选择性裁剪（当前每包 +1.1MB FreeType 代码）；HTTPS 在 android 上运行期 fail（stub 明告）；arm64 实机验证。

* **A88 Android 触屏/返回键/密度 + 手机抽屉导航 + INTERNET 权限/TLS 驱动（已完成 2026-09-03，e4e639d9 + dbe1b856）**：① **触屏手势管线（gui_runtime_sdl.c，+85 行）**：SDL_EVENT_FINGER_DOWN/MOTION/UP/CANCELED 三指路，首指跟踪（次要手指忽略），8px slop 内判定为点按→合成 move+down+up 点击序列，拖动按 1:1 合成滚轮（±120 刻度缩放，delta=dy*288/g_dpi——像素距离换算滚轮刻度），`SDL_HINT_TOUCH_MOUSE_EVENTS=0` 关掉 SDL 自带的触屏→鼠标双投递避免双份事件。`SDL_SCANCODE_AC_BACK` → kind-8 窗口关闭事件入队、不碰 g_quit——**返回键语义是"应用自己决定"**：gallery 手机态先关抽屉、再退应用（main 返回 → SDLActivity finish 任务，系统回到上一个任务），冷重启干净（新 pid）。② **显示比例（App.zan Show()）**：A87 的 dpiScale 重推是"放不下才缩小"，Android 上拟合结果可能低于设备原生密度导致 UI 过小——补 `#if ANDROID` 下限 `if (fit < dpiScale) fit = dpiScale;`：**原生密度渲染 + 页内滚动**，永不缩到设备密度以下（这就是"自动吸附布局"的最终语义），模拟器 1080×2400 dpiScale≈262 实测生效。③ **gallery 手机态导航（gui_gallery.zan）**：窗口宽 < Scale(700) 或竖屏（h>w）走手机分支——详情页全宽 + 左上角导航芯片（RenderNavToggle，area.x+Scale(12)），RenderNavDrawer 覆盖式抽屉（scrim + CaptureWheel/BlockHitsRect 模态，宽 Scale(300) clamp [Scale(200), width-Scale(48)]），kind-8 分支抽屉开时只关抽屉。ZanIDE 禁自绘守则不触碰（全部标准库组件）。④ **APK shell 补 android.permission.INTERNET**（dbe1b856）：现代 Android 对无此权限的应用 **socket() 直接 EPERM**（探针 t11 实测 'socket() failed: 1'）——此前所有 APK 内网络（连 loopback 都）根本建不了套接字。toolchain/apk-shell/ 新增文本源 AndroidManifest.xml（axml_patch 契约注释：串池须恰含一个 "dev.zan.app" + 一个 "Zan App" 占位串，含 aapt2 重生成步骤；**XML 注释禁双连字符**，长选项用 [dd] 记法书写——两次踩坑），AndroidManifest.xml.bin 以 SDK 35 aapt2 重出（2216→2356 字节，占位符各一验证通过）。⑤ **TLS 驱动（dbe1b856）**：stdlib/System/Net/Tls/drivers/android-{arm64,x64}/ 四件套——libssl.so/libcrypto.so 取 Termux openssl 3.6.3 预编译（bionic 兼容，NEEDED 仅 libc/libdl），**就地等长改写** SONAME `libssl.so.3→libssl.so`/`libcrypto.so.3→libcrypto.so` 并同步缩短 libssl 的 NEEDED（Android 只释放 lib*.so 后缀文件到 nativeLibraryDir，dlopen 按改后名匹配）；ssl.bundle/crypto.bundle 各列一个 .so（APK 打包经 `<driver>/*.bundle` 清单收集额外库进 lib/<abi>/，main.c ~5184；一分一 bundle 避免同名重复条目）。⑥ **环回 TLS 实证（_scratch/probe_https/ 探针，双架构全链路）**：模拟器无出网（netsim WiFi NO-CARRIER、eth0 无默认路由、adb reverse 数据面死、playstore 镜像无 adb root），自建 Probe Test CA + 服务器证书（CN=localhost，SAN IP:127.0.0.1/DNS:localhost）随包 embed，同一 APK 内 TLS 服务器+客户端跑 127.0.0.1:9444：embed PEM 先落应用私有目录真文件（**openssl 内部 fopen 看不到 embed 虚拟文件**；候选目录循环 /data/data/<pkg>/tls → TMPDIR → ./tls），CreateServer 装载证书私钥 OK → 客户端握手 OK + **证书链校验通过**（AddTrustedCert embed-aware）→ 服务端 accept OK/收 22/回 62 → 客户端收 62 字节 "HTTP/1.0 200 OK...pong-tls-loopback"。arm64 经 Berberis 转译（~570ms/帧，UI 慢但功能全对）、x64 原生，结果一致。A87 遗留「HTTPS 运行期 fail（stub 明告）」就此闭账。⑦ **探针工程坑（诊断面方法论）**：(a) **android-arm64 上 void 线程入口带 try/catch 悬挂**（两轮实测：线程入口只跑 try 前置语句；把 try 挪进 async 体、入口保持 void 无 try 即 ImageHttp.WorkerLoop→await FetchOne 形态，全部正常）——疑 EH/coroutine 交互编译缺陷，另案查编译器；(b) Worker/服务例程必须走「void 入口无 try → await async 方法(try 在体内)」；(c) `Task.Spawn` 自 GUI 主线程不泵反应器会挂死，IO 例程一律 `Thread.Start`；(d) **服务端线程发完即退时对端 RecvAsync 不返回**——疑 TlsStream 析构 SSL_shutdown 在已死线程上泵反应器、close_notify 发不出，对端既收不到数据也收不到关闭；服务端保活（Delay 60s）即正常——HttpClient 场景连接由调用方显式管理+有读超时，实际影响有限，记 TASKS 待修；(e) `am start` 前 `am force-stop`，否则恢复旧进程（旧代码）；(f) 模拟器进程冻结/慢帧下截图会骗人——以 frame 计数诊断面 + EGL app_time_stats 判定主循环健康。⑧ dist/android 四包重出（FormDemo + gallery_test × arm64/x64，全部 INTERNET=True；gallery 5 lib 含 ssl/crypto——ImageHttp→Tls 自动捆入）。⑨ **安卓标题栏精简（App.zan）**：新增 showCloseButton 旗标（字段/构造默认/ChromeButtonCount/RenderCaptionButtons 布局-悬停-字形-命中-tooltip-点击九处门控；关闭键原先是唯一无条件画的标题按钮），`#if ANDROID` 构造默认只留皮肤键——固顶/最小化/最大化在恒全屏表面上无意义，退出由 AC_BACK 承担（kind-8 通道与关闭键无关，不受影响）；`Show()` 的 window.SetCaptionButtons(ChromeButtonCount()) 让 C 侧 borderless hit-test 自动跟随按钮数。模拟器实测：标题栏仅 🏠+标题+🎨，皮肤抽屉 12 皮肤正常弹出。⑩ **发布配方教训**：dist 重出漏 `--embed examples/gui_gallery/assets=assets` → 包内无 gallery.json/图片素材，组件演示页整页留白（fail-soft 静默，极易误判"不稳定"）——gallery 发布必须带 embed，与 A87 §③ 配方一致。回归：smoke 214 全过（e4e639d9 时点）。遗留（另案）：arm64 实机 + 真实 https 出网验证；void 入口 try/catch 悬挂；TlsStream 析构 close_notify；APK 字体裁剪。⑪ **移动端体验三题（用户实测反馈 2026-09-04）**：(a) 模拟器滚动/弹窗/整体卡顿——主因是 x86_64 模拟器跑 arm64 包走 Berberis 转译（实测 ~570ms/帧，慢 30-100 倍），需真机 arm64 复测定性；x64 包在模拟器上原生执行可作对照组；(b) 皮肤抽屉等弹层走背景模糊（BlurRectCached），原生密度下是软件画布热点——候选优化：模糊降采样/缓存/半径随密度降档；(c) 组件演示本体仍是桌面布局，窄屏/竖屏适配（列布局溢出、定宽控件）需逐个响应式改造——手机壳（抽屉/全宽详情/原生密度+页内滚动）只是外壳。架构问题「Android 是否必须 SDL」：SDL3 在端上 = SDLActivity Java 壳 + EGL + 事件泵的薄封装，可用 NativeActivity+EGL 直连后端替代（省 libSDL3.so 11.4MB 包体积大头、少一层抽象），但属新后端工程且不改变转译卡顿；真机上 SDL3 本体不是瓶颈，软件画布+模糊才是。⑫ APK shell 清单补 android:icon="@mipmap/ic_launcher" 并以 aapt2 link 出真实 resources.arsc（原 40 字节空表解析不了资源引用），启动器不再落系统兜底机器人图标。⑬ **IME 会话跟随文本焦点（用户复测反馈 2026-09-04）**：安卓上软键盘随机弹出/常驻盖半屏、抽屉关了也不收——根因是 gui_runtime_sdl.c 建窗即 `SDL_StartTextInput` 且永不关，IME 会话与应用内是否有可编辑目标完全脱钩。修复链：runtime 增导出 `zan_gui_set_ime_open(on)`（Start/StopTextInput 幂等开关；建窗处 `#ifndef __ANDROID__` 不再锁开）；`Ime.SetOpen`（Backend/Native.zan，DllImport 收窄 `#if ANDROID`——macOS/Linux 的已发布驱动尚无此导出，导入存在就会 dlopen 失败，待各平台驱动重出后放开）；FocusManager 增 `textIds` 每帧登记（`RegisterTextEditable`，Input/TextArea/InputOtp/InputNumber 四文本控件接线）+ `RollFrame→UpdateImeSession`：焦点落文本控件才开会话、失焦即关（每帧去重，仅翻转触达后端）；gallery 抽屉三处关闭点补 `focus.ClearFocus()`。E2E（API35 arm64 模拟器）：冷启动无键盘、点 Input 字段键盘弹出、点非编辑区键盘收回，三部曲全过。另：gui_gallery.SyncLang 三行对齐组件 string lang（另一会话 lang 迁移中间态卡编译，Convert.ToString 显式转换）。回归：ctest gui|gallery（见当次提交说明）。⑭ **触摸滚动手感：拖动 1:1 + 松手惯性（用户复测「推动有明显延迟、松手该有惯性」2026-09-04）**：三处驱动侧修复（gui_runtime_sdl.c 触摸管线）。(a) **小数累积器**：拖动换算 `delta=(int)(dy*288/g_dpi)` 每事件截断，慢拖每事件不足一格的行程全被吃掉（滚动「卡住→跳格」=延迟感主根因）；改 `g_touch_acc` 浮点累积只压入整数格，亚格余量跨事件保留——实测 400px 拖动运行时换算 398px（SDL 触点归一化即 1:1）。(b) **松手惯性泵**：FINGER_MOTION 记 8 槽 (tick,y) 环形历史，UP 时取 ~120ms 窗口首尾样本算释放速度，>250px/s 即 `SDL_AddTimer(16ms)` 起泵——泵线程只 `SDL_PushEvent(SDL_EVENT_MOUSE_WHEEL)`（线程安全），复用主循环滚轮翻译路径；速度按 `v*=exp(-dt*1000/400)` 指数衰减，<120px/s 停；再次 FINGER_DOWN `fling_cancel()` 杀泵（按住即停，原生手感）；快速反向甩动按松手前 120ms 速度方向飞。(c) **滚轮事件合并（zq_push）**：应用每渲染帧只消费一个 zq 事件，触屏拖动每采样压一个 kind-13，事件到达率>消费率时积压——页面明显落后于手指、松手后还在慢慢追（模拟器 38fps 下 2s 拖动积压 ~1s 的量）。比照 kind-1 鼠标移动合并：队尾同类同窗 kind-13 **求和 delta、取新 x/y**，滚动总量守恒、积压结构性消失（一帧内吃掉全部待滚位移，捕获仲裁按指针最终位置）。遥测（临时 logcat 探针，已撤）：runtime 侧 sum_dy/sum_delta 实测 400px→455 格→按 scrollSpeed=40*dpiScale/100（g_dpi=252、dpiScale=262 实测）应滚 395px；证实 App 主滚轮分支未走（gallery 页面是 ScrollView 组件，事件被 CaptureWheel 仲裁分流），消歧 dpi 链怀疑（288·100/96=300 与 scrollSpeed 链精确对消，任何 g_dpi 下 1:1）。双架构驱动重链 + dist 双包重出（03:11/03:13），模拟器快甩实测：松手后内容续走 264px 后停（惯性），慢拖双向 1:1。⑮ **ScrollView 滚轮仲裁吞事件（根因已定位，另案修）**：拖动跨嵌套滚动区（gallery Button 页代码面板）时残差 ~14% 行程丢失——App.CaptureWheel 的 owner 选举是「上一帧最后一个含指针声明者」，事件分派先把滚轮判给 wheelCap 矩形（NoteScrollDamage），渲染期 owner 若矩形已不含指针（手指跨界后旧 owner 失配）`wheelOwnerOrder==wheelClaimSeq` 不成立，含指针的新声明者也拿不到（非 owner 一律 false），事件被静默吞掉；正确形状需两段式认领（owner 优先、无人认领时含指针者继承），画序依赖须谨慎，需配套 conformance 用例，另案处理（桌面滚轮跨嵌套滚动区同样受益）。
⑯ **安卓组件全量验收 + 演示自适应修复（用户「所有的组件都验证一下……尺寸不能自适应」2026-09-04）**：① **FilePicker 内嵌模态复验**：模拟器 x64 包实测 打开文件（带扩展名过滤条）/保存文件/打开文件夹 三模态全链路可用——打开、Cancel 关闭、Up 上一层、树/列表行点击都正常；页脚按钮条位置随模式不同（有/无 File name 行），手动测试按坐标点时先截图校准再点（此前一次「Cancel 失灵」实为点到了 File name 输入框，全屏遮罩把后续点击全吃了，造成「组件打不开」假象）。② **全组件扫掠 73/73 页**（抽屉搜索→进页→截图联系表）：发现并修复 5 处不自适应——**InputOtp**（stdlib/Gui/Widget/InputOtp.zan）：非 Block 模式格子定宽 cellSize，6 格在 ~398 逻辑宽卡片下右半排被裁；补「装不下按 w 收缩、下限 Scale(12)」（与 Block 同款）；**Radio** 演示 Panel.Row 不换行 → Flex.Wrapped（Error/Enterprise 裁字）；**SelectBox** 演示固定 0/200/400 三列（580 逻辑必溢出）→ 按 w 降列 3/2/1 + PreviewBoxH 行高分支；**Wizard** 演示「列表 240+表单」双栏 → <Scale(560) 改上下堆叠 + 框高 Scale(430)；**DynamicTags** 说明文字 → Ellipsis.Fit；连同早前 Dropdown 3/2/1 降列、DatePicker measure-first 堆叠，gui_gallery.zan 共 6 处演示自适应。组件侧 DatePicker 内容感知宽度（prefW ≥ 回显文本宽+触发钮）与 InputOtp 收缩已随 4fae299c 入库（并行会话文档扫尾提交顺带带入工作树同款改动）；FontLeadTop /4 垂直居中校准、FilePicker 遮罩自吞点击修复（Inside/Clicked 以 blocksInputBelow 甄别遮罩所有者）、Layer DialogSurface 窄视口钳制分别随 c813c2b4/4fe8ae58 入库。③ **回归**：gui_gallery 347 文件编译过；smoke GUI 74 项全过（本轮组件修复落盘前时点）；fix11 验证包 + dist 双架构包重出并装机复验（OTP 六格收缩/Radio 换行/SelectBox 双列/Wizard 堆叠/DynamicTags 省略号全数生效；arm64 包经 Berberis 转译启动正常）。④ **测试环境备忘**：模拟器被并行会话共用（awvprobe WebView 探针应用会抢前台，截图黑屏/异页先辨「谁在前台」再定责）；抽屉搜索 22 连 DEL 才清得干净、输入后勿按 ESC（kind-27 会把抽屉一起关掉，首个全量列表项是 WebView Probe 探针页，误入后 BACK 退出）。conformance_gui_webview_interop 当前失败（BuildDisplay 重复声明）为并行会话 DataTable 11 文件在途改动的中间态，与本线无关。
* **A87 Python 捆绑驱动退役 + Lua 加密内嵌（已完成 2026-09-04）**：① `System/Scripting/drivers/win-x64` 的 CPython 载荷（python312.dll/zip、vcruntime、libffi、python.bundle，先经 A47-4 瘦身再经本轮"核心档"实测 21.8MB→7.2MB，最终整体删除）不再随程序携带——Python.zan 本就按 `python313…python38.dll` 候选 dlopen 探测，系统装了 Python 的机器 `IsAvailable()` 自然为 true（比捆绑核心档能力更强：完整 stdlib），没有则 false 由程序降级/引导安装（官方安装器 `/quiet` 静默装可行，但"自动装 Python"不进 stdlib——系统级变更+供应链责任面留给应用层）。`scripts/stage_python.ps1/sh` 退役删除，driver.manifest 摘掉 `python`，examples/python/README 措辞改为"前置条件+引导"并明示脚本明文风险。POSIX 零依赖 Python 打包（目录树 + PYTHONHOME 缺失）另案，前提是 Linux 交叉先有动态链接形态（musl `-static` 内无 dlopen，实测 "Dynamic loading not supported"——Lua/Python 一并受制）。② **Lua 加密内嵌落地**（`Lua.LoadBytes/ExecBytes` + `tools/pack_script.zan`）：Lua.zan 增 `luaL_loadbufferx` 解析（注意 `luaL_loadbuffer` 是宏不导出 DLL，mode=NULL 传 nullptr）+ `LoadBytes(chunk,name)`（加载后立即清零缓冲）+ `ExecBytes`；tools/pack_script.zan（自宿主 CLI）把 `.lua|.luac` 以 AES-256-GCM（复用 stdlib AesGcm，密钥=HKDF-SHA256(seed) 运行期派生、无密钥字面量，chunk 名作 AAD 防换包）加密为 Base64 常量的自包含 `.zan`（PackedScript 类：DecryptChunk() 认证失败抛异常），产出 `Lua.ExecBytes(PackedScript.DecryptChunk(), PackedScript.ChunkName())` 两行接入。端到端实测：打包→编译→解密→执行全链路 OK（MAGIC=100/compute2(41)=141），`strings` 扫产物二进制脚本源文本 0 命中（对照：明文内嵌 1 行即可读）。保护边界：strings 级明文→"需逆向二进制"，密钥在客户端故挡不住决心逆向（客户端加密物理上限）；建议配 `luac -s` 字节码（反编译门槛再抬一档，注意 5.4.x 版本绑定）。driver.manifest 的 `<lib> if <prefix>` dlopen 驱动静态化（`--link-mode static` 现只认链接型）与 POSIX 动态交叉发布形态均为 Lua"静态编入 exe"的前置，另案。

* **待修复：ScrollColumn 首次进入视口吞掉第一下滚轮（Legend 实测）**：鼠标从非滚动区域进入无交互标签的日志视口，移动未触发渲染；首个 kind=13 帧 `App.CaptureWheel` 的 `wheelOwnerOrder=0`，而当前 `wheelClaimSeq` 增为 1，因此没有消费者。第二下正常。源头在 `stdlib/Gui/App.zan` 的前帧滚轮认领与无 hover 命中移动重绘之间的关系；不能简单放行所有 owner=0 的声明者，否则嵌套滚动可能双消费。复现：`_scratch/legend/presentation-scroll.ui` 去掉第二次 scroll，日志内容必须高于视口；探针 `_scratch/legend/main-scroll-trace.zan`、记录 `_scratch/legend/scroll-trace.txt`。布局/血条/透明素材已修，未在游戏代码中增加假滚动或额外吞发事件绕过此问题。后续须修标准库并覆盖首次滚动、兄弟容器切换、嵌套滚动、连续滚动的真实输入测试。

* **A89 初始化器挂在非 new 表达式上时，成员赋值跳过 no-setter 检查且静默丢弃（已实测，待修）**（2026-09-05）：`Maker.New() { Tag = s }`——初始化器挂在**方法调用结果**上（stdlib Gui 惯用法 `Panel.Column().Gap(6) { Children = list }` 全是这个形态）——里面对只有 getter 的属性赋值，编译零诊断、运行期赋值被静默丢弃；同款赋值在 `new T { ... }` 形态下则正确报 "property 'X' has no setter and cannot be assigned"（语句位、初始化器位、基类声明属性三种检查均在，探针逐一对过）。后果实例：tools/skinbuilder 首版 `Panel.Column()...{ Children = a }`（a 为 List<Control>）编译通过、面板 children 为空（MeasureTree prefH==0），一度按"空面板渲染 bug"排查半天。探针 `_scratch/prop_assign_probe.zan`（四形态对照：语句位报错✓、new 初始化器报错✓、继承属性报错✓、方法结果初始化器静默 no-op ✗ tag=<>）。修法方向：checker 让挂在 invocation 表达式上的初始化器走同一成员检查（setter 存在性 + 赋值兼容）；注意与**合法**的集合初始化器糖（`Children = { a, b }` → 逐个 Add，Control.zan 门面注释明载）区分，勿误伤。SkinBuilder 已改用显式 `With()` 收养绕开（组合原语、非绕过缺陷——缺陷在检查器漏报，正确写法本就该是 With/集合形态）。

* **待修复：Windows 文本超采样的测量/绘制宽度不一致（Legend 实机）**：当前 `src/runtime/gui_runtime_text.c::win_run_tile` 用 size 测量逻辑宽，再以 size*3 的 GDI 字体绘制至逻辑宽*3 的 DIB。Hinting 非线性导致截断：探针 `_scratch/legend/detail-pass/font-probe.py` 中技能消耗句 size=19 测宽360、size=57测宽1099，大于 (360+2)*3=1086，真实截图末尾数字被吃掉。修复须统一布局测量与超采样绘制的 advance/extent，覆盖长中英文数字串、普通/加粗、多 DPI 和实际 tile 像素边界，不能在游戏里补空格/假 padding/缩字号掩盖。当前 runtime 文件有其他会话未完成改动，本轮保留并登记，未改写该渲染算法。
* **待修复（编译器加固，非阻塞）：zanc `--embed <dir>=skins` 一律跳过 stdlib 皮肤基线自动内嵌**：src/compiler/main.c 的 Gui 皮肤自动内嵌块只要发现任一 `--embed` 目标名为 "skins" 就整体跳过，但项目 skins 目录往往只含自家皮肤包（如 templates/game/wuwei/skins/ 起初只有 wuwei/skin.css、无 base.css），跳过后发布产物没有 skins/base.css 基线层——Style.BaseSheet 为空，`flex { display: flex }` 不生效，所有 Flex 容器退化成 dock 排版、子控件全部叠在同一矩形（无为修仙传百艺页签全叠点不中即此因，探针 _scratch/flexprobe.zan：无 skins 内嵌则坏、内嵌含 base.css 的目录则好，已复现闭环）。本轮由模板自带 base.css（与 stdlib/Gui/skins/base.css 逐字节一致）解决，游戏 ALL PASS；建议后续把跳过条件收紧为"staged 目录含 base.css 才算完整替身"，修复草稿已写好但因 main.c 有其他会话未完成的 apk 改动（strtok_r/apk.h 签名）无法编译验证，本轮未保留该改动。

* **A90 GenDb typed-ORM 的 stdlib 内部类型被可达性裁剪丢弃（已实测，待修）**（2026-09-07，server-game 模板验证时发现）：`zanc app.zan --auto-stdlib` 单文件编译任何用到 GenDb 生成代码的程序（`db.Select<T>()` / `Insert` / `Update` …），报 51 个 "undefined type OrmCol/OrmMeta/OrmSelect/Expr" —— 这些类型由 GenDb 生成代码引用，但只存在于 `stdlib/System/Data/Orm/*.zan` 与 `stdlib/System/Linq/*.zan`。**探针**：`_scratch/orm-prune-probe/probe.zan`（15 行最小复现）。**根因假设**：`--auto-stdlib` 的 from_stdlib 输入走可达性裁剪，裁剪发生在 GenDb 生成代码参与分析**之前**（或生成代码引用不记为 stdlib 符号的根），于是 ORM/Linq 的实现文件被整文件剪掉。**workaround（已按 rule 10 登记，未在模板里隐藏）**：显式把 `stdlib/System/Data/Orm/*.zan` + `stdlib/System/Linq/*.zan` 加进命令行（等价 `_build_ide_check.ps1` 的显式 stdlib 模式），51 错误清零；pristine server-mvc 同样复现，属存量问题非本次引入。**补充实证（同日，完整 server-game 模板，legend 联机任务）**：65 文件全模板报 874 个同类 undefined，且报错被归咎到入口文件假行号（合并单元归属失真）；`--no-gen` 同批源类型检查全绿，锁定「裁剪先于生成器运行、生成器合入的输出引用已删类型」的精确顺序；第二 workaround：`ZAN_NO_PRUNE=1`（nsresolve.c 自带的 bisect 逃生口）整体跳过裁剪，server-game 编译通过并 E2E 跑通（注册/登录/建角/背包穿戴/挂机 5 杀奖励全流程）；探针另见 `_scratch/ormprobe/p2.zan`。修向：irgen/binder 里让 GenDb 生成代码的引用参与 stdlib 可达性标记，或 auto-stdlib 对 `System.Data.Orm`/`System.Linq` 命名空间整体豁免裁剪。

* **A257 Worker TCP 连接软空引用（未根治，规则 10 在案；非阻塞）**（2026-09-07）：`_scratch/server-game-run` 跑 server-game 模板 e2e（80 断言，全 PASS）时，每次完整运行在 run.log 恰好出现一条 `Net\Worker.zan:2607:16: runtime error: null reference where an object is required (member access)`——2607:16 即 `Connection.GetId(): return this.id;`，接收者为空/已释放。时间窗恒定在启动横幅之后、首个游戏登录（bob）之前，即第一个 TCP 客户端（mallory：connect → hello 推送 → register → 约 2s 闲置 → 客户端 drop）的生命周期内；每次运行恰一条、不随断言数变化。软错误只杀死当前任务：mallory 无会话、无功能断言受影响、服务端继续运行、后续连接（bob/alice/carl/dave + GM 页）全部正常。隔离探针 `_scratch/worker-onclose-probe/`（Worker("tcp") + OnConnect 推送 + onClose 里 GetId()）四形态均零复现：纯 connect+drop 30 轮、hello+register+闲置+drop 20 轮、以及 game-server 侧的 (B) register+drop、(C) login+drop、(D) 游戏连接+10 HTTP POST 混合、(E) 逐句复刻 e2e mallory→bob 前置序列——错误计数都停在基线 1，无新增。怀疑方向：完整服务端里 tick 协程（Gateway.SweepIdle 快照后逐个 GetId）与连接 EOF 清理（HandleTcp finally → onClose(conn)）之间的 Connection 释放竞态（ARC 下悬垂接收者），或 onClose 回调链上 conn 的生命周期缺口；最小探针缺 HTTP worker + World tick + 会话表的并发形状故不复现。影响评估：单发、软失败、不破坏功能与计数；真正会受伤的场景是"被杀任务恰持有会话"（会话滞留 World 直到客户端主动断开）。根治路径：给 Connection 的跨协程持有/回调补 ARC 生命周期契约（onClose 参数、Sweep 快照元素），或在 Worker 内对 GetId 接收者做存活断言把软错变成可定位的硬日志。复现环境：`_scratch/server-game-run`（fresh DB → server-game.exe → python e2e_v3.py → grep -c "runtime error" run.log == 1，连续 4+ 轮）。

* **A258 __zan_dict_remove 发射的索引回填在某键序后破坏探测不变量，后续 Remove 死循环（已实测，待修，阻塞运行期验证）**（2026-09-07，server-licensing 模板运行验证时发现）：ada73452（Dict.Remove O(1) 化）落地后重建的 build/zanc.exe 编出的程序，对一组共享前缀键做 `TryGetValue→命中→Remove→Add` 循环会在**第二次 Remove** 处死循环（进程 CPU 100%，无输出无崩溃）。**探针**：`_scratch/dictremove_probe/probe6.zan`（32 个真实屏键 `Admin.System.Roles` 等，每键 TryGetValue→Remove→Add，`timeout 6` RC=124 稳定复现；probe7.zan 加逐键打印定位：挂死点在 i=11 `Admin.System.Logs` 的 Remove 内部——打印 `removing...` 后不再出 `removed`，即挂死在 Remove 发射体内，而非后续 find）。对照组 probe1/2/3（短键 `k0..k63`、`Admin.Lic.S0..S7`、字符串值同型循环）全部通过，说明键分布（长共享前缀导致同簇探测链）是触发条件。**定位**：`__zan_dict_remove`（src/compiler/irgen_builtins.c，本发射体）step1「探测 hash(key) 找 fi+1 槽」**没有空槽出口**——若首次 Remove 的 backward-shift 把索引写错（step2 搬家槽改指 / step3 聚簇后移的边界），后续 Remove 的 step1 永远探测不到 fi+1 也碰不到空槽，转圈不止。嫌疑在 bsmove/bsstay 的 i/j 推进或 ekey2 select 对 `ej==fi` 用 ks[last] 代算 home 的分支。**影响**：阻塞 server-licensing 模板的运行期 rbac 验证（Perm.Index 对 ~62 路由做同型循环，挂死在启动阶段，master 进程只吃 CPU 不出横幅）；licensing API 契约 E2E 用更早一次构建的 exe 已全绿（激活/心跳/登录/设备上限/终身码，见模板 README），代码路径未变，仅需编译器修复后复跑一次验证。修向：先在 helper 的 step1 探测循环加空槽出口（miss 即退 -1）防死循环，再修 backward-shift 正确性；probe6 挂死复现是最小闭环。

* **A259 JSON 解析性能线封存（实测到顶，2026-09-08 沉淀）**：会话弧线 22→12→11ms/5.4MB（~490MB/s），参照 yyjson 同机 4ms。落地四笔：48ef3f7d（平铺 tape 重写）、d9e06ee4（Load64 内建 + SWAR 引号/键扫描 + 修容器值 valueSlot 指向子树尾槽）、63c82352（修空对象 firstPair 串键——`{"a":{},"b":1}` 上 Get(a,"b") 误命中 b）、d3a5c342（AsI64/AsF64 位型内建 + JsonSlot 24B→16B，double 位型折进 b 槽）。**系统性消融负结果留档**（全部独立编译实测，未来勿重复试错）：①边界检查在热循环已被 LLVM 消除（checked 字节和 34ms ≈ 纯循环地板 35ms/108MB，IR 三重检查在 -O2 后不存活）；②List.Add 在 Reserve 后非瓶颈（裸数组 tape ±0）；③递归改单循环状态机：骨架 7→5ms 但全功能版被帧簿记吃回 ±0（探针 `_scratch/bench/smskel.zan`/`_scratch/bench/arr/`）；④计数器静态字段→参数线程化 ±0；⑤强制内联 SWAR token 方法（alwaysinline ≤40bb）±0；⑥分派按频重排 ±0；⑦-O0/-Os/-O2/-O3 四档无差别；⑧walk 7ms 与 tape 密度无关，被 StrAt 物化分配 + 键 memcmp 支配（哈希字节短路是唯一未试的 walk 杠杆）。**剩余 ~2.7x 差距定性**：每 token 常数项累积（50 vs 19 周期），无单一可修瓶颈；重开条件=愿意做 yyjson 级完全重造（16B 槽 long[] arena 直写原型已到 10.8ms，见 `_scratch/bench/tape16/`，缺口是扩容设计：估少即 OOB，需 2x 倍增 + 管理数组拷贝原语）。**测试基建坑（本轮实锤）**：stdlib 改动后必须先 `cmake --build build` 刷新 stdlib.stamp 再跑用例，否则 ctest 复用陈旧产物（同一 35 例 5 秒假跑完）；且与并行会话共享 build/ 时并发构建会让用例成批假红（8 vs 2 失败数漂移）。假绿三连的教训：48ef3f7d 的 valueSlot 硬伤、selfhost_json_ignore、int_literal_range 均"测试绿"实为陈旧产物，HEAD-zanc/HEAD-stdlib 对照实验是排雷标准动作。
* **A260 全量审计缺陷修复第一批：P0 三连 + NaN/浮点格式化 + 委托 combine/内建构造编译错（已完成 2026-09-08，五笔提交）**：`_scratch/zan_audit/REPORT.md`（27 项缺陷 D1-D27 + 性能基准 + 117 例 JSON fuzz）修复批次按报告优先级落地。① **D1 除法 MIN/-1 挂死**（`21d3d07c`）：irgen_expr.c TK_SLASH/TK_PERCENT 软失败折叠只挡了除零——INT_MIN/-1、LONG_MIN/-1 执行真 sdiv 触发 x86 #DE，逃出软报告路径成 OS 级挂死；溢出组合（is_min && is_m1，仅无符号除外）把除数折到 1，商饱和到 MIN、余 0（同 ARM64 sdiv 与 C# unchecked ARM 语义）。回归 conformance div_overflow_min1。② **D2 JsonValue.Parse 尾部孤反斜杠死循环**（`e2e781e8`）：字符串 escape 循环 else 分支从反斜杠处起扫，尾部孤反斜杠让 pos 原地打转；补尾部守卫（strict Fail / lenient 逐字消费）。回归 conformance json_unterminated_escape（用 Encoding.CharFromCode(92)/(34) 绕开源码转义歧义）。③ **D3 null 字符串下标 SIGSEGV**（`057343ad`）：expr_has_reliable_string_bounds 命中的五条快速路径（局部/字段/string 字段/元素存取、读取）做越界检查却从不校验 base 为 null，null+s[i] 直接 GEP+load 段退出且无源码定位；新增 emit_string_base_guard（null 边沿触发的比较+分支，命中报 runtime error 带 file:line，软模式换 zan_rt_soft_scratch() 零页继续，硬模式报告内退出；置于长度探针之前免双报）。回归 conformance null_string_index（四孪生）。④ **D5 NaN != NaN 违反 IEEE**（`355ea6b0`）：TK_BANG_EQ 浮点分支 LLVMRealONE（有序不等）对 NaN 恒 false，违反 x!=y≡!(x==y)；改 LLVMRealUNE；== 的 OEQ 与有序比较 C# 语义不动；结构体逐字段/可空提升形态的 != 走 == 取反天然满足。回归 conformance float_nan_compare（NaN 十三态矩阵）。⑤ **D10 委托 += lambda 内部崩溃 + D11 内建标量构造静默 null**（`6980f93b`，diag_delegate_combine + diag_ctor_builtin_scalar）：untyped lambda 在 checker 定型为 error，恰好抑制算术路径末尾的运算符拒绝——`d += lambda`（脱糖 `d = d + lambda`）漏进 irgen 生成 `add ptr, @lambda`，LLVM verification failed 内部错误收场（实测字段形态 w.OnX += lambda 同炸，GUI 能用是因为 UiEvent.op_add 走类运算符路径）；checker 算术路径显式拒绝委托操作数，+/- 指引直接赋值或 UiEvent。`new string(bytes)` 则因 check_ctor_available 只查 TYPE_CLASS 放行、irgen new 降级对 Span/List/Dict/类/结构之外兜底返回常量 0 而静默产出 null（错误推迟到离根因最远处，正是 D3 族 SIGSEGV 温床）；标量原语（string/数值/bool/char）在 checker 以编译错误拒绝；`new object()`（lock 守卫，监视器只哈希指针不解引用）行为不变。⑥ **D6/D25 浮点最短往返格式化 + 特殊值 C# 拼写可回读**（`93ac441b`）：全部浮点到字符串路径（Console.WriteLine、Convert.ToString、ToString()/拼接/插值默认形态、StringBuilder.Append、emit_value_as_cstr/emit_to_cstr_u）曾是 C 的 %g——6 位有效数字截断往返、特殊值按 MSVC 旧式 "1.#INF"/"-1.#IND" 拼写无人能读回；新增 zan_rt_dbl_str（全程序必链 rt_timer 对象，%.Pe+strtod 最小精度搜索 + C# "G" 布局：一阶指数 -4..14 定点、之外 d.dddE±xx 带符号至少两位、NaN/±Infinity C# 拼写；数值缓冲 32→40）；回读侧 zan_rt_dbl_parse 前置 strtod 接管 Parse/TryParse/Convert.ToDouble 的特殊拼写——旧 msvcrt strtod（MinGW）早于 C99 对这些返回 0；显式格式说明 {v:F2} 仍走 snprintf。回归 conformance double_format（23 断言，四孪生）。**回归基线**：各笔提交时点 smoke 均 234/237（3 失败 runtime_gui_gl_aa / conformance_gui_chart_stackedarea_aa / policy_gallery_coverage 为并行会话图表 AA/GL 渲染 WIP，CPU/GPU 像素差，改动前后失败集一致，测试源不含本批新增诊断命中的构造）。**未修项如实登记（审计报告建议序 3/4/6/7 与 P2）**：D4 栈溢出诊断面、D7/D8/D9/D13 软失败策略分级（需 --strict-runtime 设计决定）、D14 File.ReadAllText("") 野异常逃 catch、D19 Main(string[] args) 空形参不填充、D16 无 >>>、D17 表达式语句（int.Parse("x"); 语法错）、D18 单行多声明符、D20/D21/D24 stdlib API 增补、JsonDoc.ForEachChild、D22 GetFiles 返回裸名（破坏性变更需协调）；另 D10 若日后要真多播需运行时 combine 记录（牵动全部调用点/ARC/闭包布局），当前以 UiEvent 为语言级答案。**第二批（同日，D19/D17/D20，c7b2b2df / 399bfefb / 1983197b）**：⑦ **D19 Main(string[] args) 空形参**：形参曾被完全忽略（局部作用域里没有 args），真实命令行只在 __zan_argc/__zan_argv 供 Environment 读取；emit_main_method 在入口块为单一 string[] 形参构建数组（逐元素 strlen+memcpy 成 owned rc 字符串并写长度头，argv[0]=程序名不进数组，argc<=1 空数组），arc_owned 局部登记照常释放；Environment.ArgCount 与 args.Count 天然一致（同一全局）。回归 conformance main_args（RUN_ARGS 注册进 CMakeLists 的 _run_args 分支）。⑧ **D17 表达式语句**：looks_like_var_decl 把「类型关键字开头」一律判成声明，`int.Parse("abc");` 在声明解析器里于点号处报 "expected variable name"；类型关键字后（跳过 [] 空档）跟 `.` 的形态改走表达式语句，void 维持无条件声明（永不开头表达式）；真声明不带点号不受影响。回归 conformance expr_stmt_builtin（bool.Parse 无此内建已从用例剔除——unresolved 与本修复无关）。⑨ **D20 string.Equals**：== 有内容比较而实例方法形态缺失；内置成员表登记 bool Equals(string other)，irgen_call 降级为同一 strcmp 内容比较，接收者 emit_str_nonnull 收敛（null 按空串，不段退出）；Equals(int) 等不匹配重载照旧 unresolved（Zan 字符串即字节串，显式 ToStr() 后再比）。回归 conformance string_equals。**第二批回归基线**：smoke 234/237（同一组并行会话图表 AA/GL 渲染 WIP 失败集）。**仍未修**：D4、D7/D8/D9/D13（--strict-runtime 设计决定）、D14、D16、D18、D21/D22/D24（D22/D24 破坏性变更需协调）、D26（行为本身正确）、D27（ARC 环引用固有局限，需文档）、JsonDoc.ForEachChild。

* **A260 全量审计缺陷修复第三批（同日，D16/D18/D14/D7 族/D21/D24/ForEachChild/D22，六个提交）**：⑩ **D16 无符号右移 `>>>`**（`1db3bd655`）：词法/解析/内建表登记三字符右移运算符，irgen 按 lshr 降级（有符号类型也按位空移），混合符号操作数语义与 C#/Java 对齐；回归 conformance unsigned_shift。⑪ **D18 单行多声明符**（`8b87caa9`）：`int a = 0, b = 2, c = a + b;` 曾报 "expected ';', got ','"；parse_var_decl 加逗号循环，每个声明符共享类型与 const/let 修饰、按源顺序声明（后面的初始化器可见前面的声明符），字段级与语句级（var 形态、首声明符无初始化器）经 pending_stmts 在 parse_block/parse_embedded_stmt/switch-section 三个语句收集点拼接；for 头仍用首声明符形态。回归 conformance multi_declarator。⑫ **D14 未捕获异常具名**（`22d5f066`）：审计所称"空路径 ReadAllText 野异常逃 catch (Exception)"经穷举形态证伪（空路径/缺文件/缺目录/带 System.Json/各类 catch 阶梯全部抛 FileNotFoundException 且可捕）——真实缺陷是未捕获报告只打 "(class object)" 无类名；编译期把 tid→类名登记进 `__zan_tid_name_reg` 零初始化数组（irgen emit 尾用 LLVMReplaceAllUsesWith 换实体、填终结项），两个未捕获报告点（同步传播尾 + async die）经 `__zan_eh_tid_name`（沿 thrown 描述符基类链比对注册表地址）打印 `Unhandled exception: FileNotFoundException`。回归 runtime exc_name（catch 语义，未捕获具名经 stderr 人工验证）。⑬ **D7 软失败策略分级 --strict-runtime**（`f6b967be`）：审计指出软失败（日志+继续）默认对数据完整性最危险；新增编译开关把 main() 序言烘焙 `zan_rt_set_strict()`（rt_timer g_soft_strict），每个守卫点的 zan_rt_guard_fail 走既有 hard 路径（报告 + RaiseException + exit(70)）；优先级 显式 ZAN_RT_HARD > 程序烘焙 > 软默认，操作员仍可用 `ZAN_RT_HARD=0` 免重建逃生。回归 runtime_strict_div_zero（run_runtime_error.cmake 新 STRICT 模式：--strict-runtime 编译且不设环境变量，要求 exit 70）。⑭ **D21 Encoding.UTF8**（`79a7dc53`）：stdlib 只有编码方向（Utf8FromCodePoint）没有 C# Encoding.UTF8 解码入口；Encoding.zan 新增 UTF8 属性、GetBytes（包 text.ToBytes()，null 收敛空串）、GetString(bytes[,offset,count])——逐字节 UTF-8 校验解码（首字节 194-223/224-239/240-244 分类、续字节 10xxxxxx、过短编码拒绝、窗口边缘截断逐个替换 U+FFFD 与 C# 宽松语义一致、绝不越界读；offset/count 夹取）。回归 conformance encoding_utf8（往返/null 收敛/非法序列/窗口夹取，四孪生）。⑮ **D24 ToUpper 非 ASCII 损坏——证伪，无需修复**：穷举验证 "abcé".ToUpper()→"ABCé"（0xC3 0xA9 原样）、"Straße"→"STRAßE"、"ÉLÈVE"→"élève"；libc toupper/tolower 对 ≥0x80 字节是恒等映射，多字节 UTF-8 序列天然存活，审计结论不成立（Zan 字符串即字节串的字节 passthrough 反而保证完整性），无需代码变更。⑯ **JsonDoc.ForEachChild + D22 GetPaths**（`cc781ee5`）：At(at,i) 顺序循环当随机索引用整体 O(n²)（6.4ms→1375ms）——JsonTape.zan 新增 JsonChildVisitor 委托与 ForEachChild(at,fn)（FirstChild/NextSibling 链遍历，O(孩子数)；对象成员给值槽位键用 KeyAt 取；标量/负槽位不触发；At 注释补陷阱警示）；D22 GetFiles 返回裸名与 C# 全路径不一致，但几十处既有调用依赖裸名，按库内 Filtered 命名族先例新增 GetPaths/GetDirectoryPaths 返回完整路径（磁盘项拼 ReadPath 解析目录、内嵌项拼原始 path），GetFiles/GetDirectories 注释显式标注裸名契约。回归：json_tape_roundtrip 扩 ForEachChild 断言 + 新增 directory_paths（前缀/可读回/裸名契约不变/缺失目录空结果），12 twin 全绿。**第三批回归基线**：smoke 235/238（3 失败 runtime_gui_gl_aa / conformance_gui_chart_symbol / conformance_gui_chart_stackedarea_aa，均为并行会话图表 AA/GL 渲染 WIP；与第二批基线相比 policy_gallery_coverage 转绿、chart_symbol 因并行会话在途图表改动入册，失败项全部落在并行会话的图表文件，本批触及 System.Text.Encoding/System.Json/System.IO.Directory/编译器 D16-D18/D14/D7 不含图表构造）。**审计缺陷台账收口**：D1-D3/D5/D6/D10/D11/D16-D22/D25 已修，D4（栈溢出诊断面）、D8/D9/D13（同 D7 族软失败点逐个接入评估，守卫机制已就绪）、D12（Dict 缺键默认值语义需设计决定）、D19（已修）、D23（字节串语义需文档显要标注）、D24（证伪）、D26（行为正确）、D27（ARC 环局限需文档）按性质归类收口；A260 批次结束。

* **A261 Thread.Start 吃实例方法组/闭包：编译通过、调用即崩 —— ✅ 已修（2026-09-11，与 A70 同一笔）**：`Thread.Start(p.InstanceEntry)` 与 `Thread.Start(() => { p.InstanceEntry(); })` 都能编译通过（Start 返回 true、线程正常创建），线程体调用委托时 SIGSEGV——ThreadStart 委托是裸函数指针（GenForm 注释明言 "delegates are plain function pointers"），实例绑定需要 `this` 语境、捕获闭包需要环境块，两者都没有，函数指针指向的调用约定不匹配直接跳野指针。复现探针（两文件均为最小形态）：实例方法组版 = class P2 { void InstanceEntry(){...} static void Main(){ Thread.Start(new P2().InstanceEntry) } }；闭包版 = P3 同形 `Thread.Start(() => { p.InstanceEntry(); })`（_scratch/hts_probe2/3.zan 实测，均 rc=139）。**修法**：采纳 A70 的根治路线①——native `zan_thread_start` 的 trampoline 按 ZAN_CLOSURE_TAG 判形态调用记录 fn 槽（捕获 lambda 与实例方法组一并成立），并按“调用方临时量 vs 工作线程生命周期”补 retain/release（见 A70）。**回归**：conformance `thread_start`（四形态）+ 500 线程 stress（500×实例方法组 + 500×捕获 lambda）在 -g 与 --check-leaks 单开两种模式下均无泄漏、无崩溃。stdlib 现有的静态 Job 通道（ImageHttp/Upload）不再必需，可作为风格选择保留。

* **A269 实例方法组绑定的接收者临时量泄漏 —— ✅ 已修（2026-09-11，查 A261 时连带发现并修）**：`Action a = new Job(i).Run;` 这一形态每次泄漏一个 `Job`（探针 20000 次循环恒定泄漏 20000 个对象，`--check-leaks` 报 `allocated at ...:7:20`；500 线程 stress 报 500 个）。根因在 `src/compiler/irgen_expr.c`：绑定实例方法组时闭包记录会 retain 接收者（`emit_closure_record(..., retain_target=true)`），但这条路径不像字段读取的 `finish_member_of_temp` 那样释放“属于临时量的那一份 +1”，于是 `new Job(i)` 的引用计数永远减不掉。修法：把 `finish_member_of_temp` 的“接收者是不是拥有所有权的临时量”判据抽成 `receiver_is_owned_temp`，字段读与方法组绑定共用，绑定后释放临时接收者（局部变量接收者不释放，因为不拥有）。**回归**：`tests/conformance/arc_temp_receiver_methodgroup.zan`（20000 次 `new Job(i).Run` 后求和 `ok 199990000`，泄漏由 leakcheck 孪生、穿越由 arcguard 孪生守住）+ 直接调用/具名接收者/静态方法组/临时字段读/属性读五组对照探针全绿。

* **A270 静态方法经实例调用（`d.StaticMethod(...)`）生成参数个数不符的 IR —— ✅ 已修（2026-09-11）**：`d` 是局部变量时，`irgen_call.c` 中「先按实例方法解析 local.Method(args)」的分支**无条件**把接收者放进第 0 个实参、参数整体后移一位，而静态方法的函数签名里没有接收者槽——LLVM 校验直接报 `Incorrect number of arguments passed to called function!`，报错点离调用点很远（只看到一个参数个数不符的 call）。同一文件更靠后的 `recv_cls` 分支早已正确处理这个形状（注释明言 “`expr.StaticMethod(args)` is legal … the receiver expression is not passed”），本分支漏了同一判定。**触发面**：`tests/gui/compref_designer_test.zan`（`d.SetCompPropValue(...)` / `d.CompPropValue(...)`，Designer 里这两个是 static），该用例因而长期挂在 standard 档。**既有缺陷证明**：最小探针 `_scratch/zq9/probe.zan`（class D { static string F(int,string); static int G(int); } + `d.F`/`d.G`）在 2026-09-03 的 HEAD 编译器上同样复现，与本轮 thread/ARC/泛型修复无关。**修法**：该分支补 `callee_static` / `recv_off` 判定——静态时不计接收者、不发射接收者表达式、参数索引不偏移、`emit_dispatch_call` 传 NULL 类（无 vtable 可查）。**回归**：新增 `tests/conformance/static_via_instance_call.zan`（局部实例 / 类型名 / 临时接收者三种形态，外加同一局部上的实例方法对照），conformance / leakcheck / arcguard 三孪生全绿；`conformance_gui_compref_designer` 恢复 `COMPREF_DESIGNER_OK`。
* **四期1 · LSP 补全/导航现状基线（2026-09-09 实测，探针 scripts/lsp_baseline_probe.mjs，`node scripts/lsp_baseline_probe.mjs` + `LSP_PROBE_MODE=repo|gallery|ra2`）**：① **仓库根索引发散**：rootUri=zan-lang 仓库根时 intel_index_project 递归解析全仓 .zan/.zform/.zscene，运行 19 分钟未到首个可用状态、RSS 2.7 GB（探针被迫终止）——monorepo 级工作区当前不可用，四期2 的首个目标。② **小型项目根（templates/game/ra2，29 个 .zan）**：initialize 275 ms；开文件+诊断 14–34 ms/文件；补全 P50 0.7 ms；命中率 2/3（局部变量 List 的成员补全 Add ✓；语句首 `using` 行前缀补全 0 结果 ✗）；goto-def `l.Add` 返回**空**——用户项目根不含 stdlib 时 stdlib 成员不可导航。③ **中型根（examples/gui_gallery）+ 736 KB 单文档**：诊断 48 ms；补全 P50 4.9 ms；`app.`（stdlib App 类型接收者）成员补全 **0 结果**——启发式 intellisense 推不出非内建 stdlib 类型的成员集；didChange→诊断 P50 45.9 ms（全量重解析，736 KB 每击键 46 ms）。④ **didChange 全文同步**：小文件 15.5 ms 尚可，随文档体积线性涨，四期2 增量前端直接针对。**四期2 指向**：索引按项目/依赖范围收敛 + 惰性（①）、stdlib 走随工具链的预算索引而非工作区扫描（②③）、前端增量入口替代全文重解析（④）、成员补全需要前端类型信息而非字符串启发式（③）。

* **四期2 · 第一批：LSP 换供数——工作区索引收敛 + 工具链 stdlib 入索引（2026-09-09）**：① **索引跳过目录收敛**（src/lsp/intellisense.c 两个平台 walker 合用 index_skip_dir）：bin/obj/build 之外补 dist/node_modules/target/_scratch/.git——仓库根场景曾 19 分钟不收敛（RSS 2.7 GB）的主因就是 _scratch/dist 被逐文件解析；跳过后 monorepo 模式 **17 秒收敛**（首开 16.6 s 含扫描+stdlib，稳态开文件 38–52 ms、补全 P50 6.8 ms、didChange→诊断 P50 7.3 ms）。② **工具链 stdlib 入项目索引**（src/lsp/lsp_main.c：lsp_stdlib_root 按 zanc --auto-stdlib 同款 exe 相对规则 `exe_dir/../stdlib`，ZAN_STDLIB 环境变量覆盖；ensure_stdlib_indexed 在项目扫描后一次性 intel_index_project 进共享 g_project_intel）：用户项目根不含 stdlib 时 goto-def 与成员补全对 stdlib 成员恒空的基线两缺一起修掉——ra2 模式 `l.Add` goto-def **空→ok**（指向 stdlib/Collections/List.zan），gallery 模式 `app.` 成员补全 **0/2→2/2**（RequestRedraw/Post）。一次性代价：首开 +2.4~2.7 s（stdlib ~360 文件解析），稳态不变。回归：lsp_integration_utf16 PASS。**余下（需前端类型信息，后续批次）**：接收者成员过滤仍靠启发式——`d.`（Designer）返回 64~203 个项目符号而未见目标成员（INTEL_MAX_COMPLETIONS 容量挤占），`List<string>` 泛型接收者 0 结果；这就是"编译器前端增量入口"批次要解决的本质问题。

* **五期 · Zan 类型元数据 → dap 变量 pretty print：探针定调（2026-09-09，_scratch/pp1 探针，实代码待做）**：DAP（src/dap）是 GDB/MI 后端，locals 走 `-stack-list-variables --simple-values`（聚合一律 "{...}"、has_children=false）——pretty print 的供给方只能调试信息。gdb 10.2 batch 实测 zanc -g 产物：`string` 局部 = `byte *`（裸指针，gdb 便利性显示直接带出 "hello pretty" 文本——字符串现状已可读）；**class 实例与 List<int> 局部均为裸 `byte *`，`ptype` 无结构、`p *p` 解引用只出首字节——编译器（irgen DI）对用户类型根本没发 DWARF 结构类型**，类字段/List 元素在调试器层不存在可展开信息。**定调：五期主体 = irgen 为 Zan class/List/string 发真实 DWARF 结构类型**（LLVMDIBuilder createStructType + 成员 offset/类型映射；字段类型递归：数值/bool→基型，string→`byte*` 附 char* 打印形态，class→具名结构指针，List→含 count/cap 头+数据指针的结构），GDB/DAP 端零改动即获字段展开；之后再视需要补 DAP 侧 python pretty printer 做语义化显示（字符串加引号、List 显示 [n](e0,e1…)）。备选方案（元数据侧表 + DAP 读内存自渲染）被否：重复实现符号/内存读取，且 GDB varobj 生态免费拿到展开树。**验收探针**：pp1.zan（string/class/List 各一）在 gdb `ptype p` 出具名结构、`p *p` 出字段名值对、DAP locals has_children=true 且可展开；lsp/dap 既有回归不回退。

* **四期2 · 第二批：typed member completion——文档优先接收者解析 + 项目成员合并（2026-09-09）**：① **接收者解析顺序修正**（src/lsp/lsp_main.c handle_completion）：原实现把 `d.` 的接收者名 `d` 直接喂给项目索引 intel_resolve_type——反向扫描下任何无关文件里的同名变量（后者覆盖前者）都会把接收者判成陌生类型，成员列表整批被污染；现改为**先对当前打开文档的 intellisense_t 解析接收者类型，解析不出才回退项目索引**，再在项目索引里按该类型取成员、与文档局部结果合并去重（同时去掉旧的 `count==0` 才回退 + 原样 memcpy——凡文档局部已有 1 条结果，项目成员就整体缺席）。② **效果**：repo 模式命中率 **4/9→9/9**（Designer 接收者 `d.` 的 SetUserComponents/SaveJson/LoadJson 全部命中，含部分前缀 `d.SetUser|`），ra2/gallery 模式不回退；补全 P50 6 ms，integration test（UTF-16 goto-def）PASS。③ **探针侧教训**：scripts/lsp_baseline_probe.mjs 的 SaveJson 查询正则从 `string saved = ` 开头而 offset 写的 2（约定是"匹配起点+offset"），光标落在 `st|` 上——发出去的是普通标识符补全（204 条 st 前缀符号），根本不是成员查询，误导了两轮"SaveJson 未入索引"的排查；修为 17（落在 `d.` 之后）后一发命中。教训进 skill（zan-lsp-intellisense）：**成员补全探针的 offset 必须按"匹配起点到点的字符数"数一遍，正则起点一变 offset 全作废。更正**：本条初稿记的"`List<string>` 泛型接收者 0 结果"是探针 offset bug 的又一误诊——ra2 的 `l` 本就是 `List<string>`（AssetDb.zan:27），repo `comps.Add`/ZanIDE `log.Add` 亦均命中，List/Dict 内建成员表对泛型类型名实测无缺。

* **四期2 · 第三批：增量 didChange（change:2）+ 诊断 worker 线程（2026-09-09）**：① **协议层增量入口**（src/lsp/lsp_main.c）：initialize 能力从 full sync(1) 翻转为 incremental(2)——contentChanges 按 LSP 语义逐条应用，带 range 的走 pos_to_offset（复用既有 UTF-16→字节偏移）+ 原地拼接，不带 range 的整文替换（full-sync 客户端零改动兼容，ZanIDE 现有全量 didChange 不受影响）。② **诊断 worker 线程**：didChange 在请求线程只做拼接 + 便宜的项目索引刷新（intellisense 启发式解析，736KB 约数 ms），昂贵的编译器前端跑（lex/parse/bind/check，大文件 30–60ms）挪到 worker：文档静默 200ms 后快照（doc_lock 内复制文本）→ 无锁跑前端 → publish；帧输出经 lsp_write 的 write_lock 串行化；dispatch 全程持 doc_lock（串行语义与单线程时代一致，worker 只在快照时碰文档表）。didOpen 仍同步（首开索引 + 首次诊断行为不变）。③ **实测**（探针新指标 completion-after-change = 击键后立刻发补全的响应延迟）：gallery 736KB 大文档 **46–63ms（旧：补全排队等同步前端跑）→ 12.2ms**；ra2 5.6ms；repo 13.4ms。didChange→诊断 P50 约 218–242ms（200ms 静默窗 + 前端跑；连续击键合并后只跑一次）。探针 didChange 段重写：3 轮全文 + 3 轮 range 编辑，垃圾行注入轮断言前端真的看到拼接后的文本、清除轮断言编辑确实被移除（拼接正确性端到端验证），三模式 6/6 ok；命中率 repo 9/9、gallery 2/2、ra2 2/3（using 首行补全为四期1 已知缺口）均不回退；lsp_integration_test PASS。④ **并发纪律**：g_project_intel 的读写全在主线程（dispatch 锁内），worker 不碰共享 intel——无需 intel 锁；主/worker 唯一共享输出点 lsp_write 已用 write_lock 串行化；worker 退出在 main 里 stop+join 后再释放文档表。

* **四期2 · 第四批（收尾）：`using` 指令命名空间补全——四期1 已知缺口清零（2026-09-09）**：① **上下文检测**（src/lsp/lsp_main.c 新增 using_ns_context，handle_completion 在成员/普通补全分派前短路）：光标所在行构成 using 指令即入命名空间模式——光标在行首而该行以 `using` 开头（四期1 探针的"语句首 `using` 行前缀补全 0 结果"即此位）、或行首到光标为 `using`/`using `/`using 词`（正在敲命名空间）；取出已敲前缀。② **供数**（src/lsp/intellisense.{c,h} 新增 intel_complete_usings）：候选 = 本文档 + 项目索引的 ISYM_NAMESPACE 符号（Gui/Game 等随四期2 第一批的工具链 stdlib 索引动态入列）+ 内建 stdlib_namespace_map（9 个 System.*，覆盖未打开模块），按前缀过滤去重，kind=namespace。③ **实测**：探针命中率 ra2 **2/3→3/3**，repo 9/9、gallery 2/2 不回退；lsp_integration_test PASS。四期1 基线的补全命中率缺口就此全部清零。

* **五期主体 · irgen 发真实 DWARF 结构类型 + DAP 变量展开（2026-09-09，验收全绿）**：① **编译器侧**（src/compiler/irgen.c +541 行 DI 段，irgen_emit.c 每次 emit 清缓存）：opaque 指针时代 LLVMTypeRef 不含 pointee 信息，`local_add` 新传入的 `zan_type_t` 是唯一真相，di_type_for_zan 从 Zan 类型递归出 DWARF——数值/bool 基型、string→"string" typedef(byte*)、class/struct→具名 DICompositeType（成员槽按 LLVM body 序 + C 布局 offset，vptr 头插 `$vptr`）、数组→{len@-16, elements@0 柔性数组}、泛型实例具名 `struct Box<int>`。三个实测才成立的规则：**泛型参数按名字替换**（binder 内部化的 TYPE_TYPE_PARAM 与 class 符号的 SYM_TYPE_PARAM 是两份对象，指针同一性永假）；**List 元素缓冲是 8 字节擦除槽，`data` 必须描述成 `long *`**（写成 int* 时 gdb 4 字节步进读槽，xs={7,9} 显示 data[1]=0 的根因）；class 环用 replaceable composite + LLVMMetadataReplaceAllUsesWith 收口（LLVM-C 无 ReplaceTemporary）。DI 全挂 `g->emit_debug` 早退（irgen.c:892），不带 -g 一行都不执行。② **DAP 侧**（src/dap/debugger.{c,h} + dap_main.c）：has_children 在 gdb 类型串上启发式（struct 前缀/结尾 *，排除 byte*/char*/string）；可展开 locals 发放 refs 3000+i，动态 varobj 节点用 DBG_VARREF_DYN(4000)+下标映射；`-var-list-children --all-values`（默认无 value=，曾致展开树全空）；MI child={} 解析引号感知+花括号配平；指针字段的单 `*expr` 伪子节点替用户多解一跳。③ **验收**：_scratch/pp1 探针 gdb 侧 `ptype p`→`struct P {int x; int y; string name;} *`、`p *p`→字段值对、`ptype b`→`struct Box<int> {int v; int n;}`、`ptype xs`→`struct List<int> {long count; long capacity; long *data;}` 且 `data[0]=7 data[1]=9`；DAP 侧 _scratch/dapprobe.mjs 全 PASS（locals 带类型、p 展开 x=3/y=4、xs.count=2、data 展开=7）；tests/dap/dap_integration_test 0 failures（目标 tests/dap/dbgtarget.zan）。④ **standard 层 29 挂归属清零（无一属本批）**：conformance 不带 -g，DI-only 改动结构性不可达（三处 hunk 两处在 DI 函数体内、一处只多传参）；逐族 git 考古——泛型 string+引用拼接 4 例挂在 de41f214b（9/5 检查器新规则 vs 旧 golden）、primitive_constants golden 停在 93ac441b3（9/8 浮点格式化未更新该 .out，expected 还是 1.#QNAN 旧拼写）、cs_b08_arrays 的 `int[,]` 挂在 9/8 parser 批（8b87caa9 单行多声明符）之后、dictionary_wide_values 在 9/8 06:33 的 full run 已挂、orm_* 10 例是 A90 裁剪账、emit_lib_macos_dylib 是 93ac441b3 新符号 _zan_rt_dbl_str 未进 mac dylib 链接面、clipboard/http/https/jwt/mysql/web_menu 6 例是环境（无网/无桌面）、chart_pie/diag_metrics/stackedarea_aa/runtime_gui_gl_aa 4 例属 charts 会话在飞文件（同 smoke 243/245 的仅挂 2 例一致）。教训入 skill（zan-dap-debugging 新建）：擦除槽 long*、按名替换泛型、--all-values、LastTest.log 轮转陷阱与归属三板斧。

* **ZanIDE 启动段错误：存量构建中间态，当前树不可复现（已闭账 2026-09-09）**：build\ZanIDE.exe（05:23 构建）启动即退，无窗口无输出——runtime 崩溃日志 build\zan_crash.log 实锤 `0xC0000005 read addr=0xffffffffffffffff` 于 exe+0xB9C85（--publish 无 -g 无行号，模块+偏移止步于此）。用 ZAN_IDE_ZANC_ARGS="-g" 重出（build_ide.ps1 逃生口，行表进崩溃日志）后**当前树构建 4/4 启动稳定**（同 config 同启动方式，旧 exe 必崩、新 exe 全活）。定性：05:23 的 exe 是共享树中间态产物（并行会话 gui runtime 在飞重构 + templates/game/legend/assets/client 素材同窗删除），不是现存缺陷，不追改。方法论沉淀：①IDE 崩溃先看 build\zan_crash.log 再上 cdb——runtime 崩溃日志器带模块+偏移，-g 重出即得行号，五分钟出结论；②重出时链接步出现过一次 zanc 0xC0000005（IDE_LINK_FAILED，重跑即成，产物完好）——疑似 A80 家族的退出期崩溃在 --embed/--emit-lib 新路径上的残影（并行会话 embedres/main.c 在飞），非确定性，暂记观察：再现于 HEAD 且可稳定复现时按 A80 的 cdb 流程另案。

* **standard 层存量挂账清零（2026-09-09，编译器/测试六连修）**：29 挂逐一归因后，凡属本仓可修的一批修掉（charts 会话在飞 4 例与环境依赖 6 例除外）。① **int[,] 语句级回归**（cs_b08_arrays，9/8 通过→9/8 parser 批后挂）：399bfebf 给 looks_like_var_decl 加的「类型关键字后跳过 `[]` 找 `.`」裸源码扫描只认空档 `[]`——`int[,]`/`int[,,]`/`int[][,]` 的 `[` 后跟 `,` 直接 `return false` 被误判成表达式语句；修为档内容忍空白+逗号（rank specifier），`int[,] r` 回到声明路径，expr_stmt_builtin 回归不破。② **emit_lib_macos_dylib 链接挂**：93ac441b3 给 rt_timer.c 加的 `zan_rt_dbl_str`（浮点最短往返格式化核心）没随交叉工具链对象重出——toolchain/{macos-x64,macos-arm64,linux-musl,linux-arm64,wasm32}/zanrt_timer.o 全停在 9/5；ELF so 容忍未定义符号（运行期才炸），Mach-O dylib 链接期即拒，只有 mac 测试报警。用 _scratch 里解包的 zig 0.15.1 跑 scripts/build_cross_rt.cmd 重出（win-arm64/ohos 块需各自 NDK 本轮未动），并手动刷 build/ 里的暂存副本（zanc 的自包含工具链打包「已存在即跳过」，重出 toolchain/ 不会自动进 build/——链接用的是 build/macos/x64/ 那份）。③ **Dict.Remove 换 swap-remove 为 shift-remove**（dictionary_wide_values）：原实现把末项搬进洞（O(1)）但 Keys/Values 随之离开插入序，golden 与 C# 可观察行为都要求保序——而且 irgen.c 的 Dict 布局注释本来就写着 "Entries stay in insertion order"（文档约定 vs 实现打架，实现是违约方）；改成 memmove 下移洞上方全部条目（键 1 词、值 value_words 词），helper 里 ~150 行增量索引修复（step1 定位/step2 改指/step3 后向移位，带两个历史挂死 bug 的补丁注释）整体删除，换成 indexed_count=0 整体失效 + find 惰性重建（icnt!=cnt 的 stale 判据已覆盖，append 快路径 kept 守卫天然拒绝 icnt==0）；Remove 变 O(n) 与 shift 同阶，conformance+leakcheck+determinism 全绿。④ **string + 可空值类型**（nullable_value_types 族 4 例）：de41f214b 的 string+引用拒绝把 int? 误伤——Nullable<T> 在 C# 是值类型，`"a=" + a` 合法且 null 拼空串。checker type_is_concatable 对 TYPE_NULLABLE 递归放行元素可拼的；irgen emit_to_cstr_of 解包 `{payload, i1}`——**select 不行，select 两臂都求值，死臂的 itoa 缓冲泄漏（leakcheck 孪生测试当场抓到）**，改 branch+phi，null 臂发 NULL 指针（emit_str_concat 的 NULL→"" 与 rt_str_release 的 NULL 守卫都已有）。⑤ **泛型 string+T 三例**（async_generic_method / generic_class_async / generic_class_instance）：C# 对无约束 T 的拼接同样拒绝，属测试源代码不合法——改显式 `x.ToString()`（Zan 无约束类型参数支持 object 成员，探针 x7/hi! 实证），golden 逐字节不变。⑥ **primitive_constants golden**：六行浮点停在 93ac441b3 前的 MSVC 拼写（1.#QNAN/1.#INF/1.79769e+308），按现行 C# 拼写（NaN/Infinity/1.7976931348623157E+308/1.401298464324817E-45）重生成，与该提交的 double_format golden 约定一致。方法论：归因表是动手的地图——「stale golden」「实现违约」「测试源不合法」「真回归」四种处置完全不同，混着修就会把行为改错。

* **win-arm64/ohos 交叉 rt 对象重出收尾（2026-09-09）**：上轮「win-arm64/ohos 块需各自 NDK 本轮未动」的尾巴清掉，过程中挖出两处真缺口一并修了编译器。① **rt_crash.h 架构门控**：aarch64-windows 下 `zig cc` 编 rt_io.c 四连挂——CONTEXT 字段名分架构（x64 `Rsp`/`Rip`，ARM64 `Sp`/`Pc`），`__builtin_setjmp/longjmp` 在 aarch64-windows 没有后端。新增 `ZAN_CTX_SP/PC` 宏 + ARM64 寄存器转储（pc/sp/lr/fp/x0-x5），guard 恢复机（recoverable/rip_recoverable/guard_resume/guard_log_deferred + setjmp 帧）整体收进 x64 门；非 x64 的 `zan__guard_call` 保留崩溃记录器一次性安装后直通执行（崩溃照常留 first-chance 记录，放弃恢复续跑——本来 VEH 的恢复重定向就只在 x64 分支里，这次把编译不过的半边也补齐）。② **arm64-windows setjmp 降层**（真正的拦路虎）：交叉链接 `async_socket_async_echo` 报 `undefined symbol: _setjmp`——ARM64 msvcrt.dll 压根没有 `_setjmp` 导出（mingw setjmp.h 明说 "ARM64 msvcrt.dll lacks _setjmp, only has _setjmpex"），x64 的「两参调用形状逼 LLVM 生成 rdx=返回地址前导」也是 x64 专属。irgen_builtins.c `emit_eh_setjmp/emit_eh_longjmp` 按目标降层：aarch64-windows 发静态对 `__mingw_setjmp(buf)`（入口 Lr 即返回地址，无需前导）+ `__mingw_longjmp`（libmingwex.a 自带，捆绑包实测有）；x64 路径逐字节不变（探针 exe 导入表仍是 msvcrt `_setjmp`）。③ **win-arm64 五对象重出**（zig 0.15.1，`zanrt_io/io_mt/sync/file/timer.o`，注意协议名 `zanrt_` 前缀）：链 `async_socket_async_echo`（reactor+定时器）与 try/catch+async 探针双冒烟，产物 `coff-arm64`，`__mingw_setjmp/__mingw_longjmp` 静态入链。④ **ohos 两架构六对象重出**（DevEco NDK clang 15.0.4，`--sysroot` -fPIC -O2；OHOS musl 的 libc.a 实测有 `_setjmp/_longjmp/longjmp`，加载期可解析）+ **补齐 ohos-arm64 sysroot 子集**：arm64 缺 `zap_main.o`（链接行强制探针，缺了报 "sysroot subset not found"）和 libEGL/libGLESv3 占位 so——这两样 ohos-x64 也只是前会话手工造的、无脚本无跟踪，新检出必挂；build_cross_rt.cmd ohos 块补上 zap_main.o（源在 ohos-x64/zap_main.c，架构无关）+ 空占位 so（新增 scripts/ohos_stub.c）配方，两架构 `tc.zan`（throw/catch+async）出 ELF so 冒烟通过。⑤ build/ 暂存副本手动刷新（win-arm64 五件、ohos 两架构八件）。**验证**：zig 先 x64 五对象回归编译（宏化无 x64 回归），standard 层 756 中 8 挂全部为已知家族（charts 会话在飞 2、网络环境 4、sqlserver/tray 2），orm 10 例已被并行会话清零，本批次零新增。
* **组件库配置面全量审计（2026-09-09，两代理盘点 + 首批 9 组件缺口关闭）**：逐组件提取 Props() 键 / 未暴露可配字段 / 工厂参数，对照 Element-Plus/AntD/Naive UI 同名组件配置面。**T1 真控件完全无 Props()（设计器不可达，按使用频率排）**：SessionList、ChatView、ConsoleView、CodeEditor（字体/语法色）、PropertyGrid、FilePicker、Dock（DockHost/DockPanel）、BandGrid、GraphView（仅 empty/class）、Wizard（内容模型全 ctor-only）、ListView、Grid、Dropdown（整套命令/数据面）、Layer（**charts 会话在飞，暂缓**）、Popover；ScrollColumn/ScrollView 属布局类低优先。**T2a 列表型配置仅 extras（可往返但属性面板不可见）**：Tabs 页签、StatusBar/ToolStrip 项、Steps 步骤、Progress 多环、Slider 刻度、Pagination order/jumperLabel、SelectBox 多列/级联/树、Calendar 标记、Card/Panel fx、Carousel slideViews/anim、RadioGroup 逐项禁用。**T2b 单字段缺失——本轮关闭 9 组件 20 键**：TextArea readOnly/autoHeight、Spin accent、Panel surfaceColor、Button color/textColor、InputNumber min/max/step/hint/precision/group（原 extras 提升，过时注释「快照 Binding 写不回」订正——普通字段直赋即实时绑定，Progress 惯用法）、Transfer 四个页脚标签、TreeView multi/reorder。**T1 标量切片（同日第二批）**：Popover（text/title/body/panelW/open——open 走 sflag 活 signal，text 带 syncName）、BandGrid（bandSpan/cellW/cellH/accent）、ConsoleView（empty/live）三控件新增整份 Props()；探针扩到 34 断言全绿。**盘点更正**：SessionList/FilePicker/DockPanel/DockHost/CodeEditor 并非 Control（普通类/partial 裸类），Props() 机制够不着——它们的设计器面要么靠宿主控件代声明、要么留待架构层决定，T1 表相应缩水；ChatView 的 busy 是运行态、文案在 ChatStrings 对象里（直绑会悬空），设计器面同样从缓。**T3 一致性**：ButtonGroup/Calendar 的 Int spec 应升 Enum（改动面板交互，留待单独批）；Input/Checkbox 补 class（已关）。PropSpec.Step(s,lo,hi) 一律置 hasRange（hi=0 即钳到 0）——无范围只配步进要用 StepBy。验证：`_scratch/props_audit/probe.zan` 34 断言 SetProp→GetProp 往返（含 TextArea.IsReadOnly / Popover.IsOpen 副作用）全绿，全库 361 文件编译通过。**T2a/T3 批（同日第三批）**：列表型 extras 配置入面板——StatusBar/ToolStrip/Tabs 的 options、Slider 的 marks 挂 Text spec 直绑 itemsSpec/marksSpec 字段，同时覆写 SetProp 把写路径截到 SetItemsText/SetMarksText 重建入口（直写字段不会重排项，spec 字段仅作读显示与设计值暂存）；ButtonGroup type/style/size 从 Int 升 Enum（下拉化，num 绑定仍存序号，PropertyGrid IndexOfValue 对旧文档整数串双向兼容；Calendar 复核已是 Enum，盘点有一处过时报）。探针 42 断言全绿；standard 层 6 挂全为已知家族（charts 在飞 2、网络 3、托盘桌面 1；上轮 clipboard 复跑通过坐实桌面剪贴板状态噪声），零新增。剩余缺口：T2a 里 Carousel slideViews/anim、Calendar marks、Card fx、RadioGroup 逐项禁用、Progress 多环、Pagination order、SelectBox 多列/级联属更深层模型改造，随 T1 架构议题一并排期。方法论沉淀进 zan-designer-components skill（字段直赋=活绑定、裸 class spec、无隐式串接、颜色整数十进制）。
* **组件配置审计 T2a 深模型批（2026-09-09，同日第四批）**：**派生键三件套定式立住**——文本是结构状态视图的键（order/circles/options/fx），Props() 里 spec 绑**表达式快照**供面板显示（PropertyGrid 每次 Bind 重建 specs，快照即新），SetProp 覆写把写路径截到重建入口（**快照绑定 `p.Write` 写临时值=静默丢失：坐实 Pagination pageSizes 设计器写入此前全丢**的真 bug），GetProp 覆写派生应答（spec 命中即短路 GetExtra，裸 spec Read() 恒 ""，不覆写会把既有 extras 往返打断）。① **Pagination**：order 入面板（spec+拦截到 SetOrderText）；pageSizes 写丢失修复（SetProp 拦截到 SetPageSizesText + GetProp 派生应答）；jumperLabel/jumperSuffix 入面板（字段直赋活绑定，无需拦截）。② **Progress**：percent 死键修复（原裸 spec 无绑定——SetProp 写空操作、GetProp 恒 ""，PropertyGrid 步进按钮也因 !IsBound() 短路；改快照 num 绑定+percent 字段拦截）；circles 入面板（快照+SetCircles 拦截）。③ **ChoiceGroup/RadioGroup**：options 入面板（基类 spec+GetProp/SetProp 覆写，OptionsText/SetOptionsText 虚化）；SetProp 写路径先 ClearOptions 后重建（面板反复编辑=替换语义，追加只属构造期直调；RadioGroup 既有 ClearOptions 升 override）；**RadioGroup 逐项禁用落文本语法 `A|B!|C`**（覆写 OptionsText 按元数据补 `!`、SetOptionsText 剥 `!` 走三参 AddOption，value=下标与文本路径既定语义一致）；CheckboxGroup 同享基类 spec。④ **Card**：fx 入面板（"ripple,aurora" 逗号文本 ↔ FxOptions 八开关，未列即关、空撤全部；glowColor/glowPeriodMs 留代码级）。**遗留（下批或架构议题）**：Carousel slideViews 是 List<Control> 文本不可表达（anim 是宿主计时对象）——需架构层；Calendar marks 是逐日运行时议程，设计器静态文本价值存疑；SelectBox 多列/级联/树为模型级改造；Steps 步骤、Card 同族 Panel fx 复核。验证：探针扩到 62 断言全绿（含重取 Props() 快照断言模型真变、options 替换不追加、radio `!` 往返、fx 清空），全库 361 文件编译通过；standard 层 6 挂全为已知家族（charts 在飞 2、网络 3、clipboard 复跑通过坐实桌面态），零新增。派生键定式与 PropertyGrid 写路径真相（p.Write 不经控件 SetProp）沉淀进 zan-designer-components skill。
* **平台缺口：Android 交叉链接不桩 win 系统库（已实测，待修编译器）**（2026-09-09）：`src/compiler/main.c` Android 交叉链接的桩循环里 `zan_win_system_lib(...)` 命中即 `continue`（跳过不桩），而 Linux/OHOS 交叉路径与原生非 Windows 路径同场景都是生成 stub——任何可达的 `[DllImport("kernel32")]` extern（如 stdlib Wide）留在 libmain.so 成 undefined symbol，NativeActivity dlopen 即 UnsatisfiedLinkError。本次治标：runtime shims 补 MultiByteToWideChar/WideCharToMultiByte（93ed2fc5）；治本应在 Android 路径对齐其余路径的桩策略。探针：`_scratch/anw`（出包链路）、dlopen 崩溃栈见 goldminer 首包。
* **IDE 不可用双缺陷定位与临时交付（2026-09-09 晚，用户报"窗口设计器无法工作"）**：① **链接失败**：build_ide.ps1 的 --link-lib 清单缺 ole32——047145bb 引入 zan_audio（WASAPI 设备枚举走 COM），静态驱动归档 libzan_gui_ide_gnu.a 直接引用 `__imp_CoInitializeEx/CoCreateInstance/CoTaskMemFree/CoUninitialize`，链接全灭且旧 exe 已被 rename 挪走 → 用户手里没有任何能跑的 ZanIDE。修复：脚本补 `--link-lib ole32`（publish_ide 的 DLL 侧清单本就含 ole32，只有 dev 构建漏了）。② **Os/O2 优化误编译**：链接修好后 IDE 启动 ~0.5s 即 0xC0000005（ARC 释放链读 0xffff…，teardown 崩溃）。归因三板斧：静态/动态烟囱 GUI 程序（含同样的在飞 gui_gl_* 3D 改动 + ole32）存活 → 运行时无罪；换 lastproject/清空 config 仍崩 → 非工程/配置数据；O0 构建（--publish 摘除）稳定存活 20s+ 且 UiDriver dump 到 76 命中区 → **--publish 特有**。继续二分：-Os 变体（无混淆/无 gc-sections/无静态驱动）同样崩（更早：CRT 启动段 0x1AE5 处读崩）；-O2 变体 6.5s 后自行 exit(1)（另一形态）。**触发开关锁定在优化 pass 本身**（Os 与 O2 皆触发，形态不同），IDE 形状代码（548 文件、GenForm 展开大项目）必现，烟囱小项目不触发。③ **临时交付**：build/ZanIDE.exe 已重出为 O0 构建（21MB，功能完整），设计器可用；build_ide.ps1 加 `IDE_NO_PUBLISH=1` 逃生门。④ **待修（编译器，需重建 zanc——与并行会话在飞的 irgen_arc/irgen_call 改动冲突，等其落地后跟进）**：优化器在 IDE 规模工程上的误编译，复现配方 = build_ide.ps1 现清单 + `-Os`（ZanIDE_A.exe / -O2 ZanIDE_B.exe 均在 build/ 可复现）， symptômes：A 启动即崩于 teardown ARC 链、B 存活 6.5s 后 exit(1)。

* **平台缺口：Android 上内嵌资产解析不通（已实测，待修 Assets/File 内嵌链路）**（2026-09-09）：`--publish --emit-apk` 打出的 APK，运行期 `Assets.Find("assets/bg.jpg")` 返回相对路径且 `File.Exists=false`（`_scratch/guiprobe` 探针 v2 实测，find=assets/bg.jpg exists=no）——纹理资产全部拿不到路径，BlitImage 走不到，模板矢量兜底成了真机实际画面。桌面/发布目录行走同一 API 正常。需要让 Android 运行期的 Assets.Find/File.Exists/读文件打通内嵌镜像（或解出到 files 目录）。探针：`_scratch/guiprobe/main.zan`（v2 带资产诊断段）。
* **两个表格调色板项 kind 断层修复（2026-09-09 晚，用户报"还有两个datagrid"）**：用户所说"两个 DataGrid"即设计器调色板的 23 表格（Table）与 41 数据表格（Data Table）。根因是 4eb8601d5"Save designs with the real widget class name"重写 KindForType 时把这两项错位：23 映到 `"Table"`——那只是设计器画布 PreviewTable 的**静态表头渲染器（static class，非 Control）**；41 整个缺席，回落 `"Input"`。三重后果（探针 `_scratch/grid_designer/probe.zan` 实证）：调色板放下 41 保存的 .zform `kind=Input`，生成 `new Input()`——数据表格凭空变输入框；手写/工具文档 `kind:"DataGrid"`（tests/gui/zform_grid.zform、IDE 自己的 AiSettingsDialog.zform）经 TypeForKey 反查不到→ftype=100 当自定义组件加载；生成器对旧 kind 无别名，旧文档 `new Table()` 直接编不过（"cannot convert 'Table' to 'Control'"，pal4 探针复现）。修复（a64a73cd）：KindForType 23/41 统一返 `"DataGrid"`（与 TypeName 的 23||41→DataGrid 对齐），TypeForKey 加 `DataGrid/DataTable→41` 显式别名，GenForm.TypeOf 把旧 `Table/DataTable` 归一为 `DataGrid`——泛型缺省 string 实参随之生效。**方法论沉淀：KindForType 的每个返回值必须是"真实存在的可实例化 Control 类"；TypeName（显示名）、KindForType（控件类）、TypeForKey（逆映射）三处必须一起对账——面板/检查器/生成器共用这张表，错一项就是"某个组件设计不能工作"。**回归九项全绿：zform_grid/zform_control/compref_designer/retarget/formbuild/datatable/datatable_formula/colchooser/batchjob + run_zform_schema 策略（78 控件类）。
* **画布自绘类型 kind 断层收尾（2026-09-09 晚，同族第二批）**：KindForType 里 43→"Chart"、44→"Ellipsis"、46→"Scrollbar"、53→"Ribbon"、56→"Layer" 五项同样指向非 Control 的渲染器/静态类，且把 4eb8601d5 自己写的"chart/scrollbar/layer 生成容器占位"规则整个遮蔽成死代码（早退在前、归一规则永不可达）。后果：调色板放下图表字段→保存→`zanc` 编译该 .zform 直接 `cannot convert 'Chart' to 'Control'`——"设计图表→编译项目"这条路从组件上线起就是断的。**修复策略与表格批不同：这五类在画布上有自绘预览，必须保住类型身份**——KindForType 保留显示 kind 不动；TypeForKey 加显式逆映射（Chart→43/Ellipsis→44/Scrollbar→46/Ribbon→53/Layer→56，此前加载时全被当自定义组件 100）；GenForm.TypeOf 在代码生成处归一为可实例化占位（Chart/Scrollbar/Layer→Panel、Ellipsis→Label、Ribbon→ToolStrip）。验证：multi 探针 22 种 kind 全编译运行、identity 探针五类型 Save/Load 往返 ftype+kind 不变、zform 系+datatable 系回归全绿（0516f4c1）。**教训升级："某个组件设计不能工作"先跑一遍"调色板放置→SaveJson→zanc 编译→运行"全链路探针，KindForType/TypeForKey/GenForm.TypeOf 三张表逐行对账——静态类、渲染器、泛型缺省实参缺一环都会断在设计器到编译器的接缝上。**
* **设计器交互面三缺陷修复（2026-09-09 深夜，用户报"排版设计、组件内嵌之后的拖动、编辑都有问题"）**：沿"报障面太大就自己全链路复现"路线把画布交互三条链路（拖动/嵌套/编辑）逐行读穿，找到三个可实证缺陷，一个对应用户一句抱怨。① **撤销只退一步、重做永远落空（编辑）**：Undo/Redo 恢复快照走 LoadJson，而 LoadJson 无条件 `ResetHistoryLocked()`（那是"打开新文档"语义）——第一步 Ctrl+Z 之后两个栈全被清空（探针实证：三次 Add 后第二次 Undo 报"没有可撤销的操作"，redo 永远空转）。修复：拆出 `ApplyJson`（恢复模型、不动历史），LoadJson = ApplyJson + 成功时清历史（解析失败保持历史，与旧行为一致）；Undo/Redo 改走 ApplyJson。② **嵌套组件一拖就"瞬移"（内嵌后的拖动）**：自由画布子级 fx/fy 是父内容框相对坐标，而拖拽基 freeSelPX/PY 只在 FreePick 直接命中时顺带写入——框选、撤销、代码置选中之后基还是旧值（通常是 0），拖嵌套子级按画布原点算位置直接飞出容器；选中框、8 个缩放手柄同样错位。修复：新增 `AbsOriginX/Y`（按父链现算）+ `SyncFreeSelBase()`（框选释放、指针按下、Undo/Redo 后对基），拖拽/缩放移动块每帧现算基不信字段（同帧的落点探测会经 FreePick 副作用改写它）；多选移动补祖先去重（Ctrl 容器+子级同选时子级只随祖先走，防双倍增量）。③ **内嵌之后拖不出来也放不进去（嵌套工作流断头）**：自由画布拖拽根本没有换父——拖进容器只能靠重新从调色板拖一个。修复：`FreeReparentSel(cont)` 在松手帧生效，摘除→设 childTab（Tabs/SplitPanel 进正在看的页）→挂入→fx/fy 按 `AbsOrigin(旧)−ContOrigin(新)` 换算保持视觉位置不动（换算函数内自算，不依赖调用方先跑 FreeDropContainer——探针自己就踩了这张契约）；InSubtree 拒绝拖进自己的子树，画布外松手不算拖出；拖拽全程 DrawFreeDropHi 给目标容器描边预告落点，与流式模式的容器高亮对齐。验证：`_scratch/designer_interact/undo_probe.zan`（撤销×3+重做×2+撤后重做、嵌套基断言、换父出/入坐标保持、自子树拒绝、操作后 SaveJson/LoadJson 往返）INTERACT_OK；上批 grid_designer 探针复跑绿；conformance zform_control/zform_grid/formbuild/compref_designer/batchjob + diag/policy zform 三件 8/8 绿。**教训入 zan-designer-components skill（画布交互节）：快照恢复与文档装入是两个语义入口；fx/fy 父相对约定 + 基的三处失效源；FreePick 的副作用字段不能跨帧信。**
* **AI 排版混乱治理：运行时重叠探测器 + 排版原语纪律（2026-09-10，用户报"AI 写窗口程序完全不会排版，界面经常重叠、尺寸位置乱七八糟"并附 Model Router 截图）**：先归因再动手。运行时排版原语其实是完备的——停靠（`Dock(1..5)` 吃边、`Control.Arrange` 构造上互不重叠）、自动流（`Panel.Column()/Row()`+`With()`）、Flex（行/列/换行/间距档位类）、Grid（N 等宽列响应降列）、FormBuilder；`Arrange` 里**只有 `dock==0` 手摆 `mx/my`+手工宽高这一条分支能产生重叠**。截图四乱象逐一对号：巨型刷新钮盖住省略号钮=手摆坐标+手设宽高、四张指标卡宽窄不齐=逐个手设宽度而非 Grid/Flex 等宽、省略号被裁剪=写死像素不随容器、底部大片死空间=内容面没 Dock(5)/容器高度不按内容量。**结论：设计器的两种模式（流式 24 栅格/自由画布）与运行时布局引擎都没有"会重叠"的缺陷，乱在生成界面的 agent 绕开布局原语按像素手摆**（HTML 绝对定位的习惯，这里没有兜底）。治理两件套：① **重叠探测器**（stdlib/Gui/Control.zan，Arrange 尾部兄弟可见矩形两两相交检查，>1px 判命中）：默认关（每帧只多一次布尔判断），`ZAN_GUI_OVERLAP=1` 或 `Control.DebugOverlap=true` 开启；命中打 `gui-overlap #N in <父>: <控件>[x,y w×h] overlaps <控件>[…] by ax×bypx`（窗口子系统无控制台时 `ZAN_GUI_OVERLAP_LOG=<文件>` 落盘），按「父 Kind+两个子矩形」签名去重（只报新面孔，测试用 `Control.OverlapHits()` 断言、`ResetOverlapHits()` 复位），刻意叠放（角标/悬浮装饰）挂 `.NoOverlapCheck()` 免检——**命中为 0 的界面必然不存在"叠在一起"，AI 自此有了机械可查的排版收尾条件**。探针 `_scratch/overlap_probe/probe.zan` 六用例（手摆命中、跨帧去重、免检、自动流静默、停靠静默、三钮互压报三对）OVERLAP_PROBE_OK；gui 回归 zform/formbuild/compref/compref_designer/batchjob/formgroup 7/7 绿。② **排版原语纪律**进 gui-design skill（硬规则节+自查第 16/17 条+触发词）：骨架停靠/内容面流式弹性容器/卡片行 Grid.Of(n) 等宽/手摆 mx/my 只属画布类/交付前 ZAN_GUI_OVERLAP 清零。**给用户的"好的办法"：让写界面的 agent 先读 gui-design skill 的排版原语纪律节，收尾必跑 ZAN_GUI_OVERLAP=1 清零；存量乱窗按探针日志逐条把手摆坐标换成对应容器即可，不需要重写。**
* **截图还原布局方法论 + 交互体验基线（2026-09-10，用户要"一张截图严格还原布局的设计规范"）**：gui-design skill 新增 `references/screenshot-restore.md`——单屏界面级的"截图→可验收布局"完整流程，与 app-migration（程序级复刻）分工。四铁律（几何忠实/语义提升=像素归档位但结构不变/颜色走 token/文案一字不改）→ 测量转写（定设计尺寸先折 DPI；从外到内切区块；产出**布局账本**表格：截图测量→原语映射→token 换算，交付逐行对账）→ 原语映射决策树（横条 Dock(1/2)、竖条 Dock(3/4)、内容 Dock(5)；内部纵向 Column、两端对齐 Flex.Between、等宽卡片 Grid.Of(n) 列数从截图数、1:1 双栏 Row+双 Grow **不用两个 Dock(5)（运行时同层 Fill 互相叠）**、表单 FormBuilder）→ 验证闭环（重叠清零 + 账本设计尺寸起窗 + 与原图并排/50% 叠加对拍，偏差改账本小步收口）。同文件附**交互体验基线**清单（反馈闭环/防呆/键盘与焦点/状态完整/信息排版——还原与新写收尾都过）。**实战示例直接用用户上一条报障的 Model Router 截图**：账本（1794×1198 物理 ÷150% DPI → 1196×798 设计）+ 结构代码骨架，`_scratch/overlap_probe/router.zan` 实测编译通过、1200×800 Arrange 重叠命中 0——示例不是示意，是验证过的。原稿的巨型钮/裁剪/卡片宽窄不齐按铁律 2 归一为档位尺寸（结构不动、病修正）。
* DPI 三票定倍数 + 账本双列制补进 screenshot-restore（2026-09-10，用户指出"截图还原时分析和还原经常因缩放问题导致混乱"）：测量转写账本改五列（物理px|倍数|设计px|原语+token），换算只在账本发生一次；新增 1.5 节——分析侧三票定倍数（正文/按钮高/标题栏三个独立证据除候选倍数落逻辑档取多数，间距步进是隐藏第四票：量到 6 的倍数基本是 1.5）、还原侧只写逻辑值（token/语义尺寸/按内容量，代码出现账本物理列原值即红旗）、对拍侧先归一再叠加（两边各除自己倍数到设计尺寸，物理尺寸不同的图直接叠差出来的全是倍数差，比比例不比绝对值）；验证闭环补"记下截图机 DPI"。Model Router 示例账本同步改五列并补三票证据（正文 24/按钮 46/标题栏 44 → 全指 1.5）。SKILL.md 缩放纪律节加回指。纯规范修订，无代码改动。

* **SDL3 整体移除 · 第一步：原生音频运行时落地 + 音频消费面清零（2026-09-09，探针 21/21 全绿）**：决策链（Gui 3D 可行性 → GL/GLES3 双形态 →"整个 SDL3 移除、跟 Gui 一样全在 Zan 内部完成"拍板）。① **zan_audio 运行时**（src/runtime/zan_audio.c，#include 挂进 gui_runtime.c 单 TU 末尾，随 zan_gui 导出——零新增驱动基建、CMake 零改动）：WASAPI 共享模式事件驱动（等待超时兼作轮询兜底）、专用混音线程（CLSID/IID 手写初值不引 uuid.lib，ole32 已链）、64 voice 槽世代号句柄（与 SDL 桥同打包 `(gen<<8)|(slot+1)`，句柄存活比声音长老实 false）、WAV 自解析（PCM 8/16/24/32/float32 + WAVE_FORMAT_EXTENSIBLE，统一归一 s16）、OGG 走 vendor stb_vorbis（src/runtime/stb_vorbis.c 自 stdlib/SDL3/native 复制，NO_PUSHDATA+NO_STDIO，文件读自走 CreateFileW 保 UTF-8 路径）、线性重采样 + float32/s16/s32/s24 四种混音格式输出、主/voice 两级音量混音时生效；非 Windows 全桩实现返回 0 + LastError 说明（CoreAudio/ALSA/AAudio/OH Audio 后续落地）。并发纪律：voice 表/clip 生命周期全走 CRITICAL_SECTION（play/stop_all/active_voices/voice_playing/voice_stop/set_gain/free_clip/混音填充都持锁；混音 64 voice 微秒级，持锁填充可接受）。② **Zan 侧 stdlib/System/Audio.zan**：Audio/AudioClip/AudioVoice 与 SdlAudio 三件套同语义同方法名（模板平迁只改类名），DllImport("zan_gui")——irgen 按**实际调用函数**收集 extern_lib（irgen_emit.c:1234），System 模块挂 zan_gui 导入不污染控制台程序。③ **消费面迁移**：goldminer（Snd 换 AudioClip，387 文件编译过）、wuwei AudioService（换原生 API + Sdl.Error→Audio.LastError，368 文件编译过）、Kit/Packed.zan（LoadWav 返 AudioClip；删零消费方的 LoadImage(SdlRenderer)；摘 using SDL3）、examples/audio demo+README 重写（内存+文件双加载）。探针 `_scratch/zan_audio_probe.zan` 21/21：设备打开/内存 WAV 元数据/一次性播放回收/双 voice 叠加/循环+Stop/错误路径假对象/文件 WAV/OGG 文件+内存解码（wuwei BGM 105040ms 2ch@44100）/OGG 循环 voice，实机出声。驱动 bundle 刷新坑：build_gui_driver.ps1 在 PowerShell 会话里 llvm-objdump 不在 PATH → DEF_EMPTY 退出 1——llvm 工具在 VS2022 `VC/Tools/Llvm/x64/bin`（bash PATH 有），objdump 提 95 导出（21 个 zan_audio_*）+ llvm-dlltool 重出 def/dll.a。**教训**：_scratch 残留 tc_goldminer.exe 进程锁着旧 _scratch/zan_gui.dll，zanc 拷新 DLL 报 "not bundled"——绕开：探针/模板产物输出独立子目录 `_scratch/zan-audio/`。**余下挂账（SDL3 移除第二步起，跨会话工程）**：Kit/Prims.zan 的 Draw/KitUi 深绑 SdlRenderer/SdlTexture（VGradient 烘焙/九宫/SceneCanvas），breakout/ddz/gomoku/snake/weiqi/xiangqi/ra2 七模板用 KitUi 走老 Kit.Host SDL 窗口——需迁 Foundation.Gui GuiHost + CanvasPrims（goldminer/wuwei/legend 是先例）；Kit/Host.zan、Foundation/Sdl/Host.zan(GameHost)、Core/App.zan、Render/*(BitmapFont/Sprite/Color)、Kit/Text.zan(Font 族) 全部零外部消费方属删除件；Arpg Engine/UiRenderer 迁 GuiHost（conformance arpg_* 存活是验收）；tests gui/game_output_supersampling_test 用 Game.Kit 待改写；ZanIDE.IsWindowedTarget 复核；最后删 stdlib/SDL3 全树 + gui_runtime_sdl.c + ZAN_GUI_SDL CMake 选项 + zan_sdl3 桥 + drivers bundle（win 包省 11.4MB libSDL3.so）。

* **全链路稳定性/安全性测试（网络/服务端/客户端/ORM，2026-09-09，两笔提交 + 本批）**：ORM 变量谓词+注入面 19 项、TCP 稳健性 5 项、HTTP 畸形/限界/走私 9 项、WS 帧边界 7 项、网关协议模糊 15 项、AES-GCM 重放 5 项、SQL 注入黑盒 4 项、HTTP 管理面 6 项、DB 池 6 项——全部探针绿。**修掉的根因**：① dbgen 生成类从不到达 nsresolve（merge 无 stamp + prune 在生成器之前删光 Orm 子树，typed-ORM 全编译挂）；② checker 无 AST_AWAIT_EXPR case，await 表达式落 type_error 后任何隐式转换静默通过（`TcpClient s = await l.AcceptAsync()` 编译过、运行段错误）——顺带暴露 templates/server-game World.zan:342 bool→int 真错并修；③ stdlib WS 四路径（Worker/WssServer/WssClient/WsSession）补 RFC 6455 门禁：保留 opcode 3-7/11-15 回 1002 断开（此前非法帧被当消息回显）、控制帧载荷 >125 拒、客户端帧未掩码拒、Ping 的 Pong 原样带回应用数据（此前空 Pong，心跳客户端断连）；④ runtime `zan_monotonic_us/ns` 的 QPC `ticks*1e6/1e9` i64 溢出（开机 9.2h/11d 后读数翻负，ServerMetrics SeriesSlot 负模 → 数组越界告警刷屏），改先除后乘。回归：conformance ws_protocol_gate 新增（golden 六行）、diag await_type_conversion 新增、orm/ws 受影响 golden 全部复验。**登记未修**：TCP 网关 login 无速率限制（30 次连续爆破全部稳定应答，无锁定/无延迟/无计数；HTTP 侧有 GlobalLimit 可配而 TCP 路径没有——server-game 模板层决定，需先定 keying 策略：按连接、按账号还是按 IP）；ServerMetrics 启动期 OOB 告警根因即 QPC 溢出已随 ④ 闭环。

* **SDL3 整体移除 · 第二步（上）：Arpg 脱 SDL + stdlib/Game 零消费方层删除（2026-09-09 下午，4831c678 + da902bd4，Game conformance 13/13 金样全绿）**：① **并行会话边界**：工作树有活跃在途改动——CanvasPrims StageViewport 桌面视口映射（CDraw.MapX/MapY/MapW/UnmapX/UnmapY）+ GuiHost.MouseX/Y + Gui/App + 七模板鼠标坐标切换（breakout/ddz/gomoku/snake/weiqi/xiangqi/ra2 main.zan + ra2 render）——七模板 GuiHost 迁移主体早已入库（181c20e5 起），KitUi/Draw 的清退属该并行车道，本侧全程不碰其文件。② **Arpg 脱 SDL（4831c678，+33/−1074）**：删 ArpgEngine（SDL 窗口/事件泵/帧步进，全仓零消费方）、UiRenderer（650 行，零消费方）、Fonts/GuiTextBackend.zan（SdlTexture 文字后端，零消费方；Fonts/ 留 PixelFont——本就无 SDL）；UiRuntime 摘 ArpgPointerButton.FromSdl(SdlMouseButton) 桥（唯一调用方是已删 Engine）；Architecture.md/README.md 改写——宿主与帧步进归 Foundation.Gui GuiHost（同七模板形态）、UiRuntime 纯逻辑自适配 Gui Canvas、RichText link 事件走 ArpgEvents。**验收**：standard 档 arpg 7/7（ui_runtime/database/formula/net/net_runtime/save_repository/tween）金样全绿。**发现先存悬空**：scripts/legend2_tool.ps1 生成的 `CreateEngine()`/`Legend2Engine`（using Game.Legend2）在仓内无任何定义——生成器早于本次改动就悬空，待单独任务定夺（补 Game.Legend2 层或改生成器）。③ **删 stdlib/Game 零消费方 SDL 层（da902bd4，−2164）**：Kit/Host.zan（老 SDL 窗口宿主）、Kit/Text.zan（Font/GlyphFont/CjkFont/SysFont/Text）、Render/（BitmapFont+Sprite+Color 全删目录消失）、Core/App.zan、Foundation/Sdl/Host.zan（GameHost）、Ui/Hud.zan（GameHud）——逐类 grep 消费方为零（`new Host(` 唯一命中是 Gui 的 radio_group 测试；goldminer 的 GameHud 仅历史注释，画布版已收敛兜底 HUD；`Draw\.`/`KitUi` 命中全落在并行在途模板），Prims 唯一外部依赖是 Support.Rng（Support 保留）；tests/gui/game_output_supersampling_test 随 Kit.Host 删（不在 ctest 档）。**验收**：Game 全部 13 个 conformance（anim_player/arpg×7/game_arcade2d/board/cards/foundation/scene）编译 + 金样 13/13。**教训：同一文件的两个 Edit 放同一并行批次会互相覆盖（后写赢，且报 success）——同文件编辑必须串行**。④ **SDL3 剩余消费面清单（删树前置，任务4）**：Kit/Prims.zan + Kit/Support.zan（ra2 的 Draw 链，随 ra2 迁移清退）、templates/game/ra2 3 文件（并行车道在迁）、examples/gamepad|gpu|touch（改写或删）、tests/gui/spritebatch_test.zan（测 SdlSpriteBatch，ctest gui 档在册——随 SDL3 删树同批删除）、tests/ra2/stdlib_rts/Engine.zan 与 tests/templates/wuwei/profiles.zan（import SDL3 但无删除集类依赖）、src/ide_zan ZanIDE.Project.zan:46 仅 `Has(source,"using SDL3;")` 字符串探测（删树后窗口目标判据需换新锚点，如 Foundation.Gui/GuiHost 特征）。

* **skills 三副本同步：发布包与全局副本补齐本会话全部教训（2026-09-09）**：用户指出 skills 有两组——发布的一组（tools/ai_pack/skills，随 SDK 进 dist/ai/skills）与 zcode 全局在用的一组（C:/Users/QQ/.agents/skills），而此前几轮 gui-design/game-dev 的更新只落了工作区副本，仓库外项目永远收不到。盘点确认三副本拓扑（工作区 .agents/skills=活源 LF；ai_pack=发布变体：剥离"在 zan-lang 仓库内工作"节、仓库路径改通用说法、description 尾带"仓库内同名版本优先"提示；全局=发布包字节副本），且 ai_pack 文件是混合换行（历史编辑遗留）。移植内容：gui-design 排版原语纪律节+截图还原路由+screenshot-restore.md（参考引用适配为仓库外可读）+缩放纪律交叉引用+自查 16/17，game-dev 统一 DPI 契约节；发布包 SKILL.md 归一为 LF。全局副本经 diff 校验与发布包字节一致。固化机制：scripts/sync_skills.ps1（①工作区↔②发布包逐文件漂移报告，忽略 CRLF；VARIANT=人工变体；-SyncGlobal 把②字节复制到③），AGENTS.md 规则 13 增补"三副本同一提交走完全程"硬规则。坑：PowerShell 5.1 无 BOM 的 UTF-8 脚本按 GBK 解析中文直接语法错误；PS 单文件目录下 Get-ChildItem 管道输出退化为标量，两标量 + 相加变字符串拼接（@( ) 包裹）。遗留：app-migration/game-online/testing-* 仅工作区（是否随 SDK 发布待决策）；zan-debugging/zan-development/zan-mcp 仅发布包（工作区无对应活源）。

* **尺寸自检 lint + flex 自检漏洞修复(2026-09-09,Gui 三连截图驱动)**:用户追问"AI 手打布局、反反复复调一个按钮,怎样才能真正避免",并连发三张 Model Router 截图(勾选框文字上叠字、巨块红绿按钮 103x140、下拉框被通栏保存钮压住、按钮文本居中错位)。与重叠检测互补的第二道机械闸门落在 stdlib/Gui:**ZAN_GUI_LAYOUTLINT=1**(或 `Control.DebugLayoutLint`),挂 Arrange 两条出口(停靠尾+flex 尾),三条规则按「父+规则+矩形」签名去重——①固定尺寸:Button/Checkbox 叶子带 prefSet(Prefer()/文档宽高,最强的手打信号,直接报);②拉高:分配高度>自然高度 1.4 倍且多 12px(巨块按钮);③裁剪:矩形装不下自然尺寸(文字被切);宽度故意不查"拉宽"(表单通栏按钮是正经排版);刻意定尺寸挂 `.FreeLayout()`(与 NoOverlapCheck 同一张 overlapExempt 牌);测试断言 `Control.LintHits()`。**顺带修洞**:flex 分支原来提前 return 绕过 CheckOverlapKids——现在两条出口统一走 CheckKidsLayout(重叠+尺寸两检;flex 兄弟构造上不重叠,但尺寸 lint 的裁剪规则全靠 flex 收缩触发)。ToolStrip 分隔条 Prefer(1,0) 挂 FreeLayout 自免。**探针**:_scratch/lint_probe(probe.zan 六断言:LINT_PROBE_OK;拉高/裁剪用裸 InitControl 子类+手填 prefW/prefH+裸 StyleBox display=1 无头打穿 flex 出口);overlap probe 复验绿;窄档 ctest 6/6(runtime_gui/stack/tabs/flex/css/theme_font_budget)。**gallery 构建与 standard 全档被并行图表车道在途的 ChartModel.zan 编译错误挡住**(ChartJsonDataset 缺 Regression/Clustering,非本车道文件,未碰)——standard 档须待该车道落地后补跑。**Zan 契约沉淀**:Control 子类必须有显式构造器调 InitControl(名字,停靠),隐式默认构造器不跑基类字段初始化,children=null/visible=false,首次 With/Arrange 即段错误(已进 gui-design skill 两副本)。gui-design skill 工作区+发布包同提交更新:排版原语纪律新增硬规则 6(文字控件宽高禁令+FreeLayout+ZAN_GUI_LAYOUTLINT+"同一条布局代码改到第二遍就停"迭代禁令)+自查 18+子类契约;全局副本 -SyncGlobal 同步。
* **H5 路线第一步：wasm32 文件 IO/EH 全通 + 两个根因修复（已完成 2026-09-10，0bdf3247）**：批准的多端发布路线（H5→微信小游戏→抖音→支付宝/QQ）第一步「最小 H5 验证」闭环。探针 `_scratch/h5probe/`：probe.zan（计算/字符串/Dict/文件 IO/try/catch 全面）+ eh_probe + read_probe，`zanc --target wasm32` 编译链接，配**纯手写 WASI shim**（zan_wasi.js，无 Emscripten；fd_write 持久化、fd_readdir 按 WASI dirent_t 填充、preopen 发现、clock/exit 全套），Node 三套验证全绿（OUTPUT MATCHES NATIVE / EH WORKS / read: hello），浏览器页 index.html（Chrome 146 实测截图）四项判定全 ✔。**修掉两个 wasm32 真缺陷**（rule 10，均已进提交）：① irgen_emit.c w32 签名适配 pass 只改写 CallInst——try/catch 体把 extern 调用降成 invoke，遗留旧 64 位签名的 invoke 让 wasm 后端物化 unreachable stub（模块可链接、运行即 trap：try 里 fread 即死）；CallInst/InvokeInst 操作数布局相同（callee 末位），同样改写。② wasi-libc 的 struct dirent 是自家的紧凑布局 d_ino(8) d_type@8 d_name@9，d_type 沿用 WASI filetype（3=目录 4=文件）而非 POSIX DT_*——Directory.zan 无 WASI 分支走了 glibc 布局，GetFiles 读到垃圾名字；补 #if WASI 分支 + direntDirType() 判据。回归钉子：tests/wasm32/file_io_in_try.zan（try/catch 内 WriteAllText+ReadAllText 交叉编译+链接断言）→ ctest `conformance_wasm32_file_io_in_try`（放 tests/wasm32/ 而非 tests/conformance/：conformance glob 会把同名注册成 native run case 撞名）；ctest -R wasm32 2/2 过 + smoke 档 243/246（余 3 为并行会话在途 GUI 改动，与 wasm 无关）。**shim 沉淀**（浏览器/小游戏 runtime 复用）：wasi-libc 的 FILE 缓冲在 fd_write 才 flush（文件持久化必须挂这里）；fd_readdir 步长 24+namlen 不 8 对齐——非对齐写必须走 DataView（typed array 静默截断，本次最深的坑）；d_ino 全字节非零（Zan posixName 从结构头 strlen 量名）；fdstat 需广告继承 rights 否则 openat 前置失败。**下一步（已批路线）**：② H5 GUI canvas 后端（A82-2 的 Web host driver）→ ③ 微信小游戏适配。

* **skill 与工具链版本错位教训(2026-09-10)**:D:/ZanIDE 安装版(9/4 的 zanc+stdlib)里 grep 不到 ZAN_GUI_OVERLAP/ZAN_GUI_LAYOUTLINT 与 DebugOverlap/FreeLayout 等新闸门——这些是 9/9(6b3a27b1)与 9/10(1c15a2ac)才进主干的;而该项目 AI 加载的 gui-design skill 来自用户全局目录(新版),于是出现"skill 要求跑闸门、工具链里不存在"的错位。该 AI 观察正确,但把自研 ScanOverlaps 定位成"本工具链的落地方式"是绕根因:正确动作是等图表车道落地、standard 跑绿后重新 publish 升级安装版,再用内建闸门;自研版只作升级前临时探针并标注。教训已进 gui-design skill(排版原语纪律节):闸门随工具链走,先查安装时间戳,升级而非自研。

* **设计器调色板对齐组件全集：新增 ft 73-85 十三类（2026-09-10）**：用户指出组件大量增改后设计器必须全部跟上。盘点确认设计器仍是 ftype 0-72 体系（FormField.KindForType → .zform "kind" → GenForm 生成代码 → ControlFactory 实例化），而运行时 ControlFactory 已扩到 30+ 新 kind——差集 13 个全部入库：**73 Flex / 74 Grid / 75 Image / 76 Calendar / 77 Countdown / 78 QrCode / 79 Watermark / 80 Marquee / 81 DynamicTags / 82 NumberAnimation / 83 InputOtp / 84 Transfer / 85 VirtualList**（新调色板分组"扩展"，MaxType 72→85）。每类：KindForType/TypeForKey 往返 + TypeName + FieldDesc(双语) + FieldIcon + RowUnits 画布行高 + PreviewField **真控件预览**（沿用 Collapse/Empty 的既有模式：真实 Flex 排 Tag/Badge、Grid.Of(2) 排 Statistic、Transfer<string> 双栏示例、VirtualList.SetItems 24 行等；可交互件 Disabled=preview 防抢点击）；DynamicTags 进 UsesOptionsOf（标签集=选项编辑器）；GenForm.DefaultTypeArgs 补 Transfer→string（VirtualList 非泛型，不加——第一版错加会生成编不过的 VirtualList<string>，已自查撤销）。**守门探针** _scratch/designer_palette/probe.zan：73-85 逐类 TypeForKey 往返一致 + ControlFactory.Create 非空 + 引用 Designer.T 把设计器 partial 整树拉进编译——这条"面板项必须能实例化"的不变量今后能拦住再次出现画布渲染器式半死条目（现存已知豁免：Chart/Ellipsis/Scrollbar/Ribbon/Layer 五个画布绘制 kind，GenForm 归一到替身控件）。conformance designer 切片 4/4（compref/compref_designer/zform_control/formbuild）。**遗留**（属性还有调整的部分）：① Tabs 方向（horizontal/vertical）设计器属性+纵向标签条预览（运行时 Tabs.Vertical 与 .zform 选项都在，设计器断入口）；② 逐组件属性深扫（Image src 走 bind、Countdown 时长/InputOtp 位数/NumberAnimation 目标值等尚无专属属性行）；③ Chart 在设计器仍是单一"柱状图"占位（ECharts 注册表重建后应带类型属性）；④ Flex/Grid 在设计器是叶子预览，作为可嵌套容器（拖放子组件）待做；⑤ headed 设计器全量过一遍（gallery 构建仍被图表车道 ChartModel 编译错误挡住）。

* **Tabs 标签条方向进设计器（2026-09-10，上条遗留①闭环）**：用户问"tab 支持标题栏左侧纵向吗、设计器里能设置并展示吗"——运行时 `Tabs(style, orient)`/`RenderVertical`/Props 枚举都在，断的只是设计器入口。全链路打通五挂点：① **FormField.tabOrient**（建模 int 0/1，InitField/Clone 同步）；② **检查器方向行**（FieldSpecs 对 kind=="Tabs" 追加 PropSpec.Enum "orient" 横向/纵向，PropertyGrid 直写 f.tabOrient）；③ **序列化**：FieldJson 写 `"orient":"horizontal"/"vertical"`（选项名可读），FieldFromJson 容忍新旧两种写法，IsModeledKey 入 "orient"（round-trip 不重不漏）；④ **画布纵向轨道**：KidInsetX 纵排让宽 `TabRailW()=160`（=运行时 trackW 未缩放宽）、KidInsetY 只留装饰边 6，RenderTabStrip 纵向分支画左缘轨道（整列页签+活动项左缘 accent 条+右侧分隔线，与运行时 RenderVertical 同构），tabHits 命中区随之换轴（点页签切页、逐页设计原样工作），LayoutFlowKids 流式宽同步扣轨道；⑤ **GenForm** 仅在 vertical 时发射 `tabs1.SetProp("orient","vertical")`（横排是默认不发射，`.zform`→代码探针 _scratch/designer_palette/genprobe.zan 断言两向）。**顺带根因修复（rule 10）**：PropSpec.Write 对枚举 kind（3）只认数字序号，`SetProp("orient","vertical")` 走 `Convert.ToInt32`→strtoll **静默得 0**、无声落回第一项——补选项名匹配分支（命中写下标到 num/snum；绑定文本时存选项名本身；未命中按整数原路，旧文档不破）。这同时救活三路消费：GenForm 发射的设计器 .zform、Serialize 文本格式、.zcomp 实例 props 直通表里用户手填的枚举值。验证：调色板探针扩 4 条 orient 断言（SetProp vertical/horizontal/数字序号→GetProp 往返）PALETTE_PROBE_OK；genprobe GEN_PROBE_OK；conformance designer 切片 5/5（zform_control/formbuild/compref_designer/formgroup/retarget）。**教训进 zan-designer-components skill（Props 定式节）：枚举属性收选项名也收序号、.zform 建模键 orient 全链路形态**；该 skill 不在发布包子集里（pack 待用户拍板），仅工作区副本。剩余遗留：②逐组件属性深扫 ③Chart 类型属性 ④Flex/Grid 容器化 ⑤headed 全量（standard 档是否已被图表车道解锁待本轮结果）。

* **内置组件直通属性行第一批（2026-09-10，上条遗留②起步）**：用户"很多属性也有调整"的另一半——运行时组件 Props() 经 T2a 审计（4c9bca05 等）已齐，设计器却没有行。选择**通用直通机制**而非逐组件建模：新增 `Designer.BuiltinPropRows(kind)`（kind → "键|双语标签" 表，首批 Image: src/alt/fit、Countdown: duration/format、NumberAnimation: from/to/duration/precision/separator、InputOtp: length/size/mask/readonly；运行态开关如 Countdown.active 不进表），值存 f.extra["props"]——与引用节点实例值**同一张直通表**（"props" 不在 IsModeledKey，SaveJson/LoadJson 透传），GenForm 的 props 泛化发射**零改动**直接生效；检查器对非 compRef 字段渲染 WIDGET PROPS 行（复用 compProp 输入状态机）；画布 PreviewDisplay 四分支接 `ApplyFieldProps`（预览件每帧新建后逐键 SetProp）。**顺带修掉 Image fit 的静默死写（rule 10）**：SetProp 管线 PropOf 先于 SetExtra，而 Props() 里 `fit.num = FitIndex()` 绑的是方法返回值快照——PropOf 命中游离 spec 即 return，SetExtra("fit") 真写入永不可达，SetProp("fit",…) 从组件上线起就静默无效；正解 `fit.str = Fit` 实时绑定字段（枚举选项文本即取值，与 InputOtp size/status 同款），FitIndex() 留作换算助手无消费方受扰。验证：探针 +8 条断言（BuiltinPropRows 形状、CompPropValue 往返、ApplyFieldProps 落 fit/src/duration 到真控件）PALETTE_PROBE_OK；designer 切片 7/7 + image/countdown/numberanimation 3/3。**教训进 zan-designer-components skill（Props 定式节加两条：PropOf-first 快照挡死 SetExtra；内置件直通行机制）**。遗留②续：其余组件按 BuiltinPropRows 表逐个补行即可，机制已通用。

* **图表家族进设计器：ChartHost 保留式控件 + 类型直通行（2026-09-10，遗留③闭环）**：回答"很多 chart 设计器里有增加吗"——此前设计器只有一条 Chart 条目：画布预览会用 uiState(0-7) 画 8 种真图，但 GenForm.TypeOf 把 Chart 归一成 **Panel 占位，发布窗体里图表整体消失**；运行时引擎却已支持 ~29 种 ECharts 类型字符串（ChartKinds 注册表 + CustomOf 扩展渲染器）。根因是 ChartView 是立即模式件（宿主逐帧 Render），控件树/生成代码无法持有它。三层打通：① **新文件 stdlib/Gui/Component/Chart/ChartHost.zan**（纯新增，不碰车道在途文件）——保留式 Control 包装：持有 ChartOption+ChartView，OnPaint 转发 Render、OnMeasure 320x200、SetProp("type"/"title")、SetOption 换真数据；**SampleOf(type)** 按类型现拼示例 option（line/bar/area/stacked/hbar/hstacked/pie/donut/scatter/gauge/funnel/radar/wordCloud 各配样，结构化图型 k/map/graph/chord/force/eventRiver/treemap/tree/heatmap 及扩展标签先以分类示例呈现，类型串保留）——设计器画布与生成窗体共用同一份示例，所见=初见。② **GenForm**：TypeOf Chart→ChartHost（Scrollbar/Layer 照旧 Panel），生成头补 `using Gui.Component.Chart;`；类型走既有 props 直通发射 `SetProp("type","pie")`，零新增发射逻辑。③ **设计器**：BuiltinPropRows("Chart") 出 type/title 直通行；PreviewChart 优先读 props.type（CompPropValue），存量稿回退 uiState→ChartKindAlias；FieldDesc 43 从"柱状图"改为"图表（…属性里选类型）"。ControlFactory 注册 "ChartHost"（Kinds+Create）。**验证**：palette 探针 +9 断言（factory 实例化、default bar、type/title 往返、gauge/wordcloud 示例、line 双系列、Chart 行存在）PALETTE_PROBE_OK；genprobe +4 断言（Chart→new ChartHost、type 发射、无 Panel 占位、无 props 也发真宿主）GEN_PROBE_OK；designer 切片 7/7。**插曲**：ctest -R chart 12 例失败全属图表车道实时编辑中的 ChartViewMore.zan（引用了 ChartModel 尚未声明的 lineStyle，保存于本次提交前 5 秒）——在途瞬态，非本批引入。**遗留**：ChartHost 的 Props() 尚未声明（PropertyGrid 场景可后补 type/title spec）；图例/配色/数据绑定属直通行下一批。

* **zanc 单输入“漏收同目录 sibling .zan” —— ✅ 结案：不是回归，是误判（2026-09-11 复核）**：现象属实（`zanc path/to/main.zan --auto-stdlib` 对 goldminer/wuwei 报 `undefined type`，显式多输入即通过），但“dde3b503 之前同命令全绿”不成立——`git show dde3b503:src/compiler/main.c` 里**唯一**的 `*.zan` glob 是 `glob_stdlib_dir(stdlib_root, subdir)`（外加 `zan_pkg_find_namespace` 的已装包目录），没有任何以“输入文件所在目录 / 项目根”为根的收集；`git diff dde3b503 HEAD -- src/compiler/main.c` 的删除行里也没有任何 project/input/glob/dir/namespace 相关代码。HEAD 的项目树扫描（`scan_project_namespaces`，由 `project_root_has_manifest` 触发）只登记命名空间**声明**，用途仅是把 `using X;` 判为“同项目引用”从而不报 `ZANPKG_MISSING` 安装建议，从不把文件加进编译。所以单输入编译跨文件引用一直就是部分编译，合同是**多文件工程显式列出全部源**（goldminer README 与所有模板构建脚本都如此写；本轮实测 `zanc .../src/main.zan .../src/Game.zan --auto-stdlib` 272 文件编译出 exe 正常）。**不做**“同目录 *.zan 自动纳入”：`tests/conformance/` 四百余个各带 Main 的用例、`examples/gui_charts/` 等一目录多程序的布局会被立即打破（重复 Main）。**影响面收窄**：仅“单文件编译心智”与文档措辞，模板构建不受影响。

* **游戏引擎三路盘点 + Game.Idle 库落地（2026-09-10，用户"成熟度检查补齐"指令）**：三路 Explore 并行盘点——①**引擎家底**（stdlib/Game）：Foundation（FixedStepClock/InputMap/SceneStack/DeterministicRandom）、Arpg 数据侧（属性/物品/技能/Buff/存档/DM 式 UiRuntime 2380 行/TCP+WS/公式求值器/30+ Easing）、Board（回合+回放）、Cards（deck+战斗）、Scene（.zscene HUD 设计器三端齐备）、音频底层（WASAPI Audio）、资源加密打包（.zrp）全齐；最薄的是**实时表现层与玩法运行时**（粒子/瓦片地图/相机手感/AI/寻路/掉落/任务/浮字）与放置数值库。②**模板实战**：legend（传奇放置，6k 行+51 op 与 server-game 全对齐+GM 后台 24 表）为传奇放置直接可开工起点，缺口=无世界场景走位渲染/NPC 对话/技能联机断裂/音效未接线/PK 组队零实现；ra2（RTS，15k 行，Westwood 格式全家桶+等轴测+BFS 寻路+AI 房）缺口=音频零/无存档/无联机；两旗舰模板零共享代码且**都不用 stdlib/Game 官方库**（断层），下沉候选=CSV 表引擎/版本化存档/Secure 线格式/双通道客户端/离线结算/相机/寻路；放置类无题材无关模板（legend/wuwei 是题材混合体）。③**设计器贯通**：窗口设计器 4/5（自举 21 个 .zform）、.zscene 场景设计器 3/5（功能全但**仓库零实例**）、瓦片/关卡编辑器 0/5、游戏模板用设计器 0/10；GameHud 合成桥只在注释里、两套 UI 栈无合流点、设计产物无运行期装载。**发现交付项**：stdlib/Game/Kit 下 Host/Prims/Text 三个文件是并行 SDL 清退车道（53f73fc9 删 Prims、da902bd4 删零消费方层）删除后**残留在磁盘的无主文件**（`git cat-file -e HEAD:...` 失败、HEAD 无此三件、依赖 `using SDL3`）——留给车道会话确认，本会话不碰（并行在途纪律）。**第一笔补强（Game.Idle 库，放置类第一批）**：stdlib/Game/Idle/ 四件——BigNum（千进制后缀 1.2K..Dc + 千分位分组）、Curves（指数成本 CostNext/CostBulk、RateAt 产出、PowerCurve 逐级复合成长、long 饱和钳制）、Wallet（多资源账本：Define/Add/TrySpend/TrySpendAll/cap 饱和/变更快照轮询（不放 UiEvent，放置结算每秒几百次内联事件烧帧）/存档文本往返/坏数字忽略）、Offline（24h 缺省封顶、时钟回拨安全、60s 静默阈值、逐步 vs 闭式判定）——legend Rules.Bounded(1e9) 钳制与三模板手滚离线结算的库化。**踩坑**：string.Split 返回 List<string> 非 string[]；bool 无 (int) 转换需手写 FlagText；试出 BigNum 首版 tier 双除法 bug（5.7B 实为 5.6B，探针先于我眼红）。**验证**：_scratch/idle_libs/probe.zan 30+ 断言 IDLE_PROBE_OK + tests/conformance/game_idle.zan 四连（conformance/determinism/leakcheck/arcguard）4/4。后续批次按盘点缺口清单推进：Foundation 通用件（版本化存档/相机手感/伤害飘字）→ 粒子+空间网格 → GameHud 桥与设计器-游戏贯通（等有头环境）。

* **H5 路线第二步：GUI wasm 后端落地——wasm32 delegate 形状根修（已完成 2026-09-10）**：批准多端发布路线（H5→微信小游戏→抖音/支付宝/QQ）的「② H5 GUI canvas 后端」里程碑闭环：Zan GUI 程序 `--target wasm32` 编译链接，浏览器 canvas 实机全链路（帧循环+点击+委托触发）验证通过。**根因（wasm32 独有的 delegate/tag 碰撞）**：delegate 值按 zan_abi.h 是"一个指针两形态"——偶数=裸函数指针，奇数=tag 过的堆 closure 记录（bit 0=ZAN_CLOSURE_TAG）。wasm32 没有真实函数指针，裸 fn 实为 **wasm 函数表索引**（小整数，可奇可偶）；奇数索引撞 tag 位 → irgen 把函数表索引解引用成"closure 记录"，fn 槽读到索引处内存 → 用户报的 `null function at App_SafeFrame`。**修法（形状按目标三元组条件化，rule 10 正解而非绕行）**：`target_is_wasm32(g)` 门控（irgen_expr.c）——native 保持历史裸形状零行为变化（C 回调、dispatch 队列契约原样），wasm32 全 delegate 走 tagged closure record：① invoke 处 tag-test 分派（tagged→卸 tag、读 fn 槽、call fn(rec,args…)）；② 静态方法组 wasm 造 thunk 记录（`__zan_mg_<method>` 丢 rec 调 target），thunk 同放 fn+target 双槽保 `E+=M;E-=M` 相等性语义，dtor/retain 跳过 target 槽（是函数非对象）；③ 捕获 lambda wasm 一律 rec-first 签名+记录；④ `(nint)M` C 回调 cast 走 emit_raw_fn_for_cb_cast 取裸指针（GuardCall 契约）；⑤ capture-less lambda native 仍无 rec 形状（is_closure 门控补 `|| target_is_wasm32`）。**C 回调边界两家族语义复现**：call-family（C 调地址：Thread.Start 等）native 裸指针不动；store-family（runtime 持有再调：dispatch 队列）tag-test 收纳两形态。**gui_runtime_wasm.c（新）**：浏览器窗壳——SAB 事件环（host push→pump drain→wq）、无锁 dispatch 队列（单线程 wasm 替 rt_sync 版，retain/release/tag-test 契约同源）、present/title/wait/sleep import；wdisp_release 的 dtor 偏移按 wasm32 指针宽修正（zan_abi.h 的 0/8/16 描述 64 位形状，wasm32 实为 0/4/8）。**rt_wasm.c**：pthread_mutex_* 无操作垫片统一为 POSIX i32 返回签名（wasm-ld 对双形状签名会合成 .L…_bitcast_invalid 陷阱桩，配 irgen_emit.c w32adapt 表项）；**main.c**：WASI 链接的 needs_sync 门——GUI 程序（引用 zan_gui_* 且无线程/原子）不链 rt_sync 拿 gui 内无锁队列。**验证闭环**：① `_scratch/h5gui/main.zan` 探针（动画条+按钮+点击计数）：Chrome cdp 驱动 PROBE-PASS（H5PROBE-START/CLICK 1/DONE frames=3000/EXIT，sample3 orangePx=2304=8 块点击方块，present=3000 零陷阱）；② Node WASI 同探针 START→CLICK 1→EXIT（[UE] pre/post 证明 UiEvent.Post→Dispatcher.Post 全链路）；③ rls3.zan delegate 语义探针（静态 MG ==/!=/invoke/RemoveAt/lambda 相异性/RaiseAll）native/wasm 双目标全过；④ Thread 家族 13 例 conformance 手跑全过（中途回归已归因并修复）；⑤ 基线 A/B（HEAD worktree）：native 输出与 HEAD 逐字节一致、Thread.Start/Display.Monitors 照旧——旧形状零回归。**standard 档归因**：135 失败中 130=并行 Chart 在途编译错（ChartViewMore 引用未声明 lineStyle）、4 例单跑过（并行负载抖动）、jwt exponent-nbf=HEAD 既有 stdlib 回归（`{"nbf":1e2}` numRaw=""→AsString="100" 误过 readNumericDate，基线 zanc 复现，属 Json 车道另案）。**已知限制**：捕获 lambda 过 extern Thread.Start 两代编译器都段错误（stdlib Threading.zan 文档在案"委托必须非捕获"）；GUI 全量编译需 `--stdlib-path` 指向冻结 stdlib 副本以避开并行车道在途改动（wasm32 目标本身 368 文件编译链接无碍）。**下一步**：H5 模板沉淀（SAB/crossOriginIsolation 部署说明）→ ③ 微信小游戏（无 SAB，需自带事件/present 宿主）。

* **A262 NativeMemory.GetString 内建被 extern 借用规则误吞 → 每请求泄一条原始请求头（已修，2026-09-10）**：用户报"内存不应该上涨这么多"后逐层定位。**根因**：`expr_yields_owned_rc_value`（src/compiler/irgen_generics.c）的 7ee37726 "DllImport 返回 string 按借用" 规则把 **NativeMemory.GetString 也吞了**——它是编译器内建（emit_native_memory_call，irgen_expr.c:556 用 emit_string_alloc_rc 建真 ARC 串、memcpy、NUL 收尾，交调用方 +1），[DllImport] 声明只为 checker 提供类型；被误判为借用后，**所有消费点（实参临时、丢弃语句、局部捕获、返回）都不再释放**，每次执行泄一条串。stdlib 中 109 处调用全中招：ByteBuffer.Str（HttpFramer.Head/Slice/ReadBody 的请求头与正文切片）、Stream.ReadLine、MemoryStream/Zip/Keyboard 等——服务端每 HTTP 请求泄一份原始请求头（+正文），RSS 线性上涨：修复前 3×60s keep-alive 登录压测 RSS 13.7→109→210→313 MB；**修复**：extern 借用判定前为 GetString 内建加白名单返回 owned=1。**验证**：Windows 24 探针归零；WSL 交叉 leakprobe2f（2000 keep-alive 请求过 HttpFramer）泄漏 2001→1（余 1 为 async frame 对 this 的 receiver-retain，防 fluent 接收者 use-after-free 的必要所有权，emit_async_complete/cleanup 平衡链完整，属既有语义非缺陷）；重出 server-game 后 3×60s 登录压测 RSS 10.3→12.7→12.9→13.1 MB 趋平（吞吐 ~8.5/s 是登录 handler 12000 轮 SHA256 PBKDF2 的算力上限，非服务端瓶颈，进程 CPU 单核）。回归：新增 tests/conformance/native_memory_getstring（丢弃/循环绑定/实参 temp/返回四消费形态 + leakcheck 双胞胎）；**残留 1 对象**：HttpFramer.zan:67 报点即该 retain，probe57（正确 await）与 probe58（static 持有）均复现同 1 条，定性为设计内所有权非泄漏。**顺带发现（均与本修无关，A/B 双向验证）**：① 7ee37726 的 conformance extern_borrowed_string_result golden 写死了 cwd 长度 19（作者仓库根），ctest --test-dir 会让子进程 cwd=build（25），已改为环境无关断言（非空+含分隔符+两次读取一致）；② checkbox_group leakcheck 1109 对象、ws/mqtt Worker 静态表泄漏、jwt_hs256 exponent-nbf（c7791e11 JsonTape 数字不再保留原始字面量后 AsString('1e2')→'100'，golden 08-03 就已失效）均为存量问题待修。

* **SDL3 终局移除收官（2026-09-10，用户"SDL3移除了关联的都移除啊"指令 + 三项拍板：ra2 彻底退役/tests-ra2 一并删除/无主文件直接删）**：①**删除面**（d7e2126e，-38429）：stdlib/SDL3 全树 44 文件（绑定+native 桥+9 平台驱动 bundle+LICENSE）、templates/game/ra2 全模板 36 文件、tests/ra2 46 文件、tests/gui/spritebatch_test（测 SdlSpriteBatch，非 golden 档）、tests/runtime/gui_sdl_smoke.c、toolchain/apk-shell/java/SDL3-LICENSE.txt、scripts/build_game_board_classics.ps1 与 build_legend_hud.ps1（SDL 时代死脚本，目标目录已不存在）；②**CMake/workflow**（0bf3950a 同笔带注释清理）：CMakeLists 删 _rts_stdlib_srcs 注入与 rts_ 分派（conformance rts_* 五件随之退役——被测代码 Ra2.StdlibRts.Formats 只在 tests/ra2，金样档不再可编）、drivers.yml 删 SDL3 构建/装运步骤与 brew/msys2 依赖与路径触发与校验和（deps/checksums.txt sdl.tar.gz 条目删）、release.yml 去 ZAN_BUILD_SDL3；③**文档/站点**：zan-site ref-data 140ns 重提取（api_extract 重跑）+ index/导航 136 块/stdlib 表 SDL3 行/examples audio 行（改 System.Audio）/ai 页清单/stdlib.md+stdlib.html Foundation 行改"Gui 宿主"/Game.Core SdlGpu App 成员消失/Gui.Backend AdoptSdlWindow 等措辞、gui.html/gui.md 镜像重生成（顺带吸收 d2d0ce35 的 flex 文档未重生成欠账）；IDE：AiAgent GAME 提示改"Gui host owns window/render/input/timing"、Workspace/Paths SDLActivity→NativeActivity 措辞、服务器模板 Index 文案、Skin/Lua/PixelFont 可选原生依赖措辞、zan_audio/ohos 头注释去 SDL 对比、CMake HAP/APK 注释、ZanApp/ZanWeb java 注释；④**并行会话踩坑（重要）**：工作树共享下，删除任务被三条并行车道反复干扰——(a) 我 staged 的删除 index 被并行会话 commit 1aeeac14 连带覆盖（其 git 操作把 stdlib/SDL3/templates-ra2/tests-ra2 工作区文件还原回来）；(b) ra2-work 分支被建且 checkout，main/工作树在删除提交窗口期被反复重置+还原，曾出现"删了又回来"三轮；(c) 12168a25 一度把 126 个复活文件误带入提交（8f624aab 按 c743b3bf..12168a25 的 A 清单整体再删恢复终态）。**教训**：删除型任务在多车道共享树上必须"stage+commit 立即成对、push 前重查 HEAD 树"；`git rm -f` 遇工作区被并发还原的文件需 `-f` 强删；验证 HEAD 树用 `git ls-tree HEAD <path>` 而非看磁盘。⑤**验证**：九游戏模板编译全绿（breakout/ddz/gomoku/weiqi/xiangqi/snake 单输入 + goldminer/wuwei/legend 显式多输入）、Game conformance 9/9（game_idle/anim/board/cards/foundation/scene/arpg）、conformance_chart 26/26、policy_zform/policy_sdk/gui 切片全绿、cmake 重配后测试注册不含 ra2/rts。**至此 SDL3 从代码/构建/测试/文档/站点/工作流全生态退场**，图形外壳只剩各平台原生壳（Win32/X11/Cocoa/Android NativeActivity/OHOS XComponent/WASM SAB）。

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

* **A264 登录提速：NativeMemory.Sha256 内建 + Hex.Encode 重写 + stdlib 加解密全面换装 OpenSSL EVP（2026-09-10，用户指令"教学就放到示例里去，其他使用到加解密的应该换的全部换掉"）**：三条腿。①**Sha256 内建**（照 Crc32 自包含模式）：irgen_expr.c 新增 nm_sha256_fn（K 常数编译期全局数组、FIPS 180-4），emit_native_memory_call 分支 + expr_yields_owned_rc_value 内建白名单补 Sha256（防 A262 借用误吞复刻）+ builtin_api.c 表项 + stdlib/System/NativeMemory.zan 声明；回归 tests/conformance/native_memory_sha256（NIST 向量：空串/abc/56 字节边界/两块）。②**Hex.Encode 重写**：去 StringBuilder/Substring，预分配 2×len 按 nibble 直写。③**Aes/AesGcm/Sm4 换装 EVP**（stdlib/System/Security/Cryptography/）：纯 Zan 教学实现原样移入 examples/crypto_reference/（三件+README，性能账 2MiB/s vs 196MiB/s）；stdlib 版走 libcrypto EVP（驱动已随 Tls 装运，零新增依赖），API 签名不变（~28 调用点零改动：WXBizMsgCrypt/WechatPay/ResourcePack/模板 Net/Save/Secure）。**三条字节串契约（踩坑实录）**：(a) byte[] 按 string 形参传入时 .Length=strlen（0x00 首字节→0），旧实现从不读 .Length 只索引——新代码加 .Length 门禁会把合法密钥判空，缓冲参数一律只判 null；(b) GetString 产物写字节会失效长度缓存（emit_string_len_invalidate 后按 strlen 重derive，NUL 内容坍缩→越界），tag 类编组必须走 byte[]，仅在 extern 边界 `string tagBuf = tagB` 零拷转；(c) 发明 DllImport 名不存在，同名异形参用 `EntryPoint` 别名（EVP_EncryptUpdateS←EVP_EncryptUpdate，string 零拷入）；ECB 单块解密要 set_padding(0)（PKCS#7 默认吞末块）；EncryptCbc 返回精确长度数组（byte_buffer golden 断言 ct.Length==32，len+32 会破档）；调用方对 outLen List<int> 须预 Add(0)（空表写下标 fail-soft 报错但继续）。**验证**：16 检探针（FIPS-197 128/256、CBC 往返+坏 pad 拒绝、CTR 半块、GCM KAT case3/case4+篡改拒绝+AAD+256 往返、SM4 GB/T 向量+CBC）全过；既有 aes_gcm/byte_buffer/rsa_oaep_cbc/crypto_digests/sdk_wechat_crypt/sdk_wechat_tenpay/respack_roundtrip/static_publish_tls_stub 全绿；吞吐 CBC 2→196 MiB/s（零拷入后；raw EVP 1250-1666），GCM token 103→3.6 µs。④**模板 PasswordHash byte[] 链**（server-game/mvc/collab/licensing 四份 Auth.zan）：12000 轮 124ms→11ms（首轮 string 材料后续 byte[]+salt 直取）。GCM 语义差异：教学版 tagOut 不足 16 字节报错，stdlib 版截断填 16——README 已记。

* **A265 server-game 多 worker 改造：跨 worker 状态上匿名共享表 + 世界单写者 + ops 表跨进程中继（2026-09-10，用户指令"做多 worker 改造，信息通过共享内存表即可，像 workerman 的数据共享服务"）**：架构=**HTTP 接入层水平扩 + 游戏世界单写者**。[worker].count>1（仅 Linux，Windows 连接分发模型下游戏 TCP 无法固定到世界 worker，main.zan 保持钳制 1 并提示）时 N 个 worker 各自完整跑 main：master 建 GameShared 五张匿名表（PermTable 模式：tokens=登录令牌 token→accId+TTL 10 天、auth=管理端 tokenVersion 缓存 TTL 30s、sys=cid 发号器+RPC 序号、online=在线计数 total/r<realmId>/p<playerId>、ops=跨进程操作通道 req 2K/resp 16K），句柄 ZAN_SHARE_* 下发；1 号 worker 是世界角色（WorldHere()=WorkerId()==1）：独占 World.Start（tick/刷怪/落库/会话字典）与 Gateway.Start（游戏 TCP 监听只在世界进程——推送连接因此全部本地，AnnounceAll/顶号 kick 原路径直发，Fight/Play 的几十处推送点零改动）。**跨 worker 三通道**：①登录算力分摊——CheckPassword（11ms KDF）留在受理 HTTP worker，会话登记经 ops 表 RPC 给世界（World.EnsureHttpSession：顶号+入表+发号，往返 ~2.5ms）；②世界类 op（进区后全部）——GameApi.Run 认 token（tokens 表跨 worker 可读）+限流后，非世界 worker 整单投 ops 表（gop+acc 进 req JSON），世界 OpLoop 2ms 轮询序号顺序执行 Gateway.Op 原代码、resp 写回同 row；③管理页写动作/在线清单——GmKick/GmBroadcast/GmAnnounce/SaveOnlinePlayer/Sessions/LivePlayer 六帮手，世界角色进程内直调（count==1 零开销）否则中继；在线数读共享计数（Gateway hb/realms、管理页、Index 全部换 GameShared.OnlineTotal/OnlineRealm）。count==1 全部旁路：五表照建（建表者即使用者），行为与改造前一致。**两个根因级坑**：(a) RPC 协议先 Increment 占序号再写行，世界进程在两步之间读到新序号会把请求整体跳过（last 指针单调前进不回头）——ServeOne 对缺行限时重试 5×2ms 封死窗口；(b) 初版世界侧从 ops 行的 acc 列读账号 id 而调用方只写进了 req JSON——所有中继请求以 accountId=0 执行、中继登录把会话全登记到账号 0（跨账号互顶！），且世界 worker 本机直调的请求一半正常一半坏，极易误判为偶发——协议字段必须单一来源。**验证**（WSL 实测，sqlite 池 8）：count=1 登录 63.5/s（8 并发，池上限）vs count=4 136/s（同 8 并发）→178/s（24 并发）=2.8×（SQLite 单写成为共享瓶颈，诚实口径）；中继 op 12/12 正确（含世界内语义错误透传）、TCP attach+12 条跨 worker GM 广播 12/12 到达推送连接、跨 worker 重登顶号旧会话当场落库、hb/realms 共享在线数一致、3×60s RSS 86.7→87.7MB 趋平（5 进程合计）；Windows count=1 全链路回归（注册/登录/建角/进区/hunt/attach/GM 广播推送/管理页在线清单）全绿。**遗留**：旧 token 在重登后仍可解析到账号（session 粒度校验），单进程时代即有，非本批引入；多 worker 下 SQLite 跨进程写吞吐封顶，突破需换 pq/mysql（模板 [database] 已支持）。

* **A266 stdlib 加解密吞吐：wrapper 输出侧零拷贝（用户质疑"不可能只有100多，几个G的速度"成立）（2026-09-10）**：A264 之后 wrapper 实测各 cipher、各尺寸一律 ~180-220 MiB/s 平板——与算法无关即与 EVP 无关，瓶颈是 A264 只优化了输入侧（零拷入 string），输出侧仍是"len+32 上界分配 + 逐字节 Zan 收紧拷贝"（那条循环本身 ~130 MiB/s，1 MiB ≈ 8ms）。本机 raw EVP 实测（缓冲+ctx 复用）：AES-128-CBC ~1855-2015、AES-256-CBC ~1333-1505、AES-256-GCM ~3160-6095 MiB/s——AES-NI 确实 GB/s 级（GCM 5 GiB/s+），用户预期正确。修法（Aes/AesGcm/Sm4 三类）：①CBC 加密的 PKCS#7 密文长度加密前即确定（(len/16+1)*16）→ 精确分配、EVP 直接写入、原样返回（guard total==outb.Length）；②CBC 解密剥填充会缩（total=len-padlen，仅 Final 后可知）→ len 上界分配，未缩原样返回，缩则一次 native memcpy 收缩（BufCopy=[DllImport("crt",EntryPoint="memcpy")] byte[] 形参直传载荷指针，File.EmbedCopyIn 同款）；③GCM 流式密文与明文等长 → 精确分配原样返回（双向），顺删 Encrypt/Decrypt 里 aadB 逐字节拷贝死代码（拷完从未传给 EVP，aad 本就以零拷贝 string 过边界）；④Sm4.CryptCbcNoPad 的 len+16 收紧为精确 len，EncryptBlock/DecryptBlock 直接返回（少一次 16B 拷贝），Sm4.EncryptCbc 长度契约修正为返回 .Length==outLen[0]（原实现带 len+32 尾巴，库内无调用方）。**修后实测**（同机同探针 _scratch 口径）：CBC-256 enc 200→1267-1319（≈raw 的 88-96%）、CBC-128 enc 187→1641、CBC-256 dec 223→1790-4129、GCM-256 enc 200→2000-4413；256B 小调用 CBC 2.02→1.04 µs/次、GCM 2.53→1.23 µs/次。**验证**：conformance aes_gcm/byte_buffer/sdk_wechat_crypt/sdk_wechat_tenpay/respack_roundtrip/crypto_digests/native_memory_sha256 全绿（末档仅 golden 行尾 LF/CRLF 差异，语义相同）。**剩余开销**：每调用 ~1 µs（EVP_CIPHER_CTX_new/free + 2-3 次 Init + nint cell + 小分配）——小缓冲热点如需再压可做 ctx 线程本地复用，当前无场景需要。

* **legend 迷你传奇 · PK竞技场页（页 1）+ 顶栏导航带两端配平（2026-09-10，用户"可以下一个窗口了"指令）**：①**页 1 落地**（新 `src/PageArena.zan` + `Spec.zan` 的 arena 常量块 + skin 的 `.arena-*` 段）：8×2 头像阵（卡 71×71 逻辑、列距 21、行距 26，前 3 名头像框金/品红/青）、左战斗日志（5 行 + 橙字提示贴底）、右战功面板（我的排名/挑战次数/挑战时间/累计战功 + 领取战功/刷新数据），数据走 `net.PlayArena()` 的 `rows`，离线回落内置 16 人演示名次。②**导航带两端配平**（用户三轮口径的最终定式，已写进 `REFERENCE_AUDIT.md` §11.2）：格间隙一律保持实测值（撑条 flex-grow 0），行首/行末各留 `LeftPad`，`.nav-row { display: flex }` + `.navtile { flex-grow: 1 }` 把剩余宽度平摊给各瓦片（按钮各宽几个设备像素）→ 顶栏两排与底排右缘同列（实测 1507..1509，聊天面板 1513，左端 4）；设置齿轮 `flex-grow: 0` 保持正方形。**坑**：`Panel.Row()` 默认是停靠布局，容器不写 `display: flex` 时子控件 `flex-grow` 完全无效（踩空一次）。**验证**：legend 模板编译 + 离线壳截图逐项量取（卡宽/列距/pitch 与原版逐项一致，导航带间隙逐条不变）。
* **legend 服务端缺口（页 1 相关，未做）**：`templates/server/server-game/src/Game/Play.zan::Arena` 只允许 `rival 1..3`（客户端是 16 格），`ArenaState` 无 `rows` 战功榜与 `myRank`（客户端已容忍缺省 → 回落演示名单）；`arena.time`（挑战时间）与「领取战功」op 服务端均未提供（战功按小时结算的语义只在客户端提示里）；另 `TowerRivals` 用 `await Gateway.Db()`，`Arena` 若补 rows 应照该模式写 async。

* **A267 gui_3d_demo 鸿蒙窗口自适应收口 + OHOS/Android 3D 性能定性（2026-09-11，用户报"鸿蒙下的窗口全屏自适应有点问题/全屏主体没有自适应"与"复杂动画 CPU GPU 内存是否爆表"）**：①**窗口自适应根修（gui_runtime_ohos.c）**：2in1 浮窗拖拽/最大化**不销毁重建** XComponent surface，驱动原本只在 surface 指针变化（旋转）时重建呈现通道 → 同指针 resize 后永远呈现旧尺寸缓冲，右侧黑带。修两处：(a) present 的 EGL 路径重建条件从 `surf_nw != nw` 扩为 **指针变化 或 记账尺寸 ≠ attach 尺寸**（struct 加 surf_w/surf_h，旋转/2in1 resize 同一判据，不动 NativeWindow 快路径与 2D 语义）；(b) 深坑——**单次大尺寸 snap 最大化**时重建瞬间原生窗口 buffer geometry 可能未落定，`eglCreateWindowSurface` 实际拿到旧尺寸（2090 宽）surface，而记账记的是预期值 w->w（3120）→ 账面吻合永不再重建、黑带永驻（拖拽 resize 因最后一次 surface-changed 在 geometry 落定后到来而幸免）。修法：创建后 `eglQuerySurface` 记录**实际**尺寸，落定竞态由下一帧的重建判据自愈。**实机验证**（MateBook Pro 模拟器，收紧深藏青区间判读）：拖拽 [515,342,2535,1394] 内容铺满 x 515→3045；snap 最大化 rect [0 0 3120 2077] + hilog "surface changed 3120 x 2007" → max3.jpeg 深藏青铺满全屏（六条扫描线 rightmost=3119，覆盖率 95.8%）；还原 [515 380 2090 1394] → 内容铺满 x 515→2604（w8.jpeg），双向多次往返稳定。壳侧 `_scratch/hap-demo/entry/src/main/cpp/xcomp.cpp` OnSurfaceChanged 查 GetXComponentSize 后转发 attach（推 kind-7 resize + kind-14 整帧重绘）。②**3D 性能定性**：`src/runtime/gui_gl_context.c` 平台臂只有 WGL(_WIN32)/GLX(linux 非 android 非 ohos)/Metal(__APPLE__)，**Android/OHOS 落 #else 空 stub——GPU 后端永不安装**，3D 全程 CPU 软件光栅 + 每帧整幅 texture 上传（浮窗 2090×1324 ≈ 11.1 MB/帧，最大化 3120×2007 ≈ 25 MB/帧，60fps 即 0.7-1.5 GB/s 内存带宽）。模拟器实测（3D demo 动画中）：应用进程 ~35% 单核 + render_service ~63% 单核（模拟器 DGLES 走 host GPU 但我们的上传是 CPU memcpy）；内存 PSS 98.7 MB 稳定不涨（ResizeSurface 重分配非泄漏）；帧时长 median 15ms / p90 19ms / max 51ms。真机上 GPU 后端缺失 + 软件光栅 + 整幅上传三重叠加才是"复杂动画爆表"的根。**挂账（另案）**：gui_gl_context.c 补 EGL 臂（Android EGL + OHOS EGL，3D 复用驱动已链的 libEGL）；`zan_gui_toggle_maximize` 在 Android/OHOS 均为空桩 return 1（自绘 chrome 最大化按钮死路，系统 ArkUI caption 可用）；`zan_gui_titlebar_height()` 返回 0。

* **A268 server-game 跨 worker 中继换代：ops 表 2ms 轮询 → 环回 TCP 控制通道（GatewayWorker 同构）+ 稳定性/性能实测（2026-09-11，用户指令"用我们最高性能的方式去完成，然后在做稳定性性能测试"）**：①**通道设计**（GameShared.zan，替代 A265 的 ops 表中继）：世界进程 RpcLoop 从 `Cfg.Server.port+1` 起扫 64 个端口取第一个空闲（facade 无 getsockname，实际端口写 sys 表 "rpc" 行作服务发现；HTTP worker 侧 DiscoverRpcPort 读表缓存，连接失败失效缓存重发现），每请求一条环回短连接、换行分帧紧凑 JSON（ToJson 保证无 \n；rpc/acc 字段单一来源随请求体），ServeConn 逐行 FIFO 执行（同连接严格保序），8s 空闲/对端关闭即断。两轮重试覆盖世界重启换端口；部分写（SendAsync 返回 < len）按帧截断处理换新连接重试——`>= 0` 判成功是对 SendAsync 契约的误读，短连接下必然丢帧挂死调用方。count==1 全部旁路（不监听）。RTT 0.1-0.3ms，替代 ops 表 2-6ms 轮询（写表+轮询读+序号协议三段开销归零）。②**实测**（WSL count=4，sqlite 池 8）：8 并发登录突发 299-330/s（A265 ops 表 178/s 的 1.7-1.9×），p50 22-24ms；60s 持续 199-202/s、错误仅限流器拒击（0 超时 0 异常）；功能矩阵全绿（登录中继/token 解析/跨 worker 顶号 old_dead/TCP attach/管理页 GM 广播 12/12/hb 共享在线数；"请先选择区服进入"是未 enter 的预期业务透传）。soak 后通道功能不劣化。③**稳定性测试揪出两个预存缺陷（均非本通道引入，count=1 复现）**：(a)**tokens 表 8192 容量静默溢出（已修）**——A265 把 TokenBox 搬进固定容量共享表后，`TokenPut` 丢弃 SetInt 返回值，表满后签出的 token 全部解析不到而登录仍返回 ok=1（实测 36k 登录打穿后新 token 立刻 "登录已过期"；8192 ÷ 峰值日登录数在真实运营下数小时即满）。修：容量 8192→131072（约 1.3 万日登录 × 10 天 TTL，共享段 ~13MB）+ TokenPut 返回写入结果 + Issue 失败返回空串 + Gateway 两处调用点空串即 "登录服务繁忙" 响亮失败——**共享表容量要按「峰值日写入量 × TTL 天数」给足，写入返回值不可丢**。(b)**协程就绪队列被轮询帧淹没（挂账，运行时层）**——高并发突发下请求子集停摆 13-24s 后自愈，bistable（同负载要么全飞 270-398/s 要么塌到 8/s）。证据链：count=1 单进程单池复现（非通道/中继问题）；[phase] handler 段 11.9-17.7s 而 SQL（[slow-sql] 零）、RPC（[gameshared] 异常零）、响应写出（0-3µs）全快；[pool]/[dbpool] 探针：池等待 14.3s 期间 `evicted=0 gates≈558-563`（ fuse 数学 64×250ms+500×20ms 吻合），且有 `idle=1..7 waiting=0` 的采样——**空闲连接在场却无人领取、唯一等待者 299ms 才被看门狗救起** = 连接池/门/双重归还全部清白（18 处 Db/Give 全 try/finally 配平，Gate FIFO+盈余计数语义 rt_io.c 审阅无误），就绪队列 rt_co.c 为干净 FIFO 环也无病态；机制判定：Socket.RecvAsync 的 1→16ms select-poll 让每个在途读按 1ms 周期生成就绪帧，24 并发 × 1ms ≈ 2.4 万帧/s 灌单线程就绪队列，每次 await 跳变排队数十 ms，池饱和重试循环把每请求跳变数放大 ~40 倍，15:1 认购比下秒级延迟→客户端 15s 超时；负载持续时自发型卡死（soak 第 3 轮半速、第 4 轮卡死后 RSS 骤降自愈）与突发型同源。修向：RecvAsync 改事件驱动（可读事件登记 epoll，删除 select-poll 循环），池等待者改为带预留的交接（Pool 归还直交 FIFO 头而非入 idle 任人抢）。附带观察：登录负载下 RSS ~7KB/请求斜率增长（105→207→253MB/各 12k 请求），空闲 60s 平台不回落、fd 恒 33 无 TIME_WAIT 堆积——retained 行（tokens/auth/限流表惰性 TTL）与分配器驻留占比未分层，随 (b) 修复后复测。④**长时混合协议 soak 揪出并修复顶号双缺陷（同日深化，用户指令"长时间运行同时有各种测试的稳定性，各种协议"；harness=12 bot HTTP 全 op + TCP attach 推送 + admin 页 + GM 广播 + 聊天 fanout，RSS/fd 纯 /proc 采样——多线程 python 里 subprocess 采样会小概率 fork 死锁）**：首轮 190s 18,312 请求零错误，但 kick=12 而 drops 直方图 633、gm 送达 15-17/58——顶号后旧推送连接成收不到任何推送的孤儿。两段根因、两段修（World.zan EnsureHttpSession）：(a) 顶号只 Leave 不通知不拆通道——旧 TCP 连接挂着死会话，客户端只能等自己的空闲超时；修=Send kick 事件 + conn.Close()，与 Enter/Kick/Sweep 三处顶号形态对齐。(b) 修完 (a) 后 attached 仍 8/12——**先 kick 后 `await Leave`（几十 ms DB 写）再登记新会话**，客户端收 kick 零延迟重连 attach，恰好掉进「旧会话已摘、新会话未登记」的缝，attach 答「登录已过期」，通道从此静默聋。定位链：drop 直方图异常→world 日志按探针 cid 切段（login/enter/leave 全配平，排除表腐坏）→attach 回复逐条记日志→「登录已过期」12/12 必现→轻载探针不复现（前一步 HTTP RTT 盖过缝）只有压测必现——缝隙类缺陷以时序为变量，压测是唯一复现手段。修=**先登记后拆通道**：建会话+byCid/byAccount 换绑+kicked 标记全部完成后才 kick+close+Leave，登记与踢线之间无 await（单线程调度下外部看不到中间态）；Leave 的 byAccount cid 守卫保证摘旧会话不误摘新登记。修后 3 分钟 soak：attach 24/24、gm 6/6、kick=drops=12 配平；30 分钟 soak：~2 万请求零错误零停摆、gm 57/57、kick=180=drops=180、attached 12/12、聊天 min 1173、RSS 122MB、fds ~239 持平、p95 个位数 ms。协议口径澄清：hb 是匿名 op，不保活 httpOwned 会话（保活靠 authed HTTP op）；README 写的 ev online 推送在代码里不存在（只有 op 响应字段）。e2e 同步 114→125 断言：新增 httpOwned attach→relogin kick→零延迟重挂→GM 送达四连，外加世界 TCP 监听滞后 HTTP 的 15s 启动重试与跨 worker 读你写缝隙（A302）的重试容忍。

* **A271 编译器/运行时/stdlib 全量代码审计（2026-09-11，六路并行代理 + 逐条最小探针复验）**：范围=词法/语法/检查器、irgen（核心/调用/泛型/ARC）、runtime（内存/字符串头/文件/计时/调度/IO/同步）、stdlib 安全面。**探针全在 `_scratch/audit2/`（先看 README.md）**。**已证伪两条，勿再登记**：① 「`await Task.Delay(long.MaxValue)` 有符号溢出→立即触发」不成立（探针 `delaymax2.zan`：打印 start 后睡死，5s 超时仍未醒；`rt_sched.c:291` 的加法溢出是 UB 但当前无可观察误行为）；② 「`irgen_emit.c` 的 `fields[32]/names[32]` 是缓冲区溢出」不成立（1802-1803、1983-1984 都有 `<32` 守卫，是**静默截断**，names 侧无可见症状；fields 侧仅 A281 那一种形状暴露）。以下 A272-A291 均为探针实测或读码确证项，按严重度排列。**2026-09-11 晚本轮修复：A272-A286、A288、A289 全部，A290 的转义半，A291 的 ③④⑥⑦，另新增 A294（已修）；续轮再修 A295/A296/A297/A301 与 A299/A300（详见各「已修」摘要行）；仍留 A287、A290 的 seed 半、A291 的 ①⑧、A292、A302×2（见各条；A291②⑤、A298 已于 2026-09-12 补修，见各条）；A293 已修（见该条）。**本轮收尾在 standard 档（785 项）里又归因出 A295/A296/A297/A301（均已修：JSON 数字 token 字面量、arpg Link 判空、HttpsServer 实例调用、Pinyin CRLF）、A298（三个 forwarder 用例 flaky，未修）、A299（selfhost null-safety，已修）、A300（TaskJoin 拉入假阴性，根因=seed 扫描器 chain 双重清空，已修），另登记 A302（tunnel leakcheck 仍可达，未修），并确认 4 项 Gui/GL 红（pagination/transfer/listview 哑停/runtime_gui_gl_3d 目标未构建）来自并行会话在途改动；本树 standard 档当前无法全绿，逐条见尾注。****

* **A272-A283、A288、A289 已修（2026-09-11 审计修复，一行摘要）**：A272 方法体内裸名实例属性赋值让 zanc 段错误（irgen 属性写分支接收者槽误用）；A273 `static async` 返回结构体字段读回 0/垃圾；A274 裸名静态属性读静默返回 0；A275 ≥2^63 十进制字面量静默变负；A276 类无构造函数时 `new C(args)` 静默丢实参；A277 17+ 参数 extern 按裸名调用（编译器栈越界写）；A278 同行 `#endif` 被吞掉致文件剩余内容消失；A279 `Encoding.UrlDecode` 丢 `%00` 及其后内容；A280 深右递归表达式让编译器静默栈溢出（补诊断）；A281 generic 类第 33 个 T 型字段起静默截断 → 误导性 unresolved call；A282 条件编译嵌套 ≥32 越界写 cond_stack/cond_seen_true；A283 字面量/维度表的静默截断与误导诊断（ranks[16] 等）；A288 若干部件的长度解析无上限/可回绕（HttpFramer 分块、头部长度等）；A289 Cookie 域校验缺失与请求头注入（CookieJar/HttpClient）。
* **A284-A285 已修（2026-09-11 运行时修复，一行摘要）**：A284 同一 handle 上并发读与 Close 的 `FILE*` use-after-free（rt_file.c 生命周期）；A285 Windows IOCP 多 worker blocking 完成项丢失后无第二条投递路径 → 永久挂死（补投递路径+登记唤醒包偶发丢失观察）。
* **A286 已修（2026-09-11 安全修复，一行摘要）**：SSRF 判定漏 IPv6 过渡/兼容地址（rt_io.c IPv6 分支补 ULA/link-local/映射地址之外的过渡段拦截）。
* **A287 [P2/安全] 数据面三条明文凭据通道（2026-09-11 审计，读码确证）**：① MySQL 连接器**完全没有 TLS**——`stdlib/System/Data/MySql/*.zan` 无任何 `Tls` 引用，`MySqlConnection.zan:268/282` 的 `doConnect` 直连后从不设置 `CLIENT_SSL`、从不发起 TLS 协商；`caching_sha2_password` 的 full-auth 路径（`:238 fullAuthCipher`）在明文信道上向服务器索要 RSA 公钥（`pemToDer` 无信任锚/指纹校验），主动中间人替换公钥即可解出口令，`mysql_native_password` 的 scramble 可离线爆破。② `System/Data/SqlServer/SqlServerConnection.zan:175-189` 只发 `ENCRYPT_NOT_SUP`、遇 `ENCRYPT_REQ` 直接拒绝——即只能「关闭加密」（拒绝强加密是好的，没有静默降级），`TdsMessage.zan:196` 的口令只是 0xA5 轮转混淆。③ `System/Data/Redis/RedisClient.zan:344` 直接以参数发 `AUTH`，全流量明文。修法方向：MySQL 至少支持 `CLIENT_SSL`（要么实现 TLS 握手、要么把「无 TLS」在文档/API 上标成显式拒绝跨网使用）；TDS/Redis 同此。　**仍留**：MySQL 无 CLIENT_SSL/TLS 握手、TDS 明文混淆、Redis 明文 AUTH 三条数据面凭据通道，修它等于在三个连接器里实现 TLS 客户端（握手/证书链/SNI/信任锚），属功能规模而非缺陷修补；本轮未动。

* **A290 [P2/安全] 已修 `pack_script` 的密钥种子明文随产物交付（2026-09-11 审计，读码确证）**：`tools/pack_script.zan:50-51` 默认 seed `"zan-default-pack-seed"`、`:107` 把 `SeedHex`（seed 的十六进制）与密文/IV 并列写进生成的 `.zan`、`:127-129` 运行期由同一 seed 现场派生 AES 密钥——任何人 `grep SeedHex` 即可复算密钥还原 Lua 源码（无需逆向），默认 seed 更让所有未显式传 seed 的产物共用一个已知密钥、可互相解密。这不是「客户端加密挡不住逆向」的取舍结论，而是实现缺陷：密钥材料不能与被保护数据同文件。同文件同类代码生成缺陷：`:97` 把 `inPath` 原样插进 `//` 注释、`:104` 把 `chunkName` 原样插进字符串字面量，含换行/引号即破坏生成源码。修法：seed 不入产物（构建期外部注入或交互输入），生成的常量串做转义。　**修（仅转义半）**：新增 EscapeLiteral——反斜杠写成两个反斜杠、双引号写成 \"、换行/回车/TAB/NUL 分别写成 \n / \r / \t / \0、其余 C0 与 DEL 写成 \u00HH 形式——以及 CommentSafe（控制字符换空格），接入生成文件的注释（inPath）与 ChunkName 字面量。实测：chunk 名含双引号、反斜杠、TAB、换行、0x01 时生成的 .zan 仍可编译（rt.exe 构建成功），DecryptChunk() 经同一 AAD 认证解出 21 字节、与输入 cmp 逐字节相同，ChunkName() 与原始名 cmp 相同。**仍留**：seed 仍随产物交付（SeedHex 与密文同文件，grep 即可复算密钥）——修它要引入构建期外部注入/交互输入的契约变更，需先确认接口形态；CommentSafe 的触发输入（路径含控制字符）被 Windows 文件系统拒绝、POSIX 上才有实害，故该半为防御性守卫、本机不可实证。

* **A291 [P3] 读码确证、未复现的其余项（2026-09-11 审计）**：① Lua 桥无沙箱且按裸库名加载（`System/Scripting/Lua.zan:481` 无条件 `luaL_openlibs`，`:580-610` 依次 dlopen `lua54.dll` 等裸名——脚本可 `os.execute` 逃逸宿主，可写目录放同名 DLL 即劫持）。② Worker 控制口令牌可预测且记录文件世界可读（`System/Net/Worker.zan:638-643` 令牌=`<pid>-<unix秒>`；`:590-599` 记录文件 `File.WriteAllText` 默认权限写进 temp）。③ `rt_mem.c:436-457` 跨线程 double-free 检测是读-改非原子竞态（两线程同 free 可能都过检）。④ `rt_timer.c:598-620` `zan_timer_delay` 的 `heap_push` 失败分支漏 `free(entry)`（同文件 `:670-674`、`:748-754` 都 free 了）。⑤ `rt_io.c:240-242` 等待者表按 fd 号索引 + `io_fd_usable` 用 `fcntl(F_GETFD)` 判活 → close 后 fd 复用会把老 waiter 串到新 fd（跨连接数据错投，需时序构造）。⑥ `src/common/rpc.c:90` `(int)(content_length - got)` 截断：`max_len <= 0`（rpc.h:38-39 契约允许）时恶意 `Content-Length` 可达堆溢出写；树内 lsp/dap 都传 64 MiB 上限，故**不可达**（公开 API 的潜在缺陷）。⑦ `src/runtime/zan_inflate.c:37` 长度校验是死代码（前缀 `len & 0x4000000000000000` 判定后 `comp_len` 是 u32，`(uint64_t)comp_len + 8 > len` 恒假；正确写法 `len & ~0x4000000000000000ULL`），当前 payload 由编译器烘焙故无实害。⑧ `System/Data/Orm/QueryBuilder.zan:486` 等裸列/裸条件入口（`BuildSelectParams(columns)` 原样 Append，`Where/OrWhere/Having/Join on` 接受整段 SQL）——标识符与值绑定已守规矩，但把不可信输入当 `columns` 传即注入，API 未加约束（按设计，需调用方纪律）。⑨ A258 复核：`irgen_builtins.c:772-829` 当前实现是 find → `cnt--` → `icnt=0` 失效哈希索引 + `irgen_call.c:4109-4240` memmove 保插入序，**描述里的线性探测删除死循环在当前源码已不存在**（只剩 `:754-771`、`:4101-4107` 的陈旧注释在讲旧设计）——建议下次顺手清注释，勿再按旧描述修。　**修（③④⑥⑦，⑤为 2026-09-12 续轮）**：③rt_mem 跨线程 double-free 检测改 __atomic_compare_exchange_n（GCC C11 无 stdbool，weak 参数用 0）；④rt_timer zan_timer_delay 的 heap_push 失败分支补 free(entry)；⑥src/common/rpc.c 的 (int)(content_length - got) 截断改为 INT_MAX 上限判定；⑦zan_inflate 长度校验改 len & ~0x4000000000000000ULL（原 & 0x4000... 使校验恒假）。**⑤新增 `zan_io_close_notify(fd)` 钩子**：Socket.Close 在 close/closesocket **之前**调用，按后端把挂在 fd 上的就绪等待者以「对端关闭」形态（recv 0 / accept -1）唤醒并摘除注册——epoll/kqueue 走 EPOLL_CTL_DEL/EV_DELETE 后 io_take 双轮收割，select 回退走 g_io_entries 链表 io_mark_dead+io_flush_dead，Windows 无操作（CancelIoEx 已让挂起 overlapped 以 0 字节完成；红线基线证实 fd 复用探针加不加钩子输出一致，可观察价值在 POSIX），wasm32 空桩；单一收口点覆盖 Socket.Close 全部调用点，不需要句柄表重构。探针 `_scratch/fixv/close_notify_probe.zan`（conn1 挂 Receive 等待者→close→conn2 复用 fd→等待者得 EOF 而非串数据）3 次全绿；http_forwarder_tunnel / http_client_keepalive / http_server_stress 回归通过。**②Worker 令牌随机化 + 记录文件 0600（2026-09-12 续轮）**：CtlToken 改 CSPRNG 16 字节 32 hex（根命名空间 RandomNumberGenerator，勿 using Cryptography 整目录拉入；CSPRNG 不可用退回 pid-秒——令牌只做 PONG 身份甄别、不是控制命令鉴权凭据，PING/STOP/RELOAD/STATUS 本就不验令牌）；RecordControlPort 写完记录文件后 POSIX chmod 0600（DllImport crt chmod；Windows %TEMP% 在用户 profile 下 ACL 天然仅本用户，无需处理），hex 编码手写 ByteHex 避免拉入。A/B 证据（_scratch/fixv/worker_token_probe.zan，master+2 worker 实跑）：修复前记录=`64491 28852-1789147592`（pid-秒），修复后=`64491 155506a470904637f886871b1c01f026`（32hex）；chmod(384)=0600 经 WSL gcc 实测 rc=0 mode=600（Zan 侧 POSIX 路径本机无 Linux 运行时，属编译+语义等价验证）；探针跑出的 master 多进程 FATAL（worker 甄别路径）红基线证实与本次改动无关（新旧两版 FATAL 一致）。**仍留**：**仍留**：①Lua 桥无沙箱、按裸库名 dlopen（需沙箱策略与加载白名单的接口设计）；⑧ORM 裸列/裸条件入口按设计如此，需调用方纪律。

* **A292 [P3] legend 包裹页装备格 Y 与立绘底图格框差 3 设备（2026-09-11 实测，未修）**：`templates/game/legend/src/Spec.zan` 的 `BagEquipY` = 46/83/134/173（左列 54/93），而底图 `assets/ui/00009.jpg`（男）/`00010.jpg`（女）实测格顶是 **43/81/131/170**（左列 132/171）——两张立绘是同一套格位，排行榜页（`PageRank.zan`）用的正是这组且已对参考图零差异（§5.3d），所以包裹页那组整体低 3 设备（2 逻辑像素）。修法：把 `BagEquipY` 对齐实测值后**必须重跑包裹页对 `mir2/mir/包裹.png` 的 A/B** 再提交；本次只登记不动它（超出排行榜页任务范围）。探针方法：按列切片统计「近黑行占比」找格内段，见本次会话脚本 `_scratch/legend-scene`（临时）。　**仍留**：包裹页 BagEquipY 与立绘底图格位的 3 设备差是纯像素对齐问题，改完必须重跑对 mir2/mir/包裹.png 的 A/B 才能提交；本轮聚焦编译器/运行时缺陷，未动 legend。

* **A293 已修（2026-09-11 续轮二，P1/编译器 async×EH）**：async 方法内嵌套的非 async 函数抛出异常、由 await 的 catch 捕获后，awaiter 的局部变量全为 NULL（最小红案 `_scratch/fixv/exc_local50.zan`：嵌套 sync throw + root catch + 一个 string 局部）。**根因不在 EH trampoline 本身而在 unwind mark 缺写**：`irgen_async.c` 的 `emit_async_eh_prologue` 给 `$resume` 装 trampoline（top+1 处 arm setjmp）时**没有写 handler 槽的 mark（unwind 栈深度）**——mark 槽是 chunk calloc 里的 0（或上次遗留值）；try 的 arm（irgen_stmt.c:2111）写了 mark，trampoline 与 frame 内 try 的 re-arm 都漏了。后果链：thrower 是普通 sync 帧（无 async frame，如 `Deep()`）→ 走 irgen_stmt.c:3022 的 `emit_eh_unwind_to_handler(g, top)` → 读 trampoline 槽的 mark=0 → `__zan_eh_tmp_unwind(0)` 把 tmp 栈上**从 0 起全部**的注册槽释放并置 NULL——包括 awaiter 帧与 root 帧注册的 owned 局部（string/List 等）→ catch 后局部全 null。**关键对照**：throw 在 async 方法体内（直接走 trampoline land，rethrow 无 unwind）不坏（exc_local40 绿）；嵌套 sync 帧才坏（exc_local47 红）；thrower 与 handler 之间隔几个 sync 帧无关。**修**：trampoline arm 与 rearm arm 两处补写 mark（`store load(eh_tmptop) → emit_eh_mark_ptr(t1)`），与 try 的 arm 对齐。**验证**：19 个 exc_local 探针全部转绿（exc_local20 的 rc=1 是无 catch 的 finally 变体预期上抛）；既有 async/exception conformance 8 项（async_catch_rethrow/async_throw/async_throw_across_frames/async_try_catch/async_try_exit_depth/exception_finally_paths/exception_handler_depth/exception_local_string_unwind）全 PASS；宽回归 web_typed_binding/pullin_qualified_escape/async_when_all/https_binary_body/http_forwarder_tunnel 等 11 项全 PASS；新用例 `tests/conformance/async_nested_throw_locals.zan`（leakcheck/determinism 孪生过）。selfhost 的 EH 是老式 `__exc_stack`（无 unwind-mark 机制），无此形状，无需同步改（gen1 的 async-void-Main 缺 sync wrapper 是 B6-SH1 域的已知缺口，另行挂账）。

* **A298 [P1/运行时静默截断×Net] http_forwarder_tunnel / stream / keepalive 三个用例 rc=0 截断/空输出（2026-09-11 登记，2026-09-12 已修，两处根因实锤）**：
  ① **调度器终局误判（主因）**：irgen 内联发射的 `zan_co_sched_run_until` 以「`woke>0 || has_timer`」决定退出。阻塞任务线程完成时 `blocking_finish_worker` 在锁外 post NULL-overlapped 唤醒包（`dns_wake_notify`），而 reactor 的 GQCS 超时路径每轮都会 `dns_drain()`——包落到「任务已被 drain」之后，该包被 dequeue 时 drain 为空，产出 woke==0 的一轮；若此刻恰无定时器，调度器带着在途 IO op 和 parked 帧直接退出：进程 rc=0、输出截断处即退出点，完全无声。IOTRACE 指纹：末行 `poll removed=1 cnt=N` 且无 `poll_op` 行。**修复**：终局条件追加 `zan_io_has_pending()`（irgen 发射 weak 声明 + 定时器-only 程序的 weak 回退 0；纤程调度器 rt_sched 本来就是这个判据）。
  ② **Windows 关闭不唤醒（帮凶）**：closesocket 不给挂起重叠操作投递完成包；`TcpClient.Close` 原来直接 Close——keep-alive 池对端停在 RecvAsync 的协程永久挂起 + HttpClient 池清扫定时器常驻，旧终局规则下表现为「提前退出」或「定时器在场的活锁挂死」两种形态（http_client_cookies/redirect/timeout 在 HEAD 用旧编译器即 100% 挂死，同根因）。**修复**：`TcpClient.Close` 先 `Socket.ShutdownBoth`（shutdown + Windows CancelIoEx，与 `TcpListener.Stop` 同型），本句柄挂起操作以 0 字节复位、对端以 EOF 复位。
  连带修用例：http_client_cookies/redirect/timeout 进程退出前不关 HttpClient（keep-alive 连接 + 定时器悬挂），旧规则下靠 ① 摇奖退出，已按 http_client_keepalive 的清理定式补显式 Close + settle 泵（三用例现在确定性绿）。
  **验证**（干净 worktree 的 zanc，只含本修复）：探针 25/25（修复前基线 6/25）；IO/async 67 用例 slice 65/67，余 2 项与本修复无关——async_nested_throw_locals 是金标 LF/输出 CRLF 的平台既有差异（新旧编译器输出字节一致）；http_forwarder_stream 的 echo 段是 HEAD 既有「转发器第二条 upstream 连接建立失败」（A/B 旧编译器同样失败，并行会话在途的 Socket.zan 重写使其转绿）。keepalive 系由「摇奖绿」变确定性绿：fwd_keepalive/tunnel/client_keepalive 各 5/5。
  **遗留**：⑴ rt_io 的 `g_blocking_wake_lost` 置位后无消费者（丢包任务要等下一个 -2 包顺带 drain，最坏交付悬挂）；⑵ Windows 版 `zan_io_close_notify` 仍为 no-op（A291⑤ 的 Windows 半），不经 TcpClient/TcpListener 的 raw Socket 关闭依旧不唤醒——rt_io.c/Socket.zan 有并行会话在途重写，本轮未动，待其落地后统一处理；⑶ A302 tunnel leakcheck「仍可达」待复测。
* **A299 已修（2026-09-11 续轮）**：三处判空——`irgen.zan` LoadStaticField(:587)与 StoreStaticField(:597)在 `FindStaticField` 返回 null 时抛 "unknown static field <owner>.<name>"（不 fault）；`irgen_expr.zan` SpecQueue(:2693)对 `CloneSubst` 返回值判空（m 非 null 时 clone 恒非 null，守卫为防御性）。顺带修掉 dbgen.zan 的三处 `FieldIndex(` 调用——该方法不存在（真实助手叫 `FieldFind`，774 行的封装也一并更名 LambdaFieldFind），C host 编译早已拒绝，属既有死代码被 null-safety 扫描牵出。**验证**：C host 编译 selfhost 全 14 源通过；gen1 构建+编译 prog1 并跑 golden 逐字节一致（selfhost_gen1、selfhost_json_ignore 两测试转绿）。`selfhost_fixed_point` 仍红=B6-SH1 既有（自举编译器无 ref/out 形参声明，见该条，不在本轮范围）。
* **A300 已修（2026-09-11 续轮，P1/编译器拉入）**：`conformance_web_typed_binding` 在 HEAD 编不过（8 处 unresolved call 'TaskJoin.WhenAll'）。根因不在注册而在 `pi_seed_source`（`src/compiler/main.c:1066`）的按需拉入种子：镜像「`Task.WhenAll` → flag TaskJoin」的分支（`:1237-1248`）判定依赖 `chain`（点号链头），而 chain 在点号本身（switch default）与每个非点号 token （switch 后置 `if (tok.kind != TK_DOT) chain = NULL;`）处被**双重清空**——恒为 NULL，该镜像与 ns_root 命名空间分支都是死代码。任何用例从未因镜像受益；`async_when_all` 能过纯因用例显式拼写了一次 `TaskJoin.CancelAll`；stdlib 自己的 `Task.WhenAll` （HttpForwarder.zan:961）由 pi_scan_file（无 chain、逐 ident 无条件 flag，`main.c:1011-1037`）扫到。**修（外科式）**：不动 chain 语义（整链复活会连带复活 ns_root 分支，其传递性拉入会破坏 `pullin_qualified_escape` 的遮蔽契约——实测 137 项连坐红），改为在裸名 `Task` 的 TK_IDENT 处用 `zan_lexer_peek2`（新增，`lexer.c` 的 peek 改造为 `lexer_peek_n(n)`，peek/peek2 皆全状态快照）前瞻两 token，`Task.WhenAll/WhenAny` 形状就地 flag TaskJoin；chain 死代码以注释言明留待后续清理。**验证**：三个此前必红的 WhenAll 探针转绿；`web_typed_binding` 输出与 golden 逐字节一致；`pullin_qualified_escape` 单独编译+运行 golden 一致；smoke 档 Gui 在途与一次 ld.exe 竞态（重跑即绿）外全过。注：本修复后的整档 standard 轮无法给出干净数字——并行会话正在改 `stdlib/Gui/Component/Chart/**` （工作树一度删除了 `PolarAxisSpec`/`ChartCalendarSpec` 类而引用它们的文件未同步改），约 136 项在 `--auto-stdlib` 下编译连坐红，与本修复无关（编译失败点全在 Chart 子树）。
* **A302 [P2/stdlib Net] leakcheck_http_forwarder_tunnel 退出时 29 对象仍可达（2026-09-11 续轮发现，未修）**：A300 修复解除了该用例的编译阻塞（此前 tunnel 测试里 `await Task.WhenAll` 拉不进 TaskJoin，conformance 与 leakcheck 双双编不过，故本项从未有机会暴露）。leak 报告（2/2 稳定复现）：`Proxy\HttpForwarder.zan:513`（TcpListener new）、`:38`（FwdChannel 构造 x4）、`:141`（FwdPool 的 List<FwdChannel>）、`:958`（PumpUntilClose 的 tasks List<long> x3）、`:394`、`:623`，加用例自身 `:102`（TcpClient）。测试尾部已有 `fwd.Stop()` + 20x10ms 让 accept 以 -1 复位退出的排水（注释明说为此目的），但 Stop 后仍有一条在途隧道的泵协程与服务端对象链在探测点前没走完/仍被持有——「可达」而非「丢失」，属停服生命周期记账问题，不是泄漏性缺陷；但 leakcheck 按仍可达计红。归 Net 车道：要么 Forwarder.Stop 把 channel 池与在途泵的收尾做成可等待（Stop 返回句柄或内部 join），要么测试加长排水并断言泵已退。未修原因：conformance 主测试 6/6 稳定绿、rc=0，修它要动 Forwarder 的 Stop 语义（Net 车道在途区域），且需要无负载环境复核排水时长。

* **A304 [模板 server-*] 其余 7 个服务端模板五维抽测 + server-mvc 6 处 type-check 修复（2026-09-11，用户指令"mvc呢，其他的服务端模板呢"）**：延续 A303 五维口径对 server-mvc/collab/licensing/iot/ws/http/tcp 七模板做同轮实测（构建→起服→功能门→安全抽测→性能抽样→混沌），全部 linux-x64 linux 侧起服。**先修模板 6 处 type-check 错（server-mvc/collab/licensing/game 四份 AppController + mvc/collab Users.zan）**：HEAD 编译器（454c72f0 起 checker 检查 await 表达式内部）暴露 (a) 生成的 `__AttrRoutes` 蹦床调用 `__TxBegin/__TxCommit/__TxRollback` 而 AppController 把 override 标了 `protected`（基类 Controller.zan 这三个钩子是 unmarked=public，`__Bind/__SetView` 同为 public——模板标 protected 破坏编译器契约）；(b) `Cache()` 可返 null（AppServices.Use 前为 null）但 Users.zan 直接 `await cache.GetAsync/SetTtlAsync`。修法：四份 AppController 摘掉 `protected`（对齐基类公开契约）；Users.zan 两处加 `if (cache != null)` 守卫（Articles.zan Bust 本来就有）。归因记录：8/9 编译器（51d2f1e13）不报——当时 await 体内表达式从未被检查；9/9 起（454c72f0 `case AST_AWAIT_EXPR`）暴露的是模板潜伏缺陷，非编译器回归。**结果矩阵**（八模板全过，无 FAIL 级缺陷）：server-mvc（8099，count=4）首页/admin 登录 302+cookie 标志/7 admin 页 200/未登录 302→login/405/路径穿越 404/静态 200/3MB body 413/失败登录入 sys_login_log（30 失败+1 成功）/页面 4 并发 ~1300/s、RSS 11-16MB；server-collab（8090，count=4）7 模块 admin 页 200/[Tx] 入库闭环（submit→wms_stock_in 落行→inventory qty=5）/405；server-licensing（8096，count=4）4 admin 页 200/api/index/ping 0000/activate 错码 0003/2001 语义正确；server-iot（MQTT :1883 + console :8080）MQTT 3.1.1 CONNACK/SUBACK/PUB 回显/PINGRESP RTT 0.039ms/双客户端 fanout 送达/敌意字节 RST 拒收、console admin/admin 登录 Bearer token 64hex/badtoken 401/stats 吭口计数正确（messages_in=2）；server-ws（:8097 RFC6455）握手 101+Accept 正确/双客户端广播 `[#N] text` 双端送达/广播 RTT 0.058ms/1000 ops；server-http（改 8083）8 并发 800 req 0.43s（~1900/s）/ping pong；server-tcp（:9000）welcome/echo/64KB 单帧完整回显/RTT 0.035ms。**稳定性抽样**：mvc 15s 混沌（二进制垃圾/30KB URI/RST 半包）22 连接后八服务全存活、mvc RSS 平（11.2MB）、日志无增长；管理后台默认口令 admin/admin1234 同 A303 观察项 1（四模板都是），生产首启必须改。**模板修正**：server-http/src/main.zan 8080→8083（8080 与 iot console 冲突，模板各自示例端口本就不同）。探针方式：临时 Python 脚本（WSL 侧）逐模板打——MQTT 最小 3.1.1 客户端、RFC6455 握手+掩码帧、Worker HTTP/TCP 直连，脚本未入 tools/（一次性、无复用价值，口径已录本条）。

* **A305 [stdlib/Gui] 导航后内容区间歇性整片不画——树在、像素空（2026-09-11 legend 复刻会话实测，未修，非页面代码缺陷）**：legend 模板（templates/game/legend）经 UiDriver 点击底部导航切页后，内容区**经常**只剩页面底色 `#1d2633`，再导航一次又可能画出来——同 build 同脚本一会画一会不画。已证与页面代码无关：连拍 `_scratch/legend-scene/multi.txt`（m1 画 / m2-m5 全空）里 m3/m4 是**已提交的成就页 26**（69d2bc58，常规 mk-frame 结构）同样空；图鉴 23 页的 LEGEND_DBG 探针（模板 main.zan 1000ms 刻度，`content.HitTest` 走可见性+矩形同一路径）显示 6 个采样点全部命中具名节点、navscan 扫出全部导航 tile、控件树实摆矩形正确；`Navigate` 已包 try/catch 挂 `nav-err` probe（随本次 legend 提交入库），空画时**从未触发**——不是布局塌陷也不是构建异常，是重绘/失效路径丢了请求。波及所有浮窗页（17/22/23/26/29 都中过），legend 验收只能靠「一次运行只做一次导航、不画就重启重试」的仪式绕行（REFERENCE_AUDIT §八）。**疑点与排查方向**：Gui 失效传播（rebuild+Refresh 后 dirty 标记被吞/重绘合并窗口错过）、Win32 后端 present 条件；本轮 stdlib/Gui 后端正被并行会话大面积修改（Native/Win32Shell/Css 在途 + 33b510e4 等 5 个 Gui 提交），无法在半成品 stdlib 上二分定根因——等 Gui 车道安静后用 `LEGEND_DBG=1` 起模板 + UiDriver 连拍复现（探针已固化在模板 main.zan），从 33b510e4 起对 Gui 提交逐个 A/B。另：初判「`mk-frame` 缺 `mk-fixed` 首子才中招」被 m3/m4 证伪，勿按该方向修。

* **A306 [templates/game/legend] 复刻期的三条验收绕行与两处已知组件偏差（2026-09-12 P6/P7 会话，登记备查）**：① **A305 的实操绕行定式**（不是修复）：UiDriver 脚本里每次导航 `click` 同一点两次 + `redraw full`，一次运行只导航一页、不画就重启重试；`dump tree` 的 y 是**屏幕绝对坐标**（root 顶 = 窗 +48）而 ZPX `dump pixels` 是客户区相对，`PNG_y = tree_y − 48`，两者对不上说明有第二个 Legend 进程在写同一批输出文件（`Stop-Process -Name Legend -Force` + 换新输出目录重跑）。② **并行会话改 stdlib 期间的构建定式**：`zanc --auto-stdlib` 会把工作区 stdlib 拉进来，凡并行会话在改 `Gui/**`，构出的 Legend.exe 点导航后内容区整片不画（与 A305 同症状、不同原因，容易误判）。legend 验证一律用 HEAD 干净副本：`zanc <sources> --stdlib-path _scratch/stdlib-head`（脚本 `_scratch/legend-scene/build_head.ps1`，另注 `build.ps1` 的源单里 main.zan 必须排第一）。③ **Pagination simple 档两处偏差**（组件侧，未修）：字段宽 69 设备而参考图约 45；「1/5」的总数画在字段框外。根因是组件内硬编码 `app.Scale(46)`（`Gui/Widget/Pagination.zan` OnMeasure/TotalWidth），且该文件正被并行会话修改，本轮不动。④ **`.arena-plain` 填充 `#16324e` 存疑**：该色在 legend 的每张参考图里出现的像素数 ≤2，从不成面，疑为非原版色；暂无正确样本，待补图或从 `assets/ui` 认领后替换（现仅竞技场页用）。
