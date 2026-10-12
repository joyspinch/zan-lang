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
