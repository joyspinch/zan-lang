---
name: zan-compiler-internals
description: zanc 编译器内部（parser/checker/irgen/nsresolve）的定式与坑——Dict 内建布局契约、LLVM select 死臂泄漏（用 branch+phi）、delegate 两形态与 wasm32 ZAN_CLOSURE_TAG 碰撞、nsresolve 冲突改名丢泛型实参（全量输入 vs 按需拉取行为不同）、限定名简单名回退（错命名空间的发射被用户同名类击穿）、ARC 所有权判定内建优先于 extern 借用、stdlib 按需拉入的坑（AST 真引用闭包、stdlib 输入自我遮蔽、潜伏缺 using、重臂 Bootstrap 注册制）、LLVMIsConstant/llvm.global_ctors/PE 数据分节 $ 命名等发布体积分节陷阱、GNU ld PE 把 .pdata 当 GC 根、交叉工具链 .o 重出配方、conformance 处置四分法、scratch 卫生（bisect 用 worktree 即用即删）。做或改 src/compiler/*、交叉运行时对象、conformance golden、追发布体积、动 stdlib 重组件目录或 ControlFactory/App 拉入面时使用；仓库内有同名项目级版本，会自动优先。
---

# zanc 编译器内部定式与坑

> 每条都是踩过坑、探针验证过的契约。

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
  前科（A258）：ada73452 的增量 backward-shift 修复在**共享前缀键序**（同簇
  探测链）下把索引写坏，后续 Remove 的探测循环无空槽出口直接死循环（CPU
  100%）——形状钉在 `tests/conformance/dict_remove_shared_prefix.zan`。
- **Keys/Values 是编译期合成的视图，不是声明的字段**（A314，2026-09-13 已修）：
  `dict.Keys[i]` 曾整表达式折叠成常量 0——`emit_expr_index` 的成员分支只认
  `member_access_field_type`（声明字段），合成视图解析失败后落进「静默折叠
  常量 0」兜底，且 `d.Keys.Count` 走另一特判一直正常、极具迷惑性。凡新增
  「名字像成员」的内建视图，检查 `emit_expr_index`/`member_access_field_type`
  是否需要回落 `infer_expr_type`（其 member 处理才是全量）。折叠常量 0 的
  IR 指纹：槽初始化 null 后紧跟 `store i64 0`。
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
- **`LLVMIsConstant()` 不是「只读全局」**（2026-09-15）：它问 ValueKind
  是否 Constant 子类，而 GlobalVariable 继承自 Constant——对任何全局恒
  真。只读标志要用 `LLVMIsGlobalConstant()`。弄错会把可变的字符串字面
  量缓冲（`__zan.deobf` 构造器启动时要写）放进只读 .rdata，发布程序启
  动即 SIGSEGV。
- **别给 `llvm.*` 全局设 section**：`llvm.global_ctors` 靠后端特殊降级
  生成 `.ctors`；手动 `LLVMSetSection` 一旦碰到它，全部静态构造器（字符
  串反混淆、运行时注册表）静默失联，症状是启动即崩且 .ctors 分节消失。

## 发布体积：数据逐符号分节与链接器 GC 的边界（2026-09-15）

irgen_emit.c write_obj 在 publish 档给全局也按符号分节（`.rdata$<名>` /
`.data$<名>`），与既有的逐函数 `.text.<名>` 对称。背景实测（空窗 GUI 探
针）：单一 .rdata 块里一个活字符串就把 65 张虚表 + 全部 ARC 表钉活，进而
钉住全部虚方法——3624 个函数分节只被回收 13 个。四条定式：

- **COFF/PE 数据分节必须 `$` 命名**：ld 把 '$' 前的基名合成一个输出段、
  每个输入段保持独立可回收。点号命名（`.rdata.foo`）会让每个数据分节成
  为独立输出段，PE 按 4KB 对齐——同一探针 +3.8MB。ELF/Mach-O 用点号。
- **GNU ld 在 PE 上把 .pdata/.xdata 当 GC 根**：带展开表的函数永远收不
  回（最小 gctest：零调用的 `dead()` 在 `--gc-sections` 下存活）。x64
  win-gnu 上 LLVM 给所有非叶函数发展开表 ⇒ PE 发布的 .text 不缩，本改动
  在 PE 净效果≈0；ELF 上 ld.lld 正常回收（实测 193 分节 / .text −280KB）。
  PE 要真裁剪：换 lld-link `/OPT:REF`（关联 COMDAT 语义正确）或发布档发
  no-unwind 表——都未做，见 TASKS.md 挂账。
- **数据分节只是使能层，语义可达才决定死活**：空窗探针在 ELF 上
  `Html_Clone → ControlFactory_Create → new ChartHost()` 一条链把整个组
  件世界拖活（App_ctor 自身就引用 45 个类描述符 + ImageHttp 拖活 TLS）。
  裁剪大头在 stdlib 解耦（ControlFactory 的重臂照 CEF/WebView 的
  Bootstrap 注册模式移出主 switch），不在链接器。
- **测量配方**：`zanc --publish -o x.a`（静态库输出路径保留完整对象，链
  接后 obj_tmp 会删）→ `llvm-ar x` → 用 build/ld.exe（PE）或 ld.lld
  （ELF）手动重链，加 `--print-gc-sections`；缺驱动符号时补
  `-z undefs --noinhibit-exec`（ELF）即可只量回收不产出可执行。

## auto-stdlib 拉入的真实语义与重臂注册制（2026-09-16 落地）

- **拉入两段制（2026-09-16 起为 AST 真引用闭包，旧"using 整目录 glob"
  已废）**：`using X.Y.Z;` 只把 `stdlib/X/Y/Z` 变成**候选集**（真词法器扫
  top 声明名/using/ext 标记，不 parse）；候选文件仅当 top 声明名是"活名"
  才入图。活名 = 已入图文件 AST 里**类型位置**（TYPE_REF 各段）+
  **成员访问链根**（写全的 `Static.Member` 头）+ qualified type 段；声明名、
  链尾成员名、字符串字面量永不旗标。`ZAN_NO_PULLIN_FILTER=1` 一键回旧
  glob 做 A/B 归因；`--emit-symbols`（IDE 符号索引）永远全量。显式传入的
  stdlib 输入，其**自身目录**也进候选集（pi_reach_input_dir，旧
  auto_include 输入命名空间 glob 的按需版）。实测 NewProject 2.6s/75 文件
  （旧 glob 3.2s），空窗 GUI 68 文件。改"谁被编译进"先看旗标链，再谈 using。
- **stdlib 输入自我遮蔽坑（2026-09-16，datatable 5 例红）**：显式传入的
  stdlib 文件在 entry 循环被打 `user_decl`（本意：用户文件遮蔽 stdlib
  同名类，防冲突改名），结果它自己写全的 `DataTable.CellTextRouted` 调用头
  被 `pi_flag_istr` 的 user_decl 检查一并压制 → 同目录 30 个 partial 部件
  一个都拉不进，报 `'DataTable' has no member 'CellTextRouted'`。
  修法 `pi_seed_stdlib_input`：stdlib 树内的输入不做 user_decl 遮蔽（判定
  复用 pi_reach_input_dir 的 stdlib 根叶名比对）。教训：`user_decl` 的语义
  是"**用户**声明遮蔽 stdlib"，不是"所有输入都遮蔽"——stdlib 输入提到
  自己的名字（partial 联动）是真引用，必须照常旗标。
- **AST 播种要连 ns_root 一起搬（2026-09-16，pullin_qualified_escape
  四变体全红）**：`ns_root`（using/namespace 的每一段 = 已知命名空间根）
  原本是词法播种 pass 1 顺手填的；播种改走 AST 后没人填了，
  `Gui.App.ISqrt(9)` 的链中段 `App` 逃逸不出 user_decl 遮蔽 → stdlib 的
  Gui/App.zan 拉不进。修法两件套：pi_parse_and_seed 对**每个** parse 的
  文件（不止 entry）把 using 段与自身 namespace 段标 ns_root；
  AST_MEMBER_ACCESS 判链根是 ns_root 时中段走 `pi_flag_qualified` 直置
  flagged（镜像词法回退的直置语义；TYPE_REF 段走 pi_flag_ident(NULL)
  本来就直置）。教训：把一个机制从词法搬进 AST，它顺手维护的**旁路
  状态**必须一起搬，否则语义静默丢失、只有专门守门用例能抓到。
- **partial 部件联动靠真引用，别钉目录**：曾试"显式输入所在目录整目录
  无条件入图"（钉住）——错：一次把 30 个部件全带进，违背按需编译本意；
  且不必要——输入文件自己写全的 `ClassName.Member` 调用头旗标即可不动点
  闭环。凡遇"部分成员缺失"，先确认引用方是否写了全名、是否被 user_decl
  压制，而不是给目录开后门。
- **潜伏缺 using 会被解耦暴露**：CodeEditor/FilePicker/SceneDesigner/
  Designer 用 `Lang.Tr`（System.Globalization）却从不声明 using——过去
  ControlFactory 的 `using Gui.Component.DataTable;` 把 DataTable.Lang.zan
  带进每一次编译，顺带把 Globalization 拉进来，坏依赖被掩盖。拔掉顺带
  链后当场 unresolved。**规则：谁用谁声明**；拔任何 using 前先 grep 该
  目录独有声明的外部用户。
- **重臂注册制（重家族一律照此办理）**：ControlFactory 主 switch 的
  `new ChartHost()`/`new DataGrid<T>()` 分支 = 语义钉子，把 0.5-0.8MB+
  的组件目录钉进每个 GUI 程序；App 自持 `WebViewBox linkBox` 字段 +
  ctor 装 ChartTheme.Css 同理。范式：类型从核心文件里搬走/改为
  `*Bootstrap.Install()` 经 HeavyControls 注册（先注册后可用），行为
  钩子用 delegate 槽反转（App.SetLinkNavigator ← WebViewBootstrap），
  资源装载懒触发（ChartTheme.EnsureDefault 挂在 ChartHost 布局/绘制）。
  设计器生成代码（GenForm）的无条件重家族 using 改为按设计树实际 kind
  发射。实测：空窗 GUI 272→213 文件；NewProject singleFile 发布
  13.4MB→4.38MB（OpenSSL/Chart/DataTable/WebView 全部退出编译图）。
  **槽反转必须全量清点旧调用点（2026-09-16，conformance_gui_html_runtime
  连环红）**：`Html.Clone` 直调改 `App.CloneTree` 槽反转（null 槽返
  null）后，凡是走克隆通道的宿主都得先 `Html.Install()` 注册实现——
  测试只补了 zform_dynamic/compref_designer，html_runtime_test 漏补，
  模板行展开拿到 null 行，ChildCount 断言红且行内越界段错误。归因
  A/B 三步定式：先 stash 自己的编译器改动重跑（排除编译器）、再 stash
  在途 stdlib 重跑（排除并发在途）、最后 HEAD 全基线复跑定谳"既有红"
  还是"改造漏网"。凡给"直调 → 槽"的改造收尾，grep 旧函数名的**全部**
  调用方逐个补 Install，别只补测试清单里点名的那几个。
- **入口契约陷阱**：设计文档入口生成 `Name.OnLoad(form)` 调用，业务侧
  必须提供同名静态方法；只传 .html 不带同名 code-behind .zan 时报
  "type X has no method OnLoad"——是输入列表不全，不是生成器坏了。
- **__DesignMain 回退是 irgen 的消费义务，发射端改了消费端必须同窗口
  落地（2026-09-16，NewProject 发布连爆 undefined main/WinMain）**：
  GenForm/GenScene 只对主设计稿发射 `__DesignMain`（不是 `Main`，免得
  偷走用户入口），irgen Pass 3 必须先找显式用户 `Main`、没有才落回
  `__DesignMain`。两提交只落了发射端，Pass 3 还只认 4 字节 "Main"——
  设计稿+partial code-behind（无用户 Main）的工程编译通过却链接爆炸
  （`undefined symbol: main` / `undefined reference to WinMain`）。
  排查要点：链接期 undefined main 先数一数输入里有没有入口发射
  （ZAN_KEEP_GEN_REQ=1 看 out.json 里 `__DesignMain`），别往 mingw CRT
  /subsystem 方向猜。另注意成员名比较的 `len` 要与字面量同步
  （"__DesignMain" 是 12 字节，写 13 永不命中——静默失配无任何诊断）。

## stdlib 肥边治理：独立类分片 + 槽反转 + 实例方法组注入（A332 肥边③④，2026-09-17）

- **重文件被"字段类型"钉进图，与被调用钉进同罪**：`HttpFramer` 有个
  `TlsStream tls` 字段（+`CreateTls` 工厂），Worker.zan 拼了
  `WsOpcode/SseConnection/MqttBroker`——字段类型与活引用一样是编译边，
  把 TLS 全家（连带 Base64/Sha256 与 6.4MB ssl/crypto 驱动拷贝）、ws
  编解码（866 行）、SSE（285 行）、MQTT broker+client（1299 行）拖进
  **每一个** HTTP 程序。实测纯 HTTP 服务端探针 43→34 发布文件、
  exe 679,936→528,896、bundled driver 行消失。
- **切法 = 独立类分片 + 静态槽 + Install opt-in（partial 不可用）**：
  实现搬到独立文件独立类（WorkerWs/WorkerSse/WorkerMqtt/HttpFramerTls，
  **绝不能写 partial Worker**——分家名会拉全家，A332 肥边②已验），
  核心留 `static ProtoEntryFn ws/sse/mqttEntry` 静态槽，Dispatch 经槽
  分发；RunAll 前置 `CheckProtoEntries()`：专用协议槽空即打印
  "needs WorkerXxx.Install()" 并 exit(1)（比首连接静默断开可诊断得多；
  http 端口设了 ws 回调但没装分片只告警，明文语义不受影响）。
  **CheckProtoEntries 只能读槽、不能拼分片类型名**——拼了（哪怕只为
  报错文案的代码路径）就把分片拉回图，槽白拆；报错文案写进字符串没事。
- **delegate 实例方法组绑定可用，async lambda 捕局部会段错误（探针
  实测）**：`framer.recv = stream.RecvIntoAsync;`（把实例方法组赋给
  `async delegate int(string,int)` 槽）编译且运行正确；但
  `delegate(string b,int c){ return await s.Recv(b,c); }`（async lambda
  捕局部 `s`）**编译通过、运行 SEGV**。字节源/回调注入一律走实例方法组
  或"显式 self 参数的静态委托"，别写 async lambda。
- **ZAN_PULLIN_DEBUG 日志两坑**：① 日志里混着"代码生成器自举"的另一次
  zanc 闭包——`zan: compiling code generators (first use; cached …)` 之后
  的 `incl Gen*/Web/Html` 行是生成器子编译的图，不是本程序的图（首次会
  因 stdlib 变更缓存失效而打印，曾被误读成"删了反而多拉 14 个文件"）；
  程序自己的图以下一行 `Published N files` / exe 尺寸为准。② 日志带
  CRLF——从日志抽文件列表再 `grep`/`[ -f ]` 探测时必须先
  `tr -d '\r'`，否则路径带 \r 全部 MISS，得出"图内没人引用它"的假阴性
  （本次差点据此推翻真实触发链）。
- **改 Worker/HttpFramer 这类"唯一宿主"的收尾清单**：grep 全仓库旧 API
  调用点逐个补（`CreateTls`→`HttpFramerTls.Create`、`onSseSubscriber`→
  `WorkerSse.onSseSubscriber` 静态字段、`conn.Push`→`WorkerWs.Push`），
  examples/templates/conformance 三处都要；ws_loopback/ws_protocol_gate/
  mqtt_loopback/sse_stream 四个 conformance 是这次拆分的守门用例。


## delegate 两形态与 wasm32 的 tag 碰撞

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
- **根修兜底：wasm 链接加 `--table-base=2`（main.c wasm-ld
  命令行）**。lld 默认表基址 1，地址取函数的表索引奇偶皆有——任何漏网
  的裸 fn delegate（如 `inp.Size = vm.x` 合成 Binding 访问器曾是裸指针，
  `emit_binding_acc_delegate` 已按 mg 形状收口）都会撞 tag：索引 N 被当
  tagged 记录 untag 成 N-1，间接调用恰好落进相邻函数。表基址挪到 2 后裸索引恒为偶，native 的「偶数才合法」
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
  自己的临时量，工作线程可能还没跑。
  **通则：任何把 delegate 存下来留给"另一次调用 / 另一线程"执行的 runtime
  入口，都要对到达时的那个值补 retain、在真正的执行点补 release**——这正是
  zan_abi.h store-family 契约的要求。

- **语义修复后要扫"断言旧行为"的每一处，生成器发出的注释也算**：
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

## stdlib 生成器（GenForm/GenHtml）：分块发射空集与直通排除清单（2026-09-14）

- **分块字面量发射必须单测空集**：为绕单字面量 4095 词法上限把
  `Css = "<全串>"` 改写成 while 分块相加（GenHtml），css 为空的文档
  （无 `<style>` 的运行时片段）一段都不产，拼出裸 `Css = ;`，整个生成
  类解析失败、conformance_gui_html_runtime 在编译步就红。空输入是分块
  改造的必测边界——循环后补兜底初值（`if (cssLit == "") { cssLit =
  "\"\""; }`）。坑出处：f69f4a99 分块改造当天回归，被同文件另一缺陷
  的编译错误 mask 了一层才暴露。
- **直通回退的排除清单要与"上方已消费分支"同步维护**：GenForm 建模键
  直通循环按排除清单跳过已消费键，`label`（已被 SetDesignText 分支消费
  的设计注记）漏在清单外，便对没有该属性的控件（容器）重复发射
  SetProp，掉进 SetProp 兜底 `AddClass` 退化成样式类——容器
  `GetProp("class")` 混进标题文案。加消费分支时同步加排除键；这类
  静默降级只有 GetProp 断言型 conformance 抓得住，输出型金样抓不住
  （gui_zform_control 的 `container class` 断言就是为此写的）。

## nsresolve 冲突改名丢泛型实参：只在"全量输入"构建炸（lambda_87，2026-09-13）

- **症状**：IDE 构建（glob 全 Gui 树）报 `LLVM verification failed in
  lambda_87: Incorrect number of arguments passed to called function!`，
  `call void %dlg.fn(ptr %clo.rec, ptr %load4)`；同一份 Gui 源码在
  conformance/单文件编译全绿。挂掉的是 ChildWindow.WireNode /
  Html.WireArg 的 `Action<string> aa = ...; () => { aa(arg); }`。
- **根因链**：`Gui/Event.zan`（namespace Gui）与
  `Gui/Reactive/Events.zan`（namespace Gui.Reactive）各有一个零参
  `delegate void Action()`。nsresolve 对跨命名空间同名声明双方 mangle
  （Gui_Action…）并改写所有解析到的引用，但 `type_ref.name` 只是基名、
  泛型实参在独立列表里——**改写基名天然丢实参**。Gui 命名空间文件里的
  `Action<string>` 于是被绑到零参 Gui_Action（binder 侧 scope 命中声明、
  静默丢 args），lambda 捕获记下 pc=0，invoke 按 argc=1 发射 → verifier。
- **为什么只有全量构建炸（本轮最大陷阱）**：conformance/单文件走
  auto-stdlib **按需拉取**，Gui.Reactive 不入编译 → 无冲突 → 不改名 →
  一切正常；glob 全量必中招。**拉入面一变，nsresolve 的冲突集就变，
  同一文件在不同输入集下类型解析结果不同**——"单文件探针无法复现"不
  等于"编译器无缺陷"。修法：resolve_ref 按泛型配对规则过滤（引用带
  实参而候选声明的 type_params 个数不符 → 不改写、回落内建 Action<T>；
  bare 形式照旧改写）。conformance 锁：ns_conflict_generic_arity
  （主文件 namespace Widgets + helpers/ 子目录附加第二个命名空间，
  ZANC_ARGS 附带——GLOB 不递归，helpers 不会被当成独立用例）。
- **诊断三板斧（verifier "Incorrect number of arguments" 通用）**：
  ① `ZANC_DUMP_BAD_IR=1` 拿挂掉函数的 IR（irgen_emit.c 现成钩子）；
  ② 临时给 emit 点加 arity 审计（`LLVMCountParamTypes(调用函数类型)`
  vs `LLVMGetNumArgOperands`）判定哪一臂、差多少；③ 临时全模块 dump
  （LLVMPrintModuleToString）后按 `%clo = call ptr @zan_rt_alloc`
  的 define 反查 lambda 创建者函数与捕获槽来源——比读源码猜形状快
  一个数量级。最终 `delegate=Gui_Action pc=0 argc=1` 一行定案。
- **bisect 度量陷阱**：用"目标错误消息消失"当 pass，会把"类型错误
  提前终止编译"的运行误判成 pass（codegen 没跑当然没有 verifier 错）
  ——本轮设计文档二分的全部结论因此作废。二分前先确认每个子集都能
  走到被观测的阶段（closed set），或把 pass 判据定为"编译成功"。

## 限定名解析的简单名回退：错命名空间的发射碰巧编过，用户同名类一出现就炸（design_palette，2026-09-17）

- **症状**：GenForm 给告警件（类在 `Gui.Hmi`）发射 `Gui.Widget.AlarmBanner`
  字段与构造，设计稿编译全绿；用户项目一旦声明同名全局类，同一行变成
  `undefined type 'AlarmBanner'`——"没动 stdlib 却编不过"。实测还可能
  静默绑到用户类（继承基类的 Kind() 自报名暴露）。
- **根因**：限定名解析在限定路径走不通时**退回简单名解析**
  （using import / 可见域）。所以限定错命名空间的名字平时碰巧编过，
  "限定名"并不锁真身；用户同名类让简单名出现双绑定后回退失效。
  只有**逐段真实存在**的限定路径才遮蔽免疫。
- **修法/纪律**：生成器/元编程发射限定名必须用真实路径。QualifyKind
  加 `Gui.Hmi.*` 支路，且支路必须在 IsStdWidgetKind 门槛**之前**——
  门槛先放行裸名，探针立刻打回（Led 绑到用户类）。新增工具箱 kind
  三处同步：FormField.KindForType、GenForm（IsStdWidgetKind/IsHmiKind）、
  ControlBootstrap（Names/Make，policy_control_factory 强约束
  键=分支=Kind() 自报且标签唯一、漏 `override` 修饰符会骗过扫描）。
  回归锁：conformance_gui_design_palette（设计文档放全 25 个
  展示/工控 kind + 探针侧 Tag/Led/Dropdown 三个同名用户类）。

## 嵌套类型提升：hoist 拿走 AST 嵌套关系，binder 必须挂回宿主 members[]（namespace_qualified_call，2026-09-23）

- **症状**：`class Outer { public class Inner { ... } }` + `Outer.Inner.Value()`
  被 checker 拒绝 `'Outer' has no member 'Inner'`，而这个限定调用在旧版
  编译运行全绿——"测试突然红"，且 binder/checker 当时的 diff 都是无关改动。
- **根因**：parser 的 `zan_parser_flatten_nested_types` 把嵌套类型**整体搬到
  单元级**（宿主 AST members 里不再有 Inner），类型注册靠简单名。早期
  checker 对非标量接收者的成员 miss 只返回 type_error **不发诊断**，
  irgen 的限定名处理器按简单名接住发射，所以"碰巧绿"；后来 typo 守卫把
  该诊断放宽到所有类/结构接收者（守卫本身是对的，未解析限定名的负例
  靠它），合法的 `Outer.Inner` 一起被拒。
- **修法**：提升时在 AST 节点盖宿主戳（`type_decl.nested_host` 存宿主**节点
  指针**），binder 注册完全部顶层类型后按节点指针找到宿主符号，
  `symbol_add_member` 挂回宿主 members[]。members[] 的消费方
  （get_field_index 数槽、checker 成员遍历、property getter 查找）全部按
  kind 过滤或按名匹配，非字段符号入表不动字段布局（类型参数本来就走
  这条路）。
- **纪律**：① union 陷阱——AST_DELEGATE_DECL 与 method_decl 共用 union，
  不能写 type_decl 字段，嵌套 delegate 不盖戳。② 挂回必须在**全部注册完
  成之后**做（深嵌套 A{B{C}} 提升顺序是 C、B 追加到 decls 尾部，B 注册在
  C 之后），宿主按节点指针找、不按名字，避免同名类错挂。③ 排查"旧版绿
  现在红"时先问：当年是不是**没有诊断的静默路径**在兜底——守卫收紧只是
  让老病灶显形。回归锁：conformance 的 namespace_qualified_call 用例
  （负例孪生 fully_qualified_unresolved 必须保持红）。

## wasm32 局部数爆炸：V8 每函数 5 万局部硬上限

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

## 属性访问：裸名 static 读静默得 0，裸名实例写让 zanc 段错误

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
- **工厂延续初始化器 `Factory() { M = v }` 的成员检查曾被整段跳过（A89，2026-09-13
  已修）**：call_init 形态没有 type 节点（`new_expr.type == NULL`），checker 先
  `type = check_expr(call_init)` 拿到工厂返回类型，后面又无条件
  `resolve_type(new_expr.type)`——NULL 进去 type_error 出来，把已算好的类型冲掉，
  共享初始化器循环的 `type && type->sym` 门恒假：no-setter / 赋值兼容 / 成员存在
  **三检查全部静默**（`Maker.New() { tag = "字符串" }` 塞 int 字段、`{ nosuch = 5 }`
  都编译通过，运行期 `if (!msym) continue` 整体丢写）。**为什么**：给表达式分支补
  "后置统一 resolve" 时会吃掉前面分支已算好的类型——新增 AST 形态要么 early-return，
  要么用形态旗标门住 resolve/ctor 检查。诊断点在 `checker.c` AST_NEW_EXPR +
  `factory_init` 旗标；合法糖 `Children = { a, b }`（AST_COLL_INIT）对 getter-only
  成员豁免（读成员逐个 Add，不是写），修检查时勿误伤。档
  `tests/diag/factory_initializer_{member,readonly}.zan`。
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

## 类型位置的裸名是 AST_TYPE_REF，不是 AST_IDENTIFIER（2026-09-15）

- 泛型实参、基底列表、extends 边里的裸名形参（`Box<T>` 的 `T`）经
  `parse_type_ref` 解析，得到 `AST_TYPE_REF` 且 `type_args.count == 0`；
  **永远不会**是 `AST_IDENTIFIER`。按节点 kind 分支识别"这是不是类型形参"
  时两个 kind 都要接（TYPE_REF 零实参 = 可能是形参也可能是具体裸名，
  先查形参表再落回具体名比较）。只查 IDENTIFIER 的分支是死代码，
  `interface BoxSource<T> : Box<T>` 的提升检查首版就栽在这。
- 为什么：类型语法统一走 parse_type_ref（parser.c:407 递归吃实参），
  AST 里不存在"类型位置的标识符"节点；这一点 checker/irgen 里所有
  对类型实参做结构匹配的代码都适用。

## 字符串位的可空值类型

- C# 语义：`"a=" + int?` 合法，null 拼空串。checker
  type_is_concatable 对 TYPE_NULLABLE 递归放行元素可拼的；
  irgen emit_to_cstr_of 对 `zan.nullable.<payload>` 命名结构解包——
  has ? cstr(payload) : NULL（branch+phi，见上）。无符号元素
  （uint?/ulong?）要传 emit_to_cstr_u 的 unsigned 旗标。
- `Convert.ToString(T?)` 与插值 `"{x}"` 同样走 emit_to_cstr_u 的
  nullable 分支（2026-09-23 修复：此前这两条自建分支链把 nullable
  结构按值递进 itoa64 炸 verifier）。`.ToString()` 直调被 checker 以
  "cannot access member on nullable type" 拦下——要文本就走
  `Convert.ToString(x)`。

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

## win-arm64 交叉 rt：setjmp 降层与 rt_crash 架构门

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

## IOCP 唤醒包丢失家族与 raw socket 探针陷阱（A298 收尾，2026-09-14）

- **丢包路径的契约**：Windows 上 `PostQueuedCompletionStatus` 失败（DNS/-2
  唤醒包）会置 `g_blocking_wake_lost`，完成结果滞留 `g_blocking_done` 且
  `g_blocking_inflight` 仍计数（递减只发生在 drain/timeout）。任何驻车前
  （尤其 INFINITE 超时）必须 check-and-drain 这个 flag——`co_wait_io` 与
  `zan_io_poll` 都有了（pre-park drain 守卫）；新增阻塞等待时照抄，别只
  依赖 GQCS 失败分支的超时扫描（INFINITE 下永不可达）。POSIX 侧 wake 走
  eventfd/管道且在 select 集合里，无丢包问题——此类改动天然 Windows-only。
- **「关闭不唤醒」指认前先 A/B 四形状**：closesocket 的 IRP 取消本就补发
  完成包（aborted op 呈现为 0 字节读=对端关闭语义）。判据矩阵=本端
  raw Close / 对端 FIN × 单反应器 / `--async-workers` 多 worker；四格全
  绿就别再指认「raw 关闭不唤醒」（A298⑵ 的旧说法即此假象）。现在
  `zan_io_close_notify`（Windows）为显式 `CancelIoEx(fd, NULL)`，唤醒
  不再依赖 closesocket 的隐式取消时序；stdlib 侧 `Socket.ShutdownBoth`
  同型（SysShutdown+CancelIoEx）。
- **raw `Socket.Create*` 曾不做 WSAStartup**（2026-09-14 已修：四个
  Create* 自调 `Socket.Initialize()`）：TcpListener/TcpClient/UdpClient
  构造器各自初始化，raw 路径原本无人兜底，Windows 上 `CreateTcp()`
  静默返 -1。**socket 探针必须断言 fd>0 且 Bind==0**——否则挂在不存在的
  fd 上，`AcceptAsync(-1)` 立即返回会伪装成「苏醒」，探针假阳性（首轮
  listener 探针就因此"通过"过一次）。

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

- **extern string 不只不能释放，也不能下标（A347，2026-09-23）**：
  `DllImport` 返回的串（如 crt `calloc`）无长度元数据，字符串下标守卫
  对它按 0 界处理——任何读写都报 "string index out of bounds"。收发
  缓冲必须用 `byte[]`（真驱动 recvExact 的形态）；`byte[]` 实参可隐式
  传给 `string` 形参且带真实长度。测试手搓 `Fake.calloc` 字符串缓冲层
  让 firebird_wire/sqlserver_tds 全红，后者还因假服务协程崩在守卫上
  表现为客户端挂死超时——**"超时"先看协程是否早崩**。
## 数组字面量初始化循环必须走 ARC retain 协议（2026-09-13 已修）

- `new T[]{a, b}` 与定长 `new T[n] {...}` 的元素初始化循环原先只
  `LLVMBuildStore` 不 retain——元素是引用类型（string/类/委托）时数组槽里
  存的是**悬垂指针**：表达式值出栈即被释放。症状不是崩在初始化处，而是
  **"串值"**：后写的元素把先写的槽内容覆盖成自己这次迭代的值、跨迭代内容
  漂移、读完像"数组元素全都变成最后一个"；且 `--check-leaks` 反而干净
  （没人 retain 也就没人 release）。**"内容莫名漂移 + leakcheck 干净"
  = 优先怀疑某条路径漏 retain**，用最小探针（两元素字面量 + 两次读）
  即可复现。
- 修法（irgen_expr.c，`array_init` 字面量与 `ninit` 定长两处循环同改）：
  逐元素 `is_rc_managed_type(elem_type) && !expr_yields_owned_rc_value(...)
  → emit_rc_retain_for_type`——与元素赋值路径（`arr[i] = x`）同一协议。
  **新增任何"把表达式值写进容器槽位"的 irgen 路径（数组/列表/字典/字段）
  都必须复制这套判据**，判据散装手写会漏。conformance：
  tests/conformance/arr_lit_rc（存 two 元素字面量后再逐个比对）。

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

## 字符串字面量里的 `\xNN` 是码点，不是裸字节

- `"\xEF"` **不是** byte 0xEF，是码点 U+00EF，进字符串时编成两个 UTF-8 字节
  `C3 AF`。于是 `"\xEF" + "\xBB" + "\xBF"` 不是 3 字节 BOM 而是 6 字节
  `C3 AF C2 BB C2 BF`：同一份 82 字节的 CSS，拼上它读出 len 88、首字节 195，
  写进文件得到的头是 `C3 AF C2 BB C2 BF`——「看起来像 BOM、其实不是」，
  拿去验证 BOM 行为会得出错误结论。
- 需要字节精确的内容（BOM、协议魔数、含高位字节的 fixture）一律从 `byte[]` 拼：
  `byte[] raw = new byte[3]; raw[0] = (byte)0xEF; ...; string s = raw.ToStr(0, 3);`
  ——`File.ReadAllText` 内部就是这样把 chunk 变字符串的。自检：`.Length` 等于
  字节数（BOM 是 3 不是 6），首字节是你想要的码值。

## stdlib 按需拉入的词法级坑（2026-09-10 首版过滤；语义守门仍有效）

> 以前 `using Gui;` = 目录全量 glob + 传递 using 扫描到不动点，一个空窗口
> 程序 parse 380 个文件、2.8s，且 stdlib 树里任何文件有语法错全体拖垮。
> 过滤迭代至今：词法"声明名被拼写"（269 文件）→ 今天的 AST 真引用闭包
> （见上"拉入两段制"，空窗 68 文件）。语义等价性靠 conformance 三件套
> （pullin_shadow_same_name / pullin_extension_host / pullin_qualified_escape）
> 钉死；下面四个坑在**词法播种回退路径**（parse 失败时的 fallback）里
> 仍然活着。

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
  自己的，不拉 stdlib 同名文件。这是 stdlib 模板（App/Window/Button 全是
  常见词）不级联的关键。**这条与 C# 同规则**（`class Panel` + 裸
  `Panel.Column()` → C# CS0117 / Zan `'Panel' has no member 'Column'`）。
- **限定名逃逸（A312，2026-09-12 已修）——注释/SPEC 说的才是对的，但曾长期是死代码**：
  `main.c` 与 `docs/SPEC.md:658` 都写「限定 `Gui.App` 仍拉」，A312 之前**不拉**，
  有两种坏法：① 无同名用户类时 `Gui.App a = null;` 报 `undefined type 'App'`
  （`Gui/App.zan` 被 `skip`；`ZAN_PULLIN_DEBUG=1` 可证，同程序
  `ZAN_NO_PULLIN_FILTER=1` 才编过）；② 有同名用户类时 `Gui.App` **静默改绑
  到用户类**（`class App{ static string Marker()=>"USER-CLASS"; }` +
  `Gui.App.Marker()` 编译并打印 `USER-CLASS`）。C# 恰恰**能**用
  `Lib.Panel.Column()` 逃出遮蔽，所以当时是 Zan 独有的洞。
  - **根因（两处清空，缺一不可）**：seed 的 `chain` 在设置它的**同一轮**被
    清两次——`default:` 分支在**点号本身**清（`TK_DOT` 落到 default），
    switch 后置守卫又在**起始 IDENT 本身**清（`tok.kind != TK_DOT`）。
    链根活不过它自己那个 token，`chain->ns_root` 分支恒不可达。
  - **修**：加 `case TK_DOT: break;`（点号是**延续**链的 token），后置守卫
    改 `if (tok.kind != TK_DOT && tok.kind != TK_IDENT) chain = NULL;`。
    **教训**：这类"某分支是死代码"的注释要当成待办，不是设计——A300 当年
    留下"整链复活会 137 项连坐红"的注释，其实是**另一个** bug（`ns_root`
    判定），不是 chain 本身的问题；A312 复活后连坐没出现。
  - **A300 连坐为何没复现**：`ns_root` 只对 `using`/`namespace` 路径的每一
    段置位，而 stdlib 里**没有**任何文件声明 `Widget`/`Component`/
    `Collections`/`Generic`/`System`/`Gui` 这类段名（grep 确认 0 个），
    传递拉入无处落脚。改 `ns_root` 相关逻辑前先重跑这个检查。
  - 判别性探针：**必须让用户类带一个 stdlib 类没有的成员**，再去调
    `Gui.App.Marker()`。原 `pullin_qualified_escape` 只写
    `Gui.App a = null; a == null`，改绑与正确解析**输出同为 `true`**，用例
    恒真（vacuous）——这类"输出与错误行为同值"的断言是假绿，写 conformance
    时先问一句"改绑/漏解析会不会也通过"。已改成
    `App.Tag()` + `Gui.App.ISqrt(9)`（输出 `user-app`/`3`），并补
    `pullin_qualified_pull`（无同名类、只有限定提及）覆盖另一半。
- **子进程 stdout 是 `--emit-ir` 的私有通道（A313，2026-09-12 已修）**：编译器
  会**自己调自己**去编译代码生成器（`genrun.c:zan_gen_ensure`），而
  `zan_spawn_wait` 刻意让子进程继承 stdout（注释说"diagnostics pass through"）。
  于是子编译的人类进度行 `Compiled 31 files ? '…ZanGen_<hash>_<pid>.exe'`
  （`main.c` 编译成功行 `printf` 到 stdout）就落进父进程 stdout——和
  `--emit-ir` 写 IR 的**同一条流**。`tests/run_determinism.cmake` 逐字节比两次
  `--emit-ir` 捕获，**冷生成器缓存**那次多一行前缀 → 必红；暖缓存（生成器已编译
  好、不 spawn）不红。**这类"只在冷缓存复现"的红不要当 flake 放过**：把
  `%LOCALAPPDATA%\Zan\gen` 移走即可稳定复现（如 `json_entity_mapping`
  这个**未改动**的既有用例冷缓存同样红，是决定性归因手段）。
  - **修法（根因层）**：加 `--quiet`/`-q`，把编译成功行、驱动捆绑通知、
    `packaging APK` 三条人类进度行纳入 `if (!quiet)`；`genrun.c` 的嵌套生成器
    编译 argv 补 `--quiet`——编译器给自己建的子构建，stdout 本来就不该给它。
    **错误/警告仍走 stderr**，冷缓存首用提示 `zan: compiling code generators
    (first use…)` 保留在 stderr 上不丢。
  - **纪律**：`--emit-ir`/`--dump-tokens`/`--dump-ast` 把 stdout 当**机器通道**；
    任何新加的人类输出都走 stderr，或纳入 `--quiet`。加"会 spawn 子 zanc"的新
    路径时，子 argv 必须带 `--quiet`。
  - 定位手法：`grep -nE '(^|[^a-zA-Z_])printf\(' src/compiler/main.c`（排掉
    `snprintf`/`fprintf`）列出所有 stdout 写点，比读全文件快。
  - **拉入闭包扩大后，并行会话的在途 stdlib 编辑会经由你的用例炸出来**：A312
    让 `pullin_qualified_*` 拉入整个 Gui 闭包（282 文件，含 72 个 Chart 文件），
    于是并行会话改到一半的 stdlib（调用与定义对不上）会让**你的**用例报
    「stdlib 自身编译错误」。**别怀疑本修**，按此顺序归因：① `git status
    stdlib/` 看谁在改；② `git archive HEAD stdlib | tar -x -C <tmp>` 抽冻结
    HEAD stdlib；③ `zanc <case> --stdlib-path <tmp>/stdlib --auto-stdlib
    --emit-ir` 连跑两次比 md5。HEAD stdlib 上两次 rc=0 且 md5 相同即证明与
    本修无关。同理，`ctest` 大面积红先看有没有**别的会话在跑 ctest/构建**
    （共用 `build\zanc.exe` 与 `conf_*.exe`），隔离重跑一遍再下结论。
- **A/B 两个 zanc 二进制时 stdlib 根跟着 exe 走（2026-09-13 实测大坑）**：
  `--auto-stdlib` 按**编译器 exe 所在目录**找 stdlib（main.c:2951）——
  worktree/副本里构建的 zanc 会静默用它自己那份冻结 stdlib；此时脚本若又
  显式喂主树 stdlib 文件，同一文件两个根各注册一次，全局类型集体报
  `ambiguous type 'X'; qualify it with its namespace`（一次 IDE 全量 A/B
  假象出 100 歧义 583 错，差点误判成编译器回归）。**正解**：把被测 zanc
  复制进主 `build/` 再跑（auto-stdlib 即指向主树 stdlib）；反方向复制
  （主 zanc 放进副本树）要连 `build/zanrt_*.obj`、`zan_*.obj` 一起带过去，
  否则链接期才炸"no such file: zanrt_timer.obj"。
- 门控：`--emit-symbols` 恒全量（IDE 索引要完整 stdlib），`ZAN_NO_PULLIN_FILTER=1`
  回退旧行为，`ZAN_PULLIN_DEBUG=1` 打印每个文件的拉入原因（含命中名）。
- 语义等价验证定式：同一程序 `ZAN_NO_PULLIN_FILTER=1` 开关两态编译运行
  diff 输出；改拉入逻辑必须补 conformance 用例并跑 smoke+standard。
- 顺带的实证：**prune 已保证未用代码不进二进制**（关 prune 只多 7KB），
  "using Gui 导致 exe 10MB"是错觉——Gui 窗口 exe 的 1.6MB .text 是
  GuiHost→App/Style/Fx 的活代码闭包 + Zan 运行时，与 unused 无关。
- stdlib 文件引用跨命名空间类型必须写 using（ChartHost 曾裸写 `App`，
  靠用户程序恰好也有 `class App` 才碰巧编译——prune 把它藏成了哑弹）。
- **（已闭，2026-09-13）极小 seed 曾报 `cannot convert 'CellOf' to
  'CellOf'`**（同名类型两份解析，Transfer/DataGrid delegate 声明面）。
  当时的诊断 heuristic 仍有效：**同名类型互斥转换先怀疑闭包裁剪，不是
  代码错**。现在的 HEAD 复现不出（6 形态 × 2 拉入模式全绿；台账行号
  所指代码已不存在），疑 A312 限定名逃逸根修顺带治愈——再遇到先隔离
  重跑，别急着当活缺陷修。

## 闭包捕获语义：捕获变量，不捕获值快照

- C# 形态的 lambda 捕获的是**变量**：只要某个声明被 lambda 真正引用，声明方和
  所有 closure 都必须读写同一个 heap cell；即使 lambda 只读，创建 closure 后的
  外层赋值也必须可见。旧的“只读捕获按创建时快照、只有 lambda 写入才装箱”规则
  已被共享捕获 oracle 推翻；它会让 `int n=1; Read r=()=>n; n=42; r()` 错误返回 1。
- 判断捕获必须按**声明身份 + 词法作用域**，不能只按名字：lambda 参数、lambda
  局部、for/foreach/query/catch/switch pattern 变量的同名遮蔽只在自己的作用域内
  生效，退出后 sibling lambda 仍应能捕获外层声明。廉价 name-only 扫描只能当候选
  过滤，真正装箱前必须再做 declaration-aware scoped scan。
- cell 的所有权必须同时覆盖正常退出和 longjmp 展开：声明作用域持一份 tagged
  cell owner，并把 owner slot 注册为 delegate 形态的 EH 临时；异常展开释放并清空
  slot，随后正常 cleanup 见 null 不得二次释放。cell 内若是 ARC 值，参数借用值放入
  cell 前先 retain，cell 析构再释放。
- 委托形状决定调用方式：静态方法组/无捕获 lambda 是裸函数指针，实例方法组/
  捕获 lambda 是带 tag 的堆闭包记录（rec-first）。跨调用保存仍遵循 store-family
  retain/release 规则。

## 并行会话下的 ctest 假红

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

## macOS 交叉编译与运行契约（ld64.lld / codesign / Cocoa 生命周期，2026-09-20）

- **Apple Silicon 必须带 `-adhoc_codesign`**：macOS 11+ arm64 Mach-O 可执行文件若无 `LC_CODE_SIGNATURE`（即使未加开发者证书也必须有 ad-hoc 签名），XNU 内核在 `execve` 时会直接 `SIGKILL`（namespace `CODESIGNING`），现象为启动即闪退。`ld64.lld` 针对 macOS target 必须常开 `-adhoc_codesign`。
- **GUI 兼容对象 `zanrt_gui.o` 仅在存在 GUI 驱动时链接**：CLI 程序不能无条件引入 `zanrt_gui.o`，否则会报 `_zan_gui_draw_text` / `_zan_gui_font_height` 等未定义符号错误。通过检查 `cross_dylibs` 是否包含 `zan_gui` 门控链接。
- **macOS 退出事件与 Cocoa Delegate 契约**：Dock 右键 Quit / Cmd+Q 发送 `kAEQuitApplication` 走 AppKit `[NSApp terminate:]`。若 `NSApp.delegate` 为 nil 或未实现 `applicationShouldTerminate:`，AppKit 回退调用各窗口 `windowShouldClose:`；如果 `windowShouldClose:` 返回 `NO` 且未调用 `zan_gui_wake()`，终止过程被静默取消且事件泵继续挂起阻塞，造成“右键退出无法退出”。正确做法：
  1. 为 `NSApp` 设置代理 `ZanDelegate`（实现 `applicationShouldTerminate:`），返回 `NSTerminateCancel` 并派发事件 8（Window Close）以及调用 `zan_gui_wake()`，让 Zan 运行时正常触发安全清理退出；
  2. `windowShouldClose:` 中也必须调用 `zan_gui_wake()` 唤醒阻塞在 `nextEventMatchingMask:` 的事件泵；
  3. `zanrt_gui.o` 在运行时通过 ObjC 运行时动态注入/挂载上述 delegate，确保即使动态链接旧版 `libzan_gui.dylib` 也能生效。
- **多开外层跳板必须声明 `LSUIElement`**：多开跳板外层 App 仅用于执行脚本 `open -n AppCore.app`，其 `Info.plist` 必须包含 `<key>LSUIElement</key><true/>`（即 `isLauncher=true`），否则外层进程执行脚本完毕退出时 Dock 图标闪烁后消失，容易被误认为崩溃闪退。
- 另一条会一次打红**整档**的：并行会话重链 `build/zanc.exe`（
  而我这轮 ctest 是 07:20:58 起的）。编译器一换，所有 `conf_*.exe`/golden 产物
  全部过期，逐条归因毫无意义；判据是 `ls -l build/zanc.exe` 的 mtime 落在你的运行
  区间内 → 整档作废重跑。

- 另一条常客：**端口/资源竞争与真 flaky**。判别法是把**同一个二进制**（不重编）
  连跑 5 次——通过/挂起交错就说明是被测代码里的竞争，单次的超时/失败不能当回归
  （本次 `conformance_gui_listview_scrollbar_drag` 同一 exe 3 过 2 挂，而它属
  Gui 车道在途改动；`conformance_http_client_keepalive` 则是全量并行 120s 超时、
  单跑 0.5s 过，属端口竞争）。并行档的超时值一律先单跑复核。

- **两档 ctest 绝不能同时跑：它们共享同一批 `build/conf_*.exe`**（smoke 与 standard
  的 label 大量重叠，`add_test` 的 `-DOUT_EXE` 是同一个路径）。本轮：我这轮
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

## wasm32 默认栈只有 64KB——GUI 深递归必打穿，症状是"乱指针"不是"栈溢出"

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

## wasm32 H5 文本两坑：中文全成"?"是字体面没盖住；"2G 内存"是 V8 预留不是真用

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
  trap，所以上界别贴着单次测得值给，留一个量级余量。
- **wasm 冒烟在 node 跑**：`WebAssembly.instantiate(bytes,
  {wasi_snapshot_preview1: Proxy})` + `ZanWASI(fsBackend, out).attach()`，
  fsBackend 要有 statSync/readFileSync/writeFileSync（缺 writeFileSync 时
  path_open 创建文件会静默失败，别误判成编译问题——tests/wasm32 的断言本
  是"编译+链接"，端到端跑通要自备内存 fs）。

## wasm32 回调地址跨界：(nint)Method 必须垫 C 形 thunk，w32adapt 表逐符号跟

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
  同步 die 块对齐。探针：`await Boom()` 无 try 时打
  `Unhandled exception: string-payload` / `Unhandled exception: Spark`
  并 exit 1；conformance 无金样期待裸行（exc_uncaught_name 走 catch 路径）。
- **并行会话下的选择性提交**：工作树文件 = HEAD + 我的 hunk + 别人在途
  hunk 时，整文件 `git add` 是禁区。配方：`git show HEAD:<f>` 基线落
  _scratch/stage3，用 python 把**我的新块从工作树文件按锚切片原样搬进
  基线**（find 定位 + 下标切，零转义），`git hash-object -w` +
  `git update-index --cacheinfo 100644,<blob>,<path>`，`git diff --cached`
  核对只剩自己的 hunk 再 commit；别人的在途改动原样留在工作树。

## wasm32 小游戏宿主事件桥：同步 runloop 占住 worker 任务，运行期消息不可达

> wxmini 探针在微信 devtools 里 `_start` 活着但零 stdout、点击永不到达：
> 不是 wasm 坏了，是宿主消息模型——事件桥的所有运行期假设在 worker 里
> 都不成立，逐条踩过才有这条契约。

- **pre-start 种子契约**：宿主必须在 `_start` 之前把 resize(kind 7)+
  layout-attach(kind 14) 排进事件队列——wasm 第一次 poll 看不到画布尺寸
  就按 0×0 窗口直接退出（零输出、无报错，像"链接失败"其实是种子丢失）。
  种子还必须在 worker 创建**之后**发（早于 worker 存在的 sendEv 静默吞掉）。
  种子尺寸给**逻辑分辨率**（windowWidth×windowHeight，与 H5 harness 同一
  坐标系），给物理分辨率会让全部按逻辑尺寸写的控件缩成 1/DPR（手机 3x 屏
  上按钮只有 7% 宽）；触控坐标、present 尺寸同步用逻辑系，画布由模拟器
  上采样。
- **worker 消息只在任务间被服务**：同步 runloop（`_start` 内 busy-park）
  占住 worker 直到退出，运行期 postMessage 的触摸/事件全部滞留到 exit 才
  冲洗。交互验证不能"运行中发事件"，要把 press/release 与种子一起排在
  wasm 投递之前（app 头几帧即处理点击）；运行期通道只对"任务会退让"的
  宿主（真 rAF 驱动的游戏循环）成立。
- **pacing**：无 SharedArrayBuffer 的宿主（devtools 模拟器整页没有跨域隔离）
  里 `Atomics.wait` 对非共享 Int32Array **抛异常**（node worker_threads 同样），
  别拿异常当阻塞；退路是有界自旋（≤16ms）保 ~60fps。桥别建在 SAB 上。
- **宿主文件管道上限**：走 base64→atob 的 readFileSync 单次上限 2^24 字符
  (~12.5MB 二进制)，超限 InvalidCharacterError；大产物预切 ≤4MB 分片循环
  读再拼接。async readFile 在这类宿主可能**静默不回调**（成功失败都不来），
  只用 sync 读。
- **worker 只投递入口文件**：依赖 JS 全部内联进入口；且 API 是全局 worker
  对象（`worker.onMessage/postMessage`），DOM Worker 的 `self.postMessage`
  不路由——stdout sink 用错会让全部程序输出**静默消失**，误诊成"卡死"。
- **真机 worker 全局缺失**：devtools 的 worker 有 `performance`/
  `TextDecoder`/`TextEncoder`，真机（JSC/V8 定制壳）没有——`performance.now
  ? ... : ...` 这种守卫引用未定义标识符**照样 ReferenceError**，必须
  `typeof performance !== "undefined"`；TextCodec 缺失则第一条 stdout
  （fd_write）就崩，症状是"黑屏且零日志"。真机交互输入走 SharedArrayBuffer
  环 + `Atomics.wait` 共享门铃（消息队列只在任务间被服务，同步 runloop 下
  永远不可达）；无 SAB 宿主（devtools）回退消息桥，仅支持 pre-start 种子。
- **坐标系分两类**：Game 模板（GuiHost + CDraw.StageViewport）把设计分辨
  率等比缩放到窗口实际尺寸（短轴贴设计、长轴延展），壳应喂**高分辨率**
  （物理或 2x，清晰且比例正确）；裸 Gui 程序无缩放层，喂高分辨率会把逻辑
  尺寸控件缩成 1/dpr，必须喂逻辑分辨率。
- **devtools sim 会整体僵尸**：工程窗/控制台看似正常但 gamePage 不再
  编译启动，跨进程冷启、WeappCache、webview 缓存清理均不愈（2026-09-13
  实测 3 次冷启无效）；判断真假启动的唯一可靠信号是**程序写出的日志文件
  mtime**——控制台回显/旧内容会被误读成新 boot。真机预览编译是独立管线，
  不受 sim 僵尸影响。
- **游戏模板实测 + 字体注入**：ddz 卡牌模板 wasm 8.8MB 在 wx worker 壳里
  跑到稳态 ~49fps（390×844 逻辑；首 120 帧个位数是 instantiate+字体挂载，
  不是稳态）。中文字形必须 pre-start 往 worker 内存 FS 挂 `/fonts/ui.ttf`
  + `/fonts/cjk.ttf`（与引擎同法 ≤4MB 分片），缺字体不报错、只画"?"。
- **渲染入口声明即契约**：Render.zan 无条件引用 `zan_gui_draw_text_bold`
  （图表标题默认加粗）与 `zan_gui_draw_polybatch`，非 Win32 的 gui_runtime_font.c / gui_compat_mac.c 必须出这些符号
  （回退=画常规体 / 循环调用 polyline），否则 wasm/非 Win32 链接期 undefined symbol；同族
  "对象 mtime 新于源码但缺符号"的坑用 nm 验对象内容，别信 mtime。macOS/iOS 编译
  还依赖 `toolchain/macos/*/zanrt_gui.o` 和 `toolchain/ios/arm64/zanrt_gui.o`。

## 改仓库文件的静默陷阱

- **行尾**：本仓 `core.autocrlf=true`，多数 `.zan`/`CMakeLists.txt`/`parser.c` 在工作区
  是 CRLF、blob 是 LF。**不要**用「读到字节里有 CRLF → 把换行全换成 CRLF」的整片
  重放模板：源文本的换行本来就带 CR，重放后每行变成 CRCRLF，`git diff` 显示整个
  文件被改写（本轮把 `tests/gui/datatable_sparse_page_test.zan` 135 行全改了）。
  正确顺序：先 `replace(CRLF, LF)` 归一化 → 改内容 → 要保 CRLF 再整体重放；
  验证靠 `git diff --numstat`：增删行数应等于你实际改的行数（本轮 2/2 才对）。
- **工具传输会把双反斜杠折叠成单反斜杠**：替换片段里想要两个反斜杠，到执行时可能
  只剩一个，于是 old == new、`str.replace()` 静默 no-op——脚本照样打印 "patched 5"，
  文件一个字节没变。含反斜杠/引号的替换一律用 `chr(92)` / `chr(34)` 在脚本里
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

## 静默截断的指纹：rc=0 但输出缺行

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

## longjmp 落地沿的 SSA 不可信：-O2 会把 catch/展开路径的槽读折叠成 undef（A300②③，2026-09-15 已修）

- **机制**：setjmp/longjmp EH 的落地块（async `$resume` 的 `co.exc`/`eh.land`、
  try 的 `try.rearm`/`try.rearm.init`）不是 CFG 边——控制从**函数外**重新进入。
  LLVM 的前端假设不变式「支配 landing 的值都有定义」在这里不成立，-O2 的 SROA
  有权把 catch phi 在该边上的 incoming 折成 `ptr undef`/null。实测三种症状：
  ① co.exc 清理释放未初始化的帧槽 → 释放垃圾 → 堆损坏、后续 malloc 段错误；
  ② rearm 落地后 catch 里 `this`=null（HttpClient.zan:1091 软错）+ 释放垃圾；
  ③ 非指针槽（循环计数）同样 undef → 所在循环失控打转。
- **修（两处）**：`irgen_emit.c` resume 入口对所有指针形槽 alloca（this/参数/
  alocal，无论 arc 归属）补 null-init——入口静态支配 landing（longjmp 不构成
  CFG 边，优化器无法据此判定 landing 不可达），槽从此在 landing 处有定义；
  `irgen_stmt.c` try.rearm/rearm.init 块对**全部** async 槽做 volatile load+store
  自拷贝——栈 alloca 物理上持有活值（同调用 throw 带 post-await 值落进来、
  子协程异常经 await 点 reload 过），volatile 迫使 SROA 从内存读而不是折叠。
  **别用 `emit_async_reload_slots`（从堆帧 reload）**：A293 契约是同调用 throw
  落地时栈槽比帧新（after-await），reload 会把帧里的旧值盖回来
  （async_throw_across_frames 当场变红）。
- **残留洞（A320，2026-09-17 已修）**：A300 的入口 null-init 只盖了预登记
  alocal/参数槽；**语句级声明走 `emit_entry_alloca` 的指针槽没有 init**。
  「声明点在 await 之后」的 owned 局部（典型：ORM 动作里第二个查询的结果
  局部）在 resume 的 co.exc 清理里 `load alloca; release_dyn` → -Os 折成
  `release_dyn(ptr undef)` → undef 具体化成 rcx 现成值（页对齐堆块基址）→
  ARC 头跨页 AV 或静默减垃圾 rc → 堆损坏（后续 0xC0000374 一批、EH chunk
  calloc 失败报 OOM）；dev 档同一 load 读栈垃圾 → E0A2C002 first-chance
  （guard 软接住，假装稳定）。修：`emit_entry_alloca` 对指针形 alloca 统一
  入口 null-init。教训：修「某类槽」不如修「创建槽的入口」——所有语句级
  alloca 都过 `emit_entry_alloca`，一处 init 全收，新槽族自动受庇。
- **判定手法（可复用）**：publish 崩溃 + `--arc-guard` 复建**无 guard 报告**=
  释放的是纯垃圾指针（未定义槽值），不是已释放 Zan 对象（over-release 会触发
  guard）；`--emit-ir`（dump 的是**优化后** IR）grep catch phi 的 undef
  incoming 直接实锤（本例直接 grep `release_dyn(ptr undef)`，比 phi 更直指）。
  注意 `--emit-ir` 跳过链接：经 build_ide 间接调 zanc 时后续步骤对缺 exe
  报错，要单跑截 stdout。VEH backtrace 被 handler 帧截断时，叶桩调用的返回
  地址就是 [rsp] 第一个字——rt_crash.h 崩溃记录已内置 Rsp 起原始栈转储，
  直接读「raw stack (@rsp)」首行。**`-Os` 不带 `--publish` 不 strip**：同
  优化档 + nm 全符号，崩溃 RVA 直接查表，不必碰 .pdata 字节匹配。
- **符号化死路提示**：publish exe 是 strip 过的，llvm-nm/llvm-symbolizer 全空；
  cdb 可用但无符号只给 module+offset。破法：`.pdata`（RUNTIME_FUNCTION 数组，
  python struct 裸解析）把 fault RVA 定到函数边界，再把函数头 24 字节去
  -O2+符号的诊断 exe 的 .text 里字节匹配换名字（prologue 相同会误配，须全 24
  字节 + 帧尺寸一致性双重确认）。注意 crash 日志 backtrace 的 #00/#01 帧常是
  `zan__crash_write_record`/`zan__crash_veh`（handler 自己），只有 #05+ 才是
  真实调用者。同族残留（IDE publish 启动期）= A318，真根因与本节旧归因
  （"未定义槽值"）不同：是**强制内联打破 ARC/EH 所有权**，释放的是已释放
  对象，见下节。

## -Os 强制内联打破 ARC/EH 槽位所有权：打内联标记=改写所有权（A318 已修，2026-09-16）

- **症状**：publish(-Os) 秒崩 0xC0000005 @ `release_dyn+0x19`，dev(-O0) 稳定；
  "没改源码也崩"——毒路径由内联器选择，与业务改动无关（IDE 启动即做 HTTP
  超时请求+try/catch，必踩）。
- **机制**：`zan_opt_inline` 曾给 ≤2 BB 函数打 `alwaysinline`。`return new
  X(...)` 形状的工厂（如 HttpClient.CreateHttps——无局部→无 arc_own_local
  槽位）被强制内联进调用方后，两套所有权簿记（EH 槽位注册 vs 调用临时释放）
  跨内联边界错位：调用方把仍注册在 EH 变量槽里的对象提前 `release_dyn`；
  异常进 catch 入口时 TMPS unwind 按 mark 二次释放 → UAF。
- **为什么只能不打标**：`noinline` 闸门挡不住（LLVM always-inliner 无视与
  alwaysinline 并存的 noinline）；改打 `inlinehint` 也挡不住（成本模型照样
  内联）。修复=`zan_opt_inline` 全空（no force, no hint，交 LLVM 尺寸档成本
  模型；顺带省 2.2MB IDE 体积）。**重开任何强制内联前，必须先做"EH 注册
  函数的内联安全契约"**（TASKS A318 遗留）。
- **调试手法（本次验证，可复用）**：① -Os 下单步（`t`）扰动 LFH 时序会
  **掩盖崩溃**（rc 139 变 30 的 heisenbug）——只用非扰动日志断点；② zanc
  自家 internal 符号 nm 可见但 cdb 不认，用 `bp mod+0x<RVA>`（nm 取 RVA +
  默认基址 0x140000000）；③ cdb 日志 grep 要 `-a`；④ bash 双引号里 cdb
  脚本的 `$<` 会被展开成 PID，`-c` 参数整段用单引号；⑤ EH 原语在 -Os 被
  部分内联（push/slot_fast 内联不可见，pop/unwind/drop 留函数）——先在
  -O0 用全原语断点做"完整台账"，再回 -Os 对账，别给不可见的符号设断。
- **gen-cache 连带坑**：zanc 字节一变，嵌套 ZanGen 缓存键即失效；若 zanc
  被本地改坏（如 LLVM API 误用段错误），外层症状只是"code-generator
  compile failed (exit -1)"——先手动嵌编
  `zanc stdlib/System/Compiler/ZanGen.zan --auto-stdlib --no-gen -DZAN_GEN_MAIN=1 -o …`
  拿到真崩溃栈再修。
- **共享树半成品护栏**：读侧换了查找函数、写侧没落（无人给字段赋非 NULL，
  查找永远 miss）时，给读侧加"miss 回落旧路径"兜底而不是回滚读侧——
  行为恢复到基线，写侧落地后节点路径自动生效（A31x 的
  `local_find_async_decl` 三处即此写法）。

## leakcheck「仍可达」与停服排水（A302，未修）

- Zan 的 leakcheck 对**仍可达**对象也报红（不区分丢失/仍被持有）。服务端对象
  （listener、连接、池）在 Stop() 后需要泵协程自然走完（accept 返回 -1、EOF 关链路）
  才不可达；测试里的固定排水（20x10ms）在负载下可能不够，`rc=0` 但 leakcheck 记红。

## Windows 路径通配符与 C++ COM 运行时解耦（2026-09-18）

- **Win32 FindFirstFileW 遇到混合斜杠在通配符下报 ERROR_FILE_NOT_FOUND (2)**：
  在 Windows 下拼接通配符路径（如 `D:\project\stdlib/Gui/icons\\*`）时，若路径前段含有正斜杠 `/`，
  Win32 的 `FindFirstFileW` 无法正确解析混合斜杠的通配模式，直接返回 `INVALID_HANDLE_VALUE` (GetLastError=2)。
  在 `src/compiler/embedres.c` 等涉及文件目录遍历的代码中，进入 Win32 API 之前必须无条件将所有 `/` 归一化为 `\`。
- **C++ 辅助源文件（如 DirectWrite）在 C 静态库中的 pure COM 准则**：
  Windows SDK 的 `dwrite.h` 必须以 C++ 编译，但若在源文件中使用 `<string>`、`std::wstring` 或默认编译选项，
  会导致输出对象产生 `__cxa_begin_catch`、`std::terminate`、`__gxx_personality_seh0`、`vtable for __cxxabiv1` 等对 C++ 运行时（`libstdc++`）的硬引用，导致 C 静态库在纯 C 链接时大面积报未定义符号。
  解法：
  ① 彻底杜绝 C++ 标准库头文件与 STL 容器，使用 `wchar_t[]`、`wcsncpy`、`wcscmp` 等 C 原生字符串操作；
  ② 编译参数必须强制带 `-fno-exceptions -fno-rtti`；
  实现 100% 零 C++ 运行时依赖的 pure COM 胶水，无缝打包进 C/GNU 目标库。
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

## 无栈调度器终局条件：woke==0 ≠ 静止；「摇奖绿」用例要按挂起事件清零来修（A298，2026-09-12 已修）

- irgen 内联发射的 `zan_co_sched_run_until` 曾以「`woke>0 || has_timer`」决定退出。
  阻塞任务线程（DNS/Resolve 等）完成时在**锁外** post NULL-overlapped 唤醒包，而
  reactor 的 GQCS 超时路径每轮都 drain 完成列表——包落在 drain 之后，dequeue 时
  drain 为空 → woke==0 一轮 → 带着 N 个在途 IO op 和 parked 帧**静默退出**（rc=0、
  输出在截断处戛然而止）。IOTRACE 指纹：末行 `poll removed=1 cnt=N` 且**无**
  `poll_op` 行（NULL-overlapped 包不打印 poll_op）。修法：终局条件追加
  `zan_io_has_pending()`（weak 声明 + 无 IO 程序 weak 回退 0），对齐 rt_sched
  纤程调度器的既有判据。凡「偶发空输出/中途截断 rc=0」先查这条，别先怀疑 IO 丢数据。
- 排查定式：`ZAN_IO_TRACE=1` 跑 N 次抓失败样本 + 用 File.AppendAllText 打点
  （stdout 不可信——正被排查的就是它）；标记序列直接指认退出点在哪个 await 之间。
  共享树 zanc 被并行会话的在途 rt_io.c 卡住编译时，`git worktree add _scratch/xx HEAD`
  + 只拷自己的编译器改动进去独立构建，验证与提交两不误（A/B 时务必记得把修复
  拷回去再重编——忘拷会拿旧编译器跑出一堆假回归/假修复）。
- 「关闭不唤醒」是 Windows 缺省：closesocket 不给挂起重叠操作投递完成包，POSIX
  的 shutdown/对端 FIN 才免费给 0 字节唤醒。**TcpClient.Close 必须先
  `Socket.ShutdownBoth`（= shutdown + CancelIoEx）再 Close**，与 TcpListener.Stop
  同型；否则 keep-alive 池对端停在 RecvAsync 的协程永久挂起，HttpClient 池清扫
  定时器常驻——调度器表现为「提前退出」或「有定时器时的活锁挂死（rc=124）」。
- 用例面：依赖旧 bug「摇奖退出」的用例（进程结束不关池化连接）在终局条件收紧后
  会从 flaky 绿变确定性红/挂——按 http_client_keepalive 的清理定式补显式
  Close + settle 泵（20×10ms），让挂起事件真正清零。判定哪层是根因：A/B 同一
  stdlib 只换编译器（或反之），单变量归因，别拿两棵树产物直接对跑下结论。
- **终局语义别记错**：`run_until` 循环头先查 root-done
  标志，Main 一完成就退出，不经 io_bb 的 has_pending 分支——「main 返回但仍有
  挂起 op」时退出是设计内（与 rt_sched 纤程调度器「排干全部协程」是两个语义）。
  判别 has_pending 是否生效，必须构造「root 挂起期间被无唤醒轮误杀」的场景；
  「main 返回后该不该挂住」型判别用例（discrim/mini）是无效的，会把有效修复
  误判成死码。

## 泄漏探测红 ≠ IO 挂死；共享 fd 的协程收尾定式（A302，2026-09-12 已修）

- `zan_io_poll` 在 `cnt==0` 时早退且**不落 IOTRACE**：settle 窗口的 trace 静默
  不代表调度器没在轮，别把「trace 停了」当「进程卡死/没跑」（A298 教训的镜像）。
- 泄漏报告只给分配站点不给持有链。定位「哪个协程停摆」用**逐 await 打点**
  （进/出/分支全打，`File.AppendAllText` 落盘，stdout 本身可能是被排查对象），
  一次编译拿全生命周期；站点行号随编辑漂移，A/B 先对站点再对数量。
- Windows 轮询式带超时 RecvAsync（stdlib 3 参版，1→16ms select-poll）对本地
  关闭**免疫**：select 对已关句柄恒不报可读 → 循环唯一的 `IsOpen` 逃生口（只在
  IsReadable 分支内）不可达，协程带着死 fd 空转到 idle 截止；本地 shutdown
  也不够（select 恒报可读但 recv 返回 -1 且 fd 未关 → 同样空转）。两个
  逃生口都够不到时，外部 CancelIoEx 无 op 可取消——「Stop+settle 排水」修不了它。
- 共享 fd 的协程收尾定式：**谁最后读谁 Close，对端只 Shutdown（半关闭）**——
  shutdown 让对端下一轮探测以 EOF 正常退出、由它亲手 Close；Close 一侧若可能
  有挂起重叠 op，先 ShutdownBoth(CancelIoEx)（读侧 0 字节复位、发送中表现为
  短写）。长生命周期连接（隧道）别继承请求级空闲截止：沉默是常态，切 idle=0
  顺带把接收换成真挂起的重叠 op，teardown 的 CancelIoEx 才有东西可取消。
- worktree zanc 的 `--auto-stdlib` 解析 exe 同目录的 stdlib；验证 stdlib 改动
  把改过的文件 `cp` 进 worktree 对应路径重编即可，泄漏 A/B 用
  `git show HEAD:path > worktree 拷贝` 还原单文件基线。

## poll 内循环会吞掉入口扫描：deadline 交付要挂在超时路径上（A308，2026-09-12 已修）

- recv-with-deadline（`zan_io_recv_to_co`）的截止表扫描 `rto_timeout_scan` 若
  只挂在 `zan_io_poll` **入口**，而 poll 内部还有自己的 `for(;;)` 超时循环
  （capped 20ms + 无限 caller 超时 + io 在途 ⇒ 永不返回），deadline 到点也
  没人交付——Linux 上 `RecvAsync(300ms)` 永久停摆。定位链：gdb 断点只见
  not-due 不见 DUE → strace 数 poll 轮数（200/4s，全 EAGAIN）→ 打点证明
  `[scan]` 全程只出现一次。**入口扫描会背内循环吞掉**，新扫描挂进 poll 的
  超时路径（dns_timeout_scan/io_sweep_slots 同位）才算数。
- gdb/strace 先行，printf 打点收尾：先在「怀疑不到场」的函数设断点看它到
  不到场，再用 strace 数原始轮数把"每轮做什么"量化，最后才打点看值。顺序
  反了会浪费整轮打点-编译循环。
- Windows 1ms ticker 的 ~65 ticks/s 是 GetTickCount64 粒度地板（Task.Delay(1)
  每 15.6ms 才醒一次），是**预存基线**：断言要用「相对基线」（≥40/0.8s），
  别按名义 1000 ticks/s 定阈值——同探针旧编译器快照同值证明与本改无关。
- C 级 socketpair harness 验证 recv-to ABI：`zan_io_pump_timeout` 只泵 IO，
  step 是经 `zan_co_ready` **入队**的，harness 必须自己 `zan_co_sched_run()`
  排干就绪队列，否则 `steps=0` 看似 hang 其实是队列没人抽；recv 缓冲要给真
  指针，`(void*)0x1000` 伪地址会在交付 recv 时 EFAULT 返回 0（伪 EOF），
  误判成对端关闭。
- 跨链接进 zanc 产物的 runtime 对象在 `build/linux-musl/zanrt_io.o`（exe 目录
  相对解析，main.c ~5538），std::sync 变体是 `zanrt_io_mt.o`（加
  `-DZAN_CO_DRIVER`）；改 rt_io.c 后用 `wsl zig cc -target x86_64-linux-musl
  -DZAN_IO_STACKLESS_ONLY -fPIC -O2 -c` 重出两份并同步 `toolchain/linux-musl/`。
  排查期可临时用 O0 打点副本顶替，收尾必须换回 O2 官方对象。
- 晚到完成包的仲裁不能按帧指针，要按 op 自持身份（A327-12a，2026-09-17 已修）：
  recv-with-deadline 赢家路径（`rto_timeout_scan` 交付 -1 后 `CancelIoEx`）的
  OPERATION_ABORTED 晚到包出队时，原实现按 `CONTAINING_RECORD(帧指针)` claim——
  async 帧是池化复用的，deadline 输了竞赛后帧立即回到就绪队列，同指针上新登记
  的 RecvToOv 条目会被晚到包偷走并往新帧结果槽写 0（EOF）→ 健康连接被误杀，
  症状是「空闲超时后紧接的 recv 秒回 EOF」。修法 = `rto=2` 标记整体丢弃：扫描
  赢家先在 op 上落标记再 CancelIoEx，两处完成包出队点先查标记（op 拥有
  OVERLAPPED，是完成包点名的唯一稳定身份）再 op_free。判定手法：
  ZAN_IO_TRACE 看 aborted 包是否带 `rto-dead`，以及紧随超时的 recv 是否 0 字节。

## 泄漏报告的站点标签按类形状混叠；排查先做单站点最小复形（A64b，2026-09-12；已修同日）

- leakcheck 报告的「allocated at file:line:col」**（修复前）不是泄漏物的出生地**：站点
  索引按 (类符号, 泛型实例) 去重（`reserve_arc_site`，dispatch 需要同形状共享），而
  `__zan_site_names[idx]` 在每次分配时被无条件覆写——标签永远属于**同类各站点中
  运行时最后分配的那个**。程序里同类有多个 `new` 站点时，报告会把真实泄漏引到
  完全无辜的那个站点上（A64b 实锤：测试自己的 listener 停摆，报告却指着 stdlib
  里 fwd 的 `new TcpListener`，误导排查一天）。**已修**：
  新增 `site_loc_file`/`site_loc_line` 并行表，check-leaks 构建下去重键扩成
  (形状, di_cur_file, di_cur_line)；无 `-g` 时 di_cur_* 恒 0、键退化为纯形状=修复前
  行为；非 check-leaks 构建键恒 0、索引序列不变（对正常构建零扰动）。4096 站点上限
  从此约束「形状×位置」（仅 check-leaks，超限大声 exit(1)）。conformance 用例
  `arc_leak_site_label`（同类两站点只泄先分配者，`EXPECT_LEAK_SITE` 反极性断言报告
  点名 `:20:`）钉死行为；`run_leakcheck.cmake` 的 `EXPECT_LEAK_SITE=` 空值保持原
  极性，不碰既有用例。
- 排查定式：**先把形状缩到「程序里只留一个同类分配站点」**——单站点时标签才可信
  （P1：单文件 parked listener → 报告自己的行 ✓）；再多引入第二个同类站点对照
  （P7：+ 零流量 HttpForwarder → 标签跳到 fwd 的站点、泄漏总数不变 → 混叠实锤）。
  `g_live` 头部总数恒可信（string 也计数但不进站点表——纯 string 泄漏只有头部没有
  站点行）；逐站点计数也可信，只是**名字**不可信。
- 判「报告的对象 X 是否真活」别信标签，用生命周期算账：X 的全部 +1/-1 调用点列
  清单（字段存取、每帧 ramp/完成、容器进出），加上「停摆帧的接收者 +1」逐项核。
  async 帧持有定式（irgen.h `current_async_this_owned`）：**ramp 对接收者 +1，
  帧完成才释放**——协程停摆在 await 上 = 它的接收者和帧本地全部算「仍可达」，
  这是记账正确不是误报；要它放，就得让那个 await 以任何值完成（-1/0 也行）。
- **worktree 命令 cwd 陷阱（本轮踩中）**：`cd _scratch/xx-worktree` 后 Bash cwd 会
  跨调用持续——之后的 `git status`/grep 全打在 worktree 拷贝上，还会把「文件被谁
  改回去了」的幻象坐实（worktree 原版 vs 主树编辑版来回横跳）。主树操作前先
  `cd` 回仓库根并用绝对路径 grep 复核；Edit/Read 用绝对路径不受影响。
- **ctest 输出必须整场落盘再 grep，别 `| tail -N`（本轮两次被 tail 截断坑）**：
  失败清单可能长于 N 行，`LastTestsFailed.log` 又会被并行/后继 ctest 覆写，tail 截断
  后「只见 3 个失败」与真貌（14 个）对不上，归因全乱。`ctest ... > _scratch/x.log 2>&1`
  后台跑，结束再 grep。
- **判红是不是自己引入的：拿「修复前二进制」交叉复跑同一用例**——泄漏**计数**与
  去重键无关（键只改标签与索引数），修复前后计数应逐个相同；主树旧 zanc 跑出同样
  5 对象即既有真泄漏，与本改无关。怀疑某 twin 是「本来就坏」时，看 CMake 里各
  foreach 的 `_extra_args` 链是否一致（见下条）。

## 测试里无界 accept 循环的监听器必须可被外部 Stop（A64b，2026-09-12）

- 服务端测试用例的 `while(true)` accept 循环，收尾只 Stop 一个服务时，另一个
  listener 的停摆 accept 帧会把它钉成 leakcheck 红。有界循环（`while (served < N)`）
  能自己退出，但 accept 数不定的用例（池化行为决定链路数）没法预设 N——把
  listener 提成 `static` 字段，Run 收尾补一次 `Stop()`：挂起 accept 以 -1 复位、
  循环 break、帧完成释放，leakcheck 转绿。Stop 幂等（`running=false; sock>=0`
  守卫），循环保守起见的二次 Stop 是无害 no-op。
- 定式（static 停靠不会假红）：static 字段持有的对象**不在泄漏报告里出现**
  （static 存储不 retain，P2 探针 0 泄漏实锤）——用它当「外部可及的关闭把手」
  安全，但别把语义依赖在它上面（对象生命周期仍由帧/局部变量掌管）。
- **leakcheck/arcguard 这些「conformance 派生 foreach」的 `_extra_args` 链必须随
  conformance 主链同步（本轮实锤）**：conformance foreach 有
  server_mvc_timezone（+Fmt.zan 源）、file_embed_bytes（`--embed`）等特例分支，
  leakcheck foreach 漏配时这两个 twin **静默红到底**（embed 缺失 → File.Exists 首断言
  出 0 → 用例自己抛的 FileNotFoundException 变 unhandled → 泄漏报告雪上加霜），
  又因 leakcheck 层不常跑、失败清单被 tail 截断，一直没人发现。修法照 arcguard
  的先例（它注明「Same branch as the conformance loop above」）把分支抄齐。配套：
  `run_leakcheck.cmake` 的产物 up-to-date 判定原来只看 (源, zanc, stdlib stamp)
  **不看 ZANC_ARGS**，args 修好后旧 exe 依旧复用、红不变——已加 `.args` sidecar
  记录编译参数，args 变了强制重编；`zan_drop_artifact` 同步删 sidecar。

## Worker.Stop() 只翻标志；停摆 accept 靠「关监听 fd」复位

- `Worker.Stop()` 语义是协作式（`running=false`，注释明言「不中断运行中的协程」）：
  循环 `while (running) { await AcceptAsync(sock); }` **停在 accept 里时永远看不到
  标志翻转**。与 TcpListener.Stop 的差别就在这里——后者会关 fd，挂起 accept 以
  -1 复位。Worker 用例的收尾定式：`Stop()` 之后必须补 `Socket.Close(listenSock)`
  （accept 返回 -1 → 回到 while 条件见 running=false → 循环退出 → `RunAllLoopsOnSockets`
  帧完成，socks List 与 Worker 对象链整体释放）。ws_loopback / ws_protocol_gate /
  mqtt_loopback / sse_stream 四用例的「用例 2 对象 + Worker.zan:280/297 2 对象」
  常红形状全是这一个缺口。
- 判「accept 靠什么唤醒」别信用例注释，读循环实现：注释说「停止会复位 accept 循环」
  的用例照红——写注释的人把 TcpListener 的语义记串了。

## 停摆的不只 accept：对端 close 唤不醒挂起的 recv，服务端帧同样钉住局部

- **坑**：测试里的 fake server 协程停在 `RecvOv` 上时，**客户端关掉自己那端不会
  唤醒它**——overlapped recv 不因对端 close 而完成（探针实证：accept 后挂起的 recv，
  对端 `Socket.Close` 后永不返回，服务端那帧永久可达）。帧本地若持有 `TdsBytes`
  之类的包装对象，就整批进 leakcheck 报告。sqlserver_tds 的「4 对象、站点
  `TdsCodec.zan:146:16`（`new TdsBytes`）」正是它：泄漏物是 **fake server 自己**
  的两个包装对象 × 2 个未退 Serve 帧，与编解码器无关——**站点名又一次张冠李戴**。
- **定位阶梯（最小探针全干净时必须回真实用例二分）**：去掉 Live() → 0；worker 数
  1/2/4 → 2/4/4（锁定在池段）；在 Serve 里 Release 后补 `body=null; pkt=null` →
  **clean**（坐实是「帧持有包装对象」而非内部缓冲）；插桩 serveLive/serveDone →
  退出时 `live=2 done=1`（两帧未退）。**判据**：泄漏数 = 未退帧数 × 帧内包装对象数，
  先数帧再数局部。
- **修法定式**：fake server 记下每个 accept 的 client fd，用例收尾关掉**服务端自己
  那端**（关对端没用），再自旋等 `serveLive==0`。`List<nint>` 这类静态字段的
  `xs.At(i)`/`xs.Count` **不能从别的静态方法直接调**（报「'xs' is not a known
  variable」），必须包一层 `ShutdownClients()/ClientsAlive()` 辅助方法。
- 与上一条 accept 同族：**测试里的停摆帧是 leakcheck 红的常见来源**（accept 停摆、
  recv 停摆），收尾必须让每个自建协程帧能退——「关自己那端的 fd」是通用把手。

## 集合查找内建吞掉 owned 实参临时：Contains/IndexOf/Dict 三兄弟

- **坑**：`List.Contains/IndexOf(item)`、`Dict.Remove/ContainsKey/TryGetValue(key)`、
  `d[key]` 读的代码生成把实参 `emit_expr` 出来后从不释放。实参是局部变量/字面量时
  无恙（帧收尾释放/借用/驻留）；写成**内联调用**——临时串唯一持有者是本次表达式——
  每次执行漏 1 条。真实形状藏在 HttpClient 重定向防环
  `visited.Contains(target.Canonical())`：每跟一跳漏一条 URL 串，
  http_bytes_redirect 漏 4 = 4 跳、http_client_redirect 漏 10 = 10 跳，
  泄漏数与业务跳数严丝合缝即是此坑的指纹。
- **缩小阶梯**（从真实形状到七行复形，一层层换血）：HTTP 重定向链 →
  裸 socket 服务器仍漏（锁定客户端）→ `RedirectNextClient` 换裸客户端仍漏
  （排除共享字段）→ 去掉 DropAlive 仍漏 → 常量实参仍漏（排除 URL 拼接）→
  同客户端二跳仍漏（排除派生客户端）→ 极小 while 循环干净 → 逐个加回
  Parse/GetHeader/ExternalTarget 都干净 → **只剩 `visited.Contains(...)` 漏** →
  语言级 `v.Contains(P.Build(2))` 七行复形。反向排除同样要逐件做：Add(调用())、
  普通方法实参、`List<int>`（值类型）全不漏——只有「RC 管理实参 + 查找类内建」
  组合踩坑。
- **修法定式**：查找类内建的 done/return 前统一补
  `emit_release_owned_call_temp(g, 实参节点, search, locals)`——帮手自带守卫
  （局部标识/非指针/非 RC/借用表达式自动跳过），str.Contains 早已是这么写的，
  List/Dict 五处（irgen_call.c 四处 + irgen_expr.c 的 `d[key]` 读）是漏网之鱼。
  新写消费 RC 实参的内建时必须过这个帮手，别自己判断要不要释放。
- conformance 用例 `arc_lookup_owned_arg` 六形态全调用实参化，leakcheck 孪生
  钉零泄漏；语义结果（at=-1 等）同 golden 钉死，防「释放修没修对、答案先错」。

## 抛出被调方吞掉 owned 实参临时：非泛型调用路径漏注册 EH unwind

- **坑**：setjmp/longjmp 异常路径下，调用方必须把 owned 实参/接收者临时注册进
  每线程 unwind 栈（`__zan_eh_tmp_push` / `emit_eh_tmp_push_slot` 打标），throw 时
  `emit_eh_unwind_to_handler` 才会释放到 handler 标记之上的一切。但代码生成里
  **只有泛型 spec 路径**（`emit_method_spec_call`）和一处 static-scall 块做了这件事；
  八条普通非泛型路径（本地接收者 mcall / 一般实例 mcall / 命名空间限定 static /
  扩展方法 extcall / 裸名 bcall / 全局 gcall / 接口分发 / 运算符调用）**只在成功路径
  释放实参**——被调方一 throw，longjmp 跳过释放，实参临时每次抛漏一个引用。
- **最小复形**：`Boom(Make(1))`，Boom 首行 throw，Make 的结果泄漏。IR 指纹：
  `%bcall = call ptr @Bag_Make` → `call i32 @Bag_Boom(ptr %bcall)` →
  **只有成功路径**才有 `zan_rt_release_dyn`，`call` 前没有任何 eh push。
  stdlib 层真实形状 = `db.Execute(sql, new DbParams())` 的 prepare 失败路径
  （Execute→Fail 抛出、BindAll 还没跑）——`db_error_throw` 的 5 对象漂移即此。
- **修法定式**：帮手 `emit_call_arg_eh_push`（与 spec 路径同守卫：RC 管理类型 +
  非局部标识 + owned 产出 + 指针类型；非 OBJ 类槽走 `emit_eh_tmp_push_slot` 打标，
  OBJ 直推），八条路径逐实参调用，**调用返回后按计数逐次 `emit_eh_tmp_pop`**。
- **自踩的坑（务必记住）**：push 是**条件式**的（借用实参不推），pop 若按 `argc`
  无条件弹就会**偷掉别的帧的注册**——首版即因此把 `db_error_throw` 从 5 对象
  **劣化成 14 对象**，且冒出全新站点（DbResult.zan:28/29、Model.zan:849、
  SqliteConnection.zan:292、test:146）。**pop 次数必须等于 push 次数**
  （`n_eh_args` 计数或 `arg_eh_pushed[]` 标志数组，LIFO 序）；运算符调用块原本
  连 pop 都完全没有，也要一并补。判据：泄漏站点从「预期形状」变成「一堆新形状」
  时，先怀疑自己的 pop 不平衡，别去查被调方。
- conformance 用例 `arc_throw_owned_arg`（**双实参** + 抛出被调方）：单实参时
  push/pop 偶发自动平衡，多实参才会把不平衡暴露出来——写这类用例别只用一参。

## object 槽的静态类型决定不了所有权：漏收 owned RHS 与头字当描述符解引用

- **坑一（漏收 +1）**：`emit_obj_local_store` 用初始化的**静态类型**判定槽是否接管
  所有权（`obj_slot_owns_value` → `is_arc_managed_type`），而
  `is_arc_managed_type(TYPE_OBJECT)` **恒为 0**——于是
  `object o = typeof(Box).CreateInstance(9)` 这类「RHS 产出 owned +1、静态类型是
  `object`」的赋值，+1 没人接管也没人释放，**每执行一次漏一个**。
  leakcheck_reflect_members 那个 `(null)` 名对象就是 `CreateInstance` 的返回值：
  `irgen_reflect.c` 的反射 ctor thunk 以 `LLVMConstNull(i8ptr)` 为站点名分配，
  所以报告里名字是 `(null)`——**看到 `(null)` 站点名先想反射 thunk**。
- **修法定式一**：`obj_slot_owns_owned_rhs`——RHS 静态类型是 `object`/`string`
  **且** `expr_yields_owned_rc_value` 为真时槽接管 +1。delegate/array 必须排除：
  它们各有自己的释放函数（闭包记录析构、数组长度前缀），走站点索引的
  `zan_rt_release_dyn` 会**绕过**它们、只把记录 free 掉。
- **坑二（默认构建段错误）**：描述符头（默认）构建里对象第二头字是描述符记录指针，
  `__zan_refl_obj_type`（`obj.GetType()`）与 `emit_runtime_is_check`（`x is T`）
  都直接 `inttoptr` 后 `+16`/`+8` 取字段。但 object 槽装**堆串**时该字是串标签
  `ZAN_STRING_TAG`（高位）+缓存字节长（低位），装**一维数组**时是
  `ZAN_ARRAY_MAGIC`，装**矩形数组**时是 rank（小整数）——三种都不是指针，
  解引用即崩：`object o = "lit"; o.GetType()` 一行必崩。
  **check-leaks 构建为什么看不出**：那条路用无符号范围比较
  `0 < site < ZAN_MAX_LEAK_SITES` 挡住串标签/垃圾字（`is` 侧还额外把串标签先分流），
  于是 `--check-leaks` 全程正常——**「两种模式语义分叉」的隐蔽 bug 只在默认
  （发布）构建踩**。排查这类「-g/--check-leaks 好、发布崩」的问题，直接怀疑
  描述符头解引用路径。
- **修法定式二**：解引用前判形状——`GetType()` 侧拒
  `zan_hdr_is_string` / `ZAN_ARRAY_MAGIC` / `site < 4096`（描述符是全局，
  绝不可能是小整数，矩形数组的 rank 由此挡住）；`is` 侧把判空扩成
  「0 / 数组魔数 / < 4096」并集。两条路都**退回静态类型的记录**，与 check-leaks
  构建对同一值的回答**逐字一致**——修完必须两模式输出比对，别再引入新分叉。
- 遗留（未修，独立设计变更）：`GetType()` 对槽里的串/数组仍答静态类型
  （`object`），C# 语义应答 `string`/`int[]`；要改需给两类载荷造记录并按形状分派，
  会同时改 check-leaks 侧既有答案。
- conformance 用例 `reflect_object_payload`：类/串/一维数组/矩形数组四种载荷的
  `GetType()`+`is` 一起钉，四孪生（conformance/leakcheck/arcguard/determinism）。

## Binding 活绑定弱引用契约与 out 字段写穿

- **A310（已修）**：`comp.prop = f().field;`（Binding<T> 属性 ← 字段左值、但接收者
  产出 owned 临时）曾合成活绑定——活绑定的 `object target` 是**刻意设计的弱引用**
  （`emit_binding_value` 注释：强引用会让 model<->component 图成环泄漏），而临时
  接收者在语句结束即被释放 → target 悬空，下一帧 `Get()` 读释放内存（轻则读出
  空串，重则 MeasureTree 段错误；堆复用探针 + `IsLive()` 观察可稳定实锤）。
  **修**：接收者 `expr_yields_owned_rc_value` 为真时不合成活绑定，降级为 const
  快照（`IsLive()==false`，语义自洽：临时源本来就不存在"活"可言）；live 路径里
  原来的临时接收者手动释放块随之变成死代码删除。持久接收者（局部/字段/容器元素）
  的活绑定语义不变，`binding_sugar` 等金样逐字不动。**铁律升级**：模板侧"先快照
  局部再赋 Binding 属性"的防御写法只对"绑定活得过源对象作用域"场景仍有意义；
  编译器现在兜底 `f().field` 形态，回归用例 `tests/conformance/binding_temp_source.zan`。
- **A307（已修，与 out_param_lvalue 同根因）**：`out` 实参目标是非标识符位置
  （`Fill(out this.v)`/`out b.field`/`out arr[i]`）曾走 emit_expr 返回**存值**被
  当地址写（int 字段存 7 就把 0x7 当指针）；修=emit_ref_arg 按位置解析真实地址
  （实例字段/静态字段/数组与 List 元素/裸字段名=this.field）。回归
  `tests/conformance/out_param_lvalue.zan`，2026-09-13 六形态补试零复现后闭账。

## A311 已修：设计类名撞 `Gui.App`

- 上半段记的「间歇性 Chart 全家 `undefined type 'App'`」**不是时序，也不是
  「Chart 排序靠后」**：三连 100% 复现，开关是 `partial class` 的逐文件
  上下文与全局命名空间。`.zform` 的 `"name"` 非 ident（如模板占位符
  `{{NAME}}`）时回退**文件基名**（App.zform→`App`），用户类与 stdlib
  `Gui.App` 同名，把下面三个缺陷一起点亮；`name="Root"` 同输入全绿。
- **三处根因**：
  ① `nr_walk` 把合并后的 partial 成员按**存活声明的文件**解析：
     `zan_parser_merge_partials`（`parser.c:4809`）把后续 partial 的成员折进
     **第一个** partial，且不合并各 partial 的 `using` 表；`nr_walk` 于是把
     第一个 partial 的 `ns_usings` 传进所有成员。`ChartView` 的第一个 partial
     是 `ChartBarLayout.zan`，它**没有 `using Gui;`**，其余 ~20 个 partial 里
     的 `App app` 形参因此全解析不到（=那 100 个 `undefined type 'App'`）。
     **修**：`nr_walk` 入口优先采信节点自己在 parse 期打的 `ns_usings`/
     `ns_name`（`zan_nsresolve_stamp` 逐文件、在 merge 之前跑，每个节点都带
     对了自己的文件上下文）——凡走「合并多个来源的声明」的遍历都要这样。
  ② 「同命名空间优先」被 `if (ctx_ns.len)` 跳过**全局作用域**：全局命名空间
     也是命名空间，全局 `App` 必须压过 `using Gui;` 的 `Gui.App`。旧代码在
     `ctx_ns` 空时直接掉进 `using` 分支绑成 `Gui_App`。**修**：无条件
     `find_full(join_ns(ctx_ns, R))`（空 `ctx_ns` 时 `join_ns` 返回裸名）。
     **坑中坑：这类判定有两处，必须同修**——`resolve_ref` 管**类型位置**，
     `resolve_static_receiver` 管**表达式位置的 static receiver**
     （`App.OnLoad(form)`）；只修前者时 `.zform` 生成的全局 `partial class App`
     仍把 `App.OnLoad` 绑到 `Gui_App`，报 `'Gui_App' has no member 'OnLoad'`。
  ③ **潜伏 stdlib 缺陷**：`Gui/ChildWindow.zan`、`Gui/UserComponents.zan`
     住在 Gui 模块却**没写 `namespace Gui;`**（同目录另外 43 个文件都写了），
     此前只靠②的 `using` 兜底才把 `App` 解析成 `Gui.App`；②去掉兜底后立刻
     暴露 `cannot convert 'App_2' to 'Gui_App'`。**教训**：模块目录下的文件
     一律显式写 `namespace`，别指望兜底——兜底一旦收紧，这类文件成片爆。
- 最小复现：`App.zform name="App"` + `partial class App` + `--auto-stdlib`
  （PRE 101 错 → POST rc=0）。conformance 用例：`conformance_nsctx`
  （`tests/conformance/nsctx/`，5 文件覆盖①跨文件 partial 上下文 + ②全局
  声明压过 import）。
- **仍留（用户命名问题，非解析缺陷）**：项目叫 `App` 时用户类仍撞 stdlib
  `Gui.App`，nsresolve 改名 `App_2`，stdlib 的 `Style.Of(App,…)` 形参不接受
  它。是否让 GenForm 回退基名时对 stdlib 已占用类名报错/加后缀属产品取舍
  （静默加后缀掩盖撞名、报错拦住合法同名局部用法），本轮未采纳。

## 生成器缓存键曾漏哈希闭包内文件

**坑**：`zan_gen_ensure` 的缓存键原先只哈希 `System/Compiler/` 下 9 个固定
文件；而生成器 exe 是 `zanc ZanGen.zan --auto-stdlib` 编的，其行为由整个
stdlib 闭包定义（GenForm P7a 起引用 System/Web/DesignerHtml.zan，GenHtml
本就引用 Html.zan）。结果：改了闭包内非 Compiler 文件 → 键不变 → 复用旧
生成器 → "stdlib 已修、行为依旧"的幽灵，且无任何诊断（编译/链接全绿）。
**实锤过程**：e2e 里 .zform 版全绿、.html 版静默不投影；清缓存强制重建后
立刻好——与代码逻辑无关，纯缓存键漏文件。

**现状（已修）**：键哈希 stdlib 根下全部 `.zan`（FindFirstFile/POSIX
recurse 收集相对路径 → qsort 定序 → 逐文件哈希路径+内容）。以后给生成器
加源/改 stdlib 闭包内任何文件，无需再动 genrun.c 的清单；kGenSources 清
单已删。临时绕过手段（诊断用）：删 `%LOCALAPPDATA%/Zan/gen/ZanGen_*.exe`
强制重建——看到 "zan: compiling code generators" 才是真重建。


## ORM 表访问器是编译期生成的（GenDb），新 [Table] 模型零接线

**坑**：server-collab 控制器里 `this.OaMessage.Where(...)` 在整个模板源码
里找不到任何属性声明——差点按"漏了接线"去翻 DbContext/AdminController。
实际是 stdlib `System/Compiler/GenDb.zan` 在编译期重写：`obj.<Entity>`
（<Entity> 匹配 [Table] 类名）整体替换为 `__DbBind.Q_<T>(obj.__Conn())`
等绑定树（指令 db_acc_head / db_acc_root）；`db.Select<T>()/Insert<T>/
Update<T>()/Delete<T>()/SyncStructure<T>()` 根调用同样重写（db_root）。
任何带 `__Conn()` 的类（模板 AppController 的请求租约）自动获得全部实体
访问器。

**定式**：加新模型 = 新建 [Table] 类文件即可，控制器 `this.<Entity>`、
裸连接 `db.Select<T>()`、`SyncStructureAllAsync()` 加列全部自动生效，
无需任何注册/清单；存量库加列后旧行 NULL 读作 0（哨兵语义，见
tenantId 回填先例）。另：`Insert(x).ExecuteIdentityAsync()` 的返回值才
是自增 id，且**不回写** `x.id`。


## File 读族 alt-base 回退 exe 目录 vs Directory 清理 CWD 相对：测试缓存目录必须用绝对路径

**坑**：GUI 用例报 hits==0 而行数据照常到达（conformance_gui_httpsource
连红两天）。根因是两套锚点不同树：`File.Exists` 相对路径 miss 时经
`rt_file.c zan_file_attributes` 的 alt-base 链回退（base0=ZAN_PKG_DIR、
base1=**exe 目录**）——测试 exe 在 build/，于是读到 build/hts_cache 里的
陈旧缓存；而 `Directory.DeleteRecursive` 走 ExistsOnDisk/相对删除，锚的
是进程 CWD（ctest 下=仓库根）。启动时把 CWD 侧缓存删得再干净，exe 侧
残留照样被 File 读族端上来；写路径（WriteAllText 等）又是 CWD 相对——
读、写、清理三边各锚一棵树，用例永远无法自清理。

**定式**：程序运行期自己创建的缓存/临时目录，路径一律用
`ProcessHost.AppDir() + "/name"` 绝对路径（绝对路径 `path[1]==':'` 或
前导 `/` 直接跳过 alt-base，三边天然同树）；只有"随程序分发的只读资源"
才该用相对路径享受 alt-base 的发布查找。排查口诀：见"删了还在/读不到刚
写的"先问一句 File 和 Directory 是否锚在同一目录——
`ProcessHost.UseAppDir(marker)` 是现成的整树切换开关。

## 加 [DllImport] 命名属性旗标的完整穿线（A2-3 Variadic 实录）

`Variadic = true` 从语法到代码生成的穿线清单，加任何 DllImport 命名属性
照抄：① `parser.c parse_attr_usages` 解码命名实参（argname 比对 +
AST_BOOL_LITERAL 判值）→ 新 out 参；**顶部还有一份 4 参前向声明，
改签名别漏**。② `ast.h method_decl` 加 `bool is_variadic`。③ arity 三处
口径必须同步：checker `method_arity`（返回 false=不判，check_call_arity
靠它跳过；变参仍要给"至少 N 参"下限诊断就在它的调用点补）、checker
`method_accepts_argc`、irgen `method_accepts_arity`（resolve_overload 的
唯一实现在 irgen.c——整个 irgen*.c 是一个 TU）。④ 声明点
`irgen_emit.c is_extern_decl` 分支：`LLVMFunctionType(..., isVarArg)`，
且**跳过 abi_extern_thunk**（varargs 无法转发，struct 变参本来也没有更
好的 ABI）。⑤ 尾参 C 默认提升放 `irgen_arc.c coerce_args_to_params`：
i1→zext i32（bool 是 0/1，sext 会变 -1）、i8/i16→sext i32、f32→fpext
f64、i64/指针原样；irgen_call.c 的"global LLVM function by name"路径有
自己的 coerce 循环，两处都要加。⑥ 验证：正例（等参/超参/运行期 string
尾部/double 尾部/返回值）+ 负例（少参 qualified 报源码错误、bare 落
verifier 属既有缺口）+ 全量 existing-extern 回归。

## stdlib 文件去掉 using System.Threading 的省税原理（A56 第二批实录）

**机制**：irgen_emit 的 extern 声明发射按"类成员被拉进闭包"就发生，与是
否真的调用无关；Threading.zan 里 zan_thread_*/zan_shared_* 等声明一被
发射，uses_sync_runtime 前缀旗标即置位，rt_sync.o（线程+共享表运行时）
整个被拖进链接。所以哪怕只用了一个 `Thread.Sleep`，`using
System.Threading` 的代价都是整个 rt_sync。

**定式**：stdlib 轮询等待里的裸睡眠，用文件内
`[DllImport("kernel32", EntryPoint = "Sleep")]`（POSIX 用 crt `usleep`，
微秒单位）——Win32Shell.SleepW、Guard.WinSleep、Automation.Window.SleepW
是三个同款先例；调用点都在 `#if WINDOWS`/`#elif LINUX` 内，未编译分支的
声明不发射、不产生未定义符号。**验证口诀**：`ZAN_TRACE_SYNC=1 zanc ...`
看 [sync-flag] 行，改动前后对比应为归零；注意 TrayIcon 这类"真线程"
（Thread.Start 消息泵）的税是正当的，不要误杀。

## zig 交叉的 linux 产物是 musl 静态链：dlopen 不可用，可选动态依赖全灭

**坑**：`--target linux-x64`（zig 交叉）产物里 `Interop.Load`/任何 dlopen
直接报 "Dynamic loading not supported"——musl 静态二进制不支持 dlopen
（musl 的已知限制）。Lua/Python/SDL 这类运行期 dlopen 的可选原生依赖在
交叉 Linux 产物上永远 IsAvailable()==false，且裸名/限定路径/CDPATH 都
救不了。排查时先在目标机跑一个 `Interop.Load("libc.so")` 探针分清
「库没找到」还是「dlopen 本身不可用」。

**定式**：交叉 Linux 产物的可选原生依赖要么随包静态链进主程序
（[DllImport] 声明 + 链接期解析），要么文档声明仅桌面动态链可用；
验证沙箱/加载器类改动用 Windows 实机（lua54.dll 铺 exe 旁即可真跑，
注意 lua_embed_smoke 的 env-skip 语义：无 Lua 时打印同一 golden，
ctest 绿≠断言跑过，必须另写显式探针）。
## 运行时 abort 死点普查要按符号抓头文件帮凶 + zan_rt_fatal 接管钩子（A52-8 实录）

**坑**：普查"库内哪些地方会硬死"时只 `grep abort()` 会漏一半——`zan_host_oom()`
（fprintf + abort）定义在 `src/common/host_oom.h` 里，rt_sync.c 六处 OOM 死点、
rt_timer.c 一处全部借它藏身；另有三处历史 abort 已改优雅路径但注释里留着
"abort()" 字样，纯文本 grep 三向误报/漏报。正确姿势：先抓 `abort()`，再抓
"包着 abort 的辅助函数"的调用点（`grep zan_host_oom`），最后逐处读上下文
分辨活死点 vs 注留史。

**定式**：运行时不可恢复死点（OOM/slab 一致性/契约违反）现在统一走
`zan_rt_fatal(category, message)`（rt_timer.c，每程序必链）：默认打印
`zan runtime: fatal (cat): msg` 后 abort（与历史行为等价）；嵌入方可
`zan_rt_set_fatal_handler` 注册回调接管——回调里自行 exit(宿主码)，
**回调返回=违约**，运行时仍 abort（现场不可恢复，绝不许继续跑）。
新死点直接调 zan_rt_fatal，不要再造裸 abort，也不要再引 host_oom.h
（那是编译器侧工具链用的）。改动 rt_sync/rt_mem 这类被测试目标**子集链接**
的文件时，记得给只链部分的 ctest 目标补链 rt_timer.c（CMake 已修三处）。

**顺手坑**：CMakeLists 的参数列表里 `/* ... */` 不是注释，会被拆成参数
传给命令——CMake 注释只有 `#` 行注释。

## async 启动语义与 sched_run 泵等待：三类静默挂死/丢消息（2026-09-23）

- **丢弃 async 调用（`Foo();` 裸语句、async void）的体在首次调度泵时才运行**，
  不是 C# 的急切执行：emit_expr 只发射 ramp（分配堆帧、返回句柄），
  AST_EXPR_STMT 对该句柄调 emit_detach_async_call 排队——首拍前什么都不跑。
  依赖"调用点立刻执行前导"的逻辑必错：WsSharedBus.StartPolling 把
  `lastReadSeq = 当前 head` 写在 async 体首行，调用点与首拍之间发布的消息
  全被当旧序号跳过（ws_cluster_bus 红）。修法=快照放同步方法（StartPolling
  同步快照 + 内部派生 PollLoop），不要指望改编译器语义。
- **`zan_co_sched_run` 后台分支的等待条件绝不能含 `zan_co_live_count`**：
  Task<T> 的 spawn 刻意留帧不收割（Result 读完才 reap），存活≠有工作。
  条件里有 live_count 后，任何 Wait/Result 泵都死等一个永不归零的计数
  （cs_b15_task 120s 超时，831577d60 引入）。可调度状态已被四个计数覆盖：
  queued(pending)/io/timer/running。
- **SharedTable 的列必须建表时声明**：`zan_find_column` 对未声明列名返回
  NULL，Increment/GetInt/GetInt 全链路静默得 0——无诊断、无崩溃，
  只有业务断言失败。WsSharedBus 用 "totalConns" 列却从未 ColumnInt 声明，
  连接计数恒 0。
- 排查手法：这三类都"测试红但单点看不出"——先写最小 Zan 探针分离
  原语与组合层（SharedTable 直连两实例→原语 OK；复刻 WsSharedBus 建表→
  列声明即现形），再用逐拍打印暴露调度时序（轮询体的真实启动时刻）。

## android 静态驱动档案 libzan_gui.a 过期 = 实机 dlopen "cannot locate symbol" 崩溃（2026-09-16 实录）

**坑**：`--emit-apk` 的 libmain.so 链接是 `-shared`，lld 默认允许未解析符号
（等运行期解析），stdlib/Gui/Render.zan 新增的 `static extern`（如
`zan_gui_font_ascent`）在**过期的**
`stdlib/Gui/drivers/android-{arm64,x64}/static/libzan_gui.a` 缺符号时
**链接照样成功**，装到手机/模拟器一启动就
`UnsatisfiedLinkError: dlopen failed: cannot locate symbol` 闪退。编译期
零报错，只有实机 logcat 有真相。win-x64 的 DLL 驱动在重导出 def 时会
当场报错，android 的静态档案不会——同源改动只炸移动端。

**定式**：① 动 `src/runtime/gui_runtime*.c` 或给 stdlib 加跨平台 extern 后，
重编 android 驱动档案：`bash scripts/build_gui_android_static.sh all`
（NDK clang 单 TU 编 gui_runtime.c `-DZAN_GUI_ANDROID_NATIVE
-DZAN_GUI_FREETYPE` + FreeType 模块成员，llvm-ar 打包；FreeType 用裁剪版
ftmodule.h——脚本从上游头自动派生到 build/android_gui_drivers/ft-inc/，
只登记实际编译的模块，否则 ftinit 引用未编译的 sdf/svg/type1 等
driver_class 直接链接失败）。② 验证符号齐：
`llvm-nm libzan_gui.a | grep T zan_gui_font_ascent` 对照 Render.zan 全部
extern。③ 编译器侧已加保险：main.c android `-shared` 链接行加
`--no-undefined`，档案过期从"实机闪退"左移成"发布当场报错"。

## embedres：skin_filter 只属 skins 规格；多 spec 别拆多次 emit（2026-09-17）

- **`zan_embed_emit_specs_filtered` 的 skin_filter 是 skins 规格专属**，
  过滤条件必须按 `prefix=="skins"` 收窄（embedres.c 的 `filtering` 判定）。
  它曾无条件作用于所有目录规格：一个只有子目录、根上无散文件的
  assets 规格在 depth-1 只保留 dark/light 包名，整树清零后掉进
  `--embed '...' matched no readable file` 硬错误。坑出处：wuwei 迁移
  （assets/ 只有 audio/、images/ 两个子目录），dev 构建当天起全红。
- **不要"修"成逐规格循环调用**：每次 `zan_embed_emit_specs*` 会新建独立
  资源表、资源名全局编号（`zan.embed.n%d`）从头起，后一次 emit 覆盖前
  一次——`--publish` 的多规格（packed-data、assets、skins）只剩最后一个
  spec 的资源，embeddedArt/embeddedMusic 全 0 但程序照常启动，只有
  探针断言能暴露。要改作用域就在 embedres 内部按前缀收窄，保持单次
  调用。回归用例 conformance_file_embed_subdir（显式 --embed 子目录树 +
  程序引用 Skin_ 前缀让 skin_filter 就位）。
- **auto-embed 锚定扫描的路径裁剪**：候选 = 首个输入源目录截断 + 父目
  录截断 + package_project_root，输入路径的 `/` 与 `\` 混用时 strrchr
  取的是最后一个分隔符——build.ps1 里 `Join-Path $project 'src/App.html'`
  这类混合分隔符路径会让截桶错位。诊断时先打印候选再怀疑逻辑。

## GenForm：带字 label 的字段名会被 text 的 syncName 吃掉（2026-09-17）

- **PropSpec.Text 工厂自带 `syncName=true`**：`SetProp("text", …)` 会把
  控件 `name` 改写成文本。设计稿通道里 html 元素内容走兜底直通发射
  `SetProp("text", …)`，在 `.name = 字段名` 之后执行——所有带文案的
  label 字段名被标题覆盖，Find/id.探针/生成字段三者失联；空 label 不受
  影响所以小样全绿、整页才塌。修法：GenForm 兜底循环发射 `text` 键后
  立刻回写 `.name`（FormBuilder 通道同款不变式"属性写完以设计的字段名
  为准"）。**作用域只限 `text` 键**——data-x-props 的同步（BatchJobProgress
  的 titleText 按 caption 检索，gui_batchjob 断言依赖）是设计意图，无条
  件的收尾回写会把那批测试打红。回归用例 conformance_gui_design_label_name。
- **定位这类"整页塌缩"先读 UiDriver dump tree 的 rect，别猜像素**：
  x/y/w/h 直读画布坐标，一处 237×0 与一处满高差一眼可辨；配合
  `dump hitregions` 看当帧可见区域集。塌缩的第一嫌疑是"父容器尺寸没
  到位/子树从未 Arrange"，而不是控件重叠。

## async 帧槽：按名字去重必炸，同名遮蔽声明各占一槽（A31x，2026-09-17）

- **`async_scan_add_local` 的去重键是 AST 声明节点，不是名字**。按名去重
  时，`foreach (string k in ...)`（no_arc 借用槽）之后同名的
  `string k = ...`（owned）会寄生在借用槽上：绑定期 capture-release 把槽
  里残留的**集合内部元素**当 prior 值释放——野释放字典/列表内部字符串，
  后续查找在 strcmp 上段错误。这就是 A321「后台协程 × 并发请求 → 传输层
  损坏、进程不崩」的机理：后台协程里野释放的正是别的协程在用的串。
- **发射侧必须把 `w->alocals[k].decl` 写进 `local_var_t.async_decl`**（写
  侧），绑定期 `local_find_async_decl` 才能按节点命中；只落读侧不落写侧
  =查找恒 miss 回落按名=回到 bug。
- **用户声明槽的作用域名前缀 `$fl<k>.`**（对按名查找不可见）：名字可见性
  从 prologue 预登记移到绑定期别名条目（`frame_owner` 指回槽条目下标、
  自身 arc_owned=0 对一切释放遍历惰性）。**合成槽 `$fe.*` 千万不能跟着改
  名**——foreach 降级按名找 `$fe.c%d` 接 col_slot，改名后 col_slot=NULL，
  cond 块跨挂起复用集合 SSA，LLVM 验证报 "does not dominate all uses"
  （三个 await-in-foreach 用例全红）。
- **别名赋值的 capture-release 必须门在 rc 托管指针类型上**：标量槽走普
  通存储。无门时 `i = i + 1` 的 i64 中间值 store 进 i32 alloca——越界砸
  邻槽，且 `emit_rc_release_for_type(int,...)` 形同虚设。
- **判定手法**：段错误先 cdb 看现场（strcmp 解引用 rcx=0 → 字典内部 key
  被野释放 → 往 ARC 所有权簿记查）；IR 存疑用 `--emit-ir` + 
  `ZANC_DUMP_BAD_IR=1` 落盘坏函数；归因用 targeted stash（只 stash 自己
  的文件）重编基线 zanc 跑同用例。契约用例
  conformance/async_shadow_same_name_across_await 四形态绿为准。

## 二进制格式补丁器（APK AXML 字符串池）：改解析先写 walk 探针（2026-09-23）

- **ResStringPool 两种池两种长度形式，别按单一形态写解析**：UTF-16 池
  条目=u16 长度前缀，≥0x8000 时扩成 32 位形式（低 15 位<<16 | 下一个
  u16）；UTF-8 池条目是**双前缀**（u16 单元数 + u8 字节数，各自 ≥128
  时扩为 16 位形式 `0x80|hi,lo`）——aapt2 产物真实如此，只认单前缀
  要么显式拒绝长串、要么整体错位。重造条目必须按规范双前缀
  （坑出处：APK manifest 补丁器见高位就拒绝，包名/标签 >127 构建
  失败；配置侧 128 字节 snprintf 静默截断成错包名更糟，均改显式报错）。
- **未触碰的池条目原样 memcpy 保留（前缀+数据+终结符），不要重编码**——
  重编码改偏移会牵连引用它的树块；只有替换目标用新编码。
- **改二进制解析器前，先用独立脚本线性走查模板自洽（walk 探针）再动手，
  改完立刻跑探针**：把硬编码常量参数化重构时，分支里忘重置派生变量
  （如 UTF-16 分支长度前缀字节数忘设 2）读侧整体错位 1 字节、占位符
  全找不到——先建好的探针当场抓住，没让它流进提交。
## 泛型 TP 按简单名全局注册：用户同名类击穿一切泛型方法调用（2026-09-23）

- **症状**：用户声明 `class T` 后，泛型类的成员方法调用
  全线 `no overload of 'Binding.Set' matches argument type(s)`（checker
  沉默，irgen 打分拒绝）；`Get()` 正常（0 参不经过参数打分）。桌面/
  android 无差别，与拉入面无关——**同名类在哪，毒就在哪**。
- **根因双层**：① 泛型 TP 以简单名注册进单元作用域，`scope_find` 命中
  **任何**同名符号（含用户类）即跳过注册，TP 名被抢；② 绑定期签名解析
  在类局部 scope 里是健康的（无条件注册 TP），但产物只落返回类型
  （`msym->type`），**参数类型没落**——irgen 对签名类型引用做裸
  `resolve_type`，在调用点作用域重解析就命中用户类，`Set(T)` 的 T 变
  具体类 → 实参全不匹配 → 唯一重载被判死。rt_type 缓存只在
  binding_done 后写，绑定期健康解析**不进缓存**，别指望它。
- **修法**：方法符号 members[] 上本就有绑定期落下的 SYM_PARAM 子符号
  （decl 指回参数节点）——irgen 的 `method_param_type` 优先按 decl
  命中取绑定类型，fallback 才 resolve。**铁律：irgen 不得对方法签名里
  的类型引用做裸 resolve_type，一律走绑定期落下的产物**（返回类型、
  字段类型早已如此，参数是漏网之鱼）。
- **定位手法**：报错形状矛盾（conformance 绿、新探针红）时，diff 通过
  与失败用例的**最小形状差**——本轮一眼扫过去是"接收者形态"（局部/
  字段/参数全红，假象），真差异是**类名**（Program vs T）。改类名二分
  一击定位，比读打分代码快一个数量级。

## 合成值进 store 必带 owned 信号：算了没用的 fval_owned = 半截线（2026-09-23）

- **症状**：leakcheck 门禁孪生红，退出恒剩 1 个 Binding 盒；探针二分
  只有"对象初始化器写 Binding 字段"这一种形状漏（纯赋值、局部声明、
  两次普通赋值全绿）。
- **根因**：`emit_binding_value` 交出 **+1 新盒**，但初始化器路径把
  **原始 RHS**（字面量/参数=借用）递给 `emit_rc_store_field`——所有权
  测试跑在 AST 节点上，判定借用再 retain 一次，盒 rc=2 落字段，出口
  级联只放一次。普通赋值路径靠**换 dummy AST_NEW_EXPR 节点**（
  `expr_yields_owned_rc_value` 对 NEW 恒真）传递 +1 信号；初始化器里
  `fval_owned` 标志算了**从没接线**。IR 直读 `--emit-ir` 的
  `retain %bindobj → store → release old` 序列一锤定音。
- **铁律**：任何 lowering 合成出 +1 值再走共享 store 路径时，所有权
  信号必须**随值一起交接**（dummy marker），不能指望 store 路径"知道"
  调用方上下文；新糖落地时 grep 一遍 `fval_owned`/同形标志是否真被
  消费——算了没用的标志就是断线的信号。
- **定位手法**：leakcheck 红先做**形状二分**（删构造器/删初始化器/
  换局部），一个维度一轮 30 秒；判 ownership 争议直接 `--emit-ir`
  数 retain/release，比读三层调用链快。
