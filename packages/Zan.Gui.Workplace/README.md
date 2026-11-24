# Zan.Gui.Workplace

IDE / 专业工作台类界面组件：停靠布局、AI 对话、控制台与日志、文件树、属性表、
会话列表。基于 Zan.Gui 标准控件，不自绘。`using Gui.Component;` 即可。

## 内容

| 类型 | 作用 |
|---|---|
| `DockHost` | IDE 风格停靠布局：面板停靠到左/右/下/中央的标签组，可拖动分隔条 |
| `ChatView` / `ChatBubbleAvatar` | 对话视图（AI 聊天），气泡头像为保留子控件 |
| `LogView` / `ConsoleView` | 尾部滚动、可选择的日志视图，以及包成控件的控制台 |
| `FileTree` | 磁盘目录到 TreeView 节点的薄封装（遍历由 `System.IO.DirectoryTree` 完成） |
| `PropertyGrid` | 把一组 `PropSpec` 画成可编辑属性行，直接读写绑定字段 / signal |
| `SessionList` | 会话导航列表（保留模式） |
| `Models/` | 上述组件的行、消息与布局模型 |

注意：本包与 Zan.Gui 及其他 Zan.Gui.* 包共用 `Gui.Component` 命名空间。
