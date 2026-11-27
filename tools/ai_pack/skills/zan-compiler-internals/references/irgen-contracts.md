# LLVM 代码生成与指令下沉核心契约（IRGen）

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
     用 `Func<T,V>` 形参代替按 V 实例化。
16. **泛型实参的实参类型必须被检查（`List<int>.Add("x")` 曾一路放行）**：
   - 动作：给泛型容器/泛型类加成员时，实参类型要按**接收者的类型实参**
     替换后再比：内建泛型从 `builtin_api.c` 的签名表取形参类型（T/K/V），
     按 `recv->type_args` 替换；用户泛型类用 `zan_binder_subst_named` 替换
     形参里的 T。两条路都要落到 `checker_arg_type_mismatch`。
   - 理由：形参类型是**类型参数**时，旧的 `checker_arg_type_mismatch`
     开头就 `return false`（"含类型参数就放行"）——于是
     `List<int>.Add("字符串")` 编译通过，运行时把字符串指针存进 int
     列表，读出来是**垃圾指针**（静默损坏，不是崩，比报错难查得多）。
   - 根因（两条路都绕开了实参类型检查）：
     - 内建泛型：`List`/`Dict` 没有 `sym`（成员签名在 `builtin_api.c` 的
       表里，不是源码声明）→ `call_arg_signature` 拿不到签名 →
       `check_call_arg_type` 从没被调用。
     - 用户泛型类：`call_arg_signature` 看到 `recv->type_arg_count` 非零
       就直接返回 NULL（怕泛型形参匹配不上）→ 实参检查整段被跳过。
   - 边界：`checker_arg_type_mismatch` 本身对"值类型形参 + 引用类型实参"
     这一格是放行的（只查"引用 ↔ 引用"）。所以泛型路径要**自己**补这一格，
     别指望通用函数兜住——通用函数一收紧就会误伤 stdlib 里的 extern
     原生调用（`NativeMemory.Base64Encode(nint, long)` 实参传 `byte[]`，
     靠 irgen 侧的 `nm_arg` 转换）。
17. **泛型类的 `this.field` 必须按当前实例替换类型实参**：
   - 动作：`member_access_field_type`（`src/compiler/irgen_expr_core.c`）在
     `this.x` / `base.x` 分支上要按 `g->cur_inst` 调
     `subst_type_param_deep`，不能直接返回字段的**声明**类型。
   - 理由：声明类型停在类型参数上时，元素类型判成"擦除的指针"——结构体
     元素写进去的是临时对象的地址，读回来就是野指针（实测 0xC0000005）。
     指针大小的元素（引用类型）恰好蒙对，所以这个缺陷能潜伏很久。
   - 边界：**字段布局**是另一处（泛型类的结构体布局不随实例特化，值类型
     字段仍会算错槽位大小，见 TASKS.md A421）——本条只管**表达式类型**，
     两处要一起修才算彻底。
18. **泛型类的字段布局不随实例特化（已知缺口，A421）**：
   - 动作：写"泛型类持有值类型字段"（`class Box<T> { T value; }`）之前要知道
     当前不支持：布局按声明处的字段类型算（类型参数 → 指针大小），具体实例
     化时没有重算槽位大小 → 写超 → 堆损坏。
   - 理由：症状是"构造时崩"或"读回是垃圾"，与用户的类型定义毫无关系，
     定位成本极高。
   - 边界：替代形态——把值类型放进 `List<T>`（元素类型走已替换的路径），
     或让泛型类只持有引用类型字段。
19. **运算符重载必须参与类型推导**：
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
20. **运算符调用的实参形态要按期望类型归一化**：
   - 动作：运算符调用路径（`src/compiler/irgen_expr.c`）拿接收者与第二个
     实参时，若参数类型是 struct 而实参是指针槽位（`new 结构体(...)` 的
     返回值），补一次 `LLVMBuildLoad2`；常规调用走 `coerce_args_to_params`，
     运算符路径**不能假设**已经走过。
   - 理由：否则报 "Call parameter type does not match function signature"，
     而用户写的就是最普通的 `a + b`。
   - 边界：标量与结构体的混合重载**只能标量在右**（`v * 2.0` 合法，
     `2.0 * v` 编译器直接报 "cannot apply operator to 'double' and 'V'"）——
     这是语言限制，不是缺陷，不要靠改编译器放宽。
21. **跨编译单元的 struct 字段布局必须按需注册后取值**：
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
22. **结构体类型注册表的条目指针在递归注册后失效**：
   - 动作：`register_struct_type` 登记完自己就立刻记下
     `int my_index = g->struct_type_count - 1;`，递归注册**之后**再用
     `&g->struct_types[my_index]` 取条目取字段数据。
   - 理由：递归注册会 realloc `g->struct_types`（数组搬家），事后用
     `&g->struct_types[g->struct_type_count - 1]` 取到的是**别的类型**的
     条目 → 字段数据互相覆盖 → 类型混淆 → **间歇性 0xC0000005**（同一
     份代码 10 次编译崩 1 次）。
   - 边界：任何"先记下元素指针、递归之后再解引用"的写法都要复查；**间歇性**
     崩溃（同样输入时好时坏）优先怀疑缓存/表条目的生命周期，而不是随机性。
23. **类布局的 vptr 槽必须整条继承**树**统一**（2026-11-25 起按树条件化）：
   - 动作：`class_vptr_offset`（`src/compiler/irgen/irgen_type_map.c`）对"树根（最上层
     非 object 祖先）的所有后代里有任一多态类（虚/抽象/覆写方法或接口）"的类返回 1，
     否则 0；结构体与显式布局类型返回 0。计划由 `class_vptr_plan_compute` 在
     `zan_irgen_emit` 注册任何类布局之前扫全部类型声明算出，计划外的类按旧口径留槽；
     `ZAN_VPTR_ALWAYS=1` 退回一律留槽（二分用）。自举后端有自己的布局（`FieldBase`
     一律留槽），与 C 宿主无须一致。
   - 理由：曾按"本类或祖先多态"逐类决定，`Base`（无多态）与 `Derived : Base, IFace`
     给同一字段两套下标（0 vs 1），派生类一继承就坏；后来一律留槽，又让 binary-trees
     这类无多态节点白占 8 字节（槽恒 null）。按整棵树决定两头都对：同树同答案。
   - 边界：字段下标只能有一个来源（`get_field_index` / `class_vptr_offset`），类型注册、
     调试信息、ARC 释放、反射、字段走图都跟着它走。按首字比对身份的分派（接口分派、
     object.ToString 候选链）只在有 vptr 的对象上命中，无 vptr 对象首字是字段值，
     走兜底。判定器：`tests/conformance/inherit_interface_layout.zan`、
     `tests/conformance/object_header_compact.zan`。
24. **泛型类的每个实例化必须有自己的 vtable**：
   - 动作：泛型类不能只发一张共享 vtable。`get_vtable_global_inst`
     （`src/compiler/irgen_arc.c`）按实例实参生成 `__zan_vtable_<Class>$<token>`，
     槽位指向该实例化的**特化版**方法体；`new` 表达式按实例化取表
     （`src/compiler/irgen_expr.c`）。表**创建即填**（`emit_vtables` 在部分路径
     上早于方法体发射，晚填会拿到全 null 的表）。**特化版与擦除版的 LLVM
     签名不一致时退回擦除版**（值类型类型实参会改签名）。
   - 理由：整类共享一张表时，槽位指向**擦除版**方法体 → 虚方法体里的 `is T`
     拿不到类型实参 → 判定恒 false（**静默错值**，不报错）。特化版方法体早
     就发射出来了，只是没被挂上——只看"方法体对不对"会误判成已修好。
   - 边界：只管**虚调用槽**；非虚调用走类级实例化路由（`route_generic_method`）。
     判定器 `tests/conformance/generic_virtual_dispatch.zan`。
25. **形参↔实参映射口径：接收者不是第 0 个形参**：
   - 动作：`infer_method_tp_bindings`（`src/compiler/irgen_expr.c`）的 `arg_base`
     必须与发射端同口径：`(recv_expr && m_static) ? 1 : 0`——只有"静态方法经
     实例表达式调用"时接收者才占第 0 个实参；**实例方法的接收者是 `this`**，
     不是声明形参之一。`try_method_spec` / `emit_method_spec_call` 本来就是
     这个口径。
   - 理由：写成 `recv_expr ? 1 : 0` 时，实例泛型方法拿**接收者**去和形参类型
     合一（形参 `Chan<T>` vs 接收者类，类型实参数对不上 → 一个都绑不上）→
     `bind[0]` 保持 NULL → `get_or_create_method_spec` 的
     `if (!bind[i] || !type_is_concrete(bind[i])) return -1` 拦下 → 调用退化成
     **擦除版** → 体内 `is T` 折成常量 false（静默错值）。
   - 边界：静态方法按推断调用本来就是对的（`recv_expr` 为 NULL）——别把"泛型
     方法推断"整族当坏的，**实例/静态要分开看**；显式类型实参永远对。
     判定器 `tests/conformance/generic_method_inference.zan`。
26. **没有装箱设施：值类型↔引用类型的转换必须报错**：
   - 动作：Zan **没有装箱**，`object` 只能装引用。值类型 → `object` 的三条
     路径都要拦：① checker 的 `checker_cast_is_valid`（`dst->kind == TYPE_OBJECT`
     时来源必须是引用类型）；② lambda 体类型 vs 委托返回类型
     （`check_no_boxing_conversion`，表达式体在 `emit_lambda_typed`，块体的
     `return` 在 `irgen_stmt.c` 且只在 `g->lambda_depth > 0` 时查）；③ 泛型
     特化里的强转（`emit_expr_cast_expr`——checker 阶段类型参数还看不出来）。
   - 理由：没拦住就是 `LLVMBuildIntToPtr`——`42` 变成指针 `0x2A`，之后任何
     对象操作（ARC release 读头 / `is` 读 site 字）都在 `0x2A-16` 处读 →
     **当场 access violation**（0xC0000005），退出时 ARC 再回收一次假对象 →
     二次崩。lambda 那条还会先给出一个"非 null 的垃圾指针"，更难查。
   - 边界：checker **不给 lambda 形参建类型**，也**不检查块形 lambda 的语句**
     （`AST_LAMBDA` 分支直接 `return type_error`；`zan_checker_check_expr` 没有
     `AST_BLOCK` 分支）——所以 lambda 的返回类型检查只能在 irgen 做。
     `object → 具体类` 的下转照旧放行；真装箱（分配带类型描述符的堆对象）是
 
    独立战线，见 TASKS.md A454。
27. **泛型值类型与 null 的比较必须在 IR 层折叠，不能直接发 `icmp`**：
   - 动作：`emit_expr_binary` 里，当一侧是空指针常量、另一侧是**值类型**
     （struct / int / float）时，`==` 折叠成 false、`!=` 折叠成 true——值类型
     永远不是 null。两侧都要 `emit_release_owned_call_temp` 释放 owned 临时。
     可空聚合体（`zan.nullable.*`）**不按值类型折叠**：它的"是否为 null"由
     has-value 标记决定。
   - 理由：泛型形参 `T` 特化成结构体后，`v == null` 的左侧是**聚合值**、
     右侧是空指针，直接发 `icmp eq %struct.Point, ptr null` → LLVM 校验失败
     （Both operands to ICmp instruction are not of the same type）→ 编译期
     崩，且诊断信息指向用户代码而不是编译器缺陷。
   - 边界：折叠只对**一侧为空指针常量**成立；两个值类型之间的比较照常走
     `emit_binary_op_values`。语义口径：值类型 `v == null` 恒 false、
     `v != null` 恒 true；引用类型仍按指针比较。
   - 判定器：`tests/conformance/generic_value_null_compare.zan`。
28. **表达式体 Lambda 返回结构体必须按值取出，不能直接 `ret` 求值地址**：
   - 动作：`emit_lambda_typed` 的表达式体路径，当返回类型是 struct 且不是
     nullable 聚合时，先 `coerce_struct_value_arg(g, result, ret_type)` 按值
     取出再 `LLVMBuildRet`。
   - 理由：表达式体求值出的是结构体对象的**地址**，与"按值返回结构体"的
     签名不符 → `ret ptr %new` 与 `%struct.Point` 不匹配 → LLVM 校验失败
     （Function return type does not match operand type of return inst）。
     块体 lambda 因为走 `emit_struct_value` 已经按值，**只有表达式体漏**。
   - 边界：nullable 聚合（`zan.nullable.*`）是另一种形态，跳过。
   - 判定器：`tests/conformance/lambda_struct_return.zan`。
29. **catch 退出路径必须先释放异常对象、再释放局部变量；eh_slot 按 LIFO 弹**：
   - 动作：所有从 catch 块退出的路径（return 带值 / return 无值 / break /
     continue / goto）都改成**先 `emit_release_active_catch_excs`、再
     `emit_release_owned_locals`**；`emit_release_owned_locals_except` 弹
     eh_slot 标记时按**逆序（LIFO）**。
   - 理由：catch 进入时异常对象压在**异常临时栈顶**（`exc.push`）。原顺序先
     释放局部变量——弹 eh_slot 标记时弹掉的其实是栈顶那个**异常**——于是
     局部变量的标记滞留在栈上、栈深错位；后续异常展开二次释放同一个对象，
     读已释放对象头拿到垃圾描述符 → SIGSEGV。定位链：
     `zan_rt_release_dyn` ← `__zan_eh_tmp_unwind` ← 业务帧。
   - 边界：catch 块内**正常执行到底**的路径不走这套释放；只有"从 catch
     里跳出"的路径要成对释放异常与局部变量。
   - 判定器：`tests/conformance/catch_return_release.zan`。
30. **委托调用点必须按当前实例替换类型参数，否则与 lambda 的实际签名不一致**：
   - 动作：`emit_delegate_call` 里，若 `g->cur_inst` 非空且委托类型是
     `TYPE_DELEGATE`，先 `subst_type_param_deep(g, delegate_type, g->cur_inst)`
     替换类型参数，再用替换后的类型算形参/返回的 LLVM 类型；结构体实参走
     `coerce_struct_value_arg` 规整。
   - 理由：泛型实例里委托字段的类型是 `Func<TTarget>`。调用点若按**未替换**
     的类型算签名，得到的是擦除形态（指针）；而实际 lambda 是按**已替换**类型
     发射的（按值返回结构体）→ 调用点把返回值当指针再 load → 读到 null/垃圾
     → `mov (%rcx),%eax` 崩溃（rcx=0）。
   - 边界：替换要**深替换**（`subst_type_param_deep`），嵌套在泛型实参里的
     类型参数也要换；只换顶层会漏掉 `Func<List<T>>` 这类形态。
   - 判定器：`tests/conformance/mapping_profile.zan`（结构体目标）、
     `tests/conformance/mapping_gen.zan`。
31. **惰性构建的辅助函数会清掉"当前调试位置"——调用点必须兜底补 `!dbg`**：
   - 动作：`di_set_loc` 记下最近一次设置的位置（连同它属于哪个函数）；
     统一发调用的入口（`zan_call2`）在发码前检查"当前调试位置是否为空"，
     为空且**当前函数确实带 subprogram**、且那个位置**属于当前函数**时补回去。
   - 理由：类的释放体、集合的释放体这类辅助函数是在**用户代码中间**被惰性
     构建出来的（`irgen_arc.c` 的 `build_class_release_body` /
     `build_collection_release_body`），构建入口 `di_clear` 把"当前调试位置"
     清掉且不恢复——于是**同一函数里后面那个调用就没了 `!dbg`**。LLVM 校验器
     对"带调试信息的函数里位置缺失的调用"直接报
     `inlinable function call in a function with debug info must have a !dbg location`，
     整个 `-g` 构建失败。
     症状落在**离病灶很远的地方**：报错说的是某个标准库的类型（如
     `LuaValue_op_call`），真因却是宿主编译器在它之前清了位置。带调试信息
     的构建（IDE 的调试版、崩溃排查）因此整个起不来。
   - 边界：只补在**确实带 subprogram** 的函数里（运行时辅助函数没有
     subprogram，塞位置反而触发 LLVM 另一条校验）；位置属于别的函数时**不补**
     （挪位置到别的函数同样是校验错误）。补的位置必须与"被清之前"同属一个函数。
   - 手法（找出真凶，别猜）：在 `di_clear` 里加一行诊断，打印"清位置时正在
     构建哪个函数、清之前的行号"，编一次就知道是哪个辅助函数在作祟。
     猜形状是没用的——"实参求值期间释放对象"这类触发条件可以有很多写法，
     逐个试要试十几次。
   - 判定器：一条带 `-g` 编译的 conformance 用例（普通构建不产生调试信息，
     踩不到这条路径）。
32. **`new List<T>(实参)` 必须按实参静态类型分流语义，数组实参不能落进集合初始化式**：
   - 动作：`new List<T>(x)` 有两种语义——实参是 `List<T>` 时是拷贝构造
     （checker 置 `new_expr.list_copy`，irgen 按槽逐元素搬）；实参是 `T[]`
     时是**数组拷贝构造**（读 `zan_array_len`、逐元素取出后按槽步长写入）。
     `list_copy_candidate` 只认 List<T> 实参，所以 irgen 里凡
     `!list_copy && args.count==1 && arg_inits.count==0` 的形态**不能**默认走
     "集合初始化式"（那会把整个数组对象当 1 个元素 Add）；要先
     `infer_expr_type` 判实参是否一维数组，是则发数组→List 逐元素拷贝
     （结构体元素 GEP by elem type + `load_struct_from_slot`，标量按位宽
     SExt/Trunc 到 i64），目标槽照常走 `slot_word_index` + 槽写入。
   - 理由：数组实参走 Add(整个数组) 时 count=1、元素是"假扮字符串的数组头"
     （len=垃圾、data=指针字节），下标 1 即越界，**全程零诊断**。实测踩坑：
     FirefoxBrowser 用 `new List<string>(args)` 拼安装器参数，进程拿到的
     argv 全是垃圾字节，官方安装器收不到 `/S` 进交互模式弹窗干等、
     不写任何文件，表象却是"自动安装反复触发"——三层排查全是假线索。
   - 判定器：`tests/conformance/list_array_ctor.zan`（string/int/空数组/
     快照独立性/结构体元素五组断言）。
33. **手写 CFG 编辑必须同步维护 PHI 前驱表——VerifyModule 失败绝不能静默吞掉**：
   - 动作：optimizer 里折叠常量条件分支（`LLVMBuildBr(target)` + erase
     condbr）时，"本块→未选中后继"这条边同时消失，未选中后继块里每个
     PHI 中来自本块的 incoming 条目必须一并删除（LLVM-C 没有
     remove-incoming，做法：新建 PHI 收集幸存 (value, block) 对 +
     `LLVMReplaceAllUsesWith` + erase 旧 PHI；本块是唯一前驱时用
     undef/null 顶替引用）。两个后继是同一块时跳过清理（边仍在）。
     另外 `LLVMVerifyModule` 失败必须 `fprintf` 报警——曾静默
     dispose-and-return，default&lt;O1&gt; 整条管线被无声跳过，所有"优化"
     实际来自管线之外的自定义 pass，用户毫不知情。
   - 理由（A595，坑很深）：early-cse 会把短路条件的分支化成常量，
     constfold 随后折叠该分支；悬空 PHI 条目使模块非法 → 管线整条
     被静默跳过 → 管线之后的 pass 与后端在非法 IR 上跑，字符串长度
     探测把正常串判成 null（长度归 0）：`--publish` 构建 ff_bidi_demo
     三处 soft error 后挂起，而 O0 一切正常。排查时表象全指向业务代码
     与 publish 插桩，真因在两步之外的 CFG 编辑。
   - 排查工具（已留常设开关，optimizer.c）：`ZAN_DUMP_OPT_IR=1`（dump
     优化后 IR）、`ZAN_OPT_PIPELINE=none/<管线串>`、
     `ZAN_NO_OPT_EARLY/ARC/DEVIRT/CONSTFOLD/DCE/INLINE=1`（逐 pass 关）、
     `ZAN_EARLY_PASSES=sroa,early-cse`（改 early 段）。验证时盯 stderr 的
     `module verification failed`——它出现即管线没跑。
   - 判定器：`tests/conformance/opt_constfold_branch_phi.zan`（注册处
     带 `-O1` 编译；普通 conformance 全在 -O0，永远踩不到这条路径）。
34. **临时拥有接收者的成员赋值必须在赋值后释放该临时接收者**：
   - 动作：在 `emit_expr_assignment`（`src/compiler/irgen_expr_ops.c`）处理
     `AST_MEMBER_ACCESS` 赋值（`obj.field = value` 或 `obj.prop = value`）时，若
     `obj_expr` 经由方法调用等表达式产生临时拥有对象（`expr_yields_owned_rc_value` 为真），
     必须在字段存储或属性 setter 调用完成之后，调用
     `emit_release_owned_call_temp(g, obj_expr, obj_val, locals)`（setter 分支对应 `rval`）
     释放该临时接收者。
   - 理由：如果不释放，调用返回的临时对象引用计数永远停留在 +1，宿主释放时
     由于计数未归零产生 ARC 内存泄漏。典型案例：`this.EnsureExtra().bindPath = value;`
     使 `ControlExtra` 的引用计数恒为 2，`Control` 销毁时只能减至 1，导致
     `ControlExtra` 永久泄漏。
   - 边界：仅对堆上托管对象生效（结构体不走该路径；局部变量标识符由
     `emit_release_owned_call_temp` 内部识别并安全跳过）。
   - 判定器：`tests/conformance/arc_assign_call_temp_receiver.zan`。

35. **软失败守卫的快路径要靠"版本化"而不是少查——合流点是性能杀手**：
   - 动作：try 外的守卫失败侧报告后合流回快路径（`phi(x, scratch)`、`safeidx`），
     合流之后 LLVM 不再知道 x 非空/下标在界，于是每次访问重查、经 phi 取址、
     不能 CSE/外提，夹在 retain 与 release 之间的守卫 invoke 还让 ARC 对消不掉
     （nbody 带检查 1.9s vs 去检查 0.9s）。别改语义去"只报第一个站点"，用
     `guard_version.cpp`（optimizer-early 扩展点，拆分管线里在两半之间）：
     ①整环版本化——循环克隆一份先跑，克隆里每个守卫的失败侧保留自己的报告块，
     报告块的后继改接原循环（原循环从那一站接着全检查地跑完），快环无调用无合流；
     ②循环不变守卫在预头部 unswitch；③大环退回"守卫支配区域"克隆。慢世界的分支
     打 `zan.guard.done` 不再版本化（k 个变量 k+1 份，不是 2^k）。
   - 配套前端卫生（都影响快路径）：分配失败用 `emit_runtime_check_noreturn`
     （报告后 exit 70，旧做法续跑后往空头写而 SIGSEGV，且让"新数组非空"丢失）；
     已钳位下标用 `emit_checked_elem_gep`（inbounds nuw，元素写不再别名长度头）；
     数组长度读打 `zan.arraylen` TBAA + `!range [0,2^63)`（软失败暂存读作 2^62，
     仍在范围内）；`emit_index_safe_bounds` 自己就报告，前面别再叠
     `emit_index_bounds_check`（同站点双报、双分支）。
   - 坑：交叉边若直接接到原报告块，它有多个前驱，late ARC 2b（把 retain 挪到
     冷边）找不到单前驱的落脚块，b 的 retain/release 照留——所以交叉点放在
     **报告块之后**。区域上限 400 条指令：1500 时 zandoc text +14%。
   - 实测（user 秒，带检查 / --no-runtime-checks）：nbody 1.9→~0.5 / 0.4，
     fannkuch 2.3→1.75 / 1.75，sort 0.36→0.30 / 0.28。剩余 nbody 差距是外层
     `Body a` 的 retain/release：内层快环有出口到慢环，late ARC 跨循环对消
     要求"全路径到 release"，需要它学会在侧出口补 retain。
   - 判定器：`tests/conformance/guard_version_loops.zan`（-O0 与 -O2 stdout/
     stderr 逐字节相同）；`ZAN_NO_GUARD_VERSION=1` A/B，
     `ZAN_GUARD_VERSION_STATS=1/2` 看版本化了哪些环。

