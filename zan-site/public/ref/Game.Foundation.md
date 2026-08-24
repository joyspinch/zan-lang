# Game.Foundation

> 源码: `stdlib/Game/Foundation/Input.zan`, `stdlib/Game/Foundation/Scene.zan`, `stdlib/Game/Foundation/Timing.zan`


## DeterministicRandom (class)

小型可复现随机源，状态对外暴露，用于存档、
确定性回放与网络同步。

- long state;

- DeterministicRandom(long seed)

- long NextRaw()

- long Next()

- int NextBelow(int bound)

- int Between(int minimum, int maximumExclusive)

- double NextDouble()

- long State()

- void Restore(long state)


## FixedStepClock (class)

确定性固定步长累加器。模拟代码在 HasStep() 为真时反复调用 Step()，
并用 Alpha() 插值进行渲染。

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

可复用的一次性或循环毫秒定时器。

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

将后端按键码映射为语义动作。多个按键可对应同一个
动作，直接调用 SetAction 则支持触摸、手柄或 AI 输入。

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


## SceneStack (class)

基于栈的游戏状态管理者，用于菜单、加载画面、游戏过程、暂停
覆盖层与结算画面。

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


## IGameScene (interface)

- string Name();

- void Enter();

- void Exit();

- void Pause();

- void Resume();

- void FixedUpdate(int deltaMilliseconds);

- void Update(int deltaMilliseconds);
