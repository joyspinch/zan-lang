# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

### [ ] A495 八条 conformance 用例在 `--check-leaks` 下报泄漏——所有权欠账浮出

- 现象（2026-10-22 合并测试孪生档时浮出）：这八条用例在 `--check-leaks` 下报
  "memory leak detected"，对象在退出时仍可达——
  `closure_cycle_warning`（4 个）、`null_guard_order`（1 个，
  `conformance/null_guard_order.zan:10`）、`oss_storage`（27 个）、
  `designer_html`（5256 个，JsonValue / PropertyGrid / Designer 一片）、
  `designer_prepared_async`、`game_data_table`、
  `gui_declarative_html_modern_test`、`sse_sink_take_co`。
- **不是插桩带来的假阳性**：`--check-leaks` 单独编译也报同样的数，
  `--arc-guard` 单独编译不报（`oss_storage` 在 `--check-leaks` 单独编译下还会
  崩，0xC0000005；双插桩下不崩）。普通版（无插桩）输出与 golden 逐字节一致
  ——功能面是好的，漏的是所有权。
- 为什么现在才看见：旧世界里这八条只红在 `leakcheck_*` 孪生档上，而那条档属于
  full 档，是发布门控、没人常规跑（AGENTS.md 规则 8）。2026-10-22 把四个孪生
  档合成一条 `case_*`（一次插桩编译、一次运行同时给 conformance/泄漏/ARC 三
  个结论）后，泄漏结论进了 standard 档，这八条当场浮出来。
- 现状：这八条按仓库既有机制（孪生档的 `_known_leaky` + WILL_FAIL）登记成
  "预期泄漏"——泄漏报告必须出现，修好了就翻红提醒把它从名单里拿掉；golden
  走未插桩的普通版（泄漏报告打在 stdout 上，插桩版比不过 golden）。
- 方向：这是所有权欠账，可能一半在 stdlib 容器/组件（闭包环、JsonValue 与
  Dictionary 的字段持有、Designer 的对象图、SseSink 的缓冲），一半在代码生成
  侧漏了释放，要逐条看站哪边。`closure_cycle_warning` 是**用例故意造环**，
  它的正确归宿是"预期泄漏"而不是"修好"；其余七条该修。
- 关联：`array_local_free` 是另一类——它量的是进程内存，插桩自己的记账
  （几十 MB）把它测的东西搅了（普通版 "sync ok"，插桩版 "sync leaked 25492KB"）。
  它的 golden 也走普通版，泄漏结论照常在插桩版上判。
### [ ] A468 宿主与自举在「`await` 一个非协程操作数」上分叉

- 现象：宿主把 `await <非协程>` **降级成普通求值**
  （`src/compiler/irgen_expr.c:10350-10358`，注释写明"不是帧就原样返回"——
  普通值往往也是 i8 指针，当成帧去读会读出面元指针的垃圾值，而且不报错）；
  自举则**直接报错**：`'await' expects an async call in the self-hosted
  compiler -- this operand is not lowered as a coroutine frame`
  （`src/selfhost/irgen_async.zan:618-632`）。
- 这**不是**宿主缺陷（2026-10-22 复核确认）：判据是"**这个 await 目标需不需要
  真挂起**"，不是"外层方法是不是 async"。await 一个普通值属于**能同步降级**的
  那一类，原样求值就是正确语义；宿主的行为被 golden 用例钉住
  （`tests/conformance/async_await_sync_delegate.zan:23` await 一个**同步委托**，
  文件头注释明写"await 的目标分两类——异步目标与普通目标"）。判据的完整说明见
  `docs/ASYNC_CPS_DESIGN.md` 的 "Where `await` may appear (the criterion)"。
  缺的是自举侧的**表达能力**：自举目前只能降级 async 调用，碰到普通值没有
  对应的"原样求值"形态。
- 方向（择一）：
  1. 自举补上"await 一个普通值 → 原样求值"这条降级路径（与宿主一致，差分门
     才不需要给这类程序开缺口）；
  2. 或者把这条分歧写进 `tests/selfhost/diff_known_gaps.txt` 并注明理由——
     但差分门只比"能编且 stdout 一致"的用例，"宿主能编、自举不能编"这类要
     另开一个 reject 清单条目才看得见。
- 关联：A467（已闭账：宿主补上「await 写在 lambda 里」的诊断）、
  `docs/ASYNC_CPS_DESIGN.md` §"Where `await` may appear (the criterion)"。

### [ ] A464 运行期分配量统计缺失——托管分配没有可读的计数入口

- 现象（2026-10-21 落地 Zan.Bench 时撞到）：基准测试要报"这次操作分配了
  多少字节"（BenchmarkDotNet 的 `Allocated` 列）做不到。`Zan.Bench` 因此
  只能报时间，README 与路线图 §1.24 都写明了这个缺口。
- 根因（三条叠在一起）：
  1. 托管分配的**唯一入口**是编译器生成的 `zan_rt_alloc`
     （`src/compiler/irgen.c` 的 `emit_new_expr` / 装箱路径），它内部调
     `malloc`——想知道"分配了多少"，只能在编译器生成的那一处挂钩子。
  2. 运行时**有**统计能力：`src/runtime/rt_mem.c` 的 `zan_mem_get_stats`
     （slab 数 / slab 字节 / 小块分配次数）与 `__wrap_malloc/calloc/realloc`；
     但 **stdlib 完全没有暴露这组 API**，包层拿不到。
  3. 含 `--wrap=malloc` 的 `zanrt_mem.obj` **只在 `--fast-alloc` 程序里
     链接**（wrap 作用于整个 image，不能只对一部分 malloc 生效），普通程序
     链接的是别的分配实现——统计口径因此不可比、也不总是存在。
- 影响：任何"这次改动是不是多分配了内存"的判断都做不了。ARC 没有 GC，
  分配量比 .NET 更该被观测，缺这一维让基准只能回答"多快"、回答不了
  "占了多少"。
- 方向（择一或组合）：
  1. **编译器侧挂钩**：让 `zan_rt_alloc` 在编译产物里带一个可选的计数钩子，
     配一个**总是参与链接**的运行时对象提供线程局部计数（ARC 下按线程计数比
     全局计数更准，也避免热路径上的原子操作）。
  2. **链接策略侧调整**：让 `zanrt_mem` 总是参与链接并 wrap 掉 malloc，
     再把 `zan_mem_get_stats` 通过 stdlib 暴露出来。
- 关联：A454（真装箱：值类型 → `object` 的堆对象）；`Zan.Bench` 见
  `docs/ECOSYSTEM_ROADMAP.md` §1.24。

### [ ] A465 泛型不能"按类型参数造对象"——没有 `new()` 约束，工厂只能由调用方给

- 背景（2026-10-21 修 A462 时从缺陷转成能力缺口）：A462 的**静默 null** 已修
  （两个后端都在编译期拒绝 `new T()`，措辞 `cannot instantiate a type parameter
  'T'`），但语言仍然**不能**用类型参数造目标对象——想要"由类型参数造对象"，
  只能把工厂（`Func<T>`）交给调用方。`Zan.Mapping` 的 `MapProfile` **目标工厂
  必填**就是这个能力边界的结果，不是库的偏好。
- 现状：`class Maker<T> { T Make() { return new T(); } }` 是**编译期错误**
  （两个后端一致）；`T[] buf = new T[3];` 仍然合法——数组的元素类型在实例化
  时被替换，路径是通的。
- 方向（能力扩展，要动约束系统）：
  1. 给泛型类加**"必须有无参构造器"的约束**（C# 的 `new()` 约束）：约束满足
     时按真实构造器发射 `new T()`；
  2. 或者允许**约束式工厂**（`where T : new()` 之外的形态，例如把工厂作为
     类型参数的一部分传进来）。
- 前提：检查器 / 绑定器要先支持泛型约束（现状见
  `tests/conformance/generic_constraints.zan`），这是一条独立的能力线。
- 关联：A462（已闭账：静默 null → 编译期诊断）、A449 / A454（装箱）。

### [x] A463 诊断里的类型名不渲染可空标志——`bool?` 的错误信息说成 `'bool'`

- 现象（2026-10-21 修 A456 时实测）：`bool? b = null; bool c = !b;` 报
  `cannot apply unary '!' to 'bool': the operand must be a bool`——可空的
  `bool?` 被报成 `bool`，用户看不出自己写的是可空类型。
- 根因：host 的 `type_name()`（`src/compiler/checker.c`）直接返回
  `t->name.str`；可空类型的 `name` 是**元素类型**的名字，`T?` 的 `?` 没有任何
  字段承载，所以所有诊断都把 `bool?` 写成 `bool`。
- 影响面：不止 `!`——凡是拿可空类型当操作数/实参的诊断（`cannot convert
  'bool' to ...`、算术、比较）都会少一个 `?`，用户容易误判成"我写的是 bool"。
- 方向：给 `zan_type_t` 补一个可空标志（或让 `type_name` 走一个
  `type_display()`，按 kind 渲染 `T?` / `T[]` / `List<T>`）。**注意**：`type_name`
  被大量诊断共用，改渲染会动到很多措辞——要么单独加一个只给新诊断用的
  渲染函数，要么一次性把全部诊断的措辞一起改并更新 golden。
- 关联：A456（一元 `!` 的操作数必须是 bool；`bool?` 在布尔上下文里两端口径
  一致地报"不是 bool"）。
- 修法（2026-10-22 落地，编译器侧提交 `df4f022f4`）：**渲染与身份彻底分离**——`zan_type_t.name` 是类型身份
  （列表特判、ARC 散列、mangle、`checker_type_equal` 都依赖它），**一个字都不改**；
  形状（可空 / 数组 / 类型实参）从 kind、element_type、array_rank、type_args 现推。
  - **单一真相源**：`src/compiler/binder.c` 新增 `zan_type_display(t, buf, cap)`
    （声明在 `binder.h`），按形状递归渲染：可空 → `T?`、数组 → `T[]`（多维
    `T[,]`）、有类型实参 → `Name<A,B>`（逗号无空格），否则取 `t->name`；
    NULL 类型渲染成 `?`；装不下时截断但保证结尾 `'\0'`。
  - `src/compiler/checker.c` 的 `type_name()` **签名不变**，内部改走渲染器，结果放进
    一个可移植 TLS 轮转池（`g_type_name_pool[8][192]`，MSVC→`__declspec(thread)`、
    GCC/Clang→`__thread`、否则 `_Thread_local`）。**45 个调用点零改动**——诊断一次
    最多并列 3 个类型名，8 个槽足够；`zan_diag_emit` 立即 vsnprintf 固化进 512B
    msgbuf，结果只需活到该调用返回；LSP 有 diag/worker 两线程，故必须 TLS。
  - 删掉冗余的 `task_type_name()`：`Task` 的**类型实参要渲染**（`Task<double>`），
    因为"cannot convert 'Task' to 'Task<int>'"这条诊断本身要求区分 Task 与 Task<T>；
    `checker_check_assignable` 的 Task 特例改用 `type_name(value)/type_name(target)`。
  - nullable 成员访问诊断不再手写 `'%s?'` + `elem_name`，直接渲染 nullable 自身
    （输出仍是 `bool?`）。
  - 另两处直取 `->name.str` 的诊断点改走渲染器：类型参数实例化、抽象类实例化、
    coll-init 无成员、with 无字段、标量无成员/无方法。
  - `src/compiler/irgen_expr_core.c`：`check_no_boxing_conversion`（装箱/拆箱）与
    `check_generic_invariance` 改用 `zan_type_display`。
  - `src/compiler/irgen_call.c` 的 `Convert.ToString` 诊断是**第三套手写类型名
    渲染器**，一并统一（措辞不变：`'Box'`/`'Pt'`/`'int[]'`/`'D'` 逐字保持）。
  - **刻意不动的几处**（各有各的身份，不能被形状污染）：
    `binder.c` 的 `tuple_sig_type`（tuple 身份）、`irgen_expr_core.c` 的
    `render_type_full`（typeof 运行时反射元数据）、`irgen.c` 的 `di_type_name`
    （DWARF）、`irgen_reflect.c` 的 `refl_field_type_name`（运行时反射字段名）、
    `checker.c` 的 `checker_type_simple_name` / `checker_report_no_member`
    （A476/A457 的**原始名轴**：回答"这是哪个类型"，不是"什么形状"）。
  - 措辞变化（4 条既有 golden 更新 + 3 条新用例）：
    `cannot apply unary '!' to 'bool?'`、
    `cannot convert 'List<Cat>' to 'Cat' in argument 1 of 'Count'`、
    `cannot convert 'Cat' to 'List<Cat>' in argument 1 of 'TakeList'`、
    `cannot convert 'List<string>' to 'string[]' in initializer`；新增
    `cannot convert 'int?' to 'int' in initializer`、
    `cannot convert 'int' to 'int[]' in assignment`、
    `cannot access member 'ToString' directly on nullable type 'bool?'`。
  - **自举侧必须连带改**：宿主渲染 `Task<double>` / `int?` 后，自举若不改，两边
    措辞分叉、`selfhost_diff` 门会红。`src/selfhost/checker.zan` 新增 `TyDisplay()`
    （Task 形状走 `TaskTyName`，`op == 1` 的可空追加 `"?"`），
    `CheckTaskOnlyHandle` / `CheckAssignable` 改用它；`tests/selfhost/reject_cases.txt`
    的 `task_slot_raw_value` / `task_return_raw_value` 措辞由 `'Task'` 改为
    `'Task<double>'` / `'Task<int>'`，并新增 `nullable_to_plain_slot` 三字段用例。
- 验证（2026-10-22，隔离构建 `_scratch/build-a463`，clang+Ninja+Release）：
  - 探针 `_scratch/a463/probe_*.zan` 六个：新措辞全部生效（`bool?` / `int?` /
    `int[]` / `List<string>` / `List<int>`）；
  - `ctest -R '^diag_'`：**161/161 通过**（原 156 + 本条新增 3 + 他人同期补 2）；
  - 宿主侧 reject 措辞全量核对：23 条三字段用例**全部命中**（含他人在途的 4 条
    `convert_to_string_*`），0 mismatch；
  - 自举一致性：用新 zanc 编译 gen1（64 files），`task_slot_raw_value` /
    `task_return_raw_value` / `nullable_to_plain_slot` 三例宿主与自举**逐字相同**；
  - `ctest -R '^selfhost_diff$'`：**通过，0 失败**（689s）——此前看到的 9 个失败
    是 HEAD 既有欠账（golden 还写 `False`、宿主已改成 `false`），与本条无关，
    已由 `89236a32c` / `4fc6b28d1` 修掉。
- 教训：**"类型的名字"和"类型的身份"是两件事**——`zan_type_t.name` 同时承担这两个
  角色，才是 `bool?` 被说成 `bool` 的根因；一旦发现一个字段被两种语义共用，
  就该拆开（渲染函数按形状现推，身份字段保持原样），而不是给身份字段打补丁。
  另外**诊断措辞是公共接口**：45 个调用点共用 `type_name()`，所以改渲染的正确做法
  是**换掉 `type_name()` 的实现**（签名不变、调用点零改动），而不是逐个诊断点手改
  措辞——后者必然漏。同时**自举与宿主必须同提交改**，否则对拍门会把措辞分叉
  变成红灯。

### [ ] A457 类型身份按**名字**比较——限定名退化后的裸名与被 mangle 的真类型对不上

- 现象（2026-10-21 修 A438 后编译 `examples/security_guard` 实测）：
  `card.OnMouseDown(onMouseDownCard)` 报
  `cannot convert 'Action' to 'System_Action' in argument 1 of 'OnMouseDown'`。
  最小复现（两文件，各自都写了 `using System;`，与跨文件导入无关）：
  ```zan
  // p1.zan
  using System;
  public class P1 { public static void Hook(Action a) { } }
  // p2.zan
  using System;
  using Gui;
  public class P2 {
      public static void Main() {
          Gui.Action x = () => { };
          P1.Hook(x);
      }
  }
  ```
  编译 `zanc p1.zan p2.zan --auto-stdlib --package-project .` → 同样报错。
- 根因：`Gui.Action` **并不存在**（Gui 命名空间下没有 `Action` 委托），限定名
  解析失败后退化成"取最后一段猜"（`nsresolve.c` 的 `resolve_ref` dotted 分支，
  见 `zan-compiler-internals` 第 6 条），裸名 `Action` 交给 binder 软匹配到
  `System.Action`；但真类型因为与 `System.Linq.Action` 同名而被 mangle 成
  `System_Action`。于是**同一个类型**在参数位是 `System_Action`、在实参位是
  `Action`，检查器按**名字**比类型（`checker.c` 的 `checker_type_equal` 走名字，
  不比符号）→ 误判成两种类型。
- 与 A438 的关系：**不是** A438 修复引入的——上面这个最小复现里两个文件都显式
  写了 `using System;`，与"跨文件导入隔离"无关；限定名退化与 mangle 后按名字
  比较这两条路径都没被 A438 的修复动过。A438 只让这类问题**更容易暴露**
  （裸名不再被别的文件的导入抢走后，剩下的名字对不上的问题就显形了）。
- 影响：`examples/security_guard` 这类用"不存在的限定名"写回调的地方直接编不过；
  该示例不在任何 ctest 门禁里，所以一直没被发现。
- 方向（择一）：
  1. **类型相等按符号比**：检查器比较两个类型时用符号（`sym`）而不是显示名，
     名字只是诊断用的字符串——这是根上的修法。
  2. **限定名退化时同步改名**：退化到裸名后，把类型引用重写成目标类型的
     mangle 名（`System_Action`），让两处名字一致。
  3. 示例侧改成 `System.Action`（不存在的限定名本来就不该写）——只治这一处，
     编译器仍然会骗人。
- 关联：A438（裸名与同名类族）。

### [ ] A455 自举后端没有命名空间/`using` 解析层——裸名按"第一个同名类型"命中

- 现象（2026-10-21 修 A438 时查实）：C 宿主 `src/compiler/nsresolve.c` 有完整的
  命名空间解析层（`resolve_ref` / `resolve_static_receiver`，按当前命名空间 →
  祖先命名空间 → 本文件 `using` → 歧义诊断的顺序解析裸名）；自举后端
  `src/selfhost/binder.zan:68` 的 `FindType(name)` 只是**按简单名线性扫一遍**
  `types` 列表，命中第一个就返回——既没有命名空间概念，也没有 `using` 概念。
- 影响：自举后端编译多包同名类（`Window`、`Marker` 这类）时，谁赢取决于
  **声明顺序**，不是导入关系；C 宿主修好的"按成员命中消歧 / 报歧义"在自举
  侧没有对应物，两后端对同一份源码可能给出不同绑定。
- 方向：自举侧补最小一层——按"声明所在命名空间 + 本文件 `using`"过滤候选，
  多命中时报与 C 宿主同款 `ambiguous type 'X'; qualify it with its namespace`。
  注意自举侧的文件模型与 C 宿主不同（`src/selfhost/parser.zan:1656` 的
  `ParseUsing` 已经把 using 存进 AST，但 binder 还没消费它）。
- ⚠ `src/selfhost/irgen_expr.zan` 此刻正被另一会话在途修改，本轮不碰；
  A438 的 C 宿主修复已单独落地（见提交正文），两后端行为要等差分门能看见
  这类用例后再对齐。
- 关联：A438（已修，结论在 git 历史）。

### [ ] A451 自举后端没有一元运算符重载派发——同一份源码两个后端语义不同

- 现象（2026-10-21 实测，用例 `tests/conformance/unary_plus_overload.zan`，
  自举 gen1 按差分门同款流程编+链+跑）：
  - C 宿主：输出 `6 / 8 / 10`——一元 `operator +` 被**派发**，重载把值 +1。
  - 自举后端：输出 `5 / 7 / 9`——重载**根本没被调用**，操作数原样返回。
- 根因：`src/selfhost/irgen_expr.zan:156-196` `GenUnary` 只处理 `++/--`、`!`、
  `~`、`-`（内建），`TK.Plus()` 落到末尾 `return v`（恒等）；自举后端**没有**
  一元重载派发这一层（C 宿主是 `src/compiler/irgen_expr.c:4352`
  `emit_unary_op_overload`，按形参个数 1 区分一元）。一元 `-` 打在类/结构体上
  更糟：`sub i64 0, <指针>` → 非法 IR。
- 影响：两后端对同一份源码给出不同结果；`operator_unary_overload`（A438 落的
  用例）与 `unary_plus_overload` 都没进差分门清单，正是因为它们会分叉——
  差分门目前**看不见**这个缺口。
- 方向：自举 `GenUnary` 增派发：操作数是类 / 结构体时查 static `op_add` /
  `op_sub` 且形参个数为 1 的重载，找到就 `call`，找不到就报与宿主同款诊断
  （`type 'X' does not define a unary '+' operator overload`），两者都不许
  静默恒等。
- ⚠ `src/selfhost/irgen_expr.zan` 此刻正被另一会话在途修改，本轮无法同一次
  提交落地，等窗口。

### [ ] A445 自举后端 `Math.*` / `Convert.ToInt32` / `Convert.ToDouble` 与 C 宿主同款缺陷

- 现象（2026-10-21 随 C 宿主的类型守卫一并查实）：
  - `src/selfhost/irgen_expr.zan:3884` `ToDbl`：实参不是 double 就一律
    `sitofp i64 <v> to double`。类实例是**指针** → 造出 `sitofp i64 i8*`，
    LLVM 校验失败（非法 IR）。`GenMathCall`（同文件 `:3897`）的所有分支
    （Sin/Cos/Tan/Log/Exp/Atan/Asin/Acos/Sqrt、Pow/Atan2、Floor/Ceiling、
    Abs、Max/Min）都走 `ToDbl` → `Math.Sqrt(<类实例>)` 在自举后端同样是非法 IR。
  - 同文件 `:4094` `Convert.ToDouble`：实参既不是 string 也不是 double 时，
    同样落到 `sitofp i64 <v> to double` → 同款非法 IR。
  - 同文件 `:4107` `Convert.ToInt32` / `ToInt`：无条件
    `call i64 @atoi(i8* <a.op>)` —— 类实例的**对象指针**被当成 C 字符串交给
    `atoi`，运行期读对象字节当文本，静默得到 0（与 C 宿主修复前
    `strtoll` 读对象首字节同款）。
- 影响：同一份源码，C 宿主现在给人话诊断，自举后端仍是编译期 LLVM 校验失败
  或运行期读对象内存。差分门（`tests/selfhost/diff_cases.txt`）目前是绿的——
  现有用例没有覆盖"给 Math/Convert 喂类实例"。
- 方向：与 C 宿主同款守卫，按**静态类型**分流：类 / 结构体 / 数组 / 委托 →
  报与宿主一致的诊断；字符串 / 数值走原有路径。守卫要落在 `ToDbl` 与
  `Convert.*` 的**分派入口**，不要只补 `GenMathCall` 的某一个分支。
- ⚠ `src/selfhost/irgen_expr.zan` 此刻正被另一个会话在途修改，本轮无法同一次
  提交落地，等窗口。
- 相关：A443（`Math` / `Convert` 公开面缺失）、A444（`Convert.ToString(object)` 残留）。

### [ ] A444 `Convert.ToString(object)` 仍按 C 字符串读——object 里装类实例时依然越界读

- 现象：A4xx 那条修复（`Convert.ToString` 按静态类型分流）把类 / 结构体 /
  数组 / 委托都改成 fail-closed 诊断，但 **`object` 只能放行**：
  - `object` 是带标签的引用，编译期看不出里面装的是字符串还是类实例；
  - `object` 上**连通用 `ToString()` 都调不通**——`obj.ToString()` 报
    "this call could not be resolved to any callable"（builtin 成员表里
    `object` 列了 ToString，但那条调用链没有可发射的实现）。
  - 放行的代价：object 里装字符串时结果正确（老行为），装**类实例**时
    仍然拿 strlen 去量对象指针 → 越界读 + 内存信息泄露（与 A4xx 修掉的那条
    同款）。仓库里 `packages/Zan.Data/src/System/Data/Redis/RedisClient.zan`
    的 `SerializeObject(object obj)` 兜底就是这条路径。
- 影响：越界读可达（Redis 序列化兜底）；另外 object 装类实例时输出无意义。
- 方向（两条，都是大改）：
  1. 给内建 `object` 一个**通用 ToString**（运行期按对象头的类型 id 分派到
     真正的 ToString 方法；字符串负载直接拷贝）——顺带解决 `obj.ToString()`
     现在调不通的问题；
  2. 或者把 `Convert.ToString` 正式声明进 stdlib（A443），按实参类型分派，
     `object` 重载走上面的通用 ToString。
- ⚠ 两条都跨后端（C 宿主 + 自举后端），且 1 还要动运行时（对象头类型 id）。
- 同族残留（2026-10-21 记）：`Convert.ToInt32/ToInt64/ToInt/ToDouble` 与
  `int.Parse` 系列的类型守卫同样**放行 `object`**——`object` 装类实例时，
  这些内建仍会把对象指针当 C 文本去 `strtoll`/`strtod` 解析，静默得到 0。
  与本条同源：object 没有通用 ToString / 值语义就没法正确分流。
### [x] A442 自举后端 `Convert.ToString(<引用类型>)` 输出指针地址——与 C 宿主的诊断不一致

- 现象（2026-10-20 随 A441 一并查实）：C 宿主已改成对引用类型 fail-closed
  （报 "Convert.ToString: cannot convert 'Box' to a string"），自举后端仍走
  `src/selfhost/irgen_expr.zan:4036` 的 `Convert.ToString` 特判 → `ToCStr`
  （同文件 `:933`）→ 非字符串/非数值就落进 `snprintf("%lld", ptr)`，**把对象
  指针当整数打出来**（例如 "140737488xxxx"）。
- 影响：同一个源文件，两个后端一个报错、一个输出地址。地址泄露本身也是
  信息泄露（ASLR 绕过），且用户拿到的是无意义的输出。
- 方向：`Convert.ToString` 特判里按**静态类型**分流——引用类型与结构体报
  与宿主一致的诊断；字符串/数值走原有路径。⚠ 与 A441 的 bool 分支、
  A443 的 stdlib 声明是同一处代码，**建议三件事一起做**（都在
  `src/selfhost/irgen_expr.zan` + `src/compiler/irgen_call.c` 两处）。
- 现状（2026-10-22 已闭账）：自举侧照抄宿主那张**按类型种类**划的白名单——
  `Convert.ToString` 特判里先按**静态类型**分流：类/结构体/委托/数组报与宿主
  **逐字一致**的诊断（`cannot convert 'X' to a string -- call .ToString() on the
  value instead`，位置指向**实参**），文本/数值/枚举/`object`/类型参数走原有
  路径。实测两侧输出逐字相同（类实例 11:44、委托 8:44、结构体 11:44、数组
  12:44 一致）。
- 回归：`tests/selfhost/reject/convert_to_string_{class,struct,array,delegate}.zan`
  进拒绝用例清单（自举与宿主都拒绝、措辞一致）。
- 遗留：A443（`Convert.ToString` 仍未进公开面——宿主靠内建特判兜住，标准库里
  还没有这个声明）、A444（`object` 装类实例时的越界读）仍未闭。


### [ ] A443 `Convert.ToString` 是编译器自造的接口——公开面（stdlib / 内建成员表）里根本没有它

- 现象：
  - `stdlib/System/Convert.zan` 里**没有** `ToString`（只有 Base64 / Hex 那几个）；
  - `src/compiler/builtin_api.c:92-96` 的 `members_convert` 里也只有
    `ToDouble` / `ToInt32` / `ToInt64`，**没有 `ToString`**；
  - 但编译器接受 `Convert.ToString(x)`（`src/compiler/irgen_call.c:1878` 的
    内建特判），而且全仓有 4100 多处调用。
  - 后果：`zanc --emit-symbols` 导出的 IDE 符号索引里，`Convert` 只有三个成员
    （实测符号表原文：`M Convert ToDouble M 1 double ToDouble(string text)` …），
    **IDE 补全补不出 `Convert.ToString`**。
- 根因：检查器对「作为被调者的成员访问」不报「无此成员」
  （`src/compiler/checker.c:4024` 的 `in_call_callee` 分支把诊断吞了），于是
  irgen 的内建特判成了这条 API 的唯一"声明"。
- 方向：把 `Convert.ToString` 真正声明进 `stdlib/System/Convert.zan`
  （按实参类型分派：int / long / double / bool / string / object），
  让公开面住在 stdlib：
  - 检查器会找到真方法 → 类型定型与重载解析交给常规路径；
  - 编译器内建特判的 `!zan_type_defines(g, "Convert", "ToString")` 守卫会自动
    让路（该守卫已经在了）；
  - IDE 补全随之补齐；
  - bool 的渲染口径（A441）也能在 stdlib 里一次定死。
- 同族：**`Math` 也是编译器自造的**。`src/compiler/builtin_api.c:74-82` 的
  `members_math` 声明了 Abs/Max/Min/Pow/Sqrt/Round/Floor/Ceiling 等成员，
  但 `stdlib/` 下**根本没有 `Math.zan`**（`stdlib/System/` 里只有 Convert.zan，
  且它也没有 ToString/ToInt32/ToDouble）。所以 IDE 补全同样补不出 `Math.*`。
  治本时应把 `Math` 与 `Convert` 一起声明进 stdlib，让公开面住在 stdlib、
  类型定型与重载解析走常规路径，编译器内建特判随之退化为兜底。
- ⚠ 跨后端：自举后端 `src/selfhost/irgen_expr.zan:4036` 的 `Convert.ToString`
  特判是**无条件拦截**的（不像 Console 那样有 `LocalIdx("Console") < 0` 守卫），
  必须同时加上守卫，否则自举侧不会调用 stdlib 方法。两端要同一次落地。
### [x] A441 布尔值→字符串的渲染有三条口径：`true` / `1` / `x1`

- 现象（2026-10-20 探针 `_scratch/bool_str.zan`，C 宿主 `build/zanc.exe` 实测）：
  ```zan
  bool b = true;
  Console.WriteLine(b);                 // true
  Console.WriteLine(b.ToString());      // true
  Console.WriteLine(Convert.ToString(b)); // 1    ← 不一致
  Console.WriteLine("x" + b);           // x1    ← 不一致
  Console.WriteLine($"{b}");            // true
  ```
  同一个 `bool`，三条路径给出三种字符串。
- 根因：**没有统一的「bool→字符串」内建**，每条路径各写各的：
  - `src/compiler/irgen_call.c:1878-1924`：`Convert.ToString(x)` 的编译器内建特判
    里**没有 bool 分支**——bool 落到末尾的 `emit_itoa_into`，按整数格式化 → `"1"`。
  - `src/compiler/irgen_generics.c:2439-2491` `emit_to_cstr_of`（字符串拼接用的
    取串路径）：同样没有 bool 分支，落到 `emit_to_cstr_u` → 整数格式化 → `"1"`。
  - `src/compiler/irgen_call.c:1074-1081`（`Console.WriteLine`）与
    `src/compiler/irgen_expr.c:6453-6461`（插值）：**有** bool 分支 →
    `select "true"/"false"`。
  - 自举后端同病：`src/selfhost/irgen_expr.zan:933-956` `ToCStr` 没有 bool 分支
    → `"1"`；但同文件 `4273-4274` 的标量 `ToString` 有 bool 分支 → `"true"`。
    所以**两个后端当前输出一致**（都是 "1"），差分门是绿的——这不是后端分歧，
    是两边各自都有的语义缺口。
- 影响：`Convert.ToString(b)` 与 `"x" + b` 拿到的不是 `"true"`；按字符串比对、
  落库、拼消息的代码会踩坑，而且**同一个值在不同上下文里长得不一样**。
- 修法（2026-10-22 落地）：口径统一到 **`true` / `false`**（与已有的
  `ToString()` / `Console.WriteLine` 一致），并且**改在汇合点上**：
  - 宿主侧：抽出公共助手 `emit_bool_to_cstr`（`select "true"/"false"`），
    `emit_value_as_cstr` 改用它；bool 分支放进**取串的汇合点**
    `emit_to_cstr_u`（`src/compiler/irgen_generics.c`）——字符串拼接、可空值
    取串、`string.Format` 的参数全都经过它，一处分支覆盖多条路径；
  - `Convert.ToString` 的内建特判（`src/compiler/irgen_call.c`）不走
    `emit_to_cstr_u`，单独补一条 bool 分支；
  - 自举后端 `src/selfhost/irgen_expr.zan` 的 `ToCStr` 补同样的 bool 分支
    （照抄同文件标量 `ToString` 的写法：两个托管串字面量 + `select`）。
- 验证：
  - 新增 `tests/conformance/bool_string_rendering.zan`（四条路径 × true/false）
    钉住输出，并进差分门清单；
  - 宿主与自举对同一份源码的输出**逐字一致**（`true / true / true / xtrue /
    true / false / yfalse / false / false`）；
  - 差分门子集 PASS；pkg_sweep 62 包全绿；conformance smoke 全绿——现有
    golden 里没有把 bool 的 `"1"` 固化的用例（`Convert.ToString` 的 230 处
    调用逐个看过，都是 int/long/标识符一类）。
- 教训：**取串路径有"汇合点"**。缺口不在四个调用点，而在它们共同经过的那个
  取串函数——在汇合点加一条分支，比在每个调用点各写一遍可靠，而且以后新增的
  取串路径会自动继承。
- 教训：**两个后端当前一致 ≠ 没问题**。这条里两个后端都输出 `"1"`，差分门是
  绿的，但两边都与 C# 语义不符——差分门只保证"两个后端一致"，语义缺口要另外
  对着标准钉。
- 关联：A442（自举侧 `Convert.ToString(<引用类型>)` 吐指针地址——已随 A442 闭账修好）、
  A443（把 `Convert.ToString` 声明进公开面）。
### [x] A393 自举调用 `async void` 方法不启动协程——方法体根本不执行

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `tests/conformance/async_void_call.zan` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A394 自举在**同步**上下文里遇到 `await` 不 fail-closed——生成读地址 0 的非法 IR

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 拒绝用例 `await_in_lambda` / `await_not_a_coroutine` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [ ] A380 协程帧的释放仍只在类型侧收口——运行时没有幂等保护

- 现状（A379 已闭）：`Task.Run(lambda)` 的句柄被检查器定型成"无结果任务"，
  把 lambda 句柄当 `Task<T>` 用去读 `.Result`/`.Wait()` 会在**编译期**被拒绝
  （`tests/diag/task_run_action_result_type.zan`），那条运行期 double free 路径
  因此不可达。**当前没有可从用户代码复现的触发路径**（5 种探针形状都试过）。
- **2026-10-20 复核更正（原登记的前提只对了一半）**：
  - **默认路径根本没有守卫**。`src/compiler/irgen.c:1128 zan_emit_frame_free`：
    只有 `g->external_async_executor`（即 `--async-workers`）才调运行时的
    `__zan_co_frame_free`（`src/runtime/rt_io.c:4164`，在 `#ifdef ZAN_CO_DRIVER`
    段里，用帧首 8 字节的 `CO_DEAD` 位做幂等守卫）；**默认的进程内协程驱动
    走的是裸 `free(frame)`**（同文件 1134 行），连守卫都没有。
  - 即便有守卫也不可靠：小对象分配器回收时把用户块首 8 字节写成 freelist
    指针（`src/runtime/rt_mem.c:448`），守卫读到的已是垃圾 → 二次释放直接
    abort（`double free of block`）。
  - **原方向 a（活协程表仲裁）与当前协议不符**：活协程表里**摘掉**帧的是协程
    自己的完成路径（`__zan_co_untrack` → `zan_co_live_del`），而**释放**帧的是
    读结果的一方（读完 `.Result` 后 `zan_emit_frame_free`）。"谁摘掉谁负责 free"
    会在读者读结果之前就把帧释放掉 → UAF。仲裁必须是**独立于活协程表的所有权
    凭据**，不能拿活协程表当仲裁器。
- 方向（二选一，都要求 **C 后端与自举后端同一次改动**）：
  - **a. 所有权句柄**：每个帧配一个稳定的句柄单元（帧指针 + released 标志），
    所有释放点走 `handle_release()`，天然 exactly-once。代价：每帧多一次分配，
    句柄本身要么泄漏、要么再加一层引用计数。
  - **b. 隔离的帧分配器**：帧由运行时分配，释放后进隔离区（地址不还给通用分配
    器）→ `CO_DEAD` 位不被覆盖 → 第二次释放读到 DEAD → 直接返回。代价：内存
    延迟回收。
  - **不要用"指针 → 已释放集合"**：地址复用后无法区分陈旧指针与新帧——要么跳过
    一次合法释放（泄漏），要么释放一个活帧（内存损坏）。指针身份本身不够。
- 连带：`__zan_co_isdone` 在帧已被完成路径释放后仍会读它的 DONE 字段（先查活
  协程表、再读帧字段，中间有窗口）——4 字节 UAF 读，通常不炸但语义不严。
- ⚠ 为什么不能只改 C 侧：自举后端发的是同一套协议
  （`src/selfhost/irgen_async.zan:667/673/680/686/1037/1195/1645/2028/2035`
  都是 `call void @__zan_co_frame_free`），帧布局也各写一份；固定点门比的是
  两个后端的产物，单改 C 侧会让 `selfhost_fixed_point` 直接不绿。另外
  `toolchain/*/zanrt_io.o`（各平台预编译运行时）要跟着重出，非本机平台
  重建不了。

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

- **编号规范（2026-10-20 定版，照此实现）**：
  - 形态：`[E123]` / `[W123]`，**编号 + 级别前缀 + 3 位十进制**；`E` = 错误、
    `W` = 警告。编号在同一个前缀内**只增不减、注销后不复用**。
  - 编号粒度：**一条诊断一处**（同一个语义点只编一次号），不按"每个报错函数"
    编号——否则同一条语义换个函数就换号，工具无法按号抑制。
  - 分段：0001–0999 语法/词法，1000–1999 名字/绑定，2000–2999 类型与约束，
    3000–3999 代码生成与后端，4000–4999 互操作与运行时契约；告警沿用同一
    段位（`W` 前缀独立计数）。
  - 文本格式：`[E1234] 源文件(行,列): 原有诊断文本`——**原有文本一字不改**，
    编号只加前缀；这样工具按号匹配、人类读文本都不受影响。
  - 兼容策略：编号**只加不改**：已发布的诊断文本不改写，只在前面加编号；golden
    诊断文件（`tests/diag/*.out`）随编号一次性重生成并入库，之后编号变更
    必须走"新增编号"而不是"复用编号"。
  - 落地次序：① 编号规范 + 全量编号表（`docs/DIAGNOSTIC_CODES.md`）+ 诊断
    文本加前缀 + golden 重生成；② `--warn-level`（按级别/编号前缀过滤）；
    ③ `#pragma warning`（源码内局部调级/禁用）；④ `Marshal`（与编号无关，
    可并行）。
- ⚠ 跨后端约束：自举后端自己发诊断（`src/selfhost/*.zan`），编号表要**两个
  后端同一次落地**，否则固定点门/差分门会不绿；所以这条不能只改 C 侧。

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

- `test_navigation_layout.py`（随 legend 独立项目搬出，现位于
  `D:/project/games/legend/client/tests/test_navigation_layout.py`）是手跑的
  对齐检查器（不参与 pytest 收集，需喂 `dump tree` 的 JSON），它硬编码的是旧模型：
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

### [ ] A439 `--emit-ir` 的 IR 在 generic CPU 下仍无法合法化——Vector256 降级路径要重做

- 这是 A388 的**治本部分**（A388 的"让 .ll 自述目标特性"那半已做，见下）。
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
  - 编译自己不受影响：`src/compiler/irgen_emit.c` 的 `zan_bind_target_layout`
    建 TargetMachine 时 CPU 取 `x86-64`、特性取
    `+sse3,+ssse3,+sse4.1,+sse4.2,+crc32,+aes,+avx,+avx2,+fma,+bmi`，后端拿到
    的是 AVX2 档，所以 zanc 产出的二进制是对的；坏的是**给外部工具消费的 .ll
    文本**。
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
- ✅ 已做（A388 的低成本那半，2026-10-20）：`--emit-ir` 的产物**自述目标特性**
  ——IR 头部注释 `; zanc-target-cpu:` / `; zanc-target-features:`，stderr 同时
  打印对应的 `llc` 命令行。实测：按提示的
  `llc -mcpu=x86-64 -mattr=+sse3,...,+bmi -filetype=obj` **通过**（出 210KB
  目标文件），不带特性的 generic `llc` 仍崩。文档见 `docs/ZANC_CLI.md` §1.5。
  这让产物**可用**，但没让 IR 在 generic CPU 下合法化——治本仍是本条。
- 方向：**重做 Vector256 的降级路径**——不要把 `<32 x i8>` 拆回标量，广播、
  比较、掩码全在 `<4 x i64>` 上做（移位/掩码的 SWAR 逐字节比较，不产生任何
  i1 向量），让 IR 在 generic CPU 下也能合法化；触及
  `src/compiler/irgen_expr.c` 的向量整段 lowering，风险高。
  ⚠ 自举后端也有一份向量 lowering（`src/selfhost/irgen_expr.zan`），两个后端
  要同一次改，否则固定点门不绿。
- ⚠ 不要用"先 `opt -O2` 再 llc"绕过：`opt -O2` 后能过，是因为优化器把死函数与
  冗余结构一起干掉了，编译器自己产出的 IR 仍然过不了自己的后端。

### [x] A390 自举的「数组 → string」是复制打包，不是共享 view

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `tests/conformance/string_nul_search.zan` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A395 闭包不捕获外层方法的接收者 `this`——实例字段在闭包里一律读成 0

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `tests/conformance/closure_captures_this.zan` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A396 委托调用的实参在分支臂里求值——clang 的 X86 指令选择器崩溃

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `tests/conformance/closure_nested_call.zan` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A397 闭包的 fn 槽是 `i8*`：直接当函数指针调是非法 IR

- 现象：闭包记录是 `{i8* fn, i8* env}`，`EmitDelegateCall` 取出的 `fn` 是
  `i8*`，却直接 `call i64 (i8*, i64) %fn(...)`——被调值类型与函数指针类型不符，
  **非法 IR**。clang 对 `.ll` 输入默认 `-disable-llvm-verifier`，非法 IR 被静默
  送进后端。
- 方向：调用前 `bitcast i8* %fn to <函数类型>*`。
- ✅ 已修（2026-10-20）：闭包路径与裸函数指针路径都先 bitcast（虚方法槽那条路径
  本来就是对的，两处要一起看）。
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 闭包族用例（`closure_capture_by_ref` / `closure_capture_async` 等） 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A399 自举：`T[]`（泛型数组）的字段槽两端类型不一致——存端打包成 i64、读端按 i64*

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 泛型族用例（`generic_field_value_type` / `generic_instance_method` 等） 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A400 自举：`foreach` 不支持可枚举协议（GetEnumerator/MoveNext/Current）

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `tests/conformance/enumerable_protocol.zan` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A401 自举：认不出的调用**静默返回 0**——违反 fail-closed

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 拒绝用例 `unresolved_call` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A402 自举：不认识 C 宿主的**编译器内建**——标准库自己就在用，全是错的

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `tests/conformance/bytes_bridge.zan` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A403 自举：调**委托类型的表达式**（`filters[f](item)`）静默返回 0

- 现象：LINQ 包 `packages/Zan.Linq/src/System/Linq/Stream.zan` 里的
  `this.filters[f](item)`（谓词存放在列表里，调用列表元素）自举**编不过**——
  被调用者是**索引表达式**，既不是成员访问也不是名字。改 fail-closed 之前，
  它静默返回 0 → 谓词永远返回"假" → 过滤结果全错。
- 方向：被调用者既不是成员访问也不是名字时，**只有委托类型的表达式**能降级
  （复用委托调用路径 `EmitDelegateCallVal`），其余报诊断。
- ✅ 已修（2026-10-20）：`EmitDelegateCall` 拆成"求值 + 调用"两半
  （`EmitDelegateCallVal`），`GenCall` 的兜底分支先求值再判是不是委托类型。
  差分门里 `value_enumerable` / `linq_query` 两个用例从"编不过"变回通过。
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `value_enumerable` / `linq_query` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A404 自举：认不出的**标识符**与**成员**仍静默返回 0
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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 拒绝用例 `unresolved_call` / `undefined_type` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A405 自举：不支持**静态属性**（`Cls.Prop`）——标准库 `Encoding.UTF8` 就是它

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `tests/conformance/static_property.zan` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A406 自举：内建表的能力没测过——先量出来，再按矩阵补齐

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `tests/conformance/builtin_surface.zan` + `scripts/selfhost_builtin_sweep.ps1` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A407 自举：double 的**文本格式**与宿主不一致（`%g` vs 最短往返）

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `tests/conformance/builtin_surface.zan` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A413 自举：`Console.*` 只有一条 blanket catch——`Clear` 打印 "0"、`ReadLine` 返回垃圾

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `tests/conformance/console_builtin.zan` / `console_ansi.zan` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A415 自举：内建的**属性**（Console 颜色/标题）不降级

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
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `tests/conformance/console_color.zan` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A416 自举的检查器不认识 `Task` / `Task<T>`——任务句柄的定型规则没镜像

- 现象（2026-10-20 对账）：C 宿主 `checker.c` 按**实参形态**给
  `Task.Run` / `Task.Spawn` 定型——lambda/委托/无结果调用 → 无结果 `Task`，
  带类型的调用 → `Task<T>`；把无结果句柄当 `Task<T>` 用在编译期被拒
  （`tests/diag/task_run_action_result_type.zan`，诊断串
  `carries no result; declare 'Task' (not 'Task<T>')`）。自举的
  `src/selfhost/checker.zan` 既不认识 `Task` 这个类型（任何 Task 形状的
  声明都被报成 `unknown type 'Task'`），也没有这条定型规则——宿主与自举
  在「任务句柄定型」上分叉。
- 方向：镜像宿主的三件事——① `Task` / `Task<T>` 是内建类型名（宿主的
  `TYPE_TASK`）；② `Task.Run` / `Task.Spawn` 的结果类型按实参形态定型
  （宿主 `task_run_result_type`）；③ 无结果句柄流进 `Task<T>` 的槽位时报
  编译期错误，措辞与宿主逐字一致。
- ✅ 已修（2026-10-20）：`src/selfhost/checker.zan` 加 `MkTaskType` /
  `IsTaskTy` / `TaskElemTy` / `TaskCallType` / `CheckTaskNoResult`；
  `CheckType` 认 `Task`（至多一个类型实参），`InferType` 按宿主口径给
  `Task.Run`/`Task.Spawn` 定型，`CheckTaskNoResult` 挂在变量初始化、赋值、
  返回值与实参四处。诊断与宿主**逐字一致**（同一份源，两边输出同一条：
  `cannot convert 'Task' to 'Task<int>' in initializer: a task started from a
  lambda/Action carries no result; ...`）。`irgen.zan` 的 `MapType` 显式把
  `Task` 映成 `i8*`（自举的任务句柄就是协程帧指针）。差分门收录
  `task_run_action_handle`（宿主能跑、自举还没有 Task API 的降级），按
  **已知缺口**记账而不是假装通过。
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 差分门用例 `task_run_action_handle`（按已知缺口记账） 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A428 自举的重载分派**按名字取**，不按实参类型——多个同名重载会选错方法

- 现象（2026-10-20 语料扫描实测，`tests/conformance/`）：
  - `operator_overload.zan`：`v %= 3` 没走重载，类指针被喂给 `srem`
    （`%t48 defined with type 'ptr' but expected 'i64'`）；`w + 2.5` 取了
    **int** 重载，把 `2.5` 当 i64 实参传（`floating point constant invalid
    for type`）。**已修**。
  - `overload_same_arity_return.zan`：`await OnMessage(message)` 的 await
    临时量取成了**另一个重载**的返回类型（类指针塞进 `printf` 的 i64 形参
    → 非法 IR）。**已修**。
  - `overload_resolution.zan`：`Derived.Widen(1)` 取了派生类的两参重载，
    报 "missing required argument for parameter 'b' of 'Widen'"（宿主选的是
    基类的一参重载）。**已修**。
  - `overload_inherited_arity.zan`：`Strip.Add(b)`（b 是 Bag）取成了派生类
    的 `Add(string)` → 无限递归 → **栈溢出**（0xC00000FD）。**已修**。
  - `cs_b12_conv_operator.zan`：转换运算符重载落到"按名字取第一个"→ 非法
    IR。**已修**（2026-10-20，见 A432）。索引器本体（单个 `this[...]`）与
    **多重**索引器重载也都已修（见 A431 / A432）。
  - `generic_param_shape_overload.zan`：`Item<T>.val` 读回的是**擦除槽的裸
    指针**（打印出地址而不是 "one"）——这是 A421 的泛型字段布局，不是重载
    分派。**未修**（见 A421）。
- 根因：自举有**三条**"按名字取成员"的路：
  1. 运算符分派 `TryOpOverload`（`binder.FindMethod`）；
  2. 普通调用 `ResolveMethod`（单候选时连**实参个数**都不看，派生类遮蔽
     基类重载时也不往基类找）；
  3. `await` 的被调方法 `AwaitCallTy`（`FindMethodUp`）——await 临时量的
     **类型**取自它，于是类型和真正调用的那个重载对不上。
- ✅ 已修（2026-10-20，提交 `93529a7c` + 本轮）：
  - 运算符路径：先在类链里找**声明类**，再按**实参类型**选重载（精确匹配
    4、int→long 3、int→double 2、已知不匹配 -1），发 `MethodSym(声明类, m)`
    （带 mangling 的符号，原来固定发 `Owner_op_add` → 多重重载会撞名）。
  - `%=` 补进复合赋值的重载分支；int 字面量的**后缀**参与选型（`7L` →
    long，宿主同口径）。
  - 普通调用：候选集改成**整条类链**（派生类的重载只遮蔽**签名相同**的
    基类重载——`Strip.Add(string)` 不遮蔽 `Bag.Add(Bag)`），先按**实参个数**
    过滤，再按实参类型打分；实参个数一个都不合适时**沿基类链**继续找。
  - 调用的符号改用**声明类**（`DeclOwnerOf`）：继承来的重载定义在基类名下，
    调用却指向派生类 → 符号不存在。
  - `await` 的被调方法改成同样的"类链候选 + 个数过滤 + 按实参类型打分"
    （参数类型从**外层方法的形参**取——归一化 await 时方法体还没生成，
    局部变量还不存在）。
  - 顺带修：struct 的 `==`/`!=` 改为**按值**比较（原来是指针同一性）。
  - 用例进差分门：`operator_overload` / `struct_operators` /
    `operator_relational` / `overload_resolution` /
    `overload_inherited_arity` / `overload_same_arity_return`。
- 剩余（未修）：泛型实参形状那一族（属 A421）。
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `operator_overload` / `overload_resolution` 等 6 个用例 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A431 自举的字段初始化器与构造器语义和宿主对不上——顺序错、基类构造器多调一次

- 现象（2026-10-20 实测；探针 `_scratch/sd/p_fldord.zan`、`p_fldord2.zan`、
  `p_basector.zan`）：
  - 宿主 `new Derived()`（Derived 与 Base 都有构造器）打印
    "base field → base ctor → derived field → derived ctor"；自举打印
    "derived field → base field → base ctor → derived ctor"。
  - 宿主 `new D0()`（D0 **没有**构造器，基类 B1 有）只跑基类与派生类的字段
    初始化器，**不调用** B1 的构造器；自举多调了一次 "B1 ctor"。
  - 宿主 `new D3()`（基类只有 `B2(int)`，没有无参构造器）仍然跑基类的字段
    初始化器；自举把基类字段丢成 0。
- 根因：自举把字段初始化器塞进"隐式构造器"里，顺序是**派生类优先**，而且
  派生类没有构造器时**照样调基类构造器**。宿主根本没有隐式构造器这个函数：
  找不到构造器时字段初始化器在**分配点**发（`irgen_expr.c` 的
  `emit_implicit_field_initializers`），基类链的顺序是"基类在前"。
- ✅ 已修（2026-10-20，本轮）：
  - 字段初始化器改成助手函数 `@__zan_finits_<Class>`：发**整条链**（基类在前），
    在"分配点找不到构造器"与"派生构造器没调到基类构造器"两种情形调用；
  - 构造器顺序改成宿主口径：基类构造器 → 基类链字段初始化器（**仅当**基类
    构造器没被调用）→ 本类字段初始化器 → 构造器体；`: this(...)` 的构造器
    不发任何字段初始化器（全交给被链接的那个构造器）；
  - 基类构造器按**参数个数**选（参数个数为 0 的那个），不再"取第一个构造器"；
  - 构造器的实参个数改成**严格**（与宿主后端同口径：省略的尾部参数用默认值
    补齐），并按宿主的两套措辞报错（检查器一套、后端一套）。
  - 用例进差分门：`field_decl_initializers` / `constructor_chaining` /
    `cs_b09_indexer`（索引器本体）。
- 剩余（未修）：
  - `objinit_ctor_field_overwrite.zan`：自举**崩**（0xC0000005）。根因**不是**
    对象初始化器，而是宿主那条 "`Binding<T>` 槽位赋值" 的原语降解（见 A432）——
    把 `title = seed`（string）塞进 `Binding<string>` 字段时，宿主会合成一个
    Binding 对象，自举直接把字符串指针写进去，随后 `title.Get()` 把它当
    Binding 解引用 → 崩。与本轮的改动无关：改动前后该用例生成的 IR 完全
    一致（无 `@__zan_finits_*`）。
  - **多重**索引器重载已修（见 A432）。
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `field_decl_initializers` / `constructor_chaining` / `cs_b09_indexer` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [x] A432 自举缺两样：多重索引器按实参类型选、用户自定义转换运算符

- 现象（2026-10-20 实测）：
  - `indexer_overload.zan`：一个类上两个索引器（`this[int]` 与
    `this[string]`）时，自举按**名字**取第一个 → `b["k"]` 走到了 int 那个
    重载。**已修**。
  - `cs_b12_conv_operator.zan`：`implicit`/`explicit operator T(S)` 自举
    完全没有这个概念——`double d = f;` 把 struct 指针直接塞进 double 槽位。
    **已修**。
- 根因：
  - 索引器：自举用 `FindMethodUp`（按名字取第一个），没有"按实参类型选"的
    路径（运算符重载那条路 §44 修好了，索引器没走那条路）。
  - 转换运算符：自举的转换点（变量初始化 / 赋值 / 实参 / return / 强制转换）
    各自做"内建转换"，没有"用户定义转换"这一层。
- ✅ 已修（2026-10-20，本轮）：
  - 索引器：候选集来自整条类链（派生索引器只遮蔽**签名相同**的基类索引器），
    按下标实参类型打分（写入口按下标与值两个实参打分）。
  - 转换运算符：一次查找（源类型优先、再目标类型；静态方法、恰好一个参数、
    返回目标类型、参数是源类型）+ 五个转换点（变量初始化 / 赋值 / 实参 /
    return / 强制转换；只有强制转换允许 `op_explicit`）。
  - 转换之后的值类型要跟着走：return 的 ARC 记账原来用**源**类型 → 转换后
    值已是目标类型，记账要改成转换后的类型。
  - 用例进差分门：`indexer_overload` / `cs_b12_conv_operator`。
- 关联：`objinit_ctor_field_overwrite.zan` 的崩溃不是对象初始化器的问题，而是
  宿主那条 "`Binding<T>` 槽位赋值" 原语（赋给 `Binding<T>` 槽位时合成一个
  Binding 对象；赋字段左值时合成 live 绑定）自举没有——见 A433。
- ✅ 销账（2026-10-22 复核）：正文「已修」记录属实——回归保护 `indexer_overload` / `cs_b12_conv_operator` 仍在树、仍在差分门清单内，且全量差分门（91 个用例 + 22 条拒绝用例）跑通。

### [ ] A433 宿主有 `Binding<T>` 双向绑定原语降解，自举没有——赋给 `Binding<T>` 槽位的值没被包成 Binding

- 现象（2026-10-20 实测，`objinit_ctor_field_overwrite.zan`）：自举**崩**
  （0xC0000005）。看起来像"对象初始化器覆盖 RC 字段"的锅，其实不是——
  把该用例里的对象初始化器去掉（`new Card()`），自举照样崩。
- 根因：`Binding<T>` 是**泛型类**，字段 `Binding<string> title` 的槽位在自举
  里是"一个指针"。宿主有一条专门的原语降解：往 `Binding<T>` 槽位赋值时，
  - 赋的是**字段左值** → 合成一个 **live 绑定**（getter/setter 委托指向那个
    字段，读走 getter、写回 setter）；
  - 赋的是**其它表达式** → 合成一个 **const 绑定**（`constVal` 存该值）。
  宿主实现见 `irgen_expr.c` 的 `type_is_binding` / `emit_binding_value` /
  `emit_binding_acc_delegate`；检查器侧 `checker.c` 允许把 `T` 直接赋给
  `Binding<T>` 槽位。
  自举没有这一层：`title = seed`（seed 是 string）把**字符串指针**直接写进
  那个槽位，随后 `title.Get()` 把它当 Binding 解引用 → 崩。
- 关联：A421（泛型类字段布局）——live 绑定要合成 `BindGet<T>`/`BindSet<T>`
  委托（泛型委托的签名特化），与泛型实例化模型是同一件事。
- 方向：先做 **const 绑定**（赋值时合成一个 `Binding<T>` 实例，`constVal` 存
  值），再做 live 绑定（合成委托）。
- 现状规避：自举产物里不要用 `Binding<T>` 字段。

### [~] A435 控件的 `Kind()` 与"kind 名单"三处副本要对得上

- 现象（2026-10-20 实测更正）：先前记的"十九个控件没有 `Kind()`"不是
  主要问题——那十九个（`BoxContent` / `ChoiceGroup` / `Column` /
  `ContextMenu` / `Ellipsis` / `FormBuilder` / `Layer` / `Menu` /
  `Message` / `Popover` / `Prompt` / `QrEncoder` / `Ribbon` / `Scrollbar` /
  `ScrollView` / `Split` / `Table` / `Tooltip` / `WidgetDelegates`）是
  设计器与代码内部件，设计稿里的 kind 由设计器模型给
  （`FormField.KindForType`），不依赖 `Control.Kind()`。真正对不上的是
  **声明了 `Kind()` 却在名单外**的控件：`ButtonGroup` / `Center` /
  `Field` / `IconView` / `ListItem` / `RadioButton` / `RichText` /
  `StyledText` / `Typography` / `Wizard` / `GraphView` / `BandGrid` /
  `PropertyGrid`。
- 后果：编译期限定名单（`GenForm`）不认这些 kind → 生成代码发裸名，
  用户项目的同名全局类压过 stdlib（探针实证）；反向也一样——注册表里有
  而编译期不认的 kind（`Row` / `Spacer` / `FormGroup` / `FormItem` /
  `Modal` / `Drawer`）同样发裸名。
- **已修**（2026-10-20）：两处名单补齐——12 个库控件 + 6 个容器件进编译期
  名单；`Field` 进不可实例化名单（它是设计器的字段记录，没有零参构造，
  `new FormField()` 编译不过）。门禁：`scripts/component_audit.py` 的 R1
  规则（注册表 ↔ 编译期名单 ↔ 控件 `Kind()` 三处对齐）。
- 剩余：运行期按 kind 重建仍不支持那 12 个库控件（注册表会把它们拖进
  每个宿主，不注册是刻意的）；代码搭的树导出设计稿后能生成代码，但
  `Serialize` / `Html` 克隆会丢掉它们——真需要时再逐个开注册。

### [~] A436 样式解析的旁路：实例方法已清零，只剩静态 helper

- 现象：控件里手写的 `Style.Of(app, ...)` 已迁到基类入口
  （`Control.Box` / `BoxState` / `ResolveStyleAs`）。
- **已修**（2026-10-20）：剩下的 20 处**实例方法**直调（列表行、树行、
  滚动条轨道、弹层、分割线、停靠面板、虚拟列表根面…）收进新基类入口
  `Control.BareBox(app, type, state)`——部件表面的"裸基线"外观：不带本
  控件的 Class、不带 id、不套行内覆盖（套上会串味：列表写成 `.primary`
  就把每一行都染成主色）。门禁：`component_audit.py` 的 R4 规则
  （实例方法不得直调 `Style.Of` / `Style.Full`）。
- 剩余（不算违规）：**静态 helper**（`Badge.WidthOf/Pill/Dot/CountAt`、
  `Layer.DrawToast/...`、`Tag.*`、`Typography.Heading`、`StyledText.FontPx`）
  ——它们没有 `this`，解析的是显式的（类型, 类）组合（类由调用方传入），
  行内样式与控件缓存本来就与它们无关。
- 关联：见 A429（排版组溢出）——那条是布局放不下，本条是样式解析
  入口不统一。

### [x] A437 尺寸档还有几处不齐：switch 的档位不单调，textarea 只调字号

- 现象：四档统一后仍有例外：
  - `switch` 的 tiny/small/默认都是 24px（`switch.small` 的
    `height: var(--height-tiny)`），只有 large 是 40——"small" 与
    "tiny" 同高，档位不单调；
  - `textarea` 的档位只调字号，高度随内容（多行盒按高度走档不合理，
    但也没说清）；
  - `avatar` 没有 tiny 档（它有显式直径字段，规则对未设置直径的默认
    头像才生效）；
  - `badge` 的默认高度 24（`--height-tiny`），tiny 档 16
    （`--height-micro-tiny`），但代码兜底值过去写的是 18（已随本轮
    改成 24）。
- **已修**（2026-10-20）：`switch.small` 改走 `--height-small`（28），
  阶梯变 24/28/32/40 单调，轨道尺寸不变（tiny 24x14 / small 30x16 /
  默认 40x24 / large 56x32）。`textarea` / `avatar` 两个例外写进
  `docs/GUI_COMPONENT_AUTHORING.md` 的尺寸档一节——是刻意的，不是漏做：
  textarea 档位只调字号、高度随内容（多行盒按固定高度分档会截断输入）；
  avatar 尺寸由显式直径字段决定，档位只作用于没设直径的默认头像，
  且没有比 `--font-size-tiny` 更小的字号令牌可用。

### [ ] A434 泛型类的类型参数字段在**擦除上下文**里读到的是内联值的头 8 字节

- 现象：`class Box<T> { T value; T Get() { return this.value; } }`，
  具体实例 `Box<Pos>`（Pos = 16 字节结构体）**按具体路径**存取已经正确
  （A421 已修，见 `tests/conformance/generic_value_type_field.zan`）；
  但从擦除上下文调用同一个方法（`static T Peek<T>(Box<T> b) { return b.Get(); }`
  → `Peek<Pos>(box)`）时走的是**擦除版** `Box_Get`：它把槽位的前 8 字节当指针
  返回，而槽位里躺着的是内联的结构体 → 调用方按 Pos 解引用 → 野指针。
- 根因：类型参数字段的槽位是**按值内联**（`generic_field_slot` 按已知实例的
  最大 ABI 尺寸加宽成 `[N x i64]`），而擦除形态的 T 只能表示成指针——同一个槽位
  在两条路径上的表示不一致。引用类型实参时两者恰好都是 8 字节，所以一直没炸。
- 边界：这是**擦除表示的固有限制**，不是布局 bug：擦除版方法拿不到类型实参，
  既不知道值类型该占多少字节，也无法接管一份拷贝（值在调用方栈上，存进字段
  会悬垂）。改成"值类型一律装箱"也只是把故障从"野指针"换成"悬垂指针"。
- 方向（择一）：
  1. **按实例动态分派**：对象头里已有类型描述符（`__zan_desc_*`），可按它
     查到具体实例的方法入口，擦除版转调具体版——等于给用户泛型类补一层
     vtable。
  2. **编译期拒绝**：擦除上下文里访问"类型参数字段"时报诊断（要求用户把
     接收者具体化，例如 `Peek<Pos>(Box<Pos> b)`）。判定要能区分"实参是引用
     类型"（现状可用）与"实参可能是值类型"。
  3. **明文化限制**：文档写清"类型参数字段不可在擦除上下文访问"，并给检查器
     一条提示级诊断（当前是静默读错值）。
- 关联：A421（同一族：泛型实例化模型与类型实参的来源）。
### [ ] A450 自举驱动的命令行是 C 宿主的子集——不收目录、也没有 `@响应文件`

- 现象（2026-10-21 给 C 宿主加目录输入时查实）：C 宿主 `zanc` 现在接受
  **目录**（递归展开 `*.zan`）与 `@响应文件`；自举的驱动 `src/selfhost/main.zan`
  只认 `zanc <output.ll> <input.zan> [more.zan ...]`——没有目录输入，没有
  `@file`，也没有 `--auto-stdlib` / `-o` 这些旗标。
- 影响：用自举编译器编大项目时，文件列表同样只能摊在命令行上，同样会撞
  Windows 命令行长度上限。本轮没有一并改自举侧：改 `src/selfhost/main.zan`
  要重编自举编译器并重跑整个差分门（`tests/selfhost/diff_cases.txt`，200+ 用例，
  每个一次 gen1 编译 + 链接 + 两遍运行），代价远超这项便利的价值。
- 方向：在自举驱动里补同样的目录展开（`Directory.GetFiles` /
  `Directory.GetDirectories` / `Directory.ExistsOnDisk` / `Directory.IsSymlink`
  都已在 stdlib 里就位）与 `@file` 展开，与 C 宿主对齐。
- ⚠ 本轮只改 C 宿主 → 两个后端的**命令行面**有差异（**程序输出**无差异，
  差分门仍绿）。自举侧与 C 宿主同改时要注意：目录展开的顺序必须与 C 宿主
  一致（按路径字典序），否则同一批文件的 decl 顺序会分叉。
- 相关：A446（`pkg_sweep` 的假绿与目录输入）。

### [x] A447 泛型类的虚方法体拿不到类型参数——`is T` 经基类虚调用恒为 false

- 现象（2026-10-21 落地 `Zan.Messaging` 时撞到）：`class Ch<T> : Base`，
  `Base.Accepts(object)` 是虚方法，`Ch<T>` 覆写它并写 `return m is T;`：
  - `Ch<Order> ch; ch.Accepts(new Order(1))`（**直接调用**）→ true ✅
  - `Base b = ch; b.Accepts(new Order(1))`（**经基类引用虚调用**）→ **false** ❌
  - 换成 `Ch<T>` 里的**非虚**方法（`Direct(object m) { return m is T; }`）→ 直接调用 true ✅
  - 虚方法体里改成 `return this.AcceptsTyped(m);`（转交非虚方法）→ 仍 false ❌
  - 同一现象在 `build/zanc.exe`（2026-10-21 01:13）与 `build/zanc5.exe`
    （2026-10-20 23:00）上都在，与 HEAD 的「类级单态化」提交无关。
- 根因（2026-10-21 已定位并修复）：**整类只有一张共享 vtable**。IR 实锤——
  `@__zan_vtable_Ch = linkonce_odr constant [1 x ptr] [ptr @Ch_Accepts]`，槽位
  指向**擦除版**方法体；特化版 `@"Ch_Accepts$Order"`（体内 `is T` 判定正确）
  已经发射出来，却没被挂到任何 vtable 上。`new Ch<Order>()` 写进对象头的也是
  这张擦除表 → 经基类引用的虚调用拿到擦除体 → `is T` 被折成常量 false。
- 修法（`src/compiler/irgen_arc.c` + `src/compiler/irgen_expr.c`）：**给泛型类
  的每个实例化单独发一张 vtable**（`__zan_vtable_Ch$<token>`），槽位指向该实例
  化的特化版方法体；`new` 表达式按实例化取表。改动面小、与既有的
  `mangle_inst_suffix` 命名一致、虚调用点一行没动。
  - 保守项：**特化版与擦除版的 LLVM 签名不一致时退回擦除版**（值类型类型实参会
    改签名，按擦除签名发起的虚调用会误读返回值）。
  - vtable 表**创建即填**（`get_vtable_global_inst` 内部填槽）：`emit_vtables`
    在部分路径上早于方法体发射，晚创建会拿到全 null 的表。
- 证据：判定器 `tests/conformance/generic_virtual_dispatch.zan`（4 条：覆写体
  认自己的类型 / 圆认圆 / 方认方 / 两个实例互不干扰）。修复前 4 条全红，修复后
  全绿（`ALL GENERIC VIRTUAL DISPATCH TESTS PASSED`）；arcguard arc-clean、
  `--check-leaks` 无泄漏。
- 影响：**静默错值**——类型判定返回 false 而不是报错。凡是靠运行期类型分发的
  库（消息总线、插件注册表、序列化分派）都会踩，且症状是"消息没人收 /
  永远匹配不上"，极难定位。
- 旁证：**方法组转委托也会抹掉 `is T`**——`FuncT<object,bool> f = chan.Accepts;`
  （`chan` 是 `Chan<T>`）→ 恒 false；同一方法用 lambda 包一层
  （`(object m) => { return chan.Accepts(m); }`）→ true。
- 绕行（已落在 `Zan.Messaging`）：**类型判定在派生类构造器里定型成委托**
  （`this.acceptsField = (object m) => { return m is T; };`），基类只调用
  委托、不碰类型信息；总线只持有基类引用。修复后这个绕行不再是必需的，但保留
  也无害。
- 探针（最小复现，内容直接抄进本条，随时可重建）：
  ```zan
  using System;
  using System.Collections.Generic;
  class Order { public int id; public Order(int id) { this.id = id; } }
  class Base {
      public virtual bool Accepts(object m) { return false; }
  }
  class Ch<T> : Base {
      public override bool Accepts(object m) { return m is T; }   // 虚方法体里用 T
      public bool Direct(object m) { return m is T; }              // 非虚方法
  }
  public class Probe {
      public static void Main() {
          Ch<Order> ch = new Ch<Order>();
          Base b = ch;
          Console.WriteLine("direct: " + ch.Direct(new Order(1)).ToString());  // true
          Console.WriteLine("virtual: " + b.Accepts(new Order(1)).ToString()); // false
      }
  }
  ```
  方法组那一支（同一文件加两行即可复现）：`FuncT<object, bool> f = ch.Accepts;`
  → 调用恒 false；改成 `FuncT<object, bool> f = (object m) => { return ch.Accepts(m); };`
  → 恒 true。
- 关联：A448、A449。

### [x] A448 泛型方法按推断调用时不单态化——方法体里的类型判定恒假

> **2026-10-21 更正**：原标题写的是"在包单元里"，**不对**。真正的触发条件是
> **调用目标是实例方法**（与跨单元无关）：单编译单元里同样复现。跨单元只是
> 最初撞到它的场景。

- 现象：`Zan.Messaging` 第一版把登记写成 `MessageBus.Register<T>(MessageChannel<T>
  channel)`（泛型方法在**包单元**里），调用方在主单元：
  - `bus.Register(orders)`（**推断** T）→ 消息投不出去（`Publish` 返回 0，
    订阅者一次没被叫醒），**不报错**；
  - `bus.Register<Order>(orders)`（**显式** T）→ 正常投递。
- 隔离出的边界（2026-10-21 复测更正）：
  - **实例**泛型方法按推断调用 → 错（同单元、包单元都错）；
  - **静态**泛型方法按推断调用 → 对（同单元、包单元都对）；
  - 显式类型实参 → 永远对；
  - 跨编译单元（stdlib 的 `RandomExt.Pick<T>(List<T>)`）推断调用**结果正确**
    ——但那个方法体里没有类型判定，擦除不影响结果。
- 根因（2026-10-21 已定位并修复）：**形参与实参的映射差一位**。
  `src/compiler/irgen_expr.c` 的 `infer_method_tp_bindings` 写的是
  `int arg_base = recv_expr ? 1 : 0;`——只看"有没有接收者表达式"，于是把
  **接收者当成了第 0 个实参**去和形参类型合一：
  - 形参类型是 `Chan<T>`（类泛型），接收者类型是接收者类（`type_arg_count == 0`）
    → `unify_method_tp` 的逐位合一一个类型实参都绑不上 → `bind[0]` 保持 NULL；
  - `get_or_create_method_spec` 撞上 `if (!bind[i] || !type_is_concrete(bind[i]))
    return -1;` → 返回 -1 → 调用**退化成擦除版方法体** → 体内 `is T` 被折成
    常量 false。
  - 发射端的口径本来是对的：`try_method_spec` 用
    `recv_params = (recv_expr && m_static) ? 1 : 0`（**只有静态方法经实例表达式
    调用时接收者才占第 0 个实参**），`emit_method_spec_call` 同口径。**只有
    推断侧写错了**。
  - 静态方法按推断调用之所以对：`recv_expr` 是 NULL → `arg_base == 0` → 映射正确。
- 修法（`src/compiler/irgen_expr.c` 一行 + 注释）：`arg_base` 改成与发射端
  同口径 `(recv_expr && m_static) ? 1 : 0`。
- 证据：判定器 `tests/conformance/generic_method_inference.zan`（实例推断 /
  实例显式 / 静态推断 / 静态显式 / 两次推断互不干扰）。修复前 2 条红
  （"instance inferred type argument was lost"、"second inference was polluted"），
  修复后全绿（`ALL GENERIC METHOD INFERENCE TESTS PASSED`）；arcguard arc-clean、
  `--check-leaks` 无泄漏。回归：generic*/inherit*/lambda*/closure*/cast*/
  object*/delegate*/func*/messaging* 共 55 条用例 **0 失败**。
- 影响：静默错值（判定恒假 → 消息没人收）。凡是"实例泛型方法 + 按推断调用 +
  方法体里用类型参数"的组合都会踩。
- 遗留（**未修**，另立条目跟踪）：类型参数在 binder 里是**按名字、先到先得**注册
  在**根作用域**的（`binder.c` 的 `register_type_param_list`），类级 `T` 与方法级
  `T` 会互相顶掉；"同名 T 先注册谁"取决于**文件解析顺序**，同一份源码放主文件
  还是放包里，绑定结果可能不同。修它要动绑定期所有类型解析，影响面远大于本条，
  单独一条。
- 探针（最小复现，内容直接抄进本条，随时可重建）：
  ```zan
  using System;
  using System.Collections.Generic;
  class Chan<T> { }
  class Order { public int id; public Order(int id) { this.id = id; } }
  class Api {
      // 实例泛型方法：推断出来的 T 必须真的进到方法体
      public FuncT<object, bool> Make<T>(Chan<T> c) {
          return (object m) => { return m is T; };
      }
      // 静态泛型方法：推断本来就是对的（对照）
      public static FuncT<object, bool> MakeStatic<T>(Chan<T> c) {
          return (object m) => { return m is T; };
      }
  }
  public class Probe {
      public static void Main() {
          Api api = new Api();
          Console.WriteLine("inferred: " + api.Make(new Chan<Order>())(new Order(1)).ToString());        // false（修复前）
          Console.WriteLine("explicit: " + api.Make<Order>(new Chan<Order>())(new Order(1)).ToString());  // true
          Console.WriteLine("static:   " + Api.MakeStatic(new Chan<Order>())(new Order(1)).ToString());  // true
      }
  }
  ```
- 关联：A447、A418、A453（类型参数作用域分层，遗留项）。

### [x] A449 值类型 ↔ `object`：显式装箱崩进程，lambda 返回类型不兼容不报错

- 现象（2026-10-21 落地 `Zan.Messaging` 时撞到，`build/zanc.exe` 01:13 实测）：
  - **显式装箱直接崩进程**：`int v = 42; object boxed = (object)v;` → 程序
    **当场 access violation**（退出码 `0xC0000005` / -1073741819），没有任何
    诊断、没有消息；连"崩之前打印一行"都做不到。
  - **lambda 返回类型与委托返回类型不兼容不报错**：
    `FuncT<Order, object> f = (Order o) => o.id;` → **编译通过**；运行期
    `f(new Order(7))` 返回**非 null 的垃圾指针**（int 值被当成对象指针，
    `r == null` 为 false）；进程退出时 ARC 去回收这个假对象 → 再次
    access violation。块形式 `(Order o) => { return o.id; }` 同样编译通过、
    同样给垃圾（两种写法结果一致）。
- 根因（2026-10-21 已定位并修复）：两处，都是"没有装箱设施"却放行了。
  1. **显式装箱**：checker 的 `checker_cast_is_valid` 对 `dst->kind == TYPE_OBJECT`
     **无条件放行**；irgen 侧 `emit_expr_cast_expr` 遇到"整数 → 指针"就走
     `LLVMBuildIntToPtr`——`42` 变成指针 `0x2A` 存进 `object`，之后任何一次
     对象操作（ARC release 读头 / `is` 读 site 字）都在 `0x2A-16` 处读 →
     **access violation**，全程无诊断。
  2. **lambda 返回类型**：checker 的 `AST_LAMBDA` 分支**直接 `return type_error`**，
     而 `type_error` 在 `checker_type_assignable` 里被当作"什么都行"
     （`if (target->kind == TYPE_ERROR || value->kind == TYPE_ERROR) return true;`）
     → 赋值检查静默通过；irgen 侧 lambda 的 LLVM 返回类型直接取委托的返回类型
     （`object` → `i8*`），函数体里的 `o.id`（i32）在 return 处被 IntToPtr 转成
     指针 → 运行期返回**非 null 的垃圾指针**，进程退出时 ARC 回收这个假对象
     再崩一次。
     - 附带发现：checker **根本没给 lambda 的形参建类型**（`AST_LAMBDA` 分支里
       那个空的 `for` 循环是残留），也**不检查块形 lambda 的语句**
       （`zan_checker_check_expr` 没有 `AST_BLOCK` 分支）——所以"在 checker 层
       比对 lambda 体类型"这条路走不通，必须在 irgen 层做（那里委托返回类型
       与体类型都精确已知）。
- 修法（走方向 2：**编译期诊断**，不做真装箱）：
  - `src/compiler/checker.c`：`checker_cast_is_valid` 里把
    `dst->kind == TYPE_OBJECT` 收窄为"**来源必须是引用类型**"（可空类型按基类型
    判）；值类型 → `object` 报 `cannot convert 'int' to 'object': value types
    cannot be boxed (Zan has no boxing facility)`。
  - `src/compiler/irgen_expr.c` / `irgen_expr_core.c`：新增
    `check_no_boxing_conversion()`——目标类型是引用类型、来源是值类型（或反过来）
    就报错；类型参数（泛型未实例化）放过，用户自定义 `op_implicit` 放过。
  - `src/compiler/irgen_expr.c` 的 `emit_lambda_typed`：表达式体按
    "委托返回类型 vs 体类型"比对。
  - `src/compiler/irgen_stmt.c` 的 `AST_RETURN_STMT`：块形 lambda 的 `return`
    走这条路（只在 `g->lambda_depth > 0` 时查——普通方法由 checker 负责，不重复报）。
  - `src/compiler/irgen_expr.c` 的 `emit_expr_cast_expr`：**兜住泛型特化**——
    `object Box<T>(T x) { return (object)x; }` 的来源是类型参数，checker 阶段
    看不出来，放行；特化成 `Box<int>` 之后 `int` → `object` 由 irgen 拦下。
  - 保留的口子：`object → 具体类` 的下转照旧放行（运行期检查）；`(T)(object)null`
    这类"先转 object 再转 T"的写法不受影响（`null` 的类型就是 object）。
- 证据（三个诊断用例，全部"修复前编译通过、修复后报错"）：
  - `tests/diag/box_value_to_object.zan` → 显式装箱；
  - `tests/diag/lambda_return_object.zan` → 表达式形 lambda（块形同样报错）；
  - `tests/diag/box_generic_value_to_object.zan` → 泛型特化里的装箱。
  - 诊断文案：`cannot convert 'int' to 'object': value types cannot be boxed
    (Zan has no boxing facility)` / `cannot convert 'int' to 'object' in lambda
    return: value types cannot be boxed into a reference type`。
- 影响：任何"值类型进 `object`"的写法要么崩进程，要么静默拿到野指针。这是
  `Zan.Messaging` 不能做"统一请求-响应 + 擦除结果"的直接原因——结果类型
  必须由泛型端点一路保留（`QueryEndpoint<T, TResult>`）。
- 未做（**真装箱**，另立条目跟踪）：值类型 → `object` 时分配一个带类型描述符的
  堆对象。可行路径已探明（`rt_alloc(16 + payload, site_id, name)` + 为该值类型
  注册 site + 载荷写在头部之后；`is T` 的运行期检查读 site 字 → 查
  `__zan_site_names_tbl`，天然可用；ARC 回收走 `__zan_site_dtor_tbl`），但要动
  运行期内存布局、ARC 析构表、反射/`ToString`/序列化对装箱值的处理，是独立战线。
- 探针（最小复现，内容直接抄进本条，随时可重建）：
  ```zan
  using System;
  using System.Collections.Generic;
  class Order { public int id; public Order(int id) { this.id = id; } }
  public class Probe {
      public static void Main() {
          FuncT<Order, object> f = (Order o) => o.id;     // 现在编译期报错
          object r = f(new Order(7));
          Console.WriteLine("r is null: " + (r == null).ToString());
          int v = 42;
          object boxed = (object)v;                      // 现在编译期报错
          Console.WriteLine("after box");
      }
  }
  ```
- 关联：A434、A441~A446（同一族：`object` 没有值语义与类型描述符）、A454。

### [ ] A453 类型参数按名字全局注册——同名 `T` 先到先得，跨文件行为可能不同

- 现象（2026-10-21 修 A448 时顺带查出，未修）：类型参数在 binder 里是**按名字、
  先到先得**注册进**根作用域**的（`binder.c` 的 `register_type_param_list`：
  `if (scope_find(b->current_scope, tp->ident.name)) continue;`）。于是：
  - 根作用域里 `"T"` 只有**一个**，是第一个声明的类级/方法级类型参数；后续所有
    `"T"` 都解析到它；
  - `bind_members` 虽然把方法级类型参数注册进**类的作用域**，但 `scope_find` 沿
    parent 链会先命中根作用域的同名 `"T"` → 方法级类型参数常常根本没被注册；
  - 所有替换都是**按名字**匹配（`subst_method_tp` / `zan_binder_subst_named`），
    名字撞了替换仍然生效，但**类型实参个数**一旦对不上，逐位合一就错位 →
    绑不上 → 退化成擦除版。
- 结构性后果：**文件解析顺序会改变结果**。所有源文件（主文件 + stdlib + 包）被
  合并进同一个 AST、同一次编译（`main.c` 把各文件的 decl 依次 push 进同一个
  `ast`），`register_type_params` 按合并后的 decl 顺序遍历 → "同名 T 先注册谁"
  取决于**哪个文件先被解析** → 同一份源码放主文件里 vs 放进包里，绑定结果可以
  不同。
- 影响：目前没有独立的复现用例（A448 的真因是 `arg_base` 口径，已修），但这是
  一颗定时炸弹：任何"两个类型参数同名"的代码（类级 `T` + 方法级 `T`，或两个类
  各有 `T`）都可能踩到跨文件行为差异。
- 2026-10-21 排查记录（**仍未复现**）：两个最直白的形态都实测**正确**，说明
  "按名字替换"这条兜底路径在常见形态下够用：
  1. 类级 `T` + 方法级同名 `T`（`Box<T>` 里的 `T Same<T>(T x)`）→ 实测方法级
     形参按方法自己的实参定型（`Same<string>("s")` 得到 `s`），没被类级实参
     顶掉；
  2. 方法级 `T` 出现在**泛型实参位置**（`public List<T> Same<T>(T x)`）→ 实测
     返回 `List<string>`，类级替换没有先把 `"T"` 改写掉。
  剩下没验的是"**类型实参个数对不上**"那条（逐位合一错位 → 退化成擦除版），
  以及**跨文件顺序**（同名 `T` 先注册谁取决于文件解析顺序）——要复现得把两个
  同名类型参数分到不同文件里对照。
- 方向：把**方法级类型参数注册进方法自己的作用域**，并让"同名但不同层"的类型
  参数**按层隔离**（类级 `T` 与方法级 `T` 不再互相顶掉）。要动绑定期所有类型
  解析（`binder_type_params_equiv`、`nsresolve.c`、`checker.c` 里按名字比较类型
  参数的几处都要同步核对），影响面大，单独一条做。
- 关联：A448（真因不在这里，但同族）、A418。

### [ ] A454 真装箱：值类型 → `object` 的堆对象（带类型描述符）

- 背景（2026-10-21 A449 的遗留）：A449 走的是**编译期诊断**路线——值类型装不进
  `object`，编译期就报错。要让"值类型进 object"成为**可用**能力，需要补装箱
  设施。
- 可行路径（已探明，未实施）：
  - `rt_alloc(16 + payload_size, site_id, site_name)` 分配带头部的堆对象，载荷写在
    头部之后；
  - 为该**值类型**注册一个 site（`reserve_arc_site` + 名字写进
    `__zan_site_names_tbl`）——`is T` 的运行期检查读对象头 `-8` 处的 site 字 →
    查名字表，**天然可用**；
  - ARC 回收走 `__zan_site_dtor_tbl`（标量无托管字段 → 空析构即可）；
  - 需要新增的只有"为值类型注册 site"+"写载荷"+"取回时按类型读载荷"三件事。
- 要动的面：运行期内存布局、ARC 析构表、反射 / `ToString` / 序列化对装箱值的
  处理。属于独立战线，不与编译器缺陷修复混提。
- 影响：没有它，`object` 只能装引用——`Zan.Messaging` 的"统一请求-响应 + 擦除
  结果"做不了，结果类型必须由泛型端点一路保留（`QueryEndpoint<T, TResult>`）。
- 关联：A449、A434。

### [ ] A472 设计稿根（body）不认 `data-dock`——body 直接子级的停靠全部失效

- 背景（2026-10-21 coreforge 底部建造栏悬在屏幕中间时查实）：CSS
  `display:block` 在引擎里映射成**块流排版**（`StyleDisplay()==3`，
  `packages/Zan.Gui/src/Gui/Core/Control.zan` 的 `ArrangeFlow`），而
  `dock` 是**停靠排版**（`display==0`）的机制，两者是互斥的两条排版路径。
  设计稿的根是 `Gui.Element`（tag `body`，UA 样式表给了 `body { display:
  block }`），于是：
  - body 直接子级的 `data-dock` **全部被忽略**：`dock=5` 的填充层只拿到
    内容自然高，`dock=2` 的底栏贴在那个小盒子的底——表现就是"工具条跑到
    屏幕中间"；
  - 只有 body 这一层是块流：设计稿里的 `<div>`（`Gui.Widget.Panel`，
    style=3 → CSS 类型选择器 `stack`）走停靠排版，它们的子级 dock 生效。
- 旁证：`Form.StyleDisplay()` 已被强制成 0，注释写明"确保内部 Dock.Fill
  占满客户区、Dock.Bottom 稳固贴底，**严禁退化为块流垂直塌陷**"——同一个
  陷阱在窗口层已修，设计稿根层没修。
- 波及面（未逐个核实）：`templates/gui/*` 的设计稿都在 body 直接子级上写
  `data-dock`（ToolStrip=1 / StatusBar=2 / Panel=5），按当前语义它们的
  StatusBar 同样贴不住窗口底；IDE 的 `src/ide_zan/src/IdeForm.html` 亦然。
- 不能一刀切改成停靠排版：`tests/gui/genform_contract.html` 这类设计稿的
  body 里有裸文本与行内元素，需要块流排版（`FlowEntries`）。
- 可行方向（待定，需评估影响面）：设计稿根按内容自动选排版（body 只有带
  dock 的元素子项、且无流内文本时走停靠排版）；或给设计稿根一个显式开关
  （如 `data-root-layout="dock"`）；或让块流排版容器也尊重显式声明了 dock
  的子项。
- 已落地的项目侧绕行（不算修复，只是把 coreforge 摆正）：`client/src/
  main.zan` 把 HUD 根改成 flex 列 + Overlay `flex-grow:1`。
- 关联：coreforge 外部项目 `D:/project/games/coreforge`（仓库外）。
### [x] A473 gui-webapp 的 66KB webview 运行时——评估后暂不抽包

- 评估对象：`templates/gui/gui-webapp/src/` 下的客户端 webview 宿主机制
  （`WebAssets.zan` 42KB + `WebBridge`/`WebServer`/`WebUpdater`/`WebSecurity`/
  `WebDevWatch` 约 25KB，合计约 66KB）。
- 判据复核（规则 14"换个游戏还成立吗"）：**成立**——web 前端做 UI 的桌面
  应用都需要"本地静态服务 + 前端打包 + 自动更新 + CSP/origin 安全策略 +
  双向桥"这一整套。它与 `Zan.Web`（服务端路由/视图，不是一层）和
  `Zan.Gui.Browser`（CEF 控件驱动，opt-in 原生依赖）都不重叠。
- **结论：暂不抽包**。理由：目前只有 gui-webapp 一个使用者，抽出去会让
  模板多一层包依赖（还得改编译口径），而"两处重复"这个真正的收益前提
  还没出现——现在抽属于提前抽象。触发条件：出现第二个 webview 宿主
  项目（另一个模板或仓库外项目用同一套桥/打包/更新机制）时再抽，那时
  直接按包拆（建议落 `packages/Zan.Web.Client` 或 `Zan.Desktop.WebHost`，
  模板只留 `web/` 前端源码 + 一份薄配置）。
- 关联：A472（GUI 排版层的同类结构性遗留）。
### [x] A474 wuwei 的养成骨架——评估后不按"抽包"处理

- 评估对象：`D:/project/games/wuwei/client/src/Game.zan`（72KB）里的养成
  核心——境界/层数（`Realm()`/`Layer()`/`RealmName()`）、功法与道艺等级、
  心魔突破（`HasEpiphany`/`TriggerEpiphany`）、天劫（`InTribulation`/
  `StartTribulation`/`EndureTribulation`）、奇遇事件（`HasAdventure`/
  `ResolveAdventure`）、寿元与年龄（`Age()`/`Lifespan()`）。
- 判据复核（规则 14"换个游戏还成立吗"）：**不成立**。这套机制是修仙/
  仙侠题材专属（境界突破、天劫心魔、奇遇抉择、道法等级）；抽象成"成长
  曲线 + 事件"的泛形反而丢掉题材味道，且与包里的通用件重复。
- 而包里的 `Game.Foundation` 通用件在 wuwei 侧的对应物是**另一层**：
  `Game.Idle`（BigNum 千进制大数 / Wallet 多资源账本 / Offline 离线收益 /
  Curves 指数成本）是**放置类增量**数值——实测 wuwei 源码里 `BigNum`/
  `Wallet` 零命中、数值用 `double` 与字典计数（修仙数值到不了千进制大数
  档），wuwei 的养成是**回合/事件驱动**而非 idle 增量，两者不重合。
- **结论：不做抽包**。wuwei 真正该共用的那层（存档基座、设置声明与逻辑
  层）已经通过 `GameSaveStore` / `GameSettings` / `SettingsModel` 覆盖
  （wuwei 已在用 `Game.Data`）。"看着像重复"不等于"该抽"——判据是换题材
  还成不成立。
- 关联：A473（同类评估：机制成立但只有一个使用者，暂不抽）。

### [x] A475 抽包时 `Gui.Widget.QrEncoder` 的公开面缩水——`SeqByteLen` 等无修饰符成员消失

- 背景：编码器实现在 `32a91d0c7` 抽到 `Zan.Barcode` 包（`System.Barcode`），
  `packages/Zan.Gui/src/Gui/Widget/QrEncoder.zan` 退化成薄适配（只留 `Encode` /
  `ParseEcl`）。那次 commit message 写的是"公开签名与行为不变"——**不成立**：
  旧类上的 `static int SeqByteLen(int lead)` 等成员**没有修饰符**，而 Zan 里
  "无修饰符"= 默认跨包可访问（成员可见性判断只拦 `private/protected/internal`），
  所以它们一直是外部项目的既成公开面，不是"内部实现"。删掉就是公开面断裂。
- 波及面（实测）：外部项目 `legend` 的名牌宽度计算直接调
  `QrEncoder.SeqByteLen(s[i] & 255)`，抽包后编译报
  `QrEncoder.SeqByteLen has no member` + `unresolved call` +
  `code generation failed`；仓库自己的 `gui-design` skill 文档也还在教用户
  这么调。绕行（改 `using System.Barcode`）不可用：同时 `using Gui.Widget`
  时裸 `QrEncoder` 是歧义名。
- 已落地的修法：兼容面**只转发纯函数**（`SeqByteLen` / `IsNumeric` /
  `AlnumValueByte` / `ModeOfByte`），实现仍然只有 System.Barcode 一份；规格表
  （EC 分块表、GF 表）在 `System.Barcode.QrSpec`；其余成员用**全限定名**可达
  （全限定名不撞名）。`System.Barcode.QrEncoder` 侧把这四个函数显式标成
  `public`，不再靠"无修饰符=可访问"的隐式规则。回归用例
  `tests/conformance/qr_encoder_compat_surface.zan` 锁住"只用 `using Gui.Widget`
  时兼容面仍在、行为不变"。
- 教训：抽包/搬实现时**公开面要逐项核对**——"无修饰符"不是私有而是默认公开；
  commit message 写"公开面不变"之前，按名字把旧类成员对一遍。
- 关联：A476（同类"成员存在性"漏网）、A472。

### [x] A476 未知方法调用目标不在类型检查阶段拦下，且诊断泄漏内部符号名

- 现象：调用**不存在的成员方法**时类型检查阶段不报，拖到发码阶段才炸：
  - 实例方法调用 `s.NoSuchInstanceMethod()` → `'string' has no member
    'NoSuchInstanceMethod'` + `error: code generation failed`；
  - 静态方法调用 `Encoding.NoSuchStaticMember(1)` → `type 'System.Text.Encoding'
    has no member 'NoSuchStaticMember'` + `unresolved call
    'Encoding.NoSuchStaticMember'` + `code generation failed`。
  而**成员访问**（不调用）是拦得住的：`s.NoSuchInstanceMember` /
  `Encoding.NoSuchStaticField` 都在类型检查阶段报
  `1 error(s) after type checking`。
- 根因：`src/compiler/checker.c` 的成员解析里 `if (!in_call_callee && ...)`
  （现 4331 行）让"调用目标"整段跳过成员存在性检查——`in_call_callee` 为真时
  直接落到函数末尾返回 `type_error` 而**不发任何诊断**，错误被推迟到
  `src/compiler/irgen_call.c`：`diagnose_unresolved_static_chain`（304 行起）
  与 5389 / 5425 / 5431 行的补诊断。
- 连带影响（诊断质量）：
  - 用户看到的是 `code generation failed` 这种**发码阶段兜底文案**，而不是类型
    错误，定位成本高；
  - 诊断里泄漏编译器内部符号名：`checker.c` 4335-4341 行把命名空间 `ns` 与
    `obj_type->sym->name` 拼起来打印，而包内类的 `sym->name` 是路径式内部名
    （如 `Gui.Widget.Gui_Widget_QrEncoder`）。
- 可行方向：让"调用目标成员不存在"也在 checker 阶段报（`in_call_callee` 时照样
  走成员存在性检查，只是措辞区分成"没有这个方法"）；诊断里的类名改用面向用户的
  名字（binder 的类型名或源文件名），不要用内部符号名。
- 关联：A475（同一类"公开面/成员存在性"问题）、A471（checker 漏检）。
- 修法（2026-10-22 落地，提交 `fc962a7b4`）：
  - checker 在**调用上下文**也判定"接收者上有没有这个方法"，没有就当场报
    `type 'Ns.Name' has no method 'X'`；取值位置仍报 `has no member`——**"方法名写
    错了"和"字段名写错了"必须分得开**。
  - 成员集判定与发码口径对齐：类链成员 + 实现的接口（含接口继承链）+ 扩展方法
    + 编译器原生成员（`int.Parse` / `string.AsSpan` / `Gate.Park` / `Socket.RecvOv`
    这类实现完全在编译器里、不在任何成员表里的成员）。
  - 只对**成员集完全已知**的接收者报错；编译期内建类、object、类型参数、可空、
    枚举、数组、无符号的内建泛型容器一律判不了就放行到发码阶段，**绝不猜**。
  - "某类型是否实现了某接口"下沉到 binder 单一事实源，checker 与 irgen 共用，
    不再各写一份遍历。
  - 诊断类型名改面向用户：命名空间 + **原始简单名**（`checker_type_simple_name`
    这条"原始名轴"保留至今——A463 的形状渲染刻意不碰它：它回答"这是哪个类型"，
    不是"什么形状"）。
  - 原生成员表（`builtin_api`）必须与发码侧分派点**逐条对齐**——漏一条不是"少报
    一个错"，而是整个标准库都编不过。
- 验证（2026-10-22）：`ctest -R '^diag_'` 156/156 通过；
  `ctest -R case_native_member_call` 通过（原生成员 `int/long/double.Parse`、
  `string.ToBytes`、`string.AsSpan` 可正常调用）；19 个外部 GUI 项目整包编译
  OK=19 FAIL=0（6189 个文件零错误）。新增用例：`tests/diag/call_unknown_method.zan`、
  `call_unknown_method_name.zan`、`call_unknown_method_name_lib_{a,b}.zan`、
  `tests/conformance/native_member_call.{zan,out}`。
- 教训：**"成员存在性"要在类型检查阶段判，不能拖到发码阶段**——发码阶段只有
  `code generation failed` 这种兜底文案，用户既看不到方法名，也看不出是哪个类型。
  另一个教训：**诊断里的类型名不能取符号名**——符号名在命名空间冲突时会被改写成
  mangle 名（`Gui.Widget.QrEncoder` → `Gui_Widget_QrEncoder`），用户会以为自己的
  类被改名了；但**显示名与身份名必须分开**：显示名走"命名空间 + 原始简单名"，
  身份名继续用 mangle 名做散列与链接。

### [x] A477 自举检查器没有通用隐式转换检查——`int y = "hello";` 在自举里编译通过

- 现象（2026-10-21 修 A471 时实测，探针 `_scratch/sh_method_conv_probe.zan`，
  gen1 直接跑 `build\selfhost_gen1.exe out.ll probe.zan`）：自举的检查器**不实现
  宿主那种通用的隐式转换检查**——`int y = "hello";` 放在**方法体**里，宿主报
  `cannot convert 'string' to 'int' in initializer: no implicit conversion`，
  **自举一个字都不说**（gen1 退出码 0，IR 照写）。
- 机制：自举 `src/selfhost/checker.zan` 的 `CheckStmt(AK.VarDecl)` 只做两条
  **Task 专属**的检查（`CheckTaskNoResult` / `CheckTaskOnlyHandle`），没有通用的
  "目标类型 vs 值类型"比较；整个文件里 "cannot convert" 的措辞只出现在那两条
  Task 规则里。
- 影响：自举编译出来的编译器**放得过宽**——把字符串指针当 int 用这类问题要到运行期
  才炸（宿主侧同类程序实测 `f()` 返回 `-1065074624`）。A471 修完后宿主在 **lambda
  体里**也查了，自举仍然放过 → 同一段代码两边判定开始分叉（宿主拒、自举收）。
- ⚠ 顺带纠正一个**误传**：A471 的旧描述说"自举那边同样不下钻 lambda 体"——**不对**。
  自举的 `AK.Lambda` 分支在 `CheckExpr` 里（不是 `CheckStmt`）：`ScopeMark` →
  形参入作用域 → `curRet = null` → `CheckExpr(n.a)`（表达式体）+ `CheckStmt(n.d)`
  （块体）→ 还原。实测自举会报 lambda 体里的 `undefined identifier`（探针
  `_scratch/sh_lambda_undef_probe.zan`）。**A471 的洞只在宿主这边。**
- 修法（2026-10-21 落地）：
  - `src/selfhost/checker.zan` 新增 `PrimCat()`（把类型名分成 0 其他 / 1 整型 /
    2 浮点 / 3 bool / 4 string / 5 char / 6 枚举 / 7 类实例）与
    `CheckAssignable()`；在 `CheckStmt(AK.VarDecl)`（含逗号续列）与
    `CheckStmt(AK.ReturnStmt)` 上调用；
  - 措辞与宿主**逐字一致**：`cannot convert 'X' to 'Y' in initializer: no implicit
    conversion` / `... in return statement: ...`；
  - **判据是先量后写**：拿 12 个目标类型 × 17 个值 = 204 组矩阵探针（分块编译，
    绕开宿主 100 条诊断的上限），把宿主的判定表**量出来**再照着实现；
  - 顺带修了字面量的类型：`5L` 是 long、`5u` 是 uint、`5UL` 是 ulong、`5.0f` 是
    float、`5.0m` 是 decimal、`0x1FL` 是 long（解析器把进制折进了同一个字段，
    类型后缀取 `suffix % 8`）——宿主这么分，诊断里点的类型名也就一致了。
- **刻意不判的三类**（宁可漏报，不可误报）：
  - `double`/`float` 进整型槽：那是宿主的**窄化**规则（另一条诊断，见 A485）；
    宿主报 "narrowing conversion from 'double' to 'int' ... (C# rules)"；
  - `object` 目标：Zan 的 `object` 不是万物的基类（宿主只收 string 与类实例），
    它的判定规则与整型族不同；
  - 用户类目标：宿主靠类层次判定（子类可赋给基类），自举的类型模型没有层次。
- 验证：
  - 204 组矩阵对照：自举对宿主 **误报 0、措辞差 0**；漏报 43 组，全部落在上面
    刻意不判的三类里（窄化 15 / `object` 目标 13 / 用户类目标 15）；
  - 自举 64 个源用宿主 zanc 重建 gen1 通过（自举自己的源码没有被误伤）；
  - 新增三条拒绝用例 `tests/selfhost/reject/implicit_conversion_*.zan` 进
    `tests/selfhost/reject_cases.txt`（三段式：自举措辞 + 宿主措辞），宿主与自举
    逐字相同；
  - return 路径也逐字相同（含列号）：`cannot convert 'string' to 'int' in return
    statement: no implicit conversion`。
- 教训：**规则要照着"量出来的表"写，不是照着想象写**。204 组矩阵把"简单名 vs
  限定名"这类直觉区分全部推翻；真正决定判不判的是「目标是不是原始类型」+「值
  是不是已知类型」两件事，而"可空值只能进同种可空槽"这条规则是从矩阵里长出来的，
  凭直觉写不出来。
- 关联：A471（宿主侧同一条战线）、A485（自举缺的窄化规则）、A463（已闭账：宿主诊断
  现在按形状渲染类型名——可空是 `T?`、数组是 `T[]`、泛型是 `List<T>`；自举侧同步
  跟上，两边不再"一致地不渲染"）。
### [x] A485 自举没有宿主的「窄化转换」规则——`int y = 5.0;` 在自举里编译通过

- 现象：宿主报
  `narrowing conversion from 'double' to 'int' in initializer needs an explicit cast (C# rules)`；
  自举一个字都不说（浮点值直接进整型槽）。
- 机制：宿主在 `checker_check_assignable` 里**先**判窄化（常量还要看是否越界：
  `byte b = 200;` 合法、`byte b = 300;` 非法），**再**判通用可赋值性；自举现在
  只有后者（A477），且**刻意跳过**了"浮点进整型槽"这一格——怕措辞对不上。
- 影响：自举仍放得过宽；两边对同一个程序的分歧还在（一条报窄化、一条不报）。
- 修法（2026-10-21 落地）：`src/selfhost/checker.zan`
  - 新增 `CheckNarrowing(target, valueExpr, valueTy, what, line, col)`，在
    `CheckStmt(AK.VarDecl)`（含逗号续列）与 `CheckStmt(AK.ReturnStmt)` 上调用；
  - 两个触发形状：**浮点值进整型槽**（char 槽、枚举槽也算整型槽，`Color c = 5.0`
    报窄化）与**整型常量越界**（`byte b = 300` 报，`byte b = 200` 不报）；
  - 常量取值靠字面量节点的 `ival`（`IsConstIntExpr` / `ConstIntExpr`，一元负号
    也算），目标范围表 `FitsTarget` 对齐 byte/sbyte/short/ushort/char/int/uint；
  - 措辞与宿主**逐字一致**：`narrowing conversion from 'X' to 'Y' in <what>
    needs an explicit cast (C# rules)`（`what` 取 `initializer` /
    `return statement`）；
  - 顺带把整型字面量的类型名补准：超 int 范围的字面量在宿主是 `long`
    （`int v = 3000000000` 报 `narrowing conversion from 'long' to 'int'`），
    自举原来一律当 `int`。
- **判据是先量后写**（同 A477）：先用 98 组矩阵 + 4 组边界探针把宿主的判定表
  量出来，再照着实现。量出来的结论有三条是凭直觉写不出来的：
  1. `decimal` **不是**浮点：`int v = 5.0m` 宿主报的是 *cannot convert* 而不是
     *narrowing*（宿主的窄化分支只认 float/double）；
  2. **可空槽不窄化**：`int? x = 5.0` 宿主一声不吭；
  3. **非常量不窄化**：`byte b = someLong` 宿主也不报，只有常量越界才报。
- 验证：
  - 98 组矩阵 + 4 组边界逐条对照宿主：**误报 0、措辞差 0**；
  - 漏报 16 条，全部落在两类已知缺口（见 A486 / A487），没有第三类；
  - 自举 64 个源用宿主 zanc 重建 gen1 通过（自举自己的源码没有被误伤）；
  - 新增 4 条拒绝用例 `tests/selfhost/reject/narrowing_*.zan` 进
    `tests/selfhost/reject_cases.txt`（三段式：自举措辞 + 宿主措辞），宿主与
    自举逐字相同（含枚举目标与 return 路径）。
- 教训：**"看起来一样"的两种类型可能不是一回事**。自举把 `decimal` 类型映射到
  `double`（`parser.zan`），第一版按"能区分"实现，结果把
  `decimal d = 5.0m;`（合法）判成了错——**误报比漏报贵得多**：漏报只是放过一个
  该拒的程序，误报是拒绝一个该过的程序。宁可把两边都放过。
- 关联：A477（同一处检查的另一半）、A486、A487、A463。

### [ ] A486 自举不判「枚举目标」的通用可赋值性——`Color c = "x";` 在自举里编译通过

- 现象：宿主报
  `cannot convert 'string' to 'Color' in initializer: no implicit conversion`；
  自举不报（枚举目标在自举的 `CheckAssignable` 里直接 return）。
- 机制：A477 把枚举目标划到"用户类型"一类（宿主靠类层次判定，自举的类型模型
  没有层次），于是枚举目标**整类**不参与通用可赋值性判定——连明显不匹配的
  `string` / `bool` 值也放过了。
- 影响：自举对枚举槽比宿主宽；同一个程序两边判定分叉。
- 方向：枚举目标可以单独判**原始类型**的值（string / bool / 浮点等），不必建模
  类层次——但要避开宿主的一个例外：`Color c = 5.0m;`（decimal 值）宿主**不报**，
  所以判的时候要把 decimal 排除掉。
- 关联：A485（枚举目标在窄化那一档已经判了）、A477。

### [ ] A487 自举把 `decimal` 类型映射成 `double`——目标侧无法区分 `decimal` 与 `double`

- 现象：宿主有真正的 `decimal` 类型，与 `double` **互不隐式转换**
  （`double d = 5.0m` 报 *cannot convert*，`decimal d = 5.0` 也报）；
  自举的 `parser.zan` 把 `TK.Decimal()` 映射成 `"double"`，AST 里 `decimal`
  目标与 `double` 目标**是同一个东西**。
- 影响：A485 的 16 条漏报里有 12 条来自这里：目标侧一旦涉及 decimal，自举就
  分不清该按 `double` 规则（拒 decimal 值）还是按 `decimal` 规则（收 decimal 值）。
- 方向：要么让自举保留 `decimal` 这个类型名（parser 不再映射，irgen 侧仍按
  double 发射但类型名留着），要么明确接受"decimal 目标侧一律放行"这条边界。
  前者更彻底，但会牵动 irgen 的类型判定。
- 关联：A485（本次的漏报清单）、A463（诊断里类型名不渲染可空标志，同属
  "自举的类型名比宿主粗"这一类）。
### [x] A478 「标题栏浮动组件忽略标题栏高度」——浮动件的基准没让开父容器的顶部停靠带

- 现象（用户 2026-10-21 报）：「标题栏浮动组件会忽略标题栏高度」。现场确认为
  **生成出来的程序窗口：自定义标题栏 + 浮动组件叠在标题栏上**。
- 根因：浮动（绝对定位）件的定位基准是父容器的**内容框上沿**，而作者自绘的
  标题栏是父容器的一个 `dock=Top` 子项，占掉了内容区上沿一条——基准没让开它，
  `fy=0` 的浮动件就正好压在标题栏上。这条与框架自带 chrome 无关：自带标题栏
  走 `App.ContentTop()`，此前逐路径核查过，各路径本来就正确。
- 修法（框架层，一处口径两处实现）：
  - 运行时 `Control.ArrangePositioned`：浮动件基准 = 内容区上沿 + **父容器最上面
    那条**顶部停靠带（`dock=Top`、可见、非浮动件、非布局内部流式容器）的高度。
  - 设计器 `Designer.DesignSlotOriginY` 用同一口径，并按"正在编辑的字段"排除
    该字段自己的停靠带——否则流式→浮动的转换那一帧基准变一次，控件跳一格。
  - 只让开最上面那一条：浮动件本来就浮在内容之上，让开全部停靠带会把它推到
    内容底部，作者 `fy=0` 的意图全丢。
- 验收：新增 `tests/gui/designer_float_band_test.zan`
  （`conformance_gui_designer_float_band`）——浮动件落在标题栏下沿、没有停靠带
  的容器里仍贴内容区上沿、设计器预览与生成运行时矩形一致。
- 教训沉淀：`.agents/skills/gui-design/references/html-window.md`
  《浮动件的定位基准：让开父容器最上面那条停靠带（框架层硬规则）》。

### [x] A479 声明前瞻不认「可空标记 + 数组」与「限定名 + 可空标记」

- 现象（2026-10-21 修 A469 时实测，探针 `_scratch/a469_probe4.zan`）：
  - `Encoding?[] a = new Encoding?[2];` → `error: unexpected token '[' in expression`；
  - `System.Text.Encoding? e = null;` → `error: expected ':', got ';'`（被当成三元）。
- **实测边界（矩阵探测，不是单条探针）**——改前：

  | 形态 | 改前 | 机制 |
  |---|---|---|
  | 关键字类型 `string?` / `string?[]` | OK | 关键字分支**不看** `?` |
  | 简单用户类型 `Encoding? e` | OK | `TK_QUESTION` 分支 |
  | 简单用户类型 `Encoding?[] a` | **FAIL** | `TK_QUESTION` 分支排除 `?` 后的 `[` |
  | 限定用户类型 `System.Text.Encoding? e` | **FAIL** | `TK_DOT` 分支**不认** `?` |
  | 限定用户类型 `System.Text.Encoding?[] a` | **FAIL** | 同上 |

  → 真正的分界线是**两个正交维度**：「`?` 后是否跟 `[]`」+「是否限定名」。
  （台账初版把差别误写成"简单名 vs 限定名"，此处更正；关键点是 `string` 是
  **关键字 token**、走关键字分支，而 `Encoding` 是标识符、走 `TK_IDENT` 分支。）
- 定位：声明前瞻 `src/compiler/parser.c` 的 `looks_like_var_decl`。
  - `TK_QUESTION` 分支（简单名 + `?`）原来**显式排除** `?` 后的 `[`；
  - `TK_DOT` 分支（限定名）扫完点号链只认 `<...>`（泛型实参）与 `[]`（数组），
    **没有处理 `?`** → `Encoding?` 被当三元问号吃掉。
- 修法（两处都改）：
  - `TK_QUESTION` 分支：`?` 之后**允许秩后缀**（方括号内只允许空白与逗号）；
  - `TK_DOT` 分支：新增 `?` 处理（同样允许秩后缀）；
  - 两处都**收紧**到"标识符 + `;` 或 `=`" → 限定名开头的三元表达式
    `A.B ? c : d` 不会被误判成声明；
  - 秩后缀扫描只允许空白与逗号 → 三元里的数组字面量 `cond ? [1,2] : x` 不被误认。
- 影响面：命名空间里的引用类型想要一个**可空数组**（或限定名的可空引用）就写不出来，
  只能退回 `Encoding[]`（丢掉可空）或给类型换个本地名字。属于**语法层**缺口：
  差分门看不见（它比的是能编出来的程序），但用户直接撞墙。
- 验证：
  - 新增 `tests/conformance/nullable_user_type.zan` / `.out` 锁住四种可空形态
    （限定名可空 / 限定名可空数组 / 简单名可空数组 / 关键字可空数组）+ 两条
    三元表达式（简单名与限定名开头都不被误判成声明）→ 单跑通过；
  - `pkg_sweep` 62 个包全绿；`diag` 153/153 通过；模板门 37 个模板 0 finding；
  - `conformance` smoke 150 个用例：8 条失败全是 `conformance_gui_*`——其中 7 条
    与改前**同一批**既有 GUI 用例；新增的 `case_gui_qrcode` 归因于另一会话
    新加的 `Zan.Barcode` 包与 `Zan.Gui` 的 `QrMatrix` 同名
    （`ambiguous type 'QrMatrix'`），该用例里没有任何 `?` / `[]` 类型声明，
    与本次前瞻改动无关；
  - **自举侧本来就接受这四种形态**（`src/selfhost/parser.zan` 的 `SkipType()`
    本身就处理 `?` 与 `[]`）→ 宿主这次是把两边**拉齐**，不是新增分叉。
    （自举另有自己的洞：语句开头的三元表达式，见 A481。）
- 教训：前瞻这类"读几个 token 猜意图"的代码，**边界要拿矩阵测**——探针只覆盖
  你想得到的形态，矩阵才暴露真正的分界线；而且前瞻改动波及**全仓每个以标识符
  开头的语句**，回归面远大于看上去。
- 关联：A469（数组的元素类型要过名字解析）、A481（自举侧同类洞）、
  §2（语法解析器分派契约）、
  `.agents/skills/zan-compiler-internals/references/frontend-binder.md` §11。

### [x] A480 保留字当名字用时诊断不点名——134 条报错没有一个说是哪个保留字

- 现象：外部项目 `mahjong` 的 agent 连撞四次保留字（`base` / `out`），
  编译器给出的 134 条报错全是 `expected variable name` / `expected member name` /
  `expected ')', got 'base'` 这类**通用语法消息**，**一条都没说是保留字撞车**——
  只能靠"换个名字就好了"反推，定位成本极高。
- 根因：解析器在"这里应该是个名字"的位置只发通用消息（`src/compiler/parser.c`
  里 12 处 `zan_diag_emit(..., "expected variable name")` 之类），不看当前 token
  是不是关键字；而 `zan_token_kind_name` 虽能反查词面，对未知 kind 却返回
  `"???"`，当不了"是不是保留字"的谓词。
- 修法：
  - `src/compiler/lexer.c` 新增 `zan_lexer_keyword_text(kind)`：拿关键字表
    （`s_keywords`）反查词面，不是关键字返回 NULL——**关键字表是唯一事实源**，
    不在编译器里另抄一份保留字清单；
  - `src/compiler/parser.c` 新增 `parser_take_reserved_word`：名字槽撞上保留字
    就**点名 + 吃掉 token**（词面留作占位名，让解析继续往下走），把 12 处
    "这里应该是个名字"的位置都改走它；不是保留字就走原来的通用消息；
  - **吃掉 token 是关键**：留着的话外层会接着补一串级联报错——字段那条会变成
    "expected type" + "expected member name"（第一条还不说是什么错），成员
    访问那条会变成四条 "expected ')'"。吃掉之后每个保留字**只报一条**点名
    诊断。
- 边界（刻意不动）：保留字在 `base` 有合法语义——`base(...)` 构造器初始化器与
  `base.Method()` 表达式（`AST_BASE_EXPR`）照旧；本次只改"这里应该是个名字"的
  位置。没改编译器的关键字规则本身（`base` / `out` 是 C# 风格保留字，自首版
  `4a5809755` 起即存在）。参数位**没有**给"无名字参数"新增任何诊断——避免给
  既有合法代码引入新报错。
- 验证：
  - 9 个探针（局部变量 / 字段 / 字段逗号续列 / 成员访问 / 参数名 / 类型名 /
    for 变量 / 局部变量逗号续列 / 局部函数）逐个跑：每个保留字出现点**恰好
    一条**点名诊断，零级联（改前字段那条 5 条、成员访问那条 6 条）；
  - `base` 的两种合法用法照旧可用（构造器初始化器 `base()` + `base.Method()`，
    探针 `_scratch/reserved/k_baseok.zan` 编译并运行通过，输出 2）；
  - `ctest -R "diag_reserved_word"` → 3/3 Passed（新增
    `tests/diag/reserved_word_as_field_name.zan`；`tests/run_compile_error.cmake`
    新增可选 `EXPECT_ERROR_COUNT`，把"只有一条错误"也锁住）；
  - 19 个外部 GUI 项目全量回归 → OK=19 / FAIL=0，5872 个编译单元 0 error。
- 教训：诊断要说"是什么错"，不是"哪里不合语法"。通用语法消息在语法**确实**
  坏掉时够用，但撞保留字这类**词法层**问题会把它变成噪音——一条能点名的诊断
  省掉的是"逐个试改名"的那一轮。**点名之后还得收级联**：错误恢复时把错位的
  token 吃掉，解析才能继续往下走，否则点名诊断下面跟一串噪音，用户还是得逐条
  读。
- 关联：A476（同类"诊断不说到点上"）、A469 / A479（解析器前瞻与分派）。

### [x] A481 自举把「语句开头的三元表达式」当成声明——`flag ? a : b` 在自举里编译不过

- 现象（2026-10-21 修 A479 时顺带实测，探针 `_scratch/a479_ternary_probe.zan`）：
  同一个程序，宿主编译并运行通过（输出 `then`），自举 gen1 报三条错：

  ```
  _scratch/a479_ternary_probe.zan:13:33: error: syntax error: unexpected '.'
  _scratch/a479_ternary_probe.zan:13:52: error: syntax error: unexpected ':'
  _scratch/a479_ternary_probe.zan:13:9:  error: undefined type 'flag'
  ```

  简单名（`flag ? a : b`）与限定名（`Shop.Item.ok ? a : b`）两种写法都炸。
- 根因：自举的声明前瞻 `src/selfhost/parser.zan` 的 `IsDeclStart()` 只要求
  「类型后面跟一个标识符」，**不要求后面是 `;` 或 `=`**；而 `SkipType()` 把 `?`
  一律当可空标记吃掉。于是 `flag ? a : b` 被拆成「类型 `flag?` + 名字 `a`」
  → 判成声明 → 后面撞上 `:` 与 `.` 就炸。
  宿主的同一处（`looks_like_var_decl` 的 `TK_QUESTION` 分支）要求
  「标识符 + `;` 或 `=`」——**是自举比宿主落后一档**（两后端分叉）。
- 影响：凡是「语句开头就是三元表达式」的代码，自举编不过、宿主能编。典型写法：
  `ok ? DoA() : DoB();`。差分门里没有这种用例，所以门一直是绿的。
- 修法：`SkipType()` 报告它是否吃到可空标记（新增 `skipNullable` 字段），
  `IsDeclStart()` **只在类型带 `?`** 时追加要求「名字后面是 `;` 或 `=`」。
  类型不带 `?` 的局部函数（`int Add(int a) {}`）走原来的宽松规则，不受影响。
- 验证：
  - 自举 64 个源用宿主 zanc 重建 gen1 通过（没把自举自己的源码解析坏）；
  - 回归：gen1 对 `tests/selfhost/prog1.zan` 生成的 IR **逐字节相同**
    （改前/改后 SHA256 都是 `9E54D3DC…`）→ 没有代码生成回归；
  - 自举差分门子集（`stmt_braceless_bodies` / `var_call_initializer` /
    `for_loopvar_capture` / `static_property` / `type_arity_ok` /
    `generic_arity_check` / `lambda_value_return` / `const_narrowing` /
    `array_init_foreach` / `switch_enum_exhaustiveness`）→ 10/10 PASS；
  - 新增 `tests/conformance/ternary_stmt_start.zan`（简单名 + 限定名两种写法，
    臂是非 void 的括号赋值，避开 A482）并**加进 `tests/selfhost/diff_cases.txt`**
    → 差分门跑它：宿主与自举输出逐字相同（`n=1` / `m=3`）。
- 教训：两个后端的**声明前瞻是同一条战线**——一个认了、另一个没认就是分叉，
  而差分门只比"能编出来的程序"，看不出"一个能编一个不能"。前瞻规则要写清
  「什么算声明」：名字后面必须是 `;` 或 `=`；`?` 到底是可空标记还是三元问号，
  靠这一条区分。
- 关联：A479（宿主侧同一条战线）、A482（自举 irgen 的 void-phi 洞）、
  `.agents/skills/zan-compiler-internals/references/frontend-binder.md` §11。

### [ ] A482 自举把「void 臂的三元表达式」降成非法 IR（`phi void`）

- 现象（2026-10-21 修 A481 时被差分门照出来，用例
  `tests/conformance/nullable_user_type.zan`）：宿主编译运行正常；自举 gen1
  **解析通过**（A481 修好之后），但生成的 IR 过不了 clang：

  ```
  build/selfhost_diff_subset/nullable_user_type.ll:1577:15: error: void type only allowed for function results
    1577 |   %t127 = phi void [ 0, %L1 ], [ 0, %L2 ]
  ```

- 根因：自举的 irgen 降三元表达式时**无条件**给结果建一个 phi；当两臂是
  **void 表达式**（`flag ? Console.WriteLine("a") : Console.WriteLine("b");`）
  时 phi 的类型成了 void → LLVM 不允许 `phi void` → IR 非法，clang 拒绝。
  宿主没这个问题（void 结果不建 phi）。
- 影响：自举编不出「语句开头的三元且两臂是 void 调用」的程序——而这正是最
  常见的一种三元写法（`ok ? Log() : Log2();`）。
- 方向：自举 irgen 在两臂类型是 void 时**不建 phi**（直接按分支走），
  或把结果槽降级成 void 的分支合并，与宿主 `irgen` 的 void 处理对齐。
  - 动手前先定位 `src/selfhost/irgen_expr.zan` 里三元（cond expr）的 phi 生成，
    对照宿主的同类分支；**`src/selfhost/irgen_expr.zan` 当前有并发会话的在途
    改动**，动手前先核对工作树，别撞车。
- 验证：把 `tests/conformance/nullable_user_type.zan`（void 臂）加进
  `tests/selfhost/diff_cases.txt`，差分门跑它应 PASS。
- 关联：A481（自举侧同一条战线的前瞻洞）、A469 / A479（宿主侧解析）。

### [x] A483 `selfhost_gen1` 这个 ctest 链接不过——它的 stage 3 没带运行时对象

- 现象：`ctest -R "^selfhost_gen1$"` 失败在 stage 3（clang 链接 gen1 产出的 IR）：
  `LNK2019: 无法解析的外部符号 zan_rt_dbl_str`（连带 zan_hw_base64_encode 等）。
  stage 1（用宿主 zanc 重建 gen1）与 stage 2（gen1 编译 prog1.zan 出 IR）**都过**，
  挂的只是最后的链接 + 运行 + 对 stdout。
- 根因：`tests/run_selfhost.cmake` 的 stage 3 只 `clang ${OUTLL} -o exe`，**没有把
  运行时对象带进去**；而差分门 `tests/run_selfhost_diff.cmake` 是带
  `${RTOBJS} ${SYSLIBS}` 的。运行时拆成独立 bundle 之后，这个 ctest 就再没人
  修过 → 它一直红着，且红的原因跟自举正确性无关。
- 修法（两处）：
  - `tests/run_selfhost.cmake` 的 stage 3 按差分门的样子把 `${RT_OBJS}` 与
    `${SYSLIBS}` 一起传给 clang；对象缺失时给出明确的"先 build bundle"提示，
    不要再让人对着一个 LNK 错误猜；
  - `CMakeLists.txt`：`selfhost_gen1` 与 `selfhost_json_ignore` 两个测试的**注册
    挪到 `zanrt_selfhost_bundle` 之后**（在那里 `_sh_rt_objs` / `_sh_syslibs`
    才存在），并把这两个参数传进去。原来它们注册在 bundle 之前，传不了。
- 验证（自举源码取 HEAD 的一份干净副本，避免撞上并发会话在途的改动）：
  - `selfhost_gen1` 的四个阶段全绿——"OK: gen1 IR clang-compiled, ran, and stdout
    matched for prog1.zan" + "OK: gen1 rejected malformed ..."；
  - `selfhost_json_ignore` 同样四阶段全绿（**这一步顺带逼出了 A484**：补上
    运行时对象之后，它才走到"IR 非法"那一步）；
  - gen1 对 `prog1.zan` 的 IR **逐字节不变**（SHA256 仍是 `9E54D3DC…`）。
- 备注：**差分门（`selfhost_diff`）本身是好的**，只是它的 bundle 对象曾经是陈的
  —— 重建 `zanrt_selfhost_diff_bundle` 之后差分门能跑通（A481 的验证就是这么做的）。
  也就是说，差分门此前很可能长期在用**旧对象**评估新自举。
- 关联：A481（自举侧同一条战线）、A484（自举 IR 的无类型 null）。

### [x] A484 自举 IR 把 `null` 当实参发出——LLVM 要的是 `i8* null`

- 现象（2026-10-21 修 A483 时撞见）：自举把 `tests/selfhost/json_ignore_ok.zan`
  编译出的 IR，clang 拒收：

  ```
  a483_json_ok.ll:489:77: error: expected type
    489 |   %p = call i8* @zalloc(i64 24, i8* bitcast (void (i8*)* @zsb_dtor to i8*), null)
  ```

- 根因：自举 irgen 的 `ContainerCid()`（`src/selfhost/irgen.zan`）在"这个类型
  不是内建容器"时返回**裸 `null`**，直接拼进 `@zalloc(size, dtor, cid)` 的第三个
  实参。LLVM 的调用实参必须带类型，裸 `null` 只在少数常量位置合法；作为实参要写
  `i8* null`。
  - 触发条件很隐蔽：**用户自定义了与内建容器同名的类**（`class StringBuilder` /
    `class List` / `class Dict`）→ `IsContainerType()` 判 false → 走 null 分支。
    所以只有这种程序会炸，普通程序一切正常——差分门 85 个用例一个都没撞上。
- 修法：`ContainerCid()` 的 null 分支返回 `"i8* null"`。
- 验证：自举源码取 HEAD 的干净副本 + 本条改动重建 gen1 → `json_ignore_ok.zan`
  的 IR 过 clang、程序跑起来、stdout 与黄金逐字相同；`prog1.zan` 的 IR 逐字节
  不变（SHA256 `9E54D3DC…`）。
  - 另一会话的在途改动落定后，工作树里直接 `ctest -R "selfhost_gen1|selfhost_json_ignore"`
    → **2/2 Passed**；
  - 自举差分门子集重跑 → **11/11 PASS**（含 `ternary_stmt_start`）→ 无回归。
- 教训：**自举拼 IR 文本时，任何"空指针"都要带类型**。宿主侧的 IR 一直是
  `i8* null`（参数类型是已知的），自举把参数类型藏进字符串拼接里，就容易把
  类型丢掉——而拼出来的 IR 只能靠 clang/llvm-as 验证，肉眼看 `null` 完全合理。
- 关联：A483（同一个测试的前一道坎）、A481 / A482（自举侧同一条战线）。

### [x] A488 `Zan.Gui` 里有 4 类指向**不存在命名空间**的 `using` —— 编译期打 `ZANPKG_MISSING`

- 现象（2026-10-22 落地 `Zan.Game.Debug` 时撞到）：凡是编译到 `Zan.Gui`
  的目标，编译器都往 stderr 打四条
  `ZANPKG_MISSING namespace=Gui/Styling` / `Gui/Rendering` /
  `System/Text/Json` / `System/Runtime/InteropServices`。
  只走 stderr，**不影响退出码**，但每个依赖 Gui 的包（`Zan.Gui.Charts`、
  `Zan.Game.Debug` …）的编译输出都带这四条噪声。
- 根因：这四个命名空间在仓库里**根本不存在**——
  - `packages/Zan.Gui/src/Gui/Styling/` 与 `Gui/Rendering/` 只是**目录**，
    里面文件声明的命名空间是 `Gui`（本仓库的目录与命名空间是解耦的），
    所以没有 `namespace Gui.Styling;` 可供索引；
  - `stdlib` 里没有 `System/Text/Json/`（Json 在 `System/Json/`），也没有
    `System/Runtime/InteropServices/`；
  - 而 Gui 包里有文件写了 `using Gui.Styling;` / `using Gui.Rendering;` /
    `using System.Text.Json;` / `using System.Runtime.InteropServices;`。
  编译器扫到 `using` 就去包里按"命名空间 = 目录"找，找不到就打这条警告
  （`src/compiler/main.c` 的 `auto_include_namespace`）。
- 影响：只是噪声，不阻断构建。但它**掩盖真正的缺包警告**，也让"编译输出
  零警告"这类验收门无法成立。
- 方向：删掉这四类**无效**的 `using` 声明（先确认文件里没用到对应命名
  空间的类型——`System.Text.Json` 那两处大概率是想用 `System.Json`）。
- 修法（2026-10-22 落地）：**97 个文件、97 行**——`packages/Zan.Gui`（47）、
  `packages/Zan.Gui.CodeEditor`（47）、`packages/Zan.Industrial`（3）：
  - `using System.Runtime.InteropServices;`（15）、`using Gui.Styling;`（1）、
    `using Gui.Rendering;`（50）——**直接删**。这三个命名空间在仓库里不存在，
    编译器按"命名空间 = 目录"查不到（`zan_pkg_source_index_visit` 的
    `pkg_index_find_namespace` 按**声明的命名空间**建桶，`Gui/Rendering`
    目录下的文件声明的是 `namespace Gui;`），所以这些 `using` 除了那条警告
    之外**什么也没做**——删掉不改变任何名字解析。
  - `using System.Text.Json;`（31）——其中 **10 个文件真的在用**
    `JsonValue` / `JsonDoc` / `JsonReader` / `JsonSerializer`，**改成
    `using System.Json;`**（JSON 在 `stdlib/System/Json/`，命名空间
    `System.Json`；仓库里 6757 个文件已经这么写）。剩下 21 个没用到 JSON
    类型的，**直接删**。那 10 个文件此前是靠 `using System;` 把
    `stdlib/System` 整棵子树拉进来才碰巧解析到 `JsonValue` 的——兜底
    解析能用，但依赖的是副作用而不是声明。
  - 验证：编译拉入 `Gui` / `Gui.Component`（CodeEditor）/ `Gui.Hmi`
    （Industrial）三个包的探针，stderr 的 `ZANPKG_MISSING` 从 4 条降到
    **0 条**，退出码 0；受影响的 conformance 用例（`gui_input` /
    `unified_layout_model` / `designer_prepared_async` / `designer_html` /
    `chart_resolved` / `datatable_hardening` / `app_icon` /
    `commercial_industrial_hardening` / `ecosystem_hardening` /
    `game_render3d`）全绿。
- 关联：`Zan.Game.Debug`（`docs/ECOSYSTEM_ROADMAP.md` §1.29）。

### [ ] A489 GPU 3D 片元着色器把光照写死成固定键光——没有材质与光源 uniform 通路

- 现象：`src/runtime/zan_game.c` 的 3D 片元着色器（四份：plain / color /
  inst / inst-color）里键光方向是**写死的常量**：
  `float lam = clamp(dot(normalize(v_normal), normalize(vec3(0.4, 0.8, 0.6))) * 0.5 + 0.5, 0.0, 1.0);`
  `float shade = 0.55 + 0.45 * lam;`
  再 `frag = vec4(tex.rgb * uColor.rgb * shade, tex.a * uColor.a);`。
  而 `zan_game_draw3d(surface_id, mesh_id, mvp, color, texture)`（同文件 589 行）
  的参数里**没有任何材质 / 光源 / uniform 槽位**。
- 影响：GPU 档的光照**不可配置**——场景里放几盏灯、材质金属度多少，画面都
  一样。`Zan.Game.Render3D` 只能在 CPU 侧算一遍着色当 tint 传进去，于是画面
  是"CPU 着色 × 固定键光"两套光照叠加。
- 方向：给 3D 管线补 uniform 通路（材质槽 + 有限盏灯的方向/位置/颜色/强度）。
  注意**实例化批次路径每实例只带 20 个 float**（mvp 16 + tint 4），光源只能
  走 uniform 数组或每批次一次的提交，塞不进实例属性。
- 关联：`Zan.Game.Render3D`（`docs/ECOSYSTEM_ROADMAP.md` §1.30）。
  `src/runtime/**` 在并发会话的在途区域，本轮未动。

### [ ] A490 3D 实例化批次依赖 GL 3.3 / GLES 3.1 的 core 特性——移动端可用性未实测

- 现象：3D 批次提交（`zan_game_draw3d_batch`）用 `gl->VertexAttribDivisor`
  与 `gl->DrawElementsInstanced`（`src/runtime/zan_game.c` 719 / 724 / 768 行）。
  这两个是 **GL 3.3 core / GLES 3.1 core** 的特性；GLES 3.0（同文件 168 行的
  `ZAN_GAME_GLSL_HEAD_GLES` 用的是 `#version 300 es`）只有 ANGLE 扩展
  `ANGLE_instanced_arrays` 才有等价物。单发路径（`zan_game_draw3d` →
  `glDrawElements`，同文件 634 行）**没有**这个限制。
- 影响：`Zan.Gui` 的 EGL/GLES 上下文路径是有的（`src/runtime/gui_gl_context.c`
  的 `zan_egl_ctx_t`），移动端**单发 3D** 可用；但 `Gui.MeshBatch` 的
  **实例化批次**在 GLES 3.0 设备上没有等价实现——而 `Zan.Game.Render3D` 的
  绘制层走的正是批次路径。
- 未实测：路线图 §3 表里"Android/OHOS 是 stub"这句到底是旧结论还是现状，
  本机没有设备/模拟器，**无法验证**。`SetRenderBackend(1)` 返回 0 的真实
  原因是"这台机器没有可用的 GL 上下文（远程桌面 / 虚拟机 / CI）"，见
  `packages/Zan.Gui/src/Gui/Core/App.zan` 的 `SetRenderBackend` 注释——
  不能据此判定移动端不可用。
- 方向：Android / OHOS 真机实测（记 GLES 版本），再决定是补
  `ANGLE_instanced_arrays` 扩展路径，还是把移动端批次降级成逐实例
  `DrawMesh3D`。
- 关联：`Zan.Game.Render3D`（`docs/ECOSYSTEM_ROADMAP.md` §1.30）。

### [x] A491 三条与"测试产物路径"无关的既有红灯——ORM 调用了接口上不存在的方法

- 现象（2026-10-22 收口测试产物路径时撞到）：把 277 处用例产物路径改到
  `build\cases\` 之后，逐家族各跑一个用例，`standard` 层有 4 条红。这 4 条
  **与输出路径无关**——手工用旧路径编译同一个源，报错逐字相同。
- 真缺口（1 条，两个用例）：`packages/Zan.Data/src/System/Data/Orm/BaseRepository.zan`
  调 `this.GetExecutor().Insert<T>(entity)` / `.Update<T>(...)` / `.Select<T>()`，
  而 `packages/Zan.Data/src/System/Data/IDbExecutor.zan` 声明的是
  `Query` / `Execute` / `ExecuteScalar` / `GetProvider` 及它们的 async 版本，
  **没有 Insert / Update / Select**。于是 `policy_server_mvc_ai_endpoint`
  与 `server_mvc_readonly_action_lease` 编译即失败：
  `type 'System.Data.IDbExecutor' has no method 'Insert'`。
  方向：接口补齐这三个方法（ORM 基类已经按它们写），或基类改走 `Execute`。
  两者取一，别两边都改。
- 检查脚本自己的判定 bug（1 条）：`tests/run_package_install_mvc.cmake:41`
  报 `MVC sources were duplicated or lost: 163 -> 163`——前后数量**相等**
  仍然判失败，判据（大概是按集合差异或顺序）与其文案对不上。
- 体积预算与基线自相矛盾（1 条）：`tests/run_size_budget.cmake:61` 报
  `file 351744 > budget 300000`，同一行又打印基线 `file=351744`——**当前
  文件大小和基线一模一样**，却仍然超预算，说明预算值（300000）本身比基线
  还小，或预算比较用的不是同一个量。
- 关联：A490（同为"跑出来的红要先分清是不是自己造成的"）；本次路径收口的
  提交正文里记了同一份判据（先分宿主/自举/golden，再看是不是路径类）。
- 修法（2026-10-22 闭账）：**三条都不是"接口缺方法"**，逐条查清后各归其位——
  - **ORM 那条已经不是缺口**：编译期 ORM **不是接口方法**，而是编译器的
    **代码生成器**（`stdlib/System/Compiler/GenDb.zan`）按调用点改写成
    `__DbQ_<E>` / `__DbI_<E>` 类型化门面（`GenDbEmit.zan`）——接口上当然
    没有 `Insert`；生成器只在**类型实参是具体实体类**时才改写
    （`GenDb.IsRoot` 要求 `ClassGet(targ) != null`），`BaseRepository<T>` 里的
    `T` 是类型形参 → 改写不了 → 才报"没有这个方法"。成因是 `b95f0a97d`
    （2026-10-22 08:35，"成员存在性检查对齐发码口径"）之前检查器把
    "所在类是泛型类"当放行理由。**`b95f0a97d` 已修**，本轮实测
    `policy_server_mvc_ai_endpoint` / `server_mvc_readonly_action_lease`
    **两个用例都 PASS**（7.67 s / 9.94 s）。**接口一个方法都不用补**——
    补了反而多一份要同步的空契约（ORM 的真身是生成的门面，不在接口上）。
  - **脚本判定 bug**：真凶是**写死的源文件计数 121**（MVC 包已长到 163），
    不是"集合差异"——文案 `163 -> 163` 是文案本身误导（它只印计数，不印
    判据）。改法：去掉写死的计数，改成**按相对路径逐个核对**（每个源文件
    在安装目录都有对应文件 + 两侧计数相等 ⇒ 双射），既能抓丢失也能抓重复，
    且不再随包增长而过期。
  - **体积预算那条**：不是"预算比基线小"，而是**预算该重定基线**。查清：
    ① `file 351744` 里 **106,360 B 是 `.rsrc`**——Windows publish 会把内置
    默认图标 `assets/zan.ico` 塞进 PE（2026-10-20 从 28,752 B 换成合规的
    105,903 B，旧文件在 48×48 之后是截断的坏条目，那次是修它）；图标是产品
    资产不是运行时载荷，而这条预算的用途是抓固定大表 / 抓丢 `--gc-sections`
    → 测量改用 `--no-icon`。② `.text` 实测 199,752（预算 200,000，只剩
    248 B 余量）——这一个月 `src/runtime` +140 提交 / 净增 ~8.8K 行，
    运行时载荷整体长大（linux 侧 .text 189,074 同步长高，排除平台特有），
    预算自 2026-09-20 起没重定过基线。改法：按脚本头注的 ~1.5× 余量重新
    定基线（宿主 .text 300,000 / file 368,000；linux .text 284,000 /
    file 332,000），并把基线数写回
    `docs/archive/embedded_runtime_assessment.md`。
  - 顺带修掉脚本的**误标**：失败文案把**当前实测值**印成 `(baseline: …)`，
    读起来像"和基线一样却还超预算"；改成 `(measured: …)`。

### [ ] A493 发布的 hello world 有 84% 的 .text 是运行时——而"补 `-ffunction-sections`"这条处方已被实测推翻

- 现象（2026-10-22 重定体积预算时撞到）：`size_budget_publish_host` 实测
  `.text = 199,752`，而这份 hello world 只打印一行字。linux-x64 侧
  `.text = 189,074`——两侧同步，说明是**通用运行时载荷**，不是平台特有。
- 载荷构成（实测）：宿主运行时对象（`build/zanrt_*.obj`，CMake 现场编）的
  `.text` 合计 **167,488**——占 hello world 总 `.text` 的 **84%**；程序自身的
  代码只有 ~32 KB。所以要瘦的是**运行时**，不是生成的代码。
- **已被推翻的处方**（2026-10-22 在构建闸内重编 9 个运行时对象后实测）：
  - 文档"2026-09-07 更新 2"写的"差值收口依赖运行时对象补
    `-ffunction-sections`"**不成立**：加 `-ffunction-sections -fdata-sections`
    后 `.text` 是 **199,736**（几乎不变）——运行时里的函数**全都可达**，链接器
    无从剪：① 编译器把 `NativeMemory.*` / `Cpu.*` 降级成对 `zan_hw_*` 的
    **直接调用**，这些符号必须由一个"永远被链上"的对象携带（定时器对象就是
    那个载体）；② 硬件加速内部按 CPU 特性**函数指针分派**，且启动要跑 KAT
    自检（`zan_hw_gate` → `zan_aes_kat` …），于是 AES-NI / 软件回退等**每个
    变体都被表引用**，全活。
  - `-fdata-sections` 更是**地雷**：它把多 worker 协程驱动里 1.4 MB 的
    `.bss` 静态表搬进 `.data`，发布产物从 **245,248 → 1,821,184 字节（×7.4）**
    ——文件体积暴涨而 `.text` 纹丝不动。**别加这两个开关。**
- 真正的候选杠杆（按体量排，都得先实测再落地）：
  - **定时器 TU 里混进了整个硬件加速**：`src/runtime/rt_timer.c` 全文 1443 行，
    **最后一行**是 `#include "rt_hw_accel.c"`（4558 行），而 rt_timer.c 里
    **一处 `zan_hw_*` 调用都没有**——纯文本包含。`rt_hw_accel.c` 单独编出来
    `.text` = **53,408**，占定时器对象 74,128 的 **72%**。定时器对象是
    **每个程序都要链**的（内联协程驱动调 `zan_timer_*`），所以每个发布产物
    都背着这 53 KB 的 SHA/AES/AVX2。
  - 拆它要**按特性拆**（sha / aes / pixel 各自成对象或归档），单纯"拆成独立
    对象"不够：hello world 已经拉进 63 个 `zan_hw_*` + 17 个 `zan_aes_*`
    符号（它用到了其中一族），只有拆到特性粒度、让链接器按符号缺时才拉，
    才可能让"不碰加密的程序"少背 AES。
  - 其余大头待逐项量：定时器本体（74 KB − 53 KB ≈ 21 KB）、io（24 KB）、
    sync（22 KB）。
- 影响：体积预算只能靠"抬高阈值"来容纳，预算的**分辨力**随之下降——阈值
  取 1.5× 于基线时，一次 +64 KB 的固定表回归不会被抓到（这正是这条预算
  最初要抓的东西）。
- 关联：`docs/archive/embedded_runtime_assessment.md`（同一条，文档里"补
  `-ffunction-sections`"那句已被本次实测推翻，需一并更正）、
  A491（预算重新定基线的直接原因）。

### [ ] A494 `BaseRepository<T>` 直接实例化会**静默**返回默认值——只有继承才能用

- 现象（2026-10-22 查 A491 时撞到）：`packages/Zan.Data/src/System/Data/Orm/BaseRepository.zan`
  是公开 API，`Orm/Models/ModelColumn.zan` 的用法注释也写着"使用
  `BaseRepository<T>` 泛型仓储"。但**直接实例化**它（`new BaseRepository<U>(zsql)`）
  时，编译通过、运行不报错，结果全是默认值：`Insert` 返回 0、`FindById` 返回
  null、`SelectAll` 返回 null（后续访问直接空引用崩）、`Count` 0、`Any` false、
  `DeleteById` 0。改成**继承**再用（`class UserRepo : BaseRepository<U>`）则
  完全正常：`insert=1` / `found=alice` / `all=1` / `count=1` / `any=true`。
- 根因：`BaseRepository<T>` 是**开放泛型模板**——编译器只在派生类里特化出
  真身（`zan_parser_specialize_generic_bases`），模板体本身只留**运行期转发
  桩**（见 `src/compiler/irgen_call.c` 的 `call_receiver_is_open_generic`）。
  而编译期 ORM 代码生成器只在**类型实参是具体实体类**时才改写
  `Insert<T>` / `Select<T>`；模板里的 `T` 是类型形参 → 改写不了 → 落到桩上
  → **默认值**。真身里 `T` 已换成具体实体 → 改写成功 → 一切正常。
  所以"接口该不该补 `Insert`/`Select`"这个问题本身是错的：**它们从来不是
  接口方法**，补接口只会多一份要同步的空契约。
- 影响：任何按文档"直接 new 一个仓储"的用户会拿到**静默的错误结果**
  （写入 0 行、查不到数据），且没有一行报错——比编译失败更危险。
- 方向（按代价排序）：
  ① **运行期响亮失败**：转发桩在"没有被特化"时抛异常，而不是返回默认值
    （落点：`src/compiler/irgen_call.c` 的桩发射 + `src/runtime/` 的桩实现）；
  ② **让 `BaseRepository<T>` 不能被直接实例化**（抽象类 / 私有构造），把
    "必须继承"这条契约写进类型系统；
  ③ 至少在包文档与 `ModelColumn.zan` 的用法注释里写明"必须继承使用"。
- 关联：A491（同一处代码的另一面：A491 问"接口要不要补方法"，答案是不要）；
  `packages/Zan.Data/src/System/Data/Orm/BaseRepository.zan`。

### [x] A492 bool 的「按静态类型渲染」只补了拼接路径——插值与 `Convert.ToString` 仍按 i1 宽度判

- 现象（2026-10-22 探针 `_scratch/a442_bool_path.zan`，宿主 `build/zanc.exe` 实测）：
  ```zan
  List<string> l = new List<string>(); l.Add("a");
  bool local = l.Contains("a");
  Console.WriteLine("var     : " + local);              // true
  Console.WriteLine("call    : " + l.Contains("a"));    // true
  Console.WriteLine("write   : " + l.Contains("a"));    // true
  Console.WriteLine("interp  : " + $"{l.Contains("a")}"); // 1   ← 口径不一致
  Console.WriteLine("convert : " + Convert.ToString(l.Contains("a"))); // 1 ← 口径不一致
  ```
  同一个 bool 表达式，**存进变量**再渲染是 "true"，**直接内联**在插值和
  `Convert.ToString` 里却是 "1"。
- 根因：bool 的值在**泛型实例化的方法返回值**上（`List<T>.Contains` 这类）被
  提升成 i64。三条取串路径的判据不一样：
  - 字符串拼接（`emit_to_cstr_of`）**已经**按静态类型判——`df4f022f4` 加的
    `infer_expr_type` 分支会先 `icmp ne 0` 再渲染，所以拼接是 "true"；
  - 插值（`src/compiler/irgen_expr.c`）与 `Convert.ToString`
    （`src/compiler/irgen_call.c` 的内建特判）仍只按 **LLVM 值的宽度**判
    （`LLVMGetIntTypeWidth(vt) == 1`），i64 的 bool 掉进 itoa → "1"/"0"。
- 影响：同一个 bool 在同一行代码里长得不一样；按字符串比对/落库的代码会踩坑。
  宿主与自举两侧都还有这两条路径。
- 方向：把 `df4f022f4` 的静态类型判据（`TYPE_BOOL` → i1 直接渲染、i64 先
  `icmp ne 0`）也搬到插值与 `Convert.ToString` 两条路上；**自举侧同款**
  （`src/selfhost/irgen_expr.zan` 的 `ToCStr` 与插值/取串路径）。
- ⚠ 并发会话正在这条战线（`df4f022f4` 已修拼接路径，其工作树里
  `tests/conformance/list_wide_struct_ops.out` 的在途更新也在等这个修）——
  动手前先对齐，别两边各改一半。
- 关联：A441（bool→字符串的口径统一，这条是它的余账）。
- 现状（2026-10-22 已闭账）：宿主侧抽出一个共用助手
  `emit_bool_cond`（`src/compiler/irgen_generics.c`，紧跟 `emit_bool_to_cstr`），
  判据是**语义类型**而不是 LLVM 宽度——`infer_expr_type` 说是 bool 就认：i1 直接
  用，宽化过的 i64 先 `icmp ne 0` 再渲染。四条取串路径全部改用它：拼接
  （`emit_to_cstr_of`）、插值（`src/compiler/irgen_expr.c`）、`Convert.ToString`
  与标量 `ToString()`（`src/compiler/irgen_call.c`）。**自举侧不用改**——
  它的 `ToCStr` 本来就按 `v.ty.sval == "bool"` 判语义类型。
- 回归：`tests/conformance/bool_widened_rendering.zan` 覆盖七条路径（变量、拼接、
  插值、`string.Format`、`Convert.ToString`、`ToString()`、`Console.WriteLine`），
  全部渲染成 true/false；该用例进了自举差分门清单（`tests/selfhost/diff_cases.txt`）。
- golden：9 份按宿主当前实际输出重写（dict_remove_churn、json_entity_mapping、
  linq_csharp_overloads、linq_equality、missing_values、nested_delegate_decl、
  orm_zansql、regex、stream_io）；`web_cookie_headers.zan` 源码里写死的旧口径
  `"1"` 改成 `"true"`。**原先以为要改 11 份**——`chart_axis_breaks` 与
  `directory_paths` 里根本没有 bool（`prct=1` 是百分比、`dirs=1` 是计数），
  上轮重写只动了行尾（HEAD 里那两份带 CR CR LF），已还原成 HEAD 原字节，
  不进本次提交。
- 教训：**宽度不是类型**。判据取 LLVM 值的宽度时，同一个 bool 在不同上下文里
  长得不一样（插值/`Convert.ToString`/`ToString` 渲染 "1"，拼接渲染 "true"）。
  凡是把值转成字符串的地方，判据都该取语义类型。另一条：golden 里的 bool
  常是**连着几个**（`"plus=" + IsMatch() + IsMatch()` 渲染成 "10"），按词边界
  判会漏——重写 golden 必须按**逐位**对齐（老侧一个 0/1 对新侧一个 true/false），
  这是 A441 漏判 123 份的同一个坑。
### [ ] A496 自举的 `ToCStr` 不认可空值——可空 bool 走拼接路径生成非法 IR

- 现象（2026-10-22 随 A492 修复时查实，自举后端 `src/selfhost/irgen_expr.zan`）：
  可空 bool（`bool?`）走**拼接**路径（`"x" + b`）时，自举生成的 IR 是
  `icmp ne i64 <装箱指针>, 0` —— 拿**装箱后的对象指针**当 i64 比较，
  IR 非法，编译死在 LLVM 校验上。
- 根因：自举的 `ToCStr` **没有可空分支**。`Console.WriteLine` 那条路自己拆箱，
  拼接这条路没有；于是可空值直接以「装好的对象」的形态进了取串逻辑。
- 为什么自举不 fail-closed：宿主侧这类错误会报诊断，自举侧直接生成非法 IR，
  死在后端校验而不是告诉程序员哪儿错了。
- 障碍：自举**没有真装箱原语**（A454）——拆箱要先能落地。当前只有「按类型
  分流」这一半可做。
- 方向：给自举的 `ToCStr` 补可空分支，判据与宿主一致（可空先拆箱再取串；
  拆不出来就报诊断，不生成非法 IR）。
- 关联：A492（同一族取串路径的宽度/类型判据）、A454（自举缺真装箱原语）。
### [x] A497 泛型类体内的成员存在性检查被**整类放行**——调不存在的方法能编得过

- 现象（2026-10-22 收窄 ORM 泛型豁免时撞到）：宿主 checker 的
  `checker_call_is_runtime_dispatched` 只要发现"所在类是泛型类"就跳过调用位置的
  成员存在性检查，于是泛型类里调一个谁都没有的方法（`shape.NoSuchMethod()`）照样
  编得过，一路跑到运行期才炸。
- 根因：豁免的**正当理由只有一条**——ORM 泛型基类模板体里的 `db.Select<T>()`
  成员集编译期判不出来（`IDbConnection` 上并没有 `Select`；编译期 ORM 代码生成器
  `GenDb` 只改写"类型实参是具体实体类"的调用，模板里的 `T` 不是具体实体，改写
  不了；真身由泛型基类特化在派生类里补出，模板体只留一个运行期转发桩）。但当时的
  实现把"所在类泛型"整类当成豁免条件，而不是"**这个调用自己的类型实参里带类型
  形参**"——豁免面比理由大了整整一层。
- 修法（2026-10-22 闭账）：豁免判据收窄成两条**同时成立**——① 所在类/结构体是
  开放泛型模板（`type_decl.type_params.count > 0`）；② 该调用的类型实参里带类型
  形参（`call.type_args` 解析后含 `TYPE_TYPE_PARAM`）。实现上把
  `checker_call_is_runtime_dispatched` 的入参从"接收者类型"改成"调用 AST 节点"
  （`check_member_access` 增 `call` 形参：AST_CALL 分支传 `expr`、AST_MEMBER_ACCESS
  分支传 `NULL`），判据才看得到调用自身的类型实参。
  - 只留 ② 的坑（第一版实测）：非泛型类里的泛型方法 `h.Missing<T>()` 会被放过，
    错误滑到发码阶段只剩 `'Helper' has no member 'Missing'` + `code generation
    failed`，比 checker 直接点名差得多，且与 irgen 口径分歧 → ① 不可省。
  - 收窄后的豁免集合 **⊆** irgen `call_receiver_is_open_generic` 的豁免集合
    （后者三条：所在类泛型 / 接收者含形参 / 实参含形参），所以不会出现"checker
    放行、irgen 报错"的两侧口径分歧（这正是 skill 第 10 条要求的）。
- 验证（2026-10-22）：
  - 新增 `tests/diag/generic_class_unknown_method.zan`：泛型类里对接口/类/自身
    三类接收者各调一次不存在的方法，用 `EXPECT_ERROR_COUNT=3` 钉住——豁免一旦
    改回整类放行就变 0 error，用例必红。
  - 新增 `tests/conformance/generic_open_call_template.zan`（+ `.out`）：ORM 泛型
    模板最小复刻（`BaseDao<T>` 模板体 `db.Select<T>()` + `SysUserDao :
    BaseDao<SysUser>`），运行输出 `PASS`——收窄后模板体仍要编得过。
  - 回归：`generic_base_dao_orm`（56 files，PASS）、`orm_zansql`（79 files，输出
    逐项一致）、`server_mvc/readonly_action_lease`（432 files）、全部 36 条
    `generic*` conformance 用例逐个编译（0 失败）、自举源 36 文件（64 files）、
    `scripts/pkg_sweep.ps1` 全包 sweep 零 RED（含 Zan.Sdk.Wechat 5619 files）。
- 关联：A491（ORM 那条不是"接口缺方法"的缺口，接口一个方法都不用补；成因是
  `b95f0a97d` 之前的过宽豁免，本条是沿同一方向的进一步收紧）；A494
  （`BaseRepository<T>` 直接实例化静默返回默认值，仍开）；宿主与自举仍有分叉——
  自举侧 `src/selfhost/checker.zan` 的 `CheckCall` **完全没有泛型豁免**（自举还
  编不了 ORM 泛型基类），本条只改宿主。

