# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

### [ ] A384 自举 IR 后端没有运行期类标识——`x is T` / switch 模式判不了

- 现象（2026-10-20 差分门 `ctest -R selfhost_diff` 实测）：同一个程序，C 宿主
  编译运行正确，自举编译器（gen1）要么拒绝编译、要么结果不同：
  - `tests/conformance/cs_b05_pattern_var.zan`（`case Dog d when ...` / `case string s`
    打在 `object` 类型的被匹配值上）→ 自举报诊断拒绝；
  - `cs_b06_switch_expr.zan`（switch **表达式**的 `Circle c when ... => 10`）→ 同上；
  - `pattern_value_types.zan`（`case Dog d` 打在基类 `Animal` 上）→ 同上。
- 根因：C 宿主用**分配点 site id**（对象头 obj-8）+ `__zan_site_tynames` 祖先名表
  做运行期类型判定（`src/compiler/irgen_expr.c:10603` `emit_runtime_is_check`）；
  ngen 后端用 obj-8 高半部的 site id（`src/selfhost/ngen.zan:8039`
  `EmitTypeTestNg`）。**自举 IR 后端（`src/selfhost/irgen*.zan`）两者都没有**：
  它的对象头只有 `[-16]=rc`、`[-8]=dtor`（`irgen.zan` 的 `EmitArcRuntime`），
  字符串更是裸 `malloc` 的 C 串（无头、无标记）。没有类身份，`object` 槽里的值
  在运行期无从判断是哪个类、是不是字符串。
- 现状处理：本轮已把"判不了"从**静默错答案**改成 **fail-closed 诊断**
  （`irgen_expr.zan` 的 `EmitTypeTestVal`），并在
  `tests/selfhost/diff_known_gaps.txt` 登记为已知缺口。功能上仍然缺。
- 方向（实现要点见 `.agents/skills/zan-compiler-internals/references/selfhost-closure.md`
  §9）：给对象头加第三个槽（obj-24 放类身份），类实例化时写入该类的 tid 全局地址
  （`@__tid_X` 的值指向基类 tid——`irgen.zan` 的 `EmitTid` 已经是这条链，catch 匹配
  就在用它），再加一个 `__zan_is_class(obj, tid)` 沿链走；字符串必须同样带上头，
  否则读 obj-24 是越界读，`object` 槽里的字符串会误判成类实例。
- ⚠ 改对象头会影响自举固定点的 gen2 运行时包（`zanrt_sh_*`），改完必须重跑
  `selfhost_fixed_point`；同时要把 `cs_b05_pattern_var` / `cs_b06_switch_expr` /
  `pattern_value_types` 从 `diff_known_gaps.txt` 删掉，让差分门开始强制它们。

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
- ⚠ 帧布局一变，自举固定点的 gen2/g3 必须重跑（`ctest -R '^selfhost_fixed_point$'`），
  差分门的 `diff_known_gaps.txt` 也要同步。

### [ ] A387 自举 async 方法里的闭包仍按值捕获（同步路径已按引用）

- 现象（2026-10-20 差分门实测，新用例 `tests/conformance/closure_capture_async.zan`）：
  同一个程序，C 宿主输出 `f=2`，自举编译器（gen1）输出 `f=1`：
  ```zan
  static async void Main() {
      int x = 1;
      Func<int> f = () => x;   // 闭包捕获 x
      x = 2;                   // 捕获之后再写
      await Task.Delay(1);
      Console.WriteLine(f());  // C 宿主 2（按引用），自举 1（按值快照）
  }
  ```
- 根因：同步路径的装箱（局部变量住进堆 cell，闭包记录存 cell 指针）已经做完，
  但 **async 方法的局部变量不住在栈上**——它们住在协程帧里（`GenAsyncMethod` 的
  `PrescanAsyncLocals` + 帧槽，跨挂起点存活的值都进帧）。自举的装箱路径
  （`irgen.zan` 的 `BoxLocal`）发的是"栈 alloca + cell 指针"，async 路径没有对应的
  形态：`GenAsyncMethod` 生成局部变量时**不开箱**，所以 async 里的闭包仍然拿到
  按值快照的 env。
- 现状处理：登记为差分门已知缺口（`tests/selfhost/diff_known_gaps.txt` 的
  `closure_capture_async`），门会持续报出来。同步路径的同一语义已由
  `tests/conformance/closure_capture_by_ref.zan` 强制。
- 方向：让 async 帧槽能装 cell 指针——
  1. async 方法里的局部变量若被闭包捕获，也要装箱：帧槽存 **cell 指针**（i64），
     而不是变量值；resume 时由 cell 指针重算 payload 指针（payload 指针指向
     堆 cell，跨挂起稳定）。
  2. `asyncSlotReg` / `EmitAsyncReloadSlots` 的溢出与回填要按"存 cell 指针"来做，
     而不是按 `MapType` 的值类型存。
  3. 闭包创建点（`GenLambda`）在 async 方法里拿到的也应是 cell 指针。
- ⚠ 动 async 帧布局，必须重跑 `selfhost_fixed_point`（gen2/g3 都要过）与差分门；
  另外 async 帧布局本身还与 C 宿主的 `zan.co.header` 不一致（A385），两者要一起看。

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

### [ ] A390 自举的「数组 → string」是复制打包，不是共享 view；含 NUL 的子串搜索仍按 C 字符串

- 现象（2026-10-20 修完 string 长度语义后实测）：
  - `byte[] raw; string v = raw;` 在自举里是**复制**（把每个元素的低 8 位
    拷进新的托管串），C 宿主是**共享 view**（改 `raw[0]`，`v[0]` 跟着变）。
    原因：自举的数组是「每元素一个 8 字节槽」，不是紧凑字节，没法直接
    当字符串指针共享。
  - `Contains` / `IndexOf` / `StartsWith` / `EndsWith` / `Split` / `Replace`
    / `LastIndexOf` 仍用 `strstr` / `strncmp` / `strcmp`：**子串本身含 NUL
    时**结果不对（长度已经对了，内容搜索还是 C 字符串语义）。
- 方向：
  (1) 数组 → string 要真正共享，得让自举的 `byte[]` 变成紧凑字节布局
      （每元素 1 字节）——这是数组表示级改造，会波及所有数组代码；
  (2) 或者承认自举的数组是 8 字节槽，把「共享 view」在文档里记为**已知
      语言分歧**，只保证长度与内容正确；
  (3) 含 NUL 的子串搜索改成按长度比较（`memcmp` + 显式长度）——工作量小，
      与 (1)(2) 无关，可以单独做。
- ⚠ 动数组布局会动自举固定点的 gen2 运行时包，改完必须重跑
  `selfhost_fixed_point` 与差分门。
