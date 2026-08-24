# `.zform` 能不能承载 `_devin_ref_tmp` 这套工具软件（修正版）

**先修正我上一版的算法。** 你说得对：不该按 DevExpress 的清单 1:1 折算。
上一版把 `UnboundSourceProperty`（373 个）、`RepositoryItem*`（34 个）、`BarDockControl`、`TileViewItemElement` 这类**框架管道**也算进了「缺口」——
Zan 的表格是虚表（`VirtualList` + `Gui.Component.DataTable`，取值走回调），这些脚手架本来就不存在。
剔掉后原工程 3648 个控件实例里，真正的「界面元素」约 3200，缺口的性质也变了：**不是缺控件，是缺「声明式契约」。**

## Zan 侧其实已经有的东西

`stdlib/Gui/Widget/` 里已有：`Ribbon`（655 行）、`Menu`、`ContextMenu`、`Table`、`Tabs`（992 行）、`TreeView`（900 行）、
`ListView`（627 行）、`VirtualList`、`Wizard`（1227 行）、`DatePicker`、`Dropdown`、`StatusBar`、`ToolStrip`，
外加 `Gui/Component/DataTable/`（`DataGrid` + 列冻结/带头/筛选/汇总/派生列，`DataColumn` 有 title/field/width/cellType/align/sortable/filterable/resizable/summary/derive 等完整字段）。
设计器调色板（`FormField.KindForType`）也已经能放下这 61 种 kind。**控件层面不缺，也不需要照搬。**

## 真正的缺口：61 种可放置的 kind 里，18 种没有声明式属性契约

按生成的 `zform.json`（收录标准 = 类声明了 `override List<PropSpec> Props()`）对照调色板：

**有契约（43）**：Input、Button、Label、Panel、Card、SelectBox、Checkbox、Radio、Switch、Slider、TextArea、Tag、Badge、Avatar、Statistic、Progress、Steps、Breadcrumb、Pagination、PageHeader、TreeView、ToolStrip、StatusBar、SplitPanel、Collapse、Timeline、Trend、Led、Digital、Gauge、Bargraph、NumPad、DeviceCard、EquipPanel、WebViewBox、CefBrowserBox、FloatButton、Divider、Empty、Result、Skeleton、Spin、Carousel。

**没有契约（18）**：`Table`、`DataGrid`、`Tabs`、`Ribbon`、`Menu`、`ContextMenu`、`ListView`、`VirtualList`、`Wizard`、`Chart`、`Dropdown`、`DatePicker`、`Rate`、`Layer`、`Scrollbar`、`Ellipsis`、`AlarmBanner`、`AlarmList`。

这 18 个恰好就是**工具软件的主体**：表格、页签、Ribbon、菜单、树/列表、向导、图表、日期。
没有 `Props()` 的连锁后果是四重的：
1. 属性检查器里没有东西可编辑；
2. `.zform` 的 `props` 里没有合法键可写；
3. 生成的 `zform.json` 索引里查不到（AI 只能回去翻源码 —— 正是你最初要解决的问题）；
4. 画布上只是个盒子，**内容只能由代码构建**。

这就是为什么现项目 `.zform` 1276 行、`.zan` 2786 行，而其中 `GridColumns.zan` 206 行 + `menu/` 290 行 + `GridPane.zan` 99 行 ≈ 595 行（代码的 21%）全是「本可以声明」的列与菜单项。

## 第二个缺口：子项只能用扁平字符串数组

今天唯一的子项通道是 `options: ["..."]`（一维字符串）。证据：`MainForm.zform` 的文档页签靠
`"kind": "DocumentTabHost", "options": ["推广软件"]`，状态栏靠 `"options": ["就绪 · 假数据"]`。
而这套软件的子项是**带属性的对象**：列要 field/width/align/summary/可编辑，菜单项要图标/快捷键/分隔符/二级子项/enabled。
所以需要的是 `columns: [{...}]` / `items: [{...}]` 这种**对象数组**，`options` 撑不住。
（页签本身不缺结构：`Tabs` + `childTab` 已经能声明式分页，缺的是页标题/可关闭等页级属性。）

## 修正后的优先级

1. **P0 给那 18 个控件补 `Props()`/`Events()` 契约**（尤其 `DataGrid`/`Table`/`Tabs`/`Ribbon`/`Menu`/`ListView`/`DatePicker`）。这一步不改格式语法，纯粹是控件补声明，且立刻同时点亮：检查器、`props` 序列化、`zan_form_schema` 索引、AI 少读源码。
2. **P0 子项对象数组**：`columns` / `items`（含分隔符与二级子项）。配合第 1 条，`GridColumns.zan`/`menu/` 的大部分能进文档。`DataColumn` 的字段已经齐全，等于只是把它 JSON 化。
3. **P0 自定义组件带参数**（报告 D4）：6 个同构 `*ObjectHost` 塌成 1 个带参组件，3 个复制版工具条塌成 1 个。
4. **P0 统一三条建树路径 + 停止几何回写**（报告 D1/D2）：界面数量放大 10 倍时，这两条会从「偶发怪事」变成每天都坏。
5. **P1 事件带 sender**（报告 D5）：复用同一工具条/面板是这类软件的常态，`static current` 在多实例下已经是错的。
6. **P2 写出省略默认值**：现在 39% 的键是默认值。
7. **P3 声明式条件键**：仍然排最后。

## 一句话回答

**能承载，而且不需要照搬 —— 控件 Zan 都有了。**
缺的是「这 18 个重型控件的声明式属性契约」+「带属性的子项数组」这两块**数据**模型；
补完之后 `.zform` 才真的能描述这套软件的界面，而不是只描述一层外壳。
