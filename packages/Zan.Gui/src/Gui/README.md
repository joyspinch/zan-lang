# Zan GUI 架构规范与层次设计

Zan GUI 是基于统一渲染管线与保留模式（Retained Mode）构建的现代化图形界面框架，在概念模型与架构上对齐现代 .NET / C#（WPF、Avalonia、WinUI）的设计原则，同时保持无运行时 GC 暂停与跨平台轻量高效。

---

## 1. 层次架构设计（Layered Architecture）

Zan GUI 划分为清晰的四层体系：

```
+-------------------------------------------------------------------------+
|  领域扩展包 (Packages / 独立源码包)                                       |
|  - Zan.Gui.Charts (高维数据可视化/图表引擎)                                |
|  - Zan.Industrial (工业 SCADA 组态/仪表/仪器)                             |
+-------------------------------------------------------------------------+
|  复合组件层 (Gui.Component / 复杂交互控件)                                 |
|  - CodeEditor, DataTable, TreeView, MarkdownViewer, FilePicker, Dock... |
+-------------------------------------------------------------------------+
|  原子控件层 (Gui.Widget / 基础 UI 元素)                                   |
|  - Button, TextBox, Label, CheckBox, Slider, ProgressBar, QrCode...     |
+-------------------------------------------------------------------------+
|  核心引擎层 (Gui 根命名空间 / 基础内核)                                    |
|  - App & Window: App, ChildWindow, Nav, Focus                           |
|  - Object Model: Element, Control, Event, Types                         |
|  - Layout Engine: Layout (Flex), LineBox, Scroll                        |
|  - Styling & Theme: Style, StyleSheet, Skin, Theme, Css, Effects, Fx   |
|  - Rendering Pipeline: Render (Canvas2D), RenderAA, Device, NativeLayer |
|  - Declarative Runtime: Html, ControlFactory, ModelBinder (data-bind)    |
+-------------------------------------------------------------------------+
```

---

## 2. 命名空间与目录组织契约

Zan 编译器采用**按路径零配置自动按需引入**（Demand-driven stdlib pull-in）机制：
GUI 的"标准库"就是本包 `packages/Zan.Gui`——`using X.Y;` 按路径按需拉取源码，
目录结构直接反映命名空间层级（不存在独立的 `stdlib/Gui/` 目录）：

| 命名空间 / 路径 | 核心定位与职责 | 代表类型与文件 |
| :--- | :--- | :--- |
| **`Gui`** (`src/Gui/` 根) | **GUI 核心内核**。提供应用程序生命周期、事件总线、排版引擎、样式系统、渲染画布与声明式工厂。 | `App`, `Control`, `Element`, `Event`, `Layout`, `Style`, `Html` |
| **`Gui.Widget`** (`src/Gui/Widget/`) | **原子与复合控件集**。轻量、结构明确的基础 UI 输入/展示控件与工具条。 | `Button`, `TextBox`, `Label`, `CheckBox`, `ComboBox`, `Slider`, `QrCode`, `Ribbon` |
| **`Gui.Component`** (`src/Gui/Component/`) | **高级业务组件**。具备独立状态机、复杂视口与重型交互的高级控件与视图。 | `TreeView`, `BandGrid`, `FilePicker`, `GraphView` |
| **`Gui.Backend`** (`src/Gui/Backend/`) | **底层图形后端适配**。对接 Windows GDI/DirectWrite、Software Rasterizer 等本地驱动。 | `GdiBackend`, `SoftwareBackend` |

命名空间 `Gui` 内部按层摆目录（目录只表层级、不改命名空间；依赖只许向下，
机检见 `scripts/gui_layer_rules.py`）：`src/Gui/` 根放几何与窗口形状基元
（`Point`/`Rect`/`Size`/`Insets`/`Cursor`/`WindowShape*`）→ `Backend` → `Rendering`/`Text`/`Media`
→ `Styling`/`Layout` → `Core`（控件树、App 主循环、`WindowRoot`/`ChildWindowBase` 契约）
→ `Binding`（`ModelBinder`、`DocWire`、`UiViewScope`）→ `Widget` → `Component` → `Markup`
（含 `HtmlDocs` 文档查找）→ `Forms`（`Form`、`ChildWindow`、`Nav`/`NavWindow`）。
`Testing`（`UiDriver`、`UiProbe`）是旁路层：可依赖任何层，任何层不得依赖它。

数据绑定走两条正道：HTML 窗口用 `data-bind`（ModelBinder），代码侧用
`Binding<T> data` 字段（`stdlib/System/Binding.zan`）。HTML 绑定对模型的读写只经
`Binding/ModelAccessor.zan`（JsonValue/JsonDoc/字典/POCO 各一个无状态实现），新增模型形态在那里加一个实现，
不要在 ModelBinder 里写 `is X` 分支。`Signal*` 只是控件内部
状态缓冲，不是对外绑定协议；本包不存在 `Gui.Reactive`/`Gui.Designer` 命名空间。

---

## 3. C# / .NET 开发者心智映射表

习惯 C# WPF / Avalonia / WinUI 的开发者可以按以下心智模型快速对应：

| .NET / WPF / Avalonia 概念 | Zan GUI 对应体系 | 说明 |
| :--- | :--- | :--- |
| `System.Windows.Application` | `Gui.App` | 进程级 GUI 入口，负责消息泵驱动、DPI 缩放与主循环 |
| `System.Windows.Window` | `Gui.ChildWindow` / `Gui.App` | 顶层/子视窗宿主与系统窗口句柄承载 |
| `System.Windows.UIElement` | `Gui.Element` | 具备尺寸、位置度量、命中测试与事件路由的基础视元 |
| `System.Windows.Controls.Control` | `Gui.Control` | 具备样式、生命周期（Mount/Unmount）、焦点与模板的控件基类 |
| `System.Windows.Controls.*` | `Gui.Widget.*` | 原子输入控件库 |
| `DrawingContext` / `SKCanvas` | `Gui.Render` | 高性能保留/即时混合 2D 绘图上下文（Canvas） |
| `Panel` / `StackPanel` / `Grid` | `Gui.Layout` / `Gui.CssGrid` | 采用 Flex 与 CSS 栅格化标准排版引擎 |
| `Style` / `ResourceDictionary` | `Gui.Style` / `Gui.StyleSheet` / `Gui.Skin` | 基于属性继承与 CSS 级联特性的主题/皮肤模型 |
| `DependencyProperty` / `INotifyPropertyChanged` | `data-bind`（ModelBinder）/ `Binding<T>` | 双向绑定两条正道；`Signal*` 仅作控件内部状态缓冲 |

---

## 4. 垂直领域解耦原则（Packages 分包规范）

为了保证标准库纯粹、坚固与精简，遵循以下准则：
1. **凡垂直业务或专有领域套件，绝不侵入标准库**：
   - 工业组态/仪表控件请引用 `packages/Zan.Industrial`；
   - 复杂统计图表/高维可视化请引用 `packages/Zan.Gui.Charts`；
   - 游戏引擎/实时动画请引用 `packages/Zan.Game`（引擎核心），具体游戏类目
     引用对应的 `Zan.Game.<类目>` 独立包（Arpg/Idle/Board/Match3 等）。
2. **纯算法与专用工具就近放置**：
   - 二维码矩阵编码等仅为特定控件服务的算法，统一收敛在 `Gui.Widget`（如 `QrEncoder.zan`），避免在根目录平铺杂质。

## 脱窗 UI 测试（`Gui.Testing.UiTest`，ZAN_STANDARD S23/S24）

窗体（ViewModel）不起窗口、不建 App 就能测：`UiTest.Open(vm)` 按类型名取 `--embed`
进来的同名标记（与 `Form.Show` 同一查找）建树、绑定到这个实例、按名接上处理器。

```csharp
LoginForm vm = new LoginForm();          // 声明式窗体 new 出来不建 App
UiTest t = UiTest.Open(vm);              // 或 UiTest.OpenMarkup(vm, html)
t.Type("UserName", "admin");             // 写入视图 → 回写模型
t.Click("Login");                        // 触发 @click 处理器，再刷回视图
t.Expect("Error").Hidden();              // Text/Value/Visible/Hidden/Enabled/Disabled/Checked
t.MatchSnapshot("tests/gui/snapshots/login_ok.json");   // 语义树快照，首次录制
```

- 名字在用到时对照真实控件树校验：写错立刻抛 `UiTestException`，并给出最接近的名字
  （"did you mean 'Login'?"）；禁用控件拒绝输入与点击。
- 每次操作后做一次完整双向同步（一帧绑定泵做的事），断言看到的就是用户会看到的。
- 快照与 `ZAN_UI_DUMP`（S21）同一格式；脱窗时没有布局，区间为 0，快照因此稳定。
- 依赖 App 的服务（`GetApp()` 返回 null）在脱窗时不可用：处理器里重绘用
  `RequestRedraw()`（无宿主时安全），不要直接 `GetApp().X`。

## 布局不变量检查（`Gui.Testing.UiCheck`，ZAN_STANDARD S22）

窗体离屏装配（不建系统窗口），按设计尺寸在给定 DPI 下排一帧后查：

```csharp
List<UiIssue> issues = UiCheck.Run(new LoginForm(), 150, true);   // 150% DPI + 伪本地化
string json = UiCheck.Json("LoginForm", issues);
```

| 种类 | 含义 |
|---|---|
| `truncated` | 带文本的叶子控件比首选尺寸窄/矮——文字被截 |
| `small_target` | 可交互控件小于 24 逻辑像素（按 DPI 折算） |
| `overlap` | 两个可交互控件相交且互不包含——点击会落错 |
| `offscreen` | 可见控件超出窗口（滚动容器内的内容不算） |
| `collapsed` | 可交互控件被挤成 0 宽或 0 高 |

伪本地化把静态文本拉长 40%，提前暴露译文变长后的截断（绑定文本同步时会被模型
覆盖，不受影响）。`zan-check` 的 ui 步自动对输入里的全部窗体跑三档 DPI 与伪本地化。

## 标记的编译期检查（ZAN_STANDARD S20）

派生自 `Form` / `ChildWindow` 的类 `X` 与同目录（或 `--embed` 进来的）`X.html` 配对，
zanc 在类型检查后对照 `X` 的成员查标记——与运行期开窗前的 DocWire 对账同一口径，
只是提前到编译期、报在 `.html` 的属性值上并给拼写补丁：

| 诊断 | 标记写法 | 要求 |
|---|---|---|
| E2112 | `@click` / `v-on:` / `z-on:` / `data-on-` / `on*` 的处理器名 | `X`（含基类）的方法，或在 `X` 的源码里以字符串登记过（`binder.Handle("名字", …)`） |
| E2113 | `bind` / `bind:属性` / `:属性` 的纯路径 | 逐段是字段或属性；只下钻用户代码里声明的类（集合、字典、`JsonValue` 按运行期协议取值，不查） |
| E2114 | `z-mount` / `data-mount` | `X` 的字段 |

不查：`<template>` 子树里的绑定（以行项为作用域）、非标识符的处理器值（路由动作）、
带运算符的表达式、`X` 在用户代码里重写了 `ViewModel()` 时的绑定路径。

