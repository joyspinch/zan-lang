# Gui.Component.DataTable

> 源码: `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataGrid.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.CfClear.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.ColumnChooser.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Columns.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.CompCell.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Compute.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.DataSource.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Diagnostics.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Edit.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Export.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.ExportUi.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.ExportXlsx.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Filter.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.FilterBuilder.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.FilterDescribe.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.FilterUI.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Formula.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.HttpSource.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Identity.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.ImageSlot.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Lang.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Layout.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.LocalSource.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.MasterDetail.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Overlays.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.PivotView.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Query.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.QueryPlan.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.QueryRequest.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.QueryResult.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Realtime.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Render.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.RowCache.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Rows.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Schema.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Search.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Selection.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Server.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Sort.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Transaction.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.TransactionRequest.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Transpose.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Value.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.WidgetComp.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTableBootstrap.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTableModel.zan`


## BandGridComp (class)

"bandgrid" 适配器：把数值序列按行优先灌进 BandGrid 矩阵。
形状缺省 1×序列长度（单行条）；heatBand 经 HeatBands 映射为
底色梯度回调。点击/拖选随 BandGrid 自身交互，CompPos 读其
CellRow/CellCol。

- List<string> poolKeys;

- List<Control> poolCtl;

- List<int> poolShape;

- BandGridComp():base("bandgrid")

- override Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- override void Fill(Control ctl, List<int> vals, int rows, int cols, string heatBand)

- override bool CompPos(Control ctl, ref int r, ref int c)

- override void Reset()


## BandSpan (class)

多级表头中的一个跨列单元格：组自身的标签、所在的
表头行（0 = 最外层），以及它覆盖的可见顺序的
半开区间 [from, to)。`path` 是完整的斜杠分隔键，
折叠状态即以此为键存储——仅用标签会在不同父级下的
两个同名叶节点分组之间发生冲突。

- string label;

- string path;

- int level;

- int from;

- int to;

- BandSpan(string l, string p, int lv, int f, int tt)


## ButtonsComp (class)

"buttons" 适配器：ButtonGroup 分段模式（选项同样取列
`choices`）。点击段 Raise CellClick（CompPos 报段号）——动作
语义；值变化（当前段作为文本写回）时经 TakeCommit 提交。
数据源只读时 SetCell 是空操作，自然退化为纯动作列；
`Actions()` 声明的动作列则连状态都不落——不回写标签，
单元格文本保持原值，活动段视觉由 FillStr 每帧复位。

- List<string> poolKeys;

- List<Control> poolCtl;

- string lastLabel;

- bool lastAction;

- ButtonsComp():base("buttons")

- override Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- override void FillStr(Control ctl, string val, DataColumn col, DataSource src, int row)

- override bool TakeCommit(Control ctl, ref string outVal)
  - 段选择未变不上报（重灌当前值不会写数据）；点击动作语义
    不受影响——CellClick 经 CompPos 独立归一，仍每次触发。
    动作列（Actions()）从不提交：点击只报 CellClick。

- override bool CompPos(Control ctl, ref int r, ref int c)

- override void Reset()


## CardDetail (class)

键值卡片详情：把主行（或任意键值对）渲染为
label: value 两列卡片。子类持有数据，本类只管绘制
与尺寸（每对一行，clamp 到 maxSlots）。

- public List<string> labels;
  - 标签/值两列的文本（子表式按行填充）。

- public List<string> values;

- public int maxSlots;

- CardDetail()
  - 内部构造；使用 Of() 创建。

- static CardDetail Of()
  - 创建空卡片，随后用 Add() 链式填充。

- CardDetail Add(string label, string value)
  - 装配一对标签/值；返回 this 便于链式填充。

- override int Slots(DataSource src, int row)
  - 详情带行槽数 = 键值对数，夹取到 [1, maxSlots]。

- override void Paint(App app, DataSource src, int row, int x, int y, int w, int h)
  - 逐行绘制 label: value 两列；超出详情带高度的行不再绘制。


## CellComp (class)

组件列适配器：把 cellType 9 的单元格值（逗号分隔数值序列，
与迷你图同一序列化约定）灌进一个池化的真控件，供渲染循环
Measure/Arrange/RenderTree 三步渲染。这是框架作者级扩展点——
使用层经 `DataGrid<T>.CompCol` 声明 `compId`，从不接触本接口。

注册表在 DataTable.CompCell 静态块完成内置装配（"bandgrid"）；
新组件实现本抽象类并 Register 一次，即对全部网格声明式可用。

池化责任在本类：以 `GetRowKey(row)` + 列为键缓存控件实例，
虚拟滚动下同一显示槽位会滚过任意多行，复用时由 Fill 只刷
数据、不重建控件——焦点不丢、状态不闪。

- public string id;

- CellComp(string id)

- virtual Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)
  - 创建（或从池中取出）该格的控件实例。`rows`/`cols` 来自
    列声明的矩阵形状（<=0 时实现自行决定默认形状）。
    实现必须缓存：同一 (key, col) 每帧拿到同一个实例。

- virtual void Fill(Control ctl, List<int> vals, int rows, int cols, string heatBand)
  - 把单元格值灌进控件。`vals` 是解析后的数值序列（可能
    为空）；`rows`/`cols` 是目标矩阵形状。实现按需 SetNum、
    多余槽位清零、缺失槽位保持旧值，不得重新 Bind 清掉
    用户选区。

- virtual void FillStr(Control ctl, string val, DataColumn col, DataSource src, int row)
  - 组件列字符串值形态（cellType 10 WidgetCol）：把原始
    单元格文本灌进控件（开关的布尔、下拉的选中项等），
    `col` 提供列级配置（choices、boolTrue/boolFalse、形状），
    `src`/`row` 供适配器需要行上下文时使用。默认空实现，
    旧数值适配器（bandgrid）零改动。

- virtual bool TakeCommit(Control ctl, ref string outVal)
  - 渲染后轮询（两个形态共用）：控件本帧发生了会写回数据的
    变化时返回 true 并把新值放 `outVal`（引擎按
    Journal(RowEdit.Cell) → src.SetCell → CellEdit 提交）。
    返回 false 表示无变化。值比较由引擎做，适配器只上报。

- virtual bool CompPos(Control ctl, ref int r, ref int c)
  - 最近一次 Fill 中命中的矩阵内位置（用于把组件内点击
    归一为 CellClick 时提供格内 (r,c)；未命中返回 false）。

- virtual void Reset()
  - 网格重建时清空池（随 CellWidgetProvider.Reset 的语义）。

- static List<CellComp> registry;

- static readonly int PoolCap=2048;
  - 单个适配器池的深度上限。池键含业务键，键集无界的数据源
    （时间戳主键、实时流逐行换键）会让池随历史行无限增长——
    超过上限整池重置：宁可损失复用，不失去内存上界。

- static void Register(CellComp comp)
  - 按 id 注册适配器（重复注册覆盖，便于测试）。首次调用
    装配内置适配器。

- static CellComp Find(string id)


## CellStyler (class)

DataTable 的外观 + 自定义绘制钩子（DevExpress 风格
RowStyle / CustomDrawCell）。

Zan 的委托不能捕获局部变量或绑定 `this`，因此逐单元格回调
不能是视图模型上的 lambda——而是*子类*，
就像 `DataSource` 一样。赋一个实例给 <c>st.styler</c>；
每个方法都可选，默认保持网格的默认外观。

颜色为打包的 ARGB。**0 表示“不覆盖”**——它是完全
透明的黑色，永远不会与任何人想绘制的颜色冲突。

```zan
class OverdueStyle : CellStyler {
override int RowBg(DataSource src, int row) {
if (src.CellText(row, 4) == "Overdue") { return DataTable.Rgb(255, 236, 236); }
return 0;
}
}
st.styler = new OverdueStyle();
```

- virtual int RowBg(DataSource src, int row)
  - 整行数据行的背景，0 则保留网格的斑马纹。
    选中和悬停依然可见：它们与此颜色混合
    而非替换它，行在激活时仍可辨认。

- virtual int RowFg(DataSource src, int row)
  - 整行数据行的文本颜色，0 用主题默认色。

- virtual int CellBg(DataSource src, int row, int col)
  - 单个单元格的背景，0 则继承行背景。

- virtual int CellFg(DataSource src, int row, int col)
  - 单个单元格的文本颜色，0 则继承 `RowFg`。

- virtual bool PaintCell(App app, DataSource src, int row, int col, int x, int y, int w, int h)
  - 自行绘制单元格。绘制了则返回 true——内置
    渲染器将完全跳过该单元格（条件格式、
    标签、进度条等全部跳过）。画布裁剪到 [x,y,w,h]，
    自定义绘制器不会溢出到相邻单元格。


## CellWidgetProvider (class)

单元格控件插槽：为网格列提供*真控件*而非自绘
（DevExpress 的 CustomRowCellEdit + RepositoryItem 语义）。

继承并重写 `Provide`，赋给 <c>st.cellWidgets</c>。
渲染循环每格调用 Provide 取控件，把控件按格矩形
停靠、渲染——控件拥有完整交互（点击、编辑、悬停），
事件经控件自身的处理器派发，与独立放置无异。

控件实例必须由 provider *池化复用*：每帧每格调用
Provide，且 row 无固定身份（虚拟滚动下同一显示槽位
会滚过任意多行），因此 provider 以 RowKey（稳定行
身份，`src.GetRowKey(row)`）为键缓存控件实例，
同一行在帧间拿回同一个控件——文本输入不丢焦点、
复选框不闪、滚动往返不重置。列数与列身份每帧
一致（由渲染循环保证）；返回 null 表示该格
退回内置渲染。

```zan
class OpCellHost : CellWidgetProvider {
List<Control> pool = new List<Control>();
override Control Provide(App app, DataSource src, int row, int col, int x, int y, int w, int h) {
RowKey k = src.GetRowKey(row);
Control ctl = this.Find(k, col);
if (ctl == null) {
Button b = new Button("Edit");
b.Class = "small text";
b.Click.Add(() => { ... });
ctl = b;
this.Remember(k, col, ctl);
}
return ctl;
}
}
st.cellWidgets = new OpCellHost();
```

- virtual Control Provide(App app, DataSource src, int row, int col, int x, int y, int w, int h)
  - 为 (row, col) 提供控件。可返回 null——该格退回
    内置渲染路径（cellType 分派）。控件随后被渲染循环
    摆放到 [x,y,w,h] 内（列头之下的格矩形），池化由
    实现负责（见类注释）。

- virtual void Reset()
  - （可选）框架在销毁/重建网格状态时通知 provider
    清空池，控件随之可被 GC。


## ChartBar (class)

选区图表浮层中的一根条：其类别标签和值。
取代旧的按索引对齐的 chartLabels/chartVals 并行列表。

- string label;

- int val;

- ChartBar(string l, int v)


## CheckboxComp (class)

"checkbox" 适配器：布尔值（同 switch 的序列化口径），格内
一个无标题复选框。

- List<string> poolKeys;

- List<Control> poolCtl;

- DataColumn lastCol;

- bool lastOn;

- CheckboxComp():base("checkbox")

- override Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- override void FillStr(Control ctl, string val, DataColumn col, DataSource src, int row)

- override bool TakeCommit(Control ctl, ref string outVal)
  - 与 SwitchComp 同契约：仅本帧用户点击才上报，列口径文本。

- override void Reset()


## ChildGridDetail (class)

详情带内渲染完整子表格的 DetailProvider。

```zan
class OrderItems : ChildGridDetail {
// 列 schema 与子行集在子类字段里持有
}
```

- public List<DataColumn> childCols;
  - 子表列 schema（决定表头与列宽）。

- public DataSource childSrc;
  - 子表数据源。

- public int childKeyCol;
  - 子表中指向主行键的列。

- public int masterKeyCol;
  - 主表中键列（默认 0）。

- public int maxSlots;
  - 详情带最多占用的行槽数（0 = 8）。

- public List<int> rows=new List<int>();
  - 子行读取钩子：默认从 `childSrc` 文本相等匹配取。
    返回 false 表示该主行无子行（拒绝展开）。
    Zan 委托不能捕获局部变量，复杂子集请子类化重写
    `Rows`/`CanExpand`，本字段保持简单场景。

- ChildGridDetail(List<DataColumn> cols, DataSource src, int keyCol)
  - 构造子表格详情：masterKeyCol 默认 0，maxSlots 默认 0（=8）。

- static ChildGridDetail Of(List<DataColumn> cols, DataSource src, int keyCol)
  - 创建子表格详情。

- public int ChildCount(DataSource src, int row)
  - 该主行的子行数（按文本相等匹配）。

- override bool CanExpand(DataSource src, int row)
  - 主行是否有可显示的子行（槽位数 > 1）才允许展开。

- override int Slots(DataSource src, int row)
  - 详情带行槽数 = 表头 1 + 子行数，夹取到 [1, maxSlots]。

- override void Paint(App app, DataSource src, int row, int x, int y, int w, int h)
  - 在详情带内绘制子表格：内缩留边后画表头行与最多
    maxSlots-1 行斑马纹数据；子行为空时画 "(no records)" 占位。


## ColFilter (class)

逐列过滤器，由三个协作阶段组成（均可选）：
1. 集合筛选 \u2014 剔除 `hidden` 中存在的值（Values 标签页）；
2. 文本筛选 \u2014 `textMode`（1 包含 / 2 等于 / 3 开头 / 4 结尾）
对 `textQuery` 进行匹配（Text Filters 标签页）；
3. 批量筛选 \u2014 匹配 `batchTerms` 中的每一项（按 `batchExact`
精确或子串匹配）；`batchKeep` 保留匹配项，否则排除。

- List<string> hidden;

- int textMode;

- string textQuery;

- List<string> batchTerms;

- bool batchExact;

- bool batchKeep;

- int cmpMode;

- string cmpA;

- string cmpB;

- int datePreset;

- int topMode;

- int topN;

- ColFilter()

- bool IsHidden(string v)

- void Toggle(string v)

- bool Active()

- bool NeedsSetPass()
  - 此过滤器是否需要在决定前取得整个过滤集合
    （Top-N 和高于/低于平均值），而不是逐行判断。

- void Reset()
  - 移除所有阶段，恢复列为不过滤状态。


## DataColumn (class)

DataTable 的列描述符。
cellType：0 文本，1 链接，2 标签，3 进度，4 徽章，5 图片，
6 图标，7 按钮，8 图表，9 组件
align：0 左对齐，1 居中，2 右对齐

- string title;

- string field;
  - 该列的稳定名称，与位置及其
    （可翻译的）标题无关。用 `DataColumn.Field(c, "name")` 设置，
    传给 `DataTable.Col(cols, "name")` 获取索引，
    使应用代码和保存的布局在列被插入、移动或重命名后依然有效。
    空表示“未命名”：此类列只能按索引访问。

- int width;

- int cellType;

- int align;

- bool sortable;

- bool filterable;

- bool resizable;

- int tagType;

- bool numeric;

- int summary;

- string summaryFmt;

- int deriveOp;

- int deriveA;

- int deriveB;

- int cfMode;

- int cfColor;

- int cfColor2;

- int cfThreshold;

- bool cfRankPct;

- int fmt;

- bool grouped;

- string prefix;

- string suffix;

- int decimals;

- string formula;

- bool isDate;

- int dateOrder;

- int dateFmt;

- int dateGroup;

- bool isBool;

- int boolStyle;

- string boolTrue;

- string boolFalse;

- bool editable;

- string btnIcon;

- string btnClass;

- bool sparkArea;

- int sparkKind;

- int sparkColor;

- int sparkNegColor;

- string compId;

- bool actionOnly;

- bool vTop;

- int shapeR;

- int shapeC;

- string heatBand;

- int editorKind;

- List<string> choices;

- bool choicesStrict;

- double editStep;

- bool required;

- int maxLength;

- bool hasMin;

- bool hasMax;

- double editMin;

- double editMax;

- int freeze;

- string band;

- string description;

- bool mergeSame;

- bool wrapText;

- int wrapLines;

- DataColumn(string title, int width)

- static DataColumn Field(DataColumn c, string name)
  - 为列命名，使其可通过 `DataTable.Col(cols, name)` 访问
    而非字面索引。与其他构建器一样可组合：
    
    ```zan
    cols.Add(DataColumn.Field(DataColumn.Text("Name", 140), "name"));
    int ci = DataTable.Col(cols, "name");
    ```

- string FieldName()
  - 列的稳定字段名（`Field` 设的名字；未命名列返回 ""）。
    命中信息按名消费用（DataGrid.HitField → 列级事件按名分派）。

- static DataColumn Describe(DataColumn c, string text)
  - 设置表头悬停提示的描述文本。与其他构建器一样可组合：
    
    ```zan
    cols.Add(DataColumn.Describe(
    DataColumn.Numeric("Score", 120), "0-100 分，越高越好"));
    ```
    指针在列标题上停留约半秒后弹出两行提示（列名 + 此文本）。

- static DataColumn Editable(DataColumn c)
  - 让列启用内联编辑（双击/第二次单击单元格，或按
    F2/Enter）。数值列在提交时校验输入。

- static DataColumn Dropdown(DataColumn c, List<string> items, bool strict)
  - 通过下拉列表编辑该列。键入会过滤列表，
    默认只接受列表中的值提交；传 `strict = false` 可允许
    在建议之外输入自由文本。

- static DataColumn DatePicker(DataColumn c)
  - 通过月历编辑该列。隐含 `AsDate`，
    所选日期经过该列自身的解析/格式化配对往返。

- static DataColumn Spinner(DataColumn c, double step)
  - 通过数字微调器编辑该列：上/下方向键和 +/- 按钮
    按 `step` 步进，值始终保持为数字。

- static DataColumn Required(DataColumn c)
  - 拒绝空值。

- static DataColumn MaxLength(DataColumn c, int n)
  - 拒绝超过 `n` 个字符的输入（0 清除限制）。

- static DataColumn Range(DataColumn c, double lo, double hi)
  - 提交时将数值列限制在 [lo, hi] 内。

- static DataColumn MinValue(DataColumn c, double lo)
  - 提交时将数值列限制为 >= lo。

- static DataColumn MaxValue(DataColumn c, double hi)
  - 提交时将数值列限制为 <= hi。

- static DataColumn Pin(DataColumn c)
  - 将列冻结在左边缘：它被提升到滚动列之前，
    并在它们于其下方平移时保持不动。

- static DataColumn PinRight(DataColumn c)
  - 将列冻结在右边缘，适合放累计总和或行的
    操作按钮：无论中间区域滚多远，它始终可及，
    这正是把它们放在那里的原因。

- static DataColumn Band(DataColumn c, string path)
  - 将列放入表头分组，表头相应增加一行：
    
    ```zan
    cols.Add(DataColumn.Band(DataColumn.Text("Street", 160), "Address"));
    cols.Add(DataColumn.Band(DataColumn.Text("City", 120), "Address"));
    ```
    
    路径相同的相邻列共享一个跨列单元格。
    用斜杠嵌套，最外层在前——“Contact/Address”位于一个
    同样横跨其他名为 Contact 的列的单元格下。传“”取消分组。

- static DataColumn Decimals(DataColumn c, int n)
  - 数值列的小数位数：将排序、聚合
    和条件格式切换到实数（double）路径，并精确渲染到
    `n` 位小数。`Decimals(c, 0)` 恢复整数路径。

- static DataColumn Real(string title, int width, int decimals)
  - 小数数值列，例如 `DataColumn.Real("Rate", 90, 2)`。

- static DataColumn Formula(string title, int width, string expr)
  - 计算列：`expr` 是对每行求值的公式（语法与语义见
    DataTable.Formula.zan）。列自动走 `decimals` 位小数的
    实数路径（默认 2 位：排序/聚合/条件格式全部一致），
    并保持只读——公式列是派生输出，内联编辑与粘贴
    对它无效。典型用法：
    
    ```zan
    cols.Add(DataColumn.Formula("Total", 100, "qty * price"));
    cols.Add(DataColumn.Formula("Share", 90, "a * 100 / (a + b)"));
    ```

- static DataColumn Formula(DataColumn c, string expr)
  - 在既有列上启用公式（`expr` 传 "" 清除）。传入了
    decimals 的列保留其位数；否则默认 2 位。列引用
    闭环（公式互相引用）不在构建期检测——求值层按
    单遍顺序读取各列的底层值，公式列引用公式列时
    读到的是后者的源数据，而非其计算结果。

- static DataColumn AsDate(DataColumn c, int order)
  - 将列视为日期：单元格解析为天数，按时间而非
    字典序排序和分组。`order` 指定存储文本的字段
    顺序（0 y-m-d，1 d-m-y，2 m-d-y）；开头的 4 位
    字段无论何种顺序都读作年份。

- static DataColumn Date(string title, int width)
  - ISO 风格日期列，例如 `DataColumn.Date("Ordered", 110)`。

- static DataColumn DateFormat(DataColumn c, int fmt)
  - 渲染日期格式：0 YYYY-MM-DD，1 DD/MM/YYYY，2 MM/DD/YYYY，
    3 YYYY年M月D日, 4 YYYY-MM, 5 YYYY, 6 "1 Jan 2026".

- static DataColumn DateGroup(DataColumn c, int mode)
  - 该列分组时对日期分桶：0 精确到日，1 年，2 月，
    3 季度，4 星期。

- static DataColumn AsBool(DataColumn c, int style)
  - 将列视为真值：单元格用 ParseBool 读取（“1”、
    “true”、“yes”、“y”、“on”、“\u2713”及其否定形式，不区分大小写），
    按 false 在前 true 在后排序，并按 `style` 居中绘制
    （0 复选框，1 对勾/叉字形，2 文本）。

- static DataColumn Bool(string title, int width)
  - 复选框列，例如 `DataColumn.Bool("Active", 70)`。
    结合 `Editable` 可让单击切换单元格。

- static DataColumn BoolText(DataColumn c, string yes, string no)
  - bool 单元格切换时写回的文本，样式 2 也用它显示。
    默认为“true”/“false”；可用例如 `BoolText(c, "Yes", "No")`。

- static DataColumn Money(DataColumn c, string symbol)
  - 货币：千位分组数字加前置符号，右对齐。
    默认 2 位小数；之后用 `Decimals` 覆盖。

- static DataColumn Percent(DataColumn c)
  - 百分比：追加“%”，右对齐。

- static DataColumn Grouped(DataColumn c)
  - 仅千位分隔符（例如 1234567 -> 1,234,567）。

- static DataColumn Affix(DataColumn c, string prefix, string suffix)
  - 任意前缀/后缀（可任一为空），例如“ kg”这样的单位。

- static DataColumn DataBar(DataColumn c, int color)

- static DataColumn ColorScale(DataColumn c, int lowColor, int highColor)

- static DataColumn HighlightGreater(DataColumn c, int threshold, int color)

- static DataColumn HighlightLess(DataColumn c, int threshold, int color)

- static DataColumn TopRules(DataColumn c, int countOrPct, int color)
  - 前 N 项（`countOrPct > 0`）或前 N%（< 0，N 取绝对值）。

- static DataColumn BottomRules(DataColumn c, int countOrPct, int color)
  - 后 N 项 / 后 N%，语义同上。

- static DataColumn NotResizable(DataColumn c)
  - 将列标记为固定宽度：用户不能拖动其右边框，
    指针在（本应出现的）调整大小手柄上显示禁止光标。

- static DataColumn Merge(DataColumn c)
  - 同值相邻纵向合并（VTable data merge 的同类项）：该列
    文本相同且相邻的数据行（中间不隔组表头/详情带）画成
    一个跨行合并格。仅是渲染效果——排序/筛选/导出仍逐行
    看到各自的值；编辑仍写回所在行的底层单元格，改值后
    合并随之下一次重绘自然拆开。
    
    ```zan
    cols.Add(DataColumn.Merge(DataColumn.Text("Region", 100)));
    ```

- static DataColumn Wrap(DataColumn c, int maxLines)
  - 文本换行：该列（cellType 0）的单元格文本按列宽折行，
    最多 `maxLines` 行；行高按全部换行列的最大需要行数
    统一增大（网格仍是统一行高，虚拟化寻址不变）。
    `maxLines <= 0` 取 2。长文本超出部分仍按截断加省略号。

- static DataColumn Text(string title, int width)

- static DataColumn Numeric(string title, int width)

- static DataColumn Link(string title, int width)

- static DataColumn TagCol(string title, int width, int tagType)

- static DataColumn ProgressCol(string title, int width)

- static DataColumn BadgeCol(string title, int width)

- static DataColumn ImageCol(string title, int width)
  - 图片列：单元格值是文件路径、`mem:` 已注册 key 或 http(s)
    URL（URL 由 ImageHttp 后台取回，就绪后自动重绘；加载中与
    失败画淡色占位块）。图片在格内 contain 缩放居中，不参与
    条件格式、汇总与内联编辑。

- static DataColumn IconCol(string title, int width)
  - 图标列：单元格值即 tabler 图标名（"check"、"trash"、"pencil"），
    以 datatable::icon 的前景色居中绘制；空值画空。
    不可排序/筛选——图标名不是可比较的数据。

- static DataColumn ButtonCol(string title, int width, string icon, string cls)
  - 按钮列：整格居中一个 small 按钮。单元格值是按钮标签；
    标签为空时用 `icon`（tabler 图标名）画图标按钮。
    点击按钮把 hitRow/hitCol 指向本格并 Raise CellClick，
    处理器与普通单元格点击走同一入口读取上下文。
    `cls` 是按钮 class（"primary"、"danger outline"、"text"…），
    空串用默认外观。不可排序/筛选/编辑。

- static DataColumn SparklineCol(string title, int width, bool area)
  - 图表列：单元格值是以逗号分隔的数值序列
    （"12,18,9,24,30"），ParseReal 宽松解析（"¥1,234" 也能读），
    格内画等槽位迷你图。`kind`：0 柱状（默认，柱高以正峰
    为比例），1 折线（抗锯齿折线 + 末点强调），2 面积
    （折线 + 基线填充）。柱状形态下 `area` 为 true 时柱下
    叠加半透明面积（历史参数）。含负值的序列以零基线分区，
    正值用 `posColor`、负值用 `negColor`（0 = 主题自动：
    accent / spark-neg）。不可排序/筛选——序列文本不构成
    可比较的键。

- static DataColumn SparkKind(DataColumn c, int kind)
  - 图表列形态：0 柱状，1 折线，2 面积（越界回柱状）。

- static DataColumn SparkColors(DataColumn c, int posColor, int negColor)
  - 图表列配色：正值/线条色与负值色（0 = 主题自动）。

- static DataColumn CompCol(string title, int width, string compId)
  - 组件列：单元格值（逗号分隔数值序列，同迷你图约定）被
    灌进 `compId` 标识的池化真控件（如 "bandgrid"），控件按
    格矩形渲染。`shapeR`/`shapeC` 声明矩阵形状（<=0 用组件
    默认），`heat` 是数值→色带名（"" 关闭；"teal"/"amber"/
    "coral"/"blue"…）。点击矩阵格子把 hitRow/hitCol 指向本格、
    CompPos() 提供格内 (r,c) 后 Raise CellClick。
    不可排序/筛选——序列文本不构成可比较的键。
    序列化边界在引擎内部：使用层经 `DataGrid<T>.CompCol` 的
    类型化访问器提供 `List<int>`，从不接触控件与绘制。

- static DataColumn Shape(DataColumn c, int rows, int cols)
  - 组件列的矩阵形状（行 × 格）。仅对 cellType 9 有意义。

- static DataColumn WidgetCol(string title, int width, string compId)
  - 组件列（字符串值形态，cellType 10）：单元格文本灌进
    `compId` 标识的池化真控件。内置 compId："switch"/"checkbox"
    （布尔文本）、"rate"（0..N 星数，Shape 的 shapeC 为显数）、
    "select"/"buttons"（选项取 `choices`，经 Options 声明）。
    用户控件实现 CellComp（FillStr/TakeCommit）Register 一次
    即可用自己的 id。控件改动经 TakeCommit 走编辑通道写回
    （撤销/SetCell/CellEdit），排序筛选关闭，导出用原始文本。

- static DataColumn Options(DataColumn c, string csv)
  - 组件列（cellType 10 select/buttons）或下拉编辑器的选项，
    逗号分隔一次给全（如 "草稿,审核中,已发布"）。

- static DataColumn Actions(DataColumn c)
  - 动作按钮组列（cellType 10 "buttons"）：点击是命令
    （CellClick，段号在 HitCol/CompPosC），不是数据选择——
    适配器不回写段标签（单元格文本保持原值），焦点描边
    与填充柄不停留在这种列上（DataTable.IsActionCol）。
    与 Options 组合声明段标签。

- static DataColumn VTop(DataColumn c)
  - 组件列改顶对齐。默认格内垂直居中（行高大于控件自然高度时
    上下均分留白）；矩阵形态或需要贴住表头的场合才需要这个。

- static DataColumn Heat(DataColumn c, string band)
  - 组件列的数值→色带映射（"teal"/"amber"/"coral"/"blue"/
    "green"/"purple"，空串关闭）。仅对 cellType 9 有意义。

- static bool HeatBands(string name, ref int lo, ref int hi)
  - 命名色带的两端颜色（0xAARRGGBB）。`name` 不识别时返回
    false，调用方回退 accent 单色。与文档第 1 节色板一致的
    内置映射，组件列着色共用。浅端按亮色（默认）皮肤调的
    参考值：组件列实际把深端交给 BandGrid.Heat，由其绘制
    时从当前皮肤表面色派生浅端，暗色皮肤自动得暗色调。

- static DataColumn Derived(string title, int width, int op, int a, int b)
  - 汇总由其他列聚合值派生的数值列
    （不逐行扫描）。`op` 为 0 百分比 / 1 比值 / 2 差值 / 3 和 / 4 积 /
    5 千分比（A*1000/B）；`a` 和 `b` 是源列索引。

- static void Derive(DataColumn c, int op, int a, int b)
  - 在既有列上设置派生汇总（老 DevExpress CustomSummaryCalculate
    等位：页脚由其它 sum 列的聚合值现算）。op 同 Derived。

- static DataColumn Sum(DataColumn c)

- static DataColumn Avg(DataColumn c)

- static DataColumn Min(DataColumn c)

- static DataColumn Max(DataColumn c)

- static DataColumn Count(DataColumn c)

- static DataColumn CountSel(DataColumn c)
  - 勾选行数聚合（DevExpress Selection 汇总等位）。

- static DataColumn SummaryFmt(DataColumn c, string fmt)
  - 页脚聚合文案模板，{0} 替换为聚合值，{0:0.##} 等按数字
    格式说明渲染（老 DisplayFormat；详见 ApplySummaryTemplate）。


## DataGrid (class)

绑定到调用方自身实体列表的数据网格，形式与
`ListView<T>` / `Dropdown<T>` 一致——用访问器声明列，传入数据，
布局、存储或单元格字符串等细节不会泄漏到
调用处：

```zan
DataGrid<User> grid = new DataGrid<User>();
grid.Col("Name", 140, u => u.name);
grid.Col("Email", 200, u => u.email);
grid.NumCol("Age", 80, u => u.age).Right();
grid.Bind(users);          // 调用方的 List<User>，每帧读取
side.Add(grid);
```

底层驱动完整的 DataTable 引擎（可排序 / 可筛选 /
可分组 / 可编辑 / 虚拟化），泛型前端不损失任何
能力——可通过 `DataGrid.State()` 访问原始
`DataTableState`。

- List <GridColumn<T>> gcols;

- List<T> data;

- DataTableState st;

- GridSource<T> source;

- List<DataColumn> descs;
  - 每帧交给引擎的 `List<DataColumn>`，与
    `gcols` 保持同步，使构造后新增的列也能显示。

- int boundN;
  - 上一次 Bind 时的行数（-1 = 还没绑过）：行数变了必须让引擎重算行号集合。

- void InitGrid()

- DataGrid()

- override string Kind()

- override string StyleType()

- DataGrid<T> Bind(List<T> src)
  - 绑定调用方的列表。不复制——网格每帧读取，
    修改列表会在下一次绘制时生效。

- DataGrid<T> KeyBy(GridKey<T> provider)
  - 绑定实体的稳定键。key 参与选择、详情、排序平局和后续远程事务；
    未设置时仍保留旧的临时位置键行为。

- DataGrid<T> Refresh()
  - 数据对象字段被原地修改、但列表引用和行数不变时调用，
    使排序、筛选、汇总和条件格式重新读取实体。

- int Count()

- DataTableState State()
  - 实时的 `DataTableState`，用于链式方法未覆盖的
    高级配置（自定义 `CellStyler`、主从详情 provider、绑定），
    以及在处理器中读取交互上下文（HitRow / HitCol / …）。

- GridColumn<T> Add(GridColumn<T> col)

- GridColumn<T> Col(string title, int width, GridText<T> read)
  - 纯文本列。

- GridColumn<T> NumCol(string title, int width, GridInt<T> read)
  - 整数列：右对齐，按值排序与合计。

- GridColumn<T> CompCol(string title, int width, string compId, GridNums<T> read)
  - 组件列：`read` 从实体取数值序列（如 `u => u.weekPlan`），
    `compId` 指定内置组件适配器（"bandgrid"）。值经序列化进
    引擎文本路径（导出/排序口径不变），渲染层灌进池化真控件。
    链式 `.Shape(r,c)` 声明矩阵形状、`.Heat("teal")` 声明色带。

- GridColumn<T> WidgetCol(string title, int width, string compId, GridText<T> read)
  - 组件列（字符串值形态）：`read` 从实体取原始单元格文本
    （"true"/"1"/"3"/"已发布"……），`compId` 指定组件适配器
    （"switch"/"checkbox"/"rate"/"select"/"buttons"，或自注册的
    CellComp）。控件改动走编辑通道写回，需要可回写访问器。
    链式 `.Options("A,B,C")` 给 select/buttons 选项、
    `.Shape(0,N)` 给 rate 显数。

- GridColumn<T> RealCol(string title, int width, int decimals, GridReal<T> read)
  - 保留 `decimals` 位小数的数值列。

- GridColumn<T> BoolCol(string title, int width, GridBool<T> read)
  - 复选框 / 布尔列。

- GridColumn<T> DateCol(string title, int width, int order, GridText<T> read)
  - 日期列；`order` 指定存储文本的字段顺序
    （0 y-m-d，1 d-m-y，2 m-d-y）。

- GridColumn<T> ImageCol(string title, int width, GridText<T> read)
  - 图片列：读取器返回文件路径 / `mem:` key / http(s) URL，
    单元格内 contain 缩放居中；URL 由 ImageHttp 后台取回。

- GridColumn<T> IconCol(string title, int width, GridText<T> read)
  - 图标列：读取器返回 tabler 图标名，居中绘制。

- GridColumn<T> ButtonCol(string title, int width, string icon, string cls, GridText<T> read)
  - 按钮列：读取器返回按钮标签（空串时用 `icon` 画图标
    按钮）；点击触发 OnCellClick，处理器从 State() 读
    HitRow()/HitCol() 得到本格位置。

- GridColumn<T> SparklineCol(string title, int width, bool area, GridText<T> read)
  - 迷你图列：读取器返回逗号分隔数值序列（"12,18,9,24"），
    格内画 accent 色迷你柱状图；`area` 叠加半透明面积。

- DataGrid<T> RowNumbers(bool on)

- DataGrid<T> Selectable(bool on)

- DataGrid<T> Striped(bool on)

- DataGrid<T> RowHeight(int px)
  - 行高（逻辑像素）；0 = 沿用主题高度。用于贴近
    WinForms/DevExpress 那种高信息密度的紧凑表格。

- DataGrid<T> RowReorder(bool on)
  - 允许用行号右侧的手柄拖动重排行（默认关闭）。

- DataGrid<T> Summary(bool on)

- DataGrid<T> FilterRow(bool on)

- DataGrid<T> StatusBar(bool on)

- DataGrid<T> GroupPanel(bool on)

- DataGrid<T> ExternalRowMenu(bool on)

- DataGrid<T> AllowAddRow(bool on)
  - 在末行下方显示"点击添加"占位符，并启用
    右键菜单的插入/删除命令。命令仅在安装行
    工厂后生效——见 RowFactory。

- DataGrid<T> HideCol(string field, bool hidden)
  - 按字段名显示/隐藏列（列的 `Field`；声明式列由生成器自动
    写入 "field"）。多个业务共用一份列并集、装配时按业务裁掉
    不适用的列就靠它。未知字段：空操作。

- DataGrid<T> ColumnChooser(bool open)
  - 打开/关闭列选择器弹层（勾选列表控制各列显隐、
    搜索框过滤、行内按钮重排）。常与 GroupPanel(true)
    搭配使用——面板右缘有常驻 "Columns" 按钮；
    也可以在任意事件处理器里调本方法弹出。

- bool IsColumnChooserOpen()
  - 列选择器弹层当前是否打开。

- DataGrid<T> RowFactory(GridNew<T> f)
  - 为数据源提供构建空白实体的工厂，以启用行插入/删除：
    `grid.RowFactory(() => new User());`
    没有工厂时泛型前端无法构造 `T`，网格将
    无论 AllowAddRow 如何设置都保持固定行数。

- DataGrid<T> OnRowClick(Action a)

- DataGrid<T> OnRowContext(Action a)

- DataGrid<T> OnRowDoubleClick(Action a)

- DataGrid<T> OnCellClick(Action a)

- DataGrid<T> OnCellEdit(Action a)

- DataGrid<T> OnSelectionChanged(Action a)

- DataGrid<T> OnSort(Action a)

- DataGrid<T> OnFilterChanged(Action a)

- void SyncDescs()
  - 根据当前列刷新引擎的描述符列表。描述符
    按引用共享，配置器的原地修改
    立即可见；此处仅在新增列时重新同步列表长度，
    以匹配列数。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override List<PropSpec> Props()
  - 声明式契约：外观开关全部落在引擎 state 上，没有对应的
    控件字段，因此按 GetExtra/SetExtra 挂钩发布（规格不绑定
    字段，读写经由下面两个覆盖走到 st）。

- void SetPreviewColumns(string spec)
  - 运行期/设计器预览的列头:`"宽,标题|宽,标题"`。工厂只能造
    `DataGrid<string>`,拿不到实体类型,所以预览只铺列头不铺行;
    真正的类型化列由编译期 GenForm 展开成泛型列 API。

- string PreviewColumnsText()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- string HitField()
  - 最近一次命中的数据字段名（CellClick/ColumnResize 等事件
    的 HitCol 翻译成稳定 field 名；越界/未命名列返回 ""）。
    GenForm 的声明式列级事件（columns[i].click）按它分派，
    列重排不影响路由。

- override void BindEvent(string evt, Action a)
  - 把网格的语义事件转发到引擎 state 自身的 UiEvent
    字段（与链式 `OnRowClick`/… 订阅的队列相同），
    使 JSON/设计器的 `onRowClick` 处理器生效；其余事件回退
    到 `On` 上的通用事件集合。


## DataPageCache (class)

按字节预算的确定性页缓存。实现使用小型数组，避免把线性 HashSet
当作高速索引；容量通常只包含当前视口附近的几十页，命中时更新 LRU。

- int byteBudget;

- int bytes;

- int clock;

- List<DataPageCacheEntry> entries;

- DataPageCache(int budget)
  - 以字节预算构造缓存；预算钳制到 >=1。

- int ByteBudget()
  - 缓存字节预算。

- int Bytes()
  - 当前占用字节数。

- int Count()
  - 缓存的页数。

- int Find(DataPageKey key)
  - 按页键查找条目下标；未命中返回 -1。

- QueryResult Get(DataPageKey key)
  - 命中时返回页结果并刷新 LRU 时钟；未命中返回 null。

- QueryResult GetRow(QueryDescriptor query, int generation, int schema, int server, int row)
  - 按查询身份（dataset/text/hash + generation/schema/server）查找
    覆盖 row 的页并刷新 LRU；未命中返回 null。

- void SetPinned(DataPageKey key, bool value)
  - 设置页的固定标志；页不存在时为空操作。

- int OldestEvictableExcept(int excluded)
  - 除 excluded 外最久未使用且未固定的条目下标；没有返回 -1。

- int EvictableBytesExcept(int excluded)
  - 除 excluded 外可淘汰（未固定）条目的字节总量。

- int OldestEvictable()
  - 全缓存中最久未使用且未固定的条目下标；没有返回 -1。

- bool EvictOne()
  - 淘汰一个最久未使用的未固定页；无可淘汰返回 false。

- bool Put(DataPageKey key, QueryResult result, int byteSize)
  - 插入/替换一页。单页大于预算时不缓存，但仍允许当前查询使用。

- void Clear()
  - 清空缓存并归零字节数。


## DataPageCacheEntry (class)

页缓存条目：页键、结果与估算字节占用。pinned 的页不参与
淘汰；lastUse 记录最近使用的时钟值，供 LRU 比较。

- DataPageKey key;

- QueryResult result;

- int bytes;

- bool pinned;

- int lastUse;

- DataPageCacheEntry(DataPageKey pageKey, QueryResult page, int byteSize, int stamp)
  - 构造条目；pinned 初始为 false，lastUse 取时钟 stamp。

- DataPageKey Key()
  - 页键。

- QueryResult Result()
  - 缓存的页结果。

- int Bytes()
  - 估算字节占用。

- bool Pinned()
  - 是否被固定（不参与淘汰）。

- int LastUse()
  - 最近使用的时钟值。


## DataPageKey (class)

页缓存的完整身份。仅使用 start 会把不同查询的同一页错误复用；
query/schema/server revision 均属于内容身份。

- string dataset;

- string queryText;

- int queryHash;

- int schemaVersion;

- int serverRevision;

- int generation;

- int start;

- int size;

- string cursor;

- DataPageKey(string name, string canonical, int hash, int schema, int server, int gen, int at, int pageSize, string token)
  - 内部构造；文本字段 null 记空串。

- static DataPageKey Of(QueryDescriptor query, QueryViewport viewport, QueryRequestToken token)
  - 以 token 携带的 generation/schema/server 身份构造页键；
    token 为 null 时三者取 0。

- static DataPageKey For(QueryDescriptor query, QueryViewport viewport, int gen, int schema, int server)
  - 以显式身份字段构造页键；query/viewport 为 null 时相应字段取零值。

- string Dataset()
  - 数据集名。

- string QueryText()
  - canonical 查询文本。

- int QueryHash()
  - 查询指纹。

- int SchemaVersion()
  - schema 版本。

- int ServerRevision()
  - 服务端 revision。

- int Generation()
  - 源 generation。

- int Start()
  - 页起始行。

- int Size()
  - 页大小。

- string Cursor()
  - 续页 token。

- bool Same(DataPageKey other)
  - 九个身份字段全部相等才算同一页；other 为 null 返回 false。


## DataRow (class)

一行字符串单元格。先用 Create() 构建，再逐列 Push()。

- List<string> cells;

- DataRow()

- void Push(string v)

- string At(int i)

- int Count()

- DataRow Copy()

- void Set(int i, string v)


## DataSource (class)

DataTable 的虚拟数据源（DevExpress 风格的 ValueNeeded）。

网格自身从不存储行——它按需为可见窗口拉取单元格，
并通过类型化键（`CellNum`）排序/聚合，无需
重新解析字符串。继承此类并用列式存储（每列
一个数组）支撑，可以远少于 `List<DataRow>`
字符串单元格的内存容纳数百万行，排序/汇总也无需
逐单元格解析字符串。

- virtual int RowCount()
  - 源行数。

- virtual string CellText(int row, int col)
  - 单元格的显示文本（row = 源索引，col = 列索引）。

- virtual int CellNum(int row, int col)
  - 单元格的数值键，用于数值列的排序/聚合。
    非数值列返回 0。

- virtual double CellReal(int row, int col)
  - 单元格的小数键，用于声明了 `decimals > 0` 的列。
    默认实现解析 CellText，现有数据源免费获得小数支持；
    当底层存储已持有 double 时重写它。

- virtual int CellDay(int row, int col, int order)
  - 日期列单元格的天数键（自 1970-01-01 起的天数）。
    默认实现按列声明的字段顺序解析 CellText，
    现有数据源免费获得日期支持；
    当底层存储已持有天数或时间戳时重写它。`order` 是
    列的 `dateOrder`；空单元格返回 `DataTable.NoDate()`。

- virtual bool CellBool(int row, int col)
  - bool 列单元格的真值。默认实现解析 CellText，
    现有数据源免费获得复选框列；
    当底层存储已持有标志位时重写它。

- virtual RowKey GetRowKey(int row)
  - 稳定行键入口。旧数据源默认使用仅当前源实例
    有效的临时位置键；远程/领域源应重写为业务主键。

- virtual void SetCell(int row, int col, string v)
  - 将单元格值写回底层数据（内联编辑）。
    只读数据源忽略写入。

- virtual bool CanInsert()
  - 该数据源支持 InsertRow / RemoveRow 时为 true。为 false 时
    网格隐藏添加/删除功能（以及新行占位符），
    只读或计算型数据源无需额外防护。

- virtual bool InsertRow(int at)
  - 插入空行使其落在索引 `at`（`at == RowCount()`
    即追加）。实际插入时返回 true。

- virtual bool RemoveRow(int at)
  - 移除 `at` 处的行。实际移除时返回 true。

- virtual bool ServerControlled()
  - 数据源自行管理行顺序时为 true。网格随后跳过本地
    排序和过滤——传给 CellText/CellNum/... 的 `row` 索引
    已是源最终（服务器排序/过滤后）的顺序，
    显示顺序即服务器给出的顺序。本地分组和
    聚合仍然适用，作用于已加载的前缀。

- virtual bool CellReady(int row, int col)
  - 单元格所在行是否已获取、可读取。
    未就绪的行渲染为加载占位符而非空文本，
    网格调用 RequestBlock 拉取它们。默认为 true（本地
    源按定义总是完整的）。

- virtual void RequestBlock(int row)
  - 请求数据源获取包含 `row` 的数据块。可以每帧调用：
    实现必须合并重复/重叠的请求。

- virtual void RequestNextBlock()
  - 请求数据源获取当前已加载前缀之后的下一块
    （滚动到底持续拉取，直到 HasMore() 变为 false）。
    与 RequestBlock(row) 不同：调用方不知道
    未获取区域从哪一行开始，所以它请求“下一块是什么”。

- virtual bool HasMore()
  - 数据源是否可能还有 RowCount() 之外的行。
    总数未知且仍在流式传输时为 true，
    用户滚动到底部时网格会请求下一块。

- virtual bool Poll()
  - 每帧轮询。每次重绘前、重算前调用一次；
    有新数据到达（取回的数据块、服务器事务）时返回 true，
    网格标记自身为脏并重绘。无数据到达时
    开销极小且无副作用。

- virtual DeltaResult ApplyDeltas(DeltaBatch batch)
  - 按稳定 RowKey 实时合并一批增量（upsert/remove/cell）。
    默认不支持：返回 null。支持按键定位的源（本地列表源、
    服务端缓存源）重写它；应用完成后源自行刷新页与缓存，
    网格经由 Poll 感知变化。见 DataTable.Realtime.zan。


## DataTable (class)

清除条件格式规则弹窗：表里可能同时存在多种规则（数据条/
色阶/高亮/前后规则，分布在多列），右键菜单的"清除规则"直项
只能整列一刀切。弹窗把当前生效的规则逐条列出（菜单槽位优先，
其次列工厂预设），勾选后单删或多删；勾选清除的列写入
menuCfOff 压制工厂预设——否则每帧重建的列会把预设带回来。

- static int CfEffectiveMode(DataTableState st, DataColumn dc, int col)
  - 列的生效条件格式模式：显式清除 > 菜单槽位 > 列工厂预设。

- static string CfRuleLabel(DataTableState st, DataColumn dc, int col)
  - 列的生效规则的可读名（数据条 / 色阶 / 大于均值 / …）。

- static int CfRuleColor(DataTableState st, DataColumn dc, int col)
  - 列的生效规则主色（色板 chip 用；槽位优先）。

- static void OpenCfClear(DataTableState st, List<DataColumn> cols)
  - 打开弹窗：枚举全表生效规则，默认全选。没有规则时提示
    并且不打开。

- static void ApplyCfClear(DataTableState st, bool all)
  - 应用清除：`all` 时忽略勾选整表清（全部清除按钮），否则
    只清勾选列。被清列写入 menuCfOff（压制工厂预设）并清掉
    菜单槽；名次缓存随 st.dirty 由下一帧 Recompute 重建。

- static void RenderCfClear(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataTableState st)
  - 弹窗绘制：居中模态（遮罩 + 面板），规则行 = 勾选框 +
    色板 chip + "列名 — 规则名"。行数多时面板长到窗口限度
    后列表区出滚动条（弹窗本身不做窄条——能一屏放下就放下）。

- static void CfClearBtn(App app, Canvas c, Theme t, int bx, int by, int bw, int bh, string label, string cls)
  - 弹窗按钮绘制（点击判定在调用方内联——Zan 委托不能捕获
    局部，回调走不了闭包）。


## DataTable (class)

DataTable 模块：列选择器（Column Chooser）——
DevExpress/ag-Grid 风格的勾选列表弹层。勾选控制各列
显隐（复用 HideField/ShowAllColumns 的 colHidden 状态，
与右键菜单的隐藏条目、布局持久化天然一致），搜索框按
标题实时过滤，行内 ←/→ 按钮在可见顺序中前移/后移
（复用 MoveColumn）。纯数据部分（ChooserOrder/
ChooserToggle/ChooserMove）可脱离渲染独立测试。

- static void SetColumnChooser(DataTableState st, bool open)
  - 打开/关闭列选择器弹层。打开时重置搜索与滚动，
    使每次展开都从完整列表开始。

- static bool IsColumnChooserOpen(DataTableState st)
  - 列选择器弹层当前是否打开。

- static List<int> ChooserOrder(DataTableState st, List<DataColumn> cols)
  - 选择器列表的行序：用户列顺序（st.colOrder）在前、
    未登记的列按声明序在后（与 VisibleOrder 的 seq 推导
    一致，但不过滤隐藏列——隐藏列正是列表的主角）。
    过滤掉搜索框匹配不到的列。

- static void ChooserToggle(DataTableState st, List<DataColumn> cols, int col, bool visible)
  - 勾选/取消第 `col` 列（勾选 = 可见）。与
    HideField 落在同一份 st.colHidden 上。

- static void ChooserMove(DataTableState st, List<DataColumn> cols, int col, bool left)
  - 在可见顺序中把第 `col` 列前移/后移一位。邻位取
    自 ChooserOrder 的全序列（不受搜索框过滤影响，
    拖动芯片拖到哪就是哪），冻结/分组列的实际首屏
    位置仍由 VisibleOrder 决定。
    
    直接在完整序列上换位后整体重写 colOrder：
    MoveColumn 只在 colOrder 内寻位，目标列未登记时
    "插到它之前"会退化成"追加到末尾"，无法表达
    未登记区段内的移动。

- static int ChooserRowH(App app)
  - 列表行的三段布局：勾选框 + 标题 + 排序按钮。

- static void RenderColumnChooser(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)
  - 渲染列选择器弹层：搜索框（按标题实时过滤）、虚拟化勾选
    列表（每行含 ←/→ 移动按钮与可拖拽滚动条）和页脚
    （全显 / 关闭）；点击弹层外关闭。锚定在网格右上角并
    整体限制在窗口内。


## DataTable (class)

DataTable 模块：列模型——字段名寻址、可见性与
自动适配、页脚聚合、固定/冻结及多级表头分组。

- static int Col(List<DataColumn> cols, string field)
  - 名为 `field` 的列的索引；没有列叫这个名字时返回 -1。
    未命名列（field == ""）永不匹配，空参数
    不会意外选中第一列。

- static bool HasCol(List<DataColumn> cols, string field)
  - `field` 是 `cols` 中某列名称时返回 true。

- static bool IsActionCol(List<DataColumn> cols, int col)
  - 该列是否动作列：内联按钮（cellType 7，ButtonCol）或动作
    按钮组（cellType 10 + actionOnly，见 DataColumn.Actions）。
    点击这类列是命令语义——适配器不回写标签，CellClick 上下文照常。

- static bool IsWidgetCol(List<DataColumn> cols, int col)
  - 该列是否交互组件列：内联按钮（cellType 7）或字符串组件列
    （cellType 10——开关/复选/评分/下拉/按钮组，动作与否不限）。
    这些格子的交互面是控件本身：点击不落选区/光标描边，事件由
    控件的绘制通道在释放帧归一（见 DataTable.Render 点击分支）。

- static string Cell(DataSource src, List<DataColumn> cols, int row, string field)
  - 按字段名寻址的单元格；字段未知时返回 ""——
    styler 或事件处理器可用 `Cell(src, cols, row, "state")` 代替
    裸写 `src.CellText(row, 4)`。

- static double CellValue(DataSource src, List<DataColumn> cols, int row, string field)
  - 按字段名寻址的单元格数值；未知时返回 0.0。

- static void SetCellField(DataSource src, List<DataColumn> cols, int row, string field, string v)
  - 按字段名写入单元格。未知字段是空操作，而非
    误写到其他列。

- static void SortByField(DataTableState st, List<DataColumn> cols, string field, int dir)
  - 按命名列排序（dir：1 升序，2 降序）。未知字段：空操作。

- static void AddSortField(DataTableState st, List<DataColumn> cols, string field, int dir)
  - 在命名列上追加次级排序键。未知字段：空操作。

- static void GroupByField(DataTableState st, List<DataColumn> cols, string field)
  - 按命名列分组。未知字段：空操作。

- static void SetAggregateField(DataTableState st, List<DataColumn> cols, string field, int mode)
  - 命名列的页脚聚合（见 SetAggregate）。未知字段：空操作。

- static void PinField(DataTableState st, List<DataColumn> cols, string field, bool on)
  - 冻结 / 取消冻结命名列。未知字段：空操作。

- static void HideField(DataTableState st, List<DataColumn> cols, string field, bool hidden)
  - 显示 / 隐藏命名列。未知字段：空操作。

- static string HitField(DataTableState st, List<DataColumn> cols)
  - 最近交互列的字段名（`st.hitCol`）；未命中或该列未命名时返回 ""。
    让 CellClick 处理器
    按名称分支，而不是把 HitCol() 与字面量比较。

- static string EditField(DataTableState st, List<DataColumn> cols)
  - 正在编辑的列的字段名（`st.editCol`），否则为 ""。

- static int HiddenCount(DataTableState st)
  - 当前隐藏列数。

- static void ShowAllColumns(DataTableState st)
  - 显示全部隐藏列；无变化时不触发失效。

- static void ResetColumns(DataTableState st, List<DataColumn> cols)
  - 重置全部列状态（AG Grid 的 resetColumns）：排序、逐列筛选、
    分组与折叠、冻结、隐藏、页脚聚合、菜单条件格式槽全部清空，
    列宽回到列声明的默认值。不动数据、勾选与撤销日志——重置
    是视图操作，不是编辑。条件格式槽清 0 后工厂预设自然恢复
    （EnsureColumnSlots 只回写非零槽，与单列"清除规则"同语义）。

- static void SetAggregate(DataTableState st, int col, int mode)
  - 设置列的合计行聚合（0 无，1 求和，2 平均，3 最小，4 最大，5 计数）
    并根据是否仍有列在聚合自动切换页脚。

- static void FreezeColumn(DataTableState st, int col, int side)
  - 冻结代码：0 随滚动，1 固定左缘，2 固定右缘。
    冻结列完全脱离滚动 band——不只是
    顺序提前，而是以固定 x 单独绘制。
    col 越界为空操作；side 越界钳制为 0；值不变时为空操作。

- static void PinColumn(DataTableState st, int col)
  - 冻结列到左缘。

- static void PinColumnRight(DataTableState st, int col)
  - 冻结列到右缘。

- static void UnpinColumn(DataTableState st, int col)
  - 取消冻结（回到滚动 band）。

- static void TogglePin(DataTableState st, int col)
  - 让列在 无 → 左 → 右 → 无 之间循环，
    这正是单个菜单项或重复快捷键所需的行为。

- static int FreezeOf(DataTableState st, int col)
  - 列的冻结代码（0 随滚动，1 左缘，2 右缘）；越界返回 0。

- static bool IsPinned(DataTableState st, int col)
  - 列是否冻结在左缘。

- static bool IsPinnedRight(DataTableState st, int col)
  - 列是否冻结在右缘。

- static void FreezeField(DataTableState st, List<DataColumn> cols, string field, int side)
  - 把命名列冻结到一侧（0 无，1 左，2 右）。未知
    字段：空操作，与其他按字段寻址的操作一致。

- static string BandOf(List<DataColumn> cols, int col)
  - 列的 band 路径；不属于任何分组时为 ""。

- static string BandPrefix(string path, int depth)
  - band 路径的前 `depth` 段，如 Prefix("a/b/c", 2) 返回
    "a/b"。段数少于 `depth` 时返回完整路径。

- static int BandDepth(string path)
  - 列上方有多少层分组。

- static int BandLevels(List<DataColumn> cols, List<int> vorder)
  - 可见列中最深的 band 嵌套层数——即列标题上方
    需要额外绘制的表头行数。0 表示普通表头。

- static List<BandSpan> BandRow(DataTableState st, List<DataColumn> cols, List<int> vorder, int level)
  - 某一层的跨列表头单元格，从左到右。相邻
    共享前缀的槽位合并为一个跨区；该层无分组的槽位
    不产生跨区，让列标题向上延伸到
    上方的空行。

- static List<BandSpan> BandRow(DataTableState st, List<DataColumn> cols, List<FrameColumn> columns, int level)
  - BandRow 的帧列重载：跨区按帧列列表推导（列表已含可见顺序与冻结带）。

- static List<BandSpan> BandSpans(DataTableState st, List<DataColumn> cols, List<int> vorder)
  - 所有跨区单元格，最外层在前。

- static bool BandIsCollapsed(DataTableState st, string path)
  - band 路径是否已折叠；空路径视为未折叠。

- static void SetBandCollapsed(DataTableState st, List<DataColumn> cols, string path, bool collapsed)
  - 把 band 折叠到第一列，或展开。折叠是隐藏
    组内其余列而非删除，因此它们的宽度、
    筛选和排序键在往返后依然保留。

- static void ToggleBand(DataTableState st, List<DataColumn> cols, string path)
  - 切换 band 的折叠状态。

- static void ApplyBandCollapse(DataTableState st, List<DataColumn> cols)
  - 从折叠集合重新推导 `colHidden`：折叠的 band 内
    除第一列外全部隐藏。
    
    `bandAutoHidden` 精确记录*本*机制隐藏的列，
    展开时只恢复这些列。否则用户手动隐藏的列
    会在下次折叠无关 band 时被悄悄恢复——
    隐藏是用户的决定，折叠不能
    悄悄覆盖它。

- static List<int> VisibleOrder(DataTableState st, List<DataColumn> cols)
  - 按屏幕显示顺序排列的列索引：左冻结列在前，然后是活动分组
    列、滚动列，最后是右冻结列。
    隐藏列被排除。所有布局流程（表头 / 筛选行 /
    单元格 / 命中测试 / 汇总）都遍历这一份列表，天然对齐，
    而非依赖四份重复计算保持一致。

- static int LeftFrozenCount(DataTableState st, List<int> vorder)
  - `vorder` 开头有多少列冻结在左缘。
    渲染流程用本函数和 RightFrozenCount 把顺序切成 [左 | 滚动 | 右]，
    因此一列绝不会在两个 band 中绘制。

- static int RightFrozenCount(DataTableState st, List<int> vorder)
  - `vorder` 末尾有多少列冻结在右缘。

- static void MoveColumn(DataTableState st, int from, int beforeCol)
  - 在用户列顺序中重排：移动 `from` 使其恰好位于
    `beforeCol` 之前（beforeCol < 0 时移到末尾）。若
    移动不会改变现状则空操作。

- static int PinnedWidth(DataTableState st)
  - 左冻结列按自然宽度的总宽度。

- static int PinnedRightWidth(DataTableState st)
  - 右冻结列的总宽度。

- static int ColumnOffset(DataTableState st, List<int> vorder, int col, int dataViewW, int scrollX)
  - 给定滚动偏移后，列相对表格数据原点的屏幕 x——
    这里唯一知道冻结列忽略 `scrollX`、
    右冻结列从右缘往回测量的位置。
    
    `dataViewW` 是整个数据区域的宽度（不含边栏）。
    返回相对数据原点的偏移；调用方自行加上 firstDataX。

- static int MeasureColWidth(App app, List<DataColumn> cols, DataSource src, DataTableState st, int col)
  - 列最宽的渲染单元格（标题 + 数据），供自动适配使用。扫描
    筛选后的视图，使宽度与实际显示一致。

- static void AutoFitColumn(App app, List<DataColumn> cols, DataSource src, DataTableState st, int col)
  - 将列宽设为 MeasureColWidth 的测量值；列越界为空操作。

- static void AutoFitAll(App app, List<DataColumn> cols, DataSource src, DataTableState st)
  - 对全部未隐藏列执行自动适配。

- static bool RowPassesFilterRow(DataTableState st, List<DataColumn> cols, DataSource src, int row)
  - 行是否通过筛选行快速筛选（逐列不区分大小写的 Contains
    匹配）。filterRowText 为空的列总是放行。计算列按
    公式显示文本匹配。

- static bool RowPasses(DataTableState st, List<DataColumn> cols, DataSource src, int row)
  - 行是否通过全部逐列筛选（日期预设用调用时的今天日号解析）。

- static bool RowPassesActive(DataTableState st, List<DataColumn> cols, DataSource src, List<int> af, int row, int today)
  - 只对已知有活动筛选的列执行筛选谓词。

- static string CellTextRouted(List<DataColumn> cols, DataSource src, int row, int col)
  - 筛选谓词的单元格文本：计算列取公式显示文本（与渲染
    一致），普通列走源读取。


## DataTable (class)

- static bool compsInited;

- static int compBandSeq;
  - 池控件专属保留 id 段（940000）的游标，seq 域——实际
    id = 1000000 + 940000 + 游标。与 Designer(800000/840000/
    860000)、SceneDesigner(820000/880000)、FilePicker/ChildWindow
    (900000)、Layer(930000)、IDE 助手(935000) 错开。

- static int CompBandEnter()
  - 进入池控件保留段（配对 CompBandExit，返回宿主计数器）。
    池控件在帧循环中迟到构造，而即时模式宿主（画廊等混合体）
    每帧 WidgetId.ResetFrame 会把主计数器回卷重发：从这里直接
    领号，领走的正是宿主后续某帧控件的本命 id——任何让当帧
    分配数变化的交互（插删行、toast、开窗）都会把分配前沿推过
    池控件的 id，点击按 id 归属时两个控件同时命中，事件落到
    错误的控件/行（「不同行组件事件乱飞」）。与 Layer 同一机制：
    段内取号，宿主计数器原样恢复、互不相扰。

- static void CompBandExit(int outer)
  - 离开池控件保留段：游标按实际消耗推进（各控件一次用量
    不一，按用量记账比定步长稳），宿主计数器恢复。

- static void EnsureCompRegistry()
  - 内置组件适配器装配。幂等。

- static bool EnsureCompRegistryProbe()
  - 测试钩子：触发注册表装配并返回已就绪标志。

- static void DrawCompCell(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, int rowIdx, int cc, DataSource src, DataTableState st)
  - 组件列渲染入口：解析值 → 取适配器 → 池化 → 刷值 →
    三步渲染 → 点击归一。由 DrawCellRangeCtx 的 cellType 9
    分支调用（画布已裁剪到格矩形内）。`src` 是网格当前
    数据源，用于稳定行键池化。

- static int CompPosR(DataTableState st)
  - 最近一次组件列点击在矩阵内的行（CellClick 处理器读取）。

- static int CompPosC(DataTableState st)
  - 最近一次组件列点击在矩阵内的列。

- static RowKey CompPoolKey(DataTableState st, RowKey key)
  - 组件池键命名空间：给行键冠以表状态实例序号。适配器池只按
    (RowKey, col) 存控件，而注册表（CellComp.registry）是进程级
    单例——同屏两个网格声明同一 compId 时，两表同帧会从同一
    适配器拿到同一个控件实例：互灌值、互抢命中矩形、提交串台
    （bandgrid 与 WidgetCol 形态同样中招）。业务键文本原样保留，
    同一表内的复用语义不变。

- static bool CompPoolKeyDistinctProbe(DataTableState a, DataTableState b)
  - 测试钩子：两个状态实例对同一行键得到不同池键，且同一实例
    跨帧（多次调用）稳定。


## DataTable (class)

DataTable 模块：派生显示——Top-N/集合过滤、行
分组、主从详情展开、可见显示列表、页脚
汇总与列统计。

- static void ApplySetPass(DataTableState st, List<DataColumn> cols, DataSource src, int c)
  - 从 `order` 中剔除某列筛选的集合相对阶段
    排除的每一行。与逐单元格阶段不同，它需要整个幸存
    集合，因此在行扫描后运行。

- static double SetPassReal(DataTableState st, List<DataColumn> cols, DataSource src, bool fcol, int c, int row)
  - Top-N / 高于平均值集合过滤的实数读取：公式列读公式值
    （失败行按 0），普通列走源实数路径。

- static bool HeapWorse(double a, double b, bool desc)
  - 堆根始终是当前 K 个候选中的“最差”值：Top-N 为最小值，
    Bottom-N 为最大值。相等值不替换，保留源顺序供最终并列扫描。

- static void HeapSwap(List<int> rows, List<double> vals, int a, int b)
  - 交换堆中下标 a、b 的 (行, 值) 两个元素。

- static void HeapSiftUp(List<int> rows, List<double> vals, int at, bool desc)
  - 把下标 at 的元素向上浮，恢复“最差在根”的堆序。

- static void HeapSiftDown(List<int> rows, List<double> vals, int at, bool desc)
  - 把下标 at 的元素向下沉，恢复“最差在根”的堆序。

- static void Recompute(DataTableState st, List<DataColumn> cols, DataSource src)
  - 显示模型的完整重算入口：仅在 st.dirty 时执行——逐列筛选 ->
    Top-N 集合阶段 -> 排序（分组/单键/多键）-> 筛选行 -> 筛选树 ->
    列统计 -> 显示模型 -> 页脚。服务端控制源跳过全部本地筛选与
    排序阶段（服务端行集即权威全集）。

- static int DeriveValue(DataColumn col, List<int> agg)
  - summary==6 列的派生聚合，由其他列
    已聚合的值计算——O(1)，不扫描行。

- static double DeriveReal(DataColumn col, List<double> agg)
  - DeriveValue 的小数版：百分比和比率保留小数，
    而非截断为整数。

- static bool IsGrouped(DataTableState st)
  - 表是否处于行分组模式。

- static int GroupCount(DataTableState st)
  - 分组列数量（嵌套深度）。

- static void CollapseAll(DataTableState st)
  - 折叠全部分组（不改变分组列）。

- static void ExpandAll(DataTableState st)
  - 展开全部分组（清空折叠键集合）。

- static void GroupBy(DataTableState st, int col)
  - 追加一个分组列（追加到最内层）；重复分组或 col<0 为空操作。

- static void Ungroup(DataTableState st)
  - 取消分组：清空全部分组列与折叠状态。

- static void UngroupOne(DataTableState st, int col)
  - 从分组中移除一列，其余保持顺序。供
    分组面板的逐芯片移除按钮使用，`Ungroup` 的全清
    过于粗暴。

- static void MoveGroup(DataTableState st, int col, int to)
  - 把已分组的列移到新的嵌套深度。`to` 是外层到内层列表中的
    目标索引；越界值钳制到两端，
    拖过任一边缘都会得到合理结果而非无响应。

- static int GroupDepth(DataTableState st, int col)
  - 分组列的嵌套深度（0 = 最外层）；列未分组时返回 -1。
    列未分组时返回 -1。

- static void GroupByAt(DataTableState st, int col, int at)
  - 把列插入分组中的指定深度，而非像 `GroupBy` 那样
    追加到最内层。供面板的按位置放置
    手势使用；已分组的列被移动而非复制。

- static bool IsCollapsed(DataTableState st, string key)
  - `key` 对应的分组当前是否折叠。

- static void ToggleCollapse(DataTableState st, string key)
  - 切换 `key` 分组的折叠状态。

- static void FillGroupMembers(DataTableState st, GroupRow grp, int lo, int hi)
  - BuildDisplay 关组时物化叶子组的成员源行（st.order 的
    连续片段）。只对最内层组调用；外层组按需经
    GroupMemberRows 从后代叶子组收集。

- static int GroupCheckState(DataTableState st, GroupRow grp, DataSource src)
  - 组表头三态复选框的选中状态：0 = 全未选（含空组），
    1 = 全选，2 = 部分选中。叶子组直接读物化成员；父组
    沿 key 前缀汇总可见后代叶子组的成员——PathKey 每层
    以分隔符结尾，子组键必以父组键开头。折叠的父组没有
    已构建的后代，按未选处理（重新展开后状态即恢复，
    选择本身存在 st.selected / selectionModel 里）。

- static void SelectGroupRows(DataTableState st, GroupRow grp, DataSource src, bool val)
  - 组表头复选框点击：把组成员整组置为 `val`。成员收集
    规则与 GroupCheckState 相同。

- static void SetTreeAdapter(DataTableState st, TreeAdapter adapter, int treeColumn)
  - 启用树模式（传入适配器）或关闭（null）。`treeColumn`
    是渲染缩进与展开箭头的列索引（-1 = 首列）。关闭时
    展开状态一并清空；开启时保持干净状态。

- static bool HasTree(DataTableState st)
  - 树模式是否启用。

- static void TreeReserve(DataTableState st, DataSource src)
  - 补齐按源行索引对齐的展开标志列表。

- static bool IsTreeExpanded(DataTableState st, DataSource src, int row)
  - `row`（源索引）是否展开。折叠/叶子/越界均为 false。

- static bool CanTreeExpand(DataTableState st, DataSource src, int row)
  - `row` 是否有子行（可展开）。适配器为 null 或行越界
    时为 false。懒加载行在子行尚未取回时同样可展开——
    展开本身就是取回的触发器。

- static void TreeExpand(DataTableState st, DataSource src, int row)
  - 展开 `row`。叶子或已展开的行保持不变。

- static bool TreeIsLoading(DataTableState st, int row)
  - 懒加载进行中的行（树列绘制占位指示）。

- static void TreeLoaded(DataTableState st, DataSource src, int row)
  - 应用取回懒加载子行后调用：解除 loading 标记并触发
    重算，已插入数据源的子行在下一帧出现。

- static void TreeCollapse(DataTableState st, DataSource src, int row)
  - 折叠 `row`。未展开的行保持不变。

- static void TreeToggle(DataTableState st, DataSource src, int row)
  - 切换 `row` 的展开与折叠。

- static void TreeExpandAll(DataTableState st, DataSource src)
  - 展开全部含子行的行。远程/懒加载源上慎用：
    这会对每个行调用一次 ChildCount。

- static void TreeCollapseAll(DataTableState st, DataSource src)
  - 折叠全部行（根行保留）。

- static void TreeExpandToLevel(DataTableState st, DataSource src, int level)
  - 展开到指定深度：深度 < level 的祖先全部展开，
    深度 >= level 的行折叠。level 0 = 只显示根，
    level 1 = 根 + 直接子行，以此类推。负值按 0 处理。
    深度来自适配器的层级投影（BuildTreeDisplay 与本
    方法共享同一 DFS 定义）。

- static void SetDetail(DataTableState st, DetailProvider p)
  - 挂接绘制详情面板的钩子以启用该功能。传入
    null 可关闭（所有行折叠；其余状态不受影响）。

- static bool HasDetail(DataTableState st)
  - 主从详情是否处于启用状态。

- static int DetailSlots(DataTableState st, DataSource src, int row)
  - 详情 band 的高度，以整行高计，至少 1。向
    provider 询问使每行不同大小（如每条子记录一个槽位）成为可能。

- static bool IsExpanded(DataTableState st, int row)
  - `row`（源索引）当前是否已展开。旧重载保留给
    只持有源索引的兼容调用；内部渲染优先使用 key 重载。

- static bool IsExpanded(DataTableState st, DataSource src, int row)
  - `row` 是否展开（key 模式按业务键匹配，否则按源行索引）。

- static bool CanExpand(DataTableState st, DataSource src, int row)
  - `row` 是否提供详情面板——功能关闭或
    provider 拒绝该行时返回 false，此时不绘制箭头。

- static void ExpandRow(DataTableState st, DataSource src, int row)
  - 打开 `row` 的详情面板。provider 拒绝的行或已打开的
    行保持不变。

- static void CollapseRow(DataTableState st, int row)
  - 关闭 `row` 的详情面板。未打开的行保持不变。

- static void CollapseRow(DataTableState st, DataSource src, int row)
  - CollapseRow 的 key 感知重载：key 模式按业务键移除，
    否则委托给源索引重载。

- static void ToggleDetail(DataTableState st, DataSource src, int row)
  - 切换 `row` 详情面板的展开与收起。

- static void CollapseAllDetail(DataTableState st)
  - 关闭所有已展开的详情面板。

- static int ExpandedCount(DataTableState st)
  - 当前有多少行的详情面板处于展开状态。

- static string PathKey(DataTableState st, List<DataColumn> cols, DataSource src, int rowIdx, int level)
  - 数据行 `rowIdx` 在嵌套层级
    `level` 所属分组的路由键——到该层为止各分组列值的拼接，
    用控制分隔符连接，避免不同路径冲突。

- static string GroupValue(List<DataColumn> cols, DataSource src, int rowIdx, int gc)
  - 行在列 `gc` 上对其分组贡献的值。日期列
    折叠为分桶标签（年 / 月 / 季度 / 星期），
    按日期分组不会每天生成一个组。

- static int CmpGroupRow(DataTableState st, List<DataColumn> cols, DataSource src, int ra, int rb)
  - 先按分组列（外层→内层）比较两行，再按
    活动排序列作决胜键，同组行保持相邻。

- static int CmpDateGroup(DataColumn col, int da, int db)
  - 按分组桶比较两个日期单元格，回退到原始日期，
    使桶内行保持时间顺序。星期桶（模式 4）
    按周日..周六而非日期排列。

- static int WeekdayOf(int days)
  - 日号对应的星期几（0=周日）；空白排在最前。

- static void MergeGrp(DataTableState st, List<DataColumn> cols, DataSource src, int lo, int mid, int hi, List<int> tmp)
  - 分组排序归并的一趟合并（比较走 CmpGroupRow）。

- static int CmpGroupCol(DataColumn gcol, DataSource src, int ra, int rb, int kd, int col)
  - 按单个分组列比较两个代表行，与
    CmpGroupRow 的逐列排序一致（先日期桶，再按列类型取值）。

- static void SortOrderGroupBucket(DataTableState st, List<DataColumn> cols, DataSource src)
  - 单分组列且无活动组内排序这一常见情况的 O(N) 分组排序：
    不用 O(N log N) 比较归并排序，而是
    单遍为每行分配小组槽位（用类型化哈希——数值列
    不用字符串键），按组顺序给少量不同槽位排名，
    再用稳定计数排序把行放入连续
    分组，省去对百万行约 20 趟归并扫描。

- static void SortOrderGrouped(DataTableState st, List<DataColumn> cols, DataSource src)
  - 分组排序入口：单分组列且无组内排序时走 O(N) 桶排序
    快路径，否则按 CmpGroupRow 做稳定归并排序。

- static void FillGroupAgg(DataTableState st, List<DataColumn> cols, DataSource src, GroupRow grp, int lo, int hi, bool toGrand)
  - 对 st.order[lo..hi) 连续片段计算逐列聚合，
    结果存入 grp.aggValues / grp.aggTexts（与
    cols 索引对齐）。只扫描 aggMode > 0 且 != 6（派生）的列；
    派生列保持 0（它们引用页脚级聚合值，而非
    每组的值）。O(|hi-lo| * 聚合列数)。

- static bool GroupLevelEq(List<DataColumn> cols, DataSource src, int ra, int rb, int gc, int kd)
  - 两行在列 `gc` 上是否同组，基于
    类型化值判断（kd：0 文本/小数文本，1 整数，2 布尔，3 日期桶），
    热行循环绝不构造字符串键。与 GroupValue 的同一性一致：
    整数列以整数为键，小数列和纯文本以
    格式化文本为键，布尔以标志为键，日期以桶标签为键。

- static int DispCount(DataTableState st)
  - 显示槽位总数（数据行 + 组表头 + 详情 band）。

- static int DispKind(DataTableState st, int i)
  - 槽位类型：0 数据行，1 组表头，2 详情 band。

- static int DispRowOf(DataTableState st, int i)
  - 数据/详情槽位的源行索引（表头槽位为组索引）。

- static int DispNo(DataTableState st, int i)
  - 行号边栏显示的从 1 开始的数据行号（非数据行为 0）。

- static int DispPart(DataTableState st, int i)
  - 多槽详情 band 内的槽位序号（其他类型为 0）。

- static int DispDepth(DataTableState st, int i)
  - 树模式下数据槽位的嵌套深度（根 = 0；其他类型
    或非树模式为 0）。渲染把它换成树列的缩进。

- static int DispLevel(DataTableState st, int i)
  - 组表头槽位的嵌套深度（其他类型为 0）。深度存放在
    该槽位指向的 `GroupRow` 上，而非显示槽位自身。

- static void BuildDisplay(DataTableState st, List<DataColumn> cols, DataSource src)
  - 由已筛选+排序的 st.order 构建扁平显示模型
    （组表头与数据行交错）。未分组时，
    模型与 st.order 一一对应，下游所有渲染/命中路径
    行为与之前完全相同。
    
    分组路径单遍完成：通过比较
    当前行与上一行的类型化分组值来发现组边界（st.order
    已排序，成员连续），而非为每行重建
    约 4 次拼接字符串键。拼接键（用于折叠
    标识）只在每个组表头构建一次（数量极少），
    每个组的小计在组关闭时填充一次。

- static void BuildTreeDisplay(DataTableState st, List<DataColumn> cols, DataSource src)
  - 树模式显示模型：以适配器的父子结构重新投影
    `st.order`。行先经筛选/排序（order 成员仍是源索引），
    再按"展开的父行后跟其可见后代"交错：
    - 只有 order 中存在的行（未被筛选掉的）参与投影；
    - 排序决定同层兄弟的相对次序（order 的相对顺序）；
    - 折叠的子树整段隐藏，但行仍在 order 中，展开后
    无需回源即可恢复。
    
    实现是显式栈的 DFS（避免深树递归溢出），兄弟按
    order 相对次序弹出。每行带 depth 供树列缩进；
    详情带在数据行之后照常发出。

- static void EmitDetail(DataTableState st, DataSource src, int row)
  - `row` 展开时为其追加详情 band：`DetailSlots` 个
    类型 2 条目，每个指向主行并以 `part` 编号。
    折叠的行，或表中没有 provider 的行，不贡献任何内容——
    未展开的网格与功能加入前的表现完全一致。
    
    band 占用整行槽位而非任意像素高度，
    主体可用一次乘法在槽位与 Y 之间换算；见
    `DispRow`。

- static void ComputeSummary(DataTableState st, List<DataColumn> cols, DataSource src)
  - 对当前筛选集合（st.order）的逐列聚合，缓存为
    格式化字符串，使汇总行每帧重绘零成本。
    
    基础聚合（求和/平均/最小/最大）在 SINGLE 一次遍历中收集：
    所有此类列一起累加，N 个聚合列
    只需一次遍历而非各一次。派生列
    （summary==6）再以 O(1) 从缓存聚合计算，
    不再触碰行。

- static void FinalizeSummary(DataTableState st, List<DataColumn> cols, List<double> sumV, List<double> minV, List<double> maxV, int n)
  - 把逐列基础累加器（对 `n` 行的求和/最小/最大）转为
    缓存页脚：基础模式直接解析，派生（模式 6）列读取
    已完成的基础聚合，所有内容只格式化一次。
    由扁平行扫描路径（ComputeSummary）和分组合并路径
    （ComputeSummaryFromGroups）共用，两者生成字节一致的页脚。
    透视视图例外：总计行由物化聚合精确写出
    （PivotFinalizeSummary），常规扫描看不到格内权重。

- static void ComputeSummaryFromGroups(DataTableState st, List<DataColumn> cols)
  - 分组页脚：合并 BuildDisplay/FillGroupAgg 已收集的每组基础聚合
    （L==0 组的 st.aggSumR/aggMinR/aggMaxR，
    这些组恰好划分每行一次）为总计，
    无需第二次百万行扫描。组和之和 = 总和，
    组最小之最小 = 总最小，组最大之最大 = 总最大，平均值 = 总和
    / 总行数——页脚与扁平重算结果完全一致。

- static void ResetGrandAgg(DataTableState st, int nc)
  - 重置 FillGroupAgg 喂入的总计累加器。min/max 以
    哨兵值起步，使首个贡献的组无需逐列
    "已见"标志即可播种；没有扫描行的列通过 FinalizeSummary
    的 aggMode 分支保持聚合为 0。

- static void ComputeColStats(DataTableState st, List<DataColumn> cols, DataSource src)
  - 筛选集合上的逐列 min/max，用于数据条和色阶
    归一化。只扫描 cfMode 为 1 或 2 的列。

- static void RankByValue(List<double> vals, List<int> ranks)
  - Top/Bottom 名次：ranks[k] = vals 中严格大于 vals[k] 的个数
    （并列值同名次，与旧 O(n²) 逐对计数语义一致）。索引数组按
    值降序稳定归并排序，同值段的首位即该段的名次。

- static void RankMerge(List<double> vals, List<int> idx, int lo, int mid, int hi, List<int> tmp)
  - RankByValue 的一趟合并：按值降序，相等保留原序（稳定）。

- static int CfOrderPos(DataTableState st, int row)
  - 行源索引在过滤集合（st.order）中的显示序；没有 Top/Bottom
    规则列时字典不维护，恒 -1。渲染的 Top/Bottom 命中判定
    用它替代对 st.order 的线性查找。


## DataTable (class)

- static TableDiagnostic DuplicateKeyDiagnostic(string key, int row)
  - 诊断代码：0 无，1 重复键，2 缺少稳定键，3 过期结果，
    4 schema 不匹配，5 查询能力不支持。

- static TableDiagnostic MissingKeyDiagnostic(int row)
  - 代码 2：行没有稳定键，已回退临时位置键。

- static TableDiagnostic StaleResultDiagnostic(int generation)
  - 代码 3：查询结果已过期；generation 记录在 row 字段。

- static TableDiagnostic SchemaDiagnostic(string field, string message)
  - 代码 4：schema 不匹配，field 标识问题列，行号为 -1。


## DataTable (class)

DataTable 模块：单元格内编辑——开始/提交/取消、日历与
选项选择器、布尔切换和数值步进。

- static void BeginEdit(DataTableState st, List<DataColumn> cols, DataSource src, int rowIdx, int col)
  - 在 (rowIdx, col) 打开行内编辑器：预填当前单元格文本并把
    光标置于末尾。下拉/日历编辑器（editorKind 1/2）立即展开
    弹层并定位高亮项；日期解析失败或为空时日历定位到当前月。
    列不可编辑或位置非法时不动作。

- static void CalendarStep(DataTableState st, DataColumn c, int days)
  - 把日历选择移动 `days` 天，显示月份跟随，
    跨月时自动翻页，并把新日期
    写入缓冲区。

- static int IndexOfChoice(DataColumn c, string v)
  - `v` 在列选项中的位置（不区分大小写），否则返回 -1。

- static void RefreshChoices(DataTableState st, DataColumn c)
  - 按键后重新筛选下拉列表，高亮保持在
    仍在列表中的输入值上，否则落在第一个匹配项。

- static void MoveChoice(DataTableState st, int step)
  - 在筛选后的列表中移动下拉高亮 `step` 个位置，
    两端钳制。

- static void CancelEdit(DataTableState st)
  - 关闭编辑器并丢弃未提交的缓冲区，同时收起弹层、
    清空错误与拒绝标记。

- static void ToggleBoolCell(DataTableState st, List<DataColumn> cols, DataSource src, int row, int col)
  - 翻转布尔单元格并记为已提交的编辑。布尔列没有
    可输入的文本，因此点击或 F2/Enter 就地切换，
    而非打开行内编辑器。

- static void CommitEdit(DataTableState st, List<DataColumn> cols, DataSource src)
  - 校验缓冲区并写回行。值必须通过
    列的声明式规则，并在 `CellValidating` 事件中存活，
    处理器可调用 `Reject(msg)`。被拒绝的值保持编辑器
    打开并显示消息，让用户修正而非
    丢失输入。


## DataTable (class)

DataTable 分部：文件导出（CSV 同步 + 异步流式；XLSX 支路在
DataTable.ExportXlsx.zan——XlsxBook/Zip/Deflate 家族只有真的
导出 Excel 的程序才需要，按需拉取据此整支裁掉，见 A332）。
行选择语义与 `BuildText`（剪贴板复制）完全一致，
"所见即所得"——导出的就是过滤/排序后看到的行。序列化本体在
`XlsxBook`（数据层），不依赖 GUI；服务端/控制台
程序直接用 XlsxBook.SaveStreaming 即可。

- static ExportJob sExportJob;

- static AtomicInt sExportBusy=new AtomicInt(0);
  - 后台导出忙标志：0 空闲 / 1 占用，抢占成功者才开工。

- static Func<bool> sXlsxCore;
  - xlsx 写盘核心槽：DataTableXlsx.ExportToXlsxAsync 启动任务前
    装入（闭包持有实参），Worker 线程只经此槽调用——主文件不
    拼 Xlsx 家族的任何类型，xlsx 支路随导出调用按需拉入。

- static XlsxExportUiFn sXlsxExportUi;
  - xlsx 弹层导出入口槽：由 DataTableXlsx.Importable()（或宿主
    显式 DataTableXlsx.InstallExportUi）装配。Overlays 的 Excel
    菜单项只经此槽调用——本文件族若拼 DataTableXlsx 类名，
    按需拉取会把 XlsxBook/Zip/Deflate 拖进每个放表格的程序
    （A332）。未装配 = null，菜单项点击报「未启用 Excel 导出」。

- static void InstallXlsxExportUi(XlsxExportUiFn fn)
  - 装配 xlsx 弹层导出入口（幂等）。DataTableXlsx.InstallExportUi
    在文件被拉入时调用；需要 Excel 导出但从未装配的宿主可显式
    调用同一个入口触发拉入。

- static bool RunXlsxExportUi(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly)
  - Overlays 的 Excel 菜单项走这里：经槽调 xlsx 入口，未装配
    返回 false（弹层显示「没有可导出的内容」——与无可导内容
    同形，xlsx 写盘家族不进图）。

- static bool ExportToCsv(DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly)
  - 导出为 CSV 文件，始终含表头行。行选择：
    `selectionOnly` 为 false 时导出整个过滤/排序后的视图；
    为 true 时有单元格范围导范围（行×列），否则导勾选行，
    两者皆无返回 false。内容与复制粘贴一致（显示文本，
    RFC 4180 转义）。UTF-8 带 BOM，Excel 双击打开不乱码。
    逐行流式写出，内存占用与行数无关；路径打不开返回 false。

- static bool ExportToCsvAsync(DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly, DataTableExportProgress progress)
  - 异步导出为 CSV，语义与 `ExportToXlsxAsync`
    相同（逐行流式 + BOM + RFC 4180 转义）。

- static DataTableExportSnapshot ExportBegin(bool selectionOnly, DataTableState st, int colCount, DataSource src, DataTableExportProgress progress)
  - 异步导出的公共前置：忙标志占位 + 当前线程冻结快照。失败
    时负责把忙标志放回去。返回 null = 拒绝启动。

- static DataTableExportSnapshot ExportSnapshot(DataTableState st, int colCount, DataSource src, bool selectionOnly)
  - 在调用线程（通常是 UI 线程）冻结导出行集：列区间与行序一次
    取走，之后表格再排序/过滤/勾选都不影响本次导出。无可导内容
    返回 null。

- static bool ExportCsvSnapshot(DataTableExportSnapshot snap, List<DataColumn> cols, DataSource src, string path, DataTableExportProgress progress)
  - CSV 写盘核心：逐行流式（UTF-8 带 BOM；RFC 4180 转义与
    复制粘贴一致）。返回 false = 取消或写盘失败。


## DataTable (class)

- static bool ExportToXlsxAsyncUi(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly)
  - 异步导出 .xlsx 并显示进度弹层：经 sXlsxExportUi 槽（xlsx 写盘
    家族随真实导出调用才拉入，见 A332）。CSV 入口是
    `ExportToCsvAsyncUi`。

- static bool ExportToCsvAsyncUi(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly)
  - 异步导出 CSV 并显示进度弹层，语义同上。

- static bool ExportAsyncUiImpl(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly, int kind)


## DataTable (class)

DataTable 模块：筛选谓词——不区分大小写的文本匹配、
表头筛选菜单使用的逐列文本/比较/预设筛选。

- static int FoldC(int cc)
  - ASCII 大写折叠为小写；其余码点原样。

- static bool MatchAt(string hay, string needle, int off)
  - hay 从 off 起是否以 needle 开头（不区分 ASCII 大小写）。

- static bool EqCI(string a, string b)
  - 不区分 ASCII 大小写的相等判断（长度不同直接 false）。

- static bool ContainsCI(string hay, string needle)
  - hay 是否包含 needle（不区分 ASCII 大小写）；空 needle 恒 true。

- static int IndexOfCI(string hay, string needle)
  - needle 在 hay 中首次出现（不区分 ASCII 大小写）的字节
    下标；无命中返回 -1。与 ContainsCI 同一匹配语义，供
    查找高亮定位匹配段。

- static bool StartsCI(string hay, string needle)
  - hay 是否以 needle 开头（不区分 ASCII 大小写）。

- static bool EndsCI(string hay, string needle)
  - hay 是否以 needle 结尾（不区分 ASCII 大小写）。

- static bool TextMatch(string v, string q, int mode)
  - 按 mode 匹配：2 等于、3 前缀、4 后缀，其余（含 1）为包含；
    空查询恒 true。均不区分 ASCII 大小写。

- static string Backspace(string s)
  - 删除最后一个字符（而非最后一个字节），
    确保多字节字符整体删除。

- static bool CellPasses(ColFilter f, string v)
  - 单个单元格值是否通过列筛选的每个阶段。
    向后兼容入口：在没有列类型的情况下筛选值，
    比较阶段因此回退为纯文本排序。

- static bool CellPassesCol(ColFilter f, DataColumn col, string v, int today)
  - 单个单元格的行级筛选阶段。`today` 是当前日号，
    传入以确保一次遍历的每行都基于同一时刻
    解析日期预设。Top-N 不在此判定：它需要整个集合。

- static bool CmpPasses(ColFilter f, DataColumn col, string v)
  - 用筛选的比较阶段测试单元格。排序依据来自
    列类型：数值列按值比较，日期列按
    日号；只有无类型列回退为文本。

- static List<int> PresetRange(int preset, int today)
  - 日期预设覆盖的闭区间日范围，相对 `today`。
    预设关闭或该侧无界时返回 false；
    lo/hi 写入双元素列表（0 = lo，1 = hi）。

- static bool PresetPasses(ColFilter f, DataColumn col, string v, int today)
  - 用筛选的日期预设测试单元格。不含
    可识别日期的单元格永不满足预设。

- static int Today()
  - 今天的日号，用于解析相对日期预设。

- static int CompareCell(string a, string b, bool numeric)
  - 单元格比较：numeric 时按 ParseInt 比较，否则按文本。

- static int CompareCellCol(DataColumn col, string a, string b)
  - 感知列类型的单元格比较：小数列按 double 比较，
    1.5 与 1.9 不再相等，整数列保持精确的 int 路径，
    其余按文本比较。

- static string Truncate(string text, int fontSize, int maxW)
  - 按字号与像素宽截断文本并追加省略号；本就放得下时原样返回。
    截断点回退到字符边界，避免多字节字符被切半。

- static int TypeColor(App app, int type)
  - 语义类型对应的主题前景色：1 primary、2 info、3 success、
    4 warning、5 error；样式无前景色时为 0。


## DataTable (class)

- static void SetFilterTree(DataTableState st, FilterGroup root)
  - 挂接（或 null 清除）组合筛选树。改动即失效显示模型，
    与 SetFilter 等入口一致。

- static FilterGroup FilterTreeOf(DataTableState st)
  - 当前组合筛选树（未挂接时 null）。

- static bool HasFilterTree(DataTableState st)
  - 组合筛选树是否有内容（null 或空组均为 false）。

- static bool GroupHasContent(FilterGroup g)
  - 组内是否有任何条件或子组；null 视为无内容。

- static bool GroupPasses(DataTableState st, FilterGroup g, List<DataColumn> cols, DataSource src, int row, int today)
  - 组求值：AND 组全过才过，OR 组一过即过。空组恒通过。

- static bool ConditionPasses(FilterCondition c, List<DataColumn> cols, DataSource src, int row)
  - 条件求值。条件按 field 名找列；找不到的列按"不匹配"
    处理——拼错的 field 不应静默放行。

- static bool RowPassesFilterTree(DataTableState st, List<DataColumn> cols, DataSource src, int row, int today)
  - 行是否通过组合筛选树（Recompute 在逐列筛选之后调用）。
    未挂接树时恒真。


## DataTable (class)

DataTable 模块：筛选条件描述——把单列 ColFilter 的
六个阶段（值勾选/文本/批量/比较/日期预设/Top-N）与
FilterBuilder 组合树压缩成人类可读短文本，
供状态栏筛选概览条与外部 UI 显示。

- static string CmpOpText(int cmpMode)
  - 比较运算符的符号（cmpMode 1..8；9/10 由 is empty/not empty 单独表达）。
    编号对齐 ColFilter 注释：1 >，2 >=，3 <，4 <=，5 =，6 <>，7 介于，8 不介于。

- static string DatePresetText(int preset)
  - 日期预设的短标签（datePreset 1..11，与 Rules 页芯片一致；
    键与 FilterUI 芯片文案共用，语言包里一份译文两处生效）。

- static string TopModeText(int topMode)
  - Top-N 模式的短标签（topMode 5/6；带 N 的四种
    由 DescribeColFilter 直接拼 N）。

- static string DescribeColFilter(ColFilter f)
  - 一列筛选条件的短描述，如 `>= 100`、`≠ 2`、
    `contains ab`、`Top 10`。无活动条件返回 ""。

- static List<string> FilterSummaryItems(DataTableState st, List<DataColumn> cols)
  - 状态栏筛选概览条的条目：一列一条（标题 + 短描述），
    另加组合树一条。纯数据面，测试可直接断言。

- static string DescribeFilterTree(DataTableState st, int maxLen)
  - 组合树的短描述：`region = EU AND (price > 100 OR grade = B)`。
    子组以括号包住（括号在嵌套与顶层统一）；文本总量截断到
    maxLen（超出加 …，总长恰为 maxLen+1）。

- static string DescribeGroup(FilterGroup g, int maxLen)
  - 递归描述一个组：条件与子组按组的连接词（AND/OR）串联，
    子组整体加括号。g 为 null 返回 ""。

- static string DescribeCondition(FilterCondition c)
  - 单个条件的短文本，如 `region = EU`、`price > 100`、
    `name contains a`；op 编号与 FilterCondition 的 0–11 一致。


## DataTable (class)

DataTable 模块：表头筛选 UI——DevExpress 风格的分页筛选
弹窗（值/规则/日历）、行内编辑器和集合筛选菜单。

- static void OpenFilterMenu(DataTableState st, List<DataColumn> cols, DataSource src, int col)
  - 打开 `col` 列的表头筛选菜单（重置页签/搜索/批量行并
    预取去重值）。图标点击与表头上下文菜单的"筛选…"项
    共用，保证两条入口的初始状态一致。

- static void RenderEditor(App app, List<DataColumn> cols, DataSource src, DataTableState st, int edX, int edY, int edW, int edH)
  - 打开的编辑器，绘制在单元格之上。文本和步进编辑器是
    带光标的输入框；下拉和日历编辑器在下方（或上方，
    当单元格靠近窗口底部时）附加弹层。校验失败的
    会在弹层位置显示错误消息。

- static void RenderChoiceList(App app, DataColumn col, DataTableState st, int px, int py, int pw, int ph)
  - 下拉列表：筛选后的候选中最多显示 8 行。

- static void RenderCalendar(App app, DataColumn col, DataTableState st, int px, int py, int pw, int ph)
  - 月历网格。点击日期按列自身的日期
    格式写回，存储文本保持列声明的格式。

- static bool TabBtn(App app, int bx, int by, int bw, int bh, string label, bool active)
  - 筛选弹窗的分页标签按钮（Values/Text/Batch/Rules），
    active 时呈选中态；返回本次是否被点击。

- static bool IconBtn(App app, int bx, int by, int bw, int bh, string icon, bool active)
  - 纯图标工具栏按钮，带"激活"（按下）外观。

- static bool TextBtn(App app, int bx, int by, int bw, int bh, string label, bool primary)
  - 带标签的工具栏按钮，`primary` 时用强调色填充。

- static void EditBox(App app, int bx, int by, int bw, int bh, string text, string ph, bool focused)
  - 绘制一个文本框：text 为内容，空文本时显示占位符 ph；
    focused 时画焦点框与光标。纯绘制，不处理输入。

- static bool HandleNavKey(App app, List<DataColumn> cols, DataSource src, DataTableState st, List<FrameColumn> columns, int visRows, int dataViewW, int pinnedW, int kc, int emods)
  - 为单元格光标处理一次按下。按键被消费时
    返回 true，调用方可阻止其进入滚动/选区处理。
    
    移动按显示槽位进行（因此会跳过组表头），
    并按可见列位置进行（隐藏与重排的列行为正确）。
    Shift 从锚点扩展范围；Ctrl 跳到边缘。

- static bool Chip(App app, int bx, int by, int bw, int bh, string label, bool on)
  - `on` 时呈选中态的小胶囊。用于规则芯片，
    行为类似单选：点击已激活的会关闭规则。

- static int RulesTabHeight(App app, ColFilter f, DataColumn col)
  - "Rules" 筛选页：值比较、相对日期预设（仅日期
    列）和 Top-N。返回所绘内容下方的 y。
    RenderRulesTab 为这个 筛选/列 组合占用的高度。
    与渲染器并列维护以保持同步：其中的每次 `cy` 前进
    在此都有对应项，包括仅日期列
    或选定计数型前/后模式后才出现的部分。

- static int RenderRulesTab(App app, int mx, int cy, int menuW, int pad, ColFilter f, DataColumn col, DataTableState st)
  - 绘制 "Rules" 筛选页：比较运算符芯片与操作数输入框、
    空值规则、日期预设（仅日期列）与 Top-N（含 N 输入框
    与 +/- 步进）；改动即失效查询。返回绘制后的 y。

- static void RenderMenu(App app, int x, int y, int viewW, int headerH, int numW, int selW, List<DataColumn> cols, DataSource src, DataTableState st)
  - 列头筛选弹窗总入口：标题 + 升/降序按钮、四个标签页
    （Values/Text/Batch/Rules）与页脚 Clear/Close；按键按
    editFocus 路由到对应编辑框，点击面板外（且不在列头上）
    时关闭。锚定在 `mi` 列下方，整体限制在窗口内。

- static void DrawMiniCheck(App app, int bx, int cy, int itemH, bool checked)
  - 在行高 itemH 内垂直居中画一个小复选框；checked 为选中态。


## DataTable (class)

计算列（Formula）分部：表达式解析与逐行求值。

`DataColumn.formula` 非空的列不读数据源：每个单元格的值
是对表达式求值的结果。语法（单遍递归下降，运算优先级
与 C 一致）：

```text
expr    := term (('+' | '-') term)*
term    := unary (('*' | '/') unary)*
unary   := '-' unary | primary
primary := 数字 | 函数调用 | '(' expr ')' | 列引用
```

- 数字：十进制字面量；一元负号由 unary 层处理；
- 函数：`min(a,b,...)`、`max(a,b,...)`（变参）、
`abs/round/floor/ceil/sqrt(x)`，`round` 可带第二参数
指定小数位；sqrt 用 Newton 迭代（运行时无 Math.Sqrt）；
- 列引用：裸标识符，先按 `field` 名解析，回退按 `title`；
嵌套公式列被引用时读到的是其**源数据**（未计算的），
因此跨公式引用应指向普通列；
- 括号深度上限 32，超出视为畸形。

语义全部 fail-soft（商业网格惯例：公式列不抛异常、
不打断渲染）：
- 解析（与行无关）：未知列名、未知函数、畸形表达式、
尾随垃圾 → 整列显示空；
- 求值（逐行）：除以 0 → 该行显示空。

实现是两段式：构建/列变更时把表达式解析成逆波兰
token 序列（`FormulaExpr`），此后每行求值只是一次
纯栈运算——不重复做名字查找与语法分析，O(rows ×
tokens)。

- class FormulaExpr
  - 逆波兰 token：0 数字，1 列引用，2 +，3 -，4 *，5 /，
    6 一元负号，7 min，8 max，9 abs，10 round，11 floor，
    12 ceil，13 sqrt。

- static bool FormulaEval(FormulaExpr e, DataSource src, int row, out double value)
  - 对编译好的表达式按行求值。返回 false 表示该行结果
    无效（除以 0），调用方渲染空文本。解析已失败的表达式
    由 FormulaExprOf 缓存为 null，不会进入这里。

- static bool IsFormula(List<DataColumn> cols, int col)
  - 列是否为计算列。

- static string FormulaText(List<DataColumn> cols, DataSource src, int row, int col)
  - 计算列在 `row` 行的显示文本。非计算列返回 null（调用方
    回落普通读取路径）；计算列求值失败（畸形表达式、除以
    0、负数开方）返回 ""——fail-soft，绝不打断渲染。

- static bool FormulaTryReal(List<DataColumn> cols, DataSource src, int row, int col, out double v)
  - 计算列在 `row` 行的实数值。非计算列返回 false。

- static FormulaCacheEntry sFormulaCache;
  - 每列表达式编译缓存。表达式对象不可变，列列表或表达式
    字符串变更时由 InvalidateFormulaCache 整体作废。

- static FormulaExpr FormulaExprOf(List<DataColumn> cols, int col)
  - 取列的编译结果；缓存按列列表指针与列数整体命中，
    未命中则整表重编译。非计算列缓存为 null。

- static void InvalidateFormulaCache()
  - 修改任意列的 formula 字段后调用：整体作废编译缓存。
    FormulaText/FormulaTryReal 在下一次调用时重新编译。


## DataTable (class)

- static void Refresh(DataTableState st)
  - 手动刷新数据源。刷新会使派生顺序、筛选、汇总和键索引失效；
    行本身的业务身份由 KeyProvider 决定。

- static void Refresh(DataTableState st, DataSource src)
  - 当前数据源的键快照发生变化时，重新建立查询/选择相关索引。

- static void PruneExpandedKeys(DataTableState st, DataSource src)
  - 移除已经不再存在于当前数据源的详情键。
    刷新/删除后不能让“幽灵”详情继续占用展开计数，
    也不能在后续重用同一键时意外复活旧面板。

- static void SetKeyProvider(DataTableState st, RowKeyProvider provider)
  - 安装稳定行键提供器。传入 null 恢复兼容的临时位置键。

- static void RebuildRowKeys(DataTableState st, DataSource src)
  - 重建当前源的键快照。调用方应只在源身份改变或首次绑定时触发；
    列表位置不是持久身份，旧源因此只能得到临时 legacy 键。

- static RowKey RowKeyAt(DataTableState st, DataSource src, int row)
  - 读取当前源行的稳定键。返回的 locator 仍是瞬时索引，
    业务代码应保存返回的 RowKey 而不是 row。

- static int RowIndexOfKey(DataTableState st, DataSource src, RowKey key)
  - 以稳定键查找当前源位置；找不到返回 -1。

- static List<TableDiagnostic> Diagnostics(DataTableState st)
  - 清除并返回当前结构诊断；调用方可在每次绑定或刷新后读取。

- static int DataRevision(DataTableState st)
  - 数据内容/行集/schema 的版本号；InvalidateData 递增，
    键快照以它与 rowKeysRevision 的差判断是否需要重建。

- static int QueryRevision(DataTableState st)
  - 查询参数（过滤/排序/分组/汇总/分页）的版本号；
    InvalidateQuery 与 InvalidateData 递增。

- static int ViewRevision(DataTableState st)
  - 视图几何/显示状态的版本号；三种 Invalidate* 都会递增。

- static SelectionModel Selection(DataTableState st)
  - 当前稳定键选择模型。旧 source-index selected 仍作为渲染兼容投影。

- static void SetSelectionModel(DataTableState st, SelectionModel model)
  - 安装 key selection 模型。安装时不立即假定旧 bool 的含义，
    首次 SyncSelectionModel 会把当前兼容选择投影进稳定键集合。

- static bool HasStableKey(DataSource src, int row)
  - 行是否拥有非临时、非空的稳定业务键；源为 null 或行越界
    时返回 false。

- static bool HasStableKey(DataTableState st, DataSource src, int row)
  - 带 state 的版本：优先采纳 state 级 keyProvider 的判定。

- static RowKey CurrentRowKey(DataTableState st, DataSource src, int row)
  - 行键解析的统一入口，优先级与 RebuildRowKeys 一致：
    先问 state 的 keyProvider，再问源自身的 GetRowKey，
    都没有则 legacy。此前只有 RebuildRowKeys 走这条链，
    HasStableKey/IsSelected/SetSelected 直接问源——状态级
    keyProvider（`DataTable.SetKeyProvider`）因此对选择模型
    不生效，这里收敛到一处。

- static void SyncSelectionModel(DataTableState st, DataSource src)
  - 将 stable-key 模型投影到旧 selected 数组；无稳定键的兼容源
    保留原有 bool 选择语义，不把 legacy key 误当持久身份。
    整表探测/投影只按行键快照的代次与行数失效——空闲帧是
    O(1) 空转。此前每帧全表 GetRowKey：无键源永远扫不到
    稳定键、百万行每帧 new 上百万个 legacy RowKey，空闲帧
    CPU 被打满（1M 行 ~420ms/帧）。

- static bool IsSelected(DataTableState st, DataSource src, int row)
  - 行是否处于选中状态。稳定键行走 SelectionModel；
    兼容源回退旧的 bool 数组。参数非法时返回 false。

- static void SetSelected(DataTableState st, DataSource src, int row, bool value)
  - 设置行的选中状态。稳定键行写入 SelectionModel 并把模型
    标记为权威；兼容源只写旧 bool 数组。


## DataTable (class)

DataTable 模块：多语言——UI 文案经 Lang 键式查找
（System.Globalization.Lang，Apple .strings 同构）。键用
英文原文；缺省串是内置中文（与 FilePicker/Wizard 同约定），
各语言显示经语言包整句改写；未加载语言包时显示内置中文
（Lang.code 为空时 Tr 直接返回缺省串，零查表开销）。
带数值的句子用 {n} 占位整句成键（如 "Pasted {n} cells"），
语言包整句改写。

- static string T(string key, string dflt)
  - 两参查找包装：key 是英文原文（语言包键），dflt 是内置
    中文缺省串。Lang 在 System.Globalization 命名空间。

- static string TN(string key, int n, string dflt)
  - 占位句查找：`{n}` 被替换成数值。包整句命中用译文替换，
    未命中回退缺省串替换——两种形态都返回含数值的完整句。

- static string TNPublicForTest(int n)
  - 测试专用：TN 的公开入口（ TN 本身是 internal，conformance
    需要断言 {n} 占位替换）。生产代码不要调用。


## DataTable (class)

DataTable 布局持久化：用户手工调整出的视图状态——
列宽、顺序、可见性、冻结、排序键、分组、逐列
聚合、完整筛选栈、稳定键选区与展开的详情行——
序列化为 JSON 并读回。

string s = DataTable.SaveLayout(st, cols);   // 例如存入设置文件
DataTable.LoadLayout(st, cols, s);           // 下次启动时

列以 `DataColumn.Field` 名称为键，绝不按位置，
插入、移动或重命名列后布局依然有效。
布局未提到的列保持声明默认值；布局中
不存在的名称被跳过。这让 `LoadLayout` 面对过期文件也安全——
退化为部分恢复，而非丢弃布局或
把某列宽度错赋给另一列。

未命名列（`field == ""`）无法寻址，保存和加载
都会被跳过：用 `DataColumn.Field` 命名即表示参与。

- static int LayoutVersion()
  - 布局格式版本，写入 `ver`。结构变化时递增，
    使未来读取方能迁移而非误解析；`LoadLayout` 拒绝
    不认识的版本。

- static string SaveLayout(DataTableState st, List<DataColumn> cols)
  - 把 `st` 的视图状态序列化为 JSON 字符串。只写入*用户*修改的状态：
    滚动偏移、选区、进行中的编辑和拖拽状态
    刻意排除，恢复它们会与下次会话冲突。

- static JsonValue FilterToJson(ColFilter f)
  - 序列化某列的筛选器；没有活动阶段时返回 null——
    未筛选的列不向布局贡献内容。

- static bool LoadLayout(DataTableState st, List<DataColumn> cols, string text)
  - 恢复 `SaveLayout` 写入的布局。文本不是已知版本的可用布局时
    返回 false 且不动 `st`，
    防止损坏或外来的设置文件被部分应用。
    
    可在网格首帧前安全调用：与索引对齐的逐列
    列表会先扩展以匹配 `cols`。

- static void LoadColumnState(DataTableState st, List<DataColumn> cols, JsonValue root)
  - 逐列状态：宽度 / 隐藏 / 冻结 / 聚合 / 筛选行 / 筛选器。

- static void LoadColumnOrder(DataTableState st, List<DataColumn> cols, JsonValue root)
  - 列顺序：布局列出的列按保存序列在前，
    布局未提到的列（写入后新增的）按声明位置
    在后，确保不丢列。

- static void LoadSortKeys(DataTableState st, List<DataColumn> cols, JsonValue root)
  - 排序键，sortCol/sortDir 镜像主键以兼容旧版。

- static void LoadGrouping(DataTableState st, List<DataColumn> cols, JsonValue root)
  - 分组列；折叠状态从零开始（键基于组值）。

- static void LoadCollapsedBands(DataTableState st, JsonValue root)
  - 折叠的表头 band；隐藏列在下一帧重新推导。

- static void LoadExpandedRows(DataTableState st, JsonValue root)
  - 展开的主从详情行。布局带 `expanded` 键数组：
    稳定键源写业务 canonical key（`legacy:` 前缀为旧索引
    格式），恢复时经 RowIndexOfKey 定位——行重排/插删后
    仍展开同一行。找不到的键被跳过；扁平化时仍逐个
    经 CanExpand 审核，无法强行展开 provider 拒绝的行。

- static bool ExpandedKeySeen(DataTableState st, string k)
  - `k` 是否已在展开键集合里（内容相等；List.IndexOf 是引用
    比较，对 RowKey 不可用）。

- static void LoadSelection(DataTableState st, JsonValue root)
  - 选区快照：从 `selection` 对象重建 SelectionModel。
    模型缺失或解析失败时保持当前模型不动（和布局中
    未提到的列一样退化为"无此状态"而非错误）。加载的
    模型此后权威：首次 SyncSelectionModel 不再把空的
    旧 bool 数组误导入成"没有选择"。

- static void LoadViewToggles(DataTableState st, JsonValue root)
  - 表级显示开关。

- static void FilterFromJson(ColFilter f, JsonValue o)
  - 从保存的对象恢复某列的筛选器。调用方先重置
    筛选器，因此只设置布局携带的阶段。

- static void EnsureLayoutSlots(DataTableState st, List<DataColumn> cols)
  - 扩展与索引对齐的逐列列表以覆盖所有列，
    使 LoadLayout 即使在网格自身首帧初始化之前
    也能按下标写入。与 EnsureInit 的尾部一致。


## DataTable (class)

DataTable 模块：主从明细（Master-Detail）深化——
在既有 DetailProvider 钩子（SetDetail/ExpandRow/详情带区）
之上提供一组"子表格"现成实现，让不用手绘 Canvas 的
业务也能得到 DevExpress 风格的主从网格：

- `ChildGridDetail`：详情带内渲染一张完整的
子表格（列 schema + List<DataRow> 按主行键取子行集），
自带表头、斑马纹与列宽，命中测试由父网格的
BlockHitsRect 屏蔽带区，交互零成本。
- `CardDetail`：键值卡片（label: value 两列），
适合单行摘要（订单头、客户资料）。

子行集按 `childKeyCol`（子表中指向主行的列）与
`masterKeyCol`（主表中键列）的文本相等匹配；主行键变化
时子集自动跟随，无需手动刷新。

- static void ChildRowsOf(DataSource childSrc, int childKeyCol, string masterKey, List<int> outIdx)
  - 与 `master` 主行匹配的子行索引（文本相等匹配）。
    每帧重扫，与 GridSource 的"不复制数据"哲学一致。

- static int ChildGridSlots(DataSource childSrc, int childKeyCol, string masterKey, int maxSlots)
  - 详情带内子表格需要的行槽数：表头 1 + 子行数，
    夹取到 [1, maxSlots]。maxSlots <= 0 时用 8（默认上限，
    防止一条巨型主行把整屏吃掉——超出部分业务自可翻页）。


## DataTable (class)

DataTable 模块：浮动浮层——右键行上下文菜单、
选区生成图表浮层和列表头上下文菜单。
两个右键菜单都是声明式构建 List<MenuItem> 交给通用弹出菜单
组件 OverlayPopup.RichMenu 渲染分发（图标/分隔线/分组标题/
一层悬停展开子菜单/外部点击与 Escape 关闭均由组件自理），
选中项的 action 码下一帧统一 dispatch。

- static readonly int RowActCopy=0;

- static readonly int RowActCopyHdr=1;

- static readonly int RowActPaste=2;

- static readonly int RowActClear=3;

- static readonly int RowActChart=5;

- static readonly int RowActPivotExit=6;

- static readonly int RowActPivot=7;

- static readonly int RowActInsert=8;

- static readonly int RowActDelete=9;

- static readonly int RowActUndo=10;

- static readonly int RowActSelAll=11;

- static readonly int RowActSelNone=12;

- static readonly int RowActCut=13;

- static readonly int RowActExpandGroups=14;

- static readonly int RowActContractGroups=15;

- static readonly int RowActExportCsv=16;

- static readonly int RowActExportXlsx=17;

- static readonly int RowActTranspose=18;

- static readonly int RowActPivotDrill=19;

- static readonly int RowActFind=20;

- static readonly int HdrActSortAsc=0;

- static readonly int HdrActSortDesc=1;

- static readonly int HdrActClearSort=2;

- static readonly int HdrActFitCol=3;

- static readonly int HdrActFitAll=4;

- static readonly int HdrActGroup=5;

- static readonly int HdrActUngroup=6;

- static readonly int HdrActPinLeft=7;

- static readonly int HdrActPinRight=8;

- static readonly int HdrActHide=9;

- static readonly int HdrActShowAll=10;

- static readonly int HdrActChooser=11;

- static readonly int HdrActFilter=12;

- static readonly int HdrActResetCols=13;

- static readonly int HdrActAgg0=20;

- static readonly int HdrActCfGreater=32;

- static readonly int HdrActCfLess=33;

- static readonly int HdrActCfClear=35;

- static readonly int HdrActCfBar0=40;

- static readonly int HdrActCfScale0=48;

- static readonly int HdrActCfTopN=60;

- static readonly int HdrActCfTopPct=61;

- static readonly int HdrActCfBottomN=62;

- static readonly int HdrActCfBottomPct=63;

- static void Flash(DataTableState st, string msg)
  - 在表格上显示一条短暂提示消息（toastFrames=90）。

- static void EnsureMenuIds(DataTableState st)
  - 懒领取菜单的稳定 id 块与结果信号（每状态一次）。表头菜单
    用 384：主面板 +256 起还有孙级色板（RichMenu 内部约定），
    只给 256 会与紧跟其后的行菜单块（ctx）的行 id 数值相撞，
    两个菜单同开时悬停/按压按 id 匹配会串。

- static void OpenRowMenu(DataTableState st)
  - 行右键菜单打开（右键/长按打开点调用）：置位并复位
    RichMenu 信号——开关信号由打开点与组件共同书写，
    渲染函数据此区分"刚打开"与"组件已自行关闭"。

- static void OpenHdrMenu(DataTableState st)
  - 表头列菜单打开（右键/长按打开点调用）。同上。

- static List<MenuItem> BuildRowMenuItems(DataTableState st, List<DataColumn> cols, DataSource src)
  - 构建行右键菜单的 MenuItem 列表（项集对照 AG Grid
    getContextMenuItems 默认值）：剪贴板组（复制/含表头/剪切/
    粘贴/清空）、导出子菜单（CSV/Excel，带进度与取消）、选区
    图表、透视；分组时追加展开/折叠全部分组（expandAll/
    contractAll 的显隐条件同 AG Grid——仅分组时出现）；数据源
    可增删行时追加插入行、删除行；撤销日志非空时出现撤销
    （撤销是编辑能力，不依赖数据源可增删行——固定行数的源
    一样能编辑单元格）；末尾恒有全选与清除选择。

- static void DispatchRowAction(App app, DataTableState st, List<DataColumn> cols, DataSource src, int act)
  - 执行行菜单选中的 action（由 RenderContextMenu 下一帧调用，
    避免在弹层分发阶段改动表格状态）。

- static void RenderContextMenu(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)
  - 行右键菜单：声明式构建并交给 RichMenu。菜单打开期间
    本函数每帧重入（重建同一份 items——action 码与结构稳定），
    选中结果写入 st.ctxMenuResult，下一帧在此统一执行。

- static void RenderChart(App app, int x, int y, int viewW, int viewH, DataTableState st)
  - 由选中范围构建的模态柱状图（见 BuildChartFromRange）。
    点击 [x] 按钮、面板外部或 Escape 关闭。

- static void ExportUiRect(App app, int x, int y, int viewW, int viewH, out int px, out int py, out int pw, out int ph)
  - 进度弹层卡片矩形：水平居中，垂直置于视口约 40% 高处
    （视觉重心比正中略稳）。高度由内容令牌推出，纯几何，
    conformance 直接断言。

- static void RenderExportUi(App app, int x, int y, int viewW, int viewH, DataTableState st)
  - 导出进度模态：遮罩压暗表格，卡片给出进度条、百分比与
    行数计数；运行中可取消（Escape 等效），结束给出结果与
    关闭按钮。

- static int[]CfPresetColors()
  - DevExpress/ag-Grid 风格列菜单，声明式构建给 RichMenu：
    排序（当前方向勾选）、自动列宽、分组、冻结、隐藏/列选择器、
    合计（勾选当前聚合）、筛选，以及条件格式子菜单
    （数据条/色阶/高亮规则/前后规则各带色板子菜单 + 清除规则）。
    条件格式预设色板（DevExpress 风格渐变/纯色数据条与色阶）：
    12 组渐变色 + 8 组纯色，用于色板子菜单。

- static string SwatchName(int i)
  - 预设色的显示名（蓝/浅蓝/绿/…）。

- static int MedianThreshold(App app, DataTableState st, List<DataColumn> cols, DataSource src, int col)
  - 高亮规则的默认阈值：列数值的平均值（空列回退 0）。

- static string AggLabel(int mode)
  - 当前聚合模式名（合计/平均/…），无则为空。

- static List<MenuItem> BuildAggChildren(int curAgg)
  - 合计子菜单：聚合预设，当前模式带勾选（勾选在子项、
    父项只作展开入口）。

- static List<MenuItem> BuildCfChildren(DataColumn dc, DataTableState st, int col)
  - 条件格式子菜单（两级分组，对齐 RichMenu 的两层子菜单
    契约）：高亮规则 / 前·后规则 / 数据条 / 色阶四个孙级面板，
    加清除规则直项。色板项带色块（MenuItem.SwatchItem）——
    数据条单色块，色阶双色对拼；当前生效规则所在叶子行带 ✓
    （父项只作展开入口，同合计子菜单）。
    勾选状态读"生效规则"：菜单槽位优先（用户最近通过菜单
    设置的规则，跨每帧列重建持久），槽位为 0 时看列自身的
    工厂预设。

- static void DispatchHeaderAction(App app, DataTableState st, List<DataColumn> cols, DataSource src, int col, int act)
  - 表头菜单 action 执行（RenderHeaderMenu 下一帧统一调用）。

- static void RenderHeaderMenu(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)
  - 表头列上下文菜单：声明式构建并交给 RichMenu。选中结果
    写入 st.hdrMenuResult，下一帧在此统一执行并关闭。


## DataTable (class)

- static bool IsPivotView(DataTableState st)
  - 表格当前是否处于透视视图。

- static bool SetPivot(DataTableState st, List<DataColumn> cols, DataSource src, int rowCol, int colDim, int valueCol, int agg)
  - 把整个表格切换为透视投影：行维度 `rowCol`、列维度
    `colDim`（-1 = 无第二维度，退化为按行维度的单值汇总列）、
    值 `valueCol` 按 `agg`（0 Sum，1 Average，2 Count，
    3 Max，4 Min）聚合。聚合范围是“通过当前筛选的行”
    ——进入视图前的原始逐列筛选、筛选行与组合树在物化时
    作用于原始列；之后视图层的筛选/排序再作用于交叉结果。
    视图期间强制开启总计行（列总计，语义随 agg），退出时
    恢复原状。返回 false 表示参数无效（维度/值列越界或重合）。

- static void SetPivotAgg(DataTableState st, List<DataColumn> cols, DataSource src, int agg)
  - 切换聚合模式并立即重建交叉表。

- static void ExitPivot(DataTableState st)
  - 退出透视视图：恢复进入前的筛选/分组/排序/汇总，
    裁掉生成列在按列状态里追加的槽位。

- static void PivotResetView(DataTableState st)
  - 选区/滚动复位：进入或退出视图后行列语义已变化，
    旧位置一律无效。

- static void DrillPin(DataTableState st, int col, string value)
  - 下钻钉选：把 `value` 作为批量精确匹配钉到列 `col` 的
    筛选上（清掉该列其它阶段，保证结果就这批行）。

- static void PivotDrill(DataTableState st, int r, int c)
  - 透视单元格下钻：回到未聚合的底层行集——退出视图，
    把当前格的行维度/列维度值钉成精确筛选，让恢复的
    平面视图只显示交叉格背后的原始行。`r`/`c` 是透视源
    行列索引（c = 0 是行标签列；无第二维度时只钉行维度）。
    行索引越界（总计行/空白处）时不动作。

- static bool PivotFromSelection(DataTableState st, List<DataColumn> cols, DataSource src)
  - 从当前单元格范围推导透视配置并切换视图（右键菜单
    “从选区生成透视表”的入口）：行维度 = 范围第一列，
    列维度 = 第一列之外最左的非值列，值 = 最右侧数值列。

- static void PivotEnsure(DataTableState st, List<DataColumn> cols, DataSource src)
  - 物化交叉聚合（pvDirty 时）：收集“通过当前筛选”的
    底层行，按行/列键去重保序聚合成 pvRowKeys/pvColKeys/
    pvCells，并生成视图列描述 pvViewCols。

- static List<DataColumn> PivotBuildCols(List<DataColumn> cols, DataTableState st, int colDim)
  - 生成透视视图的列描述：行维度列（克隆原列但退化为
    纯文本只读）+ 每个列键一列数值 + Sum/Max/Min 模式的
    Total 列。值列的格式化（前缀/后缀/小数位）随原值列。

- static List<DataColumn> PivotViewCols(DataTableState st)
  - 视图列描述（进入视图后由渲染管线作为 cols 使用）。

- static DataSource PivotViewSource(DataTableState st, DataSource src)
  - 渲染管线使用的数据源：包装后的交叉表。

- static void PivotFinalizeSummary(DataTableState st, List<DataColumn> cols)
  - 透视视图的总计行：由物化时的每格基础聚合（pvCellSum/
    pvCellCnt/pvCellMx/pvCellMn）精确写出，替换常规的
    逐行扫描。常规扫描只看得到交叉格本身——Average 的
    无权平均在格计数不一时是错的，Count 会数成格数；
    这里的 Σsum/Σcnt 与 Σcnt 才是原始数据的真总 aggregate。
    FinalizeSummary 在 pivotView 下整体转投到这里。

- static string PivotTotalText(DataColumn col, int agg, double v, int cnt)
  - 总计行单元格文本：数值聚合沿用该列的小数位与
    前后缀格式（平均不足两位补足两位），计数显示整数。

- static void TruncListI(List<int> l, int n)
  - 按类型把按列状态列表裁剪回 n 项（退出视图时移除
    生成列追加的槽位；进入只追加，因此裁剪即还原）。

- static void TruncListB(List<bool> l, int n)

- static void TruncListD(List<double> l, int n)

- static void TruncListS(List<string> l, int n)

- static DataColumn CloneCol(DataColumn c)
  - 逐字段克隆列描述（生成列与原列必须互不影响——
    视图里拖宽/隐藏不能写回应用的列对象）。

- static List<FilterChip> FilterChips(DataTableState st, List<DataColumn> cols)
  - 全部活动筛选条件的芯片投影：逐列 ColFilter（按列序）、
    筛选行文本、组合树；透视视图激活时在最前面加一张
    透视芯片。渲染与测试共用同一投影。

- static string ColTitle(List<DataColumn> cols, int c)
  - 列标题，空标题回退 field 名（与状态栏概览条一致）。

- static void ClearAllFilters(DataTableState st)
  - 清除全部筛选条件（逐列 + 筛选行 + 组合树；透视
    视图下连“进入前”的原始条件一起清并触发重物化）。
    透视视图本身是视图形态而非筛选条件，不在此列。

- static void RenderFilterPanel(App app, int x, int y, int viewW, int h, List<DataColumn> cols, DataSource src, DataTableState st)
  - 表头上方的筛选面板条带：每张芯片一个活动条件，
    × 清除该条件，点芯片主体直接打开对应入口——
    逐列芯片打开该列的筛选菜单，筛选行芯片聚焦输入框，
    透视芯片切换聚合。比 DevExpress 的独立筛选面板
    少一次跳转：条件与编辑器在同一行。

- static void RemoveFilterChip(DataTableState st, FilterChip chip)
  - 芯片 × 的语义：移除该条件。逐列筛选 Reset、
    筛选行清文本、组合树整体清除、透视芯片退出视图、
    原始条件 Reset 后触发重物化。

- static void OpenFilterChip(DataTableState st, List<DataColumn> cols, DataSource src, FilterChip chip)
  - 芯片主体的语义：直达编辑入口。逐列芯片打开该列的
    筛选菜单；筛选行芯片把焦点交给对应输入框；透视
    芯片循环切换 Sum → Average → Count → Max → Min。


## DataTable (class)

- static void InvalidateData(DataTableState st)
  - 数据内容、行键或 schema 改变：所有派生查询和显示结果失效。

- static void InvalidateQuery(DataTableState st)
  - 过滤、排序、分组、汇总或分页参数改变：数据身份不变，
    只使查询和依赖它的显示模型失效。透视视图的交叉
    聚合依赖筛选结果，一并标脏重建。

- static void InvalidateView(DataTableState st)
  - 列宽、顺序、隐藏、冻结或主题显示状态改变：只使几何/帧失效。


## DataTable (class)

实时增量分部：把一批 DeltaOp 按 RowKey 合并进数据源。

能力探测走虚拟 sentinel：DataSource 上的 `virtual DeltaResult
ApplyDeltas(DeltaBatch)` 默认返回 null（不支持），LocalDataSource /
ServerDataSource 重写它实现真正的合并。Zan 的 `x is Interface`
目前只做非空检查（编译期已定型），不能用于运行时能力判别，
所以不用接口探测。

- static void SetCellFieldLookup(DataSource src, CellFieldLookup lookup)
  - 为支持 cell 增量的源安装列 Field 名查找：委托把列索引解析为
    `DataColumn.Field` 稳定名，cell 操作按名寻址，与列重排无关。
    支持的源类型各自提供 SetCellFieldLookup；不支持的源静默跳过。

- static CellFieldLookup FieldLookupOf(List<DataColumn> cols)
  - 把列列表包装成 cell 增量的 Field 名查找委托：
    命名列返回其 Field，未命名/越界列返回空串（cell 视为 missing）。

- static DeltaResult ApplyDeltas(DataTableState st, List<DataColumn> cols, DataSource src, DeltaBatch batch)
  - 把一批增量应用到支持按键合并的数据源，并让表格状态
    与新数据对齐：键快照重建、已删行的选择/展开状态剔除、
    新键行的选择投影——稳定键选择与详情展开都按 RowKey
    对齐，行重排/插删不漂移。显示模型的脏标记已置位，
    下一帧 Recompute（渲染路径每帧调用）会自动重排。
    源不支持合并（sentinel null）时返回 null，表格不动。

- static DeltaResult ApplyDeltas(DataTableState st, List<DataColumn> cols, DataSource src, List<DeltaOp> ops)
  - 便捷入口：直接用 op 列表构造并应用一批，顺序即应用顺序。


## DataTable (class)

DataTable 模块：单元格渲染（条件格式、单元格类型）
与虚拟化的主渲染路径（表头 / 主体 / 页脚 /
交互）。

- static void DrawGrip(App app, int bx, int by, int bw, int bh, int color)
  - 在 bx,by,bw,bh 内居中画一个 2×3 点阵的拖动手柄（列头 /
    行号的专用拖动按钮）。用点阵而不是字体图标，
    是因为图标集里没有 gripper 码点，而点阵在小尺寸下
    也不会糊成一团。

- static int SparkZeroY(double negMin, double posMax, int plotY, int plotH)
  - 迷你图零基线：negMin >= 0（序列无负值）时就是绘图区
    底边；否则按 posMax:(-negMin) 的比例把零点放进绘图区，
    正负两段各自归一化——Excel 迷你图惯例。纯函数，
    conformance 测试直接断言其几何。

- static int SparkValueY(double v, double posMax, double negMin, int plotY, int plotH)
  - 迷你图某值对应的纵坐标：与柱状/折线渲染共用同一套
    归一化（正值向上压缩到 plotY..zeroY，负值向下压缩到
    zeroY..底边），保证两种形态对同一序列形状一致。

- static void DrawSparkBars(App app, DataColumn col, List<string> seq, int n, int plotX, int plotY, int plotW, int plotH, double posMax, double negMin, int zeroY, int spark, int sparkNeg)
  - 柱状形态：等槽位柱，正值从零基线向上、负值向下，
    历史的 sparkArea 半透明面积（正区全高）保留。

- static void DrawSparkLine(App app, DataColumn col, List<string> seq, int n, int plotX, int plotY, int plotW, int plotH, double posMax, double negMin, int zeroY, int spark, int sparkNeg)
  - 折线/面积形态：槽位中心的抗锯齿折线（DrawPolyline
    单笔扫描，拐角无亮缝）。面积形态先按 1px 竖条填充
    线与零基线之间的区域（正区正色淡染、负区负色淡染，
    相当于对折线下的形状做逐列采样）。负值游程用
    sparkNeg 覆描——与零线的交点处解析衔接，而不是
    整条换色。末点画实心圆点强调当前值。

- static readonly int MergeWalkCap=1000;
  - 合并单元格邻行文本比对的走查上限：防止整列同值的
    大表每帧 O(n) 回溯。超出封顶的运行按截断处理（罕见：
    mergeSame 面向品类列，运行通常短）。

- static void DrawCell(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride)
  - `fgOverride` 是 state 的 CellStyler 给出的打包颜色，0 表示
    无。它替换普通和链接单元格的主题文本色，但
    条件格式颜色仍然优先——在那里，颜色*就是*
    单元格携带的信息。

- static void DrawCellCtx(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride, int rowIdx, int cc)
  - 带行/列上下文的渲染入口：按钮列（cellType 7）的点击
    需要行号和列号填充 HitRow/HitCol。主渲染路径走这里；
    无上下文的调用方仍可用 DrawCell / DrawCellIndented。

- static void DrawCellIndented(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride, int indent, bool isTreeCol, bool canExpand, bool expanded)
  - 树模式单元格：`indent` 层级换成左缩进；`isTreeCol`
    的行首绘制展开箭头（叶子行留空白边距）。其余
    列照常渲染——树列之外不受影响。

- static void DrawCellRange(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride)
  - DrawCell 的主体（缩进版把 x/宽收缩后委托到这里）。
    交互列（按钮，cellType 7）需要行/列上下文来填充
    CellClick 的事件语义：`rowIdx` 是数据行号，`cc` 是列号
    （-1 表示调用方没有行上下文——如独立的单元格渲染器，
    此时按钮点击仅Raise事件，不设置 HitRow/HitCol）。

- static void DrawCellRangeCtx(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride, int rowIdx, int cc, DataTableState st)

- static void DrawCellRangeCtx(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride, int rowIdx, int cc, DataTableState st, DataSource src)
  - 带 `src` 的主入口：组件列（cellType 9）需要数据源取稳定
    行键做控件池化；其余 cellType 忽略它。

- static void DrawWrappedLines(App app, string text, int tx, int cy, int innerW, int rowH, int maxLines, int lineH, int color, int fs)
  - wrapText 的多行绘制：WrapTextLines 断行后自上而下排布，
    整块在行矩形内垂直居中。

- static List<string> WrapTextLines(string text, int maxW, int fs, int maxLines)
  - 按像素宽度把 `text` 断成至多 `maxLines` 行：逐行探测
    最长可容纳前缀（指数步长 + 回退，Text.ClampCharStart
    保证字符边界，多字节字符不切半）；行内有空格时回退到
    最后一个空格断词；仍有剩余时尾行以省略号收尾。

- static int BandLeft(int band, int firstDataX, int bandX, int rightX)

- static int BandRight(int band, int firstDataX, int bandX, int bandW, int rightX, int pinnedW, int pinnedRW, int clipRight)

- static void PushBandClip(Canvas c, int band, int firstDataX, int bandX, int bandW, int rightX, int pinnedW, int pinnedRW, int y, int h, int clipRight)

- static List<GroupChipGeometry> GroupChipLayout(App app, List<DataColumn> cols, DataTableState st, int x)
  - 分组面板芯片的 x 位置和宽度，由外到内。
    条带绘制、命中测试和表头拖放共用，三者
    几何上天然一致。

- static int GroupDropSlot(App app, List<GroupChipGeometry> chips)
  - 指针指向的插入槽位（0..count）：中点位于
    指针左侧的芯片数量。

- static int ModalEvt(App app, DataTableState st)
  - 表头上方的分组条带：每个分组列一个芯片
    （由外到内），带移除按钮；有内容拖过时显示插入指示，
    未分组时显示提示。
    列头被拖动期间 `headerDrag` 为 true，条带
    可标示自己为放置目标。
    模态浮层（行右键菜单/表头右键菜单/列选择器/条件格式清除/
    编辑器弹窗）打开期间，表格交互面归浮层所有：内容阶段的事件
    种类就地清零。表头 chrome（全选框/排序/列菜单/筛选钮/手柄）、
    分组与筛选芯片条、表格体一样都是几何命中——读的是原始事件，
    弹层的全窗阻挡拦不住几何命中；菜单比表格高时会被钳到表格
    顶部，菜单项正好盖住表头，点菜单项就点穿到全选框上，选中行
    被改成点击位置的行（2026-09 用户实测）。浮层自身的内容在
    覆盖层阶段绘制，仍读原始事件，不受此抑制影响。

- static void RenderGroupPanel(App app, int x, int y, int viewW, int h, List<DataColumn> cols, DataTableState st, bool headerDrag)

- static void Render(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, List<DataRow> rows, DataTableState st)
  - 便捷重载：渲染内存中的 List<DataRow>（包装为
    RowListSource）。大数据 / 虚拟数据请改用 RenderSource 配合自定义
    DataSource 子类。

- static DataTableFrame ComputeFrame(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)
  - 渲染前奏：把全部每帧布局几何解析到
    DataTableFrame——确保/同步/重算状态、表头与 band 度量、
    行号/选择边栏、冻结 band 宽度和各列屏幕 x，
    并绘制分组面板。之后的流程都读取 frame，
    不再重算几何。

- static void RenderSource(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)


## DataTable (class)

DataTable 分部：行生命周期——状态初始化、插入/追加/删除、
焦点与撤销日志。

- static void EnsureColumnSlots(DataTableState st, List<DataColumn> cols)
  - 补齐所有按列对齐的状态列表。列可以在网格首帧之后
    动态增加；因此不能只补 selected/hidden/freeze，
    否则筛选、宽度、聚合或条件格式会访问过短列表。

- static void EnsureInit(DataTableState st, List<DataColumn> cols, DataSource src)
  - 首帧/每次重绑定前的状态对齐：初始化选择列表、按当前列集
    补齐各状态槽位、列数变化时失效查询、作废公式缓存，并把
    键快照、详情展开键与选择模型同步到当前源。

- static void ShiftExpandedOnInsert(DataTableState st, int at)
  - 源行插入/删除会改变后续的兼容索引；在 RowKey
    完成迁移前，至少保持旧 expandedRows 的索引语义一致。

- static void ShiftExpandedOnDelete(DataTableState st, int at)
  - 源行删除后收缩后续兼容索引；正好位于 `at` 的展开详情移除。

- static void Journal(DataTableState st, RowEdit e)
  - 将可逆更改压入日志；达到上限时丢弃最旧的条目
    。撤销本身产生的写入不会被记录——
    否则撤销会立刻变成自己的重做。
    新编辑会清空 redoLog：从分支点开始，旧的重做路径作废
    （与 Excel/主流编辑器一致）。

- static void BeginUndoGroup(DataTableState st)
  - 开始一次复合更改：到 EndUndoGroup 为止记录的所有内容
    在单次撤销中一起回退。用于粘贴、范围内删除和
    删除选中行等一次操作涉及多单元格/多行的情况。

- static void EndUndoGroup(DataTableState st)
  - 结束复合更改；后续更改恢复为逐条记录撤销。

- static int UndoDepth(DataTableState st)
  - 日志还能承受的 Ctrl+Z 次数。分组条目
    只计一次，因为它们会一起回退。

- static void ClearUndo(DataTableState st)
  - 清空日志（例如保存后）。重做历史随保存快照一并作废
    ——保存后的重放可能把已持久化的状态再次改掉。

- static int InsertRow(DataTableState st, List<DataColumn> cols, DataSource src, int at)
  - 在数据索引 `at` 处插入空行并选中它。返回新行的索引，
    源拒绝时返回 -1。
    
    `st.selected` 以数据行为索引，因此插入点及其后的
    所有标记都要随之后移——否则新行会继承
    原来在该索引上的行的选中状态。

- static int AppendRow(DataTableState st, List<DataColumn> cols, DataSource src)
  - 在最后一行之后追加空行。

- static List<string> RowCells(DataSource src, List<DataColumn> cols, int row)
  - 快照一行的所有单元格，使删除在源
    忘记该行后仍可撤销。计算列取源数据
    （公式列不可写，恢复时 SetCell 对它无效，
    但快照仍保持与列数对齐）。

- static bool DeleteRow(DataTableState st, List<DataColumn> cols, DataSource src, int at)
  - 删除数据索引 `at` 处的行。成功删除时返回 true。

- static int DeleteSelectedRows(DataTableState st, List<DataColumn> cols, DataSource src)
  - 删除所有选中的行。返回删除的数量。
    
    行从后往前删除：从前删会重新编号
    尚未删除的行，导致每次删错对象。

- static int SlotOfRow(DataTableState st, int row)
  - 显示数据行 `row` 的槽位；没有时为 -1——该行
    被过滤掉，或位于折叠的分组内。数据行索引
    不是槽位索引，因此所有滚动或停靠光标的地方都必须
    经过这里。

- static void FocusRow(DataTableState st, List<DataColumn> cols, DataSource src, int row)
  - 将单元格光标停靠在 `row` 上并打开其第一个可编辑列，
    这样新添加的行无需二次点击即可使用。
    当行当前未显示时不执行任何操作。

- static bool Undo(DataTableState st, List<DataColumn> cols, DataSource src)
  - 回退最近一次记录的更改；有回退时返回 true
    。

- static bool Redo(DataTableState st, List<DataColumn> cols, DataSource src)
  - 重放最近一次被撤销的更改；有可重做内容时返回 true。

- static void RedoOne(DataTableState st, List<DataColumn> cols, DataSource src, RowEdit e)
  - 重放一条日志条目。假定 st.undoing 已设置。

- static void UndoOne(DataTableState st, List<DataColumn> cols, DataSource src, RowEdit e)
  - 回退一条日志条目。假定 st.undoing 已设置。


## DataTable (class)

- static readonly int FindScanCap=200000;
  - 查找与筛选互补：筛选丢弃不匹配的行，查找定位并高亮
    全部命中格。命中不物化——它们随排序/筛选即时变化，
    因此导航沿当前显示顺序现场扫描；每帧高亮只扫可见
    窗口（与截断 tooltip 同一成本量级），百万行源也不加重
    渲染。命中计数按查询变化后的第一帧统计一次，扫描行数
    以 FindScanCap 封顶，输入不卡顿。

- static void OpenFind(DataTableState st)
  - 打开查找条并聚焦输入框。

- static void CloseFind(DataTableState st)
  - 关闭查找条并清掉命中高亮（不改数据）。

- static bool FindableCol(DataColumn col)
  - 该列是否参与查找：文本可读的列（文本/链接/标签/进度/
    徽章/日期）。图标/按钮/迷你图/组件列的值是图标名、
    序列文本，不构成可读的查找键，跳过。

- static bool RowHasMatch(List<DataColumn> cols, DataSource src, int row, string q)
  - 数据行 `row` 的任一可查找列命中 `q`（不区分大小写）。

- static bool CellMatches(DataTableState st, List<DataColumn> cols, DataSource src, int row, int col)
  - 单元格（数据行 row、列 col）是否命中当前查询。供每帧
    的高亮绘制与导航扫描调用——一次子串比较，循环内安全。

- static void CommitFind(DataTableState st, List<DataColumn> cols, DataSource src)
  - 提交查询：把缓冲同步为生效查询，并按显示顺序统计命中
    （FindScanCap 封顶）。当前命中序号复位为未导航。

- static void FindEnsureCount(DataTableState st, List<DataColumn> cols, DataSource src)
  - 命中计数就地刷新：缓冲与生效查询不一致（正在输入）
    才重新统计，否则 O(1) 空转。由查找条绘制每帧调用。

- static void FindStep(DataTableState st, List<DataColumn> cols, DataSource src, int dir, int visRows)
  - 沿显示顺序前进（dir 1）/后退（dir -1）到下一个命中格，
    到视图尽头回绕（浏览器式）。命中格成为单元格光标并
    滚入视野。把 (显示槽位 × 可见列) 摊平成一条序列环扫：
    组表头/详情带槽位天然不命中，跳过即可。

- static void RenderFindBar(App app, int x, int y, int viewW, List<DataColumn> cols, DataSource src, DataTableState st, int visRows)
  - 查找条浮层：网格右上角的输入框 + 命中计数 + 上/下/关闭。
    绘制在上下文菜单等模态浮层之前，因此菜单永远盖住它；
    键盘输入在 RenderSource 的查找键盘块处理（与筛选行
    同一模式），这里只处理条内的指针交互。


## DataTable (class)

DataTable 分部：排序与外观——条件格式颜色
辅助、外观钩子、归并排序与多列排序次序。

- static int Rgb(int r, int g, int b)
  - 不透明 0xFFRRGGBB（alpha 高位使其作为 int 常为负）。

- static int ColorAlpha(int packed, int a)
  - 相同 RGB 但带自定义 alpha（用于半透明单元格底色/数据条）。

- static int LerpColor(int c0, int c1, int num, int den)
  - 两个不透明打包颜色的线性混合：(1-num/den)*c0 + (num/den)*c1。

- static int RowBackground(DataTableState st, DataSource src, int row, int stripeBg, int activeBg, int hoverBg, bool sel, bool hover)
  - 数据行的背景。styler 的颜色优先于斑马纹
    默认色，但选中和悬停是对它*染色*而非替换——
    各半混合让样式行在激活时仍能保持
    其含义清晰，而不是在光标下悄悄失去颜色。
    选中优先于悬停：光标扫过选中行时它仍是
    选中行——悬停只是一种"经过"，不该掩盖
    用户明确做出的选择（商业网格的通行做法）。

- static int CellBackground(DataTableState st, DataSource src, int row, int col)
  - 单个单元格的背景，0 则露出行背景。

- static int CellForeground(DataTableState st, DataSource src, int row, int col)
  - 单个单元格的文本颜色，0 用主题默认色。单元格级颜色
    覆盖行级颜色；行级颜色应用于所有未设颜色的单元格。

- static void MergeKeyed(int kind, int dir, List<int> order, List<int> nkey, List<string> skey, List<double> rkey, int lo, int mid, int hi, List<int> otmp, List<int> ntmp, List<string> stmp, List<double> rtmp)
  - `kind` 选择装饰后的键数组：0 字符串、1 int、2 实数。

- static void SyncPrimary(DataTableState st)
  - sortKeys 保存当前生效的键（外→内）。sortCol/sortDir 与
    主键保持同步，以便现有读取方继续工作。

- static int SortIndexOf(DataTableState st, int col)
  - `col` 在排序键中的索引，没有则为 -1。键现在是实体而非
    裸列索引列表，因此取代了 List.IndexOf。

- static void SortBy(DataTableState st, int col, int dir)
  - 用单个键替换排序（dir：1 升序，2 降序）。col<0 清除排序。

- static void AddSort(DataTableState st, int col, int dir)
  - 追加（或重定向）一个次要排序键，不影响其他键。

- static void ClearSort(DataTableState st)
  - 清空全部排序键并使查询失效。

- static void CycleSort(DataTableState st, int col, bool additive)
  - 表头点击状态机。`additive`（Shift）把该列作为
    附加键循环切换（升序 → 降序 → 移除）；否则它成为唯一键，
    反复点击时循环：升序 → 降序 → 清除。

- static int CmpMultiKey(DataTableState st, List<DataColumn> cols, DataSource src, int ra, int rb)
  - 按所有生效的排序键比较两行数据（稳定平局次序）。

- static void MergeMulti(DataTableState st, List<DataColumn> cols, DataSource src, int lo, int mid, int hi, List<int> tmp)
  - 按 CmpMultiKey 的次序对 order[lo..hi) 做归并排序的一趟合并。

- static void SortOrderMulti(DataTableState st, List<DataColumn> cols, DataSource src)
  - 多列排序：从最低优先级到最高优先级逐趟单列稳定排序，
    等价于按键序的字典序；少于两个键时为空操作。

- static void SortOrder(DataTableState st, List<DataColumn> cols, DataSource src)
  - 单主键（sortCol/sortDir）排序入口。

- static void SortOrderByCol(DataTableState st, List<DataColumn> cols, DataSource src, int sc, int dir)
  - 对 st.order 按单列 `sc` 做稳定装饰排序（dir：1 升序，2
    降序）。从 SortOrder 中拆出，使分组排序能用同样的
    O(n) 装饰后廉价比较机制按每个分组列做基数排序，
    而不是在每次 n*log2(n) 比较中重新获取单元格。

- static int DistinctCapacity(int n)
  - 去重哈希表容量：不小于 2n 的最小 2 的幂（下限 16）。

- static int DistinctHash(string val)
  - FNV-1a 的 30 位字符串哈希。

- static bool DistinctAdd(List<string> vals, List<int> slots, string val)
  - 线性探测插入唯一值；已存在返回 false，新插入返回 true。

- static void MergeStr(List<string> src, int lo, int mid, int hi, List<string> tmp)
  - 字符串归并排序的一趟合并（src[lo..hi)）。

- static void SortStr(List<string> a)
  - 字符串稳定归并排序（就地）。

- static List<string> Distinct(DataSource src, int col)
  - 某列在全部源行（不过滤）上的排序去重值。
    为向后兼容保留（历史用户代码在此传入 DataRow 列表）。

- static List<string> DistinctFiltered(DataTableState st, List<DataColumn> cols, DataSource src, int col)
  - 某列在当前过滤视图范围内的排序去重值
    （st.order）。这是集合筛选弹窗应显示的内容：
    当前过滤视图中存在、由 st.order 表示的值。
    对 n 个可见行和 u 个唯一值，哈希后仅对唯一值排序，
    复杂度为 O(n + u log u)。


## DataTable (class)

- static readonly int TransposeRowCap=300;
  - 转置的行数上限：显示列 = 原行，无界宽表没有可操作性
    （VTable 的转置示例同样是小表）。超过上限的行不进入
    视图，状态栏行数仍如实显示原行数。

- static bool IsTransposeView(DataTableState st)
  - 表格当前是否处于转置视图。

- static bool SetTranspose(DataTableState st, List<DataColumn> cols, DataSource src)
  - 把整个表格切换为转置投影：显示行 = 原列（首列是列
    标题），显示列 = 原（过滤前的前 TransposeRowCap）行。
    生成列只读、统一文本路径；视图层的排序/选区/剪贴板
    照常工作。与透视视图互斥——进入时先退出透视。

- static void ExitTranspose(DataTableState st)
  - 退出转置视图：裁掉生成列在按列状态里追加的槽位，
    原始列与数据恢复原状。

- static void TransposeEnsure(DataTableState st)
  - 物化转置列（trDirty 时）：生成 1 + min(行数, Cap) 列
    只读文本列。行数随底层源变化自动跟进（Poll 置脏）。

- static DataSource TransposeViewSource(DataTableState st, DataSource src)
  - 渲染管线使用的数据源：包装后的转置投影。


## DataTable (class)

DataTable 分部：值解析、数字/实数/bool/日期格式化与
日历计算——原始单元格字符串与其类型化/显示形式之间的纯静态转换。
此处不触碰任何表格状态。

- static int CompareStr(string a, string b)
  - 逐码点比较两个字符串，短者为小；返回 -1/0/1。

- static bool IsIntStr(string s)
  - 仅由可选前导 '-' 与数字组成（至少一个数字）时为 true。

- static int ParseInt(string s)
  - 宽松整数解析：可选前导 '-'，取前导十进制数字（遇非数字停止，
    尾部忽略）；空串/无数字得 0。

- static double Pow10(int n)
  - 10 的 n 次幂。

- static double ParseReal(string s)
  - 宽松的实数解析：可选的前导 '-'、十进制数字和至多一个
    '.'；千位分隔符、货币符号和单位后缀会被
    跳过，因此“¥1,234.56”和“-98%”都能正确解析。“”视为 0。

- static string PadFrac(int fp, int decimals)
  - 将小数部分左补零到 `decimals` 位（“5”在 2 位时 -> “05”）。

- static string FormatReal(double v, int decimals)
  - 将 double 渲染为恰好 `decimals` 位小数（四舍五入）。
    `Convert.ToString(double)` 只有约 6 位有效数字，因此
    网格从不用它处理单元格值。

- static string FormatSpecNum(double v, string spec)
  - 按 .NET 自定义数字格式说明渲染数值——合计行模板
    `{0:spec}` 里的 spec。支持 `0`（固定数字位，不足补零）、
    `#`（可选数字位，末尾零剪除）、`.`（小数点）、`,`（千位
    分组），如 `0.##`、`#,##0.00`、`0.0`、`00`。说明串含其他
    字符（N/C/P/E 等标准格式名、前缀文字）时返回 ""，调用方
    回退为该列的默认渲染文本。

- static string ApplySummaryTemplate(string fmt, string dflt, double num)
  - 合计行文案模板：`{0}` 替换为该列默认渲染的聚合文本，
    `{0:spec}` 按数字格式说明渲染聚合值（老 DevExpress
    DisplayFormat，如「合计{0:0.##}行:」）。无法识别的 spec
    回退为默认文本；不含占位符的模板原样显示。

- static bool ParseBool(string s)
  - 将单元格读作真值。“1”、“true”、“yes”、“y”、“on”、
    “t”和勾号视为 true；其他任何值（包括“”、“0”和“false”）
    视为 false。不区分大小写且容忍首尾空格，
    因此混用“Yes”/“TRUE”/“1”的列也能一致地读取。

- static string TrimSpace(string s)
  - 去除首尾空格和制表符。

- static string LowerAscii(string s)
  - 仅对 ASCII 转小写；多字节序列保持不变。

- static int ParseDate(string s, int dateOrder)
  - 解析日期，返回自纪元起的天数；无法识别时返回
    `DataTable.NoDate`。接受任意单个非数字
    分隔符（“2026-07-26”、“2026/7/6”、“2026.7.6”）、可选的尾部
    时间（忽略），以及 `dateOrder` 指定的字段顺序：
    0 = y-m-d，1 = d-m-y，2 = m-d-y。两位数年份映射到 2000..2069 /
    1970..1999。单独的 8 位数字读作 YYYYMMDD。

- static int NoDate()
  - “非日期”的哨兵天数：排在所有真实日期之前，
    并渲染为空单元格。

- static int DateFromYmd(int y, int m, int d)
  - 校验字段，然后转换为天数；月/日越界返回 NoDate。

- static string FormatDate(int days, int dateFmt)
  - 按 `dateFmt` 指定的格式渲染天数：
    0 = YYYY-MM-DD, 1 = DD/MM/YYYY, 2 = MM/DD/YYYY, 3 = YYYY年M月D日,
    4 = YYYY-MM（月），5 = YYYY（年），6 = “1 Jan 2026”。

- static int DateStoreFormat(DataColumn c)
  - 在该列 dateOrder 下 ParseDate 能读回的 FormatDate 格式。
    渲染格式（dateFmt）可以任意——它甚至可以
    完全去掉日——因此写入*单元格*的值必须使用
    存储时的字段顺序，否则无法经受再次解析。

- static string MonthAbbr(int m)
  - 月份的英文三字母缩写（Jan..Dec）；越界为空串。

- static string DateGroupKey(int days, int dateGroup)
  - 按 `dateGroup` 对日期单元格分组的分桶标签：
    0 = 精确到日，1 = 年，2 = 年月，3 = 季度，4 = 星期。
    按月分组会把一个月的每一天合并到一个标题下。

- static string DayName(int dow)
  - 星期几的英文名（Sunday..Saturday）；越界为空串。

- static string GroupDigits(string ipart)
  - 在一串数字中插入千位分隔符。若字符串
    含有任何非数字字符则保持原样（以免破坏非数值单元格）。

- static string FormatCell(DataColumn col, string raw)
  - 将列的显示格式应用于原始单元格值：可选的千位
    分组（整数部分）加前缀/后缀。符号和小数
    部分保持不变。fmt 关闭时原样返回原始值。

- static bool IsNumericStr(string s)
  - 接受可选的前导 '-'、十进制数字和至多一个 '.'（至少一个数字）。

- static string ValidateCell(DataColumn c, string v)
  - 按列的声明式规则校验值，返回
    拒绝信息；通过时返回“”。顺序很重要：先报空值，
    再报格式，最后报范围，让用户始终看到
    最根本的问题。

- static List<int> FilterChoices(DataColumn c, string typed)
  - 与 `typed` 匹配（不区分大小写的
    子串匹配）的下拉选项索引。空文本匹配所有项，打开编辑器时显示
    完整列表。

- static string StepValue(DataColumn c, string cur, int dir)
  - 按 `dir` 步进数字编辑器缓冲区，并夹紧到列的
    声明边界内。非数字缓冲区从下界（或 0）开始，
    确保微调器总是产生有效值。

- static int YearMonth(int y, int m)
  - 将年和月打包成日历跟踪的单个 int。

- static int YmYear(int ym)
  - 解出打包年月中的年份。

- static int YmMonth(int ym)
  - 解出打包年月中的月份（1–12）。

- static int MonthFirstDow(int ym)
  - 某月 1 号的星期几，0 = 周日。纪元第 0 天是
    星期四，因此有 +4。

- static List<int> MonthCells(int ym)
  - 月网格的 42 个单元格以日号表示，属于
    相邻月份处为 0。固定六行，网格永不回流。


## DataTable (class)

- static void EnsureWidgetComps()
  - 装配字符串值内置组件（幂等，紧随 bandgrid）。


## DataTable (class)

企业级数据网格：固定行号与选择列、可排序
表头、Excel 式集合筛选表头菜单、可调列宽、自定义单元格
渲染器（文本/链接/标签/进度/徽章）和虚拟化行（只绘制
可见窗口，数万行也能保持流畅）。

数据通过 `DataSource` 提供（List<DataRow> 便捷重载见 `Render`，
或通过 `RenderSource` 传入自定义虚拟源）。

- static int MinI(int a, int b)
  - 两者中较小的整数。

- static int MaxI(int a, int b)
  - 两者中较大的整数。

- static bool HasRange(DataTableState st)
  - 当前存在单元格范围选择时为 true。

- static bool HasSelection(DataTableState st)
  - 当前已有任意选中行（勾选框或范围选中）时为 true。

- static string CsvField(string v)
  - 转义单个 CSV 字段（含逗号、引号或换行时加引号，内嵌引号翻倍）。

- static string BuildText(DataTableState st, List<DataColumn> cols, DataSource src, bool csv, bool withHeader)
  - 为当前单元格范围构建制表符分隔（剪贴板）或逗号分隔（CSV）文本块；
    无范围则用勾选的行，
    否则用整个（过滤/排序后的）视图。`csv` 决定分隔符和转义方式；
    `withHeader` 在前面加上列标题。
    Ctrl 追加出的多区域选区按区域顺序逐块拼接（Excel 同款：
    每个区域独立成段，区域间不共享行）。

- static void SelectAllRows(DataTableState st, DataSource src, bool val)
  - 按当前显示顺序全选/反选。稳定键源走 SelectionModel
    （全选不物化 bool[N]，只置 allSelected 或排除集合），
    兼容 keyless 源保持旧 bool 行为。

- static void SelectRowSpan(DataTableState st, DataSource src, int a, int b)
  - 把 `a`..`b`（顺序位置，可反向）之间的数据行设为唯一选中集合。
    行号列上的拖动多选与 Shift 点击都走这里。

- static void PickRow(DataTableState st, DataSource src, int oi, int mods)
  - 行号列上的按行选择：Shift 连续扩展、Ctrl 增删、
    其余情况单选并把锚点落在该行。

- static int PrimarySelected(DataTableState st)
  - 第一个选中行的源行索引；无选中时为 -1。
    这是通过 `selection` 绑定暴露的标量。

- static bool AnyRowSelected(DataTableState st, DataSource src)
  - 任一行处于选中状态（勾选投影里有 true）。
    普通新手势开始前判断“是否有旧行选择要替换”。

- static void SetPrimarySelected(DataTableState st, DataSource src, int row)
  - 独占选中单个源行（清除其他所有选择）。
    row < 0 或越界会清除选择。当模型侧变化时，
    由 `selection` 绑定使用。

- static void SyncBindings(DataTableState st, DataSource src)
  - 每帧将可选的 `selection` / `page` 绑定与实时状态
    对账一次。与 Select 类似，上次同步后变化的一侧
    胜出，因此编辑双向流动：model -> table 与 table -> model。
    在 RenderSource 开头、选择列表定好大小之后调用。

- static int ParseIntLoose(string s)
  - 宽松格式数值单元格的整数部分（忽略分隔符和
    小数尾巴）：“1,234.56” -> 1234，“-98%” -> -98，“” -> 0。

- static bool BuildChartFromRange(DataTableState st, List<DataColumn> cols, DataSource src)
  - 从当前单元格范围（或多区域并集）构建条形图：条 = 范围行，
    值 = 范围内最右侧的数值列，标签 = 范围的第一
    列（若该列就是值列，则用行序号）。多区域时逐区域
    追加条目（区域间以 "｜" 前缀区分第一列的类别标签）。
    没有范围或范围内无数值列时返回 false。

- static void ClearRange(DataTableState st)
  - 清除单元格范围选择。

- static int MultiCount(DataTableState st)
  - 归档的多区域数（不含当前 range）。

- static bool HasMulti(DataTableState st)
  - 当前是否处于多区域选区（有归档区域，或 Ctrl 追加已发生）。

- static bool MultiAt(DataTableState st, int i, out int r0, out int c0, out int r1, out int c1)
  - 取归档区域 i（0 基）的边界：out 依次为 r0,c0,r1,c1
    （已归一化为 lo/hi）。越界返回 false。

- static void ArchiveRange(DataTableState st)
  - 把当前 range 归档为第 n 个区域并清空活动 range。
    用于 Ctrl+按下时：上一区域就此固化，随后按下的新区域
    继续作为 range 拖拽。

- static void ClearMulti(DataTableState st)
  - 丢弃全部归档区域，回到单矩形模式。

- static int RangeCount(DataTableState st)
  - 区域数量（多区域时为 归档+当前；单矩形时 0 或 1）。
    只统计有内容的区域；调用方以 RangeCount>1 判断"真的多区域"。

- static bool RangeAt(DataTableState st, int i, out int r0, out int c0, out int r1, out int c1)
  - 取第 i 个区域（0 基，先归档区域后当前 range）的归一化
    边界 r0,c0,r1,c1。越界返回 false。复制/汇总/清空等
    "逐区域"消费者统一走它，无感知多区域与单矩形。

- static void SyncRangeChecks(DataTableState st, DataSource src)
  - 让选择列的勾选框跟随当前单元格范围：范围跨多行
    （即一次"区域选择"）时，覆盖的行全部勾上；本手势
    早先勾上、但已随拖动移出范围的行再取消——勾选框
    实时镜像高亮的矩形，而不是等松手才对账。
    单格点击（范围只有一行）不碰勾选框：那是单元格
    选择，不是区域选择；勾选框自己的点击也不受影响。
    `st.rangeSelTouched` 只记录本手势勾过的行，
    松手或重按即清空，绝不会退掉用户此前手动勾的行。

- static void MoveOrder(DataTableState st, int from, int to)
  - 将位于顺序位置 `from` 的行移到顺序位置
    `to`，其余行顺移——即可见顺序的手动拖拽重排。

- static int NextDataSlot(DataTableState st, int from, int step)
  - `from` 及其后、承载数据行的显示槽位，按
    `step` 步进。跳过分组标题，方向键在单元格间移动
    而不会卡在标题上。走完返回 -1。

- static int FirstDataSlot(DataTableState st)
  - 第一个承载数据行的显示槽位；没有数据行时为 -1。

- static int LastDataSlot(DataTableState st)
  - 最后一个承载数据行的显示槽位；没有数据行时为 -1。

- static int VisPos(List<int> vorder, int col)
  - 可见列 `col` 在 `vorder` 中的位置；列被
    隐藏时为 -1。导航按可见顺序移动，隐藏或
    重排的列不会困住光标。

- static int VisPos(List<FrameColumn> columns, int col)
  - 可见列 `col` 在帧列列表中的位置；列不存在时为 -1。

- static bool IsNavKey(int kc)
  - kc 是否为导航键（方向键/PageUp/PageDown/Home/End/Tab）。

- static List<int> NavTarget(DataTableState st, List<int> vorder, int visRows, int kc, int emods)
  - 导航键应把光标放到哪里：两元素列表，依次为
    目标显示槽位 (0) 和列索引 (1)；网格无处可去时为 -1/-1。
    纯决策逻辑，独立于渲染路径，
    可在无界面环境下测试。
    
    行按显示槽位移动以跳过分组标题；列
    按可见顺序位置移动，隐藏和重排的列也能正常处理。

- static List<int> NavTarget(DataTableState st, List<FrameColumn> columns, int visRows, int kc, int emods)
  - NavTarget 的帧列重载（帧列自带冻结带信息）；决策语义与上一重载一致。

- static bool MoveCursor(DataTableState st, DataSource src, int oi, int col, bool extend)
  - 将单元格光标移到显示槽位 `oi`、列 `col`。`extend` 从锚点
    扩展当前范围，而不是收缩为单个单元格。
    目标不是可导航单元格时返回 false。

- static void ScrollSlotIntoView(DataTableState st, int oi, int visRows)
  - 以最小滚动量将显示槽位 `oi` 滚入视野。
    分页模式下改为选中包含它的页。

- static void ScrollColIntoView(DataTableState st, List<int> vorder, int col, int dataViewW, int pinnedW)
  - 水平平移使列 `col` 完全可见。
    
    冻结列（任一边）按定义已在屏幕上，因此对它是
    空操作。对滚动列，可见带区要减去
    两侧冻结带：仅仅滚过 `pinnedW` 仍会
    让它藏在右侧冻结带之下。

- static void ScrollColIntoView(DataTableState st, List<FrameColumn> columns, int col, int dataViewW, int pinnedW)
  - ScrollColIntoView 的帧列重载：带区归属直接取帧列的 band。

- static string CopyText(DataTableState st, List<DataColumn> cols, DataSource src)
  - 当前单元格范围（或选择）的制表符分隔文本。
    这就是 Ctrl+C 复制到剪贴板的内容。

- static int WriteCellValue(DataTableState st, List<DataColumn> cols, DataSource src, int row, int col, string v)
  - 写入一个单元格：bool 归一化、与键入相同的校验、
    记撤销日志。同值写入直接跳过（重复块填充不产生
    无意义的日志）。返回 1 表示写入了。

- static bool FillWritable(DataColumn col)
  - 该列是否接受粘贴/填充写入：可编辑、非必填
    （留空非法的列不能被批量覆写）、非公式列
    （公式列的值是算出来的，写基础值没有意义）。

- static int PasteSeg(DataTableState st, List<DataColumn> cols, DataSource src, List<string> lines, int bw, int slotR0, int slotR1, int c0, int ncols)
  - 把一个文本块按 Excel 语义平铺进槽位×列的矩形：
    行取 `lines[(slot-r0)%行数]`，列取 `fields[ci%bw]`，
    块比目标大则裁剪。写入前跳过分组行。

- static int PasteText(DataTableState st, List<DataColumn> cols, DataSource src, string text)
  - 从光标处开始向网格粘贴制表符分隔文本。
    有选区时按 Excel 语义写入整个选区：块比选区小就
    平铺（1x1 块铺满整片），块比选区大则裁剪到选区内；
    多区域（Ctrl 追加）时逐区域独立平铺。无选区时从
    锚点向下向右铺开（块按原样流动）。一次粘贴记一组
    撤销。只有可编辑且非 bool 的列接受值；bool 列通过
    ParseBool 读取粘贴文本，"Yes"/"1" 都能落库。
    返回实际写入的单元格数。

- static int FillDown(DataTableState st, List<DataColumn> cols, DataSource src)
  - Ctrl+D：把选区顶行的值向下复制到选区内其余行。
    只写 FillWritable 的列。返回写入的单元格数。

- static int FillRight(DataTableState st, List<DataColumn> cols, DataSource src)
  - Ctrl+R：把选区左列的值向右复制到选区内其余列。
    只写 FillWritable 的列。返回写入的单元格数。

- static List<int> FillSrcRows(DataTableState st, int sr0, int sr1)
  - 源矩形内的数据行索引（跳过分组行）。

- static string FillSeriesValue(DataTableState st, List<DataColumn> cols, DataSource src, List<int> srcRows, int col, int d, bool forward)
  - 级差填充：源行/列 ≥2 格且全为非空数值时按线性级差
    外推（10/20 → 30/40…），与 Excel 拖两格数字一致；
    其余（含 bool、文本）返回 null，调用方按块复制。

- static int FillApply(DataTableState st, List<DataColumn> cols, DataSource src, int sr0, int sc0, int sr1, int sc1, int tr0, int tc0, int tr1, int tc1)
  - 填充柄内核：把源矩形 [sr0..sr1]×[sc0..sc1] 的内容铺进
    相邻目标矩形（下/上/右/左之一，调用方保证单轴）。
    数值块按级差外推（FillSeriesValue），其余按块循环复制。
    一次填充一组撤销；只写 FillWritable 的列。
    返回写入的单元格数。

- static string FillSeriesValueRow(DataTableState st, List<DataColumn> cols, DataSource src, int row, int c0, int c1, int d, bool forward)
  - 行方向的级差外推：同一行内 [c0..c1] 的源值全为非空
    数值时线性外推第 d 步；否则 null。

- static int ClearRangeCells(DataTableState st, List<DataColumn> cols, DataSource src)
  - 清空当前范围内的所有可编辑单元格（Delete 键）。
    多区域（Ctrl 追加）时先归并成互不重叠的行×列段，逐段
    调用自身清空（此时 RangeCount==1，不会再次进入本分支）。
    返回清空的单元格数。

- static int ClearSeg(DataTableState st, List<DataColumn> cols, DataSource src, int r0, int r1, int c0, int c1)
  - 清空单个归一化行×列段内的可编辑单元格。
    ClearRangeCells 的共用内核：不做撤销分组，不读
    st.range*——边界全部来自参数。

- static List<string> SplitLines(string s)
  - 按 '\n' 分割并去掉 '\r'——剪贴板文本的
    换行符因复制来源而异。

- static List<string> SplitTabs(string s)
  - 按 '\t' 分割文本。

- static List<string> NumSeries(string s)
  - 迷你图数值序列：按 ',' 分割，丢弃空段
    （"1,,2" 与 "1,2" 等价）。元素交给 ParseInt/ParseReal
    宽松解析——"¥12" 之类带修饰的文本也能读出数值。


## DataTableBootstrap (class)

DataTable 家族（DataGrid/Transfer）的 HeavyControls 注册入口。
ControlFactory 主 switch 的 "DataGrid" 分支要求 Gui.Component.
DataTable 目录整体编译进图（DataGrid + 模型/渲染，0.8MB+），
不用表格的程序发布也背上这份体积。改由宿主在启动时调用一次
`Install`：设计器生成代码直接 new DataGrid<T>()/
Transfer<T>() 的程序不受影响；运行期经 JSON/HTML 按 kind 重建
（Serialize/Html 克隆、FormBuilder 的 ft 23/41/84 字段）需要注册。
不注册的程序里 DataTable 目录整体被裁掉。
图片列的远端取回（http(s) URL）是独立 opt-in：Gui.ImageHttp.
Install()（同时装配 Widget.Image 与 GridImageSlot）——不调用的
程序网络栈与 ssl/crypto 驱动不进编译图，图片列画占位。这里
绝不能拼 ImageHttp：按需拉取按名字拉文件，本文件一进图家族
就进图，装配行会把 6.4MB OpenSSL 拖回每个注册宿主。

- static bool installed;

- static void Install()

- static Control MakeDataGrid(string kind)

- static Control MakeTransfer(string kind)
  - 泛型穿梭框：无参字符串工厂只能造 string 实例供设计器/运行期
    预览；类型化的 Transfer<T> 由代码持有实体列表来绑定。


## DataTableExportProgress (class)

DataTable 导出的进度/取消钩子。回调都在后台写盘线程上发生：
OnStart 一次、OnProgress 约每 512 行一次、OnDone 一次；
Cancelled() 每行都会问一次（请保持便宜，例如读一个原子标志）。
要更新 UI 控件，在回调里用 App.Post 把工作切回 UI 线程——不要
直接碰控件。不需要进度/取消时传 null 即可（同步导出就是这么
用的）。

- virtual void OnStart(int totalRows)
  - 开始写盘。`totalRows` 为总行数（含表头行）。

- virtual void OnProgress(int doneRows, int totalRows)
  - 进度：已完成 `doneRows` / `totalRows` 行。

- virtual void OnDone(bool ok, string error)
  - 结束。`ok` 为 false 时 `error` 给出原因
    （"已取消" / "已有导出在运行" / I/O 错误消息）。

- virtual bool Cancelled()
  - 返回 true 中止导出：半成品文件被删除，随后
    OnDone(false, "已取消")。


## DataTableExportSnapshot (class)

导出行集快照：UI 线程上一次性冻结的列区间与行序。

- public List<int> rows;

- public int c0;

- public int c1;


## DataTableExportUi (class)

DataTable 导出进度弹层：ExportToXlsxAsyncUi / ExportToCsvAsyncUi
启动后台导出时在表格上叠一个居中模态卡片——进度条 + 百分比 +
行数计数，运行中可取消，结束给出结果与关闭按钮。

`DataTableExportUi` 是弹层的全部状态，挂在 <c>st.exportUi</c>，
只在 UI 线程读写。后台写盘线程不触碰它：进度回调只写
`DataTableUiExportProgress` 的静态暂存，渲染循环
每帧开头经 `DataTableUiExportProgress.SyncUi` 拉取
同步——与画廊此前手搓的静态镜像同纪律，只是收进了组件。
<c>cancel</c> 是唯一的反向通道：UI 线程写、写盘线程每行询问。

- public bool visible;

- public bool finished;

- public bool ok;

- public string message;

- public string path;

- public int done;

- public int total;

- public App app;

- public AtomicInt cancel=new AtomicInt(0);


## DataTableFrame (class)

单次 DataTable 渲染的每帧布局几何。由
`DataTable.ComputeFrame`（渲染前奏）一次性计算，供所有绘制/
命中测试/交互阶段读取，表头/筛选行/单元格/汇总/
滚动条在结构上始终保持对齐。纯瞬时数据：
帧间不持久化任何内容（那是 `DataTableState` 的职责）。

`columns` 是切成三个带区的可见列顺序
[左侧冻结 | 滚动 | 右侧冻结]。所有坐标均已
按当前 `scrollX`、分组面板条和冻结带夹取解析完毕。

- int y;

- int viewH;

- int numW;

- int selW;

- int gridColor;

- int menuIconW;

- int clipRight;

- int firstDataX;

- int dataViewW;

- int titleRowH;

- int rowH;

- int fs;

- int bandRowH;

- int bandLevels;

- int headerH;

- int titleTop;

- int pinnedW;

- int pinnedRW;

- int bandX;

- int bandW;

- int bandNatW;

- int maxScrollX;

- int rightX;

- int panelY;

- int groupPanelH;

- List<FrameColumn> columns;


## DataTableState (class)

DataTable 持久状态——创建一次（在帧循环外）并
每帧传入，使选择/排序/过滤/滚动在重绘之间得以保留。

- bool inited;

- int sortCol;

- int sortDir;

- List<SortKey> sortKeys;

- List<bool> selected;

- SelectionModel selectionModel;

- bool selectionModelAuthoritative;

- int selectionSyncRevision;

- int selectionSyncRows;

- List<int> colWidths;

- List<int> dispW;

- List<int> order;

- List<ColFilter> filters;

- int openMenu;

- int scrollRow;

- int scrollX;

- int resizeCol;

- int resizeStartX;

- int resizeStartW;

- bool dirty;

- RowKeyProvider keyProvider;

- List<RowKey> rowKeys;

- int rowKeysRevision;

- List<TableDiagnostic> diagnostics;

- int dataRevision;

- int queryRevision;

- int viewRevision;

- int columnCount;

- static int instSeq;

- int inst;
  - 本状态实例序号（构造时分配，进程内唯一）。

- int filterTab;

- string searchText;

- int valScroll;

- int editFocus;

- List<string> batchLines;

- int batchExactFlag;

- List<string> menuVals;

- List<string> menuMatched;

- string menuSearchCache;

- bool showRowNum;

- bool showSelect;

- bool striped;

- int rowH;

- bool rowReorder;

- bool showSummary;

- List<int> aggMode;

- List<int> aggValue;

- List<double> aggReal;

- List<string> summaryText;

- List<double> aggSumR;

- List<double> aggMinR;

- List<double> aggMaxR;

- List<int> cfMin;

- List<int> cfMax;

- List<double> cfMinR;

- List<double> cfMaxR;

- List<int> cfRank;

- Dict <int, int> cfOrderPos;

- List<string> formulaSeen;

- List<int> menuCfMode;

- List<int> menuCfColor;

- List<int> menuCfColor2;

- List<int> menuCfThreshold;

- List<bool> menuCfRankPct;

- List<bool> menuCfOff;

- bool cfClearOpen;

- List<int> cfClearCols;

- List<bool> cfClearSel;

- int cfClearScroll;

- bool paged;

- SignalInt pageModel;

- int pageRows;

- Binding<int> selection;
  - 绑定的主选择：`st.selection = vm.selectedRow;` 使
    选中行双向同步。绑定值是第一个选中行的
    源行索引（无选中时为 -1）；从模型侧写入
    会独占选中该行。

- Binding<int> page;
  - 绑定的当前页：`st.page = vm.page;` 使从 1 起的页码
    （pageModel）双向同步。

- int lastSelSync;

- int lastPageSync;

- int selAnchor;

- int selAnchorCol;

- int multiAnchor;

- int rangeR0;

- int rangeC0;

- int rangeR1;

- int rangeC1;

- bool rangeDrag;

- List<int> rangeSelTouched;

- List<int> multiRanges;

- bool multiActive;

- int tipOi;

- int tipCol;

- int tipSinceMs;

- int hdrTipCol;

- int hdrTipSinceMs;

- int dragRow;

- int dragStartY;

- bool dragMoved;

- bool rowGripDrag;

- bool rowSelDrag;

- bool ctxOpen;

- bool externalRowMenu;

- int ctxX;

- int ctxY;

- int ctxRow;

- int ctxOrder;

- List<bool> colHidden;

- List<int> colFreeze;

- List<int> colOrder;

- List<string> bandCollapsed;

- List<int> bandAutoHidden;

- int dragCol;

- int dragColStartX;

- bool dragColMoved;

- int dropVis;

- bool hdrCtxOpen;

- int hdrCtxCol;

- int hdrCtxX;

- int hdrCtxY;

- string toast;

- int toastFrames;

- int hdrMenuBaseId;

- SignalInt hdrMenuResult;

- SignalBool hdrMenuOpen;

- SignalInt hdrMenuSub;

- SignalInt hdrMenuSub2;

- SignalInt hdrMenuScroll;

- int ctxMenuBaseId;

- SignalInt ctxMenuResult;

- SignalBool ctxMenuOpen;

- SignalInt ctxMenuSub;

- SignalInt ctxMenuScroll;

- bool showFilterRow;

- List<string> filterRowText;

- int filterRowFocusCol;

- string filterRowBuf;

- int filterRowCur;

- bool showFilterPanel;

- bool showStatusBar;

- bool colChooserOpen;

- string colChooserSearch;

- int colChooserScroll;

- bool findOpen;

- bool findFocused;

- string findQuery;

- string findBuf;

- int findCur;

- int findIndex;

- int findCount;

- bool transposeView;

- List<DataColumn> trViewCols;

- TransposeDataSource trSource;

- DataSource trInner;

- List<DataColumn> trOrigColsList;

- int trOrigCols;

- int trRows;

- bool trDirty;

- bool chartOpen;

- string chartTitle;

- List<ChartBar> chartBars;

- int chartMax;

- DataTableExportUi exportUi;

- bool pivotView;

- int pvRowCol;

- int pvColDim;

- int pvValueCol;

- List<string> pvRowKeys;

- List<string> pvColKeys;

- List<double> pvCells;

- List<double> pvRowTot;

- List<double> pvCellSum;

- List<int> pvCellCnt;

- List<double> pvCellMx;

- List<double> pvCellMn;

- List<DataColumn> pvViewCols;

- DataSource pvInner;

- PivotDataSource pvSource;

- bool pvDirty;

- int pvOrigCols;

- List<DataColumn> pvOrigColsList;

- List<ColFilter> pvSavedFilters;

- List<string> pvSavedFilterRowText;

- List<int> pvSavedGroupCols;

- List<SortKey> pvSavedSortKeys;

- bool pvSavedSummary;

- string pvRowTitle;

- string pvColTitle;

- string pvValTitle;

- int pivotAgg;

- List<int> groupCols;

- List<string> collapsedKeys;

- List<DispRow> dispRows;

- TreeAdapter treeAdapter;

- List<bool> expandedTree;

- int treeCol;

- List<bool> treeLoading;

- FilterGroup filterTree;

- bool dispFlat;
  - 显示模型为隐式时为 true：未分组且无
    展开详情带的表格，每个有序行对应一个显示槽位，
    槽位可从 `order` 推导，`dispRows` 保持为空。
    通过 DataTable.DispCount / DispKind / DispRowOf / DispNo / DispPart 读取模型，
    而非 `dispRows`，后者仅在分组和
    详情布局时才会物化。

- List<GroupRow> groups;

- bool showGroupPanel;

- int dragChip;

- bool dragHbar;

- bool dragVbar;

- int chipDropVis;

- bool hdrDragToPanel;

- UiEvent FilterChanged;

- UiEvent HeaderClick;

- UiEvent Sort;

- UiEvent RowClick;

- UiEvent RowContext;

- UiEvent RowDoubleClick;

- UiEvent CellClick;

- UiEvent CellEdit;

- UiEvent CellValidating;

- UiEvent SelectionChanged;

- UiEvent ColumnResize;

- UiEvent ColumnResizeEnd;

- UiEvent ScrollChanged;

- UiEvent GroupChanged;

- UiEvent ColumnReorder;

- UiEvent RowInserted;

- UiEvent RowDeleted;

- UiEvent BandToggled;

- int hitRow;

- int hitOrder;

- int hitCol;

- int editRow;

- int editCol;

- string editVal;

- bool editing;

- string editBuf;

- int editCur;

- string editError;

- bool editRejected;

- bool edPopOpen;

- int edPopSel;

- int edPopScroll;

- List<int> edPopItems;

- int edCalMonth;

- bool edPopHot;

- CellStyler styler;

- CellWidgetProvider cellWidgets;

- int compR;

- int compC;

- DetailProvider detail;

- List<int> expandedRows;
  - 兼容的源索引列表；新 API 以 expandedKeys 为权威身份。

- List<RowKey> expandedKeys;

- bool expandedByKey;

- List<RowEdit> undoLog;

- List<RowEdit> redoLog;

- int undoLimit;

- bool allowAddRow;

- bool undoing;

- int lastAddedRow;

- int undoGroup;

- int undoGroupSeq;

- bool fillDrag;

- int fillHX;

- int fillHY;

- int fillSR0;

- int fillSC0;

- int fillSR1;

- int fillSC1;

- int fillTR0;

- int fillTC0;

- int fillTR1;

- int fillTC1;

- string emptyText;

- string loadingText;

- string overlayKind;

- DataTableState()

- int HitRow()

- bool ExportUiOpen()
  - 导出进度弹层是否显示中（模态：表体按下/滚轮/按键等
    事件路径据此抑制，弹层由 RenderExportUi 最后绘制并
    独占处理自己的按钮）。

- int HitOrderIndex()

- int CtxRow()

- int CtxOrder()

- int CtxX()

- int CtxY()

- int HitCol()

- int SortColIndex()

- int SortDirection()

- int ScrollTop()

- int EditRow()

- int EditCol()

- string EditValue()

- List<int> SelectedRowIndices()
  - 返回当前全部选中行的源行索引，顺序与数据源索引一致。
    业务侧可在 RowContext 处理器中读取多选对象。

- void Reject(string msg)
  - 拒绝正在校验的值。在 `CellValidating` 处理器中调用：
    编辑器保持打开并在下方显示 `msg`，不会有写入
    到达数据源。

- string EditError()
  - 当前的拒绝信息（上次提交被接受时为“”）。


## DataTableUiExportProgress (class)

内置进度实现：后台回调只写静态暂存，UI 线程每帧拉取。
进程内同一时刻至多一个导出（Export.zan 的 sExportBusy 原子门），
静态槽因此安全。

- public static DataTableExportUi ui;
  - 当前弹层状态（包装入口 Bind；Cancelled 在后台线程读取）。

- static int sDone;

- static int sTotal;

- static bool sOk;

- static string sMsg;

- static bool sFinished;

- public static void Bind(DataTableExportUi target)
  - 包装入口在启动导出前登记状态对象并复位暂存。

- public override void OnStart(int totalRows)

- public override void OnProgress(int doneRows, int totalRows)

- public override void OnDone(bool ok, string error)

- public override bool Cancelled()

- public static void SyncUi()
  - UI 线程：渲染弹层前把后台暂存拉取进弹层状态。完成态只在
    首次到达时提交（message/ok 不被后续帧改写）。


## DataTableXlsx (class)

xlsx 写盘核心与带弹层的导出入口（原 DataTable partial 的
XLSX 支路，见文件头）。

- static void InstallExportUi()
  - 装配 xlsx 弹层导出口（把本类的 ExportToXlsxAsyncUi 挂进
    DataTable.sXlsxExportUi 槽）。需要 Excel 导出的宿主（或测试）
    调一次本方法——按需拉取按名计数，这一拼就把本文件连同
    XlsxBook/Zip/Deflate 写盘家族拉进图；不调用的程序整支裁掉。

- static bool ExportToXlsxAsyncUi(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly)
  - 异步导出 .xlsx 并显示居中进度弹层（含取消/关闭）。行选择
    与快照语义与 `DataTable.ExportToCsvAsync` 完全
    一致。返回 false（无可导内容或已有导出在运行）时弹层仍会
    显示失败原因，点关闭收起。弹层本体（DataTableExportUi/
    Progress/SyncUi）在 DataTable.ExportUi.zan——本文件被拉入
    的前提是有人真的点了 Excel 导出（Overlays 菜单项先经
    DataTable.sXlsxExportUi 槽查装配），那时弹层文件必然在图。

- static bool ExportToXlsxAsync(DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly, DataTableExportProgress progress)
  - 异步导出为 .xlsx：立即返回 true 表示任务已启动，写盘在后台
    线程上流式进行。行集/列区间在本调用内（当前线程）冻结成
    快照——之后表格的排序、过滤、勾选变化不影响本次导出；数据
    源内容本身请保持只读。进度/取消经 `progress` 回调（在后台
    线程上发生，UI 更新用 App.Post 切回），结束时必有一次
    OnDone。返回 false：已有导出在运行（并回调
    OnDone(false, "已有导出在运行")）或无可导内容（不回调）。

- static bool ExportXlsxSnapshot(DataTableExportSnapshot snap, List<DataColumn> cols, DataSource src, string path, DataTableExportProgress progress)
  - xlsx 写盘核心（同步与后台线程共用）。progress 为 null 表示
    不需要进度/取消。返回 false = 取消或写盘失败。

- static XlsxCell ExportCell(int ci, List<DataColumn> cols, DataColumn col, DataSource src, int row)
  - 单元格 → xlsx 值：按列声明的类型键取数（与排序/汇总走的
    是同一条路径，导出与表格显示不会各说各话）。
    空单元格返回 null（写出时跳过）。计算列导出公式实数。


## DeltaBatch (class)

一批实时增量。批是应用/传输的最小单元：同批内后面的 op
可以覆盖前面的（同键 upsert 两次 = 一次最终态），应用方
只在整批成功后触发一次刷新。

- static const int FormatVersion=1;

- int version;

- List<DeltaOp> ops;

- DeltaBatch()
  - 构造空批，版本固定为 FormatVersion。

- static DeltaBatch Create()
  - 创建空批。

- void Add(DeltaOp op)
  - 追加一个 op 的瘦身拷贝；null 忽略。

- int Count()
  - 批内 op 数。

- DeltaOp At(int i)
  - 第 i 个 op。

- int Version()
  - 批格式版本。

- void Compress()
  - 同键去重合并：保留每个 (键, 种类, cell 的列) 组合中最后一次
    出现的 op。批次典型规模是几十条，O(n²) 保序线性扫描足够。

- JsonValue ToJsonValue()
  - 批的 JSON 形式（ver/ops 数组）。

- string ToJson()
  - ToJsonValue 的序列化文本。

- static DeltaBatch FromJson(string text)
  - 解析一批；文本为空、结构不是对象、版本未知或任何一条 op
    非法时返回 null——部分应用的批次会造成幽灵行，宁缺毋滥。


## DeltaOp (class)

一条实时增量操作。`kind`：0 upsert（按键整行写入或更新）、
1 remove（按键移除）、2 cell（按键修订单列的值）。

所有操作都以稳定 RowKey 的 canonical 文本为行身份——没有键的行
无法参与实时合并，调用方必须先为源配置键（KeyProvider 或源的
GetRowKey）。upsert 的整行值放在 `cells`（与列列表按索引对齐）；
cell 操作只写 `field`（列的稳定 Field 名）和 `value`。

- static const int Upsert=0;
  - 0：按键整行写入或更新。

- static const int Remove=1;
  - 1：按键移除。

- static const int Cell=2;
  - 2：按键修订单列。

- int kind;

- string key;

- int revision;

- List<string> cells;

- string field;

- string value;

- DeltaOp(int opKind, string rowKey, int rev)
  - 内部构造：cells 为空列表，field/value 为空串，key 为 null 记空串。

- static DeltaOp UpsertRow(string rowKey, List<string> values, int rev)
  - 构造按键整行写入（已存在的键被覆盖，缺失的键追加）。

- static DeltaOp RemoveRow(string rowKey, int rev)
  - 构造按键移除。键不存在时为幂等空操作。

- static DeltaOp SetCell(string rowKey, string fieldName, string cellValue, int rev)
  - 构造按键修订单列。列用稳定 Field 名寻址，
    与列的重排/插入无关。

- int Kind()
  - 操作种类（Upsert/Remove/Cell）。

- string Key()
  - 行身份的 canonical 文本。

- int Revision()
  - 构造时携带的行 revision。

- string Field()
  - cell 操作的列 Field 名；其他操作为空串。

- string Value()
  - cell 操作的新值；其他操作为空串。

- List<string> Cells()
  - upsert 的整行值（与列索引对齐）；其他操作为空列表。

- DeltaOp Copy()
  - 一个 op 的瘦身拷贝：加入批后，调用方对原 cells 列表的
    后续修改不会影响批内已保存的值。

- JsonValue ToJsonValue()
  - op 的 JSON 形式：op/key/rev 恒有，upsert 带 cells 数组，
    cell 带 field/value。

- static DeltaOp FromJsonValue(JsonValue node)
  - 解析单条 op；结构不完整或 kind 未知时返回 null。


## DeltaResult (class)

批应用结果的计数快照。调用方据此更新状态栏或写日志；
同键 upsert 的"更新"与"追加"在 `updated`/`inserted` 中区分。

- int inserted;

- int updated;

- int removed;

- int cells;

- int missing;

- static DeltaResult Create()
  - 构造全零计数的结果。

- int Inserted()
  - 追加的新行数。

- int Updated()
  - 按键覆盖的既有行数。

- int Removed()
  - 移除的行数。

- int Cells()
  - 应用的单列修订数。

- int Missing()
  - 键存在但操作未生效的条数（例如 cell 指向未知 Field）。


## DetailProvider (class)

主从详情钩子：提供展开行下方绘制的面板。

继承它，重写 `Paint`（可选 `Slots` / `CanExpand`），
并用 `DataTable.SetDetail(st, provider)` 交给网格。行随即在行号栏
长出展开箭头；点击后在行下方打开一个
供详情面板绘制的带区——子表格、表单、图表。

详情带高度为整数个普通行槽位（`Slots`），
这使表格体的滚动和命中测试运算保持一致；见 `DispRow`。

- virtual int Slots(DataSource src, int row)
  - `row` 的详情带高度，以普通行高为单位。
    网格会将其夹取到至少 1。可按行重写（例如根据
    子记录数量）。

- virtual bool CanExpand(DataSource src, int row)
  - `row` 是否可以展开。无可展示内容的行返回 false，
    它便显示空白边距而不是无用的箭头。

- virtual void Paint(App app, DataSource src, int row, int x, int y, int w, int h)
  - 将 `row` 的详情面板绘制到 [x,y,w,h]。画布裁剪到
    该矩形内，面板不会溢出到相邻行。


## DispRow (class)

扁平显示模型中的一条：`kind` 0 = 数据行，1 = 分组
标题，2 = 详情带（展开的数据行下方的主从详情面板）；
`row` = 数据行索引（kind 0 和 2）或 GroupRow 索引（kind 1）；
`no` = 数据行从 1 起的数据序号，其余为 0。取代旧的
按索引对齐的 dispKind/dispRow/dispNo 并行列表。

详情带占据 `DataTable.DetailSlots(st)` 个*连续*的 kind-2
条目，全部指向同一个主行。让详情带保持整数个
行槽位，表格体才能继续用
`bodyTop + slot * rowH` 寻址行——真正可变的行高会迫使
所有滚动、命中测试和拖拽计算依赖累计高度表。`part`
说明这是详情带的第几个槽位（0 = 第一个），渲染器可在
其第一个槽位绘制一次面板并跳过其余槽位。

- int kind;

- int row;

- int no;

- int part;
  - 此槽位在多槽位详情带中的序号（其他
    kind 时为 0）。仅当 kind == 2 时有意义。

- int depth;
  - 树模式下该数据行的嵌套深度（根 = 0，其他
    kind 时为 0）。渲染把它换成树列的缩进。

- DispRow(int k, int r, int n)


## ExportJob (class)

后台导出任务。Zan 的委托不能捕获局部变量或绑定 this，线程
入口必须是静态方法——任务本体放在 `DataTable`
的静态槽里（进程内同一时刻至多一个导出），Worker 从槽里取。
与 DownloadJob 同一范式。

- public int kind;
  - 1 = xlsx，2 = csv。

- public DataTableExportSnapshot snap;

- public List<DataColumn> cols;

- public DataSource src;

- public string path;

- public DataTableExportProgress progress;

- static void Worker()
  - 线程入口：从静态槽取任务按 kind 分派写盘，结束后清槽并
    释放忙标志。异常经 OnDone(false, 异常消息) 上报。
    注意：这里绝不能拼 DataTableXlsx/Xlsx* 类型——本文件随
    partial 家族进每个放表格的程序，一拼就把 XlsxBook/Zip/
    Deflate 写盘家族拖回去（A332）。xlsx 分派经 vtable 化的
    委托槽走：DataTableXlsx.ExportToXlsxAsync 启动任务时把
    写盘核心装进 sXlsxCore，未装 = xlsx 支路不在图，走 null
    报错路径（调用方是 ExportToXlsx*，自身即证明已拉入）。


## FilterChip (class)

一张筛选条件芯片。kind：0 逐列 ColFilter，1 筛选行
文本，2 组合筛选树，3 透视视图（常驻首张），4 透视
视图下“进入前”的原始逐列筛选（已烘进聚合，× 触发
重物化）。`col` 仅对 0/1/4 有意义。纯数据投影，
测试可直接断言。

- int kind;

- int col;

- string text;

- FilterChip(int chipKind, int chipCol, string chipText)

- static FilterChip Make(int kind, int col, string text)

- int Kind()
  - 芯片类别（0 逐列 / 1 筛选行 / 2 组合树 / 3 透视 / 4 原始逐列）。

- int Col()
  - 所属列索引（kind 0/1/4），其余 -1。

- string Text()
  - 显示文本。


## FilterCondition (class)

程序化组合筛选（Filter Builder）。与每列独立的 ColFilter
弹层筛选正交：FilterGroup 是一棵 AND/OR 嵌套树，挂到
state 上后 Recompute 在逐列筛选之外整体求值——同组条件
按组连接词合并，子组作为单个操作数递归参与。

典型用法：
```zan
FilterGroup root = FilterGroup.And();
FilterGroup or = FilterGroup.Or();
or.Conditions().Add(FilterCondition.Greater("price", "100"));
or.Conditions().Add(FilterCondition.Equals("grade", "A"));
root.Groups().Add(or);
root.Conditions().Add(FilterCondition.Equals("region", "EU"));
DataTable.SetFilterTree(st, root);
```

条件按列 field 名寻址（列重排/插删后仍指向同一业务列）；
未注册 field 的条件视为不匹配（该行被拒），防止拼写错误
静默放行全部数据。

- int op;
  - 条件运算：0 Contains，1 Equals（不区分大小写），
    2 NotEquals，3 Greater，4 GreaterOrEqual，5 Less，
    6 LessOrEqual，7 Between（含端点），8 IsEmpty，
    9 IsNotEmpty，10 StartsWith，11 EndsWith

- string field;

- string valueA;

- string valueB;

- FilterCondition(int conditionOp, string fieldName, string a, string b)
  - 内部构造；请使用各具名工厂（Contains/Equals/…）。

- static FilterCondition Of(int op, string field, string a, string b)
  - 通用构造；null 比较值一律按空文本处理。

- static FilterCondition Contains(string field, string value)
  - 文本包含（不区分大小写）。

- static FilterCondition Equals(string field, string value)
  - 相等（不区分大小写）。

- static FilterCondition NotEquals(string field, string value)
  - 不相等（不区分大小写）。

- static FilterCondition Greater(string field, string value)
  - 数值大于（非法数值按 0 处理）。

- static FilterCondition GreaterOrEqual(string field, string value)
  - 数值大于等于（非法数值按 0 处理）。

- static FilterCondition Less(string field, string value)
  - 数值小于（非法数值按 0 处理）。

- static FilterCondition LessOrEqual(string field, string value)
  - 数值小于等于（非法数值按 0 处理）。

- static FilterCondition Between(string field, string lo, string hi)
  - 数值区间，含两端。

- static FilterCondition IsEmpty(string field)
  - 文本为空（Trim 后）。

- static FilterCondition IsNotEmpty(string field)
  - 文本非空（Trim 后）。

- static FilterCondition StartsWith(string field, string value)
  - 文本前缀匹配（不区分大小写）。

- static FilterCondition EndsWith(string field, string value)
  - 文本后缀匹配（不区分大小写）。

- int Op()
  - 条件运算编号（0–11，见类头注释）。

- string Field()
  - 目标列的 field 名；未注册 field 的条件恒不匹配。

- string ValueA()
  - 第一个比较值；语义随条件运算而定。

- string ValueB()
  - 第二个比较值；仅 Between（op 7）使用。


## FilterGroup (class)

AND/OR 组：`and` 为 true 时子条件与子组全部通过才通过，
为 false 时任一通过即通过。空组恒通过（AND 的单位元），
因此空 AND 组外层不会误杀全部行。

- bool and;

- List<FilterCondition> conditions;

- List<FilterGroup> groups;

- FilterGroup(bool isAnd)
  - 内部构造；使用 And()/Or() 创建。

- static FilterGroup And()
  - 全部条件与子组取交集。

- static FilterGroup Or()
  - 任一条件或子组通过即通过。

- bool IsAnd()
  - 组连接词：true = AND，false = OR。

- List<FilterCondition> Conditions()
  - 本组的直接条件列表，按声明顺序求值。

- List<FilterGroup> Groups()
  - 子组列表，每个子组作为单个操作数递归求值。


## FormulaCacheEntry (class)

表达式编译缓存的载体：一组列的编译结果。

- public List<DataColumn> cols;

- public List <DataTable.FormulaExpr> exprs;


## FrameColumn (class)

当前渲染中一个可见列的几何信息。`column` 是
DataColumn 索引；`band` 为 0 左侧冻结、1 滚动或 2 右侧冻结。

- int column;

- int x;

- int band;

- FrameColumn(int c, int xx, int b)


## GridColumn (class)

`DataGrid<T>` 的一列：引擎可理解的复用 `DataColumn` 描述符，
配以直接读取该列绑定 `T` 值的访问器，
使网格绑定到真实的
领域对象（`List<User>`），而非按位置排列的字符串单元格。

配置方法镜像 `DataColumn` 的（Money / Percent / Sortable / …），
原地修改描述符，因此可以流畅地链式组合：

```zan
grid.RealCol("Amount", 110, 2, o => o.amount).Money("¥").Sum();
```

- DataColumn desc;
  - DataTable 引擎读取的描述符（标题、宽度、类型、格式、
    汇总、编辑、冻结、band …）。与网格每帧交给引擎的列表
    按引用共享。

- GridText<T> text;
  - 显示文本。为 null 时网格从列持有的类型化
    访问器推导，因此数值 / bool 列无需单独的
    文本读取器。

- GridInt<T> num;
  - 整数排序/聚合键（数值列）。为 null 时回退为解析
    显示文本。

- GridNums<T> nums;
  - 数值序列键（组件列）。null 时组件列无数据。

- GridReal<T> real;
  - 小数排序/聚合键（decimals > 0）。为 null 时回退为解析
    显示文本。

- GridBool<T> truth;
  - 布尔键（bool 列）。回退为解析显示文本。

- GridSet<T> setter;
  - 就地编辑的回写。null 表示该列只读显示。

- GridColumn(DataColumn d)

- GridColumn<T> Editable(GridSet<T> w)
  - 使该列可编辑，并把提交的值通过 `w` 写回
    实体。

- GridColumn<T> Field(string name)

- GridColumn<T> Right()

- GridColumn<T> Center()

- GridColumn<T> NotSortable()

- GridColumn<T> NotResizable()

- GridColumn<T> Money(string symbol)

- GridColumn<T> Percent()

- GridColumn<T> Grouped()

- GridColumn<T> Affix(string prefix, string suffix)

- GridColumn<T> Pin()

- GridColumn<T> PinRight()

- GridColumn<T> Band(string path)

- GridColumn<T> Sum()

- GridColumn<T> Avg()

- GridColumn<T> Min()

- GridColumn<T> Max()

- GridColumn<T> Count()

- GridColumn<T> Options(string csv)
  - 组件列（cellType 10 select/buttons / 下拉编辑器）选项，逗号分隔。

- GridColumn<T> Shape(int rows, int cols)
  - 组件列矩阵形状（cellType 9）/ rate 显数（cellType 10）。

- string Raw(T row)
  - 引擎用于显示、排序
    键和导出的原始（未格式化）单元格字符串。引擎在此基础上应用列的显示格式
    （千分位 / 前缀 / 后缀 / 日期 / bool），因此原始
    值保持可解析。未提供文本读取器时，则
    从类型化访问器推导。


## GridImageSlot (class)

- static GridUrlEnsureFn ensurer;
  - 注册的取回实现（ImageHttp.EnsureUrl：排队-去重-mem: key 缓存）。

- static void SetEnsurer(GridUrlEnsureFn fn)

- static string Ensure(App app, string url)
  - 就绪返回已注册的 `mem:` key，加载中/失败/未注册返回 null——
    null 直接落渲染的占位分支，与未就绪同形。


## GridSource (class)

将 `List<T>` 及各列访问器适配为引擎的 `DataSource`，
使强大的 DataTable 引擎（排序 / 筛选 / 分组 / 聚合 / 编辑 /
虚拟化）能原样作用于调用方自己的对象。不复制任何数据：
每帧都从活动列表读取单元格。

- List<T> data;

- List <GridColumn<T>> cols;

- GridNew<T> factory;
  - 空白行实体工厂；null 使数据源保持只读。

- GridKey<T> key;
  - 领域实体的稳定键读取器；null 时回退到临时位置键。

- override int RowCount()

- bool Row(int row)
  - 行号在当前这份数据里有效。引擎按上一次重算出来的行号集合
    （`st.order`）取单元格，绑定的列表在那之后缩短过的话行号就会越界；
    读单元格是每帧的事，越界一次就是整个程序退出，所以这里按列一样挡住。

- override string CellText(int row, int col)

- override RowKey GetRowKey(int row)

- override int CellNum(int row, int col)

- override double CellReal(int row, int col)

- override bool CellBool(int row, int col)

- override void SetCell(int row, int col, string v)

- override bool CanInsert()
  - 含回写访问器的列使网格可编辑；
    没有此类列的源保持只读。行插入/删除由
    `DataGrid.RowFactory` 开启，它提供泛型前端无法
    合成的空白实体构造函数。

- override bool InsertRow(int at)

- override bool RemoveRow(int at)


## GroupChipGeometry (class)

当前渲染中一个分组面板 chip 的几何信息。

- int x;

- int width;

- GroupChipGeometry(int xx, int w)


## GroupRow (class)

分组表格中的一行分组标题：标题文本、嵌套层级、所含
数据行数、完整路径键（折叠查找）和折叠快照。
取代旧的按索引对齐的 grpLabel/grpLevel/grpCount/grpKey/grpCollapsed
并行列表。

`aggValues` / `aggTexts` 是该组自身成员行的逐列小计
（与表格列列表按索引对齐），在
BuildDisplay 中一次性计算，让分组标题能显示 DevExpress 风格的分组汇总
（例如 "Category: Electronics (42)  Revenue: \u00a51,234,567"）而无需额外
每帧开销。

- string label;

- int level;

- int count;

- string key;

- bool collapsed;

- List<int> aggValues;

- List<string> aggTexts;

- List<int> members;
  - 最内层（叶子）分组的成员源行（st.order 的连续片段），
    BuildDisplay 关组时填充。最外层组不物化成员（每行
    只会记一次，嵌套层级不重复占内存）；组表头三态
    复选框对父组按 key 前缀收集后代叶子组的成员。

- GroupRow(string lb, int lv, int cnt, string k, bool coll)


## HttpTableSource (class)

HTTP 块源：ServerDataSource 的"限流接口 + 本地缓存"适配器。

面向把慢/限流的远端列表接口接进 DataTable 的工具型应用（列表先出、
行块懒加载）：继承 ServerDataSource 的块合同，只补两件事——

1. **缓存先行**：每个块 (start,count) 对应缓存目录里的一个 JSON
文件。命中直接在 UI 线程同步供给（零网络、零等待）；未命中才
入队后台拉取，取回后先落盘再交付——进程重启后同一块不再打网。
2. **后台交付**：拉取走全局单工的静态 Job 通道（线程入口用
静态方法组 + 静态槽位，而不是给每条取数连源一起捕获；实例
方法组与捕获 lambda 自 2026-09 起已能直接喂给 Thread.Start，
见 docs/ABI.md §3.6）。
void 循环里 await 静态 async 取数（try 在 async 体内，TASKS
A88 悬挂教训），完成写静态槽位，由网格每帧的 Poll() 在 UI
线程并入。源状态除经 Mutex 保护的槽位外不出 UI 线程，迟到/
过期结果被 nonce 丢弃。

线协议：`GET <basePath>?start=<s>&count=<c>`，响应 JSON：

{ "total": 123, "rows": [["c0","c1"], ["c0","c1"]] }

total 可省略（省略时短块固定总数）。单元格一律字符串，与
DataRow/CellText 的文本通道一致。缓存页即该响应原文。

v1 有意不做：磁盘缓存的主动失效（删除缓存目录即可）、每源并发
拉取（全局单工 + 基类在途合并兜底）、写穿透（SubmitTransaction
事务链路的远端回写需服务端配合，另案）。

- string host;

- int port;

- string basePath;

- string cacheDir;

- bool useTls;

- string token;

- string lastError;

- int fetchNonce;

- int activeNonce;

- int pendStart;

- int pendCount;

- string pendPath;

- static nint lockHandle;

- static List<HttpTableSource> queue;

- static bool workerUp;

- static HttpTableSource doneSrc;

- static int doneNonce;

- static string doneErr;

- static string doneBody;

- static HttpTableSource Create(string host, int port, string basePath, string cacheDir)
  - 创建普通（明文）HTTP 块源。cacheDir 是块缓存目录，须已存在。

- static HttpTableSource CreateTls(string host, int port, string basePath, string cacheDir)
  - 创建 HTTPS 块源（证书校验遵循 TlsStream 既有策略）。

- void SetToken(string t)
  - 可选的鉴权 token：以 `&token=` 查询参数随块请求发送（v1 不做
    请求头注入——SendBytesAsync 尚无头扩展点）。

- string LastError()
  - 最近一次网络失败的说明；成功后清空。缓存命中不改动它。

- string CachePath(int start, int count)
  - 块 (start,count) 的缓存文件路径（键=主机端口路径的净化串，
    无哈希折叠——路径可读可手工排查）。

- override void FetchBlock(int start, int count)
  - 网格请求块：缓存命中同步供给；未命中入队后台拉取（本方法立即
    返回，不碰 FinishBlock——交付在 Poll 里完成）。

- static void WorkerEntry()
  - worker 线程入口：队列空即退出，下一次 FetchBlock 重新拉起。
    void 循环里 await 静态 async 取数；入口本身不带 try（TASKS A88）。

- static async int FetchOne(HttpTableSource src)
  - 取数一个块：传输里任何一处抛出都必须变成一条错误结果——不接住
    的话 worker 静静死掉，网格永远停在加载占位上（ImageHttp 同戒）。
    落盘是尽力而为：写失败不吞数据，正文照常经槽位交付。

- override bool Poll()
  - 每帧并入：先消费完成槽（正文交付 / 失败块），再交给基类。

- void ServeFile(string path)
  - 从缓存页文件读出块并交付（UI 线程）。

- bool ParsePage(string body, List<DataRow> dst)
  - 解析线协议页：{"total":n,"rows":[[..],..]}。行一律 Push 成
    字符串单元格；total >= 0 时固定总数。坏页返回 false。


## LocalDataSource (class)

本地内存查询源：持有行快照，按 QueryViewport 同步交付一页，
支持筛选/排序/分组/聚合/分页/编辑/插入/删除全量能力。

- List<DataRow> rows;

- RowKeyProvider keyProvider;

- CellFieldLookup cellFieldOf;
  - 可选的列 Field 名查找委托（应用 cell 增量时按名寻址）。
    由 DataTable.EnsureInit 在绑定列列表时安装；
    null 时 cell 增量按"列索引即 Field 数字"解释。

- LocalDataSource(List<DataRow> values)
  - 以行快照构造本地源；null 行归一化为空 DataRow。

- override QueryCapabilities Capabilities()
  - 覆写：本地内存源宣称支持筛选/排序/分组/聚合/分页/稳定键/
    编辑/插入/删除全量能力。

- override bool ServerControlled()
  - 查询门面基类默认宣称行序由"服务器"掌控，但网格并不自动
    把排序/筛选下推为 QueryDescriptor——不驱动 RequestQuery 时
    本地源按 RowCount/CellText 交出的是全量内存数据，行序即
    内存快照的原始次序。本地排序/筛选/分组必须照常生效，
    否则表头排序与列筛选对全内存数据静默失效。

- int ColumnCount()
  - 列数取 schema 宽度与各行宽度的最大值。

- void ReplaceRows(List<DataRow> values)
  - 深复制替换内存行，并 Refresh 使页缓存与查询失效。

- void SetKeyProvider(RowKeyProvider provider)
  - 安装行键提供者并 Refresh；null 恢复 legacy 位置键。

- void SetCellFieldLookup(CellFieldLookup lookup)
  - 安装列 Field 名查找（cell 增量按稳定名寻址）。

- List<DataRow> Snapshot()
  - 全部内存行的深拷贝列表。

- override int RowCount()
  - 当前结果 total 已知时为过滤后的总行数，否则为内存行数。

- RowKey RawKey(int row)
  - 第 row 行的原始行键：keyProvider 优先，否则 legacy 位置键；越界返回 null。

- int SourceRowFor(int row)
  - 显示行索引 -> 内存行索引。当前页/缓存未覆盖该行时原样返回
    row（冷启动直读内存）；否则按键匹配内存行，匹配不到返回 -1。

- override bool CellReady(int row, int col)
  - 当前页或缓存覆盖该行时为 true；二者皆无时仅当尚无当前结果
    且 row 在内存范围内为 true（冷启动直读内存）。

- override RowKey GetRowKey(int row)
  - 行业务身份：当前页/缓存中的键优先，否则回退 RawKey。

- override string CellText(int row, int col)
  - 单元格文本：当前页/缓存优先，冷启动（无当前结果）直读内存行；
    其余情况为空串。

- override void SetCell(int row, int col, string value)
  - 写单元格：把显示行解析为内存行后写值并 Refresh；无法解析时忽略。

- override bool CanInsert()
  - 本地源支持插行。

- int RowOfKey(string canonical)
  - 当前内存行里键为 `canonical` 的行索引；找不到返回 -1。
    键来源与 Execute 投页一致：显式 keyProvider 优先，
    否则 legacy 位置键。

- override DeltaResult ApplyDeltas(DeltaBatch batch)
  - 按键合并一批增量到内存列表。upsert 覆盖已存在的键并追加
    缺失的键（整行值与列对齐，短行右侧补空）；remove 幂等；
    cell 用稳定 Field 名寻址单列，未知 Field 计入 missing。
    应用完成即 Refresh——页缓存与查询 revision 一起失效，
    网格在下一帧 Poll 到并重算显示模型。

- override bool InsertRow(int at)
  - 在内存位置 at 插入空行并 Refresh；位置越界返回 false。

- override bool RemoveRow(int at)
  - 删除内存位置 at 的行并 Refresh；位置越界返回 false。

- int CompareValue(string leftText, string rightText, int kind)
  - 按 kind（1 整数、2 实数、3 bool、其余文本）比较两个单元格文本，
    返回 -1/0/1。

- bool FilterPassValues(string value, int mode, string a, string b, int kind)
  - 以给定模式/值类型判断单元格文本是否通过过滤：Between 为闭
    区间，Empty/NotEmpty 按文本长度，未识别的模式放行。

- bool FilterPass(string value, QueryFilterClause clause)
  - FilterPassValues 的子句形式；clause 为 null 时一律不通过。

- List<LocalFilterColumn> BuildFilterColumns(QueryPlan plan)
  - 为计划的每个过滤子句预解析全部内存行，构造查询内过滤缓存。

- bool RowPasses(int row, QueryPlan plan, List<LocalFilterColumn> columns)
  - 第 row 行是否通过计划的全部过滤子句。

- List<LocalSortColumn> BuildSortColumns(QueryPlan plan)
  - 为计划的每个排序键预解析全部内存行。

- int CompareRows(int left, int right, QueryPlan plan, List<LocalSortColumn> columns)
  - 按计划的排序键字典序比较两行：类型化键比较预解析值，dir=2
    反转该键；全键相等时按内存行序保持稳定。

- void SortOrder(List<int> order, QueryPlan plan, List<LocalSortColumn> columns)
  - 对 order 做稳定归并排序（比较走 CompareRows）；无排序键时不变。

- string GroupAtom(string value, int kind)
  - 单个分组列的规范化分组原子：整数/实数/bool 按解析值转文本，
    其余原样。

- string GroupKey(int row, QueryPlan plan)
  - 一行在全部分组列下的组合键；原子带长度前缀，避免相邻值粘连歧义。

- void AddGroupBucket(List<QueryGroupBucket> buckets, string key)
  - 累加 key 的行计数；不存在则新建计数为 1 的桶。

- List<QueryGroupBucket> BuildGroups(List<int> order, QueryPlan plan)
  - 按给定行序构建分组桶（组合键 -> 行数）。

- double AggregateNumber(string value, int kind)
  - 把单元格文本按 kind 转为聚合用实数：bool 视为 1/0，其余走 ParseReal。

- List<QueryAggregateValue> BuildAggregates(List<int> order, QueryPlan plan)
  - 计算计划的全部聚合：count（mode 5）为行数；kind=0 的 min/max
    按字典序选原始文本；数值聚合经 AggregateNumber，空集得 0。

- override QueryResult Execute(QueryDescriptor query, QueryViewport viewport, QueryRequestToken token)
  - 同步执行一页查询：校验计划 -> 过滤 -> 排序 -> 视口切片，
    total 为过滤后行数，行 revision 恒为 0；带分组/聚合时附带
    形状结果。token/viewport/query 任一为 null 返回 null。


## LocalFilterColumn (class)

过滤键的查询内缓存。列值只在 Execute 开始时解析一次，比较器
只读取 primitive 数组；Empty/NotEmpty 仍依据原始文本判断。

- int kind;

- int mode;

- string aText;

- string bText;

- int aInt;

- int bInt;

- double aReal;

- double bReal;

- bool aBool;

- bool bBool;

- List<int> ints;

- List<double> reals;

- List<bool> bools;

- LocalFilterColumn(int valueKind, int op, string a, string b)
  - 构造过滤列：类型化（kind 1–3）的两个比较值在构造时解析一次。

- void Add(string value)
  - 追加一行的预解析值：按 kind 解析文本；kind=0（文本）不缓存。

- bool Pass(string raw, int row)
  - 第 row 行是否通过本列过滤。类型化操作按预解析值比较
    （Between 为闭区间；bool 只支持 Equals），Empty/NotEmpty
    始终按原始文本判断，其余模式回退 PassText 的文本语义。

- bool PassText(string value)
  - 对原始文本执行文本语义比较：Contains/StartsWith/EndsWith、
    字典序比较（Equals/Greater/Less 及组合）与闭区间 Between；
    未识别的模式放行。


## LocalSortColumn (class)

单个排序键的预计算值。文本仍直接读取源行；数值和 bool 只在
查询开始时解析一次，避免稳定归并排序的比较热路径反复转换。

- int kind;

- List<int> ints;

- List<double> reals;

- List<bool> bools;

- LocalSortColumn(int valueKind)
  - 以值类型 kind（0 文本、1 整数、2 实数、3 bool）建列，键数组为空。

- void Add(string value)
  - 追加一行的预解析键：按 kind 解析文本；kind=0（文本）不缓存。

- int IntAt(int row)
  - 第 row 行的预解析整数键。

- double RealAt(int row)
  - 第 row 行的预解析实数键。

- bool BoolAt(int row)
  - 第 row 行的预解析 bool 键。


## MutationResult (class)

单个 mutation 的服务端结果。

- static const int Accepted=1;
  - 1 接受、2 拒绝、3 冲突、4 取消。

- static const int Rejected=2;

- static const int Conflict=3;

- static const int Cancelled=4;

- string mutationId;

- int status;

- string canonicalValue;

- string serverValue;

- int serverRevision;

- string message;

- MutationResult(string id, int resultStatus, string canonical, string server, int revision, string detail)
  - 构造结果；各文本字段为 null 时记为空串。

- static MutationResult AcceptedValue(string id, string value, int revision)
  - 接受：canonical 为规范化后的新值，revision 为服务端行 revision。

- static MutationResult RejectedValue(string id, string detail)
  - 拒绝：detail 说明原因，serverRevision 为 -1。

- static MutationResult ConflictValue(string id, string server, int revision, string detail)
  - 冲突：server/serverRevision 为服务端当前值与行 revision。

- static MutationResult CancelledValue(string id, string detail)
  - 取消：detail 说明原因，serverRevision 为 -1。

- string MutationId()
  - 对应的 mutation 标识。

- int Status()
  - 结果状态（Accepted/Rejected/Conflict/Cancelled）。

- string CanonicalValue()
  - 接受时的规范化新值；其它状态为空串。

- string ServerValue()
  - 冲突时的服务端当前值；其它状态为空串。

- int ServerRevision()
  - 服务端行 revision；拒绝/取消时为 -1。

- string Message()
  - 失败/冲突说明文本。

- bool IsValid()
  - id 非空且状态为四个合法值之一时为 true。

- string ToJson()
  - 单个结果的传输 JSON（id/status/canonical/server/revision/message）。


## PivotDataSource (class)

透视视图数据源：把 DataTableState 里物化好的交叉聚合
（pvRowKeys × pvColKeys → pvCells）当成普通行×列源交给
渲染管线。列 0 是行维度标签，之后每列一个列键的聚合值，
Sum/Max/Min 模式再追加一列“Total”行总计。聚合格是派生结果，
写入被忽略、不可插入——视图层的所有排序/筛选/汇总
（作用于交叉结果本身）照常工作。

Poll 代理到底层源：底层交付新数据时标记交叉表脏，
下一帧重物化，服务端/实时源的增量自动跟进。

- DataTableState st;

- DataSource inner;

- PivotDataSource(DataTableState state, DataSource innerSrc)

- override int RowCount()

- override string CellText(int row, int col)

- override int CellNum(int row, int col)

- override double CellReal(int row, int col)

- override bool Poll()


## QueryAggregateClause (class)

一个声明式聚合。mode：1 sum、2 average、3 min、4 max、5 count。

- int column;

- int mode;

- int kind;

- QueryAggregateClause(int col, int op, int valueKind)
  - 构造聚合子句。

- int Column()
  - schema 中的稳定列槽位。

- int Mode()
  - 聚合模式（1 sum、2 average、3 min、4 max、5 count）。

- int Kind()
  - 值类型 kind（与 QueryFilterClause 相同）。


## QueryAggregateValue (class)

查询结果中的聚合值。number 用于数值/布尔聚合，text 保存文本
min/max 的规范值；count 由 mode=5 表示。

- int column;

- int mode;

- int kind;

- string text;

- double number;

- QueryAggregateValue(int col, int op, int valueKind, string valueText, double valueNumber)
  - 构造聚合值；text 为 null 记空串。

- int Column()
  - 聚合目标列索引。

- int Mode()
  - 聚合模式（1 sum、2 average、3 min、4 max、5 count）。

- int Kind()
  - 值类型 kind（与 QueryFilterClause 相同）。

- string Text()
  - 文本 min/max 的规范值；数值聚合为空串。

- double Number()
  - 数值/布尔聚合结果；count 模式下为行数。


## QueryCapabilities (class)

查询数据源独立能力。各能力彼此正交，使用位掩码而不是单一
ServerControlled 开关，避免把“能服务端排序”误当成“能服务端聚合”。

- static const int None=0;
  - 无任何能力。

- static const int Filter=1;
  - 支持下推过滤。

- static const int Sort=2;
  - 支持下推排序。

- static const int Group=4;
  - 支持下推分组。

- static const int Aggregate=8;
  - 支持下推聚合。

- static const int Page=16;
  - 支持视口分页（offset/cursor）。

- static const int StableKeys=32;
  - 行携带稳定业务键。

- static const int Edit=64;
  - 支持服务端单元格编辑。

- static const int Insert=128;
  - 支持服务端插行。

- static const int Delete=256;
  - 支持服务端删行。

- static const int Server=512;
  - 预留能力位，当前无消费方。

- int mask;

- QueryCapabilities(int value)
  - 内部构造；value 为能力掩码。

- static QueryCapabilities Of(int value)
  - 以给定掩码构造能力集。

- int Mask()
  - 原始能力掩码。

- bool Has(int capability)
  - 掩码包含 capability 位时为 true。


## QueryDataSource (class)

查询数据源的兼容门面。旧 DataSource 读取接口只暴露当前已提交页；
Execute/Finish/Poll 负责把不可变 QueryResult 原子交给 UI，旧结果不会
因为网络返回顺序而覆盖新查询。

- QueryDescriptor activeQuery;

- QueryViewport activeViewport;

- QueryRequestToken activeToken;

- QueryResult pendingResult;

- QueryResult currentResult;

- QueryResult errorResult;

- bool resultPending;

- bool requestActive;

- int generation;

- int pageSize;

- int nextRequestId;

- int schemaVersion;

- int serverRevision;

- List<SchemaColumn> schema;

- DataPageCache pageCache;

- TableTransaction activeTransaction;

- TransactionRequestToken activeTransactionToken;

- TableTransactionResult pendingTransactionResult;

- TableTransactionResult currentTransactionResult;

- TableTransactionResult errorTransactionResult;

- bool transactionPending;

- bool transactionActive;

- int nextTransactionRequestId;

- QueryDataSource()
  - 构造源：generation 从 1 起，页大小 100，页缓存上限 256 MiB。

- void Setup()
  - 重置全部查询与事务状态到初始值。

- void ClearTransactionState(bool keepCurrent)
  - 取消活动事务 token、终结仍在 Pending 的事务并清理事务状态；
    keepCurrent=false 时连同当前事务结果一起清除。

- void SetPageSize(int size)
  - 修改页大小（最小 1）；同时取消活动请求、清空结果与页缓存、
    清理事务并推进 generation。页大小未变化时为空操作。

- int PageSize()
  - 当前页大小。

- void Refresh()
  - 源内容或行身份改变时使当前页、活动请求和页缓存一起失效。

- void SetSchema(List<SchemaColumn> value, int version)
  - 替换 schema（复制保存、跳过 null 列），版本号钳制到 >=1；
    同时取消活动请求、清空页缓存与结果、清理事务并推进 generation。

- List<SchemaColumn> DescribeSchema()
  - 当前 schema 的浅拷贝列表。

- virtual QueryCapabilities Capabilities()
  - 门面基类只承诺分页；子类按实际能力覆盖。

- virtual QueryPlanContract BuildQueryPlan(QueryDescriptor query)
  - 构建远端执行契约。schema 尚未描述时使用未知列数，
    避免把未初始化的远端源误判为零列。

- QueryRequestEnvelope BuildQueryRequest(QueryPlanContract contract, QueryViewport viewport, QueryRequestToken token)
  - 将已校验契约、视口和 token 绑定为稳定的远端请求包。
    服务端适配器可直接序列化该对象，不必重复拼装身份字段。

- int SchemaVersion()
  - 当前 schema 版本号。

- int ServerRevision()
  - 当前服务端数据 revision。

- void SetServerRevision(int revision)
  - server revision 变化必须由调用方显式推进；旧 token 不会再匹配。

- QueryRequestToken NewToken(QueryDescriptor query, int generation)
  - 用下一个请求序号和当前源状态（generation/schema/revision）构造
    请求 token；query 为 null 时身份字段取空串/0。

- QueryRequestToken RequestQuery(QueryDescriptor query, QueryViewport viewport, QueryRequestToken token)
  - 开始一个查询。子类可重写 Execute 做同步 fake 或启动异步工作；
    异步完成路径只调用 FinishQuery(result, token)。

- override void RequestBlock(int row)
  - 请求包含 row 的随机视口页；已就绪或同一页正在请求时合并。

- override void RequestNextBlock()
  - 请求紧接当前结果之后的下一页：start 顺延，有 continuation 时
    携带它继续 keyset 分页。total 已知且已到尾、或结果声明无更多
    数据时不发请求；已有请求在途时忽略。

- override bool HasMore()
  - 是否还有后续页：尚无结果时为 true（未知）；total 已知时按
    start+count<total 判断，否则沿用结果的 hasMore 标记。

- virtual void FetchQuery(QueryDescriptor query, QueryViewport viewport, QueryRequestToken token)
  - 同步扩展点：调用 Execute 并立即 FinishQuery。异步源改为重写
    FetchQueryPlan 启动后台工作，完成时只回调 FinishQuery。

- virtual void FetchQueryPlan(QueryDescriptor query, QueryPlanContract contract, QueryViewport viewport, QueryRequestToken token)
  - 新的远端入口同时接收下推/残差契约；使用独立名称避免
    依赖同名重载虚分派。默认实现保留旧 FetchQuery 扩展点，
    已有数据源无需修改即可继续工作。

- virtual QueryResult Execute(QueryDescriptor query, QueryViewport viewport, QueryRequestToken token)
  - 执行一页查询；基类返回 null（经 FinishQuery 视为失败），
    子类必须提供真实实现。

- TransactionRequestEnvelope BuildTransactionRequest(TableTransaction transaction, TransactionRequestToken token)
  - 构建独立于分页请求的事务传输包。

- TransactionRequestToken SubmitTransaction(TableTransaction transaction)
  - 提交一个已完成本地校验的事务。事务必须处于 Pending，
    同一数据源同时只保留一个活动提交。

- virtual void FetchTransaction(TableTransaction transaction, TransactionRequestToken token)
  - 远端适配器重写此方法启动实际提交；完成时只调用 FinishTransaction。

- bool TransactionResultShapeValid(TableTransactionResult result)
  - 校验结果形状可否采信：事务 id 与条数匹配活动事务，每条结果
    有效、mutationId 属于该事务且不重复，且逐条状态分布与事务级
    状态一致——Acked/Cancelled 要求全部同态；Rejected/Conflict 在
    AllOrNothing 模式下不允许部分接受。

- bool FinishTransaction(TableTransactionResult result, TransactionRequestToken token)
  - 在收到完整且匹配身份的事务结果后排入 UI 线程 pending 槽。

- void CancelTransaction(TransactionRequestToken token)
  - 取消 transport token，并将仍在 Pending 的事务终结为 Cancelled。

- TableTransactionResult CurrentTransactionResult()
  - 尚无完成事务时为 null。

- TableTransactionResult ErrorTransaction()
  - 最近一次被拒绝入库的异常事务结果；尚无为 null。

- virtual void ApplyTransactionResult(TableTransactionResult result)
  - accepted 结果默认使查询页和缓存失效；具体 source 可重写此方法
    先更新后端/overlay，再调用 base 触发新 revision 查询。

- bool PollTransaction()
  - 把 pending 事务结果落实为状态迁移：Acked 触发 ApplyTransactionResult，
    Conflict/Rejected 把首条详情记入事务，其余取消；形状不再合法时把
    结果转入 error 槽。返回 UI 是否需要刷新（有已落实快照）。

- void FinishQuery(QueryResult result, QueryRequestToken token)
  - 迟到、取消、schema/revision 不匹配的结果在进入 pending 前丢弃。

- override bool Poll()
  - UI 每帧轮询：先落实事务结果，再把 pending 查询结果原子转入
    currentResult。过期、取消或视口不匹配的结果被静默丢弃。
    返回本轮是否有任何数据变化。

- void Cancel(QueryRequestToken token)
  - 取消 token；若它正是活动请求，同时复位请求状态。token 为 null
    时不做任何事。

- QueryResult CurrentResult()
  - 当前已提交页的结果；尚无为 null。

- QueryResult ErrorResult()
  - 最近一次失败/过期查询的结果；尚无为 null。

- int CacheBytes()
  - 页缓存当前占用的估算字节数。

- void ClearCache()
  - 清空页缓存（不影响当前结果）。

- QueryResult CachedRow(int row)
  - 按当前查询身份在页缓存中查找包含 row 的页；未命中返回 null。

- override bool ServerControlled()
  - 查询门面由外部查询驱动，行序归“服务端”掌控；渲染层因此
    不自动把表头排序/筛选下推为查询。

- override int RowCount()
  - total 已知时为 total，否则为已获取范围的尾部（start+count）。

- override bool CellReady(int row, int col)
  - 当前页覆盖该行或页缓存命中时为 true。

- override RowKey GetRowKey(int row)
  - 行的业务身份；当前页与页缓存都未覆盖该行时返回 null。

- override string CellText(int row, int col)
  - 单元格文本；当前页未覆盖该行时回退页缓存，仍不可得为空串。

- override int CellNum(int row, int col)
  - 单元格文本经 DataTable.ParseInt 的整数解读。

- override double CellReal(int row, int col)
  - 单元格文本经 DataTable.ParseReal 的实数解读。

- override int CellDay(int row, int col, int order)
  - 单元格文本经 DataTable.ParseDate 的日号解读（order 为列的日期字段顺序）。

- override bool CellBool(int row, int col)
  - 单元格文本经 DataTable.ParseBool 的真值解读。


## QueryDescriptor (class)

可传给本地/远端查询执行器的稳定查询快照。
过滤器和排序键只使用声明式标量，不捕获 lambda 或 UI 对象，
因而可以直接序列化、哈希和下推到服务端。

- static const int FormatVersion=1;

- int version;

- string dataset;

- int dataRevision;

- int queryRevision;

- int viewRevision;

- int capabilities;

- List<QueryFilterClause> filters;

- List<QuerySortClause> sorts;

- List<QueryGroupClause> groups;

- List<QueryAggregateClause> aggregates;

- QueryDescriptor(string datasetName, int dataRev, int queryRev, int viewRev, int caps)
  - 构造空查询描述：版本固定为 FormatVersion，四类子句列表为空；
    datasetName 为 null 记空串。

- static QueryDescriptor Of(DataTableState st, string datasetName, int caps)
  - 从表格状态取 data/query/view 三个 revision 构造描述。

- string Dataset()
  - 目标数据集名。

- int DataRevision()
  - 数据内容 revision。

- int QueryRevision()
  - 查询参数 revision。

- int ViewRevision()
  - 视图（几何/显示）revision。

- int Capabilities()
  - 能力掩码。

- List<QueryFilterClause> Filters()
  - 过滤子句列表（构建阶段可变）。

- List<QuerySortClause> Sorts()
  - 排序子句列表（构建阶段可变）。

- List<QueryGroupClause> Groups()
  - 分组子句列表（构建阶段可变）。

- List<QueryAggregateClause> Aggregates()
  - 聚合子句列表（构建阶段可变）。

- void AddFilter(QueryFilterClause clause)
  - 追加一个过滤子句；null 忽略。

- void AddSort(QuerySortClause clause)
  - 追加一个排序子句；null 忽略。

- void AddGroup(QueryGroupClause clause)
  - 追加一个分组子句；null 忽略。

- void AddAggregate(QueryAggregateClause clause)
  - 追加一个聚合子句；null 忽略。

- void ClearFilters()
  - 清空过滤子句。

- void ClearSorts()
  - 清空排序子句。

- void ClearGroups()
  - 清空分组子句。

- void ClearAggregates()
  - 清空聚合子句。

- QueryPlan BuildPlan(int columnCount, QueryCapabilities supported)
  - 校验并构建查询计划（列数已知，columnCount<0 表示未知）。

- QueryPlan BuildPlan(List<SchemaColumn> schema, QueryCapabilities supported)
  - 校验并构建查询计划（追加 schema 值类型匹配检查）。

- bool SameQuery(QueryDescriptor other)
  - 逻辑查询相同的判断不包含 viewRevision；列宽、隐藏和冻结不应
    让远端查询缓存失效。

- bool IsCurrent(DataTableState st)
  - 渲染/显示快照是否仍对应当前状态。

- JsonValue FilterJson(QueryFilterClause clause)
  - 单个过滤子句的 JSON 形式（col/op/kind/a/b）。

- JsonValue SortJson(QuerySortClause clause)
  - 单个排序子句的 JSON 形式（col/dir/kind）。

- JsonValue GroupJson(QueryGroupClause clause)
  - 单个分组子句的 JSON 形式（col/kind）。

- JsonValue AggregateJson(QueryAggregateClause clause)
  - 单个聚合子句的 JSON 形式（col/op/kind）。

- void PutShape(JsonValue root)
  - 把 groups/aggregates 数组写入 root；相应类别为空时不写键。

- JsonValue ToJsonValue()
  - 完整描述的 canonical JSON：字段写入顺序固定，含 filters/sorts/
    groups/aggregates（空类别不写键）；view revision 也在其中。

- string ToJson()
  - ToJsonValue 的序列化文本（完整描述的 canonical JSON）。

- string QueryJson()
  - 供查询执行和页缓存使用的逻辑 canonical。视图 revision 不属于
    数据内容身份；列宽、隐藏和冻结变化不应丢弃已获取的查询页。

- int QueryFingerprint()
  - FNV-1a 的 30 位稳定查询指纹；只是缓存键辅助，不替代文本比较。

- int Fingerprint()
  - 完整描述的旧入口保留兼容；查询缓存使用 QueryFingerprint。

- static QueryDescriptor FromJson(string text)
  - 从 ToJson 文本恢复描述。文本为空、结构不符、版本不匹配、
    revision/caps 为负或任一子句非法时返回 null。


## QueryFilterClause (class)

一个可传输的过滤谓词。column 是 schema 中的稳定列槽位；kind：
0 文本、1 整数、2 实数、3 bool。

- int column;

- int mode;

- int kind;

- string value;

- string value2;

- QueryFilterClause(int col, int op, string a, string b, int valueKind)
  - 构造过滤子句；a/b 为 null 记空串。

- int Column()
  - schema 中的稳定列槽位。

- int Mode()
  - 过滤操作符（QueryFilterMode）。

- int Kind()
  - 值类型 kind：0 文本、1 整数、2 实数、3 bool。

- string Value()
  - 第一比较值文本。

- string Value2()
  - 第二比较值文本（Between 的上界）。


## QueryFilterMode (class)

声明式过滤操作。过滤值保持文本形式，执行器按 kind 解释它，
因而本地和远端都能使用同一份可序列化描述。

- static const int Equals=1;
  - 等值比较。

- static const int Contains=2;
  - 子串包含。

- static const int StartsWith=3;
  - 前缀匹配。

- static const int EndsWith=4;
  - 后缀匹配。

- static const int Greater=5;
  - 大于。

- static const int GreaterOrEqual=6;
  - 大于等于。

- static const int Less=7;
  - 小于。

- static const int LessOrEqual=8;
  - 小于等于。

- static const int Between=9;
  - 闭区间 [value, value2]。

- static const int Empty=10;
  - 原始文本为空。

- static const int NotEmpty=11;
  - 原始文本非空。


## QueryGroupBucket (class)

查询结果中的分组元数据。rows 仍然是原始行快照，组树和小计
单独传递，避免为每个显示组复制整页行对象。

- string key;

- int count;

- QueryGroupBucket(string groupKey, int rowCount)
  - 构造分组桶。

- string Key()
  - 分组组合键。

- int Count()
  - 组内行数。


## QueryGroupClause (class)

一个稳定的分组键。多个键按添加顺序形成分组路径。

- int column;

- int kind;

- QueryGroupClause(int col, int valueKind)
  - 构造分组子句。

- int Column()
  - schema 中的稳定列槽位。

- int Kind()
  - 值类型 kind（与 QueryFilterClause 相同）。


## QueryPlan (class)

已校验的查询快照。调用方不能通过它修改原 QueryDescriptor；过滤器
和排序键在构造时逐项复制，只通过按索引读取暴露给执行器。

- static const int InvalidQuery=1;
  - 1：查询描述为 null，或某个子句为 null。

- static const int InvalidRevision=2;
  - 2：data/query/view revision 出现负数。

- static const int InvalidFilterColumn=3;
  - 3：过滤列索引超出 schema 列数。

- static const int InvalidSortColumn=4;
  - 4：排序/分组/聚合列索引超出 schema 列数。

- static const int UnsupportedFilter=5;
  - 5：数据源不支持过滤却携带过滤子句。

- static const int UnsupportedSort=6;
  - 6：数据源不支持排序却携带排序子句。

- static const int InvalidFilterOperator=7;
  - 7：过滤操作符不在 QueryFilterMode 的 1–11 范围内。

- static const int InvalidValueKind=8;
  - 8：子句值类型 kind 不在 0–3 范围内。

- static const int InvalidSortDirection=9;
  - 9：排序方向不是 1（升序）或 2（降序）。

- static const int EmptyDataset=10;
  - 10：预留错误码，当前构建流程不会产生。

- static const int SchemaValueKindMismatch=11;
  - 11：子句值类型 kind 与 schema 列的 ValueType 不匹配。

- static const int UnsupportedGroup=12;
  - 12：数据源不支持分组却携带分组子句。

- static const int UnsupportedAggregate=13;
  - 13：数据源不支持聚合却携带聚合子句。

- static const int InvalidAggregateMode=14;
  - 14：聚合模式不在 1–5 范围内。

- string dataset;

- string queryText;

- int queryHash;

- int dataRevision;

- int queryRevision;

- int capabilities;

- List<QueryFilterClause> filters;

- List<QuerySortClause> sorts;

- List<QueryGroupClause> groups;

- List<QueryAggregateClause> aggregates;

- QueryPlanIssue issue;

- QueryPlan(string name, string text, int hash, int dataRev, int queryRev, int caps, List<QueryFilterClause> fs, List<QuerySortClause> ss, List<QueryGroupClause> gs, List<QueryAggregateClause> aggs, QueryPlanIssue problem)
  - 构造计划快照：四类子句逐项复制（跳过 null 项），null 列表按空处理；
    dataset/queryText 为 null 时记为空串。

- static QueryPlan Subset(QueryPlan source, int caps, List<QueryFilterClause> fs, List<QuerySortClause> ss, List<QueryGroupClause> gs, List<QueryAggregateClause> aggs)
  - 从已校验计划复制一个操作子集。子计划保留完整查询身份，
    只改变执行器可见的 clause 集合；列表和 clause 均重新复制。

- static QueryPlan Fail(QueryPlan plan, QueryPlanIssue problem)
  - 保留原计划的身份与子句集合，仅附加校验问题，得到失败计划副本。

- static bool SchemaKindMatches(int schemaKind, int queryKind)
  - 判断查询值 kind（0 文本、1 整数、2 实数、3 bool）能否作用于
    schema 列的 ValueType（按 TableValue kind 编号：1 文本、2 整数、
    3 实数、4 bool）；文本查询额外接受 ValueType 0。

- static QueryPlan Build(QueryDescriptor query, List<SchemaColumn> schema, QueryCapabilities supported)
  - 按 schema 校验并构建计划：在无 schema 版本（列数校验、能力检查）
    通过后，追加“子句值 kind 与列 ValueType 匹配”检查，过滤/排序/
    分组/聚合逐类检查，首个不匹配即以 SchemaValueKindMismatch 失败返回。

- static QueryPlan Build(QueryDescriptor query, int columnCount, QueryCapabilities supported)
  - 校验查询并构建计划：检查 revision 非负、各类子句非空且列索引/
    操作符/方向/kind 合法，并确认所需能力在 supported 掩码内；
    首个问题即作为 issue 返回（子句列表仍原样复制进计划）。
    columnCount<0 表示列数未知，跳过列越界检查。

- bool IsValid()
  - 计划无校验问题时为 true。

- QueryPlanIssue Issue()
  - 校验问题；有效计划为 null。

- string Error()
  - 校验错误文本；无问题时为空串。

- string Dataset()
  - 目标数据集名。

- string QueryText()
  - 构建时锁定的 canonical 查询文本。

- int QueryHash()
  - 查询指纹（QueryDescriptor.QueryFingerprint）。

- int DataRevision()
  - 构建时的数据 revision。

- int QueryRevision()
  - 构建时的查询 revision。

- int Capabilities()
  - 执行器可见的能力掩码。

- int FilterCount()
  - 过滤子句数量。

- int SortCount()
  - 排序子句数量。

- int FilterColumnAt(int index)
  - 第 index 个过滤子句的列索引；越界返回 -1。

- int FilterModeAt(int index)
  - 第 index 个过滤子句的操作符（QueryFilterMode）；越界返回 0。

- int FilterKindAt(int index)
  - 第 index 个过滤子句的值类型 kind；越界返回 -1。

- string FilterValueAt(int index)
  - 第 index 个过滤子句的第一比较值；越界返回空串。

- string FilterValue2At(int index)
  - 第 index 个过滤子句的第二比较值（Between 的上界）；越界返回空串。

- int SortColumnAt(int index)
  - 第 index 个排序子句的列索引；越界返回 -1。

- int SortDirectionAt(int index)
  - 第 index 个排序子句的方向（1 升序、2 降序）；越界返回 0。

- int SortKindAt(int index)
  - 第 index 个排序子句的值类型 kind；越界返回 -1。

- int GroupCount()
  - 分组子句数量。

- int AggregateCount()
  - 聚合子句数量。

- int GroupColumnAt(int index)
  - 第 index 个分组子句的列索引；越界返回 -1。

- int GroupKindAt(int index)
  - 第 index 个分组子句的值类型 kind；越界返回 -1。

- int AggregateColumnAt(int index)
  - 第 index 个聚合子句的列索引；越界返回 -1。

- int AggregateModeAt(int index)
  - 第 index 个聚合子句的模式（1 sum、2 average、3 min、4 max、5 count）；越界返回 0。

- int AggregateKindAt(int index)
  - 第 index 个聚合子句的值类型 kind；越界返回 -1。

- QueryFilterClause FilterAt(int index)
  - 返回第 index 个过滤子句的复制值，调用方无法改动计划内部；越界返回 null。

- QuerySortClause SortAt(int index)
  - 返回第 index 个排序子句的复制值；越界返回 null。

- JsonValue FilterJson(QueryFilterClause clause)
  - 单个过滤子句的 JSON 形式（col/op/kind/a/b，均为声明式标量）。

- JsonValue SortJson(QuerySortClause clause)
  - 单个排序子句的 JSON 形式（col/dir/kind）。

- JsonValue GroupJson(QueryGroupClause clause)
  - 单个分组子句的 JSON 形式（col/kind）。

- JsonValue AggregateJson(QueryAggregateClause clause)
  - 单个聚合子句的 JSON 形式（col/op/kind）。

- JsonValue ToJsonValue()
  - 计划的 canonical JSON：版本、完整查询身份、四类子句数组，以及失败时的 issue 对象。

- string ToJson()
  - ToJsonValue 的序列化文本，用于远端传输与日志。


## QueryPlanContract (class)

远端查询的下推/残差契约。Pushed 是服务端明确承诺执行的
操作，Residual 必须由后续执行器在完整候选集上完成；二者都保留
原查询的 dataset、revision、canonical text 和 hash。

- QueryPlan pushed;

- QueryPlan residual;

- int pushedCapabilities;

- int residualCapabilities;

- QueryPlanIssue issue;

- QueryPlanContract(QueryPlan pushedPlan, QueryPlan residualPlan, int pushedMask, int residualMask, QueryPlanIssue problem)
  - 内部构造：pushed/residual 与两个能力掩码、issue 一一对应。

- static QueryPlanContract FromPlan(QueryPlan full, int supportedMask)
  - 按能力掩码把已校验计划拆成下推与残差两个子计划：数据源支持的
    操作类别整体进入 pushed，其余整体落入 residual；两个子计划均
    保留完整查询身份。full 为 null 或无效时结果携带同一 issue。

- static QueryPlanContract Build(QueryDescriptor query, int columnCount, QueryCapabilities supported)
  - 先按“全能力”校验查询再拆分，使能力不足只影响下推范围，
    不会掩盖子句本身的校验错误；columnCount<0 表示列数未知。

- static QueryPlanContract Build(QueryDescriptor query, List<SchemaColumn> schema, QueryCapabilities supported)
  - 同上，但校验阶段追加 schema 值类型匹配检查。

- bool IsValid()
  - 契约无校验问题时为 true。

- QueryPlanIssue Issue()
  - 校验问题；有效契约为 null。

- string Error()
  - 校验错误文本；无问题时为空串。

- QueryPlan Pushed()
  - 服务端承诺执行的下推子计划；契约无效时为 null。

- QueryPlan Residual()
  - 必须由后续执行器补执行的残差子计划；无残差时子句列表为空。

- int PushedCapabilities()
  - pushed 中实际出现的操作类别掩码。

- int ResidualCapabilities()
  - residual 中实际出现的操作类别掩码。

- bool HasResidual()
  - 存在需要本地补执行的操作时为 true。

- bool IsComplete()
  - 有效且无残差，即查询可整体下推。

- string ToJson()
  - 远端传输包的 canonical JSON。子计划 JSON 各自携带完整查询身份，
    服务端可以只读取 pushed，残差执行器可以只读取 residual。


## QueryPlanIssue (class)

查询计划的确定性校验问题。计划只保存声明式标量和错误文本，
不持有数据源、UI 或可执行回调，因而可安全交给本地/远端执行器。

- int code;

- string message;

- int column;

- int clause;

- QueryPlanIssue(int c, string msg, int col, int at)
  - 构造校验问题；col 为出错列索引、at 为出错子句索引，与列/子句无关时均为 -1。

- int Code()
  - 稳定错误码，取值见 QueryPlan 的 Invalid*/Unsupported* 常量。

- string Message()
  - 面向日志与远端传输的错误文本。

- int Column()
  - 出错列索引；与列无关时为 -1。

- int Clause()
  - 出错子句在其类别列表中的索引；与子句无关时为 -1。


## QueryRequestEnvelope (class)

远端查询请求的稳定传输包。它把不可变计划、视口和请求身份
绑定在一起，服务端可据此校验迟到响应、cursor 和 revision。

- static const int FormatVersion=1;

- QueryPlanContract contract;

- QueryViewport viewport;

- QueryRequestToken token;

- QueryRequestEnvelope(QueryPlanContract planContract, QueryViewport pageViewport, QueryRequestToken requestToken)
  - 内部构造。

- static QueryRequestEnvelope Build(QueryPlanContract contract, QueryViewport viewport, QueryRequestToken token)
  - 把契约、视口与请求身份绑定为传输包。

- QueryPlanContract Contract()
  - 下推/残差契约。

- QueryViewport Viewport()
  - 请求的视口。

- QueryRequestToken Token()
  - 请求身份。

- bool IsValid()
  - 包自洽：契约有效、两个子计划齐全，且 token 的查询文本与
    指纹同 pushed/residual 一致。

- string ToJson()
  - canonical JSON：顶层先写请求身份和视口，再写完整 plan 包，
    方便服务端在不解析 rows 的情况下完成请求去重和 revision 校验。


## QueryRequestToken (class)

一次查询请求的不可变身份。cancel 只改变请求状态，不改变身份，
因而迟到结果即使内容相同也不能重新进入缓存。

- int requestId;

- int generation;

- string queryText;

- int queryHash;

- int schemaVersion;

- int serverRevision;

- bool cancelled;

- QueryRequestToken(int id, int gen, string canonical, int hash, int schema, int server)
  - 构造请求身份；cancelled 初始为 false，canonical 为 null 记空串。

- int RequestId()
  - 单调递增的请求序号。

- int Generation()
  - 创建时的源 generation；Refresh/SetSchema 等会使旧代失效。

- string QueryText()
  - 请求的 canonical 查询文本。

- int QueryHash()
  - 查询指纹。

- int SchemaVersion()
  - 创建时的 schema 版本。

- int ServerRevision()
  - 创建时的服务端 revision。

- bool IsCancelled()
  - token 是否已被取消。

- void Cancel()
  - 取消该 token；只改变请求状态，不改变身份。

- bool SameIdentity(QueryRequestToken other)
  - 六个身份字段全部相等才视为同一次请求；other 为 null 返回 false。


## QueryResult (class)

一页查询结果的传输元数据。totalKnown=false 时，total 不参与
RowCount 推断，HasMore/continuation 才是尾部语义的来源。

- int requestId;

- int generation;

- string queryText;

- int queryHash;

- int schemaVersion;

- int serverRevision;

- int start;

- int requestedCount;

- string cursor;

- int total;

- bool totalKnown;

- bool hasMore;

- string continuation;

- List<QueryRow> rows;

- List<QueryGroupBucket> groups;

- List<QueryAggregateValue> aggregates;

- string error;

- bool valid;

- QueryResult(int id, int gen, string canonical, int hash, int schema, int server, int at, int requested, string pageCursor, List<QueryRow> values, int totalCount, bool known, bool more, string nextCursor, string failure)
  - 内部构造：start 钳制到 >=0、requestedCount 钳制到 >=1，文本字段
    null 记空串；total 已知但小于 start+行数 时抬到 start+行数。
    任一行键缺失或 revision 为负、行数超出请求页大小、或声明
    hasMore 却缺 continuation 时置 valid=false 并把原因记入 error。

- static QueryResult Empty(QueryRequestToken token, QueryViewport viewport)
  - 构造 token/viewport 身份下的空页（无行，total=0 且已知）；
    token 或 viewport 为 null 返回 null。

- static QueryResult EmptyAt(QueryRequestToken token, int at)
  - 构造仅声明覆盖行 at 的空结果（页大小 1）；token 为 null 返回 null。

- static QueryResult Failure(QueryRequestToken token, QueryViewport viewport, string message)
  - 构造携带 message 的失败结果（无行、total 未知）；
    token 或 viewport 为 null 返回 null。

- int RequestId()
  - 结果所属请求序号。

- int Generation()
  - 结果所属 generation。

- string QueryText()
  - 请求的 canonical 查询文本。

- int QueryHash()
  - 查询指纹。

- int SchemaVersion()
  - 构建时的 schema 版本。

- int ServerRevision()
  - 构建时的服务端 revision。

- int Start()
  - 本页起始行索引。

- int RequestedCount()
  - 请求的页大小。

- string Cursor()
  - 请求使用的续页 token。

- int Count()
  - 实际返回行数。

- List<QueryRow> Rows()
  - 行快照列表。

- int Total()
  - 全集行数；仅在 TotalKnown 时可信。

- bool TotalKnown()
  - 服务端是否给出了全集行数。

- bool HasMore()
  - 简单顺延后是否还有数据。

- string Continuation()
  - 服务端续页 token；空串表示无。

- string Error()
  - 失败/形状错误文本；成功为空串。

- bool Failed()
  - 有错误文本或形状校验未通过时为 true。

- List<QueryGroupBucket> Groups()
  - 分组桶列表（无分组为空）。

- List<QueryAggregateValue> Aggregates()
  - 聚合值列表（无聚合为空）。

- QueryResult WithShape(List<QueryGroupBucket> groupValues, List<QueryAggregateValue> aggregateValues)
  - 复制本结果并替换分组/聚合形状（逐项复制，null 列表按空处理）；
    其余元数据与行快照保持共享。

- bool Covers(int row)
  - 本页是否覆盖显示行索引 row。

- bool MatchesViewport(QueryViewport viewport)
  - 结果的 start/count/cursor 与视口完全一致；viewport 为 null 返回 false。

- QueryRow RowAt(int row)
  - 覆盖 row 时返回该行快照，否则返回 null。

- string CellText(int row, int col)
  - 单元格文本；行不存在或行快照无 cells 时为空串。

- bool Matches(QueryRequestToken token)
  - 结果身份与 token 的六个字段完全一致；token 为 null 返回 false。

- QueryResult Rebind(QueryRequestToken token, QueryViewport viewport)
  - 缓存页不属于某一次请求；命中后只替换传输身份和视口元数据，
    行快照本身保持共享，从而让 Poll() 按新 token 完成生命周期校验。


## QueryRow (class)

查询结果中的一行。RowKey 和 revision 随结果一起交付，避免页被
重新定位后仍把旧的源索引当成业务身份。

- RowKey key;

- int revision;

- DataRow cells;

- QueryRow(RowKey rowKey, int rowRevision, DataRow values)
  - 构造行快照。

- RowKey Key()
  - 行业务身份。

- int Revision()
  - 交付时的行 revision。

- DataRow Cells()
  - 行单元格快照；可能为 null（读取方需判空）。


## QuerySortClause (class)

一个可传输的稳定排序键。多个键按添加顺序组成字典序。
kind 与 QueryFilterClause 相同。

- int column;

- int direction;

- int kind;

- QuerySortClause(int col, int dir, int valueKind)
  - 构造排序子句；direction 为 1 升序、2 降序。

- int Column()
  - schema 中的稳定列槽位。

- int Direction()
  - 排序方向：1 升序、2 降序。

- int Kind()
  - 值类型 kind（与 QueryFilterClause 相同）。


## QueryViewport (class)

视口请求。start/count 用于 offset 分页；cursor 非空时可由服务端
采用 keyset/cursor 分页。查询描述本身不携带回调或运行时对象。

- int start;

- int count;

- string cursor;

- QueryViewport(int at, int size, string token)
  - 构造视口；start 钳制到 >=0，count 钳制到 >=1，cursor 为 null 记空串。

- int Start()
  - 页起始行索引（offset 分页）。

- int Count()
  - 请求的页大小（行数），至少 1。

- string Cursor()
  - 服务端续页 token；未使用时为空串。

- bool UsesCursor()
  - 是否携带续页 token（可走 keyset/cursor 分页）。


## RateComp (class)

"rate" 适配器：整星数（0..N 文本）。显数 N 取列形状 shapeC
（<=0 用 Rate 默认），经 Shape 声明。Rate 内部以 *2 编码支持
半星；这里只用整数档。

- List<string> poolKeys;

- List<Control> poolCtl;

- int lastV;

- RateComp():base("rate")

- override Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- override void FillStr(Control ctl, string val, DataColumn col, DataSource src, int row)

- override bool TakeCommit(Control ctl, ref string outVal)
  - 空文本/非数值灌 0 后不再上报 "0"（展示不写数据）。

- override void Reset()


## RowEdit (class)

一条可逆更改，记录后 Ctrl+Z 可将其还原。

`kind` 为 0 单元格编辑、1 行插入、2 行删除。删除把整行
文本保存在 `cells` 中，因为源在 RemoveRow 返回的
那一刻就可以丢弃存储——
让行可恢复的是日志，而非源。

- int kind;

- int row;

- int col;

- string before;

- string after;

- List<string> cells;

- int group;
  - 同组条目一起撤销。一次粘贴涉及五十个
    单元格，必须一次 Ctrl+Z 全部回退，而不是五十次——
    因此撤销的单位是组而非条目。0 表示“单独成组”。

- static RowEdit Cell(int row, int col, string before, string after)

- static RowEdit Insert(int row)

- static RowEdit Delete(int row, List<string> cells)


## RowKey (class)

稳定的数据行身份。

新的 DataTable 能力应使用 RowKey，而不是把当前显示位置或源数组
下标当成业务身份。文本形式采用调用方提供的规范化键；旧列表源
可通过 Legacy() 获得临时键，但临时键不承诺跨刷新或跨会话稳定。

- string value;

- bool temporary;

- RowKey(string v, bool temp)
  - 内部构造；优先经 Of()/Legacy() 创建。

- static RowKey Of(string value)
  - 以业务键文本构造稳定身份；空文本退化为临时 legacy 键。

- static RowKey Legacy(int row)
  - 为没有业务主键的旧位置数据生成兼容键。
    该键只在当前源实例中有意义。

- static RowKey Legacy(string value)
  - 以任意文本（非行号）生成临时 legacy 键。

- string Canonical()
  - 规范化键文本，用作字典键与相等比较依据。

- bool IsTemporary()
  - 是否为位置派生的临时键（Legacy() 产物）。

- bool Same(RowKey other)
  - 与另一键按规范化文本比较；other 为 null 时返回 false。


## RowKeyProvider (class)

为 DataSource 提供稳定行键的适配点。
默认实现使用兼容的临时位置键。

- virtual RowKey Key(DataSource src, int row)
  - 返回行 `row` 的业务键；默认实现给出临时 legacy 位置键。


## RowListSource (class)

向后兼容适配器：通过 DataSource 接口提供内存中的
字符串单元格 `List<DataRow>`，
现有 `DataTable.Render(..., rows, ...)` 调用无需改动即可继续工作。

- List<DataRow> rows;

- RowListSource(List<DataRow> rows)

- override int RowCount()

- override string CellText(int row, int col)

- override int CellNum(int row, int col)

- override void SetCell(int row, int col, string v)

- override bool CanInsert()
  - 内存列表总能增长或收缩。

- override bool InsertRow(int at)

- override bool RemoveRow(int at)


## RowRef (class)

行键与当前数据源位置的短期映射。
locator 会随排序、过滤和页缓存变化，不能持久化为身份。

- RowKey key;

- int locator;

- int revision;

- RowRef(RowKey k, int at, int rev)
  - 内部构造。


## SchemaColumn (class)

DataTable 列的稳定身份和类型描述。DataColumn 仍是现有渲染/编辑
配置的兼容入口；SchemaColumn 用于新查询、缓存和扩展能力。

- string id;

- string field;

- int valueType;

- string rendererId;

- string editorId;

- bool nullable;

- SchemaColumn(string columnId, string fieldName, int kind)
  - 构造列描述：渲染器/编辑器默认 "text"，默认可空。

- string Id()
  - 列稳定标识。

- string Field()
  - 数据字段名（cell 增量按名寻址）。

- int ValueType()
  - 值类型，按 TableValue 的 kind 编号（0 null、1 text、2 int、3 real、4 bool、5 day）。

- bool Nullable()
  - 列是否允许空值。


## SelectComp (class)

"select" 适配器：选项列 `choices`（Options 声明），单元格值是
所选项文本。灌入按文本匹配 SetIndex；上报当前选项标签。

- List<string> poolKeys;

- List<Control> poolCtl;

- int lastIdx;

- SelectComp():base("select")

- override Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- override void FillStr(Control ctl, string val, DataColumn col, DataSource src, int row)

- override bool TakeCommit(Control ctl, ref string outVal)
  - 单元格值不在 choices 内时灌 -1，TakeCommit 因状态未变
    不上报——展示不会把未知值清成空串。

- override void Reset()


## SelectionMode (class)

选择粒度。Row 是默认的数据行选择模式；Cell/Range 为后续
单元格命令保留明确的协议值。

- static const int Row=1;
  - 数据行选择（默认）。

- static const int Cell=2;
  - 单元格选择（协议保留值）。

- static const int Range=3;
  - 区域选择（协议保留值）。


## SelectionModel (class)

基于稳定 RowKey 的选择快照。

普通模式维护显式选中的业务键；全选模式不物化全集，
只维护被排除的键。字典按 canonical string 做内容相等判断，
列表保留确定性的插入顺序用于快照和导出。

- int mode;

- bool allSelected;

- List<string> explicitKeys;

- Dict <string, bool> explicitIndex;

- List<string> excludedKeys;

- Dict <string, bool> excludedIndex;

- SelectionModel(int selectionMode)
  - 以选择粒度构造模型；非法值回退为 Row。

- static SelectionModel Row()
  - 行选择模型。

- static SelectionModel Cell()
  - 单元格选择模型。

- static SelectionModel Range()
  - 区域选择模型。

- int Mode()
  - 当前选择粒度。

- void SetMode(int selectionMode)
  - 修改选择粒度；非法值忽略（不清空已有选择）。

- bool AllSelected()
  - 是否处于全选模式。

- int Count()
  - 显式选中键数；全选模式返回 -1（全集不物化）。

- int ExplicitCount()
  - 显式选中键数。

- int ExcludedCount()
  - 全选模式下的排除键数。

- bool ValidKey(string key)
  - 键非 null 且非空串时为 true。

- bool ContainsKey(string key)
  - key 是否被选中：全选模式看排除表，否则看显式表。

- bool ExcludedKey(string key)
  - key 是否在排除表中。

- bool AddExplicit(string key)
  - 加入显式选中集；重复或非法键返回 false。

- bool RemoveExplicit(string key)
  - 从显式选中集移除；不存在或非法键返回 false。

- bool AddExcluded(string key)
  - 加入排除集；重复或非法键返回 false。

- bool RemoveExcluded(string key)
  - 从排除集移除；不存在或非法键返回 false。

- bool SelectKey(string key)
  - 选中 key：全选模式移除排除项，否则加入显式集。

- bool DeselectKey(string key)
  - 取消选中 key：全选模式加入排除表，否则移出显式集。

- bool ToggleKey(string key)
  - 切换 key 的选中状态。

- bool ForgetKey(string key)
  - 从选择快照中移除已经不存在的业务键。
    全选模式不新增排除项：删除的行不应在未来重建同键时
    因历史删除而被错误排除。

- void SelectAll()
  - 进入全选模式并清空显式集（排除表保留并继续生效）。

- void Clear()
  - 退出全选并清空显式集与排除集。

- List<string> Keys()
  - 显式选中键的副本列表（插入序）。

- List<string> ExcludedKeys()
  - 排除键的副本列表（插入序）。

- string ToJson()
  - 序列化为 ver/mode/all/keys/excluded JSON。

- static SelectionModel FromJson(string text)
  - 从 ToJson 文本恢复；文本为空或非对象返回 null。
    all=true 时只保留排除表，否则只保留显式集。


## ServerDataSource (class)

服务端/无限/视口数据源：网格以
固定大小的块从底层服务拉取行，只缓存已获取的数据，
总数未知，随用户滚动而增长。

继承它并实现 FillBlock——实际的数据获取。两种交付模式：
- 同步（默认）：网格请求块时 FillBlock 在 UI 线程运行。
适合快速本地源（文件、计算视图、进程内查询）：
网格每帧的 Poll() 会
把刚填充的块并入，因此可见块最多延迟一帧。
- 异步：重写 FetchBlock 启动自己的后台获取
（Thread 工作线程、异步 I/O 调用），并在结果返回处
（例如 App.Post 的处理器）调用 FinishBlock(rows, count)。
Poll() 只在 FinishBlock 执行后才并入数据块，UI 线程从不
等待远端。获取进行中时 RequestBlock 保持合并，
慢服务器无法堆积无界的工作。

行索引由服务器排序：ServerControlled() 返回 true，
网格跳过本地排序/过滤，按源提供的顺序原样渲染
。分组和聚合仍在本地运行，作用于已加载的
前缀（这是部分数据集的固有属性，不是 bug）。

总数模式：
- 未知（默认）：RowCount() 报告已获取前缀，HasMore() 保持 true，
直到一个短块（少于 blockSize 行）宣告
结束——或由 SetKnownTotal 显式固定。
- 已知：Create 后立即 SetKnownTotal(n) 让网格从一开始就有精确的
滚动条；数据块按需在其后流入。

- int blockSize;

- List<DataRow> cache;

- int knownTotal;

- bool loading;

- int fetchStart;

- bool blockDone;

- List<DataRow> blockRows;

- int blockCount;

- int nextFetchId;

- int activeFetchId;

- CellFieldLookup cellFieldOf;
  - 可选的列 Field 名查找（cell 增量按名寻址）。
    null 时 cell 的 Field 按"纯数字 = 列索引"解释。

- ServerDataSource(int blockSize)
  - 以给定块大小创建服务端数据源。
    之后调用 SetKnownTotal 固定总行数；不调用则保持未知并持续增长。

- void Setup(int blockSize)
  - 初始化数据源。自行构建实例的子类（如
    RowListSource.Create 之类的静态工厂）在使用前调用它。

- void SetKnownTotal(int total)
  - 固定总行数。传 -1 回到未知/增长模式。
    数据源可随时调用——例如计数查询返回后——
    网格会在下一次 Poll() 时获取。

- void SetCellFieldLookup(CellFieldLookup lookup)
  - 安装列 Field 名查找（cell 增量按稳定名寻址）。

- override int RowCount()
  - 当前已知行数：服务器已声明时为其总数，
    否则为已获取前缀（网格随数据到达而增长）。

- override bool ServerControlled()
  - 顺序由服务器掌控：网格不得在本地重新排序或过滤。

- override bool CellReady(int row, int col)
  - 只有已获取的行可读；其余渲染为加载
    占位符并触发数据块请求。

- override bool HasMore()
  - 总数已知且已全部获取，或出现短块
    宣告结束时（未知模式），数据源即已耗尽。

- override void RequestBlock(int row)
  - 请求包含 row 的块。连续前缀模型：row 落在已加载前缀内时为
    空操作，只从当前前缀末端继续；获取进行中或数据已耗尽时忽略。

- override void RequestNextBlock()
  - 获取已加载前缀之后的下一块。网格在
    用户滚动到底部时调用：它覆盖参差边界的情况（
    前缀在块中间结束，下一次获取补全该块）以及
    块对齐的情况（前缀正好在边界结束，下一次
    获取从新块开始）。

- void StartFetch(int start)
  - 从 start（钳制到当前前缀末端）发起一次块获取，并登记请求 nonce。

- override bool Poll()
  - 将完成的获取并入缓存。网格每帧调用；
    在 FetchBlock 完成（同步）或调用 FinishBlock（异步）之前不做事。
    有变化时返回 true，网格
    据此标记自身为脏并用新行重绘。

- virtual void FetchBlock(int start, int count)
  - 开始将行 [start, start+count) 取入块缓冲区。
    默认在调用方（UI）线程上同步运行 FillBlock，
    适合快速本地源。重写以真正异步交付，
    并在完成路径中调用 FinishBlock。

- void FinishBlock(List<DataRow> rows, int count)
  - 保存进行中获取的结果。Poll() 在下一帧
    将其并入缓存。

- void FinishBlock(List<DataRow> rows, int count, int fetchId)
  - 带 nonce 的底层完成入口：nonce 不匹配当前请求时丢弃；
    rows 为 null 或 count 越界视为失败块（Poll 将丢弃）。

- virtual int FillBlock(int start, int count, List<DataRow> dst)
  - 从 `start` 开始取最多 `count` 行，每行追加一个 DataRow
    到 `dst`。返回追加的数量；少于 `count` 表示
    服务器没有更多行了（此时固定总数）。同步
    数据源只需实现这一个方法。

- override string CellText(int row, int col)
  - 缓存前缀内的单元格文本；未加载行为空串。

- override int CellNum(int row, int col)
  - 缓存前缀内的整数解读（ParseInt）；未加载行为 0。

- override double CellReal(int row, int col)
  - 缓存前缀内的实数解读（ParseReal）；未加载行为 0.0。

- override int CellDay(int row, int col, int order)
  - 缓存前缀内的日期解读（ParseDate）；未加载行为 NoDate。

- override bool CellBool(int row, int col)
  - 缓存前缀内的布尔解读（ParseBool）；未加载行为 false。

- override void SetCell(int row, int col, string v)
  - 写缓存前缀内的单元格；越界为空操作。

- override RowKey GetRowKey(int row)
  - 稳定行键入口：缓存前缀的行没有业务键，增量按
    legacy:<row> 位置键匹配。持有业务键的服务端源应
    重写 GetRowKey 返回业务键，让 delta 以键身份合并。

- override DeltaResult ApplyDeltas(DeltaBatch batch)
  - 把一批按键增量合并进已获取缓存：upsert 覆盖/追加，
    remove 幂等，cell 按列 Field 名（可选查找）或列索引寻址。
    应用后使缓存语义与新行数一致——knownTotal 未知时前缀
    行数即 RowCount；已知时按需抬高。行键为 legacy 前缀源
    用 legacy:<row> 匹配。


## SortKey (class)

一个生效的排序键：列索引及其方向（1 升序，2 降序）。
取代旧的按索引对齐的 sortCols/sortDirs 并行列表。

- int col;

- int dir;

- SortKey(int c, int d)


## SparseServerDataSource (class)

稀疏分页数据源：按页大小向服务端取数，已知总数时允许直接
请求中间/尾部页；每页独立维护 Empty/Loading/Ready/Error 状态。
同步子类实现 FillPage，异步子类重写 FetchPage。

- int pageSize;

- int knownTotal;

- int generation;

- List<SparseServerPage> pages;

- bool loading;

- bool pageDone;

- SparseServerPage pendingPage;

- List<DataRow> pendingRows;

- int pendingCount;

- int pendingTotal;

- int pendingGeneration;

- int pendingPageId;

- int nextPageId;

- int activePageId;

- SparseServerDataSource(int size)
  - 以页大小创建稀疏页数据源；之后可 SetKnownTotal 固定总数。

- void Setup(int size)
  - 初始化/重置全部状态（供静态工厂式子类使用）；页大小钳制到 >=1。

- void SetKnownTotal(int total)
  - 服务器总数已知时固定 RowCount，使中部/尾部可直接定位。
    传 -1 恢复未知总数模式；下一次短页会重新固定尾部。

- void Invalidate()
  - 查询、schema 或服务器 revision 变化时调用。旧页和旧响应
    均失效，但不会让旧 generation 的结果污染新查询。

- int Generation()
  - 当前 generation（Invalidate 递增）。

- override int RowCount()
  - 总数已知时为其值，否则为已就绪页的最大尾部。

- override bool ServerControlled()
  - 行序归服务器掌控：网格不做本地排序/筛选。

- SparseServerPage PageAt(int start)
  - 起始行为 start 的页；不存在返回 null。

- SparseServerPage PageFor(int row)
  - row 所属页（按页大小对齐），不存在则创建空页登记。

- bool IsPageReady(int start)
  - 该页在本 generation 下已就绪。

- override bool CellReady(int row, int col)
  - row 所在页已就绪且属于当前 generation。

- override bool HasMore()
  - 总数未知时为 true；已知时只要存在未就绪或过期的页就为
    true，全部页就绪为 false。

- override void RequestBlock(int row)
  - 请求 row 所属页：页已就绪/加载中、总数已知且越界、或有请求
    在途时忽略；否则启动该页获取。

- void StartPage(SparseServerPage p)
  - 将页置为 Loading 并发起获取，登记 generation 与页 nonce。

- override void RequestNextBlock()
  - 找到第一个未就绪/过期的页发起获取：总数已知时按页序扫描，
    未知时从已就绪连续前缀的末端继续；无缺口时不请求。

- override bool Poll()
  - 每帧并入完成的页结果：nonce/generation 匹配时校验行数
    （总数已知时要求恰好填满本页剩余量），合法则入页，非法置
    Error；旧代结果被静默丢弃。返回是否处理了当前请求。

- virtual void FetchPage(int start, int count, int gen)
  - 同步扩展点：调用 FillPage 并立即 FinishPage；异步子类重写
    它启动后台获取。

- virtual void FetchPage(int start, int count, int gen, int pageId)
  - 带 nonce 的重载；基类忽略 nonce 直接委托。

- void FinishPage(List<DataRow> rows, int count, int total, int gen)
  - 异步完成页请求。pageId 与 generation 都必须匹配当前请求；
    旧的同 generation 回调也因此不能覆盖新页。
    FinishPage 的四参便捷重载，nonce 取当前活动请求。

- void FinishPage(List<DataRow> rows, int count, int total, int gen, int pageId)
  - 异步完成入口：nonce/generation 必须匹配当前请求，否则丢弃；
    rows 为 null 或 count 越界记为非法（Poll 判为 Error）。

- virtual int FillPage(int start, int count, List<DataRow> dst)
  - 从 start 取最多 count 行追加到 dst，返回实际行数；少于 count
    视为尾页（未知总数模式下由此固定总数）。同步子类只需实现此方法。

- SparseServerPage ReadyPage(int row)
  - 覆盖 row 且属于当前 generation 的就绪页；没有返回 null。

- override string CellText(int row, int col)
  - 就绪页内的单元格文本；未加载/过期行为空串。

- override int CellNum(int row, int col)
  - 就绪页单元格的整数解读（ParseInt）。

- override double CellReal(int row, int col)
  - 就绪页单元格的实数解读（ParseReal）。

- override int CellDay(int row, int col, int order)
  - 就绪页单元格的日期解读（ParseDate，order 为列的日期字段顺序）。

- override bool CellBool(int row, int col)
  - 就绪页单元格的布尔解读（ParseBool）。

- override void SetCell(int row, int col, string v)
  - 写就绪页内的单元格；未就绪为空操作。


## SparseServerPage (class)

稀疏页服务端数据源：与旧的连续前缀 ServerDataSource 并存，
允许已知总数时直接请求中间或尾部页，不把未加载区域误当成
空字符串数据。每个页独立记录 Empty/Loading/Ready/Error 状态，
同一时刻最多一个 in-flight 请求；generation 变化后旧响应直接丢弃。

同步子类只需实现 FillPage；真正异步的子类重写 FetchPage，
在完成路径调用 FinishPage(rows, count, total, generation)。

- int start;

- int count;

- int total;

- int generation;

- int state;

- List<DataRow> rows;

- SparseServerPage(int at, int gen)
  - 构造空页（state=0），total 未知。

- bool Covers(int row)
  - 页处于 Ready 且覆盖 row 时为 true。


## TableDiagnostic (class)

DataTable 的非致命结构诊断。诊断不改变旧 API 的返回值，
供宿主在开发/遥测模式下显示重复键、过期结果和能力不匹配。

- int code;

- string message;

- string field;

- int row;

- TableDiagnostic(int c, string msg, string f, int r)
  - 内部构造；经各 *Diagnostic 工厂创建。

- int Code()
  - 诊断代码：1 重复键，2 缺少稳定键，3 过期结果，4 schema 不匹配。

- string Message()
  - 人类可读的诊断消息。

- string Field()
  - 相关列的 field 名；与列无关的诊断为空串。

- int Row()
  - 相关数据行索引；schema 类诊断为 -1，
    过期结果诊断在此字段携带 generation。


## TableMutation (class)

一次远程单元格修改的不可变描述。业务身份使用 RowKey，
expectedRevision 用于服务端乐观并发校验。

- string mutationId;

- RowKey rowKey;

- int column;

- int expectedRevision;

- string value;

- TableMutation(string id, RowKey key, int col, int revision, string nextValue)
  - 构造 mutation；id/value 为 null 时记为空串。

- static TableMutation Cell(string id, RowKey key, int col, int revision, string nextValue)
  - 构造一个单元格修改；revision 为提交时客户端已知的行 revision。

- string MutationId()
  - 事务内唯一的 mutation 标识。

- RowKey Key()
  - 目标行业务身份。

- int Column()
  - 目标列索引。

- int ExpectedRevision()
  - 乐观并发校验的期望行 revision；服务端值不一致即冲突。

- string Value()
  - 新单元格文本。

- bool IsValid()
  - id 非空、rowKey 存在、列索引与期望 revision 均非负时为 true。

- string ToJson()
  - 序列化为传输 JSON（id/key/col/rev/value）；rowKey 为 null 时 key 记空串。


## TableTransaction (class)

一次可批量提交的 mutation 集合。Draft 阶段可追加，进入校验后
不再接受修改；状态转移是单向的，避免 ACK 覆盖已取消事务。

- string transactionId;

- int mode;

- int state;

- List<TableMutation> mutations;

- string error;

- TableTransaction(string id, int transactionMode)
  - 构造 Draft 事务；id 为 null 记空串，mode 非法时回退为 AllOrNothing。

- static TableTransaction Create(string id, int transactionMode)
  - 创建 Draft 事务。

- string TransactionId()
  - 事务标识。

- int Mode()
  - 提交策略（TransactionMode）。

- int State()
  - 当前生命周期状态（TransactionState）。

- string Error()
  - 失败/冲突原因文本；未失败时为空串。

- int Count()
  - 已加入的 mutation 数量。

- bool IsValid()
  - 事务可提交：id 非空、mode 合法、至少一个 mutation 且每个
    mutation 有效，mutationId 无重复。

- bool HasMutationId(string id)
  - 事务内是否已存在该 id 的 mutation；id 为 null 返回 false。

- bool Add(TableMutation mutation)
  - 仅在 Draft 阶段接受有效且 id 不重复的 mutation；否则返回 false。

- TableMutation At(int index)
  - 第 index 个 mutation 的复制值；越界返回 null。

- bool BeginValidation()
  - 进入校验阶段：要求处于 Draft 且至少有一个 mutation；成功后状态为 Validating。

- bool MarkPending()
  - Validating -> Pending，表示即将提交；其它状态返回 false。

- bool Ack()
  - Pending -> Acked；仅在 Pending 状态成功。

- bool Reject(string message)
  - Pending -> Rejected 并记录原因 message（null 记空串）。

- bool MarkConflict(string message)
  - Pending -> Conflict 并记录冲突详情 message（null 记空串）。

- bool Cancel()
  - Draft/Validating/Pending -> Cancelled；终态不可取消。

- bool IsTerminal()
  - 处于 Acked/Rejected/Conflict/Cancelled 任一终态时为 true。

- string ToJson()
  - 序列化为传输 JSON：事务身份加逐个 mutation 的 JSON 对象数组。


## TableTransactionResult (class)

一次事务的不可变结果快照。结果列表通过 Add 只在构造阶段填充，
读取时使用 ResultAt 的复制值，避免调用方修改内部对象。

- static const int Acked=1;
  - 1 确认、2 拒绝、3 冲突、4 取消。

- static const int Rejected=2;

- static const int Conflict=3;

- static const int Cancelled=4;

- string transactionId;

- int status;

- List<MutationResult> results;

- TableTransactionResult(string id, int resultStatus)
  - 构造结果快照；id 为 null 记空串，results 由 Add 填充。

- static TableTransactionResult Create(string id, int resultStatus)
  - 创建指定状态的结果快照。

- string TransactionId()
  - 事务标识。

- int Status()
  - 事务级状态（Acked/Rejected/Conflict/Cancelled）。

- int Count()
  - 单个结果条数。

- bool IsValid()
  - 结果可采信：id 非空、状态合法且至少包含一条结果。

- bool Add(MutationResult result)
  - 构造阶段追加一条结果；null 被忽略并返回 false。

- MutationResult ResultAt(int index)
  - 第 index 个结果的复制值；越界返回 null。

- int AcceptedCount()
  - 状态为 Accepted 的结果条数。

- int FailedCount()
  - 非 Accepted 的结果条数（含取消）。

- TableTransactionResult Copy()
  - 深复制：状态与全部结果逐条复制，内部列表与原对象独立。

- string ToJson()
  - 序列化为传输 JSON：事务 id/status 加逐条结果的 JSON 对象数组。


## TableValue (class)

类型化查询/编辑值的最小兼容表示。
kind: 0 null, 1 text, 2 int, 3 real, 4 bool, 5 day。

- int kind;

- string text;

- int integer;

- double real;

- bool truth;

- static TableValue Null()
  - kind=0 的空值，其余字段取零值。

- static TableValue Text(string s)
  - kind=1 的文本值。

- static TableValue Int(int n)
  - kind=2 的整数值。

- static TableValue Real(double n)
  - kind=3 的实数值。

- static TableValue Bool(bool b)
  - kind=4 的布尔值。

- static TableValue Day(int n)
  - kind=5 的日期值，integer 存日号（自 1970-01-01 起）。

- int Kind()
  - 值类型 kind（0 null、1 text、2 int、3 real、4 bool、5 day）。

- string AsText()
  - 文本字段（kind=1 有效）。

- int AsInt()
  - 整数字段（kind=2/5 有效）。

- double AsReal()
  - 实数字段（kind=3 有效）。

- bool AsBool()
  - 布尔字段（kind=4 有效）。


## TransactionMode (class)

事务提交策略。

- static const int AllOrNothing=1;
  - 全部成功才提交，任一失败整体拒绝。

- static const int BestEffort=2;
  - 尽力而为：允许部分 mutation 被拒绝，其余照常生效。


## TransactionRequestEnvelope (class)

把事务快照和独立请求身份绑定为服务端传输包。

- static const int FormatVersion=1;

- TableTransaction transaction;

- TransactionRequestToken token;

- TransactionRequestEnvelope(TableTransaction value, TransactionRequestToken requestToken)
  - 内部构造。

- static TransactionRequestEnvelope Build(TableTransaction value, TransactionRequestToken token)
  - 把事务快照与请求身份绑定为传输包。

- TableTransaction Transaction()
  - 待提交的事务快照。

- TransactionRequestToken Token()
  - 请求身份。

- bool IsValid()
  - 包可发送：事务有效且处于 Pending、事务 id 与 token 一致且未被取消。

- string ToJson()
  - canonical JSON：请求身份 + valid 标记 + 完整事务 JSON。


## TransactionRequestToken (class)

一次事务提交的独立请求身份。它不复用 QueryRequestToken，
因而 cell mutation 不会被误判为分页结果。

- int requestId;

- string transactionId;

- int generation;

- int schemaVersion;

- int serverRevision;

- bool cancelled;

- TransactionRequestToken(int id, string txId, int gen, int schema, int server)
  - 构造请求身份；txId 为 null 记空串，cancelled 初始为 false。

- int RequestId()
  - 单调递增的请求序号。

- string TransactionId()
  - 提交的事务标识。

- int Generation()
  - 创建时的源 generation。

- int SchemaVersion()
  - 创建时的 schema 版本。

- int ServerRevision()
  - 创建时的服务端 revision。

- bool IsCancelled()
  - token 是否已被取消。

- void Cancel()
  - 取消该 token；不改变身份。

- bool SameIdentity(TransactionRequestToken other)
  - 五个身份字段全部相等才视为同一次提交；other 为 null 返回 false。


## TransactionState (class)

事务生命周期：Draft -> Validating -> Pending -> Acked，
失败分支为 Rejected/Conflict/Cancelled。

- static const int Draft=1;
  - 草稿：仍可追加 mutation。

- static const int Validating=2;
  - 本地校验中：不再接受修改。

- static const int Pending=3;
  - 已提交，等待服务端结果。

- static const int Acked=4;
  - 服务端确认全部生效。

- static const int Rejected=5;
  - 服务端拒绝。

- static const int Conflict=6;
  - 乐观并发冲突。

- static const int Cancelled=7;
  - 提交前或等待中被取消。


## TransposeDataSource (class)

转置视图数据源：把"原列 i × 原行 j"互换后当成普通行×列源
交给渲染管线。显示行 = 原列（0..trOrigCols-1），显示列 0 是
列标题（行标签），之后每列一个已转置的原行。生成列统一为
只读纯文本——一个显示列混合了原表各类型的值，类型化路径
（数值/日期/布尔）不再成立；排序/选区/剪贴板以文本照常工作。

Poll 代理到底层源：底层交付新数据时标脏，下一帧重物化
（新行数进入列集），服务端/实时源自动跟进。

- DataTableState st;

- DataSource inner;

- TransposeDataSource(DataTableState state, DataSource innerSrc)

- override int RowCount()
  - 显示行数 = 原列数。

- override string CellText(int row, int col)

- override bool Poll()


## TreeAdapter (class)

树模式适配器：把任意层级结构投影成"每行有若干子行"
的形状，网格据此交错显示。行身份仍是源行索引（与
排序/筛选/选择的读取路径一致）；适配器只回答结构问题。

继承它并重写 RootCount / ChildCount / ChildOf：
- `RootCount` 返回顶层行数，`RootAt(i)` 返回第 i 个
顶层行的源索引。默认实现遍历源，把无父行（`ParentOf`
返回 -1）当根；按需重写成直接索引。
- `ChildCount(src, row)` 返回该行的子行数（0 = 叶子）；
`ChildOf(src, row, index)` 返回第 index 个子行的源索引。
- 可选重写 `ParentOf`（默认 -1）：无重写时根 = 全部行
中 ParentOf < 0 者，需要 O(n) 扫描；重写后 RootAt 走
RootCount 直接索引。

- virtual int RootCount(DataSource src)
  - 顶层行数。

- virtual int RootAt(DataSource src, int i)
  - 第 i 个顶层行的源索引。默认实现再次扫描源，
    与 RootCount 的判定保持一致。

- virtual int ChildCount(DataSource src, int row)
  - `row` 的子行数；0 = 叶子。

- virtual int ChildOf(DataSource src, int row, int index)
  - `row` 的第 `index` 个子行的源索引；越界返回 -1。

- virtual int ParentOf(DataSource src, int row)
  - `row` 的父行源索引；顶层行返回 -1。仅默认
    RootCount/RootAt 的"无父即根"判定使用它。

- virtual bool IsLazy(DataSource src, int row)
  - `row` 的子行是否需要按需（异步）取回。默认 false =
    子行已在数据源里，展开即得。

- virtual bool ChildrenLoaded(DataSource src, int row)
  - 懒加载行的子行是否已取回。仅 IsLazy 行会被询问；
    未取回的行展开时触发 LoadChildren。

- virtual void LoadChildren(DataSource src, int row)
  - 开始异步取回 `row` 的子行。实现应尽快把取到的子行
    插入数据源，然后调用 DataTable.TreeLoaded(st, src, row)。
    网格不等待：加载期间该行显示 spinner。


## WidgetComps (class)

内置适配器装配与共享小工具。

- static string PoolKey(RowKey key, int col)
  - 池化键（与 BandGridComp 同约定：稳定行键 + 列号）。

- static bool BoolOf(DataColumn c, string val)
  - 布尔列口径解读：声明的 boolTrue/boolFalse 文本精确匹配
    优先（"是"/"否" 等自定义口径 ParseBool 不认识），其余交
    ParseBool（"1"/"true"/"yes"/"✓"…）。

- static string BoolText(DataColumn c, bool on)
  - 布尔列口径文本：开关/复选提交写回用，与 ToggleBoolCell
    同一套口径，不产生混方言数据。


## List (delegate)

从绑定实体读取数值序列（组件列 CompCol）：返回值按行优先
序列化为逗号分隔文本喂给引擎（与迷你图同一约定），序列化
边界在 GridColumn.Raw() 内——使用层只见 List<int>。

`delegate List<int> GridNums<T>(T row);`


## RowKey (delegate)

从实体读取稳定行键。推荐使用数据库主键或业务复合键；
未设置时由兼容层使用临时位置键。

`delegate RowKey GridKey<T>(T row);`


## T (delegate)

为"新行"占位符和右键菜单插入命令创建空白行实体
（`() => new User()`）。泛型前端无法
自行 `new T()`，因此这是让 AllowAddRow 真正生效的关键；
缺少它时网格行数保持只读。

`delegate T GridNew<T>();`


## bool (delegate)

从实体读取布尔值，用于复选框 / bool 列。

`delegate bool GridBool<T>(T row);`


## bool (delegate)

xlsx 弹层导出入口的槽签名（实现在 DataTable.ExportXlsx.zan 的
DataTableXlsx.ExportToXlsxAsyncUi；类型在此，槽经类型无关的
委托调用，主文件族不拼 xlsx 家族类名）。

`delegate bool XlsxExportUiFn(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly);`


## double (delegate)

从实体读取小数键，用于声明了
`decimals > 0`（货币、汇率）的列：排序与合计保留小数。

`delegate double GridReal<T>(T row);`


## int (delegate)

从实体读取整数键，用于让数值列按值而非渲染字符串
排序 / 聚合。

`delegate int GridInt<T>(T row);`


## string (delegate)

从绑定实体中读取一列的显示文本：
`grid.Col("Name", 140, u => u.name)`。

`delegate string GridText<T>(T row);`


## string (delegate)

DataGrid 图片列的远端取回通道槽（与 Widget.Image 的 urlFetcher 同构）。
渲染遇到 http(s) 图片单元格时经 `Ensure` 走注册的实现；
未注册画加载失败占位，网络栈不进编译图。装配点在
Gui.ImageHttp.Install()（同时给 Widget.Image 注册）——需要远端
图片列的程序调它（或经 Html.Install 间接）。槽独立成类而不是
放进 partial class DataTable：谁拼 "DataTable" 谁拉 30 个 partial
分片，ImageHttp 装配时绝不能背全家。

`delegate string GridUrlEnsureFn(App app, string url);`


## string (delegate)

本地内存查询源。源保存自己的行快照，按 QueryViewport 同步交付一页，
不把当前页位置暴露为业务身份；无自定义键时使用带 temporary 标记的 legacy key。
`CellFieldLookup` 把列索引解析为稳定 Field 名（cell 增量寻址用）。

`delegate string CellFieldLookup(int col);`


## void (delegate)

把编辑后的值写回实体（就地编辑）。未提供时，
即使网格其他部分允许编辑，该列也只读显示。

`delegate void GridSet<T>(T row, string v);`
