# Gui.Animation

> 源码: `packages/Zan.Gui/src/Gui/Animation/Easing.zan`, `packages/Zan.Gui/src/Gui/Animation/SpriteAnimator.zan`, `packages/Zan.Gui/src/Gui/Animation/Timeline.zan`, `packages/Zan.Gui/src/Gui/Animation/Tween.zan`


## EaseType (class)

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

- const float PI=3.14159265f;

- const float HALF_PI=1.57079632f;

- public static float Evaluate(int easeType, float t)

- static float BounceOutCore(float t)


## SpriteAnimator (class)

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

- public void Play()

- public void Stop()

- public bool Step(int deltaMs)

- public void Draw(SpriteBatch sb, float dx, float dy, float dw, float dh, int tint)


## Timeline (class)

- static Timeline shared;

- public static Timeline Shared { get }

- List<Tween> tweens;

- List<SpriteAnimator> animators;

- List<Tween> tweenPool;

- App boundApp;

- public Timeline()

- public void BindApp(App app)

- public bool HasActive()

- public Tween ObtainTween(float fromVal, float toVal, int durationMs, int easeType, TweenUpdate onUpdate)

- public static Tween To(float fromVal, float toVal, int durationMs, int easeType, TweenUpdate onUpdate)

- public void Add(Tween t)

- public void Add(SpriteAnimator a)

- public void Remove(Tween t)

- public void Remove(SpriteAnimator a)

- public void Clear()

- public void Tick(int deltaMs)


## Tween (class)

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

- public Tween OnComplete(TweenCallback onComplete)

- public Tween Play()

- public void Pause()

- public void Resume()

- public void Stop()

- public bool Step(int deltaMs)


## void (delegate)

`delegate void TweenUpdate(float value);`


## void (delegate)

`delegate void TweenCallback();`
