# Gui.Component.DataTable

> 源码: `stdlib/Gui/Component/DataTable/DataGrid.zan`, `stdlib/Gui/Component/DataTable/DataTable.Columns.zan`, `stdlib/Gui/Component/DataTable/DataTable.Compute.zan`, `stdlib/Gui/Component/DataTable/DataTable.Edit.zan`, `stdlib/Gui/Component/DataTable/DataTable.Filter.zan`, `stdlib/Gui/Component/DataTable/DataTable.FilterUI.zan`, `stdlib/Gui/Component/DataTable/DataTable.Layout.zan`, `stdlib/Gui/Component/DataTable/DataTable.Overlays.zan`, `stdlib/Gui/Component/DataTable/DataTable.Render.zan`, `stdlib/Gui/Component/DataTable/DataTable.Rows.zan`, `stdlib/Gui/Component/DataTable/DataTable.Server.zan`, `stdlib/Gui/Component/DataTable/DataTable.Sort.zan`, `stdlib/Gui/Component/DataTable/DataTable.Value.zan`, `stdlib/Gui/Component/DataTable/DataTable.zan`, `stdlib/Gui/Component/DataTable/DataTableModel.zan`


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


## ChartBar (class)

选区图表浮层中的一根条：其类别标签和值。
取代旧的按索引对齐的 chartLabels/chartVals 并行列表。

- string label;

- int val;

- ChartBar(string l, int v)


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
cellType：0 文本，1 链接，2 标签，3 进度，4 徽章
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

- int deriveOp;

- int deriveA;

- int deriveB;

- int cfMode;

- int cfColor;

- int cfColor2;

- int cfThreshold;

- int fmt;

- bool grouped;

- string prefix;

- string suffix;

- int decimals;

- bool isDate;

- int dateOrder;

- int dateFmt;

- int dateGroup;

- bool isBool;

- int boolStyle;

- string boolTrue;

- string boolFalse;

- bool editable;

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

- DataColumn(string title, int width)

- static DataColumn Field(DataColumn c, string name)
  - 为列命名，使其可通过 `DataTable.Col(cols, name)` 访问
    而非字面索引。与其他构建器一样可组合：
    
    ```zan
    cols.Add(DataColumn.Field(DataColumn.Text("Name", 140), "name"));
    int ci = DataTable.Col(cols, "name");
    ```

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

- static DataColumn NotResizable(DataColumn c)
  - 将列标记为固定宽度：用户不能拖动其右边框，
    指针在（本应出现的）调整大小手柄上显示禁止光标。

- static DataColumn Text(string title, int width)

- static DataColumn Numeric(string title, int width)

- static DataColumn Link(string title, int width)

- static DataColumn TagCol(string title, int width, int tagType)

- static DataColumn ProgressCol(string title, int width)

- static DataColumn BadgeCol(string title, int width)

- static DataColumn Derived(string title, int width, int op, int a, int b)
  - 汇总由其他列聚合值派生的数值列
    （不逐行扫描）。`op` 为 0 百分比 / 1 比值 / 2 差值 / 3 和 / 4 积；
    `a` 和 `b` 是源列索引。

- static DataColumn Sum(DataColumn c)

- static DataColumn Avg(DataColumn c)

- static DataColumn Min(DataColumn c)

- static DataColumn Max(DataColumn c)

- static DataColumn Count(DataColumn c)


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

- GridColumn<T> RealCol(string title, int width, int decimals, GridReal<T> read)
  - 保留 `decimals` 位小数的数值列。

- GridColumn<T> BoolCol(string title, int width, GridBool<T> read)
  - 复选框 / 布尔列。

- GridColumn<T> DateCol(string title, int width, int order, GridText<T> read)
  - 日期列；`order` 指定存储文本的字段顺序
    （0 y-m-d，1 d-m-y，2 m-d-y）。

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

- override void BindEvent(string evt, Action a)
  - 把网格的语义事件转发到引擎 state 自身的 UiEvent
    字段（与链式 `OnRowClick`/… 订阅的队列相同），
    使 JSON/设计器的 `onRowClick` 处理器生效；其余事件回退
    到 `On` 上的通用事件集合。


## DataRow (class)

一行字符串单元格。先用 Create() 构建，再逐列 Push()。

- List<string> cells;

- DataRow()

- void Push(string v)

- string At(int i)

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


## DataTable (class)

DataTable 模块：列模型——字段名寻址、可见性与
自动适配、页脚聚合、固定/冻结及多级表头分组。

- static int Col(List<DataColumn> cols, string field)
  - 名为 `field` 的列的索引；没有列叫这个名字时返回 -1。
    未命名列（field == ""）永不匹配，空参数
    不会意外选中第一列。

- static bool HasCol(List<DataColumn> cols, string field)
  - `field` 是 `cols` 中某列名称时返回 true。

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

- static void ShowAllColumns(DataTableState st)

- static void SetAggregate(DataTableState st, int col, int mode)

- static void FreezeColumn(DataTableState st, int col, int side)

- static void PinColumn(DataTableState st, int col)

- static void PinColumnRight(DataTableState st, int col)

- static void UnpinColumn(DataTableState st, int col)

- static void TogglePin(DataTableState st, int col)
  - 让列在 无 → 左 → 右 → 无 之间循环，
    这正是单个菜单项或重复快捷键所需的行为。

- static int FreezeOf(DataTableState st, int col)

- static bool IsPinned(DataTableState st, int col)

- static bool IsPinnedRight(DataTableState st, int col)

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

- static List<BandSpan> BandSpans(DataTableState st, List<DataColumn> cols, List<int> vorder)
  - 所有跨区单元格，最外层在前。

- static bool BandIsCollapsed(DataTableState st, string path)

- static void SetBandCollapsed(DataTableState st, List<DataColumn> cols, string path, bool collapsed)
  - 把 band 折叠到第一列，或展开。折叠是隐藏
    组内其余列而非删除，因此它们的宽度、
    筛选和排序键在往返后依然保留。

- static void ToggleBand(DataTableState st, List<DataColumn> cols, string path)

- static void ApplyBandCollapse(DataTableState st, List<DataColumn> cols)
  - 从折叠集合重新推导 `colHidden`：折叠的 band 内
    除第一列外全部隐藏。
    
    `bandAutoHidden` 精确记录*本*机制隐藏的列，
    展开时只恢复这些列。否则用户手动隐藏的列
    会在下次折叠无关 band 时被悄悄恢复——
    隐藏是用户的决定，折叠不能
    悄悄覆盖它。

- static List<int> VisibleOrder(DataTableState st, List<DataColumn> cols)

- static int LeftFrozenCount(DataTableState st, List<int> vorder)
  - `vorder` 开头有多少列冻结在左缘。
    渲染流程用本函数和 RightFrozenCount 把顺序切成 [左 | 滚动 | 右]，
    因此一列绝不会在两个 band 中绘制。

- static int RightFrozenCount(DataTableState st, List<int> vorder)
  - `vorder` 末尾有多少列冻结在右缘。

- static void MoveColumn(DataTableState st, int from, int beforeCol)

- static int PinnedWidth(DataTableState st)

- static int PinnedRightWidth(DataTableState st)

- static int ColumnOffset(DataTableState st, List<int> vorder, int col, int dataViewW, int scrollX)
  - 给定滚动偏移后，列相对表格数据原点的屏幕 x——
    这里唯一知道冻结列忽略 `scrollX`、
    右冻结列从右缘往回测量的位置。
    
    `dataViewW` 是整个数据区域的宽度（不含边栏）。
    返回相对数据原点的偏移；调用方自行加上 firstDataX。

- static int MeasureColWidth(App app, List<DataColumn> cols, DataSource src, DataTableState st, int col)

- static void AutoFitColumn(App app, List<DataColumn> cols, DataSource src, DataTableState st, int col)

- static void AutoFitAll(App app, List<DataColumn> cols, DataSource src, DataTableState st)

- static bool RowPassesFilterRow(DataTableState st, DataSource src, int row)

- static bool RowPasses(DataTableState st, List<DataColumn> cols, DataSource src, int row)

- static bool RowPassesActive(DataTableState st, List<DataColumn> cols, DataSource src, List<int> af, int row, int today)


## DataTable (class)

DataTable 模块：派生显示——Top-N/集合过滤、行
分组、主从详情展开、可见显示列表、页脚
汇总与列统计。

- static void ApplySetPass(DataTableState st, List<DataColumn> cols, DataSource src, int c)
  - 从 `order` 中剔除某列筛选的集合相对阶段
    排除的每一行。与逐单元格阶段不同，它需要整个幸存
    集合，因此在行扫描后运行。

- static void SortRowsByReal(List<int> rows, DataSource src, int col, bool desc)

- static void Recompute(DataTableState st, List<DataColumn> cols, DataSource src)

- static int DeriveValue(DataColumn col, List<int> agg)

- static double DeriveReal(DataColumn col, List<double> agg)

- static bool IsGrouped(DataTableState st)

- static int GroupCount(DataTableState st)

- static void CollapseAll(DataTableState st)

- static void ExpandAll(DataTableState st)

- static void GroupBy(DataTableState st, int col)

- static void Ungroup(DataTableState st)

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

- static void ToggleCollapse(DataTableState st, string key)

- static void SetDetail(DataTableState st, DetailProvider p)
  - 挂接绘制详情面板的钩子以启用该功能。传入
    null 可关闭（所有行折叠；其余状态不受影响）。

- static bool HasDetail(DataTableState st)
  - 主从详情是否处于启用状态。

- static int DetailSlots(DataTableState st, DataSource src, int row)
  - 详情 band 的高度，以整行高计，至少 1。向
    provider 询问使每行不同大小（如每条子记录一个槽位）成为可能。

- static bool IsExpanded(DataTableState st, int row)
  - `row`（源索引）当前是否已展开。

- static bool CanExpand(DataTableState st, DataSource src, int row)
  - `row` 是否提供详情面板——功能关闭或
    provider 拒绝该行时返回 false，此时不绘制箭头。

- static void ExpandRow(DataTableState st, DataSource src, int row)
  - 打开 `row` 的详情面板。provider 拒绝的行或已打开的
    行保持不变。

- static void CollapseRow(DataTableState st, int row)
  - 关闭 `row` 的详情面板。未打开的行保持不变。

- static void ToggleDetail(DataTableState st, DataSource src, int row)
  - 切换 `row` 详情面板的展开与收起。

- static void CollapseAllDetail(DataTableState st)
  - 关闭所有已展开的详情面板。

- static int ExpandedCount(DataTableState st)
  - 当前有多少行的详情面板处于展开状态。

- static string PathKey(DataTableState st, List<DataColumn> cols, DataSource src, int rowIdx, int level)

- static string GroupValue(List<DataColumn> cols, DataSource src, int rowIdx, int gc)

- static int CmpGroupRow(DataTableState st, List<DataColumn> cols, DataSource src, int ra, int rb)

- static int CmpDateGroup(DataColumn col, int da, int db)

- static int WeekdayOf(int days)

- static void MergeGrp(DataTableState st, List<DataColumn> cols, DataSource src, int lo, int mid, int hi, List<int> tmp)

- static int CmpGroupCol(DataColumn gcol, DataSource src, int ra, int rb, int kd, int col)

- static void SortOrderGroupBucket(DataTableState st, List<DataColumn> cols, DataSource src)

- static void SortOrderGrouped(DataTableState st, List<DataColumn> cols, DataSource src)

- static void FillGroupAgg(DataTableState st, List<DataColumn> cols, DataSource src, GroupRow grp, int lo, int hi, bool toGrand)

- static bool GroupLevelEq(List<DataColumn> cols, DataSource src, int ra, int rb, int gc, int kd)

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

- static int DispLevel(DataTableState st, int i)
  - 组表头槽位的嵌套深度（其他类型为 0）。深度存放在
    该槽位指向的 `GroupRow` 上，而非显示槽位自身。

- static void BuildDisplay(DataTableState st, List<DataColumn> cols, DataSource src)

- static void EmitDetail(DataTableState st, DataSource src, int row)
  - `row` 展开时为其追加详情 band：`DetailSlots` 个
    类型 2 条目，每个指向主行并以 `part` 编号。
    折叠的行，或表中没有 provider 的行，不贡献任何内容——
    未展开的网格与功能加入前的表现完全一致。
    
    band 占用整行槽位而非任意像素高度，
    主体可用一次乘法在槽位与 Y 之间换算；见
    `DispRow`。

- static void ComputeSummary(DataTableState st, List<DataColumn> cols, DataSource src)

- static void FinalizeSummary(DataTableState st, List<DataColumn> cols, List<double> sumV, List<double> minV, List<double> maxV, int n)

- static void ComputeSummaryFromGroups(DataTableState st, List<DataColumn> cols)

- static void ResetGrandAgg(DataTableState st, int nc)

- static void ComputeColStats(DataTableState st, List<DataColumn> cols, DataSource src)


## DataTable (class)

DataTable 模块：单元格内编辑——开始/提交/取消、日历与
选项选择器、布尔切换和数值步进。

- static void BeginEdit(DataTableState st, List<DataColumn> cols, DataSource src, int rowIdx, int col)

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

DataTable 模块：筛选谓词——不区分大小写的文本匹配、
表头筛选菜单使用的逐列文本/比较/预设筛选。

- static int FoldC(int cc)

- static bool MatchAt(string hay, string needle, int off)

- static bool EqCI(string a, string b)

- static bool ContainsCI(string hay, string needle)

- static bool StartsCI(string hay, string needle)

- static bool EndsCI(string hay, string needle)

- static bool TextMatch(string v, string q, int mode)

- static string Backspace(string s)

- static bool CellPasses(ColFilter f, string v)

- static bool CellPassesCol(ColFilter f, DataColumn col, string v, int today)
  - 单个单元格的行级筛选阶段。`today` 是当前日号，
    传入以确保一次遍历的每行都基于同一时刻
    解析日期预设。Top-N 不在此判定：它需要整个集合。

- static bool CmpPasses(ColFilter f, DataColumn col, string v)

- static List<int> PresetRange(int preset, int today)
  - 日期预设覆盖的闭区间日范围，相对 `today`。
    预设关闭或该侧无界时返回 false；
    lo/hi 写入双元素列表（0 = lo，1 = hi）。

- static bool PresetPasses(ColFilter f, DataColumn col, string v, int today)

- static int Today()
  - 今天的日号，用于解析相对日期预设。

- static int CompareCell(string a, string b, bool numeric)

- static int CompareCellCol(DataColumn col, string a, string b)

- static string Truncate(string text, int fontSize, int maxW)

- static int TypeColor(App app, int type)


## DataTable (class)

DataTable 模块：表头筛选 UI——DevExpress 风格的分页筛选
弹窗（值/规则/日历）、行内编辑器和集合筛选菜单。

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

- static bool IconBtn(App app, int bx, int by, int bw, int bh, string icon, bool active)
  - 纯图标工具栏按钮，带"激活"（按下）外观。

- static bool TextBtn(App app, int bx, int by, int bw, int bh, string label, bool primary)
  - 带标签的工具栏按钮，`primary` 时用强调色填充。

- static void EditBox(App app, int bx, int by, int bw, int bh, string text, string ph, bool focused)

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

- static int RenderRulesTab(App app, int mx, int cy, int menuW, int pad, ColFilter f, DataColumn col, DataTableState st)

- static void RenderMenu(App app, int x, int y, int viewW, int headerH, int numW, int selW, List<DataColumn> cols, DataSource src, DataTableState st)

- static void DrawMiniCheck(App app, int bx, int cy, int itemH, bool checked)


## DataTable (class)

DataTable 布局持久化：用户手工调整出的视图状态——
列宽、顺序、可见性、冻结、排序键、分组、逐列
聚合及完整筛选栈——序列化为 JSON 并读回。

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
  - 展开的主从详情行（扁平化时仍逐个经 CanExpand 审核）。

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

DataTable 模块：浮动浮层——右键行上下文菜单、
选区生成图表浮层和列表头上下文菜单。

- static void CtxItem(App app, int mx, int my, int mw, int ih, string label, string accel)

- static int AggItem(App app, int mx, int iy, int mw, int ih, DataTableState st, int col, int mode, string label, int curAgg)

- static bool CtxHit(App app, int mx, int my, int mw, int ih)

- static void Flash(DataTableState st, string msg)

- static void RenderContextMenu(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)

- static void RenderChart(App app, int x, int y, int viewW, int viewH, DataTableState st)

- static void RenderHeaderMenu(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)


## DataTable (class)

DataTable 模块：单元格渲染（条件格式、单元格类型）
与虚拟化的主渲染路径（表头 / 主体 / 页脚 /
交互）。

- static void DrawGrip(App app, int bx, int by, int bw, int bh, int color)
  - 在 bx,by,bw,bh 内居中画一个 2×3 点阵的拖动手柄（列头 /
    行号的专用拖动按钮）。用点阵而不是字体图标，
    是因为图标集里没有 gripper 码点，而点阵在小尺寸下
    也不会糊成一团。

- static void DrawCell(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride)
  - `fgOverride` 是 state 的 CellStyler 给出的打包颜色，0 表示
    无。它替换普通和链接单元格的主题文本色，但
    条件格式颜色仍然优先——在那里，颜色*就是*
    单元格携带的信息。

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

- static void RenderGroupPanel(App app, int x, int y, int viewW, int h, List<DataColumn> cols, DataTableState st, bool headerDrag)
  - 表头上方的分组条带：每个分组列一个芯片
    （由外到内），带移除按钮；有内容拖过时显示插入指示，
    未分组时显示提示。
    列头被拖动期间 `headerDrag` 为 true，条带
    可标示自己为放置目标。

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

- static void EnsureInit(DataTableState st, List<DataColumn> cols, DataSource src)

- static void Journal(DataTableState st, RowEdit e)
  - 将可逆更改压入日志；达到上限时丢弃最旧的条目
    。撤销本身产生的写入不会被记录——
    否则撤销会立刻变成自己的重做。

- static void BeginUndoGroup(DataTableState st)
  - 开始一次复合更改：到 EndUndoGroup 为止记录的所有内容
    在单次撤销中一起回退。用于粘贴、范围内删除和
    删除选中行等一次操作涉及多单元格/多行的情况。

- static void EndUndoGroup(DataTableState st)

- static int UndoDepth(DataTableState st)
  - 日志还能承受的 Ctrl+Z 次数。分组条目
    只计一次，因为它们会一起回退。

- static void ClearUndo(DataTableState st)
  - 清空日志（例如保存后）。

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
    忘记该行后仍可撤销。

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

- static void UndoOne(DataTableState st, List<DataColumn> cols, DataSource src, RowEdit e)
  - 回退一条日志条目。假定 st.undoing 已设置。


## DataTable (class)

DataTable 分部：排序与外观——条件格式颜色
辅助、外观钩子、归并排序与多列排序次序。

- static int Rgb(int r, int g, int b)

- static int ColorAlpha(int packed, int a)

- static int LerpColor(int c0, int c1, int num, int den)

- static int RowBackground(DataTableState st, DataSource src, int row, int stripeBg, int activeBg, int hoverBg, bool sel, bool hover)
  - 数据行的背景。styler 的颜色优先于斑马纹
    默认色，但选中和悬停是对它*染色*而非替换——
    各半混合让样式行在激活时仍能保持
    其含义清晰，而不是在光标下悄悄失去颜色。

- static int CellBackground(DataTableState st, DataSource src, int row, int col)
  - 单个单元格的背景，0 则露出行背景。

- static int CellForeground(DataTableState st, DataSource src, int row, int col)
  - 单个单元格的文本颜色，0 用主题默认色。单元格级颜色
    覆盖行级颜色；行级颜色应用于所有未设颜色的单元格。

- static void MergeKeyed(int kind, int dir, List<int> order, List<int> nkey, List<string> skey, List<double> rkey, int lo, int mid, int hi, List<int> otmp, List<int> ntmp, List<string> stmp, List<double> rtmp)

- static void SyncPrimary(DataTableState st)

- static int SortIndexOf(DataTableState st, int col)

- static void SortBy(DataTableState st, int col, int dir)
  - 用单个键替换排序（dir：1 升序，2 降序）。col<0 清除排序。

- static void AddSort(DataTableState st, int col, int dir)
  - 追加（或重定向）一个次要排序键，不影响其他键。

- static void ClearSort(DataTableState st)

- static void CycleSort(DataTableState st, int col, bool additive)

- static int CmpMultiKey(DataTableState st, List<DataColumn> cols, DataSource src, int ra, int rb)

- static void MergeMulti(DataTableState st, List<DataColumn> cols, DataSource src, int lo, int mid, int hi, List<int> tmp)

- static void SortOrderMulti(DataTableState st, List<DataColumn> cols, DataSource src)

- static void SortOrder(DataTableState st, List<DataColumn> cols, DataSource src)

- static void SortOrderByCol(DataTableState st, List<DataColumn> cols, DataSource src, int sc, int dir)

- static int DistinctCapacity(int n)

- static int DistinctHash(string val)

- static bool DistinctAdd(List<string> vals, List<int> slots, string val)

- static void MergeStr(List<string> src, int lo, int mid, int hi, List<string> tmp)

- static void SortStr(List<string> a)

- static List<string> Distinct(DataSource src, int col)

- static List<string> DistinctFiltered(DataTableState st, DataSource src, int col)


## DataTable (class)

DataTable 分部：值解析、数字/实数/bool/日期格式化与
日历计算——原始单元格字符串与其类型化/显示形式之间的纯静态转换。
此处不触碰任何表格状态。

- static int CompareStr(string a, string b)

- static bool IsIntStr(string s)

- static int ParseInt(string s)

- static double Pow10(int n)

- static double ParseReal(string s)
  - 宽松的实数解析：可选的前导 '-'、十进制数字和至多一个
    '.'；千位分隔符、货币符号和单位后缀会被
    跳过，因此“¥1,234.56”和“-98%”都能正确解析。“”视为 0。

- static string PadFrac(int fp, int decimals)

- static string FormatReal(double v, int decimals)
  - 将 double 渲染为恰好 `decimals` 位小数（四舍五入）。
    `Convert.ToString(double)` 只有约 6 位有效数字，因此
    网格从不用它处理单元格值。

- static bool ParseBool(string s)
  - 将单元格读作真值。“1”、“true”、“yes”、“y”、“on”、
    “t”和勾号视为 true；其他任何值（包括“”、“0”和“false”）
    视为 false。不区分大小写且容忍首尾空格，
    因此混用“Yes”/“TRUE”/“1”的列也能一致地读取。

- static string TrimSpace(string s)

- static string LowerAscii(string s)

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

- static string DateGroupKey(int days, int dateGroup)
  - 按 `dateGroup` 对日期单元格分组的分桶标签：
    0 = 精确到日，1 = 年，2 = 年月，3 = 季度，4 = 星期。
    按月分组会把一个月的每一天合并到一个标题下。

- static string DayName(int dow)

- static string GroupDigits(string ipart)

- static string FormatCell(DataColumn col, string raw)

- static bool IsNumericStr(string s)

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

- static int YmMonth(int ym)

- static int MonthFirstDow(int ym)
  - 某月 1 号的星期几，0 = 周日。纪元第 0 天是
    星期四，因此有 +4。

- static List<int> MonthCells(int ym)
  - 月网格的 42 个单元格以日号表示，属于
    相邻月份处为 0。固定六行，网格永不回流。


## DataTable (class)

企业级数据网格：固定行号与选择列、可排序
表头、Excel 式集合筛选表头菜单、可调列宽、自定义单元格
渲染器（文本/链接/标签/进度/徽章）和虚拟化行（只绘制
可见窗口，数万行也能保持流畅）。

数据通过 `DataSource` 提供（List<DataRow> 便捷重载见 `Render`，
或通过 `RenderSource` 传入自定义虚拟源）。

- static int MinI(int a, int b)

- static int MaxI(int a, int b)

- static bool HasRange(DataTableState st)

- static string CsvField(string v)

- static string BuildText(DataTableState st, List<DataColumn> cols, DataSource src, bool csv, bool withHeader)

- static void SelectAllRows(DataTableState st, bool val)

- static void SelectRowSpan(DataTableState st, int a, int b)
  - 把 `a`..`b`（顺序位置，可反向）之间的数据行设为唯一选中集合。
    行号列上的拖动多选与 Shift 点击都走这里。

- static void PickRow(DataTableState st, int oi, int mods)
  - 行号列上的按行选择：Shift 连续扩展、Ctrl 增删、
    其余情况单选并把锚点落在该行。

- static int PrimarySelected(DataTableState st)
  - 第一个选中行的源行索引；无选中时为 -1。
    这是通过 `selection` 绑定暴露的标量。

- static void SetPrimarySelected(DataTableState st, int row)
  - 独占选中单个源行（清除其他所有选择）。
    row < 0 或越界会清除选择。当模型侧变化时，
    由 `selection` 绑定使用。

- static void SyncBindings(DataTableState st)
  - 每帧将可选的 `selection` / `page` 绑定与实时状态
    对账一次。与 Select 类似，上次同步后变化的一侧
    胜出，因此编辑双向流动：model -> table 与 table -> model。
    在 RenderSource 开头、选择列表定好大小之后调用。

- static int ParseIntLoose(string s)

- static bool BuildChartFromRange(DataTableState st, List<DataColumn> cols, DataSource src)

- static void ClearRange(DataTableState st)

- static void MoveOrder(DataTableState st, int from, int to)

- static int NextDataSlot(DataTableState st, int from, int step)
  - `from` 及其后、承载数据行的显示槽位，按
    `step` 步进。跳过分组标题，方向键在单元格间移动
    而不会卡在标题上。走完返回 -1。

- static int FirstDataSlot(DataTableState st)

- static int LastDataSlot(DataTableState st)

- static int VisPos(List<int> vorder, int col)
  - 可见列 `col` 在 `vorder` 中的位置；列被
    隐藏时为 -1。导航按可见顺序移动，隐藏或
    重排的列不会困住光标。

- static int VisPos(List<FrameColumn> columns, int col)

- static bool IsNavKey(int kc)

- static List<int> NavTarget(DataTableState st, List<int> vorder, int visRows, int kc, int emods)
  - 导航键应把光标放到哪里：两元素列表，依次为
    目标显示槽位 (0) 和列索引 (1)；网格无处可去时为 -1/-1。
    纯决策逻辑，独立于渲染路径，
    可在无界面环境下测试。
    
    行按显示槽位移动以跳过分组标题；列
    按可见顺序位置移动，隐藏和重排的列也能正常处理。

- static List<int> NavTarget(DataTableState st, List<FrameColumn> columns, int visRows, int kc, int emods)

- static bool MoveCursor(DataTableState st, int oi, int col, bool extend)
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

- static string CopyText(DataTableState st, List<DataColumn> cols, DataSource src)
  - 当前单元格范围（或选择）的制表符分隔文本。
    这就是 Ctrl+C 复制到剪贴板的内容。

- static int PasteText(DataTableState st, List<DataColumn> cols, DataSource src, string text)
  - 从光标处开始向网格粘贴制表符分隔文本，向下
    向右填充。只有可编辑且非 bool 的列接受值；bool
    列通过 ParseBool 读取粘贴文本，“Yes”/“1”都能落库。
    返回实际写入的单元格数。

- static int ClearRangeCells(DataTableState st, List<DataColumn> cols, DataSource src)
  - 清空当前范围内的所有可编辑单元格（Delete 键）。
    返回清空的单元格数。

- static List<string> SplitLines(string s)
  - 按 '\n' 分割并去掉 '\r'——剪贴板文本的
    换行符因复制来源而异。

- static List<string> SplitTabs(string s)


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

- int rangeR0;

- int rangeC0;

- int rangeR1;

- int rangeC1;

- bool rangeDrag;

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

- bool showFilterRow;

- List<string> filterRowText;

- int filterRowFocusCol;

- string filterRowBuf;

- int filterRowCur;

- bool showStatusBar;

- bool chartOpen;

- string chartTitle;

- List<ChartBar> chartBars;

- int chartMax;

- List<int> groupCols;

- List<string> collapsedKeys;

- List<DispRow> dispRows;

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

- DetailProvider detail;

- List<int> expandedRows;

- List<RowEdit> undoLog;

- int undoLimit;

- bool allowAddRow;

- bool undoing;

- int lastAddedRow;

- int undoGroup;

- int undoGroupSeq;

- DataTableState()

- int HitRow()

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

- DispRow(int k, int r, int n)


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

- string Raw(T row)
  - 引擎用于显示、排序
    键和导出的原始（未格式化）单元格字符串。引擎在此基础上应用列的显示格式
    （千分位 / 前缀 / 后缀 / 日期 / bool），因此原始
    值保持可解析。未提供文本读取器时，则
    从类型化访问器推导。


## GridSource (class)

将 `List<T>` 及各列访问器适配为引擎的 `DataSource`，
使强大的 DataTable 引擎（排序 / 筛选 / 分组 / 聚合 / 编辑 /
虚拟化）能原样作用于调用方自己的对象。不复制任何数据：
每帧都从活动列表读取单元格。

- List<T> data;

- List <GridColumn<T>> cols;

- GridNew<T> factory;
  - 空白行实体工厂；null 使数据源保持只读。

- override int RowCount()

- bool Row(int row)
  - 行号在当前这份数据里有效。引擎按上一次重算出来的行号集合
    （`st.order`）取单元格，绑定的列表在那之后缩短过的话行号就会越界；
    读单元格是每帧的事，越界一次就是整个程序退出，所以这里按列一样挡住。

- override string CellText(int row, int col)

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

- GroupRow(string lb, int lv, int cnt, string k, bool coll)


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

- override void RequestNextBlock()
  - 获取已加载前缀之后的下一块。网格在
    用户滚动到底部时调用：它覆盖参差边界的情况（
    前缀在块中间结束，下一次获取补全该块）以及
    块对齐的情况（前缀正好在边界结束，下一次
    获取从新块开始）。

- void StartFetch(int start)

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

- virtual int FillBlock(int start, int count, List<DataRow> dst)
  - 从 `start` 开始取最多 `count` 行，每行追加一个 DataRow
    到 `dst`。返回追加的数量；少于 `count` 表示
    服务器没有更多行了（此时固定总数）。同步
    数据源只需实现这一个方法。

- override string CellText(int row, int col)

- override int CellNum(int row, int col)

- override double CellReal(int row, int col)

- override int CellDay(int row, int col, int order)

- override bool CellBool(int row, int col)

- override void SetCell(int row, int col, string v)


## SortKey (class)

一个生效的排序键：列索引及其方向（1 升序，2 降序）。
取代旧的按索引对齐的 sortCols/sortDirs 并行列表。

- int col;

- int dir;

- SortKey(int c, int d)


## T (delegate)

为"新行"占位符和右键菜单插入命令创建空白行实体
（`() => new User()`）。泛型前端无法
自行 `new T()`，因此这是让 AllowAddRow 真正生效的关键；
缺少它时网格行数保持只读。

`delegate T GridNew<T>();`


## bool (delegate)

从实体读取布尔值，用于复选框 / bool 列。

`delegate bool GridBool<T>(T row);`


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


## void (delegate)

把编辑后的值写回实体（就地编辑）。未提供时，
即使网格其他部分允许编辑，该列也只读显示。

`delegate void GridSet<T>(T row, string v);`
