# Gui.Component.DataTable

> 源码: `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataGrid.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.CfClear.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.ColumnChooser.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Columns.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.CompCell.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Compute.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.DataSource.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Diagnostics.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Edit.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Export.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.ExportUi.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.ExportXlsx.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Filter.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.FilterBuilder.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.FilterDescribe.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.FilterUI.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Formula.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.HttpSource.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Identity.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.ImageSlot.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Lang.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Layout.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.LocalSource.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.MasterDetail.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Overlays.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.PivotView.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Query.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.QueryPlan.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.QueryRequest.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.QueryResult.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Realtime.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Render.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.RowCache.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Rows.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Schema.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Search.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Selection.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Server.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Sort.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Transaction.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.TransactionRequest.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Transpose.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.Value.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.WidgetComp.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTable.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTableBootstrap.zan`, `packages/Zan.Gui.DataTable/src/Gui/Component/DataTable/DataTableModel.zan`


## BandGridComp (class)

- List<string> poolKeys;

- List<Control> poolCtl;

- List<int> poolShape;

- BandGridComp():base("bandgrid")

- override Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- override void Fill(Control ctl, List<int> vals, int rows, int cols, string heatBand)

- override bool CompPos(Control ctl, ref int r, ref int c)

- override void Reset()


## BandSpan (class)

- string label;

- string path;

- int level;

- int from;

- int to;

- BandSpan(string l, string p, int lv, int f, int tt)


## ButtonsComp (class)

- List<WidgetPoolSlot> pool;

- string lastLabel;

- bool lastAction;

- ButtonsComp():base("buttons")

- override Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- override void FillStr(Control ctl, string val, DataColumn col, DataSource src, int row)

- override bool TakeCommit(Control ctl, ref string outVal)

- override bool CompPos(Control ctl, ref int r, ref int c)

- override void Reset()


## CardDetail (class)

- public List<CardDetailItem> items;

- public int maxSlots;

- CardDetail()

- static CardDetail Of()

- CardDetail Add(string label, string value)

- override int Slots(DataSource src, int row)

- override void Paint(App app, DataSource src, int row, int x, int y, int w, int h)


## CardDetailItem (class)

- public string label;

- public string value;

- public CardDetailItem(string label, string value)


## CellComp (class)

- public string id;

- CellComp(string id)

- virtual Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- virtual void Fill(Control ctl, List<int> vals, int rows, int cols, string heatBand)

- virtual void FillStr(Control ctl, string val, DataColumn col, DataSource src, int row)

- virtual bool TakeCommit(Control ctl, ref string outVal)

- virtual bool CompPos(Control ctl, ref int r, ref int c)

- virtual void Reset()

- static List<CellComp> registry;

- static readonly int PoolCap=2048;

- static void Register(CellComp comp)

- static CellComp Find(string id)


## CellStyler (class)

- virtual int RowBg(DataSource src, int row)

- virtual int RowFg(DataSource src, int row)

- virtual int CellBg(DataSource src, int row, int col)

- virtual int CellFg(DataSource src, int row, int col)

- virtual bool PaintCell(App app, DataSource src, int row, int col, int x, int y, int w, int h)


## CellWidgetProvider (class)

- virtual Control Provide(App app, DataSource src, int row, int col, int x, int y, int w, int h)

- virtual void Reset()


## CfClearItem (class)

- public int column;

- public bool selected;

- public CfClearItem(int column, bool selected)


## ChartBar (class)

- string label;

- int val;

- ChartBar(string l, int v)


## CheckboxComp (class)

- List<WidgetPoolSlot> pool;

- DataColumn lastCol;

- bool lastOn;

- CheckboxComp():base("checkbox")

- override Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- override void FillStr(Control ctl, string val, DataColumn col, DataSource src, int row)

- override bool TakeCommit(Control ctl, ref string outVal)

- override void Reset()


## ChildGridDetail (class)

- public List<DataColumn> childCols;

- public DataSource childSrc;

- public int childKeyCol;

- public int masterKeyCol;

- public int maxSlots;

- public List<int> rows=new List<int>();

- ChildGridDetail(List<DataColumn> cols, DataSource src, int keyCol)

- static ChildGridDetail Of(List<DataColumn> cols, DataSource src, int keyCol)

- public int ChildCount(DataSource src, int row)

- override bool CanExpand(DataSource src, int row)

- override int Slots(DataSource src, int row)

- override void Paint(App app, DataSource src, int row, int x, int y, int w, int h)


## ColFilter (class)

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

- void Reset()


## ColumnAggregateState (class)

- public int mode;

- public int value;

- public double realValue;

- public string summaryText;

- public ColumnAggregateState()

- public ColumnAggregateState(int mode, int value, double realValue, string summaryText)


## ColumnCfExtents (class)

- public int min;

- public int max;

- public double minReal;

- public double maxReal;

- public ColumnCfExtents()

- public ColumnCfExtents(int min, int max, double minReal, double maxReal)


## ColumnCfRule (class)

- public int mode;

- public int color;

- public int color2;

- public int threshold;

- public bool rankPct;

- public bool off;

- public ColumnCfRule()

- public static ColumnCfRule Create()


## DataColumn (class)

- string title;

- string field;

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

- string FieldName()

- static DataColumn Describe(DataColumn c, string text)

- static DataColumn Editable(DataColumn c)

- static DataColumn Dropdown(DataColumn c, List<string> items, bool strict)

- static DataColumn DatePicker(DataColumn c)

- static DataColumn Spinner(DataColumn c, double step)

- static DataColumn Required(DataColumn c)

- static DataColumn MaxLength(DataColumn c, int n)

- static DataColumn Range(DataColumn c, double lo, double hi)

- static DataColumn MinValue(DataColumn c, double lo)

- static DataColumn MaxValue(DataColumn c, double hi)

- static DataColumn Pin(DataColumn c)

- static DataColumn PinRight(DataColumn c)

- static DataColumn Band(DataColumn c, string path)

- static DataColumn Decimals(DataColumn c, int n)

- static DataColumn Real(string title, int width, int decimals)

- static DataColumn Formula(string title, int width, string expr)

- static DataColumn Formula(DataColumn c, string expr)

- static DataColumn AsDate(DataColumn c, int order)

- static DataColumn Date(string title, int width)

- static DataColumn DateFormat(DataColumn c, int fmt)

- static DataColumn DateGroup(DataColumn c, int mode)

- static DataColumn AsBool(DataColumn c, int style)

- static DataColumn Bool(string title, int width)

- static DataColumn BoolText(DataColumn c, string yes, string no)

- static DataColumn Money(DataColumn c, string symbol)

- static DataColumn Percent(DataColumn c)

- static DataColumn Grouped(DataColumn c)

- static DataColumn Affix(DataColumn c, string prefix, string suffix)

- static DataColumn DataBar(DataColumn c, int color)

- static DataColumn ColorScale(DataColumn c, int lowColor, int highColor)

- static DataColumn HighlightGreater(DataColumn c, int threshold, int color)

- static DataColumn HighlightLess(DataColumn c, int threshold, int color)

- static DataColumn TopRules(DataColumn c, int countOrPct, int color)

- static DataColumn BottomRules(DataColumn c, int countOrPct, int color)

- static DataColumn NotResizable(DataColumn c)

- static DataColumn Merge(DataColumn c)

- static DataColumn Wrap(DataColumn c, int maxLines)

- static DataColumn Text(string title, int width)

- static DataColumn Numeric(string title, int width)

- static DataColumn Link(string title, int width)

- static DataColumn TagCol(string title, int width, int tagType)

- static DataColumn ProgressCol(string title, int width)

- static DataColumn BadgeCol(string title, int width)

- static DataColumn ImageCol(string title, int width)

- static DataColumn IconCol(string title, int width)

- static DataColumn ButtonCol(string title, int width, string icon, string cls)

- static DataColumn SparklineCol(string title, int width, bool area)

- static DataColumn SparkKind(DataColumn c, int kind)

- static DataColumn SparkColors(DataColumn c, int posColor, int negColor)

- static DataColumn CompCol(string title, int width, string compId)

- static DataColumn Shape(DataColumn c, int rows, int cols)

- static DataColumn WidgetCol(string title, int width, string compId)

- static DataColumn Options(DataColumn c, string csv)

- static DataColumn Actions(DataColumn c)

- static DataColumn VTop(DataColumn c)

- static DataColumn Heat(DataColumn c, string band)

- static bool HeatBands(string name, ref int lo, ref int hi)

- static DataColumn Derived(string title, int width, int op, int a, int b)

- static void Derive(DataColumn c, int op, int a, int b)

- static DataColumn Sum(DataColumn c)

- static DataColumn Avg(DataColumn c)

- static DataColumn Min(DataColumn c)

- static DataColumn Max(DataColumn c)

- static DataColumn Count(DataColumn c)

- static DataColumn CountSel(DataColumn c)

- static DataColumn SummaryFmt(DataColumn c, string fmt)


## DataGrid (class)

- List <GridColumn<T>> gcols;

- List<T> data;

- DataTableState st;

- GridSource<T> source;

- List<DataColumn> descs;

- int boundN;

- void InitGrid()

- DataGrid()

- override string Kind()

- override string StyleType()

- DataGrid<T> Bind(List<T> src)

- DataGrid<T> KeyBy(GridKey<T> provider)

- DataGrid<T> Refresh()

- int Count()

- DataTableState State()

- GridColumn<T> Add(GridColumn<T> col)

- GridColumn<T> Col(string title, int width, GridText<T> read)

- GridColumn<T> NumCol(string title, int width, GridInt<T> read)

- GridColumn<T> CompCol(string title, int width, string compId, GridNums<T> read)

- GridColumn<T> WidgetCol(string title, int width, string compId, GridText<T> read)

- GridColumn<T> RealCol(string title, int width, int decimals, GridReal<T> read)

- GridColumn<T> BoolCol(string title, int width, GridBool<T> read)

- GridColumn<T> DateCol(string title, int width, int order, GridText<T> read)

- GridColumn<T> ImageCol(string title, int width, GridText<T> read)

- GridColumn<T> IconCol(string title, int width, GridText<T> read)

- GridColumn<T> ButtonCol(string title, int width, string icon, string cls, GridText<T> read)

- GridColumn<T> SparklineCol(string title, int width, bool area, GridText<T> read)

- DataGrid<T> RowNumbers(bool on)

- DataGrid<T> Selectable(bool on)

- DataGrid<T> Striped(bool on)

- DataGrid<T> RowHeight(int px)

- DataGrid<T> RowReorder(bool on)

- DataGrid<T> Summary(bool on)

- DataGrid<T> FilterRow(bool on)

- DataGrid<T> StatusBar(bool on)

- DataGrid<T> GroupPanel(bool on)

- DataGrid<T> ExternalRowMenu(bool on)

- DataGrid<T> AllowAddRow(bool on)

- DataGrid<T> HideCol(string field, bool hidden)

- DataGrid<T> ColumnChooser(bool open)

- bool IsColumnChooserOpen()

- DataGrid<T> RowFactory(GridNew<T> f)

- DataGrid<T> OnRowClick(Action a)

- DataGrid<T> OnRowContext(Action a)

- DataGrid<T> OnRowDoubleClick(Action a)

- DataGrid<T> OnCellClick(Action a)

- DataGrid<T> OnCellEdit(Action a)

- DataGrid<T> OnSelectionChanged(Action a)

- DataGrid<T> OnSort(Action a)

- DataGrid<T> OnFilterChanged(Action a)

- void SyncDescs()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override List<PropSpec> Props()

- void SetPreviewColumns(string spec)

- string PreviewColumnsText()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- string HitField()

- override void BindEvent(string evt, Action a)


## DataPageCache (class)

- int byteBudget;

- int bytes;

- int clock;

- List<DataPageCacheEntry> entries;

- DataPageCache(int budget)

- int ByteBudget()

- int Bytes()

- int Count()

- int Find(DataPageKey key)

- QueryResult Get(DataPageKey key)

- QueryResult GetRow(QueryDescriptor query, int generation, int schema, int server, int row)

- void SetPinned(DataPageKey key, bool value)

- int OldestEvictableExcept(int excluded)

- int EvictableBytesExcept(int excluded)

- int OldestEvictable()

- bool EvictOne()

- bool Put(DataPageKey key, QueryResult result, int byteSize)

- void Clear()


## DataPageCacheEntry (class)

- DataPageKey key;

- QueryResult result;

- int bytes;

- bool pinned;

- int lastUse;

- DataPageCacheEntry(DataPageKey pageKey, QueryResult page, int byteSize, int stamp)

- DataPageKey Key()

- QueryResult Result()

- int Bytes()

- bool Pinned()

- int LastUse()


## DataPageKey (class)

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

- static DataPageKey Of(QueryDescriptor query, QueryViewport viewport, QueryRequestToken token)

- static DataPageKey For(QueryDescriptor query, QueryViewport viewport, int gen, int schema, int server)

- string Dataset()

- string QueryText()

- int QueryHash()

- int SchemaVersion()

- int ServerRevision()

- int Generation()

- int Start()

- int Size()

- string Cursor()

- bool Same(DataPageKey other)


## DataRow (class)

- List<string> cells;

- DataRow()

- void Push(string v)

- string At(int i)

- int Count()

- DataRow Copy()

- void Set(int i, string v)


## DataSource (class)

- virtual int RowCount()

- virtual string CellText(int row, int col)

- virtual int CellNum(int row, int col)

- virtual double CellReal(int row, int col)

- virtual int CellDay(int row, int col, int order)

- virtual bool CellBool(int row, int col)

- virtual RowKey GetRowKey(int row)

- virtual void SetCell(int row, int col, string v)

- virtual bool CanInsert()

- virtual bool InsertRow(int at)

- virtual bool RemoveRow(int at)

- virtual bool ServerControlled()

- virtual bool CellReady(int row, int col)

- virtual void RequestBlock(int row)

- virtual void RequestNextBlock()

- virtual bool HasMore()

- virtual bool Poll()

- virtual DeltaResult ApplyDeltas(DeltaBatch batch)


## DataTable (class)

- static int CfEffectiveMode(DataTableState st, DataColumn dc, int col)

- static string CfRuleLabel(DataTableState st, DataColumn dc, int col)

- static int CfRuleColor(DataTableState st, DataColumn dc, int col)

- static void OpenCfClear(DataTableState st, List<DataColumn> cols)

- static void ApplyCfClear(DataTableState st, bool all)

- static void RenderCfClear(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataTableState st)

- static void CfClearBtn(App app, Canvas c, Theme t, int bx, int by, int bw, int bh, string label, string cls)


## DataTable (class)

- static void SetColumnChooser(DataTableState st, bool open)

- static bool IsColumnChooserOpen(DataTableState st)

- static List<int> ChooserOrder(DataTableState st, List<DataColumn> cols)

- static void ChooserToggle(DataTableState st, List<DataColumn> cols, int col, bool visible)

- static void ChooserMove(DataTableState st, List<DataColumn> cols, int col, bool left)

- static int ChooserRowH(App app)

- static void RenderColumnChooser(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)


## DataTable (class)

- static int Col(List<DataColumn> cols, string field)

- static bool HasCol(List<DataColumn> cols, string field)

- static bool IsActionCol(List<DataColumn> cols, int col)

- static bool IsWidgetCol(List<DataColumn> cols, int col)

- static string Cell(DataSource src, List<DataColumn> cols, int row, string field)

- static double CellValue(DataSource src, List<DataColumn> cols, int row, string field)

- static void SetCellField(DataSource src, List<DataColumn> cols, int row, string field, string v)

- static void SortByField(DataTableState st, List<DataColumn> cols, string field, int dir)

- static void AddSortField(DataTableState st, List<DataColumn> cols, string field, int dir)

- static void GroupByField(DataTableState st, List<DataColumn> cols, string field)

- static void SetAggregateField(DataTableState st, List<DataColumn> cols, string field, int mode)

- static void PinField(DataTableState st, List<DataColumn> cols, string field, bool on)

- static void HideField(DataTableState st, List<DataColumn> cols, string field, bool hidden)

- static string HitField(DataTableState st, List<DataColumn> cols)

- static string EditField(DataTableState st, List<DataColumn> cols)

- static int HiddenCount(DataTableState st)

- static void ShowAllColumns(DataTableState st)

- static void ResetColumns(DataTableState st, List<DataColumn> cols)

- static void SetAggregate(DataTableState st, int col, int mode)

- static void FreezeColumn(DataTableState st, int col, int side)

- static void PinColumn(DataTableState st, int col)

- static void PinColumnRight(DataTableState st, int col)

- static void UnpinColumn(DataTableState st, int col)

- static void TogglePin(DataTableState st, int col)

- static int FreezeOf(DataTableState st, int col)

- static bool IsPinned(DataTableState st, int col)

- static bool IsPinnedRight(DataTableState st, int col)

- static void FreezeField(DataTableState st, List<DataColumn> cols, string field, int side)

- static string BandOf(List<DataColumn> cols, int col)

- static string BandPrefix(string path, int depth)

- static int BandDepth(string path)

- static int BandLevels(List<DataColumn> cols, List<int> vorder)

- static List<BandSpan> BandRow(DataTableState st, List<DataColumn> cols, List<int> vorder, int level)

- static List<BandSpan> BandRow(DataTableState st, List<DataColumn> cols, List<FrameColumn> columns, int level)

- static List<BandSpan> BandSpans(DataTableState st, List<DataColumn> cols, List<int> vorder)

- static bool BandIsCollapsed(DataTableState st, string path)

- static void SetBandCollapsed(DataTableState st, List<DataColumn> cols, string path, bool collapsed)

- static void ToggleBand(DataTableState st, List<DataColumn> cols, string path)

- static void ApplyBandCollapse(DataTableState st, List<DataColumn> cols)

- static List<int> VisibleOrder(DataTableState st, List<DataColumn> cols)

- static int LeftFrozenCount(DataTableState st, List<int> vorder)

- static int RightFrozenCount(DataTableState st, List<int> vorder)

- static void MoveColumn(DataTableState st, int from, int beforeCol)

- static int PinnedWidth(DataTableState st)

- static int PinnedRightWidth(DataTableState st)

- static int ColumnOffset(DataTableState st, List<int> vorder, int col, int dataViewW, int scrollX)

- static int MeasureColWidth(App app, List<DataColumn> cols, DataSource src, DataTableState st, int col)

- static void AutoFitColumn(App app, List<DataColumn> cols, DataSource src, DataTableState st, int col)

- static void AutoFitAll(App app, List<DataColumn> cols, DataSource src, DataTableState st)

- static bool RowPassesFilterRow(DataTableState st, List<DataColumn> cols, DataSource src, int row)

- static bool RowPasses(DataTableState st, List<DataColumn> cols, DataSource src, int row)

- static bool RowPassesActive(DataTableState st, List<DataColumn> cols, DataSource src, List<int> af, int row, int today)

- static string CellTextRouted(List<DataColumn> cols, DataSource src, int row, int col)


## DataTable (class)

- static bool compsInited;

- static int compBandSeq;

- static int CompBandEnter()

- static void CompBandExit(int outer)

- static void EnsureCompRegistry()

- static bool EnsureCompRegistryProbe()

- static void DrawCompCell(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, int rowIdx, int cc, DataSource src, DataTableState st)

- static int CompPosR(DataTableState st)

- static int CompPosC(DataTableState st)

- static RowKey CompPoolKey(DataTableState st, RowKey key)

- static bool CompPoolKeyDistinctProbe(DataTableState a, DataTableState b)


## DataTable (class)

- static void ApplySetPass(DataTableState st, List<DataColumn> cols, DataSource src, int c)

- static double SetPassReal(DataTableState st, List<DataColumn> cols, DataSource src, bool fcol, int c, int row)

- static bool HeapWorse(double a, double b, bool desc)

- static void HeapSwap(List<int> rows, List<double> vals, int a, int b)

- static void HeapSiftUp(List<int> rows, List<double> vals, int at, bool desc)

- static void HeapSiftDown(List<int> rows, List<double> vals, int at, bool desc)

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

- static void MoveGroup(DataTableState st, int col, int to)

- static int GroupDepth(DataTableState st, int col)

- static void GroupByAt(DataTableState st, int col, int at)

- static bool IsCollapsed(DataTableState st, string key)

- static void ToggleCollapse(DataTableState st, string key)

- static void FillGroupMembers(DataTableState st, GroupRow grp, int lo, int hi)

- static int GroupCheckState(DataTableState st, GroupRow grp, DataSource src)

- static void SelectGroupRows(DataTableState st, GroupRow grp, DataSource src, bool val)

- static void SetTreeAdapter(DataTableState st, TreeAdapter adapter, int treeColumn)

- static bool HasTree(DataTableState st)

- static void TreeReserve(DataTableState st, DataSource src)

- static bool IsTreeExpanded(DataTableState st, DataSource src, int row)

- static bool CanTreeExpand(DataTableState st, DataSource src, int row)

- static void TreeExpand(DataTableState st, DataSource src, int row)

- static bool TreeIsLoading(DataTableState st, int row)

- static void TreeLoaded(DataTableState st, DataSource src, int row)

- static void TreeCollapse(DataTableState st, DataSource src, int row)

- static void TreeToggle(DataTableState st, DataSource src, int row)

- static void TreeExpandAll(DataTableState st, DataSource src)

- static void TreeCollapseAll(DataTableState st, DataSource src)

- static void TreeExpandToLevel(DataTableState st, DataSource src, int level)

- static void SetDetail(DataTableState st, DetailProvider p)

- static bool HasDetail(DataTableState st)

- static int DetailSlots(DataTableState st, DataSource src, int row)

- static bool IsExpanded(DataTableState st, int row)

- static bool IsExpanded(DataTableState st, DataSource src, int row)

- static bool CanExpand(DataTableState st, DataSource src, int row)

- static void ExpandRow(DataTableState st, DataSource src, int row)

- static void CollapseRow(DataTableState st, int row)

- static void CollapseRow(DataTableState st, DataSource src, int row)

- static void ToggleDetail(DataTableState st, DataSource src, int row)

- static void CollapseAllDetail(DataTableState st)

- static int ExpandedCount(DataTableState st)

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

- static int DispKind(DataTableState st, int i)

- static int DispRowOf(DataTableState st, int i)

- static int DispNo(DataTableState st, int i)

- static int DispPart(DataTableState st, int i)

- static int DispDepth(DataTableState st, int i)

- static int DispLevel(DataTableState st, int i)

- static void BuildDisplay(DataTableState st, List<DataColumn> cols, DataSource src)

- static void BuildTreeDisplay(DataTableState st, List<DataColumn> cols, DataSource src)

- static void EmitDetail(DataTableState st, DataSource src, int row)

- static void ComputeSummary(DataTableState st, List<DataColumn> cols, DataSource src)

- static void FinalizeSummary(DataTableState st, List<DataColumn> cols, List<double> sumV, List<double> minV, List<double> maxV, int n)

- static void ComputeSummaryFromGroups(DataTableState st, List<DataColumn> cols)

- static void ResetGrandAgg(DataTableState st, int nc)

- static void ComputeColStats(DataTableState st, List<DataColumn> cols, DataSource src)

- static void RankByValue(List<double> vals, List<int> ranks)

- static void RankMerge(List<double> vals, List<int> idx, int lo, int mid, int hi, List<int> tmp)

- static int CfOrderPos(DataTableState st, int row)


## DataTable (class)

- static TableDiagnostic DuplicateKeyDiagnostic(string key, int row)

- static TableDiagnostic MissingKeyDiagnostic(int row)

- static TableDiagnostic StaleResultDiagnostic(int generation)

- static TableDiagnostic SchemaDiagnostic(string field, string message)


## DataTable (class)

- static void BeginEdit(DataTableState st, List<DataColumn> cols, DataSource src, int rowIdx, int col)

- static void CalendarStep(DataTableState st, DataColumn c, int days)

- static int IndexOfChoice(DataColumn c, string v)

- static void RefreshChoices(DataTableState st, DataColumn c)

- static void MoveChoice(DataTableState st, int step)

- static void CancelEdit(DataTableState st)

- static void ToggleBoolCell(DataTableState st, List<DataColumn> cols, DataSource src, int row, int col)

- static void CommitEdit(DataTableState st, List<DataColumn> cols, DataSource src)


## DataTable (class)

- static ExportJob sExportJob;

- static AtomicInt sExportBusy=new AtomicInt(0);

- static Func<bool> sXlsxCore;

- static XlsxExportUiFn sXlsxExportUi;

- static void InstallXlsxExportUi(XlsxExportUiFn fn)

- static bool RunXlsxExportUi(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly)

- static bool ExportToCsv(DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly)

- static bool ExportToCsvAsync(DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly, DataTableExportProgress progress)

- static DataTableExportSnapshot ExportBegin(bool selectionOnly, DataTableState st, int colCount, DataSource src, DataTableExportProgress progress)

- static DataTableExportSnapshot ExportSnapshot(DataTableState st, int colCount, DataSource src, bool selectionOnly)

- static bool ExportCsvSnapshot(DataTableExportSnapshot snap, List<DataColumn> cols, DataSource src, string path, DataTableExportProgress progress)


## DataTable (class)

- static bool ExportToXlsxAsyncUi(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly)

- static bool ExportToCsvAsyncUi(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly)

- static bool ExportAsyncUiImpl(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly, int kind)


## DataTable (class)

- static int FoldC(int cc)

- static bool MatchAt(string hay, string needle, int off)

- static bool EqCI(string a, string b)

- static bool ContainsCI(string hay, string needle)

- static int IndexOfCI(string hay, string needle)

- static bool StartsCI(string hay, string needle)

- static bool EndsCI(string hay, string needle)

- static bool TextMatch(string v, string q, int mode)

- static string Backspace(string s)

- static bool CellPasses(ColFilter f, string v)

- static bool CellPassesCol(ColFilter f, DataColumn col, string v, int today)

- static bool CmpPasses(ColFilter f, DataColumn col, string v)

- static List<int> PresetRange(int preset, int today)

- static bool PresetPasses(ColFilter f, DataColumn col, string v, int today)

- static int Today()

- static int CompareCell(string a, string b, bool numeric)

- static int CompareCellCol(DataColumn col, string a, string b)

- static string Truncate(string text, int fontSize, int maxW)

- static int TypeColor(App app, int type)


## DataTable (class)

- static void SetFilterTree(DataTableState st, FilterGroup root)

- static FilterGroup FilterTreeOf(DataTableState st)

- static bool HasFilterTree(DataTableState st)

- static bool GroupHasContent(FilterGroup g)

- static bool GroupPasses(DataTableState st, FilterGroup g, List<DataColumn> cols, DataSource src, int row, int today)

- static bool ConditionPasses(FilterCondition c, List<DataColumn> cols, DataSource src, int row)

- static bool RowPassesFilterTree(DataTableState st, List<DataColumn> cols, DataSource src, int row, int today)


## DataTable (class)

- static string CmpOpText(int cmpMode)

- static string DatePresetText(int preset)

- static string TopModeText(int topMode)

- static string DescribeColFilter(ColFilter f)

- static List<string> FilterSummaryItems(DataTableState st, List<DataColumn> cols)

- static string DescribeFilterTree(DataTableState st, int maxLen)

- static string DescribeGroup(FilterGroup g, int maxLen)

- static string DescribeCondition(FilterCondition c)


## DataTable (class)

- static void OpenFilterMenu(DataTableState st, List<DataColumn> cols, DataSource src, int col)

- static void RenderEditor(App app, List<DataColumn> cols, DataSource src, DataTableState st, int edX, int edY, int edW, int edH)

- static void RenderChoiceList(App app, DataColumn col, DataTableState st, int px, int py, int pw, int ph)

- static void RenderCalendar(App app, DataColumn col, DataTableState st, int px, int py, int pw, int ph)

- static bool TabBtn(App app, int bx, int by, int bw, int bh, string label, bool active)

- static bool IconBtn(App app, int bx, int by, int bw, int bh, string icon, bool active)

- static bool TextBtn(App app, int bx, int by, int bw, int bh, string label, bool primary)

- static void EditBox(App app, int bx, int by, int bw, int bh, string text, string ph, bool focused)

- static bool HandleNavKey(App app, List<DataColumn> cols, DataSource src, DataTableState st, List<FrameColumn> columns, int visRows, int dataViewW, int pinnedW, int kc, int emods)

- static bool Chip(App app, int bx, int by, int bw, int bh, string label, bool on)

- static int RulesTabHeight(App app, ColFilter f, DataColumn col)

- static int RenderRulesTab(App app, int mx, int cy, int menuW, int pad, ColFilter f, DataColumn col, DataTableState st)

- static void RenderMenu(App app, int x, int y, int viewW, int headerH, int numW, int selW, List<DataColumn> cols, DataSource src, DataTableState st)

- static void DrawMiniCheck(App app, int bx, int cy, int itemH, bool checked)


## DataTable (class)

- class FormulaExpr

- static bool FormulaEval(FormulaExpr e, DataSource src, int row, out double value)

- static bool IsFormula(List<DataColumn> cols, int col)

- static string FormulaText(List<DataColumn> cols, DataSource src, int row, int col)

- static bool FormulaTryReal(List<DataColumn> cols, DataSource src, int row, int col, out double v)

- static FormulaCacheEntry sFormulaCache;

- static FormulaExpr FormulaExprOf(List<DataColumn> cols, int col)

- static void InvalidateFormulaCache()


## DataTable (class)

- static void Refresh(DataTableState st)

- static void Refresh(DataTableState st, DataSource src)

- static void PruneExpandedKeys(DataTableState st, DataSource src)

- static void SetKeyProvider(DataTableState st, RowKeyProvider provider)

- static void RebuildRowKeys(DataTableState st, DataSource src)

- static RowKey RowKeyAt(DataTableState st, DataSource src, int row)

- static int RowIndexOfKey(DataTableState st, DataSource src, RowKey key)

- static List<TableDiagnostic> Diagnostics(DataTableState st)

- static int DataRevision(DataTableState st)

- static int QueryRevision(DataTableState st)

- static int ViewRevision(DataTableState st)

- static SelectionModel Selection(DataTableState st)

- static void SetSelectionModel(DataTableState st, SelectionModel model)

- static bool HasStableKey(DataSource src, int row)

- static bool HasStableKey(DataTableState st, DataSource src, int row)

- static RowKey CurrentRowKey(DataTableState st, DataSource src, int row)

- static void SyncSelectionModel(DataTableState st, DataSource src)

- static bool IsSelected(DataTableState st, DataSource src, int row)

- static void SetSelected(DataTableState st, DataSource src, int row, bool value)


## DataTable (class)

- static string T(string key, string dflt)

- static string TN(string key, int n, string dflt)

- static string TNPublicForTest(int n)


## DataTable (class)

- static int LayoutVersion()

- static string SaveLayout(DataTableState st, List<DataColumn> cols)

- static JsonValue FilterToJson(ColFilter f)

- static bool LoadLayout(DataTableState st, List<DataColumn> cols, string text)

- static void LoadColumnState(DataTableState st, List<DataColumn> cols, JsonValue root)

- static void LoadColumnOrder(DataTableState st, List<DataColumn> cols, JsonValue root)

- static void LoadSortKeys(DataTableState st, List<DataColumn> cols, JsonValue root)

- static void LoadGrouping(DataTableState st, List<DataColumn> cols, JsonValue root)

- static void LoadCollapsedBands(DataTableState st, JsonValue root)

- static void LoadExpandedRows(DataTableState st, JsonValue root)

- static bool ExpandedKeySeen(DataTableState st, string k)

- static void LoadSelection(DataTableState st, JsonValue root)

- static void LoadViewToggles(DataTableState st, JsonValue root)

- static void FilterFromJson(ColFilter f, JsonValue o)

- static void EnsureLayoutSlots(DataTableState st, List<DataColumn> cols)


## DataTable (class)

- static void ChildRowsOf(DataSource childSrc, int childKeyCol, string masterKey, List<int> outIdx)

- static int ChildGridSlots(DataSource childSrc, int childKeyCol, string masterKey, int maxSlots)


## DataTable (class)

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

- static void EnsureMenuIds(DataTableState st)

- static void OpenRowMenu(DataTableState st)

- static void OpenHdrMenu(DataTableState st)

- static List<MenuItem> BuildRowMenuItems(DataTableState st, List<DataColumn> cols, DataSource src)

- static void DispatchRowAction(App app, DataTableState st, List<DataColumn> cols, DataSource src, int act)

- static void RenderContextMenu(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)

- static void RenderChart(App app, int x, int y, int viewW, int viewH, DataTableState st)

- static void ExportUiRect(App app, int x, int y, int viewW, int viewH, out int px, out int py, out int pw, out int ph)

- static void RenderExportUi(App app, int x, int y, int viewW, int viewH, DataTableState st)

- static int[]CfPresetColors()

- static string SwatchName(int i)

- static int MedianThreshold(App app, DataTableState st, List<DataColumn> cols, DataSource src, int col)

- static string AggLabel(int mode)

- static List<MenuItem> BuildAggChildren(int curAgg)

- static List<MenuItem> BuildCfChildren(DataColumn dc, DataTableState st, int col)

- static void DispatchHeaderAction(App app, DataTableState st, List<DataColumn> cols, DataSource src, int col, int act)

- static void RenderHeaderMenu(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)


## DataTable (class)

- static bool IsPivotView(DataTableState st)

- static bool SetPivot(DataTableState st, List<DataColumn> cols, DataSource src, int rowCol, int colDim, int valueCol, int agg)

- static void SetPivotAgg(DataTableState st, List<DataColumn> cols, DataSource src, int agg)

- static void ExitPivot(DataTableState st)

- static void PivotResetView(DataTableState st)

- static void DrillPin(DataTableState st, int col, string value)

- static void PivotDrill(DataTableState st, int r, int c)

- static bool PivotFromSelection(DataTableState st, List<DataColumn> cols, DataSource src)

- static void PivotEnsure(DataTableState st, List<DataColumn> cols, DataSource src)

- static List<DataColumn> PivotBuildCols(List<DataColumn> cols, DataTableState st, int colDim)

- static List<DataColumn> PivotViewCols(DataTableState st)

- static DataSource PivotViewSource(DataTableState st, DataSource src)

- static void PivotFinalizeSummary(DataTableState st, List<DataColumn> cols)

- static string PivotTotalText(DataColumn col, int agg, double v, int cnt)

- static void TruncListI(List<int> l, int n)

- static void TruncListB(List<bool> l, int n)

- static void TruncListD(List<double> l, int n)

- static void TruncListS(List<string> l, int n)

- static DataColumn CloneCol(DataColumn c)

- static List<FilterChip> FilterChips(DataTableState st, List<DataColumn> cols)

- static string ColTitle(List<DataColumn> cols, int c)

- static void ClearAllFilters(DataTableState st)

- static void RenderFilterPanel(App app, int x, int y, int viewW, int h, List<DataColumn> cols, DataSource src, DataTableState st)

- static void RemoveFilterChip(DataTableState st, FilterChip chip)

- static void OpenFilterChip(DataTableState st, List<DataColumn> cols, DataSource src, FilterChip chip)


## DataTable (class)

- static void InvalidateData(DataTableState st)

- static void InvalidateQuery(DataTableState st)

- static void InvalidateView(DataTableState st)


## DataTable (class)

- static void SetCellFieldLookup(DataSource src, CellFieldLookup lookup)

- static CellFieldLookup FieldLookupOf(List<DataColumn> cols)

- static DeltaResult ApplyDeltas(DataTableState st, List<DataColumn> cols, DataSource src, DeltaBatch batch)

- static DeltaResult ApplyDeltas(DataTableState st, List<DataColumn> cols, DataSource src, List<DeltaOp> ops)


## DataTable (class)

- static void DrawGrip(App app, int bx, int by, int bw, int bh, int color)

- static int SparkZeroY(double negMin, double posMax, int plotY, int plotH)

- static int SparkValueY(double v, double posMax, double negMin, int plotY, int plotH)

- static void DrawSparkBars(App app, DataColumn col, List<string> seq, int n, int plotX, int plotY, int plotW, int plotH, double posMax, double negMin, int zeroY, int spark, int sparkNeg)

- static void DrawSparkLine(App app, DataColumn col, List<string> seq, int n, int plotX, int plotY, int plotW, int plotH, double posMax, double negMin, int zeroY, int spark, int sparkNeg)

- static readonly int MergeWalkCap=1000;

- static void DrawCell(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride)

- static void DrawCellCtx(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride, int rowIdx, int cc)

- static void DrawCellIndented(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride, int indent, bool isTreeCol, bool canExpand, bool expanded)

- static void DrawCellRange(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride)

- static void DrawCellRangeCtx(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride, int rowIdx, int cc, DataTableState st)

- static void DrawCellRangeCtx(App app, DataColumn col, string val, int cx, int cy, int cw, int rowH, double cfLo, double cfHi, int fgOverride, int rowIdx, int cc, DataTableState st, DataSource src)

- static void DrawWrappedLines(App app, string text, int tx, int cy, int innerW, int rowH, int maxLines, int lineH, int color, int fs)

- static List<string> WrapTextLines(string text, int maxW, int fs, int maxLines)

- static int BandLeft(int band, int firstDataX, int bandX, int rightX)

- static int BandRight(int band, int firstDataX, int bandX, int bandW, int rightX, int pinnedW, int pinnedRW, int clipRight)

- static void PushBandClip(Canvas c, int band, int firstDataX, int bandX, int bandW, int rightX, int pinnedW, int pinnedRW, int y, int h, int clipRight)

- static List<GroupChipGeometry> GroupChipLayout(App app, List<DataColumn> cols, DataTableState st, int x)

- static int GroupDropSlot(App app, List<GroupChipGeometry> chips)

- static int ModalEvt(App app, DataTableState st)

- static void RenderGroupPanel(App app, int x, int y, int viewW, int h, List<DataColumn> cols, DataTableState st, bool headerDrag)

- static void Render(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, List<DataRow> rows, DataTableState st)

- static DataTableFrame ComputeFrame(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)

- static void RenderSource(App app, int x, int y, int viewW, int viewH, List<DataColumn> cols, DataSource src, DataTableState st)


## DataTable (class)

- static void EnsureColumnSlots(DataTableState st, List<DataColumn> cols)

- static void EnsureInit(DataTableState st, List<DataColumn> cols, DataSource src)

- static void ShiftExpandedOnInsert(DataTableState st, int at)

- static void ShiftExpandedOnDelete(DataTableState st, int at)

- static void Journal(DataTableState st, RowEdit e)

- static void BeginUndoGroup(DataTableState st)

- static void EndUndoGroup(DataTableState st)

- static int UndoDepth(DataTableState st)

- static void ClearUndo(DataTableState st)

- static int InsertRow(DataTableState st, List<DataColumn> cols, DataSource src, int at)

- static int AppendRow(DataTableState st, List<DataColumn> cols, DataSource src)

- static List<string> RowCells(DataSource src, List<DataColumn> cols, int row)

- static bool DeleteRow(DataTableState st, List<DataColumn> cols, DataSource src, int at)

- static int DeleteSelectedRows(DataTableState st, List<DataColumn> cols, DataSource src)

- static int SlotOfRow(DataTableState st, int row)

- static void FocusRow(DataTableState st, List<DataColumn> cols, DataSource src, int row)

- static bool Undo(DataTableState st, List<DataColumn> cols, DataSource src)

- static bool Redo(DataTableState st, List<DataColumn> cols, DataSource src)

- static void RedoOne(DataTableState st, List<DataColumn> cols, DataSource src, RowEdit e)

- static void UndoOne(DataTableState st, List<DataColumn> cols, DataSource src, RowEdit e)


## DataTable (class)

- static readonly int FindScanCap=200000;

- static void OpenFind(DataTableState st)

- static void CloseFind(DataTableState st)

- static bool FindableCol(DataColumn col)

- static bool RowHasMatch(List<DataColumn> cols, DataSource src, int row, string q)

- static bool CellMatches(DataTableState st, List<DataColumn> cols, DataSource src, int row, int col)

- static void CommitFind(DataTableState st, List<DataColumn> cols, DataSource src)

- static void FindEnsureCount(DataTableState st, List<DataColumn> cols, DataSource src)

- static void FindStep(DataTableState st, List<DataColumn> cols, DataSource src, int dir, int visRows)

- static void RenderFindBar(App app, int x, int y, int viewW, List<DataColumn> cols, DataSource src, DataTableState st, int visRows)


## DataTable (class)

- static int Rgb(int r, int g, int b)

- static int ColorAlpha(int packed, int a)

- static int LerpColor(int c0, int c1, int num, int den)

- static int RowBackground(DataTableState st, DataSource src, int row, int stripeBg, int activeBg, int hoverBg, bool sel, bool hover)

- static int CellBackground(DataTableState st, DataSource src, int row, int col)

- static int CellForeground(DataTableState st, DataSource src, int row, int col)

- static void MergeKeyed(int kind, int dir, List<int> order, List<int> nkey, List<string> skey, List<double> rkey, int lo, int mid, int hi, List<int> otmp, List<int> ntmp, List<string> stmp, List<double> rtmp)

- static void SyncPrimary(DataTableState st)

- static int SortIndexOf(DataTableState st, int col)

- static void SortBy(DataTableState st, int col, int dir)

- static void AddSort(DataTableState st, int col, int dir)

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

- static List<string> DistinctFiltered(DataTableState st, List<DataColumn> cols, DataSource src, int col)


## DataTable (class)

- static readonly int TransposeRowCap=300;

- static bool IsTransposeView(DataTableState st)

- static bool SetTranspose(DataTableState st, List<DataColumn> cols, DataSource src)

- static void ExitTranspose(DataTableState st)

- static void TransposeEnsure(DataTableState st)

- static DataSource TransposeViewSource(DataTableState st, DataSource src)


## DataTable (class)

- static int CompareStr(string a, string b)

- static bool IsIntStr(string s)

- static int ParseInt(string s)

- static double Pow10(int n)

- static double ParseReal(string s)

- static string PadFrac(int fp, int decimals)

- static string FormatReal(double v, int decimals)

- static string FormatSpecNum(double v, string spec)

- static string ApplySummaryTemplate(string fmt, string dflt, double num)

- static bool ParseBool(string s)

- static string TrimSpace(string s)

- static string LowerAscii(string s)

- static int ParseDate(string s, int dateOrder)

- static int NoDate()

- static int DateFromYmd(int y, int m, int d)

- static string FormatDate(int days, int dateFmt)

- static int DateStoreFormat(DataColumn c)

- static string MonthAbbr(int m)

- static string DateGroupKey(int days, int dateGroup)

- static string DayName(int dow)

- static string GroupDigits(string ipart)

- static string FormatCell(DataColumn col, string raw)

- static bool IsNumericStr(string s)

- static string ValidateCell(DataColumn c, string v)

- static List<int> FilterChoices(DataColumn c, string typed)

- static string StepValue(DataColumn c, string cur, int dir)

- static int YearMonth(int y, int m)

- static int YmYear(int ym)

- static int YmMonth(int ym)

- static int MonthFirstDow(int ym)

- static List<int> MonthCells(int ym)


## DataTable (class)

- static void EnsureWidgetComps()


## DataTable (class)

- static int MinI(int a, int b)

- static int MaxI(int a, int b)

- static bool HasRange(DataTableState st)

- static bool HasSelection(DataTableState st)

- static string CsvField(string v)

- static string BuildText(DataTableState st, List<DataColumn> cols, DataSource src, bool csv, bool withHeader)

- static void SelectAllRows(DataTableState st, DataSource src, bool val)

- static void SelectRowSpan(DataTableState st, DataSource src, int a, int b)

- static void PickRow(DataTableState st, DataSource src, int oi, int mods)

- static int PrimarySelected(DataTableState st)

- static bool AnyRowSelected(DataTableState st, DataSource src)

- static void SetPrimarySelected(DataTableState st, DataSource src, int row)

- static void SyncBindings(DataTableState st, DataSource src)

- static int ParseIntLoose(string s)

- static bool BuildChartFromRange(DataTableState st, List<DataColumn> cols, DataSource src)

- static void ClearRange(DataTableState st)

- static int MultiCount(DataTableState st)

- static bool HasMulti(DataTableState st)

- static bool MultiAt(DataTableState st, int i, out int r0, out int c0, out int r1, out int c1)

- static void ArchiveRange(DataTableState st)

- static void ClearMulti(DataTableState st)

- static int RangeCount(DataTableState st)

- static bool RangeAt(DataTableState st, int i, out int r0, out int c0, out int r1, out int c1)

- static void SyncRangeChecks(DataTableState st, DataSource src)

- static void MoveOrder(DataTableState st, int from, int to)

- static int NextDataSlot(DataTableState st, int from, int step)

- static int FirstDataSlot(DataTableState st)

- static int LastDataSlot(DataTableState st)

- static int VisPos(List<int> vorder, int col)

- static int VisPos(List<FrameColumn> columns, int col)

- static bool IsNavKey(int kc)

- static List<int> NavTarget(DataTableState st, List<int> vorder, int visRows, int kc, int emods)

- static List<int> NavTarget(DataTableState st, List<FrameColumn> columns, int visRows, int kc, int emods)

- static bool MoveCursor(DataTableState st, DataSource src, int oi, int col, bool extend)

- static void ScrollSlotIntoView(DataTableState st, int oi, int visRows)

- static void ScrollColIntoView(DataTableState st, List<int> vorder, int col, int dataViewW, int pinnedW)

- static void ScrollColIntoView(DataTableState st, List<FrameColumn> columns, int col, int dataViewW, int pinnedW)

- static string CopyText(DataTableState st, List<DataColumn> cols, DataSource src)

- static int WriteCellValue(DataTableState st, List<DataColumn> cols, DataSource src, int row, int col, string v)

- static bool FillWritable(DataColumn col)

- static int PasteSeg(DataTableState st, List<DataColumn> cols, DataSource src, List<string> lines, int bw, int slotR0, int slotR1, int c0, int ncols)

- static int PasteText(DataTableState st, List<DataColumn> cols, DataSource src, string text)

- static int FillDown(DataTableState st, List<DataColumn> cols, DataSource src)

- static int FillRight(DataTableState st, List<DataColumn> cols, DataSource src)

- static List<int> FillSrcRows(DataTableState st, int sr0, int sr1)

- static string FillSeriesValue(DataTableState st, List<DataColumn> cols, DataSource src, List<int> srcRows, int col, int d, bool forward)

- static int FillApply(DataTableState st, List<DataColumn> cols, DataSource src, int sr0, int sc0, int sr1, int sc1, int tr0, int tc0, int tr1, int tc1)

- static string FillSeriesValueRow(DataTableState st, List<DataColumn> cols, DataSource src, int row, int c0, int c1, int d, bool forward)

- static int ClearRangeCells(DataTableState st, List<DataColumn> cols, DataSource src)

- static int ClearSeg(DataTableState st, List<DataColumn> cols, DataSource src, int r0, int r1, int c0, int c1)

- static List<string> SplitLines(string s)

- static List<string> SplitTabs(string s)

- static List<string> NumSeries(string s)


## DataTableBootstrap (class)

- static bool installed;

- static void Install()

- static Control MakeDataGrid(string kind)

- static Control MakeTransfer(string kind)


## DataTableExportProgress (class)

- virtual void OnStart(int totalRows)

- virtual void OnProgress(int doneRows, int totalRows)

- virtual void OnDone(bool ok, string error)

- virtual bool Cancelled()


## DataTableExportSnapshot (class)

- public List<int> rows;

- public int c0;

- public int c1;


## DataTableExportUi (class)

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


## DataTableRowSource (class)

- public DataSource src;

- public List<DataColumn> cols;

- public DataTableExportSnapshot snap;

- public DataTableExportProgress progress;

- override int RowCount()

- override bool FillRow(int index, List<XlsxCell> row)

- override void OnProgress(int done, int total)

- override bool Cancelled()


## DataTableState (class)

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

- List<ColumnAggregateState> aggregates;

- List<int> aggMode;

- List<int> aggValue;

- List<double> aggReal;

- List<string> summaryText;

- List<double> aggSumR;

- List<double> aggMinR;

- List<double> aggMaxR;

- List<ColumnCfExtents> cfExtents;

- List<int> cfMin;

- List<int> cfMax;

- List<double> cfMinR;

- List<double> cfMaxR;

- List<int> cfRank;

- Dict <int, int> cfOrderPos;

- List<string> formulaSeen;

- List<ColumnCfRule> cfRules;

- bool cfClearOpen;

- List<CfClearItem> cfClearItems;

- int cfClearScroll;

- bool paged;

- SignalInt pageModel;

- int pageRows;

- Binding<int> selection;

- Binding<int> page;

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

- List<TableCellRange> cellRanges;

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

- void Reject(string msg)

- string EditError()


## DataTableUiExportProgress (class)

- public static DataTableExportUi ui;

- static int sDone;

- static int sTotal;

- static bool sOk;

- static string sMsg;

- static bool sFinished;

- public static void Bind(DataTableExportUi target)

- public override void OnStart(int totalRows)

- public override void OnProgress(int doneRows, int totalRows)

- public override void OnDone(bool ok, string error)

- public override bool Cancelled()

- public static void SyncUi()


## DataTableXlsx (class)

- static void InstallExportUi()

- static bool ExportToXlsxAsyncUi(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly)

- static bool ExportToXlsxAsync(DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly, DataTableExportProgress progress)

- static bool ExportXlsxSnapshot(DataTableExportSnapshot snap, List<DataColumn> cols, DataSource src, string path, DataTableExportProgress progress)

- static XlsxCell ExportCell(int ci, List<DataColumn> cols, DataColumn col, DataSource src, int row)


## DeltaBatch (class)

- static const int FormatVersion=1;

- int version;

- List<DeltaOp> ops;

- DeltaBatch()

- static DeltaBatch Create()

- void Add(DeltaOp op)

- int Count()

- DeltaOp At(int i)

- int Version()

- void Compress()

- JsonValue ToJsonValue()

- string ToJson()

- static DeltaBatch FromJson(string text)


## DeltaOp (class)

- static const int Upsert=0;

- static const int Remove=1;

- static const int Cell=2;

- int kind;

- string key;

- int revision;

- List<string> cells;

- string field;

- string value;

- DeltaOp(int opKind, string rowKey, int rev)

- static DeltaOp UpsertRow(string rowKey, List<string> values, int rev)

- static DeltaOp RemoveRow(string rowKey, int rev)

- static DeltaOp SetCell(string rowKey, string fieldName, string cellValue, int rev)

- int Kind()

- string Key()

- int Revision()

- string Field()

- string Value()

- List<string> Cells()

- DeltaOp Copy()

- JsonValue ToJsonValue()

- static DeltaOp FromJsonValue(JsonValue node)


## DeltaResult (class)

- int inserted;

- int updated;

- int removed;

- int cells;

- int missing;

- static DeltaResult Create()

- int Inserted()

- int Updated()

- int Removed()

- int Cells()

- int Missing()


## DetailProvider (class)

- virtual int Slots(DataSource src, int row)

- virtual bool CanExpand(DataSource src, int row)

- virtual void Paint(App app, DataSource src, int row, int x, int y, int w, int h)


## DispRow (class)

- int kind;

- int row;

- int no;

- int part;

- int depth;

- DispRow(int k, int r, int n)


## ExportJob (class)

- public int kind;

- public DataTableExportSnapshot snap;

- public List<DataColumn> cols;

- public DataSource src;

- public string path;

- public DataTableExportProgress progress;

- static void Worker()


## FilterChip (class)

- int kind;

- int col;

- string text;

- FilterChip(int chipKind, int chipCol, string chipText)

- static FilterChip Make(int kind, int col, string text)

- int Kind()

- int Col()

- string Text()


## FilterCondition (class)

- int op;

- string field;

- string valueA;

- string valueB;

- FilterCondition(int conditionOp, string fieldName, string a, string b)

- static FilterCondition Of(int op, string field, string a, string b)

- static FilterCondition Contains(string field, string value)

- static FilterCondition Equals(string field, string value)

- static FilterCondition NotEquals(string field, string value)

- static FilterCondition Greater(string field, string value)

- static FilterCondition GreaterOrEqual(string field, string value)

- static FilterCondition Less(string field, string value)

- static FilterCondition LessOrEqual(string field, string value)

- static FilterCondition Between(string field, string lo, string hi)

- static FilterCondition IsEmpty(string field)

- static FilterCondition IsNotEmpty(string field)

- static FilterCondition StartsWith(string field, string value)

- static FilterCondition EndsWith(string field, string value)

- int Op()

- string Field()

- string ValueA()

- string ValueB()


## FilterGroup (class)

- bool and;

- List<FilterCondition> conditions;

- List<FilterGroup> groups;

- FilterGroup(bool isAnd)

- static FilterGroup And()

- static FilterGroup Or()

- bool IsAnd()

- List<FilterCondition> Conditions()

- List<FilterGroup> Groups()


## FormulaCacheEntry (class)

- public List<DataColumn> cols;

- public List <DataTable.FormulaExpr> exprs;


## FrameColumn (class)

- int column;

- int x;

- int band;

- FrameColumn(int c, int xx, int b)


## GridColumn (class)

- DataColumn desc;

- GridText<T> text;

- GridInt<T> num;

- GridNums<T> nums;

- GridReal<T> real;

- GridBool<T> truth;

- GridSet<T> setter;

- GridColumn(DataColumn d)

- GridColumn<T> Editable(GridSet<T> w)

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

- GridColumn<T> Shape(int rows, int cols)

- string Raw(T row)


## GridImageSlot (class)

- static GridUrlEnsureFn ensurer;

- static void SetEnsurer(GridUrlEnsureFn fn)

- static string Ensure(App app, string url)


## GridSource (class)

- List<T> data;

- List <GridColumn<T>> cols;

- GridNew<T> factory;

- GridKey<T> key;

- override int RowCount()

- bool Row(int row)

- override string CellText(int row, int col)

- override RowKey GetRowKey(int row)

- override int CellNum(int row, int col)

- override double CellReal(int row, int col)

- override bool CellBool(int row, int col)

- override void SetCell(int row, int col, string v)

- override bool CanInsert()

- override bool InsertRow(int at)

- override bool RemoveRow(int at)


## GroupAggregateEntry (class)

- public int value;

- public string text;

- public GroupAggregateEntry(int v, string t)


## GroupChipGeometry (class)

- int x;

- int width;

- GroupChipGeometry(int xx, int w)


## GroupRow (class)

- string label;

- int level;

- int count;

- string key;

- bool collapsed;

- List<GroupAggregateEntry> aggregates;

- List<int> members;

- GroupRow(string lb, int lv, int cnt, string k, bool coll)


## HttpTableSource (class)

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

- static HttpTableSource CreateTls(string host, int port, string basePath, string cacheDir)

- void SetToken(string t)

- string LastError()

- string CachePath(int start, int count)

- override void FetchBlock(int start, int count)

- static void WorkerEntry()

- static async int FetchOne(HttpTableSource src)

- override bool Poll()

- void ServeFile(string path)

- bool ParsePage(string body, List<DataRow> dst)


## LocalDataSource (class)

- List<DataRow> rows;

- RowKeyProvider keyProvider;

- CellFieldLookup cellFieldOf;

- LocalDataSource(List<DataRow> values)

- override QueryCapabilities Capabilities()

- override bool ServerControlled()

- int ColumnCount()

- void ReplaceRows(List<DataRow> values)

- void SetKeyProvider(RowKeyProvider provider)

- void SetCellFieldLookup(CellFieldLookup lookup)

- List<DataRow> Snapshot()

- override int RowCount()

- RowKey RawKey(int row)

- int SourceRowFor(int row)

- override bool CellReady(int row, int col)

- override RowKey GetRowKey(int row)

- override string CellText(int row, int col)

- override void SetCell(int row, int col, string value)

- override bool CanInsert()

- int RowOfKey(string canonical)

- override DeltaResult ApplyDeltas(DeltaBatch batch)

- override bool InsertRow(int at)

- override bool RemoveRow(int at)

- int CompareValue(string leftText, string rightText, int kind)

- bool FilterPassValues(string value, int mode, string a, string b, int kind)

- bool FilterPass(string value, QueryFilterClause clause)

- List<LocalFilterColumn> BuildFilterColumns(QueryPlan plan)

- bool RowPasses(int row, QueryPlan plan, List<LocalFilterColumn> columns)

- List<LocalSortColumn> BuildSortColumns(QueryPlan plan)

- int CompareRows(int left, int right, QueryPlan plan, List<LocalSortColumn> columns)

- void SortOrder(List<int> order, QueryPlan plan, List<LocalSortColumn> columns)

- string GroupAtom(string value, int kind)

- string GroupKey(int row, QueryPlan plan)

- void AddGroupBucket(List<QueryGroupBucket> buckets, string key)

- List<QueryGroupBucket> BuildGroups(List<int> order, QueryPlan plan)

- double AggregateNumber(string value, int kind)

- List<QueryAggregateValue> BuildAggregates(List<int> order, QueryPlan plan)

- override QueryResult Execute(QueryDescriptor query, QueryViewport viewport, QueryRequestToken token)


## LocalFilterColumn (class)

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

- void Add(string value)

- bool Pass(string raw, int row)

- bool PassText(string value)


## LocalSortColumn (class)

- int kind;

- List<int> ints;

- List<double> reals;

- List<bool> bools;

- LocalSortColumn(int valueKind)

- void Add(string value)

- int IntAt(int row)

- double RealAt(int row)

- bool BoolAt(int row)


## MutationResult (class)

- static const int Accepted=1;

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

- static MutationResult AcceptedValue(string id, string value, int revision)

- static MutationResult RejectedValue(string id, string detail)

- static MutationResult ConflictValue(string id, string server, int revision, string detail)

- static MutationResult CancelledValue(string id, string detail)

- string MutationId()

- int Status()

- string CanonicalValue()

- string ServerValue()

- int ServerRevision()

- string Message()

- bool IsValid()

- string ToJson()


## PivotDataSource (class)

- DataTableState st;

- DataSource inner;

- PivotDataSource(DataTableState state, DataSource innerSrc)

- override int RowCount()

- override string CellText(int row, int col)

- override int CellNum(int row, int col)

- override double CellReal(int row, int col)

- override bool Poll()


## QueryAggregateClause (class)

- int column;

- int mode;

- int kind;

- QueryAggregateClause(int col, int op, int valueKind)

- int Column()

- int Mode()

- int Kind()


## QueryAggregateValue (class)

- int column;

- int mode;

- int kind;

- string text;

- double number;

- QueryAggregateValue(int col, int op, int valueKind, string valueText, double valueNumber)

- int Column()

- int Mode()

- int Kind()

- string Text()

- double Number()


## QueryCapabilities (class)

- static const int None=0;

- static const int Filter=1;

- static const int Sort=2;

- static const int Group=4;

- static const int Aggregate=8;

- static const int Page=16;

- static const int StableKeys=32;

- static const int Edit=64;

- static const int Insert=128;

- static const int Delete=256;

- static const int Server=512;

- int mask;

- QueryCapabilities(int value)

- static QueryCapabilities Of(int value)

- int Mask()

- bool Has(int capability)


## QueryDataSource (class)

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

- void Setup()

- void ClearTransactionState(bool keepCurrent)

- void SetPageSize(int size)

- int PageSize()

- void Refresh()

- void SetSchema(List<SchemaColumn> value, int version)

- List<SchemaColumn> DescribeSchema()

- virtual QueryCapabilities Capabilities()

- virtual QueryPlanContract BuildQueryPlan(QueryDescriptor query)

- QueryRequestEnvelope BuildQueryRequest(QueryPlanContract contract, QueryViewport viewport, QueryRequestToken token)

- int SchemaVersion()

- int ServerRevision()

- void SetServerRevision(int revision)

- QueryRequestToken NewToken(QueryDescriptor query, int generation)

- QueryRequestToken RequestQuery(QueryDescriptor query, QueryViewport viewport, QueryRequestToken token)

- override void RequestBlock(int row)

- override void RequestNextBlock()

- override bool HasMore()

- virtual void FetchQuery(QueryDescriptor query, QueryViewport viewport, QueryRequestToken token)

- virtual void FetchQueryPlan(QueryDescriptor query, QueryPlanContract contract, QueryViewport viewport, QueryRequestToken token)

- virtual QueryResult Execute(QueryDescriptor query, QueryViewport viewport, QueryRequestToken token)

- TransactionRequestEnvelope BuildTransactionRequest(TableTransaction transaction, TransactionRequestToken token)

- TransactionRequestToken SubmitTransaction(TableTransaction transaction)

- virtual void FetchTransaction(TableTransaction transaction, TransactionRequestToken token)

- bool TransactionResultShapeValid(TableTransactionResult result)

- bool FinishTransaction(TableTransactionResult result, TransactionRequestToken token)

- void CancelTransaction(TransactionRequestToken token)

- TableTransactionResult CurrentTransactionResult()

- TableTransactionResult ErrorTransaction()

- virtual void ApplyTransactionResult(TableTransactionResult result)

- bool PollTransaction()

- void FinishQuery(QueryResult result, QueryRequestToken token)

- override bool Poll()

- void Cancel(QueryRequestToken token)

- QueryResult CurrentResult()

- QueryResult ErrorResult()

- int CacheBytes()

- void ClearCache()

- QueryResult CachedRow(int row)

- override bool ServerControlled()

- override int RowCount()

- override bool CellReady(int row, int col)

- override RowKey GetRowKey(int row)

- override string CellText(int row, int col)

- override int CellNum(int row, int col)

- override double CellReal(int row, int col)

- override int CellDay(int row, int col, int order)

- override bool CellBool(int row, int col)


## QueryDescriptor (class)

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

- static QueryDescriptor Of(DataTableState st, string datasetName, int caps)

- string Dataset()

- int DataRevision()

- int QueryRevision()

- int ViewRevision()

- int Capabilities()

- List<QueryFilterClause> Filters()

- List<QuerySortClause> Sorts()

- List<QueryGroupClause> Groups()

- List<QueryAggregateClause> Aggregates()

- void AddFilter(QueryFilterClause clause)

- void AddSort(QuerySortClause clause)

- void AddGroup(QueryGroupClause clause)

- void AddAggregate(QueryAggregateClause clause)

- void ClearFilters()

- void ClearSorts()

- void ClearGroups()

- void ClearAggregates()

- QueryPlan BuildPlan(int columnCount, QueryCapabilities supported)

- QueryPlan BuildPlan(List<SchemaColumn> schema, QueryCapabilities supported)

- bool SameQuery(QueryDescriptor other)

- bool IsCurrent(DataTableState st)

- JsonValue FilterJson(QueryFilterClause clause)

- JsonValue SortJson(QuerySortClause clause)

- JsonValue GroupJson(QueryGroupClause clause)

- JsonValue AggregateJson(QueryAggregateClause clause)

- void PutShape(JsonValue root)

- JsonValue ToJsonValue()

- string ToJson()

- string QueryJson()

- int QueryFingerprint()

- int Fingerprint()

- static QueryDescriptor FromJson(string text)


## QueryFilterClause (class)

- int column;

- int mode;

- int kind;

- string value;

- string value2;

- QueryFilterClause(int col, int op, string a, string b, int valueKind)

- int Column()

- int Mode()

- int Kind()

- string Value()

- string Value2()


## QueryFilterMode (class)

- static const int Equals=1;

- static const int Contains=2;

- static const int StartsWith=3;

- static const int EndsWith=4;

- static const int Greater=5;

- static const int GreaterOrEqual=6;

- static const int Less=7;

- static const int LessOrEqual=8;

- static const int Between=9;

- static const int Empty=10;

- static const int NotEmpty=11;


## QueryGroupBucket (class)

- string key;

- int count;

- QueryGroupBucket(string groupKey, int rowCount)

- string Key()

- int Count()


## QueryGroupClause (class)

- int column;

- int kind;

- QueryGroupClause(int col, int valueKind)

- int Column()

- int Kind()


## QueryPlan (class)

- static const int InvalidQuery=1;

- static const int InvalidRevision=2;

- static const int InvalidFilterColumn=3;

- static const int InvalidSortColumn=4;

- static const int UnsupportedFilter=5;

- static const int UnsupportedSort=6;

- static const int InvalidFilterOperator=7;

- static const int InvalidValueKind=8;

- static const int InvalidSortDirection=9;

- static const int EmptyDataset=10;

- static const int SchemaValueKindMismatch=11;

- static const int UnsupportedGroup=12;

- static const int UnsupportedAggregate=13;

- static const int InvalidAggregateMode=14;

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

- static QueryPlan Subset(QueryPlan source, int caps, List<QueryFilterClause> fs, List<QuerySortClause> ss, List<QueryGroupClause> gs, List<QueryAggregateClause> aggs)

- static QueryPlan Fail(QueryPlan plan, QueryPlanIssue problem)

- static bool SchemaKindMatches(int schemaKind, int queryKind)

- static QueryPlan Build(QueryDescriptor query, List<SchemaColumn> schema, QueryCapabilities supported)

- static QueryPlan Build(QueryDescriptor query, int columnCount, QueryCapabilities supported)

- bool IsValid()

- QueryPlanIssue Issue()

- string Error()

- string Dataset()

- string QueryText()

- int QueryHash()

- int DataRevision()

- int QueryRevision()

- int Capabilities()

- int FilterCount()

- int SortCount()

- int FilterColumnAt(int index)

- int FilterModeAt(int index)

- int FilterKindAt(int index)

- string FilterValueAt(int index)

- string FilterValue2At(int index)

- int SortColumnAt(int index)

- int SortDirectionAt(int index)

- int SortKindAt(int index)

- int GroupCount()

- int AggregateCount()

- int GroupColumnAt(int index)

- int GroupKindAt(int index)

- int AggregateColumnAt(int index)

- int AggregateModeAt(int index)

- int AggregateKindAt(int index)

- QueryFilterClause FilterAt(int index)

- QuerySortClause SortAt(int index)

- JsonValue FilterJson(QueryFilterClause clause)

- JsonValue SortJson(QuerySortClause clause)

- JsonValue GroupJson(QueryGroupClause clause)

- JsonValue AggregateJson(QueryAggregateClause clause)

- JsonValue ToJsonValue()

- string ToJson()


## QueryPlanContract (class)

- QueryPlan pushed;

- QueryPlan residual;

- int pushedCapabilities;

- int residualCapabilities;

- QueryPlanIssue issue;

- QueryPlanContract(QueryPlan pushedPlan, QueryPlan residualPlan, int pushedMask, int residualMask, QueryPlanIssue problem)

- static QueryPlanContract FromPlan(QueryPlan full, int supportedMask)

- static QueryPlanContract Build(QueryDescriptor query, int columnCount, QueryCapabilities supported)

- static QueryPlanContract Build(QueryDescriptor query, List<SchemaColumn> schema, QueryCapabilities supported)

- bool IsValid()

- QueryPlanIssue Issue()

- string Error()

- QueryPlan Pushed()

- QueryPlan Residual()

- int PushedCapabilities()

- int ResidualCapabilities()

- bool HasResidual()

- bool IsComplete()

- string ToJson()


## QueryPlanIssue (class)

- int code;

- string message;

- int column;

- int clause;

- QueryPlanIssue(int c, string msg, int col, int at)

- int Code()

- string Message()

- int Column()

- int Clause()


## QueryRequestEnvelope (class)

- static const int FormatVersion=1;

- QueryPlanContract contract;

- QueryViewport viewport;

- QueryRequestToken token;

- QueryRequestEnvelope(QueryPlanContract planContract, QueryViewport pageViewport, QueryRequestToken requestToken)

- static QueryRequestEnvelope Build(QueryPlanContract contract, QueryViewport viewport, QueryRequestToken token)

- QueryPlanContract Contract()

- QueryViewport Viewport()

- QueryRequestToken Token()

- bool IsValid()

- string ToJson()


## QueryRequestToken (class)

- int requestId;

- int generation;

- string queryText;

- int queryHash;

- int schemaVersion;

- int serverRevision;

- bool cancelled;

- QueryRequestToken(int id, int gen, string canonical, int hash, int schema, int server)

- int RequestId()

- int Generation()

- string QueryText()

- int QueryHash()

- int SchemaVersion()

- int ServerRevision()

- bool IsCancelled()

- void Cancel()

- bool SameIdentity(QueryRequestToken other)


## QueryResult (class)

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

- static QueryResult Empty(QueryRequestToken token, QueryViewport viewport)

- static QueryResult EmptyAt(QueryRequestToken token, int at)

- static QueryResult Failure(QueryRequestToken token, QueryViewport viewport, string message)

- int RequestId()

- int Generation()

- string QueryText()

- int QueryHash()

- int SchemaVersion()

- int ServerRevision()

- int Start()

- int RequestedCount()

- string Cursor()

- int Count()

- List<QueryRow> Rows()

- int Total()

- bool TotalKnown()

- bool HasMore()

- string Continuation()

- string Error()

- bool Failed()

- List<QueryGroupBucket> Groups()

- List<QueryAggregateValue> Aggregates()

- QueryResult WithShape(List<QueryGroupBucket> groupValues, List<QueryAggregateValue> aggregateValues)

- bool Covers(int row)

- bool MatchesViewport(QueryViewport viewport)

- QueryRow RowAt(int row)

- string CellText(int row, int col)

- bool Matches(QueryRequestToken token)

- QueryResult Rebind(QueryRequestToken token, QueryViewport viewport)


## QueryRow (class)

- RowKey key;

- int revision;

- DataRow cells;

- QueryRow(RowKey rowKey, int rowRevision, DataRow values)

- RowKey Key()

- int Revision()

- DataRow Cells()


## QuerySortClause (class)

- int column;

- int direction;

- int kind;

- QuerySortClause(int col, int dir, int valueKind)

- int Column()

- int Direction()

- int Kind()


## QueryViewport (class)

- int start;

- int count;

- string cursor;

- QueryViewport(int at, int size, string token)

- int Start()

- int Count()

- string Cursor()

- bool UsesCursor()


## RateComp (class)

- List<WidgetPoolSlot> pool;

- int lastV;

- RateComp():base("rate")

- override Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- override void FillStr(Control ctl, string val, DataColumn col, DataSource src, int row)

- override bool TakeCommit(Control ctl, ref string outVal)

- override void Reset()


## RowEdit (class)

- int kind;

- int row;

- int col;

- string before;

- string after;

- List<string> cells;

- int group;

- static RowEdit Cell(int row, int col, string before, string after)

- static RowEdit Insert(int row)

- static RowEdit Delete(int row, List<string> cells)


## RowKey (class)

- string value;

- bool temporary;

- RowKey(string v, bool temp)

- static RowKey Of(string value)

- static RowKey Legacy(int row)

- static RowKey Legacy(string value)

- string Canonical()

- bool IsTemporary()

- bool Same(RowKey other)


## RowKeyProvider (class)

- virtual RowKey Key(DataSource src, int row)


## RowListSource (class)

- List<DataRow> rows;

- RowListSource(List<DataRow> rows)

- override int RowCount()

- override string CellText(int row, int col)

- override int CellNum(int row, int col)

- override void SetCell(int row, int col, string v)

- override bool CanInsert()

- override bool InsertRow(int at)

- override bool RemoveRow(int at)


## RowRef (class)

- RowKey key;

- int locator;

- int revision;

- RowRef(RowKey k, int at, int rev)


## SchemaColumn (class)

- string id;

- string field;

- int valueType;

- string rendererId;

- string editorId;

- bool nullable;

- SchemaColumn(string columnId, string fieldName, int kind)

- string Id()

- string Field()

- int ValueType()

- bool Nullable()


## SelectComp (class)

- List<WidgetPoolSlot> pool;

- int lastIdx;

- SelectComp():base("select")

- override Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- override void FillStr(Control ctl, string val, DataColumn col, DataSource src, int row)

- override bool TakeCommit(Control ctl, ref string outVal)

- override void Reset()


## SelectionMode (class)

- static const int Row=1;

- static const int Cell=2;

- static const int Range=3;


## SelectionModel (class)

- int mode;

- bool allSelected;

- List<string> explicitKeys;

- Dict <string, bool> explicitIndex;

- List<string> excludedKeys;

- Dict <string, bool> excludedIndex;

- SelectionModel(int selectionMode)

- static SelectionModel Row()

- static SelectionModel Cell()

- static SelectionModel Range()

- int Mode()

- void SetMode(int selectionMode)

- bool AllSelected()

- int Count()

- int ExplicitCount()

- int ExcludedCount()

- bool ValidKey(string key)

- bool ContainsKey(string key)

- bool ExcludedKey(string key)

- bool AddExplicit(string key)

- bool RemoveExplicit(string key)

- bool AddExcluded(string key)

- bool RemoveExcluded(string key)

- bool SelectKey(string key)

- bool DeselectKey(string key)

- bool ToggleKey(string key)

- bool ForgetKey(string key)

- void SelectAll()

- void Clear()

- List<string> Keys()

- List<string> ExcludedKeys()

- string ToJson()

- static SelectionModel FromJson(string text)


## ServerDataSource (class)

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

- ServerDataSource(int blockSize)

- void Setup(int blockSize)

- void SetKnownTotal(int total)

- void SetCellFieldLookup(CellFieldLookup lookup)

- override int RowCount()

- override bool ServerControlled()

- override bool CellReady(int row, int col)

- override bool HasMore()

- override void RequestBlock(int row)

- override void RequestNextBlock()

- void StartFetch(int start)

- override bool Poll()

- virtual void FetchBlock(int start, int count)

- void FinishBlock(List<DataRow> rows, int count)

- void FinishBlock(List<DataRow> rows, int count, int fetchId)

- virtual int FillBlock(int start, int count, List<DataRow> dst)

- override string CellText(int row, int col)

- override int CellNum(int row, int col)

- override double CellReal(int row, int col)

- override int CellDay(int row, int col, int order)

- override bool CellBool(int row, int col)

- override void SetCell(int row, int col, string v)

- override RowKey GetRowKey(int row)

- override DeltaResult ApplyDeltas(DeltaBatch batch)


## SortKey (class)

- int col;

- int dir;

- SortKey(int c, int d)


## SparseServerDataSource (class)

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

- void Setup(int size)

- void SetKnownTotal(int total)

- void Invalidate()

- int Generation()

- override int RowCount()

- override bool ServerControlled()

- SparseServerPage PageAt(int start)

- SparseServerPage PageFor(int row)

- bool IsPageReady(int start)

- override bool CellReady(int row, int col)

- override bool HasMore()

- override void RequestBlock(int row)

- void StartPage(SparseServerPage p)

- override void RequestNextBlock()

- override bool Poll()

- virtual void FetchPage(int start, int count, int gen)

- virtual void FetchPage(int start, int count, int gen, int pageId)

- void FinishPage(List<DataRow> rows, int count, int total, int gen)

- void FinishPage(List<DataRow> rows, int count, int total, int gen, int pageId)

- virtual int FillPage(int start, int count, List<DataRow> dst)

- SparseServerPage ReadyPage(int row)

- override string CellText(int row, int col)

- override int CellNum(int row, int col)

- override double CellReal(int row, int col)

- override int CellDay(int row, int col, int order)

- override bool CellBool(int row, int col)

- override void SetCell(int row, int col, string v)


## SparseServerPage (class)

- int start;

- int count;

- int total;

- int generation;

- int state;

- List<DataRow> rows;

- SparseServerPage(int at, int gen)

- bool Covers(int row)


## SwitchComp (class)

- List<WidgetPoolSlot> pool;

- DataColumn lastCol;

- bool lastOn;

- SwitchComp():base("switch")

- override Control Acquire(App app, DataSource src, int row, int col, RowKey key, int x, int y, int w, int h, int rows, int cols)

- override void FillStr(Control ctl, string val, DataColumn col, DataSource src, int row)

- override bool TakeCommit(Control ctl, ref string outVal)

- override void Reset()


## TableCellRange (class)

- public int row0;

- public int col0;

- public int row1;

- public int col1;

- public TableCellRange(int r0, int c0, int r1, int c1)


## TableDiagnostic (class)

- int code;

- string message;

- string field;

- int row;

- TableDiagnostic(int c, string msg, string f, int r)

- int Code()

- string Message()

- string Field()

- int Row()


## TableMutation (class)

- string mutationId;

- RowKey rowKey;

- int column;

- int expectedRevision;

- string value;

- TableMutation(string id, RowKey key, int col, int revision, string nextValue)

- static TableMutation Cell(string id, RowKey key, int col, int revision, string nextValue)

- string MutationId()

- RowKey Key()

- int Column()

- int ExpectedRevision()

- string Value()

- bool IsValid()

- string ToJson()


## TableTransaction (class)

- string transactionId;

- int mode;

- int state;

- List<TableMutation> mutations;

- string error;

- TableTransaction(string id, int transactionMode)

- static TableTransaction Create(string id, int transactionMode)

- string TransactionId()

- int Mode()

- int State()

- string Error()

- int Count()

- bool IsValid()

- bool HasMutationId(string id)

- bool Add(TableMutation mutation)

- TableMutation At(int index)

- bool BeginValidation()

- bool MarkPending()

- bool Ack()

- bool Reject(string message)

- bool MarkConflict(string message)

- bool Cancel()

- bool IsTerminal()

- string ToJson()


## TableTransactionResult (class)

- static const int Acked=1;

- static const int Rejected=2;

- static const int Conflict=3;

- static const int Cancelled=4;

- string transactionId;

- int status;

- List<MutationResult> results;

- TableTransactionResult(string id, int resultStatus)

- static TableTransactionResult Create(string id, int resultStatus)

- string TransactionId()

- int Status()

- int Count()

- bool IsValid()

- bool Add(MutationResult result)

- MutationResult ResultAt(int index)

- int AcceptedCount()

- int FailedCount()

- TableTransactionResult Copy()

- string ToJson()


## TableValue (class)

- int kind;

- string text;

- int integer;

- double real;

- bool truth;

- static TableValue Null()

- static TableValue Text(string s)

- static TableValue Int(int n)

- static TableValue Real(double n)

- static TableValue Bool(bool b)

- static TableValue Day(int n)

- int Kind()

- string AsText()

- int AsInt()

- double AsReal()

- bool AsBool()


## TransactionMode (class)

- static const int AllOrNothing=1;

- static const int BestEffort=2;


## TransactionRequestEnvelope (class)

- static const int FormatVersion=1;

- TableTransaction transaction;

- TransactionRequestToken token;

- TransactionRequestEnvelope(TableTransaction value, TransactionRequestToken requestToken)

- static TransactionRequestEnvelope Build(TableTransaction value, TransactionRequestToken token)

- TableTransaction Transaction()

- TransactionRequestToken Token()

- bool IsValid()

- string ToJson()


## TransactionRequestToken (class)

- int requestId;

- string transactionId;

- int generation;

- int schemaVersion;

- int serverRevision;

- bool cancelled;

- TransactionRequestToken(int id, string txId, int gen, int schema, int server)

- int RequestId()

- string TransactionId()

- int Generation()

- int SchemaVersion()

- int ServerRevision()

- bool IsCancelled()

- void Cancel()

- bool SameIdentity(TransactionRequestToken other)


## TransactionState (class)

- static const int Draft=1;

- static const int Validating=2;

- static const int Pending=3;

- static const int Acked=4;

- static const int Rejected=5;

- static const int Conflict=6;

- static const int Cancelled=7;


## TransposeDataSource (class)

- DataTableState st;

- DataSource inner;

- TransposeDataSource(DataTableState state, DataSource innerSrc)

- override int RowCount()

- override string CellText(int row, int col)

- override bool Poll()


## TreeAdapter (class)

- virtual int RootCount(DataSource src)

- virtual int RootAt(DataSource src, int i)

- virtual int ChildCount(DataSource src, int row)

- virtual int ChildOf(DataSource src, int row, int index)

- virtual int ParentOf(DataSource src, int row)

- virtual bool IsLazy(DataSource src, int row)

- virtual bool ChildrenLoaded(DataSource src, int row)

- virtual void LoadChildren(DataSource src, int row)


## WidgetComps (class)

- static string PoolKey(RowKey key, int col)

- static bool BoolOf(DataColumn c, string val)

- static string BoolText(DataColumn c, bool on)


## WidgetPoolSlot (class)

- public string key;

- public Control ctl;

- public WidgetPoolSlot(string key, Control ctl)


## List (delegate)

`delegate List<int> GridNums<T>(T row);`


## RowKey (delegate)

`delegate RowKey GridKey<T>(T row);`


## T (delegate)

`delegate T GridNew<T>();`


## bool (delegate)

`delegate bool GridBool<T>(T row);`


## bool (delegate)

`delegate bool XlsxExportUiFn(App app, DataTableState st, List<DataColumn> cols, DataSource src, string path, bool selectionOnly);`


## double (delegate)

`delegate double GridReal<T>(T row);`


## int (delegate)

`delegate int GridInt<T>(T row);`


## string (delegate)

`delegate string GridText<T>(T row);`


## string (delegate)

`delegate string GridUrlEnsureFn(App app, string url);`


## string (delegate)

`delegate string CellFieldLookup(int col);`


## void (delegate)

`delegate void GridSet<T>(T row, string v);`
