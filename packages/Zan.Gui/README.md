# Zan.Gui

The Zan GUI toolkit, extracted from the standard library. Namespaces are
unchanged (`Gui`, `Gui.Core`, `Gui.Widget`, `Gui.Component`, ...), so existing
programs compile as-is once the package is visible to zanc (project
`packages/`, `.zan-packages/`, or a toolchain-relative `packages/` store).

- `Gui` / `Gui.Core` — `App`, window shell, frame pump, hit testing, overlays,
  `ControlExtra` sparse property container, and `VirtualScrollHost` viewport primitives.
- `Gui.Widget`, `Gui.Component` — the control library (buttons, inputs, tabs,
  VirtualList, ChatView, ConsoleView, Ribbon, Wizard, ...).
- `Gui.Styling` / `Gui.Rendering` / `Gui.Layout` / `Gui.Text` / `Gui.Media` /
  `Gui.Markup` / `Gui.Animation` / `Gui.Reactive` / `Gui.Designer` — style,
  render, layout, text and tooling layers.
- `Gui.Backend` — the Zan-side half of the native window shells (Win32/X11/
  Cocoa) whose native halves live in the compiler's `zan_gui` runtime driver.
- `Gui.Diagnostics` — `UiWatchdog`, the "is it stuck, and where" tool for
  GUI programs: `Enter(name)` / `Exit()` bracket the code you suspect
  (it reports `UIWATCHDOG stall stage=<name> blocked=<ms>ms` only when the
  bracket exceeds the threshold, so idle time is never misreported), and
  `FrameBegin()` / `FrameEnd()` count slow frames (>16.7ms) and the worst
  frame. Opt-in: nothing happens until you call it.

24	## 核心架构范式（Dual-Paradigm Architecture）
25	
26	Zan GUI 采用双范式正交分层架构，彻底消除传统框架中“重度保留对象树”与“即时虚拟化视口”之间的认知割裂：
27	
28	1. **保留式面向对象树与标准路由总线（Retained Control Tree & Routed Events）**
29	   - **适用场景**：常规业务界面（Form、Card、Tabs、Button、Input、VirtualList、ChatView 等）。
30	   - **Visual Tree 路由分发（Tunneling & Bubbling）**：事件分发遵循现代 GUI 标准，分为自 Root 向叶子节点的下行隧道阶段（Preview/Tunneling）与自叶子向 Root 的上行冒泡阶段（Bubbling）；调用 `e.Handle()` 即可终止继续冒泡，配合 `App.CapturePointer` 全局指针捕获，彻底杜绝裸坐标判定与事件穿透。
31	   - **稀疏属性容器（`ControlExtra`）**：将数据绑定、浮动布局、动画过渡、事件槽位与无障碍语义（`a11yRole`, `a11yLabel`）等 38 个低频字段收拢于按需分配的 `ControlExtra` 容器，使基础 `Control` 节点体积缩减 62%（约 280 字节），支撑上万节点无GC停顿。
32	   - **无障碍与自动化测试（A11y Semantic Tagging）**：核心控件原生支持角色与标签标注，`UiDriver` 可直接通过 `@role:label` 进行语义化断言与点击，免受像素坐标漂移影响。
33	
34	2. **作用域哈希栈控件标识（Scoped ID Hash Stack）**
35	   - **杜绝硬编码号段**：彻底废除 `820000`/`900000` 等全局魔数号段，全库统一基于 `WidgetId.Push(name/index)` / `focus.PushId(key)` 的 FNV-1a 32 位作用域 Hash 栈派生局部稳定 ID。
36	   - **无限嵌套隔离**：复合控件、循环项、主从嵌套网格与全局浮层（Overlays）天然形成独立 ID 命名空间，从根本上解决多组件、多实例同屏时的 ID 踩踏冲突。
37	
38	3. **数据驱动虚拟化视口（Data-Driven Viewports）**
39	   - **适用场景**：高吞吐数据流、海量数据报表与动态图表（`DataTable`、`ChartView`）。
40	   - **虚拟化原语（`VirtualScrollHost`）**：行切片、视口裁剪、分页与点击判定抽离为纯计算基元，状态外置，实现零堆分配的帧绘制。

Resources ride inside the package and zanc discovers them from package source
roots (stdlib copies win if present):

- `src/Gui/skins/`, `src/Gui/icons/` — embedded at compile time, resolved at
  run time.
- `src/Gui/drivers/<target>/` — per-target `zan_gui` driver import libraries
  and `driver.manifest`; zanc bundles the driver when the image emits
  `zan_gui_*` imports.

Browser components live in the separate `Zan.Gui.Browser` package (WebView /
CefBrowser); `App` has no build-time dependency on it.
