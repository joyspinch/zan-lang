# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

### [ ] A421 泛型类的字段布局不随实例特化——泛型类不能持有值类型字段

- 现象：`public class Box<T> { private T value; }`，`new Box<Pos>(p)`（Pos 是
  16 字节结构体）→ 构造时按擦除布局（8 字节指针槽）写 16 字节 → 堆损坏，
  读回即 0xC0000005。
- 根因：泛型类的**结构体布局**按声明处的字段类型算（类型参数 → 指针大小），
  发射具体实例（`Box<Pos>`）时没有按类型实参重算字段槽位大小。
- 关联：同族缺陷 A418（泛型方法内实例化泛型类）、A419（依赖型泛型基类）。
  三处都是"泛型实例化模型没有把类型实参从哪来理顺"。
- 方向：泛型类特化时按类型实参重算字段布局（值类型字段的槽位大小、偏移），
  与方法签名的 `subst_type_param_deep` 替换配套。
- 现状规避：`Zan.Game.Ecs` 的组件池不持有值类型字段——"缺失实体的零值"放进
  单元素 `List<T>` 盒子（列表的元素类型走的是已修的那条路径）。同类形态的
  用户代码会踩同一个坑，写泛型容器时先避开。

### [ ] A420 运行时没有 TTY 判定（isatty）——CLI 判不出"输出被重定向到文件"

- 现象：`Zan.Cli` 的颜色策略只能看环境变量（`ZAN_CLI_COLOR` / `NO_COLOR` /
  `TERM=dumb`），判不出 stdout 是不是终端——`prog > out.txt` 会把 ANSI 序列
  写进文件，日志被转义符污染。全仓搜不到 `isatty`（编译器内建表、运行时
  都没有）。
- 方向：给内建表加成员（`Console.IsOutputRedirected` / `Console.IsTerminal`），
  底层按平台判 `isatty(_fileno(stdout))`（Windows 侧是 `_isatty`）。
- 边界（这条决定了实现方式）：新增**运行时导出符号**要同步各平台的预编译
  目标（`toolchain/{macos,ios,wasm32}/**/*.o` 是仓库里的预编译产物，Windows
  上重建不了）——所以实现要么放在编译器侧（`irgen_builtins.c` 直接发
  `isatty` 调用），要么只用 libc 已有符号、不新增运行时导出。
- 现状：`Zan.Cli` 用环境变量策略兜底，并提供 `Cli.EnableColor(bool)` 让
  应用显式关色；文档里写明了这个限制。

### [ ] A418 泛型方法内实例化泛型类（`new Foo<T,V>`，V 是方法级类型参数）——变体不发射，规则静默失效

- 现象：`AbstractValidator<T>.Rule<V>(string property, FuncT<T,V> getter)` 里
  `new RuleBuilder<T,V>(...)`；`RuleBuilder<Signup,string>` 这类别实例化在
  AST 扫描期不可见（V 是**方法级**类型参数），`discover_generic_insts`
  （`src/compiler/irgen_generics.c`）不记录它 → 变体不发射 → 调用退化为
  擦除版 → 运行期取值器返回默认值（null/false/0），所有规则**静默不生效**
  （不报错、不抛异常，最难查的一类）。
- 证据：IR 里只有 `RuleBuilder_*`（擦除版），没有 `RuleBuilder$Signup$string`。
- 已排除的形态（都能用）：内联 `is string` 判定；泛型类的**实例方法**
  （`Ops<V>.IsEmpty` ✓）；用户在具体类里显式 `new` 的具体实例化
  （`new Plan<Signup>()` ✓）；泛型类持有依赖型泛型**字段**
  （`class Base<T> { Inner<T> inner; }` + `new Inner<T>()` ✓）。
  方法级类型实参的绑定推断本批已修（`infer_method_tp_bindings`），
  但那只解决"推得出 T"，不解决"变体不发射"。
- 方向：让发射期按需注册并发射**依赖方法级类型参数**的泛型类变体——
  要么在 checker 阶段把方法级类型实参具体化后再做实例化扫描，要么在
  `discover_generic_insts` 之外增加"按需实例化"通道。
- 现状规避：`Zan.Validation` 改用 `ValidationPlan<T>`（只用**类级**类型
  参数）+ 取值器闭包表达规则，API 形态见
  `packages/Zan.Validation/README.md`；这不是临时绕路——属性取值器本来就是
  FluentValidation 的形态——但"泛型方法内 `new 泛型类<类参数, 方法参数>`"
  本身仍是编译器缺口。

### [ ] A419 依赖型泛型基类（`class V<T> : Plan<T>`）特化后字段偏移错位——运行期空引用

- 现象：`SignupValidator : Validator<Signup>`，而 `Validator<T> : Plan<T>`。
  构造 `SignupValidator` 时崩：基类构造里 `this.properties` / `this.checks`
  看着是好的（计数 0），但紧接着在 `Plan<T>.Add` 里读 `this.properties`
  得到 null →
  `runtime error: null reference where an object is required (member access)`。
- 根因：泛型类的**基类类型**在特化时没有把类型实参替换进去（基类仍是
  `Plan<T>` 而不是 `Plan<Signup>`），派生类里看到的字段偏移与基类方法里
  算出的偏移不一致——同一份数据在两个地方被解释成两个东西。
- 已排除的形态（都能用）：具体实例化的基类（`class X : Base<Signup>` ✓，
  含继承来的方法体）；泛型类持有依赖型泛型**字段**（`Base<T>` 里的
  `Inner<T> inner`）✓。
- 方向：泛型类特化时同步替换基类类型实参（复用 `subst_type_param_deep`），
  并让基类字段偏移按替换后的类型重算。
- 现状规避：`Zan.Validation` 让用户直接继承 `ValidationPlan<T>`
  （`ValidationPlan<T>` 自身无基类），不套一层 `AbstractValidator<T>`。

### [x] A392 自举的运行期类型判定看不见接口与容器

- 现象（2026-10-20 修完 A384 后的边界）：自举后端现在能给对象判定类身份
  （对象头里的类标识槽，见
  `.agents/skills/zan-compiler-internals/references/selfhost-classid.md`），
  但还有两类值判不出来：
  - **接口**：接口不是基类链的一环，`IsSubclassName` 沿着基类链走，看不见
    「某类实现了某接口」→ `x is IFoo` 恒为假（vtable 时代也如此，不是回归）。
  - **容器**：`List<T>` / `Dictionary<K,V>` / `StringBuilder` 的类标识槽是 0
    → `o is List<int>` 恒为假（C 宿主能判）。
- ✅ 已修（2026-10-20）：
  - **接口**：自举的 parser 把接口表示成带 `MOD.Iface()` 的类声明
    （`parser.zan` 的 `ParseInterface`），于是「实现了某接口」就是基类链上的
    一环，`IsSubclassName` 沿链就能看见 → `x is IFoo` 由类标识判定。
  - **容器**：容器也是堆对象，也该有类标识。`irgen.zan` 新增
    `ContainerTidName` / `IsContainerType` / `ContainerCid` /
    `EmitContainerTids`：给 `List` / `Dict` / `StringBuilder` 各发一个
    `@__tid_*` 全局（用户类占了同名就让位），容器运行时的 `zalloc` 把这个
    tid 的地址写进类标识槽；`is` 判定与 switch 模式的类型测试在遇到容器名
    时走运行期类标识比较。
  - **口径**：宿主的运行期身份**不含元素类型**——`List<int>` 的实例
    `is List<string>` 也为真，`Dictionary` 与 `Dict` 是同一个类型 → 一类容器
    一个 tid 就够，不必按元素类型分别发射（原方向里的这条顾虑不成立）。
  - 用例 `tests/conformance/runtime_type_test.zan` 钉住（11 项判定，含空值
    与跨类型），差分门收录该用例。
- 边界：容器判定只看**类**，不看泛型实参（与宿主一致）；`object` 槽里的
  容器与源类互判为假；空容器判为假（宿主也是假）。

### [x] A385 自举协程帧布局与 C 宿主的 `zan.co.header` 不一致——自举产出的程序用不了发行版 mt 运行时

- 现象（2026-10-20 差分门实测）：自举产出的 `.ll` 链接发行版协程运行时
  （`toolchain/win-x64/zanrt_io_mt.o`，即 `rt_io.c -DZAN_CO_DRIVER` 的产物）会跑飞：
  该运行时的 `zan_co_ready` 把帧首部当 `zan_co_fhdr` 读写（`src/runtime/rt_io.c:4140`
  读 `h->sched`、`h->pending`），而自举的帧首 8 字节是**状态机 state**、次 8 字节是
  **done 标志**——运行时会去改这两个字段，状态机随即错乱。
- 根因：两套帧布局各写各的：
  - C 宿主：`zan.co.header`（`ASYNC_FRAME_*` 枚举：sched/pending/state/done/
    awaiter/…/hcount/self_step），运行时按这个布局接管帧；
  - 自举 IR 后端（`src/selfhost/irgen_async.zan`）：自定义布局
    `[0]=state [1]=done [2]=awaiter [3]=awaiter_step [4]=result [5]=own_resume
     [6]=cleanup [7]=hcount`，之后才是 this/参数/局部/子任务槽。
  两边都不认识对方的帧。
- ✅ 已修（2026-10-20）：帧仍是**扁平 i64 数组**（不改成宿主那种混合宽度的结构体），
  但头部**槽位编号**改成宿主的 `ASYNC_FRAME_*` 顺序：
  `0=sched 1=sched_step 2=state 3=done 4=awaiter 5=awaiter_step 6=result
   7=cleanup 8=hcount 9=own_resume`——运行时只碰 slot0（CO_* 位）与 slot1
  （pending step），这两槽**让给**运行时，自举从 slot2 起（原 slot5 的
  `own_resume` 随之变成 9，调用点读它的地方同步改）。
  - 帧释放改走 `__zan_co_frame_free`（宿主语义：先标 CO_DEAD 再 free），
    9 处直接 `free(frame)` 全部改掉；自举保留 weak 回退，发行版的强符号压过它。
  - action 帧的委托槽 8→10，头部 8→10 槽，分配 72→88 字节，清零范围同步。
  - 槽位重编号必须**单遍**替换：逐槽循环会级联（5→9 之后原本就是 9 的又变 10）。
- 验证：新增门 `selfhost_shipped_runtime`（`tests/run_selfhost_mt.cmake` +
  `tests/selfhost/mt/async_timer.zan`）——自举产物用 **MinGW 目标**编译 IR
  （`clang --target=x86_64-w64-windows-gnu`）+ `ld.lld -m i386pep` 链**发行版**
  mt 运行时（`toolchain/win-x64/*.o` + 捆绑 `mingw/lib`），输出与宿主逐字一致
  （`a=45` / `b=190` / `s=zane`）。发行版的 `zan_co_ready`/`zan_co_sched_init`
  是强符号，压过自举的 weak 回退 → 帧真的被发行版的多 worker 调度器驱动，
  定时器的 ready hook 也是运行时的。差分门 41 passed / 0 gaps / 0 failures，
  固定点 g2==g3（18,405,662 字节）。
- 残留：宿主的 `state`/`done` 是两个 i32（`done` 在偏移 20），自举的扁平数组把
  `done` 放在偏移 24——运行时的 join 路径（`rt_timer.c` 的 `JOIN_OFF_DONE`）
  **不适用于自举帧**；自举自己降级 `Task.WhenAll`/Join，不走那条路。

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

### [x] A389 自举的 `struct` 是堆对象（引用语义），与 C 宿主的按值语义分歧

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
- ✅ 已修（2026-10-20）：后端的**表示**不变（struct 仍是堆对象 + 指针），
  但把宿主按值拷贝的**每个边界**补齐成显式内容拷贝——`src/selfhost/irgen_struct.zan`
  （新增）：
  - `CopyStructValue` / `CopyStructInto`：分配新实例 + **逐字段**拷贝。字段的
    分裂照抄宿主——struct 字段**递归深拷贝**，class 字段**共享**（拷贝方
    retain 一次，源方那一份不动）。
  - `InitNestedFields`：创建实例时把 **struct 字段的嵌套实例**分配出来——
    嵌套 struct 字段是指针，不分配就是空指针解引用（自举此前在这里直接崩，
    不是算错）。
  - 拷贝点：赋值（`StoreTo` 与变量声明的初始化式）、传参（`CallArg`）、
    入容器（`List.Add/Insert/set`、`Dictionary.set`、数组下标、集合初始化、
    查询表达式的 select）、字段写入（`StoreField`）。
  - 用例 `tests/conformance/struct_value_copy.zan`（8 个形态）钉住，差分门
    收录该用例（40 passed / 0 gaps）。
- 边界：
  - **方法接收者不拷**：宿主里 `v.Bump()` 改的就是调用方的 `v`（不是 C#
    语义），自举沿用同一行为——先在宿主上测准，再决定要不要拷。
  - **泛型容器里的 struct 仍是别名**：泛型容器的元素类型是擦除的，判定
    "这是不是 struct"拿不到具体类型。
  - **数组元素不释放**：自举的数组没有析构器，拷进数组的副本永远不释放
    （与"数组里的 class 元素也不释放"同源，不是本次新引入的泄漏）。

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

### [x] A411 `conformance_clipboard_roundtrip` 在无交互窗口站点的会话里恒红

- 现象：用例输出 `clip-ok: 0`（`bad != 0`），其余 19 条全绿。
- 原登记的根因（**已推翻**）："**环境**——本会话没有交互窗口站点的剪贴板，
  实测 PowerShell `Set-Clipboard` 返回成功但 `Get-Clipboard` 读回空串"。**这条
  读数被误读了**：同一会话后来实测剪贴板完全可用（写入读回一致、SessionId=1、
  12 个交互窗口），8 条剪贴板用例全绿。"写成功、读回空"不是"没有剪贴板"的
  证据，而是**争用**的签名——剪贴板是全局独占资源，两个进程同时开会被拒，
  撞上争用时"写"抢到了、"读"没抢到。
- 真实根因：**库层没有退避重试**。`Clipboard` 的 `OpenClipboard` 失败就直接
  返回空串/失败（`GetText`/`SetText`/`GetFiles`/`SetFiles`/`Clear`/`Formats`
  六个点全是这样），而一次"写-读"往返要开关剪贴板十几次，撞上一次整条就红。
  另外 Windows 的独占是**按窗口句柄**判的：持有者挂真实窗口（资源管理器/
  IDE/输入法都是这样）时别人的 `OpenClipboard(NULL)` 会被拒；两边都传 NULL
  则互不排斥——这正是"看起来偶发"的来源。
- ✅ 已修（2026-10-20）：
  1. `packages/Zan.Desktop/src/System/Windows/Clipboard/Clipboard.zan`：
     新增 `WinOpenClipboardRetry()`（退避 10ms × 30 拍，合计 ~300ms），六个
     `OpenClipboard` 调用点全部改走它；新增公开的 `Clipboard.IsAvailable()`
     探针（能打开就说明有剪贴板），把"没有剪贴板"和"内容为空"分开。
  2. `tests/run_case.cmake`：新增 **skip 约定**——用例只输出一行
     `SKIP: <理由>` 且退出 0 时，harness 报 SKIPPED 而不是失败（环境缺资源
     不该算被测代码的错）。
  3. `tests/conformance/clipboard_roundtrip.zan`：先 `IsAvailable()` 判定，
     不具备就打印 skip 理由跳过（原来在无头机器上恒红）。
- 新增回归：`tests/conformance/clipboard_contention.zan`（+ `.out`）——起一个
  子进程用**真实窗口句柄**占住剪贴板，父进程在它占着的时候 SetText +
  GetText 必须成功；用例自带前提自证（裸 `OpenClipboard` 必须失败，否则
  说明没真占住，按 skip 走而不算通过）。**实测能抓住缺陷**：把重试次数临时
  降到 1（≈不重试）时用例输出 `clip-contention-ok: 0`（红），恢复 30 后
  5/5 绿。
- 验证：ctest `-R clipboard` **12/12 全绿**（conformance / determinism /
  leakcheck / arcguard × contention / roundtrip / uri_roundtrip）。
- 教训：**"读不到"要先分清是哪一种**——能力探针（能打开就是有）比"读一次
  看是不是空"可靠；把一次争用当成"环境缺失"登记成环境问题，会让真缺陷一直
  躺在"环境问题"里没人修。

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
### [ ] A416 自举的检查器不认识 `Task` / `Task<T>`——任务句柄的定型规则没镜像

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

### [x] A417 自举的 parser 缺宿主的 `desugar_async_task_method`——`ValueTask` 拼写与接口的 Task 形状没被改写

- 现象：宿主在**解析期**就把 async 方法的返回类型改写掉
  （`src/compiler/parser.c:3375` `desugar_async_task_method`）：
  `async Task<int>` → `int`、`async Task` → `void`，并给无方法体的
  （接口/抽象）声明补上 `async` 标记。所以检查器看到的永远是**结果类型**，
  `ValueTask` 这个名字也从来不需要进 binder（`binder.c` 只认 `Task`）。
  自举的 parser 没有这一步：`async Task<int> Foo()` 的返回类型原样进了
  检查器与后端。
- 方向：照宿主在 parser 里补这一步，并用 `is_task_return` 之类的标记把
  「这个方法原本是 Task 形状」传给后端——自举的 ngen 后端现在靠**类型名**
  认 Task 形状（`ngen_async.zan` 的 `IsTaskShapedRetNg`），改写返回类型
  后必须换成看标记，否则接口的 Task 形状识别会失效。
- ⚠ 本轮做了两半：
  1. **检查器侧**（`checker.zan` 的 `CheckMethod` 对 async + Task 形状返回
     类型改用元素类型当**方法体**的返回类型），够把 stdlib 里
     `async Task SleepAsync` 的假阳性（`not all code paths return a value`）
     消掉；
  2. **IR 后端侧**（`irgen_async.zan` 的 `AsyncElemTyOf`：await 的结果类型
     取元素类型，`Task` → `void`）——原来 await 的结果类型是**句柄**类型，
     整数与字符串靠 inttoptr/ptrtotr 的位往返**碰巧**对，double 结果就落到
     double 槽里成了指针，clang 直接拒（`'%t8' defined with type 'ptr' but
     expected 'double'`）。用例 `tests/conformance/async_task_result.zan`
     钉住（进了差分门）。
- ✅ 已修（2026-10-20）：自举的 parser 补上这一步的**可观察部分**，但不照抄
  宿主的改写方式：
  1. `ValueTask` → `Task`（只对 async 或无方法体的方法；普通方法照旧报
     "undefined type 'ValueTask'"，与宿主逐字一致）；
  2. 给"无方法体 + Task 形状返回类型"的方法补 `MOD.Async()` 标记（接口/抽象
     声明也是 async 方法，宿主就是这么判的：`body == NULL` 也算）。
  **不改写返回类型**（宿主改成元素类型并用 `is_task_return` 标记传给后端）：
  自举的检查器与 IR 后端本来就按"声明类型是 Task 形状"取元素类型
  （`TaskElemTy` / `AsyncElemTyOf`），改写返回类型会把调用点的类型也一起改，
  动静更大。保留类型名还顺带保住了 ngen——`IsTaskShapedRetNg` 靠类型名认
  形状，不用换成看标记。
  3. IR 后端补一处：`EmitVirtualCall`（接口/虚方法调用）原来不标 async 调用，
     await 会拒收这个操作数（"this operand is not lowered as a coroutine
     frame"）；现在与非虚的 `EmitInstanceCall` 一样标帧指针。
- 用例：`tests/conformance/async_valuetask.zan`（宿主与自举都输出 q=49）与
  `tests/conformance/async_interface_task.zan`（都输出 v=7），都进了差分门；
  反例 `tests/selfhost/reject/valuetask_not_async.zan` 钉住 "undefined type
  'ValueTask'"（两侧同一句）。
- 验证：差分门 39 passed / 0 gaps / 8 rejected / 0 failures；固定点门
  g2.ll == g3.ll（18300740 bytes）。ngen 路径只做了**冒烟**（能编出非空 .obj、
  没崩），它没有差分门覆盖，接口 + async 的组合在 ngen 侧仍未验证。
### [x] A418 自举的程序入口没初始化协程调度器——宿主在 `emit_main` 里调 `zan_co_sched_init`

- 现象：宿主在程序入口调 `zan_co_sched_init`（`src/compiler/irgen_emit.c:132`），
  自举的 `GenMainWrapper` 没有这一句。
- ⚠ 查清了一件容易搞错的事：自举产物里的 `zan_co_sched_init` 是**自举自己发的
  弱符号**（`irgen_async.zan` 的 `EmitAsyncPumpReady`），而自举运行时对象
  （`build/zanrt_shd_rt_io.obj` 等）**没有 `zan_co_*` 强符号**（`llvm-nm
  --defined-only` 查过），所以在自举侧这句调用今天只重置自举自己的调度器状态
  ——**结构对齐，不是行为修复**。压测佐证：同一个探针（`p_task6`）在有/无这行
  时输出逐字相同（各 200 次、0 挂起）。
- ✅ 已修（2026-10-20）：`irgen_async.zan` 的 `GenMainWrapper` 补上这一句，注释
  写清两种链接情形。差分门 33 passed / 1 gap / 4 rejected / 0 failures；固定点
  门 g2.ll == g3.ll（18084544 bytes）。
- 真正生效的时机：等 A385（帧布局对齐 `zan_co_fhdr`）后链接完整运行时，强符号
  胜出，这句才真的装上 timer ready hook（`src/runtime/rt_io.c:4242`），把延迟帧
  重新入队而不是在定时器线程上直接恢复（`src/runtime/rt_timer.c:729`）。
### [x] A419 自举没有任务句柄族的**降级**——差分门最后一个已知缺口

- 现象：`task_run_action_handle` 是差分门里唯一的已知缺口——自举认得句柄的
  **形状**（A416），但 `Task.Run` / `Task.Spawn` / `t.Wait()` / `t.Result` /
  `t.IsCompleted` / `Task.IsDone` 六个入口一律报 "not supported by the
  self-hosted compiler yet"，只能按已知缺口记账。
- ✅ 已修（2026-10-20）：`irgen_async.zan` 新增任务句柄族（`EmitTaskRunCall`
  / `EmitTaskMemberCall` / `EmitTaskIsDone` / `EmitTaskActionFrame` /
  `EmitTaskActionStep`），`irgen_expr.zan` 两处挂接——`GenCall`（静态
  `Task.Run` / `Task.Spawn` / `Task.IsDone`）与 `GenMember`（**不带括号**的
  `t.Result` / `t.IsCompleted`；`t.Wait()` 是调用，走另一条路）。
- 降级落在**自举自己的帧布局**上，**没有**去对齐 A385 的宿主帧头：句柄=帧
  指针，done 标志=槽 1，结果=槽 4。宿主是同一套协议、另一套字节
  （`src/compiler/irgen_expr_core.c` 的 `emit_task_member`：test/pump 循环 →
  读结果 → 回收）。差分门比的是 stdout 逐字，不是帧布局——所以"布局没对齐"
  从来不是"降级做不了"的理由。
- 三个坑（细节见 skill `zan-compiler-internals/references/selfhost-closure.md`
  §35）：
  1. **action 帧不能在完成时释放**：句柄在任务完成后还要能读 done 标志，
     提前释放就是读已释放内存。宿主靠运行时的活跃帧登记表回收，自举没有 →
     action 帧不释放（每个泄漏 72 字节；不是 ARC 对象，泄漏检查看不见）。
  2. **`Task.IsDone` 要给调度器一次机会**：自举没有宿主那样的工作线程池，
     任务只在主线程泵调度器时前进；读到"还没完"就泵一次再读，否则
     `while (Task.IsDone(h) == 0) {}` 这种忙等永远推不动。这是自举与宿主
     唯一的语义偏差。
  3. **句柄当 long 用**：`long h = Task.Run(Cube(5))` 走 ptrtoint、
     `Task.IsDone(h)` 走 inttoptr——与宿主对 i64 句柄的往返一致。
- 验证：`tests/conformance/task_run_action_handle.zan` 宿主与自举输出**逐字
  相同**（7 行全中）；差分门 35 passed / **0 gaps** / 4 rejected / 0 failures；
  固定点门 g2.ll == g3.ll（18266528 bytes）。
### [x] A420 自举的检查器把 lambda 的函数体按**外层方法**的返回类型查 `return`

- 现象：void 方法里写 `Func<int> f = delegate () { return 5; };`，宿主编译并
  运行（输出 5），自举却报 "cannot return a value from a void method"——
  **假阳性**：lambda 返回的是它自己的值，跟外层方法无关。
- 连带影响：这条假阳性会**挡掉**别的诊断对齐——`Task.Run(delegate () { return
  5; })` 在宿主侧报的是 Task.Run 专属的那条（"Task.Run delegate must be a
  parameterless void body (an Action); pass an async call for results"），
  自举侧却先被这条泛化诊断拦下，措辞对不上。
- ✅ 已修（2026-10-20）：`checker.zan` 加 `curRetKnown` 标志——方法体为 true，
  lambda 体为 false（lambda 的返回类型在自举侧**不知道**：检查器不从上下文
  给 lambda 定型），函数体里的 `return` 不查"void 不能返回值"也不查"必须
  返回值"。用例 `tests/conformance/lambda_value_return.zan`（进了差分门）。
- 教训：**"措辞不同"先查更早的层**——宿主报专属诊断、自举报泛化诊断时，先
  确认自举是不是在更早的检查里就拒了它。
### [x] A422 子窗口在**主帧内同步**渲染且无时间预算——子窗一多整帧被拖住，Windows 判未响应

- 现象：桌面应用"很多子窗口都未响应"。实测一个多窗口应用（oneplus）在 20 个
  子窗全开时帧耗时随子窗数**线性增长**；旧基线（写于旧产物、旧探针格式）显示
  20 窗 509ms/帧，当前产物用**同一探针**重测只有 87ms/帧（natural 与 force
  两种口径基本一致）。而单窗最贵的一次「构造＋首帧」也只有 174ms——**没有哪
  一步是秒级的**。所以"未响应"不是某个窗口卡住，而是**主帧被子窗渲染时间
  吃光**：帧越长，Windows 越早判未响应（判据是 UI 线程 5 秒不泵消息）。
- 根因：`packages/Zan.Gui/src/Gui/Core/ChildWindows.zan:215` 的 `PumpAll()` 在
  **主线程的主帧内同步渲染每一个脏子窗**（`w.Render()` → MeasureTree +
  RenderTree + `PresentFrame()`），既没有时间预算，也没有耗时日志。子窗数一多，
  单帧总时长 ≈ Σ(子窗渲染)；其中任一重子窗（大表格/长列表）就能把单帧顶过
  5 秒——进程其实一直在跑、只是没泵消息，于是表现为"未响应"。
- 探针（现成，勿重造）：`OnePlus.exe --test-child-layout-frames <case>
  [force]` 分 1/10/20 窗三档各测 60 帧；`--test-child-layout-perf <case>…`
  逐窗分开计时「构造」与「首帧渲染」。判定口径：**看 slow16（>16.7ms 的帧数）
  与最差帧，不要只看平均帧时**；不同档位必须**同产物同口径**重测，跨版本的
  基线文件不可比（过期基线比没有基线更危险：它带着数字，看起来可信）。
- 方向：① `PumpAll()` 给每帧的子窗渲染设**总预算**，用满就把剩下的留到下一帧，
  别让一个重子窗独占整帧；② 每个子窗渲染计时，超阈值打一行告警（哪个窗口、
  多久），让"未响应"自带证据而不是靠猜；③ 子窗内容重时走局部重绘，别每帧
  全量 Measure/Render。
- ✅ 已修（2026-10-20）：① `ChildWindows` 新增**单帧子窗渲染总预算**
  （`SetRenderBudgetMs`，默认 50ms，0=不限）+ **轮转游标**（预算用尽从下一个
  子窗接着画，推迟不饿死——实测 25 帧内每个子窗至少被画到 2 次）；② `ChildWindow.Render`
  拆成"计时包装 + 实际绘制"，单个子窗超阈值（`SetSlowRenderWarnMs`，默认 200ms）
  报 `UIWATCHDOG stall stage=child-window blocked=<ms>ms title=<标题>`，出口可由
  `SetSlowRenderSink` 接管。实测 20 个重子窗（每个绘制 30ms+）：单帧从 **1468ms
  降到 66ms**，17 个推迟。
- 边界（随修复一起记）：预算只约束"空闲泵"这条路径——子窗收到**自己的窗口
  事件**时会就地渲染一次（交互路径：用户点的那扇窗必须立刻出画），不在预算内；
  预算也切不开**单个**子窗自己的渲染，单窗就慢到秒级时靠耗时告警点名。
- 回归：`tests/gui/childwindow_budget_test.zan`（预算推迟/轮转公平/告警格式/
  预算 0 回退旧行为）+ `tests/conformance/gui_childwindow_budget.out`，
  ctest `conformance_gui_childwindow_budget` 通过；`conformance_gui_childwindow_shape`
  / `conformance_gui_form_dynamic` / `conformance_gui_html_runtime` /
  `conformance_gui_html_runtime_cjk` / `conformance_gui_upload` 一并复跑无回归。
### [x] A423 自举的类型诊断措辞/位置和宿主对不上——"unknown type" vs "undefined type"，位置停在 0:0

- 现象：`Undefined x = null;` 宿主报 `undefined type 'Undefined'` 并指向类型名
  （6:9），自举报 `unknown type 'Undefined'` 且**位置 0:0**（类型节点没记位
  置）。泛型实参个数更是各说各话：宿主
  `using the generic type 'List' requires 1 type arguments (got 2)`，自举是自造
  的 `Task takes at most one type argument`；基类同理（宿主
  `undefined base type 'X'`，自举 `unknown base type 'X'`）。
- 根因：自举的类型解析（`src/selfhost/checker.zan` 的 `CheckType`）是照着
  "能判出类型名就行"写的，没照宿主 `binder.c` 的 `binder_resolve_type` 三步
  （找基类型 → 查实参个数 → 逐个解析实参）走；`parser.zan` 的 `ParseType`
  也没把类型名的行列记进类型节点。
- ✅ 已修（2026-10-20）：
  1. `parser.zan`：`ParseType` / `ParseTypeNoArray` 记下类型名自己的行列，
     `ParseClass` 另记基类名的行列（`ast.zan` 的 `Node` 加
     `baseLine`/`baseCol`）；`new` 表达式与 `out T x` 的合成类型节点也补位置。
  2. `checker.zan`：`CheckType` 拆成 `TypeDiags`（照宿主三步走，只收集诊断
     不直接报），措辞换成宿主原文：`undefined type 'X'` /
     `using the generic type 'X' requires N type arguments (got M)` /
     `the non-generic type 'X' cannot be used with type arguments` /
     `undefined base type 'X'`。
  3. **签名类型报两遍**：宿主把方法签名（返回类型 + 参数类型）解析两遍
     （binder 一遍、checker 在方法作用域里再一遍），同一个坏签名会报两条诊断；
     自举照抄（`CheckSignatureType`）。
- 实参个数的三档口径（宿主 `binder.c`）：内建泛型 List/Span/ReadOnlySpan/Task
  收 1 个、Dict/Dictionary 收 2 个；用户泛型类/结构/接口按声明的类型参数个数
  （**可以是 0**，此时措辞是 "requires 0 type arguments"）；枚举与具名委托是
  "the non-generic type" 那一档。**Func/Action 不查个数**——宿主在作用域查找
  之前就把它们当内建委托类型（标准库里 `delegate TResult Func<TResult>();`
  的声明被遮住）；自举照做时踩过一次：一度把 `Func<int,int>` 判成
  "requires 1 type arguments (got 2)"，差分门里两个闭包用例当场炸掉。
- 验证：11 个探针逐字对齐（措辞 + 行列 + 报告条数）；差分门 37 passed /
  **0 gaps** / 7 rejected / 0 failures；固定点门 g2.ll == g3.ll（18292974
  bytes）。差分门的 reject 用例新增第三段（`name|||自举子串|||宿主子串`）：
  两侧必须用**同一句**诊断拒绝同一个程序，光"都拒绝"不算对齐。
- 遗留：宿主认识 `ReadOnlySpan`，自举还不认识（仍报 undefined type）——自举
  后端没有这个类型的布局，等 irgen/ngen 支持了再放开。
### [x] A424 Task 形状的槽位只收任务句柄——自举把裸值塞进 `Task<T>` 也不报

- 现象：`Task<double> t = Half(5.0);` 宿主报 `cannot convert 'double' to 'Task'
  in initializer: no implicit conversion`（位置指向类型名 8:9），**自举接受**
  ——自举会把一个 double 当成任务句柄写进槽位。
- 根因：自举的检查器（`checker.zan`）只镜像了宿主 `checker.c` 的
  `checker_check_assignable` 里 Task 相关的**一半**——"无结果句柄塞进
  `Task<T>`"那条做了，"Task 槽位只收句柄"这条更基本的没做。
- ✅ 已修（2026-10-20）：`CheckTaskOnlyHandle`——目标形状是 Task 而值的类型
  不是 Task 时，报宿主那句诊断（目标拼 **'Task'，不带类型实参**；null 放行）。
  覆盖变量声明的初始化式与 return 语句两处；顺带把这两处的诊断位置从 0:0
  改成**类型的行列**（宿主指类型名，而自举的变量声明节点本来不记位置）。
- 宿主口径（六个探针实测）：
  - `Task<double> t = Half(5.0)` → 拒（'double' to 'Task' in initializer）；
  - `Task t = 5` → 拒（'int' to 'Task' in initializer）；
  - `Task<int> t = null` → **接受**（null 句柄还是句柄）；
  - `Task<int> t = plain`（plain 是无结果句柄）→ 拒**另一条**（'Task' to
    'Task<int>' ... carries no result），位置同样是类型名；
  - 非 async 的 `Task<int> Bad() { return 5; }` → 拒（in return statement）；
  - `Take(5)`（把裸值传给 Task 形参）→ 宿主**接受**（它不对实参查这条规则），
    自举也接受 ✓。
- 教训：镜像一条规则时把它**周围的规则**列一遍——缺口往往就在旁边那条。
- 验证：六个探针逐字对齐（措辞 + 行列 + 条数）；差分门 39 passed / 0 gaps /
  10 rejected / 0 failures；固定点门 g2.ll == g3.ll（18308944 bytes）。新增两个
  reject 用例（task_slot_raw_value / task_return_raw_value），两侧同一句。
- 插曲：这一轮固定点门第一次跑挂在「gen0 → gen1 failed (rc=Access violation)」——
  原因是**并行的会话在 13:36:57 重建了 `build\zanc.exe`**（对方在改
  `src/compiler/irgen.c`），门禁正好跑到被替换掉的二进制上。单独重跑即绿。
  教训照旧：门禁期间不要有别的构建（AGENTS.md 规则 8）。
### [x] A425 ngen 路径**不查类型**——用了未定义类型的程序也照发目标码

- 现象：`Undefined x = null;`（不依赖标准库）交给 ngen（`zanc out.obj prog.zan`）
  → **rc=0，还写出了 66KB 的 .obj**。宿主在编译期就拒（"undefined type
  'Undefined'"），IR 后端（irgen）也拒（自举的检查器在 IR 路径上跑），只有
  ngen 路径不查——`main.zan` 的 ngen 分支在创建 Checker **之前**就 return 了。
- 根因：自举的 ngen 分支跳过了整个检查阶段，而宿主是"先查类型再落后端"。
- ✅ 已修（2026-10-20）：`main.zan` 的 ngen 分支先跑一遍共享的 Checker
  （和 IR 路径同一个），有错就报诊断、拒绝、**不写目标码**。现在 ngen 与
  宿主/IR 路径对同一个程序的判断一致。
- 顺带补了 ngen 的**最小回归门**（ngen 没有差分门——宿主没有 ngen 后端可比，
  但缺的是*参照实现*，类型检查不需要参照实现）：`tests/run_selfhost_ngen.cmake`
  + ctest `selfhost_ngen_smoke`，钉三条：
  1. 能降级的程序 → 产出非空目标码（带 64 字节下限，空文件不算有产物）；
  2. 后端不认识的成员 → 拒绝且**不写**目标码（fail closed）；
  3. 有类型错误的程序 → 拒绝且不写目标码，诊断是 "undefined type"。
- 验证：新门 0 failures；差分门 39 passed / 0 gaps / 10 rejected / 0 failures；
  固定点门 g2.ll == g3.ll（18311019 bytes）。
- 遗留：ngen 目前**编不了依赖标准库的程序**（标准库里的 Vector128 extern
  返回 ABI 不支持），所以这条门只覆盖不依赖标准库的形状；接口 + async 的
  组合在 ngen 侧仍只能冒烟（能编出 .obj），没有行为对拍。

### [x] A426 自举后端（`src/selfhost/`）尚未核对 C 宿主本轮修掉的 4 处缺陷

- 背景：2026-10-22 落地 `Zan.Game.Physics2D` 时，在 **C 宿主编译器**
  （`src/compiler/*`）修掉 4 个"静默失效"缺陷（提交 `b6dbf4a9`）：
  1. 运算符重载不参与类型推导（`infer_expr_type` / `AST_BINARY`）；
  2. 运算符调用的结构体实参按指针传入；
  3. 跨编译单元的 struct 字段布局降级成 `i8*`（`map_type` /
     `map_tuple_struct` 的条件写反）；
  4. `register_struct_type` 的条目指针在递归注册后失效（realloc 搬家）。
- ✅ 已核对（2026-10-20，探针 `_scratch/sd/p_opover.zan` + 语料扫描）：
  - 第 1 条（运算符重载参与类型推导）：自举侧**等价**——自举的运算符分派
    发生在代码生成期（`TryOpOverload`），checker 的 `ArgTypeNode` 对
    "左右操作数类型不同 kinds" 返回左操作数类型，够用。探针实测
    `a * 3 + b`（重载结果再参与下一次重载）宿主与自举逐字一致。
  - 第 2 条（运算符的 struct 实参按指针传）：自举侧**天然成立**——自举的
    struct 一律是 `%Name*`（堆对象指针），传参就是传指针。探针实测
    `new Pt() + p` 逐字一致。
  - 第 3、4 条：**在自举后端没有对应面**（结构上不可能发生）。自举的
    struct/class 一律映射成 `%Name*`，没有"结构体值 + 字段偏移"这套布局；
    `EmitStruct` 只发一个带字段类型的 named type，字段槽偏移在运行期算。
    宿主那两类缺陷（字段类型降级成 `i8*` → 布局错位；注册表条目 realloc
    失效）在自举这边没有可退化的路径。
- ⚠ 核对时挖出**另一批**真缺口：自举的重载分派**按名字取**，不按实参类型
  ——见 A428（本轮已修运算符那一半，通用调用那一半仍开着）。

### [ ] A428 自举的重载分派**按名字取**，不按实参类型——多个同名重载会选错方法

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
  - `indexer_overload.zan` / `cs_b12_conv_operator.zan`：索引器重载与转换
    运算符重载都落到"按名字取第一个"→ 非法 IR。**未修**。
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
- 剩余（未修）：索引器重载（`op_index`）、转换运算符（`op_implicit` /
  `op_explicit`）、泛型实参形状那一族（属 A421）。
