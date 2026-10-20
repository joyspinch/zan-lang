---
name: zan-compiler-internals
description: zanc 编译器内部（parser/checker/irgen/runtime/ABI）设计契约与底层不变量规范。修改 src/compiler/*、src/runtime/*、交叉目标或 conformance 测试时使用。
---

# zanc 编译器内部设计契约与底层不变量

定位：`src/compiler/` 与 `src/runtime/` 开发与维护的架构规范与底层契约。
详细实现细节见各专题：
- 前端解析与语义绑定：[frontend-binder.md](references/frontend-binder.md)
- LLVM 指令生成与向量优化：[irgen-llvm.md](references/irgen-llvm.md)
- 运行时数据布局与 ARC 模型：[runtime-abi.md](references/runtime-abi.md)
- 自举闭环不变量（src/selfhost 改动必读）：[selfhost-closure.md](references/selfhost-closure.md)
- 自举的托管字符串（长度语义、来源清单）：[selfhost-string.md](references/selfhost-string.md)
- 自举的运行期类标识（对象头、类型判定）：[selfhost-classid.md](references/selfhost-classid.md)

---

## 一、前端解析、绑定与语义检查（Parser / Binder / Checker）

1. **扩展方法接收者兼容性双向同步**：
   - 动作：扩展方法的接收者兼容性判定必须同时作用于候选筛选与重载评分。
   - 理由：非泛型接口扩展在具体实现类与派生接口变量上应均可调用；精确类型匹配优于接口兼容，接口兼容优于裸类型参数。
   - 边界：涉及泛型接口实参匹配时，必须保留完整的泛型类型实例化匹配路径，禁止单纯通过 implements 符号遍历放行。
2. **语法树节点类型语义隔离**：
   - 动作：类型位置的节点为 `AST_TYPE_REF`，不可与表达式标识符 `AST_IDENTIFIER` 混淆。
   - 理由：混淆会导致符号绑定阶段将类型引用误判为局部变量或成员，引发语义解析穿透。
   - 边界：语法解析器（`parser.c`）中前瞻判定（`looks_like_var_decl`）需严格区分类型指示符与表达式操作符。
3. **调用形状与静态性强约束**：
   - 动作：发射调用指令时，接收者槽位必须由目标方法的静态性确定（实例方法绑定 `this`，静态方法无接收者槽）。
   - 理由：防止属性或方法静态性判定失配导致 IRGen 阶段解引用空指针。
4. **控制流可空守卫深度遍历**：
   - 动作：可空性静态分析（Nullability Check）必须穿透 `lock` 等复合控制流语句，同步扫描内部守卫与返回路径。
   - 理由：避免对受锁保护的合法空值判空分支产生假阳性诊断。
5. **抽象方法的三条检查器规则**：
   - 动作：抽象类不可实例化（`new` 报 "cannot instantiate abstract class 'X'"）；
     具体类必须实现继承来的抽象方法（报 "non-abstract class 'X' does not
     implement abstract method 'Y' inherited from 'Z'"）；抽象方法不得同时是
     static / async；含抽象方法的类必须声明为 abstract。
   - 理由：抽象方法没有方法体，调用点在编译期不拦截的话，运行期只能撞空
     指针——而且是"某个分支才崩"的那种。检查器把错误提前到编译期，是抽象
     方法唯一不依赖运行期兜底的保证。
   - 边界：虚表槽位记账必须把 `MOD_ABSTRACT` 与 `MOD_VIRTUAL` 同等对待
     （`irgen.c` 的 `class_has_virtual_methods` / `count_virtual_methods` /
     `get_virtual_method_index`、`irgen_arc.c` 的 `collect_vslot_decls`、
     `irgen_generics.c` 的 `emit_dispatch_call`）——漏一处，派生类的 override
     会落到别的槽位上，虚分发静默走错方法。
6. **限定名必须按命名空间解析，不得退化成"取最后一段猜"**：
   - 动作：`Ns.Type.Member(...)` 这类限定名路径，只能解析到该命名空间（或其
     子命名空间）里的类型；路径首段是**类**时（`Widget.Tooltip.Flush`）后面
     才是嵌套成员。解析不到就报 "unresolved call"，不许退化成"拿最后一段去
     作用域里找同名类"。
   - 理由：退化之后，限定名写错（`System.Text.Csv.Parse`，Csv 实际住在
     `System.Csv`）会静默绑到 `using` 引进的同名类上——不报错、返回类型也
     对不上（`CsvTable` 当 `List<List<string>>` 用），运行期直接踩内存
     0xC0000005。症状落在业务代码里，根因却在编译器，定位成本极高。
   - 边界：判据是"路径前缀与类型所在命名空间必须互为祖先"（按命名空间段边界
     比），外加"路径首段是类 → 嵌套成员"这条例外——少了它
     `Widget.Tooltip.Flush` 这类嵌套访问会被误杀。回归用例：
     `tests/diag/qualified_name_wrong_namespace.zan`（必须编译失败）+
     `tests/conformance/qualified_name_namespace.zan`（合法限定名仍能解析）。
7. **重载分解只有一处规则：checker 与 irgen 必须共用**：
   - 动作：重载候选的打分与取舍规则抽到**共享**位置
     （`src/compiler/binder.c` 的 `zan_null_arg_score`），checker
     （`checker_find_method_typed`）与 irgen（`irgen_expr_core.c` 的
     `concrete_arg_score`）都调它；null 字面量只能绑到引用类型/可空类型
     形参，值类型形参判为不兼容。
   - 理由：两侧各自分解一次、规则还不一致时，**静默走错重载**：
     `Arg.Text(null)`（Text 有 int/long/double/bool/string 五个重载）以前会
     走到 `Text(int)`——checker 侧按 null 的静态类型（object）打分、irgen 侧
     对 null 推断不出类型一律给 0 分，于是"第一个重载"（值类型）赢了，null
     被当成 0 传下去。调用点看不出任何异常，断言却永远对不上。
   - 边界：null 的静态类型是 `object`（`checker.c` 的 `AST_NULL_LITERAL`
     分支），所以判据是"**形参能不能接住 null**"，不是实参的静态类型；
     值类型形参接 null 必须在编译期报错（`tests/diag/null_to_value_type.zan`），
     不能等到运行期变成 0。`object` / `T?` 形参接得住 null。
     实例：`Arg.Text(null)` 与 `Overloads.Of(null)` 两条曾同时走错
     （`tests/conformance/null_overload_resolution.zan` 钉住正解）。

---

## 二、LLVM 代码生成与指令下沉（IRGen）

1. **LLVM Builder 基本块定位生命周期**：
   - 动作：调用 `LLVMBuildGlobalStringPtr` 等指令构建函数前，`builder` 必须通过 `LLVMPositionBuilderAtEnd` 绑定至有效基本块。
   - 理由：向未定位或已脱钩的 builder 插入指令会导致底层 SIGSEGV 崩溃；合成辅助函数必须在自身已定位的 entry block 内发射。
2. **LLVM 选择指令（Select）与求值死臂**：
   - 动作：带内存分配或副作用的操作严禁使用 `LLVMBuildSelect`，必须使用条件分支（`LLVMBuildCondBr`）和 `LLVMBuildPhi` 合流。
   - 理由：`LLVMBuildSelect` 的两个分支在执行时均会计算，放入分配操作会导致死臂内存泄漏。
3. **只读全局变量判定标志**：
   - 动作：判定全局变量是否只读必须使用 `LLVMIsGlobalConstant`，禁止使用 `LLVMIsConstant`。
   - 理由：`LLVMIsConstant` 对所有全局变量恒为真，误用会导致可变缓冲区被放入只读段（.rdata）并在写入时崩溃。
4. **内建硬件原语整型重载判断**：
   - 动作：降低多重载位操作与硬件原语（如 `PopCount`, `LeadingZeroCount`, `RotateLeft` 等）时，必须通过 `infer_expr_type` 获取语义类型。
   - 理由：Zan 语法树中整数字面量发射默认宽度为 64 位（`i64`），仅凭 LLVM 类型宽度判定会导致 32 位整型字面量被误判为 64 位。
5. **向量与 SIMD 内存对齐及步长缩放**：
   - 动作：对类型化数组执行向量加载/存储时，源码 `offset` 下标必须乘以元素宽度（`offset * elem_size`）后再进行 GEP 字节寻址；非对齐内存操作必须标记 `LLVMSetAlignment(ld, 1)`。
   - 理由：防止下标步长错位及非 16 字节对齐缓冲区触发硬件通用保护错误（#GP）。
6. **目标架构指令集特性基线（`tm_features`）**：
   - 动作：底层指令特性开关必须严格符合目标平台的 ABI 基线（如 Android x86_64 仅开启 SSE4.2，不开启 AVX2）。
   - 理由：防止高级指令在不支持的目标硬件运行时抛出 SIGILL 非法指令异常。
7. **子协程 resume 入口在调用点必须能取到**：
   - 动作：任何在调用点需要子协程 resume 入口的下沉路径（`await` 挂起、
     `Task.Spawn`/`Task.Run` 脱钩、语句级 fire-and-forget）都必须能从协程帧头
     `ASYNC_FRAME_SELF_STEP` 槽加载到本协程的 resume 入口；只有当被调值
     **确实是指令**时才谈按 `<callee>$resume` 查具名函数（见本节第 8 条：
     async 调用的返回值是帧指针，永远不是调用指令，按名字查这条路不成立）。
   - 理由：间接调用（接口/虚方法/委托分发）按名字必然查不到 → `Task.Spawn`
     静默返回 0 句柄、协程永不
     入调度器；而 `__zan_co_isdone` 对不在活协程表里的帧返回 1，于是
     `Task.IsDone` 报"已完成"，业务代码永远等不到副作用，症状与业务逻辑 bug
     无法区分。await 路径早有 SELF_STEP 回退（`irgen_expr.c` 间接 await 分支），
     detach 路径漏掉即是此坑（`Task.Spawn` 的 `emit_detach_async_call`）。
   - 边界：回退只对协程帧指针有效；ramp 必然在任何调度之前把 resume 写进
     SELF_STEP，故加载安全。单实现者会被去虚拟化成直接调用从而掩盖该缺陷，
     回归用例必须声明 ≥2 个实现者以强制走间接分发。
8. **LLVM C API 的取值函数必须先判 kind 再用**：
   - 动作：`LLVMGetCalledValue` 只对 CallInst 有效，取被调值前必须先
     `LLVMIsACallInst(sub)`；取操作数前必须确认索引落在
     `LLVMGetNumOperands` 之内；聚合常量取元素用 `LLVMGetAggregateElement`，
     不要用 `LLVMGetOperand`。
   - 理由：这些 C API **不做任何边界检查**，按错布局解读就是越界读——读到
     相邻堆内存，恰好可读时不崩、不可读时 0xC0000005，症状是"间歇性崩溃"，
     定位成本极高。实例：async 调用的返回值是协程帧指针（phi/load），
     从来不是 CallInst；按 CallInst 解读后紧跟的 `LLVMGetValueName2` 就去
     解引用野指针。
   - 边界：需要"从值反推被调符号"的路径（查 `<callee>$resume`）对 async
     调用天然不成立——帧指针不是调用指令；resume 入口只能从帧头
     `ASYNC_FRAME_SELF_STEP` 槽取（见本节第 7 条）。
9. **间歇性缺陷的复现、定位与门禁纪律**：
   - 动作：判定"偶发崩溃已修复"必须**重复运行**（≥8 次）而不是单次通过；
     调试器下不复现通常说明堆布局参与了触发条件——改用 Debug 构建 +
     `cdb -hd`（调试堆）复现，再取栈；门禁要重复编译同一输入 N 次才拦得住回归。
   - 理由：野指针类缺陷只在"被解引用的那块内存恰好可读"时发作，单次绿是
     假绿；Release 构建的符号被内联打散，栈上没有可读帧名，只能靠 Debug
     构建才拿得到行号。
   - 边界：重复次数按实际发作率取（发作率 40% 时 8 次的漏检率 < 1%）；
     cmake 脚本里对崩溃子进程拿到的 `RESULT_VARIABLE` 是描述串
     （"Access violation"）而不是负数退出码，按形状判定才分得清"崩了"与
     "编译报错"。
10. **结构体按值语义的两种形态必须归一**：
   - 动作：结构体实参/元素在调用点与集合槽位处，若发射出的值是**指针**
     （`new T(...)` 构造临时对象即如此：构造器写入 sret 槽，表达式值是该槽地址），
     而消费方按**值**接收（形参 LLVM 类型是结构体类型、集合槽是结构体槽），
     必须先按值取出（`LLVMBuildLoad2`）再传/再存。已收口在
     `irgen_arc.c` 的 `coerce_struct_value` / `coerce_args_to_params`。
   - 理由：变量实参走的是「load 出结构体值」，构造临时对象走的是「槽地址」，
     两种形态不一致 → 调用点传出指针（LLVM 校验直接失败：
     `Call parameter type does not match function signature`），
     或集合槽里存进指针、读出来是全零（`List<Vector3>.Add` 后读回全零）。
     根因在 irgen，不在用户代码——不能要求用户绕开 `new T(...)`。
   - 边界：`ref` / `out` 形参的 LLVM 类型是指针，**不**走这条路径；
     类（引用类型）同样不走。
11. **导出的 IR 只在编译器自己的目标特性下合法**：
   - 动作：`zanc --emit-ir` 落出的 .ll 是给外部工具（llc/opt/lli）消费的，必须用
     与编译器一致的目标特性去消费：编译器的 x86-64 档是 CPU `x86-64` + 特性
     `+sse3,+ssse3,+sse4.1,+sse4.2,+crc32,+aes,+avx,+avx2,+fma,+bmi`
     （`src/compiler/irgen_emit.c` 的 `zan_bind_target_layout` 建 TargetMachine 时
     定的），llc 侧对应 `llc -mattr=+avx2`（`-mcpu=haswell` 等价）。
   - 理由：编译器自己不受影响（TargetMachine 带着 AVX2），所以"zanc 能编过"**不能**
     证明"导出的 IR 能编过"——两者用的目标档不同。llc 的默认 generic CPU 会把
     256 位算子拆成 2×128 位，而函数里同时存在 `llvm.x86.avx2.pmovmskb` 这类
     AVX2 内建函数时 256 位类型被判为合法、`<N x i1>` 却留在图里，
     `SplitVectorOperand` 没有兜底 → fatal error
     "Do not know how to split this operator's operand!"。
   - 边界：函数级 `"target-cpu"`/`"target-features"` 属性在 LLVM 20 的 llc 上
     **不参与**合法化，别指望写进 IR 就能免掉 `-mattr`；真要 IR 自洽，得让
     Vector256 的降级路径不产生 256 位向量值（见 TASKS.md A388）。
12. **抽象方法要占虚表槽位，并配一个致命桩**：
   - 动作：抽象方法必须与 virtual 方法一样参与虚表槽位记账（见第一节第 5 条
     的四处），并为无实现体的抽象方法发射一个调用
     `zan_rt_fatal("abstract", ...)` 后 `LLVMBuildUnreachable` 的桩函数；
     `declare_user_methods` 里不能因为"没有方法体"就把抽象方法整个跳过
     （跳过 = 虚表里那个槽是空的，虚分发跳到野地址）。
   - 理由：漏实现的抽象方法必须**响亮地**失败，而不是悄悄调到相邻方法或
     读空指针。
   - 边界：抽象方法强制 `is_async = false`（async 抽象方法没有可生成的
     协程帧）。
13. **"调用解析不出来"的兜底必须按声明返回类型给默认值**：
   - 动作：`emit_unresolved_call_result` 的兜底值要按被调方法的声明返回类型
     给（指针/结构体 → null，浮点 → 0.0，整数 → 0），不能一律给 `i32 0`。
   - 理由：兜底值要参与后续 IR 的类型校验，固定 `i32 0` 会让
     `icmp eq i32 0, ptr %x` 这类比较直接触发 LLVM 校验失败
     ("Both operands to ICmp instruction are not of the same type")，
     编译期报错的位置离病因很远。
   - 边界：兜底只保证**类型**正确，不保证**语义**——被丢弃的调用在 IR 里
     根本不存在，运行期会静默取到默认值。排查"某个值莫名其妙是 0/null"
     时，先看 IR 里这个 call 还在不在。
14. **泛型基类的字段类型要按实例实参替换**：
   - 动作：合并基类字段/属性到派生类成员表时，若基类是具体化实例
     （`base_type->type_arg_count > 0`）且字段类型是类型参数，必须按实参
     替换后再复制符号（`binder.c` 的 `binder_subst_type_param`）。
   - 理由：不替换 → 继承来的方法在派生类上被发射成 `ret ptr null` 的空桩，
     方法体整个消失（症状是"调用返回 null 且不报错"）。
   - 边界：这条只管**字段类型**；泛型类作**基类**时的基类类型替换是另一处
     缺口（见下一条）。
15. **泛型的两处已知缺口（A418 / A419）**：
   - 动作：写"泛型方法里按**方法级**类型参数实例化泛型类"（`new Foo<T,V>`，
     V 是方法级类型参数）或"依赖型泛型基类"（`class V<T> : Plan<T>`）之前，
     先知道这两条当前**不支持**：前者 `discover_generic_insts` 记不到 →
     变体不发射 → 退化成擦除版、取值全是默认值（**静默失效**）；后者特化后
     基类字段偏移错位 → 运行期空引用。
   - 理由：这两类都是静默失效（不报错），比编译报错难查得多，且会诱使人
     把"编译器不支持"误判成"自己代码写错"。
   - 边界：可用的替代形态：类型实参留在**类级**（`class Base<T> { Inner<T>
     inner; }` 正常）、具体实例化的基类（`class X : Base<Signup>` 正常）、
     用 `FuncT<T,V>` 形参代替按 V 实例化。
16. **泛型类的 `this.field` 必须按当前实例替换类型实参**：
   - 动作：`member_access_field_type`（`src/compiler/irgen_expr_core.c`）在
     `this.x` / `base.x` 分支上要按 `g->cur_inst` 调
     `subst_type_param_deep`，不能直接返回字段的**声明**类型。
   - 理由：声明类型停在类型参数上时，元素类型判成"擦除的指针"——结构体
     元素写进去的是临时对象的地址，读回来就是野指针（实测 0xC0000005）。
     指针大小的元素（引用类型）恰好蒙对，所以这个缺陷能潜伏很久。
   - 边界：**字段布局**是另一处（泛型类的结构体布局不随实例特化，值类型
     字段仍会算错槽位大小，见 TASKS.md A421）——本条只管**表达式类型**，
     两处要一起修才算彻底。
17. **泛型类的字段布局不随实例特化（已知缺口，A421）**：
   - 动作：写"泛型类持有值类型字段"（`class Box<T> { T value; }`）之前要知道
     当前不支持：布局按声明处的字段类型算（类型参数 → 指针大小），具体实例
     化时没有重算槽位大小 → 写超 → 堆损坏。
   - 理由：症状是"构造时崩"或"读回是垃圾"，与用户的类型定义毫无关系，
     定位成本极高。
   - 边界：替代形态——把值类型放进 `List<T>`（元素类型走已替换的路径），
     或让泛型类只持有引用类型字段。
18. **运算符重载必须参与类型推导**：
   - 动作：`infer_expr_type`（`src/compiler/irgen_expr_core.c`）的 `AST_BINARY`
     分支，在走数值运算前先查有没有对应的 `op_*` 重载（`op_add` / `op_sub` /
     `op_mul` / `op_div` / `op_mod` / `op_eq` / `op_neq` / `op_lt` / `op_gt` /
     `op_le` / `op_ge`），查到就返回该方法的**返回类型**。
   - 理由：推导返回 NULL → 重载分派被跳过 → 代码生成退化成**整数加法**，
     LLVM 报 "Integer arithmetic operators only work with integral types"。
     症状离病因很远：报的是加法不合法，真实问题是重载根本没被调用。
   - 边界：运算符结果**再参与下一次运算**时最容易踩（`a * b + a`：第一
     次的结果类型是 NULL，第二次运算就彻底失去类型信息）——推导必须能
     链式传递。
19. **运算符调用的实参形态要按期望类型归一化**：
   - 动作：运算符调用路径（`src/compiler/irgen_expr.c`）拿接收者与第二个
     实参时，若参数类型是 struct 而实参是指针槽位（`new 结构体(...)` 的
     返回值），补一次 `LLVMBuildLoad2`；常规调用走 `coerce_args_to_params`，
     运算符路径**不能假设**已经走过。
   - 理由：否则报 "Call parameter type does not match function signature"，
     而用户写的就是最普通的 `a + b`。
   - 边界：标量与结构体的混合重载**只能标量在右**（`v * 2.0` 合法，
     `2.0 * v` 编译器直接报 "cannot apply operator to 'double' and 'V'"）——
     这是语言限制，不是缺陷，不要靠改编译器放宽。
20. **跨编译单元的 struct 字段布局必须按需注册后取值**：
   - 动作：`map_type`（`src/compiler/irgen.c`）的 `TYPE_STRUCT` / `TYPE_CLASS`
     分支，在该类型尚未注册时按「有 `decl` 则先 `register_struct_type`」
     再取字段类型——class 取**指针**，struct 取**值**；`map_tuple_struct`
     的兜底分支同规则（原来写成 `if (!type->sym->decl)`，条件写反：有
     声明、成员已绑定、能算布局时反而不注册）。
   - 理由：漏注册 → 字段类型退化成 `i8*` → 类的字段布局整体错位 →
     **静默内存损坏**（实测 `v 3,0.25` 把结构体的 y 读成了相邻的 double，
     编译与运行都无报错）。这类缺陷比崩溃更难查：值是错的，不是没有。
   - 边界：判定器是**跨文件**用例——结构体定义文件必须排在引用它的用户文件
     **之后**编译，才命中旧的文件序缺陷（见
     `tests/conformance/struct_field_layout/`）。
21. **结构体类型注册表的条目指针在递归注册后失效**：
   - 动作：`register_struct_type` 登记完自己就立刻记下
     `int my_index = g->struct_type_count - 1;`，递归注册**之后**再用
     `&g->struct_types[my_index]` 取条目取字段数据。
   - 理由：递归注册会 realloc `g->struct_types`（数组搬家），事后用
     `&g->struct_types[g->struct_type_count - 1]` 取到的是**别的类型**的
     条目 → 字段数据互相覆盖 → 类型混淆 → **间歇性 0xC0000005**（同一
     份代码 10 次编译崩 1 次）。
   - 边界：任何"先记下元素指针、递归之后再解引用"的写法都要复查；**间歇性**
     崩溃（同样输入时好时坏）优先怀疑缓存/表条目的生命周期，而不是随机性。
22. **类布局的 vptr 槽必须整条继承链统一**：
   - 动作：`class_vptr_offset`（`src/compiler/irgen.c`）对**所有类**一律返回
     1（0 号槽留指针位），结构体返回 0，显式布局类型自己给偏移也不留；
     虚表仍然只在"本类或祖先多态"时才发（`class_has_virtual_methods`），
     但**布局**不再看多态。自举侧同步：`src/selfhost/irgen.zan` 的
     `FieldBase` 与 `EmitStruct` 同样按"类一律留槽"改。
   - 理由：vptr 槽以前只在多态类里留，于是 `Base`（无接口无虚方法）与
     `Derived : Base, IFace`（实现了接口）给**同一个字段**两套下标
     （0 vs 1）：基类构造器把字段写进了派生对象的 vptr 槽，派生类读到的
     却是垃圾（实测 `d.count=-724564432`），ARC 释放也漏掉基类那份字段
     （泄漏报告指向基类的 `new`）。派生类一继承基类就坏。
   - 边界：字段下标只能有一个来源（`get_field_index` / `class_vptr_offset`），
     结构体类型注册（`register_struct_type`）、调试信息、ARC 释放函数
     （`irgen_arc.c`）、反射（`irgen_reflect.c`）都必须跟着它走；漏一处
     就又是一处错位。判定器：`tests/conformance/inherit_interface_layout.zan`
     （基类字段在派生实例上的读写 + 基类方法 + 接口分派），泄漏档与
     arcguard 档同源覆盖。

---

## 三、运行时数据布局与内存所有权（Runtime / ARC）

1. **Dict 内建内存布局与保序约束**：
   - 动作：Dict 的 keys/values 并行缓冲区必须严格保持插入顺序；元素删除（Remove）使用 memmove 整体向前搬移并置 `indexed_count = 0` 使索引表整体失效。
   - 理由：枚举顺序与析构顺序依赖插入序；防止增量哈希索引修复引发探测链死循环。
2. **FFI 外部指针借用与内建所有权白名单**：
   - 动作：从 `DllImport` 外部返回的字符串是借用指针，运行时不释放、不下标；内建 API 若返回借用或托管字符串必须显式在借用白名单中登记。
   - 理由：防止跨 FFI 边界的野指针析构或托管堆内存泄漏。
3. **临时所有权实参自动回收（Lookup Builtins）**：
   - 动作：在集合查找类内建（如 `Contains`, `IndexOf`, `Dict.Remove`, `d[key]`）计算完成后，必须调用 `emit_release_owned_call_temp` 释放内联求值的临时对象。
   - 理由：防止查找表达式内部求值的临时字符串或对象在每次执行时泄露。
4. **异常展开栈与实参临时平衡注册（EH Unwind）**：
   - 动作：调用函数时若求值产生托管临时对象，必须在压栈前注册至每线程 unwind 栈；调用返回后 pop 次数必须与 push 次数严格相等。
   - 理由：保证被调方发生 setjmp/longjmp 展开时临时对象被完整释放，且展开栈深度平衡。
5. **委托双形态与闭包捕获保活**：
   - 动作：区分纯函数指针与携带上下文环境的闭包（Tagged Closure）；闭包跨调用帧逃逸或传递至异步线程时必须调用 ARC retain，并在执行入口校验 `ZAN_CLOSURE_TAG`。
   - 理由：确保被捕获的上下文变量在异步或脱钩执行期间生命周期完整。
6. **协程帧只许被 reap 一次（帧所有权契约）**：
   - 动作：给帧设置"自 awaiter"（`ASYNC_FRAME_AWAITER = 自身`、`AWAITER_STEP = __zan_co_reap`，即"任务完成即释放"）之前，必须确认没有第二条路径会再释放同一帧；带结果（keep_result）的帧不设自 awaiter，由用户侧 `reap` 释放，**两类帧的所有权契约不可混用**。
   - 理由：运行时 `__zan_co_frame_free` 的幂等守卫读帧首 8 字节的 `CO_DEAD` 位，但小对象分配器回收时把用户块首 8 字节写成 freelist 指针，守卫读到的已是垃圾 → 二次释放直接 double free（进程 abort）。实例：lambda 形态的 `Task.Run` 走 action 帧并自 reap，用户再读 `.Result`/`.Wait()` 又 reap 一次（该缺陷已按类型侧收口闭账，运行时仍缺幂等兜底，见 TASKS.md A380）。
   - 边界：运行时的守卫位不要放在分配器会回写的槽位（帧首 8 字节）；若要让 reap 幂等，标记必须落在分配器不覆盖的字段上。
7. **释放路径上的"已释放"标记不能放在分配器会回写的槽位**：
   - 动作：给任何"释放后仍要能被识别"的对象加幂等标记时，标记位必须放在**分配器不会覆盖**的字段上；小对象分配器回收时把用户块首 8 字节写成 freelist 指针（`src/runtime/rt_mem.c`），块首的任何标记在 free 之后立刻变成垃圾。
   - 理由：把守卫放在块首 → 二次释放读到的不是"已释放"而是 freelist 指针 → 幂等判断失效 → double free abort；`__wrap_free` 自己的双释放检测（块头 magic）才是真正兜底，它一命中就 fatal。
   - 边界：真要让 reap 幂等，用**注册表仲裁**（活协程表 `zan_co_live_del` 返回"本次是否真的摘掉了这个帧"），别指望对象里的标记位。
8. **同一块内存有两条释放路径时，先定单一所有者，再谈幂等**：
   - 动作：动手改帧/缓冲区的释放协议前，先把"谁拥有它、什么时候交棒"写在纸上：完成路径与用户路径同时存在时，必须选一个做唯一 owner，另一个改成只观测（等状态、不释放）。把"两边都释放但加了守卫"当解决方案，等于把 double free 换成偶发 UAF。
   - 理由：`Task.Run(lambda)` 的 action 帧此前"完成路径 reap 一次 + 用户侧 reap 一次"，靠时序侥幸没炸；真正的收口是让类型区分两种帧（无结果 action 帧 vs 带结果 keep_result 帧），使误用在编译期被拒。
9. **集合元素槽位按「字」而非「个」计**：
   - 动作：`List<T>` 的元素槽步长是 `elem_slot_words(T)` 个 8 字节字（值结构体 >1 字，
     指针/整型/浮点为 1 字）；任何走槽下标的路径（索引读、Add/Insert/RemoveAt、
     AddRange、ToArray、Dict.Keys/Values、数组元素赋值）都必须用
     `slot_word_index(idx, words)` 换算，缓冲区字节数用 `cap * 8 * words`。
   - 理由：按「每元素 1 字」硬编码的路径在结构体元素上会只搬第一个字，
     读出来是全零（`List<Vector3>.AddRange` 的元素全 0），且缓冲区会越界写。
   - 边界：`elem_slot_words` 由结构体的 LLVM 布局算出；布局算不出来时
     （`llvm_scalar_size` 为 0）必须报错拒绝，而不是悄悄退化成 1 字。
10. **静态（immortal）对象与堆对象共用头部布局时，哨兵判定必须覆盖每一条 ARC 路径**：
   - 动作：字符串字面量等编译期常量不是堆对象，它们的引用计数用哨兵值
     `ZAN_STRING_SENTINEL_RC`（不参与计数）。凡是会读写"对象前 16 字节计数头"
     （`ZAN_OBJ_RC_OFF = -16`）的 ARC 路径——通用 retain / 通用 release、字符串
     retain、动态释放——都必须**先判哨兵再动计数**。静态对象在只读段，对它做
     原子加一/减一就是访问冲突。
   - 理由：判一半就会崩。字符串 retain 与动态释放都判了哨兵，唯独通用
     retain/release 漏了；而"字面量存进 `object` 槽"走的正是通用 retain
     （槽的静态类型是 object 不是 string），于是对只读段做原子加一 → 0xC0000005。
     实例：`conformance_ternary_is_type` / `conformance_cast_string_object` /
     `conformance_cs_b05_pattern_var` 三条同时崩，根因同一（见 TASKS.md A412）。
   - 排查手法：访问冲突先看**写入目标地址落在哪个映像段**——写 exe 的只读段
     = 在改静态对象；再看寄存器里的对象指针是不是"目标地址 + 16"（那就是 ARC
     计数头）。崩溃现场在 `<exe_dir>/zan_crash.log`（含 fault/rip/rax/raw stack），
     先读它再决定要不要上调试器；最小探针二分（`object o = "字面量"` vs
     `string s = "字面量"`）通常一步就切到根因。


---

## 四、编译发布与驱动链接（Packaging & Cross-Linking）

1. **驱动运行时打包与 LLVM 模块生命周期顺序**：
   - 动作：驱动 manifest 中的条件依赖符号前缀（如 `WebView`, `Cef` 等）必须在释放 LLVM 模块前预缓存至 `prefix_cache`，禁止在 `zan_irgen_release_llvm` 后调用模块遍历 API。
   - 理由：LLVM 模块销毁后指针置空，再次遍历会导致进程异常崩溃。
2. **按需拉入与运行时依赖前缀触发**：
   - 动作：标准库与驱动的按需拉入应基于符号消费触发，避免无条件的粗粒度导入引入沉重的多线程或特定平台运行时依赖。
   - 理由：减少单执行文件的发布体积，避免无用依赖拖慢启动性能。
3. **iOS Mach-O 交叉编译平台标记与原生外壳驱动内嵌**：
   - 动作：交叉编译生成 iOS ARM64 运行时对象时，必须指定 `-target aarch64-ios`（严禁使用 `aarch64-macos` 代替），避免 `ld64.lld` 因平台头标识不符（`has platform macOS, which is different from target platform iOS`）拒绝合流。针对 iOS 上的 `zan_gui` 与 `zan_image` 驱动，编译器禁止生成桩函数，而是直接静态内嵌 `zanrt_gui.o` 与 `zanrt_image.o`。
   - 理由：iOS 设备禁止动态加载未签名的第三方 dylib，所有 GUI 视图生命周期、Retina 触控、2D 软件光栅与图像解码器（PNG/WebP/SVG）驱动均以静态目标文件合流进可执行 Mach-O，确保输出的标准 `.ipa` 即装即用。
4. **iOS CoreText 字体生命周期、主线程呈现与触控事件流纪律**：
   - 动作：(1) 通过 UIKit `[UIFont systemFontOfSize:]` 桥接获取系统字体后，必须显式 `CFRetain` 并常驻全局字号索引表（`g_font_table[128]`），其 ascent/height 度量必须持久化缓存，禁止在文本测量或光栅化完成时直接调用 `CFRelease` 释放系统单例，防止内部度量数据归零引发排版坍塌；(2) CoreText 绘制采用正向坐标系光栅化，基线锚定 `descent + 1.0`，内存行索引直接按 `mask[py * tw]` 正向写入 Zan 软件表面，严禁在写入时做垂直翻转（`th - 1 - py`），否则会导致字形 180 度上下倒置；(3) iOS CALayer 的 `setContents:` 与 `CATransaction` 提交必须通过 `dispatch_sync_f` 派发至主线程（`_dispatch_main_q`）执行，禁止在后台工作线程直接 flush；(4) 触控滑动判定阈值（slop）设定为 `6.0f * scale`（6pt），滚轮事件在队列中实施增量合并（coalescing），并在拖拽释放时标记 `flag = 1` 抑制误触。
   - 理由：UIFont 在 iOS 内部由 UIKit 统一管理引用计数，过度释放破坏单例内存引发度量归零与中文降级回退（问号乱码/像素风）；CGBitmapContext 在 iOS 内存布局中第 0 行为顶部，反向 blit 会造成字形上下颠倒；CoreAnimation 事务在非主线程提交会被系统延迟至未知主循环周期，导致拖动画面延迟极高（卡顿迟钝）。

---

## 五、内建类型成员表与代码生成的一致性契约

1. **内建成员表签名必须与实际发射的返回类型一致**：
   - 动作：`src/compiler/builtin_api.c` 的成员表（`members_*`）里声明的返回类型，必须与 `irgen_call.c` / `irgen_builtins.c` 中该成员实际发射的 LLVM 返回类型逐个对齐；改实现语义必须同改表里的签名（或反之）。
   - 理由：类型检查只认成员表，代码生成只认发射代码，两边脱节时**类型检查先拒绝合法用法**，而运行期其实是对的——症状是"明明实现了却编不过"，极难定位。实例：`Dict.Remove` 表里写 `void Remove(K key)`，实现却返回 0/1 标志（`irgen_call.c` 命中置 1、落空置 0），于是 C# 惯用法 `bool rm = d.Remove(k)` 被判 `cannot convert 'void' to 'bool'`，`conformance_arc_lookup_owned_arg` 连带它的 leakcheck 孪生一起红。
   - 边界：内建集合方法（`Dict` / `List`）的"命中/落空"一律用 0/1 标志返回，对齐 C# `Dictionary.Remove` 的 bool 语义；不要把槽位下标当返回值暴露出去。
2. **内建表就是公开面，改它等于改 API**：
   - 动作：内建成员表同时是 IDE 补全与 LSP 的数据源（`zanc --emit-symbols` 写出的类型/成员索引），改签名后必须同步 `docs/STDLIB.md` 对应节，并留一条 conformance 用例钉住语义。
   - 理由：表里的签名直接进 IDE 补全与类型检查；声明与实现不一致时，IDE 补出来的用法与编译器的判定会互相打架，用户看到的是"补全说能写、编译器说不能写"。
3. **批量改可见性（`private` / `public`）属于跨边界公共面改动**：
   - 动作：把成员改成 `private` 或补 `public` 之前，先查它的跨包调用方（`tools/agent/impact.ps1` / `map.ps1 -Callers`），并定向跑受影响的用例。
   - 理由：可见性一改，**外部调用点立刻编译不过**，而这类破坏不会自己暴露——实例：`Socket.ResolveIpv4` 被规范成 `private` 后，`conformance_async_dns` / `conformance_async_mt` 两个用例从那天起一直红，直到有人手工重跑才发现（档位日志停在改动前一天）。
   - 边界：默认可见性是 public（无修饰符即 public），显式 `private` 才是收窄；规范化的目的是让可见性**显式可读**，不是顺手收紧 API。
4. **内建静态调用的返回类型必须按实参形态定型，并与 irgen 的分支一一对应**：
   - 动作：同一个内建静态成员（`Task.Run` / `Task.Spawn` 这类）传不同形态的实参时，若 irgen 会走**不同的帧/所有权分支**，检查器就必须给不同的返回类型（无类型实参的"无结果类型" vs 带类型实参的 `Task<T>`）；检查器不得对内建调用的返回类型一律宽松放行（`type_error` 对一切可赋值），也不得把"结果类型"一律写成 `long`。
   - 理由：宽松放行会让"把无结果句柄当 `Task<T>` 用"一路通到 IR 与运行期，直到帧被释放两次才 abort；类型侧收口后，误用在编译期就被拒绝，运行期那条 double free 路径根本不可达。实例：`Task.Run(lambda)` 产出的是无结果 action 帧，把它声明成 `Task<int>` 去读 `.Result` 曾经是运行期 double free（已闭账，见 TASKS.md A380 的残留项）。
   - 边界：改检查器的定型前，先读 irgen 里同一调用的分支（哪个实参形态走 keep_result、哪个走 action 帧），两边必须对得上；类型说 A、代码生成做 B 是最难查的一类 bug。
5. **内建成员表里的类型名不是可见符号，跨命名空间引用仍要显式 `using`**：
   - 动作：`src/compiler/builtin_api.c` 的类型表（`BT("Aes", "Aes", ...)`）只服务于
     **成员签名的类型定型**（`Aes.Encrypt` 的返回类型是 `Vector128`），不参与名字
     解析；引用某个类必须让它的命名空间真正进入作用域（文件头的 `using`）。
   - 理由：表里有 "Aes"（`System.Runtime.Intrinsics.X86.Aes` 硬件原语），但
     Wechat SDK 里的 `Aes.DecryptBlockEcb`（`System.Security.Cryptography.Aes`，
     在 Zan.Security 包里）报的是 "'Aes' is not a known variable, type, or
     namespace"——**未知名**而不是"未知名成员"，极易误判成"编译器漏注册内建
     类型"；实际只差一行 `using System.Security.Cryptography;`，同包的另一个
     文件早就写了这一行。
   - 边界：同名类分处不同命名空间时，先确认 import 的是哪一个；内建表的签名
     按硬件原语那份算，名字解析归 `using`，两套机制不要互相推断。
6. **把成员收窄成 private 之前，先按"族"核对公开面**：
   - 动作：可见性规范化（批量补显式修饰符）收窄成员时，判定基准是
     **同族成员**（同一类里其它同角色成员）＋**用例的静态直调**；凡是用例以
     `Cls.Member(...)` 形式直接调用、且该成员是自足工具（不依赖外部状态）的，
     就是公开面的一部分，应保持 public。
   - 理由：规范化曾把 38 个这类成员收成 private，**22 条 conformance 用例直接
     编译失败**——红灯藏在编译错误里，看起来像"用例坏了"，其实是公开面被误收。
     铁证：`WechatReply.Text` 被收窄而同族 `Image`/`NewsItem`/`News`/`Build`
     全是 public；`MySqlConnection.readBinValue` 被收窄而
     `MySqlSyncConnection.readBinValue` 本来就是 public（无修饰符=public）。
   - 边界：默认可见性是 public（无修饰符即 public），显式 `private` 才是收窄——
     规范化的目的是让可见性**显式可读**，不是顺手收紧 API；收窄前先问"这条成员
     有没有对外服务的义务"，而不是"它原来有没有 public 关键字"。

