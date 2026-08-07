# 代码地图：需求 → 该改哪儿

> 目的：拿到一句需求，30 秒内定位到"改哪几个文件"。行号会漂，所以每条都给
> **grep 关键字**，用它重新定位。

## 顶层目录

| 目录 | 语言 | 职责 |
| --- | --- | --- |
| `src/compiler/` | C | 编译器：词法 → 语法 → 绑定 → 检查 → IR/LLVM 生成 → 链接、打包、发布 |
| `src/runtime/` | C | 运行时：ARC、字符串、集合、异常、协程/async、GC 边界、平台调用 |
| `src/common/` | C | 编译器与运行时共用（诊断、字符串池、文件/路径） |
| `src/lsp/` | C | 语言服务：符号索引、补全、悬停、跳转、诊断推送 |
| `src/dap/` | C | 调试适配器（断点、变量、单步） |
| `src/fmt/`, `src/doc/` | C | `zanfmt` 格式化、`zandoc` 文档抽取 |
| `src/selfhost/` | Zan | 自举相关 |
| `src/ide_zan/` | **Zan** | 自举 IDE（编辑器、设计器、面板、AI 助手、市场客户端） |
| `stdlib/` | **Zan** | 标准库：`System`（核心/IO/网络/DB/文本）、`Gui`、`Game`、`Platform`、`Sdk`、`SDL3` |
| `templates/` | 混合 | IDE"新建项目"模板（`console/ gui/ library/ server/`） |
| `tests/` | 混合 | 测试用例（见 [testing.md](testing.md)） |
| `examples/` | Zan | 示例与真实项目（`examples/game/ra2` 等） |
| `scripts/` | PowerShell/Python | 构建、打包、测试、截图、驱动 UI 的脚本 |
| `docs/` | md | 规范与设计文档；本知识库在 `docs/agent-kb/` |
| `build/` | — | 唯一的构建输出目录（`zanc.exe`、`ZanIDE.exe`、测试 exe、驱动 DLL） |
| `_scratch/` | — | 一切临时探针、日志、试验项目 |

## 编译器：按"哪一层"找文件

编译顺序即定位顺序。报错先判断属于哪一层，再进对应文件：

| 层 | 文件 | 典型症状 |
| --- | --- | --- |
| 词法 | `lexer.c` | 非法字符、字符串/注释未闭合、数字字面量 |
| 语法 | `parser.c` | "expected X"、语句/表达式结构、新语法糖 |
| 命名空间/绑定 | `nsresolve.c`, `binder.c` | 找不到类型/命名空间、重名、`using` 解析 |
| 类型检查 | `checker.c` | 类型不匹配、枚举成员、隐式转换、可空性 |
| IR 生成（表达式） | `irgen_expr.c`（最大，312K）、`irgen_expr_core.c` | 成员访问、索引、运算符、临时值生命周期 |
| IR 生成（调用） | `irgen_call.c` | 方法解析、重载、内建函数、委托、链式调用 |
| IR 生成（语句/泛型/异步/ARC） | `irgen_stmt.c`, `irgen_generics.c`, `irgen_async.c`, `irgen_arc.c` | 控制流、泛型实例化、await 状态机、retain/release |
| 代码生成/发射 | `irgen_emit.c` | LLVM 发射、调试信息 |
| 领域代码生成 | `stdlib/System/Compiler/`（ZanGen.zan: `GenForm`/`GenScene`/`GenJson`/`GenRoute`/`GenDb`） | 设计器窗体、场景、JSON 绑定、路由表、ORM 绑定 |
| 驱动/CLI | `main.c` | 命令行开关、`--publish`、`--stdlib-path`、打包 |

**入口**：`src/compiler/main.c`。**诊断输出**：`zan_diag_emit(...)`，grep 消息文本即可
找到发出点——这是从错误信息反查代码最快的一招。

## IDE（Zan 自举）

`src/ide_zan/` 是一个 `partial class ZanIDE` 切成多个文件：

| 文件 | 职责 | grep 入口 |
| --- | --- | --- |
| `ZanIDE.zan` (310K) | `Main`、`Run` 主循环、初始化、Ribbon/菜单命令分发 | `static void Main`, `void Run()`, `Init...(` |
| `ZanIDE.State.zan` | 所有 UI 状态字段声明（信号、输入框、选择项） | 字段名 |
| `ZanIDE.Shell.zan` | 子窗口泵：向导/发布/文件选择器的每帧处理与结果落地 | `PumpWizardWindow` |
| `ZanIDE.Workspace.zan` | 项目/文件系统：模板目录扫描、脚手架、`zan.proj`、模板另存 | `BuildCatalog`, `CreateProjectT`, `CreateFile` |
| `ZanIDE.CodeNav.zan` | 代码导航、大纲、窗体文档模型（`FormDoc`/`FormFieldDoc`）与代码生成 | `class FormDoc`, `FreeFormBody` |
| `ZanIDE.Panels.zan` | 面板（输出、问题、终端、Git、资源…）渲染与命令 | 面板标题字符串 |
| `ZanIDE.Docs.zan`, `views/DocsData.zan`, `views/DocsPage.zan` | 内置文档/示例浏览器 | `DocsData` |
| `LspSession.zan`, `DebugSession.zan`, `ZanIDE.Dap.zan` | 连接 `src/lsp`、`src/dap` | `LspSession`, `DebugSession` |
| `AiAgent.zan`, `AiTeam.zan`, `AiNet.zan` | 内置 AI 助手（会话、工具调用、多角色） | `AiAgent` |
| `Marketplace*.zan` | 插件/组件市场客户端（已有骨架） | `Marketplace` |
| `AssetManager.zan` | 资源管理 | `AssetManager` |
| `IdeVersion.zan` | 版本与 commit（构建时生成/校验） | `IdeVersion` |

**IDE 是 Zan 写的**：改 IDE 就是写 Zan 代码，会同时受编译器能力约束。IDE 编不过时，
先分清是"IDE 代码写错"还是"编译器缺陷"（见 [debugging-playbook.md](debugging-playbook.md)）。

## 标准库

| 路径 | 内容 |
| --- | --- |
| `stdlib/System/` | 核心类型、集合、IO、Json、Text（含 Regex）、Net（Http/Ws/Mqtt/Tls/Rpc/Sse）、Data/DB、Web（Router/ApiDocs/…） |
| `stdlib/Gui/` | GUI 框架，见 [gui-development.md](gui-development.md) |
| `stdlib/Gui/Widget/` (58 个) | 基础控件：Button/Input/Table/Tabs/Ribbon/ToolStrip/StatusBar/SplitPanel/Wizard… |
| `stdlib/Gui/Component/` | 复合组件：Chart、DataTable、CodeEditor、WebView、Dock、FilePicker、PivotTable… |
| `stdlib/Gui/Hmi/` | 工控：`IoTag`、`Indicator`(LED/数显)、`Gauge`、`Trend`、`Alarm`、`NumPad`、`EquipPanel` |
| `stdlib/Gui/Designer/` | 表单设计器（`Designer.zan`、`Designer.Form.zan`、`Designer.Inspector.zan`） |
| `stdlib/Gui/skins/`, `drivers/`, `Backend/` | 皮肤资源、原生驱动绑定、后端 |
| `stdlib/Game/`, `stdlib/Platform/`, `stdlib/Sdk/` | 游戏引擎、平台 API（Windows 托盘/打印/服务/自动化…）、第三方 SDK |

## 常见需求 → 落点

| 需求 | 主要文件 |
| --- | --- |
| 新语法/语义 | `parser.c` → `binder.c`/`checker.c` → `irgen_*.c` + `tests/conformance` |
| 编译器误报/漏报 | 对应层 + `tests/diag`（编译期错误用 `run_compile_error.cmake`） |
| 新控件 | `stdlib/Gui/Widget/<Name>.zan` + `stdlib/Gui/Component/` 注册 + gallery 示例 |
| 新 HMI 元件 | `stdlib/Gui/Hmi/` + `templates/gui/gui-hmi` 演示 |
| 主题/皮肤 | `stdlib/Gui/Theme.zan`, `Skin.zan`, `Style*.zan`, `skins/` |
| 设计器行为 | `stdlib/Gui/Designer/*` + `stdlib/System/Compiler/GenForm.zan`（`.zform` → 代码） |
| 新建项目模板 | `templates/**/template.manifest` + `ZanIDE.Workspace.zan`（见 [templates-and-wizard.md](templates-and-wizard.md)） |
| IDE 面板/命令 | `ZanIDE.Panels.zan` + `ZanIDE.zan`（命令分发）+ `ZanIDE.State.zan`（状态） |
| 补全/跳转/悬停 | `src/lsp/` + `LspSession.zan` |
| 调试器 | `src/dap/` + `DebugSession.zan`, `ZanIDE.Dap.zan` |
| Web 路由/接口文档 | `stdlib/System/Web/Router.zan`, `ApiDocs.zan` + `stdlib/System/Compiler/GenRoute.zan` |

## 工具链命令（都在仓库根执行）

```powershell
scripts\build_ide.ps1                 # 编 zanc + IDE（含组件扫描、嵌入资源）
scripts\build_gallery.ps1             # 编控件画廊（GUI 回归的主力）
scripts\test.ps1 smoke|standard|full  # 分层测试
scripts\e2e_pipeline.ps1              # 遍历 templates\：建项目 → 编译 → 发布 → 冒烟
build\zanc.exe --stdlib-path stdlib <files> -o build\x.exe
build\zanc.exe --stdlib-path stdlib <files> --publish -o out\app.exe
```

注意：这些都要求**没有其他构建在跑**——它们共用 `build\zanc.exe` 和标准库时间戳。
