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

## 13. 捕获语义：被闭包捕获的局部变量必须装箱（按引用捕获）

- 动作：局部变量**被本函数内的闭包捕获**时，必须装箱成堆上的 cell，闭包记录里
  存 **cell 指针**，读写都穿过 cell（C# 语义：闭包看到捕获之后的写入，闭包里的
  写外面也看得见）。自举的实现形态：
  - 函数体生成前先跑一遍**预扫描** `CollectCapturedNames`：把本函数里所有
    lambda 体内出现过的标识符名字收进 `capNames`（**故意过近似**：名字撞上的
    局部变量也会装箱，多一次分配而已，语义不变）；
  - 声明局部变量时，名字在 `capNames` 里就装箱（`irgen.zan` 的 `BoxLocal`）：
    局部变量的寄存器换成 **cell 里 payload 的指针**，于是 `GenIdent` 的 load 和
    `StoreTo` 的 store 一行都不用改，读写自动穿 cell；
  - 建闭包时，捕获项存 cell 指针；闭包体入口由 cell 指针算出 payload 指针当
    自己的局部变量 → 闭包内外看到的是同一份数据；
  - `for`/`foreach` 的变量是**每次迭代独立快照**（`locIter`）：建闭包时把当前值
    复制进**新** cell，所以 `for` 里捕获仍是 0,1,2 而不是 0,3,3。
- 理由（2026-10-20 差分门实测，用例 `tests/conformance/closure_capture_by_ref.zan`）：
  自举原来在建闭包时把捕获变量的**当前值**快照进 env 数组，全程没有 cell →
  捕获之后再写局部变量，闭包看不见（C 宿主输出 2，自举输出 1）。
  C 宿主是同一套模型：`local_is_lambda_captured` + `body_writes_ident_memo`
  决定装箱（`irgen_expr.c:11812`），per-iteration 标记在 `irgen_stmt.c:1826`，
  取值时复制新 cell 在 `irgen_expr.c:11509`。
- 连带：装箱是**按变量名**判定的，参数、方法体变量、lambda 形参都要走同一条路
  （`ShouldBox`）；`locBox`/`locIter`/`locCell` 三张表必须与
  `locName`/`locType`/`locReg` **同步增删**——`AddLocal`、`TrimLocals`、
  `StartFn` 和那两处直接 `locName.Add` 的地方都要一起改，漏一处就串位。
- 边界：**async 方法还没做**（async 的局部变量住在协程帧里，不是栈 alloca，
  装箱路径要按"帧槽存 cell 指针"重做，见 TASKS A387）。async 之外的同步路径
  已经和 C 宿主一致。

## 14. 发 `call` 时函数类型要整体带上返回值类型

- 动作：自举发间接调用时，函数类型统一由 `LamFnType` 生成、去掉结尾的 `*`
  （`void (i8*)*` → `void (i8*)`），返回值类型写在类型里，**不要**再单独拼
  `"call void "` 前缀，也不要按固定偏移切字符串。
- 理由（2026-10-20 实测）：调用返回 `void` 的委托（`Action`）时，代码走的是
  另一条手写分支，用 `fnt.Substring(6)` 切函数类型 → 切出 `i8*)*` 这种碎片，
  生成的 IR 是 `call void i8*) %f(...)`，clang 直接报 "expected value token"。
  同一段代码的非 void 分支是对的（先 `Substring(0, len-1)` 再 `call <type>`），
  两条分支各切一次就必然分叉。
- 边界：clang 报 `.ll` 语法错时，先看那一行是**谁**拼的——多半是某个分支自己
  手拼了类型片段，而不是 LLVM 类型规则本身有问题。

## 15. 容器内建方法：要么真降级，要么 fail-closed

- 动作：自举 IR 后端的容器内建方法（`List<T>` / `Dict<K,V>` / 字符串方法）
  **必须要么真的降级、要么报诊断拒绝编译**，不能"落到兜底分支返回 0 或返回
  接收者"。新增内建时，元素比较要走**带类型的相等**：`operator==`（类/结构体
  自定义）→ 结构体逐字段比较 → 字符串按内容 → 类按同一性（指针）→ 其余按槽值。
- 理由（2026-10-20 差分门实测）：`List<T>.AddRange / Insert / IndexOf / Contains`
  在自举的 IR 后端里**根本没有降级**——`EmitListMethod` 的兜底分支直接
  `return 0`，调用被静默丢弃：`copy.AddRange(more)` 什么都没做，列表仍是空的，
  紧接着的 `copy[0]` 越界读 → 0xC0000005（差分门 `list_wide_struct_elements`）。
  同一批里自举自己的 `attrgen.zan` 的 `OptNames.Contains(nm)` 一直在静默返回
  false（选项重名从来没被发现）——静默错答案比崩溃更难查。
- 连带：容器搬移元素时保持**每个槽位一份引用**。插入是"元素整体后移一格"，
  不是"丢弃被顶掉的那个"——所以插入路径**不能**再 release 被顶掉的槽位，
  否则同一个元素被释放两次 → 双重释放 → 堆损坏（0xC0000374，ASAN 反而查不出
  来的那一类）。
- 边界：差分门只能覆盖"用例集里有的"那部分内建；新增内建方法时，
  顺手把同族方法（`LastIndexOf` / `Sort` / `Remove` …）也补上或明确 fail-closed，
  别留"兜底返回 0"的洞。

## 16. 自举的 string 是「带长度头的托管串」——不是裸 C 字符串

- 动作：自举 IR 后端的 `string` 值指向一个托管块的 payload，块前面是
  `[i64 len][i64 magic]`，payload 仍以 NUL 结尾（与 C 运行时的 ABI 不变），
  长度取**精确字节数**而不是 `strlen`。新造字符串只有三条路：`zstr_new(len)`、
  `zstr_of(裸C串)`、字面量（常量区直接排好头）。数组 → string 的隐式转换
  必须在**每个 string 位置**做（声明 / 赋值 / 字段 / 实参 / return）。
- 理由（2026-10-20 差分门实测）：原来自举的 string 是裸 C 字符串，
  `Length` 走 `strlen`，于是 `string view = byteArray;` 的长度是**垃圾**
  （实测 1 / 6），`"AB\0CD"` 的长度被截成 2，嵌入 NUL 的内容整个丢——
  差分门 `string_view_length` 7 条断言全红。改带长度头后该用例 8 行逐字
  与 C 宿主一致。
- 连带：头必须写在 malloc 块**内**（写块前会越界 → 堆损坏）；`zstr_len`
  读到不认识的 magic 直接 `abort()`，让"漏掉的 string 来源"当场炸出来，
  而不是静默给一个错长度。
- 细节（布局、来源清单、LLVM 层面的坑）见
  [`selfhost-string.md`](selfhost-string.md)。

## 17. async 方法里的捕获：cell 指针存在 spill 槽里

- 动作：async 方法的**局部变量**若被闭包捕获，也要装箱成堆 cell；但 async 的
  resume 函数每次挂起恢复都是**一次全新的函数调用**（新的栈帧），所以栈上的
  alloca 槽活不过挂起点——装箱变量的槽必须是 `alloca i8*`（存 **cell 指针**），
  spill 槽（`asyncSlotReg` / `asyncSlotTy` / `asyncSlotIdx`）也按 cell 指针
  保存与恢复。变量的读与写都要绕一层 cell（`GenIdent` / `StoreTo` /
  `GenVarDecl` 的 async 分支）。
- 理由（2026-10-20 差分门实测）：async 方法的闭包原本拿到的是**值快照**
  （`f=1`，C 宿主 `f=2`）——async 路径从来没做过装箱，`GenAsyncMethod` 也没有
  收集闭包捕获的名字。改成装箱后，捕获后的写入、闭包内的写入、跨 `await`
  的写入都可见（用例 `closure_capture_async` 4 条断言与 C 宿主一致）。
- 连带：`CollectCapturedNames` 必须放在 **resume 函数的 `StartFn()` 之后**——
  `StartFn` 会把 `capNames` 清空，放在方法开头会被 ramp 的 `StartFn` 冲掉
  （症状：装箱没发生，变量槽还是 `alloca i64`）。
- 边界：**async 方法的参数与 foreach 元素变量按 C 宿主实测就是"按值"，别按 C# 猜**
  （2026-10-20 实测，探针 `_scratch/sd/p_capparam.zan`）：
  - **同步**方法：参数被闭包捕获是**按引用**（写参数，闭包可见，`s1=4`）；
  - **async** 方法：参数被闭包捕获是**捕获时刻的值快照**（`f=4`）——C 宿主本身
    就是按值，**不是** C# 的"参数只读 + 按引用捕获"；把 async 参数也装箱成 cell
    反而会与 C 宿主**相反**（`f=101`）。
  - async 的 **foreach 元素变量**被闭包捕获：C 宿主是**每次迭代独立**（`v=1,2,3`），
    自举已一致，**跨 `await` 也一致**（用例 `tests/conformance/async_foreach_capture.zan`）。
  - 推论：async 路径**不需要**为参数做 cell 装箱；`asyncCaps` 那套收集逻辑是多余的。

## 18. 装箱 cell 的释放：释放的是 cell 里的值，不是 cell 指针

- 动作：任何按 `locBox` 标记装箱的局部，其**释放路径**（`ReleaseLocalsFrom` 与
  async 的 `$cleanup`）必须先把 **cell 指针**读出来、再从 cell 里读出**值**、
  最后释放那个**值**；绝不能把 cell 指针本身当对象去 `zrelease`。
- 理由（2026-10-20 实测，用例 `tests/conformance/async_boxed_local_release.zan`）：
  装箱后局部变量的寄存器里存的是 **cell 指针**（8 字节的 malloc 块），
  而原来的释放代码直接 `load lt, lt* locReg[i]` 再 `zrelease` —— 把一个堆块
  当成对象去读引用计数（偏移 -16）并 free，**0xC0000005 访问违规**。
  C 宿主输出 `v=7 / done`，修复前的自举直接崩。
- 连带：判断"这个局部是不是装箱的"要用**装箱标记**（`locBox` / async 路径新增的
  `asyncBoxIdx`），不要用"名字是否出现在捕获集合里"——后者在 async 路径上会
  把没装箱的局部也当成装箱的。
- 边界：cell 本身（malloc 块）没有释放，是**泄漏**不是**破坏**；泄漏可接受，
  错释放不可接受。

## 19. 固定点门红灯：先排除"门在跑，源码被改"的竞态

- 动作：自举固定点门（`g2.ll == g3.ll`）报"不等"时，**先看被编译源文件的
  mtime** 是否落在门的运行窗口内；命中就当作假红灯，**重跑一次**再下结论。
- 理由（2026-10-20 实测）：门跑动的 2 分钟里我改了 `irgen.zan` / `irgen_async.zan`，
  于是 gen1 用的是**旧**源码、gen2 用的是**新**源码，两代产物当然不等——红字是
  竞态不是缺陷。差点把"gen1 丢了代码"当成根因，追查了两小时。
- 判据：门的时间戳是"生成 g2.ll 的时刻"，源码 mtime 落在**门启动 ~ g2.ll 落盘**
  这段窗口里，就可疑；重跑时**不要碰源码**。
- 边界：真正的固定点破坏通常表现为"两代产物大小差几千字节且内容稳定可复现"；
  一次性的、随跑随变的大小差，先怀疑环境（竞态、并发 tier 重编 `zanc`）。

## 20. async 方法的调用点：帧必须挂上调度器，否则方法体根本不执行

- 动作：async 方法的 ramp **只分配帧并返回**（方法体从 `$resume` 跑）。调用点
  在**语句位置**（fire-and-forget）必须把帧交给协作调度器：
  `zan_co_ready(frame, resume)`。resume 入口从**帧的槽 5** 读（ramp 写在那儿），
  不要按名字找 `<callee>$resume`（间接派发时根本找不到）。
- 理由（2026-10-20 实测，用例 `tests/conformance/async_void_call.zan`）：自举原来
  只 `call` 拿帧指针就丢掉 → 被调 async 方法**整个方法体不执行**
  （C 宿主输出 `work` / `done`，自举只输出 `done`）。C 宿主在调用点做同一件事
  （`irgen_call.c` 的 `emit_detach_async_call`）。
- 连带：只有**真正的 async 调用**才能 detach——用 `ExprVal.asyncCall` 标记区分，
  **别用「类型是 `__task`」**：降级路径（await 的操作数、还不认识的类型）也会带
  `__task` 类型但值是常量 `0`，detach 它会生成 `bitcast i8* 0 to i64*`，
  读地址 0 的槽 → clang 报 `integer constant must have integer type`。
- 边界：`await` 路径自己管调度（`EmitAwait` 里已经 `zan_co_ready`），不要再
  detach 一次；同步 `Main` 里调用 async 方法没有调度器在跑 → 方法体不执行
  （与 C 宿主一致，别去"修"成会执行）。

## 21. 闭包写在实例方法里就必须捕获接收者 `this`

- 动作：实例方法里的闭包若提到**本类的字段或实例方法**（或显式 `this`），就必须
  捕获接收者。判据照抄 C 宿主的 `checker.c` 的 `expr_captures_this`：**不是局部变量**
  且是本类的实例字段/实例方法 → 需要 `this`；名字被闭包自己声明的局部变量遮住时
  不需要。自举的实现形态（`irgen_expr.zan` 的 `CapturesThis` / `LambdaNeedsThis` /
  `ThisReceiver`）：
  - env 数组**多一个槽**，存接收者指针（`ptrtoint`/`inttoptr` 打包成 i64）；
  - 闭包体入口把该槽还原成 `%this`（`inttoptr`），并登记一个名为 `this` 的**局部**
    ——嵌套闭包才能继续捕获它（`ReceiverAvailable()` 判的就是它）；
  - 闭包体里 `curHasThis = true` / `curClass = 外层类`，字段与实例方法就走
    `LoadField("%this", …)` 这条既有路径。
- 理由（2026-10-20 实测，用例 `tests/conformance/closure_captures_this.zan`）：自举
  原来在闭包体里 `curHasThis = false`、`curClass = ""`，于是**实例字段一律读成 0**
  （C 宿主 `n=3 / len=3`，自举 `n=0 / len=0`）。C 宿主与 ngen 后端都捕获 `this`
  （`ngen_delegate.zan` 的 `DelegateCaptureNameNg("this", …)`），只有自举 IR 后端漏了。
- 连带：判"要不要捕获"必须在**外层作用域**做完（`CapturesThis` 要**递归进嵌套
  闭包**），否则外层不捕获、内层要用 `this` → 内层 env 里出现未定义的 `%this`
  → 非法 IR。
- 边界：接收者按**借用**语义处理（闭包体入口 retain、出口 release，净值为 0），
  与自举既有的"捕获局部变量靠外层帧持有引用"一致；闭包在方法返回后才被调用时，
  接收者可能已经没了（C 宿主靠闭包持有强引用保活，见 TASKS 的闭包生命周期条目）。

## 22. 委托调用的实参必须在分支之前求值

- 动作：`EmitDelegateCall` 里**先把实参求值完**（存成临时值），再按标签位分支走
  "堆闭包"还是"裸函数指针"两条路；两条路复用同一批临时值。
- 理由（2026-10-20 实测，用例 `tests/conformance/closure_nested_call.zan`）：实参
  在分支**臂里**求值时，一个"实参本身是委托调用"的表达式会把内层调用的
  **分支+phi 塞进外层分支的臂里**；这个 IR 形状（phi 的前驱块里还有 phi）会让
  **clang 的 X86 指令选择器崩溃**（0xC0000005，`Running pass 'X86 DAG->DAG
  Instruction Selection'`），LLVM 19 与 20 都复现，IR 本身**通过验证器**。
  把实参提到分支之前，形状变成"先合并、再分支"，就不再触发。
- 连带：顺带修掉了"实参被求值两次"（原来两条臂各生成一份，实参副作用会重复出现
  在 IR 里）。
- 排查法：clang 在 ISel 崩时，先把出问题的函数**单独抠出来**（`define` 到下一个
  `define` 之间）编译，再逐块删指令二分——IR 文本手工改比改自举快得多。

## 23. 闭包的 fn 槽是 `i8*`：调用前必须 bitcast 成函数指针

- 动作：闭包记录是 `{i8* fn, i8* env}`，取出的 `fn` 是 **`i8*`**；调用前必须
  `bitcast i8* %fn to <函数类型>*` 再 `call`，不能拿 `i8*` 直接当函数指针调。
- 理由（2026-10-20 实测）：`call i64 (i8*, i64) %fn(...)` 而 `%fn` 是 `i8*`，
  是**非法 IR**（被调值类型与函数指针类型不符）。clang 对 `.ll` 输入默认
  `-disable-llvm-verifier`（见 §6），非法 IR 被静默送进后端。
- 边界：同一份文件里**所有**间接调用都要检查——虚方法槽（`EmitInstanceCall` 的
  vtable 路径）已经是"先 bitcast 再调"，委托调用之前是漏的；两处要一起看。

## 24. `foreach` 的可枚举协议要按 C 宿主的口径降级

- 动作：`foreach` 除了 `List<T>` / `T[]` / `string`，还要支持**鸭子类型的可枚举
  协议**：`coll.GetEnumerator()` → 枚举器 → `enumerator.MoveNext()` → bool →
  `enumerator.Current` → 元素。`Current` 的形态要与 C 宿主 irgen 的口径一致：
  **方法** `Current()` 或**属性** `get_Current`（C 宿主的 `irgen_stmt.c` 只认这两
  种；`Current` 写成**字段**是 C 宿主自己的缺口——它的检查器收下了、irgen 不读，
  循环会静默不执行）。
- 理由（2026-10-20 差分门实测）：自举原来对非 List 集合一律报诊断拒绝编译，
  LINQ 的迭代器（`ValueWhereIterator` 等）整个用不了，差分门里
  `value_enumerable` / `linq_query` 长期挂着。
- 连带：自举的泛型是**完全擦除**的（只生成一个符号，没有特化体），所以
  `Foo<T>.GetEnumerator()` **不需要**泛型特化机制——直接走既有的实例调用路径
  （`EmitInstanceCall`）即可，别去补特化。
- 边界：协议不认得就**报诊断**（fail-closed），不要"安静地不迭代"；async 上下文
  暂时不支持（枚举器没有帧槽），要显式拒绝。

## 25. 泛型数组 `T[]` 的槽类型：数组先于擦除判定

- 动作：`MapType` 的判定顺序必须是**数组先于擦除**（`t.ival >= 1` → `i64*`），
  且 `IsErasedType` 对数组返回 false。擦除的是**元素类型**，不是数组本身。
- 理由（2026-10-20 实测）：`T[]` 的元素类型是类型参数 → 整个数组被降成 i64 槽，
  **存端**按 i64 打包、**读端**按 `i64*` 读，两端对不上 → 数组指针丢失
  （`buf.Length` 读成 0）。
- 边界：这条只管**类型映射**；`List<T>` 的槽位本来就是 i64（元素擦除、列表本身
  是 `i8*`），别把两套模型混起来。

## 26. `await` 的操作数必须是协程帧；「必须拒绝」的程序要有门

- 动作：自举的 await 降级读的是**协程帧的槽 5**（resume 入口），所以被 await
  的操作数**必须是协程帧**。用 `ExprVal.asyncCall` 标记判断（async 方法的调用
  才带这个标记），不是帧就报诊断，**不要**发 `bitcast i8* 0 to i64*`。
  另外：async 方法里 **lambda 内部的 await** 要拒绝（lambda 不是协程，在协程里
  跑同步 await 会重入调度器）。
- 理由（2026-10-20 实测，探针 `_scratch/sd/p_await_sync.zan`）：
  `await Task.Delay(80)` 在同步方法里 → `Task.Delay` 是 C 宿主的**内建**
  （`builtin_api.c`），自举不认识 → 调用**静默降级成常量 0** → await 读 0 号地址
  → clang 报 `integer constant must have integer type`（非法 IR）。
- ⚠ **别把规则写成「同步方法里的 await 一律拒绝」**：同步方法里 await 一个
  **async 方法**是合法的（C 宿主也接受）：自举的 `tests/selfhost/prog1.zan` 靠
  `int av = await P.AsTwice(21);` 跑出 42——同步上下文会把被 await 的协程跑完
  再取结果。判据是**操作数是不是帧**，不是**方法是不是 async**。
- **await 落在哪儿也要管**：async 方法里、await 写在 **lambda 体内** → 拒绝
  （lambda 不是协程，没有自己的帧可挂；硬降级会把它挂到**外层协程**的块上，
  发出跨函数的 br，LLVM 校验直接失败：`Referring to a basic block in another
  function`）。两个后端**同一句措辞**：`'await' inside a lambda is not
  supported: a lambda is not a coroutine`。边界：**同步方法**里的同样写法合法
  （外面没有协程，根驱动等得到）——判据是"外层有没有协程帧"，不是"外面是不是
  lambda"。
- 坑（2026-10-21 修 A471 时改掉）：这条诊断**曾经做不进检查器**——检查器
  **不遍历 lambda 的函数体**（`AST_LAMBDA` 分支把体丢给 `check_expr`，而体是
  语句块，`check_expr` 没有语句分支 → 体根本没被走）。现在检查器**下钻 lambda
  体**了（见下一条），但这条 await 规则**仍留在 irgen**：判据是"外层有没有
  协程帧"，这个上下文只有发码时才知道（发射 lambda 时按 `saved_async_frame !=
  NULL` 判；检查器那边根本没有"当前方法是不是 async"的概念——`async Task<T>` 的
  返回类型是解析器改写掉的）。自举侧相反：它的 AST 有通用子节点表，**检查器**里
  就能查（`FindAwaitInLambda`）。
- 做法（A471，2026-10-21）：`checker.c` 的 `AST_LAMBDA` 分支按**体的形态**分派
  （`AST_BLOCK` → `zan_checker_check_stmt`，否则 → `zan_checker_check_expr`），
  并且三件事必须配套：
  - **作用域是嵌套的**：`c->locals` 链头存起来，lambda 的**形参**加在链头
    （闭包要捕获外层的局部与形参，它们必须**可见**），出 lambda 还原链头；把
    `c->locals` 置空会误报"未定义"。
  - **`c->current_return_type = NULL`**：lambda 的返回类型在检查器这里**不知道**
    （委托类型是从使用现场推出来的），拿外层方法的返回类型去判 lambda 的
    `return` 会误报（自举侧同口径：`curRet = null`，见 §36）。
  - **`c->current_body` 换成 lambda 的体**：可空接收者规则的空值守卫分析
    （`node_guards_null`）要在 **lambda 自己的体**里找守卫，不该翻外层方法体。
- ⚠ **别把自举当成同款受害者**：自举的检查器**本来就下钻 lambda 体**
  （`src/selfhost/checker.zan` 的 `AK.Lambda` 分支在 **`CheckExpr`** 里——不是
  `CheckStmt`：`ScopeMark` → 形参入作用域 → `curRet = null` → `CheckExpr(n.a)`
  （表达式体）+ `CheckStmt(n.d)`（块体）→ 还原）。实测自举会报 lambda 体里的
  "undefined identifier"。A471 的洞**只在宿主**这边。
  顺带：自举的检查器是**部分实现**——它没有宿主那种通用隐式转换检查
  （`int y = "hello";` 在自举的**方法体**里也不报），所以"lambda 里的类型错误
  静默通过"这个症状在自举侧压根不成立。
- 坑（连带，2026-10-21 实测）：检查器不下钻 lambda 体，**不止 await 这一条规则**——
  lambda 体里的**类型错误也全部漏检**：`Func<int> f = () => { int x = "hello";
  return x; };` 编译通过、运行时 `f()` 返回 `-1065074624`（把字符串指针当 int
  读出来），而同一段代码放进方法体正常报 `cannot convert 'string' to 'int'`。
  根因同上（`AST_LAMBDA` 把语句块丢给没有语句分支的 `check_expr`）。A471 修完
  后这类漏检归零，但**代价是全仓所有 lambda 体一次性进入类型检查**——被新规则
  抓出来的多是**真 null-deref**（`WxNet.DataOf(x).Str(...)`：应答里没有 `data`
  节点时 `DataOf` 返回 null），修法是"先存局部量再判空"，不是放宽规则。
- 门：差分门除了"能编且输出逐字一致"的用例，还多了一份 **reject 列表**
  （`tests/selfhost/reject_cases.txt` + `tests/selfhost/reject/<name>.zan`）：
  这些程序自举**必须拒绝**——非零退出**且不落 IR**。fail-closed 的边界要有门
  守着，否则下一次重构就会把它悄悄放回去。

## 27. 自举的调用路径必须 fail-closed：认不出的调用不能静默返回 0

- 动作：自举的调用路径里，凡是"认不出的调用"都要**报诊断**，不能返回常量 0：
  - 接收者的类型不认识（不是类 / List / Dictionary / StringBuilder）→ 诊断；
  - 接收者的名字**根本解析不出来**（不是变量、不是类型、不是命名空间）→ 诊断，
    措辞照 C 宿主：`unresolved call 'X.Y': 'X' is not a known variable, type, or
    namespace`；
  - 字符串 / 字典 / StringBuilder 的**未知方法** → 诊断（跟 `List` 早先的做法
    一致：`List method 'X' is not supported by the self-hosted compiler yet`）；
  - 被调用者既不是成员访问也不是名字（`filters[f](item)` 这种）→ 只有**委托类型
    的表达式**能降级（`EmitDelegateCallVal`），其余报诊断。
- 理由（2026-10-20 差分门实测）：自举不认识 C 宿主的内建（`Task.Delay`、
  `Math.Max`…），调用**静默降级成常量 0**——程序拿到了错的值却没有一个字提示。
  更糟的是**标准库自己就在用**这些调用（`String.CompareOrdinal`、
  `string.ToBytes()`、`byte[].ToStr()`），它们在自举里全是错的，只是没人跑到。
- 连带：fail-closed 之前必须把标准库用到的**编译器内建**补齐——它们是 C 宿主
  后端里的内建，**不是标准库方法**，标准库只是调用方：
  - `String.CompareOrdinal(a,b)` → `strcmp`（C 宿主 `irgen_call.c`）；
  - `string.ToBytes()` → `byte[]`（`irgen_expr.c` 的 `emit_bytes_call`）；
  - `byte[].ToStr(off,len)` → `string`（同一个原语；窗口要按 C 宿主的口径
    **钳制**：off 夹到 `[0, len]`，长度夹到 `[0, len-off]`）。
  自举的数组是**每元素一个 8 字节槽**，所以字节要逐个打包/拆开，不能整块 memcpy。
- 边界：诊断要发在**降级点**（irgen），不要放在检查器里猜——只有降级点知道
  "这个调用自举到底会不会降级"。检查器对"接收者类型未知"一律放过（`cls == null`
  时不报），所以这类调用会一路走到 irgen。
- 门：这两条边界各有 reject 用例钉住
  （`tests/selfhost/reject/unsupported_builtin_call.zan`、
  `tests/selfhost/reject/unresolved_call.zan`）。

## 28. 标识符/成员/静态属性：兜底要报诊断，静态属性要降级成静态 getter 调用

- 动作：
  - `GenIdent` / `GenMember` 的兜底（"解析不出来"）也改成**报诊断**——这是
    "检查器与后端不一致"时的背靠背安全网。常见情形**检查器早就拦住了**
    （`undefined identifier 'X'` / `type 'X' has no member 'Y'`，与 C 宿主同款
    拒绝），所以这条兜底平时不触发；固定点门（自举编自己的 62 个文件）与差分门
    （27 个用例 + 标准库）是它的证据。
  - **静态属性**（`Cls.Prop`，`public static T Prop { get { ... } }`）要降级成
    **静态 getter 调用**：解析器把属性拆成 `get_<name>` 方法，所以静态分支要
    多找一条 `FindMethodUp(tc, "get_" + name)` + `HasMod(Static)`。
- 理由（2026-10-20 差分门实测）：标准库 `stdlib/System/Text/Encoding.zan`
  里的 `Encoding.UTF8` 就是静态属性——自举以前**静默返回 0**（编码整个是错的，
  没人跑到），把兜底改成诊断之后才暴露出来。
- 边界：`FindMethodUp` / `FindStaticField` / `CollectFields` 都会走**基类链**，
  所以继承来的字段、静态字段、静态属性都能找到；别在自举里另写一套查找。

## 29. 内建表的能力要**量**出来；double 的文本格式要用运行时的最短往返

- 动作：
  - 宿主把一批 API 声明成**内建**（`src/compiler/builtin_api.c` 的
    `builtin_types[]` / `members_*[]`），自举后端要么自己降级、要么报诊断。
    "哪些能降级"必须**测量**：`scripts/selfhost_builtin_sweep.ps1` 按内建表
    自动生成探针（参数类型从签名取，只编译不运行），矩阵落在
    `docs/selfhost-builtins.md`。新增内建不用改脚本。
  - double→文本要用运行时的 `zan_rt_dbl_str`（**最短往返**格式，
    `src/runtime/rt_timer.c`），**不要**用 `printf("%g")`——那是六位有效数字，
    与宿主不一致。
- 理由（2026-10-20 实测）：`Convert.ToString(Math.Acos(0.0))` 宿主给
  `1.5707963267948966`，自举给 `1.5708`——凡是打印 double 的程序，输出都不同。
  而"自举到底缺哪些内建"在 A401 之前**没人知道**，只能撞。
- 边界：能力矩阵只测"能不能编译"；**行为**是否一致由差分门断言（同一程序
  两边 stdout 逐字相同）。新降级的内建都要配 conformance 用例并进
  `tests/selfhost/diff_cases.txt`。
- 连带：`SeedExternBuiltins`（`irgen.zan`）要跟模块头部的 `declare` 列表
  **同步**——头里声明了 `memmove`/`memset`/`memcmp` 而种子表里没有时，
  后面再声明一次就是 "invalid redefinition of function"。




## 30. 内建家族要**逐个成员**降级；"能编译"不等于"行为对"

- 动作：`Console` 家族的每个成员都要按 C 宿主 `irgen_call.c` 的口径**单独**降级，
  不要留一条"取第一个实参、把它打印出来"的 blanket catch：
  - `Write` / `WriteLine` / `PrintLine`：按**实参的静态类型**分流——string → `%s`；
    double → 运行时 `zan_rt_dbl_str` 的最短往返；bool → `true`/`false`；
    char → 它的 UTF-8 文本；ulong → `%llu`；其余 → `%lld`；
  - `Clear` → `ESC[2J ESC[H`；`ResetColor` → `ESC[0m`；
  - `ReadLine` → `fgets` + 把**精确长度**回写托管串的头（换行留在值里，宿主不 trim）；
  - `Read` → `getchar`；`ReadKey` → Windows `_getch`、其它平台 `getchar`
    （可选的 `intercept` 参数按宿主口径**忽略**）。
- 理由（2026-10-20 差分门实测）：自举原来对 `Console.*` 只有一条兜底分支，
  于是每个成员都被当成"打印第一个实参"：`Clear()` 打印 `0`、`ReadLine()` 打印
  `0` 并返回垃圾（取 `.Length` 直接访问冲突崩，exit -1073741819）、
  `WriteLine(true)` 打印 `1`、`WriteLine('x')` 打印 `120`——**一个字诊断都没有**。
- 连带：能力矩阵里的"ok"只代表**能编译**。Console 家族在矩阵里全是 ok，行为却
  全错——矩阵是**入口**，差分门才是判据。
- 边界：`ReadLine` / `Read` / `ReadKey` 要 stdin，**进不了 golden**（差分门不喂
  stdin）——只能用探针断言，别塞进 conformance 用例。

## 31. C 宿主自己也会不自洽：差分门撞到不一致时，先问"谁对"

- 动作：差分门报出不一致时，**先分清是自举错还是宿主错**——用 C 宿主自己跑
  探针，看它**自不自洽**：
  - `char` 字面量：宿主 `lexer.c` 的 `lexer_escape_char` 取 `zan_utf8_encode`
    结果的**第一个字节**（`'\u4e2d'` 存成 0xE4，不是码点 0x4E2D），而宿主的
    codegen `emit_char_to_cstr` 按**码点**做完整 UTF-8 编码 → 词法与代码生成
    两半不自洽；`docs/ABI.md` §3.1 明确 char 是 Unicode scalar。→ 修**宿主**
    （新增 `lexer_escape_char_cp` 返回码点），自举本来就对。
  - bool 的渲染：宿主渲染 bool 靠 `expr_is_bool`（**静态类型**），而 irgen 的
    `infer_expr_type` 对**内建类型的实例调用**（`list.Contains(x)`）和**原始
    类型名上的静态内建调用**（`string.IsNullOrEmpty(x)`）推不出返回值 → 同一个
    程序里 `Console.WriteLine(true)` 打印 "true"、
    `Console.WriteLine(list.Contains(x))` 打印 "1"。→ 修**宿主**的推断：
    内建类型没有 binder symbol，符号表答不了"这个成员返回什么"，**内建表
    `builtin_api.c` 才是权威**（`string` 的静态成员在静态类 `String` 下，
    别名要一并映射）。
- 理由：差分门的标准是"与 C 宿主一致"，但宿主自己两半不自洽时，**一致地错
  仍然是错**。先修自相矛盾的那一半，再让两边对上。
- 连带：改了宿主的**类型推断**后 golden 会变（bool 从 "1" 变 "true"）——这些
  golden 要**重生成并逐行看**，确认变的是"该变的那几行"。
- 边界：宿主改的是**核心推断函数**时，影响面比看上去大——至少把整个
  conformance 集跑一遍，别只看自己碰过的那两个用例。
## 32. 内建的**属性**也要降级：读＝读模块级槽，写＝发转义序列

- 动作：`Console.ForegroundColor` / `Console.BackgroundColor` 这类内建**属性**
  不是"调用"，是**读/写一个模块级槽**（C 宿主 `irgen_expr.c` 的
  `console_color_slot` / `emit_console_color`）：
  - 读＝load 槽（缺省前景 7 / 背景 0）；
  - 写＝存 `color & 15`，并发 `ESC[<code>m`；码表是
    `(idx<8?30:90) + 4*b0 + 2*b1 + b2`（b0..b2 是 idx 的低三位），
    背景再 +10。
  - `Console.Title` **只可写**：`ESC]0;<title> BEL`；宿主没有 getter——
    读它在宿主那边就是编译错误（`cannot resolve member 'Title'`），
    自举也别给它编一个 getter。
- 理由（2026-10-20 差分门实测）：属性在内建表里是 `'P'`，能力矩阵把它归到
  "内建属性"一栏（自举不降级）。矩阵**看不出**属性该是"读槽"还是"发转义"——
  得先用 C 宿主跑探针，把字节（`ESC[36m`、`ESC]0;…BEL`）量出来再写。
- 边界：属性赋值走的是 `StoreTo`（不是 `GenAssign`），读取走的是 `GenMember`
  ——两处都要挂，别只改一处（只改一处会变成"写进去了、读不出来"）。
## 33. 任务句柄：先镜像**形状**（类型模型），再谈能不能降级

- 动作：C 宿主把 `Task` / `Task<T>` 当**内建类型**（`binder.c` 的
  `zan_binder_make_task_type`，`TYPE_TASK`），不是标准库里的类。自举的检查器
  必须照抄这个类型模型，否则任何 Task 形状的声明都被报成
  `unknown type 'Task'`——一个**关于语言的假陈述**（语言里明明有 Task）。
- 形状由**实参形态**决定，不是由被调者名字决定（C 宿主
  `checker.c` 的 `task_run_result_type`）：
  - `Task.Spawn(...)` → `Task`（脱钩帧，无结果）
  - `Task.Run(lambda/Action)` → `Task`（action 帧，无结果）
  - `Task.Run(<无结果调用>)` → `Task`
  - `Task.Run(<带类型的调用>)` → `Task<T>`（keep_result 帧）
- 无结果句柄流进 `Task<T>` 的槽位要在**编译期**被拒，诊断与宿主**逐字一致**
  （`cannot convert 'Task' to 'Task<int>' in initializer: a task started from a
  lambda/Action carries no result; declare 'Task' (not 'Task<T>') and wait with
  .Wait()`）。理由是运行期 double free：action 帧由完成路径释放一次，读者再
  释放一次 → abort。
- 理由（2026-10-20 对账）：宿主与自举在"任务句柄定型"上分叉，是**并行的两
  条编译线**之间的语义漂移；差分门一旦收录相关用例就会红。
- 坑：宿主在**解析期**就把 async 方法的返回类型改写掉
  （`parser.c` 的 `desugar_async_task_method`：`async Task<int>` → `int`、
  `async Task` → `void`，无方法体的接口声明还补上 `async` 标记），所以
  检查器看到的永远是**结果类型**，`ValueTask` 这个名字也从来不需要进 binder。
  自举的 parser 没有这一步 → 检查器若只"认识 Task"而不按元素类型判
  "not all code paths return a value"，就会在 stdlib 的 `async Task
  SleepAsync` 上报**假阳性**。自举侧先在检查器里补一半（async + Task 形状
  → 用元素类型当方法体的返回类型），parser 侧的改写要连带 ngen 后端
  （它靠**类型名**认 Task 形状）一起动，不能盲改。
- 边界：**认识形状 ≠ 能降级**。自举还没有 `Task.Run` / `t.Wait()` /
  `t.Result` / `t.IsCompleted` 的实现——它们一律报"not supported by the
  self-hosted compiler yet"（fail-closed），差分门里对应用例记在
  `tests/selfhost/diff_known_gaps.txt`（**已知缺口**），不要为了"全绿"把它
  悄悄放过，也不要假装通过。
- 补充（同一批实测）：`async Task<T>` 这个**返回类型**的形状也要拆包——await
  出来的值是**元素类型**（T），不是句柄。宿主在解析期改写（parser 的
  `desugar_async_task_method`），自举拆晚了：检查器那一半漏了时，double 结果
  落到 double 槽里成了指针（clang 拒 IR：`'%t8' defined with type 'ptr' but
  expected 'double'`），而整数/字符串靠 inttoptr/ptrtoint 的**位往返碰巧**对
  ——这类"碰巧对"最危险：差分门不收这个形状就没人知道。教训：**类型形状要按
  元素拆，不按名字认**。

## 34. 照抄宿主的调用前，先确认这个符号在自举产物里解析到**哪一份实现**

- 坑（2026-10-20 实测）：宿主在程序入口调 `zan_co_sched_init`
  （`src/compiler/irgen_emit.c:132`），自举的程序入口没有这一句。照抄过来就
  以为"对齐了"——但自举产物里的 `zan_co_sched_init` 是**自举自己发的弱符号**
  （`irgen_async.zan` 的 `EmitAsyncPumpReady`），而自举运行时对象
  （`build/zanrt_shd_rt_io.obj` 等）**根本没有 `zan_co_*` 强符号**
  （`llvm-nm --defined-only` 查过），于是这句调用今天只重置自举自己的调度器
  状态：**结构对齐，行为没变**。等 A385（帧布局对齐 `zan_co_fhdr`）后链接
  完整运行时，强符号胜出，这句才真的装上 timer ready hook
  （`src/runtime/rt_io.c:4242`），把延迟帧重新入队而不是在定时器线程上直接
  恢复协程（`src/runtime/rt_timer.c:729`）。
- 做法：往自举产物里加一个**外部符号**之前，先问三件事——
  1. 自举侧自己发的是什么（弱回退？强定义？）；
  2. 链接进去的运行时对象里有没有同名**强符号**（`llvm-nm --defined-only
     <obj> | Select-String <name>`）；
  3. 弱/强谁胜出，胜出后行为是不是你要的那一份。
  没查清就写"修好了"，等于把**空转**记成修复。
- 推论：差分门通过 ≠ 这条路径真的被走过。判"修好"要看**行为差分**，不是看
  门绿；同一个探针在有/无这行时输出**逐字相同**，就说明这行当前是空转。

## 35. 降级到**自己**的布局：行为对齐即可，字节不必对齐

- 动作：自举实现任务句柄族（`Task.Run` / `Task.Spawn` / `t.Wait()` /
  `t.Result` / `t.IsCompleted` / `Task.IsDone`）时，**没有**去对齐宿主的
  `zan_co_fhdr` 帧头（A385），而是降级到自举**自己**的扁平 i64 帧上：句柄
  = 帧指针，done 标志 = 槽 1，结果 = 槽 4。宿主是同一套协议、另一套字节
  （`irgen_expr_core.c` 的 `emit_task_member`：test/pump 循环 → 读结果 →
  回收）。差分门比的是 **stdout 逐字**，不是帧布局——所以"布局没对齐"从来不
  是"降级做不了"的理由，只有**行为**对齐才是。
- 坑一：**完成即释放 = 句柄读的是已释放内存**。句柄在任务完成后还要能读
  done 标志（`t.IsCompleted`），所以 action 帧不能在完成路径里释放。宿主靠
  运行时的**活跃帧登记表**（`zan_co_live_add/has`）回收，自举没有 → 只能
  不释放（每个 action 帧泄漏 72 字节；不是 ARC 对象，泄漏检查看不见）。
  要根治就得在自举侧也做一份活跃帧登记表。
- 坑二：**自举没有工作线程池**。宿主的 `Task.Run` 把任务丢给工作线程，主
  线程只等；自举的调度器是**单线程协作式**的，任务只在主线程泵调度器时
  前进。于是 `while (Task.IsDone(h) == 0) {}` 这种忙等在自举侧永远推不动：
  `IsDone` 读到"还没完"就必须**给调度器一次机会**（泵一次再读），否则是死
  循环。这是自举与宿主唯一的语义偏差，遇到"宿主能跑、自举卡住"先查这条。
- 坑三：**属性形态与调用形态是两条路**。`t.Wait()` 是调用，`t.Result` /
  `t.IsCompleted` 是**不带括号的成员访问**——只挂调用路径会漏掉一半，症状是
  "member 'Result' of type 'Task' is not supported"。挂的时候两处都要挂
  （`GenCall` 与 `GenMember`）。
- 顺带：显式函数类型里**不能带形参名**。`call void (i8* %env) %fp(...)` 是
  非法 IR（clang: "argument name invalid in function type"），要写
  `call void (i8*) %fp(i8* %env)`——类型里只留类型，名字只留在实参上。

## 36. lambda 返回的是**自己**的值：返回类型未知时不要查 `return`

- 坑（2026-10-20 实测）：自举的检查器把 **lambda 的函数体**按**外层方法**的
  返回类型查 `return`——于是在 void 方法里写
  `Func<int> f = delegate () { return 5; };` 被报成
  "cannot return a value from a void method"，而 C 宿主**编译并运行**这个程序
  （输出 5）。这是**假阳性**：lambda 返回的是它自己的值，跟外层方法无关。
- 做法：lambda 的返回类型在自举侧**不知道**（检查器不从上下文给 lambda 定
  型），所以函数体里的 `return` **不查**（既不查"void 方法不能返回值"，也不
  查"必须返回值"）。实现上给检查器加一个 `curRetKnown` 标志：方法体为
  true，lambda 体为 false。
- 宿主侧同口径的等价做法（A471，2026-10-21）：`checker.c` 下钻 lambda 体时把
  `c->current_return_type` 置空（不是"设成 void"，是**置空**——置成 void 会把
  "void 方法不能返回值"那条误报又请回来）。
- 为什么值得单独记：这条假阳性会**挡掉**别的诊断对齐——`Task.Run(delegate ()
  { return 5; })` 在宿主侧报的是 Task.Run 专属的那条（"Task.Run delegate
  must be a parameterless void body (an Action); pass an async call for
  results"），自举侧却先被这条泛化诊断拦下，措辞对不上。**上游的检查器
  假阳性会掩盖下游的诊断对齐**，差分门看到的"措辞不同"往往根子在更早的
  一层。
- 教训：**"措辞不同"先查更早的层**。同一个程序，宿主报专属诊断、自举报泛化
  诊断时，先确认自举是不是在更早的检查里就拒了它。

## 37. 诊断对齐要连**位置**和**条数**一起对齐；类型解析照抄宿主的三步

- 坑（2026-10-20 实测）：自举的类型诊断只对齐了**一部分**——未定义类型报
  "unknown type 'X'"（宿主是 "undefined type 'X'"），而且**位置停在 0:0**：
  类型节点根本没记自己的行列，诊断指不到类型名头上。泛型实参个数更是各说
  各话：宿主是 "using the generic type 'List' requires 1 type arguments
  (got 2)"，自举是自造的 "Task takes at most one type argument"。
- 做法一：**类型节点要带自己的位置**。解析类型时把类型名 token 的行列记进
  类型节点（基类名另记一份），诊断才有地方指。合成类型节点（`new` 表达式
  的类型、`out T x` 的类型）也要补位置，否则报出来还是 0:0。
- 做法二：**照抄宿主 binder 的三步**：找基类型 → 查类型实参个数 → 逐个解析
  实参。基类型解析不出来就**到此为止**（不再去看实参）——这一步决定了错误
  的条数。
- 做法三：**实参个数有三档口径**，写错档就会误报：
  - 内建泛型：List/Span/ReadOnlySpan/Task 收 1 个，Dict/Dictionary 收 2 个；
  - 用户泛型类/结构/接口：按**声明的类型参数个数**，**可以是 0**（此时措辞
    是 "requires 0 type arguments"，不是 "the non-generic type"）；
  - 枚举与**具名委托**："the non-generic type 'X' cannot be used with type
    arguments"。
- 坑：**Func/Action 不查个数**。宿主在作用域查找**之前**就把 Func/Action 当
  内建委托类型，标准库那句 `delegate TResult Func<TResult>();` 的声明被
  遮住。自举照抄时踩过：标准库的 Func 委托带着 1 个类型参数，一度把
  `Func<int,int>` 判成 "requires 1 type arguments (got 2)"，差分门里两个
  闭包用例当场炸掉。**先问宿主"这个类型名走的是内建分支还是作用域分支"。**
- 坑：**签名类型在宿主被解析两遍**（binder 一遍，checker 在方法自己的作用域
  里再来一遍），所以一个坏的参数类型会报**两条**诊断；自举照抄这个行为。
  实现上把"解析类型"拆成"只收集诊断"（`TypeDiags`）+ 调用方决定报几遍，
  而不是让解析函数直接报错。
- 做法四：**门禁能钉措辞**。差分门的 reject 用例加第三段
  `name|||自举子串|||宿主子串`：两侧必须用**同一句**诊断拒绝同一个程序，
  光"都拒绝"不算对齐。
- 探针法：先把同一批程序在**宿主**上跑一遍，拿到"措辞 + 行列 + 条数"三样
  基准真值，再照着改自举——只盯着措辞改会漏掉位置和条数，**这三条一起
  才叫逐字一致**。

## 38. async 形状在**解析期**就定形：ValueTask 与"没方法体的 Task 返回类型"

- 坑（2026-10-20 实测）：自举的 parser 不做宿主那步定形，于是两个形状对不上：
  1. `async ValueTask<int>`：宿主在 `desugar_async_task_method` 里把它改写掉，
     `ValueTask` 这个名字**从来不需要进 binder**（宿主的 binder 只认 `Task`）；
     自举不认这个名字 → 报 "undefined type 'ValueTask'"。
  2. **接口上的 `Task<int> GetAsync();`**（没有方法体）：宿主也把它当 async
     方法（`body == NULL` 也算，补 async 标记），于是经接口调用拿到的是协程
     帧；自举没补 → 调用点不认为这是协程调用 → await 报 "this operand is not
     lowered as a coroutine frame"。
- 做法（自举的取舍：**不改写返回类型**，只做两件事）：
  - `ValueTask` → `Task`（仅限 async 或无方法体的方法；普通方法照旧报
    "undefined type 'ValueTask'"，与宿主逐字一致）；
  - 给"无方法体 + Task 形状返回类型"的方法补 `MOD.Async()` 标记。
  保留 `Task<T>` 这个声明类型（不像宿主那样改写成元素类型），因为自举的检查器
  与后端已经按"声明类型是 Task 形状"取元素类型（`TaskElemTy` / `AsyncElemTyOf`），
  改写反而要把**调用点**的类型也一起改（宿主为此专门有 `is_task_return` 标记）。
  **保留类型名还顺带保住了 ngen**：`IsTaskShapedRetNg` 靠类型名认形状，不必
  换成看标记。
- 坑：**虚调用（接口/虚方法）也可能是协程调用**。自举的 `EmitVirtualCall`
  原来只按普通调用发射，返回值不标 `asyncCall` → await 拒收。非虚的 async
  调用走 `EmitInstanceCall`，那里有 `AsyncFrame(...)` 标帧指针；虚调用要补
  同样的标记（帧指针 + `__task` 伪类型）。
- 探针法：这类形状**必须**两侧都跑同一个程序对拍——"宿主能编译运行、自举
  报错"这种分歧只看编译日志容易漏。

## 39. 镜像一条规则时，把它**周围的规则**一起列出来

- 坑（2026-10-20 实测）：自举已经实现了宿主 Task 相关的**一条**赋值规则
  （"无结果句柄塞进 `Task<T>`" → "carries no result"），于是自举以为 Task 的
  赋值规则做完了。实际宿主还有一条更基本的：**Task 形状的槽位只收任务句柄**
  ——`Task<double> t = Half(5.0);` 宿主报 "cannot convert 'double' to 'Task'
  in initializer: no implicit conversion"，自举**接受**（把一个 double 当句柄
  写进槽位）。
- 教训：镜像一条规则时，把它**周围的规则**列一遍再动手。做法：拿一批探针
  （每条是规则的一个角）在**宿主**上跑一遍，拿到"接受/拒绝 + 措辞 + 位置"的
  矩阵，再照着改自举——只补自己想到的那一条，缺口往往就在旁边。
- 细节：**措辞里的类型名不一定带类型实参**。宿主这条把目标拼成 'Task'（不带
  `<T>`），而"无结果句柄"那条把目标拼成 'Task<int>'——两条规则出自同一次
  可赋值检查，拼法却不同，照抄时要**逐条**取基准。
- 细节：**位置**要一起对齐。变量的转换失败宿主指**类型名**（不是变量名，也
  不是 0:0）——自举的变量声明节点本来不记位置，得用类型节点自己的行列。
- 细节：**null 放行**。`Task<int> t = null;` 宿主接受——null 句柄还是句柄，
  规则只在"值的类型不是 Task"时才触发。
- 细节：这条规则**不覆盖实参**。`Take(5)`（Task 形参收裸值）宿主接受，自举
  也接受——别顺手把实参也管起来。

## 40. 没有差分门 ≠ 没有检查：后端路径也得**先查类型再落码**

- 坑（2026-10-20 实测）：自举的 ngen（原生后端）分支在 `main.zan` 里**早于
  检查器**就 return 了——于是一个用了未定义类型的程序，ngen 照样 rc=0 写出
  66KB 的目标码。IR 路径（irgen）是查的，宿主也是查的，只有 ngen 不查。
- 教训：**"这条路径没有差分门"不等于"这条路径没有检查"**。差分门缺的是
  *参照实现*，而类型检查不需要参照实现——它只需要和 IR 路径共用同一个
  Checker。宿主就是"先查类型再落后端"的次序，自举的每条后端路径都该照抄。
- 门禁：没有参照实现的后端，用**自洽门**——"能降级的产出非空产物"+"不能
  降级的**不产出**任何东西并给出诊断"（fail closed）。第二条最关键：没有产物
  就没有"错答案"，比"产物对不对"更容易断言，也更能兜住 fail-closed 边界。
- 坑：断言产物"非空"要定个**下限**（如 64 字节）——空文件也算"有产物"。
- 坑：门禁脚本自己也会编 gen1，所以它同样受"别的会话重建 zanc.exe"的连坐
  影响（撞上被替换的二进制会报 Access violation）。门禁失败先看
  `build\zanc.exe` 的时间戳，别急着怀疑自己的改动。
## 41. 值类型在"没有聚合值"的后端里 = 指针 + **边界拷贝**

- 坑（2026-10-20 实测）：自举 IR 后端没有聚合值，`struct` 实例被表示成堆对象
  （`zalloc` + 字段偏移），于是**赋值 / 传参 / 入容器都只拷指针**——而 C 宿主
  把 struct 按值拷贝。同一个程序：宿主 `list.Add(p)` 后改 `p.x` 读出 `1`，
  自举读出 `99`；`P3 r = q; r.x = 42;` 宿主读 `q.x` 得 `3`，自举得 `42`。
- 教训：后端的**表示**可以不同（堆对象 vs LLVM 聚合值），**可观察语义**不能。
  表示换了，拷贝点就得**逐个补齐**：宿主在哪里按值拷，自举就在哪里显式拷内容。
  "以后改成聚合值就好了"不能作为现在语义不对的理由。
- 做法（`src/selfhost/irgen_struct.zan`）：拷贝 = 分配新实例 + **逐字段**拷，
  字段的分裂照抄宿主：
  - struct 字段 → **递归深拷贝**（嵌套实例也要分配）；
  - class 字段 → **共享**（拷贝方 retain 一次，源方那一份不动）。
- 拷贝点要**全**：赋值（`StoreTo` 与变量声明的初始化式）、传参（`CallArg`）、
  入容器（`List.Add/Insert/set`、`Dictionary.set`、数组下标、集合初始化、
  查询表达式的 select）、字段写入（`StoreField`）。漏一个就是一个别名。
- 坑：struct 字段是**指针**时，**创建实例**就得把嵌套实例分配出来——否则
  `o.in2.v = 5` 是空指针解引用，自举此前直接崩（不是算错，是崩）。
- 坑：拷贝要**无分支**。嵌套字段可能为 null，判空再拷就得把拷贝代码复制两
  份（IR 里 phi 的前驱块里还有 phi → clang 的 ISel 崩，见 §22 的排查法）。
  办法：用 `select` 选源指针——为 null 时读**目标**（刚分配、已清零）那份，
  等价于"拷一份零"。
- 坑：**方法接收者不在拷贝点里**。宿主里 `v.Bump()` 改的就是调用方的 `v`
  （不是 C# 语义）——先在宿主上把这条测准再决定要不要拷。
- 坑：泛型容器的元素类型是**擦除**的，拿不到具体类型 → "这是不是 struct"
  判不出来，泛型容器里的 struct 仍是别名（记为边界，别假装修好了）。
- 坑：自举的**数组没有析构器**（元素从不释放）→ 拷进数组的副本也永远不释放。
  这与"数组里的 class 元素也不释放"同源，别算成新引入的泄漏。
- 门禁：用例 `tests/conformance/struct_value_copy.zan`（8 个形态）与差分门
  一起钉住。

## 42. 运行期类型判定：没有类声明的类型也要有**类标识**

- 坑（实测）：自举的对象头带类标识槽（`@__tid_X` 全局的地址），于是
  `o is Foo` 判得出；但 `o is List<int>` **恒为假**——容器也是堆对象，却
  在 `zalloc` 时把类标识写成 `null`（`zlist_new` / `zdict_new` / `zsb_new`
  的第三个实参）。
- 教训：**"这个类型没有源声明"不等于"这个值没有类身份"**。内建容器
  （List / Dict / StringBuilder）同样要在对象头里写自己的 tid，类型测试
  才认得出它们。
- 做法：给容器各发一个 `@__tid_*` 全局（没有源声明，所以不能走
  `EmitTid`），容器运行时的 `zalloc` 把它的地址写进类标识槽，类型测试在
  遇到容器名时比较这个标识。**用户类占了同名就让位**（同名 `@__tid_*`
  发两遍就是非法 IR）。
- 坑：往 `zalloc` 传类标识时，`bitcast` 的结果**必须带类型前缀**
  （`i8* bitcast (i8** @__tid_List to i8*)`）——call 的实参是
  `<type> <value>` 对，漏了前缀 clang 报 "expected type"，而且报错位置
  落在全局名上，看着像"全局写错了"。
- 坑：**先量宿主的口径**再动手。原方向以为"泛型容器的 tid 要按元素类型
  分别发射"——实测宿主**不区分元素类型**（`List<int>` 的实例
  `is List<string>` 也为真，`Dictionary` 与 `Dict` 是同一个类型），所以
  一类容器一个 tid 就够。按错误的设想做，工作量翻倍还和宿主对不上。
- 坑：类型测试的口径要**连空值一起量**：宿主里空的容器判为**假**（不是
  "静态类型相同即真"）——所以容器必须走运行期判定，不能走静态类型的捷径。
- 门禁：用例 `runtime_type_test.zan`（11 项判定）与差分门一起钉住。

## 43. 帧头要跟**运行时的调度器头**对齐：对齐的是槽位编号，不是结构体

- 坑（实测）：自举的协程帧是**扁平 i64 数组**，头部自定义编号
  （0=state 1=done 2=awaiter …），而发行版 mt 运行时把帧首当 `zan_co_fhdr`
  读写（`rt_io.c` 的 `co_hdr()`：slot0 = `sched` 的 CO_* 位，slot1 = pending
  step）——运行时去改自举的 state/done，状态机当场错乱，程序跑飞。
- 教训：**跨后端共享的内存布局，编号必须跟对方走**。运行时只碰两个槽，
  但那两槽必须**让给**运行时，哪怕自举用不上。
- 做法：帧仍是扁平 i64 数组（不改成宿主那种混合宽度的结构体），但头部编号
  改成宿主的 `ASYNC_FRAME_*` 顺序：0=sched 1=sched_step 2=state 3=done
  4=awaiter 5=awaiter_step 6=result 7=cleanup 8=hcount 9=own_resume，
  自举从 slot2 起；`own_resume` 原本在 slot5，调用点读它的地方同步改成 9。
- 坑：**重编号必须单遍**。逐槽循环替换会级联（把 5 改成 9 之后，后面原本
  就是 9 的又变成 10）。要一次 `MatchEvaluator` 映射全部槽号。
- 坑：帧的**释放**也必须走运行时：直接 `free(frame)` 绕过运行时的 CO_DEAD
  标记，调度器还会去跑已释放的帧。改成调 `__zan_co_frame_free`（宿主语义：
  先标 CO_DEAD 再 free），自举保留 weak 回退（发行版的是强符号，压过回退）。
- 坑：帧的大小与清零范围跟着头部走（8 槽 → 10 槽，72 → 88 字节；action
  帧的委托槽 8 → 10）。
- 坑：链发行版运行时要用 **MinGW 目标**：`clang --target=x86_64-w64-windows-gnu`
  编译 IR + `ld.lld -m i386pep` + 捆绑的 `mingw/lib`；用 MSVC 链会报
  `__mingw_fprintf` 之类未解析（发行版的 `.o` 是 MinGW 构建）。
- 残留：宿主的 `done` 是 i32、在偏移 20（`state`/`done` 两个 i32 挤在一个
  8 字节里），自举的扁平数组把它放在偏移 24——所以运行时的 join 路径
  （`rt_timer.c` 的 `JOIN_OFF_DONE`）**不适用于自举帧**；自举自己降级
  `Task.WhenAll`/Join，不走那条路。
- 门禁：新增 `selfhost_shipped_runtime`（`tests/run_selfhost_mt.cmake` +
  `tests/selfhost/mt/`）——自举产物链**发行版** mt 运行时
  （`toolchain/win-x64/zanrt_io_mt.o`），输出与宿主逐字一致。发行版的
  `zan_co_ready`/`zan_co_sched_init` 是强符号，压过自举的 weak 回退：帧真的
  被发行版的多 worker 调度器驱动，定时器的 ready hook 也是运行时的。

## 44. 重载分派要按**实参类型**选，不能按名字取

- 坑（实测）：自举的运算符分派用 `binder.FindMethod(cls, name)`——**按名字
  取第一个**。三个后果：
  1. 同名重载取错：`v + 2.5` 取了 int 重载，把 `2.5` 当 i64 实参传
     （`floating point constant invalid for type`）；
  2. 调用发的是**未 mangle 的符号** `Owner_op_add`，而定义侧带后缀
     （`MethodSym`）→ 链接不上；
  3. 运算符在**基类**声明时，调用指向操作数的静态类而不是声明类 → 符号
     根本不存在。
- 教训：重载分派的依据是**实参类型**（宿主口径），名字只是候选集；任何
  "按名字取成员"的地方都要再问一句"取的是哪一个重载"。
- 做法：先在类链里找**声明类**（类 → 基类，第一个声明该 `op_*` 的类），
  再在声明类里收齐全部同名候选，按实参类型打分：精确匹配 4 分、
  int→long 3 分、int→double 2 分、已知不匹配 -1 分；最后发
  `MethodSym(声明类, m)`。
- 坑：**调用侧的符号必须与定义侧一致**。定义侧 `GenMethod` 用
  `MethodSym(cls.sval, m)`（声明类 + mangling），调用侧自己拼
  `Owner_op_add` 就对不上（多重重载还会撞名）。
- 坑：字面量的**类型带后缀**（`7L` 是 long，不是 int）。自举里字面量的值
  类型一律 int，选型要用 AST 节点的 `suffix`（与宿主 `checker.c` 的
  `AST_INT_LITERAL` / `lit_suffix` 同口径）。
- 坑：复合赋值要**逐个**对齐运算符家族。`%=` 漏了 → 类指针被喂给 `srem`
  （`'%t48' defined with type 'ptr' but expected 'i64'`）。宿主把
  `+ - * / %` 映射到 `op_add/op_sub/op_mul/op_div/op_mod`（那张表在
  `irgen_expr_core.c` 的 `infer_expr_type`），自举的 `OpName` 要与之一致。
- 教训：**struct 的相等按值**。自举的 struct 是堆对象，指针同一性会把两个
  相等的值判为不等；没有 `op_eq` 时逐字段比（`EmitStructEq`，列表元素那条
  路径本来就在用）。
- 门禁：`operator_overload` / `struct_operators` / `operator_relational`
  进差分门（逐字一致）。

## 45. 重载候选要来自**整条类链**；派生类只遮蔽**签名相同**的那个

- 坑（实测）：自举的普通调用只拿**本类**的重载当候选，派生类声明了同名
  重载就再也看不见基类的：
  - `Derived.Widen(1)` 取了派生类的两参重载 → "missing required argument
    for parameter 'b'"（宿主选的是基类的一参重载）；
  - `Strip.Add(b)`（b 是 Bag）取成了 `Strip.Add(string)` → **无限递归 →
    栈溢出**（0xC00000FD）——调用进了自己。
- 教训：C# 的重载解析覆盖**整个继承链**；派生成员只遮蔽**签名相同**的基类
  成员。`Strip.Add(string)` 不遮蔽 `Bag.Add(Bag)`。
- 做法：候选集 = 本类 + 整条基类链（同名**同签名**的基类重载按"遮蔽"剔除）；
  再按**实参个数**过滤（`AcceptsArity`：默认参数不算必填，`params` 尾巴
  数组吃剩下的实参）；最后按实参类型打分。
- 坑：**调用的符号要指向声明类**。继承来的重载定义在**基类**名下，调用却
  按派生类拼符号 → 符号不存在（旧代码用 `MethodOwner(cls, name)`，那是
  **按名字**找声明类，重载一多就找错；要用"按签名"找：`DeclOwnerOf`）。
- 坑：`await` 的被调方法**也要**按实参类型解析。`AwaitCallTy` 原来用
  `FindMethodUp`（按名字）→ await 临时量的**类型**取成了另一个重载的返回
  类型（类指针塞进 `printf` 的 i64 形参 → 非法 IR）。
- 坑：归一化 await 时**方法体还没生成**，局部变量还不存在——实参的类型要
  从**外层方法的形参**取（`AwaitArgType`）。
- 门禁：`overload_resolution` / `overload_inherited_arity` /
  `overload_same_arity_return` 进差分门（逐字一致）。

## 46. 字段初始化器与构造器：**分配点**才是它们的家，顺序按基类链

- 坑（实测）：自举把字段初始化器塞进"隐式构造器"里，于是两处都错：
  1. **顺序**：自举是"派生类字段 → 基类字段 → 基类构造器 → 派生类构造器"，
     宿主是"**基类字段 → 基类构造器 → 派生类字段 → 派生类构造器**"
     （基类字段初始化器在基类构造器**内部**跑，因为基类构造器先被调用）；
  2. **基类构造器被多调了一次**：宿主在 `new D()`（D 没有自己的构造器）时
     **不调用**基类构造器——只在**分配点**发整条链的字段初始化器。
- 宿主口径（`irgen_emit.c` 构造器发射 + `irgen_expr.c` 的
  `emit_implicit_field_initializers`）：
  - 构造器 C：先调基类构造器（`: base(args)` 按实参选，没有就选**参数个数
    为 0** 的那个），再发 C 自己的字段初始化器，最后构造器体；
  - `: this(...)` 的构造器**什么都不发**，全交给被链接的那个构造器；
  - **基类构造器没被调用**时（C 没有可用的基类构造器），基类链的字段初始化器
    在 C 的构造器里发，**仍然在 C 自己的字段初始化器之前**；
  - `new C(...)` **找不到构造器**时：字段初始化器在**分配点**发（整条链，
    基类在前），基类构造器**不调用**。
- 做法（自举）：宿主没有"隐式构造器"这个函数，字段初始化器直接在分配点发；
  自举的 `this` 硬绑在字面量 `%this` 上，分配点没有 `%this`，于是用一个
  助手函数 `@__zan_finits_<Class>` 代替（它内部发整条链，基类在前）。
  分配点找不到构造器 → 调这个助手；派生构造器没调到基类构造器 → 调**基类**
  的助手。
- 坑：**基类构造器要按参数个数选**（"参数个数为 0 的那个"），不是"取第一个
  构造器"——基类声明了 `B(int)` 和 `B()` 时，取第一个会取到 `B(int)`，
  于是基类构造器**根本没被调用**（基类的字段初始化器也跟着丢了）。
- 坑：**构造器的实参个数要严格**。宿主有两套规则、两套措辞：
  - 后端（`find_ctor`）：参数个数必须**正好**等于实参个数（省略的尾部参数
    用默认值补齐后再匹配一次）；`params` 数组**匹配不上** →
    "no constructor of 'C' accepts 3 arguments"；
  - 检查器（`checker.c` 的 `ctor_arity`）：实参个数落在 [最少必需, 最多] 内
    就行，`params` 数组上界是 -1（任意个数）→ 检查器放行、后端拒绝。
  - 检查器拒绝时的措辞是另一条："no constructor of 'C' takes 1 argument(s);
    a class that declares constructors has no implicit parameterless one"。
  - 类**一个构造器都没声明**时，检查器的措辞是 "no constructor of 'C'
    accepts N arguments"（复数恒定）。
- 坑：**字段初始化器不能编码成 `Assignment` 节点**。自举的 `GenAssign` 靠
  `op == TK.Eq()` 区分纯赋值与复合赋值；合成节点忘了设 `op` → 走复合赋值分支
  → 生成 `add i64 %t6, %t7` 这种非法 IR。字段初始化器要**单独发射**
  （`EmitFieldInits`），不要塞进语句列表。
- 坑：跨类 `bitcast` 的类型**必须带 `%` 前缀**（`bitcast %Derived* %this to
  %Base*`）——漏了前缀 clang 报 "expected type"，且报错行看着像类型名写错了。
- 门禁：`field_decl_initializers` / `constructor_chaining` 进差分门（逐字一致）。

## 47. 索引器：走 parser 合成的 `op_index` / `op_index_set`

- 坑（实测）：自举的 `GenIndex` 只认数组和内建容器，用户类写 `obj[i]` 直接
  报错；`obj[i] = v` 更是落进"字段赋值"分支，**静默写错地方**。
- 做法：parser 已经把 `this[int i] { get {...} set {...} }` 合成成两个方法
  `op_index` / `op_index_set`，自举照着调即可（读取调 `op_index`，赋值调
  `op_index_set`）。没有索引器的类型**必须 fail-closed** 报错，不能默默按
  字段/下标处理。
- 坑：判"是不是用户类"不能只看 `ClassOf(obj.ty)`——`Vec[]` 的
  `ty.sval` 是元素类型名（"Vec"）、`ty.ival` 是 1，会被误判成类。加
  `obj.ty.ival == 0` 守卫，数组才走数组路径（踩了之后 `struct_value_copy`
  用例从 MATCH 变成 BAD-IR）。
- 坑：**多个索引器重载要按实参类型选**（`this[int]` 与 `this[string]`）。
  按名字取第一个（`FindMethodUp`）会走到错的那个重载。做法与 §45 相同：
  候选集来自整条类链（派生索引器只遮蔽**签名相同**的基类索引器），按下标
  实参类型打分（`OpArgScore`），写入口按下标与值两个实参打分。
- 门禁：`cs_b09_indexer` / `indexer_overload` 进差分门（逐字一致）。

## 48. 用户自定义转换运算符：一处查找，五个转换点

- 坑（实测）：自举完全没有 `implicit`/`explicit operator T(S)` 的概念——
  `double d = f;` 把 struct 指针**直接**塞进 double 槽位，程序读到垃圾。
- 宿主口径（`irgen_expr.c` 的 `find_user_conversion` / `find_conversion_method`）：
  转换运算符是**一个静态方法**，名字 `op_implicit` / `op_explicit`，**恰好一个
  参数**，**返回目标类型**、**参数是源类型**；先在**源类型**里找，再在**目标
  类型**里找（所以 `implicit operator double(Feet)` 与
  `implicit operator Fahrenheit(double)` 两种写法都成立）。
- 做法：一次查找（`FindUserConv`）+ 五个调用点：
  | 转换点 | 宿主位置 | 允许的运算符 |
  |---|---|---|
  | 变量声明的初始化式 | `irgen_stmt.c` | `op_implicit` |
  | 赋值 | `irgen_expr.c` | `op_implicit` |
  | 实参 | `irgen_expr.c` | `op_implicit` |
  | return | `irgen_stmt.c` | `op_implicit` |
  | 强制转换 `(T)x` | `irgen_expr.c` | `op_implicit` + `op_explicit` |
- 坑：**转换之后的类型要跟着值走**。return 路径的 ARC 记账原来用**源**类型
  （`v.ty`），转换后值已经是目标类型 → `bitcast %Feet* <double>` 这种非法
  IR。记账要用**转换后**的类型。
- 坑：转换运算符的**参数是 struct 时，实参要按值拷贝**（C# 语义：运算符收到
  的是副本）。自举的 struct 是堆对象，这个拷贝由 `CallArg` 的
  `NeedsStructCopy` 路径自动完成——**不要**在转换点自己再拷一次。
- 坑：自举源码本身受宿主检查器约束（`ClassOf` 可能返回 null → 直接取
  `.sval` 会被宿主拒绝编译）。写自举代码时按宿主的空值规则来。
- 门禁：`cs_b12_conv_operator` 进差分门（逐字一致）。

## 49. 字段遮蔽：名字查找最派生优先，两个槽位都留

- 坑（实测）：自举的 `FieldIndex`/`FieldType` 扫的是**扁平化后的字段表**
  （`CollectFields`，基类在前），名字命中**第一个**就返回 → 派生类遮蔽基类
  同名字段时，读到的是**基类**的槽位。宿主 `get_field_index` 扫的是成员表
  （基类成员在前、派生成员在后），**取最后一个命中** → 派生优先。
- 做法：拆成 `BaseFieldCount`（基类链的字段数）+ `FieldPos`（本类字段在扁平
  表里的位置，本类字段先找，找不到再递归基类）+ `FieldIndex`/`FieldType`。
  派生字段遮蔽基类字段时**两个槽位都保留**（扁平表里各占一个），只是**名字
  解析**指向派生那个。
- 坑：扁平表是"基类在前"，所以派生字段的位置 = 基类链字段数 + 本类内的序号；
  写错顺序会让派生字段和基类字段**写进同一个槽**（互相覆盖）。
- 门禁：`field_hiding` 进差分门（逐字一致）。

## 50. op_call 协议：裸调用的接收者是类类型局部变量

- 坑（实测）：`E(args)`（E 是类类型的局部变量）自举的 IR 路径直接报
  "call expression is not supported"；ngen 路径早就有这个协议，IR 路径漏了
  → 同一份源码两个后端行为不一致（差分门抓到 `cs_b_opcall_params` 生成
  `call i64 @Prog_sc(i64 1.0, i64 2.5)` 这种非法 IR）。
- 宿主口径：`E(args)` 走**被调用对象协议**——E 的类声明了静态 `op_call` 就调
  `Cls.op_call(E, args...)`（E 作为**显式第一个实参**），声明了实例
  `op_call` 就调 `E.op_call(args...)`。
- 做法：**改写成普通调用**再走既有路径（重载选择、params 打包、实参转换全都
  复用），不要自己拼实参。
- 门禁：`cs_b_opcall_params` 进差分门（逐字一致）。

## 51. 隐式构造器转换：标量实参 → 类类型形参

- 坑（实测）：宿主允许 `TakeBool(false)`（形参是类 `Box`，类有 `Box(bool)`
  构造器）；自举的 IR 路径直接把 bool 塞进 `%Box*` 槽位 → 运行时
  0xC0000005 访问冲突，**一个输出都没有**（stdout 缓冲区随崩溃一起丢）。
  ngen 路径有这条协议，IR 路径漏了。
- 宿主口径（`irgen_expr.c` 的 `is_implicit_ctor_source` / `implicit_ctor_for_arg`
  / `wrap_implicit_ctor_arg`）：源类型是**标量/枚举**（bool/sbyte/byte/short/
  ushort/int/uint/long/ulong/nint/char/enum），目标类型是**类**，且该类有
  **匹配的一参构造器** → 实参改写成 `new Target(arg)`；没有匹配构造器 →
  后端报 "cannot convert 'X' to 'Y' in argument: no matching constructor"
  （检查器放行、后端拒绝，所以措辞是后端这一条）。
- 做法：改写成 `new Target(arg)` 后交给既有的 `new` 管线（分配、构造器、
  字段初始化器、ARC 记账全都对）。自举 IR 路径的落点：`GenArgExpr`（所有
  实参的公共入口）。
- 门禁：`implicit_ctor_argument` 进差分门（逐字一致）。

## 52. `new List<T>(src)`：拷贝构造 vs 集合初始化式

- 坑（实测）：自举的 `new List<T>(...)` 只认 `ival == 2` 的**集合初始化式**
  （`new List<T>{...}`），带括号的实参被**默默丢掉** → `new List<int>(src)`
  得到空表，随后 `cp[0] = 100` 越界 → 0xC0000005。
- 宿主口径（`irgen_expr.c` 的 `list_copy`）：**一个**实参且它是 `List<...>`
  → **拷贝构造**（逐元素复制，新表拥有自己的引用）；其它实参 → 集合初始化式
  （`new List<int>(5)` 得到 1 个元素 5 的表，`new List<int>(1,2,3)` 得到 3 个
  元素的表）。
- 做法：自举的运行时有 `zlist_new`/`zlist_add`，补一个 `zlist_copy(dst, src)`
  （逐元素 `zlist_add`，元素归新表所有时照常 retain——和宿主拷贝路径的
  retain 语义一致）。
- 门禁：`list_copy_ctor` 进差分门（逐字一致）。

## 53. 整数宽度：值在 64 位槽上，**进定宽位置时收窄**

- 坑（实测）：自举的 `int`/`uint` 和 `long` 一样映射到 i64 槽，`int z = unchecked(x + 1)`
  得到 2147483648，宿主给的是 -2147483648。
- 宿主口径（实测，探针 `p_intw` / `p_intw2`）：
  - 整数的**运算**在 64 位槽上做：`int x = 2147483647; x + 1` 直接打印是
    **2147483648**（不是 -2147483648）；
  - 但值**进入定宽位置**（变量初始化 / 赋值 / 字段写 / 实参 / return）时，
    **按声明类型收窄**：同一个表达式存进 `int` 就变成 -2147483648，存进
    `long` 仍是 2147483648；
  - `Console.WriteLine(2147483647 + 1)` 打印 **2147483648**——`Console` 是
    内建路径，不走实参转换，不收窄；用户方法的 `int` 形参则收窄。
  - 无符号类型零扩展、有符号类型符号扩展。
- 做法：新增 `IntBits` / `IntUnsigned` / `NarrowToDeclared`，在五个落点调用：
  局部初始化（`GenVarDecl`）、赋值（`StoreTo`）、字段写（`StoreField`）、
  实参（`CallArg`）、return。**内建路径（Console/字符串/容器方法）不收窄**。
- 坑：收窄只对**整数载体**做：指针、double、装箱的可空值、擦除的类型参数、
  数组槽都不动（擦除槽里可能躺着指针，截成 32 位会毁掉它）。
- 门禁：`cs_b14_checked` / `cs_b15_checked_unsigned` 进差分门（逐字一致）。

## 54. `>>>` 逻辑右移：先加宽再逻辑移位

- 坑（实测）：自举的 IR 路径没有 `>>>`（token 有、ngen 路径有），落到
  `ArithOp` 的空分支 → 整个表达式变成常量 0。
- 宿主口径：`>>>` 一律**逻辑右移**（高位补零），与操作数的有号性无关；宿主
  的做法是**先把左操作数加宽到 64 位**（有符号符号扩展、无符号零扩展）再
  `lshr`。所以 `int neg = -16; neg >>> 2` = 4611686018427387900（不是 C# 的
  1073741820），`uint u = 0x80000000u; u >>> 4` = 134217728。
- 做法：自举的槽本来就是 64 位且有符号值天然符号扩展、无符号值天然零扩展，
  所以 `>>>` 就是 `lshr i64`；`>>>=` 走复合赋值的同一条路。
- 顺带：**移位计数要按左操作数的宽度取掩码**（32 位类型 → 31，64 位 → 63），
  否则 LLVM 里越界的移位计数是 poison（宿主的 `mask_shift_count` 同口径）。
- 门禁：`ushr_operator` 进差分门（逐字一致）。

## 55. 静态构造器：`static C() { ... }`

- 坑（实测）：自举把静态构造器当**实例**构造器发射（带 `%this` 形参），
  它于是永远不被调用 → 类里的静态字段停在初值（`Registry.doubled` 是 0
  而不是 6）。
- 宿主口径（实测，探针 `p_sctor` / `p_sctor2`）：
  - 静态构造器**在程序入口就跑**（早于 Main），按**声明顺序**；
  - 每个类内部：**先**静态字段初始化器，**再**静态构造器；
  - 静态构造器**不是**实例构造器：不参与 `new C(...)` 的重载集（宿主把
    `static C()` 和 `C()` 判成"重复构造器"）。
- 做法：`GenStaticCtor` 发一个 `@<Class>_sctor()`（无接收者），`EmitStaticInits`
  在该类的静态字段初始化器之后调用它；`FindCtor` / `FindCtorArity` /
  `CtorDeclCount` / `ResolveCtorEx` 都要**跳过**静态构造器（否则
  `new C()` 会挑中它，或者符号名撞车）。

## 56. 表达式里的简单名：局部 > 字段 > 类型

- 坑（实测）：类 `Emitter` 有字段 `Collapse`，另有类 `Collapse`：
  `Collapse.Add("x")` 被当成**静态调用** `Collapse.Add` → 字段里的列表没收
  到元素（`Count()` 是 0 而不是 2）。
- 宿主口径：表达式里的简单名按**最内层声明**绑定——局部变量、然后是外层类
  的字段、最后才是类型名。字段与类型同名时，**字段赢**。
- 做法：`ShadowsTypeName(name)`（当前类有同名的实例字段或静态字段）→ 静态
  接收者的两条入口都要让路：`GenMember` 的类型接收者分支、`GenCall` 的
  `Cls.Method(...)` 分支。

## 57. null 字符串：软失败读零页

- 坑（实测）：自举的 `zstr_len` 直接读 `-8` 处的头 → 空指针崩溃；字符串
  下标更是直接 GEP+load → 0xC0000005，**整个程序一个输出都没有**（stdout
  缓冲区随崩溃一起丢，看起来像"用例没输出"）。
- 宿主口径：null 字符串是**运行期软错误**——带源位置报告一句，然后**换到
  scratch 页继续执行**：长度探针答 0，下标读出 NUL（打印出来是空行）。
- 做法：`zstr_len` 先判 null → 答 0；字符串下标把基址与下标都 select 成
  零页（`@.zeropage`）与 0 → 读出 NUL。
- 坑：自举的 IR 路径**没有运行期报告通道**（报告只在编译期），所以这条
  用例的 stderr 与宿主不同（stdout 一致）。
- 门禁：`static_constructor` / `static_object_fields` /
  `field_shadows_type` / `null_string_index` 进差分门（逐字一致）。



## 58. NativeMemory.Find：命中位置从**基址**量，不是从窗口起点

- 坑（实测）：自举的 `NativeMemory.Find(p, off, b, n)` 把窗口偏移算了两遍
  ——`rel = hit - p` 之后又 `+ off`，于是 `Find(p, 3, 122, 2)` 返回 6 而不是
  3，`ByteBuffer.IndexOfByte(4, 10)` 返回 11 而不是 7（"从偏移续找"整体
  错位）。
- 宿主口径：`Find` 的返回值是**相对基址 p 的绝对下标**（`at - p`），窗口
  偏移已经包含在 memchr 的起始地址里，**不能再加一次**。
- 做法：自举 `irgen.zan` 的 `Find` 内建只保留 `rel = at - base0`，
  `base0` 是 p（不带 off）。
- 连带：`ByteBuffer.IndexOfByte(from, b)` 是 `Find(data, from, b, len-from)`，
  所以同一个错位会让"续找/最后一个/越界起点"三类断言一起挂。
- 门禁：`bytebuffer_indexof` 进差分门（逐字一致）。
## 59. 擦除的类类型参数：null 比较比的是 0，不是 null 指针

- 坑（实测）：自举把泛型类的类型参数擦除成 **i64 槽**，于是
  `val == null`（`val: T`）发出来的是 `icmp eq i64 %t1, null` —— LLVM 直接
  报 "null must be a pointer type"，链接都到不了（用例
  `generic_type_param_null_check` BAD-IR）。
- 宿主口径：宿主按实参把 `Binding<T>`/`Container<T>` 单态化，槽位是指针，
  比的是 null；自举的槽是 i64（指针的 ptrtoint 值），**0 就是 null**。
- 做法：`GenBinary` 的 null 比较与 `EmitNonNullVal` 都按值的 LLVM 类型选
  零：`IsPtrLL(ty)` 为真才用 `null`，否则用 `0`。
- 连带：同一个错位也会出现在 `x is null`（那里早就按类型分叉了）。

## 60. 自举检查器不得比宿主更严：宿主没有的诊断要删掉

- 坑（实测）：自举检查器有一条 "not all code paths return a value in 'X'"
  ——**宿主根本没有这条诊断**（`src/compiler/checker.c` 里搜不到）。于是
  方法体以 `lock (this) { ... }` 收尾的正常程序（packages 里的连接池就是
  这么写的）被自举判成错误，整个 packages 用例编译不过。
- 口径：**宿主是规格**。自举检查器多出来的诊断 = 自举拒收宿主接受 programs
  = 行为不一致；这种"更严"不是安全网，是偏差。
- 做法：删掉那条诊断（连同只服务于它的 `DefinitelyReturns`/`HasBreak`/
  `IsTrueLit`）。自举 irgen 在方法尾部本来就会补默认返回（`ret i64 0` /
  `ret <ptr> null`），IR 不会缺返回块。

## 61. 裸名：类里的字段 / 属性（实例与静态）

- 坑（实测）：类里写 `Value = 100;`（`Value` 是带 `get/set` 访问器的属性）
  被自举检查器判成 "undefined identifier 'Value'" —— 属性在 parser 里被
  降解成 `get_Value`/`set_Value` 两个方法，检查器的名字表里没有它。
- 宿主口径：表达式里的裸名按**最内层声明**绑定——局部、然后是外层类的
  字段/属性、最后才是类型名（属性与字段同等）。
- 做法：检查器的 `KnownValue` 走 `HasMember`（字段 + 方法 + `get_`/`set_`
  访问器）；`InferType` 对裸名再查一次 getter 的返回类型。
- 连带（自举侧）：裸名读属性走 `get_X`，裸名写属性走 `set_X`；**静态属性
  的访问器没有接收者**，必须发静态调用（`Owner_set_X(v)`），不能按实例
  调用发（`Owner_set_X(%this, v)`）——发错就是"把 this 当参数传"的坏 IR。
## 62. 异步方法里的未初始化局部变量：结构体照样要零初始化存储

- 坑（实测）：自举把 `struct` 实例表示成**堆对象**（一个指针）。同步路径里
  `Vec3 v;` 会给局部发一次零初始化分配（zalloc + 嵌套字段），但**异步路径
  只处理有初始化器的声明**——没有初始化器就什么都不发，局部槽里留着帧槽
  的垃圾值。于是 `v.x = x` 往一个野指针上写，程序 0xC0000005，一个输出都
  没有（用例 `async_struct_return`）。
- 做法：把零初始化分配抽成 `AllocZeroStruct`，同步与异步两条路径共用；
  异步路径没有初始化器时也要发（局部被闭包捕获时先分配 cell，结构体实例
  放进 cell 里）。托管引用类型（类）没有初始化器时存 null。
- 连带：帧槽里的垃圾值不是"未定义但无害"——它会变成野指针，症状是整个
  程序无输出，看起来像"用例没跑"。

## 63. by-ref（`ref`/`out`）形参：传的是地址，引用归调用方

- 坑（实测）：自举 IR 后端只认 `ival == 1` 的 place 形式，而 parser 只给
  `ref x` 置了这个标记——`out x` 落到默认分支，被发成 `%Box* null` 当实参，
  而被调方的形参是 `%Box** %p0`：**类型不匹配**。clang 对 `.ll` 默认不跑
  verifier，这种错配会静默通过，运行期解引用空指针 → 0xC0000005，一个输出
  都没有（用例 `out_param_field`）。
- 宿主口径（`src/compiler/irgen_expr.c` 的 `emit_ref_arg`）：
  - `out T name`（**声明**形式）：新建一个零初始化的槽，登记为局部，传地址；
  - `out x` / `ref x`（**place** 形式）：传该 place 的地址（局部 alloca、
    字段地址、元素地址）；
  - 被调方的形参类型是 `map_type(pt)*`——**指向调用方槽位**的指针。
- 做法：用**节点形状**区分两种形式（`a != null` 是 place，`type != null` 是
  声明），不要靠 `ival`；调用点对 `AK.OutArg()` 统一按"传地址"处理。
- 关键纪律：**by-ref 形参的引用归调用方**。被调方读/写这个槽，但**不能在
  作用域退出时释放它**（宿主的 `local_owns_arc()` 对 `byref_slot` 返回 0）。
  自举原来把它当普通局部，退出时多 `zrelease` 一次 → 调用方的引用被多释放
  一次 → use-after-free。
- 连带：
  - 字段地址是 `getelementptr %Cls, %Cls* obj, i32 0, i32 i`，类型是
    `%Cls*`，而 place 要的是 `<字段类型>*`——必须补一次
    `bitcast %Cls* %p to i64*`，否则 clang 报 "invalid cast opcode"；
  - 虚表槽的函数指针类型（`MethodFnTypeThis`）也要给 by-ref 形参加 `*`，
    否则虚调用和定义对不上；
  - 自举的数组元素一律是 8 字节槽，`out arr[i]` 只能给"槽形状"的元素
    （byte/char 除外），否则要 fail-closed 而不是发出语义不同的地址。

## 64. `Binding<T>` 的槽位赋值糖：IR 后端要自己造 Binding 对象

- 坑（实测）：自举把 `input.data = u.name;`（`data` 是 `Binding<string>` 字段）
  当成普通赋值，把字符串指针 bitcast 成 `%Binding*` 存进槽位——后面
  `Binding_Get` 拿到的是野指针，程序一个输出都没有（5 个 binding 用例全
  DIFF）。
- 口径（宿主 `src/compiler/irgen_expr.c` 的 `emit_binding_value`）：赋给
  `Binding<T>` 槽的**非 Binding 表达式**要包成一个新建的 Binding 对象
  {target, getter, setter, live, constVal, version}：
  - 右值是**字段左值**（`obj.field`，含类内裸字段名 = `this.field`）→
    **活绑定**：target = 接收者（**非持有**的弱引用，否则 model<->view 成环
    泄漏）、getter/setter = 合成的访问器函数对、live = 1；
  - 其他表达式 → **常量绑定**：constVal = 值快照、live = 0；
  - `b = null` → 普通存储（清空）；右值本身是 Binding → 普通存储（共享）；
  - 接收者是"语句结束就死"的 owned 临时（`f().field`）→ 降级为常量绑定
    （活绑定的 target 是弱引用，临时死了 target 就悬空）。
- 做法（IR 后端）：
  - 访问器函数按委托 ABI 发射（env 在前）：`i64 @get(i8* env, i8* target)`
    与 `void @set(i8* env, i8* target, i64 v)`，字段字经 i64 槽搬运
    （指针 ptrtoint/inttoptr，double bitcast）；
  - 委托值是**带 tag 的闭包记录** `{fn, env}`（env 槽为空），不是裸函数
    指针——裸指针走委托调用的"无 env"分支，形参个数对不上；
  - 擦除的类型参数住在 i64 槽里，所以 get/set 的签名都是 i64；
  - 声明路径（`Binding<string> s = m.name;`）与字段初始化器路径要走同一
    套糖，只有赋值路径做是不够的。
- 连带：
  - **类型参数不能当类解析**：方法形参上的 `T` 只活在声明类的作用域里，
    不能被单元里同名的用户类劫持（宿主用 SYM_PARAM 修的）。自举里
    `WrapImplicitCtorArg` 会把 `num.Set(v)` 的 int 实参包成 `new T(v)`——
    用户恰好有个 `class T` 时就发出坏 IR。修法：擦除的类型参数不做隐式
    构造器转换。
  - LLVM 的函数类型在**值位置**不能加括号：`bitcast i64 (i8*, i8*)* @f to
    i8*` 合法，`bitcast (i64 (i8*, i8*)*) @f to i8*` 报 "expected type"
    （括号形式只在实参位置合法）。

## 65. 泛型重载：符号要带类型参数，显式类型实参要参与打分

- 坑（2026-10-20 实测）：自举 IR 后端的 `MethodSym`（`src/selfhost/irgen_stmt.zan`）
  只按"类名_方法名_形参类型标签"拼符号，**不带方法自己的类型参数**。于是
  `class Box { async string GetAsync(string k); async T GetAsync<T>(string k); }`
  的两个重载撞成同一个 `Box_GetAsync___string` —— LLVM 报 "invalid
  redefinition of function"，程序一个输出都没有（用例
  `generic_overload_type_arity`）。宿主是按实参单态化后拼
  `mangle_inst_suffix`（`src/compiler/irgen_emit.c`），自举擦除版要对齐的
  是"泛型与非泛型不能同名"。
- 做法：`MethodSym` 在重载符号里追加方法自身的类型参数标记（泛型重载变成
  `Box_GetAsync___T_string`），两个重载不再撞名。
- 坑（2026-10-20 实测）：**显式类型实参在 parser 里被 `StripTypeArgs` 丢掉了**，
  只留在 call 节点的 `d` 槽里（`__typeargs` 容器，kids 是 TypeRef）。重载解析
  此前完全不看它 → `b.Pick<User>(u)` 选中非泛型的 `Pick(string)`，返回值类型
  都不对（宿主选泛型那个）。
- 做法：`ScoreCand` 里按显式类型实参的个数与候选的类型参数个数匹配与否计分
  （匹配 +8、不匹配 -8），让 `M<int>(x)` 稳定落到泛型重载上。
- 边界：显式类型实参与重载打分只是"选对函数"，**不等于按实参单态化**——
  泛型重载的返回类型仍是擦除的 i64 槽，异步泛型方法（`async T GetAsync<T>`）
  的协程帧与 await 降级也还没做，别把"能选中"当成"能用"。

## 66. 宿主没有的诊断要删；调用要能推断类型；闭包要保留类上下文

- 坑（2026-10-21 实测）：自举检查器有两条宿主**根本没有**的诊断：
  - "cannot return a value from a void method" / "must return a value"
    —— 宿主 grep `src/compiler` 找不到这两条；`static void Main() { ...;
    return 0; }` 是宿主编译并运行的正常程序，自举却整份拒收（用例
    `member_index_store_valid`）。
  - "ambiguous call to overloaded 'X'" —— 宿主从不报歧义，它按自己的最佳
    匹配规则选一个。自举一报就把宿主接受的程序拒了（`Sha1.Hash` 等）。
- 口径（§60 的延续）：**宿主是规格**。自举多出来的诊断 = 自举拒收宿主接受
  的程序，一律删掉；重载打平时取第一个最佳候选，不报错。
- 连带（要让打分有意义）：**调用表达式的返回类型要能推出来**。此前
  `ArgTypeNode` 不认 `Call` → `Sha1.Hash(b.Data(), b.Len())` 的实参类型全是
  "未知" → 所有重载同分 → 判成歧义。补上 Call 的返回类型推断后，
  `Hash(byte[], int)` 才压过 `Hash(string, int)`。
- 坑（2026-10-21 实测）：**闭包里裸调的静态方法会丢掉类名**。自举在发射
  lambda 体时把 `curClass` 清空（"没捕获 this 就没有类上下文"），于是
  `CaptureSafe(exe, args)` 变成 `EmitStaticCall("", ...)` → 符号
  `_CaptureSafe`（无类前缀）、返回类型退化成 i64 → 链接期 "use of undefined
  value '@_CaptureSafe'"（用例 `log_structured`）。
  正确做法：**类上下文与接收者上下文是两回事**——`curClass` 始终保留，
  只有 `curHasThis` 随捕获走。静态字段/属性/方法在闭包里照样可解析。
- 坑（2026-10-21 实测）：**对象初始化器里的属性要走 setter**。
  `new A { Mixed = 50 }`（`Mixed` 是带访问器的属性）此前一律当字段存 →
  字段下标 -1 → "invalid getelementptr indices"；自动属性（`{ get; set; }`）
  在自举里是**后备字段**（没有访问器方法），仍走字段存。
- 补齐的内建（宿主有、自举没有）：`Dictionary<K,V>.TryGetValue(key, out v)`
  （命中才写 out 槽——宿主未命中时**不碰** out 槽）、`List<T>.LastIndexOf`、
  `NativeMemory.FindNotAnyOf` / `ScanNotAnyOf`（strcspn / strspn）。
- 门禁：每修一条，配一个 conformance 用例进 `tests/selfhost/diff_cases.txt`。

## 67. 泛型字段：存储形状按声明类型，值类型按接收者的具体实参

- 坑（2026-10-21 实测）：自举把泛型类的类型参数擦除成 i64 槽之后，**字段读回
  来的值类型也跟着丢了**：`Box<double>.values` 的声明类型是 `List<T>`，读出来
  仍是 `List<T>`（元素类型 T 依旧擦除）→ 元素被当成 i64 槽读 → 打印出位模式
  而不是 double（用例 `generic_field_value_type`）。
- 口径：宿主 `generic_field_slot` 取各实例化的**最大槽宽**（= 8 字节），所以
  两边的**存储布局**本来就一致；差的是**使用点的值类型**——宿主按接收者的具体
  类型实参替换，自举原先直接丢。
- 做法：`LoadField` 把两件事分开：声明类型 `fty` 决定**存储形状**（擦除时仍是
  i64），接收者静态类型 `recvTy` 决定**值类型**——`SubstRecvTy(fty, recvTy)`
  用接收者类型里的具体类型实参，替换字段声明里提到的类级类型参数；擦除字段
  用 `FromSlot(t, vty)` 还原。
- 连带坑（2026-10-21 实测）：`SubstTP` 之前**丢掉了数组的秩**——`T[]` 替换成
  实参后变成裸元素类型（`T[]` → `int` 而不是 `int[]`），字段存的数组指针和读出
  的元素类型对不上。数组类型节点用 `ival` 记秩、`sval` 记元素类型名，替换时只
  换元素、保留 `ival`/`sval2`。
- 边界：这套只解决**使用点**的值类型。泛型**类的方法体**是共享的（擦除），方法
  体里的 `this.item` 拿不到具体实参——那要类级单态化（宿主
  `irgen_generics.c` 的 `generic_insts` + `find_generic_fn`），自举还没有。
  别把"字段读对了"当成"泛型全对了"。
- 门禁：`tests/conformance/generic_field_value_type.zan`——把 `SubstRecvTy` 短路
  成直接返回声明类型，用例会立刻变红。

## 68. 泛型方法单态化：显式类型实参要一路带进克隆体；约束是模板体唯一的线索

- 坑（2026-10-21 实测）：自举有方法级单态化的骨架（`SpecQueue` / `CloneSubst`
  / `SpecTag`），但**实例调用**（`EmitInstanceCall`）根本没走这条路——只有扩展
  方法（`EmitExtensionCall`）和静态调用（`EmitStaticCall`）接了。于是
  `p.Describe<Conn>(c)` 选中的是**擦除**的方法体：体里的 `c` 类型是 `T`，成员
  查不到 → "call to 'c.Name' is not supported"（用例 `generic_instance_method`）。
- 做法：实例调用也走 `SpecQueue`——把方法自己的类型参数绑出来（**显式类型实参
  优先**，其次从实参类型推断），算 tag，克隆一份具体化的方法体，调用点指向克隆
  的符号。静态调用也改成显式类型实参优先。
- 坑（2026-10-21 实测）：`CloneSubst` 之前**只替换节点的 `type` 槽，不替换类型
  名**。类型节点（`AK.TypeRef`）的名字在 `sval` 里 → 克隆体里的显式类型实参还是
  `T`，嵌套调用（`Both<T>` 里的 `this.Describe<T>(c)`）于是又落回擦除形态。修法：
  `CloneSubst` 对 `AK.TypeRef` 节点改走 `SubstTP`（按 `sval` + `kids` 重建类型
  节点）。`SubstTP` 的数组分支还要保住 `op`（可空标记）与行列号。
- 坑（2026-10-21 实测）：**擦除形态的方法体里，类型参数上的成员访问不能报错**。
  宿主对类型参数做成员访问是**静默**的——`checker.c` 的成员检查只对 class /
  struct / interface 报 "has no member"，因为泛型体只是**模板**：真正的成员访问
  发生在单态化之后的体里。自举原来报 "is not supported by the self-hosted
  compiler yet" → 把宿主接受的程序整份拒了。修法：接收者类型是擦除类型参数时，
  模板体返回该擦除类型的零值（不报错）；单态化克隆体干真活。
- 坑（2026-10-21 实测）：`where T : IConn` 这样的**约束**原来被 parser 直接丢弃
  （`SkipWhereClauses`）。约束是模板体里唯一的"接收者能干什么"的线索：丢掉它，
  `T` 上的成员就只能等单态化，而**类**的方法体没有单态化。修法：约束存进类型
  参数节点（`b` 槽），IR 期按**当前作用域**查（方法自己的类型参数遮蔽类的——
  `curMethodNode`），接口约束走虚调用、类约束走直接调用 / 字段偏移。
- 口径补充：**宿主是规格**这条在"能力缺口"上也要用——宿主对类型参数上的成员
  访问不报错，自举就不能报；报了就等于比宿主更严，把宿主接受的程序拒了。
- 边界：这套解决的是**方法级**。泛型**类**的方法体仍然是共享的一份，类自己的
  类型参数在体里拿不到具体实参（`this.item` 里的 `item` 还是 `T`）——那要类级
  单态化，见 §69。自举的 **ngen 后端**已经有一版类级单态化（`ngen.zan` 的
  `SpecRegister` / `SpecRegisterClass` / `RegisterInstCls`），搬到 LLVM 后端就是
  照它设计的。

## 69. 类级单态化：类的类型参数要从**接收者的静态类型**里绑出来

- 坑（2026-10-21 实测）：泛型**类**的方法体是共享的一份**擦除体** → 体里
  `this.item` 的类型还是 `T`（不是 `string`），方法级单态化管不着（它只绑方法
  自己的类型参数）。宿主是连**类的类型参数**一起单态化（`irgen_generics.c` 的
  `generic_insts` + `find_generic_fn`）→ 每个实例化一份方法体。
- 做法（`irgen_expr.zan` 的 `EmitInstanceCall`）：接收者静态类型带了具体类型实参
  （`Pool<string>` 的 kids 数量 == 类的类型参数数量）时，把**类的类型参数**绑成
  这些实参，与方法自身的类型参数**合并成一次克隆**（不是两次——两次会克隆出两个
  体，符号还会撞）。克隆体的 `this` 静态类型 = 具体实例化（`Pool<string>`）→
  体里的字段读回才有真实值类型。
- 坑（2026-10-21 实测）：克隆体和普通体在同一个队列里，符号名是
  `Owner_Method__SP<tag>`。类级和方法级若参数列表相同 → 同一 tag → 抢同一符号
  → **先注册的体赢**，后面所有调用点都打到错的体上。修法：tag 加前缀区分类级
  （`"I"`）与方法级（`""`）。
- 坑（2026-10-21 实测）：`this` 的静态类型不是 `curClass`，而是"当前发射的方法的
  接收者类型"。引入 `CurThisTy()`（`curThisTy` 字段，默认 `MkType(curClass)`），
  克隆体发射期间由 drain 循环设成 `specThis[sp]`（**保存/恢复**，否则后续普通
  方法会继承克隆体的 `this` 类型）。
- 边界：类级克隆只对**本类自己声明**的方法做（`owner == clsName`）。继承来的方法
  （基类没有类型参数）不需要克隆；**虚方法不克隆**——vtable 指向未特化的定义，
  克隆体不会被虚调用到，克隆了是死代码。
- 门禁：`generic_param_shape_overload` / `generic_field_33` 由 DIFF 转 MATCH。

## 70. async 方法的函数类型**恒**返回协程帧指针（`async void` 也不例外）

- 坑（2026-10-21 实测）：`MethodFnTypeThis` 用**声明的**返回类型 → `async void`
  的函数类型是 `void (%Pool*)`，而**定义**返回的是协程帧 `i8*` → vtable 项与定义
  的类型不一致；而且 await 拿不到帧指针 → "this operand is not lowered as a
  coroutine frame"。
- 口径：async 方法的函数**恒**返回 `i8*`（帧指针），与声明的返回类型无关；
  `async int` 的结果在帧里，不在返回值里。
- 连带坑：`EmitVirtualCall` 里 async 判断必须**在** void 分支之前，否则虚调用
  `async void` 方法时帧指针被吞掉。
- 门禁：`async_interface` 用例（async void 经接口虚调用）。

## 71. 泛型类型作形参类型：parser 的"定形"前瞻要扫平衡尖括号

- 坑（2026-10-21 实测）：`using (ConcurrentDictionary<string, int> cd = ...)` 报
  "syntax error: unexpected ','"。`LooksLikeTypedParam` 的前瞻只认 `ident ident`，
  遇到 `Name<...> name` 就判成表达式 → 把 `Name<` 当比较表达式解析 → 在 `,` 上炸。
- 做法：前瞻里跳过**平衡的**尖括号组（`<` / `>` / `>>` 一起计数），再看后面是不是
  标识符。表达式 `a < b` 会跑出角括号组 → 找不到名字 → 仍读作表达式。
- 通用教训：凡是"扫两个 token 判断形状"的前瞻，遇到**带参数的语法**都要扩；否则
  新语法一加就误判（这类坑在自举里反复出现：泛型类型参数、类型实参、where 子句）。

## 72. try/finally：`return` / `break` / `continue` 必须跑 finally（否则死锁）

- 坑（2026-10-21 实测）：自举的 `GenTry` 只在 try 体**正常结束**时跑 finally
  （`if (!term) { ...; GenStmt(s.c); }`）。try 体里一个 `return` 让 `term` 变成
  true → **finally 被跳过**。`ConcurrentQueue.Count()` 正好是这个形状：
  `try { return this.inner.Count(); } finally { Mutex.Unlock(this.mtx); }`
  → 锁永远不释放 → 第二次 `Lock` 同一把 SRW 锁**死锁**（程序无输出、像是卡住）。
- 口径：C# / C 宿主都会在 return 上跑 finally。自举原来不跑 = 语义缺失，不是
  "能力缺口"。
- 做法（照 ngen 的 pending-finally 模型）：每个 try 入栈一条
  `{finally 体, EH 帧是否还在}`；`return` / `break` / `continue` 离开 try 区域时，
  由内向外把还在栈上的 EH 帧弹掉（`@__exc_sp` 必须回落，否则帧数组会越界），
  再依次跑 finally 体。finally 体自己再 return 时靠"正在执行"的标记防止自我
  递归。break/continue 只跑**循环内部**打开的 finally（记一个循环基线）。
- 连带坑：finally 体在 try 体的**正常路径**和 **catch 路径**上各有一份拷贝
  （原代码就是这么发的），别以为 finally 只发一次。
- 坑（2026-10-21 实测）：finally 体在 try 区域里被**内联复制**到每个退出点上，
  所以 finally 体不能假设"只被发一次"；发两份以上时，局部变量槽位会重复注册，
  作用域要各自收干净。

## 73. 泛型擦除必须**按作用域**判定：用户类名不能被同名类型参数击穿

- 坑（2026-10-21 实测）：自举的擦除判定用一份**全局**类型参数名表
  （`tparamNames`），于是 `class T`（用户类）和 `Binding<T>`（类型参数 T）
  同名时，`T` 处处被当成擦除类型 → 用户类被降成 i64 槽 → 程序把指针存进
  i64 槽，`store i64 %t66, i64* %v.victim.4` 直接是坏 IR（用例
  `binding_tp_shadowed_by_user_class`）。
- 口径（C# 语义）：**类型参数只在自己声明的作用域里遮蔽同名用户类型**。
  `Binding<T>` 的体里 `T` 是参数；`Program` 里 `T` 是用户类。
- 做法：擦除判定改成"作用域感知"——
  1. 名字是**当前类/当前方法**的类型参数 → 擦除；
  2. 否则程序里有同名**用户类型** → **不擦除**（那是用户类）；
  3. 否则 → 擦除（别的类的类型参数）。
  判定用的作用域不是恒等于"当前类"：**别的类的方法**的声明类型写在**声明类**
  的作用域里（`MapTypeDecl` / `IsErasedTypeDecl`），**类布局**、**析构函数**、
  **静态字段**的类型也写在**各自类**的作用域里。
- 连带坑：类布局（`%C = type { ... }`）和析构函数是在**类上下文之外**发射的，
  忘了切作用域 → 泛型字段被按用户类型布局（`%Binding = type { ..., %T*, ... }`）
  → 析构函数还会去 release 一个其实是 i64 槽的字段 → **访问违例**。
- 连带坑：方法级/类级单态化之外，**索引器与属性访问器**（`op_index` /
  `op_index_set` / `set_X`）也必须走类级单态化——它们的形参/返回值是类的类型
  参数，擦除形态会把 string 键按**数字**格式化（`snprintf("%d")`）后塞进字典，
  查不到就抛异常。
## 74. 泛型类的构造器：实参推断用**具体类型**，调用 ABI 仍是**擦除形**

- 坑（2026-10-21 实测）：`LabelOf<Opt>` 这类泛型类的构造器，形参是委托类型
  `delegate Label<Opt> (Opt)`。自举用**声明的形参类型**（`T` → 擦除的 i64 槽）去推断
  实参 → 传进去的 lambda 拿到的是**擦除的形参类型** → lambda 体里写什么都不生效
  （用例 `generic_nested_generic_field`）。
- 口径：擦除只影响**存储形状**，不影响**实参推断**。lambda 的形参类型必须来自
  接收者的**具体类型实参**。
- 做法：调用构造器时，形参的**推断类型**用 `SubstTP(pty, ctps, ty.kids)`（具体），
  但**实参的槽形状**仍用 `MapType(pty)`（擦除）——构造器体是**共享**的（不克隆），
  它按槽收参数，槽宽按声明类型；把 double 之类的具体值直接塞进 i64 槽会被截断。
- 连带坑：泛型类的**引用类型字段**在**存字段**的那一刻要 retain。字段**拥有**它
  持有的引用；克隆体里值类型已知，不 retain 的话字段存的是借用指针，
  调用方释放临时后字段悬空 → 下一次读是 use-after-free → 堆损坏。
## 75. 泛型类的静态成员按**实例化**隔离（宿主的 mangle_inst_suffix 语义）

- 坑（2026-10-21 实测）：自举给泛型类的静态字段只发一个全局
  （`@__zan_sf_Stat_s`），于是 `Stat<int>.s` 与 `Stat<double>.s` 是同一格，
  `Stat<int>.Bump()` 加的是所有实例化共享的计数（用例 `generic_statics`：
  自举 i=6/d=6/s=6/f=7，宿主 i=4/d=4/s=5/f=5）。静态字段的**初始化器**同理：
  `public static int seq = Seq.Next();` 必须**每个实例化各跑一次**，否则
  70 个实例化的求和差一个（用例 `generic_inst_count_70`：宿主 2485=1+…+70）。
- 宿主口径（`irgen_emit.c` / `irgen_arc.c`）：
  - 宿主给每个泛型类发**一个擦除变体 + 每个具体实例化一个变体**；
  - 静态字段的全局名按实例化加后缀（`mangle_inst_suffix`）→ 每实例化一份存储；
  - 静态字段的**存储形状不随实例化变**（宿主用声明类型 `map_type(fsym->type)`，
    只改名字不改类型）——别自作聪明把 `T` 换成具体类型，形状会和宿主不一致；
  - 静态字段初始化器按变体各跑一次，**擦除变体的那一份不跑**（否则
    `Seq.Next()` 会被多调一次）；
  - 泛型类的静态**方法**也按实例化各有一份（`Stat<int>.Bump()` 走
    `Stat<int>` 的那份方法体）。
- 做法（自举侧）：
  1. parser 要**保留类型实参节点**（不能只留名字）：`Stat<List<int>>.s` 的
     实参名字拆不回来。`StripTypeArgs` 把 `__typeargs` 容器留在 MemberAccess
     的 `d` 槽（`sval2` 的名字串继续给 ngen 用）。
  2. 发射前先扫一遍 AST 收集**所有具体实例化**（宿主的 `discover_generic_insts`）：
     类型引用（`new Wrap<T0>()`、字段/局部类型）与泛型限定的成员访问
     （`Stat<int>.s`）都要收。静态存储要在发全局之前就知道有哪些实例化。
  3. 静态字段的全局名 = `@__zan_sf_<类>_<实例化标签>_<字段>`；实例化标签由
     实参的类型名拼成（`int`、`double`、`string__int`）。
  4. 静态方法按实例化走**类级克隆**（和实例方法同一套 `SpecTagC` + `I` 前缀），
     克隆体里的 `Stat<T>.s` 经类型实参替换后指向该实例化的全局。
  5. 克隆体里的**裸**静态字段（不带 `Stat<...>` 限定）要用"当前克隆的实例化"
     定位存储：克隆的接收者静态类型（`curThisTy`）就带着实参。
## 76. 无类型的 lambda 参数在推断 body 类型时要换成委托的形参类型

- 坑（2026-10-21 实测）：`us.MinBy(u => u.name)`（`MinBy<T, R>(this List<T>, Sel<T, R>)`）
  在自举里把方法类型参数 **R 绑成了 `var`**：克隆体的符号名是
  `Ext_MinBy__SP_double_var_List.T_Sel.T.R`——`var` 是 parser 给**无类型**
  lambda 参数写的占位类型（`parser.zan` 的 `ParseLambda`：`p.type =
  MkTypeRef("var")`）。
  后果是克隆体里 `R mk` 成了**指针槽**，而 `k < mk` 按整数比较 →
  `icmp slt i64 %t51, %t52`（`%t51` 是 `i8*`）→ **坏 IR**（用例
  `generics_uniform_repr`）。
- 根因：`LambdaBodyType` 只在形参类型为 **null** 时才用委托的形参类型；
  parser 给的是 `"var"`（不是 null）→ 形参类型保持 `var` → body 的类型
  （`u.name`）推不出来 → 类型参数绑成 `var`。
- 做法：把 `"var"` 和 `null` 一样看待——无类型的 lambda 参数取委托的形参
  类型（`dps[pi]`）。绑上之后 `u => u.name` 的 body 类型是 `string`、
  `u => u.score` 是 `double`、`x => x`（`List<double>`）是 `double`，
  R 绑到真正的类型，克隆体里的 `R mk` 才是 double/string 而不是指针。
- 通用教训：**"没写类型"和"类型未知"在 AST 里是两种表示**（null vs
  `"var"`），凡是"没有就用上下文补"的地方，两种都要认。
## 77. 硬件内建要复刻宿主的降级表；自举 IR 不能用目标特性 intrinsic

- 坑（2026-10-21 实测）：自举把 `NativeMemory.AesCbcEncrypt` 这类硬件内建
  当成普通 extern 方法，符号按驼峰拼成 `@zan_hw_AesCbcEncrypt` → 链接期
  LNK2019（运行时导出的是小写下划线 `zan_hw_aes_cbc_encrypt`）。
- 根因：**宿主不是在 extern 声明层改符号名**，而是在调用点把整张硬件内建表
  **内联降级**成对 `zan_hw_*` 的直接调用（`src/compiler/irgen_expr.c` 的
  `emit_native_memory_call` / `emit_pixel_ops_call`）：每个方法的**参数顺序按
  运行时签名重排**（`AesCbcEncrypt(dst,src,size,key,keybits,iv)` 在运行时的
  顺序是 `src,size,key,keybits,iv,dst`），`keybits` 还要收窄成 i32。
  所以自举不能"声明 extern 再按声明序调用"，必须照抄这张表。
- 做法：自举侧建三个助手——`NmCall`/`NmCallVoid` 发调用，`NmI32` 把整数实参
  收窄到 i32；参数值是裸的，**类型由运行时签名串给出**，按签名逐个拼成
  typed value。覆盖清单：AES（CBC/ECB/CTR/GCM）、GHASH、RSA（ModPow/CRT）、
  X25519、SM4、JSON 扫描、Base64、摘要（SHA1/256/512、SM3）、Load64/AsI64/
  AsF64、ScanNotByte，以及 PixelOps 的 BlendOver/SwapRB/FillRect/
  ResampleBilinearRow。
- 坑（LLVM 文本 IR 语法，实测）：call 的实参**必须**是 typed value
  （`i8* %p`、`i64 16`、`i32 %t5`），裸 SSA 名或裸常量都报
  "expected value token"；指令操作数**不能**是括号表达式
  （`and i64 (lshr i64 %d64, 8), 255` 非法），要拆成单独的 SSA 名；
  同类型的 `bitcast` 也非法（用 `add x, 0` 代替）。
- 坑（可移植性，实测）：自举产出的 .ll 由 sweep 用 **generic CPU** 的 clang
  编译；宿主自己出码带 `-mcpu/-mattr` 所以不受限。于是自举 IR **不能**用
  需要目标特性的 intrinsic——`llvm.x86.sse42.crc32.*` 在 generic CPU 下
  "Cannot select"。`Sse42.Crc32` 改成查表 CRC32C 的软件实现（逐字节、
  小端），语义与硬件指令一致。
- 通用教训：**宿主是规格**——自举要复刻的是宿主的**降级表**（符号名 + 参数
  顺序 + 类型宽度 + 可移植实现），不是"能编译过"就行。

## 78. byte[] 在自举里必须真的是字节数组（一字节一个元素）

- 坑（2026-10-21 实测）：自举的数组一律按 **8 字节槽**存元素，但宿主里
  `byte[]` 就是字节数组——标准库把它当**原始缓冲**交给运行时（Base64、
  摘要内核、协议编解码）。自举里 `byte[]` 每元素占 8 字节，于是所有
  "按字节读"的代码读到垃圾：Base64 输出 `SAAAAAAAAABhAAAAA...`，
  `Aes.DecryptCbc` 解不出明文，hw_crypto_pixel 在 `dec.ToStr(...)` 上
  **空指针崩溃**（解密返回 null）。
- 判据：**标准库有没有把这种数组当字节缓冲用**。有，就必须按字节存；
  没有（只是元素列表），8 字节槽行为等价，可以不动。
- 做法（自举侧）：`ArrElemSize` 决定元素宽度——`byte`/`sbyte` 是 1 字节，
  其余仍是 8 字节槽（`char[]` 保持 8 字节：char 是码点不是字节，截到
  1 字节会丢值）。要跟着改的点：分配（`new T[n]` 的字节数与初始化器）、
  字符串↔byte[] 桥接（ToBytes / ToStr）、索引读写、`List<T>.ToArray()`、
  `foreach`。
- 陷阱：数组指针的静态类型还是 `i64*`（宽度未知时才统一），所以 1 字节
  元素要 `bitcast i64* → i8*` 再 `getelementptr i8`；`store`/`load` 的
  宽度也要跟着变（`trunc i64 → i8` / `zext i8 → i64`）。
- 通用教训：**"行为一致"不等于"随便换个布局"**——布局只要被标准库看见
  （被当缓冲、被当指针传出去），就必须和宿主一样。
## 79. 位运算内建要按实参的**声明宽度**落码（32 位 vs 64 位）

- 坑（2026-10-21 实测）：自举所有整数都住在 64 位槽上，
  `BitOperations.RotateLeft/RotateRight` 一律用 `llvm.fshl.i64`/
  `fshr.i64`；宿主按实参的**声明类型**选宽度（`is_64bit_int_arg`：
  `long`/`ulong` 走 64 位，其余收窄到 32 位）。于是
  `BitOperations.RotateLeft(int, int)` 在自举里是 64 位旋转——
  `RotL32` 全部算错，`Md5.Hash("")` 给出的是 `md5("hello world")`；
  `LeadingZeroCount`/`TrailingZeroCount` 对负数也错（64 位的符号扩展
  把前导零算没了）。
- 做法：判宽度用**实参的声明类型**（自举的 `ArgTypeNode`），不是值的
  运行时类型——自举的值永远是 64 位槽。32 位路径：`trunc i64 → i32`，
  用 `fshl.i32`/`fshr.i32`/`ctlz.i32`/`cttz.i32`，再 `zext` 回槽。
- 通用教训：**自举的"统一槽宽"会把宿主的定宽语义抹平**。凡是宿主按
  参数类型分 32/64 位的地方（位运算、`ToString` 的格式宽度、窄化转换），
  自举都得显式收窄再算。

## 80. 给未声明标识符赋值：宿主**静默丢弃**，只有"读"才是错

- 坑（2026-10-21 实测）：自举的 checker 对未声明标识符一律报
  `undefined identifier 'x'`（读和写都报）。宿主只在**读**的时候报错
  （措辞 `use of undeclared identifier 'x'`，由 irgen 兜底发出）；**写**
  （`x = v`）在宿主里既不报错也不存——左值解析到"局部变量 / 实例字段 /
  静态字段"三处都没命中时，这次赋值被直接丢掉。于是标准库里一段
  "赋了但没人读"的代码（Zan.Gui 的 `Control.zan` 给从未声明的
  `paneMin`/`paneMax` 赋值）把整包编译拖垮：自举报
  `undefined identifier 'paneMin'`，宿主照收不误。
- 做法：checker 对**普通 `=`** 的左值放行——左值是标识符且
  `KnownValue` 为假时只检查右值；复合赋值（`+=`）和 `??=` 仍要报错
  （它们要先读左值）。自举的 irgen 本来就"静默丢弃"（`StoreTo` 找不到
  落点就 `return`），所以只有 checker 这一处要放宽。
- 通用教训：**"宿主是规格"包含宿主的宽容**。自举比宿主更严，就会把
  宿主能编译的程序判死；诊断的"宽严"也要对齐，不能只对齐措辞。
## 81. `phi` 放进函数入口块：初值永远不落地，循环变量是垃圾

- 坑（2026-10-21 实测）：自举给"结构体数组填默认实例"生成循环时写成
  ```
  define void @T_initarr(i64* %arr, i64 %n) {
  entry:
    %i = phi i64 [ 0, %entry ], [ %i2, %body ]
  ```
  `phi` 待在**函数入口块**里：前驱只有回边 `body`，入口自环那条入边
  没有独立的块来承载"把 0 写进 phi 槽"的赋值——LLVM 把它降成一次
  **未初始化的栈槽读取**。于是循环计数器是垃圾值：要么一次都不跑
  （数组元素全是 null），要么写到野地址（当场崩）。表现是
  `new Point[3]` 之后 `pts[0].x = 5` 空指针崩溃，而
  `new Point[]{p, p}`（初始化器路径）正常。
- 做法：循环用**规范形状**——入口块只 `br label %loop`，`phi` 放在单独
  的 `%loop` 块里：
  ```
  entry: br label %loop
  loop:  %i = phi i64 [ 0, %entry ], [ %i2, %body ]
  ```
  自举里另两处同样的循环（`__zan_nm_crc32`、`__zan_async_*`）本来就是
  规范形状，只有新加的这处踩了坑。
- 通用教训：**生成的 IR 要按语义写，不要按"看着像"写**。LLVM 文本 IR
  里 `phi` 的入边必须与前驱一一对应；入口块自带的那条"函数入口"入边
  没有块来承载它的赋值。
## 82. 自举把结构体**装箱**：数组元素与静态字段各要一个默认实例

- 坑（2026-10-21 实测）：自举没有聚合值——一个结构体实例是堆对象
  （`%Name*`），结构体字段存的是**指向嵌套实例的指针**。宿主把结构体
  **按值内联**在数组槽/静态字段里，所以 `new Point[3]` 的三个元素天生
  就是三个"默认值"。自举的数组槽是 i64，calloc 出来是 0 → 槽里是
  **空指针**，`pts[0].x = 5` 直接空指针崩溃；静态结构体字段
  （`static Outer global;`）同理。
- 做法：给每个结构体类型生成两个模块级函数——`<T>_newobj()`（分配一个
  零值实例，并给它的结构体字段递归分配嵌套实例）与
  `<T>_initarr(arr, n)`（按槽填默认实例）；`new T[n]` 在没有初始化器
  时调用后者。静态结构体字段在**静态初始化**里调 `<T>_newobj()` 存进
  全局。类实例的结构体字段本来就有（`InitNestedFields`）。
- 通用教训：**换一种表示法（装箱 vs 内联）就要把"默认构造"的语义一起
  搬过来**——零值不再自动等于"一个可用的实例"。
## 83. 对象初始化器写 `Binding<T>` 字段：值要包成**新的 Binding 对象**

- 坑（2026-10-21 实测）：`new Card { title = "x" }`（`title` 是
  `Binding<string>`）在自举里把**裸字符串**直接塞进 `%Binding*` 槽，还
  按对象指针去 `zretain`——把字符串头当成 Binding 头改引用计数，
  随后 `a.title.Get()` 在字符串头上读长度，当场崩。普通赋值路径
  （`a.title = "y"`）是对的：它走 `GenBindingValue`，会包一个新 Binding
  对象。
- 做法：对象初始化器的字段写入要和普通赋值**同一条路**——先按字段的
  声明类型判断是不是 `Binding<T>`，是就调 `GenBindingValue` 包一个新
  对象再存（`GenBindingValue` 自己也只在"普通存储"时才返回 null）。
  坑出处：`tests/conformance/objinit_ctor_field_overwrite.zan`。
- 通用教训：**同一个语义只留一条实现路径**。初始化器与普通赋值是同一
  件事的两种写法，分开实现就会漏掉其中一种的转换规则。
## 84. 委托重载打平，多半是**参与打分的类型没推出来**

- 坑（2026-10-21 实测）：`items.OrderBy(x => x.Length)`（x 是 string）
  在自举里选中了 `StrKeySelector` 重载而不是 `KeySelector`（int 返回），
  委托按 string 返回而 lambda 返回 int，运行时崩（exit -1073741819）。
  宿主靠"lambda 体类型 == 委托返回类型"给候选加分
  （`src/compiler/irgen_expr_core.c`：`types_concrete_equal(bt,
  pt->delegate_ret_type)` → +2）。自举的 `ArgTypeNode` 对成员访问
  **只查类字段**，`x.Length` 的接收者是 string 这个内建类型 → 查不到
  → 返回类型推不出来 → 三个同名重载全部打平 → 按声明顺序选中第一个
  （`Enumerable.zan` 里 `OrderBy` 的第一个重载就是 `StrKeySelector`）。
- 做法：
  - `ArgTypeNode` 的成员访问按宿主 `src/compiler/builtin_api.c` 的属性表
    补内建成员：string.Length、List.Count、Dict.Count/Keys/Values、
    StringBuilder.Length，以及数组的 Length/Count。
  - 块体 lambda 的返回类型按宿主 `stmt_lambda_return_type` 收集：所有
    `return` 的类型必须一致（第一个说了算）；裸 `return;`、循环体、
    推不出的类型 → 未知。
  - 评分与宿主一致：**类型相同加分，类型不同不扣分**（未知与不匹配同分，
    按声明顺序决胜）。自举原来给"不匹配"扣分，于是"候选 A 未知 /
    候选 B 不匹配"时会选错。
- 通用教训：**"重载打平"往往不是重载规则的问题，是参与打分的那个类型
  没推出来**——先确认评分输入的类型是真的，再去调权重。宿主是规格：
  加分制不是扣分制。
## 85. 字符串转义解码只有一份实现（宿主在**词法期**解）

- 坑（2026-10-21 实测）：`"\u0001"` 在自举里解成了 "u0001" —— 自举的
  `DecodeStr` 只认 `\n \r \t \0`，其余转义原样落成字母。宿主在**词法期**
  就把转义解掉（`src/compiler/lexer.c` 的 `lexer_escape_seq`）：`\xNN`
  （1..4 位十六进制）与 `\uNNNN`（4 位）解成码点，再按 UTF-8 编码
  （`zan_utf8_encode`）；代理码点（D800..DFFF）不接受。
  表现：`Log.EscapeJson("x\u0001y").Contains("\u0001")` 在自举里恒为真
  → 断言反转 → `log_structured` 第 6 行输出 false（宿主 true）。
- 做法：转义解码只留一份实现（`ast.zan` 的 `StrEsc`），运行期字符串
  （`irgen`）、属性字面量（`parser`）、编译期元数据（`attrgen`）三处都
  走它。原生后端那套早就自己解了 `\x` `\u`（`ngen.zan` 的
  `DecodeLiteralNg`），这次是把 LLVM IR 后端补齐。
- 通用教训：**同一个语义只留一条实现路径**。转义解码曾经有三份
  （irgen / parser / attrgen），各写各的，改一处就漂。宿主在词法期解、
  自举在生成期解——位置可以不同，规则必须只有一份。
## 86. 泛型类的构造器要**按实例特化**（宿主 find_generic_ctor）

- 坑（2026-10-21 实测）：`class Bag<T> { List<T> src; Bag(...) { src = new List<T>(); } }`
  在自举里构造器是**所有实例共用的那一份**（`Bag_ctor__TextOf.T`），于是
  `new List<T>()` 的元素类型是擦除的 T → 列表建出来是
  `zlist_new(i64 0)`（**不拥有元素**）→ 元素没被 retain → lambda 读完
  字段就 release 掉 → 之后再读就是 use-after-free → 堆损坏
  （exit 0xC0000374，离崩溃点很远）。
  宿主按实例特化构造器（`src/compiler/irgen_expr.c` 的
  `find_generic_ctor`）：`Bag<Item>` 的构造器里 `new List<Item>()` 的
  列表带元素描述符、**拥有**元素。
- 做法：泛型类的 `new` 调用点按类实例特化构造器（符号带类型实参），
  构造器体用**代入后的** AST（`CloneSubst`）生成，接收者的静态类型带上
  具体类型实参（`curThisTy`/`curInstArgs`），字段读回来才是
  `List<Item>` 而不是 `List<T>`；调用点的实参也用具体形参类型。
- 通用教训：**"所有实例共用一份泛型代码"和"每个实例一份"是两种语义**，
  差别通常落在集合的**拥有关系**上（谁 retain 谁）。宿主选了每个实例
  一份，自举就得跟上，否则表现为远处的堆损坏。
## 87. 泛型类里的委托字段：签名要按接收者特化（宿主 subst_delegate_sig）

- 坑（2026-10-21 实测）：`class Bag<T> { TextOf<T> text; string TextAt(int i) { return text(src[i]); } }`
  在自举里 `text` 的类型是擦除的 `TextOf<T>` → 委托调用按
  `i8* (i8*, i64)*` 发出，把元素**当整数**传进去；而闭包
  （`__lambda_0`）的形参是 `%Item*` → 闭包把整数当指针解 → 崩。
  宿主在 `src/compiler/irgen_expr.c` 的 `subst_delegate_sig` 里按
  **接收者的类型实参**特化委托签名（`T` → `Item`）。
- 做法：委托调用发射前，按当前实例（`curInstCls`/`curInstArgs`）把委托
  类型的类型实参代入（`InstDelegateType`）。
- 通用教训：**擦除的槽位形状和真实的值形状要分开**——存储用擦除形，
  值的类型按实例还原；漏了还原就是"把整数当指针"。
## 88. 构造器重载：lambda 实参**只**匹配委托形参（宿主 find_ctor）

- 坑（2026-10-21 实测）：`new Bag<Item>(i => i.name)`（`Bag<T>` 有
  `Bag(List<string>)`、`Bag(TextOf<T>)`、`Bag(RowOf<T>)` 三个构造器），
  自举按"实参类型未知"把三个都打平 → 按声明顺序选中
  `Bag(List<string>)` → lambda 的形参没有类型 → 体里的 `i.name` 报
  "member 'name' of type 'int'"。
  宿主的 `find_ctor`（`src/compiler/irgen_expr_core.c:917`）把不匹配的
  候选**排除**（compatible=false）：lambda 实参只匹配委托形参，且委托的
  形参个数必须与 lambda 的相同。
- 做法：构造器候选筛选阶段加这条规则（`CtorTakesLambda`）。
- 通用教训：**"实参类型未知"不等于"实参适配任何形参"**——宿主在实参
  类型推不出来时按**形状**（是不是 lambda、委托的元数）先筛一遍。
## 89. 重载评分：形参提到类型参数时按"模类型参数"匹配

- 坑（2026-10-21 实测）：`new Bag<Item>(new Col<Item>("Name", Cells.NameOf))`
  （`Bag<T>` 有 `Bag(List<Col<T>>)`、`Bag(Col<T>)`、`Bag(RowOf<T>)` 三个
  构造器）在自举里三个候选都拿到同样的 +2（"形参提到了类型参数"就加分）
  → 打平 → 按声明顺序选中 `Bag(List<Col<T>>)` → 列数不对。
  宿主只在实参**模掉类型参数**后与形参同形时才加分
  （`src/compiler/irgen_expr_core.c` 的 `types_match_modulo_tp` → +3）：
  `Col<T>` 匹配 `Col<Item>`（+3），`List<Col<T>>` 与 `RowOf<T>` 都不匹配
  （+0）。
- 做法：`ScoreCand` 把"形参提到类型参数"这一支从"无条件 +2"改成
  "模类型参数匹配得上 +3，否则 0"（`TypesMatchModuloTP`）。
- 通用教训：**"提到了类型参数"不等于"匹配"**——泛型形参要比的是**去掉
  类型参数后的形状**（类名 + 层数 + 其余类型实参），不是"有没有提到"。
## 90. 方法组实参只匹配委托形参（宿主 arg_method_group）

- 坑（2026-10-21 实测）：`new Bag<Item>(Rows.Of)`（`Rows.Of` 是静态方法，
  当委托值用）在自举里选中了 `Bag(List<Col<T>>)` → 委托字段是空的 → 崩。
  宿主把方法组实参**排除**在非委托形参之外（`find_ctor` 的
  compatible=false 规则 + `method_group_score`：必须是委托形参、参数个数
  相同、返回类型同族）。
- 做法：构造器候选筛选加方法组规则（`CtorArgFits`），评分按
  `MethodGroupScore`（返回类型同族：同型 +2，不同族直接排除）。
- 通用教训：**"方法名不带括号"是一种实参形状**，要按形状（而不是按
  "类型未知"）去筛候选。
## 91. 命名空间限定的静态接收者也要走命名空间解析

- 坑（2026-10-21 实测）：`Widget.Tooltip.Flush(this)`（`namespace Gui;`
  里的 `Gui.Widget.Tooltip`）在自举里报 "unresolved identifier 'Widget'"。
  自举的 nsresolve 只对**单段**接收者（`Cls.Method`）做命名空间解析，
  限定接收者（`A.B.C`）只做**全名精确匹配**——"Widget.Tooltip" 不是
  任何声明的全名（"Gui.Widget.Tooltip" 才是），于是没被折叠成那个声明。
  宿主的 nsresolve 对限定名同样按"当前命名空间 → using → 全局"解析。
- 做法：限定接收者的折叠改用与静态接收者同一套解析
  （`ResolveStaticReceiver`），并加一条保护：链头的标识符若是局部变量
  （被遮蔽）就不折叠——那是字段访问，不是类型。
- 通用教训：**同一套名字解析规则要覆盖单段名和限定名**——只给一半用
  命名空间解析，缺口就会出现在"带点号的名字"上。
## 92. `base.M()` 是**直调**，绕过虚表

- 坑（2026-10-21 实测）：`base.Name()` 在自举里报 "call to 'Name' is not
  supported by the self-hosted compiler yet"——自举根本没处理 base 接收者。
  宿主把 `base.M()` 降成**直调**（`src/compiler/irgen_call.c` 的 base_recv →
  `emit_dispatch_call` 传 NULL 类，绕过虚表）：基类自己的函数体执行，不是
  虚函数分发。
- 做法：`EmitInstanceCall` 加 `direct` 开关（跳过虚表），`EmitBaseCall`
  把 `this` 按基类指针转换后直调基类方法。
- 通用教训：**"基类调用"不是"虚调用的另一种写法"**——它绕过虚表，语义
  与 `this.M()` 相反，实现时不能复用虚分发那条路。
## 93. async 泛型方法按实例特化（与同步泛型方法同一套）

- 坑（2026-10-21 实测）：`await s.Echo<int>(42)`（async 泛型方法）在自举里
  报 "not supported by the self-hosted compiler yet"——`CanSpecialize` 里
  一句 `if (IsAsync(m)) return false;` 把所有 async 方法挡在特化之外，
  调用点只能打到**擦除**的共享体（`async U Echo<U>(U x)` 的 `U` 是 i64 槽）。
  宿主对 async 方法一样按实例特化（与普通泛型方法同一条路）。
- 做法：放开 `CanSpecialize` 的 async 限制；克隆体的符号是克隆自己的名字
  （`Owner_M__SPint`），`GenAsyncMethod` 用 `MethodSym(cls.sval, clone)` 算
  出来正好等于 `SpecQueue` 交给调用点的那个符号——两边天然对齐；调用点的
  **awaited 结果类型**要用代入后的返回类型（`tt.a = SubstTP(m.type, ...)`）。
- 通用教训：**"特化"是按调用点的类型实参做的 monomorphization，与方法
  是不是 async 无关**；async 只是把"结果类型"藏在了协程帧里，别把它当成
  不能特化的理由。
## 94. 调用点的返回类型要代入类型实参（接收者实例化 + 显式类型实参）

- 坑（2026-10-21 实测）：`await ps.Get<int>(1)`（`Pool<string>` 的方法，
  返回 `T`）在自举里把结果打成了一串指针数字（`140698019280840`）而不是
  `hello`：await 临时量的类型来自 `AwaitCallTy` → 克隆方法**声明的**返回
  类型 `T`（擦除），没有代入。宿主在检查器里就代入了
  （`src/compiler/checker.c:2880`：`binder_subst_named(ret, tps, targs)`）。
- 做法：加 `SubstCallType`（接收者的具体实例化绑定**类**的类型参数，
  显式类型实参绑定**方法**的类型参数）+ `CallResultType`；`ArgTypeNode` 的
  call/await 分支与 `AwaitCallTy` 都走它。宿主还有一条：接收者的实例化
  决定成员类型（`Pool<string>.item` 是 `string` 不是 `T`）。
- 通用教训：**"声明的返回类型"和"这次调用的返回类型"是两回事**——凡是
  返回类型里出现类型参数的地方，都要问"这次调用把它代入成什么了"。
## 95. 方法签名含**类型参数个数**（泛型重载与普通重载并存）

- 坑（2026-10-21 实测）：`b.GetAsync<User>("k")` 与 `b.GetAsync("k")`
  指向同一对重载（`GetAsync(string)` 与 `GetAsync<T>(string)`），自举的
  候选去重按**参数类型标签**（`ParamTags`）判"同签名"，于是泛型那个被当
  成重复候选丢掉 → 显式类型实参选不到它。C# 的方法签名含类型参数的
  **元数**，`M<T>(string)` 与 `M(string)` 是两个签名。
- 做法：去重改用 `SigTags`（类型参数个数 + 参数类型标签）；显式类型实参
  另外按宿主的规则筛候选（`checker.c:1681`：候选的类型参数个数与调用点
  的类型实参个数不同的**不是候选**），`AwaitTarget` 也要同一条。
- 通用教训：**"重载"要按语言定义的签名判，不是按参数列表判**；参数
  列表相同但类型参数元数不同的两个方法，在 C# 里是两个重载。
## 96. `Task.Cancel` = 帧上的取消标志 + resume 时中止

- 坑（2026-10-21 实测）：自举对 `Task.Cancel(handle)` 报 "not supported"，
  根因是自举把任务句柄降级成**自己的扁平帧**（句柄就是帧指针），而帧头
  里没有取消标志这一格。宿主在协程帧头里有 `ASYNC_FRAME_CANCEL`，取消
  时置位（`src/compiler/irgen_async.c` 的 `get_co_cancel_fn`），帧在
  **下一次 resume** 时检查它并走完成路径（`emit_async_cancel_check`）。
- 做法：自举的帧头扩一格（槽 10 = cancel，帧体整体后移一格）；帧的 resume
  入口检查该标志，置位则**不跑函数体**，直接释放帧自己拥有的引用、标记
  完成、唤醒 awaiter（帧本身由 awaiter 回收，所以取消路径不能自己
  `__zan_co_frame_free`）。
- 通用教训：**"取消"是协作式的**——置标志 + 在恢复点检查，而不是立刻
  杀线程；标志放在**帧**上（不是全局表）才能随帧一起被复用与回收。
## 97. async 跨帧异常：longjmp 的目标帧可能已经死了（已知缺口）

- 坑（2026-10-21 实测）：`await s.Thrown()`（被 await 的 async 方法里
  `throw`）在自举里**堆损坏**（0xC0000374），宿主正常。原因：自举的
  异常传播是 `longjmp` 到**外层** try 的 setjmp 缓冲，而那个 setjmp 是在
  awaiter **上一次** resume 调用里下的——那次调用在挂起时已经返回，栈帧
  死了，longjmp 恢复到一段已复用的栈上。
- 宿主的两条机制（`src/compiler/irgen_async.c`）：异常**经帧传递**（子任务
  的异常存在帧里，awaiter 恢复后在自己的帧里重新抛出），且**每次 resume
  都重新挂载**处理器（帧的处理器栈 + resume 入口的 re-arm 段）。
- 通用教训：**协程里的 setjmp 目标必须是"还活着"的栈帧**——要么把异常
  变成帧上的数据（由恢复方在自己的栈里重抛），要么在每次恢复时重新
  下 setjmp；只把 jmp_buf 放进全局数组**不够**（缓冲活着，里面存的
  rsp/rip 仍然指向死栈）。
## 98. 取消要沿"等待关系"下传（自举与宿主的已知差异）

- 坑（2026-10-21 实测）：宿主的 `__zan_co_cancel` 不止置目标帧的
  `ASYNC_FRAME_CANCEL`，还沿帧头的 `ASYNC_FRAME_CHILD` 链把**后代帧**
  一起置位；自举的 `Task.Cancel` 只置目标帧的槽 10——父帧没有"当前正在
  等的那个子帧"这一格可循。
- 现状：自举的帧是"每个 await 点一个子帧槽"，父帧不保存"当前子帧"，
  因此取消无法沿链下传：被取消的任务若正卡在一次 `await` 上，要等它被
  恢复时才会看到标志。
- 通用教训：**"取消"在协程里是沿"等待关系"传播的**，只置目标帧等于只取消
  了"自己那一层"；要补齐得先有"当前子帧"指针（父帧记住正在等的子帧）。
## 99. reject 用例的"钉子"会随实现推进失效——要改钉，不是删用例

- 坑（2026-10-21 实测）：`tests/selfhost/reject_cases.txt` 里的
  `unsupported_builtin_call` 拿 `Task.Cancel` 当"自举还没实现的 C 宿主
  内建"的钉子；自举把 `Task.Cancel` 实现之后，这条用例反而**编译通过了**
  → 差分门判 "the self-hosted compiler accepted a program it must reject"。
- 做法：实现一个内建前先看它是不是别处的钉子；是的话把钉子**改钉**到
  另一个仍未实现的内建（同一个"仍不支持"诊断措辞），而不是删用例或放弃
  实现。钉子的作用是守住 fail-closed 边界，边界本身不能消失。
  顺带把"未支持的 `Task.X` 静态成员"做成一条干净的诊断分支（不要再往下
  走接收者求值——`Task` 是类型不是值，只会多出一条误导性的错误）。
- 通用教训：**测试用例钉住的是"边界"，不是"某个具体函数名"**——实现推进
  时边界要跟着搬家，否则会出现"实现变好 → 门变红"的反向激励。
## 100. struct 有隐式无参构造器（宿主对 struct 跳过构造器检查）

- 坑（2026-10-21 实测）：`new Matrix4x4()`（`Matrix4x4` 是 **struct**，只有
  一个 16 个 float 的构造器）在自举里报 "no constructor of 'Matrix4x4' takes 0
  argument(s); a class that declares constructors has no implicit parameterless
  one"，宿主却编译通过。根因：宿主检查器的构造器可用性检查第一句就是
  `if (type->kind != TYPE_CLASS || !type->sym) return;`
  （`src/compiler/checker.c: check_ctor_available`）——**struct 根本不做这个
  检查**，于是 `new Vector3("x")`（1 个 string 传给 3 个 float 的构造器）在
  宿主也合法，产物里就是一个全零的 struct。
- 做法：自举在 `new` 的降级里，当没有构造器匹配时，若目标是 **struct** 就不
  报错——按"隐式无参构造器"处理（实例已经分配好且已清零，字段初始化器照跑）。
  "class 声明了构造器就没有隐式无参构造器"这条规则只对 class 成立。
- 通用教训：**"隐式无参构造器"是 class 与 struct 的分水岭**——照抄别家的
  报错文案时，要先确认那条规则的对象是"类型"还是"类"。
## 101. 浮点字面量的 `f` 后缀决定类型（宿主 lit_suffix ? float : double）

- 坑（2026-10-21 实测）：`Vector4.Multiply(a, 2.0f)` 在自举里选中了
  `Multiply(Vector4, Vector4)` 重载，把 `2.0f` 当成一个 Vector4 传下去 →
  clang 报 "floating point constant invalid for type"。根因：自举的词法器把
  `f/F/m/M` 后缀**吃掉**了（只留在文本之外），于是 `2.0f` 与 `2.0` 在类型
  眼里都是 double；候选里只有 `Multiply(Vector4, float)` 的形参是 float，
  精确匹配拿不到分，两个候选打平 → 取第一个。
- 做法：词法器把浮点后缀记进 token（`litSuffix`：1=f/F，2=m/M），解析器放进
  节点的 `suffix`，**后端的实参定型**（`ArgTypeNode` → `FloatLitType`）按后缀
  给 "float" 或 "double"。
- 通用教训：**后缀不是装饰**——它决定字面量的类型，而类型决定重载选择；
  词法器"顺手吃掉"后缀时，后面所有按类型选候选的逻辑都会悄悄偏一格。
## 102. float 与 double 互相可隐式转换：重载评分不算"不匹配"

- 坑（2026-10-21 实测）：`Foo(double)` 与 `Foo(float)` 两个重载，传 `2.0f`
  时若按"名字不同即不匹配"扣分，float 那个重载会被判成不可用。
- 做法：`ScoreCand` 里 float/double 这一对不再扣分（只是拿不到精确匹配的
  加分）——宿主允许 float 实参进 double 形参（加宽），也允许反向。
- 通用教训：**"不匹配"和"不是精确匹配"是两件事**：能隐式转换的类型对不该
  被判出局，只是排序靠后。
## 103. `await` 有三种形态：协程帧 / 任务句柄 / 普通值透传

- 坑（2026-10-21 实测）：`await sqlite3_step(stmt)`（一个返回 int 的
  extern 调用）在自举里报 "'await' expects an async call in the self-hosted
  compiler -- this operand is not lowered as a coroutine frame"，宿主却编译
  通过。宿主的三种形态在 `src/compiler/irgen_expr.c: emit_expr_await_expr`：
  ① 被 await 的调用开了一个**协程帧** → 挂起等它完成；② 被 await 的是
  **任务句柄** → 轮询到完成再取结果；③ **其它任何值** → 函数的最后一句是
  `return sub`，值原样透传（等于"await 了但没等待什么"）。
  自举原来只认第 ① 种。
- 做法：`EmitAwait` 分流——不是协程帧时，Task 句柄走"泵调度器到 done 再读
  结果槽 6 并回收帧"（与 `t.Result` 同一套协议），其它类型直接透传。
- 通用教训：**"await"不是"必须等"**——它先是一种**类型规则**（Task<T> → T），
  再是一种**降级**（帧 / 句柄 / 透传）；只实现其中一种就会把合法程序判死。

## 104. 实例方法组要绑定接收者（`obj.Method` 当委托值）

- 坑（2026-10-21 实测）：`Action<int> f = probe.Probe;`（把实例方法当委托
  值传出去）在自举里报 "member 'Probe' of type 'Probe' is not supported"，宿主
  却编译通过。宿主的 `emit_method_group_closure`
  （`src/compiler/irgen_expr.c`）把方法组变成一条**带接收者的闭包记录**：
  `{fn, dtor, target}`——fn 是一个从 env 槽 0 取接收者的 thunk。
- 做法：自举生成 thunk `@__mg_<target>`：从 env 槽 0 读接收者，调目标方法，
  返回值按方法返回类型转换。ARC 类的接收者在 thunk 内部 zretain/zrelease
  （自举的闭包记录不会释放成员，所以释放责任落在 thunk 上）。
- 通用教训：**"方法组"不是一个方法指针**——它是一段"等接收者到齐才调用"的
  代码；只把函数地址塞进闭包、丢了接收者，调用时会拿随机值当 `this`。

## 105. `nint` 是关键字：词法、语法、类型名三处都要有

- 坑（2026-10-21 实测）：`(nint)-1` 在自举报 "unresolved identifier
  'nint'"，宿主编译通过。宿主有 `TK_NINT`（`src/compiler/lexer.c`），绑定器
  也认识 `nint` 这个类型名（`binder.c` 的 `type_nint`）；自举的绑定器早就能
  解析 `nint` 这个类型名，缺的是**词法与语法**把它当类型关键字。
- 做法：`token.zan` 加 `static int Nint()`；`lexer.zan` 的关键字表加
  `if (s == "nint") { return TK.Nint(); }`；`parser.zan` 的 `IsTypeKeyword`
  / `TypeKeywordName` 加上 Nint → "nint"（注意 `decimal` 在
  `TypeKeywordName` 里映射成 "double"，`nint` 映射成 "nint"）。
- 通用教训：**类型名认识 ≠ 类型能用**——词法、语法、绑定三张表要一起补；
  只补绑定器，最先炸的是"把它当表达式用的地方"（强制转换）。

## 106. 数组级优先于元素名：`List<string>[]` 的元素是 `List<string>`

- 坑（2026-10-21 实测）：`List<string>[] a; a[0].Add("x");` 在自举把
  `a[0]` 定型成 **string**（报 "string method 'Add' is not supported"），
  宿主正常。两处根因：① 检查器 `InferType` 的 `AK.Index` 分支先判
  `sval == "List"` 就返回类型实参（`kids[0]`），没先看数组级——`List<string>[]`
  的类型节点是 `sval="List"` + `kids=[string]` + `ival=1`，于是命中了 List
  特例。② 后端 `IsListType` / `IsDictType` 只看 `sval` 不看 `ival`，
  `List<string>[]` 被当成 List 走 `zlist_get`；`ArrayElemType` / `GenNew` 的
  数组分支还把类型实参（`kids`）丢了。
- 做法：**数组级（`ival`）先判**——`ival >= 1` 就是数组，元素类型是
  `sval` + `kids`（类型实参）+ `ival - 1`；`IsListType` / `IsDictType` /
  `IsSbType` 与 `IsStr` / `IsSpanType` 一样要先排除 `ival >= 1`；派生元素
  类型时把 `kids` 一起带走。
- 通用教训：**类型节点上的"名字"只是类型的一部分**——名字后面还挂着数组级、
  可空标志和类型实参；凡是"从类型 A 推出类型 B"的地方，只抄名字就会丢信息。

## 107. 裸 `throw;` 是重抛

- 坑（2026-10-21 实测）：`catch { ...; throw; }`（把捕获的异常原样再抛）
  在自举里被降成 `bitcast i64 0 to i8*`——clang 报 "invalid cast opcode for
  cast from 'i64' to 'ptr'"。自举原来只有 `throw <expr>`，裸 `throw;` 的
  表达式是 null，被当成"抛 0"处理。
- 做法：后端在 catch 子句体里把当时的 `@__exc_val` / `@__exc_tid` 存进
  自己的槽（每个子句一份），裸 `throw;` 重新写回这两个全局再 `EmitRaise()`；
  检查器加 `catchDepth`，在 catch 块之外用裸 `throw;` 报宿主同款诊断
  "a rethrow (`throw;`) is only valid inside a catch block"。
- 通用教训：**"重抛"不是"抛 null"**——它重抛的是**在飞的那一个异常**；
  子句体里可能又跑过嵌套 try（在飞的全局已被覆盖），所以要按子句各存一份。

## 108. `await` 的隐藏内建：`Socket.ResolveSockAddr` / `Socket.ResolveAsync`

- 坑（2026-10-21 实测）：标准库的 `Socket.BuildSockAddrAsync` 里
  `await Socket.ResolveSockAddr(ip, port, addr, 28)` 在自举里编译出对
  未定义函数 `@Socket_ResolveSockAddr` 的调用；`Socket.ResolveAsync` 同理。
  这两个不是普通方法：宿主在 `emit_expr_await_expr` 里把它们**识别成
  编译器内建**（`src/compiler/irgen_expr.c`），降级成
  `zan_io_resolve_sa_co` / `zan_io_resolve_co` 的挂起调用；标准库自己写的
  `public static async int ResolveAsync(string hostname) { return await
  Socket.ResolveAsync(hostname); }` 就是靠这条内建才不递归。
- 做法：自举的 `IsSocketIntrinsic` 加上这两个名字，`EmitSocketIntrinsic`
  按运行时契约发调用（`zan_io_resolve_sa_co(name, port, buf, cap, frame,
  resume, out)`，结果长度写在帧的结果槽 6 上，是个 **i32**），并在
  `EmitAsyncRuntime` 里补上对应的 `declare`。
- 通用教训：**"标准库自己调用的方法"往往不是普通方法**——宿主可能把它当
  编译器内建；自举补齐内建时，要按运行时的**真实契约**（参数顺序、结果
  槽位宽度）来，不能照抄别处内建的形状。
## 109. 插值的格式说明符要走 printf 语义（`x.ToString("D4")`）

- 坑（2026-10-21 实测）：`$"{v:D4}"` 在自举里报 "call to 'v.ToString' is
  not supported by the self-hosted compiler yet"——自举的词法器把插值洞
  `{v:D4}` 拼成 `v.ToString("D4")`，而 irgen 后端只认**无参**的
  `ToString`。宿主则把说明符映射成 printf 模式
  （`src/compiler/irgen_expr.c: interp_format_to_printf`）：D/X 要位数
  （位数 ≤ 0 或 > 512 → 不套格式）、F/E 把位数夹到 512、G 是"有效位数"、
  `0`/`#` 是自定义模式（点前 0 的个数 = 整数补零，点后 0/# 的个数 = 小数位）。
- 做法：irgen 后端补 `FormatSpecToPrintf`（与 ngen 后端同名函数同规则）+
  `SpecWantsDouble` + `EmitScalarToStringFmt`（snprintf 两段式：先问长度，
  再写进 `zstr_new` 的缓冲）；`ToString(<字符串字面量>)` 在标量接收者上
  走这条路。**注意 `x` 小写要映射成 `llx`**（宿主按大小写给 `llX`/`llx`），
  ngen 后端写死 `llX` 是错的。
- 通用教训：**"格式化"不是一个字符串，是一个语义表**——同一个洞里的
  `D4`/`X2`/`F1`/`G3`/`E2`/`0.00` 走的是六条不同的规则；照抄"看起来像"的
  映射表会在大小写、位数边界上出错。

## 110. 插值里的 `{{` / `}}` 是转义花括号

- 坑（2026-10-21 实测）：`$"{{{n}}}"` 在自举里报 "syntax error: unexpected
  ':'"——自举的词法器（`lexer.zan` 的 `InterpStr`）见到 `{` 就开一个洞，
  不认 `{{` 是转义的左花括号，于是把 `{n` 当成洞的内容。
  宿主在 `lexer_interp_string_segment` 里明确：`{{` 是一个字面 `{`，
  `}}` 是一个字面 `}`，单个 `}` 是字面 `}`。
- 做法：自举的 `InterpStr` 在文本段里遇到 `{{` / `}}` 时，把前面的文本
  切一段、补一个字面花括号段，再继续。
- 通用教训：**"分隔符"要先问有没有转义形式**——C# 的插值里 `{` 有两种
  含义（洞的开始 / 字面花括号），词法器不先处理转义，后面的语法分析
  看到的就是完全不同的东西。

## 111. 自举的插值是 `+` 链，bool/nullable 空洞因此对不上（已知缺口）

- 坑（2026-10-21 实测）：`$"{flag}"`（bool）在宿主输出 "true"，在自举输出
  "1"；`$"{u}"`（`ulong?`）在宿主输出载荷或空串，在自举直接生成坏 IR
  （把可空槽当 i64 喂给 snprintf）。根因：自举的词法器把插值**降成
  `+` 拼接链**，于是空洞和普通拼接操作数无法区分——而宿主的
  `emit_expr_string_interp` 是逐段判断的（bool → "true"/"false"，
  char → 字符本身，可空 → 载荷或 ""，其余按类型）。
- 做法（未做）：自举要有独立的插值节点（宿主是 `AST_STRING_INTERP`），
  词法/语法/检查器/两个后端都要认它；检查器还得跟宿主一样拒绝插值类
  实例（"cannot interpolate 'Box' -- convert the operand explicitly
  instead of formatting the reference"）。
- 通用教训：**"降级成更简单的表达式"会丢掉语义**——`+` 链能表达拼接，
  表达不了"这一段按 bool 渲染"；凡是宿主有专门节点的地方，自举抄成
  通用表达式就会在边角上分叉。

## 112. `%E` 的指数位数是 C 运行时的差异，不是编译器的差异

- 坑（2026-10-21 实测）：`$"{f:E2}"` 在宿主输出 "3.14E+000"（指数三位），
  在自举产物里输出 "3.14E+00"（指数两位）。两边发射的格式串都是
  `%.2E`，差别在**链接时的 C 运行时**：宿主 zanc 的链接行是
  `-lmingw32 -lgcc -lmoldname -lmingwex -lmsvcrt`（`src/compiler/main.c`），
  产物走 `msvcrt.dll`（老 CRT，指数至少三位）；用 VS clang 链出来的产物
  走 UCRT（指数至少两位）。
- 做法：这是**链接层面**的差异，编译器两边一致；对拍要么让两侧用同一个
  C 运行时，要么把它记成环境差异（差分门的 84 个用例都不含 `%E`）。
- 通用教训：**"逐字一致"要区分"编译器的语义"和"工具链的环境"**——
  printf 家族的细节（指数位数、`%g` 的尾零）由 C 运行时决定，链接器换了
  运行时，输出就变；先确认差异出在哪一层，再决定改哪边。
## 113. extern（DllImport）实参要传原始缓冲：byte[] 不是字符串

- 坑（2026-10-21 实测）：自举编译的程序一碰 SQLite 就崩（0xC0000005），
  而 stdout 因为块缓冲全丢，看起来像"跑到退出时才崩"——实际是崩在
  `SqliteConnection.Open` 里。根因：自举把 `byte[]` 实参**复制成一个
  新的托管字符串**（`zstr_new` + 逐字节拷贝）再交给 C，宿主却是
  **原样传指针**（`src/compiler/irgen_arc.c: coerce_args_to_params`
  只对指针类型做 bitcast，从不复制）。于是
  `sqlite3_open(filename, ppDb)` 把 8 字节的连接句柄写进了那个临时
  字符串的载荷区（压在字符串头上），程序自己的缓冲区仍然是空的，
  字符串头也被写坏——后面一释放就崩。
- 做法：自举的 `CallArg` 加 `rawStr` 开关，extern 被调方**不做
  byte[]→string 打包**，直接传数组的数据指针；`Coerce` 补上通用的
  指针→指针 bitcast（数组是 i64*，C 的 `char*` 是 i8*）。自举的
  `StrSlotConv`（赋值/返回时的打包）保持不变——那是托管语义。
- 剩余差异（已知）：宿主把 byte[] **别名**成字符串（同一个指针，
  `strbuf` 认得数组头，长度取元素个数），自举是**复制**一份；所以
  "赋值之后再改数组"在宿主会看到新值（`after: ZBC`），在自举还是旧值
  （`after: ABC`）。要对齐得让自举的字符串运行时支持"数组视图"。
- 通用教训：**托管侧的"隐式转换"到了 ABI 边界必须退回零拷贝指针**。
  byte[]→string 在托管语言里通常是拷贝，但 C 的出参缓冲区是**给 C
  写的**（句柄、长度、指针都往里塞），一复制就等于把结果写进临时
  对象。判据：实参那块内存**会不会被 C 侧写**——会，就不能复制。
## 114. 宿主把 `Math.PI` 和 `Cpu.*` 当内建：自举照抄，连宿主的"恒 0"一起照抄

- 坑（2026-10-21 实测）：`packages/Zan.Gui` 里的 `Math.PI` 和
  `stdlib` 里的 `Cpu.HasSse42` 让自举**直接拒绝编译**两个用例
  （"Math is a built-in type and cannot be used as a value" +
  "unresolved identifier 'Cpu'"）。宿主这边：
  - `Math.PI` 被折成**编译期 double 常量**（`src/compiler/irgen_expr.c:6783`，
    3.14159265358979323846），压根不走成员查找；
  - `Cpu.HasXxx` / `Cpu.CpuFeature(id)` 被降成编译器内部的
    `__zan_cpu_feature()`（`irgen_expr.c:1488` 调用、`irgen_expr.c:6651`
    属性），**完全绕开 stdlib 的 `System.Runtime.Intrinsics.Cpu` 类**。
- 关键事实：原生构建时 `g->target_triple` 是**空串**（只有 `--target`
  交叉编译才填，`src/compiler/irgen.c:1568`），而 `cpu_feature_fn` 用
  `strstr(triple, "x86")` 判架构——空串既不是 x86 也不是 arm → 走
  "其它平台"回退 → 发射出来的 IR 就是
  `define internal i32 @__zan_cpu_feature(i32) { ret i32 0 }`。
  于是宿主在**任何机器上**都报"没有 popcnt / sse42 / avx2"，
  `Cpu.CpuFeature(n)` 恒为 0。同一文件里的 `emit_target_is_x86`
  反而正确处理了空串——两个函数对空串的判断不一致，宿主选了保守的那个。
- 做法：自举照抄这个行为——`Math.PI` 折成同一个 double（打印出来是
  `3.141592653589793`），`Cpu.HasXxx` 折成 `false`，
  `Cpu.CpuFeature(n)` 折成 `0`。**不要**去问真正的 CPU：自举若走 stdlib 的
  `zan_cpu_feature` 会拿到真实值（1），和宿主对不上。
- 通用教训：**"宿主是规格"包括宿主的 bug**。自举对齐的目标是"两边输出
  逐字相同"，不是"两边都正确"。遇到宿主明显不合常理的行为（空 triple →
  恒 0），先确认它确实是当前宿主的行为，再照抄，并在注释和 skill 里
  写明"这是宿主的保守回退"，免得后人当成自举的缺陷去"修"。
## 115. 字符串实参可以是 char；span 的 float 要显式窄化

- 坑（2026-10-21 实测）：`s.Split(',')` 在自举里发出
  `call i8* @zstr_split(i8* %s, i8* 10)`——clang 直接报
  "integer constant must have integer type"。根因：宿主把"能当字符串用的
  实参"抽象成 `is_string_like_expr`（`src/compiler/irgen_call.c:536`：
  是字符串**或** char），char 先被 `emit_char_to_cstr` 变成一个字符的
  文本；自举只认字符串，把 char 的整数值直接塞进了 `i8*` 形参。
  同样要照顾 char 的还有 Contains / IndexOf / LastIndexOf / Equals /
  StartsWith / EndsWith / Replace（Replace 的两个实参都算）。
- 做法：自举加 `StrArg(e)`——实参类型是 char 时用 `CharToStr` 打包成
  一个字符的托管字符串（作为语句临时值在语句末尾释放），其余原样。
- 顺带修的第二个坑：**span 的元素宽度和值的宽度不是一回事**。span 的
  float 元素在 IR 里是真的 32 位 `float`，而自举的 float 值一律是
  64 位 `double`（`MapType` 把 float 映成 double），所以往 `Span<float>`
  里写必须先 `fptrunc`。原来的判断写的是 `v.ty.sval == "double"`，可值
  本身也可能是 `float`（比如从 `float[]` 里读出来的元素），于是漏掉了
  窄化，clang 报 "'%t33' defined with type 'double' but expected 'float'"。
  判据要改成"这个值是不是浮点值"（`IsDbl`），不是"类型名恰好是 double"。
- 通用教训：**同一个类型名，在"值"和"存储"两层宽度可能不同**。自举的
  模型是"float 即 double"，凡是遇到真实的 32 位存储（span 元素、原生
  内存、FFI 结构体），写入前必须显式窄化；而且判断依据要用"类别"
  （浮点/整数），不要用"类型名相等"——值的声明类型可能是 float 而它的
  LLVM 形状是 double。

## 116. LLVM 的 `null` 实参必须带类型——裸 `null` 不是实参

- 坑（2026-10-21 实测，A484）：自举拼出来的 IR 里出现了裸 `null` 当调用实参，
  clang 报 "expected type"：

  ```
  %p = call i8* @zalloc(i64 24, i8* bitcast (void (i8*)* @zsb_dtor to i8*), null)
  ```

  根因：自举 irgen 的 `ContainerCid()`（`src/selfhost/irgen.zan`）在"这个类型
  不是内建容器"时返回**裸 `null`**，直接拼进 `@zalloc(size, dtor, cid)` 的第三个
  实参。LLVM 的调用实参必须带类型（`i8* null`）；裸 `null` 只在少数常量位置
  合法。
  - 触发条件很隐蔽：**用户自定义了与内建容器同名的类**（`class StringBuilder` /
    `class List` / `class Dict`）→ `IsContainerType()` 判 false → 走 null 分支。
    普通程序一切正常，差分门 85 个用例一个都没撞上。
- 做法：`ContainerCid()` 的 null 分支返回 `"i8* null"`。
- 通用教训：**自举拼 IR 文本时，任何"空指针"都要带类型**。宿主侧的 IR 一直是
  `i8* null`（参数类型是已知的），自举把参数类型藏进字符串拼接里就容易把
  类型丢掉——而拼出来的 IR 只能靠 clang/llvm-as 验证，肉眼看 `null` 完全合理，
  看不出问题。

## 117. 通用可赋值性检查：先量矩阵，再照着写

- 坑（2026-10-21 实测，A477）：自举的检查器**没有**宿主那种通用的隐式转换检查——
  `int y = "hello";` 在自举里编译通过（IR 照写），宿主报
  `cannot convert 'string' to 'int' in initializer: no implicit conversion`。
  自举原来只有两条 Task 专属规则（`CheckTaskNoResult` / `CheckTaskOnlyHandle`）。
- 做法：
  - **先量，后写**：12 个目标类型 × 17 个值 = 204 组矩阵探针（分块编译，绕开宿主
    100 条诊断的上限），把宿主的判定表**量出来**再照着实现。凭直觉写必错——
    "简单名 vs 限定名"这类区分全被推翻；真正决定判不判的是「目标是不是原始类型」
    +「值是不是已知类型」两件事，而"可空值只能进同种可空槽"这条规则是从矩阵里
    长出来的。
  - `PrimCat()` 把类型名分成 0 其他 / 1 整型 / 2 浮点 / 3 bool / 4 string /
    5 char / 6 枚举 / 7 类实例；`CheckAssignable()` 按这张表判。
  - **宁可漏报，不可误报**：误报会让自举编不过自己的源码、也会拒掉合法程序；
    漏报只是留一条台账。所以三类刻意不判——`double`/`float` 进整型槽（那是宿主
    的**窄化**规则，另一条诊断）、`object` 目标（Zan 的 `object` 不是万物基类）、
    用户类目标（宿主靠类层次判定，自举没有层次）。
  - 字面量的类型也要按后缀分：`5L` long、`5u` uint、`5UL` ulong、`5.0f` float、
    `5.0m` decimal、`0x1FL` long（解析器把进制折进同一个字段，类型后缀取
    `suffix % 8`）——宿主这么分，诊断里点的类型名才一致。
  - 措辞逐字对齐宿主：`cannot convert 'X' to 'Y' in initializer: no implicit
    conversion` / `... in return statement: ...`；`null` 字面量在宿主那里类型是
    `object`，所以诊断里写的是 `'object'` 而不是 `'null'`。
- 通用教训：**两个后端要"逐字一致"的规则，先把对面的行为量成表**。矩阵比对给出的
  三个数（误报 / 漏报 / 措辞差）比任何单条探针都有说服力——而且漏报的每一格都要
  能说清"为什么这次不判"，那才是能进台账的结论。

## 118. 向量层：宿主的每一个运算都在"字节道"上

- 坑（2026-10-21 实测）：自举把 `Vector128`/`Vector256` 当成"两个 64 位槽"来算，
  宿主却是**在字节道上定义每一个运算**（`src/compiler/irgen_expr.c:1995-2436`）：
  值是 `{ i64, i64 }` 结构，运算前转成 `<16 x i8>`（256 位是 `<32 x i8>`），算完再
  转回来。于是自举的 `AddSaturate` / `Average` / `Equals` / `Unpack` / `Shuffle` /
  `ConditionalSelect` / `ExtractMostSignificantBits` 逐字节对不上；`Multiply` /
  `AddFloat` / `SubtractFloat` / `Sqrt` / `MultiplyAdd` 走的又是 `<4 x float>` 另一条道。
- 照抄宿主时踩到的五个具体的坑：
  - `Shuffle`（pshufb）的可移植实现是**逐字节查表**：掩码字节最高位为 1 → 该道清零，
    否则取 `a[掩码 & 0x0F]`（宿主 `emit_pshufb_portable`，`irgen_expr.c:1951`）。
  - `Average`（pavgb）是 `(a&b) + ((a^b)>>1) + ((a^b)&1)`——**向量加法逐道回绕**，
    道与道之间不进位，所以这三步就够了。我们多加了一次 `odd`，答案从 150 变成 0。
  - `ConditionalSelect` 是 `(c&l) | (~c&r)`，取反的掩码是 **0xFF**，不是 1——用 1 会
    得到 `0xFE & r`，答案差一位（42 变成 106）。
  - `UnpackLow/High` 用的是 `shufflevector` **指令**（掩码是 `<16 x i32>` 常量），不是
    intrinsic。`llvm.shufflevector.v16i8.v16i8` 这个 intrinsic 根本不存在，链接期才
    报 `use of undefined value`——IR 文本看着完全合理。
  - `Create(float)` 要先 `fptrunc` 到 float 再进 float 道；自举的 float 值是 double，
    不窄化就发出 `insertelement <4 x float> ..., float %double` 的坏 IR。
  - `Prefetch` 发 `llvm.prefetch.p0`（指针 + 三个 i32 参数），不是调用一个同名函数。
- 通用教训：**照抄宿主的"道"**。宿主把值拆到哪条道上运算，自举必须拆到同一条道；
  先把宿主那段代码（`emit_vector128_call`）读完再动手，别照着 API 名字猜语义。

## 119. 窄化的时机、重载的选择、裸 return：都是宿主行为的一部分

- 坑（2026-10-21 实测），三条"宿主有、自举没有"的规则：
  1. 裸 `return;` 在**非 void** 函数里返回**结果类型的零值**
     （`src/compiler/irgen_stmt.c:1739-1742`），不是 `ret void`。自举一律发 `ret void`，
     生成的 IR 过不了 LLVM 校验（`value doesn't match function result type`）。
  2. `Convert.ToInt32` **不做** 32 位窄化：宿主用 strtoll 解析文本、fptosi 截断浮点、
     整数原样返回（`src/compiler/irgen_call.c:1937-1981`），**窄化发生在值进入有类型的
     位置时**（int 槽是 i32），不在调用里。所以 `Console.WriteLine(Convert.ToInt32(v))`
     打的是完整的 64 位值，而 `int x = Convert.ToInt32(v);` 打的是窄化后的 32 位值。
  3. 自举检查器的重载选择原来只按**名字**取第一个：`Description()` 会撞上
     `Description(string desc)`，`OnMessage(message)` 会撞上 `OnMessage(MessageHandler cb)`
     ——推出来的返回类型是另一个重载的，检查器据此报错（宿主与后端都按**实参类型**
     打分选重载，见后端的 `ScoreCand`）。自举检查器也要按实参类型打分：精确匹配 +4、
     已知不匹配 -1、未知中性。
  4. 顺带：可赋值性检查要认**用户自定义的隐式转换**（`implicit operator T`，解析器
     落成 `op_implicit` 方法），否则 `double d = feet;` 会被误判成不能转换。
- 通用教训：**"逐字一致"不只是诊断措辞**。同一个表达式的**求值时机**（在哪一步窄化、
  谁来选重载、裸 return 返回什么）同样是宿主行为的一部分；照抄时要把宿主那段代码
  读到底，再决定自举在哪一步做这件事。

## 120. 窄化是独立的一条规则：浮点进整型槽、常量越界，措辞还不一样

- 坑（2026-10-21 实测，A485）：宿主判可赋值性时**先**判窄化，窄化有自己的措辞：
  `narrowing conversion from 'X' to 'Y' in <what> needs an explicit cast (C# rules)`
  ——和通用的 `cannot convert 'X' to 'Y' in <what>: no implicit conversion` 是两条
  不同的诊断。自举只学了后者，前者整条缺失。
- 把宿主的判定表量出来（98 组矩阵 + 边界探针），三条结论凭直觉写不出来：
  1. **触发形状只有两个**：浮点值进整型槽（char 槽、枚举槽也算整型槽）、
     整型**常量**越界（`byte b = 300` 报、`byte b = 200` 不报）；
  2. **可空槽不窄化**（`int? x = 5.0` 宿主一声不吭）、**非常量不窄化**
     （`byte b = someLong` 也不报）——只有字面量常量才查范围；
  3. **`decimal` 不是浮点**：宿主有独立的 decimal 类型，`int v = 5.0m` 报的是
     *cannot convert*（不是窄化），`double` 与 `decimal` 互不隐式转换。
- 更贵的坑：**自举把 `decimal` 类型映射成 `double`**（`parser.zan`），AST 里
  `decimal` 目标与 `double` 目标分不出来。第一版按"能区分"实现，结果把
  `decimal d = 5.0m;`（合法程序）判成了错。
- 通用教训：**误报比漏报贵得多**——漏报只是放过一个该拒的程序，误报是拒绝一个
  该过的程序，误报还会让自举编不过自己的源码。当两侧**无法区分**时，宁可把两边
  都放过（记成台账缺口），也不要赌一边。
- 通用教训：**规则要照着"量出来的表"写**。判定表要先用矩阵探针量出来（宿主当
  规格），再照着实现；凭直觉写的规则集在第二版几乎必然要返工。
