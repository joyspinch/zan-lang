---
name: zan-compiler-internals
description: zanc 编译器内部（parser/checker/irgen/runtime/ABI）设计契约与底层不变量规范。修改 src/compiler/*、src/runtime/*、交叉目标或 conformance 测试时使用。仓库内有同名项目级版本，会自动优先。
---

# zanc 编译器内部设计契约与底层不变量

定位：`src/compiler/` 与 `src/runtime/` 开发与维护的架构规范与底层契约。
详细实现细节见各专题：
- 前端解析与语义绑定：[frontend-binder.md](references/frontend-binder.md)
- LLVM 指令生成与向量优化：[irgen-llvm.md](references/irgen-llvm.md)
- 运行时数据布局与 ARC 模型：[runtime-abi.md](references/runtime-abi.md)

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
7. **子协程 resume 入口在调用点必须"具名符号 + 帧头回退"双路可取**：
   - 动作：任何在调用点需要子协程 resume 入口的下沉路径（`await` 挂起、
     `Task.Spawn`/`Task.Run` 脱钩、语句级 fire-and-forget）都必须先按
     `<callee>$resume` 查具名函数；查不到时（间接调用——接口/虚方法/委托分发，
     被调入口不是静态符号）回退到从协程帧头 `ASYNC_FRAME_SELF_STEP` 槽加载；
     两条都失败才允许放弃。
   - 理由：间接调用按名字必然查不到 → `Task.Spawn` 静默返回 0 句柄、协程永不
     入调度器；而 `__zan_co_isdone` 对不在活协程表里的帧返回 1，于是
     `Task.IsDone` 报"已完成"，业务代码永远等不到副作用，症状与业务逻辑 bug
     无法区分。await 路径早有 SELF_STEP 回退（`irgen_expr.c` 间接 await 分支），
     detach 路径漏掉即是此坑（`Task.Spawn` 的 `emit_detach_async_call`）。
   - 边界：回退只对协程帧指针有效；ramp 必然在任何调度之前把 resume 写进
     SELF_STEP，故加载安全。单实现者会被去虚拟化成直接调用从而掩盖该缺陷，
     回归用例必须声明 ≥2 个实现者以强制走间接分发。

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
   - 理由：运行时 `__zan_co_frame_free` 的幂等守卫读帧首 8 字节的 `CO_DEAD` 位，但小对象分配器回收时把用户块首 8 字节写成 freelist 指针，守卫读到的已是垃圾 → 二次释放直接 double free（进程 abort）。实例：lambda 形态的 `Task.Run` 走 action 帧并自 reap，用户再读 `.Result`/`.Wait()` 又 reap 一次（详见 TASKS.md A379）。
   - 边界：运行时的守卫位不要放在分配器会回写的槽位（帧首 8 字节）；若要让 reap 幂等，标记必须落在分配器不覆盖的字段上。

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

