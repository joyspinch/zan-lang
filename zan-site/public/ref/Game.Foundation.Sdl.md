# Game.Foundation.Sdl

> 源码: `stdlib/Game/Foundation/Sdl/Host.zan`


## GameHost (class)

SDL3 宿主，提供确定性固定更新、语义化输入状态与
逻辑分辨率呈现。

- string title;

- int logicalWidth;

- int logicalHeight;

- int targetFrameMilliseconds;

- SdlWindow window;

- SdlRenderer renderer;

- InputMap input;

- FixedStepClock clock;

- bool running;

- bool verticalSync;

- int lastTick;

- GameHost(string title, int logicalWidth, int logicalHeight, int fixedStepMilliseconds)

- bool Open()

- bool Run(IGameHostLoop loop)

- void Close()

- void RequestStop()

- bool Running()

- InputMap Input()

- SdlWindow Window()

- SdlRenderer Renderer()

- int Width()

- int Height()

- void SetVerticalSync(bool enabled)


## IGameHostLoop (interface)

- void Start(GameHost host);

- void Event(GameHost host, int eventType);

- void FixedUpdate(GameHost host, int deltaMilliseconds);

- void Update(GameHost host, int deltaMilliseconds);

- void Render(GameHost host, SdlRenderer renderer, double alpha);

- void Stop(GameHost host);
