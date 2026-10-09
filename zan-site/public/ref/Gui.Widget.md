# Gui.Widget

> 源码: `packages/Zan.Gui.CodeEditor/src/Gui/Widget/CodeBlock.zan`, `packages/Zan.Gui/src/Gui/Widget/AlertBox.zan`, `packages/Zan.Gui/src/Gui/Widget/Avatar.zan`, `packages/Zan.Gui/src/Gui/Widget/Badge.zan`, `packages/Zan.Gui/src/Gui/Widget/BoxContent.zan`, `packages/Zan.Gui/src/Gui/Widget/Breadcrumb.zan`, `packages/Zan.Gui/src/Gui/Widget/Button.zan`, `packages/Zan.Gui/src/Gui/Widget/ButtonGroup.zan`, `packages/Zan.Gui/src/Gui/Widget/Calendar.zan`, `packages/Zan.Gui/src/Gui/Widget/Card.zan`, `packages/Zan.Gui/src/Gui/Widget/Carousel.zan`, `packages/Zan.Gui/src/Gui/Widget/Checkbox.zan`, `packages/Zan.Gui/src/Gui/Widget/ChoiceGroup.zan`, `packages/Zan.Gui/src/Gui/Widget/Collapse.zan`, `packages/Zan.Gui/src/Gui/Widget/ColorPicker.zan`, `packages/Zan.Gui/src/Gui/Widget/ContextMenu.zan`, `packages/Zan.Gui/src/Gui/Widget/Countdown.zan`, `packages/Zan.Gui/src/Gui/Widget/DatePicker.zan`, `packages/Zan.Gui/src/Gui/Widget/Divider.zan`, `packages/Zan.Gui/src/Gui/Widget/Dropdown.zan`, `packages/Zan.Gui/src/Gui/Widget/DynamicTags.zan`, `packages/Zan.Gui/src/Gui/Widget/Ellipsis.zan`, `packages/Zan.Gui/src/Gui/Widget/Empty.zan`, `packages/Zan.Gui/src/Gui/Widget/Flex.zan`, `packages/Zan.Gui/src/Gui/Widget/FloatButton.zan`, `packages/Zan.Gui/src/Gui/Widget/FormBuilder.zan`, `packages/Zan.Gui/src/Gui/Widget/FormField.zan`, `packages/Zan.Gui/src/Gui/Widget/FormGroup.zan`, `packages/Zan.Gui/src/Gui/Widget/Grid.zan`, `packages/Zan.Gui/src/Gui/Widget/GridItem.zan`, `packages/Zan.Gui/src/Gui/Widget/IconView.zan`, `packages/Zan.Gui/src/Gui/Widget/Image.zan`, `packages/Zan.Gui/src/Gui/Widget/Input.zan`, `packages/Zan.Gui/src/Gui/Widget/InputNumber.zan`, `packages/Zan.Gui/src/Gui/Widget/InputOtp.zan`, `packages/Zan.Gui/src/Gui/Widget/Label.zan`, `packages/Zan.Gui/src/Gui/Widget/Layer.zan`, `packages/Zan.Gui/src/Gui/Widget/ListItem.zan`, `packages/Zan.Gui/src/Gui/Widget/ListView.zan`, `packages/Zan.Gui/src/Gui/Widget/Marquee.zan`, `packages/Zan.Gui/src/Gui/Widget/Menu.zan`, `packages/Zan.Gui/src/Gui/Widget/NumberAnimation.zan`, `packages/Zan.Gui/src/Gui/Widget/PageHeader.zan`, `packages/Zan.Gui/src/Gui/Widget/Pagination.zan`, `packages/Zan.Gui/src/Gui/Widget/Panel.zan`, `packages/Zan.Gui/src/Gui/Widget/Popover.zan`, `packages/Zan.Gui/src/Gui/Widget/Progress.zan`, `packages/Zan.Gui/src/Gui/Widget/Prompt.zan`, `packages/Zan.Gui/src/Gui/Widget/QrCode.zan`, `packages/Zan.Gui/src/Gui/Widget/QrEncoder.zan`, `packages/Zan.Gui/src/Gui/Widget/Radio.zan`, `packages/Zan.Gui/src/Gui/Widget/Rate.zan`, `packages/Zan.Gui/src/Gui/Widget/Result.zan`, `packages/Zan.Gui/src/Gui/Widget/Ribbon.zan`, `packages/Zan.Gui/src/Gui/Widget/RichText.zan`, `packages/Zan.Gui/src/Gui/Widget/ScrollColumn.zan`, `packages/Zan.Gui/src/Gui/Widget/ScrollView.zan`, `packages/Zan.Gui/src/Gui/Widget/Scrollbar.zan`, `packages/Zan.Gui/src/Gui/Widget/SelectBox.zan`, `packages/Zan.Gui/src/Gui/Widget/Skeleton.zan`, `packages/Zan.Gui/src/Gui/Widget/Slider.zan`, `packages/Zan.Gui/src/Gui/Widget/Spin.zan`, `packages/Zan.Gui/src/Gui/Widget/Split.zan`, `packages/Zan.Gui/src/Gui/Widget/SplitPanel.zan`, `packages/Zan.Gui/src/Gui/Widget/Statistic.zan`, `packages/Zan.Gui/src/Gui/Widget/StatusBar.zan`, `packages/Zan.Gui/src/Gui/Widget/Steps.zan`, `packages/Zan.Gui/src/Gui/Widget/StyledText.zan`, `packages/Zan.Gui/src/Gui/Widget/Switch.zan`, `packages/Zan.Gui/src/Gui/Widget/Table.zan`, `packages/Zan.Gui/src/Gui/Widget/Tabs.zan`, `packages/Zan.Gui/src/Gui/Widget/Tag.zan`, `packages/Zan.Gui/src/Gui/Widget/TextArea.zan`, `packages/Zan.Gui/src/Gui/Widget/Timeline.zan`, `packages/Zan.Gui/src/Gui/Widget/ToolStrip.zan`, `packages/Zan.Gui/src/Gui/Widget/Tooltip.zan`, `packages/Zan.Gui/src/Gui/Widget/Transfer.zan`, `packages/Zan.Gui/src/Gui/Widget/TreeView.zan`, `packages/Zan.Gui/src/Gui/Widget/Typography.zan`, `packages/Zan.Gui/src/Gui/Widget/Upload.zan`, `packages/Zan.Gui/src/Gui/Widget/VirtualList.zan`, `packages/Zan.Gui/src/Gui/Widget/Watermark.zan`, `packages/Zan.Gui/src/Gui/Widget/Wizard.zan`


## AlertBox (class)

- Binding<string> Text;

- int typeNo;

- void InitAlertBox(string msg)

- AlertBox()

- AlertBox(string msg)

- string Label()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()


## Avatar (class)

- Binding<string> Text;

- string Icon;

- int Size;

- string Shape;

- void InitAvatar(string label, int size, string icon)

- Avatar()

- Avatar(string label)

- Avatar(string label, int size)

- Avatar(string label, int size, string icon)

- string Label()

- int Diameter(App app)

- void OnMeasure(App app)

- void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()


## Badge (class)

- int Count;

- int Max;

- void InitBadge(int n)

- Badge()

- Badge(int n)

- StyleBox ResolvedStyle(App app)

- string Label()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int WidthOf(App app, string label, string cls)

- static void Pill(App app, int x, int y, string label, string cls)

- static void Dot(App app, int x, int y)

- static void CountAt(App app, int x, int y, int count)

- override string Kind()

- override List<PropSpec> Props()

- override bool SetExtra(string key, string val)


## BoxContent (class)

- static int GlyphSize(int fontPx)

- static int Width(string icon, string label, string trailing, int fontPx, int gap)

- static int StackedWidth(App app, string icon, string label, int fontPx)

- static int StackedHeight(App app, string icon, string label, int fontPx, int gap)

- static void PaintStacked(App app, StyleBox s, StyleBox iconStyle, StyleBox labelStyle, int x, int y, int w, int h, string icon, string label, int fgColor)

- static int Paint(App app, int id, string type, string cls, string name, StyleBox s, int x, int y, int w, int h, string icon, string label, string trailing, bool busy, bool disabled)


## Breadcrumb (class)

- List<string> items;

- string Separator;

- void InitBreadcrumb()

- Breadcrumb()

- void AddItem(string label)

- static Breadcrumb Of(List<string> labels)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)


## Button (class)

- Binding<string> Text;

- string Icon;

- Binding<bool> Checked;

- string Tip;

- string TipSide;

- int wid;

- bool clicked;

- int corners;

- UiEvent Click;

- Binding<int> Color;

- Binding<int> TextColor;

- bool Tertiary;

- bool Quaternary;

- bool Strong;

- string IconPlacement;

- void InitButton(string label)

- Button()

- Button(string label)

- string Label()

- bool IsChecked()

- bool IsToggle()

- bool WasClicked()

- override bool Fired()

- string TipText()

- StyleBox ResolvedStyle(App app)

- int AutoWidth(App app)

- int AutoHeight(App app)

- int Render(App app, int x, int y, int w)

- int RenderIn(App app, int x, int y, int w, int h)

- void EnsureColorInjected(App app)

- static string CustomSelector(int c)

- string AppendCustomClass(string orig, int c)

- string BuildColorCss(int c)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnMeasure(App app)

- override bool LintLeafSize()

- override void OnPaint(App app)


## ButtonGroup (class)

- List<ButtonSegment> segs;

- int btnType;

- int btnStyle;

- int size;

- bool vertical;

- bool barMode;

- List<Button> barBtns;

- int pressed;

- int clicked;

- int active;

- int lineH;

- int lines;

- ButtonGroup(int type)

- ButtonGroup Add(string label)

- int SegCount()

- void ClearSegs()

- ButtonGroup Outline(bool o)

- ButtonGroup Vertical(bool v)

- ButtonGroup SetSize(int s)

- static ButtonGroup Bar(string cls)

- ButtonGroup AddButton(Button b)

- Button ButtonAt(int i)

- int TakePressed()

- int TakeClicked()

- int Active()

- string ActiveLabel()

- void SetActiveIndex(int idx)

- void SetActive(string label)

- string SegLabel(int i)

- List<int> BarWidths(App app)

- int BarGap(App app)

- static int WeldCorners(bool first, bool last)

- override string Kind()

- override List<PropSpec> Props()

- override string StyleType()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- int TypeColor(Theme t)

- int Height(Theme t)

- int FontSz(App app, StyleBox s)

- int Render(App app, int x, int y)

- int SegMaxW(Theme t, int fs)

- void FillSeg(Canvas c, int x, int y, int w, int h, int r, int col, bool first, bool last)

- void StrokeSeg(Canvas c, int x, int y, int w, int h, int r, int col, bool first, bool last, bool vert)


## ButtonSegment (class)

- string label;

- int id;

- ButtonSegment(string label, int id)

- string Label()


## Calendar (class)

- Binding<string> Mode;

- DateTime selected;

- int viewYear;

- int viewMonth;

- UiEvent Change;

- UiEvent PanelChange;

- DateFilter IsDateDisabled;

- List<CalendarMark> marks;

- int baseId;

- bool idsReady;

- Calendar()

- Calendar(DateTime value):this()

- static DateTime DayOf(DateTime t)

- static DateTime TodayRef()

- static int GridStart(int year, int month)

- static DateTime CellDate(int year, int month, int index)

- static string MonthLabel(int year, int month)

- static string ToneClass(int tone)

- DateTime GetDate()

- void SetDate(DateTime value)

- void ClearDate()

- void AddMark(DateTime day, string text, int tone)

- void ClearMarks()

- bool IsDayDisabled(DateTime d)

- Calendar OnChange(Action a)

- Calendar OnPanelChange(Action a)

- void MoveMonth(int delta)

- void JumpToToday()

- void PickDate(DateTime picked)

- void EnsureIds()

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void PaintMonthMode(App app, StyleBox s)

- void PaintPanelMode(App app, StyleBox s)

- int MarkColor(App app, int tone, int fallback)

- void DispatchClick(App app)


## CalendarMark (class)

- DateTime day;

- string text;

- int tone;

- CalendarMark(DateTime d, string t, int tn)


## Card (class)

- string title;

- FxOptions fx;

- bool lifted;

- bool bordered;

- Binding<string> Size;

- int wid;

- void InitCard(string ttl)

- Card WithFx(FxOptions o)

- Card Lift()

- Card(string ttl)

- Card():this("")

- override string Kind()

- override Binding<string> SizeOf()

- override List<PropSpec> Props()

- string FxSpec()

- void SetFxSpec(string text)

- override string GetProp(string key)

- override void SetProp(string key, string val)

- override string StyleType()

- override void OnMeasure(App app)

- override int StylePadT()

- override void OnPaint(App app)

- static int ContentY(int y, Theme t)

- static Rect ContentRect(App app, int x, int y, int w, int h)

- static int FooterHeight(Theme t)

- static Rect BodyRect(App app, int x, int y, int w, int h)

- static Rect FooterRect(App app, int x, int y, int w, int h)


## Carousel (class)

- List<string> slides;

- SignalInt model;

- Binding<int> Index;

- CarouselAnim anim;

- UiEvent Change;

- int baseId;

- int idCount;

- bool autoplay;

- int intervalMs;

- string dotPlacement;

- string direction;

- int lastTickMs;

- CarouselChrome chrome;

- List<Control> slideViews;

- void InitCarousel(SignalInt m)

- Carousel()

- Carousel(SignalInt m)

- bool Vert()

- Carousel Direction(string d)

- void AddSlide(string label)

- void AddSlideView(Control v)

- static Carousel Of(List<string> labels, SignalInt m)

- Carousel Animate(CarouselAnim state)

- void EnsureIds()

- override void SyncBinding()

- void GoTo(App app, int i)

- override void Arrange(int px, int py, int pw, int ph)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void ShowSlides(int from, int to, int shift, int dir)

- void PaintSlide(App app, int x, int y, int i)

- void PaintChrome(App app, int count, int cur)

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## CarouselAnim (class)

- int shown;

- int from;

- int startFrame;

- bool animating;

- int dir;

- int tickMs;

- CarouselAnim()


## CarouselChrome (class)

- Carousel owner;

- void InitChrome(Carousel c)

- CarouselChrome(Carousel c)

- override void OnPaint(App app)


## CascadeColumnScroll (class)

- public int offset;

- public SignalInt signal;

- public CascadeColumnScroll()

- public CascadeColumnScroll(int offset, SignalInt signal)


## Checkbox (class)

- string label;

- SignalBool model;

- Binding<bool> data;

- int wid;

- UiEvent Change;

- Binding<bool> Indeterminate;

- void InitCheckbox(string lbl, SignalBool m)

- Checkbox()

- Checkbox(string lbl)

- static Checkbox Bind(string label, SignalBool model)

- string Str()

- void SetLabel(string v)

- bool IsChecked()

- void SetChecked(bool v)

- void SyncBinding()

- int Render(App app, int x, int y)

- bool IsIndeterminate()

- int RenderIn(App app, int x, int y, int h)

- void Toggle()

- override List<string> Events()

- override string Kind()

- override List<PropSpec> Props()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- int PaintStyled(App app, int id, int x, int y, int h, string text, bool checked)

- int PaintStyled(App app, int id, int x, int y, int h, string text, bool checked, bool indet)

- static void PaintBox(App app, int x, int y, int size, int level)

- static void PaintBoxIndeterminate(App app, int x, int y, int size)

- override bool LintLeafSize()

- override void OnMeasure(App app)

- override void OnPaint(App app)


## CheckboxGroup (class)

- UiEvent Change;

- CheckboxGroup()

- override Control MakeItem(int index, string text)

- override void OnChildEvent(Control child, string evt)

- bool IsChecked(int index)

- void SetChecked(int index, bool v)

- int CheckedCount()

- void SetAllChecked(bool v)

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override List<string> Events()

- override string Kind()


## ChoiceGroup (class)

- int gapX;

- int gapY;

- int columns;

- int rowHeight;

- int sGapX;

- int sGapY;

- int sRowH;

- List<string> labels;

- int mCols;

- int mColGap;

- int mRowGap;

- int mRowH;

- int mRows;

- int aRows;

- void InitChoice()

- void SetColumns(int n)

- int Columns()

- void SetRowHeight(int h)

- int RowHeight()

- override string StyleType()

- virtual Control MakeItem(int index, string text)

- virtual string OptionsText()

- int Count()

- Control AddOption(string text)

- virtual void SetOptionsText(string text)

- override string GetProp(string key)

- virtual void ClearOptions()

- override void SetProp(string key, string val)

- override List<PropSpec> Props()

- override void OnPaint(App app)

- int InnerW(int pw)

- int RowsHeight(int rows)

- int FlowRows(int avail)

- int RowGapPx()

- int ColGapPx()

- void ResolveMetrics()

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)

- void ArrangeItem(Control c, int x, int y, int w, int rowH)


## CodeBlock (class)

- List<string> lines;

- Binding<string> Caption;

- Binding<string> CopyLabel;

- SignalInt scroll;

- bool copied;

- int wid;

- static int selOwner;

- static bool selActive;

- static bool selDragging;

- static int selAnchorLine;

- static int selAnchorCol;

- static int selLine;

- static int selCol;

- UiEvent Copy;

- void InitCodeBlock(string caption, List<string> src)

- CodeBlock()

- CodeBlock(string caption, List<string> src)

- void UseScroll(SignalInt s)

- bool Copied()

- void SetLines(List<string> src)

- static void ClearSelection()

- string AllText()

- string SelectedText(int owner)

- void SelectAll(int owner)

- void CaretFromMouse(App app, int codeX, int topY, int lineH, int off, int font, bool moveAnchor)

- static int LineHeight(App app)

- static int HeaderHeight(App app)

- static int GutterWidth(App app, int lineCount)

- static int SurfaceColor(App app, EditorPalette pal)

- static int Height(App app, int lineCount)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## Collapse (class)

- List<CollapsePanel> panels;

- SignalInt open;

- Binding<int> Active;

- UiEvent Change;

- int baseId;

- int idCount;

- string arrowPlacement;

- bool expander;

- void InitCollapse(SignalInt m)

- Collapse()

- Collapse(SignalInt m)

- void AddPanel(string title, string body)

- void AddPanel(CollapsePanel panel)

- static Collapse Of(List<CollapsePanel> items, SignalInt m)

- static int HeaderHeight(App app)

- static int BodyHeight(App app)

- static int Height(App app, int count, SignalInt openIdx)

- void EnsureIds()

- override void SyncBinding()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## CollapsePanel (class)

- string title;

- string body;

- CollapsePanel(string title, string body)


## ColorPicker (class)

- SignalInt model;

- Binding<int> data;

- int wid;

- UiEvent Change;

- bool open;

- int hue;

- int sat;

- int val;

- int alpha;

- bool showAlpha;

- List<string> modes;

- Binding<string> Mode;

- int dragPart;

- Input inR;

- Input inG;

- Input inB;

- Input inA;

- Input inHex;

- Input inHsl;

- Input inHsb;

- int inputView;

- static int SvPanelOff()

- static int HueOff()

- static int AlphaOff()

- static int Opaque(int r, int g, int b)

- static int RgbOf(int packed)

- static int AlphaOf(int packed)

- static bool SamePacked(int a, int b)

- static int HsvToRgb(int h, int s, int v)

- static int hueOut;

- static int RgbToSv(int rgb)

- static string HexText(int color)

- static string HexTextAlpha(int color)

- static string Hex2(int b)

- static int HexByte(int c)

- static int HexDigit(string ch)

- static bool parseOk;

- static int HexParse(string s)

- static int FuncParse(string s)

- static int PctOrFrac(string s)

- static int PctNum(string s)

- static int ParseIntSafe(string s)

- static int RgbHsl(int r, int g, int b)

- static int HslToRgb(int h, int s, int l)

- bool hsvTouched;

- int basePacked;

- void InitPicker(SignalInt m)

- ColorPicker()

- ColorPicker(int packed)

- int CurrentRgb()

- void ApplyPacked(int packed)

- void SetColor(int packed)

- int Value()

- void Commit()

- override void SyncBinding()

- void SetShowAlpha(bool on)

- void SetModes(List<string> list)

- void Modes(string a, string b, string c, string d)

- string Text()

- string ModeName()

- string FormatAs(string m)

- string AlphaText()

- override string Kind()

- override List<PropSpec> Props()

- override string GetProp(string key)

- override void SetProp(string key, string val)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- void SetText(string value)

- int TriggerH(App app)

- int Render(App app, int x, int y, int width)

- int RenderIn(App app, int x, int y, int width, int height)

- override void OnPaintOverlay(App app)

- static int ViewOf(string m)

- static string ViewLabel(int view)

- static int NextAllowedView(int start, List<string> modes)

- static bool Allowed(int view, List<string> modes)

- static ColorPicker editing;

- bool viewAligned;

- static void SubmitR()

- static void SubmitG()

- static void SubmitB()

- static void SubmitA()

- static void SubmitHex()

- static void SubmitHsl()

- static void SubmitHsb()

- void SubmitChannel(int which)

- void PaintInputs(App app, int x, int y, int h, int w, bool clicked)

- void LayInput(App app, Input inp, int x, int y, int w, int h)

- void SyncInput(App app, Input inp, string text)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## ContextMenu (class)

- static int lastHover;

- static void PaintItem(App app, int x, int y, int w, int h, string label, string accel, bool hovered)

- static void PaintSeparator(App app, int x, int y, int w)

- static int Height(App app, List<string> labels)

- static int Render(App app, int mx, int my, List<string> labels)


## Countdown (class)

- Binding<int> Duration;

- Binding<bool> Active;

- Binding<string> Format;

- UiEvent Finish;

- int remainingMs;

- int lastTickMs;

- int lastDuration;

- bool finished;

- bool activeNow;

- int subCad;

- void InitCountdown(int durationMs, string fmt)

- Countdown()

- Countdown(int durationMs)

- Countdown(int durationMs, string format)

- int Remaining()

- void Restart()

- void Arm(int ms)

- void Tick(int nowMs)

- void SyncProps()

- string Fmt()

- string DisplayText()

- static string FormatText(string fmt, int rem)

- static string Pad2(int v)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## DatePicker (class)

- static DatePicker current;

- Input editor;

- Button trigger;

- UiEvent Change;

- bool open;

- int viewYear;

- int viewMonth;

- DateTime selected;

- DateTime pendingStart;

- bool hasPending;

- bool pickingEnd;

- bool hasHover;

- DateTime hoverDate;

- int hour;

- int minute;

- int endHour;

- int endMinute;

- int wid;

- string Type;

- List<DateShortcut> shortcuts;

- Binding<string> Size;

- DatePicker()

- override string Kind()

- override string StyleType()

- override Binding<string> SizeOf()

- void ApplySizeClass()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- DatePicker ClearShortcuts()

- DatePicker AddShortcut(string label, int startOffset, int endOffset)

- DatePicker UseDefaultShortcuts()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- DatePicker OnChange(Action a)

- string Text()

- DateTime GetDate()

- DateTime GetStart()

- DateTime GetEnd()

- bool IsRange()

- bool IsDateTime()

- bool HasTime()

- string Granularity()

- void SetDate(DateTime value)

- void SetText(string value)

- string FormatText()

- string FormatValue(DateTime d)

- static int Digit(string s)

- static int Number(string s, int at, int count)

- static DateTime ParseDate(string value)

- DateTime ParseSingle(string raw)

- DateTime ParseTime(string raw, bool start)

- void Edited()

- static void TriggerClicked()

- static void Use(DatePicker value)

- void ToggleOpen()

- void MoveMonth(int delta)

- void MoveView(int delta)

- int PageStart(int year)

- string TitleText(int year, int month)

- DateTime WeekStart(DateTime d)

- void PickDay(int day)

- void PickDate(DateTime picked)

- override void OnPaint(App app)

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)

- int appScale(int n)

- override void OnPaintOverlay(App app)

- void ApplyShortcut(DateShortcut sc)

- void ApplyShortcutIndex(int index)

- DateTime HoverDay(App app, int panelX, int top, int pad, int cellW, int cellH, int year, int month)

- DateTime HoverCell(App app, int panelX, int top, int pad, int cellW, int cellH, int year)

- string CellLabel(string gran, int idx, int year)

- DateTime CellValue(string gran, int idx, int year)

- bool PaintMonth(App app, Canvas c, StyleBox pop, int panelX, int top, int pad, int cellW, int cellH, int fs, int fg, int year, int month, bool clicked)

- bool PaintCellGrid(App app, Canvas c, StyleBox pop, int panelX, int top, int pad, int cellW, int cellH, int fs, int fg, int year, bool clicked)

- int PaintTimeGroup(App app, Canvas c, StyleBox pop, int gx, int timeY, int rowH, int fs, int h, int mi, bool clicked)


## DateShortcut (class)

- string label;

- int startOffset;

- int endOffset;

- DateShortcut(string label, int startOffset, int endOffset)


## Divider (class)

- Binding<string> Text;

- void InitDivider(string title)

- Divider()

- Divider(string title)

- string Label()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()


## Dropdown (class)

- List<MenuItem> menuItems;

- List<T> data;

- CellOf<T> textOf;

- string hint;

- T selItem;

- int selIndex;

- int lastAction;

- int wid;

- int trigX;

- int trigY;

- int trigW;

- int trigH;

- int maxRows;

- bool wasOpen;

- SignalBool menuOpen;

- SignalInt menuResult;

- SignalInt menuSub;

- SignalInt menuScroll;

- int menuBase;

- UiEvent Change;

- UiEvent Opened;

- UiEvent Closed;

- void InitDropdown()

- Dropdown()

- Dropdown(CellOf<T> text)

- override string Kind()

- override string StyleType()

- Dropdown<T> Bind(List<T> src)

- Dropdown<T> BindMenu(List<MenuItem> items)

- Dropdown<T> OnChange(Action a)

- Dropdown<T> OnOpen(Action a)

- Dropdown<T> OnClose(Action a)

- Dropdown<T> Hint(string text)

- Dropdown<T> Label(CellOf<T> text)

- Dropdown<T> Rows(int n)

- int Count()

- int SelectedIndex()

- bool HasSelection()

- T Selected()

- int Action()

- void SelectIndex(int row)

- void Clear()

- bool IsOpen()

- void SetOpen(bool want)

- void Open()

- void Close()

- string TriggerText()

- override void OnMeasure(App app)

- void PaintTrigger(App app)

- override void OnPaint(App app)

- List<MenuItem> SyncItems(App app)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override void OnPaintOverlay(App app)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## DropdownChrome (class)

- static void PaintTrigger(App app, int id, int x, int y, int w, int h, string label, bool open)


## DynamicTags (class)

- List<string> items;

- bool Closable;

- int Max;

- string AddText;

- string Placeholder;

- Binding<string> Size;

- UiEvent Change;

- int tagBase;

- int triggerId;

- Input editor;

- bool editing;

- bool takeFocus;

- bool editorFocused;

- static string lang="zh";

- static string TT(string en, string zh)

- void InitDynamicTags()

- DynamicTags()

- DynamicTags(List<string> initial)

- int Count()

- string TextAt(int i)

- List<string> Items()

- void Add(string text)

- void RemoveAt(int i)

- void SetItems(List<string> src)

- void Clear()

- bool CanAdd()

- string AddLabel()

- void StartEdit()

- void FinishEdit(App app, bool add)

- int TriggerWidth(App app, string cls)

- int RowWidth(App app, string cls)

- int Render(App app, int x, int y)

- override string Kind()

- override Binding<string> SizeOf()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## Ellipsis (class)

- static string Fit(string text, int w, int fs)

- static void Render(App app, int x, int y, int w, string text)


## Empty (class)

- Binding<string> Text;

- string Icon;

- void InitEmpty(string msg)

- Empty()

- Empty(string msg)

- string Label()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()


## FbBuildContext (class)

- List<Control> prepared;

- List<JsonValue> originalNodes;

- string sizeClass;

- FbBuildContext()


## FbFlow (class)

- List<int> spans;

- int colGap;

- int rowGap;

- int minCell;

- int mColGap;

- int mRowGap;

- int mMinCell;

- FbFlow()

- void AddCell(Control c, int span)

- int SpanAt(int i)

- int RowEnd(int start)

- int RowHeight(int start, int end)

- override void OnPaint(App app)

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)


## FbSlotFlow (class)

- Control host;

- FbFlow flow;

- FbSlotFlow(Control host, FbFlow flow)


## FbStack (class)

- FbStack()

- override void OnPaint(App app)

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)


## Flex (class)

- static string DirGroup()

- static string WrapGroup()

- static string JustifyGroup()

- static string ContentGroup()

- static string AlignGroup()

- static string GapGroup()

- void InitFlex(string dirCls)

- Flex()

- static Flex Row()

- static Flex Column()

- static Flex Wrapped()

- override string Kind()

- override string StyleType()

- Flex Vertical()

- Flex Horizontal()

- Flex Wrap()

- Flex NoWrap()

- Flex Justify(string mode)

- Flex Center()

- Flex End()

- Flex Between()

- Flex Align(string mode)

- Flex AlignCenter()

- Flex AlignStart()

- Flex AlignEnd()

- Flex Content(string mode)

- Flex Size(string size)

- override bool SetExtra(string key, string val)

- override string GetExtra(string key)


## FloatButton (class)

- Binding<string> Icon;

- Binding<int> Size;

- UiEvent Click;

- int wid;

- void InitFloatButton(string icon)

- FloatButton()

- FloatButton(string icon)

- int Diameter(App app)

- int Render(App app, int x, int y)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## FormBuilder (class)

- static Control Build(JsonValue root)

- static Control BuildWith(JsonValue root, List<Control> prepared)

- static void BuildInto(Control host, JsonValue root, List<Control> prepared)

- static bool IsHtmlRoot(JsonValue root)

- static void SetupRoot(Control host, JsonValue root)

- static string ApplyStyles(App app, JsonValue root)

- static void ConfigureWindow(App app, JsonValue root)

- static string DefaultSizeClass(JsonValue root)

- static bool HasSizeClass(string classes)

- static bool IsElement(JsonValue o)

- static string Kind(JsonValue o)

- static string LegacyKind(int t)

- static bool IsContainer(JsonValue o)

- static bool OwnsLabel(Control ctl)

- static bool NeedsCaption(JsonValue o, Control ctl)

- static int PrefH(JsonValue o)

- static int DefaultDock(JsonValue o)

- static int ClampSpan(int s)

- static string Caption(JsonValue o)

- static int ChildCount(JsonValue o)

- static string OptAt(JsonValue o, int i, string def)

- static int OptInt(JsonValue o, int i, int def)

- static bool IsNumber(string s)

- static string JoinOpts(JsonValue o)

- static Control MakeControl(JsonValue o, bool free)

- static Control CreateControl(JsonValue o)

- static bool IsEventKey(string key)

- static bool IsModelKey(string key)

- static string ScalarText(JsonValue value)

- static bool AcceptScalar(Control ctl, string key)

- static void SetupControl(Control ctl, JsonValue o, string sizeClass, bool anon)

- static string GridColumnsSpec(JsonValue o)

- static Control SlotOf(Control parent, JsonValue o)

- static void AddChild(Control parent, Control child)

- static void BuildArray(Control parent, JsonValue arr, JsonValue content, bool free, bool natural, bool anon, FbBuildContext ctx)

- static void CollectNodes(JsonValue arr, List<JsonValue> nodes)

- static Control BuildNode(JsonValue o, bool free, bool anon, FbBuildContext ctx)

- static bool HasGeometry(JsonValue o)

- static void SetupLayout(Control ctl, JsonValue o, bool free)

- static void AttachNode(Control parent, JsonValue o, Control ctl, bool free, bool natural, List<FbSlotFlow> flows)

- static Control WrapCell(JsonValue o, Control ctl)

- static void FillKids(Control parent, JsonValue o, bool free)

- static void BuildFlow(Control parent, JsonValue arr)

- static Control BuildCell(JsonValue o)

- static void BuildFlowOne(Control parent, JsonValue kid)

- static void BuildFree(Control parent, JsonValue o)

- static JsonValue CompDoc(string name)

- static string CompStr(JsonValue o, string key)

- static JsonValue DeepCopy(JsonValue v)

- static string ExpandRefs(JsonValue arr, int depth)

- static Control FromField(FormField f)


## FormField (class)

- string kind;

- int ftype;

- string label;

- string fname;

- string placeholder;

- string customKind;

- bool compRef;

- bool pvPreview;

- bool required;

- bool defOn;

- bool wrap;

- int span;

- int fx;

- int fy;

- int fw;

- int fh;

- int layoutX;

- int layoutY;

- int layoutW;

- int layoutH;

- bool layoutReady;

- string altRects;

- bool locked;

- int dockSide;

- int position;

- int anchor;

- int flexGrow;

- List<string> options;

- List<FormField> kids;

- int childTab;

- SignalInt uiState;

- int tabOrient;

- string styleText;

- JsonValue extra;

- public JsonValue designSource;

- void InitField(int ft)

- static bool IsContainerOf(int ft)

- static int SuggestDockOf(int ft)

- static bool IsShellOf(int ft)

- bool IsContainer()

- int KidCount()

- FormField KidAt(int i)

- FormField PlaceAt(int x, int y)

- FormField FreeSize(int w, int h)

- int FreeX()

- int FreeY()

- int FreeW()

- int FreeH()

- void SetLayoutRect(int x, int y, int w, int h)

- void ResetLayout()

- int LayoutX()

- int LayoutY()

- int LayoutW()

- int LayoutH()

- void StashRect(string key)

- bool LoadRect(string key)

- static bool AltKeyIs(string entry, string key)

- void SeedOptions(int ft)

- FormField(int ft)

- static FormField New(int ft, int seq)

- FormField Clone()

- static int Custom()

- bool IsCustom()

- static FormField NewCustom(string kind, int seq)

- static string TypeKey(int ft)

- static int TypeForKey(string key)

- static int MaxType()

- static string TypeName(int ft)

- static List<string> SemanticEventsOf(int ft)

- override List<string> Events()

- static string DefaultLabel(int ft)

- static string DefaultPlaceholder(int ft)

- static bool UsesOptionsOf(int ft)

- static bool IsNonVisualOf(int ft)

- bool IsNonVisual()

- bool UsesOptions()

- static bool IsDisplayOf(int ft)

- bool IsDisplay()

- int RowUnits()

- int OptionCount()

- string OptionAt(int i)

- void AddOption(string s)

- void RemoveOptionAt(int i)

- void SetOptionAt(int i, string s)

- void MoveOption(int from, int to)

- string JoinOptions()

- void SetOptionsFrom(string joined)

- string KindName()

- static string KindForType(int ft)

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)


## FormGroup (class)

- List<FormRow> rows;

- int badCount;

- FormGroup()

- override string Kind()

- FormRow Add(string caption, Control ctl)

- FormRow Field(string name)

- int Count()

- bool Validate()

- int ErrorCount()

- void ClearErrors()

- bool Valid()

- string FirstError()

- static string ValueOf(Control ctl)

- string Value(string name)

- bool BoolValue(string name)

- int IntValue(string name, int def)


## FormRow (class)

- Control field;

- Label err;

- List<FormRule> rules;

- FormRow(string caption, Control ctl)

- string Name()

- string Value()

- FormRow Rule(FormRule r)

- bool Active()

- string Check()

- string Peek()

- void Clear()

- void Paint(string msg)


## FormRule (class)

- FormCheck check;

- FormRule(FormCheck c)

- string Apply(string value)

- static FormRule Required(string msg)

- static FormRule MinLen(int n, string msg)

- static FormRule MaxLen(int n, string msg)

- static FormRule Email(string msg)

- static FormRule Range(int lo, int hi, string msg)

- static FormRule EqualsField(FormRow peer, string msg)

- static FormRule Checked(string msg)

- static bool IsInt(string s)


## Grid (class)

- int cols;

- int minColW;

- int lastW;

- int measW;

- void InitGrid(int n)

- Grid()

- static Grid Of(int n)

- override string Kind()

- override string StyleType()

- Grid SetCols(int n)

- Grid MinColumn(int px)

- int ResolvedCols(int cw)

- int ColGapPx()

- int RowGapPx()

- static int SpanOf(Control c)

- static int OffsetOf(Control c)

- static int ColEdge(int cw, int gap, int n, int i)

- static int SpanWidth(int cw, int gap, int n, int at, int span)

- int PlaceSlots(int n, List<Control> items, List<GridSlotPlacement> outSlots)

- int Place(int n, List<Control> items, List<int> rowOf, List<int> colOf, List<int> spanOf)

- List<Control> VisibleItems()

- List<int> RowHeightsFromSlots(int rows, List<Control> items, List<GridSlotPlacement> slots)

- List<int> RowHeights(int rows, List<Control> items, List<int> rowOf)

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)

- override bool SetExtra(string key, string val)

- override string GetExtra(string key)


## GridItem (class)

- int span;

- int offset;

- void InitItem(int sp, int off)

- GridItem()

- GridItem(int sp)

- GridItem(int sp, int off)

- override string Kind()

- override string StyleType()

- int Span()

- int Offset()

- GridItem SetSpan(int sp)

- GridItem SetOffset(int off)

- override bool SetExtra(string key, string val)

- override string GetExtra(string key)


## GridSlotPlacement (class)

- public int row;

- public int col;

- public int span;

- public GridSlotPlacement(int row, int col, int span)


## IconView (class)

- string Name;

- int Box;

- int Color;

- void InitIconView(string name, int box, int color)

- IconView()

- IconView(string name)

- IconView(string name, int box)

- IconView(string name, int box, int color)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()


## Image (class)

- Binding<string> Src;

- string Alt;

- string Fit;

- UiEvent Loaded;

- UiEvent Error;

- string resolvedSrc;

- string imgKey;

- int imgW;

- int imgH;

- int loadState;

- bool isSvg;

- string svgText;

- byte[]svgBytes;

- int svgBytesLen;

- string svgBaseKey;

- string svgRasterKey;

- int svgRasterW;

- int svgRasterH;

- void InitImage(string src, string alt, string fit)

- Image()

- Image(string src)

- Image(string src, string alt)

- Image(string src, string alt, string fit)

- string FitMode()

- void Resolve(App app)

- void ResetState()

- void MarkLoaded(int w, int h)

- void ResolveFile(string src)

- void ResolveSvgFile(string path)

- void ResolveDataUri(string src)

- void ResolveUrl(App app, string src)

- delegate void

- UrlFetchFn(App app, Image img, string src, string key);

- static UrlFetchFn urlFetcher;

- static void SetUrlFetcher(UrlFetchFn fn)

- void RasterSvg(int boxW, int boxH)

- void OnFetched(string key, int w, int h)

- void OnSvgFetched(string text)

- void OnFetchFailed()

- void Reload()

- static long StrHash(string s)

- static int ScaleTo(int v, int num, int den)

- void OnMeasure(App app)

- void OnPaint(App app)

- void BlitFitted(App app, Canvas c)

- void DrawPlaceholder(App app, Canvas c, StyleBox s)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- int FitIndex()


## Input (class)

- SignalString editSig;

- Binding<string> data;

- string hint;

- int cursorPos;

- int selAnchor;

- int wid;

- App liveApp;

- bool password;

- bool reveal;

- string iconLead;

- string iconTrail;

- UiEvent Change;

- UiEvent Submit;

- string errMsg;

- Binding<string> Size;

- bool Clearable;

- bool Autoselect;

- bool autoselected;

- int MaxLen;

- string Prefix;

- string Suffix;

- string PrefixIcon;

- string PasswordIcon;

- string ClearIcon;

- bool ShowCount;

- bool CountGraphemes;

- bool Loading;

- bool Round;

- string FieldStatus;

- bool PassiveActivated;

- bool passiveActive;

- InputFilterFn Filter;

- void InitInput(SignalString sig, string hintText)

- Input():this("")

- Input(string hintText)

- static string Mask(string s)

- string GetText()

- void SetHint(string v)

- int WidgetId()

- void SetText(string v)

- void SelectAll()

- void Clear()

- void Focus()

- void Blur()

- void ScrollToEnd()

- void ScrollToStart()

- void SetStatus(string v)

- int CountOf(string s)

- int CountValue()

- static string CountText(int count, int max)

- static string OnlyDigits(string s)

- static string NoSpaces(string s)

- void SetError(string msg)

- void ClearError()

- override void SyncBinding()

- void PushBinding()

- void Edited()

- void ClampCursor()

- bool HasSel()

- int SelStart()

- int SelEnd()

- string SelText()

- void DeleteSel()

- bool Insert(string s)

- bool DeleteCandidate(string cand)

- void HandleInput(App app)

- int Render(App app, int x, int y, int w, int h)

- override string Kind()

- override Binding<string> SizeOf()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## InputNumber (class)

- SignalInt model;

- Binding<int> data;

- string hint;

- int minV;

- int maxV;

- int stepV;

- string editBuf;

- bool hadFocus;

- int wid;

- int minusId;

- int plusId;

- UiEvent Change;

- UiEvent Submit;

- UiEvent Invalid;

- Binding<string> Size;

- Binding<string> Prefix;

- Binding<string> Suffix;

- Binding<string> Placement;

- bool Loading;

- int Precision;

- bool GroupDigits;

- NumFormatter Formatter;

- NumParser Parser;

- NumValidator Check;

- bool invalidFlag;

- void InitNumber(SignalInt m, int lo, int hi)

- InputNumber()

- InputNumber(int lo, int hi)

- override string Kind()

- override string StyleType()

- override Binding<string> SizeOf()

- void Hint(string text)

- InputNumber OnChange(Action a)

- InputNumber OnSubmit(Action a)

- InputNumber OnInvalid(Action a)

- InputNumber WithFormatter(NumFormatter f)

- InputNumber WithParser(NumParser p)

- InputNumber WithValidator(NumValidator v)

- int Step()

- int Decimals()

- int Value()

- static int Pow10(int n)

- static string NumberText(int scaled, int decimals)

- static string GroupThousands(string s)

- string PlainText(int scaled)

- string DisplayText()

- static int ParseScaled(string text, int decimals, int def, NumParser parser)

- int StepUpVal(int v)

- int StepDownVal(int v)

- int Clamp(int v)

- void RunValidator(int v)

- string EditDisplay()

- void SetRaw(int v)

- void SetValue(int v)

- override void SyncBinding()

- static int ParseInt(string s, int def)

- void CommitBuffer()

- int Render(App app, int x, int y, int w, int h)

- void PaintBox(App app, int x, int y, int w, int h)

- void PaintSpinnerDots(App app, int cx, int cy, int radius, int accent, int rest)

- void HandleKeys(App app, bool focused)

- override List<PropSpec> Props()

- override string GetProp(string key)

- override void SetProp(string key, string val)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## InputOtp (class)

- List<string> cells;

- int active;

- int wid;

- int length;

- App liveApp;

- Binding<string> data;

- Binding<string> Size;

- bool MaskMode;

- string MaskChar;

- bool Block;

- bool ReadOnly;

- string FieldStatus;

- List<int> Groups;

- string Separator;

- InputFilterFn Filter;

- UiEvent Change;

- UiEvent Complete;

- bool wasFull;

- static List<string> SplitCps(string s)

- static string OnlyLetters(string s)

- int SeparatorCount()

- bool SepAfter(int i)

- void InitOtp(int n)

- InputOtp()

- InputOtp(int n)

- int WidgetId()

- void SetLength(int n)

- int GetLength()

- int ActiveIndex()

- void SetCodeFromModel()

- string GetCode()

- int FilledCount()

- bool IsFull()

- bool SetCode(string s)

- bool SetValue(string s)

- bool IsReadOnly()

- void ClearAll()

- void Focus()

- void Blur()

- void SetStatus(string v)

- void Edited(bool filled)

- bool TypeChar(string ch)

- static void Restore(List<string> cells, List<string> snapshot)

- bool Backspace()

- bool DeleteForward()

- void MoveLeft()

- void MoveRight()

- void MoveHome()

- void MoveEnd()

- bool Paste(string clip)

- void HandleKeys(App app)

- override void SyncBinding()

- string CellDisplay(int i)

- int Render(App app, int x, int y, int w, int h)

- int CellIndexAt(int boxX, int startX, int cellW, int gap, int sepTextW, int boxW, int px)

- override string Kind()

- override Binding<string> SizeOf()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## Label (class)

- Binding<string> Text;

- bool wrap;

- void InitLabel(string txt)

- Label()

- Label(string txt)

- string Str()

- StyleBox ResolvedStyle(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()


## Layer (class)

- static List<ToastItem> toasts;

- static List<ToastItem> notifs;

- static Rect toastPrev;

- static Rect notifPrev;

- static List<LayerState> zorder;

- static bool requestRedraw;

- static int photosTick;

- static SignalInt photosIdx;

- static int photoCount;

- static bool photosCloseReq;

- static int savedSeq;

- static int bodySeq;

- static void EnsureZorder()

- static void PruneZorder()

- static LayerState TopmostDialog()

- static bool IsTopmost(LayerState s)

- static void Close(App app, LayerState s)

- static void BeginExit(LayerState s)

- static void NotifyOpened(LayerState s)

- static bool CloseTopmostDialog()

- static bool CloseLast(int kind)

- static void CloseAll(int kind)

- static bool Key(App app, int code)

- static void BeginBody()

- static void EndBody()

- static void EnsureQueues()

- static void MarkQueueDirty(App app, Rect prev, bool any, int rx, int ry, int rw, int rh)

- static int ActionWidth(App app, Button b, int minW)

- static int Clamp01(int v)

- static string RoleClass(int mtype)

- static string RoleIcon(int mtype)

- static void Msg(string text, int type)

- static void RenderToasts(App app)

- static Rect DrawToast(App app, int y, int a, string text, int mtype)

- static void Notify(string title, int type)

- static void RenderNotifs(App app)

- static int NotifyH(App app)

- static void DrawNotify(App app, int x, int y, int w, string message, int ntype)

- static void DrawNotifyCard(App app, int x, int y, int w, int a, string message, int ntype)

- static void RenderLoad(App app, int x, int y, int w, int h, string tip)

- static void Tips(App app, int anchorX, int anchorY, string text)

- static int Modal(App app, int w, int h, string title)

- static int DrawPopconfirm(App app, int anchorX, int anchorY, string message, string okText, string cancelText)

- static bool RenderPhotos(App app, List<string> slides, SignalInt idx, CarouselAnim anim)

- static int BarH(App app)

- static int FooterH(App app)

- static int BtnW(App app)

- static int BodyTop(App app, LayerState s)

- static int BodyLeft(App app, LayerState s)

- static bool Inside(int mx, int my, int x, int y, int w, int h)

- static void ApplyResize(App app, LayerState s, int mx, int my)

- static void MinPos(App app, LayerState s, List<int> outv)

- static void Bounds(App app, LayerState s, List<int> outv)

- static bool Contains(App app, LayerState s, int mx, int my)

- static int Entrance(App app, LayerState s)

- static int EntranceDy(App app, LayerState s)

- static int ExitMs()

- static int ExitP(LayerState s)

- static int MulAlpha(int color, int a)

- static bool ClosingFrame(App app, LayerState s)

- static void RenderStack(App app, List<LayerState> list)

- static void Render(App app, LayerState s)

- static void FlushCloseRedraw(App app)

- static void RenderActive(App app, LayerState s, bool active)

- static void RenderClosing(App app, LayerState s, int outP)

- static void RenderLayerBody(App app, LayerState s, int ex, int ey, int ew, int eh, int bar)

- static void RenderDialog(App app, LayerState s)

- static void RenderDialogExit(App app, LayerState s, int outP)

- static void PaintDialog(App app, LayerState s, int x, int y, int w, int h, int a, int shadeId)

- static void CapButton(App app, int x, int y, int w, int h, string glyph, int gl, bool danger)


## LayerState (class)

- string title;

- string body;

- int x;

- int y;

- int w;

- int h;

- int sx;

- int sy;

- int sw;

- int sh;

- int state;

- bool open;

- bool dragging;

- int grabDx;

- int grabDy;

- int barId;

- int closeId;

- int maxId;

- int minId;

- bool footer;

- int action;

- int kind;

- bool modal;

- int dtype;

- bool twoButtons;

- string okText;

- string cancelText;

- string altText;

- Button okBtn;

- Button cancelBtn;

- Button altBtn;

- Input promptInput;

- int openMs;

- int minSlot;

- Control view;

- bool shadeClose;

- int promptFormType;

- int promptMaxLen;

- TextArea promptArea;

- bool sizing;

- int rDir;

- int rStartX;

- int rStartY;

- int rStartW;

- int rStartH;

- int rsLId;

- int rsRId;

- int rsBId;

- int rsSEId;

- int rsSWId;

- UiEvent Opened;

- UiEvent Closed;

- bool wasOpen;

- bool openedRaised;

- int AutoCloseMs;

- bool closing;

- int closeMs;

- bool closedRaised;

- bool autoCenter;

- int sizeMinW;

- int sizeMaxH;

- Control wrap;

- static int winSeq;

- LayerState(string title, int x, int y, int w, int h)

- static LayerState Confirm(string title, string body, int dtype)

- static LayerState Choose(string title, string body, int dtype, string okText, string altText, string cancelText)

- static LayerState Alert(string title, string body, int dtype)

- static LayerState Prompt(string title, string hint)

- static LayerState PromptWith(string title, string hint, int formType, string value, int maxLen)

- void SetBody(Control v)

- void SizeToContent(App app, int minW, int maxH)

- void ApplySizeToContent(App app)

- void Open()

- string PromptText()


## ListColumn (class)

- string title;

- int weight;

- bool right;

- CellOf<T> cell;

- ListColumn(string title, CellOf<T> cell)

- ListColumn(string title, int weight, CellOf<T> cell)

- ListColumn<T> Right()

- string TextOf(T item)


## ListItem (class)

- Binding<string> Text;

- Binding<string> Desc;

- bool Selected;

- void InitItem(string ttl, string desc)

- ListItem()

- ListItem(string ttl)

- ListItem(string ttl, string desc)

- static ListItem Header(string ttl)

- string Label()

- string Sub()

- override string Kind()

- override string StyleType()

- int StateNow()

- StyleBox ResolvedStyle(App app)

- StyleBox TitleStyle(App app)

- StyleBox DescStyle(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override List<PropSpec> Props()


## ListView (class)

- List<T> data;

- List <ListColumn<T>> cols;

- RowOf<T> rowOf;

- string Empty;

- int sel;

- int hover;

- bool multi;

- List<int> marks;

- RowGate<T> onlySelectable;

- SignalInt scroll;

- bool rowsStale;

- int builtFor;

- int rowSeqBase;

- int rowHitBase;

- int rowHitCount;

- List<RowDragOut> dragOut;

- int dragOutFrom;

- static int RowIdReserve()

- UiEvent Select;

- UiEvent Activate;

- UiEvent Context;

- void InitList()

- ListView()

- ListView(List <ListColumn<T>> columns)

- ListView(ListColumn<T> column)

- ListView(RowOf<T> row)

- ListView<T> WithColumns(List <ListColumn<T>> columns)

- ListView<T> WithRow(RowOf<T> row)

- override string Kind()

- override string StyleType()

- ListView<T> Bind(List<T> src)

- void Refresh()

- int Count()

- int SelectedIndex()

- bool HasSelection()

- T Selected()

- int ContentHeightForTest(App app)

- bool Has(int row)

- T ItemAt(int row)

- void SelectIndex(int row)

- void ClearSelection()

- ListView<T> WithMultiSelect()

- ListView<T> WithSelectable(RowGate<T> gate)

- bool IsMarked(int row)

- List<int> SelectedIndices()

- int SelectionCount()

- ListView<T> OnSelect(Action a)

- ListView<T> OnActivate(Action a)

- ListView<T> OnContext(Action a)

- ListView<T> OnDragOut(RowDragOut a)

- ListView<T> EmptyText(string text)

- bool Templated()

- int RowHeight(App app)

- int HeaderHeight(App app)

- void BuildRows(App app)

- int ContentHeight(App app)

- int RowExtent(App app, Control row)

- void EnsureRowIds()

- void DragOutTick(App app)

- int RowInteract(App app, int index, int x, int y, int w, int h)

- void NoteSelfDamage(App app)

- bool Selectable(int row)

- void PaintEmpty(App app)

- List<int> ColumnWidths(int rowW)

- void PaintHeader(App app, int headH)

- void PaintColumnRows(App app, int headH)

- void ArrangeRows(App app, int headH)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## Marquee (class)

- Binding<string> Text;

- int speed;

- bool autoFill;

- int phase;

- int lastMs;

- MarqueeAnim anim;

- void InitMarquee(string txt)

- Marquee()

- Marquee(string txt)

- string Str()

- Marquee Speed(int pxPerSec)

- Marquee AutoFill(bool v)

- Marquee Animate(MarqueeAnim state)

- StyleBox ResolvedStyle(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()


## MarqueeAnim (class)

- int phase;

- int lastMs;

- MarqueeAnim()


## Menu (class)

- static int RenderVertical(App app, int x, int y, int w, int bottomY, int itemH, List<string> items, SignalInt selected, int scrollOffset)

- static void HandleClick(App app, int firstId, int count, SignalInt selected)

- static string KeyOf(MenuItem m)

- static bool IsGroup(MenuItem m)

- static List<MenuRow> BuildVisibleRows(List<MenuItem> items, MenuExpandState st)

- static void AppendRows(List<MenuRow> rows, List<MenuItem> items, int depth, MenuExpandState st)

- static bool ExpandAncestors(List<MenuItem> items, MenuExpandState st, string key)

- static bool ContainsDeep(List<MenuItem> items, string key)

- static void InitDefaults(List<MenuItem> items, MenuExpandState st, List<string> defaultExpandedKeys, string selectedKey)

- static int RenderTree(App app, int x, int y, int w, int bottomY, int itemH, List<MenuItem> items, SignalString selectedKey, MenuExpandState state, MenuLabelOf renderLabel, MenuIconOf renderIcon, MenuExpandIconOf expandIcon, List<string> defaultExpandedKeys, int scrollOffset)

- static void PaintTreeRow(App app, Canvas c, Theme t, int id, int x, int iy, int w, int itemH, int accent, MenuRow row, string selKey, MenuExpandState state, StyleBox normal, StyleBox hover, StyleBox selectedStyle, StyleBox disabledStyle, StyleBox headerStyle, StyleBox arrowStyle, StyleBox iconStyle, StyleBox sepStyle, MenuLabelOf renderLabel, MenuIconOf renderIcon, MenuExpandIconOf expandIcon)

- static string RowText(MenuLabelOf renderLabel, MenuItem m)

- static void HandleTreeClick(App app, int firstId, List<MenuItem> items, MenuExpandState state, SignalString selectedKey)

- static bool ClickTree(App app, int firstId, List<MenuItem> items, MenuExpandState state, SignalString selectedKey, int hit)


## MenuExpandState (class)

- List<string> keys;

- bool initialized;

- MenuExpandState()

- bool Contains(string key)

- void Expand(string key)

- void Collapse(string key)

- void Toggle(string key)


## MenuRow (class)

- MenuItem item;

- int depth;

- MenuRow(MenuItem it, int d)


## NumberAnimation (class)

- Binding<int> From;

- Binding<int> To;

- Binding<int> Duration;

- Binding<bool> Active;

- Binding<int> Precision;

- Binding<string> Separator;

- Binding<string> Decimal;

- Binding<string> Prefix;

- Binding<string> Suffix;

- UiEvent Finish;

- int curScaled;

- int fromS, toS, durS;

- int precS;

- string sepS, decS, preS, sufS;

- int elapsedMs;

- int lastTickMs;

- int lastFrom, lastTo, lastDur;

- bool activeNow;

- bool finished;

- void InitNumberAnimation(int from, int to, int dur)

- NumberAnimation()

- NumberAnimation(int to)

- NumberAnimation(int from, int to)

- NumberAnimation(int from, int to, int durationMs)

- int Value()

- string CurrentText()

- void SetRange(double from, double to)

- static int ScaleIt(double v, int scale)

- void Restart()

- void Arm(int from, int to, int dur)

- void Tick(int nowMs)

- void SyncProps(int nowMs)

- string DisplayText()

- static int ValueAt(int from, int to, int e)

- static string Format(int scaled, int precision, string sep, string dec)

- static string GroupWith(string s, string sep)

- override void OnMeasure(App app)

- string DisplayTextOf(int scaled)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## PageHeader (class)

- Binding<string> Text;

- Binding<string> Subtitle;

- string Icon;

- int wid;

- UiEvent Back;

- void InitPageHeader(string ttl, string sub)

- PageHeader()

- PageHeader(string ttl, string sub)

- string Label()

- string Sub()

- StyleBox ResolvedStyle(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int TitleFont(App app, string cls)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## Pagination (class)

- SignalInt model;

- Binding<int> Page;

- int totalPages;

- UiEvent Change;

- UiEvent SizeChange;

- int baseId;

- int idCount;

- bool useItemCount;

- int itemCount;

- int pageSize;

- List<int> pageSizes;

- int sizeIdx;

- bool sizeOpen;

- int sizeBoxX;

- int sizeBoxY;

- int sizeBoxW;

- int sizeBoxH;

- string prefixText;

- string suffixText;

- string prevText;

- string nextText;

- bool showTotal;

- bool jumper;

- string jumperLabel;

- string jumperSuffix;

- List<string> order;

- int fieldId;

- string fieldBuf;

- int pagerCount;

- void InitPagination(int total, SignalInt m)

- Pagination():this(1)

- Pagination(int total)

- Pagination(int total, SignalInt m)

- static Pagination Of(int total, SignalInt m)

- static Pagination WithCount(int items, int size, SignalInt m)

- void SetTotal(int total)

- int TotalPages()

- int Total()

- void SetItemCount(int items)

- int ItemCount()

- bool UsesItemCount()

- void SetPageSize(int size)

- int PageSize()

- void SetPageSizes(List<int> sizes)

- List<int> PageSizes()

- void SetPageSizesText(string text)

- string PageSizesText()

- Pagination Jumper(bool v)

- bool HasJumper()

- void OpenSizes()

- void CloseSizes()

- bool SizesOpen()

- Pagination ShowSizes(bool v)

- bool HasSizes()

- Pagination Prefix(string t)

- Pagination Suffix(string t)

- Pagination PrevText(string t)

- Pagination NextText(string t)

- Pagination ShowTotal(bool v)

- Pagination JumperLabel(string t)

- Pagination JumperSuffix(string t)

- void SetPagerCount(int n)

- int PagerCount()

- List<string> EffOrder()

- void SetOrderText(string text)

- string OrderText()

- static int MeasureWidth(App app, int totalPages, int current)

- int EllipsisStep()

- static List<int> BuildPages(int totalPages, int current)

- static List<int> BuildPagesSlots(int totalPages, int current, int slots)

- int ButtonSize(App app, StyleBox s)

- int FontSize(App app, StyleBox s)

- int Radius(App app, StyleBox s, int btnSize)

- int ArrowWidth(App app, string txt, int btnSize, int fs)

- void EnsureIds(int entries)

- void SetPage(int pg)

- void GoTo(App app, int pg)

- override void SyncBinding()

- void Normalize()

- override void OnMeasure(App app)

- int TotalWidth(App app, StyleBox s, int fs, int gap)

- int SegmentWidth(App app, StyleBox s, int fs, string seg)

- string TotalText()

- string JumperLabelText()

- string SizeText(int n)

- override void OnPaint(App app)

- void HandleInput(App app, List<int> pages, int nextId, int fldId, int chevId, bool off)

- void CommitField(App app, int fldId)

- void PaintField(App app, int id, int x, int y, int w, int h, int radius, int fontSize, int current, bool off)

- void PaintArrow(App app, string glyph, int id, int cx, int w, int by, int btnSize, int radius, int ico, bool atEnd, StyleBox item, StyleBox arrow)

- void PaintEllipsis(App app, int id, int cx, int btnSize, int radius, int fontSize, int current, bool off)

- void PaintPage(App app, int id, int pg, int cx, int btnSize, int radius, int fontSize, int current, bool off)

- void PaintSizesBox(App app, int id, int x, int y, int w, int h, int radius, int fontSize, bool off)

- void PaintSimple(App app)

- void HandleSimpleInput(App app, int nextId, int fldId, bool off)

- override void OnPaintOverlay(App app)

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override string GetProp(string key)

- override void SetProp(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## Panel (class)

- int style;

- string title;

- int surfaceColor;

- FxOptions fx;

- void InitPanel(string name, int st)

- Panel WithFx(FxOptions o)

- Panel()

- Panel(string name)

- Panel Plain(int color)

- Panel Layout()

- Panel Titled(string t)

- static Panel Column()

- static Panel Root(string name)

- static Panel Row()

- override string Kind()

- override string StyleType()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- bool StyledSurface(App app, string type, StyleBox s)

- override void OnPaint(App app)

- override int StylePadT()

- Rect Inner()


## Popover (class)

- string trigLabel;

- string title;

- string body;

- int panelW;

- int trigId;

- SignalBool open;

- int trigX;

- int trigY;

- int trigW;

- int trigH;

- void InitPopover(string trig, string ti, string bo, int w)

- Popover(string trig, string ti, string bo, int w)

- bool IsOpen()

- int Render(App app, int x, int y, int w, int h)

- override List<PropSpec> Props()

- override void OnPaintOverlay(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int PaintPanel(App app, int anchorX, int anchorY, int w, string title, string body)

- static void Render(App app, int anchorX, int anchorY, int w, int h, string title, string body)


## Progress (class)

- int percent;

- Binding<int> data;

- int ptype;

- int shape;

- bool showIndicator;

- int strokeWidth;

- bool processing;

- int fillColor;

- int railColor;

- string format;

- int ringSize;

- List<ProgressRing> rings;

- void InitProgress(int pct, int pt)

- Progress():this(0, 1)

- Progress(int pct, int pt)

- void SetPercent(int pct)

- int Percent()

- Progress Ring()

- Progress Multi()

- Progress Dashboard()

- Progress AddCircle(int pct, int pt)

- Progress Processing(bool on)

- Progress StrokeWidth(int px)

- Progress FillColor(int argb)

- Progress RailColor(int argb)

- Progress Format(string tmpl)

- Progress Size(int px)

- Progress ShowText(bool on)

- override string Kind()

- static string TypeClass(int pt)

- string VariantClass()

- string PartCls()

- override List<PropSpec> Props()

- override string GetProp(string key)

- override void SetProp(string key, string val)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- string CirclesSpec()

- void SetCircles(string spec)

- string IndicatorText(int pct)

- void PaintBar(App app, int pct)

- void PaintRing(App app, int pct, int startDeg, int totalDeg)

- void PaintMulti(App app)

- int CircleType(int i)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int Clamp(int percent)

- static int MixWhite(int c, int num)

- static bool RectOnScreen(App app, int x, int y, int w, int h)


## ProgressRing (class)

- public int percent;

- public int ringType;

- public ProgressRing(int percent, int ringType)


## Prompt (class)

- static int FieldHeight(App app)

- static int Height(App app, int rows)

- static void Surface(App app, int x, int y, int w, int h, string caption)

- static void Caption(App app, int x, int y, string caption)

- static void Field(App app, int x, int y, int w, string text)


## QrCode (class)

- Binding<string> Text;

- Binding<string> Ecl;

- Binding<int> Size;

- Binding<int> Dark;

- Binding<int> Light;

- Binding<int> Margin;

- Binding<string> Logo;

- Binding<int> LogoPct;

- UiEvent Change;

- string cacheKey;

- QrMatrix cacheMx;

- int baseId;

- void InitQr()

- QrCode()

- QrCode(string text)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- int Render(App app, int x, int y)

- void PaintLogo(App app, Canvas c, QrMatrix mx, int margin, int cell, int x, int y)

- int BoxPx(App app, int mods)

- int EffectiveDark(App app)

- int EffectiveLight(App app)

- QrEcl EffectiveEcl()

- QrMatrix Matrix()

- string ToSvg()

- byte[]ToPngBytes()

- int SavePng(string path)

- int ExportBox()

- int ExportDark()

- int ExportLight()

- static string SvgOf(QrMatrix mx, int margin, int dark, int light, int boxPx)

- static string HexColor(int argb)

- static string HexNib(int v)

- static byte[]PngOf(QrMatrix mx, int margin, int dark, int light, int boxPx)

- static byte[]Ihdr(int w)

- static int WriteChunk(byte[]dst, int at, byte b0, byte b1, byte b2, byte b3, byte[]data)


## QrEncoder (class)

- static int[]EC_PER_BLOCK_L=new int[]{ -1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30};

- static int[]EC_PER_BLOCK_M=new int[]{ -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28};

- static int[]EC_PER_BLOCK_Q=new int[]{ -1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 37, 34, 28, 30, 31, 24, 37, 32, 29, 37, 34, 31};

- static int[]EC_PER_BLOCK_H=new int[]{ -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30};

- static int[]NUM_BLOCKS_L=new int[]{ -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25};

- static int[]NUM_BLOCKS_M=new int[]{ -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49};

- static int[]NUM_BLOCKS_Q=new int[]{ -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68};

- static int[]NUM_BLOCKS_H=new int[]{ -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81};

- static int EcPerBlock(QrEcl ecl, int ver)

- static int NumBlocks(QrEcl ecl, int ver)

- static int MODE_NUMERIC=1;

- static int MODE_ALNUM=2;

- static int MODE_BYTE=4;

- static string ALNUM_CHARS="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

- static int[]AlignPattern(int ver)

- static int[]BlockPlan(int ver, QrEcl ecl)

- static int TotalCodewords(int ver)

- static int[]gfExp=null;

- static int[]gfLog=null;

- static void EnsureGf()

- static int GfMul(int a, int b)

- static int[]RsGenerator(int deg)

- static int[]RsRemainder(int[]data, int dataLen, int[]gen)

- class Segment

- public static QrMatrix Encode(string text, QrEcl ecl)

- public static QrEcl ParseEcl(string s)

- static bool IsNumeric(int b)

- static int AlnumValueByte(int c)

- static int AlnumValue(string ch)

- static int SeqByteLen(int lead)

- static List <QrEncoder.Segment> MakeSegments(string text)

- static QrEncoder.Segment MakeByteRun(string run)

- static int ModeOfByte(int b, string text, int pos)

- static int CharCountBits(int mode, int ver)

- static int DataCapacity(int ver, QrEcl ecl)

- static int PickVersion(List <QrEncoder.Segment> segs, QrEcl ecl)

- static int WriteSegments(List <QrEncoder.Segment> segs, int ver, QrEcl ecl, int[]cw)

- static void AppendVal(List<int> bits, int val, int n)

- static void SetFn(bool[]fn, int size, int x, int y)

- static void SetDark(bool[]dark, bool[]fn, int size, int x, int y)

- static void DrawFinder(bool[]dark, bool[]fn, int size, int cx, int cy)

- static void DrawAlign(bool[]dark, bool[]fn, int size, int cx, int cy)

- static int BchVersion(int ver)

- static QrMatrix BuildMatrix(int ver, QrEcl ecl, int[]cw, int nDataCw)

- static bool[]ApplyMask(bool[]dark, bool[]fn, int size, int mask, QrEcl ecl)

- static bool MaskBit(int m, int x, int y)

- static int Penalty(bool[]m, int size)

- static int RunPenaltyLine(bool[]m, int size, int line, bool horizontal)

- static bool Pattern11(bool[]m, int size, int x, int y, bool horizontal)


## QrMatrix (class)

- public int version;

- public int size;

- public QrEcl ecl;

- public bool[]modules;

- public bool Get(int x, int y)


## Radio (class)

- SignalInt model;

- Binding<int> data;

- int optionValue;

- int wid;

- string label;

- Binding<string> Size;

- Binding<bool> Bordered;

- UiEvent Change;

- void InitRadio(SignalInt m, int ov, string lbl)

- Radio()

- Radio(int ov, string lbl)

- Radio(SignalInt m, int ov, string lbl)

- bool IsSelected()

- void SetLabel(string v)

- override string Kind()

- override Binding<string> SizeOf()

- void ApplyVariants()

- override void SyncBinding()

- int Render(App app, int x, int y, string label)

- void Select()

- override List<string> Events()

- override List<PropSpec> Props()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- int PaintStyled(App app, int id, int x, int y, string text, bool selected)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## RadioButton (class)

- SignalInt model;

- int optionValue;

- int wid;

- string label;

- string vcls;

- string seamCls;

- int mw;

- int mh;

- int row;

- UiEvent Change;

- void InitRadioButton(SignalInt m, int ov, string lbl)

- RadioButton()

- RadioButton(SignalInt m, int ov, string lbl)

- bool IsSelected()

- void SetMeta(int value, string sizeCls)

- void SetSeam(string cls)

- string Cls()

- StyleBox Box(App app, int latched)

- void Measure(App app)

- void PaintSeg(App app, int x, int y, int w, int h)

- void Select()

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## RadioButtonGroup (class)

- SignalInt model;

- List<RadioButton> items;

- UiEvent Change;

- int wid;

- string sizeCls;

- int rowGapPx;

- int colGapPx;

- int rowCount;

- Binding<string> Size;

- void InitRadioButtonGroup(SignalInt m)

- RadioButtonGroup()

- RadioButtonGroup(SignalInt m)

- SignalInt Selected()

- int Count()

- RadioButton AddOption(string label)

- RadioButton AddOption(string label, int value)

- override void OnChildEvent(Control child, string evt)

- void ClearItems()

- void SetSize(string cls)

- void SetGap(int rg, int cg)

- int LayoutRows(int avail)

- int rowOf(int i)

- int colOf(int i)

- int lastColOf(int r)

- void SyncGap(App app)

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)

- override void OnPaint(App app)

- int IndexOfSelected()

- override string Kind()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override List<string> Events()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<PropSpec> Props()


## RadioGroup (class)

- SignalInt model;

- Binding<int> data;

- Binding<bool> Buttons;

- Binding<string> Size;

- List<RadioOptionMeta> optionMetas;

- UiEvent Change;

- RadioButtonGroup buttonGroup;

- RadioGroup()

- SignalInt Selected()

- int SelectedValue()

- int SelectedIndex()

- int IndexOfValue(int v)

- void SetSelectedValue(int v)

- void SetSelectedIndex(int index)

- Control AddOption(string text, int value)

- Control AddOption(string text, int value, bool disabled)

- void SetOptions(List<RadioOption> opts)

- void SyncGroup()

- string labelOf(int i)

- int valueAt(int i)

- bool optionDisabledAt(int i)

- void PushValue()

- void ReconcileButtons()

- override void ClearOptions()

- override Control MakeItem(int index, string text)

- override void OnChildEvent(Control child, string evt)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override bool SetExtra(string key, string val)

- override string GetExtra(string key)

- override string OptionsText()

- override void SetOptionsText(string text)

- override List<PropSpec> Props()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override List<string> Events()

- override string Kind()


## RadioOption (class)

- string label;

- int value;

- bool hasValue;

- bool disabled;

- string cls;

- RadioOption(string lbl)

- RadioOption(string lbl, int val)

- RadioOption(string lbl, int val, bool dis)


## RadioOptionMeta (class)

- public int value;

- public bool disabled;

- RadioOptionMeta(int value, bool disabled)


## Rate (class)

- SignalInt model;

- Binding<int> data;

- int maxStars;

- int baseId;

- UiEvent Change;

- UiEvent HoverChange;

- bool allowHalf;

- bool readOnly;

- bool clearable;

- Binding<string> Size;

- Binding<string> Icon;

- Binding<string> VoidIcon;

- int lastHover;

- void InitRate(SignalInt m, int stars)

- Rate():this(5)

- Rate(int stars)

- Rate Max(int stars)

- int MaxStars()

- override string Kind()

- override Binding<string> SizeOf()

- override List<PropSpec> Props()

- int Value()

- void SetStars(int v)

- int HoverValue()

- override void SyncBinding()

- string SizeCls()

- int StepPx(App app)

- void SetHover(App app, int v)

- int Render(App app, int x, int y)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- void PaintStars(App app, int x, int y)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## Result (class)

- Binding<string> Text;

- Binding<string> Desc;

- string Icon;

- void InitResult(string ttl, string ds)

- Result()

- Result(string ttl, string ds)

- string Label()

- string Description()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static string StatusClass(int status)

- static string GlyphOf(string cls)

- static int ActionsY(App app, bool hasDesc)

- override string Kind()

- override List<PropSpec> Props()

- override bool SetExtra(string key, string val)


## Ribbon (class)

- static int Height(App app)

- static int Render(App app, int x, int y, int w, int h, List<RibbonCmd> cmds)

- static int tipBaseY;

- static int TabStripHeight(App app)

- static int HeightTabbed(App app)

- static int RenderTabbed(App app, int x, int y, int w, List<string> tabs, SignalInt activeTab, List<RibbonGroup> groups)

- static int HeightGroups(App app)

- static int RenderGroups(App app, int x, int y, int w, List<RibbonGroup> groups)

- static int SmallColW(App app, RibbonGroup grp)

- static int BandWidth(App app, List<RibbonGroup> groups, int bh, int rows)

- static int Band(App app, int x, int by, int w, int bh, List<RibbonGroup> groups, int firstId, int rows)

- static void PaintItem(App app, int id, RibbonItem it, int x, int y, int w, int h, bool small, StyleBox hoverStyle, StyleBox activeStyle, StyleBox textStyle, StyleBox mutedStyle, StyleBox accentStyle, StyleBox accentHoverStyle, StyleBox disabledStyle)

- static bool ItemClicked(App app, int id, RibbonItem it)


## RibbonCmd (class)

- string icon;

- string label;

- bool sep;

- bool enabled;

- bool compact;

- bool on;

- string tip;

- RibbonCmd(string icon, string label)

- RibbonCmd SetTip(string text)

- string TipText()

- static RibbonCmd Switch(string icon, string label, bool on)

- void SetOn(bool state)

- static RibbonCmd Group(string icon, string label)

- void SetEnabled(bool on)


## RibbonGroup (class)

- string title;

- List<RibbonItem> items;

- RibbonGroup(string title)

- static RibbonGroup Of(string title, List<RibbonItem> items)

- RibbonGroup Add(RibbonItem it)


## RibbonItem (class)

- string icon;

- string label;

- bool small;

- bool tile;

- bool menu;

- bool enabled;

- bool toggle;

- bool on;

- string tip;

- static RibbonItem Large(string icon, string label)

- static RibbonItem Small(string icon, string label)

- static RibbonItem Toggle(string icon, string label, bool on)

- static RibbonItem Tile(string icon, string label)

- RibbonItem SetMenu()

- RibbonItem Disable()

- RibbonItem SetTip(string text)

- void SetOn(bool state)

- void SetEnabled(bool state)

- string TipText()


## RichText (class)

- string markup;

- RichTextDocument doc;

- List<RichTextLine> lines;

- string laidMarkup;

- int laidW;

- int laidFont;

- int scroll;

- int maxScroll;

- bool bottomAnchor;

- SignalInt scrollBar;

- int fontPxOverride;

- int defaultColor;

- int rowH;

- SignalInt barPx;

- List<RichTextLinkActivated> linkHandlers;

- string pressedAction;

- RichText()

- void SetMarkup(string newValue)

- string Markup()

- void SetBottomAnchor(bool newValue)

- void SetFontPx(int newValue)

- void SetDefaultColor(int newValue)

- void OnLinkActivated(RichTextLinkActivated handler)

- override string Kind()

- int LineCount()

- int ResolveFontPx(App app)

- int ResolveDefaultColor(App app)

- override void OnMeasure(App app)

- RichTextLine NewLine(int alignment)

- void RebuildLayout(App app, int availW, int fp)

- void EnsureLayout(App app, int availW, int fp)

- override void OnPaint(App app)

- int BarW(App app)

- RichTextSegment SegmentHit(int pad, int start, int maxRows, int mx, int my)


## RichTextLine (class)

- List<RichTextSegment> segments;

- int width;

- int alignment;

- int height;

- RichTextLine(int alignment)


## RichTextSegment (class)

- string text;

- int x;

- int width;

- int color;

- int background;

- RichTextLink link;

- RichTextSegment(string text, int color, int background, RichTextLink link)


## ScrollColumn (class)

- ScrollView sv;

- int contentH;

- int barReserve;

- void InitScrollColumn()

- ScrollColumn()

- override string Kind()

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)

- override void OnPaint(App app)


## ScrollView (class)

- int offset;

- int viewH;

- int contentH;

- int barW;

- bool edgeInset;

- bool dragging;

- int dragMouseY0;

- int dragOffset0;

- int dragMo0;

- int collapseSavedOffset;

- int collapseMo;

- int lastMo;

- bool collapsePending;

- ScrollView()

- void SetEdgeInset(bool v)

- int Offset()

- int MaxOffset()

- bool Overflowing()

- void ScrollTo(int y)

- int Begin(App app, Rect area, int contentHeight)

- void End(App app, Rect area)


## Scrollbar (class)

- static int grabId=0-1;

- static int grabDy=0;

- static void RenderVertical(App app, int x, int y, int height, int contentHeight, int scrollOffset)

- static void RenderVerticalScroll(App app, int x, int y, int height, int viewH, int contentHeight, SignalInt offset)

- static void RenderVerticalScrollIn(App app, int x, int y, int height, int viewH, int contentHeight, SignalInt offset, int vx, int vy, int vw, int vh)

- static void RenderVerticalScrollCore(App app, int x, int y, int height, int viewH, int contentHeight, SignalInt offset, bool scoped, int vx, int vy, int vw, int vh)

- static void HandleWheel(App app, int vx, int vy, int vw, int vh, int contentHeight, SignalInt offset)


## SelectBox (class)

- List<SelectOption> opts;

- int selected;

- SignalInt model;

- Binding<int> data;

- int lastModel;

- int lastData;

- UiEvent Change;

- UiEvent Open;

- UiEvent Close;

- string hint;

- bool multi;

- bool clearable;

- bool open;

- int scrollY;

- SignalInt scrollSignal;

- Binding<string> Size;

- int status;

- int maxVisible;

- int placement;

- bool filterable;

- string filterText;

- Input filterBox;

- bool cascade;

- List<int> cascadePath;

- List<SelectOption> cascRoots;

- List<int> cascadeFilter;

- bool cascadeLink;

- int checkStrategy;

- bool showPath;

- bool hoverExpand;

- bool clearFilterAfter;

- List<CascadeColumnScroll> cascadeCols;

- bool treeMode;

- TreeView treeBox;

- int treeSelSeen;

- string pathText;

- List<int> filteredIdx;

- int trigX;

- int trigY;

- int trigW;

- int wid;

- int lastH;

- int hoverRow;

- UiEvent Hover;

- int cols;

- List<SelectBoxColumn> tableColumns;

- string filterHint;

- int popupW;

- bool popupWDirty;

- static int Normal()

- static int Success()

- static int Warning()

- static int Error()

- SelectBox():this("", false)

- SelectBox(string hintText):this(hintText, false)

- SelectBox(string hintText, bool isMulti)

- static SelectBox FromOptions(List<string> options, string hint)

- string OptionsText()

- void SetOptionsText(string text)

- SelectBox Columns(int n)

- SelectBox Table(List<SelectBoxColumn> columns)

- SelectBox Table(List<string> titles, List<int> widths)

- bool IsTable()

- void AddRow(List<string> cells)

- int HeaderH(App app)

- int DisplayCount()

- int RowToOpt(int row)

- int Hovered()

- string OptionLabel(int i)

- int ColumnCount()

- int RowCount()

- int Value()

- void SetIndex(int idx)

- bool IsOpen()

- void SetOpen(bool v)

- override void SyncBinding()

- void Choose(int idx)

- SelectBox Cascade()

- static int CheckAll()

- static int CheckParent()

- static int CheckChild()

- SelectBox CascadeLink(bool v)

- SelectBox CheckStrategy(int v)

- SelectBox ShowPath(bool v)

- SelectBox HoverExpand(bool v)

- SelectBox ClearFilterAfter(bool v)

- int AddBranch(string label)

- int AddChild(int parent, string label)

- SelectBox Tree()

- SelectBox TreeNodes(List<TreeNode> nodes)

- static SelectBox Multi(string hintText)

- void AddOption(string label)

- void AddOption(string label, bool disabled)

- void AddGroup(string title)

- SelectBox Clearable(bool v)

- SelectBox Status(int v)

- SelectBox Placement(int v)

- SelectBox Filterable(bool v)

- SelectBox FilterHint(string s)

- string FilterText()

- void SetFilterText(string s)

- void RebuildFilter()

- int Selected()

- int SelectCount()

- string SelectedLabel()

- bool IsChosen(int i)

- bool HasValue()

- void Select(int idx)

- void Clear()

- void ClearOptions()

- int Count()

- string Text()

- int HeadHeight(Theme t)

- int FontSize(App app, StyleBox s)

- int SizeIdx()

- string StyleClass()

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void Render(App app, int x, int y, int w)

- void RenderIn(App app, int x, int y, int w, int hIn)

- int PopupWidth(App app, Theme t, int trigWidth, int fs, int arrowSlot)

- int FilterBarH(App app)

- List<int> VisibleOpts()

- override void OnPaintOverlay(App app)

- void OverlayDismiss(App app, int popX, int popY, int w, int viewH, int ek)

- void CascadeOverlay(App app, int x, int y, int w, int h, int fs, int arrowSlot)

- void SetColScroll(int col, int v)

- int DrawCascadeCheck(App app, Canvas c, Theme t, string cls, int cx, int ry, int optH, bool on, bool partial)

- void DrawCascadeTags(App app, Canvas c, Theme t, string cls, int partState, int x, int y, int w, int h, int pad, int arrowSlot, bool showClear, int clearSize, int fs, bool leftUp)

- void ChooseCascade(int idx)

- string PathOf(int idx)

- string ValueText(int idx)

- List<int> ValueIdxs()

- List<string> ValueLabels()

- void SetChecked(int idx, bool on)

- void ApplyCheck(int idx, bool on)

- void FixupAncestors(int idx)

- bool IsPartial(int idx)

- bool HasCheckedDescendant(int idx)

- int ColScroll(int col, int contentH, int viewH)

- bool SamePath(List<int> a)

- void TreeOverlay(App app, int x, int y, int w, int h)


## SelectBoxColumn (class)

- public string title;

- public int width;

- public SelectBoxColumn(string title, int width)


## SelectOption (class)

- string label;

- bool chosen;

- List<string> cells;

- List<SelectOption> children;

- int flatIdx;

- int parentIdx;

- bool grp;

- bool disabled;

- SelectOption(string label, bool chosen)

- void Attach(SelectOption child, int idx)

- bool HasChildren()

- string Cell(int i)


## Skeleton (class)

- int Width;

- int Height;

- void InitSkeleton(int w, int h)

- Skeleton()

- Skeleton(int w, int h)

- StyleBox ResolvedStyle(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static Skeleton Line(App app, int width)

- override string Kind()

- override List<PropSpec> Props()


## Slider (class)

- int minVal;

- int maxVal;

- SignalInt model;

- SignalInt hiModel;

- Binding<int> data;

- Binding<int> dataHigh;

- int wid;

- UiEvent Change;

- UiEvent DragEnd;

- int step;

- bool range;

- bool vertical;

- bool reverse;

- bool tooltip;

- bool showTooltip;

- string tipFormat;

- bool stepMark;

- bool keyboard;

- List<SliderMarkItem> marks;

- string marksSpec;

- int dragThumb;

- int lastEdited;

- static int MarkCap()

- int Quantize(int v)

- int NearestMarkVal(int v)

- int NextMarkVal(int v, bool up)

- int StepUpVal(int v)

- int StepDownVal(int v)

- int Span()

- int FracOf(int v)

- int PxAt(int f, int pos, int len)

- int ValAt(int p, int pos, int len)

- string TipText(int v)

- void InitSlider(int lo, int hi, SignalInt m)

- Slider():this(0, 100)

- Slider(int lo, int hi)

- int Value()

- int Low()

- int High()

- void SetValue(int v)

- void SetRange(int lo, int hi)

- void AddMark(int v, string label)

- void ClearMarks()

- int MarkCount()

- int MarkValueAt(int i)

- string MarkLabelAt(int i)

- string MarksText()

- void SetMarksText(string spec)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override void SyncBinding()

- int Render(App app, int x, int y, int width)

- int RenderIn(App app, int x, int y, int width, int height)

- override List<string> Events()

- override string Kind()

- override List<PropSpec> Props()

- override void SetProp(string key, string val)

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- bool NearThumb(App app, int cx, int cy, int r)

- void RequestTip(App app, int cx, int cy, int r, int v)

- bool PaintStyled(App app, int id, int x, int y, int width, int height)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## SliderMarkItem (class)

- int value;

- string label;

- SliderMarkItem(int value, string label)


## Spin (class)

- int Size;

- int Accent;

- Binding<string> Tip;

- void InitSpin(int size)

- Spin()

- Spin(int size)

- int Diameter(App app)

- string Caption()

- int TipFont(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int OffX(int i)

- static int OffY(int i)

- static bool OnScreen(App app, int cx, int cy, int radius)

- static int PivotSafeMul(int per1000, int radius)

- override string Kind()

- override List<PropSpec> Props()


## Split (class)

- Rect first;

- Rect second;

- int handleId;

- static Split Vertical(App app, Rect area, SignalInt firstW, int minFirst, int minSecond)

- static Split Horizontal(App app, Rect area, SignalInt firstH, int minFirst, int minSecond)


## SplitPanel (class)

- int orient;

- SignalInt firstSize;

- int minFirst;

- int minSecond;

- Panel firstPane;

- Panel secondPane;

- int wid;

- int handleX;

- int handleY;

- int handleW;

- int handleH;

- static int Vertical()

- static int Horizontal()

- void InitSplitPanel(int o, int size)

- static Panel NewPane(string name)

- SplitPanel(int o, int size)

- SplitPanel(int o)

- SplitPanel()

- Panel First()

- Panel Second()

- int Size()

- void SetSize(int px)

- SplitPanel MinSizes(int first, int second)

- SplitPanel Orient(int o)

- override string Kind()

- override List<PropSpec> Props()

- override Control SlotHost(int slot)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)

- static int BarSize()

- int Clamp(int want, int total, int bar)

- override void OnPaint(App app)


## Statistic (class)

- Binding<string> Text;

- Binding<string> Value;

- void InitStatistic(string label, string amount)

- Statistic()

- Statistic(string label, string amount)

- string Label()

- string Amount()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int Height(App app)

- override string Kind()

- override List<PropSpec> Props()


## StatusBar (class)

- List<Label> items;

- int tintBg;

- string itemsSpec;

- StatusBar()

- override string Kind()

- override List<PropSpec> Props()

- override void SetProp(string key, string val)

- Label NewItem(string text)

- int AddLeft(string text)

- int AddRight(string text)

- void Clear()

- void SetItemsText(string spec)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- void SetText(int index, string text)

- void SetColor(int index, int color)

- void SetTint(int bg)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void RenderAt(App app, int x, int y, int w, int h)


## Step (class)

- string label;

- string desc;

- string icon;

- Step(string label, string desc, string icon)


## Steps (class)

- List<Step> steps;

- SignalInt model;

- Binding<int> data;

- int ErrorIndex;

- Binding<string> Size;

- string Placement;

- string Status;

- string FinishIcon;

- string ErrorIcon;

- bool Clickable;

- int baseId;

- bool idTaken;

- UiEvent Change;

- int iconPx;

- void InitSteps(SignalInt m)

- Steps(SignalInt m)

- Steps()

- override void SyncBinding()

- void AddStep(string label)

- void AddStepDesc(string label, string desc)

- void AddStepIcon(string label, string icon)

- void AddStepIconDesc(string label, string desc, string icon)

- static Steps Of(List<string> labels, SignalInt m)

- bool IsVertical()

- bool IsRight()

- int RowGap(App app)

- void EnsureIds()

- static string StatusNorm(string v)

- string StatusCls()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- int RenderAt(App app, int x, int y, int w)

- void PaintBodyHorizontal(App app, int bx, int by, int bw)

- void PaintBodyRight(App app, int bx, int by, int bw)

- void PaintBodyVertical(App app, int bx, int by, int bw)

- void PaintText(App app, int i, int current, int errorIndex, string cls, int tx, int cy, int availW)

- void HandleClick(App app, int current)

- int Radius(App app, string cls)

- int TitleFont(App app, string cls)

- static string StateClass(string cls, int index, int current, int errorIndex)

- void Circle(App app, int cx, int cy, int r, int index, int current, int errorIndex, string cls)

- void Line(App app, int x0, int y0, int x1, int y1, int index, int current, int errorIndex, string cls)

- override string Kind()

- override Binding<string> SizeOf()

- override List<PropSpec> Props()

- string ItemsText()

- void SetItemsText(string spec)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override string GetProp(string key)

- override void SetProp(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## StyledText (class)

- List<TextRun> lines;

- int scroll;

- bool busy;

- LogState sel;

- bool interactive;

- SignalInt thinkOpen;

- List<int> toolOpen;

- string busyText;

- int maxScroll;

- SignalInt scrollBar;

- static Label thinkingLbl;

- static SignalInt barSig;

- static SignalInt BarSig()

- static int BarW(App app)

- static void EnsureThinking(App app)

- StyledText()

- static int FontPx(App app)

- static int RowH(App app)

- static int SepH(App app)

- static int RowAt(int startY, int pad, int rowH, int start, int count, int mouseY)

- static string Initial(string name)

- static Avatar headAva;

- static Avatar HeadAvatar()

- static void PaintHeadRow(App app, TextRun ln, int x, int y, int rowH, int rightEdge, StyleBox timeStyle)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()


## Switch (class)

- SignalBool onSig;

- Binding<bool> data;

- Binding<int> dataValue;

- int wid;

- UiEvent Change;

- Binding<string> Size;

- bool Loading;

- bool Round;

- Binding<string> CheckedText;

- Binding<string> UncheckedText;

- Binding<string> CheckedIcon;

- Binding<string> UncheckedIcon;

- int CheckedColor;

- int UncheckedColor;

- int CheckedValue;

- int UncheckedValue;

- void InitSwitch(SignalBool m)

- Switch()

- bool HasValueAxis()

- int IntGet()

- void IntSet(int v)

- bool IsOn()

- void SetOn(bool v)

- override void SyncBinding()

- int Render(App app, int x, int y)

- override List<string> Events()

- override string Kind()

- override Binding<string> SizeOf()

- override List<PropSpec> Props()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override string GetProp(string key)

- override void SetProp(string key, string val)

- static int TrackRadius(int trackH, bool round)

- static int ExpandedTrackW(int cssW, int thumbD, int slotW, int pad)

- static int SpinnerPhase(int tickMs)

- static string SizeClsFor(string size)

- string ActiveSizeCls()

- static int SlotWidth(App app, string text, string icon, int fs)

- string TextOf(Binding<string> b)

- int PaintStyled(App app, int id, int x, int y, bool isOn)

- void DrawInner(App app, Canvas c, int rx, int ry, int rw, int rh, string text, string icon, int color, int alpha, int fs)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## TabItem (class)

- string label;

- bool closable;

- TabItem(string label, bool closable)


## Table (class)

- static void RenderHeaderCols(App app, int x, int y, List<TableColumnSpec> cols)

- static void RenderHeader(App app, int x, int y, List<string> columns, List<int> widths)

- static void RenderRowCellsSel(App app, int x, int y, List<TableCellItem> cells, bool striped, bool selected)

- static void RenderRowCells(App app, int x, int y, List<TableCellItem> cells, bool striped)

- static void RenderRow(App app, int x, int y, List<string> cells, List<int> widths, bool striped)

- static void RenderRowSel(App app, int x, int y, List<string> cells, List<int> widths, bool striped, bool selected)


## TableCellItem (class)

- public string text;

- public int width;

- public TableCellItem(string text, int width)

- public static TableCellItem Of(string text, int width)


## TableColumnSpec (class)

- public string title;

- public int width;

- public TableColumnSpec(string title, int width)

- public static TableColumnSpec Of(string title, int width)


## Tabs (class)

- List<TabItem> tabs;

- int active;

- int style;

- int orient;

- bool showAdd;

- bool closableAll;

- Binding<string> Size;

- int scroll;

- bool follow;

- bool hostModel;

- int ctxIndex;

- List<Panel> pages;

- string itemsSpec;

- int hdrH;

- int trackW;

- int changedIndex;

- int closedIndex;

- int addedIndex;

- List<int> closedQueue;

- SignalBool menuOpen;

- int menuTab;

- int menuX;

- int menuY;

- SignalInt menuAction;

- SignalInt menuSub;

- int menuBaseId;

- UiEvent TabChanged;

- UiEvent TabClosed;

- UiEvent TabAdded;

- static string lang="zh";

- static string TT(string en, string zh)

- static int Line()

- static int Card()

- static int Segment()

- static int Horizontal()

- static int Vertical()

- static Tabs CreateVertical(int style)

- Tabs():this(0, 0)

- Tabs(int style):this(style, 0)

- Tabs(int style, int orient)

- void SetShowAdd(bool on)

- void SetHostModel(bool on)

- override string Kind()

- override Binding<string> SizeOf()

- override string StyleType()

- override List<PropSpec> Props()

- override void SetProp(string key, string val)

- Panel Page(int i)

- int PageCount()

- void DropPage(int i)

- override Control SlotHost(int slot)

- void SetItemsText(string spec)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)

- override void OnPaint(App app)

- int TakeContext()

- void Add(string label, bool canClose)

- void AddPage(string label, Control content, bool canClose)

- void AddPage(string label, Control content)

- void Clear()

- int Count()

- int Active()

- string ActiveLabel()

- void SetLabel(int i, string label)

- string LabelAt(int i)

- void Select(int i)

- int ClosedIndex()

- int TakeChanged()

- int TakeClosed()

- int TakeAdded()

- void AddNew()

- void EmitClosed(int i)

- void ReindexActive(int i)

- bool Closable(int i)

- int ClosableCount(int keep)

- void CloseAt(int i)

- void CloseAll()

- void CloseOthers(int keep)

- void CloseBulk(int keep)

- string TabClasses()

- static int StateOf(bool hover, bool selected)

- int HeaderHeight(App app)

- int TabWidth(App app, int i)

- int ContentWidth(App app)

- void Render(App app, int x, int y, int totalWidth)

- void PumpClosed(App app)

- void RenderTabStrip(App app, int x, int y, int viewW, int tabH, int count, string tcls, bool leftUp, bool rightUp)

- void RenderAddButton(App app, int x, int y, int totalWidth, int addW, int tabH, bool leftUp)

- void RenderOverflowChevrons(App app, int x, int y, int viewW, int chevW, int tabH, int maxScroll, bool leftUp)

- void RenderVertical(App app, int x, int y, int w, int h)

- void ApplyMenuAction()

- void EmitMenu(App app)


## Tag (class)

- Binding<string> Text;

- string Icon;

- bool Closable;

- bool Checkable;

- Binding<bool> Checked;

- int wid;

- UiEvent Close;

- UiEvent Change;

- void InitTag(string label)

- Tag()

- Tag(string label)

- string Label()

- StyleBox ResolvedStyle(App app)

- int AutoWidth(App app)

- int Render(App app, int x, int y)

- int RenderW(App app, int x, int y, int w)

- bool IsChecked()

- static int Width(App app, string text, string cls)

- static int Height(App app, string cls)

- static void Chip(App app, int x, int y, string text, string cls)

- static int ChipClosable(App app, int x, int y, string text, string cls)

- static int ClosableWidth(App app, string text, string cls)

- static void ChipFlow(App app, int x, int wrapW, string text, string cls, int cx, int cy, out int nx, out int ny)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## TextArea (class)

- SignalString model;

- Binding<string> data;

- string hint;

- int cursorPos;

- int selAnchor;

- int scrollY;

- int wid;

- bool skipKeys;

- string cacheTxt;

- List<string> cacheLines;

- List<TextAreaRow> rows;

- string wrapTxt;

- int wrapW;

- int wrapFs;

- string errMsg;

- bool readOnly;

- bool autoHeight;

- App liveApp;

- int MaxLen;

- bool ShowCount;

- bool CountGraphemes;

- string FieldStatus;

- bool Round;

- InputFilterFn Filter;

- UiEvent Change;

- void InitTextArea(SignalString sig, string hintText)

- TextArea():this("")

- TextArea(string hintText)

- string GetText()

- TextArea ReadOnly(bool on)

- bool IsReadOnly()

- TextArea AutoHeight(bool on)

- static int LineHeight(App app)

- int ContentHeight(App app)

- int Id()

- void SkipKeysOnce()

- void SetText(string v)

- override void SyncBinding()

- void PushBinding()

- void Edited()

- void SetError(string msg)

- void ClearError()

- void SetStatus(string v)

- int CountOf(string s)

- int CountValue()

- void SelectAll()

- void Clear()

- void Focus()

- void Blur()

- void ScrollToEnd()

- void ScrollToStart()

- void ClampCursor()

- bool HasSel()

- int SelStart()

- int SelEnd()

- string SelText()

- void DeleteSel()

- bool Insert(string s)

- bool DeleteCandidate(string cand)

- List<string> Lines()

- void Wrap(int maxW, int fs)

- void WrapLine(string ln, int abs, int maxW, int fs)

- int RowOfPos(int pos)

- string RowText(int i)

- void EnsureRows()

- void HandleInput(App app)

- int PosFromMouse(App app, int innerX, int innerY, int lineH, int fs)

- int Render(App app, int x, int y, int w, int h)

- override string Kind()

- override string StyleType()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- int BottomRowH(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## TextAreaRow (class)

- int start;

- int len;

- TextAreaRow(int start, int len)


## TextRun (class)

- string text;

- int color;

- int kind;

- int meta;

- int owner;

- string ava;

- string tagCls;

- string status;

- int pct;

- string time;

- bool last;

- static TextRun Create(string text, int color, int kind)


## Timeline (class)

- List<TimelineItem> items;

- Binding<string> Size;

- void InitTimeline()

- Timeline()

- void Add(TimelineItem it)

- static Timeline Of(List<TimelineItem> its)

- void AddItem(string time, string title, string desc)

- void AddItemIcon(string time, string title, string desc, string cls, string icon)

- int Count()

- bool IsHorizontal()

- bool IsRight()

- override Binding<string> SizeOf()

- string FullCls(App app)

- static int DotRadius(App app, string cls)

- static int TimeFont(App app, string cls)

- static int TitleFont(App app, string cls)

- static int DescFont(App app, string cls)

- static int RowHeightFor(App app, string cls)

- static int RowHeight(App app)

- override void OnMeasure(App app)

- int HBlockHeight(App app, string cls)

- override void OnPaint(App app)

- void PaintLine(Canvas c, bool right, int contentR, int textL, int y, string text, int color, int font)

- void PaintTail(Canvas c, int x, int y, int lw, int len, int color, bool pending, bool horizontal, App app)

- void PaintMarker(App app, Canvas c, StyleBox dot, string icon, int cx, int cy, int r)

- void PaintHorizontal(App app, string cls, int n, int r, int lw, int timeFont, int titleFont, int descFont)

- void PaintWrapped(Canvas c, string text, int x, int y, int right, int color, int font, int bottom)

- string ItemsText()

- void SetItemsText(string spec)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override string Kind()

- override List<PropSpec> Props()


## TimelineItem (class)

- string time;

- string title;

- string description;

- string cls;

- string icon;

- bool pending;

- TimelineItem(string time, string title, string description):this(time, title, description, "", "")

- TimelineItem(string time, string title, string description, string cls):this(time, title, description, cls, "")

- TimelineItem(string time, string title, string description, string cls, string icon)


## ToastItem (class)

- string text;

- int type;

- int bornMs;

- int lifeMs;

- bool closing;

- int closeMs;

- int curY;

- int curX;

- ToastItem(string text, int type, int lifeMs)


## ToolStrip (class)

- List<Button> items;

- List<ToolItemCallback> itemCbs;

- UiEvent ItemClick;

- int tintBg;

- bool iconOnly;

- string itemsSpec;

- ToolStrip()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- void OnItemClick(ToolItemCallback cb)

- void FireItem(int index)

- override string Kind()

- override List<PropSpec> Props()

- override void SetProp(string key, string val)

- int Count()

- Button ItemAt(int i)

- Button NewItem(string text, string icon)

- int Add(string text, string icon)

- int Add(string text)

- void Clear()

- int AddRight(string text, string icon)

- int AddRight(string text)

- int AddSeparator()

- void SetItemsText(string spec)

- void SetIconOnly(bool on)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- void SetTint(int bg)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void RenderAt(App app, int x, int y, int w, int h)


## Tooltip (class)

- static int FontSize(App app)

- static int Top()

- static int Bottom()

- static int Left()

- static int Right()

- static void TriDown(Canvas c, int cx, int topY, int half, int h, int color)

- static void TriUp(Canvas c, int cx, int topY, int half, int h, int color)

- static void TriLeft(Canvas c, int leftX, int cy, int half, int w, int color)

- static void TriRight(Canvas c, int leftX, int cy, int half, int w, int color)

- static List<string> WrapLines(string text, int fs, int maxW)

- static int TipCapH(App app)

- static Rect BoxRect(App app, int anchorX, int anchorY, string text, int placement, int maxW)

- static int MaxScroll(App app, string text, int maxW)

- static void RenderScroll(App app, int anchorX, int anchorY, string text, int placement, int maxW, int scrollOff)

- static void RenderAt(App app, int anchorX, int anchorY, string text, int placement, int maxW)

- static void RenderBox(App app, int x, int y, int w, int h, string text, int placement, int maxW)

- static void RequestBox(App app, int x, int y, int w, int h, string text, int placement, int maxW)

- static void RenderRich(App app, int x, int y, string title, string desc)

- static void Render(App app, int anchorX, int anchorY, string text)

- static string pendingText;

- static int pendingX;

- static int pendingY;

- static int pendingPlace;

- static int pendingMaxW;

- static void Request(int anchorX, int anchorY, string text, int placement, int maxW)

- static int DefaultDelayMs()

- static void Cancel()

- static TooltipRequest Suspend()

- static void Restore(TooltipRequest state)

- static void Flush(App app)


## TooltipRequest (class)

- string text;

- int x;

- int y;

- int placement;

- int width;


## Transfer (class)

- List<T> source;

- List<T> target;

- TransferPane left;

- TransferPane right;

- CellOf<T> label;

- IconOf<T> iconOf;

- bool searchable;

- string leftHint;

- string rightHint;

- bool oneWay;

- string emptyText;

- string selectAllText;

- string totalText;

- string selectedText;

- string clearText;

- int pendRow;

- int pendPane;

- bool pendOn;

- bool wasPaint;

- bool lastDrag;

- Button rightBtn;

- Button leftBtn;

- UiEvent Change;

- UiEvent SelectChanged;

- int opW;

- int opGap;

- int opH;

- int searchH;

- int headH;

- void InitTransfer()

- Transfer()

- Transfer(List<T> src, List<T> tgt)

- override string Kind()

- override string StyleType()

- Transfer<T> Bind(List<T> src, List<T> tgt)

- void Refresh()

- void Invalidate()

- Transfer<T> WithLabel(CellOf<T> text)

- Transfer<T> WithIcon(IconOf<T> icon)

- Transfer<T> WithSearch()

- Transfer<T> WithSearchHint(string hint)

- Transfer<T> WithSearchHints(string lh, string rh)

- Transfer<T> WithOneWay()

- Transfer<T> WithEmptyText(string text)

- Transfer<T> WithHeaderText(string selectAll, string total, string selected, string clear)

- Transfer<T> WithOperationIcons(string rightIcon, string leftIcon)

- Transfer<T> WithOperation(string rightLabel, string leftLabel)

- Button MoveRightButton()

- Button MoveLeftButton()

- int SourceCount()

- int TargetCount()

- int VisibleCount(bool isSource)

- List<T> Source()

- List<T> Target()

- bool IsChecked(T item)

- bool FlagAt(TransferPane p, int i)

- void SetChecked(T item, bool on)

- void SetFlag(TransferPane p, int i, bool on, int n)

- List<T> Checked()

- List<T> CheckedRight()

- int CheckedSource()

- int CheckedTarget()

- List<T> CheckedIn(List<T> data, TransferPane p)

- int CheckedCount(TransferPane p, int count)

- void MoveRight()

- void MoveLeft()

- void ClearChecked(bool all)

- void ClearAll()

- void ClearPane(TransferPane p, List<T> data, bool all)

- void SelectAll(bool isSource, bool on)

- void SelectAllVisible(bool isSource)

- void Move(TransferPane from, List<T> fd, TransferPane to, List<T> td)

- void SyncFlags(TransferPane p, int n)

- List<int> Order(TransferPane p, List<T> data)

- string LabelOf(T item)

- Input SearchBox(bool isSource)

- Transfer<T> SetQuery(bool isSource, string q)

- string Query(bool isSource)

- bool RowCenter(bool isSource, int dataIdx, out int cx, out int cy)

- void Render(App app, int x, int y, int w, int h)

- void ComputeMetrics(App app)

- bool HeadSelectAll(App app, TransferPane p, List<int> order, bool isSource, int hx, int iy, int headH, int hy, int fs, int fg)

- bool FlushPaint(TransferPane p)

- static int PaintRange(TransferPane p, int a, int b, bool val)

- int CheckRange(bool isSource, int a, int b, bool val)

- bool InPaintBand(TransferPane p, int i)

- bool RenderPane(App app, TransferPane p, List<T> data, int x, int y, int w, int h, bool isSource)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override List<PropSpec> Props()


## TransferPane (class)

- List<bool> flags;

- SignalInt scroll;

- List<int> order;

- Input search;

- string query;

- int paintStart;

- int paintEnd;

- bool paintVal;

- int bodyX;

- int bodyY;

- int bodyW;

- int bodyH;

- int rowHPx;

- int orderFor;

- string orderQuery;

- TransferPane()


## TreeNode (class)

- string label;

- int depth;

- bool isDir;

- bool expanded;

- string icon;

- string path;

- string payload;

- bool checked;

- static TreeNode Dir(string label, int depth, bool expanded)

- static TreeNode File(string label, int depth)

- static TreeNode FileIcon(string label, int depth, string icon)

- TreeNode WithPayload(string p)


## TreeView (class)

- List<TreeNode> data;

- SignalInt sel;

- SignalInt scroll;

- Input filter;

- bool checks;

- bool multi;

- List<int> marks;

- NodeOf tpl;

- bool rowsStale;

- int builtFor;

- int ctxRow;

- int ctxX;

- int ctxY;

- string Empty;

- UiEvent Select;

- UiEvent Activate;

- UiEvent Expand;

- UiEvent Collapse;

- UiEvent Context;

- UiEvent Check;

- UiEvent Press;

- UiEvent Move;

- bool reorder;

- bool pressed;

- int dragRow;

- bool dragging;

- int dropAt;

- bool dropDone;

- void InitTree()

- TreeView()

- TreeView(NodeOf template)

- override string Kind()

- override List<PropSpec> Props()

- override string StyleType()

- TreeView Bind(List<TreeNode> nodes)

- void Refresh()

- TreeView BindSel(SignalInt s)

- TreeView WithFilter(Input f)

- TreeView WithChecks()

- TreeView WithMultiSelect()

- TreeView WithReorder()

- int MoveFrom()

- int MoveTo()

- int Count()

- void ScrollToEnd()

- List<TreeNode> Nodes()

- bool Has(int row)

- TreeNode NodeAt(int row)

- int SelectedIndex()

- bool HasSelection()

- TreeNode Selected()

- void SelectIndex(int row)

- void ClearSelection()

- bool IsMarked(int row)

- List<int> SelectedIndices()

- int SelectionCount()

- int ContextRow()

- int ContextX()

- int ContextY()

- TreeView OnSelect(Action a)

- TreeView OnActivate(Action a)

- TreeView OnExpand(Action a)

- TreeView OnCollapse(Action a)

- TreeView OnContext(Action a)

- TreeView OnCheck(Action a)

- TreeView OnPress(Action a)

- TreeView OnMove(Action a)

- TreeView EmptyText(string text)

- void ExpandAll()

- void CollapseAll()

- void SetAllChecked(bool on)

- void Toggle(int row)

- List<int> CheckedIndices()

- List<string> CheckedLabels()

- int CheckedCount()

- bool Templated()

- int RowHeight(App app)

- int FilterHeight(App app)

- bool Shown(int row)

- int ContentHeight(App app)

- int RowExtent(App app, Control row)

- void BuildRows(App app)

- Rect PaintFilter(App app)

- int RowInteract(App app, int index, Rect row)

- void NoteSelfDamage(App app)

- bool PaintCheck(App app, int row, int cx, int rowY, int rowH, int box)

- void PaintRow(App app, int row, Rect box, int checkCol, bool active)

- int ScrollOffset(App app, Rect rows, int contentH)

- void UpdateDrag(App app, Rect rows)

- void PaintDropLine(App app, Rect rows, int y)

- void PaintRows(App app, Rect rows)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void PaintEmpty(App app, Rect rows)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- static int SubtreeEnd(List<TreeNode> nodes, int idx)

- static int CheckState(List<TreeNode> nodes, int idx)

- static void ToggleCheck(List<TreeNode> nodes, int idx)


## Typography (class)

- Binding<string> Text;

- void InitTypography(string txt)

- Typography()

- Typography(string txt)

- string Str()

- static string HeadingClass(int level)

- static Typography H(string txt, int level)

- static Typography P(string txt)

- override string StyleType()

- StyleBox ResolvedStyle(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int Heading(App app, int x, int y, string text, int level)

- static int Paragraph(App app, int x, int y, string text)

- override string Kind()

- override List<PropSpec> Props()


## Upload (class)

- List<UploadFile> Files;

- bool AutoUpload;

- bool Multiple;

- int Max;

- bool Dragger;

- string DragText;

- string Tip;

- string TriggerText;

- bool ShowTrigger;

- bool ShowRemoveButton;

- bool ShowDownloadButton;

- bool PictureCard;

- bool Thumbnail;

- string Accept;

- string Action;

- ExternalCallPolicy CallPolicy;

- string FieldName;

- UploadBeforeFn BeforeUpload;

- UploadRequestFn CustomRequest;

- UiEvent Change;

- UiEvent UploadStart;

- UiEvent Finish;

- UiEvent Error;

- UiEvent Remove;

- UiEvent Download;

- UiEvent Exceed;

- UploadFile LastItem;

- int baseId;

- static int uidCounter;

- App uiApp;

- FilePicker picker;

- List<UploadJob> jobs;

- int dropX;

- int dropY;

- int dropW;

- int dropH;

- void InitUpload()

- Upload()

- Upload WithBeforeUpload(UploadBeforeFn f)

- Upload WithCallPolicy(ExternalCallPolicy policy)

- Upload WithCustomRequest(UploadRequestFn f)

- Upload WithAction(string url, string fieldName)

- override string Kind()

- int BaseId()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- void SetFiles(List<UploadFile> files)

- int AddPaths(List<string> paths)

- bool TryAddPath(string path)

- UploadFile AddFile(string name, string path, string url)

- bool Admit(UploadFile f)

- bool AcceptAllows(string ext)

- static bool IsImageExt(string ext)

- void Submit()

- void StartItem(UploadFile f)

- void DropJob(UploadJob job)

- void FailItem(UploadFile f, string message)

- void ApplyProgress(UploadJob job)

- void ApplyDone(UploadJob job)

- UploadFile Find(int id)

- void SetItemPercent(int id, int percent)

- void SetItemStatus(int id, int status, string message)

- void SetItemUrl(int id, string url)

- void SetItemThumb(int id, string thumb)

- bool RemoveItem(int id)

- void Clear()

- bool WantsDropAt(int x, int y)

- int AcceptDropped(List<string> paths)

- void EnsurePicker()

- void OpenDialog()

- void CloseDialog()

- FilePicker Picker()

- void OnPicked()

- List<FileFilter> BuildFilters()

- bool DialogOpen()

- bool DialogNeedsRedraw()

- bool OwnsEventWindow(nint evh)

- nint DialogWindowHandle()

- bool ApplyEventWindow(nint evh)

- void RenderDialog()

- int TakeDialogResult()

- void SweepDialogClosed()

- int StyleGap(App app)

- int RowH(App app)

- StyleBox TriggerStyle(App app, bool off)

- int TriggerH(App app)

- int TriggerW(App app)

- int DraggerH(App app)

- static string Ellipsize(string s, int maxW, int fs)

- void DashedRect(Canvas c, int x, int y, int w, int h, int color, int dash, int gapLen, int thickness, int radius)

- void BlitCover(Canvas c, string src, int dx, int dy, int size)

- int Render(App app, int x, int y, int w)

- void PaintTrigger(App app, int x, int y, int tw, int th, bool off)

- void PaintDragger(App app, int x, int y, int w, int h, bool off)

- int PaintRows(App app, int x, int y, int w, bool off)

- int PaintTiles(App app, int x, int y, int w, bool off)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## UploadFile (class)

- int id;

- string name;

- string url;

- string thumb;

- string path;

- string message;

- string response;

- int size;

- int status;

- int percent;

- string ext;

- Upload owner;

- UploadFile()

- static string ExtOf(string fileName)


## UploadJob (class)

- static nint lockHandle;

- static List<UploadJob> pending;

- static bool workerStarted;

- Upload host;

- UploadFile item;

- int itemId;

- ExternalCallPolicy callPolicy;

- bool tls;

- string hostName;

- int port;

- string urlPart;

- string field;

- int lastPct;

- int doneCode;

- string doneBody;

- string doneErr;

- bool donePosted;

- static bool Enqueue(UploadJob job)

- static void Run()

- static UploadJob Take()

- void Init(Upload h, UploadFile f, string url, string fieldName, ExternalCallPolicy policy)

- async int Execute()

- void Probe(int sent, int total)

- void ReportDone(int code, string body, string err)

- void FlushProgress()

- void FlushDone()


## UploadStatus (class)

- static int Pending()

- static int Uploading()

- static int Success()

- static int Error()


## VirtualList (class)

- ScrollView scroll;

- List<string> items;

- int selected;

- int rowH;

- int activated;

- RowAt rowAt;

- int rowCount;

- bool rowsStale;

- int builtFirst;

- int builtLast;

- int rowSeqBase;

- int tipIdx;

- int tipSinceMs;

- VirtualList()

- override string Kind()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int RowIdStride()

- void SetItems(List<string> rows)

- void Add(string row)

- void SetRows(int count, RowAt build)

- void Refresh()

- void SetRowHeight(int h)

- int Count()

- int Selected()

- string SelectedText()

- int TakeActivated()

- int ResolveRowH(App app)

- void BuildVisible(App app, int first, int last)

- void Render(App app, int x, int y, int w, int h)

- void HitSelect(App app, Rect area, int rh, int dy)


## Watermark (class)

- Binding<string> Content;

- string Src;

- int Color;

- int FontPx;

- int Rotate;

- int GapX;

- int GapY;

- int OffsetX;

- int OffsetY;

- int LineHeight;

- int TileW;

- int TileH;

- int TextAlign;

- int Weight;

- bool NoClip;

- void InitWatermark()

- Watermark()

- Watermark(string content)

- int RotClamped()

- int EffectiveFont(App app, StyleBox s)

- int EffectiveColor(StyleBox s)

- override void OnPaint(App app)

- void PaintTextTiles(App app, Canvas c, StyleBox s)

- void PaintTextTile(Canvas c, List<string> lines, int tx, int ty, int tileW, int lineH, int fs, int color, int rot, int bold, double cs, double sn)

- void PaintImageTiles(App app, Canvas c, string src)

- override string Kind()

- override List<PropSpec> Props()


## Wizard (class)

- static string lang="zh";

- static string notice="";

- static SignalInt listScroll;

- static SignalInt catScroll;

- static SignalInt tplScroll;

- static SignalInt Scroller(SignalInt s)

- static int ScrollOffset(App app, int x, int y, int w, int h, int contentH, SignalInt off)

- static string TW(string en, string zh)

- static string OsOf(string tid)

- static bool TplSupports(WizardTemplate tpl, string tid)

- static string OsName(string os)

- static string PrettyPlats(string spec)

- string iTitle;

- bool iCategorized;

- List<WizardItem> iItems;

- SignalInt iSel;

- Input iName;

- Input iLoc;

- bool iShowLoc;

- List<string> iCatNames;

- SignalInt iCatSel;

- List<WizardTemplate> iTpls;

- SignalInt iTplSel;

- List<WizardTarget> iTargets;

- List<string> iGroups;

- SignalInt iSizeSel;

- Input iSizeW;

- Input iSizeH;

- SignalInt iDevSel;

- SignalInt iDevRot;

- Wizard()

- override string Kind()

- override List<PropSpec> Props()

- Wizard(string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc)

- Wizard(string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc, SignalInt sizeSel)

- Wizard(string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc, SignalInt sizeSel, Input sizeW, Input sizeH)

- static List<int> SizePresetW()

- static List<int> SizePresetH()

- static string SizePresetName(int i)

- static bool SketchHas(string sketch, string token)

- static void RenderPreview(App app, int x, int y, int w, int h, WizardTemplate tpl, int devW, int devH)

- static void RenderSketch(App app, int x, int y, int w, int h, WizardTemplate tpl)

- static int ParseDim(string s)

- static Wizard Categorized(string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames)

- static Wizard Categorized(string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames, SignalInt sizeSel, Input sizeW, Input sizeH)

- static Wizard Categorized(string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames, SignalInt sizeSel, Input sizeW, Input sizeH, SignalInt devSel, SignalInt devRot)

- int Show(App app)

- static int Render(App app, string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc, SignalInt sizeSel)

- static int Render(App app, string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc, SignalInt sizeSel, Input sizeW, Input sizeH)

- static void RenderList(App app, int x, int y, int w, int h, List<WizardItem> items, SignalInt sel)

- static int RenderCategorized(App app, string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames)

- static int RenderCategorized(App app, string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames, SignalInt sizeSel, Input sizeW, Input sizeH)

- static int RenderCategorized(App app, string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames, SignalInt sizeSel, Input sizeW, Input sizeH, SignalInt devSel, SignalInt devRot)


## WizardItem (class)

- string name;

- string icon;

- string desc;

- WizardItem(string n, string ic, string d)


## WizardTarget (class)

- string name;

- int selected;

- string id;

- WizardTarget(string n, int s)

- WizardTarget(string n, int s, string tid)


## WizardTemplate (class)

- string cat;

- string name;

- string icon;

- string desc;

- bool sizeable;

- int defW;

- int defH;

- bool round;

- string preview;

- string sketch;

- string deviceId;

- string caps;

- string plats;

- WizardTemplate(string c, string n, string ic, string d)

- WizardTemplate Shape(bool canSize, int w, int h, bool isRound)

- WizardTemplate Look(string previewPath, string sketchSpec)

- WizardTemplate Device(string id)

- WizardTemplate Caps(string spec)

- WizardTemplate Plats(string spec)


## Control (delegate)

`delegate Control RowOf<T>(T item);`


## Control (delegate)

`delegate Control NodeOf(TreeNode node);`


## Control (delegate)

`delegate Control RowAt(int index);`


## bool (delegate)

`delegate bool DateFilter(DateTime d);`


## bool (delegate)

`delegate bool NumValidator(int value);`


## bool (delegate)

`delegate bool RowGate<T>(T item);`


## bool (delegate)

`delegate bool UploadBeforeFn(UploadFile item);`


## string (delegate)

`delegate string FormCheck(string value);`


## string (delegate)

`delegate string InputFilterFn(string candidate);`


## string (delegate)

`delegate string NumFormatter(string text);`


## string (delegate)

`delegate string NumParser(string text);`


## string (delegate)

`delegate string CellOf<T>(T item);`


## string (delegate)

`delegate string MenuLabelOf(MenuItem item);`


## string (delegate)

`delegate string MenuIconOf(MenuItem item);`


## string (delegate)

`delegate string MenuExpandIconOf(MenuItem item, bool expanded);`


## string (delegate)

`delegate string IconOf<T>(T item);`


## void (delegate)

`delegate void RowDragOut(int index);`


## void (delegate)

`delegate void RichTextLinkActivated(string action);`


## void (delegate)

`delegate void ToolItemCallback(int index);`


## void (delegate)

`delegate void UploadRequestFn(UploadFile item);`


## FieldAnchorMode (enum)

- None = =0

- TopLeft = =1

- TopCenter = =2

- TopRight = =3

- CenterLeft = =4

- Center = =5

- CenterRight = =6

- BottomLeft = =7

- BottomCenter = =8

- BottomRight = =9

- Stretch = =10


## FieldPositionMode (enum)

- Flow = =0

- Float = =1


## QrEcl (enum)

- L = =0

- M = =1

- Q = =2

- H = =3
