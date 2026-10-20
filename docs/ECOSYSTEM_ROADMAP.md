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
| `Zan.Test.Mocks` | Moq / NSubstitute | 6,410 | ✅ 已落地 `packages/Zan.Test.Mocks`（`System.Test.Mocks`：调用记录 + 六种参数匹配 + 次数期望 + 手写替身基类 + 桩件与异步桩，无反射无代码生成，见 §1.14） | — |
| `Zan.Bench` | BenchmarkDotNet | 11,512 | 基准测试（ARC 无 GC，分配量统计比 .NET 更该做） |

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
| `Zan.Game.Animation` | Unity Animation Rigging | 榜外 | 有 clip 播放器/纸娃娃/3D 轨 → 蒙皮、状态机混合、IK | 4–6 周 |
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
**第 4 批（重型）**：`Zan.Game.Netcode` ✅ → `Zan.Game.Animation` → `Zan.Game.Render3D`（含跨平台 GPU 后端）→ `Zan.Pdf`

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
