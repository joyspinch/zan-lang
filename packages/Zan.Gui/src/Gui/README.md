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

数据绑定走两条正道：HTML 窗口用 `data-bind`（ModelBinder），代码侧用
`Binding<T> data` 字段（`stdlib/System/Binding.zan`）。`Signal*` 只是控件内部
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
