# Game.Core

> 源码: `packages/Zan.Game/src/Game/Core/Anim.zan`, `packages/Zan.Game/src/Game/Core/Camera2D.zan`, `packages/Zan.Game/src/Game/Core/Clock.zan`, `packages/Zan.Game/src/Game/Core/Entity.zan`, `packages/Zan.Game/src/Game/Core/GameViewport.zan`, `packages/Zan.Game/src/Game/Core/Scene.zan`, `packages/Zan.Game/src/Game/Core/SceneManager.zan`, `packages/Zan.Game/src/Game/Core/SpatialHash2D.zan`


## AnimClip (class)

基于方向优先帧表的单个动画片段（
Mir 式布局：先是方向 0 的全部帧，再是方向 1，……）。
由 IDE 资源管理器写入的 `.anim` 旁置 JSON 加载。

- string image;

- int frameW;

- int frameH;

- int baseFrame;

- int dirs;

- int framesPerDir;

- int fps;

- int loopMode;

- int loopStart;

- AnimClip(int dirs, int framesPerDir, int fps, int loopMode, int loopStart)

- static AnimClip ParseJson(string json)
  - 解析 `.anim` JSON 文档；不是该格式则返回 null。

- static AnimClip Load(string path)
  - 从磁盘上的 `.anim` 文件加载片段（失败返回 null）。

- string Image()
  - 帧表图片条目/路径（`.anim` 的 image 字段，默认空串）。

- int Base()
  - 多动作帧表中该动作的首帧（`.anim` 的 base 字段）。

- int FrameW()
  - 帧单元宽（0 = 未知，从帧表推导）。

- int FrameH()
  - 帧单元高（0 = 未知，从帧表推导）。

- int Dirs()
  - 方向数量（构造时小于 1 钳制为 1；经典 Mir 移动为 8）。

- int FramesPerDir()
  - 每个方向的帧数（构造时小于 1 钳制为 1）。

- int Fps()
  - 播放帧率（FPS，构造时小于 1 钳制为 1）。

- int LoopMode()
  - 循环模式：0 播放一次，1 循环，2 循环片段。

- int LoopStart()
  - 循环片段（模式 2）的首帧（钳制在 [0, framesPerDir-1]）。


## AnimPlayer (class)

播放 AnimClip：按流逝毫秒推进，可随时切换
朝向（帧位置保持不变，与经典 Mir 渲染器一致），
并读回帧表中要绘制的绝对帧。
循环模式：once（停在最后一帧，Done() 变为 true）、loop（回到
0）、section（先播放 0..loopStart-1 的前奏一次，再循环
loopStart..末尾——例如施法前摇后接持续施法）。

- AnimClip clip;

- int dir;

- int frame;

- int accMs;

- bool done;

- AnimPlayer()

- void Play(AnimClip c)
  - 从第 0 帧开始（或重新开始）播放片段，保持当前朝向。

- void SetDir(int d)
  - 切换朝向；片段内帧位置保持不变，
    转身的角色不会重新开始步伐。

- int Dir()
  - 当前朝向（0..dirs-1，SetDir 已归一化）。

- bool Done()
  - once 模式片段是否已停在最后一帧；循环片段恒为 false。

- int Frame()
  - 当前方向内的帧索引（0..framesPerDir-1）。

- void Update(int dtMs)
  - 按流逝毫秒推进。

- int SheetFrame()
  - 当前朝向与位置在帧表中的绝对帧索引
    ：base + dir * framesPerDir + frame。


## Camera2D (class)

2D 游戏正交相机与视口管理。
支持平滑插值跟随、视口内外剔除判定、地图边界钳制与阻尼屏幕震动。

- double x;

- double y;

- int viewWidth;

- int viewHeight;

- double zoom;

- bool hasBounds;

- double minX;

- double minY;

- double maxX;

- double maxY;

- double shakeIntensity;

- double shakeTimer;

- double shakeOffsetX;

- double shakeOffsetY;

- Random rnd;

- Camera2D(int viewW, int viewH)

- static Camera2D Create(int viewW, int viewH)

- double X{ get set}

- double Y{ get set}

- double Zoom{ get set}

- int ViewWidth { get }

- int ViewHeight { get }

- void Resize(int w, int h)

- void SetBounds(double minX, double minY, double maxX, double maxY)

- void ClearBounds()

- void Shake(double intensity, double durationSec)
  - 触发屏幕震动（例如暴击、爆炸、地震）。

- void Update(double dt)
  - 逐帧更新相机插值运动与震屏衰减。

- void Follow(double targetX, double targetY, double smoothSpeed, double dt)
  - 平滑跟随目标世界坐标（Lerp 缓动）。

- void ClampToBounds()

- void WorldToScreen(double wx, double wy, out double sx, out double sy)
  - 世界坐标转换为屏幕像素坐标。

- void ScreenToWorld(double sx, double sy, out double wx, out double wy)
  - 屏幕像素坐标转换为世界坐标（例如鼠标点击位置拾取）。

- bool IsVisible(double wx, double wy, double w, double h)
  - 视锥范围裁剪判定：判断一个世界矩形是否在屏幕可见范围内。


## Entity (class)

游戏轻量实体基类。
内置位置历史（用于亚帧平滑插值渲染）、包围盒及生命周期钩子。

- int id;

- string tag;

- bool isAlive;

- bool isActive;

- double x;

- double y;

- double prevX;

- double prevY;

- double width;

- double height;

- double radius;

- double footOffsetY;

- int sortLayer;

- Entity(int id, string tag)

- int Id { get }

- string Tag{ get set}

- bool IsAlive { get }

- bool IsActive{ get set}

- double X{ get set}

- double Y{ get set}

- double PrevX { get }

- double PrevY { get }

- double Width{ get set}

- double Height{ get set}

- double Radius{ get set}

- double FootOffsetY{ get set}

- double FootY { get }

- int SortLayer{ get set}

- void Destroy()
  - 标记并销毁该实体，下一帧从场景中移除。

- void SnapshotHistory()
  - 记录当前帧历史坐标，准备开始物理步进计算。

- virtual void OnAwake(Scene scene)
  - 实体被加入场景时触发。

- virtual void OnFixedUpdate(Scene scene, double dt)
  - 固定时间步长逻辑更新（碰撞、寻路、AI）。

- virtual void OnRender(Scene scene, Canvas c, double interpX, double interpY)
  - 亚帧平滑插值渲染钩子。
    interpX/interpY 已经过 (prev * (1 - alpha) + cur * alpha) 滤波平滑，完全消除帧率抖动。

- virtual void OnDestroy(Scene scene)
  - 实体彻底销毁移除时触发。


## GameClock (class)

确定性高频固定步长游戏时钟（Fixed Time-Step Loop）。
将不稳定的可变渲染帧间隔转化为恒定物理步长（如 50Hz/60Hz），
杜绝物理穿模、积分漂移与逻辑因掉帧而脱节。

- double fixedDeltaTime;

- double accumulator;

- double maxAccumulator;

- long lastTickMs;

- double timeScale;

- long tickCount;

- GameClock(double fixedDt)

- static GameClock Create60Hz()

- static GameClock Create50Hz()

- double FixedDeltaTime { get }

- double TimeScale{ get set}

- long TickCount { get }

- void Reset()
  - 重置计时基准（通常在场景加载或窗口从最小化恢复时调用，避免产生巨大 dt）。

- int Advance()
  - 推进时钟，返回本渲染帧需要执行的固定逻辑步长次数（通常为 0, 1 或 2）。
    宿主只需按照该步数循环调用 FixedUpdate(fixedDeltaTime) 即可。

- double InterpolationAlpha()
  - 获取当前剩余时间在固定步长内的插值比例（0.0 ~ 1.0）。
    用于渲染层在两帧固定物理状态之间做平滑位置插值（Pos = PrevPos * (1-alpha) + CurrPos * alpha）。


## GameViewport (class)

官方游戏世界视口集成控件（GameViewport）。
继承自 Gui.Control，无缝缝合底层 GPU Canvas 渲染与上层高频游戏世界逻辑。
自动同步视口边界、推进固定时钟步进并映射鼠标世界空间拾取。

- Scene activeScene;

- InputMapper input;

- Action <Canvas, int, int> onRenderHudCallback;

- bool autoDriveClock;

- GameViewport(Scene scene, int dockMode)

- static GameViewport Create(Scene scene)

- Scene CurrentScene{ get set}

- InputMapper Input { get }

- bool AutoDriveClock{ get set}

- void SetHudRenderer(Action <Canvas, int, int> hudCallback)
  - 注册顶层 HUD / UI 绘制回调（在场景世界渲染完成后叠加）。

- override void OnMeasure(App app)

- override void OnPaint(App app)


## Scene (class)

统一游戏场景与实体生命周期管理器。
将 GameClock、Camera2D、SpatialHash2D 与 YSortLayer 缝合成自动化流水线。

- string name;

- Camera2D camera;

- SpatialHash2D spatialGrid;

- YSortLayer ySortLayer;

- GameClock clock;

- int maxEntities;

- Entity[]entities;

- int entityCount;

- Entity[]pendingAdd;

- int pendingAddCount;

- int nextEntityId;

- Scene(string name, int viewW, int viewH, int maxCap)

- static Scene Create(string name, int viewW, int viewH, int maxCap)

- string Name { get }

- Camera2D Camera { get }

- SpatialHash2D SpatialGrid { get }

- GameClock Clock { get }

- int EntityCount { get }

- int NextId()

- void AddEntity(Entity e)
  - 登记并向场景添加实体。

- void FlushPendingAdd()

- void AdvanceFixedStep(double fixedDt)
  - 推进固定步长物理循环。
    自动调用实体的 OnFixedUpdate 并重建空间哈希索引。

- int Tick()
  - 驱动整个场景的时钟泵。
    自动处理螺旋死锁并根据累计耗时推进固定物理步长。

- void Render(Canvas c)
  - 场景完整视锥剔除与深度 Y 排序渲染。
    自动解算亚帧平滑插值，按脚底 Y 坐标从小到大绘制，消除一切抖动与层级遮挡错乱。

- Entity FindById(int id)
  - 按 ID 查找场景内活跃实体，未找到返回 null。


## SceneManager (class)

工业级场景状态栈与无缝转场管理器。
核心特性：
1. 场景栈管理（Push 挂起不销毁，Pop 瞬间恢复，ReplaceScene 平滑切关）；
2. 电影级淡入淡出遮罩（避免硬切导致的画面突兀与掉帧感）；
3. 黑屏遮蔽下异步安全资产卸载与预热契约。

- Scene[]sceneStack;

- int stackTop;

- TransitionPhase phase;

- double fadeDuration;

- double fadeTimer;

- double fadeAlpha;

- Scene pendingNextScene;

- bool popPendingOnSwitch;

- Action <int, int> onAudioTransitionCallback;

- SceneManager(int maxStackDepth)

- static SceneManager Create(int maxStackDepth)

- TransitionPhase Phase { get }

- double FadeAlpha { get }

- bool IsTransitioning { get }

- Scene GetActiveScene()
  - 获取当前顶层活跃场景。

- void Push(Scene newScene)
  - 压入新场景（当前场景挂起不销毁，例如在地下城中打开大地图或背包打造）。

- Scene Pop()
  - 弹出顶层场景，恢复下一层场景为活跃态。

- void SetAudioTransitionCallback(Action <int, int> callback)
  - 设置切场时的音频渐隐渐入联动回调。

- void SwitchScene(Scene nextScene, double duration)
  - 平滑切换主场景（自动触发黑屏遮罩渐变：FadeOut -> 换图载入 -> FadeIn）。

- void Update(double dt)
  - 推进场景更新循环与转场时间轴。

- void Render(Canvas c, double viewportWidth, double viewportHeight)
  - 渲染当前场景并叠加平滑黑屏遮罩。


## SpatialHash2D (class)

高性能二维紧凑空间哈希网格（Spatial Hash Grid）。
专为塔防怪群、ARPG 怪物碰撞、RTS 寻敌等海量实体设计。
内部基于紧凑静态扁平链表实现，查询与插入完全零 GC 分配，O(1) 检索周围邻域。

- int cellSize;

- int tableSize;

- int maxEntities;

- int count;

- int[]bucketHead;

- int[]nextEntry;

- int[]entityIds;

- double[]posX;

- double[]posY;

- double[]radius;

- SpatialHash2D(int cellSize, int maxEntities)

- static SpatialHash2D Create(int cellSize, int maxCapacity)

- void Clear()
  - 清空整张网格，准备下一帧重新插入（复杂度仅为 O(tableSize)）。

- int HashCoords(int cellX, int cellY)

- bool Insert(int id, double x, double y, double r)
  - 插入一个实体到哈希网格中。

- int QueryRange(double qx, double qy, double qRadius, int[]outIds, int maxOut)
  - 查询指定圆心 (cx, cy) 范围内 radius 半径内的所有实体。
    结果输出到传入的 outIds 数组中，返回命中实体数量。零堆分配。

- int FindNearest(double qx, double qy, double maxRange)
  - 寻找离指定点 (qx, qy) 在 maxRange 范围内最近的实体 ID，未找到返回 -1。
    塔防防御塔攻击索敌、自动施法的关键核心算子。


## TransitionPhase (enum)

场景切场过渡状态机。

- None

- FadeOut

- Switching

- FadeIn = 渐亮显露新世界 (1.0 -> 0.0)
