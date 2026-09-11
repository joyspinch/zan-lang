---
name: zan-compiler-internals
description: zanc 编译器内部（parser/checker/irgen）的实测定式与坑——Dict 内建布局契约（插入序 keys/values + 惰性哈希索引 + Remove 整体失效）、LLVM select 两臂都求值导致的死臂分配泄漏（用 branch+phi）、delegate 两形态与 wasm32 函数表索引撞 ZAN_CLOSURE_TAG 的根修定式（形状按 target_is_wasm32 条件化）、ARC 所有权判定内建优先于 extern 借用（GetString 误判=每 HTTP 请求泄一条请求头）、looks_like_var_decl 的内建关键字分派契约（rank specifier 必须容忍逗号）、stdlib 按需拉入的四坑（扩展宿主关键字接收者/方法名撞类名/泛型委托假名 T/#if 区域必须带宏扫描）、交叉工具链 .o 重出配方（zig cc + build/ 暂存副本不会自动刷新）、可空值类型在字符串位的解包形状、编译器调试的 scratch 卫生（bisect 用 worktree 即用即删、A/B 对照复用固定目录名）。做或改 src/compiler/*、交叉运行时对象、conformance golden 时使用。
---

# zanc 编译器内部定式与坑

> 提炼自 standard 层存量挂账清零批（2026-09-09：int[,] 回归、Dict.Remove
> 保序、string+可空拼接、交叉 rt 对象重出）。每条都实测踩过。

## Dict 内建（irgen.c 布局注释 = 契约）

- 布局 8 字段：`{i64 count, i64 capacity, i8** keys, i64* values, i64* index,
  i64 index_capacity, i64 indexed_count, ...}`。**keys/values 并行缓冲保持
  插入序**（枚举序与 ARC 释放序都依赖它），`index` 是开地址哈希索引（存
  entry+1，0=空），由 `__zan_dict_find` 惰性重建——重建判据
  `indexed_count != count` 或 `index_capacity==0`。
- **Remove 必须保插入序**：swap-remove（末项搬进洞）曾让 Keys/Values 离开
  插入序，违反布局契约、偏离 C# 可观察行为。正确形状 = memmove 下移洞上
  方全部条目（键 1 词、值 value_words 词）+ **索引整体失效
  （indexed_count=0）**——洞上方条目全部重编号后任何增量索引修复都不可能，
  find 的 stale 判据天然触发全量重建。Remove 变 O(n) 与 shift 同阶，可接受。
- find 的 append 快路径（只索引新追加的键）有 `kept = icnt > 0` 守卫，
  icnt=0 不会误入快路径——整体失效与增量追加逻辑兼容。
- tail 槽清零不能省（ARC 不得见脏引用）；memset 按 libc 真身
  `(ptr,i32,i64)->ptr` 调（get_libc_fn 按名字取先到声明，签名不一致
  verifier 直接拒）。

## LLVM 陷阱

- **`LLVMBuildSelect` 两臂都求值**。字符串化之类会分配的辅助（itoa64 的
  数字缓冲）放进 select 死臂就是泄漏——leakcheck 孪生测试当场抓。要用
  branch+phi：两块各算各的，merge 处 phi 合流。
- 返回 NULL 字符串是合法的空串形状：emit_str_concat 有 NULL→""、
  zan_rt_str_release 有 NULL 守卫，全链路安全。

## delegate 两形态与 wasm32 的 tag 碰撞（2026-09-10）

- **形态契约（zan_abi.h）**：delegate 值一个指针两形态，bit 0 区分——偶数
  =裸 fn 指针（静态方法组/无捕获 lambda），奇数=tag 过的堆 closure 记录
  `{fn,dtor,target,<captures>}`，invoke 是 `fn(rec, args...)`（rec-first）。
  runtime 侧 store-family（rt_sync 的 dispatch 队列、gui_runtime_wasm 的
  wdisp）用 `v & ZAN_CLOSURE_TAG` tag-test 收纳两形态，retain 打
  `rec-16` 的 rc 槽。
- **wasm32 没有真实函数指针**：裸 fn 是 wasm 函数表索引（小整数，奇偶皆
  有），奇数索引撞 tag 位 → irgen 把表索引解引用成记录，fn 槽读到索引处
  内存 → 浏览器报 `null function at <调用帧>`。**修法是形状按目标条件化
  （irgen_expr.c `target_is_wasm32(g)` 门控），native/wasm 各自复现历史
  契约**：native 全裸形状零变化；wasm32 全走 tagged record——静态方法组
  造 `__zan_mg_<method>` thunk 记录（thunk 丢 rec 调 target），thunk 同放
  fn+target 双槽保 `E+=M;E-=M` 相等性语义（equality 按 target 槽比对），
  dtor/retain 跳过 target 槽（是函数非对象）；无捕获 lambda 也走 rec-first
  （`is_closure || target_is_wasm32`）。C 回调 cast `(nint)M` 走裸 fn 指针
  路径（emit_raw_fn_for_cb_cast），与记录互不干扰。
- **根修兜底（2026-09-11）：wasm 链接加 `--table-base=2`（main.c wasm-ld
  命令行）**。lld 默认表基址 1，地址取函数的表索引奇偶皆有——任何漏网
  的裸 fn delegate（如 `inp.Size = vm.x` 合成 Binding 访问器曾是裸指针，
  `emit_binding_acc_delegate` 已按 mg 形状收口）都会撞 tag：索引 N 被当
  tagged 记录 untag 成 N-1，间接调用恰好落进相邻函数（实测 Canvas_DrawGlyph
  落进 getenv）。表基址挪到 2 后裸索引恒为偶，native 的「偶数才合法」
  假设在 wasm 上成立，整族漏网点一次性免疫。
- **教训**：中间层用「指针低位 tag」复用值位时，必须审计目标平台上"这个
  位模式是否真的不可能出现"——函数表索引/句柄/压缩指针都可能撞 tag。
  新 target 落地时对"native 偶数才合法"的隐含假设逐个显式化。

- **跨调用保活 delegate 必须 retain，线程入口必须 tag-test（A70/A261，
  2026-09-11）**：`Thread.Start(job.Run)`（实例方法组 = tagged closure 记录）
  此前在 native `zan_thread_start` 里被原样当裸 `void(*)()` 调用 → 启动瞬间
  SIGSEGV；同形的 `Thread.Start(() => {...})`（捕获 lambda）一样崩，裸静态
  方法组却正常（checker 不拦，编译期无诊断）。native 两路（Win32
  CreateThread / POSIX pthread_create）与 wasm 版、UI 派发队列同构化：
  trampoline 先 `v & ZAN_CLOSURE_TAG` 判形态（带 tag 就卸 tag 取记录 fn 槽按
  `fn(rec, ...)` rec-first 调；裸指针才直接调），`zan_thread_start` 侧
  `zan_delegate_retain(body)`、trampoline 收尾 `zan_delegate_release(arg)`。
  **retain 是必需的，不是保险**：调用方在 `Thread.Start(...)` 语句结束就释放
  自己的临时量，工作线程可能还没跑（实测去掉 retain 后 `-g` 下即段错误）。
  **通则：任何把 delegate 存下来留给"另一次调用 / 另一线程"执行的 runtime
  入口，都要对到达时的那个值补 retain、在真正的执行点补 release**——这正是
  zan_abi.h store-family 契约的要求。

- **语义修复后要扫"断言旧行为"的每一处，生成器发出的注释也算（2026-09-11）**：
  A70/A261 让 `Thread.Start` 收得下实例方法组与捕获 lambda 之后，仓里仍留着三处
  按旧事实写的说明——`Gui/Widget/Upload.zan` 的类注释（"线程入口必须是静态方法组，
  `zan_thread_start` 只接受裸函数指针"）、`DataTable/DataTable.HttpSource.zan`
  （"实例方法组/闭包喂给 Thread.Start 会编译通过、调用即崩……该编译器缺陷另案"）、
  以及 **`System/Compiler/GenForm.zan` 往生成代码里 Append 的
  `/// ... (delegates are plain function pointers)`**。前两处是注释，第三处是
  **字符串字面量**（生成物里的注释）——只扫 `docs/` 与指南扫不到它。做法：改完
  ABI/调用约定类语义，用旧断言的特征词（`plain function pointers`、`必须是静态
  方法组`、`只接受`）`git grep` 全仓（含 `stdlib/**` 与生成器 Append 串），注释里
  引用的 `_scratch` 探针结论一并复核。坑出处：这三处是"文档更新已完成"当天漏掉
  的，"更新 stdlib 注释"被当成做完，实际只改了一半。

## wasm32 局部数爆炸：V8 每函数 5 万局部硬上限（2026-09-11）

- **症状**：浏览器 `WebAssembly.instantiate` 报 `Compiling function #N:"X"
  failed: local count too large`。V8 上限 50,000 局部/函数；gui_gallery 的
  RenderPreviewEx（1.1 万行 IR）在默认档发出 116,222 个局部。
- **根因**：默认档（ZAN_OPT_NONE）走 `fast_codegen`→`LLVMCodeGenLevelNone`，
  wasm 后端零 stackify，几乎每条 IR 中间值都落成一个 wasm local（11.3 万
  IR 行 ≈ 11.6 万局部，一比一）。wasm32 上跳过 `fast_codegen`（main.c
  `if (!irgen.target_is_wasm) irgen.fast_codegen = true;`）后同一函数 731
  局部，全模块 max 731，编译 60s 可接受。**诊断工具**：解析 wasm code
  section 的 ULEB local 声明即可按函数计数（`_scratch` 里现成
  wasmlocals.py/wasmname.py 思路：导入函数计数偏移 + 每函数 `count×valtype`
  对求和）。
- **DLLImport nint 的坑**：`map_type` 对 TYPE_NINT 恒发 i64（所有 target）。
  embedres.c 往 File.zan 的 `nint zan_embed_bytes` 声明里塞函数体时，返回
  值必须按声明的实际返回类型 coerce（ptrtoint）；x64 宽度撞对掩盖了非法
  IR，wasm32 的 32 位指针立刻 `type error in return[0] (expected i64, got
  i32)`。凡「编译器合成的函数体装进 DllImport 声明」都要查这一层。
- **wasi-libc getenv 在 zan shim 下崩**：libc 惰性 environ 初始化走 zan
  WASI shim 的空 environ 契约，strncmp 读野指针 OOB（只在分配压力大、
  渲染中途首次 getenv 时炸，启动时探针复现不了）。H5 无环境变量——
  rt_sync_wasm.c 直接 stub `getenv→NULL`（显式 .o 优先于 libc.a 档案）。
- **stdlib 快照坑**：wasm gallery 构建用 `--stdlib-path _scratch/h5gui/
  stdlib_min`（带 WASI 分支的裁剪快照），**repo stdlib 的修复必须镜像进
  快照**（Effects.zan TakeDamage 首帧 null 守卫），否则构建用的还是旧代码。

## 泛型实例化定长表：第 64 个起静默丢弃（A78-3，2026-09-11）

- `irgen_emit.c` 里两张"按实例逐份"的表曾用定长栈数组：`variants[64]`
  （每实例一份方法体）与 `insts[64]`（泛型类静态字段的按实例初始化器）。
  `variants` 有一槽留给擦除版，循环守卫 `nvar < 64` 只装得下 **63** 个具体
  实例化：第 64 个实例化的专用体 `Wrap_Show$T63` 从未发射，调用点
  `route_generic_method` 查 `find_generic_fn` 落空后**静默退回擦除版**，而擦除
  版的体就是 `call abort(); unreachable` → 程序无输出、退出码 3。同族的
  `insts` 从第 65 个起静默丢弃，那些实例化的静态字段停在 0。
- **触发条件是"体的发射需要按实例特化"**（体里用到类型参数，如 `Wrap<T>` 的
  `Show()` 调 `item.Name()`）。只靠擦除形态就够的实例化（类实参、体不碰 T）
  到 100 个也正常——所以早期记录把它误判成"≥64 个实例化堆损坏、非确定性、
  ASLR 相关"，实际是**确定性的运行期 abort**，只跟"需要几份专用体"有关。
- **修法**：两张表改按需倍增的堆数组。**任何容量上限都不许静默截断**——截断
  等于悄悄改变行为，且症状与原因相隔极远。
- **诊断定式**：`ZANC_TRACE=1` 下 `discover_generic_insts` 报实例化总数、
  `route_generic_method` 在"具体实例化找不到专用体而退回擦除版"时报
  `route miss: Type.Method argc=..`——见到 `route miss` 就是这张表漏了实例。
- **回归**：`tests/conformance/generic_inst_count_70.zan`（70 个实例化，同时
  覆盖 64 边界的专用体与 65 边界的静态初始化器）输出 `2485/70`。

## 调用形状：接收者槽必须按静态性判定（A270，2026-09-11）

- `obj.Method(args)` 在 irgen_call.c 里按接收者形态走**两条互不相交的分支**：
  局部变量接收者（`local.Method(...)`）与表达式接收者（`expr.Method(...)`，`if (recv_cls)`）。
  两条都必须先判 `method_sym` 的 MOD_STATIC：静态方法签名里没有接收者槽，契约是
  「不传接收者」（另一分支的注释明写 `expr.StaticMethod(args)` is legal；C# 会 CS0176
  拒绝、Java 允许，Zan 选了允许）。
- 局部变量那条曾**无条件** `argc = args.count + 1`、把接收者塞进 slot 0、参数整体后移
  一位 → 静态方法调用生成 3 参 call 打 2 参函数，LLVM 校验报
  `Incorrect number of arguments passed to called function!`；报错点离调用点很远，调用者
  只看得到一个参数个数不符的 call。触发面就是 `d.StaticMethod()` 这一种写法
  （`tests/gui/compref_designer_test.zan` 曾长期挂在 standard 档）。
- **定式**：新增或改动任何 call 发射分支，先问三件事——接收者槽要不要（静态性）、
  参数索引用不用偏移（`recv_off`）、`emit_dispatch_call` 的类参数要不要传（静态方法
  没有 vtable，传 NULL 走直接调用）。三者是一组，漏一个就是形状错配。
- **归属定式**：这类"参数个数不符"报错不明说谁多传了，定位靠最小探针 + 逐一遍历接收者
  形态（局部 / 字段 / 临时 / 类型名）；并用**旧编译器快照**证明是既有缺陷而非本轮引入
  （本次用 `_scratch/zanc_head.exe`，2026-09-03 构建，同探针同样复现）。

## 属性访问：裸名 static 读静默得 0，裸名实例写让 zanc 段错误（2026-09-11 审计实测）

- **方法体里对「本类带自定义 setter 的属性」写裸名 `Prop = v;` / `Prop++` → zanc
  rc=139 段错误**。`irgen_expr.c:3058` 把 `recv_type` 传成 `g->cur_inst`（非单体化流程
  里是 NULL），`emit_property_setter_call` 在 `:1090` 无保护解引用 `recv_type->sym`；
  getter 对应路径 `:1017-1021` 明写「`recv_type == NULL` 标记 `base.Prop` 读」并做了
  判定，setter 漏了同一条。`route_generic_method`（`irgen_generics.c:261`）与
  `subst_type_param_deep` 都容忍 NULL，唯一裸解引用就是 1090。
  **绕法**：写 `this.Prop = v`（探针 `_scratch/audit2/prop_set_this.zan` 正确 101）；
  auto 属性走字段槽不受影响，static 属性裸名写走 `:3032` 另一分支也不崩。
  探针：`_scratch/audit2/prop_set.zan`、`prop_inc.zan`（均 139）。
- **裸名 static 属性读静默返回 0**：`static int P { get { return b+1; } }` 里
  `Console.WriteLine(P)` → 0，`A.P` → 正确值；auto static 属性裸名读正常。即同一面
  上「写正常、读错」。探针 `_scratch/audit2/static_read.zan`（Auto=7 ✓ / GOnly=0 ✗ /
  Custom=0 ✗ / A.Custom=4 ✓）。**绕法**：静态属性读一律限定 `类名.属性`。
- **17+ 参数 extern 只能限定名调用**：裸名走 `irgen_call.c:5019` 的
  `LLVMGetNamedFunction` 兜底，`:5030` 的 `LLVMTypeRef ptypes[16]` 配 `:5031`
  `if (nparams > 16) nparams = 16;` —— 钳的是后面的 `ptypes[k]` 循环，
  `LLVMGetParamTypes` 没有容量参数、按真实个数全量写（60 参 = 480B 进 128B 栈数组，
  越界写），症状是离真实原因很远的
  `LLVM verification failed ... Call parameter type does not match function signature!`
  （第 16 参起仍是 i64 未被 coerce）。限定名 `A.big17(...)` 路径正常（探针
  `p17.zan` / `p60.zan` / `p17b.zan`）。
- **本类一个构造函数都没有时 `new C(args)` 静默丢实参**（`irgen_expr.c:7281-7287` 的
  诊断以 `type_has_ctor()` 为门，无 ctor 反而不报）→ 对象只用字段初始化器，args 不
  求值。探针 `_scratch/audit2/newargs.zan`。
- **审计纪律（本轮两条代理结论被证伪，别再登记）**：① 「`Task.Delay(long.MaxValue)`
  有符号溢出→立即触发」不成立——探针 `delaymax2.zan` 打印 start 后睡死，5s 超时仍未醒；
  ② 「`irgen_emit.c` 的 `fields[32]/names[32]` 缓冲区溢出」不成立——`:1802-1803`、
  `:1983-1984` 都有 `< 32` 守卫，是**静默截断**（names 侧无可见症状；fields 侧只有
  「>32 个 T 型字段的泛型类」以 `unresolved call 'F32.ToString'` 暴露，见 TASKS A281）。
  静态阅读/代理给的结论必须逐条最小探针复验再入账——错报会让人去修不存在的东西。

## parser：looks_like_var_decl 的分派契约

- 内建类型关键字开头的语句要在「声明」（`int x = 3`、`int[] a`）与
  「表达式语句」（`int.Parse(s)`）之间分派，实现是裸源码扫描找 `.`。
  **扫 `[` 档位时必须容忍空白+逗号**——`int[,]`/`int[,,]`/`int[][,]` 的
  rank specifier 里是逗号，只认 `]` 就把整个语句误判成表达式
  （cs_b08_arrays 回归的根因）。混合档 `int[][,]` 也要过。
- 单行多声明符 `int a = 0, b = 2;` 走 pending_stmts 队列 + 三个语句收集点
  splice；comma 循环只吃 `IDENT [= expr]`。

## 字符串位的可空值类型

- C# 语义：`"a=" + int?` 合法，null 拼空串。checker
  type_is_concatable 对 TYPE_NULLABLE 递归放行元素可拼的；
  irgen emit_to_cstr_of 对 `zan.nullable.<payload>` 命名结构解包——
  has ? cstr(payload) : NULL（branch+phi，见上）。无符号元素
  （uint?/ulong?）要传 emit_to_cstr_u 的 unsigned 旗标。
- `Convert.ToString(int?)` 与 `.ToString()` 直接调至今会炸 verifier
  （nullable 结构按值进了 itoa64 形参）——拼接路径能走是因为有解包；
  直接调用是另一个待修缺口。

## 交叉运行时对象（toolchain/*/*.o）重出配方

- 源头 scripts/build_cross_rt.cmd 用 zig cc（本机无安装时：
  C:/Users/QQ/Downloads/zig-x86_64-windows-0.15.1.zip 解包即用；
  NDK 块要 ANDROID_NDK，OHOS 块要 OHOS_NDK）。
- **build/ 里的暂存副本不会自动刷新**：zanc 链接后处理的「自包含工具链
  打包」对已存在的 build/macos/ 等目录直接 skip——重出 toolchain/*.o 后
  必须手动 cp 进 build/<target>/，否则链接（--emit-lib、交叉 exe）用的
  还是旧对象，测试照样挂。
- ELF so 链接容忍未定义符号（运行期才炸），Mach-O dylib 链接期即拒——
  运行时新符号没进交叉对象时，只有 macos dylib 测试会报警，别被
  「只有 mac 挂」误导成 mac 特有问题。
- toolchain/** 的 *.o 命中 gitignore，提交要 `git add -f`。

## win-arm64 交叉 rt：setjmp 降层与 rt_crash 架构门（实测踩坑 2026-09）

- **生成的 `_setjmp` 在 aarch64-windows 链不上**：ARM64 的 msvcrt.dll 根本
  没有 `_setjmp` 导出（mingw setjmp.h 原话 "ARM64 msvcrt.dll lacks _setjmp,
  only has _setjmpex"），x64 那套「两参调用形状逼 LLVM 生成 rdx=返回地址
  前导」也是 x64 专属。irgen_builtins.c `emit_eh_setjmp/emit_eh_longjmp`
  已按目标降层：aarch64-windows 发静态对 `__mingw_setjmp(buf)`（入口 Lr
  即返回地址，无需前导）+ `__mingw_longjmp`，两端布局一致，libmingwex.a
  里就有；x64 路径一字未动。
- **rt_crash.h 的 guard 恢复是 x64 专属**：`__builtin_setjmp/longjmp` 在
  aarch64-windows 没有后端，CONTEXT 字段名也分架构（x64 Rsp/Rip，ARM64
  Sp/Pc，用 ZAN_CTX_SP/PC 宏）。恢复机（guard_recoverable/rip_recoverable/
  guard_resume/guard_log_deferred + setjmp 帧）整体收进 `#if x64` 门；
  非 x64 的 `zan__guard_call` 保留一次性崩溃记录器安装后直通执行——崩溃
  照常有 first-chance 记录，只是不做恢复续跑。头注释里写明这个降级。
- **ohos 交叉链接的两个暗坑**：(1) 链接行强制要求 toolchain\ohos-<arch>\
  zap_main.o（XComponent 壳适配），缺了报「sysroot subset not found」，
  但源 zap_main.c 只在 ohos-x64 目录里跟踪、.o 两架构都不跟踪也没脚本造
  ——build_cross_rt.cmd ohos 块现已补 zap_main.o + libEGL/libGLESv3 空占位
  so（scripts/ohos_stub.c）的构建配方；(2) `-shared` 默认容忍未定义符号，
  ohos 链接成功≠符号齐——要 `llvm-nm -D` 看未定义表再去 OHOS libc.a 里
  对（musl 有 `_setjmp`/`_longjmp`/`longjmp`，加载期可解析）。
- 重出对象名是 `zanrt_*.o` 前缀（build_win_rt.sh 协议），别把裸 `rt_*.o`
  拷进 toolchain/。OHOS NDK clang 15.0.4 在 DevEco Studio 安装目录的
  native/ 下。

## conformance 处置四分法

挂的测试先归因再动手：「stale golden」（重生成，逐行核对 C# 拼写）、
「实现违约」（修实现——布局注释/文档注释就是契约，「注释与实现打架时
先查 git 考古谁先谁后」）、「测试源不合法」（C# 也拒绝的写法改测试源，
如无约束 T 的 string 拼接改 .ToString()）、「真回归」（git 考古最后
通过点定位元凶提交修编译器）。混着处置就会把行为改错。

## ARC 契约：DllImport 的 string 返回值是借用指针，永不释放（内建除外！）

- **内建优先于 extern 借用规则（A262，2026-09-10）**：`NativeMemory.GetString`
  是编译器内建（emit_native_memory_call，irgen_expr.c 用 emit_string_alloc_rc
  建真 ARC 串、memcpy、NUL 收尾，交调用方 +1），`[DllImport]` 声明只为
  checker 提供类型。extern 借用判定必须先给内建开白名单
  （`is_call_to(e,"NativeMemory","GetString") && args==3` → return 1），
  否则所有消费点（实参临时/丢弃/局部捕获/返回）各泄一条串——stdlib 里
  ByteBuffer.Str → HttpFramer.Head/Slice/ReadBody 等约百处调用全中招，
  服务端每 HTTP 请求泄一份原始请求头，RSS 线性上涨（WSL 3×60s 登录压测
  13.7→313 MB；修复后 10.3→13.1 MB 趋平）。诊断定式：**泄漏报告的分配点
  不是泄漏原因**（报在消费点，根因在所有权判定）；**RSS 随请求数线性
  上涨=每请求泄漏，段间恒定=有界设计成本**；async frame 对 `this` 的
  receiver-retain（防 fluent 接收者 use-after-free）是设计内所有权，
  leakcheck 残余 1 条非缺陷。
- **新内建必须同步借用白名单（A264）**：白名单按内建名逐个枚举，
  nm_sha256_fn（NativeMemory.Sha256）落地时 expr_yields_owned_rc_value
  同步补 `is_call_to(e,"NativeMemory","Sha256") && args==2`——**加内建
  不加白名单 = A262 的复刻**，且泄漏只在真实调用方出现，编译器自身
  测试照绿。
- **expr_yields_owned_rc_value 对 extern 调用必须返回 0**（irgen_generics.c）：
  声明返回 `string` 的 bodyless [DllImport] 返回的是裸 `char*`——extern 内存
  或**调用方传入的缓冲**，没有 rc 头。通用丢弃路径（AST_EXPR_STMT）把所有
  AST_CALL 结果当 owned(+1) 去 release，就会把别人的内存放掉：stdlib 的
  `getcwd(b, 4096)` 返回指针落在调用方 `byte[]` 上，release 经
  zan_rt_str_release 的数组转发臂把数组引用减到 0 → free → musl mallocng
  把整组 16KB unmap → 随后 b[n] 扫描读已卸载页 SIGSEGV。Windows 分支不走
  getcwd 所以模板只在 Linux 上崩（诊断时先看平台分支差异）。修法是给
  expr_yields_owned_rc_value 的 AST_CALL 分支加 extern 判定
  （extern_check_callee_sym + sym_declares_extern + method_ret_type_at），
  void/string 返回一律借用。conformance 用例
  tests/conformance/extern_borrowed_string_result（getcwd 三次全读）。
- **platform-defining DllImport 会被 irgen 内建短路**：`Directory.\
  GetCurrentDirectory`/`SetCurrentDirectory` 在 irgen_call.c 有内建降层
  （!zan_type_defines 才生效）——探针里自己写 Directory 类会静默走 stdlib
  版本，写别的名字才有效。
- **musl 静态链内存布局速查**：cross 链接带 zanrt_mem.o + --wrap 时
  ≤2048B 走 zan 槽位分配器（1MiB 对齐 slab，指针 & ~0xFFFFF 可判归属），
  >2048B（如 4096 的 getcwd byte[]）落 musl mallocng 的 mmap 堆
  （0x7ffff7xxxxxx 段）；崩溃地址落在 mallocng 元数据检查
  （__malloc_allzerop）且 RSS 平台化后不重现，优先怀疑高压竞争而非
  Zan 侧 UAF——先用低速率复跑分型。

- **消费方要留引用时，拥有所有权的临时接收者必须释放（A269，2026-09-11）**：
  委托绑定（`Action a = new Job(i).Run;`）与 retained 字段读一样，会让闭包
  记录 retain 接收者（`emit_closure_record(..., retain_target=true)`），但方法组
  这条路径原先没有像 `finish_member_of_temp` 那样把"临时量自带的那一份 +1"
  释放掉 → 每次 `new Job(i).Run` 恒定泄漏一个 `Job`（20000 次循环泄漏 20000
  个；`--check-leaks` 报的分配点是那行 `new`，一眼看像"新对象泄漏"）。修法：
  把判据抽成 `receiver_is_owned_temp`（`obj_val && is_rc_managed_type(obj_type)
  && !expr_is_local_ident(object) && expr_yields_owned_rc_value(g, object)`），
  字段读与方法组绑定共用；**局部变量接收者不释放**（它不拥有 +1，释放会提前
  free）。诊断定式：**泄漏报告的分配点是"被泄漏的对象"，不是"忘记 release 的
  那条路径"**——按"谁 retain 了它"反查消费点（这里是 emit_closure_record 的
  retain_target），别只盯着 new。

## 字节串 ABI 契约（stdlib crypto EVP 换装踩坑，2026-09-10）

- **byte[] 按 string 形参传入时 `.Length` = strlen**：共享的是 payload
  指针，没有数组头，长度按 0x00 截断——Hex.Decode 产物首字节为 0 时
  `.Length==0`。stdlib 旧 crypto 从不读缓冲参数的 `.Length`、只索引；
  新代码加 `.Length` 门禁会把合法密钥/密文判空（Aes.zan:127 报
  "list index out of bounds" 的根源）。缓冲参数只判 null。
- **GetString 产物不能写字节**：`s[i]=x` 触发长度缓存失效
  （emit_string_len_invalidate），之后按 strlen 重 derive，NUL 开头的
  内容坍缩成长度 1，第二次写就越界。GCM tag 这类含 NUL 的编组必须走
  `byte[]`，仅在 extern 调用边界 `string tagBuf = tagB` 零拷转换。
- **同符号异形参用 EntryPoint 别名**：`[DllImport("crypto",
  EntryPoint="EVP_EncryptUpdate")] static extern int EVP_EncryptUpdateS(...)`
  ——发明不存在的符号名链接期才炸；string 形态做零拷输入（CBC 2→196
  MiB/s 的差额全在这一次字节拷贝）。
- **EVP 单块 ECB 两方向都要 `set_padding(0)`**：解密侧 PKCS#7 默认把
  末块扣在 Final 里，Update 返回 0 块（表现为"解密全零"）；SM4 微信
  无 pad 语义同款。EncryptCbc 返回精确长度数组（golden 断言 ct.Length，
  多给的 len+32 破档）；调用方对 outLen `List<int>` 预 `Add(0)`（空表
  写 [0] 是 fail-soft：报错但继续，错误会漂到别处爆）。
- **Zan 逐字节循环 ~130 MiB/s（1 MiB≈8ms），热路径字节过 native 边界
  进出两个方向都必须零拷贝**：进=byte[]→string 形参直传（上一条）；
  出=native 写进**精确长度预分配**的 byte[] 原样返回（CBC 加密 PKCS#7
  长度加密前确定 `(len/16+1)*16`；GCM 流式等长；仅解密剥填充会缩——
  未缩原样返回、缩则一次 `EntryPoint="memcpy"` 收缩，byte[] 形参直传
  载荷指针，File.EmbedCopyIn 同款）。坑：A264 只零拷了输入侧，吞吐立
  即被输出侧的"len+32 上界分配+逐字节收紧拷贝"钉死在 ~200 MiB/s
  平板（与算法无关=与 native 库无关=拷贝循环在扛），输出零拷贝后
  CBC 1267-1319 / GCM 2000-4413 MiB/s（raw 的 88-96%）。

## 字符串字面量里的 `\xNN` 是码点，不是裸字节（2026-09-11 实测）

- `"\xEF"` **不是** byte 0xEF，是码点 U+00EF，进字符串时编成两个 UTF-8 字节
  `C3 AF`。于是 `"\xEF" + "\xBB" + "\xBF"` 不是 3 字节 BOM 而是 6 字节
  `C3 AF C2 BB C2 BF`：同一份 82 字节的 CSS，拼上它读出 len 88、首字节 195，
  写进文件得到的头是 `C3 AF C2 BB C2 BF`——「看起来像 BOM、其实不是」，
  拿去验证 BOM 行为会得出错误结论（实测踩过一轮）。
- 需要字节精确的内容（BOM、协议魔数、含高位字节的 fixture）一律从 `byte[]` 拼：
  `byte[] raw = new byte[3]; raw[0] = (byte)0xEF; ...; string s = raw.ToStr(0, 3);`
  ——`File.ReadAllText` 内部就是这样把 chunk 变字符串的。自检：`.Length` 等于
  字节数（BOM 是 3 不是 6），首字节是你想要的码值。

## stdlib 按需拉入（demand-driven pull-in，2026-09-10）

> 以前 `using Gui;` = 目录全量 glob + 传递 using 扫描到不动点，一个空窗口
> 程序 parse 380 个文件、2.8s，且 stdlib 树里任何文件有语法错全体拖垮。
> 现在目录内文件按"声明名被拼写"过滤后才 parse（`main.c` 的 pi_* 块），
> 空窗口 269 文件、纯 hello 3 文件 0.3s。语义等价性靠 conformance 三件套
> （pullin_shadow_same_name / pullin_extension_host / pullin_qualified_escape）
> 钉死。

- **词法级名字匹配的四个假阳性/假阴性坑，全踩过**：
  - 扩展方法宿主是"不可见名字"（调用处只写 `s.CompareTo(...)`），必须当
    锚点无条件拉入。识别形状是 `( this Type`——**Type 常是内建关键字
    token**（`this string s` 的 string 是 TK_STRING 不是 IDENT），只认
    IDENT 会漏掉所有内建类型扩展；`M(this.x)` / `M(this)` 是表达式不算。
  - 方法名撞 stdlib 类名（用户方法 `Label()` vs `Gui.Widget.Label`）：
    种子阶段"点号链根是命名空间段"才把链上名字当拉入信号；类型体内
    `类型 + IDENT + (`/`{` 是方法/属性**声明**名，不标活。否则一个
    `b.Label()` 级联拉进整个 Gui。
  - 泛型委托 `delegate T Mapper<T>(T x)` 的名字 = `<>` **外**最后一个
    IDENT；不跳泛型参数会把 "T" 铸成声明名，此后任何文件提到 T 全量
    级联。
  - 扫描必须带与真实 parse 相同的 `-D`/target 宏：`#if` 停用区里的引用
    不进种子（ZanGen 的 Main 整个在 `#if ZAN_GEN_MAIN` 里，Gen* 类全靠
    它引用）。
- **用户同名类遮蔽**：用户自己 `class App` 时未限定 `App` 永远解析到用户
  自己的，不拉 stdlib 同名文件；限定 `Gui.App` 仍拉。这是 stdlib 模板
  （App/Window/Button 全是常见词）不级联的关键。
- 门控：`--emit-symbols` 恒全量（IDE 索引要完整 stdlib），`ZAN_NO_PULLIN_FILTER=1`
  回退旧行为，`ZAN_PULLIN_DEBUG=1` 打印每个文件的拉入原因（含命中名）。
- 语义等价验证定式：同一程序 `ZAN_NO_PULLIN_FILTER=1` 开关两态编译运行
  diff 输出；改拉入逻辑必须补 conformance 用例并跑 smoke+standard。
- 顺带的实证：**prune 已保证未用代码不进二进制**（关 prune 只多 7KB），
  "using Gui 导致 exe 10MB"是错觉——Gui 窗口 exe 的 1.6MB .text 是
  GuiHost→App/Style/Fx 的活代码闭包 + Zan 运行时，与 unused 无关。
- stdlib 文件引用跨命名空间类型必须写 using（ChartHost 曾裸写 `App`，
  靠用户程序恰好也有 `class App` 才碰巧编译——prune 把它藏成了哑弹）。

## 闭包捕获语义：按变量分型（2026-09-11 实测）

写闭包相关的代码或文档时**不要假设捕获统一按引用装箱**——Zan 按变量分型
（实测探针 `_scratch/zqa/cap2.zan`）：

```zan
int p = 1;
Action r = () => { Log(p); };   // 只读捕获：创建时把值快照进闭包记录
p = 42;
r();                            // 打印 1 —— 外层之后的写入它看不见

int q = 1;
Action w = () => { q = q + 1; }; // 被闭包赋值的变量：局部提升为共享单元
q = 10;
w();
Log(q);                          // 打印 11 —— 闭包内外读写同一个单元
```

- 坑出处：给 `zan-site/guides/gui.md` 校订「委托不能捕获局部变量」这类断言时，
  差点按"闭包一律快照"或"一律共享"写成一句错的——实测发现是**逐变量**决定的
  （只赋值的那一个才装箱）。写文档/写跨帧状态共享时先按这个分型核对。
- 配套事实：对象捕获持有的是引用（`h.v = 9` 对闭包可见），所以"捕获后改状态
  看不见"只适用于**只读捕获的值类型局部**。
- 委托形状决定调用方式：静态方法组/无捕获 lambda 是裸函数指针，实例方法组/
  捕获 lambda 是带 tag 的堆闭包记录（rec-first）。判据、契约与 store-family
  retain 规则见 `src/common/zan_abi.h` 与 `docs/ABI.md` §3.6。

## 并行会话下的 ctest 假红（2026-09-11）

同一工作树里有别的会话在改 stdlib 时，standard 档会出现**与自己无关的红**。
先归因，别急着改自己的代码：

- **编译/检查类失败**：报错落在某个 stdlib 文件、而该文件在工作区是 `M`（在途
  改动）→ 属于那条车道。关键判据是**报错来自哪个阶段**：只改了 irgen/runtime
  时，checker/binder 的报错（"after type checking"、未解析调用、null 安全）不
  可能是你引入的——那些阶段在你的改动之前跑。
- **陈旧产物导致的挂起/超时**：`tests/run_case.cmake` 只在 exe 不存在、或比 `.zan` 源旧、或比
  `-DSTDLIB_STAMP` 旧时才重编；而 `STDLIB_STAMP` 由**调用方**传入——glob 出来的
  conformance 用例传了它，**gui 段的 `add_test()` 没传**，所以 gui 用例从不因
  stdlib 变化而重编（下面那条 10 分钟挂死就是它的代价）。别的会话在你上次跑之后改了
  stdlib，ctest 仍会复用旧的 `build/conf_*.exe`，于是出现"单跑必挂、手编必过"
  的怪象（本次 `conformance_gui_listview_scrollbar_drag` 挂死在 5:09 的中间态
  产物上 10 分钟，`rm build/conf_<name>.exe` 重编即 PASS）。
- **归因顺序（四步）**：单跑该用例 → 删 `build/conf_<name>.exe` 重编单跑 →
  手工 `zanc` 编译 + 直接跑 exe → 旧编译器快照（如 `_scratch/zanc_head.exe`）
  复现。四步都指向"不是我"再继续；否则停下来查自己。
- 另一条会一次打红**整档**的：并行会话重链 `build/zanc.exe`（本轮实测 07:21:34，
  而我这轮 ctest 是 07:20:58 起的）。编译器一换，所有 `conf_*.exe`/golden 产物
  全部过期，逐条归因毫无意义；判据是 `ls -l build/zanc.exe` 的 mtime 落在你的运行
  区间内 → 整档作废重跑。

- 另一条常客：**端口/资源竞争与真 flaky**。判别法是把**同一个二进制**（不重编）
  连跑 5 次——通过/挂起交错就说明是被测代码里的竞争，单次的超时/失败不能当回归
  （本次 `conformance_gui_listview_scrollbar_drag` 同一 exe 3 过 2 挂，而它属
  Gui 车道在途改动；`conformance_http_client_keepalive` 则是全量并行 120s 超时、
  单跑 0.5s 过，属端口竞争）。并行档的超时值一律先单跑复核。

- **两档 ctest 绝不能同时跑：它们共享同一批 `build/conf_*.exe`**（smoke 与 standard
  的 label 大量重叠，`add_test` 的 `-DOUT_EXE` 是同一个路径）。本轮实测：我这轮
  `-L standard -j 4` 起来后，另一会话的 `-L smoke -j 32` 也在跑，两条进程同时往同一个
  `conf_*.exe` 写、又互相把它当「已是最新」复用，双方都开始冒出无法归因的红。**开工前
  先查** `Get-CimInstance Win32_Process -Filter "Name='ctest.exe'"`，有别人的档就先等它
  跑完（或另开 `git worktree` 用自己的 build 目录），别硬上。

## 编译器调试的 scratch 卫生（bisect / A-B 对照）

> 2026-09 清理时 `_scratch` 已积到 48G：bisect 整树、A/B 快照、stdlib
> 副本、SDK 解压双份只进不出。清理是任务收尾的一部分，不是可选项。

- **bisect 用 `git worktree add _scratch/xxx_wt <commit>` 开树**，定位完
  当场 `git worktree remove --force` + `git worktree prune`。裸 `cp -r`
  出来的树不带注册，事后没人记得它是谁的；曾清出 b3_wt~b22_src 二十来棵
  整树（每棵 250M+），其中还有已掉注册的 worktree 残骸——目录在、
  `git worktree list` 没有，就是纯垃圾。
- **A/B 对照（新旧 zanc 行为对比）复用固定目录名** `_scratch/zanc-good` /
  `_scratch/zanc-mine`，下次覆盖使用，不新起名字；对照一结束两个目录就
  是垃圾，当场删。stdlib 整树快照同理——head-stdlib、pristine2、
  stdlib-fixed 这类一次名快照只会越攒越多。
- **下载的 SDK/源码包先解压验证、随即压缩包与解压副本二选一**（llvm-dl
  与 ohos-probe 的双份并存一次占 10G）。要长期留的大件写一页 README
  （是什么+怎么再取），`scripts/clean_scratch.ps1` 会跳过带 README 的
  条目，其余按 7 天清。
- **提交前自检**：本次会话在 `_scratch` 造的东西还在吗？在，删掉再提交。
  会话验收只看"任务完成"，没人替你收尾。

## wasm32 默认栈只有 64KB——GUI 深递归必打穿，症状是"乱指针"不是"栈溢出"（2026-09-11）

> gui_gallery wasm 版随机崩在 measure/strcmp/str_retain：野字符串指针
> （a=0x72='r'）、emmalloc 块头被清零、缓存 payload 变成别的 JSON 文本——
> 全是**栈向下溢进静态区**的二次假象，按 UAF/堆损坏查了一整轮都白查。

- **根修已入库**：main.c 的 wasm-ld 命令加 `-z stack-size=4194304`。lld 默认
  64KB，对 `Control_RenderTree × 样式缓动 × StyleSheet` 这种深度递归远远不够
  （原生 x64 有 8MB 所以永远复现不了 wasm 侧的问题）。栈只占线性内存地址
  空间，V8 惰性提交页面，4MB 不花真钱。
- **症状→怀疑排序要倒过来**：wasm32 上"野指针/堆头损坏"先查栈大小再查
  堆。判别法：**把 free 改成只记账不回收（quarantine），崩溃依旧 ⇒ 读侧
  （栈/类型错），崩溃消失 ⇒ 真 UAF**。本次 quarantine 后崩溃照旧且指针
  大于内存顶——曾经合法的指针永远不会超顶，必是被写坏的值。
- **有效侦插三件套**（都进 zanrt_gui.o 编一次）：① `--wrap=strcmp` 的
  `__wrap_strcmp` 打印界外实参（wasm 没有 `__builtin_return_address`，调用方
  看 V8 栈）；② emmalloc free 前置 ptr/size 合法性检查+分配环形账本，
  BAD FREE 时倒出来；③ 每 64 次 measure 全量 emmalloc_validate。注意
  `--wrap` 需要**手工 wasm-ld 链接**（zanc 的命令行不带它），libzigc.a 是
  单成员大对象不能删成员，全局重定义 strcmp 会 duplicate symbol。
- **手工复刻链接看归属**：zanc 删 app 对象（`<out>.o`）在链接后，竞速
  `cp` 抢一份，再按 main.c 6027 的顺序手工 `wasm-ld -Map=` ——map 直接
  告诉你 `free` 来自哪个归档成员。归档成员解析：对象文件定义 > 先扫描的
  归档；改归档不生效先怀疑"定义在别的输入里"。
- **w32adapt 签名串格式**：首字符=返回类型（v/p/j/其他=i32），其余=参数
  （p=ptr, j=i64, 其他=i32）。`"iiiii"` 是返回 i32+4 参，不是 5 参——
  摆过一次乌龙。nint 在 IR 恒为 i64（irgen.c TYPE_NINT），C 侧 iptr 是
  i32，wasm-ld 的 signature mismatch 告警就是这对宽度差，逐符号进表。

## wasm32 H5 文本两坑：中文全成"?"是字体面没盖住；"2G 内存"是 V8 预留不是真用（2026-09-11）

> gallery 网页版中文全画成 "?"——不是编码，是 ui.ttf（Segoe UI）cmap 里
> 中/文/✓ 全是 glyph 0，ft 路径对未覆盖 cp 回退画问号。任务管理器 2G 也
> 不是泄漏：无 max 声明的 memory32，V8 就预留数 GB 地址空间。

- **wasm 字体回退面**：`ft_face_for_cp` 的 wasm 分支只认 `/fonts/ui.ttf`，
  拉丁面盖不住 CJK。根修：首遇未覆盖 cp 时 `FT_New_Face("/fonts/cjk.ttf")`
  一次并进 `g_ft_fb` 链（对齐 Android fonts.xml 模式），文件缺失则记
  `cjk_tried` 不再重开，纯拉丁包不涨足迹。验证只认浏览器截图（侧栏
  组件/EN·中 chip/✓已复制 三处齐活才算过）。
- **CJK 字体子集配方**：`python -m fontTools.subset msyh.ttc
  --font-number=0 --no-hinting --layout-features='' --unicodes=U+0020-007E,
  U+3000-303F,U+4E00-9FFF,U+FF00-FFEF,U+2600-27BF,...` → 6.2MB TTF 盖常用
  区；全量 msyh.ttc 19.7MB 别进包。TTC 用 --font-number=0 取第一面。
- **--max-memory 收口预留**：wasm-ld 命令加 `--max-memory=536870912` 后
  模块 memory 段带 max 声明（min 11MB/max 512MB），V8 只预留 512MB，宿主
  JS **零改动**（memory 仍是模块导出，不用 --import-memory）。增长越界会
  trap，所以上界别贴着实测给，留一个量级余量。
- **wasm 冒烟在 node 跑**：`WebAssembly.instantiate(bytes,
  {wasi_snapshot_preview1: Proxy})` + `ZanWASI(fsBackend, out).attach()`，
  fsBackend 要有 statSync/readFileSync/writeFileSync（缺 writeFileSync 时
  path_open 创建文件会静默失败，别误判成编译问题——tests/wasm32 的断言本
  是"编译+链接"，端到端跑通要自备内存 fs）。

## wasm32 回调地址跨界：(nint)Method 必须垫 C 形 thunk，w32adapt 表逐符号跟（2026-09-11）

> wxprobe 在真浏览器首帧即陷 "null function or function signature mismatch"：
> App.PumpGuarded 把 `(nint)App.GuardBody` 交给 C `zan_gui_guard_call`，C 侧
> `void(*)(void*)` 的 call_indirect 只认一个 i32，而 Zan 函数表项还是 Zan
> 形状——nint 参数在 wasm32 是 i64，参数没被用到时甚至整参被丢。

- **两层各修各的，缺一即陷**：① irgen_expr.c 的 `(nint)Method` cast 点，
  wasm32 先过 `emit_wasm_cb_thunk` 合成 `__zan_cb_thunk.<名>`——(i32)->void，
  body 把 i32 ZExt 回 i64（callee 参数是 i32 则 Trunc）、0 参 callee 直接调，
  每 callee 只合成一次——再把垫片地址交给 C；② w32adapt 签名表补
  `{ "zan_gui_guard_call", "iii" }`，direct-call 层的 (nint,nint)→(iptr,iptr)
  才有适配项。缺任何一层，wasm-ld 只给一条 signature-mismatch warning 并留
  trap stub——**warning 不是噪音，是排期在首次调用那一刻的 trap**。
- **为什么原生 x64 永远不暴露**：宽度差在寄存器里重叠、调用约定不查参数
  个数，i64/i32 传参恰好等价。这族问题只在 wasm32 靶显形，native 全绿
  ≠ wasm 安全；判别法就是 wasm-ld 的 signature mismatch 告警清单。
- **async 未捕获 die 块具名**（irgen_async.c `emit_eh_rethrow_current`）：
  原来只打裸行 "Unhandled exception"，设备控制台上没法归因。现在字符串
  载荷直打消息、类异常经 tid-name 注册表打类名、无异常在飞维持裸行，与
  同步 die 块对齐。探针实测：`await Boom()` 无 try 时打
  `Unhandled exception: string-payload` / `Unhandled exception: Spark`
  并 exit 1；conformance 无金样期待裸行（exc_uncaught_name 走 catch 路径）。
- **并行会话下的选择性提交**：工作树文件 = HEAD + 我的 hunk + 别人在途
  hunk 时，整文件 `git add` 是禁区。配方：`git show HEAD:<f>` 基线落
  _scratch/stage3，用 python 把**我的新块从工作树文件按锚切片原样搬进
  基线**（find 定位 + 下标切，零转义），`git hash-object -w` +
  `git update-index --cacheinfo 100644,<blob>,<path>`，`git diff --cached`
  核对只剩自己的 hunk 再 commit；别人的在途改动原样留在工作树。

## 改仓库文件的静默陷阱（2026-09-11 本轮实测，三个都真的浪费过时间）

- **行尾**：本仓 `core.autocrlf=true`，多数 `.zan`/`CMakeLists.txt`/`parser.c` 在工作区
  是 CRLF、blob 是 LF。**不要**用「读到字节里有 CRLF → 把换行全换成 CRLF」的整片
  重放模板：源文本的换行本来就带 CR，重放后每行变成 CRCRLF，`git diff` 显示整个
  文件被改写（本轮把 `tests/gui/datatable_sparse_page_test.zan` 135 行全改了）。
  正确顺序：先 `replace(CRLF, LF)` 归一化 → 改内容 → 要保 CRLF 再整体重放；
  验证靠 `git diff --numstat`：增删行数应等于你实际改的行数（本轮 2/2 才对）。
- **工具传输会把双反斜杠折叠成单反斜杠**：替换片段里想要两个反斜杠，到执行时可能
  只剩一个，于是 old == new、`str.replace()` 静默 no-op——脚本照样打印 "patched 5"，
  文件一个字节没变（A290 的转义修复就这样空转 4 次，`zanc` 一直报同一个错，还以为
  是别的会话在还原文件）。含反斜杠/引号的替换一律用 `chr(92)` / `chr(34)` 在脚本里
  拼，写后做两件事：**md5 前后对比**、**断言 old 计数归零**。
- **不止双反斜杠：\r / \n 这类同样会被折成真的 CR / LF**（本轮两处：
  `Pinyin.zan` 的注释里落了 2 个裸 CR、`TASKS.md` A301 里落了 3 个裸 CR + 1 个裸 LF，
  终端里那行字被 CR 吃掉半截才看出来）。所以写后自检不能只看 bare LF，要同时看
  「裸 CR = CR 总数 − CRLF 数」和「bare LF」，两个都为 0 才算干净；带 \r / \n 的文本
  一律用 `chr(92)` 拼。
- **收紧诊断前先全仓编译一遍**：把「静默放行」改成「报错」时，仓内本来就有靠那个
  静默行为才编过的代码。A276 去掉 `type_has_ctor` 门后，
  `tests/gui/datatable_sparse_page_test.zan` 的 `new SparseSource(pageSize)`
  （基类 `SparseServerDataSource(int)` 有 1 参 ctor、派生类没声明 ctor）由「悄悄
  丢参」变成编译错。**Zan 不做 ctor 向基类转发**（探针 `ctor_inh1/2.zan`：
  `new Derived()` 编过但基类 ctor 不执行、字段保持 0；`new Derived(5)` 在修复后报
  "no constructor of 'Derived2' accepts 1 argument"），这类调用点本就该写无参构造 +
  `Setup(...)`。改这类语义前先全档编译，连带修调用点，别把诊断再放回去。

- **改 stdlib 源文件不会让产物过期**：`run_case.cmake` 的新旧判定只比 `.zan` 源、
  `ZANC`、`STDLIB_STAMP` 三者对 exe 的 mtime，**不比 stdlib 源**。所以编辑完
  `stdlib/**.zan` 直接重跑 ctest，用的还是旧 stdlib 编出来的 exe，修复「看起来没生效」，
  很容易反过来怀疑自己的补丁。改完 stdlib 必须重打时间戳
  （`cmake --build build --target stdlib_stamp`）或先删 `build/conf_<name>.exe` 再跑档。
- **单行替换展开后会留下原来那一行**：把一行换成多行时，旧行本身还在（本轮把
  `return JsonValue.NewDouble(d2);` 换成含 `return v;` 的多行，原行残留成重复返回），
  替换后要回看上下文（`sed -n` 看几行），别只看工具回了 `replacements: 1`。

- **bash heredoc 过工具层会咬转义**：`<<'PYEOF'` 引号 heredoc 里的 python
  `'\\n'` 到执行时可能已是真换行，`s.count(anchor)` 静默得 0——出现过
  "anchor count 0" 而文本明明在文件里（repr 都能看到了还 count 0，就是
  传输层改了脚本字节）。多行 C/Zan 块搬运别走字符串字面量：**从工作树
  文件按锚切片、原样拼进基线**（find 定位 + 下标切），零转义零风险；
  写完断言 count==1/块内标记存在，再 md5 前后对比。

## 解析器别丢 token 原文本：格式化输出 ≠ 无损（A295，2026-09-11 已修）

- 场景：`JsonValue.ParseNumberToken` 为性能把「含 . / e 的数字」直接转 double 且不存原文本，
  `AsString`/`ToJson` 回落到 `Convert.ToString(numD)`。后果不是「格式不同」而是**语义错误**：
  `1e2` 输出成 `100`，于是 JWT 的数字日期校验（要求 token 逐字符都是数字）把
  `{"nbf":1e2}` 当合法的 100 接受（conformance 期望 invalid nbf）；>18 位整数走 double
  分支还会丢精度（输出 `1.23E19` 这种近似值）。
- 规则：解析时把 token 转成数值只是读取侧优化；**只要还有 AsString/ToJson 这类文本出口，
  就必须把原文本一起留下**，两者不能互相替代。
- 零成本修法：该分支本来已为 `DoubleOf` 切过一次子串，复用它填 `numRaw` 即可（热路径
  ≤18 位整数一行未动，分配次数不变）。

## 静默截断的指纹：rc=0 但输出缺行（2026-09-11 实测）

- Zan 调度器在**协程全部 parked** 时正常退出（rc=0）。所以「丢唤醒 / 某个 await 永不 resume」
  不崩不报错，只会**少打印后面所有行**——conformance 报 output mismatch，人工看像「输出少
  了几行」，容易误判成打印/缓冲问题。
- 判据：mismatch 且行数变少、进程 rc=0、输出停在某个 await 之后 → 先怀疑 parked 协程
  （IO 完成丢唤醒、对端没按用例假设建连），别去查 Console/缓冲。
- 本轮实例（未修的 A298）：`http_forwarder_stream` 同一个 exe 跑 6 次得 3 种形态
  （停在 stream-progressive 的 4 行 / 11 行但内容错 / 全空），**全部 rc=0**；线索是用例的
  upstream 只 accept 2 个连接，转发器一旦复用连接就停在 accept 上，Main 的下一个 await 永不 resume。

## async × 异常：unwind mark 是每个 handler 的义务，漏一个就殃及全部帧（A293，2026-09-11 已修）

- 症状：async 方法内**嵌套的非 async 函数** throw、由 awaiter catch 后，awaiter 的
  所有局部 NULL/0。最小红案：嵌套 sync throw + root catch + 一个 string 局部。
- 根因（不在 trampoline 本身）：`emit_async_eh_prologue` 给 `$resume` arm trampoline
  时**没写 handler 槽的 unwind mark（tmp 栈深度）**——mark 槽是 calloc 的 0。try 的
  arm 写了 mark，trampoline 与 frame 内 try 的 re-arm 都漏了。thrower 是普通 sync 帧
  时走 `emit_eh_unwind_to_handler(top)` → 读到 mark=0 → `__zan_eh_tmp_unwind(0)` 把
  tmp 栈**从 0 起全部**注册槽释放并置 NULL——awaiter/root 帧的 owned 局部（string、
  List）全部殃及。
- 关键对照（定位时靠它剪枝）：throw 在 async 体内（走 trampoline land、rethrow 无
  unwind）不坏；**嵌套 sync 帧才坏**；隔几个 sync 帧无关、await 是否真挂起无关。
- 修：trampoline/rearm 两处 arm 补 `store tmp_top → mark_ptr(t1)`，与 try arm 对齐。
- 教训：**handler 栈上每个写 top 的地方都必须同时写自己的 mark**——这是「谁 arm
  谁负责记账」的契约，靠 calloc 0 兜底的槽 = 深度 0 = 「释放全世界」。新 handler
  加进 EH 机制时，把「arm 三件套」写成一个小 helper（top++、写 mark、setjmp），
  别让三步散在三处。

## 拉入闭包的链扫描：一个变量被两处清空 = 镜像全死（A300，2026-09-11 已修）

- 症状：`await Task.WhenAll(...)` 编不过（TaskJoin 拉不进来），但**没有 namespace 的
  同款用例能过**、显式拼 `TaskJoin.X` 也能过——「形状相关」的假象差点把人引向
  「namespace 影响解析」的邪路。真差异：能过的用例都在别处裸名拼写了 `TaskJoin`。
- 根因：`pi_seed_source`（main.c）维护点号链 `chain`（链头=第一个段）。两处清空叠加：
  switch 的 `default:` 对 `TK_DOT` 也执行 `chain=NULL`（点号自己抹链头），switch 后又有一行
  「非 dot 就清 chain」把 `TK_IDENT` 分支刚设的链头**立即**抹掉。两者叠加 chain 恒 NULL，
  ns_root 分支与 Task.WhenAll 镜像全是死代码——**靠一个从不生效的分支兜底的特性=不存在**。
- 修法：三种 token 各司其职——TK_IDENT 设链头、default 清链头、`case TK_DOT: break` 保留；
  删掉冗余的后置清除。教训：**「这个 fallback 分支最后一次真正生效是什么时候？」——
  说不出来就去插桩验证（ZAN_SEED_DEBUG 打印 prev/chain），别信注释**。注释写得越笃定
  （「mirror the rewrite or TaskJoin.zan is never pulled」）越没人怀疑它是死的。
- 插桩小坑：往 C 源里加 `\n` 的 fprintf 时，工具传输会把 `\\n` 折成真换行直接
  咬断字符串字面量（本轮踩中，编译错误 expected expression 才发现）；写完先看 repr。

## leakcheck「仍可达」与停服排水（A302，未修）

- Zan 的 leakcheck 对**仍可达**对象也报红（不区分丢失/仍被持有）。服务端对象
  （listener、连接、池）在 Stop() 后需要泵协程自然走完（accept 返回 -1、EOF 关链路）
  才不可达；测试里的固定排水（20x10ms）在负载下可能不够，`rc=0` 但 leakcheck 记红。
- 判据：leak 行全指向服务端对象分配点、主输出全对 → 停服排水不足或 Stop 语义不
  可等待，不是真泄漏。修法方向：Stop 返回可等待句柄（服务端 join 自己的泵），
  而不是让每个用例猜排水时长。

## reactor 关 fd 必须"先摘注册后 close"，且 Windows 上探针证不出差异（A291⑤，2026-09-12 已修）

- rt_io 的等待者表按 fd 号索引（epoll/kqueue 槽位、select 链表都一样），close 后
  OS 会把同一个 fd 号立刻复用给新连接：老 waiter 还挂在表里，新连接一有活动就被
  唤醒，数据错投到上一条连接。修法不是句柄表重构，而是**关闭通知钩子**
  `zan_io_close_notify(fd)`：Socket.Close 在 close/closesocket **之前**调用，把挂
  在 fd 上的就绪等待者以「对端关闭」形态（recv 0 / accept -1）唤醒并摘除注册。
  单一收口点覆盖 Close 全部调用点，不必逐个改 63 处。
- 各后端形态差一眼看清：epoll/kqueue 走 EPOLL_CTL_DEL/EV_DELETE 后 io_take 双轮
  收割；select 回退走 g_io_entries 链表 io_mark_dead + io_flush_dead；**Windows
  无操作**——IOCP 的 CancelIoEx 已让挂起 overlapped 以 0 字节完成，钩子是给 POSIX
  的；wasm32 空桩补符号。
- 坑：在 Windows 上给这类修复做"红基线"是证不出的——fd 复用时序探针加不加钩子
  输出完全一致（CancelIoEx 早兜住了）。别据此判定修复无效，可观察价值只在 POSIX
  （epoll 侧至少编译验证：WSL gcc 编 rt_io.c 过即可），行为差异写进 TASKS.md 叙述。
- stdlib 侧接运行时钩子的定式：`[DllImport("crt", EntryPoint="zan_io_*")]`
  声明成 Socket 的私有静态 extern，在 Close 这类单一入口里先钩后关；运行时四个
  后端 + wasm 桩都要有符号，否则任一目标平台链接就炸。

## Worker 控制口令牌/记录文件：验证要靠 A/B 记录内容，别盯进程行为（A291②，2026-09-12 已修）

- stdlib 要 CSPRNG 用根命名空间的 `RandomNumberGenerator.GetBytes(n)`（Windows
  RtlGenRandom / POSIX /dev/urandom，无弱回退）；**别 `using System.Security.Cryptography`**
  ——`using X.Y` 按目录整体拉入会把 20 个加密文件编进每个引用者，RNG 当年搬出
  Cryptography 就是为了这个。几字节的 hex 编码手写即可。
- 令牌/凭据类修复的可观察面是**记录文件内容**（temp `zan-worker-<AppId>.ctl`，
  AppId=exe 路径哈希）：A/B 跑真实 master 写出的记录，修复前 `port pid-秒`、
  修复后 `port 32hex`，一锤定音。POSIX 权限半（chmod 0600）在无 Linux 运行时
  的 Windows 盒上做语义等价验证：WSL gcc 编同参数 chmod 片段看 mode=600。
- 盒子上多进程 master 行为有噪声（本机探针实跑出 worker 甄别 FATAL）：判定
  「与我改动无关」同样用红基线——stash 修复重编重跑，FATAL 新旧一致即既有。
  注意 worker 的 stdout 进 logDir 不进控制台，控制台里的 FATAL 未必是被杀进程
  的遗言；以记录文件内容和 rc 为准。
