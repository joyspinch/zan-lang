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

### 1.6 2D 物理：`Zan.Game.Physics2D`

新包 `packages/Zan.Game.Physics2D`，命名空间 `Game.Physics2D`。补上 §3 里
标着的最大缺口：几何检测已有（`Arcade2D/Collision2D`），缺的是**动力学层**。

| 文件 | 类型 | 内容 |
|---|---|---|
| `Vec2.zan` | `Vec2` | 值语义向量（struct），运算符重载 `+ - * / == !=`，`Dot/Cross/Perpendicular/Rotated/Normalized/Length/DistanceTo` |
| `Aabb.zan` | `Aabb` | 包围盒（struct），`Overlaps/Contains/Center/Perimeter/Expand` |
| `Shape.zan` | `Shape` | 抽象形状基类：`Radius/Mass/InertiaPerMass/ComputeAabb` |
| `CircleShape.zan` | `CircleShape` | 圆（面积 πr²，`InertiaPerMass = ½r²`） |
| `PolygonShape.zan` | `PolygonShape` | 凸多边形（自动纠正顺/逆时针，外法线取边的右法线 `(dy,-dx)`，`InertiaPerMass = Σ cross·(…) /(12A)`） |
| `RigidBody.zan` | `RigidBody` | 质量/转动惯量、位置/角度/速度/角速度、`ToWorld/ToLocal/VelocityAt/PseudoVelocityAt`、`ApplyForceAtPoint`（带力矩） |
| `Manifold.zan` | `ContactPoint` / `Manifold` | 接触点集合：法线 A→B、接触点数、接触点列表 |
| `Collision.zan` | — | 窄相：`CircleCircle` / `CirclePolygon` / `PolygonPolygon`（SAT）、通用分派 `Collide` |
| `Contact.zan` | `Contact` | 常驻接触对象 + 热启动（`AdoptImpulses`）、材料组合（摩擦取几何平均、弹性取 max） |
| `Joint.zan` | `Joint` | 抽象关节：`ApplyForces/Prepare/SolveVelocity/SolvePosition` |
| `DistanceJoint.zan` | `DistanceJoint` | 刚性定距；`frequency > 0` 时按 Hz + 阻尼比退化为弹簧 |
| `RevoluteJoint.zan` | `RevoluteJoint` | 2×2 有效质量矩阵的点约束 |
| `PhysicsWorld.zan` | `PhysicsWorld` | 宽相（按 minX 插入排序的扫掠剪枝）+ 窄相 + 顺序冲量求解（法线 + 库仑摩擦、热启动、8 次速度迭代）+ 分离冲量位置修正 + 子步 |

**五条硬取舍**：

1. **只支持凸多边形**：凹多边形在 SAT 下没有唯一分离轴，直接拒绝（不静默
   给错结果）。
2. **无 TOI 扫掠，用推测接触顶替**：分离量落在 `speculativeMargin` 内也建接触，
   求解器把接近速度限制为 `gap/dt`——高速物体不再一步穿墙，但**极高速仍可能穿
   薄壁**（README 已写明边界）。
3. **穿透修正走分离冲量（伪速度）**，不往真实速度里注入能量：位置修正不应让
   物体弹开。
4. **接触点用类而不是 struct**：`List<T>` 取结构体元素是**副本**，累积冲量无处
   写回。这与 `Zan.Game.Ecs` 里泛型类值类型字段的坑是同源的。
5. **确定性优先**：无随机数、接触/关节顺序固定、迭代次数固定——回放与 lockstep
   的前提。

**落地时踩出的编译器缺陷（均已修）**：

| 缺陷 | 症状 | 修处 |
|---|---|---|
| 运算符重载不参与类型推导 | `this.value * s + this.value` 被推成整数加法，LLVM 报 "Integer arithmetic operators only work with integral types" | `irgen_expr_core.c` 的 `infer_expr_type` / `AST_BINARY` 分支解析 `op_*` 并回其返回类型 |
| 运算符实参按指针传 | `new 结构体(...)` 作运算符实参时，接收者/第二实参是指针槽位 → "Call parameter type does not match function signature" | `irgen_expr.c` 运算符调用路径：期望 struct 而实参是 ptr 时补 `LLVMBuildLoad2` |
| 跨编译单元 struct 字段布局降级 | 类字段声明在另一个文件时，字段类型被降级成 `i8*` → 布局整体错位 → **静默内存损坏**（实测 `v 3,0.25` 把 y 读成相邻 double） | `irgen.c` 的 `map_type`：`TYPE_STRUCT`/`TYPE_CLASS` 未注册时按「有 decl 则注册」再取（class 取指针、struct 取值） |
| `register_struct_type` 递归后条目错位 | 递归注册触发 `g->struct_types` realloc 搬家，事后用 `&g->struct_types[count-1]` 取到的是**别的类型**的条目 → 类型混淆 → **间歇性 0xC0000005**（ecs_world 10 次编译崩 1 次） | 登记后立刻记 `int my_index = g->struct_type_count - 1;`，递归后按 `my_index` 取条目 |

后两条的回归用例：`tests/conformance/struct_field_layout/`（结构体文件排在用户
文件之后，正好命中旧缺陷的文件序）。

conformance：`physics2d_world`（自由落体积分、落地静止、弹性反弹、库仑摩擦、
旋转多边形 SAT、穿透修正、铰链摆锤、定距关节、确定性重放）。

**已知边界**：只支持凸多边形；无 TOI 扫掠；角-角接触按单点近似；无 sleeping、
无 CCD。

### 1.7 寻路：`Zan.Game.Pathfinding`

新包 `packages/Zan.Game.Pathfinding`，命名空间 `Game.Pathfinding`。补上 §3
里标着的缺口：仓库里已有棋盘 BFS（`Game.Board.GridPathfinder`）与塔防流场
（`Game.Tactics.FlowField`），缺的是**加权**寻路与**连续坐标**路径。

| 文件 | 类型 | 内容 |
|---|---|---|
| `Point2.zan` | `Point2` | 值语义二维点（struct），运算符与 `Length/DistanceTo/Lerp/Normalized` |
| `GridMap.zan` | `GridPoint` / `GridMap` | 通行 + 每格代价；`FromBlocked` / `FromWalkable` / `FromCosts`；格子中心 ↔ 世界坐标 |
| `CellPath.zan` | `CellPath` | 格子路径（点序列 + 总代价 + 几何长度 + 展开节点数） |
| `OpenHeap.zan` | `OpenHeap` | 开放表用的最小堆（优先级 + 平局键） |
| `AStar.zan` | `AStarOptions` / `AStar` | 加权 A*：欧氏代价（对角 √2）、八分距离启发式、可调权重、展开上限 |
| `Jps.zan` | `Jps` | 跳跃点搜索：均匀代价 + 不切角，展开节点比 A* 少一个数量级 |
| `PathSmoother.zan` | `PathSmoother` | 视线拉直（string pulling），严格不切角 |
| `NavMesh.zan` | `NavRect` / `NavPortal` / `NavMesh` | 贪心最大矩形分解 + 门图 A* + 漏斗（funnel）平滑折线 |
| `Pathfinding.zan` | `Pathfinding` | 门面：`FindPath` / `FindPathJps` / `FindNavPath` |

**五条硬取舍**：

1. **确定性**：开放表是自带的二叉堆，优先级相同时按次级键（h）再按入堆
   顺序打破平局——同一对起终点必然给出同一条路径（回放 / 锁步的前提）。
2. **JPS 只在均匀代价网格上成立**：非均匀代价时 `Jps.Find` **自动退回
   A***——JPS 的剪枝规则在非均匀代价下不成立，硬用会给出次优路径。
3. **JPS 不切角**：对角移动与跳跃扫描都要求两个正交邻格可通行。
4. **导航网格用贪心最大矩形分解**，不做多边形裁剪：结果确定、实现简单，
   代价是矩形数比理论最优多。
5. **漏斗的"左/右"按行进方向定**：门的两个端点谁在左由"本矩形中心 →
   下一矩形中心"的方向决定——用固定方向会在急转弯走廊里把左右定反，
   路径会贴着走廊边缘走。

conformance：`pathfinding_grid`（A* 代价与形状、绕墙、加权地形、JPS 与
A* 路径长度一致且展开节点更少、不可达、确定性、拉直不穿墙、导航网格路径
必经缺口）。

**已知边界**：导航网格路径贴矩形边界走（无单位半径）；JPS 在狭长走廊上
可能比 A* 慢；无动态障碍（改地图要重建）；无多层导航。

### 1.8 粒子：`Zan.Game.Particles`

新包 `packages/Zan.Game.Particles`，命名空间 `Game.Particles`。收敛 13 套
游戏模板各自实现的粒子（`templates/game/*/src/.../Particle.zan` + idle 模板的
`ParticleSystem.zan`）。

| 文件 | 类型 | 内容 |
|---|---|---|
| `Particle.zan` | `Particle` | 位置/速度/旋转/寿命/尺寸/颜色；`CurrentSize()` / `CurrentColor()` |
| `Emitter.zan` | `EmitterConfig` / `Emitter` | 速率 + 突发发射；扇形角度、速度/寿命/尺寸区间、起止色、位置噪声漂移 |
| `Curve.zan` | `Curve` | 关键帧曲线（线性 / smoothstep），驱动尺寸、透明度等 |
| `Noise.zan` | `Noise` / `Rng` | 确定性值噪声（2D + 分形）与带种子的随机源 |
| `ParticleSystem.zan` | `ParticleSystem` | 对象池 + 半隐式欧拉积分 + 回收；容量硬上限 |

**五条硬取舍**：

1. **引擎无关**：不依赖 `Gui` / `SpriteBatch`，只产出粒子数据——包可无头
   跑、可在 conformance 里断言，渲染由调用方做。
2. **对象池**：粒子用完回收复用，运行期不分配；容量是硬上限（满了就不再
   发射），内存占用有界。
3. **确定性**：随机源带种子、噪声是纯函数（同一坐标永远同一值）——回放 /
   锁步可复现。随机数取 LCG 的**高位**：低位周期太短，会出现可见带状分布。
4. **半隐式欧拉**（先速度后位置）：粒子多时比显式欧拉稳，代价是对连续解
   有系统性偏差（自由落体 0.5 秒得 -12.9167 而不是 -12.5）。
5. **有序移除**：渲染顺序 = 出生顺序，叠加混合的视觉结果稳定。

conformance：`particles_system`（速率发射、突发发射、寿命回收、重力与阻力
积分、容量上限与对象池、确定性、曲线求值与钳位、噪声确定性与范围、曲线
驱动的尺寸与颜色插值）。

**已知边界**：无 GPU 粒子（渲染侧自行批量提交）；无粒子碰撞；噪声是值噪声
不是梯度噪声（做不了流体）；池不跨系统共享。

### 1.9 资源运行时：`Zan.Game.Assets`

新包 `packages/Zan.Game.Assets`，命名空间 `Game.Assets`。补上 `Game.Kit.PackedAssets`
（只读加密资源包）与 `Game.Tools.AssetPipeline`（离线元数据生成）之间缺的那一层：
**运行期的资源生命周期管理**。

| 文件 | 类型 | 内容 |
|---|---|---|
| `AssetKind.zan` | `AssetKind` / `AssetKinds` | 资源种类枚举 + 名称映射（名字不认识时**当场拒绝**，不静默降级） |
| `AssetEntry.zan` | `AssetEntry` | 清单条目：id / kind / path / depends / size |
| `AssetManifest.zan` | `AssetManifest` | 清单：`Declare` / `Add` / `Get` / `Validate` / `TopoOrder` / `ToJson` / `FromJson` |
| `IAssetProvider.zan` | `IAssetProvider` | 来源契约：`TryRead(path, out data)` / `Exists` |
| `FileAssetProvider.zan` | `FileAssetProvider` | 文件系统来源（相对 root 解析） |
| `MemoryAssetProvider.zan` | `MemoryAssetProvider` | 内存来源（测试 / 内嵌 / 网络注入） |
| `AssetEntryState.zan` | `AssetEntryState` | 运行时条目：refCount / depCount / pinned / lastUse |
| `AssetStore.zan` | `AssetStore` | `Acquire` / `Release` / `Pin` / `EnforceBudget` / `TrimIdle` |
| `AssetLoader.zan` | `AssetLoader` / `AssetDecoder<T>` | 按强类型解码字节（零反射） |
| `AssetGroup.zan` | `AssetGroup` | 按 id 前缀分组，整组加载 / 释放 |
| `AssetException.zan` | `AssetError` / `AssetException` | 可区分的错误码 |

**六条硬取舍**：

1. **清单与来源分离**：清单是纯数据（可 JSON 往返），来源决定字节从哪来。
   同一份清单能配文件（开发）/ 打包文件（发布）/ 内存（测试），调用代码不改。
2. **依赖是图的属性，不是引用次数**：`depCount` 恒等于"有多少个**已驻留**的资源
   依赖我"。混进 acquire 次数必然导致"提前回收"或"永远不回收"二选一。
3. **引用与驻留分离**：`Release` 只减引用，资源仍驻留（缓存），由预算回收。
   用完立刻扔掉会让下次使用重新读盘。
4. **确定性淘汰**：LRU 用单调序号（`lastUse`），**不用墙上时钟**——回放 / 锁步
   需要"同一串操作同一结果"。
5. **超预算时宁可超着**：全部资源被引用 / 依赖 / 钉住时淘汰停手，偷偷回收
   正在用的资源比超预算危险。
6. **零反射 + 非线程安全**：解码器按具体类型注册（`AssetDecoder<T>` 委托）；
   面向单线程主循环，跨线程由调用方加锁（锁粒度该由调用方决定）。

conformance：`assets_store`（清单 JSON 往返、重复 id / 悬空依赖 / 环依赖 / 未知
kind 拒绝、依赖自动拉取、depCount 图语义、引用计数累加与下溢报错、预算淘汰、
LRU 顺序、pinned 保护、依赖阻塞淘汰、级联回退、资源组、文件来源缺失处理、拓扑序）。

**已知边界**：无异步 / 流式加载；无内容哈希与热更比对；淘汰只按真实字节数；
资源组是清单层面的划分（按 id 前缀），不携带运行期状态。

### 1.10 音频处理：`Zan.Game.Audio`

新包 `packages/Zan.Game.Audio`，命名空间 `Game.Audio`。补上原生音频栈
（`System.Media`：WAV/OGG 解码 → 声卡）**够不着**的那一半：原生层给的是
"播出来"，**不给"拿到样本"**。而响度归一、变调、切片、离线混音、程序化生成
音效，全都要在纯 Zan 侧看到采样数据。

| 文件 | 类型 | 内容 |
|---|---|---|
| `AudioBuffer.zan` | `AudioBuffer` | 采样缓冲（double 存储）：峰值 / RMS / 直流、归一化、限幅、淡入淡出、拼接 |
| `Oscillator.zan` | `Oscillator` / `Waveform` | 相位累加振荡器：正弦 / 锯齿 / 方波 / 三角 / 噪声 |
| `AdsrEnvelope.zan` | `AdsrEnvelope` / `EnvelopeStage` | ADSR 包络状态机（攻击 / 衰减 / 保持 / 释放） |
| `BiquadFilter.zan` | `BiquadFilter` / `FilterKind` | 二阶滤波（RBJ 公式）：低通 / 高通 / 带通 / 陷波 / 峰化 |
| `OnePoleFilter.zan` | `OnePoleFilter` | 一阶 RC 滤波（6 dB/oct，无相位失真） |
| `AudioMixer.zan` | `AudioMixer` | 多路混音 + 统一削波 |
| `Synth.zan` | `Synth` / `SynthOptions` | 参数化音效合成（含频率扫频） |
| `PitchShift.zan` | `PitchShift` | 重采样 / 变调不变速（OLA）/ 采样率转换 |
| `Wav.zan` | `Wav` | 纯 Zan WAV 读写（PCM 8/16/24/32 + IEEE float） |
| `Rng.zan` | `Rng` | 带种子的确定性随机源（取 LCG 高位） |

**六条硬取舍**：

1. **内部统一 double**：样本存 `double[]`（归一化到 [-1, 1]），只在导出时
   量化回目标位深。混音乘增益、滤波要累加，int 会中途溢出 / 截断。
2. **相位累加而非每样本求 sin**：`sin(2*pi*f*t)` 在大时间偏移时丢精度，
   且每样本一次三角函数太慢；累加相位的精度只跟增量有关。
3. **噪声取 LCG 高位**：低位周期极短，直接用会出现可见的带状分布
   （合成噪声听起来有"音调"）。
4. **ADSR 是显式状态机**：不用"当前时间 vs 阶段时间"倒推——改参数
   （跳段）时会算出负时长，行为不可预测。
5. **滤波状态按声道独立**：共用一份状态会让右声道被左声道的历史污染。
6. **等功率交叉淡化**（sqrt）而不是线性：线性叠加时中间掉能量
   （两路 0.5 → 0.25），听起来有"空洞"。

conformance `audio_dsp`（缓冲的峰值/RMS/直流/归一化/限幅/淡入淡出/拼接、
振荡器波形与相位归一与噪声确定性、包络四段状态机与提前释放、滤波的低通/
高通/陷波与一阶直流增益、混音叠加/增益/削波/格式拒绝、合成确定性与频率
扫频、变调时长不变与重采样比例、WAV 16 位与浮点往返与非法输入拒绝）。

**已知边界**：无 MP3 解码（OGG 由原生层 stb_vorbis 负责）；无流式播放
（纯离线处理）；重采样无抗混叠滤波；变调用 OLA 不做波形相似度对齐
（轻微"金属味"）；IIR 滤波有启动暂态（断言要看稳态段）。

### 1.11 程序化生成：`Zan.Game.Procgen`

新包 `packages/Zan.Game.Procgen`，命名空间 `Game.Procgen`。补上"关卡怎么来"这一段：
WFC（波函数坍缩）拼图关卡、按接口标签横向拼接子关卡、房间-走廊地牢布局。

| 文件 | 类型 | 内容 |
|---|---|---|
| `WfcTile.zan` | `WfcTile` / `WfcDir` / `WfcDirs` | 瓦片 + 四方向允许列表（null = 不限制）；方向工具（取反 / 偏移） |
| `WfcTileSet.zan` | `WfcTileSet` | 瓦片集 + 有向邻接规则 + 权重；`Validate()` 拒绝不对称规则与未知瓦片 |
| `WfcSolver.zan` | `WfcSolver` | WFC 求解器：最低熵坍缩 + 队列传播 + 矛盾定位 |
| `LevelStitcher.zan` | `LevelStitcher` / `Chunk` / `Stitched` | 按出口标签横向拼接子关卡（拼不出时返回 `error` 而不是抛异常） |
| `RoomCorridorGenerator.zan` | `RoomCorridorGenerator` / `Rect` | 房间撒点 + MST 连通 + 额外回路 + 补墙 |
| `Rng.zan` | `Rng` | 带种子的确定性随机源（取 LCG 高位） |

**六条硬取舍**：

1. **域不用位掩码**：每格一个 `List<int>`。Zan 的 `long` 不支持 `<<`，做不了
   64 位掩码；位掩码方案还会把瓦片数**封顶在 64**。列表求交是 O(n)，
   但 WFC 网格通常几百格、瓦片几十个，不是瓶颈。
2. **规则是有向的**：`A` 的右边可以是 `B`，`B` 的右边不一定是 `A`
   ——单向箭头、单向门都需要这种不对称。
3. **不对称规则在构造期就报错**：违反"A 的 dir 允许 B ⇒ B 的反方向允许 A"的
   规则**永远无解**（传播时两边互相排除）。报错比在求解中途抛矛盾好得多。
4. **传播的判据是"交集比当前域小"**，不是"允许集比当前域小"。后者在
   "邻居域已缩到 1、允许集有 3"时会误判成有变化并重新入队，两个相邻格
   互相触发、**无限乒乓死循环**（实测挂死，判据修正后 4 格全坍缩）。
5. **房间连通用最小生成树**（朴素 Prim）而不是"随机连最近"：后者产生长短
   不一的走廊和大量绕路。树化后另挖**额外回路**（默认房间数 / 4）——
   回路只加地板、不拆地板，连通性不会被破坏；纯树关卡让玩家频繁原路返回。
6. **拼接失败返回 `error` 而不是抛异常**："这批素材拼不出来"是正常情况，
   调用方要决定是换一批还是降级。WFC 的矛盾则是**求解失败**，返回 `false`
   并给出 `conflict` / `conflictCell` 定位。

conformance `procgen_wfc`（WFC 求解与全坍缩、邻接规则成立、同 seed 同结果、
不同 seed 不同结果、不对称规则拒绝、重复 id 拒绝、未知瓦片拒绝、拼接的
标签匹配 / 宽度累加 / 顺序 / 拼不出报错 / 空输入报错、房间不重叠与墙紧贴
地板、确定性、洪水填充验证全连通、额外回路只加地板且保持连通）。

**已知边界**：WFC 只有最小熵启发式，没有**回溯**（矛盾即失败，不重试别的
坍缩顺序）；无"边界/接缝规则"（首尾行的专用瓦片）；无多样本生成
（同 seed 只能出一张图，想换一张得换 seed）；房间走廊只有 L 形（先横后竖，
不拐第二种形状）；拼接只支持横向一行，且各块高度不等时下方补空。

### 1.12 游戏公共基座：`Zan.Game.Common`

新包 `packages/Zan.Game.Common`，命名空间 `Game.Common`。补上 16 套游戏模板
之间反复重写的那部分：**存档**、**飘字**、**血条**、**冷却条**、**背包格**、
**小地图**。每个模板都自带一份 `Profile`（手搓 key=value 文本解析）、一份
`Popup`（裸字段的飘字）、自己的背包格与冷却逻辑——换个游戏抄一遍，抄出三种
口径。

| 文件 | 类型 | 内容 |
|---|---|---|
| `ProfileStore.zan` | `ProfileStore` | 键值型存档：JSON + format 版本标记 + 原子替换 + `.bak` 备份恢复 |
| `FloatText.zan` | `FloatText` / `FloatTextLayer` | 飘字：上升 + 淡出，池化复用，容量硬上限 |
| `HealthBar.zan` | `HealthBar` | 血条：真实值 + 掉血残影（滞后追赶），回血立即跟随 |
| `Cooldown.zan` | `Cooldown` | 冷却条：剩余比例口径统一 + 充能（可连按） |
| `Inventory.zan` | `InventorySlot` / `Inventory` | 背包格：堆叠规则（先堆叠后开格）+ JSON 往返 |
| `Minimap.zan` | `Minimap` / `MinimapEntity` / `MinimapLayer` | 小地图：世界→像素投影 + 视口剔除 + 帧内标记复用 |

**六条硬取舍**：

1. **模型与渲染分离**：本包只产出数据，不碰 Gui / SpriteBatch——所以这些
   逻辑能无头跑、能被 conformance 逐条断言、能换任何渲染后端（与
   `Zan.Game.Particles` 同一原则）。
2. **存档损坏不毁档**：读不出来就返回 `false` + 原因，内存不清空、磁盘不删；
   写盘先写 `.tmp`、读回校验通过才顶替正式文件，替换前留 `.bak`。玩家可能
   手动抢救，程序无权替他销毁。
3. **版本标记拒绝加载**：旧档不该被新代码悄悄按新语义改写（"high=100" 在
   新版本可能是别的意思）。
4. **血条只在受伤方向留残影**：回血立即跟随——血条慢慢往上爬看着像掉血。
5. **冷却条口径只有一个**：`Ratio()` 是剩余比例（1 = 刚按下，0 = 就绪），
   UI 不用再猜方向；`Progress()` = 1 - `Ratio()`。
6. **容量硬上限 + 满了不新增**：飘字层与标记层满了就丢弃并计数
   （`DroppedCount()`）——静默吞掉会让"飘字偶尔不出现"这类 bug 查不出来。

conformance `game_common`（存档往返/默认值/格式不符拒绝/损坏拒绝/原文件保留/
原子替换不留 .tmp/备份恢复、飘字生成上升淡出过期回收池复用池满丢弃、血条
比例与夹取与残影滞后追赶与回血立即跟随、冷却条口径与充能与冷却中不可触发与
瞬发不耗充能、背包先堆叠后开格与满包剩余与取出清空与 JSON 往返与坏档不改
背包、小地图映射与视口剔除与全图模式与帧内复用）。

**已知边界**：无加密存档（明文 JSON，防篡改需自行套一层）；无多槽位管理
（一个实例一个文件）；背包不支持装备位/权重/排序；小地图只做投影与剔除，
绘制归渲染层；飘字只有垂直上升（横向漂移自己设 `vx`）。

### 1.13 回滚网络同步：`Zan.Game.Netcode`

新包 `packages/Zan.Game.Netcode`，命名空间 `Game.Netcode`。补上 `Game.Net`
（lockstep + 回放）缺的另一半：**快照同步、rollback 预测、延迟补偿**。
lockstep 要求所有客户端同时推进（一方卡顿全员等），rollback 让本地不等远端。

| 文件 | 类型 | 内容 |
|---|---|---|
| `Checksum.zan` | `Checksum` | 确定性 FNV-1a 校验和（31 位非负），用于分叉检测 |
| `Snapshot.zan` | `Snapshot` / `SnapshotBuffer` | 世界状态快照 + 按帧号存取的环形缓冲（覆盖检测） |
| `InputFrame.zan` | `InputFrame` / `InputHistory` | 逐帧输入 + 权威/预测标记 + 环形历史 |
| `RollbackSession.zan` | `RollbackSession` | 回滚会话：`Advance` / `SubmitRemoteInput` / `Resimulate` / `VerifyChecksum` |
| `LagCompensation.zan` | `PositionHistory` / `LagCompensation` / `NetPoint` | 服务器侧按帧位置历史 + 回溯钳制 + 取位置 |

**六条硬取舍**：

1. **状态用不透明字节**：回滚层不知道游戏状态长什么样，游戏自己
   `Capture` / `Restore` 成字节。代价是状态字节数**必须恒定**。
2. **预测只用"沿用上次输入"**：不做方向外推——简单、不会引入"预测出来的
   怪动作"，代价是猜错率随延迟上升。
3. **预测基线是"该帧之前最近的权威输入"**，不是"最近收到的那个值"：远端
   输入可能提前到达（第 5 帧先到），拿它预测第 2 帧就是拿未来当过去。
4. **回滚失败要明说**（`Resimulate()` 返回 -1）：快照或输入被环形缓冲覆盖
   时，拿别的帧的状态恢复会把世界改成另一个样子，比不同步更难查。
5. **校验和用 FNV-1a 而不是哈希表**：哈希表迭代顺序不保证，拿它算校验和
   = 永远在报"分叉"。截到 31 位避开 int 最高位的跨平台呈现差异。
6. **延迟补偿只回溯、不改写历史**：补偿是判定时的一次查询；改写历史会让
   "回溯过的判定"污染后续帧。回溯窗口有上限（延迟 10 秒的客户端不能让
   服务器回到 10 秒前判命中）。

conformance `netcode_rollback`（校验和确定性与字节敏感性与空输入、快照缓冲
存取与帧号校验与覆盖后旧帧不可用与越界取回失败、输入帧权威值只接受一次与
确认状态与预测标记、回滚会话的预测推进与回滚重放与重放结果与权威一致与
预测错计数与快照丢失时回滚失败并明说与提前到达输入不算预测错、分叉检测、
延迟补偿的位置历史与回溯钳制与取位置）。

**已知边界**：无网络传输层（归 `Zan.Net`）；无输入延迟（input delay）；
状态字节数必须恒定；回滚窗口 = 环形缓冲容量；延迟补偿只回溯位置，不回溯
完整世界（技能范围等复杂判定要自行扩展历史）。

### 1.14 接口替身与行为校验：`Zan.Test.Mocks`

新包 `packages/Zan.Test.Mocks`，命名空间 `System.Test.Mocks`。补上 Moq /
NSubstitute（6,410★）那一档的能力，但走**另一条路**：Moq 靠动态代理凭空
造接口实现（代码生成 + 表达式树），Zan 没有那套——**替身必须手写**，
本包提供手写替身缺的那一半：**记录调用与事后校验**。

| 文件 | 类型 | 内容 |
|---|---|---|
| `Arg.zan` | `ArgMatcher` / `ArgKind` / `Arg` | 实参规范化（int/long/double/bool/string）+ 六种匹配器 |
| `CallRecord.zan` | `CallRecord` / `Times` | 一次调用记录 + 次数期望（恰好 / 至少 / 至多） |
| `CallLog.zan` | `CallLog` | 调用日志 + 行为校验（`Verify` / `Never` / `Called`）+ 失败描述 |
| `MockBase.zan` | `MockBase` | 手写替身基类：`Record0..Record3` / `Calls()` / `ResetCalls` |
| `Stub.zan` | `StubValue<T>` / `Sequence<T>` / `ActionSpy` | 桩件：固定返回值 / 顺序返回 / 回调探针 |
| `TaskStub.zan` | `TaskStub` | 异步桩：已完成 / 已失败 / 永不完成的任务 |

**六条硬取舍**：

1. **不生成 mock**：替身手写。代价是每个接口一个替身；收益是没有代码
   生成、没有代理开销、行为完全可读可断言。
2. **实参存规范化文本**：替身拿不到实参的静态类型（没有反射），所有实参
   经 `Arg.Text(...)` 规范化后记录，`Arg.Eq(3)` 才能与实际传入的 3 对上。
3. **匹配器只有六种**（Any / Eq / NotEq / Contains / Prefix / Between）：
   Moq 的 `It.Is<int>(x => x > 3)` 靠表达式树求值，这里用区间覆盖最常见
   的数值范围断言。**实参个数必须相同**才可能匹配。
4. **断言与本包解耦**：校验返回 `bool`，失败描述由 `Describe()` 给出，
   用哪个断言库由调用方决定（本包不依赖 `Zan.Test`）。
5. **记录与断言分开**：替身只记录，不在方法里断言——同一个替身可以先跑
   一遍，再断言好几条不同的事实。
6. **序列为空时抛异常**：没有默认值可返回（Zan 没有 `default(T)`），
   而且"空序列"一定是配置错误。

conformance `test_mocks`（实参规范化含 null 归一、六种匹配器、调用日志的
记录与计数与查询与清空、行为校验的三种次数模式与带参数匹配与参数个数不匹配、
`MockBase` 派生替身的记录与计数与重置、桩件的改值与顺序与重复末值与空序列
抛错与回调探针、异步桩的成功与失败）。

**已知边界**：无运行时生成 mock；不记录属性 setter；无参数化匹配（谓词）；
double 相等依赖格式化一致；`Sequence<T>` 用完重复末值；`TaskStub.Pending`
会留下挂起协程。

### 1.15 描述统计与图表桥：`Zan.DataFrame` + `Zan.Gui.Charts`

**不新建平行包**：统计是对"一列数值"的加工，列是 `DataFrame` 的原生概念；
绘图侧已有 `Zan.Gui.Charts`（164 文件，ECharts 全套）。两个已有包各扩一层，
中间不夹新包。

**缺口（实测）**：`Zan.DataFrame` 5 个文件 20 个方法，只有
`Filter`/`SortBy`/`GroupBy`+`Aggregate`(4 op)/`Join`(单键内连接)；
`Zan.Gui.Charts` 的数据入口是 `ChartJsonDataset`（`JsonValue` 源），
与 `DataFrame` 之间**没有桥**——"算出统计结果"和"画成图"分属两个世界，
中间要手写 source 数组 + 类目轴 + 系列数据。两端都有，中间断了。

| 文件 | 位置 | 内容 |
|---|---|---|
| `Stats.zan` | `packages/Zan.DataFrame/src/System/Data/` | `Stats`：Mean / Median / Percentile（线性插值）/ Min / Max / Range / Variance / SampleVariance / StdDev / SampleStdDev / Skewness / Kurtosis / Correlation / SortedCopy |
| `DataFrame.Stats.zan` | 同上 | `DataFrameStats`：列 → 数值序列 → 统计；`Column` 做唯一的类型转换点（Int/Float 直取，Str 按 strtod 前缀） |
| `ChartBridge.zan` | `packages/Zan.Gui.Charts/src/Gui/Component/Chart/` | `ChartBridge`：`ToDataset`（DataFrame → `ChartJsonDataset`，首行表头）/ `CategorySeries` / `CategoryAxis` / `ValueSeries` / `GroupedSeries`（一表多系列，分组保序）/ `AddSeries` / `AddGrouped`（挂到 `ChartOption`） |

**四条硬取舍**：

1. **依赖方向是 `Gui.Component.Chart → System.Data`，不是反过来**。
   实测：图表侧多带 `System.Data` 只多 19 个编译单元（337 → 356），
   反过来让 `System.Data` 依赖图表要多带 297 个——数据层不该知道图表
   存在。
2. **桥直接填 `series.data`，不走 dataset 自动绑定**。ECharts 的
   dataset→series 绑定发生在 option **解析期**（`ChartOption.FromJsonValue`），
   程序化构造的 `ChartOption` 不走那条路。桥填的是渲染器真正读的
   `cs.data`。
3. **统计的边界语义显式定死**：空集返回 0.0（不抛——聚合查询里空组很
   常见）；常量序列的偏度/峰度返回 0 而非 NaN（NaN 会污染下游图表）；
   分位数用线性插值（与 numpy 默认一致）。
4. **数值走 `ChartData.Numeric`（double）不走整数道**。整数道会把小数
   抹平（`SeriesFrac` 判定为假），小数项在系列里有专门的定点通道。

conformance `analytics_bridge`（统计的已知值核对与边界语义、
`SortedCopy` 不改入参、相关系数的正/负/常量三种情形、DataFrame 统计
入口与缺列语义、DataFrame → dataset 的表头形态与行号表、DataFrame →
系列/类目轴、一表多系列的分组保序、小数不被截断、option 组合与已有
类目轴不覆盖、缺列抛异常、空表桥接不崩）。

**已知边界**：无多级索引与缺失值处理（多级索引与缺失值是表级变换的
另一块，见 §1.19 末尾）；`GroupedSeries` 只做等值分组，不做区间分箱；
`ToDataset` 的 Str 列不隐式转数值（列建错类型不会被静默修正）。

### 1.16 三维动画运行时：`Zan.Game.Animation`

新包 `packages/Zan.Game.Animation`，命名空间 `Game.Animation`。补 §3 里记的
那一档缺口：**蒙皮 + 状态机混合 + IK**——3D 角色真正能动起来的那三件事
（仓内原有的 `AnimPlayer` 走 2D 帧表、`Animation3DTrack` 只有加权轨道、
`PaperdollRenderer` 只能换贴图）。

| 文件 | 类型 | 内容 |
|---|---|---|
| `Transform3D.zan` | `Transform3D` | TRS 变换：组合 / 求逆 / 世界↔局部反解 / 点与方向变换 |
| `AnimMath.zan` | `AnimMath` | float 标量工具（钳位 / 插值 / 归一化）+ **确定性球面插值** |
| `Bone.zan` | `Bone` | 骨骼：名字 / 父下标 / 绑定姿势 / 局部姿势 / 世界姿势 |
| `Skeleton.zan` | `Skeleton` | 骨架：加骨（父在子前）/ 绑定姿势 / 世界姿势递推 / 蒙皮矩阵 |
| `AnimationTrack.zan` | `AnimationTrack` | 单骨三通道关键帧（位移 / 旋转 / 缩放）+ 采样 |
| `AnimationClip.zan` | `AnimationClip` | 片段：按骨名索引的轨道 + 采样到骨架 + 时长自动跟随 |
| `PoseBuffer.zan` | `PoseBuffer` | 姿势缓冲 + 交叉淡化混合（旋转走球面插值） |
| `AnimationPlayer.zan` | `AnimationPlayer` | 播放器：推进 / 循环 / 变速 / 播完停止 |
| `AnimationState.zan` / `AnimationTransition.zan` / `AnimationStateMachine.zan` | 状态机 | 状态 / 转移（浮点·布尔·触发器）/ 参数驱动 + 交叉淡化 |
| `SkinMesh.zan` | `SkinMesh` | 四槽权重蒙皮网格 + 权重归一 |
| `TwoBoneIk.zan` | `TwoBoneIk` | 两骨解析 IK（余弦定理 + 极向量）+ 朝向构造 |
| `AnimationException.zan` | `AnimationException` | 越界 / 重名 / 顺序错 / 几何退化一律当场报错 |

**六条硬取舍**：

1. **纯变换，不碰矩阵**。`System.Numerics` 的 `Matrix4x4` 缺通用求逆与四元数
   构造，拿它做蒙皮得自己补一整套矩阵层；本包改走 TRS 路线，组合与求逆在
   `Transform3D` 里闭合。代价：TRS 在"父级非等比缩放 + 子级带旋转"时无法
   精确分解（缩放与旋转不可交换，业界通病），骨骼缩放建议等比。
2. **球面插值自己写一遍**（`AnimMath.Slerp`）。两个朝向正好差 180° 时四元数
   点积为 0，"往哪边转"本该任意，但系统库按 `dot < 0` 取反——一个 -4e-8 的
   浮点噪声就能决定人物朝**反**方向转身，同一份动画在不同机器上插值方向
   相反。本包固定"按目标自身的轴走"，结果与平台无关。
3. **姿势按骨名索引**（片段里），不按下标：动画资产来自 DCC 工具，那里的
   引用单位是骨骼名。
4. **片段只描述动的部分**：某骨没有轨道时采样回落到它的绑定姿势，不是单位
   变换。
5. **状态机自持参数**：浮点 / 布尔参数与一次性触发器都在状态机里，转移条件
   由参数驱动，调用方不必另建一层条件系统。
6. **IK 是解析解**：两骨 IK 用余弦定理一次算完；够不着时把距离钳到边界
   （手臂伸直指向目标），不炸不抖。

conformance `animation_pipeline`（变换的单位性 / 组合顺序 / 父级缩放对子级位移
的影响 / 求逆回代 / 方向不含平移；骨架的重名与越界报错 / 世界姿势递推 / 绑定
姿势重置 / 世界姿势反解局部姿势；蒙皮的绑定姿势不动点 / 位移精确落位 / 旋转
保半径；片段的关键帧插值 / 时长自动延长 / 无轨道回落绑定姿势 / 越界钳位 /
球面插值 / 180° 插值方向确定 / 乱序关键帧报错 / 姿势缓冲长度校验；播放器的
推进 / 循环回绕 / 变速 / 播完停止；状态机的浮点与布尔参数触发 / 触发器一次性 /
交叉淡化的权重曲线 / 未知状态报错；蒙皮网格的权重混合与归一；两骨 IK 的末端
命中目标 / 骨长不变 / 够不着时朝向不歪）。

**已知边界**：无曲线缓动（缓动要在导入阶段烘进关键帧）；无动画层与遮罩
（状态机是单层 + 交叉淡化）；IK 只有两骨；蒙皮是 LBS（不做 Dual Quaternion，
极端弯曲处会体积塌陷）；无 DCC 导入器（片段与网格由外部工具导出成数据）。

### 1.17 假数据生成：`Zan.Test.Fixtures`

新包 `packages/Zan.Test.Fixtures`，命名空间 `System.Test.Fixtures`。补 Bogus
（9,747★）/ AutoFixture（3,539★）那一档：**能造出"像真的"的测试数据**。

| 文件 | 类型 | 内容 |
|---|---|---|
| `FixtureRandom.zan` | `FixtureRandom` | 种子化随机源：int / long / double / float / bool / 字符串 / 字母串 / 数字串 / 十六进制 / 字节 / UUID / 日期时间 / 时长 / 抽样 / 洗牌 |
| `FixtureText.zan` | `FixtureText` | 语义化数据：姓名 / 登录名 / 邮箱 / 域名 / URL / 电话 / 地址 / 城市 / 国家 / 邮编 / 公司 / 句子 / 段落 / slug / 颜色 / 版本号 / 卡号 |
| `FixtureCollection.zan` | `FixtureCollection` | 集合假数据：定长生成 / 数组 / 字典 / 唯一值 / 分块 / 抽样 |
| `FixtureBuilder.zan` | `FixtureBuilder` | 显式装配的取值器：按键登记 + 取值（同一键恒等） |
| `FixtureException.zan` | `FixtureException` / `FixtureError` | 未登记键 / 约束取尽 / 参数非法（可区分） |

**五条硬取舍**：

1. **无反射，对象图必须显式装配**。AutoFixture 靠反射扫字段自动填，Zan 没有
   反射——本包提供另一半：**按名字取值**。登记 `fb.WithInt("age", 18, 60)`，
   再由调用方 `u.age = fb.Int("age")` 一行行装配。装配显式，但每个字段的
   来源都看得见（比反射自动填更可控）。
2. **登记时就把值定下来**，不是取值时现生成：同一键两次取到同一个值，
   免得"对象里的字段"与"断言里的期望值"对不上。
3. **未登记的键当场报错**（`FixtureError.KeyNotRegistered`），不现编一个值
   ——"忘了登记"和"名字打错"都当场暴露。
4. **唯一值有重试上限，取尽报错**，不返回重复值。重复的"唯一"字段会让
   测试以莫名其妙的方式失败。
5. **不生成"看起来能用"的敏感数据**：卡号走保留测试段（4242…）、电话走保留
   测试号段（+1-555-…）；UUID 自己拼字节并摆正版本位与变体位（过得了严格
   的 UUID 校验），但不用操作系统的随机源——**同一种子必须给出同一批数据**，
   否则失败样本复现不了。

conformance `test_fixtures`（随机源的区间边界与反向区间、字符串长度、字母 /
十六进制字符集、UUID 形态、字节数、日期与时长区间、空集合取样报错；同种子
同序列与异种子异序列；邮箱 / URL / 地址 / 电话 / 颜色 / 版本号 / 卡号的形态；
集合的定长生成与下标传递、数组与字典、唯一性、取尽报错、分块、抽样不重复；
装配器的取值稳定、类型隔离、未登记键报错、自定义生成器只跑一次、键名清单、
登记区间生效）。

**已知边界**：无反射式自动装配；无属性 / 字段级的"忽略"规则；无日期序列
（工作日 / 节假日）；词表是内联小表而非多语言语料库；不保证跨语言一致
（同一字段在不同 locale 下没有对应词表）。

### 1.18 压缩：已由 `Zan.IO` 覆盖，不新建平行包

原 §2 里 `Zan.Compression` 记的缺口（"压缩只在 AppUpdate 零散使用；无通用
deflate/lz4/zstd"）**已经过期**：`packages/Zan.IO/src/System/IO/Compression/`
里有纯 Zan 实现的 RFC1951 `Deflate`、`Lz4Block` / `Lz4Frame`、`GZip`、
`BZip2`、`Zip` / `ZipWriter`、`Tar`、`Crc32`，与任何标准实现互通
（conformance `lz4_roundtrip` / `compression_archive_lifecycle` /
`zip_deflate_bounds` 在跑）。

按 `Zan.Math` 的先例（并入 `stdlib/System/Numerics`，不另开包），**压缩不再
单开 `Zan.Compression`**：同一个能力有两个家只会让人不知道该用哪个。

剩下的真空白只有 **zstd**：它的熵编码（FSE + Huffman）解码器体量大，
编码器可以先做"仅 raw block"的合法子集，但收益有限，暂缓。

### 1.19 PDF 生成：`Zan.Pdf`

新包 `packages/Zan.Pdf`，命名空间 `System.Pdf`。补 QuestPDF（14,205★）那一档的
**生成**侧：把数据排成页、写出去。原 §2 记的"全仓唯一 pdf 命中是 HttpClient 的
MIME 映射"确实是空白——没有任何排版 / 字体 / 表格引擎。

| 文件 | 类型 | 内容 |
|---|---|---|
| `PdfDocument.zan` | `PdfDocument` | 文档：加页 / 取页 / 序列化 / 存盘 + 标题 / 作者 / 主题 / 关键词元数据 |
| `PdfPage.zan` | `PdfPage` | 页面：画线 / 矩形（填充·描边·两者）/ 文本（盒内对齐）+ 文本测量 |
| `PdfPageSize.zan` | `PdfPageSize` | A4 / Letter / Legal / 自定义（单位：点） |
| `PdfFont.zan` | `PdfFont` / `PdfMetrics` | 标准 14 字体（Helvetica / Times / Courier × 常规粗体）+ 每字符宽度表 + 上伸下伸与行高 |
| `PdfColor.zan` | `PdfColor` | RGB 颜色：黑 / 白 / 灰 / 0-255 / 十六进制 / 混合 |
| `PdfTable.zan` | `PdfTable` / `PdfColumn` | 表格：列宽 / 对齐 / 内边距 / 表头 / 边框 / 行高按内容撑开 / 绘制 |
| `PdfGraphics.zan` | `PdfGraphics` / `PdfGraphicsKind` | 绘图指令原语（线 / 矩形 / 文本） |
| `PdfWriter.zan` | `PdfWriter` | PDF 1.4 写出器：Catalog / Pages / Page / 内容流 / 五个字体对象 / xref / trailer |
| `PdfBuffer.zan` | `PdfBuffer` | 字节缓冲 + 不依赖 locale 的定点数字格式化 |
| `PdfException.zan` | `PdfException` / `PdfError` | 参数非法 / 字符编不出来 / 文档状态不对（可区分） |

**六条硬取舍**：

1. **只用标准 14 字体，不嵌字体**。这几种字体是阅读器自带的：文档小、没有
   授权问题、任何阅读器都能开。代价是没有 CJK——标准 14 字体没有汉字字形。
2. **编不出来的字符当场报错**（`PdfError.UnencodableCharacter`），不写出一份
   显示成方框或干脆打不开的文档。PDF 这种格式"要么全对要么全错"，半个可用的
   文档比报错糟糕得多。
3. **坐标用左上角原点、y 向下**，翻成 PDF 的左下角原点只在一处做（写内容流
   时 `H - y`）。排版代码按 HTML 画布的直觉写。
4. **不自动分配列宽**：列宽显式给。自动分配要么按内容比例（长文本的列被挤成
   一条），要么等分（表格布局引擎要的是"我说了算"）。
5. **数字不用 `Convert.ToString`**：不同 locale 的小数点与千分位不同，写进
   PDF 会让阅读器解析出错误的坐标；包内自写定点格式化（最多三位小数）。
6. **只生成不解析**：本包不是 PDF 阅读器。读 PDF 的文本与表格是另一件事
   （内容流解析 + 字体映射）。

conformance `pdf_document`（文件头 / 文件尾、startxref 指向的确实是 xref 表、
**每条 xref 记录指向的确实是 `<对象号> 0 obj`**、对象号与页树一致；Catalog /
Pages / Page / Font 对象、MediaBox、文本操作符序列、颜色操作符、坐标翻转；
括号与反斜杠的 PDF 字面量转义；标准 14 字体的宽度 / 上伸 / 行高；表格的列宽
累加、总高、绘制高度与测量一致、单元格数校验；错误路径：无页文档、编不出来
的字符、非法页尺寸、越界取页）。

**外部验证**：生成的样例文档用 **pypdf** 与 **pymupdf** 两个独立实现解析通过
（页数、元数据、文本抽取、表格单元格、右对齐与居中的坐标全部正确），并渲染
出位图人工复核过版面。

**已知边界**：没有中文 / CJK（要中文得嵌 TrueType 子集 + 编码表）；不解析
PDF；没有图片（XObject）；内容流不压缩（可后续接 `System.IO.Compression.Deflate`）；
没有加密 / 签名 / 附件 / 书签 / XMP；表格没有合并单元格与跨页表头重复；只支持
Type1 简单字体与 WinAnsiEncoding。

---

### 1.19 表级变换：`Zan.DataFrame` 补齐 pandas 的 reshape 家族

**不新建平行包**：表级变换（选列/算列/去重/拼接/宽转长/长转宽/透视/连接/
窗口/重采样）全部是 `DataFrame` 的操作，与 §1.15 的统计同属一个包。
依赖方向不变：`System.Data` 不依赖任何图表或分析包。

**缺口（实测）**：§1.15 之后 `Zan.DataFrame` 有了统计与画图桥，但仍缺
pandas 的重头戏——"表长什么样"的变换。§1.15 末尾记的已知边界
（无透视表/多级索引/时序重采样/滑动窗口/缺失值处理）就是这块。

| 文件 | 位置 | 内容 |
|---|---|---|
| `Reshape.zan` | `packages/Zan.DataFrame/src/System/Data/` | `Reshape`：`Select`（选列）/ `WithColumn`（算列）/ `WithTextColumn` / `DropDuplicates`（按列判重，保留首次）/ `Concat`（纵向拼接）/ `Melt`（宽转长）/ `Unstack`（长转宽）/ `Transpose` |
| `Pivot.zan` | 同上 | `Pivot`：`PivotTable`（行标签 × 列标签，多值列）/ `Crosstab`（交叉表，只计数） |
| `Merge.zan` | 同上 | `MergeKind` 枚举 + `Merge`：`MergeTables`（内/左/右/外连接，多键）/ `Cross`（笛卡尔积） |
| `Window.zan` | 同上 | `Window`：`Rolling`（滑动窗口）/ `RollingValid`（有效性掩码）/ `Ewm`（指数加权）/ `Diff`（差分） |
| `Resample.zan` | 同上 | `Resample`：`ResampleBy`（秒/分/时/日/周/月重采样）/ `BucketStart` / `BucketEnd` / `DayOfWeek` |
| `Models/Freq.zan` | 同上 | `Freq` 枚举（重采样频率档位） |
| `DataFrame.zan` | 同上 | 补 `Copy()`（深拷贝，列存不共享）——变换层的基础原语 |

**六条硬取舍**：

1. **不新建 `Zan.Analytics` 包**。上轮规划里曾提过"分析层独立成包"，
   实测后改为扩 `Zan.DataFrame`：这些操作全部以 `DataFrame` 为输入输出，
   没有独立于表的数据模型；再开一个包只会让"分析数据"有两个家。
2. **枚举必须是顶层类型**。`Resample.Freq.Second` 这样的嵌套枚举
   **解析不了**（编译器报 `cannot resolve member`），`Freq` 单独成文件
   与 `Agg`/`ColKind` 放在一起。
3. **窗口结果写进新列，不覆盖输入列**。输入列可能是 Int 型，`PutDouble`
   对 Int 列会截断（窗口均值 16.67 被抹成 16）。新列是 Float 型，
   默认名 `<列名>_rolling` / `<列名>_ewm` / `<列名>_diff`。
4. **"算不出的行"不写 NaN，改为不写值 + 有效性掩码**。Zan 没有 NaN
   字面量（`0.0/0.0` 能造出 NaN，但它穿过格式化与图表链路会渲染成
   平台相关的文本，还会污染下游平均），所以用"不写值 + 掩码"表达。
   `minPeriods` 默认 1
   （窗口不满也出值，用现有的行算），显式给 >1 时不足样本的行不写值，
   要判哪些行真算得出用 `RollingValid` 拿掩码。
5. **重采样的空桶不产生行**（不是补 0）。补 0 会让"这段时间没数据"和
   "这段时间销量为 0"混为一谈——这在时序上最容易出错。桶边界左闭右开
   （与 pandas 一致），周桶起点是周一 00:00（以 Unix 纪元对齐）。
6. **外连接的键从右表补齐**。右表独有的行进结果时，右表键列不进结果
   （与既有 `Join` 一致，避免同列出现两次），但左表的键列要**从右表
   取值**——否则那条行的键是空的，认不出它是什么。

conformance `table_transform`（选列的列序与不改入参 / 缺列抛异常 /
算列与同名列替换 / 去重保留首次 / 拼接的缺列填空 / 宽转长的行内序 /
长转宽的列名拼接 / 转置的轴向；透视表的行列序、缺组合填 0、缺组合不
造列、多值列的列标签优先序；交叉表的计数；内/左/右/外连接的保留行与
键补齐、交叉连接的笛卡尔积、多键连接；滑动窗口的不满窗取值与有效性
掩码、指数加权的归一化与不归一化、差分的首行；重采样的日/周/月桶
边界、空桶不产生行、星期几与已知日期核对）。

**已知边界**：无多级索引（`MultiIndex`）、无缺失值处理（`fillna`/
`dropna`/`interpolate`——列存没有 null 标记位，是另一个量级的改动）、
无 `groupby().agg()` 的自定义聚合字典（`Aggregate` 只认固定的 5 个算子）、
无 `Concat` 的列名对齐重命名、无 `Resample` 的升采样与时间偏移
（`asfreq`/`offset`）、无区间分箱（`GroupedSeries` 只做等值分组）。

---

### 1.20 AI 应用管线：`Zan.Ai`

**新建独立包**（`packages/Zan.Ai`，命名空间 `System.Ai`）。理由：
`Zan.Mvc/Framework/Ai` 那 7 个文件是 Web 框架内的一小块，绑死在
`HttpContext` 与 `Settings` 上——桌面/游戏/CLI 场景用不了。AI 管线
是跨场景的通用能力，该独立成包（依赖方向：`System.Ai → System.Net`，
反过来不可能）。

**缺口（实测）**：Zan 的 AI 侧**零件全有、组装线没有**——

| 环节 | 已有 | 缺 |
|---|---|---|
| 分词 | `Zan.Text/TextTokenizer`（含 CJK 判定） | 文档分块 |
| 全文检索 | `Zan.Text/Bm25Index` | — |
| 向量索引 | `Zan.Data/ZanDb/VectorIndex`（HNSW） | 向量**生产**（embedding 客户端） |
| LLM 调用 | `Zan.Mvc/Ai`（绑死在 Web 框架） | 独立可复用的客户端 |
| 端点解析 | `Zan.Mvc/Ai/AiEndpoint` | 通用版 |

即"文档 → 分块 → 向量化 → 检索 → 拼提示词 → 生成"这条链，
前半段（分块、向量化）没有可复用的实现。

| 文件 | 位置 | 内容 |
|---|---|---|
| `AiClient.zan` | `packages/Zan.Ai/src/System/Ai/` | `AiClient`：非流式/**真流式**（`SseSink` + `TakeCo` 增量投递）补全、重试、超时预算、Token 计数、`lastError` 失败语义 |
| `Embeddings.zan` | 同上 | `Embeddings`：OpenAI 兼容 `/v1/embeddings`、批量、归一化、**按 index 归位**、维度校验 |
| `Chunking.zan` | 同上 | `Chunking`：递归字符分割 / token 窗口 / 句子 / 段落，四种策略 |
| `AiEndpoint.zan` | 同上 | `AiEndpoint`：base URL → scheme/host/port/path；本地端点识别 |
| `VectorStore.zan` | 同上 | `VectorStore`：**内存**向量存储（`AiEmbedding` double → `VectorIndex` float 适配、入库/检索/导出导入往返） |
| `Retriever.zan` | 同上 | `Retriever`：**内存**混合检索（向量 + BM25）+ **RRF 融合**；保留两路原始排名与分数 |
| `ZanDbRetriever.zan` | 同上 | `ZanDbRetriever`：**本地持久化**混合检索——文档与索引存 `ZanDb`（事务 + journal + 检查点），走原生 `Collection.SearchHybrid` |
| `RemoteZanDbRetriever.zan` | 同上 | `RemoteZanDbRetriever`：**远端**混合检索——文档与索引在另一台机器的 `ZanDbServer` 上，客户端走 `ZanDbClient` 的 TCP 协议 |
| `IRetriever.zan` | 同上 | `IRetriever`：检索的**统一契约**（同步 + 协程两版检索、取原文、失败原因） |
| `IIndexWriter.zan` | 同上 | `IIndexWriter`：写入的统一契约（**后端分配 id**，返回 id 列表） |
| `VectorStoreWriter.zan` | 同上 | `VectorStoreWriter`：内存档的写入适配器（把 `VectorStore` 包成 `IIndexWriter`） |
| `IReranker.zan` | 同上 | `IReranker`：**重排**的统一契约（检索之后的第二道精排） |
| `ApiReranker.zan` | 同上 | `ApiReranker`：调 `/v1/rerank` 的 cross-encoder 服务（Jina / Cohere 等） |
| `LlmReranker.zan` | 同上 | `LlmReranker`：用通用大模型逐条打分（行号协议，不需要专用重排模型） |
| `Conversation.zan` | 同上 | `Conversation`：**会话记忆**——多轮对话的历史 + 按轮裁剪 + 落盘往返 |
| `Models/AiMessage.zan` | 同上 | `AiMessage`：一条消息（角色 + 内容）的实体 |
| `Rag.zan` | 同上 | `Rag`：分块→向量化→入库→检索→拼提示词→生成；`RagAnswer` 带引用溯源 |
| `ToolAgent.zan` | 同上 | `ToolAgent`：**工具调用循环**——模型要调工具 → 执行 → 结果按 `tool_call_id` 回传 → 模型接着答；一轮多个工具全执行、上限可配、每步出事件 |
| `ToolRegistry.zan` | 同上 | `ToolRegistry`：工具名 → 实现的**白名单**（没注册的名字一律拒绝执行，模型幻觉出的工具名回错误结果而不是崩） |
| `Models/AiTool*.zan` | `src/System/Ai/Models/` | `AiTool`（声明）/ `AiToolCall`（模型的一次调用）/ `AiToolResult`（执行结果）/ `AiToolTurn`（一轮的解析结果） |
| `Models/ToolEvent.zan` | 同上 | `ToolEvent`：工具调用的**可观测**实体（工具 / 最终答复 / 上限 / 错误） |
| `Models/*.zan` | `src/System/Ai/Models/` | `AiConfig` / `AiUsage` / `AiPricing` / `AiChunk` / `AiEmbedding` / `RetrieverHit` |

**六条硬取舍**：

1. **失败用返回值不用异常**：网络失败是常态（超时、断网、5xx），
   `Complete` 返回 null + `client.lastError` 有可读原因，比抛异常顺手。
2. **4xx 不重试，POST 补全默认不重试**（`maxAttempts` 默认 1）：补全
   没有幂等键，重试可能重复计费。5xx 与网络错误才重试（指数退避，
   上限 2 秒）。
3. **嵌入响应按 `index` 归位，不靠数组顺序**：协议里有 `index` 字段，
   靠顺序是典型的错位 bug 来源（某个网关重排了数组，向量就全对不上
   号）。维度不一致直接报错，**不截断和稀泥**（截断会让相似度算错
   且错得隐蔽）。
4. **token 估算单一口径**：`Chunking.TokenCost`（4 倍刻度：CJK 4、
   ASCII 1、其它 2）与 `AiClient.EstimateTokens` 是同一把尺子。
   两处口径不一致是这类库里最常见的 bug——切出来说"每块 100 token"，
   一算却是 25。**成本是估算**，`AiUsage.estimated` 标出来。
5. **不内置模型价格**：价格会变，内置就是内置一份会过期的谎言。
   `AiPricing` 由调用方传。
6. **`JsonValue.Parse` 对坏 JSON 抛异常**（不是返回 null），所以所有
   解析外部响应的路径都兜了 try/catch——一个畸形响应不该把调用方炸了。

**偏移单位是字节不是字符**：Zan 的 `string` 是 UTF-8 字节序列，
`AiChunk.start/end` 与 `string.Substring` 同口径（字节），切分点必须
落在字符边界上（劈开多字节字符会产出替换字符，块内容就脏了）。

**工具调用：检索是"模型想查数据库"的特例**。`ToolAgent` 把它泛化成
任意工具——查订单、查天气、算账。模型返回「工具名 + 参数」，程序按名
字从 `ToolRegistry` 找回实现执行，把结果按 `tool_call_id` **原样**回传，
模型看着结果接着答。三条硬取舍：

1. **注册表是白名单**：没注册的名字一律拒绝执行，返回错误结果（不崩），
   模型自己会纠正幻觉出的工具名。
2. **工具失败不是对话失败**：执行失败变成一条 `tool` 消息回给模型，
   不是抛异常——模型还有机会换个参数重试。
3. **同步工具不必写成 async**：注册表有两条路径（`Register` 走 async
   委托、`RegisterSync` 走同步委托）。不碰 IO 的工具不该被迫写成
   async——async 委托是 async-only 的，同步实现塞不进去。

**这里踩到一个编译器缺陷**：`await` 的目标分异步（值是子协程帧）与普通
（值就是普通值）两类，判错就是读野指针——普通值往往也是 i8 指针（字符
串就是），与子协程帧同型。async 方法此前一律被当普通值，`await` 一个
async 方法拿到空结果。判定补齐了六种形态（Task/ValueTask 类型、async
方法、async 委托的三种位置、接口方法、类名静态接收者）。详见
`.agents/skills/zan-compiler-internals` 第 42 条。

conformance `ai_pipeline`（端点解析的 host/port/path/本地端点与五类
非法地址：没 scheme、带 userinfo、带 fragment、空 host、非法端口；
token 估算的 CJK/ASCII 口径与两处一致性；四种分块策略的块数、原文
偏移可还原、**无分隔符时按字符边界硬切不产生替换字符**、滑动窗口
的步长 = 窗口 - 重叠；嵌入响应的按 index 归位、归一化后模长为 1、
维度不一致报错、少返回向量报错、服务端错误提取、坏 JSON 不炸；
余弦相似度的正交/同向/维度不等/零向量；用量的 total 与成本核算；
客户端在连不上/地址非法/没模型三种情况下的失败语义；向量存储的
入库/检索/**导出导入往返后检索结果不变**、维度不符报错；**RRF 融合
的精确分数**（两路都命中 = 1/(k+r1) + 1/(k+r2)，只命中一路 = 单项）
与融合顺序的稳定性；占位符替换（全替换而非只替第一个））。

conformance `ai_tools`（工具调用循环：单工具一轮、**一轮多个工具全执行**、
结果按 `tool_call_id` 原样回传、**同步工具**也能注册并执行、工具失败
回给模型而不是抛异常、循环上限生效、不存在的工具回错误结果不崩、
坏参数 JSON 回错误结果；事件流里能看到每一步）。

conformance `async_await_sync_delegate`（`await` 一个**同步委托**拿到的是
委托的返回值，不是子协程帧——判错就是静默读野指针）。

conformance `ai_stream`（**真流式**：`onDelta` 在分片**到达时**就被调用，
不是等整条响应收完再一次性倾泻。判定是**确定性**的：服务端发完第一帧
后**等客户端确认收到**才发第二帧——缓冲式客户端的确认永远到不了，
服务端等超时，用例直接看到"没增量"。另含：完整文本拼接、**回调返回
false → 主动停止**（中止上游、返回已收到的全文，且**主动停止不算失败**，
`lastError` 保持为空）、**最后一帧不以换行收尾**（SSE 允许这样收尾；
客户端只在遇到 `\n` 时才处理行，流结束时缓冲区里剩下的那半行必须
按完整的一行补处理，否则最后一个分片被**静默丢掉**——流正常结束、
内容却少一截，且没有任何错误提示））。

conformance `async_sync_delay`（**非 async 方法里的 `await Task.Delay(ms)`
必须真的等**：编译器原来在"顶层非异步上下文"分支**只查**模块里的 `Sleep`
（Win32 API）/ `poll`（libc 符号）——`LLVMGetNamedFunction` 找不到就返回
NULL，而这两个符号本来就不在当前模块里（链接期才进来），于是整个等待被
**静默跳过**，`await Task.Delay(500)` 编译成一条 ret。实测请求 500ms、
实际耗时 0.0002ms。改法与 `irgen.c` 里 `zan_io_pump_timeout` 一致：
**声明**符号（`LLVMAddFunction`）而不是只查。断言只卡**下界**（等了 >=
阈值），不卡上界：上界在慢机器上会 flaky，而"等没等"是二值事实）。

**检索为什么是混合 + RRF**（§1.20 补记）：

- **混合**：纯向量检索认不出专有名词（"iPhone 15" 与 "iPhone 14"
  的向量几乎一样），纯 BM25 认不出同义表达（"怎么退款" 检索不到
  "退钱流程"）。两者互补。
- **RRF 而不是分数加权**：向量的距离（1 - cos_sim，0~2）与 BM25 的
  分数（无上界）**不在同一量纲**，加权求和必须先归一化，而归一化
  方式（min-max？z-score？按查询内分布？）每种都有坑且对查询敏感。
  RRF 只看**排名**不看分数（`score(d) = Σ 1/(k + rank_i(d))`，k=60
  取自原论文），对量纲完全免疫。两路的原始排名与原始分数都保留在
  `RetrieverHit` 上，要自己加权时可用。

**依赖方向的实测**：`System.Ai → System.Data.ZanDb`（HNSW 索引）+
`System.Text`（BM25）。实测这两个命名空间只带 17 / 37 个编译单元，
**不拖 ORM 与 sqlite 驱动**（那些在 `System.Data` 根与
`System.Data.Orm` 下，本包不碰）——这正是用子命名空间而不是包根
的好处。

**向量库是 ZanDb 支持的**（实测，非推测）：`Collection.EnsureVectorIndex`
（HNSW 建索引）、`SearchVector` / `SearchVectorExact`、
`SearchHybrid`（混合检索）、`RebuildSearchIndexes`（从源文档重建）、
检查点与世代机制——`Zan.Data` 的 README 与 conformance
`zandb_search.zan` 都在维护这套契约。

**并发怎么解（实测结论）**：`VectorIndex.Add` 会改 `chunks`/`nodes`/
`idSlots` 三个共享结构且**没有锁**——并发写会撕裂图结构（不是"结果
不准"，是内存结构损坏）。ZanDb 那侧由 `AsyncRwLock` 串行化写
（`InsertAsync`/`UpdateAsync`/`FindByIdAsync`/`UpdateAtomic`），索引
变更走 **journal + 检查点**，文档写入与索引更新落在同一事务，不会
出现"文档写进去了但索引没更新"。

所以分工是**三档**：

| 档 | 类 | 存储 | 并发写 | 适合 |
|---|---|---|---|---|
| 内存 | `VectorStore` + `Retriever` | 进程内存 | **不支持** | 小规模、单写者、一次性脚本、测试夹具 |
| 本地 | `ZanDbRetriever` | 本地 `ZanDb` 文件 | 支持 | 单机生产 |
| 远端 | `RemoteZanDbRetriever` | 远端 `ZanDbServer` | 支持 | 多机共享一份知识库 |

三档**语义一致**（同一 RRF 公式 k=60，同一个 `RetrieverHit` 实体），
conformance 里有一条断言专门盯这个——三个后端对同一批数据给出**完全
相同的融合分数**（0.032787），所以从内存切到本地、再切到远端，检索
结果不会悄悄变样。

**远端写入的 request id 语义**（服务端按 id 绑定请求指纹）：一个 id
只能配一个请求——同一个 id 配不同请求体会被判 `id_conflict`（不是
静默覆盖，也不是当成两次独立写入）；同一个请求重试要用**同一个**
id，这才是幂等。实测踩过：建文本索引与向量索引共用一个 id，第二次
直接被拒 `id_conflict`，连带向量索引没建成、检索报"search index does
not exist"。所以 `EnsureIndexesAsync` 内部给两个 ensure 派生两个 id。

**`double` → `float` 的精度代价（实测）**：单精度有效位约 7 位十进制。
embedding 分量（量级 0.01~1）相对误差约 1e-8，余弦相似度偏差约
1e-9——对检索排序**没有可观察的影响**。但需要与服务端原始向量逐位
一致时（跨系统精确比对）**不能经过这层**，直接用 `AiEmbedding.vectors`。

**编排层与存储后端解耦**（补记）：`Rag` 原来只认内存档的 `Retriever`
（签名写死），接本地/远端档就得绕开编排层自己拼提示词——那等于没有
编排层。现在抽出两个契约：

- `IRetriever`：**同步 + 协程两版**检索、取原文、失败原因。为什么两
  版：本地档（文件 + 内存索引）没有 IO 等待，同步更直白；远端档**必须**
  走协程（阻塞会停掉 worker 的事件循环，把并发限制在 worker 数量上）。
- `IIndexWriter`：批量写入，**id 由后端分配**。数据库档的 id 是服务端
  在事务内发的（`Collection.NextId`）；让调用方给 id 就得"先查最大 id
  再 +1"——那是竞态（两个进程同时写会撞 id）。

远端档的**同步接口明确报错**（`Search` / `TextOf` 抛异常），与
`Zan.Data` 的协程化 MySQL 驱动同款——静默阻塞会伪装成"能用"，实际把
并发能力废掉了。

conformance `ai_rag` 在本进程内起**真的 HTTP 服务**当假 AI 端点
（embedding + 补全）与**真的 TCP 服务**当远端 ZanDb，跑同一个 `Rag`
管线接三个后端，断言三者的**引用原文、顺序、分数、上下文逐项相等**。

**重排层**（补记）：双路检索解决的是**召回**，RRF 只看排名不看分数，
所以捞进来的 top-k 里常混着"词面沾边但语义不对"的块。重排用更强的
模型给"查询-文档对"逐对打分，精度显著高于召回，但慢且贵。

所以形态是**宽召回 + 窄精排**：`Rag` 召回 `recallK` 条（默认 topK 的
下界 20），重排后取 topK 进提示词。`Rag.SetReranker(reranker, recallK)`
装上。

三个设计决定：

- **接口放在这一层**（`IReranker`）：重排有三种实现——重排 API（跨编码
  器托管在服务商）、本地 cross-encoder、LLM 打分。输入输出形态一致
  （查询 + 候选文本 → 分数），上层不关心用的是哪种。本包实现了前两种
  的接口形态（API 版 + LLM 版），**本地模型的绑定留给调用方**。
- **重排失败要降级，不能拖垮问答**：重排是**增强**不是必需。失败时用
  未重排的候选继续，失败原因记在 `Rag.rerankError`（与 `lastError` 分
  开——一个是"问答失败了"，一个是"问答成功但没重排"）。
- **分数不归一化、不写进 `RetrieverHit.score`**：重排分数的量纲由服务
  决定（0~1 或无上界 logit），与 RRF 分数不同量纲，混在一起下游没法
  比。重排的相对次序由**顺序**表达；`RetrieverHit.score` 仍是 RRF 分
  数（表示"被召回时的强度"）。

`LlmReranker` 用**行号协议**而不是 JSON：模型吐 JSON 的失败率不低
（多一个引号就废一次调用），行号协议容忍格式噪声。解析**容错**——只
认能解析出的行，认不出的**跳过**（不猜、不硬套）；少几条候选参与重排
比整批报废强。

conformance `ai_rerank` 在本进程内起**真的 HTTP 服务**当假重排/假 LLM
端点，验：按 `index` 归位（不靠数组顺序）、分批时下标映射回全局、
LLM 行号协议的容错解析、**重排真的换了顺序**（配一条"不装重排器"的
对照用例证明顺序变化是重排造成的）、**重排失败降级但问答仍成功**。

**会话记忆**（补记）：`Rag.Ask` 原来是无状态的一问一答，多轮对话要每
轮重发全部历史——历史放不下就得裁，裁哪几条又得想清楚。新增
`Conversation` 管这件事，`Rag.AskIn(question, convo)` 带上历史。

三个设计决定：

- **按**轮**裁，不按**条**裁**：一轮 = 一条用户消息 + 一条助手消息（成
  对）。裁到一半（一半提问没回答）上下文就是断的——模型看到"我上一轮问
  了什么"却没有"我上一轮得到什么"，容易重复提问、答非所问。超预算整
  轮丢，从最老的开始。
- **当前轮不裁 + 至少留最新一轮**：最后一条还没助手回答的用户消息（正
  在进行的那一轮）必须保留——裁了就丢了当前提问。预算小到装不下一轮
  时宁可超预算也不把历史清空（有历史总比没历史好，调用方该调大预算）。
- **当前指令不参与裁剪**：系统提示（人设、规则、格式要求）不是"历史"，
  它是**当前指令**——裁掉了模型就变了个人格、忘了规矩。裁剪只动系统提
  示之后的部分。

另外两条边界：**历史不进检索**（`AskIn` 的检索只用当前问题——历史里的
旧问题对当前问题没有帮助，混进去会拉偏召回；历史只进提示词）；**失败
时不写历史**（写进去会让下一轮带着一条失败的问题，模型以为自己问过而
没得到答案）。

顺带把消息的表示从 `List<string[]>` 换成实体 `AiMessage`——消息有两个
字段（角色 + 内容），用平行数组装就是"靠下标对齐"，而且**长度不受检
查**（写成三个元素的数组编译器不报错，运行时读到越界才炸）。角色用字
符串不用枚举：OpenAI 协议的角色是**开放**的（`system`/`user`/
`assistant`/`tool`/各家自定义），枚举会锁死协议。

conformance `ai_memory` 在本进程内起**真的 HTTP 服务**当假 AI 端点，验：
按轮裁剪（超预算整轮丢、留的是**最新**那轮）、当前轮不裁、预算为 0 表
示不限制、系统提示不参与裁剪、落盘往返、坏 JSON 给可读原因、`AskIn`
把历史带进提示词（消息条数 1 → 3）但**不进检索**、失败时不写历史。

**已知边界**：远端档每次操作一次网络往返（`ZanDbClient` 每次
`SendAsync` 新建连接，**无连接池化、无批量检索**——重排要逐条取候选
文本，这段是主要开销）、重排层**没有本地 cross-encoder 的绑定**（接口
留好了位置）、会话记忆是**进程内**的（没有内置落盘/多端同步，`ToJson`/
`FromJson` 给了钩子，存哪儿调用方自己定）、无评估与
可观测、无流式的
**背压**（回调返回 false 只能停在这一帧之后）、无 Token 精确计数
（要接厂商 tokenizer）、Embeddings 只支持 OpenAI 兼容协议（不做各家
私有协议）、`Rag.Index` 与 `Rag.Ask` 都要求**在线调 embedding 服务**
（没有本地离线 embedding 模型）。

### 1.21 缺失值处理：`Zan.DataFrame`

**缺口**：`Zan.DataFrame` 的列存是强类型的 `List<long>` /
`List<double>` / `List<string>`——格子里**总有值**，所以"这行没填"与
"这行填了 0"**分不开**。这不是少一个函数的问题，是列存缺一个表达
"没有值"的位置。

**取舍：掩码位，不是哨兵值**。`0` 与 `""` 都是合法值，拿 `0` 当缺失
的哨兵，会把"这行没填"和"这行填了 0"混为一谈——而这两者的处理方
式常常相反（前者该 drop，后者该保留）。pandas 的 `NaN` 之所以好使，
是因为 double 有专门的 NaN 位；Zan 的列存是强类型的，没有 NaN 语义，
所以用**独立的布尔掩码**（`DataCol.missing`，与值列表等长）表达
"缺失"这件事本身。掩码为空 = 该列无缺失（常见情形，不占额外内存）。

| 文件 | 内容 |
|---|---|
| `Models/DataCol.zan` | `missing` 掩码 + `IsMissing`/`MarkMissing`/`ClearMissing`/`MissingCount`；`Append`/`PutStr` 与掩码同步（**写值清标记，空串置标记**） |
| `DataFrame.zan` | `InferKind` 空字段不参与类型推断（否则 `1,,3` 被推成 Str）；`AddRow`/`AddColumn` 的新格是缺失不是 0；`Aggregate` **跳过缺失**；`CastColumn`（转列类型，缺失标记原样保留）；`ToCsv` 缺失格写空字段 |
| `Missing.zan` | `IsNaN`/`NotNaN`/`Mask`/`ValidMask`/`NaNCountInRow`/`NaNCount`/`ValidCount`/`TotalNaNCount`；`DropNa`（any/all/subset）；`FillNa`（常量）/ `FillNaByColumn`（逐列）/ `FillNaStat`（均值/中位数） |

**CSV 约定**：空字段 → Int/Float 列的**缺失**（不是 0）；Str 列的空
字段仍是空串（Str 列没有独立的缺失态，这是类型系统的直接后果）；整列
全空 → 推断为 Str。

**聚合语义**：缺失**不参与**聚合（求和/均值/最值跳过，而不是当 0）。
全列无有效值时返回 0.0（沿用本包"空集返回 0"的约定）而不是 NaN——
NaN 会顺着下游的平均再平均，污染出一片垃圾。要区分"没有有效值"与
"有效值恰好是 0"，用 `ValidCount`。

**升型而不是截断**：往 Int 列填均值 1.5 会截成 1，**静默算错**。
`FillNaStat` 遇到带小数的填充值先把列**升成 Float**（`CastColumn`），
再填。同一坑在上一批的窗口均值上就踩过一次（`PutDouble` 往 Int 列
写会 `(long)v`）——这次把它变成类型系统层面的显式升型。

conformance `missing_values`（66 条断言）：CSV 空字段 → 缺失、空字段
不参与类型推断、整列全空 → Str；**填 0 与缺失必须可区分**（`0`/`""`/`5`
三格的求和是 5 不是 10，均值是 2.5 不是 1.67）；聚合跳过缺失（和=3、
均值=1.5、Count 仍按行数）；`DropNa` 的 any/all/subset 与非法 how/列名
报错；`FillNa` 常量/逐列/均值/中位数、Int 列自动升 Float、全列无有效
值时不填；写入即"有值"（写值清标记、空串置标记）；CSV 往返保持缺失；
`AddRow`/`AddColumn` 的新格是缺失；掩码随 `Filter`/`Copy` 传播。

### 1.21 进程内消息总线：`Zan.Messaging`

新包 `packages/Zan.Messaging`，命名空间 `System.Messaging`。补 MassTransit（7,809★）
那一档的**进程内**那一半：把"这里发生了一件事"和"谁负责处理它"解耦。原 §2 记的
"进程内事件只有 `Zan.Game` 的 `EventBus`（弱引用、无类型分发）"确实是空白。

| 文件 | 类型 | 内容 |
|---|---|---|
| `MessageBus.zan` | `MessageBus` | 总线：登记通道 / 广播 / 命令 / 中间件链 / 错误路由 + 计数 |
| `MessageChannel.zan` | `MessageChannelBase` / `MessageChannel<T>` | 通道：订阅 / 退订 / 广播 / 命令 + 运行期类型判定（构造器定型成委托） |
| `SubscriptionList.zan` | `SubscriptionList<T>` | 订阅表：订阅 / 退订 / 计数 / 广播（单独成类是为了闭包不捕获 `this` 成环） |
| `QueryEndpoint.zan` | `QueryEndpointBase` / `QueryEndpoint<T, TResult>` | 请求-响应端点：处理函数 + 结果类型完整保留（值类型不丢） |
| `MessageMiddleware.zan` | `MessageMiddleware` | 中间件委托：`(object message, Action next)`，必须调 `next` |
| `MessageDelivery.zan` | `MessageDelivery` | 一次投递的执行体（消息 + 通道表 + 计数） |
| `MessageError.zan` / `MessageException.zan` | `MessageError` / `MessageException` | 可区分错误码（`NoHandler` / `TooManyHandlers` / `HandlerFailed` / `InvalidHandler` / `InvalidMessage`）+ 原异常保留 |
| `MessageHolder.zan` / `ValueHolder.zan` | `MessageHolder` / `ValueHolder<T>` | 闭包捕获用的盒子（`object` 形参与泛型形参都不能直接捕获） |

**六条硬取舍**：

1. **通道由使用者建，不由总线按类型自动建**。Zan 没有运行期类型名，总线判断
   "这个类型是不是已经建过通道"只能靠运行期类型试探，会把同类型消息投两遍。
   一个类型一个通道、通道由使用者持有，问题就不存在了。
2. **类型判定在通道的构造器里定型成委托**。**Zan 里泛型类的虚方法体拿不到
   类型参数**——`message is T` 经基类虚调用恒为 false（实测，见下"编译器缺陷
   记录"）。所以 `MessageChannel<T>` 的构造器把判定与投递定型成三个委托
   （`accepts` / `dispatch` / `handlerCount`），基类只调用委托、不碰类型信息。
3. **消息必须是引用类型（类）**。总线按运行期类型分发，值类型没有运行期类型
   可言；更根本的是 Zan 的 `object` 目前装不下值类型（显式装箱直接崩进程）。
4. **订阅者异常默认原样抛出（不吞）**。总线里的异常多半是处理逻辑的 bug，吞掉
   会让问题变成"消息莫名其妙没到"。登记 `OnError` 之后才改走错误处理器，异常
   被包装成 `MessageException(HandlerFailed)`，原异常在 `InnerException`。
5. **中间件必须调用 `next`**，不调用就是"吞掉这条消息"；中间件链包裹的是
   **一次投递**（一次 `Publish` / `Send` / 查询跑一遍链，不是每个订阅者一遍）。
6. **查询端点由使用者持有，结果类型完整保留**。把结果擦成 `object` 再转回来
   这条路在 Zan 里不通（装箱缺陷），端点是泛型类、处理函数是
   `FuncT<T, TResult>`，结果一路都是具体类型。

conformance `messaging_bus`（类型分发 / 广播顺序 / 退订 / 命令语义 / 中间件顺序
与吞消息 / 错误路由 / 查询端点值类型结果 / 参数校验）。ARC 守卫干净，
`--check-leaks` 无泄漏。

**已知边界**：没有跨进程传输；消息与（接总线时的）查询必须是引用类型；没有
重试 / 死信 / 延迟投递 / 出站箱（持久化语义归 `Zan.Jobs`）；没有订阅者优先级；
非线程安全。

---

### 1.22 对象映射：`Zan.Mapping`

新包 `packages/Zan.Mapping`，命名空间 `System.Mapping`。补 AutoMapper（10,188★）/
Mapster（5,182★）那一档，但**只做显式映射**：运行期逐成员规则 + 生成期按约定
配对生成源码，**不做反射式自动映射**。

| 文件 | 类型 | 内容 |
|---|---|---|
| `MapProfile.zan` | `MapProfile<TSource, TTarget>` | 门面：目标工厂（必填）+ 链式 `Member` + `Map` / `TryMap` / `MapList` / `TryMapList` + 规则计数与名字 |
| `MapRule.zan` | `MapRule<TSource, TTarget>` | 一条成员规则：`Name` + `Apply`（`(源, 目标) → 新目标`） |
| `ValueConverter.zan` | `ValueConverter<TSource, TTarget>` | 值转换器：`Apply`（失败抛）/ `TryConvert`（不抛） |
| `MappingSpec.zan` | `MappingSpec` / `MappingMember` | 生成规格：源/目标类型、类名、命名空间、成员对（源路径 → 目标成员名） |
| `GenMapping.zan` | `GenMapping` | 生成器：`Emit` / `TryEmit` / `TryEmitFromEntities` / `MatchByConvention` / `UnmatchedByConvention` |
| `MappingError.zan` / `MappingException.zan` / `MappingResult.zan` | — | 可区分错误码 + 异常 + 不抛的结果对象 |

**六条硬取舍**：

1. **不做运行期反射式自动映射**。AutoMapper 的"按约定自动映射"靠运行期反射，
   而 Zan 没有反射（没有 `typeof`、没有成员枚举）——自动匹配只能放在**生成期**：
   读 `zanc --gen-meta` 导出的实体元数据，按约定配对成员，生成**普通 Zan 源码**。
   生成物是编译期常量：没有反射、没有装箱、没有运行期开销，"约定悄悄变了"也不
   会发生（生成物进版本库，diff 得出来）。
2. **运行期与生成期是两条互补的路**。要"每条规则单独写、单独报错"的复杂映射用
   `MapProfile`；要"几十个 DTO 一次性对齐"的机械映射用 `GenMapping` 生成代码后
   直接用。两条路产出同样的显式代码。
3. **规则交还新值而不是就地改**。`(源, 目标) → 新目标` 让类目标与**结构体目标**
   都对——结构体是值语义，就地改会改在副本上（改完丢掉）。
4. **目标工厂必填**。Zan 不能 `new T()`（类型参数实例化得到 null，静默错值），
   目标对象由调用方造，"用哪个构造器"也是调用方的事。
5. **错误可区分**。错误码区分"源是 null"/"成员规则抛了"/"值转换失败"/"工厂没给"/
   "档案没有规则"，并带**成员名**；两条出口——`Map` 抛 `MappingException`，
   `TryMap` 返回 `MappingResult`。
6. **约定只覆盖能确定的那部分，配不上的如实报出**。同名同类型直拷；扁平化
   （`customer.name` → `customerName`）。类型对不上、需要显式转换的**不猜、不静默
   跳过**——`UnmatchedByConvention` 把它们列出来，由人决定。

conformance `mapping_profile`（链式注册 / 成员名列表 / 类目标 / 列表映射 /
源为 null / 成员规则失败归因 / 抛与不抛两条路 / 值转换器 / 结构体目标）与
`mapping_gen`（两条约定 / 配不上的报出 / 生成源码形状 / 目标没有无参构造器
时报错 / 非法规格抛 `InvalidSpec`）。ARC 守卫干净，`--check-leaks` 无泄漏。

**已知边界**：没有运行期反射式自动映射；运行期拿不到类型名（配对靠生成期
元数据）；生成器按**名字**配对，同名类型族会让配对挑花眼；结构体目标要求规则
交还新值；档案非线程安全；没有嵌套映射 / 集合映射 / 循环引用检测。

### 1.23 后台任务：`Zan.Jobs`

新包 `packages/Zan.Jobs`，命名空间 `System.Jobs`。补 Hangfire（10,151★）/
Quartz.NET（7,096★）那一档的**后台任务核心子集**：持久化任务存储 + 显式处理器
注册 + 重试退避 + 延迟/周期调度 + 死信与人工重排 + 状态统计。

| 文件 | 类型 | 内容 |
|---|---|---|
| `JobScheduler.zan` | `JobScheduler` | 门面：注册 / 入队 / 延迟 / 周期 / 起 worker / 状态统计 / 死信重排 / 删除 |
| `JobQueue.zan` | `JobQueue` | 生产者侧：`Enqueue` / `EnqueueWithId`（幂等键）/ `Schedule` / `EnqueueRecurring` / `NextId` |
| `JobWorker.zan` | `JobWorker` | 消费者侧：`Start` / `Stop` / `ProcessOnce`（同步跑一轮）/ `ProcessJob` |
| `JobRegistry.zan` | `JobRegistry` | 任务名 → 处理器的显式映射；`Register` / `RegisterTyped<TArgs>` / `Find` / `Names` |
| `JobHandler.zan` | `JobHandler` | 一个处理器：`Name` + `Run`（JSON 文本 → `JobResult`）+ `Normalize` |
| `JobStorage.zan` | `JobStorage` | 存储抽象契约（基类 + 虚方法）：`Append` / `Snapshot` / `Find` / `Filter` / `CountByState` / `Compact` |
| `FileJobStorage.zan` | `FileJobStorage` | 追加式事件日志 + 启动重放 + 压实（临时文件 + 原子替换）+ 跨进程文件锁 |
| `InMemoryJobStorage.zan` | `InMemoryJobStorage` | 只留当前状态的内存实现 |
| `RetryPolicy.zan` | `RetryPolicy` | 重试退避：总尝试次数 / 基础延迟 / 指数系数 / 延迟上限 / 抖动；`None` / `Default` / `Fixed` |
| `JobResult.zan` | `JobResult` | 处理器返回值：`Success` / `Retry`（可重试失败）/ `Fail`（不可重试失败） |
| `JobRecord.zan` | `JobRecord` | 任务记录（id / 名字 / 状态 / 参数 / 时间戳 / 重试次数 / 错误 / worker / 周期间隔）+ JSON 往返 |
| `JobState.zan` | `JobState` / `JobStates` | 状态机与状态名（`Enqueued` / `Scheduled` / `Processing` / `Succeeded` / `Failed` / `Dead` / `Deleted`） |
| `JobStats.zan` | `JobStats` | 状态计数快照 + 一行摘要 |
| `JobError.zan` / `JobException.zan` | — | 可区分错误码（未注册 / id 冲突 / 存储失败 / 已在运行 / 找不到 / 非法结果 …）+ 异常 |

**六条硬取舍**：

1. **显式注册，不做反射式派发**。Zan 没有运行期反射，也没有"从字符串构造对象"
   的能力——持久化下来的任务名要在重启后找回处理代码，唯一办法是**启动时显式
   注册**。接线错误因此在启动时暴露（重复注册 → `DuplicateHandler`），而不是等
   某个任务半夜静默失败。强类型参数走**泛型 + 显式序列化委托**（`RegisterTyped`）：
   没有反射也能同时拿到类型安全与可持久化，代价是三个委托都得显式给。
2. **处理器返回结果，不靠抛异常表达重试意图**。抛异常只能表达"失败了"，而后台
   任务还要回答"还要不要重试"——网络抖动重试有意义，参数非法重试一万次也没意义。
   所以 `JobResult.Success` / `Retry` / `Fail` 由处理器自己说清楚；处理器**真的抛了
   异常**时按"可重试"处理，且 worker 不会因此死掉。
3. **存储用追加日志，不是整表重写**。整表重写是每次变更 O(n)，追加只写一行，
   崩溃时最多丢最后一行；启动重放一遍折叠成当前状态。代价是文件会变长，所以
   提供 `Compact()`（写临时文件再原子替换）。
4. **核心逻辑是同步方法**。worker 的核心是同步的 `ProcessOnce()`，后台线程只是
   它的薄包装——后台线程的执行时刻不确定，判定器没法稳定复现"重试了几次"
   "退避多久"；同步入口让测试能精确驱动，也让你能"我就是想同步跑一遍"。
5. **入队时校验任务名**。没注册的处理器在入队那一刻就抛 `UnknownJob`，而不是
   让任务在队列里躺到被 worker 捡起来才变成死信。
6. **状态机分得清"暂时没跑"和"永远跑不动"**。`Failed`（还会重试）与 `Dead`
   （死信，要人工介入）是两个状态——告警因此能分级。`RetryDead(id)` 让人工介入
   之后重排；幂等键 `EnqueueWithId` 在 id 重复时**抛 `DuplicateId`**（静默跳过
   会让调用方以为提交成功，其实任务被丢了）。

conformance `jobs_scheduler`（38 条断言）：成功路径 / 未注册任务名在入队时拒绝 /
幂等 id 冲突 / 可重试失败走 Failed→退避→重试→超上限 Dead / 不可重试直接 Dead /
处理器抛异常按可重试处理 / 延迟任务不到点不跑 / 周期任务自动续命 / 死信人工重排 /
删除 / 状态统计 / 指数退避数值与上限 / 强类型注册往返 / 持久化重启后继续处理 /
压实后状态不丢。

**已知边界**：没有 cron 表达式语言（周期用固定间隔，要 cron 就用
`System.Hosting.Cron` 算下次时间再调 `Schedule`）；没有集群调度 / 分布式锁 /
leader 选举；没有内置仪表板；参数是 JSON 文本（强类型靠显式委托）；周期任务
每次执行一条独立记录（历史完整但记录会变多）；文件存储是**单写者**（跨进程拿
不到锁直接报 `StorageFailure`，多实例部署要各给一份存储路径或换数据库实现）；
存储层全量扫描，任务量到万级需要换实现。

### 1.24 基准测试：`Zan.Bench`

新包 `packages/Zan.Bench`，命名空间 `System.Bench`。补 BenchmarkDotNet
（11,512★）那一档的**时间基准核心子集**：预热 + 重复测量（单调时钟）+ 中位数
主导的统计 + 参数化用例 + 文本/JSON 报告 + 基线对比。

| 文件 | 类型 | 内容 |
|---|---|---|
| `BenchSuite.zan` | `BenchSuite` | 门面：注册用例 / 跑 / 出文本报告 / 出 JSON；重名注册抛 `DuplicateCase` |
| `BenchRunner.zan` | `BenchRunner` | 执行器：预热 → 测量循环 → 统计；`TicksToNs` / `TicksToMs`（先除后乘避免溢出） |
| `BenchOptions.zan` | `BenchOptions` | 四个旋钮：预热轮数 / 轮数下限 / 目标时长 / 轮数上限；`Default()` / `Quick()` / `Copy()` |
| `BenchCase.zan` | `BenchCase` | 无参用例：名字 + `Action` |
| `BenchCaseT.zan` | `BenchCaseT<TArgs>` | 参数化用例：名字 + `ActionT<TArgs>` + 参数（泛型保类型，零装箱） |
| `BenchResult.zan` | `BenchResult` | 测量结果：轮数 / 样本 / 总耗时 / 均值 / 中位数 / 最小 / 最大 / 标准差 / ops/s / 相对波动 |
| `BenchStats.zan` | `BenchStats` | 统计原语：插入排序 / 中位数 / 均值 / 最小最大 / 样本标准差 / 平方根 |
| `BenchReport.zan` | `BenchReport` | 文本表格（按列宽对齐）+ JSON 报告 + 字符串转义 |
| `BenchBaseline.zan` | `BenchBaseline` / `BenchDelta` | 基线解析与对比：`faster` / `slower` / `same` / `new` + 变化百分比 + 噪声阈值 |
| `BenchError.zan` / `BenchException.zan` | — | 可区分错误码（用例非法 / 名字重复 / 基线损坏 / 基线缺项 / 选项非法 / 被测体抛异常）+ 异常 |

**七条硬取舍**：

1. **中位数是主指标，均值只作参考**。基准最常见的失真不是"测不准"，而是被偶发
   尖峰带偏——一次调度抖动就能把均值抬一大截。中位数对这类噪声不敏感，所以
   基线比的是中位数。
2. **预热是显式的一步**，且**预热轮次不计入统计**（`iterations` 是测量轮数，
   `samples` 里只有测量样本）。冷启动开销不该被算成"这个操作很慢"。
3. **三个旋钮一起给**：轮数下限（样本够）、目标时长（样本稳）、轮数上限（不过热）。
   测量循环跑到"满足下限且达到目标时长"就停，但永不超过上限。判定器与 CI 用
   `BenchOptions.Quick()`（固定 3 轮），轮数确定可复现。
4. **委托调用开销不掩饰**。`BenchCase` 的被测体是 `Action`，每次测量都过一次
   委托调用（几十纳秒量级），对极快的操作会主导结果。源码与 README 都写明这层
   开销；要测更细的东西就把操作内联进循环体手写计时。
5. **参数化用例用泛型保类型，不用 `object`**。Zan 没有装箱，`object` 装不下
   异构输入；`BenchCaseT<TArgs>` 把参数类型留在泛型里（调用点零开销），套件层面
   再擦除成无参委托（参数在注册时固定，是基准的常态）。**注意 Zan 不允许同名
   泛型与非泛型类型共存**（`BenchCase` + `BenchCase<TArgs>` 会报
   `duplicate type declaration`），所以泛型版另起名 `BenchCaseT`。
6. **同名用例注册直接抛错**，不静默覆盖——静默覆盖的后果是"以为测的是 A、
   其实测的是 B"，基准报告里几乎发现不了。
7. **基线对比带噪声阈值**（默认 ±3% 判 `same`）。把"变慢了 2%"报成回归，只会
   让人养成忽略告警的习惯；告警一旦不可信就等于没有告警。

conformance `bench_suite`（74 条断言）：统计原语（中位数奇偶取值 / 均值 / 最小
最大 / 有序副本不改原数组 / 标准差 / 平方根 / 空样本）· 选项口径（默认值 /
`Quick` 固定轮数 / `Copy` 不改原件）· 测量循环语义（轮数确定 / 预热不计入统计 /
样本数 / 被测体调用次数 / 中位数落在最快与最慢之间 / ops/s 为正）· 参数化用例
（拿到参数、保留参数类型）· 套件（两类用例共存 / 保持注册顺序）· 文本报告
（表头 / 用例名 / 行数）· JSON 报告（结构 / 名字 / 转义）· 基线往返（生成的
JSON 能被自己解析回来）· 基线对比（变快 / 变慢 / 噪声 / 新用例 / 自定义阈值）·
错误可区分（重名 / 空名 / null 用例 / 基线损坏 / 空基线 / 基线缺 benchmarks
数组）。判定器**只断言结构与关系，不断言具体耗时**——耗时依赖机器，绝对值不是
契约。

**已知边界**：**不提供分配量统计**（BenchmarkDotNet 的 `Allocated` 列缺席）——
托管分配的唯一入口是编译器生成的 `zan_rt_alloc`，运行时的 malloc wrap 只在
`--fast-alloc` 程序里链接、且 stdlib 完全没暴露 `zan_mem_get_stats`，当前语言与
链接策略做不到，根因与两个方向登记在 `TASKS.md` 的 **A464**；本包只回答"多快"，
不回答"占了多少"。此外不做进程隔离（用例之间会互相预热）、没有 GC 相关统计
（Zan 是 ARC，**没有 GC**）、不做统计显著性检验、文本报告的浮点格式走
`Convert.ToString`（机器比对请用 JSON）。

### 1.25 条码与二维码：`Zan.Barcode`

新包 `packages/Zan.Barcode`，命名空间 `System.Barcode`。补 ZXing.Net 那一档的
**二维码编解码**：编码器从 `Zan.Gui` 迁入（并扩成完整模型 1-40），**新增解码
器**——能从图片里把码读回来，这是路线图原文「补全 Gui 侧扫码」的缺口。

| 文件 | 类型 | 内容 |
|---|---|---|
| `BarcodeScanner.zan` | `BarcodeScanner` | 门面：`Scan` / `TryScan`；零配置，给图就行 |
| `BarcodeResult.zan` | `BarcodeResult` | 结果：文本 + 原始字节 + 版本 + 边长 + 级别 + 掩码 + 模块尺寸 + 四角坐标 |
| `BarcodeImage.zan` | `BarcodeImage` | 输入：宽高 + 亮度平面；`FromLuminance`（主）/ `FromArgb`（便利） |
| `BarcodeError.zan` / `BarcodeException.zan` | — | 可区分错误码（无效图 / 没找到 / 码坏了 / 结构不合法 / 模式不支持 / 超长 / 内部）+ 异常 |
| `QrEncoder.zan` | `QrEncoder` | 编码器：分段（numeric/alnum/byte UTF-8）→ 自动选版本 → 交织 → 八掩码选优 → 矩阵 |
| `QrDecoder.zan` | `QrDecoder` | 解码器：二值化 → 定位 → 变换 → 采样 → 格式信息 → 去掩码 → 反交织 → RS → 分段 |
| `QrSpec.zan` | `QrSpec` | ISO 表：每块纠错码字 / 分块数 / 容量 / 对齐位置 + GF(256) + 功能图形地图 + RS 生成多项式 |
| `QrMatrix.zan` / `QrSegment.zan` / `QrEcl.zan` | — | 模型：模块矩阵（行主序 `bool[]`）/ 编码分段 / 纠错级别 |
| `QrFormat.zan` | `QrFormat` | 格式信息 BCH(15,5)：编解码 + 从矩阵读出（两份副本取纠错少者） |
| `QrRsDecoder.zan` | `QrRsDecoder` | RS 解码：扩展欧几里得（Sugiyama）求 Λ/Ω + Chien 定位 + Forney 求幅 |
| `QrBinarizer.zan` | `QrBinarizer` | Otsu 全局 / 8x8 局部自适应 / 3x3 多数滤波去噪 |
| `QrFinder.zan` | `QrFinder` / `QrFinderPattern` / `QrScanHit` | 定位图形检测（1:1:3:1:1 扫线 + 竖直交叉验证 + 聚类取中位数）+ 校正图形 5x5 模板匹配 |
| `QrTransform.zan` | `QrTransform` | 模块坐标 → 图像像素：3 点仿射 / 4 点投影（高斯消元） |
| `QrBitReader.zan` | `QrBitReader` | 位流读取（分段解析用） |
| `QrRenderer.zan` | `QrRenderer` | 渲染：模块矩阵 → ARGB 像素（纯算术，不出文件） |

**八条硬取舍**：

1. **编码器只有一份**。实现从 `Zan.Gui` 迁到本包，Gui 侧 `Gui.Widget.QrEncoder`
   改成薄适配（显式级别映射 + 模块位图直搬），`Gui.Widget.QrMatrix` /
   `QrEcl` 不动。两份编码器迟早分叉，症状是"同样内容编出不同码"，极难排查。
2. **定位中心按簇取中位数，不取单次命中**。斜切方块的弦**不关于垂足对称**，
   3-bar 中点随扫描行线性漂移（速率 tanθ）——15° 时行 39→55 中点从 78.5 漂到
   74.5，取单次命中会偏半个模块以上，采样全错。命中带关于中心行对称，取
   中位数正好落在中心（中位数而非平均值：簇里会混进数据区的假命中）。
3. **聚类键用"横竖配对点"**。左上与左下两个定位图形的 x 完全相同（相隔十几
   个模块），只按 x 聚类会把它们并成一簇。
4. **旋转因子显式修正**。图上量到的模块尺寸是"视在"值（斜切时弦长放大
   1/max(|cosθ|,|sinθ|) 倍），真实边长 s = m·max(|cosθ|,|sinθ|)；θ 由定位三角形
   的斜边方向给出。不修正则 45° 的码被算成 1.41 倍大，版本直接判错。
5. **校正图形在符号坐标系里匹配**。轴对齐模板在旋转图上对不上；改用仿射把
   候选位置投到图上、按模块中心采样，模板跟着码面转。25 格全中才算数。
6. **RS 解码用扩展欧几里得，不用 BM**。一步同时得 Λ 与 Ω，公式链更短。三个
   坑写进注释：Ω 用 `u_k(0)` 归一化（不是 `r_k(0)`）、Forney 要乘 X（不是
   X⁻¹）、特征 2 下负号等于正号。
7. **预处理是策略不是单一路径**：全局 Otsu → 全局+去噪 → 局部自适应 →
   局部+去噪。3x3 多数滤波把椒盐噪声的孤立像素抹掉（噪声会把游程劈开，
   1:1:3:1:1 当场就废），但模块只有 2 像素宽时滤波会抹平整张图，所以是兜底
   不是默认。
8. **"没码"与"码坏了"分开报**。`NotFound` 是采集问题（该重扫），
   `CorrectionFailed` 是图像/码本身的问题（该提示换图）。合成一句话会让
   调用方没法分支。

conformance `barcode_qr`（42 条断言）：编码指纹（固定输入 → 固定位图）· 往返
（ASCII / 数字 / URL / UTF-8 中文 / 单字符，文本+版本+边长+级别四项一致）·
旋转 0/15/45/90/180/270 · 缩放 2..6 像素 · 椒盐噪声 2%/5% · 遮挡一块靠 RS
还原 · 错误码可区分（损坏→CorrectionFailed / 空白→NotFound / 空图→
InvalidImage / 超长→TooLong / v40-L Byte 容量边界 2953 卡准）· TryScan 语义。
编码器与 Gui 原实现逐模块比对过（9 组文本 × 级别，含中文与 URL），搬迁没改
任何一个模块。

**已知边界**：**不做一维条码**（Code128 / Code39 / EAN 不在范围内）· 不做多码制
仲裁 · 不做摄像头取流与 UI · 不出 PNG/SVG 文件（只产像素，文件格式归
`Zan.IO` / GUI 层）· 不支持 Kanji / 结构化追加 / ECI 自定义字符集（解出抛
`UnsupportedMode`）· 透视矫正依赖校正图形，版本 1 只能退回仿射 · 解码是全图
扫描，无"大概在哪"的先验 · 一图多码只返回先解成功的那个。

**编译器缺口**：本包把 `QrFinderPattern` / `QrScanHit` 放在**命名空间层**而不是
`QrFinder` 里——Zan 编译器目前不支持"嵌套类型的数组"（`Outer.Inner[]` 报
undefined type；命名空间限定的 `System.Text.Encoding[]` 正常）。最小复现与
根因登记在 `TASKS.md`。

---
---

## 2. 通用生态包（按依赖顺序）

### 第 0 层 · 底座（其它包都依赖它）

| 建议包 | C# 参考 | ★ | 补什么（实测缺口） | 工期 |
|---|---|---|---|---|
| `Zan.Math` | MathNet.Numerics | 3,772 | ✅ 已并入 `stdlib/System/Numerics`（`MathUtil`/`Quaternion`/`Noise`/`RandomExt`/`Spline`，不新建平行包，见 §1.2） | — |
| `Zan.Compression` | SharpZipLib / LZ4 / Zstd | 3,933 | ✅ 已在 `packages/Zan.IO` 的 `System.IO.Compression` 内落地（纯 Zan：RFC1951 Deflate / LZ4 block+frame / GZip / BZip2 / Zip / Tar / Crc32，**不新建平行包**，见 §1.18）；剩下的真空白只有 **zstd**（熵编码解码器体量大，暂缓） | — |
| `Zan.Validation` | FluentValidation | 9,765 | ✅ 已落地 `packages/Zan.Validation`（`System.Validation`，规则链 + 错误聚合，编译期绑定取值器、运行期零反射，见 §1.3） | — |
| `Zan.Logging` | Serilog / NLog | 8,051 / 6,550 | ✅ 已在 `System.Diagnostics` 内落地（见 §1） | — |

### 第 1 层 · 中间件

| 建议包 | C# 参考 | ★ | 补什么 | 工期 |
|---|---|---|---|---|
| `Zan.Mapping` | AutoMapper / Mapster | 10,188 / 5,182 | ✅ 已落地 `packages/Zan.Mapping`（`System.Mapping`：运行期 `MapProfile` 显式成员规则 + 生成期 `GenMapping` 按约定配对生成源码（**无反射**）+ 可区分错误码与成员归因，见 §1.22） | — |
| `Zan.Messaging` | MassTransit | 7,809 | ✅ 已落地 `packages/Zan.Messaging`（`System.Messaging`：类型化通道 + 广播 / 命令语义 + 请求-响应端点（结果类型完整保留）+ 中间件管道 + 可区分错误码，无反射，见 §1.21） | — |
| `Zan.Cli` | Spectre.Console / Terminal.Gui | 11,666 / 11,279 | ✅ 已落地 `packages/Zan.Cli`（`System.Cli`：ANSI 样式 / 宽字符对齐表格 / 进度条 / 交互提示，见 §1.4） | — |
| `Zan.Jobs` | Hangfire / Quartz.NET | 10,151 / 7,096 | ✅ 已落地 `packages/Zan.Jobs`（`System.Jobs`：持久化任务存储（追加日志 + 重放 + 压实）+ 显式注册（无反射）+ 重试退避（含抖动）+ 延迟/周期调度 + 死信人工重排 + 状态统计，见 §1.23） | — |

### 第 2 层 · 领域

| 建议包 | C# 参考 | ★ | 补什么 |
|---|---|---|---|
| `Zan.Pdf` | QuestPDF | 14,205 | ✅ 已落地 `packages/Zan.Pdf`（`System.Pdf`：页面 + 线 / 矩形 / 文本 + 标准 14 字体度量 + 表格 + PDF 1.4 写出器（xref / 元数据 / WinAnsi），不嵌字体，见 §1.19） | — |
| `Zan.Ssh` | SSH.NET | 4,385 | SSH 客户端、SFTP、密钥管理 |
| `Zan.Barcode` | ZXing.Net | 榜外 | ✅ 已落地 `packages/Zan.Barcode`（`System.Barcode`：完整 QR 模型 1-40 编解码器（ISO/IEC 18004：分段 + 自动选版本 + GF(256) RS 交织 + 八掩码罚分）+ 扫描器（二值化 → 定位图形 → 仿射/投影变换 → 采样 → 格式信息 BCH → 去掩码 → 反交织 → RS 纠错 → 分段解析），**任意角度旋转、缩放 2px 起、椒盐噪声 5% 内、遮挡靠纠错还原**；不做一维条码，见 §1.25） | — |
| `Zan.Test.Fixtures` | AutoFixture / Bogus | 3,539 / 9,747 | ✅ 已落地 `packages/Zan.Test.Fixtures`（`System.Test.Fixtures`：定长种子随机源 + 语义化假数据（人名 / 邮箱 / 地址 / 句子 / 颜色 / 保留测试卡号）+ 集合假数据（唯一性有上限保证）+ 显式装配取值器，无反射，见 §1.17） | — |
| `Zan.Test.Mocks` | Moq / NSubstitute | 6,410 | ✅ 已落地 `packages/Zan.Test.Mocks`（`System.Test.Mocks`：调用记录 + 六种参数匹配 + 次数期望 + 手写替身基类 + 桩件与异步桩，无反射无代码生成，见 §1.14） | — |
| `Zan.Bench` | BenchmarkDotNet | 11,512 | ✅ 已落地 `packages/Zan.Bench`（`System.Bench`：预热 + 重复测量（单调时钟）+ 中位数主导统计 + 泛型参数化用例 + 文本/JSON 报告 + 基线对比（带噪声阈值）；**不提供分配量统计**（根因见 `TASKS.md` A464），见 §1.24） | — |

> LiteDB（9,486★）**不做**：嵌入式文档库用现有 ZanDb 覆盖，
> 不引入第二套持久化语义。

---

## 3. 游戏向增强

| 建议包 | C# 参考 | ★ | 现状 → 缺口 | 工期 |
|---|---|---|---|---|
| `Zan.Game.Ecs` | DefaultEcs / Unity ml-agents | 榜外 / 19,771 | ✅ 已落地 `packages/Zan.Game.Ecs`（`Game.Ecs`：类型化组件池 + 延迟销毁 + 分组系统调度 + 稳定序查询，见 §1.5） | — |
| `Zan.Game.Physics2D` | nphysics / JigglePhysics | 榜外 | ✅ 已落地 `packages/Zan.Game.Physics2D`（`Game.Physics2D`：值语义向量 + 凸多边形/圆窄相 + 顺序冲量动力学 + 距离/旋转关节，见 §1.6） | — |
| `Zan.Game.Physics3D` | BulletSharp | 榜外 | 无 | 6–8 周 |
| `Zan.Game.Pathfinding` | AStarPathFinding | 榜外 | ✅ 已落地 `packages/Zan.Game.Pathfinding`（`Game.Pathfinding`：加权 A* + 跳跃点搜索 + 视线拉直 + 导航网格漏斗平滑，见 §1.7） | — |
| `Zan.Game.Assets` | SharpGLTF / AssimpNet | 榜外 | ✅ 已落地 `packages/Zan.Game.Assets`（`Game.Assets`：声明式清单 + 依赖图 + 引用计数加载 + 确定性 LRU 预算淘汰 + 资源组，见 §1.9） | — |
| `Zan.Game.Audio` | NAudio | 6,249 | ✅ 已落地 `packages/Zan.Game.Audio`（`Game.Audio`：采样缓冲 + 波形合成 + ADSR 包络 + 双二阶滤波 + 混音 + 变调 + 纯 Zan WAV 读写，见 §1.10） | — |
| `Zan.Game.Netcode` | Mirror / LiteNetLib | 6,366 / 3,632 | ✅ 已落地 `packages/Zan.Game.Netcode`（`Game.Netcode`：确定性校验和 + 快照环形缓冲 + 逐帧输入预测 + 回滚重放会话（含分叉检测）+ 服务器侧延迟补偿，见 §1.13）；已有 lockstep + 回放 → 二者并列（lockstep 适合 RTS 同频，rollback 适合格斗/射击跟手） | — |
| `Zan.Game.Particles` | MonoGame.Extended | 榜外 | ✅ 已落地 `packages/Zan.Game.Particles`（`Game.Particles`：对象池 + 速率/突发发射器 + 曲线驱动 + 确定性噪声湍流，见 §1.8） | — |
| `Zan.Game.Animation` | Unity Animation Rigging | 榜外 | ✅ 已落地 `packages/Zan.Game.Animation`（`Game.Animation`：TRS 变换 + 骨骼层级 + 线性混合蒙皮 + 关键帧片段 + 播放器 + 状态机交叉淡化 + 两骨解析 IK，见 §1.16） | — |
| `Zan.Game.Render3D` | Silk.NET / OpenTK | 5,242 / 3,543 | `Mesh3D` 很薄；**GPU 后端只接 WGL/GLX，Android/OHOS 是 stub**（`SetRenderBackend(1)` 返回 0 是预期行为）→ 相机/材质/光照/shader/后处理 + 补齐跨平台 GPU 后端 | 8–12 周 |
| `Zan.Game.Procgen` | WaveFunctionCollapse / MarkovJunior | 25,416 / 8,443 | ✅ 已落地 `packages/Zan.Game.Procgen`（`Game.Procgen`：WFC 波函数坍缩 + 有向邻接规则 + 关卡拼接 + 房间走廊 MST 布局，见 §1.11） | — |
| `Zan.Game.Debug` | ImGui.NET | 榜外 | 无 → 帧剖析覆盖层、实体检查器、内存精灵缓存可视化 | 2–3 周 |
| `Zan.Game.Common` | — | — | ✅ 已落地 `packages/Zan.Game.Common`（`Game.Common`：版本化存档 + 飘字 + 血条残影 + 冷却条充能 + 背包堆叠 + 小地图投影，见 §1.12） | — |
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
**第 2 批（游戏纵深）**：`Zan.Game.Ecs` ✅ → `Zan.Game.Physics2D` ✅ → `Zan.Game.Pathfinding` ✅ → `Zan.Game.Particles` ✅
**第 3 批（内容生产）**：`Zan.Game.Assets` ✅ → `Zan.Game.Audio` ✅ → `Zan.Game.Procgen` ✅ → `Zan.Game.Common` ✅
**第 4 批（重型）**：`Zan.Game.Netcode` ✅ → `Zan.Game.Animation` ✅ → `Zan.Game.Render3D`（含跨平台 GPU 后端）→ `Zan.Pdf`
**第 5 批（生态纵深）**：`Zan.Test.Mocks` ✅ → `Zan.Test.Fixtures` ✅ → `Zan.Messaging` ✅ → `Zan.Mapping` ✅ → `Zan.Jobs` ✅ → `Zan.Bench` ✅ → `Zan.Barcode` ✅

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
