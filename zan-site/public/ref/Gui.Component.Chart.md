# Gui.Component.Chart

> 源码: `stdlib/Gui/Component/Chart/Chart.zan`, `stdlib/Gui/Component/Chart/ChartBig.zan`, `stdlib/Gui/Component/Chart/ChartController.zan`, `stdlib/Gui/Component/Chart/ChartLayoutRelation.zan`, `stdlib/Gui/Component/Chart/ChartLayoutSpecial.zan`, `stdlib/Gui/Component/Chart/ChartModel.zan`, `stdlib/Gui/Component/Chart/ChartResolved.zan`, `stdlib/Gui/Component/Chart/ChartView.zan`, `stdlib/Gui/Component/Chart/ChartViewBar.zan`, `stdlib/Gui/Component/Chart/ChartViewEventRiver.zan`, `stdlib/Gui/Component/Chart/ChartViewFinance.zan`, `stdlib/Gui/Component/Chart/ChartViewHeatmap.zan`, `stdlib/Gui/Component/Chart/ChartViewHier.zan`, `stdlib/Gui/Component/Chart/ChartViewLine.zan`, `stdlib/Gui/Component/Chart/ChartViewMap.zan`, `stdlib/Gui/Component/Chart/ChartViewPie.zan`, `stdlib/Gui/Component/Chart/ChartViewPolar.zan`, `stdlib/Gui/Component/Chart/ChartViewRelation.zan`, `stdlib/Gui/Component/Chart/ChartViewScatter.zan`, `stdlib/Gui/Component/Chart/ChartViewShared.zan`, `stdlib/Gui/Component/Chart/ChartViewVenn.zan`


## BoxItem (class)

箱线图统计量（最小值、下四分位数、中位数、上四分位数、最大值）。
分类标签与统计值一起携带，调用方不会因
并行列表而使标签与数值失同步。

- string label;

- int lo;

- int q1;

- int med;

- int q3;

- int hi;

- static BoxItem Of(string label, int lo, int q1, int med, int q3, int hi)


## Bubble (class)

加权散点（x、y 和一个决定气泡大小的权重）。

- int x;

- int y;

- int weight;

- static Bubble Of(int x, int y, int weight)


## Candle (class)

金融/蜡烛图的一个 OHLC 柱。

- int open;

- int high;

- int low;

- int close;

- static Candle Of(int open, int high, int low, int close)


## Chart (class)

ChartView 渲染器共享的绘图工具：颜色、比例辅助、
面板/图例框架、dataZoom 滑块、标记线/区域、直角坐标系和
双轴坐标构建器。一次性的静态渲染器（Chart.Line/
Bar/Pie/...）已移除——所有图表现在声明为 ChartOption
并由 ChartView 绘制。

- static int Rgb(int r, int g, int b)

- static int WithAlpha(int packed, int a)

- static int Palette(Theme t, int i)

- static int NiceMax(int v)

- static int MaxOf(List<ChartSeries> series)

- static int MaxOfWindow(List<ChartSeries> series, int i0, int i1)

- static int MaxStack(List<ChartSeries> series, int n)

- static int MaxStackW(List<ChartSeries> series, int n, int i0)

- static int MaxStackAll(List<ChartSeries> series, int n)

- static int MaxStackAllW(List<ChartSeries> series, int n, int i0)

- static int MaxInt(List<int> vs)

- static bool PointInPoly(List<ChartMapPoint> poly, int px, int py)

- static int Log10(int v)

- static List<int> LogTicks(int lo, int hi)

- static string TimeLabel(int t)

- static List<int> TimeTicks(int t0, int t1, int maxTicks)

- static string FormatFixed(double v, int precision)

- static int DrawPanel(App app, int x, int y, int w, int h, string title, List<ChartSeries> series, bool showLegend)

- static int LegendKey(int wid, int si)

- static int ZoomLoKey(int wid)

- static int ZoomHiKey(int wid)

- static int TreePanXKey(int wid)

- static int TreePanYKey(int wid)

- static int TreeDragXKey(int wid)

- static int TreeDragYKey(int wid)

- static int TreeDragPanXKey(int wid)

- static int TreeDragPanYKey(int wid)

- static int TreeDraggingKey(int wid)

- static int TreeZoomKey(int wid)

- static int CacheKey(int slot)

- static int ZoomI0(App app, ChartOption o, int wid, int n)

- static int ZoomI1(App app, ChartOption o, int wid, int n)

- static List<string> WindowLabels(List<string> labels, int i0, int i1)

- static void DrawZoomBar(App app, int plotX, int plotW, int barY, int barH, int wid, int n)

- static int DrawPanelI(App app, int x, int y, int w, int h, string title, List<ChartSeries> series, int wid, bool showLegend)

- static void DrawMarkArea(App app, ChartFrame f, ChartOption o)

- static void DrawMarkLine(App app, ChartFrame f, ChartOption o)

- static ChartFrame DrawFrame(App app, int x, int y, int w, int h, int plotTop, List<string> labels, int maxV)

- static ChartFrame DrawFrameLoHi(App app, int x, int y, int w, int h, int plotTop, List<string> labels, int lo, int hi)

- static int AxisMaxFor(List<ChartSeries> series, int axisIndex)

- static int AxisMinFor(List<ChartSeries> series, int axisIndex)

- static bool AnyOnAxis(List<ChartSeries> series, int axisIndex)

- static int NiceMin(int v)

- static int AxisLo(int fixedLo, int dataMin)

- static int AxisHi(int fixedHi, int dataMax, int lo)

- static int TickLabelW(int lo, int hi, int ticks, int fs)

- static ChartFrame BuildAxes(App app, int x, int y, int w, int h, int plotTop, List<string> labels, ChartOption o, List<ChartSeries> series)

- static ChartFrame BuildAxesR(App app, int x, int y, int w, int h, int plotTop, List<string> labels, ChartOption o, List<ChartSeries> series, int leftDataMax, int rightDataMax)


## ChartAreaStyle (class)

- int color;

- string type;

- static ChartAreaStyle Create()


## ChartAxis (class)

一个坐标轴（ECharts xAxis/yAxis 条目）。分类轴携带其
标签；数值轴携带可选固定范围（ChartOption.Auto()
从数据推导）和刻度数。

- ChartAxisType type;

- string title;

- int min;

- int max;

- bool reversed;

- int ticks;

- bool showGrid;

- List<string> categories;

- static ChartAxis Category(List<string> cats)

- static ChartAxis CategoryTitled(List<string> cats, string title)

- static ChartAxis Value()

- static ChartAxis ValueRange(int min, int max)

- static ChartAxis ValueTitled(string title)

- static ChartAxis Time()


## ChartBig (class)

虚拟 ChartSource 的高性能渲染器。每个系列被缩减为
最多约绘图区宽度列的 min/max 包络，缓存在 ChartState 中，
仅在数据版本或宽度变化时重建——
数百万点的折线/面积图每帧只需 O(width) 而非 O(points)，
图表从不持有每点一个对象的数组。用法：

ChartState st = new ChartState();          // 创建一次，由调用方保存
ChartBig.Render(app, st, mySource, ChartOption.Create(), x, y, w, h, "CPU");

- static bool NeedsRebuild(ChartState st, ChartSource src, int w)

- static void Build(ChartState st, ChartSource src, int target)

- static int DrawHead(App app, int x, int y, int w, int h, string title, ChartSource src)

- static void Render(App app, ChartState st, ChartSource src, ChartOption o, int x, int y, int w, int h, string title)
  - 将源数据作为抽取后的折线/面积图绘制到给定矩形中。


## ChartController (class)

ChartOption 的可变持有者。用于立即模式 GUI 中的动态数据：
控件可以每帧重新绑定同一个 controller，而不需要重建 ChartView。

- ChartOption option;

- int version;

- static ChartController Of(ChartOption option)

- ChartOption Option()

- int Version()

- ChartController SetOption(ChartOption next)
  - 替换整个 option，保留 controller 身份与图表交互键。

- ChartController SetData(int seriesIndex, List<ChartData> data)
  - 替换一个系列的数据项。

- ChartController AppendData(int seriesIndex, List<ChartData> data)
  - 追加已命名/着色的数据项，适合实时折线和滚动窗口。

- ChartController AppendValues(int seriesIndex, List<int> values)
  - 追加纯数值数据的快捷形式。

- ChartController ClearData(int seriesIndex)
  - 清空一个系列的数据，保留系列名称、类型、样式和轴绑定。


## ChartElementType (enum)

图表命中图元分类。`Chart` 表示整图空白区域；其余类型表示可提供数据点事件的图元（全部成员均有对应 renderer）。

- Chart
- LinePoint
- Bar
- ScatterPoint
- HeatmapCell
- PieSlice
- MapRegion
- FunnelStage
- Node
- Link
- VennCircle
- EventBand
- RadarAxis
- Candlestick
- BoxPlot
- ErrorBar
- Gauge
- WordTag


## ChartHit (class)

渲染器内部使用的当前几何命中快照。`Same` 只比较图元、系列和源数据索引，不比较鼠标像素或可变展示名称，因此同一数据点内移动不会重复触发 Hover。

- ChartElementType elementType;

- int seriesIndex;

- string seriesName;

- int dataIndex;

- string dataName;

- int value;

- bool numberValueSet;

- double numberValue;

- bool xValueSet;

- double xValue;

- bool yValueSet;

- double yValue;

- int pixelX;

- int pixelY;

- int axisIndex;

- static ChartHit Of(ChartElementType elementType, int seriesIndex, string seriesName, int dataIndex, string dataName, int value, int pixelX, int pixelY)

- static bool Same(ChartHit a, ChartHit b)

- static ChartHit Copy(ChartHit src)


## ChartData (class)

一个数据点（ECharts series.data[i]）：一个值加可选名称
（扇区/分类标签）和颜色。颜色 0 表示"继承系列
颜色"——这是 ECharts 三级控制的第三级
（option < series < data item）。

- int val;

- double number;

- bool numberSet;

- string name;

- int color;

- bool hidden;

- static ChartData Of(int v)

- static ChartData Numeric(double v)

- static ChartData NamedNumber(double v, string name)

- static ChartData Named(int v, string name)

- static ChartData Colored(int v, int color)

- static ChartData Full(int v, string name, int color)
  - `Numeric/NamedNumber` 保留原始 double，并通过 `numberSet` 表示该字段存在；`Value/Number` 分别提供兼容整数和原始数值视图。


## ChartDataset (class)

ECharts 风格共享数据源：命名维度按列存储，外加
可选的 x 轴分类标签。一个数据集可通过
EncodeBy / EncodeNamed 选择不同维度来驱动多个图表——
调用方从不复制或重构底层列表。

- List<ChartDimension> dimensions;

- List<string> cats;

- static ChartDataset Of(List<ChartDimension> dimensions)

- static ChartDataset WithCats(List<ChartDimension> dimensions, List<string> cats)

- int RowCount()

- int DimOf(string name)
  - 按名称查找维度的索引，不存在返回 -1。

- List<string> Labels()
  - x 轴分类标签：给定 `cats` 时用显式值，否则
    用字符串形式的 1..RowCount。

- ChartOption EncodeBy(string title, List<int> yDims, ChartType type)
  - 从一组数值维度（按索引）构建 ChartOption。
    系列名取自维度名；颜色从主题自动解析。

- ChartOption EncodeNamed(string title, List<string> yNames, ChartType type)
  - 同 EncodeBy，但按名称选择维度。


## ChartDimension (class)

一个命名、按列存储的数据集维度。

- string name;

- List<int> values;

- static ChartDimension Of(string name, List<int> values)


## ChartInteractionEvent (class)

带载荷的 typed 图表交互事件。未命中的图表级事件使用 `dataHit=false`、`elementType=Chart`、`dataIndex=-1`；命中任意图元时填充源/布局索引、名称、兼容整数值和可用的 double/x/y 字段。`ChartElementType` 全部成员均有对应 renderer（含 VennCircle/EventBand/RadarAxis/Candlestick/BoxPlot/ErrorBar/Gauge/WordTag）。

- ChartEventType type;

- ChartElementType elementType;

- bool dataHit;

- int chartWid;

- int seriesIndex;

- string seriesName;

- int dataIndex;

- string dataName;

- int value;

- bool numberValueSet;

- double numberValue;

- bool xValueSet;

- double xValue;

- bool yValueSet;

- double yValue;

- int x;

- int y;

- int axisIndex;

- bool selected;

- int zoomStart;

- int zoomEnd;

- static ChartInteractionEvent Of(ChartEventType type, int chartWid)

- static ChartInteractionEvent DataPoint(ChartEventType type, int chartWid, int seriesIndex, string seriesName, int dataIndex, string dataName, ChartElementType elementType)

- static string ElementName(ChartElementType type)

- string TypeName()

`PieSelected` 与 `MapSelected` 是选择状态变化事件，与通用点级 `Click` 分开；右键和地图拖拽释放不会触发选择。事件计算不依赖 `showTooltip`。这组 API 是 Zan typed delegate，不等同 ECharts DOM `event.params` 或完整 JavaScript `on/un`。


## ChartEvent (class)

Event River 的一个事件：名称、时间区间 [start, end]、值
（驱动带宽）、可选颜色。普通标量 data 兼容转换为等距事件。

- string name;

- int start;

- int end;

- int value;

- int color;

- static ChartEvent Of(string name, int start, int end, int value)


## ChartFrame (class)

纯 Zan 图表库 —— ECharts 2.2.x 的声明性子集，全部用
stdlib 组件与确定性的整数几何绘制，无 JS / DOM / WebView：
· 17 种原生 series.type：line / bar / pie / scatter / k /
radar / chord / force / map / gauge / funnel / eventRiver /
treemap / tree / wordCloud / heatmap，外加 custom kind
（sankey / box / error / waterfall / sunburst / radialBars /
venn）。
· 轴：category / value / time（整数天数，表格驱动日期）/
log（仅正值、10 的幂刻度），独立左右轴 min/max/reversed。
· 交互：hover tooltip、图例切换可见性、dataZoom 窗口、
markLine / markPoint / markArea、emphasis（hover）paint。
· 确定性：无随机数、无系统时钟；布局/命中/指纹均为整数运算，
conformance 测试可逐值断言。
Zan 扩展（超出 ECharts 2.2.x 的便捷 API）：ChartOption.Of /
OfRings、ChartSeries.Line / Bar / Area / OfRegions / OfEvents /
OfBoxes / OfErrors / OfPoints、pieRings 嵌套环、eventRiver 由
数值系列驱动（按类别索引映射时间）、图表数据用 List<int> /
ChartData 直接表达。
渲染由 ChartView（partial）分发到每类一个渲染器；所有绘制走
ChartOption -> ResolvedChart -> Canvas 管道。

内部持有者：计算出的绘图矩形加上解析后的数值轴
范围（左轴 + 可选右轴），使每个渲染器以相同方式
映射数值 -> 像素。leftMin/leftMax 始终有效；rightMin/rightMax 仅在有右轴时有效。
leftLog/rightLog 为对数轴（仅正值）；isTime 为时间 X 轴
（整数天数，标签由 Chart.TimeLabel 生成）。

- int plotX;

- int plotY;

- int plotW;

- int plotH;

- int leftMin;

- int leftMax;

- int rightMin;

- int rightMax;

- bool hasRight;

- bool reversed;

- bool rightReversed;

- bool leftLog;

- bool rightLog;

- bool isTime;

- int timeMin;

- int timeMax;

- int YOf(int v, int axisIndex)

- int YOfLog(int v, int axisIndex)

- int XOfTime(int t)


## ChartItemStateStyle (class)

- int color;

- int borderColor;

- int borderWidth;

- ChartLineStyle lineStyle;

- ChartAreaStyle areaStyle;

- static ChartItemStateStyle Create()


## ChartItemStyle (class)

- ChartItemStateStyle normal;

- ChartItemStateStyle emphasis;

- static ChartItemStyle Create()


## ChartKinds (class)

- static ChartType TypeOf(string s)

- static string CustomOf(string s)
  - 非 ECharts 2.2.7 原生类型的扩展渲染器标签（"box"、"error"、
    "sankey"、"waterfall"、"sunburst"、"radialBars"）；原生类型为 ""。

- static string TypeName(ChartType t)

- static ChartAxisType AxisOf(string s)

- static string AxisName(ChartAxisType t)


## ChartLineStyle (class)

- int color;

- int width;

- string type;

- int shadowColor;

- int shadowBlur;

- static ChartLineStyle Of(int color, int width)


## ChartLink (class)

Sankey 图的有向流转链接：从节点 -> 到节点，带一个值
（链接粗细）。节点在渲染时从链接推导。

- string from;

- string to;

- int val;

- static ChartLink Of(string from, string to, int val)


## ChartMapPoint (class)

地图多边形的一个顶点（已规范化为整数）。

- int x;

- int y;

- static ChartMapPoint Of(int x, int y)


## ChartMapRegion (class)

地图区域：名称、值（驱动连续色域）、可选颜色和多条环
（单环=单部件；多外环=多部件；hole=true 的环减去面积）。

- string name;

- int value;

- int color;

- List<ChartMapRing> rings;

- static ChartMapRegion Of(string name, int value, List<ChartMapRing> rings)

- static ChartMapRegion Colored(string name, int value, int color, List<ChartMapRing> rings)


## ChartMapRing (class)

地图区域的一条环：外环勾出区域轮廓，hole=true 的环是其中的洞。
多部件区域用多条外环表达。

- List<ChartMapPoint> points;

- bool hole;

- static ChartMapRing Of(List<ChartMapPoint> points, bool hole)


## ChartMarkPoint (class)

标记点（ECharts markPoint.data）：在线/柱系列上标注极值或显式点。
kind = "max" | "min" | "point"；dataIndex 命中系列内的数据点；
xValue/yValue 用于显式坐标（dataIndex < 0 时）。

- string kind;

- int dataIndex;

- int xValue;

- int yValue;

- string label;

- int color;

- static ChartMarkPoint Max()

- static ChartMarkPoint Min()

- static ChartMarkPoint Point(int xValue, int yValue, string label)


## ChartNode (class)

Treemap / Sunburst 图的层级节点：名称、值和
嵌套子节点。叶子携带值；组的值由
其子节点推导（渲染为组的总量 / 环扫掠）。

- string name;

- int val;

- List<ChartNode> children;

- static ChartNode Leaf(string name, int val)

- static ChartNode Group(string name, List<ChartNode> children)

- int Total()
  - 此节点下所有叶子值的总和（组的总量）。


## ChartOption (class)

完整图表配置（ECharts option）：标题、坐标轴、
系列和所有展示选项集中一处。用 Create() /
Of(series) / FromJson() 构建，直接调整字段，然后交给
ChartView.Of(option).Render(app, x, y, w, h)。三级控制对应
ECharts：option.*（全局）< series.*（逐系列）< data[i].*（逐项）。

- string title;

- List<ChartAxis> xAxes;

- List<ChartAxis> yAxes;

- List<ChartSeries> series;

- ChartType defaultType;

- bool showLegend;

- bool showTooltip;

- bool showGrid;

- bool showValues;

- bool animate;

- bool smooth;

- bool gradient;

- bool rounded;

- int animMs;

- int donutPct;

- int gaugeMax;

- bool pieLabels;

- bool pieRings;

- List<int> radarMax;

- bool zoom;

- int markLine;

- bool markPoint;

- string markLabel;

- int markLo;

- int markHi;

- int gridLeft;

- int gridRight;

- int gridTop;

- int gridBottom;

- int zoomStart;

- int zoomEnd;

- int windowStart;

- int windowEnd;

- int backgroundColor;

- int iwid;

- static int Auto()
  - 哨兵值，表示"此边界从数据推导"。

- static ChartOption Create()

- static ChartOption Of(List<ChartSeries> series)

- static ChartOption Of(string title, List<string> categories, List<ChartSeries> series)
  - 声明式一次性构建：标题 + 分类轴 + 完整系列列表——
    对应 ECharts `{ title, xAxis.data, series }`。混合图表只是
    列表中不同类型的系列。

- static ChartOption Single(ChartSeries s)

- static ChartOption OfRings(string title, List<ChartSeries> series)
  - 多系列嵌套环饼图：每个 Pie 系列绘制为一层环。

- static ChartOption With(ChartType t, string title, List<string> cats)
  - 基础工厂：默认系列类型 + 可选分类轴。饼/环
    系列族将分类用作扇区标签；直角坐标系系列族用作
    x 轴标签。

- static ChartOption Line(string title, List<string> cats)

- static ChartOption Bar(string title, List<string> cats)

- static ChartOption Pie(string title, List<string> cats)

- static ChartOption Scatter(string title)

- static ChartOption Treemap(string title)

- ChartOption Add(string name, List<int> values)
  - 添加一个选项默认类型的系列；颜色自动分配。

- ChartOption Add(string name, ChartType type, List<int> values)
  - 添加显式类型的系列（混合图表：柱 + 折线叠加）。

- ChartOption AddLabel(string name, List<ChartData> data)
  - 从带名称/颜色的数据项添加系列（饼图的逐项标签、
    瀑布图总量柱、逐扇区颜色等）。

- ChartOption Add(ChartSeries s)
  - 添加现成系列（显式颜色 / 专用数据列表）。

- ChartOption Categories(List<string> cats)
  - 快速设置分类 x 轴（ECharts xAxis.data）。

- ChartOption X(ChartAxis ax)

- ChartOption Y(ChartAxis ax)

- ChartOption RadarMax(List<int> maxs)

- ChartOption DataZoom(int start, int end)
  - 设置 dataZoom 的初始窗口（百分比，自动钳制到 0..100）。

- ChartOption Grid(int left, int right, int top, int bottom)
  - 覆盖绘图区边距，等价于 ECharts grid.left/right/top/bottom。

- ChartAxis XAxis0()

- ChartAxis YAxis0()

- ChartAxis YAxis1()

- List<string> XCategories()
  - x 轴的分类标签：第一个 x 轴的分类，否则为
    第一个系列的数据名，再否则 1..N。

- static ChartOption FromJson(string src)
  - 解析 ECharts 风格的选项文档。结构：
    
    {"title":"Revenue",
    "xAxis":{"type":"category","data":["Q1","Q2","Q3"]},
    "yAxis":{"type":"value"},
    "series":[{"type":"bar","name":"Rev","data":[12,28,20]},
    {"type":"line","name":"Cost","data":[{"value":8,"name":"Q1"},14]}]}
    
    旧的紧凑结构（labels + series[].values）仍然接受。

- static ChartScalar ParseScalar(JsonValue v, ChartScalar dflt)

- static int ParseColor(JsonValue v, int dflt)

- static void ParseTextStyle(JsonValue v, ChartTextStyle text)

- static void ParseLineStyle(JsonValue v, ChartLineStyle line)

- static ChartPosition ParsePoint(JsonValue v, ChartPosition dflt)

- static void ParseGauge(JsonValue v, GaugeOption gauge)

- static void ParseItemStateStyle(JsonValue v, ChartItemStateStyle style)

- static void ParseItemStyle(JsonValue v, ChartItemStyle style)

- static void ParseSeriesStyle(JsonValue v, ChartSeries s)

- static void ParseGrid(JsonValue v, ChartOption o)

- static void ParseDataZoom(JsonValue v, ChartOption o)

- static void ParseMarkArea(JsonValue v, ChartOption o)

- static int StackIdFor(string name)

- static void ParseLinks(JsonValue v, List<ChartLink> into)

- static void ParseCandles(JsonValue v, List<Candle> into)

- static void ParseBubbles(JsonValue v, List<Bubble> into)

- static void ParseBoxes(JsonValue v, List<BoxItem> into)

- static void ParseErrors(JsonValue v, List<ErrorItem> into)

- static void ParsePoints(JsonValue v, List<ChartPoint> into)

- static void ParseEvents(JsonValue v, List<ChartEvent> into)

- static List<ChartMapPoint> ParseMapPoints(JsonValue arr)

- static void ParseRegions(JsonValue v, List<ChartMapRegion> into)

- static void ParseMarks(JsonValue v, List<ChartMarkPoint> into)

- static ChartOption FromJsonValue(JsonValue v)

- static void ParseAxis(JsonValue v, List<ChartAxis> into, bool isX)

- static void ParseAxisOne(JsonValue v, List<ChartAxis> into)


## ChartPoint (class)

- int x;

- int y;

- static ChartPoint Of(int x, int y)


## ChartPosition (class)

- ChartScalar x;

- ChartScalar y;

- static ChartPosition Of(ChartScalar x, ChartScalar y)


## ChartScalar (class)

- int amount;

- bool percent;

- static ChartScalar Px(int amount)

- static ChartScalar Pct(int amount)


## ChartSeries (class)

一个数据系列（ECharts series[i]）。每个系列携带自己的类型，
混合图表自然实现：带折线叠加的柱状图只是
共享同一坐标系的两个不同类型系列——无需 AsKind 垫片。
axisIndex 将系列绑定到 yAxis（0 = 左/主，1 = 右/次）；
stack 将系列归入一个堆叠柱/面积（相同正 id；
StackedBar / StackedArea 工厂默认 stack = 1）。
旧的伪类型（Area、StepLine、Donut、Rose 等）已移除：其行为现由
系列子实体表达——areaStyle、step、innerPct、roseType、sort
——与 ECharts 的建模完全一致。数据为统一的 List<ChartData>；
多值系列族（candle/bubble/box/error/scatter）使用专用的
列表。

- string name;

- ChartType type;

- int axisIndex;

- int stack;

- string stackName;

- int color;

- bool hidden;

- int smooth;

- bool areaStyle;

- string step;

- string sort;

- string roseType;

- int innerPct;

- bool hbar;

- string treeOrient;

- string custom;

- ChartItemStyle itemStyle;

- ChartTextStyle label;

- bool labelShow;

- List<ChartData> data;

- List<Candle> candles;

- List<Bubble> bubbles;

- List<BoxItem> boxes;

- List<ErrorItem> errors;

- List<ChartPoint> points;

- List<ChartNode> tree;

- List<ChartLink> links;

- List<ChartMapRegion> regions;

- List<ChartEvent> events;

- List<ChartMarkPoint> marks;

- GaugeOption gauge;

- static ChartSeries Of(string name, ChartType type, List<int> values)
  - 从纯 int 值构建系列（每个值变为 ChartData.Of(v)）。

- static ChartSeries Named(string name, ChartType type, List<ChartData> data)
  - 从显式数据项构建系列（名称 / 逐项颜色）。

- static ChartSeries Line(string name, List<int> values)

- static ChartSeries Area(string name, List<int> values)

- static ChartSeries Bar(string name, List<int> values)

- static ChartSeries StackedBar(string name, List<int> values)

- static ChartSeries HBar(string name, List<int> values)

- static ChartSeries StepLine(string name, List<int> values)

- static ChartSeries StackedLine(string name, List<int> values)

- static ChartSeries StackedArea(string name, List<int> values)

- static ChartSeries Pie(string name, List<int> values)

- static ChartSeries Donut(string name, List<int> values)

- static ChartSeries Gauge(string name, int val)

- static ChartSeries GaugeNumber(string name, double val)

- static ChartSeries Polar(string name, List<int> values)

- static ChartSeries WordCloud(string name, List<ChartData> words)

- static ChartSeries Funnel(string name, List<int> values)

- static ChartSeries Pyramid(string name, List<int> values)

- static ChartSeries Rose(string name, List<int> values)

- static ChartSeries OfCandles(string name, List<Candle> candles)
  - 由 OHLC 柱构建蜡烛图系列（ECharts type "k"）。

- static ChartSeries OfBubbles(string name, List<Bubble> bubbles)
  - 由气泡构建加权散点系列（ECharts scatter + symbolSize）。

- static ChartSeries OfBoxes(string name, List<BoxItem> boxes)
  - 由逐分类统计量构建箱线图系列（扩展渲染器：
    ECharts 2.2.7 没有 boxplot 类型）。

- static ChartSeries OfErrors(string name, List<ErrorItem> errors)
  - 由值 +/- 误差对构建误差条系列（扩展渲染器）。

- static ChartSeries OfPoints(string name, List<ChartPoint> points)
  - 由显式 x/y 点构建散点系列。

- static ChartSeries OfTree(string name, ChartType type, ChartNode root)
  - 由根节点构建层级系列（Treemap / Tree / Sunburst）。
    系列持有根列表；子节点可任意深度嵌套。

- static ChartSeries OfTreeOrient(string name, ChartNode root, string orient)
  - 带显式布局方向的树系列（ECharts tree.orient）。

- static ChartSeries OfLinks(string name, List<ChartLink> links)
  - 由有向流转链接构建 Sankey 系列（节点从链接推导；
    扩展渲染器——ECharts 2.2.7 没有 sankey 类型）。

- static ChartSeries Chord(string name, List<ChartLink> links)
  - 和弦图系列：节点从 links 的 from/to 推导。

- static ChartSeries Force(string name, List<ChartLink> links)
  - 力导向图系列：节点从 links 的 from/to 推导。

- static ChartSeries Venn(string name, List<ChartData> data)
  - 韦恩图系列：data 项依次为 [A, B, A∩B]（名称 + 值）。

- static ChartSeries Waterfall(string name, List<ChartData> data)
  - 由命名的累计柱数据构建瀑布图系列（渲染器构建
    不可见的堆叠基底；扩展渲染器）。

- static ChartSeries SunburstTree(string name, ChartNode root)
  - 由层级根构建 Sunburst 系列（径向环；
    扩展渲染器——ECharts 2.2.7 没有 sunburst 类型）。

- static ChartSeries RadialBars(string name, int val)
  - 同心进度环，一个系列 = 一个环（扩展渲染器；
    option 的 gaugeMax 为满刻度）。

- static ChartSeries OfRegions(string name, List<ChartMapRegion> regions)
  - 地图系列：从调用方提供的多部件区域构建。渲染器
    不硬编码世界地图，polygon 由调用方决定（确定性
    样例见 gallery）。

- static ChartSeries OfEvents(string name, List<ChartEvent> events)
  - Event River 系列：从显式事件构建。

- ChartSeries MarkPoint(ChartMarkPoint m)
  - 给线/柱系列追加一个 markPoint。option.markPoint=true 时
    渲染器也会自动推导 max/min；显式 marks 优先展示。

- ChartSeries GaugeRange(int min, int max)
  - 仪表盘的几何与范围。百分比相对图表主体。

- ChartSeries GaugeArc(int startAngle, int endAngle)

- ChartSeries GaugeLayout(int centerXPct, int centerYPct, int radiusPct)

- ChartSeries GaugeTicks(int splitNumber, int minorTicks)

- ChartSeries GaugeSizes(int axisWidth, int tickLength, int splitLength, int pointerLengthPct, int pointerWidth)

- ChartSeries GaugeVisible(bool axis, bool ticks, bool labels, bool title, bool detail)

- ChartSeries GaugeText(int titleX, int titleY, int detailX, int detailY, string suffix)

- ChartSeries GaugeColors(int stop1, int color1, int stop2, int color2, int stop3, int color3, int stop4, int color4)

- int Count()

- double Number(int i)

- int Value(int i)


## ChartSource (class)

图表的虚拟、拉取式数据源——图表无需持有
原始点数组，数百万点的系列也不产生逐单元对象
分配（对应 DataTable 的 DataSource）。子类化它，
按需从自己的列式/类型化存储读取点，或用 SeriesListSource 包装
内存中的 List<ChartSeries>。

- virtual int SeriesCount()

- virtual string SeriesName(int s)

- virtual int SeriesColor(int s)

- virtual int PointCount(int s)

- virtual int ValueAt(int s, int i)


## ChartStage (class)

漏斗/玫瑰/饼图的一个阶段：值、填充色和显示名。

- int val;

- int color;

- string name;

- ChartStage(int v, int col, string nm)


## ChartState (class)

高性能图表路径的缓存 min/max 抽取。每个大图表
持有一个（类似 DataTableState）；底层数据变化时
调用 Invalidate()。ChartBig 惰性填充并在帧间复用。

- int version;

- int builtVersion;

- int builtW;

- int builtSeries;

- int cols;

- int axisLo;

- int axisHi;

- List <List<int>> colMin;

- List <List<int>> colMax;

- ChartState()

- void Invalidate()
  - 将缓存标记为过期，使下次渲染重新抽取源数据。


## ChartTextStyle (class)

- int color;

- int fontSize;

- string fontStyle;

- string fontWeight;

- string fontFamily;

- int shadowColor;

- int shadowBlur;

- static ChartTextStyle Create()


## ChartView (class)

组件化、带动画的图表控件（Chart 控件的现代形态）。
一次性绑定 ChartOption（ECharts 风格：标题 + 轴 + 系列，
每个系列携带自己的类型），然后每帧调用 Render。
它会播放增长入场动画，绘制渐变/平滑系列和可选的悬停
十字线 + 数值工具提示。多级控制对应 ECharts：option.*
（全局）< series.*（逐系列）< data[i].*（逐项），因此混合图表
只是共享同一坐标系的不同类型系列。

- ChartOption option;

- ChartOption sourceOption;

- ChartController controller;

- ResolvedChart resolved;

- bool staticFrame;

- int wid;

- static string ttName;

- static int ttVal;

- static int ttX;

- static int ttY;

- static int ttW;

- static int ttH;

- static int ttA0;

- static int ttA1;

- static int ttDepth;

- static ChartView Of(ChartOption o)

- static ChartView Keyed(ChartOption o, int key)
  - 绑定一个选项及调用方所有的稳定动画键。

- static ChartView Bind(ChartController controller)
  - 将 ChartView 绑定到可变 controller。Render 时读取 controller 的当前 option。

- static ChartView BindKeyed(ChartController controller, int key)
  - 带稳定交互键的 controller 绑定，适合列表和虚拟化单元格。

- static int cacheSeq=16;

- static int AllocCacheSlot()

- static void ReleaseCached(Canvas c, int slot)
  - 释放 RenderCached 槽位拥有的像素缓冲区（在区域
    离开视口时调用，如网格单元格滚走）。槽位稍后
    可复用——指纹键按槽位区分，过期的快照
    直接失配并重新渲染。

- static bool RenderCached(App app, ChartOption o, int slot, int x, int y, int w, int h)
  - 静态且光栅化成本高的图表的缓存区域渲染（热力图、
    密集系列）：首次调用把最终形态（关闭动画）绘制进
    快照槽位 0-7 并记录数据指纹；后续调用以单次拷贝
    恢复缓存像素而非重新光栅化，
    因此 100x50 热力图每帧成本为 O(区域) 而非 O(单元格)。
    缓存对任何数据 / 可见性 / 缩放 / 主题 /
    几何变化自动失效（指纹比较 + 几何匹配）。
    悬停反馈（如 HeatmapHover）由调用方在命中*之后*绘制，
    使工具提示保持活跃。帧由缓存提供时返回 true。

- static int CacheFingerprint(App app, ChartOption o, ResolvedChart resolved, int x, int y, int w, int h)
  - RenderCached 的数据指纹：系列值 + 类型/轴/隐藏、
    dataZoom 窗口、当前主题，以及全部专用数据
    （links/tree/points/candles/bubbles/boxes/errors/events/
    regions/marks/gauge/radarMax）。任何会改变
    像素的因素都使缓存快照失效；悬停位置刻意不
    不（悬停层在缓存命中后绘制于其上）。

- static int FoldStr(int h, string s, int seed)
  - 把字符串内容折叠进指纹。Zan 没有 char 值类型，但
    s[i] 给出字符编码（UTF-8 字节）；逐字节折叠使长度相同、
    内容不同的标签/名称折叠出不同值，避免陈旧缓存。
    空串返回 h 原值。

- static int FingerprintCore(int themeGen, int zoomLo, int zoomHi, ChartOption o, ResolvedChart resolved)
  - 指纹的纯核心。主题代（themeGen）与 dataZoom 状态
    （zoomLo/zoomHi）作为整数注入，使 conformance 测试
    无需 App 即可验证数据/可见性/主题/缩放变化都会改变指纹；
    CacheFingerprint 从 App 读取这两个状态，语义与旧版完全一致。

- static ChartSeries LeadSeries(ChartOption o)
  - 第一个可见序列（整图系列族从中选取布局
    标志位 —— innerPct、roseType、sort、hbar、custom）。

- static ChartType LeadType(ChartOption o)
  - 主导图表族：第一个可见序列的类型（
    整图系列族 —— pie、gauge、heatmap 等 —— 从中选择布局，
    笛卡尔系列族则逐序列混用）。

- static string DispatchKind(ChartOption o)
  - 渲染器分发键：每个 ChartType 和 custom kind 都映射到
    一个显式渲染器；没有未知类型回退为折线图。
    未知/空 kind 返回 "empty"（渲染明确的空状态）。
    这是 chart_kinds_complete 测试的确定性锚点。

- static void DrawEmptyState(App app, int x, int y, int w, int h, ChartOption o)
  - 未知 / 空 kind 的明确空状态：不绘制错误的图表。

- static void OfferActiveHit(ChartHit hit)
  - 渲染器内部命中 seam；由 `ChartView` 统一比较前后帧并派发 Hover、MouseOut、Click。

- static bool HasInteraction(ChartView v, ChartEventType type)
  - 同时检查 view 与 controller 的 typed 事件订阅。

- static bool OwnsActiveClick(App app, int id)
  - 选择事件的点击所有权检查；几何命中仍由对应 renderer 完成。

### Typed data-point events

`ChartInteractionEvent` 的 `dataHit`、`elementType`、源 `dataIndex`、`dataName`、`value` 和可选 `numberValue/numberValueSet` 描述数据图元命中。散点还填充 `xValue/yValue`；屏幕位置仍由 `x/y` 表示。全部 `ChartElementType` 成员均有 renderer 接入：line/bar/scatter/heatmap/pie/map 数据点，Chord/Force/Sankey/Treemap/Sunburst 的 Node/Link，Bubbles 散点，Venn 的 VennCircle（0=A/1=B/2=交集），EventRiver 的 EventBand（稳定排序前的源下标），Radar 的 RadarAxis（轴下标），Funnel 的 FunnelStage，Finance 的 Candlestick/BoxPlot/ErrorBar，Gauge，WordCloud 的 WordTag。`showTooltip=false` 不会关闭事件；点级事件优先于空白图表级事件。`PieSelected` / `MapSelected` 是独立的选择状态变化事件。


- static bool HasArea(ChartOption o)
  - 任一序列为面积线时返回 true（对应 ECharts line.areaStyle.show）。

- static bool IsLineFamily(ChartType t)
  - 折线族类型以折线绘制（可选填充 / 阶梯 /
    堆叠 —— 目前均为 Line 类型；它们是序列的子实体）。

- static bool IsBarFamily(ChartType t)
  - 柱状族类型占据一个柱位（分组 / 堆叠 / 水平）。

- static int GroupIndex(List<string> groups, string name)
  - 堆叠组名称的索引，无则 -1（柱状图按 stackName 分组）。

- void Render(App app, int x, int y, int w, int h)

- void DispatchRender(App app, int x, int y, int w, int h, int g)
  - 显式分发：每个 kind 都有一个明确的分支。没有未知
    kind 回退为折线图；未知/空 kind 绘制明确空状态。


## ChartView (class)

- static void DrawBars(App app, int x, int y, int w, int h, ChartOption o, int g, bool stacked, bool horizontal)

- static void BarCell(Canvas c, int x, int y, int w, int h, int rad, int color, bool gradient)
  - 单个柱格，可选垂直渐变（color -> 更浅色）。

- static List<ChartSeries> Scaled(List<ChartSeries> series, int g)
  - 返回 `series` 的副本，每个值按 g/1000 缩放（用于
    给只接受普通序列列表的渲染器做动画）。

- static void HBarCore(App app, int x, int y, int w, int h, string title, List<string> labels, List<ChartSeries> series, bool stacked, bool showLegend)
  - 水平柱状图：类别在左纵排，数值横伸。stacked 为 true 时，
    共享 stackName 的序列每组堆成一条水平色带，
    （对应 ECharts stacked bar4），各行上下排列各组。

- static void DrawWaterfall(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void DrawEventRiver(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void DrawCandles(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawBubbles(App app, int x, int y, int w, int h, ChartOption o, int g)

- static List<string> BoxLabels(List<BoxItem> items)

- static List<string> ErrorLabels(List<ErrorItem> items)

- static void DrawBox(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawError(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void DrawHeatmap(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawHeatmapCore(App app, int x, int y, int w, int h, ChartOption o, int g)
  - 仅热力图主体（面板、表头、色块）—— 不含悬停覆盖层，
    以便 RenderCached 缓存，指针反馈在
    缓存命中后重绘于其上。

- static void HeatmapHover(App app, int x, int y, int w, int h, ChartOption o)
  - 热力图悬停覆盖层：高亮指针下的单元格并
    显示其值。在 DrawHeatmapCore（或 RenderCached
    恢复）之后绘制，缓存主体在鼠标移动时无需重绘。


## ChartView (class)

- static void DrawTreemap(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void TreemapSlice(App app, int x, int y, int w, int h, List<ChartNode> nodes, int depth)

- static void TreemapCell(App app, int x, int y, int w, int h, string name, string val)

- static int TreeDepth(ChartNode n)

- static void TreemapHit(App app, int x, int y, int w, int h, List<ChartNode> nodes, int depth, int px, int py)

- static void SunburstHit(App app, int cx, int cy, int ringH, ChartNode node, int depth, int a0, int a1, int px, int py)

- static void DrawSunburst(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void SunburstRing(App app, int cx, int cy, int ringH, ChartNode node, int depth, int a0, int a1, int sweepMax, int parentCol)

- static int SankeyIndex(List<SankeyNodeLayout> nodes, string name)

- static void DrawSankey(App app, int x, int y, int w, int h, ChartOption o, int g)

- static int TreeLeafCount(ChartNode n)
  - 节点下的叶子槽位数（一个叶子 = 1 个槽位）。

- static int TreeDepth(ChartNode n)
  - 节点下方最深路径的深度（边数）。

- static void TreeAssign(ChartNode n, int lo, int depth, List<TreeLayoutNode> layout)
  - 为每个节点分配 span 坐标：节点跨越叶子 [lo, hi)，
    其中心位于该叶子范围中点。span 在垂直树中
    从左到右，在水平树中从上到下。

- static int TreeSpan(TreeLayoutNode node, int spanG, int leaves)
  - 布局节点在增长缩放后的 span 上叶子范围的中心。

- static int TreeLabelWidth(ChartNode n, int fs)

- static bool WordCloudFits(List<WordCloudItem> placed, int x, int y, int w, int h)

- static void DrawWordCloud(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawTree(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void BuildPath(List<int> px, List<int> py, bool smooth, List<int> ox, List<int> oy)

- static void BuildPathFx(List<int> px, List<int> py, bool smooth, List<int> ox, List<int> oy)

- static void FillUnder(Canvas c, List<int> ox, List<int> oy, int baseY, int color, bool gradient, int topA, int botA)

- static void FillColumnAA(Canvas c, int px, int yyF, int baseY, int color, bool gradient, int topA, int botA, int solidA)

- static void FillBandColumnAA(Canvas c, int px, int topF, int botF, int color, bool gradient, int topA, int botA, int solidA)

- static void FillBandFx(Canvas c, List<int> topX, List<int> topY, List<int> botX, List<int> botY, int color, bool gradient, int topA, int botA)

- static void PolyLine(Canvas c, List<int> px, List<int> py, bool smooth, int color, int th)

- static void DrawLines(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawStackedArea(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static int MapRamp(int value, int vMin, int vMax)

- static int Floordiv(int a, int b)

- static void FillPolygon(Canvas c, List<ChartMapPoint> poly, int color)

- static void PolyOutline(Canvas c, List<ChartMapPoint> poly, int color)

- static void DrawMap(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void DrawPie(App app, int x, int y, int w, int h, ChartOption o, bool sliceLabels, int g)

- static int GaugeSweep(GaugeOption gauge)

- static int GaugeStart(GaugeOption gauge)

- static int GaugeScalar(App app, ChartScalar scalar, int extent)

- static int GaugeX(int angle)

- static int GaugeY(int angle)

- static void GaugeSector(Canvas c, int cx, int cy, int inner, int outer, int start, int sweep, int color)

- static int GaugeColor(GaugeOption gauge, int perMille)

- static int GaugeFont(App app, int requested, int fallback, int radius, int permilleOfRadius)
  - 仪表文字大小：表盘的标签、标题和读数按表盘定尺寸，
    而非窗口。固定 px 字号（ECharts 的 30px detail）只在
    demo 的表盘尺寸下合适 —— 小卡片上同样字号会盖住
    刻度标签并溢出圆弧，这正是此前
    仪表与参照完全不像的原因。`permilleOfRadius` 把
    请求字号限制在半径的一定比例内。

- static void DrawGauge(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawRadialBars(App app, int x, int y, int w, int h, ChartOption o, int g)

- static List<ChartStage> GatherStages(App app, List<ChartSeries> series)

- static void DrawFunnel(App app, int x, int y, int w, int h, ChartOption o, int g, bool pyramid)

- static string StageName(int i)

- static void DrawRose(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static int RadarAxisMax(List<ChartSeries> series, List<int> radarMax, int j)

- static void DrawPolar(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void DrawChord(App app, int x, int y, int w, int h, ChartOption o, int g)

- static bool InSweep(int ang, int a0, int a1)

- static void DrawForce(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void DrawScatter(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawScatterCore(App app, int x, int y, int w, int h, ChartOption o, int g)
  - 仅散点图主体（面板、坐标轴、点）—— 不含悬停覆盖层，
    可由 RenderCached 缓存（2000 点的散点云每帧只需一次像素
    拷贝），命中后指针反馈再绘制于其上。

- static void ScatterHover(App app, int x, int y, int w, int h, ChartOption o)
  - 散点图悬停覆盖层：在命中半径内查找最近点
    并显示其坐标。在 DrawScatterCore（或
    RenderCached 恢复）之后绘制，使缓存的密集点云仍有实时提示。
    窗口与 DrawScatterCore 锁定同一 [i0,i1]。


## ChartView (class)

- static int PanelHead(App app, int x, int y, int w, int h, string title)

- static void CartesianTooltip(App app, ChartFrame f, List<string> labels, List<ChartSeries> series, int maxV, int step, int half, int baseIdx)

- static void TooltipCard(App app, int px, int py, int maxRight, List<TooltipRow> rows)

- static int SeriesMaxIndex(ChartSeries s, int i0, int i1)

- static int SeriesMinIndex(ChartSeries s, int i0, int i1)

- static void DrawSeriesMarks(App app, ChartFrame f, ChartSeries s, int i0, int i1, int step, int half, ChartOption o)

- static void MarkAt(App app, ChartFrame f, ChartSeries s, int idx, int slot, int step, int half, string lbl)

- static int AngleOf(int dx, int dy)

- static int DistToSeg(int px, int py, int x0, int y0, int x1, int y1)

- static void DrawDashed(Canvas c, List<int> px, List<int> py, int color, int width, int dash, int gap)

- static int ISqrt(int v)

- static int SinDeg(int deg)

- static int CosDeg(int deg)

- static void RingOutline(Canvas c, int cx, int cy, int r, int color)


## ChartView (class)

- static void DrawVenn(App app, int x, int y, int w, int h, ChartOption o, int g)

- static string VennName(ChartSeries s, int i, string fallback)

- static void VennLabel(Canvas c, int ax, int ay, int fh, int fs, string name, int value, int color)

- static bool InCircle(int mx, int my, VennCircle v)


## ChordLayout (class)

弦图布局：节点 = links 的 from/to 首现序；节点权重 = 入 + 出；
圆周 360° 先扣除每节点稳定 gap，再按权重以最大余数法分整数弧；
每个节点内 incident links（按 link 原序）再分子弧，ribbon
连接源/目标子弧中心。输出节点弧 + ribbon 角度。

- List<ChordNodeArc> nodes;

- List<ChordRibbon> ribbons;

- int gap;

- int arcTotal;

- static ChordLayout Of(List<ChartLink> links)

- static int NodeIndex(List<ChordNodeArc> nodes, string name)

- static int Pos(List<int> list, int v)


## ChordNodeArc (class)

弦图的一个节点弧（布局输出；角度为度，0=12 点钟方向，顺时针）。

- string name;

- int weight;

- int a0;

- int a1;

- static ChordNodeArc Of(string name, int weight, int a0, int a1)


## ChordRibbon (class)

弦图的一条 ribbon（布局输出）：源/目标节点子弧的中心角。

- int from;

- int to;

- int val;

- int srcAngle;

- int dstAngle;

- static ChordRibbon Of(int from, int to, int val, int srcAngle, int dstAngle)


## ErrorItem (class)

带对称误差范围（y +/- err）的数值，用于误差条图。
分类标签与测量值一起携带。

- string label;

- int y;

- int err;

- static ErrorItem Of(string label, int y, int err)


## EventBand (class)

Event River 的一条带（布局输出；绝对像素矩形）。

- string name;

- int start;

- int end;

- int value;

- int x0;

- int x1;

- int y;

- int h;

- int row;

- static EventBand Of(string name, int start, int end, int value, int x0, int x1, int y, int h, int row)


## EventRiverLayout (class)

Event River 布局：统一时间范围 [t0, t1] 线性映射到水平轴；
事件按 (start, end, 原序) 稳定排序后做区间着色（贪心放入
第一条空闲行，行数最优），每条带高度 ∝ value，行围绕
中线上下对称堆叠。输出绝对像素 band 矩形，不接触 App/Canvas。

- int t0;

- int t1;

- int span;

- int rowCount;

- List<EventBand> bands;

- static EventRiverLayout Of(List<ChartEvent> events, int x, int y, int w, int h, int pad)

- static bool Less(List<ChartEvent> evs, int i, int j)


## ForceEdge (class)

力导向图的一条边（布局输出；去重后的无向对）。

- int a;

- int b;

- int val;

- static ForceEdge Of(int a, int b, int val)


## ForceLayout (class)

力导向布局：节点 = links 首现序；边 = 无向对（自环丢弃、
重复边求和）。初始位置为圆周（无随机）；固定轮数的整数
定点弹簧模拟（近距离斥力 + 边吸引 + 中心引力 + 边界钳制）。
节点数 > 60 时降迭代。相同输入 → 相同输出。

- List<ForceNode> nodes;

- List<ForceEdge> edges;

- int iterations;

- static ForceLayout Of(List<ChartLink> links, int cx, int cy, int r, int w, int h)

- static int NodeIndex(List<ForceNode> nodes, string name)

- static int EdgeIndex(List<ForceEdge> edges, int a, int b)


## ForceNode (class)

力导向图的一个节点（布局输出；整数像素坐标）。

- string name;

- int x;

- int y;

- int weight;

- static ForceNode Of(string name, int x, int y, int weight)


## GaugeAxisLabel (class)

- bool show;

- string formatter;

- ChartTextStyle textStyle;

- static GaugeAxisLabel Create()


## GaugeAxisLine (class)

- bool show;

- ChartLineStyle lineStyle;

- List<GaugeColorStop> colors;

- static GaugeAxisLine Create()


## GaugeAxisTick (class)

- bool show;

- int splitNumber;

- int length;

- ChartLineStyle lineStyle;

- static GaugeAxisTick Create()


## GaugeColorStop (class)

- int stop;

- int color;

- static GaugeColorStop Of(int stop, int color)


## GaugeDetail (class)

- bool show;

- int backgroundColor;

- int borderWidth;

- int borderColor;

- int width;

- int height;

- ChartPosition offsetCenter;

- string formatter;

- ChartTextStyle textStyle;

- static GaugeDetail Create()


## GaugeOption (class)

- ChartPosition center;

- ChartScalar innerRadius;

- ChartScalar radius;

- int startAngle;

- int endAngle;

- int min;

- int max;

- int precision;

- int splitNumber;

- GaugeAxisLine axisLine;

- GaugeAxisTick axisTick;

- GaugeAxisLabel axisLabel;

- GaugeSplitLine splitLine;

- GaugePointer pointer;

- GaugeTitle title;

- GaugeDetail detail;

- static GaugeOption Create()


## GaugePointer (class)

- ChartScalar length;

- int width;

- int color;

- int shadowColor;

- int shadowBlur;

- static GaugePointer Create()


## GaugeSplitLine (class)

- bool show;

- int length;

- ChartLineStyle lineStyle;

- static GaugeSplitLine Create()


## GaugeTitle (class)

- bool show;

- ChartPosition offsetCenter;

- ChartTextStyle textStyle;

- static GaugeTitle Create()


## MapLayout (class)

地图布局：全局数据边界等比 fit 到绘图区（居中），输出变换
后的环、缩放（千分比像素/单位）与变换原点。纯几何，不碰
Canvas，命中走 bbox + 整数 point-in-polygon。

- List<MapPolygon> polys;

- int regionCount;

- int scale;

- int minX;

- int minY;

- int maxX;

- int maxY;

- int offX;

- int offY;

- static MapLayout Of(List<ChartMapRegion> regions, int x, int y, int w, int h, int pad)

- static int RegionAt(MapLayout l, int px, int py)


## MapPolygon (class)

已变换到屏幕坐标的一条环（外环或洞），带 bbox 与质心。

- int region;

- bool hole;

- List<ChartMapPoint> points;

- int minX;

- int minY;

- int maxX;

- int maxY;

- int cx;

- int cy;

- static MapPolygon Of(int region, bool hole, List<ChartMapPoint> points)


## PieLayout (class)

纯饼图布局：把可见 Pie 系列的 data 项归一化为扇区，供
chart_pie_layout 测试直接断言几何。语义：
- 每条可见 Pie series 的每个 ChartData 是一个扇区；
- 隐藏扇区（data.hidden 或系列 hidden）不进 total，也不占角度；
- pieRings=true 时每条可见系列 = 一独立环，各自计算 total
（各自 360°）；否则所有可见系列的扇区合并进同一张饼；
- 颜色解析：数据项色 > 单数据项系列的系列色 > 按扇区序调色板。
不触碰 App/Canvas —— (主题, 选项) 进，扇区几何出。

- List<PieSlice> slices;

- int total;

- int ringCount;

- List<int> ringTotal;

- List<int> ringInner;

- static PieLayout Of(Theme t, ChartOption o)

- static int SliceColor(Theme t, ChartSeries s, int di, int sliceIndex)
  - 扇区颜色：数据项色 > 单数据项系列的系列色 > 按扇区序调色板。
    多数据项系列用逐扇区调色板（ECharts 饼图按数据项着色），
    单数据项系列沿用系列色（保持旧版单系列单扇区的外观）。

- static string SliceName(ChartSeries s, int di)
  - 扇区名：数据项名优先，其次系列名，最后回退到序号。


## PieSlice (class)

饼图一个扇区（归一化后）：值、颜色、显示名、所属系列/数据项
索引、角度区间 [a0,a1]（度，Canvas 顺时针，0 在 12 点）与环索引。
环索引在 pieRings 模式下是可见 Pie 系列的顺序；否则恒为 0。

- int value;

- int color;

- string name;

- int seriesIndex;

- int dataIndex;

- int a0;

- int a1;

- int ring;

- static PieSlice Of(int value, int color, string name, int seriesIndex, int dataIndex, int a0, int a1, int ring)


## ResolvedChart (class)

ChartOption 的只读、逐帧规范化结果。

- ChartOption source;

- List<ResolvedSeries> series;

- int wid;

- bool animate;

- int windowStart;

- int windowEnd;

- static ResolvedChart Resolve(ChartOption source, Theme theme, App app, int wid)

- ResolvedSeries For(ChartSeries source)

- List<ChartSeries> MaterializeSeries()

- ChartOption DrawOption(int wid, bool animate)
  - 创建渲染器使用的帧内 option。数据集合继续共享，可能被旧
    渲染器修改的 option/series 外壳则复制并与源模型隔离。


## ResolvedSeries (class)

单个源系列的只读绘制快照。保留 source 引用，使渲染器
无需复制数据，同时把继承值和交互状态隔离在当前帧。

- ChartSeries source;

- int color;

- int lineColor;

- int lineWidth;

- int areaColor;

- int labelColor;

- int labelFontSize;

- int emphColor;

- int emphBorderColor;

- int emphBorderWidth;

- bool hidden;

- int wid;

- bool animate;

- static ResolvedSeries Of(ChartSeries source, Theme theme, int index, int wid, bool animate, bool hidden)

- int DataColor(int index)

- int EffectiveColor(bool hovered)

- int EffectiveBorderColor(bool hovered)

- int EffectiveBorderWidth(bool hovered)

- ChartSeries Materialize()


## SankeyNodeLayout (class)

- string name;

- int layer;

- int flowOut;

- int flowIn;

- int x;

- int y;

- int h;

- static SankeyNodeLayout Of(string name)


## SeriesListSource (class)

适配器：把已有的 List<ChartSeries> 暴露为 ChartSource，
高性能路径直接接受内存数据，无需重构。

- List<ChartSeries> series;

- static SeriesListSource Of(List<ChartSeries> series)

- override int SeriesCount()

- override string SeriesName(int s)

- override int SeriesColor(int s)

- override int PointCount(int s)

- override int ValueAt(int s, int i)


## TooltipRow (class)

- int color;

- string text;

- static TooltipRow Of(int color, string text)


## TreeLayoutNode (class)

- ChartNode node;

- int depth;

- int lo;

- int count;

- static TreeLayoutNode Of(ChartNode node, int depth, int lo, int count)


## VennCircle (class)

韦恩图的一个集合圆（布局输出）。

- string name;

- int value;

- int cx;

- int cy;

- int r;

- static VennCircle Of(string name, int value, int cx, int cy, int r)


## VennLayout (class)

两圆韦恩图布局：data[0]=A、data[1]=B、data[2]=A∩B（缺失按 0）。
hidden 数据项按 0 处理（不进布局）。半径按面积比例
（r ∝ sqrt(v)）由 rMax 缩放；圆心距按重叠比例 vI/vMin
从相离 (rA+rB+gap) 线性压向包含，全部确定性整数运算。
输出两圆圆心/半径、圆心距、重叠千分比、包含标志和
三个标签锚点（A-only / B-only / 交集中心）。

- VennCircle a;

- VennCircle b;

- int d;

- int overlap;

- bool contained;

- int gap;

- int ax;

- int ay;

- int bx;

- int by;

- int ix;

- int iy;

- static VennLayout Of(List<ChartData> data, int rMax, int gap, int cx0, int cy0)

- static int Val(ChartData d)

- static int Rad(int v, int vMax, int rMax)


## WordCloudItem (class)

- ChartData data;

- int x;

- int y;

- int w;

- int h;

- int fontSize;

- static WordCloudItem Of(ChartData data, int x, int y, int w, int h, int fontSize)


## ChartAxisType (enum)

ECharts xAxis/yAxis.type。

- Category

- Value

- Time

- Log


## ChartType (enum)

ECharts 2.2.7 原生 series.type。样式、堆叠、半径等行为由 series 子实体表达，不能伪装成类型。

- Line

- Bar

- Pie

- Scatter

- K

- Radar

- Chord

- Force

- Map

- Gauge

- Funnel

- EventRiver

- Treemap

- Tree

- WordCloud

- Heatmap

- Custom
