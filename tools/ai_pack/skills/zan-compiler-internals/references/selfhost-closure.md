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

- 坑（实测）：宿主在程序入口调 `zan_co_sched_init`（`irgen_emit.c` 的
  `emit_main`），自举的程序入口没有这一句。照抄过来就以为"对齐了"——但自举
  产物里的 `zan_co_sched_init` 是**自举自己发的弱符号**（`irgen_async.zan` 的
  `EmitAsyncPumpReady`），而自举运行时对象**根本没有 `zan_co_*` 强符号**
  （`llvm-nm --defined-only` 查过），于是这句调用今天只重置自举自己的调度器
  状态：**结构对齐，行为没变**。等帧布局对齐到宿主的 `zan_co_fhdr` 之后链接
  完整运行时，强符号胜出，这句才真的装上 timer ready hook（`rt_io.c` 的
  `zan_co_sched_init`），把延迟帧重新入队而不是在定时器线程上直接恢复协程
  （`rt_timer.c` 的延迟派发）。
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
  协程帧头，而是降级到自举**自己**的扁平 i64 帧上：句柄 = 帧指针，
  done 标志 = 槽 1，结果 = 槽 4。宿主是同一套协议、另一套字节
  （`irgen_expr_core.c` 的 `emit_task_member`：test/pump 循环 → 读结果 →
  回收）。差分门比的是 **stdout 逐字**，不是帧布局——所以"布局没对齐"从来
  不是"降级做不了"的理由，只有**行为**对齐才是。
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

- 坑（实测）：自举的检查器把 **lambda 的函数体**按**外层方法**的返回类型查
  `return`——于是在 void 方法里写
  `Func<int> f = delegate () { return 5; };` 被报成
  "cannot return a value from a void method"，而 C 宿主**编译并运行**这个程序
  （输出 5）。这是**假阳性**：lambda 返回的是它自己的值，跟外层方法无关。
- 做法：lambda 的返回类型在自举侧**不知道**（检查器不从上下文给 lambda 定
  型），所以函数体里的 `return` **不查**（既不查"void 方法不能返回值"，也不
  查"必须返回值"）。实现上给检查器加一个 `curRetKnown` 标志：方法体为
  true，lambda 体为 false。
- 为什么值得单独记：这条假阳性会**挡掉**别的诊断对齐——`Task.Run(delegate ()
  { return 5; })` 在宿主侧报的是 Task.Run 专属的那条（"Task.Run delegate
  must be a parameterless void body (an Action); pass an async call for
  results"），自举侧却先被这条泛化诊断拦下，措辞对不上。**上游的检查器
  假阳性会掩盖下游的诊断对齐**，差分门看到的"措辞不同"往往根子在更早的
  一层。
- 教训：**"措辞不同"先查更早的层**。同一个程序，宿主报专属诊断、自举报泛化
  诊断时，先确认自举是不是在更早的检查里就拒了它。

## 37. 诊断对齐要连**位置**和**条数**一起对齐；类型解析照抄宿主的三步

- 坑（实测）：自举的类型诊断只对齐了**一部分**——未定义类型报
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

- 坑（实测）：自举的 parser 不做宿主那步定形，于是两个形状对不上：
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

- 坑（实测）：自举已经实现了宿主 Task 相关的**一条**赋值规则
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

- 坑（实测）：自举的 ngen（原生后端）分支在 `main.zan` 里**早于检查器**就
  return 了——于是一个用了未定义类型的程序，ngen 照样 rc=0 写出 66KB 的目标
  码。IR 路径（irgen）是查的，宿主也是查的，只有 ngen 不查。
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

- 坑（实测）：自举 IR 后端没有聚合值，`struct` 实例被表示成堆对象
  （`zalloc` + 字段偏移），于是**赋值 / 传参 / 入容器都只拷指针**——而 C 宿主
  把 struct 按值拷贝。同一个程序：宿主 `list.Add(p)` 后改 `p.x` 读出 `1`，
  自举读出 `99`；`P3 r = q; r.x = 42;` 宿主读 `q.x` 得 `3`，自举得 `42`。
- 教训：后端的**表示**可以不同（堆对象 vs LLVM 聚合值），**可观察语义**不能。
  表示换了，拷贝点就得**逐个补齐**：宿主在哪里按值拷，自举就在哪里显式拷内容。
  "以后改成聚合值就好了"不能作为现在语义不对的理由。
- 做法（`irgen_struct.zan`）：拷贝 = 分配新实例 + **逐字段**拷，
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
- 门禁：用例 `struct_value_copy.zan`（8 个形态）与差分门一起钉住。

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
- 门禁：新增 `selfhost_shipped_runtime`——自举产物链**发行版** mt 运行时
  （`zanrt_io_mt.o` + 捆绑 `mingw/lib`），输出与宿主逐字一致。发行版的
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
- 做法：自举 IR 后端的 `Find` 内建只保留 `rel = at - base0`，
  `base0` 是 p（不带 off）。
- 连带：`ByteBuffer.IndexOfByte(from, b)` 是 `Find(data, from, b, len-from)`，
  所以同一个错位会让"续找/最后一个/越界起点"三类断言一起挂。
- 门禁：`bytebuffer_indexof` 进差分门（逐字一致）。
## 59. 擦除的类类型参数：null 比较比的是 0，不是 null 指针

- 坑（实测）：自举把泛型类的类型参数擦除成 **i64 槽**，于是
  `val == null`（`val: T`）发出来的是 `icmp eq i64 %t1, null` —— LLVM 直接
  报 "null must be a pointer type"，链接都到不了（用例
  `generic_type_param_null_check` BAD-IR）。
- 宿主口径：宿主按实参把泛型类单态化，槽位是指针，比的是 null；自举的槽
  是 i64（指针的 ptrtoint 值），**0 就是 null**。
- 做法：自举 IR 后端的 null 比较与 `EmitNonNullVal` 都按值的 LLVM 类型选
  零：`IsPtrLL(ty)` 为真才用 `null`，否则用 `0`。
- 连带：同一个错位也会出现在 `x is null`（那里早就按类型分叉了）。

## 60. 自举检查器不得比宿主更严：宿主没有的诊断要删掉

- 坑（实测）：自举检查器有一条 "not all code paths return a value in 'X'"
  ——**宿主根本没有这条诊断**。于是方法体以 `lock (this) { ... }` 收尾的
  正常程序（连接池这类资源管理代码就是这么写的）被自举判成错误，整个
  packages 用例编译不过。
- 口径：**宿主是规格**。自举检查器多出来的诊断 = 自举拒收宿主接受的程序
  = 行为不一致；这种"更严"不是安全网，是偏差。
- 做法：删掉那条诊断（连同只服务于它的 `DefinitelyReturns`/`HasBreak`/
  `IsTrueLit`）。自举 IR 后端在方法尾部本来就会补默认返回（`ret i64 0` /
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