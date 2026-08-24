# Gui.Component

> 源码: `stdlib/Gui/Component/ChatView.zan`, `stdlib/Gui/Component/ConsoleView.zan`, `stdlib/Gui/Component/Dock.zan`, `stdlib/Gui/Component/FilePicker.zan`, `stdlib/Gui/Component/FileTree.zan`, `stdlib/Gui/Component/GraphView.zan`, `stdlib/Gui/Component/LogView.zan`, `stdlib/Gui/Component/PivotTable.zan`, `stdlib/Gui/Component/PropertyGrid.zan`, `stdlib/Gui/Component/SessionList.zan`


## ChatMessage (class)

一条聊天消息：记录里显示的角色（user / assistant / tool / error）、
正文，以及这一轮的思考过程（没有则 ""）。

多智能体（团队）会话再带一层信封：说话的成员 id、寻址给谁、协议
节拍、所属任务与进度。单助手会话里这些字段全为空，因此**同一种
记录格式**既能服务单助手也能服务团队，存档也能互相打开。

- string role;

- string text;

- string reason;

- string agent;

- string to;

- string kind;

- string taskId;

- int progress;

- string call;
  - 这一拍实际发出的工具调用（宿主自己的格式，原样留档）。它不进聊天区
    的正文——那是给人看的；宿主把请求发回模型时带上它，模型才看得到自己
    上一拍真正的调用长什么样。

- long at;
  - 这条消息产生的时刻（Unix 秒，UTC）。0 = 不知道（旧存档里没有这个
    字段），头行就不显示时刻。流式那条气泡在建出来时定下这个值，后面
    每一拍只改正文，因此显示的是「这一轮开始说话的时间」。

- static ChatMessage Create(string role, string text)

- static ChatMessage CreateWithReason(string role, string text, string reason)

- static ChatMessage CreateFrom(string role, string text, string agent, string to, string kind, string taskId, int progress)
  - 一条带信封的团队消息。


## ChatSpeaker (class)

一位发言者的外观：显示名、色调与 Tag 类名。多智能体程序用它把
成员 id 翻成头行上看到的样子；单助手程序用不到。

- string name;

- int tint;

- string tagCls;

- static ChatSpeaker Of(string name, int tint, string tagCls)


## ChatStrings (class)

聊天视图要显示的所有文案。全部由宿主给出，组件本身不依赖任何
翻译层，于是同一个组件可以出现在任意语言的程序里。

- string you;

- string assistant;

- string tool;

- string error;

- string thinkShow;

- string thinkHide;

- string codeMorePre;

- string codeMorePost;

- string codeCollapse;

- string busy;

- string empty;

- static ChatStrings Default()
  - 英文默认文案；宿主按需覆盖字段即可。


## ChatView (class)

可复用的**聊天视图控件**：消息流 + markdown-lite 排版 + 思考过程与
工具调用折叠 + 长代码块折叠 + 选区复制 + 底端锚定滚动 + busy 提示。

ChatView chat = new ChatView();
chat.WithStrings(myStrings).Bind(msgs);
chat.SetBusy(waitingForModel);

宿主只提供数据与文案：换行缓存（只重排正在流式输出的那一轮）、
滚轮、选择、折叠状态、Ctrl+C 复制全在控件内部。绘制交给
`Gui.Widget.StyledText`，因此宿主一行画布代码也不需要写。

声明式界面里可以直接写 `{"kind": "ChatView", "name": "Chat"}`，
code-behind 只调用 `Bind` / `SetBusy`。

- List<ChatMessage> data;
  - 调用方的消息列表（实时视图，不拷贝）。

- ChatStrings str;

- ChatSpeakerOf speakerOf;

- ChatKindLabel kindLabelOf;

- List<TextRun> cache;
  - 正在排的那份行表，以及增量重排用的水位线：`cacheTurns` 之前的
    轮次已定型，`cachePrefix` 是它们占的行数。

- int cacheTurns;

- int cachePrefix;

- string cacheKey;

- string cacheSig;

- List<TextRun> show;
  - 画出来的那份行表：整份重排要跨好几帧，把半成品直接画出去，看到的
    就是「底端锚定的最后一行」一帧一帧往下长——改一次窗口大小，记录就
    自己从头滚到底一遍。所以排好的那一份才换上来。

- bool showStale;
  - 这次重排是整份重排（行号会全部重映射，换上来时要丢掉选区）。

- List<int> toolOpen;
  - 展开的消息下标（工具结果与长代码块共用一份）。

- SignalInt thinkOpen;

- SignalInt scrollSig;

- LogState sel;

- bool busy;

- StyledText grid;
  - 记录网格与空态提示：常驻子控件，绘制全在它们内部。

- Label emptyLbl;

- ChatView()

- override string Kind()

- override string StyleType()

- ChatView Bind(List<ChatMessage> msgs)
  - 绑定调用方的消息列表（实时视图，不拷贝）。

- ChatView WithStrings(ChatStrings s)
  - 全部可见文案（宿主已本地化）。

- ChatView WithSpeakers(ChatSpeakerOf who, ChatKindLabel kindLabel)
  - 多智能体会话的外观解析钩子（单助手程序不必调用）。

- static string ClockText(long at)
  - 一条消息头行右侧的本机时区时刻（HH:MM）；没有记时间的旧消息返回 ""。

- void SetBusy(bool on)
  - 模型正在回答：记录末尾显示 busy 行。

- LogState Selection()
  - 选区状态（宿主可持久化或复位）。

- SignalInt Scroll()
  - 底端锚定的滚动偏移信号（0 = 贴住最新一行）。

- void Reset()
  - 换掉一整份记录（切换会话）：缓存、折叠、选区与滚动全部归零。

- void Invalidate()
  - 下一帧整份重排（文案、字号或钩子变了）。

- string SelectedText()
  - 当前选中的文本（未选中则 ""）。

- override void OnPaint(App app)

- void TakeWheel(App app)
  - 指针在记录上时滚轮滚动。

- void TakeCopy(App app)
  - 指针在记录上时 Ctrl+C 复制选中的文本。

- void Build(App app)
  - 换行缓存：只有记录、宽度、字号或折叠状态变了才重建，而且只有
    正在流式输出的最后一轮会重排——1000 轮的会话和空会话一样便宜。

- static int CodeFoldLines()
  - 折叠长代码块前保留的行数。

- static int BuildBudgetMs()
  - 整份重排时每帧的时间预算（毫秒）：排完这么久就把控制权交
    回循环，下一帧接着排。一帧 16ms 的预算里留出绘制的份。

- static int BuildChunk()
  - 一个切片重排多少轮：太小会把时间花在重新取时间上，太大则一个
    切片就可能超出预算。

- static bool HasInt(List<int> xs, int v)

- static int RoleCode(string role)
  - 发言者编码，用于整块着色：0 我 / 1 助手 / 2 工具 / 3 错误。

- static void WrapInto(string s, int maxW, int fontSize, List<string> outLines)
  - 把 `s` 贪心换行到 `maxW` 像素，结果追加进 `outLines`。

- void WrapRows(string s, int maxW, int fontSize, int color, int kind)
  - 把 `s` 换行后作为展示行追加进缓存（至少追加一行）。

- void MarkBlockEnd()
  - 收束一个轮次：最后一行带上分隔标记。StyledText 把它画成块底部的
    一条细线，比追加一整行空行省掉整整一行行高——记录里那些空行
    就是这么来的。

- bool SkipBlank(string s)
  - 这一行正文该不该丢掉：markdown 段落之间的空行只留一条，紧跟
    名字行 / 折叠行的空行直接不要，间距统一交给行高与分隔线。

- void TrimTrailBlank(int floorIdx)
  - 去掉 `floorIdx` 之后结尾处的空正文行（模型爱在答案末尾留空行）。

- void OwnRows(int start, int owner)
  - 给 `start` 之后新增的行标注所属发言者，以便整块着色。

- static string MdStrip(string ln)
  - 去掉轻量 markdown 标记（标题井号、`**` 粗体、单反引号），保留列表短横。

- static bool HasLongCode(List<string> src)
  - 记录里是否有需要折叠的长代码块。

- void BuildRange(App app, int from, int to, int textW)
  - 只排 [from, to) 这几轮并**追加**进缓存。这样切分让已定型轮次的
    换行结果留下来，只重排正在流式输出的那一轮。

- static string SelectionText(List<TextRun> rows, LogState st)
  - `cache` 中选中的文本（换行拼接，未选中则 ""）。


## ConsoleView (class)

尾部滚动、可选择的控制台视图**控件**：把 LogView 的滚动与
选择状态包进一个标准 retained 控件，于是声明式界面
（`.zform`）可以像放 ListView 一样直接声明一个控制台，
宿主只需要绑定行列表。行颜色可选。

- List<string> data;
  - 调用方的行，每帧读取（不拷贝）。

- List<int> colors;
  - 与 `data` 平行的行颜色（0 = 主题默认），可为空列表。

- LogState st;

- string Empty;
  - 无行时居中显示的提示（"" 则不绘制）。

- bool live;
  - false 时只绘制，不处理滚轮/选择（嵌入的小日志条）。

- ConsoleView()

- override string Kind()

- override string StyleType()

- ConsoleView Bind(List<string> lines)
  - 绑定调用方的行列表（实时视图，不拷贝）。

- ConsoleView WithColors(List<int> cols)
  - 绑定与行平行的颜色列表。

- ConsoleView EmptyText(string text)

- ConsoleView Passive()
  - 只读日志条：不接受滚轮与拖拽选择。

- LogState State()
  - 视图的滚动/选择状态，供宿主持久化或复位。

- void Reset()
  - 跳回最新一行并清除选择。

- string SelectedText()
  - 当前选中的文本（无选择时为 ""）。

- override void OnPaint(App app)


## DockGroup (class)

共享同一区域的面板堆栈：顶部是标签条，
下方是活动面板的内容。同一侧的分组沿该侧的
交叉轴排列（left/right 垂直堆叠，bottom/center 并排），
相邻分组之间有可拖拽的分隔条。

- int key;
  - 在列表变动中保持稳定的身份（停靠时分组会被移动和移除），
    代码可以跨一次变更持有一个分组。

- static int nextKey;

- int side;

- List<string> ids;

- int active;

- int extent;
  - 交叉轴尺寸（像素）。首次布局得出实际值前 <= 0
    `weight`；此后由拖拽分隔手柄决定。

- int weight;

- Rect bounds;

- Rect header;

- Rect body;

- DockGroup(int side, int weight)


## DockHost (class)

停靠布局管理器——IDE 风格窗口的外壳。面板位于
停靠到左、右、下或中央的标签组中；每个组都可以
拖动分隔手柄调整大小，每个标签可拖到另一个
组（或拖到边缘以在那里新建组）并关闭，整个
布局会序列化为字符串，重启后仍能恢复。

保留模式：宿主只创建一次、面板只注册一次，然后每帧
在 Begin 与 End 之间驱动它，把每个面板画进宿主交回的矩形
中：

// 仅一次：
DockHost dock = new DockHost();
dock.Register("explorer", "EXPLORER", DockSide.Left(), true, true);
dock.Register("outline",  "OUTLINE",  DockSide.Left(), true, true);
dock.RegisterFixed("editor", "Editor");            // 中央文档区
dock.Register("output", "OUTPUT", DockSide.Bottom(), true, true);
dock.LoadLayout(File.ReadAllText(layoutPath));     // 可选

// 每帧：
dock.Begin(app, area);
RenderExplorer(app, dock.Content("explorer"));     // 隐藏时为空矩形
RenderEditor(app, dock.Content("editor"));
RenderOutput(app, dock.Content("output"));
dock.End(app);                                     // 拖拽幽灵 + 放置提示
if (dock.TakeChanged()) { File.WriteAllText(layoutPath, dock.SaveLayout()); }

关闭的面板只是隐藏而不会被遗忘：HiddenIds() 为"恢复
面板"菜单提供数据，Show(id) 可将其停靠回原位置。

- List<DockPanel> panels;

- List<DockGroup> groups;

- int leftW;
  - 各侧尺寸（像素）：左右为宽度，底部为高度。

- int rightW;

- int bottomH;

- bool sized;

- int dpiScale;

- bool logicalLayoutPending;

- bool dragging;
  - 拖拽状态：正在拖拽的面板和已解析的放置目标。
    dropKind：0 = 无，1 = 并入 `dropGroup`，2 = 在 `dropSide` 新建组
    ，索引为 `dropIndex`。

- bool dragArmed;

- string dragId;

- int dropKind;

- int dropGroup;

- int dropSide;

- int dropIndex;

- Rect dropRect;

- bool menuOpen;
  - 面板菜单（右键标签条）：列出已关闭的面板，以便
    重新停靠。无关闭面板时显示 `emptyMenuLabel`，它
    可设置，方便宿主本地化。

- int menuX;

- int menuY;

- string emptyMenuLabel;

- List<string> switchIds;
  - Ribbon 面板开关：它们对应的 id 以及在
    宿主命令列表中的起始位置（见 AddSwitches）。

- int switchBase;

- string switchHideHint;
  - 点击开关会做什么，附加到其提示中（见 SetSwitchHints）。

- string switchShowHint;

- string lastClosed;

- bool changed;

- bool pendingResize;
  - 正在进行的调整大小拖拽：松开时提升为 `changed`，让宿主
    持久化布局时只写入一次，而不是每帧一次。

- Rect area;

- int headerH;

- DockHost()

- DockPanel Find(string id)

- int GroupOf(string id)
  - 包含 `id` 的组索引；面板已关闭时为 -1。

- void Register(string id, string title, int side, bool closable, bool newGroup)
  - 注册面板。`newGroup` 在该侧新建一个标签组；
    否则面板加入该侧最后一个组。注册是
    幂等的，因此宿主可以在恢复的布局上重建。

- void RegisterFixed(string id, string title)
  - 注册固定文档区：一个无标题栏、不可关闭的中央面板，
    其他面板围绕它停靠（宿主不为它绘制标签，因此
    屏幕上只有调用方自己的文档标签栏）。

- DockGroup LastGroupOf(int side)

- void SetWeight(string id, int weight)
  - 为包含 `id` 的组设置其所在侧交叉轴的初始占比
    （相对于该侧其他组）。用户拖动过手柄或恢复过保存的布局后
    此设置将被忽略。

- void SetSideExtent(int side, int px)
  - 设置某一侧的像素尺寸（左右为宽度，底部为高度）。

- bool Visible(string id)

- Rect Content(string id)
  - 上一次 Begin() 解析出的内容矩形。隐藏（或未激活）的
    面板得到零尺寸矩形，调用方可以用
    单个 `if (r.width <= 0) { }` 守卫跳过渲染，或直接画到空处。

- Rect ContentParked(string id)
  - 与 Content 类似，但隐藏面板得到一个停在屏幕外远处的 1x1 矩形，
    而不是零尺寸矩形。始终渲染（并注册命中区域）的即时模式内容
    可以不加守卫：它只是画在屏幕外而已。

- string Title(string id)

- void SetTitle(string id, string title)
  - 重命名面板标题（例如跟随 UI 语言切换）。

- void SetIcon(string id, string icon)
  - 设置面板 Ribbon 开关显示的图标。

- void SetTip(string id, string text)
  - 描述面板用途；其 Ribbon 开关悬停时显示该描述。

- void SetSwitchHints(string hideHint, string showHint)
  - 附加到每个开关提示的本地化"点击作用"说明。

- string SwitchTip(DockPanel p)

- int AddSwitches(List<RibbonCmd> cmds)
  - 为每个可关闭面板向 ribbon 命令列表追加一个图标开关——
    即工具栏的"面板"区段。开关位于列表尾部，
    因此宿主仍可按索引访问自己的命令，
    HandleRibbon() 消费它们的点击。（重新）构建命令列表后立即调用；
    返回开关起始的索引。
    
    dock.AddSwitches(cmds);                    // 在 BuildRibbon() 之后
    dock.SyncSwitches(cmds);                   // 每帧
    int pos = Ribbon.Render(app, x, y, w, h, cmds);
    if (!dock.HandleRibbon(pos)) { ...own commands... }

- void AddSwitchGroup(List<RibbonGroup> groups, string title)
  - 向选项卡式 ribbon 的组列表追加一个带标题的"面板"图标开关区段（每个可关闭面板一个）——
    即 View 选项卡对应的停靠版本。
    每帧构建组，开关承载实时的
    可见性；HandleRibbon() 消费它们的点击：
    
    List<RibbonGroup> groups = BuildMyGroups(tab);
    if (tab == viewTab) { dock.AddSwitchGroup(groups, "Panels"); }
    else { dock.ClearSwitches(); }
    int pos = Ribbon.RenderTabbed(app, x, y, w, tabs, activeTab, groups);
    if (!dock.HandleRibbon(pos)) { ...own commands... }

- void ClearSwitches()
  - 丢弃开关映射——用于显示它们的工具栏消失时（另一个 ribbon 选项卡），
    这样任何点击都不会被误认为是开关点击。

- void SyncSwitches(List<RibbonCmd> cmds)
  - 根据实时布局刷新开关的点亮状态（和标签）。

- bool HandleRibbon(int pos)
  - 切换 ribbon 点击命中的面板。点击是面板开关时返回 true
    （宿主不应将其作为命令分发）。

- void Close(string id)
  - 隐藏面板并将其移出所在组（若它是最后一个标签则删除该组）。
    面板仍保持注册状态，可随时恢复。

- void NoteHome(DockPanel p)
  - 记住面板当前所在的分组、标签序号和该分组在同侧的序号，
    供 Show() 原位恢复。

- int SideIndexOf(int gi)
  - 分组在同侧分组中的序号（0 = 该侧第一个）。

- int InsertPosFor(int side, int at)
  - 同侧第 `at` 个分组在 groups 列表中的插入位置。

- int GroupWithKey(int key)

- void Detach(string id)

- void ShowOn(string id, int side)
  - 重新停靠已关闭的面板：若 `side` 上有打开的组则并入其中，
    否则在该侧新建组。同时选中该面板。

- bool IsHidden(string id)

- void Toggle(string id)
  - 面板的显示/隐藏开关——"面板"工具栏按钮所驱动的内容。

- void Show(string id)
  - 把已关闭的面板放回关闭前的位置：原分组还在就插回原来的
    标签序号，原分组已经空掉被移除就在同侧原来的位置重建它。

- int IndexIn(DockGroup g, string id)

- List<string> HiddenIds()
  - 所有已关闭面板的 id，按注册顺序——供"恢复面板"菜单
    作为数据模型。

- void Focus(string id)
  - 将面板带到其组的最前面（若已关闭，先停靠回原位
    ）。

- string TakeClosed()
  - 自上次调用以来被关闭的面板，或 ""——让宿主记录它或
    销毁面板的资源。

- bool TakeChanged()
  - 布局发生变化（调整大小、停靠、关闭、恢复）时返回一次 true——
    提示应持久化 SaveLayout()。

- int PersistLogical(int px)

- int PersistPhysical(int logical)

- string SaveLayout()
  - 将各侧、组和隐藏面板序列化为一行，例如
    `v2;S=240,280,160;G=0,180,0,explorer|outline;G=3,0,0,editor;H=terminal:2`
    （隐藏面板携带它应恢复到的侧边）。
    加载时跳过未知 id，因此该格式能容忍不同版本间
    出现和消失的面板。

- bool LoadLayout(string text)
  - 恢复 SaveLayout 写入的布局。布局未提及的面板
    保留其注册位置；布局提到但未注册的 id 被忽略。
    当字符串不是本版本能理解的布局时返回 false
    （调用方此时保留默认设置）。

- static int SideOfIn(List<DockGroup> gs, string id)

- static bool ListHas(List<string> xs, string v)

- static bool HasPrefix(string s, string p)

- static string After(string s, int n)

- static List<string> SplitOn(string s, string sep)
  - 按单字符分隔符拆分，丢弃空段。

- bool SideHasVisible(int side)

- bool GroupHasVisible(DockGroup g)

- List<DockGroup> VisibleGroups(int side)

- void Begin(App app, Rect a)
  - 在 `a` 内布置整个停靠区，绘制分隔手柄和每个
    组的标签条，并处理调整大小拖拽、标签点击、关闭和
    标签拖拽的开始。每帧在绘制面板之前调用一次。

- int Handle(App app, int x, int y, int w, int h, bool vertical)
  - 单个分隔手柄。拖动时返回新的起始坐标
    （垂直手柄返回 x，水平手柄返回 y），否则返回 -1。

- void LayoutSide(App app, List<DockGroup> gs, Rect r, bool vertical, int hs)
  - 沿交叉轴在侧边各组之间分割 `r`，在相邻组之间绘制
    手柄以及每个组的边框装饰。

- void RenderGroup(App app, DockGroup g)
  - 绘制一个组的表面和标签条，解析活动面板的
    内容矩形，并处理其标签上的选择/关闭/开始拖拽。

- void RenderMenu(App app)
  - 绘制右键单击标签条打开的面板菜单：每个
    关闭的面板一个条目，选中即将其停靠回原位置。

- void RegisterClipped(App app, int id, int rx, int ry, int rw, int rh, int clipX, int clipW)

- void End(App app)
  - 结束一帧：标签拖拽期间解析放置
    目标、高亮它并绘制拖拽幽灵；松开时
    重新停靠面板。每帧在绘制面板之后调用一次，这样
    提示会绘制在最上层。

- void ResolveDrop(App app)
  - 选取指针下方的放置目标：组的标签条或其中部并入该组，
    组的边缘带在旁侧新建组，
    整个停靠区的外侧带停靠到相应侧边。

- static int EdgeZone(Rect r, int mx, int my)
  - 0 = 中部，1 = 左带，2 = 右带，3 = 顶带，4 = 底带。

- static Rect HalfRect(Rect r, int zone)

- static int SideForZone(int zone)

- Rect SideStrip(App app, int side)
  - 某一侧将占据的条带，用作侧边停靠的放置提示。

- int SideIndexOf(DockGroup g)
  - `g` 在其所在侧各组中的位置。

- int SideCount(int side)

- int InsertPos(int side, int k)
  - 新组成为 `side` 的第 `k` 组时的全局索引。

- void ApplyDrop()

- int IndexOfKey(int key)

- bool Dragging()
  - 标签正在拖拽时为 true——宿主可以抑制自己本帧的
    悬停/拖拽处理。


## DockPanel (class)

一个可停靠面板：稳定 id（用于持久化和查找）、
标签页上显示的标题，以及上次布局解析出的内容矩形。
`headerless` 面板（如 IDE 的文档区）不绘制标签页，也不能被
拖拽或关闭——其他面板围绕它们停靠。

- string id;

- string title;

- string icon;
  - 宿主面板切换器的图标名（见 DockHost.AddSwitches）。

- string tip;
  - 一行说明面板用途的文字，显示在其切换器的悬停提示中。

- bool closable;

- bool headerless;

- bool hidden;

- int home;
  - 面板上次停靠的边——Show() 将其放回的地方。

- int homeKey;
  - 关闭时所在分组的 key、在该分组里的标签序号，以及该分组
    在同侧分组中的序号——Show() 据此把面板放回原来的位置，
    而不是简单地并到该侧最后一个分组的末尾。

- int homeAt;

- int homeSideIdx;

- Rect body;

- DockPanel(string id, string title, bool closable)


## DockSide (class)

具名停靠边，布局中写 `DockSide.Left()` 而不是魔法整数。
Left/Right 是通高列，Bottom 是中央区域下方的条，
Center 是其他所有面板围绕停靠的区域。

- static int Left()

- static int Right()

- static int Bottom()

- static int Center()


## FileFilter (class)

一个底部扩展名过滤器：显示标签和其以 ";" 分隔的模式
（"" 匹配所有文件）。用单个实体取代并行的标签/模式列表。

- string label;

- string pattern;

- FileFilter(string label, string pattern)


## FilePicker (class)

可复用的应用内路径选择器，承载于独立 GUI 窗口。

- static int lang=1;

- Input pathInput;

- Input nameInput;

- Input folderInput;

- Input searchInput;
  - 列表上方的筛选框：按子串缩小当前目录里的文件夹和文件，
    两种模式都有（选文件夹时也要能在几百个目录里找）。

- SignalInt treeScroll;

- SignalInt listScroll;

- SignalBool showHidden;

- App popup;

- bool open;

- bool closing;

- bool creatingFolder;

- bool treeDragging;

- bool listDragging;

- int treeDragOffset;

- int listDragOffset;

- int mode;

- int result;

- int skinIndex;

- string title;

- string suffix;

- string selectedPath;

- string statusText;

- string lastFilter;

- List<string> expanded;

- List<FileFilter> filters;
  - 底部下拉框中显示的扩展名过滤器。每个 FileFilter 将一个
    人类可读标签（如 "Zan source (*.zan)"）与 ";" 分隔的扩展名
    模式配对（"" = 所有文件）。filterSel 是选中的索引；filterOpen
    跟踪下拉弹窗状态。

- SignalInt filterSel;

- bool filterOpen;

- bool filtersCustom;

- UiEvent Accepted;

- UiEvent Cancelled;

- UiEvent Closed;

- UiEvent PathChanged;

- UiEvent FilterChanged;

- FilePicker()

- void SetFilters(List<FileFilter> items)
  - 为下一次打开提供显式扩展名过滤器。每个条目将
    显示标签与 ";" 分隔的扩展名模式配对（"" 匹配全部），
    e.g. SetFilters(new List<FileFilter>{
    new FileFilter("Zan (*.zan)", ".zan"),
    new FileFilter("All (*.*)", "")
    });

- Input SearchBox()
  - 筛选框，首次渲染时按当前界面语言建好并复用。

- static string T(string en, string zh)

- static string Normalize(string path)

- static string LowerAscii(string text)

- static bool EndsWith(string text, string ending)

- static string Join(string dir, string name)

- static string ParentDirectory(string dir)

- static string ValidDirectory(string initial)

- static string RootOf(string dir)

- static bool Inside(App app, int x, int y, int w, int h)

- static bool Clicked(App app, int x, int y, int w, int h)

- static bool Pressed(App app, int x, int y, int w, int h)

- static int WheelStep(App app)

- static bool ValidFolderName(string name)

- static bool SamePath(string a, string b)
  - 同一个目录在不同来源下大小写并不一致：树根来自
    `Directory.GetRootsUtf8()`（`D:/`），展开集是从初始目录一路取父
    目录得到的（`d:/project/...`）。逐字符比较会让 `D:/` 永远不算已
    展开，于是左侧树只剩两个盘符、点不开当前目录。

- bool IsExpanded(string path)

- void Expand(string path)

- void Collapse(string path)

- void ExpandAncestors(string dir)

- void BuildTree(string dir, int depth, List<string> paths, List<int> depths)

- void Begin(int pickerMode, string initial, string extension, string defaultName, string dialogTitle)

- void BeginFolder(string initial, string dialogTitle)

- void BeginOpenFile(string initial, string extension, string dialogTitle)

- void BeginOpenFileFilters(string initial, List<FileFilter> items, string dialogTitle)
  - 带显式扩展名过滤器集合的打开文件对话框。

- static string FirstExt(string pattern)
  - ";" 分隔模式中的第一个扩展名（模式为空则为 ""）。

- static bool ContainsText(string text, string needle)
  - 名称过滤框的不区分大小写子串测试（"" 匹配所有）。

- bool MatchesCurrentFilter(string name)
  - `name` 通过当前所选过滤器时为 true。空模式
    （所有文件）匹配一切；否则 ";" 分隔的
    任一扩展名匹配即可。

- void BeginSaveFile(string initial, string extension, string defaultName, string dialogTitle)

- void ApplySkin(int index)

- bool IsOpen()

- bool NeedsRedraw()

- bool OwnsWindow(nint hwnd)

- string SelectedPath()

- string CurrentDirectory()

- void RaiseFilterChanged()

- void Finish(int action)

- void Close()

- bool ApplyEvent(nint evHwnd)

- int TakeResult()

- void Navigate(string dir)

- void RenderWindow()

- void RenderContent(App app)


## FileTree (class)

磁盘目录 -> TreeView 节点的薄封装：遍历本身由 System.IO.DirectoryTree
完成，这里只负责把名字变成带真实路径的 TreeNode，让资源管理器、示例
面板、资产面板共用同一份树：

tree.Bind(FileTree.Nodes(root, "src",
PathFilter.Any().Skip(".build"), true, true));

- static void Into(List<TreeNode> nodes, string dirPath, string dirName, int depth, bool expanded, PathFilter f, bool withRoot, bool expandAll)
  - 扫描 `dirPath`，目录在前、文件在后，按前序加入 `nodes`。
    `withRoot` 为 true 时先加一个代表 `dirPath` 自身的目录节点，
    其内容缩进一级；为 false 时内容直接落在 `depth` 上。
    `expandAll` 让所有子目录节点默认展开（示例面板那种一眼看全的树）。

- static List<TreeNode> Nodes(string dirPath, string dirName, PathFilter f, bool withRoot, bool expanded)
  - `Into` 的便捷形式：新建列表返回，可直接 TreeView.Bind。


## GraphNode (class)

依赖图里的一个节点。

`deps` 里放前置节点的 id，图的层次、连线和解锁都从它推出来：
前置全部 `done` 的节点才可以开工，未满足的节点由图自己标成锁住的
——调用方不需要再维护一份「谁可以开始」的并行状态。

布局字段（`depth`/`lx`/`ly`/`lw`/`lh`）由 GraphView 每帧写回，读它们
可以把浮层、菜单锚到节点上。

- string id;

- string title;

- string tag;
  - 负责人之类的短标签，画在标题下方。

- string tagClass;
  - 上面那个标签的语义类（`primary`/`success`…），由皮肤取色。

- string state;
  - 状态短语（进行中 / 待办 / 阻塞…）。

- string stateClass;

- int progress;
  - 0..100 的进度；负数表示这个节点不显示进度条。

- bool done;
  - 该节点已完成：后继节点据此解锁。

- List<string> deps;
  - 前置节点的 id。

- string payload;
  - 调用方自己的 payload（索引、路径…），图不解释它。

- int depth;
  - 布局结果：层号与节点卡片的屏幕矩形。

- int lx;

- int ly;

- int lw;

- int lh;

- static GraphNode Of(string id, string title)

- GraphNode After(string depId)
  - 声明一个前置节点。

- GraphNode WithTag(string text, string cls)

- GraphNode WithState(string text, string cls)

- GraphNode WithProgress(int pct)

- GraphNode WithPayload(string p)

- GraphNode Finished()


## GraphView (class)

节点连线画布：一份 `List<GraphNode>` 按依赖关系分层画成图，
节点是标准控件搭的卡片，画布本身只负责层次布局、连线和交互。

GraphView g = new GraphView();
g.Bind(model.nodes);            // 调用方的列表，不复制
g.OnSelect(ShowNodeContext);    // 读 SelectedIndex / Selected
g.OnActivate(StartSelected);    // 双击开工
g.OnBlocked(ExplainLock);       // 双击了前置未完成的节点
panel.Add(g);

层次是从依赖算出来的最长路径：没有前置的节点在第 0 层，其余节点
排在其所有前置的下一层，所以连线一律自上而下，不会出现回头的边。
一层放不下就在本层内折行，因此只需要纵向滚动。

节点卡片默认由标准控件拼出（标题 + 负责人/状态标签 + 进度条 +
锁标记）；传入模板即可换成调用方自己的组件：

GraphView g = new GraphView(n => MyCard(n));

前置未全部完成的节点是锁住的：仍可点开查看，但双击不会触发
Activate，而是触发 Blocked，让宿主说明还差哪一步。

- List<GraphNode> data;
  - 调用方的节点，每帧读取；卡片需要重建时调用 Refresh()。

- SignalInt sel;

- SignalInt scroll;

- GraphCardOf tpl;

- List<Control> cards;
  - 每个节点一张卡片，索引与 `data` 对齐。

- List<Tag> locks;
  - 默认卡片里的锁标记，索引与 `cards` 对齐（自定义卡片处为 null）。

- bool cardsStale;

- int builtFor;

- string Empty;
  - 图没有节点时居中显示（"" 不画）。

- int ctxRow;
  - 处理器运行期间设置：事件涉及的节点与指针位置。

- int ctxX;

- int ctxY;

- UiEvent Select;
  - 节点被选中时（读 SelectedIndex / Selected）。

- UiEvent Activate;
  - 节点被双击且已解锁时（开工）。

- UiEvent Blocked;
  - 双击了锁住的节点时（读 ContextRow，说明还差哪些前置）。

- UiEvent Context;
  - 节点被右键点击时（读 ContextRow / ContextX / ContextY）。

- void InitGraph()

- GraphView()

- GraphView(GraphCardOf template)
  - 自定义节点卡片。

- override string Kind()

- override string StyleType()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- GraphView Bind(List<GraphNode> nodes)
  - 绑定调用方的节点列表；不复制任何内容。

- void Refresh()
  - 节点内容变了：下一帧重建卡片。

- GraphView BindSel(SignalInt s)
  - 共享调用方的选择信号，供在列表重建后保持选择的宿主使用。

- GraphView EmptyText(string text)

- GraphView OnSelect(Action a)

- GraphView OnActivate(Action a)

- GraphView OnBlocked(Action a)

- GraphView OnContext(Action a)

- int Count()

- bool Has(int row)

- GraphNode NodeAt(int row)

- List<GraphNode> Nodes()

- int SelectedIndex()

- bool HasSelection()

- GraphNode Selected()
  - 选中的节点；仅在 HasSelection() 时有效。

- int ContextRow()
  - 事件处理器涉及的节点行号与指针位置。

- int ContextX()

- int ContextY()

- int IndexOfId(string id)

- void SelectId(string id)
  - 按 id 选中节点，并触发 Select（宿主恢复上次选择时用）。
    空 id ＝ 取消选择：宿主离开某个节点（回到全局视图）时，图上不该
    还高亮着它。

- void Deselect()
  - 取消选择。不触发 Select（没有节点可读），只清掉高亮。

- static int IndexOf(List<GraphNode> ns, string id)

- List<string> PendingDeps(int row)
  - `row` 的前置里还没做完的那些 id。空表示它可以开工了。
    指向不存在节点的前置被忽略：图只为自己画得出的关系负责，
    半截的引用留给宿主自己校验，而不是在这里把节点永久锁死。

- bool LockedAt(int row)
  - 前置没做完 —— 节点锁住，双击只会触发 Blocked。

- void ComputeDepth()
  - 层号 = 到某个无前置节点的最长路径。用逐轮松弛算，轮数以节点
    数为上限：依赖成环时不再变化就停下，图仍然画得出来（环上的边
    会同层相连），而不是无限递归下去。

- bool Templated()

- void BuildCards(App app)
  - 每个节点一张卡片。卡片是本控件的子节点，因此由框架测量、
    绘制、换肤，画布自己只摆位置、画连线和标记选中/锁住。

- static Card DefaultCard(GraphNode n, Tag mark)
  - 内置节点卡片：标题（右上角锁标记）+ 负责人/状态标签 + 进度条，
    全是标准控件，卡面本身是标准 Card，`card.graphnode` 及其状态类
    在皮肤里。

- static string TagClass(string cls)
  - 节点标签一律用小号，语义类由调用方给。

- static Tag LockMark()
  - 锁标记：前置没做完时显示的标准标签（默认卡片专用）。

- int Gap(App app)

- int CardWidth(App app, int areaW)
  - 所有卡片统一宽度：图要读的是层次，宽度参差不齐会让同层
    的卡片看着像不同的东西。取最宽的那张，再收进可用宽度。

- int CardHeight(App app)

- int Layout(App app, int areaX, int areaY, int areaW)
  - 按层排布，写回每个节点的矩形，返回内容总高度。`areaX/areaY`
    是内容区左上角（已含滚动偏移）。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- int ScrollOffset(App app, int contentH)

- void PaintEdges(App app)
  - 依赖连线：前置卡片底边中点 -> 本节点顶边中点，走折线，末端
    带箭头。已完成的前置用强调色，未完成的用分隔线色 —— 一眼
    看出哪条边还卡着。

- void PaintNodes(App app)
  - 节点：把卡片摆到算出来的矩形上，并按选中/悬停/锁住换类 ——
    选中态、悬停态和锁住态的长相因此都在皮肤里，画布不自绘卡面。

- static string NodeClass(bool selected, bool hovered, bool locked)
  - 节点卡片的状态类。

- int NodeInteract(App app, int index, GraphNode n)
  - 一个节点的选中 / 开工 / 右键。返回命中 id，供上面画悬停态。

- void NoteSelfDamage(App app)
  - 选中与滚动只改画布自己那块像素，损伤限定在控件矩形内。

- void PaintEmpty(App app)


## LogState (class)

LogView 的滚动和文本选择状态，由调用方持有，
使视图在帧间保持位置。

- int scroll;

- int selR0;

- int selC0;

- int selR1;

- int selC1;

- bool selDrag;

- bool barDrag;

- LogState()

- void ClearSelection()

- bool HasSelection()
  - 选择至少跨越一个字符时为 true。

- void Reset()


## LogView (class)

尾部滚动、可选择的文本日志（控制台视图）：最新一行位于
底部，滚轮和可拖拽的滚动条可回溯历史，
指针可跨行选择文本。行颜色由
调用方提供，视图无需知道自己在记录什么。

- static int RowHeight(App app)

- static int Padding(App app)

- static int VisibleRows(App app, int h)
  - 高度为 `h` 的视图中能容纳的行数。

- static int FirstRow(App app, int h, int count, int scroll)
  - 给定滚动位置下绘制的第一行的索引。

- static int RowAt(App app, int y, int h, int count, int scroll, int mouseY)
  - `mouseY` 下的行索引；日志为空时为 -1。

- static int ColAt(string line, int textX, int fontSize, int mouseX)
  - `line` 中最接近 `mouseX` 的字符边界。

- static string SelectionText(List<string> lines, LogState st)
  - `lines` 中被选中的文本，以换行连接（无选中时为
    ""）。

- static void Render(App app, int x, int y, int w, int h, List<string> lines, List<int> colors, LogState st, string emptyText, bool interactive)
  - 在 x,y,w,h 中绘制日志，并处理滚轮滚动、滚动条拖拽
    和拖拽选择。`colors` 为空（每行使用主题的
    主文本色）或与 `lines` 平行，其中 0 也表示
    "主题默认"。无内容可显示时绘制 `emptyText`。
    传 `interactive` 为 false 可绘制指针不响应的日志。

- static void HandleInput(App app, int x, int y, int w, int h, List<string> lines, LogState st)
  - 日志主体的滚轮、滚动条拖拽和拖拽选择。


## PivotAgg (class)

单个透视单元格或合计的聚合累加器（求和/计数/最大/最小）。
取代旧的 sum/cnt/mx/mn 平行列表约定。

- int sum;

- int cnt;

- int mx;

- int mn;

- PivotAgg()

- void Add(int v)

- int Value(int aggType)


## PivotConfig (class)

PivotTable 交叉表（cross-tabulation）的配置。
rowField  ：列索引，其不同取值成为透视表的行
colField  ：列索引，其不同取值成为透视表的列
valueField：数值列索引，在每个交叉点处聚合
aggType   ：0 求和，1 计数，2 平均，3 最大，4 最小

单个度量可内联给出（valueField/aggType）。若要多个
度量并排显示，请调用 AddValue(...) 一次或多次；此时内联的
value 被忽略，每个 colKey 在两级列标题下展开为每个度量
一个子列。

- int rowField;

- int colField;

- int valueField;

- int aggType;

- string rowTitle;

- string colTitle;

- string valueTitle;

- List<PivotMeasure> measures;

- PivotConfig(int rowField, int colField, int valueField, int aggType)

- static PivotConfig AddValue(PivotConfig p, int field, int agg, string title)
  - 添加一个度量（数值字段 + 聚合方式）。流式：返回配置本身。


## PivotMeasure (class)

一个已解析的度量：读取哪一列、如何聚合以及它的
显示标题。取代旧的 field/agg/title 平行列表。

- int field;

- int agg;

- string title;

- PivotMeasure(int f, int a, string tt)


## PivotTable (class)

交叉表/透视表。按两个字段对原始 DataRows 分组，并在每个
行列交叉点聚合一个或多个数值度量，带行
合计、列合计和总计。聚合每帧运行（每个轴上的
不同键数量预计不会太多）。

- static string AggName(int a)

- static int AggValue(int s, int n, int mx, int mn, int a)

- static string MeasureLabel(int agg, string title)

- static List<string> SortedDistinct(List<DataRow> rows, int field)

- static void DrawNum(Canvas c, int cx, int cy, int cw, int rowH, int pad, string val, int color, int fs)

- static void Render(App app, int x, int y, int viewW, int viewH, List<DataRow> rows, PivotConfig cfg)


## PropertyGrid (class)

属性表：把一组 PropSpec 画成可编辑的属性行，值直接读写 spec 绑定
的字段 / signal。行的标签、编辑器、滚动、裁剪都由控件自己完成，
宿主只提供 spec 列表：

grid.Bind("Field:" + sel.fname, this.FieldSpecs(sel));
grid.RenderInside(app, new Rect(x, y, w, h));

编辑器按 PropSpec.kind 选：0/4 文本 Input，1 整数 Input，2 Switch，
3 SelectBox，5 分组标题（PropSpec.Section，只有文字没有编辑器）。

- List<PropSpec> data;

- List<Input> texts;
  - 每行一个编辑器（按行索引对齐，未用到的槽为 null）。

- List<SignalString> textSigs;

- List<Switch> flags;

- List<SignalBool> flagSigs;

- List<SelectBox> picks;

- List<SignalInt> pickSigs;

- List<int> minusIds;
  - 整数属性行的 -/+ 步进按钮稳定控件 id（按行索引对齐，
    非整数行也占位）。

- List<int> plusIds;

- List<string> shadow;
  - 上一帧的值：用来判断这一帧是外部改了模型还是用户改了编辑器。

- string key;
  - 当前绑定对象的标识，变化时重建编辑器。

- SignalInt scroll;

- string Empty;
  - 没有属性时显示的文本（"" 不画）。

- UiEvent Change;
  - 某一行被用户改动后触发（读 ChangedKey 得知是哪一个）。

- int hitRow;

- PropertyGrid()

- override string Kind()

- PropertyGrid OnChange(Action a)

- int ChangedRow()
  - 最近被用户改动的行号 / 属性键。

- string ChangedKey()

- void Bind(string k, List<PropSpec> specs)
  - 绑定一组属性。`k` 标识当前对象（如 "Field:btn0"）：它变化时
    才重建编辑器，否则沿用现有的，输入焦点和光标不会每帧丢失。

- void Rebuild()

- int IndexOfValue(PropSpec p, string val)
  - 枚举值（可能存的是序号，也可能是选项文本）对应的选项下标。

- string PickValue(PropSpec p, int idx)
  - 枚举行当前选择在模型里的字符串形式：绑定到 int 的存序号，
    绑定到字符串的存选项文本。

- static bool IsNumber(string s)

- string Editor(int i)
  - 编辑器当前值的字符串形式（与 PropSpec.Read 同一种表示）。

- void Load(int i, string val)
  - 把模型值灌回编辑器（外部改了属性时）。

- void Sync(App app, int i)
  - 一行的双向同步：外部改动优先灌回编辑器，否则把编辑器的新值
    写进模型并广播 Change。整数行在输入合法前不写。
    提交时请求重绘：Change 经 Dispatcher 延迟到 UI 队列，若不保证
    下一帧，即时模式下循环会空转，处理器不排空、预览慢半拍。

- void Step(App app, int i, int delta)
  - 整数属性的 -/+ 步进：按 spec 配置的步长增减、钳制到范围，
    直接写回模型与编辑器，并立即请求重绘，预览同帧跟新。

- bool StepBtn(App app, int id, int x, int y, int sz, string glyph, StyleBox normalStyle, StyleBox hoverStyle)
  - 画一个步进小按钮（方形，中间一个字符字形），注册命中区，
    返回本帧是否被点击。

- int RowHeight(App app)

- int ContentHeight(App app)
  - 全部行的高度（宿主据此在自己的滚动区里给出槽位）。

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override List<string> Events()

- override void BindEvent(string evt, Action a)


## SessionList (class)

一个垂直、可搜索的命名条目列表，每行带删除按钮，顶部有
"新建"按钮——即聊天/会话侧边栏，保持通用：它只管理
自己的搜索框和滚动，对行的含义一无所知。
宿主每帧传入标题和活动索引，并
根据返回的 SessionListAction 采取行动。

SessionList nav = new SessionList("Search chats...", "New chat");
...
SessionListAction a = nav.Render(app, x, y, w, h, titles, current);
if (a.created) { ... }
if (a.selected >= 0) { ... }
if (a.deleted >= 0) { ... }

- Input search;

- SignalInt scroll;

- Button newBtn;

- string emptyText;

- bool showArchived;
  - 底部开关：归档行在开启前保持隐藏。

- string archiveTip;

- string deleteTip;

- string archivedLabel;

- string renameTip;

- int editing;
  - 行内重命名：正在编辑的行（-1 表示无）及其字段。从第一条消息
    推导出的标题只是猜测；用户有权决定
    聊天叫什么。

- Input rename;

- bool takeFocus;

- SessionList(string searchHint, string newLabel)

- SessionList Labels(string archive, string delete, string archived)
  - 每行归档/删除点击区域以及底部开关的标签，
    让宿主掌握措辞（及其翻译）。

- SessionList RenameLabel(string rename2)
  - 每行重命名框的提示（"" = 无）。

- void CancelRename()
  - 停止任何进行中的重命名（例如宿主在列表下方切换数据时）。

- SessionList EmptyText(string text)
  - 无行匹配当前搜索时居中绘制的文本（"" = 无）。

- string Query()
  - 当前搜索文本，宿主也可据此过滤自己的状态。

- static int RowH(App app)

- static int Fold(int b)

- static bool CiContains(string hay, string needle)

- SessionListAction Render(App app, int x, int y, int w, int h, List<string> titles, int cur)
  - 在给定矩形中绘制整个侧边栏，并返回被点击的内容。

- SessionListAction RenderEx(App app, int x, int y, int w, int h, List<string> titles, int cur, List<int> archived)
  - 同上，但每个标题带归档标志（1 = 已归档）。归档行
    隐藏在底部开关之后，每行在其删除叉号旁
    多一个归档框。


## SessionListAction (class)

SessionList 在单帧内报告的内容：切换、删除或新建聊天点击中至多一个。
索引指向调用方自己的标题列表，
宿主可直接将它们映射回自己的会话。

- int selected;

- int deleted;

- int archived;

- bool created;

- int renamed;

- string title;

- static SessionListAction None()

- bool Any()


## ChatSpeaker (delegate)

把成员 id 解析成外观；返回 null 表示「不认识这个 id，按默认助手画」。

`delegate ChatSpeaker ChatSpeakerOf(string agent);`


## Control (delegate)

为一个节点构建卡片控件，供节点长相由调用方决定的图使用。

`delegate Control GraphCardOf(GraphNode node);`


## string (delegate)

把协议节拍（`ChatMessage.kind`）翻成头行右侧的状态标签。

`delegate string ChatKindLabel(string kind);`
