# GUI 开发指南（Zan）

> 本指南覆盖 Zan 桌面 GUI 框架 `packages/Zan.Gui/src/Gui`（纯 Zan 源码 + 原生驱动
> `zan_gui`）：架构模型、`.html` 可视化设计文档、代码式组件操作、布局、
> 样式/主题/皮肤、事件、控件大全、自定义绘制与高级组件。
> 所有 API 均可进一步查询逐命名空间参考页，如 [Gui](/ref/Gui)、
> [Gui.Widget](/ref/Gui.Widget)、[Gui.Component](/ref/Gui.Component)。

## 架构：App / Form / Control / Window

`packages/Zan.Gui/src/Gui` 顶层是 `namespace Gui`；控件在 `Gui.Widget`，高级组件在
`Gui.Component`（及其子命名空间 `Gui.Component.DataTable`、`Gui.Component.WebView`、
`Gui.Component.CefBrowser`、`Gui.Component.CodeEditor`、`Gui.Component.Chart`），
另有 `Gui.Designer`（可视化设计器）、`Gui.Hmi`（工控组件）、`Gui.Backend`
（原生层）、`Gui.Reactive`（响应式）。

四个关键概念（易混淆，务必分清）：

| 概念 | 是什么 | 位置 |
|---|---|---|
| `Window` | 纯操作系统窗口封装（Win32/macOS/linux 经 FFI），`Show/Present/SetTitle/SetShape/SetOpacity/SetResizable/Close/...` | `packages/Zan.Gui/src/Gui/Backend/Native.zan` |
| `App` | 应用宿主：持有窗口 + `canvas`（软件光栅器）+ 主题 + 焦点/命中测试 + 帧循环，构造 `App(title, w, h)`、`App.CreateDark(...)` | `packages/Zan.Gui/src/Gui/Core/App.zan` |
| `Form : Control` | **窗口本身就是控件**：`Form(string title, int w, int h)` 内部 `new App(...)`；`Form.Create/CreateDark/GetApp/SetTitle/Run/OnLoad/FrameHook/Every/OnExit` | `packages/Zan.Gui/src/Gui/Core/Control.zan` |
| `Control` | 组件树节点基类：字段属性 + 布局 + 事件 + 绘制虚方法 | `packages/Zan.Gui/src/Gui/Core/Control.zan` |

- `App` 与 `Window` 一对一；`Form` 是"窗口 + 根控件"的保留式封装。
- **没有 `App.OnStart/OnStop`**。`Form` 提供的生命周期钩子是：
  - `static void OnLoad(Form form)` —— 由生成代码调用一次（窗口构建后、
    事件循环开始前），代码后置文件在这里做初始化/绑定；
  - `form.FrameHook(() => ...)` —— 每帧（处理事件之后、渲染之前）回调；
  - `form.Every(ms, () => ...)` —— 周期任务（Timer 风格）；
  - `form.OnExit(() => ...)` —— 循环结束后一次。
- 子窗口/对话框：`ChildWindow` / `ChildWindows`（由 `Form.Run` 自动泵送）。

### 三种启动方式

**方式 A：`.html` 设计稿为入口（模板默认，最常用）**

`zan.proj`：`type = gui`、`entry = src/<Name>.html`。编译器把 HTML 设计文档（`<body data-zan-design>`）
投影成 `partial class`（字段 + `__CreateWindow()` + `Show()` + `Main()`），
与手写代码后置一起编译。见下文「.html 设计文档」一节与最小完整示例。

**方式 B：Form 保留式（纯代码）**

```zan
using System;
using Gui;
using Gui.Widget;

class Program {
    static void Main() {
        Form form = Form.Create("My App", 800, 600);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        Button ok = new Button("OK");
        ok.OnClick(() => { Console.WriteLine("clicked"); });
        root.Add(ok);
        form.Add(root);
        form.Run();
    }
}
```

**方式 C：App 即时模式（自绘/游戏式）**

```zan
App app = App.CreateDark("Demo", 1120, 720);
app.UseAppCss("button.pink { background:#e91e63; color:#fff; border-color:#e91e63 }");
app.Show();
app.SetFrameBody(() => { WidgetId.ResetFrame(); RenderAll(app); app.RenderChrome("Demo"); });
while (app.isRunning) {          // 真实项目用 Form.Run(); 这里手写事件泵
    app.ProcessEvent();
    app.BeginFrame();
    app.RequestRedraw();
    app.PresentFrame();          // 或 App.Run("title") 便捷循环
}
```

关键方法（App）：`Show()`、`Run(title)`、`RunLoop(body)`、`ProcessEvent()`、
`BeginFrame()`、`PresentFrame()`、`RequestRedraw()`、`Post(Action)`（后台线程
安全投递 UI 队列）、`Scale(int)`（DPI 逻辑→物理）、`SetChromeVisible/Status`
（标题栏）、`SetWindowShape/Shadow/Opacity/SetNativeGlass`（窗口外观）、
`UseSkin(name)`、`UseCss/UseAppCss`、`ApplyTheme`、`IsRunning`。

## Control 基类

### 字段即属性（直接赋值，没有 setter 方法）

```zan
btn.Text = "OK";            // Binding<string>，可绑定模型
btn.Class = "primary small";
btn.Name = "OkButton";
btn.Visible = true;
btn.Disabled = busy;        // Binding<bool>，沿树向下继承
```

公开字段（`packages/Zan.Gui/src/Gui/Core/Control.zan`）：`name`（稳定名，`Find` 用）、`dock`、
`layout`、`grow`、`prefW/prefH`（首选尺寸）、`mx/my`（手动偏移）、
`padL/T/R/B`（内边距）、`gap`（子间距）、`visible`、`bx/by/bw/bh`（布局结果）、
`parent`（weak）、`children`、`events`（设计器事件绑定）、`On`（`WidgetEvents`
通用事件包：`btn.On.Enter += ...`）、`Paint/Resize/Show/Hide`（生命周期
`UiEvent`）、`bindPath/bindProp`（数据绑定）、`Class`（CSS 类，空格分隔）、
`Disabled`（`Binding<bool>`）、`styleBg/styleBgTo/styleRadius/styleBorderColor/
styleBorderWidth/styleShadow/styleShadowDy/styleColor/styleFontPx/styleTransitionMs`
（内联样式字段，一般用链式方法设置）。

### 链式方法（都返回 this，可连续调用）

```zan
// CSS 风格链式设置
btn.Bg(0x5b6cff).Radius(12).Border(0x3a4570, 1).Shadow(0x000000, 6)
   .TextColor(0xffffff).FontPx(16).Gradient(top, bottom).Transition(150);

// 事件订阅（方法形式）
btn.OnClick(() => { ... }).OnChange(() => { ... });
```

事件方法：`OnClick OnDoubleClick OnRightClick OnChange OnEnter OnLeave
OnMouseDown OnMouseUp OnWheel OnFocus OnBlur OnKeyDown OnKeyUp OnLongPress
OnSwipe OnDrag OnDrop OnResize OnShow OnHide(Action)`。

### 树操作

```zan
Control c = btn.Add(child);        // 添加子节点，返回 child
Panel row = Panel.Row();
row.With(btn1).With(btn2);         // With 返回 this，可链式；自动沿 layout 轴停靠
row.Remove(btn1);                  // 移除
row.RemoveAll();                   // 清空
row.InsertAt(0, btn3);             // 插入
row.MoveChild(btn3, 2);
row.IndexOf(btn);
row.ChildCount();
root.Find("OkButton");             // DFS 查找（含自身）
form.Get<Button>("OkButton");      // Form 上的类型化查找
form.Ctl("OkButton");              // 任意控件
```

### 位置测量与渲染

`MeasureTree(app)` → `Arrange(px, py, pw, ph)` → `RenderTree(app)`；
宿主捷径 `ctrl.RenderInside(app, rect)` / `ctrl.RenderAt(app, x, y[, w])`
一次完成测量+布局+绘制（即时模式常用）。

## 代码式操作组件

```zan
using Gui;
using Gui.Widget;

// 1. 创建（对象初始化器风格）
Button save = new Button { Text = "Save", Class = "primary small" };
Label title = new Label { Text = "Welcome", Class = "h1" };
Input name = new Input { Placeholder = "Type here..." };

// 2. 属性赋值（字段）
save.Text = "OK";            // Binding<string>：绑定模型每帧重读
save.Checked = vm.bold;      // Binding<bool>：成为切换开关
save.Icon = "plus";
save.Tip = "保存当前文档";

// 3. 事件
save.Click += () => { TitleLabel.Text = "Hello!"; };   // UiEvent 字段 + (C# 风格)
save.OnClick(() => { ... });                           // 基类链式方法（返回 this）

// 4. 容器与布局
Panel root = Panel.Root("root");           // 窗口根容器
root.Dock(Dock.Fill());
Panel card = new Panel("My Card");          // 卡片容器
Panel col = Panel.Column().Gap(app.Scale(8));   // 纵向流式（子节点依次向下）
Panel row = Panel.Row();                    // 横向流式
col.With(new Button("A")).With(new Button("B"));
root.Add(col);

// 5. 动态增删
list.RemoveAll();
list.Add(page); page.Dock(Dock.Fill());

// 6. 运行时样式 / 主题
btn.Class = "pink teal";
btn.AddClass("big"); btn.RemoveClass("small");
app.UseAppCss("button.pink { background:#e91e63; color:#fff }");
app.UseSkin("light");
form.GetApp().SetChromeStatus("已保存");

// 7. 定时 / 每帧
form.Every(1000, () => RefreshStatus());        // 周期任务
form.FrameHook(() => SyncTitle());              // 每帧回调
app.Post(() => { ... });                        // 后台线程安全投递到 UI 线程

// 8. 双向绑定（System.Binding）
SignalString inputText = new SignalString();    // 响应式信号
Input box = new Input("type here");
box.data = model.inputText;                     // data 是 Binding<T>
```

💡 关键约定：**跨帧要读写同一份状态时，别指望闭包的捕获项**。闭包能捕获
局部变量，但只读捕获是创建时的快照；需要跨帧共享状态的处理器请：
- 存为类字段/静态字段，或
- 用设计稿的 `data-on-click="Save"` + `Form.On("Save", () => ...)`（HandlerRegistry）。

## 布局系统

### 停靠布局（默认，WinForms 风格）

控件 `dock` 字段：`0` 手动 / `1` 顶 / `2` 底 / `3` 左 / `4` 右 / `5` 填充。
停靠子节点按插入顺序贴边占用空间，Fill 占剩余。辅助：

```zan
c.Dock(Dock.Fill());         // 或 DockTop()/DockBottom()/DockLeft()/DockRight()/DockFill()
c.Prefer(200, 80);           // 首选尺寸（top/bottom 为高度，left/right 为宽度）
c.Place(40, 20);             // 手动偏移（dock=0）
c.Pad(8);                    // 内边距；Padding(top, right, bottom, left)
c.Gap(8);                    // 子间距
c.StylePad... c.SetShown(v).Grow();
```

`Dock` 常量类是静态方法：`Dock.Top()=1 ... Dock.Fill()=5`。

### 流式布局（layout=1 列 / 2 行）

`Panel.Column()` / `Panel.Row()` 设置 `layout`，`With()` 子节点自动沿轴停靠；
`grow=true` 的节点填满剩余主轴空间。配合 `Gap()` 使用。

### CSS flex 布局

给容器加 `display: flex`（样式表/`Style`），支持
`flex-direction`（row/column）、`flex-wrap`、`justify-content`
（start/center/end/between/around）、`align-items`（stretch/start/center/end）、
`align-content`（换行后各行在交叉轴的分布，含 stretch）、`gap`/`row-gap`/
`column-gap`、margin、min/max/百分比宽高。

子项上可以写 `flex`（`1 1 0` / `none` / `auto`）、`flex-grow`、`flex-shrink`
（`0` = 溢出时不让位）、`flex-basis`、`align-self`（覆盖容器的 `align-items`）、
`aspect-ratio`（只给一根轴，另一根按比例推出）、`order`。

`flex-basis: 0` 配 `flex-grow: 1` 是等宽列的标准写法——剩余空间按 grow 平分，
整除余数给最后一项，所以右边缘精确对齐容器。

不必手写这些的场合用现成容器：`Gui.Widget.Flex`（对标 n-flex/n-space）和
`Gui.Widget.Grid` + `GridItem`（对标 n-grid/n-gi，等宽列、span/offset、
按最小列宽自动降列）。两者只往 `Class` 写类名，几何全部由 `base.css` 给出。

### 特殊容器

- `Panel.Root(name)`：窗口根；`Layout()` 透明流式容器；`Titled(t)` 带标题分区。
- `FbFlow`（`Gui.Widget.FormBuilder`）：24 列流式网格，`AddCell(ctrl, span)`
  （1..24 行内贪心换行）——HTML 设计稿 `data-layout-mode="0"` 用它在页面里排字段。
- `ScrollColumn`：垂直滚动容器（子节点 dock=top 堆叠，超出滚动）。
- `SplitPanel`：可拖分割面板，子节点放 `parent.SlotHost(index)`。
- `Tabs`：`SlotHost(index)` 为页面槽。
- `Component/Dock`：`DockPanel/DockHost/DockGroup`（IDE 停靠系统）。

## 样式 / 主题 / 皮肤

### 解析链

主题默认 → 皮肤样式表（`skins/<name>/skin.css`）→ 应用 CSS（`UseCss`/
`UseAppCss`）→ 控件内联（`Bg()/Class/...`）。

### CSS 文本（`StyleSheet.Parse`/`guicss`）

`font-size`/`font`、`width`/`min-width`/`max-width`、`height`/`min/max`、
`aspect-ratio`、`gap`（`<both>` 或 `<row> <column>`）/`row-gap`/`column-gap`、
`radius`/`border-radius`（1~4 值）+ `border-<corner>-radius` 四个单角、
`padding`/`pad`（+每边）、`margin`（+每边）、`background`/`background-color`/`bg`、
`opacity`、`color`、`accent-color`、`font-weight`、`line-height`、
`letter-spacing`、`text-align`、`vertical-align`、`text-transform`、
`text-overflow`、`white-space`、`border`（+每边简写）、`border-width`/
`border-color`（+每边）、`border-style`、`box-shadow`/`shadow`、
`display`（flex/inline-flex/none）、`flex-direction`、`flex-wrap`、
`justify-content`、`align-items`、`align-content`、`place-content`、
`flex`/`flex-grow`/`flex-shrink`/`flex-basis`、`align-self`、`order`、
`position`、`top`/`right`/`bottom`/`left`/`inset`、`overflow`、`z-index`、
`visibility`、`cursor`、`box-sizing`（盒子一律 border-box，照收不改）、
`transition`（+`-duration`/`-timing-function`）、`transform`、
`rotate`/`scale`/`translate`（脱离 transform 的单独写法）、
`animation`（+`-name`/`-duration`）、`backdrop-filter`/`filter`（blur）。
颜色：命名色 + `#hex` + `0xAARRGGBB`。

长度按 100% 编写，DPI 缩放由引擎统一施加（含 `row-gap`/`column-gap`、
单角圆角与 `flex-basis`）。百分比不缩放。

支持选择器：类型 `button { }`、类 `.primary { }`、状态 `button:hover`/
`:active`/`:focus`/`:checked`、`a,b{}`、子件 `select::option`、
`:root { --name: value }` 自定义属性 `var(--name)`。

### 在代码里设置

```zan
app.UseAppCss("button.pink { background:#e91e63; color:#fff; border-color:#e91e63 }"
            + " button.teal { background:#13c2c2; color:#fff }");
btn.Class = "primary small";
btn.Bg(0x5b6cff).Radius(8);
btn.styleBg = 0x5b6cff;       // 内联字段直写
```

区别：`UseAppCss` 不随皮肤失效（切换皮肤时重新叠放，适合页面布局与应用专属
类）；`UseCss` 装进当前皮肤链。皮肤加载 `App.UseSkin("light")` /
`EnableSkins(0)`（标题栏选择器）/ `ReloadSkin()`。

### 主题与皮肤

`Theme.Light()` / `Theme.Dark()`：token `textPrimary/textSecondary/bgPrimary/
gradient/borderRadiusSmall/Medium/Large/heightMedium/gapMedium` 等，
`app.ApplyTheme(Theme.Dark(), true)`。

内建皮肤（`packages/Zan.Gui/src/Gui/skins/`）：`base dark light brutalism chinese darkgold
dreamy emerald fortune liquidglass matrix mono neon peachblossom retroamber
sunset`。

### Tailwind 风格工具类（`packages/Zan.Gui/src/Gui/Tailwind.zan`）

间距刻度 `n*4px`（`p-2`、`m-4`）；变体 `hover:/focus:/active:/disabled:/
checked:/selected:/focus-visible:`；任意值 `w-[240px]`、`bg-[#0f172a]`；
透明度 `bg-black/40`；工具类最后应用覆盖皮肤规则。
❌ 没有 `md:/sm:/dark:/group-hover:` 断点变体。

布局类：`flex flex-col flex-wrap items-center justify-between gap-4 gap-x-2`、
`grid grid-cols-3`；子项 `flex-1`（`1 1 0`，一排即等宽）/`flex-auto`/
`flex-initial`/`flex-none`、`grow grow-0 shrink shrink-0`、
`basis-0 basis-1/3 basis-full basis-24`、`self-center self-end self-stretch`、
`content-between content-stretch`、`aspect-video aspect-square`。

## 事件机制

### 三条路径（都可用）

```zan
// 1. 事件字段 +=（UiEvent 重载了 +=/-=）
btn.Click += () => { ... };                       // Button.zan 定义的 UiEvent Click
btn.On.MouseEnter += () => { ... };               // WidgetEvents.On 通用包

// 2. 基类链式方法（返回 this）
btn.OnClick(() => { ... }).OnChange(...);

// 3. HTML 设计稿处理器名 + Form.On（设计器绑定）
//    HTML: <button id="OkBtn" data-kind="Button" data-on-click="Save">确定</button>
//    App.zan: Form.On("Save", () => { CancelClicked; });
```

事件名（`Control.CommonEvents()`）：`Click DoubleClick RightClick MouseDown
MouseUp Enter Leave Move Wheel Focus Blur KeyDown KeyUp KeyPress Tap LongPress
Swipe DragStart Drag Drop Resize Show Hide`（+ 控件语义事件，如 `Tabs.TabChanged`）。

### 执行模型

事件处理器经 **UI 线程派发队列**（`App.DrainPosts`/`App.Post`）执行——处理器
可以安全地重建控件树、打开对话框、启动后台任务，不会碰到正在绘制的树。
`app.Post(action)` 可从任意后台线程投递到 UI 线程。

## .html 设计文档（重要）

Zan GUI 统一采用标准 **`.html` 设计文档**（由 `DesignerHtml` 与 `GenForm` 驱动），
通过 `<body data-zan-design>` 承载可视化窗口元数据。编译器在编译期自动将其投影成合成
`partial class`（不落盘），与手写代码后置（code-behind）一起编译。

### 根窗口定义（<body data-zan-design>）

```html
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>App</title>
</head>
<body data-zan-design id="App"
      data-win-title="App"
      data-win-w="480" data-win-h="520"
      data-win-center="true" data-win-pos-x="0" data-win-pos-y="0"
      data-win-resizable="true" data-win-chrome="true" data-win-opacity="0" data-win-glass="false"
      data-win-shape="0" data-win-shape-radius="0" data-win-round="false" data-win-shadow="0"
      data-layout-mode="0">
  <!-- 控件树：使用语义化 HTML 标签或 div/span，通过 data-kind 声明组件类 -->
</body>
</html>
```

常用窗口属性说明：
- `id`：投影类名（与代码后置类名一致，如 `App`）；
- `data-role`：`"window"`（默认） | `"control"`（可嵌入自定义控件，由 `.zcomp` 声明）；
- `data-win-title`：窗口初始标题；
- `data-win-w` / `data-win-h`：设计期窗口宽高像素；
- `data-win-center`：是否默认屏幕居中；
- `data-layout-mode`：`0` 为流式 24 列 FbFlow 布局；`1` 为自由坐标/停靠布局（绝对定位）；
- `data-win-shape` / `data-win-shape-radius` / `data-win-shadow`：异形窗口与阴影配置。

### 控件声明属性

控件使用对应 HTML 语义标签或容器标签，通过 `data-*` 属性声明组件元数据：

| 属性 | 类型 | 说明 |
|---|---|---|
| `data-kind` | string | **控件类名**：`"Button"`、`"Panel"`、`"SelectBox"`、`"WebViewBox"`、`"CefBrowserBox"`、`"DataGrid"` 等 |
| `id` | string | 控件字段名（生成强类型静态/实例字段，代码后置直接访问）；同时赋值给 `control.name` |
| `data-of` | string | 泛型实参：如 `data-kind="ListView" data-of="string"` → `ListView<string>` |
| `data-label` | string | 设计期标题/展示文本（Button/Label 等控件的显示文本） |
| `class` | string | CSS 样式类名 → `control.Class` |
| `data-props` | JSON 字符串 | 控件自有属性直通（`SetProp(key, value)`） |
| `data-x-options` | JSON 数组 | 选项控件选项列表，如 `["选项A","选项B"]` |
| `data-placeholder` / `data-required` | string/bool | 输入类控件占位文本与必填项标记 |
| `data-bind` / `data-bind-prop` | string | 双向数据绑定路径与绑定属性 |
| `data-child-tab` | int | 父容器槽位索引（Tabs 第几页/分栏第几格） |
| `data-span` | int | 流式布局 24 列网格占比（1..24，默认 24） |
| `data-fx` `data-fy` `data-fw` `data-fh` | int | 自由布局坐标与宽高（像素） |
| `data-dock` | int | 自由布局停靠：0 manual、1 顶、2 底、3 左、4 右、5 fill |
| `data-pad` / `data-gap` | int | 容器内边距与子控件间距 |
| `data-on-<event>` | string | **事件绑定**：如 `data-on-click="Save"` → 自动绑定至代码后置方法或 `Form.On` |

### 生成产物（GenForm）

- **窗口角色（window）**：生成 `partial class <Name> { static Panel __BuildForm(); static Form __CreateWindow(); static void Show(); static void Main(); ... }`。具名控件（带 `id`）自动生成为 **static 字段**；
- **初始化生命周期**：`__CreateWindow()` 负责构建窗体并装配控件树，随后回调用户的 `OnLoad(form)` 钩子，最后绑定事件处理器；
- **控件角色（control）**：生成 `partial class X : Control`，具名控件为实例字段。

### 最小完整示例（gui-empty 模板）

`src/App.html`：

```html
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>App</title>
</head>
<body data-zan-design id="App" data-win-w="480" data-win-h="520">
  <label id="TitleLabel" data-kind="Label" data-label="欢迎"></label>
  <input id="NameInput" data-kind="Input" data-label="姓名" />
  <button id="OkButton" data-kind="Button" data-label="确定"></button>
</body>
</html>
```

`src/App.zan`（手写代码后置，生成代码与其无缝拼合）：

```zan
using System;
using Gui;
using Gui.Widget;

partial class App {
    static void OnLoad(Form form) {
        OkButton.OnClick(() => { TitleLabel.Text = "Hello, " + NameInput.Text + "!"; });
    }
}
```

### 自由布局 + Tabs 样例（gui-tabform 模板节选）

`src/App.html`：

```html
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>TabForm</title>
</head>
<body data-zan-design id="TabForm" data-win-w="1000" data-win-h="680" data-win-title="TabForm" data-win-center="true" data-layout-mode="1">
  <div id="MainToolStrip" data-kind="ToolStrip" data-label="工具条" data-dock="1" data-fh="34"
       data-x-options='["新建:plus","打开:folder","保存:save","-",">帮助:info"]'></div>
  <div id="MainStatusBar" data-kind="StatusBar" data-label="就绪" data-dock="2" data-fh="24"
       data-x-options='["就绪",">UTF-8"]'></div>
  <div id="DocumentTabs" data-kind="Tabs" data-label="文档" data-dock="5" data-x-options='["文档 1","文档 2"]'>
    <textarea id="Doc1Text" data-kind="TextArea" data-dock="5" data-child-tab="0" data-placeholder="第一个文档..."></textarea>
    <textarea id="Doc2Text" data-kind="TextArea" data-dock="5" data-child-tab="1" data-placeholder="第二个文档..."></textarea>
  </div>
</body>
</html>
```

### 多窗口工程与编译管线

1. 多个 `.html` 设计稿一起编译（主入口窗口由 `zan.proj` 的 `entry` 指定，如 `entry = src/App.html`）；
2. 窗口间解耦：代码后置可直接调用另一窗口的静态启动方法，如 `OtherForm.Show()`；
3. 子窗口可通过 `ChildWindow` / `ChildWindows` 管理。

编译管线：
`.html (HTML 设计稿) --zanc formgen--> partial class 投影（内存合成，磁盘无冗余文件）→ 与手写代码后置（src/App.zan）合并编译`。
**永远在 IDE 或文本编辑器中编辑设计稿 `.html` 与代码后置 `.zan`，不要手写生成代码。**

## 容器与常用控件代码示例

### 面板与卡片

```zan
Panel root = Panel.Root("root");
root.Dock(Dock.Fill());

Panel card = new Panel("设置");            // 卡片（title + padding + 主题表面）
card.Dock(Dock.Top()).Prefer(0, 96);
Panel col = Panel.Column().Gap(8);        // 纵向流式
col.With(title).With(new Input { Class = "full" });
Panel row = Panel.Row();                  // 横向流式
row.With(new Button("取消", "ghost")).With(new Button("保存", "primary small"));
```

### 输入与表单

```zan
Input name = new Input("输入姓名");
name.Placeholder = "..."; name.Required = true;
name.OnChange(() => { Validate(); });

TextArea body = new TextArea { Height = 120, Class = "full monospace" };
Checkbox code = new Checkbox("内联样式");
RadioGroup rg = new RadioGroup("radio", ["A", "B", "C"]);
Slider slider = new Slider(0, 100, 40);
Rate rate = new Rate { Value = 3.5 };
SelectBox sel = SelectBox.FromOptions(["一", "二", "三"]).Bind("model.key");
FormField ff = new FormField("标签", name);   // label + 控件 + 校验
```

### 表格与列表

```zan
Table tbl = new Table(new string[] { "列1", "列2" });
tbl.AddRow(new string[] { "a", "1" });

// 泛型 ListView<T>：绑定实体列表
ListView<string> lv1 = new ListView<string>();
ListView<Row> lv2 = new ListView<Row>();
lv2.SetItems(rows, (row, item) => { ... });   // 详见参考页

// DataGrid<T>（绑定实体、带类型化列，设计稿 data-kind="DataGrid"）
DataGrid<Row> grid = new DataGrid<Row>();
grid.Col("Name", row => row.Name).Title("名字").Width(120);
grid.NumCol("Price", row => row.Price).Money("¥");
grid.Bind(rows);
```

### Tabs / 向导 / 折叠 / 弹出

```zan
Tabs tabs = new Tabs(Tabs.Card(8, 0));       // 或 Tabs.Line(...)/CreateVertical()
tabs.Add("概览", false).Add("详情", true);
tabs.TabChanged += (int i) => { ShowPage(i); };
page = tabs.SlotHost(0);                      // 第 0 页容器

Collapse col = new Collapse("折叠面板");     // items.Add(...)
Wizard wizard = new Wizard(); wizard.AddStep("第一步", panel1).AddStep("第二步", panel2);
Layer.LayerAlert(app, "标题", "内容");        // Toast/对话框快捷
DialogResult r = Layer.Confirm(app, "确定删除？");
Popover pop = new Popover();                  // 锚定气泡
Tooltip tip = new Tooltip("悬停提示"); Tip 字段设置
```

## 自定义控件与 Canvas 绘制

### 自定义控件（保留式）

```zan
// 1. 继承 Control，重写 OnPaint / OnMeasure / Kind()
class MyWidget : Control {
    int myValue;
    public void InitMyWidget(string n) { InitControl(n, 1); myValue = 0; }
    public override void OnPaint(App app) {
        Canvas c = app.canvas;
        c.FillRoundRect(bx, by, bw, bh, 8, 0x223355);
        c.DrawText(bx + 4, by + 4, "custom", Theme.Rgb(255, 255, 255), app.Scale(12));
    }
    public override void OnMeasure(App app) {
        prefW = app.Scale(120); prefH = app.Scale(48);
    }
    public override string Kind() => "MyWidget";
}
```

`Canvas`（`Gui.Render`，`App.canvas`）绘制 API：`Clear`、`FillRect`、
`DrawRect`、`FillRoundRect([In])`、`FillCircle`、`DrawCircle`、`FillRadial`、
`DrawLine`、`DrawPolyline`、`FillSector`、`DrawArc`、`FillVGrad`、
`BlurRect(Cached)`（磨砂）、`DrawText/Centered`、`MeasureText`、`FontHeight`、
`DrawIcon`、`DrawGlyph`、`BlitImage`、`PushClip/PopClip/ResetClip`、
`SnapshotRect/RestoreRect`（缓存加速）。颜色 = `0xAARRGGBB` 整数；
`Theme.Rgb(r,g,b)`。

**交互**：控件要接收输入需 `wiresOwnEvents = true`，然后
`Ui.Activate/RegisterRect` + 每帧 `Ui.Clicked(app, id) / Ui.Hovered(...) /
Ui.KeyDown(...)` 轮询（约 50 个查询，见 `Ui.zan`）；或宿主直接用
`ctrl.Render(app, x, y, w)` 的即时模式（返回 `Ui.Clicked` 可用的 id）。

### 即时模式（免布局器）

```zan
Button addBtn = new Button { Text = "+ Add", Class = "primary outline small" };
int id = addBtn.Render(app, app.Scale(24), app.Scale(64), app.Scale(64));
if (Ui.Clicked(app, id)) { ...; app.RequestRedraw(); }
```

## 高级组件

### WebView（Edge WebView2 / WKWebView）

- `WebViewBox`（保留式控件，设计稿 `data-kind="WebViewBox"`）：`SetStartUrl/Navigate/
  Back/Forward/Reload/Stop/CanGoBack/CanGoForward/View()`；
  `View().NavComplete += ...`、`View().Eval(js)`、`GetCookies/SetCookie/ClearCookies`。
- **平台**：Windows = Edge WebView2（COM 直驱）；macOS = WKWebView；其它
  平台 `IsSupported() == false`（画提示占位）。
- 隐藏页由 `NativeLayer` 自动裁空；多标签浏览器 = 多个 `WebViewBox`。

### CEF（Chromium Embedded Framework）

- `CefBrowserBox`（保留式控件）、`CefBrowser`、`CefHost`、`CefBootstrap`、
  `CefPage`、`CefCdp`。驱动分发在 `drivers/win-x64|macos-x64|macos-arm64`。
- 启动流程（oneplus 模式）：

```zan
static async int Main() {
    CefBootstrap.RunHelper();
    bool ready = await CefBootstrap.StartAsync();
    if (ready) { MainForm.SetCefState(...); }
    Form form = MainForm.__CreateWindow();
    form.FrameHook(MainForm.FrameTick);
    form.Every(250, MainForm.SyncAddress);
    form.Run();
    CefHost.Shutdown();
    return 0;
}
```

### CodeEditor（代码编辑器）

即时模式、纯软件绘制、无原生依赖（IDE 本身就用它）：

```zan
CodeEditor ed = new CodeEditor();
ed.LoadText("class Foo { }");
ed.Render(app, rect);
ed.GetText(); ed.LineCount(); ed.CurLine(); ed.CurCol();
ed.GoToLine(12); ed.SetScrollLine(n);
ed.SetSyntaxLang("zan"); ed.LangForName("cpp");
ed.SetCompilerDiagnostics(...); ed.CompletionActive(); ed.WordAtCaret();
ed.ApplyTheme(...);
```

### DataGrid / Chart / GraphView / PropertyGrid / FileTree 等

- `DataGrid<T>`（`Gui.Component.DataTable`）：`grid.Bind(rows)`、
  `grid.Col/NumCol/RealCol/BoolCol/DateCol(...)` 列 API（`.Money().Percent()
  .Affix().Sum().Editable(...).Pin()`）、`grid.Layout/State` 持久化
  （`DataTable.SaveLayout/LoadLayout`）。
- `Chart`（`Gui.Component.Chart`）：`Chart` + `ChartView.Render(app, rect)`，
  见 [Gui.Component.Chart](/ref/Gui.Component.Chart)。
- `GraphView`：节点图；`PropertyGrid`：反射属性网格；`FilePicker`：原生
  文件选择；`FileTree`：文件树；`LogView`/`ConsoleView`：日志/终端；
  `PivotTable`：透视表；`SessionList`、`Downloader`、`ChatView`。
- `CodeBlock`（`Gui.Widget`）代码展示块。

## DPI 与窗口

`app.Scale(n)`（逻辑→物理）；`App` 构造尺寸是逻辑尺寸，DPI 初始化在 `Show()`
里；`Canvas.GetDpiScale()` 可查当前缩放。`Form` 尺寸也可以用逻辑值。
异形窗口：`SetWindowShape` + `winShadow`；透明：`SetWindowOpacity`；
玻璃/亚克力：`SetNativeGlass` + `winGlass`；无标题栏：`SetChromeVisible(false)`
（配套 `SetChromeStatus`）。

## 可视化设计器（Gui.Designer）

IDE 内置的窗口设计器（`Gui.Designer`）：左侧分类控件面板（拖拽/点击
加行）、中间画布预览（点击选中）、右侧属性检查器（Field/Form 两个页签）、
底部状态栏。

- `layoutMode: 0` 流式：24 列网格，编辑有序字段列表（VB/WinForms 表单式），
  `span` 控制宽度，`FbFlow` 承载。
- `layoutMode: 1` 自由画布：绝对定位 + `winZoom` 缩放 + 栅格吸附 +
  8 个拖拽手柄。
- 检查器保存的就是 HTML 设计文档（`<body data-zan-design>`）；`Designer.Inspector`
  的 `SaveJson()` / `FieldFromJson()` 实现读写。设计器另写 form-builder 专属键
  （`labelPos/formSize/hideStar/showSubmit/...`），GenForm 忽略这些。
- IDE 宿主可把左右栏挂到自己的 dock（`hostPanels`）。

手动写 `.html` 设计稿也完全可行（IDE 的源码视图可直接编辑），规范见上面
「.html 设计文档」。

## 常见问题 / 备忘

- 事件委托跨帧共享状态用类字段/`Signal`——闭包的只读捕获是创建时快照，改不出跨帧效果。
- 设计稿里 `data-kind` 用类名（`SelectBox` 不是 `Select`、`WebViewBox` 不是
  `WebView`、`CefBrowserBox` 不是 `CefBrowser`、`DataGrid` 不是 `DataTable`）。
- 泛型控件设计稿用 `data-of` 给类型实参；缺省 `string`。
- `Panel.Root` 是窗口根容器；普通 `Panel` 是卡片。窗口本身是 `Form`。
- 控件文本/选中/禁用等字段是 `Binding<T>`：直接赋值每帧读一次模型字段，
  适合做双向绑定。
- `Form.OnLoad` 在 `__WireForm` **之前**调用（设计里 `on<Event>` 绑定在
  Wire 阶段）——OnLoad 里可以注册 `Form.On(name, Action)`。
- WebView/CEF 的 `kind` 有平台要求，其它平台画占位提示。
- 一切样式都可在代码里覆盖：`Class`/内联链式/`UseAppCss`/`UseSkin`。
- 逐命名空间 API 参考：[Gui](/ref/Gui)、[Gui.Widget](/ref/Gui.Widget)、
  [Gui.Component](/ref/Gui.Component)、[Gui.Hmi](/ref/Gui.Hmi)、
  [Gui.Designer](/ref/Gui.Designer)（或同名 `.md` 供 AI 抓取）。
