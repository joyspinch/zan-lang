# Game.Foundation

> 源码: `packages/Zan.Game/src/Game/Foundation/AudioBus.zan`, `packages/Zan.Game/src/Game/Foundation/Input.zan`, `packages/Zan.Game/src/Game/Foundation/Pool.zan`, `packages/Zan.Game/src/Game/Foundation/Scene.zan`, `packages/Zan.Game/src/Game/Foundation/SpatialAudio2D.zan`, `packages/Zan.Game/src/Game/Foundation/SpringDamper.zan`, `packages/Zan.Game/src/Game/Foundation/Timing.zan`, `packages/Zan.Game/src/Game/Foundation/TrajectorySimulator.zan`, `packages/Zan.Game/src/Game/Foundation/Tween.zan`


## AudioBus (class)

- double masterVolume;

- double bgmVolume;

- double sfxVolume;

- double voiceVolume;

- bool masterMuted;

- bool bgmMuted;

- bool sfxMuted;

- bool voiceMuted;

- AudioVoice currentBgmVoice;

- AudioClip pendingBgmClip;

- int fadeOutDurationMs;

- int fadeInDurationMs;

- int fadeElapsedMs;

- double fadeStartGain;

- double fadeTargetGain;

- int fadePhase;

- Dictionary <string, int> sfxCooldowns;

- AudioBus()

- void SetMasterVolume(double volume)

- double MasterVolume()

- void SetBgmVolume(double volume)

- double BgmVolume()

- void SetSfxVolume(double volume)

- double SfxVolume()

- void SetVoiceVolume(double volume)

- double VoiceVolume()

- void SetMasterMuted(bool muted)

- bool IsMasterMuted()

- void SetBgmMuted(bool muted)

- bool IsBgmMuted()

- void SetSfxMuted(bool muted)

- bool IsSfxMuted()

- void SetVoiceMuted(bool muted)

- bool IsVoiceMuted()

- double EffectiveGain(int channel, double localGain)

- void PlayBgm(AudioClip clip, double gain)

- void StopBgm()

- void FadeOutBgm(int durationMs)

- void CrossFadeBgm(AudioClip nextClip, int fadeOutMs, int fadeInMs, double targetVolume)

- bool IsBgmFading()

- bool CanPlaySfx(string sfxKey, int cooldownMs)

- AudioVoice PlaySfxThrottled(string sfxKey, AudioClip clip, int cooldownMs, double gain)

- void Update(int deltaMs)

- void ApplyBgmVoiceGain()


## AudioChannel (class)

- const int Master=0;

- const int Bgm=1;

- const int Sfx=2;

- const int Voice=3;


## DeterministicRandom (class)

- long state;

- DeterministicRandom(long seed)

- long NextRaw()

- long Next()

- int NextBelow(int bound)

- int Between(int minimum, int maximumExclusive)

- double NextDouble()

- long State()

- void Restore(long state)


## EaseType (class)

- const int Linear=0;

- const int QuadIn=1;

- const int QuadOut=2;

- const int QuadInOut=3;

- const int CubicIn=4;

- const int CubicOut=5;

- const int CubicInOut=6;

- const int QuartIn=7;

- const int QuartOut=8;

- const int QuartInOut=9;

- const int QuintIn=10;

- const int QuintOut=11;

- const int QuintInOut=12;

- const int SineIn=13;

- const int SineOut=14;

- const int SineInOut=15;

- const int ExpoIn=16;

- const int ExpoOut=17;

- const int ExpoInOut=18;

- const int CircIn=19;

- const int CircOut=20;

- const int CircInOut=21;

- const int BackIn=22;

- const int BackOut=23;

- const int BackInOut=24;

- const int BounceIn=25;

- const int BounceOut=26;

- const int BounceInOut=27;

- const int ElasticIn=28;

- const int ElasticOut=29;

- const int ElasticInOut=30;


## Easing (class)

- const double PI=3.14159265358979323846;

- const double HALF_PI=1.57079632679489661923;

- static double Evaluate(int easeType, double t)

- static double BounceOutCore(double t)


## FixedStepClock (class)

- int stepMilliseconds;

- int maxStepsPerFrame;

- int accumulator;

- int pendingSteps;

- bool paused;

- double timeScale;

- FixedStepClock(int stepMilliseconds, int maxStepsPerFrame)

- int Advance(int deltaMilliseconds)

- bool HasStep()

- int ConsumeStep()

- int StepMilliseconds()

- int PendingSteps()

- double Alpha()

- void SetPaused(bool paused)

- bool Paused()

- void SetTimeScale(double scale)

- double TimeScale()

- void Reset()


## GameTimer (class)

- int duration;

- int remaining;

- bool repeating;

- bool running;

- int triggers;

- static GameTimer Once(int durationMilliseconds)

- static GameTimer Repeating(int intervalMilliseconds)

- GameTimer(int durationMilliseconds, bool repeating)

- int Tick(int deltaMilliseconds)

- void Restart()

- void Stop()

- bool Running()

- int Remaining()

- int Duration()

- int Triggers()


## InputActionState (class)

- string name;

- bool down;

- bool pressed;

- bool released;

- double amount;

- InputActionState(string name)

- string Name()

- bool Down()

- bool Pressed()

- bool Released()

- double Value()

- void BeginFrame()

- void Set(bool down, double amount)


## InputBinding (class)

- string action;

- int key;

- bool down;

- static InputBinding Key(string action, int key)

- string Action()

- int KeyCode()

- bool Down()

- void SetDown(bool down)


## InputMap (class)

- List<InputActionState> actions;

- List<InputBinding> bindings;

- InputMap()

- int ActionIndex(string name)

- InputActionState EnsureAction(string name)

- InputMap BindKey(string action, int key)

- void BeginFrame()

- void ProcessKey(int key, bool down)

- void RefreshAction(string action)

- void SetAction(string action, bool down, double amount)

- bool Down(string action)

- bool Pressed(string action)

- bool Released(string action)

- double Value(string action)

- void Clear()


## ObjectPool (class)

- List<T> items;

- int maxCapacity;

- ObjectPool(int maxCapacity)

- ObjectPool()

- int Count()

- T Rent()

- bool Return(T item)

- void Clear()


## SceneStack (class)

- List<IGameScene> scenes;

- SceneStack()

- void Push(IGameScene scene)

- IGameScene Pop()

- void Replace(IGameScene scene)

- IGameScene Current()

- int Count()

- void FixedUpdate(int deltaMilliseconds)

- void Update(int deltaMilliseconds)

- void Clear()


## SpatialAudio2D (class)

- double listenerX;

- double listenerY;

- double panSpanWidth;

- double minDistance;

- double maxDistance;

- AudioBus audioBus;

- SpatialAudio2D(AudioBus bus, double spanW, double minD, double maxD)

- static SpatialAudio2D Create(AudioBus bus, double spanW, double minD, double maxD)

- double ListenerX{ get set}

- double ListenerY{ get set}

- double MinDistance{ get set}

- double MaxDistance{ get set}

- void SetListenerPosition(double x, double y)

- void CalculateSpatial(double sourceX, double sourceY, double baseGain, out double outPan, out double outGain)

- AudioVoice PlayAt(AudioClip clip, double sourceX, double sourceY, double baseGain, int loop)


## SpringDamper (class)

- double current;

- double target;

- double velocity;

- double stiffness;

- double damping;

- SpringDamper(double initialValue, double stiffness, double damping)

- SpringDamper(double initialValue)

- void SetTarget(double target)

- void SnapTo(double val)

- void AddImpulse(double impulse)

- double Update(double dt)

- double GetValue()

- double GetVelocity()

- double GetTarget()

- bool IsAtRest(double tolerance)


## TrajectoryPoint (class)

- double time;

- double x;

- double y;

- double vx;

- double vy;

- TrajectoryPoint(double time, double x, double y, double vx, double vy)

- double GetTime()

- double GetX()

- double GetY()

- double GetVx()

- double GetVy()


## TrajectorySimulator (class)

- static List<TrajectoryPoint> SimulateJump(double gravity, double jumpVelocity, double airDrag, int maxSteps, double dt)

- static double[]AnalyzeJumpMetrics(double gravity, double jumpVelocity, double airDrag)


## Tween (class)

- double fromValue;

- double toValue;

- int durationMs;

- int delayMs;

- int easeType;

- int loopMode;

- int elapsedMs;

- bool finished;

- bool paused;

- bool reverse;

- double currentValue;

- Tween(double from, double to, int durationMs, int easeType)

- Tween SetDelay(int delayMs)

- Tween SetLoop(int loopMode)

- void Pause()

- void Resume()

- bool IsPaused()

- void Reset()

- void Complete()

- void Update(int deltaMs)

- double Value()

- double Progress()

- bool IsFinished()


## TweenGroup (class)

- List<Tween> tweens;

- TweenGroup()

- Tween Add(Tween tween)

- Tween To(double from, double to, int durationMs, int easeType)

- void Update(int deltaMs)

- int Count()

- void Clear()


## TweenLoop (class)

- const int Once=0;

- const int Loop=1;

- const int Yoyo=2;


## IGameScene (interface)

- string Name();

- void Enter();

- void Exit();

- void Pause();

- void Resume();

- void FixedUpdate(int deltaMilliseconds);

- void Update(int deltaMilliseconds);
