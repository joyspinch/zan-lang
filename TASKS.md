# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

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
- A445 的连带影响（2026-10-22 补记）：宿主把 `decimal` 降成 struct，所以
  `Math.Sqrt(2.5m)` 被宿主拒绝（"requires an int or floating-point argument"）、
  `Convert.ToInt32(2.5m)` 宿主给 0；自举因为把 decimal 折叠成 double，
  `Math.Sqrt(2.5m)` **照收**、`Convert.ToInt32(2.5m)` 给 2。A445 只在自举侧
  补了"按静态类型分流"的守卫，这两条 decimal 差异要等本条目把 decimal
  还原成独立类型才会消失。

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

### [ ] A493 发布的 hello world 有 84% 的 .text 是运行时——已改归档成员收口大半，win/android 交叉待工具链

- 现象（2026-10-22 重定体积预算时撞到）：`size_budget_publish_host` 实测
  `.text = 199,752`，而这份 hello world 只打印一行字。linux-x64 侧
  `.text = 189,074`——两侧同步，说明是**通用运行时载荷**，不是平台特有。
- 载荷构成（实测）：宿主运行时对象（`build/zanrt_*.obj`，CMake 现场编）的
  `.text` 合计 **167,488**——占 hello world 总 `.text` 的 **84%**；程序自身的
  代码只有 ~32 KB。所以要瘦的是**运行时**，不是生成的代码。
- 根因（2026-10-23 钉死）：**不是"函数全可达"，是"链接的是裸 .o 而不是归档"**。
  - `src/runtime/rt_timer.c` 末行是 `#include "rt_hw_accel.c"`（**文本包含**），
    而 rt_timer.c 里 **0 处 `zan_hw_*` 调用**。文本包含 ⇒ crypto 与定时器同 TU
    ⇒ 必然整体进 `.o` ⇒ 必然整体进每个产物。`rt_hw_accel.c` 单独编出来
    `.text` = **53,408**，占定时器对象 74,128 的 **72%**。
  - 关键对照（同一 TDM gcc / 同一 `--gc-sections`）：
    | 链接方式 | 结果 |
    |---|---|
    | crypto 作为**裸 .o** 直接进链接行 | `zan_hw_=63` 全留，`.text=156,640` |
    | crypto 打进**静态库** `libcrypto.a` | `zan_hw_=0`，`.text=103,232` |
    | 裸 .o + `--gc-sections` + `-ffunction-sections` | 仍 `zan_hw_=63` |
    → **裸 .o 进链接行 = 整体载入，section GC 对它无效**；只有归档成员才按
    "符号缺才拉"。这也推翻了"补 `-ffunction-sections`"那条处方——那个实验做在
    **裸 .o** 上，前提就不成立。`-fdata-sections` 更是**地雷**：1.4 MB 的
    `.bss` 静态表被搬进 `.data`，发布产物 **245,248 → 1,821,184 字节（×7.4）**。
    **这两个开关都别加。**
  - 交叉侧同因同解：交叉链接本来就带 `--gc-sections`（`ld.lld -static ... -s
    --gc-sections`），**带了照样剪不掉**，反证根因不是"缺 GC 开关"。
- 落地（2026-10-22 收口）：
  - 宿主：`rt_timer.c` 去掉文本包含；crypto 由 CMake 编成 `libzancrypto.a`
    （`zanrt_crypto` 规则），zanc 在 **13 处**链接点注入该归档（原生 / 交叉 /
    dylib / DLL / wasm 全覆盖）。
  - 交叉：5 份配方（`build_linux_rt.sh` / `build_macos_rt.sh` /
    `build_ohos_rt_zig.sh` / `build_win_rt.sh` / `build_cross_rt.cmd`）统一改成
    "编 timer + 编 crypto + 打归档"；linux/macos/ohos/wasm32 的 timer 与
    `libzancrypto.a` 已用 zig 0.15.1 重出并提交。
  - **顺序纪律**：归档必须排在 timer 对象**之后**进链接行——归档成员只为"仍然
    未定义"的符号解引用，timer 若已定义同名符号则成员不会被拉进来（不会重复
    定义）；反过来排则同一符号被定义两次。
  - 归档与 timer 对象必须**同一代**重出：旧的 timer（内含 crypto）+ 新的归档
    同链接会重复定义，新 timer + 旧归档则会缺符号。
- 实测收益：
  | 产物 | 改动前 .text | 改动后 .text | 差 |
  |---|---|---|---|
  | 宿主发布 hello（`--publish --no-icon`） | 199,752 | **146,344** | −53,408 |
  | 交叉 linux-x64 hello | 189,074 | **137,458** | −51,616 |
  - 按需拉取（**不剥符号**的链接实测：`--publish` 会剥掉符号表，数符号必须用
    未发布产物）：hello 的 `zan_hw_*`/`zan_aes_*` = **0**；用 crypto 的程序 =
    **95**（含 `zan_hw_sha256`）——归档成员按需拉取成立。macOS arm64/x64 与
    wasm32 同样成立（hello 0 / crypto 83~95）。
  - WSL 实跑交叉产物：hello 输出 `hi`；crypto 输出 SHA-256("abc") 标准向量
    `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`。
  - 回归：`ctest -R crypto` **14/14** 通过（含 determinism 孪生）；
    `ctest -R "timer|decimal|async_await|case_hello"` **32/32** 通过。
- 顺带修掉的三个真缺陷（都在本条落地时撞到，根因各自独立）：
  - `src/runtime/rt_crash.h`：musl 目标缺 `sigaltstack` / `SS_DISABLE` 声明
    （musl 把它们藏在 `_BSD_SOURCE/_XOPEN_SOURCE/_GNU_SOURCE` 后面），配方在
    现代 zig 下**根本编不过 `rt_timer.c`** —— 交叉对象因此长期停在旧代码
    （已提交的 linux timer 对象里连 altstack 代码都没有）。
  - `toolchain/linux-musl/` 缺 compiler-rt 内建（`__udivti3` / `__umodti3`）：
    128 位除法链不过，交叉 linux 任何用到 `System.Decimal` 的程序都断（已用
    zig 的 `lib/compiler_rt` 补出 `libgcc.a` 装进工具链）。
  - `toolchain/linux-*/` 缺 `zanrt_decimal.o`（decimal 运行时只补了 macOS/Android）。
- 剩余（未闭）：
  - **win-x64 / win-arm64 / android-\*** 的 timer 与归档待各自的工具链
    （MSYS2 clang / Android NDK）重出——本机没有这两个工具链，配方已改好，
    CI/持工具链的人跑一次即可。
  - 归档目前**只有一个成员**（整块 crypto）：用到任一符号仍拉进全部 63+ 个
    `zan_hw_*`——按特性再切见 A499。
- 关联：`docs/archive/embedded_runtime_assessment.md`（同一条，文档里"补
  `-ffunction-sections`"那句已被本次实测推翻，已一并更正）、A491（预算重新定
  基线的直接原因）、A499（归档按特性再切的后续）。

### [ ] A499 crypto 归档只有一个成员——用到任一族仍拉进全部 63+ 个 `zan_hw_*`

- **2026-10-24 实测校准（缩小口径）**：归档按符号缺才拉的机制已验证兑现——
  hello 的 `--publish` 产物符号表里 `zan_hw_*` 计数 **0**（完全不拉）。
  本条对**发布体积**的诉求已由 A493 归档化 + 发布 `--gc-sections` 兜底实现；
  剩余价值仅剩**开发构建**（无 gc，任何一个 `zan_hw_*` 引用仍拉整块 53 KB）
  与不跑 gc 的消费方。按族拆成员（下）仍值得做，但优先级降为开发体验项。
- 现象（2026-10-22 A493 落地时留的尾巴）：`libzancrypto.a` 现在**只有一个成员**
  （整块 `rt_hw_accel.c`）。归档机制本身按"符号缺才拉"生效，但粒度只到"整块
  crypto"：程序只要用到其中一个符号（如 `zan_hw_sha256`），整簇 **63+ 个**
  `zan_hw_*` / `zan_aes_*` 仍被拉进来（实测交叉产物 95 个符号）。
- 切分可行性已量：29 个外部入口，族分 = aes 26 / cpu 探测 23 / 其他 84 /
  pixel 4 / sm3-sm4 3 / sha 3 / base64 3 / rsa-x25519 3 / json 2 / ghash 2 /
  crc32 1；**族间仅 aes↔ghash 5 条交叉调用边**（core 依赖：aes→core 15 /
  sha 6 / sm 4 / crc 2 / ghash 2）⇒ 几乎正交，可以按族切成员。
- 方向：把 `rt_hw_accel.c` 按族拆成多个 .o 打进同一个归档（sha / aes /
  sm3-sm4 / rsa-x25519 / pixel / crc / base64 / json / cpu 探测），让"只算
  CRC32 的程序"不必背 AES。切分要保证同族内部互根的函数仍在同一成员里
  （`zan_hw_gate` 的函数指针分派表按族分组，别把 aes↔ghash 拆散）。
- 关联：A493（归档成员机制的落地，本条的直接来源）。

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

### [~] A501 跨目标全量扫描：13 个目标的 console 与 GUI 交叉链接体检

- 起因（2026-10-22）：被问"跨平台交叉编译现在都是全绿的吗"。此前只逐个目标试过
  单个程序，没有**全目标 × 多特性**的扫描，于是漏掉的东西整整一批。
- 扫描装置（`_scratch/xsweep/`，一次性）：3 个 console 探针（hello / crypto /
  decimal）+ 1 个 GUI 探针（GUI + decimal）× 13~16 个目标，逐个记录链接返回码、
  `.text` 与 `zan_hw_*` 符号数。**探针必须按特性选**：只跑 console 探针的话，
  iOS（GUI-only）与 GUI 目标专属的注入路径一条都照不到。
- 扫描暴露并已修掉的缺陷（五个，各有独立根因）：
  - `src/compiler/main.c`：交叉路径复用 `rt_crypto_buf` 这块**共享缓冲区**，
    归档找不到时仍留着指向它的指针 → 链接器拿到一个不存在的交叉路径
    （android/ios 直接报错）。改为交叉分支**无条件赋值**
    （`zan_file_exists(...) ? buf : NULL`），原生分支加 `if (!cross_compiling)`。
  - **wasm32 的链接分支根本没注入 decimal 运行时**：别的目标都有
    `if (irgen.uses_decimal_runtime)` 注入 `zanrt_decimal.o`，wasm 没有 →
    任何用 `System.Decimal` 的 wasm 程序链接期报一串
    `undefined symbol: __zan_decimal_*`。已补注入 + 补对象
    （`toolchain/wasm32/zanrt_decimal.o`）。
  - `src/runtime/rt_decimal.{c,h}`：对外 ABI 用了 `size_t`（长度/容量），
    而编译器后端按 Zan 的 `long`（**恒 64 位**）发调用 → wasm32 上 `size_t` 是
    32 位，`wasm-ld` 报 *function signature mismatch*（链接过了，模块运行时坏）。
    改为定宽 `int64_t`（其余 `__zan_decimal_*` 早就是定宽的）。
  - **ohos / win / ios / wasm 目标缺 `zanrt_decimal.o`**：decimal 运行时当初只补了
    macOS/Android/linux，用 `System.Decimal` 的交叉程序在这些目标上链接红。
  - `toolchain/ohos-{x64,arm64}/zanrt_sync.o` 停在 **10-05**，比 `rt_sync.c` 老，
    缺 `zan_ui_thread_set` → OHOS 的 **GUI** 程序链接红（console 程序照不到）。
    已用 zig 按 `build_ohos_rt_zig.sh` 的配方重出。
- 顺带修掉的**自相矛盾**：`zanc --list-targets` 是一张写死的表，把本机构建
  **发不了码**的目标也 advertise 出去（mozbuild LLVM 没有 RISC-V 后端，
  `zanc --target riscv64` 只会得到一句笼统的 "code generation failed"）。
  现在按 **LLVM 自己的注册表**过滤：目标表里没有的 triple 不出现在列表里，
  `--target` 直接点名"这个构建没有该 triple 的 LLVM 后端"（链接了带 RISC-V
  后端的 LLVM 时这些目标照旧可用）。后端注册要先做
  （`zan_irgen_init_llvm_targets()` 从 irgen_emit.c 提成对外函数），
  否则查表时注册表是空的，会把**所有**目标都判成不可用。
- 现在的体检结论：13 个目标 × console 全绿；GUI 探针 10 绿 3 红（红的是
  android-x64 与 ohos 两个平台，原因见 A502 / A503）。**OHOS 的 GUI exe 现在
  给的是一句"为什么 + 怎么办"**（静态链接装不下驱动要的 EGL/GLES/FreeType，
  GUI 请用 `--emit-lib`），不是一串 undefined symbol；OHOS 的 GUI 走
  `--emit-lib` 实测**链接成功**。
- 关联：A493（归档成员机制）、A502、A503、A499。

### [~] A503 OHOS 的 GUI 交叉链接缺 EGL/GLES——静态 exe 形态装不下预编译驱动的 GL 依赖

- 现象（2026-10-22 A501 扫描）：`--target ohos-x64` / `ohos-arm64` 链接 GUI
  程序报一串 `undefined symbol: eglMakeCurrent / eglDestroySurface /
  glCreateShader / glShaderSource / glCompileShader ...`（外加 20 多个
  `FT_*`），引用方是
  `packages/Zan.Gui/src/Gui/drivers/ohos-<arch>/static/libzan_gui.a` 里的
  `zan_gui_present`。
- 根因（2026-10-22 钉死）：**OHOS 的 exe 走全静态链接**
  （`ld.lld -static ... libc.a libunwind.a libclang_rt.builtins.a`），静态模型里
  没有 EGL/GLES/FreeType 的位置；而 OHOS 的 GUI 驱动直连 EGL/GLES
  （实测需要 10 个 `egl*` + 34 个 `gl*`；`FT_*` 是编进驱动归档的，所以不报
  undefined）。⇒ 任何 OHOS GUI 程序走 **exe** 这条路都链不过，**不是配置问题，
  是链接形态选错了**。
- 更正一处此前的误判（2026-10-22 复核 `scripts/build_gui_ohos.sh` 时发现）：
  "驱动是预编译产物、没有源码" —— **不对**。OHOS 驱动由
  `scripts/build_gui_ohos.sh` 从 `src/runtime/gui_runtime.c`（`-DZAN_GUI_OHOS
  -DZAN_GUI_FREETYPE`）+ freetype 源码编出来（用 DevEco Studio 自带的 OHOS
  SDK），再 `llvm-ar` 成 `static/libzan_gui.a`、编成 `libzan_gui.so`。
  **源码就在仓库里**，只是要一台装了 DevEco 的机器。
- **再更正一处（同日）**：本条上一版写着"**本机没有 DevEco**，这一支只能在那台
  机器上跑"——**也是错的**。DevEco Studio 就在这台机器上
  （`C:\Program Files\Huawei\DevEco Studio`，带
  `sdk/default/openharmony/native/{llvm,sysroot}`，sysroot 里有
  `x86_64-linux-ohos` / `aarch64-linux-ohos` 两套 `libEGL.so`/`libGLESv3.so`），
  freetype 源码也在（`D:/project/firefox/modules/freetype2`）。于是
  `scripts/build_gui_ohos.sh all` **在这台机器上跑通了**（2026-10-22 实跑）：
  两个架构的 GUI/image/audio/game 驱动全部重出。
  **教训：判"这台机器补不了"之前，先把常见安装位置查一遍**——凭印象下结论会
  把"能补"误写成"补不了"。
- 重出后暴露的两处**仓库缺口**（2026-10-22 实跑）：
  ① **committed 的 `libzan_gui.so` 是拆分前的旧产物**：它还带着 nanosvg / libwebp
     的符号（`nsvgCreateRasterizer` / `kVP8Log2Range` / `IsValidColorspace`），
     而当前代码已把图像解码拆到独立的 image 驱动（重出的 GUI 驱动导出 673 个
     符号，旧的 1005 个，多出来的正是那批解码符号），并且**缺**新加的
     `zan_gui_gpu_*` / `zan_gui_blit_*` 入口 ⇒ 仓库里那个 .so 是**陈旧**的。
  ② **image / game 驱动的二进制从来没进过仓库**：只跟踪了 `.libs` 与
     `.bundle`，而 `.bundle` 里写着 `libzan_image.so` / `libzan_game.so`
     ⇒ **发布出去的 OHOS 应用会缺这两个驱动**（audio 驱动的 .so/.a 是跟踪着的，
     image/game 不是）。
- 正确的形态（2026-10-22 实测）：OHOS 的 GUI 程序应当产出 **HAP**
  （`zanc --target ohos-x64 --emit-lib`）——**实测链接成功**：HAP 本身不调用
  EGL/GLES（那是驱动 .so 的事），驱动 .so 随应用进 bundle
  （`zan_gui.bundle` 里列了 `libzan_gui.so`），设备系统提供 EGL/GLES 真库。
  **静态 exe + GUI 驱动**这个组合在 OHOS 上本来就不成立。
- 补法（2026-10-22 已落地）：
  ① **静态 exe 带 GUI 驱动时给明确错误**（已改 `src/compiler/main.c`）：说明
     "静态链接装不下驱动要的 EGL/GLES、OHOS 的 GUI 请用 `--emit-lib`（HAP
     形态）"，不再吐 60 多个 undefined symbol 的瀑布。
  ② **HAP 形态按需链 EGL/GLES**（同上）：`<sys>`（=toolchain/ohos-<arch>/）里有
     就链上（记 DT_NEEDED），没有就点名缺哪个、去哪拷。**只列 EGL/GLES 两项**：
     OHOS 的 FreeType 是**静态**链进驱动的，设备上不存在 `libfreetype.so`
     ——列进去只会让人去找一个哪都没有的文件。
  ③ **配方改成拷真库**（`scripts/build_cross_rt.cmd` 的 OHOS NDK 块）：从
     `<OHOS_NDK>/native/sysroot/usr/lib/<triple>/` 拷 `libEGL.so` /
     `libGLESv3.so` 到 `toolchain/ohos-<arch>/`；原先那两个**空壳**（导不出任何
     符号，只记录 DT_NEEDED，连同 `scripts/ohos_stub.c`）已删——**空壳比缺库
     更坏**：它让人以为支持着这个目标。
  ④ **让 OHOS 的对象别再停在旧代码**：`toolchain.yml` 新增 `ohos-objects`
     job（zig 路径，`scripts/build_ohos_rt_zig.sh`），源码一改就重出并提交。
  ⑤ **驱动本身重出并补齐跟踪**（2026-10-22，本机有 DevEco）：GUI 驱动的 .so/.a
     重出（旧产物是拆分前的形态），image / game 驱动的 .so/.a **补进仓库**
     （`.bundle` 指着它们，仓库里却没有）。
- 剩下的：**真机 / 模拟器上的运行验证**（DevEco 模拟器装在这台机器上，
  `C:\Users\QQ\AppData\Local\Huawei\Emulator26.0`；仓库历史里也有 DevEco 模拟器
  跑通 GUI 画廊的记录 `e59be75a`）。**构建这一环已经不再是缺口。**
- 关联：A501、A502、A504。

### [~] A504 CI 的"配方产出"与"提交清单/触发路径"必须一一对应——漏一处，对象就永远停在旧代

- 现象（2026-10-22 查 A502 时顺着查出来的）：`.github/workflows/` 里重建
  交叉运行时对象的 job 有三处**清单与配方脱节**：
  - `drivers.yml` 的 win job：配方产出 `zanrt_decimal.o` / `libzancrypto.a` /
    `zan_embed_api.o` / `zan_inflate.o`，**提交清单一个都没有** ⇒ CI 每次把它们
    编出来又丢掉，仓库里的对象永远停在旧代（这正是 A493 之后 win 的 timer
    仍然背着一整块 crypto 的原因）。
  - `toolchain.yml` 的 linux/macos job：清单只有 `zanrt_*.o` +
    `libzancrypto.a`，**漏了 `zan_embed_api.o` / `zan_inflate.o`**（配方产出，
    清单不提交）。
  - `toolchain.yml` 的**触发路径**：只列了 `rt_io.c` / `rt_sched.c` /
    `rt_sync.c` / `rt_timer.c` / `rt_hw_accel.c`，**漏了 `rt_file.c`、
    `rt_decimal.c/h`、`zan_embed_api.c`、`zan_inflate.c`** ⇒ 改了这些源，CI
    根本不触发重建，交叉对象照旧是旧的。
- 另一头：**没有任何 job 重建 android / ohos / wasm32 / ios 的对象**（只有
  linux×3 + macos×2 + win×2 有）。ohos 已接上（zig 路径，`ohos-objects`
  job）；android 已在 `android-objects` job 里接上（NDK 路径）；wasm32 与 ios
  还没（wasm32 的配方是 .cmd、GUI 那一支还依赖外部 freetype checkout）。
- 补法（2026-10-22 落地）：三份清单补齐 + 触发路径补齐（21 条）+ 新增
  `ohos-objects` / `android-objects` 两个 job + `stale-check` 依赖 ohos job。
- **macOS 这条腿整个是死的**（2026-10-22 顺着查 macOS 的 decimal 查出来的）：
  - `runtime-objects` job 的检查步骤 `python3 scripts/check_macos_rt.py` 从
    **2026-09-20**（`e35521ad7` 让 `gui_compat_mac.c` 开始 import
    `zan_gui_draw_text` / `zan_gui_draw_polyline` / `zan_gui_font_height`，
    而检查器只认 libSystem.tbd 与同目录对象）起就是红的 ⇒ **job 在提交步骤
    之前就中止** ⇒ 一个月里没有任何 macOS 对象被 CI 刷新过。检查步骤红了
    就等于腿没了：检查在提交前面，红了根本到不了提交。
  - macOS 的 decimal 对象**四处皆无**（配方 / CI 清单 / ARTIFACTS 表 / 跟踪
    的文件）：`main.c` 的 macOS 分支会给用 decimal 的程序注入
    `toolchain/macos/<arch>/zanrt_decimal.o`，而新克隆的仓库里没有这个文件
    ⇒ **macOS 交叉编译一个用到 decimal 的程序必然失败**（报
    "bundled macOS runtime objects not found"，与"目录不存在"一样的措辞，
    容易误判成环境问题）。普通 console 程序不用 decimal，所以这条红灯平时
    看不见。
  - 补法（2026-10-22 落地）：配方（`build_macos_rt.sh` + `build_cross_rt.cmd`
    的 macos 块）加 decimal 编译；ARTIFACTS 表加两条；两个对象用 zig 重出
    并**跟踪进仓库**；检查器改成**从驱动 dylib 里读导出符号**当白名单
    （`packages/Zan.Gui/.../macos-<arch>/libzan_gui.dylib`，写死清单会让
    驱动一加新入口就又变红——这正是它当初变红的原因）；触发路径补
    `scripts/check_macos_rt.py`（检查器自己也要被 CI 验）。
- 留下的纪律（比清单本身更重要）：
  ① **"配方产出的东西"与"CI 提交的东西"是同一
  个集合**。加一条配方产物时，提交清单与过时检查的 ARTIFACTS 表要同步加，
  否则它会静默停在旧代，而且**构建是绿的**。
  ② **CI 腿的每一步都要单独验**：检查步骤红了会吃掉后面的提交步骤，整条腿
  静默停摆——而"腿停了"和"配方没产出"在仓库里长得一模一样（对象停在旧代）。
  ③ **白名单要从产物里读，不要写死**：允许集写成代码里的常量，就会在产物
  变化时过期。
- 补法（2026-10-22 第二轮落地，针对 win + 过时检查）：
  ① **win 的运行时对象由 CI 托管**（`drivers.yml` 的 windows job：MSYS2 的
     MINGW64 / CLANGARM64 clang 编 mingw ABI 对象，源一改就重出并提交），所以
     18 条 win 条目从 `manual` 改到 `runtime`；android 的 16 条同理（NDK 腿
     已跑通）。ios/wasm32 没有腿，改到 `manual`（只报不卡）。
  ② **`build_linux_rt.sh` 补产 `zanrt_mem.o`**（linux-musl 的小对象分配器内存
     对象）：过时表列了它、CI 提交清单用 `zanrt_*.o` 通配也覆盖得到，唯独配方
     没产 ⇒ 它永远停在旧代 ⇒ 门永远红。
  ③ **`build_win_rt.sh` 改成"先编到暂存目录、全成了再一次性换入"**：半路失败
     的重建原本会留下**半新半旧**的一包对象，而 CI 的提交步骤会把工作树里
     看到的**照单提交**——新 `zanrt_io.o` 配旧 `zanrt_timer.o` 就这么进了仓库
     （2026-10-22 实测：工作树里那份 win timer 带着 59 个 `zan_hw_*`，仓库里
     CI 出的那份是 0 个；用前者链 win hello，`.text` 168,528，用后者
     **112,336**，差 56 KB）。
  ④ 配方末尾加**自检**（有 nm 时）：`zanrt_timer.o` 里若又出现 `zan_hw_*`
     就直接失败并说明原因——那是 A493 的体积不变量被打破（`rt_timer.c` 又
     文本包含了 `rt_hw_accel.c`）的信号，让它在 CI 里当场炸，而不是让每个产物
     悄悄胖 53 KB。
- 关联：A502（android 的具体症状）、A503（ohos 的具体症状）、A493（归档拆分
  后 win 变胖的根因）、A501。

### [x] A506 语言服务冷启动 11~13 秒——`initialize` 同步索引了整座包仓库

- **已修（2026-10-24）**：按推荐方向落地，另拆一刀握手。实测：initialize
  **11~13 s → 239~254 ms**；包闭包挪到首个 intel 请求一次性吸收（ws506 实测
  首次补全 5985 ms、后续 20 ms）；只 import stdlib 的工程全程亚秒。
  两处关键发现：
  - **`zan_pkg_find_namespace` 解析不了两级命名空间**——它走
    `pkg_scan_store`，把命名空间当单段目录拼 `src\<ns>`，`System.Json`
    拼成 `src\System.Json` 永远 miss（实测 Gui/System 一级能中、
    System.Json/System.IO 全 0）。正确原语是编译器同款
    `zan_pkg_source_index_create` + `source_index_visit`：按每个 .zan
    **声明的** namespace 建档（probe 读文件头去注释/字符串取
    `namespace X;`），点号两级才解析得到；visit 返回源文件，LSP 侧
    `pkg_root_from_file` 反推包根去重后整根索引。Zan.Json 就是反例——
    包名 Zan.Json、命名空间 System.Json，还**扩展** stdlib 的
    System/Json（JsonDoc/JsonReader），按"stdlib 已覆盖就跳过"会丢
    包类型补全，不能走捷径。
  - **源索引构建本身要探遍包仓库**（~15k 文件 fread+扫声明，实测 ~6 s），
    不能塞进 initialize 同步路径——握手只带 stdlib+工作区，
    `ensure_packages_indexed` 挂在 doc_intel_for/didOpen 首次触发
    （一次性，后续 20 ms）。`Zan.Sdk.*` 特判随闭包自然消失（没人 using
    就不索引）。
  - using 收集：自写 `scan_dir_usings`/`scan_text_usings`（行首 `using X;`，
    Win32/POSIX 双实现），工作区做种子，包根做传导，4 轮不动点。
  - 遗留：`lsp_session_editor` ctest 在**不带本改动**时同样红——与 A506
    无关，是 21712161c 的 `LspSession.Open` 签名漂移连环账。修复面（本轮
    试到一半的实证）：① 测试客户端三处 `Open(port, root)` 要补第三参
    `new LspStartReport()`；② lambda 捕获的 session 要在 lambda 内重判空
    （检查器的可空收窄不过 lambda 边界，外面 throw 也没用）；③ 测试源清单
    （run_lsp_session_test.py）缺 `LspSession.Diagnostics.zan`
    （WireConnectionDiagnostics）；④ 真正的坎：`LspSession.Start()` 引用
    `ZanIDE.getenv/ToolchainDir/ExeDir/ExeSuffix`，测试源集没有完整
    ZanIDE——垫片类声明与检查器为未定义引用**自动合成的桩类**（连被调
    方法都合成）撞 duplicate type declaration，测试侧无解，只能要么补全
    ZanIDE 源闭包（拖 TabRecord/T() 整棵 IDE），要么把 Start 的找 exe
    逻辑抽到 ZanIDE 之外。

- 现象（2026-10-22 修 IDE 崩溃时顺带测出）：IDE 打开工程后状态栏长时间停在
  「语言服务：连接中」，实测冷启动 **10292 ms**，点重连再 **12260 ms**；
  独立跑 `build\zan-lsp.exe` 走一次 initialize 应答要 **10803 ms**（默认
  stdlib 11068 ms，热了再跑 11842 ms）。
- 判据：把 `zan-lsp.exe` 单独复制到一个没有 `../stdlib`、没有 `../packages`
  的空目录里，同样的握手只要 **5 ms**。差值全花在"索引"上，不是协议开销。
- 根因（`src/lsp/lsp_main.c`）：
  - `handle_initialize()`（:954）在应答 initialize **之前同步**调
    `ensure_project_indexed(s)`（:1135）；
  - `ensure_stdlib_indexed(s)`（:1108）调 `zan_pkg_all_source_roots(project_dir, ...)`
    （`src/compiler/package.c:1427`），这个函数按 **zan-lsp.exe 所在目录**解析
    包仓库——`build/../packages` 就是仓库的 `packages/`；
  - 于是仓库里 **8491 个 .zan** 全被 `intel_index_project()`
    （`src/lsp/intellisense.c:2911`）索引，只有 `Zan.Sdk.*` 被排除。
    stdlib 才 167 个 .zan，约 2 秒；包仓库吃掉剩下 10.8 秒。
- 已做的部分：IDE 侧握手预算本来就够（`d.waitBudget = 240` ×
  `RecvTimeoutMs() = 250` = 60 秒），不是客户端超时问题；已把连接超时从
  20 s 放宽到 60 s（按实测冷启动留 4 倍余量），并把实际耗时暴露成探针
  字段 `lsp.connectMs`。
- 方向（推荐）：**只索引工程实际依赖的包**——从工程文件/源码的 import 出发
  推出依赖闭包，`zan_pkg_all_source_roots` 换成"按需解析"而不是"扫全仓库"，
  启动快与补全不缺才能同时成立。备选：initialize 先应答、索引转后台并发
  进度通知（要处理 C 端线程安全）；或持久化索引缓存（要处理失效判定）。
- 关联：本轮已提交的 `feat(ide): 语言服务状态常驻状态栏`（把"连接中"如实
  显示出来并给一键重连）。
- 坑出处：探针的等待时长必须按被测服务端的**实测**耗时定——本轮最初按 10 s
  等，真实冷启动 13 s，直接判成假失败。

### [ ] A542 Panel.Column/Row 的流式轴只对 With() 生效：Add() 的子项全叠在容器原点
- 现象：代码建的纵向面板（资料卡、设置页导航列、工作台详情卡、聊天抽屉、
  资料名片）里，多个子项全部叠在容器左上角同一处——头像压在按钮上、六行
  键值挤成一条、两个小节标题跑到卡片顶端。布局自检不报（重叠的兄弟矩形
  相同但都在同一容器原点），只有像素截图看得出来。
- 根因：`Panel.Column()`/`Panel.Row()` 的流式轴由 `With()` 兑现——`Control.With`
  会把非 grow 的子项改成沿流式轴停靠（列→dock=1，行→dock=3），于是子项在
  停靠排布里依次往下/往右排。`Control.Add()` 不做这层归一化：子项 dock 仍是
  0（手摆），停靠排布里 dock==0 的子项一律排在容器原点（`Control.zan` 约
  2924 行的 `c.Arrange(cx + c.mx + ml, cy + c.my + mt, ...)`），于是全部重叠。
  `Control.Add` 的注释却写着「追加一个子节点（自动停靠）」，与实现不符——
  `Children = { ... }` 集合初始化器走的也是 `With`（`ControlChildren.Add`
  → `owner.With(c)`），只有显式 `Add` 落回手摆语义。
- 已落地绕法（本次）：gui-wechat 模板的代码建树统一改用 `Flex.Column()`
  （+ `AddClass`，`Class =` 会把方向类冲掉）+ CSS `grow` 类吃满剩余空间；
  框架自带的 `Panel.Column` 仍保留（`With` 路径照旧可用）。
- 方向（候选，需带 IDE 截图验证）：
  ① 框架侧让 `Add()` 对流式容器（`layout != 0`）也归一化停靠——风险是设计
     器自由画布：设计稿里的面板同样是 `layout=column/row`，子项靠 `mx/my`
     手摆，一旦被自动停靠，设计器画布会散架；这条路要先给设计器容器单独
     标记（例如 `style=layout` 且 `layout=column` 的语义区分）。
  ② 或让 `Panel.Column/Row` 直接成为 flex 容器（`StyleDisplay()` 返回 1，
     主轴方向取自 `layout`），但 `Control.zan` 里 `computedStyle.flexDir` 有
     多处直读，需要新增可覆写的「主轴方向」取值再逐处替换，影响 IDE 与全部
     模板。
  ③ 无论走哪条，都要把 `Control.Add` 的注释与实现对齐（现在文档承诺「自动
     停靠」，实现只对 `With` 兑现），否则每个写代码建树的人都会踩同一个坑。

### [ ] A543 读 Binding<T> 字段当 T 用：拿到的是常量槽，live 绑定的当前值读不出来
- 现象：聊天抽屉里点头像弹出的「资料名片」没有名字（昵称位置空白，性别符
  挨着头像），视频号名显示成「的分享」（= 空名 + "的分享"）。控件自己渲染
  出来的文字却是对的（抽屉标题「冰冰」正常）。
- 根因：`Label.Text` 是 `Binding<string>` 字段。赋模型字段左值
  （`pName.Text = s.name;`）会被编译器降级成 **live 绑定**（每帧回读模型
  字段），其余表达式降级成常量绑定（`stdlib/System/Binding.zan` 的 `Get()`：
  live 走 getter，const 走 `constVal`）。控件渲染走 `Text.Get()`，拿到的是
  live 的当前值；而把 `pName.Text` 直接当 `string` 读（隐式降解）拿到的是
  绑定对象的**常量槽**——live 绑定没有常量，恒为空串。于是「读出来传给
  回调」这一步把名字丢了。
- 已落地绕法（本次）：取值改走控件的取值接口 `pName.GetText()`（或
  `Str()`）；另外把会话字段先快照进局部量再赋给控件，避免 live 绑定在会话
  对象被换掉后指向悬空。
- 方向（候选）：编译器侧把「`Binding<T>` 字段读到 `T` 上下文」降解成
  `Get()` 调用（而不是常量槽），让读与渲染同源；需要确认这条隐式降解在
  类型检查（`checker_type_assignable` 允许 `Binding<T>` 赋给 `T`）与 irgen
  两侧的落点一致，并补一条 conformance。

### [ ] A544 子窗口（聊天记录窗/独立聊天窗/设置窗）在本会话无法像素验收
- 现象：`ZAN_UI_SCRIPT` 的 `dump pixels` 只抓**主 App** 的窗口；子窗口
  （`Gui.ChildWindow`：聊天记录窗、独立聊天窗、设置窗、通讯录管理窗）是
  各自的顶层窗口，抓不到。改用屏幕抓取（CopyFromScreen）在本会话拿到的是
  全黑桌面——没有可交互的桌面会话。
- 绕法（已落地）：把窗口**内容**（视图）塞进独立宿主 Form 再抓像素。设置窗
  （`WxSettingsView`）与通讯录管理窗（`WxContactManagerView`）用这招验过，
  像素正常（导航列、正文、底栏各归各位）。聊天记录窗与独立聊天窗把 root
  建在 ChildWindow 类内部，视图接口没暴露，暂用这招。
- 已定性（2026-10-23 19:0x，闭掉「待查」那半）：那 6 条「裁剪」命中**不是误报**，
  是真缺陷——**flex 收缩没有 min-content 下限**（CSS 的 `min-width:auto` 未实现），
  行一旦放不下，子项按比例缩：图标钮被切掉几像素，工具条项被压成 **0 宽**（按钮
  看不见也点不着）。复现：把客户端主窗口改窄（探针副本里 `Form.Create("微聊",
  520, 600)`，只改副本不动模板）后跑聊天页，`ZAN_GUI_LAYOUTLINT=1` 稳定命中 4 条：
  `Button[675,69 34x42]`/`[721,69 35x42]`（标题行图标钮，42→34/35 被切）、
  `Button[717,661 36x42]`/`[753,661 0x42]`（输入区工具条末项被压成 0 宽）。
  单独开聊天记录窗/独立聊天窗（宿主窗）各跑一遍都是 **0 命中**——子窗本身干净。
- 模板侧已修（gui-wechat）：皮肤层补全局工具类 `.shrink-0 { flex-shrink: 0; }`；
  标题行图标钮与各页工具条按钮挂 `shrink-0`；聊天页 `#ToolsHost` 改 `display:
  flex`（ToolStrip 是块级子项时宽度=宿主宽，窄窗口下项被压成 0 宽；做成 flex
  行后工具条是 flex 项，shrink-0 即可保持自然尺寸）。修后同一窄窗探针 **0 命中**，
  像素复验：标题行两个图标钮与工具条图标完整不再缺块（见 `_scratch/probe/
  narrow3.png`）；正常宽度十页重跑仍 0 命中、像素无变化。
- 框架侧缺口另记 A545（`base.css` 烤进驱动，模板改不动）。
- 前提：要有可交互的桌面会话（锁屏/远程会话会拿到黑屏），或让子窗口支持
  把 root 暴露给宿主驱动。

### [ ] A545 框架：flex 收缩没有 min-content 下限 + base.css 缺 `shrink-0` 工具类
- 现象一：`packages/Zan.Gui/src/Gui/Widget/Flex.zan` 的类注释承诺子项可用
  `shrink-0`（「溢出时不让位」），但全仓搜不到 `shrink-0` 的定义——
  `packages/Zan.Gui/src/Gui/skins/base.css` 里没有这条规则（`flex-shrink` 属性
  本身在 `StyleSheet.zan:1369` 是支持的，只是没有这个工具类）。base.css 是烤进
  驱动的（`src/runtime/main.c` 把皮肤包嵌进 exe），改它要重建 Gui 驱动。
- 现象二：`Control.zan` 的 flex 收缩段（`free < 0` 分支，约 3527-3564 行）按比例
  收缩子项，**没有 min-content 下限**（CSS 里 flex 项默认 `min-width:auto`，
  即 min-content，图标钮不该被缩到图标以下）。停靠子项更极端：`Control.zan`
  约 2890 行的 `FitSize(c.DockSize(availW, true), availW)` 把停靠子项裁到「剩余
  宽」，剩余宽不足时直接给 **0 宽**——0 宽按钮既看不见也点不着（WinForms 的
  ToolStrip 是溢出而不是压扁）。
- 候选修法（需专门一轮，带 IDE 截图验证）：① base.css 补 `.shrink-0 { flex-shrink:
  0; }`（与 Flex 注释对齐）+ 重建驱动；② flex 收缩加 min-content 下限（近似取子项
  自然尺寸的最小值——图标钮按图标尺寸、文本钮按文字宽度）；③ 停靠子项不再被
  `FitSize` 裁到 0 宽（不足时溢出或隐藏，而不是 0 宽）。
- 模板侧已用等价的全局规则绕开（gui-wechat 皮肤层 `.shrink-0`），修法见 A544。

### [ ] A546 框架：普通 HTML 里的 `<label>` 是元素，静态文字排不出来
- 现象：微聊模板的**登录窗**实机渲染缺字——标题「微 聊」与三个入口
  （注册账号/修改密码/忘记密码）全是 `<label>` 元素，**一个字都不画**；同文档里
  的 `<div>` 文字（头像里的「微」）正常。复现：把登录文档建进宿主窗再 dump 像素
  （`_scratch/probe_host.ps1 -Mode loginhtml`）。
- 根因：`Style.UaCss()` 的 UA 表只给「纯布局/排版 tag」声明 display，`label` 因为
  与真控件 `Label` 撞名被**故意排除**（注释写明「避免改变现有控件的 display 归类」）。
  于是普通 HTML 路径里的 `<label>` 建成 `Gui.Element`（Kind()="label"），没有 display
  归类 → 它的块流文字排不出来（元素高度 0、字不画）。设计器路径把 label 映射成
  `Label` 控件，两条路径对同一个标签的语义不一致。
- 候选修法（需专门一轮 + 重建 Gui 驱动）：① `Html.Build` 与 GenHtml 都把 `label`
  映射成 `Label` 控件（与设计器路径对齐，文字照常画，`data-on-click` 照常接）；
  ② UA 表补 `label { display: block; }`——但它会连带改 `Label` **控件**的 display
  归类（这正是 UA 注释顾虑的点），要先在 IDE 里验一遍。
- 前置：`packages/Zan.Gui/src/Gui/drivers/win-x64/*` 的驱动二进制当前是**另一会话的
  在途改动**，这轮不重建驱动；模板侧先用 `<div>` 顶上（登录窗已改，见提交）。

### [ ] A547 框架：ListView 行模板走 html 的真实门槛（编译期通道本就能按行建树）
- 更正（2026-10-23 实测）：先前记的「HTML 只能整树装载」只对**运行期**
  `app.LoadHtmlWith` 成立——它每次装载都把作用域 CSS 追加进 appCss 并整表
  重铺。**编译期 GenHtml 通道（`XxxHtml.Build()`）每次调用建一棵新树**，
  `Css` 是静态串、启动时并一次 → 行片段本来就能「按行建树」，不必等框架
  补 API。（`HtmlDoc` 不暴露 WDoc/WNode 那条只卡运行期通道。）
- 真正的门槛是 ListView 的行工厂契约：`RowOf<T>` 要的是代码建的 Control，
  框架规范也写明「运行期挂载件（行模板等）不携作用域类，样式放皮肤层」；
  声明式行只有 `<template data-for>` 一路，代价是丢掉 ListView 的
  虚拟化/滚动/选中语义。
- 本轮在这条路上撞到一个真缺陷并已修：`<template data-for>` 展开的行，
  **行根（@click 的宿主）没有作用域**——早期只给声明了绑定的控件登记，
  `ScopeOf(行根)` 返回 null → 行点击静默失灵（界面正常、点击毫无反应，
  服务端一个请求都没发）。已修 `ModelBinder.ScopeRow`：行根无条件登记一次
  （撤行时对称摘除）；conformance 用例 `gui_html_dynamic` 钉住（行根
  `ScopeOf` 命中行模型项）。
- 本轮取舍：行模板（ListView 行）与弹出面板**保持命令式**——外观由皮肤 CSS
  类驱动（样式单一来源成立），html 化要么丢 ListView 语义、要么把行样式拆到
  两个地方；窗口骨架（设置/通讯录管理/聊天记录/独立聊天）与十页已全部
  html+css。
