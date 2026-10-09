# Gui.Hmi

> 源码: `packages/Zan.Industrial/src/Gui/Hmi/Alarm.zan`, `packages/Zan.Industrial/src/Gui/Hmi/EquipPanel.zan`, `packages/Zan.Industrial/src/Gui/Hmi/Gauge.zan`, `packages/Zan.Industrial/src/Gui/Hmi/Indicator.zan`, `packages/Zan.Industrial/src/Gui/Hmi/IoTag.zan`, `packages/Zan.Industrial/src/Gui/Hmi/NumPad.zan`, `packages/Zan.Industrial/src/Gui/Hmi/Trend.zan`


## AlarmBanner (class)

- AlarmBuffer buffer;

- int wid;

- int ackId;

- UiEvent Ack;

- AlarmBanner():this(new AlarmBuffer())

- void Bind(AlarmBuffer buf)

- AlarmBuffer Buffer()

- AlarmBanner(AlarmBuffer buf)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## AlarmBuffer (class)

- List<AlarmItem> items;

- int limit;

- AlarmBuffer()

- int Count()

- AlarmItem At(int i)

- int UnackedCount()

- AlarmItem Newest()

- AlarmItem Raise(string tagName, string text, int prio)

- void AckAll()

- void Purge()

- void Scan(TagTable io)

- AlarmItem ActiveOf(string tagName)


## AlarmItem (class)

- string stamp;

- string point;

- string message;

- int priority;

- bool acked;

- bool active;

- AlarmItem(string tagName, string text, int prio)

- string Stamp()

- string Point()

- string Message()

- int Priority()

- bool IsAcked()

- bool IsActive()

- void Ack()

- void Clear()

- static int ColorOf(App app, int prio)

- static string LevelOf(int prio)


## AlarmList (class)

- AlarmBuffer buffer;

- int wid;

- int rowBaseId;

- int selected;

- UiEvent RowClick;

- AlarmList():this(new AlarmBuffer())

- void Bind(AlarmBuffer buf)

- AlarmBuffer Buffer()

- AlarmList(AlarmBuffer buf)

- int Selected()

- AlarmItem SelectedItem()

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## Bargraph (class)

- string caption;

- IoTag tag;

- SignalInt raw;

- int scale;

- string unit;

- int rawMin;

- int rawMax;

- int loLimit;

- int hiLimit;

- int orient;

- void InitBar(string text, IoTag t, SignalInt v, int sc, string u, int lo, int hi)

- Bargraph(string text, IoTag t)

- Bargraph(string text, SignalInt v, int lo, int hi)

- Bargraph(string text)

- Bargraph()

- static int Vertical()

- static int Horizontal()

- SignalInt Value()

- Bargraph Orient(int o)

- Bargraph Range(int lo, int hi)

- Bargraph Limits(int lo, int hi)

- void Bind(IoTag t)

- override string Kind()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- int Fraction()

- override void OnPaint(App app)

- int PosOf(int v, int len)

- void PaintLimitsV(App app, int x, int y, int w, int h)

- void PaintLimitsH(App app, int x, int y, int w, int h)


## DeviceCard (class)

- string title;

- SignalInt state;

- List<DeviceCardRow> rows;

- int wid;

- UiEvent Click;

- DeviceCard():this("")

- DeviceCard(string name)

- SignalInt State()

- void Set(int st)

- int RowCount()

- DeviceCard AddRow(DeviceCardRow r)

- DeviceCard Row(string label, IoTag t)

- override string Kind()

- override List<string> Events()

- override List<PropSpec> Props()

- static string TextOf(int st)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## DeviceCardRow (class)

- public string name;

- public IoTag tag;

- public DeviceCardRow(string name, IoTag tag)

- public static DeviceCardRow Of(string name, IoTag tag)


## Digital (class)

- string caption;

- IoTag tag;

- SignalInt raw;

- int scale;

- string unit;

- void InitDigital(string text, IoTag t, SignalInt v, int sc, string u)

- Digital(string text, IoTag t)

- Digital(string text, SignalInt v, int sc, string u)

- Digital(string text)

- Digital()

- SignalInt Value()

- void Bind(IoTag t)

- override string Kind()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- override void OnPaint(App app)


## EquipPanel (class)

- List<string> names;

- List<DeviceCard> cards;

- int cardW;

- int cardH;

- int gapPx;

- int cellW;

- int cellH;

- int cellGap;

- int lastArrangeW;

- EquipPanel()

- int Count()

- DeviceCard AddDevice(string name)

- DeviceCard At(int i)

- DeviceCard Of(string name)

- void Set(string name, int state)

- EquipPanel CardSize(int w, int h)

- void SetItemsText(string spec)

- override string Kind()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)

- override void OnPaint(App app)


## Gauge (class)

- string caption;

- IoTag tag;

- SignalInt raw;

- int scale;

- string unit;

- int rawMin;

- int rawMax;

- int loLimit;

- int hiLimit;

- int startDeg;

- int sweepDeg;

- void InitGauge(string text, IoTag t, SignalInt v, int sc, string u, int lo, int hi)

- Gauge(string text, IoTag t)

- Gauge(string text, SignalInt v, int lo, int hi)

- Gauge(string text)

- Gauge()

- SignalInt Value()

- void Bind(IoTag t)

- Gauge Range(int lo, int hi)

- Gauge Limits(int lo, int hi)

- override string Kind()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- int AngleOf(int v)

- override void OnPaint(App app)

- static int CosDeg(int deg)

- static int SinDeg(int deg)

- static int SinQ(int deg)


## IoTag (class)

- string tagName;

- string unit;

- int scale;

- SignalInt raw;

- int rawMin;

- int rawMax;

- int loLimit;

- int hiLimit;

- bool good;

- IoTag(string name, string engUnit, int scaleCounts)

- IoTag(string name):this(name, "", 1)

- string Name()

- string Unit()

- int Scale()

- SignalInt Value()

- int Raw()

- bool IsGood()

- int Min()

- int Max()

- IoTag Range(int lo, int hi)

- IoTag Limits(int lo, int hi)

- void SetRaw(int v)

- void SetBad()

- int Alarm()

- int Percent()

- string Text()

- string TextWithUnit()

- static string Format(int v, int scale)

- static int Digits(int scale)


## Led (class)

- string caption;

- SignalInt state;

- bool blink;

- int dotSize;

- void InitLed(string text, SignalInt st)

- Led(string text, SignalInt st)

- Led(string text)

- Led()

- int State()

- void Set(int st)

- void SetBlink(bool on)

- void SetCaption(string s)

- SignalInt Value()

- override string Kind()

- override List<PropSpec> Props()

- static int ColorOf(App app, int st)

- override void OnMeasure(App app)

- int Dot(App app)

- override void OnPaint(App app)


## NumPad (class)

- SignalString entry;

- IoTag target;

- int baseId;

- bool allowSign;

- bool allowDot;

- UiEvent Enter;

- UiEvent Cancel;

- NumPad()

- SignalString Entry()

- string Text()

- void SetText(string s)

- void Clear()

- NumPad Target(IoTag t)

- override string Kind()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- string KeyAt(int i)

- override void OnPaint(App app)

- void PaintKey(App app, int id, int x, int y, int w, int h, string label, int bg, int fg)

- void Press(string label)

- void Commit()

- static int Parse(string s, int scale)


## TagTable (class)

- List<IoTag> tags;

- int periodMs;

- TagTable()

- int Count()

- IoTag At(int i)

- int Period()

- void SetPeriod(int ms)

- IoTag Add(string name, string unit, int scale)

- IoTag Add(string name)

- IoTag Of(string name)

- IoTag Ensure(string name)

- void Set(string name, int rawValue)

- void SetBad(string name)

- int AlarmCount()


## Trend (class)

- string caption;

- List<TrendPen> pens;

- int rawMin;

- int rawMax;

- int scale;

- int window;

- bool showGrid;

- Trend(string text, int lo, int hi, int sc, int win)

- Trend(string text):this(text, 0, 100, 1, 120)

- Trend():this("", 0, 100, 1, 120)

- int PenCount()

- TrendPen PenAt(int i)

- void SetGrid(bool on)

- Trend Range(int lo, int hi)

- TrendPen Add(string title, IoTag t, int color)

- void Sample()

- override string Kind()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- int PlotY(int v, int ph)


## TrendPen (class)

- string title;

- IoTag tag;

- SignalInt raw;

- int color;

- List<int> samples;

- int capacity;

- int head;

- int filled;

- TrendPen(string name, IoTag t, int col, int cap)

- string Title()

- int Color()

- int Count()

- IoTag Point()

- void Sample()

- void Push(int v)

- int At(int i)
