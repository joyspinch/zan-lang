# Zan 生态扩展路线图

> 目标：Zan 的底座（41 个包、162 个 stdlib 文件、13 套游戏模板、96 个 GUI 组件、
> 8 类数据库驱动、16 类网络协议）已经能撑起中等规模项目，短板在**通用库层**
> 与**游戏纵深**。本表给出要补的扩展库、缺口依据与落地顺序。
>
> 数据来源：C# Top 500 实测名单（GitHub API `language:C# stars:>1500`，
> `total_count = 942`，即按星排序的 Top ~1000）；能力盘点为本仓库实测
> （grep 全仓 + 目录清点）。
>
> ★ = Top 1000 内实测星标；`榜外` = 不在 Top 1000 但该能力不可替代。

## 0. 两条铁律

1. **在现有架构上扩展，不新建平行包。** 已有能力一律扩包：
   日志进 `System.Diagnostics`（Zan.Diagnostics），不另起 `Zan.Logging`；
   嵌入式文档库用 ZanDb，不引入 LiteDB 式第二套。
2. **按 `docs/STDLIB_COMPONENT_STANDARDS.md` 写**：门面/能力/原语三层、
   零配置可用、链式可选配置、错误可区分、并发安全注明、
   新组件必须带 conformance 用例。

---

## 1. 已落地

| 项 | 落点 | 状态 |
|---|---|---|
| 结构化日志（事件模型 + 落地端契约 + 三个落地端 + 分类日志器 + 注册表） | `packages/Zan.Diagnostics/src/System/Diagnostics/` | ✅ 已实现，conformance `log_structured` |

要点：历史 `Log.*` API 语义不变（逐字节兼容，见 `log_hardening`/`log_rolling_file`
两个既有用例仍绿）；新增 `LogEvent`（值对象）、`LogSink`（落地端基类）、`Logger`、
`LogHub`（注册表）、`ConsoleLogSink`/`FileLogSink`/`MemoryLogSink`；JSON 渲染改走
`JsonValue`（旧实现手拼转义表，会漏控制字符，产出非法 JSON）。

**注册表是实例级而非静态**——这不是风格偏好，是被编译器逼出来的（见 §7）。

### 1.2 数值层：`Zan.Math` 并入 `System.Numerics`

**不新建平行包**：`System.Numerics` 已有 `Vector2/3/4`、`Matrix4x4`、`BitOperations`，
数学层天然属于它。落在 `stdlib/System/Numerics/`：

| 文件 | 类型 | 内容 |
|---|---|---|
| `MathUtil.zan` | `double` 标量 | Clamp / Lerp / InverseLerp / Remap / SmoothStep / SmootherStep / Sign / Approximately / Wrap / PingPong / MoveTowards / DeltaAngle / LerpAngle / 角度换算 / Hermite / Step / RoundTo，以及 **Log / Exp**（编译器内建 `Math` 没有这两项，此处纯实现） |
| `Quaternion.zan` | `float`（与 Vector/Matrix 同型） | FromAxisAngle / FromEuler(Degrees/Radians) / ToEuler(Degrees/Radians) / Multiply / Rotate / Slerp / Lerp / Angle / Normalize / Inverse / ToMatrix / LookRotation |
| `Noise.zan` | `double` | Value2D/3D、Perlin2D/3D、Fbm2D、Ridged2D；同种子必然同序列 |
| `RandomExt.zan` | `double` | xorshift64\* 确定性 PRNG、NextDouble/Range/IntRange、Normal / Exponential / Bernoulli、`Pick<T>` / `WeightedPick<T>` / `Shuffle<T>` |
| `Spline.zan` | `float`（控制点 `List<Vector3>`） | Catmull-Rom / Bezier / Hermite 求值、近似弧长、等弧长重采样 |

**类型分工**（有实测依据，不是随手选的）：几何面用 `float`——与既有
`Vector2/3/4`、`Matrix4x4` 同型，可直接互转；标量与采样面用 `double`——本仓库
游戏层数学以 `double` 为主导（`packages/Zan.Game` 中 double 出现 986 次 vs float 24 次），
而内建 `Math` 的成员一律返回 `double`。

**约定**：`Quaternion` 的乘法 `q1*q2` 表示「先施加 q2 再施加 q1」，欧拉角为 YXZ 序，
角度参数一律显式带 `Degrees` / `Radians` 后缀以消歧义；`Quaternion.ToMatrix()` 与
`Matrix4x4` 的存储/行向量约定一致，故 `Matrix4x4.Transform(v, q.ToMatrix())`
与 `Quaternion.Rotate(v, q)` 结果相同（conformance 已断言）。

**未覆盖**：定点数 / 大数（decimal 已在 `System.Decimal`，128 位定点）、
复数、凸包/几何算法（`Zan.Game` 的 `Arcade2D`/`Collision2D` 已覆盖 2D 几何检测）。

### 1.3 校验层：`Zan.Validation`

新包 `packages/Zan.Validation`，命名空间 `System.Validation`（不进 stdlib——
校验是可选能力，不是语言底座；`Zan.Web` 的 `Validator` 保持原样不动）。

| 文件 | 类型 | 内容 |
|---|---|---|
| `ValidationPlan.zan` | `ValidationPlan<T>` | 引擎：规则链 + 错误聚合；文本/数值/布尔/任意/嵌套五类规则 |
| `ValidationResult.zan` | `ValidationResult` | 失败项列表、字典视图、文本视图、JSON 视图（走 `JsonValue`） |
| `ValidationFailure.zan` | `ValidationFailure` | property / message / code / attemptedValue |
| `ValidationContext.zan` | `ValidationContext` | 首错即停、属性名前缀 |
| `ValidationException.zan` | `ValidationException` | 校验失败即抛，携带原始结果 |
| `ValidationText.zan` | `ValidationText` | 空/空白判定 + 通配符匹配（`*` / `?`，不引正则） |
| `Validation.zan` | `Validation` | 门面：Ok / Ensure / Validate |

**绑定方式**：规则在**声明时**用取值器把"取哪个属性"固化（`s => s.userName`），
运行期零反射——属性改名在编译期就报错。这比路线图里设想的"`Gen*` 生成绑定"
更直接：`Gen*` 是给"从字段名反查类型"用的，而校验需要的只是"从属性到值"的
一个 lambda，编译器本来就检查它的类型。

**形态被编译器逼了一把**：原设计是 C# 风格的 `Rule<V>(...).NotEmpty().MaxLength()`
（方法级类型参数 V）。当前编译器不支持"泛型方法内 `new 泛型类<类参数, 方法参数>`"
（变体不发射，规则静默失效，见 `TASKS.md` A418），也不支持依赖型泛型基类
（`class V<T> : Plan<T>`，字段偏移错位，见 A419）。因此规则形态收敛为
**类级类型参数 + 取值器闭包**：`ValidationPlan<T>` 既是引擎也是基类，用户
直接继承它（不再多包一层 `AbstractValidator<T>`）。

conformance：`validation_rules`（规则触发、错误聚合顺序、首错即停、属性名前缀、
嵌套前缀、异常、三种错误视图、通配匹配）。

### 1.4 终端输出层：`Zan.Cli`

新包 `packages/Zan.Cli`，命名空间 `System.Cli`（不进 stdlib——终端输出是
应用层能力，语言底座不该带 ANSI 依赖）。

| 文件 | 类型 | 内容 |
|---|---|---|
| `Ansi.zan` | `Ansi` | SGR 原语、16/256/真彩色、颜色开关策略（`ZAN_CLI_COLOR` / `NO_COLOR` / `TERM=dumb`） |
| `TextWidth.zan` | `TextWidth` | 显示宽度：宽字符（2 列）/ 零宽（0 列）判定、补齐、截断 |
| `Style.zan` | `Style` | 作用域样式：套完立即复位（与 `Console.ForegroundColor` 的粘性语义相反） |
| `Table.zan` | `Table` / `TableColumn` | 表格：Unicode / ASCII / 无边框，列宽按显示宽度自适应或钉死 |
| `Progress.zan` | `ProgressBar` / `Progress` | 进度条：纯渲染 + 写同一行 |
| `Prompt.zan` | `Prompt` | 文本 / 确认 / 单选提示，读取器与输出 sink 均可注入 |
| `Cli.zan` | `Cli` | 门面：Success / Error / Warning / Info / Header / Rule / Table |

**渲染是纯函数**——所有输出先渲染成字符串，由调用方决定写到哪里。这让
conformance 能逐字节比对（含表格边框、进度条字符、提示文本），也让"同一段
代码在控制台与文件里产出同样的字节"成为可依赖的性质。

**宽字符对齐**是这层的主要价值：`string.Length` 是字节数，中文是双宽字符、
组合记号是零宽字符，表格按字节数对齐会歪。`TextWidth` 按 East Asian
Width 的 Wide/Fullwidth 与零宽规则算显示宽度，表格、进度条、截断统一走它。

**已知缺口**：运行时没有 `isatty` 探测（全仓无 TTY 判定），所以颜色策略
判不出"输出被重定向到文件"。默认策略是"环境变量优先，其余开启"；需要严格
TTY 语义的应用显式 `Cli.EnableColor(false)`。

conformance：`cli_output`（转义序列、作用域样式、显示宽度、补齐截断、三种
表格边框、CJK 表格等宽、进度条、提示解析与提示文本）。

### 1.5 游戏 ECS：`Zan.Game.Ecs`

新包 `packages/Zan.Game.Ecs`，命名空间 `Game.Ecs`（游戏包布局：
`packages/Zan.Game.<Module>/src/Game/<Module>/`）。`Game/Core/Entity.zan` 的
OOP 实体基类**不动**——它是"一个对象一个实体"的写法，与 ECS 是两种范式，
并存即可，不必强拆。

| 文件 | 类型 | 内容 |
|---|---|---|
| `ComponentStore.zan` | `ComponentStore<T>` | 组件池：实体 id ↔ 槽位 ↔ 值，删除保序 |
| `World.zan` | `World` | 实体生命周期、延迟销毁、清理回调、系统调度 |
| `System.zan` | `System` | 系统基类（抽象 `Update`） |
| `EcsGroup.zan` | `EcsGroup` | `PreUpdate` / `Update` / `PostUpdate` |
| `Query.zan` | `Query1<T>` / `Query2<T,U>` | 类型化查询与遍历回调 |

**三条硬取舍**：

1. **零反射**：组件池是泛型类，元素类型编译期定死；没有"按类型名查组件"的
   运行时字典（要那种得靠 `Gen*` 生成代码）。
2. **稳定序**：删除用顺序搬移而非 swap-remove，槽位顺序不变——同种子同操作
   序列必然同结果（本仓库有 lockstep 与回放，确定性是硬需求）。
3. **延迟销毁**：`DestroyEntity` 只打标记，帧末统一清理。系统遍历组件途中
   销毁实体是常态，立即删会改坏正在遍历的槽位。

**落地时踩出的编译器缺陷（已修）**：泛型类的 `this.field` 不做类型实参替换，
字段声明里的 `List<T>` 停在类型参数上 → 元素类型判成擦除指针 → 结构体元素
写进去的是临时对象地址，读回即野指针（0xC0000005）。指针大小的元素恰好蒙对，
所以此前没人发现。修在 `irgen_expr_core.c` 的 `member_access_field_type`，
回归用例 `generic_dependent_list`。

**已知边界**：泛型类**不能持有值类型字段**（字段布局不随实例特化），组件池
因此把"缺失实体的零值"放进单元素 `List<T>` 盒子。编译器侧修法见
`TASKS.md` A421。

conformance：`ecs_world`（组件读写、延迟销毁与清理、分组顺序、禁用系统、
查询命中、值/引用组件语义、确定性重放）。

---

## 2. 通用生态包（按依赖顺序）

### 第 0 层 · 底座（其它包都依赖它）

| 建议包 | C# 参考 | ★ | 补什么（实测缺口） | 工期 |
|---|---|---|---|---|
| `Zan.Math` | MathNet.Numerics | 3,772 | ✅ 已并入 `stdlib/System/Numerics`（`MathUtil`/`Quaternion`/`Noise`/`RandomExt`/`Spline`，不新建平行包，见 §1.2） | — |
| `Zan.Compression` | SharpZipLib / LZ4 / Zstd | 3,933 | 压缩只在 AppUpdate 零散使用；无通用 deflate/lz4/zstd | 1–2 周 |
| `Zan.Validation` | FluentValidation | 9,765 | ✅ 已落地 `packages/Zan.Validation`（`System.Validation`，规则链 + 错误聚合，编译期绑定取值器、运行期零反射，见 §1.3） | — |
| `Zan.Logging` | Serilog / NLog | 8,051 / 6,550 | ✅ 已在 `System.Diagnostics` 内落地（见 §1） | — |

### 第 1 层 · 中间件

| 建议包 | C# 参考 | ★ | 补什么 | 工期 |
|---|---|---|---|---|
| `Zan.Mapping` | AutoMapper / Mapster | 10,188 / 5,182 | 全仓 0 命中。做「显式映射函数 + `Gen*` 生成」，**不做反射式自动映射** | 2 周 |
| `Zan.Messaging` | MassTransit | 7,809 | 进程内发布/订阅、请求-响应、管道中间件 | 1–2 周 |
| `Zan.Cli` | Spectre.Console / Terminal.Gui | 11,666 / 11,279 | ✅ 已落地 `packages/Zan.Cli`（`System.Cli`：ANSI 样式 / 宽字符对齐表格 / 进度条 / 交互提示，见 §1.4） | — |
| `Zan.Jobs` | Hangfire / Quartz.NET | 10,151 / 7,096 | 只有 `Zan.Hosting/Cron.zan` 定时触发；缺持久化任务队列、重试退避、任务存储 | 2–3 周 |

### 第 2 层 · 领域

| 建议包 | C# 参考 | ★ | 补什么 |
|---|---|---|---|
| `Zan.Pdf` | QuestPDF | 14,205 | **当前最大空白**：全仓唯一 "pdf" 命中是 HttpClient 的 MIME 映射；无排版/字体/表格引擎 |
| `Zan.Ssh` | SSH.NET | 4,385 | SSH 客户端、SFTP、密钥管理 |
| `Zan.Barcode` | ZXing.Net | 榜外 | 条码/二维码编解码（补全 Gui 侧扫码） |
| `Zan.Test.Fixtures` | AutoFixture / Bogus | 3,539 / 9,747 | 假数据生成、对象图构造、断言助手 |
| `Zan.Test.Mocks` | Moq / NSubstitute | 6,410 | 接口替身与行为校验（无反射下的替代方案） |
| `Zan.Bench` | BenchmarkDotNet | 11,512 | 基准测试（ARC 无 GC，分配量统计比 .NET 更该做） |

> LiteDB（9,486★）**不做**：嵌入式文档库用现有 ZanDb 覆盖，
> 不引入第二套持久化语义。

---

## 3. 游戏向增强

| 建议包 | C# 参考 | ★ | 现状 → 缺口 | 工期 |
|---|---|---|---|---|
| `Zan.Game.Ecs` | DefaultEcs / Unity ml-agents | 榜外 / 19,771 | ✅ 已落地 `packages/Zan.Game.Ecs`（`Game.Ecs`：类型化组件池 + 延迟销毁 + 分组系统调度 + 稳定序查询，见 §1.5） | — |
| `Zan.Game.Physics2D` | nphysics / JigglePhysics | 榜外 | **几何检测已有**（`Arcade2D/Collision2D` + `AdvancedCollision2D`：射线、AABB、分离轴粗筛）→ 缺**动力学层**：积分器、约束/关节、旋转体（OBB/多边形 SAT）、连续碰撞、穿透修正 | 3–4 周 |
| `Zan.Game.Physics3D` | BulletSharp | 榜外 | 无 | 6–8 周 |
| `Zan.Game.Pathfinding` | AStarPathFinding | 榜外 | 只有棋盘 `GridPathfinder` + 塔防 `FlowField` → A*/JPS、导航网格、路径平滑 | 2–3 周 |
| `Zan.Game.Assets` | SharpGLTF / AssimpNet | 榜外 | 有 `AssetPipeline` + `PackedAssets` → glTF 导入、骨骼/动画切片、图集、字体与 shader | 3–5 周 |
| `Zan.Game.Audio` | NAudio | 6,249 | 有 `AudioBus`/`SpatialAudio2D`/`Desktop/Audio`，无 mp3/ogg 解码与流式播放 | 2–3 周 |
| `Zan.Game.Netcode` | Mirror / LiteNetLib | 6,366 / 3,632 | 已有 lockstep + 回放 → 快照同步、rollback 预测、延迟补偿 | 4–6 周 |
| `Zan.Game.Particles` | MonoGame.Extended | 榜外 | 13 套模板各自实现粒子（idle 模板自带 `ParticleSystem.zan`）→ 统一发射器、曲线/噪声驱动、GPU 粒子 | 2–3 周 |
| `Zan.Game.Animation` | Unity Animation Rigging | 榜外 | 有 clip 播放器/纸娃娃/3D 轨 → 蒙皮、状态机混合、IK | 4–6 周 |
| `Zan.Game.Render3D` | Silk.NET / OpenTK | 5,242 / 3,543 | `Mesh3D` 很薄；**GPU 后端只接 WGL/GLX，Android/OHOS 是 stub**（`SetRenderBackend(1)` 返回 0 是预期行为）→ 相机/材质/光照/shader/后处理 + 补齐跨平台 GPU 后端 | 8–12 周 |
| `Zan.Game.Procgen` | WaveFunctionCollapse / MarkovJunior | 25,416 / 8,443 | 无 → WFC 算法可直接复刻（纯 C#、数学自洽）、关卡拼接 | 2 周 |
| `Zan.Game.Debug` | ImGui.NET | 榜外 | 无 → 帧剖析覆盖层、实体检查器、内存精灵缓存可视化 | 2–3 周 |
| `Zan.Game.Common` | — | — | 13 套模板重复代码（每套自带 Particle/Profile/Popup/存档）→ 公共基座：存档、飘字、血条、背包格、冷却条、小地图 | 2–3 周 |
| `Zan.Game.Profiler` | LiteMonitor / LibreHardwareMonitor | 6,682 / 9,236 | prof 走廊只打 stdout → 帧预算分析器、分配热点、硬件指标面板 | 2 周 |

---

## 4. 放置与命名

- 通用包：`packages/Zan.<Domain>/src/System/<Domain>/*.zan`（对齐现有 41 包结构）
- 游戏包：`packages/Zan.Game.<Module>/src/Game/<Module>/*.zan`（对齐 `packages/Zan.Game/src/Game/<X>/` 布局）
- 引擎级能力（物理/渲染/ECS/资源）走独立包并单独版本化，不进 stdlib；
  stdlib 只留跨包共用的最小基元

## 5. 游戏包的强制契约（`.agents/skills/game-dev`）

1. **两套缩放路径不得混排**：Gui 控件走框架自动缩放；Canvas 自绘必须过单一 `Hud.Dp(v)` helper。
2. **必接皮肤**：新 HUD 控件支持 `skins/<名>/skin.css`，默认桌面样式只算开发占位。
3. **合成契约**：present 后画布不保留，命中区每帧重建；跳过重绘不得跳 present。
4. **帧循环分段计时**：事件/步进/绘制/HUD/提交分段预算，走环境变量开关的 prof 走廊。
5. **惰性初始化预热**：声卡、纹理、字体、shader 编译必须在启动期预热，不得落在玩家第一次操作那一帧。
6. **ARC 纪律**：全局事件订阅短生命周期视图必须解绑；高频对象走对象池。

## 6. 落地顺序

**第 1 批（生态骨架）**：`Zan.Logging` ✅ → `Zan.Math` ✅ → `Zan.Validation` ✅ → `Zan.Cli` ✅
**第 2 批（游戏纵深）**：`Zan.Game.Ecs` ✅ → `Zan.Game.Physics2D` → `Zan.Game.Pathfinding` → `Zan.Game.Particles`
**第 3 批（内容生产）**：`Zan.Game.Assets` → `Zan.Game.Audio` → `Zan.Game.Procgen` → `Zan.Game.Common`
**第 4 批（重型）**：`Zan.Game.Netcode` → `Zan.Game.Animation` → `Zan.Game.Render3D`（含跨平台 GPU 后端）→ `Zan.Pdf`

## 7. 已知缺陷：包级 static 持有 rc 集合在退出时不释放

**现象**：包（`packages/*/src/**`）里的静态字段持有 `List<T>` / `Dictionary<K,V>`
等 rc 集合时，进程退出时**不会被释放**——编译器退出期的静态释放只覆盖
**主编译单元**的静态字段，包单元的静态不在其中。后果是集合本身、以及集合
里的一切（落地端 → 事件 → 字段字典 → 时间戳）整条对象图都留在进程结束那一刻，
`--check-leaks` 会逐条报出。

**实证**（2026-10-20，结构化日志落地时撞到）：

- 探针：`tests/conformance/log_structured.zan`（注册表写成 `static List<ILogSink>`）
  → `leakcheck_log_structured` 报 `1 object(s) leaked … [List<ILogSink>]`，
  去掉注册表调用后同一用例干净。
- 反证：把注册表从 `static List<ILogSink>` 换成
  - 静态持有类实例（`static LogState` + 实例字段 `List<ILogSink>`）→ 仍泄漏；
  - 实例级注册表（`LogHub`，使用者持有）→ **完全干净**。
- 已知同类形态（本仓库既有）：`Zan.Web.extraOrigins`、
  `Zan.Desktop.Window.enumBuf`、`Zan.Game.CDraw.blitSources`——它们目前没被
  leakcheck 用例覆盖，所以没人看见。

**给生态包的规约**：常驻的可变状态放**实例**（使用者持有），不要放包级 static；
确需进程级单例时，像 `Activity.Current` 那样提供显式清空入口。

**编译器侧的修法方向**（未做，需独立变更）：退出期释放**所有单元**的静态字段，
或把静态视为 GC 根集（不计入泄漏）。后者语义上更接近 .NET（静态是根，不是泄漏），
但会改变全仓 leakcheck 用例的判定口径——影响面大，须单独评估；本次未动
`src/compiler/*`（该目录当时有其他会话的在途改动）。
