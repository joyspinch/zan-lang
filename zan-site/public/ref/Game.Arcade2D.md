# Game.Arcade2D

> 源码: `stdlib/Game/Arcade2D/Animation.zan`, `stdlib/Game/Arcade2D/Geometry.zan`, `stdlib/Game/Arcade2D/World.zan`


## AnimationClip (class)

- string name;

- bool loop;

- List<AnimationFrame> frames;

- AnimationClip(string name, bool loop)

- AnimationClip Add(int x, int y, int width, int height, int duration)

- string Name()

- bool Loop()

- int FrameCount()

- AnimationFrame FrameAt(int index)


## AnimationFrame (class)

- int x;

- int y;

- int width;

- int height;

- int duration;

- AnimationFrame(int x, int y, int width, int height, int duration)

- int X()

- int Y()

- int Width()

- int Height()

- int Duration()


## AnimationPlayer (class)

- AnimationClip clip;

- int frameIndex;

- int elapsed;

- bool playing;

- bool finished;

- AnimationPlayer(AnimationClip clip)

- void Play(AnimationClip clip)

- void Update(int deltaMilliseconds)

- AnimationFrame Current()

- int FrameIndex()

- bool Playing()

- bool Finished()


## ArcadeEntity (class)

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

- void Step(int deltaMilliseconds)

- void MoveTowards(double targetX, double targetY, double speed)

- int Damage(int amount)

- Rect2 Bounds()

- Circle2 Circle()

- int Id()

- int Kind()

- int Team()

- bool Active()

- double X()

- double Y()

- double PreviousX()

- double PreviousY()

- double VelocityX()

- double VelocityY()

- double Width()

- double Height()

- double Radius()

- int Hp()

- int MaxHp()

- int TargetId()

- int Cooldown()

- int Lifetime()

- int PathIndex()

- int Value()

- void SetActive(bool active)

- void SetPosition(double x, double y)

- void SetVelocity(double x, double y)

- void SetRadius(double radius)

- void SetHealth(int hp)

- void SetTargetId(int targetId)

- void SetCooldown(int cooldown)

- void SetLifetime(int lifetime)

- void SetPathIndex(int pathIndex)

- void SetValue(int data)


## ArcadePath (class)

- List<Vector2> points;

- ArcadePath()

- ArcadePath Add(double x, double y)

- bool Follow(ArcadeEntity entity, double speed, double tolerance)

- int Count()

- Vector2 At(int index)


## ArcadeWorld (class)

复用实体存储，供自动战斗、塔防、粒子及
轻量竞技场游戏使用；非活跃槽位在 Spawn 时回收。

- List<ArcadeEntity> entities;

- int nextId;

- ArcadeWorld()

- ArcadeEntity Spawn(int kind, int team, double x, double y, double width, double height)

- void Update(int deltaMilliseconds)

- ArcadeEntity FindById(int id)

- ArcadeEntity FindClosestEnemy(ArcadeEntity source, double maximumDistance)

- int CountActive(int kind)

- int CountTeam(int team)

- int Capacity()

- ArcadeEntity At(int index)


## Camera2D (class)

- double x;

- double y;

- double zoom;

- int viewportWidth;

- int viewportHeight;

- Camera2D(int viewportWidth, int viewportHeight)

- void CenterOn(double x, double y)

- void SetPosition(double x, double y)

- void SetZoom(double zoom)

- int ScreenX(double worldX)

- int ScreenY(double worldY)

- double WorldX(double screenX)

- double WorldY(double screenY)

- double X()

- double Y()

- double Zoom()


## Circle2 (class)

- double x;

- double y;

- double radius;

- Circle2(double x, double y, double radius)

- double X()

- double Y()

- double Radius()


## Collision2D (class)

- static bool PointInRect(double x, double y, Rect2 rect)

- static bool Rects(Rect2 a, Rect2 b)

- static bool Circles(Circle2 a, Circle2 b)

- static double DistanceSquared(double ax, double ay, double bx, double by)


## Rect2 (class)

- double x;

- double y;

- double width;

- double height;

- Rect2(double x, double y, double width, double height)

- double X()

- double Y()

- double Width()

- double Height()

- double Right()

- double Bottom()


## Vector2 (class)

- double x;

- double y;

- Vector2(double x, double y)

- double X()

- double Y()

- void Set(double x, double y)

- void Add(double x, double y)
