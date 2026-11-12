# Zan.Gui

The Zan GUI toolkit, extracted from the standard library. HTML + CSS and
code-built retained controls share one Control tree, property and binding
protocol, layout, and routed input. ModelBinder consumes model-path metadata
from either authoring mode; code can also use typed `Binding<T>` fields.
The target layering, current migration gaps, and verified results are in
`docs/GUI_ARCHITECTURE.md` at the repository root.

Programs compile as-is once the package is visible to zanc (project
`packages/`, `.zan-packages/`, or a toolchain-relative `packages/` store).
Namespaces today (the per-layer split is phase P7 of the architecture plan):

- `Gui` — shared by `src/Gui/{Core,Layout,Styling,Rendering,Text,Media}`:
  `App`, window shell, frame pump, hit testing, overlays, `Control`/`Form`,
  the `ControlExtra` sparse property container, `VirtualScrollHost` viewport
  primitives, CSS styling/skins, layout, canvas, text shaping and media.
- `Gui.Widget`, `Gui.Component` — the control library (buttons, inputs, tabs,
  VirtualList, ChatView, ConsoleView, Ribbon, Wizard, ...).
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
   - **适用场景**：所有业务界面与扩展组件（Form、Card、Tabs、Button、Input、VirtualList、ChatView 等），由 HTML 标签或声明式代码长成长生命周期的 `Control` 树。
   - **输入根与绘制**：`SetRootControl` 选择输入树；`RenderTree` 绘制已有树，不自动登记输入根，也不代替 Measure/Arrange。嵌入树通过真实父树或现有输入根适配器接入。
   - **Visual Tree 路由分发（Tunneling & Bubbling）**：自 Root 向叶子的隧道阶段与自叶子向 Root 的冒泡阶段，`e.Handle()` 终止冒泡。`CapturePointer(control)` 用于普通按压，拖动使用二参 `true`，触摸越过阈值后仍继续；主键 Up 提交最终坐标后释放，Cancel 清理状态。
   - **长按**：`OnLongPress` 订阅接入共同手势状态机，保留原始 Down 的叶节点来源，静止达到500ms触发一次；越过12px阈值或退出输入树时取消。组件固有长按可覆写 `AcceptsRetainedLongPress()` 明确认领，普通命中或泛路由订阅不会自动认领。绘制不驱动长按计时。
   - **锚定弹层生命周期**：换根与摘除输入子树会关闭失去输入归属的旧锚定弹层，即使锚点没有焦点或捕获。聚合输入根仍包含锚点时保留；点坐标菜单仍由调用方显式关闭。取消或关闭回调可以创建新输入与弹层，共享宿主按各自身份和登记代次保护新状态。
   - **稀疏属性容器（`ControlExtra`）**：将数据绑定、浮动布局、动画过渡、事件槽位与无障碍语义（`a11yRole`, `a11yLabel`）等 38 个低频字段收拢于按需分配的 `ControlExtra` 容器，使基础 `Control` 节点体积缩减 62%（约 280 字节），支撑上万节点无GC停顿。
   - **无障碍与自动化测试（A11y Semantic Tagging）**：核心控件原生支持角色与标签标注，`UiDriver` 可直接通过 `@role:label` 进行语义化断言与点击，免受像素坐标漂移影响。

2. **两大创作范式（Two-Paradigm Standard）**
   - **范式一：HTML + CSS**：Markup 标签由底座解析为现代盒模型，外观完全由 CSS 级联与 Theme token 决定，排版纯走 CSS（Flex/Grid/Flow）。
   - **范式二：保留模式声明式对象树**：代码侧使用 `Children = { ... }`、`ClassName = "..."` 或 Add/With 建树，排版纯走 CSS（Flex/Grid/Flow）；集合入口必须收养子节点并继承宿主。两种模式共用 PropSpec、ModelBinder 与 GetProp/SetProp，代码也可配置 `Binding<T>` 字段。
   - **组件开发约定**：图表、HMI、代码高亮等 UI 的像素绘制封装在标准控件内部 OnPaint，对外暴露属性与数据绑定。此约定不代表全部遗留组件与调用方已迁移，当前缺口和验收结果见架构文档。

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
