# Zan 编程语言 (Zan Programming Language)

<p align="left">
  <a href="README.md">English</a> | <b>简体中文</b>
</p>

Zan 是一门面向现代系统级、桌面客户端与实时应用开发的强类型编程语言，融合了 **C# 亲和语法**、**LLVM 本地编译后端** 与 **ARC 确定性内存管理**。

---

## 核心特性 (Features)

- **亲切舒适的语法**：采用主流 C# / Java 风格语法，告别晦涩的特殊符号与繁琐的生命周期标注。
- **AOT 本地机器码编译**：通过 LLVM 后端直接编译为原生无依赖机器码（支持 Windows / Linux / macOS / Android / iOS）。
- **ARC 确定性内存管理**：自动引用计数，对象生命周期结束即刻析构，彻底告别 GC 停顿（Zero GC Pauses），保证低延迟与稳定帧率。
- **高性能值语义**：结构体分配在栈上，配合写时复制（Copy-on-Write）集合，兼顾安全与吞吐性能。
- **极简原生 FFI**：支持直接通过 `DllImport` 调用系统底层 API 与 C 语言原生动态库。
- **自宿主轻量级 IDE (ZanIDE)**：完全使用 Zan 语言编写的轻量 IDE，深度集成可视化界面设计器、LSP（代码补全/诊断）与 DAP（交互式调试）。
- **源码级标准库 (Stdlib)**：标准库以纯 `.zan` 源码形式组织，按需即时拉入编译，零冗余分发。

---

## 代码速览 (Quick Example)

### 基础语法

```csharp
using System;

namespace HelloWorld;

struct Point {
    public float X;
    public float Y;

    public float Length() => Math.Sqrt(X * X + Y * Y);
}

class Program {
    static void Main(string[] args) {
        var p = Point { X = 3.0, Y = 4.0 };
        Console.WriteLine($"Point: ({p.X}, {p.Y}), Length: {p.Length()}");
    }
}
```

### 声明式 GUI 示例

```csharp
using Gui;
using Gui.Widget;

class MainWindow : Window {
    public MainWindow() {
        Title = "Hello Zan GUI";
        Width = 400;
        Height = 300;

        Content = new VStack {
            Spacing = 16,
            Children = {
                new Text("欢迎使用 Zan 现代化声明式 GUI"),
                new Button("点击我") {
                    OnClick = () => Console.WriteLine("Button Clicked!")
                }
            }
        };
    }
}
```

---

## 编译与构建 (Building)

本项目基于 CMake 构建，依赖 **LLVM 17+** 开发库环境。

```bash
# 生成 Ninja 构建文件
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 编译编译器与全套底层工具链
cmake --build build
```

编译生成的目标程序：
- `build/zanc`：Zan 语言编译器。
- `build/zan-lsp`：语言服务服务端（LSP）。
- `build/zan-dap`：调试适配器（DAP）。

### 编译你的第一个 Zan 程序

```bash
# 编译并直接运行（自动拉入所需标准库）
build/zanc src/main.zan -o _scratch/app.exe && _scratch/app.exe

# 发布模式构建（代码优化与符号剥离）
build/zanc src/main.zan --publish -o app.exe
```

详细命令行用法与跨平台交叉编译参考：[zanc 命令全解文档 (docs/ZANC_CLI.md)](docs/ZANC_CLI.md)。

---

## 项目结构 (Project Structure)

```
src/
├── compiler/          # 编译器前端与后端 (C11: 词法、语法解析、语义绑定、类型检查、LLVM IR 生成)
├── runtime/           # 运行时系统 (C11: ARC 引用计数、异步反应堆、调度器、GUI 渲染驱动)
├── lsp/               # Language Server (zan-lsp) 及 IntelliSense 引擎
├── dap/               # Debug Adapter (zan-dap) 及调试器引擎
├── ide_zan/           # ZanIDE 自宿主集成开发环境 (纯 Zan 语言自研实现)
├── common/            # 公共 C 工具库 (JSON、RPC 协议通信等)
└── selfhost/          # 自宿主编译器源码 (.zan)
stdlib/                # 标准库 (.zan 源码及多平台原生驱动束)
templates/             # 预置工程向导模板 (控制台、GUI、游戏、服务端、类库等)
examples/              # 官方精选可运行示例程序
tests/                 # 完备的自动化测试套件 (语言一致性、ABI、运行时、GUI、LSP、DAP)
docs/                  # 语言规范、架构设计与操作文档
```

---

## 许可证 (License)

本项目采用 [Zan 社区源码许可协议 (ZCSL)](./LICENSE.zh-CN)（英文正本见 [LICENSE](./LICENSE)）：

- ✅ **免费商用 (Free for Commercial Use)**：个人与企业均可免费无偿用于商业开发与生产服务部署。
- ✅ **产物完全自由 (Runtime & Output Exception)**：使用 Zan 编写的代码及由 Zan 编译生成的任何可执行程序/库文件，其知识产权完全归开发者所有，分发、闭源、商业售卖不受任何限制。
- ❌ **禁止二开分发 (No Derivative Redistribution)**：严禁修改 Zan 编译器/运行时/工具链源码后对外公开发布衍生版本、改名换皮发布或打包转售。
