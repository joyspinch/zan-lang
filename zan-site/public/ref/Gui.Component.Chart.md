# Gui.Component.Chart

> 源码: `packages/Zan.Gui.Charts/src/Gui/Component/Chart/Chart.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartBarLayout.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartBig.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartBootstrap.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartController.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartEvents.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartFonts.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartGeoJson.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartHost.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartLayoutRelation.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartLayoutSpecial.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartMaps.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartModel.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartPolarBarLayout.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartResolved.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartSkin.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartSvgMap.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartTheme.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartTimeline.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartToolbox.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartView.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewBar.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewCalendar.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewDataRange.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewEventRiver.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewFinance.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewHeatmap.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewHier.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewLine.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewMap.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewMatrix.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewMore.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewPictorial.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewPie.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewPolar.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewPolarBar.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewRelation.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewScatter.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewShared.zan`, `packages/Zan.Gui.Charts/src/Gui/Component/Chart/ChartViewVenn.zan`


## BoxItem (class)

- string label;

- int lo;

- int q1;

- int med;

- int q3;

- int hi;

- static BoxItem Of(string label, int lo, int q1, int med, int q3, int hi)

- static BoxItem Clone(BoxItem src)


## Bubble (class)

- int x;

- int y;

- int weight;

- static Bubble Of(int x, int y, int weight)

- static Bubble Clone(Bubble src)


## Candle (class)

- int open;

- int high;

- int low;

- int close;

- ChartItemStyle itemStyle;

- static Candle Of(int open, int high, int low, int close)

- static Candle Clone(Candle src)


## Chart (class)

- static int Rgb(int r, int g, int b)

- static int WithAlpha(int packed, int a)

- static int Transparent()

- static int ContrastFg(int bg)

- static int fadeFocusIdx=-1;

- static int FadeFocusAlpha()

- static int FadeFocusColor(int si, int color)

- static int toolReserve;

- static ChartOption legendOpt;

- static int legendBottomReserve;

- static int zoomBandReserve;

- static bool gridPanelSub;

- static bool zoomBarHidden;

- static int RampAt(List<int> colors, int t)

- static int Palette(Theme t, int i)

- static int SeriesPaintColor(ChartSeries s, Theme t, int index)

- static void DrawSymbol(Canvas c, int cx, int cy, int r, string shape, int color, int borderColor, int borderWidth)

- static int SymbolRadius(int symbolSize, int defaultR)

- static bool CanShowAllSymbolForCategory(int plotW, int n, int symbolSizeF, List<ChartData> data, int i0, int dpiScale)

- static void PolyVerts1000(string shape, List<int> vx, List<int> vy)

- static void FillPoly1000(Canvas c, int cx, int cy, int r, List<int> vx, List<int> vy, int color)

- static bool LabelShown(int ci, int interval)

- static List<int> CategoryTicks(ChartFrame f, List<string> labels, int fs, int plotX, int labelGap, ChartAxis x0)

- static int RotatedLabelH(int maxW, int fh, int deg)

- class ChartLayoutRect

- static void GridMergeLayoutParam(List<ChartBoxParam> target, List<ChartBoxParam> newOption)

- static void GridMergeOne(int[]names, List<ChartBoxParam> target, List<ChartBoxParam> newOption)

- static bool BoxParamHasValue(ChartBoxParam p)

- static double GridParsePercent(ChartBoxParam p, double base1, out bool wrote)

- static ChartLayoutRect GridGetLayoutRect(List<ChartBoxParam> p, double cw, double ch)

- static List<ChartBoxParam> GridParamsNew()

- static ChartLayoutRect GridRawRect(ChartBoxParam l, ChartBoxParam r, ChartBoxParam t, ChartBoxParam b, ChartBoxParam w, ChartBoxParam h, double cw, double ch, bool nullDefaults)

- static void GridShrinkRect(ChartLayoutRect rect, double mTop, double mRight, double mBottom, double mLeft, double minW, double minH)

- static int NiceMax(int v)

- static int QuantityExponent(double val)

- static double Pow10D(int e)

- static double RoundP(double x, int precision)

- static int GetPrecisionSafe(double val)

- static int GetPrecision(double val)

- static int GetIntervalPrecision(double niceInterval)

- static double NiceD(double val)

- static double NiceMode(double val, bool roundMode)

- static void IntervalScaleNiceTicks(double e0, double e1, int splitNumber, double minInterval, double maxInterval, out double interval, out int precision, out double nt0, out double nt1)

- static void ScaleTicks(double extent0, double extent1, double nt0, double nt1, double interval, int precision, List<double> outTicks)

- static double IncreaseInterval(double iv)

- static ChartSpan ClampNiceRange(int dataLo, int dataHi, bool scale, double minInterval, double maxInterval)

- static ChartSpan NiceSpan6(int dataLo, int dataHi, double minInterval, double maxInterval)

- static ChartSpan AlignRange(double dataLo, double dataHi, int alignToSegs, bool incl0)

- static void MinorTickValuesPairs(List<int> ticks, int splitNumber, List<double> into)

- static void MinorTickValues(double e0, double e1, int segs, int splitNumber, List<double> into)

- static void AxisNiceRange(double fixedLo, double fixedHi, double dataMin, double dataMax, int splitNumber, double minInterval, double maxInterval, out double lo, out double hi, out double interval, out int precision)

- static int AxisMinForF(List<ChartSeries> series, int axisIndex, int i0, int i1, bool scale)

- static bool HasStackedBarsF(List<ChartSeries> series, int axisIndex)

- static ChartSpan StackExtentF(List<ChartSeries> series, int axisIndex, int i0, int i1)

- static int FracAxisLoF(int fixedLo, List<ChartSeries> series, int axisIndex, int i0, int i1, bool scale)

- static int FracAxisHiF(int fixedHi, List<ChartSeries> series, int axisIndex, int i0, int i1, bool scale)

- class ChartSpan

- class ChartExp

- static int FloorDivQ(int a, int b)

- static int CeilDivQ(int a, int b)

- static int DecDigits(int v)

- static int StripZeros(int v)

- static int JSRoundHalf(int q)

- static ChartExp ExpNum2Rat(int num, int den)

- static ChartExp ExpNumFull(int n, bool byFloor)

- static void SmartCeilStep(ChartExp n)

- static void ExpAlign(ChartExp x, ChartExp refx, bool byFloor)

- static ChartSpan SmartForInteger(int min, int max, int section)

- static int SmartTryForInt(int min, int max, ChartExp expMin, ChartExp expMax, int secs)

- static ChartSpan SmartCoreCalc(int min, int max)

- static int ExpRestore(ChartExp x)

- static ChartSpan NiceRange(int lo, int hi)

- static ChartSpan NiceRange(int lo, int hi, bool scale)

- static ChartSpan ScaleRange(int lo, int hi)

- static int MaxOf(List<ChartSeries> series)

- static int MinOf(List<ChartSeries> series)

- static int ValueX(int plotX, int plotW, int lo, int hi, int v)

- static int ValueXF(int plotX, int plotW, int lo, int hi, int v, int g)

- static double BrkElapsedItems(List<ChartBreakResolvedItem> items, double v)

- static double BrkUnelapsedItems(List<ChartBreakResolvedItem> items, double e)

- static double BrkElapsed(List<ChartBreak> breaks, List<double> gapReals, double v)

- static double BrkUnelapsed(List<ChartBreak> breaks, List<double> gapReals, double e)

- static List<double> BrkGapReals(List<ChartBreak> breaks, double extentLo, double extentHi)

- static bool BrkActive(ChartBreak b, double extentLo, double extentHi)

- static List<double> BrkNiceExtent(List<ChartBreak> brks, double rawLo, double rawHi, int splitNumber)

- static List<double> BrkTicks(List<ChartBreak> breaks, List<double> grs, double lo, double hi, double interval)

- static List<int> BrkPixelBands(ChartFrame f, int kind)

- static void DrawAxisLineBands(Canvas c, int x, int y, int len, bool vertical, int thick, int col, List<int> bands)

- static void BrkZigzag(int seed, int amp, int plotPos, int plotLen, int bandLo, int bandHi, bool horizontal, List<int> xsA, List<int> ysA, List<int> xsB, List<int> ysB)

- static void BrkDashPolyline(Canvas c, List<int> xs, List<int> ys, int col)

- static void DrawBreakAreas(Canvas c, ChartFrame f, ChartAxis ax, int kind)

- static int ValueYF(int plotY, int plotH, int lo, int hi, int v, int g)

- static int ValueY(int plotY, int plotH, int lo, int hi, int v)

- static int ValueYF1000(int plotY, int plotH, int loF, int hiF, int vF)

- static bool AxisLineDraws(int axisLineShow, bool otherIsValueOrLog)

- static int ZeroKindOf(int lo, int hi)

- static bool CanOnZeroTo(ChartAxis target, int targetLo, int targetHi, int riderOnZero)

- static bool PieLabelShows(int showT)

- static bool PieLabelInside(string pos)

- static bool PieLeaderDraws(int lineShowT)

- static int PieEdgeX3(bool rightSide, int viewL, int viewW, int edgeDist, int textW, int dist)

- static int PiePct(int v, long tot)

- static string RichFmtRaw(ChartSeries s, string fmt, string name, int val, int pct)

- static ChartRichStyle RichStyleOf(ChartTextStyle st, string nm)

- static List<RichSeg> RichTokenize(string f, ChartTextStyle st)

- static int RichSegH(int declaredH, int fontH, int padT, int padB)

- static string RichStrip(string f)

- static void DrawAxisLabel(App app, Canvas c, ChartAxis a, string lab, int bx, int by, int defCol, int fs)

- static void DrawAxisLabelCentered(App app, Canvas c, ChartAxis a, string lab, int rx, int ry, int rw, int rh, int defCol, int fs)

- static int MaxOfWindow(List<ChartSeries> series, int i0, int i1)

- static int MaxStack(List<ChartSeries> series, int n)

- static int MaxStackW(List<ChartSeries> series, int n, int i0)

- static int MaxStackAll(List<ChartSeries> series, int n)

- static int MaxStackAllW(List<ChartSeries> series, int n, int i0)

- static int MaxStackGroupsW(List<ChartSeries> series, int n, int i0)

- static int MaxBarStackGroupsW(List<ChartSeries> series, int n, int i0)

- static int MinBarStackGroupsW(List<ChartSeries> series, int n, int i0)

- static int MaxInt(List<int> vs)

- static bool PointInPoly(List<ChartMapPoint> poly, int px, int py)

- static int Log10(int v)

- static int Pow10(int e)

- static List<int> LogTicks(int lo, int hi)

- static string LogFracText(long vF)

- static int LogFloorI(int v)

- static int LogCeilI(int v)

- static long LogFloorF(long vF)

- static long LogCeilF(long vF)

- static string TimeLabel(int t)

- static List<int> TimeTicks(int t0, int t1, int maxTicks)

- static string FormatFixed(double v, int precision)

- static int DrawPanel(App app, int x, int y, int w, int h, string title, List<ChartSeries> series, bool showLegend)

- static int LegendKey(int wid, int si)

- static int DRangeLoKey(int wid)

- static int DRangeHiKey(int wid)

- static int DRangeBandKey(int wid, int bi)

- static int DRangeHoverKey(int wid)

- static string LegendLabel(string fmt, string name)

- static bool LegendVertical()

- static string LegendFmt()

- static int PanelBgOr(int themeBg)

- static string TitleSubText()

- static string TitleAlignMode()

- static int TitleColor()

- static int TitleSubColor()

- static int TitleNeutral(int r, int g, int b)

- static string LegendAlignX()

- static string LegendBoxAlignX()

- static int LegendAnchorLeftPx()

- static int LegendAnchorRightPx()

- static int LegendAnchorTopPx()

- static int LegendAnchorBottomPx()

- static string LegendBoxAlignY()

- static int LegendFloatTop(App app, int y, int h, int lgH, int pad)

- static int LegendFloatLeft(App app, int x, int w, int contentW, int pad, int x0, int maxX)

- static int LegendSelMode()

- static int DataRangeValue(int dLo, int dHi, int q)

- static int LegendSliceKey(int wid, int si, int di)

- static int PieSelKey(int wid, int si, int di)

- static int LegendChipW(App app, string label)

- static int HeadBandBottomValues(int y, int pad, int titleH, int toolboxH, int gap)

- static int HeadBandBottom(App app, int y, string title)

- static bool DrawLegendChip(App app, int lx, int ly, int legendW, int lh, int color, string label, bool hidden, int stateKey)

- static int ZoomLoKey(int wid)

- static int ZoomHiKey(int wid)

- static int TreePanXKey(int wid)

- static int TreePanYKey(int wid)

- static int TreeDragXKey(int wid)

- static int TreeDragYKey(int wid)

- static int TreeDragPanXKey(int wid)

- static int TreeDragPanYKey(int wid)

- static int TreeDraggingKey(int wid)

- static int TreeDragMovedKey(int wid)

- static int TreeZoomKey(int wid)

- static int TreeHitKey(int wid)

- static int CacheKey(int slot)

- static int TbViewKey(int wid)

- static int TbMagicKey(int wid)

- static int TbZoomKey(int wid)

- static int MapZoomKey(int wid)

- static int MapPanXKey(int wid)

- static int MapPanYKey(int wid)

- static int MapSelKey(int wid)

- static int MapRegionSelKey(int wid, int region)

- static int MapDragKey(int wid)

- static int MapDragXKey(int wid)

- static int MapDragYKey(int wid)

- static int MapDragPanXKey(int wid)

- static int MapDragPanYKey(int wid)

- static int TbHitKey(int wid, int k)

- static int ConnectGroupKey(int wid)

- static int ConnectHoverKey(int wid)

- static int ConnectHoverGenKey(int wid)

- static int ZoomI0(App app, ChartOption o, int wid, int n)

- static int ZoomI1(App app, ChartOption o, int wid, int n)

- static List<string> WindowLabels(List<string> labels, int i0, int i1)

- static int CategoryIndexOf(List<string> cats, string name)

- static void DrawZoomBar(App app, ChartOption o, int plotX, int plotW, int x, int y, int w, int h, int wid, int n)

- static ChartZoom ZoomBarComp(ChartOption o)

- static int ZoomBarH(App app, ChartZoom zc, int containerH)

- static int ZoomBandH(App app, ChartOption o, int containerH)

- static int ZoomShadowN(ChartSeries s)

- static int ZoomShadowV(ChartSeries s, int i)

- static bool ZoomShadowWanted(ChartOption o, ChartZoom zc)

- static ChartSeries ZoomShadowSeries(ChartOption o)

- class ChartAxisName

- static int NameGapPx(App app, ChartAxis a)

- static ChartAxisName AxisNamePosPx(ChartAxis a, bool isX, int plotX, int plotY, int plotW, int plotH, bool far, int gap, int fh, int centerTop)

- static ChartAxisName AxisNamePosPx(ChartAxis a, bool isX, int plotX, int plotY, int plotW, int plotH, bool far, int gap, int fh, int centerTop, int axisLine)

- static ChartAxisName AxisNamePos(App app, ChartAxis a, bool isX, int plotX, int plotY, int plotW, int plotH, bool far, int fs)

- static ChartAxisName AxisNamePos(App app, ChartAxis a, bool isX, int plotX, int plotY, int plotW, int plotH, bool far, int fs, int axisLine)

- class ChartNameBox

- static ChartNameBox AxisNameBoxPx(ChartAxis a, bool isX, int plotX, int plotY, int plotW, int plotH, bool far, int gap, int fh, int centerTop, int tw, int axisLine)

- static int AxisNameX(App app, ChartAxis a, ChartAxisName n, int fs)

- static int AxisNameY(App app, ChartAxis a, ChartAxisName n, int fs)

- static void DrawAxisName(App app, Canvas c, ChartAxis a, bool isX, int plotX, int plotY, int plotW, int plotH, bool far, StyleBox sTitle, int fs)

- static void DrawAxisNameAt(App app, Canvas c, ChartAxis a, bool isX, int plotX, int plotY, int plotW, int plotH, bool far, StyleBox sTitle, int fs, int axisLine)

- static int LegendStartY(App app, int y, int proposed)

- static int LegendStartYRows(App app, int y, int proposed, int h, int rowsH, int pad)

- static List<int> LegendPrimIdx(List<ChartSeries> series)

- static int LegendRowCount(App app, int x, int w, string title, List<ChartSeries> series)

- static int PanelContentTop(App app, int x, int y, int w, string title, List<ChartSeries> series, bool showLegend)

- static int DrawPanelI(App app, int x, int y, int w, int h, int legendTextCol, string title, List<ChartSeries> series, int wid, bool showLegend)

- static bool LegendClick(App app, int wid, List<ChartSeries> series, int si, bool hid, int mode)

- static int PanelTopI(App app, int x, int y, int w, string title, List<ChartSeries> series, bool showLegend)

- static void DrawMarkArea(App app, ChartFrame f, ChartOption o)

- static int DRangeEff(App app, ChartOption o, int wid, int dMin, int dMax, bool upper)

- static bool DRangeBandOk(App app, ChartOption o, int wid, int v)

- static bool DRangeOk(App app, ChartOption o, int wid, int dMin, int dMax, int v)

- static void DrawMarkLine(App app, ChartFrame f, ChartOption o)

- static void FrameLines(App app, Canvas c, ChartOption o, int yLo, int yHi, int plotX, int plotY, int plotW, int plotH, int ink)

- static ChartFrame DrawFrame(App app, int x, int y, int w, int h, int plotTop, List<string> labels, int maxV)

- static ChartFrame DrawFrameT(App app, int x, int y, int w, int h, int plotTop, List<string> labels, int maxV, int ticks)

- static ChartFrame DrawFrameTO(App app, int x, int y, int w, int h, int plotTop, List<string> labels, int maxV, int ticks, ChartOption o)

- static ChartFrame DrawFrameLoHi(App app, int x, int y, int w, int h, int plotTop, List<string> labels, int lo, int hi)

- static ChartFrame DrawFrameLoHiT(App app, int x, int y, int w, int h, int plotTop, List<string> labels, int lo, int hi, int ticks)

- static ChartFrame DrawFrameLoHiO(App app, int x, int y, int w, int h, int plotTop, List<string> labels, int lo, int hi, int ticks, ChartOption o)

- static int PointV(int v, int g)

- static string PointText(int v, int g)

- static int ZeroAnchorLo(bool scale, bool fixLo, int lo, int hi)

- static int ZeroAnchorHi(bool scale, bool fixHi, int lo, int hi)

- static ChartSpan ScatterAutoRange(int lo, int hi)

- static int NiceStepUp(int v)

- static int AxisMaxFor(List<ChartSeries> series, int axisIndex, int i0, int i1, bool scale)

- static int AxisMaxForF(List<ChartSeries> series, int axisIndex, int i0, int i1, bool scale)

- static bool SeriesFracAny(List<ChartSeries> series, int axisIndex)

- static bool ScatterAxisFrac(List<ChartSeries> series, bool xAxis)

- static int AxisMinFor(List<ChartSeries> series, int axisIndex, int i0, int i1, bool scale)

- static int XMinFor(List<ChartSeries> series, bool fromValues, bool scale)

- static int XMaxFor(List<ChartSeries> series, bool fromValues, bool scale)

- static int FormatterExtraW(string fmt, int v, int fs)

- static bool AnyOnAxis(List<ChartSeries> series, int axisIndex)

- static int NiceMin(int v)

- static double NiceMin(double v)

- static int BarDataMinRaw(List<ChartSeries> series)

- static int BarDataMaxRaw(List<ChartSeries> series)

- static int AxisLo(int fixedLo, int dataMin)

- static int AxisLo(int fixedLo, int dataMin, bool scale)

- static int AxisHi(int fixedHi, int dataMax, int lo)

- static string Commas(int v)

- static string FracText(int v)

- static List<int> LlShift1D(List<int> pos, List<int> size, int gap)

- static string SeriesNumText(ChartSeries s, int i)

- static int TickLabelW(int lo, int hi, int ticks, int fs)

- static int TickLabelWF(int loF, int hiF, int ticks, int fs)

- static ChartFrame BuildAxes(App app, int x, int y, int w, int h, int plotTop, List<string> labels, ChartOption o, List<ChartSeries> series)

- static ChartFrame BuildAxesW(App app, int x, int y, int w, int h, int plotTop, List<string> labels, ChartOption o, List<ChartSeries> series, int i0, int i1)

- static ChartFrame BuildAxesR(App app, int x, int y, int w, int h, int plotTop, List<string> labels, ChartOption o, List<ChartSeries> series, int leftDataMax, int rightDataMax, int winI0, int winI1)


## ChartAreaStyle (class)

- int color;

- string type;

- int alpha;

- List<int> gradStops;

- static ChartAreaStyle Create()


## ChartAxis (class)

- ChartAxisType type;

- string title;

- int min;

- int max;

- double minD;

- double maxD;

- bool minDataB;

- bool maxDataB;

- bool scale;

- bool reversed;

- int ticks;

- bool showGrid;

- bool splitLineDeclared;

- int splitColor;

- int splitWidth;

- int axisPointerShow;

- double apValue;

- bool apValueSet;

- int apColor;

- int apWidth;

- bool apLabelShow;

- int apLabelBg;

- bool splitArea;

- int labelRotate;

- int labelInterval;

- int labelMargin;

- bool labelShow;

- int labelFontSize;

- string nameLocation;

- int nameGap;

- string labelFormatter;

- ChartTextStyle labelSt;

- bool labelNegate;

- bool boundaryGap;

- int bgLoF;

- int bgHiF;

- int axisLineColor;

- int axisLineShow;

- int onZero;

- bool show;

- string position;

- string axisId;

- double minInterval;

- double maxInterval;

- bool alignTicks;

- bool minorTickShow;

- int minorTickSplitNumber;

- bool minorSplitLineShow;

- List<ChartBreak> breaks;

- bool breakShow;

- int breakAmp;

- int breakBorderColor;

- int breakFillColor;

- int breakOpacityX10;

- List<string> categories;

- static ChartAxis Category(List<string> cats)

- static ChartAxis CategoryTitled(List<string> cats, string title)

- static ChartAxis Value()

- static ChartAxis ValueRange(int min, int max)

- static ChartAxis ValueTitled(string title)

- static ChartAxis Time()

- static ChartAxis Log()

- static ChartAxis LogTitled(string title)

- void SetRange(double lo, double hi)

- static ChartAxis Clone(ChartAxis src)


## ChartBarCol (class)

- double offset;

- double width;

- static ChartBarCol Of(double o, double w)


## ChartBig (class)

- static int AnimationKey(int key)

- static bool NeedsRebuild(ChartState st, ChartSource src, int w)

- static void Build(ChartState st, ChartSource src, int target)

- static int DrawHead(App app, int x, int y, int w, int h, string title, ChartSource src)

- static void Render(App app, ChartState st, ChartSource src, ChartOption o, int x, int y, int w, int h, string title)


## ChartBigColExtents (class)

- public int min;

- public int max;

- public ChartBigColExtents(int min, int max)

- public static ChartBigColExtents Of(int min, int max)


## ChartBootstrap (class)

- static bool installed;

- static void Install()

- static Control MakeChartHost(string kind)


## ChartBoxParam (class)

- int kind;

- double num;

- string kw;

- static ChartBoxParam Px(double v)

- static ChartBoxParam Pct(double v)

- static ChartBoxParam Key(string s)

- static ChartBoxParam Void()

- static ChartBoxParam Clone(ChartBoxParam src)


## ChartBreak (class)

- double startD;

- double endD;

- bool gapPrct;

- double gapVal;

- static ChartBreak Of()

- static ChartBreak Clone(ChartBreak src)


## ChartBreakResolved (class)

- public ChartBreak item;

- public double gapReal;

- public ChartBreakResolved(ChartBreak item, double gapReal)

- public static ChartBreakResolved Of(ChartBreak item, double gapReal)


## ChartBreakResolvedItem (class)

- public ChartBreak brk;

- public double gapReal;

- public ChartBreakResolvedItem(ChartBreak brk, double gapReal)

- public static ChartBreakResolvedItem Of(ChartBreak brk, double gapReal)


## ChartCalendarSpec (class)

- int rangeT0;

- int rangeT1;

- bool vertical;

- int cellW;

- int cellH;

- int leftPx;

- int rightPx;

- int topPx;

- int bottomPx;

- bool hasLeft;

- bool hasRight;

- bool hasTop;

- bool hasBottom;

- bool leftCenter;

- bool rightCenter;

- bool topCenter;

- bool bottomCenter;

- int widthPx;

- int heightPx;

- bool showDayLabel;

- int dayMargin;

- string dayPos;

- int dayFirstDay;

- string dayNameMap;

- int dayColor;

- int dayFontSize;

- bool showMonthLabel;

- int monthMargin;

- string monthPos;

- string monthAlign;

- string monthNameMap;

- int monthColor;

- int monthFontSize;

- bool showYearLabel;

- int yearMargin;

- string yearPos;

- string yearFormatter;

- int yearColor;

- int yearFontSize;

- bool showSplitLine;

- int splitColor;

- int splitWidth;

- int itemColor;

- int itemBorderWidth;

- int itemBorderColor;

- static ChartCalendarSpec Of()


## ChartController (class)

- ChartOption option;

- ChartOption initialOption;

- int version;

- string status;

- string statusMessage;

- bool disposed;

- ChartEventHub eventHub;

- static ChartController Of(ChartOption option)

- ChartOption Option()

- ChartOption GetOption()

- int Version()

- string Status()

- string StatusMessage()

- bool IsDisposed()

- ChartController On(ChartEventType type, ChartInteractionHandler handler)

- ChartController Off(ChartEventType type, ChartInteractionHandler handler)

- ChartController ClearEvents()

- bool HasEvent(ChartEventType type)

- void RaiseEvent(ChartInteractionEvent eventArgs)

- void RaiseType(ChartEventType type)

- ChartController OnClick(ChartInteractionHandler handler)

- ChartController OnDoubleClick(ChartInteractionHandler handler)

- ChartController OnHover(ChartInteractionHandler handler)

- ChartController OnMouseOut(ChartInteractionHandler handler)

- ChartController OnLegendSelected(ChartInteractionHandler handler)

- ChartController OnPieSelected(ChartInteractionHandler handler)

- ChartController OnMapSelected(ChartInteractionHandler handler)

- ChartController OnMapRoam(ChartInteractionHandler handler)

- ChartController OnTimelineChanged(ChartInteractionHandler handler)

- ChartController OnMagicTypeChanged(ChartInteractionHandler handler)

- ChartController OnRefresh(ChartInteractionHandler handler)

- ChartController OnForceLayoutEnd(ChartInteractionHandler handler)

- ChartController OnDataZoom(ChartInteractionHandler handler)

- ChartController OnDataRange(ChartInteractionHandler handler)

- ChartController OnRestore(ChartInteractionHandler handler)

- ChartController OnDataChanged(ChartInteractionHandler handler)

- ChartController SetOption(ChartOption next)

- ChartController SetData(int seriesIndex, List<ChartData> data)

- ChartController AppendData(int seriesIndex, List<ChartData> data)

- ChartController AppendValues(int seriesIndex, List<int> values)

- ChartController ClearData(int seriesIndex)

- ChartController Clear()

- ChartController Restore()

- ChartController ShowLoading(string message)

- ChartController HideLoading()

- ChartController SetEmptyMessage(string message)

- ChartController SetErrorMessage(string message)

- void Dispose()


## ChartData (class)

- int val;

- double number;

- bool numberSet;

- string name;

- int color;

- bool hidden;

- bool selected;

- int size;

- bool sizeSet;

- bool gap;

- string symbol;

- string labelText;

- int labelSide;

- bool labelHide;

- string labelFmtT;

- ChartTextStyle dlabel;

- bool hasCat;

- int cat;

- ChartItemStyle itemStyle;

- int calDay;

- int heatX;

- int heatY;

- static ChartData Of(int v)

- static ChartData Gap()

- static ChartData Numeric(double v)

- static ChartData NamedNumber(double v, string name)

- static ChartData Named(int v, string name)

- static ChartData Colored(int v, int color)

- static ChartData Full(int v, string name, int color)

- static ChartData Clone(ChartData src)


## ChartDataset (class)

- List<ChartDimension> dimensions;

- List<string> cats;

- static ChartDataset Of(List<ChartDimension> dimensions)

- static ChartDataset WithCats(List<ChartDimension> dimensions, List<string> cats)

- int RowCount()

- int DimOf(string name)

- List<string> Labels()

- ChartOption EncodeBy(string title, List<int> yDims, ChartType type)

- ChartOption EncodeNamed(string title, List<string> yNames, ChartType type)


## ChartDimension (class)

- string name;

- List<int> values;

- static ChartDimension Of(string name, List<int> values)


## ChartEvent (class)

- string name;

- int start;

- int end;

- int value;

- int color;

- static ChartEvent Of(string name, int start, int end, int value)

- static ChartEvent Clone(ChartEvent src)


## ChartEventHub (class)

- List<ChartEventSubscription> subscriptions;

- ChartEventHub()

- void On(ChartEventType type, ChartInteractionHandler handler)

- void Off(ChartEventType type, ChartInteractionHandler handler)

- int Count(ChartEventType type)

- void Clear()

- void Raise(ChartInteractionEvent eventArgs)


## ChartEventSubscription (class)

- ChartEventType type;

- ChartInteractionHandler handler;

- ChartEventSubscription(ChartEventType type, ChartInteractionHandler handler)


## ChartFonts (class)

- static int AxisLabel(App app)

- static int Legend(App app)

- static int SeriesLabel(App app)

- static int Tooltip(App app)

- static int AxisName(App app)


## ChartFrame (class)

- int plotX;

- int plotY;

- int plotW;

- int plotH;

- int leftMin;

- int leftMax;

- int leftMinF;

- int leftMaxF;

- int rightMin;

- int rightMax;

- int rightMinF;

- int rightMaxF;

- bool hasRight;

- bool reversed;

- bool rightReversed;

- bool leftLog;

- bool rightLog;

- bool isTime;

- int timeMin;

- int timeMax;

- bool catBoundaryGap;

- bool catY;

- int xLo;

- int xHi;

- bool leftNegate;

- bool rightNegate;

- List<ChartBreakResolved> resolvedBrkL;

- List<ChartBreak> brkL;

- List<double> brkGapL;

- double brkSpanL;

- double brkIvL;

- List<ChartBreakResolved> resolvedBrkR;

- List<ChartBreak> brkR;

- List<double> brkGapR;

- double brkSpanR;

- double brkIvR;

- List<ChartBreakResolved> resolvedBrkX;

- List<ChartBreak> brkX;

- List<double> brkGapX;

- double brkSpanX;

- int CatLeft(int i, int n)

- int CatWidth(int i, int n)

- int CatMid(int i, int n)

- int CatX(int i, int n)

- int CatIndexAt(int px, int n)

- int YOf(int v, int axisIndex)

- bool BrkHas(int axisIndex)

- int YOfBrk(double v, int axisIndex)

- int XOfBrk(double v)

- int YOfFx(int v, int axisIndex)

- int YOfF(int vF, int axisIndex)

- int YOfFL(long vF, int axisIndex)

- int ZeroBase(int axisIndex)

- int ZeroLineY(int axisIndex)

- int ZeroLineX()

- int CatMidY(int i, int n)

- int XOfValue(int v)

- int YOfLog(int v, int axisIndex)

- int YOfLogF(long vF, int axisIndex)

- static int Log10(int v)

- static long Log10F(int v)

- static long Log10Fx(long vF)

- int ValueAtY(int py, int axisIndex)

- int XOfTime(int t)

- string ValueTextAtY(int py, int axisIndex)


## ChartGeo (class)

- string map;

- bool roam;

- bool labelShow;

- ChartTextStyle labelSt;

- string selectedMode;

- int areaColor;

- int borderColor;

- int borderWidth;

- List<ChartMapRegion> regions;

- bool matAnchor;

- int mx0;

- int mx1;

- int my0;

- int my1;

- static ChartGeo Of()

- static ChartGeo Clone(ChartGeo src)


## ChartGeoJson (class)

- static List<ChartMapRegion> ParseText(string json)

- static List<ChartMapRegion> Parse(JsonValue v)

- static void Feature(JsonValue f, bool encoded, int scale, List<ChartMapRegion> into)

- static void Polygon(JsonValue rings, JsonValue offs, bool encoded, int scale, List<ChartMapRing> into)

- static List<ChartMapPoint> DecodeRing(string s, int ox, int oy, int scale)

- static int Zig(int c)

- static void PushPoint(List<ChartMapPoint> pts, int x, int y)

- static int QuantDeg(double d)

- static int QuantScaled(int v, int scale)

- static List<int> CodePoints(string s)

- static JsonValue At(JsonValue v, int i)


## ChartGridSpec (class)

- int lPct;

- int lPx;

- int tPct;

- int tPx;

- int rPct;

- int rPx;

- int bPct;

- int bPx;

- int wPct;

- int wPx;

- int hPct;

- int hPx;

- bool containLabel;

- ChartBoxParam lRaw;

- ChartBoxParam tRaw;

- ChartBoxParam rRaw;

- ChartBoxParam bRaw;

- ChartBoxParam wRaw;

- ChartBoxParam hRaw;

- string id;

- bool matAnchor;

- int mx0;

- int mx1;

- int my0;

- int my1;

- static ChartGridSpec Of()


## ChartHit (class)

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


## ChartHost (class)

- ChartView view;

- ChartOption opt;

- string chartType;

- ChartHost()

- override string Kind()

- override string GetProp(string key)

- override void SetProp(string key, string val)

- void SetChartType(string s)

- void SetOption(ChartOption o)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static ChartOption SampleOf(string type)


## ChartInteractionEvent (class)

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

- static ChartInteractionEvent Series(ChartEventType type, int wid, int seriesIndex, string seriesName)

- static ChartInteractionEvent DataPoint(ChartEventType type, int wid, int seriesIndex, string seriesName, int dataIndex, string dataName, ChartElementType elementType)

- static string ElementName(ChartElementType type)

- string TypeName()

- static string TypeNameOf(ChartEventType type)


## ChartItemStateStyle (class)

- int color;

- int color0;

- int borderColor;

- int borderWidth;

- List<int> borderRadius;

- ChartLineStyle lineStyle;

- ChartAreaStyle areaStyle;

- static ChartItemStateStyle Create()


## ChartItemStyle (class)

- ChartItemStateStyle normal;

- ChartItemStateStyle emphasis;

- static ChartItemStyle Create()

- static ChartItemStateStyle CloneState(ChartItemStateStyle src)

- static ChartItemStyle Clone(ChartItemStyle src)


## ChartJsonDataset (class)

- string id;

- JsonValue source;

- List<int> rows;

- List<string> dims;

- bool noHdr;

- JsonValue source1;

- List<int> rows1;

- int HdrRows()

- static bool HeaderEvidence(JsonValue source)

- static ChartJsonDataset ById(List<ChartJsonDataset> list, string want)

- static ChartJsonDataset At(List<ChartJsonDataset> list, int index)

- int RowCount()

- int Width()

- string Header(int col)

- int ColumnOf(JsonValue dim)

- JsonValue CellAbs(int absRow, int col)

- JsonValue Cell(int rowIdx, int col)

- double Num(int r, int c)

- string Text(int r, int c)

- static void Transform(ChartJsonDataset from, ChartJsonDataset into, JsonValue tr)

- static bool RowLess(ChartJsonDataset ds, int a, int b, List<JsonValue> cfgs)

- static JsonValue SynthRows(string[]header, List <List<double>> rows)

- static void Boxplot(ChartJsonDataset from, ChartJsonDataset into, JsonValue cfg)

- static void Aggregate(ChartJsonDataset from, ChartJsonDataset into, JsonValue cfg)

- static double Quantile(List<double> asc, double p)

- static void TakeSynth(ChartJsonDataset into, JsonValue arr)

- static bool ColNumeric(ChartJsonDataset ds, int col)

- static int Mul1000(double v)

- static int Rint(double v)

- static int CatIdx1000(ChartAxis a, string text)

- static void PieceColorPoints(ChartOption o, ChartSeries cs)

- static void Regression(ChartJsonDataset from, ChartJsonDataset into, JsonValue cfg)

- static void Clustering(ChartJsonDataset from, ChartJsonDataset into, JsonValue cfg)

- static List<double> Solve(List <List<double>> A, List<double> b, int k)

- static double Pow(double b, int e)

- const double Ln2=0.6931471805599453;

- static double Ln(double v)

- static double Exp(double v)

- static bool FilterRow(ChartJsonDataset ds, int absRow, JsonValue cfg)

- static bool CellEq(JsonValue cell, JsonValue want)


## ChartKinds (class)

- static ChartType TypeOf(string s)

- static string CustomOf(string s)

- static string TypeName(ChartType t)

- static string SymbolSanitize(string s)

- static string SelectedModeSanitize(string s)

- static bool SelectedModeEnabled(string s)

- static string RoamWheelModeSanitize(string s)

- static string LegendModeSanitize(string s)

- static ChartAxisType AxisOf(string s)

- static string AxisName(ChartAxisType t)


## ChartLevel (class)

- List<int> color;

- int hasColorSat;

- int satLo;

- int satHi;

- int colorMappingBy;

- int borderWidth;

- int borderColor;

- int gapWidth;

- int borderColorSat;

- int upperShow;

- int upperHeight;

- int r0;

- int r;

- int labelMode;

- int labelRotate;

- int r0Pct;

- int rPct;

- int colorOne;

- int labelColor;

- static ChartLevel Of()

- static ChartLevel Clone(ChartLevel src)


## ChartLineStyle (class)

- int color;

- int color0;

- int width;

- string type;

- int shadowColor;

- int shadowBlur;

- List<int> gradStops;

- bool transparent;

- int alpha;

- static ChartLineStyle Of(int color, int width)

- static ChartLineStyle Clone(ChartLineStyle src)


## ChartLink (class)

- string from;

- string to;

- int val;

- static ChartLink Of(string from, string to, int val)

- static ChartLink Clone(ChartLink src)


## ChartMapEntry (class)

- string name;

- string svg;

- double vx;

- double vy;

- double vw;

- double vh;

- bool scanned;

- List<ChartMapRegion> regions;

- static ChartMapEntry Of(string name)


## ChartMapPoint (class)

- int x;

- int y;

- static ChartMapPoint Of(int x, int y)


## ChartMapRegion (class)

- string name;

- int value;

- int color;

- List<ChartMapRing> rings;

- static ChartMapRegion Of(string name, int value, List<ChartMapRing> rings)

- static ChartMapRegion Colored(string name, int value, int color, List<ChartMapRing> rings)

- static ChartMapRegion Clone(ChartMapRegion src)


## ChartMapRing (class)

- List<ChartMapPoint> points;

- bool hole;

- static ChartMapRing Of(List<ChartMapPoint> points, bool hole)


## ChartMaps (class)

- static List<ChartMapEntry> all;

- static List<ChartMapEntry> Table()

- static ChartMapEntry Find(string name)

- static void Register(string name, string geoJsonText)

- static void RegisterRegions(string name, List<ChartMapRegion> regions)

- static void RegisterSvg(string name, string svgText)

- static void EnsureScanned(ChartMapEntry e)

- static void BindSeries(ChartSeries s)


## ChartMarkArea (class)

- bool xMode;

- int v0;

- int v1;

- int color;

- string name;

- static ChartMarkArea Of()


## ChartMarkLine (class)

- string kind;

- string name;

- int xValue;

- int yValue;

- bool hasPoint;

- string label;

- string lineType;

- int color;

- bool labelShow;

- bool startArrow;

- bool endArrow;

- bool hasEnd;

- int x2Value;

- int y2Value;

- int valueIndex;

- static ChartMarkLine Of(string kind)

- static ChartMarkLine Of(string kind, string name)

- static ChartMarkLine Average()

- static ChartMarkLine Average(string name)

- static ChartMarkLine Min()

- static ChartMarkLine Max()

- static ChartMarkLine At(int yValue, string label)

- static ChartMarkLine Clone(ChartMarkLine src)


## ChartMarkPoint (class)

- string kind;

- string name;

- int dataIndex;

- int xValue;

- int yValue;

- string label;

- int color;

- string symbol;

- int size;

- int valueIndex;

- bool sizeSet;

- static ChartMarkPoint Max()

- static ChartMarkPoint Max(string name)

- static ChartMarkPoint Min()

- static ChartMarkPoint Min(string name)

- static ChartMarkPoint Point(int xValue, int yValue, string label)

- static ChartMarkPoint Clone(ChartMarkPoint src)


## ChartMatrixBandNode (class)

- public string text;

- public int level;

- public int first;

- public int span;

- public int kind;

- public ChartBoxParam size;

- ChartMatrixBandNode(string text, int level, int first, int span, int kind, ChartBoxParam size)


## ChartMatrixBodyCell (class)

- public string text;

- public int x0;

- public int x1;

- public int y0;

- public int y1;

- public int labelColor;

- public int labelFontSize;

- ChartMatrixBodyCell(string text, int x0, int x1, int y0, int y1, int labelColor, int labelFontSize)


## ChartMatrixCellQuery (class)

- public string name;

- public int index;

- public bool isX;

- ChartMatrixCellQuery(string name, int index, bool isX)


## ChartMatrixSpec (class)

- ChartBoxParam lRaw;

- ChartBoxParam tRaw;

- ChartBoxParam rRaw;

- ChartBoxParam bRaw;

- bool xShow;

- bool yShow;

- ChartBoxParam xLevelSize;

- ChartBoxParam yLevelSize;

- int xItemColor;

- int yItemColor;

- List<ChartMatrixBandNode> xNodes;

- List<ChartMatrixBandNode> yNodes;

- int xLeaves;

- int yLeaves;

- int xLevels;

- int yLevels;

- List<ChartMatrixCellQuery> queries;

- List<ChartMatrixBodyCell> bodyCells;

- bool bodyBorder;

- List<string> cornerText;

- ChartBoxParam wRaw;

- ChartBoxParam hRaw;

- bool leftCenter;

- bool topMiddle;

- int xLabelFontSize;

- int xLabelColor;

- bool xLabelBold;

- int yLabelFontSize;

- int yLabelColor;

- bool yLabelBold;

- List<ChartBoxParam> XSizes()

- List<ChartBoxParam> YSizes()

- static ChartMatrixSpec Of()

- static ChartMatrixSpec ParseOne(JsonValue v)

- static ChartMatrixSpec ParseOne(JsonValue v, JsonValue ovrX, JsonValue ovrY)

- static void MatCoordSpan(ChartMatrixSpec m, bool isX, JsonValue e, out int c0, out int c1)

- static int ParseCells(JsonValue arr, ChartMatrixSpec m, bool isX, int first, int level)

- int FindCell(bool isX, string name)


## ChartNode (class)

- string name;

- int val;

- string id;

- int color;

- List<int> vals;

- string symbol;

- int size;

- bool collapsed;

- List<ChartNode> children;

- static ChartNode Leaf(string name, int val)

- static ChartNode Group(string name, List<ChartNode> children)

- static ChartNode GroupVal(string name, int val, List<ChartNode> children)

- static ChartNode Shaped(string name, int val, string symbol, int size)

- int ValueOf(int dim)

- static ChartNode Clone(ChartNode src)

- int Total()


## ChartNodeCoord (class)

- public int x;

- public int y;

- public ChartNodeCoord(int x, int y)

- public static ChartNodeCoord Of(int x, int y)


## ChartOption (class)

- string title;

- string titleSub;

- string titleAlign;

- int titleColor;

- int titleSubColor;

- List<ChartAxis> xAxes;

- List<ChartJsonDataset> datasets;

- List<ChartVisualMap> visualMaps;

- List<ChartTitleSpec> titles;

- List<ChartGridSpec> grids;

- List<ChartMarkArea> markAreas;

- List<ChartAxis> yAxes;

- List<ChartSeries> series;

- List<ChartSeries> declOrder;

- List<ChartGeo> geos;

- List<ChartCalendarSpec> calendars;

- ChartMatrixSpec matrix;

- List<ChartParAxis> parAxes;

- List<PolarAxisSpec> angleAxes;

- List<PolarAxisSpec> radiusAxes;

- string parLayout;

- ChartType defaultType;

- bool showLegend;

- bool showTooltip;

- string tooltipTrigger;

- string tooltipTriggerOn;

- bool toolboxShow;

- bool toolboxDataView;

- bool toolboxMagicLine;

- bool toolboxMagicBar;

- bool toolboxRestore;

- bool toolboxSave;

- bool toolboxZoom;

- bool roam;

- string roamWheelMode;

- bool mapSelect;

- bool showGrid;

- bool showValues;

- bool animate;

- bool smooth;

- bool pointerCross;

- bool gradient;

- bool rounded;

- int animMs;

- int donutPct;

- int gaugeMax;

- bool pieLabels;

- bool pieCenterText;

- bool pieRings;

- bool overlay;

- List<RadarIndicator> radarIndicators;

- List<int> radarMax;

- List<string> radarNames;

- bool radarScaled;

- string radarShape;

- bool radarSplitArea;

- int radarNameColor;

- List<RadarPolar> polars;

- int legendTextColor;

- string legendOrient;

- string legendX;

- string legendSelectedMode;

- string legendFormatter;

- List<string> legendData;

- string legendLeft;

- string legendRight;

- string legendTop;

- string legendBottom;

- bool legendHasLeft;

- bool legendHasRight;

- bool legendHasTop;

- bool legendHasBottom;

- int legendLeftPx;

- int legendRightPx;

- int legendTopPx;

- int legendBottomPx;

- int legendBoxW;

- int legendBoxH;

- bool dataRangeShow;

- int dataRangeLo;

- int dataRangeHi;

- bool dataRangeCalculable;

- int dataRangeSplit;

- bool dataRangeHoverLink;

- List<ChartRangeSplit> dataRangeList;

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

- int gridLeftPct;

- int gridRightPct;

- int gridTopPct;

- int gridBottomPct;

- bool containLabel;

- ChartBoxParam gridLeftRaw;

- ChartBoxParam gridRightRaw;

- ChartBoxParam gridTopRaw;

- ChartBoxParam gridBottomRaw;

- ChartBoxParam gridWidthRaw;

- ChartBoxParam gridHeightRaw;

- ChartBoxParam obLeftRaw;

- ChartBoxParam obRightRaw;

- ChartBoxParam obTopRaw;

- ChartBoxParam obBottomRaw;

- bool obDeclared;

- int zoomStart;

- int zoomEnd;

- string zoomStartValue;

- string zoomEndValue;

- List<ChartZoom> zooms;

- ChartZoom zoomX;

- ChartZoom zoomY;

- int windowStart;

- int windowEnd;

- int backgroundColor;

- List<int> colorPal;

- int iwid;

- int connectGroup;

- List<int> rampColors;

- int rampLo;

- int rampHi;

- static int Auto()

- static int RawBound(double d)

- static double AutoD()

- static bool IsAutoD(double v)

- static bool SameIsh(double a, double b)

- ChartOption ConnectGroup(int groupId)

- static ChartOption Clone(ChartOption src)

- static ChartOption Create()

- static ChartOption Of(List<ChartSeries> series)

- static ChartOption Of(string title, List<string> categories, List<ChartSeries> series)

- static ChartOption Single(ChartSeries s)

- static ChartOption OfRings(string title, List<ChartSeries> series)

- static ChartOption With(ChartType t, string title, List<string> cats)

- static ChartOption Line(string title, List<string> cats)

- static ChartOption Bar(string title, List<string> cats)

- static ChartOption Pie(string title, List<string> cats)

- static ChartOption Scatter(string title)

- static ChartOption Treemap(string title)

- ChartOption Add(string name, List<int> values)

- ChartOption Add(string name, ChartType type, List<int> values)

- ChartOption AddLabel(string name, List<ChartData> data)

- ChartOption Add(ChartSeries s)

- ChartOption Categories(List<string> cats)

- ChartOption RoamWheel(string mode)

- ChartOption X(ChartAxis ax)

- ChartOption Y(ChartAxis ax)

- ChartOption RadarIndicators(List<RadarIndicator> indicators)

- ChartOption RadarMax(List<int> maxs)

- ChartOption VisualRange(int lo, int hi, List<int> colors)

- ChartOption DataZoom(int start, int end)

- ChartOption Grid(int left, int right, int top, int bottom)

- ChartAxis XAxis0()

- ChartAxis YAxis0()

- ChartAxis YAxis1()

- List<string> XCategories()

- static ChartOption FromJson(string src)

- static long TimeValueSec(JsonValue v, long dflt)

- static int TimeDayOf(JsonValue v)

- static long TimeStringSec(string s)

- static ChartScalar ParseScalar(JsonValue v, ChartScalar dflt)

- static int ParseColor(JsonValue v, int dflt)

- static List<int> ParseGradStops(JsonValue cv)

- static void ParseTextStyle(JsonValue v, ChartTextStyle text)

- static void ParsePaddingInto(JsonValue pv, ChartTextStyle text, ChartRichStyle rs)

- static int RoundOff(double d)

- static void ParseRichStyle(JsonValue v, ChartRichStyle rs)

- static void ParseLineStyle(JsonValue v, ChartLineStyle line)

- static ChartPosition ParsePoint(JsonValue v, ChartPosition dflt)

- static void ParseGauge(JsonValue v, GaugeOption gauge)

- static void ParseItemStateStyle(JsonValue v, ChartItemStateStyle style)

- static void ParseItemStyle(JsonValue v, ChartItemStyle style)

- static void ParseSeriesStyle(JsonValue v, ChartSeries s)

- static void ParseGrid(JsonValue v, ChartOption o)

- static void ParseGridBody(JsonValue v, ChartOption o)

- static ChartBoxParam ParseBoxParam(JsonValue e)

- static bool StrLooksNumeric(string s)

- static void ParseGridEdge(JsonValue e, bool isLeft, bool isVert, ChartOption o)

- static int PercentOf(string s, int dflt)

- static void ParseDataZoom(JsonValue v, ChartOption o)

- static ChartZoom ParseZoomComp(JsonValue z)

- static int ZoomAxisIndex(JsonValue v)

- static void ParseZoomBoxPct(JsonValue v, ChartZoom c, int kind)

- static void ParseLegendAnchor(JsonValue v, string key, bool isVert, bool isFar, ChartOption o, int sidePx)

- static void ParseMarkArea(JsonValue v, ChartOption o)

- static int MarkAreaNone()

- static int MarkAreaCatIdx(ChartOption o, JsonValue xv)

- static int StackIdFor(string name)

- static void ParseLinks(JsonValue v, List<ChartLink> into)

- static void ParseTree(JsonValue v, List<ChartNode> into)

- static void ParseGraphLinks(JsonValue v, List<ChartLink> into, List<string> gnames)

- static string GraphNodeName(JsonValue a, JsonValue b, List<string> gnames)

- static int AxisGapPerMille(JsonValue x)

- static int PercentPerMille(string s)

- static void ParseLevels(JsonValue v, List<ChartLevel> into)

- static void ParseCandles(JsonValue v, List<Candle> into)

- static void ParseBubbles(JsonValue v, List<Bubble> into)

- static void ParseBoxes(JsonValue v, List<BoxItem> into)

- static void ParseErrors(JsonValue v, List<ErrorItem> into)

- static int GeoQ(double d)

- static void ParsePoints(JsonValue v, List<ChartPoint> into)

- static int Mul1000(double d)

- static void ParseEvents(JsonValue v, List<ChartEvent> into)

- static List<ChartMapPoint> ParseMapPoints(JsonValue arr)

- static void ParseRegions(JsonValue v, List<ChartMapRegion> into)

- static void ParseGeo(JsonValue v, ChartOption o)

- static void ParseParAxes(JsonValue v, ChartOption o)

- static void ParsePolarAxes(JsonValue v, ChartOption o, bool isAngle)

- static void ParsePolarAxisOne(JsonValue a, ChartOption o, bool isAngle, int slot)

- static void ParseGeoOne(JsonValue g, ChartOption o, int index)

- static int MarkCoordX(JsonValue p, int dft)

- static int MarkCoordY(JsonValue p, int dft)

- static void ParseMarkLines(JsonValue v, List<ChartMarkLine> into)

- static void ParseTitlePos(JsonValue v, string key, ChartTitleSpec ts, bool isTop)

- static ChartTitleSpec ParseTitleOne(JsonValue tit)

- static ChartTitleSpec ParseTitleOne(JsonValue tit, ChartOption o)

- static ChartCalendarSpec ParseCalendarOne(JsonValue it)

- static void ParseCalendar(JsonValue v, ChartOption o)

- static int CellSizeOf(JsonValue v)

- static string NameMapOf(JsonValue v)

- static int FontSizeOf(JsonValue v, int dflt)

- static int ParseDateDay(string s, int dflt)

- static bool IsYearOnly(string s)

- static bool IsYearMonthOnly(string s)

- static bool AllDigits(string s)

- static JsonValue MediaEntryById(JsonValue arr, string id)

- static bool MatAnchorCoord(JsonValue coord, ChartOption o, out int x0, out int x1, out int y0, out int y1)

- static ChartGridSpec ParseGridOne(JsonValue it)

- static ChartGridSpec ParseGridOne(JsonValue it, ChartOption o)

- static void ParseVisualMap(JsonValue v, List<ChartVisualMap> into)

- static void ParseVisualMapOne(JsonValue v, List<ChartVisualMap> into)

- static int Lerp2(int a, int b, int tF)

- static int VisualSegmentColorAt(ChartOption o, int seriesIndex, int k0, int k1)

- static int VisualColorAt(ChartOption o, int seriesIndex, int absIdx, int val)

- static void ParseMarks(JsonValue v, List<ChartMarkPoint> into)

- static bool HasUserMapSeries(ChartOption o)

- static ChartOption FromJsonValue(JsonValue v)

- static void ParseAxis(JsonValue v, List<ChartAxis> into, bool isX)

- static void ParseAxisOne(JsonValue v, List<ChartAxis> into, bool isX)


## ChartParAxis (class)

- string name;

- int dim;

- int min;

- int max;

- bool minSet;

- bool maxSet;

- bool inverse;

- List<string> cats;

- static ChartParAxis Of()

- static ChartParAxis Clone(ChartParAxis src)


## ChartParallelRow (class)

- public List<int> values;

- public ChartParallelRow()

- public ChartParallelRow(List<int> values)

- public static ChartParallelRow Of(List<int> values)

- public static ChartParallelRow Clone(ChartParallelRow src)


## ChartPoint (class)

- int x;

- int y;

- int z;

- int size;

- bool sizeSet;

- int color;

- string name;

- static ChartPoint Of(int x, int y)

- static ChartPoint Sized(int x, int y, int size)

- static ChartPoint Clone(ChartPoint src)


## ChartPolarBarCol (class)

- double offset;

- double width;

- static ChartPolarBarCol Of(double o, double w)


## ChartPolarBarFrame (class)

- int cx;

- int cy;

- int r0Px;

- int r1Px;

- bool aCat;

- bool rCat;

- double aLo;

- double aHi;

- double rLo;

- double rHi;

- List<string> aCats;

- List<string> rCats;

- int startAngle;

- int endAngle;

- bool endAngleSet;

- bool clockwise;

- bool aOnBand;

- bool rOnBand;


## ChartPolarBarLayout (class)

- int r0;

- int r;

- int startAngleX10;

- int endAngleX10;

- bool clockwise;

- static ChartPolarBarLayout Of(int r0, int r, int a0X10, int a1X10, bool cw)


## ChartPosition (class)

- ChartScalar x;

- ChartScalar y;

- static ChartPosition Of(ChartScalar x, ChartScalar y)

- static ChartPosition Clone(ChartPosition src)


## ChartRangeSplit (class)

- string label;

- int lo;

- int hi;

- static ChartRangeSplit Of(string label, int lo, int hi)

- static ChartRangeSplit Clone(ChartRangeSplit src)


## ChartRichStyle (class)

- string name;

- int color;

- int fontSize;

- string fontWeight;

- string fontFamily;

- int tbColor;

- int tbWidth;

- int tsColor;

- int tsBlur;

- int tsDx;

- int tsDy;

- int align;

- int lineHeight;

- int backgroundColor;

- int bgIsImage;

- int borderColor;

- int borderWidth;

- int radTL;

- int radTR;

- int radBR;

- int radBL;

- int padT;

- int padR;

- int padB;

- int padL;

- int width;

- int height;

- static ChartRichStyle Create()


## ChartRiverPoint (class)

- string time;

- int value;

- int category;

- static ChartRiverPoint Of(string time, int val, int cat)

- static ChartRiverPoint Clone(ChartRiverPoint src)


## ChartScalar (class)

- int amount;

- bool percent;

- static ChartScalar Px(int amount)

- static ChartScalar Pct(int amount)

- static ChartScalar Clone(ChartScalar src)


## ChartSeries (class)

- string name;

- ChartType type;

- int axisIndex;

- int xIndex;

- bool visBlocked;

- int stack;

- string stackName;

- int stackStrategy;

- int color;

- bool hidden;

- int smooth;

- bool areaStyle;

- bool endLabelShow;

- int endLabelDistance;

- string endLabelFormatter;

- string step;

- string sort;

- string roseType;

- ChartPosition center;

- ChartScalar radius;

- ChartScalar radiusInner;

- int startAngle;

- bool clockWise;

- int polarIndex;

- int selectedOffset;

- int barWidth;

- int barWidthPct;

- int barHeight;

- int barMinHeight;

- int barMinAngle;

- bool roundCap;

- int barGapPct;

- string barWidthRaw;

- string barGapRaw;

- string barCategoryGapRaw;

- string barMinWidthRaw;

- string barMaxWidthRaw;

- bool large;

- int largeThreshold;

- bool silent;

- string symbol;

- int symbolSize;

- int pointG;

- string symbolSizeFn;

- int showSymbol;

- int showAllSymbol;

- bool showBackground;

- int backgroundColor;

- int innerPct;

- bool hbar;

- string treeOrient;

- int initialTreeDepth;

- bool treeExpandCollapse;

- ChartLineStyle treeLine;

- bool piePerPoint;

- string custom;

- string emphasisFocus;

- string selectedMode;

- string mapName;

- int geoIndex;

- bool isGeoBase;

- int mapBorderW;

- int mapBorderC;

- string picSymbol;

- string picRepeat;

- bool picClip;

- string picPos;

- int picBound;

- int picW;

- int picH;

- int picWPct;

- int picHPct;

- List<ChartParallelRow> parallelRows;

- List <List<int>> parRows;

- List<ChartRiverPoint> riverPoints;

- List<string> riverNames;

- int forceScaling;

- int forceGravity;

- int forceRepulsion;

- int forceEdgeLength;

- int forceMinRadius;

- int forceMaxRadius;

- int forceNodeColor;

- int forceLinkColor;

- string forceLinkSymbol;

- string chordRibbonType;

- int chordSort;

- bool chordSortSub;

- int chordGapDeg;

- int chordRingWidthPct;

- List <List<int>> matrix;

- string funnelAlign;

- int funnelGap;

- bool funnelPercent;

- int maxSizePct;

- bool funnelLabelOut;

- int forceCurveness;

- int wcMinFontSize;

- int wcMaxFontSize;

- string wcRotate;

- string wcRotateList;

- ChartItemStyle itemStyle;

- ChartTextStyle label;

- bool labelShow;

- bool pieLabels;

- string labelFmt;

- string labelPos;

- int labelShowT;

- int labelLineShow;

- int labelLineLen;

- int labelLineLen2;

- int labelDist;

- int labelEdgeDist;

- int labelAlignTo;

- int labelRotate;

- List<ChartData> data;

- List<Candle> candles;

- List<Bubble> bubbles;

- List<BoxItem> boxes;

- List<ErrorItem> errors;

- List<ChartPoint> points;

- List<ChartNode> tree;

- List<ChartLevel> levels;

- int visibleMin;

- int leafDepth;

- int tmBorderWidth;

- int tmBorderColor;

- int tmGapWidth;

- int tmBorderColorSat;

- int tmHasColorSat;

- int tmSatLo;

- int tmSatHi;

- int tmUpperShow;

- int tmUpperHeight;

- int sunLabelMode;

- int sunLabelRotate;

- int sunLabelColor;

- int sunItemColor;

- int visualDimension;

- int visualMin;

- int visualMax;

- List<ChartLink> links;

- List<ChartMapRegion> regions;

- List<ChartEvent> events;

- List<ChartMarkPoint> marks;

- string markLabelFmt;

- ChartTextStyle markLabelSt;

- string markLabelPos;

- int markLabelDist;

- int markSymbolSize;

- List<ChartMarkLine> markLines;

- bool llDeclared;

- int llDx;

- int llDy;

- int llX;

- int llY;

- string llAlign;

- bool llHideOverlap;

- string llMove;

- static int LL_UNSET=0-1000000000;

- string graphLayout;

- List<ChartNodeCoord> nodeCoords;

- List<string> graphCats;

- string coordSys;

- int z;

- int zlevel;

- int calIndex;

- int calCenter;

- string matCX;

- string matCY;

- string typeName;

- List <List<ChartPoint>> geoLines;

- GaugeOption gauge;

- static ChartSeries Of(string name, ChartType type, List<int> values)

- static ChartSeries Named(string name, ChartType type, List<ChartData> data)

- static ChartSeries Line(string name, List<int> values)

- static ChartSeries Area(string name, List<int> values)

- static ChartSeries Bar(string name, List<int> values)

- static ChartSeries StackedBar(string name, List<int> values)

- static ChartSeries HBar(string name, List<int> values)

- static ChartSeries HStacked(string name, List<int> values)

- static ChartSeries OfBarPoints(string name, List<ChartPoint> pts)

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

- static ChartSeries OfBubbles(string name, List<Bubble> bubbles)

- static ChartSeries OfBoxes(string name, List<BoxItem> boxes)

- static ChartSeries OfErrors(string name, List<ErrorItem> errors)

- static ChartSeries OfPoints(string name, List<ChartPoint> points)

- static ChartSeries OfLinePoints(string name, List<ChartPoint> points)

- static ChartSeries OfTree(string name, ChartType type, ChartNode root)

- static ChartSeries OfTreeOrient(string name, ChartNode root, string orient)

- static ChartSeries OfLinks(string name, List<ChartLink> links)

- static ChartSeries Chord(string name, List<ChartLink> links)

- static ChartSeries ChordMatrix(string name, List<string> nodes, List <List<int>> matrix)

- static ChartSeries Force(string name, List<ChartLink> links)

- static ChartSeries Venn(string name, List<ChartData> data)

- static ChartSeries Waterfall(string name, List<ChartData> data)

- static ChartSeries SunburstTree(string name, ChartNode root)

- static ChartSeries RadialBars(string name, int val)

- static ChartSeries OfRegions(string name, List<ChartMapRegion> regions)

- static ChartSeries OfEvents(string name, List<ChartEvent> events)

- ChartSeries MarkPoint(ChartMarkPoint m)

- ChartSeries MarkLine(ChartMarkLine m)

- ChartSeries GaugeRange(int min, int max)

- ChartSeries GaugeArc(int startAngle, int endAngle)

- ChartSeries GaugeLayout(int centerXPct, int centerYPct, int radiusPct)

- ChartSeries GaugeTicks(int splitNumber, int minorTicks)

- ChartSeries GaugeSizes(int axisWidth, int tickLength, int splitLength, int pointerLengthPct, int pointerWidth)

- ChartSeries GaugeVisible(bool axis, bool ticks, bool labels, bool title, bool detail)

- ChartSeries GaugeText(int titleX, int titleY, int detailX, int detailY, string suffix)

- ChartSeries GaugeColors(int stop1, int color1, int stop2, int color2, int stop3, int color3, int stop4, int color4)

- int Count()

- bool InLargeMode()

- double Number(int i)

- int Value(int i)

- bool SeriesFrac(int i)

- static ChartSeries Clone(ChartSeries src)

- static ChartSeries Clone(ChartSeries src, bool shareData)


## ChartSkin (class)

- static string current;

- static string Current()

- static void SetCurrent(string name)

- static int Count()

- static string Name(int i)

- static string Label(int i, bool en)

- static int IndexOf(string name)

- static int ColorAt(string name, int i)


## ChartSource (class)

- virtual int SeriesCount()

- virtual string SeriesName(int s)

- virtual int SeriesColor(int s)

- virtual int PointCount(int s)

- virtual int ValueAt(int s, int i)


## ChartStage (class)

- int val;

- int color;

- string name;

- int seriesIndex;

- int dataIndex;

- ChartStage(int v, int col, string nm, int si)


## ChartState (class)

- int version;

- int builtVersion;

- int builtW;

- int builtSeries;

- int cols;

- int axisLo;

- int axisHi;

- List <List<ChartBigColExtents>> seriesCols;

- List <List<int>> colMin;

- List <List<int>> colMax;

- int animationKey;

- bool animationKeySet;

- ChartState()

- ChartState(int key)

- int IntroKey(int w)

- void Invalidate()


## ChartSvgCursor (class)

- string s;

- int i;

- int to;

- public bool any;

- static ChartSvgCursor Of(string s, int from, int to)

- bool HasMore()

- int Peek()

- double NextNum()

- void SkipSep()

- static double ParseDouble(string s, int start, int end)


## ChartSvgMap (class)

- static void Scan(ChartMapEntry e)

- static void RootBox(ChartMapEntry e)

- static bool IsSkipEl(string name)

- static void Tag(string svg, int lt, int gt, List<double> tf, List<string> skip, List<ChartSvgShape> shapes, List<string> gnames)

- static bool NoFill(string svg, int from, int to)

- static List<ChartMapRing> ShapeRings(string name, string svg, int from, int to, double a, double b, double c, double d, double ee, double f)

- static List<ChartMapRing> PathRings(string dd, double a, double b, double c, double d, double ee, double f)

- static void Finish(List<ChartMapRing> rings, List<ChartMapPoint> cur)

- static void Emit(List<ChartMapPoint> pts, double x, double y, double a, double b, double c, double d, double ee, double f)

- static List<double> ParseTransform(string tr, double a, double b, double c, double d, double ee, double f)

- static string Attr(string svg, int from, int to, string name)

- static double UnitNum(string s)

- static void Push(List<double> tf, double a, double b, double c, double d, double ee, double f)


## ChartSvgShape (class)

- string name;

- List<ChartMapRing> rings;

- static ChartSvgShape Of(string name, List<ChartMapRing> rings)


## ChartTextStyle (class)

- int color;

- int fontSize;

- string fontStyle;

- string fontWeight;

- string fontFamily;

- int shadowColor;

- int shadowBlur;

- int tbColor;

- int tbWidth;

- int tsColor;

- int tsBlur;

- int tsDx;

- int tsDy;

- int lineHeight;

- List<ChartRichStyle> rich;

- int boxBg;

- int boxBorder;

- int boxBorderW;

- int boxRadius;

- int padT;

- int padR;

- int padB;

- int padL;

- static ChartTextStyle Create()

- static ChartTextStyle Clone(ChartTextStyle src)


## ChartTheme (class)

- static App defaultDone;

- static string Css(string name)

- static bool Use(App app, string name)

- static void EnsureDefault(App app)

- static List<string> Names()

- static List<string> Roots()


## ChartTimeline (class)

- List<ChartOption> frames;

- List<string> labels;

- int intervalMs;

- ChartTimeline()

- static ChartTimeline Of()

- ChartTimeline Frame(ChartOption o)

- ChartTimeline Labels(List<string> ls)

- ChartTimeline AutoPlay(int interval)

- int FrameCount()

- ChartOption FrameAt(int i)

- ChartOption Current(int idx)

- static int TimelineIdxOf(App app, int wid, int n)

- static ChartTimeline FromJsonValue(JsonValue tl, JsonValue optionsNode)

- string LabelAt(int i)

- static int Clamp(int i, int n)

- static int StepPlay(int i, int n, int last, int now, int interval)

- static ChartOption Merge(ChartOption baseOpt, ChartOption frame)

- static int TlIdxKey(int wid)

- static int TlPlayKey(int wid)

- static int TlTickKey(int wid)

- static int TlHitPlay(int wid)

- static int TlHitPrev(int wid)

- static int TlHitNext(int wid)

- static int TlHitDot(int wid, int bi)

- static int Height(App app)

- int Draw(App app, int wid, int x, int y, int w)


## ChartTitleSpec (class)

- string text;

- string sub;

- int topPct;

- int topPx;

- int leftPct;

- int leftPx;

- string align;

- string id;

- bool matAnchor;

- int mx0;

- int mx1;

- int my0;

- int my1;

- static ChartTitleSpec Of()


## ChartToolbox (class)

- static int Btn(App app)

- static int Gap(App app)

- static int Chrome(App app)

- static int Height(App app)

- static int Pad(App app)

- static bool Cartesian(ChartOption o)

- static List<int> Actions(ChartOption o)

- static int Width(App app, ChartOption o)

- static void Draw(App app, int x, int y, int w, int h, int wid, ChartOption o)

- static void Click(App app, int action, int wid, ChartOption o, int x, int y, int w, int h, int magic, int zOn)


## ChartView (class)

- static double BarBandWidth(int plotW, int count)

- static bool PercentOf(string raw, double baseW, out double outV)

- static double AutoBarWidth(double remained, double catGapNum, int autoCount, double gapPct)

- static List<ChartBarCol> CalcBarCols(double bandW, List<ChartSeries> bars, List<string> stackIds)

- static int RoundPx(double v)

- static int BarX0(ChartFrame f, int i, double band, double off)

- static int BarY0(int plotY, int i, double band, double off)


## ChartView (class)

- static double PolarBandWidth(double pxSpan, int count, bool onBand)

- static List<ChartPolarBarCol> CalcPolarBarCols(double bandW, List<ChartSeries> bars, List<string> stackIds)

- static string PolarStackIdOf(ChartSeries s, int si)

- static bool PolarBaseIsAngle(bool angleIsCategory, bool radiusIsCategory)

- static List <List<ChartPolarBarLayout>> PolarBarLayout(List<ChartSeries> bars, List<ChartPolarBarCol> cols, List<string> stackIds, bool valueIsRadius, bool stacked, List <List<double>> baseCoords, List <List<double>> valCoords, double valStart, List<bool> clampFlags, double valCoordLo, double valCoordHi)

- static double DAbs(double v)


## ChartView (class)

- ChartOption option;

- ChartOption sourceOption;

- ChartOption initialOption;

- ChartController controller;

- string directStatus;

- string directStatusMessage;

- ResolvedChart resolved;

- bool staticFrame;

- int wid;

- UiEvent Rendered;

- UiEvent Resized;

- UiEvent Updated;

- UiEvent Disposed;

- int lastRenderW;

- int lastRenderH;

- int directVersion;

- int lastControllerVersion;

- int lastDirectVersion;

- bool hasRendered;

- bool disposed;

- ChartEventHub eventHub;

- int eventHitId;

- ChartHit currentHit;

- int lastZoomLo;

- int lastZoomHi;

- int lastEventStateWid;

- static ChartView activeEventView;

- static List<int> hoverSlotWids;

- static List<ChartHit> hoverSlotHits;

- static List<int> hoverSlotOvers;

- static int HoverSlotIdx(int wid)

- static ChartHit HoverSlotHit(int wid)

- static bool HoverSlotOver(int wid)

- static void HoverSlotSet(int wid, ChartHit hit, bool over)

- void InitLifecycle()

- static void ApplyDataLegend(App app, ChartOption o)

- static bool tailHooked;

- static string ttName;

- static int ttVal;

- static int ttX;

- static int ttY;

- static int ttW;

- static int ttH;

- static int ttRadius;

- static ChartNode ttNode;

- static int ttA0;

- static int ttA1;

- static int ttDepth;

- static ChartNode ttSunNode;

- static ChartView Of(ChartOption o)

- static ChartView Keyed(ChartOption o, int key)

- static ChartView KeyedStatic(ChartOption o, int key)

- static ChartView Bind(ChartController controller)

- static ChartView BindKeyed(ChartController controller, int key)

- ChartView OnRendered(Action a)

- ChartView OnResize(Action a)

- ChartView OnUpdated(Action a)

- ChartView OnDisposed(Action a)

- ChartView On(ChartEventType type, ChartInteractionHandler handler)

- ChartView Off(ChartEventType type, ChartInteractionHandler handler)

- ChartView ClearEvents()

- void RaiseInteraction(ChartInteractionEvent eventArgs)

- void RaiseLocal(ChartInteractionEvent eventArgs)

- static void RaiseActive(ChartInteractionEvent eventArgs)

- static ChartInteractionEvent EventAt(ChartEventType type, int wid, int px, int py)

- static ChartInteractionEvent EventFromHit(ChartEventType type, int wid, ChartHit hit)

- static void OfferActiveHit(ChartHit hit)

- static bool HoverBlocked(App app)

- static void OfferTopmostCategoryHit(ChartHit hit)

- static ChartHit MakeDataHit(ChartSeries s, int seriesIndex, int dataIndex, string fallbackName, ChartElementType elementType, int px, int py, int value)

- static void OfferCategoryPointHits(App app, ChartFrame f, List<string> labels, List<ChartSeries> series, int baseIdx, int g, ChartElementType elementType, bool forceTopmost)

- static bool HasInteraction(ChartView v, ChartEventType type)

- static int SegDist2(int mx, int my, int px, int py, int qx, int qy)

- static bool OwnsActiveClick(App app, int id)

- void DispatchPointerEvents(App app, int x, int y, int w, int h, string statusNow)

- ChartView OnClick(ChartInteractionHandler handler)

- ChartView OnDoubleClick(ChartInteractionHandler handler)

- ChartView OnHover(ChartInteractionHandler handler)

- ChartView OnMouseOut(ChartInteractionHandler handler)

- ChartView OnLegendSelected(ChartInteractionHandler handler)

- ChartView OnPieSelected(ChartInteractionHandler handler)

- ChartView OnMapSelected(ChartInteractionHandler handler)

- ChartView OnMapRoam(ChartInteractionHandler handler)

- ChartView OnTimelineChanged(ChartInteractionHandler handler)

- ChartView OnMagicTypeChanged(ChartInteractionHandler handler)

- ChartView OnRefresh(ChartInteractionHandler handler)

- ChartView OnForceLayoutEnd(ChartInteractionHandler handler)

- ChartView OnDataZoom(ChartInteractionHandler handler)

- ChartView OnDataRange(ChartInteractionHandler handler)

- ChartView OnRestore(ChartInteractionHandler handler)

- ChartView OnResizeEvent(ChartInteractionHandler handler)

- ChartView OnDataChanged(ChartInteractionHandler handler)

- ChartOption GetOption()

- string Status()

- string StatusMessage()

- bool IsDisposed()

- ChartView SetOption(ChartOption next)

- ChartView Refresh()

- ChartView Restore()

- ChartView Clear()

- ChartView ShowLoading(string message)

- ChartView HideLoading()

- ChartView SetEmptyMessage(string message)

- ChartView SetErrorMessage(string message)

- void Dispose()

- static int cacheSeq=16;

- static int AllocCacheSlot()

- static void ReleaseCached(Canvas c, int slot)

- static bool RenderCached(App app, ChartOption o, int slot, int x, int y, int w, int h)

- static List<ChartView> cachedOwners;

- static List<int> cachedOwnerKeys;

- static List<int> cachedOwnerRects;

- static void SetCachedOwner(int key, ChartView v, int x, int y, int w, int h)

- static ChartView CachedOwner(ChartOption o, int key, int x, int y, int w, int h)

- void BeginCachedEventFrame(App app, int x, int y, int w, int h)

- static int CacheFingerprint(App app, ChartOption o, ResolvedChart resolved, int x, int y, int w, int h)

- static int FoldStr(int h, string s, int seed)

- static int FingerprintCore(int themeGen, int zoomLo, int zoomHi, ChartOption o, ResolvedChart resolved)

- static ChartSeries LeadSeries(ChartOption o)

- static ChartSeries MapBaseSeries(ChartOption o)

- static int LeadSeriesIndex(ChartOption o)

- static int SeriesIndexOf(ChartOption o, ChartSeries target)

- static ChartType LeadType(ChartOption o)

- static int ZKeyOf(ChartSeries s)

- static void ZOrder(ChartOption o, List<int> order)

- static void ZSort(ChartOption o)

- static string DispatchKind(ChartOption o)

- static void DrawEmptyState(App app, int x, int y, int w, int h, ChartOption o)

- static void DrawStatusState(App app, int x, int y, int w, int h, string status, string message)

- static bool HasArea(ChartOption o)

- static bool IsLineFamily(ChartType t)

- static bool IsBarFamily(ChartType t)

- static void StackResults(List<double> rawVals, List<int> strategies, List<double> into)

- static void StackTopAll(List<ChartSeries> series, int oi, List<double> into)

- static double StackTopAt(List<ChartSeries> series, int si, int oi)

- static double StackBaseAt(List<ChartSeries> series, int si, int oi)

- static bool IsStackedSeries(ChartSeries s)

- static int GroupIndex(List<string> groups, string name)

- void Render(App app, int x, int y, int w, int h)

- static int ConnectedWid(App app, int wid, int group)

- static void ApplyMagic(ChartOption o, int magic)

- void DispatchRender(App app, int x, int y, int w, int h, int g)

- static void RenderOnce(App app, int x, int y, int w, int h, ChartOption option, int g)

- static void DrawOverlaySeries(App app, int x, int y, int w, int h, ChartOption o, int g, string kind)

- static RichBlock RichLayout(App app, List<RichSeg> segs, ChartTextStyle st, int defFs)

- static void DrawTextStyled(Canvas c, int x, int y, string text, int color, int fs, int tbColor, int tbWidth, int tsColor, int tsBlur, int tsDx, int tsDy)

- static void DrawTextStyled(Canvas c, int x, int y, string text, int color, int fs, ChartTextStyle st)

- static void RichDraw(App app, Canvas c, int ox, int oy, RichBlock b, int defCol)


## ChartView (class)

- static void DrawBars(App app, int x, int y, int w, int h, ChartOption o, int g, bool stacked, bool horizontal)

- static void DrawValueXBars(App app, ChartFrame f, ChartSeries s, int si, int i0, int i1, int g, ChartOption o)

- static void BarCell(Canvas c, int x, int y, int w, int h, int rad, int color, bool gradient)

- static void BarCellR4(Canvas c, int x, int y, int w, int h, List<int> r4, int radDef, int color, bool gradient)

- static ChartFrame HBarCore(App app, int x, int y, int w, int h, ChartOption o, List<string> labels, List<ChartSeries> series, bool stacked, int g, int baseIdx)

- static void DrawWaterfall(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void CalendarGeom(App app, ChartOption o, int ci, int x, int y, int w, int h, out int rectX, out int rectY, out int rectW, out int rectH, out int cellW, out int cellH, out int weeks, out int fweek, out int allDay)

- static int CalendarWeekday(int day)

- static void CalendarCellTL(ChartCalendarSpec c, int rectX, int rectY, int cellW, int cellH, int fweek, int rel, out int cx, out int cy)

- static void DrawCalendar(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawCalendarOne(App app, int ox, int oy, int ow, int oh, ChartOption o, int ci, int g)

- static void DrawCalendarSplit(App app, ChartCalendarSpec spec, int rectX, int rectY, int cellW, int cellH, int fweek, int color, int thickness)

- static void DrawCalendarYearLabel(App app, ChartCalendarSpec spec, int rectX, int rectY, int cellW, int cellH, int fweek, int weeks)

- static void DrawCalendarMonthLabels(App app, ChartCalendarSpec spec, int rectX, int rectY, int cellW, int cellH, int fweek, int weeks)

- static void DrawCalendarDayLabels(App app, ChartCalendarSpec spec, int rectX, int rectY, int cellW, int cellH, int weeks)

- static string MonthNameOf(string nameMap, int m)

- static string DayNameOf(string nameMap, int day)

- static int CalendarNeutral00()

- static int CalendarNeutral10()

- static int CalendarNeutral50()

- static int CalendarNeutral70()

- static void DrawCalendarSeries(App app, int ox, int oy, int ow, int oh, ChartOption o, int ci, int g)

- static void CalendarPairs(ChartSeries s, List<int> days, List<int> vals)

- static int CalendarValLo(ChartOption o, List<int> vals)

- static int CalendarValHi(ChartOption o, List<int> vals)

- static int CalendarRamp(ChartOption o, int v, int dLo, int dHi)

- static void DrawCalendarHeat(App app, ChartOption o, ChartSeries s, int si, ChartCalendarSpec spec, int rectX, int rectY, int cellW, int cellH, int fweek, int allDay)

- static void DrawCalendarPoints(App app, ChartOption o, ChartSeries s, int si, ChartCalendarSpec spec, int rectX, int rectY, int cellW, int cellH, int fweek, int allDay, bool effect)

- static int CalendarLabelFont(App app, ChartOption o, int si)

- static int CalendarLabelColor(App app, ChartOption o, int si)

- static void DrawCalendarGraph(App app, ChartOption o, ChartSeries s, int si, ChartCalendarSpec spec, int rectX, int rectY, int cellW, int cellH, int fweek, int allDay)

- static int CalendarNodeOf(ChartSeries s, string name)

- static void DrawCalendarPies(App app, ChartOption o, ChartSeries s, int si, ChartCalendarSpec spec, int rectX, int rectY, int cellW, int cellH, int fweek, int allDay)

- static double CalVal(ChartData d)

- static int CalendarVizOpacity(ChartOption o, int si)

- static int SeriesColorAt(App app, ChartOption o, int si)


## ChartView (class)

- static int EventRiverWaveAmp(int height)

- static int EventRiverColorIndex(List<string> names, string name)

- static void DrawEventRiver(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static int FinanceRevealAlpha(int progress)

- static void DrawCandles(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void CandleMarkPin(App app, Canvas c, int lx, int ly, int rad, int col, string lbl, int mFs, ChartFrame f)

- static void DrawBubbles(App app, int x, int y, int w, int h, ChartOption o, int g)

- static List<string> BoxLabels(List<BoxItem> items)

- static List<string> ErrorLabels(List<ErrorItem> items)

- static void DrawBox(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void BoxTooltip(App app, ChartOption o, List<ChartSeries> bxs, int k, int s, int mx, int my)

- static void DrawBoxOverlay(App app, ChartOption o, int lo, int hi, int n, bool horiz, int pX, int pY, int pW, int pH, int rowH, int revealAlpha)

- static void DrawError(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void DrawHeatmap(App app, int x, int y, int w, int h, ChartOption o, int g)

- static bool HeatmapGridDetect(ChartOption o)

- static int HeatmapCellStart(int dimension, int index, int count)

- static int HeatmapCellSize(int dimension, int index, int count)

- static int HeatmapIndexAt(int q, int dimension, int count)

- static void DrawHeatmapCore(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void HeatmapHover(App app, int x, int y, int w, int h, ChartOption o)


## ChartView (class)

- static HeatGridGeom HeatmapGridGeomFromFacts(App app, int x, int y, int w, int h, ChartOption o, int top, int maxX, int maxY, double eLo, double eHi)

- static ChartOption heatMemoOpt;

- static int heatMemoX;

- static int heatMemoY;

- static int heatMemoW;

- static int heatMemoH;

- static int heatMemoScale;

- static HeatGridGeom heatMemo;

- static void HeatMemoStore(App app, ChartOption o, int x, int y, int w, int h, HeatGridGeom geo)

- static HeatGridGeom HeatMemoLoad(App app, ChartOption o, int x, int y, int w, int h)

- static void HeatFactsScan(ChartOption o, out int maxX, out int maxY, out double eLo, out double eHi)

- static int HeatmapGridColorAt(HeatGridGeom geo, double v, int g)

- static int MatColorAt(List<int> ramp, double dLo, double dHi, int split, double v, int g)

- static int HeatmapGridStep(int needs, int have)

- static string HeatmapGridNumText(double v)

- static void DrawHeatmapGridCore(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawHeatmapGridBody(App app, ChartOption o, HeatGridGeom geo, int g, int x, int w)

- static void HeatmapGridHover(App app, int x, int y, int w, int h, ChartOption o)


## ChartView (class)

- static string TreeNodeName(ChartNode n)

- static int TreeChildCount(ChartNode n)

- static int TreemapValue(ChartNode n)

- static int TreemapTotal(List<ChartNode> nodes)

- static int TreemapAdd(int left, int right)

- static void DrawTreemap(App app, int x, int y, int w, int h, ChartOption o, int g)

- static int TmBuildIdMap(List<ChartNode> nodes, Dict <string, int> into, int next)

- static int TmNodeIndex(List<ChartNode> nodes, ChartNode target)

- static ChartLevel TmLevel(ChartSeries s, int depth)

- static bool TmAnyLevelColor(ChartSeries s)

- static TmStyle TmStyleOf(App app, ChartSeries s, int depth)

- static double TmSquareRatio()

- static double DMin(double a, double b)

- static double TmNodeValue(ChartNode n, int dim)

- static double TmWorst(double rowArea, double areaMax, double areaMin, double side, double ratio)

- static double TmRowArea(List<int> row, List<double> areas)

- static double TmRowMax(List<int> row, List<double> areas)

- static double TmRowMin(List<int> row, List<double> areas)

- static void TmPosition(TmBox rect, List<int> row, List<double> areas, double side, double halfGap, bool flush, int depth, List<TmRect> rects)

- static int TmValueT(ChartSeries s, double lo, double hi, double v)

- static int TmLerp(int lo, int hi, int tPerMille)

- static int RgbSat(int color, int satPerMille)

- static int PaletteLerp(List<int> stops, int tPerMille)

- static void TmUpperLabel(App app, int x, int y, int w, int hgt, string name)

- static void TmLayoutPaint(App app, ChartSeries s, ChartNode self, List<ChartNode> nodes, int depth, int rx, int ry, int rw, int rh, int desigColor, int desigSat, List<TmRect> rects, Dict <string, int> idmap)

- static void TreemapCell(App app, int x, int y, int w, int h, string name)

- static int TreeDepth(ChartNode n)

- static int SunburstRingCount(ChartNode root)

- static int SunburstRingHeight(int rMax, int rings)

- static int TreeNodeCount(ChartNode n)

- static int TreeNodeIndex(ChartNode root, ChartNode target)

- static bool TreeRoamEnabled(bool roam)

- static int TreeHitRadius(int radius, int zoom, int scale)

- static bool TreemapContains(int px, int py, int x, int y, int w, int h, int radius)

- static int SunburstVisibleEnd(int rawEnd, int sweepMax)

- static void SunburstHit(App app, ChartSeries s, int cx, int cy, int rMax, int rPer, List<ChartNode> nodes, int depth, int a0, int a1, int sweepMax, int px, int py)

- static SunBand SunBandOf(App app, ChartSeries s, int depth, int rMax, int rPer)

- static int SunUpright(int angle)

- static void SunLabel(App app, ChartSeries s, ChartNode node, int cx, int cy, SunBand band, int a0, int a1, int depth)

- static void DrawSunburst(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void SunRingList(App app, ChartSeries s, int cx, int cy, int rMax, int rPer, List<ChartNode> nodes, int depth, int a0, int a1, int sweepMax, int parentCol)

- static int SankeyIndexOf(List<SankeyNodeLayout> nodes, string name)

- static int SankeyRevealAlpha(int progress)

- static void DrawSankey(App app, int x, int y, int w, int h, ChartOption o, int g)

- static int TreeLeafCount(ChartNode n)

- static bool TreeExpanded(ChartNode n, int depth, int effDepth)

- static int TreeVisLeafCount(ChartNode n, int effDepth, int depth)

- static int TreeVisDepth(ChartNode n, int effDepth, int depth)

- static int TreeExpandDepth(ChartOption o, int fullDepth)

- static void TreeAssign(ChartNode n, int lo, int depth, List<TreeLayoutNode> layout)

- static void TreeAssignEx(ChartNode n, int lo, int depth, int effDepth, List<TreeLayoutNode> layout)

- static int TreeSpan(TreeLayoutNode node, int spanG, int leaves)

- static int TreeLabelWidth(ChartNode n, int fs)

- static void TreeEdgePoints(List<int> xs, List<int> ys, int p0, int p1, int q0, int q1, bool horiz, string type)

- static bool WordCloudFits(List<WordCloudItem> placed, int x, int y, int w, int h)

- static void DrawWordCloud(App app, int x, int y, int w, int h, ChartOption o, int g)

- static List<WordCloudItem> PlaceWordCloud(App app, Theme t, List<ChartData> src, int aw, int ah, int minFs, int maxFs, int maxV, bool vary, string rotList)

- static int WordCloudFingerprint(List<ChartData> data, int aw, int ah, int minFs, int maxFs, int rotate)

- static int WordCloudRotListKey(string rotList)

- static List<WordCloudCacheEntry> wcCache;

- static int TreeGridFirst(int origin, int view0, int gs)

- static void DrawTreeCanvasGrid(App app, Canvas c, int ax, int ay, int aw, int ah, int panX, int panY, int zoom)

- static void DrawTree(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void BuildPath(List<int> px, List<int> py, bool smooth, List<int> ox, List<int> oy)

- static void BuildPathFx(List<int> px, List<int> py, bool smooth, List<int> ox, List<int> oy)

- static void BuildPathFxEx(List<int> px, List<int> py, bool smooth, bool inFx, List<int> ox, List<int> oy)

- static long ISqrt(long v)

- static long[]MonoTangents(List<int> v, int n)

- static void FillUnder(Canvas c, List<int> ox, List<int> oy, int baseY, int color, bool gradient, int topA, int botA)

- static void FillColumnAA(Canvas c, int px, int yyF, int baseY, int color, bool gradient, int topA, int botA, int solidA)

- static void FillBandColumnAA(Canvas c, int px, int topF, int botF, int color, bool gradient, int topA, int botA, int solidA)

- static void FillBandFx(Canvas c, List<int> topX, List<int> topY, List<int> botX, List<int> botY, int color, bool gradient, int topA, int botA, int solidA)

- static void PolyLine(Canvas c, List<int> px, List<int> py, bool smooth, int color, int th)

- static bool ThinSymbolsFor(ChartOption o, ChartSeries s, ChartFrame f, List<int> symTicks, int n, int i0, App app)

- static void OfferCategoryOverlayHits(App app, ChartFrame f, List<string> labels, List<ChartSeries> series, int baseIdx, int g)

- static void DrawMultiGridPanels(App app, int x, int y, int w, int h, ChartOption o, int g)

- static int sPanelFg(App app)

- static void DrawLines(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void OfferStackedAreaHits(App app, ChartFrame f, List<string> labels, List<ChartSeries> series, int baseIdx, int g, int maxV)

- static void DrawStackedArea(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static int MapRamp(ChartOption o, int value, int vMin, int vMax)

- static long FloordivLong(long a, long b)

- static void FillPolygon(Canvas c, List<ChartMapPoint> poly, int color)

- static void PolyOutline(Canvas c, List<ChartMapPoint> poly, int color)

- static void PolyOutlineW(Canvas c, List<ChartMapPoint> poly, int color, int thickness)

- static ChartTextStyle GeoLabelStyle(ChartOption o, ChartSeries lead)

- static void DrawMap(App app, int x, int y, int w, int h, ChartOption o, int g)

- static int GeoProjectX(MapLayout l, int lngQ)

- static int GeoProjectY(MapLayout l, int latQ)

- static int ProjX(MapLayout l, int lngQ, bool svgUnits)

- static int ProjY(MapLayout l, int latQ, bool svgUnits)

- static void DrawGeoOverlay(App app, ChartOption o, MapLayout layout, int plotX, int plotY, int plotW, int plotH, bool svgUnits, int g)

- static void DrawMapMark(App app, Canvas c, int px, int py, string txt, int fg, int fs, int clipX, int clipY, int clipW, int clipH)


## ChartView (class)

- static int MatSide(ChartBoxParam p, int full, int dfltPct)

- static List<double> MatUnitSizes(int bandCount, bool bandShow, ChartBoxParam levelSize, int leafCount, List<ChartBoxParam> sizes, int total)

- static List<int> MatStartsOf(int origin, int total, List<double> ws)

- static int MatXAt(MatGeom geo, int k)

- static int MatYAt(MatGeom geo, int k)

- static void MatXCellRect(MatGeom geo, ChartMatrixSpec m, int ci, out int cx, out int cy, out int cw, out int ch)

- static void MatYCellRect(MatGeom geo, ChartMatrixSpec m, int ri, out int cx, out int cy, out int cw, out int ch)

- static void MatBodyRect(MatGeom geo, int i, int j, out int bx, out int by, out int bw, out int bh)

- static MatGeom MatLayout(App app, int x, int y, int w, int h, int top, ChartOption o, double eLo, double eHi)

- static int MatTextFold(int f, string s)

- static void MatDrawLinesCentered(Canvas c, int x, int y, int w, int h, string text, int col, int font, App app)

- static int MatBoxFold(ChartBoxParam p)

- static void DrawMatrix(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawMatrixBody(App app, ChartOption o, ChartMatrixSpec m, MatGeom geo, int g)

- static void MatLegendDraw(App app, ChartOption o, MatGeom geo)

- static void DrawMatrixPies(App app, ChartOption o, ChartMatrixSpec m, MatGeom geo)

- static int MatNodeIndex(ChartSeries s, string key)

- static void DrawMatrixGraph(App app, ChartOption o, ChartMatrixSpec m, MatGeom geo, ChartSeries s, int si)

- static ChartOption matMemoOpt;

- static int matMemoX;

- static int matMemoY;

- static int matMemoW;

- static int matMemoH;

- static int matMemoScale;

- static MatGeom matMemo;

- static void MatMemoStore(App app, ChartOption o, int x, int y, int w, int h, MatGeom geo)

- static MatGeom MatMemoLoad(App app, ChartOption o, int x, int y, int w, int h)

- static void MatrixHover(App app, int x, int y, int w, int h, ChartOption o)


## ChartView (class)

- static List<ParCategoryLookupItem> parCatCache;

- static List<string> parCatSrcCats;

- static string parCatSrcId;

- static List<int> ParCatLookup(ChartOption o)

- static int ParRowColor(ChartOption o, List<int> catCols, int baseCol, int rowIdx, List<int> row)

- static int parProgFp;

- static bool parProgHas;

- static int parProgDone;

- static int parProgSlot=-1;

- static bool parProgFresh;

- static int ParHash(int h, int v)

- static void DrawParallel(App app, int x, int y, int w, int h, ChartOption o, int g)

- static int di2(int d, List<int> row, int dims)

- static void DrawThemeRiver(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawLinesCustom(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void DrawPictorialBar(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void Symbol(App app, Canvas c, ChartFrame f, ChartSeries s, int sx, int yBase, int sw, int hVal, int col, int colA, int v)

- static void DrawSource(App app, Canvas c, ChartSeries s, string sym, int dx, int dy, int dw, int dh, int fullH, int col, int colA, bool clip)

- static void Poly(Canvas c, int dx, int dy, int dw, int dh, int col, int[]uv)

- static void DrawPath(Canvas c, string dd, int dx, int dy, int dw, int dh, int colA, int col)

- static void DrawImageUri(Canvas c, string uri, int dx, int dy, int dw, int dh, int fullH, bool clip)

- static int Fingerprint(string data)

- static byte[]B64Decode(string s)


## ChartView (class)

- static int PieSelectedOffset(int raw)

- static int ScalarRatioPromille(ChartScalar a, ChartScalar b)

- static bool PieHasEmphasisPaint(int fill, int borderColor, int borderWidth)

- static void PieRingBounds(PieLayout layout, int ring, int ringCount, int rOuter, int band, int minGap, out int ri, out int ro)

- static int PieSliceRo(int ri, int ro, int roseP)

- static void DrawPie(App app, int x, int y, int w, int h, ChartOption o, bool sliceLabels, int g)

- static int GaugeSweep(GaugeOption gauge)

- static int GaugeStart(GaugeOption gauge)

- static int GaugeScalar(App app, ChartScalar scalar, int extent)

- static int GaugeX(int angle)

- static int GaugeY(int angle)

- static void GaugeSector(Canvas c, int cx, int cy, int inner, int outer, int start, int sweep, int color)

- static int GaugeColor(GaugeOption gauge, int perMille)

- static int GaugeFont(App app, int requested, int fallback, int radius, int permilleOfRadius)

- static void DrawGauge(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawRadialBars(App app, int x, int y, int w, int h, ChartOption o, int g)

- static List<ChartStage> GatherStages(App app, List<ChartSeries> series)

- static void DrawFunnel(App app, int x, int y, int w, int h, ChartOption o, int g, bool pyramid)

- static string StageName(int i)

- static string FunnelPct(List<ChartStage> stages, int idx)

- static int DrawStageLegendWrap(App app, ChartOption o, int x, int y, int w, int top)

- static void DrawStageLegend(App app, ChartOption o, int lx, int ly, int legendW)

- static ChartTextStyle PieSliceRichStyle(ChartSeries s, PieSlice sl)

- static string PieSliceRaw(ChartSeries s, PieSlice sl, int pct)

- static string PieLabelFmt(ChartSeries s, string name, int val, int pct)

- static string PieStripRich(string f)

- static void DrawPieMark(App app, Canvas c, int cx, int cy, int rOuter, int rot, List<PieSlice> slices, int idx, string lbl, Theme t, List<ChartSeries> series)


## ChartView (class)

- static int RadarAxisMax(List<ChartSeries> series, List<int> radarMax, int j)

- static int RadarAxisAt(int angle, int n)

- static int RadarTooltipRadius(int radius, int progress, int tolerance)

- static void DrawPolar(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawPolarMulti(App app, int x, int y, int w, int h, ChartOption o, int g)

- static PolarAxisSpec PolarAxisOf(List<PolarAxisSpec> list)

- static PolarAxisSpec PolarAxisFor(List<PolarAxisSpec> list, int pi)

- static string PolarTickText(int v)

- static int PolarScreenX(int cx, int rr, int mathA)

- static int PolarScreenY(int cy, int rr, int mathA)

- static int SinDegX10(int degX10)

- static int CosDegX10(int degX10)

- static void DrawPolarCoord(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static int PolarAngleAt(int startA, int spanX10, double frac)

- static bool PolarHasBars(ChartOption o)

- static bool PolarHasBarsAt(ChartOption o, int pi)

- static void PolarBarsValueExtent(ChartOption o, bool isAngle, out double lo, out double hi)

- static void PolarBarsValueExtentAt(ChartOption o, bool isAngle, int pi, out double lo, out double hi)

- static double PolarAngleSpan(int startAngle, int endAngle, bool endAngleSet, bool clockwise)

- static double PolarCatFrac(int i, int n, bool onBand)

- static double PolarCatSpan(double span, int n, bool onBand, bool clockwise)

- static double PolarLinear(double v, double lo, double hi, double p0, double p1)

- static double PolarCatCoord(int i, int n, bool onBand, double p0, double p1)

- static List <List<double>> PolarStackTops(List<ChartSeries> bars, List<string> stackIds, int n)

- static void PolarValueExtent(List<ChartSeries> bars, List<string> stackIds, int n, bool isAngle, bool minSet, bool maxSet, double minV, double maxV, out double lo, out double hi)

- static void PolarFillBar(Canvas c, int cx, int cy, int r0, int r, int a0X10, int a1X10, bool valueIsRadius, bool roundCap, int color)

- static List <List<ChartPolarBarLayout>> PolarBarSolve(List<ChartSeries> bars, List<int> siOf, ChartPolarBarFrame f, out bool valueIsRadius)

- static void DrawPolarBarSeries(App app, Canvas c, ChartOption o, List<ChartSeries> bars, List<int> siOf, ChartPolarBarFrame f, int g)


## ChartView (class)

- static List<ForceLayoutCache> forceCache;

- static int ForceFingerprint(List<ChartLink> links, int r, int w, int h, int scaling, int gravity)

- static ForceLayout CachedForceLayout(List<ChartLink> links, int r, int w, int h, int scaling, int gravity)

- static ForceLayout CachedForceLayoutK(List<ChartLink> links, int r, int w, int h, int scaling, int gravity, int repulsion, int edgeLength)

- static void DrawChord(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void AppendArcPts(List<int> px, List<int> py, int cx, int cy, int r, int a0, int a1)

- static int ChordNodeColor(ChartSeries s, Theme t, int i, string name)

- static int GraphNodeColor(ChartSeries s, Theme t, int i)

- static void DrawForceArrow(Canvas c, int ax, int ay, int bx, int by, int nodeR, int col)

- static void AppendQuadPts(List<int> px, List<int> py, int x1, int y1, int cx, int cy, int x2, int y2)

- static void StrokeQuad(Canvas c, int x1, int y1, int cx, int cy, int x2, int y2, int color, int th)

- static void FillChordRibbon(Canvas c, int cx, int cy, int r, int sa0, int sa1, int da0, int da1, int color, int alpha)

- static bool InSweep(int ang, int a0, int a1)

- static void DrawForce(App app, int x, int y, int w, int h, ChartOption o, int g)


## ChartView (class)

- static void ScatterSpan(List<ChartSeries> series, int i0, int i1, out int minX, out int maxX, out int minY, out int maxY, out int fMnX, out int fMxX, out int fMnY, out int fMxY)

- static void PinDeclaredF(ChartAxis ax, ChartSpan sp)

- static ChartSpan ScatterRangeX(ChartOption o, List<ChartSeries> series, int i0, int i1)

- static ChartSpan ScatterRangeY(ChartOption o, List<ChartSeries> series, int i0, int i1)

- static void DrawScatter(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void DrawScatterCore(App app, int x, int y, int w, int h, ChartOption o, int g)

- static void ScatterHover(App app, int x, int y, int w, int h, ChartOption o)


## ChartView (class)

- static int PanelHead(App app, int x, int y, int w, int h, string title)

- static void DrawToolbox(App app, int x, int y, int w, int h, int wid, ChartOption o)

- static void ToolboxRestore(App app, int wid, ChartOption o)

- static void DrawDataView(App app, int x, int y, int w, int h, int wid, ChartOption o)

- static string OptionTsv(ChartOption o)

- static void FlushToolboxSave(App app, int wid)

- static bool tbSavePending;

- static int tbSaveWid;

- static int tbSaveX;

- static int tbSaveY;

- static int tbSaveW;

- static int tbSaveH;

- static void QueueToolboxSave(int wid, int x, int y, int w, int h)

- static int WheelTicks(App app)

- static int ZoomByTicks(int zoom, int ticks, int lo, int hi)

- static int ClampPan(int pan, int content, int view)

- static void CartesianTooltip(App app, ChartFrame f, List<string> labels, List<ChartSeries> series, int maxV, int step, int half, int baseIdx)

- static void StrokeSector(Canvas c, int cx, int cy, int ri, int ro, int a0, int a1, int color, int w)

- static void StrokeRect(Canvas c, int x, int y, int w, int h, int color, int t2)

- static List<TooltipRow> tipRows;

- static int tipPx;

- static int tipPy;

- static int tipRight;

- static void TooltipCard(App app, int px, int py, int maxRight, List<TooltipRow> rows)

- static void FlushTooltips(App app)

- static int SeriesMaxIndex(ChartSeries s, int i0, int i1)

- static int SeriesMinIndex(ChartSeries s, int i0, int i1)

- static int SeriesAverage(ChartSeries s, int i0, int i1)

- static int SeriesAvg1000(ChartSeries s, int i0, int i1)

- static string Avg1000Text(int avg1000)

- static int AverageY(ChartFrame f, ChartSeries s, int i0, int i1)

- static int MarkLineValue(ChartMarkLine m, ChartSeries s, int i0, int i1)

- static void DrawValueXMarkLines(App app, ChartFrame f, ChartSeries s, int i0, int i1, int nCat)

- static void DrawMarkAreaBands(App app, ChartFrame f, ChartOption o, int i0, int nCat, int seriesColor)

- static void DrawPointsMarkPoints(App app, ChartFrame f, ChartSeries s, int i0, int i1)

- static string LabelText(ChartSeries s, int di, string cat, int v)

- static void PointsHover(App app, ChartFrame f, List<ChartSeries> series, int i0, int i1, int g, bool cross)

- static bool VisualStrokeOn(ChartOption o, int seriesIndex, ChartSeries s)

- static bool VisualPiecewiseOn(ChartOption o, int seriesIndex)

- static void DrawMarkArrow(App app, Canvas c, int ex, int ey, int col, bool horizontal)

- static void DrawSeriesMarkLines(App app, ChartFrame f, ChartSeries s, int i0, int i1, ChartOption o)

- static void DrawHBarMarkLines(App app, int plotX, int plotY, int plotW, int plotH, int lo, int hi, ChartSeries s, int i0, int i1)

- static int BarMarkX(ChartFrame f, int barX, int slot, double band)

- static void DrawSeriesMarks(App app, ChartFrame f, ChartSeries s, int i0, int i1, double band, int barX, ChartOption o)

- static void DrawSeriesMarksH(App app, ChartSeries s, int baseIdx, int n, double band, ChartBarCol hbc, int plotY, int plotX, int plotW, int lo, int hi)

- static void MarkAt(App app, ChartFrame f, ChartSeries s, int idx, int slot, int nWin, string lbl, bool below, int colorOv, string symbolOv, int sizeOv, int axOv)

- static void MarkLabelAt(App app, Canvas c, ChartFrame f, int lx, int ly, int rad, string lbl, bool below)

- static int AngleOf(int dx, int dy)

- static int DistToSeg(int px, int py, int x0, int y0, int x1, int y1)

- static void DrawDashed(Canvas c, List<int> px, List<int> py, int color, int width, int dash, int gap)

- static void FillPolyAlpha(Canvas c, List<int> px, List<int> py, int color, int alpha)

- static void FillSpanAlpha(Canvas c, int xF0, int xF1, int y, int color, int alpha)

- static void FillPolyWinding(Canvas c, List<int> px, List<int> py, int color, int alpha)

- static int ISqrt(int v)

- static int SinDeg(int deg)

- static int CosDeg(int deg)

- static int RadialRadius(int areaW, int areaH, int pct)

- static void StrokePolyFx(Canvas c, List<int> px, List<int> py, int color, int th)

- static int FillDx(int a)

- static int FillDy(int a)

- static int FillX(int cx, int r, int a)

- static int FillY(int cy, int r, int a)

- static void PushPlotClip(App app, int plotX, int plotY, int plotW, int plotH)

- static void DrawTextClamped(Canvas c, int x, int y, string text, int color, int fs, int clipX, int clipY, int clipW, int clipH)

- static void DrawPolarLabel(App app, Canvas c, int cx, int cy, int r, int ang, string lbl, int color, int fs, int boxX, int boxY, int boxW, int boxH)

- static void RingOutline(Canvas c, int cx, int cy, int r, int color)


## ChartView (class)

- static void DrawVenn(App app, int x, int y, int w, int h, ChartOption o, int g)

- static string VennName(ChartSeries s, int i, string fallback)

- static int VennRadius(int areaW, int areaH, int minGap)

- static void VennLabel(Canvas c, int ax, int ay, int fh, int fs, string name, int value, int color)

- static bool InCircle(int mx, int my, VennCircle v)


## ChartViewDataRange (class)

- static int Pad(App app)

- static int Height(App app, ChartOption o)

- static void DataSpan(ChartOption o, out int lo, out int hi)

- static void Draw(App app, int x, int y, int w, int h, ChartOption o)

- static bool HoverHighlight(App app, ChartOption o, int vMin, int vMax, int v)

- static void DrawSplits(App app, Canvas c, int x, int y, int w, ChartOption o, int fs, int fg)


## ChartVisualMap (class)

- int type;

- bool show;

- bool hasMin;

- bool hasMax;

- int minV;

- int maxV;

- int dimension;

- int seriesIndex;

- List<int> rangeColors;

- bool colorsExplicit;

- int minRaw;

- int maxRaw;

- List<ChartVisualPiece> pieces;

- List<string> vmCats;

- int inOpacity;

- int sizeLo;

- int sizeHi;

- bool hasSize;

- int splitNumber;

- static ChartVisualMap Of()

- static ChartVisualMap Clone(ChartVisualMap src)

- static int Lerp(List<int> stops, int tF)


## ChartVisualPiece (class)

- bool hasLo;

- int lo;

- bool loIncl;

- bool hasHi;

- int hi;

- bool hiIncl;

- bool hasEq;

- int eq;

- int color;

- string label;

- static ChartVisualPiece Of()

- bool Hit(int probe)


## ChartZoom (class)

- bool slider;

- bool show;

- bool showDetail;

- bool zoomLock;

- bool brushSelect;

- int start;

- int end;

- int axisIndex;

- bool isY;

- int shadowMode;

- int handleSize;

- int handleSizePx;

- int minValueSpan;

- int minSpan;

- int height;

- int heightPct;

- int width;

- int widthPct;

- int top;

- int topPct;

- int bottom;

- int bottomPct;

- int left;

- int leftPct;

- int right;

- int rightPct;

- int borderColor;

- int fillerColor;

- int textStyleColor;

- int dbAreaColor;

- int dbLineColor;

- int selDbAreaColor;

- int selDbLineColor;

- string startValue;

- string endValue;

- string labelFormatter;

- string handleIcon;

- static ChartZoom Of()

- static ChartZoom Clone(ChartZoom src)


## ChordLayout (class)

- List<ChordNodeArc> nodes;

- List<ChordRibbon> ribbons;

- int gap;

- int arcTotal;

- static ChordLayout Of(List<ChartLink> links, int sortMode, bool sortSub, int gapDeg)

- static int OtherIndex(List<ChordNodeArc> nodes, ChartLink lk, int self)

- static void SortInc(List<int> inc, List<ChartLink> links, List<ChordNodeArc> nodes, int self, bool byValue)

- static int NodeIndex(List<ChordNodeArc> nodes, string name)

- static int Pos(List<int> list, int v)


## ChordNodeArc (class)

- string name;

- int weight;

- int a0;

- int a1;

- static ChordNodeArc Of(string name, int weight, int a0, int a1)


## ChordRibbon (class)

- int from;

- int to;

- int val;

- int srcAngle;

- int dstAngle;

- int srcA0;

- int srcA1;

- int dstA0;

- int dstA1;

- static ChordRibbon Of(int from, int to, int val, int srcAngle, int dstAngle)

- static ChordRibbon OfSpan(int from, int to, int val, int srcAngle, int dstAngle, int srcA0, int srcA1, int dstA0, int dstA1)


## ErrorItem (class)

- string label;

- int y;

- int err;

- static ErrorItem Of(string label, int y, int err)

- static ErrorItem Clone(ErrorItem src)


## EventBand (class)

- string name;

- int start;

- int end;

- int value;

- int x0;

- int x1;

- int y;

- int h;

- int row;

- int src;

- static EventBand Of(string name, int start, int end, int value, int x0, int x1, int y, int h, int row, int src)


## EventRiverLayout (class)

- int t0;

- int t1;

- int span;

- int rowCount;

- List<EventBand> bands;

- static EventRiverLayout Of(List<ChartEvent> events, int x, int y, int w, int h, int pad)

- static bool Less(List<ChartEvent> evs, int i, int j)


## ForceEdge (class)

- int a;

- int b;

- int val;

- static ForceEdge Of(int a, int b, int val)


## ForceLayout (class)

- List<ForceNode> nodes;

- List<ForceEdge> edges;

- int iterations;

- List<ChartNodeCoord> startCoords;

- List<int> startX;

- List<int> startY;

- static ForceLayout Of(List<ChartLink> links, int r, int w, int h, int scaling, int gravity)

- static ForceLayout OfK(List<ChartLink> links, int r, int w, int h, int scaling, int gravity, int repulsion, int edgeLength)

- static int NodeIndex(List<ForceNode> nodes, string name)

- static ForceLayout Build(List<ChartLink> links, List<string> nodeNames)

- static ForceLayout CircularOf(List<ChartLink> links, List<string> nodeNames, int w, int h)

- static ForceLayout FixedOfCoords(List<ChartLink> links, List<string> nodeNames, List<ChartNodeCoord> coords, int w, int h)

- static ForceLayout FixedOf(List<ChartLink> links, List<string> nodeNames, List<int> xs, List<int> ys, int w, int h)

- static void ApplyShapes(ForceLayout l, List<ChartNode> shapes, int minR, int maxR)

- static int EdgeIndex(List<ForceEdge> edges, int a, int b)


## ForceLayoutCache (class)

- int fp;

- ForceLayout layout;

- static ForceLayout Lookup(List<ForceLayoutCache> cache, int fp)

- static void Store(List<ForceLayoutCache> cache, int fp, ForceLayout l, int capacity)


## ForceNode (class)

- string name;

- int x;

- int y;

- int weight;

- string symbol;

- int size;

- static ForceNode Of(string name, int x, int y, int weight)


## GaugeAxisLabel (class)

- bool show;

- string formatter;

- ChartTextStyle textStyle;

- static GaugeAxisLabel Create()

- static GaugeAxisLabel Clone(GaugeAxisLabel src)


## GaugeAxisLine (class)

- bool show;

- ChartLineStyle lineStyle;

- List<GaugeColorStop> colors;

- static GaugeAxisLine Create()

- static GaugeAxisLine Clone(GaugeAxisLine src)


## GaugeAxisTick (class)

- bool show;

- int splitNumber;

- int length;

- ChartLineStyle lineStyle;

- static GaugeAxisTick Create()

- static GaugeAxisTick Clone(GaugeAxisTick src)


## GaugeColorStop (class)

- int stop;

- int color;

- static GaugeColorStop Of(int stop, int color)

- static GaugeColorStop Clone(GaugeColorStop src)


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

- static GaugeDetail Clone(GaugeDetail src)


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

- static GaugeOption Clone(GaugeOption src)


## GaugePointer (class)

- ChartScalar length;

- int width;

- int color;

- int shadowColor;

- int shadowBlur;

- static GaugePointer Create()

- static GaugePointer Clone(GaugePointer src)


## GaugeSplitLine (class)

- bool show;

- int length;

- ChartLineStyle lineStyle;

- static GaugeSplitLine Create()

- static GaugeSplitLine Clone(GaugeSplitLine src)


## GaugeTitle (class)

- bool show;

- ChartPosition offsetCenter;

- ChartTextStyle textStyle;

- static GaugeTitle Create()

- static GaugeTitle Clone(GaugeTitle src)


## HeatGridGeom (class)

- int gx;

- int gy;

- int gw;

- int gh;

- int cols;

- int rows;

- List<string> xCats;

- List<string> yCats;

- List<int> ramp;

- double dLo;

- double dHi;

- int split;

- static HeatGridGeom Of()


## LineEndLabelItem (class)

- int x;

- int y;

- int color;

- string text;

- ChartSeries series;

- LineEndLabelItem(int x, int y, int color, string text, ChartSeries series)


## MapLayout (class)

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

- static MapLayout OfRoam(List<ChartMapRegion> regions, int x, int y, int w, int h, int pad, int zoom, int panX, int panY)

- static MapLayout OfRoamBox(int mnX, int mnY, int mxX, int mxY, List<ChartMapRegion> regions, int x, int y, int w, int h, int pad, int zoom, int panX, int panY)

- static int RegionAt(MapLayout l, int px, int py)


## MapPolygon (class)

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


## MatGeom (class)

- int rx;

- int ry;

- int rw;

- int rh;

- int xBands;

- int yBands;

- int xLeaves;

- int yLeaves;

- int unitsX;

- int unitsY;

- List<int> xStarts;

- List<int> yStarts;

- int bodyL;

- int bodyT;

- int panelX;

- int panelY;

- int panelW;

- int panelH;

- List<int> ramp;

- double dLo;

- double dHi;

- int split;

- static MatGeom Of()


## ParCategoryLookupItem (class)

- string name;

- int col;

- ParCategoryLookupItem(string name, int col)


## PieLayout (class)

- List<PieSlice> slices;

- int total;

- long totalWide;

- int ringCount;

- List<PieRingMeta> rings;

- List<int> ringTotal;

- List<long> ringTotalWide;

- List<int> ringInner;

- List<ChartPosition> ringCenter;

- List<ChartScalar> ringRadius;

- List<ChartScalar> ringRadiusIn;

- int rot;

- static PieLayout Of(Theme t, ChartOption o)

- static int ClampInt(long v)

- static int GlobalColorIndex(ChartOption o, string nm, int fallback)

- static int SlicePalIndex(ChartOption o, ChartSeries s, int di, int fallback)

- static int SliceColor(Theme t, ChartSeries s, int di, int sliceIndex)

- static string SliceName(ChartSeries s, int di)


## PieLegendItem (class)

- int seriesIndex;

- int dataIndex;

- string name;

- PieLegendItem(int seriesIndex, int dataIndex, string name)


## PieOuterLabelEntry (class)

- int x1;

- int y1;

- int x2;

- int x3;

- int tx;

- int yc;

- int side;

- int color;

- int lineColor;

- bool drawLine;

- string text;

- RichBlock block;

- int height;

- PieOuterLabelEntry(int x1, int y1, int x2, int x3, int tx, int yc, int side, int color, int lineColor, bool drawLine, string text, RichBlock block, int height)


## PieRingMeta (class)

- public int total;

- public long totalWide;

- public int innerPct;

- public ChartPosition center;

- public ChartScalar radius;

- public ChartScalar radiusInner;

- PieRingMeta()


## PieSlice (class)

- int value;

- int color;

- string name;

- int seriesIndex;

- int dataIndex;

- int a0;

- int a1;

- int ring;

- bool selected;

- int roseP;

- static PieSlice Of(int value, int color, string name, int seriesIndex, int dataIndex, int a0, int a1, int ring)


## PolarAxisSpec (class)

- bool isAngle;

- ChartAxisType type;

- int polarIndex;

- int min;

- int max;

- bool minSet;

- bool maxSet;

- int startAngle;

- int endAngle;

- bool endAngleSet;

- bool clockwise;

- bool boundaryGap;

- List<string> cats;

- static PolarAxisSpec Of(bool angle)

- static PolarAxisSpec Clone(PolarAxisSpec src)


## PolarNameStyle (class)

- int color;

- string format;

- PolarNameStyle(int color, string format)


## RadarIndicator (class)

- public string text;

- public string name;

- public int max;

- public RadarIndicator(string text, int max)

- public static RadarIndicator Of(string text, int max)


## RadarPolar (class)

- List<RadarIndicator> indicators;

- ChartPosition center;

- ChartScalar radius;

- ChartScalar radiusInner;

- int startAngle;

- int splitNumber;

- string type;

- int nameColor;

- string nameFormat;

- int axisLineColor;

- int axisLineWidth;

- int splitLineColor;

- int splitLineWidth;

- List<int> splitAreaColors;

- static RadarPolar Create()

- static RadarPolar Clone(RadarPolar src)


## ResolvedChart (class)

- ChartOption source;

- List<ResolvedSeries> series;

- int wid;

- int stateWid;

- bool animate;

- int windowStart;

- int windowEnd;

- static ResolvedChart Resolve(ChartOption source, Theme theme, App app, int wid)

- ResolvedSeries For(ChartSeries source)

- List<ChartSeries> MaterializeSeries()

- ChartOption DrawOption(int wid, bool animate)


## ResolvedSeries (class)

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

- bool visBlocked;

- static ResolvedSeries Of(ChartSeries source, Theme theme, int index, int wid, bool animate, bool hidden, List<int> pal)

- int DataColor(int index)

- void OverrideColor(int cssColor)

- int EffectiveColor(bool hovered)

- int EffectiveBorderColor(bool hovered)

- int EffectiveBorderWidth(bool hovered)

- ChartSeries Materialize(bool allowShare)


## RichBlock (class)

- List<RichSeg> segs;

- ChartTextStyle st;

- int w;

- int h;

- int lines;


## RichSeg (class)

- string text;

- int line;

- ChartRichStyle st;

- int x;

- int y;

- int w;

- int h;

- int fsPx;

- int tw;

- int lh;

- static RichSeg Create()


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

- List<ChartSeries> series;

- static SeriesListSource Of(List<ChartSeries> series)

- override int SeriesCount()

- override string SeriesName(int s)

- override int SeriesColor(int s)

- override int PointCount(int s)

- override int ValueAt(int s, int i)


## SunBand (class)

- int rStart;

- int rEnd;


## TmBox (class)

- double x;

- double y;

- double w;

- double h;


## TmRect (class)

- ChartNode node;

- int depth;

- int x;

- int y;

- int w;

- int h;

- static TmRect Of(ChartNode node, int depth, int x, int y, int w, int h)


## TmStyle (class)

- int borderWidth;

- int gapWidth;

- int borderColor;

- int borderColorSat;

- int hasColorSat;

- int satLo;

- int satHi;

- int upperShow;

- int upperHeight;

- List<int> color;

- int colorMappingBy;


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

- string name;

- int value;

- int cx;

- int cy;

- int r;

- static VennCircle Of(string name, int value, int cx, int cy, int r)


## VennLayout (class)

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


## WordCloudCacheEntry (class)

- int fp;

- List<WordCloudItem> items;

- static WordCloudCacheEntry Lookup(List<WordCloudCacheEntry> cache, int fp)

- static void Store(List<WordCloudCacheEntry> cache, int fp, List<WordCloudItem> items, int capacity)


## WordCloudItem (class)

- ChartData data;

- int x;

- int y;

- int w;

- int h;

- int fontSize;

- int color;

- int rot;

- static WordCloudItem Of(ChartData data, int x, int y, int w, int h, int fontSize, int color, int rot)


## void (delegate)

`delegate void ChartInteractionHandler(ChartInteractionEvent e);`


## ChartAxisType (enum)

- Category

- Value

- Time

- Log


## ChartElementType (enum)

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


## ChartEventType (enum)

- Click

- DoubleClick

- Hover

- MouseOut

- LegendSelected

- PieSelected

- MapSelected

- MapRoam

- DataZoom

- DataRange

- TimelineChanged

- MagicTypeChanged

- Restore

- Resize

- DataChanged

- Refresh

- ForceLayoutEnd


## ChartType (enum)

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
