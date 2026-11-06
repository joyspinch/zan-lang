# Zan.Gui

The Zan GUI toolkit, extracted from the standard library. It does two jobs:
describe and draw the UI with HTML + CSS (tags grow into a retained control
tree), and bind Zan data to that UI both ways (`data-bind` in HTML windows,
`Binding<T> data` fields in code). The target layering, the current debt and
the migration route are in `docs/GUI_ARCHITECTURE.md` at the repository root.

Programs compile as-is once the package is visible to zanc (project
`packages/`, `.zan-packages/`, or a toolchain-relative `packages/` store).
Namespaces today (the per-layer split is phase P7 of the architecture plan):

- `Gui` — shared by `src/Gui/{Core,Layout,Styling,Rendering,Text,Media}`:
  `App`, window shell, frame pump, hit testing, overlays, `Control`/`Form`,
  the `ControlExtra` sparse property container, `VirtualScrollHost` viewport
  primitives, CSS styling/skins, layout, canvas, text shaping and media.
- `Gui.Widget`, `Gui.Component` — the control library (buttons, inputs, tabs,
  VirtualList, ChatView, ConsoleView, Ribbon, Wizard, DockHost, ...).
- `Gui.Markup` — HTML parsing and markup-to-control-tree building.
- `Gui.Animation` — tweens and transitions.
- `Gui.Backend` — the Zan-side half of the native window shells (Win32/X11/
  Cocoa) whose native halves live in the compiler's `zan_gui` runtime driver.
- `Gui.Diagnostics` — `UiWatchdog`, the "is it stuck, and where" tool for
  GUI programs: `Enter(name)` / `Exit()` bracket the code you suspect
  (it reports `UIWATCHDOG stall stage=<name> blocked=<ms>ms` only when the
  bracket exceeds the threshold, so idle time is never misreported), and
  `FrameBegin()` / `FrameEnd()` count slow frames (>16.7ms) and the worst
  frame. Opt-in: nothing happens until you call it.

Charts, DataTable, CodeEditor, Browser (WebView / CEF / Firefox) and the visual
Designer live in the sibling `Zan.Gui.*` packages; industrial HMI controls live
in `Zan.Industrial`.

## 架构要点

1. **保留式控件树与路由事件（Retained Control Tree & Routed Events）**
   - **适用场景**：常规业务界面（Form、Card、Tabs、Button、Input、VirtualList、ChatView 等），由 HTML 标签长成长生命周期的 `Control` 树。
   - **Visual Tree 路由分发（Tunneling & Bubbling）**：自 Root 向叶子的隧道阶段与自叶子向 Root 的冒泡阶段已经实现，`e.Handle()` 终止冒泡，`App.CapturePointer` 做指针捕获，`App.ProcessEvent` 会先走这条路径。存量控件大多仍在绘制期轮询输入，按 B-ID177 分批迁到路由事件。
   - **稀疏属性容器（`ControlExtra`）**：将数据绑定、浮动布局、动画过渡、事件槽位与无障碍语义（`a11yRole`, `a11yLabel`）等 38 个低频字段收拢于按需分配的 `ControlExtra` 容器，使基础 `Control` 节点体积缩减 62%（约 280 字节），支撑上万节点无GC停顿。
   - **无障碍与自动化测试（A11y Semantic Tagging）**：核心控件原生支持角色与标签标注，`UiDriver` 可直接通过 `@role:label` 进行语义化断言与点击，免受像素坐标漂移影响。

2. **即时模式 API 正在下线**
   - 应用代码每帧调用 `widget.Render(app, x, y, …)`、配合 `WidgetId.Push(name/index)` 作用域 Hash 栈派生控件 ID 的即时模式写法是历史包袱，按 B-ID177 逐步迁移后删除。
   - pre-commit 棘轮禁止新增即时模式入口与 `ClaimClick` 一类输入仲裁调用（`docs/WORKSPACE_CONVENTIONS.md` §17 第 5 条）。

3. **数据驱动虚拟化视口（Data-Driven Viewports）**
   - **适用场景**：高吞吐数据流、海量数据报表与动态图表（`DataTable`、`ChartView`）。
   - **虚拟化原语（`VirtualScrollHost`）**：行切片、视口裁剪、分页与点击判定抽离为纯计算基元，状态外置，供控件在自己的绘制里实现零堆分配的帧绘制。

Resources ride inside the package and zanc discovers them from package source
roots (stdlib copies win if present):

- `src/Gui/skins/`, `src/Gui/icons/` — embedded at compile time, resolved at
  run time.
- `src/Gui/drivers/<target>/` — per-target `zan_gui` driver import libraries
  and `driver.manifest`; zanc bundles the driver when the image emits
  `zan_gui_*` imports.

Browser components live in the separate `Zan.Gui.Browser` package (WebView /
CefBrowser); `App` has no build-time dependency on it.
