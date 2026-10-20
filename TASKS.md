# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

### [ ] A392 自举的运行期类型判定看不见接口与容器

- 现象（2026-10-20 修完 A384 后的边界）：自举后端现在能给对象判定类身份
  （对象头里的类标识槽，见
  `.agents/skills/zan-compiler-internals/references/selfhost-classid.md`），
  但还有两类值判不出来：
  - **接口**：接口不是基类链的一环，`IsSubclassName` 沿着基类链走，看不见
    「某类实现了某接口」→ `x is IFoo` 恒为假（vtable 时代也如此，不是回归）。
  - **容器**：`List<T>` / `Dictionary<K,V>` / `StringBuilder` 的类标识槽是 0
    → `o is List<int>` 恒为假（C 宿主能判）。
- 方向：接口要在基类链之外另建「实现关系表」；容器要给每个实例写自己的
  tid（泛型实例化按元素类型分别记）。两者都要先决定**类型名怎么写进对象头**
  （自举现在只存 tid 全局的地址，没有类型名）。
- ⚠ 泛型容器的 tid 要按元素类型分别发射，工作量不小；先确认差分门是否
  需要这两种判定再动手。

### [ ] A385 自举协程帧布局与 C 宿主的 `zan.co.header` 不一致——自举产出的程序用不了发行版 mt 运行时

- 现象（2026-10-20 差分门实测）：自举产出的 `.ll` 链接发行版协程运行时
  （`toolchain/win-x64/zanrt_io_mt.o`，即 `rt_io.c -DZAN_CO_DRIVER` 的产物）会跑飞：
  该运行时的 `zan_co_ready` 把帧首部当 `zan_co_fhdr` 读写（`src/runtime/rt_io.c:4140`
  读 `h->sched`、`h->pending`），而自举的帧首 8 字节是**状态机 state**、次 8 字节是
  **done 标志**——运行时会去改这两个字段，状态机随即错乱。
- 根因：两套帧布局各写各的：
  - C 宿主：`zan.co.header`（state/done/awaiter/awaiter_step/result/exc/exc_tid/
    exc_own/hcount/sched/pending…），运行时按这个布局接管帧；
  - 自举 IR 后端（`src/selfhost/irgen_async.zan` 的 `GenAsyncMethod`）：自定义布局
    `[0]=state [1]=done [2]=awaiter [3]=awaiter_step [4]=result [5]=own_resume
     [6]=cleanup [7]=hcount`，之后才是 this/参数/局部/子任务槽。
  两边都不认识对方的帧。
- 现状处理：自举后端**自带一套完整的弱协程运行时**（`EmitAsyncRuntime`：
  就绪队列 + 定时器表 + 泵），差分门链接集里不放 `rt_co.c`/mt 运行时，让这套弱实现
  整体生效（`tests/run_selfhost_diff.cmake` 的链接集说明）。自举产出的程序因此
  **不能**与发行版 mt 运行时混用（异步路径会跑飞），非异步路径无影响。
- 方向：把自举的帧布局改成 C 宿主的 `zan.co.header`（字段顺序、类型、异常槽都要
  对齐），然后自举就可以只保留"弱回退"、让发行版运行时接管——那才是与 C 宿主
  同构的终局。改动面：`irgen_async.zan` 的 `GenAsyncMethod` 全部槽位下标、
  `EmitAsyncComplete`/`EmitAwait`/`__zan_async_unwind` 的字段访问、以及帧的分配大小。
- 布局对照（2026-10-20 实测，权威在 `src/compiler/irgen_expr_core.c:36-48` 的
  `ASYNC_FRAME_*` 枚举 + `irgen.c:2224-2236` 的 `co_hdr_fields`）：
  | 下标 | 字段 | 类型 | 偏移 |
  |---|---|---|---|
  | 0 | `SCHED` | i64 | 0 |
  | 1 | `SCHED_STEP` | ptr | 8 |
  | 2 | `STATE` | i32 | 16 |
  | 3 | `DONE` | i32 | 20 |
  | 4 | `AWAITER` | ptr | 24 |
  | 5 | `AWAITER_STEP` | ptr | 32 |
  | 6 | `RESULT` | i64 | 40 |
  | 7 | `CLEANUP` | ptr | 48 |
  | 8 | `HCOUNT` | i32 | 56 |
  | 9 | `SELF_STEP` | ptr | 64 |
  | 10 | `EXC` | i64 | 72 |
  | 11 | `EXC_TID` | i32 | 80 |
  ……共 16 个字段（`co_hdr_fields`），之后才是 this/参数/局部/子任务槽。
  运行时把这套布局**写死**了：`src/runtime/rt_timer.c:1021` 的
  `JOIN_OFF_DONE = 12 + sizeof(void*)`（=20）配两条 `_Static_assert` 钉住
  `state`/`done` 的偏移——帧布局一动，运行时与后端必须同时改。
  自举的帧现在是**扁平 i64 数组**（头 8 槽），要改成同构的 struct，槽位访问
  全部从 `getelementptr i64` 换成 `getelementptr %struct`。
- ⚠ 帧布局一变，自举固定点的 gen2/g3 必须重跑（`ctest -R '^selfhost_fixed_point$'`），
  差分门的 `diff_known_gaps.txt` 也要同步。

### [ ] A393 自举调用 `async void` 方法不启动协程——方法体根本不执行

- 现象（2026-10-20 差分门探针实测，探针在 `_scratch/sd/p_asyncvoid.zan`）：
  ```zan
  static async void Main() { Work(); await Task.Delay(50); Console.WriteLine("done"); }
  static async void Work()  { await Task.Delay(1);  Console.WriteLine("work"); }
  ```
  - C 宿主：`work` 然后 `done`——**方法体会执行**（async Main 的调度器在跑）。
  - 自举：只有 `done`——**方法体被整个丢掉**，`Work` 当普通方法调用。
  - 同步 `Main` 里调 async void 方法时，两边都不执行（C 宿主也没有调度器）。
- 根因：自举后端在**调用点**没有区分 async 方法——没有生成"分配帧 + 挂起 +
  调度"的启动序列，只有 async `Main` 自己是被特殊处理的（见 0f063c2e）。
- 方向：调用点遇到 async 方法时，按 `GenAsyncMethod` 的 ramp 形态生成协程启动
  （分配帧、填 resume/cleanup 指针、交给调度器），而不是直接 `call`。
- ⚠ 与 async 帧布局耦合（A385），改完必须重跑 `selfhost_fixed_point` 与差分门；
  另外 async 方法的返回类型 `Task<T>` 自举还不认识（`unknown type 'Task'`），
  只能先覆盖 `async void`。
- ✅ 已修（2026-10-20，提交 `c5421633`）：调用点新增 `Irgen.EmitAsyncDetach`，
  语句位置的 async 调用把帧交给 `zan_co_ready(frame, resume)`（resume 入口从**帧槽 5**
  读，ramp 写在那儿）。「真 async 调用」用 `ExprVal.asyncCall` 标记区分，**不能**
  用「类型是 `__task`」——降级路径也带 `__task` 类型但值是常量 0，detach 它会生成
  `bitcast i8* 0 to i64*`。用例 `tests/conformance/async_void_call.zan` 钉住。

### [ ] A394 自举在**同步**上下文里遇到 `await` 不 fail-closed——生成读地址 0 的非法 IR

- 现象（2026-10-20 探针实测，探针在 `_scratch/sd/p_capparam.zan`）：
  ```zan
  static void Main() {            // 没有 async 修饰，但函数体里有 await
      A1(3);                       // A1 是 async void
      await Task.Delay(80);
      Console.WriteLine("done");
  }
  ```
  - C 宿主：把 `Main` 当协程处理，程序正常跑完。
  - 自举（HEAD 与修复后都一样）：`Main` 仍按**同步**方法生成，但 `await` 落到
    `EmitAwait` 的非 async 分支，它把 await 的操作数当**协程帧指针**用：
    `bitcast i8* 0 to i64*` → 读地址 0 的槽 5 → clang 直接报
    `integer constant must have integer type`（链接失败）。
- 根因：自举的 await 降级假设「被 await 的东西是**协程帧**」（读帧的槽 5 拿
  resume 入口）。`Task.Delay` 是 C 宿主的**内建**（`builtin_api.c`），自举不认识
  它 → 调用**静默降级成常量 0** → `bitcast i8* 0 to i64*` → 非法 IR。
- 方向（**注意：原方向写错了**）：不是「同步方法里的 await 一律拒绝」——
  **同步方法里 await 一个 async 方法是合法的**（C 宿主也接受，自举的
  `tests/selfhost/prog1.zan` 就靠 `int av = await P.AsTwice(21);` 跑出 42：
  同步上下文会把被 await 的协程跑完再取结果）。真正要 fail-closed 的是
  **被 await 的操作数不是协程帧**。
- ✅ 已修（2026-10-20）：`EmitAwait` 检查操作数的 `ExprVal.asyncCall` 标记——
  不是协程帧就报诊断（"'await' expects an async call in the self-hosted
  compiler"），不再生成非法 IR。配套：检查器拒绝 **async 方法里 lambda 内部的
  await**（lambda 不是协程，在协程里跑同步 await 会重入调度器）。
  回归门：差分门新增 **reject 列表**（`tests/selfhost/reject_cases.txt` +
  `tests/selfhost/reject/`），这类"必须拒绝"的程序由门断言"非零退出且不落 IR"。
- 遗留（另立）：自举不认识 C 宿主的内建方法（`Task.Delay` / `Task.Yield` /
  `Math.*` …）时**静默返回 0**——这是"能力缺口必须 fail-closed"的另一处违反，
  要单独做（要么给自举一份内建表，要么对认不出的调用报诊断）。
- ⚠ 这条与 A393 无关：A393 修的是 **async 方法的调用点不启动协程**；
  两者都在 async 路径上，但根因不同，修完都要重跑 `selfhost_fixed_point`
  与差分门。

### [ ] A380 协程帧的释放仍只在类型侧收口——运行时没有幂等保护

- 现状（A379 已闭）：`Task.Run(lambda)` 的句柄被检查器定型成"无结果任务"，
  把 lambda 句柄当 `Task<T>` 用去读 `.Result`/`.Wait()` 会在**编译期**被拒绝
  （`tests/diag/task_run_action_result_type.zan`），那条运行期 double free 路径
  因此不可达。
- 但运行时本身仍没有防御层：帧若真被释放两次，`__zan_co_frame_free`
  （`src/runtime/rt_io.c:4164`）的幂等守卫读帧首 8 字节的 `CO_DEAD` 位，而小对象
  分配器回收时把用户块首 8 字节写成 freelist 指针（`src/runtime/rt_mem.c:447`），
  守卫读到的已是垃圾 → 二次释放直接 abort（`double free of block`）。
- 方向（二选一）：
  - **a. 活协程表仲裁**：`zan_co_live_del(frame)` 改成返回"本次是否真的摘掉了
    这个帧"，谁摘掉谁负责 free，后到的 reap 看到"已摘"直接返回。活协程表本来
    就是帧的注册表（`src/runtime/rt_timer.c:956`，带墓碑），天然适合当所有权仲裁。
  - **b. 守卫位搬家**：把 DEAD 标记挪到分配器不回写的槽位（帧首 8 字节不能用）。
    但帧释放后那块内存会被复用，标记会变成别的对象的垃圾 → 只能把"误判为已死"
    的代价从 abort 降成泄漏，不彻底。
- 连带：`__zan_co_isdone` 在帧已被完成路径释放后仍会读它的 DONE 字段（先查活
  协程表、再读帧字段，中间有窗口）——4 字节 UAF 读，通常不炸但语义不严。
- ⚠ 改 `src/runtime/rt_io.c` / `rt_timer.c` 会动到自举固定点的 gen2 运行时包
  （`zanrt_sh_*`），改完必须重跑 `selfhost_fixed_point`。

### [ ] A381 SPEC 的"尚未实现"清单 7 项一项没动

`docs/SPEC.md:766-776` 明列、实测仍不可用的：

- `checked` / `unchecked` 关键字是 **no-op**（溢出检查恒关，环绕语义）
- lazy iterators：`yield` 物化成 list，没有 `IEnumerable<T>` 协议
- tagged union 枚举（代数数据类型）
- `params` 之后的可选实参组合未系统验证
- `$@"..."` 组合字符串、`"""` 原始字符串
- COW 集合：`List` 赋值是引用共享，修改即共享
- `[CImport]`、`project.zan` 项目文件、`zan` CLI（实际是 `zanc`）

- 方向：逐项单独立项实现，每项带 conformance 用例；不做半截实现。

### [ ] A382 编译器缺三件基础能力：诊断编号体系、告警级别开关、Marshal

实测（`src/compiler` 全量 grep 0 命中）：

- 无诊断编号：诊断只有自然语言文本，没有 `E001`/`W001` 这样的稳定编号——工具链
  无法按编号定位/抑制，也做不了 `--warn-as-error` 这类开关
- 无 `--warn-level`：没有统一的告警级别开关
- 无 `#pragma warning`：无法在源码里局部调级或禁用
- 无 `Marshal`（`DllImport` 之外的互操作面）
- 附带一条语义偏差：越界检查是有的（`src/compiler/irgen_generics.c:1313`
  数组/string/span），但 `checked`/`unchecked` 开关不了它——只有全局
  `runtime_checks` 标志能整体关掉，与 C#"checked 上下文才检查"对不上。

- 方向：诊断编号是**破坏性变更**（所有诊断文本与编号都要定版，且影响 golden
  诊断输出），先定编号规范与兼容策略；`--warn-level` 与 `#pragma warning`
  跟在编号之后做。

### [ ] A383 图表引擎整代语义差：官方 470 个 option 键里 278 个（59%）从不读取

- 事实（`docs/archive/CHART_CODE_GAP_LEDGER.md`）：对照 ECharts 6.1 的 596 个
  `.ts` 源，官方 option 用到的 470 个配置键里，引擎**从不读取**的有 278 个（59%）。
- 表现：用户照官方示例写 option，键被静默丢弃——不报错、不告警，图形少一块也
  不知道少了哪一块。
- 方向：先补"未知 option 键"的告警（廉价，立刻让缺口可见），再按官方示例
  覆盖度逐个补实现；不要一次铺 278 个键。

### [ ] A377 legend 页 16「将星图谱」页脚被挤成 14 设备高、文字上半被裁

- 现象（`LEGEND_OFFLINE=1` 走查页 16，设备像素 @150%）：页脚那条 `row`
  （`dock 2`）只有 **h=14**，而它自己的内容偏好高是 **30**（`star-pager` 的
  `prefH=30`、两个 `Label` 的 `prefH=32`）→ 「已点亮：0 / 35 说明」与分页
  控件上半被裁掉。
- 量化：body 可用 **682**（y 200..882），页内容定高 = 卡阵/分栏 **611**
  （y 212..823）+ 顶距 **36** + 页脚带宽 **45** + 页脚高 **30** ≈ **723**
  → 超 41 设备，第一趟布局按序排 dock 1..4 时后面的页脚只能吃残值 14。
- 方向：审计 §5.4b「坑 1」给过定死改法——撑条别用 `Grow()`
  （`dock=5` 走第二趟、不参与占位），要定高 `VGap(h)` 或 `DockBottom()`；
  同时需按参考图 `将星图谱.png` 实测该页**顶距与页脚带**（审计只给了卡阵
  180×255+价格条 34、行节距 316、左分栏 13 格，没给顶距/页脚），别拿现在
  的 36/45/30 三个数当准。
- ⚠ **别靠改窗口高来给页脚腾地方**：`Spec.WindowH()=625`（= 内容 610 +
  标题栏 32 − 17）是**用户点名缩过**的（`Spec.zan:35-38`：「整个窗口高度
  应该可以缩小一点，元素整体上收」，让卡阵在主区上下留白 57 设备与左右 44
  相等）。窗口比参考图矮 21 逻辑像素是有意为之，本页要从**页内**（顶距/
  页脚带）收，并保持卡阵居中留白。
- 同类风险页（同形状：定高卡阵 + 底锚页脚）：7 城池争霸、8 传说塔、
  19 至尊殿堂。

### [ ] A378 legend 导航带对齐校验脚本与 Spec 常量不一致，门已失效

- `tests/templates/legend/test_navigation_layout.py` 是手跑的对齐检查器
  （不参与 pytest 收集，需喂 `dump tree` 的 JSON），它硬编码的是旧模型：
  图标 **22×22**、瓦片高 **32**（导航 30 号 48）、inset 4、gap 4。
- 当前 Spec 是 `NavIconW=Px(19)`、`NavIconH=Px(21)`、`NavTileH=Px(33)`
  （外框 32.7）、`Spec.Px(6)` 间隙；实测 dump（nav-0 瓦片 142×49、图标
  28×31、inset 12 设备、格间隙 9 设备、nav-30 49×49 纯图标）与 Spec 一致
  → **任何当前构建下该脚本必失败**，标准实际上处于无人校验状态。
- 另有一处矛盾要一并定：审计 §「顶部导航」写瓦片高 **30.7**（逻辑），
  Spec 注释写外框 **32.7**，差 2 逻辑像素（3 设备），需按参考图逐像素
  实测瓦片外框高定哪套是准。
- 方向：先定准绳（参考图实测外框高），再把脚本改成**从 `Spec` 取参**并补
  导航 30 号齿轮格，最后拿真 dump 跑绿；不要为了让它变绿去改脚本里的数字。

### [ ] A388 `zanc --emit-ir` 输出的 IR 交给 LLVM 后端会被拒——256 位向量路径在 generic CPU 下拆不开

- 现象：`zanc <file> --emit-ir` 落出的 IR 直接喂给 LLVM 自带的 `llc` 会在
  `DAGTypeLegalizer::SplitVectorOperand` 处 fatal error：
  `LLVM ERROR: Do not know how to split this operator's operand!`
  （停在哪一步：*Running pass 'X86 DAG->DAG Instruction Selection' on function
  '@MemoryExtensions_SequenceEqual'*）。复现：
  `zanc tests/conformance/task_spawn_indirect.zan --emit-ir --no-packages > t.ll`
  然后 `llc -filetype=obj t.ll`（确定性必崩）。
- **2026-10-20 实测更正（此前登记的根因只对了一半）**：
  - 触发条件不是"`<32 x i1>` 本身"这么窄，而是**CPU 特性**。同一份 IR：
    `llc -filetype=obj`（默认 generic CPU）崩；`llc -mattr=+avx2` /
    `-mcpu=haswell` / `-mcpu=znver2` / `-mcpu=skylake-avx512` **全部通过**。
  - 编译自己不受影响：`src/compiler/irgen_emit.c:3032` 建 TargetMachine 时 CPU 取
    `x86-64`、特性取 `+sse3,...,+avx,+avx2,+fma,+bmi`，后端拿到的是 AVX2 档，
    所以 zanc 产出的二进制是对的；坏的是**给外部工具消费的 .ll 文本**。
  - 此前"正常构建靠 globaldce 删掉死的 stdlib 函数才不炸"的说法是错的：live
    函数（`Program_Mask`，`Vector256.Equals` + `ExtractMostSignificantBits`）
    单独抽出来喂 llc 同样崩，只是正常构建里它被 globaldce 顺手删掉了。
  - 不可合法化的不止比较一处：把 `icmp eq <32 x i8>` 换成常量后仍崩，说明
    `Vector256.Create` 的字节广播（`shufflevector <32 x i8> ..., zeroinitializer`
    + bitcast 到 `<4 x i64>` + extractelement）同样不可拆。**凡是 256 位向量被
    拆回标量的地方都中招**：generic CPU 下 X86 要把 256 位算子拆成 2×128 位，
    而函数里同时存在 `llvm.x86.avx2.pmovmskb` 这类 AVX2 内建函数 → 256 位类型
    被判为合法 → `<32 x i1>` 这类 i1 向量留在图里 → `SplitVectorOperand` 没有
    兜底路径 → 报错。
  - 试过但**不管用**的写法（别再试）：函数级属性
    `attributes #0 = { "target-cpu"=... "target-features"=... }`（LLVM 20 的 llc
    不拿它做合法化依据）、文本 IR 的 `module attributes`（语法不支持）、
    `!llvm.module.flags` 里的 `target-features`。
- 方向（按代价从低到高）：
  (1) **让 .ll 自述目标特性**：在 `--emit-ir` 的输出里带上编译自己用的
      `+sse3,...,+avx2,...`（哪怕只是注释或 stderr 提示），让消费方用
      `llc -mattr=+avx2` 复现编译器的选择；不修根因但让产物可用。
  (2) **重做 Vector256 的降级路径**：不要把 `<32 x i8>` 拆回标量——
      广播、比较、掩码全在 `<4 x i64>` 上做（移位/掩码的 SWAR 逐字节比较，
      不产生任何 i1 向量），让 IR 在 generic CPU 下也能合法化；这是治本但
      触及 `src/compiler/irgen_expr.c` 的向量整段 lowering，风险高。
  (3) 走 (1) 并把 (2) 拆成独立条目。
- ⚠ 不要用"先 `opt -O2` 再 llc"绕过：`opt -O2` 后能过，是因为优化器把死函数与
  冗余结构一起干掉了，编译器自己产出的 IR 仍然过不了自己的后端。

### [ ] A389 自举的 `struct` 是堆对象（引用语义），与 C 宿主的按值语义分歧

- 现象（2026-10-20 自举探针实测，探针在 `_scratch/sd/s1.zan`、`s3.zan`）：
  同一个程序，C 宿主按值语义，自举编译器（gen1）是别名语义：
  | 场景 | C 宿主 | 自举 gen1 |
  |---|---|---|
  | `list.Add(p)` 后改 `p.x`，读 `list[0].x` | `1` | `99` |
  | `Bump(p)` 改形参 `v.x`，读 `p.x` | `1` | `7` |
  | `P3 r = q; r.x = 42;` 读 `q.x` | `3` | `42` |
- 根因：自举 IR 后端把 `struct` 实例化成堆对象（`zalloc` + 字段偏移），
  赋值、传参、`List<T>` 入列都只拷**指针**；C 宿主把 struct **按值**拷贝
  （C# 的 struct 是值类型，`List<struct>` 的槽位存的是值的副本）。
- 影响面：struct 的一切传参/赋值/入容器都是别名；`List<struct>`、
  `Dictionary<K,V>` 的值语义与 C 宿主不一致（自举里改一个 struct 变量，
  容器里、别名变量里全都跟着变）。
- 方向（二选一）：
  (1) 让自举的 struct 回到值语义——赋值/传参/入容器走一次字段拷贝，
      局部变量不装箱；这是与 C# 语言规范同构的终局，但要动 struct 的
      类型表示（当前是"堆对象 + 指针"），波及面大；
  (2) 明确自举的 struct 为引用语义，在 `docs/agent-kb/bootstrap.md` 记为
      **已知语言分歧**，不追求一致——但差分门一旦收录 struct 相关用例就会红。
- ⚠ 动 struct 的表示会动自举固定点的 gen2 运行时包（`zanrt_sh_*`），改完必须
  重跑 `selfhost_fixed_point` 与差分门；另外要新增 conformance 用例把
  "struct 按值传参/赋值/入列"钉住，否则修好了也没人守。

### [ ] A390 自举的「数组 → string」是复制打包，不是共享 view

- 现象（2026-10-20 修完 string 长度语义后实测）：
  - `byte[] raw; string v = raw;` 在自举里是**复制**（把每个元素的低 8 位
    拷进新的托管串），C 宿主是**共享 view**（改 `raw[0]`，`v[0]` 跟着变）。
    原因：自举的数组是「每元素一个 8 字节槽」，不是紧凑字节，没法直接
    当字符串指针共享。
- 方向：
  (1) 数组 → string 要真正共享，得让自举的 `byte[]` 变成紧凑字节布局
      （每元素 1 字节）——这是数组表示级改造，会波及所有数组代码；
  (2) 或者承认自举的数组是 8 字节槽，把「共享 view」在文档里记为**已知
      语言分歧**，只保证长度与内容正确。
- ⚠ 动数组布局会动自举固定点的 gen2 运行时包，改完必须重跑
  `selfhost_fixed_point` 与差分门。

- ✅ 已修（2026-10-20，含 NUL 的子串搜索语义）：原方向 (3)「含 NUL 的子串
  搜索改成按长度比较（memcmp + 显式长度）」是**错的**——先实测 C 宿主才发现
  它的搜索/变换类方法用的是 **C 字符串语义**（截断于第一个 NUL）：
  `"AB\0CD".Trim().Length == 2`、`.EndsWith("CD") == false`、
  `.LastIndexOf("C") == -1`、`.LastIndexOf("") == 2`、`.ToString().Length == 2`；
  而 `Length` / 索引 / `Substring` / 拼接 / `==` 才是精确语义
  （`Length == 5`、`Substring(2,2).Length == 2`、`(h+"X").Length == 6`）。
  自举原来按精确长度做搜索，**反而**与 C 宿主分歧（`EndsWith` 判 true、
  `LastIndexOf` 判 3）。已改为与 C 宿主一致：新增运行时 `zstr_clen`
  （= `strlen`，带 magic 校验），`Trim` / `ToUpper` / `ToLower` / `Replace` /
  `Split` / `ToString` / `EndsWith` / `LastIndexOf` / `StartsWith` 全部改用
  C 可见长度；用例 `tests/conformance/string_nul_search.zan` 钉住。

### [ ] A395 闭包不捕获外层方法的接收者 `this`——实例字段在闭包里一律读成 0

- 现象（2026-10-20 探针实测，探针在 `_scratch/sd/p_this.zan`）：
  ```zan
  class Counter { int n; int[] buf;
      public Func<int> GetN() { return () => n; } }         // C 宿主 n=3，自举 n=0
  ```
  - C 宿主：`n=3` / `len=3` / 嵌套闭包 `p=8`；
  - 自举：`n=0` / `len=0`（嵌套闭包因捕获的是**字段**同样读 0）。
- 根因：自举 IR 后端在闭包体里把 `curHasThis` 置 false、`curClass` 置空，
  于是闭包里的裸标识符既不是局部变量、也够不到实例字段 → 直接返回常量 0。
  C 宿主（`checker.c` 的 `expr_captures_this`）与 ngen 后端
  （`ngen_delegate.zan` 的 `DelegateCaptureNameNg("this", …)`）都捕获 `this`，
  只有自举 IR 后端漏了。
- 方向：env 多一个槽存接收者；闭包体入口还原成 `%this` 并登记名为 `this` 的局部
  （嵌套闭包继续捕获它）；闭包体恢复 `curHasThis`/`curClass`。
- ✅ 已修（2026-10-20）：`CapturesThis` / `LambdaNeedsThis` / `ThisReceiver` /
  `ReceiverAvailable`（`irgen_expr.zan`）。判据照抄 C 宿主：不是局部变量且是本类
  的实例字段/实例方法才需要 `this`，且**要递归进嵌套闭包**（否则外层不捕获、
  内层引用未定义的 `%this` → 非法 IR）。用例
  `tests/conformance/closure_captures_this.zan` 钉住。
- 边界：接收者按**借用**语义处理（闭包体入口 retain、出口 release，净值为 0），
  与自举既有的"捕获局部靠外层帧持有引用"一致；闭包在方法返回后才被调用时，
  接收者可能已经没了（C 宿主靠闭包持有强引用保活）。

### [ ] A396 委托调用的实参在分支臂里求值——clang 的 X86 指令选择器崩溃

- 现象（2026-10-20 差分门实测）：`value_enumerable` / `linq_query` 两个用例
  编译到 IR 没问题，但 clang 在
  `Running pass 'X86 DAG->DAG Instruction Selection' on function '@__lambda_3'`
  阶段 0xC0000005 崩溃（LLVM 19.1.5 与 mozbuild LLVM 20.1.8 都复现，IR 通过验证器）。
- 根因：`EmitDelegateCall` 的实参在"堆闭包 / 裸函数指针"两条**分支臂里**求值，
  于是「实参本身是委托调用」时，内层调用的分支+phi 被塞进外层分支的臂里——
  这个 IR 形状（phi 的前驱块里还有 phi）触发 clang 的 ISel 崩溃。
- 方向：实参先求值成临时值，再分支；两条路复用同一批临时值。
- ✅ 已修（2026-10-20）：实参提到分支之前求值。顺带修掉"实参被求值两次"。
  用例 `tests/conformance/closure_nested_call.zan` 钉住；两个 GAP 随之闭账。
- 排查法：clang 在 ISel 崩时，把出问题的函数单独抠出来编译，再逐块删指令二分。

### [ ] A397 闭包的 fn 槽是 `i8*`：直接当函数指针调是非法 IR

- 现象：闭包记录是 `{i8* fn, i8* env}`，`EmitDelegateCall` 取出的 `fn` 是
  `i8*`，却直接 `call i64 (i8*, i64) %fn(...)`——被调值类型与函数指针类型不符，
  **非法 IR**。clang 对 `.ll` 输入默认 `-disable-llvm-verifier`，非法 IR 被静默
  送进后端。
- 方向：调用前 `bitcast i8* %fn to <函数类型>*`。
- ✅ 已修（2026-10-20）：闭包路径与裸函数指针路径都先 bitcast（虚方法槽那条路径
  本来就是对的，两处要一起看）。

### [ ] A398 C 宿主：`Current` 是**字段**的可枚举，检查器收下了但 irgen 不读——循环静默不执行

- 现象（2026-10-20 探针实测，探针在 `_scratch/sd/p_enum4.zan`）：
  ```zan
  class FieldEnum { public int Current; public bool MoveNext() { ... } }
  ```
  - C 宿主：`foreach` **一轮都不跑**（没有报错，也没有输出）——检查器
    （`checker.c:3360`）把字段 `Current` 也认成元素类型来源，但 irgen
    （`irgen_stmt.c:2907`）只会 `call` 方法 `Current()` 或属性 getter，
    两者都没有 → 取不到元素。
  - 自举：正常迭代（自举的 `GenForeachEnumerable` 直接读字段）。
- 影响面：只有"`Current` 写成字段"这一种写法；LINQ 的枚举器用的是**属性**，
  所以现有用例都没碰到。
- 方向：要么在 C 宿主的 irgen 里补字段分支（与检查器的接受口径对齐），
  要么在检查器里拒绝这种写法（fail-closed，别静默）。
- ⚠ 这是 **C 宿主**的缺口，不是自举的：差分门**不收录**该形态（收录就会红），
  自举保持"能跑且正确"。

### [ ] A399 自举：`T[]`（泛型数组）的字段槽两端类型不一致——存端打包成 i64、读端按 i64*

- 现象（2026-10-20 探针实测，探针在 `_scratch/sd/p_enum3.zan`）：
  ```zan
  class Box<T> { T[] buf; public Box(List<T> xs) { buf = new T[xs.Count]; } }
  ```
  - 症状：`buf.Length` 读成 0（闭包里取到的数组指针丢了）。
- 根因：`MapType` 先判"是否被擦除"再判"是否数组"，`T[]` 的元素类型是类型参数
  → 整个数组被降成 i64 槽；而 `IsErasedType` 又把数组算成擦除类型，于是
  **存端**按 i64 打包、**读端**按 i64\* 读，两端对不上。
- 方向：数组先于擦除判定（`MapType`），且 `IsErasedType` 排除数组。
- ✅ 已修（2026-10-20）：`MapType` 的数组判断提到擦除判断之前，
  `IsErasedType` 对 `t.ival >= 1`（数组）返回 false。探针 p_enum3 现在与
  C 宿主逐字一致（`p=8`）。

### [ ] A400 自举：`foreach` 不支持可枚举协议（GetEnumerator/MoveNext/Current）

- 现象：自举的 `foreach` 只认 `List<T>` / `T[]` / `string`，其余集合一律
  **报诊断拒绝编译**；LINQ 的迭代器（`ValueWhereIterator` 等）因此整个用不了，
  差分门里 `value_enumerable` / `linq_query` 长期是已知缺口。
- 方向：按 C 宿主的协议降级——`coll.GetEnumerator()` → 枚举器，
  `enumerator.MoveNext()` → bool，`enumerator.Current` → 元素。
  自举的泛型是**完全擦除**的，所以 `Foo<T>.GetEnumerator()` 不需要特化，
  直接走既有的实例调用路径即可。
- ✅ 已修（2026-10-20）：`irgen_stmt.zan` 新增 `GenForeachEnumerable`，
  `GenForeach` 在非 List/数组/字符串时先试它、失败才报原诊断。
  `Current` 支持**属性**（`get_Current`）与**方法**（`Current()`）两种形态
  ——与 C 宿主 irgen 的口径一致（字段形态是 C 宿主的缺口，见 A398）。
  用例 `tests/conformance/enumerable_protocol.zan` 钉住。

### [ ] A401 自举：认不出的调用**静默返回 0**——违反 fail-closed

- 现象（2026-10-20 探针实测，探针在 `_scratch/sd/p_unknown.zan`）：
  ```zan
  Console.WriteLine(Task.Delay(80));
  ```
  - C 宿主：编译期报错（`unresolved call 'Task.Delay'`）。
  - 自举：IR 里是 `printf(@.fmt.nl, i64 0)`——**一个字诊断都没有**，程序
    拿着错的值继续跑。
  - 根因：`Task` 是 C 宿主的**内建**（`src/compiler/builtin_api.c`），不在
    标准库的 Zan 源里，自举的 binder 不认识它 → `GenCall` 落到兜底分支
    `return new ExprVal("0", MkType("int"))`。
- 影响面：不止"接收者类型不认识"这一条——字符串/字典/StringBuilder 的**未知
  方法**也静默降级（字符串返回**接收者本身**，字典返回 0）；被调用者既不是
  成员访问也不是名字（`filters[f](item)`）也静默返回 0。
- 方向：这些位置全部改成**报诊断**；诊断发在**降级点**（irgen），因为只有
  降级点知道"自举到底会不会降级这个调用"（检查器对"接收者类型未知"一律放过）。
- ✅ 已修（2026-10-20）：`irgen_expr.zan` 的 `GenCall` / `EmitStrMethod` /
  `EmitDictMethod` / `EmitSbMethod` 全部 fail-closed；接收者名字解析不出来时
  报 C 宿主同款措辞（`unresolved call 'X.Y': 'X' is not a known variable, type,
  or namespace`）。两个 reject 用例钉住边界：
  `tests/selfhost/reject/unsupported_builtin_call.zan`、
  `tests/selfhost/reject/unresolved_call.zan`。

### [ ] A402 自举：不认识 C 宿主的**编译器内建**——标准库自己就在用，全是错的

- 现象：把 A401 改成 fail-closed 之后，标准库自己编不过了——报的是
  `call to 'String.CompareOrdinal' is not supported`、`call to 'b.ToStr' is not
  supported`、`string method 'ToBytes' is not supported`。这三类调用**自举一直
  在错编**（静默返回 0 / 返回接收者），只是没人跑到。
- 根因：它们是 **C 宿主后端里的内建**，不是标准库方法：
  `String.CompareOrdinal(a,b)` → `strcmp`（`irgen_call.c`）；
  `string.ToBytes()` / `byte[].ToStr(off,len)` → 零拷贝桥接
  （`irgen_expr.c` 的 `emit_bytes_call`）。标准库只是调用方（`Convert.zan`、
  `StringExtensions.zan`、`Text/Encoding.zan` …）。
- 方向：自举按 C 宿主的口径补齐这三个内建。窗口要**钳制**（off 夹到
  `[0,len]`，长度夹到 `[0,len-off]`）；自举的数组是**每元素一个 8 字节槽**，
  字节要逐个打包/拆开（不能整块 memcpy）。
- ✅ 已修（2026-10-20）：`irgen_expr.zan` 新增 `StrToBytes` /
  `ByteArrWindowToStr`，`String.CompareOrdinal` 落到 `strcmp`；用例
  `tests/conformance/bytes_bridge.zan` 钉住（自举与 C 宿主逐字一致）。

### [ ] A403 自举：调**委托类型的表达式**（`filters[f](item)`）静默返回 0

- 现象：LINQ 包 `packages/Zan.Linq/src/System/Linq/Stream.zan` 里的
  `this.filters[f](item)`（谓词存放在列表里，调用列表元素）自举**编不过**——
  被调用者是**索引表达式**，既不是成员访问也不是名字。改 fail-closed 之前，
  它静默返回 0 → 谓词永远返回"假" → 过滤结果全错。
- 方向：被调用者既不是成员访问也不是名字时，**只有委托类型的表达式**能降级
  （复用委托调用路径 `EmitDelegateCallVal`），其余报诊断。
- ✅ 已修（2026-10-20）：`EmitDelegateCall` 拆成"求值 + 调用"两半
  （`EmitDelegateCallVal`），`GenCall` 的兜底分支先求值再判是不是委托类型。
  差分门里 `value_enumerable` / `linq_query` 两个用例从"编不过"变回通过。

### [ ] A404 自举：认不出的**标识符**与**成员**仍静默返回 0
- 现象：`GenIdent` 末尾 `return new ExprVal("0", MkType("int"))`、
  `GenMember` 末尾同样。也就是 `int x = undefinedName;` 在自举里是 `x = 0`，
  `obj.NoSuchField` 也是 0。
- 方向：与 A401 同口径——报诊断。但要小心：自举的**类型推断**偏弱
  （注释里写过"自举常常留一个标识符的值类型没解析出来"），照搬"解析不出来就
  报错"会把合法程序也拒掉，所以要先摸清自举在哪些地方真的拿不到类型。
- ⚠ 尚未开工：A401 只覆盖了**调用**路径，标识符/成员访问两条路还开着。
- ✅ 已修（2026-10-20）：`GenIdent` / `GenMember` 的兜底改成诊断。
  探针实测：**常见情形检查器早就拦住了**（`undefined identifier 'X'`、
  `type 'X' has no member 'Y'`，与 C 宿主同款拒绝），所以这条兜底是
  "检查器与后端不一致"时的**背靠背安全网**，不是日常路径——固定点门
  （自举编自己的 62 个文件）与差分门（27 个用例 + 标准库）证明它平时不触发。
  真正被它挖出来的是 A405。

### [ ] A405 自举：不支持**静态属性**（`Cls.Prop`）——标准库 `Encoding.UTF8` 就是它

- 现象（2026-10-20 差分门实测）：把 A404 改成 fail-closed 之后，标准库
  `stdlib/System/Text/Encoding.zan:137` 报 `unresolved identifier 'Encoding'`：
  ```zan
  public static Encoding ASCII { get { return Encoding.UTF8; } }
  ```
  `Encoding.UTF8` 是**静态属性**，自举的 `GenMember` 只认静态**字段**、枚举成员、
  静态方法组——静态属性落到兜底 → 之前**静默返回 0**（`Encoding.UTF8` 拿到 0，
  编码整个是错的），现在报诊断。
- 方向：静态属性就是**静态 getter 方法**（解析器把属性拆成 `get_Prop`），
  按 C 宿主的口径降级成 `Cls.get_Prop()` 的静态调用。
- ✅ 已修（2026-10-20）：`GenMember` 的静态分支加一条"找静态 `get_<name>`"
  （`FindMethodUp` 会走基类链）；用例 `tests/conformance/static_property.zan`
  钉住（自举与 C 宿主逐字一致）。

### [ ] A406 自举：内建表的能力没测过——先量出来，再按矩阵补齐

- 现象（2026-10-20）：C 宿主把一批 API 声明成**内建**
  （`src/compiler/builtin_api.c` 的 `builtin_types[]` / `members_*[]`），
  自举后端要么自己降级、要么报诊断拒绝——但"哪些能降级"**从来没量过**，
  全靠撞。A401 之后更明显：不支持的调用会报诊断，可诊断只会告诉你
  "不支持"，不告诉你**一共还差多少**。
- 方向：写一个按内建表**自动生成探针**的测量脚本（参数类型从签名取，
  只编译不运行），把结果落成能力矩阵文档；然后按矩阵分组补齐。
- ✅ 已修（2026-10-20）：
  - 测量脚本 `scripts/selfhost_builtin_sweep.ps1`，矩阵文档
    `docs/selfhost-builtins.md`。
  - 按矩阵补齐了一批：`Math` 全家（14 个）、`string.Equals`、
    `List<T>.ToArray`、`Dictionary.Clear`、`StringBuilder.Clear`；
    用例 `tests/conformance/builtin_surface.zan` 钉住（逐字一致）。
  - 剩下的缺口记在文档里：`Task.*`（要协程帧，见 A385）与 `Console` 的
    颜色/标题属性。

### [ ] A407 自举：double 的**文本格式**与宿主不一致（`%g` vs 最短往返）

- 现象（2026-10-20 探针实测）：`Convert.ToString(Math.Acos(0.0))`
  - C 宿主：`1.5707963267948966`
  - 自举：`1.5708`
  根因：自举用 `printf("%g")`（六位有效数字），宿主用运行时的
  `zan_rt_dbl_str`（**最短往返**格式，`src/runtime/rt_timer.c`）。
  凡是打印 double 的程序，自举与宿主的输出都不一样。
- 方向：自举的 double→文本改走运行时的 `zan_rt_dbl_str`（跟宿主同一份
  格式代码），`Console.WriteLine(double)` 与 `ToCStr(double)` 两处都要改。
- ✅ 已修（2026-10-20）：`irgen_expr.zan` 新增 `DblToStr`（调
  `@zan_rt_dbl_str` 再包成托管串），`ToCStr` 与 `Console.WriteLine` 的
  double 分支都改走它；`builtin_surface` 用例里 `acos=1.5707963267948966`
  与宿主逐字一致。

### [x] A408 HTTP/2 帧 API 在仓库里根本不存在——三个类型一个都找不到

- 现象：`conformance_http2_frame_test` 与 `conformance_protocol_audit_harden`
  的 http2 段编译失败，报 `cannot resolve member 'REFUSED_STREAM'` /
  `'CANCEL'`，并最终 `error: code generation failed`。
- 事实：全仓 grep（stdlib + packages + tests）**找不到** `Http2Frame`、
  `Http2Error`、`Http2FrameType` 任何一个类型的定义——用例引用的是尚不存在的
  API，不是"被收窄"（与 A39x 系列的可见性误伤不同类）。
- 方向：(1) 实现 HTTP/2 帧编解码最小集（帧头 9 字节 + RST_STREAM 构造 +
  帧类型枚举 + 错误码枚举）并让两个用例引用它；(2) 若判定 HTTP/2 不在本期范围，
  把两处用例的 http2 段摘掉并把帧 API 登记为未实现（与 A381 的清单同口径）。
- ⚠ 不要用"给用例补个桩类型"绕过：那是让用例测一个假实现。
- ✅ 已修（2026-10-20）。复查发现登记时的结论**已经过期**：帧编解码其实早就
  落地了（`packages/Zan.Net/src/System/Net/Http/Http2Frame.zan` +
  `Models/Http2FrameType.zan`、`Models/Http2Flags.zan`、`Models/Http2Settings.zan`），
  真正缺的是**错误码类型**——`Models/Http2Error.zan` 这个文件存在，**但文件
  是空的**（只有 `using System;` + `namespace System.Net.Http;`），类型从没被
  写出来；连带 `Http2Flags.zan` 末尾还挂着一句本该属于它的注释
  （"HTTP/2 标准错误码（RFC 7540 §7）"）——清理机械模板注释那轮把这段内容
  冲掉了，只剩壳。
- 修法：按 RFC 9113 §7 补齐 `Http2Error`（14 个错误码常量，NO_ERROR=0x0 …
  HTTP_1_1_REQUIRED=0xd），并把 `Http2Flags.zan` 末尾的错位注释删掉。
  不是给用例补桩——`RST_STREAM`/`GOAWAY` 帧本来就按 RFC 编码错误码，这里补的
  是缺失的标准常量表。
- 验证：`zanc` 单独编译两条用例后逐行比对基准**完全一致**
  （`http2_frame_test` → `HTTP/2 framing test passed!`；
  `protocol_audit_harden` → `ALL_PROTOCOL_AUDIT_OK`）。

### [x] A409 X509 证书匹配"无 SAN 时回退 CN"与用例口径不符

- 现象：`conformance_x509_certificate` 其余 20+ 项检查全绿，只有一项红：
  `FAIL Fallback to CN when no SAN`（程序 exit=1，failures=1）。
- ✅ 已修（2026-10-20）。先前的登记判断有误（当时写成"现代实现不再回退 CN，
  应改用例"），实测定位后按**主流口径**改实现，两处根因：
  1. **CN 匹配被当成 DNS 名**：CA 证书（`CA_CERT`）实测扩展只有
     2.5.29.14/2.5.29.35/2.5.29.19，**根本没有 SAN**；它的 CN 是
     `Zan Test CA`——带空格的**标识符**，不是 DNS 名。`MatchPattern` 对 host
     套了 LDH 域名规范化（`DnsCanonical`），空格直接判否，于是"CN 回退"对
     真实证书形同虚设——而文件自己的注释写的是"若无 SAN 则回退至 Subject CN"，
     实现与自带文档矛盾。
  2. **降级禁令的判据用错**：代码用 `hasSan`（SAN 扩展是否存在）判断"禁止回退
     CN"，但 RFC 6125 §6.4.4 的判据是"SAN 里**含 dNSName 条目**"；扩展里只放
     email/URI 时 CN 仍是合法回退来源。
- 修法（对齐 Go `crypto/x509`、Java `HostnameChecker`、OpenSSL 的主流行为）：
  - `X509Certificate.VerifyHost` 的门控改成"有 dNSName 条目才禁回退"；
  - 新增私有 `MatchCommonName`：CN 与 host 都是合法 LDH 域名形态才走通配/单级
    匹配，否则退化为**大小写不敏感的精确标识符比对**（Go 的 `matchExactly`/
    Java 的 `equalsIgnoreCase` 同款），不再套 DNS 规范化；
  - 删掉因此不再需要的 `hasSan` 字段。
- 用例：新证书 `SAN_NO_DNS`（SAN 扩展只含 email 条目）钉住"无 dNSName 条目时
  允许 CN 回退"，`tests/conformance/x509_certificate.zan` 增 5 条断言 +
  基准同步。验证：`zanc tests/conformance/x509_certificate.zan --auto-stdlib`
  编译后逐行比对基准**完全一致**（exit=0，failures=0）。

### [x] A410 MVC 菜单有条目的 icon 解析为空

- 现象：`conformance_web_menu_attrs` 输出与基准差两处：期望
  `icon=mdi:antenna`，实际 `icon=-`（文章 `/admin/menuposts`、首页 `/menuhome`
  两条），其余字段（active/路径/标题）都对。
- 根因：`5b2bf946`（纯"文件规范化"重构）把带构造参数的专用 `CustomAttribute`
  换成通用名值对 `CustomAttribute(name, value)` 时，丢掉了 `Icon` 的构造默认值
  `mdi:antenna`（见 `a085f541:stdlib/System/Web/Attributes.zan:94`）。通用名值对
  特性按 key 存值，没有"某个 key 的默认值"这个位置 → 没写 `Icon` 的菜单项
  `r.meta.Get("Icon")` 返回空串。基准自 `a085f541` 起就期望 `mdi:antenna`，
  从那次重构起一直红。
- ✅ 已修（2026-10-20）：
  - `MenuBuilder` 加 `public static string defaultIcon = "mdi:antenna"`，并在
    `ForUser` 里对空 icon 兜底（`MenuBuilder` 是 icon 的唯一消费方，缺省归位
    到这一层最合适）；显式写了 `Icon` 的条目不受影响。
  - 顺带查出**另一处**独立差异（与 icon 无关）：匿名用户看到 0 条菜单而不是基准
    的 1 条。根因是 10-09 的 fail-closed 策略（`GenRoute`：未声明
    `Authorization` 的端点一律要求登录）让"隐式公开"的 `MenuHomeController`
    对匿名隐身，而基准仍期望匿名可见——用例没跟上这次有意的安全策略改动。
    修法是让用例把意图**显式**写出来（`[Custom(Authorization =
    CustomAuthorization.None, IsMenu = true)]`），基准保持不动（匿名仍应看到
    首页这条公开菜单项），而不是把基准改成"匿名 0 条"。
  - 验证：`zanc tests/conformance/web_menu_attrs.zan --auto-stdlib` 编译后逐行
    比对基准**完全一致**（exit=0）。

### [ ] A411 `conformance_clipboard_roundtrip` 在无交互窗口站点的会话里恒红

- 现象：用例输出 `clip-ok: 0`（`bad != 0`），其余 19 条全绿。
- 根因：**环境**——本会话没有交互窗口站点的剪贴板，实测 PowerShell
  `Set-Clipboard` 返回成功但 `Get-Clipboard` 读回空串；用例第一步
  `Clipboard.GetText()` 之后的每一次读写都落在同一个坏剪贴板上。
  注意同批的 `conformance_clipboard_uri_roundtrip` 是绿的——它只测
  `FileUri`/`PathFromUri` 编解码，不真碰剪贴板。
- 方向：用例前置判定剪贴板可用（不可用就跳过并打印 skip 理由），否则在
  CI/agent/无桌面会话的机器上恒红；真要在无头环境测，得走可注入的剪贴板后端。

### [x] A412 三条 conformance 用例运行期 Access violation（崩溃，不是断言失败）

- 现象：`conformance_ternary_is_type`、`conformance_cast_string_object`、
  `conformance_cs_b05_pattern_var` 三条程序退出码 1，报 `program exited with
  Access violation`（0xC0000005）——是**崩溃**，不是断言失败。
- 已排除的可能：不是编译器改动带出来的。把 `src/compiler/optimizer.c` 的去虚拟化
  边界检查临时换回 `789642b0^` 的版本、重建 zanc 后，三条**仍然同样崩**；且这三个
  程序全文没有 `Task.` / `async` / `await`，够不到 Task 句柄定型与 async resume
  两条改动面。崩溃日志只记了 run/exit 行（`logged=0`），没留下栈。
- 现象细分：`ternary_is_type` 在**打印任何一行之前**就崩；`cs_b05_pattern_var`
  打印了若干行后才崩——像是某个类型转换 / 模式匹配的运行期路径。
- ✅ 已修（2026-10-20）。取栈 + 最小探针定位到**同一条根因**，三条用例
  都是被它带崩的：
  - 崩溃日志（`<exe_dir>/zan_crash.log`）给出关键事实：`code=0xC0000005`
    `access=write`，**写入目标落在 exe 映像的只读段**（`addr=exe+0x3A1B0`），
    而寄存器里的对象指针是 `exe+0x3A1C0`（正好是目标 +16）——写的是
    "对象前 16 字节的引用计数头"（`ZAN_OBJ_RC_OFF = -16`），对象本体却是
    **映像里的静态数据**，不是堆对象。
  - 最小探针二分：`object o = "hi";` 这一句就崩（`p1`）；换成
    `string s = "hi";` 一切正常（`p3`）。三条用例的共同点正是
    `object o = "字面量"`。
  - 根因：字符串字面量是**编译期常量**，引用计数用哨兵值
    `ZAN_STRING_SENTINEL_RC`（immortal，不参与计数）。运行时三条 ARC 路径
    里，`zan_rt_str_retain`（irgen.c）与 `zan_rt_release_dyn`（irgen.c）都
    判了哨兵，唯独**通用**的 `zan_rt_retain` / `zan_rt_release` 没判——
    而"字面量存进 `object` 槽"走的正是通用 retain（静态类型是 object，
    不是 string），于是对着只读段做原子加一 → 访问冲突。
- 修法（`src/compiler/irgen.c`）：通用 `zan_rt_retain` / `zan_rt_release`
  读一次引用计数，命中哨兵就跳过（与另外两条路径同口径）；堆对象与类实例
  的计数路径不变。
- 用例：新增 `tests/conformance/arc_immortal_object_slot.zan` 钉住"字面量进
  object 槽不崩 + is/强转正常"，并反向锁住"堆字符串与类实例进 object 槽
  仍走正常引用计数"。
- 验证（A/B 举证，同一份用例源码）：
  - 旧编译器 `build/zanc.exe` → exit `-1073741819`（0xC0000005，复现崩溃）；
  - 修复后 zanc → exit 0、`failures=0`；
  - 原三条用例（`ternary_is_type` / `cast_string_object` / `cs_b05_pattern_var`）
    修复后逐行比对基准**完全一致**。
  - 注：验证用的 zanc 是在 `_scratch/build_a412` 独立目录构建的——当时另一
    会话正跑 conformance 整档（共用 `build/zanc.exe`），不换共享二进制以免
    打断他们（仓库规则 8）。

### [ ] A413 自举：`Console.*` 只有一条 blanket catch——`Clear` 打印 "0"、`ReadLine` 返回垃圾

- 现象（2026-10-20 差分门探针实测，探针在 `_scratch/sd/p_console.zan` 等）：
  - C 宿主 `Console.Clear()` 输出 `ESC[2J ESC[H`、`ResetColor()` 输出 `ESC[0m`；
    自举两者都打印 `0`。
  - C 宿主 `Console.WriteLine(true)` → `true`、`WriteLine('x')` → `x`；
    自举 → `1` / `120`。
  - C 宿主 `Console.ReadLine()` 返回读到的行（含 `\r\n`）；自举返回垃圾，
    `line.Length` 直接访问冲突崩（exit -1073741819）。
- 根因：自举 `irgen_expr.zan` 里 `Console.*` 只有**一条兜底分支**——"取第一个
  实参，把它打印出来"。于是每个成员都被当成"打印第一个参数"：`Clear()` 打印
  `0`，`ReadLine()` 打印 `0` 并返回 0。
- 方向：照 C 宿主 `irgen_call.c` 的口径**逐个成员**降级（按实参静态类型分流
  `%s` / 最短往返 / `true`-`false` / char 的 UTF-8 / `%llu` / `%lld`；
  `Clear`、`ResetColor` 发 ANSI 序列；`ReadLine` 用 `fgets` 并回写精确长度；
  `Read`、`ReadKey` 读一个字符）。
- ✅ 已修（2026-10-20）：`irgen_expr.zan` 新增 `GenConsoleCall`（逐成员分流）、
  `CharToStr`（char → UTF-8 文本），删掉 blanket catch；`irgen.zan` 加两个 ANSI
  常量。用例 `tests/conformance/console_builtin.zan`（各类型渲染）与
  `tests/conformance/console_ansi.zan`（ANSI 字节）钉住，都进了差分门。

### [ ] A414 C 宿主自身不自洽：char 字面量只留 UTF-8 首字节，bool 的渲染看"类型推不推得出来"

- 现象 1（char 字面量）：宿主 `lexer.c` 的 `lexer_escape_char` 取
  `zan_utf8_encode` 结果的**第一个字节**——`'\u4e2d'` 存成 0xE4 而不是码点
  0x4E2D；可宿主的 codegen `emit_char_to_cstr` 又是按**码点**做完整 UTF-8
  编码。词法与代码生成两半不自洽（`docs/ABI.md` §3.1 明确 char 是 Unicode
  scalar）。自举一直是对的。
- 现象 2（bool 的渲染）：宿主渲染 bool 靠 `expr_is_bool`（**静态类型**），而
  irgen 的 `infer_expr_type` 对**内建类型的实例调用**（`list.Contains(x)`）和
  **原始类型名上的静态内建调用**（`string.IsNullOrEmpty(x)`）推不出返回值 →
  同一个程序里 `Console.WriteLine(true)` 打印 "true"，
  `Console.WriteLine(list.Contains(x))` 打印 "1"。
- 方向：修**宿主**——词法层保留码点（新增 `lexer_escape_char_cp`）；类型推断
  在内建类型上查内建表（`builtin_api.c` 是权威：内建类型没有 binder symbol，
  符号表答不了"这个成员返回什么"；`string` 的静态成员在静态类 `String` 下，
  别名要一并映射）。
- ✅ 已修（2026-10-20）：宿主两处都修好；受影响的 golden 重生成并逐行看过
  （`lang_fixes`、`list_wide_struct_elements` 的 bool 行从 "1"/"0" 变
  "true"/"false"）。
### [ ] A415 自举：内建的**属性**（Console 颜色/标题）不降级

- 现象（2026-10-20 能力矩阵实测）：`Console.ForegroundColor` /
  `Console.BackgroundColor` / `Console.Title` 在内建表里是属性（`'P'`），
  自举**不降级**——能力矩阵把它们归到"内建属性"一栏。
- 根因：自举的 `GenMember` / `StoreTo` 只认**调用**与**字段/属性 setter**，
  没有内建属性这条路径。
- 方向：照宿主的口径补——读＝读模块级颜色槽（缺省前景 7 / 背景 0）；
  写＝存 `color & 15` 并发 `ESC[<code>m`（`(idx<8?30:90)+4*b0+2*b1+b2`，
  背景 +10）；`Console.Title` **只可写**（`ESC]0;<title> BEL`），宿主没有
  getter，读它在宿主那边就是编译错误。
- ✅ 已修（2026-10-20）：`irgen_expr.zan` 新增 `ConsoleColorGet` /
  `ConsoleColorSet` / `ConsoleTitleSet`，`GenMember`（读）与 `StoreTo`（写）
  两处都挂了；用例 `tests/conformance/console_color.zan` 钉住（字节逐字一致）。