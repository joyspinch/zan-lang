# Gui.Widget

> 源码: `stdlib/Gui/Widget/Avatar.zan`, `stdlib/Gui/Widget/Badge.zan`, `stdlib/Gui/Widget/BoxContent.zan`, `stdlib/Gui/Widget/Breadcrumb.zan`, `stdlib/Gui/Widget/Button.zan`, `stdlib/Gui/Widget/ButtonGroup.zan`, `stdlib/Gui/Widget/Card.zan`, `stdlib/Gui/Widget/Carousel.zan`, `stdlib/Gui/Widget/Checkbox.zan`, `stdlib/Gui/Widget/CodeBlock.zan`, `stdlib/Gui/Widget/Collapse.zan`, `stdlib/Gui/Widget/ContextMenu.zan`, `stdlib/Gui/Widget/DatePicker.zan`, `stdlib/Gui/Widget/Divider.zan`, `stdlib/Gui/Widget/Dropdown.zan`, `stdlib/Gui/Widget/Ellipsis.zan`, `stdlib/Gui/Widget/Empty.zan`, `stdlib/Gui/Widget/FloatButton.zan`, `stdlib/Gui/Widget/FormBuilder.zan`, `stdlib/Gui/Widget/FormField.zan`, `stdlib/Gui/Widget/Input.zan`, `stdlib/Gui/Widget/Label.zan`, `stdlib/Gui/Widget/Layer.zan`, `stdlib/Gui/Widget/ListView.zan`, `stdlib/Gui/Widget/Menu.zan`, `stdlib/Gui/Widget/PageHeader.zan`, `stdlib/Gui/Widget/Pagination.zan`, `stdlib/Gui/Widget/Panel.zan`, `stdlib/Gui/Widget/Popover.zan`, `stdlib/Gui/Widget/Progress.zan`, `stdlib/Gui/Widget/Prompt.zan`, `stdlib/Gui/Widget/Radio.zan`, `stdlib/Gui/Widget/Rate.zan`, `stdlib/Gui/Widget/Result.zan`, `stdlib/Gui/Widget/Ribbon.zan`, `stdlib/Gui/Widget/ScrollColumn.zan`, `stdlib/Gui/Widget/ScrollView.zan`, `stdlib/Gui/Widget/Scrollbar.zan`, `stdlib/Gui/Widget/SelectBox.zan`, `stdlib/Gui/Widget/Skeleton.zan`, `stdlib/Gui/Widget/Slider.zan`, `stdlib/Gui/Widget/Spin.zan`, `stdlib/Gui/Widget/Split.zan`, `stdlib/Gui/Widget/SplitPanel.zan`, `stdlib/Gui/Widget/States.zan`, `stdlib/Gui/Widget/Statistic.zan`, `stdlib/Gui/Widget/StatusBar.zan`, `stdlib/Gui/Widget/Steps.zan`, `stdlib/Gui/Widget/StyledText.zan`, `stdlib/Gui/Widget/Switch.zan`, `stdlib/Gui/Widget/Table.zan`, `stdlib/Gui/Widget/Tabs.zan`, `stdlib/Gui/Widget/Tag.zan`, `stdlib/Gui/Widget/TextArea.zan`, `stdlib/Gui/Widget/Timeline.zan`, `stdlib/Gui/Widget/ToolStrip.zan`, `stdlib/Gui/Widget/Tooltip.zan`, `stdlib/Gui/Widget/TreeView.zan`, `stdlib/Gui/Widget/Typography.zan`, `stdlib/Gui/Widget/VirtualList.zan`, `stdlib/Gui/Widget/Wizard.zan`


## Avatar (class)

头像：圆形标记内的首字母或图标。填充、文字颜色和
半径来自 `avatar` CSS 规则；`Size` 是圆形的直径，单位为
CSS px（`width`/`height` 规则优先于它）。

Avatar a = new Avatar { Text = "JS", Size = 40 };
Avatar b = new Avatar { Icon = "user", Class = "primary" };

- Binding<string> Text;

- string Icon;

- int Size;

- void InitAvatar(string label, int size, string icon)

- Avatar()

- Avatar(string label)

- Avatar(string label, int size, string icon)

- string Label()

- int Diameter(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()


## Badge (class)

徽标：圆点或计数胶囊。颜色、大小、半径和字体来自
`badge` CSS 规则，皮肤可重设样式，代码侧只需赋值：

Badge b = new Badge { Count = 5, Class = "error" };
b.Count = unread;         // 0 时绘制圆点形式

- int Count;

- int Max;
  - 超过此值的计数绘制为“<max>+”。

- void InitBadge(int n)

- Badge()

- Badge(int n)

- StyleBox ResolvedStyle(App app)

- string Label()
  - 计数绘制的标签（圆点形式为“”）。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int WidthOf(App app, string label, string cls)
  - 计数胶囊的宽度（无标签时为圆点，取高度）。

- static void Pill(App app, int x, int y, string label, string cls)
  - 在其左上角绘制徽标：`label` 为空时画圆点，否则画
    计数胶囊。

- static void Dot(App app, int x, int y)
  - 居中于 (x, y) 的圆点——覆盖标记所需。

- static void CountAt(App app, int x, int y, int count)
  - 居中于 (x, y) 的计数胶囊。

- override string Kind()

- override List<PropSpec> Props()

- override bool SetExtra(string key, string val)
  - `dot: true` 为无计数形式，即计数为 0。


## BoxContent (class)

所有紧凑控件都会绘制的标准“带字形和标签的盒子”：
按钮、标签、chip、分段、菜单项。它解析 CSS 部件
（`<type>::icon`、`::label`、`::spinner`），绘制盒子、添加按下
涟漪，再居中前导图标（忙碌时为 spinner）、标签和
可选的尾随字形，盒子太小时省略标签。

控件调用它而不是自行布局内容：

BoxContent.Paint(app, wid, "button", Class, name, style, x, y, w, h,
Icon, this.Label(), "", busy, Disabled);

它返回尾随字形的左边缘，可关闭 chip 就用它
注册关闭命中区域。

- static int GlyphSize(int fontPx)
  - 文本为 `fontPx` 的盒子的字形大小：字形约为文本的 1.33 倍
    （NaiveUI 比例），使纯图标控件不小于
    带标签的控件。

- static int Width(string icon, string label, string trailing, int fontPx, int gap)
  - `fontPx` 下字形 + 标签的内容宽度，不含盒子自身的
    内边距——控件测量自身时再加内边距。

- static int StackedWidth(App app, string icon, string label, int fontPx)
  - 堆叠盒子（图标在上、标签在下）的内容宽度：取二者较宽。

- static int StackedHeight(App app, string icon, string label, int fontPx, int gap)
  - 堆叠盒子的内容高度：字形、间距和标签行。

- static void PaintStacked(App app, StyleBox s, StyleBox iconStyle, StyleBox labelStyle, int x, int y, int w, int h, string icon, string label, int fgColor)
  - 绘制堆叠盒子：字形居中于标签上方，两者整体居中于
    盒内——`.stacked` 类用于所有命令面（ribbon 命令、
    工具瓦片）的样式。

- static int Paint(App app, int id, string type, string cls, string name, StyleBox s, int x, int y, int w, int h, string icon, string label, string trailing, bool busy, bool disabled)


## Breadcrumb (class)

面包屑导航：同级盒子上的面包屑项用分隔字形连接。
面包屑项、当前项和分隔符都是样式部件
（`breadcrumb::item`、`::current`、`::separator`），皮肤可重设，
代码侧只是简单赋值：

Breadcrumb b = new Breadcrumb();
b.AddItem("Home"); b.AddItem("Projects");
b.Separator = "slash";

- List<string> items;

- string Separator;
  - 绘制在面包屑项之间的字形。

- void InitBreadcrumb()

- Breadcrumb()

- void AddItem(string label)
  - 命名为 AddItem（而非 Add），以免遮蔽 Control.Add(Control)。

- static Breadcrumb Of(List<string> labels)
  - 根据现有标签列表构建一个面包屑。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)
  - 面包屑是列表，因此以逗号分隔的单个值存取。

- override bool SetExtra(string key, string val)


## Button (class)

按钮。外观完全由 CSS 决定，皮肤无需一行代码即可重设样式，
代码侧只需赋值——没有 setter 方法：

Button save = new Button { Text = "Save", Class = "primary small" };
save.Click += () => { ... };
save.Disabled = busy;

可识别的 class 只是皮肤可以定义的选择器：

button                                      默认
.primary .info .success .warning .error     语义类型
.outline .text .dashed                      填充处理
.secondary .ghost .specular .loading        修饰符
.round .circle                              形状
.tiny .small .medium .large                 尺寸（高度/字体/内边距）
.stacked                                    图标在标签上方
.align-left                                 内容靠起始边缘

`Style` 把主题默认 -> 皮肤规则 -> 悬停/按下状态解析为一个
StyleBox，`BoxContent` 在该盒子里绘制图标和标签，
`Ui.Activate` 使其可点击——所以本类只负责测量自身并
触发 Click。

- Binding<string> Text;
  - 可绑定标签：`btn.Text = vm.title;` 每帧重新读取模型字段
    （编译器降级的 Binding）；`btn.Text = "OK";` 存储常量。

- string Icon;

- Binding<bool> Checked;
  - 可绑定的开/关状态。绑定到模型字段（`btn.Checked = vm.bold;`）
    后按钮即成为切换开关：点击翻转该字段，解析出的
    样式进入 `selected` 状态（皮肤中的 `button:checked`）。

- string Tip;
  - 悬停说明，悬停稳定后显示在按钮下方。空
    表示无提示。

- int wid;

- bool clicked;
  - 按钮被激活的帧为 true，供轮询一组按钮
    的宿主（ribbon、工具栏）使用，而非给每个按钮绑定处理器。
    `Disabled` 继承自 Control（可绑定，且沿树
    向下继承，禁用一组即禁用其所有命令）。

- int corners;
  - 按钮哪些角保持圆角（默认 Corner.All）。
    分组通过把按钮间的接缝变方来焊接按钮，使
    整条看起来像一个两端圆角的控件。

- UiEvent Click;
  - 按钮被点击的帧触发（C# 风格：`btn.Click += h;`）。
    处理器经 UI 线程派发队列（App.DrainPosts）执行，因此
    处理器可以重建控件树、打开对话框或启动后台
    任务，而不会改动正在绘制的控件树。
    常用事件集（Enter/Leave/MouseDown/...）由 Control.On 继承。

- void InitButton(string label)

- Button()
  - `new Button { Text = "Save", Class = "primary" }`

- Button(string label)
  - `new Button("Save")` —— 同样的东西，标签前置。

- string Label()
  - 当前标签文本（通过绑定解析）。

- bool IsChecked()
  - 当前切换状态（非切换按钮时为 false）。

- bool IsToggle()
  - Checked 绑定到模型字段的按钮即切换开关：点击
    翻转该字段，而不仅仅是触发 Click。

- bool WasClicked()
  - 按钮被激活的帧为 true——与 Click 对应的
    轮询版本。

- string TipText()
  - 悬停气泡显示的内容：标签，其下方是命令的
    作用（仅标签看不出新意）。

- StyleBox ResolvedStyle(App app)
  - 该按钮当前解析出的样式（大小、内边距、颜色）。

- int AutoWidth(App app)
  - 应用解析样式后的自然宽度，Render 布局时用的就是它——
    用这种方式测量按钮的条状控件，绝不会省略
    明明放得下的标签。

- int AutoHeight(App app)
  - 高度来自解析出的尺寸类；堆叠按钮则取内容高度，
    因为其图标位于标签上方。

- int Render(App app, int x, int y, int w)

- int RenderIn(App app, int x, int y, int w, int h)
  - 把按钮绘制到显式盒子中；`h` 为 0 时取解析样式的高度
    （即 Render 的行为），其他值优先——条状控件
    （ribbon 带、工具栏）给命令统一行高时，
    直接传进来即可，无需逐个设置样式。

- override string Kind()

- override List<PropSpec> Props()
  - 每个属性都指向其编辑的字段，读写操作
    由基类完成（Control.GetProp/SetProp），此列表就是该控件
    的全部属性代码。

- override List<string> Events()

- override void BindEvent(string evt, Action a)
  - 把 Click 路由到 Click UiEvent 字段（与 `btn.Click += h` 同一队列），
    而非基类的 `On` 事件集，使 JSON/设计器的 `onClick`
    处理器与代码处理器走同一通道并按序执行。

- override void OnMeasure(App app)
  - 首选尺寸由解析样式 + 内容得出，停靠/自动布局
    无需手算坐标即可确定按钮大小。

- override void OnPaint(App app)


## ButtonGroup (class)

NaiveUI 风格的吸附按钮组：多个按钮焊接在一起，
共享边框（仅组的外角为圆角），段间有 1px 分隔线。
支持横向或纵向。所有颜色来自主题，
可自动跟随皮肤变化。

ButtonGroup g = new ButtonGroup(1);   // primary（主样式）
g.Add("Left"); g.Add("Middle"); g.Add("Right");
int hit = g.Render(app, 40, 60);          // 被点击的索引，或 -1

- List<ButtonSegment> segs;

- int btnType;

- int btnStyle;

- int size;

- bool vertical;

- bool barMode;
  - 条状模式（由旧 ButtonBar 吸收而来）：一排常驻的真实
    Button 控件，窄时换行，并上报哪个按钮被按下。
    当 `barMode` 为 false 时，该控件即焊接分段
    组，通过 `Render(app, x, y)` 立即渲染。

- List<Button> barBtns;

- int pressed;

- int lineH;

- int lines;

- ButtonGroup(int type)

- ButtonGroup Add(string label)

- ButtonGroup Outline(bool o)

- ButtonGroup Vertical(bool v)

- ButtonGroup SetSize(int s)

- static ButtonGroup Bar(string cls)
  - 一排常驻的横向 Button 控件条（旧 ButtonBar）。
    用 AddButton() 添加按钮；它会自动布局、窄时换行，
    并通过 TakePressed() 上报按下索引。可作布局子控件。

- ButtonGroup AddButton(Button b)

- Button ButtonAt(int i)

- int TakePressed()
  - 上一帧按下的按钮（-1 为无）。此读取会消耗该状态，
    页面恰好处理一次。

- List<int> BarWidths(App app)

- int BarGap(App app)
  - 条状按钮之间的间距。默认 0：组的按钮
    焊接成一条（这正是它看起来像组而不是三个散按钮的原因）；
    若样式表给元素设置 `gap`，
    又会把它们分开。

- static int WeldCorners(bool first, bool last)
  - 焊接行中第 `i` 个按钮保留哪些角：只有行两端
    为圆角，内部接缝为直角。

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
  - 填充一个段，仅对组的外角做圆角。

- void StrokeSeg(Canvas c, int x, int y, int w, int h, int r, int col, bool first, bool last, bool vert)
  - 段的描边（外角圆角）。


## ButtonSegment (class)

一个焊接段：其标签和稳定控件 id（用于焦点/命中）。
用一个实体代替并行的标签/id 列表。

- string label;

- int id;

- ButtonSegment(string label, int id)


## Card (class)

带标题、分隔线和阴影的卡片容器。表面由
样式层解析：皮肤 CSS 中的 `card` / `card.<class>` 规则覆盖
主题的表面默认值，因此换肤无需改动本
代码；`WithFx` 在此基础上可选添加动效。

- string title;

- FxOptions fx;

- bool lifted;
  - 悬停时高亮边框并接收点击，用于可点击的卡片。

- int wid;

- void InitCard(string ttl)

- Card WithFx(FxOptions o)
  - 可选动效，以数据配置：`card.WithFx(o)`，`o` 选择
    效果（aurora/sheen/motes/breath/borderGlow/spotlight）。所有效果
    都在卡片内部裁剪绘制，位于内容之后。

- Card Lift()
  - 让卡片可交互：`card.Lift()` 在悬停时
    平滑显现强调边框，并把卡片矩形注册为点击区域。

- Card(string ttl)
  - 保留模式容器：`Card c = new Card("Profile"); c.Add(child);`

- Card():this("")
  - 无标题卡片：设计器与 `.zform` 生成的代码用 `new Card()`
    建字段，没有这个构造函数就没有任何构造函数会跑，
    卡片的 children 还是 null，第一次 Add 就崩。

- override string Kind()

- override List<PropSpec> Props()

- override string StyleType()

- override void OnMeasure(App app)

- override int StylePadT()
  - 子控件从标题行及其分隔线下方开始：头部是
    卡片自身的装饰，因此样式表 `padding` 只能叠加，
    绝不会让内容滑到标题下面。

- override void OnPaint(App app)

- static int ContentY(int y, Theme t)

- static Rect ContentRect(App app, int x, int y, int w, int h)
  - 带标题卡片的内容矩形：位于标题行及其分隔线下方，
    再内缩卡片自身的水平/底部内边距。调用方
    直接在此矩形内布局内容，无需重新推导头部高度。

- static int FooterHeight(Theme t)
  - 卡片为底部栏预留的高度：一行标准控件高度，
    正好容纳默认尺寸的按钮。

- static Rect BodyRect(App app, int x, int y, int w, int h)
  - 带底部栏卡片的内容矩形：底部栏上方
    的全部区域，两者之间留主题的小间距。

- static Rect FooterRect(App app, int x, int y, int w, int h)
  - 卡片的底部栏：内容区最底行，与
    `BodyRect` 对应——两者合起来铺满内容矩形。


## Carousel (class)

轮播图：带上一张/下一张箭头和指示点的滑动面板。当前
页为双向绑定；`Animate(anim)` 在变化时
水平滑动面板。

Carousel car = Carousel.Of(slides, idx);
car.Animate(Gallery.carAnim);
car.RenderInside(app, rect);

- List<string> slides;

- SignalInt model;

- Binding<int> Index;
  - 双向绑定的当前页索引。

- CarouselAnim anim;

- UiEvent Change;
  - 当前页变化时触发。

- int baseId;

- int idCount;

- void InitCarousel(SignalInt m)

- Carousel()

- Carousel(SignalInt m)

- void AddSlide(string label)
  - 命名为 AddSlide（而非 Add），以免遮蔽 Control.Add(Control)。

- static Carousel Of(List<string> labels, SignalInt m)

- Carousel Animate(CarouselAnim state)
  - 滑动离开和进入的面板；`state` 由宿主持有，
    因此控件重建后过渡依然继续。

- void EnsureIds()
  - 上一张、下一张，然后每个指示点一个 id，作为连续块预留。

- void SyncBinding()

- void GoTo(App app, int i)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void PaintSlide(App app, int x, int i)
  - 在任意 x 位置绘制一张幻灯片（主体 + 居中标签），过渡时可
    并排绘制离开和进入的面板。

- void PaintChrome(App app, int count, int cur)
  - 箭头和指示点，以及各自的点击区域。

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)
  - 幻灯片是列表，因此以逗号分隔的值序列化。

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)


## CarouselAnim (class)

每轮播图一个过渡状态，独立成对象，使每帧重建
控件的宿主仍能持有进行中的动画。

- int shown;

- int from;

- int startFrame;

- bool animating;

- int dir;

- CarouselAnim()


## Checkbox (class)

带勾选图标和响应式布尔绑定的复选框。

Checkbox agree = Checkbox.Bind("I agree", model);
agree.Change += () => { ... };   // 每次切换时触发
agree.Render(app, x, y);

- string label;

- SignalBool model;

- Binding<bool> data;
  - 双向绑定状态：`cb.data = user.agreed;` 使模型字段
    与复选框双向同步（编译器降级的 Binding）。

- int wid;

- UiEvent Change;
  - 选中状态切换时触发（C# 风格：`cb.Change += h;`）。

- void InitCheckbox(string lbl, SignalBool m)

- Checkbox()
  - Default constructor used by .zform and the string-kind registry.

- Checkbox(string lbl)
  - 保留模式构造函数：`Checkbox agree = new Checkbox("I agree");`

- static Checkbox Bind(string label, SignalBool model)

- bool IsChecked()

- void SetChecked(bool v)

- void SyncBinding()
  - 将绑定的模型值拉入本地信号（模型 -> UI）。

- int Render(App app, int x, int y)
  - 保留模式渲染：绘制、分发自身的点击/键盘切换并
    触发 Change。返回控件 id。

- int RenderIn(App app, int x, int y, int h)
  - 与 Render 相同，但按排布给定的行高 `h` 绘制（0 = 按样式/
    主题的控件高度）。停靠成一列的复选框行高由容器决定，
    用主题高度画会让方框和点击区溢出到下一行上。

- override List<string> Events()

- override string Kind()

- override List<PropSpec> Props()

- override void BindEvent(string evt, Action a)

- int PaintStyled(App app, int id, int x, int y, int h, string text, bool checked)

- static void PaintBox(App app, int x, int y, int size, int level)
  - 绘制方框 + 标签并注册点击区域。保留模式与
    旧模式路径共用，保证两者外观一致。
    仅绘制复选框图形（无标签、无点击区域），使需要
    内联复选框的控件画出与真实复选框相同的图形，而无需
    自行绘制。`level` 在 0（未选中）~1000（选中）间交叉淡化。

- static void PaintBoxIndeterminate(App app, int x, int y, int size)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## CheckboxGroup (class)

复选框组：每个选项一个独立的真实 Checkbox。提供聚合
查询（IsChecked/CheckedCount）与组级 Change 事件：任一
选项被用户切换时组事件一并触发，便于表单读取全部勾选。

- UiEvent Change;
  - 任一子复选框被用户切换时触发（C# 风格：`group.Change += h;`）。

- CheckboxGroup()

- override Control MakeItem(int index, string text)

- bool IsChecked(int index)
  - 第 index 个选项的勾选状态；越界或该项不是复选框时返回 false。

- void SetChecked(int index, bool v)
  - 程序化设置第 index 个选项的勾选状态（不触发组 Change，
    与 Checkbox.SetChecked 的静默语义一致）。

- int CheckedCount()
  - 已勾选的选项数。

- void SetAllChecked(bool v)
  - 全选 / 全不选。

- override void BindEvent(string evt, Action a)

- override List<string> Events()

- override string Kind()


## ChoiceGroup (class)

选项组基类：把若干真实选择控件（Radio/Checkbox）按水平流布局排列，
超出可用宽度自动换行。设计器预览与运行时窗口经 FormBuilder / formgen
共用同一实现，使多选项在两侧真实且一致地渲染。无表面。

- int gapX;
  - 列距 / 行距的回退值：样式表的 `column-gap` / `row-gap` / `gap`
    （Tailwind 的 `gap-x-*` / `gap-y-*`）优先。

- int gapY;

- int columns;
  - 每行放几个（0 = 按可用宽度自动换行）。样式表的 `columns`
    （Tailwind 的 `grid-cols-N`）优先。大于 0 时该行按可用宽度等分。

- int rowHeight;
  - 行高（0 = 按最高的选项）。样式表的 `line-height` 优先。

- List<string> labels;
  - 与 children 对齐的选项标签，供 OptionsText 往返（子控件
    的 label 字段不统一，不能靠 GetProp 读回）。

- int mCols;
  - OnMeasure 解析出的行几何，供没有 App 的 Arrange 复用。

- int mColGap;

- int mRowGap;

- int mRowH;

- int mRows;
  - 测量时假定的行数与实际排布出的行数：不一致说明可用宽度
    变了（自动换行模式），下一帧按新宽度重新测量。

- int aRows;

- void InitChoice()

- void SetColumns(int n)
  - 每行的选项个数（0 = 自动换行）。设为 N 后该行按可用宽度等分成
    N 列，最后一行不足 N 个也保持同样的列宽。

- int Columns()

- void SetRowHeight(int h)
  - 每行的高度（0 = 按最高的选项自适应）。选项在行内垂直居中，
    所以调大行高只会拉开行距，不会让相邻行互相压住。

- int RowHeight()

- override string StyleType()
  - 选项组是自己的 CSS 类型（`checkboxgroup` / `radiogroup`），而不是
    Panel 的 `stack`：皮肤要能单独给它写 `columns` / `row-gap` /
    `column-gap` / `line-height`，而不影响所有布局容器。

- virtual Control MakeItem(int index, string text)
  - 供子类实现：为第 index 个选项构造单个选择控件。

- string OptionsText()
  - 选项以 `a|b|c` 字符串表示（设计器 / .zform 属性往返）。

- void SetOptionsText(string text)
  - 按 "A|B|C" 追加选项（与 SelectBox.SetOptionsText 同一约定）。

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<PropSpec> Props()

- override void OnPaint(App app)

- int InnerW(int pw)
  - 内容区宽度（去掉内边距）。

- int RowsHeight(int rows)
  - `rows` 行占用的总高度（含内边距与行距）。

- int FlowRows(int avail)
  - 自动换行模式下，宽度 `avail` 内能排下的行数。

- void ResolveMetrics()
  - 解析行几何（列数 / 行距 / 列距 / 行高）。全部取自已解析的
    样式盒与子项尺寸，不需要 App，因此 Arrange 也能自己调（不
    依赖先跑过 OnMeasure）。

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)

- void ArrangeItem(Control c, int x, int y, int w, int rowH)
  - 把一个选项放进行高为 `rowH` 的单元格里：选项按自身高度
    垂直居中，行高调大只会拉开行距，不会让相邻行互相压住。


## CodeBlock (class)

只读代码面板：带标题的头部含复制操作，下方是
可滚动、等宽外观的源码行。调用方拥有
内容行并决定“复制”做什么，因此本控件不关心
语言或所显示的文档。

CodeBlock cb = new CodeBlock("Zan", lines);
cb.Copy += () => { Clipboard.Set(text); };
cb.RenderInside(app, rect);

- List<string> lines;

- Binding<string> Caption;
  - 头部标题，通常是语言名。

- Binding<string> CopyLabel;
  - 复制操作上的标签。

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
  - 复制操作被点击时触发。

- void InitCodeBlock(string caption, List<string> src)

- CodeBlock()

- CodeBlock(string caption, List<string> src)

- void UseScroll(SignalInt s)
  - 通过宿主持有的信号滚动，因此偏移量在
    面板重建后依然保留。

- bool Copied()
  - 刚绘制的这一帧内复制操作被点击时返回 true，供
    倾向于轮询而非订阅 `Copy` 的宿主使用。

- void SetLines(List<string> src)
  - 显示的行；赋值会替换行、重置滚动并清除
    任何选中状态。

- static void ClearSelection()
  - 清除共享选中状态，无论此前属于谁。

- string AllText()
  - 将整个代码片段拼成一个字符串。

- string SelectedText(int owner)
  - 选中的文本；本块无选中时返回 ""。

- void SelectAll(int owner)
  - 选中整个代码片段（Ctrl+A）。

- void CaretFromMouse(App app, int codeX, int topY, int lineH, int off, int font, bool moveAnchor)
  - 把指针位置映射为代码区内的（行, 列）光标位置。

- static int LineHeight(App app)

- static int HeaderHeight(App app)
  - 标题栏高度：一个标准小按钮加上下
    留白，使标题和复制操作在合适的栏内
    居中，而非被压缩进过矮的栏。

- static int GutterWidth(App app, int lineCount)
  - `lineCount` 行时行号槽的宽度。

- static int SurfaceColor(App app, EditorPalette pal)
  - 默认表面：皮肤自带的面板材质，使代码块看起来
    像周围卡片的一员，而非编辑器配色的一块
    色斑——编辑器调色板跟随*代码主题*，与
    页面其余部分的皮肤无关。
    但样式表在 `codeblock` 上的 `background` 仍然优先。

- static int Height(App app, int lineCount)
  - `lineCount` 行的自然高度（限制在可用空间内）。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)


## Collapse (class)

手风琴（NaiveUI n-collapse）。同时只展开一个面板；`Active` 保存
展开面板的索引（-1 = 全部折叠），可双向绑定：

Collapse c = new Collapse();
c.AddPanel("General", "Theme, language and startup");
c.Active = vm.openPanel;

- List<CollapsePanel> panels;

- SignalInt open;

- Binding<int> Active;
  - 双向绑定的展开索引；-1 折叠所有面板。

- UiEvent Change;
  - 点击标题切换展开面板时触发。

- int baseId;

- int idCount;

- void InitCollapse(SignalInt m)

- Collapse()

- Collapse(SignalInt m)

- void AddPanel(string title, string body)
  - 命名为 AddPanel（而非 Add），以免遮蔽 Control.Add(Control)。

- void AddPanel(CollapsePanel panel)

- static Collapse Of(List<CollapsePanel> items, SignalInt m)
  - 基于现有面板记录构建手风琴。

- static int HeaderHeight(App app)

- static int BodyHeight(App app)

- static int Height(App app, int count, SignalInt openIdx)

- void EnsureIds()
  - 每个面板标题占一个 id，作为连续块预留，使
    面板列表不变时点击 id 保持稳定。

- void SyncBinding()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)
  - 设计器仍把标题和内容作为独立的序列化
    属性暴露，但运行时状态仍是单一的面板实体列表。

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)


## CollapsePanel (class)

一个手风琴面板：标题和内容作为一个记录同存。

- string title;

- string body;

- CollapsePanel(string title, string body)


## ContextMenu (class)

悬浮的右键上下文菜单。在 (mx, my) 绘制，位置被限制在
窗口内，拦截下层点击，并在本帧报告用户的选择：
-1 = 尚无选择，-2 = 已关闭（点击外部），或所选
标签的索引。内容为 "-" 的条目渲染为分隔线。

- static int lastHover;
  - 上一帧悬停到的行；菜单是即时模式绘制的，用它判断是否值得
    为一次鼠标移动重绘。

- static void PaintItem(App app, int x, int y, int w, int h, string label, string accel, bool hovered)
  - 菜单行：悬停高亮、标签以及可选的右对齐
    快捷键/勾选标记。公开出来，使自带菜单的控件
    （表头菜单、设计器）获得一致的行，而无需重复
    绘制。`hovered` 由调用方传入，因为有的菜单按几何
    命中测试，有的通过注册的 id。

- static void PaintSeparator(App app, int x, int y, int w)
  - 菜单内的分隔行。

- static int Height(App app, List<string> labels)
  - `labels` 将占用的高度，使调用方把菜单锚定在按钮上方时
    （如面板底部的输入行）能传入正确的 `my`，
    而不依赖会把菜单挤到窗口边缘的裁剪逻辑。

- static int Render(App app, int mx, int my, List<string> labels)


## DatePicker (class)

日期编辑控件：可键盘编辑的 YYYY-MM-DD 输入框，右侧带日历按钮。
月历在覆盖层中绘制，日期选择通过 OnChange 报告。

- static DatePicker current;

- Input editor;

- Button trigger;

- UiEvent Change;

- bool open;

- int viewYear;

- int viewMonth;

- DateTime selected;

- int wid;

- DatePicker()

- override string Kind()

- override string StyleType()

- override List<PropSpec> Props()

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- DatePicker OnChange(Action a)

- string Text()

- DateTime GetDate()

- void SetDate(DateTime value)

- void SetText(string value)

- static int Digit(string s)

- static int Number(string s, int at, int count)

- static DateTime ParseDate(string value)

- void Edited()

- static void TriggerClicked()

- static void Use(DatePicker value)

- void ToggleOpen()

- void MoveMonth(int delta)

- void PickDay(int day)

- override void OnPaint(App app)

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)

- int appScale(int n)

- override void OnPaintOverlay(App app)


## Divider (class)

分隔线，可含标题。颜色、粗细和字体
来自 `divider` CSS 规则（`background` 为线条，`height` 为
粗细），因此换肤可一次性重设所有分隔线。

Divider d = new Divider { Text = "Section", Class = "left" };

类名：`left` / `right` 调整标题位置（默认居中），`vertical`
绘制竖向分隔线。

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

Dropdown<T>：展开后下拉显示 T 列表的触发器。

与 ListView<T> 结构相同——条目是普通实体，控件负责
其余一切（展开状态、位置、滚动、命中测试、选中）：

Dropdown<City> pick = new Dropdown<City>(c => c.name);
pick.Bind(cities);
pick.OnChange(OnCity);
parent.Add(pick);            // 尺寸和位置由布局决定

条目可以是完整组件而不只是文本，与列表行完全一致：

Dropdown<User> who = new Dropdown<User>(u => UserCard(u));

面板延迟到框架覆盖层绘制，因此总是显示在
后续内容之上，拦截穿透点击，并在外部点击
或按 Escape 时关闭。

- ListView<T> list;
  - 面板内容：由列表控件管理行、模板和滚动。

- CellOf<T> textOf;

- string hint;

- bool open;

- int wid;

- int trigX;
  - 每帧暂存触发器几何信息，使延迟面板（在覆盖
    层阶段绘制）能定位到触发器下方。

- int trigY;

- int trigW;

- int trigH;

- int lastSel;

- int maxVisible;

- bool textRows;
  - 条目是文本行（可以按最长文本量出面板宽度），还是模板控件行。

- UiEvent Change;
  - 选中不同条目时触发。

- UiEvent Opened;

- UiEvent Closed;

- void InitDropdown(ListView<T> content)

- Dropdown(CellOf<T> text)
  - 文本条目：`new Dropdown<City>(c => c.name)`。

- Dropdown(RowOf<T> item)
  - 组件条目：`new Dropdown<User>(u => UserCard(u))`。

- override string Kind()

- override string StyleType()

- Dropdown<T> Bind(List<T> src)
  - 绑定数据源；列表每帧重新读取。

- Dropdown<T> OnChange(Action a)

- Dropdown<T> OnOpen(Action a)

- Dropdown<T> OnClose(Action a)

- Dropdown<T> Hint(string text)
  - 未选中任何条目时显示的提示文本。

- Dropdown<T> Label(CellOf<T> text)
  - 组件条目（`new Dropdown<User>(u => UserCard(u))`）的触发器文本：
    模板行画的是控件，没有可显示的文本，不给这个的话触发器选中后
    仍然只显示提示语。

- Dropdown<T> Rows(int n)
  - 面板在滚动前显示的条目数。

- int Count()

- int SelectedIndex()

- bool HasSelection()

- T Selected()

- void SelectIndex(int row)

- void Clear()

- bool IsOpen()

- void SetOpen(bool want)

- void Open()

- void Close()

- string TriggerText()
  - 触发器标签：所选条目的文本，否则为提示文本。

- override void OnMeasure(App app)

- void PaintTrigger(App app)
  - 触发器走皮肤解析出的 `select` 框，而不是硬写主题色：
    硬写的边框在任何皮肤上都是同一道亮边（嵌在深色面板头部
    里就是一圈白边），样式表也关不掉它。

- override void OnPaint(App app)

- int PopupWidth(App app)
  - 延迟面板：在所有内容之后绘制并分发事件，因此位于
    所有控件之上并接收最上层的点击。
    面板宽度：不窄于触发器，但至少能放下最长的一条文本。
    按触发器宽度开面板会把每一条都截成 "Agnes AI / agnes-2..."，
    选项之间看不出区别。模板行（组件条目）没有可量的文本，
    保持触发器宽度。

- override void OnPaintOverlay(App app)

- override List<string> Events()

- override void BindEvent(string evt, Action a)
  - 把下拉框的语义事件（Change/Opened/Closed）路由到对应
    UiEvent 字段；其余回落到 `On` 上的通用事件包
    （其 AddByName 忽略未知名称）。


## DropdownChrome (class)

在按钮下方挂面板的控件们共用的触发器按钮外观
（Dropdown、Popover、属性编辑器）。

- static void PaintTrigger(App app, int id, int x, int y, int w, int h, string label, bool open)
  - 用给定 id 绘制触发器按钮并注册其点击区域。


## Ellipsis (class)

省略号：单行文本在将超出给定宽度时
截断并在末尾加 "..."。

- static string Fit(string text, int w, int fs)
  - 字体大小 `fs` 下若 `text` 能在 `w` 像素内放下则原样返回，
    否则返回放得下的最长前缀加尾部 "..."。纯字符串
    辅助函数，任何调用方都可用自己的颜色/位置绘制结果。

- static void Render(App app, int x, int y, int w, string text)


## Empty (class)

空状态占位：大图形加一行说明。两者都通过 CSS
部件设置样式，`empty::icon` 和 `empty::label` 控制其大小和
颜色，代码侧只需简单赋值：

Empty e = new Empty { Text = "No records yet" };
e.Icon = "search";

- Binding<string> Text;
  - 可绑定标题：`e.Text = vm.emptyHint;` 每帧重新读取模型字段
    （编译器降级的 Binding）；字面量则存为常量。

- string Icon;

- void InitEmpty(string msg)

- Empty()

- Empty(string msg)

- string Label()
  - 当前标题（通过绑定解析）。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()


## FbFlow (class)

24 列流式容器（设计器/运行时/FormBuilder 共用的唯一布局原语）。
把子节点按各自 span（1..24）贪心打包成多行：一行累计 span 超过 24
就换行；每个单元宽度按 span/24 比例分配（与设计器 LayoutFlowList
完全一致：按 24 列而非行内总和分配，列间留 colGap，行间留 rowGap），
行高取该行子节点 prefH 的最大值。无表面。

- List<int> spans;

- int colGap;

- int rowGap;

- int minCell;

- FbFlow()

- void AddCell(Control c, int span)
  - 追加一个单元及其列宽（span，1..24）。

- int SpanAt(int i)

- int RowEnd(int start)
  - 行结束下标（不含）：从 start 起贪心累计到超过 24 列。

- int RowHeight(int start, int end)

- override void OnPaint(App app)

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)


## FbStack (class)

竖直单元：标签在上、控件填充其余。用于带 caption 的流式字段，
无表面。

- FbStack()

- override void OnPaint(App app)

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)


## FloatButton (class)

浮动按钮：固定“返回顶部”/快捷操作的圆形操作按钮
。`Class = "primary"` 使用强调色填充。

int id = new FloatButton("plus") { Class = "primary" }.Render(app, x, y);
if (Ui.Clicked(app, id)) { ... }

- Binding<string> Icon;
  - 绘制在圆内的图形。

- Binding<int> Size;
  - 直径（设备像素）；0 表示用主题的大控件高度。

- UiEvent Click;
  - 按钮被点击时触发。

- int wid;

- void InitFloatButton(string icon)

- FloatButton()

- FloatButton(string icon)

- int Diameter(App app)

- int Render(App app, int x, int y)
  - 在 (x, y) 绘制并返回控件 id，宿主可用
    `Ui.Clicked` 检测点击，而无需订阅 `Click`。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)


## FormBuilder (class)

统一的 .zform -> 真控件树构建器。设计器预览与运行时窗口
共用这一套：把设计文档（Designer.SaveJson / formgen 消费的同一 JSON）
实例化为真正的 Control 树，按 24 列流式或自由画布布局排布。

设计器把它渲染在编辑外框内并叠加选中/handle 覆盖层；运行时把
同一棵树放进 Form。因此“所见即所得”天然成立，自定义组件也走
同一条实例化路径（ProjectComponents / 注入工厂）。

Canonical kind, layout metadata, and property setup are shared with
formgen through the Control contract. Every setup is applied to the
concrete local Control variable, avoiding downcasts.

- static Control Build(JsonValue root)
  - 把设计文档根对象构建为内容根 Control（不含窗口外框）。

- static string Kind(JsonValue o)

- static string LegacyKind(int t)
  - One-time read compatibility for old designer documents. New writers
    never emit this key.

- static bool IsContainer(JsonValue o)

- static bool NeedsCaption(JsonValue o)

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

- static string GridColumnsSpec(JsonValue o)
  - DataGrid 的 columns 对象数组压成列头预览串（"宽,标题|…"）。
    运行时建树拿不到 "of" 的实体类型，预览只铺列头；类型化取值
    由编译期 GenForm 生成。

- static void FillKids(Control p, JsonValue o, bool free)
  - 把 Panel 型容器（card/tabs/dockpanel）的 kids 填入。

- static void BuildFlow(Control parent, JsonValue arr)

- static Control BuildCell(JsonValue o)

- static void BuildFlowOne(Control pane, JsonValue kid)
  - 把单个字段（分栏面板某个 pane 的直接子节点）按流式加入。

- static void BuildFree(Control parent, JsonValue o)

- static Control FromField(FormField f)
  - 从设计器的 FormField 构建单个叶子控件（不含标题标签——标题由
    设计器/表单布局单独绘制）。设计器预览用它渲染真控件，与运行时的
    MakeControl 走同一条构造路径，从根上消除“所见非所得”。


## FormField (class)

数据录入表单的一行（FormCreate / 表单构建器风格）。

FormField 是可设计控件，描述收集“什么”，而非
如何绘制交互控件：字段类型（`ftype`）、旁边显示的标签、
模型键（`fname`）、占位符、必填标记，以及——选择型
字段的——选项列表。可视化设计器渲染该行预览，
并通过标准 Kind()/Props()/GetProp/SetProp 模型编辑，因此
整个表单可无特判地往返 Serialize。

字段类型（ftype）：
0 input      1 textarea  2 password 3 number  4 radio
5 checkbox   6 select    7 switch   8 rate    9 slider
10 date      11 time     12 upload  13 color   14 richtext

显示/布局组件（ftype >= 15）渲染静态内容而非
接收输入；占满整行宽度，不带标签列。
在设计器面板中按类别分组：
Layout:     15 title     16 text        17 divider
Display:    18 card       19 tag         20 badge     21 avatar
22 statistic  23 table       24 timeline  25 tree
26 collapse   27 empty
Feedback:   28 alert      29 progress     30 result    31 skeleton
32 spin       33 tooltip
Navigation: 34 steps      35 breadcrumb   36 tabs      37 pagination
38 menu       39 pageheader

- string kind;

- int ftype;

- string label;

- string fname;

- string placeholder;

- string customKind;

- bool required;

- bool defOn;

- bool wrap;
  - 显示文本（ftype 15/16）的自动换行：渲染的 Label 换行适应
    字段宽度而非省略截断。序列化以便自由布局携带
    较长的提示段落。

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
  - 按设计尺寸存储的备用自由画布矩形（LVGL 风格断点
    布局）："WxH:x,y,w,h;WxH:x,y,w,h"。fx/fy/fw/fh 始终保存
    当前设计尺寸的矩形；设计器中切换尺寸时把
    旧矩形存到这里，存在新矩形时再加载。

- bool locked;
  - 设计器锁定：锁住的组件仍可选中和改属性，
    但画布上拖不动、拉不大（和场景设计器的 Locked() 一致）。

- int dockSide;
  - 停靠边（Dock.Manual/Top/Bottom/Left/Right/Fill 的数字）。
    自由画布默认 0（给定的 fx/fy/fw/fh 绝对定位）；在
    Dock Panel 里改成某一边，就能做出“上工具条 + 左侧栏 +
    下状态栏 + 中间填充”这种应用外壳，且能随窗口缩放。

- List<string> options;

- List<FormField> kids;

- int childTab;

- SignalInt uiState;

- JsonValue extra;
  - 设计器未建模的字段键原样保留(DataGrid 的 "of"/"columns"、
    控件的 "props" 直通表等):加载时收集,保存时原样写回,
    手写设计稿经设计器往返不丢声明。为 null 表示没有。

- void InitField(int ft)

- static bool IsContainerOf(int ft)
  - 容器类型拥有嵌套子组件：Card (18) 有单一
    内容体；Panel (45) 是独立的容器面板；Tabs (36) 把子元素
    分配到各个标签页；Split (60) 分成两个窗格（childTab 0/1
    即窗格序号）；Dock Panel (61) 按各子元素自己的 `dock` 边停靠。

- static int SuggestDockOf(int ft)
  - 外壳条通常占哪一边：工具条贴顶、状态栏贴底、停靠面板
    吃掉剩下的空间。只是给「停靠」属性的建议值，组件建出来
    一律是自由定位（0），不会自己跑去贴边。

- static bool IsShellOf(int ft)
  - 应用外壳类容器 / 条（工具条、状态栏、分栏、停靠面板）。
    它们自带表面并在窗口边缘成条，因此设计器不给它们
    加标签列。

- bool IsContainer()

- int KidCount()

- FormField KidAt(int i)

- FormField PlaceAt(int x, int y)
  - 设置设计空间中的绝对位置（容器子元素相对于父内容
    框）。

- FormField FreeSize(int w, int h)

- int FreeX()

- int FreeY()

- int FreeW()

- int FreeH()

- void SetLayoutRect(int x, int y, int w, int h)
  - Runtime rectangle used by the designer canvas. It is deliberately
    separate from fx/fy/fw/fh so automatic layout cannot alter the source.

- void ResetLayout()

- int LayoutX()

- int LayoutY()

- int LayoutW()

- int LayoutH()

- void StashRect(string key)
  - 把当前 fx/fy/fw/fh 写入备用矩形表，键为
    "WxH"（覆盖该尺寸之前的条目）。

- bool LoadRect(string key)
  - 把 "WxH" 键下保存的备用矩形加载到 fx/fy/fw/fh。
    该尺寸无条目时返回 false（矩形保持不变）。

- static bool AltKeyIs(string entry, string key)

- void SeedOptions(int ft)
  - 为组件填充默认选项列表——选项值、表格列、
    标签页标题、步骤/菜单/树条目——使新添加的组件
    在预览中不至于空白。

- FormField(int ft)

- static FormField New(int ft, int seq)
  - 构建 `ftype` 字段并生成唯一的默认模型键。

- FormField Clone()
  - 深拷贝（子元素一并复制）：设计器的「复制」用它，
    副本除了模型名之外和原件完全一致。

- static int Custom()
  - Legacy palette value retained only while reading old designer state.

- bool IsCustom()

- static FormField NewCustom(string kind, int seq)
  - 构建携带发现的 `kind` 标签的自定义组件字段。

- static string TypeKey(int ft)
  - 用于默认模型名称的短类型键，如 "input"、"select"。

- static int TypeForKey(string key)
  - TypeKey 的逆运算：由规范字符串类型（即 .zform 的
    `kind` 字段中所写）求 ftype。"custom" -> 100；未知 -> 0（input）。

- static int MaxType()
  - 最大的内置字段类型（自定义组件为哨兵 100）。面板范围、
    序列化和编译期投影都以它为界。

- static string TypeName(int ft)
  - 供面板/检查器使用的可读类型名称。

- static List<string> SemanticEventsOf(int ft)
  - 某字段类型特有的语义事件（除每个 Control 都继承的通用
    指针/焦点/键盘/生命周期事件集之外）。驱动
    设计器的事件检查器和 JSON `on*` 处理器约定。

- override List<string> Events()

- static string DefaultLabel(int ft)

- static string DefaultPlaceholder(int ft)

- static bool UsesOptionsOf(int ft)
  - 字段类型是否带用户可编辑的选项列表。除
    选择型字段外，一些显示/导航组件也复用该选项
    这些组件的条目使用列表：表格列(23)、时间线事件(24)、树
    节点(25)、折叠区(26)、步骤(34)、面包屑(35)、
    标签页标题(36)和菜单项(38)。

- static bool IsNonVisualOf(int ft)
  - 非可视化（行为）组件，WinForms 组件托盘风格：它们
    不在设计窗口本身中占空间——设计器把它们显示在
    画布下方的托盘条里。Tooltip(33)、
    Loading(47)、Dropdown(48)、Float Button(49)、Popover(50)、
    Popconfirm(51)、Notification(52)、Context Menu(55)、Layer(56)、
    Timer(57)。

- bool IsNonVisual()

- bool UsesOptions()

- static bool IsDisplayOf(int ft)
  - 显示 / 布局 / 反馈 / 导航组件（ftype >= 15）渲染
    静态内容：它们横跨整行，跳过标签列。

- bool IsDisplay()

- int RowUnits()
  - 行高（未缩放），让较高的输入框（textarea/upload/richtext）和
    多选项字段在画布上获得所需空间。

- int OptionCount()

- string OptionAt(int i)

- void AddOption(string s)

- void RemoveOptionAt(int i)
  - 通过重建列表来移除一个选项（运行时的列表中段
    RemoveAt 与列表中段 Insert 一样可能破坏堆）。

- string JoinOptions()

- void SetOptionsFrom(string joined)

- string KindName()
  - Canonical .zform kind for this field. Custom fields retain the
    discovered class name directly; built-ins use their real widget kind.

- static string KindForType(int ft)
  - The single source of truth for "which widget class does this field
    become": the designer writes it into `kind`, the .zform generator
    declares it as the field's type, and TypeForKey reads it back. Every
    built-in type maps to a real Control with a parameterless constructor,
    so a design can never generate code that names a type that does not
    exist. Field types that have no widget of their own (a date or colour
    entry, an upload row) resolve to the control that actually collects
    them.

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)
  - 选项列表存为文本，逗号分隔的每项即一个选项。

- override bool SetExtra(string key, string val)


## Input (class)

带光标、占位符和响应式绑定的单行文本输入框。

保留式 C# 风格控件：拥有稳定标识（因此焦点在
兄弟/弹层变动后仍能保持），焦点/点击由框架解析，并
暴露委托事件：
Input name = Input.Bind(model, "your name");
name.Change += () => { ... };   // 每次编辑时触发
name.Submit += () => { ... };   // 回车时触发
name.Render(app, x, y, w, h);
获得焦点时消费键盘事件（可打印字符、Backspace、
Delete、Left/Right/Home/End），就地编辑绑定的 SignalString，并将
光标位置上报给框架，使 IME 组合窗口
跟随光标移动。

- SignalString model;

- Binding<string> data;
  - 双向绑定文本：`name.data = user.name;` 使输入框与
    模型双向保持同步（编译器降级的 Binding）。可选的；
    未设置时，输入框仅编辑其内部 SignalString。

- string hint;

- int cursorPos;

- int selAnchor;

- int wid;

- bool password;
  - 密码掩码：设置后文本以 `*` 渲染，直到被明文显示。

- bool reveal;
  - 掩码字段当前是否显示明文（通过眼睛图标切换）。

- string iconLead;
  - 可选的前导 / 尾随图标名（Gui.Icon）。密码框
    无论 `iconTrail` 如何都会在尾端绘制可点击的眼睛图标。

- string iconTrail;

- UiEvent Change;
  - 文本每次变化时触发（C# 风格：`inp.Change += h;`）。

- UiEvent Submit;
  - 获得焦点时按下回车触发。

- string errMsg;
  - 校验错误信息；"" 表示有效。SetError 会绘制红色边框，并
    在字段下方显示消息；任何编辑都会清除它。

- void InitInput(SignalString sig, string hintText)
  - 立即模式工厂与保留模式构造函数共用的初始化函数：
    设置 Control 基类（停靠顶部，使追加的输入框
    自动贴齐），再设置输入框专属状态。

- Input():this("")
  - Default design-time constructor; the form supplies `placeholder` later.

- Input(string hintText)
  - 保留模式构造函数：`Input name = new Input("Your name");`

- static Input Bind(SignalString sig, string hintText)

- static string Mask(string s)
  - 一个与 `s` 等长（按字节）的 `*` 字符串，使掩码显示保持
    相同的字节布局，光标/选区计算仍然有效。

- string GetText()

- int WidgetId()
  - Allows a host to move focus to this input programmatically
    (used for the inline rename box in a list).

- void SetText(string v)

- void SelectAll()
  - 选中全部文本（等同于用户按 Ctrl+A），因此下一次输入
    直接替换原内容。地址栏之类「点进来就是要换掉它」的
    字段在 Focus 事件里调用它。

- void SetError(string msg)
  - 给字段标记校验错误；绘制红色边框，并显示
    `msg` 于字段下方，直到用户编辑（或调用 ClearError）。

- void ClearError()
  - 清除待处理的校验错误（任何编辑时自动调用）。

- void SyncBinding()
  - 把绑定的模型值拉入编辑缓冲区（model -> UI）。

- void PushBinding()
  - 把编辑缓冲区通过绑定写回模型（UI -> model）。

- void Edited()
  - 所有 UI 编辑都汇入此处：先写回，再通知；同时清除
    任何校验错误——修正正是问题已解决的最强信号。

- void ClampCursor()

- bool HasSel()

- int SelStart()

- int SelEnd()

- string SelText()

- void DeleteSel()
  - 删除选中的范围，并把光标收拢到其起点。

- void HandleInput(App app)

- int Render(App app, int x, int y, int w, int h)

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)
  - 文本经 SetText 写入，保证光标与选区不越界。

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)
  - 将输入框自身的语义事件（Change/Submit）路由到对应的 UiEvent
    字段；其余事件落入 `On` 上的通用事件包。

- override void OnMeasure(App app)

- override void OnPaint(App app)


## Label (class)

文本标签。颜色与字号来自 `label` CSS 规则，通过
与设计系统其余部分共用的类来选择：

Label l = new Label { Text = "Name" };
l.Class = "secondary small";       // 弱化的说明文字
l.Text = vm.status;                // 绑定，每帧重读

类：`secondary` / `hint` / `inverse` 以及语义角色
（`primary` … `error`）决定颜色，`tiny` / `small` / `medium` / `large`
和 `title` 决定字号。

- Binding<string> Text;
  - 可绑定文本：`lbl.Text = vm.status;` 每帧重新读取模型字段
    （编译器降级的 Binding）；字面量则存为常量。

- bool wrap;
  - 自动换行：为 true 时，标签将文本折成
    适合槽宽的若干行，而不是用省略号截断。高度随行数变化。

- void InitLabel(string txt)

- Label()

- Label(string txt)

- string Str()
  - 当前文本（通过绑定解析）。

- StyleBox ResolvedStyle(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()

- override List<PropSpec> Props()


## Layer (class)

统一的 overlay 层——单个组件满足所有弹层需求
（layui `layer` 风格）：可拖动的页面窗口、模态 alert/confirm/prompt
对话框、顶部居中的消息 toast 队列、角落通知、加载
遮罩、锚定提示和全屏图片轮播。打开/关闭时均有动画，
并在延迟的 overlay 绘制阶段渲染，因此浮在
页面内容之上。

- static List<ToastItem> toasts;

- static List<ToastItem> notifs;

- static List<int> toastPrev;

- static List<int> notifPrev;

- static void EnsureQueues()

- static List<int> NewRect()

- static void MarkQueueDirty(App app, List<int> prev, bool any, int rx, int ry, int rw, int rh)
  - 声明这条队列这一帧的脏区，并把上一帧的脏区一起交上去。
    
    队列里的东西一直在动（滑入、淡出、幸存者上移收拢），只声明「它现在
    在哪」的话，上一帧它占过、这一帧已经离开的那几行像素没人重画，留在
    屏幕上就是残影；最后一条消失的那一帧更是连一次重绘都不会请求，于是
    那条 toast 会一直挂在屏幕上直到下一次整窗重绘。

- static int ActionWidth(App app, Button b, int minW)
  - 对话框操作按钮宽度：取标签自然宽度，不低于
    标准对话框按钮宽度，使一排按钮保持齐整。

- static int Clamp01(int v)

- static string RoleClass(int mtype)
  - toast/notification/dialog 类型到 CSS 角色类（1/默认无类）。

- static string RoleIcon(int mtype)

- static void Msg(string text, int type)
  - 入队一条短暂的顶部居中 toast。type：1 默认，2 信息，
    3 成功，4 警告，5 错误。

- static void RenderToasts(App app)
  - 绘制并推进 toast 队列。在 overlay 阶段每帧调用一次。
    条目从槽位正上方淡入/滑入，短暂停留后
    淡出；幸存者缓动上移，填补空隙。

- static Rect DrawToast(App app, int y, int a, string text, int mtype)

- static void Notify(string title, int type)
  - 入队一条角落通知。type：1 默认，2 信息，3 成功，
    4 警告，5 错误。

- static void RenderNotifs(App app)

- static int NotifyH(App app)
  - 单条通知 / 内联横幅卡片的高度。

- static void DrawNotify(App app, int x, int y, int w, string message, int ntype)
  - 绘制通知卡片（不透明）。也复用作静态预览渲染器。

- static void DrawNotifyCard(App app, int x, int y, int w, int a, string message, int ntype)

- static void RenderLoad(App app, int x, int y, int w, int h, string tip)
  - 覆盖某区域的加载遮罩：变暗的圆角面板，中央是
    旋转指示器和可选的说明文字。

- static void Tips(App app, int anchorX, int anchorY, string text)
  - 小型的锚定提示气泡（委托给共享的 Tooltip 视觉）。

- static int Modal(App app, int w, int h, string title)
  - 居中的模态面板，带标题栏 + 关闭按钮。返回
    关闭按钮的命中 id。（替代原 Modal.Render。）

- static int DrawPopconfirm(App app, int anchorX, int anchorY, string message, string okText, string cancelText)
  - 锚定在触发器下方的小型确认弹出框。返回 0 无，
    1 确认，2 取消。（替代原 Popconfirm.Render。）

- static bool RenderPhotos(App app, List<string> slides, SignalInt idx, CarouselAnim anim)
  - 全屏变暗的图片查看器，包装共享的 Carousel。
    关闭按钮被点击的那一帧返回 true。

- static int BarH(App app)

- static int FooterH(App app)

- static int BtnW(App app)

- static int BodyTop(App app, LayerState s)
  - 状态 `s` 下，调用方正文内容应开始的绝对 Y 坐标。

- static int BodyLeft(App app, LayerState s)
  - 正文内容应开始的绝对 X 坐标。

- static bool Inside(int mx, int my, int x, int y, int w, int h)

- static void Bounds(App app, LayerState s, List<int> outv)
  - `s` 实际在屏幕上的边界（考虑最大化 / 最小化），
    写入 4 元素 int 列表 [x, y, w, h]。

- static bool Contains(App app, LayerState s, int mx, int my)

- static int Entrance(App app, LayerState s)
  - layer 打开以来的入场进度（千分比）；仍在缓入时
    会重新启动动画计时。

- static void RenderStack(App app, List<LayerState> list)
  - 从后往前绘制页面窗口堆栈（最后一个 = 最顶层）。点击
    任一窗口都会将其置顶；只有指针下最顶层的窗口（或被拖动者）
    才消费鼠标事件。

- static void Render(App app, LayerState s)

- static void RenderActive(App app, LayerState s, bool active)

- static void RenderLayerBody(App app, LayerState s, int ex, int ey, int ew, int eh, int bar)
  - Layer 正文文本 + 可选的底部按钮（最小化时跳过）。

- static void RenderDialog(App app, LayerState s)
  - 居中的模态对话框：alert / confirm / prompt。将 s.action 设为
    1 (OK)、2 (Cancel) 或 3 (关闭 X)；调用方读取后关闭。

- static void CapButton(App app, int x, int y, int w, int h, string glyph, int gl, bool danger)
  - 一个内嵌的标题栏按钮：圆角 hover 背景 + 居中字形。


## LayerState (class)

浮动层的保留状态，支撑两种形态：
kind 0 —— 可拖动的“页面”窗口（layui `layer` 的 page/iframe 风格）：
持有几何信息、拖动状态及最小化/最大化/还原。
kind 1 —— 居中的模态对话框（alert / confirm），变暗的背景遮罩，
图标 + 标题 + 正文 + 操作行。
kind 2 —— 居中的提示对话框：类似 confirm 再加一个文本输入框。
state：0 正常，1 最大化，2 最小化（仅窗口）。

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

- int closeId;

- Input promptInput;

- int openMs;

- LayerState(string title, int x, int y, int w, int h)

- static LayerState Confirm(string title, string body, int dtype)
  - 居中的模态确认对话框（OK + Cancel）。dtype 决定图标。

- static LayerState Choose(string title, string body, int dtype, string okText, string altText, string cancelText)
  - 居中的模态对话框，提供三种出路——“保存它，
    丢弃它，还是保持原状”，这是关闭编辑器时必须问的问题。
    主操作报告 1，取消（及 X）报告 2 / 3，备选操作报告 4。

- static LayerState Alert(string title, string body, int dtype)
  - 居中的模态 alert 对话框（仅一个 OK 按钮）。

- static LayerState Prompt(string title, string hint)
  - 带文本输入框的居中模态 prompt 对话框（OK + Cancel）。

- void Open()

- string PromptText()
  - 用户在 prompt layer 中键入的文字（未输入时为空）。


## ListColumn (class)

ListView 的一列：表头显示什么、宽度相对其他列
如何（0 = 均分），以及如何从实体读取其文本。

- string title;

- int weight;

- bool right;

- CellOf<T> cell;

- ListColumn(string title, CellOf<T> cell)

- ListColumn(string title, int weight, CellOf<T> cell)

- ListColumn<T> Right()
  - 让本列文本右对齐（数字、大小、日期）。

- string TextOf(T item)


## ListView (class)

绑定到调用方自身数据的列表。只需提供三样东西——行
长什么样、数据、处理器——布局一概不用管：
控件位于树中，盒子由 CSS 决定，因此调用处无需计算
矩形或维护行模型。

ListView<FileItem> files = new ListView<FileItem>(cols);
files.Bind(model.files);          // 调用方的 List<FileItem>
files.OnSelect(OpenSelected);
side.Add(files);

行也可以是独立的组件而非文本列——
模板返回任意控件即可：

ListView<Msg> feed = new ListView<Msg>(m => MessageCard(m));
feed.Bind(model.inbox);

处理器被编排到 UI 线程执行（UiEvent.Post），因此后台
任务修改绑定列表并调用 Refresh() 也是安全的。

- List<T> data;
  - 调用方的数据。不做任何拷贝：每帧都从中读取行，
    所以修改列表后，下一次绘制即可生效。

- List <ListColumn<T>> cols;

- RowOf<T> rowOf;

- string Empty;
  - 列表无行时居中显示（"" 则不绘制）。

- int sel;

- int hover;

- bool multi;
  - 多选模式（WithMultiSelect）：Ctrl+点击把行加入/移出
    `marks`，Shift+点击从锚点扩展。`sel` 仍是聚焦行，
    单选调用方不受影响。

- List<int> marks;

- SignalInt scroll;

- bool rowsStale;
  - 模板模式：每个构建的子控件分配一个行 id，因此
    未重建任何行的帧里，选择状态也能保留。

- int builtFor;

- int rowSeqBase;
  - 模板行独占的 WidgetId 段起点（-1 = 还没建过行）。

- static int RowIdReserve()
  - 模板行独占的 id 段长度：够放几百行、每行十来个控件。

- UiEvent Select;
  - 某行被选中（读取 SelectedIndex / Selected）。

- UiEvent Activate;
  - 选中的行再次被点击（打开 / 深入）。

- UiEvent Context;
  - 某行被右键点击。

- void InitList()

- ListView()
  - 单列纯文本（不假定 `item.ToString()`：当实体不是字符串时，
    请绑定一个列）。

- ListView(List <ListColumn<T>> columns)
  - 常见形式：直接传列。

- ListView(ListColumn<T> column)
  - 单列，用于简单的标签列表。

- ListView(RowOf<T> row)
  - 自定义行：模板构建每行的控件子树。

- ListView<T> WithColumns(List <ListColumn<T>> columns)
  - 声明式构造后再给列（`.zform` 里声明的列表用默认构造函数，
    列在代码里补上：列标题多是本地化文本，属于代码而非设计）。

- ListView<T> WithRow(RowOf<T> row)
  - 声明式构造后再给行模板（`.zform` 里声明的列表用
    默认构造函数，行模板在代码里补上）。

- override string Kind()

- override string StyleType()

- ListView<T> Bind(List<T> src)
  - 绑定调用方的列表。列表不拷贝，因此这是其实时视图；
    需要重建自定义行的变更之后，调用 Refresh()。

- void Refresh()
  - 标记绑定数据已变更，自定义行将在下一帧
    重建。任意线程调用都安全。

- int Count()

- int SelectedIndex()

- bool HasSelection()

- T Selected()
  - 选中的实体；仅在 HasSelection() 为真时有效。

- bool Has(int row)

- T ItemAt(int row)
  - `row` 处的实体；仅在 Has(row) 为真时有效。

- void SelectIndex(int row)
  - 像点击那样移动选中项，发生变化时触发 Select。

- void ClearSelection()

- ListView<T> WithMultiSelect()
  - 启用多选（Ctrl+点击切换、Shift+点击范围选择）。

- bool IsMarked(int row)
  - `row` 属于多选集合（或为聚焦行）时为 true。

- List<int> SelectedIndices()
  - 所有选中的行索引（标记加聚焦行），升序排列。

- int SelectionCount()
  - 选中的行数。

- ListView<T> OnSelect(Action a)

- ListView<T> OnActivate(Action a)

- ListView<T> OnContext(Action a)

- ListView<T> EmptyText(string text)
  - 列表为空时显示的文本。

- bool Templated()

- int RowHeight(App app)

- int HeaderHeight(App app)

- void BuildRows(App app)
  - 通过行模板为每个实体重建一个子控件。子控件
    在此测量，因为发现需要重建时，树的测量阶段已越过
    它们。

- int ContentHeight(App app)
  - 行的总高度；当样式表未声明高度时，列布局用它
    来确定列表尺寸。

- int RowExtent(App app, Control row)

- int RowInteract(App app, int index, int x, int y, int w, int h)
  - 处理某行矩形的选择 / 激活 / 右键菜单。返回其注册的 id，
    调用方可据此绘制 hover 状态。

- void NoteSelfDamage(App app)
  - 选中一行只改变列表自己的像素，因此把损伤限定在控件矩形
    内，而不是整窗重绘（弹层里的列表会自动退回整窗重绘：
    覆盖层存在时 NoteDamage 本就不生效）。需要更大范围的宿主
    在自己的 Select/Activate 处理器里调用 RequestRedraw。

- void PaintEmpty(App app)

- List<int> ColumnWidths(int rowW)
  - 当前盒子的列宽：某列的 weight 是它对整行的占比，
    权重全为 0 时均分。

- void PaintHeader(App app, int headH)

- void PaintColumnRows(App app, int headH)

- void ArrangeRows(App app, int headH)
  - 在此盒子内按滚动偏移量布局模板行；
    从 OnPaint 运行，即在子控件绘制自身之前，
    因此树依然是它们坐标的唯一所有者。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override List<string> Events()

- override void BindEvent(string evt, Action a)
  - 将列表的语义事件（Select/Activate/Context）路由到对应的
    UiEvent 字段；其余事件落入 `On` 上的通用事件包
    （其 AddByName 忽略不认识的名称，因此 JSON/设计器中的 `onSelect`
    处理器不会静默地永远不执行）。


## Menu (class)

带响应式选中的垂直导航菜单（NaiveUI n-menu 风格）。
条目是扁平列表；选中项获得强调条 + 底色。
`scrollOffset` 滚动列表；完全在 [y, bottomY) 之外的行被剔除。

- static int RenderVertical(App app, int x, int y, int w, int bottomY, int itemH, List<string> items, SignalInt selected, int scrollOffset)

- static void HandleClick(App app, int firstId, int count, SignalInt selected)
  - 圆角表面上的图标 + 标签变体（模板选择器、向导）。
    行直接把点击的索引写回 `selected`。


## PageHeader (class)

页面页眉：返回箭头、标题和可选副标题行。与按钮一样，
它自己处理交互（箭头触发 `Back`），外观由
类和样式部件（`pageheader::back`、`::title`、`::subtitle`）决定：

PageHeader h = new PageHeader { Text = "Details" };
h.Subtitle = "GUI reference";
h.Back.On(vm.GoBack);

- Binding<string> Text;

- Binding<string> Subtitle;

- string Icon;
  - 返回图标字形；"" 表示隐藏。

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
  - 标题字体：页眉携带的标题类，默认 `h3`。

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)
  - 将返回箭头的 Back 路由到其 UiEvent 字段；其余事件
    落入 `On` 上的通用事件包（其 AddByName 忽略它
    不认识的名称）。


## Pagination (class)

带响应式页码绑定的分页组件。页数较多时，中间会
用省略号窗口化（NaiveUI 风格）：1 … 4 5 [6] 7 8 … 200。

外观来自类列表：`small` 缩小按钮，`pill` 让按钮变圆，
`simple` 把按钮条收窄成“prev  cur / total  next”。

Pagination pg = new Pagination(12, page);
pg.Class = "pill";
pg.RenderAt(app, x, y);

- SignalInt model;

- Binding<int> Page;
  - 双向绑定的当前页，从 1 开始。

- int totalPages;

- UiEvent Change;
  - 页码变化后触发。

- int baseId;

- int idCount;

- void InitPagination(int total, SignalInt m)

- Pagination():this(1)

- Pagination(int total)

- Pagination(int total, SignalInt m)

- static Pagination Of(int total, SignalInt m)

- void SetTotal(int total)

- int Total()

- static List<int> BuildPages(int totalPages, int current)
  - 为当前页构建可见页码列表。值 0 表示
    省略号占位。小范围（<= 7 页）显示全部页码。

- static int MeasureWidth(App app, int totalPages, int current)
  - 给定页数下控件的总像素宽度，供调用方
    居中放置。与绘制时所用的条目布局一致。

- int ButtonSize(App app, StyleBox s)

- int FontSize(App app, StyleBox s)

- int Radius(App app, StyleBox s, int btnSize)

- void EnsureIds(int entries)
  - Prev、每个窗口条目一个 id（含省略号，使 id 与
    布局对齐）以及 next；作为连续块一次性保留。

- void GoTo(App app, int pg)

- void SyncBinding()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void PaintArrow(App app, string glyph, int id, int cx, int btnSize, int radius, int ico, bool atEnd, StyleBox item, StyleBox arrow)
  - 一个 prev/next 控件：一个表面、一个在范围末端置灰的箭头，
    以及它的命中区域。

- void PaintSimple(App app)
  - `simple` 类：prev 箭头、“current / total”文本、next 箭头。

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)


## Panel (class)

表现型容器表面（卡片 / 分区），统一的
背景、边框、圆角与内边距均取自当前主题。

面板在内边距内测量并放置子控件，因此宿主
只需给它一个矩形：

Panel box = new Panel("main").Titled("Details");
box.Gap(app.theme.gapMedium);
box.With(new Label { Text = "Name" });
box.RenderInside(app, rect);

- int style;
  - 0 卡片（填充 + 边框），1 纯表面（仅填充），2 带标题的分区，
    3 布局（透明流式容器：无表面，尊重用户内边距）。

- string title;

- int surfaceColor;

- FxOptions fx;

- void InitPanel(string name, int st)

- Panel WithFx(FxOptions o)
  - 可选的动画，以数据配置（见 FxOptions）；裁剪绘制在
    面板表面内、其子控件之后。

- Panel()
  - Default design-time constructor; the form supplies the field name.
    设计里的面板默认只做布局（不画表面）：设计器里的容器绝大多数
    是分组行、工具条或列，画成卡片就会和里面控件自己的边框叠在
    一起。要一张卡片就在设计里声明 style = card。

- Panel(string name)
  - 保留模式构造函数：卡片容器 `new Panel("main")`。

- Panel Plain(int color)

- Panel Layout()
  - 纯布局宿主：不画表面、不取卡片内边距，只把自己的矩形
    交给子控件。放在矮行（工具栏、面板头）里的容器必须用它：
    卡片默认的 paddingLarge 会吃掉一行的全部高度，子控件只能
    溢出到宿主外面。

- Panel Titled(string t)

- static Panel Column()
  - 透明垂直流式容器：通过 With() 添加的子控件自上而下堆叠，
    尺寸由各自 OnMeasure 决定，用 Gap() 分隔、Pad() 内缩。
    容器根据子控件测量自身，因此可干净地嵌套在
    其他 Column/Row 中。

- static Panel Root(string name)
  - 窗口根容器：填满客户区、按停靠排布子控件的透明表面。
    窗口本身就是背景，因此根节点不画卡片（不然设计
    四周会多出一圈边框），内边距也留给调用方决定。

- static Panel Row()
  - 透明水平流式容器（子控件从左到右排列）。

- override string Kind()

- override string StyleType()
  - 流式容器（Panel.Row/Panel.Column）只做布局：不绘制任何表面，
    因此也不应继承样式表的 `panel` 规则——
    否则皮肤的 `panel { padding: 18px; backdrop-filter: blur(...) }` 会
    吃掉 Row 的内容框（使其各行互相重叠）并
    给没有内容的容器做毛玻璃。样式表可用 `stack`
    定位它们。

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- bool StyledSurface(App app, string type, StyleBox s)
  - 当解析出的盒子 `s` 与该类型的裸规则解析结果不同时返回 true——
    即 `<type>.<class>` / `#<name>` 规则或内联 Bg()/Gradient()/Radius()
    设置器专门给了这个容器一张表面。

- override void OnPaint(App app)

- Rect Inner()
  - 该面板表面给子控件留下的内容矩形，
    供在自绘面板内布局的宿主使用。


## Popover (class)

锚定在某一点的浮动面板，带小箭头凹口和
自动换行的正文。

C# 风格的有状态实例——自持打开状态，并在延迟的
覆盖阶段绘制，因此显示在其他控件之上、阻挡其下方一切输入
（无父子点击穿透），点击外部或按 Esc 关闭：
Esc：
Popover pop = new Popover("Details", "Title", "Body text.", 280);
pop.Render(app, x, y, w, h);
静态 Render(...) 是立即模式版本（状态由调用方管理）。

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
  - 有状态模式构造函数。

- bool IsOpen()

- int Render(App app, int x, int y, int w, int h)

- override void OnPaintOverlay(App app)
  - 延迟面板：在所有内容之后由 App.RunOverlays 绘制并派发事件。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int PaintPanel(App app, int anchorX, int anchorY, int w, string title, string body)
  - 绘制锚定面板（阴影、主体、边框、箭头凹口），
    正文自动换行并贴齐窗口各边。`anchorX` 为
    指针 x，`anchorY` 为触发控件正下方的 y。返回面板上
    注册的阻挡区域 id。

- static void Render(App app, int anchorX, int anchorY, int w, int h, string title, string body)
  - 立即模式版本：可见性由调用方管理。`anchorX` 为箭头
    x，`anchorY` 为锚点正下方的 y。自动换行并限制范围；
    `h` 被忽略（为源码兼容保留）。


## Progress (class)

带类型变体的进度指示器。
类型：default(1)、info(2)、success(3)、warning(4)、error(5)
形状：线性轨道条（Render / RenderText）或圆环
（Circle），两者都可选标注数字百分比。

- int percent;

- Binding<int> data;
  - 绑定百分比：`p.data = task.progress;` 每帧重新读取模型字段
    （编译器降级的 Binding）；设置后覆盖 `percent`。

- int ptype;

- int shape;
  - 0 = 线性条，1 = 圆环。

- void InitProgress(int pct, int pt)

- Progress():this(0, 1)
  - Default design-time constructor; properties configure the value later.

- Progress(int pct, int pt)
  - 有状态模式构造函数：`Progress p = new Progress(60, 3);`

- void SetPercent(int pct)

- int Percent()
  - 当前百分比：设置了绑定时返回绑定的模型值。

- Progress Ring()
  - 将此实例切换为圆环形状（可链式调用）。

- override string Kind()

- string VariantClass()

- override List<PropSpec> Props()

- override string GetExtra(string key)
  - 显示的百分比可能来自绑定值，因此通过
    Percent() 读取，且只写入本地字段。

- override bool SetExtra(string key, string val)

- void PaintBar(App app, int pct)

- void PaintRing(App app, int pct)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int Clamp(int percent)


## Prompt (class)

小型浮动提示面板（查找、跳转到行、重命名等）：带标题的
表面上有单行文本框，文本归调用方所有，
因此调用方完全掌控按键处理，而面板保持
主题外观。

- static int FieldHeight(App app)
  - 单个文本框行的高度。

- static int Height(App app, int rows)
  - 含 `rows` 行文本框的面板高度。

- static void Surface(App app, int x, int y, int w, int h, string caption)
  - 面板表面及其标题。字段和按钮在其上绘制。

- static void Caption(App app, int x, int y, string caption)
  - 面板的标题，用于标题需在表面
    绘制后才计算的面板。

- static void Field(App app, int x, int y, int w, string text)
  - 显示调用方所有文本的单行字段，超出框体部分裁剪。


## Radio (class)

带响应式 int 绑定（选中索引）的单选按钮。

C# 风格的有状态实例（每个选项一个）：
Radio a = Radio.Bind(model, 0);
a.Change += () => { ... };
a.Render(app, x, y, "Option A");
为兼容现有调用方保留旧的立即模式静态方法。

- SignalInt model;

- Binding<int> data;
  - 双向绑定的选中值：`r.data = form.gender;` 使模型字段与组选择
    双向保持同步（编译器降级的
    Binding）。组内每个单选按钮都绑定同一字段。

- int optionValue;

- int wid;

- string label;

- UiEvent Change;
  - 该选项被选中时触发（C# 风格：`r.Change += h;`）。

- void InitRadio(SignalInt m, int ov, string lbl)

- Radio()
  - Default constructor used by .zform and the string-kind registry.

- Radio(SignalInt m, int ov, string lbl)
  - 有状态模式构造函数：`Radio a = new Radio(model, 0, "Option A");`

- Radio(int ov, string lbl)
  - 自持信号：`Radio a = new Radio(0, "Option A");`，随后通过 `.data`
    在组内每个单选按钮上绑定同一模型字段——调用处无需任何信号
    连接代码。

- static Radio Bind(SignalInt model, int optionValue)

- bool IsSelected()

- void SyncBinding()
  - 将绑定的模型值拉入本地信号（model -> UI）。

- int Render(App app, int x, int y, string label)

- override List<string> Events()

- override string Kind()

- override List<PropSpec> Props()

- override void BindEvent(string evt, Action a)

- int PaintStyled(App app, int id, int x, int y, string text, bool selected)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## RadioGroup (class)

单选按钮组：所有选项共享一个 SignalInt（选中索引），每个选项一个
真实 Radio。默认选中第 0 项。

- SignalInt model;

- RadioGroup()

- SignalInt Selected()
  - 选中索引信号（供代码后置双向绑定）。

- override Control MakeItem(int index, string text)

- override string Kind()


## Rate (class)

星级评分（NaiveUI n-rate），带响应式 int 绑定与悬停预览。
使用矢量 Icon 组件（"star" / "star-empty"），使实心和空心
星星在各平台渲染一致。

C# 风格的有状态实例：
Rate stars = Rate.Bind(model, 5);
stars.Change += () => { ... };
stars.Render(app, x, y);

- SignalInt model;

- Binding<int> data;
  - 双向绑定的评分：`stars.data = review.score;` 使模型字段与评分
    双向保持同步（编译器降级的 Binding）。

- int maxStars;

- int baseId;

- UiEvent Change;
  - 评分变化时触发（C# 风格：`r.Change += h;`）。

- void InitRate(SignalInt m, int stars)

- Rate():this(5)
  - Default design-time constructor.

- Rate(SignalInt m, int stars)
  - 有状态模式构造函数：`Rate stars = new Rate(model, 5);`

- Rate(int stars)
  - 自持信号：`Rate stars = new Rate(5);`，再通过 `.data` 双向绑定——
    调用处无需任何信号连接代码。

- override string Kind()

- override List<PropSpec> Props()

- static Rate Bind(SignalInt model, int maxStars)

- int Value()

- void SyncBinding()
  - 将绑定的模型值拉入本地信号（model -> UI）。

- int Render(App app, int x, int y)

- override List<string> Events()

- override void BindEvent(string evt, Action a)
  - 将评分的 Change 路由到其 UiEvent 字段；其余一切
    落到 `On` 的公共事件集（其 AddByName 忽略
    不认识的名称）。

- void PaintStars(App app, int x, int y)
  - 按 `rate` CSS 规则绘制星星，悬停的星星预览
    将要设置的评分。

- override void OnMeasure(App app)

- override void OnPaint(App app)


## Result (class)

结果页：圆盘上的大型状态图标，加标题和描述。
状态与设计系统其他各处一样是语义 class，
皮肤可重新样式化，代码侧只是普通赋值：

Result r = new Result { Text = "Published", Class = "success" };
r.Desc = "The executable is ready.";

class：`info`（默认）/ `success` / `warning` / `error` 决定强调色
和默认图标；`result::icon`、`result::title` 与 `result::desc` 是
可设置样式的部件。

- Binding<string> Text;

- Binding<string> Desc;

- string Icon;
  - 覆盖状态 class 隐含的图标。

- void InitResult(string ttl, string ds)

- Result()

- Result(string ttl, string ds)

- string Label()

- string Description()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static string StatusClass(int status)
  - 数字状态对应的 class：0 info、1 success、2 warning、
    3 error——文档或设计器字段存储的值。取设计系统共享角色刻度上的偏移，
    而不是重复其映射表。

- static string GlyphOf(string cls)
  - 状态 class 隐含的图标（未指明其他角色时为 "info"）。

- static int ActionsY(App app, bool hasDesc)
  - 自定义操作内容可开始绘制的 Y 偏移（相对渲染 y）。

- override string Kind()

- override List<PropSpec> Props()

- override bool SetExtra(string key, string val)
  - 旧文档中的数字 `status` 选择对应的角色 class。


## Ribbon (class)

Office 风格功能区：水平条带，含大图标+标题命令
按钮和可选的分组分隔线。无状态/立即模式——Render
绘制条带并返回本帧点击命令的索引，
无则返回 -1。

List<RibbonCmd> cmds = new List<RibbonCmd>();
cmds.Add(RibbonCmd.Group("file", "New"));
cmds.Add(new RibbonCmd("folder", "Open"));
int cmd = Ribbon.Render(app, x, y, w, h, cmds);
if (cmd == 0) { ... }

- static int Height(App app)

- static int Render(App app, int x, int y, int w, int h, List<RibbonCmd> cmds)

- static int tipBaseY;
  - 条带底边的 Y，悬停提示锚定于此，使它们全部
    在功能区下方对齐而非跟随指针。在条带绘制期间设置
    （见 PaintItem）。

- static int TabStripHeight(App app)

- static int HeightTabbed(App app)

- static int RenderTabbed(App app, int x, int y, int w, List<string> tabs, SignalInt activeTab, List<RibbonGroup> groups)
  - 绘制标签条和活动条带，并返回本帧点击命令的全局索引
    （按顺序跨所有组计数），
    无则返回 -1。`activeTab` 是双向的。

- static int HeightGroups(App app)
  - 无标签页的分组条带：命令按命名组排列、组间竖线分隔，
    小命令每列堆叠 `rows` 个。给只有一屏命令、不值得
    分页的宿主用（设计器工具条就是这种）。返回本帧
    点击命令的全局索引（跨组按顺序计数），无则 -1。

- static int RenderGroups(App app, int x, int y, int w, List<RibbonGroup> groups)

- static int SmallColW(App app, RibbonGroup grp)
  - 分组命令条带本体，标签式和无标签式功能区共用。
    `rows` 是一列里堆叠多少个小命令。
    一组小命令的列宽：组内最宽的标签说了算，这样每列的
    图标和文字都对齐，长标签也不会被邻列压住。

- static int BandWidth(App app, List<RibbonGroup> groups, int bh, int rows)
  - 条带排完所有分组需要多宽（和 Band 里的列游标算法一致，
    只量不画）。宿主给的宽度不够时据此收缩排布。

- static int Band(App app, int x, int by, int w, int bh, List<RibbonGroup> groups, int firstId, int rows)

- static void PaintItem(App app, int id, RibbonItem it, int x, int y, int w, int h, bool small, StyleBox hoverStyle, StyleBox activeStyle, StyleBox textStyle, StyleBox mutedStyle, StyleBox accentStyle, StyleBox accentHoverStyle, StyleBox disabledStyle)

- static bool ItemClicked(App app, int id, RibbonItem it)


## RibbonCmd (class)

单个功能区命令：图标加下方标题。`sep` 在命令后绘制组
分隔线（用于在视觉上分组相关命令）。

- string icon;

- string label;

- bool sep;

- bool enabled;

- bool compact;

- bool on;

- string tip;
  - 悬停说明，显示在标题下方；否则只显示标题。

- RibbonCmd(string icon, string label)

- RibbonCmd SetTip(string text)

- string TipText()

- static RibbonCmd Switch(string icon, string label, bool on)
  - 纯图标开关。`label` 用于提示/无障碍
    名称；每帧用 SetOn 驱动状态。

- void SetOn(bool state)

- static RibbonCmd Group(string icon, string label)

- void SetEnabled(bool on)


## RibbonGroup (class)

命名的功能区命令组，绘制为带标题的簇，
与相邻簇以竖线分隔。

- string title;

- List<RibbonItem> items;

- RibbonGroup(string title)

- static RibbonGroup Of(string title, List<RibbonItem> items)
  - 从现成的项列表构建组，例如
    RibbonGroup.Of("File", new List<RibbonItem>{ ..., ... })

- RibbonGroup Add(RibbonItem it)


## RibbonItem (class)

Office 风格功能区组中的一个命令。`small` 渲染紧凑的
图标+标签行形式（每列三个堆叠）；否则为大的
图标+标题按钮。

- string icon;

- string label;

- bool small;

- bool tile;
  - 纯图标小方钮，按 Office 的图标格铺排（一列堆叠多个，
    标签只出现在提示里）。用于同一族的一批命令，
    例如六个对齐方式。

- bool menu;
  - 画一个下拉箭头（拆分按钮）：宿主自己在点击后弹菜单，
    功能区只负责表达"这个命令还有下一级"。

- bool enabled;

- bool toggle;
  - 开关在 `on` 时点亮绘制（强调色填充 + 强调色图标），使一排
    看起来像一排开关而非一排命令。

- bool on;

- string tip;
  - 悬停提示；留空时默认取标签。

- static RibbonItem Large(string icon, string label)

- static RibbonItem Small(string icon, string label)

- static RibbonItem Toggle(string icon, string label, bool on)
  - 小型开关，例如停靠布局中每个工具面板一个，
    （见 DockHost.AddSwitchGroup）。

- static RibbonItem Tile(string icon, string label)
  - 纯图标方钮。`label` 只用于提示。

- RibbonItem SetMenu()

- RibbonItem Disable()

- RibbonItem SetTip(string text)

- void SetOn(bool state)

- void SetEnabled(bool state)

- string TipText()
  - 悬停气泡显示：标题，及下方命令功能的描述
    （仅标题说明不了新信息）。


## ScrollColumn (class)

用于有状态 UiDoc 子树的垂直滚动容器。

子控件停靠为 "top"（各自带高度）；列将其堆叠，
当内容总高度超过容器时，按自管理的滚动量
向上偏移，并裁剪到其边界内（RenderTree
已推入裁剪区），隐藏滚出视野的行（使其不响应
点击），并绘制可拖动的滚动条。它复用立即模式
ScrollView 辅助类处理滚轮/拖动/滚动条，使行为与
其他滚动视图一致。在文档中以 "ScrollColumn" 类型注册。

- ScrollView sv;

- int contentH;

- int barReserve;

- void InitScrollColumn()

- ScrollColumn()

- override string Kind()

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)
  - 按自然高度自上而下堆叠子控件，向上偏移
    当前滚动量；完全在视口外的行被隐藏。

- override void OnPaint(App app)


## ScrollView (class)

自管理的垂直滚动容器。

创建一次并跨帧保留；它自持滚动偏移，指针在内部时消费
鼠标滚轮事件，将内容裁剪到
视口内，并仅在内容
溢出时绘制（并拖动处理）滚动条。调用方无需注册滚动条或跟踪滚动状态：

// 创建一次，放在其他状态旁：
ScrollView nav = new ScrollView();

// 每帧：
Rect area = new Rect(x, y, w, h);
int dy = nav.Begin(app, area, contentHeight);
// 按自然 Y 减去 dy 绘制各项；`area` 之外的内容
// 会被自动裁剪。
nav.End(app, area);

- int offset;

- int viewH;

- int contentH;

- int barW;

- bool edgeInset;

- bool dragging;

- int dragMouseY0;

- int dragOffset0;

- ScrollView()

- void SetEdgeInset(bool v)
  - 从右侧内收滚动条，避开窗口的调整大小
    边框（仅当视图紧贴窗口边缘时需要，例如
    详情面板——导航列表保持默认贴边位置）。

- int Offset()
  - 当前滚动位置（像素）。

- int MaxOffset()
  - 上次测量的视口/内容对应的最大有效滚动偏移。

- bool Overflowing()
  - 内容高于视口时返回 true（显示滚动条）。

- void ScrollTo(int y)
  - 以编程方式滚动到像素偏移（下一帧钳制）。

- int Begin(App app, Rect area, int contentHeight)
  - 开始一帧：测量视口、处理鼠标滚轮、钳制
    偏移并将绘制裁剪到 `area`。返回内容应上移的像素数，
    从每个项的自然 Y 中减去该值。

- void End(App app, Rect area)
  - 结束一帧：释放视口裁剪区，并在内容溢出时绘制
    滚动条并处理滑块拖动。传入与 Begin 相同的 Rect。


## Scrollbar (class)

滚动条轨道和滑块。

- static void RenderVertical(App app, int x, int y, int height, int contentHeight, int scrollOffset)

- static void RenderVerticalScroll(App app, int x, int y, int height, int viewH, int contentHeight, SignalInt offset)
  - 绑定滚动偏移信号的交互式垂直滚动条。支持
    滑块拖动和点击跳转：按下时滑块以指针为中心，
    因此点击轨道也会滚动到该处。`viewH` 为滚动表面的可见高度；
    `height` 为滚动条长度（通常与前者
    相同）。`offset` 更新范围为 [0, contentHeight - viewH]。

- static void RenderVerticalScrollIn(App app, int x, int y, int height, int viewH, int contentHeight, SignalInt offset, int vx, int vy, int vw, int vh)

- static void RenderVerticalScrollCore(App app, int x, int y, int height, int viewH, int contentHeight, SignalInt offset, bool scoped, int vx, int vy, int vw, int vh)

- static void HandleWheel(App app, int vx, int vy, int vw, int vh, int contentHeight, SignalInt offset)
  - 可滚动表面的鼠标滚轮滚动。传入完整的内容
    视口矩形（不只是滚动条），使指针位于内容
    任意位置时滚轮都能滚动。`offset` 更新范围为 [0, contentHeight-vh]。
    每帧与 RenderVerticalScroll 一起调用一次。


## SelectBox (class)

高层、自包含的 select / 下拉框。

有状态（创建一次，跨帧保留）。单个 Render() 绘制
触发控件和弹出层，并自持开/关、选项列表、单选或
多选、可选清除按钮、尺寸和状态变体，以及
选项列表较长时自滚动的弹出层（鼠标滚轮 + 滚动条 + 裁剪），
因此调用处只需：

SelectBox city = new SelectBox("Pick a city", false);   // 创建一次
city.AddOption("London"); city.AddOption("Paris");

city.Render(app, x, y, w);                           // 每帧
int i = city.Selected();          // 无选中时为 -1
string text = city.Text();

多选：SelectBox.Multi(hint) + IsChosen(i)。流畅配置：
new SelectBox(hint, false).Clearable(true).Size(SelectBox.Large())
.Status(SelectBox.Error())

绑定：`sel.data = form.color;` 将选中索引双向绑定到
普通字段；`SelectBox.Bind(options, signal, hint)` 与其他控件共享 SignalInt，
且 `sel.Change += h;` 在每次选择变化时触发。

- List<SelectOption> opts;

- int selected;

- SignalInt model;
  - 可选共享选择信号（SelectBox.Bind）。

- Binding<int> data;
  - 双向绑定的选中索引：`sel.data = form.color;`（编译器降级的
    Binding）。新变动的一方生效，弹出层与字段保持同步。

- int lastModel;

- int lastData;

- UiEvent Change;
  - 选择变化时触发（`sel.Change += h;`）。

- UiEvent Open;
  - 选项列表打开/关闭时触发，无论由哪种方式
    触发（点击触发控件、选择、外部按压、Esc）。

- UiEvent Close;

- string hint;

- bool multi;

- bool clearable;

- bool open;

- int scrollY;

- int size;

- int status;

- int maxVisible;

- int trigX;

- int trigY;

- int trigW;

- int wid;
  - 稳定控件 id：触发控件注册它，使悬停/按压/焦点（及读取它们的
    缓动样式过渡）与任何其他控件一样工作。

- int lastH;
  - 上一帧实际绘制的头部高度（尊重显式 RenderIn
    高度）；延迟弹出层读取它，使其正好位于触发控件下方。

- int hoverRow;
  - 上一帧指针悬停的选项（无则 -1）。弹出层在覆盖阶段绘制
    且未按行注册命中区域，因此没有其他机制
    会请求重绘光标下那行的帧。

- int cols;
  - 弹层列数：0 = 自动（多选框选项多于一屏时分成等宽多列，
    而不是拉成一条长条），否则固定列数。

- static int Small()

- static int Medium()

- static int Large()

- static int Normal()

- static int Success()

- static int Warning()

- static int Error()

- SelectBox():this("", false)
  - Default design-time constructor; the form supplies `placeholder` later.

- SelectBox(string hintText):this(hintText, false)
  - 单选下拉框（多选用 `new SelectBox(hint, true)`）。

- SelectBox(string hintText, bool isMulti)

- static SelectBox FromOptions(List<string> options, string hint)
  - 从普通标签列表构建选择框（JSON / 设计器 / 单行场景使用）。

- static SelectBox Bind(List<string> options, SignalInt model, string hint)
  - 共享外部 SignalInt 作为选中索引：读取同一信号的每个控件
    都保持同步。

- string OptionsText()
  - 选项以 `a|b|c` 字符串表示（设计器 / .zform 属性往返）。

- void SetOptionsText(string text)

- SelectBox Columns(int n)
  - 固定弹层列数（等宽）；0 恢复自动。选项按列优先填，
    先从第一列自上而下。

- int ColumnCount()
  - 本次开层实际的列数。自动时：单选保持单列（下拉就该是
    一条列表）；多选超过一屏时按 maxVisible 分列，最多 4 列，
    这样一屏就能成行成列地看完。

- int RowCount()
  - 当前列数下每列的行数。

- int Value()

- bool IsOpen()

- void SetOpen(bool v)
  - 打开/关闭列表，并在实际切换时触发 Open/Close，
    使每个入口（触发控件、选择、外部按压、Esc、代码）以相同方式
    报告。

- void SyncBinding()
  - 将共享信号和双向绑定与 `selected` 协调一致。
    弹出层直接写 `selected`，因此每一侧都与
    上次同步值比较，发生变动的一方生效。

- void Choose(int idx)
  - 应用弹出层作出的选择：将绑定和 Change
    事件集中在一处，使所有入口行为一致。

- static SelectBox Multi(string hintText)

- void AddOption(string label)

- SelectBox Clearable(bool v)

- SelectBox Size(int v)

- SelectBox Status(int v)

- int Selected()

- bool IsChosen(int i)

- bool HasValue()

- void Select(int idx)
  - 以编程方式设置选中索引（-1 清除）。保持绑定的
    model/data 同步，但不触发 Change（调用方发起的更新）。

- void Clear()

- string Text()

- int HeadHeight(Theme t)

- int FontSize(App app, StyleBox s)

- string StyleClass()
  - 校验状态以额外 class 的形式进入样式层（base.css 的
    `select.success/.warning/.error` 规则），绘制代码不再直接
    覆盖边框颜色。

- override string Kind()

- override List<PropSpec> Props()

- override string GetExtra(string key)
  - 选项列表为文本，每项一个 `label|value` 对。

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void Render(App app, int x, int y, int w)
  - 以自然（样式/尺寸）高度绘制触发控件。

- void RenderIn(App app, int x, int y, int w, int hIn)
  - 在显式盒子内绘制触发控件；`hIn` 为 0 时高度取自
    解析出的样式/尺寸变体，其他值优先生效，使工具栏一行
    的 select、按钮和输入框共享同一高度。

- int PopupWidth(App app, Theme t, int trigWidth, int fs, int arrowSlot)
  - 弹层宽度：不窄于触发器，但至少能放下最长的一条；
    多列时按列数等宽扩开。按触发器宽度开弹层会把每一条
    都截成同一个前缀，选项之间看不出区别。

- override void OnPaintOverlay(App app)
  - 延迟弹层：在 App.RunOverlays 中、所有内容绘制完成后绘制并派发事件。


## SelectOption (class)

一个下拉选项：其标签及是否被选中（多选）。
用一个实体代替平行的选项/选中列表。

- string label;

- bool chosen;

- SelectOption(string label, bool chosen)


## Skeleton (class)

加载占位控件：一个纯色填充框。填充和圆角来自
`skeleton` CSS 规则，皮肤可一次性重设所有占位符，而
代码侧只需简单赋值：

Skeleton s = new Skeleton { Width = 200, Height = 16 };
Skeleton avatar = new Skeleton { Class = "circle", Width = 40 };

类：`text` 是一行文字，`circle` 是圆形标记（头像
占位符），`shimmer` 动画类可让任意一种动起来。

- int Width;
  - 占位符尺寸（CSS px）；`width` / `height` 规则优先于此值。

- int Height;

- void InitSkeleton(int w, int h)

- Skeleton()

- Skeleton(int w, int h)

- StyleBox ResolvedStyle(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)
  - 占位框本身：填充、圆角和动画均由样式决定。

- static Skeleton Line(App app, int width)
  - 一行 `width` px 宽的占位文字，高度与所代表的文本一致。

- override string Kind()

- override List<PropSpec> Props()


## Slider (class)

滑块，支持响应式 int 绑定与实时拖拽 / 点击定位。
按住滑块（或轨道）时值跟随指针移动，
与 NaiveUI 的 n-slider 行为一致。所有尺寸均按 DPI 缩放。

C# 风格保留式实例：
Slider vol = Slider.Bind(0, 100, model);
vol.Change += () => { ... };   // 拖动过程中值变化时触发
vol.Render(app, x, y, width);
为现有调用方保留旧版立即模式静态方法。

- int minVal;

- int maxVal;

- SignalInt model;

- Binding<int> data;
  - 双向绑定值：`vol.data = settings.volume;` 保持模型
    字段与滑块双向同步（编译器降级的 Binding）。

- int wid;

- UiEvent Change;
  - 值变化时触发（C# 风格：`s.Change += h;`）。

- UiEvent DragEnd;
  - 拖拽/定位手势结束时触发（在滑块上松开指针）。

- void InitSlider(int lo, int hi, SignalInt m)

- Slider():this(0, 100)
  - Default design-time constructor.

- Slider(int lo, int hi, SignalInt m)
  - 保留式构造器：`Slider vol = new Slider(0, 100, model);`

- Slider(int lo, int hi)
  - 自持信号：`Slider vol = new Slider(0, 100);`，之后通过 `.data` 双向绑定，
    调用点无需任何信号接线。

- static Slider Bind(int minVal, int maxVal, SignalInt model)

- int Value()

- void SyncBinding()
  - 将绑定的模型值拉入本地信号（model -> UI）。

- int Render(App app, int x, int y, int width)

- override List<string> Events()

- override string Kind()

- override List<PropSpec> Props()

- override void BindEvent(string evt, Action a)

- bool PaintStyled(App app, int id, int x, int y, int width)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## Spin (class)

动画加载指示器：一圈圆点，带旋转的高亮头部和
渐隐的尾迹。颜色来自 `spin` CSS 规则（accent 是头部，background 是静止圆点），
皮肤可重设样式，而代码侧
只需简单赋值：

Spin s = new Spin { Size = 36, Class = "info" };

类：语义角色（`primary` … `error`）决定强调色，尺寸
级别（`tiny` … `large`）决定直径。

- int Size;
  - 直径（CSS px）；`width` / `height` 规则优先于此值。

- int Accent;
  - 圆点颜色；0 表示采用 `spin` 规则的 accent（在自己内容中绘制加载指示器的宿主，
    如加载中的按钮，可设置其前景色）。

- Binding<string> Tip;
  - 圆点下方的说明文字（由 `spin::label` 部件设置样式）；空表示
    纯指示器。

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
  - 指示器包围盒与窗口表面重叠时返回 true。

- static int PivotSafeMul(int per1000, int radius)

- override string Kind()

- override List<PropSpec> Props()


## Split (class)

可拖拽分隔条，把区域分成两个可调大小的面板——
IDE 风格布局的构件（项目树 | 编辑器，编辑器 |
输出）。第一个面板的尺寸保存在响应式 SignalInt 中，
可跨帧保持并可在别处绑定。

立即模式用法：每帧用要分割的区域调用工厂。
它绘制分隔条、处理进行中的拖拽（更新信号），
并返回一个 Split，调用方把内容布局到其 `first`/`second` 矩形中：

Split sp = Split.Vertical(app, area, sidebarW, 160, 320);
RenderSidebar(app, sp.first);
RenderEditor(app, sp.second);

- Rect first;
  - 第一个（左/上）面板的内容矩形。

- Rect second;
  - 第二个（右/下）面板的内容矩形。

- int handleId;
  - 拖拽句柄的焦点 id（需要自定义命中查询时使用）。

- static Split Vertical(App app, Rect area, SignalInt firstW, int minFirst, int minSecond)
  - 将 `area` 分割为左|右面板，带垂直拖拽句柄。
    `firstW` 保存左面板宽度（px）。`minFirst`/`minSecond` 限制
    拖拽范围，防止任一面板缩到无法使用的宽度。

- static Split Horizontal(App app, Rect area, SignalInt firstH, int minFirst, int minSecond)
  - 将 `area` 分割为上|下面板，带水平拖拽句柄。
    `firstH` 保存上面板高度（px）。


## SplitPanel (class)

保留式两栏容器，中间是可拖拽的分隔条——IDE / 上位机
外壳的基本骨架（导航树 | 编辑区，画面 | 报警列表）。

与立即模式的 `Split` 不同，它是控件树里的一个
节点：两个窗格是它的子控件，宿主（或窗口设计器）往
`First()` / `Second()` 里添加内容即可，分栏尺寸由控件自己
跨帧持有：

SplitPanel sp = new SplitPanel(SplitPanel.Vertical(), 240);
sp.First().Add(nav);
sp.Second().Add(editor);
root.Add(sp);

拖动分隔条改变第一个窗格的尺寸（垂直分隔条改宽度，
水平分隔条改高度），受 MinSizes() 限制。

- int orient;
  - 0 = 垂直分隔条（左 | 右），1 = 水平分隔条（上 | 下）。

- SignalInt firstSize;
  - 第一个窗格的尺寸（px，未缩放的逻辑像素由调用方决定），
    是信号以便别处绑定/持久化。

- int minFirst;

- int minSecond;

- Panel firstPane;

- Panel secondPane;

- int wid;

- int handleX;
  - 分隔条上一次的命中矩形，供 OnPaint 之后的拖拽处理使用。

- int handleY;

- int handleW;

- int handleH;

- static int Vertical()

- static int Horizontal()

- void InitSplitPanel(int o, int size)

- static Panel NewPane(string name)
  - 透明窗格：没有自己的表面，子控件按停靠布局排布，
    因此设计器放进去的绝对定位控件和停靠控件都能工作。

- SplitPanel(int o, int size)

- SplitPanel(int o)

- SplitPanel()

- Panel First()

- Panel Second()

- int Size()

- void SetSize(int px)

- SplitPanel MinSizes(int first, int second)
  - 拖拽下限：两个窗格各自允许的最小尺寸（px）。

- SplitPanel Orient(int o)

- override string Kind()

- override List<PropSpec> Props()

- override Control SlotHost(int slot)
  - 设计里放进某个窗格的子控件（.zform 的 `childTab`：0 = 第一格，
    1 = 第二格）真正的父节点就是那个窗格——否则它们会挂在
    分栏容器自己身上，而 Arrange 只摆两个窗格，于是全部消失。

- override string GetExtra(string key)
  - 方向与分栏尺寸也可以写成一条 `options`（"vertical|620"），
    这是设计器列表型属性的通用写法。

- override bool SetExtra(string key, string val)

- override void OnMeasure(App app)

- override void Arrange(int px, int py, int pw, int ph)
  - 两个窗格按当前分栏尺寸排布，分隔条占中间的
    一条（其宽度按 DPI 缩放，命中区再向两侧放宽）。

- static int BarSize()
  - 逻辑像素的分隔条厚度。静态而非主题值，
    因为 Arrange 拿不到 App（布局在测量之后进行）。

- int Clamp(int want, int total, int bar)
  - 把请求的分栏尺寸夹到两个窗格的最小值之间。

- override void OnPaint(App app)


## States (class)

规范的交互状态颜色。

作为唯一事实来源，所有组件以相同方式着色 hover / pressed /
selected / focused / disabled 状态，而不是每个控件
各自手选主题字段（容易漂移并产生不一致或
难以辨认的状态）。优先级始终为：

disabled > pressed > selected > hovered > normal

用法：
int bg = States.Bg(app, hovered, pressed, selected, disabled);
int fg = States.Text(app, selected, disabled);
int bd = States.Border(app, hovered, pressed, focused, disabled);

- static StyleBox Part(App app, string part, int state)
  - 给定状态下中性（默认类型）表面的背景填充。

- static int Bg(App app, bool hovered, bool pressed, bool selected, bool disabled)

- static bool HasBg(bool hovered, bool pressed, bool selected, bool disabled)
  - 当状态确实需要绘制背景时返回 true（normal 在多数表面上是
    无操作，调用方可跳过填充）。

- static int Text(App app, bool selected, bool disabled)
  - 前景色 / 文字 + 图标颜色。

- static int TextMuted(App app, bool hovered, bool selected, bool disabled)
  - 次要前景色，hover 时变亮（用于未选中项）。

- static int Border(App app, bool hovered, bool pressed, bool focused, bool disabled)
  - 边框颜色，包括焦点环。

- static int Accent(App app)
  - 用于选中条 / 活动指示器的强调色。


## Statistic (class)

统计组件：大数值上方的小标题。两者都是样式部件，
CSS 中 `statistic::label` 和 `statistic::value` 控制其颜色和
字号。

Statistic s = new Statistic { Text = "Active users", Value = "12,480" };

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

窗口状态栏：一行停靠的小文本项，靠左或
靠右对齐，位于带细顶部分隔线的主题表面上。
各项是 Label，文本颜色、省略号和样式与其它控件一样跟随主题；
调用方拥有项文本及其含义。

StatusBar bar = new StatusBar();
int posItem = bar.AddLeft("Ln 1, Col 1");
int encItem = bar.AddRight("UTF-8");
// 每帧：
bar.SetText(posItem, "Ln 12, Col 4");
bar.RenderAt(app, 0, y, width, height);

- List<Label> items;

- int tintBg;

- string itemsSpec;
  - `options` 的原样文本，供检查器 / 序列化按原样回读。

- StatusBar()

- override string Kind()

- override List<PropSpec> Props()

- Label NewItem(string text)

- int AddLeft(string text)
  - 追加一个左对齐项；返回其索引供后续更新。

- int AddRight(string text)
  - 追加一个右对齐项；返回其索引供后续更新。

- void Clear()
  - 丢掉所有项。

- void SetItemsText(string spec)
  - 按序列化文本重建全部项：每条一个标题，`|` 分隔，
    前缀 `>` 靠右。设计器与 .zform 走这条路径。

- override string GetExtra(string key)
  - 项集合是一个列表，不是字段，因此走 extra 属性：
    设计器 / .zform 的 `options` 由此真正建出状态项。

- override bool SetExtra(string key, string val)

- void SetText(int index, string text)

- void SetColor(int index, int color)

- void SetTint(int bg)
  - 纯色背景着色，替换默认表面（0 恢复默认），
    用于宿主想在整个状态栏传达某种模式时。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void RenderAt(App app, int x, int y, int w, int h)
  - 将状态栏渲染到显式矩形中（供自行管理
    窗口布局的宿主使用）；作为 Control 树的停靠子控件时无需此方法。


## Step (class)

一步：标签和可选描述行。以实体存储，
使标签和描述保持为一条连贯记录，而不是两条
必须手动保持索引对齐的并行列表。

- string label;

- string desc;

- Step(string label, string desc)


## Steps (class)

步骤指示器：编号圆点由连接线串联，当前
步骤从信号读取。圆点、连接线和标签都是样式部件
（`steps::circle`、`::line`、`::label`、`::desc`），每个部件按
步骤状态解析为类，皮肤可重设样式，代码侧只需
简单赋值：

Steps s = new Steps(new SignalInt(0)) { Class = "vertical" };
s.AddStep("Details");
s.AddStepDesc("Account", "Email and password");
s.ErrorIndex = 1;

类：`vertical` 纵向堆叠步骤；语义角色（`primary` … `error`）
选择强调色；皮肤可针对每步状态类 `done`、
`active`、`pending` 和 `error` 设置样式。

- List<Step> steps;

- SignalInt model;

- Binding<int> data;
  - 双向绑定当前步骤：`steps.data = wizard.step;` 保持模型
    字段与指示器同步（编译器降级的 Binding）。可选；
    未设置时指示器仅读取内部 SignalInt。

- int ErrorIndex;
  - 标记为出错的步骤；-1 表示无。

- void InitSteps(SignalInt m)

- Steps(SignalInt m)

- Steps()
  - 自持信号：`Steps s = new Steps();`，之后通过 `.data` 双向绑定当前
    步骤——调用点无需任何信号接线。

- void SyncBinding()
  - 将绑定的模型值拉入本地信号（model -> UI）。

- void AddStep(string label)

- void AddStepDesc(string label, string desc)
  - 添加一个带次要描述行的步骤（纵向布局中显示）。

- static Steps Of(List<string> labels, SignalInt m)
  - 基于现有标签列表的跟踪器，由 `m` 驱动。

- bool IsVertical()

- int RowGap(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void PaintBodyHorizontal(App app)

- void PaintBodyVertical(App app)

- static string StateClass(string cls, int index, int current, int errorIndex)
  - 步骤在序列中的位置所对应的类列表，使步骤的每个
    部件（圆点、标签、描述）都由同一个状态解析。
    状态名是设计系统共享的序列词汇。

- void Circle(App app, int cx, int cy, int r, int index, int current, int errorIndex, string cls)
  - 步骤圆点：已完成（对勾）、出错（!）、活动或待处理（数字）。

- void Line(App app, int x0, int y0, int x1, int y1, int index, int current, int errorIndex, string cls)
  - 两个圆点之间的连接线，采用后续步骤选定的样式。

- override string Kind()

- override List<PropSpec> Props()


## StyledText (class)

聊天记录的展示网格：一个自管绘制的内容型控件，承接整行网格——
块底色 / 竖条 / 代码底纹 / 正文 / 选中反白 / 头行（委托给 Avatar +
Tag）/ busy 提示。它把记录渲染从视图层搬到标准库组件，这样
ChatArea 作为视图层不再有任何画布自绘调用。

每帧由调用方绑定：行缓存、滚动、busy、选择状态、交互开关、
思考折叠信号、工具展开列表，以及 busy 行要显示的文本（已由调用方
本地化，控件本身不依赖 IDE 的翻译层）。绘制完成后 `maxScroll`
写出最大滚动偏移，供调用方回读。

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
  - 调用方的滚动偏移信号（底端锚定，单位是展示行，0 = 贴住最新一行）。
    给了它就在右侧画一条可拖动的纵向滚动条：只有滚轮的时候，长记录既
    看不出自己停在整段的哪一段，也没法一把拖回去。null = 不画。

- static Label thinkingLbl;

- static SignalInt barSig;

- static SignalInt BarSig()

- static int BarW(App app)
  - 纵向滚动条占掉的宽度。调用方按它收窄换行宽度，最长的一行才不会被
    压在滑块底下。

- static void EnsureThinking(App app)

- StyledText()

- static int FontPx(App app)
  - 记录的字号，解析自各行绘制时用的同一套样式，使测量与绘制不会错位。

- static int RowH(App app)
  - 一行的高度：一行文本加呼吸空间，且不小于头行要画的芯片。两者
    都解释了之前面板为何显得局促错位——固定 18px 在大 UI 缩放
    下比字体的行盒更紧，也比它要装的 Tag 更矮。

- static int SepH(App app)
  - 轮次之间的分隔线高度：块与块之间只让出这么多，不再插空行。

- static int RowAt(int startY, int pad, int rowH, int start, int count, int mouseY)
  - `mouseY` 下的展示行索引（底端锚定的记录，第一可见行为 `start`）。

- static string Initial(string name)
  - 发言者名字的首个可见字符，用于圆形 Avatar。CJK 名字保留首字；
    拉丁名字大写。

- static Avatar headAva;

- static Avatar HeadAvatar()

- static void PaintHeadRow(App app, TextRun ln, int x, int y, int rowH, int rightEdge, StyleBox timeStyle)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override string Kind()


## Switch (class)

开关，支持响应式 bool 绑定。

C# 风格保留式实例：
Switch dark = Switch.Bind(model);
dark.Change += () => { ... };
dark.Render(app, x, y);
为现有调用方保留旧版立即模式静态方法。

- SignalBool model;

- Binding<bool> data;
  - 双向绑定状态：`sw.data = settings.darkMode;` 保持模型
    字段与开关双向同步（编译器降级的 Binding）。

- int wid;

- UiEvent Change;
  - 开关切换时触发（C# 风格：`sw.Change += h;`）。

- void InitSwitch(SignalBool m)

- Switch()
  - 保留式构造器：`Switch dark = new Switch();`

- static Switch Bind(SignalBool model)

- bool IsOn()

- void SetOn(bool v)

- void SyncBinding()
  - 将绑定的模型值拉入本地信号（model -> UI）。

- int Render(App app, int x, int y)

- override List<string> Events()

- override string Kind()

- override List<PropSpec> Props()

- override void BindEvent(string evt, Action a)

- int PaintStyled(App app, int id, int x, int y, bool isOn)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## Table (class)

带表头和斑马纹行的数据表。

- static void RenderHeader(App app, int x, int y, List<string> columns, List<int> widths)

- static void RenderRow(App app, int x, int y, List<string> cells, List<int> widths, bool striped)


## Tabs (class)

- List<TabItem> tabs;

- int active;

- int style;

- int orient;

- bool showAdd;

- int scroll;

- bool follow;

- bool hostModel;

- int ctxIndex;

- List<Panel> pages;
  - 设计出来的页容器（下标 = 标签索引）。有了它，标签条
    本身就是容器：设计器 / .zform 里放在某个标签下的控件
    经 SlotHost 落进对应的页，切换标签即切换页面，宿主
    不必自己隐藏和摆放每一页。为空时控件行为与从前一致
    （只画标签条，页面由宿主自行绘制）。

- string itemsSpec;
  - `options` 的原样文本，供检查器 / 序列化按原样回读。

- int hdrH;
  - 上一次测量出的表头高度与侧栏宽度：Arrange 拿不到 App，
    而页面区正是表头之外的那块。

- int trackW;

- int changedIndex;

- int closedIndex;

- int addedIndex;

- List<int> closedQueue;
  - 还没被宿主领走的关闭索引，从大到小排队。批量关闭（关闭全部 /
    关闭其他）一次移除多个标签，而宿主每帧只 TakeClosed() 一次并按
    索引删自己的并行数组（页、浏览器…）：只报一个索引，宿主就少删
    几项，剩下的条目全部错位到别人的页上。倒序入队使每个上报的
    索引在宿主删到它之前始终指向自己那一项。

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

- static int lang=1;
  - 内置上下文菜单标签的 UI 语言（0 = 英语，1 = 简体中文）。宿主可在
    渲染前改它，让菜单跟随应用语言；默认简体中文，因为不设语言的
    发布版界面本身就是中文的，此时菜单不该是唯一说英语的地方。

- static string TT(string en, string zh)

- static int Line()

- static int Card()

- static int Segment()

- static int Horizontal()

- static int Vertical()

- static Tabs CreateVertical(int style)

- Tabs():this(0, 0)
  - 默认标签条：Line 外观、水平方向。（没有这个构造，`new Tabs()`
    会跳过唯一的构造函数，留下一个字段全为 null 的对象。）

- Tabs(int style):this(style, 0)
  - 给定外观的水平标签条；垂直轨道用 `new Tabs(style, Tabs.Vertical())`。

- Tabs(int style, int orient)

- void SetShowAdd(bool on)

- void SetHostModel(bool on)
  - 宿主驱动模式：控件不再拥有标签页集合。
    关闭 / 添加 / 右键操作上报给宿主（宿主拥有
    真实模型并每帧重新同步标签），而不是修改
    内部列表或打开内置上下文菜单。

- override string Kind()

- override string StyleType()

- override List<PropSpec> Props()

- Panel Page(int i)
  - 标签 i 的页容器，按需补齐（缺页的标签是空白页）。
    页是透明的停靠容器，因此页里的控件既能停靠也能
    绝对摆放，和窗口根一样。

- int PageCount()

- void DropPage(int i)
  - 丢掉第 i 页（标签被关掉时）。页以下标对应标签，因此删标签
    必须同步删页，否则剩下的标签全部错位到前一页的内容上。
    页也从控件树上摘下，它里的控件因此不再测量/绘制/接事件，
    持有它们的宿主也能据此（HostForm() == null）回收资源。

- override Control SlotHost(int slot)
  - 设计里放在某个标签下的控件（.zform 的 `childTab`）真正的
    父节点就是那一页。

- void SetItemsText(string spec)
  - 按序列化文本重建标签：每条一个标题，`|` 分隔，
    前缀 `x` 表示该标签可关闭。设计器与 .zform 走这条路径。

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)
  - 将标签条的语义事件（TabChanged/TabClosed/TabAdded）
    路由到对应的 UiEvent 字段，使 JSON/设计器的 `onTabChanged` 处理器
    可与每帧的 TakeChanged()/TakeClosed()/TakeAdded() 轮询并存；
    其余事件全部落入 `On` 的公共绑定。

- override void OnMeasure(App app)
  - 首选尺寸：水平条取内容宽度（容器可能压缩它们，
    此时出现溢出箭头）；垂直
    轨道取固定列宽，每个标签占一行。

- override void Arrange(int px, int py, int pw, int ph)
  - 页面区是表头（或侧栏）之外的那块：活动页铺满它，
    其余页隐藏（因此不测量、不绘制、不接事件）。没有
    设计页时退化为基类的停靠布局。

- override void OnPaint(App app)
  - 标签条停靠在控件树中时，会绘制到自己的
    布局边界内；显式的 Render/RenderVertical 入口保留
    给自行定位头部的宿主。

- int TakeContext()

- void Add(string label, bool canClose)

- void Clear()
  - 丢掉所有标签页（选中项回到第一页）。声明式界面在语言切换后
    重建标签文本时用它，免得宿主自己再持有一份影子列表。

- int Count()

- int Active()

- string ActiveLabel()

- void SetLabel(int i, string label)
  - 改写某个标签的标题（浏览器拿网页标题回填标签时用它，
    而不必把整条标签文本重新拼一遍）。

- string LabelAt(int i)

- void Select(int i)

- int ClosedIndex()

- int TakeChanged()

- int TakeClosed()
  - 领走一个已关闭的标签索引（没有则 -1）。一帧里关掉多个标签时
    逐帧逐个领，顺序与移除顺序一致（从大到小）。

- int TakeAdded()

- void AddNew()

- void EmitClosed(int i)
  - 上报一个被移除的标签：入队给按帧轮询 TakeClosed() 的宿主，
    同时抛事件给订阅 TabClosed 的宿主。

- void ReindexActive(int i)
  - 移除第 i 个标签之后把选中项挪回有效位置。

- bool Closable(int i)
  - 第 i 个标签是否允许被关闭。固定标签（closable == false）既不画关闭
    按钮，也不能被右键菜单或 CloseAt() 关掉：宿主把它当常驻页，
    关掉它宿主的并行数组就和标签错位了。

- int ClosableCount(int keep)
  - 除第 keep 个之外还有几个可关闭的标签（keep < 0 表示不留）。

- void CloseAt(int i)

- void CloseAll()
  - 关闭全部标签（「关闭全部」）。固定标签（closable == false）是宿主的
    常驻页，批量关闭不能连它一起端走。

- void CloseOthers(int keep)
  - 关闭除第 keep 个之外的标签（「关闭其他」）；keep 保留并成为选中项。

- void CloseBulk(int keep)
  - 批量关闭：keep < 0 关掉全部可关闭的标签，否则额外保留第 keep 个。
    先把要关的索引从大到小定下来再逐个关，这样上报出去的每个索引
    在宿主处理到它之前都还指向自己那一项，宿主的并行数组不会错位。

- string TabClasses()
  - 此变体标签页/标签条匹配的 CSS 类，皮肤可通过
    `tab.line`、`tab.card`、`tab.segment` 规则设置三种外观。

- static int StateOf(bool hover, bool selected)
  - 单个标签行的样式状态位。

- int HeaderHeight(App app)
  - 水平条的表头高度（也是侧边轨道中的行高）。

- int TabWidth(App app, int i)

- int ContentWidth(App app)

- void Render(App app, int x, int y, int totalWidth)

- void PumpClosed(App app)
  - 队列里还有没被领走的关闭索引：每帧只轮询一次的宿主靠后续帧把
    剩下的领完，而空闲时框框并不自己重绘，所以这里把帧要出来。

- void RenderTabStrip(App app, int x, int y, int viewW, int tabH, int count, string tcls, bool leftUp, bool rightUp)
  - 滚动标签条（裁剪）：每个标签一个样式框 + 标签/关闭字形，支持逐标签点击/右键菜单。

- void RenderAddButton(App app, int x, int y, int totalWidth, int addW, int tabH, bool leftUp)
  - 右边缘固定的尾部「+」添加按钮。

- void RenderOverflowChevrons(App app, int x, int y, int viewW, int chevW, int tabH, int maxScroll, bool leftUp)
  - 标签条宽于视口时显示的溢出箭头。

- void RenderVertical(App app, int x, int y, int w, int h)
  - 垂直轨道：一列全宽标签行，活动项在
    左侧高亮。调用方把页面绘制在轨道右侧。

- void ApplyMenuAction()
  - 应用前一帧从上下文菜单中选择的动作。
    overlay 在延迟 overlay 阶段（Render 之后）将所选索引写入 menuAction，
    因此在这里于下一帧消费。

- void EmitMenu(App app)
  - 将标签上下文菜单入队到框架 overlay 层，使其始终
    绘制在后续内容之上，并在外部点击时关闭。菜单在
    右键抬起时打开，使触发事件是抬起（kind 3），而不是
    overlay 视为外部点击关闭的按下（kind 2）。使用
    共享的富菜单组件（图标、组标题、分隔线和
    子菜单）。


## Tag (class)

标签 / 徽章。与 Button 形状相同：简单赋值，所有视觉都是
类，框及其内容由共享层绘制。

Tag t = new Tag { Text = "必读", Class = "错误 small" };
t.Closable = true;
t.Close += () => { tags.RemoveAt(i); };

可识别的类只是皮肤定义的选择器：

tag                                        默认
.primary .info .success .warning .error     语义类型
.tiny .small .medium .large                 尺寸（与按钮共用）
.round                                     形状

- Binding<string> Text;

- string Icon;

- bool Closable;
  - 添加尾部关闭字形，点击时触发 Close。

- int wid;

- UiEvent Close;
  - 点击关闭字形时触发；像 Button.Click 一样在 UI 线程排队，
    处理器可从中移除模型中的该标签。

- void InitTag(string label)

- Tag()

- Tag(string label)

- string Label()

- StyleBox ResolvedStyle(App app)

- int AutoWidth(App app)

- int Render(App app, int x, int y)

- int RenderW(App app, int x, int y, int w)
  - 以给定宽度绘制；比自然宽度窄时由共享盒子层省略
    标签文本。

- static int Width(App app, string text, string cls)
  - `text` 在 `cls` 样式下占据的宽度，使一行标签的布局
    与 Render 绘制的间距完全一致。

- static int Height(App app, string cls)
  - `cls` 样式下标签的高度，使自行布局行的绘制者
    能按 Chip 实际绘制的高度预留空间，而不是靠猜（行
    比标签矮正是名称条与下方线条重叠的原因
    ）。

- static void Chip(App app, int x, int y, string text, string cls)
  - 供自行管理布局的绘制者使用的立即模式标签（列表行、
    设计器浮层）：同样的框和内容，无需实例。

- static int ChipClosable(App app, int x, int y, string text, string cls)
  - 立即模式可关闭标签；返回关闭字形的命中 id。

- static int ClosableWidth(App app, string text, string cls)
  - 可关闭形式占据的宽度，使一行标签精确布局。

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)
  - 将关闭字形的 Close 路由到其 UiEvent 字段；其余事件
    全部落入 `On` 的公共绑定。

- override void OnMeasure(App app)

- override void OnPaint(App app)


## TextArea (class)

多行文本输入（textarea）。

保留式组件（创建一次，跨帧保留），绑定到
SignalString。它负责光标移动、换行编辑、自身的视口
裁剪和垂直滚动（鼠标滚轮 + 自动隐藏滚动条，
输入时保持光标可见）——调用点只需：

TextArea notes = new TextArea("Write something...");   // 一次
notes.Change += () => { ... };                            // C# 风格
notes.Render(app, x, y, w, h);                            // 每帧
string value = notes.GetText();

它拥有稳定的身份（焦点在兄弟/弹层变动后依然保留），框架
解析其焦点/点击，并上报光标位置，使 IME 组合
窗口跟随光标。只读代码展示 / 语法功能请使用
CodeEditor；这是普通可编辑的多行字段。

它同时是保留式 Control，设计的表单可将其停靠进树中：

TextArea body = new TextArea("Write something...");
body.Dock(Dock.Top()).Prefer(0, 90);
root.Add(body);

- SignalString model;

- Binding<string> data;
  - 双向绑定文本：`notes.data = doc.body;` 保持模型字段与
    文本框双向同步（编译器降级的 Binding）。

- string hint;

- int cursorPos;

- int selAnchor;
  - 选区的固定端（字节偏移），-1 表示无选区。光标是活动端，
    因此 Shift+方向键、拖动和 Ctrl+A 都只移动 `cursorPos`。

- int scrollY;

- int wid;

- bool skipKeys;
  - 由本帧拥有键盘的属主设置一帧——
    悬浮在框上的自动补全列表需要 Up/Down/Enter 来移动和
    接受选择，而不是移动光标、破坏换行。每次
    Render 都会清除，因此漏帧不会让框失聪。

- string cacheTxt;
  - `cacheTxt` 按行拆分的结果。拆分会为每行分配一个列表和一个
    字符串，而框在每帧以及每次
    方向键时都需要这些行——每帧都做正是让长提示文本
    输入卡顿的原因。缓存以文本为键，任何编辑（按键、绑定
    或 SetText）都恰好重建一次。

- List<string> cacheLines;

- List<int> rowStart;
  - 软换行后的显示行：每一项是一段文本的字节区间（起点 + 长度）。一行
    写不下就拆成几行显示，而文本本身不动（只有 Enter 才真的插入
    换行）：否则一句长句子会一直往右跑到框外面，又没有横向滚动，
    后半句就看不见了。按它的宽度与字号缓存，因为量文本宽度不便宜。

- List<int> rowLen;

- string wrapTxt;

- int wrapW;

- int wrapFs;

- bool readOnly;
  - 只读展示：文字仍可点选、拖选、Ctrl+A / Ctrl+C，但任何按键都改不了
    内容，也不画输入框的外框——用来显示一段可复制的正文。

- bool autoHeight;
  - 高度跟着内容走（放进滚动列里的正文用），而不是固定一格。

- UiEvent Change;
  - 文本变化时触发（C# 风格：`area.Change += h;`）。

- void InitTextArea(SignalString sig, string hintText)
  - 立即模式工厂与保留式构造器
    共用的初始化：设置 Control 基座（默认停靠顶部，追加的区域
    自动吸附），再设置文本框专属状态。

- TextArea():this("")
  - Default design-time constructor; the form supplies `placeholder` later.

- TextArea(string hintText)
  - 保留式构造器：`TextArea notes = new TextArea("Notes...");`

- static TextArea Bind(SignalString sig, string hintText)

- string GetText()

- TextArea ReadOnly(bool on)
  - 只读：可选中复制，不可编辑。

- bool IsReadOnly()

- TextArea AutoHeight(bool on)
  - 高度按行数自动撑开（外层负责滚动）。

- static int LineHeight(App app)
  - 一行的高度，正文换行的调用方按它算可见行数。

- int ContentHeight(App app)
  - 放下全部文本需要的高度（含内边距）。量的是显示行：一句被折成
    三行的长句子占三行的高度，否则自动高度的正文会把自己剪掉。还没
    画过（不知道宽度）时退回按逻辑行算。

- int Id()
  - 此框的焦点 id，属主可在渲染前查询它是否获得焦点
    （其上的弹层必须决定谁拥有按键）。

- void SkipKeysOnce()
  - 把本帧的按键事件交给调用方而不是文本框。

- void SetText(string v)

- void SyncBinding()
  - 将绑定的模型值拉入编辑缓冲区（model -> UI）。

- void PushBinding()
  - 通过绑定把编辑缓冲区写回（UI -> model）。

- void Edited()
  - 所有 UI 编辑都汇入这里：先写回，再通知。

- void ClampCursor()

- bool HasSel()

- int SelStart()

- int SelEnd()

- string SelText()

- void DeleteSel()
  - 删除选中的范围，并把光标收拢到其起点。

- List<string> Lines()
  - 文本按行返回，每次编辑最多拆分一次。

- void Wrap(int maxW, int fs)
  - 重算软换行（文本 / 宽度 / 字号变了才算）。

- void WrapLine(string ln, int abs, int maxW, int fs)
  - 一段逻辑行拆成几行显示。宽度还没算出来（maxW <= 0，第一帧）就不拆。

- int RowOfPos(int pos)
  - 字节位置落在哪一显示行。刚好落在软换行的接缝上时算下一行的行首：
    光标跟着刚敲进去的字走，而那个字已经在下一行了。

- string RowText(int i)
  - 第 `i` 行显示出来的那段文本。

- void EnsureRows()
  - 确保显示行算过一次（第一帧还没画过时宽度未知，那就不折行）。

- void HandleInput(App app)

- int PosFromMouse(App app, int innerX, int innerY, int lineH, int fs)
  - 鸠标位置对应的字节偏移（行高/滚动位置均取当前帧）。按显示行算：
    点在折下来那半句上，光标就落在那半句里。

- int Render(App app, int x, int y, int w, int h)

- override string Kind()

- override string StyleType()

- override List<PropSpec> Props()

- override string GetExtra(string key)
  - 文本经由 SetText 写入，使光标保持在范围内。

- override bool SetExtra(string key, string val)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void OnMeasure(App app)

- override void OnPaint(App app)


## TextRun (class)

一条预换行的展示行：文本、颜色、显示类型（0 普通 / 1 代码 / 2 标题 /
3 轮次头行 / 4 思考折叠 / 5 可折叠摘要 / 6 轮次间隔），kind-5 行
切换的消息索引（否则 -1），以及该行所属发言者（0 我 / 1 小赞 /
2 工具 / 3 错误，用于整块着色）。
把旧的并行 lines/colors/kinds/meta/owner 列表合并成一个实体，
避免任何编辑都可能让索引对齐静默错位。

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

垂直时间线：每个条目一个圆点和连接线，旁边是时间 / 标题 /
描述块。圆点、连接线和三行文本都是
样式部件（`timeline::dot`、`::line`、`::time`、`::title`、`::desc`），
皮肤可重设样式，代码侧只需简单赋值：

Timeline tl = new Timeline { Class = "primary" };
tl.AddItem("09:00", "Created", "Project initialized");
tl.Add(new TimelineItem("12:30", "Published", "", "success", ""));

- List<TimelineItem> items;

- void InitTimeline()

- Timeline()

- void Add(TimelineItem it)

- static Timeline Of(List<TimelineItem> its)
  - 基于已构建的条目列表创建时间线。

- void AddItem(string time, string title, string desc)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int RowHeight(App app)

- override string Kind()

- override List<PropSpec> Props()


## TimelineItem (class)

时间线条目：时间/元信息标签、标题、描述以及
圆点携带的类（`success`、`error`、…——共享语义角色）。

- string time;

- string title;

- string description;

- string cls;

- TimelineItem(string time, string title, string description):this(time, title, description, "")
  - 不带语义类的条目：圆点用默认样式。

- TimelineItem(string time, string title, string description, string cls)


## ToastItem (class)

一个短暂的队列项（toast / notification）。队列掌管其诞生
时刻，以便淡入、存活 `lifeMs` 后淡出；`curY`/`curX`
携带缓动后的屏幕位置，使更早的项被移除时
堆栈能平滑收拢。

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

窗口工具条：一行停靠的图标 / 文字按钮，位于带底部细
分隔线的主题表面上——菜单栏下面那条工具栏，以及上位机
画面顶部的操作条。

各项是 Button，因此主题、样式表和事件与其它控件一致；
工具条只负责表面、内边距和左右分组：

ToolStrip bar = new ToolStrip();
int save = bar.Add("Save", "save");
bar.AddSeparator();
int help = bar.AddRight("Help", "info");
bar.ItemAt(save).Click += () => { ... };

序列化 / 设计器把每项写成 `options` 里的一条文本：
`"标题:图标"`，前缀 `>` 表示靠右，`-` 表示分隔条。

- List<Button> items;

- List<ToolItemCallback> itemCbs;
  - 带索引的项点击回调（OnItemClick）。

- UiEvent ItemClick;
  - 不关心是哪一项的项点击处理（设计器 ItemClick 事件）。

- int tintBg;

- bool iconOnly;
  - 只显示图标（不画标题），紧凑的图标工具条。

- string itemsSpec;
  - `options` 的原样文本，供检查器 / 序列化按原样回读。

- ToolStrip()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- void OnItemClick(ToolItemCallback cb)
  - 任何一项被点击时回调，参数为项的索引。

- void FireItem(int index)
  - 向已注册的处理器发布一次项点击。

- override string Kind()

- override List<PropSpec> Props()

- int Count()

- Button ItemAt(int i)

- Button NewItem(string text, string icon)

- int Add(string text, string icon)
  - 追加一个左对齐项；返回其索引供后续访问。

- int Add(string text)

- void Clear()
  - 丢掉所有项。声明式界面在按钮集合随状态变化（如调试器
    的启动 / 暂停两套按钮）时重建工具条，用它代替宿主自己
    持有一份影子列表。

- int AddRight(string text, string icon)
  - 追加一个右对齐项（右侧分组按添加顺序从右往左排）。

- int AddRight(string text)

- int AddSeparator()
  - 竖直分隔条：一个禁用的窄项，用来分组按钮。

- void SetItemsText(string spec)
  - 按序列化文本重建全部项：每条 `"标题:图标"`，
    前缀 `>` 靠右，`-` 为分隔条。设计器与 .zform 走这条路径。

- void SetIconOnly(bool on)

- override string GetExtra(string key)
  - 项集合是一个列表，不是字段，因此走 extra 属性：
    设计器 / .zform 的 `options` 由此真正建出按钮。

- override bool SetExtra(string key, string val)

- void SetTint(int bg)
  - 纯色背景着色，替换默认表面（0 恢复默认）。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void RenderAt(App app, int x, int y, int w, int h)
  - 渲染到显式矩形（供自管布局的宿主使用）；作为
    控件树里的停靠子控件时无需此方法。


## Tooltip (class)

锚定到某一点的提示气泡，带小三角。支持四种
位置（上 / 下 / 左 / 右），长文本自动换行，
并且有高度上限，换行内容
溢出时显示滚动条指示。

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
  - 贪婪地将 `text` 按词换行，使每行不超过 `maxW` 像素。
    没有空格的长单词按字符硬拆。

- static int TipCapH(App app)

- static Rect BoxRect(App app, int anchorX, int anchorY, string text, int placement, int maxW)
  - 为给定的锚点/位置/宽度解析屏幕上的气泡矩形（原点 + 受限尺寸），
    采用与绘制时相同的边缘翻转 + 限制。
    调用方用它对滚轮输入做命中测试。

- static int MaxScroll(App app, string text, int maxW)
  - 提示的最大滚动偏移（px）：内容适配高度上限时为 0。

- static void RenderScroll(App app, int anchorX, int anchorY, string text, int placement, int maxW, int scrollOff)
  - 富锚定提示：不透明深色气泡，正文自动换行，
    四种位置（即将离开屏幕时自动翻转），
    换行文本高于高度上限时显示滚动条。
    `anchorX`/`anchorY` 是三角应触及的点。`scrollOff` 是
    垂直滚动量（限制在 [0, MaxScroll]）。

- static void RenderAt(App app, int anchorX, int anchorY, string text, int placement, int maxW)
  - 无滚动的锚定提示（偏移 0）。为现有调用方保留。

- static void RenderBox(App app, int x, int y, int w, int h, string text, int placement, int maxW)
  - 锚定到控件矩形的提示：调用方传入
    组件边界，位置决定贴哪条边——Top 位于控件
    上方（三角在顶缘）、Bottom 在下方、Left/Right 在两侧。
    调用点无需手动计算锚点。

- static void RequestBox(App app, int x, int y, int w, int h, string text, int placement, int maxW)
  - RenderBox 的延迟（帧末统一刷新）变体：为控件的矩形请求提示，
    位置决定贴哪条边。

- static void RenderRich(App app, int x, int y, string title, string desc)
  - 不透明两行提示：粗体标题在上，弱化描述在下。

- static void Render(App app, int anchorX, int anchorY, string text)
  - 锚点上方简单的单行提示（空间不足时翻到下方）。为现有
    调用方保留；委托给感知位置的渲染器。

- static string pendingText;

- static int pendingX;

- static int pendingY;

- static int pendingPlace;

- static int pendingMaxW;

- static void Request(int anchorX, int anchorY, string text, int placement, int maxW)
  - 入队控件本帧希望显示的悬停提示，锚定在
    三角应触及的点上。

- static void Flush(App app)
  - 绘制本帧请求的提示（若有）并清空队列。


## TreeNode (class)

树的一个节点，按先序排列。`depth` 是缩进级别（根 = 0）。
目录可展开/折叠；文件是叶节点。`icon` 非空时覆盖
默认文件夹/文件字形。`path` 可选地携带该节点表示的
真实文件系统路径（纯内存树为空）。
`checked` 支撑可选的选择复选框列。

- string label;

- int depth;

- bool isDir;

- bool expanded;

- string icon;

- string path;

- string payload;
  - 调用方可选的 payload（例如索引、id），
    节点本身不解释它。

- bool checked;

- static TreeNode Dir(string label, int depth, bool expanded)

- static TreeNode File(string label, int depth)

- static TreeNode FileIcon(string label, int depth, string icon)

- TreeNode WithPayload(string p)


## TreeView (class)

绑定到调用方自有 `List<TreeNode>` 的可折叠树。与所有
保留式控件一样，它位于控件树中，由 CSS 决定其盒子，调用点
无需计算矩形、跟踪滚动偏移或轮询返回码：

TreeView files = new TreeView();
files.Bind(model.nodes);          // 调用方的列表，不复制
files.OnSelect(OpenSelected);
files.OnContext(ShowMenu);        // 读取 ContextRow / ContextX / ContextY
side.Add(files);

节点可以是自己的组件，而不是内置行：

TreeView tv = new TreeView(n => NodeCard(n));

点击目录会切换展开并隐藏其子树；滚轮滚动、
overlay 滚动条、可选的内联过滤行和可选的复选框
列都在控件内部处理。处理器被编组到
UI 线程（UiEvent.Post），后台任务重建绑定列表
并调用 Refresh() 是安全的。

- List<TreeNode> data;
  - 调用方的节点，每帧读取：修改列表会在
    下一次绘制时体现（自定义节点控件需要重建时调用 Refresh()）。

- SignalInt sel;

- SignalInt scroll;

- Input filter;

- bool checks;

- bool multi;
  - 多选模式：Ctrl 点击切换行的选中状态，
    Shift 点击从焦点行扩展到该行。`marks` 保存
    `sel`（焦点行）之外的额外选中行。

- List<int> marks;

- NodeOf tpl;

- bool rowsStale;

- int builtFor;

- int ctxRow;
  - 处理器运行期间设置：事件涉及的节点，以及
    指针位置，使上下文菜单无需调用点
    读取鼠标状态即可自行锚定。

- int ctxX;

- int ctxY;

- string Empty;
  - 树没有节点时居中显示（"" 不绘制任何内容）。

- UiEvent Select;
  - 节点被选中时（读取 SelectedIndex / Selected）。

- UiEvent Activate;
  - 节点被双击时（打开 / 运行它）。

- UiEvent Expand;
  - 目录被展开时。

- UiEvent Collapse;
  - 目录被折叠时。

- UiEvent Context;
  - 节点被右键点击时（读取 ContextRow / ContextX / ContextY）。

- UiEvent Check;
  - 复选框被切换时（用 ContextRow 读取节点）。

- UiEvent Press;
  - 行被左键按下时（读取 ContextRow / ContextX / ContextY）。
    供把行当拖拽源的宿主使用，例如设计器工具箱。

- UiEvent Move;
  - 拖动行改变次序后（读取 MoveFrom / MoveTo）。

- bool reorder;
  - 拖动排序（WithReorder）的状态：按下的行、是否已经拖起来、
    松手时的插入位置，以及本帧是否要吃掉点击（拖完不应该
    顺带选中或展开落点那一行）。

- bool pressed;

- int dragRow;

- bool dragging;

- int dropAt;

- bool dropDone;

- void InitTree()

- TreeView()

- TreeView(NodeOf template)
  - 自定义行：模板为每个节点构建控件子树。

- override string Kind()

- override List<PropSpec> Props()

- override string StyleType()

- TreeView Bind(List<TreeNode> nodes)
  - 绑定调用方的节点列表；不复制任何内容。

- void Refresh()
  - 标记绑定的节点为已变化，使自定义行在下一帧重建。

- TreeView BindSel(SignalInt s)
  - 共享调用方的选择信号，供在节点列表重建时
    持久化选择的宿主使用。

- TreeView WithFilter(Input f)
  - 在树顶部绘制内联过滤行，本身不进行过滤：
    宿主在输入变化时重新绑定过滤后的列表。

- TreeView WithChecks()
  - 在每个节点前绘制三态复选框。

- TreeView WithMultiSelect()
  - 启用 Ctrl 点击（切换）和 Shift 点击（范围）多选。

- TreeView WithReorder()
  - 启用拖动排序：按住一行拖动时画出插入位置，松手后触发
    Move。树只报告「哪一行拖到了哪个插入点」，真正的次序由
    宿主在自己的模型上完成（树的节点列表下一帧照常重建）。

- int MoveFrom()
  - 被拖动的行。

- int MoveTo()
  - 插入位置：行要放到这个索引之前；等于 Count() 表示放到末尾。

- int Count()

- void ScrollToEnd()
  - 滚到底部；下一帧绘制时会被钳到实际最大偏移。

- List<TreeNode> Nodes()
  - 绑定列表本身，供自行维护节点视图的宿主
    在处理器被触发时使用。

- bool Has(int row)

- TreeNode NodeAt(int row)

- int SelectedIndex()

- bool HasSelection()

- TreeNode Selected()
  - 选中的节点；仅在 HasSelection() 时有效。

- void SelectIndex(int row)

- void ClearSelection()

- bool IsMarked(int row)
  - `row` 属于选择集（含焦点行）时返回 true。

- List<int> SelectedIndices()
  - 所有选中行按升序排列（含焦点行）。

- int SelectionCount()

- int ContextRow()
  - 正在运行的处理器所涉及的节点（被右键点击或切换复选框的）。

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
  - 树为空时显示的文本。

- void ExpandAll()

- void CollapseAll()

- void SetAllChecked(bool on)

- void Toggle(int row)

- List<int> CheckedIndices()
  - 所有已勾选节点的索引（含目录）。

- List<string> CheckedLabels()
  - 所有已勾选叶节点的标签。

- int CheckedCount()

- bool Templated()

- int RowHeight(App app)

- int FilterHeight(App app)

- bool Shown(int row)
  - 没有折叠的祖先隐藏 `row` 时返回 true。

- int ContentHeight(App app)
  - 可见行的像素高度；面板未声明高度时，列布局
    据此确定树的高度。

- int RowExtent(App app, Control row)

- void BuildRows(App app)
  - 通过模板为每个节点重建一个子控件。在此处
    测量子控件，因为树的测量阶段在发现需要重建时
    已经走过了它们。

- Rect PaintFilter(App app)
  - 过滤行，绘制在树自身的背景上，使其看起来是树的一部分。
    返回其下方的行区域。

- int RowInteract(App app, int index, Rect row)
  - 一行的选择 / 展开 / 上下文交互。返回注册的 id，
    以便据此绘制悬停状态。

- void NoteSelfDamage(App app)
  - 选择、展开、折叠只改变树自己的那块像素，因此把损伤
    限定在控件矩形内：整窗重绘会把功能区、编辑器、预览
    一起重画，展开一个目录的 CPU 峰值就是这么来的。需要
    更大范围的宿主在它们的 Select/Expand 处理器里自行
    调用 RequestRedraw（处理器在下一帧之前执行）。

- bool PaintCheck(App app, int row, int cx, int rowY, int rowH, int box)
  - 行前的复选框：绘制三态框，点击时切换节点
    （目录则连同其后代）。返回 true
    表示已消费该点击，行不会同时被选中。

- void PaintRow(App app, int row, Rect box, int checkCol, bool active)
  - 内置行：箭头、图标和标签，按深度缩进。

- int ScrollOffset(App app, Rect rows, int contentH)
  - 滚轮 + `contentH` 在行区域内限制的滚动偏移。

- void UpdateDrag(App app, Rect rows)
  - 拖动排序的每帧状态机：移动时更新插入位置，松手时把
    结果交给宿主。返回本帧是否刚刚完成一次拖放。

- void PaintDropLine(App app, Rect rows, int y)
  - 插入位置的横线（拖动排序时画在两行之间）。

- void PaintRows(App app, Rect rows)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void PaintEmpty(App app, Rect rows)

- override List<string> Events()

- override void BindEvent(string evt, Action a)
  - 将树的语义事件（Select/Activate/Expand/Collapse/
    Context/Check）路由到对应 UiEvent 字段；其余事件落入
    `On` 的公共绑定。没有这个，基类会把 `onSelect`
    JSON/设计器处理器映射到 `On`，而 AddByName 忽略未知名称，
    处理器会静默地从未执行。

- static int SubtreeEnd(List<TreeNode> nodes, int idx)
  - `idx` 的子树之后第一个节点的索引。

- static int CheckState(List<TreeNode> nodes, int idx)
  - 节点复选框的三态：0 = 未勾选，1 = 已勾选，2 = 部分
    勾选（后代混合的目录）。

- static void ToggleCheck(List<TreeNode> nodes, int idx)
  - 切换节点。叶节点翻转自身状态；目录将其所有后代
    （以及自身）设为当前聚合状态的反面。


## Typography (class)

标题和正文。与 Label 形状相同——简单赋值，
字号和颜色都是皮肤无需一行代码即可重设的类：

Typography h = new Typography { Text = "Components", Class = "h2" };
Typography p = new Typography { Text = vm.summary, Class = "paragraph" };

类：`h1` … `h6` 是标题级别，`paragraph` 是正文，
`secondary` / `hint` / `inverse` 以及语义角色（`primary` … `error`）
决定颜色。文本由 Label 绘制，因此 `label` 规则同样作用于
这些控件。

- Binding<string> Text;

- void InitTypography(string txt)

- Typography()

- Typography(string txt)

- string Str()
  - 当前文本（通过绑定解析）。

- static string HeadingClass(int level)
  - 标题级别（1..6）对应的类。

- static Typography H(string txt, int level)
  - `Typography.H("Title", 2)` —— 该级别的标题。

- static Typography P(string txt)
  - `Typography.P("Body copy")` —— 一个段落。

- override string StyleType()
  - 按 label 设置样式，因此 `label.h2` 和 `label.paragraph` 是选择器，
    皮肤的文本规则同时作用于标题和正文。

- StyleBox ResolvedStyle(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- static int Heading(App app, int x, int y, string text, int level)
  - 立即模式标题；返回其占用的行高。

- static int Paragraph(App app, int x, int y, string text)
  - 立即模式段落；返回其占用的行高。

- override string Kind()

- override List<PropSpec> Props()


## VirtualList (class)

高层级虚拟化列表。

保留式（创建一次，跨帧保留）。在任意大的条目列表上渲染一个
固定高度、带边框的视口，只绘制
当前可见的行（行虚拟化），10 万行的开销与
一屏相同。它拥有像素滚动、滚动条以及悬停/选择，
内部组合了一个 ScrollView——调用点只需：

VirtualList list = new VirtualList();     // 一次
list.SetItems(rows);

list.Render(app, x, y, w, h);                // 每帧
int sel = list.Selected();                   // -1 表示无
int hit = list.TakeActivated();              // 本帧点击的行，-1

与 ListView（每行全高布局、无视口）不同，
本组件成本恒定且自行滚动。

- ScrollView scroll;

- List<string> items;

- int selected;

- int rowH;

- int activated;

- VirtualList()

- override string Kind()

- override List<PropSpec> Props()

- override void OnMeasure(App app)

- override void OnPaint(App app)

- void SetItems(List<string> rows)

- void Add(string row)

- void SetRowHeight(int h)

- int Count()

- int Selected()

- string SelectedText()

- int TakeActivated()
  - 本帧点击的行（或 -1），随后清除。

- int ResolveRowH(App app)

- void Render(App app, int x, int y, int w, int h)

- void HitSelect(App app, Rect area, int rh, int dy)


## Wizard (class)

渲染浮动 "wizard" 子窗口的完整内容：标题栏
（带关闭按钮）、左侧可选择的模板列表、右侧的名称（和
可选位置）输入框、所选模板的实时描述，
以及 Create / Cancel 按钮。

与 Modal/Dialog（在主窗口内绘制遮罩）不同，Wizard
旨在填充一个真实的二级顶层窗口，用户可将其拖到
桌面任意位置——调用方拥有该窗口的 App 和事件循环，
每帧只需调用 Wizard.Render(childApp, ...)。

返回 0 无操作、1 创建、2 取消/关闭。

- static int lang=1;
  - UI language 用于 wizard's built-in labels (0 = English, 1 = 简体中文).
    宿主 IDE 在渲染前设置此项，使标签跟随应用语言。

- static string notice="";
  - 可选的一行提示，以主题错误色绘制在
    Create/Cancel 按钮旁边（如 "目标文件夹不为空"）。宿主
    拒绝 Create 时设置它，对话框关闭时清除。

- static SignalInt listScroll;
  - 三个列表视口各自的滚动位置（条目列表、分类列、模板列）。
    行数超出可见高度时列表自己滚动，宿主不必再包一层滚动容器。

- static SignalInt catScroll;

- static SignalInt tplScroll;

- static SignalInt Scroller(SignalInt s)

- static int ScrollOffset(App app, int x, int y, int w, int h, int contentH, SignalInt off)
  - 行列表视口的滚动量：处理滚轮、把偏移夹在
    [0, contentH - h]，返回本帧应使用的偏移。

- static string TW(string en, string zh)

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
  - 为生成设计窗口的文件模板提供的可选窗口尺寸预设选择
    （参见 SizePresetW/H/Name）。null 隐藏该区域。

- Input iSizeW;
  - 可选的宽/高自定义输入，绘制在预设网格下方
    （null 隐藏自定义行）。点击预设会回填这些输入，宿主
    从这些输入读取最终尺寸。

- Input iSizeH;

- Wizard()

- override string Kind()

- override List<PropSpec> Props()

- Wizard(string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc)
  - 单列表向导：`new Wizard(title, items, sel, name, loc, showLoc)`。

- Wizard(string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc, SignalInt sizeSel)
  - 带可选尺寸预设行的单列表向导（`sizeSel` 选择
    Phone/Tablet/Desktop/Default；传 null 则不显示尺寸行）。

- Wizard(string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc, SignalInt sizeSel, Input sizeW, Input sizeH)
  - 带完整尺寸预设网格和自定义
    宽/高输入的单列表向导（传 null 输入可隐藏自定义行）。

- static List<int> SizePresetW()
  - 共享的设计尺寸预设，与自由画布设计器的
    下拉一致：LVGL 显示尺寸 + 手机/平板/桌面 + 手表/手环。

- static List<int> SizePresetH()

- static string SizePresetName(int i)

- static bool SketchHas(string sketch, string token)
  - `sketch` 记号列表里是否含有 `token`。

- static void RenderPreview(App app, int x, int y, int w, int h, WizardTemplate tpl, int devW, int devH)
  - 右侧预览面板：所选模板在目标尺寸下的样子。有真实截图
    （模板目录里的 `preview` 图片）就按比例贴图，否则按模板
    声明的 `sketch` 骨架画线框——两者都在正确宽高比的设备
    外框里，因此"创建出来是什么样"在创建之前就能看到。

- static void RenderSketch(App app, int x, int y, int w, int h, WizardTemplate tpl)
  - 在 (x,y,w,h) 内按 `tpl.sketch` 的记号画出模板骨架的线框：
    每个记号占据它在真实窗口里的位置（标题栏在上、状态栏在
    下、侧栏在左），内容记号填充剩下的区域。

- static int ParseDim(string s)
  - 从自定义宽/高输入里解析前导数字（输入是自由文本，
    单位或空格都无所谓），无数字时返回 0。

- static Wizard Categorized(string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames)
  - 分类项目向导（分类 + 模板列 + 目标标签）。

- static Wizard Categorized(string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames, SignalInt sizeSel, Input sizeW, Input sizeH)
  - 带设备尺寸选择的分类项目向导：选中的模板声明
    `sizeable` 时，描述下方出现尺寸预设行与自定义宽高，
    右侧预览按所选尺寸的比例绘制。

- int Show(App app)
  - 本帧将向导绘制到其窗口中，并返回动作码
    （0 无、1 创建、2 取消/关闭、3 浏览）。所有状态都保存在
    实例上；宿主对返回码作出响应。

- static int Render(App app, string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc, SignalInt sizeSel)

- static int Render(App app, string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc, SignalInt sizeSel, Input sizeW, Input sizeH)

- static void RenderList(App app, int x, int y, int w, int h, List<WizardItem> items, SignalInt sel)
  - 可选择的模板行垂直列表（图标 + 标签），绑定到
    SignalInt 选择。任何选择器风格的窗口都可复用。

- static int RenderCategorized(App app, string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames)
  - 两级项目选择器：左侧分类列表，所选分类中的模板
    居中，名称/位置输入框和实时
    描述在右侧。模板以单一实体列表提供
    （每个携带其分类 / 名称 / 图标 / 描述）；tplSel 保存
    所选模板的全局索引。返回 0 无、1 创建、2 取消。

- static int RenderCategorized(App app, string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames, SignalInt sizeSel, Input sizeW, Input sizeH)


## WizardItem (class)

Wizard 列表中一个可选择的条目：图标、标签和一行
描述（当前选中时显示）。取代旧的
name/icon/description 并行列表约定，改为单一实体。

- string name;

- string icon;

- string desc;

- WizardItem(string n, string ic, string d)


## WizardTarget (class)

多选目标标签：标签及其开/关状态（1 = 已选）。

- string name;

- int selected;

- WizardTarget(string n, int s)


## WizardTemplate (class)

分类项目向导的模板条目：一个 WizardItem 加上其所属的
分类列，以及向导右侧预览所需的形态信息。

尺寸不再是分类里的独立模板：产出设计窗口的模板把
`sizeable` 置为 true，向导据此显示设备尺寸行，宿主用所选
尺寸创建项目——手机 / 平板 / 手表只是同一个模板的目标尺寸。

- string cat;

- string name;

- string icon;

- string desc;

- bool sizeable;
  - 该模板产出一个可按设备尺寸创建的设计窗口。

- int defW;
  - 默认设计尺寸（sizeable 时用于预览与创建）。

- int defH;

- bool round;
  - 圆形设计区（智能手表表盘）。

- string preview;
  - 真实截图的绝对路径，为空时按 `sketch` 画线框。

- string sketch;
  - 线框骨架：逗号分隔的记号，见 Wizard.RenderSketch
    （titlebar / toolbar / ribbon / tabs / sidebar / statusbar /
    form / chart / gauges / alarms / list / console / webview /
    server / library）。

- WizardTemplate(string c, string n, string ic, string d)

- WizardTemplate Shape(bool canSize, int w, int h, bool isRound)
  - 链式配置形态信息，让宿主只描述它从清单里读到的内容。

- WizardTemplate Look(string previewPath, string sketchSpec)
  - 预览来源：真实截图路径（可为空）与线框记号。


## Control (delegate)

为某个绑定实体构建一行的控件子树，适用于
行是自定义组件而非文本列的列表。

`delegate Control RowOf<T>(T item);`


## Control (delegate)

构建节点自身的控件子树，用于行是自定义
组件而非内置字形 + 标签行的树。

`delegate Control NodeOf(TreeNode node);`


## string (delegate)

从绑定实体中读取某一列的文本：`new ListColumn<File>("Name",`
f => f.name)`.

`delegate string CellOf<T>(T item);`


## void (delegate)

工具条项被点击时的回调，带项的索引。

`delegate void ToolItemCallback(int index);`
