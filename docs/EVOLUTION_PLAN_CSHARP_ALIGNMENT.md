# Zan 与 C# 对齐并超越：标准库与开发辅助能力演进落地规划

## 一、 背景与愿景

Zan 采用 C# 风格的现代语法体系，初衷是让开发者拥有与 C# 相当的优雅表达力与高生产力，同时彻底剔除 CLR 虚拟机沉重的运行时包袱，获得原生 AOT 毫秒级启动、超低内存与确定性 ARC 原生性能。

经过深入代码探查（`stdlib/`、`src/compiler/`、`src/lsp/`、`src/ide_zan/`），Zan 目前已经具备了非常扎实的基础：
1. **语法与查询**：已原生支持完整 LINQ 查询语法糖（`from ... in ... where ... select ... join ... group ... by`）以及 `stdlib/System/Linq/Enumerable.zan` 丰富扩展方法。
2. **序列化**：已拥有编译期 Source Generator 机制（`GenJson.zan` + AST 元数据重写），零运行时反射、高性能静态绑定。
3. **工具链**：已拥有完整的自研 LSP (`zan-lsp`)、DAP (`zan-dap`) 和纯标准库组件自举的 `ZanIDE`。

为了彻底抹平开发者从 C# 迁移到 Zan 的心智摩擦，并发挥 Zan 在原生体系下的极致性能，制定本演进落地实施路线图。

---

## 二、 四大核心模块改造设计方案

### 模块 1：ARC 引用环与闭包逃逸预警（消除最大心智负担）
* **现状痛点**：C# 有分代 GC 兜底，事件和闭包捕获随便写；而在 Zan 中，所有闭包默认在堆上分配，被捕获的局部变量与 `this` 均会被打包进 ARC 管理的 Cell 中。当控件回调 `btn.OnClick = () => { this.OnDoSomething(); }` 且 `btn` 为 `this` 成员时，会形成无感强引用环导致严重内存泄漏。
* **演进方案**：
  1. **静态逃逸与引用环分析 (`src/compiler/checker.c`)**：
     - 在语义检查器阶段，遍历 Lambda AST 并结合当前作用域分析。
     - 若 Lambda 捕获了 `this` 或包含该委托字段的宿主实例，且该委托被赋值回该实例自身的字段/事件订阅，发出编译期警告 `DIAG_WARNING`: `Potential ARC reference cycle detected: closure captures 'this' which directly or indirectly owns this delegate`.
  2. **LSP 快速修复与闭包弱引用语法扩展 (`src/lsp/`, `src/compiler/`)**：
     - 在 `zan-lsp` 的 `handle_code_action` 中提供 CodeAction: `Convert capture to weak reference / break cycle`。
     - 编译器支持弱捕获注解或自动弱化修饰。
  3. **运行期 ARC 泄漏定位 (`src/runtime/` + `zan.checkLeaks`)**：
     - 强化现有 `workspace/executeCommand: zan.checkLeaks`，在调试期能给出对象分配栈与残留引用路径。

---

### 模块 2：Zero-Alloc 轻量流式数据管道（标准库人体工学升级）
* **现状痛点**：目前 Zan 的 LINQ 扩展方法（如 `Where`、`Select`）通过直接返回新的 `List<T>` 实现。虽然直观，但在多段链式调用（如 `.Where(...).Select(...).Take(...)`）中会产生多次中间临时 `List` 分配与释放。
* **演进方案**：
  1. **基于值的轻量迭代器切片（ValueEnumerable / Struct Iterator）**：
     - 在 `stdlib/System/Linq/` 中补充轻量级值结构体（`ValueWhereIterator<T>`, `ValueSelectIterator<T, R>`）。
     - 支持 `foreach` 鸭子类型（`MoveNext()` / `Current()`）直接消费，使得链式过滤与转换零额外堆分配，内联至等同于手写 `for` 循环。
  2. **Array / List 高性能切片（Span/ReadOnlySpan 心智）**：
     - 扩展 `Slice<T>` 与 `ArraySegment<T>`，提供零拷贝只读切片及常用字符串/集合分词操作，彻底对齐现代 C# 的 `Span<T>` 体验。

---

### 模块 3：编译期数据映射与实体扩展（现代业务开发体验）
* **现状评估**：已有 `Json.Deserialize<T>` 的编译期生成机制（`GenJson.zan`）。
* **演进方案**：
  1. **扩展实体映射支持**：
     - 支持更加丰富的 Attribute：如 `[JsonProperty("alias_name")]`、`[JsonRequired]`、`[JsonDefault(value)]`。
     - 扩展支持 Dictionary 嵌套绑定与自定义类型转换器。
  2. **轻量 SQLite 实体映射（Micro-ORM 式体验）**：
     - 基于类似 GenJson 的编译期 AST 扫描机制，为标注了 `[Table]` / `[Column]` 的实体类自动生成 `DbBind` 读写代码。
     - 开发者写 `db.Query<User>("SELECT * FROM users WHERE age > ?", 18)` 时，无需手动 `row.GetInt(0)`、`row.GetString(1)`，由编译期直接生成高效解包赋值代码。

---

### 模块 4：GUI 体验与表单开发（ZanIDE 极速所见即所得）
* **现状优势**：ZanIDE 严格遵循“禁止自绘，全部使用标准库组件”，架构轻盈且纯粹。
* **演进方案**：
  1. **表单设计器（Designer）与代码即时双向同步**：
     - 优化 `packages/Zan.Gui/src/Gui/Designer/Designer.Form.zan`，对 `.html` 设计稿提供更丝滑的可视化控件拖拽、属性栅格编辑与对齐吸附。
  2. **GUI 布局毫秒级实时微调（Live Layout Preview）**：
     - 当修改 `.html` 表单设计稿属性时，无需重新编译整个项目程序，通过内部 IPC / 管道将表单定义差量推送给预览窗口，实现毫秒级“所见即所得”。
  3. **工业级虚拟化大列表与树表组件**：
     - 引入 `VirtualListView<T>` / `VirtualDataGrid<T>`：只对当前可视区域创建 DOM / 控件元素，百万级数据流畅滚动。

---

## 三、 实施阶段与排期路线

| 阶段 | 目标与里程碑 | 核心产出物 | 验证方式 |
| :--- | :--- | :--- | :--- |
| **Phase 1: ARC 防护与闭包预警** | 在编译器与 LSP 建立循环引用预警体系，彻底消除初学者从 C# 转 Zan 的泄漏恐慌 | 1. `checker.c` 闭包循环捕获检测与警告<br>2. `zan-lsp` 对应警告与诊断发布<br>3. 典型 GUI 事件回调防漏用例 | 增加 `tests/conformance/arc_cycle_warn.zan`，运行 `scripts/test.ps1 smoke` 验证 |
| **Phase 2: Zero-Alloc 流式计算与切片** | 提供类似 C# LINQ 链式手感但无中间堆分配的流式结构体操作 | 1. `stdlib/System/Linq/ValueEnumerable.zan`<br>2. `List`/`Array` 零分配流式扩展方法 | 编写针对内存分配的性能探查测试，验证中间分配为 0 |
| **Phase 3: 编译期实体映射升级 (JSON+DB)** | 让日常业务开发中 JSON 处理与数据库 CRUD 像 C# Dapper 一样丝滑 | 1. `GenJson.zan` 支持 `[JsonProperty]` 等丰富注解<br>2. 轻量编译期 SQLite 实体绑定扩展 | 编写实体自动解析测试用例，验证编译期展开无反射性能 |
| **Phase 4: ZanIDE 与表单设计器极速体验** | 提升界面开发生产力，巩固轻量原生响应护城河 | 1. 表单设计器体验微调与对齐辅助<br>2. 虚拟化大列表控件<br>3. 局部布局快速预览能力 | 运行 ZanIDE 实际体验表单可视化排版与大列表加载 |

---

## 四、 现代语言设计哲学与 AI 友好型架构

Zan 的演进目标绝不仅是做 C# 的克隆版，而是结合现代编程语言（Rust / Swift / Zig / TypeScript）的优势，并面向 **“AI 协同开发与代码生成”** 场景打磨出独特的护城河：

1. **Bug 尽量在编译期爆发（Shift-Left 编译期拦截）**：
   - **ARC 引用环编译期预警**：对闭包捕获 `this` 并回赋给自身成员/事件的模式，直接在语义分析阶段发出 Warning/Error，杜绝隐蔽内存泄漏。
   - **严格空安全（Strict Null Safety）**：向非空类型（`T`）与可空类型（`T?`）演进，未做窄化检查前杜绝解引用。
   - **模式匹配穷尽性检查**：`switch` 必须穷尽所有枚举值或提供默认处理，防止遗漏分支在运行时静默暴死。

2. **AI 友好型语言特性（AI-First Ergonomics）**：
   - **语法高正交、零二义性**：规则清晰、避免上下文推导倾斜，减少大模型理解和生成代码的幻觉。
   - **可行动的诊断信息（Actionable Diagnostics）**：编译器报错直接提供具体原因和修复建议（Fix-it Suggestion），使 AI 具备高效的自纠错循环能力。
   - **编译期自解释元数据（Source Generator）**：通过编译期自动生成序列化与数据访问代码，避免开发者与 AI 手写大量脆弱的样板反射与胶水代码。

