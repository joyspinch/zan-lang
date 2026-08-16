# zan-lang 全仓审计 · 可执行任务清单

**总目标**：除 compiler 和 runtime 外，一律用 Zan 写。一切以最终形态和最佳实现为准，
不为兼容老代码让步（未发布）。语法语义**尽可能保持 C#**。

主体分三部分：A 编译器/运行时能力，B 标准库，C 文档。每项带 ID、依赖、判定标准。
后续扩展章节：A15-A16（语言缺口/CSS 现状）、A17-A31 与 A35-A42（历史修复，一行摘要）、
A32（遗留收尾路线）、A33-A47（专项记录）、文末"已撤回的结论"。

**本清单里凡标注〔已实测〕的结论都在机器上跑过验证；未标注的是静态分析结论。**
早期草稿中的错误结论已作废，见文末"已撤回的结论"。

> 2026-08-03：本文件已清理。已完成的条目压缩为一行摘要（原始详细记录在
> git 历史与 `_scratch/TASKS.md.bak-2026-08-03`）；未完成/进行中/冻结的条目保留原文。
> 2026-08-08：第二次清理。全量复核状态与数字（smoke 103/103 全绿为基线），
> 修正了 A32-1/A32-2/A45 等"实际已完成仍标未完成"的失实条目、更新过时统计数字、
> 修复 B7-4/B7-7 重复编号（后到者顺延为 B7-8/B7-9），并再次压缩已完成条目
> （原始详细记录在 git 历史与 `_scratch/TASKS.md.bak-2026-08-08`）。

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
  `gui_runtime_shims.c` 只剩 macOS 非-Cocoa 构建的 **22 个 WebView no-op 桩**。
  **剩余**：macOS 那条分支要么接真 WKWebView、要么让 Zan 侧 `#if` 兜底后整文件删掉。
* **B5-3 ✅ 已完成**（`406c451` gui: draw icons in Zan as vector primitives）。
  `draw_icon` 在 `src/runtime/*.c` 里零匹配。
* **B5-4 ✅ 已完成**（`067ec4e`）删 `gui_runtime_text.c` 的 Win32 窗口壳：
  只剩 3 个文字导出（draw_text / measure_text / font_height）。
* **B5-5** [ ] 〔A2 后〕`X11Shell.zan`：X11 窗口管理导出已从 `gui_runtime_font.c`
  离开，但落在新的 `src/runtime/gui_runtime_x11.c`（30KB / **25 个导出**），**仍是 C**。
  注意宏用函数版代替（`XDefaultScreen` 等），不构成阻塞。
* **B5-6** [ ] 〔A2+A3 后〕SDL 后端（window registry / event queue / dirty rect /
  texture 上传）改由 Zan 组织，C facade 退回纯绑定；Cocoa 后端
  （`objc_allocateClassPair` + `class_addMethod` 造类）。
* **B5-7** [ ] 终态：删除 `zan_gui` 和 `zan_sdl3` 两个自建垫片。
  〔2026-08-08 复核〕`zan_gui` **109**、`zan_sdl3` **80** 个唯一 extern——未减少（依赖 B5-1/5/6）。
  `user32`(186) / `kernel32`(116，多为 `EntryPoint` 直绑) / `gdi32`(49) / `crt`(257) /
  `odbc` / `ws2_32` / `imm32` / `dwmapi` / `psapi` 直接绑 OS 的**是正确形态，不动**。

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

## B7 runtime 边界复核

〔2026-07-29 实测〕`rt_io.c` **77KB**、`rt_sync.c` 49KB、`rt_sched.c` 10KB /
`rt_mem.c` 7.6KB / `rt_co.c` 2.3KB / `rt_crash.h` 5KB / `rt_wasm.c` 0.8KB。
真 reactor / 调度 / 同步原语 / 内存分配留 C 没问题，但要逐个过一遍：如果混了
HTTP 解析、编码转换、路径处理这类纯逻辑，上移到 Zan。

* **B7-1 ✅ `rt_sync.c` 复核（2026-07-29，`1f12a88`）** 挖出两个真缺陷并已修：
  `lock` 离开 body 不还锁（return/break/抛异常出口，现展开成 finally 区）；
  所有 `lock(obj)` 共用一把全局互斥量（改按对象地址 64 条 stripe 的递归锁）。
  用例 `lock_release_paths.zan` / `monitor_striped.zan`，733/733。
* **B7-2 🚧 `rt_io.c` 复核（进行中）** 已修（epoll 路径，本机 Windows 无法实测，
  仅静态修正）：第二个 waiter 的 `calloc` 失败时直接 `return` 污染全局 pending →
  与"槽表扩容失败"同一处理（`io_mark_dead` + 清空 pending）；`io_take` 一次最多取
  8 个 waiter、超出的直接 `free` 掉使协程永不唤醒 → 溢出留在原地。
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
* **C9** [ ] 文档覆盖率差距悬殊：`System` 16.5%、`Gui` 9.8%、`Game` **0.7%**。
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

## A32-0 · 冻结基线与失败矩阵（S）—— ✅ 已完成（2026-08-04，随 A32-3 落地）

失败矩阵即 **A43-A 表**（每条带探针编号 + 预期输出）；实例字段初始化器"语义是否执行"
在 A32-1 落地时确认并同批补齐（此前不执行）；async EH 成本基线记录在 A8-12/A43。
基线命令与退出标准原文见备份。

## A32-1 · 字段声明初始化器转换与语义补齐（S）—— ✅ 已完成（2026-08-08 复核）

静态字段初始化器接入 main-entry 初始化循环；实例初始化器经 `emit_decl_field_initializers`
从所有构造入口调用（此前实例初始化器不执行，语义已同批补齐；顺序/继承/每对象一次按
C# 目标定型）；所有入口在 emit 前先过 `check_implicit_narrowing()`（窄化诊断覆盖
static/instance/generic 三形态）；RC 字段复用 typed retain/store。

**正式测试**：`tests/conformance/field_decl_initializers.zan`（执行顺序/一次性/RC 字段，
2026-08-08 golden 实测通过）+ `diag_field_initializer_narrowing_{static,generic}.zan`
（smoke 全绿）。

## A32-2 · 统一 stride-aware 集合元素操作（L）—— ✅ 已完成（2026-08-08 复核）

### A32-2a List 全操作 ✅

`AddRange`（容量按 count*stride，self-AddRange 源快照）、`Insert`（整元素 stride 右移 +
typed-store）、`Reverse`（整元素交换，不重复 retain/release）、`IndexOf`/`Contains`
（typed equality，覆盖 12/32 字节 struct 及自定义 `==`）全部按 `elem_slot_words()` 实现；
`irgen_call.c` 的 `wide_elem_unsupported()` 拒绝路径已全部删除（复核 0 处残留）。

### A32-2b Dictionary 宽值槽 ✅

`dict_struct_type` 已是 8 字段布局，field 7 = `value_words` stride 指针
（`irgen.h` 里 334 行的注释仍是旧 4 字段描述，属代码内陈旧注释待清理）；扩容按
value_words 算字节；new/initializer/indexer set/get/Add/Remove/Clear/扩容/析构全部
按 stride 读写，同提交内完成全部消费者，无新旧双布局。

**正式测试**：`list_wide_struct_elements.zan`（五个操作）+ `dictionary_wide_values.zan`；
2026-08-08 实测 24 字节 struct 的 List 五操作与 Dictionary 扩容/覆盖/Remove/Clear
全链路输出正确。

## A32-3 · 统一「声明类型实例 + 方法类型参数」特化（XL）—— ✅ 已完成（2026-08-04）

**根因**：`zan_method_spec` 只以 `msym + method bind[]` 为 key，泛型声明类型的实例方法被
拒绝、body emission 又清空 `g->cur_inst`；async 由 `emit_user_methods()` 单独按原方法
符号建 ramp/frame/resume，无法复用普通 method spec。

### A32-3a 泛型类的实例泛型方法 ✅（详情见备份；正式测试
`generic_class_instance_generic_method.zan`，三档全过）

特化 key 扩为 `{method symbol, owner instantiation, method type args}`，mangling 同时编码
`Pool<OwnerT>` 与 `M<U>`（`Pool$int_Echo$$Box`）；显式 substitution context 取代互斥的
`cur_inst`/`cur_mbind` 隐式状态；`this` 用 owner instantiation 的具体布局，body 内
self-call 路由到同一特化缓存；`T` 字段的 ARC 与格式化按实例化后具体类型
（`field_store_type()` + `infer_expr_type` 的 `concretize`）。

### A32-3b async 泛型方法 ✅（详情见备份；正式测试 `async_generic_method.zan` /
`generic_class_async_generic_method.zan`，三档全过）

`declare_async_method()`/`emit_async_method_ir()` 抽取为可复用 async emitter（Pass A/B
与方法特化共用同一份 lowering）；async method spec 记录 ramp/resume/frame 布局，key 与
名字含 owner + method type args（`Pool$string_Wrap$$int$resume` 等，不同特化不共用
frame）；async scan 改 `resolve_type_ctx()` 解析声明类型；await 结果静态类型经
`method_ret_type_at()` 绑定方法类型参数。「async generic methods cannot be
monomorphized」拒绝分支已删除。

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

## A32-7 · 整数→浮点隐式转换（S，已完成 2026-08-04）

`double`/`float`/`decimal` 目标遇到整数源时不转换、直接按位落槽（`double b = 1;` 打印
`4.94066e-324`、`2 * d` LLVM 校验失败）。修法：所有「整数值遇浮点目标」路径补一次
`sitofp`——`coerce_int_to`/`coerce_int_pair`（混合运算两侧统一浮点后判定）、
`emit_arg_typed`（浮点形参）、AST_RETURN、`emit_collection_slot_store`
（List/Dictionary 宽槽，槽物理 i64 转换后再 bitcast）、`zan_store_fit`（数组元素）。
测试 `tests/conformance/int_to_double.zan`；`ctest -R "double|float|numeric|list|dict|arith|int_to"` 47/47。

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

捕获的是**变量**而不是值：被 lambda 赋值的局部搬进**堆单元**（形状同闭包记录
`{ fn = null, dtor, target = null, value }`），存储槽变成指向单元内 `value` 的指针，
外层与闭包的读写落在同一块内存（`emit_boxed_var_decl` / `emit_box_cell` / `box_value_ptr` /
`local_is_lambda_written`）；生命周期 = 闭包生命周期（声明作用域持一引用、每个捕获闭包各持
一引用，最后一个出门的跑单元 dtor，闭包活得比栈帧长时变量跟着活）。只装箱**确实被写**的
局部（`node_writes_ident` 认赋值/`++`/`--`/`ref`/`out`），只读捕获仍按值拷贝；async 局部
本来就在协程帧里天然共享，跳过。
正式测试 `closure_mutable_capture.zan`，conformance/determinism/leakcheck 三档全过。

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
   比对 / linux_so / macos_dylib），`ctest -R emit_lib` 4/4。
2. [x] **A34-2 IDE 消费库目标（已实现，commit `1fde2b0f`，2026-08-08 复核）**：
   `RunBuildBegin` 对 `IsLibraryProject` 分支产出 `HostLibExt` 库文件（Build 出库不 Run）；
   Publish 有 isLib 分支按平台扩展名出共享库 + 可选静态库（`ProjStaticLib`/
   `StaticLibExtFor`）；`CreateProjectT` 把 `target = <target>` 写进 zan.proj。
3. [~] **A34-3 库的消费侧（半边完成，2026-08-08 复核）**：`--link-lib <name>` 已实现
   （`main.c:1457` 参数解析 → `-l<name>`，commit `30d46622` bundled ld 自包含链接）；
   **仍缺**：`link =` zan.proj 键「引用已编译 .zan 库」的语义——现实现是资源引用
   （Linked resources），不是库引用。

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

> 各条修复细节（根因、落点、探针输出、回归记录）已压缩；原始详细记录在 git 历史与
> `_scratch/TASKS.md.bak-2026-08-08`。

## A43-B 语法缺失（parser 层不接受，按价值排序）

| # | C# 语法 | 探针 | 现状 |
|---|---|---|---|
| A43-B1 | `Func<...>` / `Action<...>` | 26,39 | ✅ 已修（2026-08-08，`cs_b01_func_action.zan`）。内置泛型 delegate 类型 + 匿名 delegate 语法；`op_call` 的 `params` 尾部重载在零参数调用时修复了 fixed 参数计数下溢（`args[-1]` 越界读崩溃）：`resolve_op_overload` 的 `fixed = ps->count - 1 - p0`（原 `-2-p0` 静态/实例各差 1），`pack_params_args` 的 `injected` 按 `MOD_STATIC` 判定，emit 侧参数类型索引按 static/instance 取 `self_off`。回归测试 `cs_b_opcall_params.zan`（静态/实例、零参/多参） |
| A43-B2 | `using (res) { }` 确定性释放 | 03 | ✅ 已修（2026-08-08，`cs_b02_using.zan`）。语句级 `using (res) { }` 降级 try/finally + `Dispose()`；stdlib 定义 `IDisposable`。另修复字符串拼接对 NULL 操作数直传 `strlen` 的崩溃（`emit_str_concat`/`emit_str_concat_n` 补 `emit_str_nonnull`） |
| A43-B3 | 元组 `(int, string)` 与解构 | 01 | ✅ 已修（2026-08-08，`cs_b03_tuple.zan`）。`(a,b)` 字面量降级为匿名 struct（`Item1..N` 字段，签名缓存）；`var (a,b)=rhs` / `(int a,string b)=rhs` 解构→字段赋值；支持方法返回元组、显式 `(T1,T2)` 类型、`.ItemN` 访问。**限制**：嵌套解构 `var (a,(b,c))=...` 与 `(a,b)=rhs` 赋值未实现（后者已报错）；struct 字段持动态字符串沿用 KeyValuePair 的既有泄漏（ARC 不管理 struct 内 rc 字段） |
| A43-B4 | 命名实参 `F(b: 2)` | 17 | `expected ')' , got ':'` |
| A43-B5 | 模式变量 `is T x` / `case T x:` / `when` 子句 / `is not null` | 23,34,35,45 | ✅ 已修（2026-08-08，`cs_b05_pattern_var.zan`）。`is T x` / `is not T` / `is null` / `is not null`、`case T x:`（类/string/object 运行时 is 检查，string 走 obj-8 magic 探测）、`when` 守卫（独立块短路，失败时不求值守卫）、`case null:`、case 体模式变量重绑定（先前 case 的 scope 释放会截断 locals）、switch `break` 经 `loop_locals_base` 释放不再误释放判别式（`case T x:` 后 `x is T` 此前读悬垂指针）。`emit_runtime_is_check` 的越界上界改用常量 `ZAN_MAX_LEAK_SITES`（原用发射时的 `leak_site_count`，helper 函数先于 `new` 站点注册时恒 false）。standard 回归发现的 `conformance_python_embed_smoke` 编译崩溃（`Python.Eval("lambda: 42")()` 空参 op_call 的 params 重载）已定位为 op_call 重载解析的 fixed 计数下溢（见 B1 行）并修复 |
| A43-B6 | switch 表达式 `x switch { ... }` | 16 | ✅ 已修（2026-08-08，`cs_b06_switch_expr.zan`）。`expr switch { <pattern> when <guard> => <result>, ... }`：常量/类型模式（`is T x` 型 pattern + 变量绑定，`case T x:` 同款运行时检查）、`null`、`_` discard、`default`、`when` 守卫、pattern 变量在 result 中可用、无匹配 arm → 零值（int 0 / 引用 null）、嵌套 switch、可作为调用实参/赋值 RHS。降级：合成 AST_SWITCH_STMT，arms 的 result 存入隐藏局部 `\x01swx`（每个 case 体 `{__swx = result; break;}`），无 discard 时补空 default，emit 后 load 隐藏槽；隐藏局部移出 scope（`locals->count` 回滚）使 +1 归属表达式结果。**ARC**：`expr_yields_owned_rc_value` 加 AST_SWITCH_EXPR 分支返回 1（槽内存储后恒为 +1：owned arm move、borrowed arm retain，而本地作用域不释放），否则接收方二次 retain 导致 `leakcheck_cs_b06_switch_expr` 泄漏 |
| A43-B7 | 扩展方法 `this int v` 形参 | 15 | ✅ 已实现（`this int v` + `find_extension_method` 全链路；`tests/conformance/cs_b07_ext_method.zan`）。数字字面量接收者需写 `(5).Twice()`（`5.` 会被词法当作 float，与 C# 一致） |
| A43-B8 | 交错数组 `int[][]`、多维数组 `int[,]` | 24,25 | ✅ 已修（2026-08-08，`cs_b08_arrays.zan`）。`int[][]`（数组的数组）、`int[,]`/`int[,,]`（rank-N 矩形数组）、混合 `int[][,]`（rank-2 数组的 1D 数组）与 `int[,][]`，按 C# 最左 rank 规格最外层折叠；`.Length` 返回元素总数、`.GetLength(d)` 返回第 d 维长度；多下标 `m[i,j]` 按行优先扁平化越界检查；`new int[2,2]{{1,2},{3,4}}` 带维度初始化。**实现**：`type_ref` 加 `array_rank`+`array_element`（parse_type_ref 从右到左折叠最多 16 个 rank 规格）、`new_expr` 加 `array_rank`（sized 层作为最外层包装）、`AST_INDEX.extra` 多下标列表（首下标留在原字段，1D 路径不变）；binder 构造嵌套 TYPE_ARRAY（`binder_type_equal`/`checker_type_equal`/`types_concrete_equal`/`types_equal`/`tuple_sig_type`/`render_type_full` 全部比较 rank）；`zan_mdarray_alloc` 新布局 raw+0=count、raw+8=rank、raw+16=dims[0..rank-1]、data 紧跟其后，`zan_array_len`/`.Length` 可直接复用；`emit_mdarray_elem_ptr` 行优先扁平化，rank>1 的 new/store/incdec/read 路径全部分支到它。leakcheck `-- leak-clean` |
| A43-B9 | 索引器 `public int this[int i]` | 37 | `expected member name` |
| A43-B10 | 插值格式串 `{v:D4}` | 50 | ✅ 已修（2026-08-08，`cs_b10_interp_format.zan`） |
| A43-B11 | 局部函数 | 05 | ✅ 已修（2026-08-08，`cs_b11_local_func.zan`，含泛型局部函数） |
| A43-B12 | `implicit` / `explicit operator` | 06 | ✅ 已修（2026-08-08，`cs_b12_conv_operator.zan`） |
| A43-B13 | 泛型变体 `interface I<out T>` | 07 | ✅ 已修（2026-08-08，`cs_b13_variance.zan`，接受语法不实现协变） |
| A43-B14 | `checked` / `unchecked` | 02 | ✅ 已修（2026-08-08，`cs_b14_checked.zan`，no-op 纯表达式） |
| A43-B15 | `Task` / `Task<T>` 类型名与 API | 12,31,32 | ✅ 已修（2026-08-08，`cs_b15_task.zan`）。`Task`/`Task<T>` 成为可用作值的类型（新 `TYPE_TASK` kind，opaque i64 协程句柄；`Task<int>` 携带结果类型），同时保留 builtin 静态 `Task.Spawn/Run/IsDone/Cancel/IsCancellationRequested` 调用面。`Task.Run(delegate(){...})`/`Task.Run(()=>...)`/`Task.Run(Action 变量)`：内联执行 delegate（体内 await 经 root-await 路径泵驱动），返回已完成任务（句柄 0，`__zan_co_isdone(0)` 恒 1）；`t.Wait()`：泵 `zan_co_sched_run` 直到该 frame done（协作式调度下同步上下文等协程的唯一正确方式）；`t.Result`：读 frame 结果槽（`ASYNC_FRAME_RESULT`）解码为 T 后 reap（untrack+free）；`t.IsCompleted`：非泵探针。**Task\<T\> 生命周期**：`Task.Run(<非 void async 调用>)` 不装 reaper、保留 track，frame 活到 `Result`/`Wait` 读取（恰好一个终结任务）；`Task.Spawn` 恒装 reaper（fire-and-forget 不泄漏）；`Task<T>` → `Task` 协变可赋值。**顺带修复 B5 预存缺陷**：`stdlib/System/Threading/Threading.zan` 的 `MacSemWaitUntil(string, long when)`/`MacDispatchTime(long base,...)` 参数名撞 `when`/`base` 保留字，macOS 目标 ABI 测试（stale 构建图掩盖）编译失败——改名 `deadline`/`baseTime`。**回归门禁**：standard 477/480（`cs_b15` 3 项 + 既有 474 项全过）；3 个 `emit_lib_*` 失败均为环境/并行因素，非 B15 回归：(1) `windows_dll` 链接时 `libgcc.a(emutls.o)` 缺 pthread 符号（mingw 交叉工具链无 winpthread stub，B8 时代产物时间戳佐证与本次无关）；(2) `linux_so`/`macos_dylib` 的 cross-shared guard 因 **IDE 改版并行加入的 `stdlib/System/MessageBox.zan`（untracked，17:29 创建）`using System.IO` 把 IO→DirectoryWatcher→Threading 全链拉进 `using System` 闭包**而触发（`zan_thread_*`/`zan_gate_*` extern 置位 `uses_sync_runtime`/`uses_socket_async`；移走该文件 cross-shared 立即通过，B8 时代 16:23 产物无此文件即通过）。IDE 改版合入后应将 MessageBox 移出 `System` 根命名空间（`using System` 会全量拉入 `stdlib/System/*.zan`） |
| A43-B16 | `KeyValuePair<K,V>` | 27 | ✅ 已修（2026-08-08，`cs_b16_keyvaluepair.zan`） |
| A43-B17 | LINQ `orderby` / `join` / `let` / `group into` | 14 | 查询语法只支持单 `from...where...select` |
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

# A44 · Chart 组件对照 ECharts 的搁置项（2026-08-06，范围决策记录）

图表组件完善（补齐雷达面积填充/每轴 max、markPoint、嵌套环饼、K 线 dataZoom、
悬停 emphasis，新增和弦/力导向/事件河/韦恩渲染器 + `examples/gui_charts`
独立示例）时，对照 ECharts 2.2.4 明确**不做**、留待后续的部分。均非缺陷绕过：
现有 API 已解析相关字段或留有占位，只是渲染层未接。

* [ ] **数值 / 时间 / 对数 X 轴**：`ChartAxisType.Value/Time/Log` 已定义且
  `FromJson` 已解析，但 `Chart.BuildAxes*` 仍按类别槽布局——X 轴只能等距分类。
* [ ] **itemStyle / emphasis 完整样式树**：`ChartItemStyle` 等样式类已建模，
  但渲染器尚未逐项消费（目前只有三级颜色控制 option < series < data item）。
* [ ] **toolbox / visualMap / timeline 组件**：ECharts 的工具箱、视觉映射与
  时间轴组件无对应物。
* [ ] **地图渲染器**：`ChartType.Map` 目前渲染占位面板
  （`ChartView.DrawMapPlaceholder`）；需要地理边界数据（GeoJSON），标准库不内置。
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

合计约 18 MB 纯重复（macOS 只有 Postgres 一份，不重复；
`System/Scripting/drivers/win-x64` 的 `libcrypto-3.dll` 是 CPython 嵌入包
自带的**另一个** build，名字和内容都不同，不能与上表合并）。

* [ ] **A47-1 拆出共享原生依赖目录**。挡路的是安全校验
  `zan_is_safe_bundle_name`（`src/compiler/main.c`）：bundle 清单只允许同目录
  裸文件名，禁止路径分隔符，所以今天没法引用别的模块的文件。方案：清单支持
  一种受限的共享引用（如 `@shared/openssl-3/<file>`，解析相对 stdlib 根、
  仍然拒绝 `..`、`:`、绝对路径），文件只留一份在
  `stdlib/_shared/openssl-3/<target>/`，发布时按 basename 复制到 exe 旁边。
  链接期不受影响：导入库（`libcrypto.dll.a` / `libssl.dll.a`）只有 Tls 用，
  留在原处。
* [~] **A47-2 `build/toolchain` 自嵌套**：曾实测嵌套到 32 层
  （`build/toolchain/toolchain/toolchain/...`，每层都带一份 stdlib 和
  openssl），`build/toolchain` 单独占 1.29 GB / 3205 文件。`build/` 是
  git-ignored 的一次性产物，但说明某个发布/拷贝步骤把目标目录拷进了自己
  （`scripts/publish_ide.ps1` 的注释已经点出要避免 `toolchain\toolchain`）。
  **2026-08-08 复核：自嵌套已消失**——当前 `build/toolchain` 无嵌套（189MB / 1026
  文件，实测深度 0），大概率随 build 目录重建而清掉；「定位到具体步骤并加自嵌套
  防护」未验证，重跑发布流程时仍需检查。
  要定位到具体步骤并加自嵌套防护，然后清掉。
* [ ] **A47-3 `templates/server/server-mvc/.build/` 里躺着 libcrypto/libssl
  两份拷贝**（被 `.gitignore` 的 `.build/` 挡住没进版本库），属于跑过一次
  模板构建留下的残留，清掉。
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

* [ ] **A48-1 交叉链接共享库时把运行时对象一起编出来**：Windows 本机 DLL 路径
  把 `rt_io/rt_sync/rt_embed/rt_timer` 的 `.o` 放在链接行上，交叉路径没有对应
  目标平台的产物，只能报错。补上按目标三元组编译运行时对象再链接，两个用例
  即可解封。

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
* [ ] **A49-2 `StringBuilder.Append(ulong)` 丢无符号语义**：`sb.Append(umax)`
  打印 `-1`。`emit_value_as_cstr` 只看 LLVM 值宽度、拿不到 Zan 静态类型，无从
  判断该按 `ulong` 输出；要么给它传入静态类型，要么在 append 侧分流。基线同样
  如此，不在本轮性能改动范围内，用例里因此只放了 `long` 的 append，修好后补行。

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
