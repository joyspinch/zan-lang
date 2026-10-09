# Game.Core

> 源码: `packages/Zan.Game/src/Game/Core/Anim.zan`, `packages/Zan.Game/src/Game/Core/Camera2D.zan`, `packages/Zan.Game/src/Game/Core/Clock.zan`, `packages/Zan.Game/src/Game/Core/Entity.zan`, `packages/Zan.Game/src/Game/Core/GameViewport.zan`, `packages/Zan.Game/src/Game/Core/Scene.zan`, `packages/Zan.Game/src/Game/Core/SceneManager.zan`, `packages/Zan.Game/src/Game/Core/SpatialHash2D.zan`


## AnimClip (class)

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

- static AnimClip Load(string path)

- string Image()

- int Base()

- int FrameW()

- int FrameH()

- int Dirs()

- int FramesPerDir()

- int Fps()

- int LoopMode()

- int LoopStart()


## AnimPlayer (class)

- AnimClip clip;

- int dir;

- int frame;

- int accMs;

- bool done;

- AnimPlayer()

- void Play(AnimClip c)

- void SetDir(int d)

- int Dir()

- bool Done()

- int Frame()

- void Update(int dtMs)

- int SheetFrame()


## Camera2D (class)

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

- void Update(double dt)

- void Follow(double targetX, double targetY, double smoothSpeed, double dt)

- void ClampToBounds()

- void WorldToScreen(double wx, double wy, out double sx, out double sy)

- void ScreenToWorld(double sx, double sy, out double wx, out double wy)

- bool IsVisible(double wx, double wy, double w, double h)


## Entity (class)

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

- void SnapshotHistory()

- virtual void OnAwake(Scene scene)

- virtual void OnFixedUpdate(Scene scene, double dt)

- virtual void OnRender(Scene scene, Canvas c, double interpX, double interpY)

- virtual void OnDestroy(Scene scene)


## GameClock (class)

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

- int Advance()

- double InterpolationAlpha()


## GameViewport (class)

- Scene activeScene;

- InputMapper input;

- HudRenderFn onRenderHudCallback;

- bool autoDriveClock;

- bool hostDriven;

- Canvas off;

- int offWorldGen;

- bool offValid;

- GameViewport(Scene scene, int dockMode)

- static GameViewport Create(Scene scene)

- Scene CurrentScene{ get set}

- InputMapper Input { get }

- bool AutoDriveClock{ get set}

- void MarkHostDriven()

- void SetHudRenderer(HudRenderFn hudCallback)

- void StepLogic()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void RecreateOff(int w, int h)


## Scene (class)

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

- Dictionary <int, Entity> entityById;

- int nextEntityId;

- int worldGen;

- Scene(string name, int viewW, int viewH, int maxCap)

- static Scene Create(string name, int viewW, int viewH, int maxCap)

- string Name { get }

- Camera2D Camera { get }

- SpatialHash2D SpatialGrid { get }

- GameClock Clock { get }

- int EntityCount { get }

- int NextId()

- void AddEntity(Entity e)

- void FlushPendingAdd()

- void AdvanceFixedStep(double fixedDt)

- int Tick()

- int WorldGen()

- void Render(Canvas c)

- Entity FindById(int id)


## SceneManager (class)

- Scene[]sceneStack;

- int stackTop;

- TransitionPhase phase;

- double fadeDuration;

- double fadeTimer;

- double fadeAlpha;

- Scene pendingNextScene;

- bool popPendingOnSwitch;

- AudioTransitionFn onAudioTransitionCallback;

- SceneManager(int maxStackDepth)

- static SceneManager Create(int maxStackDepth)

- TransitionPhase Phase { get }

- double FadeAlpha { get }

- bool IsTransitioning { get }

- Scene GetActiveScene()

- void Push(Scene newScene)

- Scene Pop()

- void SetAudioTransitionCallback(AudioTransitionFn callback)

- void SwitchScene(Scene nextScene, double duration)

- void Update(double dt)

- void Render(Canvas c, double viewportWidth, double viewportHeight)


## SpatialHash2D (class)

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

- int[]usedBuckets;

- int usedCount;

- double maxInsertRadius;

- SpatialHash2D(int cellSize, int maxEntities)

- static SpatialHash2D Create(int cellSize, int maxCapacity)

- void Clear()

- int HashCoords(int cellX, int cellY)

- bool Insert(int id, double x, double y, double r)

- int QueryRange(double qx, double qy, double qRadius, int[]outIds, int maxOut)

- int FindNearest(double qx, double qy, double maxRange)


## void (delegate)

`delegate void HudRenderFn(Canvas canvas, int width, int height);`


## void (delegate)

`delegate void AudioTransitionFn(int fadeOutMs, int fadeInMs);`


## TransitionPhase (enum)

- None

- FadeOut

- Switching

- FadeIn = 渐亮显露新世界 (1.0 -> 0.0)
