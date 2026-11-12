# 运行时数据布局与 ARC 内存模型契约

## 1. 内建 Dict 内存布局契约

- **8 字段数据布局**：
  `{i64 count, i64 capacity, i8** keys, i64* values, i64* index, i64 index_capacity, i64 indexed_count, ...}`。
  - `keys` 和 `values` 是保持插入顺序的并行缓冲区。枚举顺序和 ARC 内存释放顺序均依赖此顺序。
  - `index` 是开地址哈希索引表（存储 entry + 1，0 表示空槽位），由 `__zan_dict_find` 惰性重建。重建判据为 `indexed_count != count` 或 `index_capacity == 0`。
- **Remove 操作严格保插入序**：
  移除元素时严禁使用 swap-remove（将末项搬移至空位），否则会破坏遍历顺序并违反语义规范。正确做法是使用 memmove 整体向前搬移空位之上的所有条目，并将索引表整体置为失效（`indexed_count = 0`），由下一次查找触发重建。
- **尾部槽位清零**：
  在容量收缩或移除元素后，尾部槽位必须清零（memset 为 NULL/0），防止 ARC 遍历时访问到脏指针。

## 2. ARC 所有权与借用契约

- **Binding.target 的显式写入与自动绑定共用借用 ABI**（2026-11-12 严格验证）：该 object 槽析构时有意跳过 target，写入必须既不 retain 新借用，也不 release 旧借用；普通同名 object 字段仍走强所有权。owner 必须活过 Get/Set，该槽不提供 tracked weak 自动置 null，owner 结束后只可清空或替换旧借用。owned 临时 RHS 必须 E3105 拒绝；`Binding<object> snapshot = binding.target` 生成常量快照，不能合成可写借用访问器让 Set(临时对象) 绕过限制。原泛型委托正例曾功能通过但退出泄漏一个 model；统一 symbol-aware member store 后，原探针、`generic_delegate_field_assignment`、`binding_target_borrowed`、`binding_target_lifetime` strict/leak/ARC 全绿，E3105 三处临时赋值及 E2098 错误委托仍准确拒绝。不能删析构 skip 或把 object 标 weak 来掩盖协议不对称。
- **自动 Binding 的访问器沿用普通属性调用契约**：可写实例字段与 get/set 属性生成 live，getter-only/init-only 属性、readonly 字段及 owned 临时接收者生成快照。属性不能按字段槽 load/store；合成 helper 复用 getter/setter 分派，借用接收者 AST 不能误释放 owner，getter owned 结果不重复 retain。cache 同时区分声明、具体 receiver/value 类型与 base 直派；保留 base 标记，不能因缓存创建顺序调用 override。辅助函数发码用 builder save/restore 隔离调试位置，并显式恢复泛型和类型上下文。回归应包含两种 base/virtual 创建顺序、不同泛型实例、访问器副作用及 -g strict/leak/ARC；仅 IsLive 或字段对照通过不足以证明属性绑定正确。
- **DllImport 外部字符串借用规则**：
  从标准 C FFI（`DllImport`）返回的字符串默认为借用指针，由调用方或 C 库自身管理生命周期，Zan 运行时的 ARC 机制永不主动释放该指针，也不应进行危险越界下标。
- **内建函数与借用白名单**：
  内建函数（Builtin API）默认接管或生产受控对象。若内建函数返回受控或借用字符串，必须显式在借用白名单中登记。新增内建 API 必须同步检查所有权与释放行为，防止漏释放或重复释放。
- **类型限定的静态读取先于同名实例接收者的所有权推断**：`Type.Field` 发码若命中类型上的静态字段，所有权分类必须返回借用；不能把继承的同名弱字段或自定义 getter 当作已求值的 owned 接收者。静态自定义 getter 的结果仍为 owned，局部变量遮蔽类型名仍走实例语义。实证：Form 保存 `App.frameApp` 缺 retain、退出释放旧 App；同名弱字段最小探针复现，修正分类后 Form 正常/异常路径与 ARC 探针通过。回归：`tests/conformance/arc_static_member_shadow.zan`。
- **引用型 `as` 继承源表达式所有权**：`AST_AS_EXPR` 的 owned/borrowed 分类须递归使用操作数分类；成功时把 owned 源的引用转交结果，运行时检查失败时仅释放 owned 源，借用源不释放。弱字段读取会提升强引用，遗漏该规则会在成功局部初始化时多 retain，失败返回 null 时又遗留源引用。ColorPicker 通道提交曾由 `sender.parent as ColorPalettePanel` 暴露此问题；回归覆盖弱字段、调用临时值的成功/失败与借用局部：`tests/conformance/arc_as_owned_operand.zan`。
- **`is` 的 bool 结果不继承源对象所有权**：无 pattern 时，类型/null 判定完成后用 `emit_release_owned_call_temp` 回收 owned call/getter/indexer 源，借用局部不释放；外层接收 bool 的调用无法补清理。pattern 命中转移 owned 源、借用源 retain，未命中清空绑定槽并释放 owned 源；绑定用 `arc_own_local` 接入既有 EH。槽须 entry 初始化 null，防短路未执行却进入清理；IF 条件绑定须在 true/false 两边各自清理（含无 else、pattern 命中而后续 && 为 false），发射 then 清理会修改 locals，false 边需保留原条件绑定状态。纯类 `Check(Make() is Derived)` 曾 run0 但泄漏一个 Make 对象；回归 `case_arc_is_owned_operand` 覆盖 call/getter/indexer、命中/失败/无 else、借用源置 null 后绑定仍存活、短路、条件 EH 与 body EH。不能把自然 `is` 改写成局部变量绕过，也不能给 pattern 无条件 release。
- **循环条件绑定与跳转清理必须覆盖后续 EH**：while/for 的 false 边须释放条件中新建的 owned pattern，不能等 body 清理后再从已截断的 locals 恢复；可先发独立 false 清理块，保持编译期 locals 供 body 发码。`emit_release_owned_locals_range` 不截断 locals，但普通 ARC 槽释放前必须清空，因 break/continue 后外层 landing pad 仍可能登记并读取同槽。纯类 normal/strict 红探针曾 while/for false 合计泄漏 2 对象，break/continue 后再抛异常均 stale-reference release；根修后全部干净，`case_arc_is_owned_operand` 扩展覆盖 false/回边/短路/跳转后 EH 与返回 bool/pattern 转交。只测正常 if 或只测 break 出口不能证明槽已解除所有权。
- **内建泛型集合的运行时身份必须保留全部实参**：`List<T>` 与 `Dict<K,V>` 的 `is`、`as`、引用强转查询名，须与 ARC site 表及 descriptor 表共用完整类型名；不能只取 `tt->name` 或写死 `List`/`Dict`。复用 `render_type_full` 递归包含 namespace、嵌套泛型、数组与 nullable；Dict/Dictionary 别名归一到 binder 的 Dict。List site 保存元素 T，Dict site 已保存完整 K,V，沿用现有记录即可，无需改对象 ABI。`--check-leaks` 走旧 site 表，普通编译走 descriptor，两模式必须验证。纯语言红证据曾使 double/bool/string 三字典的九项 `is` 全真，误导模型类型分派；修后只对角匹配，回归 `case_runtime_generic_collection_identity` 覆盖 key/value、List、嵌套、namespace 同名、null、is/as、owned/borrowed 与写回，`case_runtime_type_test` 的异型 List 预期须为 false。
- **属性调用的清理 AST 必须描述实际接收者**：裸名 `Prop = value` 调用 setter 时，接收者是借用的 `this`，须传 `AST_THIS_EXPR` 给接收者清理，不能传属性左值 AST；后者按自定义 getter 返回值判为 owned，会把没有 retain 的 `this` 提前 release。显式 owned 临时接收者仍按原表达式释放。Layer.SetBody 的纯类探针在严格 ARC 下曾于 setter 后释放宿主，修正后退出零泄漏且 IR 无该 release。判定器：`case_arc_implicit_setter_receiver`。
- **`List.Clear` 的 owned 接收者必须在清零计数后释放**：getter 或方法返回的 List 是 +1 临时，内建循环清空元素不消费这个引用；沿用 `emit_intrinsic_own_recv`/`emit_intrinsic_drop_recv`，在求值后登记 EH、正常出口清零 count 后 pop 并释放。局部/字段等借用接收者不释放。ScrollColumn 的 `column.Children.Clear()` 曾仅剩一个 List 泄漏；纯类复现同样 1 个，修后严格 ARC 干净。判定器：`case_arc_list_clear_receiver`。
- **Windows TLS 的 SECREL 偏移不能带 ASLR 基址重定位**：GNU ld 2.36.1 曾把 TLS 位移 8 也登记为 `HIGHLOW`，落盘反汇编正确、加载后被 image delta 改坏，`release_dyn` 在读递归深度时崩溃。Windows 原生 EXE 必须使用 bundled LLD，缺失时明确失败；clang 备用链接也须显式指定同一 LLD，不能回落 GNU ld。不能退回进程全局深度或关闭 ASLR。LLD 的 `--wrap` 还会改写 CRT import thunk，令 `__real_malloc` 再进包装器；Windows allocator 内部四函数必须同源直达 `msvcrt.dll`，用不分配的 `InitOnceExecuteOnce` 发布，其他平台保持原 linker ABI。判定器：`case_arc_release_tls_static_mutex`、静态 Mutex+Clear 严格探针、PE `.reloc` 不含 TLS offset 的 `HIGHLOW`；allocator 故意环泄漏探针仍须报出站点。
- **延期析构不能越过栈对象的调用帧**：`release_dyn` 被逃逸分析视为不逃逸，带 `ZAN_OBJ_STACK_FLAG` 的对象必须同步完成析构；超过阈值也不能入队。栈同步分支仍须参与深度加减，避免其析构内部释放堆对象时失去延期上限；递归深度为线程局部状态，不能用共享队列长度或进程全局计数代替，wasm 当前单线程形态除外。正常同步入口加一、返回减一；堆对象延期路径不修改深度。`case_arc_deferred_stack` 用手工 ABI 头/描述符与 100 个真实析构帧验证栈同步、嵌套 heap-mark 延期及线程隔离；其计数析构器不覆盖真实 heap allocator/free 或自然 Zan 栈提升，真实堆树回收由 `case_arc_deep_tree_drop` 补充。该 fixture 要求 `rt_mem` 真队列，只在非 Apple 且有 `zanrt_mem` target 时注册；无队列的同步弱回退不适用其 pending 断言。
- **临时所有权实参的生命周期回收（Lookup Builtins）**：
  在集合查找内建（如 `List.Contains`、`List.IndexOf`、`Dict.ContainsKey`、`d[key]` 读等）中，若实参是内联求值生成的临时对象（如字符串拼接或函数调用产生），IRGen 发射完成后必须通过 `emit_release_owned_call_temp` 释放该临时对象，避免每次查找泄露一次临时对象。
- **异常展开栈与实参临时注册（EH Unwind）**：
  在 setjmp/longjmp 异常展开体系中，向函数传参时，若实参属于 ARC 管理的临时对象，调用方必须在压栈前将其实参临时槽注册至每线程的 unwind 栈（`__zan_eh_tmp_push`）。当被调函数抛出异常时，展开流程将自动清理这些已注册的临时对象。调用成功返回后，pop 的次数必须与 push 次数严格保持一致，防止展开栈失衡。

- **接口接收者的正常/EH 所有权必须同源判定**：getter/调用返回的 owned 接口接收者，
  必须沿用正常释放路径的源表达式所有权分类，在求值实参前注册 EH 临时；借用字段/局部接收者不注册。
  正常返回按 LIFO 先 pop 实参临时、再 pop 接收者，随后按原协议释放；单候选与多候选接口分派均须覆盖。
  静态接口单例仍可能因调用抛异常遗留 getter 的 +1 引用，不能用清空单例掩盖调用方缺失的 EH 回收。
  回归：`tests/conformance/arc_interface_receiver_throw.zan`。

- **拥有型字符串返回值的空分支也必须保留 ARC 形态**：`Substring` 这类拥有型内建的
  零长度分支用 `emit_string_literal_rc`，不能返回 `zan_irgen_intern_string` 的裸 C 字符串。
  裸空串可能被常量合并到其他字符串末尾，地址低三位不为零；EH 临时栈把低三位当种类，
  原本的字符串就可能被误判为数组，掩码后的字符片段被当作析构描述符解引用。
  AppUpdate 持久化故障注入曾在 `__zan_eh_tmp_unwind` 因此原生崩溃；链接布局变化又会使
  同一错误暂时不崩。回归必须同时检查结果的对齐/字符串头与后续实参抛异常的展开路径，
  不能只凭一次业务测试通过。判定器：`tests/conformance/substring_empty_exception_owner.zan`。

- **embed 借用字符串仍须有可读的 ARC 头（2026-11-12 实证）**：`DllImport`
  返回借用指针不等于后续从不 retain；普通 Zan wrapper 返回与 collection 保存仍会
  探测字符串头。`zan_embed_read` 在文本出口提供对齐、sentinel RC、正确文本长度的
  稳定进程级缓存视图，空/缺失也同契约；不能把 decoder 的 raw malloc 指针直接当串。
  `zan_embed_bytes` 保持原指针和完整字节长度，文本遇 NUL 截止不能截断二进制尾部。
  压缩资源解密临时块在解压后释放，最终 raw 缓存与文本视图按资源生命周期保留；
  重复读取须复用缓存，不能把不可达的中间块当长期缓存。生成侧用 IR/libc 实现，
  避免新增符号使既有交叉运行时对象断链。回归：`case_file_embed_bytes`、
  `conformance_embed_compress`、`runtime_embed_string_abi`；IDE 首绘曾因 raw 串缺头，
  retain 向前探测跨 guard 页 AV，最小真实 embed+collection 探针先以缺头断言证红。
  **文本缓存锁不保护解码状态**：read/bytes 必须共用同一 gate，在读 entry flags 与
  data 之前取得，覆盖完整解密/解压及指针、长度发布；成功/失败/raw 出口都释放后
  才进入独立文本缓存锁。只在最终文本视图加锁仍会以旧 encrypted flag 读新明文指针，
  二次 XOR 造成持久数据损坏/拒读。`conformance_embed_first_read_concurrent` 以
  16 线程混合 read/bytes、64 个压缩/未压缩首读资源、8 个全新进程验证；旧版第2个
  进程数据校验失败，同锁修复后精确单跑及 leak/ARC 通过。

## 3. 委托与闭包形态

- **委托的双形态表示**：
  委托具有两种物理形态：① 纯静态函数指针；② 携带环境的闭包（Tagged Closure，携带上下文环境和捕获变量）。
- **跨帧保活与标记检查**：
  当委托跨调用帧逃逸或传递给异步线程（如 `Thread.Start`）时，若为闭包结构，必须执行 ARC retain 操作并在入口点进行 `ZAN_CLOSURE_TAG` 判别，确保闭包上下文环境在目标线程执行期间生命周期完整。
- **数组字面量初始化循环**：
  数组字面量初始化包含引用类型元素时，循环赋值必须遵守 ARC retain 协议，保证各元素引用计数正确自增。

## 4. 新增平台能力的落点：编译器侧发 libc 调用，不新增运行时导出符号

- **为什么不能新增运行时导出符号**：各平台的预编译运行时目标是仓库里的产物，
  在非目标平台上**重建不了**。新增一个 `zan_xxx` 导出符号，就等于让所有交叉
  目标链接失败——所以新能力优先落在**编译器侧**（IRGen 直接发 libc 调用）或
  只用 libc 既有符号。
- **落地形态**：内建表（`builtin_api.c` 的成员表）登记签名 + IRGen 直接发
  libc 调用。例如 TTY 判定：`Console.IsOutputRedirected()` /
  `Console.IsTerminal()` → 发 `_isatty(1)`（Windows）/ `isatty(1)`（POSIX）；
  wasm 没有终端概念 → 恒为"非终端"。**不新增运行时导出符号**。
- **stdlib 没有对应类也能用**：`Console` / `Math` 这类内建类型在 stdlib 里
  **没有类定义**，成员签名只在内建表里，编译器在发码阶段拦截这些调用。用户
  自己定义了同名类（`zan_type_defines` 命中）则以用户实现为准。
- **坑出处**：2026-10-20 之前 `Zan.Cli` 的颜色策略只能看环境变量
  （`ZAN_CLI_COLOR` / `NO_COLOR` / `TERM=dumb`），判不出"输出被重定向到文件"，
  ANSI 序列被写进日志文件。有 isatty 之后判定顺序是
  `ZAN_CLI_COLOR` > `NO_COLOR` > `TERM=dumb` > stdout 不是终端。
- **TTY 判定的验证方式**：管道里跑（stdout=管道 → 已重定向）与
  `cmd /c start "" /wait prog.exe`（新控制台 → 是终端）**两侧都要验**，
  只测一侧等于没测——本机默认没有可见控制台，测"是终端"必须自己开一个。

## 5. 协程帧的所有权：谁登记、谁摘表、谁释放

- **帧的生命周期是三方分工，不是两方**：
  ① 编译器分配帧；② 协程入口把帧登记进**活协程表**，协程完成时**摘表**；
  ③ **读者**（awaiter / 读 `.Result` 的一方）读完结果后**释放**帧。
  摘表的是协程自己的完成路径，释放的是读者——**这两件事不是同一个角色**。
- **两条释放路径的防御强度不同**：
  - 外部执行器（多 worker）路径走运行时的帧释放函数，用帧首 8 字节的
    `DEAD` 位做幂等守卫；
  - **默认的进程内协程驱动路径，编译器发的是裸 `free`，根本没有守卫**。
  写代码/审代码时别默认"运行时兜底了"。
- **就算有守卫也不可靠**：小对象分配器回收时把用户块首 8 字节写成 freelist
  指针，守卫位被覆盖 → 第二次释放读到垃圾 → 直接 abort（double free）。
  帧内存被复用后，守卫位就是别的对象的垃圾。
- **活协程表不能当所有权仲裁器**："谁把帧从表里摘掉谁负责释放"会释放得太早
  ——读者还没读结果帧就没了 → UAF。仲裁必须用**独立于活协程表的所有权凭据**。
- **"指针 → 已释放集合"方案不可用**：地址复用后无法区分陈旧指针与新帧——
  要么跳过一次合法释放（泄漏），要么释放一个活帧（内存损坏）。指针身份本身
  不够；要做 exactly-once，得给帧一个**稳定的身份**（独立句柄单元）或让帧
  内存进隔离区不回到通用分配器。
- **这类改动是协议变更**：编译器后端与自举后端各发一份帧释放调用，固定点门
  比的是两个后端的产物 → **必须同一次落地**，单改一边会让门直接不绿；
  各平台的预编译运行时产物也要跟着重出。
- **坑出处**：2026-10-20 复核协程帧释放的幂等性时把这三层事实查实了
  （默认路径无守卫 / 守卫位会被覆盖 / 活协程表不是仲裁器）。

## 6. 随机输出、队列槽复用与 sanitizer 证据（2026-11-10 实证）

- **独立输出不能假设地址邻接**：两个 `uint64_t *` 盐输出不构成 16 字节缓冲区；
  先把随机字节读入 `uint64_t keys[2]`，确认完整读取后分别赋值。回归使用非邻接
  输出与前后、间隔哨兵，避免测试布局恰好邻接而漏掉越界（`rt_hash_salt_test.c`）。
- **索引原子不保护环形队列槽的复用**：窃取者先复制槽、再 CAS head 时，其他消费者
  可能已推进 head，生产者随后复用同一槽；即使失败的 CAS 会丢弃副本，复制本身仍
  与写入竞争。保护完整 `{fn/step, frame}` task 对、槽快照及 head 推进，不能只把
  两个字段分别改为 atomic（仍可能配对撕裂）。当前方案用每 worker 队列锁；
  spill/steal 批次留在栈上，解锁源队列后再向其他队列或 injector 发布，避免嵌套锁。
- **启动失败不是 sanitizer 通过**：区分进入用例后的实际报告与 main 前的工具启动失败；
  GCC 13 TSan 的 `unexpected memory mapping` 不能计为通过，也不能靠抑制或
  `continue-on-error` 消掉。可用 Clang 的成功执行单独留证，不能替代未执行的 GCC
  门禁；同时核对进程退出码与完整 PASS 计数。短跑通过后仍需既定长测：本次槽复用
  竞态只在 4 worker 长测暴露。最终 Clang 18 的 TSan、ASan/UBSan 与普通版本均按
  2/4 workers、1000×512、每进程 600 秒 watchdog 通过，保留丢失、重复和重入检查。

## 7. 宿主 Task.Run action：取消也须在 DONE 发布前释放 owner（2026-11-11 已验证）

- **所有完成分支先清 owner，再发布完成**：`src/compiler/irgen_call_async.c`
  的 `get_task_action_step_fn` 曾只在 invoke 返回后释放委托；entry 的取消分支
  直接进入 done，跳过释放。把 load delegate → 清空帧槽 → release 统一放到
  done 出口，早于 release-order 的 DONE=1、join 通知及 awaiter 唤醒。
  正常返回、内部 catch 返回、执行前取消均只释放一次；cleanup 回调仍保留
  load → 清槽 → release，读取 null 时为空操作。
- **self-reaper 不等于 action cleanup**：`get_co_reap_fn`
  （`src/compiler/generics/generic_str.c`）处理帧异常 owner、注销活表、取消延迟并
  释放帧，不调用 action 的 ASYNC_FRAME_CLEANUP 回调。不能指望它补释放委托。
  原取消路径既漏 owner，又允许 Wait 在 owner 尚未释放时观察 DONE 返回；
  不应只解释成“稍后 cleanup 的 race”，也不能靠修改 observer 预期掩盖。
- **Wait 的 acquire 必须对应完成前的清理**：`emit_task_member` 通过
  `get_co_isdone_fn` acquire 读取 DONE；结果无关的 Wait 只观察完成，不负责释放
  action 帧。把清理留在 DONE 之后或异步 reaper 中，不能保证 Wait 后的弱引用观察。
- **验证证据与范围**：主会话报告原自然方法组 fixture
  `tests/conformance/task_action_owner.zan` 原样运行 8 次通过，正式 case 通过
  （3.21 秒）；保留 `.out`，取消后 observer 仍预期 false。该证据覆盖正常、
  内部 catch、取消，不证明外逸异常清理、重复使用旧句柄或跨 worker 帧回收已闭合。

## 8. action 外逸异常边界：已有契约与未验证范围（2026-11-11 只读核实）

- **worker 上未处理的 native 异常按现有机制终止进程**：
  `src/compiler/emit/emit_main.c` 安装 `__zan_eh_terminate`，调用 unhandled hook、
  报告异常并 exit(1)。`src/runtime/io/rt_io_co_sched.c` 的 worker thunk/step
  调用外没有语言异常 catch guard；正常返回后的 `__zan_eh_release` 不是异常 guard。
  GUI 的 `zan__guard_call` 不是 coroutine worker 的语言 EH 边界。
- **不要把 async 帧异常协议推导成 action 故障存储协议**：
  `emit_async_exc_epilogue` 把异常值、类型、ownership 移入帧，
  `emit_async_check_sub_exc` 在 await 路径取回传播；当前 action step 未接入。
  `docs/STDLIB.md` 明确无结果 Task.Wait 只观察完成、detached handle 无异常交接。
  不新增 Wait 重抛，也不默认捕获 action 异常写入 Task 后让 worker 继续运行。
- **同线程栈传播不是跨线程回提交者**：同步泵执行 action 时可能命中当前线程
  外层 handler；worker 异常不能回到提交线程 handler。当前 host 使用 native
  landing pad，不套用 selfhost 的 setjmp/longjmp 设计。
- **尚未修复或验证**：action 外逸会绕过普通 done；EH temporary
  `emit_eh_tmp_push_slot(..., ZAN_EH_SLOT_DLG)` 登记的是委托值，不是帧槽地址，
  push 不 retain、pop 不 release，unwind 只清登记项后释放，不清帧 owner 或完成帧。
  单加 push/pop 不能闭合；cleanup-only landing pad 在无 handler 的 native
  异常上也不能保证 terminate 前执行。保证终止前清理需要明确的 native 终止边界；
  若让异常穿过 co_run 后继续执行，还须处理其被跳过的 worker/sched 状态恢复。
  这些均为只读设计边界，不能记作已修、运行验证通过或零泄漏保证。

## 9. Gate 的登记到挂起间隙必须保持每个等待者的身份与所有权

- 为每次等待登记独立记录，并把票据显式传到 park。不要在 gate 上保存单个可覆盖的待挂起票据，也不能依赖协程始终运行在同一线程。
- timeout、Signal 或 Close 可能先于 park；保留完成状态，直到对应 park 消费它。等待记录在 park 与定时器两份所有权都结束后才回收，期间保活 gate。无超时等待也须在 managed lock 下登记，再交给 park，避免 Close 落在解锁与挂起之间。
- 晚到的超时只能完成对应记录，不能转成计数盈余；已被 Signal 唤醒的等待者也不能再唤醒一次。锁外派发需另保活待派发记录。
- 用可控顺序测试两票据交错、先超时后 park、先 Signal 后 park、关闭排空和无幽灵盈余；再跑自然协程并发等待。判定器为 `runtime_gate_timeout` 和 `case_gate_wait_timeout`。

## 10. Reactor → ready 的调度活性所有权与批量计数

- **先发布 continuation，再释放 pending 所有权**：IOCP 两个 polling 入口、
  POSIX reactor、DNS 正常完成和超时都必须把原 pending 计数保持到
  `zan_co_ready_batch` / `io_wake` 返回。先减到零再入队，会让另一 worker 的
  quiescence 判定看到“没有 IO、没有 runnable”，退出调度并留下未完成帧及 ARC owner。
  批次只保存已完成数量，发布后不得再访问可能已被 consumer 重用/释放的 op 或 frame。
- **批量增减必须记完整 delta**：`co_cnt_add(..., N, N)` 配 `InterlockedExchangeAdd`，
  不能只看正负号加减一。否则 N 次完成只对应一次提交，outstanding 变负。
  回归既断言入队后 outstanding=N，也断言排空后为零并可 idle。
- **失败队列也是 pending**：POSIX dead waiter 从 IO owner 转交 dead owner，
  `co_all_idle` 使用包含 dead queue 的 `zan_io_has_pending`；dead owner 同样在 wake
  之后释放。DNS timeout 只释放一次活性 owner，迟到 worker 结果不得再次 wake/减计数。
- **用确定性观察替代猜测**：在真实 decrement 后暂停 consumer，由独立线程调用
  `co_all_idle`，再排空队列；覆盖单项、批量、正常完成、超时及迟到完成。
  Windows `rt_iocp_timeout_test.c` 已复现旧顺序的两条 false-idle，并验证修复与 DNS；
  MariaDB savepoint 定向八次无泄漏通过提供集成证据，但历史省略结果日志没有 scheduler
  trace，不能把该机制断言为每个历史失败的唯一原因。此前 failed-await cleanup、
  泛型 async frame owner、awaiting-finally return 修复仍是独立修复。
- **活性计数不等于内存保活**：DNS timeout 摘链后，扫描器仍持有锁外派发列表；
  在 dns lock 下登记独立 delivery owner，worker 完成或启动失败只能清工作状态，
  待 delivery owner 交接后才可 free。扫描器派发后锁内裁决最后释放者，交棒后
  不再解引用 job。回归要强制 worker 在摘链解锁后、wake 返回前完成，不能只测
  scanner 返回后才完成的顺序；同时核对一次 free/wake/inflight release。
- **原生跨平台测试按 API 的真实成功值断言**：Windows TlsSetValue 非零成功，
  POSIX 兼容宏映射 pthread_setspecific 时零成功；设置后还要查实际 TLS 状态。
  工作线程本地批量计数用本轮 delta，避免先前测试留下的分片历史影响断言。

## 11. Linux 静态 musl、异常链接与 POSIX fixture 的证据边界（2026-11-11）

- **静态 malloc 包装必须兼顾 libc 自己的 calloc**：`--wrap=malloc` 会让 musl
  `calloc` 取得 Zan 分配的块；musl 的 `__malloc_allzerop` 不能检查该块的自定义
  头部，否则会把 Zan 元数据当 mallocng 元数据读取。链接时同时传
  `--wrap=__malloc_allzerop`；包装器先判 `zan_mem_owns(p)`，Zan 块返回 false
  让 calloc 正常清零，原生块转发 `__real___malloc_allzerop`，该符号用 weak
  引用，缺失时也返回 false。回归必须在真实静态 musl 上直接调用 libc calloc，
  覆盖小块与零初始化；glibc 上跳过不构成 musl 通过证据。
- **`__real_calloc` 不保证原生分配来源**：静态 musl 的 libc `calloc` 内部仍可
  调用被 `--wrap=malloc` 改写的分配入口，返回 Zan slab 块；活对象注册表扩容时
  再用 `__real_free` 释放就会进入 musl `get_meta` 崩溃。凡内部元数据与
  `__real_free` 配对，必须用 `__real_malloc` 分配并显式清零，乘法先检查
  `SIZE_MAX` 溢出；不能仅凭 `__real_` 前缀推断分配来源。
  `__malloc_allzerop` 包装只解决 libc 对自定义块头的探测与清零，不改变块的来源。
  回归 `tests/conformance/live_registry_growth.zan` 保活大量 `Convert.ToString`
  字符串，强制活对象注册表多次扩容，再释放并重复以覆盖条目复用；静态 musl
  验证必须实际链接 `rt_mem`。
- **交叉静态 EH 必须使用同一目标的完整链接集**：从成功的
  `zig c++ -target <arch>-linux-musl -static -fexceptions -funwind-tables -v`
  异常程序链接中提取实际输入，配齐该目标的 musl `libc.a`、startup 对象、
  `libc++.a`、`libc++abi.a`、`libunwind.a` 与 `libcompiler_rt.a`；禁止按 Zig
  缓存修改时间选库，也不能混入 GNU/宿主库。最终静态 ELF 链接保留
  `--eh-frame-hdr`，否则展开器缺少查找异常帧所需的头部；仅链接成功不证明
  throw/catch 可运行。
- **严格 C11 的 feature define 必须早于首个系统头**：`rt_file.c` 在包含头前
  定义 `_POSIX_C_SOURCE=200809L`，`rt_timer.c` 同时启用所需 POSIX/GNU 特性，
  `rt_crash.c` 提前定义 `_GNU_SOURCE` 以暴露 musl 的 sigaltstack/SS_DISABLE。
  在间接包含系统头之后再定义无效，不能靠 GNU 默认编译模式掩盖声明缺失。
- **AppUpdate 的 chmod fixture 要落在有真实权限语义的文件系统**：POSIX
  可执行位断言使用原生文件系统（如 `/tmp` 所在 ext4）；WSL DrvFS 未启用
  metadata 时不能建立 chmod/exec-bit 语义，不能据此判更新器权限逻辑正确或错误。
- **进程中断 fixture 用 `_exit`**：`tests/conformance/app_update.zan` 模拟中断时
  需跳过回滚、解锁与 native atexit 泄漏钩子；`exit` 会执行正常退出处理，
  把故意遗留的进程状态报告为泄漏，改变 fixture 要模拟的边界。
- **根因结论与最终验证分开记账**：以上记录已核实的机制与验证要求；待完成的
  真实 musl、交叉 EH 与 AppUpdate 最终测试不得记作已通过。

## 12. 反射构造、调用与站点冻结的边界（2026-11-12 Windows 实证）

- **引用强转与 `is` 分开处理 null**：引用强转先把 null 短路到成功合流，
  非 null 再沿原有闭合类型身份检查；不要修改共享 `is` 判定器，`null is T`
  仍为 false。普通 object-null、泛型局部与泛型实参转闭合 List/类均须成功，
  非 null 的异型泛型强转仍须抛 InvalidCastException。
- **反射实参沿用普通调用的借用与异常合同**：pack 每个实参只求值一次，记录
  原始 SSA 值；fresh owned 临时通过 `emit_call_arg_eh_push` 保活，正常返回后
  平衡 pop 并调用 `emit_release_owned_call_temp`，借用局部不释放。后续实参
  求值或被调方抛异常时走同一 EH 清理；用户函数中的 CreateInstance/Invoke
  外层调用用 `zan_call_or_invoke` 接入 caller landingpad，内部 helper 的普通
  call 不能替代这个入口。新构造对象在调用构造器前登记 EH，成功才撤销登记；
  Add 仍 retain 借用输入，不能靠吞掉输入引用补偿调用方泄漏，也不要与 Set
  已有临时清理重复释放。
- **隐式构造初始化也属于冻结前的分配闭包**：仅预留 ctor 自身 site 不够，
  字段初始化器还能分配类与闭合 List。先预留已知 ctor site，再预发该轮 ctor
  thunk；对新增 site/meta/ctor 继续迭代至不增长，元数据先入缓存以终止递归。
  记录已发范围，冻结后 finalize 不再重复发 ctor body；否则对象虽能通过 typed
  身份检查，动态 Add 仍可能因站点元数据缺失返回 false。
- **thunk 必须恢复完整调试状态**：切换辅助函数时保存并恢复 builder 位置、
  current_fn、LLVM debug location，以及 `di_cur_file/line/col`、
  `di_last_loc/di_last_loc_fn`。`di_clear` 会清逻辑位置，单用
  `zan_builder_save/restore` 不足以保留后续用户分配点的文件、行列归因。

判定锚点：`case_reflect_typed_list` 覆盖上述 null、正常/未命中调用、后参求值
异常、callee/ctor 异常与仅反射创建的嵌套字段初始化；本轮已以
`--check-leaks --arc-guard -g` 实跑无泄漏，并用 IR 核对惰性元数据生成后的
用户分配点仍保留实际文件、行列与匹配的 `!dbg`。不外推为其他平台已验证。
