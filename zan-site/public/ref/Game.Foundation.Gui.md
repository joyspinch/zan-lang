# Game.Foundation.Gui

> 源码: `packages/Zan.Game/src/Game/Foundation/Gui/Host.zan`, `packages/Zan.Game/src/Game/Foundation/Gui/HudLayer.zan`


## GuiHost (class)

GUI 运行时宿主：确定性固定更新、语义化输入状态、连续帧驱动
（SetPollEventMode + 每帧 RequestRedraw，由 App 的 16ms 帧预算
自限速在约 60fps）。窗口经 App.CreateDarkStage 创建——统一 DPI
契约：不做显示器 DPI 放大，游戏舞台（内容区）尺寸恒等于请求的
逻辑分辨率，拖到任何缩放率的显示器都不变，输入坐标 1:1（高
DPI 屏上窗口视觉尺寸变小，同 SDL 时代的行为）。窗口模式带标准
Gui 标题栏（皮肤按钮默认关，宿主每帧在内容之上画 chrome，模板
绘制与鼠标 y 以 ContentTop() 为界）；全屏收起标题栏后舞台即整
个客户区。模板按固定逻辑分辨率绘制，用 canvas.Width()/Height()
取绘制范围。

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

- GuiHost(string title, int logicalWidth, int logicalHeight, int fixedStepMilliseconds)
  - fixedStepMilliseconds <= 0 时取 16ms（约 60Hz）。

- HudLayer Hud()
  - 获取 HUD 挂载层，供挂载标准 GUI 组件。

- bool Run(IGuiHostLoop loop)
  - 运行主循环直到退出：轮询事件 → 推进固定步长时钟 →
    依次回调 loop 的 Update/Render（在 App 的 BeginFrame/
    PresentFrame 之间）。App 帧预算把节奏压在约 60fps；
    关窗（Process 返回 false）结束循环。正常返回 true。

- void DispatchKeyEvent(IGuiHostLoop loop)
  - 输入事件转发：键盘喂语义输入映射，键盘与鼠标都回调 loop.Event。

- void RequestStop()
  - 请求退出主循环（下一圈生效）。

- bool Running()
  - 主循环是否仍在运行。

- InputMap Input()
  - 语义输入映射（模板用 BindKey 绑定动作到 Gui 键码）。

- int Width()
  - 逻辑宽度（创建时请求；实际绘制范围看 canvas.Width()）。

- int Height()
  - 逻辑高度。

- App App()
  - 宿主的 App（Start 之后有效；一般只在需要控件/皮肤时用）。

- int ContentTop()
  - 内容区顶部。视口契约下舞台坐标 0 基即内容区顶（桌面标题栏的
    偏移已折进 CDraw.vpY；手机全屏无 chrome）——模板的
    Origin(0, ContentTop()) 自然退化为 0。

- int StageWidth()
  - 延展后的逻辑舞台尺寸：短轴贴设计值、长轴随窗口比例变大
    （StageViewport 每帧帧首更新）。模板 Render 开头把它赋给自己
    的 W/H 静态量，布局全部锚定 W/H 即自适应任意窗口。

- int StageHeight()

- int MouseX()
  - 指针的舞台（逻辑）坐标：桌面客户区像素经 CDraw 视口反变换；
    手机上驱动交还原表面坐标，由同一视口反变换。

- int MouseY()


## HudLayer (class)

声明式游戏 HUD 挂载层。
允许在游戏世界画布之上挂载标准的 Zan GUI 控件（按钮、背包面板、血条、设置对话框等）。
既享受游戏循环的高帧率与低延迟，又拥有标准 GUI 组件的精美排版与事件响应。

- List<Control> controls;

- bool visible;

- HudLayer()

- bool IsVisible()
  - HUD 是否可见。

- void SetVisible(bool v)
  - 设置 HUD 显示/隐藏。

- void Add(Control control)
  - 挂载一个 GUI 控件至 HUD 层。

- bool Remove(Control control)
  - 从 HUD 层移除控件。

- void Clear()
  - 清空所有 HUD 控件。

- int Count()
  - 当前挂载的控件数量。

- void Render(App app, Canvas canvas)
  - 在游戏画面渲染完成后调用，将 HUD 控件分层叠画至画布上。


## IGuiHostLoop (interface)

GUI 运行时应用主循环回调。宿主在 Run() 里按固定顺序回调：
Start → 每帧 { Event* → FixedUpdate×N → Update → Render } → Stop。
* Event 对每个输入事件各调一次（kind 1=鼠标移动 2=鼠标按下
3=鼠标释放 4=键按下 5=键抬起——Win32Shell/App 控件分发同一
编码，keycode/按钮见 Event 参数）；FixedUpdate 一帧内可能触发
多次（追帧）也可能零次；Render 的 alpha 是 0..1 的帧间插值系数。
渲染回调给 Gui.Canvas（软件光栅 + 各平台原生 present）而非
SdlRenderer——窗口、事件泵与呈现全部由 stdlib/Gui 的原生
外壳承担（Windows GDI / Linux X11 / macOS / OHOS / Android
NativeActivity+GL），不依赖 SDL。

- void Start(GuiHost host);
  - 主循环开始前调用一次（窗口已就绪）。

- void Event(GuiHost host, int kind, int keycode);
  - 每个输入事件回调一次：kind 1=鼠标移动，2=鼠标按下，3=鼠标
    释放，keycode 传按钮索引（0=左键、1=右键）；kind 4=键按下，
    5=键抬起，keycode 是 Gui 的键码空间（Windows VK：8=退格、
    13=回车、32=空格、方向键 37..40 等）。鼠标坐标直接读
    host.App().mouseX/mouseY。

- void FixedUpdate(GuiHost host, int deltaMilliseconds);
  - 固定步长模拟回调（步长毫秒数由构造参数决定）。

- void Update(GuiHost host, int deltaMilliseconds);
  - 每帧可变更新回调；deltaMilliseconds 是真实帧间隔。

- void Render(GuiHost host, Canvas c, double alpha);
  - 每帧渲染回调；alpha 为固定步之间的插值系数 0..1。传入的
    Canvas 是当帧唯一有效画布：移动端表面晚于 Start 到达（转向、
    后台往返亦然）时 App 会整体换新画布对象（App.SwapCanvas），
    Start 里抓的引用画进已销毁的旧表面、永不 present——绘制一律
    用本参数，或每帧把参数同步给游戏对象（this.g.c = c）。

- void Stop(GuiHost host);
  - 主循环结束后调用一次（窗口即将销毁）。
