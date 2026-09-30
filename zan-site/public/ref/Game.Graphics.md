# Game.Graphics

> 源码: `packages/Zan.Game/src/Game/Graphics/Mesh3DProjection.zan`, `packages/Zan.Game/src/Game/Graphics/PaperdollRenderer.zan`, `packages/Zan.Game/src/Game/Graphics/SpriteAnimation.zan`, `packages/Zan.Game/src/Game/Graphics/SpriteSheet.zan`


## Animation3DTrack (class)

3D 骨骼动作切片与权重（用于动作平滑混合 Cross-Fade）。

- string clipName;

- double time;

- double duration;

- double speed;

- double weight;

- bool loop;

- Animation3DTrack()

- static Animation3DTrack Create(string clipName, double duration, double speed, bool loop)

- string ClipName { get }

- double Time{ get set}

- double Duration { get }

- double Weight{ get set}

- bool Loop { get }

- void Advance(double dt)


## AnimationState (class)

单个动作片段元数据（例如 "walk"、"attack"）。

- string name;

- int startRow;

- int framesPerDir;

- double fps;

- bool loop;

- AnimationState(string name, int startRow, int framesPerDir, double fps, bool loop)

- static AnimationState Create(string name, int startRow, int framesPerDir, double fps, bool loop)

- string Name { get }

- int StartRow { get }

- int FramesPerDir { get }

- double Fps { get }

- bool Loop { get }


## BoneSocket (class)

3D 骨骼挂点（Bone Socket），用于在角色手部挂载武器、背部挂载翅膀或头顶定位血条。

- string name;

- double localX;

- double localY;

- double localZ;

- BoneSocket(string name, double lx, double ly, double lz)

- static BoneSocket Create(string name, double lx, double ly, double lz)

- string Name { get }

- double LocalX { get }

- double LocalY { get }

- double LocalZ { get }

- void ResolveWorldOffset(double rotYRad, out double outX, out double outY, out double outZ)
  - 根据角色当前的 Y 轴旋转偏角（弧度），将局部偏置旋转换算为世界 3D 偏移。


## DirectionalAnimator (class)

2.5D / ARPG / RTS 经典 8 方向多状态骨骼级帧动画机。
自动衔接待机、行军、挥刀攻击、施法及死亡动画，杜绝手写零碎帧数判定。

- SpriteSheet sheet;

- AnimationState[]states;

- int stateCount;

- int currentStateIndex;

- int currentDir8;

- bool is5DirMode;

- double frameTimer;

- int currentFrameInDir;

- bool isFinished;

- DirectionalAnimator(SpriteSheet sheet, int maxStates)

- static DirectionalAnimator Create(SpriteSheet sheet, int maxStates)

- SpriteSheet Sheet { get }

- int Direction{ get set}

- bool Is5DirMode{ get set}

- bool IsFinished { get }

- int CurrentFrameInDir { get }

- string CurrentStateName { get }

- void AddState(string name, int startRow, int framesPerDir, double fps, bool loop)
  - 注册一个动作状态配置。

- void Play(string stateName, int dir8, bool forceRestart)
  - 切换播放状态（例如从 "idle" 切到 "attack"）。

- void Update(double dt)
  - 逐帧推进动画定时器。

- static void ResolveDir5(int dir8, out int outRowOffset, out bool outFlipX)
  - 5 方向对称工业标准映射矩阵。
    将 8 方向逻辑朝向解算为 5 行贴图索引与水平翻转标志（FlipX）。

- int GetCurrentGlobalFrame()
  - 获取当前帧在 SpriteSheet 中的全局绝对图元帧索引。
    8方向模式：(startRow + dir8) * cols + currentFrameInDir
    5方向模式：(startRow + dir5) * cols + currentFrameInDir

- bool IsCurrentFlipped()
  - 获取当前是否处于水平镜像翻转状态（仅在 5 方向模式下生效）。

- void GetCurrentUV(out float u0, out float v0, out float u1, out float v1)
  - 直接获取当前帧对应的纹理 UV 矩形。
    若启用 5方向镜像模式且当前朝向处于西侧，自动交换 u0 与 u1 实现 GPU 零开销硬件镜像。


## Mesh3DProjection (class)

2.5D 瓦片世界与 3D 角色模型表现层的高性能桥接组件。
核心功能：
1. 像素单位与 3D 世界单位严密对齐（PPU 投影转换）；
2. 360 度任意朝向无级丝滑平滑插值（克服传统 8 方向生硬跳跃）；
3. 动作交叉平滑融合（Cross-Fade 避免动作切帧突兀）；
4. 骨骼挂点挂载计算（武器、翅膀贴合跟随）。

- double pixelsPerUnit;

- double currentYaw;

- double targetYaw;

- double rotationSpeed;

- Animation3DTrack currentTrack;

- Animation3DTrack fadeTrack;

- double crossFadeDuration;

- double crossFadeElapsed;

- bool isFading;

- BoneSocket[]sockets;

- int socketCount;

- Mesh3DProjection(double ppu, int maxSockets)

- static Mesh3DProjection Create(double ppu, int maxSockets)

- double PPU { get }

- double CurrentYaw{ get set}

- double TargetYaw{ get set}

- double RotationSpeed{ get set}

- Animation3DTrack CurrentTrack { get }

- Animation3DTrack FadeTrack { get }

- bool IsFading { get }

- void World2Dto3D(double x2d, double y2d, double height2d, out double x3d, out double y3d, out double z3d)
  - 将 2D 逻辑世界坐标 (x, y) 转换为 3D 场景坐标 (X3D, Y3D, Z3D)。

- void Project3DtoScreen(double x3d, double y3d, double z3d, out double screenX, out double screenY)
  - 将 3D 场景坐标正交等轴测投影回屏幕像素相对坐标。
    标准 2:1 菱形视角投影：
    screenX = (X - Z) * PPU * 0.5
    screenY = (X + Z) * PPU * 0.25 - Y * PPU

- void SetLookDirection(double dx, double dy)
  - 设置目标朝向（360度任意角度，弧度制）。

- void RegisterSocket(string name, double lx, double ly, double lz)
  - 注册骨骼挂点（例如 "hand_r", 0.5, 0.8, 0.2）。

- bool GetSocketWorldPosition(string name, double charX3d, double charY3d, double charZ3d, out double outX, out double outY, out double outZ)
  - 获取挂点在世界 3D 空间中的坐标（叠加角色自身 3D 位置与当前朝向偏转）。

- void CrossFade(string newClipName, double duration, double speed, bool loop, double fadeTime)
  - 触发动作切换与平滑融合（Cross-Fade）。

- void Update(double dt)
  - 逐帧推进旋转插值与动作混合时间。


## PaperdollLayer (class)

纸娃娃独立挂件层（如：身体、服饰、武器、翅膀、头饰）。

- string name;

- SpriteSheet sheet;

- bool is5DirMode;

- bool visible;

- double offsetX;

- double offsetY;

- int baseOrder;

- int[]orderPerDir;

- PaperdollLayer(string name, SpriteSheet sheet, int baseOrder, int[]orderPerDir, bool is5DirMode)

- static PaperdollLayer Create(string name, SpriteSheet sheet, int baseOrder, int[]orderPerDir, bool is5DirMode)

- string Name { get }

- SpriteSheet Sheet{ get set}

- bool Visible{ get set}

- bool Is5DirMode{ get set}

- double OffsetX{ get set}

- double OffsetY{ get set}

- int BaseOrder{ get set}

- int GetOrder(int dir8)


## PaperdollRenderer (class)

2.5D / ARPG 传奇与暗黑类纸娃娃换装与分层渲染系统。
主控单一时间轴，保证各挂件同帧严格对齐；
支持 5 方向自动镜像与 8 方向动态层深调序（彻底消除穿模）。

- PaperdollLayer[]layers;

- int layerCount;

- DirectionalAnimator masterAnimator;

- int[]sortedLayerIndices;

- int[]sortKeys;

- PaperdollRenderer(SpriteSheet baseSheet, int maxLayers)

- static PaperdollRenderer Create(SpriteSheet baseSheet, int maxLayers)

- DirectionalAnimator MasterAnimator { get }

- int LayerCount { get }

- void AddLayer(string name, SpriteSheet sheet, int baseOrder, int[]orderPerDir, bool is5DirMode)
  - 添加或挂载一个纸娃娃部件层。

- PaperdollLayer GetLayer(string name)
  - 根据名字获取指定部件层。

- void SetLayerVisible(string name, bool visible)
  - 设置部件层可见性（例如隐藏头盔或脱下翅膀）。

- void SwapLayerSheet(string name, SpriteSheet newSheet)
  - 替换指定部位的图集（实现即时换装换武器）。

- void Play(string stateName, int dir8, bool forceRestart)
  - 播放动作，统一主控动画机。

- void Update(double dt)
  - 时钟步进推进。

- int SortLayersByDirection()
  - 根据当前朝向对所有激活的部件层进行 Z-Order 快速排序（升序）。
    返回有效部件层数量，排序结果保存在 sortedLayerIndices 中。

- int GetSortedLayerIndex(int sortedRank)
  - 获取排序后的第 i 个图层索引。

- void GetLayerCurrentUV(int layerIndex, out float u0, out float v0, out float u1, out float v1, out bool flipped)
  - 获取指定部件层在当前主控动画帧下的 UV 矩形与翻转标志。


## SpriteSheet (class)

纹理图集切片网格（SpriteSheet）。
将单张大贴图按照固定列数、行数划分为帧图元，提供高频 UV 映射。

- string textureKey;

- int texWidth;

- int texHeight;

- int frameWidth;

- int frameHeight;

- int cols;

- int rows;

- int totalFrames;

- SpriteSheet(string key, int texW, int texH, int frameW, int frameH)

- static SpriteSheet Create(string key, int texW, int texH, int frameW, int frameH)

- string TextureKey { get }

- int FrameWidth { get }

- int FrameHeight { get }

- int TotalFrames { get }

- int Cols { get }

- int Rows { get }

- void GetFrameUV(int frameIndex, out float u0, out float v0, out float u1, out float v1)
  - 获取指定帧在图集上的归一化纹理坐标 UV 矩形 [u0, v0, u1, v1]。

- void GetFrameRect(int frameIndex, out int srcX, out int srcY, out int srcW, out int srcH)
  - 获取指定帧在图集上的像素裁剪矩形 [srcX, srcY, srcW, srcH]。
