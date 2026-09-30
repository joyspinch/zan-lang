# Game.Foundation

> 源码: `packages/Zan.Game/src/Game/Foundation/AudioBus.zan`, `packages/Zan.Game/src/Game/Foundation/Input.zan`, `packages/Zan.Game/src/Game/Foundation/Pool.zan`, `packages/Zan.Game/src/Game/Foundation/Scene.zan`, `packages/Zan.Game/src/Game/Foundation/SpatialAudio2D.zan`, `packages/Zan.Game/src/Game/Foundation/SpringDamper.zan`, `packages/Zan.Game/src/Game/Foundation/Timing.zan`, `packages/Zan.Game/src/Game/Foundation/TrajectorySimulator.zan`, `packages/Zan.Game/src/Game/Foundation/Tween.zan`


## AudioBus (class)

游戏级音频总线与混音管理器。
提供分级通道音量控制、独立静音、BGM 平滑淡入淡出、以及音效防爆音限频机制。

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
  - 计算某通道当前声音的实际有效增益（考虑主音量、通道音量与静音状态）。

- void PlayBgm(AudioClip clip, double gain)
  - 立即播放背景音乐（循环）。

- void StopBgm()
  - 停止当前 BGM。

- void FadeOutBgm(int durationMs)
  - 触发 BGM 平滑淡出。

- void CrossFadeBgm(AudioClip nextClip, int fadeOutMs, int fadeInMs, double targetVolume)
  - 平滑淡出当前音乐并淡入新音乐。

- bool IsBgmFading()

- bool CanPlaySfx(string sfxKey, int cooldownMs)
  - 检查指定音效是否允许播放（若距上次播放超过 cooldownMs 则允许并刷新时间戳）。

- AudioVoice PlaySfxThrottled(string sfxKey, AudioClip clip, int cooldownMs, double gain)
  - 播放一次性音效，带防爆音限频过滤。

- void Update(int deltaMs)
  - 帧循环推进：负责更新淡入淡出插值与音效冷却倒计时。

- void ApplyBgmVoiceGain()


## AudioChannel (class)

音频总线通道标识。

- const int Master=0;

- const int Bgm=1;

- const int Sfx=2;

- const int Voice=3;


## DeterministicRandom (class)

小型可复现随机源（PCG 线性同余变体），状态对外暴露——保存
state 即可完整恢复随机序列，用于存档、确定性回放与网络同步。
同一种子的序列永远一致；种子 0 视为 1（避免全零序列）。

- long state;

- DeterministicRandom(long seed)
  - 用种子构造；种子 0 自动改为 1。

- long NextRaw()
  - 原始 64 位 LCG 状态推进（含符号位，可直接做哈希/噪声）。

- long Next()
  - 非负 64 位随机数（0..2^63-1）。

- int NextBelow(int bound)
  - [0, bound) 的随机整数；bound≤0 返回 0。

- int Between(int minimum, int maximumExclusive)
  - [minimum, maximumExclusive) 的随机整数；
    上界不大于下界时恒返回 minimum。

- double NextDouble()
  - [0,1) 的随机小数（精度 1e-6）。

- long State()
  - 当前 LCG 状态（存档这个值即可完整恢复序列）。

- void Restore(long state)
  - 恢复到指定状态（0 自动改为 1），后续序列与保存时一致。


## EaseType (class)

缓动类型枚举常数。
涵盖主流曲线族：线性、二次、三次、四次、五次、正弦、指数、圆弧、回弹、弹跳与弹性。

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

标准缓动方程求值器。将 [0, 1] 的线性归一化进度映射为指定曲线的插值因子。

- const double PI=3.14159265358979323846;

- const double HALF_PI=1.57079632679489661923;

- static double Evaluate(int easeType, double t)
  - 对指定缓动类型计算归一化时间 t 在 [0, 1] 上的插值因子。
    若 t <= 0 返回 0；t >= 1 返回 1（弹性/回弹曲线中间允许超出边界）。

- static double BounceOutCore(double t)


## FixedStepClock (class)

确定性固定步长累加器。每帧 Advance(真实帧间隔)，然后在
HasStep() 为真时反复 ConsumeStep() 驱动模拟；渲染插值用
Alpha()。单帧补步数有上限（防死亡螺旋），时间缩放只影响
累加速度。

- int stepMilliseconds;

- int maxStepsPerFrame;

- int accumulator;

- int pendingSteps;

- bool paused;

- double timeScale;

- FixedStepClock(int stepMilliseconds, int maxStepsPerFrame)

- int Advance(int deltaMilliseconds)
  - 累加一帧的真实毫秒数并换算成本帧待执行的固定步数。
    缩放后的时间超过 步长×每帧上限 时截断（补步最多
    maxStepsPerFrame 个），避免卡顿后一帧内狂补模拟。
    暂停中或 delta≤0 返回 0。每次调用会先清空上次余量。

- bool HasStep()
  - 本帧是否还有未消费的固定步。

- int ConsumeStep()
  - 消费一个固定步，返回其毫秒数（恒等于步长，供模拟代码
    做确定性积分）；无步可消费时返回 0。

- int StepMilliseconds()
  - 固定步长毫秒数（构造时 ≤0 钳为 16）。

- int PendingSteps()
  - 本帧剩余待执行的固定步数。

- double Alpha()
  - 渲染插值系数：accumulator/步长，0..1。渲染时用
    前后两次模拟状态按 alpha 混合可消除固定步的颗粒感。

- void SetPaused(bool paused)
  - 暂停/恢复累加（暂停时 Advance 直接返回 0）。

- bool Paused()
  - 是否暂停中。

- void SetTimeScale(double scale)
  - 时间流速倍率，影响 Advance 的累加速度（0=冻结，1=正常，
    2=两倍速）。超出 [0,8] 钳到边界；不影响已累计的余量。

- double TimeScale()
  - 当前时间流速。

- void Reset()
  - 清零累计余量与待执行步数（不影响暂停/流速设置）。


## GameTimer (class)

可复用的一次性或循环毫秒定时器。

- int duration;

- int remaining;

- bool repeating;

- bool running;

- int triggers;

- static GameTimer Once(int durationMilliseconds)
  - 创建一次性定时器（触发一次后自动停止）。

- static GameTimer Repeating(int intervalMilliseconds)
  - 创建循环定时器（按间隔反复触发）。

- GameTimer(int durationMilliseconds, bool repeating)

- int Tick(int deltaMilliseconds)
  - 推进定时器并返回本次触发的次数（一帧可能触发多次）。
    每次调用都会重置触发计数。已停止或 delta≤0 返回 0；
    一次性定时器触发后自动停止。

- void Restart()
  - 重置到满时长并恢复运行（触发计数清零）。

- void Stop()
  - 停止定时器（Restart 可再启动）。

- bool Running()
  - 是否运行中。

- int Remaining()
  - 距下次触发的剩余毫秒。

- int Duration()
  - 定时时长/循环间隔毫秒（构造时 ≤0 钳为 1）。

- int Triggers()
  - 最近一次 Tick 的触发次数。


## InputActionState (class)

单个语义动作的运行时状态。pressed/released 是边沿标志：
Set 只在按下的转换沿置位，之后保持锁存，直到 BeginFrame 清除，
因此每帧开头必须调用一次 BeginFrame 才能读到正确的边沿。

- string name;

- bool down;

- bool pressed;

- bool released;

- double amount;

- InputActionState(string name)

- string Name()
  - 动作名，即创建时的标识符。

- bool Down()
  - 当前是否处于按住状态（持续为 true，非边沿）。

- bool Pressed()
  - 本帧内是否发生了按下动作；锁存直到 BeginFrame。

- bool Released()
  - 本帧内是否发生了抬起动作；锁存直到 BeginFrame。

- double Value()
  - 模拟量强度（键盘按下为 1.0，抬起时清零）。

- void BeginFrame()
  - 清除 pressed/released 边沿标志；每帧开头调用一次。

- void Set(bool down, double amount)
  - 更新状态并做边沿检测：down 从 false 变 true 置 pressed，
    从 true 变 false 置 released 并将强度清零。


## InputBinding (class)

一个"动作-按键"绑定项；用静态 Key 工厂创建。

- string action;

- int key;

- bool down;

- static InputBinding Key(string action, int key)
  - 创建绑定：动作名 + 后端按键码。

- string Action()
  - 绑定的动作名。

- int KeyCode()
  - 绑定的后端按键码。

- bool Down()
  - 该按键当前是否按住。

- void SetDown(bool down)
  - 由 InputMap 内部更新；一般不必直接调用。


## InputMap (class)

将后端按键码映射为语义动作。多个按键可对应同一个
动作，直接调用 SetAction 则支持触摸、手柄或 AI 输入。

- List<InputActionState> actions;

- List<InputBinding> bindings;

- InputMap()

- int ActionIndex(string name)
  - 动作的下标；不存在返回 -1。

- InputActionState EnsureAction(string name)
  - 取动作状态，不存在则先创建（首次查询即注册）。

- InputMap BindKey(string action, int key)
  - 绑定按键到动作，可链式调用；同名动作可绑定多个键。

- void BeginFrame()
  - 清除所有动作的 pressed/released 边沿；每帧开头调用。

- void ProcessKey(int key, bool down)
  - 后端按键事件入口：key 变化时刷新其绑定的所有动作。
    多键绑定同一动作时任一键按住即视为按下（OR 逻辑）。

- void RefreshAction(string action)
  - 按全部绑定重算动作状态；供 ProcessKey 内部调用。

- void SetAction(string action, bool down, double amount)
  - 直接设置动作状态：触摸、手柄、AI 等非键盘输入的入口，
    amount 为模拟量强度（键盘路径固定 1.0）。

- bool Down(string action)
  - 动作当前是否按住；未注册的动作返回 false。

- bool Pressed(string action)
  - 动作本帧是否按下过（边沿，锁存到 BeginFrame）。

- bool Released(string action)
  - 动作本帧是否抬起过（边沿，锁存到 BeginFrame）。

- double Value(string action)
  - 动作的模拟量强度；未注册的动作返回 0。

- void Clear()
  - 清空全部输入：所有绑定与动作复位为未按下。


## ObjectPool (class)

高性能通用对象池，供高频分配与丢弃的对象（粒子、弹幕、飘字文本、音效请求等）复用。
避免反复 new / ARC 回收带来的缓存未命中与内存抖动。

- List<T> items;

- int maxCapacity;

- ObjectPool(int maxCapacity)

- ObjectPool()

- int Count()
  - 当前池内空闲可用对象数。

- T Rent()
  - 从池中借出一个对象；若池为空返回 null，由调用方负责构造新实例。

- bool Return(T item)
  - 将使用完毕的对象归还池中；若池已达上限则直接丢弃交由垃圾回收。

- void Clear()
  - 清空池内所有缓存对象。


## SceneStack (class)

基于栈的游戏状态管理者，用于菜单、加载画面、游戏过程、暂停
覆盖层与结算画面。

- List<IGameScene> scenes;

- SceneStack()

- void Push(IGameScene scene)
  - 压入新场景：旧栈顶 Pause，新场景 Enter 后成为栈顶。
    null 直接忽略。

- IGameScene Pop()
  - 弹出栈顶场景（收到 Exit）并返回它；下层场景收到 Resume。
    栈空时返回 null 且无副作用。

- void Replace(IGameScene scene)
  - 弹出当前栈顶并压入新场景（旧栈顶 Exit、新场景 Enter）。

- IGameScene Current()
  - 当前栈顶场景；栈空返回 null。

- int Count()
  - 栈中场景数。

- void FixedUpdate(int deltaMilliseconds)
  - 把固定步长更新转发给栈顶场景（栈空时忽略）。

- void Update(int deltaMilliseconds)
  - 把每帧更新转发给栈顶场景（栈空时忽略）。

- void Clear()
  - 依次弹出全部场景（每个都收到 Exit），清空栈。


## SpatialAudio2D (class)

2.5D / 2D 空间立体声与声相衰减系统（Spatial Audio 2D）。
专为 ARPG 传奇打怪听声辨位、RTS 战场侧翼交火与射击弹道音效打造：
1. 距离平方反比与线性衰减（随与听者/相机中心距离降低增益）；
2. 屏幕左右声相偏转（Stereo Panning: -1.0 极左 ~ +1.0 极右）；
3. 等功率立体声能量法则（Constant Power Panning）；
4. 移动音源追踪与视区外超远距静音裁剪。

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
  - 计算指定世界坐标处的声相值与距离衰减增益。
    outPan: -1.0 (极左) ~ +1.0 (极右)
    outGain: 0.0 ~ 1.0

- AudioVoice PlayAt(AudioClip clip, double sourceX, double sourceY, double baseGain, int loop)
  - 在指定空间坐标处触发一次具有立体声空间感的音效。


## SpringDamper (class)

二阶弹簧阻尼振荡器（Spring-Damper Model）。
用于丝滑的相机跟随、UI 弹跳弹性动效、角色物理阻尼手感，彻底替代容易产生超调抖动的普通线性 Lerp。
基于隐式欧拉半积分数值解，在变帧率 dt 下仍保持极高数值稳定性。

- double current;

- double target;

- double velocity;

- double stiffness;

- double damping;

- SpringDamper(double initialValue, double stiffness, double damping)

- SpringDamper(double initialValue)

- void SetTarget(double target)
  - 设置目标位置（激励源）。

- void SnapTo(double val)
  - 瞬间重置当前位置与速度（如场景切换或瞬移）。

- void AddImpulse(double impulse)
  - 给系统施加一个瞬时冲量（如受击震动、开火后座力）。

- double Update(double dt)
  - 推进物理时间 dt 秒，返回当前帧插值后的值。

- double GetValue()

- double GetVelocity()

- double GetTarget()

- bool IsAtRest(double tolerance)
  - 判断振荡是否已基本静止（位置贴近且速度极小）。


## TrajectoryPoint (class)

轨迹采样点。

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

离线手感物理轨迹仿真器。
专供游戏手感调参（跳跃手感、重力加速度、阻力曲线），可在无游戏窗口环境下直接输出物理曲线指标。

- static List<TrajectoryPoint> SimulateJump(double gravity, double jumpVelocity, double airDrag, int maxSteps, double dt)
  - 仿真跳跃手感轨迹并输出诊断分析。
    gravity: 重力加速度 (如 980.0 px/s^2)
    jumpVelocity: 起跳初速度 (向上为负或正，此处统一按向上初速度标量值计算)
    airDrag: 空气阻力系数 (0.0 ~ 1.0)
    maxSteps: 最大仿真帧数 (如 120 帧，约 2 秒)
    dt: 单帧步长 (默认 1/60 秒)

- static double[]AnalyzeJumpMetrics(double gravity, double jumpVelocity, double airDrag)
  - 计算跳跃手感属性摘要：返回 [最高点高度(像素), 达到最高点耗时(秒), 滞空总时长(秒)]


## Tween (class)

独立的补间动画实例。管理从起始值向目标值的平滑过渡，
支持延迟等待、循环/往复、单步增量更新与完成态查询。

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
  - 创建数值补间动画实例。

- Tween SetDelay(int delayMs)
  - 设置延迟毫秒数

- Tween SetLoop(int loopMode)
  - 设置循环模式：Once / Loop / Yoyo

- void Pause()
  - 暂停补间更新

- void Resume()
  - 恢复补间更新

- bool IsPaused()
  - 是否处于暂停状态

- void Reset()
  - 重置补间回到初始状态

- void Complete()
  - 立即完成补间并跳至最终目标值

- void Update(int deltaMs)
  - 推进补间动画时间。以毫秒为增量。

- double Value()
  - 获取当前计算插值结果

- double Progress()
  - 获取归一化进度 [0.0, 1.0]

- bool IsFinished()
  - 是否已播放完毕


## TweenGroup (class)

补间动画管理器。集中批量推进多个动画，并在动效播放完毕后自动清理。

- List<Tween> tweens;

- TweenGroup()

- Tween Add(Tween tween)
  - 注册一个补间实例

- Tween To(double from, double to, int durationMs, int easeType)
  - 创建并添加一个缓动实例

- void Update(int deltaMs)
  - 集中推进所有补间并移除已完成的一次性动效

- int Count()
  - 当前托管的动效数量

- void Clear()
  - 清空所有托管的动效


## TweenLoop (class)

缓动循环模式。

- const int Once=0;

- const int Loop=1;

- const int Yoyo=2;


## IGameScene (interface)

栈式场景生命周期契约：只有栈顶场景接收 FixedUpdate/Update
（由 SceneStack 转发）。Push 时旧栈顶收到 Pause、本场景收到
Enter；Pop 时本场景收到 Exit、新栈顶收到 Resume。

- string Name();
  - 场景名（用于调试与存档定位）。

- void Enter();
  - 进入场景（Push/Replace 压入时调用一次）。

- void Exit();
  - 退出场景（Pop/Replace 弹出时调用一次）。

- void Pause();
  - 被新场景覆盖时调用（保持状态但不接收更新）。

- void Resume();
  - 覆盖场景弹出后恢复时调用。

- void FixedUpdate(int deltaMilliseconds);
  - 固定步长模拟回调，仅栈顶场景收到。

- void Update(int deltaMilliseconds);
  - 每帧可变更新回调，仅栈顶场景收到。
