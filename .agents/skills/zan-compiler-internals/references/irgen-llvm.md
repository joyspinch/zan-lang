# LLVM 代码生成与底层指令下沉契约

## 1. LLVM Builder 与指令发射基本契约

- **`LLVMBuildGlobalStringPtr` 必须在有效 BasicBlock 内调用**：
  在 LLVM C API 中，调用 `LLVMBuildGlobalStringPtr(builder, str, name)` 等构建指令时，`builder` 必须已经通过 `LLVMPositionBuilderAtEnd(builder, bb)` 关联了有效的 BasicBlock。对未定位或已脱钩的 builder（如 IRGen 主流程结束后已退出的 `g->builder`）调用会导致指令插入空块内部，触发底层 SIGSEGV 崩溃。在合成辅助函数中返回空串时，必须在自身已定位的 entry block 内通过 `LLVMBuildGlobalStringPtr` 生成或直接发射常量指针。
- **`LLVMBuildSelect` 两臂均会求值**：
  带内存分配或副作用的辅助操作（如数字转字符串、格式化缓冲）严禁放进 select 的死臂，否则无论条件真假都会分配并造成内存泄漏。必须使用条件分支（`LLVMBuildCondBr`）分别在独立 BasicBlock 中计算，并在汇聚块通过 `LLVMBuildPhi` 进行合流。
- **`LLVMIsGlobalConstant` 与 `LLVMIsConstant` 的语义差异**：
  `LLVMIsConstant` 测试的是 LLVM ValueKind 是否为 Constant 的派生类，对所有全局变量（GlobalVariable）恒为真。要判定变量是否真正只读，必须使用 `LLVMIsGlobalConstant`。若用错，会将可变的全局缓冲区放入只读数据段（.rdata），导致运行时写入触发段错误。
- **避免为内部全局符号手动指定 Section**：
  类似 `llvm.global_ctors` 等内部符号依赖 LLVM 后端特定的段降级机制。对其显式调用 `LLVMSetSection` 会破坏后端处理逻辑，导致静态初始化链断裂。

## 2. 内建原语与整数重载判定

- **内建硬件原语整型重载判断必须基于语义类型**：
  在 Zan 的 IRGen 体系中，整数字面量（`AST_INT_LITERAL`）在 `emit_expr` 中默认发射为 64 位整型（`i64`）。当降低包含多重载的硬件原语（如 `PopCount`, `LeadingZeroCount`, `TrailingZeroCount`, `RotateLeft`, `RotateRight`, `ReverseEndianness` 等）时，不可仅凭发射值的 LLVM 类型宽度（`LLVMGetIntTypeWidth == 64`）判断宽度，否则 32 位整数字面量会被误判为 64 位。必须通过 `infer_expr_type(g, arg, locals)` 查询语义类型（`TYPE_LONG` / `TYPE_ULONG` 为 64 位，其余标量为 32 位）。
- **`emit_*_call` 拦截原语的方法名比对**：
  使用 `method.len == N && memcmp(method.str, "Name", N) == 0` 时，长度 `N` 必须与字面量长度严格相等（统一使用 `sizeof("Name") - 1`）。长度不匹配会导致拦截静默穿透至链接期符号解析，报未定义引用。

## 3. 硬件向量与 SIMD 下沉契约

- **Vector128 寄存器映射与零开销互转**：
  `Vector128` 在 Zan 中定义为 16 字节值结构体 `{ long Low; long High; }`（LLVM `%struct.Vector128 = type { i64, i64 }`）。通过 `extractvalue`/`insertelement` 与 `extractelement`/`insertvalue` 实现纯 SSA 寄存器与 `<16 x i8>` / `<2 x i64>` 的双向转换，不经过任何 `alloca` 栈内存中转，直接映射到 128 位 XMM 寄存器。
- **非对齐加载与存储**：
  对于底层缓冲区加载与存储，必须显式调用 `LLVMSetAlignment(ld, 1)`，防止在非 16 字节对齐的缓冲区上触发 General Protection Fault (#GP) 异常。
- **类型化数组下标与字节偏移缩放**：
  对类型化数组（`float[]`, `int[]`, `long[]` 等）执行向量加载时，源码中的 `offset` 是元素下标。IRGen 阶段必须通过 `infer_expr_type` 获取元素宽度（`float`/`int` 为 4 字节，`long`/`double` 为 8 字节等），将 `offset * elem_size` 后再进行 GEP 字节寻址，避免步长错位。
- **目标架构指令集特性基线（`tm_features`）**：
  开启底层机器指令时，特性开关必须严格符合目标平台的 ABI 基线。例如 Android x86_64 ABI 保证 SSE4.2 但不保证 AVX/AVX2；LLVM 中 CRC32 拆分在独立的 `+crc32` 特性下。不能无条件开启宿主机支持的高级指令，防止交叉构建的产物在目标环境运行时抛出 SIGILL 非法指令异常。

## 4. 泛型类的类型参数字段：槽位是"按值内联"，读路径必须按实例替换类型实参

- **槽位表示是"按值内联"，不是"存指针"**：泛型类 `class Box<T> { T value; }` 的
  `value` 槽位按值内联存放——`generic_field_slot`（IRGen 阶段）按已知实例的最大
  ABI 尺寸把槽位加宽成 `[N x i64]`。指针方案在擦除上下文里会悬垂（擦除形态拿到的
  是调用方临时量的地址），所以内联才是唯一安全的选择。
- **读路径必须和写路径用同一套替换**：写路径 `field_store_type` 按**当前实例**
  （`g->cur_inst`）替换类型实参 → 写进去的是内联值；读路径若按**擦除形态**替换
  （`infer_expr_type(this)` 给的是 `g->current_type_sym->type`，即不带类型实参的
  声明类型），字段类型停在类型参数上 → `map_type` 退化成指针 → 把内联值的前 8
  字节当指针解引用 → 16 字节结构体直接 `0xC0000005`。
- **统一入口 `field_type_at(g, fsym, recv)`**：做"按接收者实例的深度替换"
  （`subst_type_param_deep`，浅替换只认裸 `T`，`List<T>` 这类嵌套实参会停在擦除态）。
  接收者是擦除形态**且就是当前类**时才用 `g->cur_inst` 兜底——别的泛型类的擦除
  形态不能拿当前实例的实参去替换，否则替错类。
- **浅替换 ≠ 深度替换**：`subst_type_param` 只处理字段类型本身就是 `T` 的情形；
  字段类型是 `List<T>` / `Inner<T>` 时必须走 `subst_type_param_deep`，否则元素
  类型停在类型参数上，按值语义退化成"存指针"，结构体元素写进去的是临时对象的
  地址，读回来就是野指针。
- **坑出处**：2026-10-20 实测 `Box<Pos>`（Pos = 4 个 int）`Set` 后 `Get` 读回即
  `0xC0000005`；写路径早就按实例替换，读路径没跟上（擦除接收者 + 浅替换）。
  回归用例：conformance 里的"泛型类值类型字段"用例（值类型字段读回、
  构造赋值、`int`/`string`/带 string 的结构体实参）。
- **残留限制**：擦除上下文（泛型方法、泛型参数化类型）里访问类型参数字段仍是
  固有限制——擦除版方法拿不到类型实参，既不知道值类型该占多少字节，也无法接管
  一份拷贝。引用类型实参时内联与指针都是 8 字节，恰好不炸；值类型实参会读错。
  见项目台账 A434。

## 5. 泛型类的基类实例必须一并注册——否则基类方法只有擦除版变体

- **实例注册表决定方法变体是否存在**：具体实例按 `type_sym + type_args` 去重
  登记（`add_generic_inst`），只有登记过的实例才会发出对应的方法变体，变体名
  形如 `Plan_Add$string`（`$` 后缀是实参的 mangled 名）。
- **派生实例要连带注册基类实例**：`class Validator<T> : Plan<T>` 实例化成
  `Validator<Signup>` 时，基类实例是 `Plan<Signup>`。只登记派生实例 →
  `Plan<T>` 的方法只剩擦除版（`Plan_Add`），调用退化成擦除版：基类方法体里
  `this.properties` 拿到的是擦除槽位的指针，与派生类看到的字段不是同一个东西
  （实测运行期 `0xC0000005`）。
- **替换要用派生实例的实参**：`subst_type_param_deep(g, t->base_type, t)`——
  基类类型里的 `T` 要用**派生实例已经替换好的实参**再替一次，不能用声明处的
  裸 `T`（浅替换只认裸 `T`，`List<T>` 这类嵌套实参会停在擦除态）。
- **注册必须早于布局计算**：结构体槽位按已注册实例的最大 ABI 尺寸加宽
  （`generic_field_slot`），而结构体 body 一旦设定不可再改 → 漏登记的实例会
  事后把槽位撑窄，按值内联的字段就会踩到相邻内存。
- **坑出处**：2026-10-20 实测链式泛型基类（`Validator<Signup> : Plan<Signup>`）
  的基类方法读字段拿野指针。IR 对照：修复前只有 `Plan_ctor`/`Plan_Add`/
  `Plan_Count`（全是擦除版），修复后多出 `Plan_ctor$string`/`Plan_Add$string`/
  `Plan_Count$string`。回归用例：conformance 里的"泛型基类值类型字段"用例
  （非泛型派生类 / 泛型派生类 / 基类具体实例共存 三条路径）。
- **深度上限**：基类成环（理论上不该出现）用深度上限 16 兜住，递归不会无限展开。

## 6. `--emit-ir` 的产物必须自述目标特性

- **编译器的后端档位 ≠ 消费方的默认档位**：编译器建 TargetMachine 时选的
  CPU/特性（如 x86-64 + sse3…avx2/fma/bmi）是**编译期**决定的；把
  `--emit-ir` 落出的 .ll 文本交给外部工具（llc 等）时，消费方默认按
  generic CPU 处理 → 图里残留的宽向量类型（256 位）拆不开 → 后端 fatal
  error。**zanc 自己出的二进制是对的，坏的是给外部消费的 .ll 文本。**
- **IR 里没有这层信息**：文本 IR 不带 target-cpu/target-features，函数级
  `attributes` 与 module flags 在 LLVM 20 的 llc 上都不作为合法化依据。
- **落地形态**：IR 头部写注释自述（`; zanc-target-cpu:` /
  `; zanc-target-features:` + 消费方该用的后端命令行），同时往 stderr 打
  同一条提示。实测：按提示带 `-mcpu/-mattr` 消费同一份 .ll 即通过，不带
  仍 fatal error。
- **别用"先跑一遍 opt 优化再喂后端"绕过**：优化器会把死函数和冗余结构一起
  干掉才让后端过，编译器自己产出的 IR 仍然过不了自己的后端——那是掩盖，
  不是修复。
- **坑出处**：2026-10-20 实测（256 位向量路径在 generic CPU 下触发
  `Do not know how to split this operator's operand!`）。

## 7. 运算符重载：一元与二元同名，靠形参个数区分；重载结果是"拥有型临时值"

- **一元与二元重载同名**：解析器把 `operator -` / `operator +` 一律映射成
  `op_sub` / `op_add`，一元与二元靠**形参个数**区分（重载是 static 的：1 个
  形参=一元、2 个=二元）。**重载解析的探针里实参个数决定选中哪个**：二元路径
  的探针带 1 个显式实参（+1 接收者），一元路径的探针**不带实参**。照抄二元
  路径的探针会把一元重载判成 arity 不匹配而落回内建算术。
- **一元运算不能复用内建算术的 codegen 路径**：内建一元负号是对**整型**做
  `neg`；落到类/结构体上就是对指针做 `sub` → LLVM 校验失败（"Integer
  arithmetic operators only work with integral types"）。类/结构体上的一元
  运算必须**先查重载**，没有重载就报人话诊断（fail-closed），不能默默当 0。
- **重载的接收者既可能是"值"也可能是"槽位指针"**（`new` 表达式返回槽位
  地址）：按值传参前要 load，否则把指针塞进调用里，LLVM 校验直接失败。
- **重载结果是"拥有型临时值"，必须在拥有型临时值登记表里登记**：表达式产出一个
  新对象时（调用、`new`、二元重载……），消费方（赋值、传参）要按转移/释放
  处理。没登记 → 赋值时不转移所有权 → 重载里 new 出来的对象漏一条引用
  （leakcheck 变体直接红）。**新增任何"表达式产出新对象"的路径，都要在这张
  表里登记。**
- **坑出处**：2026-10-20 修一元 `operator -` 重载被误编译成指针减法时踩到：
  重载调通了但漏一条引用——二元路径在拥有型临时值表里有登记，一元没有。
## 8. 内建转换特判必须按静态类型分流：「指针」不等于「字符串」

- **只有 `string` 能当 C 字符串读**：编译器内建的转换/格式化特判
  （`Convert.ToString` 等）里，"实参是指针" **不等于** "实参是字符串"。类实例、
  数组、委托、`object` 在 LLVM 层都是 `i8*`，跟 `string` 长得一模一样，只能靠
  **静态类型**（`infer_expr_type`）区分。
  - 坑出处：2026-10-20 实测 `Convert.ToString(<类实例>)` 会拿 `strlen` 去量
    对象指针——对象头里的指针字节被当成文本，一路扫到进程映像里的某个 0 字节
    为止：程序打印出了进程里的符号名（`PostQueuedCompletionStatus`），并以
    0x80000003 崩掉。**这是越界读 + 内存信息泄露**，不是"输出不好看"。
- **结构体不能落进整数格式化分支**：`Convert.ToString(<结构体>)` 此前落进
  `emit_itoa_into`，生成 `call i64 @__zan_itoa64(ptr %s, %struct.Pt %v, i32 0)`
  ——形参类型不匹配，LLVM 校验失败（"Call parameter type does not match function
  signature"），编译直接死在代码生成阶段。
- **动作**：内建转换/格式化特判一律先取实参的**静态类型**再分流：`string` →
  拷贝；数值 → 格式化；**类 / 结构体 / 数组 / 委托 → 报人话诊断
  （fail-closed）**，不要"猜一个能编译的路径"。
- **只在能证明类型不对时才拦**：静态类型推不出来（类型参数、泛型实例化等）
  时允许退回旧路径——否则 `Convert.ToString(T)` 这类泛型代码会被误伤。
- **`object` 是特例，只能放行**：它是带标签的引用，编译期看不出里面装的是
  字符串还是类实例；而 `object` 上**连通用 `ToString()` 都调不通**
  （"this call could not be resolved to any callable"），所以没有正确的
  实现可选。放行 `object` 的代价是它仍按 C 字符串读：里面装的是字符串时
  结果正确，装的是类实例时仍会越界读。治本要等 `object` 的通用 `ToString`
  或把 `Convert.ToString` 声明进 stdlib（见 TASKS.md A443 / A444）。
- **配套事实**：这类内建特判是**编译器自造的接口**——`stdlib/System/Convert.zan`
  里没有声明 `ToString`，`src/compiler/builtin_api.c` 的 `members_convert` 里
  也没有（IDE 符号索引 `zanc --emit-symbols` 因此补全不出 `Convert.ToString`）。
  公开面该住在 stdlib，别住在编译器里。
## 9. 内建数值 / 文本特判同样必须先判静态类型——`sitofp` 与 `atoi` 一样会咬人

- **数值特判不能"先转再问"**：`Math.Sqrt/Sin/Cos/Tan/Log/Exp/Atan/Asin/Acos/
  Abs/Max/Min/Pow/Floor/Ceiling/Round` 这族内建在 C 宿主里是"把实参转成
  double 再调 libm"。若转换发生在类型判定**之前**，类实例（LLVM 层是
  `i8*`）会被喂给 `sitofp i64` → 造出 `sitofp i64 i8*` → **LLVM 校验失败**
  （"LLVM verification failed"），用户拿到的是一句完全看不懂的话。
  - 坑出处：2026-10-21 实测 `Math.Sqrt(<类实例>)` 就是 "LLVM verification
    failed"；`Math.Max(<类实例>, 1)` 同样炸——**双参特判尤其危险**：两个参数
    必须**分别**判，写成 `!(isInt(a) && isInt(b))` 会让"一个指针 + 一个整数"
    整条溜过去。
- **文本解析型内建只接受字符串**：`int.Parse` / `double.Parse` / `long.Parse`
  / `...TryParse` 的实参必须是 `string`；传类实例时旧路径会拿对象的
  **首字节**当 C 文本去 `strtoll`（扫描很短所以不越界，但**静默返回 0**——
  比崩溃更难查）。`Convert.ToInt32/ToInt64/ToInt/ToDouble` 接受字符串**或**
  数值，判定口径不同，要写成**两个独立谓词**
  （`type_is_text_arg` / `type_is_textual_or_numeric`），不要复制成两份
  类型清单。
- **诊断文案里的 `%s` 必须配字符串指针**：`zan_diag_emit` 走 printf 语义，
  写 `'%s'` 却传 `(int)name.len` 会让 printf 把**长度当指针**解引用 →
  访问冲突（0xC0000005）。带长度的字符串一律用 `'%.*s'` + `(int)len, ptr`。
  - 坑出处：2026-10-21 自己写 `Convert.ToDouble` 诊断时踩到：`%s` 配 int 长度，
    编译器自己崩在诊断打印上。
- **跨后端必须同改**：`src/selfhost/irgen_expr.zan` 的 `ToDbl` /
  `GenMathCall` / `Convert.*` 是同一套逻辑的 Zan 实现，同样的输入会造同样的
  非法 IR（`ToDbl` 也走 `sitofp i64`，`Convert.ToInt32` 无条件
  `call i64 @atoi(i8* <ptr>)` → 运行期读对象字节当文本）。守卫要落在
  **分派入口**（`ToDbl`、`Convert.*`），不要只补某一个分支。

## 10. 源文件里的裸 NUL 字节会让 ripgrep 静默跳过整个文件

- **症状**：搜索某个函数时，ripgrep 对该文件返回 **0 命中**，或只吐一句
  "binary file matches" 而不给行号与上下文——看起来像"这个函数根本不存在"。
  根因是文件里混进了**裸 NUL（0x00）字节**，多半是手写注释时把 `\0`
  写成了真字节。
  - 坑出处：2026-10-21 在 `src/compiler/irgen_call.c` 的注释
    「无 \0 范围内的切片」里写进了真 NUL，ripgrep 随即把整个
    `irgen_call.c` 当二进制跳过——"这个守卫写在哪个文件"直接落空，
    一度让人以为守卫不存在。
- **动作**：注释里要写 NUL 就写**转义** `\0`（两个字符），不要写真字节；
  怀疑文件被当二进制时用 `rg -a` / `grep -a` 复核；写完源码后顺手扫一遍
  裸 NUL 再提交。

## 11. 调用实参的所有权：正常路径与异常路径必须用同一套判定

- **契约**：调用实参里的"拥有型临时量"（`new` 出来的对象、lambda 闭包、
  临时集合……）归**调用方**所有——调用正常返回后由调用方释放
  （`emit_release_owned_call_temp`），调用**抛异常**时由异常展开器释放
  （入栈的 EH 临时槽）。两条路径判定的是**同一批对象**：判定不同源，
  就会有一边漏放，表现为 `zanc --check-leaks` 报
  "captured local / closure" 或 "N object(s) still reachable at exit"。
- **坑**：早先实参的 EH 入栈判定要求 `infer_expr_type(arg)` 非空
  （`at && is_rc_managed_type(at) && … expr_yields_owned_rc_value(…)`），
  而**裸 lambda 没有自然类型**——推断出来是空的，于是 lambda 实参在异常
  路径上根本没入栈：调用一抛，闭包连同它捕获的局部（堆 cell）一起留在
  堆上。正常路径却认它（`emit_release_owned_call_temp` 先判
  `expr_yields_delegate_value`），两边判定不同源。
  - 坑出处：迁移 `Zan.Test` 的 fixture 框架时，`Fixture.Register<T>` 把
    闭包交给 `RegisterRaw`，后者在"重名注册"时抛 `FixtureException`——
    conformance 的 leak 轮当场报出 2 个对象泄漏（`captured local` +
    `closure`），最小复现只要 6 行：把捕获了局部变量的 lambda 传给一个
    会抛的方法即可。
- **动作**：判定收口到一个共用谓词（`call_arg_is_owned_temp`），正常路径与
  EH 路径都调它；委托值**不能**拿推断类型当前提（裸 lambda 推不出类型），
  要走 `expr_yields_delegate_value`。
- **委托实参的 EH 槽**：委托在 IR 里是函数指针，而展开器按 **i8ptr** 取回
  闭包指针再跑它的析构，所以入栈的槽必须是 i8ptr（位转换后存），槽种类用
  `ZAN_EH_SLOT_DLG`。存成函数指针类型会让展开器的 `load i8ptr` 类型不符
  （非法 IR）；存成 `ZAN_EH_SLOT_OBJ` 更糟——`zan_rt_release_dyn` 会把闭包
  头字当成 site 下标去查析构表，读坏内存。
- **展开顺序**：EH 临时栈是 LIFO。被捕获局部的 cell 由作用域绑定持有
  （`own_box_cell`，先入栈），闭包实参后入栈 → 展开时先放闭包（闭包析构
  释放它对 cell 的引用）再放 cell，计数刚好归零；反过来就会多释放一次。