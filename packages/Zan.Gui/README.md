# Zan.Gui

The Zan GUI toolkit, extracted from the standard library. HTML + CSS and
code-built retained controls share one Control tree, property and binding
protocol, layout, and routed input. ModelBinder consumes model-path metadata
from either authoring mode; code can also use typed `Binding<T>` fields.
The layering rules are enforced by `scripts/gui_layer_rules.py` (shrink-only
baseline in `scripts/gui_layers_baseline.json`); current migration gaps are
tracked as B-ID171..179 in `TASKS.md`.

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
- `Gui` in `src/Gui/Forms/` — HTML document assembly for `Form`.
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

`Form.Create` / `Run` provide the base window and system caption buttons.
Optional services are installed by the application before `Run` or `App.Show`:

```zan
App app = form.GetApp();
ThemeSettings.Enable(app);                // caption button and retained settings drawer
Gui.Backend.UiDriver.Enable(app);          // ZAN_UI_SCRIPT, started after Show settles client metrics
GuiFrameProfiler.EnableFromEnvironment(app); // ZAN_FRAME_PROF=1 frame log
```

Skin selection through `EnableSkins` / `ApplySkinName` works independently of
the settings drawer. `ThemeSettings.Show(app)` opens that drawer directly;
explicit legacy `ToggleThemeMenu` / `ShowThemeDrawer` calls remain available.
Setting `ZAN_UI_SCRIPT` or `ZAN_FRAME_PROF` alone no longer installs an optional
service into every window. `UiDriver.Begin(app)` remains the explicit start
entry for automation clients whose window is already shown. Enabling UiDriver
also opts into the frame profiler when `ZAN_FRAME_PROF=1` is set.

For remote images in an application that uses the scoped HTML or code-built
entry points, call `ImageHttp.Install()` before creating the view. Local and
embedded images work without this setup. The compatibility HTML entry points
install the remote image service automatically.

Use `Html.LoadWith` or `Html.ParseWith` when the application owns the set of
controls that a document can use. The factory receives the normalized kind
(`Button` for `<button>`, `InputNumber` for `<input type="number">`). It
returns a fresh control, or `null` to render the tag as an ordinary `Element`.
The document uses the same factory for includes and template row clones. For
custom components, register their definitions with
`UserComponentRegistry.RegisterDefinition(name, doc)` and have the factory call
`UserComponentRegistry.BuildWith(name, factory)` for the component kinds it supports.

```zan
static Control CreateControl(string kind) {
    if (kind == "Button") { return new Button(); }
    if (kind == "Label") { return new Label(); }
    return null;
}

// Inside the application entry point, after creating the host:
Control view = Html.LoadWith(app, "<label>Ready</label><button>Save</button>",
    handlers, "", CreateControl);
app.SetRootControl(view);
```

Named window documents accept the same factory through `Form.ShowWith`,
`ShowOffscreenWith`, `ShowDialogWith`, `ChildWindow.OpenHostWith`, and
`OpenStandaloneWith`. `Html.Load` and `Html.Parse` retain the standard HTML
vocabulary and resolve extensions through `ControlFactory`. `Form.Show` and
the original child window entry points install the standard design factory
when they find a matching document.

Published forms accept the `document`, `factory`, and `register` arguments in
`ShowWith`, `ShowOffscreenWith`, and `ShowDialogWith`. Child windows accept the
same arguments in `OpenStandaloneWith` and after the parent in `OpenHostWith`.
Pass the exact resource name from `--embed path=resource-name`. Project
publishers can forward each `gen.embed` entry in `zan.proj` as an `--embed`
argument. Applications that relied on automatic window resource discovery
must declare those documents and fragments when publishing. Each
`<include src="…">` also uses its full embedded resource name. Register document
actions with `ModelBinder.Handle` and `HandleArg` in the `register` callback.
For a button declared as `@click="save"`, register `binder.Handle("save", this.Save)`.
The window checks for missing resource names and unregistered actions before
opening. The explicit entry points support template and two-way model bindings
and run the existing window lifecycle hooks.

For documents with explicit events and no model directives, use
`Form.ShowStaticWith(document, factory, register, onLoad)` or
`ShowOffscreenStaticWith`. Register actions through `HandlerRegistry.Set` and
`SetArg`. The optional `onLoad` callback receives the assembled form after
event wiring. The development overload omits `document` and looks it up by
class name. The static entry points reject missing actions and model
directives before opening the window. Use `ShowWith` for `bind`, `:prop`,
and `data-for`. The `gui-empty` template uses the static entry point and
enables the settings drawer and UI automation only in development builds.

`Form.Show`, `ChildWindow.SetView` and `UiViewScope.Bind` assemble the real
`ModelBinder` for model paths, template rows and two-way binding. Hand-built
windows can register their binder through `App.RegisterModelHost`; layout and
writeback consume only the `AppModelHost` lifecycle contract and existing loop
registration. Plain typed `Binding<T>` controls do not install a model engine.

## Local Progress animation

Call `form.SetLocalAnimation(true)` before running a Form to enable local
updates for a single processing bar Progress on the Windows x64 CPU renderer.
The Form paints normally to establish the background. During a phase-only
update it restores that background, redraws the stripe, and submits the clipped
fill rectangle in client device pixels.

A processing stripe moves one device pixel every 33 ms. Use
`progress.Processing(true).DisplayCadence(true)` to sample intermediate
positions at the window's display cadence in both ordinary frames and local
updates. On Win32 the monitor rate sets the timer cadence. Use
`DisplayCadence(false)` to restore the default integer-pixel updates.
Reduced motion keeps the stripe stationary in either mode.

Input, scrolling, layout or DPI changes, model polling, and later paint that touches the
stripe use the ordinary Form frame. For custom paint, keep state changes outside
paint callbacks and request a redraw when state changes. The option defaults to
false and can be turned off with `form.SetLocalAnimation(false)`.

Enable ordinary partial frames with `form.GetApp().SetPartialFrames(true)` and
call `form.SetReusePaintLayout(true)` to reuse committed layout when only a
paint animation is due. A processing Progress with
a non-live value can use this path. The Form composites the damage rectangle in
paint order, including any overlapping translucent controls. The paint clip and
native dirty rectangle use the same client device pixels. Input, scroll and DPI
changes use normal layout and input preparation.

Request a redraw after changing layout or style through public fields or after
changing custom paint state. Keep paint callbacks free of layout mutations. A
custom leaf can opt in to culling through `PaintWithinBounds()` if its paint is
confined to its arranged box. Layout reuse defaults to off and can be disabled
with `form.SetReusePaintLayout(false)`.

## Native platform contracts

`IPlatformWindow` and `IPlatformShell` select Win32 on Windows and the
`zan_gui_*` C ABI on the other targets. Native sources are in
`src/drivers/gui/`; each target needs a driver built from the matching sources.
A source implementation or successful cross-compilation alone does not establish
native UI behavior on that platform.

- Desktop X11 and Cocoa accept client resize dimensions in **device pixels**.
  `ResizeClientExact` uses the same native operation; neither method promises a
  synchronous resize event. Cocoa converts through the target window's backing
  scale. Mobile surfaces follow their host layout, so client resize and desktop
  window positioning do not resize or move the mobile host.
- X11 Xdnd and Cocoa file-URL drops expose paths for the **current dequeued
  event and window**. Check `DropPending()` and consume `TakeDroppedFiles()`
  during that event; advancing to another event releases unconsumed paths.
  Remote file hosts and invalid UTF-8 paths are rejected. X11 cancels an
  unanswered selection request after one second and currently rejects
  incremental (`INCR`) selection transfers.
- Android, iOS and OHOS text-input sessions use `ImeSetOpen`, `ImeSetCaret` and
  `ImeComposing`. Text events carry Unicode scalar values; backspace uses text
  code 8. Desktop input methods own their composition UI and may return an
  empty composition string. iOS keeps marked text in a UIKit text-input proxy;
  this interface does not provide the application's full document context or
  promise grapheme-cluster deletion.
- OHOS clipboard reads and writes use the system Pasteboard/UDMF services.
  Missing services, denied access and other failures return a nonzero write
  status or an empty read result. The driver loads these services dynamically;
  it does not substitute a process-local clipboard.

An OHOS host must call this native bridge after every surface attach and when
its screen position, layout or transform changes:

```c
int32_t zan_gui_ohos_set_geometry(double surfaceScreenLeftPx,
                                 double surfaceScreenTopPx,
                                 double physicalScreenPxPerSurfacePx);
```

The origin is the surface's absolute position in physical screen pixels,
including window and content offsets. Convert ArkUI `vp` coordinates first;
parent-relative XComponent offsets alone are insufficient. An untransformed
surface uses scale 1. Scale 0 invalidates the geometry and returns -1; valid
geometry returns 0. The driver withholds cursor-position notifications until
valid geometry is supplied. This repository currently has no ETS/NAPI host
calling the bridge, so a host integration is still required for candidate
positioning.

The focused regressions are `runtime_gui_x11_platform`,
`runtime_gui_mac_window_drop`, `runtime_gui_ios_input`,
`runtime_gui_ohos_native` and `conformance_gui_capi_platform`. The mobile host
tests exercise production state machines with native-service boundaries
substituted; they do not replace system keyboard, permissions or cross-app
clipboard checks on devices. `scripts/build_gui_ohos.sh check-native all`
checks the official API-13 headers and links both OHOS architectures. Cocoa
window/drop behavior requires a macOS SDK and a graphical macOS session;
Retina, mixed-display scaling and real desktop drag sources need native
acceptance checks.
