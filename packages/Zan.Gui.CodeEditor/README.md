# Zan.Gui.CodeEditor - 代码编辑与语言服务组件包

从 stdlib `Gui/Component` 拆出的专业代码编辑器组件族。纯粹专注于代码编辑、语法解析与语言服务交互。命名空间保持不变（`Gui.Component.CodeEditor` / `Gui.Widget`），`using` 语句无需改动；`--auto-stdlib` 会按需从本包拉入用到的命名空间。

> **架构注记（职责正交解耦）**：通用 AI 气泡对话视图（`ChatView`）与控制台视图（`ConsoleView`）已下沉至基础包 `Zan.Gui`（`Gui.Component` 命名空间），普通 GUI 业务程序无需强依赖本包即可直接使用对话与控制台组件。

## 组件

- **CodeEditor**（`Gui.Component.CodeEditor`）：语法高亮代码编辑器，
  自动补全 / IntelliSense / 调试联动（断言 gutter）/ 符号索引。
- **CodeBlock**（`Gui.Widget`）：只读代码块（文档、富文本消息内嵌代码）。

## 留在 stdlib / Zan.Gui 的共享原语

- `EditorPalette`（`Gui.Component`）：语法/选区语义调色板，LogView 与本包共用。
- `LogView` + `LogState`（`Gui.Component`）：日志滚动与文本选区几何原语。
- `ChatView` + `ConsoleView`（`Gui.Component`）：通用 AI 交互与流式日志终端。

## 依赖

仅依赖 stdlib（Gui 核心 / Widget / Styling / System）。无原生驱动、无皮肤基线
改动；`--publish` 语义与拆包前完全一致。
