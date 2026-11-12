# 快速开始：安装 SDK，运行第一个 Zan 程序

核实日期：2026-11-11。本文面向当前官方 Windows SDK 的目录布局与当前源码中的 CLI 行为；下载到的 SDK 可能早于本文，请先记录 `zanc --version`，再以该版本的帮助和随包文档核对功能。

Zan 是使用 C# 风格语法、经 LLVM 提前编译为原生程序的独立语言，使用 ARC 管理对象生命周期。已有 C# 经验有助于阅读代码，但运行库、包、构建方式需要按 Zan 的实现学习。语言定位见[总览](overview.md)。

## 1. 获取完整的 SDK

官方发行入口是 [joyspinch/zanlang](https://github.com/joyspinch/zanlang)。它是 IDE 与 SDK 的二进制发行仓库；本文列出的编译器源码路径属于维护仓库，并不表示这些文件可以在发行仓库按同一路径浏览。

1. 先读发行仓库当前 README 和 [AI_README.md](https://github.com/joyspinch/zanlang/blob/main/AI_README.md)。
2. 按发行说明使用 GitHub 的 Code → Download ZIP，完整解压归档。
3. 保留 `ZanIDE.exe`、`toolchain/`、`stdlib/`、随包 DLL 与其他配套目录的相对位置。
4. 启动 `ZanIDE.exe`，或从终端调用 `toolchain/zanc.exe`。

常规使用已打包 SDK 不需要自行构建编译器，也不需要另外安装 LLVM/clang。不要只复制一个 `zanc.exe`，它还需要配套运行时、链接工具和库。

本文用 `D:\Zan` 代表 SDK 安装目录，用 `D:\work\hello-zan` 代表项目目录。请替换为本机真实路径；项目建在 SDK 目录之外。

其他系统的原生 IDE 下载、交叉编译目标和 GUI 支持是不同事项。先核对发行包实际内容，再读[平台与发布](platforms.md)，不要把 sysroot 目录当作对应平台 IDE 安装包。

## 2. 确认编译器并建立项目

以下命令使用 Windows PowerShell：

```powershell
& "D:\Zan\toolchain\zanc.exe" --version
& "D:\Zan\toolchain\zanc.exe" --help
New-Item -ItemType Directory -Force "D:\work\hello-zan\src" | Out-Null
New-Item -ItemType Directory -Force "D:\work\hello-zan\build" | Out-Null
Set-Location "D:\work\hello-zan"
```

一个最小项目只需要源码和输出目录：

```text
hello-zan/
  src/
    main.zan
  build/
```

IDE 创建的项目可以有 `zan.proj`、资源与模板配置；命令行入门不必先理解全部工程配置。

## 3. 保存完整的 Hello 程序

把下面的完整程序保存为 `src/main.zan`，文件使用 UTF-8 编码。此示例名为 `HelloZan`，没有额外扩展包或输入文件。

```zan
using System;

public class Program {
    static void Main() {
        Console.WriteLine("Hello, Zan!");
    }
}
```

`using System;` 引入所需名字，`Program.Main` 是入口，`Console.WriteLine` 写出一行文本。入口放在类中；不要把其他语言的顶层语句直接贴进这个文件。

编译并运行：

```powershell
& "D:\Zan\toolchain\zanc.exe" .\src\main.zan --auto-stdlib -o .\build\hello.exe
if ($LASTEXITCODE -eq 0) { & .\build\hello.exe }
```

预期输出为：

```text
Hello, Zan!
```

这里的输出是示例验收目标。编译返回成功后还要实际运行；如果编译失败，不应继续运行磁盘上可能残留的旧程序。

## 4. 理解源码、标准库和包的输入范围

当前 `--auto-stdlib` 默认开启，显式写出它便于说明构建意图。它会自动定位标准库与可发现的包，并按用到的命名空间拉入源码。

这不等于自动收集应用的所有源码。只给 `src/main.zan` 时，项目中另一个 `Utils.zan` 不会因为与入口相邻就自动加入。

多文件项目应显式列出所需源码。以下写法递归枚举 `src/`，也能保留带空格的文件路径：

```powershell
$zanSources = @(Get-ChildItem .\src -Recurse -File -Filter *.zan |
    Sort-Object FullName | Select-Object -ExpandProperty FullName)
if ($zanSources.Count -eq 0) { throw "src 中没有 .zan 源文件" }
& "D:\Zan\toolchain\zanc.exe" @zanSources --auto-stdlib -o .\build\app.exe
if ($LASTEXITCODE -eq 0) { & .\build\app.exe }
```

当前编译器也支持把目录作为位置参数，例如 `zanc .\src --auto-stdlib -o .\build\app.exe`，会递归展开 `.zan` 文件。目录输入会跳过构建、缓存等特定目录；需要精确控制输入时仍应枚举源文件或使用响应文件，详见[工具链](tooling.md)。

只收集当前应用的源码，不要把其他示例、测试入口与应用一起编译。HTML、图片和配置属于资源，不能靠加入 `.zan` 输入集合获得正确处理。

## 5. 区分普通构建、调试与发布

| 目的 | 主要选项 | 当前行为 |
| --- | --- | --- |
| 日常编译 | 不加调试或发布选项 | 默认 `-O2`，保留默认运行时守卫 |
| 调试排错 | `-g` | 强制 `-O0`，非发布构建自动开泄漏报告与 ARC 检疫 |
| 遇到守卫错误立即停止 | `--strict-runtime` | 守卫失败以退出码 70 结束；`-g` 不会自动开启它 |
| 发布 | `--publish` | 默认 `-Os`，剥离符号，采用发布时的驱动与诊断策略 |

调试单文件示例：

```powershell
& "D:\Zan\toolchain\zanc.exe" .\src\main.zan --auto-stdlib -g --strict-runtime -o .\build\hello-debug.exe
if ($LASTEXITCODE -eq 0) { & .\build\hello-debug.exe }
```

发布单文件示例：

```powershell
& "D:\Zan\toolchain\zanc.exe" .\src\main.zan --auto-stdlib --publish -o .\build\hello-release.exe
```

不要为了“既优化又调试”直接组合 `-g --publish`：调试选项强制关闭优化，而发布链路仍会剥离符号。ARC 检疫会保留已释放块，只适合诊断运行。

原生发布并不保证所有应用只有一个文件。GUI、数据库或浏览器组件可能需要匹配目标的驱动和运行库；按发布结果保留配套文件，并在实际目标机器上验证启动与功能。

## 6. 选择下一步需要的库

标准库随 SDK 以源码提供；扩展能力由独立包提供。`using` 导入命名空间，不负责下载或安装包，包名也不必等于命名空间。

例如 HTTP 客户端来自 `Zan.Net`，其 `HttpClient` 声明在 `System.Net.Http.Client`；GUI 核心来自 `Zan.Gui`，常用命名空间是 `Gui`。选择包、确认已安装、核对声明和调用签名，是四件需要分别确认的事。

先看[标准库与扩展库](libraries.md)按场景选择，再查[完整包目录](packages.md)。从一个文件工具、一个可交互窗口或一个本地服务开始，每次只增加可运行、可验收的行为。

## 7. 遇到错误时先定位哪一层

| 现象 | 优先检查 |
| --- | --- |
| 找不到 `zanc.exe` | SDK 路径与完整解压结果 |
| 找不到项目自己的类 | 是否列齐该项目的 `.zan` 文件 |
| 找不到命名空间 | 包是否可发现，源码实际声明的命名空间是什么 |
| 方法不存在或参数不匹配 | 用 `zanc --api` 查签名，再核对当前源码 |
| 编译成功但启动失败 | 原生依赖、目标架构、CPU 要求和进程错误输出 |
| 运行结果不对 | 实际运行的文件是否本次生成，运行时诊断与输入是否完整 |

离线查询不需要 MCP：

```powershell
& "D:\Zan\toolchain\zanc.exe" --api WriteLine
```

查询没有结果可能是索引缺失、版本不匹配或覆盖不完整。`Console`、字符串、`List<T>` 等还有编译器内建实现，仅搜索 `stdlib/` 文件不足以判断 API 是否存在。

## 8. 继续阅读

- [语言基础](language.md)与[类型系统](types.md)：声明、表达式和类型边界。
- [内存与资源](memory.md)：ARC、引用环和外部资源清理。
- [异步与并发](async.md)：异步调用、调度与共享状态。
- [原生互操作](interop.md)与[平台与发布](platforms.md)：ABI、依赖和目标验证。
- [AI 协作开发](ai.md)：配置本地工具并让 AI 依据签名与诊断工作。
- [智能体入口](../agent-guide.md)：适合交给编程助手的简明操作指南。

## 核实来源

以下为本次核对的维护仓库本地路径；SDK 中对应文件的布局可能不同。

- `zan-site/agent-guide.md`、`zan-site/src/onboarding.js`：发行入口、目录布局和接入约定。
- `docs/ZANC_CLI.md`、`src/compiler/main_options_parse.c`：输入集合、目录输入、构建与发布选项。
- `templates/console/console/src/main.zan`、`src/compiler/builtin_api.c`：程序入口与控制台调用。
- `docs/STDLIB.md`、`packages/Zan.Net/src/System/Net/Http/Client/HttpClient.zan`：标准库、包与命名空间边界。
