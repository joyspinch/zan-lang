# Zan 标准库组件封装规范 (Stdlib Component Standards)

> 适用范围：`stdlib/` 下所有组件（System.*、Gui、Game、Platform）。
> 目标：开箱即用、库内消化潜在问题、高性能安全封装、保持灵活性、杜绝屎山。
> 本文与 `docs/CODING_STANDARDS.md`（编译器 C 代码规范）互补，约束 `.zan` 标准库层。

---

## 1. 三层架构（每个组件必须分层）

```
门面层 Facade   —— 用户默认接触的 API：零配置可用，链式可选配置
能力层 Core     —— 协议/算法实现：状态机、编解码、缓冲管理
原语层 Native   —— FFI / reactor / 系统调用绑定：仅供能力层使用
```

规则：
- 用户默认只需要门面层。示例必须体现"Workerman 式"体验：
  ```csharp
  HttpServer s = new HttpServer("0.0.0.0", 8080);  // 一行可用
  s.OnRequest((req, resp) => { ... });
  await s.Start();
  ```
- 可选配置用链式方法（`.MaxConnections(1024).Timeout(30)`），全部有合理默认值。
- 能力层公开（供高级用户下钻），原语层的 `DllImport` 一律 `static` 私有，不外泄。
- 层间只允许向下依赖，禁止跨层调用。

## 2. 安全与正确性（库内消化，不让使用者踩坑）

1. **禁止字符串拼协议**：
   - JSON 必须走 `JsonValue`/`Json.Serialize`，禁止 StringBuilder 手拼（转义/嵌套一定会错）。
   - SQL 必须走参数绑定（`?`/`$n` 占位），禁止值内联；标识符（表/列名）白名单校验 `[A-Za-z_][A-Za-z0-9_]*` 并加引号。
   - 二进制协议走 Buffer 抽象，不拿 string 当字节数组做业务逻辑。
2. **统一错误模型**：失败必须可区分——抛异常（`try/catch` 已可用）或返回带 `IsError/LastError` 的结果对象；禁止静默返回 `""`/`0`/空集合冒充成功。
3. **资源确定性释放**：`calloc`/`free`、句柄 open/close 必须逐路径配对（含所有错误分支）；对外暴露的资源类提供 `Close()` 且析构器兜底。
4. **并发安全**：跨协程共享的状态用 `AsyncGate`/`Gate`/`AtomicInt` 保护；文档必须注明每个类型的线程/协程安全级别。
5. **真异步原则**：`XxxAsync` 必须真正挂起到 reactor（`await Gate.Park` / `Socket.*Ov`）；同步实现禁止套 Async 后缀冒充（现存的假异步包装要逐步清理）。

## 3. 性能

1. **流式优先**：大数据路径提供游标/流（`QueryReader`、`Stream`），禁止只有"全量物化"一种形态。
2. **缓冲复用**：热路径的 scratch buffer 复用（参考 RedisClient），不逐次 calloc。
3. **循环规范**：`Count`/`Length` 在循环外取一次存局部变量；逐字符处理禁用 `Substring(i,1)`（用 `s[i]` 字节访问）；排序禁用冒泡（用归并/插入混合）。
4. **语句/连接缓存**：网络与 DB 组件必须有池化 + prepared 缓存能力（可关闭）。
5. **批量 API**：凡逐条写入的组件必须配 `XxxMany`/`XxxBulk` 批量形态。

## 4. API 设计

1. 命名与 .NET 习惯对齐（`Open/Close/Read/Write/TryParse`），同一动词全库含义一致。
2. 每个公开成员必须有 `<summary>` 文档注释 + 顶部 Usage 示例。
3. 默认值哲学：90% 场景零参数可用；不确定的默认值宁可保守（安全/耐久优先）。
4. 破坏性/危险操作显式命名（`WhereRaw`、`UnsafeXxx`），并在注释中警告。
5. 平台差异在原语层用 `#if WINDOWS/#else` 消化，门面层 API 跨平台一致。

## 5. 反屎山守则

1. 单一职责：一个类一件事；超过 ~500 行先考虑拆层。
2. 重复三次的样板必须提取 helper（例：驱动里的 C 字符串拷贝 `CopyText`）。
3. 内部 helper 不进公共命名空间；实验代码进 `_scratch/`，不进 stdlib。
4. 修 bug 必须补 `tests/` 用例；新组件必须带 conformance 测试。
5. 禁止"文档先行但无实现"的空壳类（现存空壳要么补齐要么删除）。

## 6. 数据绑定（Gui 组件契约）

> 背景：审查发现 `Binding<T>`（System）与 `Signal*`（Gui/Reactive）两套机制在
> Widget 中混用（56 个 Widget 中 18 个双通道并存，Switch 同挂四条通道）。
> 本节为收敛契约，2026-09 起新代码必须遵守，存量逐步收敛。

1. **外部数据绑定的唯一协议是 `Binding<T>`**（`stdlib/System/Binding.zan`）：
   - 组件作者把可绑定属性声明为 `Binding<T>` 字段：主值命名 `data`，辅值 `dataXxx`；
   - 使用方 `control.data = model.field;`（实时）或 `= 常量`（常量绑定），不感知机制；
   - 禁止为同一控件再发明第二条同语义通道（历史双通道已收敛：
     Switch 四通道 → `data` + `dataValue`，SelectBox `size` → `Size`，
     Input 缓冲 `model` → `editSig`）；
   - **Signal 保留式工厂已整体移除**（2026-09-02）：`Switch.Bind` /
     `Input.Bind` / `SelectBox.Bind` / `Slider.Bind` / `Radio.Bind` / `Rate.Bind` /
     `InputNumber.Bind` / `TextArea.Bind` / `ColorPicker.Bind` 及 Signal 共享构造器
     全部删除（Checkbox 因在途改动暂缓）。动态宿主（PropertyGrid / Designer /
     FilePicker 等没有编译期字段可绑的场景）不走 Binding，统一用控件自身的
     `GetText/SetText` / `GetProp/SetProp` 属性协议或普通字段；控件族内部组装
     （如 ChoiceGroup 组子 Radio）可共享内部 Signal，但构造器必须注明
     "内部组装专用"。
2. **`Signal*` 是控件内部状态缓冲**（编辑缓冲、滚动位置、级联选择等），不是绑定协议：
   - 不得作为 Widget 公开绑定 API 的参数/字段对外提供；
   - 变更检测统一走 immediate-mode 帧轮询（`Version()` / 值比对），
     **禁止事件式变更通知**（`Signal.Changed`/`ChangeTracker` 已删除：全仓零订阅的死器官）。
3. **同步契约**：双向绑定控件 `override` 基类的 `SyncBinding()`（model→UI 拉取），
   **由 `Control.RenderTree` 在每帧 OnPaint 前统一驱动**，控件无需在 OnPaint
   开头自调（存量控件的历史自调幂等无害，随命名收敛逐步清理）；UI→model
   回写统一在编辑汇聚点（如 `Edited()`）调用 `data.Set(...)`。
   冲突解法（谁是真相）必须在控件头注释写明。
4. **命名表**：同一语义角色全库同名——主值绑定 `data`；尺寸档 `Binding<string> Size`
   （`"small"`/`"medium"`/`"large"`）；标签/文案 `text`。禁止大小写漂移
   （`Value`/`value`/`data` 混指一物属违规）。
5. **每控件绑定通道声明**：Widget 类头注释必须有一段"绑定通道"说明
   （通道字段、方向、真相归属），照 `SelectBox.zan` 头注释样式。
6. **文档承诺必须与实现一致**：宣称"响应式/变更通知"的成员必须有真实订阅者，
   否则删除（反例：`Signal.Changed` 曾零订阅空转）。
7. **字符串 `bind` 通道的边界**：`Control.bindPath`/`bindProp` 是**设计时声明
   元数据**（设计器 Inspector、Serialize 行格式、GenForm 生成器读写），运行时
   由 `ChildWindow` 消费——每帧把 JsonValue 状态实体按路径同步进控件/回写。
8. **`Props()` 指针必须优先指向活的外部通道**：属性 spec 同时存在 Binding 与
   内部 Signal 背书时，`data != null` 分支必须指向 Binding（`spec.num = data`），
   仅在无外部绑定时才落到内部缓冲（`spec.snum = model`）。否则 `SetProp` 写进
   内部缓冲后会被下一帧 SyncBinding 用旧模型值覆盖（P5 修复的真实缺陷类）。
   `PropSpec` 的 `s*` 字段仅用于控件自身内部状态接线（如 ChoiceGroup 组子
   Radio），不构成对外 API。
   它服务"对话框 ↔ 状态实体"场景，与代码内实时绑定 `Binding<T>`（第 1 条）
   分工不同、互不替代；新控件不得再开第三种绑定写法。绑定机制的派生缓存
   不进 Control 基类——它属于消费方（如"模型侧确认值"快照归 ChildWindow
   的旁表所有，`Control.bindSnapshot` 已拆除）。

## 7. DataTable 单元格内嵌（Gui 组件契约）

> DataTable 是即时模式自绘表格（虚拟滚动，无每格控件树），但单元格内嵌
> 图标/按钮/图表/真控件是商业网格的基本能力。2026-09 起提供三层机制，
> 选型从轻到重，禁止在渲染循环外自绘或绕过热区注册。

1. **声明式内置列优先**（`DataColumn.IconCol / ButtonCol / SparklineCol`，
   cellType 6/7/8）：图标走 `Canvas.DrawGlyph` tabler 名；按钮画圆角小按钮 +
   `Ui.Activate` 热区/键盘激活，点击把 `st.hitRow/st.hitCol` 指向本格后
   Raise `CellClick`（处理器与普通单元格点击同入口）；迷你图按 `,` 分隔
   数值序列画 accent 迷你柱状图。三类列默认 `sortable=false/filterable=false`，
   导出走默认文本路径无需改动。新增 cellType 必须在 `DrawCellRangeCtx` 加
   渲染分支 + 本条登记，消费面只在 Render.zan。
1a. **声明式组件列 `CompCol`**（cellType 9，2026-09）：一列内嵌"真正的
   控件库组件"（如 BandGrid 矩阵）但仍走数据绑定——`DataColumn.CompCol(
   title, width, compId)` 声明列，`DataColumn.Shape(c, rows, cols)` /
   `DataColumn.Heat(c, band)` 链上配置形状与色带；绑定 `DataGrid<T>` 时用
   `grid.CompCol(title, width, compId, u => u.field)` 传类型化读取器
   （`GridNums<T>` 委托），单元格值 = 逗号分隔数值序列（与迷你图同一约定，
   序列化边界在 `GridColumn.Raw()` 内）。引擎侧 `CellComp` 注册表按
   compId 找适配器（内置 `bandgrid` → `BandGridComp`），以
   `RowKey:col` 为键**池化真控件实例**（同 §7.2 的硬责任），形状变化
   （rows×cols）时重建；组件内点击归一为 `st.hitRow/hitCol` +
   `st.compR/compC`（格内矩阵位置，`DataTable.CompPosR/CompPosC` 读取）
   后 Raise `CellClick`。未知 compId fail-soft 画占位块。选型：要在
   单元格里内嵌整个交互组件（矩阵/表格/开关组）但不想手写 Provide/池化 →
   CompCol；需要任意控件布局 → 下面的 CellWidgetProvider。
2. **真控件插槽 `CellWidgetProvider`**（`st.cellWidgets`）：需要输入框、
   下拉、开关等真控件交互时继承它，`Provide()` 按 `(row, col)` 返回
   `Control`（null 退回内置渲染）。渲染循环 `MeasureTree → Arrange(格矩形) →
   RenderTree`，裁剪在格内。**池化是实现的硬责任**：以 `src.GetRowKey(row)`
   为键缓存控件实例（虚拟滚动下同一显示槽位会滚过任意多行），否则焦点
   丢失、状态闪变。provider 需实现 `Reset()` 供网格重建时清池。
3. **自绘兜底 `CellStyler.PaintCell`**：返回 true 完全接管该格（画布已裁剪），
   用于着色、徽标、城市色点这类非交互定制。交互热区自行注册时必须用
   `app.focus.AllocId()` + `app.hitTester.RegisterRect`，点击判定走
   `Ui.Clicked/Ui.Activate`，禁止裸比 `app.mouseX/Y`（看不见上层弹层）。

选型决策：只展示 → 内置列；单格内嵌完整交互组件且要数据绑定 → CompCol；
要真控件交互 → CellWidgetProvider；只是画 → PaintCell。四者在渲染循环的
优先级：cellWidgets > styler > 内置 cellType（6/7/8）与组件列（9）。

## 8. 现存违规清单（改造 backlog）

以下已解决项移出待办（✅）；未解决项仍有效。

| 组件 | 违规 | 处置 |
|---|---|---|
| Orm/QueryBuilder, Model | ~~SQL 值内联拼接、标识符不校验~~ | ✅ 已解决：参数化绑定 |
| Sqlite/MySql/Postgres | ~~无参数绑定~~ | ✅ 已解决：bind FFI / 二进制协议 / PQexecParams |
| Orm/ModelRow.ToJson | ~~手拼 JSON，值含引号即产出非法 JSON~~ | ✅ 已解决：改走 JsonValue |
| Threading/Channel | ~~Send/Receive 不阻塞，假实现~~ | ✅ 已解决：基于 Gate/异步锁重写（现为 string 专属非泛型） |
| Socket 旧 `*Async` | ~~select busy-wait~~ | ✅ 已解决：迁移 AsyncSocket，标记废弃 |
| Enumerable.OrderBy | ~~冒泡 O(n²)~~ | ✅ 已解决：归并排序 |
| 各驱动/File | ~~`int` 承载指针~~ | ✅ 已解决：统一句柄封装 |
| ODBC | 无参数绑定（`OdbcConnector.zan` 无 Param/Bind） | 未解决 |
| Thread.SleepAsync / File.*Async / Model.*Async | 同步冒充异步（如 `File.zan` 的 `ReadAllTextAsync` 仍是同步包装） | 未解决：真异步或去 Async 后缀 |
| IO/File | 整读整写仍为主流形态，流式路径不普及 | 未解决：逐步提供 Stream 家族 |
| Gui/Reactive | ~~`Signal.Changed` 事件通知 + `ChangeTracker`（零订阅死代码，文档承诺与实现相反）~~ | ✅ 已删除：变更检测统一帧轮询（§6.2） |
| Gui Widget 绑定通道命名 | ~~同一角色多名（`value`/`Value`/`data` 混指、SelectBox 尺寸档 int 漂移）~~ | ✅ 已收敛（2026-09-02）：Switch 四通道收敛为 `data` + `dataValue`（`valSignal`/`BindValue` 删除）；SelectBox `size`(int 0/1/2) → `Size`(string，顺带修掉设计器 tiny 档与 Small/Medium/Large 助手的既有错位)；ChoiceGroup `Value` → `data`；Input 私有编辑缓冲 `model` → `editSig` |
| Gui Widget Binding/Signal 双通道 | ~~18 个 Widget 仍存 Signal 保留式外部通道与工厂~~ | ✅ 已移除（2026-09-02，P5）：10 个 Signal 工厂 + Signal 共享构造器删除，约 25 处调用点迁移到构造器 + `data` 绑定或 `GetText/GetProp` 属性协议；PropertyGrid 平行信号表删除、改走属性协议；`Props()` 指针统一"优先外部通道"（§6.8）；Checkbox 因在途改动暂缓，仅剩族内组装（ChoiceGroup→Radio）的注明内部 Signal |
