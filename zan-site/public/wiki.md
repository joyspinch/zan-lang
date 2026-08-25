# IDE 指南（Zan IDE）

> Zan IDE 是纯 Zan 自研的免费 IDE（源码在 `src/ide_zan/`，分析/调试后端为
> `zan-lsp` / `zan-dap`）：编辑、可视化设计、运行、调试、发布、AI 助手全部
> 内建，无外部 SDK 依赖。下载解压即可用（蓝奏云，提取码 `zanlang`）。

## 下载与启动

1. 点击官网「下载」，复制提取码 `zanlang`；
2. 解压到任意目录（**路径不要含空格**更稳妥）；
3. 双击 `ZanIDE.exe` 启动。

首次运行会在用户目录生成配置与项目缓存；项目默认存放位置可改（设置里）。

## 界面概览

- **左栏**：项目树（`.zan` 源码 / `.zform` 设计 / `zan.proj`）；
- **主区**：标签页式编辑器（代码 / 设计器 / json 视图 / 手动文档）；
- **底部**：运行输出 / 错误列表 / 终端；
- **右栏（可选）**：AI 助手、组件面板、属性检查器（设计器时出现）。
- 状态栏显示编译器版本、当前平台（win-x64 等）、构建状态、行/列。

## 项目管理：zan.proj

Zan 没有 `project.zan` 项目文件；IDE 用一个小的 `zan.proj` 描述工程：

```ini
type = gui            # console | gui | server | library
target = exe          # exe | dll
entry = src/App.zform # 入口（.zform 或 .zan）
platforms = win-x64   # 可发布目标（逗号分隔）
```

### 新建项目（模板向导）

菜单 文件 → 新建项目 打开向导：左侧**分类**、右侧**模板列表 + 预览
（真实截图或描线图）。分类：

- **Console / 控制台**：`console`（Hello World）、`console-args`（命令行参数）、
  `console-multi`（多文件项目）；
- **Window App / 窗口应用**：`gui-empty`（最小 `App.zform` 窗口）、`gui-free`
  （自由画布）、`gui-tabform`（标签页 + 工具条 + 状态栏）、`gui-sidebar`、
  `gui-ribbon`、`gui-toolbar`、`gui-components`（组件展示）、`gui-webview`
  （内置网页视图）、`gui-watch`（圆形表盘）；
- **HMI / SCADA / 工控 HMI**：`gui-dashboard`（仪表板卡片网格）、`gui-hmi`
  （工控仪表）、`gui-watch`；
- **Server / 服务端**：`server-mvc`（注解式 Web）、`server-http`（轻量
  HTTP）、`server-iot`（MQTT 物联网）、`server-ws`（WebSocket 网关）、
  `server-tcp`（TCP 服务）；
- **Library / 类库**：`dll-export`（导出函数）、`dll-class`（导出类）。

创建向导支持选择**设备尺寸**（`sizeable` 模板）：生成时会按尺寸改写 `.zform`
的 `winW/winH` 并把控件夹回画布内。默认设计尺寸如 `1000x680`。

## 编辑器

- 语法高亮、自动补全、跳转定义、悬停文档、实时错误（`zan-lsp`）；
- `Ctrl+S` 保存即编译检查（错误显示在错误列表与行内波浪线）；
- 多文档编辑、文件树操作（新建文件/文件夹、重命名、删除）；
- 代码格式化（`zanfmt`）。

## 可视化设计器（.zform）

`.zform` 是 JSON 设计文档（语法见「GUI 指南 → .zform」）。IDE 里打开 `.zform`
可直接在**设计器视图**与**代码视图**之间切换：

- **设计器**：左侧分类控件面板（拖拽或点击加行）→ 中间画布预览
  （点选控件）→ 右侧属性检查器（样式/布局/事件/绑定页签）→ 底部状态栏；
- **流式模式**（`layoutMode: 0`）：24 列网格，像表单生成器一样排字段；
- **自由模式**（`layoutMode: 1`）：绝对定位画布 + 缩放手柄 + 栅格吸附 + 8 拖拽柄；
- 属性检查器保存的就是 `.zform` JSON；设计器的「保存」会**重新生成**代码
  （`partial class`），你手写的代码后置（`src/App.zan` 的 `OnLoad`）不会被覆盖。

> 铁律：**不要在 `__BuildForm/__CreateWindow` 等生成代码里手写内容**
> （每次都重新生成）；初始化与事件逻辑写在 `src/App.zan`。

## 运行与调试

- 工具栏 ▶（或 `F5`）编译并运行；输出显示在底部面板；
- 断点/单步/变量/调用栈由 `zan-dap` 驱动（调试会话中可用）；
- 热重载：修改后重新编译运行（工程有热重载支持，若未支持会完整重启）；
- 运行控制台应用时可在 IDE 内置终端里输入。

## 发布

「发布」按 `--publish` 语义构建：`-Os` + 剥离调试信息 + 按
`--link-mode shared|static` 链接原生驱动到 exe 旁或并入 exe。产物是**单文件
可执行**（加驱动目录）：拷贝到目标平台即可运行，无需装运行时。

平台：`win-x64`、`win-arm64`、`linux-x64`、`linux-arm64`、`linux-musl`、
`macos-x64`、`macos-arm64`、`wasm32`（WASI）、`linux-riscv64`。交叉编译
需要相应工具链（IDE 发布窗口会检查并提示）。

## 内置 AI 助手

- 配置：设置 → AI（接口地址 / 模型 / API Key，兼容常见 OpenAI 风格端点）；
- **RepoMap 索引**：IDE 生成离线知识库 `knowledge/`（`symbols.json` API 索引 +
  `gallery.json` 示例目录 + `zform.json` 设计 schema），AI 通过
  `api_search` / `example` 工具先检索再作答，不靠全量 grep；
- **开放 MCP**：外部 AI（如 ZCode/其他 agent）可连接 IDE 的 MCP 服务，对
  项目增删改查、检索、打补丁（服务端工具见 `tools/mcp_server/`，文档见
  `docs/MCP_HOSTING.md` 与 `docs/ai-assist.md`）；
- 网站文档（本站）即是 AI 与人工的共同知识源：让 AI 抓 `/lang.md`、
  `/gui.md`、`/ref/<ns>.md` 即可获得准确上下文。
- 想把外部 AI 编码工具（Claude Code / Cursor / Copilot / Windsurf 等）接到
  本机 SDK？见 **[/ai-connect](/ai-connect)**——`tools\ai_pack\`（AGENTS.md +
  skills + MCP 配置）的网站镜像，一条命令装进项目。

## 快捷键速查

| 操作 | 键 |
|---|---|
| 运行 | `F5` |
| 停止 | `Shift+F5` / 控制台停止按钮 |
| 保存并编译 | `Ctrl+S` |
| 格式化 | 编辑器菜单（zanfmt） |
| 新建项目 | 文件 → 新建项目 |
| 搜索文件 | 文件树上方过滤框 |

> 详见 IDE 内「帮助」与 `docs/agent-kb/templates-and-wizard.md`
> （模板清单与向导数据流）。
