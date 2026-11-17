# Zan 游戏开发——窗口输入与场景 UI

> 从 game-dev SKILL.md 拆出：设计稿界面方案、面板布局缩放、DPI 契约、输入事件、保留树点击排空、登录门、SceneRouter/HtmlScene 迁移。

## 游戏界面设计方案：菜单/面板走 .html 设计稿 + Nav 出口

放置/面板驱动类（legend、wuwei 这类"页签导航 + 全屏面板堆"，界面
代码占大头）按页面组织；RTS/动作类的 UI 同样使用标准控件，
世界渲染按前节边界执行。

- **每个页面一张 .html 设计稿**：布局/列表/样式进设计稿，数据绑定走
  `data-bind`/`data-for`（服务端 JSON 或本地模型都接 JsonValue），
  交互在 code-behind。五件声明原语与硬边界见 gui-design skill 的
  "HTML 窗口开发定式"（五件声明原语等；设计稿运行期加载，发布
  `--embed` 烘入，不是编译输入）。
- **页与页切换 = `Gui/Nav.zan` 的 `Nav.Embed`**（Tabs-as-outlet）：
  路由表对应按钮排（legend 的按钮本来就从 navigation.csv 读——
  `data-for` 一条渲染），中间区 = 出口；惰性实例化、默认切走保留
  （keepAlive）、可按路由配临态。别用 N 个布尔 data-if 硬拼页切换
  （data-if 只认路径真值，没有比较表达式）。
- **高频 HUD 仍用标准控件**：血条、伤害数字、小地图 UI 通过属性与
  数据更新，绘制封在组件内部；低频菜单与页面也采用同一范式。

## 面板布局与缩放边界

- HUD 与菜单优先 Flex/Grid/Flow；贴底采用有确定高度的 Flex 列，
  内容增长、页脚固定并按文档序排列。不要用 DockBottom 或数字 dock。
- 真正悬浮的控件使用 CSS absolute 或 `Float(x, y)`（当前转调 Place
  写 absolute 样式），按包含块定位；不再有“切手动停靠”步骤。
- 标准控件的 token、字号、间距由样式路径缩放一次，不再手乘 DPI。
  世界画布使用游戏视口坐标变换；控件内部绘制消费已解析的样式度量，
  不把世界坐标缩放 helper 用到 HUD 的 CSS 度量上。
- 验收在目标窗口/DPI 下同时核对实际控件矩形、文字、命中和世界视口；
  不能仅用世界截图推断 GUI 布局或缩放正确。

## 游戏舞台的统一 DPI 契约（GuiHost，脱 SDL 定式）

固定分辨率游戏走 `Game.Foundation.Gui.GuiHost` + `Game.Kit.CanvasPrims`，
**不要自创"物理像素窗口"路径**——三条坑都踩过：①普通 `CreateDark` 按显示
器 DPI 放大客户区，150% 屏上 1280x720 舞台只占 1920x1080 画布的左上角；
②自建物理像素窗口又丢了标题栏，且撞上工作区 82% 钳制（副屏把 1280x720
钳成 868x517）；③chrome 字号跟 dpiScale、标题条高度跟设备 DPI，两套来源
在舞台窗口里对不齐（32px 字挤 48px 条）。统一契约（已在 stdlib 实现，
模板只需遵守）：

- 窗口:`App.CreateDarkStage(title, w, h)`——客户区 = 舞台 w×h + 标准标题
  条（设备像素），不做 DPI 放大、不参与工作区/最小尺寸钳制、不做
  AdjustWindowRect 补偿（NCCALCSIZE 已把客户区扩成整窗）。
- 绘制:模板按 0 基舞台坐标作画，帧首 `CDraw.Origin(0, host.ContentTop)`，
  清屏用 `CDraw.Clear`（只铺内容区）；所有 C* 助手自动叠加原点，绕开 C*
  直调 canvas 的绘制会漏偏移。chrome 由宿主在 `loop.Render` 之后
  `RenderChrome` 叠画，皮肤按钮默认关；全屏（ContentTop==0）自然退化。
- 输入:鼠标是客户区坐标，y 减 `host.ContentTop` 换回舞台坐标。
- 验证:客户区物理尺寸应为 `w × (h + 32*dpi/96)`。

**非 DPI 感知进程的测量是假象**:150% 屏上 1280x768 物理窗会报成
853x512（÷1.5 虚拟化），别拿它反推"钳制/缩放 bug"——截图/测量脚本先
`SetProcessDpiAwarenessContext(-4)`。
同理，ctest 冒烟在并行会话构建时会假失败（共享 build\zanc.exe），单独
重跑一次再定论。

## GuiHost 输入事件契约：kind 1=移动 2=按下（文档曾写反）

IGuiHostLoop.Event 的 kind 编码与 Win32Shell/App 控件分发是同一套：
**1=鼠标移动、2=鼠标按下、3=鼠标释放、4=键按下、5=键抬起**。GuiHost
接口注释曾把 1/2 写反，六个游戏模板照错文档编码——鼠标移动被当点击、
真实点击被忽略："无输入自动放钩"类缺陷就来自（一次游离
WM_MOUSEMOVE 就放一钩），gomoku/ddz 真机鼠标操作等于乱落子/乱选牌。
修的是文档 + 同批修模板。键盘 keycode = Windows VK
（WM_KEYDOWN wParam）：空格 32、回车 13、Esc 27、方向键 37..40、
字母=大写 ASCII（P=80、Q=81、C=67）。接输入前先对 Win32Shell 的
Post 调用核对编码，别信二手注释。

## 游戏窗口保留树收不到点击：排空清掉事件状态（已修）+ UiDriver 点击验证配方

- **旧绘制消费机制的历史坑**：GuiHost 排空时的空轮询曾清掉原生事件槽，
  旧 Ui.Clicked 渲染期消费者因此读不到 release。HoldEventKind 只为这类兼容
  消费者保留帧内事件（无输入帧必须清零）；标准保留控件、HTML 和 GameViewport
  已在事件泵共享路由，不能再靠持住 kind 或绘制后桥接来接线。
- **UiDriver 在游戏窗口的点击验证配方**：普通 Button 命中区走
  `RegisterRect`（无语义标签），`clickid @按钮文字` 解析不到（@label 只认
  RegisterRectL 自报标签的控件，如 Wizard 行）；数字 id 跨构建漂移（同一
  源码重编后 1000018 → 1000004）。定式：先 `dump hitregions` 拿按钮几何
  中心，再用 `click x y` 几何点击；场景切换断言看 stdout 的 `SCENE ->` 行
  （SceneRouter）+ 各阶段 dump 的区域布局比对。
- **HTML HUD 的 flex row 别写 align-items:center**：body 满画布时它把按钮
  条垂直居中到整屏中央（工具栏跑到 y=382）；顶部工具栏用
  `align-items:flex-start`。
- **画布层点击 = dump 坐标 +48px Y**：UiDriver `click x y` 注入的是客户区
  像素事件。HTML 层命中区与点击坐标 1:1，直接用 `dump hitregions` 的几何
  中心；但 canvas 游戏收到的是舞台坐标 = 客户 Y − 48（标题栏），点舞台
  (sx,sy) 要发 `click sx (sy+48)`。实测：instrument OnDown 打印，设备
  (582,918) → 游戏 (582,870)（棋盘 74px 格阵差出一整格）。
- **dump 命令恰好两个参数**：`dump pixels a.png` / `dump hitregions a.json`，
  多塞 token（`dump hitregions 3 a.json`）整条静默不执行——后续断言全空
  时先查这个。
- **首击前等菜单装载完成**：启动后立刻 click 会跑在 HTML 命中区注册之前，
  这一击会整个丢失，下一击落在菜单上可能误触别的按钮（实测 1.2s 时首个
  按钮点击丢失、下一击误开了另一菜单项）。定式：launch 后 `wait 2500`，
  再 `dump hitregions` 门控（HTML 菜单 ≈15 个含 1000018+；纯 canvas 对局
  4 个仅 chrome 990001-990004），确认场景对了再点。
- **`SCENE ->` 行第一条是启动场景**：`Run("menu")` 也打一行，数"切了几次
  场景"要把它减掉，否则断言恒差一。
- **验证"Esc 退出进程"看 results.log 断尾**：驱动脚本活在游戏进程里，键
  真把进程退了，后续命令（含 quit）不再出现于 `ZAN_UI_OUT/results.log`
  且 exit 仍 0——"日志停在 keydown 那行"就是退出成功的证据。
- **随机种子的局，交互坐标不可跨运行复用**：发牌/布阵随 `Window.GetTickMs`
  种子变（斗地主每局牌序不同，固定下标点选两次拿到不同牌），点选目标要么
  当运行内 dump 后人工换算再发第二轮探针，要么选与布局无关的必然合法动作
  （如"单张领出"）；菜单/结算这类静态布局例外，坐标可跨运行复用。
- **在点击处理器里重建整棵控件树不会"点穿"到新控件**：框架按**上一帧**
  的命中区域解析点击目标，一次点击只投递给一个控件（`Ui.Clicked` 要求
  `app.ClickTarget() == id`），所以点完"收入牌组"重建出下一屏，下一屏的
  按钮不会收到同一次点击的尾巴，**不需要额外 `app.ClaimClick()`**。
  坑出处：肉鸽卡牌模板（战后三选一 → 下一层地图）首轮驱动时，现象是
  "选完牌直接进了战斗"，一度判成点击穿透；加埋点对比两次运行的 trace
  后发现 trace 完全一致，真因是两份驱动脚本差了一轮出牌——**现象先 diff
  脚本、再对比埋点 trace，最后才下"框架有 bug"的结论**。
- **种子固定的局，坐标可跨运行复用**：与"随机种子的局不可复用"相对——
  同种子必然同地图/同牌序/同奖励（如肉鸽卡牌：种子决定地图、敌人与
  奖励三选一），此时 dump 出来的几何跨运行稳定，可以固化成回归脚本。

## Zan 游戏模板迁 SceneRouter/HtmlScene 定式（menu/play/over 三场景）

批次实证（打砖块/贪吃蛇/五子棋/围棋/象棋/斗地主/黄金矿工七个游戏，2026-10；
这批当时是仓库内模板，现已搬出仓库作为独立项目）：

- **引擎类只留仿真**：删 `host`/`scene` 字段与 OnKey/Render 分发；**Create
  里 `g.c = h.App.canvas` 必须删**——Register 阶段 App 还是 null，留着
  即启动 NRE（画布每帧由场景 Render 对齐传入，Android 换面也顺带解决）。
- **OnDown 拆两半**：棋盘/牌桌/出牌等玩法点击留引擎（OnDownPlay），菜单/
  重玩/底排/Esc 升到场景层 `Event`（kind 2 = 按下，指针坐标
  `host.MouseX/Y`；kind 4 key 27 → menu 场景 `router.Exit`、其余场景
  `Go("menu")`）。
- **DrawMenu → DrawMenuArt**：只画背景+标题贴图，副标题/按钮/战绩行全部
  归 HTML（body 透明扁平列）；**DrawOver 删除**——OverScene.Render 继续
  画定格的对局画面，body `rgba` 半透明压暗，按钮文案走 HTML。
- **Main 四行**：`SceneRouter.CreateFixed(题名, w, h, 16)` → `Create(router.Host)`
  → `Register("menu"/"play"/"over", ...)` → `Run(g.demo ? "play" : "menu")`
  （demo 标记文件直接进对局自弈，截图/无头验证通道）。
- **结算场景有确认冷却就必须续推世界**：OverScene.FixedStep 继续调引擎
  Tick/Step（黄金矿工 ConfirmResult 的 600ms resultWait 只在 Step 里累计，
  停推则按钮永远无效）；纯棋类无此需求。
- **存档挂 OnExit**：切场景（ApplyPending）与退应用（SceneRouter.Stop）都
  会调当前场景 OnExit，旧主循环 Stop 里的落盘搬到这里。
- **`<button>` 生成 Gui Button 控件，不是文本 Element**：`root.Find(id)` 认
  **显式 `id=` 属性**（`data-on-click` 值不是 Find 键，漏 id 静默拿 null）；
  动态改按钮标签走 `Button.Text = "..."`（Binding 字段，Label 每帧重读，
  已实机验证）；<p>/<h1> 才是 `Element.SetText`。
- **handler 写 expression lambda**：`handlers.Add("x",  => this.Pick(0))`，
  语句块 lambda 不用（提炼成方法）；按钮多的菜单显式逐条注册，不靠循环变
  量闭包。

## GUI 登录门（登录成功才进主窗）定式

- **时序**：入口顺序是 注册→LoadHtml 建树→OnLoad→Run，在 OnLoad 里
  阻塞即"主窗 Run 之前"的门。子窗类（ChildWindow 子类，覆写 Title/Width/
  Height/ShowMaximize/IdBase）用 `OpenStandalone` + `PumpStandaloneUntil(
  stop)` 泵自己的事件循环；登录成功置位 stop 条件返回 true，主窗才继续。
  进程内没有干净的"放弃启动"出口：登录窗被直接关掉 = Pump 返回 false，
  宿主 `while (!WxLoginWindow.Run) { }` 重新拉起（门必须闩住）。
- **子页超高**：登录窗固定高里，注册/改密/找回 4 输入页比登录页（2 输入
  +链接行）高，大头像页头占 ~90px 会把页尾按钮挤出固定窗高被裁。翻页时
  收起大头像（原版微信子页也没有头像）即可，标题行保留。
- **窗口根用 dock 布局**（head/titlerow dock=1 吸顶，status/foot dock=2 
  落底，页区 dock=5 吃剩余）——纯流式排布在窗口根里会把底部行挤到页前面
  （WxChatWindow 同款成熟定式）。
- **UiDriver 限制**：`ZAN_UI_SCRIPT` 只绑第一个 App（`if (active) return;
  `），主 App 在 OnLoad 前构造 → 登录独立窗驱动不了。实机验证用
  SetCursorPos+mouse_event 真实点击（坐标 = WinRect + 自绘标题栏 50 物理px）；
  注意 Agent 会话注入的点击可能到不了用户交互桌面的窗口（WM_CLOSE 能到而
  鼠标不到=会话隔离），此时用"探针页码"（构造后直接 ShowPage(n) 的静态槽）
  做无头布局截图，交互行为留给用户实机点。

