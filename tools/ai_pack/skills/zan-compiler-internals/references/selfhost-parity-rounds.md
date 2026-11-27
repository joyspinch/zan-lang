# 自举闭环专题（五）：能力对齐复核与调度/EH 专题（§149–§164 及后续轮次）

返回总索引：[selfhost-closure.md](selfhost-closure.md)

## 149. 自举审计按调用契约与当前证据归因

入口 wrapper 必须按实际 `Main` 声明区分 void/int 返回值和有无 `string[]` 形参，生成相同签名的调用并传递参数；不能给带参声明发无参 call。字面量 Format 的占位索引在十进制累加和 `idx + 1` 前都须验证范围；`2147483648` 会越过 int32，溢出后的负索引不能只靠上界比较拦截。根因已确定也须把“正在修复”与“已完成回归”分开记录。

自举 GC 的缺失导出先核对对象头、分配器、活对象注册表和链接闭包；`zan_gc_detect_cycles` / `zan_gc_write_heap_snapshot` 缺失不能靠盲链宿主 `rt_mem` 补齐，自举 ARC registry 与它不兼容。链接成功只证明符号齐全，不证明检测或快照能读取当前对象布局。

`Task.IsDone` 在**协程执行栈上**必须纯查询，在**同步上下文**才泵调度器（自举没有工作线程池，同步侧不泵则忙等挂死）。该缺陷已于 2026-11-15 修复：无条件泵改为按 `@__zan_co_depth` 分流，深度只在调度器执行 step 的那一处维护；回归由 conformance `task_isdone_pure_query`（协程内 `q=0 ranAfter=0`，旧实现 `q=1 ranAfter=1`）与 diff_cases 的 `async_when_all` / `task_run_action_handle` 两条分别钉住两个分支。修复后仍要独立的崩溃栈与 ABI 证据，才能把 `async_when_all` 的访问冲突归因到过度 pump 或运行时强弱符号混搭——目前没有，不得写成已证根因。

审计旧 corpus 先以当前宿主和 gen1 对同一输入、参数及运行时闭包重新取证，区分编译、链接、运行、输出与超时；历史行数和分类计数是语料快照，不能直接当当前缺陷数。宿主对照通过只排除该输入的宿主故障，gen1 根因及修复仍须独立验证。

## 150. 普通方法组共享目标签名契约与闭合泛型虚表桥接（B-ID207）

- 契约只保留一套：checker 与 IR 共用 `Binder.ResolveMethodGroup(receiver, name, form, target)`，返回 `MethodGroupBinding(method, ownerType, score)`；复用既有 delegate 参数/返回类型与类型替换能力。AST 独立 `methodGroupTarget` 只存期望 delegate 类型，不把某个泛型实例的候选方法写入共享声明。外层调用/构造的候选探测不得修改目标信息，胜出后才提交；克隆时同步替换该目标类型。
- 候选规则必须一致：参数个数、async 状态一致，方法自有类型参数与方法/目标 delegate 的 ref/out 均拒绝。参数按“delegate 实参 → 方法形参”、返回按“方法结果 → delegate 返回”检查；只允许精确匹配与支持的引用转换，不引入数值拓宽或装箱。基础分 +4，精确参数 +4、引用兼容 +1；同深度最高分歧义拒绝，等分更远基类不替换近层候选。
- 接收者与遮蔽先于方法候选：bare/this/value/type 共用静态性规则，静态上下文的 bare 名不能捕获实例方法。局部/参数即使类型未知也遮蔽同名方法；字段、属性和 getter 继续作为普通 delegate 值。嵌套字段接收者经 owner 替换后保留闭合泛型类型，不能退回声明中的裸类型参数。
- 目标传播覆盖字段初始化、对象初始化、条件表达式两臂、调用及构造参数；条件本身不继承 delegate 目标。字段初始化检查必须隔离并恢复当前方法、static 状态、返回类型、作用域和 lambda 深度，避免上一个方法的局部状态污染绑定。
- 实例方法组只求值接收者一次，复用 `OwnedEnvironment` / `OwnedClosure` 及既有临时值释放路径；闭合 owner 走现有具体 ABI/泛型符号路由。静态方法组保持原生函数指针，delegate 字段/局部保持普通借用值；不能为实现方法组另造一套 ARC 或泛型规则。
- 虚表槽身份按祖先实参替换后的签名匹配，槽 ABI 仍保留引入声明的形式。`Base<T>.Echo(T)` 与 `Child.Echo(double)` 不能因原始参数标签不同而裂成两个槽；`CollectVSlotNodes`、`VSlotIndexM`、`EmitVtable` 共用 `VSlotMatches`，实际实现与继承槽的 value ABI 不同时由 `EmitVSlotAdapter` 桥接擦除参数/返回值。方法组 thunk 仍从对象虚表取实现，不能直调具体符号代替动态 override。
- adapter 边界：未找到实现先做 null 守卫；同 ABI 的 ref/out 参数按指针原样透传，不能送入值参数 `CallArg` 转换；需要改变 ref/out 存储 ABI 时明确诊断并 fail-closed。async 返回保留 coroutine frame 的 `i8*` ABI。**这些 ref/out 与 async 分支本轮仅完成静态守卫，未分别通过运行用例验证，不能记为已验证支持。**
- 数组形状是签名的一部分：类型参数替换为数组时组合内外层 rank，类型显示保留完整维度。除精确匹配和转 object 等明确规则外，不能把数组当元素类沿基类链判断引用兼容。不同命名 delegate 可按结构比较；同名递归 delegate 先按普通类型形状处理，结构展开需有身份短路及深度上限，避免无限递归。
- 本轮证据（2026-11-15，主代理执行）：gen1 build rc=0、`CHANGED_INPUTS=0`；三个 positive（含 contexts）均 emit/llvm-as/link/run rc=0 且 golden 一致，三个 negative 分别准确拒绝 3/4/2 条。contexts 覆盖闭合泛型 virtual direct call 与 method-group call 命中真实 override，并包含同名递归 delegate。此结果不等于 host 全门通过；随后 host 侧递归 delegate 栈溢出已由 `irgen_type_map.c` 将委托值映射改为 opaque pointer 修复，contexts 双端复验通过（证据见 build/test-out/selfhost-method-group/RESULTS.md）。
- 未修边界必须保留：`generic_virtual_dispatch` 的历史 wrongstdout、类型判定/泛型多实例表 known gap 未在本轮解决；泛型接口继承转换也未扩展。不得删除失败断言、绕写自然语法，或以本轮 contexts 通过宣称泛型虚分派已全面覆盖；新增 adapter 模式仍需独立取证。

## 151. W2072 返回逃逸警告对齐与 Func/Action 内建合成遮蔽（B-ID208）

- W2072 谓词是**窄开关**（宿主 `checker_util.c` 的 `has_closure_capturing_this`）：剥 cast → 方法组绑 this → lambda 体 → 赋值右值 → 条件两臂 → 调用实参 → new 实参与初始化项，到此为止。`this.field[0]` 这类读取既有委托值的形状不构成新捕获，宿主不警告；自举旧谓词的 a/b/c/d/kids 泛递归会把读形状也扫成警告，已对齐（`checker_expr.zan` 的 `HasClosureCapturingThis`）。W2009 回挂环与 W2072 返回逃逸两个入口都只在类里触发（struct 不发；interface 以 ClassDecl+MOD.Iface 编码，同样排除）。
- 裸 `Action` 由内建按使用点 arity 合成，stdlib 只声明 `Action<T>`（arity 1）。`DelegateParams`/`DelegateRet` 对 Func/Action 名字必须跳过 `FindDelegate` 声明路径，否则裸 `Action` 会借到未替换的 `T`，0 参方法组被误判 E2098 "signature is not compatible"（`checker_type.zan` 的 arity 注释记录同一宿主契约：内建名先于任何作用域声明解析）。
- 自举驱动诊断默认模式会重复打印：checker 与 irgen 阶段各 `Report(src, 0)` 把 checker 的警告打两遍；第二遍必须从 checker 阶段结束的条目数起打（`driver/main.zan` 的 `reported` 标记）。自举诊断走 stdout、宿主走 stderr，跨端比对必须合并双流，单流 grep 会把"零警告"误读成奇偶。
- `--deny-warnings` 行为差异：宿主把警告升级为 E 前缀并按错误计数；自举保留 warning 行、以 "warnings denied by --deny-warnings" 拒绝编译——拒绝语义一致，严重级显示不同。
- 证据（2026-11-15）：11/11 定向 ctest，含新增 `selfhost_method_group_this_return`（gen1 `--deny-warnings` 孪生，锚定 `[W2072] returned closure captures`）与复验的六案差分门；三案双端奇偶探针：return fixture 2/2 条、clean fixture 0/0、边角形态 3/3（调用实参/cast/条件臂触发，索引读取/局部遮蔽/struct 静默）。interface 方法组返回无专门运行用例，仅静态路径覆盖。

## 152. 自举 GC 探测/快照运行时的发射模式与 IR 文本调试法（B-ID211）

- gen1 缺 `zan_gc_detect_cycles` / `zan_gc_write_heap_snapshot` 导出（宿主每模块发 weak no-op stub 压过，自举链无 rt_mem）→ 发射**自包含探测运行时**镜像宿主 rt_mem 算法，不盲链宿主对象（自举 ARC registry 与宿主不兼容）。接线三板斧：`irgen.zan` SeedExtern 后对 AST 预扫描 `GC.DetectCycles/WriteHeapSnapshot` 调用定开关（不触 GC 的程序零发射、零行为变化，prog1 IR `zan_gc` 零命中可证）；`externDecls.Add` 预加两符号抑制 DllImport declare（finalize 发射的 define 是唯一定义）；`@zalloc/@zrelease/@zstr_new/@zstr_release` 四处 `if (gcProbeUsed)` 条件钩子。per-class 字段 walk 函数复用 dtor 发射的 CollectFields/FieldBase/ArcManaged 惯用法，cid 表 `%zan.gc.ty {i8* cid, i8* walkfn, i8* name}`。
- 两个 IR 裁判级 bug 模式（clang `-disable-llvm-verifier` 下表现为随机崩溃/控制流损坏，`opt -passes=verify` 是唯一正道）：①多前驱块的 **phi 前驱表不全**——给循环尾块加 phi 后，后加的中间块（名字解析链）汇入同一尾块时必须同步补进 phi 前驱列表，否则 SSA 支配违规；②**struct 布局存读不一致**——写侧 `{count@+0, name@+1, bytes@+2}`、读侧把 +0 当 name 拿去 `inttoptr` 后 `fprintf %s` 打小整数地址即 AV。哨兵定位法：LLVM IR 是文本，手插 `%sf = call i8* @__acrt_iob_func(i32 2)` + `fprintf(%sf, "X\0A")` 哨兵（stderr 无缓冲、崩溃不丢；哨兵不能插在 terminator 之后），复刻 gen1 LinkExe 的 clang 命令编译即得崩点字母。MSVC 目标 CRT 无 stderr 数据导出，一律走 `__acrt_iob_func(2)`。
- 孤儿明细行的**预算语义**：宿主列前 min(oc,20) 个孤儿（按槽序、活/死槽不耗预算），孤儿槽散布在活对象之后——若拿 min(oc,20) 当槽位上界则一行都打不出（早期 3 行 stderr vs 宿主 9 行即此）。快照零计数行必须跳过：name==0 的行 `%-40s` 是 UB。
- 先读被测程序再怀疑编译器：探针"m1 无限重复/类分配即段错误"追了半天 IR，根因是**调试程序自己把标记块误贴进递归构造函数**（每次 new Node 打一行 m1 = 无限递归栈溢出），与探测运行时无关。二分定位之前先 `awk '/define .*被疑函数/,/^}/' 生成IR` 读一遍程序侧调用点。
- 金标对探全绿（2026-11-15）：gc_cycle_detector 双端 compileExit=0 runExit=0 stdout EXACT（12 行）、stderr 孤儿报告同形（1/2/3 环、摘要+明细、rc/internal_rc/size 字段一致；size 48 vs 40 源自 gen1 对象头多 cid 槽、site=0 是 v1 契约边界）；t3 最小快照案（对象数/头/行格式/自删）绿；毕业 diff_cases.txt + 单案 BLESS `tests/selfhost/ir_golden/gc_cycle_detector.hashes`（cmake -P 带 SKIP_GEN1_BUILD=1 + 单案清单可只 bless 一案，不动其他 244 份金标）；corpus_gaps.txt illegal-ir 14→13 出账。定向 ctest `case_gc_cycle_detector` + `selfhost_gen1` 绿。v1 边界（与宿主对齐或降级显示，非遗漏）：数组负载不追踪、探测程序分配付 O(live) 线性 dict 扫描、表外 cid 退 `"?"` 行。

## 153. 运行时内部函数绕过公开守卫的裸调缺陷与台账批量复核法（B-ID212）

- 自举 `async_when_all` 的 0xC0000005 根因在 **C 运行时**而非 gen1：`zan_join_bind`（rt_timer_co.c）裸调内部 `live_has_nolock`，而公开入口 `zan_co_live_has` 带 `!g_colive_cap return 0` 守卫。cap==0 时 `mask = cap-1 = SIZE_MAX`、`g_colive_slots==NULL`，`slots[hash & SIZE_MAX]` 用全宽哈希解引用空表 → AV。gen1 程序从不走 `zan_co_live_add`（那是宿主 io_co_sched 调度器启动流程的一部分），宿主程序 cap>0 故不崩——**"宿主过、自举崩"的案子根因可能在两端共享的运行时里，且只在其中一端的调用剖面触发**。修法一行：`if (g_colive_cap != 0 && live_has_nolock(frame))`；新增/复用内部函数时必须审计全部调用点的前置条件（本文件同类调用 :140/:281 都有调用方守卫，:389 是唯一漏网）。
- 取证路径照抄 §152 的 IR 哨兵法：runtime call 后各插一个 stderr 字母（register=A join_new=B acquire=C bind=D unpin=M resume=J/K），B、C 打了而 D、M 没打 ⇒ 崩在 `zan_join_bind` 内部未返回，再读 C 源锁行。修一类根因后**批量复核同台账类**：对 crash 类全部 async 案跑同一探针（gen1 编译 → 运行 → 归一化 diff 金标），13 案过绿批量毕业（diff_cases.txt 追加 + run_selfhost_ir_golden.cmake 带 SKIP_GEN1_BUILD=1 与多案清单一次 BLESS 13 份哈希）、转类案如实改台账（reject/wrong-stdout 各附原因与日期），不顺手硬修语义差。
- 归一化口径：`run_diff_parallel.py` 的 normalize 剥 `\r\n`→`\n`，且**金标自身行尾 CRLF/LF 混合**（宿主产出时带入）——本地探针判 DIFF 先 `tr -d '\r'` 复核再下结论。`run_case.cmake` 的 up-to-date stamp 只含 SRC/ZANC/STDLIB_STAMP，**不含 runtime obj**——改 C 运行时后验证宿主侧必须先删 `build/cases/*.exe` 强制重链，否则 case 复用旧二进制测不到修复。
- 本轮证据（2026-11-15）：gen1 侧 async_when_all 金标 EXACT（runExit=0）；13 案批量复核过绿毕业；宿主定向 `case_(async_when_all|async_mt_sched|async_delay_max|task_isdone_pure_query|gc_trim_memory)` 5/5 绿（新 obj 重链）；`selfhost_gen1` 绿。转类余量：`async_catch_ownership_route`（gen1 拒 Task.Spawn 静态方法组）、`async_preempt_hotloop`（brokeEarly 取消时机晚一轮——协作单 worker 泵与宿主多 worker 抢占的语义差，留 wrong-stdout 段）。zanc.exe 重链因他会话占用运行中 exe 而 pending，修复不改编译器代码，不阻塞。

## 154. 向量 Load/Store 的对齐无关契约与 crash 类批量复核 round1（B-ID199）

- 坑（2026-11-15 实测）：gen1 的 `Vector128/256.Load/Store`（irgen_simd.zan
  四处发射点）发出**无 align 属性的向量 load/store**，LLVM 按类型的 ABI
  对齐（16B）选 `movaps/movaps store`；而该 API 的契约是**对齐无关**
  （.NET 语义，C 宿主 expr_simd256.c 四处显式 `LLVMSetAlignment(…, 1)`）。
  stdlib 大量以内点指针驱动这些 API（`StringView.ptr+7`、
  `NativeMemory.SequenceEqual/IndexOf`），非对齐地址 → #GP → 0xC0000005。
  修法：四个发射点统一补 `, align 1`；**凡契约是"任意地址可访问"的内建
  API，gen1 发射访存必须落 align 1，与宿主 SetAlignment 一一同形**。
  钉子：新 conformance `simd_unaligned_load`（+3/+5/+7 三个非对齐内点
  驱动 128/256 位 load→store→读回，双端金标）。
- crash 类批量复核 round1（台账 crash 104 案，复用 `run_diff_parallel.py
  --cases 子集清单`，不带 --gaps 即无豁免直判）：19 案毕业（3×3 稳定率
  复核后再入 diff_cases——kafka_wire_protocol 两代二进制都掷硬币，v1
  批跑单样本 PASS 是假绿，**单次过绿不算毕业**）；61 案同一根转 reject
  （packages 内 `Task.Spawn/Run` 未支持：gen1 的诊断走 **stdout** 而非
  stderr，rc=1，采证脚本别只读 stderr）；7 转 wrong-stdout、1 转
  illegal-ir（orm_pool i64/ptr 混型），余 16 crash（12 AV + 4 堆损坏）。
- 定位手法（本案 round 的增量）：① gen1 程序的 VEH 崩溃日志在
  `%LOCALAPPDATA%\Zan\logs\zan_crash.log`（不随 exe 走）——但 **longjmp
  到垃圾 RIP 时 VEH 自身无法运行**（first-chance=0 即此症状），仍需 IR
  哨兵；② IR 哨兵线性行号二分**不等于执行序**：分支会跳过锚点行，
  "后面行号的哨兵打了而前面没打"属正常，要按控制流收窄；③ 栈溢出假设
  用 `-Wl,/STACK:33554432` 重链一次即可证伪（零成本）；④ 文本级实验
  先于源码重编：对产物 .ll 直接 sed 补 align 1 重链跑一次，金标 EXACT
  即坐实假设，再落源码修。
- 台账卫生：corpus_gaps 段头计数与段内实数存在**既有漂移**
  （wrong-stdout 头 49 实 47），本次按实数纠正并在段头注明；毕业名单
  进 gaps 注记块 + diff_cases.txt + ir_golden BLESS（清单必须用**纯案
  名**，传路径会把 `CASE_DIR/<路径>.zan` 拼成不存在的源）。
- 本轮证据：string_view_test 新 gen1 自然写法 rc=0 金标 EXACT；
  simd_unaligned_load 双端 MATCH；19 案 BLESS（19 rewritten, 0 errors）；
  定向 ctest case_memory_extensions_slice_indexof Passed（余两个在
  10 分钟窗外补跑取显式结论）。

## 155. 内建遮蔽守卫对齐与"症状轮盘"复核 round2（B-ID199）

- 坑（2026-11-15 实测）：gen1 的 GenCall 对 `Environment/File/Directory`
  内建**按裸类名+方法名劫持**（irgen_expr_call.zan），而宿主 24 处内建全部
  带 `!zan_type_defines(g, cn, mn)` 遮蔽守卫（expr_pred.c:159：binder 查得到
  类型且该类型定义了此方法就让路——**stdlib 定义同样生效**，宿主对
  stdlib-only 程序走 stdlib 体，探针二进制含 `zan_embed_has` 即铁证）。
  后果两族：① 用户类 `Environment` 的方法被劫持到真 argv 内建（判别
  实验：传真参后 builtin_shadowing 跑通但输出被替换成真 argv 值）；
  ② `Environment.ArgAt` 无边界检查裸读 `@__zan_argv[i+1]`——宿主契约
  `idx>=0 && idx+1<argc` 越界回空串，差分门裸跑（无 RUN_ARGS）必段错误。
  修法：七站点补 `e.resolvedSym == null || e.isIntrinsic`（**Task 分支
  既有判据**，与宿主 zan_type_defines 同义），ArgAt 补边界+phi+空串回退；
  `Cpu.CpuFeature` 宿主亦无守卫（expr_hw.c 名字直断），保持同形不动。
- **裸跑环境不公平案**：金标带 RUN_ARGS 的案（environment_unicode_args、
  main_args）在差分门裸跑协议下对**两个编译器都不满足**（宿主裸跑同样
  "0/空/空"）——按台账纪律逐案区分环境不公平与真实缺口：gen1 带参 =
  金标 EXACT（3×3）即不属 crash；真实缺口（main_args 的入口
  `string[] args` 装配）另记注记。裸跑契约钉子用**自足案**入 diff_cases
  （argat_oob_returns_empty 不带参数即可双端复现越界回空串契约）。
- **回归三分法**（改 irgen 后必做）：① `comm -12` 算改动面的
  diff_cases 交集（本轮为空 → 零自动回归面）；② ir_golden 既有指纹是
  否覆盖被改发射路径（277 份无内建使用案 → 不波及）；③ 批跑出现
  "已入门案"变红时**先二分再认账**——`git show HEAD:<file>` 写进
  `_scratch` 下的 src/selfhost 副本树，`zanc <副本清单> --auto-stdlib`
  独立编 pre-fix gen1（stdlib 解析按 CWD 找仓根，不依赖树位置），跑同
  一批：本轮 7 个 zandb/config_providers 案两代同挂 ⇒ 在飞 packages 域
  既有簇，与本报无关；不认领、如实留账。
- **症状轮盘**：堆损坏（0xC0000374）族的同案跨代换脸（AV / exit-1 软
  错误 / 静默 exit-0 / heap）——同一 exe 三连跑都可能不同；分类转移前
  必须做确定性验证（同 exe 3×、两代 gen1 对照），zandb_backup 首跑
  "unsupported or unfinished search index" 是轮盘面不是分类转移。
- 台账手术第二次教训：插入条目后除验成员外**必须验位置与计数**——
  round1 的 7 条 wrong-stdout 出账插进了文件头注释段中间且在 crash 段
  后滞留一份重复（awk 段计数 23/47 vs 头 16/54 立刻露馅），round2 布局
  修正并在段头注明。批跑 `--cases` 清单喂 **ugrep 输出的反斜杠路径**会
  拼出 `CASE_DIR\tests/conformance\x.zan`——sed 剥前缀用 `.*[/\]`。
- 本轮证据：builtin_shadowing 3×3 金标 EXACT + 官方 PASS；
  argat_oob_returns_empty 双端 EXACT + case_ ctest Passed；
  selfhost_gen1 绿；111 案批跑 45 过（stdlib File 体路由候选留后续轮
  3×3，单次过绿不毕业）；zanc 重链由他会话经闸代收。

## 156. gen1 EH 面三根因与守卫不对称性复核 round3（B-ID199）

- 坑（2026-11-15 实测）：gen1 的 EH 与守卫有三处**与宿主不对称**，同一
  批 crash 案三副面孔：(1) 实例字段**读**无 null 接收者守卫——A507 只补
  了方法调用路径（irgen_expr_call.zan:978-1000），`LoadField`
  （irgen_expr.zan:235）裸 GEP+load；宿主 access_member.c:534-546 对
  TYPE_CLASS 接收者是「icmp 查空 → emit_runtime_check → 软基址」三件套。
  **字段写两端都不设防**（探针实证宿主 `t.Value=5` 空引用也段错误）——
  补守卫只补读路径，写路径跟着宿主崩才是契约。(2) catch 子句遍历的
  「null tid（字符串/legacy throw）命中首子句」特判把裸字符串绑进
  typed 变量，读成员 = 野指针；宿主语义是 miss 上抛（金标 A/B 行）。
  判别探针：单机制全过（除零/越界/空方法调用/字符串 catch-all/连续
  try/简单 finally/typed 类抛接）→ 组合形态才崩（p_nullf/sA/sB）——
  **最小化必须按组合维度二分**，不能只按机制维度。(3) 静态 `[64]` EH
  帧栈四数组（__exc_bufs/__exc_stack/zpending_marks/zexc_marks）无界写：
  65 层过但静默写穿邻接全局、100 层输出全对后**析构段崩**、300 层中途
  崩——输出正确不等于没病，边界探测（64/65/100/200/300）才能定位。
- 设计坑：动态化帧栈时 main 根处理器往 `@__exc_stack[rsp]` 存的是
  **alloca 地址**（irgen_method.zan:405-409），朴素 realloc+fixup 会
  覆写成 &bufs[i]，uncaught throw 会 longjmp 到无人 setjmp 过的地址。
  候选：逐帧 malloc（条目为帧指针，增长免 fixup，unwind/landing 帧释放
  纪律要重排）或段链。
- ir_golden 全档门在在飞期不可用（预检红=diff_cases 在飞加案缺金标，
  B-ID214 账）；改按**存量金标清单**（ls ir_golden/*.hashes）手动
  emit+fnhash 量化漂移，再用 scratch HEAD 基线（git show HEAD 版两个
  改动文件）归因「我的漂移」vs「在飞漂移」——comm 差集才是本报表。
- 本轮证据：探针 12/12；runtime_error_catch / string_throw_dispatch
  3×3 EXACT + 官方 PASS；钉子 null_receiver_field_catch /
  string_throw_typed_miss 双端 3×3（带正控防过度修正）；锚定 ctest
  3/3（selfhost_gen1 重编+冒烟）；EH 已入门 4 案官方 4/4 PASS。

## 157. 动态 EH 帧栈 stack 退役法与 UCRT 对齐坑 round4（B-ID199）

- 设计法（找不变量的唯一破坏者）：`stack[i] ≡ &bufs[i]` 是 EH 帧栈的
  核心不变量，逐站点排查后发现**唯一**破坏者是 main 根处理器的 alloca
  （irgen_method.zan:405-409）。把它统一成 push 形态后，`@__exc_stack`
  全模块只剩 unwind 一处读点且可由 depth 重推导 → **整个数组退役**，
  帧地址永远从 `@__exc_sp` 推导（EmitFrameBuf），没有任何指针跨 grow
  存活 → 朴素 realloc+fixup 的安全顾虑直接消失，不需要 fixup 循环。
  先证「唯一破坏者」再设计，比给所有站点打 fixup 补丁便宜一个量级。
- UCRT 坑：`aligned_alloc` 无导出（LNK2019）。便携方案：
  `malloc(newcap*256+32)` + ptrtoint/add 31/and -32/inttoptr 手工对齐，
  **原始指针另存 `@__exc_bufs_raw` 供 free**（对齐后指针不能还给 free）。
  jmp_buf 是纯字节数据，随 memcpy 整体搬迁安全（longjmp 只消费缓冲内容，
  不依赖缓冲自身地址）。
- 改动面纪律：round4 触碰全部 push 站点（method 根/unwind/async
  prologue+rearm/task arm），含 try 的每个函数 IR 都变 → ir_golden
  **所有含 try 案全漂是预期**（25/25 same=0），不是回归信号；同树基线
  （金标=round3 签名、仅差本轮 6 文件）归因仍严格，全量 BLESS 重签。
  改完必须全量 grep 残留形态（round3 教训重演：unwind 的 marks GEP
  漏改两处，复查抓回）。
- 验证矩阵：边界探针 64/65/100/200/300（round3 的 65/100/300 三种病态
  全消，fin 逐层正确）；自然案 9/9 官方 PASS + 3×3 EXACT + 锚定 ctest
  9/9（round1-3 钉子零回归）。firebird_wire 驱动首跑单发 AV、裸跑与
  复跑全绿——非确定性轮盘**如实记录单发事实**，无因果证据不武断归因，
  留观。crash 段 12→11。

## 158. 同名类型参数遮蔽失败与「守卫即静默丢弃」反模式 round5（B-ID199/B-ID216）

- 反模式（守卫即静默丢弃）：`BindTypeArgs` 用 `IndexOfStr(names, T) < 0`
  才绑——这个守卫在**空列表**上是幂等去重，在**预填了另一个作用域绑定**
  （类级 T→string）的列表上就变成「显式 `<int>` 被静默吞掉」。同一守卫
  两种语义，取决于列表谁先写。同名遮蔽的正确形态：**各作用域先绑进自己
  的全新列表，最后按名合并（hit>=0 覆写否则追加）**——下游（tag/clone/
  ABI）全走合并表，零改名零波及。本例代码注释自己写着不变量（"the two
  must not be looked up in the same list"）但代码没执行——**注释声明
  的不变量要抽查代码是否真的在守**。
- 判别实验要变**实参类型**维度：引用实参（List）让 inttoptr bug 完全
  隐形（指针还是指针），值实参（int 7）才崩。只按「构造形态」二分会
  误判成 OpenList/Dual 无辜——它们的形状对了，实参类型不对。
- 在飞共享树的归因法（round3 HEAD 基线的修正版）：sources 清单引用在飞
  新文件时 HEAD 基线树建不齐 → 改用 **nofix gen1 = 在飞树减自己 2 文件**
  （scratch 拷树 + git show HEAD 还原那几个文件），全量 404 案 comm 对比
  fix/nofix 红集，净差再**多轮驱动复跑**验轮盘（本轮净差 2 案双方摇摆
  PASS/FAIL、prometheus_text 双端恒红子测轮换——单轮样本会把轮盘案误判
  成自己的副作用）。
- 宿主金标也可能是 bug（B-ID216）：对齐前先查**宿主自身一致性**——
  HolderB<int> at0=41/at1=0（写 ptr 步 8 字节槽 / 读 i32 步 4 字节，
  槽 0 侥幸对齐）是宿主 access_index.c 自我矛盾；把 gen1 修成 41 =
  复刻人家的步长错配。判据：具体化数组双端全对 + 宿主写/读 GEP 元素
  类型不同 → 金标锁的是宿主 bug，立账翻金标，不修 gen1。
- 本轮证据：7 探针全绿；type_param 不再崩（10/11 行合）；test_runner
  毕业（官方 PASS 双代 gen1 + 3×3 EXACT + 锚定 ctest Passed）；404 案
  全量归因零确定性副作用；金标 25/25 SAME + 258 存量陈旧归 B-ID214。

## 159. 擦除域的描述符按开放类型参数构建 round6（B-ID216 收口）

- 病根形态（「共享助手 × 实例化相关常量」）：字段初始化器住在
  **按类共享**的 `__zan_finits_*` 助手（隐式无参 ctor 的替身），而它
  `new T[n]` 里建的数组描述符（accept 谓词/ARC kind/形状键）是**编译期
  常量**——按开放 T 生成：谓词不命中任何分支 → accept 恒 false →
  任何引用型元素存储被 `zarr_store_compatible` 拒掉（鸣叫 + 槽保持
  null）；kind=0 还让 ARC 全程不 retain。int 案「过」纯属侥幸
  （kind 0 走裸 i64 槽路径，从不查描述符）——**侥幸路径会掩盖病根，
  修之前先分清「真对」与「恰好对」**。
- 修法（镜像既有机制，不发明新轮子）：spec finits——入队时
  `CloneSubst(cls, tps, args)` 把类节点连字段初始化器一起替后成具体
  类型，排放循环里带 curInstCls/curInstArgs 发射特化助手（命名
  `__zan_finits_<cls>__SPI_<tags>`，与 SpecCtorSym 同构）。存/读两端
  （LoadField 的 SubstRecvTy）本来就替后——**只有漏掉的那一端要补，
  先证三端各自动向再动手**（本轮 alloc/读/写三端逐一侦察后才发现
  gen1 是 alloc 端、宿主是写端，同族病不同端）。
- 有显式 ctor 的泛型类不走这条账（GenCtor:177 内联 EmitFieldInits，
  spec ctor 克隆的 AST 在入队时已替后）；反射路径
  （EmitTypeInfoMethod.CreateInstance 运行期按名匹配）无静态实例化，
  保持共享助手——**修法的适用边界要跟机制走，不是所有 new 站点都换**。
- `CloneSubst` 须和 `arrDepth` 一并保留 `arrRanks`，使特化体中的多维数组
  保留每层秩。回归由 `selfhost_generic_array_rank` 覆盖（32e7fe7d4）。
  链式泛型基类的字段初始化器
  在 spec finit 里仍按开放基类参数擦除（本轮案无此形）。宿主侧镜像
  修法一行：`concretize(g, fsym->type)` 仅在 `g->cur_inst` 活跃时套，
  具体类型透传——**替换必须保非泛型路径逐位等价**（cur_inst==NULL
  早退），这是金标不漂的先决条件。
- 本轮证据：p_tp2v 修后宿主 41/42、金标 83 双端一致；strs 探针 gen1
  aa/bb（修前鸣叫+null）；全案 gen1 EXACT 金标；锚定 ctest
  case_type_param_same_name Passed；corpus 波及面 = 仅本案（开放 T[]
  元素写全库只此一形）。

## 160. crash 段四根因与「共享树验证」纪律 round7（B-ID199）

- byte[]→string 形参的 ABI 契约是「裸指针直传」：宿主
  coerce_args_to_params 只 bitcast 指针类型、从不拷贝（TASKS A390），
  out/buffer 形参（AesGcm tagOut、decrypt pt）靠写穿透回调用方数组。
  gen1 托管路径自作主张打包一次性拷贝 → 写落拷贝、调用方数组恒零、
  认证必败。**跨 ABI 的参数传递以宿主契约为单一事实源，托管与 extern
  两条路径必须同形**；共享缓冲可行的前提是字符串运行时双布局（zstr
  /byte[] 头部不同，zstr_len/ARC retain-release/GC 分派全按 -8 magic
  词识别）——先审运行时容忍面，再动 ABI。
- 对数组做 `arr[-1]` 头部窥视的发射点必须先 null 守卫：null 数组读
  地址 -8 直接 AV。惯例 = null→""（与字符串路径同形），phi 汇合。
- async 帧槽位复用必须 consume-and-clear：socket intrinsic 把完成值
  （裸字节数）借用结果槽 6 存放，resume 读出后不清槽 → 返回路径的
  release-old 把标量当托管指针 free（zrelease(4)）。**槽位双职时，
  「读」不等于「消费」——所有权交接要在读点显式清槽**。
- 结构体永不走 vtable 路径：`override` 让 IsPolyClass 对结构体也真，
  EmitVirtualCall 会把打包 {x,y} 值载荷当 vtable 指针
  （call [0x400000003]）。分派门按 `cls.kind != AK.StructDecl()` 排除
  ——结构体运行时类型静态可知，接收者自身类型之下的 override 就是
  唯一实现。**「多态」判据必须含值/引用之别，不只看有没有 override**。
- EmitInterfaceCall 单候选 devirt 的健全性前提 = 接收者运行时类静态
  可知：interface 型接收者可持内建容器（List/Dict 无 ClassDecl，
  binder.types 扫描看不见）→ 对唯一可见候选直调是类型混淆。守卫把
  AV 变成受控 abort——**把内存错误变成诚实的受控失败本身就是修复的
  一部分**（语义缺口另立特性账，不混在崩溃账里）。
- 归因纪律（本轮 generic_virtual_dispatch 教训）：接口族回归疑似自己
  引入时，用「反演自己 hunks 的基线 gen1」（v1/v2）单跑对照——两代
  同红即存量，一跑定案不猜；IR 取证（擦除体恒 ret 0 vs 继承克隆发
  tid 比较）直接定位机制差异。该案本就在 wrong-stdout 台账裸列，
  **组回归名单前先对台账，裸列案不是新猎物**。
- 共享树验证纪律：ir_golden 全档门 270/279 红遇他会话 src/compiler
  在飞重构（zanc 重建 vs 金标铸造日）——发射面系统性漂移时**不
  BLESS**（同 B-ID214 先例：不把他人在飞工作洗进金标基线），改用
  行为差分（官方驱动，同 zanc 双侧自洽）+ 3×3 + 锚定 ctest 做本轮
  验证基；全批失败的归因用「行为态基线 gen1 复跑失败集」二分：
  双侧同红 = 存量/漂移，单侧红 = 嫌疑，再三连跑辨轮盘。

## 161. crash 段复核归因法与探针自伤坑 round8（B-ID199）

- 每轮开工先对 crash 段余案做**当前 gen1 复核**：累计修复会顺带治愈
  （test_runner、tdengine_pool 两个先例）——治愈案直接走毕业三件套
  （官方驱动 3×3 + 锚定 ctest + diff_cases 追加），不要按旧签名继续
  猎。
- 归因「近期才坏」的簇用**三代 gen1 对照**：现行树、行为态基线
  （回退本轮 hunks 重编）、**HEAD worktree gen1 + 当前 zanc**——三代
  同签名即与 selfhost 改动无关，根在 zanc（他会话在飞重建）或更下游。
  HEAD worktree（`git worktree add _scratch/<名> HEAD`，B-ID186 先例）
  是唯一干净的「零 selfhost 改动」基线；注意 gen1 运行时 objs 按 exe
  目录解析，worktree 产的 gen1 要放进 build/ 旁边跑。
- 已毕业案在批跑里翻红≠回归：先查他是否在「三代同签名」组里——
  zandb 全族（含毕业的 docs/kernel）随 zanc 重建整体翻红，B-ID186 的
  bzip2/decimal 漂移同面。**在飞域不修不绕**（WC §11），台账留归因
  注记 + 复归因触发条件（对方收口）。
- 探针自伤坑：bash heredoc 会吃反斜杠（`\s` 进文件变 `\s`），
  由此把「Insert 堆损坏」误入「转义丢失」歧途浪费一轮。**含反斜杠
  字面量的探针源一律用 python `io.open(...).write(r'''...''')` 写入**，
  写完 `cat -A` 验字节。副产品：该误探针暴露 gen1 词法对非法转义
  （宿主 E0017）静默吞——reject-parity 缺口立 B-ID219。
- 分层打点法（崩溃在包内时）：不改包源，探针案里用
  `File.AppendAllText("trace.txt", ...)` 在**怀疑链路的调用边界**打点
  （open→GetCollection→Insert），trace 停在哪一级，崩点就在下一级
  调用内；stdout 缓冲在崩溃即丢，trace 文件是唯一可靠信使（round7
  File 标记法的多级形态）。

## 162. lambda 重载评分的候选绑定与 checker 注解残留 round9（B-ID199）

- **AST 上有 checker 的注解残留**：无类型 lambda 参数可以带着类级类型
  参数 T（checker 语义检查时写入）。irgen 侧判定「待绑定」时只看
  `p.type == null || "var"` 会被骗过——**类型参数不是具体类型，一律
  `binder.IsTypeParam(p.type.sval)` 也算待绑定**，由候选提供真绑定。
- **lambda 实参的重载评分需要候选形参绑定**：`q.OrderBy(a => a.name)`
  的体类型推断离不开候选（Func<T,string> vs Func<T,int>）给出的参数
  类型；泛型类候选的 Func 类型实参是声明形态（Func<T,int>），必须用
  **调用点 receiver 实例化类型** `binder.SubstOwner(pt, recvTy)` 替换
  后再取（`binder.DelegateParams` 现成：Func 取 kids[:-1]、命名
  delegate 自带替换，勿手写）。receiver 实例化类型在 irgen 的唯一
  持有点是 EmitInstanceCallAs 的 `recv.ty`（FindType(clsName) 只是
  声明形态）——解析链 ResolveMethodUp/ResolveMethod/ScoreCand 要把
  它传下去；静态/探测/ctor 旁路传 null 是安全的（SubstOwner 零参
  owner 守卫 + DelegateParams 既有行为兜底）。
- **分层打点法在编译器本体上的应用**（§161 的 trace 是案程序，这轮
  是 gen1 自身）：打点打编译器源码（`Console.ErrorWriteLine`，编译期
  stderr），重编 gen1 后**编译一个最小案**观察。三层就够：候选集
  （ResolveMethod fit 名单+ParamTags）→ 分支进入（ScoreCand lambda
  分支）→ 参数态（LambdaRetTypeForCand 的 p.type 实况 + subst 结果）。
  「候选完好但推断恒 null」的平局疑案，参数态打点一锤定音（这轮
  靠它发现 p0.type=T 而非 null）。打点是证据也是临时脚手架：毕业前
  全删、干净版复跑 3×3 + 锚定 ctest。
- **共享 build 产物被他会话占用**：`ld.lld permission denied` 且
  Get-Process 见 python/clang/gen1 成组在飞 = 他会话差分驱动在跑。
  **不杀进程、不空转等待**——闸内改编独立名产物（-o
  build/selfhost_diff_gen1_r9.exe）全程零干扰，收尾不恢复共享名
  （等对方窗口统一重编标准名）。
- **批量回归顺带翻绿的案不毕业**：回归批里 1/1 金标一致但与本轮
  改动无因果证据（案内零相关构造成分、红因注记对不上）的案，按
  「单次过绿不毕业」持留，条目注记复验条件（3× 复跑）；因果归因
  纪律对面就是收益归因纪律。

## 163. reject-parity 钉法与 lexer 期诊断的 Diags 后置 round10（B-ID219）

- **reject 案必须零 stdlib 依赖**：官方驱动的 gen1 reject 编译**不带
  `--auto-stdlib`**（run_focused_reject 双端命令不对称：host 带
  `--package-project`，gen1 裸跑 `[gen1, out.ll, src]`）。用 `string` 的
  案会先报 undefined type，被「refused for another reason」顶掉预期
  诊断——reject 案用 char/int 内建类型写。
- **诊断码前缀 = 消息内嵌**：gen1 的 `[E3043]` 是 checker 手拼进消息
  文本的，没有独立码通道。做 reject-parity 时直接在消息里带
  `[E00XX] ` 前缀，就能和宿主**全形态逐词一致**（四码双路径 8 探针
  含码比对 8/8）；reject_cases 的子串钉法对带码/不带码都兼容。
- **lexer 期诊断的 Diags 后置**：driver 的 `Diags` 在 lex 之后才构造，
  Lexer 没有 diags 可报——用三平行表（escMsgs/escMsgLine/escMsgCol）
  收集，main 在 `new Diags()` 后逐条 `d.Error(msg, line, col)`（loc
  用字面量起始行列，不丢）。语义挂唯一实现（§85 的 StrEsc）：
  字符串转义诊断在 DecodeBytesChecked 里发，**不在 lexer 对字符串
  重复校验**（lexer 只对 char 字面量报——它本来就在词法期解码），
  两处消息文本必须逐词同源。
- **驱动的 cwd 陷阱**：传了 `--workdir` 后案编译在 workdir 里跑，
  `--case-dir`/`--rtobjs` 的相对路径全部失效（WinError 267/2 两形态：
  找不到案文件 / clang 找不到 obj）——**凡调 run_diff_parallel.py
  带非默认 workdir，一律绝对路径**。
- **reject 整表 63 案是活的回归面**：改 lexer/诊断输出后跑
  `--rejects tests/selfhost/reject_cases.txt`（--cases 传一个锚定
  正案补链接参数），63 存量钉 + 新钉一次收口——比逐案手动快且防
  「修一处漂三处」。

- 虚调用槽查询必须匹配所选重载的完整参数签名和引用传递方式。
  同名虚重载不能替代继承的普通方法重载。`inherited_overload_chain` 与
  `inherited_onclick_field_method_group` 分别覆盖普通和嵌套接收者的方法组，
  `generic_virtual_dispatch` 继续覆盖真正的虚分派。

## 164. 泛型类自有实例的 per-instantiation vtable round11（B-ID218）

**现象**：generic_virtual_dispatch 5 断言 wrong-answer，`direct.Accepts`
直调也红。**两个关键认知**：①`o is T` 的擦除不在 vtable 而在 GenIs——
T 非具体类 → tCls=false → 静态名比较恒假（.ll 里擦除体 `ret i64 0`）；
②「直调」对 vslot 方法同样走 EmitVirtualCall（从对象头 load vtable），
所以修一处分配点就修所有分发路径。

**修法（镜像 specFinits/SpecCtorSym，三处）**：
- `SpecVtableSym(cls, instTy)`：守卫（tps.Count、kids.Count 匹配、
  kids 具体、`VTableNeedsSpec`=任一 vslot impl 声明类为泛型且体提 T）
  → `Cls__SPI_<TypeTag>_vtable`，入 specVt 三队列；
- `EmitVtableAs` 在 Gen drain 循环新 vp 腿发射：体提 T 的槽经
  SpecQueueTy 排克隆（`MentionsTPDeep` 全树扫描定槽；与调用点克隆族
  同符号空间、specSyms 去重——直调先排的克隆直接被表引用）；绑定用
  TypeArgsForAncestor 支持继承链；**adapt 槽保持声明形态**（引用声明表
  发射的 `Cls_vslot_i` 适配器，行为与诊断零变化）；
- GenNew 存表指针一行。

**验证与归因**：3×3 驱动 + 锚定 ctest + .ll 铁证（`@Shape__SPI_Circle_
vtable` 槽 0 指真 tid 比较克隆、槽 1 Kind() 共享）。全批 419 案 61 红的
归因三板斧：①**触发面普查**（红案源码 grep 泛型类声明——仅 2 案且均为
接口实现零 vtable 槽）；②**零触碰路径静态证明**（runtime_type_test 的
`is List<string>` 走 ContainerTidName 单 tid 恒真路径，round11 零触碰
文件）；③**单钩子 nofix 对照**（禁 GenNew 钩子重编跑同案——超时三案
同挂=存量）。34 个 zan_hw_* LNK1120 是复现口径漏 hw_accel obj（补齐后
10 案转绿）——**全批回归先核对 rtobjs 与上轮一致再谈新增红**。

**工程坑**：①共享 build/ 的 diff gen1 exe 会被并行会话清理——gen1 放
`_scratch/<题>/` 隔离路径，驱动命令全绝对路径；②混轮次文件（
irgen_resolve.zan 同时承载 round9+round11 未提交改动）stash 不出干净
nofix——改用**功能级禁用**（注释单钩子）做 nofix 对照；③自举 gen1 的
`-o` 直连模式链的 rt 桩集在 build/ 清理后 LNK1120——复验一律走官方
驱动（显式 rtobjs），不走 gen1 裸 `-o`。

## 自举产物与宿主同链 M:N 调度器（B-ID199 round 6，2026-11-25）

**事实**：宿主在桌面目标上给每个程序链 `zanrt_io_mt`（`rt_io.c` 以
`-DZAN_IO_STACKLESS_ONLY -DZAN_CO_DRIVER` 编译的工作线程池调度器）。自举的
sh/shd 运行时束同样把 `rt_io.c` 按这一口味编译，强符号覆盖自举发射的弱 co
兜底——两侧调度语义一致。单线程兜底下"阻塞型 spawn 协程（accept、
Thread.Sleep）冻住整个程序"正是旧网络挂死簇的根因。

**协程代码在工作线程上必须守的三条**（违反即并发 UAF / 死锁，宿主早已守）：
1. **协程步内不泵调度器**：`zan_co_sched_run` 在 worker 上等全池空闲，而调用
   方自己这一步就算"在跑"——永不返回。`Task.IsDone` 是纯查询；协程内
   `await <已存句柄>` 用"测 done → 让出（Task.Yield 式重排）→ 复测"，同步
   上下文才保留泵循环（宿主 `construct_await.c` 的 stored-handle 分支同口径；
   这条分支此前因 `await_target_is_async` 对 Task 恒真而是死代码，句柄被当
   子帧读出垃圾结果）。
2. **完成序：先清自身，再发布 DONE**：DONE 一可见，等待者就可能在另一
   worker 上读结果并 `zexc_frame_free` 本帧（展开槽 18 所有者链）。所以写结
   果、`zan_timer_cancel_delay(self)`、读 awaiter 槽、展开自身所有者链、归还
   线程 owners 全在 DONE（release store）之前；之后只用 SSA 值唤醒。
3. **`await` 运算对象的"已存句柄"只认局部变量（或局部列表按局部/字面量下
   标）**：`tcs.Task` 之类属性读出的是可 await 的帧，走帧等待路径。

**诊断法**：gen1 IR 链 ASan 时 clang 不给 `.ll` 函数加 `sanitize_address`
属性，检查不插桩——先 `perl -pe 's/^(define [^{]*\))\s*\{\s*$/$1 sanitize_address {\n/'`
再 `-fsanitize=address` 链接，heap-use-after-free 的分配/释放线程一目了然。
测试里 `x = x + 1` / 共享 `List.Add` 在 M:N 下是真数据竞争（宿主同样偶发），
修测试加锁而不是让编译器背锅；判据是宿主多轮循环也能复现。

## 协程里的求值序与异常区（B-ID199 round 7，2026-11-25）

- **await 提升不能改求值序**：ANF 把嵌入 await 提到语句前的临时量时，
  ①`x OP await f()` 的左操作数（字段/成员/调用——局部与常量除外）先快照进
  临时量；②`&&`/`||`/`?:` 臂里的 await 只在该臂被取时执行（降为
  `bool t = a; if (t) { …; t = b; }` 与 if/else 赋临时量），无条件提升会让
  死臂的生产者也跑（async_await_string_operator）。
- **协程里的 `lock` 也是异常区**：体内抛出必须 `zan_monitor_exit`。异步方法
  按 async try 同样布防（EmitExcPush + 处理器计数 + asyncTryLbl 落点），并让
  `AsyncExceptionNesting` 把 LockStmt 计入嵌套层（帧上 owner-mark 槽按它分配）。
  漏了它锁永不释放，下一个加锁者（常在另一 worker）永久阻塞。
- 判定先看宿主：本轮扫描里 timezone_info、yaml_values、firefox_browser_model 等
  "红案"宿主同红——先在本机跑宿主再归因，别对着陈旧 golden 改编译器。

## 异常路径上的所有权（B-ID199 round 8，2026-11-25）

- **调用表达式临时量要上所有者链**：GenExpr 进 Call 时 EnterCallArm，
  AddTemp 立刻把新临时量落 entry 槽并 zlocal_arm；调用返回时内层清单并入
  同函数的外层调用，最外层 LeaveCallArm 统一 zlocal_retire（语句末释放照旧）。
  调用表达式内无函数出口，所以武装槽不会活过帧；清单按 entryAllocas 归属
  函数，内联生成的 lambda/绑定访问器绝不武装进外层函数的调用。
- **条件里的临时量判定完即释放**：if/while/for 条件的临时量在分支前释放；
  条件里声明的模式变量**持有**自己的引用（retain），到语句/本轮迭代结束释放
  （宿主同口径，且宿主不许在 if 之后使用该变量）。
- **字段初始化器是独立语句**：初始化表达式的临时量要释放，否则每个
  `public H State = new H();` 都多一个引用。
- 归因法：weak 字段观察释放点 + 宿主/旧 gen1 对照（gen1_rN）——每案先判
  "本轮引入 / 存量 / 宿主同红"再动手。
