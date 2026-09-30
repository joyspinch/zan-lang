# Zan.Gui.CodeEditor - 代码编辑与控制台组件包

从 stdlib `Gui/Component` 拆出的编辑器组件族。命名空间保持不变
（`Gui.Component.CodeEditor` / `Gui.Component` / `Gui.Widget`），
`using` 语句无需改动；`--auto-stdlib` 会按需从本包拉入用到的命名空间。

## 组件

- **CodeEditor**（`Gui.Component.CodeEditor`）：语法高亮代码编辑器，
  自动补全 / IntelliSense / 调试联动（断言 gutter）/ 符号索引。
- **ChatView**（`Gui.Component`）：气泡式对话视图（AI 面板、IM 场景）。
- **ConsoleView**（`Gui.Component`）：尾部滚动、可选择的控制台视图。
- **CodeBlock**（`Gui.Widget`）：只读代码块（文档、聊天消息内嵌代码）。

## 留在 stdlib 的共享原语

- `EditorPalette`（`Gui.Component`，stdlib `Gui/Component/EditorPalette.zan`）：
  语法/选区语义调色板，LogView 与本包共用。
- `LogView` + `LogState`（`Gui.Component`）：日志滚动与文本选区几何原语，
  ChatView / ConsoleView / StyledText 共用。

## 依赖

仅依赖 stdlib（Gui 核心 / Widget / Styling / System）。无原生驱动、无皮肤基线
改动；`--publish` 语义与拆包前完全一致。
