# zan-lang 全仓审计 · 可执行任务清单

**总目标**：除 compiler 和 runtime 外，一律用 Zan 写。一切以最终形态和最佳实现为准，
不为兼容老代码让步（未发布）。语法语义**尽可能保持 C#**。

三部分合并：A 编译器/运行时能力，B 标准库，C 文档。每项带 ID、依赖、判定标准。

**本清单里凡标注〔已实测〕的结论都在机器上跑过验证；未标注的是静态分析结论。**
早期草稿中的错误结论已作废，见文末"已撤回的结论"。

> 2026-08-03：本文件已清理。已完成的条目压缩为一行摘要（原始详细记录在
> git 历史与 `_scratch/TASKS.md.bak-2026-08-03`）；未完成/进行中/冻结的条目保留原文。

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
  （`user32` 56 / `kernel32` 9 / `gdi32` 30 / `crt` 90 等，见 B5-7 旁注）；
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
* **A2-3** [ ] 变参：加 `[DllImport(..., Variadic = true)]`。
* **A2-4** [ ] `signext` / `zeroext` + 调用约定属性
  （全 compiler 里 `stdcall` / `CallConv` 出现 0 次）。
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
* **B7-4** ✅ dispatch 队列首次使用竞态（`rt_sync.c`）：Windows 上由"谁先看到
  `g_dispatch_ready` 为 0 谁就 `InitializeCriticalSection`"创建 ⇒ 两个后台线程同时
  初始化同一 section。改用 `INIT_ONCE`；用例 `dispatch_first_use.zan`。
  顺带把 `zan_atomic_int` 的所有权契约写进 `rt_sync.h`。
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
* **B1-2** [ ] `stdlib/Game`（75 文件 / 788KB / 2.1 万行代码，**只有 142 行文档注释**）：
  `Zgm` 380KB、`Arpg` 275KB、`Rts`、`Scene`、`Cards`、`Board`、`Arcade2D`。
  文档覆盖率 0.7% 是全库最低——**先补公开接口文档再谈提炼**，
  没有接口文档就重构 2 万行是盲改。
  提炼候选：`Zgm/UiRuntime.zan` 65KB 与 `Arpg/UiRuntime.zan` 67KB 同名同量级，
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
  >150 行的方法从 30 个降到 **22 个**，`ZanIDE.Run()` 已从 3408 行拆到 2934 行。
  已完成的拆分：`ZanIDE.Run`（15 个 `Init*` 方法）、`StyleSheet.Decl`（175→11）、
  `DataTable.Layout.LoadLayout`（153→~20）、`CodeEditor.Intelli.HandleInput`（215）、
  `Arpg/Project.Validate`（243）、`Zgm/Project.Validate`（214）、`Tabs.Render`（192→~90）、
  `Layer.RenderActive`（174→~144）、`App.RenderChrome`（181→~45）、
  `Event.RenderThemeDrawer`（467→~110）、`views/DocsPage.BuildDocs`（225）、
  selfhost 发射器（`EmitListRuntime`/`EmitStrOpsRuntime`/`EmitDictRuntime`/
  `EmitAsyncRuntime`，gen2==gen3 字节一致）；`CodeEditor.Render` 823→~510（随 B3-3）。
  **剩余最大件**：`ZanIDE.Main` **3745 行**（ide_zan 顶层事件/面板巨方法）需拆成
  per-panel/per-ribbon 处理器，自成一项工程。
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

* **B5-1 ❄️ 冻结**（2026-07-29 定调）——光栅器搬 Zan（`gui_runtime.c` 19 个导出：
  surface / clip / 全部图元 / blur / snapshot；文件仍有 **28 个导出** / 54KB）。
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
  〔2026-07-29 重数〕`zan_gui` **108**、`zan_sdl3` **78**——未减少（依赖 B5-1/5/6）。
  `user32`(56) / `kernel32`(9) / `gdi32`(30) / `crt`(90) / `odbc` / `ws2_32` /
  `imm32` / `dwmapi` / `psapi` 直接绑 OS 的**是正确形态，不动**。

## B6 工具链 Zan 化 〔依赖 A3 + A6〕

**❄️ 整块冻结到 A6 就绪**（2026-07-29 定调）：`src/` 下非 compiler 的 C 共
**约 230KB**——`lsp/intellisense.c` 84KB、`lsp/lsp_main.c` 67KB、`dap/debugger.c` 41KB、
`dap/dap_main.c` 28KB、`common/json.c` 15KB、`common/rpc.c` 3KB。
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

* **B7-4 ✅（2026-08-08）三项 runtime 修复 + 一个 emutls 死锁根因（探针实测）**：
  1. `rt_timer.c` 竞态：`timer_add` 的 `callback` 在入堆（unlock）之后才赋值，另一线程
     `dispatch_due` 可能对 NULL 调用；`zan_timer_delay` 的 `g_sequence` 在锁外递增；
     `g_ready_hook` 无锁读；`zan_timer_stats` 无锁读 `g_initialized`/`g_round`（且嵌套调用
     `zan_timer_list_count` 会二次加锁）。全部改为发布前赋值 / 锁内读，stats 锁内内联计数。
  2. `rt_mem.c` double-free：free 时 header 置 `ZAN_MEM_FREED` 哨兵，同一块二次 free 直接
     abort（编译器对象头在 p..p+15、分配器头在 p-16，互不覆盖，哨兵可靠）；顺带 `cls`
     越界校验。**附赠根因（Windows 探针实测，gdb 栈）**：MinGW 的 `__thread` 由 libgcc
     emutls 模拟，首次访问触发 `pthread_once(emutls_init)`，而 `emutls_init` 内部调
     `malloc` → wrap 后递归访问同一未初始化 emutls 变量 → once 锁自旋死锁。rt_mem 目前
     只在 POSIX 链接（原生 TLS）不受影响；已把 Windows 侧改为普通 static（slab 路径在
     Windows 本来就禁用）+ 注释防未来误用。POSIX 上的 double-free abort 已用 WSL 实测
     （2026-08-08）：新增 `tests/runtime/rt_mem_dblfree_test.c`（fork 子进程触发二次
     free / 损坏 header，父进程断言 SIGABRT + 消息，`--wrap` 链接 rt_mem.c；注册为
     `runtime_mem_dblfree`，Linux CI 生效）。**实测暴露一个真 bug**：`zan_mem_small`
     从自由链表弹块时不重置 header，哨兵（`ZAN_MEM_FREED`）残留到下一轮，合法的
     free→malloc 复用→再 free 被误判 double-free 而 abort（Windows 无 slab 路径所以
     完全隐形；smoke 在 Windows 全过恰因如此）。已修：弹出时重置 `magic`/`cls`；
     200k 次混合 malloc/free/realloc churn 无误报；hello / Json.Serialize 程序的
     linux-musl ELF 在 WSL 运行正常。本地 `toolchain/linux-musl/zanrt_mem.o` 已用
     zig 重建（未跟踪产物，非签入物）。
  3. `rt_sched.c` 任务对象只增不减（`g_all_tasks` 直到 shutdown 才清）→ 新增
     `zan_task_release()`（幂等摘链+free）与 `zan_task_live()` 测试钩子，所有权契约写入
     `rt_sched.h`；`tests/runtime/rt_test.c` 全部 task 生命周期补 release，新增
     `zan_task_live()==0` 泄漏断言（17/17 通过）。构建命令注释补上缺失的 `rt_co.c`。
  **预存失败（与本次无关）**：`conformance_stopwatch_timer` 编译失败——
  `stdlib/System/Diagnostics/ServerMetrics.zan:1070` `Json.Serialize` 无法解析
  （HEAD 同样存在，疑似未提交编译器改动或 stdlib map 缺 Json 注册）。
  （2026-08-08 已随生成器迁移修复：现在通过。）

* **B7-5 ✅（2026-08-08）五个 C 代码生成器整体迁移到纯 Zan**（`stdlib/System/Compiler/`，
  约 6800 行 C → 约 5300 行 Zan + 约 2100 行 C 通用管道）：
  - 架构：`genmeta.c`（AST → JSON 元数据：类形状 + 调用点索引，children-first 编号）、
    `genrun.c`（触发词预过滤 + 缓存式 `ZanGen.exe` 子进程 + JSON 交换 + 通用重写执行器），
    框架知识全部进 `GenForm/GenScene/GenJson/GenRoute/GenDb(+GenDbEmit)`，C 端零框架字面量。
  - 等价性：生成文本逐字节一致（form 13/13 模板、scene 往返、route golden、
    orm typed_query/extended/table_accessor 64114/63741/60194 字节），运行时行为
    conformance golden 覆盖；smoke 101/101、orm 8+3、新增 `tests/gen/`（回复断言）+
    `conformance_servermetrics_snapshot`（手写 JsonValue 序列化形状钉死）。
  - 关键坑：调用点必须 children-first 编号 + 升序处理（与旧 dg_visit_call 一致，跨方法顺序才
    对）；链方法重写后必须在 `Rewrote` 登记 id 且 `ChainEntity2` 要穿过未重写的真实方法
    （SetDict/Page 等）直达根——否则 `Where(...).Sum(...)`、`Update().SetDict().Where()` 断链。
  - `ServerMetrics.SnapshotJson` 改为手写 JsonValue（在生成器自举路径上，不能再依赖
    `Json.Serialize<T>`）；`--no-gen` 不再有 C 回退（旧 jsongen/routegen/dbgen 已删除）。
  - **预存失败（与本次无关，full 层）**：`selfhost_fixed_point` 在 gen1→gen2 处失败——
    `src/selfhost/dbgen.zan:581` 用 `ref` 参数，而自举编译器（selfhost/parser.zan）从未支持
    `ref` 关键字（e01975ee 引入，parser 无 TK_REF）。
  - **2026-08-08 已修复其余 13 个 standard 失败（standard 448/448 全绿）**：
    - 7 个 `conformance_arpg_*`：`ArpgSaveState` 等 5 个存档类自 2580fa4a 删
      `Game/Zgm/Engine.zan` 时丢失、引用保留 → 恢复为
      `Game/Arpg/Data/SaveState.zan`（构造器 + 链式 SetProgress/AddItem 等 + Dispose，
      按 test 与 Save.zan 的调用面）；`Formula.zan` 两处 double→int 返回缺显式转换
      （整数求值器语义，补 `(int)`）；`Project.zan` 补回 `databases` 组件列表 +
      `FindDatabase`；另 2 个过时 golden（`ZgmTcpClient` 未改名、已删的 OpenProject 行）。
    - `jwt_rs256`/`rsa_oaep_cbc`/`sdk_wechat_tenpay`/`tls_hostname`：根因是 557f91b3
      的严格 base64 审计（拒绝空白字符）打断了 PEM 解析——RsaKey/TlsStream/MySql 的
      PEM 正文提取未剥离 64 列软换行（且 TlsStream 用 `IndexOf("END")` 短标记把
      `-----` 包进正文）。修法：PEM 层剥离 \r\n（RFC 7468 软换行），`Base64.Decode`
      保持严格；`tls_hostname` 因此从"环境性"归位为真 bug（内嵌证书、loopback）。
    - `clipboard_roundtrip`/`win_tray_screen_smoke`：桌面环境测试，直跑 5/5 + 全档
      通过（偶发抖动，无代码缺陷）。

---

# C. 文档

`docs/` 40 份。问题不是日历陈旧，是**内容与实现漂移**。

* **C1 ✅ 已完成（2026-07-29）：`ABI.md` 已按当前实现重写受影响小节 + 加分层横幅。**
  （int 32 位、结构体布局、marshalling、按平台传参分类；§3.3–3.5/§4/§5/§7 标为设计意图。）
* **C2 🟡 部分完成（2026-07-29）：已修跨文档的确定性事实漂移。**
  `ABI.md` / `DESIGN.md` / `SPEC.md` / `ARCHITECTURE.md` 已改。
  **剩余**：`CODING_STANDARDS.md` / `ERROR_CATALOG.md` / `IDE.md` / `SECURITY.md`
  尚需逐份深核（未发现明显数值/结构性漂移，但未逐条走读）。
* **C3 ✅ 已完成（2026-07-29）：`STDLIB_ANALYSIS.md` `git mv` 进 `docs/archive/`。**
* **C4 ✅ 已删（2026-07-27）** `STDLIB_DB_AARDIO_REF.md` 残桩。
* **C5 ✅ 已完成** 3 份 + 新增 3 份 bug 文档（含 repro）均纳入 git 跟踪。
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

---

# 建议执行顺序（2026-08-03 更新，只列未完成项）

| 批次 | 内容 | 为什么排这里 |
|---|---|---|
| **1** | **A32-0 基线**（探针矩阵 + async EH 正确性/成本基线）→ **A32-1 字段声明初始化器** | 当前唯一整块计划，A32 内部依赖顺序固定（见 A32 章末尾依赖表） |
| **2** | **A32-2** 集合 stride 统一（List 五操作 + Dictionary 宽值槽） | 先于泛型/EH 大改，是 A30 的直接延续 |
| **3** | **A32-3** 泛型统一特化（`Pool<T>.M<U>()` + async 泛型方法） | A7/A29 的收尾，依赖 A32-2 |
| **4** | **A2-3 / A2-4**（变参、signext/callconv）→ **B5-5 / B5-6**（X11 / SDL / Cocoa） | A2 只剩这两项，是 B5 后端的入口 |
| **5** | **A3** bindgen + **A6** 编译器 API → **B6** 工具链 Zan 化 | 仍在 C 的约 230KB（LSP/DAP/json/rpc）的出口 |
| **6** | **A32-4** await 同步完成 fast path → **A32-5** LLVM 真 EH（单独里程碑，最高风险） | A8 补偿层的终局替代 |
| **7** | **B3-2 剩余**（`ZanIDE.Main` 3745 行等）、**B1-2 / C9**（Game 文档）、**B5-2 剩余**（macOS WebView 桩）、**B7-2 收尾**、**A4-2 剩余**（detach 钩子）、**C2 剩余**（四份文档）、**A15-5**（switch 重写）、**A34-2 / A34-3**（库消费侧）、**A33-2b / A33-3**（捕获按引用、GUI 事件绑定） | 收尾与能力落地后的配套 |
| **8** | **A32-6** macOS 实机 + 签名公证发布门（外部阻塞：无 Mac / 凭据） | 发布验收 |

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
* **A15-5** [ ] `switch` 全库 0 处使用——是历史惯性不是能力缺口，属于可读性/性能债
  （Gui 有 871 处 `if (x == "...")` 链），随颜色迁移一起清。
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

## A32-0 · 冻结基线与失败矩阵（S，所有阶段前置）

* 为下列边界各建最小探针并记录当前结果：静态/实例字段声明初始化器窄化；
  `Pool<T>.M<U>()`；static/instance async `M<T>()`；宽 struct 的五个 List 操作；
  宽 struct 与含引用字段 struct 的 Dictionary 增删改查/枚举；同步完成 await 链。
* 先确认实例字段声明初始化器的**语义本身**是否执行。当前 codegen 只明确看到
  `irgen_emit.c` 的静态字段入口；若实例初始化器未执行，A32-1 必须同时补语义，不能只加诊断。
* 保存当前 async EH 正确性与成本基线：A8-12 的同步/async 混合栈矩阵、
  `_scratch/eh_cost.zan` 的裸循环/空 try 中位数、生成 IR 中 setjmp/push/pop 数量。
* 基线命令：构建 `zanc`，再分别跑 `diag_nint_to_int_`、`generic`、
  `list_wide_struct_elements`、`dict`、`exception|throw|catch|async|try|await` 的精确子集。

**退出标准**：每个后续条目都有「当前失败的最小探针 + 预期输出/诊断」，且没有把探针
放进版本库测试目录；只有开始修复时才把对应案例提升为正式测试。

## A32-1 · 字段声明初始化器转换与语义补齐（S）

**实现点**：`irgen_emit.c` 静态字段初始化入口、对象/值类型构造路径
`irgen_expr.c` / `irgen_stmt.c`、`irgen_expr_core.c::check_implicit_narrowing()`。

1. 提取声明初始化器的共同检查入口：以字段经泛型替换后的声明类型为目标、初始化表达式
   推断类型为源，在任何 `emit_expr`/coerce/store 之前调用 `check_implicit_narrowing()`。
2. 静态字段直接接入现有 main-entry 初始化循环；诊断位置必须指向初始化表达式。
3. 若 A32-0 证实实例初始化器未执行，生成每类型一次的 defaults lowering，并从所有 class/
   struct 构造入口调用；顺序、继承和「每对象只执行一次」按 Zan 既有 C# 兼容目标定型。
   RC 字段必须复用 typed retain/store，构造失败或异常路径不得泄漏。
4. 覆盖普通类、struct、泛型类型特化、显式构造器/无构造器、static/instance 字段。

**正式测试**：新增 static/instance/generic field initializer 三个编译失败诊断；再加一份
`field_decl_initializers.zan` 覆盖执行顺序、一次性和 RC 字段。其 conformance、determinism、
leakcheck 全部通过；显式 cast 版本必须编译运行成功。

## A32-2 · 统一 stride-aware 集合元素操作（L，先于泛型/EH 大改）

**设计决策**：不逐个复制 `IndexOf`/`Insert` 的补丁。先在 irgen 公共层建立 typed element
primitives：`address/load/store/copy/move/equal/release/zero`，参数包含 Zan 元素类型和
`elem_slot_words()` stride。struct 比较复用语言的逐字段/运算符语义，不用可能比较 padding
的裸 `memcmp`；含 RC 字段的 struct 复制、覆盖和删除必须逐字段遵守所有权。

### A32-2a List 全操作

* `AddRange`：容量按 `count * stride`，支持 self-AddRange 的源快照和重叠搬运。
* `Insert`：按整元素 stride 右移并 typed-store 新值。
* `Reverse`：交换完整元素，不重复 retain/release。
* `IndexOf` / `Contains`：走 typed equality，覆盖 12/32 字节 struct 及自定义 `==`。
* 删除 `irgen_call.c` 中这五条 `wide_elem_unsupported()` 拒绝路径；保留标量/引用快路径只能
  是优化，不能成为不同语义。

### A32-2b Dictionary 宽值槽

* 将 `irgen.h::dict_struct_type` 的 values 从隐含单 `i64` 语义改为带 `value_words` 的
  stride buffer；keys/hash index 仍保持现有插入序布局。
* 拆掉 `__zan_dict_set(i8*, i8*, i64, i64)` 对值 ABI 的固化：runtime helper 只负责
  find/ensure-capacity/返回 entry，具体值由调用点通过 typed element primitives 写入。
* 同步修改 new/initializer/indexer set/get/`Add`/`Remove`/`Clear`/`Values`/扩容/析构；
  覆盖替换旧值、删除后左移、hash index 失效重建和 RC 递归释放。
* Dictionary 是编译进最终 module 的内部布局，无需兼容旧二进制；同一提交内完成全部消费者，
  禁止暂留新旧双布局。

**正式测试**：扩展 `list_wide_struct_elements.zan` 覆盖五个操作；新增
`dict_wide_struct_values.zan`，用 12/32 字节纯值 struct 和含 string/class 字段 struct 覆盖
initializer/Add/upsert/indexer/Values/Remove/Clear/扩容。三档测试全部通过，且代码中不再有
这五个 List 宽元素拒绝分支或 Dictionary 单值槽假设。

## A32-3 · 统一「声明类型实例 + 方法类型参数」特化（XL）

**根因**：`zan_method_spec` 目前只以 `msym + method bind[]` 为 key；
`get_or_create_method_spec()` 明确拒绝 generic declaring type，body emission 又把
`g->cur_inst = NULL`。async 则由 `emit_user_methods()` 单独按原方法符号建立 ramp/frame/resume，
无法复用普通 method spec。

### A32-3a 泛型类的实例泛型方法（非 async 部分已完成，见 A43-A7 详情）

1. 把特化 key 扩为 `{method symbol, owner instantiation, method type args}`；mangling 同时编码
   `Pool<OwnerT>` 与 `M<U>`，重载序号只处理真正签名冲突。
2. 引入显式 substitution context，同时保存 owner type args 与 method type args；
   `resolve_type_ctx`、参数/返回类型、字段类型、receiver layout、ARC concretize 都从同一 context
   解析，停止靠互斥的 `cur_inst`/`cur_mbind` 隐式状态拼接。
3. 特化 `this` 使用 owner instantiation 的具体布局；内部 self-call、跨模板转发、任意表达式
   receiver 和递归调用都路由到同一个特化缓存。
4. 只有调用点确实无法得到具体类型参数时才发源码级诊断，不允许回落到会 abort/产坏 IR 的
   erased template。

### A32-3b async 泛型方法（已完成 2026-08-04，见 A43-A8 详情）

1. 从 `emit_user_methods()` 提取可复用的 async-specialization emitter，输入完整 substitution
   context，输出一组 ramp/frame/resume/cleanup，而不是复制第二套 CPS lowering。
2. async method spec 记录 ramp、resume、frame type、await/local/handler 布局；所有名字包含
   owner + method type args，避免不同特化共用错误 frame。
3. 调用点仍返回统一 task handle；frame result、参数槽、跨 await locals、异常和 cancellation
   全部使用替换后的具体类型与 ARC 规则。
4. 支持 static/instance、普通类/泛型类、嵌套泛型调用和 async generic 调 async generic；
   不以禁用 try/catch、引用类型或 cancellation 换取通过。

**正式测试**：新增 `generic_class_instance_generic_method.zan`、
`async_generic_method.zan`、`generic_class_async_generic_method.zan`，覆盖值/引用参数、T 成员访问、
模板转发、两种 owner instantiation、正常完成、跨 await 局部、throw/catch、取消和 RC。
三档测试全部通过；源码中删除「async generic methods / instance generic methods of a generic type
cannot be monomorphized」拒绝分支。

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

`double`/`float`/`decimal` 目标遇到整数源时不做转换，直接把整数位模式落进槽位或参数，
是**静默错值**而不是报错：`double b = 1;` 打印 `4.94066e-324`、`b == 1.0` 为 false；
`D(2)`（`double` 形参）、`return 2;`（`double` 返回）、`2 * d` 直接 LLVM 校验失败。

* 根因与修复点（各处都是「整数值遇到浮点目标」缺一次 `sitofp`）：
  * `irgen_generics.c::coerce_int_to()`——局部变量声明初始化、字段存储等所有 `zan_store_fit` 路径；
  * `irgen_generics.c::coerce_int_pair()`——混合运算两侧统一到浮点；
  * `irgen_expr.c` 二元运算：`is_float` 改为在 `coerce_int_pair()` **之后**按两侧判定，
    否则 `2 * d` 会选整数 `mul`；
  * `irgen_expr.c::emit_arg_typed()`——实参遇到 `double`/`float` 形参；
  * `irgen_stmt.c` `AST_RETURN_STMT` 返回值转换；
  * `irgen_arc.c::emit_collection_slot_store()`——`List<double>`/`Dictionary<K,double>` 元素槽
    （槽物理上是 i64，转换后再 bitcast，与读侧一致）；
  * `irgen_generics.c::zan_store_fit()`——数组元素 GEP 的 `double[]` 目标类型此前无法还原。
* 实测（`_scratch/p/54..66`）：局部/字段/形参/返回值/`d*2`/`2*d`/`d+1`/`float`/`decimal`/
  `double[]`/`List<double>`/`Dictionary<string,double>` 全部得到正确值。
* 正式测试：`tests/conformance/int_to_double.zan`（新增，通过）。
* 回归：`ctest -R "double|float|numeric|list|dict|arith|int_to"` 47/47 通过；
  全量 conformance 剩 9 项失败（`builtin_shadowing`、`field_decl_initializers`、
  `firebird_wire`、`orm_*`、`sqlserver_tds`），已用 HEAD 版本 `zanc` 复跑确认为既有失败，
  与本次改动无关。

## 依赖与提交顺序

| 顺序 | 里程碑 | 依赖 | 可并行项 |
|---|---|---|---|
| 1 | A32-0 基线 | 无 | macOS workflow 设计 |
| 2 | A32-1 字段初始化器 | A32-0 | A32-6 无凭据自动化 |
| 3 | A32-2 集合 stride | A32-1 | A32-6 |
| 4 | A32-3 泛型统一特化 | A32-2 | 无 |
| 5 | A32-4 await fast path | A32-3 async emitter 稳定 | A32-6 |
| 6 | A32-5 LLVM EH | 前述 IR/frame/layout 均冻结 | 无 |
| 7 | release gate + A32-6 外部验收 | A32-1..5；Mac/凭据 | 无 |

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

1. [x] **A33-1 立刻堵住静默崩溃**（2026-08-02 已实现）：`irgen_expr.c` 三处诊断——
   `emit_lambda_typed` 内 `this`/`base` 引用报 `cannot use 'this' here: a lambda cannot
   capture 'this' (lambdas are non-capturing)`（新增 `g->lambda_depth`）；
   `emit_expr_member_access` 报 `instance method group 'X' cannot be used as a value:
   delegates cannot capture a receiver yet`；`emit_ident` 对裸实例方法名报同样诊断。
   四个探针（`_scratch/a33*.zan`）从静默崩溃变为编译错误。
   测试：`tests/diag/lambda_capture_this.zan` / `lambda_capture_local.zan` /
   `instance_method_group.zan` / `instance_method_ident.zan`（`diag_a33_*`），`ctest -R diag_` 19/19。
2. [x] **A33-2 委托带 receiver/env（能力项，2026-08-04 已实现）**：见下「A33-2 详情」。
3. [x] **A33-2b 被写入的捕获局部按引用捕获**（2026-08-04 已实现）：见下「A33-2b 详情」。
4. [ ] **A33-3 GUI 事件绑定改成实例方法/闭包（依赖 A33-2）**：`lv.OnSelect(this.Open)`
   取代目前的静态 handler + 轮询取值。

### A33-2 详情（已完成 2026-08-04，同时关掉 A43-A5 / A43-A6）

委托值现在是一个指针的两种形态，靠**最低位**区分（`irgen_arc.c` 顶部有完整说明）：

- 偶数 —— 裸函数指针：静态方法、不捕获 lambda。仍可直接交给 C 回调，零开销。
- 奇数 —— 指向堆上闭包记录的带标记指针，记录形状
  `{ ptr fn, ptr dtor, ptr target, <按值捕获的值...>, [ptr this] }`，用对象分配器分配
  （自带 rc 头），调用形式 `fn(record, args...)`。`target` 是实例方法组绑定的接收者，
  lambda 为 null。

落点：

1. `irgen_arc.c`：`emit_closure_retain/release`（先测标记位，裸指针是 no-op；
   归零时调记录里的 dtor）、`emit_delegate_invoke`（调用点按标记位分派，两条路 phi 汇合）、
   `emit_delegate_equals`（C# 的「同函数 + 同 target」语义）、`is_rc_managed_type` 纳入
   `TYPE_DELEGATE`，类析构释放委托字段。
2. `irgen_expr.c`：捕获式 lambda 生成闭包记录与 `__zan_clo_dtor_*`（捕获值按 ARC retain，
   `this` 也是一个捕获槽）；实例方法组 `obj.M` / 裸 `M` 绑定 receiver，走
   **每方法唯一**的 thunk `__zan_mg_<fn>`（同一方法在不同点取值必须得到同一函数指针，
   否则 `E -= obj.M` 找不到当初订阅的那个）；`==`/`!=` 走 `emit_delegate_equals`;
   运算符重载调用点释放临时委托实参（`E += obj.M` 的记录由处理器列表接管）。
3. `irgen.c`：`reserve_closure_site` 给每个闭包一个独立的泄漏站点，报告不再把
   所有闭包挤到同一个源位置上。
4. `parser.c`：事件降级出的 `__Event_D` 增加 `static void op_call(self, ...)`，于是
   `E(v)` 就是 C# 的触发写法（无订阅者时是空操作，即手写的 `E?.Invoke(...)`）。

实测（`_scratch/clo1..clo5.zan`、`ev1/ev2.zan`，均 `--check-leaks` 无泄漏）：捕获局部、
捕获字符串、捕获 `this`（`c.Adder()` 返回的闭包读 `Base`）、不捕获 lambda 仍是裸指针、
委托字段赋值/改写、把 lambda 当实参传、`E += 实例方法/静态方法/捕获 lambda`、`E(v)` 触发、
`E -= 实例方法/静态方法` 正确摘除。

正式测试：`tests/conformance/delegate_closures.zan`、`event_receiver_handlers.zan`（+`.out`）。
`diag_a33_*` 四个诊断测试与 `tests/diag/lambda_capture_{this,local}.zan`、
`instance_method_{group,ident}.zan` 一并删除——它们断言的正是现在已经支持的写法。
回归 `ctest -R 'delegate|event|lambda|closure|leakcheck' -E 'zgm|gui|webview|selfhost'`：
相关项（conformance/determinism/leakcheck 三档）全通过；其余失败仍是工作区未提交的
stdlib 改动导致的 `'DbParams' has no member 'Create'` 这类编译错误，与本次无关。

### A33-2b 详情（已完成 2026-08-04）

捕获的是**变量**而不是值：lambda 里写被捕获的局部，写的就是那个局部，外层随后读到新值。

* 落点：被 lambda 赋值的局部不再放在栈帧里，而是搬进一个**堆单元**，形状与闭包记录
  相同（`{ fn = null, dtor, target = null, value }`，`irgen_expr.c` 的 boxed-locals 注释
  是完整说明），它的存储槽变成指向单元内 `value` 的指针，于是外层与闭包的读写都落在
  同一块内存上。`irgen_stmt.c::emit_boxed_var_decl` 负责声明，`irgen_expr.c` 的
  `emit_box_cell` / `box_value_ptr` / `local_is_lambda_written` 是实现。
* 生命周期就是闭包的生命周期：声明作用域持一个引用，每个捕获它的闭包各持一个，最后
  一个出门的跑单元的 dtor（`irgen_arc.c::emit_closure_record_release`，从
  `emit_closure_release` 里拆出来复用），因此闭包活得比栈帧长时变量也跟着活。
  `irgen_generics.c` 把作用域退出拆成 `local_slot_owns_rc`（赋值时换引用）与
  `local_owns_arc`（退出时释放槽），boxed 局部只按整单元释放，避免双重释放。
* 只装箱**确实被写**的局部（`node_writes_ident` 认赋值、`++`/`--`、`ref`/`out`）；
  只读捕获仍是按值拷贝，更便宜且不可区分。async 局部本来就在协程帧里、天然共享，跳过。
* 正式测试：`tests/conformance/closure_mutable_capture.zan`（+`.out`）覆盖 lambda 写、
  外层写同一变量、两个闭包共享一个变量、循环里驱动闭包、只读捕获读到共享新值、
  变量活得比声明它的栈帧长；conformance/determinism/leakcheck 三档全过
  （`ctest -R closure` 6/6）。

---

# A34 · zanc 无库输出：不能编译 DLL/.so/.dylib/.a（2026-08-01，已实测）

**根因**：zanc 的链接路径（`src/compiler/main.c`）只支持**可执行文件**输出，三条路径都
固定链接 CRT 启动对象、生成带入口的 PE/ELF，没有 `-shared`/`-dynamiclib`/静态归档分支；
`templates/library/*` 的 `target=dll` 写进了 zan.proj 但编译器从未消费。

**缺口（跨平台全家桶）**：Windows `.dll`/`.lib`、Linux `.so`/`.a`、macOS `.dylib`/`.a`。

**任务**：

1. [x] **A34-1 编译器新增库输出模式（已实现，2026-08-01）**：`main.c` 按 `-o` 后缀自动切
   库模式；`irgen_emit.c` 仅库模式对 `public` 方法跳过 `zan_set_module_local`（GlobalDCE 后
   仍导出），Windows 共享库发射 `DllMain`（返回 1）。Windows DLL 走 bundled
   `ld -shared -e DllMainCRTStartup` + `dllcrt2.o` + `-out-implib`（`foo.dll`→`libfoo.dll.a`）；
   Linux `.so` 走 `ld.lld -shared`、macOS `.dylib` 走 `ld64.lld -dylib` + libSystem.tbd；
   runtime 对象按需链接，纯函数库零 runtime，cross 共享库需要 runtime 时明确报错。
   测试：`tests/emit_lib/` + `run_emit_lib.cmake`（`emit_lib_static` / `emit_lib_windows_dll`
   含消费者经 `[DllImport]` 链接运行比对 / `emit_lib_linux_so` / `emit_lib_macos_dylib`），
   `ctest -R emit_lib` 4/4；`--list-targets` 各平台均产出库。
2. [ ] **A34-2 IDE 消费库目标**：`ZanIDE.Workspace.zan` 的 `CreateProjectT` 已把
   `target=dll` 写进 zan.proj，但 `CompileToExe`/`RunBuildBegin` 一律按 exe 编译。
   库项目应编译成库（Build 产出库文件、不 Run），Publish 按目标平台出对应
   扩展名；`dll-class`/`dll-export` 模板恢复可建可用。
3. [ ] **A34-3 库的消费侧**：同一 zan.proj 内 SmartInputs 已支持把兄弟 `.zan`
   一起编译（源码复用）；A34-1/2 之后补「引用已编译库」的 `--link-lib`/
   `link =` 路径，与 `[DllImport]` 消费外部原生库的既有机制对齐。

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
| A43-A2 | 默认参数值不填充：`F(1)` 调 `F(int a, int b = 2)` LLVM 校验失败；`new A(1)` 静默不调用构造器，字段留 0 | 19,81–88 | ✅ 已修（见下 A43-A2 详情） |
| A43-A3 | 接口默认方法（`interface I { int G() { return 42; } }`）经接口变量调用一律返回 0；经类变量调用报 `'C' has no member 'G'` | 44,51,52,80,100–102 | ✅ 已修（见下 A43-A3 详情） |
| A43-A4 | `int? v = a?.x;` 运行时崩溃（0xC0000005）；此前是 `PHI node operands are not the same type` | 21,43,110–145 | ✅ 已修（见下 A43-A4 详情） |
| A43-A11 | `Nullable<T>`：值类型可空缺少真实表示（值 + has-value），`int?` 一度只能报错拒绝 | 130,144 | ✅ 已修（见下 A43-A11 详情） |
| A43-A5 | `event H E;` + `a.E += P.OnE; a.Fire();` 编译通过、静默不触发（输出只有 `end`） | 28 | ✅ 已修（见下 A33-2 详情） |
| A43-A6 | 捕获式 lambda：`(a) => a + k` 报 `use of undeclared identifier 'k'`；捕获字段时诊断还指向 `stdlib/System/Security/Cryptography/Md5.zan`（位置也错） | 22 | ✅ 已修（见下 A33-2 详情） |
| A43-A7 | 泛型类里的实例泛型方法 `Pool<T>.M<U>()` 运行崩溃（0xC0000005） | 13 | ✅ 已修（A32-3a，见下 A43-A7 详情） |
| A43-A8 | async 泛型方法返回垃圾值（打印 `-1886711216`） | 56 | ✅ 已修（A32-3b，见下 A43-A8 详情） |
| A43-A9 | `static A()` 只在首次 `new` 时执行：先读 `A.n` 得 0，`new A()` 后才是 7（C# 首次访问静态成员即触发） | 08,40,95–99 | ✅ 已修（见下 A43-A9 详情） |
| A43-A10 | `readonly` 只解析不强制：`public readonly int x;` 在类外 `a.x = 6;` 编译通过并改值 | 71,72,90–94 | ✅ 已修（见下 A43-A10 详情） |

### A43-A2 详情（已完成 2026-08-04）

* 根因：调用点从不补齐缺省实参。方法调用直接把源实参数交给 `zan_call2`（参数个数
  与被调用者不符）；构造器更糟——`find_ctor()` 只按 `param_count == argc` 精确匹配，
  少一个实参就一个都不匹配，于是**根本不调用构造器**，对象字段停在 0。
* 修复点：`irgen_builtins.c::fill_default_args()`（在 `pack_params_args` 之前，于
  `irgen_call.c` 的 5 个方法调用路径调用）；`irgen_expr_core.c::fill_ctor_default_args()`
  + `irgen_expr.c`（`new T(...)`）、`irgen_emit.c`（`: base(...)` / `: this(...)`）、
  `irgen_stmt.c`（struct/class 局部变量的 `new`）三处回退。缺省表达式在调用点求值，
  与 C# 语义一致；重写幂等。
* 实测：静态/实例/多缺省/`double` 缺省/`this.M()` 与裸 `M()`/构造器/`: base` 链/
  `: this` 链/struct 构造器全部正确（探针 81–88）。
* 正式测试：`tests/conformance/default_parameters.zan`。
* 回归：`ctest -R "default_param|ctor|constructor|overload|params|generic|struct|interface|record|field_decl|int_to"`
  99 项里仅 `field_decl_initializers`（及其 leakcheck）失败，已确认是 A32-1 的既有失败。

### A43-A10 详情（已完成 2026-08-04）

* 根因：`MOD_READONLY` 只在 parser 里存了位，没有任何一处检查写入。
* 修复点分两层，因为写入目标的类型不总在同一阶段可知：
  * `checker.c`：新增 `current_type_sym` / `in_ctor` 上下文与 `check_readonly_assignment()`，
    覆盖 `x = v`、`this.x = v`（checker 没有局部作用域，只能处理这两种形态）；
  * `irgen_expr.c::check_readonly_store()`：`obj.x = v` 的接收者类型要到 irgen
    才有（`infer_expr_type` + 局部变量表），并新增 `g->current_fn_is_ctor` 判断
    「是否在声明该字段的类型的构造器内」。
  * 复合赋值 `x += 1` 在 parser 阶段就降级成 `AST_ASSIGNMENT`，因此同一处即可覆盖。
* 规则：readonly 字段只能在**声明它的类型的构造器**里赋值（声明初始化器就降级在构造器内），
  其他位置一律报 `cannot assign to readonly field 'x' outside a constructor of 'T'`。
* 正式测试：`tests/conformance/readonly_fields.zan`（构造器赋值、`this.` 赋值、
  声明初始化器、同类中的可变字段仍可写）+ `tests/diag/readonly_assign_method.zan`、
  `tests/diag/readonly_assign_outside.zan` 两个编译失败诊断。
* 回归：`ctest -R "readonly|diag_|default_param|struct|field|record|conformance_class|property"`
  83 项里只有既有的 `field_decl_initializers` 失败；`scripts/_ide_compile_check.ps1`
  以 `IDE_COMPILE_EXIT=0` 通过（stdlib/IDE 全量编译无误报）。

### A43-A9 详情（已完成 2026-08-04）

* 根因：`irgen_emit.c` 的 `is_static = !is_ctor && ...`，即**构造器一律当实例构造器**。
  `static A() { }` 于是被注册成 A 的零参构造器，只有 `new A()` 才会跑它；
  只读静态字段的程序看到的是未初始化值。
* 修复：识别带 `static` 的构造器为类型初始化器 —— 发射为无 `this` 的 `T_cctor`、
  不进 ctor 表（`new A()` 不会选中它）、不做 base/this 链与实例字段初始化器，
  并在 `emit_main_method` 的静态字段初始化器之后按声明顺序逐个调用。
* 与 C# 的差异（有意）：C# 是首次使用该类型时惰性触发；这里在程序入口一次性跑完，
  单程序的可观察顺序相同，且省掉每个静态访问点的 guard 判断。泛型类型只按擦除声明
  发射一次，不随实例化重复。
* 实测：先读静态字段即为 7（08、40）、静态字段初始化器先于静态构造器（96：`m = n*2` → 6）、
  只有静态构造器的类仍可 `new`（97）、静态构造器与实例构造器共存（98）、
  `static readonly` 在静态构造器里赋值合法（99）。
* 正式测试：`tests/conformance/static_constructor.zan`。
* 回归：`ctest -R "static|cctor|readonly|default_param|ctor|field|record|struct|generic|async"`
  206 项，失败仅 `field_decl_initializers`（既有）与 `golden_async_delay_ir`/
  `golden_async_socket_ir` —— 后两者用 HEAD 版 `irgen_emit.c` 重建后同样失败，
  来自工作区里未提交的 stdlib 改动，不是本次修改引入。

### A43-A3 详情（已完成 2026-08-04）

* 根因：带方法体的接口方法只被绑定成**接口的**成员就到此为止 —— irgen 从不遍历
  `AST_INTERFACE_DECL`，默认体永远不发射；于是经接口变量调用得 0，经类变量调用
  直接报 `'C' has no member 'G'`。
* 修复：`binder.c` 新增 pass 1.5（在类型注册之后、成员绑定之前）
  `bind_default_interface_methods()`：把实现类型没有自己覆盖的默认接口方法**浅拷贝**
  成该类型的普通方法（节点身份必须独立，irgen 用声明节点做方法符号与函数的键）。
  之后一切照普通方法走：绑定、发射、分发都不需要特殊分支，默认体里的 `this.F()`
  自然解析到实现类型的 `F`。接口继承接口的默认方法递归拷贝（深度上限 8）。
* 实测：经接口变量（44=42）、经类变量（80=42）、默认体调用 `this.F()`（100=5|5，
  类自己覆盖时取覆盖版 99）、带参数与返回 string（101）、接口继承链（102=6|6）。
* 正式测试：`tests/conformance/interface_default_methods.zan`。
* 回归：全量 `ctest`（994 项）失败 100 项，其中 91 项在**未改动 binder 的基线**
  上同样失败（工作区里未提交的 stdlib 改动删掉了 `X.Create(...)` 工厂，测试仍在调用，
  例：`'StreamWriter' has no member 'Create'`），另外 9 项 leakcheck 在本次改动的
  构建上反而全部通过（上一次是并发跑测时的偶发失败）。

### A43-A4 详情（已完成 2026-08-04）

崩溃的不是 `?.`，是 **`T?` 的表示**。`TYPE_NULLABLE` 在 `map_type` 里落到 `default:`
→ 裸 `i8*`，于是：

* 值类型：`int? v = 5;` 把 5 当地址存进指针槽，任何使用都崩（`Convert.ToString(v)`
  当字符串解引用 5）或 LLVM 校验失败（`v + 1`）。顺带 `int? v = 0;` 与 `null`
  无法区分。
* 引用类型：`A? a = new A(); a.x` 读的是无类型指针上的字段 → 0，`I? i` 上的调用
  分发不到任何函数（**静默错值**，比崩溃更危险）。

修复分三处：

1. `binder.c`：`T?` 在引用类型上**不再包装** —— 引用本身就允许 null，`A?` 就是 `A`，
   类身份不再丢失。
2. `binder.c`：`T?` 在值类型（含用户 `struct`、`enum`）上先直接诊断
   `nullable value type 'int?' is not supported yet`，不再误编译。真实的
   `Nullable<T>` 表示登记为 A43-A11，已在下文实现，诊断随之删除。
3. `parser.c`：`looks_like_var_decl` 之前只认内建类型的 `int?`，`A? a = ...`
   被当成三元表达式（`expected ':', got ';'`）。现在 `IDENT ?` 后跟标识符再跟
   `=` 或 `;` 判为声明，`a ?? b`、`a?.b`、`a?[i]`、`c ? a : b` 仍是表达式。

实测：`A? a = new A(); a.x` → 2、`A? b = null; b == null` → 成立（140）、
`I? i = new C(); i.F()` → 8（145）、`string? s = null; s ?? "d"`（131、141）、
三元与 `?.` 未受影响（142、143）、`int?`/`S?` 报诊断（130、144）。

正式测试：`tests/conformance/nullable_reference_types.zan`
（`tests/diag/nullable_value_type.zan` 在 A43-A11 落地后删除）。

回归：全量 `ctest`（998 项）新增失败在**基线**上逐一复跑同样失败（`X.Create(...)`
工厂被工作区未提交的改动删掉），其余是并发跑测时的超时，串行复跑通过。

### A43-A11 详情（已完成 2026-08-04）

值类型 `T?` 现在有真实表示：`{ payload, i1 }` —— 值本身加一个 has-value 标志。
LLVM 侧是**具名** struct `zan.nullable.<payload>`（`irgen.c` 的 `nullable_type_of`），
名字前缀就是识别依据，形状相同的用户 struct 不会被误判成可空值。

改动落在值的每个进出边界，而不是某一处特判：

1. `binder.c`：值类型 `T?` 不再诊断，包成 `TYPE_NULLABLE`（引用类型仍然不包，
   `A?` 就是 `A`）。
2. `irgen.c`：`map_type` 增加 `TYPE_NULLABLE` 分支（此前落到 `default:` 的裸 `i8*`
   就是 A43-A4 里那些崩溃的根因），并提供包/解包与标志读写的小工具。
3. `irgen_generics.c`：`coerce_int_to` 是所有存储的共同入口，在这里把
   payload 包起来、把 `null`（i8* null）变成 none、把另一种 payload 宽度的可空值
   （`a + 1` 的结果算在 i64 上）转成槽位的形状且保留标志；`zan_store_fit` 对可空槽
   先转换再存，避免把 `null` 当成"指针后面的 struct"去复制。
4. `irgen_expr.c`：二元运算的算符部分拆成 `emit_binary_op_values`，可空提升
   （`emit_nullable_binary`）在解包后复用同一套 lowering —— `==`/`!=` 对 `null`
   只看标志，两个可空值比较遵循 C#（都为 null 相等），`<`/`>` 等有 null 参与即
   为 false，算术运算 null 传染，`??` 解包；`/`、`%` 在结果为 null 时把除数换成 1，
   免得除零检查在一个根本不会执行的运算上报错（C# 的提升运算符不执行该运算）。
   另外 `v.HasValue`/`v.Value`（null 时按运行时错误报
   `Nullable object must have a value`）、`(int)v` 显式解包、`(int?)x` 包装、
   `T?` 形参传值。
5. `irgen_call.c`：`v.GetValueOrDefault()`、`v.ToString()`，以及
   `Console.WriteLine/Write` 直接打印可空值（null 打印空串，同 C#）。
6. `irgen_stmt.c`：`return` 到 `T?` 返回类型，`var v = <可空表达式>` 保留可空类型。
7. `irgen_expr_core.c`：`infer_expr_type` 认识 `HasValue`/`Value`/
   `GetValueOrDefault` 和"算术结果继承可空性"。

实测（`_scratch/a11.zan`、`_scratch/a11b.zan`、`_scratch/a11c.zan`）：
`int? a = 5; a.Value` → 5；`b = null` 时 `b.HasValue` → 0、`b ?? 7` → 7、
`b == null` 成立；`a + 1` → 6、`b + 1` → null、`b / 0` → null（不再触发除零检查）；
`int? Half(int)` 返回 `null`/值经 `??` 取出 → -1 / 5；`Console.WriteLine(a)` → `5`，
null 打印空行，`"a=" + a` → `a=5`、`"f=" + f` → `f=`；`a == g`（都为 5）相等、
`f == f`（都为 null）相等、`a == f` 不等、`f > 3` 为 false；字段 `int? level`
默认为 null、可写入可写回 null；`Color?`、`bool?`、`long?`、`double?`、
`Point?`（`pp.Value.x + pp.Value.y` → 3）都正确；`v.Value` 在 null 上运行时报
`runtime error: Nullable object must have a value`（退出码 70）。

正式测试：`tests/conformance/nullable_value_types.zan`（+`.out`，conformance 目录
是 glob 注册，无需改 CMakeLists）。原来的 `diag_nullable_value_type` 测试与
`tests/diag/nullable_value_type.zan` 一并删除——它断言的正是现在已经支持的写法。

回归：`ctest -R 'conformance|diag' -j 8` → 343 项中 45 项失败，失败全部是
`'X' has no member 'Create'` 这类工作区里未提交的 stdlib 改动造成的编译错误
（redis/orm/gui/sdk 等），加上依赖真实桌面窗口的 `win_automation_smoke`；
`conformance_nullable_reference_types` 与新增的 `conformance_nullable_value_types`
都通过。

仍缺（登记为 A43-B19）：`int?[]` 这种可空元素数组 parser 不接受
（`expected variable name`）。

### A43-A7 详情（已完成 2026-08-04）

`Pool<T>.M<U>()` 的崩溃不是一个 bug，是「类型参数被抹成裸指针」在四个边界上各漏一次：

1. **方法特化的 ABI**（`irgen_emit.c`）。泛型方法原本只在「值得特化」时才单态化，
   其余走 erased 模板：`M<int>(7)` 把 7 当指针传进去，返回值又被调用方当指针读回来，
   于是 `Pool_Wrap$string` 的 IR 里参数和返回全是 `ptr`。现在只要调用点能把每个方法类型
   参数绑定到具体类型，就一定发一份具体签名的特化，erased 变体只服务绑不出来的调用点。
2. **特化 key 少了 owner instantiation**（`irgen.h`/`irgen_emit.c`/`irgen_call.c`）。
   `zan_method_spec` 增加 `owner_inst`，key 变成 `{method symbol, owner instantiation,
   method type args}`，名字里编码 `Pool$int_Echo$$Box` 这样的双重实例化；
   `get_or_create_method_spec()` 不再拒绝泛型声明类型的实例方法，body emission 也不再把
   `g->cur_inst` 清空，参数/返回类型先按 method bind、再按 owner instantiation 替换。
3. **body 内的 self-call 要路由到同一个缓存**（`irgen_call.c`）。`this.Echo<U>(x)` 里
   receiver 类型是未绑定的 `Pool<T>`，此前推不出 owner instantiation 就回落到 erased 变体：
   `Pool$int_Forward$$Box` 直接调 `Pool_Echo$int`，返回的是**借用**的参数值，调用方却按
   拥有 +1 处理，程序结束时二次释放（0xC0000374 堆损坏）。现在 receiver 落在正在发射的
   实例化上时用 `g->cur_inst` 补齐 owner。
4. **`T` 字段的 ARC 与格式化**（`irgen_expr.c`/`irgen_expr_core.c`/`irgen_generics.c`）。
   `item = x`（隐式 `this.item`）用字段声明类型判断是否 RC 托管，`T` 不是托管类型于是
   裸存指针：`Pool<Box>.Set(new Box(5))` 后临时对象被释放，字段成悬垂指针（而 `Get()`
   那侧是 retain 的，收支不平）——改成用 `field_store_type()` 拿实例化后的具体类型。
   同理 `infer_expr_type` 对裸字段名 `item` 现在也做 `concretize`，`item + "/" + x`
   才知道 `T=int` 要按整数格式化，而不是拿位模式当字符串跑 `strlen`。

实测（`_scratch/a7c.zan` 等探针）：`Pool<string>` / `Pool<int>` / `Pool<Box>` 三种实例化下的
`Set/Get`、`Echo<int|string|double|Box>`、`Pair<U>`（`item + "/" + x`）、`PairThis<U>`、
`Describe<U>`（`this.item.ToString()`）、`Forward<U>`（模板内转发）、`CountOf<U>(U[])`
全部输出正确，退出码 0；`--check-leaks` 亦为 0。

正式测试：`tests/conformance/generic_class_instance_generic_method.zan`（+`.out`）。

回归：`ctest --test-dir build -R "generic|leakcheck|diag_" -j 8` → 356 项中 43 项失败，
全部是工作区里未提交的 stdlib 改动导致的编译错误（`'X' has no member 'Create'`、
ide/stdlib 的 parse error）；所有 `generic_*`、`diag_*` 通过，新增用例在
conformance/determinism/leakcheck 三档均通过。唯一一个能编过却崩的
`leakcheck_field_decl_initializers` 已用 HEAD（fd59fcb）的干净 worktree 编译验证
为既存缺陷，与本次改动无关。

### A43-A8 详情（已完成 2026-08-04，A32-3b）

async 泛型方法此前根本没有特化：`emit_user_methods()` 只按原方法符号发一份 erased 的
ramp/frame/resume，`get_or_create_method_spec()` 直接 `return -1`，模板型 async 泛型方法
还专门发一条「cannot be monomorphized today」诊断。于是 `await P.Id<int>(1)` 拿到的是
把 frame 指针位模式当 `int` 读出来的垃圾值。

1. **可复用的 async emitter**（`irgen_emit.c`）。原来内联在 Pass A/Pass B 里的两段代码
   抽成 `declare_async_method()`（ANF 归一化 + async scan + frame 布局 + ramp/resume 声明）
   与 `emit_async_method_ir()`（ramp 体、resume 状态机、cleanup），二者都只吃一个
   `method_body_work_t`；Pass A/B 与方法特化共用同一份 lowering，没有第二套 CPS。
2. **async method spec**（`irgen.h`/`irgen_emit.c`）。`zan_method_spec` 增加
   `is_async` + `async_ir`（ramp、resume、frame type、await/local/handler 布局），
   key 仍是 `{method symbol, owner instantiation, method type args}`，名字随之变成
   `Pool$string_Wrap$$int` / `…$resume` / `…$cleanup` / `…$frame`，不同特化不会共用 frame。
   声明与 body 发射都在绑定生效（`cur_mtps`/`cur_mbind`/`cur_inst`）的上下文里进行，
   所以参数槽、返回、跨 await 的局部、异常与取消路径全部是替换后的具体类型和 ARC 规则；
   调用点仍然返回统一的 task handle（ramp 返回 `i8*`），await/spawn 协议不变。
3. **async scan 也要按上下文解析**（`irgen_async.c`）。`T x = …` / `foreach (T e …)` /
   `catch (E e)` 之前用 `zan_binder_resolve_type()` 解析声明类型，特化体里 `U tail = x;`
   于是拿到指针宽度的 frame 槽，`Pool<string>.Wrap<int>` 一挂起就崩；改用
   `resolve_type_ctx()`。
4. **await 结果的静态类型**（`irgen_expr_core.c`）。`s.Echo<int>(42)` 与不加限定的
   `Id<int>(8)` 这两条推断路径只替换了类的类型实参、没有替换方法自己的，
   `Console.WriteLine(await s.Echo<int>(42))` 因此按指针重载打印 → 0xC0000005。
   两处改为经 `method_ret_type_at()` 绑定方法类型参数后再套 owner 实例化。

删除的拒绝分支：`method_is_tp_template()` 对 async 模板的
「async generic methods cannot be monomorphized today」诊断（模板现在与非 async 模板
一样，只以特化形式存在）。

正式测试：`tests/conformance/async_generic_method.zan`、
`tests/conformance/generic_class_async_generic_method.zan`（各含 `.out`），覆盖
值/引用/`double` 参数、T 成员访问的模板方法、跨 await 的 U/T 局部、模板转发、
async generic 调 async generic、frame 内 catch、异常穿出到 awaiter、
`Task.Spawn` + `Task.Cancel`、两种 owner instantiation（`Pool<string>` / `Pool<int>` /
`Pool<Item>`）与 ARC；conformance/determinism/leakcheck 三档均通过。

回归：`ctest -R 'generic|async|await|coroutine' -j 8` → 144 项中仅 `golden_async_delay_ir`、
`golden_async_socket_ir` 失败，二者是过期的黄金 IR 期望（期望 `%zan.co.timer` /
`@__zan_co_timers`，而定时器早已移进 runtime，编译器源码里已无此符号），与本次改动无关。
`ctest -R 'conformance_|leakcheck_' -j 8` 的其余失败仍是工作区未提交的 stdlib 改动导致的
编译错误（`Convert.ToString`、`SemaphoreSlim.Create` 等已被删除的成员）。

## A43-B 语法缺失（parser 层不接受，按价值排序）

| # | C# 语法 | 探针 | 现状 |
|---|---|---|---|
| A43-B1 | `Func<...>` / `Action<...>` | 26,39 | `undefined type`；只能自定义 `delegate` |
| A43-B2 | `using (res) { }` 确定性释放 | 03 | `using` 只做命名空间导入 |
| A43-B3 | 元组 `(int, string)` 与解构 | 01 | `expected type`；多返回值靠 `out`/`ref` |
| A43-B4 | 命名实参 `F(b: 2)` | 17 | `expected ')' , got ':'` |
| A43-B5 | 模式变量 `is T x` / `case T x:` / `when` 子句 / `is not null` | 23,34,35,45 | 全部 parse 失败 |
| A43-B6 | switch 表达式 `x switch { ... }` | 16 | `expected ';', got 'switch'` |
| A43-B7 | 扩展方法 `this int v` 形参 | 15 | `expected ')' , got 'IDENT'` |
| A43-B8 | 交错数组 `int[][]`、多维数组 `int[,]` | 24,25 | `expected variable name` |
| A43-B9 | 索引器 `public int this[int i]` | 37 | `expected member name` |
| A43-B10 | 插值格式串 `{v:D4}` | 50 | `expected ')' , got ':'` |
| A43-B11 | 局部函数 | 05 | 无 AST 节点 |
| A43-B12 | `implicit` / `explicit operator` | 06 | `undefined type 'implicit'` |
| A43-B13 | 泛型变体 `interface I<out T>` | 07 | `expected '>', got 'out'` |
| A43-B14 | `checked` / `unchecked` | 02 | 无关键字 |
| A43-B15 | `Task` / `Task<T>` 类型名与 API | 12,31,32 | `undefined type 'Task'`（`async` 本体可用） |
| A43-B16 | `KeyValuePair<K,V>` | 27 | `undefined type`；字典只能遍历 `Keys` |
| A43-B17 | LINQ `orderby` / `join` / `let` / `group into` | 14 | 查询语法只支持单 `from...where...select` |
| A43-B18 | `init` 访问器、`readonly struct` / `ref struct` | 09 | 无对应修饰符 |
| A43-B19 | 可空元素数组 `int?[]` | a11b | `expected variable name`；`int?` 本身已支持 |

## A43-C1 类型化查询不能跨语句组装（dbgen，已记录未修）

`this.Post.Where(a => ...).OrderByDescending(a => a.id).ToListAsync()` 只在
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

> 已实测可用、别再当缺失：带参构造器与重载、`: this(...)` / `: base(...)`、方法重载、
> `public/private/protected/internal`、`partial`、`#region/#endregion`、`const`、
> `static readonly`（读侧）、属性 get/set、泛型类/方法/具名约束、interface、delegate、
> 无捕获 lambda、运算符重载、try/catch/finally、非泛型 `async/await`、`yield return`、
> `enum`、`virtual/override/abstract/base`、`params`、`record`、基础字符串插值、
> `nameof`、原始字符串、`lock`、`goto`、`switch` 语句、List/Dictionary、
> `new int[]{...}`、集合与对象初始化器、`??`。

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

# A45 · List<T> 实参不做类型实参检查的 typecheck 洞（2026-08-06，编译器缺陷记录）

**现象**：`examples/gui_charts` 的 Heatmap 演示闪退（静默 SIGSEGV）。
回溯：`ChartSeries_Number ← ChartSeries_Value ← ChartView_CacheFingerprint ←
Charts_DemoHeatmap` —— 演示把 `List<int>` 传给了
`ChartSeries.Named(name, type, List<ChartData>)`，裸整数被当成 `ChartData*`
解引用。

**根因**：checker 不比较泛型容器实参的类型实参，`List<int>` 可以顶替
`List<ChartData>` 通过编译。最小探针（`zanc` 编译通过，EXIT=0）：

    class Box { int x; }
    class P {
        static void Take(List<Box> b) { }
        static void Main() {
            List<int> v = new List<int>();
            v.Add(1);
            P.Take(v);    // 应当编译报错，实际通过
        }
    }

**现状处理**：演示侧改用正确工厂 `ChartSeries.Of(name, type, List<int>)`
（演示本身是 API 误用，不属于缺陷绕过）；checker 修复超出本次任务范围，
记录在此。将来修 checker 时：比较泛型容器的类型实参（不变式即可），把上面
的探针落为 `tests/diag/` 负例（期望编译报错）。修复前注意：任何向容器形参
传容器的调用点误用都拦不住，只能靠运行时崩溃暴露——重构 stdlib 容器调用时
优先核对工厂签名。

# A46 · 设计器内置类型有一半没接到真控件（2026-08-06，formgen 缺口记录）

**现象**：`.zform` 设计支持 72 种内置类型（`src/compiler/formgen.c` 的 `fg_kind_of`
表，0..71），但代码生成只映射了其中一部分，其余全部落到兜底分支
`return "Label"`（`formgen.c` 的 `fg_widget_type`）——表单能编译、能显示
标题，控件本体是假的。

**回落成 Label 的类型**（21 个真缺口，另有 `title`/`text`/`ellipsis` 三个
本就该是文本，不算）：`date`(10) `time`(11) `upload`(12) `color`(13)
`table`(23) `alert`(28) `tooltip`(33) `menu`(38) `listview`(40)
`datatable`(41) `chart`(43) `scrollbar`(46) `loading`(47) `popover`(50)
`popconfirm`(51) `notification`(52) `ribbon`(53) `icon`(54)
`contextmenu`(55) `layer`(56) `timer`(57)。

**被降级的类型**：`card`(18) / `tabs`(36) / `dockpanel`(61) 都映射到
`Panel`——`Card` 的边框标题、`Tabs` 的标签页、`Dock` 的停靠边全部丢失，
`fg_default_dock` 只给 dockpanel 填了个默认停靠值补偿。

**分两类处理**：

* [ ] **A46-1 只差接线**（`stdlib/Gui` 里已有真控件，`fg_widget_type` 加映射
  即可，必要时补 `fg_pref_h` / `fg_is_container` / 选项透传）：
  `Table`→`Widget/Table.zan`、`ListView`→`Widget/ListView.zan`、
  `Tooltip`、`Popover`、`ContextMenu`、`Menu`、`Ribbon`、`Layer`、
  `Scrollbar`、`Ellipsis`、`Spin`(loading)、`Icon`→`Gui/Icon.zan`、
  `DataTable`→`Component/DataTable`、`Chart`→`Component/Chart`、
  `Card`→`Widget/Card.zan`、`Tabs`→`Widget/Tabs.zan`、
  `Dock`→`Component/Dock.zan`。
* [ ] **A46-2 控件本身不存在**，要先在 `stdlib/Gui` 写出来再接：
  日期选择、时间选择、上传、取色、`Alert`、`Popconfirm`、`Notification`、
  `Timer`（非可视，需决定生成成字段还是控件）。

**A46-3 菜单栏与下拉菜单**：`toolbar`(58) 生成的是真 `ToolStrip`，缺的是
菜单能力——没有 `MenuBar` 类型，工具项挂不了下拉菜单，属性检查器对工具项
只能编辑一行文本。需要：`MenuBar` 内置类型 + 项的子菜单模型 + 检查器的
多级编辑 UI。

**判定标准**：每个接上的类型在 `tests/conformance/` 有一个 `.zform` 驱动的
用例，断言生成代码里出现对应控件类名（而不是 `new Label`），并跑通
`scripts\test.ps1 smoke`。

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
* [ ] **A47-2 `build/toolchain` 自嵌套**：实测嵌套到 32 层
  （`build/toolchain/toolchain/toolchain/...`，每层都带一份 stdlib 和
  openssl），`build/toolchain` 单独占 1.29 GB / 3205 文件。`build/` 是
  git-ignored 的一次性产物，但说明某个发布/拷贝步骤把目标目录拷进了自己
  （`scripts/publish_ide.ps1` 的注释已经点出要避免 `toolchain\toolchain`）。
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
