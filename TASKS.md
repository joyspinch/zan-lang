# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

- [ ] B-ID57 server_mvc generator_sources 红：生成产物断言 "api json projection" 失败（Zan.Mvc 源生成器 Gen.zan 的输出内容，非 routegen）。已修闭：admin_gate 金样 193 为旧"属性壳"架构（387 动作/29 控制器）的内容快照，b1752059e 重构改直连动作（150 动作）后真值 173 且行为断言（gate/trio/preAnonymous/menu/txprobe）全绿——金样已按内容漂移更新；ai_endpoint/timezone 双案因 CMakeLists 残留 ZanWeb 旧路径瞬时红亦已修。
- [ ] B-ID59 ORM 查询链泛型推断断裂（2e3932369 DbExtensions 重写 +805 行的爆炸半径，HEAD 既有，非 11-01/11-02 批次引入）：E2108 泛型不变性——`ToList<RouteStat>(a => new RouteStat {...})` / `ToList(a => a.path)` 等链式投影报 cannot convert List<MetricMinute> to List<string>；case_orm_group_aggregate 双案、case_linq_and_zandb_opt、case_orm_metadata_concurrency、case_chart_data_range 等约 20+ 案红（server_mvc/zandb/orm 族 -j2 扫 129 中 24 红）。已修子项（本批）：diag_orm_where_int_string/string_int/set_string_int 三例——checker 对开放泛型 lambda 体不判成员类型（E2045 被静默跳过）、特化后 irgen 发 i32 vs ptr 的裸 icmp 被 LLVM verify 整模块击毙（E3090 报 0:0）；irgen 二元入口加比较类 int/ptr 混比守卫（E2045 当场诊断 + 返回常量假），三例期望串从 dbgen 时代的 "type mismatch" 改为现行诊断。
- [ ] B-ID60 反射元数据特性体系重构（解耦硬编码字符串）：`irgen_reflect.c` 与 selfhost 镜像目前以 `memcmp` 硬编码匹配有限特性名（Key/Required/NotMapped），命名参数与参数化属性（如 `[Column(IsPrimary = true)]`）无法进入元数据位表。需重构为结构化特性元数据流（Metadata Blob），支持反射面读取完整参数并保留 C11/selfhost 1:1 对齐。
- [ ] B-ID61 MVC 路由生成与动词推断正规化：`routegen.c` 与 `GenRoute.zan` 目前依据动作名前缀/子串（post/save/del/update/upload 等）启发式猜测 HTTP 动词，存在命名意外风险；未声明鉴权端点硬编码 `.Login()`。需重构为显式契约优先与标准化源生成体系，双编译器保持相同分发生成口径。
- [ ] B-ID62 语言内置原语与方法横截重构：`irgen_call_builtins.c` 现存 3200+ 行根据方法名字符串横截调用（nameof/GetValueOrDefault/ToString 等），易与用户自定义符号或重载产生意外冲突。需演进为标准库 `[Intrinsic]` 符号级绑定分发机制，checker 期绑定符号而非 irgen 字符串匹配，C11 与 selfhost 差分保持一致。
- [ ] B-ID63 泛型系统协变/逆变与调用环境特化：补全接口/委托方差标记（out/in 支持）解决 E2108 泛型不变性导致的 LINQ/ORM 链式投影类型转换断裂；在异步状态机和闭包展开帧中隐式压入泛型环境上下文（Type Context），根除嵌套或扩展方法中 `Meta.For<T>` 特化丢失问题；双轨同步。
- [ ] B-ID64 异步状态机帧布局类型化与 ARC 生命周期闭环：`irgen_async.c` 与 `src/selfhost/irgen_async.zan` 目前将异步结果强制打包规整为 64 位整数槽（res.slot），大结构体与聚合槽逻辑复杂；需演进为类型化返回值字段（Typed Result Field），并在状态机终结时自动析构捕获帧，防止长驻异步任务产生 ARC 循环引用与连接泄露。
- [ ] B-ID65 ORM 强类型表达式树与复合主键支持：建立标准只读表达式树（Expression Tree）AST 节点体系及 SQL 访问者（Visitor）翻译管线，支持从实体元数据完整提取多列复合主键用于 ON CONFLICT 解析，消除手工拼接字符串 SQL 的脆弱性与多列约束失真；双轨保持一致。现状（11-01 批）：bool-lambda Where/Delete/Update 已按「候选行物化→内存谓词过滤→主键 WhereIn/复合元组/SQLite rowid 下推」给出正确语义（旧值探针绑错列绑常量 1，静默打错行；大批量路径仍应显式列谓词，参 MetricsStore.Sweep），表达式树是把过滤下推为纯 SQL 的后续方案。
- [ ] B-ID66 GUI 跨平台平台后端抽象统一（抹平 Win32 与 POSIX 差异）：`packages/Zan.Gui` 目前对 Windows 强依赖 3000 行 `Win32Shell.zan` 裸露 Win32 API，而 Linux (X11/Wayland) 与 macOS (Cocoa) 仅有残缺 C 桥接桩，导致异形窗口、托盘、拖拽、IME 与多窗口在非 Windows 平台功能断裂。需抽象统一的 `IPlatformWindow` / `IPlatformShell` 驱动接口；双编译器双轨保持一致。
- [ ] B-ID67 HTML+CSS 窗口排版与形状协议标准化（解耦魔术字符串与伪排版）：`FormBuilder.zan` 与 `App.Window.zan` 现存 `"1,0,0,w,h,r"` 逗号分隔字符串传递异形窗口、硬编码 40+ 键名匹配、WinForms 式 Dock 模拟 CSS 的缺陷，在 Linux/macOS 上外观无法一致呈现。需重构为强类型 `WindowShapeDescription` 协议并演进为标准化 Flexbox 布局规范；双轨同步。
- [ ] B-ID68 跨平台路径规范化与大小写敏感校验（Linux/macOS 兼容）：编译器与标准库 IO 中存在反斜杠路径拼接假定，且 Windows 大小写不敏感掩盖了源码与包引用（如 `using System.Web` 路径）大小写漂移。需在编译器前端与标准库加入统一的 POSIX/Windows 路径正规化（Path Normalization）与大小写保真校验；双轨同步。
- [ ] B-ID69 存量 conformance 红定性清单（-j1 串行复跑已排除共享 fixture 争用，基线已红、非 11-01/11-02 批引入）：server_mvc_security_bootstrap（6.9s 即红，待查）；server_mvc_permission_branches（8 条路由门 FAIL + "branch permission regression"）；case_orm_pool（after commit 1≠2，事务提交计数）；case_orm_dynamic（src:0≠1、row2 值漂移，Select 侧 lambda 语义）；case_orm_sync_all（DbException: no such table: sync_all_a，运行期表场景）；case_zandb_fuzz（segs/log 段数金样非确定，model mismatches=0，需放宽金样或固定分段策略）；case_orm_table_accessor（AccUser 接口扩展 Where+CountAsync 经 ToList→MapRow InvalidCastException，DbExtensions.zan:462（T)obj，旧 Delete 探针 try/catch 吞掉的同类转换在 Select 面暴露）。E2108/E3023 投影孤儿族见 B-ID59，不再重复。
- [ ] B-ID69 网络与异步 IO 多路复用跨平台统一（IOCP / epoll / kqueue 抽象）：`System.Net` 目前缺乏跨平台高性能事件循环（Event Loop），在 Windows IOCP 之外的 POSIX 平台缺乏高效统一的 epoll/kqueue 反应堆封装，存在固定 Buffer 分配与假死隐患。需建立统一的非阻塞网络驱动层；双轨同步。
- [ ] B-ID70 跨平台动态库导入（DllImport）与非托管内存 SafeHandle 闭环：`DllImport` 平台库名硬编码（如只写 `user32` 或未加扩展名平台探查），导致非 Windows 平台动态解析失败；且平台互操作中广泛存在 `Wide.Of` / `Wide.Free` 裸指针手动释放缺陷。需引入跨平台动态库映射表与具作用域自动析构的 `SafeHandle` 机制；双轨保持一致。
- [ ] B-ID71 SJLJ（setjmp/longjmp）异常机制与 ARC 栈展开资源泄漏/锁死锁：`irgen_stmt.c` 与 `irgen_builtins.c` 使用 setjmp/longjmp 实现异常跳转，跨函数 throw 时直接恢复栈寄存器，绕过被穿越中间栈帧的析构逻辑，导致中间局部 ARC 对象漏释放、using 资源未关闭、lock 语句（zan_monitor_enter）无法执行 exit 从而引发全局永久死锁。需逐步演进为零开销栈展开（Zero-cost LandingPad Unwinding）；双轨保持一致。
- [ ] B-ID72 宽字符与文本封送跨平台断裂（Wide.zan 强绑 kernel32）：`stdlib/System/Interop/Wide.zan` 无条件 `[DllImport("kernel32")]` 导入 Win32 API 且写死 2 字节 UTF-16 假定，在 Linux/macOS 上直接因找不到 kernel32 崩毁，且违背 POSIX 4 字节 wchar_t 规范。需抽象为支持 POSIX iconv / mbstowcs 与 Windows 多分发的跨平台文本封送器；双轨同步。
- [ ] B-ID73 DateTime.Now 本地墙钟时间语义纠正与跨平台时区（IANA）规范化：`stdlib/System/DateTime.zan` 中 `DateTime.Now` 硬编码返回 `UtcNow()` 严重背离行业标准心智，导致业务时间偏差；且本地时区偏移依赖平台特化 CRT 函数，缺乏 POSIX 下 `/usr/share/zoneinfo` IANA 时区统一支持。需纠正 Now/UtcNow 语义并建立跨平台 TimeZoneInfo；双轨同步。
- [ ] B-ID74 编译器静态魔数深度上限弹性化（ZAN_MAX_FINALLY_DEPTH 等）：`src/compiler/irgen_stmt.c` 对 try-finally 与 lock 嵌套深度施加固定静态数组上限（超出直接报 3080 错误），且多处存在写死缓冲区大小假定。需将静态栈帧与嵌套跟踪重构为动态伸缩结构，消除硬编码限制；双轨同步。
- [ ] B-ID75 全仓平台抽象层（PAL, Platform Abstraction Layer）统一标准落地：终结全仓各模块随意穿插 `#if WINDOWS`、写死 `kernel32`/`user32` 与平铺单平台实现的混乱局面。确立“抽象契约（Interface/Abstractions）+ 平台物理目录隔离（Platform/Windows/Linux/Mac）+ 统一工厂/构建系统装配（Factory/CMake）”的顶层架构纲领，覆盖四大核心领域：(1) 文件与路径系统（路径分隔/文件锁/目录监听）、(2) 进程/管道与环境信号、(3) 网络事件循环（IOCP/epoll/kqueue）、(4) 动态库加载与错误码映射（dlopen/LoadLibrary/errno）；双编译器双轨保持一致。
- [ ] B-ID76 整数算术溢出未定义行为（UB）防护与 checked/unchecked 模式：`src/compiler/irgen_expr_ops.c` 算术运算直接发射裸 LLVM `add/sub/mul` 指令，有符号溢出在 LLVM IR 属未定义行为，优化器会激进消除边界检查引发越界漏洞。需引入溢出陷阱检测（llvm.sadd.with.overflow 等）并支持编译期 checked/unchecked 算术控制；双轨同步。
- [ ] B-ID77 静态空安全（Null-Safety）契约与可空引用类型体系：当前引用类型全部默认为裸指针，全仓依赖运行期判空与空指针异常崩溃。需在类型检查器引入现代空安全语义（如 `string?` 可空与 `string` 非空分离），在编译期截胡潜在空引用；双轨保持一致。
- [ ] B-ID78 ARC 深层对象树级联析构栈溢出防护（迭代式延迟释放队列）：`irgen_arc.c` 中对象析构函数采用递归调用下钻释放子字段，遇到上千层深度的链表、树或 AST 根节点被 release 时，同步递归在析构阶段直接耗尽线程栈引发 Stack Overflow 崩溃。需引入工业级迭代式延迟释放队列（Deferred Drop Queue）；双轨同步。
- [ ] B-ID79 结构化并发（Structured Concurrency）与统一 CancellationToken 取消机制：异步任务（Task/async）缺乏父子生命周期联动与取消令牌链，父协程超时或异常退出时子协程沦为失控孤儿协程。需在标准库与运行时建立贯穿异步调用链的 CancellationToken 取消信号与任务树监控；双轨同步。
- [ ] B-ID80 核心集合抗哈希碰撞拒绝服务攻击（Hash-DoS）免疫：`Dictionary<K, V>` 与 `HashSet<T>` 目前采用无盐简单哈希算法，在 Web 服务端接收恶意构造的碰撞 JSON 键时哈希桶退化为 O(N) 链表被打满 CPU。需引入带随机盐的 SipHash-2-4 / AHash 算法保证抗碰撞安全性；双轨同步。
- [ ] B-ID81 只读与不可变集合契约健全（IReadOnlyList / ReadOnlySpan 等）：标准库集合缺乏只读抽象约束，实体向外暴露内部集合时可被随意篡改或并发破坏。需补全 `IReadOnlyCollection<T>`、`IReadOnlyList<T>`、`IReadOnlyDictionary<K, V>` 接口及切片视图；双轨同步。
- [ ] B-ID82 包管理语义化版本仲裁与钻石依赖求解器（Diamond Dependency Solver）：当前 `.zan-packages/` 仅支持简单扁平安装，当两个依赖库分别引入同一包的不同大版本时缺乏冲突求解能力。需引入类似 PubGrub 的语义化版本依赖解析算法与包隔离机制；双轨保持一致。
- [ ] B-ID83 模块/包级私有可见性修饰符（internal 访问控制）：语言目前仅有 public/private/protected，缺乏包级内部访问控制，导致驱动与基础设施细节类被迫暴露为 public 被外部深度耦合。需在编译器与符号表中引入 `internal` 模块可见性边界；双轨同步。
- [ ] B-ID84 高精度十进制（Decimal）除零静默返回 0 与逐位移位性能缺陷：`src/runtime/rt_decimal.c` 中 `u128_divrem` 在除数为 0 时静默返回 0（`100 / 0 = 0`），严重破坏金融与计费业务安全底线，应严格抛出除零异常；且 128 位除法采用 128 次循环逐位移位法，单次除法耗费上千 CPU 周期，需演进为 Knuth 算法 D 多精度快速除法；双轨同步。
- [ ] B-ID85 密码学实现缓存时序侧信道攻击风险（SM4 静态 S-Box 查表）：`src/runtime/rt_hw_accel_sm4_tables.h` 采用全局静态 S-Box 表直查实现国密 SM4。非恒定时间查表在多租户/云原生环境下易遭受 Cache-Timing 侧信道攻击还原出加密密钥。需改用比特切片（Bit-sliced）恒定时间位运算或调用硬件加速指令（ARMv8 Crypto / SM4 特性）；双轨保持一致。
- [ ] B-ID86 协程纯协作式调度引发的线程饥饿（Starvation）与废弃 ucontext 依赖：`src/runtime/rt_sched.c` 缺乏基于时间片或信号的抢占机制（Preemption）。CPU 密集型长循环会永久霸占调度线程，导致挂载在该线程上的所有异步 I/O、Web 请求与定时器永久假死；且 POSIX 下依赖已在 POSIX.1-2008 废弃的 `ucontext.h`（换栈拷贝信号掩码带来巨大开销），需演进为轻量寄存器汇编保存与协作抢占插桩；双轨保持一致。
- [ ] B-ID87 Unicode 规范化（NFC/NFD）与国际化大小写折叠（Case Folding）缺失：标准库缺乏 Unicode 规范化算法（UAX #15）支持，`string.ToLower/ToUpper/Equals` 仅能处理基本 ASCII 或简单码点映射。欧洲带重音符、德语等在不同输入源下单码点与组合码点无法等价匹配，存在严峻的国际化乱码与鉴权仿冒越权风险；双轨同步。
- [ ] B-ID88 崩溃信号处理器中非异步信号安全函数（Async-Signal-Safety）死锁隐患：`src/runtime/rt_crash.h` 在崩溃信号处理过程中直接调用 `fprintf`、`CreateFileA`、`ReadFile` 等带内部锁的 I/O 操作。在进程持锁状态崩溃时极易触发信号处理器内部死锁，导致崩溃栈日志丢失、进程假死。需重构为仅调用直接系统调用（`write`/裸文件描述符）的纯 Async-Signal-Safe 报告器；双轨同步。
- [ ] B-ID89 生产级内存可观测性与循环引用泄漏分析工具缺失：Zan 采用无 GC 的纯 ARC 模型，循环引用必定造成长驻内存泄漏。目前全仓缺乏运行时“孤儿环探测器（Cycle Detector）”与生产环境 Heap Snapshot / Dump 导出机制，在长期运行的服务端与复杂桌面 GUI 中一旦发生内存缓慢增长，运维与开发缺乏定位手段；双轨保持一致。
- [ ] B-ID90 包管理器校验和仅计算 Manifest 导致源码防篡改失效（供应链安全虚设）：`src/compiler/package.c:679` 中 `zan_pkg_compute_checksum` 计算的仅是 `manifest_path`（`package.json`）的哈希，而非源码压缩包或源码树哈希。攻击者篡改源码包内部 `.zan` 文件仍能通过校验。需升级为计算并锁定完整 Tarball/源码树的 SHA-256 哈希锁；双轨保持一致。
- [ ] B-ID91 解析器语法容错与错误节点占位机制伪造（Resilient Parsing & Poison Node 缺失）：`src/compiler/parser.c:41` `parser_error_node` 在语法解析失败时粗暴返回 `AST_INT_LITERAL`（常数 0 节点），导致后续类型检查器误将其作为 int 校验并喷出大量风马牛不相及的级联误报（如 cannot access member on int），破坏 IDE/LSP 键入补全。需建立正规 `AST_ERROR_EXPR` 毒化节点与错误静默抑制体系；双轨同步。
- [ ] B-ID92 静态构造函数（cctor）声明序盲目初始化与依赖拓扑排序缺失：`src/compiler/irgen_emit.c:206` 在 Main 函数开头仅按 AST 声明的原始数组顺序同步调用各个类型的 `_cctor`，缺乏依赖图拓扑排序与按需/首次访问惰性初始化（Lazy BeforeFieldInit）机制，跨文件静态字段互相依赖时因编译顺序漂移引发静默空引用或读取未初始化零值；双轨同步。
- [ ] B-ID93 异步调试信息（Debug Info）状态机展开与原始源码行映射断裂：`src/compiler/di.c` 目前在生成 DWARF/PDB 调试符号时，尚未对 `async/await` 重写后的闭包状态机帧建立原始行号映射（Source-Mapping），在调试器（zan-dap / lldb）中单步步入（Step-Over / Step-Into）异步函数时 PC 指针直接跳入底层合成的 `$resume` 状态机跳转表与帧槽存取汇编，开发者无法直观断点调试业务源码行；双轨保持一致。



