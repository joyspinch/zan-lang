# `.zform` 声明式外壳设计方案（阶段 2 设计稿，待确认后才动代码）

阶段 1 已把「只缺 `Props()`」的 7 个控件补完（`zform.json` 49→56）。剩下的两类不是补契约能解决的：

- **(a) 根本不是 Control**：`Table`、`Ribbon`、`Menu`、`ContextMenu`、`Chart`、`Layer`、`Scrollbar`、`Ellipsis` 都是 `static` 渲染助手，只能在立即模式里被调用，没有 `Kind()`，工厂也造不出来。
- **(b) 泛型 Control**：`DataGrid<T>`、`ListView<T>`、`Dropdown<T>`。泛型类既进不了 `ControlFactory` 的 switch，也不可能被 `GenForm` 的 `new <kind>()` 生成 —— 所以 `.zform` 今天**完全无法声明一张表格**。

本方案的结论：**新增 4 个非泛型外壳控件 + 2 个声明式子项数组（`columns` / `items`）**，不改 `.zform` 的语法基元（仍是 JSON），不引入任何表达式语言，也不动现有泛型控件。

---

## 1. 为什么外壳很便宜：助手吃的都是纯数据

查证过的入口签名（全部非泛型）：

```zan
DataTable.RenderSource(app, x, y, w, h, List<DataColumn>, DataSource, DataTableState)
Menu.RenderVertical(app, x, y, w, bottomY, itemH, ...)
ContextMenu.Render(app, mx, my, List<string> labels)
Ribbon.RenderGroups(app, x, y, w, List<RibbonGroup> groups)
Chart.*(List<ChartSeries>)
```

所以外壳 = 「持有数据 + `OnPaint` 里调一次助手」，**不是重写控件、也不是照搬 DevExpress**。

---

## 2. 表格：直接声明泛型 `DataGrid<T>`（已按用户决定改向，替代早先的 `DataGridBox` 草案）

> **本节为已实施方案。** 早先草案是造非泛型外壳 `DataGridBox` + `List<DataRow>` 字符串行；用户明确否掉了 DataRow 中间层（“传泛型 List 用来取数据而不是 DataRow”），改为让 `.zform` 直接声明既有的 `DataGrid<T>`。

关键事实：`GenForm.TypeOf` 早已支持 `of` 泛型键（`ListView<string>` 就是这么来的），所以 `"kind": "DataGrid", "of": "Order"` 在编译期天然生成 `DataGrid<Order>` 字段——不需要任何新控件类。数据仍由 code-behind `grid.Bind(orders)` 传入 `List<Order>`，网格每帧直读调用方实体，零拷贝、无字符串化中间行。

`columns` 展开成既有的类型化列 API（字段名拼错由 Zan 编译器直接报错）：

```json
"columns": [
  { "field": "name",  "title": "名称", "width": 140 },
  { "field": "spend", "title": "花费", "width": 100, "type": "real", "decimals": 2, "money": "¥", "sum": true },
  { "field": "active","title": "在投", "width": 60,  "type": "bool" }
]
```

生成：

```zan
this.grid.Col("名称", 140, __r => __r.name).Field("name");
this.grid.RealCol("花费", 100, 2, __r => __r.spend).Field("spend").Money("¥").Sum();
this.grid.BoolCol("在投", 60, __r => __r.active).Field("active");
```

- `Props()`：`DataGrid<T>` 直接发布 `st` 上已有的开关（`rowNumbers`/`selectable`/`striped`/`rowHeight`/`summary`/`filterRow`/`statusBar`/`groupPanel`/`allowAddRow` 等），经 `GetExtra`/`SetExtra` 转发，**不新增字段**。
- `rowFactory: true` 生成 `RowFactory(() => new <of>())`，让新行占位符可用。
- 运行期/设计器：`ControlFactory.Create("DataGrid")` 拿不到实体类型，只能造 `DataGrid<string>` 预览实例，由 `previewColumns` 铺列头；真正的类型化列只在编译期由 GenForm 展开。
- 覆盖测试：`tests/gui/zform_grid.zform` + `tests/gui/zform_grid_test.zan`（conformance_gui_zform_grid）。

---

## 3. 菜单 / 工具条 / Ribbon：`items` 对象数组

现状（查证）：唯一的子项通道是 `ToolStrip` 的 `options` **扁平字符串**，而且背后藏着一套**没有进 schema 的迷你语法**：

```
"新建:plus|-|>设置:gear"      // label:icon 、"-" 分隔符、">" 右对齐
```

`zan_form_schema` 里 `options` 只是个字符串，这套语法查不到 —— 这本身就是逼 AI 回去翻源码的直接原因之一。

往这串字符串里再塞快捷键、二级菜单、enabled，就是在造第二门语言（正是上次否掉「`.zform` 声明式条件键」的同一个理由）。所以改成对象数组：

```json
"items": [
  { "label": "文件", "items": [
      { "label": "新建", "icon": "plus", "shortcut": "Ctrl+N" },
      { "kind": "separator" },
      { "label": "退出", "enabled": false }
  ]},
  { "label": "视图", "kind": "check", "checked": true }
]
```

- `options` 作为兼容输入保留（继续解析旧迷你语法），并且**把那套迷你语法写进 schema**，让今天已有的 `.zform` 也能被查到。
- 新外壳：`MenuBarBox`（顶栏菜单，`Menu.RenderVertical` + 下拉）、`RibbonBox`（`Ribbon.RenderGroups`，`items` 的两级天然映射到 group/item）。
- `ContextMenu` **不做外壳**：它是弹层，按定义由代码在命中时唤起，不该出现在画布上。

---

## 4. 有些应该从调色板移除，而不是包装

调色板里列着「拖进去造不出任何东西」的 kind，比缺少更糟。建议移除：

- `Table` —— 声明式 `DataGrid`（`of` + `columns`）完全覆盖且更强；
- `Layer` / `Scrollbar` / `Ellipsis` —— 立即模式的基础设施，不是可放置控件；
- `ContextMenu` —— 见上。

`Chart` 给一个 `ChartBox` 外壳，但**序列数据不进 `.zform`**（那是运行时数据）：只声明 `type`/`stacked`/`legend` 这类外观和序列名字列表。

---

## 5. 三条建树路径怎么吃这两个数组（这才是真正的工作量）

- **`FormBuilder.MakeControl`**：今天只搬 5 个属性，`props`/`label`/`bind`/`bindProp`/`childTab`/`gap`/`pad` 全丢。加 `columns`/`items` 必须连这几个一起补齐，否则运行期加载器与编译期 `GenForm` 继续两套语义。
- **`GenForm`**：编译期把 `columns`/`items` 展开成构造语句。
- **设计器 Inspector**：需要一个「子项列表编辑器」UI —— 这是本阶段最大的一块，属于 UI 工作而非数据结构工作。

**分步原则：先保真、后可编辑。** 先让数据面（外壳 + 两条数组 + FormBuilder/GenForm/schema）打通，同时保证设计器**至少不会吞掉**这两个数组（加载→保存必须原样保留）；Inspector 的子项编辑器单独一步做。

---

## 6. 明确不做的事 / 已知代价

- 不改 `.zform` 语法基元，不加表达式语言，不做 `showIf`。
- 不引入 `DataRow`/`RowListSource` 字符串中间行：声明式表格与代码模式共用同一个 `DataGrid<T>`，取值回调直读实体，不存在两套数据模型。
- 序列化：新键**追加**在现有键之后，避免旧文档产生解析歧义（与上一轮 `class`/`span` 键序的处理一致）。
- 每一步都要复跑：`policy_zform_schema`、`policy_control_factory`、`policy_gallery_coverage`、smoke，以及全仓库 31 份 `.zform` 的加载→保存（作者几何字段零改写、无凭空增删键）。

---

## 7. 建议顺序

| 步 | 内容 | 说明 |
|---|---|---|
| 1 | `DataGrid` 的 `of` + `columns` + FormBuilder/GenForm/schema/policy（已完成） | 单独这一步就能把 `GridColumns.zan`(206 行) 与 `GridPane` 样板变成声明式 |
| 2 | `items` + `MenuBarBox` / `RibbonBox` + `ToolStrip` 迷你语法进 schema | 覆盖原工程占比最大的菜单/Ribbon 项 |
| 3 | 调色板清理（移除 4 个假 kind）+ `ChartBox` | 低风险，消除"拖进去没反应" |
| 4 | Inspector 子项列表编辑器 | UI 工作，可独立排期 |

约 4 个会话。第 1 步风险可控且收益立刻可见，建议从它开始。
