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

## 核心架构范式（Dual-Paradigm Architecture）

Zan GUI 采用双范式正交分层架构，彻底消除传统框架中“重度保留对象树”与“即时虚拟化视口”之间的认知割裂：

1. **保留式面向对象树（Retained Control Tree）**
   - **适用场景**：常规业务界面（Form、Card、Tabs、Button、Input、VirtualList、ChatView 等）。
   - **稀疏属性容器（`ControlExtra`）**：将数据绑定、浮动布局、动画过渡、事件槽位与无障碍语义（`a11yRole`, `a11yLabel`）等 38 个低频字段收拢于按需分配的 `ControlExtra` 容器，使基础 `Control` 节点体积缩减 62%（约 280 字节），支撑上万节点无GC停顿。
   - **无障碍与自动化测试（A11y Semantic Tagging）**：核心控件原生支持角色与标签标注，`UiDriver` 可直接通过 `@role:label` 进行语义化断言与点击，免受像素坐标漂移影响。

2. **数据驱动虚拟化视口（Data-Driven Viewports）**
   - **适用场景**：高吞吐数据流、海量数据报表与动态图表（`DataTable`、`ChartView`）。
   - **虚拟化原语（`VirtualScrollHost`）**：行切片、视口裁剪、分页与点击判定抽离为纯计算基元，状态外置，实现零堆分配的帧绘制。

Resources ride inside the package and zanc discovers them from package source
roots (stdlib copies win if present):

- `src/Gui/skins/`, `src/Gui/icons/` — embedded at compile time, resolved at
  run time.
- `src/Gui/drivers/<target>/` — per-target `zan_gui` driver import libraries
  and `driver.manifest`; zanc bundles the driver when the image emits
  `zan_gui_*` imports.

Browser components live in the separate `Zan.Gui.Browser` package (WebView /
CefBrowser); `App` has no build-time dependency on it.
