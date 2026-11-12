# AI 协作开发：让当前 SDK 提供事实

核实日期：2026-11-11。本文以当前网站入口、SDK 接入模板和 MCP 服务端源码为范围。不同发行版本、客户端权限和知识库安装状态会改变可用工具；每次接入都要重新确认本机环境。

AI 可以帮助理解需求、查 API、编辑和验证 Zan 项目。可靠的依据来自当前版本的语言文档、真实签名、源码与运行结果，而不只是模型记忆中的相似语言。

## 先分清三种 AI 能力

| 能力 | 提供什么 | 不能据此推断什么 |
| --- | --- | --- |
| 网站 Markdown 与 llms 文件 | 可阅读、可检索的语言和工具上下文 | 网站已连接本机，或模型已被训练成理解 Zan |
| 本地 zan-mcp 与编译器 | API 查询、文件操作、编译和可用的验证工具 | 每个工具都有依赖，或一次编译已覆盖全部项目行为 |
| 应用扩展包 Zan.Ai | LLM 调用、流式输出、检索和工具调用等应用能力 | 安装它就配置好了编程助手 |

[llms.txt](../llms.txt) 是内容导航，[llms-full.txt](../llms-full.txt) 提供集中全文。客户端可以把它们当上下文或检索材料；发布这些文件本身不会训练、微调模型，也不保证搜索引擎抓取、模型记住或自动使用最新内容。

## 推荐阅读顺序

1. 先读当前项目的 `AGENTS.md` 与任务相关规则，确认允许修改和验证的范围。
2. 阅读本站[总览](overview.md)、[快速开始](quickstart.md)，建立独立语言、SDK 和项目目录的概念。
3. 按需读取[语言基础](language.md)、[类型系统](types.md)、[内存与资源](memory.md)和[异步与并发](async.md)。
4. 用 [llms.txt](../llms.txt) 找到专题；需要离线检索或集中阅读时使用 [llms-full.txt](../llms-full.txt)，不要每次重复塞入全部全文。
5. 阅读本机 SDK 的 `AI_README.md`、`docs/AI_ONBOARDING.md` 与任务相关技能；这些内容解释实际安装和工具路径。
6. 查询当前签名和源码，用编译与运行结果确认实现；操作速查见[智能体入口](../agent-guide.md)。

文档有版本范围。历史设计、旧教程、技能中的经验条目可能早于当前实现；发生矛盾时记录版本差异，核对当前源码和可复现结果，不把旧缺陷继续写成语言限制。

## 安装与工作区初始化

从 [官方发行仓库](https://github.com/joyspinch/zanlang) 获取适合本机的完整 SDK，按其当前 README 与 [AI_README.md](https://github.com/joyspinch/zanlang/blob/main/AI_README.md) 操作。该仓库提供二进制发行；不要构造指向维护仓库内部源码的同名 GitHub 链接。

以下为 PowerShell 示例，假设 SDK 在 `D:\Zan`，项目在 `D:\work\hello-zan`：

```powershell
New-Item -ItemType Directory -Force "D:\work\hello-zan" | Out-Null
& "D:\Zan\tools\zan-mcp.exe" --init-agent "D:\work\hello-zan"
```

初始化要求目标目录存在，并把以下配置写入项目：

| 文件 | 用途 |
| --- | --- |
| `AGENTS.md` | 项目内的开发指导 |
| `.agents/skills/` | 按任务读取的技能 |
| `.mcp.json` | Claude Code 等客户端的 MCP 配置 |
| `.cursor/mcp.json` | Cursor 配置 |
| `.vscode/mcp.json` | VS Code / Copilot 配置 |

已有文件会跳过，不会自动合并 JSON。保留现有项目配置并检查需要合并的内容；`--force` 会覆盖初始化目标文件，不应当作常规重试选项。SDK 移动后也应先检查路径，再更新相应配置。

初始化不创建应用源码、不安装 AI 编辑器，也不编译程序。打开项目目录，并按客户端当前版本的操作启用本地 MCP；它通常会作为子进程启动：

```powershell
& "D:\Zan\tools\zan-mcp.exe" --stdio "D:\work\hello-zan"
```

正常使用时由客户端管理这个进程。SDK 中 `ai/` 的配置文件是含占位符的模板，不应未经展开直接复制。MCP 文件工具使用工作区相对路径；SDK 的绝对路径应配置在服务端启动信息中。

## 先确认环境，再选工具

连接后的第一步是 `zan_start_here`，必要时再用 `health_check`。确认工作区根、真实入口、编译器、索引状态和权限；不要假定例子里的路径在用户机器上存在。

以下工具名来自当前服务端实现。客户端可能附加命名空间前缀；应使用本次会话实际暴露的名称和参数。

| 任务 | 工具与关键参数 | 返回结果的用途 |
| --- | --- | --- |
| 初始定位 | `zan_start_here`、`health_check` | 工程与 SDK 状态 |
| 搜符号 | `zan_api_search(query)` | 名字、签名与位置 |
| 定位声明 | `zan_lookup_symbol(name)` | 精确定位类型或成员 |
| 查完整成员面 | `zan_list_members(type)` | 查看重载与可用成员 |
| 查官方用法 | `zan_example(topic)`，无 topic 时列目录 | 获取当前安装提供的示例 |
| 定位与读取源码 | `search_text(query)`、`find_files(glob)`、`read_file(path, from_line, max_lines)` | 按命中位置读必要窗口 |
| 按符号读源码 | `zan_read_context(symbol)`，若提供 | 用索引定位定义窗口 |
| 改前评估影响 | `zan_find_refs(name)`、`zan_change_impact(symbol)`，若提供 | 引用候选及相关路由、实体 |
| 发现扩展包 | `pkg_search(query)`、`pkg_info(name)`、`pkg_list_installed` | 分清目录、包详情和项目安装状态 |
| 安装扩展包 | `pkg_install(name, scope)` | 按项目需要安装，检查实际结果 |
| 编译探针 | `zan_compile(content, filename)` | 完整小程序的编译诊断 |
| 入口编译 | `zan_build_project(entry)` | 当前入口文件的编译结果 |
| 执行验证 | `run_command(command)`，若允许 | 运行真实构建命令和目标程序 |
| GUI 验证 | `zan_gui_snapshot(file, action_script)`，若提供 | 控件树、交互区域和布局诊断 |

工具目录、`available` 字段与实际调用结果需要一起看。部分工具按索引或编译器是否存在注册；部分工具即使已注册，仍可能返回依赖缺失。工具不存在不等于语言 API 不存在，空搜索结果也不能直接证明能力不存在。

当前 `zan_build_project` 默认尝试 `src/App.zan` 或 `src/main.zan`，但源码实现只传入所选入口，不能假定会枚举整个工程。入口可能被 IDE 重命名；多文件项目应使用项目构建脚本或显式列出全部源码，方法见[工具链](tooling.md)。

引用检索中的文本候选不等于重载解析结果。`zan_refresh_knowledge` 负责示例知识生成，不应把它描述成重建所有符号索引的万能修复；先核对具体缺少哪一份索引。

## 没有 MCP，也可以完成开发循环

本地编译器和文档仍然可以工作。先确认 SDK 与项目路径，再查符号：

```powershell
& "D:\Zan\toolchain\zanc.exe" --version
& "D:\Zan\toolchain\zanc.exe" --api WriteLine
& "D:\Zan\toolchain\zanc.exe" --api File.ReadAllText
```

`--api` 使用离线索引，不从网络下载接口。可以直接检索 `knowledge/symbols.json`，其中记录名字、类型、签名和位置；示例可从 `knowledge/gallery.json`、`templates/`、`examples/` 查找，实际目录以安装内容为准。

如果提示索引缺失，先说明缺失项并检查 SDK 是否完整。索引没有结果时，换精确类型名、查看相关包源码和已知示例；内建成员还要核对编译器 API 表。不要把搜索失败转成“请自己实现一个同名 API”。

多文件项目的编译示例：

```powershell
Set-Location "D:\work\hello-zan"
New-Item -ItemType Directory -Force .\build | Out-Null
$zanSources = @(Get-ChildItem .\src -Recurse -File -Filter *.zan |
    Sort-Object FullName | Select-Object -ExpandProperty FullName)
if ($zanSources.Count -eq 0) { throw "未找到项目源码" }
& "D:\Zan\toolchain\zanc.exe" @zanSources --auto-stdlib -o .\build\app.exe
if ($LASTEXITCODE -eq 0) { & .\build\app.exe }
```

这条命令验证的是实际列出的源码集合。当前编译器也接受源码目录参数；旧 SDK 是否支持应查其帮助。

## 不要把 C# 或 .NET 的惯例当作 Zan 事实

- Zan 有独立编译器与运行时，不能直接运行 NuGet 程序集或把 .NET 库名当作现成依赖。
- 相似类名不保证相同构造器、重载、属性、异常或返回类型；静态成员与实例成员也要查声明。
- `using` 负责命名空间导入，不负责包安装；包目录名、包名和源码 `namespace` 声明要分别核对。
- `Console`、字符串方法、`List<T>`、字典和 `StringBuilder` 包含编译器内建能力，搜不到对应标准库文件不能判定不存在。
- ARC 生命周期、引用环和外部资源清理需要明确设计，不能套用托管 GC 的回收直觉。
- 异步调用按 Zan 的返回类型与调度语义编写，不按 .NET 的 `Task<T>` 形状自动补全；方法名字带 Async 也不证明内部一定是内核非阻塞 I/O。
- GUI 的 HTML/CSS 支持范围、原生互操作 ABI 和目标平台依赖，应分别查[扩展库](libraries.md)、[互操作](interop.md)与[平台](platforms.md)。

## 从需求到证据的闭环

1. 把需求变成一个具体行为，例如“保存后重启仍可读到事项”，并确定输入、预期输出和验收环境。
2. 查已存在的包、真实方法签名与完整示例，读取有争议实现的源码窗口。
3. 实现一小段行为，保持数据、错误和资源生命周期清楚。
4. 编译真实项目输入集合；读取 `ok`、`exitCode`、`diagnostics` 和 `raw`，从首条错误开始处理。
5. 使用错误编号或完整消息查询本版本 `docs/DIAGNOSTIC_CODES.md` 与相关源码，再用最小探针缩小问题。
6. 实际运行并验证行为；GUI 要操作窗口，网络要检查请求与响应，持久化要验证重新打开后的数据。
7. 报告改动文件、实际命令、观察结果与未验证项；编译通过、运行通过、目标设备通过分别说明。

签名解决“怎样调用”，源码解释“当前怎样实现”，测试验证“这个场景实际怎样表现”。三者相互补充；只有截图、只有退出码或只有编译成功，都不足以支持超出覆盖范围的结论。

## 可复制给编程助手的任务说明

```text
请用 Zan 实现我接下来描述的功能。先确认 SDK、项目目录和版本，读项目规则及官方入门。
有 MCP 时先用 zan_start_here，查实际 API、示例和必要源码；没有 MCP 时使用本地文档与 zanc --api。
不要按 C#/.NET 记忆猜签名。多文件工程列齐源码，区分包安装、命名空间导入和原生依赖。
每次完成一个行为后编译并实际运行；报告真实命令、观察结果和未验证项。
需求与验收条件：在这里填写。
```

## 核实来源

以下为维护仓库本地路径；随包文档应按用户实际 SDK 版本读取。

- `zan-site/agent-guide.md`、`zan-site/src/onboarding.js`：公开发行入口与客户端接入说明。
- `tools/ai_pack/docs/AI_README.md`、`tools/ai_pack/docs/AI_ONBOARDING.md`：初始化、知识库与本地开发流程。
- `tools/mcp_server/McpServer.zan`：真实工具名、参数、可选注册、入口编译和诊断返回。
- `src/compiler/driver/main_driver.c`、`src/compiler/builtin_api.c`：API 查询与内建能力。
- `docs/ZANC_CLI.md`、`docs/DIAGNOSTIC_CODES.md`：源文件输入、CLI 与诊断查询。
