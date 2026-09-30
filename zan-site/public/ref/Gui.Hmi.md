# Gui.Hmi

> 源码: `packages/Zan.Industrial/src/Gui/Hmi/Alarm.zan`, `packages/Zan.Industrial/src/Gui/Hmi/EquipPanel.zan`, `packages/Zan.Industrial/src/Gui/Hmi/Gauge.zan`, `packages/Zan.Industrial/src/Gui/Hmi/Indicator.zan`, `packages/Zan.Industrial/src/Gui/Hmi/IoTag.zan`, `packages/Zan.Industrial/src/Gui/Hmi/NumPad.zan`, `packages/Zan.Industrial/src/Gui/Hmi/Trend.zan`


## AlarmBanner (class)

报警横幅（alarm banner）：窗口顶部/底部的一条，显示最新未确认
报警并带"确认"按钮，未确认时按优先级闪烁。

- AlarmBuffer buffer;
  - 数据源（可与 AlarmList 共享同一份报警）。

- int wid;
  - 整条命中 id。

- int ackId;
  - 确认按钮命中 id。

- UiEvent Ack;
  - 报警被确认时触发。

- AlarmBanner():this(new AlarmBuffer())
  - 自带缓冲区（设计器 / 表单编译路径用），之后可用 Bind()
    换成与报警列表共享的同一个缓冲区。

- void Bind(AlarmBuffer buf)
  - 换绑缓冲区（与 AlarmList 共享同一份报警）。

- AlarmBuffer Buffer()
  - 当前绑定的缓冲区。

- AlarmBanner(AlarmBuffer buf)
  - 共享缓冲区构造。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：无设计器属性（内容由数据源驱动）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Ack"。

- override void BindEvent(string evt, Action a)
  - 覆写："Ack" 挂 Ack 事件，其余按名称走通用路由。

- override void OnMeasure(App app)
  - 首选高度 = 主题的 medium 行高。

- override void OnPaint(App app)
  - 覆写：画最新未确认报警文本与"确认"按钮，未确认时按优先级周期闪烁。


## AlarmBuffer (class)

报警缓冲区：画面上的报警栏和报警列表共享同一个实例，
因此"确认"在两处同时生效。

AlarmBuffer al = new AlarmBuffer();
al.Raise("PT101", "压力高高", 2);
al.Scan(io);              // 按位号限值自动产生 / 复归

- List<AlarmItem> items;
  - 记录列表（追加序，下标 0 最旧）。

- int limit;
  - 容量上限（超出时淘汰最旧记录）。

- AlarmBuffer()
  - 默认容量 500。

- int Count()
  - 记录条数 / 按下标取记录（下标 0 最旧）。

- AlarmItem At(int i)
  - 第 i 条记录（不查越界）。

- int UnackedCount()
  - 未确认的报警条数（报警栏据此闪烁）。

- AlarmItem Newest()
  - 最新一条未确认报警，没有则返回 null。

- AlarmItem Raise(string tagName, string text, int prio)
  - 产生一条报警；超出容量上限（默认 500）时淘汰最旧记录。

- void AckAll()
  - 确认全部报警。

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
  - 发生时间（ToTimeString 文本）。

- string point;
  - 位号名。

- string message;
  - 描述文本。

- int priority;
  - 优先级：0 提示 / 1 警告 / 2 故障。

- bool acked;
  - 是否已确认。

- bool active;
  - 报警是否仍然成立（复归后仍留在列表里，直到被确认）。

- AlarmItem(string tagName, string text, int prio)
  - 构造一条活动、未确认的报警（时间取当前时刻）。

- string Stamp()
  - 发生时间文本。

- string Point()
  - 位号名。

- string Message()
  - 描述文本。

- int Priority()
  - 优先级。

- bool IsAcked()
  - 是否已确认。

- bool IsActive()
  - 报警是否仍成立。

- void Ack()
  - 确认（acked 置真）。

- void Clear()
  - 复归（active 置假；记录留在列表里直到确认并 Purge）。

- static int ColorOf(App app, int prio)
  - 优先级颜色：2 故障红、1 警告黄、其它信息蓝。

- static string LevelOf(int prio)
  - 优先级文字：FAULT / WARN / INFO。


## AlarmList (class)

报警列表（alarm list）：时间 / 级别 / 位号 / 描述 / 状态五列，
未确认行以优先级色底显示，点击行选中。

- AlarmBuffer buffer;
  - 数据源（可与 AlarmBanner 共享同一份报警）。

- int wid;
  - 整条命中 id（On.Fire 用）。

- int rowBaseId;
  - 行命中区的连续 id 块（缓冲上限即最大行数）。

- int selected;
  - 当前选中行下标（-1 = 无）。

- UiEvent RowClick;
  - 选中行变化时触发。

- AlarmList():this(new AlarmBuffer())
  - 自带缓冲区（设计器 / 表单编译路径用）。

- void Bind(AlarmBuffer buf)
  - 换绑缓冲区（与 AlarmBanner 共享同一份报警）。

- AlarmBuffer Buffer()
  - 当前绑定的缓冲区。

- AlarmList(AlarmBuffer buf)
  - 共享缓冲区构造。

- int Selected()
  - 当前选中行下标（-1 = 无选中）。

- AlarmItem SelectedItem()
  - 选中的报警记录；无选中或下标越界返回 null。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：无设计器属性（内容由数据源驱动）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "RowClick"。

- override void BindEvent(string evt, Action a)
  - 覆写："RowClick" 挂 RowClick 事件，其余按名称走通用路由。

- override void OnMeasure(App app)
  - 覆写：固定首选尺寸 480 x 200（逻辑 px）。

- override void OnPaint(App app)
  - 覆写：画表头与五列报警行（最新在最上），点击行选中并触发 RowClick。


## Bargraph (class)

棒图 / 量程条（bargraph）：竖向或横向的填充条，带量程刻度和
报警限标线，超限时条体转报警色。

- string caption;
  - 标题（顶部，"" 不显示）。

- IoTag tag;
  - 数据位号；null = 用独立信号 raw。

- SignalInt raw;
  - 底层信号（条体长度跟随它的值）。

- int scale;
  - 缩放：显示值 = 原始计数 / scale（1 = 不缩放）。

- string unit;
  - 单位文本（读数后缀）；"" 不显示。

- int rawMin;
  - 量程（原始计数）；rawMax ≤ rawMin 时被修正为 rawMin+1。

- int rawMax;

- int loLimit;
  - 报警限标线（原始计数）；相等 = 不画标线。

- int hiLimit;

- int orient;
  - 0 竖向（自下而上），1 横向（自左向右）。

- void InitBar(string text, IoTag t, SignalInt v, int sc, string u, int lo, int hi)
  - 初始化：标题 + 数据源 + 量程/单位（同 Gauge 的参数约定）。

- Bargraph(string text, IoTag t)
  - 位号构造：量程/缩放/单位取自位号。

- Bargraph(string text, SignalInt v, int lo, int hi)
  - 独立信号构造：量程 lo..hi（原始计数），scale=1、无单位。

- Bargraph(string text)
  - 标题构造：内部自建信号，量程 0..100。

- Bargraph()
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

- static int Vertical()
  - 方向常量：竖向（自下而上，0）。

- static int Horizontal()
  - 方向常量：横向（自左向右，1）。

- SignalInt Value()
  - 底层信号访问器。

- Bargraph Orient(int o)
  - 设方向（0 竖向 / 1 横向，链式）。

- Bargraph Range(int lo, int hi)
  - 设量程（原始计数，hi ≤ lo 时修正为 lo+1；链式）。

- Bargraph Limits(int lo, int hi)
  - 设报警限标线（原始计数；相等 = 不画，链式）。

- void Bind(IoTag t)
  - 换绑位号（同步量程、缩放与单位）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（标题 / 量程 / 数值信号 / 方向）。

- override void OnMeasure(App app)
  - 覆写：按方向给首选尺寸（横向 220 x 40，竖向 64 x 180）。

- int Fraction()
  - 当前值占量程的比例（千分比 0..1000）。

- override void OnPaint(App app)
  - 覆写：画轨道、按当前值比例填充、报警限标线与读数（横竖两向）。

- int PosOf(int v, int len)
  - 值 v 在长度 len 上的位置（钳制到 0..len）。

- void PaintLimitsV(App app, int x, int y, int w, int h)
  - 竖条的报警限标线（低报蓝线、高报红线）。

- void PaintLimitsH(App app, int x, int y, int w, int h)
  - 横条的报警限标线（低报蓝线、高报红线）。


## DeviceCard (class)

设备状态卡（device card）：设备名 + 状态灯 + 几个关键数值，
用于设备总览页的卡片网格。状态 0 停机 / 1 运行 / 2 报警 /
3 故障 / 4 离线，与 Led 的状态取值一致。

DeviceCard c = new DeviceCard("1# 压缩机");
c.Row("电流", currentTag);
c.Row("排气压力", pressTag);

- string title;
  - 设备名（卡片左上标题）。

- SignalInt state;
  - 设备状态（0 停机 / 1 运行 / 2 报警 / 3 故障 / 4 离线）。

- List<string> rowNames;
  - 数据行名称，与 rowTags 按下标一一对应。

- List<IoTag> rowTags;
  - 数据行位号，与 rowNames 按下标一一对应。

- int wid;
  - 整卡命中 id（点击时触发 Click）。

- UiEvent Click;
  - 卡片被点击时触发（一般用于打开设备详情）。

- DeviceCard():this("")
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

- DeviceCard(string name)
  - 以设备名构造空卡片（数据行经 Row 追加）。

- SignalInt State()
  - 设备状态信号访问器。

- void Set(int st)
  - 写入设备状态（取值见类注释）。

- int RowCount()
  - 数据行数。

- DeviceCard Row(string label, IoTag t)
  - 追加一行"名称 : 数值"。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<string> Events()
  - 覆写：仅公共事件（点击语义走 Click）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（设备名 / 状态信号）。

- static string TextOf(int st)
  - 状态文字。

- override void OnMeasure(App app)
  - 覆写：首选高度 = 标题行 + 数据行数 x 行高 + 边距。

- override void OnPaint(App app)
  - 覆写：画设备名、状态芯片、左侧状态色条与数据行，整卡可点击。


## Digital (class)

数显表（digital readout）：大字号数值 + 单位 + 标题，超限时
数值转报警色。这是上位机画面上用得最多的一个件。

Digital pv = new Digital("TIC101 PV", tag);

- string caption;

- IoTag tag;

- SignalInt raw;
  - 无位号时的自持值（原始计数）与刻度。

- int scale;
  - 每工程单位的计数（显示值 = raw / scale，<1 按 1）。

- string unit;
  - 工程单位后缀；空串不画。

- void InitDigital(string text, IoTag t, SignalInt v, int sc, string u)
  - 初始化：标题 + 数据源（位号或独立信号）+ 缩放与单位。

- Digital(string text, IoTag t)
  - 标题 + 绑定位号（缩放与单位取自位号）。

- Digital(string text, SignalInt v, int sc, string u)
  - 标题 + 独立信号 + 缩放与单位。

- Digital(string text)
  - 标题 + 内部自持信号（初始 0，缩放 1，无单位）。

- Digital()
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

- SignalInt Value()
  - 底层信号（换绑位号请用 <c>Bind</c>）。

- void Bind(IoTag t)
  - 换绑位号（同步缩放与单位）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（标题 / 单位 / 缩放 / 数值信号）。

- override void OnMeasure(App app)
  - 覆写：固定首选尺寸 150 x 72（逻辑 px）。

- override void OnPaint(App app)
  - 覆写：画数显面板、标题与工程量值；超限转报警色，坏质量显示 "----"。


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
  - 设备名列表，与 cards 按下标平行（Of 按它查找）。

- List<DeviceCard> cards;
  - 设备卡片列表，与 names 按下标对应。

- int cardW;
  - 单张卡片的最小宽度（逻辑 px），决定自适应列数。

- int cardH;
  - 单张卡片的逻辑高度（px）。

- int gapPx;
  - 卡片间距（逻辑 px）。

- int cellW;
  - 缩放后的单元尺寸（Arrange 拿不到 App，故在测量时算好）。

- int cellH;

- int cellGap;

- int lastArrangeW;
  - 上一帧 Arrange 实际拿到的宽度。列数由宽度决定，而测量发生在
    分配尺寸之前：用上一帧的真实宽度来算行数，测量和排布才不会
    各算一套（那会让多出来的行被裁掉）。

- EquipPanel()
  - 空设备墙（之后 AddDevice/SetItemsText）。

- int Count()
  - 设备卡片数量。

- DeviceCard AddDevice(string name)
  - 追加一台设备，返回它的卡片以便挂数值行和点击处理。
    （不叫 Add：那是 Control 添加子控件的名字。）

- DeviceCard At(int i)
  - 第 i 张卡片；越界返回 null。

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
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（卡片宽 / 卡片高）。

- override void OnMeasure(App app)
  - 覆写：缓存缩放后的单元尺寸，并按上一帧排布宽度推列数、
    行数回写首选尺寸。

- override void Arrange(int px, int py, int pw, int ph)
  - 卡片网格：按最小卡宽算出列数，再把剩余宽度均分给
    每列，行高固定，超出高度的卡片仍被排布（宿主可滚动）。

- override void OnPaint(App app)
  - 覆写：空面板时画一个可见的占位边框（设计器里不致一块空白）；
    有卡片则整体交给子控件绘制。


## Gauge (class)

圆盘仪表（dial gauge）：270° 刻度弧 + 指针 + 数字读数。
弧段按位号的报警限自动着色：低报段蓝、正常段绿、高报段红，
因此不必额外配置颜色区间。

Gauge g = new Gauge("PT101", tag);

- string caption;
  - 标题（底部居中，"" 不显示）。

- IoTag tag;
  - 数据位号；null = 用独立信号 raw。

- SignalInt raw;
  - 底层信号（指针角度跟随它的值）。

- int scale;
  - 缩放：显示值 = 原始计数 / scale（1 = 不缩放）。

- string unit;
  - 单位文本（读数后缀）；"" 不显示。

- int rawMin;
  - 量程（原始计数）；rawMax ≤ rawMin 时被修正为 rawMin+1。

- int rawMax;

- int loLimit;
  - 报警限（原始计数）；相等 = 整弧按正常色。

- int hiLimit;

- int startDeg;
  - 刻度弧的起止角（度，0 为 3 点方向，顺时针为正）。

- int sweepDeg;
  - 刻度弧的扫过角度（度，顺时针）。

- void InitGauge(string text, IoTag t, SignalInt v, int sc, string u, int lo, int hi)
  - 初始化：标题 + 数据源（位号或独立信号）+ 量程/单位。

- Gauge(string text, IoTag t)
  - 位号构造：量程/缩放/单位/报警限全部取自位号。

- Gauge(string text, SignalInt v, int lo, int hi)
  - 独立信号构造：量程 lo..hi（原始计数），scale=1、无单位。

- Gauge(string text)
  - 标题构造：内部自建信号，量程 0..100。

- Gauge()
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

- SignalInt Value()
  - 底层信号（指针角度跟随它的值）。

- void Bind(IoTag t)
  - 换绑位号（同步量程、缩放与单位）。

- Gauge Range(int lo, int hi)
  - 设量程（原始计数）。

- Gauge Limits(int lo, int hi)
  - 报警限对应的弧段着色（相等则整弧为正常色）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（标题 / 单位 / 量程 / 数值信号）。

- override void OnMeasure(App app)
  - 覆写：固定首选尺寸 160 x 160（逻辑 px）。

- int AngleOf(int v)
  - 原始值在刻度弧上的角度。

- override void OnPaint(App app)
  - 覆写：画刻度弧（按报警限分色）、主刻度、指针、读数与标题。

- static int CosDeg(int deg)
  - 定点三角函数（返回值放大 1000 倍），避免为了画一根指针
    引入浮点数学依赖。

- static int SinDeg(int deg)
  - 定点 sin（返回值放大 1000 倍，画指针用）。

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
  - 原始计数值信号（显示值 = raw / scale），HMI 控件共享引用。

- int rawMin;

- int rawMax;

- int loLimit;
  - 低 / 高报警限（原始计数）。相等表示不判限。

- int hiLimit;

- bool good;
  - 最近一次写入是否被认为有效（通信中断时置 false，
    显示端据此画出坏值样式）。

- IoTag(string name, string engUnit, int scaleCounts)
  - 构建位号：缩放 <1 按 1；量程默认 0..100 单位、不判限。

- IoTag(string name):this(name, "", 1)
  - 无量纲整数位号（无单位、无小数）。

- string Name()
  - 位号名 / 工程单位 / 定点缩放（每单位计数）。

- string Unit()
  - 工程单位文本。

- int Scale()
  - 定点缩放（每单位计数，显示值 = raw / scale）。

- SignalInt Value()
  - 底层信号（HMI 控件间共享引用实现整幅画面同步刷新）。

- int Raw()
  - 原始定点计数值（显示值 = raw / scale）。

- bool IsGood()
  - 最近写入是否有效（通信坏时 false，显示端画坏值样式）。

- int Min()
  - 量程上下限（原始计数）。

- int Max()
  - 量程上限（原始计数）。

- IoTag Range(int lo, int hi)
  - 设量程（原始计数，hi ≤ lo 时修正为 lo+1；链式）。

- IoTag Limits(int lo, int hi)
  - 设低/高报警限（原始计数；相等 = 不判限）。

- void SetRaw(int v)
  - 写入有效原始值（同时清除坏值标记）。

- void SetBad()
  - 通信中断 / 坏值：保留最后一次的数值但标记为不可信。

- int Alarm()
  - 0 正常，1 低报，2 高报（未设限值时恒为 0）。

- int Percent()
  - 量程内的百分比（0..100），用于棒图 / 仪表指针。

- string Text()
  - 带小数点的工程值文本（不含单位）。

- string TextWithUnit()
  - 带小数点的工程值文本（含单位；无单位时退化为 <c>Text</c>）。

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
  - 标题文本；空串不画。

- SignalInt state;
  - 状态信号：0 灭 / 1 绿 / 2 黄 / 3 红 / 4 蓝。

- bool blink;
  - 报警态（2/3）闪烁。

- int dotSize;
  - 圆点直径（逻辑 px，0 = 按字号自动）。

- void InitLed(string text, SignalInt st)
  - 初始化：标题 + 状态信号（0 灭 / 1 绿 / 2 黄 / 3 红 / 4 蓝）。

- Led(string text, SignalInt st)
  - 标题 + 外部状态信号。

- Led(string text)
  - 标题 + 内部自持信号（初始 0 = 灭）。

- Led()
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

- int State()
  - 状态读写 / 闪烁开关 / 标题文本 / 底层信号。

- void Set(int st)
  - 写状态值。

- void SetBlink(bool on)
  - 设置闪烁开关。

- void SetCaption(string s)
  - 设置标题文本。

- SignalInt Value()
  - 底层状态信号。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（标题 / 状态信号 / 报警闪烁）。

- static int ColorOf(App app, int st)
  - 状态色：灭为边框灰，1 绿 2 黄 3 红 4 蓝。

- override void OnMeasure(App app)
  - 覆写：首选尺寸 = 圆点直径 + 标题宽，高度取 small 行高。

- int Dot(App app)
  - 圆点直径（缩放后的像素；未配置时用 12 基准值）。

- override void OnPaint(App app)
  - 覆写：画状态圆点与标题；报警闪烁按半秒周期申请局部动画帧。


## NumPad (class)

屏幕数字键盘（on-screen numeric keypad）：工控机多为触摸屏、
无实体键盘，改设定值靠它。键盘自己维护输入缓冲，确认时把
缓冲按位号的刻度换算成原始计数写入。

NumPad kp = new NumPad();
kp.Target(spTag);                 // 确认写入该位号
kp.Enter += () => { ... };        // 也可自己接管

- SignalString entry;
  - 输入缓冲（显示文本，尚未按位号刻度换算）。

- IoTag target;
  - 确认（ENT）时写入的目标位号；null = 无目标，结果留在缓冲。

- int baseId;
  - 命中 id 块：0..11 数字/小数点/退格，12 +/-，13 CLR，14 ENT，
    15 保留给 On.Fire。

- bool allowSign;
  - 允许输入负号与小数点。

- bool allowDot;
  - 允许输入小数点；整个缓冲只允许一个小数点。

- UiEvent Enter;
  - 确认（ENT）时触发。

- UiEvent Cancel;
  - 取消（ESC）时触发。

- NumPad()
  - 默认设计期构造：允许负号与小数点，无目标位号。

- SignalString Entry()
  - 输入缓冲的信号访问器。

- string Text()
  - 当前缓冲文本。

- void SetText(string s)
  - 覆盖缓冲文本（不触发 Enter）。

- void Clear()
  - 清空缓冲。

- NumPad Target(IoTag t)
  - 确认时写入的位号（同时把它的当前值预置进缓冲）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Enter" 与 "Cancel"。

- override void BindEvent(string evt, Action a)
  - 覆写："Enter"/"Cancel" 挂对应事件，其余按名称走通用路由。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（输入缓冲 / 允许负号 / 允许小数点）。

- override void OnMeasure(App app)
  - 覆写：固定首选尺寸 200 x 260（逻辑 px）。

- string KeyAt(int i)
  - 第 i 个键的文字（4 行 x 3 列）。

- override void OnPaint(App app)
  - 覆写：画输入显示屏、4x3 数字键盘与 +/-、CLR、ENT 底行。

- void PaintKey(App app, int id, int x, int y, int w, int h, string label, int bg, int fg)
  - 画一枚按键（含悬停/按下反馈与点击命中注册）。

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
  - 空表；采集周期默认 500ms。

- int Count()
  - 位号数量 / 按下标取位号。

- IoTag At(int i)
  - 返回第 i 个位号。

- int Period()
  - 采集周期（毫秒，默认 500）。

- void SetPeriod(int ms)
  - 设置采集周期（毫秒）；非正数忽略。

- IoTag Add(string name, string unit, int scale)
  - 声明一个位号（配对单位与定点缩放）。

- IoTag Add(string name)
  - 无量纲整数位号（无单位、缩放 1）。

- IoTag Of(string name)
  - 按名查找位号，没有则返回 null（调用方不要假定非空）。

- IoTag Ensure(string name)
  - 按名查找，不存在时按默认量程即时建立——数据驱动的
    画面加载时不必先声明全部位号。

- void Set(string name, int rawValue)
  - 采集回调写值入口：按名写入，位号不存在时静默忽略。

- void SetBad(string name)
  - 按名标记坏值（位号不存在时静默忽略）。

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
  - 标题 + 量程（原始计数）/缩放/时间窗（点数，<8 按 8）；
    hi ≤ lo 时量程自动扩为 lo+1。

- Trend(string text):this(text, 0, 100, 1, 120)
  - 默认量程 0..100、缩放 1、120 点时间窗。

- Trend():this("", 0, 100, 1, 120)
  - Placeable from the designer, which builds every control with no
    arguments and then applies the design's properties.

- int PenCount()
  - 曲线笔数 / 按下标取笔 / 网格开关。

- TrendPen PenAt(int i)
  - 返回第 i 支笔。

- void SetGrid(bool on)
  - 设置背景网格开/关。

- Trend Range(int lo, int hi)
  - 设量程（原始计数，所有笔共用）。

- TrendPen Add(string title, IoTag t, int color)
  - 加一笔曲线。第一次加入时若量程还是默认值，则跟随位号量程。

- void Sample()
  - 所有曲线同时采一个点。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（标题 / 量程 / 时间窗 / 网格）。

- override void OnMeasure(App app)
  - 覆写：固定首选尺寸 360 x 200（逻辑 px）。

- override void OnPaint(App app)
  - 覆写：画网格与纵轴刻度、各笔曲线和底部图例（新点在右、旧点左移）。

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
  - 构造：笔名 + 位号 + 颜色 + 环形缓冲容量（<8 按 8）。

- string Title()
  - 笔名 / 颜色 / 已存样本数 / 绑定的位号。

- int Color()
  - 本笔曲线颜色。

- int Count()
  - 已存样本数。

- IoTag Point()
  - 绑定的位号。

- void Sample()
  - 采一个点（一般由画面的周期定时器统一调用）。

- void Push(int v)
  - 直写一个原始值进环形缓冲（不走位号）。

- int At(int i)
  - 第 i 个点（0 = 最旧），按环形缓冲展开。
