# Gui.Animation

> 源码: `stdlib/Gui/Animation/Easing.zan`, `stdlib/Gui/Animation/SpriteAnimator.zan`, `stdlib/Gui/Animation/Timeline.zan`, `stdlib/Gui/Animation/Tween.zan`


## EaseType (class)

缓动类型常量（A356 P0 动画引擎基座）。
包含常用曲线族：线性、二次、三次、四次、正弦、指数、圆弧、回弹、弹跳与弹性。

- public const int Linear=0;

- public const int QuadIn=1;

- public const int QuadOut=2;

- public const int QuadInOut=3;

- public const int CubicIn=4;

- public const int CubicOut=5;

- public const int CubicInOut=6;

- public const int QuartIn=7;

- public const int QuartOut=8;

- public const int QuartInOut=9;

- public const int SineIn=10;

- public const int SineOut=11;

- public const int SineInOut=12;

- public const int ExpoIn=13;

- public const int ExpoOut=14;

- public const int ExpoInOut=15;

- public const int CircIn=16;

- public const int CircOut=17;

- public const int CircInOut=18;

- public const int BackIn=19;

- public const int BackOut=20;

- public const int BackInOut=21;

- public const int BounceOut=22;

- public const int BounceIn=23;

- public const int BounceInOut=24;

- public const int ElasticOut=25;

- public const int ElasticIn=26;

- public const int ElasticInOut=27;


## Easing (class)

标准缓动方程求值器。将 [0, 1] 的归一化进度映射为指定曲线的插值因子。

- const float PI=3.14159265f;

- const float HALF_PI=1.57079632f;

- public static float Evaluate(int easeType, float t)
  - 对指定缓动类型计算归一化时间 t 在 [0, 1] 上的插值因子。

- static float BounceOutCore(float t)


## SpriteAnimator (class)

精灵序列帧动画（A356 P0 动画引擎基座）：
支持图集网格切片（行列网格）或自定义帧序列。
与 `SpriteBatch` 协同，当前帧零额外分配直接提交。

- public int spriteHandle;

- public int cols;

- public int rows;

- public float cellW;

- public float cellH;

- public int startFrame;

- public int frameCount;

- public int frameDurationMs;

- public bool loop;

- public int currentFrame;

- public int elapsedInFrame;

- public bool active;

- public bool finished;

- public Action completeAction;

- public SpriteAnimator()

- public static SpriteAnimator CreateGrid(int handle, int cols, int rows, float cellW, float cellH, int startFrame, int frameCount, int frameDurationMs, bool loop)
  - 创建规则网格序列帧动画。

- public void Play()
  - 播放动画。

- public void Stop()
  - 停止并重置动画。

- public bool Step(int deltaMs)
  - 推进时间 deltaMs。返回是否仍在播放。

- public void Draw(SpriteBatch sb, float dx, float dy, float dw, float dh, int tint)
  - 把当前帧装入 SpriteBatch。


## Timeline (class)

动画时间轴与调度器（A356 P0 动画引擎基座）：
集中管理所有活跃的值动画与精灵动画。
与 App 联动实现"有动画自动出帧、无动画彻底静默"的按需重绘，
并提供对象池避免高频飘字、掉落动画的垃圾颠簸。

- static Timeline shared;

- public static Timeline Shared { get }

- List<Tween> tweens;

- List<SpriteAnimator> animators;

- List<Tween> tweenPool;

- App boundApp;

- public Timeline()

- public void BindApp(App app)
  - 绑定主应用窗口。动画更新时自动触发 RequestRedraw。

- public bool HasActive()
  - 是否有动画正在活跃播放。

- public Tween ObtainTween(float fromVal, float toVal, int durationMs, int easeType, TweenUpdate onUpdate)
  - 从对象池获取或新建一个 Tween。

- public static Tween To(float fromVal, float toVal, int durationMs, int easeType, TweenUpdate onUpdate)
  - 启动一个值动画并纳入调度。

- public void Add(Tween t)
  - 添加一个值动画到时间轴。

- public void Add(SpriteAnimator a)
  - 添加一个序列帧动画到时间轴。

- public void Remove(Tween t)
  - 移除一个值动画并归还池。

- public void Remove(SpriteAnimator a)
  - 移除一个序列帧动画。

- public void Clear()
  - 清空所有活跃动画。

- public void Tick(int deltaMs)
  - 推进所有动画一个时间步（毫秒）。
    如果有动画仍在运行，自动通知 boundApp 触发下一帧重绘。


## Tween (class)

通用值动画（A356 P0 动画引擎基座）：把浮点值从 from 缓动到 to。
既可由 Timeline 自动按需出帧调度，也可脱机手动 Step(dt)。

- public float fromVal;

- public float toVal;

- public int durationMs;

- public int elapsedMs;

- public int easeType;

- public bool active;

- public bool finished;

- public TweenUpdate updateAction;

- public TweenCallback completeAction;

- public Tween()

- public static Tween Create(float fromVal, float toVal, int durationMs, int easeType, TweenUpdate onUpdate)
  - 便捷创建并配置值动画。

- public Tween OnComplete(TweenCallback onComplete)
  - 配置完成时的回调。

- public Tween Play()
  - 开始播放动画。

- public void Pause()
  - 暂停动画。

- public void Resume()
  - 恢复播放。

- public void Stop()
  - 停止并重置动画。

- public bool Step(int deltaMs)
  - 推进时间 deltaMs。返回是否仍在活跃播放。


## void (delegate)

值动画更新回调。

`delegate void TweenUpdate(float value);`


## void (delegate)

值动画完成回调。

`delegate void TweenCallback();`
