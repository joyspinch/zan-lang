# Game.Arcade2D

> 源码: `packages/Zan.Game/src/Game/Arcade2D/Animation.zan`, `packages/Zan.Game/src/Game/Arcade2D/Collision2D.zan`, `packages/Zan.Game/src/Game/Arcade2D/Geometry.zan`, `packages/Zan.Game/src/Game/Arcade2D/World.zan`


## AdvancedCollision2D (class)

高级 2D 碰撞检测与空间几何计算扩展。

- const double EPSILON=0.0000001;

- static RaycastHit2D RaycastRect(double ox, double oy, double dx, double dy, double maxDist, Rect2 rect)
  - 2D 射线与轴对齐矩形 (AABB) 快速相交检测（Slab 法）。

- static RaycastHit2D RaycastCircle(double ox, double oy, double dx, double dy, double maxDist, Circle2 circle)
  - 2D 射线与圆的相交检测。

- static Vector2 ClosestPointOnSegment(double px, double py, double ax, double ay, double bx, double by)
  - 计算点 P 到线段 AB 的最近投影点。

- static bool SegmentIntersect(double ax1, double ay1, double ax2, double ay2, double bx1, double by1, double bx2, double by2, Vector2 outHit)
  - 判断两条 2D 线段 A1A2 与 B1B2 是否相交，相交时通过 outHit 返回交点坐标。

- static Vector2 FindSegmentIntersection(double ax1, double ay1, double ax2, double ay2, double bx1, double by1, double bx2, double by2)
  - 便捷版本：若两线段相交返回交点 Vector2，否则返回 null。

- static bool CircleVsSegment(Circle2 circle, double ax, double ay, double bx, double by)
  - 圆形与线段（骨架线段/胶囊体端点）相交测试。

- static CollisionManifold RectManifold(Rect2 a, Rect2 b)
  - 计算两个 AABB 轴对齐矩形的碰撞流形。
    若相交，返回最小分离法线（Normal 指向物体 B）与穿透深度（Depth），
    用于将重叠物体推开纠正物理穿模。


## AnimationClip (class)

有序动画帧序列，附带名称与是否循环标记。
通过 Add 链式追加帧。

- string name;

- bool loop;

- List<AnimationFrame> frames;

- AnimationClip(string name, bool loop)
  - 创建空片段；name 仅为标识用途，loop 决定播放到末尾后是否回到第 0 帧。

- AnimationClip Add(int x, int y, int width, int height, int duration)
  - 追加一帧（参数同 AnimationFrame，duration 会被钳制为至少 1 毫秒）；
    返回自身以便链式调用。

- string Name()
  - 片段名称（构造时传入的标识字符串）。

- bool Loop()
  - 是否循环播放。

- int FrameCount()
  - 已追加的帧数。

- AnimationFrame FrameAt(int index)
  - 返回第 index 帧；index 越界时行为由底层 List 决定（抛错），调用方需保证 0 <= index < FrameCount()。


## AnimationFrame (class)

帧表动画的单帧：帧表图集中的一个子矩形与持续时长。
坐标单位为帧表图集上的像素。

- int x;

- int y;

- int width;

- int height;

- int duration;

- AnimationFrame(int x, int y, int width, int height, int duration)
  - 创建一帧：x/y/width/height 为帧表图集上的像素子矩形，
    duration 为展示毫秒数（小于 1 按 1 钳制）。

- int X()
  - 帧在帧表图集上的左上角 X（像素）。

- int Y()
  - 帧在帧表图集上的左上角 Y（像素）。

- int Width()
  - 帧宽度（像素）。

- int Height()
  - 帧高度（像素）。

- int Duration()
  - 该帧的展示时长（毫秒），构造时已保证至少为 1。


## AnimationPlayer (class)

单片段动画播放器：按 Update 累积的毫秒数推进帧。
非循环片段播完后停在最后一帧并置 Finished；循环片段回到第 0 帧。
需在每帧循环中手动调用 Update 传入流逝毫秒。

- AnimationClip clip;

- int frameIndex;

- int elapsed;

- bool playing;

- bool finished;

- AnimationPlayer(AnimationClip clip)
  - 创建播放器并立即开始播放 clip（等价于 Play(clip)）。

- void Play(AnimationClip clip)
  - 从第 0 帧开始（或重新开始）播放 clip；
    clip 为 null 或无帧时进入非播放状态。

- void Update(int deltaMilliseconds)
  - 推进动画：deltaMilliseconds 为流逝毫秒（非正数时被忽略）。
    单帧时长耗尽即切到下一帧，剩余毫秒结转；非循环播完置 Finished，
    循环片段从第 0 帧继续。

- AnimationFrame Current()
  - 当前帧；片段为 null 或无帧时返回 null。

- int FrameIndex()
  - 当前帧索引（非循环播完后停留在最后一帧的索引）。

- bool Playing()
  - 是否正在播放（非循环播完后为 false）。

- bool Finished()
  - 非循环片段是否已播放完毕；循环片段恒为 false。


## ArcadeEntity (class)

街机世界中的单个实体（敌人/子弹/粒子等）：极简物理（速度按
秒积分）、碰撞体、生命与冷却等通用槽位。实体不自我构造，
由 ArcadeWorld.Spawn 分配并复用失活槽位。

- int id;

- int kind;

- int team;

- bool active;

- double x;

- double y;

- double previousX;

- double previousY;

- double velocityX;

- double velocityY;

- double width;

- double height;

- double radius;

- int hp;

- int maxHp;

- int targetId;

- int cooldown;

- int lifetime;

- int pathIndex;

- int data;

- void Reset(int id, int kind, int team, double x, double y, double width, double height)
  - （重）初始化实体：写入 id/kind/team/位置与碰撞矩形，清零
    速度与各槽位，激活实体；碰撞半径重置为 (宽+高)/4，生命
    重置为 1，寿命重置为无限（-1）。由 Spawn 调用。

- void Step(int deltaMilliseconds)
  - 推进一帧：记录上一帧位置后按速度积分位移（速度单位为
    单位/秒），冷却毫秒递减到 0，寿命毫秒递减、减到 0 即失活。
    失活实体或 deltaMilliseconds ≤ 0 时无操作。

- void MoveTowards(double targetX, double targetY, double speed)
  - 把速度设为指向 (targetX, targetY) 的 speed（单位/秒）。已到
    达目标（距离平方 ≤ 0.0001）或 speed ≤ 0 时清零速度。
    只改速度不移动，位移在 Step 中积分。

- int Damage(int amount)
  - 结算一次伤害：失活或 amount ≤ 0 时返回 0 无副作用；实际
    生效量按剩余生命钳制，生命减到 0 即失活。返回实际扣除的
    生命值。

- Rect2 Bounds()
  - 以中心点为准的碰撞矩形（宽高为 Width × Height）。

- Circle2 Circle()
  - 以中心为圆心的碰撞圆（半径可被 SetRadius 覆盖）。

- int Id()
  - 实例唯一 ID（由 World.Spawn 分配，递增且不复用）。

- int Kind()
  - 类型标签（调用方自定义的枚举值）。

- int Team()
  - 阵营标签（同队互为目标查找排除）。

- bool Active()
  - 是否活跃（失活槽位会被 Spawn 回收复用）。

- double X()
  - 当前世界坐标 X（单位由调用方约定，通常像素）。

- double Y()
  - 当前世界坐标 Y。

- double PreviousX()
  - 上一帧的 X（Step 前的位置）。

- double PreviousY()
  - 上一帧的 Y。

- double VelocityX()
  - X 方向速度（单位/秒）。

- double VelocityY()
  - Y 方向速度（单位/秒）。

- double Width()
  - 宽度（碰撞矩形尺寸）。

- double Height()
  - 高度（碰撞矩形尺寸）。

- double Radius()
  - 碰撞半径（默认 (宽+高)/4）。

- int Hp()
  - 当前生命值（0 表示已死亡/失活）。

- int MaxHp()
  - 生命上限（SetHealth 设置的值）。

- int TargetId()
  - 目标实体 ID（0 表示未设置；由调用方维护语义）。

- int Cooldown()
  - 剩余冷却毫秒（Step 中递减到 0）。

- int Lifetime()
  - 剩余寿命毫秒；-1 表示无限。

- int PathIndex()
  - 当前路径点索引（ArcadePath.Follow 使用）。

- int Value()
  - 调用方自定义整数槽位（如伤害值、掉落类型）。

- void SetActive(bool active)
  - 设置活跃状态；设为 false 后 Step/Damage 均无操作。

- void SetPosition(double x, double y)
  - 直接设置世界坐标（不影响上一帧位置的记录时序）。

- void SetVelocity(double x, double y)
  - 设置速度分量（单位/秒）。

- void SetRadius(double radius)
  - 覆盖碰撞半径（默认 (宽+高)/4，Reset 时重置）。

- void SetHealth(int hp)
  - 设置生命值并回满：小于 1 时钳制为 1，maxHp 与 hp 同时置为该值。

- void SetTargetId(int targetId)
  - 设置目标实体 ID（语义由调用方维护，0 表示未设置）。

- void SetCooldown(int cooldown)
  - 设置冷却毫秒数，负值钳制为 0；Step 中递减。

- void SetLifetime(int lifetime)
  - 设置剩余寿命（毫秒）；负值表示无限（Step 不再递减失活）。

- void SetPathIndex(int pathIndex)
  - 设置当前路径点索引（配合 ArcadePath.Follow 使用）。

- void SetValue(int data)
  - 写入调用方自定义整数槽位。


## ArcadePath (class)

由有序路径点构成的路径：Follow 让实体逐点移动，到达判定用
距离阈值（tolerance），走过的点推进实体内的路径索引。

- List<Vector2> points;

- ArcadePath()

- ArcadePath Add(double x, double y)
  - 追加一个路径点（世界坐标），返回 this 便于链式构建。

- bool Follow(ArcadeEntity entity, double speed, double tolerance)
  - 让实体沿路径移动一步（speed 单位/秒；tolerance 为判定到达的
    距离阈值，进入阈值即吸附到该点并推进索引）。
    返回 true 表示路径已走完（或实体失活），此时速度清零；
    false 表示仍在途中。实体需每帧传入同一实例。

- int Count()
  - 路径点数量。

- Vector2 At(int index)
  - 返回第 index 个路径点；越界行为由底层 List 决定（抛错）。


## ArcadeWorld (class)

复用实体存储，供自动战斗、塔防、粒子及
轻量竞技场游戏使用；非活跃槽位在 Spawn 时回收。

- List<ArcadeEntity> entities;

- int nextId;

- ArcadeWorld()

- ArcadeEntity Spawn(int kind, int team, double x, double y, double width, double height)
  - 生成一个实体：优先复用最早的失活槽位，否则追加新槽位；
    分配自增且不复用的唯一 ID 并重置全部状态后返回。

- void Update(int deltaMilliseconds)
  - 推进所有实体一帧（含移动、冷却、寿命递减与自动失活）；
    deltaMilliseconds <= 0 时各实体内部直接忽略。

- ArcadeEntity FindById(int id)
  - 按 ID 查找活跃实体；不存在或已失活时返回 null。

- ArcadeEntity FindClosestEnemy(ArcadeEntity source, double maximumDistance)
  - 查找 source 附近最近的敌方活跃实体（排除自身与同阵营），
    距离按平方比较，上限 maximumDistance（含）；source 为 null
    或无候选时返回 null。

- int CountActive(int kind)
  - 统计活跃实体数；kind < 0 时统计全部，否则只统计该 kind。

- int CountTeam(int team)
  - 统计指定阵营的活跃实体数。

- int Capacity()
  - 当前槽位总数（活跃 + 已失活待复用，只增不减）。

- ArcadeEntity At(int index)
  - 返回第 index 个实体（含失活的）；越界行为由底层 List 决定（抛错）。


## Camera2D (class)

简单 2D 摄像机：维护世界坐标视口左上角与缩放，提供
世界坐标 <-> 屏幕坐标的整数换算。屏幕坐标原点在左上角，
向右向下为正；缩放 <= 1 时视野扩大，> 1 时放大画面。

- double x;

- double y;

- double zoom;

- int viewportWidth;

- int viewportHeight;

- Camera2D(int viewportWidth, int viewportHeight)
  - 创建视口尺寸 viewportWidth x viewportHeight（像素）的摄像机；
    初始位于世界原点、缩放 1.0。

- void CenterOn(double x, double y)
  - 平移视口，使世界点 (x, y) 位于视口中心（等价于
    SetPosition(x - 视口宽/zoom/2, y - 视口高/zoom/2)）。

- void SetPosition(double x, double y)
  - 直接设置视口左上角的世界坐标。

- void SetZoom(double zoom)
  - 设置缩放倍率；小于 0.1 钳制为 0.1，大于 8.0 钳制为 8.0。

- int ScreenX(double worldX)
  - 世界坐标 -> 屏幕坐标 X（截断取整，可能为负）。

- int ScreenY(double worldY)
  - 世界坐标 -> 屏幕坐标 Y（截断取整，可能为负）。

- double WorldX(double screenX)
  - 屏幕坐标 -> 世界坐标 X（ScreenX 的逆变换，保留小数）。

- double WorldY(double screenY)
  - 屏幕坐标 -> 世界坐标 Y（ScreenY 的逆变换，保留小数）。

- double X()
  - 视口左上角的世界坐标 X。

- double Y()
  - 视口左上角的世界坐标 Y。

- double Zoom()
  - 当前缩放倍率（0.1..8.0）。


## Circle2 (class)

以圆心 (x, y) 与半径 radius 定义的圆形碰撞体。

- double x;

- double y;

- double radius;

- Circle2(double x, double y, double radius)

- double X()
  - 圆心 X。

- double Y()
  - 圆心 Y。

- double Radius()
  - 半径（不校验非负）。


## Collision2D (class)

二维碰撞判定工具集；任一形状参数为 null 时判定为不相交。

- static bool PointInRect(double x, double y, Rect2 rect)
  - 点 (x, y) 是否位于矩形内（含边界）。

- static bool Rects(Rect2 a, Rect2 b)
  - 两轴对齐矩形是否相交（仅边缘相触不算）。

- static bool Circles(Circle2 a, Circle2 b)
  - 两圆是否相交（圆心距 ≤ 半径之和，含恰好相切）。

- static double DistanceSquared(double ax, double ay, double bx, double by)
  - 两点间距离的平方（避免开方）；比较距离时用它配对同侧平方阈值。


## CollisionManifold (class)

2D 碰撞接触流形（Collision Manifold）。
包含两物体相交时的法线方向与重叠穿透深度，供物理位移分离（Depenetration）使用。

- bool collided;

- double normalX;

- double normalY;

- double depth;

- CollisionManifold(bool collided, double nx, double ny, double depth)

- static CollisionManifold None()
  - 创建未碰撞流形

- bool Collided()
  - 是否发生碰撞接触

- double NormalX()
  - 物体 A 推开物体 B 的分离法线 X（从 A 指向 B）

- double NormalY()
  - 物体 A 推开物体 B 的分离法线 Y（从 A 指向 B）

- double Depth()
  - 沿法线方向的重叠穿透深度


## RaycastHit2D (class)

2D 射线检测结果。包含命中状态、碰撞距离、接触点坐标与法线向量。

- bool hit;

- double distance;

- double pointX;

- double pointY;

- double normalX;

- double normalY;

- RaycastHit2D(bool hit, double distance, double px, double py, double nx, double ny)

- static RaycastHit2D Miss()
  - 创建未命中结果

- bool Hit()
  - 是否命中目标

- double Distance()
  - 从射线起点到命中点的距离

- double PointX()
  - 命中点世界坐标 X

- double PointY()
  - 命中点世界坐标 Y

- double NormalX()
  - 命中表面单位法线 X

- double NormalY()
  - 命中表面单位法线 Y


## Rect2 (class)

轴对齐矩形，(x, y) 为左上角，宽高向右/向下延伸。
坐标单位与 `Vector2` 一致（通常为像素）。

- double x;

- double y;

- double width;

- double height;

- Rect2(double x, double y, double width, double height)
  - 创建左上角 (x, y)、尺寸 width x height 的矩形；不做合法性检查（负宽高原样保留）。

- double X()
  - 左上角 X。

- double Y()
  - 左上角 Y。

- double Width()
  - 宽度。

- double Height()
  - 高度。

- double Right()
  - 右边界 X（= X + Width；负宽时小于 X）。

- double Bottom()
  - 下边界 Y（= Y + Height；负高时小于 Y）。


## Vector2 (class)

二维浮点向量（世界坐标，单位约定由调用方决定，通常为像素）。
可变值类型语义：Set/Add 直接修改自身。

- double x;

- double y;

- Vector2(double x, double y)
  - 创建分量 (x, y) 的向量。

- double X()
  - X 分量。

- double Y()
  - Y 分量。

- void Set(double x, double y)
  - 将两个分量替换为给定值。

- void Add(double x, double y)
  - 就地累加偏移量（x、y 分别加到对应分量）。
