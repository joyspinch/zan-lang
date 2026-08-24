# Gui.Designer

> 源码: `stdlib/Gui/Designer/Designer.Form.zan`, `stdlib/Gui/Designer/Designer.Inspector.zan`, `stdlib/Gui/Designer/Designer.zan`


## Designer (class)

设计器部分：表单画布——行/容器渲染、拖放
放置和实时控件预览。

- int RibbonH(App app)
  - 设计窗口顶部的功能区：不分标签页，命名分组一次
    全部铺开（排版 / 层次 / 网格 / 缩放 / 视图），命令
    一律是小图标 + 一行标签、每组两行堆叠，这样一屏
    放得下又不占画布高度。命令的登记顺序必须与 acts
    一一对应：Ribbon 返回的是被点命令的全局序号。

- void RenderRibbon(App app, Canvas c, Theme t, int x, int y, int w)

- void RunRibbon(App app, string act)
  - 执行功能区命令。动作码与 RenderRibbon 里登记的
    acts 一一对应。

- void ZoomStep(int dir)
  - 放大/缩小一级（10%），钳制在 25%–200%。从「适应」
    起步时以上一帧实际算出的适应比例为基准，缩放是连续的。

- List<int> ZoomPresets()
  - 缩放档位（千分比，1000 = 100%），0 = 适应视口，与
    缩放下拉菜单一一对应。

- List<string> ZoomMenuLabels()
  - 缩放下拉菜单的显示标签（"适应"、"25%" .. "200%"）。

- void AlignSelGroup(int mode)
  - 把选中组件对齐到它所在的框（顶层为设计窗口，容器子元素
    为容器的内容框）：0-2 左/水平居中/右，3-5 上/垂直居中/下。
    对整个多选做对齐：只有一个选中项时就是它自己
    对齐到所属框；多选时所有成员对齐到同一条边
    （主选中项所在框的边）。

- void AlignSel(int mode)

- void RenderForm(App app, Canvas c, Theme t, int cx, int cy, int cw, int ch)
  - 表单（自动吸附）和自由窗口共用同一块设计画布：抓手平移、
    缩放、框选、多选完全一致，区别只有组件坐标从哪来——流式
    布局每帧自动排版算出来，自由布局由拖放决定。

- int FlowTop()
  - 设计窗口内容区的上边（留出表单标题）。

- int LayoutFlow(int dw)
  - 重算整个流式布局，返回内容底边（设计空间）。

- int LayoutFlowList(List<FormField> lst, int x0, int y0, int w0, int tabFilter)
  - 把一列字段按 24 列打包成行并写入设计空间矩形。
    tabFilter >= 0 时只排布该标签页下的子级。返回底边。

- int KidInsetX(FormField f)
  - 容器内容框相对容器矩形的偏移（设计空间）。自由画布上
    运行时给容器 Pad(0)、子级按自己的停靠边铺满整块，所以只有
    Tabs 需要让出标签条；再多让 6/26 就是设计器比运行时矮一
    截、子级整体右下偏移的来源。流式布局仍是 6/26（与
    LayoutFlowKids 一致）。

- int KidInsetY(FormField f)

- int FlowFieldH(FormField f)
  - 一个字段在流式布局里的高度（宽度须已确定）。容器
    顺带排布自己的子级。

- int LayoutFlowKids(FormField f)
  - 排布容器的子级（坐标相对容器内容框，与 RenderFreeField
    的 +6 / +26 偏移一致），返回内容框高度。

- void FlowDropTarget(App app, Canvas c, Theme t, int ox, int oy, int psMilli, int dw)
  - 流式模式的放置目标：指针下的容器（放入），否则顶层
    插入位置。顺带画出容器高亮 / 插入线。

- FormField TopLevelOf(FormField f)
  - 某个字段所属的顶层字段（自身或最外层容器）。

- void DropField()
  - 把待处理的拖放落到表单上：面板拖拽在 dropIndex 插入新字段；
    表单内拖拽把被拖的行移动到那里。

- void ApplyDesignSize(int newW, int newH)
  - 把设计窗口切换到新尺寸。一份设计只有一块画板：
    改宽高就是改这块画板，组件矩形原样保留，不再按
    "WxH" 存多套断点布局（那会让同一个设计出现好几个
    版本，而且改回尺寸时画布和设置对不上）。

- void ApplyDockLayout(List<FormField> lst, int ax, int ay, int aw, int ah)
  - 停靠布局：设了停靠边的组件在所在层里自动吸边——按顺序
    从剩余矩形上切走一条（厚度沿用组件自己的宽/高），Fill
    铺满剩下的全部；停靠为「自由」(0) 的组件不参与，保持
    绝对定位。每帧算一遍并写入运行时矩形，画布和命中测试
    看到解算结果，而保存的 JSON 仍只看作者矩形。

- void ApplyDockNested(List<FormField> lst)
  - 对每个容器的内容框再停靠一层（与 RenderFreeField 的
    +6 / +26 内边距一致）。标签页容器的每一页各自为一层：
    否则看不见的页里的停靠组件会吃掉当前页的空间，
    当前页的 Fill 就只剩 0。

- void ApplyDockLayer(List<FormField> lst, int ax, int ay, int aw, int ah, int tabFilter)
  - 把一层停靠组件排进 (ax, ay, aw, ah)。`tabFilter >= 0` 时
    只排该标签页下的子级。

- void RenderFreeField(App app, Canvas c, Theme t, FormField f, int ox, int oy, int psMilli, int px, int py)
  - 自由画布元素的设计空间 -> 屏幕空间转换（子节点已在 px/py 中
    累积了父内容框的偏移）。

- static int TabPageCount(FormField f)
  - Tabs 容器的页数：没写选项时仍算一页，容器里总有地方放组件。

- static int TabActivePage(FormField f)
  - Tabs 容器当前活动页的序号，钳制到有效范围。

- static string TabPageTitle(FormField f, int i)
  - 第 i 页的页签标题（没写选项时是 "Tab i"）。

- void RenderTabStrip(App app, Canvas c, Theme t, FormField f, int ex, int ey, int ew, int psMilli, int active)
  - 画 Tabs 容器的标签条（跟随画布缩放），并把每个页签的屏幕
    矩形登记到 tabHits，供画布的指针处理切页。

- TabHeaderHit PickTabHeader(int mx, int my)
  - 指针位置上的页签（后画的在上层），没有则 null。

- FormField FreePick(List<FormField> lst, int mx, int my, int px, int py)
  - 设计空间 (mx, my) 处最上层的字段，从后往前搜索 `lst`，
    后面的兄弟节点优先；子节点优先于其容器。父节点的
    内容框原点存入 freeSelPX/freeSelPY，供拖拽使用。

- FormField FreePickIn(List<FormField> lst, int mx, int my, int px, int py, int tabFilter)
  - `tabFilter >= 0` 时只看该标签页下的子级——看不见的页里的
    组件也点不中。

- static int FreeHandleX(int hn, int ex, int ew)

- static int FreeHandleY(int hn, int ey, int eh)

- int FreeSnapTol(App app, int psMilli)
  - 磁吸阈值：屏幕上约 6px 换算回设计像素，于是任何显示/视图缩放下手感一致
    （放大后要贴得更近才吸，缩小后更远就吸），而不是写死设计像素。

- static List<int> FreeSnapLines(bool horiz, List<FormField> sibs, FormField self, int span)
  - 一根轴上的候选对齐线：画布的起点/中点/终点，加上除自己外每个可见字段的
    起点/中点/终点。X 轴传 horiz=true，`span` 是该轴上画布的尺寸。静态是为了
    无窗口回归能直接喂一组字段进来。

- static List<int> SnapAxis(int start, int size, List<int> lines, int tol)
  - 一根轴上的磁吸：把移动元素的起点/中点/终点三个锚点吸到最近的候选线，
    取三者里最近的那一次命中，所以「贴边、居中、和另一个控件对齐」都能触发。
    
    返回两个值：[0] = 校正后的起点（没命中就是原值），[1] = 命中的参考线坐标
    （-1 = 没命中，调用方据此决定画不画线）。纯几何、无 App，方便无窗口回归。

- int FreeSnapX(App app, int newLeft, int w, int dw, FormField self, int psMilli)
  - 横向磁吸：命中则写 freeGuideX 参考线，返回校正后的左边。

- int FreeSnapY(App app, int newTop, int h, int dh, FormField self, int psMilli)
  - FreeSnapX 的纵向对应，写 freeGuideY。

- void RenderDesignCanvas(App app, Canvas c, Theme t, int cx, int cy, int cw, int ch)
  - 设计画布：信箱式窗口 + 网格 + 字段 + 场景设计器那套交互
    模型（选择 / 拖拽 / 框选 / 抓手平移 / 缩放 / 面板放置）。
    表单（自动吸附）和自由窗口共用这一份实现。

- FormField FreeDropContainer(int dx, int dy)
  - 自由画布上 (dx, dy) 落入的容器，其内容框原点写入
    freeDropPX/freeDropPY；null 表示顶层表单。指到已有组件上
    时取它所在的容器，于是拖到标签页内任何地方都是
    「加到正看着的这一页」。

- void PlaceFree(FormField f, FormField cont, int lx, int ly)
  - 把新组件以容器内容框坐标 (lx, ly) 为中心放进 cont
    （null = 顶层）；标签页容器落在正看着的那一页。

- void DropFreePalette(int dx, int dy)
  - 在设计空间 (dx, dy) 处把面板字段添加到自由画布，
    新元素以光标为中心；光标下有容器则成为它的子级。

- int DefaultFreeW(FormField f)

- int DefaultFreeH(FormField f)

- void DrawLabel(App app, Canvas c, Theme t, FormField f, int lx, int ly, int lw, int align)

- int PvS(App app, int v)

- int PvFont(int fsz)

- void PreviewControl(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail)
  - 绘制字段输入控件的非交互预览。

- void PreviewDisplay(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail, int line)
  - 为显示/布局/反馈/
    导航组件（ftype >= 15）绘制非交互预览。每个分支绘制对应 stdlib 控件的
    可辨识的缩略图。

- void PvCtrlAuto(App app, Control ctl, int x, int y, int w, int h)
  - 在设计画布上按给定矩形绘制一个保留式控件的实时预览。
    用 BeginCtrlZoom/EndCtrlZoom 夹住渲染：真控件内部只认 app.dpiScale
    和主题度量，本身不知道画布视图缩放，因此临时把两者按视图缩放放大/
    缩小，控件的字号、内边距就随画布一起等比缩放（放大不再「有的变有的
    不变」）。给定矩形 x/y/w/h 已是缩放后的屏幕坐标，渲染完精确还原。
    与 PvCtrl 相同，但高度取控件自己测量出来的（不小于 `h`）：
    换行的容器在设计器里也能完整显示。

- void PvCtrl(App app, Control ctl, int x, int y, int w, int h)

- void BeginCtrlZoom(App app)
  - 进入真控件等比缩放：按视图缩放（winZoom，50%–200%）临时放大/缩小
    app.dpiScale 和主题度量。100% 时为恒等变换，真控件保持 1:1 清晰。

- void EndCtrlZoom(App app)
  - 退出真控件等比缩放：从基线精确还原主题度量与 app.dpiScale。

- bool PvBasic(App app, FormField f, int x, int y, int w, int avail, int line)
  - 基础输入控件（输入/多行/密码/数字/下拉/开关/评分/滑块）用
    FormBuilder 构造的真控件渲染预览，取代仿画，使设计与运行完全
    一致。这些类型不把标题嵌进控件（标题由 DrawLabel 单独绘制），
    故不会重复。返回 true 表示已处理。

- static string ChartKindName(int k)
  - 图表类型的人类可读名称（Chart 字段把类型存在 uiState 中）。

- void PreviewChart(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail)
  - Chart 组件的预览。图表类型保存在字段的
    uiState 中（0 柱状图 .. 7 散点图），因此一个面板条目即可组合出
    stdlib Chart 控件提供的所有图表类型。

- void PreviewTable(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail)
  - 渲染表格字段的紧凑非交互预览：由字段选项（其列）构建的
    表头行加上几行占位行。

- void PreviewChoice(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int line)
  - 水平渲染单选/复选字段选项的预览。

- void Box(App app, Canvas c, Theme t, int x, int y, int w, int h)


## Designer (class)

设计器部分：属性检查器（逐字段编辑器、双向
同步）和设计 JSON 序列化。

- void RenderInspector(App app, Canvas c, Theme t, int x, int y, int w, int h)

- void SyncEditors(FormField f)
  - 选区变化时重置选项 / 事件编辑器（普通属性行直接
    绑定字段，不需要手动同步）。

- static string SuggestHandler(FormField f, string ev)
  - 为事件建议委托方法名，例如字段 "amount" +
    "Change" -> "OnAmountChange"。类型名中的空格会被去掉，
    显示组件（如 "Data Table"）仍能产生合法标识符。

- List<PropSpec> FieldSpecs(FormField f)
  - 选中组件的属性列表：只声明要编辑哪些字段，
    行的标签 / 编辑器 / 滚动全由 PropertyGrid 完成。

- int RenderFieldProps(App app, Canvas c, Theme t, FormField f, int ix, int iy, int iw)
  - 选中组件的属性选项卡（不含事件——它们在事件选项卡）。
    属性行交给 PropertyGrid，这里只处理绑定选择器、选项表
    这些语义动作。返回最后一行之后的 y，供主体滚动。

- void RemoveOptionFixup(FormField f, int oi)
  - 从 `f` 中移除选项 `oi` 并修复依赖状态，
    UI 永远不会索引超出缩小后的列表（旧的直接 RemoveOptionAt 可能
    让 Tabs 容器的活动标签/子标签超出范围）。

- static string ColTitle(string opt)
  - 表格列选项的显示标题（"标题:字段"约定）。

- static string ColField(string opt)
  - 表格列选项的数据字段键（未声明时为 ""）。

- static int IndexOfColon(string s)

- List<PropSpec> WindowSpecs()
  - 根 Window 的属性列表：窗口装饰（标题 / 尺寸 / 标志）以及
    应用于其内带标签输入行的共享布局默认值。

- int RenderWindowProps(App app, Canvas c, Theme t, int ix, int iy, int iw)
  - 根 Window 的属性选项卡：属性行交给 PropertyGrid，
    这里只留「保存为组件」和自定义形状的只读提示。

- int RenderEventsTab(App app, Canvas c, Theme t, int ix, int iy, int iw)
  - 事件选项卡：当前目标（窗口根或选中组件）的所有事件，
    按类别分组（Pointer / Keyboard / Focus / Drag /
    Lifecycle / Value），可滚动，带行内处理器编辑器。

- static List<string> WindowEventNames()
  - 根 Window 的可设计事件（生命周期/尺寸/焦点/键盘）。

- string WinGetHandler(string ev)

- void WinSetHandler(string ev, string h)

- static string EventCategory(string ev)
  - 事件名的逻辑类别，事件选项卡据此把长列表
    分组到可读标题之下而不是溢出。

- static string GroupLabel(string grp)
  - 事件类别标题的显示标签。类别键保持
    英文（按字符串相等匹配）；只有事件选项卡中
    显示的标题文本被翻译。

- static List<string> EventGroups()
  - 事件选项卡的类别显示顺序。

- static bool Contains(string hay, string needle)
  - 面板过滤器使用的区分大小写子串测试（它匹配
    小写类型键，因此小写查询即可）。

- void RenderStatus(App app, Canvas c, Theme t, int x, int y, int w)

- string SaveJson()
  - 把整个表单序列化为人类可编辑的 JSON 文档：表单
    配置加有序的 "fields" 数组。这是交换格式——
    手工编辑 JSON 再加载回去即可重新配置表单，
    设计器完全由数据驱动。与 LoadJson 往返兼容。
    { name, labelPos, formSize, labelWidth, hideStar, showSubmit,
    showReset, submit, fields:[ { type,label,name,placeholder,
    required,change,options:[...] } ] }

- JsonValue ShapeJson()
  - winShape 的 JSON：Rect/Rounded/Ellipse 为普通整数，
    自定义并集形状为区域数组（"t,x,y,w,h,r;..." -> [{"t":..},..]）。

- string ShapeSpecFromJson(JsonValue arr)
  - ShapeJson 的逆操作：winShape 区域数组 -> 运行时消费的紧凑
    "t,x,y,w,h,r;..." 规格。

- JsonValue FieldJson(FormField f)
  - 从 FormField 构建一个字段对象，递归进入容器
    `kids`，使 JSON 往返包含所有设计器事件绑定、选项和
    子节点。读取器（FieldFromJson）与此键集镜像对应。

- void LoadJson(string s)
  - 根据 SaveJson 生成（或
    手写/工具生成的）JSON 设计文档重建表单。用真正的 JSON 解析器读取，因此任意
    合法格式——美化打印、紧凑、或键乱序——加载结果都一致，
    而不依赖每行一个键的布局。

- List<FormField> ParseComponentFields(string s)
  - 仅解析设计 JSON 的 "fields" 数组到一个新列表，用于
    在不触碰表单的情况下实例化已保存的用户组件。接受
    完整设计文档或纯 fields 数组。

- void BuildFieldsFromJson(JsonValue arr, List<FormField> dst)
  - 将 JSON 数组中的每个对象追加为 FormField（递归处理
    容器 "kids"），保持顺序。

- static bool RetargetDoc(JsonValue doc, int devW, int devH)
  - 把设计文档改到 devW x devH 画布，并把绝对定位的控件夹回
    新画布内。就地编辑 JSON：.zform 每个字段携带的状态（kind、
    placeholder、绑定、事件、options、容器 kids）远多于这里要动的
    几何，任何「反序列化到模型再序列化回去」的写法都会静默丢掉
    其余键——控件会全部退化成默认 Input。
    返回 true 表示文档被改动。

- static void FitFieldsToCanvas(JsonValue fields, int boxW, int boxH, int margin)
  - 把每个字段的绝对矩形夹进 boxW x boxH，边距 margin。容器的 kids
    用的是父级坐标系，所以递归时换成父级矩形、不再留窗口边距。

- FormField FieldFromJson(JsonValue o)
  - 从 JSON 对象构建一个 FormField。先应用 "type"，这样
    字段的事件集（Events()）和容器属性反映真实类型，
    然后才读取 handlers、options 和子级。

- static JsonValue UnmodeledKeys(JsonValue o)
  - 收集字段对象里设计器没有建模的键(FieldFromJson 读的之外
    的一切,含 DataGrid 的 "of"/"columns"/"rowFactory" 和 "props"
    直通表),供 FieldJson 原样写回。没有时返回 null。

- static bool IsModeledKey(string k)
  - FieldJson / FieldFromJson 已经读写的键集(事件处理器是
    "on" 前缀的动态键)。与两者的键集镜像对应:新建模一个键
    时这里同步加上,否则会重复写出。

- int CountFields(List<FormField> lst)
  - 字段总数（含嵌套容器子级）；加载后为唯一名计数器
    提供初始值。


## Designer (class)

- static int lang;

- List<FormField> fields;

- FormField sel;

- FormField selPrev;

- string doc;

- string msg;

- int inspTab;

- bool preview;

- bool hostPanels;

- int seq;

- FormField activeContainer;

- TreeView palTv;

- List<TreeNode> palNodes;

- List<int> palKind;

- List<string> palCustom;

- bool palClickPending;

- int palClickKind;

- string palClickCustom;

- bool palBuilt;

- string palBuiltQ;

- int palBuiltUser;

- List<string> palFolded;

- int layoutMode;

- int winZoom;

- int lastFitPct;

- bool freeDragging;

- bool freeResizing;

- int freeResizeH;

- int freeDragDx;

- int freeDragDy;

- int freeGuideX;

- int freeGuideY;

- int freeSelPX;

- int freeSelPY;

- int freeDropPX;

- int freeDropPY;

- bool showGrid;

- bool snapGrid;

- int gridSize;

- int freePanX;

- int freePanY;

- bool freePanning;

- List<FormField> freeExtra;
  - 自由画布上的附加选中项（Ctrl+点击 / 框选），
    `sel` 是其中的主选中项（手柄和检查器跟着它）。

- bool freeHand;

- bool freeMarquee;

- int freeMqSX;

- int freeMqSY;

- int freePanSX;

- int freePanSY;

- int freePanOX;

- int freePanOY;

- bool winRound;

- int pvScale;

- List<TabHeaderHit> tabHits;
  - 本帧画出的页签矩形（渲染时填，指针处理时读）。

- List<int> czBase;

- int czDpi;

- int czScale;

- int czDepth;

- bool zoomMenuOpen;

- int zoomMenuX;

- int zoomMenuY;

- bool zoomMenuArm;

- int labelPos;

- int formSize;

- int labelWidth;

- bool hideStar;

- bool showSubmit;

- bool showReset;

- bool dragging;

- bool dragFromPalette;

- bool dragMoved;

- int dragKind;

- string dragCustomKind;

- int dragIndex;

- int dropIndex;

- int dropY;

- int dragMouseX;

- int dragMouseY;

- FormField dropContainer;

- int dragUserComp;

- int hoverFt;

- int hoverX;

- int hoverY;

- PropertyGrid propGrid;

- int inspKeySeq;

- SignalString optSig;

- Input optInput;

- SignalString optFieldSig;

- Input optFieldInput;

- SignalString evtSig;

- Input evtInput;

- string evtEditing;

- SignalString formNameSig;

- Input formNameInput;

- SignalString submitSig;

- Input submitInput;

- SignalString winTitleSig;

- Input winTitleInput;

- int winW;

- int winH;

- bool winResizable;

- bool winTool;

- bool winCenter;

- bool winGlass;

- int winShape;

- int winShapeRadius;

- string winShapeSpec;

- bool winChrome;

- int winOpacity;

- string docRole;
  - 设计文档的 "role"："control" 的文档编译成可嵌入的自定义
    组件（partial class X : Control）而不是窗口。设计器自己
    不用它，但必须原样往返：丢了这一键，一次另存就把组件
    变成窗口，代码后置里的字段访问全部编译不过。

- int winPosX;

- int winPosY;

- List<EventBinding> winEvents;

- string winEvtEditing;

- SignalString filterSig;

- Input filterInput;

- int inspScroll;

- int inspContentH;

- List<string> bindVars;

- bool bindPickOpen;

- SignalString jsonSig;

- TextArea jsonArea;

- bool jsonOpen;

- bool evtRootPrev;

- FormField evtSelPrev;

- List<UserComponent> userComps;

- bool saveCompRequested;

- void SetLayoutMode(int mode)
  - 切换被设计窗口的布局模型。宿主在打开一份设计时
    调用（.zform 里存了 layoutMode），设计器自己不提供
    切换命令：两种布局不共享坐标，来回切会丢摆放。

- void SeedFreeBounds()
  - 给还没有绝对坐标的组件在自由画布上排一列，
    免得它们全叠在原点。

- static string T(string en, string zh)
  - 按当前 UI 语言选取字符串。

- Designer()

- void ScrollPaletteToEnd()

- void Seed(App app)
  - 可选的示例内容，让新建的设计器不是空表单——
    包含一个 Card 和带嵌套组件的 Tabs 容器，让组合 /
    标签切换行为开箱即可见。

- bool FieldAlive(List<FormField> lst, FormField f)
  - `f` 是否还挂在设计树上。删除组件后 activeContainer /
    dropContainer 这类引用可能指向已经被删掉的容器，往里加组件
    会得到一个谁也看不见、也存不下去的字段。

- void DropDeadRefs()
  - 丢弃指向已删组件的容器引用。

- void AddField(int ftype)
  - 添加新组件。有活动容器时组件会嵌套进去（Tabs 则进入活动
    标签页）；否则追加到顶层表单。添加容器后会把它设为活动
    容器，可以立刻往里面放置组件。

- void SetBindVars(List<string> vars)
  - 宿主 IDE 提供从表单配对的 code-behind 文件
    扫描出的可绑定变量名（见检查器的绑定路径选择器）。

- void SetUserComponents(List<UserComponent> comps)
  - 宿主 IDE 提供项目保存的用户组件（每个组件一个实体，
    来自 components/*.zcomp）。

- bool ConsumeSaveComponent()
  - 用户请求把当前设计保存为可复用组件后，恰好返回一次 true；
    宿主 IDE 持久化它并重新加载列表。

- void AddUserComponent(int k)
  - 在表单上实例化一个已保存的用户组件：单容器
    设计直接原样插入，其余情况用名为组件名的
    Card 包裹（WinForms UserControl 风格）。

- void AddCustomField(string kind)
  - 按 ProjectComponents tag 添加发现的项目自定义组件
    到表单/活动容器，与内置组件的 AddField 对应。

- bool InSubtree(FormField owner, FormField inner)
  - 当 `inner` 位于 `owner` 子树的任意位置时返回 true。

- bool ContainsRef(List<FormField> lst, FormField target)

- int IndexIn(List<FormField> lst, FormField f)

- FormField ParentContainerOf(FormField target)
  - 把 `target` 作为子级的容器；若 target 是
    顶层字段（或未找到）则返回 null。递归查找。

- FormField FindOwner(List<FormField> lst, FormField target)

- List<FormField> ListRemove(List<FormField> lst, FormField target)
  - 重建不含 `target` 的 `lst`（列表中间位置的 RemoveAt 在其他地方使用，
    但重建与下方的 Insert 规避方案保持一致）。

- List<FormField> ListMove(List<FormField> lst, int from, int to)
  - 把 `from` 处的元素移动到 `to`，通过 Add 重建（
    运行时在列表中间 Insert 会溢出缓冲区并破坏堆）。

- void MoveSel(int delta)

- void InsertFieldAt(int at, FormField nf)
  - 插入 `nf`，使其最终位于顶层列表的 `at` 索引处。

- bool IsExtraSel(FormField f)
  - 组件是否在多选里（主选中项或附加项）。

- void ToggleExtraSel(FormField f)

- void ClearExtraSel()

- void SizeSelGroup(int mode)
  - 把多选里其余成员的宽/高设成主选中项的（0=等宽，1=等高），
    和场景设计器的「尺寸」组同义。

- void SelToEdge(int top)
  - 把选中项移到同级的最前/最后（1=置顶，0=置底）。

- void DuplicateSel()
  - 复制选中项，副本偏移一点并成为新的选中项。

- void ToggleLockSel()
  - 锁定/解锁整个多选：以主选中项的状态取反，其余成员跟随，
    这样一组组件的锁定状态始终一致。

- void DeleteSelGroup()
  - 删除整个多选：先删附加项，再删主选中项。
    无论从功能区按钮还是 Delete 键触发，都走这里，
    保证多选里的每一个都被删掉（而不是只删主选中项）。

- void DeleteSel()

- void Render(App app, int x, int y, int w, int h)

- void RenderHostPanels(App app, Rect toolRect, Rect propRect)
  - 宿主把组件面板和属性面板画进自己的 dock 面板里。调用
    它就等于告诉设计器「左右两栏归我管」，设计器自己那份
    不再排版。id 基值和 Render 分开，避免两趟互相错位。

- void RenderPinned(App app, int x, int y, int w, int h)

- void RenderJsonPanel(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - 开启 JSON 时替代画布显示的可编辑 JSON 抽屉。
    文本框就是文档：Apply 从中重建设计，
    Regenerate 把当前设计重新导出到其中。

- void RenderPalette(App app, Canvas c, Theme t, int px, int py, int pw, int ph)
  - 组件工具箱：一棵 TreeView（分组是目录，组件是叶子），
    行的绘制、滚动、过滤、折叠全部由控件自己完成，这里
    只负责喂数据和处理「选中 = 添加 / 按下 = 开始拖拽」。

- void EnsurePalette()

- void BuildPalette(string q)
  - 重建工具箱节点。过滤串非空时是一层扁平的匹配结果，
    否则是按类别分组的两层树。

- void PalGroup(string title, int lo, int hi)

- void PalDir(string title)

- void PalLeaf(string label, string icon, int kind, string custom, int depth)

- int PalLeafRow()
  - 事件涉及的行是组件叶子时返回其下标，否则返回 -1。

- int UserCompIndex(string name)

- void PalActivate()
  - 单击工具箱里的组件即添加一个；随后清掉选中，
    这样连点同一行可以连续添加。实际添加发生在抬起时
    （见 PalClickRelease），因为按下这一刻还分不清点击和拖拽。

- void PalClickRelease()
  - 在工具箱上按下又原地抬起：这是一次点击，添加组件。

- void PalPress()
  - 在组件行上按下即开始一次拖拽，拖到画布上放开即添加。

- void PalFoldNote(bool folded)
  - 记住分组的折叠状态，重建节点后仍然保留。

- bool MatchesFilter(int ft, string q)
  - 当字段类型的 key 或显示名包含过滤关键字时为 true。

- bool PalFolded(string title)
  - 已折叠的分组标题。折叠状态按标题记住，跨帧保留。

- void PalToggleFold(string title)

- static List<string> ListRemoveStr(List<string> src, int idx)

- bool InRect(int mx, int my, int x, int y, int w, int h)

- void RenderTray(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - WinForms 风格组件托盘：表单上的每个非可视组件
    在此显示为一个小块。点击选中它，其属性和
    事件即可在检查器中编辑；选中的小块会高亮。

- int ClampSpan(int sp)
  - 把字段的列跨度限制到合法的 1..24 网格范围。

- string FieldDesc(int ft)
  - 面板悬停 tooltip 中显示的一行描述。

- string FieldIcon(int ft)
  - 把字段类型映射为面板用的 Segoe MDL2 图标名。

- int SmallBtn(App app, string text, int type, int x, int y, int w)
  - 渲染一个小按钮（高 28）并返回其 id。检查器各处使用，
    让控件能放进紧凑的行内。

- int SmallIconBtn(App app, string iconName, int type, int x, int y, int w)
  - 小号（高 28）纯图标按钮——尺寸与 SmallBtn 相同，但
    标签是矢量 Icon 字形而非文本字符。


## TabHeaderHit (class)

画布上一个页签的屏幕矩形。渲染标签条时逐个登记，指针按下
时据此把点击判成「翻到那一页」，而不是选中或拖动 Tabs 容器。

- FormField tabs;

- int page;

- int x;

- int y;

- int w;

- int h;

- TabHeaderHit(FormField tabs, int page, int x, int y, int w, int h)
