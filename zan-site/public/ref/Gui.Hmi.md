# Gui.Hmi

> 源码: `stdlib/Gui/Hmi/Alarm.zan`, `stdlib/Gui/Hmi/EquipPanel.zan`, `stdlib/Gui/Hmi/Gauge.zan`, `stdlib/Gui/Hmi/Indicator.zan`, `stdlib/Gui/Hmi/IoTag.zan`, `stdlib/Gui/Hmi/NumPad.zan`, `stdlib/Gui/Hmi/Trend.zan`


## AlarmBanner (class)

报警横幅（alarm banner）：窗口顶部/底部的一条，显示最新未确认
报警并带"确认"按钮，未确认时按优先级闪烁。

- AlarmBuffer buffer;

- int wid;

- int ackId;

- UiEvent Ack;
  - 报警被确认时触发。

- AlarmBanner():this(new AlarmBuffer())
  - 自带缓冲区（设计器 / 表单编译路径用），之后可用 Bind()
    换成与报警列表共享的同一个缓冲区。

- void Bind(AlarmBuffer buf)
  - 换绑缓冲区（与 AlarmList 共享同一份报警）。

- AlarmBuffer Buffer()

- AlarmBanner(AlarmBuffer buf)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## AlarmBuffer (class)

报警缓冲区：画面上的报警栏和报警列表共享同一个实例，
因此"确认"在两处同时生效。

AlarmBuffer al = new AlarmBuffer();
al.Raise("PT101", "压力高高", 2);
al.Scan(io);              // 按位号限值自动产生 / 复归

- List<AlarmItem> items;

- int limit;

- AlarmBuffer()

- int Count()

- AlarmItem At(int i)

- int UnackedCount()
  - 未确认的报警条数（报警栏据此闪烁）。

- AlarmItem Newest()
  - 最新一条未确认报警，没有则返回 null。

- AlarmItem Raise(string tagName, string text, int prio)

- void AckAll()

- void Purge()
  - 移除已确认且已复归的记录。

- void Scan(TagTable io)
  - 扫描位号表：越限的位号若还没有在册的活动报警就产生一条，
    回到限内则把对应记录置为已复归。这样画面只需周期调用一次，
    不必在每个采集点手写报警逻辑。

- AlarmItem ActiveOf(string tagName)
  - 该位号仍然成立的报警记录，没有则 null。


## AlarmItem (class)

一条报警记录：发生时间、位号、描述、优先级和确认状态。
优先级 0 提示 / 1 警告 / 2 故障，决定颜色与排序。

- string stamp;

- string point;

- string message;

- int priority;

- bool acked;

- bool active;
  - 报警是否仍然成立（复归后仍留在列表里，直到被确认）。

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
  - 优先级颜色：2 故障红、1 警告黄、其它信息蓝。

- static string LevelOf(int prio)


## AlarmList (class)

报警列表（alarm list）：时间 / 级别 / 位号 / 描述 / 状态五列，
未确认行以优先级色底显示，点击行选中。

- AlarmBuffer buffer;

- int wid;

- int rowBaseId;
  - 行命中区的连续 id 块（缓冲上限即最大行数）。

- int selected;

- UiEvent RowClick;
  - 选中行变化时触发。

- AlarmList():this(new AlarmBuffer())
  - 自带缓冲区（设计器 / 表单编译路径用）。

- void Bind(AlarmBuffer buf)
  - 换绑缓冲区（与 AlarmBanner 共享同一份报警）。

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

棒图 / 量程条（bargraph）：竖向或横向的填充条，带量程刻度和
报警限标线，超限时条体转报警色。

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
  - 0 竖向（自下而上），1 横向（自左向右）。

- void InitBar(string text, IoTag t, SignalInt v, int sc, string u, int lo, int hi)

- Bargraph(string text, IoTag t)

- Bargraph(string text, SignalInt v, int lo, int hi)

- Bargraph(string text)

- Bargraph()
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

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

设备状态卡（device card）：设备名 + 状态灯 + 几个关键数值，
用于设备总览页的卡片网格。状态 0 停机 / 1 运行 / 2 报警 /
3 故障 / 4 离线，与 Led 的状态取值一致。

DeviceCard c = new DeviceCard("1# 压缩机");
c.Row("电流", currentTag);
c.Row("排气压力", pressTag);

- string title;

- SignalInt state;

- List<string> rowNames;

- List<IoTag> rowTags;

- int wid;

- UiEvent Click;
  - 卡片被点击时触发（一般用于打开设备详情）。

- DeviceCard():this("")
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

- DeviceCard(string name)

- SignalInt State()

- void Set(int st)

- int RowCount()

- DeviceCard Row(string label, IoTag t)
  - 追加一行"名称 : 数值"。

- override string Kind()

- override List<string> Events()

- override List<PropSpec> Props()

- static string TextOf(int st)
  - 状态文字。

- override void OnMeasure(App app)

- override void OnPaint(App app)


## Digital (class)

数显表（digital readout）：大字号数值 + 单位 + 标题，超限时
数值转报警色。这是上位机画面上用得最多的一个件。

Digital pv = new Digital("TIC101 PV", tag);

- string caption;

- IoTag tag;

- SignalInt raw;
  - 无位号时的自持值（原始计数）与刻度。

- int scale;

- string unit;

- void InitDigital(string text, IoTag t, SignalInt v, int sc, string u)

- Digital(string text, IoTag t)

- Digital(string text, SignalInt v, int sc, string u)

- Digital(string text)

- Digital()
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

- SignalInt Value()

- void Bind(IoTag t)

- override string Kind()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- override void OnPaint(App app)


## EquipPanel (class)

设备状态面板：一屏看完一条线上所有设备的卡片墙。它是
控件树里的容器，自己按可用宽度把 DeviceCard 排成卡片网格
（列数随宽度自适应），因此窗口拉宽只是每行多放几张卡，
不用重新设计画面。

EquipPanel wall = new EquipPanel();
DeviceCard p1 = wall.AddDevice("1# 泵");
p1.Row("电流", currentTag);
wall.Set("1# 泵", 1);             // 停机 / 运行 / 报警 / 故障 / 离线

序列化 / 设计器把设备名写成 `options` 里的一条文本，卡片
上的数值行由业务代码按位号追加——I/O 是代码，不是布局。

- List<string> names;

- List<DeviceCard> cards;

- int cardW;
  - 单张卡片的最小宽度（逻辑 px），决定自适应列数。

- int cardH;

- int gapPx;

- int cellW;
  - 缩放后的单元尺寸（Arrange 拿不到 App，故在测量时算好）。

- int cellH;

- int cellGap;

- EquipPanel()

- int Count()

- DeviceCard AddDevice(string name)
  - 追加一台设备，返回它的卡片以便挂数值行和点击处理。
    （不叫 Add：那是 Control 添加子控件的名字。）

- DeviceCard At(int i)

- DeviceCard Of(string name)
  - 按设备名取卡片；名称不存在时返回 null。

- void Set(string name, int state)
  - 按设备名写状态（0 停机 / 1 运行 / 2 报警 / 3 故障 / 4 离线）。
    名称未知时是空操作，因此扫描回来的多余位号不会崩画面。

- EquipPanel CardSize(int w, int h)
  - 卡片尺寸（逻辑 px）；宽度是最小值，实际会拉伸填满整行。

- void SetItemsText(string spec)
  - 按序列化文本重建设备清单：`"1# 泵|2# 泵|风机"`。

- override string Kind()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)
  - 卡片网格：按最小卡宽算出列数，再把剩余宽度均分给
    每列，行高固定，超出高度的卡片仍被排布（宿主可滚动）。

- override void OnPaint(App app)


## Gauge (class)

圆盘仪表（dial gauge）：270° 刻度弧 + 指针 + 数字读数。
弧段按位号的报警限自动着色：低报段蓝、正常段绿、高报段红，
因此不必额外配置颜色区间。

Gauge g = new Gauge("PT101", tag);

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
  - 刻度弧的起止角（度，0 为 3 点方向，顺时针为正）。

- int sweepDeg;

- void InitGauge(string text, IoTag t, SignalInt v, int sc, string u, int lo, int hi)

- Gauge(string text, IoTag t)

- Gauge(string text, SignalInt v, int lo, int hi)

- Gauge(string text)

- Gauge()
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

- SignalInt Value()

- void Bind(IoTag t)

- Gauge Range(int lo, int hi)

- Gauge Limits(int lo, int hi)
  - 报警限对应的弧段着色（相等则整弧为正常色）。

- override string Kind()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- int AngleOf(int v)
  - 原始值在刻度弧上的角度。

- override void OnPaint(App app)

- static int CosDeg(int deg)
  - 定点三角函数（返回值放大 1000 倍），避免为了画一根指针
    引入浮点数学依赖。

- static int SinDeg(int deg)

- static int SinQ(int deg)
  - 0..90 度的 sin*1000，10 度一档线性插值（画表盘足够）。


## IoTag (class)

一个过程位号（tag / point）：上位机画面上所有数显、仪表、
指示灯、棒图和趋势都绑定到位号，而不是各自持有一份值，
因此一次采集刷新即可让整幅画面同步。

值用定点整数表示：`raw` 是原始整数，`scale` 是每单位的
计数（1 = 整数，10 = 一位小数，100 = 两位小数）。工控
现场的量程和限值本来就是定点的，避免浮点显示抖动。

TagTable io = new TagTable();
IoTag t = io.Add("TIC101.PV", "\u00b0C", 10);   // 一位小数
t.Range(0, 3000);                             // 0.0 .. 300.0
t.Limits(500, 2500);                          // 低报 / 高报
t.SetRaw(1234);                               // 123.4 \u00b0C

- string tagName;

- string unit;

- int scale;
  - 每工程单位的计数（1 / 10 / 100 ...）。

- SignalInt raw;

- int rawMin;

- int rawMax;

- int loLimit;
  - 低 / 高报警限（原始计数）。相等表示不判限。

- int hiLimit;

- bool good;
  - 最近一次写入是否被认为有效（通信中断时置 false，
    显示端据此画出坏值样式）。

- IoTag(string name, string engUnit, int scaleCounts)

- IoTag(string name):this(name, "", 1)
  - 无量纲整数位号（无单位、无小数）。

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
  - 通信中断 / 坏值：保留最后一次的数值但标记为不可信。

- int Alarm()
  - 0 正常，1 低报，2 高报（未设限值时恒为 0）。

- int Percent()
  - 量程内的百分比（0..100），用于棒图 / 仪表指针。

- string Text()
  - 带小数点的工程值文本（不含单位）。

- string TextWithUnit()

- static string Format(int v, int scale)
  - 定点整数按 `scale` 格式化：scale=10 时 1234 -> "123.4"。

- static int Digits(int scale)
  - scale 对应的小数位数（10 -> 1，100 -> 2）。


## Led (class)

指示灯（LED）：一个发光圆点加标题，上位机画面里表示
运行 / 停止 / 故障 / 通信状态。状态用整数，因此可以直接
绑定到位号：0 灭、1 绿（正常）、2 黄（警告）、3 红（故障）、
4 蓝（信息）。

Led run = new Led("RUN");
run.state = tag.Value();          // 绑定位号
run.SetBlink(true);               // 故障闪烁

- string caption;

- SignalInt state;

- bool blink;
  - 报警态（2/3）闪烁。

- int dotSize;
  - 圆点直径（逻辑 px，0 = 按字号自动）。

- void InitLed(string text, SignalInt st)

- Led(string text, SignalInt st)

- Led(string text)

- Led()
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

- int State()

- void Set(int st)

- void SetBlink(bool on)

- void SetCaption(string s)

- SignalInt Value()

- override string Kind()

- override List<PropSpec> Props()

- static int ColorOf(App app, int st)
  - 状态色：灭为边框灰，1 绿 2 黄 3 红 4 蓝。

- override void OnMeasure(App app)

- int Dot(App app)

- override void OnPaint(App app)


## NumPad (class)

屏幕数字键盘（on-screen numeric keypad）：工控机多为触摸屏、
无实体键盘，改设定值靠它。键盘自己维护输入缓冲，确认时把
缓冲按位号的刻度换算成原始计数写入。

NumPad kp = new NumPad();
kp.Target(spTag);                 // 确认写入该位号
kp.Enter += () => { ... };        // 也可自己接管

- SignalString entry;

- IoTag target;

- int baseId;

- bool allowSign;
  - 允许输入负号与小数点。

- bool allowDot;

- UiEvent Enter;
  - 确认（ENT）时触发。

- UiEvent Cancel;
  - 取消（ESC）时触发。

- NumPad()

- SignalString Entry()

- string Text()

- void SetText(string s)

- void Clear()

- NumPad Target(IoTag t)
  - 确认时写入的位号（同时把它的当前值预置进缓冲）。

- override string Kind()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- string KeyAt(int i)
  - 第 i 个键的文字（4 行 x 3 列）。

- override void OnPaint(App app)

- void PaintKey(App app, int id, int x, int y, int w, int h, string label, int bg, int fg)

- void Press(string label)
  - 处理一次按键，`label` 即键面文字。

- void Commit()
  - 把输入缓冲按目标位号的刻度换算并写入。没有目标位号时
    只保留缓冲，由宿主自行读取。

- static int Parse(string s, int scale)
  - "12.34" + scale=100 -> 1234。多余的小数位截断，缺位补零。


## TagTable (class)

画面的位号表：按名字建立/查找位号，并统一驱动周期刷新。
采集侧（Modbus / OPC / 串口线程）只管往位号里写值，
显示侧只管读，两边通过信号解耦。

TagTable io = new TagTable();
io.Add("PT101.PV", "kPa", 10).Range(0, 10000).Limits(500, 9000);
// 采集回调里：
io.Set("PT101.PV", 4321);

- List<IoTag> tags;

- int periodMs;
  - 采集周期（毫秒），供宿主的定时器使用。

- TagTable()

- int Count()

- IoTag At(int i)

- int Period()

- void SetPeriod(int ms)

- IoTag Add(string name, string unit, int scale)

- IoTag Add(string name)

- IoTag Of(string name)
  - 按名查找位号，没有则返回 null（调用方不要假定非空）。

- IoTag Ensure(string name)
  - 按名查找，不存在时按默认量程即时建立——数据驱动的
    画面加载时不必先声明全部位号。

- void Set(string name, int rawValue)

- void SetBad(string name)

- int AlarmCount()
  - 处于报警状态的位号数量（低报 + 高报）。


## Trend (class)

实时趋势图（rolling trend）：多笔曲线共享时间轴与量程，
新点从右侧进入、旧点左移，是上位机最常用的历史观察件。

Trend tr = new Trend("Reactor");
tr.Add("PV", pvTag, theme.success);
tr.Add("SV", svTag, theme.info);
// 周期定时器里：
tr.Sample();

- string caption;

- List<TrendPen> pens;

- int rawMin;

- int rawMax;

- int scale;

- int window;
  - 采样点容量 = 时间窗宽度（点数）。

- bool showGrid;

- Trend(string text, int lo, int hi, int sc, int win)

- Trend(string text):this(text, 0, 100, 1, 120)

- Trend():this("", 0, 100, 1, 120)
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

- int PenCount()

- TrendPen PenAt(int i)

- void SetGrid(bool on)

- Trend Range(int lo, int hi)

- TrendPen Add(string title, IoTag t, int color)
  - 加一笔曲线。第一次加入时若量程还是默认值，则跟随位号量程。

- void Sample()
  - 所有曲线同时采一个点。

- override string Kind()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- int PlotY(int v, int ph)
  - 原始值在绘图区内的高度（自底向上的像素数）。


## TrendPen (class)

一条趋势曲线：环形采样缓冲 + 颜色 + 位号。缓冲写满后自动
覆盖最旧的点，因此长时间运行不增长内存。

- string title;

- IoTag tag;

- SignalInt raw;

- int color;

- List<int> samples;

- int capacity;

- int head;
  - 下一个写入位置（环形缓冲）。

- int filled;

- TrendPen(string name, IoTag t, int col, int cap)

- string Title()

- int Color()

- int Count()

- IoTag Point()

- void Sample()
  - 采一个点（一般由画面的周期定时器统一调用）。

- void Push(int v)

- int At(int i)
  - 第 i 个点（0 = 最旧），按环形缓冲展开。
