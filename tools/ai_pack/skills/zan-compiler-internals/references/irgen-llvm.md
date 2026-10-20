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
