# Gui.Widget

> 源码: `stdlib/Gui/Widget/Avatar.zan`, `stdlib/Gui/Widget/Badge.zan`, `stdlib/Gui/Widget/BoxContent.zan`, `stdlib/Gui/Widget/Breadcrumb.zan`, `stdlib/Gui/Widget/Button.zan`, `stdlib/Gui/Widget/ButtonGroup.zan`, `stdlib/Gui/Widget/Calendar.zan`, `stdlib/Gui/Widget/Card.zan`, `stdlib/Gui/Widget/Carousel.zan`, `stdlib/Gui/Widget/Checkbox.zan`, `stdlib/Gui/Widget/ChoiceGroup.zan`, `stdlib/Gui/Widget/CodeBlock.zan`, `stdlib/Gui/Widget/Collapse.zan`, `stdlib/Gui/Widget/ColorPicker.zan`, `stdlib/Gui/Widget/ContextMenu.zan`, `stdlib/Gui/Widget/Countdown.zan`, `stdlib/Gui/Widget/DatePicker.zan`, `stdlib/Gui/Widget/Divider.zan`, `stdlib/Gui/Widget/Dropdown.zan`, `stdlib/Gui/Widget/DynamicTags.zan`, `stdlib/Gui/Widget/Ellipsis.zan`, `stdlib/Gui/Widget/Empty.zan`, `stdlib/Gui/Widget/Flex.zan`, `stdlib/Gui/Widget/FloatButton.zan`, `stdlib/Gui/Widget/FormBuilder.zan`, `stdlib/Gui/Widget/FormField.zan`, `stdlib/Gui/Widget/FormGroup.zan`, `stdlib/Gui/Widget/Grid.zan`, `stdlib/Gui/Widget/GridItem.zan`, `stdlib/Gui/Widget/IconView.zan`, `stdlib/Gui/Widget/Image.zan`, `stdlib/Gui/Widget/Input.zan`, `stdlib/Gui/Widget/InputNumber.zan`, `stdlib/Gui/Widget/InputOtp.zan`, `stdlib/Gui/Widget/Label.zan`, `stdlib/Gui/Widget/Layer.zan`, `stdlib/Gui/Widget/ListItem.zan`, `stdlib/Gui/Widget/ListView.zan`, `stdlib/Gui/Widget/Marquee.zan`, `stdlib/Gui/Widget/Menu.zan`, `stdlib/Gui/Widget/NumberAnimation.zan`, `stdlib/Gui/Widget/PageHeader.zan`, `stdlib/Gui/Widget/Pagination.zan`, `stdlib/Gui/Widget/Panel.zan`, `stdlib/Gui/Widget/Popover.zan`, `stdlib/Gui/Widget/Progress.zan`, `stdlib/Gui/Widget/Prompt.zan`, `stdlib/Gui/Widget/QrCode.zan`, `stdlib/Gui/Widget/Radio.zan`, `stdlib/Gui/Widget/Rate.zan`, `stdlib/Gui/Widget/Result.zan`, `stdlib/Gui/Widget/Ribbon.zan`, `stdlib/Gui/Widget/ScrollColumn.zan`, `stdlib/Gui/Widget/ScrollView.zan`, `stdlib/Gui/Widget/Scrollbar.zan`, `stdlib/Gui/Widget/SelectBox.zan`, `stdlib/Gui/Widget/Skeleton.zan`, `stdlib/Gui/Widget/Slider.zan`, `stdlib/Gui/Widget/Spin.zan`, `stdlib/Gui/Widget/Split.zan`, `stdlib/Gui/Widget/SplitPanel.zan`, `stdlib/Gui/Widget/Statistic.zan`, `stdlib/Gui/Widget/StatusBar.zan`, `stdlib/Gui/Widget/Steps.zan`, `stdlib/Gui/Widget/StyledText.zan`, `stdlib/Gui/Widget/Switch.zan`, `stdlib/Gui/Widget/Table.zan`, `stdlib/Gui/Widget/Tabs.zan`, `stdlib/Gui/Widget/Tag.zan`, `stdlib/Gui/Widget/TextArea.zan`, `stdlib/Gui/Widget/Timeline.zan`, `stdlib/Gui/Widget/ToolStrip.zan`, `stdlib/Gui/Widget/Tooltip.zan`, `stdlib/Gui/Widget/Transfer.zan`, `stdlib/Gui/Widget/TreeView.zan`, `stdlib/Gui/Widget/Typography.zan`, `stdlib/Gui/Widget/Upload.zan`, `stdlib/Gui/Widget/VirtualList.zan`, `stdlib/Gui/Widget/Watermark.zan`, `stdlib/Gui/Widget/Wizard.zan`


## Avatar (class)

头像：圆形标记内的首字母或图标。填充、文字颜色和
半径来自 `avatar` CSS 规则；`Size` 是圆形的直径，单位为
CSS px（`width`/`height` 规则优先于它）。

Avatar a = new Avatar { Text = "JS", Size = 40 };
Avatar b = new Avatar { Icon = "user", Class = "primary" };
Avatar c = new Avatar { Text = "S", Size = 48, Shape = "square" };

- Binding<string> Text;
  - 圆内文本（首字母 / 缩写）；Icon 非空时不显示。

- string Icon;
  - 图标字形名；非空时取代文本。

- int Size;
  - 直径（CSS px）；height 样式规则优先。

- string Shape;
  - 形状：circle（默认）、rounded、square（Naive UI shape prop）。

- void InitAvatar(string label, int size, string icon)
  - 初始化：注册为 "avatar" 并自持样式表面，置文本/图标/直径与
    circle 形状。

- Avatar()
  - 默认 32px 空头像（设计器用）。

- Avatar(string label)
  - 给定首字母文本、32px。

- Avatar(string label, int size)
  - 给定文本与直径。

- Avatar(string label, int size, string icon)
  - 给定文本、直径与图标字形。

- string Label()
  - 当前文本；Text 为 null 时返回空串。

- int Diameter(App app)
  - 生效直径：`height` 样式规则优先，否则 Size 按 DPI 缩放。

- void OnMeasure(App app)
  - 测量：宽高都取直径（正方形占位）。

- void OnPaint(App app)
  - 绘制：按直径在盒内居中画头像表面（Shape 映射圆角）与
    图标/首字母文本。

- string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（text/icon/size/shape/class）。

- override string GetExtra(string key)
  - shape prop 是枚举（string），通过 GetExtra/SetExtra 与
    字符串字段 Shape 同步。PropSpec.Enum 驱动的是内部 num，
    这里由 SetExtra 接住字符串后回写到 Shape。

- override bool SetExtra(string key, string val)
  - 设计器写入 `shape`（枚举字符串回写到 Shape 字段）；
    其他键返回 false 交给基类。


## Badge (class)

徽标：圆点或计数胶囊。颜色、大小、半径和字体来自
`badge` CSS 规则，皮肤可重设样式，代码侧只需赋值：

Badge b = new Badge { Count = 5, Class = "error" };
b.Count = unread;         // 0 时绘制圆点形式

- int Count;
  - 计数值；≤ 0 画成圆点形式，> Max 画成 "<Max>+"。

- int Max;
  - 超过此值的计数绘制为“<max>+”。

- void InitBadge(int n)
  - 初始化：计数 n、上限 99、默认 error 色。

- Badge()
  - 计数 0（圆点形式，设计器用）。

- Badge(int n)
  - 计数 n。

- StyleBox ResolvedStyle(App app)
  - 解析 `badge` 样式盒（Class 与命名参与匹配）。

- string Label()
  - 计数绘制的标签（圆点形式为“”）。

- override void OnMeasure(App app)
  - 覆写：首选尺寸 = 样式高度 + 标签宽（圆点形式取高度）。

- override void OnPaint(App app)
  - 覆写：在盒内垂直居中地画计数胶囊或圆点。

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
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（计数 / 上限 / 类）。

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
  - 绘制盒子（表面 + 按下涟漪）与内容：busy 时画 spinner 取代前导
    图标，标签空间不足时省略，`.stacked` 类走图标在上布局。
    返回尾随字形左边缘 x（无尾随字形或 stacked 时为 0）。


## Breadcrumb (class)

面包屑导航：同级盒子上的面包屑项用分隔字形连接。
面包屑项、当前项和分隔符都是样式部件
（`breadcrumb::item`、`::current`、`::separator`），皮肤可重设，
代码侧只是简单赋值：

Breadcrumb b = new Breadcrumb();
b.AddItem("Home"); b.AddItem("Projects");
b.Separator = "slash";

- List<string> items;
  - 面包屑文本，按显示顺序；最后一项用 `::current` 样式绘制。

- string Separator;
  - 绘制在面包屑项之间的字形。

- void InitBreadcrumb()
  - 初始化：空清单，分隔符默认右箭头（chevron-right）。

- Breadcrumb()
  - 空面包屑（之后 AddItem/Of）。

- void AddItem(string label)
  - 命名为 AddItem（而非 Add），以免遮蔽 Control.Add(Control)。

- static Breadcrumb Of(List<string> labels)
  - 根据现有标签列表构建一个面包屑。

- override void OnMeasure(App app)
  - 覆写：行高 = 字体高，宽度 = 各项文本宽 + 分隔符与间距之和。

- override void OnPaint(App app)
  - 覆写：一行内依次画分隔字形与各项文本，末项用 current 样式。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（项清单 / 分隔符 / 类）。

- override string GetExtra(string key)
  - 面包屑是列表，因此以逗号分隔的单个值存取。

- override bool SetExtra(string key, string val)
  - 覆写：写回扩展属性，"items" 按逗号拆分为项清单。


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

满宽按钮不需要专门的 prop：停靠成一列时按钮本来就占满容器宽度，
放在 Flex 里给它挂 `.grow`（flex-grow: 1）即可——这就是
Naive UI `block` 的效果。

`Style` 把主题默认 -> 皮肤规则 -> 悬停/按下状态解析为一个
StyleBox，`BoxContent` 在该盒子里绘制图标和标签，
`Ui.Activate` 使其可点击——所以本类只负责测量自身并
触发 Click。

- Binding<string> Text;
  - 可绑定标签：`btn.Text = vm.title;` 每帧重新读取模型字段
    （编译器降级的 Binding）；`btn.Text = "OK";` 存储常量。

- string Icon;
  - 图标字形名；"" = 无。只有图标没有文本的按钮按方形测量。

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

- Binding<int> Color;
  - 背景色，Binding 支持可响应式更新。为 0 表示跟随 Class 中的
    角色类型（primary/info/success/warning/error）。非 0 时
    运行时向样式表注入 `[class*="custom-"]` 规则，自动派生
    hover（+10% Lighten）和 pressed（+10% Darken）色。
    这是 Naive UI Button 的 `color` prop 语义。

- Binding<int> TextColor;
  - 文字色，0 表示由 Class 决定。Color != 0 时生效。

- bool Tertiary;
  - tertiary（次要平坦）和 quaternary（卡片内按钮）变体，
    渲染时等价于 Class 里写 tertiary / quaternary。
    对应 Naive UI 的 tertiary / quaternary 按钮。

- bool Quaternary;

- bool Strong;
  - 粗体字（font-weight: 700）。Naive UI Button 的 strong 属性。

- string IconPlacement;
  - 图标在标签的哪一侧：`""`/`"left"` 前置（默认），`"right"` 后置。
    对应 Naive UI 的 icon-placement。后置图标走 BoxContent 的
    trailing 槽位，所以宽度、间距和居中逻辑与前置完全一致
    （"下一步 →"、"更多 ▾" 这类按钮）。
    图标在标签**上方**是另一回事，用 `Class = "stacked"`。

- void InitButton(string label)
  - 初始化：标签文本与默认状态（全圆角、非切换、无覆盖色）。

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

- override bool Fired()
  - 按钮的主动作就是点击，因此遍历整棵子树问「谁被按了」的宿主
    （Control.FiredIn）不必知道 Button 这个类型。

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
  - 在 (x, y) 绘制：宽度 w ≤ 0 时取自然宽度，高度取样式高度；
    返回控件 id。

- int RenderIn(App app, int x, int y, int w, int h)
  - 把按钮绘制到显式盒子中；`h` 为 0 时取解析样式的高度
    （即 Render 的行为），其他值优先——条状控件
    （ribbon 带、工具栏）给命令统一行高时，
    直接传进来即可，无需逐个设置样式。

- void EnsureColorInjected(App app)
  - 解析样式前确保 Color 的 CSS 规则已注入 sheet。
    颜色变化时（1）重新注入派生 CSS（2）触发样式缓存失效。
    
    判重只能查 sheet 里是否已有该色值的规则——不能用按钮
    自身的实例字段，也不能用 bgThemeGen：立即模式的界面
    （演示页、设计器画布）每帧重建控件，实例字段到下一帧
    就归零；而注入本身会递增代次，拿代次当守卫时下一帧必然
    判作"已变化"。两者的结局相同：每个 Color 按钮每帧重新
    注入并再次递增 bgThemeGen，整张样式缓存与 baseSheets
    每帧作废重建，一帧里把 base.css 重解析数次，帧耗时以秒
    计，事件循环被饿死，窗口进入"未响应"。按色值选择器的
    存在性判重后，每个色值在每张 sheet 上只注入一次；换肤
    换掉整张 sheet，规则随之消失，这里自然重新注入。

- static string CustomSelector(int c)
  - Color 派生规则的选择器：`button[class*="custom-<hex>"]`。
    片段按色值取键，同页多个 Color 按钮互不覆盖；带 `button`
    类型前缀使特异性与 base.css 的默认外观规则
    `button[class*="custom-"]` 持平，而应用 sheet 在基础层
    之后应用、同权重时后规则胜出，派生色因此覆盖默认外观。
    BuildColorCss 与判重共用同一份文本。

- string AppendCustomClass(string orig, int c)
  - 编码 Color 值为 `custom-{hex}` class 片段，附加到 Class 末尾。

- string BuildColorCss(int c)
  - 从 Color 值生成 hover/pressed 派生色 CSS 规则。
    选择器按色值取片段匹配，见 CustomSelector。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（文本 / 图标 / 提示 / 开关与变体布尔等）。

- override List<string> Events()
  - 覆写：Click 已属于常用事件集，无需额外声明。

- override void BindEvent(string evt, Action a)
  - 把 Click 路由到 Click UiEvent 字段（与 `btn.Click += h` 同一队列），
    而非基类的 `On` 事件集，使 JSON/设计器的 `onClick`
    处理器与代码处理器走同一通道并按序执行。

- override void OnMeasure(App app)
  - 首选尺寸由解析样式 + 内容得出，停靠/自动布局
    无需手算坐标即可确定按钮大小。

- override bool LintLeafSize()
  - 尺寸自检的自愿者：按钮的宽高有唯一正解（文字+皮肤内边距），
    手写尺寸或被拉成巨块都值得报给作者。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = 在自身矩形上 RenderIn。


## ButtonGroup (class)

NaiveUI 风格的吸附按钮组：多个按钮焊接在一起，
共享边框（仅组的外角为圆角），段间有 1px 分隔线。
支持横向或纵向。所有颜色来自主题，
可自动跟随皮肤变化。

ButtonGroup g = new ButtonGroup(1);   // primary（主样式）
g.Add("Left"); g.Add("Middle"); g.Add("Right");
int hit = g.Render(app, 40, 60);          // 被点击的索引，或 -1

- List<ButtonSegment> segs;
  - 焊接分段（分段模式的内容，Add 追加）。

- int btnType;
  - 按钮类型，决定配色（经 Style.TypeColor/TypeFill 查询）。

- int btnStyle;
  - 0 常规 / 1 描边（Outline）。

- int size;
  - 尺寸档：0 tiny / 1 small / 2 medium / 3 large（Height/FontSz）。

- bool vertical;
  - 纵向排列（仅分段模式）。

- bool barMode;
  - 条状模式（由旧 ButtonBar 吸收而来）：一排常驻的真实
    Button 控件，窄时换行，并上报哪个按钮被按下。
    当 `barMode` 为 false 时，该控件即焊接分段
    组，通过 `Render(app, x, y)` 立即渲染。

- List<Button> barBtns;
  - bar 模式的常驻子按钮。

- int pressed;
  - 上一帧按下的按钮索引；-1 无（TakePressed 消费）。

- int clicked;
  - 上一帧被点击的段索引；-1 无（TakeClicked 消费，分段模式）。

- int active;
  - 当前高亮段（-1 无）——数据驱动用法里表示该行的当前值。

- int lineH;
  - bar 模式行高（测量时由按钮高度得出）。

- int lines;
  - bar 模式换行后的行数（测量时得出）。

- ButtonGroup(int type)
  - 分段模式构造：type 决定配色，默认 medium 尺寸、横向。

- ButtonGroup Add(string label)
  - 追加一个段（自动分配稳定控件 id），链式返回自身。

- int SegCount()
  - 段数（程序化灌入/对账用）。

- void ClearSegs()
  - 清空全部段（选项重灌前用；点击态一并复位）。

- ButtonGroup Outline(bool o)
  - 描边样式开关（链式）。

- ButtonGroup Vertical(bool v)
  - 纵向排列开关（链式，仅分段模式生效）。

- ButtonGroup SetSize(int s)
  - 尺寸档（链式）：0 tiny / 1 small / 2 medium / 3 large。

- static ButtonGroup Bar(string cls)
  - 一排常驻的横向 Button 控件条（旧 ButtonBar）。
    用 AddButton() 添加按钮；它会自动布局、窄时换行，
    并通过 TakePressed() 上报按下索引。可作布局子控件。

- ButtonGroup AddButton(Button b)
  - bar 模式追加一个按钮（链式）。

- Button ButtonAt(int i)
  - 第 i 个 bar 按钮（不查越界）。

- int TakePressed()
  - 上一帧按下的按钮（-1 为无）。此读取会消耗该状态，
    页面恰好处理一次。

- int TakeClicked()
  - 上一帧被点击的段（-1 为无），分段模式专用。此读取会
    消耗该状态——保留模式嵌格时由宿主每帧询问一次。

- int Active()
  - 当前高亮段（-1 无）。数据驱动的分段组（表格组件列等）
    用它表达行的当前值。

- string ActiveLabel()
  - 当前高亮段的标签（无高亮返回空串）。

- void SetActiveIndex(int idx)
  - 按下标设高亮段（-1 清除）；越界静默忽略。

- void SetActive(string label)
  - 按标签设高亮段；`label` 为空串清除。无匹配项时保留原状
    （灌入的是数据文本，比高亮显示更权威）。

- string SegLabel(int i)
  - 第 i 段的标签（越界返回空串）。

- List<int> BarWidths(App app)
  - 各 bar 按钮的自然宽度（AutoWidth）。

- int BarGap(App app)
  - 条状按钮之间的间距。默认 0：组的按钮
    焊接成一条（这正是它看起来像组而不是三个散按钮的原因）；
    若样式表给元素设置 `gap`，
    又会把它们分开。

- static int WeldCorners(bool first, bool last)
  - 焊接行中第 `i` 个按钮保留哪些角：只有行两端
    为圆角，内部接缝为直角。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（类型 / 样式 / 尺寸档 / 纵向 / 类）。
    枚举轴一律 PropSpec.Enum + options：面板出下拉而非裸整数框；
    绑定仍是 num（存序号），PropertyGrid 对旧文档的整数串双向兼容。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"buttongroup"）。

- override void OnMeasure(App app)
  - 覆写：bar 模式按按钮自然宽与组宽推行数并回写首选高度；
    分段模式首选高度即段高（`bw` 未设时按段数估宽），
    保留态宿主（MeasureTree）据此给足格矩形。

- override void OnPaint(App app)
  - 覆写：保留模式绘制——bar 模式换行排布各按钮（焊接行只在
    行首/行尾保留圆角）、记录按下的索引并画接缝分隔线；
    分段模式转调立即渲染 Render，把点击暂存进 clicked 供
    TakeClicked 消费（DataTable 组件列等保留态宿主用）。

- int TypeColor(Theme t)
  - 类型对应的主题强调色。

- int Height(Theme t)
  - 尺寸档对应的主题行高。

- int FontSz(App app, StyleBox s)
  - 尺寸档对应的字号（样式 font 优先）。

- int Render(App app, int x, int y)
  - 立即模式渲染整组：绘制各段并处理点击/悬停/焦点。
    返回被点击的段索引，无点击返回 -1。仅分段模式使用。
    高亮段（active）画填充反色以表达"当前值"。

- int SegMaxW(Theme t, int fs)
  - 最长段的宽度（纵向排列时各段按它等宽对齐）。

- void FillSeg(Canvas c, int x, int y, int w, int h, int r, int col, bool first, bool last)
  - 填充一个段，仅对组的外角做圆角。

- void StrokeSeg(Canvas c, int x, int y, int w, int h, int r, int col, bool first, bool last, bool vert)
  - 段的描边（外角圆角）。


## ButtonSegment (class)

一个焊接段：其标签和稳定控件 id（用于焦点/命中）。
用一个实体代替并行的标签/id 列表。

- string label;
  - 段标签。

- int id;
  - 稳定控件 id（命中/焦点）。

- ButtonSegment(string label, int id)
  - 构造一段。

- string Label()
  - 段标签（宿主按值对账时读取）。


## Calendar (class)

日历（Naive UI Calendar）。Mode 选择外形：
"month" — 整幅月历卡：头部 ‹ 年月 › + 右侧"今天"，星期条，
7×6 网格撑满分配的矩形（相邻月日期灰显、可点选）；
"panel" — 紧凑面板：单月日网格 + 底部"今天 / 清除"动作行。
点击日期经 Change 报告（on-update:value），可见月份变化经
PanelChange 报告（on-panel-change）；IsDateDisabled 挂
is-date-disabled，AddMark 挂逐日议程。value 可为空（panel 的
"清除"），GetDate() 此时返回 null。

Calendar cal = new Calendar();
cal.OnChange(() => { title.Text = cal.GetDate().ToDateString(); });
cal.AddMark(DateTime.Now(), "站会", 1);
cal.RenderInside(app, new Rect(20, 60, 560, 300));

- Binding<string> Mode;
  - 显示模式：month | panel（Binding<string>，设计器可读写）。

- DateTime selected;
  - 选中日期（当天 00:00）；null = 未选。

- int viewYear;
  - 视图所在年月：跟随选中日、翻页箭头与"今天"。

- int viewMonth;
  - 视图所在月（1–12）。

- UiEvent Change;
  - 选中变化（on-update:value）。

- UiEvent PanelChange;
  - 可见月份变化（on-panel-change）。

- DateFilter IsDateDisabled;
  - 该回调返回 true 的日期不可选（Naive UI is-date-disabled）。

- List<CalendarMark> marks;
  - 逐日议程，见 CalendarMark。

- int baseId;
  - 命中 id 块：0 prev，1 next，2 today（month 头部），3 now
    （panel 底部），4 clear（panel 底部），5..46 六行七列日期格。

- bool idsReady;
  - baseId 是否已分配。

- Calendar()
  - 默认选中今天、视图停在当月。

- Calendar(DateTime value):this()
  - 以选中日期构造（视图带到该月）。

- static DateTime DayOf(DateTime t)
  - 把任意时刻归一化到当天 00:00，使选中值、议程与网格单元
    之间的相等比较都落在同一天。

- static DateTime TodayRef()
  - 今天 00:00。

- static int GridStart(int year, int month)
  - 月视图网格的起始格：本月 1 号落在 7 列里的第几格（0=周日列）。

- static DateTime CellDate(int year, int month, int index)
  - 网格第 index 格（0..41）对应的日期：前导格属于上月、尾部格
    属于下月——Naive UI 相邻月日期灰显仍可点的行为。

- static string MonthLabel(int year, int month)
  - 头部标题（Naive UI 的"2026年8月"）。

- static string ToneClass(int tone)
  - 议程色调对应的类片段（calendar::mark.info 等 CSS 规则）。

- DateTime GetDate()
  - 选中日期；panel 的"清除"之后为 null。

- void SetDate(DateTime value)
  - 程序化设值（归一化到当天 00:00 并把视图带到该月），
    不触发 Change——与 Input.SetText 的语义一致。

- void ClearDate()
  - 清空选中（panel 模式"清除"动作，Naive UI clear）。

- void AddMark(DateTime day, string text, int tone)
  - 追加一条议程；day 归一化到当天 00:00，调用方传 DateTime.Now()
    这类带时刻的值也能正确落到格子上。

- void ClearMarks()
  - 清空全部议程（数据驱动宿主重填前调用）。

- bool IsDayDisabled(DateTime d)
  - 该日是否不可选：IsDateDisabled 为空时全部可选。

- Calendar OnChange(Action a)
  - 订阅选中变化（链式）。

- Calendar OnPanelChange(Action a)
  - 订阅可见月份变化（链式）。

- void MoveMonth(int delta)
  - 翻到上/下个月（delta = ∓1），跨年自动进位。

- void JumpToToday()
  - 视图跳回今天所在月份（Naive UI month 头部的"今天"按钮）：
    只滚回当月，不改变选中值。

- void PickDate(DateTime picked)
  - 点选一天：跨月点选把视图带到目标月，相邻月的两次报告
    （Change + PanelChange）与 Naive UI 保持一致。

- void EnsureIds()
  - 首次绘制前分配命中 id 块（47 个）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（text/mode/class）。

- override string GetExtra(string key)
  - 设计器读取选中日期（"text"/"value" 同键，YYYY-MM-DD；未选返回 ""）。

- override bool SetExtra(string key, string val)
  - 设计器写入选中日期（"text"/"value" 同键，解析失败保持原状）；
    其他键返回 false 交给基类。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"/"PanelChange"。

- override void BindEvent(string evt, Action a)
  - 覆写："Change"/"PanelChange" 挂各自 UiEvent，其余按名称走
    通用路由。

- override void OnMeasure(App app)
  - 覆写：按模式量首选尺寸——面板模式紧凑，月历默认 600x340
    （高度须容得下日期格加一行议程）。

- override void OnPaint(App app)
  - 覆写：画表面后按模式分派到月历/面板绘制，并统一派发点击。

- void PaintMonthMode(App app, StyleBox s)
  - month 模式：头部（‹ 年月 › + 今天）、星期条、撑满剩余高度的
    7×6 网格。格与格之间补细分隔线（Naive UI 的单元边框）。

- void PaintPanelMode(App app, StyleBox s)
  - panel 模式：居中标题两端翻页箭头、星期条、紧凑日网格
    （选中日是实心圆角块，同 datepicker::day:selected 的 token）、
    底部"今天 / 清除"动作行（Naive UI actions: now/clear）。

- int MarkColor(App app, int tone, int fallback)
  - 议程色调颜色：`calendar::mark.<tone>` 的 color，未配置时回退
    主文本色。

- void DispatchClick(App app)
  - 点击分发：命中 id 已在绘制时注册，这里只查表映射到动作。


## CalendarMark (class)

单元格议程（Naive UI default slot 的最小对应）：某天的一条标注。
month 模式在日号下方渲染"色点 + 文本"（每格最多两条，放不下截断），
panel 模式渲染成日号下方一枚色点。tone：0 primary，1 info，
2 success，3 warning，4 error。

- DateTime day;
  - 标注落在哪一天（AddMark 时归一化到当天 00:00）。

- string text;
  - 标注文本。

- int tone;
  - 色调：0 primary，1 info，2 success，3 warning，4 error。

- CalendarMark(DateTime d, string t, int tn)
  - 构造一条标注。


## Card (class)

带标题、分隔线和阴影的卡片容器。表面由
样式层解析：皮肤 CSS 中的 `card` / `card.<class>` 规则覆盖
主题的表面默认值，因此换肤无需改动本
代码；`WithFx` 在此基础上可选添加动效。

- string title;
  - 标题文本；空串 = 不画标题行，内容从卡片顶部内边距开始。

- FxOptions fx;
  - 可选动效配置（WithFx 设置）；null = 无。

- bool lifted;
  - 悬停时高亮边框并接收点击，用于可点击的卡片。

- bool bordered;
  - 显示边框：Naive UI bordered，false 时无外框。

- Binding<string> Size;
  - 尺寸变体：small / medium / large（Binding<string>，
    属性面板与响应式绑定直接读写选项文本）。

- int wid;

- void InitCard(string ttl)
  - 初始化：标题文本、自绘表面、边框开、尺寸 medium。

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
  - 控件类型标识（序列化/设计器用）。

- override Binding<string> SizeOf()
  - 覆写：返回 Size 绑定字段（设计器经它发现尺寸档位绑定）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（标题 / 悬停抬升 / 边框 / 尺寸档 / 类）。

- string FxSpec()
  - 动效开关的文本形式（"ripple,aurora"）；无动效为 ""。

- void SetFxSpec(string text)
  - 按 "ripple,aurora" 文本重建动效开关：未列出的关闭，
    空文本撤掉全部动效。glowColor/glowPeriodMs 是代码级配置，
    不在文本形式里。

- override string GetProp(string key)
  - 覆写：`fx` 的应答走 FxSpec（fx 是可选对象，文本只是视图）；
    其余键交基类。

- override void SetProp(string key, string val)
  - 覆写：写入 `fx` 走 SetFxSpec 重建动效开关；其余键交基类。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"card"，与 Kind 一致但布局节点不中表面规则）。

- override void OnMeasure(App app)
  - 覆写：按尺寸档解析 `card` 样式回写内边距；有标题时顶部预留标题行。

- override int StylePadT()
  - 子控件从标题行及其分隔线下方开始：头部是
    卡片自身的装饰，因此样式表 `padding` 只能叠加，
    绝不会让内容滑到标题下面。

- override void OnPaint(App app)
  - 覆写：画卡片表面、标题与分隔线；可选动效（WithFx）与
    Lift 模式下的悬停强调边框及点击命中注册。

- static int ContentY(int y, Theme t)
  - 内容区起始 Y（相对卡片顶）：标题行 + 分隔线占据的高度。

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

轮播图控件：文字页（AddSlide）与控件页（AddSlideView）混排，当前页
经 Index 双向绑定；上一张/下一张箭头与指示点由覆盖层 CarouselChrome
绘制并处理点击，autoplay / dotPlacement / direction 见各字段。

- List<string> slides;
  - 文字页标题（与 AddSlideView 的控件页共用一个列表，
    控件页标题为空串）。

- SignalInt model;
  - 当前页索引（0 基；越界绘制时钳到首/尾页）。

- Binding<int> Index;
  - 双向绑定的当前页索引。

- CarouselAnim anim;
  - 过渡状态（Animate 传入）；null = 直接切换不滑动。autoplay
    计时也寄存在它里面。

- UiEvent Change;
  - 当前页变化时触发（点击、GoTo、autoplay）。

- int baseId;
  - 上一张/下一张与每个指示点一个 id 的连续预留段
    （EnsureIds 分配）。

- int idCount;

- bool autoplay;
  - 自动播放：每 intervalMs 毫秒翻到下一页（Naive UI autoplay）。

- int intervalMs;
  - 自动播放间隔（毫秒）。默认 3000。

- string dotPlacement;
  - 指示点位置：bottom / top / left / right（Naive UI dot-placement）。

- string direction;
  - 滑动方向：horizontal（默认，左右滑）/ vertical（上下滑，
    Naive UI direction）。

- int lastTickMs;
  - 上次翻页时刻（autoplay 用）。

- CarouselChrome chrome;
  - 箭头/指示点覆盖层：始终是最后一个子控件，见 CarouselChrome。

- List<Control> slideViews;
  - 控件页（AddSlideView 加入）：与 slides 平行的子控件列表。页面
    就是真正的控件（典型是 Gui.Widget.Image——文件路径 / data URI /
    http(s) URL / SVG 的地址语义全部由 Image 承担），由轮播负责
    排布、可见性与过渡。null = 全部是文字页。

- void InitCarousel(SignalInt m)
  - 初始化：置默认值（autoplay 关、间隔 3000ms、圆点在底部、横向滑动）。

- Carousel()
  - 空轮播（之后 AddSlide/AddSlideView）。

- Carousel(SignalInt m)
  - 共享当前页信号。

- bool Vert()
  - 纵向轮播？上下滑动、箭头朝上/下。

- Carousel Direction(string d)
  - 设定滑动方向："horizontal"（默认）/ "vertical"。返回自身便于链式。

- void AddSlide(string label)
  - 命名为 AddSlide（而非 Add），以免遮蔽 Control.Add(Control)。
    追加一个纯文字页。

- void AddSlideView(Control v)
  - 加入一个控件页：v 成为轮播的子控件并占满整个轮播框，裁剪与
    过渡动画由轮播管理。与 AddSlide 可混用（控件页的标题留空）。
    chrome 已挂载时把它挪回末位——晚加的页不能排到覆盖层之后，
    否则又会被页盖住。

- static Carousel Of(List<string> labels, SignalInt m)
  - 从标题列表构建文字页轮播。

- Carousel Animate(CarouselAnim state)
  - 开启页面过渡：`state` 由宿主持有（可每帧重建控件），
    传 null 关闭过渡（直接切换）。

- void EnsureIds()
  - 上一张、下一张，然后每个指示点一个 id，作为连续块预留。

- override void SyncBinding()
  - 把绑定值拉入本地信号（模型 -> UI）。

- void GoTo(App app, int i)
  - 翻页（编程入口）：越界回绕到头/尾，变化时触发 Change。

- override void Arrange(int px, int py, int pw, int ph)
  - 覆写：占满给定矩形并让控件页随之铺满；首次排布时把覆盖层
    惰性挂载为最后一个子控件（压在所有页之上）。

- override void OnMeasure(App app)
  - 覆写：把 dotPlacement/纵向映射为 Class 片段后量取样式，
    未指定时兜底 320x160。

- override void OnPaint(App app)
  - 覆写：同步绑定并驱动 autoplay 计时，按过渡状态滑动绘制页面
    （文字页在这里画；控件页由子树自绘，这里只摆位与控可见性）。

- void ShowSlides(int from, int to, int shift, int dir)
  - 让控件页与过渡状态一致：静止帧只有当前页可见（占满轮播框）；
    过渡帧离开页沿滑动轴停在 bx/by-dir*shift、进入页在进入位，
    其余页隐藏。控件的 OnPaint 之后才会画子树，所以这里赶在子树
    绘制前把矩形与可见性摆好即可。

- void PaintSlide(App app, int x, int y, int i)
  - 在任意位置绘制一张文字页（主体 + 居中标签），过渡时可并排
    绘制离开和进入的面板。控件页自己会画（RenderTree 的子树
    遍历），这里只负责文字页。

- void PaintChrome(App app, int count, int cur)
  - 箭头和指示点，以及各自的点击区域。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（slides/index/autoplay/interval/
    dotPlacement/direction/class）。

- override string GetExtra(string key)
  - 幻灯片是列表，因此以逗号分隔的值序列化
    （`slides` 键；页索引走 Props 的 index 属性）。

- override bool SetExtra(string key, string val)
  - 设计器写入 `slides`（逗号分隔标题）；其他键返回 false。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"。

- override void BindEvent(string evt, Action a)
  - 覆写："Change" 挂 Change 事件，其余按名称走通用路由。


## CarouselAnim (class)

每轮播图一个过渡状态，独立成对象，使每帧重建
控件的宿主仍能持有进行中的动画。经 `Carousel.Animate(state)`
传入；要 autoplay 的宿主必须传入同一实例（tickMs 也在里面）。

- int shown;
  - 已稳定显示的页（过渡完成后的落点）。

- int from;
  - 过渡的离开页。

- int startFrame;
  - 过渡起始帧号（app.frameCount）。

- bool animating;
  - 过渡进行中。

- int dir;
  - 滑动方向：1 = 翻到下一页，-1 = 上一页。

- int tickMs;
  - autoplay 的挂钟：宿主每帧重建控件时实例字段会归零，
    计时永远走不完；跟着 anim 一样由宿主持有才能跨帧存活。

- CarouselAnim()
  - 初始化：过渡状态全部清零（dir=1 朝下一页）。


## CarouselChrome (class)

轮播图：带上一张/下一张箭头和指示点的滑动面板。当前
页为双向绑定；`Animate(anim)` 在变化时
水平滑动面板。

Carousel car = Carousel.Of(slides, idx);
car.Animate(Gallery.carAnim);
car.RenderInside(app, rect);
轮播的覆盖层：箭头与指示点必须浮在控件页（图片页）之上，而
RenderTree 先画父节点自己的 OnPaint 再画子树，父级里画的部件
会被图片页盖住。覆盖层作为最后一个子控件参与子树绘制，天然
位于所有页面之上；箭头/指示点的命中登记与点击处理也在这里做，
保持"同一帧内先登记后派发"的次序。

- Carousel owner;

- void InitChrome(Carousel c)
  - 初始化：以 "carousel-chrome" 注册为无子控件覆盖层，记下宿主并自管命中事件。

- CarouselChrome(Carousel c)
  - 构造：转发到 InitChrome。

- override void OnPaint(App app)
  - 覆写：画箭头与指示点覆盖层，并处理各自命中（上/下一张、跳到指定页）。


## Checkbox (class)

带勾选图标和响应式布尔绑定的复选框。

Checkbox agree = Checkbox.Bind("I agree", model);
agree.Change += () => { ... };   // 每次切换时触发
agree.Render(app, x, y);

- string label;
  - 显示的标题文本（SetLabel / 设计器 label 属性写它）。

- SignalBool model;
  - 勾选状态信号（true = 选中）。

- Binding<bool> data;
  - 双向绑定状态：`cb.data = user.agreed;` 使模型字段
    与复选框双向同步（编译器降级的 Binding）。

- int wid;

- UiEvent Change;
  - 选中状态切换时触发（C# 风格：`cb.Change += h;`）。

- Binding<bool> Indeterminate;
  - 半选态（Naive UI indeterminate prop）：绘制横线而不是勾。
    用于"全选父项"语义——部分子项选中时为 true。点击仍然
    在 on/off 间切换；Binding<bool> 可绑定到模型字段实现
    `全选 = 子项全部勾选` 的派生值。

- void InitCheckbox(string lbl, SignalBool m)
  - 初始化：标题与勾选信号（默认非半选）。

- Checkbox()
  - Default constructor used by .zform and the string-kind registry.

- Checkbox(string lbl)
  - 保留模式构造函数：`Checkbox agree = new Checkbox("I agree");`

- static Checkbox Bind(string label, SignalBool model)
  - 绑定到模型信号的复选框：勾选状态随 model 双向同步。

- string Str()
  - 当前显示的标题。唯一的标题通道就是 `label` 字段：
    `Props()` 把它以活绑定暴露给 GetProp/SetProp（设计器、
    zform 宿主与运行期 SetProp("label"/"text") 全部同路）。
    曾经这里有一个优先于 label 的 `text` 常量绑定，结果是
    运行期改 label 字段后渲染仍读旧常量——发布/项目配置
    对话框里所有复选框标签因此消失，教训：标题只留一条通道。

- void SetLabel(string v)
  - 运行期改写标题；与 `Label.Text = ...` 保持一致。

- bool IsChecked()
  - 当前是否选中（模型信号的当前值）。

- void SetChecked(bool v)
  - 程序化设置选中态：写模型信号，绑定了 data 时同步写回模型
    字段。不触发 Change（那是用户切换的事件）。

- void SyncBinding()
  - 将绑定的模型值拉入本地信号（模型 -> UI）。

- int Render(App app, int x, int y)
  - 保留模式渲染：绘制、分发自身的点击/键盘切换并
    触发 Change。返回控件 id。

- bool IsIndeterminate()
  - 当前是否处于半选态。

- int RenderIn(App app, int x, int y, int h)
  - 与 Render 相同，但按排布给定的行高 `h` 绘制（0 = 按样式/
    主题的控件高度）。停靠成一列的复选框行高由容器决定，
    用主题高度画会让方框和点击区溢出到下一行上。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change" 与 "Toggle"。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（标题 / 选中态 / 半选）。

- override void BindEvent(string evt, Action a)
  - 覆写："Change"/"Toggle" 都挂 Change 事件，其余按名称走通用路由。

- int PaintStyled(App app, int id, int x, int y, int h, string text, bool checked)
  - 同三参版本，非半选。

- int PaintStyled(App app, int id, int x, int y, int h, string text, bool checked, bool indet)
  - 绘制勾选框 + 标签并登记命中矩形：选中态经约 130ms 淡入
    交叉淡化，indet 为 true 时画半选横线。返回控件 id。

- static void PaintBox(App app, int x, int y, int size, int level)
  - 绘制方框 + 标签并注册点击区域。保留模式与
    旧模式路径共用，保证两者外观一致。
    仅绘制复选框图形（无标签、无点击区域），使需要
    内联复选框的控件画出与真实复选框相同的图形，而无需
    自行绘制。`level` 在 0（未选中）~1000（选中）间交叉淡化。

- static void PaintBoxIndeterminate(App app, int x, int y, int size)
  - 半选态的内联方框：填充态方框中央一条短横线（与 PaintBox
    同一套 checkbox::box / ::check 规则）。

- override bool LintLeafSize()
  - 尺寸自检的自愿者：勾选框的宽高同样由方框+标签自算。

- override void OnMeasure(App app)
  - 覆写：按选中态解析样式，宽 = 方框 + 间距 + 标签宽，高取样式行高。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = 以自身矩形、行高 bh 走 RenderIn。


## CheckboxGroup (class)

复选框组：每个选项一个独立的真实 Checkbox。提供聚合
查询（IsChecked/CheckedCount）与组级 Change 事件：任一
选项被用户切换时组事件一并触发，便于表单读取全部勾选。

- UiEvent Change;
  - 任一子复选框被用户切换时触发（C# 风格：`group.Change += h;`）。

- CheckboxGroup()
  - 构造空组。

- override Control MakeItem(int index, string text)
  - 覆写：为选项造一个真实 Checkbox，其切换冒泡为组级 Change。

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
  - 覆写："Change"/"Toggle" 都挂组事件，其余按名称走通用路由。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。


## ChoiceGroup (class)

选项组基类：把若干真实选择控件（Radio/Checkbox）按水平流布局排列，
超出可用宽度自动换行。设计器预览与运行时窗口经 FormBuilder / formgen
共用同一实现，使多选项在两侧真实且一致地渲染。无表面。

- int gapX;
  - 列距 / 行距的回退值：样式表的 `column-gap` / `row-gap` / `gap`
    优先。

- int gapY;

- int columns;
  - 每行放几个（0 = 按可用宽度自动换行）。样式表的 `columns`
    优先。大于 0 时该行按可用宽度等分。

- int rowHeight;
  - 行高（0 = 按最高的选项）。样式表的 `line-height` 优先。

- int sGapX;
  - gapX / gapY / rowHeight 都是逻辑像素（100% 下的样子），这里存
    它们在当前缩放下的值：直接拿逻辑值当物理像素用，150% 下行距
    和行高都会偏小，相邻选项行互相贴住。OnMeasure 每帧刷新，
    拿不到 App 的 Arrange 复用。

- int sGapY;

- int sRowH;

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
  - 公共初始化：列距 16 / 行距 4 / 自动换行 / 自适应行高（逻辑像素）。

- void SetColumns(int n)
  - 每行的选项个数（0 = 按宽度自动换行）。设为 N 后该行按可用宽度等分成
    N 列，最后一行不足 N 个也保持同样的列宽。

- int Columns()
  - 当前配置的每行选项数（0 = 按宽度自动换行）。

- void SetRowHeight(int h)
  - 每行的高度（0 = 按最高的选项自适应）。选项在行内垂直居中，
    所以调大行高只会拉开行距，不会让相邻行互相压住。

- int RowHeight()
  - 当前配置的行高（0 = 按最高选项自适应）。

- override string StyleType()
  - 选项组是自己的 CSS 类型（`checkboxgroup` / `radiogroup`），而不是
    Panel 的 `stack`：皮肤要能单独给它写 `columns` / `row-gap` /
    `column-gap` / `line-height`，而不影响所有布局容器。

- virtual Control MakeItem(int index, string text)
  - 供子类实现：为第 index 个选项构造单个选择控件。

- virtual string OptionsText()
  - 选项以 `a|b|c` 字符串表示（设计器 / .zform 属性往返）；
    子类可扩展语法（RadioGroup 的 `!` 后缀 = 禁用位）。

- int Count()
  - 追加一个选项，返回新建的选择控件（越界/构造失败时为 null）。
    代码里逐项建组用这个：`g.AddOption("Cheese")`——`Add` 是基类
    挂子控件用的，选项组要的是「按标签造一个选项」，两回事。
    选项个数。聚合查询（"全选" 父项要拿它和 CheckedCount 比）
    靠它，调用方不必去数 children。

- Control AddOption(string text)
  - 按标签构造一个选项控件并对齐 labels（失败返回 null；
    详见上方 Count 处的说明——那份文档本属本方法）。

- virtual void SetOptionsText(string text)
  - 按 "A|B|C" 追加选项（与 SelectBox.SetOptionsText 同一约定）；
    子类可覆写以扩展逐项语法。

- override string GetProp(string key)
  - 覆写：`options` 的应答走 OptionsText——选项是结构状态，
    文本只是它的视图，不能落 Props() 的快照；其余键交基类。

- virtual void ClearOptions()
  - 清空全部选项（设计器重复写入 `options` 前调用；子类要连
    平行的元数据列表一起清，否则下标错位）。

- override void SetProp(string key, string val)
  - 覆写：写入 `options` 先清后建（面板可反复编辑，追加语义
    只属于构造期的 SetOptionsText 直调）；其余键交基类。

- override string GetExtra(string key)
  - 设计器读取 `options`（"A|B|C" 文本）；其余键返回 ""。

- override bool SetExtra(string key, string val)
  - 设计器写入 `options`（"A|B|C" 逐项追加）；其他键返回 false
    交给基类。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（columns/rowHeight）。

- override void OnPaint(App app)
  - 覆写：自动换行行数随宽度变化时按新行数重测一次，
    避免高度停留在旧行数上把选项裁掉。

- int InnerW(int pw)
  - 内容区宽度（去掉内边距）。

- int RowsHeight(int rows)
  - `rows` 行占用的总高度（含内边距与行距）。

- int FlowRows(int avail)
  - 自动换行模式下，宽度 `avail` 内能排下的行数。

- int RowGapPx()
  - 像素行距/列距：与 ResolveMetrics 同一解析（CSS row-gap /
    column-gap，回退到字段配置），供按钮组形态在样式解析后取用。

- int ColGapPx()
  - 像素列距（CSS column-gap，回退字段配置）。

- void ResolveMetrics()
  - 解析行几何（列数 / 行距 / 列距 / 行高）。全部取自已解析的
    样式盒与子项尺寸，不需要 App，因此 Arrange 也能自己调（不
    依赖先跑过 OnMeasure）。

- override void OnMeasure(App app)
  - 覆写：解析行几何后按固定列数/自动换行算出行数，
    得出内容首选宽高。

- override void Arrange(int px, int py, int pw, int ph)
  - 覆写：固定列数时等分宽排布（余数摊给前几列），否则按选项
    自身宽度流式换行；行高统一，选项在格内垂直居中。

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
  - 初始化：置标题/内容行/滚动信号并自管 Copy 事件。

- CodeBlock()
  - 空代码块（设计器用）。

- CodeBlock(string caption, List<string> src)
  - 标题 + 内容行；src 被直接引用，替换内容请走 SetLines。

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
  - 行高：`codeblock::line` 字号加呼吸空间。

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
  - 覆写：自然高度（标题栏 + 行数 × 行高 + 留白），宽取最宽行
    加行号槽。

- override void OnPaint(App app)
  - 覆写：画表面、标题栏（标题 + 复制按钮）、行号与代码文本，
    并处理滚轮滚动、文本选择与复制交互。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（caption/copyLabel/class）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Copy"。

- override void BindEvent(string evt, Action a)
  - 覆写："Copy" 挂 Copy 事件，其余按名称走通用路由。


## Collapse (class)

手风琴（NaiveUI n-collapse）。同时只展开一个面板；`Active` 保存
展开面板的索引（-1 = 全部折叠），可双向绑定：

Collapse c = new Collapse();
c.AddPanel("General", "Theme, language and startup");
c.Active = vm.openPanel;

- List<CollapsePanel> panels;
  - 面板列表（AddPanel 追加）。

- SignalInt open;
  - 内部展开索引信号（-1 = 全折叠），与 Active 双向同步。

- Binding<int> Active;
  - 双向绑定的展开索引；-1 折叠所有面板。

- UiEvent Change;
  - 点击标题切换展开面板时触发。

- int baseId;

- int idCount;

- string arrowPlacement;
  - 折叠箭头位置：left / right（Naive UI arrow-placement）。

- bool expander;
  - true=accordion 模式（同时只开一个），false=可同时多开
    （Naive UI expandX / accordion 取反语义）。

- void InitCollapse(SignalInt m)
  - 初始化：共享展开索引信号（-1 = 全折叠），默认手风琴、箭头居左。

- Collapse()
  - 空手风琴（之后 AddPanel）。

- Collapse(SignalInt m)
  - 共享展开索引信号（-1 = 全折叠）。

- void AddPanel(string title, string body)
  - 追加一个面板（标题 + 正文文本）。

- void AddPanel(CollapsePanel panel)
  - 追加一个已构建的面板记录。

- static Collapse Of(List<CollapsePanel> items, SignalInt m)
  - 基于现有面板记录构建手风琴。

- static int HeaderHeight(App app)
  - 面板标题行高（主题 heightLarge，部件样式可覆盖）。

- static int BodyHeight(App app)
  - 展开面板正文行高（同上）。

- static int Height(App app, int count, SignalInt openIdx)
  - 总高度：count 个标题行 + 展开时一行正文。

- void EnsureIds()
  - 每个面板标题占一个 id，作为连续块预留，使
    面板列表不变时点击 id 保持稳定。

- override void SyncBinding()
  - 覆写：把 Active 绑定的值拉入本地展开信号（model -> UI）。

- override void OnMeasure(App app)
  - 覆写：高 = 标题行数 x 行高 +（有展开面板时）正文行高，宽缺省 240。

- override void OnPaint(App app)
  - 覆写：画各面板标题行（箭头 + 标题 + 分隔线）与展开面板正文，
    处理点击展开/折叠并写回 Active 绑定。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（标题 / 正文 / 展开项 / 箭头位置 / 多开 / 类）。

- override string GetExtra(string key)
  - 设计器额外键：`titles`/`bodies` 以逗号分隔文本与面板列表
    互相转换（运行时实体仍是 CollapsePanel 列表）。

- override bool SetExtra(string key, string val)
  - 设计器写入 `titles`/`bodies`（逗号分隔）；其他键返回 false。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"。

- override void BindEvent(string evt, Action a)
  - 覆写："Change" 挂 Change 事件，其余按名称走通用路由。


## CollapsePanel (class)

一个手风琴面板：标题和内容作为一个记录同存。
经 `Collapse.AddPanel(title, body)` 或 `Collapse.Of(list, m)` 使用。

- string title;

- string body;

- CollapsePanel(string title, string body)
  - 构建一个面板记录。


## ColorPicker (class)

颜色选择器（对齐 Naive UI n-color-picker）：触发条显示当前颜色，
点击弹出 SV 面板（饱和度×明度，两层渐变自绘）、色相条（六段
渐变）、透明度条（棋盘格打底）与 RGBA/HEX/HSL/HSB 输入行
（左侧标签点击循环切换格式，回车提交）。所有尺寸按 DPI 缩放，
皮肤走 `colorpicker` 规则（`::trigger` / `::panel` / `::checker` /
`::label`）。

C# 风格保留式实例：
ColorPicker pick = new ColorPicker(0xFF5B3737);
pick.Change += () => { ... };   // 值变化时触发
pick.Render(app, x, y, width);
模式（Naive UI modes，决定取值文本格式）：
pick.Modes("rgb", "hex", "", "");  // 限制可选格式
pick.Mode = "hex";                 // 当前取值文本格式
string v = pick.Text();            // "#5B3737FF"
带透明度（Naive UI show-alpha，默认开）：
pick.SetShowAlpha(false);          // 隐藏透明度条，alpha 恒 1

内部以 HSV（h 0..359、s/v 0..1000）+ alpha（0..255）工作，绑定
值是打包的 0xAARRGGBB（`SignalInt` / `Binding<int>` 双向）。Zan
的 int 是 32 位，打包色常以负数形式存储——凡提取分量一律走
RgbOf / AlphaOf（App.LerpColor 同款符号安全写法），禁止裸除。

- SignalInt model;
  - 内部当前打包色信号（0xAARRGGBB）；存在双向绑定时与其同步。

- Binding<int> data;
  - 双向绑定值（打包 0xAARRGGBB）：`pick.data = model.color;`
    保持模型字段与选择器双向同步。

- int wid;
  - 触发条命中 id；+1/+2/+3 为 SV 面板 / 色相条 / 透明度条（WidgetId.Block(8)）。

- UiEvent Change;
  - 值变化时触发（C# 风格：`pick.Change += h;`）。拖动面板/条、
    输入行回车提交都会触发；程序化 SetColor 不触发。

- bool open;
  - 弹层展开状态。

- int hue;
  - HSV 内部状态：h 0..359，s/v 0..1000（v=0 纯黑、s=0 灰阶）。

- int sat;
  - 饱和度 s，0..1000。

- int val;
  - 明度 v，0..1000。

- int alpha;
  - alpha 通道 0..255。

- bool showAlpha;
  - 显示透明度条（Naive UI show-alpha，默认开）。

- List<string> modes;
  - 可选格式（Naive UI modes）：rgb / hex / hsl / hsb 的子集。
    空列表 = 全部四种。取值文本跟随 Mode。

- Binding<string> Mode;
  - 当前取值文本格式（Naive UI 的 value 格式跟随 modes）。

- int dragPart;
  - 正在拖动的部件：0 无 / 1 SV 面板 / 2 色相条 / 3 透明度条。

- Input inR;
  - RGBA 四个数字输入框（弹层内保留态，焦点跨帧存活）。

- Input inG;
  - G 通道输入框。

- Input inB;
  - B 通道输入框。

- Input inA;
  - A 通道输入框（0..100%）。

- Input inHex;
  - HEX 十六进制输入框（#RRGGBB 或 #RRGGBBAA）。

- Input inHsl;
  - HSL 三元组输入框（"h, s%, l%"）。

- Input inHsb;
  - HSB 三元组输入框（"h, s%, v%"）。

- int inputView;
  - 输入行当前显示的格式：0 RGBA / 1 HEX / 2 HSL / 3 HSB，
    左侧标签点击循环切换。

- static int SvPanelOff()
  - id 段（WidgetId.Block(8)）：wid = 触发条，wid+1 = SV 面板，
    wid+2 = 色相条，wid+3 = 透明度条。

- static int HueOff()
  - 色相条命中 id：wid + 2。

- static int AlphaOff()
  - 透明度条命中 id：wid + 3。

- static int Opaque(int r, int g, int b)
  - 由 8 位 r/g/b 生成"不透明"打包色。255*2^24 在 32 位里回绕成
    负数——与 Theme.Rgb 的位型一致，属规范存储形式。

- static int RgbOf(int packed)
  - 打包色的低 24 位 RGB（0..16777215，负数包也正确）。

- static int AlphaOf(int packed)
  - 打包色的 alpha 通道（0..255，负数包也正确）。

- static bool SamePacked(int a, int b)
  - 两个打包色是否同一个颜色（各通道比较，兼容正负两种存储）。

- static int HsvToRgb(int h, int s, int v)
  - HSV → 打包不透明色（h 0..359，s/v 0..1000）。

- static int hueOut;
  - RGB（0..16777215 低 24 位）→ HSV。s/v 打包成 s*4096+v 返回，
    h 写入静态 hueOut（Zan 无出参；调用后立即消费）。
    RgbToSv 写出的色相（0..359）；Zan 无出参，调用后立即消费。

- static int RgbToSv(int rgb)
  - RGB（低 24 位）转 HSV：h 写入 hueOut，返回 s*4096+v。

- static string HexText(int color)
  - 打包色 → "#RRGGBB"。

- static string HexTextAlpha(int color)
  - 打包色 → "#RRGGBBAA"（alpha 始终写出）。

- static string Hex2(int b)
  - 字节 → 两位大写十六进制文本。

- static int HexDigit(string ch)
  - 单个十六进制字符 → 0..15（非法返回 -1；大小写均接受）。

- static bool parseOk;
  - HexParse / FuncParse 最近一次是否解析成功（Zan 无出参；且
    合法打包色在 alpha ≥ 0x80 时回绕成负 int，-1 不能再当失败
    哨兵——旧写法把 "#RRGGBB" 一类高 alpha 颜色整体拒收。
    hueOut 同款套路）。

- static int HexParse(string s)
  - "#RGB"/"#RRGGBB"/"#RRGGBBAA" → 打包色（失败时 parseOk=false）。

- static int FuncParse(string s)
  - "rgba(91, 55, 55, 1)" / "rgb(91, 55, 55)" /
    "hsl(120, 50%, 40%)" / "hsb(120, 50%, 40%)"（可带第 4 个
    alpha 参数）→ 打包色（失败时 parseOk=false）。

- static int PctOrFrac(string s)
  - "1" / "0.5" / "50%" → alpha 0..255（非法 -1）。

- static int PctNum(string s)
  - "50%" → 0..1000 千分比（非法 -1）。

- static int ParseIntSafe(string s)
  - 纯数字串 → int（任何非数字字符返回 -1；防 Convert.ToInt32
    对垃圾输入的运行时错误）。

- static int RgbHsl(int r, int g, int b)
  - RGB → HSL（h 0..359 * 1e6 | s 0..1000 * 1e3 | l 0..1000）。

- static int HslToRgb(int h, int s, int l)
  - HSL → 打包不透明色。

- bool hsvTouched;
  - 正在拖动/已拖动色盘的标志：false 时 CurrentRgb 直接回
    basePacked（赋值往返精确，不被 HSV 千分比量化磨损）。

- int basePacked;
  - 最近一次外部赋入的打包色（hsvTouched=false 时的取值基准）。

- void InitPicker(SignalInt m)
  - 公共初始化：分配 id 块、建默认输入行并挂 Submit 静态转发。

- ColorPicker()
  - Default design-time constructor（黑色）。

- ColorPicker(int packed)
  - 自持信号：`ColorPicker pick = new ColorPicker(0xFF5B3737);`

- int CurrentRgb()
  - 当前打包颜色。HSV 分量是 0..1000 的整数量化——纯 HSV 重算会把
    外部赋进来的颜色抹掉 1/1000（91,55,55 显示成 90,54,54）。在
    用户没动过色盘（hsvTouched=false）时直接回原包，赋值往返精确。

- void ApplyPacked(int packed)
  - 从打包色同步内部 HSV/alpha（外部 SetColor、绑定回拉共用）。

- void SetColor(int packed)
  - 程序化赋值（同步内部 HSV 与触发条；不触发 Change）。

- int Value()
  - 当前模型值（打包 0xAARRGGBB）。

- void Commit()
  - 用户手势落定：写模型、写回双向绑定、触发 Change。

- override void SyncBinding()
  - 将绑定的模型值拉入内部状态（model -> UI）。

- void SetShowAlpha(bool on)
  - 显示透明度条（Naive UI show-alpha）。关闭时 alpha 恒 1。

- void SetModes(List<string> list)
  - 设定可选格式（Naive UI modes）。空列表 = 全部。
    只配一种时取值格式随之锁定（Naive UI 传单一 modes 的行为），
    此处直接改写 Mode，纯计算路径（Text()）与渲染路径同源。
    当前 Mode 不在新集合里时改取集合首个（Naive UI 的行为：
    modes 缩小后 value 格式必须仍是可选格式之一）。

- void Modes(string a, string b, string c, string d)
  - 限制可选格式：Modes("rgb", "hex", "", "")。

- string Text()
  - 当前取值文本（跟随 Mode / modes 缺省）。rgb →
    "rgba(91, 55, 55, 1)"（showAlpha=false 时 "rgb(91, 55, 55)"），
    hex → "#5B3737FF"（showAlpha=false → "#5B3737"），hsl/hsb
    → "hsl(0, 34%, 29%)"。

- string ModeName()
  - 当前生效的格式名（Mode 绑定值；非法/为空时取 modes 首个，
    再缺省 "rgb"）。

- string FormatAs(string m)
  - 按 `m` 格式格式化当前颜色。

- string AlphaText()
  - alpha 通道的文本形式：255 显示 "1"，其余 0..1 两位小数。

- override string Kind()
  - 设计器控件类型名："ColorPicker"。

- override List<PropSpec> Props()
  - 设计器属性表：showAlpha 开关与 mode 取值格式枚举。

- override string GetExtra(string key)
  - 设计器读取扩展属性：value/color 返回当前取值文本，showAlpha/mode 返回同名设置。

- override bool SetExtra(string key, string val)
  - 设计器写入扩展属性：showAlpha/mode/value/color/text；未知键返回 false。

- override List<string> Events()
  - 设计器事件列表：通用事件之外提供 "Change"。

- override void BindEvent(string evt, Action a)
  - 设计器事件绑定："Change" 订阅 Change，其余事件走通用 On。

- void SetText(string value)
  - 从文本赋值（designer / 程序化）："#RRGGBB[AA]" 与
    rgba()/rgb()/hsl()/hsb() 都接受，非法文本忽略。

- int TriggerH(App app)
  - 触发条高度（皮肤可写 colorpicker::trigger 的 height 覆盖）。

- int Render(App app, int x, int y, int width)
  - 保留式渲染：以触发条标准高度绘制，返回触发条命中 id。

- int RenderIn(App app, int x, int y, int width, int height)
  - 显式盒高版本（保留式布局走 OnPaint 自动带 bh）。

- override void OnPaintOverlay(App app)
  - 弹层打开时在覆盖阶段绘制取色弹层，并拦截、分发弹层内的交互。

- static int ViewOf(string m)
  - 输入行视图编号：0 RGBA / 1 HEX / 2 HSL / 3 HSB。

- static string ViewLabel(int view)
  - 视图编号 → 输入行左侧标签文字（RGBA / HEX / HSL / HSB）。

- static int NextAllowedView(int start, List<string> modes)
  - 从 `start` 往后找 modes 允许的第一个视图（全空/找不到回 start）。

- static bool Allowed(int view, List<string> modes)
  - `view` 是否在允许的 modes 里（空列表 = 全部允许）。

- static ColorPicker editing;
  - 正在渲染输入行的选择器（Input 的 Submit 委托无法捕获 this，
    静态转发宿主，DatePicker.current 同款）。

- bool viewAligned;
  - 弹层打开后是否已把输入行对齐到 Mode（每次打开重新对齐）。

- static void SubmitR()
  - Input.Submit 的静态转发入口：经 editing 找到当前宿主后分发。

- static void SubmitG()
  - Input.Submit 静态转发：提交 G 通道输入。

- static void SubmitB()
  - Input.Submit 静态转发：提交 B 通道输入。

- static void SubmitA()
  - Input.Submit 静态转发：提交 A 通道（0..100%）输入。

- static void SubmitHex()
  - Input.Submit 静态转发：提交 HEX 整色输入。

- static void SubmitHsl()
  - Input.Submit 静态转发：提交 HSL 整色输入。

- static void SubmitHsb()
  - Input.Submit 静态转发：提交 HSB 整色输入。

- void SubmitChannel(int which)
  - 输入行回车提交：0-2 替换单个 R/G/B 通道，3 写 alpha（0..100%），
    4-6 整色替换（hex / hsl / hsb）。非法文本忽略。解析成功的一律
    走 ApplyPacked 整色替换——单通道补丁会让 CurrentRgb 的
    hsvTouched 基准停在旧包上。

- void PaintInputs(App app, int x, int y, int h, int w, bool clicked)
  - 输入行的保留态子控件排布 + 文本同步。四种格式各一组 Input，
    只排当前视图那组（其余不排布即不显示、不吃键盘）。

- void LayInput(App app, Input inp, int x, int y, int w, int h)
  - 子输入框在弹层里的排布（不走树的 Arrange，手动摆）。

- void SyncInput(App app, Input inp, string text)
  - 文本相同或字段正被编辑就不写，避免每帧重置光标。

- override void OnMeasure(App app)
  - 测量首选尺寸：高取样式 height（缺省 heightMedium），宽取样式 width（缺省 160）。

- override void OnPaint(App app)
  - 保留式绘制：在自身矩形内 RenderIn。


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
  - 在 (mx, my) 绘制菜单并处理本帧交互：返回 -1 尚无选择、
    -2 已关闭（点击外部/在菜单外右键），其余为所选条目下标。
    宽度按最宽条目自适应，位置钳制到窗口内。


## Countdown (class)

倒计时：从 `Duration` 毫秒起倒数，归零时触发 `Finish`。
`Active` 暂停/恢复，`Format` 用令牌控制显示的位面：

| 令牌 | 含义                     |
|------|--------------------------|
| D    | 总天数（不补零）         |
| HH   | 时（补零）               |
| mm   | 分（补零）               |
| ss   | 秒（补零）               |
| S    | 十分之一秒（1 位数字）    |
| SS   | 百分之一秒（补零，2 位）  |

时间单位一律双写，其余字符（含单个 h/m/s）原样输出，因此
`"mm:ss"`、`"ss.S"`、`"D 天 HH:mm:ss"` 都是合法格式，而
`"T-minus"` 这类字面量不会被吃掉；`"sss"` 渲染成 `"05s"`。
时刻由 tick 差值推得（不按帧数累计），掉帧、窗口失焦再回来
都不会走慢；走动期间经 `App.RequestAnimationFrameIn` 限速
重绘——秒级格式只在对齐秒边界时醒来，含 `S` 以 100ms 节奏、
含 `SS` 以 16ms（60fps，显示帧率封顶）节奏刷新。百分位**不**
按 10ms 排程：显示值每帧由 tick 差值现算，60fps 下依然逐帧
准确，而 100 次唤醒/秒会让事件循环永远睡不成。

绘制形态：默认皮肤下套一张圆角卡片（`countdown::box`）+ 底部
剩余进度条（`::track`/`::fill`，success/warning/error 角色类给
进度条换色）；控件太小（密集布局内嵌）时退化裸文本。暂停或
归零时落到 disabled 皮肤，数字与面板一起降调。

Countdown cd = new Countdown(5 * 60000, "mm:ss");
cd.Finish += () => { label.Text = "Done"; };

外部改变 `Duration`（含双向绑定）会自动重摆；`Restart()`
手动重摆到当前 `Duration`。

- Binding<int> Duration;
  - 总时长（毫秒）。变化即重摆；默认 60000。

- Binding<bool> Active;
  - 是否走动。false 暂停（保持剩余值），恢复后从剩余值继续。

- Binding<string> Format;
  - 显示格式，令牌见类型说明；空串按 `"HH:mm:ss"`。

- UiEvent Finish;
  - 倒计时归零（仅触发一次，重新走动后再次归零会再触发）。

- int remainingMs;

- int lastTickMs;

- int lastDuration;

- bool finished;

- bool activeNow;

- int subCad;

- void InitCountdown(int durationMs, string fmt)
  - 构造器链的公共初始化（控件类型 "countdown"）。

- Countdown()
  - 60 秒默认倒计时（设计器用）。

- Countdown(int durationMs)
  - 指定总时长（毫秒），默认格式。

- Countdown(int durationMs, string format)
  - 总时长 + 格式（令牌见类型注释）。

- int Remaining()
  - 当前剩余毫秒（宿主轮询用；推模式请挂 `Finish`）。

- void Restart()
  - 重摆到当前 `Duration` 并从头开始走。暂停态下只清零进度，
    不改变 `Active`。

- void Arm(int ms)
  - 重摆：剩余值置为 `ms`，下一拍重定时间基准，清除已触发标记。

- void Tick(int nowMs)
  - 推进计时并按需触发 Finish。`nowMs` 由调用方传入
    （绘制路径传 `Window.GetTickMs()`），测试可注入确定时刻。

- void SyncProps()
  - 读取绑定的 Duration/Active：Duration 变化即重摆。

- string Fmt()
  - 生效格式：Format 绑定为空/null 时按 "HH:mm:ss"。

- string DisplayText()
  - 当前帧显示文本（按 Fmt() 与剩余毫秒现算）。

- static string FormatText(string fmt, int rem)
  - 纯函数格式化：`rem` 为剩余毫秒，令牌见类型说明；
    `fmt` 为空按 `"HH:mm:ss"`。

- static string Pad2(int v)
  - 两位数字补零（"07"）。

- override void OnMeasure(App app)
  - 覆写：宽 = 显示文本 + 卡片内边距，高 = 字体行高 + 内距 + 底部进度条。

- override void OnPaint(App app)
  - 覆写：驱动计时并画倒计时卡片（圆角面板 + 数字 + 剩余进度条），
    走动时按显示精度限速申请局部动画帧。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（时长 / 启停 / 格式 / 类）。

- override List<string> Events()
  - 设计器事件列表（公共事件 + Finish）。

- override void BindEvent(string evt, Action a)
  - 设计器/反射绑定：按事件名挂回调。


## DatePicker (class)

日期编辑控件：可键盘编辑的取值输入框，右侧带日历按钮。
弹层在覆盖层中绘制，取值通过 OnChange 报告。
Type 字段控制变体（Naive UI type prop）：
"date"          — 默认，单个日期（YYYY-MM-DD）
"datetime"      — 含时分；弹层底部 HH:MM 数字区点击递增
"daterange"     — 起止日期对，弹层为左右双月历（左=选中月，
右=下个月），点起始再点结束，悬停实时预览区间
"datetimerange" — 双月历 + 各面板底部一组 HH:MM（起始时间复用
hour/minute 字段，结束时间为 endHour/endMinute）
"week"          — 整周选择：点任意一天选中其所在周（周一~周日），
文本 "周一 ~ 周日"
"month"         — 12 宫格选月，文本 YYYY-MM（取值=当月 1 日）
"monthrange"    — 双面板选月区间（右面板 = 下一年）
"quarter"       — Q1-Q4 选季度，文本 YYYY-Qn（取值=季首月 1 日）
"quarterrange"  — 双面板选季度区间
"year"          — 12 年一页选年份，取值=当年 1 月 1 日
"yearrange"     — 双面板选年份区间（右面板 = 下一个 12 年页）

- static DatePicker current;
  - 最近交互的实例（Input.Change 不带宿主，静态转发经它找实例）。

- Input editor;
  - 取值输入框（键盘编辑路径）。

- Button trigger;
  - 右侧日历按钮（点击开合弹层）。

- UiEvent Change;
  - 取值变化后触发（弹层点选、键盘编辑出合法文本）。

- bool open;
  - 弹层展开状态。

- int viewYear;
  - 弹层左面板当前显示的年月。

- int viewMonth;

- DateTime selected;
  - 当前取值：单值模式的选中值；range 模式的结束值。

- DateTime pendingStart;
  - range 模式下第一个点击（pending）；第二次点击后才关闭弹层。
    week 模式下一次点击即同时写入起（周一）止（周日）。

- bool hasPending;
  - range/week 模式下是否已有有效区间（pendingStart..selected）。

- bool pickingEnd;
  - 已点起始、等待第二个点击。重新打开弹层后复位，
    让"再点一次"总是开启一条新区间而不是续接旧区间。

- bool hasHover;
  - 本帧悬停到的取值（仅弹层绘制期间有效），用于区间预览。

- DateTime hoverDate;

- int hour;
  - datetime 的时分；datetimerange 里为起始时分。

- int minute;

- int endHour;
  - datetimerange 的结束时间；起始时间复用 hour/minute。

- int endMinute;

- int wid;
  - 组件命中 id（构造时 WidgetId.Next() 分配）。

- string Type;
  - Naive UI type prop：date | datetime | daterange | datetimerange |
    week | month | monthrange | quarter | quarterrange | year | yearrange。

- Binding<string> Size;
  - Naive UI size prop：small | medium | large。Binding<string>
    映射为 input.small/large 档位 class，支持响应式换档。

- DatePicker()
  - 构造：内置编辑框与日历按钮，默认取今天。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"input"，与输入框共用皮肤规则）。

- override Binding<string> SizeOf()
  - 覆写：返回 Size 绑定字段（设计器经它发现尺寸档位绑定）。

- void ApplySizeClass()
  - Size prop 值映射为 input.small/large 档位 class（与 Input
    同一套 CSS 规则）；测量与渲染前临时并入 Class。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（日期文本 / 类型 / 尺寸档）。

- override string GetExtra(string key)
  - 覆写：应答扩展属性键（日期文本与 type）。

- override bool SetExtra(string key, string val)
  - 覆写：写回扩展属性键（文本经 SetText 解析，type 合法值切换粒度）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"。

- override void BindEvent(string evt, Action a)
  - 覆写："Change" 挂 Change 事件，其余按名称走通用路由。

- DatePicker OnChange(Action a)
  - 挂接 Change 回调（链式）。

- string Text()
  - 当前编辑框文本（各 Type 的格式见类文档）。

- DateTime GetDate()
  - 当前选中值（range 模式为结束值）。

- DateTime GetStart()
  - range 模式下的起始值；非 range 模式时 == GetDate()。

- DateTime GetEnd()
  - range 模式下的结束值；非 range 模式时 == GetDate()。

- bool IsRange()
  - 是否为区间/周选择变体（取值为起止对）。

- bool IsDateTime()
  - 是否为 datetime 变体（单值 + 时分）。

- bool HasTime()
  - 是否带时分输入行（datetime / datetimerange）。

- string Granularity()
  - 面板粒度：day（date/datetime/week/daterange/datetimerange）、
    month、quarter、year。month/quarter/year 复用同一套宫格绘制。

- void SetDate(DateTime value)
  - 程序化赋值：同步视图年月与时分并刷新编辑框文本（不触发 Change）。

- void SetText(string value)
  - 程序化按文本赋值：与键盘编辑同一解析路径（非法文本忽略）。

- string FormatText()
  - 格式化当前值为编辑框文本，遵循 Type 变体。

- string FormatValue(DateTime d)
  - 单个取值的文本形式（不含时间与范围分隔符）。

- static int Digit(string s)
  - 单个数字字符 → 0..9（非法 -1）。

- static int Number(string s, int at, int count)
  - 从 s[at] 起取 count 位数字；任一位非法返回 -1。

- static DateTime ParseDate(string value)
  - "YYYY-MM-DD" 严格解析（位数 / 分隔符 / 月日范围全查）；失败 null。

- DateTime ParseSingle(string raw)
  - 按 Granularity 解析单值文本：month "YYYY-MM"、
    quarter "YYYY-Qn"、year "YYYY"，day 走 ParseDate。

- DateTime ParseTime(string raw, bool start)
  - "YYYY-MM-DD[ HH:MM]" 解析；start=true 把时分写进 hour/minute
    （起始时间），否则写进 endHour/endMinute（结束时间）。

- void Edited()
  - 编辑框文本变化的统一收口：按 Type 解析并写回取值（起止颠倒
    自动交换），合法且变化才触发 Change。

- static void TriggerClicked()
  - 日历按钮的静态转发（经 current 找到实例后开合弹层）。

- static void Use(DatePicker value)
  - 归属切换：悬停新实例或弹层开着时转移 current，
    并关闭前一个实例仍开着的弹层。

- void ToggleOpen()
  - 开合弹层；打开时把视图对到当前区间起始（range）或
    解析编辑框文本（单值）。

- void MoveMonth(int delta)
  - 左面板月份翻页（跨年自动进退）。

- void MoveView(int delta)
  - 头部箭头的翻页步进：day=±1 个月，month/quarter=±1 年，
    year=±1 页（12 年）。

- int PageStart(int year)
  - year 粒度页起始（12 年一页，页内年=起..起+11）。

- string TitleText(int year, int month)
  - 面板头部标题：day="YYYY-MM"、month="YYYY年"、quarter="YYYY"、
    year="起 - 止"。

- DateTime WeekStart(DateTime d)
  - picked 所在周的周一（周一起始；DayOfWeek() 0=周日）。

- void PickDay(int day)
  - 点击左面板第 day 天（按当前视图年月构造取值后走 PickDate）。

- void PickDate(DateTime picked)
  - 取值入口：week 一次点击选整周；range 两次点击成区间
    （结束 < 起始自动交换）；单值直接落定。落定后写编辑框、
    关弹层并触发 Change。

- override void OnPaint(App app)
  - 覆写：轮询内嵌编辑框、归属判定（Ui.Over，弹层压住邻居时不抢焦点），
    展开时把日历弹层推迟到覆盖阶段绘制。

- override void OnMeasure(App app)
  - 覆写：按尺寸档解析 input 样式给宽高，并保证装下回显文本 + 触发按钮。

- override void Arrange(int px, int py, int pw, int ph)
  - 覆写：把空间分成编辑框与右侧触发按钮（按钮宽 34）。

- int appScale(int n)
  - 保留式布局下的缩放占位（当前恒等返回）。

- override void OnPaintOverlay(App app)
  - 覆写：展开且归属本实例时绘制日历/时间弹层（Naive UI 各 type 面板）。

- DateTime HoverDay(App app, int panelX, int top, int pad, int cellW, int cellH, int year, int month)
  - 扫描一个月的日期网格，返回鼠标悬停到的日期（无则 null）。
    仅供区间预览在绘制前整帧调用一次。

- DateTime HoverCell(App app, int panelX, int top, int pad, int cellW, int cellH, int year)
  - 扫描 month/quarter/year 宫格，返回悬停格子的取值（无则 null）。

- string CellLabel(string gran, int idx, int year)
  - month/quarter/year 宫格里第 idx 个格子的文字。

- DateTime CellValue(string gran, int idx, int year)
  - month/quarter/year 宫格里第 idx 个格子的取值
    （当月 1 日 / 季首月 1 日 / 当年 1 月 1 日）。

- bool PaintMonth(App app, Canvas c, StyleBox pop, int panelX, int top, int pad, int cellW, int cellH, int fs, int fg, int year, int month, bool clicked)
  - 绘制一个月的星期条与日期网格并处理日期点击。
    返回 true 表示本次点击命中了日期格，调用方应立即结束本次
    覆盖绘制，不再做弹层外部关闭判定。

- bool PaintCellGrid(App app, Canvas c, StyleBox pop, int panelX, int top, int pad, int cellW, int cellH, int fs, int fg, int year, bool clicked)
  - 绘制 month/quarter/year 选择宫格并处理点击。
    year 传页起始（PageStart）；返回 true 表示点击命中了格子。

- int PaintTimeGroup(App app, Canvas c, StyleBox pop, int gx, int timeY, int rowH, int fs, int h, int mi, bool clicked)
  - 绘制一组 "HH:MM" 数字（三段按文本宽度排布，整体居中于
    gx 起的槽内）；点击命中返回 1=小时、2=分钟，由调用方对
    相应字段做递增循环。


## Divider (class)

分隔线，可含标题。颜色、粗细和字体
来自 `divider` CSS 规则（`background` 为线条，`height` 为
粗细），因此换肤可一次性重设所有分隔线。

Divider d = new Divider { Text = "Section", Class = "left" };

类名：`left` / `right` 调整标题位置（默认居中），`vertical`
绘制竖向分隔线。

- Binding<string> Text;
  - 分隔线标题（可绑定）；空串 = 纯线条。

- void InitDivider(string title)
  - 初始化：标题（可绑定）；表面自绘，只画中线一条线。

- Divider()
  - 无标题分隔线（设计器用）。

- Divider(string title)
  - 带标题分隔线。

- string Label()
  - 标题文本（空绑定返回空串）。

- override void OnMeasure(App app)
  - 覆写：无标题时高度取 medium 间距，有标题时取字体高；宽 200。

- override void OnPaint(App app)
  - 覆写：画中线（`vertical` 时竖向）；带标题时线条让位于文字，
    标题可居中 / 居左 / 居右。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（标题 / 类）。


## Dropdown (class)

Dropdown：触发器下方弹出动作菜单的控件。

与 SelectBox 分工：SelectBox 是「从列表里选一个值」；Dropdown
是「点开菜单执行一个动作」。两种数据形态：

1. 命令菜单（MenuItem）——图标、快捷键、分隔线、分组标题、
禁用/危险项和一层悬停展开的多级子菜单；叶子点击后其
`action` 写入并触发 Change，触发器不保留选中值：

menu.BindMenu(new List<MenuItem> {
MenuItem.Item("Share", "share", 0),
MenuItem.Separator(),
MenuItem.Danger("Delete", "trash", 1),
MenuItem.Submenu("Export", "download", kids),
});
menu.OnChange(OnAct);       // menu.Action() 是命中的 action

2. 实体列表（与 SelectBox 的数据形态一致）——条目是普通实体，
`textOf` 取显示文本，选中索引语义与 SelectBox 相同，适合
简单的「选一个再执行」场景：

Dropdown<City> pick = new Dropdown<City>(c => c.name);
pick.Bind(cities);
pick.OnChange(OnCity);      // pick.Selected() 是所选实体
parent.Add(pick);           // 尺寸和位置由布局决定

弹层是共享的富菜单组件（OverlayPopup.RichMenu）：延迟到框架
覆盖层绘制，总是显示在后续内容之上，拦截穿透点击，外部点击
或 Esc 关闭；长菜单出滚动条，滚轮与拖动都可用。

- List<MenuItem> menuItems;
  - 命令菜单条目（BindMenu 模式）；flat 模式为 null。

- List<T> data;
  - 实体数据源（flat 模式）。每帧重新读取。

- CellOf<T> textOf;
  - 从实体取显示文本（flat 模式，Label 可换口径）。

- string hint;
  - 未选中任何条目时触发器显示的提示文本。

- T selItem;
  - 选中实体（flat 模式选择后存储；仅在选择发生时写入）。

- int selIndex;
  - flat 模式选中行号（-1 = 无）。

- int lastAction;
  - 最近一次选择的 action id（菜单模式）。

- int wid;
  - 触发器命中 id。

- int trigX;
  - 每帧暂存触发器几何信息，使延迟面板（在覆盖
    层阶段绘制）能定位到触发器下方；面板宽度也不窄于触发器。

- int trigY;

- int trigW;

- int trigH;

- int maxRows;
  - 面板在滚动前显示的可点击行数（Rows(n)）。

- bool wasOpen;
  - 上一帧的打开状态：RenderRich 关闭菜单只写 menuOpen 信号，
    这里的跳变沿负责补发 Opened/Closed 与选中结算。

- SignalBool menuOpen;
  - 富菜单的信号组：结果 / 子菜单展开 / 滚动偏移都由宿主跨帧
    持有（弹窗每帧重建）。

- SignalInt menuResult;

- SignalInt menuSub;

- SignalInt menuScroll;

- int menuBase;
  - 覆盖层命中 id 保留段（RenderRich 要求至少 256 个连续 id）。

- UiEvent Change;
  - 选中不同条目 / 命中命令时触发。

- UiEvent Opened;
  - 面板打开后触发。

- UiEvent Closed;
  - 面板关闭后触发（选中结算在 Change）。

- void InitDropdown()
  - 公共初始化（构造共用）。

- Dropdown(CellOf<T> text)
  - 文本条目：`new Dropdown<City>(c => c.name)`。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"select"，与 SelectBox 共用皮肤）。

- Dropdown<T> Bind(List<T> src)
  - 绑定实体数据源；列表每帧重新读取。选择被清除。

- Dropdown<T> BindMenu(List<MenuItem> items)
  - 绑定命令菜单（MenuItem 树）。命中的叶子 action 在 Change
    之后可经 `Action()` 读取；触发器不保留选中值，回到提示语。

- Dropdown<T> OnChange(Action a)
  - 订阅选中/命令命中（链式）。

- Dropdown<T> OnOpen(Action a)
  - 订阅面板打开（链式）。

- Dropdown<T> OnClose(Action a)
  - 订阅面板关闭（链式）。

- Dropdown<T> Hint(string text)
  - 未选中任何条目时显示的提示文本。

- Dropdown<T> Label(CellOf<T> text)
  - flat 模式选择后的触发器文本：默认用 `textOf`，绑定后仍可
    用 Label 换一个取文本的口径。

- Dropdown<T> Rows(int n)
  - 面板在滚动前显示的可点击行数。

- int Count()
  - 条目数（菜单模式为菜单项数，flat 模式为数据行数）。

- int SelectedIndex()
  - flat 模式选中行号（-1 = 无）。

- bool HasSelection()
  - 是否有选中条目。

- T Selected()
  - 选中的实体（仅 flat 模式选择后有效）。

- int Action()
  - 菜单模式：最近一次命中的叶子 action；无则为 -1。

- void SelectIndex(int row)
  - 程序化选中一行（不触发 Change、不弹面板）。

- void Clear()
  - 清除选择：触发器回到提示语（动作菜单语义）。

- bool IsOpen()
  - 面板当前是否打开。

- void SetOpen(bool want)
  - 程序化开关面板；打开时重置滚动、子菜单与结果。
    打开/关闭事件经 OnPaint 的跳变沿统一补发。

- void Open()
  - 打开面板。

- void Close()
  - 关闭面板。

- string TriggerText()
  - 触发器标签：flat 模式所选条目的文本，否则为提示文本
    （命令菜单不把选中值留在触发器上）。

- override void OnMeasure(App app)
  - 覆写：触发器默认一行高、160 逻辑像素宽。

- void PaintTrigger(App app)
  - 触发器走皮肤解析出的 `select` 框，而不是硬写主题色：
    硬写的边框在任何皮肤上都是同一道亮边（嵌在深色面板头部
    里就是一圈白边），样式表也关不掉它。

- override void OnPaint(App app)
  - 覆写：画触发器并处理点击/键盘开合与打开关闭跳变沿
    （菜单本体经帧末覆盖层绘制）。

- List<MenuItem> SyncItems(App app)
  - flat 模式把实体列表转成菜单条目（action = 行号）。每帧重建：
    数据是调用方的，绑定后原地修改也要在下一帧生效。

- override void OnPaintOverlay(App app)
  - 延迟面板：交给共享富菜单（图标/分隔线/子菜单/滚动条）在
    覆盖层阶段绘制与分发。面板不窄于触发器。

- override List<string> Events()
  - 设计器事件列表：通用事件之外提供 Change/Opened/Closed。

- override void BindEvent(string evt, Action a)
  - 把下拉框的语义事件（Change/Opened/Closed）路由到对应
    UiEvent 字段；其余回落到 `On` 上的通用事件包
    （其 AddByName 忽略未知名称）。


## DropdownChrome (class)

在按钮下方挂面板的控件们共用的触发器按钮外观
（Dropdown、Popover、属性编辑器）。

- static void PaintTrigger(App app, int id, int x, int y, int w, int h, string label, bool open)
  - 用给定 id 绘制触发器按钮并注册其点击区域。


## DynamicTags (class)

动态标签：一排可增删的 Tag，尾部带一个「+ 新建标签」触发器，
点击后原位变成一个输入框（Naive UI 的 n-dynamic-tags）：

DynamicTags tags = new DynamicTags();
tags.SetItems(new List<string>{ "调查中", "已发布" });
tags.Change += () => { Save(tags.Items()); };
tags.Render(app, 40, 80);          // 或作为停靠控件放进容器

交互与 Naive UI 对齐：点触发器打开输入框；回车或点击别处（失焦）
提交，文本非空才追加；点标签尾部的 x 移除该条。Closable 默认
true，Max 限制数量、达到后触发器置灰且不再打开输入框（Esc 关闭
输入框且不追加，是 Naive 没有的桌面补充）。

事件在改动发生处同步抛出（与 Tabs 的 TabClosed 同款）：移除在
标签循环内抛出后立即结束本帧绘制，输入框提交在循环外抛出，
处理器里改标签集合是安全的。

可识别的类只是皮肤定义的选择器（与 Tag 共用一套，作用于整排
标签；尾部触发器固定追加 `add` 类，皮肤用 `tag.add` 画虚线框）：

tag                                        默认
.primary .info .success .warning .error     语义类型
.tiny .small .medium .large                 尺寸（Size 字段）
tag.add                                     尾部触发器

- List<string> items;
  - 标签文本，按显示顺序。整体重置走 SetItems；单个增删走
    Add/RemoveAt（两者都会触发 Change）。

- bool Closable;
  - 每个标签带尾部关闭字形，点击移除（Naive UI closable，
    动态标签下默认 true）。

- int Max;
  - 标签数量上限；0 = 不限（Naive UI max）。达到后触发器置灰
    且不再打开输入框；程序化 Add 不受限。

- string AddText;
  - 触发器文案；"" = 跟随界面语言（「新建标签」）。

- string Placeholder;
  - 输入框占位文字；"" = 无。

- Binding<string> Size;
  - 尺寸档（tiny/small/medium/large），映射为 tag 皮肤类；
    `Binding<string>` 使 `tags.Size = vm.density;` 可响应式换档。

- UiEvent Change;
  - 标签集合被改动时触发（`tags.Change += h;`）。SetItems/Clear
    是宿主驱动的整体同步，不触发。

- int tagBase;
  - 每个标签一对命中 id（整体 tagBase+i、关闭字形
    tagBase+512+i），从预留段按下标分配，标签增删不会挪动其他
    控件的 id。上限 512 个标签，与 Tabs 的预留段同款。

- int triggerId;

- Input editor;
  - 内联输入框（保留实例，不挂进控件树）：点触发器后原位渲染
    并接管键盘，光标、选区、IME 组合窗都由它处理。

- bool editing;
  - 输入框开着时为 true；占据触发器的位置。

- bool takeFocus;
  - 打开输入框的这一帧在其 Render 返回后把焦点交给它。

- bool editorFocused;
  - 上一帧输入框是否持有焦点：由有到无即失焦，按 Naive 的
    handleInputBlur 语义提交。

- static string lang="zh";
  - 内置触发器文案的 UI 语言码（"zh" / "en"），与
    Tabs 相同：不设语言的发布版界面本身是中文的。

- static string TT(string en, string zh)
  - 内置文案走键式查找（System.Globalization.Lang）：英文原文
    作键、内置中文为缺省串；语言包未加载时回退缺省串。

- void InitDynamicTags()
  - 初始化：置默认字段（可关闭、无上限、中档尺寸）、预留 id 段
    与内置输入框。

- DynamicTags()
  - 空标签集合（设计器用）。

- DynamicTags(List<string> initial)
  - 给定初值的构造；列表被拷贝，之后的结构改动走 Add/RemoveAt。

- int Count()
  - 标签数量。

- string TextAt(int i)
  - 第 i 个标签文本；越界返回空串。

- List<string> Items()
  - 活动列表。读随意；结构改动请走 Add/RemoveAt/Clear，让
    订阅者收到 Change。

- void Add(string text)
  - 追加一个标签并触发 Change。不受 Max 限制（那是给交互
    触发器的闸门，拦不住宿主自己的数据）。

- void RemoveAt(int i)
  - 移除第 i 个标签并触发 Change；越界为空操作。

- void SetItems(List<string> src)
  - 以 `src` 的内容重置标签（拷贝）。整体同步不触发 Change，
    并会关掉开着的输入框——列表都换了，编辑中的草稿没有意义。

- void Clear()
  - 清空全部标签；与 SetItems 同为整体同步：不触发 Change，
    并关掉开着的输入框。

- bool CanAdd()
  - 触发器还允许再添加吗（未设 Max 或未达上限）。

- string AddLabel()
  - 触发器文案。

- void StartEdit()
  - 打开输入框（占据触发器位置）。达到 Max 或整控件禁用时
    是空操作。

- void FinishEdit(App app, bool add)
  - 关闭输入框；`add` 为真且文本非空时先追加（回车与失焦），
    否则丢弃草稿（Esc）。

- int TriggerWidth(App app, string cls)
  - 触发器自然宽度：加号字形 + 文案 + 标签内边距。

- int RowWidth(App app, string cls)
  - 整行自然宽度：所有标签 + 间距 + 触发器（输入框开着时它
    原位让给输入框，宽度按触发器计，测量阶段编辑未开）。

- int Render(App app, int x, int y)
  - 绘制整行；返回触发器 id（无它用时可忽略）。行内所有点击
    都在本方法里就地处理：关 x 移除、触发器开编辑框。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override Binding<string> SizeOf()
  - 覆写：返回 Size 绑定字段（设计器经它发现尺寸档位绑定）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（closable/max/addText/placeholder/
    size/class）。

- override string GetExtra(string key)
  - 标签集合按 `|` 分隔的原样文本回读（Tabs.SetItemsText 同款），
    供设计器与 .zform 序列化。

- override bool SetExtra(string key, string val)
  - 设计器写入 `options`（`|` 分隔标签文本），整体重建标签；
    其他键返回 false 交给基类。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"。

- override void BindEvent(string evt, Action a)
  - 覆写："Change" 挂 Change 事件，其余按名称走通用路由。

- override void OnMeasure(App app)
  - 覆写：按尺寸档映射 Class 后量取行高与自然行宽。

- override void OnPaint(App app)
  - 覆写：在停靠边界内垂直居中绘制标签行与添加触发器/输入框，
    裁在本控件边界内。


## Ellipsis (class)

省略号：单行文本在将超出给定宽度时
截断并在末尾加 "..."。

- static string Fit(string text, int w, int fs)
  - 字体大小 `fs` 下若 `text` 能在 `w` 像素内放下则原样返回，
    否则返回放得下的最长前缀加尾部 "..."。纯字符串
    辅助函数，任何调用方都可用自己的颜色/位置绘制结果。

- static void Render(App app, int x, int y, int w, string text)
  - 在 (x, y) 用 `ellipsis` 样式绘制 `text`：放得下原样画，
    放不下时截断加尾部 "..."（同 Fit 的算法）。


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
  - 大图形字形名（默认 "folder"）。

- void InitEmpty(string msg)
  - 初始化：标题文本（可绑定），图形默认 "folder"。

- Empty()
  - 空标题（设计器用）。

- Empty(string msg)
  - 给定标题文本。

- string Label()
  - 当前标题（通过绑定解析）。

- override void OnMeasure(App app)
  - 覆写：首选尺寸取 `empty` 样式宽高（默认 240 x 120）。

- override void OnPaint(App app)
  - 覆写：垂直居中画大图形与标题行（样式部件 empty::icon / ::label）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（标题 / 图形 / 类）。


## FbFlow (class)

24 列流式容器（设计器/运行时/FormBuilder 共用的唯一布局原语）。
把子节点按各自 span（1..24）贪心打包成多行：一行累计 span 超过 24
就换行；每个单元宽度按 span/24 比例分配（与设计器 LayoutFlowList
完全一致：按 24 列而非行内总和分配，列间留 colGap，行间留 rowGap），
行高取该行子节点 prefH 的最大值。无表面。

- List<int> spans;
  - 与 children 平行的列宽表（span 1..24）。

- int colGap;
  - 逻辑像素（100% 下的样子）。

- int rowGap;

- int minCell;
  - 单元最小宽度（逻辑像素，过窄时兜底）。

- int mColGap;
  - OnMeasure 按当前缩放解析出的实际间距，供拿不到 App 的
    Arrange 复用 —— 直接用逻辑值排布，150% 下列间距会偏窄、
    行距不足，字段互相贴住。

- int mRowGap;

- int mMinCell;

- FbFlow()
  - 构造：列距 12 / 行距 6 / 最小单元 24（逻辑像素）。

- void AddCell(Control c, int span)
  - 追加一个单元及其列宽（span，1..24）。

- int SpanAt(int i)
  - 第 i 单元的列宽（钳制 1..24；越界按 24）。

- int RowEnd(int start)
  - 行结束下标（不含）：从 start 起贪心累计到超过 24 列。

- int RowHeight(int start, int end)
  - [start,end) 行高 = 行内子节点 prefH 的最大值。

- override void OnPaint(App app)
  - 覆写：不画任何表面（画布容器，视觉全由子控件承担）。

- override void OnMeasure(App app)
  - 覆写：按 24 栅格分行的行高累计出首选高（宽随宿主）。

- override void Arrange(int px, int py, int pw, int ph)
  - 覆写：按每项 span 把 24 栅格宽折算成单元格矩形逐行摆放。


## FbStack (class)

竖直单元：标签在上、控件填充其余。用于带 caption 的流式字段，
无表面。

- FbStack()
  - 构造：以 "fbstack" 注册为透明布局面板（标签在上、控件填充）。

- override void OnPaint(App app)
  - 覆写：不画任何表面（无表面竖直单元）。

- override void OnMeasure(App app)
  - 覆写：子项测量高累计、宽取最宽者（宽随宿主）。

- override void Arrange(int px, int py, int pw, int ph)
  - 覆写：子项自上而下按测量高堆叠，dock-fill（dock==5）的
    子项吃掉剩余高度。


## Flex (class)

弹性布局容器（Naive UI n-flex / n-space）：把子控件排成一行或
一列，可换行，间距、对齐、主轴分布全部由 CSS 决定。

这是编排界面的默认容器——绝大多数排版需求（一排按钮、
一列表单、标题行左右分置、一堆标签自动换行）都不该再手算
坐标，交给它即可：

Flex row = Flex.Row().Gap(app.theme.gapMedium);
row.With(new Button("保存") { Class = "primary" });
row.With(new Button("取消"));
row.RenderInside(app, area);

一排会溢出的标签自动折行（对应 n-space 的默认行为）：

Flex tags = Flex.Row().Wrap();
for (int i = 0; i < names.Count; i = i + 1) {
tags.With(new Tag(names[i]));
}

标题行：左侧标题占满剩余空间，右侧动作贴右——不需要
任何像素值，靠 `justify-content: space-between`：

Flex head = Flex.Row().Between().AlignCenter();
head.With(new Label("详情") { Class = "title" });
head.With(new Button("编辑"));

容器自身按内容测量（换行时按行数累加高度），因此可以
干净地嵌套：Flex 里放 Flex，外层的高度会自动跟上。

外观完全没有：Flex 不画背景也不画边框（base.css 里
`flex { background: transparent; display: flex; }`）。要一张
卡片就把它放进 Card/Panel，或者给它挂类由皮肤上色。

所有变体都落在 CSS 类上，皮肤可以整体改写：

flex                            默认：行、不换行、start 对齐
.column                         主轴改为纵向
.wrap / .nowrap                 是否换行
.justify-center .justify-end
.justify-between .justify-around  主轴分布
.align-start .align-center
.align-end .align-stretch       交叉轴对齐
.content-center .content-between
.content-stretch …              换行后各行的交叉轴分布
.gap-none .gap-small
.gap-medium .gap-large          间距档（取 --gap-* token）

子项侧的 CSS 也照常可用（写在子控件的 Class 上）：`grow` 吃掉
剩余空间、`shrink-0` 溢出时不让位、`flex-1` 一排等宽、
`self-center` 单独跳出容器的对齐、`aspect-video` 按比例定高。

- static string DirGroup()
  - 互斥变体组：换一个方向/对齐方式时先清掉同组的旧类，
    否则生效的是样式表里靠后那条规则。

- static string WrapGroup()
  - 互斥类组：换行档。

- static string JustifyGroup()
  - 互斥类组：主轴分布档。

- static string ContentGroup()
  - 互斥类组：换行后各行在交叉轴的分布档。

- static string AlignGroup()
  - 互斥类组：交叉轴对齐档。

- static string GapGroup()
  - 互斥类组：间距档。

- void InitFlex(string dirCls)
  - 初始化：以 "flex" 注册为 dock-fill 排版根，并挂上主轴方向类。

- Flex()
  - Default design-time constructor：横向、不换行。

- static Flex Row()
  - 横向排列（n-flex 默认）。

- static Flex Column()
  - 纵向排列（n-flex 的 `vertical`）。

- static Flex Wrapped()
  - 横向 + 自动换行（等价于 n-space 的默认行为）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）："Flex"。

- override string StyleType()
  - 皮肤样式类型键："flex"。

- Flex Vertical()
  - 改变主轴方向为纵向（等价 Row/Column 工厂的反向切换）。

- Flex Horizontal()
  - 改变主轴方向为横向。

- Flex Wrap()
  - 开启换行：一行放不下就折到下一行，行距取 `row-gap`
    （未声明时回退到 `gap`）。

- Flex NoWrap()
  - 关闭换行（显式落 nowrap 类，与 Wrap 互斥）。

- Flex Justify(string mode)
  - 主轴分布：`start`/`center`/`end`/`between`/`around`
    （对应 CSS justify-content）。

- Flex Center()
  - 子项贴主轴中线。

- Flex End()
  - 子项贴主轴末端（一行按钮靠右）。

- Flex Between()
  - 首尾贴边、剩余空间平分到中间（标题行左右分置）。

- Flex Align(string mode)
  - 交叉轴对齐：`stretch`/`start`/`center`/`end`
    （对应 CSS align-items）。

- Flex AlignCenter()
  - 一行里高矮不齐的子项垂直居中——按钮和标签混排时几乎
    总是要这个，否则矮的那个会被拉到整行高。

- Flex AlignStart()
  - 子项贴交叉轴起点（顶部对齐）。

- Flex AlignEnd()
  - 子项贴交叉轴终点（底部对齐）。

- Flex Content(string mode)
  - 换行之后各行在交叉轴上怎么分布：`start`/`center`/`end`/
    `between`/`around`/`stretch`（对应 CSS align-content）。只有
    Wrap() 的容器且交叉轴有多余空间时才看得出区别。

- Flex Size(string size)
  - 间距档：`none`/`tiny`/`small`/`medium`/`large`，取
    base.css 的 --gap-* token（对应 n-space 的 size prop）。
    需要具体像素时用基类的 Gap(px)。

- override bool SetExtra(string key, string val)
  - 设计器序列化应答：`direction`/`wrap`/`justify`/`align`/`size`
    五个文档键映射到对应变体方法；未知键返回 false 交给基类。

- override string GetExtra(string key)
  - 设计器读取当前变体状态：`direction` 返回 row/column，
    `wrap` 返回 true/false；其余键返回 ""。


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
  - 初始化：置图标并自管 Click 事件（直径默认随主题）。

- FloatButton()
  - 无图标（之后设置 Icon）。

- FloatButton(string icon)
  - 指定图形的浮动按钮。

- int Diameter(App app)
  - 实际直径：Size > 0 时取 Size，否则主题大控件高度。

- int Render(App app, int x, int y)
  - 在 (x, y) 绘制并返回控件 id，宿主可用
    `Ui.Clicked` 检测点击，而无需订阅 `Click`。

- override void OnMeasure(App app)
  - 覆写：宽高都取直径（正方形占位）。

- override void OnPaint(App app)
  - 覆写：画阴影、圆形主体与图标，登记命中并处理点击。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（icon/size/class）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Click"。

- override void BindEvent(string evt, Action a)
  - 覆写："Click" 挂 Click 事件，其余按名称走通用路由。


## FormBuilder (class)

统一的 .zform -> 真控件树构建器。设计器预览与运行时窗口
共用这一套：把设计文档（Designer.SaveJson / formgen 消费的同一 JSON）
实例化为真正的 Control 树，按 24 列流式或自由画布布局排布。

设计器把它渲染在编辑外框内并叠加选中/handle 覆盖层；运行时把
同一棵树放进 Form。因此“所见即所得”天然成立，自定义组件也走
同一条实例化路径（HeavyControls 注册工厂 / 注入工厂）。

Canonical kind, layout metadata, and property setup are shared with
formgen through the Control contract. Every setup is applied to the
concrete local Control variable, avoiding downcasts.

- static Control Build(JsonValue root)
  - 把设计文档根对象构建为内容根 Control（不含窗口外框）。

- static string Kind(JsonValue o)
  - 字段的规范 kind：设计文档 `kind` 键优先，旧文档回退 LegacyKind(type)。

- static string LegacyKind(int t)
  - One-time read compatibility for old designer documents. New writers
    never emit this key.

- static bool IsContainer(JsonValue o)
  - 设计对象是否带 kids 数组（即容器）。

- static bool OwnsLabel(Control ctl)
  - 控件是否自带 `label` 属性（Checkbox / Radio 那样把标题画在框
    旁边）。自带的交给控件，不自带的由表单在它上面补一行标题。

- static bool NeedsCaption(JsonValue o, Control ctl)
  - 字段需不需要表单替它画标题。以前这里是一张写死的白名单
    （Input / TextArea / SelectBox），于是 Switch、Slider、Rate 这些
    字段的 label 既没进控件、也没画成标题，直接丢了；Checkbox 和
    Radio 明明自带 label 属性也拿不到值。改成问控件自己有没有
    label：没有就由表单补标题，两边都不会再漏。

- static int PrefH(JsonValue o)
  - 字段高度（设计文档 fh；缺省 / 非法 32）。

- static int DefaultDock(JsonValue o)
  - 工具条 / 状态栏的默认停靠边（Top / Bottom），其余 Manual。

- static int ClampSpan(int s)
  - span 钳制到 [1,24]。

- static string Caption(JsonValue o)
  - 标题文案：label 优先，为空回退 name。

- static int ChildCount(JsonValue o)
  - kids 数组长度（非容器 0）。

- static string OptAt(JsonValue o, int i, string def)
  - 第 i 个选项文本（缺失 / 非字符串回退 def）。

- static int OptInt(JsonValue o, int i, int def)
  - 第 i 个选项解析为整数（缺失 / 非数字回退 def）。

- static bool IsNumber(string s)
  - 是否为（可选负号前缀的）纯整数字符串。

- static string JoinOpts(JsonValue o)
  - options 数组 → "a|b|c" 文本（空数组返回 ""）。

- static Control MakeControl(JsonValue o, bool free)
  - 按 kind 经共享注册表实例化真控件，并套用设计文档的
    name / label / class / options / placeholder / defOn / columns 等属性。

- static string GridColumnsSpec(JsonValue o)
  - DataGrid 的 columns 对象数组压成列头预览串（"宽,标题|…"）。
    运行时建树拿不到 "of" 的实体类型，预览只铺列头；类型化取值
    由编译期 GenForm 生成。

- static void FillKids(Control p, JsonValue o, bool free)
  - 把 Panel 型容器（card/tabs/dockpanel）的 kids 填入。

- static void BuildFlow(Control parent, JsonValue arr)
  - 24 列流式布局：逐字段 BuildCell 后按 span（容器恒 24）加入 FbFlow。

- static Control BuildCell(JsonValue o)
  - 单个流式字段 → 控件：需要标题时包 FbStack（标签在上），
    选项组单独设行高，其余按 fh 钉高。

- static void BuildFlowOne(Control pane, JsonValue kid)
  - 把单个字段（分栏面板某个 pane 的直接子节点）按流式加入。

- static void BuildFree(Control parent, JsonValue o)
  - 自由画布：按 dock 边停靠，否则 Place 绝对定位 + Prefer 尺寸；
    容器递归 FillKids。

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
  - Canonical serialized component class. The legacy ftype is retained only
    for reading old designer documents and palette metadata.

- int ftype;

- string label;
  - 字段旁显示的标签文字。

- string fname;
  - 模型键：运行时绑定与提交数据用的字段名。

- string placeholder;
  - 占位文本（ftype 72 时为起始 URL）。

- string customKind;
  - Project component class name. A non-empty value marks a custom kind
    and is serialized directly so JSON round-trips preserve the class.

- bool compRef;
  - 该字段是一个「已保存用户组件」的引用节点（而非项目源码
    类）：.zform 里写成 {"kind":名,"ref":名,...}，组件设计
    的每次进化自动跟随所有实例；GenForm 编译期按 ref 展开
    组件文档，画布上按组件文档做真控件预览。

- bool pvPreview;
  - 引用节点的画布真控件预览开关（false = 显示占位块，
    序列化为 "pvOff": true）。

- bool required;
  - 是否必填（提交校验标记）。

- bool defOn;
  - switch 字段的默认开启状态（ftype 7）。

- bool wrap;
  - 显示文本（ftype 15/16）的自动换行：渲染的 Label 换行适应
    字段宽度而非省略截断。序列化以便自由布局携带
    较长的提示段落。

- int span;
  - 24 分栏中的列跨度（24 = 整行，12 = 半行，……）。

- int fx;
  - 自由画布布局（designer layoutMode == 1）：设计空间中的绝对
    位置和尺寸（窗口像素）。容器子元素的坐标
    相对于容器的内容框（对应运行时的 DockManual + Place
    ）。流式模式下忽略，此时宽度由 span 决定。

- int fy;

- int fw;

- int fh;

- int layoutX;
  - Runtime layout result. Dock/flow resolution updates these fields for
    the canvas only; the author rectangle above remains serializable.

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
  - 选项列表：选择型字段的候选项，也复用作表格列、标签页
    标题、步骤/菜单/树条目等（见 UsesOptionsOf）。

- List<FormField> kids;
  - 容器支持。容器（Card / Tabs）在 `kids` 中持有嵌套
    子组件；每个子元素在 `childTab` 中记录所属标签页
    （如 Card 这类单内容体容器恒为 0）。`uiState`
    是组合控件绑定的响应式索引——Tabs 的当前标签页、
    Steps 的当前步骤、Collapse 的展开面板、
    Pagination 的页码、Menu / Tree 的选中项——让预览实时生效。

- int childTab;

- SignalInt uiState;

- int tabOrient;
  - Tabs 容器的标签条方向（0 = 顶部横向，1 = 左侧纵向，
    对应运行时 Tabs 的 orient 属性）。序列化为
    "orient": "horizontal"/"vertical"，纵向时画布与生成
    代码都让子级让出左侧轨道。

- JsonValue extra;
  - 设计器未建模的字段键原样保留(DataGrid 的 "of"/"columns"、
    控件的 "props" 直通表等):加载时收集,保存时原样写回,
    手写设计稿经设计器往返不丢声明。为 null 表示没有。

- void InitField(int ft)
  - 公共初始化：按字段类型填默认标签 / 占位 / 选项与 24 栅格跨度。

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
  - 本字段是否为容器类型（拥有嵌套子组件）。

- int KidCount()
  - 嵌套子组件数 / 第 i 个子组件。

- FormField KidAt(int i)
  - 返回第 i 个子组件。

- FormField PlaceAt(int x, int y)
  - 设置设计空间中的绝对位置（容器子元素相对于父内容
    框）。

- FormField FreeSize(int w, int h)
  - 设置设计空间中的尺寸（自由画布）。

- int FreeX()
  - 设计空间绝对坐标 / 尺寸（自由画布；未放置时为默认值）。

- int FreeY()
  - 设计空间 Y 坐标。

- int FreeW()
  - 设计空间宽度。

- int FreeH()
  - 设计空间高度。

- void SetLayoutRect(int x, int y, int w, int h)
  - Runtime rectangle used by the designer canvas. It is deliberately
    separate from fx/fy/fw/fh so automatic layout cannot alter the source.

- void ResetLayout()
  - 清除运行时布局结果（含全部子组件），取值回退到设计矩形。

- int LayoutX()
  - 运行时布局矩形（未结算 layoutReady=false 时回退设计矩形）。

- int LayoutY()
  - 运行时布局 Y（未结算时回退设计 fy）。

- int LayoutW()
  - 运行时布局宽（未结算时回退设计 fw）。

- int LayoutH()
  - 运行时布局高（未结算时回退设计 fh）。

- void StashRect(string key)
  - 把当前 fx/fy/fw/fh 写入备用矩形表，键为
    "WxH"（覆盖该尺寸之前的条目）。

- bool LoadRect(string key)
  - 把 "WxH" 键下保存的备用矩形加载到 fx/fy/fw/fh。
    该尺寸无条目时返回 false（矩形保持不变）。

- static bool AltKeyIs(string entry, string key)
  - 备用矩形条目是否为给定 "WxH" 键。

- void SeedOptions(int ft)
  - 为组件填充默认选项列表——选项值、表格列、
    标签页标题、步骤/菜单/树条目——使新添加的组件
    在预览中不至于空白。

- FormField(int ft)
  - 构造指定类型的字段（默认标签 / 占位 / 选项已种子化）。

- static FormField New(int ft, int seq)
  - 构建 `ftype` 字段并生成唯一的默认模型键。

- FormField Clone()
  - 深拷贝（子元素一并复制）：设计器的「复制」用它，
    副本除了模型名之外和原件完全一致。

- static int Custom()
  - Legacy palette value retained only while reading old designer state.

- bool IsCustom()
  - 是否为自定义组件字段（customKind 非空）。

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
  - 覆写：公共事件之外并入字段类型（ftype）对应的语义事件集。

- static string DefaultLabel(int ft)
  - 字段类型的默认标签文案（中文）。

- static string DefaultPlaceholder(int ft)
  - 字段类型的默认占位文本。

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
  - 本字段是否为非可视（行为）组件（设计器托盘条显示）。

- bool UsesOptions()
  - 本字段类型是否带用户可编辑的选项列表。

- static bool IsDisplayOf(int ft)
  - 显示 / 布局 / 反馈 / 导航组件（ftype >= 15）渲染
    静态内容：它们横跨整行，跳过标签列。

- bool IsDisplay()
  - 本字段是否为显示/布局组件（ftype >= 15，渲染静态内容）。

- int RowUnits()
  - 行高（未缩放），让较高的输入框（textarea/upload/richtext）和
    多选项字段在画布上获得所需空间。

- int OptionCount()
  - 选项数 / 第 i 个选项 / 追加一个选项。

- string OptionAt(int i)
  - 返回第 i 个选项。

- void AddOption(string s)
  - 追加一个选项。

- void RemoveOptionAt(int i)
  - 通过重建列表来移除一个选项（运行时的列表中段
    RemoveAt 与列表中段 Insert 一样可能破坏堆）。

- string JoinOptions()
  - 选项列表 → "a|b|c" 文本（GetExtra 序列化用）。

- void SetOptionsFrom(string joined)
  - "a|b|c" 文本 → 选项列表（SetExtra 反序列化用）。

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
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（label/fname/placeholder/required/
    wrap/defOn/class/span/dock/customKind/options，部分按类型给）。

- override string GetExtra(string key)
  - 选项列表存为文本，逗号分隔的每项即一个选项。

- override bool SetExtra(string key, string val)
  - 设计器写入 `options`（逗号分隔选项文本）；其他键返回 false
    交给基类。


## FormGroup (class)

带校验的表单容器：`Add` 进来的每个字段包成一行（标题 + 控件 +
错误行），Validate() 按规则逐行校验并就地显示第一条错误；被联动
隐藏/禁用的行自动跳过，因此「radio 切换显隐 + 必填」可以同框工作。

FormGroup form = new FormGroup();
form.Add("用户名", name)
.Rule(FormRule.Required("请输入用户名"))
.Rule(FormRule.MinLen(3, "至少 3 个字符"));
form.Add("确认密码", pwd2)
.Rule(FormRule.EqualsField(form.Field("pwd"), "两次输入不一致"));
submit.OnClick(() => { if (form.Validate()) { Save(); } });

- List<FormRow> rows;

- int badCount;
  - 上一次 Validate 的失败行数。

- FormGroup()
  - 空表单（之后 Add）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- FormRow Add(string caption, Control ctl)
  - 添加一行字段：`caption` 为标题（可为 ""），`ctl` 是任意数据
    控件（Input / SelectBox / Switch / Checkbox / DatePicker …）。
    返回该行以便链式追加规则。

- FormRow Field(string name)
  - 按字段控件名查行；不存在返回 null。

- int Count()
  - 行数。

- bool Validate()
  - 全量校验：逐行 Check()，失败行就地显示错误。全部通过返回 true。

- int ErrorCount()
  - 上一次 Validate 的失败行数。

- void ClearErrors()
  - 清除全部错误显示（重置表单时）。

- bool Valid()
  - 静默检查：不改动任何显示状态，全部有效才为 true。

- string FirstError()
  - 静默检查时的第一条错误消息（全部有效为 ""）。

- static string ValueOf(Control ctl)
  - 控件当前值的文本视图：文本类控件给文本，布尔类给
    "true"/"false"，选项类给选中项文本。未知控件回退到属性
    协议的 `value`（GetExtra），再没有就为空串。

- string Value(string name)
  - 按字段名取值（控件不存在为 ""）。

- bool BoolValue(string name)
  - 按字段名取布尔值（"true" 才为真）。

- int IntValue(string name, int def)
  - 按字段名取整数值（空/非法/越界回退 `def`）。


## FormRow (class)

表单里的一行：标题（可选）+ 字段控件 + 错误消息行，竖直排布。

错误画法分两类：Input 自带 SetError（红框 + 字段下小字，编辑即
清除），直接用它；其余控件（TextArea / SelectBox / Switch /
Checkbox / 自定义控件）没有错误态，由行内的 `label.error` 承担。
两种画法的消息都落在字段正下方，视觉一致。

- Control field;

- Label err;

- List<FormRule> rules;

- FormRow(string caption, Control ctl)
  - 构造一行：标题（可为 ""）+ 字段控件 + 隐藏的错误消息行，
    竖直堆叠在透明面板里。

- string Name()
  - 字段控件名（FormGroup.Field 的查找键）。

- string Value()
  - 字段当前值（FormGroup.ValueOf 的类型聚合视图）。

- FormRow Rule(FormRule r)
  - 追加一条规则（链式）：`form.Add("邮箱", email)
    .Rule(FormRule.Required("必填")).Rule(FormRule.Email("格式不对"))`。

- bool Active()
  - 该行是否参与校验：被联动隐藏或禁用的字段不算错误——依赖
    表单里「选了配送才出现的地址」在自提时不应阻塞提交。

- string Check()
  - 运行全部规则并就地绘制状态；返回第一条错误消息（"" = 通过）。

- string Peek()
  - 静默检查：不改任何显示状态，返回第一条错误消息。

- void Clear()
  - 清除该行的错误显示。

- void Paint(string msg)
  - 显示一条错误消息。Input 自带错误态（红框 + 字段下消息），但
    那条消息画在自身矩形之外（y+h+2 处），行式布局里正好落在下一
    行的背景之下会被整条盖掉——留一个空的错误标签占住这一行，
    消息就画在预留出的空间里。


## FormRule (class)

一条字段校验规则。规则是无状态的：每次校验时拿字段的当前值文本
重跑一遍，字段隐藏/禁用时整行跳过（见 FormRow.Active）。

- FormCheck check;

- FormRule(FormCheck c)
  - 构造：包一条校验委托（用 Required/Email 等工厂或直接给 lambda）。

- string Apply(string value)
  - 对当前值执行本规则："" = 通过，否则为错误消息。

- static FormRule Required(string msg)
  - 必填：去空白后不能为空。

- static FormRule MinLen(int n, string msg)
  - 最短长度。

- static FormRule MaxLen(int n, string msg)
  - 最长长度。

- static FormRule Email(string msg)
  - 电子邮件格式：`@` 不在首尾，`@` 后还有带点的域名。空值不判
    （是否可空交给 Required 决定），这样「选填的邮箱」只填一半才报错。

- static FormRule Range(int lo, int hi, string msg)
  - 整数值域（含端点）。空值不判；非数字按越界处理。

- static FormRule EqualsField(FormRow peer, string msg)
  - 与另一行的值一致（确认密码、再次输入邮箱）。peer 在校验时
    实时取值，先填后改都算。

- static FormRule Checked(string msg)
  - 必须为真（同意条款、开启某前提）。布尔类控件（Checkbox /
    Switch / Radio）的值是 "true"/"false"，Required 只看非空、
    对未勾选的 "false" 无感，所以这类约束用它。

- static bool IsInt(string s)
  - 十进制整数（允许负号）。手动扫一遍而不是直接 Convert：非法
    输入在这里是常态（校验器就是为它存在的），不该走异常路径。


## Grid (class)

等宽栅格容器（Naive UI n-grid）：把子控件铺进 N 等宽列，
列宽自适应容器宽度，行高按每行最高的格子决定。

用来排卡片墙、指标面板、表单的多列分区——凡是"三个一排、
宽度均分、窗口变窄要能自己降列"的场合：

Grid g = Grid.Of(3).Gap(app.theme.gapLarge);
g.With(cardA);
g.With(cardB);
g.With(new GridItem(2).With(wideCard));  // 占两列
g.RenderInside(app, area);

响应式：给一个每列最小宽度，窗口变窄时自动减列（本框架的
CSS 没有 @media，断点由容器自己按实际宽度算）：

Grid g = Grid.Of(4).MinColumn(app.Scale(220));

容器按内容测量高度（行高之和），因此可以直接停靠进一列里，
不必给死高度。外观为透明（base.css 的 `grid` 规则），要边框
背景就放进 Card/Panel 或给它挂类。

列数与间距都可以由 CSS 给：`grid { columns: 3; column-gap: 16px;
row-gap: 24px }`——代码里的 Of(n)/Gap(px) 只是覆盖它。

- int cols;
  - 列数（<1 表示听样式表的 `columns`，仍缺省则 1 列）。

- int minColW;
  - 每列的最小宽度（0 = 不做响应式降列）。

- int lastW;
  - 上一帧实际排布用的内容宽度，以及上一次测量用过的值：
    行数（因此高度）取决于宽度，而测量发生在拿到宽度之前，
    两者不一致说明高度还没收敛，需要再排一帧。

- int measW;

- void InitGrid(int n)
  - 初始化：以 "grid" 注册为 dock-fill 容器，置列数并清零宽度缓存。

- Grid()
  - Default design-time constructor：单列（等同一列堆叠）。

- static Grid Of(int n)
  - `Grid.Of(3)` —— 三等宽列。

- override string Kind()
  - 控件类型标识（序列化/设计器用）："Grid"。

- override string StyleType()
  - 皮肤样式类型键："grid"。

- Grid SetCols(int n)
  - 设置列数；<1 时听样式表的 `columns`。通常用 `Grid.Of(n)`。

- Grid MinColumn(int px)
  - 每列不窄于 `px`：容器宽度不够时自动减少列数（响应式）。

- int ResolvedCols(int cw)
  - 本次排布的实际列数：代码给的列数优先，其次是样式表的
    `columns`；配了每列最小宽度时按可用宽度往下调，最少 1 列。

- int ColGapPx()
  - 列间距（px）：样式表 column-gap，未声明时回退默认间距。

- int RowGapPx()
  - 行间距（px）：样式表 row-gap，未声明时回退默认间距。

- static int SpanOf(Control c)
  - 子项在栅格里的跨列数（非 GridItem 一律占 1 列）。

- static int OffsetOf(Control c)
  - 子项在栅格里的起始列偏移（非 GridItem 为 0）。

- static int ColEdge(int cw, int gap, int n, int i)
  - 列 `i` 相对内容框左边缘的偏移。按 `(cw + gap) * i / n` 取整而不
    是先算单列宽再乘——后者会把每列的取整误差累加，最后一列
    差出好几个像素、右边缘对不齐。

- static int SpanWidth(int cw, int gap, int n, int at, int span)
  - 从第 `at` 列起、跨 `span` 列的宽度。

- int Place(int n, List<Control> items, List<int> rowOf, List<int> colOf, List<int> spanOf)
  - 把可见子项分配到各行：返回每个子项所在的行号写入 `rowOf`、
    起始列写入 `colOf`、实际跨列写入 `spanOf`，并返回行数。
    一行放不下当前格时换行；跨列超过总列数的格子按总列数截断，
    因此不会出现放不进任何一行的死格。

- List<Control> VisibleItems()
  - 当前可见（StyleVisible）的子项。

- List<int> RowHeights(int rows, List<Control> items, List<int> rowOf)
  - 每行的高度：该行最高格子的测量高度，样式表声明了
    `line-height` 时统一取它（等高卡片墙）。

- override void OnMeasure(App app)
  - 覆写：借上一帧排布用的宽度算列数/行数与每行高度，得出偏好
    高度；首帧宽度未知时主动再要一帧让行数收敛。

- override void Arrange(int px, int py, int pw, int ph)
  - 覆写：解析实际列数后，把可见子项按行/列（含跨列与前置空列）
    铺进内容框，并记下本次内容宽度供 OnMeasure 使用。

- override bool SetExtra(string key, string val)
  - 设计器序列化应答：`cols`（或 `columns`）与 `min-column`
    两个文档键；未知键返回 false 交给基类。

- override string GetExtra(string key)
  - 设计器读取当前列数与最小列宽；其余键返回 ""。


## GridItem (class)

栅格里的一格（Naive UI n-gi）：给内容声明跨几列、前面空几列。

grid.With(new GridItem(2).With(chart));   // 占两列
grid.With(new GridItem(1, 1).With(note)); // 空一列后占一列

只跨一列时不必包这一层，直接把控件加进 Grid 即可。
本身是纵向流式的透明容器：多个子控件会自上而下堆叠。

- int span;
  - 跨列数（至少 1；超过栅格总列数时按总列数截断）。

- int offset;
  - 本格之前留空的列数。

- void InitItem(int sp, int off)
  - 初始化：以 "gridItem" 注册为纵向流式容器，钳制跨列/空列下限。

- GridItem()
  - Default design-time constructor：占一列。

- GridItem(int sp)
  - 占 sp 列（<1 按 1）。

- GridItem(int sp, int off)
  - 先空 off 列再占 sp 列（越界按边界钳制）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）："GridItem"。

- override string StyleType()
  - 皮肤样式类型键："griditem"。

- int Span()
  - 跨列数。

- int Offset()
  - 前置空列数。

- GridItem SetSpan(int sp)
  - 改跨列数（<1 按 1），返回自身。

- GridItem SetOffset(int off)
  - 改前置空列数（<0 按 0），返回自身。

- override bool SetExtra(string key, string val)
  - 设计器序列化应答：`span`/`offset` 两个文档键；未知键返回
    false 交给基类。

- override string GetExtra(string key)
  - 设计器读取当前跨列与空列数；其余键返回 ""。


## IconView (class)

SVG 图标控件：把 IconSvg 的具名图标当作一个可嵌套的控件放进
Flex/Panel 组件树，交给布局测量与排列，而不是在 OnPaint 里逐个
手绘定位。名字用 Tabler 原名（完整清单见 IconSvg.Names()），
Box 是图标边长（CSS px）；Color 为 0 时取样式前景色，再退到
主题主文字色。

Flex row = Flex.Wrapped().Gap(8);
row.With(new IconView { Name = "home" });
row.With(new IconView { Name = "settings", Box = 32 });
row.With(new IconView { Name = "heart", Box = 32, Color = 0xFFE91E63 });

- string Name;
  - Tabler 图标名；未知名字静默不画（与 IconSvg.Draw 一致）。

- int Box;
  - 图标边长（CSS px）。

- int Color;
  - ARGB 覆盖色；0 = 跟随样式前景色 / 主题文字色。

- void InitIconView(string name, int box, int color)
  - 初始化：图标名、边长与覆盖色；表面自绘（不铺样式底）。

- IconView()
  - 无名图标（之后设 Name；设计器用）。

- IconView(string name)
  - 默认 20px 边长。

- IconView(string name, int box)
  - 指定边长（CSS px）。

- IconView(string name, int box, int color)
  - 指定边长与覆盖色（0 = 跟随样式前景色 / 主题文字色）。

- override void OnMeasure(App app)
  - 覆写：首选尺寸 = 缩放后的边长（非法值回退 20px）。

- override void OnPaint(App app)
  - 覆写：盒内居中画图标；颜色取覆盖色，其次样式前景色，再退主题文字色。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（图标名 / 边长 / 颜色 / 类）。


## Image (class)

图片：传一个地址即可渲染。`Src` 支持四类来源，按前缀自动识别：

1. 文件路径 —— PNG/JPEG/BMP/GIF/TGA/PNM/PSD/WebP 直接解码，
`.svg` 文件按矢量光栅化；
2. `http://` / `https://` 网络地址 —— 后台线程取回（见
ImageHttp），完成前后都是占位，不阻塞 UI；
3. `data:image/...;base64,...` 数据 URI —— base64 解码后内存
解码，不落盘；
4. SVG（上述任意来源指向 SVG，或 mime 带 `svg` 的 data URI）——
以固有尺寸光栅化，首帧绘制后再按盒子尺寸重光栅一次。

`Fit` 控制缩放方式：contain（默认，等比留白）、cover（等比裁剪
铺满）、fill（拉伸铺满）、none（原始尺寸居中裁剪）。加载中与失败
由 `image` CSS 规则画的底色加 `Alt` 文本占位；`Loaded` / `Error`
事件通知结果。

Image img = new Image { Src = "https://example.com/logo.png" };
img.Error += () => img.Reload();

圆角：CSS `border-radius` 作用于背景与边框；位图内容本身按矩形
裁剪（当前光栅器只有矩形剪裁路径）。

- Binding<string> Src;
  - 图片地址（可双向绑定）：文件路径 / http(s) URL /
    data URI / SVG，按前缀自动识别（见类 doc 四类来源）。

- string Alt;
  - 替代文本：加载中/失败时显示在占位框里（也供无障碍用途）。

- string Fit;
  - 缩放方式：contain / cover / fill / none。

- UiEvent Loaded;
  - 图片就绪（已解码并缓存）。工作线程触发时自动封送回 UI 线程。

- UiEvent Error;
  - 解码失败、URL 取回失败或超时。

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
  - 公共初始化（各构造共用）。

- Image()
  - 空图（之后设 Src）。

- Image(string src)
  - 只给来源，Fit 用默认 contain。

- Image(string src, string alt)
  - 来源 + 替代文本。

- Image(string src, string alt, string fit)
  - 全参构造：来源、替代文本、Fit（contain/cover/fill/none）。

- string FitMode()
  - 归一化缩放方式（脏值回 contain）。

- void Resolve(App app)
  - Src 变化时重新解析来源并（重）注册内存图像。每帧Measure/Paint
    调用，未变化的 Src 直接返回。

- void ResetState()
  - 清空解析状态（Src 变化换源时）。

- void MarkLoaded(int w, int h)
  - 加载成功：记录像素尺寸、置状态 1 并 Post Loaded。

- void ResolveFile(string src)
  - 普通图片文件：路径即绘制 key，能读到宽度即视为加载成功。

- void ResolveSvgFile(string path)
  - .svg 文件：读源文本并以固有尺寸光栅化。

- void ResolveDataUri(string src)
  - data URI：mime 带 svg 走 SVG（base64 或文本载荷），否则按
    base64 位图解码进内存注册表。

- void ResolveUrl(App app, string src)
  - http(s) URL：内存缓存命中直接用，否则交 ImageHttp 后台取回。

- void RasterSvg(int boxW, int boxH)
  - 把 SVG 源光栅化到 imgKey。boxW/boxH > 0 时按盒子尺寸光栅
    （key 加 @WxH 后缀），否则用文档固有尺寸。

- void OnFetched(string key, int w, int h)
  - ImageHttp 完成回调：key 不匹配（已换源）或已加载时忽略。

- void OnSvgFetched(string text)
  - ImageHttp 取回 SVG 源的回调：光栅化成功后补发 Loaded。

- void OnFetchFailed()
  - ImageHttp 失败回调：置失败态并 Post Error。

- void Reload()
  - 丢弃解析状态并重新加载当前 Src：URL 失败重试、源文件或
    远端内容变化后刷新。真正的工作在下一次绘制时发生。
    典型用法：`img.Error += () => img.Reload();`

- static long StrHash(string s)
  - FNV-1a 变体（djb2 取模）：内存图像 key 由源地址派生，同一
    地址稳定复用缓存，长 int 运算避免 32 位回绕的歧义。

- static int ScaleTo(int v, int num, int den)
  - 按 num/den 四舍五入缩放 v（den ≤ 0 时原样返回）。

- void OnMeasure(App app)
  - 测量：CSS 尺寸优先，缺一边时按已加载图片的纵横比补齐，
    未加载时兜底 96x96。

- void OnPaint(App app)
  - 绘制：样式表面 + 按自适应模式 blit 图像；未加载完成时画占位。

- void BlitFitted(App app, Canvas c)
  - 按 Fit 把已加载图像 blit 进盒子（contain/cover/fill/none）。

- void DrawPlaceholder(App app, Canvas c, StyleBox s)
  - 占位文本：Alt（加载中/失败时显示）；失败且无 Alt 时为 "..."。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 设计器属性面：src / alt / fit / class。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Loaded"/"Error"。

- override void BindEvent(string evt, Action a)
  - 覆写："Loaded"/"Error" 各挂对应 UiEvent，其余按名称走通用路由。

- override string GetExtra(string key)
  - 设计器额外键：`fit` 收发 contain/cover/fill/none。

- override bool SetExtra(string key, string val)
  - 设计器写入 `fit`；其他键返回 false 交给基类。

- int FitIndex()
  - FitMode 的枚举序号（设计器 fit 下拉用）：0 contain / 1 cover /
    2 fill / 3 none。


## Input (class)

带光标、占位符和响应式绑定的单行文本输入框。

保留式 C# 风格控件：拥有稳定标识（因此焦点在
兄弟/弹层变动后仍能保持），焦点/点击由框架解析，并
暴露委托事件：
Input name = new Input("your name");
name.Change += () => { ... };   // 每次编辑时触发
name.Submit += () => { ... };   // 回车时触发
name.Render(app, x, y, w, h);
获得焦点时消费键盘事件（可打印字符、Backspace、
Delete、Left/Right/Home/End），就地编辑绑定的 SignalString，并将
光标位置上报给框架，使 IME 组合窗口
跟随光标移动。

能力面（对齐 Naive UI Input）：`Disabled`（继承自 Control）、
`SetStatus` 校验态描边、`Prefix`/`Suffix` 文字槽与 `PrefixIcon`、
`Loading` 转圈、`ShowCount`+`MaxLen`(+`CountGraphemes`) 字数统计、
`Round` 圆角、`PassiveActivated` 回车激活、`Filter` 输入过滤、
以及 Clear/Focus/Blur/SelectAll/ScrollToEnd 手动操作。

- SignalString editSig;
  - 内部编辑缓冲（SignalString，§6.2 内部状态缓冲）：光标、选区、
    过滤、IME 组合等编辑操作的运行时真相。不是绑定协议，
    外部通道是 `data`（Binding<string>）。

- Binding<string> data;
  - 双向绑定文本：`name.data = user.name;` 使输入框与
    模型双向保持同步（编译器降级的 Binding）。可选的；
    未设置时，输入框仅编辑其内部 SignalString。

- string hint;
  - 占位文本（内容为空时显示）。

- int cursorPos;
  - 光标位置（字节偏移）。

- int selAnchor;

- int wid;
  - 控件命中 id。

- App liveApp;
  - 上一帧渲染时的宿主 App：手动操作（Focus/Blur/Clear/
    ScrollToEnd）需要在事件回调里改焦点并请求重绘，而回调
    没有 app 形参——渲染沿顺手记一笔即可。

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

- Binding<string> Size;
  - 尺寸变体：对应 Class = "small" / "large"（Naive UI size prop）。
    Binding<string> 使 `inp.Size = vm.density;` 可响应式换档，
    属性面板（PropSpec.Enum + str 绑定）也能直接读写选项文本。

- bool Clearable;
  - 可清空：文本非空且（获焦或悬停）时在尾端显示 X 按钮，
    点击清空内容并触发 Change（Naive UI clearable prop）。

- bool Autoselect;
  - 聚焦时自动全选内容（Naive UI autoselect prop）。适合
    地址栏、"搜索并跳转" 等点进来就整段替换的字段。

- bool autoselected;
  - 跟踪 autoselect 已执行过（防止每次获焦都全选）。

- int MaxLen;
  - 输入字符上限（>0 生效）。按码点计——一个 CJK 字符记 1，
    `CountGraphemes` 打开时按字素簇计（👨‍‍👧 记 1）。
    与 `ShowCount` 显示的计数同一口径。0 表示不限。

- string Prefix;
  - 前缀 / 后缀文字槽（Naive UI prefix/suffix）。画在文本与图标
    之间，颜色来自 `input::prefix` / `input::suffix`。

- string Suffix;

- string PrefixIcon;
  - 自定义图标名（Gui.Icon）：前缀槽用 `PrefixIcon`、后缀槽用
    `iconTrail`；密码框用 `PasswordIcon`（默认 "eye"），清除钮用
    `ClearIcon`（默认 "x"）。

- string PasswordIcon;

- string ClearIcon;

- bool ShowCount;
  - 是否显示字数统计（"当前 / 上限"）。上限来自 MaxLen；未设
    MaxLen 时按纯显示处理（Naive UI show-count）。

- bool CountGraphemes;
  - 字数统计与 MaxLen 是否按字素簇计数（Naive UI count-graphemes）。
    false 时按码点计数。

- bool Loading;
  - 加载态：后缀区画一个转圈指示器，并禁用编辑（Naive UI loading）。

- bool Round;
  - 圆角字段：整高半圆角（Naive UI round），走 `.round` 类，
    皮肤可覆写。

- string FieldStatus;
  - 校验状态：""/success/warning/error，映射到 base.css 的
    字段状态类。与 errMsg 独立（后者是具体消息文案）。

- bool PassiveActivated;
  - 被动激活（Naive UI passively-activated）：聚焦后先不接收
    文本输入，按回车才开始编辑——避免误触 Tab 聚焦就吞掉输入。

- bool passiveActive;

- InputFilterFn Filter;
  - 输入过滤（Naive UI allow-input）：返回清洗后的候选文本，
    null 表示原样拒绝。用于「只允许数字」「trim 空白」这类
    输入约束。内置 `Input.OnlyDigits` / `Input.NoSpaces`。

- void InitInput(SignalString sig, string hintText)
  - 立即模式工厂与保留模式构造函数共用的初始化函数：
    设置 Control 基类（停靠顶部，使追加的输入框
    自动贴齐），再设置输入框专属状态。

- Input():this("")
  - Default design-time constructor; the form supplies `placeholder` later.

- Input(string hintText)
  - 保留模式构造函数：`Input name = new Input("Your name");`

- static string Mask(string s)
  - 一个与 `s` 等长（按字节）的 `*` 字符串，使掩码显示保持
    相同的字节布局，光标/选区计算仍然有效。

- string GetText()
  - 当前文本。

- void SetHint(string v)
  - 宿主调整占位文字（SelectBox 的筛选框在 Filterable(true) 之后
    才由 FilterHint 定文案，晚于构造）。

- int WidgetId()
  - Allows a host to move focus to this input programmatically
    (used for the inline rename box in a list).

- void SetText(string v)
  - 覆盖文本：光标收到末尾、清空选区并写回绑定。

- void SelectAll()
  - 选中全部文本（等同于用户按 Ctrl+A），因此下一次输入
    直接替换原内容。地址栏之类「点进来就是要换掉它」的
    字段在 Focus 事件里调用它。

- void Clear()
  - 清空内容（Naive UI ref.clear()）。触发 Change，光标归零。

- void Focus()
  - 以编程方式把焦点抢到本输入框（Naive UI ref.focus()）。
    禁用态不响应。

- void Blur()
  - 交出焦点（Naive UI ref.blur()）。

- void ScrollToEnd()
  - 光标移到行尾（Naive UI ref.scrollTo 的等价物：渲染沿本来
    就会把光标滚进视口）。

- void ScrollToStart()
  - 光标移到行首。

- void SetStatus(string v)
  - 设置校验状态（""/success/warning/error），即 base.css 的
    字段状态类。与 errMsg 互不影响：前者描边，后者带消息。
    非白名单值归一为 ""（设计器经 PropSpec 直写字段时，渲染沿兜底同见）。

- int CountOf(string s)
  - 当前文本的计数值（按 CountGraphemes 口径）。

- int CountValue()
  - 当前文本的计数值。

- static string CountText(int count, int max)
  - 字数统计的显示文本："当前 / 上限"；计数超过 99 时上限侧
    截为 "99+"（Naive UI 的显示口径）。

- static string OnlyDigits(string s)
  - 内置过滤器：只允许数字（Naive UI allow-input 示例一）。

- static string NoSpaces(string s)
  - 内置过滤器：不许有前后空格（Naive UI allow-input 示例二，
    边输边 trim；词与词之间的空格照常输入）。

- void SetError(string msg)
  - 给字段标记校验错误；绘制红色边框，并显示
    `msg` 于字段下方，直到用户编辑（或调用 ClearError）。

- void ClearError()
  - 清除待处理的校验错误（任何编辑时自动调用）。

- override void SyncBinding()
  - 把绑定的模型值拉入编辑缓冲区（model -> UI）。

- void PushBinding()
  - 把编辑缓冲区通过绑定写回模型（UI -> model）。

- void Edited()
  - 所有 UI 编辑都汇入此处：先写回，再通知；同时清除
    任何校验错误——修正正是问题已解决的最强信号。

- void ClampCursor()
  - 光标与选区锚点钳回文本范围内。

- bool HasSel()
  - 是否存在非空选区。

- int SelStart()
  - 选区起点（较小端；无选区即光标位置）。

- int SelEnd()
  - 选区终点（较大端）。

- string SelText()
  - 选区文本（无选区为 ""）。

- void DeleteSel()
  - 删除选中的范围，并把光标收拢到其起点。

- bool Insert(string s)
  - 插入的唯一闸门：先替换选区，再过宿主过滤器与字符上限。
    被拒绝时完整回滚（选区也还回去）——"30 上限里再敲一个
    字"不应有任何可见变化。过滤后整体被改写时（在 "12" 的
    数字框中间插 "a" → "12"），光标按净增字节平移，不会被
    trim 式过滤器甩到文末。上限按 CountOf 口径计，与字数
    统计显示同一个数。

- bool DeleteCandidate(string cand)
  - 退格/删除的候选整体再过一次过滤器：删掉词边界上的字母
    可能"暴露"出前后空格（NoSpaces 场景），删除路径也要被
    清洗。返回 false 表示过滤器拒绝该结果。

- void HandleInput(App app)
  - 键盘输入处理（聚焦时由 Render 调用）：编辑键、Ctrl 快捷键、
    方向键/Home/End 导航与鼠标选择/拖选。

- int Render(App app, int x, int y, int w, int h)
  - 每帧渲染与交互（立即模式入口；停靠布局经 OnPaint 进入）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override Binding<string> SizeOf()
  - 覆写：返回 Size 绑定字段（设计器经它发现尺寸档位绑定）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（占位 / 文本 / 尺寸 / 前后缀 / 状态 / 长度等）。

- override string GetExtra(string key)
  - 文本经 SetText 写入，保证光标与选区不越界。

- override bool SetExtra(string key, string val)
  - 覆写：写回扩展属性（文本 / 密码 / 图标 / 状态等键）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change" 与 "Submit"。

- override void BindEvent(string evt, Action a)
  - 将输入框自身的语义事件（Change/Submit）路由到对应的 UiEvent
    字段；其余事件落入 `On` 上的通用事件包。

- override void OnMeasure(App app)
  - 覆写：按尺寸档解析样式，高取样式行高，宽缺省 220。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = 以自身矩形 Render。


## InputNumber (class)

数字输入框（Naive UI n-input-number）：内嵌 −/+ 步进按钮，
中间是可编辑的数字区。绑定 SignalInt / `.data` 双向同步；
min/max 钳制，step 定步长；聚焦后 ↑/↓ 键同样加减，回车提交。

InputNumber age = new InputNumber(0, 120);
age.data = form.age;          // two-way
age.Change += () => { ... };
age.Prefix = "¥";             // 前缀
age.Render(app, 40, 40, 160, 34);

Naive UI 对齐的能力：
- `Prefix` / `Suffix`：常显前后缀（"¥ 0"、"0 %"）；
- `Placement`：步进钮位置 "both"（默认左右各一）/ "right"
（两枚并排收在右侧，Naive UI button-placement）；
- `Loading`：加载态——整框落灰、步进钮换成旋转圆点、输入忽略；
- `GroupDigits`：千分位分组展示（"1,075"），编辑缓冲仍存原始串；
- `Precision`：小数位数（定点数：模型值 = 显示值 × 10^Precision，
step/min/max 同按缩放单位计）；
- `Formatter` / `Parser`：自定义展示/解析钩子（Naive UI
format/parse，设了 format 通常就要设 parse）；
- `Check`：自定义验证——未通过时红描边 + `Invalid` 事件；
- `Submit`：回车提交事件。

外观走 `input` 类型规则（尺寸档 input.small/.large 直接生效），
步进按钮颜色取 `input::icon` part、前后缀取 `input::prefix` /
`input::suffix` part——代码只留几何与交互，不直读主题语义色。

- SignalInt model;
  - 值信号：立即模式与保留模式共用一条真值来源。

- Binding<int> data;
  - 双向绑定状态：`num.data = model.field;` 使模型字段与
    输入框双向同步（编译器降级的 Binding）。

- string hint;
  - 空值占位提示（值为 0 且不在编辑时显示 hint 而非 "0" 的场景
    由 hint != "" 决定；默认 "" 直接显示数值）。

- int minV;
  - 取值范围（写入与绑定拉取都钳到 [minV, maxV]）。

- int maxV;

- int stepV;
  - 步进量：按钮/↑↓ 每次 ±step。0 视为 1。带小数时按缩放单位计
    （Precision=2 时 step=1 即 0.01）。

- string editBuf;
  - 聚焦编辑中的文本缓冲（原始串，不含分组逗号）；失焦、回车或
    点击步进时提交。

- bool hadFocus;
  - 上一帧是否聚焦（进入聚焦灌缓冲、离开聚焦提交的跳变沿）。

- int wid;
  - 字段命中 id。

- int minusId;
  - − 钮命中 id。

- int plusId;
  - + 钮命中 id。

- UiEvent Change;
  - 值被用户改变时触发（C# 风格：`n.Change += h;`）。

- UiEvent Submit;
  - 回车提交编辑缓冲时触发（Naive UI 的键盘提交沿）。

- UiEvent Invalid;
  - 自定义验证未通过时触发（值已写入，仅标记 error 状态）。

- Binding<string> Size;
  - 尺寸变体：对应 Class = "small" / "large"（Naive UI size prop）。

- Binding<string> Prefix;
  - 前缀文本（Naive UI prefix）：画在数值左侧的常显文字，如 "¥"。

- Binding<string> Suffix;
  - 后缀文本（Naive UI suffix）：画在数值右侧的常显文字，如 "%"。

- Binding<string> Placement;
  - 步进按钮位置（Naive UI button-placement）："both"（默认，左右
    各一）| "right"（两枚并排收在右侧）。

- bool Loading;
  - 加载状态（Naive UI loading）：置真时整框落灰、步进钮换成旋转
    圆点、指针与键盘都不响应（远端校验/取数进行中的典型形态）。

- int Precision;
  - 小数位数（Naive UI precision）：钳制到 0..6。值模型仍是 int，
    按定点数解释——模型存显示值 × 10^Precision（2 位小数时 1079
    显示 "10.79"）。

- bool GroupDigits;
  - 千分位分组：整数部分每三位插一个逗号（"1,079"）。编辑缓冲
    始终存原始串，分组只作用于展示与提交解析。

- NumFormatter Formatter;
  - 自定义展示钩子（Naive UI format）；null 走内置展示。

- NumParser Parser;
  - 自定义解析钩子（Naive UI parse）；null 走内置解析。

- NumValidator Check;
  - 自定义验证（Naive UI validator）；null 恒过。

- bool invalidFlag;
  - 最近一次提交/步进是否未通过 Check：置真时画 `input::error`
    描边，下一次通过验证的写入清除。

- void InitNumber(SignalInt m, int lo, int hi)
  - 公共初始化（各构造共用）。

- InputNumber()
  - Default design-time constructor（范围 0..100）。

- InputNumber(int lo, int hi)
  - `new InputNumber(0, 120)` —— 带取值范围。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"input"，与单行输入框共用皮肤规则）。

- override Binding<string> SizeOf()
  - 覆写：返回 Size 绑定字段（设计器经它发现尺寸档位绑定）。

- void Hint(string text)
  - 设空值占位提示。

- InputNumber OnChange(Action a)
  - 订阅值变化（链式）。

- InputNumber OnSubmit(Action a)
  - 订阅回车提交（链式）。

- InputNumber OnInvalid(Action a)
  - 订阅验证失败（链式）。

- InputNumber WithFormatter(NumFormatter f)
  - 设自定义展示钩子（链式）。

- InputNumber WithParser(NumParser p)
  - 设自定义解析钩子（链式）。

- InputNumber WithValidator(NumValidator v)
  - 设自定义验证（链式）。

- int Step()
  - 生效步长（<1 视为 1）。

- int Decimals()
  - 生效的小数位数（钳到 0..6，与展示/解析共用一条口径）。

- int Value()
  - 当前值（经绑定解析）。

- static int Pow10(int n)
  - 10^n（n 钳到 0..6）。

- static string NumberText(int scaled, int decimals)
  - 把缩放整数值写成定长小数的数值文本：1079@2 -> "10.79"、
    -5@2 -> "-0.05"、1234@0 -> "1234"。

- static string GroupThousands(string s)
  - 整数部分加千分位逗号（只认首个 '.' 前的部分；已含逗号则原样
    返回，供编辑缓冲的分组展示复用）。"-1234.5" -> "-1,234.5"。

- string PlainText(int scaled)
  - 展示用的原始数值文本（含小数，未分组未套钩子）。

- string DisplayText()
  - 当前完整展示串（分组 + Formatter），即字段里显示的那段数字。

- static int ParseScaled(string text, int decimals, int def, NumParser parser)
  - 把编辑缓冲（或任意展示串）解析为缩放整数：先过 Parser 钩子，
    再剥分组逗号，按小数点拆分、多余小数四舍五入到 Precision 位。
    非法/空串回退 def。"-1,0.795"@2 -> -108（795 舍入为 80 进位）。

- int StepUpVal(int v)
  - 步进一步（未钳制）：v ± step。

- int StepDownVal(int v)
  - 步退一步（未钳制）：v - step。

- int Clamp(int v)
  - 钳制到 [min, max]（公开供测试与宿主复用）。

- void RunValidator(int v)
  - 对将要写入的值跑一次 Check，刷新 invalidFlag，返回该值。

- string EditDisplay()
  - 编辑缓冲的即时展示串（分组 + Formatter）。

- void SetRaw(int v)
  - 内部写值：钳制、写回绑定、跑验证并触发 Change（值未变时静默）。

- void SetValue(int v)
  - 程序化写值（与用户输入同一条校验/事件路径）。

- override void SyncBinding()
  - 把绑定的模型值拉入本地信号并钳制（model -> UI）。

- static int ParseInt(string s, int def)
  - 宽松整数解析（非法/溢出回 def）。

- void CommitBuffer()
  - 解析编辑缓冲为数值并提交（空缓冲/非法串还原当前值）。

- int Render(App app, int x, int y, int w, int h)
  - 每帧渲染与交互（立即模式入口；停靠布局经 OnPaint 进入）：
    聚焦沿、步进钮与键盘。

- void PaintBox(App app, int x, int y, int w, int h)
  - 绘制字段盒、前后缀、步进钮（或加载圆点）与中间数值。颜色来自
    解析样式盒（input 类规则）与 `input::icon` / `input::prefix` /
    `input::suffix` / placeholder / caret / error parts。

- void PaintSpinnerDots(App app, int cx, int cy, int radius, int accent, int rest)
  - 一圈 8 枚小圆点的加载指示（与 Spin 同相位规则），画在钮位上。

- void HandleKeys(App app, bool focused)
  - 键处理：↑/↓ 步进；数字/小数点/逗号写入缓冲；回车提交；Esc 还原。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（值 / 尺寸档 / 前后缀 / 按钮位置）；
    设计期常量键（min/max/step 等）经 GetExtra/SetExtra 应答。

- override string GetExtra(string key)
  - 覆写：应答扩展属性键（值显示文本与 min/max/step/precision 等设计期常量）。

- override bool SetExtra(string key, string val)
  - 覆写：写回扩展属性键（值经解析换算写入，其余常量直写）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"、"Submit" 与 "Invalid"。

- override void BindEvent(string evt, Action a)
  - 覆写：三个语义事件挂对应 UiEvent，其余按名称走通用路由。

- override void OnMeasure(App app)
  - 覆写：按尺寸档解析 input 样式，高取样式行高，宽缺省 140。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = 以自身矩形 Render。


## InputOtp (class)

验证码输入框（Ant Design Input.OTP 对位）：N 个一格一字符的小框
（默认 6），键盘直录、退格回退、方向键移格、Ctrl+V 整段粘贴。

保留式控件：稳定 WidgetId 使焦点在兄弟/弹层变动后仍能保持，
点击/焦点由框架按整框一个命中区解析（格内定位走几何），编辑逻辑
全部收在不依赖 App 的纯函数里（TypeChar/Backspace/MoveLeft/
Paste/SetCode…），无窗口即可单测：
InputOtp code = new InputOtp();
code.data = form.otp;          // 双向绑定验证码字符串
code.Change += () => { ... };  // 每格编辑时触发
code.Complete += () => { ... };// 最后一格填满时触发一次
code.Render(app, 40, 40, 320, 40);

能力面（对齐 antd Input.OTP 文档页）：`Length` 字符数、
`MaskMode` 密码模式（点显）、`Block` 占满宽度、`Size` 三档、
`ReadOnly`/`Disabled`/`SetStatus` 状态族、`Groups`+`Separator`
自定义分组渲染（"65-43-21"）、`Filter` 只允许特定值
（内置 `Input.OnlyDigits` / `InputOtp.OnlyLetters`）。

- List<string> cells;
  - 每格一个码点（空串 = 未填）。

- int active;
  - 当前活动格索引（0 基）。

- int wid;
  - 整框命中 id。

- int length;
  - 字符格数。

- App liveApp;
  - 上一帧渲染的宿主：Focus/Blur 要在没有 app 形参的回调里用。

- Binding<string> data;
  - 双向绑定验证码：`otp.data = form.code;`（编译器降级的
    Binding）。未设置时只编辑内部格子。

- Binding<string> Size;
  - 尺寸档位：tiny/small/medium/large，映射 `.small/.large` 类
    （与 Input 同一路数，几何在 base.css 的 otp 规则里）。

- bool MaskMode;
  - 密码模式：格内以掩码字符（默认 •）显示。

- string MaskChar;
  - 自定义掩码字符（空串时用 •）。

- bool Block;
  - 占满宽度：格子横向平铺到宿主给的宽度（antd block）。

- bool ReadOnly;
  - 只读：可聚焦、可见值，但不接受任何编辑。

- string FieldStatus;
  - 校验状态：""/success/warning/error，走 base.css 的字段状态类。

- List<int> Groups;
  - 分组渲染：各组格数（如 {2,2,2} → "65-43-21"）。空列表 = 不分组。

- string Separator;
  - 组间分隔文字（默认 "-"）。

- InputFilterFn Filter;
  - 输入约束（antd allow-input）：收到「编辑后的完整候选」，
    返回清洗后的文本，null 表示整体拒绝。

- UiEvent Change;
  - 任何一次落地编辑触发。

- UiEvent Complete;
  - 从「未满」变「全部填满」的那一次编辑额外触发。

- bool wasFull;

- static List<string> SplitCps(string s)
  - 一格一个码点；内部按码点存，GetCode 拼回字符串。

- static string OnlyLetters(string s)
  - 内置过滤器：只允许英文字母（antd allow-input 示例二；
    只允许数字直接用 `Input.OnlyDigits`）。

- int SeparatorCount()
  - 组数 = 分隔符数量：只有当各组之和恰好等于格数时才分组，
    否则视为未配置（渲染回一视同仁的等距排）。纯函数，可单测。

- bool SepAfter(int i)
  - 格子 i 之后是否接一个分隔符槽。

- void InitOtp(int n)
  - 公共初始化（各构造共用）。

- InputOtp()
  - 设计器默认构造：6 格。

- InputOtp(int n)
  - `InputOtp code = new InputOtp(6);`

- int WidgetId()
  - 整框命中 id（宿主查焦点用）。

- void SetLength(int n)
  - 字符格数（<1 钳到 1）。缩小时保留已填的前缀。

- int GetLength()
  - 当前格数。

- int ActiveIndex()
  - 当前活动格索引（0 基）。

- void SetCodeFromModel()
  - 从绑定的模型拉一次变更（渲染沿的绑定拉取；无宿主也可调用，
    供脚本/测试驱动）。无绑定时空转。

- string GetCode()
  - 拼回的完整验证码（未填格为空串）。

- int FilledCount()
  - 已填格数。

- bool IsFull()
  - 是否全部填满。

- bool SetCode(string s)
  - 把「完整候选字符串」写进格子（左对齐）；过 Filter 闸门。
    返回 false 表示被拒绝（状态不变）。

- bool SetValue(string s)
  - 程序化赋值（走 SetCode + 事件 + 回写绑定）。净零变化静默。

- bool IsReadOnly()
  - 只读判定：ReadOnly 或控件整体 Disabled。

- void ClearAll()
  - 清空全部格子并回到首格（触发 Change；只读或已空时静默）。

- void Focus()
  - 聚焦整框（禁用态不响应）。

- void Blur()
  - 交出焦点。

- void SetStatus(string v)
  - 设置校验状态（白名单归一），与 Input 同族。

- void Edited(bool filled)
  - 把编辑缓冲区写回绑定并广播事件。`filled` = 本笔编辑填了空
    格——Complete 只在「这次真的把验证码填满」的沿上发（在满格
    上重敲同字 / 程序化覆盖不算「刚填上」）。

- bool TypeChar(string ch)
  - 在 active 格起逐字符落格并前进；过 Filter 闸门，净零变化静默。
    返回 false = 被拒绝 / 只读 / 无变化。

- static void Restore(List<string> cells, List<string> snapshot)
  - 用快照恢复格子（过滤拒绝时的回滚）。

- bool Backspace()
  - 退格：当前格有字符则清空并留格；空格则回退一格再清（回退
    后那格也空则整体无变化、静默）。

- bool DeleteForward()
  - Delete：清当前格，不移动。

- void MoveLeft()
  - 左移一格（首格不动）。

- void MoveRight()
  - 右移一格（末格不动）。

- void MoveHome()
  - 跳到首格。

- void MoveEnd()
  - End：跳到最后一个已填格（全空则第一格）。

- bool Paste(string clip)
  - Ctrl+V：整段走同一候选闸门，从 active 起铺。

- void HandleKeys(App app)
  - 聚焦时的按键消费（与 Input 同构的事件口径：kind 6 字符、
    kind 4 按键）。

- override void SyncBinding()
  - 把绑定模型的变更拉进格子（model -> UI）。长度对齐即可，
    相同就不动（避免每帧重写 active）。

- string CellDisplay(int i)
  - 第 i 格的显示文本；掩码模式返回掩码字符（默认 "•"）。

- int Render(App app, int x, int y, int w, int h)
  - 每帧渲染与交互（立即模式入口；停靠布局经 OnPaint 进入）：
    分组/分隔符几何、逐格状态与指针定位。

- int CellIndexAt(int boxX, int startX, int cellW, int gap, int sepTextW, int boxW, int px)
  - 指针 x -> 格索引（落在分隔符/空隙返回 -1）。步距几何与
    Render 的逐格推进完全一致：格宽 + 缝，组界处多一个分隔槽。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override Binding<string> SizeOf()
  - 覆写：返回 Size 绑定字段（设计器经它发现尺寸档位绑定）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（长度 / 尺寸 / 掩码 / 分组 / 只读 / 状态 / 分隔符）。

- override string GetExtra(string key)
  - 覆写：应答扩展属性键（验证码值 / 掩码字符 / 分组长度串）。

- override bool SetExtra(string key, string val)
  - 覆写：写回扩展属性键（值经 SetValue 写入，长度 / 分组等解析后生效）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change" 与 "Complete"。

- override void BindEvent(string evt, Action a)
  - 覆写：两个语义事件挂对应 UiEvent，其余按名称走通用路由。

- override void OnMeasure(App app)
  - 覆写：宽 = 格宽 x 格数 + 间距与分隔符，高 = 单元格边长（按尺寸档）。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = 以自身矩形 Render。


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
  - 初始化：文本（可绑定），默认不换行。

- Label()
  - 空标签（设计器用）。

- Label(string txt)
  - 给定初值文本。

- string Str()
  - 当前文本（通过绑定解析）。

- StyleBox ResolvedStyle(App app)
  - 解析 `label` 样式（带缓存）。

- override void OnMeasure(App app)
  - 覆写：测文本宽高；wrap 时按槽宽折行，取最大行宽与行数总高。

- override void OnPaint(App app)
  - 覆写：画文本；wrap 时逐行绘制，否则超宽省略号截断并垂直居中。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（文本 / 换行 / 类）。


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
  - 剔除已关闭的注册项（随渲染路径惰性调用）。出场动画没走完
    （closing）与还没轮到启动出场（wasOpen——宿主刚翻 open=false、
    本帧渲染沿才会 BeginExit）的保留——RenderStack 的收场扫描要从
    这里找到它们补画淡出帧（宿主驱动的对话框 open=false 后不再被
    Layer.Render 画到）；先剪后扫会把它们剪没，对话框就瞬间消失。
    5 秒兜底：宿主若从此不再渲染该层，注册表也不被永久占用。

- static LayerState TopmostDialog()
  - 最上层打开中的对话框（kind 1/2），无则 null。

- static bool IsTopmost(LayerState s)
  - `s` 是否是当前最上层打开中的 layer（渲染路径里避免同一键
    事件被多个对话框重复消费）。

- static void Close(App app, LayerState s)
  - 规范关闭入口（layui isOutAnim:true 的等价物）：open 立即为
    false（宿主状态机、Esc 往返立刻稳定），层进入 closing 态——
    渲染沿还会把它在屏幕上淡出+上滑收场。Closed 仍在关闭沿立即
    触发（回调时序与无出场动画时完全一致，出场纯粹是收场动画）。
    直接置 open=false 的宿主走同一条路（关闭沿在渲染时启动出场，
    并在那里补触发 Closed）。

- static void BeginExit(LayerState s)
  - 关闭沿（!open 且 wasOpen）的第一帧：启动出场动画。

- static void NotifyOpened(LayerState s)
  - 弹出完成回调：入场进度到 1000（或无入场）的第一帧触发一次。
    渲染路径在算完 Entrance 后调用。

- static bool CloseTopmostDialog()
  - Esc 的语义：最上层对话框报告取消（action 2，由调用方关闭；
    与 X 的 action 3、按钮的 1/2 一致）。photos 在屏时请求关闭。

- static bool CloseLast(int kind)
  - 关闭最上层打开中的 layer（直接置 open=false，不产生 action）。
    kind 0 窗口 / 1 对话框 / 2 提示框，-1 不限。无则返回 false。

- static void CloseAll(int kind)
  - 关闭全部打开中的 layer（不产生 action）。photos 不经注册表，
    由 CloseLast(-1) 的兜底分支请求关闭。

- static bool Key(App app, int code)
  - 键盘语义（宿主在 key-down 事件里调用；对话框与 photos 的渲染
    路径也经此消费）。Esc 关最上层（对话框优先、photos 次之），
    Enter 确认最上层对话框（多行 prompt 除外——Enter 是换行），
    ←/→ 在 photos 里翻页。有下拉/菜单等框架弹层打开时让位
    （框架自己收 Esc）。返回 true 表示本次按键已被层消费。

- static void BeginBody()
  - page 层正文控件树的构造包裹（与 LayerState.SetBody 配对）：
    BeginBody 把 WidgetId 计数器切到 Layer 专属的 body 保留段
    （seq 980000 起，每棵树步长 1024），EndBody 构造完还原宿主
    计数器并前进游标。正文控件在点击回调里中途 new 出来，若直接
    从宿主计数领号，领走的正是随后每帧控件的本命 id（“信号污染”）。

- static void EndBody()

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
  - 全屏变暗的图片查看器，包装共享的 Carousel。每张 slide 是一个
    图片源（文件 / data URI / http(s) URL / SVG），由 Gui.Widget.Image
    以 cover 填满观看框——photos 层不自己画占位。
    关闭按钮被点击的那一帧返回 true。

- static int BarH(App app)

- static int FooterH(App app)

- static int BtnW(App app)

- static int BodyTop(App app, LayerState s)
  - 状态 `s` 下，调用方正文内容应开始的绝对 Y 坐标。

- static int BodyLeft(App app, LayerState s)
  - 正文内容应开始的绝对 X 坐标。

- static bool Inside(int mx, int my, int x, int y, int w, int h)

- static void ApplyResize(App app, LayerState s, int mx, int my)
  - 缩放拖拽的几何推导：从 rStart* 起始几何与当前鼠标位推出新的
    x/y/w/h（含最小尺寸与客户区钳制）。dir 与 s.rDir 同义：
    1 左缘，2 右缘，3 下缘，4 右下角，5 左下角。渲染拖动与
    无窗口回归共用这一份推导。

- static void MinPos(App app, LayerState s, List<int> outv)
  - 最小化窗口在左下角任务条上的位置（仅剩一条标题栏，宽 260）：
    槽位从左往右横排，一行放不下向上折行。命中区（Bounds）与
    绘制（RenderActive）都从这里取位，保证两者始终一致。

- static void Bounds(App app, LayerState s, List<int> outv)
  - `s` 实际在屏幕上的边界（考虑最大化 / 最小化），
    写入 4 元素 int 列表 [x, y, w, h]。

- static bool Contains(App app, LayerState s, int mx, int my)

- static int Entrance(App app, LayerState s)
  - layer 打开以来的入场进度（千分比）；仍在缓入时
    会重新启动动画计时。

- static int ExitMs()
  - 出场动画时长（layui isOutAnim ~300ms 的量级）。

- static int ExitP(LayerState s)
  - 出场进度（0..1000）：closing 层在关闭沿之后的淡出/位移进度，
    非 closing 层恒 1000。渲染路径用它衰减 shade 与表面。

- static int MulAlpha(int color, int a)
  - 出场衰减：按 a‰ 乘法衰减颜色的既有 alpha（a=1000 原样返回）。
    Style.Fade 是「设为 a‰」的绝对语义，对 0x22000000 阴影、遮罩
    ov.bg 这类预透明色，a=1000 时会跳成全不透明——淡出必须用
    乘法衰减。

- static bool ClosingFrame(App app, LayerState s)
  - !open 层的关闭沿：第一帧启动出场并补触发 Closed（Layer.Close
    路径已在 Close 里触发过，closedRaised 拦住二次触发），其后画
    淡出/上滑收场帧（模态对话框走整屏遮罩+表面，页面窗口走窗口
    自身），走完请求整帧重绘擦净。返回 true 表示本帧处理了该层
    （调用方不必再画它）。

- static void RenderStack(App app, List<LayerState> list)
  - 从后往前绘制页面窗口堆栈（最后一个 = 最顶层）。点击
    任一窗口都会将其置顶；只有指针下最顶层的窗口（或被拖动者）
    才消费鼠标事件。

- static void Render(App app, LayerState s)

- static void FlushCloseRedraw(App app)
  - CloseLast/CloseAll 翻开 open 标志时没有 App 可请求重绘；渲染沿
    结束后在这里兑现（无论该层走的是哪条渲染路径——宿主可能只渲染
    这一个层就返回）。CloseTopmostDialog/Key/Close 已就地请求。

- static void RenderActive(App app, LayerState s, bool active)

- static void RenderClosing(App app, LayerState s, int outP)
  - 出场收场帧（closing 层专用）：与 RenderActive 的画法同一套
    表面/标题栏/正文，但不注册任何命中区（层已在逻辑上关闭，
    点击应穿透到页面）、不画交互按钮的点击态，整体按 outP
    淡出并加速上滑（layui isOutAnim 的等价物）。

- static void RenderLayerBody(App app, LayerState s, int ex, int ey, int ew, int eh, int bar)
  - Layer 正文文本 + 可选的底部按钮（最小化时跳过）。

- static void RenderDialog(App app, LayerState s)
  - 居中的模态对话框：alert / confirm / prompt。将 s.action 设为
    1 (OK)、2 (Cancel) 或 3 (关闭 X)；调用方读取后关闭。

- static void RenderDialogExit(App app, LayerState s, int outP)
  - 模态对话框的出场收场帧：整屏遮罩与表面按 outP 淡出并上滑
    （页面窗口 RenderClosing 的对话框对应物，由 ClosingFrame 驱动）。
    不注册命中、不消费点击——对话框逻辑上已关闭，页面立即恢复可点。

- static void PaintDialog(App app, LayerState s, int x, int y, int w, int h, int a, int shadeId)
  - 对话框的表面绘制：阴影/玻璃/图标标题/正文/嵌入树/操作条/关闭
    钮。打开帧（a=1000）与出场收场帧（a<1000，MulAlpha 乘法衰减
    预透明色）共用；shadeId 是打开帧 BlockHitsBelow 的返回值供
    shadeClose 判定，收场帧传 0。嵌入树/按钮自带配色无法外挂
    alpha，与入场动画一样按不透明绘制。

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
  - 居中的模态确认对话框（OK + Cancel）。dtype 决定图标。

- static LayerState Choose(string title, string body, int dtype, string okText, string altText, string cancelText)
  - 居中的模态对话框，提供三种出路——“保存它，
    丢弃它，还是保持原状”，这是关闭编辑器时必须问的问题。
    主操作报告 1，取消（及 X）报告 2 / 3，备选操作报告 4。

- static LayerState Alert(string title, string body, int dtype)
  - 居中的模态 alert 对话框（仅一个 OK 按钮）。

- static LayerState Prompt(string title, string hint)
  - 带文本输入框的居中模态 prompt 对话框（OK + Cancel）。

- static LayerState PromptWith(string title, string hint, int formType, string value, int maxLen)
  - 强化版 prompt：formType 0 文本 / 1 密码 / 2 多行，value 为
    初值（"" 表示空），maxLen > 0 时限制输入的 UTF-8 字节长度。

- void SetBody(Control v)
  - page 层宿主控件：把一棵保留的组件树挂进窗口正文
    （layui page 层放任意 HTML 的等价物——表单、面板等组合）。
    构造这棵树时必须包在 Layer.BeginBody()/EndBody() 里，否则
    控件在点击回调里中途领宿主计数器的 id，会重演“信号污染”。

- void SizeToContent(App app, int minW, int maxH)
  - layui `area:'auto'` + `offset:'auto'` 的组件层等价物：按正文
    控件树的自然测量尺寸定窗宽高（minW 兜底窄表单），在画布上
    居中；正文自然高度超过可用正文高度时自动包一层 ScrollColumn，
    超出部分交给滚动条。SetBody 之后、Open 之前调用一次；
    maxH > 0 时额外压低正文高度上限（弹窗不顶满视口）。
    
    参数是逻辑 px（内部按 DPI 换算）。深链可能在画布/DPI 定型前
    建窗，这里只记参数；真正的量测与定几何在渲染沿
    ApplySizeToContent 现算（此时画布、DPI、主题都已是真值）。

- void ApplySizeToContent(App app)
  - SizeToContent 的现算体（LayerState 成员）：量正文树 → 定窗
    宽高 → 居中。窗口渲染沿也会调用它（画布定型/重算窗口尺寸），
    保证深链建窗与 DPI 切换后几何都正确。

- void Open()

- string PromptText()
  - 用户在 prompt layer 中键入的文字（未输入时为空）。


## ListColumn (class)

ListView 的一列：表头显示什么、宽度相对其他列
如何（0 = 均分），以及如何从实体读取其文本。

- string title;
  - 表头文本。

- int weight;
  - 相对宽度权重（0 = 均分）。

- bool right;
  - 文本右对齐（数字、大小、日期）。

- CellOf<T> cell;
  - 从行实体取单元格文本的回调。

- ListColumn(string title, CellOf<T> cell)
  - 构造均分宽度的一列。

- ListColumn(string title, int weight, CellOf<T> cell)
  - 构造指定相对宽度的一列。

- ListColumn<T> Right()
  - 让本列文本右对齐（数字、大小、日期）。

- string TextOf(T item)
  - 读取该行实体在本列的文本；无 cell 回调时为空串。


## ListItem (class)

列表里的两行行：标题 + 可选副标题。给 ListView 的模板行、
目录/会话类侧栏当标准行内容——行的视觉（上下对称的留白、
字号阶梯、选中配色、超宽省略号）全部住在 `listitem` 样式
规则里，使用处只喂数据：

ListView<Demo> nav = new ListView<Demo>(
d => new ListItem(d.title, d.desc));

选中配色：宿主把 `Selected` 置真（ListView 模板行在重建时
设置），控件按 `selected` 状态解析样式，标题/副标题换
`listitem::title:selected` / `::desc:selected` 的颜色。
`ListItem.Header`（`.header` 类）是分组标题档：弱色小字，
上方留白把组隔开。外观随皮肤整体变，控件代码不认颜色。

- Binding<string> Text;
  - 标题文本（可绑定）。

- Binding<string> Desc;
  - 副标题文本（可绑定）；空串不画该行。

- bool Selected;
  - 选中态：参与样式状态选择（`selected`）。

- void InitItem(string ttl, string desc)
  - 公共初始化。

- ListItem()
  - 空行（设计器用）。

- ListItem(string ttl)
  - 单行：只有标题。

- ListItem(string ttl, string desc)
  - 两行：标题 + 副标题。

- static ListItem Header(string ttl)
  - 分组标题行（弱色小字，上方留白把组隔开）。

- string Label()
  - 当前标题；Text 为 null 时返回空串。

- string Sub()
  - 当前副标题；Desc 为 null 时返回空串。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"listitem"）。

- int StateNow()
  - 当前状态档：禁用行按 `disabled` 状态解析（弱色），
    其次选中行按 `selected` 状态解析。

- StyleBox ResolvedStyle(App app)
  - 解析 `listitem` 自身样式盒（内边距、行距从这里来）。

- StyleBox TitleStyle(App app)
  - 标题行部件样式（`listitem::title`）。

- StyleBox DescStyle(App app)
  - 副标题行部件样式（`listitem::desc`）。

- override void OnMeasure(App app)
  - 覆写：对称上下内边距 + 标题行高 + 可选副标题行高。

- override void OnPaint(App app)
  - 覆写：按内边距缩进画标题/副标题，超宽省略号截断。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（text/desc/selected/class）。


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
  - 文本列（列布局模式用）。

- RowOf<T> rowOf;
  - 行模板（模板模式用）；与 cols 二选一。

- string Empty;
  - 列表无行时居中显示（"" 则不绘制）。

- int sel;
  - 聚焦/选中行号（-1 = 无）。

- int hover;
  - 本帧悬停的行号（绘制时更新，-1 = 无）。

- bool multi;
  - 多选模式（WithMultiSelect）：Ctrl+点击把行加入/移出
    `marks`，Shift+点击从锚点扩展。`sel` 仍是聚焦行，
    单选调用方不受影响。

- List<int> marks;
  - 多选命中的行号集合（Ctrl+点击维护，不含聚焦行）。

- RowGate<T> onlySelectable;
  - 可选行门（WithSelectable）：null = 全部可选。

- SignalInt scroll;
  - 垂直滚动偏移（宿主可读写以驱动/响应滚动）。

- bool rowsStale;
  - 模板模式：每个构建的子控件分配一个行 id，因此
    未重建任何行的帧里，选择状态也能保留。

- int builtFor;
  - 上次构建模板行时的行数（判断是否需要重建）。

- int rowSeqBase;
  - 模板行独占的 WidgetId 段起点（-1 = 还没建过行）。

- int rowHitBase;
  - 行命中区的稳定 id 块起点与块长（-1/0 = 未预留）：行 i 的命中
    id = rowHitBase + i。必须整块预留而不能逐帧 AllocId——每帧只有
    可见行占号，拖动滚动条时可见行数在 n/n+1 间来回变，其后注册
    的滚动条 id 跟着漂移，pressedId 失配后拖动两三像素就停摆
    （offset 不动 → 可见行数也不翻回 → 直到松手）。

- int rowHitCount;

- static int RowIdReserve()
  - 模板行独占的 id 段长度：够放几百行、每行十来个控件。

- UiEvent Select;
  - 某行被选中（读取 SelectedIndex / Selected）。

- UiEvent Activate;
  - 选中的行再次被点击（打开 / 深入）。

- UiEvent Context;
  - 某行被右键点击。

- void InitList()
  - 公共初始化（各构造共用）。

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
  - 控件类型标识（序列化/设计器用）。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"list"）。

- ListView<T> Bind(List<T> src)
  - 绑定调用方的列表。列表不拷贝，因此这是其实时视图；
    需要重建自定义行的变更之后，调用 Refresh()。

- void Refresh()
  - 标记绑定数据已变更，自定义行将在下一帧
    重建。任意线程调用都安全。

- int Count()
  - 当前绑定数据的行数。

- int SelectedIndex()
  - 选中行号（-1 = 无选中）。

- bool HasSelection()
  - 是否有一行处于选中状态。

- T Selected()
  - 选中的实体；仅在 HasSelection() 为真时有效。

- int ContentHeightForTest(App app)
  - 当前行总高（模板行 = 实测 prefH 累计，列模式 = 行数×行高）。
    滚动条几何与拖拽期望的公共推导点，测试与宿主同式复算用。

- bool Has(int row)
  - `row` 是否在数据范围内。

- T ItemAt(int row)
  - `row` 处的实体；仅在 Has(row) 为真时有效。

- void SelectIndex(int row)
  - 像点击那样移动选中项，发生变化时触发 Select。
    禁用行不接收：与鼠标路径同一道门。

- void ClearSelection()
  - 清空选中与全部多选标记。

- ListView<T> WithMultiSelect()
  - 启用多选（Ctrl+点击切换、Shift+点击范围选择）。

- ListView<T> WithSelectable(RowGate<T> gate)
  - 声明哪些行可选（分组标题档、条件未满足的禁用项等非选项）。
    不可选行对点击/Ctrl+点击/Shift+范围/右键/长按全部无反应，
    悬停与选中带不出现，文本以 `disabled` 状态弱色呈现；
    模板模式下行控件自带的 `Disabled` 同样生效。

- bool IsMarked(int row)
  - `row` 属于多选集合（或为聚焦行）时为 true。

- List<int> SelectedIndices()
  - 所有选中的行索引（标记加聚焦行），升序排列。

- int SelectionCount()
  - 选中的行数。

- ListView<T> OnSelect(Action a)
  - 某行被选中时回调。

- ListView<T> OnActivate(Action a)
  - 选中的行再次被点击（打开 / 深入）时回调。

- ListView<T> OnContext(Action a)
  - 某行被右键点击时回调。

- ListView<T> EmptyText(string text)
  - 列表为空时显示的文本。

- bool Templated()
  - 是否为模板（自定义行）模式。

- int RowHeight(App app)
  - 行高：样式 height 优先，否则主题 large。

- int HeaderHeight(App app)
  - 表头高度：≥2 列且未标 headless 时为主题 small，否则 0。

- void BuildRows(App app)
  - 通过行模板为每个实体重建一个子控件。子控件
    在此测量，因为发现需要重建时，树的测量阶段已越过
    它们。

- int ContentHeight(App app)
  - 行的总高度；当样式表未声明高度时，列布局用它
    来确定列表尺寸。

- int RowExtent(App app, Control row)
  - 模板行实际占高（prefH 未测出时退回统一行高）。

- void EnsureRowIds()
  - 行命中 id 块：每行一个，作为连续块预留（同 Carousel/Collapse
    的 EnsureIds），数据超出块长时整块换新——按倍数增长，持续追加
    的信息流不至于每来一条就换一遍所有行的 id。

- int RowInteract(App app, int index, int x, int y, int w, int h)
  - 处理某行矩形的选择 / 激活 / 右键菜单。返回其注册的 id，
    调用方可据此绘制 hover 状态。

- void NoteSelfDamage(App app)
  - 选中一行只改变列表自己的像素，因此把损伤限定在控件矩形
    内，而不是整窗重绘（弹层里的列表会自动退回整窗重绘：
    覆盖层存在时 NoteDamage 本就不生效）。需要更大范围的宿主
    在自己的 Select/Activate 处理器里调用 RequestRedraw。

- bool Selectable(int row)
  - `row` 是否可选（可用）：在数据范围内、未被 WithSelectable
    的门挡下，且（模板模式）行控件自身没有 Disabled。禁用项
    对一切选择途径关闭——鼠标的五种手势、程序化 SelectIndex、
    悬停高亮，视觉上以 `disabled` 状态呈现弱色。

- void PaintEmpty(App app)
  - 空列表时居中绘制 Empty 提示文本。

- List<int> ColumnWidths(int rowW)
  - 当前盒子的列宽：某列的 weight 是它对整行的占比，
    权重全为 0 时均分。

- void PaintHeader(App app, int headH)
  - 绘制表头：底色、分隔线与各列标题（超宽省略）。

- void PaintColumnRows(App app, int headH)
  - 列布局模式的行区：滚轮/滚动条、逐行命中与单元格文本。

- void ArrangeRows(App app, int headH)
  - 在此盒子内按滚动偏移量布局模板行；
    从 OnPaint 运行，即在子控件绘制自身之前，
    因此树依然是它们坐标的唯一所有者。

- override void OnMeasure(App app)
  - 覆写：模板行按需重建后，内容高度加表头得出偏好高度。

- override void OnPaint(App app)
  - 覆写：按 `list` 样式画可选表面，再画表头与各行
    （模板行走行控件排布，列模式直接绘制）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Select"/"Activate"/"Context"。

- override void BindEvent(string evt, Action a)
  - 将列表的语义事件（Select/Activate/Context）路由到对应的
    UiEvent 字段；其余事件落入 `On` 上的通用事件包
    （其 AddByName 忽略不认识的名称，因此 JSON/设计器中的 `onSelect`
    处理器不会静默地永远不执行）。


## Marquee (class)

跑马灯：一段文本在自己的矩形内匀速向左滚动，滚完一个循环宽度后
无缝衔接，循环往复。仿 Naive UI 的 Marquee 组件：

* `speed` 是滚动速度（像素/秒，默认 48）；
* `autoFill` 为 false（默认）时一个循环等于可视宽，文本完全
滚出左缘后从右缘重新进入（经典公告栏；文本比框宽时取内容宽，
镜像副本在它滚出的同时从右侧补位）；
* `autoFill` 为 true 时按 `gap` 的间距用副本铺满可视宽，
文本再短也没有空档（无缝流）。

Marquee mq = new Marquee();
mq.Text = vm.notice;      // 绑定，每帧重读
mq.Speed(120);            // 更快
mq.AutoFill(true);        // 无缝铺满

相位按真实时间推进（与帧率无关）。控件被销毁重建后从头滚；
要跨帧续滚时传一个宿主持有的 MarqueeAnim：

mq.Animate(Gallery.demoMarquee);

- Binding<string> Text;
  - 可绑定文本：`mq.Text = vm.notice;` 每帧重新读取模型字段
    （编译器降级的 Binding）；字面量则存为常量。

- int speed;
  - 滚动速度（像素/秒）。

- bool autoFill;
  - 为 true 时用内容副本铺满可视宽。

- int phase;
  - 滚动相位（像素）与上次推进时刻。控件跨帧存活时够用；
    每帧重建的宿主改用 MarqueeAnim。

- int lastMs;

- MarqueeAnim anim;
  - 宿主持有的滚动状态（Animate() 传入），非空时优先于自身字段。

- void InitMarquee(string txt)
  - 初始化：以文本注册为控件并置默认值（48px/s、不铺满）。

- Marquee()
  - 空文本跑马灯（之后设 Text；设计器用）。

- Marquee(string txt)
  - 给定初值文本。

- string Str()
  - 当前文本（通过绑定解析）。

- Marquee Speed(int pxPerSec)
  - 设置滚动速度（像素/秒），链式。

- Marquee AutoFill(bool v)
  - 设置是否用副本铺满可视宽，链式。

- Marquee Animate(MarqueeAnim state)
  - 把滚动相位交给 `state`：宿主每帧重建控件时传同一个对象，
    滚动就不会从头再来（同 Carousel.Animate）。

- StyleBox ResolvedStyle(App app)
  - 解析跑马灯自身的样式盒（`marquee` 规则，带缓存）。

- override void OnMeasure(App app)
  - 覆写：高度取样式表声明或一行文本高，宽度取样式表或内容宽。

- override void OnPaint(App app)
  - 覆写：按真实时间推进滚动相位并绘制循环文本（镜像副本补位/
    铺满），持续请求重绘。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（text/speed/autoFill/class）。


## MarqueeAnim (class)

跑马灯的滚动相位，独立成对象，使每帧重建控件的宿主仍能
持有进行中的滚动（同 CarouselAnim 的角色）。

- int phase;
  - 滚动相位（像素，对循环宽取模）。

- int lastMs;
  - 上次推进时刻（毫秒时钟）。

- MarqueeAnim()
  - 构造：相位与时刻归零。


## Menu (class)

带响应式选中的垂直导航菜单（NaiveUI n-menu 风格）。
v1：`RenderVertical`/`HandleClick`——扁平纯文字列表 + SignalInt 索引。
v2：`RenderTree`/`HandleTreeClick`——树形条目（Gui.MenuItem），key 化
选中（SignalString）、子菜单展开、三个批量渲染钩子，对齐 layui「菜单强化」。
`scrollOffset` 滚动列表；完全在 [y, bottomY) 之外的行被剔除。

- static int RenderVertical(App app, int x, int y, int w, int bottomY, int itemH, List<string> items, SignalInt selected, int scrollOffset)
  - v1 扁平文字列表：每行一个连续 id，scrollOffset 按像素上移，
    完全在 [y, bottomY) 之外的行不绘制。返回首行 id 供
    HandleClick 使用。

- static void HandleClick(App app, int firstId, int count, SignalInt selected)
  - 圆角表面上的图标 + 标签变体（模板选择器、向导）。
    行直接把点击的索引写回 `selected`。

- static string KeyOf(MenuItem m)
  - 行的稳定标识：显式 key 优先，空串时退化为 label（layui 的 key/name 对位）。

- static bool IsGroup(MenuItem m)
  - 是否为可展开组：显式 submenu（kind==3），或挂有子项的普通项。

- static List<MenuRow> BuildVisibleRows(List<MenuItem> items, MenuExpandState st)
  - 先序展平为可见行：收起的组只输出组行本身、跳过整棵子树。

- static void AppendRows(List<MenuRow> rows, List<MenuItem> items, int depth, MenuExpandState st)
  - BuildVisibleRows 的递归体：输出本层各行，展开的组递归其子项。

- static bool ExpandAncestors(List<MenuItem> items, MenuExpandState st, string key)
  - 展开 key 的全部祖先组（key 命中某祖先的子树时）。返回是否找到 key。

- static bool ContainsDeep(List<MenuItem> items, string key)
  - items 子树（含组自身）里是否存在 key。

- static void InitDefaults(List<MenuItem> items, MenuExpandState st, List<string> defaultExpandedKeys, string selectedKey)
  - 首次渲染套一次展开策略：有 defaultExpandedKeys 则展开每个 key 的父级链
    并把 key 自身也展开；否则展开选中项的全部父级。

- static int RenderTree(App app, int x, int y, int w, int bottomY, int itemH, List<MenuItem> items, SignalString selectedKey, MenuExpandState state, MenuLabelOf renderLabel, MenuIconOf renderIcon, MenuExpandIconOf expandIcon, List<string> defaultExpandedKeys, int scrollOffset)
  - v2 渲染入口。每个可见行（含不可交互的分隔线/表头/禁用行）占一个 id，
    保证 id 在展平行序里连续，ClickTree 才能按 firstId+idx 反查。
    返回 firstId，与 HandleTreeClick/ClickTree 配对。

- static void PaintTreeRow(App app, Canvas c, Theme t, int id, int x, int iy, int w, int itemH, int accent, MenuRow row, string selKey, MenuExpandState state, StyleBox normal, StyleBox hover, StyleBox selectedStyle, StyleBox disabledStyle, StyleBox headerStyle, StyleBox arrowStyle, StyleBox iconStyle, StyleBox sepStyle, MenuLabelOf renderLabel, MenuIconOf renderIcon, MenuExpandIconOf expandIcon)
  - 绘制单行：分隔线/表头/禁用行各有专门外观；组行画展开箭头，
    选中行铺选中样式并画左缘强调条。

- static string RowText(MenuLabelOf renderLabel, MenuItem m)
  - 行标签：有 renderLabel 钩子时用其返回值，否则条目自带 label。

- static void HandleTreeClick(App app, int firstId, List<MenuItem> items, MenuExpandState state, SignalString selectedKey)
  - v2 点击入口：命中组行只 toggle 展开不动 selectedKey；命中可用叶子写
    selectedKey；禁用/表头/越界 id 安全无操作。

- static bool ClickTree(App app, int firstId, List<MenuItem> items, MenuExpandState state, SignalString selectedKey, int hit)
  - HandleTreeClick 的实现体：hit 为相对 firstId 的行号。命中组行
    只切换展开，命中可用叶子写 selectedKey；返回是否消费了点击。


## MenuExpandState (class)

展开态：以 key 集合记录当前展开的组。宿主持有，跨帧存活；RenderTree
首次调用时按 defaultExpandedKeys / 选中项父级初始化一次。

- List<string> keys;

- bool initialized;

- MenuExpandState()
  - 空展开态：尚未初始化（首次渲染按默认策略展开）。

- bool Contains(string key)
  - key 当前是否处于展开态。

- void Expand(string key)
  - 展开 key（已展开为空操作）。

- void Collapse(string key)
  - 收起 key（不存在为空操作）。

- void Toggle(string key)
  - 切换 key 的展开态。


## MenuRow (class)

树形菜单的一行：条目 + 深度（顶层 0，子项逐级 +1，用于缩进）。

- MenuItem item;

- int depth;

- MenuRow(MenuItem it, int d)
  - 构建一行。


## NumberAnimation (class)

数值滚动动画：在 `Duration` 毫秒内把显示值从 `From` 缓动到
`To`，到位触发一次 `Finish`。数字按 countup.js 风格逐帧变化，
用于把"营业额 12039"这类数据从静止文本变成有科技感的入场。

数值模型是**定点 scaled 整数**：`From`/`To` 存的是显示值 ×
10^`Precision`（口径同 InputNumber）。这样精度、千分位、小数
点串全部在整数域推导，任意时刻的显示文本只由 (From,To,t) 决
定——掉帧、失焦、暂停恢复都不会让数字与时间轴脱钩。便捷方法
`SetRange(from,to)` 接受 double 并按当前 `Precision` 舍入到
scaled，宿主无需手算缩放。

| 属性       | 含义                                           |
|------------|------------------------------------------------|
| From/To    | 起止 scaled 值；任一变化即重摆并重新起跑       |
| Duration   | 动画毫秒，钳 ≥1；默认 2000                     |
| Active     | false 暂停（冻结当前值），true 恢复且无跳变；   |
|            | 默认 true 即首绘自动开播（AutoStart 语义）      |
| Precision  | 小数位 0..6；默认 0                            |
| Separator  | 千分位分组符，默认 ","；"" 关闭分组            |
| Decimal    | 小数点串，默认 "."（俄语场景传 ","）           |
| Prefix     | 前缀（如 "-"、货币符号），默认 ""              |
| Suffix     | 后缀（如 " 条群消息"、"/月"），默认 ""         |

`Separator`/`Decimal`/`Prefix`/`Suffix`/`Precision` 的变化**不**
重摆（只影响显示），`From`/`To`/`Duration` 的变化才重摆。

缓动用全库统一的 Smoothstep（App.Ease），曲线本身不进组件的
逐帧累加——每帧显示值 = ValueAt(From, To, Ease(p)) 现算，与帧
率无关。`Finish` 每轮到位恰触发一次，Restart 或改 To 重摆后
可再次触发；`From==To` 时仍要跑满 Duration 才到位（避免首帧
立刻回调）。

绘制形态：与 Countdown 同构——默认皮肤下套一张圆角卡片
（`numberanim::box`）+ 底部进度条（`::track`/`::fill`，占比为
动画完成度，success/warning/error 角色类给进度条换色）；控件
太小（密集布局内嵌）时退化裸文本。暂停时落到 disabled 皮肤，
数字与面板一起降调；到位不降调（到位是常态，区别于归零）。

NumberAnimation na = new NumberAnimation(12039);
na.Finish += () => { status.Text = "完成"; };
playBtn.Click += () => { na.Restart(); };

// 国际化：俄语小数点是逗号、千分位是空格
NumberAnimation ru = new NumberAnimation(0, 699700699, 2000);
ru.Precision = 3;
ru.Separator = " ";  ru.Decimal = ",";   // -> "699 700,699"

- Binding<int> From;
  - 起点 scaled 值（显示值 × 10^Precision）；默认 0。变化即重摆。

- Binding<int> To;
  - 终点 scaled 值；默认 0（静止）。变化即重摆。

- Binding<int> Duration;
  - 动画时长（毫秒），钳 ≥1；默认 2000。变化即重摆。

- Binding<bool> Active;
  - 是否走动。false 暂停（冻结当前值），true 恢复且无跳变。

- Binding<int> Precision;
  - 小数位 0..6（钳制口径同 InputNumber）；默认 0。

- Binding<string> Separator;
  - 千分位分组符；默认 ","；"" 关闭分组。

- Binding<string> Decimal;
  - 小数点串；默认 "."（俄语场景传 ","）。

- Binding<string> Prefix;
  - 前缀（如负号、货币符号）；默认 ""。

- Binding<string> Suffix;
  - 后缀（如单位）；默认 ""。

- UiEvent Finish;
  - 动画到位（每轮仅触发一次；Restart 或改 To 重摆后再次到位会
    再触发）。

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
  - 初始化：置起止/时长与展示默认值，并立即开跑。

- NumberAnimation()
  - 0 -> 0、2000ms（设计器用，属性随后配置）。

- NumberAnimation(int to)
  - 从 0 滚动到 to，默认 2000ms。

- NumberAnimation(int from, int to)
  - 起止 scaled 值，默认 2000ms。

- NumberAnimation(int from, int to, int durationMs)
  - 起止 scaled 值 + 时长（毫秒，钳 ≥1）。

- int Value()
  - 当前 scaled 显示值（宿主轮询用；推模式请挂 `Finish`）。

- string CurrentText()
  - 当前完整展示串（前后缀 + 格式化数字）。

- void SetRange(double from, double to)
  - 按 double 设起止并立即开跑：入参按当前 Precision 舍入到
    scaled。例 `SetRange(0, 12039)`、`SetRange(0, 24.0)` @2。

- static int ScaleIt(double v, int scale)
  - double -> scaled 整数（四舍五入，向零取整的绝对值舍入）。

- void Restart()
  - 重摆到当前 From/To/Duration 并从头开跑。暂停态下只清零进度，
    不改变 `Active`。

- void Arm(int from, int to, int dur)
  - 重摆到给定起止并从头开跑：进度清零、本轮 Finish 重新武装、
    dur 钳 ≥1。

- void Tick(int nowMs)
  - 推进动画并按需触发 Finish。`nowMs` 由调用方传入
    （绘制路径传 `Window.GetTickMs()`），测试可注入确定时刻。
    走动毫秒只在 Active 为 true 时累加，故暂停-恢复无跳变。

- void SyncProps(int nowMs)
  - 读取绑定快照；From/To/Duration 变化即重摆。展示符（精度、
    分隔符、前后缀）变化只刷新显示，不重摆。Active 落沿在此
    结算/重定时间轴，故需当前时刻（绘制路径传 GetTickMs，
    测试注入确定时刻）。

- string DisplayText()
  - 当前帧完整展示串：按快照精度/分组符格式化当前值并拼前后缀。

- static int ValueAt(int from, int to, int e)
  - 千分比 e（已缓动 0..1000）下 from->to 的插值，四舍五入
    （远离零语义，正负向对称）、long 中间量、int 钳制；
    `e<=0` 取 from、`e>=1000` 取 to 短路，保证落点精确。

- static string Format(int scaled, int precision, string sep, string dec)
  - scaled 定点 -> 展示文本：precision 补零、负号前置、分组符
    只作用整数段、小数点串可配（俄语 dec=","）；sep="" 关闭分组。
    例：1203900@2 "," "." -> "12,039.00"；699700699@3 " " ","
    -> "699 700,699"；-5@2 -> "-0.05"；1000000@0 "" -> "1000000"。

- static string GroupWith(string s, string sep)
  - 整数部分加 `sep` 分组（只认首个小数点串前的部分；小数段
    原样保留）。与 InputNumber.GroupThousands 同算法，但分组符
    参数化且不做"已含逗号原样返回"的编辑缓冲幂等——动画值恒为
    刚生成的规范文本。"-1234.5" @"," -> "-1,234.5"。

- override void OnMeasure(App app)
  - 覆写：宽度取 From/To 两端点文本的较大者（动画中途不重排），
    高度含数值行与进度条。

- string DisplayTextOf(int scaled)
  - 指定 scaled 值的完整展示串（供 OnMeasure 量端点包络）。

- override void OnPaint(App app)
  - 覆写：同步属性、推进动画并绘制数值卡与进度条；走动时按
    16ms 限速请求重绘。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（to/from/duration/active/precision/
    separator/decimal/prefix/suffix/class）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Finish"。

- override void BindEvent(string evt, Action a)
  - 覆写："Finish" 挂 Finish 事件，其余按名称走通用路由。


## PageHeader (class)

页面页眉：返回箭头、标题和可选副标题行。与按钮一样，
它自己处理交互（箭头触发 `Back`），外观由
类和样式部件（`pageheader::back`、`::title`、`::subtitle`）决定：

PageHeader h = new PageHeader { Text = "Details" };
h.Subtitle = "GUI reference";
h.Back.On(vm.GoBack);

- Binding<string> Text;
  - 标题文本（可绑定）。

- Binding<string> Subtitle;
  - 副标题文本（可绑定）；空串不画该行。

- string Icon;
  - 返回图标字形；"" 表示隐藏。

- int wid;

- UiEvent Back;
  - 点击返回箭头时触发（Post 到 UI 线程队列）。

- void InitPageHeader(string ttl, string sub)
  - 初始化：以标题文本注册为控件，置副标题/返回图标并自管 Back 事件。

- PageHeader()
  - 空页眉（设计器用）。

- PageHeader(string ttl, string sub)
  - 标题 + 副标题。

- string Label()
  - 当前标题；Text 为 null 时返回空串。

- string Sub()
  - 当前副标题；Subtitle 为 null 时返回空串。

- StyleBox ResolvedStyle(App app)
  - 解析页眉自身的样式盒（`pageheader` 规则）。

- override void OnMeasure(App app)
  - 覆写：标题行高（有副标题时再加一行）定高，宽度取样式表或
    兜底 320。

- override void OnPaint(App app)
  - 覆写：画返回箭头（hover 态、登记命中并触发 Back）、标题与
    可选副标题行。

- static int TitleFont(App app, string cls)
  - 标题字体：页眉携带的标题类，默认 `h3`。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（text/subtitle/icon/class）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Back"。

- override void BindEvent(string evt, Action a)
  - 将返回箭头的 Back 路由到其 UiEvent 字段；其余事件
    落入 `On` 上的通用事件包（其 AddByName 忽略它
    不认识的名称）。


## Pagination (class)

分页组件（Element Plus 对位）。页数较多时中间用省略号
窗口化：1 … 4 5 [6] 7 8 … 200；省略号本身可点击，向前/
向后整窗翻页（快速跳页）。

两种计页方式：直接给页数 `new Pagination(12, page)`，或给
条目数 + 每页条数（SetItemCount），总页数随之推导、改
pageSize 自动重算。

附属段（都可选）：
- `Jumper(true)`   尾部“跳至 [输入框]”快速跳页，回车提交；
- `ShowSizes(true)` 尾部“n / 页”每页条数下拉（SetPageSizes 定选项）；
- `Prefix/Suffix`   前后缀文案；`ShowTotal` “共 N 页”；
- `PrevText/NextText` 自定义上一页/下一页文字（空 = 箭头图标）；
- `SetOrder`       调整各段展示顺序。

外观来自类列表：`small` 缩小按钮，`pill` 让按钮变圆，
`simple` 把按钮条收窄成“prev [cur] / total next”，当前页
变成可编辑输入框（回车提交）。

Pagination pg = new Pagination(100, page);
pg.Jumper(true).ShowSizes(true);
pg.RenderAt(app, x, y);

- SignalInt model;
  - 内部当前页信号（1 起始）；存在双向绑定时与其保持同步。

- Binding<int> Page;
  - 双向绑定的当前页，从 1 开始。不绑定时用构造器给的信号。

- int totalPages;
  - 显式页数（未启用 item-count 模式时生效）。

- UiEvent Change;
  - 页码变化后触发（SetPage/点击翻页）。

- UiEvent SizeChange;
  - 每页条数变化后触发（ShowSizes 下拉选择）。

- int baseId;
  - 命中 id 块基址（EnsureIds 保留）：prev、各页码槽（含省略号）、
    next、页码输入框、sizes 箭头。

- int idCount;
  - 当前 id 块容量；够用时复用，不足时整块重分配。

- bool useItemCount;
  - 是否处于 item-count 模式（总页数由条目数推导）。

- int itemCount;
  - 条目总数（item-count 模式）。

- int pageSize;
  - 每页条数（>0）。item-count 模式下参与计页，其余模式仅由宿主读取。

- List<int> pageSizes;
  - ShowSizes 下拉的候选条数；空列表 = 不显示 sizes 段。

- int sizeIdx;
  - pageSizes 中当前 pageSize 的下标；无匹配为 -1。

- bool sizeOpen;
  - sizes 下拉弹层是否打开。

- int sizeBoxX;
  - sizes 下拉弹出层的锚点几何（绘制时登记，覆盖阶段读取）。

- int sizeBoxY;

- int sizeBoxW;

- int sizeBoxH;

- string prefixText;
  - 前缀文案；空串 = 该段不渲染。

- string suffixText;
  - 后缀文案；空串 = 该段不渲染。

- string prevText;
  - 自定义上一页 / 下一页文字；空串用箭头图标（Element Plus prev/next 插槽）。

- string nextText;

- bool showTotal;
  - 是否显示「共 N 页」总数文本。

- bool jumper;
  - 是否显示跳页输入框。

- string jumperLabel;
  - 跳页标签文字（空 = 默认「跳至」）。

- string jumperSuffix;
  - 跳页输入框之后的后缀文字（空 = 不渲染）。

- List<string> order;
  - 段名列表（prefix/total/pager/suffix/jumper/sizes 子集）；
    null = 按 enablement 的默认顺序。宿主一经 SetOrder 即全权指定，
    缺 "pager" 会被补回。

- int fieldId;
  - 当前页码输入框的命中 id（每次绘制刷新）。

- string fieldBuf;
  - 输入框编辑缓冲；聚焦时显示，提交或失焦即清空。

- int pagerCount;
  - 窗口页码按钮数（Element Plus pager-count），奇数、>=5。

- void InitPagination(int total, SignalInt m)
  - 初始化：置页数/当前页信号与各段默认值（每页 10 条、7 个
    窗口页码）。

- Pagination():this(1)
  - 构造：默认 1 页，内部信号从第 1 页开始。

- Pagination(int total)
  - 构造：显式页数，内部新建当前页信号。

- Pagination(int total, SignalInt m)
  - 构造：显式页数 + 共享当前页信号。

- static Pagination Of(int total, SignalInt m)
  - 工厂：显式页数 + 共享当前页信号。

- static Pagination WithCount(int items, int size, SignalInt m)
  - item-count 模式：由条目数与每页条数推导总页数
    （Element Plus item-count + page-size）。当前页随之钳制。

- void SetTotal(int total)
  - 显式页数（退出 item-count 模式）。

- int TotalPages()
  - 总页数：item-count 模式推导，否则显式值。

- int Total()
  - TotalPages 的别名（Naive UI 的 page-count 惯用名）。

- void SetItemCount(int items)
  - 切到 item-count 模式：给条目数，页数按每页条数推导。

- int ItemCount()
  - item-count 模式下的条目数。

- bool UsesItemCount()
  - 是否处于 item-count 模式。

- void SetPageSize(int size)
  - 每页条数（item-count 模式下决定页数）；当前页随之钳制。

- int PageSize()
  - 当前每页条数。

- void SetPageSizes(List<int> sizes)
  - ShowSizes 的候选条数（宿主列表，不复制）。

- List<int> PageSizes()
  - 候选条数列表（可能为宿主列表本体，勿原地改）。

- void SetPageSizesText(string text)
  - `"10, 20, 50, 100"` ↔ 候选条数列表（设计器 / JSON 往返）。

- string PageSizesText()
  - 候选条数的序列化文本形式（逗号分隔）。

- Pagination Jumper(bool v)
  - 是否显示「跳至第 N 页」输入框。

- bool HasJumper()
  - 当前是否启用了跳页输入框。

- void OpenSizes()
  - 编程入口：打开/关闭 sizes 下拉弹层（gallery 截图与测试用，
    用户路径是点击 sizes 框）。

- void CloseSizes()
  - 编程入口：关闭 sizes 下拉弹层。

- bool SizesOpen()
  - sizes 下拉弹层当前是否打开。

- Pagination ShowSizes(bool v)
  - 显示「条/页」下拉（首次开启时填充 10/20/50/100 默认候选）。

- bool HasSizes()
  - 是否配置了 sizes 候选（决定是否渲染下拉）。

- Pagination Prefix(string t)
  - 各段文案定制：前缀 / 后缀 / 上页 / 下页 / 总数开关 /
    跳页标签与后缀（Naive UI slot 对位）。

- Pagination Suffix(string t)
  - 设置后缀文案（默认空 = 该段不渲染）。

- Pagination PrevText(string t)
  - 设置上一页文字（默认空 = 用箭头图标）。

- Pagination NextText(string t)
  - 设置下一页文字（默认空 = 用箭头图标）。

- Pagination ShowTotal(bool v)
  - 设置是否显示「共 N 页」总数文本（默认关）。

- Pagination JumperLabel(string t)
  - 设置跳页标签文字（默认空 = 「跳至」）。

- Pagination JumperSuffix(string t)
  - 设置跳页输入框后的后缀文字（默认空 = 不渲染）。

- void SetPagerCount(int n)
  - 窗口页码按钮数：规范化为 >=5 的奇数（Element Plus pager-count）。

- int PagerCount()
  - 当前窗口页码按钮数。

- List<string> EffOrder()
  - 段顺序：宿主列表优先（缺 pager 补回），否则按
    prefix/total/suffix → pager → jumper → sizes 的默认顺序，
    只保留启用中的段。

- void SetOrderText(string text)
  - `"jumper, sizes, pager"` ↔ 段顺序（设计器 / JSON 往返）；
    空串或无有效段名 = 恢复默认顺序。

- string OrderText()
  - 段顺序的序列化文本形式（设计器 / JSON 往返）。

- static int MeasureWidth(App app, int totalPages, int current)
  - 给定页数下控件 pager 段的总像素宽度（纯按钮条，供
    DataTable 等调用方定位）。与绘制所用的条目布局一致。

- int EllipsisStep()
  - 省略号快速跳页步长：向前/向后翻一整窗（默认 7 槽即 ±5，
    Element Plus 行为）。

- static List<int> BuildPages(int totalPages, int current)
  - Element Plus 页码窗口算法。值 0 表示省略号占位：
    - 总页数 <= 槽位：全部平铺；
    - 当前页靠前 / 靠后：领起 / 收尾长串；
    - 居中：当前页 ± half。
    默认 7 槽的页码窗口（BuildPagesSlots 的便捷重载）。

- static List<int> BuildPagesSlots(int totalPages, int current, int slots)
  - 可指定槽位数的页码窗口；值 0 = 省略号占位。

- int ButtonSize(App app, StyleBox s)
  - 页码按钮边长：`small` 类用 heightSmall，否则 heightMedium（样式可覆写）。

- int FontSize(App app, StyleBox s)
  - 页码字号：`small` 类取 small 档，否则 medium 档（样式可覆写）。

- int Radius(App app, StyleBox s, int btnSize)
  - 按钮圆角：`pill` 类取按钮半高（全圆），否则 borderRadiusSmall。

- int ArrowWidth(App app, string txt, int btnSize, int fs)
  - 自定义 prev/next 文字的按钮宽度：不小于方形按钮，文字
    两侧各留一点呼吸边（箭头图标模式恒为方形）。

- void EnsureIds(int entries)
  - Prev、每个窗口条目一个 id（含省略号，使 id 与
    布局对齐）、next、页码输入框与 sizes 箭头；作为
    连续块一次性保留。

- void SetPage(int pg)
  - 稳定的当前页读写：同步 model 与双向绑定，触发 Change。
    编程入口（测试 / 宿主），不检查禁用态。

- void GoTo(App app, int pg)
  - 用户路径的 GoTo：禁用态直接忽略。

- override void SyncBinding()
  - 把外部绑定（Page）的页码拉入本地信号，并把越界页码静默钳回。

- void Normalize()
  - pageSize / itemCount / total 变更后的统一收口：钳制当前页、
    重找 sizes 选中项。

- override void OnMeasure(App app)
  - 覆写：高取页码按钮尺寸，宽按生效段顺序累加（与绘制同算法）。

- int TotalWidth(App app, StyleBox s, int fs, int gap)
  - 全控件宽度：按生效顺序累加各段（与绘制同一套算法），
    供宿主居中 / 右对齐放置。

- int SegmentWidth(App app, StyleBox s, int fs, string seg)
  - 单段宽度（0 = 该段不显示）。必须与 OnPaint 的摆放一致。

- string TotalText()
  - 「共 N 页」总数文本。

- string JumperLabelText()
  - 跳页标签文字：jumperLabel 非空用它，否则默认「跳至」。

- string SizeText(int n)
  - sizes 框与选项的文字：“n / 页”。

- override void OnPaint(App app)
  - 覆写：同步绑定后按 simple/完整两种布局摆放并绘制各段
    （页码、翻页箭头、跳页输入、sizes 下拉），sizes 开着时
    追加帧末覆盖层。

- void HandleInput(App app, List<int> pages, int nextId, int fldId, int chevId, bool off)
  - 主阶段输入：页码点击、省略号整窗翻页、输入框键入、
    sizes 箭头开合。sizes 弹层开着时主阶段不吃任何输入，
    由覆盖阶段统一处理（点外部 = 只关闭）。

- void CommitField(App app, int fldId)
  - 提交页码输入框：纯数字才接受，钳到 [1, 总页数]；
    空串 / 非法恢复当前页。成功后保持焦点（Element Plus 行为）。

- void PaintField(App app, int id, int x, int y, int w, int h, int radius, int fontSize, int current, bool off)
  - 页码 / 跳页输入框。聚焦时画光标，回车提交、Esc 还原、
    点外面交回焦点。

- void PaintArrow(App app, string glyph, int id, int cx, int w, int by, int btnSize, int radius, int ico, bool atEnd, StyleBox item, StyleBox arrow)
  - 一个 prev/next 控件：一个表面、一个在范围末端（或整体
    禁用）置灰的箭头 / 自定义文字，以及它的命中区域。
    自定义文字时按钮加宽为文字宽度（w 传入 ArrowWidth 的值），
    文字不再溢出方形边界。

- void PaintEllipsis(App app, int id, int cx, int btnSize, int radius, int fontSize, int current, bool off)
  - 省略号：悬停时提示可以整窗翻页（变为箭头），点击翻页。

- void PaintPage(App app, int id, int pg, int cx, int btnSize, int radius, int fontSize, int current, bool off)
  - 一个页码按钮：normal / hover / selected 三态，禁用整体置灰。

- void PaintSizesBox(App app, int id, int x, int y, int w, int h, int radius, int fontSize, bool off)
  - “n / 页”每页条数框：一张表面 + 当前值 + 下拉箭头；
    选项列表在覆盖阶段绘制（见 OnPaintOverlay）。

- void PaintSimple(App app)
  - `simple` 类：prev 箭头、可编辑的当前页输入框、
    “/ total”文本、next 箭头。

- void HandleSimpleInput(App app, int nextId, int fldId, bool off)
  - `simple` 布局的输入处理：键盘/点击逻辑与标准布局的
    HandleInput 相同，但没有页码按钮与 sizes 箭头。

- override void OnPaintOverlay(App app)
  - sizes 弹层：锚定在条数框下方（超出窗口底部则翻转到上方），
    每行一个候选条数。行由几何驱动（不注册命中 id）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（total/page/itemCount/pageSize/
    pageSizes/showTotal/jumper/prefix/suffix/prevText/nextText/
    pagerCount/class）。

- override string GetExtra(string key)
  - 设计器读取 sizes 候选、段顺序与跳页文案四个文档键；
    其余返回 ""。

- override bool SetExtra(string key, string val)
  - 设计器写入 sizes 候选、段顺序与跳页文案；其他键返回 false
    交给基类。

- override string GetProp(string key)
  - 覆写：列表/派生键（pageSizes/order）的应答走各自的序列化器，
    不落 Props() 的快照；其余键交基类。

- override void SetProp(string key, string val)
  - 有副作用收口的属性（钳制 / 重算 / 重找选中项）在基类
    写完后统一走 Normalize。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"/"SizeChange"。

- override void BindEvent(string evt, Action a)
  - 覆写："Change"/"SizeChange"（别名 "SizeChanged"）各挂对应
    UiEvent，其余按名称走通用路由。


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
  - 标题文本（Titled 设置）。

- int surfaceColor;
  - Plain 的填充色（0 = 皮肤 panel::plain 背景部件）。

- FxOptions fx;
  - 可选动效（WithFx 设置）；null = 无。

- void InitPanel(string name, int st)
  - 初始化：以给定名字注册并自持表面样式，置面板形态（见 style）。

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
  - 纯色表面：只填 color（0 = 用皮肤 panel::plain 背景部件），
    无边框/圆角/投影。返回自身。

- Panel Layout()
  - 纯布局宿主：不画表面、不取卡片内边距，只把自己的矩形
    交给子控件。放在矮行（工具栏、面板头）里的容器必须用它：
    卡片默认的 paddingLarge 会吃掉一行的全部高度，子控件只能
    溢出到宿主外面。

- Panel Titled(string t)
  - 带标题的分区：表面 + 标题行 + 细分隔线，标题行高度计入
    顶部内边距。返回自身。

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
  - 控件类型标识（序列化/设计器用）。

- override string StyleType()
  - 流式容器（Panel.Row/Panel.Column）只做布局：不绘制任何表面，
    因此也不应继承样式表的 `panel` 规则——
    否则皮肤的 `panel { padding: 18px; backdrop-filter: blur(...) }` 会
    吃掉 Row 的内容框（使其各行互相重叠）并
    给没有内容的容器做毛玻璃。样式表可用 `stack`
    定位它们。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（style/layout/title）。

- override void OnMeasure(App app)
  - 覆写：流式容器按已测子控件加间距推出首选尺寸（显式声明优先）；
    其余形态只在调用方未固定内边距时落主题默认值（带标题时把
    标题行计入顶边距）。

- bool StyledSurface(App app, string type, StyleBox s)
  - 当解析出的盒子 `s` 与该类型的裸规则解析结果不同时返回 true——
    即 `<type>.<class>` / `#<name>` 规则或内联 Bg()/Gradient()/Radius()
    设置器专门给了这个容器一张表面。

- override void OnPaint(App app)
  - 覆写：按形态画表面——流式容器仅在样式表专门给了背景/边框时
    照画；卡片/纯表面/带标题分区走 `panel` 样式规则，带标题时
    再画标题行与细分隔线。

- override int StylePadT()
  - 标题行是面板自身的装饰：样式表 `padding` 只能叠加，
    绝不能把内容框顶回标题下面（与 Card 同一约定）。

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
  - 初始化：以 "popover" 注册，置触发文本/标题/正文/面板宽并保持关闭。

- Popover(string trig, string ti, string bo, int w)
  - 有状态模式构造函数。

- bool IsOpen()
  - 面板当前是否打开。

- int Render(App app, int x, int y, int w, int h)
  - 绘制触发器并处理点击开合；打开时把面板登记为帧末覆盖层
    （经 OnPaintOverlay 绘制，点击外部或 Esc 关闭）。返回触发器 id。

- override List<PropSpec> Props()
  - 覆写：设计器属性（触发文本/标题/正文/面板宽/开合缺省）。

- override void OnPaintOverlay(App app)
  - 延迟面板：在所有内容之后由 App.RunOverlays 绘制并派发事件。

- override void OnMeasure(App app)
  - 覆写：触发器默认一行高、120 逻辑像素宽。

- override void OnPaint(App app)
  - 覆写：在自身矩形内画触发器并处理点击开合（转发到 Render）。

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

进度指示器（Naive UI Progress 的完整形状集）。类型
default(1)/info(2)/success(3)/warning(4)/error(5) 选配色；
形状有四种：线性轨道条（默认）、圆环 `Ring()`、多圆环
`Multi()`（`AddCircle` 逐环加值）、仪表盘 `Dashboard()`
（下方留缺口的 270° 弧）。

其余能力对齐 Naive UI：`showIndicator` 百分比文本、
`strokeWidth` 轨道高度 / 圆环厚度、`processing` 处理中
滚动斜纹、`FillColor`/`RailColor` 覆盖皮肤颜色
（color / rail-color）、`Format` 自定义指示文本模板、
`Size` 圆环直径。

new Progress(66, 3)                          // 绿色线性条
new Progress(72, 0).Ring()                   // 圆环
new Progress(0, 0).Multi().AddCircle(72, 0).AddCircle(45, 2)
new Progress(60, 0).Dashboard().Processing(true)

- int percent;
  - 未绑定 data 时的本地百分比（绘制时钳到 0..100）。

- Binding<int> data;
  - 绑定百分比：`p.data = task.progress;` 每帧重新读取模型字段
    （编译器降级的 Binding）；设置后覆盖 `percent`。

- int ptype;
  - 颜色变体：1=default 2=info 3=success 4=warning 5=error。

- int shape;
  - 0 = 线性条，1 = 圆环，2 = 多圆环，3 = 仪表盘。

- bool showIndicator;
  - 显示百分比文本（Naive UI show-indicator）。默认 true。

- int strokeWidth;
  - 轨道条高度 / 圆环厚度（Naive UI stroke-width）。0 = 用皮肤默认。

- bool processing;
  - 处理中（Naive UI processing）：线性条填充上叠加流动斜纹。

- int fillColor;
  - 自定义填充色（Naive UI color）。0 = 跟随皮肤。

- int railColor;
  - 自定义轨道色（Naive UI rail-color）。0 = 跟随皮肤。

- string format;
  - 指示文本模板（Naive UI indicator）：`{0}` 换成百分比，
    如 `"已完成 {0}%"`；空 = 默认 `"N%"`。

- int ringSize;
  - 圆环直径（CSS px，ring/multi/dashboard）。0 = 跟随布局与皮肤。

- List<int> circlePcts;
  - 多圆环各环的百分比与颜色变体。`AddCircle` 填充；设计器 /
    .zform 经 `circles` 额外属性按 `"72:0,45:2"` 原样回填。

- List<int> circleTypes;

- void InitProgress(int pct, int pt)
  - 初始化：置百分比/变体并清零其余字段（条形、显示指示文本）。

- Progress():this(0, 1)
  - Default design-time constructor; properties configure the value later.

- Progress(int pct, int pt)
  - 有状态模式构造函数：`Progress p = new Progress(60, 3);`

- void SetPercent(int pct)
  - 设置百分比并同步到绑定（data 非空时写回模型字段）；不做
    钳制，绘制时钳到 0..100。

- int Percent()
  - 当前百分比：设置了绑定时返回绑定的模型值。

- Progress Ring()
  - 将此实例切换为圆环形状（可链式调用）。

- Progress Multi()
  - 切换为多圆环：每个 `AddCircle` 的值一圈，从外到内排布。

- Progress Dashboard()
  - 切换为仪表盘：起于左下、止于右下的 270° 弧，底部留缺口。

- Progress AddCircle(int pct, int pt)
  - 多圆环追加一环（百分比 + 颜色变体），返回自身供链式调用。

- Progress Processing(bool on)
  - 处理中开关（可链式调用）：线性条填充上叠加滚动斜纹。

- Progress StrokeWidth(int px)
  - 轨道条高度 / 圆环厚度（Naive UI stroke-width），可链式调用；
    0 恢复皮肤默认。

- Progress FillColor(int argb)
  - 覆盖填充色（0 恢复皮肤），可链式调用。

- Progress RailColor(int argb)
  - 覆盖轨道色（0 恢复皮肤），可链式调用。

- Progress Format(string tmpl)
  - 自定义指示文本模板，`{0}` 为百分比，可链式调用。

- Progress Size(int px)
  - 圆环直径（CSS px），可链式调用。

- Progress ShowText(bool on)
  - 开关百分比文本，可链式调用。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- static string TypeClass(int pt)
  - 变体号 → 皮肤类：2=info 3=success 4=warning 5=error，
    其余为 default（返回 ""）。

- string VariantClass()
  - 当前实例变体对应的皮肤类。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（percent/type/shape/showIndicator/
    strokeWidth/processing/color/railColor/format/ringSize）。

- override string GetProp(string key)
  - 覆写：percent 的应答是有效值（可能来自绑定），circles 的
    应答是环值序列化——都不落 Props() 快照；其余键交基类。

- override void SetProp(string key, string val)
  - 覆写：percent 只写本地字段（有效值可能来自绑定），circles
    走 SetCircles 重建环列表；其余键交基类。

- override string GetExtra(string key)
  - 显示的百分比与多圆环环值可能来自绑定 / 列表，
    因此 percent 只写本地字段，circles 是列表属性。

- override bool SetExtra(string key, string val)
  - 设计器写入 `percent` 与 `circles`（多圆环 "72:0,45:2"）；
    其他键返回 false 交给基类。

- string CirclesSpec()
  - 多圆环环值序列化为 `"72:0,45:2,30:3"`（百分比:变体）。

- void SetCircles(string spec)
  - 按 `"72:0,45:2"` 文本重建多圆环环值（缺失的变体按 0）。

- string IndicatorText(int pct)
  - 指示文本：`format` 为空时是默认的 `"N%"`，否则把模板里的
    每个 `{0}` 换成百分比数字。

- void PaintBar(App app, int pct)
  - 线性轨道条：轨道 + 填充 + 右侧指示文本；processing 时在填充上
    叠加流动斜纹并驱动该区域重绘。

- void PaintRing(App app, int pct, int startDeg, int totalDeg)
  - 圆环通用绘制：`startDeg`/`totalDeg` 传入 0/360 即圆环，
    135/270 即仪表盘（起于左下、底部留缺口；角从 12 点
    方向顺时针，与 Canvas.FillSector 一致）。

- void PaintMulti(App app)
  - 多圆环：每环一值，从外到内排布，中心逐环显示各自的
    百分比（颜色跟随该环填充色）。

- int CircleType(int i)
  - 第 i 环的颜色变体（越界按 default）。

- override void OnMeasure(App app)
  - 覆写：圆环族取直径（ringSize 优先），条形取轨道高与指示
    文本行高的较大者。

- override void OnPaint(App app)
  - 覆写：按 shape 分派到多圆环/仪表盘/圆环/条形的绘制。

- static int Clamp(int percent)
  - 钳制到 [0, 100]。

- static int MixWhite(int c, int num)
  - 把颜色向白色混合 num%（0..100），用于处理中斜纹高光。
    颜色按 0xAARRGGBB 存（int 为 64 位，各字节非负）。

- static bool RectOnScreen(App app, int x, int y, int w, int h)
  - 矩形与窗口表面重叠时返回 true；处理中斜纹只在
    真正可见时驱动重绘，屏幕外不空转。


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


## QrCode (class)

二维码（对标 layui qrcode 演示）。内容经 `Gui.QrEncoder` 编码为
模块矩阵，逐模块 `Canvas.FillRect`（行游程合并）绘制。

QrCode qr = new QrCode("https://zan-lang.dev");
qr.Ecl = "H";
qr.Logo = "assets/logo.png";   // 带 logo 自动升 H（同 layui）
qr.Render(app, x, y);
qr.SavePng("qr.png");          // 「下载」
string svg = qr.ToSvg();       // 「渲染类型 = svg」

颜色/尺寸/留白走 CSS 规则 `qrcode`（color / background-color /
width）；显式属性值 0 = 交给 CSS。编码结果按 (Text,Ecl,Logo)
指纹缓存，属性不变不重算。

- Binding<string> Text;
  - 二维码内容（响应式）。

- Binding<string> Ecl;
  - 纠错级别 "L"/"M"/"Q"/"H"（默认 "M"）。

- Binding<int> Size;
  - 边长（逻辑 px，含留白）；0 = 按 CSS 盒 / 默认。

- Binding<int> Dark;
  - 深色模块 0xAARRGGBB；0 = CSS `color`，再退黑。

- Binding<int> Light;
  - 浅色底色 0xAARRGGBB；0 = CSS `background-color`，再退白。

- Binding<int> Margin;
  - 留白模块数（默认 4，ISO 静区建议值）。

- Binding<string> Logo;
  - logo 图片（文件路径 / mem: key）；空 = 无 logo。

- Binding<int> LogoPct;
  - logo 边长占整码的百分比（默认 18，钳到 1..30）。

- UiEvent Change;
  - 内容/属性变化时触发。

- string cacheKey;
  - (Text,Ecl,Logo) 指纹缓存的编码结果。

- QrMatrix cacheMx;

- int baseId;
  - 整码矩形命中 id（Render 返回值）。

- void InitQr()
  - 公共初始化（两个构造共用）。

- QrCode()
  - 默认设计期构造。

- QrCode(string text)
  - 内容构造：`QrCode qr = new QrCode("https://...");`

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（text/ecl/size/dark/light/margin/
    logo/logoPct）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"。

- override void BindEvent(string evt, Action a)
  - 覆写："Change" 挂 Change 事件，其余按名称走通用路由。

- override void OnMeasure(App app)
  - 覆写：按模块数与盒宽量出正方形首选尺寸。

- override void OnPaint(App app)
  - 覆写：在自身位置绘制二维码（转发到 Render）。

- int Render(App app, int x, int y)
  - 手动绘制（宿主自由布局时）。返回命中 id。

- void PaintLogo(App app, Canvas c, QrMatrix mx, int margin, int cell, int x, int y)
  - logo 叠加：中心衬底（浅色）+ 等比缩放绘制。衬底盖住模块，
    因此 Matrix() 把纠错自动升到 H。

- int BoxPx(App app, int mods)
  - 生效盒边长（物理 px）：显式 Size 优先，其次 CSS width，
    再退每模块 6 逻辑 px 的默认。

- int EffectiveDark(App app)
  - 生效深色：显式 Dark 优先，其次 CSS `color`，再退黑。

- int EffectiveLight(App app)
  - 生效浅色：显式 Light 优先，其次 CSS `background-color`，再退白。

- QrEcl EffectiveEcl()
  - 生效纠错级别：显式 Ecl 为基，带 Logo 时保底 H（同 layui）。

- QrMatrix Matrix()
  - 编码结果（指纹缓存）。超长内容抛异常——上层（画廊属性面板）
    自己捕获；缓存里保留旧矩阵的语义由调用方保证。

- string ToSvg()
  - 当前二维码的 SVG 源（「渲染类型 = svg」）。headless 导出无
    App 可用，颜色取显式 Dark/Light，未设时退黑白。

- byte[]ToPngBytes()
  - 当前二维码的 PNG 字节（「下载」落盘前的中间步）。

- int SavePng(string path)
  - 写 PNG 文件并返回字节数——截图「下载」按钮的直接入口。

- int ExportBox()
  - 导出用盒边长（逻辑 px）：显式 Size 优先，否则按模块数×6。

- int ExportDark()
  - 导出用深色：显式 Dark 优先，否则纯黑。

- int ExportLight()
  - 导出用浅色：显式 Light 优先，否则纯白。

- static string SvgOf(QrMatrix mx, int margin, int dark, int light, int boxPx)
  - 静态导出：不经控件，由矩阵直接出 SVG（深色行游程合并成 rect）。

- static string HexColor(int argb)
  - 0xAARRGGBB → "#rrggbb"（alpha 忽略，SVG/PNG 全不透明底）。

- static string HexNib(int v)
  - 0–15 → 一位十六进制小写字符。

- static byte[]PngOf(QrMatrix mx, int margin, int dark, int light, int boxPx)
  - 静态导出：PNG（8bit RGB，模块最近邻放大；签名/IHDR/IDAT/IEND
    手工组包，压缩走 System.IO.Compression，CRC 走纯 Zan Crc32）。

- static byte[]Ihdr(int w)
  - 13 字节 IHDR 数据（宽=高=w、8bit、RGB，压缩/滤波/隔行取默认）。

- static int WriteChunk(byte[]dst, int at, byte b0, byte b1, byte b2, byte b3, byte[]data)
  - 写一个 PNG 分块（长度/类型/数据/CRC），返回新偏移。


## Radio (class)

带响应式 int 绑定（选中索引）的单选按钮。

C# 风格的有状态实例（每个选项一个）：
Radio a = new Radio(0, "Option A");
a.Change += () => { ... };
a.Render(app, x, y, "Option A");
为兼容现有调用方保留旧的立即模式静态方法。

- SignalInt model;
  - 组内共享的选中信号：值为选中项的 optionValue（控件族内部
    组装；应用代码经 data 绑定到同一模型字段）。

- Binding<int> data;
  - 双向绑定的选中值：`r.data = form.gender;` 使模型字段与组选择
    双向保持同步（编译器降级的
    Binding）。组内每个单选按钮都绑定同一字段。

- int optionValue;
  - 本按钮代表的选项值（组信号等于它时本按钮选中）。

- int wid;

- string label;
  - 按钮文本。

- Binding<string> Size;
  - 尺寸档位（Naive UI size prop）：tiny/small/medium/large，
    档位映射为 `radio.small` / `radio.large` 样式类（几何在
    base.css 的尺寸规则里）。

- Binding<bool> Bordered;
  - 边框变体（Element Plus el-radio border）：整个选项画成带
    描边圆角盒，选中描边走强调色，语义色类照常联动。

- UiEvent Change;
  - 该选项被选中时触发（C# 风格：`r.Change += h;`）。

- void InitRadio(SignalInt m, int ov, string lbl)
  - 初始化：组信号、本项选项值与标题。

- Radio()
  - Default constructor used by .zform and the string-kind registry.

- Radio(int ov, string lbl)
  - 自持信号：`Radio a = new Radio(0, "Option A");`，随后通过 `.data`
    在组内每个单选按钮上绑定同一模型字段——调用处无需任何信号
    连接代码；组互斥由共享字段经 SyncBinding 协调。

- Radio(SignalInt m, int ov, string lbl)
  - 共享组信号构造器——**控件族内部组装专用**（ChoiceGroup 以此
    把子 Radio 接到组状态上）。应用代码不要用：外部通道只有
    `Radio(ov, lbl)` + `data`（§6.1）。

- bool IsSelected()
  - 是否为当前选中项：绑定的组信号值等于本按钮的
    optionValue。同一组内恰好只有一个按钮为 true。

- void SetLabel(string v)
  - 运行期改写标题（老窗体切优化目标时改档位文案
    「低 62-75%」/「低 0.05-0.07元」）。不触发重绘，调用方需自行
    RequestRedraw。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override Binding<string> SizeOf()
  - 覆写：返回 Size 绑定字段（设计器经它发现尺寸档位绑定）。

- void ApplyVariants()
  - 把尺寸档位与边框变体落到样式类上（Switch 的既有做法：
    AddClass 幂等，档位类不污染用户自设的语义色类）。

- override void SyncBinding()
  - 将绑定的模型值拉入本地信号（model -> UI）。

- int Render(App app, int x, int y, string label)
  - 立即模式渲染：绘制本按钮并处理点击/键盘（空格或回车
    选中）。选中状态变化时写组信号与 data 绑定、Raise Change 并请求
    重绘；已选中再点不重复触发。返回控件 id。

- override List<string> Events()
  - 设计器事件清单：公共事件外加 Change。

- override List<PropSpec> Props()
  - 设计器属性清单：label(=text)、option、value（优先指向
    活的外部绑定，§6.1 单协议）、size 档位、bordered 变体。

- override void BindEvent(string evt, Action a)
  - 事件路由：Change 走语义事件，其余按名转发给 On。

- int PaintStyled(App app, int id, int x, int y, string text, bool selected)
  - 立即模式绘制主体：解析根/指示环/圆点/标签四个样式
    分区，处理选中淡入动画（约 130ms）与边框变体内边距，最后登记
    命中矩形。颜色全部来自 radio::indicator / ::dot 的 CSS 规则。

- override void OnMeasure(App app)
  - 测布局：按样式规则（含边框变体）计算首选宽高，
    宽度兜底 = 左右内边距 + 指示环直径 + 间距 + 文本宽。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = 在自身矩形上 Render。


## RadioButton (class)

连体按钮段（Element Plus el-radio-button 的对应物）：共享组选中
信号（写选项 value），外观全走 `radiobutton` 样式族。接缝由皮肤
承担：基础规则 `border-left-width: 0`，行首段（组每帧写的
`rb-first` 类）恢复左边框，外侧圆角由首/尾规则给；选中段由组最
后绘制并向左右各外扩 1px 盖住接缝，强调色描边压在中性色上。

段一般不单独使用：RadioGroup 的按钮形态或 RadioButtonGroup 直接
挂一排段。独立保留模式（.zform / 注册表）也可用：自持信号、
整段独立成胶囊。

- SignalInt model;
  - 所属组共享的选中信号（值为选中选项的 value）。

- int optionValue;
  - 本段代表的 value。

- int wid;
  - 段命中 id。

- string label;
  - 段文字。

- string vcls;
  - 组每帧写：档位类（small/large）与接缝类（rb-first/rb-last）。
    只参与样式解析，不污染用户的 Class。

- string seamCls;

- int mw;
  - 组 OnMeasure 测好的自然宽高（排布跨帧复用，不需要 App）。

- int mh;

- int row;
  - 组 LayoutRows 写回的行归属（行首/行尾接缝判定用）。

- UiEvent Change;
  - 该选项被选中时触发（C# 风格：`b.Change += h;`）。

- void InitRadioButton(SignalInt m, int ov, string lbl)
  - 公共初始化：分配命中 id、记录 value 与标签。

- RadioButton()
  - 独立模式：自持信号、value 0、无文字。

- RadioButton(SignalInt m, int ov, string lbl)
  - 组挂载模式：共享选中信号 + value + 标签。

- bool IsSelected()
  - 本段是否为当前选中项。

- void SetMeta(int value, string sizeCls)
  - 组每帧下发：value 与档位类（label 建段时已定）。

- void SetSeam(string cls)
  - 接缝类（"rb-first" / "rb-last" / 空），组每帧重算。

- string Cls()
  - 生效样式类：用户 Class + 档位类 + 接缝类。

- StyleBox Box(App app, int latched)
  - 段的样式盒：fallbackType 走 `radiobutton` 根（height/padding/
    font/border 的默认都在根上），几何覆盖写 `radiobutton::box`。

- void Measure(App app)
  - 量好自然宽高存入 mw/mh（列缝恒 0，宽就是段的盒宽）。

- void PaintSeg(App app, int x, int y, int w, int h)
  - 在 (x,y,w,h) 绘制本段：盒（背景/各边边框/分角圆角）+ 居中
    文字，并注册命中矩形。选中态由组决定绘制次序。

- void Select()
  - 选中本项（静默判重）：写 model 并触发段事件；组的冒泡由
    订阅方（RadioGroup/RadioButtonGroup）接线。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（label/option/value）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"。

- override void BindEvent(string evt, Action a)
  - 覆写："Change" 挂段事件，其余按名称走通用路由。

- override void OnMeasure(App app)
  - 独立保留模式（.zform / 直接挂载）：自绘整段 + 点击。

- override void OnPaint(App app)
  - 独立保留模式绘制：自绘按钮段；点击，或聚焦后按空格/回车，
    即切换选中并请求重绘。


## RadioButtonGroup (class)

连体按钮段组（不依赖 RadioGroup 独立使用时的容器）：一排
RadioButton 共享一个选中信号（写选项 value），水平连体、窄宽
自动换行。段的绘制/命中由组统一调度：未选中段先画，选中段最后
画并外扩 1px 压住两条接缝。

RadioButtonGroup g = new RadioButtonGroup();
g.AddOption("当日达配送");
g.AddOption("门店自提（旗舰店）", 1);
g.OnChange(() => { Refresh(); });
int v = g.Selected().Get();

- SignalInt model;
  - 组共享选中信号（值为选中选项的 value）。

- List<RadioButton> items;
  - 段列表（与添加顺序一致）。

- UiEvent Change;
  - 组级 Change：任一选项被用户切换时触发。

- int wid;
  - 组命中 id。

- string sizeCls;
  - 尺寸档位类（RadioGroup 或本组 Size 落下来的）。

- int rowGapPx;
  - 皮肤行距/列缝（radiogroup.button；独立时自己解析，
    RadioGroup 挂载时由组下发）。

- int colGapPx;

- int rowCount;
  - 最近一次 LayoutRows 折行后的行数。

- Binding<string> Size;
  - 独立使用时的尺寸档位。

- void InitRadioButtonGroup(SignalInt m)
  - 公共初始化：自持事件、默认 medium 档。

- RadioButtonGroup()
  - 构造：自持信号，默认选中 value 0。

- RadioButtonGroup(SignalInt m)
  - 构造：共享组选中信号。

- SignalInt Selected()
  - 选中信号：组内写的是选中选项的 value。

- int Count()
  - 段数。

- RadioButton AddOption(string label)
  - 追加选项（value 默认 = 下标）。

- RadioButton AddOption(string label, int value)
  - 追加选项（显式 value），段事件冒泡为组级 Change。

- void ClearItems()
  - 清空全部段。

- void SetSize(string cls)
  - 设尺寸档位类（RadioGroup 下发或独立使用时经 Size 解析）。

- void SetGap(int rg, int cg)
  - RadioGroup 每帧下发皮肤行距/列缝（按钮形态列缝恒 0）。

- int LayoutRows(int avail)
  - 把各段按 `avail` 宽流式排成行（avail <= 0 = 单行不折），
    结果直接写进各段的 bx/by/bw/bh + row（供组与 RadioGroup 查
    行归属）。返回行数。测量（mw/mh）必须先由 Measure 做好。

- int rowOf(int i)
  - 第 i 段的行号。

- int colOf(int i)
  - 第 i 段在本行内的列序（行首 = 0）。

- int lastColOf(int r)
  - 第 r 行最后一段的列序。

- void SyncGap(App app)
  - 独立使用（无 RadioGroup 托管）时从皮肤解析按钮形态行距。

- override void OnMeasure(App app)
  - 量好所有段并回写本组的自然尺寸（单行总宽 / 最高段）。

- override void Arrange(int px, int py, int pw, int ph)
  - 覆写：占满给定矩形并把各段流式折行排布（LayoutRows）。

- override void OnPaint(App app)
  - 未选中段先画，选中段最后画并左右各外扩 1px：它的强调色描边
    压住两条接缝（Element Plus 同款处理），自身不挡别人（透明底）。

- int IndexOfSelected()
  - 当前选中段的下标（无 -1）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override void BindEvent(string evt, Action a)
  - 覆写："Change" 挂组事件，其余按名称走通用路由。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"。

- override string GetExtra(string key)
  - 设计器读取 `options`（"A|B|C" 文本）与 `size`；其余返回 ""。

- override bool SetExtra(string key, string val)
  - 设计器写入 `options`（"A|B|C" 逐项追加段）与 `size`；
    其他键返回 false。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（options/size）。


## RadioGroup (class)

单选按钮组：所有选项共享一个选中信号（组内写的是选项 value），
每个选项一个真实 Radio。默认选中第 0 项。

增强（对齐 Element Plus el-radio-group）：
* 选项可携带自定义 value / disabled / 语义色（AddOption 重载与
SetOptions，label-field/value-field 的显式对应物）；
* `g.data = form.city;` 组级双向绑定（编译器降级的 Binding，
与 Radio.data 同一路，写的是选项 value）；
* `g.Buttons = true` 切成连体按钮段形态（Element Plus 按钮组）；
* `g.Size = "large"` 一次作用到整组（tiny/small/medium/large）；
* 组级 `Change` 事件：任一选项被用户切换时触发（`g.OnChange(h)`）。

- SignalInt model;
  - 组真相：当前选中选项的 value。

- Binding<int> data;
  - 组级双向绑定：写的是选中选项的 value（越界/禁用的值不生效）。

- Binding<bool> Buttons;
  - 连体按钮段形态（RadioButtonGroup 渲染，本容器只当轨道）。

- Binding<string> Size;
  - 尺寸档位（Naive UI size prop）。

- List<int> optionValues;
  - 与 children 对齐的选项元数据（label 由基类 labels 承担）。

- List<bool> optionDisabled;

- UiEvent Change;
  - 组事件：任一选项被用户切换时触发。

- RadioButtonGroup buttonGroup;
  - 按钮形态的承载控件（懒建，作为组的唯一子项）。

- RadioGroup()
  - 构造：默认选中 value 0（第 0 项）。

- SignalInt Selected()
  - 选中信号。默认形态（圆点）且未设自定义 value 时与
    `SelectedIndex()` 等价（value = 下标）；组真相始终是它，
    用 `SelectedValue()` / `SetSelectedValue()` 读写更稳。

- int SelectedValue()
  - 当前选中的选项 value（= model 现值）。

- int SelectedIndex()
  - 当前选中项的下标；model 值没有对应选项时回退：value 走
    下标语义（没有任何自定义 value）则 model 现值就是下标，
    越界时收进最后一项；自定义 value 而无匹配才是 -1。

- int IndexOfValue(int v)
  - value 对应的选项下标；无此 value 时 -1。

- void SetSelectedValue(int v)
  - 程序化选中（静默：不触发组 Change，与 SetChecked 一族一致）。
    没有对应选项或选项被禁用时为无操作。

- void SetSelectedIndex(int index)
  - 程序化选中第 index 个选项（静默）。越界 / 禁用无操作。

- Control AddOption(string text, int value)
  - 追加一个带自定义 value 的选项（Element Plus options 的对应物；
    只给 label 的旧重载 `AddOption(text)` 继承基类，value = 下标）。

- Control AddOption(string text, int value, bool disabled)
  - 追加带 value 与禁用位的选项（元数据入列后交基类构造控件）。

- void SetOptions(List<RadioOption> opts)
  - 用显式元数据一次性追加一批选项：调用方把自己的记录投影成
    RadioOption 列表（label-field/value-field 的显式对应物）。

- void SyncGroup()
  - 每帧（测量/绘制入口）把组级状态同步进选项：data -> model 拉取、
    子控件的 value/disabled 元数据下发、尺寸类下发；按钮形态还要
    保证段列表与组选项一致并写 seams（行首/行尾）。

- string labelOf(int i)
  - 第 i 个选项的标签。

- int valueAt(int i)
  - 第 i 个选项的 value（缺元数据时 = 下标）。

- bool optionDisabledAt(int i)
  - 第 i 个选项是否禁用（缺元数据 false）。

- void PushValue()
  - 用户切换后把 model 现值回写组级绑定。

- void ReconcileButtons()
  - 按钮段列表与组选项对齐（数量不一致才重建；逐项状态在
    SyncGroup 每帧下发）。

- override void ClearOptions()
  - 清空全部选项（两种形态共用）：用于从文本/JSON 重建选项；
    覆写基类以连元数据与按钮组一起清。

- override Control MakeItem(int index, string text)
  - 覆写：为下标造一个真实 Radio（写选项 value，禁用位与组
    Change 冒泡），并把落后的元数据列表就地补齐。

- override void OnMeasure(App app)
  - 覆写：先同步组状态；按钮形态量按钮段加内边距，圆点形态走基类。

- override void OnPaint(App app)
  - 覆写：按钮形态把段的绘制/命中整体交给按钮组子控件，
    圆点形态走基类轨道。

- override bool SetExtra(string key, string val)
  - 设计器写入 `type`/`buttons`（切换按钮段/圆点形态）与 `size`；
    其余交给基类。

- override string GetExtra(string key)
  - 设计器读取 `type`（按钮形态返回 "button"）与 `size`；
    其余交给基类。

- override string OptionsText()
  - 选项文本带禁用位：`A|B!|C` 的 `!` 后缀把 B 标成不可点
    （与 optionDisabled 元数据一一对应）。

- override void SetOptionsText(string text)
  - 按 `A|B!|C` 重建：尾缀 `!` 的项以禁用位入列
    （value = 下标，与文本路径的既定语义一致）。

- override List<PropSpec> Props()
  - 覆写：基类清单之外追加 buttons/size/value（value 优先指向
    活的外部绑定）。

- override void BindEvent(string evt, Action a)
  - 覆写："Change" 挂组事件，其余按名称走通用路由。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。


## RadioOption (class)

RadioGroup 的单个选项元数据（对应 Element Plus 的 options 数组项）：
label 是显示文字，value 是选中时写回模型的整数（未显式给定时 = 下标），
disabled 使该项不可点（选中它的值也点不出来），cls 是该选项自己的
语义色类（info/success/...，与尺寸类互不冲突）。

- string label;
  - 显示文字。

- int value;
  - 选中时写回模型的整数（未显式给定时 = 下标）。

- bool hasValue;
  - value 是否由调用方显式给出（false = 用添加位置作下标值）。

- bool disabled;
  - 该项不可点（选中它的值也点不出来）。

- string cls;
  - 该选项自己的语义色类（info/success/...，与尺寸类互不冲突）。

- RadioOption(string lbl)
  - 仅 label：value = 下标。

- RadioOption(string lbl, int val)
  - label + 显式 value。

- RadioOption(string lbl, int val, bool dis)
  - label + value + 禁用位。


## Rate (class)

星级评分（NaiveUI n-rate），带响应式 int 绑定与悬停预览。
图标走图标组件管线（IconSvg 优先、IconVector 回退）：
`Icon` / `VoidIcon` 给出实心/空心档的图标名（默认 "star" /
"star-empty"），换成 "heart"、"flame" 等任何图标集认识的名字
即可自定义外观。

鼠标语义与框架对齐：点击走 `App.ClickAvailable()`（一次释放只
被消费一帧，弹层已认领的点击不再穿透）；半星模式下每颗星注册
左右两个命中矩形，点击落在哪一半、悬停预览到哪一半都直接由
命中 id 决定，不再用 mouseX 对格子取余。悬停预览值变化时触发
`HoverChange`（`HoverValue()` 读取，0 = 已离开）。

C# 风格的有状态实例：
Rate stars = new Rate(5);
stars.Change += () => { ... };
stars.Render(app, x, y);

- SignalInt model;
  - 本地评分信号：整星模式 1..maxStars，allowHalf 时按 *2 编码
    （7 = 3.5 星）。

- Binding<int> data;
  - 双向绑定的评分：`stars.data = review.score;` 使模型字段与评分
    双向保持同步（编译器降级的 Binding）。allowHalf 时同样按 *2
    编码（7 = 3.5 星），与模型信号、HoverValue 同一套语义。

- int maxStars;
  - 星星总数。

- int baseId;
  - 命中 id 段起点：整星每星一个；allowHalf 时每星另有右半格
    id（baseId+maxStars+i），故按 2×maxStars 预留。

- UiEvent Change;
  - 评分变化时触发（C# 风格：`r.Change += h;`）。

- UiEvent HoverChange;
  - 悬停预览值变化时触发（含离开归 0）。`r.HoverChange += h;`
    之后用 `r.HoverValue()` 读取当前预览值。

- bool allowHalf;
  - 允许半星（Naive UI allow-half）。true 时点击星星左半
    = i+0.5，右半 = i+1，模型用 *2 存储。

- bool readOnly;
  - 只读：禁止点击编辑、无悬停预览，但仍绘制（Naive UI readonly）。

- bool clearable;
  - 可清空：再次点击同一半格清除评分（Naive UI clearable）。

- Binding<string> Size;
  - 尺寸变体：small/medium/large（Naive UI size）。Binding<string>
    支持响应式换档，属性面板直接读写选项文本。

- Binding<string> Icon;
  - 实心档图标名（Binding<string> 支持响应式，属性面板直写）。

- Binding<string> VoidIcon;
  - 空心档图标名。

- int lastHover;
  - 上一帧的悬停预览值；0 = 未悬停。Half 模式按 *2 编码。

- void InitRate(SignalInt m, int stars)
  - 初始化：评分信号与星数（命中 id 按 2x 星数预留，供半星使用）。

- Rate():this(5)
  - Default design-time constructor.

- Rate(int stars)
  - 自持信号：`Rate stars = new Rate(5);`，再通过 `.data` 双向绑定——
    调用处无需任何信号连接代码。

- Rate Max(int stars)
  - 设置星星总数（流式写法：`new Rate().Max(5)`）。星数变化时
    按 2×重新预留命中 id 段（allowHalf 的右半格同段）：初始构造
    只按构造星数预留，扩到更大星数后不重预留的话，PaintStars
    注册的 id 会越过初始段落进后续控件的 id 空间——两控件互相
    误认领对方的悬停/点击。

- int MaxStars()
  - 当前星数（只读取数，不触动 id 预留；适配器按它避免逐帧
    重复 Max 重预留）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override Binding<string> SizeOf()
  - 覆写：返回 Size 绑定字段（设计器经它发现尺寸档位绑定）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（星数 / 评分 / 半星、只读、可清空、尺寸与图标）。

- int Value()
  - 当前评分（allowHalf 时按 *2 编码）。

- void SetStars(int v)
  - 程序化设置评分（整星档；超出星数截断）。不触发 Change——
    与 Switch.SetOn 同一约定：程序赋值不算用户交互。

- int HoverValue()
  - 当前悬停预览值（`HoverChange` 报告的同一个数）：
    整星模式 = 将点亮的星数；半星模式按 *2 编码（3 = 1.5 星）；
    0 = 指针已离开。

- override void SyncBinding()
  - 将绑定的模型值拉入本地信号（model -> UI）。data 与 model
    同一种编码（allowHalf 时都是 *2），直接比较即可——一边除
    一边乘的换算会把 3.5 星量化回 3 星。

- string SizeCls()
  - 当前档位对应的 class 片段（"" = medium）。

- int StepPx(App app)
  - 一颗星占据的横向步长：字号 + CSS 的 `gap`。绘制、测量和
    半星命中判定必须用同一个值——三处各算一遍的时候，皮肤
    一改 `rate { font-size }` 点击位置就和画出来的星星错开。

- void SetHover(App app, int v)
  - 悬停预览值落定：变化的那一帧才触发 HoverChange（边沿触发，
    处理器里改模型不会造成重绘风暴）。

- int Render(App app, int x, int y)
  - 绘制星星并处理点击（悬停预览经 HoverValue/HoverChange 报告）；
    返回命中 id 段起点。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change" 与 "HoverChange"。

- override void BindEvent(string evt, Action a)
  - 将评分的 Change / HoverChange 路由到各自 UiEvent 字段；其余
    一切落到 `On` 的公共事件集（其 AddByName 忽略
    不认识的名称）。

- void PaintStars(App app, int x, int y)
  - 按 `rate` CSS 规则绘制星星，悬停的星（半星模式下是悬停的
    半格）预览将要设置的评分。半星模式（allowHalf）下每颗星
    注册左右两个命中矩形：左半 id = baseId+i（= i+0.5 星），
    右半 id = baseId+maxStars+i（= i+1 星），命中/悬停/预览
    共用这一份几何。半星 = 50% alpha 实心（`rate::star-half`）。

- override void OnMeasure(App app)
  - 覆写：高 = 字体行高，宽 = (n-1) x 步长 + 星形宽。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = 在自身位置 Render。


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
  - 标题文本（可绑定）。

- Binding<string> Desc;
  - 描述文本（可绑定）；空串不画该行。

- string Icon;
  - 覆盖状态 class 隐含的图标。

- void InitResult(string ttl, string ds)
  - 初始化：绑定标题与描述并默认 info 状态 class。

- Result()
  - 空结果页（设计器用）。

- Result(string ttl, string ds)
  - 标题 + 描述。

- string Label()
  - 当前标题；Text 为 null 时返回空串。

- string Description()
  - 当前描述；Desc 为 null 时返回空串。

- override void OnMeasure(App app)
  - 覆写：高取图标盘 + 标题（有描述再加一行）的累计偏移，
    宽取样式表或兜底 360。

- override void OnPaint(App app)
  - 覆写：居中画图标盘（状态 class 定强调色、Icon 可覆盖图标）、
    标题与可选描述行。

- static string StatusClass(int status)
  - 数字状态对应的 class：0 info、1 success、2 warning、
    3 error——文档或设计器字段存储的值。取设计系统共享角色刻度上的偏移，
    而不是重复其映射表。

- static string GlyphOf(string cls)
  - 状态 class 隐含的图标（未指明其他角色时为 "info"）。

- static int ActionsY(App app, bool hasDesc)
  - 自定义操作内容可开始绘制的 Y 偏移（相对渲染 y）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（text/desc/icon/class）。

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
  - 功能区条带的默认高度。

- static int Render(App app, int x, int y, int w, int h, List<RibbonCmd> cmds)
  - 立即模式渲染整条带（大命令 + 紧凑命令 + 组分隔线）；
    返回本帧被点击的命令索引，无则 -1。

- static int tipBaseY;
  - 条带底边的 Y，悬停提示锚定于此，使它们全部
    在功能区下方对齐而非跟随指针。在条带绘制期间设置
    （见 PaintItem）。

- static int TabStripHeight(App app)
  - 标签条高度。

- static int HeightTabbed(App app)
  - 标签式功能区总高（标签条 + 条带）。

- static int RenderTabbed(App app, int x, int y, int w, List<string> tabs, SignalInt activeTab, List<RibbonGroup> groups)
  - 绘制标签条和活动条带，并返回本帧点击命令的全局索引
    （按顺序跨所有组计数），
    无则返回 -1。`activeTab` 是双向的。

- static int HeightGroups(App app)
  - 无标签页的分组条带：命令按命名组排列、组间竖线分隔，
    小命令每列堆叠 `rows` 个。给只有一屏命令、不值得
    分页的宿主用（设计器工具条就是这种）。返回本帧
    点击命令的全局索引（跨组按顺序计数），无则 -1。
    分组条带高度（无标签式）。

- static int RenderGroups(App app, int x, int y, int w, List<RibbonGroup> groups)
  - 渲染无标签的分组条带；返回本帧点击命令的全局索引
    （跨组按顺序计数），无则 -1。

- static int SmallColW(App app, RibbonGroup grp)
  - 分组命令条带本体，标签式和无标签式功能区共用。
    `rows` 是一列里堆叠多少个小命令。
    一组小命令的列宽：组内最宽的标签说了算，这样每列的
    图标和文字都对齐，长标签也不会被邻列压住。

- static int BandWidth(App app, List<RibbonGroup> groups, int bh, int rows)
  - 条带排完所有分组需要多宽（和 Band 里的列游标算法一致，
    只量不画）。宿主给的宽度不够时据此收缩排布。

- static int Band(App app, int x, int by, int w, int bh, List<RibbonGroup> groups, int firstId, int rows)
  - 分组条带本体，标签式和无标签式功能区共用；
    返回本帧点击命令的全局索引，无则 -1。

- static void PaintItem(App app, int id, RibbonItem it, int x, int y, int w, int h, bool small, StyleBox hoverStyle, StyleBox activeStyle, StyleBox textStyle, StyleBox mutedStyle, StyleBox accentStyle, StyleBox accentHoverStyle, StyleBox disabledStyle)
  - 绘制单个命令项（大 / 小行 / 纯图标格三种形态），
    处理悬停/按下淡变与悬停提示。

- static bool ItemClicked(App app, int id, RibbonItem it)
  - 本帧该命令项是否被点击（禁用项恒 false）。


## RibbonCmd (class)

单个功能区命令：图标加下方标题。`sep` 在命令后绘制组
分隔线（用于在视觉上分组相关命令）。

- string icon;
  - 图标名。

- string label;
  - 标题。

- bool sep;
  - 本命令之后画一条组分隔线。

- bool enabled;
  - false 时命令变灰绘制、无悬停/手型光标，
    且不可点击（Render 永不返回其索引）。

- bool compact;
  - 紧凑命令是纯图标方按钮，宽度为通常的一半：
    用于开关类（面板可见性、视图选项），它们占用整个
    带标题槽位太浪费。

- bool on;
  - 开关类命令的当前态（SetOn 每帧驱动；on 时按按下态着色）。

- string tip;
  - 悬停说明，显示在标题下方；否则只显示标题。

- RibbonCmd(string icon, string label)
  - 构造常规命令（默认可用、非紧凑）。

- RibbonCmd SetTip(string text)
  - 设悬停说明（链式）。

- string TipText()
  - 提示文本：有说明时为「标题\n说明」。

- static RibbonCmd Switch(string icon, string label, bool on)
  - 纯图标开关。`label` 用于提示/无障碍
    名称；每帧用 SetOn 驱动状态。

- void SetOn(bool state)
  - 驱动开关态（紧凑开关命令每帧调用）。

- static RibbonCmd Group(string icon, string label)
  - 构造组尾命令（其后画组分隔线）。

- void SetEnabled(bool on)
  - 设可用性。


## RibbonGroup (class)

命名的功能区命令组，绘制为带标题的簇，
与相邻簇以竖线分隔。

- string title;
  - 组标题。

- List<RibbonItem> items;
  - 组内命令。

- RibbonGroup(string title)
  - 构造空组。

- static RibbonGroup Of(string title, List<RibbonItem> items)
  - 从现成的项列表构建组，例如
    RibbonGroup.Of("File", new List<RibbonItem>{ ..., ... })

- RibbonGroup Add(RibbonItem it)
  - 追加一个命令（链式）。


## RibbonItem (class)

Office 风格功能区组中的一个命令。`small` 渲染紧凑的
图标+标签行形式（每列三个堆叠）；否则为大的
图标+标题按钮。

- string icon;
  - 图标名。

- string label;
  - 标题 / 标签。

- bool small;
  - 紧凑的图标+标签行形式（每列多行堆叠）。

- bool tile;
  - 纯图标小方钮，按 Office 的图标格铺排（一列堆叠多个，
    标签只出现在提示里）。用于同一族的一批命令，
    例如六个对齐方式。

- bool menu;
  - 画一个下拉箭头（拆分按钮）：宿主自己在点击后弹菜单，
    功能区只负责表达"这个命令还有下一级"。

- bool enabled;
  - 可用性（Disable/SetEnabled 切换）。

- bool toggle;
  - 开关在 `on` 时点亮绘制（强调色填充 + 强调色图标），使一排
    看起来像一排开关而非一排命令。

- bool on;
  - 开关当前态（SetOn 每帧驱动）。

- string tip;
  - 悬停提示；留空时默认取标签。

- static RibbonItem Large(string icon, string label)
  - 构造大命令（图标+标题）。

- static RibbonItem Small(string icon, string label)
  - 构造小命令（图标+标签行）。

- static RibbonItem Toggle(string icon, string label, bool on)
  - 小型开关，例如停靠布局中每个工具面板一个，
    （见 DockHost.AddSwitchGroup）。

- static RibbonItem Tile(string icon, string label)
  - 纯图标方钮。`label` 只用于提示。

- RibbonItem SetMenu()
  - 标记为拆分按钮（画下拉箭头，链式）。

- RibbonItem Disable()
  - 禁用该命令（链式）。

- RibbonItem SetTip(string text)
  - 设悬停说明（链式）。

- void SetOn(bool state)
  - 驱动开关态。

- void SetEnabled(bool state)
  - 设可用性。

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
  - 滚轮/拖动/滚动条共用的立即模式滚动状态。

- int contentH;
  - Arrange 后写出：子控件总内容高度（px），供滚动计算。

- int barReserve;
  - 滚动条预留宽度（缩放后，OnMeasure 刷新）。

- void InitScrollColumn()
  - 初始化：以 "scroll" 注册（子控件停靠堆叠），置共享滚动状态
    与滚动条预留宽。

- ScrollColumn()
  - 空滚动列（子控件停靠 top 依次堆叠）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override void OnMeasure(App app)
  - 覆写：按 DPI 刷新滚动条预留宽度。

- override void Arrange(int px, int py, int pw, int ph)
  - 按自然高度自上而下堆叠子控件，向上偏移
    当前滚动量；完全在视口外的行被隐藏。

- override void OnPaint(App app)
  - 覆写：处理滚轮/拖动并绘制滚动条；新滚动量在下次 Arrange 生效。


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

- int dragMo0;

- int collapseSavedOffset;

- int collapseMo;

- int lastMo;

- bool collapsePending;

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

- static int grabId=0-1;

- static int grabDy=0;

- static void RenderVertical(App app, int x, int y, int height, int contentHeight, int scrollOffset)
  - 纯绘制的非交互垂直滚动条：轨道 + 按内容比例的滑块
    （contentHeight > height 时才显示），不处理任何输入。

- static void RenderVerticalScroll(App app, int x, int y, int height, int viewH, int contentHeight, SignalInt offset)
  - 绑定滚动偏移信号的交互式垂直滚动条。支持
    滑块拖动和点击跳转：拖动按抓取点跟随（抓住滑块
    哪里就从哪里动，起手不跳位）；点击轨道仍以指针
    为中心跳转。`viewH` 为滚动表面的可见高度；
    `height` 为滚动条长度（通常与前者
    相同）。`offset` 更新范围为 [0, contentHeight - viewH]。

- static void RenderVerticalScrollIn(App app, int x, int y, int height, int viewH, int contentHeight, SignalInt offset, int vx, int vy, int vw, int vh)
  - RenderVerticalScroll 的限定矩形变体：滚动变化只声明
    (vx, vy, vw, vh) 区域重绘，不整页。

- static void RenderVerticalScrollCore(App app, int x, int y, int height, int viewH, int contentHeight, SignalInt offset, bool scoped, int vx, int vy, int vw, int vh)
  - 两个公开变体的实现体：scoped 为 true 时按限定矩形声明重绘，
    否则整页请求。

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
new SelectBox(hint, false).Clearable(true).Status(SelectBox.Error())
尺寸档另行赋值：`box.Size = "large";`（"small"/"medium"/"large"）。

绑定：`sel.data = form.color;` 将选中索引双向绑定到
普通字段；`SelectBox.FromOptions(options, hint)` 从标签列表构建，
且 `sel.Change += h;` 在每次选择变化时触发。

- List<SelectOption> opts;
  - 全部选项（平铺：分组标题、禁用项、级联子级都在此列）。

- int selected;
  - 单选选中下标（-1 无）；多选模式不用。

- SignalInt model;
  - 可选共享选择信号（SelectBox.Bind）。

- Binding<int> data;
  - 双向绑定的选中索引：`sel.data = form.color;`（编译器降级的
    Binding）。新变动的一方生效，弹出层与字段保持同步。

- int lastModel;
  - 上次与 model / data 同步的值：SyncBinding 据此判断谁变了。

- int lastData;

- UiEvent Change;
  - 选择变化时触发（`sel.Change += h;`）。

- UiEvent Open;
  - 选项列表打开/关闭时触发，无论由哪种方式
    触发（点击触发控件、选择、外部按压、Esc）。

- UiEvent Close;

- string hint;
  - 无选中时显示的占位文字。

- bool multi;
  - 多选模式（取值为勾选集合）。

- bool clearable;
  - 有值且悬停时显示清除按钮。

- bool open;
  - 弹层展开状态。

- int scrollY;
  - 弹层列表滚动偏移（像素）。

- SignalInt scrollSignal;
  - 弹层滚动偏移的信号视图：内嵌 Scrollbar 直接拖它，
    每帧与 `scrollY` 互相同步。

- Binding<string> Size;
  - 尺寸档："small" / "medium" / "large"（§6.4 尺寸档统一 string）。
    Binding<string> 使属性面板的 size 行真正写回；渲染时映射为
    select.small/.large 档位 class（base.css 的 --height-* token 规则）。

- int status;
  - 校验状态（Normal/Success/Warning/Error），经 StyleClass 映射为状态 class。

- int maxVisible;
  - 弹层最多直接可见的行数，超出滚动（也决定多选自动分列行数）。

- int placement;
  - 弹出层位置：0 自动（触发控件下方，超出则反向），1 仅下方，2 仅上方。
    Naive UI `placement` prop。

- bool filterable;
  - 可搜索：弹出层顶部加搜索框，键入时过滤选项（Naive UI `filterable` prop）。

- string filterText;
  - 当前搜索关键词。

- Input filterBox;
  - 筛选搜索框：弹层顶部内嵌的真 Input 组件（不是自绘的假输入条），
    焦点、光标、选区、Ctrl+A/C/X/V、清除按钮全部由 Input 自持。
    打开弹层期间焦点锁定在它上面，键入才不需要先点一下搜索框。

- bool cascade;
  - 级联模式：选项带子级，弹层逐列展开（省市区这类层级）。

- List<int> cascadePath;
  - 级联当前展开链：每列已展开选项的平铺下标（flatIdx）。

- List<SelectOption> cascRoots;
  - 级联顶层选项（AddBranch 建立的那些），弹层第一列渲染它们。

- List<int> cascadeFilter;
  - 级联过滤的命中下标（filterable + 关键词非空时重建）：平铺所有
    层级里标签命中的节点，行上显示完整路径。

- bool cascadeLink;
  - 级联父子联动（Naive UI `cascade` prop，默认开）：勾选父级带动
    全部子孙，父级的勾/半勾由子孙推导；关掉后任何层级的选项都可
    独立勾选，单选下也允许直接选中带子级的分支。

- int checkStrategy;
  - 勾选策略（Naive UI check-strategy）：0 all 全部勾选节点、
    1 parent 只算没被更深勾选祖先盖住的节点（完整勾选子树的顶）、
    2 child 只算勾选的叶子。

- bool showPath;
  - 触发器/标签显示完整路径（"浙江 / 杭州 / 西湖区"，
    Naive UI show-path）；关掉只显示末级短标签。

- bool hoverExpand;
  - 悬停即展开下一列（Naive UI hover-trigger）；默认点击展开。

- bool clearFilterAfter;
  - 从过滤结果选中后清掉搜索词（默认清）。

- List<int> cascadeScroll;
  - 级联各列的滚动偏移（按下标取，多退少补）。

- List<SignalInt> cascadeScrollSig;
  - 各列内嵌 Scrollbar 的信号视图，懒建，与 cascadeScroll 同步。

- bool treeMode;
  - 树形模式：弹层内嵌一颗真 TreeView，展开/折叠、滚动、悬停
    全部由 TreeView 自持，SelectBox 只负责摆位与收值。

- TreeView treeBox;

- int treeSelSeen;
  - 树形模式上次见到的选中下标（-1 无），用于每帧轮询选中变化。

- string pathText;
  - 级联/树形选中的完整路径文本（"浙江 / 杭州"），供 Text() 显示。

- List<int> filteredIdx;
  - 过滤后的选项索引列表（null = 不启用过滤）。每帧根据 filterText 重建。

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

- UiEvent Hover;
  - 悬停的选项变化时触发（`sel.Hover += h;`，用 Hovered() 读是哪一条）。
    没有它，调用方就只能靠自己在弹层几何上重算一遍鼠标位置 —— 而弹层
    的位置是框架翻转后决定的，外面根本算不准。

- int cols;
  - 弹层列数：0 = 自动（多选框选项多于一屏时分成等宽多列，
    而不是拉成一条长条），否则固定列数。

- List<string> headers;
  - 表格式下拉的列标题；null = 普通单列列表。首列对应选项的
    label，其余对应 SelectOption.cells。

- List<int> colWidths;
  - 各列宽度（设备像素）。与 headers 一一对应。

- string filterHint;
  - 搜索框里的提示文字（filterable）。空则用 "Search"。

- int popupW;
  - 弹层宽度缓存。量一次即可：逐帧测量每条标签在上万条选项时
    就是每帧上万次文本测量，比真正的绘制还贵。

- bool popupWDirty;
  - 弹层宽度缓存是否失效（选项集 / 过滤词 / 列数变化时置位）。

- static int Normal()
  - 校验状态常量（Status 的取值）。

- static int Success()
  - 校验通过状态。

- static int Warning()
  - 校验警告状态。

- static int Error()
  - 校验错误状态。

- SelectBox():this("", false)
  - Default design-time constructor; the form supplies `placeholder` later.

- SelectBox(string hintText):this(hintText, false)
  - 单选下拉框（多选用 `new SelectBox(hint, true)`）。

- SelectBox(string hintText, bool isMulti)
  - 主构造：占位提示 + 是否多选；默认弹层最多显示 6 行，级联联动开。

- static SelectBox FromOptions(List<string> options, string hint)
  - 从普通标签列表构建选择框（JSON / 设计器 / 单行场景使用）。

- string OptionsText()
  - 选项以 `a|b|c` 字符串表示（设计器 / .zform 属性往返）。

- void SetOptionsText(string text)
  - "a|b|c" 文本 → 选项列表（SetExtra 反序列化；滚动复位）。

- SelectBox Columns(int n)
  - 固定弹层列数（等宽）；0 恢复自动。选项按列优先填，
    先从第一列自上而下。

- SelectBox Table(List<string> titles, List<int> widths)
  - 表格式下拉：弹层变成一张带列标题的表，每个选项占一行、
    每行按 `widths` 分列。首列是选项的 label（Text() 仍然返回它），
    其余列由 AddRow 提供。
    
    SelectBox pick = new SelectBox("Pick a stock");
    pick.Table(new List<string>{ "Code", "Name", "Last" },
    new List<int>{ 90, 160, 90 });
    pick.AddRow(new List<string>{ "600519", "Kweichow", "1712.00" });
    
    传 null 恢复普通列表。

- bool IsTable()
  - 是否处于表格式下拉模式。

- void AddRow(List<string> cells)
  - 表格式下拉的一行。`cells[0]` 是 label（首列），其余为后续列。

- int HeaderH(App app)
  - 表头高度（表格模式下弹层顶部占用的空间）。

- int DisplayCount()
  - 过滤后参与显示的行数。

- int RowToOpt(int row)
  - 显示行 -> 真实选项下标。过滤开着时经过滤表转换，否则同一个数。
    渲染、命中和悬停三处原本各自内联这段映射，任何一处写歪都表现为
    「点中的不是看到的那条」。

- int Hovered()
  - 悬停中的选项下标，没有则 -1（配合 `Hover` 事件读取）。

- string OptionLabel(int i)
  - 第 `i` 个选项的标签（越界返回空串）。悬停/选择事件给出的是下标，
    调用方要显示的是文字。

- int ColumnCount()
  - 本次开层实际的列数。自动时：单选保持单列（下拉就该是
    一条列表）；多选超过一屏时按 maxVisible 分列，最多 4 列，
    这样一屏就能成行成列地看完。表格模式恒为一列（列在行内部）。

- int RowCount()
  - 当前列数下每列的行数。

- int Value()
  - 当前选中下标（Selected 的别名）。

- void SetIndex(int idx)
  - 外部设定选中下标（钳制到有效范围，-1 = 无选择）。
    程序化赋值不触发 Change；弹出层与渲染下一帧读到新值。

- bool IsOpen()
  - 弹层当前是否打开。

- void SetOpen(bool v)
  - 打开/关闭列表，并在实际切换时触发 Open/Close，
    使每个入口（触发控件、选择、外部按压、Esc、代码）以相同方式
    报告。

- override void SyncBinding()
  - 将共享信号和双向绑定与 `selected` 协调一致。
    弹出层直接写 `selected`，因此每一侧都与
    上次同步值比较，发生变动的一方生效。

- void Choose(int idx)
  - 应用弹出层作出的选择：将绑定和 Change
    事件集中在一处，使所有入口行为一致。

- SelectBox Cascade()
  - 级联选择模式。层级用 AddBranch / AddChild 建立，
    弹层逐列展开，选中叶子时触发器显示完整路径
    （"浙江 / 杭州 / 西湖区"）。配合 Multi(true) 得到勾选式
    多选（父子联动 + 勾选策略 + 标签芯片触发器）。

- static int CheckAll()
  - 勾选策略常量（CheckStrategy 的取值）。

- static int CheckParent()
  - 策略 1：只有勾选的父级节点计入触发器值集合。

- static int CheckChild()
  - 策略 2：只有勾选的子级节点计入触发器值集合。

- SelectBox CascadeLink(bool v)
  - 级联父子联动（Naive UI `cascade` prop，默认开）：勾选父级带动
    全部子孙；关掉后任何层级的选项独立勾选，单选下也允许直接选中
    带子级的分支。

- SelectBox CheckStrategy(int v)
  - 勾选策略：CheckAll / CheckParent / CheckChild（Naive UI
    check-strategy）。决定哪些勾选节点计入触发器的值集合。

- SelectBox ShowPath(bool v)
  - 触发器是否显示完整路径；false 只显示末级短标签
    （Naive UI show-path）。

- SelectBox HoverExpand(bool v)
  - 悬停即展开下一列（Naive UI hover-trigger）；默认点击展开。

- SelectBox ClearFilterAfter(bool v)
  - 从过滤结果里选中后是否清掉搜索词（Naive UI
    clear-filter-after-select，默认清）。

- int AddBranch(string label)
  - 加一个顶层分支（级联第一列的条目），返回它的平铺下标
    （继续 AddChild 时作父下标用）。

- int AddChild(int parent, string label)
  - 给第 `parent` 个选项挂一个子级（父下标是 AddBranch /
    AddChild 的返回值），返回子级的平铺下标。挂在同一父级
    下的子级按插入顺序排成一列。

- SelectBox Tree()
  - 树形选择模式：弹层内嵌一颗真 TreeView。层级用
    TreeNode.Dir / TreeNode.File 构建，经 TreeNodes 绑定；
    叶节点被选中时触发器显示它的 payload（未设则 label）。
    树的展开、滚动、悬停全部是 TreeView 自己的行为。

- SelectBox TreeNodes(List<TreeNode> nodes)
  - 绑定树节点（调用方的列表，TreeView.Bind 不复制）。

- static SelectBox Multi(string hintText)
  - 工厂：多选下拉框。

- void AddOption(string label)
  - 追加一个普通选项。

- void AddOption(string label, bool disabled)
  - 加一个禁用项：占正常位置、置灰显示，但不响应悬停与点击。

- void AddGroup(string title)
  - 加一个分组标题：随后 AddOption 的条目在视觉上归属该组。
    标题行本身不可悬停、不可选择；键入筛选时若组内没有命中
    项，标题随组一起隐藏。

- SelectBox Clearable(bool v)
  - 是否显示清除按钮（有值且悬停时）。

- SelectBox Status(int v)
  - 设校验状态（Normal/Success/Warning/Error 常量）。

- SelectBox Placement(int v)
  - 弹出位置：0=自动（默认），1=下方（Naive UI placement="bottom"），2=上方。

- SelectBox Filterable(bool v)
  - 启用搜索过滤（Naive UI filterable）。

- SelectBox FilterHint(string s)
  - 搜索框的提示文字。不设则用 "Search"。

- string FilterText()
  - 当前搜索关键词（供调用方回显 / 远程搜索用）。

- void SetFilterText(string s)
  - 以编程方式设置关键词并重建过滤结果。

- void RebuildFilter()
  - 根据 filterText 重建过滤索引列表（空文本/未启用 → 全量）。
    级联模式下平铺所有层级、按标签匹配（行上显示完整路径）；
    表格模式匹配整行的所有单元格，否则只匹配 label：一张表里
    按代码搜和按名称搜同样自然，只认首列会让另一半列形同虚设。

- int Selected()
  - 当前选中下标（-1 无）。

- int SelectCount()
  - 选项总数（程序化灌入/对账用）。

- string SelectedLabel()
  - 当前选中项的标签（未选中返回空串）。

- bool IsChosen(int i)
  - 多选模式下第 i 项是否被勾选。

- bool HasValue()
  - 是否有有效取值（单选已选 / 多选有勾选 / 级联树形有路径文本）。

- void Select(int idx)
  - 以编程方式设置选中索引（-1 清除）。保持绑定的
    model/data 同步，但不触发 Change（调用方发起的更新）。

- void Clear()
  - 清除取值：单选/级联复位选中与路径，多选清全部勾选，
    树形清 TreeView 选中（选项列表保留）。

- void ClearOptions()
  - 清空选项集（连同过滤结果与宽度缓存）。远程搜索每次换一批
    候选项时需要它。

- int Count()
  - 选项总数（含分组标题与禁用项）。

- string Text()
  - 触发器显示文本：多选逗号拼接勾选值；级联按当前 showPath
    出完整路径或末级短标签；树形用收值时缓存的 payload 文本。

- int HeadHeight(Theme t)
  - 触发器高度（按尺寸档取主题行高）。

- int FontSize(App app, StyleBox s)
  - 触发器字号（按尺寸档取 small/medium/large 档）。

- int SizeIdx()
  - 尺寸档 string → 渲染下标（0 small / 1 medium / 2 large）。

- string StyleClass()
  - 校验状态以额外 class 的形式进入样式层（base.css 的
    `select.success/.warning/.error` 规则），绘制代码不再直接
    覆盖边框颜色。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（占位 / 选项 / 选中值 / 清除 / 尺寸 /
    弹层方向 / 过滤与级联选项）。

- override string GetExtra(string key)
  - 选项列表为文本，每项一个 `label|value` 对。

- override bool SetExtra(string key, string val)
  - 覆写：写回扩展属性，"options" 按每行 `label|value` 解析。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"/"SelectionChanged"/"Open"/"Close"/"Hover"。

- override void BindEvent(string evt, Action a)
  - 覆写：语义事件挂对应 UiEvent，其余按名称走通用路由。

- override void OnMeasure(App app)
  - 覆写：高取样式行高（触发器），宽缺省 200。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = 以自身矩形 RenderIn（弹层经 OnPaintOverlay 延迟绘制）。

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
    
    结果缓存在 popupW 里，只有选项集或过滤词变了才重新量：这段
    要遍历全部标签，而它每帧都被调用一次。一万条选项时那是每帧
    一万次 MeasureText —— 比画出来的十几行本身贵上三个数量级，
    也是长列表下拉「一开就卡」的真正来源（虚表只省了绘制）。

- int FilterBarH(App app)
  - 搜索框高度（filterable=true 时占用的顶部空间）。

- List<int> VisibleOpts()
  - 过滤后参与渲染的选项索引（filterable + 关键词非空才过滤）。

- override void OnPaintOverlay(App app)
  - 延迟弹层：在 App.RunOverlays 中、所有内容绘制完成后绘制并派发事件。

- void OverlayDismiss(App app, int popX, int popY, int w, int viewH, int ek)
  - 级联/树形弹层共用的收尾手势：点击弹层外部（含触发器）
    关闭；弹层内没被任何子控件认领的点击在这里吃掉，不能
    落到弹层下面的内容上；Esc 关闭。须在弹层内容处理完
    点击之后调用。

- void CascadeOverlay(App app, int x, int y, int w, int h, int fs, int arrowSlot)
  - 级联弹层：一列一级。点中带子级的选项在其右侧展开下一列
    （HoverExpand 时悬停即展开），叶子（联动关闭时的任意层级）
    选中收值；多选渲染行首勾选框，勾/半勾沿父子推导。每列自带
    滚动（滚轮 + 内嵌真 Scrollbar）；filterable 时顶部搜索条，
    命中项平铺单列、行文本是完整路径。列宽固定、弹层向右生长，
    最多同时 4 列。

- void SetColScroll(int col, int v)
  - 设第 col 列滚动偏移（列表按需扩容）。

- int DrawCascadeCheck(App app, Canvas c, Theme t, string cls, int cx, int ry, int optH, bool on, bool partial)
  - 级联行首的勾选框：复用 checkbox 的 box/check 样式 part。勾上
    打勾，半勾（联动模式下子孙有勾选）画短横。返回方框宽度，
    供点击分区（框区勾选、标签区展开）。

- void DrawCascadeTags(App app, Canvas c, Theme t, string cls, int partState, int x, int y, int w, int h, int pad, int arrowSlot, bool showClear, int clearSize, int fs, bool leftUp)
  - 级联多选的触发器：勾选值渲染成一排标签芯片（select::tag
    part），每片带 ×，点 × 单独移除该值；放不下的折叠成 +N ——
    maxTagCount 由触发器宽度自适应兜住。showPath 决定片上的
    文本是完整路径还是末级短标签。

- void ChooseCascade(int idx)
  - 级联选中一个节点（平铺下标）：写入触发器（showPath 决定完整
    路径还是末级短标签）、同步绑定并关闭。联动开启时只有叶子能
    走到这里；关闭时带子级的分支也可以直接选中。

- string PathOf(int idx)
  - 第 `idx` 个选项的完整路径文本（"浙江省 / 杭州市 / 西湖区"）。

- string ValueText(int idx)
  - 值的显示文本：showPath 决定完整路径还是末级短标签。

- List<int> ValueIdxs()
  - 勾选策略下的值集合（平铺下标）：all = 全部勾选节点；
    parent = 没有被更深的勾选祖先盖住的节点（完整勾选子树的顶）；
    child = 勾选的叶子。

- List<string> ValueLabels()
  - 值集合的显示标签（配合 ValueText 的 showPath 语义）。

- void SetChecked(int idx, bool on)
  - 级联多选：把 `idx` 的勾选态设为 `on`。联动开启时带动全部
    子孙，再沿父链回推（父级 = 全部子级勾上才算勾上）；
    关闭时只改自己。统一触发 Change。

- void ApplyCheck(int idx, bool on)
  - 递归应用勾选：联动开启时带动全部子孙。

- void FixupAncestors(int idx)
  - 沿父链回推勾选态：全子勾上 → 父勾上，否则父取消。
    某层已一致就停——更上层不可能因此变化。

- bool IsPartial(int idx)
  - 第 `idx` 个选项的半勾态：自身未勾但有勾选的子孙（联动模式）。

- bool HasCheckedDescendant(int idx)
  - 是否存在已勾选的子孙（递归）。

- int ColScroll(int col, int contentH, int viewH)
  - 第 `col` 列的滚动偏移（像素），按内容高度钳制。

- bool SamePath(List<int> a)
  - 展开链是否与当前 cascadePath 相同（避免悬停展开反复触发重绘）。

- void TreeOverlay(App app, int x, int y, int w, int h)
  - 树形弹层：内嵌真 TreeView，选中叶节点收值关闭。
    展开/折叠、滚动、悬停全部是 TreeView 自己的行为；
    SelectBox 只给矩形、轮询选中、把叶子收成路径文本。


## SelectOption (class)

一个下拉选项：其标签及是否被选中（多选）。
用一个实体代替平行的选项/选中列表。

- string label;
  - 选项标签（显示文本）。

- bool chosen;
  - 多选勾选态（单选模式不用）。

- List<string> cells;
  - 表格式下拉（SelectBox.Table）里这一行除首列以外的单元格。
    null 表示这条只有 label —— 普通列表模式下始终如此。

- List<SelectOption> children;
  - 级联模式的子层级；null = 叶子。

- int flatIdx;
  - 在 SelectBox.opts 平铺列表中的下标。AddOption / AddBranch /
    AddChild 都会追加到 opts，级联展开链用它寻址。

- int parentIdx;
  - 父级的平铺下标（顶层为 -1）。级联多选沿它向上修祖链的勾选态，
    过滤结果拼完整路径、重开弹层自动展开选中链也靠它。

- bool grp;
  - 分组标题行：只作小标题展示，不可悬停、不可选择。

- bool disabled;
  - 禁用项：可见、置灰，但不可选择。

- SelectOption(string label, bool chosen)
  - 构造普通选项（无附加列、非分组、无子级）。

- void Attach(SelectOption child, int idx)
  - 级联：给本选项挂一个子级，并回填它的平铺下标。

- bool HasChildren()
  - 是否带子层级（级联模式）。

- string Cell(int i)
  - 第 `i` 列的文本（0 为 label 列），越界返回空串。


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
  - 初始化：占位尺寸（CSS px）。

- Skeleton()
  - 默认 200×16（设计器用）。

- Skeleton(int w, int h)
  - 指定尺寸（CSS px）。

- StyleBox ResolvedStyle(App app)
  - 解析 `skeleton` 样式盒（Class 参与匹配）。

- override void OnMeasure(App app)
  - 覆写：首选尺寸取样式宽高，缺省用 Width/Height。

- override void OnPaint(App app)
  - 占位框本身：填充、圆角和动画均由样式决定。

- static Skeleton Line(App app, int width)
  - 一行 `width` px 宽的占位文字，高度与所代表的文本一致。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（宽 / 高 / 类）。


## Slider (class)

滑块（对齐 Naive UI n-slider）：响应式 int 绑定与实时拖拽 / 点击定位，
双柄范围（range）、垂直（vertical）、倒转（reverse）、标记刻度
（marks，AddMark(值, 文案)）、限取标记值（stepMark，即 step="mark"）、
拖拽值气泡（tooltip / showTooltip / tipFormat 模板）、键盘微调
（keyboard）与 Disabled 禁用态（皮肤 `slider:disabled`）。
所有尺寸均按 DPI 缩放。

C# 风格保留式实例：
Slider vol = new Slider(0, 100);
vol.Change += () => { ... };   // 拖动过程中值变化时触发
vol.Render(app, x, y, width);
范围模式（值 = [Low, High] 区间，Naive UI range）：
Slider rg = new Slider(0, 100);
rg.range = true;
rg.data = rangeModel.lo;       // 低值双向绑定
rg.dataHigh = rangeModel.hi;   // 高值双向绑定
rg.Low(); rg.High(); rg.SetRange(20, 70);
标记刻度（Naive UI marks / step="mark"）：
sl.AddMark(20, "20°C"); sl.AddMark(37, "37°C");
sl.stepMark = true;            // 只能停在标记值上
垂直（Naive UI vertical）：布局里给足高度（Grow/Prefer），
立即模式直接调用请用 RenderIn 显式给 height。

- int minVal;
  - 量程端点；构造时 maxVal < minVal 会归一为相等。

- int maxVal;

- SignalInt model;
  - 单值模式的值；range 模式下是低值（Naive UI value[0]）。

- SignalInt hiModel;
  - range 模式的高值（Naive UI value[1]）。

- Binding<int> data;
  - 双向绑定值：`vol.data = settings.volume;` 保持模型
    字段与滑块双向同步（编译器降级的 Binding）。
    range 模式绑定低值，高值走 dataHigh。

- Binding<int> dataHigh;
  - range 模式高值的双向绑定。

- int wid;

- UiEvent Change;
  - 值变化时触发（C# 风格：`s.Change += h;`）。

- UiEvent DragEnd;
  - 拖拽/定位手势结束时触发（在滑块上松开指针）。

- int step;
  - 拖动步长（Naive UI step prop）。0 = 任意（默认）。

- bool range;
  - 范围模式（Naive UI range）：双滑块钮，值是 [Low, High] 区间。

- bool vertical;
  - 垂直模式（Naive UI vertical）：轨道竖放，min 在下、值向上增长。

- bool reverse;
  - 倒转（Naive UI reverse）：轨道调头，min 落在右端 / 顶端。

- bool tooltip;
  - 拖拽 / 悬停圆钮时显示值气泡（Naive UI tooltip，默认开）。

- bool showTooltip;
  - 始终显示值气泡（Naive UI show-tooltip）。

- string tipFormat;
  - 气泡文案模板（Naive UI format-tooltip）："{v}" 替换为当前值，
    如 "{v}%"。空 = 直接显示数字。

- bool stepMark;
  - 只能取标记值（Naive UI step="mark"）。

- bool keyboard;
  - 键盘方向键微调（Naive UI keyboard prop，默认开）。

- List<int> markVals;
  - 标记刻度：AddMark(值, 文案)，按值升序保持。

- List<string> markLabels;

- string marksSpec;
  - designer 往返串："20=20°C|37=37°C"（GetExtra/SetExtra("marks")）。

- int dragThumb;
  - 正在拖拽的钮：0 无 / 1 低（单值）/ 2 高。按住期间保持，
    松开清零——按轨道选中"最近的钮"只在按下那一刻决定。

- int lastEdited;
  - range 模式里最近一次编辑的钮，键盘微调作用于它。

- static int MarkCap()
  - id 段（WidgetId.Block(32)）：wid = 轨道（焦点/键盘/点击定位），
    wid+1 = 低值钮，wid+2 = 高值钮，wid+8+i = 第 i 个标记（上限 24）。

- int Quantize(int v)
  - 将任意值量化到 `step` 整数倍 / 最近的标记值，再夹回
    `[min, max]`——先量化后夹，max 端点永远可达。

- int NearestMarkVal(int v)
  - 离 `v` 最近的标记值（markVals 已升序，等距取更小者）。

- int NextMarkVal(int v, bool up)
  - 下一个 / 上一个标记值；没有更远的标记时夹到 max / min。

- int StepUpVal(int v)
  - 键盘步进 +1 档：stepMark 时在标记间跳，否则按 step（缺省 1）。

- int StepDownVal(int v)
  - 键盘步进 -1 档。

- int Span()
  - 量程宽度（min == max 时按 1，防除零）。

- int FracOf(int v)
  - 值 → 千分比（0..1000）。

- int PxAt(int f, int pos, int len)
  - 千分比 → 轨道上的像素位置。默认：横向 min 在左、竖向 min 在下；
    reverse 把起点换到右端 / 顶端。

- int ValAt(int p, int pos, int len)
  - 轨道上的指针位置 → 量化后的值（横向/竖向、reverse 通吃）。

- string TipText(int v)
  - 气泡文案：tipFormat 里的 "{v}" 替换为值（Naive UI format-tooltip）。

- void InitSlider(int lo, int hi, SignalInt m)
  - 公共初始化（各构造共用）。

- Slider():this(0, 100)
  - Default design-time constructor.

- Slider(int lo, int hi)
  - 自持信号：`Slider vol = new Slider(0, 100);`，之后通过 `.data` 双向绑定，
    调用点无需任何信号接线。

- int Value()
  - 当前值（单值模式即值，range 模式为低值）。

- int Low()
  - range 模式的低值（单值模式与 Value() 相同）。

- int High()
  - range 模式的高值（单值模式返回同一个值）。

- void SetValue(int v)
  - 写入当前值（量化 + 同步双向绑定）。不触发 Change——
    程序化赋值不算用户手势。

- void SetRange(int lo, int hi)
  - 一次写入区间两端（各自量化并保持 low <= high）。

- void AddMark(int v, string label)
  - 加一枚标记刻度（Naive UI marks 的成员）。同值重复添加覆盖
    文案；列表保持按值升序。

- void ClearMarks()
  - 清空全部标记刻度。

- int MarkCount()
  - 标记数。

- int MarkValueAt(int i)
  - 第 i 个标记的值（不查越界）。

- string MarkLabelAt(int i)
  - 第 i 个标记的文案（不查越界）。

- string MarksText()
  - designer 往返串："20=20°C|37=37°C|100=100°C"。

- void SetMarksText(string spec)
  - 解析 MarksText 往返串并整体重建标记。

- override string GetExtra(string key)
  - 设计器挂钩：marks 不是单个字段，走 "20=20°C|37=37°C" 串往返。

- override bool SetExtra(string key, string val)
  - 覆写：写回扩展属性，"marks" 走 SetMarksText 往返串。

- override void SyncBinding()
  - 将绑定的模型值拉入本地信号（model -> UI）。

- int Render(App app, int x, int y, int width)
  - 立即模式渲染（高度取测量 prefH，缺省竖向 160 / 横向主题行高）；
    需要显式盒高（竖向）时用 RenderIn。

- int RenderIn(App app, int x, int y, int width, int height)
  - 显式盒高版本：竖向滑块需要调用方给高度。保留式布局走
    Arrange/OnPaint 自动带 bh；立即模式请直接调这个。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change" 与 "DragEnd"。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（量程 / 值与高值 / 步长 / range、vertical、
    reverse、标记与气泡等选项）。

- override void SetProp(string key, string val)
  - 面板直改 `marks` 必须走重建入口（直写字段不会重建标记）。

- override void BindEvent(string evt, Action a)
  - 覆写："Change"/"DragEnd" 挂对应事件，其余按名称走通用路由。

- bool NearThumb(App app, int cx, int cy, int r)
  - 圆钮命中（悬停气泡用）：指针落在钮的外扩方框里。

- void RequestTip(App app, int cx, int cy, int r, int v)
  - 值气泡挂在帧末刷新的延迟队列上（盖在后续控件之上）：
    横向悬在钮上方（placement 0 = top），竖向挂在右侧
    （placement 3 = right）。

- bool PaintStyled(App app, int id, int x, int y, int width, int height)
  - 绘制 + 指针定位。返回本帧值是否变化（RenderIn 据此回写
    双向绑定并触发 Change）。轨道/填充/圆钮/标记的颜色全在
    `slider::track/::fill/::thumb/::mark/::mark-label` 规则里，
    悬停与拖拽档由 ResolveEasedPartAs 淡变，代码只管值域→几何。

- override void OnMeasure(App app)
  - 覆写：竖向给 160 高（有标记再加文字列宽），横向给行高（有标记再加
    文字行高），宽缺省 160。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = 以自身矩形 RenderIn。


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
  - 初始化：直径（CSS px），无说明文字。

- Spin()
  - 默认 36px 直径。

- Spin(int size)
  - 指定直径（CSS px）。

- int Diameter(App app)
  - 实际绘制直径：皮肤 height 规则优先，否则按缩放后的 Size。

- string Caption()
  - 说明文字；Tip 为 null 时返回空串。

- int TipFont(App app)
  - 说明文字字号：spin::label 规则优先，否则 small 档。

- override void OnMeasure(App app)
  - 覆写：宽高 = 直径；带说明文字时高度再加间距与文字行高。

- override void OnPaint(App app)
  - 覆写：画 8 点旋转指示圈（高亮头部 + 渐隐尾迹）与可选说明文字；
    可见时按 90ms 相位申请局部动画帧。

- static int OffX(int i)
  - 8 个单位圆偏移（x1000），每 45 度一步，从顶部开始。

- static int OffY(int i)
  - 同 OffX：第 i 个圆点的 y 偏移（x1000，顺时针排列）。

- static bool OnScreen(App app, int cx, int cy, int radius)
  - 指示器包围盒与窗口表面重叠时返回 true。

- static int PivotSafeMul(int per1000, int radius)
  - (offset_x1000 * radius) / 1000，避免溢出意外。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（直径 / 说明文字 / 类）。


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
  - 两个窗格各自允许的最小尺寸（px，默认 60），拖拽下限。

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
  - 分隔条方向常量：0 = 垂直分隔条（左 | 右），1 = 水平（上 | 下）。
    传给构造器或 Orient()。

- static int Horizontal()
  - 方向常量：1 = 水平分隔条（上 | 下）。

- void InitSplitPanel(int o, int size)
  - 各构造器的公共初始化：注册 "split" 控件、设方向与首格尺寸，
    建出两个透明窗格并挂为子项。

- static Panel NewPane(string name)
  - 透明窗格：没有自己的表面，子控件按停靠布局排布，
    因此设计器放进去的绝对定位控件和停靠控件都能工作。

- SplitPanel(int o, int size)
  - 指定方向与首格尺寸（px）。

- SplitPanel(int o)
  - 指定方向，首格默认 240px。

- SplitPanel()
  - 垂直分隔条 + 默认 240px 首格（设计器用）。

- Panel First()
  - 两个窗格的内容落点——往 .First()/.Second() 里 Add 子控件，
    不要 Add 到 SplitPanel 自身（Arrange 只摆这两个窗格）。

- Panel Second()
  - 第二个窗格（分隔条另一侧）的内容落点。

- int Size()
  - 第一个窗格的当前尺寸（px，逻辑像素）。

- void SetSize(int px)
  - 设置第一个窗格的尺寸（px）；跨帧持有，可绑定/持久化。

- SplitPanel MinSizes(int first, int second)
  - 拖拽下限：两个窗格各自允许的最小尺寸（px）。

- SplitPanel Orient(int o)
  - 覆盖分隔条方向（0 = 垂直，1 = 水平；常量见 Vertical/Horizontal）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（orient/size/minFirst/minSecond）。

- override Control SlotHost(int slot)
  - 设计里放进某个窗格的子控件（.zform 的 `childTab`：0 = 第一格，
    1 = 第二格）真正的父节点就是那个窗格——否则它们会挂在
    分栏容器自己身上，而 Arrange 只摆两个窗格，于是全部消失。
    设计时窗格选择：slot 0 → 第一格，slot ≥1 → 第二格。
    （对应 .zform 的 `childTab`。）

- override string GetExtra(string key)
  - 方向与分栏尺寸也可以写成一条 `options`（"vertical|620"），
    这是设计器列表型属性的通用写法。

- override bool SetExtra(string key, string val)
  - 设计器写入 `options`：解析 "vertical|620" 形式的方向与
    首格尺寸；其他键返回 false 交给基类。

- override void OnMeasure(App app)
  - 覆写：自身不主动要尺寸——分栏占满宿主给的矩形
    （显式声明过首选尺寸时优先）。

- override void Arrange(int px, int py, int pw, int ph)
  - 两个窗格按当前分栏尺寸排布，分隔条占中间的
    一条（其宽度按 DPI 缩放，命中区再向两侧放宽）。

- static int BarSize()
  - 逻辑像素的分隔条厚度。静态而非主题值，
    因为 Arrange 拿不到 App（布局在测量之后进行）。

- int Clamp(int want, int total, int bar)
  - 把请求的分栏尺寸夹到两个窗格的最小值之间。

- override void OnPaint(App app)
  - 覆写：画分隔条并登记命中区；按住拖动时把鼠标位置经 Clamp
    写回首格尺寸。


## Statistic (class)

统计组件：大数值上方的小标题。两者都是样式部件，
CSS 中 `statistic::label` 和 `statistic::value` 控制其颜色和
字号。

Statistic s = new Statistic { Text = "Active users", Value = "12,480" };

- Binding<string> Text;
  - 上方小标题（可绑定）。

- Binding<string> Value;
  - 下方大数值（可绑定）。

- void InitStatistic(string label, string amount)
  - 初始化：以标题注册为控件并绑定标题与数值。

- Statistic()
  - 空统计（设计器用）。

- Statistic(string label, string amount)
  - 标题 + 数值。

- string Label()
  - 当前标题；Text 为 null 时返回空串。

- string Amount()
  - 当前数值；Value 为 null 时返回空串。

- override void OnMeasure(App app)
  - 覆写：自然高度（标题行 + 数值行），宽度取两行文本中较宽者。

- override void OnPaint(App app)
  - 覆写：上画小标题、下画大数值（部件 statistic::label/::value）。

- static int Height(App app)
  - 自然高度：标题行 + 间距 + 数值行。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（text/value/class）。


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
  - 空状态栏（之后 AddLeft/AddRight，或 SetItemsText）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（tint/class）。

- override void SetProp(string key, string val)
  - 面板直改 `options` 必须走重建入口（直写字段不会重建状态项）。

- Label NewItem(string text)
  - 建一个 secondary small 文本项并加入条（尚未停靠）。返回标签。

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
  - 设计器写入 `options`（按分隔文本重建状态项，前缀 `>` 靠右）；
    其他键返回 false 交给基类。

- void SetText(int index, string text)
  - 更新第 index 项文本；越界为空操作。

- void SetColor(int index, int color)
  - 设置第 index 项文字颜色；越界为空操作。

- void SetTint(int bg)
  - 纯色背景着色，替换默认表面（0 恢复默认），
    用于宿主想在整个状态栏传达某种模式时。

- override void OnMeasure(App app)
  - 覆写：量出状态栏固定行高与内边距/间距，并落表面着色
    （SetTint 或 `statusbar` 样式的纵向渐变）。

- override void OnPaint(App app)
  - 覆写：画顶部分隔线（表面填充由 OnMeasure 设置的基础 CSS
    样式层完成）。

- void RenderAt(App app, int x, int y, int w, int h)
  - 将状态栏渲染到显式矩形中（供自行管理
    窗口布局的宿主使用）；作为 Control 树的停靠子控件时无需此方法。


## Step (class)

一步：标签、可选描述行与可选的每步图标。以实体存储，
使标签、描述和图标保持为一条连贯记录，而不是三条
必须手动保持索引对齐的并行列表。

- string label;
  - 步骤标题。

- string desc;
  - 次要描述行（"" = 无）。

- string icon;
  - 这一步自己的图标名（图标组件 Icon/IconSvg 认识的语义名）。
    "" 表示按状态绘制默认圆点（对勾 / ! / 数字）。

- Step(string label, string desc, string icon)
  - 构造一步。


## Steps (class)

步骤指示器（Naive UI n-steps）：编号圆点由连接线串联，当前
步骤从信号读取。圆点、连接线和标签都是样式部件
（`steps::marker`、`::connector`、`::title`、`::description`），每个部件按
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
  - 全部步骤（AddStep 一族追加）。

- SignalInt model;
  - 当前步骤内部信号（SyncBinding 自 data 拉入，点击写回）。

- Binding<int> data;
  - 双向绑定当前步骤：`steps.data = wizard.step;` 保持模型
    字段与指示器同步（编译器降级的 Binding）。可选；
    未设置时指示器仅读取内部 SignalInt。

- int ErrorIndex;
  - 标记为出错的步骤；-1 表示无。

- Binding<string> Size;
  - 尺寸变体：small / medium / large（Naive UI size prop）。
    Binding<string> 支持响应式换档。

- string Placement;
  - 次要文字的摆放（Naive UI placement）：""（默认，标题在圆点行
    下方、整列对齐圆点）或 "right"（描述接到标题右列，横向模式的
    Naive vertical 变体）。

- string Status;
  - 整条流程的语义状态（Naive UI status）：success / error /
    warning 覆盖每步状态解析——所有步骤按该状态的 class 解析
    （`steps success`），空白 = 按各步与当前步的相对位置（默认）。

- string FinishIcon;
  - 全部完成态的图标名（`finish` 状态的 `finishIcon`），
    "" = 默认对勾。

- string ErrorIcon;
  - 出错圆点的图标名，"" = 默认 "!"。

- bool Clickable;
  - 允许点击已到达的步骤来切换当前步骤（Naive UI 在
    `on-update:current` 存在时可点）。true 时每步注册命中矩形，
    点击 >= 0 且 < 当前步的落点写入模型。

- int baseId;
  - 命中 id 块首（Clickable 时每步一个，依次递增）。

- bool idTaken;
  - baseId 是否已保留。

- UiEvent Change;
  - 点击已到达步骤时触发（`Clickable` 下命中才触发）。

- int iconPx;
  - 步骤图标的兜底尺寸（标题字号）。

- void InitSteps(SignalInt m)
  - 公共初始化（各构造共用）。

- Steps(SignalInt m)
  - 由调用方信号驱动当前步骤。

- Steps()
  - 自持信号：`Steps s = new Steps();`，之后通过 `.data` 双向绑定当前
    步骤——调用点无需任何信号接线。

- override void SyncBinding()
  - 将绑定的模型值拉入本地信号（model -> UI）。

- void AddStep(string label)
  - 添加一步（无描述、默认圆点图标）。

- void AddStepDesc(string label, string desc)
  - 添加一个带次要描述行的步骤。

- void AddStepIcon(string label, string icon)
  - 添加一个自定义图标的步骤（`icon` 为图标组件认识的语义名；
    空串 = 按状态绘制默认圆点）。

- void AddStepIconDesc(string label, string desc, string icon)
  - 图标 + 描述一步到位。

- static Steps Of(List<string> labels, SignalInt m)
  - 基于现有标签列表的跟踪器，由 `m` 驱动。

- bool IsVertical()
  - 是否为纵向堆叠（Class 含 `vertical`）。

- bool IsRight()
  - 次要信息（标题右列 vs 圆点行下方的整列）。

- int RowGap(App app)
  - 纵向/右列模式的行距。

- void EnsureIds()
  - 点击命中的连续 id 块：需要时一次保留 steps.Count 个，
    保证帧间稳定（每帧重建演示也不漂移）。

- static string StatusNorm(string v)
  - `Status` 白名单归一：只认 success / error / warning，脏值回 ""。

- string StatusCls()
  - 流程整体状态的 class 片段（`steps success`）。

- override void OnMeasure(App app)
  - 覆写：竖向/right 按行数测高，横向取标题 + 描述两行高；宽取样式或内容兜底。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = 在自身矩形 RenderAt。

- int RenderAt(App app, int x, int y, int w)
  - 立即模式入口：在 (x, y) 处按给定宽度绘制，返回保留的
    命中 id 块首（`Clickable` 时每步一个 id，依次递增）。

- void PaintBodyHorizontal(App app, int bx, int by, int bw)
  - 横向：圆点一行，标题/描述列在各自圆点下方（按分格居中）。

- void PaintBodyRight(App app, int bx, int by, int bw)
  - 横向、次要信息在标题右列（Naive UI vertical 变体）：
    圆点一列，标题 + 描述左对齐成两行，行高由两者撑开。

- void PaintBodyVertical(App app, int bx, int by, int bw)
  - 纵向（`vertical` 类）：与右列模式同形，圆点一列、文字在右。

- void PaintText(App app, int i, int current, int errorIndex, string cls, int tx, int cy, int availW)
  - 标题 + 可选描述两行块，圆点右侧、以圆点为轴垂直居中。
    横向与纵向共用（样式与回退一致，只是落点不同）。

- void HandleClick(App app, int current)
  - `Clickable`：点击已到达（i <= current，出错步除外）的圆点
    切换当前步骤并写回绑定。用 ClickAvailable 门控——一次释放
    只被消费一帧，弹层认领的点击不穿透。

- int Radius(App app, string cls)
  - 圆点半径：皮肤 `steps { height }` 给直径（与 MarkerSize 同源），
    缺省按尺寸档缩放——small 10 / medium 13 / large 16（逻辑 px，
    app.Scale 出物理像素）。

- int TitleFont(App app, string cls)
  - 标题字号：`steps::title { font-size }`，缺省按尺寸档。

- static string StateClass(string cls, int index, int current, int errorIndex)
  - 步骤在序列中的位置所对应的类列表，使步骤的每个
    部件（圆点、标签、描述）都由同一个状态解析。
    状态名是设计系统共享的序列词汇。

- void Circle(App app, int cx, int cy, int r, int index, int current, int errorIndex, string cls)
  - 步骤圆点：已完成（对勾 / FinishIcon）、出错（! / ErrorIcon）、
    活动或待处理（数字）；声明了每步图标（AddStepIcon）则优先。

- void Line(App app, int x0, int y0, int x1, int y1, int index, int current, int errorIndex, string cls)
  - 两个圆点之间的连接线，采用后续步骤选定的样式。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override Binding<string> SizeOf()
  - 覆写：返回 Size 绑定字段（设计器经它发现尺寸档位绑定）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（当前步 / 错误步 / 尺寸 / 位置 / 状态 / 图标 / 可点击 / 类）。

- string ItemsText()
  - designer 往返串："Details|Review=第二阶段|Done"（GetExtra/
    SetExtra("items")）；"标题=描述" 的步骤带描述行。

- void SetItemsText(string spec)
  - 解析 ItemsText 的往返串并整体重建步骤列表。

- override string GetExtra(string key)
  - 覆写：应答扩展属性键，"items" 为步骤往返串。

- override bool SetExtra(string key, string val)
  - 覆写：写回扩展属性键，"items" 重建步骤列表。

- override string GetProp(string key)
  - GetProp/SetProp 挂钩：Placement / Status / FinishIcon /
    ErrorIcon / Clickable 没有绑定的 Binding 字段，走字符串往返，
    让文档与设计器读到实时值。

- override void SetProp(string key, string val)
  - 覆写：无绑定字段的键（placement/status/图标/clickable）按字符串直写，
    其余交给基类。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"。

- override void BindEvent(string evt, Action a)
  - 将步骤的 Change 路由到自己的 UiEvent 字段；其余一切落到
    `On` 的公共事件集（其 AddByName 忽略不认识的名称）。


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
  - 展示行缓存（调用方每帧赋值）；null 或空 = 不画。

- int scroll;
  - 滚动偏移（底端锚定的行数，0 = 贴住最新一行），调用方持有。

- bool busy;
  - true 时在记录末尾追加一行 busy 提示（busyText）。

- LogState sel;
  - 文本选择状态（interactive 时由控件更新）。

- bool interactive;
  - 允许指针交互：拖选文本、点击思考折叠行与工具摘要行。

- SignalInt thinkOpen;
  - 思考折叠信号（1 = 展开），点击 kind-4 行切换；null = 无该行。

- List<int> toolOpen;
  - 已展开工具结果的消息索引列表，点击 kind-5 行增删。

- string busyText;
  - busy 行文本（调用方已本地化）。

- int maxScroll;
  - 绘制后写出：本帧的最大滚动偏移，供调用方回读。

- SignalInt scrollBar;
  - 调用方的滚动偏移信号（底端锚定，单位是展示行，0 = 贴住最新一行）。
    给了它就在右侧画一条可拖动的纵向滚动条：只有滚轮的时候，长记录既
    看不出自己停在整段的哪一段，也没法一把拖回去。null = 不画。

- static Label thinkingLbl;

- static SignalInt barSig;

- static SignalInt BarSig()
  - 常驻的滚动条转换信号（惰性创建，避免每帧新分配）。

- static int BarW(App app)
  - 纵向滚动条占掉的宽度。调用方按它收窄换行宽度，最长的一行才不会被
    压在滑块底下。

- static void EnsureThinking(App app)
  - 惰性创建常驻的「思考中」标签（hint small 样式），避免每帧分配。

- StyledText()
  - 空控件；行缓存与各状态由调用方每帧绑定（见类型注释）。

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
  - 常驻的头行头像实例（惰性创建：每个可见轮次都画一个头行，
    复用同一实例避免每行每帧分配控件）。

- static void PaintHeadRow(App app, TextRun ln, int x, int y, int rowH, int rightEdge, StyleBox timeStyle)
  - 绘制消息头行：时刻贴右缘，再从左向右画头像、名字与状态/进度
    芯片；右缘空间不足时宁可丢弃后面的芯片也不压到时刻上。

- override void OnMeasure(App app)
  - 覆写：纯铺满槽位的画布，偏好尺寸取当前边界。

- override void OnPaint(App app)
  - 覆写：绘制整行网格（正文/代码底纹/头行/busy 行），处理文本
    选择、折叠行与工具行的点击及滚动条交互，并写出 maxScroll。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。


## Switch (class)

开关，支持响应式 bool 绑定。

C# 风格保留式实例：
Switch dark = new Switch();
dark.Change += () => { ... };
dark.Render(app, x, y);
为现有调用方保留旧版立即模式静态方法。

增强能力（对位 Ant Design Switch 文档演示）：
- 内嵌文字/图标：`CheckedText` / `UncheckedText` / `CheckedIcon` /
`UncheckedIcon`，有内容时轨道自动加宽；
- 形状：`Round = false` 得方形轨道 + 圆角方形拇指；
- 加载中：`Loading = true` 时拇指上叠画弧线 spinner、轨道变暗，
且点击/键盘不再切换；
- 自定义颜色：`CheckedColor` / `UncheckedColor`（0 = 跟随皮肤）；
- 自定义选中值：`CheckedValue` / `UncheckedValue` + int 辅值绑定轴
`dataValue`（`sw.dataValue = form.status;`），开关表达
「值 == 选中值」。轴优先级：`dataValue`(int) > `data`(bool)。

绑定通道（规范 §6）：外部通道 `data`(bool 主值) 与 `dataValue`
(int 辅值，配 CheckedValue/UncheckedValue 映射)；`onSig` 是内部
开关状态缓冲（SignalBool，§6.2），不是绑定协议。

- SignalBool onSig;
  - 内部开/关状态缓冲（SignalBool，§6.2）：IsOn/SetOn/SyncBinding
    的运行时真相，绘制直接读它。不是绑定协议，别对外消费。

- Binding<bool> data;
  - 主值通道：`sw.data = settings.darkMode;` 保持模型
    字段与开关双向同步（编译器降级的 Binding）。

- Binding<int> dataValue;
  - int 辅值通道（§6.1 dataXxx 命名）：`sw.dataValue = form.status;`
    配 `CheckedValue` / `UncheckedValue`，开关表达「值 == 选中值」。
    该轴激活时 `data` 让位（`GetProp("value")` 也应答于此轴）。

- int wid;
  - 控件命中 id。

- UiEvent Change;
  - 开关切换时触发（C# 风格：`sw.Change += h;`）。

- Binding<string> Size;
  - 尺寸变体：tiny / small / medium / large。
    Binding<string> 支持响应式换档。

- bool Loading;
  - 加载中：轨道变暗 + 拇指叠画旋转弧线，且不再响应点击。

- bool Round;
  - 圆角轨道 + 圆形 thumb；false 得方形（小圆角）形状。

- Binding<string> CheckedText;
  - 选中/未选中时轨道内嵌文字（空 = 无）；有内容时轨道自动加宽。

- Binding<string> UncheckedText;

- Binding<string> CheckedIcon;
  - 选中/未选中时轨道内嵌图标（Icon.zan glyph 名，空 = 无）。

- Binding<string> UncheckedIcon;

- int CheckedColor;
  - 自定义轨道色（Progress fillColor 同款约定：0 = 跟随皮肤）。

- int UncheckedColor;

- int CheckedValue;
  - int 轴的选中值/未选中值（默认 1/0，即等价于 bool）。

- int UncheckedValue;

- void InitSwitch(SignalBool m)
  - 公共初始化（构造共用）。

- Switch()
  - 保留式构造器：`Switch dark = new Switch();`

- bool HasValueAxis()
  - int 辅值轴是否激活（激活时优先于 bool 轴）。

- int IntGet()
  - 读 int 辅值。

- void IntSet(int v)
  - 写 int 辅值。

- bool IsOn()
  - 当前是否处于选中态：int 轴激活时按「值 == CheckedValue」判定，
    否则读内部信号缓冲。

- void SetOn(bool v)
  - 程序化设置开关状态。int 轴激活时写入 CheckedValue/UncheckedValue，
    bool 轴激活时同步 `data` 绑定；均同步内部信号缓冲。
    不触发 Change 事件（用户交互才触发）。

- override void SyncBinding()
  - 将绑定的模型值拉入本地信号（model -> UI）。

- int Render(App app, int x, int y)
  - 渲染开关并处理点击/键盘（Space/Enter）切换，返回控件 wid。
    位置由布局决定，尺寸来自 OnMeasure 的测量结果。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change" 与 "Toggle"。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override Binding<string> SizeOf()
  - 覆写：返回 Size 绑定字段（设计器经它发现尺寸档位绑定）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（值 / 尺寸档 / 加载 / 形状 / 内嵌文字图标 / 自定义色与值）。

- override void BindEvent(string evt, Action a)
  - 覆写："Change"/"Toggle" 都挂 Change 事件，其余按名称走通用路由。

- override string GetProp(string key)
  - 覆写：int 轴激活时 "value" 键按值语义应答，其余交给基类。

- override void SetProp(string key, string val)
  - 覆写：int 轴激活时 "value" 键按选中值语义写入，其余交给基类。

- static int TrackRadius(int trackH, bool round)
  - 轨道圆角：Round 为胶囊（半高），否则小圆角方块（对位 checkbox::box）。

- static int ExpandedTrackW(int cssW, int thumbD, int slotW, int pad)
  - 有内嵌内容时的轨道宽：容纳「拇指 + 两侧间隙 + 内容槽」。

- static int SpinnerPhase(int tickMs)
  - spinner 相位（千分位 0..999）：墙钟每 100ms 转 1/8 圈，
    与 Spin 的节奏同源、与页面实际重绘频率无关。

- static string SizeClsFor(string size)
  - Size 字段 → 皮肤类片段。tiny 是 Switch 独有档位（共享的
    Control.SizeClass 把 tiny 并入 small，这里不借用、不改动它）。

- string ActiveSizeCls()
  - 当前尺寸档的 class 片段（medium 为空串）。

- static int SlotWidth(App app, string text, string icon, int fs)
  - 内嵌内容测量（文字取实测宽，图标取字号级方框）。

- string TextOf(Binding<string> b)
  - Binding 文本兜底读取（null 为 ""）。

- int PaintStyled(App app, int id, int x, int y, bool isOn)
  - 绘制开关主体：轨道、滑块（含投影）、内嵌文字/图标与加载 spinner，
    登记命中矩形并返回 id。

- void DrawInner(App app, Canvas c, int rx, int ry, int rw, int rh, string text, string icon, int color, int alpha, int fs)
  - 画一段内嵌内容（文字在前、图标紧随其后，整体居中于给定槽）。

- override void OnMeasure(App app)
  - 覆写：按尺寸档与内嵌内容解析样式；宽 = 轨道宽（内容可撑宽），高取样式行高。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = 在自身位置 Render。


## Table (class)

带表头和斑马纹行的数据表。

- static void RenderHeader(App app, int x, int y, List<string> columns, List<int> widths)
  - 表头行（主题中等控件高）：columns/widths 按下标配对，长表头
    在列宽内省略，底部画分隔线。

- static void RenderRow(App app, int x, int y, List<string> cells, List<int> widths, bool striped)
  - 普通数据行（无选中）：striped 为 true 时铺斑马纹底。

- static void RenderRowSel(App app, int x, int y, List<string> cells, List<int> widths, bool striped, bool selected)
  - 带选中态的行：选中行铺 `table::row:selected` 的主色浅底
    并在左缘画强调条——与 VirtualList / DataTable 的选中语言
    一致，静态演示也能表达"哪一行是选中的"。


## Tabs (class)

标签页控件：维护标签列表与选中索引，外观有线形/卡片/分段三种，
方向支持水平（标签条在顶部）与垂直（沿左侧）。

- List<TabItem> tabs;
  - 标签页列表（hostModel 下是宿主同步来的影子）。

- int active;
  - 当前选中标签索引（空标签条为 -1）。

- int style;
  - 外观：Line / Card / Segment 常量之一。

- int orient;
  - 方向：Horizontal / Vertical 常量之一。

- bool showAdd;
  - 尾部是否显示「新建标签」按钮。

- bool closableAll;
  - 全局 closable：true 时新建 tab 默认带 X（Naive UI closable）。

- Binding<string> Size;
  - 尺寸变体：small/medium/large（Naive UI size）。Binding<string>
    支持响应式换档，属性面板直接读写选项文本。

- int scroll;
  - 水平标签条的滚动偏移（px）。

- bool follow;
  - 选中项变化后让标签条自动滚到可见；手动滚动清除。

- bool hostModel;
  - 宿主驱动模式：不修改内部列表，只上报动作。

- int ctxIndex;
  - 右键命中的标签索引（TakeContext 消费，-1 无）。

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
  - 待领走的选中变化索引（TakeChanged 消费，-1 无）。

- int closedIndex;
  - 最近一次关闭的标签索引（ClosedIndex 读取，-1 无）。

- int addedIndex;
  - 待领走的「新建标签」插入位置（TakeAdded 消费，-1 无）。

- List<int> closedQueue;
  - 还没被宿主领走的关闭索引，从大到小排队。批量关闭（关闭全部 /
    关闭其他）一次移除多个标签，而宿主每帧只 TakeClosed() 一次并按
    索引删自己的并行数组（页、浏览器…）：只报一个索引，宿主就少删
    几项，剩下的条目全部错位到别人的页上。倒序入队使每个上报的
    索引在宿主删到它之前始终指向自己那一项。

- SignalBool menuOpen;
  - 内置右键菜单的打开状态（延迟一帧消费）。

- int menuTab;
  - 菜单针对的标签索引。

- int menuX;
  - 菜单弹出位置（鼠标处）。

- int menuY;

- SignalInt menuAction;
  - 菜单选中的动作 id（overlay 写入，ApplyMenuAction 消费）。

- SignalInt menuSub;
  - 富菜单的子菜单打开状态（-1 无）。

- int menuBaseId;
  - 菜单命中的 id 块。

- UiEvent TabChanged;
  - 选中标签变化时触发。

- UiEvent TabClosed;
  - 标签被关闭时触发。

- UiEvent TabAdded;
  - 新建标签时触发。

- static string lang="zh";
  - 内置上下文菜单标签的 UI 语言码（"zh" / "en"）。宿主可在
    渲染前改它，让菜单跟随应用语言；默认简体中文，因为不设语言的
    发布版界面本身就是中文的，此时菜单不该是唯一说英语的地方。

- static string TT(string en, string zh)
  - 按键式查找取文案（System.Globalization.Lang）：英文原文
    作键、内置中文为缺省串；语言包未加载时回退缺省串。

- static int Line()
  - 外观常量：线形 / 卡片 / 分段按钮（对应 n-tabs 的 type）。

- static int Card()
  - 卡片外观：带边框的页签卡。

- static int Segment()
  - 分段按钮外观（segment）。

- static int Horizontal()
  - 方向常量：标签条在顶部或沿左侧。

- static int Vertical()
  - 垂直方向：标签条沿左侧排列。

- static Tabs CreateVertical(int style)
  - 垂直标签条（标签沿左侧排列）的快捷构造。

- Tabs():this(0, 0)
  - 默认标签条：Line 外观、水平方向。（没有这个构造，`new Tabs()`
    会跳过唯一的构造函数，留下一个字段全为 null 的对象。）

- Tabs(int style):this(style, 0)
  - 给定外观的水平标签条；垂直轨道用 `new Tabs(style, Tabs.Vertical())`。

- Tabs(int style, int orient)
  - 完整构造：外观 + 方向常量。

- void SetShowAdd(bool on)
  - 尾部是否显示「新建标签」按钮（浏览器式 +）。

- void SetHostModel(bool on)
  - 宿主驱动模式：控件不再拥有标签页集合。
    关闭 / 添加 / 右键操作上报给宿主（宿主拥有
    真实模型并每帧重新同步标签），而不是修改
    内部列表或打开内置上下文菜单。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override Binding<string> SizeOf()
  - 覆写：返回 Size 绑定字段（设计器经它发现尺寸档位绑定）。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"tabs"）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（样式 / 方向 / 新建与关闭 / 尺寸 / 活动页）。

- override void SetProp(string key, string val)
  - 面板直改 `options` 必须走重建入口（直写字段不会重建标签）。

- Panel Page(int i)
  - 标签 i 的页容器，按需补齐（缺页的标签是空白页）。
    页是透明的停靠容器，因此页里的控件既能停靠也能
    绝对摆放，和窗口根一样。内容页要么都走 Page(i)，
    要么都走 Add(label)——页与标签按下标对应。

- int PageCount()
  - 当前页容器数量（与 Count() 同步）。

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
  - 覆写：应答扩展属性键，"options" 为标签标题的序列化串。

- override bool SetExtra(string key, string val)
  - 覆写：写回扩展属性键，"options" 走 SetItemsText 重建标签。

- override List<string> Events()
  - 覆写：公共事件之外提供 "TabChanged"/"TabClosed"/"TabAdded"。

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
  - 上下文菜单涉及的标签索引（读取一次后清零，无则 -1）。

- void Add(string label, bool canClose)
  - 追加一个标签（canClose = 是否显示关闭 X）；
    空标签条上第一个标签自动成为选中项。

- void Clear()
  - 丢掉所有标签页（选中项回到第一页）。声明式界面在语言切换后
    重建标签文本时用它，免得宿主自己再持有一份影子列表。

- int Count()
  - 标签数量。

- int Active()
  - 当前选中标签的索引（空标签条为 -1）。

- string ActiveLabel()
  - 当前选中标签的标题（无选中为 ""）。

- void SetLabel(int i, string label)
  - 改写某个标签的标题（浏览器拿网页标题回填标签时用它，
    而不必把整条标签文本重新拼一遍）。

- string LabelAt(int i)
  - 读取第 i 个标签的标题（越界为 ""）。

- void Select(int i)
  - 编程式选中第 i 个标签：触发 TabChanged 并让标签条滚到可见。

- int ClosedIndex()
  - 最近一次关闭事件的标签索引（读取后不清零，供事件处理器用）。

- int TakeChanged()
  - 领走一次选中变化（无则 -1）：按帧轮询时用它，
    事件订阅（TabChanged）时不必。

- int TakeClosed()
  - 领走一个已关闭的标签索引（没有则 -1）。一帧里关掉多个标签时
    逐帧逐个领，顺序与移除顺序一致（从大到小）。

- int TakeAdded()
  - 领走一次「新建标签」的插入位置（无则 -1）。

- void AddNew()
  - 模拟点击「+」：hostModel 下只上报索引给宿主，
    否则自己加一个可关闭的新标签并选中。

- void EmitClosed(int i)
  - 上报一个被移除的标签：入队给按帧轮询 TakeClosed() 的宿主，
    同时抛事件给订阅 TabClosed 的宿主。

- void ReindexActive(int i)
  - 移除第 i 个标签之后调用：把选中项挪回有效位置
    （空标签条时为 -1）。

- bool Closable(int i)
  - 第 i 个标签是否允许被关闭。固定标签（closable == false）既不画关闭
    按钮，也不能被右键菜单或 CloseAt() 关掉：宿主把它当常驻页，
    关掉它宿主的并行数组就和标签错位了。

- int ClosableCount(int keep)
  - 除第 keep 个之外还有几个可关闭的标签（keep < 0 表示不留）。

- void CloseAt(int i)
  - 关闭第 i 个标签（不可关闭的固定标签忽略）；
    非 hostModel 下同步移除对应页。

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
  - 单个标签的宽度：文本（超长截断省略）+ padding + 关闭按钮。

- int ContentWidth(App app)
  - 全部标签的总宽度（溢出判定用）。

- void Render(App app, int x, int y, int totalWidth)
  - 水平标签条：绘制标签/「+」/溢出箭头并处理点击、右键与滚轮
    （立即模式宿主入口；停靠布局经 OnPaint 转发到这里）。

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
  - 标签文本（Binding<string>，可响应式绑定模型字段）。

- string Icon;
  - 前置图标字形名；"" = 无。

- bool Closable;
  - 添加尾部关闭字形，点击时触发 Close。

- bool Checkable;
  - 可选中：标签变成 toggle 按钮，点击切换 Checked 并触发 Change。
    对应 Naive UI Tag 的 `checkable` prop。

- Binding<bool> Checked;
  - Checkable=true 时绑定到 bool 模型；切换时在皮肤
    中进入 `tag:selected` 状态（类似 button:checked）。

- int wid;

- UiEvent Close;
  - 点击关闭字形时触发；像 Button.Click 一样在 UI 线程排队，
    处理器可从中移除模型中的该标签。

- UiEvent Change;
  - Checkable 切换时触发（`tag.Change += h;`）。

- void InitTag(string label)
  - 初始化：以标签文本注册，置默认字段并自管 Close/Change 事件。

- Tag()
  - 空标签（设计器用）。

- Tag(string label)
  - 给定初值文本的标签。

- string Label()
  - 当前文本；Text 为 null 时返回空串。

- StyleBox ResolvedStyle(App app)
  - 解析标签自身的样式盒（`tag` 规则 + 类/名字）。

- int AutoWidth(App app)
  - 按当前文本与样式算出的自然宽度。

- int Render(App app, int x, int y)
  - 按自然宽度绘制；返回控件 id。

- int RenderW(App app, int x, int y, int w)
  - 以给定宽度绘制；比自然宽度窄时由共享盒子层省略
    标签文本。

- bool IsChecked()
  - Checkable 时的当前选中态；未绑定 Checked 为 false。

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

- static void ChipFlow(App app, int x, int wrapW, string text, string cls, int cx, int cy, out int nx, out int ny)
  - 流式排布的立即模式标签：从游标 (cx, cy) 起顺排，越过
    [x, x+wrapW] 右缘就换行（行距 8）。新游标 (nx, ny) 经
    out 返回、由调用方持有，连续调用即组成一串自动换行的
    标签行——窄容器（手机卡片）靠它把标签排进可见区，而
    不是按桌面固定偏移排到界外。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（text/icon/closable/checkable/
    checked/class）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Close"。

- override void BindEvent(string evt, Action a)
  - 将关闭字形的 Close 路由到其 UiEvent 字段；其余事件
    全部落入 `On` 的公共绑定。

- override void OnMeasure(App app)
  - 覆写：宽度取样式表或自然宽度，高度取样式表或主题小行高。

- override void OnPaint(App app)
  - 覆写：在停靠边界内垂直居中并按自然宽度绘制（空间不足时
    省略文本，不越界）。


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

能力面（对齐 Naive UI 的 type="textarea"）：`Disabled`（继承自
Control）、`SetStatus` 校验态描边、`ShowCount`+`MaxLen`
(+`CountGraphemes`) 字数统计、`Round` 圆角、`Filter` 输入过滤，
以及 Clear/Focus/Blur/SelectAll/ScrollToEnd 手动操作。

- SignalString model;
  - 内部编辑缓冲（SignalString）；外部通道是 data。

- Binding<string> data;
  - 双向绑定文本：`notes.data = doc.body;` 保持模型字段与
    文本框双向同步（编译器降级的 Binding）。

- string hint;
  - 占位文本（内容为空且未聚焦时显示）。

- int cursorPos;
  - 光标位置（字节偏移）。

- int selAnchor;
  - 选区的固定端（字节偏移），-1 表示无选区。光标是活动端，
    因此 Shift+方向键、拖动和 Ctrl+A 都只移动 `cursorPos`。

- int scrollY;
  - 垂直滚动偏移（px）。

- int wid;
  - 控件命中 id。

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

- string errMsg;
  - 校验错误信息；"" 表示有效。SetError 会绘制红色边框，并
    把 `msg` 显示在字段下方，直到用户编辑（或调用 ClearError）。
    与 Input 的校验 API 同款，FormGroup 对字段统一调用。

- bool readOnly;
  - 只读展示：文字仍可点选、拖选、Ctrl+A / Ctrl+C，但任何按键都改不了
    内容，也不画输入框的外框——用来显示一段可复制的正文。

- bool autoHeight;
  - 高度跟着内容走（放进滚动列里的正文用），而不是固定一格。

- App liveApp;
  - 由本帧渲染沿记一笔宿主 App：手动操作（Focus/Blur/Clear/
    ScrollToEnd）要在没有 app 形参的事件回调里改焦点、请求重绘。

- int MaxLen;
  - 输入字符上限（>0 生效），按码点计；CountGraphemes 打开时按
    字素簇计（与 ShowCount 显示同一口径）。0 表示不限。

- bool ShowCount;
  - 是否显示字数统计（"当前 / 上限"），画在框内右下角。

- bool CountGraphemes;
  - 计数口径：true 按字素簇，false 按码点。

- string FieldStatus;
  - 校验状态：""/success/warning/error，映射 base.css 字段状态类。

- bool Round;
  - 圆角框（整高半圆角），走 `.round` 类。

- InputFilterFn Filter;
  - 输入过滤（Naive UI allow-input）：收到插入后的完整候选文本，
    返回清洗后的文本；null 表示拒绝该插入。

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

- string GetText()
  - 当前文本。

- TextArea ReadOnly(bool on)
  - 只读：可选中复制，不可编辑。

- bool IsReadOnly()
  - 是否只读。

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
  - 覆盖文本：光标收到文末、清空选区并写回绑定。

- override void SyncBinding()
  - 将绑定的模型值拉入编辑缓冲区（model -> UI）。

- void PushBinding()
  - 通过绑定把编辑缓冲区写回（UI -> model）。

- void Edited()
  - 所有 UI 编辑都汇入这里：先写回，再通知。

- void SetError(string msg)
  - 给字段标记校验错误；绘制红色边框，并显示
    `msg` 于字段下方，直到用户编辑（或调用 ClearError）。

- void ClearError()
  - 清除待处理的校验错误（任何编辑时自动调用）。

- void SetStatus(string v)
  - 设置校验状态（""/success/warning/error），即 base.css 的字段
    状态类。与 errMsg 互不影响：前者描边，后者带消息。

- int CountOf(string s)
  - 当前文本的计数值（按 CountGraphemes 口径）。

- int CountValue()
  - 当前文本的计数值。

- void SelectAll()
  - 选中全部文本（等同于用户按 Ctrl+A）。

- void Clear()
  - 清空内容（Naive UI ref.clear()）。触发 Change，光标归零。

- void Focus()
  - 以编程方式把焦点抢到本文本框（Naive UI ref.focus()）。
    禁用态不响应。

- void Blur()
  - 交出焦点（Naive UI ref.blur()）。

- void ScrollToEnd()
  - 光标移到文末（渲染沿本就会把光标滚进视口）。

- void ScrollToStart()
  - 光标移到文首。

- void ClampCursor()
  - 光标与选区锚点钳回文本范围内。

- bool HasSel()
  - 是否存在非空选区。

- int SelStart()
  - 选区起点（较小端；无选区即光标位置）。

- int SelEnd()
  - 选区终点（较大端）。

- string SelText()
  - 选区文本（无选区为 ""）。

- void DeleteSel()
  - 删除选中的范围，并把光标收拢到其起点。

- bool Insert(string s)
  - 插入的唯一闸门：先替换选区，再过宿主过滤器与字符上限。
    被拒绝时完整回滚（选区也还回去）。与 Input 同款：过滤后
    整体被改写时光标按净增字节平移，上限按 CountOf 口径计。

- bool DeleteCandidate(string cand)
  - 退格/删除的候选整体再过一次过滤器：删除路径也可能"暴露"
    出非法文本（如 trim 场景删掉词边界字母露出空格）。

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
  - 键盘输入处理（聚焦且本帧拥有按键时由 Render 调用）：
    编辑键、Ctrl 快捷键、基于显示行的导航。

- int PosFromMouse(App app, int innerX, int innerY, int lineH, int fs)
  - 鸠标位置对应的字节偏移（行高/滚动位置均取当前帧）。按显示行算：
    点在折下来那半句上，光标就落在那半句里。

- int Render(App app, int x, int y, int w, int h)
  - 每帧渲染与交互（立即模式入口；停靠布局经 OnPaint 进入）：
    折行、滚动、选区/光标、滚动条与底部校验/计数行。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"textarea"，皮肤与单行 Input 区分）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（占位 / 文本 / 状态 / 长度 / 计数 / 圆角）。

- override string GetExtra(string key)
  - 文本经由 SetText 写入，使光标保持在范围内。

- override bool SetExtra(string key, string val)
  - 覆写：写回扩展属性（文本 / 状态 / 长度 / 计数 / 圆角）；
    文本经 SetText 写入，保护光标与选区。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"。

- override void BindEvent(string evt, Action a)
  - 覆写："Change" 挂 Change 事件，其余按名称走通用路由。

- int BottomRowH(App app)
  - 框外底行（show-count 文本与校验消息共用同一排，框下沿 2px 起）。
    父容器渲染子树时会把自己的矩形作为裁剪带（Control.RenderTree 的
    PushClip），越出测量高度的绘制会被整条裁掉——所以这一行必须计入
    测量高度，框体本身相应只占 bh - 本值，Render 内的描边/命中矩形
    仍是框的真实高度。

- override void OnMeasure(App app)
  - 覆写：autoHeight 时按内容测高，否则取样式高；底行（计数/校验）计入测量高。

- override void OnPaint(App app)
  - 覆写：保留模式绘制 = Render，框体高度让出底行。


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
  - 构造一条空展示行并置默认值（meta/owner/pct 置 -1）。


## Timeline (class)

时间线：每个条目一个圆点（或图标）和连接线，旁边是
时间 / 标题 / 描述块。圆点、连接线和三行文本都是
样式部件（`timeline::dot`、`::line`、`::time`、`::title`、`::desc`），
皮肤可重设样式，代码侧只需简单赋值。

形态全部走 class/字段，与 Steps 同一套词汇：
- `Class = "large"` / `Size = "large"`：大小档（medium 默认）。
- `Class = "right"`：轴线镜像到右侧，文字右对齐。
- `Class = "horizontal"`：轴线横贯顶部，每条目占一列。
- 条目 `icon`：圆点换成图标；条目 `pending`：空心圈 + 虚线尾。

Timeline tl = new Timeline { Class = "primary" };
tl.AddItem("09:00", "Created", "Project initialized");
tl.Add(new TimelineItem("12:30", "Published", "", "success", ""));

- List<TimelineItem> items;
  - 已添加的条目（AddItem/Add 追加）。

- Binding<string> Size;
  - 尺寸档（small/medium/large），双向可绑定。

- void InitTimeline()
  - 初始化：以 "timeline" 注册，置空条目表与中档尺寸。

- Timeline()
  - 空时间线（之后 AddItem/Add）。

- void Add(TimelineItem it)
  - 追加一个已构建的条目。

- static Timeline Of(List<TimelineItem> its)
  - 基于已构建的条目列表创建时间线。

- void AddItem(string time, string title, string desc)
  - 追加一个默认样式条目（时间 / 标题 / 描述）。

- void AddItemIcon(string time, string title, string desc, string cls, string icon)
  - 带语义类与图标的入口（对齐 Steps 的 AddStepIcon 命名）。

- int Count()
  - 条目数（测量与测试用）。

- bool IsHorizontal()
  - 当前形态是否为横排（Class 含 "horizontal"）。

- bool IsRight()
  - 轴线是否镜像在右侧（Class 含 "right"）。

- override Binding<string> SizeOf()
  - 尺寸档绑定（small/medium/large，默认 medium），供设计器读写。

- string FullCls(App app)
  - 参与样式解析的完整类串：用户 class + 尺寸档片段
    （皮肤用 `timeline.large` 规则改点径/字号）。

- static int DotRadius(App app, string cls)
  - 圆点半径：皮肤 `timeline::dot { height }` 给物理直径，
    否则按尺寸档 small 8 / medium 10 / large 14（app.Scale）。

- static int TimeFont(App app, string cls)
  - 时间行字号：皮肤 timeline::time { font } 优先，否则 small 档。

- static int TitleFont(App app, string cls)
  - 标题行字号：皮肤 timeline::title { font } 优先，否则按尺寸档
    取 large / medium。

- static int DescFont(App app, string cls)
  - 描述行字号：皮肤 timeline::desc { font } 优先，否则 small 档。

- static int RowHeightFor(App app, string cls)
  - 竖排条目行高：三行文本 + 间距 + 兜底（容纳点/图标），
    字号随尺寸档变化（按 cls 解析样式）。

- static int RowHeight(App app)
  - 兼容旧调用：默认（无尺寸档）行高。

- override void OnMeasure(App app)
  - 覆写：竖排按行高累计（末条待办多留半行虚线尾），横排取
    列块高度；宽度取样式表或兜底 240。

- int HBlockHeight(App app, string cls)
  - 横排一列的文本块高度（时间/标题/描述逐条换行）。

- override void OnPaint(App app)
  - 覆写：画轴线、圆点/图标与每条目的时间/标题/描述
    （横排时轴线横贯顶部、条目逐列排布）。

- void PaintLine(Canvas c, bool right, int contentR, int textL, int y, string text, int color, int font)
  - 一行文本：右对齐版把起点算到 `contentR - w`，左对齐版直接
    从 textL 起画。

- void PaintTail(Canvas c, int x, int y, int lw, int len, int color, bool pending, bool horizontal, App app)
  - 条目尾连接线：待办条目画虚线段（dash/gap 各 Scale(4)），
    普通条目画实心。vertical=false 时横着画。

- void PaintMarker(App app, Canvas c, StyleBox dot, string icon, int cx, int cy, int r)
  - 圆点：`icon` 非空画语义图标（颜色随圆点样式走），否则画
    描边圈（外圈圆点色 + 内圈底色透出）。图标盒比点圈大一档
    （直径 r*2 + Scale(4) 居中），字形才清晰可辨。

- void PaintHorizontal(App app, string cls, int n, int r, int lw, int timeFont, int titleFont, int descFont)
  - 轴线横贯顶部，每条目占一列等宽；点在上、列内容在下。

- void PaintWrapped(Canvas c, string text, int x, int y, int right, int color, int font, int bottom)
  - 横排列内容的朴素逐词换行（按空格断词，整词绘制；
    中文串无空格时整行不换，由列宽兜底）。

- string ItemsText()
  - 条目的设计器往返串：`时间;标题;描述;类;图标[;pending]`，
    条目间用 `|`（见 ItemsText 注释）。

- void SetItemsText(string spec)
  - 从 ItemsText 同格式串重建条目列表（设计器 / .zform 用）。

- override string GetExtra(string key)
  - 设计器额外键：`items`（条目往返串）。

- override bool SetExtra(string key, string val)
  - 设计器写入 `items`；其他键返回 false 交给基类。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（size/class）。


## TimelineItem (class)

时间线条目：时间/元信息标签、标题、描述、圆点携带的类
（`success`、`error`、…——共享语义角色）、可选的自定义图标
（`icon`，图标组件认识的语义名），以及可选的待办态
（`pending`：空心圈 + 虚线尾）。

- string time;

- string title;

- string description;

- string cls;

- string icon;

- bool pending;

- TimelineItem(string time, string title, string description):this(time, title, description, "", "")
  - 不带语义类的条目：圆点用默认样式。

- TimelineItem(string time, string title, string description, string cls):this(time, title, description, cls, "")
  - 带语义类的条目（success / error / …，决定圆点配色）。

- TimelineItem(string time, string title, string description, string cls, string icon)
  - 带自定义图标的条目：`icon` 非空时圆点改画该图标，
    颜色取圆点样式的前/背景（随语义角色走）。


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
  - 空工具条（之后 Add/AddRight/AddSeparator，或 SetItemsText）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "ItemClick"。

- override void BindEvent(string evt, Action a)
  - 覆写："ItemClick" 挂 ItemClick 事件，其余按名称走通用路由。

- void OnItemClick(ToolItemCallback cb)
  - 任何一项被点击时回调，参数为项的索引。

- void FireItem(int index)
  - 向已注册的处理器发布一次项点击。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（iconOnly/tint/class）。

- override void SetProp(string key, string val)
  - 面板直改 `options` 必须走重建入口（直写字段不会重建按钮项）。

- int Count()
  - 项数（含分隔条）。

- Button ItemAt(int i)
  - 第 i 项；越界返回 null。

- Button NewItem(string text, string icon)
  - 建一个按钮项并加入条（尚未停靠）：iconOnly 且 icon 非空时
    不显示标题；Click 已接到项点击派发。返回按钮。

- int Add(string text, string icon)
  - 追加一个左对齐项；返回其索引供后续访问。

- int Add(string text)
  - 追加一个无图标的左对齐项。

- void Clear()
  - 丢掉所有项。声明式界面在按钮集合随状态变化（如调试器
    的启动 / 暂停两套按钮）时重建工具条，用它代替宿主自己
    持有一份影子列表。

- int AddRight(string text, string icon)
  - 追加一个右对齐项（右侧分组按添加顺序从右往左排）。

- int AddRight(string text)
  - 追加一个无图标的右对齐项。

- int AddSeparator()
  - 竖直分隔条：一个禁用的窄项，用来分组按钮。

- void SetItemsText(string spec)
  - 按序列化文本重建全部项：每条 `"标题:图标"`，
    前缀 `>` 靠右，`-` 为分隔条。设计器与 .zform 走这条路径。

- void SetIconOnly(bool on)
  - 切换只显示图标模式；只影响之后新建的项——已有项的标题在
    建入时已按当时的模式处理。

- override string GetExtra(string key)
  - 项集合是一个列表，不是字段，因此走 extra 属性：
    设计器 / .zform 的 `options` 由此真正建出按钮。

- override bool SetExtra(string key, string val)
  - 设计器写入 `options`（按分隔文本重建按钮项）；其他键返回
    false 交给基类。

- void SetTint(int bg)
  - 纯色背景着色，替换默认表面（0 恢复默认）。

- override void OnMeasure(App app)
  - 覆写：量出工具条内边距/间距与固定行高，并落表面着色
    （SetTint 或 `toolstrip` 样式）。

- override void OnPaint(App app)
  - 覆写：画底部细分隔线（表面填充已由 OnMeasure 设置的基础
    CSS 样式层完成）。

- void RenderAt(App app, int x, int y, int w, int h)
  - 渲染到显式矩形（供自管布局的宿主使用）；作为
    控件树里的停靠子控件时无需此方法。


## Tooltip (class)

锚定到某一点的提示气泡，带小三角。支持四种
位置（上 / 下 / 左 / 右），长文本自动换行，
并且有高度上限，换行内容
溢出时显示滚动条指示。

- static int FontSize(App app)
  - 提示正文字号：tooltip 规则优先，否则 small 档。

- static int Top()
  - 位置常量：气泡在锚点上方（0）。

- static int Bottom()
  - 气泡在锚点下方（1）。

- static int Left()
  - 气泡在锚点左侧（2）。

- static int Right()
  - 气泡在锚点右侧（3）。

- static void TriDown(Canvas c, int cx, int topY, int half, int h, int color)
  - 实心向下三角形（宽底在 `topY` 上方，尖在下方）。

- static void TriUp(Canvas c, int cx, int topY, int half, int h, int color)
  - 实心向上三角形（尖在 `topY`，宽底在下方）。

- static void TriLeft(Canvas c, int leftX, int cy, int half, int w, int color)
  - 实心向左三角形（尖在 `leftX`，宽底在右侧）。

- static void TriRight(Canvas c, int leftX, int cy, int half, int w, int color)
  - 实心向右三角形（尖在 `leftX`+w，宽底在左侧）。

- static List<string> WrapLines(string text, int fs, int maxW)
  - 贪婪地将 `text` 按词换行，使每行不超过 `maxW` 像素。
    没有空格的长单词按字符硬拆。

- static int TipCapH(App app)
  - 气泡内容的高度上限（超出即出现滚动条指示与滚动）。

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

- static int DefaultDelayMs()
  - 显式延迟版本：调用方在悬停稳定 `delayMs` 毫秒后才调用
    Request 一次（Input.Button 的 Tip 已有 900ms 阈值）。这里保留
    钩子便于未来把所有内嵌悬停提示迁移到统一计时器。
    Naive UI Tooltip 的 `delay` prop 即此参数。

- static void Flush(App app)
  - 绘制本帧请求的提示（若有）并清空队列。


## Transfer (class)

穿梭框（Transfer）：左右两栏列表，左栏勾选后经中缝按钮「穿梭」
到右栏，右栏勾选可移回。每一栏是一块独立的虚拟化面板——头部
（全选 / 计数 / 清除）、可选搜索框、复选行区与空态占位——所以
四万项与一屏同开销。绑定的是调用方自己的两个列表（不拷贝），
行文本由 `CellOf<T>` 委托读出（未给时取 `item.ToString()`）。

Transfer<string> t = new Transfer<string>();
t.Bind(model.left, model.right);       // 两个 List<string>
t.WithSearch();                         // 两栏各加一个过滤框
t.Change += () => { Save(); };          // 数据移动后触发
t.Render(app, x, y, w, h);              // 每帧画进这块框

勾选态是控件的瞬时 UI 状态（不反向写进宿主列表），`Checked()` /
`CheckedRight()` 随时可读。中缝的移动按钮走保留态 `Button`
（primary 皮肤、Disabled 联动对应侧的勾选数），因此外观与
全库按钮一体，宿主也能经 `MoveRightButton()` 直接拿到引用定制。
移动只搬「当前过滤结果里被勾的」行：滚多远都不会丢点。

- List<T> source;
  - 绑定的源侧列表（宿主列表本体，不拷贝）。

- List<T> target;
  - 绑定的目标侧列表（宿主列表本体，不拷贝）。

- TransferPane left;
  - 左 / 右栏各自的瞬时状态（勾选、滚动、过滤）。

- TransferPane right;

- CellOf<T> label;
  - 行文本委托（未设时取 `item.ToString()`）。

- IconOf<T> iconOf;
  - 行首图标委托（null = 不画图标）。

- bool searchable;
  - 是否在两栏头部下各显示搜索框。

- string leftHint;
  - 左 / 右栏搜索框占位文字。

- string rightHint;

- bool oneWay;
  - 单向模式：只可右移，右栏只读、左移按钮消失。

- string emptyText;
  - 空栏占位文案。

- string selectAllText;
  - 表头文案：全选复选框标题 / 计数 / 已选 / 清除。

- string totalText;

- string selectedText;

- string clearText;

- int pendRow;
  - 按住拖拽多选：按下行暂存的切换（抬起帧统一入账，保住
    Ui.Clicked 的单次语义）、拖过即置的目标态，以及「上一次
    按下是否成过一次拖动」——双击的第二次按下若紧接一次拖动
    后落在同一行，不能把刚入账的切换再翻回去。

- int pendPane;

- bool pendOn;

- bool wasPaint;

- bool lastDrag;

- Button rightBtn;
  - 中缝移动按钮（保留态 Button，不入树 children——由 Render
    每帧显式绘制；文字非空时文字优先于图标）。

- Button leftBtn;

- UiEvent Change;
  - 数据移动后触发（宿主读回 source/target 或做持久化）。

- UiEvent SelectChanged;
  - 任意一侧勾选集合变化。

- int opW;
  - 表头 / 中缝 / 搜索框的布局度量，ComputeMetrics 里算好，
    OnMeasure 与 Render 共用（保证宿主给的框高第一次就够）。

- int opGap;

- int opH;

- int searchH;

- int headH;

- void InitTransfer()
  - 初始化：置空双栏数据与默认文案（全选/共/已选/清除/无数据）、
    中缝按钮与布局度量。

- Transfer()
  - 默认构造：绑定前两侧都是空列表。

- Transfer(List<T> src, List<T> tgt)
  - 直接带初始数据（宿主通常用 Bind 保留实时视图）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"transfer"）。

- Transfer<T> Bind(List<T> src, List<T> tgt)
  - 绑定两侧列表。不拷贝：每帧都从中读行，所以宿主改动列表
    （增删条目）后下一帧即生效；勾选标记按行序自动伸缩。

- void Refresh()
  - 标记两侧过滤行序过期（宿主在 Bind 之外改了数据时调用）。

- void Invalidate()
  - 使两侧过滤行序缓存失效（下次 Order 重建）。

- Transfer<T> WithLabel(CellOf<T> text)
  - 行文本委托（未设时取 `item.ToString()`）。

- Transfer<T> WithIcon(IconOf<T> icon)
  - 行首图标委托（返回 Gui.Icon 名）。

- Transfer<T> WithSearch()
  - 两栏头部下各加一个搜索框（按行文本子串过滤，忽略大小写）。

- Transfer<T> WithSearchHint(string hint)
  - 搜索框占位文字（两栏同一文案）。

- Transfer<T> WithSearchHints(string lh, string rh)
  - 两栏各自的搜索框占位文字。

- Transfer<T> WithOneWay()
  - 仅右移：左向按钮彻底消失，右栏变成只读列表（不可回退）。

- Transfer<T> WithEmptyText(string text)
  - 空栏占位文案（默认「无数据」）。

- Transfer<T> WithHeaderText(string selectAll, string total, string selected, string clear)
  - 表头文案（按中文默认：全选 / 共 / 已选 / 清除；空参不改）。

- Transfer<T> WithOperationIcons(string rightIcon, string leftIcon)
  - 中缝按钮图标（两枚，默认 chevron-right / chevron-left）。

- Transfer<T> WithOperation(string rightLabel, string leftLabel)
  - 中缝按钮文字（Ant Design operations：给了文字就取代图标）。

- Button MoveRightButton()
  - 暴露中缝移动按钮：宿主可继续定制（Text / Icon / Tip /
    OnClick 追加自己的逻辑）。
    暴露中缝移动按钮：宿主可继续定制（Text / Icon / Tip /
    OnClick 追加自己的逻辑）。

- Button MoveLeftButton()
  - 暴露中缝回退按钮（同上；单向模式下不可见）。

- int SourceCount()
  - 源侧 / 目标侧条目数。
    源侧 / 目标侧条目数。

- int TargetCount()
  - 目标侧条目数。

- int VisibleCount(bool isSource)
  - 某侧在当前过滤关键词下可见的行数（Order 缓存的重建结果）。

- List<T> Source()
  - 源侧绑定的列表本体。

- List<T> Target()
  - 目标侧绑定的列表本体。

- bool IsChecked(T item)
  - `item` 当前所在一侧是否被勾选。

- bool FlagAt(TransferPane p, int i)
  - 某栏第 i 行的勾选标记（越界返回 false）。

- void SetChecked(T item, bool on)
  - `item` 所在一侧的勾选开关；找不到（还没 Bind）则为空操作。

- void SetFlag(TransferPane p, int i, bool on, int n)
  - 写某栏第 i 行的勾选标记；变化时 Post SelectChanged。

- List<T> Checked()
  - 左栏当前被勾选的实体（按行序）。

- List<T> CheckedRight()
  - 右栏当前被勾选的实体。

- int CheckedSource()
  - 两侧当前被勾选的行数（头部计数与半选态用）。

- int CheckedTarget()
  - 右栏当前被勾选的行数（单向模式恒 0）。

- List<T> CheckedIn(List<T> data, TransferPane p)
  - 收集某栏被勾选的实体（按行序）。

- int CheckedCount(TransferPane p, int count)
  - 某栏前 count 行中被勾选的行数。

- void MoveRight()
  - 编程式移动：把源侧被勾选项搬到目标侧末尾（保持行序），
    触发 Change。宿主在自定义移动入口里调用。

- void MoveLeft()
  - 编程式移动：把目标侧被勾选项搬回源侧末尾（保持行序），
    触发 Change。单向模式下等价空操作（按钮不可见）。

- void ClearChecked(bool all)
  - 清空两侧的勾选标记（all=false 只清当前过滤结果里的勾选）。

- void ClearAll()
  - 编程式「清除」：与右栏表头清除链接同一实现（清掉右栏全部
    勾选；单向模式右栏只读，为空操作）。

- void ClearPane(TransferPane p, List<T> data, bool all)
  - 清一栏勾选：all=true 清全部，false 只清当前过滤结果里的。

- void SelectAll(bool isSource, bool on)
  - 程序化全选/取消勾选某侧（只作用于当前过滤结果的可见行，
    与表头全选框同一语义）。单向模式的右栏只读，为空操作。

- void SelectAllVisible(bool isSource)
  - 勾选/取消勾选某侧当前过滤结果的全部可见行（表头全选框的
    单向对位：on=false 即清空可见勾选）。

- void Move(TransferPane from, List<T> fd, TransferPane to, List<T> td)
  - 移动核心：按源侧当前可见行序（过滤时只搬看得见的勾选）
    把勾选项 append 到目标侧末尾，并从源侧原位删除
    （倒序删避免行序漂移）。搬完源侧勾选清零。

- void SyncFlags(TransferPane p, int n)
  - flags 与数据长度对齐（宿主在 Bind 之外改了列表时自愈）。

- List<int> Order(TransferPane p, List<T> data)
  - 一栏当前过滤后的行序（缓存：数据计数与关键词都没变就复用）。

- string LabelOf(T item)
  - 行文本：label 委托优先，缺省 `item.ToString()`。

- Input SearchBox(bool isSource)
  - 跨帧保留的搜索框（未渲染前为 null）：宿主经它定制
    Clearable、MaxLen 等 Input 能力。

- Transfer<T> SetQuery(bool isSource, string q)
  - 程序化设定某侧过滤关键词（远程搜索 / 代码驱动筛选用）：等价于
    往那侧搜索框里打字，行序缓存随之失效。渲染过搜索框时同步回显。

- string Query(bool isSource)
  - 某侧当前的过滤关键词（"" 表示未过滤）。

- bool RowCenter(bool isSource, int dataIdx, out int cx, out int cy)
  - 第 `dataIdx` 行（原列表下标）在屏幕上的中心点（cx/cy 出参）。
    尚未渲染过（Render 之前的空档）或该行不在该侧时返回 false。

- void Render(App app, int x, int y, int w, int h)
  - 每帧入口（保留模式由 OnPaint 代调）：把穿梭框画进
    (x,y,w,h)。先摆中缝按钮，再画两栏面板。

- void ComputeMetrics(App app)
  - 量出跨帧一致的布局度量（OnMeasure 与 Render 共用，
    保证宿主给的框高第一次就对）。

- bool HeadSelectAll(App app, TransferPane p, List<int> order, bool isSource, int hx, int iy, int headH, int hy, int fs, int fg)
  - 表头全选框（两栏共用）：画框（全勾/半选/空）+ 文案 + 命中
    区，点击时对当前过滤结果的可见行整体勾选/取消。返回本帧
    是否有勾选变更。单向模式右栏只读，不画框（行区同口径）。

- bool FlushPaint(TransferPane p)
  - 抬起帧入账一栏的按下/拖拽（见 PaintRange）。单向模式右栏
    只读、整件禁用：锚点直接作废，不入账。

- static int PaintRange(TransferPane p, int a, int b, bool val)
  - 抬起入账：把锚点行与抬起行之间的闭区间（含端点、方向无关）
    统一置为锚点按下时暂存的目标态；带外行一律不碰（橡皮筋只染
    扫过的带）。单击（a==b）即翻一行——与旧 Clicked 路径同语义，
    双击连点回到原状态。无头回归直接重放本函数（见
    tests/gui/transfer_test.zan）；越界端点自动收窄到 flags 内。
    返回变更的行数（调用方据此决定 Post 一次）。

- int CheckRange(bool isSource, int a, int b, bool val)
  - 程序化范围勾选（按住拖拽扫带入账的同一核心，也供无头回归
    重放）：把原列表下标闭区间 [a,b]（两端大小不限）整段置为
    val，带外行不碰。返回变更行数；单向模式右栏只读，空操作。

- bool InPaintBand(TransferPane p, int i)
  - 本行是否落在当前按下-拖拽带里（区间含端点、方向无关）。
    未按下时恒 false；行区渲染据此画预览态。

- bool RenderPane(App app, TransferPane p, List<T> data, int x, int y, int w, int h, bool isSource)
  - 一栏：头部（全选 / 计数 / 清除+已选）、可选搜索框、虚拟化
    复选行区（按住拖拽扫带多选）、空态。返回本帧是否有状态
    变化（需要重绘）。

- override void OnMeasure(App app)
  - 覆写：量出布局度量后给默认尺寸（两栏各约 160 宽、一屏 6 行）。

- override void OnPaint(App app)
  - 覆写：在自身矩形内绘制双栏穿梭框并处理交互（见 Render）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"/"SelectChanged"。

- override void BindEvent(string evt, Action a)
  - 覆写："Change"/"SelectChanged" 挂各自 UiEvent，其余按名称走
    通用路由。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（oneWay/searchable/emptyText/
    searchHintLeft/searchHintRight）。


## TransferPane (class)

穿梭框一栏的瞬时状态：勾选标记、滚动、过滤行序与内嵌搜索框。
勾选标记与绑定的列表按行序平行伸缩；order 缓存过滤后的
原列表下标，计数与关键词都没变就不重扫。

- List<bool> flags;
  - 每行勾选标记，与绑定的列表按行序平行伸缩。

- SignalInt scroll;
  - 行区滚动偏移（像素）。

- List<int> order;
  - 过滤后的行序缓存（存原列表下标）。

- Input search;
  - 内嵌搜索框（该栏渲染过才有）。

- string query;
  - 当前过滤关键词。

- int paintStart;
  - 按住拖拽多选的锚点：按下行的原列表下标（-1 = 未在拖）与
    拖到的最远行下标；两端闭区间内的行取锚点目标态（橡皮筋
    只染扫过的带），带外行保持真实勾选。

- int paintEnd;

- bool paintVal;
  - 锚点按下时暂存的切换（抬起帧统一入账，保住双击=两次翻勾
    的既有语义）：拖拽期间行画的是两端闭区间算出的预览态。

- int bodyX;
  - 命中区几何缓存（RenderPane 行区开画前记录）：RowCenter 据
    此把「第 i 行的屏幕位置」算给宿主拖放对接与无头回归重放。

- int bodyY;

- int bodyW;

- int bodyH;

- int rowHPx;
  - 行高像素（<=0 表示该栏尚未渲染过，RowCenter 直接失败）。

- int orderFor;
  - 最近一次量出 order 时数据计数与关键词。

- string orderQuery;

- TransferPane()
  - 构造空面板（滚动归零、无搜索框、锚点未按下）。


## TreeNode (class)

树的一个节点，按先序排列。`depth` 是缩进级别（根 = 0）。
目录可展开/折叠；文件是叶节点。`icon` 非空时覆盖
默认文件夹/文件字形。`path` 可选地携带该节点表示的
真实文件系统路径（纯内存树为空）。
`checked` 支撑可选的选择复选框列。

- string label;
  - 节点标题。

- int depth;
  - 缩进级别（根 = 0）。

- bool isDir;
  - 是否目录（可展开/折叠；叶节点为 false）。

- bool expanded;
  - 目录的展开状态。

- string icon;
  - 自定义图标名（"" = 默认文件夹/文件字形）。

- string path;
  - 该节点表示的真实文件系统路径（纯内存树为空）。

- string payload;
  - 调用方可选的 payload（例如索引、id），
    节点本身不解释它。

- bool checked;
  - 复选框状态（WithChecks 列的数据来源）。

- static TreeNode Dir(string label, int depth, bool expanded)
  - 构造目录节点（`expanded` 决定初始展开状态）。

- static TreeNode File(string label, int depth)
  - 构造叶节点（默认图标）。

- static TreeNode FileIcon(string label, int depth, string icon)
  - 构造叶节点并指定图标名（覆盖默认文件字形）。

- TreeNode WithPayload(string p)
  - 附带调用方自定义标识（索引、id 等），节点不解释它。


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
  - 焦点/选中行信号（-1 = 无；可经 BindSel 换成共享信号）。

- SignalInt scroll;
  - 垂直滚动偏移。

- Input filter;
  - 顶部内联过滤行（WithFilter 挂入；过滤由宿主重绑列表完成）。

- bool checks;
  - 是否显示复选框列（WithChecks）。

- bool multi;
  - 多选模式：Ctrl 点击切换行的选中状态，
    Shift 点击从焦点行扩展到该行。`marks` 保存
    `sel`（焦点行）之外的额外选中行。

- List<int> marks;
  - 多选集合（sel 焦点行之外的行号）。

- NodeOf tpl;
  - 自定义行模板（null = 内置行）。

- bool rowsStale;
  - 自定义行需要重建。

- int builtFor;
  - 上次构建行时的节点数。

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
  - 公共初始化（各构造共用）。

- TreeView()
  - 默认构造：内置行（箭头/图标/标签）。

- TreeView(NodeOf template)
  - 自定义行：模板为每个节点构建控件子树。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（checks/empty/class）。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"tree"）。

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
  - 节点数（含隐藏在折叠目录里的）。

- void ScrollToEnd()
  - 滚到底部；下一帧绘制时会被钳到实际最大偏移。

- List<TreeNode> Nodes()
  - 绑定列表本身，供自行维护节点视图的宿主
    在处理器被触发时使用。

- bool Has(int row)
  - `row` 是否在节点列表范围内。

- TreeNode NodeAt(int row)
  - `row` 处的节点；仅在 Has(row) 为真时有效。

- int SelectedIndex()
  - 选中行号（-1 = 无选中）。

- bool HasSelection()
  - 是否有一行处于选中状态。

- TreeNode Selected()
  - 选中的节点；仅在 HasSelection() 时有效。

- void SelectIndex(int row)
  - 编程式选中一行，变化时触发 Select。

- void ClearSelection()
  - 清空选中与全部多选标记。

- bool IsMarked(int row)
  - `row` 属于选择集（含焦点行）时返回 true。

- List<int> SelectedIndices()
  - 所有选中行按升序排列（含焦点行）。

- int SelectionCount()
  - 多选中的行数。

- int ContextRow()
  - 处理器回调里读取当前涉及的节点行号；
    仅在 Context/Check/Press 处理器运行期间有效。

- int ContextX()
  - 事件发生时的屏幕坐标（配套 ContextRow）。

- int ContextY()
  - 事件发生时的屏幕 Y 坐标（配套 ContextRow）。

- TreeView OnSelect(Action a)
  - 某行被选中时回调。

- TreeView OnActivate(Action a)
  - 选中行再次被点击时回调。

- TreeView OnExpand(Action a)
  - 目录被展开时回调。

- TreeView OnCollapse(Action a)
  - 目录被折叠时回调。

- TreeView OnContext(Action a)
  - 行被右键点击时回调（读取 ContextRow/ContextX/ContextY）。

- TreeView OnCheck(Action a)
  - 复选框被切换时回调（读取 ContextRow）。

- TreeView OnPress(Action a)
  - 行被左键按下时回调（读取 ContextRow/ContextX/ContextY）。

- TreeView OnMove(Action a)
  - 拖动排序完成后回调（读取 MoveFrom/MoveTo）。

- TreeView EmptyText(string text)
  - 树为空时显示的文本。

- void ExpandAll()
  - 展开所有目录。

- void CollapseAll()
  - 折叠所有目录。

- void SetAllChecked(bool on)
  - 勾选/取消全部节点（含目录）。

- void Toggle(int row)
  - 切换 `row` 行的三态复选框状态（目录联动子节点）。

- List<int> CheckedIndices()
  - 所有已勾选节点的索引（含目录）。

- List<string> CheckedLabels()
  - 所有已勾选叶节点的标签。

- int CheckedCount()
  - 已勾选叶节点数。

- bool Templated()
  - 是否为模板行模式。

- int RowHeight(App app)
  - 行高：样式 height 优先，否则主题 small。

- int FilterHeight(App app)
  - 过滤行高度（未挂过滤行时为 0）。

- bool Shown(int row)
  - 没有折叠的祖先隐藏 `row` 时返回 true。

- int ContentHeight(App app)
  - 可见行的像素高度；面板未声明高度时，列布局
    据此确定树的高度。

- int RowExtent(App app, Control row)
  - 模板行实际占高（prefH 未测出时退统一行高）。

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
  - 行区绘制与交互：折叠隐藏、选中/悬停底色、复选框与拖动排序。

- override void OnMeasure(App app)
  - 覆写：模板行按需重建后，内容高度加过滤框高度得出偏好高度。

- override void OnPaint(App app)
  - 覆写：按需重建模板行，画过滤框与空态提示，再画可见行
    （含拖放指示线）。

- void PaintEmpty(App app, Rect rows)
  - 空树时绘制 Empty 提示文本。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Select"/"Activate"/"Expand"/
    "Collapse"/"Context"/"Check"。

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
  - 文本内容（可绑定）。

- void InitTypography(string txt)
  - 初始化：以文本注册为控件并绑定文本。

- Typography()
  - 空文本（设计器用）。

- Typography(string txt)
  - 给定初值文本。

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
  - 按 "label" 规则解析并缓存的样式盒（Class 提供 h1..h6/paragraph 级别）。

- override void OnMeasure(App app)
  - 覆写：按样式字号量出行高与文本宽度。

- override void OnPaint(App app)
  - 覆写：按 `label` 规则的样式绘制文本。

- static int Heading(App app, int x, int y, string text, int level)
  - 立即模式标题；返回其占用的行高。

- static int Paragraph(App app, int x, int y, string text)
  - 立即模式段落；返回其占用的行高。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（text/class）。


## Upload (class)

文件上传（Naive UI n-upload 对位）：选择/拖拽入列，
内置 multipart HTTP POST 或 CustomRequest 自定义传输，
列表/缩略图/照片墙三形态渲染，进度/状态/移除/下载。

Upload up = new Upload();
up.Action = "http://127.0.0.1:8080/upload";
up.Change += OnChanged;
up.Render(app, x, y, w);

非受控：组件自建 Files；受控：宿主把自己的
List<UploadFile> 赋给 Files（或 SetFiles）共享引用。
事件（Change/Upload/Finish/Error/Remove/Download/Exceed）
处理器读 LastItem 拿当前项。内置选择弹窗按 FilePicker
范式路由：OwnsEventWindow → ApplyEventWindow → RenderDialog。

- List<UploadFile> Files;
  - 文件列表。null 时组件自建（非受控）；宿主赋入
    自己的 List 即受控共享引用（Naive UI file-list 对位）。

- bool AutoUpload;
  - 入列即自动开始上传（Naive UI list-type 的默认行为；false 时
    停留在 Pending，等 Submit()）。默认 true。

- bool Multiple;
  - 允许一次选择/拖入多个文件。默认 false（拖入仍逐项过门）。

- int Max;
  - 列表上限；0 = 不限。超出时触发 Exceed 并拒绝入列。

- bool Dragger;
  - 触发区形态：大虚线拖放框（Naive UI :default-slot 的
    dragger 对位）；false 时为普通按钮。

- string DragText;
  - Dragger 主文案。

- string Tip;
  - Dragger 下方的灰色副文案（"" = 不画）。

- string TriggerText;
  - 触发按钮文字（Naive UI :trigger 默认槽位）。

- bool ShowTrigger;
  - 显示触发入口（按钮 / 照片墙“+”块）。默认 true。

- bool ShowRemoveButton;
  - 行尾/遮罩显示移除按钮。默认 true。

- bool ShowDownloadButton;
  - Success 且有 url 时显示下载按钮、名称变主色链接
    （点击触发 Download）。默认 false。

- bool PictureCard;
  - 照片墙形态（Naive UI list-type="image-card"）：方格 + 添加块 +
    hover 遮罩。

- bool Thumbnail;
  - 文本列表行内缩略图（图片项显示 30 方图）。默认 false。

- string Accept;
  - 接受扩展名，逗号分隔（"png,jpg"；可带点/星号）；"" = 不限。

- string Action;
  - 内置 multipart POST 的目标 URL（"http(s)://host[:port]/path"）；
    "" = 无内置传输（CustomRequest 或手动 SetItemStatus 驱动）。

- ExternalCallPolicy CallPolicy;
  - 内置 HTTP 上传使用的统一外部调用策略。默认拒绝明文 HTTP 和
    loopback；需要本地 HTTP 时必须显式使用
    `ExternalCallPolicy.Default().AllowLocalHttp()` 配置。

- string FieldName;
  - multipart 表单字段名。默认 "file"。

- UploadBeforeFn BeforeUpload;
  - before-upload 拦截钩子（Naive UI on-before-upload）。方法组
    只能在实参位转换，用 WithBeforeUpload 挂接。

- UploadRequestFn CustomRequest;
  - 自定义传输钩子（Naive UI custom-request）；设置后优先于 Action。
    用 WithCustomRequest 挂接。

- UiEvent Change;
  - 列表任何变化时触发（入列/移除/状态写入）。

- UiEvent UploadStart;
  - 一项开始传输时触发（Naive UI on-before-upload 之后的提交沿）。

- UiEvent Finish;
  - 一项传输成功时触发。

- UiEvent Error;
  - 一项失败时触发（message 已写入项）。

- UiEvent Remove;
  - 移除一项时触发。

- UiEvent Download;
  - 点击下载按钮/名称链接时触发（宿主自行发起下载）。

- UiEvent Exceed;
  - 超出 Max 时触发（LastItem = 被拒项）。

- UploadFile LastItem;
  - 当前事件对应的文件项（宿主在事件处理器里读它）。

- int baseId;

- static int uidCounter;
  - 全局流水号：跨实例唯一（宿主可按 id 反查项，多组件并存不撞号）。

- App uiApp;
  - 最近一次渲染所属的 App（线程封送与重绘用）。

- FilePicker picker;

- List<UploadJob> jobs;

- int dropX;
  - 本帧拖放落区（Render 时刷新；WantsDropAt 据此判定）。

- int dropY;

- int dropW;

- int dropH;

- void InitUpload()
  - 初始化：非受控空列表、默认文案与三形态开关、七个事件槽与
    id 预留段。

- Upload()
  - 构造：非受控模式，自建空文件列表。

- Upload WithBeforeUpload(UploadBeforeFn f)
  - 挂 before-upload 拦截钩子（Naive UI on-before-upload）。

- Upload WithCallPolicy(ExternalCallPolicy policy)
  - 配置内置 HTTP 上传的统一外部调用策略；null 恢复安全默认值。

- Upload WithCustomRequest(UploadRequestFn f)
  - 挂自定义传输钩子（Naive UI custom-request），传 null 解除。
    CustomRequest 接管传输并绕过内置 HttpClient，故不受 CallPolicy 约束；
    宿主若在钩子中发起网络请求，必须自行绑定并执行 ExternalCallPolicy。

- Upload WithAction(string url, string fieldName)
  - 挂内置 HTTP 目标并（可选）指定字段名。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- int BaseId()
  - 本组件的命中 id 基址（Render 返回值）。行命中区按
    `BaseId() + 8 + 行号*4` 排布：行本体 +1 名称链接 +2 移除 +3 下载。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（action/fieldName/accept/max/
    autoUpload/multiple/triggerText/showTrigger/showRemoveButton/
    showDownloadButton/dragger/dragText/tip/pictureCard/thumbnail）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Change"/"Upload"/"Finish"/"Error"/
    "Remove"/"Download"/"Exceed"。

- override void BindEvent(string evt, Action a)
  - 覆写：语义事件各挂对应 UiEvent（"Upload" 与 "UploadStart"
    同槽），其余按名称走通用路由。

- void SetFiles(List<UploadFile> files)
  - 受控列表：共享宿主 List 引用（非受控时忽略 null）。

- int AddPaths(List<string> paths)
  - 接受一批本地路径（选择/拖拽统一入口）：逐项过
    Accept → Max → BeforeUpload 门。返回实际入列数。

- bool TryAddPath(string path)
  - 单个路径造项并过门入列（AddPaths 的内层）。

- UploadFile AddFile(string name, string path, string url)
  - 程序化造项（默认文件列表/演示用）：不过 Accept/Max/BeforeUpload
    门（宿主显式添加）；url 非空 → Success，AutoUpload 且可启动 →
    立即开传。返回新建项。

- bool Admit(UploadFile f)
  - 入列唯一通路：id 分配 + Accept/Max/BeforeUpload 门 + 自动开传。

- bool AcceptAllows(string ext)
  - 扩展名是否命中 Accept 列表（逗号/分号分隔，容忍 "*." 与 "." 前缀；
    "*" 通配；空扩展名恒通过）。

- static bool IsImageExt(string ext)
  - 扩展名是否为常见图片格式（决定缩略图/图标形态）。

- void Submit()
  - 把全部 Pending 项启动传输（AutoUpload=false 的手动提交，
    Naive UI submit() 对位）。

- void StartItem(UploadFile f)
  - 启动一项：CustomRequest 优先；否则 Action 非空且有界面循环
    时起后台线程。两者皆无则不改动状态（不撒谎，留在 Pending）。

- void DropJob(UploadJob job)
  - 从活动作业表摘除已完结的作业。

- void FailItem(UploadFile f, string message)
  - 标记一项失败：写状态与原因，触发 Error + Change。

- void ApplyProgress(UploadJob job)
  - 后台线程 → UI 线程封送的目标：进度写入（不发事件，只重绘）。

- void ApplyDone(UploadJob job)
  - 后台线程 → UI 线程封送的目标：完成沿（2xx → Success + Finish，
    其余 → Error）。

- UploadFile Find(int id)
  - 按项 id 查找文件项；未找到返回 null。

- void SetItemPercent(int id, int percent)
  - 刷新一项进度（CustomRequest/演示用）。

- void SetItemStatus(int id, int status, string message)
  - 写入一项状态：Success → 触发 Finish，Error → 触发 Error，
    任一状态变化都触发 Change。

- void SetItemUrl(int id, string url)
  - 写入一项远端 url（成功回显/下载目标）。

- void SetItemThumb(int id, string thumb)
  - 写入一项缩略图来源（Naive UI on-create-thumbnail 的宿主替代）。

- bool RemoveItem(int id)
  - 按 id 移除一项（触发 Remove + Change）。返回 false = 未找到。

- void Clear()
  - 清空列表（触发 Change）。

- bool WantsDropAt(int x, int y)
  - 指针 (x, y) 是否落在本组件的拖放落区（Dragger 大框 /
    照片墙区域 / 普通模式触发按钮）。宿主可直接用它判定
    OS 文件拖放的落点（本帧渲染后有效）。

- int AcceptDropped(List<string> paths)
  - 接收一批拖入的本地文件（等价 AddPaths，供宿主事件回路调用）。

- void EnsurePicker()
  - 懒建内建选择弹窗并接 Accepted 回调。

- void OpenDialog()
  - 打开内建选择弹窗：Multiple 或 Max>1 时走多选模式（Ctrl/Shift
    点选，确认后逐项过门入列）；Accept 生成过滤器。弹窗皮肤跟随
    宿主当前主题；initial 留空让 FilePicker 续用上一次浏览的目录。
    弹窗事件/渲染由 Form.Run 的标准循环按 ChildWindows 注册表自动
    泵送；自写循环的宿主（gallery 范式）依旧可以用
    OwnsEventWindow/RenderDialog 自己泵——两条路互不干扰。

- void CloseDialog()
  - 程序化关闭内建选择弹窗（取消语义，对位 FilePicker.Close）。
    测试与宿主清场用；用户关闭走弹窗自身的取消/关闭按钮。

- FilePicker Picker()
  - 内建弹窗本体（未开且从未打开过为 null）。测试与宿主检查
    lastDir/上限透传等 picker 侧语义用；宿主正常流程无需触碰。

- void OnPicked()
  - 弹窗确认回调：按单/多选模式取选中路径，逐项过门入列。

- List<FileFilter> BuildFilters()
  - Accept 字符串 → 选择对话框过滤器（始终追加“所有文件”兜底）。

- bool DialogOpen()
  - 选择弹窗是否打开中。

- bool DialogNeedsRedraw()
  - 内建弹窗要求重绘（对位 FilePicker.NeedsRedraw：宿主用它决定
    主循环节奏——弹窗开着时不能阻塞在 WaitEvent 上）。

- bool OwnsEventWindow(nint evh)
  - 宿主事件路由：事件窗口属于 Upload 内建的弹窗时返回 true——
    命中后该帧的 OS 事件交给 ApplyEventWindow，其余交给弹窗自己的
    渲染通道 RenderDialog（FilePicker 文档范式）。

- nint DialogWindowHandle()
  - 内建弹窗的原生窗口句柄（未开为 0）。宿主焦点管理与路由测试用。

- bool ApplyEventWindow(nint evh)
  - 处理投递到内建弹窗的 OS 事件。按属主门控：只有 evh 确实是本
    组件弹窗的窗口时才接手并返回 true；非属主（从未开过、弹窗已
    关的实例一律在内）返回 false。FilePicker.ApplyEvent 的布尔是
    「回路是否继续」语义，对非属主窗口返回 true——宿主若按
    「谁拥有谁处理」折叠扫描（gallery 的 UploadApplyEvent 范式），
    不门控就会在第一个开过弹窗的实例处吞掉后面所有弹窗的事件。

- void RenderDialog()
  - 渲染内建弹窗（OwnsEventWindow 为 true 的帧里，替代主窗口的
    一帧渲染）。

- int TakeDialogResult()
  - 收一次弹窗结果（对位 FilePicker.TakeResult）：关闭瞬间触发
    Accepted → OnPicked，选中路径已并入文件列表；0 = 本轮未关，
    2 = 用户取消。

- void SweepDialogClosed()
  - 弹窗刚关闭的收尾（对位 FilePicker.SweepClosed）：销毁程序化
    Close 留下的死窗口、清空 picker 内部引用。宿主在
    TakeDialogResult() != 0 的那一轮调用一次。

- int StyleGap(App app)
  - 触发区与列表行之间的间距（皮肤 gap 可覆写）。

- int RowH(App app)
  - 文本列表行高：带缩略图 44，否则 34（按 DPI 缩放）。

- StyleBox TriggerStyle(App app, bool off)
  - 触发按钮样式（normal / hover / disabled 三态）。

- int TriggerH(App app)
  - 触发按钮高度（皮肤 height 可覆写）。

- int TriggerW(App app)
  - 触发按钮宽度 = 文字宽 + 左右 padding（TriggerText 空 = 默认文案）。

- int DraggerH(App app)
  - Dragger 大框高度：图标圆 + 主文案（+ 可选副文案）装配。

- static string Ellipsize(string s, int maxW, int fs)
  - 超宽文本逐字收缩截断并补「…」；不超宽原样返回。

- void DashedRect(Canvas c, int x, int y, int w, int h, int color, int dash, int gapLen, int thickness, int radius)
  - 圆角矩形四边虚线框（拖放区/添加块），按 radius 收边。

- void BlitCover(Canvas c, string src, int dx, int dy, int size)
  - 图片缩略图 cover 裁切：源图居中裁成方形，铺进目标正方形。

- int Render(App app, int x, int y, int w)
  - 立即模式入口：绘制整个 Upload（触发区 + 列表/照片墙），
    接线全部命中区，返回本组件的命中基址。

- void PaintTrigger(App app, int x, int y, int tw, int th, bool off)
  - 普通触发按钮：一张表面 + 文字 + 命中区（点击开选择弹窗）。

- void PaintDragger(App app, int x, int y, int w, int h, bool off)
  - 大虚线拖放框：图标圆 + 主文案 + 副文案 + 命中区。

- int PaintRows(App app, int x, int y, int w, bool off)
  - 文本列表：每行缩略图/图标、文件名（成功且有 url 时为链接）、
    状态徽标、错误附言、进度条与移除/下载按钮（最多画 128 行）。
    返回列表底部 y。

- int PaintTiles(App app, int x, int y, int w, bool off)
  - 照片墙：96 方格流式排布 + hover 遮罩（移除/下载）+ 状态徽标 +
    底部进度条 +「+ 添加」块。返回内容底部 y，并刷新拖放落区。

- override void OnMeasure(App app)
  - 覆写：按形态量首选尺寸——照片墙按瓦片折行，拖拽区/触发器
    各自计高，再加文件列表行高。

- override void OnPaint(App app)
  - 覆写：在自身矩形内绘制并处理交互（转发到 Render）。


## UploadFile (class)

Upload 列表中的一个文件项（Naive UI file-list 元素）。

- int id;
  - 组件内唯一 id（入列时由 Upload 赋值）。

- string name;
  - 文件名（显示用，含扩展名）。

- string url;
  - 远端地址：Success 且非空时名称渲染为可点击链接。

- string thumb;
  - 缩略图来源（本地路径或 http/https）；空且项为图片时回退 path。

- string path;
  - 本地文件路径（拖拽/选择/内置传输的来源；纯演示项为空）。

- string message;
  - 状态附言：Error 时为失败原因。

- string response;
  - 传输响应体（内置 HTTP 成功时填充；CustomRequest 可自填）。

- int size;
  - 字节大小（已知时 > 0）。

- int status;
  - UploadStatus.Pending/Uploading/Success/Error。

- int percent;
  - 进度百分比（0..100，Uploading 中刷新）。

- string ext;
  - 小写扩展名（不含点）。

- Upload owner;
  - 归属的 Upload 宿主（组件入列路径自动回填；宿主自造的受控项为 null）。
    让静态方法组形式的 CustomRequest 能回推 SetItemPercent/SetItemStatus。

- UploadFile()
  - 构造空白项：Pending 状态、无扩展名、全字段空。

- static string ExtOf(string fileName)
  - 取小写扩展名（不含点）；无点返回 ""。


## UploadJob (class)

一项内置 HTTP 上传的工作对象：绑定到具体项（实例方法组，
规避闭包），在后台线程跑 HttpClient.UploadFileBytesAsync，
进度/完成经 App.Post 封送回 UI 线程。

线程入口必须是静态方法组：实例方法组降级为带 `this` 的闭包
记录，而 `zan_thread_start` 只接受裸函数指针（ImageHttp、
DataTable.Export 同受此约），故这里走「静态队列 + 单 worker」
范式：Enqueue 在空闲时拉起 UploadJob.Run，Run 取一项 await
一项，队列排空即退，下次入队再拉起。

- static nint lockHandle;
  - 任务队列互斥锁句柄（懒建）。

- static List<UploadJob> pending;
  - 待处理作业队列（锁保护）。

- static bool workerStarted;
  - 后台 worker 是否存活（队列排空即退出并复位）。

- Upload host;
  - 作业归属的宿主组件（进度/完成封送目标）。

- UploadFile item;
  - 传输的文件项与它的 id（封送后按 id 反查，列表可能已变）。

- int itemId;

- ExternalCallPolicy callPolicy;
  - 作业创建时从 Upload 快照的统一外部调用策略。

- bool tls;
  - 目标连接参数（Init 从 Action URL 解析；无端口时按 scheme 补 80/443）。

- string hostName;

- int port;

- string urlPart;

- string field;
  - multipart 表单字段名。

- int lastPct;
  - 上次封送的进度百分比（去重用）；-1 = 尚未报过。

- int doneCode;
  - 完成结果快照（ReportDone 写入，UI 线程 FlushDone 消费）：
    HTTP 状态码、响应体、错误信息，donePosted 防重复封送。

- string doneBody;

- string doneErr;

- bool donePosted;

- static bool Enqueue(UploadJob job)
  - UI 线程：入队并（worker 空闲时）拉起后台线程。
    线程起不来时返回 false（任务已从队列摘除），调用方负责失败该项。

- static void Run()
  - 后台线程入口（静态方法组）：逐条取任务直到队列清空，
    然后退出（下一次入队会重新拉起）。await 让出期间
    IO 反应器推进传输——与 ImageHttp.WorkerLoop 同构。

- static UploadJob Take()
  - 取队首任务；null = 队列已空。

- void Init(Upload h, UploadFile f, string url, string fieldName, ExternalCallPolicy policy)
  - 解析 Action URL（scheme/host[:port]/path）并绑定任务。
    policy 在此处快照，之后修改 Upload 不影响已排队的作业。

- async int Execute()
  - 后台线程上跑一项的传输（由静态 Run 循环调用）。

- void Probe(int sent, int total)
  - HttpClient 发送探针（后台线程）：折算百分比后封送回 UI。

- void ReportDone(int code, string body, string err)
  - 记录完成结果并（仅首次）封送 FlushDone 回 UI 线程。

- void FlushProgress()
  - UI 线程侧转发：进度 / 完成结果交给宿主 Upload 入账。

- void FlushDone()
  - UI 线程侧转发：完成结果交给宿主 Upload 入账（仅首次）。


## UploadStatus (class)

文件上传状态取值（对齐 Naive UI status:
pending / uploading / finished / error）。

- static int Pending()
  - 等待上传：已入列但尚未开始（Submit() 或宿主启动）。

- static int Uploading()
  - 上传中：percent 为进度（0..100）。

- static int Success()
  - 成功：url 可指向远端地址（配合下载/回显）。

- static int Error()
  - 失败：message 携带原因。


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

行也可以是自定义组件（模板模式，与 ListView 的行模板同构，
但只在行进入视口时才构建）：

list.SetRows(model.quotes.Count, i => QuoteRow(model.quotes[i]));
...                                         // 数据变更后
list.Refresh();                             // 下一帧重建可见行

行内子控件（按钮、开关）照常收自己的点击：每行按行号独占
一段 WidgetId，滚动或重建都不改号，上一帧登记的命中区因此
始终对得上。行等高（虚拟化的前提）；行内控件的易失状态
（如开关的本地翻转）放在调用方的数据里，重建后由数据恢复。

与 ListView（每行全高布局、无视口）不同，
本组件成本恒定且自行滚动。

- ScrollView scroll;

- List<string> items;

- int selected;

- int rowH;

- int activated;

- RowAt rowAt;
  - 模板模式的行构建器；null = 字符串行模式。

- int rowCount;

- bool rowsStale;
  - 模板行待重建标记；可见区间变化时也会重建。

- int builtFirst;

- int builtLast;

- int rowSeqBase;
  - 模板行独占的 WidgetId 段起点（-1 = 还没建过行）。

- int tipIdx;
  - 字符串行悬停提示的跟踪状态：正在计时的行号（-1 无）
    与指针进入该行的时刻。指针换行或离开即重置。

- int tipSinceMs;

- VirtualList()
  - 空列表（字符串行模式，之后 SetItems/SetRows）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（rowHeight）。

- override void OnMeasure(App app)
  - 覆写：默认 240x160 的逻辑尺寸（实际占宿主给的矩形）。

- override void OnPaint(App app)
  - 覆写：在自身矩形内做虚拟化绘制（见 Render）。

- static int RowIdStride()
  - 每行独占的 WidgetId 数：够放一行十来个控件还有余。
    超出会把段基后移、下一帧整体重建——宁可丢一次点击，
    也不能让两行共用同一段 id。

- void SetItems(List<string> rows)
  - 整体替换字符串行数据（模板模式之外的最简用法）；
    行数变化后选中行号自动夹回范围内。

- void Add(string row)
  - 追加一行字符串数据。

- void SetRows(int count, RowAt build)
  - 模板模式：共 `count` 行，第 `i` 行由 `build(i)` 构建控件
    子树，数据由调用方闭包持有。构建器像 ListView 的行模板
    一样创建一次即可；行 id 只取决于行号，因此即使每帧传入
    新的 lambda（内容相同），行内控件也照常收得到点击。
    行数变化或内容变更（Refresh()）后，可见行在下一帧重建。

- void Refresh()
  - 标记模板行数据已变更，可见行将在下一帧重建。
    任意线程调用都安全。

- void SetRowHeight(int h)
  - 固定行高（逻辑 px）；0 = 用主题的 heightLarge。

- int Count()
  - 当前行数：模板模式返回 SetRows 给的 count，
    字符串模式返回 items 数。

- int Selected()
  - 当前选中行号（无选中为 -1）。

- string SelectedText()
  - 选中行的文本；模板模式无文本概念，恒返回 ""。

- int TakeActivated()
  - 本帧点击的行（或 -1），随后清除。

- int ResolveRowH(App app)
  - 实际使用的行高：显式 SetRowHeight 优先，否则主题值。

- void BuildVisible(App app, int first, int last)
  - 为可见区间 [first, last) 重建模板行。行控件的 WidgetId
    按「段基 + 行号 × 步长」发号：滚动改变可见集时行内控件
    的 id 不变，上一帧登记的命中区依旧对得上号；区间与数据
    都没变时不动 children，行内控件跨帧保留。

- void Render(App app, int x, int y, int w, int h)
  - 渲染视口、可见行与滚动条，并处理滚轮/拖动/悬停/选择；
    作为控件树子节点时经 OnPaint 每帧调用。

- void HitSelect(App app, Rect area, int rh, int dy)
  - 字符串行按坐标选中：由点击 y 与滚动量算出行号并记为选中/激活。


## Watermark (class)

水印层：在自身矩形内平铺半透明文字（或图像），盖在页面内容上标注
状态——机密、草稿、环境名、账号/租户名。参照页面把它作为最后一个
子节点停靠填满（子节点按顺序绘制，最后添加者画在最上面）：

panel.Add(new Watermark { Content = "内部资料" }.DockFill());

文本 `Content` 支持 `\n` 分行，整块作为一组绕每块自己的盒子中心
刚性倾斜（`Rotate` 度，正 = 顺时针，CSS 约定；默认 -22）。相邻块
的间距由 `GapX`/`GapY` 控制（逻辑 px），`OffsetX`/`OffsetY` 平移
整层——把 offset 取在半个间距内可以让拼缝错开内容的关键部位。

`LineHeight` 给出显式行盒高（逻辑 px，0 = 字体行高）；`TileW`/
`TileH` 给出显式贴片盒（0 = 按内容测量），设置后块内行按
`TextAlign`（0 左 / 1 中 / 2 右）在盒内水平对齐。`Weight` >= 600
时同行重描一次做模拟加粗——文本管线没有字重字体，与
StyleBox.DrawRun 同一做法。

`Color` 为 0xAARRGGBB，alpha 参与混合（0 视为不透明，文本渲染的
约定）；0 表示交给 CSS `color`，再退内置的淡黑。`FontPx` 为 0 时
取 CSS `font-size`，再退 14。

平铺默认被自身矩形裁剪；`NoClip` 为 true 时越过边界继续铺——
宿主想留边距又想让水印铺到边时，把水印框缩进去、开着 NoClip
即可（能铺多远取决于各层祖先的裁剪，一般到宿主面板边缘）。

`Src` 指向图像（文件路径或 mem: 内存 key）时改为平铺图像：像素
自带的 alpha 参与混合，倾斜应预先烘进图里——位图旋转不在图像
管线上，`Rotate` 只作用于文字。

控件不接线任何事件，因此不注册命中区域，指针事件穿透到下层内容；
设计器仍可通过 HitTest 选中它。裸控件测量为 0×0，用
DockFill()/Grow() 或 CSS width/height 给出覆盖范围。

- Binding<string> Content;
  - 水印文本（`\n` 分行）。

- string Src;
  - 图像水印源（文件路径 / mem: key）；非空时替代文本。

- int Color;
  - 文字颜色 0xAARRGGBB；0 = CSS `color`，再退内置默认。

- int FontPx;
  - 字号（逻辑 px）；0 = CSS `font-size`，再退 14。

- int Rotate;
  - 倾斜角（度，正 = 顺时针）；仅作用于文字。

- int GapX;
  - 块间距（逻辑 px）。

- int GapY;
  - 块纵向间距（逻辑 px，默认 100）。

- int OffsetX;
  - 整层平移（逻辑 px）。

- int OffsetY;

- int LineHeight;
  - 显式行盒高（逻辑 px）；0 = 字体行高。

- int TileW;
  - 显式贴片盒（逻辑 px）；0 = 按内容测量。TileW > 0 时行按
    `TextAlign` 在盒内水平对齐。

- int TileH;
  - 显式贴片高（逻辑 px）；0 = 行数×行高，给出的值只用于增高。

- int TextAlign;
  - 贴片内文字水平对齐：0 左 / 1 中 / 2 右。

- int Weight;
  - >= 600 时同行重描一次（模拟加粗，见 StyleBox.DrawRun）。

- bool NoClip;
  - true 时不被自身矩形裁剪，平铺越过边界继续（默认 false）。

- void InitWatermark()
  - 初始化：置默认水印参数（-22° 倾斜、100px 间距、其余清零）。

- Watermark()
  - 空内容水印（设计器用）。

- Watermark(string content)
  - 给定文本的水印。

- int RotClamped()
  - 倾斜角钳制到 [-90, 90] 后的生效值。

- int EffectiveFont(App app, StyleBox s)
  - 生效字号：显式 `FontPx` 优先于 CSS `font-size`，再退 14（逻辑 px
    经 DPI 缩放）。

- int EffectiveColor(StyleBox s)
  - 生效文字颜色：显式 `Color` 优先于 CSS `color`，再退
    rgba(0,0,0,0.145)——与 antd 水印同档的"看得见但不打扰"。

- override void OnPaint(App app)
  - 覆写：按 `Src` 有无铺图片或文字贴片（`NoClip` 时越过自身
    边界继续平铺）。

- void PaintTextTiles(App app, Canvas c, StyleBox s)
  - 以文字内容为贴片在控件内平铺（含倾斜、间距、偏移与
    显式贴片尺寸；四周多铺一圈防缺角）。

- void PaintTextTile(Canvas c, List<string> lines, int tx, int ty, int tileW, int lineH, int fs, int color, int rot, int bold, double cs, double sn)
  - 画一块文字水印。倾斜时每行的锚点先绕块中心旋转，运行时再把
    整行绕锚点转过去——两步合成 = 整块绕中心的刚性旋转（每行绕
    自己左上角转同一角度并非刚性，多行会扇形张开）。`TileW` 给出
    的盒内余量按 `TextAlign` 分给每行的锚点，倾斜后仍是整块对齐。

- void PaintImageTiles(App app, Canvas c, string src)
  - 以图片为贴片在控件内平铺（DPI 缩放后按间距重复）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（content/src/color/fontpx/rotate/
    间距偏移/贴片尺寸/textalign/weight/noclip/class）。


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

- static string lang="zh";
  - UI language code for the wizard's built-in labels ("zh" / "en").
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
  - null 信号换成一个共享的 0 初值信号，其余原样返回。

- static int ScrollOffset(App app, int x, int y, int w, int h, int contentH, SignalInt off)
  - 行列表视口的滚动量：处理滚轮、把偏移夹在
    [0, contentH - h]，返回本帧应使用的偏移。

- static string TW(string en, string zh)
  - 内置文案走键式查找（System.Globalization.Lang）：英文原文
    作键、内置中文为缺省串；语言包未加载时回退缺省串。

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

- SignalInt iDevSel;
  - 目标设备画像选择（DeviceProfile.All() 的下标）与横竖屏
    方向（0 竖屏 / 1 横屏）。null 隐藏目标设备行（保持旧的
    纯尺寸预设行为）。选中锁定类设备时尺寸行收起，画布
    尺寸由画像给出并回填 iSizeW/iSizeH——宿主仍从这两个
    输入读取最终尺寸，契约不变。

- SignalInt iDevRot;

- Wizard()
  - 设计器用无参构造；状态由静态工厂（Categorized）或实例构造器填充。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（title/showLocation）。

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
  - 与 SizePresetW 平行的预设高度表（下标一一对应）。

- static string SizePresetName(int i)
  - 第 i 个尺寸预设的显示名（部分随 UI 语言）。

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

- static Wizard Categorized(string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames, SignalInt sizeSel, Input sizeW, Input sizeH, SignalInt devSel, SignalInt devRot)
  - 再带目标设备画像行的分类项目向导：devSel 选中
    DeviceProfile.All() 的下标（锁定类设备收起尺寸预设，
    画布由画像给出），devRot 是横竖屏方向。传 null 则退化为
    上面的纯尺寸预设行为。

- int Show(App app)
  - 本帧将向导绘制到其窗口中，并返回动作码
    （0 无、1 创建、2 取消/关闭、3 浏览）。所有状态都保存在
    实例上；宿主对返回码作出响应。

- static int Render(App app, string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc, SignalInt sizeSel)
  - Render 的尺寸预设行变体（无自定义宽/高输入）。

- static int Render(App app, string title, List<WizardItem> items, SignalInt sel, Input nameInput, Input locInput, bool showLoc, SignalInt sizeSel, Input sizeW, Input sizeH)
  - 完整版单列表向导：可附带尺寸预设行（sizeSel 与可选的宽/
    高自定义输入）。返回 0 无操作、1 创建、2 取消/关闭。

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
  - 完整版两级向导：分类/模板列表加尺寸预设行。返回值同上。

- static int RenderCategorized(App app, string title, List<string> catNames, SignalInt catSel, List<WizardTemplate> tpls, SignalInt tplSel, Input nameInput, Input locInput, List<WizardTarget> targets, List<string> groupNames, SignalInt sizeSel, Input sizeW, Input sizeH, SignalInt devSel, SignalInt devRot)
  - 完整版两级向导（带目标设备画像行）。devSel/devRot 为
    null 时目标设备行隐藏，等同上一重载。


## WizardItem (class)

Wizard 列表中一个可选择的条目：图标、标签和一行
描述（当前选中时显示）。取代旧的
name/icon/description 并行列表约定，改为单一实体。

- string name;

- string icon;

- string desc;

- WizardItem(string n, string ic, string d)
  - 构建条目：名字、图标、描述。


## WizardTarget (class)

多选目标标签：标签及其开/关状态（1 = 已选）。

- string name;

- int selected;

- WizardTarget(string n, int s)
  - 构建目标标签。


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

- string deviceId;
  - 模板声明的目标设备画像 id（manifest `device=`，见
    DeviceProfile）。空串 = 自由尺寸（桌面），向导的目标
    设备行预选桌面并放开分辨率预设与自定义宽高。

- WizardTemplate(string c, string n, string ic, string d)
  - 构造：分类/名称/图标/描述必填，形态与预览信息经
    Shape/Look 链式补齐。

- WizardTemplate Shape(bool canSize, int w, int h, bool isRound)
  - 链式配置形态信息，让宿主只描述它从清单里读到的内容。

- WizardTemplate Look(string previewPath, string sketchSpec)
  - 预览来源：真实截图路径（可为空）与线框记号。

- WizardTemplate Device(string id)
  - 声明目标设备画像 id（manifest `device=`）。


## Control (delegate)

为某个绑定实体构建一行的控件子树，适用于
行是自定义组件而非文本列的列表。

`delegate Control RowOf<T>(T item);`


## Control (delegate)

构建节点自身的控件子树，用于行是自定义
组件而非内置字形 + 标签行的树。

`delegate Control NodeOf(TreeNode node);`


## Control (delegate)

为虚拟列表构建第 `index` 行的控件子树；数据由调用方的
闭包持有，行只在进入视口时才会被构建。

`delegate Control RowAt(int index);`


## bool (delegate)

is-date-disabled 回调（Naive UI 的 is-date-disabled prop）：
返回 true 表示该日禁用——灰显、不可点选。

`delegate bool DateFilter(DateTime d);`


## bool (delegate)

自定义验证（Naive UI `validator`）：对将要写入的新值投票，
false 时值仍会写入，但整框标 error（红描边）并触发 Invalid。

`delegate bool NumValidator(int value);`


## bool (delegate)

回答某个实体所在的行是否可选（可用）。返回 false 的行
视为禁用项：点击、Ctrl+点击、Shift+范围、右键、长按
全部无反应，悬停/选中带不出现，SelectedIndex 不落在其上。

`delegate bool RowGate<T>(T item);`


## bool (delegate)

before-upload 钩子：项入列前调用，返回 false 拦截入列
（Naive UI on-before-upload）。必须是非捕获方法组。

`delegate bool UploadBeforeFn(UploadFile item);`


## string (delegate)

校验规则委托：传入字段当前值文本，返回 "" 表示通过，否则返回要
显示的错误消息。内置规则见 FormRule 的静态方法；业务自定义规则
直接构造：`new FormRule(v => v == "expected" ? "" : "不一致")`。

`delegate string FormCheck(string value);`


## string (delegate)

输入过滤回调（Naive UI allow-input）：收到「插入后的完整候选
文本」，返回清洗后的文本；返回 null 表示拒绝这次插入。
用于「只允许数字」「不许前后空格」这类输入约束。

`delegate string InputFilterFn(string candidate);`


## string (delegate)

展示串的格式化钩子（Naive UI `format`）：在分组/小数之后对
数值文本再加工，如 s => s + " ¥"。

`delegate string NumFormatter(string text);`


## string (delegate)

展示串的解析钩子（Naive UI `parse`）：提交前把展示串还原成
原始数值串，如 s => Text.Replace(s, " ¥", "")。

`delegate string NumParser(string text);`


## string (delegate)

从绑定实体中读取某一列的文本：`new ListColumn<File>("Name",`
f => f.name)`.

`delegate string CellOf<T>(T item);`


## string (delegate)

`delegate string MenuLabelOf(MenuItem item);`


## string (delegate)

`delegate string MenuIconOf(MenuItem item);`


## string (delegate)

`delegate string MenuExpandIconOf(MenuItem item, bool expanded);`


## string (delegate)

行首图标委托：返回 Gui.Icon 图标名，"" 表示本行不画图标。

`delegate string IconOf<T>(T item);`


## void (delegate)

工具条项被点击时的回调，带项的索引。

`delegate void ToolItemCallback(int index);`


## void (delegate)

custom-request 钩子：接管一项的传输（Naive UI custom-request）。
组件已把项置为 Uploading，宿主自行推进 SetItemPercent /
SetItemStatus。必须是非捕获方法组。

`delegate void UploadRequestFn(UploadFile item);`
