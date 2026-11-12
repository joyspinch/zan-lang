# 工具链：编译、查询、调试与发布

核实日期：2026-11-11。本文描述当前维护仓库的工具行为，并使用 Windows SDK 路径举例；独立发行包可能有版本差异。先用 `zanc --version` 和 `zanc --help` 核对本机，不要把旧教程的默认值直接套到新工具上。

第一次运行程序请从[快速开始](quickstart.md)进入。本页解释如何给编译器正确输入、获取诊断，以及把编辑器工具接入实际开发循环。

## 工具各自负责什么

| 工具 | 职责 | SDK 常见位置 |
| --- | --- | --- |
| ZanIDE | 项目编辑、构建、运行与调试界面 | `ZanIDE.exe` |
| zanc | 解析、检查、原生代码生成与链接，也能查询 API 索引 | `toolchain/zanc.exe` |
| zan-lsp | 向编辑器提供补全、定义、诊断等 LSP 能力 | `toolchain/zan-lsp.exe` |
| zan-dap | 向编辑器提供断点、步进、变量等 DAP 能力 | `toolchain/zan-dap.exe` |
| zan-mcp | 向 AI 客户端提供项目、API、文件和编译工具 | `tools/zan-mcp.exe` |

SDK 工具位置与维护仓库的 `build/` 输出位置不同。不要把教程中的 `build/zanc` 原样当作安装后的路径。普通应用开发直接使用完整 SDK；编译语言工具本身是另一套维护工作。

## 编译输入是文件集合

`zanc` 一次完成源码编译和所需链接。位置参数可以是 `.zan` 文件，也可以是当前版本支持的源码目录。

单文件命令，假设当前目录是应用项目、`build/` 已存在：

```powershell
& "D:\Zan\toolchain\zanc.exe" .\src\main.zan --auto-stdlib -o .\build\app.exe
if ($LASTEXITCODE -eq 0) { & .\build\app.exe }
```

多文件应用显式枚举全部需要参与构建的源文件：

```powershell
$zanSources = @(Get-ChildItem .\src -Recurse -File -Filter *.zan |
    Sort-Object FullName | Select-Object -ExpandProperty FullName)
if ($zanSources.Count -eq 0) { throw "未找到项目源码" }
& "D:\Zan\toolchain\zanc.exe" @zanSources --auto-stdlib -o .\build\app.exe
```

当前目录输入的等价简写为：

```powershell
& "D:\Zan\toolchain\zanc.exe" .\src --auto-stdlib -o .\build\app.exe
```

目录输入会递归展开 `.zan`，按路径排序，并跳过隐藏目录以及 `build`、`publish`、`bin`、`obj`、`node_modules`、`_scratch`、`dist`、`vendor`、`.zan-packages` 等目录。精确输入集合应由项目构建脚本明确表达。

单独传入口文件不会自动加入其旁边的应用源码。`--auto-stdlib` 当前默认开启，负责发现标准库和包命名空间；它不承担项目兄弟文件收集。

源文件很多时可以使用 `@响应文件`。响应文件支持一行一个或空白分隔的参数，带空格路径加引号，`#` 开头表示注释；不要把 shell 的转义规则直接当成响应文件规则。

```powershell
& "D:\Zan\toolchain\zanc.exe" '@build/sources.rsp' --auto-stdlib -o .\build\app.exe
```

这里的 `sources.rsp` 应由项目构建流程生成，内容是实际源文件路径。不要把别的项目入口一起收进去。

## 经常需要查的 CLI 选项

| 选项 | 作用与边界 |
| --- | --- |
| `--api <symbol>` | 查询离线符号索引，无需编译 |
| `--stdlib-path <dir>` | 指定标准库根目录；常规 SDK 会自动定位 |
| `--no-stdlib` | 关闭自动标准库和包发现 |
| `--no-packages` | 仅关闭包发现 |
| `-o <file>` | 指定输出；库后缀可决定共享库或静态库类型 |
| `--emit-lib` | 选择库输出，具体链接要求见互操作文档 |
| `--target <name>` | 指定目标；有效名字以 `--list-targets` 为准 |
| `--embed <path[=alias]>` | 将文件或目录嵌入产物供运行时读取 |
| `-L<dir>` / `--libpath <dir>` | 追加原生库搜索目录 |
| `--link-input <file>` | 追加对象、资源或库文件 |
| `--time` | 输出各编译阶段耗时 |

包的安装、可发现位置与命名空间导入见[标准库与扩展库](libraries.md)。内部包市场 CLI 选项仍标为未稳定，不应把它们当成所有发行版本通用的安装协议。

## 普通构建、诊断构建和发布构建

| 模式 | 优化与符号 | 诊断行为 |
| --- | --- | --- |
| 普通构建 | 默认 `-O2` | 默认运行时守卫开启 |
| `-g`，未加 `--publish` | 强制 `-O0`，生成调试信息 | 默认开 `--check-leaks` 与 `--arc-guard` |
| `--publish` | 默认 `-Os`，链接时剥离符号 | 泄漏报告与 ARC 检疫默认关；运行时守卫仍默认开 |

显式 `-O` 可以覆盖发布默认优化档，但 `-g` 仍强制 `-O0`。不建议组合 `-g --publish`，发布链路仍会剥离符号。

`--strict-runtime` 是独立开关：守卫失败直接退出，退出码为 70。`-g` 与 `--publish` 均不会自动开启它。默认软守卫可能报告错误后继续执行，所以退出码为零不能替代对运行时错误输出的检查。

`--check-leaks` 在程序退出时报告尚未释放的 ARC 对象。`--arc-guard` 会隔离已释放的块，内存占用可能随分配增长，适合诊断运行。发布默认的 over-release 检查与完整 ARC 检疫并非同一种模式。

诊断结果需要结合所有权和实际行为解释，详见[内存与资源](memory.md)。不应把“没有泄漏报告”扩大成所有外部句柄都正确关闭的结论。

## 查询 API 与编译错误

```powershell
& "D:\Zan\toolchain\zanc.exe" --api File.ReadAllText
& "D:\Zan\toolchain\zanc.exe" --api WriteLine
```

`--api` 检索知识库记录并输出名字、签名和源码位置。当前实现依次检查编译器目录附近的 `knowledge/symbols.json`，再检查工作目录的 `.zanmap/symbols.json` 与 `knowledge/symbols.json`。

“索引不存在”与“查不到匹配项”是不同情况。前者需要修复 SDK 知识库安装；后者还可能由查询词、索引范围或版本差异引起。编译器内建的字符串、集合和控制台能力不一定有对应 `.zan` 文件，核对时不能只 grep `stdlib/`。

遇到编译失败，保存完整命令、工作目录、退出码和首条诊断。诊断通常有文件、行、列以及 `[E1234]` 或 `[W1234]` 编号；用编号查本版本的 `docs/DIAGNOSTIC_CODES.md`，再按具体消息查源码或最小示例。

1. 先修首条错误，再判断后续错误是否只是连锁结果。
2. 缺类先核对源文件集合、命名空间和包可见性。
3. 调用不匹配时核对真实返回类型、参数、可空性与异步声明。
4. 涉及链接时查看原生库、目标架构和工具链输出。
5. 重新编译后运行新产物，验证输出或交互，而不只看编译摘要。

`--deny-warnings` 可以把编译警告升级为错误；`--warn-level none|default|error` 控制警告级别。不要通过关闭警告、运行时守卫或泄漏检查来证明问题已修复。

## LSP：编辑器中的语言能力

`zan-lsp` 使用带 `Content-Length` 消息头的 stdio LSP 协议。当前源码声明增量文档同步，并提供补全、悬停、签名帮助、定义、引用、改名、文档与工作区符号、折叠、格式化、导入整理、内联提示和语义标记。

补全当前不声明 `completionItem/resolve` 支持。具体功能仍受编辑器客户端、工程索引和工具版本影响；“协议中声明支持”不等于每种语言形态都有完全相同的语义精度。改名前查看影响范围，改名后审查变更并编译。

早期 `docs/TOOLING.md` 中关于全文同步、缺少语义标记或内联提示的叙述已经落后；本文这些能力以当前 `src/lsp/lsp_main.c` 为准。

## DAP：调试原生程序

`zan-dap` 使用带 `Content-Length` 消息头的 stdio DAP 协议，提供启动或附加、条件断点、命中次数、日志断点、步进、线程、调用栈、作用域、变量与表达式求值等接口。

实际进程调试由 gdb 驱动，查找顺序是随包 `toolchain/debugger/bin/gdb.exe`、`ZAN_GDB` 环境变量、系统 gdb。编辑器还需要相应 DAP 客户端配置；仅放一个启动配置文件不等于已经安装调试适配器集成。

先编译含调试信息的程序，再让调试器启动该输出路径。查看正确二进制、正确源码和正确工作目录，是排除“断点不命中”的第一步。

## MCP：向 AI 暴露工具

MCP 接入方法见[AI 协作开发](ai.md)。先调用 `zan_start_here` 查看工作区、工具目录与 `available`；工具可能缺少索引、编译器或执行权限，即使已列在目录中也应检查实际返回。

`zan_compile` 适合完整的小型探针。当前 `zan_build_project` 默认寻找 `src/App.zan` 或 `src/main.zan`，但实现只把所选入口文件传给编译器；多文件项目应使用实际构建脚本，或通过可用的 `run_command` 执行明确枚举源码的命令。

结构化返回的 `ok`、`exitCode`、`diagnostics` 与 `raw` 都要读。诊断解析可能没有覆盖链接器或环境错误，不能把空诊断数组当作成功。

## 发布前验证目标与依赖

`--list-targets` 列出的目标表示编译器认识这些目标；完整交付还取决于对应运行时、sysroot、原生驱动、打包工具与设备验证。详见[平台与发布](platforms.md)和[原生互操作](interop.md)。

`--publish` 默认使用 `--link-mode shared` 装运原生驱动；只有存在相应静态库时，`--link-mode static` 才能把该驱动并入产物。资源可以通过 `--embed` 嵌入，但配置更新策略应考虑内嵌资源优先于同名磁盘文件的规则。

x86-64 的 CPU 要求应主动核对。当前桌面默认使用 AVX2 等扩展；需兼容较老 CPU 时核对 `--cpu-profile baseline` 的支持范围，并对依赖的硬件 intrinsic 和外部库分别验证。

Windows/Linux/macOS 的 x64/arm64 目标默认使用多 worker 协程调度器。当前 worker 配置依次取 `AsyncRuntime.SetWorkers`、`ZAN_CO_WORKERS`，默认取逻辑核数与 4 中的较小值；并发语义和共享数据约束见[异步与并发](async.md)。

## 核实来源

以下为维护仓库本地路径，不是二进制发行仓库的公开源码链接。

- `docs/ZANC_CLI.md`、`src/compiler/main_options_parse.c`、`src/compiler/driver/main_options.c`：CLI 选项与模式。
- `src/compiler/driver/main_driver.c`：`--api` 索引检索。
- `docs/DIAGNOSTIC_CODES.md`：错误与警告编号。
- `docs/TOOLING.md`、`src/lsp/lsp_main.c`：LSP、DAP 与调试器约定；LSP 能力以源码为准。
- `tools/mcp_server/McpServer.zan`：工具注册、项目编译输入与结构化诊断。
- `stdlib/System/Threading/AsyncRuntime.zan`、`src/runtime/io/rt_io_co_pool.c`：worker 配置。
