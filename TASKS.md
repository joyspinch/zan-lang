# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

- [ ] B-ID57 server_mvc generator_sources 红：生成产物断言 "api json projection" 失败（Zan.Mvc 源生成器 Gen.zan 的输出内容，非 routegen）。已修闭：admin_gate 金样 193 为旧"属性壳"架构（387 动作/29 控制器）的内容快照，b1752059e 重构改直连动作（150 动作）后真值 173 且行为断言（gate/trio/preAnonymous/menu/txprobe）全绿——金样已按内容漂移更新；ai_endpoint/timezone 双案因 CMakeLists 残留 ZanWeb 旧路径瞬时红亦已修。
- [ ] B-ID58 CodeFirst 主键链断裂（f896826d3 TypeInfo 切换回归）：`[Column(IsPrimary = true)]` 在反射面零消费（irgen_reflect.c 位表只认 [Key]/[PrimaryKey]/IsIdentity），SyncAsync 建表丢 PRIMARY KEY，而 Insert.OnConflict 以 PkCols 生成 `ON CONFLICT(主键列)` → SQLite "ON CONFLICT clause does not match any PRIMARY KEY"；且 2e3932369 把 OnConflict 复合键 lambda 降为单列 string（inner.OC 可累加可链式补救）。正确修复需把 IsIdentity/IsPrimary 拆成两个反射位（编译器 ABI + selfhost 镜像 + Meta.BuildFor 接线 + PkList 口径），selfhost 镜像为并发会话在飞文件，落地需协调。已修部分：SyncStructureAsync 包装 async void→async bool（await 挂 async void 是火后不理，MetricsStore.Start 曾在建表完成前还池）；MetricsStore 直调 SyncAsync 绕开泛型扩展特化失真（泛型扩展体内 Meta.For<T> 静默 false，最小复现：扩展方法+泛型 T+await 内层）。metrics_flush_retry 仍红（建表已通、卡 ON CONFLICT）。
- [ ] B-ID59 ORM 查询链泛型推断断裂（2e3932369 DbExtensions 重写 +805 行的爆炸半径，HEAD 既有，非 11-01/11-02 批次引入）：E2108 泛型不变性——`ToList<RouteStat>(a => new RouteStat {...})` / `ToList(a => a.path)` 等链式投影报 cannot convert List<MetricMinute> to List<string>；case_orm_group_aggregate 双案、case_linq_and_zandb_opt、case_orm_metadata_concurrency、case_chart_data_range 等约 20+ 案红（server_mvc/zandb/orm 族 -j2 扫 129 中 24 红）。已修子项（本批）：diag_orm_where_int_string/string_int/set_string_int 三例——checker 对开放泛型 lambda 体不判成员类型（E2045 被静默跳过）、特化后 irgen 发 i32 vs ptr 的裸 icmp 被 LLVM verify 整模块击毙（E3090 报 0:0）；irgen 二元入口加比较类 int/ptr 混比守卫（E2045 当场诊断 + 返回常量假），三例期望串从 dbgen 时代的 "type mismatch" 改为现行诊断。
- [ ] B-ID60 反射元数据特性体系重构（解耦硬编码字符串）：`irgen_reflect.c` 与 selfhost 镜像目前以 `memcmp` 硬编码匹配有限特性名（Key/Required/NotMapped），命名参数与参数化属性（如 `[Column(IsPrimary = true)]`）无法进入元数据位表。需重构为结构化特性元数据流（Metadata Blob），支持反射面读取完整参数并保留 C11/selfhost 1:1 对齐。
- [ ] B-ID61 MVC 路由生成与动词推断正规化：`routegen.c` 与 `GenRoute.zan` 目前依据动作名前缀/子串（post/save/del/update/upload 等）启发式猜测 HTTP 动词，存在命名意外风险；未声明鉴权端点硬编码 `.Login()`。需重构为显式契约优先与标准化源生成体系，双编译器保持相同分发生成口径。
- [ ] B-ID62 语言内置原语与方法横截重构：`irgen_call_builtins.c` 现存 3200+ 行根据方法名字符串横截调用（nameof/GetValueOrDefault/ToString 等），易与用户自定义符号或重载产生意外冲突。需演进为标准库 `[Intrinsic]` 符号级绑定分发机制，checker 期绑定符号而非 irgen 字符串匹配，C11 与 selfhost 差分保持一致。
- [ ] B-ID63 泛型系统协变/逆变与调用环境特化：补全接口/委托方差标记（out/in 支持）解决 E2108 泛型不变性导致的 LINQ/ORM 链式投影类型转换断裂；在异步状态机和闭包展开帧中隐式压入泛型环境上下文（Type Context），根除嵌套或扩展方法中 `Meta.For<T>` 特化丢失问题；双轨同步。
- [ ] B-ID64 异步状态机帧布局类型化与 ARC 生命周期闭环：`irgen_async.c` 与 `src/selfhost/irgen_async.zan` 目前将异步结果强制打包规整为 64 位整数槽（res.slot），大结构体与聚合槽逻辑复杂；需演进为类型化返回值字段（Typed Result Field），并在状态机终结时自动析构捕获帧，防止长驻异步任务产生 ARC 循环引用与连接泄露。
- [ ] B-ID65 ORM 强类型表达式树与复合主键支持：建立标准只读表达式树（Expression Tree）AST 节点体系及 SQL 访问者（Visitor）翻译管线，支持从实体元数据完整提取多列复合主键用于 ON CONFLICT 解析，消除手工拼接字符串 SQL 的脆弱性与多列约束失真；双轨保持一致。
- [ ] B-ID66 GUI 跨平台平台后端抽象统一（抹平 Win32 与 POSIX 差异）：`packages/Zan.Gui` 目前对 Windows 强依赖 3000 行 `Win32Shell.zan` 裸露 Win32 API，而 Linux (X11/Wayland) 与 macOS (Cocoa) 仅有残缺 C 桥接桩，导致异形窗口、托盘、拖拽、IME 与多窗口在非 Windows 平台功能断裂。需抽象统一的 `IPlatformWindow` / `IPlatformShell` 驱动接口；双编译器双轨保持一致。
- [ ] B-ID67 HTML+CSS 窗口排版与形状协议标准化（解耦魔术字符串与伪排版）：`FormBuilder.zan` 与 `App.Window.zan` 现存 `"1,0,0,w,h,r"` 逗号分隔字符串传递异形窗口、硬编码 40+ 键名匹配、WinForms 式 Dock 模拟 CSS 的缺陷，在 Linux/macOS 上外观无法一致呈现。需重构为强类型 `WindowShapeDescription` 协议并演进为标准化 Flexbox 布局规范；双轨同步。
- [ ] B-ID68 跨平台路径规范化与大小写敏感校验（Linux/macOS 兼容）：编译器与标准库 IO 中存在反斜杠路径拼接假定，且 Windows 大小写不敏感掩盖了源码与包引用（如 `using System.Web` 路径）大小写漂移。需在编译器前端与标准库加入统一的 POSIX/Windows 路径正规化（Path Normalization）与大小写保真校验；双轨同步。
- [ ] B-ID69 网络与异步 IO 多路复用跨平台统一（IOCP / epoll / kqueue 抽象）：`System.Net` 目前缺乏跨平台高性能事件循环（Event Loop），在 Windows IOCP 之外的 POSIX 平台缺乏高效统一的 epoll/kqueue 反应堆封装，存在固定 Buffer 分配与假死隐患。需建立统一的非阻塞网络驱动层；双轨同步。
- [ ] B-ID70 跨平台动态库导入（DllImport）与非托管内存 SafeHandle 闭环：`DllImport` 平台库名硬编码（如只写 `user32` 或未加扩展名平台探查），导致非 Windows 平台动态解析失败；且平台互操作中广泛存在 `Wide.Of` / `Wide.Free` 裸指针手动释放缺陷。需引入跨平台动态库映射表与具作用域自动析构的 `SafeHandle` 机制；双轨保持一致。
- [ ] B-ID71 SJLJ（setjmp/longjmp）异常机制与 ARC 栈展开资源泄漏/锁死锁：`irgen_stmt.c` 与 `irgen_builtins.c` 使用 setjmp/longjmp 实现异常跳转，跨函数 throw 时直接恢复栈寄存器，绕过被穿越中间栈帧的析构逻辑，导致中间局部 ARC 对象漏释放、using 资源未关闭、lock 语句（zan_monitor_enter）无法执行 exit 从而引发全局永久死锁。需逐步演进为零开销栈展开（Zero-cost LandingPad Unwinding）；双轨保持一致。
- [ ] B-ID72 宽字符与文本封送跨平台断裂（Wide.zan 强绑 kernel32）：`stdlib/System/Interop/Wide.zan` 无条件 `[DllImport("kernel32")]` 导入 Win32 API 且写死 2 字节 UTF-16 假定，在 Linux/macOS 上直接因找不到 kernel32 崩毁，且违背 POSIX 4 字节 wchar_t 规范。需抽象为支持 POSIX iconv / mbstowcs 与 Windows 多分发的跨平台文本封送器；双轨同步。
- [ ] B-ID73 DateTime.Now 本地墙钟时间语义纠正与跨平台时区（IANA）规范化：`stdlib/System/DateTime.zan` 中 `DateTime.Now` 硬编码返回 `UtcNow()` 严重背离行业标准心智，导致业务时间偏差；且本地时区偏移依赖平台特化 CRT 函数，缺乏 POSIX 下 `/usr/share/zoneinfo` IANA 时区统一支持。需纠正 Now/UtcNow 语义并建立跨平台 TimeZoneInfo；双轨同步。
- [ ] B-ID74 编译器静态魔数深度上限弹性化（ZAN_MAX_FINALLY_DEPTH 等）：`src/compiler/irgen_stmt.c` 对 try-finally 与 lock 嵌套深度施加固定静态数组上限（超出直接报 3080 错误），且多处存在写死缓冲区大小假定。需将静态栈帧与嵌套跟踪重构为动态伸缩结构，消除硬编码限制；双轨同步。







