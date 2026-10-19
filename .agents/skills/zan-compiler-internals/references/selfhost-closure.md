# 自举闭环契约（src/selfhost）

自举编译器是语言语义的权威实现，C 宿主（`src/compiler/`）只为引导 gen1 而存。
固定点门（`ctest -R selfhost_fixed_point`）断言 **gen2 与 gen3 的 LLVM IR 文本逐字节相同**——
这是自举后端对自己的不动点证明，比"gen1 能编出像样的 IR"强得多。

本文记录闭环会塌的若干坑，每个都是"改一处、门红一次"换来的。

## 0. 单一源清单

`scripts/selfhost_sources.txt` 是自举源清单的**唯一真相**，ctest harness
（`tests/run_selfhost.cmake`、`tests/run_fixedpoint.cmake`）与
`scripts/bootstrap.{bat,sh}` 全部读它。

- 理由：曾经 harness 里抄一份、bootstrap 脚本里又抄一份（还停在 9 文件的老子集），
  新增自举源时闭环悄悄少编一个文件，门红了很久没人知道红在哪。
- 动作：新增/删除自举源，只改 `scripts/selfhost_sources.txt`。

## 1. ARC 临时值必须落 entry 槽位

- 动作：自举 irgen 把 ARC 托管的表达式临时值登记为待释放时，若当前块不是函数
  entry 块，就把它落到一个 **entry 块槽位**（alloca + 存 null + 存值），
  释放时再 load 出来；分支没走时槽位是 null，`zrelease(null)` 是空操作。
- 理由：临时值的释放点固定在**语句末尾**，而它可能生成在三元/if/循环体的
  嵌套块里。直接用寄存器值去释放，会在合并块里违反 LLVM 支配关系
  （`Instruction does not dominate all uses`）。C 宿主对 owned call temp 用
  `emit_entry_alloca` 落槽位，自举必须同法。
- 边界：只在 `eaActive`（方法体发射期，entryAllocas 会被拼到 entry 块）为真时
  落槽位；异步 resume 函数没有 entryAllocas 机制，仍靠 CPS 帧槽。

## 2. 标签只能经 `Label()` 发出

- 动作：自举 irgen 里所有基本块标签必须走 `Label(n)`（它会同步 `curLbl`），
  禁止手工 `L("L" + n + ":")`。
- 理由：`curLbl` 是 phi 记录**前驱块名**的唯一来源。手工发标签不更新它，
  后续 `&&`/`||` 合流时 phi 会把前驱记成旧块，LLVM 报
  `PHI node entries do not match predecessors`；自举编译器自身调用委托
  （delegate）时就踩过这个坑。
- 边界：手工发标签的地方（delegate 调用、异步 resume）必须一并改成 `Label()`。

## 3. 写文件必须二进制模式

- 动作：自举 irgen 的 `File.WriteAllText` 内建用 `fopen(path, "wb")`；
  常量 `.mode_wb` 的值必须是 `"wb\00"`（3 字节），不是 `"w\00"`。
- 理由：文本模式在 Windows 上把每个 LF 翻成 CRLF。于是 gen1 写出的 `.ll`
  是 LF、gen2 写出的 `.ll` 是 CRLF，**内容完全相同但字节不同**，固定点永远
  不成立，而且 diff 看上去"只有换行符不同"，极难定位。
- 边界：与 stdlib `File.WriteAllText`（用 `"wb"`）保持一致；常量名与实际
  字节数必须同时改（长度写错会变成越界 GEP）。

## 4. 自带的运行时回退实现必须是 weak

- 动作：自举 irgen 内联发出的协程调度器（`zan_co_sched_init` /
  `zan_co_ready` / `zan_co_delay` / `zan_co_sched_run`）必须用
  `define weak`，与 C 宿主的 `zan_runtime_fallback`（COMDAT/weak）同法。
- 理由：这些符号 C 运行时（rt_io.o / rt_co.o）也提供。自举发出强定义时，
  gen2 链接运行时对象会 LNK2005 重复定义。
- 边界：`zan_exe_dir` / `zan_dir_list` 是自举独有的，运行时没有，保持强定义。

## 5. 闭环的链接集

gen2 链接的不是"最小运行时"，而是**普通程序用的那一套**：file / io / sched /
timer / decimal / sync + embed 运行时对象，外加平台系统库（Windows：
kernel32、shell32、user32、advapi32、version）。

- 理由：自举编译器自己的源码要用 FileStream、Environment、嵌入资源，缺一个
  就是一串 LNK2019；自举拉的 stdlib 还会调 `CommandLineToArgvW` /
  `SHGetFileInfoW` / 版本 API，需要系统库。
- 边界：**不要**链 `rt_mem.o`（它依赖 `--wrap=malloc` 的链接器换名，只在
  `--fast-alloc` 程序里用）；自举自带 `zretain/zrelease/zlist/zsb/zstr` 实现，
  不依赖它。

## 6. 定位手段

- `llvm-as`（mozbuild LLVM 自带）能跑 IR 验证器，比 clang 快且能直接指出
  支配违规、phi 前驱不匹配；clang 对 `.ll` 输入会自动加
  `-disable-llvm-verifier`，非法 IR 会被静默送进后端然后以 0xC0000005 崩在
  ISel——看到这个崩溃，先用 `llvm-as` 验证，别怀疑后端。
  ⚠ 本机 CMake 找到的是 VS 自带 LLVM，**没有** `llvm-as`/`opt`（只有 clang、
  llvm-nm、llvm-objdump…），`clang -Xclang -verify` 与 `-flto` 都**不会**报出
  支配违规——要验证自举产出的 IR，得另写一个走 LLVM C API 的小工具
  （`LLVMParseIRInContext` + `LLVMVerifyModule`）。
- 链接器的 LNK2019/LNK1120 走 **stdout**，不是 stderr；只看 stderr 会误判成
  "没有具体错误"。

## 7. 固定点只证明"自洽"，不证明"与 C 宿主一致"

- 动作：差分门 `ctest -R selfhost_diff`（`tests/run_selfhost_diff.cmake`）把一批
  conformance 用例交给 gen1 编译、链接、运行，与 C 宿主的 golden 输出逐字比对。
  已知缺口列在 `tests/selfhost/diff_known_gaps.txt`（只报告、不失败），修好就删行；
  用例集在 `tests/selfhost/diff_cases.txt`。
- 理由：固定点门断言 `g2.ll == g3.ll`，而**两代都是自举后端产出的**——自举后端
  整体错了照样绿。2026-10-20 建差分门时一次挖出三个静默错误：`foreach` 只支持
  `List<T>`（数组/字符串集合被整段丢弃，循环体都不生成）、switch 空段落不落空、
  `x is null` 让自举编译器自己空指针崩。
- 边界：差分门链接自举产出的 `.ll` 时，运行时对象集要跟 zanc 一致：
  `zan_hw_base64_encode/decode` 之类符号在 `zanrt_timer.obj` 里（因为
  `src/runtime/rt_timer.c:1388` 直接 `#include "rt_hw_accel.c"`），只链 clang
  默认的 libc 会 LNK2019。**但不要链 `rt_co.c`**（见 §11）：它提供部分同名强符号，
  会把自举的弱协程运行时切一半，定时器就再也等不到。

## 8. 能力缺口一律 fail-closed

- 动作：自举后端遇到"判不了"的情况（需要运行期类标识、集合协议等），必须
  **报诊断并拒绝编译**，不能"猜一个答案"。
- 理由：自举 IR 后端此前把 `case T x` 这种模式段落当成 `default`（模式段落在
  `a == null`），把非 `List` 集合的 `foreach` 整段丢掉——两者都**编译通过、
  结果全错**。差分门能发现，但用户先撞上的是错答案。
- 边界：诊断要说清"缺什么能力"（例："a 'Dog' pattern over an 'object' value
  needs a runtime class identity the self-hosted compiler does not emit yet"），
  不要写成"不支持"。

## 9. 运行期类标识：自举 IR 后端还没有

- 现状：C 宿主用**分配点 site id**（obj-8）+ `__zan_site_tynames` 祖先名表做
  `x is T`；ngen 后端用 obj-8 高半部的 site id。自举 IR 后端两者都没有：对象头
  只有 `[-16]=rc`、`[-8]=dtor`，字符串更是裸 malloc 的 C 串（无头无标记）。
  所以 `object o` 上的 `case Dog d` / `o is Dog` / `case string s` 判不了。
- 方向：给对象头加第三个槽（obj-24 放类身份），类实例化时写入该类的 tid 全局
  地址（`@__tid_X` 的值指向基类 tid——`irgen.zan` 的 `EmitTid` 已经是这条链，
  catch 匹配就在用它），再加一个 `__zan_is_class(obj, tid)` 沿链走。字符串必须
  同样带上头，否则读 obj-24 是越界读，`object` 槽里的字符串会误判成类实例。
- ⚠ 改对象头会影响自举固定点的 gen2 运行时包（`zanrt_sh_*`），改完必须重跑
  `selfhost_fixed_point`。

## 10. 跨挂起的值必须活在帧里，resume 块必须自己重算

- 动作：异步方法里**跨挂起点存活**的值（循环计数器、被遍历集合、元素变量、
  表达式临时值）必须放进协程帧（堆内存），不能放栈 alloca；resume 块里用到的
  帧槽地址必须**在该块内重新计算**（`getelementptr %ff, <idx>`），不能复用
  在别的块里算好的指针。
- 理由（2026-10-20 差分门实测）：栈 alloca 每次 resume 都会重建 → 循环从 0 重来
  或读到未初始化槽位；而"在 co.start 块里算好、resume 块里用"的指针**违反 LLVM
  支配关系**——`clang` 对 `.ll` 输入默认关掉 verifier（见 §6），非法 IR 被静默
  送进后端，表现为 0xC0000005 或读出随机地址，排查成本极高。
- 连带：resume 块是**从分派 switch 直接进入**的，不经过 co.start，所以凡是
  "块外算好、块内用"的值都要在块内重算；集合这种表达式值也要 spill 进帧
  （自举的帧槽：每个 foreach 占 3 槽——集合/计数器/元素变量）。
- 边界：这条只约束异步方法；同步路径的普通 alloca 仍然安全。

## 11. 弱协程回退必须自洽，不能与运行时的强符号混用

- 动作：自举发出的弱协程运行时（`zan_co_ready` / `zan_co_sched_run` /
  `zan_co_delay` / `zan_co_sched_init`）必须**整套一起生效**；链接集里不要再放
  提供部分同名强符号的运行时对象（如 `rt_co.c`）。自举的协程帧布局是它自己的
  （不是 C 宿主的 `zan.co.header`，见 TASKS A385），所以驱动它的必须是自举自己
  的调度器。
- 理由（2026-10-20 实测）：`rt_co.c` 的强 `zan_co_ready`/`zan_co_sched_run` 会
  盖掉自举的弱实现，而 `rt_co.c` 的调度器**不处理定时器**——`Task.Delay` 的
  计时器被挂进自举自己的定时器表，永远没人 drain，表现为"await 之后没有输出"。
  混搭出来的两半运行时比"没有运行时"更难查。
- 连带：自举的协程回退必须**无条件发出**（不是"程序用到 await 才发"）——C 运行时
  对象（`rt_io.o` 的 `zan_io_recv_co`）本身就引用 `zan_co_ready`，不发就是
  LNK2019。
- 边界：弱定义仍会被发行版运行时的强定义盖掉（那是有意的），但自举产出的程序
  目前**不能**与发行版 mt 运行时混用（帧布局不同，见 TASKS A385）。

## 12. 闭包值必须带标签：生产侧与消费侧必须同一套约定

- 动作：闭包值 = **闭包记录地址 | 1**（bit0 = 1），与 C 宿主同约定
  （`src/compiler/irgen_arc.c:51` 的 `emit_closure_is_tagged`）。
  自举里**建**闭包的地方（`GenLambda` 的堆记录、方法组的 `{fn,null}` 全局、
  extern 返回裸函数指针后的再包装）一律打标签；**用**闭包的地方
  （`EmitDelegateCall` 取 `{fn,env}`、extern 传参取代码指针）一律先
  `and i64 %iv, -2` 脱标签再取字段。
- 理由（2026-10-20 差分门实测）：自举原来**生产侧不打标签、消费侧按标签判别**
  ——闭包记录地址被当成裸函数指针直接 `call`，于是 pc 落在数据地址上，
  表现为 0xC0000005（ASAN: "UNKNOWN memory access"，pc=数据地址）。
  判别位是必需的：`(Delegate)nint`（`stdlib/System/Interop.zan` 的
  `(GetEnvironmentVariableWFn)fn`）产生的是**没有标签的裸原生函数指针**，
  调用侧要靠标签区分"堆闭包"与"裸函数指针"，后者按声明形参直接调、不带 env。
- 连带：extern 形参是 delegate 时要的是**代码地址**，必须先脱标签再取
  `rec[0]`，否则会把 `rec|1` 当指针解引用。
- 边界：C 宿主的记录布局是 `{fn, dtor, target, cap0..capN, [this]}`
  （`irgen_expr.c:12150`），自举现在是 `{fn, env}`——同一程序内自洽即可，
  但两边**不是二进制兼容**的；真要对齐见 §13。

## 13. 捕获语义：被写过的局部变量必须装箱（按引用捕获）

- 动作：局部变量**被本函数内的闭包捕获**且**在本函数体内被写过**时，
  必须装箱成堆上的 cell，闭包记录里存 **cell 指针**，读写都穿过 cell
  （C# 语义：闭包看到捕获之后的写入）。没被写过的可以按值快照
  （语义等价，更省）。循环变量额外按**每次迭代独立快照**（per-iteration），
  所以 `for` 里捕获仍是 0,1,2 而不是 0,3,3。
- 理由（2026-10-20 差分门实测，新用例 `tests/conformance/closure_capture_by_ref.zan`）：
  自举原来在建闭包时把捕获变量的**当前值**快照进 env 数组，全程没有 cell →
  捕获之后再写局部变量，闭包看不见（C 宿主输出 2，自举输出 1）。
  C 宿主的判定在 `irgen_expr.c:11812`（`local_is_lambda_captured` +
  `body_writes_ident_memo`），per-iteration 标记在 `irgen_stmt.c:1826`。
- 连带：自举的 env 槽现在是**每个捕获一个 i64**，宽于 8 字节的捕获值
  （struct）会被截断——重做捕获模型时要一并改成按类型分槽。
- 边界：这条动的是**所有**局部变量的读写路径（`GenIdent` 的 load、
  `StoreTo` 的 store），风险面大；改完必须重跑 `selfhost_fixed_point`
  与差分门，并把 `closure_capture_by_ref` 从 `diff_known_gaps.txt` 删掉。
  进展见 TASKS A386。
