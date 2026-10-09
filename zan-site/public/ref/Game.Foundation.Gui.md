# Game.Foundation.Gui

> 源码: `packages/Zan.Game/src/Game/Foundation/Gui/Host.zan`, `packages/Zan.Game/src/Game/Foundation/Gui/HudLayer.zan`, `packages/Zan.Game/src/Game/Foundation/Gui/SceneRouter.zan`


## GuiHost (class)

- string title;

- int logicalWidth;

- int logicalHeight;

- int targetFrameMilliseconds;

- App app;

- InputMap input;

- FixedStepClock clock;

- HudLayer hud;

- bool running;

- int lastTick;

- List<GameViewport> viewports;

- GuiHost(string title, int logicalWidth, int logicalHeight, int fixedStepMilliseconds)

- HudLayer Hud()

- void AttachViewport(GameViewport vp)

- bool Run(IGuiHostLoop loop)

- void DispatchKeyEvent(IGuiHostLoop loop)

- void RequestStop()

- bool Running()

- InputMap Input()

- int Width()

- int Height()

- App App()

- int ContentTop()

- int StageWidth()

- int StageHeight()

- int MouseX()

- int MouseY()


## HtmlScene (class)

- SceneRouter router;

- virtual string HtmlFile()

- virtual void OnWire(HtmlHandlers handlers)

- virtual void OnEnter(Control root)

- virtual void OnExit()

- virtual void Event(int kind, int key)

- virtual void FixedStep(int dtMs)

- virtual void Update(int dtMs)

- virtual void Render(Canvas c, double alpha)


## HudLayer (class)

- List<Control> controls;

- bool visible;

- HudLayer()

- bool IsVisible()

- void SetVisible(bool v)

- void Add(Control control)

- bool Remove(Control control)

- void Clear()

- int Count()

- void Render(App app, Canvas canvas)

- void RenderViewport(App app, Canvas canvas, int vw, int vh)


## SceneRouter (class)

- GuiHost host;

- Dict <string, HtmlScene> scenes;

- HtmlScene current;

- string currentName;

- string pendingName;

- Control hudRoot;

- string baseDir;

- bool installTried;

- static SceneRouter Create(string title, int w, int h)

- static SceneRouter CreateFixed(string title, int w, int h, int stepMs)

- SceneRouter(GuiHost host)

- void Register(string name, HtmlScene scene)

- void SetBaseDir(string dir)

- bool Go(string name)

- void Exit()

- bool Run(string initial)

- HtmlScene Current()

- string CurrentName()

- GuiHost Host()

- void Start(GuiHost host)

- void Event(GuiHost host, int kind, int key)

- void FixedUpdate(GuiHost host, int deltaMilliseconds)

- void Update(GuiHost host, int deltaMilliseconds)

- void Render(GuiHost host, Canvas c, double alpha)

- void Stop(GuiHost host)

- void ApplyPending()

- Control LoadSceneHtml(string file, HtmlScene scene)

- virtual void OnStart(GuiHost host)

- virtual void OnEvent(int kind, int key)

- virtual void OnFixedStep(int dtMs)

- virtual void OnFrameUpdate(int dtMs)

- virtual void OnStop(GuiHost host)


## IGuiHostLoop (interface)

- void Start(GuiHost host);

- void Event(GuiHost host, int kind, int keycode);

- void FixedUpdate(GuiHost host, int deltaMilliseconds);

- void Update(GuiHost host, int deltaMilliseconds);

- void Render(GuiHost host, Canvas c, double alpha);

- void Stop(GuiHost host);
