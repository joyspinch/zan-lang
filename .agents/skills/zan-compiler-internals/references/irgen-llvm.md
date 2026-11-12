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
- **跨后端必须同改**：`src/selfhost/irgen/irgen_expr.zan` 的 `ToDbl` /
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
- **构造路径也是调用，实参同样要登记**：类构造（`irgen_expr.c` 的
  `emit_expr_new_expr`）与结构体构造（`irgen_stmt.c` 的字段初始化式）都是
  调用，实参里的拥有型临时量归调用方所有——构造函数一抛，就得由展开器放。
  早先这两条路径**只把对象压进 EH 临时栈，实参没登记**：
  `new Holder(Make(), true)`（`Make()` 返回 `List<int>`，构造抛异常）→
  `--check-leaks` 报 1 个 `List<int>` 泄漏；而同样形状的普通方法调用
  `Thrower(Make())` 干净——差别就是普通方法调用路径有
  `emit_call_arg_eh_push`，构造路径没有。
  - 修法与普通方法调用路径**同源**：逐实参 `emit_call_arg_eh_push`，
    调用后**逆序** `emit_eh_tmp_pop`（EH 临时栈 LIFO：实参后进先出，
    对象槽最后弹）。`emit_eh_tmp_pop` 只弹槽不释放，真正释放仍由正常路径的
    `emit_release_owned_call_temp` 负责——两条路径做的是同一批对象。
  - 坑出处：类路径靠"对象析构顺带释放字段"把结构体路径的同类问题盖住了
    ——同一个形状换个载体就漏，测的时候必须把类与结构体分开测。
- **属性 setter 的临时接收者只清理一次**（2026-11-11 trace + 反汇编实证）：
  `expr/expr_prop.c` 的 `emit_property_setter_call` 已释放 `obj_ast/recv`；
  `ops/ops_assign.c` 的 setter 分支不得再次释放。`owner.Child.Value = v`
  中 Child getter 只返回一个 +1 临时，重复 release 会把 owner 字段仍持有的
  对象归零；后续 getter 报 stale retain。普通字段直写不经过 setter helper，
  仍须调用侧清理临时接收者。`game_pipeline_suite` 的
  `second.Clock.TimeScale = 0.0` 曾命中，新增判定器
  `arc_property_setter_temp_receiver`（修复后验证由收口会话执行）。
- **bare 自定义 getter 的 owned 判定与显式成员访问同源**（2026-11-11 实证）：
  `AST_IDENTIFIER` 若实际解析为自定义 getter，其返回值与 `this.Prop` 一样按
  owned 临时量处理，不能因语法是裸标识符就归为 borrowed；先解析 local shadow，
  局部变量遮蔽属性时仍按局部变量处理，auto property 与普通 field 的 borrowed
  规则也须保留，不能把所有裸标识符一概改成 owned。
  `emit_property_getter_call` 的 `obj_ast` 必须描述传入 `recv` 对应的 receiver，
  隐式 this 访问使用 `AST_THIS_EXPR`，static getter 无 receiver；不可把属性
  结果 AST 当作 receiver AST 传入，否则临时量清理会释放宿主本身。
  集合属性返回的 owned 临时 receiver 进入 `Contains`、`IndexOf`、
  `LastIndexOf` 特化路径时，也须配对 own/drop：调用后消费临时集合的所有权，
  不得遗漏 drop，也不能释放 borrowed receiver。
  bare getter owned/receiver AST 与上述三个方法的 own/drop 已经 probe 验证；
  `arc_implicit_property_receiver` 严格 golden 比较及 `--check-leaks --arc-guard`
  均通过，原 16 个残留已消失。InputNumber lifecycle 的 11 项严格 golden
  及同组合检查亦通过；这些是 probe 结果，正式 CTest 与 IDE 验证结果仍待。
  无 `-g` 时两个分配点可能共用 site 编号，不能仅凭 site/行号归因；须用
  allocation/release trace 或 `-g` 确认实际来源。本次 trace 确认 Bundle 正常，
  旧 line 75 归因来自共用 site，泄漏实际为临时集合 receiver 漏释放。

## 12. Nullable decimal：类型表示、提升运算与 pointer ABI 同源（2026-11-11 实证）

- binder 值类型清单必须包含 `TYPE_DECIMAL`；`decimal?` 应构造为带 decimal
  payload 的 `TYPE_NULLABLE`，不能误作普通 decimal 加可空引用标记，否则 null
  初始化/赋值/返回、比较与算术结果推断会一起失配。
- lifted decimal 的五算术与比较必须复用现有 decimal pointer ABI；聚合 payload
  不能传给 `LLVMConstInt` 或整数算子。仅在双方 `HasValue` 分支求值 payload，
  再用 phi 合流；事后 select 不会抑制死臂的零除/溢出。比较保留双方 null 相等、
  单边 null 不等、关系比较遇 null 为 false 的契约。
- checker/result inference、混合整数提升、复合赋值、`++/--` 和一元 `+/-`
  必须采用同一 nullable decimal 口径；复用现有 ABI/包装，保持 place 单次求值、
  prefix 返回新值、postfix 返回旧值。回归：`selfhost_nullable_decimal_lifted`
  宿主 probe 已实测 60 个 true；同时检查普通 `int?` 与非 nullable decimal 路径。

## 13. Task 方法组：类型 owner、fresh closure 与虚分发（2026-11-11 宿主实证）

- 类型限定方法组（`Task.Run(C.Work)`）必须从 binder 的类型符号绑定候选 owner；
  识别 type receiver 后同时设置 owner，不能只改 receiver 分类。irgen 的 identifier
  表达式类型推断只查 local/field，类型名没有值类型，`expr_class_sym` 可返回空。
  坑出处：裸静态方法组已能发射，但自然写法 `Task.Run(TaskMethodGroupBinding.StaticWork)`
  仍报 E3064 unresolved Task.Run；补齐 bound type owner 后通过。保留 local/field 遮蔽。
- 新建 method-group closure 的初始 owner 必须显式交接给 Task 路径：frame retain
  后释放 fresh closure 初始引用，不能依赖原实参 AST 的通用临时量分类；裸方法名、
  静态方法组与实例方法组的分类不同。receiver 是调用/new 临时量时，闭包保活 receiver
  后还须释放 receiver 初始 owner。坑出处：仅补 frame retain 会让裸实例方法组连带
  receiver 留活；以自然裸/限定静态、裸实例、`Make().Work`、`new C().Work` 回归，
  并用弱引用检查任务完成后的 receiver 释放。
- 方法组 thunk 调用虚方法必须复用正常调用的虚分发路径（`emit_dispatch_call`），
  不能直接调用静态选中符号的 LLVM 函数；基类变量持有派生对象时，Task.Run/Spawn
  必须调用 override。坑出处：直接 `zan_call2(target)` 固定到基类实现；自然方法组
  回归覆盖基类引用上的 Run 与 Spawn，预期均为派生输出。
- 验证范围：宿主 `task_method_group_binding` 与 `task_action_owner` 各连续 8 次
  输出匹配，正式定向 CTest 2/2 PASS（6.42s）；7 个相关诊断用例此前 PASS。
  自举尚未完成 methods 方法组对拍，不将宿主证据外推为自举已验证。

## 14. Selfhost 短路表达式：私有临时列表必须在已求值路径闭合（2026-11-11 实证）

- 根因：GenBinary 的 &&/|| 创建私有 stmtTemps/stmtTempTypes，操作数中的
  EmitDelegateCallVal 会 retain callee 并 AddTemp；若合流后直接恢复外层列表，
  这些 owner 会被丢弃。数组 descriptor 与元素 dtor 即使正确，调用过的 delegate
  元素仍残留引用，WeakReference 在替换元素和数组销毁后仍报存活。
- 动作：左侧 GenCond 生成 i1 后，在发短路分支前 EmitTempReleases；右侧
  GenCond 生成 i1 后，在右侧出口、跳合流前 EmitTempReleases。仅释放实际求值
  路径的 owner；不得在未求值路径使用右侧 SSA。条件结果是独立 i1，释放载荷
  不改变条件。此法不依赖 eaActive，不能假设 async 也有 entry 临时槽。
- 列表与 phi 纪律：GenCond 本身不清临时列表；EmitTempReleases 末尾重建两表，
  所以左右两侧清理互不重复。bpre/bpost 必须在释放发射之后读取 curLbl，防止
  release helper 新增控制流后 phi 指向过时前驱。嵌套短路各闭合自己的列表，
  外层已有 owner 保留在保存的列表中。
- 验证证据：selfhost_shortcircuit_callee_owner 宿主与同一 gen1 均精确匹配
  10 行，覆盖 &&/|| 左右 callee、跳过 RHS、嵌套短路、calls=5 与清空最后
  delegate 后 payload 销毁；三个数组 ownership fixture 自举亦精确匹配。
  不将这些正常路径证据外推为异常展开或 allocator 故障注入已经验证。

## 15. Nullable 标量 rank、数组 slot stride 与 sbyte 输出（2026-11-11 实证）

- selfhost 的 nullable 标量判定必须同时要求 `ival == 0`：`T?[]` / `T?[,]` 是数组，不能只看元素名和 nullable 标记就进入标量 boxing。`IsNullableValue` 的 rank 门禁须供类型映射、存取与 ARC 分类共用；误把整个数组当 nullable 标量可造成错误 boxing、指针误解和堆损坏。
- 数组 slot stride 必须按实际存储表示决定，先判数组/nullable 再判元素名字：嵌套数组引用槽 8 字节，当前 selfhost nullable 标量 box 指针槽 8 字节，非 nullable byte/sbyte 才是 1 字节，非 nullable decimal 是 16 字节。不能让 `byte?` / `sbyte?` 依名字走紧凑字节槽；分配、descriptor、index load/store 和矩形数组路径必须采用同一 `ArrElemSize`。这些是 selfhost 表示，不可直接套成 host nullable 聚合布局。
- host nullable sbyte 的本轮错误在输出扩宽而非数组 stride：`Nullable.Value` 推断保留 TYPE_SBYTE，payload extract 得到正确 i8，但 `emit_widen_i64_for_print -> zan_iwiden` 对 <=8 位整数做 ZExt，使 -113/-7 变成 143/249。Console.WriteLine/Write 的整数出口利用表达式语义类型，对 TYPE_SBYTE 显式 SExt 到 i64；byte 保持原零扩展。LLVM i8 本身不携带 signedness，不能为修 sbyte 全局把 i8 扩宽改为 SExt；本次也未证明所有转换/格式化出口的 signedness 已闭合。
- 回归锚点：`selfhost_nullable_byte_array_stride` 覆盖 byte?/sbyte? 一维及矩形数组、null、替换、alias 与负值输出；nullable rank fixture 覆盖数组不能误进标量路径。主会话报告新增 3 组对拍 PASS、6 项 host CTest 全 PASS（7.27 秒）。nullable ARC 检查未开启：上述证据确认功能输出与本轮布局路径，不外推为 nullable box 引用计数/析构无泄漏或异常展开已验收。


## 16. 异步 GenVarDecl：捕获 cell 标准头与 lambda 目标签名（2026-11-11 实证）

- async `GenVarDecl` 的提前返回分支必须遵循普通局部声明的闭包契约：被捕获局部的 cell 统一由 `OwnedCell(pty0)` 创建，不能用裸 `malloc(TypeSizeBytes(...))`。初始化、默认 struct 初始化和其他默认初始化的 captured cell 均须覆盖；cell 的零初始化 payload 由标准分配器提供。
- 原因：closure environment 会 retain cell，lambda 访问与环境析构会 retain/release cell，async frame 的 spill 保存 cell 指针，正常退出与异常 frame cleanup 通过 `EmitCellRelease` 释放 cell。`zretain/zrelease` 读取 payload 前的标准 24 字节头 `[class id, refcount, destructor]`；裸 malloc payload 没有该头，不能混用。复用 `OwnedCell` 的 typed payload destructor，不能额外手工释放 payload 或给捕获再加一套所有权。
- async 局部的 lambda initializer 必须在 `GenExpr` 前按声明的 delegate 类型调用 `AnnotateLambda`，与普通 `GenVarDecl` 一致。否则 `Func<int>` 会生成错误的 `i8*` 返回 thunk，并把整数值 `inttoptr`；目标类型必须同时决定 thunk 定义、closure 函数指针和调用签名。
- 实证：旧 `closure_capture_async` 生成 IR 对 malloc(8) 的 captured x cell 调用 zretain/zrelease，并将三个 Func<int> lambda 生成 i8* 返回；运行退出 3221226505（0xc0000409），无 stdout。IR 契约违规已定位，未取异常栈，不将退出码单独当作根因证明。修后 IR 的 x cell 为 `zalloc(i64 8, ... @zowned_dtor_0, null)`，closure 函数指针及 `__lambda_0/2/3` 定义返回 i64，lambda body 为 `ret i64`。
- 验证边界：主会话报告修后 5 个定向用例真实 PASS（5/5），新 gen1 SHA256 为 `f224bc6d6e70963fd7405f6512a0a0f8cdc861245503ba45226e441c7a6e239b`，证据目录 `_scratch/production-array/closure-async-closeout-20261110T182512186`。这是 5 项定向收口，不是全生产 24 项重跑；不外推为所有 async/EH/cancellation 路径均无泄漏。本次只读复核新 IR，未额外构建或运行。
- 后续同源检查点：正式 fixedpoint 脚本直接执行 exit0，gen2/gen3 IR 均 28,474,342 字节且 SHA256 `b02b35372deb80cf02200f716b554341f0d71aeaa6dc387601a2540d99e64e75`；复用本轮 gen1（`1b6e49ea41a4818393c6073266b191a1e916478fbe31dab78085b63e89fd4770`）执行正式 production driver，24 行为 + 10 指定拒绝全部通过，输入前后 hash 未变。证据 `_scratch/production-array/fixedpoint-checkpoint-20261110T184252495`。均为 direct script/driver，不能写成 CTest 或 full release PASS；不覆盖后续源码变动。

## 17. Async pending return：撤销 owner 与最终结果缓存必须同源（2026-11-11 实证）

- Selfhost 返回值先 retain 并写 frame slot6，再执行可挂起 finally；finally 逃逸异常、下一次 resume 取消或 frame cleanup 必须 load → clear → 按声明类型 release。helper 显式传 retTy，独立 cleanup 生成前保存类型；正常 completion 将 owner 转给 awaiter，不能在每次 throw 时撤销，finally 内部 catch 后仍可能继续返回。替换 return 先 retain 新值再释放旧值，允许两者别名。
- 宿主 typed RVAL 还兼作 pending return 缓存。pending discard 已释放 owner 后，RVAL 可能残留旧指针；orphan 清理必须以最终 RESULT 是否交付值为准，先清 RVAL，指针结果从 RESULT 解码，聚合只在实际交付时按类型释放。不得用旧 RVAL 再 release，也不得关 arcguard 掩盖错误。
- 实证：selfhost_async_pending_result_owner 的旧 gen1 异常及取消两处 WeakReference 错误为 true；修后四路径（正常跨 Yield、finally 内部 catch、异常终结、取消）均匹配 golden。修后 gen1 c7614f9ab367a98983898b7b5e05ecd0a3d8ad07fbafec11145c49e9af7f0454，另 3 项 focused 通过。宿主普通 probe 虽绿，正式 CTest arcguard 首次报 stale release；修 RVAL/RESULT 后原 fixture 与 async_spawn_result_release 的 arcguard+check-leaks probe 和两条正式 CTest 均通过（4.79 秒）。
- 边界：独立 cleanup callback 与 finally 第二次 return 只有生成/链接覆盖；最新改动未重跑 fixedpoint 或完整 production driver。前一 checkpoint 的 fixedpoint+24行为+10拒绝通过不可冒充本次新源码结果。证据 _scratch/async-pending-owner、docs/PRODUCTION_ACCEPTANCE.md。

## 18. 同步 pending return：handler 水位与 finally 外层 EH（2026-11-11 实证）

- 返回先求值及retain，再运行finally；预留给调用方的owner必须登记。selfhost仅同步return建立entry栈record，记录typed payload及destructor；handler武装时保存pending链水位，longjmp前摘链清槽并按类型展开。正常return摘链不release；finally内部catch不得撤销原返回，替代return须先保活新值再撤销旧值。跨函数Fail不能靠EmitExitFins之后的term判断补救。
- 全部handler（含async trampoline）保存同步链水位；不能让协程pending record跨挂起留在栈上。lambda、task action和函数入口需保存/清空/恢复编译期pending状态。复用OwnedPayloadDtor类型化释放，object字符串边界不能统一当class。此协议只登记返回owner，不代表一般同步局部变量异常展开已完成。
- 宿主同步pending槽必须登记现有owned local清理，正常交付清槽；执行离开的finally先切外层landingpad/EH上下文，并保存恢复嵌套栈条目。finally-only landingpad若同函数有外catch，原生phase1需可见catch clause，否则直接跳帧，不能靠cleanup阶段再补catch。
- 实证：旧selfhost五处class释放失败及delegate/typed-array异常撤销失败；host还有重复finally和丢catch。根修后两新fixture（23行/8行）及6邻近selfhost共8项emit/link/run全MATCH；host两新+exception_finally_paths+exception_finally_managed_owner正式CTest全PASS，4.90秒。邻近SameFrame没有原host基线，不声称本改动引入。证据_scratch/pending-return及docs/PRODUCTION_ACCEPTANCE.md。
- 最新同步修复尚未重跑fixedpoint/27项生产driver；此前checkpoint固定点28,476,832字节一致、25行为+10拒绝全绿不能外推本次新源码。

## 19. 同步 finally 内嵌套异常不能覆盖正在传播的异常

- 原异常的对象和类型必须一起保存。同步 throw、裸重抛、catch 类型不匹配后的传播在运行内联 finally 后，只有控制流正常返回才发布原异常；finally 逃逸的新异常继续传播。不要把同步 SSA 保存方式用于可能挂起的 async 路径。
- 实证：selfhost_sync_finally_payload 旧 gen1 三次进入错误的外层 handler，新 gen1 与两个 pending-return 回归均匹配预期。另有独立探针证明普通同步局部在同帧和跨帧异常时仍漏释放；异常派发修复不等于 ARC 保活或局部 owner 展开。证据见 docs/PRODUCTION_ACCEPTANCE.md 与 _scratch/sync-owner-eh-review。


## 20. 失败的 await 必须沿最内层异常清理入口展开

- `emit_async_check_sub_exc` 从子帧接管异常、清空原 owner 并释放帧后，
  按当前词法作用域进入最内层 landing pad；只含 `finally` 的块同样是入口。
  该块完成后，异步传播继续进入尚未清理的外层入口。
- 显式 `throw` 路径已用 `emit_finallys_left_by_throw` 发射清理，随后走只找
  catch 的入口。不要把两个入口混用，否则会跳过清理或重复执行。
- `async_await_exception_finally` 验证子协程立即失败和挂起后失败时，两层
  `finally` 各执行一次、顺序正确且异常载荷完整。网络回归还必须检查套接字
  关闭和池许可归还；收到预期异常本身不能证明清理已发生。

### ANF 跨 await 暂存接收者时必须区分静态类型名与实例值

- 实例接收者必须在含 await 的实参求值前暂存，保持原接收者及语言求值顺序；静态类型名不能提升成值。原生内建静态类型可能没有普通 binder 声明，应在没有 binder 符号且没有已解析用户调用符号时查询 `zan_builtin_types` 的 `is_static` 元数据，不能只凭名字或缺失符号猜测。
- 同名局部实例仍须按实例处理。回归同时覆盖 `Console.WriteLine(await Number())` 和局部 `Sink Console`：await 实参将该变量改为 replacement 时，本次调用仍写入 original，下一次调用才使用 replacement。
- 2026-11-11：`case_async_builtin_static_receiver` 与未改写的 `case_async_generic_method` 定向通过（2/2，2.81 秒）；前者完整输出为 `42`、`original:7`、`replacement:9`。这证明本次静态接收者分类与实例求值顺序，不代表所有异步降级路径已验收。

## 21. 泛型异步帧扫描与恢复体必须使用同一组类型绑定

- `emit_async.c` 的 `async_scan_stmt` 之前，安装当前 work item 的方法类型形参、实参和接收者实例（`w->mtps`、`w->mbind`、`w->cur_inst`）；扫描结束恢复调用者环境。帧槽类型和 ARC owner 分类必须与写入该槽的 resume 体一致。
- 扫描留在开放泛型环境时，恢复体仍可能正确写入具体引用值，却没有为该槽生成释放路径。典型形状是泛型转发器 `return await factory.Open()`，以及等价的命名局部与 `try/finally` 形状；返回值正确不代表 owner 已回收。
- 用 `case_async_generic_frame_owner` 检查结果与泄漏。独立探针使用未优化构建配合 `--check-leaks --arc-guard --strict-runtime`，三种形状都应返回 42 且无运行时诊断。


## 22. 异常态 finally 的原异常拥有者与离开入口（2026-11-11 实证）

同步 finally 暂存异常时必须接管 owned 引用：清空活动 dispatch，正常结束交回并撤销暂存槽；return、向外跳转或逃出的新异常必须释放被替换的拥有引用。内部 catch 不得释放外层暂存拥有者；同一对象的两份引用也必须独立记账。复用内部 owned local 与 landing-pad cleanup，槽和拥有标记先清空再动态析构。前向 goto 的源局部快照必须具有 arena 生命周期，内联 finally 的内部标签需有独立 scope 身份。

同步/异步内联 finally 执行时都必须隐藏正在离开的 try landing pad，否则 helper/await 子任务抛出会重入已执行的 finally。协程根入口必须先设 fin_idx=-1，再增加 lpad_depth；根入口不属于可隐藏的 try。验证同时覆盖显式 throw、抛异常的 helper、嵌套处理、同对象替换、向内/向外跳转以及 Yield 前后由 awaiter 捕获异常，并分别检查完整输出、ARC guard 与泄漏报告。进程退出 0 不能替代泄漏检查。

### await 的 async 分类必须覆盖扩展方法

- `await_target_is_async` 在没有适用实例/接口方法后调用共享 `find_extension_method`，按选中声明的 `MOD_ASYNC` 决定取帧。泛型 async 扩展的静态返回类型仍是结果类型，不能依赖 Task 类型或假定 checker 已填 resolved_sym。
- 缺扩展分类时，`await executor.ReadAsync<Entity>()` 曾落到同步 emit_expr，挂起后的返回值变成空白/垃圾整数；加分类后同步根驱动与 async 嵌套等待均返回42。判定器：`case_async_generic_extension` 和真实 SQLite `case_orm_executor_extensions`；保留同步扩展 await 的直返语义。
- 自举后端单独依赖 `ExprVal.asyncCall`（EmitAwait）；`EmitExtensionCall` 对 async 方法必须发 `i8*` 帧返回签名，并用 `AsyncFrame` 携带替换后的结果类型，不能登记为普通 ARC 临时。`PickExtension` 同样先拒绝显式泛型元数不符候选。修前同一用例输出 -1/-1/垃圾整数；局部修复并按 canonical 清单重建 gen1 后，单案差分 PASS（1 passed, 0 gaps, 0 failures），已登记 diff_cases。


## 23. 自举同步异常清理入口与线程隔离（2026-11-11 实证）

同步显式 throw、callee/guard 抛出应统一进入 try landing；catch 的同级 finally 需要不重新匹配 sibling catch 的清理入口。锁异常出口也需 landing，并在发射 monitor_exit 前退休该路径的 monitor 槽，避免外层 finally 改写退出时重复解锁；编译期状态随后恢复供其他 CFG 路径生成。用第二个原生线程取得同一把锁来证明释放，单看外层 catch 输出不够。

自举五个 EH 全局与两个 pending-return 全局必须一致使用 thread_local；异常缓冲保留对齐。协程每次 resume/rearm 取当前线程的新深度与 watermark，挂起时恢复入口深度，不能跨挂起保留本机栈记录。同步 landing 改造不代表异常 payload 已有 ARC 所有权；嵌套取消 pending return、向外跳出 finally、异步异常身份需要独立验证。对拍退出 0 仍可能漏 finally；用完整语义输出、旧新同源探针和固定点分别取证。

## 24. 同步 pending-return 的作用域、水位与 finally 索引复用（2026-11-11 实证）

替换返回 B 尚未提交时，外层返回 A 的记录必须保留；B 的 finally 抛出若被 A 内部 handler 捕获，仍须返回 A。只有标记 A 的 handler 全部退休后，才能在仍武装的 B 下摘除并释放 A，同时修复前驱链接。不能用空指针守卫掩盖失效 watermark。向外 break/continue/goto 按目标的作用域祖先取消退出的 owner，内部跳转保留；switch 只改 break 目标，不能覆盖循环 continue 目标。

记录只关联返回起源的 finally 边界；发射 finally 本体时清空 ambient syncPendingReturn，避免嵌套非 return 清理把同一记录登记到第二个边界。隐藏已完成 finally 尾部时，bodies/scopes/live/monitor 与 finActive 的数值索引必须一起保存、过滤及恢复，否则新 try 复用隐藏索引后被误认为 active，finally 静默漏执行。lambda/task 原生函数发码需隔离并恢复 pending、作用域及 clone 标签状态。

实证：原 nested probe 访问冲突 3221225477，修后 inner/1；break probe 从 2/true 变为 2/false。中间版索引碰撞 probe 退出 0 却只输出 3，修后 observe/3。最终 sync_pending_scope 的 80 行输出与弱生命周期全部匹配，8 个既有控制通过，独立 runner 两次通过；证据 _scratch/sync-exception-landing/pending-after-20261111T103048184。此结论不覆盖异步异常身份、完整 payload ARC 或普通局部异常展开。

## 25. 异步异常身份与 named catch binding 独立拥有（2026-11-11 实证）

待传播 value/type 与 bare rethrow 的不可变快照必须按嵌套活动范围保存到协程帧，不能让 entry alloca 或线程 dispatch 充当跨挂起存储。finally 正常完成恢复原 pair，replacement/return 终止不恢复。显式 throw、callee 和 failed await 统一走 cleanup landing；catch 清理不得重新匹配 sibling catch。通用 spill 可被 foreach 追加，快照撤销必须按注册身份移除，不能盲删末尾。

named binding 与原异常快照分开：初始化取得自身引用；正常、异常、return 和向外 goto 全部归还。async 先清 local/frame 再 typed/cell release，避免后续 spill 复活旧 owner；捕获变量遵守统一 cell 布局。goto 按目标 catch 范围判断，内部跳转保留，嵌套跳回外 catch 只释放离开的内层。同步 binding 同样不能借用初始化却在退出时释放。

实证：原 async probe 退出0却覆盖原异常，修后 nested/original/nested/original。中间版正确传播后暴露同步 named catch 消耗 static 异常引用，连续 replacement 发生堆损坏；binding 独立 retain 并补全出口后自然矩阵通过。identity 45行覆盖实例标记、挂起、bare、替换、cell及weak出口；自然fixture重复同一A/B且无额外keepalive。锚点identity通过不等于payload生命周期通过；中间版CTest不能冒充最终实现证据。完整异常payload协议和普通local unwind仍开放，详细身份/固定点见docs/PRODUCTION_ACCEPTANCE.md。

## 26. 自举异常记录转移与消费式 Task.Result（2026-11-11 实证）

异常是否活动必须由 record 的 active 状态决定，不能用 payload 指针非零替代。take 转移后清空来源，clone 必须取得独立拥有引用；已有 record 应直接转移，不能复用旧 owned 标记重新 publish。跨 catch/finally 与 resume 的 owner 链和 handler watermark 必须同时保持有效。

failed await 与消费式 Task<T>.Result 共用 EmitAwaitCheckExc：先检查 frame slot17 active，take 到 dispatch、清空旧 value/type 镜像，恢复调用者异常上下文，再用 zexc_frame_free 释放被消费帧，最后 raise。成功分支读取结果后释放一次；decimal 两字必须在释放前取出。Result 等待完成不代表成功，不能未查错误就读 slot6；每 handle 单次消费，不以重复读取要求偷换所有权协议。

实证：旧 Result 对立即失败与 Yield 后失败均返回0、跳过 catch 且 weak 仍活；修后 Wait 不消费异常、catch 内存活、catch 后死亡，成功值保持正确。exception_payload_ownership 的130行与其余三份异常 fixture 由同一 fresh gen1 验证，正式 selfhost_exception_ownership 单条通过。弱引用观察不得加强引用锚点；字符串内容/NUL/逃逸通过不代表完整分配平衡。runner 应删旧产物/通过标记，检查 LLVM、精确输出、退出码和 stderr，并对构建至子测试结束的已记录输入作前后身份比较；自动 stdlib 依赖未入清单时不得声称完整依赖闭包。固定点证明和旧版三项门结果不得冒充后续改动的固定点证据。

成功协程的终态完成也必须恢复本次 resume 入口保存的异常深度：EmitAsyncComplete 在 owner 清理与恢复后，将 asyncEhDepth 写回 __exc_sp 再 ret；挂起路径退栈不覆盖正常完成路径。否则调用方预先安装的 catch 被已返回 resume 的 trampoline 盖住，成功 await/Result 后直接 throw 会访问冲突或堆损坏。必须把 caller try 装在成功任务之前再 throw；成功之后才装新 try 会掩盖缺陷。立即/Yield×同步/异步调用×await/Result 八形旧六红两绿（async await rearm 掩盖），修后八形全绿。此例说明静态疑点需要精确控制 handler 安装顺序的实证，旧绿探针的排列不等于反证。

## 27. 自举普通局部的异常清理是 typed slot 协议（2026-11-11 实证）

局部拥有值在 materialize 后登记到统一异常 owner 链，kind3 保存 typed slot 地址和析构函数，不能保存旧 value 快照。entry 先置 null；normal exit 先 retire、清槽再 release；unwind 先摘链清槽再析构。cell 的独立 ownerSlot 也必须 entry null，不能依赖不属于 locReg 的 EA 自动初始化。异步登记必须指向稳定 frame 槽，不能指向本次 resume 的 alloca；save/reload 不得恢复已清掉的拥有值。captured await 声明要先创建 cell，再将结果写 payload，frame 槽保存 cell 指针。

所有拥有范围都要闭合，不能只检查 Block/catch：query range 每次覆盖 retain 新值并 release 旧值，结尾 retire；pattern guard false 释放绑定；switch expression 引用结果先独立保活，phi 默认值用 ZeroConst 而非仅识别 i8*。loop header、foreach 元素和 switch section 绑定均是独立拥有范围。内部跳转需按目标范围退出清理；源语言是否允许该 goto 另由宿主约束决定，不能把修好清理机制当成扩大语言许可。宿主前向 owning goto 的 E3083 必须在自举保持一致。

实证：third gen1 `ebe2ded3d5aa19596d3f47785bc030f3ef2a0fab8f6378a79ca2ad13ce333f4e` 的 46 行扩展诊断在 LLVM、exact stdout、stderr 和记录源身份检查下通过；随后 host oracle 证明其中 owning goto 应拒绝，已拆分为正例与 reject 回归，不能把原 46 行称作语言差分通过。string async 返回临时槽先 clear 再 release 后，durable 分配从 18/17 变为 18/18，11组总计134/134；临时实参在 await 挂起前的独立清理仍需下一版验证。

### await 拥有型字符串必须同时通过字符串分类（2026-11-12 实证）

- `expr_yields_owned_rc_value` 已将 `AST_AWAIT_EXPR` 归为 owned；`is_string_expr` 也必须按 `infer_expr_type` 的 `TYPE_STRING` 识别其结果，字符串比较与拼接才能消费这份拥有引用。仅补所有权分类不足以触发字符串运算路径的释放。
- 探针必须让 async producer 返回动态分配字符串；字面量的 immortal 引用计数会掩盖漏释放。修前三次短路 await 比较残留三个对象，修后 IR 比较后有对应释放，动态探针及 `--check-leaks --arc-guard` 通过。回归：`case_async_await_string_operator`、`case_mvc_session_revocation`。

### lazy RHS 的 ANF 必须在守卫内正规化（2026-11-12 实证）

- `&&` / `||` 的右支含 await 时，用帧内结果局部和条件守卫承载右支的完整 ANF；提升语句留在守卫体内，不能提前执行死臂，也不能把原始 await 留在二元运算中导致挂起前 SSA 值在恢复入口失去支配关系。
- 普通 `AST_BINARY` 的右操作数含 await 时，先求值并将左值快照到帧内局部，再正规化右支，保持左到右求值及异步修改来源前的原值。`AST_ASSIGNMENT` 的左侧是存储位置，必须排除此值快照转换。
- 回归：`case_async_await_string_operator` 覆盖 await 在左右及两侧、右支修改来源后的左值快照、live producer 调用次数与死臂；`case_async_lazy_call_args` 覆盖嵌套 lazy 表达式及接收者/实参顺序。两条正式定向用例的 golden、泄漏与 ARC 检查均通过；不外推为所有异步路径已验证。

## 28. 同步 finally 的选中 handler 传播与终止清理（2026-11-12 Windows 实证）

- finally-only 的 catch-all 可成为原生选中 handler。landingpad selector 为 0 时沿 cleanup resume；非零时必须保留原生 payload 并继续向外搜索 handler。Windows GNU SEH 上对选中 handler 发射 `LLVMBuildResume` 会回到同一帧，重复 finally 与 ARC release；`_Unwind_Resume_or_Rethrow` 在终止传播时还会直接 abort。当前选中路径用 `_Unwind_RaiseException`，返回表示传播失败（END_OF_STACK 或 unwind error），再进入已安装的 terminate handler。
- 当前函数没有外层 landingpad 不能推出调用者没有 catch；不能据此直接终止或仅特判 Main。回归必须包含 callee finally 后由 caller catch 接住，以及同函数精确匹配与不匹配 catch。
- terminate 必须先打印异常，再清空 TLS owned/exc/tid 后按原类型释放 Zan owner，避免递归终止时二次释放；原生 payload 不得当作 Zan 对象释放。随后复用正常 Main 的静态字段清理。terminate 函数体在 Main 完成静态字段发现后发射，跨函数插入须保存恢复 builder、current_fn 和 debug location。
- `sync_finally_terminal_unwind` 的直接探针以 `--arc-guard --check-leaks` 验证通过：父进程精确 PASS/exit0；finally-only、不匹配 catch、foreign native throw 三个子进程均 exit1，finally 一次且有预期诊断，捕获输出不得隐藏 ARC、leak 或 runtime error。测试用自有 fixture 根与重复进入退出码守卫；不能只依赖 Process.CaptureSafe 的 timeout。此证据限定 Windows，其他目标仍需各自验证。

## 29. 混合整数与 nint 必须保留原生宽度（2026-11-12 Windows/Linux x64 实证）

- checker、IR 类型推断与 selfhost 的数值/条件表达式提升必须一致：`long` 之后、窄整数之前保留 `nint`，并覆盖两种操作数顺序。漏判会把 `int * nint` 推成 int，使正确的 int32 回绕截掉指针高位；指针解码或 Process 捕获中的复制故障应先追值与原生指令，不能交换业务操作数绕行。
- 用高 32 位非零的加减乘、比较、嵌套表达式、混合条件表达式和 long 对照回归；`nint_mixed_arithmetic` 与完整 `sync_finally_terminal_unwind` 在 Windows/Linux x64 严格直接探针均 exit0、golden 匹配、stderr 空。保持 long 优先级、目标原生位宽和 int32 回绕规则；此证据不等于 32 位目标或 selfhost bootstrap 已验证。
