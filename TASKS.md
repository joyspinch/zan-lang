# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。领账按批次：批次 1 根修优先（闭一条解锁一片），批次 2 独立账可并行，批次 3 等前置闭了再领，批次 4 大工程须独占窗口；同批内按编号序。每条正文前的〔…〕是依赖标签：〔前置 X〕= X 不闭本条闭不了；〔软前置 X〕= 没 X 也能做但成本高；〔根修·解锁 Y〕= 本条闭了 Y 的前置随之解除；〔串行前置 X〕= 与 X 同改一个文件，等 X 落地再动。根修闭账时同批摘掉下游条目的〔前置〕标签并上调批次。领账前 `git status` + 文件 mtime 查在飞——并发会话不一定走调度器注册（2026-11-03 B-ID107 即靠 mtime 抓到撞车）。
>
> 最近平账 2026-11-03（彻底清理）：全账按「只记剩余欠账」压缩，验证流水归 git 历史；B-ID95/98/100/105 闭账移出（导出面 devirt 预修 33f21d49e 落地且场景暂不可达 / OOM 注入钩子+三钉 55367b004 落地 / 预扫缓存收益 ~12ms 低于噪声底不做 / json 消费方 dap_integration+lsp_integration_utf16 绿）；B-ID68/69/70 归并指针行移出（B-ID75 领域点名承接）。同日晚：B-ID113 闭账移出（stdout 二进制模式 25ca931ff，字节钉 stdio_binary_mode 7/7 绿）、B-ID108 闭账移出（JsonDoc 槽位三访问器 9a40bdf88，AppUpdate --emit-lib 77 文件绿 + Json 回归五钉绿）。早前同日：B-ID80 SipHash 落地闭账、B-ID66 闭账移出、B-ID104/105/106/110 各片落地（详 git 历史）。同日晚 ORM 族平账：orm/linq/diag_orm/dao 35 案实测 31 绿 4 红——B-ID59 余量收敛为两案（"orm 族 ~20 案红"过时移除）、case_orm_upsert（E3023 OnConflict 匿名对象）红面归 B-ID65、B-ID107 症状复核成立（arpg_save_repository `near "."` sqlite 语法错，基线同红）。同日按前置依赖把全账 33 条重排为四批次（根修 / 独立 / 有前置 / 大工程），每条加〔依赖标签〕，正文不动。同日 B-ID107 闭账移出（Name=短名/FullName/TableName 兜底短名 e75ce25e4，arpg_save_repository 转绿 + 反射三钉金标随契约翻转 + ORM 族 16 绿；selfhost ngen_reflect 镜像同步余项并入 B-ID109）、B-ID104 收窄（JRE 钉扎+哈希校验 bde79b74a 落地，余安装器移出）、B-ID93 定性改写（行号发射整体缺失，非 async 特缺）、B-ID76 闭账移出（--checked 全局开关落地，同一源双面钉 checked_arith_default/flag，既有 checked_overflow_coverage 回归绿）。同日 B-ID83 闭账移出（internal 强制镜像：parser 记 MOD.Internal + checker 模块可见性判定，模块=ns 前两段对齐 C11；reject access_internal 三字段钉双编译器同措辞——自举拒 + 宿主同句 + 201 diff 案零误报，selfhost_diff 整体仍 B-ID109 预存红）。

## 未完成（按平账顺序分批）

### 批次 0 · 在飞（持有会话未提交；其他会话勿碰同文件）


### 批次 1 · 根修项（闭它解锁下游，优先领）

- [ ] B-ID63〔根修 · 解锁 B-ID59；selfhost 半与 B-ID64 共改 irgen_async.zan〕泛型系统协变/逆变与调用环境特化：余：接口/委托方差标记（out/in）补全解 E2108；异步状态机与闭包展开帧泛型环境上下文（Meta.For<T> 特化丢失，SyncStructureAll 静态直调绕开扩展分发同根）；selfhost 欠账：checker.zan 表访问器合成（recv.<Entity> → Dao/BaseRepository<E>）与 SyncStructureAll 编译期合成镜像、irgen_async.zan 帧槽接线。已闭子项见 git 历史（a00c09ba3、ed551f707）。
- [ ] B-ID64〔根修 · 解锁 B-ID65 泄漏半、B-ID79；与 B-ID93 共改 irgen_async.c〕异步状态机帧布局类型化与 ARC 生命周期闭环：`irgen_async.c` 与 `src/selfhost/irgen_async.zan` 目前将异步结果强制打包规整为 64 位整数槽（res.slot），大结构体与聚合槽逻辑复杂；需演进为类型化返回值字段（Typed Result Field），并在状态机终结时自动析构捕获帧，防止长驻异步任务产生 ARC 循环引用与连接泄露。
- [ ] B-ID75〔根修 · 承接 B-ID72；B-ID67/B-ID86 的平台层同根〕全仓平台抽象层（PAL）统一标准落地：终结全仓随意穿插 `#if WINDOWS`、写死 `kernel32`/`user32` 与平铺单平台实现的局面。确立"抽象契约 + 平台物理目录隔离（Platform/Windows/Linux/Mac）+ 统一工厂/构建装配"，覆盖四大领域：(1) 文件与路径系统（路径分隔/文件锁/目录监听，承接原 B-ID68 路径规范化与大小写保真校验）、(2) 进程/管道与环境信号、(3) 网络事件循环（IOCP/epoll/kqueue，承接原 B-ID69）、(4) 动态库加载与错误码映射（dlopen/LoadLibrary/errno，承接原 B-ID70 DllImport 平台库名映射与 SafeHandle 作用域析构）；双编译器双轨保持一致。
- [ ] B-ID102〔根修 · 软解锁 B-ID77（流分析遍历）、降 B-ID62 改 switch 成本〕AST 统一 child-iteration/visitor 基建：tagged union 无 visitor 无统一 child 迭代器，binder/nsresolve/checker/irgen 五套手写遍历器，加一种节点要改 10+ 处 switch（已发生 B-ID101 型真实漏改）；lexer_peek_n 双实现（parser.c:2052 分叉自 lexer.c:1883）同族收敛。
- [ ] B-ID89〔工具先行 · B-ID64/B-ID65 泄漏定位缺它只能靠 --check-leaks 退出报告〕生产级内存可观测性与循环引用泄漏分析工具缺失：纯 ARC 模型循环引用必泄漏，全仓缺乏运行时孤儿环探测器（Cycle Detector）与 Heap Snapshot / Dump 导出，长驻服务端与复杂 GUI 内存缓增无定位手段；双轨保持一致。

### 批次 2 · 独立账（无前置，任一会话可领，按编号序）

- [~] B-ID73〔独立〕DateTime.Now 本地墙钟时间语义：`Now()`/`UtcNow()` 语义与 DST 边界已闭（conformance datetime_now，git 历史）。余：脱离操作系统时区库的 `TimeZoneInfo` 与 IANA 时区库（`/usr/share/zoneinfo`）统一支持——按任意时区名查询偏移、跨平台规则一致；现仅支持本机时区。
- [ ] B-ID81〔独立〕只读与不可变集合契约健全（IReadOnlyList / ReadOnlySpan 等）：标准库集合缺乏只读抽象约束，实体向外暴露内部集合时可被随意篡改或并发破坏。需补全 `IReadOnlyCollection<T>`、`IReadOnlyList<T>`、`IReadOnlyDictionary<K, V>` 接口及切片视图；双轨同步。
- [ ] B-ID82〔独立〕包管理语义化版本仲裁与钻石依赖求解器（Diamond Dependency Solver）：当前 `.zan-packages/` 仅支持简单扁平安装，同一包不同大版本缺冲突求解。需引入类似 PubGrub 的语义化版本依赖解析与包隔离机制；双轨保持一致。
- [ ] B-ID87〔独立〕Unicode 规范化（NFC/NFD）与国际化大小写折叠（Case Folding）缺失：标准库缺乏 UAX #15 规范化，`string.ToLower/ToUpper/Equals` 仅基本码点映射，欧洲带重音符/德语等单码点与组合码点无法等价匹配，存在鉴权仿冒越权风险；双轨同步。
- [ ] B-ID92〔独立 · 闭后助 B-ID109 gen1≠gen2 差分收敛〕静态构造（cctor）初始化序——selfhost 镜像欠账：C11 侧已闭（拓扑序发射 + conformance static_init_dependency_order，git 历史）；余：selfhost irgen.zan 的 Main 入口静态初始化仍按声明序，待镜像。惰性首次访问初始化（BeforeFieldInit）不做。
- [ ] B-ID96〔独立 · 纯性能，末位〕optimizer ARC 对消窗口余量：主洞已保守关窗（git 历史）；余：使关窗更早、提升对消率（纯性能，无正确性影响，无 conformance 直钉，靠代码审查覆盖）。
- [ ] B-ID104〔独立〕apk.c 打包面：余：JRE 安装器职责整体移出编译器（scripts/ 或独立 setup 工具）。JRE 下载哈希校验已落（钉扎 jdk-21.0.13+9 不可变 URL + SHA-256，ZAN_JDK_SHA256 覆盖面，探针 4/4）；axml 池头/长度前缀越界、失败路径泄漏、PowerShell 单引号注入已修（1752eeeeb）。
- [ ] B-ID106〔独立 · 内部序：diag 去重重做前先定位 0xC0000005〕运行时与公共库结构性收敛余项：rt_crash.h 1413 行实现住头文件被 3 TU 重复编译，拆 .c 为工程项（rt_io/rt_timer 是独立 .obj 定制命令拼链，需动原生/mt/wasm/bare 多条链路）；rt_mem slab 只进不出无归还 OS 路径（长驻进程高水位永不下落）；错误码四种口径并存（rt_file.c:113 返 0 与 epoch 0 不可区分为典型）；diag 精确去重已回退（21dde884c 在 831 文件规模编译触发 zanc 退出 0xC0000005，revert 落地；去重逻辑审读无内存病、单线程、free 序对称，根因未定位——疑大型编译既有堆破坏殃及签名表分配，重做前须先定位崩溃）。json 族/arena/tuple 签名/ferror-limit 已修（git 历史）。
- [ ] B-ID110〔前置 B-ID60〕Zan.Web 四案余红——routegen 删除后参数元数据无生产者：层1 编译红已修（__AttrRoutes 迁 Zan.Mvc 包致 using 缺失 + 包自带控制器被 Register 隐式发现吞掉；用例补 using + checker 发现跳过 Zan.Web* ns，web 族其余 13 案绿）。余红精确化：api_docs/list_protocol——Route.Param 参数行缺（654a90a7b 删 C11 routegen「收编自举轨」而自举轨未接，见 B-ID109，server-collab 模板同受影响）；typed_binding——__h_* 合成桩随 routegen 删除；menu_attrs——特性命名参数（Icon=/Name=）不进元数据串，B-ID60 同根。修法：从 654a90a7b^ 挖 routegen 参数提取适配新 AttrRoutes 契约重落。原登记：case_web_api_docs、case_web_list_protocol、case_web_menu_attrs、case_web_typed_binding——手编复刻缺 api-docs 参数行（page/limit/order/dir），指向 AttrRoutes 的 ApiDocs 生成链路在近期 MVC/attrs 重构后掉了参数元数据。gallery 两政策案已闭（97c7925dc，git 历史）。
- [ ] B-ID114〔独立〕lsp_session_editor 预存红——三层定性（2026-11-03）：层1 编译红（ZANPKG_MISSING System/Threading/Tasks→exit 3）已修：offender 是 `using Gui;` 层级包访问把 Gui.* 全子树拉进编译集，FirefoxAgent.zan 头部死 using（Task/Task.Delay 系内建，命名空间无实体）删除，修落在在飞文件上随其提交收口；ide_zan editor/nav 7 文件同款死 using 待同法清理。层2 zanc 退出崩溃为本会话 diag 去重回归，已 revert（见 B-ID106）。层3（现行失败点，真实预存缺陷）：编译全绿后客户端会话挂死——subprocess 20s 超时（run_lsp_session_test.py:280），客户端 Main 在 NavigationParsing/LspSession.Open/握手或 Poll 链路之一停滞 ≥20s，需运行时专项定位；同日 dap_integration、lsp_integration_utf16 绿（传输面无恙，挂点在会话层）。

### 批次 3 · 有前置（前置闭了再领）

- [ ] B-ID59〔前置 B-ID63〕ORM 查询链泛型推断断裂（E2108 泛型不变性）：`ToList<RouteStat>(a => new RouteStat {...})` 等链式投影报 cannot convert；余（2026-11-03 实测收敛）：case_orm_group_aggregate（E2108 三处投影）与 case_linq_and_zandb_opt（运行期零输出 access violation，接手先 _scratch 直执行定位）两案；orm/linq 族其余、generic_base_dao_orm、diag_orm 三例全绿（原"orm 族 ~20 案红"过时）；根修归 B-ID63（方差/泛型环境）。已修片段见 git 历史（diag_orm 三例、E2030 皮下钩）。
- [ ] B-ID60 反射元数据特性体系重构（解耦硬编码字符串）：`irgen_reflect.c` 与 selfhost 镜像目前以 `memcmp` 硬编码匹配有限特性名（Key/Required/NotMapped），命名参数与参数化属性（如 `[Column(IsPrimary = true)]`）无法进入元数据位表。需重构为结构化特性元数据流（Metadata Blob），支持反射面读取完整参数并保留 C11/selfhost 1:1 对齐。
- [ ] B-ID62〔软前置 B-ID102〕语言内置原语与方法横截重构：`irgen_call_builtins.c` 现存 3200+ 行根据方法名字符串横截调用（nameof/GetValueOrDefault/ToString 等），易与用户自定义符号或重载产生意外冲突。需演进为标准库 `[Intrinsic]` 符号级绑定分发机制，checker 期绑定符号而非 irgen 字符串匹配，C11 与 selfhost 差分保持一致。
- [ ] B-ID65〔前置 B-ID64（异步泄漏半）；表达式树/ON CONFLICT 半无前置可先行〕ORM 强类型表达式树与复合主键支持：余：只读表达式树（Expression Tree）AST 节点体系及 SQL 访问者（Visitor）翻译管线（复合主键 ON CONFLICT 下推）；异步仓储帧捕获链未随任务终结释放（退出 52 对象仍可达：Meta 静态单例链 + 连接内部集合，同步形态零泄漏）。红案面（2026-11-03 实测）：case_orm_upsert——`.OnConflict(a => new { a.minute, a.method, a.path })` 匿名对象键报 E3023（皮下钩只认 GroupBy/ToList/ToAggregate），即 ON CONFLICT 下推未落地的 conformance 直钉。bool-lambda Where/Delete/Update 语义已修见 git 历史。
- [ ] B-ID72〔前置 B-ID75（先有 PAL 目录结构再迁入）〕宽字符与文本封送跨平台断裂（Wide.zan 强绑 kernel32）：`stdlib/System/Interop/Wide.zan` 无条件 `[DllImport("kernel32")]` 导入 Win32 API 且写死 2 字节 UTF-16 假定，在 Linux/macOS 上直接崩毁，且违背 POSIX 4 字节 wchar_t 规范。需抽象为支持 POSIX iconv / mbstowcs 与 Windows 多分发的跨平台文本封送器；双轨同步。
- [ ] B-ID77〔软前置 B-ID102；B-ID112 同族随行〕静态空安全（Null-Safety）契约与可空引用类型体系：当前引用类型全部默认为裸指针，全仓依赖运行期判空与空指针异常崩溃。需在类型检查器引入现代空安全语义（如 `string?` 可空与 `string` 非空分离），在编译期截胡潜在空引用；双轨保持一致。
- [ ] B-ID79〔前置 B-ID64〕结构化并发父子生命周期联动：余：父子任务树联动贯穿 async 调用链——父协程超时或异常退出时子协程取消信号传播与任务树监控（CancellationToken 全家令牌基础设施已落，git 历史）；双轨同步。
- [ ] B-ID109〔部分前置 B-ID92/B-ID63-selfhost 半（gen1≠gen2 差分含镜像缺口）；gen2≠gen3 不动点另需二分〕selfhost_fixed_point / selfhost_diff 闭环红：gen1 编译通过，但 gen2==gen3 不动点不成立且 gen2/gen3 与 gen1 产物 diff 失败（ctest，预存失败，最早引入点未定位，需向前二分）。diff 门内另有 4 案 gen1 拒编（convert_int_semantics/bool_widened_rendering/builtin_parse_chain/animation_pipeline——`int/string/double.Parse` 进三元表达式的 E0045，selfhost 解析器静态方法调用洞，2026-11-03 实测，非可见性镜像引起）。修法方向：scripts/bootstrap.sh 复现，diff g2.ll/g3.ll 首个分叉定位（自举 irgen/ngen 与宿主 stdlib 子集语义差）；闭环时同批同步 ngen_reflect.zan 镜像 name 槽为短名 + FullName 槽（B-ID107 C11 侧已闭，自举链红无法单独验证镜像改动）。
- [ ] B-ID112〔前置 B-ID77 的生命周期/流分析基建 · 登记不修〕string.AsView 悬垂视图——语言级生命周期分析缺口（登记不修）：`StringExtensions.AsView(this string)` 返回非 owning 的视图结构，表达式临时 string 上取视图在语句末被 ARC 释放后即悬垂，后续读取越界/脏数据（探针实证 2026-11-03；同族面 StringBuilder/ByteBuffer.AsView）。根修在编译器生命周期分析（视图与源对象生存期绑定、视图逃逸禁释放），库级不可修；短期约束：文档明示 AsView 结果不得脱离源对象生存期使用。

### 批次 4 · 大工程 / 独占窗口（无其他 irgen/runtime 改动在飞时领）

- [ ] B-ID67〔大工程 · GUI 双轨；关联 B-ID75〕HTML+CSS 窗口排版与形状协议标准化（解耦魔术字符串与伪排版）：`FormBuilder.zan` 与 `App.Window.zan` 现存逗号分隔字符串传递异形窗口、硬编码 40+ 键名匹配、WinForms 式 Dock 模拟 CSS 的缺陷，在 Linux/macOS 上外观无法一致呈现。需重构为强类型 `WindowShapeDescription` 协议并演进为标准化 Flexbox 布局规范；双轨同步。（SetShape 等能力已进 IPlatformWindow 契约面，本条协议化改造在其上继续。）
- [ ] B-ID71〔大工程 · 独占窗口；关联 B-ID64 异常路径帧析构〕SJLJ（setjmp/longjmp）异常机制与 ARC 栈展开资源泄漏/锁死锁：跨函数 throw 直接恢复栈寄存器，绕过被穿越中间帧的析构逻辑（ARC 漏释放、using 不关、zan_monitor_enter 无 exit 全局死锁）；exception_handler_depth 真回归同源——O2 DCE 把 rethrow 路径写入的局部当 dead store 删（setjmp/longjmp 对 LLVM 是不可见控制流边；已证全量 volatile alloca 无效，longjmp 后 volatile store 仍被 CPU 级跳过）。须演进为零开销栈展开（LandingPad）根治；双轨保持一致。
- [ ] B-ID86〔大工程 · 独占窗口；关联 B-ID75 POSIX 层〕协程纯协作式调度线程饥饿与废弃 ucontext 依赖：`src/runtime/rt_sched.c` 缺乏时间片/信号抢占，CPU 密集长循环永久霸占调度线程致挂载其上的异步 I/O 假死；POSIX 下依赖已废弃的 `ucontext.h`（换栈拷贝信号掩码开销大）。需演进为轻量寄存器汇编保存与协作抢占插桩；双轨保持一致。
- [ ] B-ID93〔大工程 · 独占窗口 · 横跨全部 stmt/expr 发射点〕异步调试信息（Debug Info）行映射——定性完成（2026-11-03，范围比原登记更大）：DIBuilder 只建 CU/文件/discope 即 Finalize（irgen.c:98-260），全编译器**没有任何指令附着 DILocation**（唯一一次 SetCurrentDebugLocation2 是 irgen_emit.c:3059 拆除时置 NULL）——不是 async 状态机特缺，是语句/表达式级行号发射整体缺失，async $resume 单步直落合成代码只是显性症状。修法：stmt/expr 发射点统一走「当前源位置→DILocation 挂 builder」管线 + 异步帧把原始调用点位置带进状态机帧；双轨保持一致。
- [ ] B-ID103〔大工程 · 独占窗口 · main.c 并发禁动区〕irgen unity build 拆分：irgen.c 把 21 个 irgen_*.c 文本 include 进单 TU（约 52K 行），静态函数靠跨"文件"前向声明网维系插入顺序；main.c 在 main() 函数体内 #include "main_options_parse.c"（驱动不可单测）。修法方向：真 TU 化 + 声明网收编进头文件 + 选项解析独立编译单元。main.c 属并发禁动区，待窗口。
