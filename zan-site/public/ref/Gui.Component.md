# Gui.Component

> 源码: `packages/Zan.Gui.CodeEditor/src/Gui/Component/ChatView.zan`, `packages/Zan.Gui.CodeEditor/src/Gui/Component/ConsoleView.zan`, `packages/Zan.Gui/src/Gui/Component/Dock.zan`, `packages/Zan.Gui/src/Gui/Component/EditorPalette.zan`, `packages/Zan.Gui/src/Gui/Component/FilePicker.zan`, `packages/Zan.Gui/src/Gui/Component/FileTree.zan`, `packages/Zan.Gui/src/Gui/Component/GraphView.zan`, `packages/Zan.Gui/src/Gui/Component/LogView.zan`, `packages/Zan.Gui/src/Gui/Component/PropertyGrid.zan`, `packages/Zan.Gui/src/Gui/Component/Ribbon.zan`, `packages/Zan.Gui/src/Gui/Component/SessionList.zan`


## ChatBubble (class)

气泡模式里的一条：一条消息在内容坐标里的位置，以及它要画的东西。
由 `ChatView.BuildBubbles` 生成、`PaintBubbles` 消费，宿主看不到。

与记录模式的行表（`TextRun`）分开，是因为两者不是一回事：记录模式
的消息是「若干等高的展示行」，气泡模式的消息是「一个宽度按内容、
高度按换行结果的可变盒子」。把气泡硬塞进行表，量出来的宽度与画出来
的盒子迟早对不上。

- int idx;
  - 消息在 `data` 里的下标；-1 表示这不是一条消息（busy 提示行）。

- int top;
  - 内容坐标里的顶边，以及这块占的高度（含名字行与时刻分隔）。

- int h;

- int divH;
  - 顶部为时刻分隔预留的高度（0 = 这条不画分隔）与要画的文本。

- string divText;

- int nameH;
  - 名字行高度（群聊里别人的消息才有）与名字。

- string name;

- int quoteH;
  - 引用块高度（这条消息是「引用某条消息」的回复时非 0）与内容：
    被引人的显示名 + 原文摘要。IM 客户端把引用画在正文上方那一条，
    与名字行占的是同一段纵向空间的两个位置（引用在下、名字在上）。

- string quoteWho;

- string quoteText;

- string quoteShown;
  - 已经省略号截好的引用行（`引用者: 摘要`），绘制时直接画它，
    免得每帧在一段长摘要上做一次二分。

- List<string> lines;
  - 气泡正文的换行结果与最长行宽（图片气泡为空 / 0）。

- WrappedText linesAt;
  - 展示行 → 正文的字节偏移账（与 `lines` 同长）：鼠标落点换算成
    正文偏移、选区高亮反算每行的像素区间都要它。图片气泡为空。

- int textW;

- int imgW;
  - 图片气泡的画面尺寸（0 = 文本气泡）。

- int imgH;

- string imgPath;
  - 图片气泡的图片路径（`imgW > 0` 时有效）。

- string initial;
  - 头像首字与配色类名（busy 行没有头像）。

- string avaCls;

- bool mine;
  - 这条是「我」发的（右对齐、头像在右）。

- int bw;
  - 气泡自身的宽高（含内边距）。

- int bh;

- bool waiting;
  - busy 提示行：气泡用弱色画、不跟任何消息关联。

- ChatBubble()
  - 全部字段显式归零：`new` 出来的实例不依赖语言对未赋值字段的默认值。


## ChatHit (class)

气泡模式里的一处落点：命中第几条消息、落在正文的第几个字节。
选择按「消息下标 + 正文偏移」记（不是展示行/列）——展示行随窗口
宽度重排，正文偏移不会，改窗口宽度后选区仍框住同一段文字。

- int msg;

- int off;

- ChatHit()


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

- string img;
  - 图片附件路径（本地文件 / http(s) / data URI，交给 `Gui.Widget.Image`
    解码）。非空时这一轮在气泡模式里画成一张图片气泡；正文仍然保留，
    供记录模式（ChatStyle.Log）与复制使用。

- string msgId;
  - 宿主的消息 id（IM/服务端场景按 id 对账增量流；"" = 本地消息）。

- int mark;
  - 回执位（IM 用）：0 发出 / 1 送达 / 2 已读 / -1 别人的消息。
    渲染走 kind（宿主把 mark 翻成 "wx-acked" 一类的节拍串）。

- string quoteWho;
  - 引用回复：被引消息的发送者显示名与原文摘要（"" = 这条不是回复）。
    引用块画在自己气泡内、正文之上，与 IM 客户端的观感一致。

- string quoteText;

- string quoteId;
  - 被引消息的宿主 id（发送时原样回给服务端，对端才能还原引用关系）。

- bool card;
  - 卡片消息（任务卡片一类「整体即入口」的消息）：左键单击（未拖动）
    交给宿主 OnBubbleActivate，不进文本选区——卡片没有可选中正文，
    IM 惯例里单击选区对它没有意义。默认 false。

- static ChatMessage Create(string role, string text)
  - 一条普通消息：无思考、无信封，时间戳取当前 UTC 时刻。

- static ChatMessage CreateImage(string role, string path, string text)
  - 一条带图片的消息（气泡模式画图片，记录模式画正文）。

- static ChatMessage CreateWithReason(string role, string text, string reason)
  - 带思考过程的普通消息。

- static ChatMessage CreateFrom(string role, string text, string agent, string to, string kind, string taskId, int progress)
  - 一条带信封的团队消息。


## ChatSpeaker (class)

一位发言者的外观：显示名、色调与 Tag 类名。多智能体程序用它把
成员 id 翻成头行上看到的样子；单助手程序用不到。

- string name;

- int tint;

- string tagCls;

- static ChatSpeaker Of(string name, int tint, string tagCls)
  - 组装一位发言者外观。


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


## ChatStyle (class)

聊天视图的排版模式。

- static int Log()
  - 工具调用式记录（默认）：整宽行、轮次色块、头行横幅。适合助手/工具
    会话——那里每一条背后都跟着一大段工件，气泡会把它挤成窄柱。

- static int Bubble()
  - IM 气泡：左右分置的圆角气泡、头像、时刻与发送者名字在外的聊天记录。
    适合人对人的会话（微信/QQ 那种一次一两句的往来）。


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

- int style;
  - 排版模式（ChatStyle.Log / ChatStyle.Bubble）。

- ChatClock clockOf;
  - 时刻格式化钩子（null = 组件内置的本机时区 HH:MM）。

- ChatAvatarClick avatarClick;
  - 气泡头像被点中时的宿主回调（bubble 模式专用，见 AvatarHit）。

- ChatBubbleMenu bubbleMenu;
  - 气泡上右键的宿主回调（bubble 模式；null = 不响应右键）。

- ChatBubbleMenu bubbleActivate;
  - 卡片消息（msg.card）左键单击的宿主回调（null = 卡片不响应单击）。

- int actMsg;
  - 卡片单击的按下状态：命中消息下标与按下点（未拖动即视为单击）。

- int actX;

- int actY;

- App uiApp;
  - 命中测试要从鼠标坐标换算气泡几何，而点击事件到达时手里没有
    App 参数——OnPaint 把当帧的 app 缓存下来供 AvatarHit 用。

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

- List<ChatBubble> bub;
  - 量好的气泡几何（内容坐标，不含滚动偏移）。

- string bubKey;
  - 几何缓存对应的输入签名（"" = 还没量过）。

- int bubH;
  - 几何的总高度（内容坐标），滚动条的界限由它和视口高算出。

- bool bubStale;
  - 本次几何是整份重排（会丢弃选区）。

- SignalInt bscr;
  - 气泡模式的像素滚动信号（0 = 贴住最新一条）。

- int bubWMille;
  - 气泡正文的最大宽度比例（千分比），由 `WithBubbleWidth` 给。

- string bubSig;
  - 上次量气泡用的布局签名（`BubbleLayoutSig`），变了就是整份重排。

- bool showNames;
  - 别人的气泡上方显示发言人名字（群聊）。自己发的永远不显示。

- int selMsg0;

- int selOff0;

- int selMsg1;

- int selOff1;

- bool selDrag;

- ChatQuote quoteCb;
  - 宿主回调：用户对某条消息选择「引用」。文本是选中部分（没有选区
    就是整条），宿主据此在输入区拉起引用条。

- ChatView()
  - 空视图：英文默认文案，常驻的记录网格与空态提示都已建好
    （是否显示由每帧绘制决定）。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"log"）。

- ChatView Bind(List<ChatMessage> msgs)
  - 绑定调用方的消息列表（实时视图，不拷贝）。

- ChatView WithStrings(ChatStrings s)
  - 全部可见文案（宿主已本地化）。

- ChatView WithSpeakers(ChatSpeakerOf who, ChatKindLabel kindLabel)
  - 多智能体会话的外观解析钩子（单助手程序不必调用）。

- ChatView WithStyle(int st)
  - 排版模式（ChatStyle.Log / ChatStyle.Bubble）。换了模式要整份重排。

- ChatView WithClock(ChatClock c)
  - 时刻格式化钩子（气泡模式的时刻分隔与气泡尾巴小字都走它）。

- ChatView WithBubbleWidth(int mille)
  - 气泡正文的最大宽度（占视口宽的千分比，默认 620 = 六成）。窗口
    很宽时一条短消息拉满整屏并不好读，这个上限就是「一句话最长能
    占多宽」；窗口变窄时气泡仍然按可用宽度收窄，不会溢出。

- int Style()
  - 当前排版模式。

- ChatView WithSpeakerNames(bool on)
  - 别人的气泡上方是否显示发言人名字（群聊场景）。一对一聊天关掉它：
    左边永远只有一个人，名字行只是白占一行高度。

- string Clock(long at)
  - 本机时区下这条消息的显示文本；没有记时间的旧消息返回 ""。

- static string ClockText(long at)
  - 一条消息头行右侧的本机时区时刻（HH:MM）；没有记时间的旧消息返回 ""。

- void SetBusy(bool on)
  - 模型正在回答：记录末尾显示 busy 行。

- LogState Selection()
  - 选区状态（宿主可持久化或复位）。

- SignalInt Scroll()
  - 底端锚定的滚动偏移信号（0 = 贴住最新一行）。

- SignalInt BubbleScroll()
  - 气泡模式的像素滚动偏移信号（0 = 贴住最新一条）。宿主把它接到
    自己的滚动记忆上时用；不接也不影响滚轮与拖动。

- void Reset()
  - 换掉一整份记录（切换会话）：缓存、折叠、选区与滚动全部归零。

- void Invalidate()
  - 下一帧整份重排（文案、字号或钩子变了）。

- string SelectedText()
  - 当前选中的文本（未选中则 ""）。气泡模式下回答气泡选区，
    记录模式下回答行网格选区——宿主两个模式都可能用。

- bool HasSelection()
  - 气泡模式下是否有选区（宿主菜单据此决定「复制」是灰的还是亮的）。

- void ClearSelection()
  - 清空当前选择（气泡/记录两模式通用）。

- override void OnPaint(App app)
  - 绘制：按有无消息在记录网格与空态提示间切换；消费滚轮、做
    增量重排（受时间预算约束），把排好的行、滚动、选区与折叠
    状态交给常驻的 StyledText 呈现。

- ChatView OnAvatarClick(ChatAvatarClick cb)
  - 宿主注册「气泡头像被点」回调；链式返回自己。

- void AvatarHit()
  - 点击命中测试：只有 bubble 模式响应。点击坐标来自缓存的
    uiApp（事件到达在两帧之间，没有 app 参数），气泡几何与
    PaintBubbles 同一套换算：先滚动态偏移出每条气泡的 y，
    头像在气泡同行一侧（我方在右、对方在左），直径 34 缩放像素。

- ChatHit HitBubble(App app, int mx, int my, bool clamp)
  - 把视口坐标换成「第几条消息、正文第几个字节」。
    `clamp` 为真时越界的 y 收进最近的一条（拖选时指针拖出视口上下的
    常用语义），为假时返回 msg = -1（右键/按键只认真实落点）。

- void SelRange(out int m0, out int o0, out int m1, out int o1)
  - 选择区间归一化后的两端（谁在文档里靠前谁当起点）。

- string BubbleSelectionText()
  - 气泡模式下选中的文本（跨消息拼接，未选中为 ""）。

- bool HasBubbleSelection()
  - 有选区（跨一个字符即算）。

- void ClearBubbleSelection()
  - 清掉气泡选区。

- ChatHit SnapHit(ChatHit h)
  - 起始偏移落在某个字符中间时钳到边界（拖选端点不得切开多字节字符）。

- void BubbleMouse(App app)
  - 气泡模式的鼠标处理：按下起选、移动续选、松开收尾、双击选整条、
    右键把落点交给宿主菜单。滚动拖动与头像点击优先，命中不了正文
    的落点不建立选区。

- void BubbleDoubleClick(App app)
  - 双击选整条（IM 惯例：双击一条消息选中它，右键即可引用/复制）。

- ChatView OnBubbleMenu(ChatBubbleMenu cb)
  - 宿主注册「气泡被右键」回调（复制/引用/转发一类菜单由宿主出）。

- ChatView OnBubbleActivate(ChatBubbleMenu cb)
  - 宿主注册「卡片消息被单击」回调（任务卡片一类整体即入口的消息；
    只对 msg.card = true 的消息生效，见 BubbleMouse）。

- ChatView OnQuote(ChatQuote cb)
  - 宿主注册「引用了一条消息」回调（选中部分或整条）。

- string NameOf(int idx)
  - 消息的显示名（引用块里标「引用谁」用；与气泡上方的发言人同一口径）。

- void TakeBubbleWheel(App app)
  - 指针在气泡区上时滚轮滚动：气泡是像素滚动的，一档滚 40px（约一
    行多一点），而不是记录模式的一整行。

- void TakeWheel(App app)
  - 指针在记录上时滚轮滚动。

- void TakeCopy(App app)
  - 指针在记录上时 Ctrl+C 复制选中的文本。

- void TakeBubbleCopy(App app)
  - 气泡模式下 Ctrl+C 复制选区、Ctrl+A 全选当前会话。
    按键是窗口级事件，不要求指针正悬在控件上（记录模式那条
    旧口径要求鼠标在框内，是当年点选语义的遗留；键盘复制不该
    因为鼠标恰好移开了就失效）。命中区间用本控件的矩形限定：
    一个窗口里叠着多个 ChatView 时不让它们一起响应。

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
  - 列表是否含有 `v`。

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

- static int BubbleTimeGap()
  - 两条消息相隔多久才插一条时刻分隔。5 分钟内连着说的几句算一轮，
    只在轮次开头报一次时间——和 IM 客户端的手感一致。

- static int BubbleFont(App app)
  - 气泡正文的字号。

- string BubbleLayoutSig(App app)
  - 话轮几何的输入签名。它一变，整份重排（行宽全变），因此与
    缓存键分开算：`layoutSig` 记账、`keyNow` 判断这一帧要不要干活。

- void BuildBubble(App app)
  - 把消息流量成一列可变高的气泡盒子（内容坐标）。换行结果缓存在
    `bub` 里，只有签名变了才重算——`TextWrap.Lines` 是逐字符量宽，
    每次绘制都重排一份长会话会把帧预算吃光。

- string SpeakerName(ChatMessage msg)
  - 发言者显示名（多智能体会话走 `speakerOf`，否则用文案里的助手名）。

- string SpeakerInitial(ChatMessage msg)
  - 发言者头像里的首字。

- string InitialOf(string name)
  - 名字首字（与 `StyledText.Initial` 同一套字符边界规则）。

- void PaintBubbles(App app)
  - 画一列气泡：时刻分隔居中、气泡靠发送者一侧、头像在最外侧，
    图片气泡按 contain 铺进盒内。滚动是「离底部多少像素」，
    滚轮与滚动条动的是同一个量。

- static SignalInt bubbleBar;
  - 滚动条要的是「像素、从顶算」，气泡滚动是「像素、从底算」；
    常驻一个转换信号，免得每帧新分配。

- static SignalInt BubbleBarSig()

- static Avatar bubbleAva;
  - 气泡头像复用同一实例：每个可见气泡都要一枚，每帧每气泡新分配
    一个控件正是长会话卡顿的来源（与 StyledText 的头行头像同理）。

- static Avatar BubbleAvatar()

- static string SelectionText(List<TextRun> rows, LogState st)
  - `cache` 中选中的文本（换行拼接，未选中则 ""）。


## ConsoleView (class)

尾部滚动、可选择的控制台视图**控件**：把 LogView 的滚动与
选择状态包进一个标准 retained 控件，于是声明式界面
（.html 设计稿）可以像放 ListView 一样直接声明一个控制台，
宿主只需要绑定行列表。行颜色可选。

- List<string> data;
  - 调用方的行，每帧读取（不拷贝）。

- List<int> colors;
  - 与 `data` 平行的行颜色（0 = 主题默认），可为空列表。

- LogState st;
  - 滚动与选择状态（渲染共用一份，宿主可经 State() 持久化或复位）。

- string Empty;
  - 无行时居中显示的提示（"" 则不绘制）。

- bool live;
  - false 时只绘制，不处理滚轮/选择（嵌入的小日志条）。

- ConsoleView()
  - 构造空视图：未绑定行，活动模式（滚轮/选择可用）。

- override string Kind()
  - 控件类型标识（"ConsoleView"）。

- override string StyleType()
  - 样式类别（沿用 LogView 的 "log"，两者皮肤一致）。

- ConsoleView Bind(List<string> lines)
  - 绑定调用方的行列表（实时视图，不拷贝）。

- ConsoleView WithColors(List<int> cols)
  - 绑定与行平行的颜色列表。

- ConsoleView EmptyText(string text)
  - 无行时的提示文案（"" 不显示）。

- ConsoleView Passive()
  - 只读日志条：不接受滚轮与拖拽选择。

- LogState State()
  - 视图的滚动/选择状态，供宿主持久化或复位。

- void Reset()
  - 跳回最新一行并清除选择。

- string SelectedText()
  - 当前选中的文本（无选择时为 ""）。

- override List<PropSpec> Props()
  - 覆写：设计器属性（空态文案与只读开关；数据经 Bind 在代码侧给）。

- override void OnPaint(App app)
  - 绘制：整体委托给 LogView.Render（含滚轮、尾部滚动与选择）。


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
  - `weight` 是交叉轴的初始占比（各组具体化尺寸前生效）。


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

- int stripSentinelSeq;
  - 触摸长按开菜单的哨兵一次性闸门：EventKind 跨帧滞留
    （动画帧回读旧值），哨兵若每帧都补记会把 longPressFired
    反复清零，释放帧就再也读不到"刚长按过"。按事件序号
    只在真正的新按压帧记一次。

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
  - 空布局：无面板、无分组，尺寸未定（首帧取默认值）。

- DockPanel Find(string id)
  - 按 id 查面板；未注册为 null。

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
  - `side` 侧最后一个组（无组为 null）——Register/Show 的并入目标。

- void SetWeight(string id, int weight)
  - 为包含 `id` 的组设置其所在侧交叉轴的初始占比
    （相对于该侧其他组）。用户拖动过手柄或恢复过保存的布局后
    此设置将被忽略。

- void SetSideExtent(int side, int px)
  - 设置某一侧的像素尺寸（左右为宽度，底部为高度）。

- bool Visible(string id)
  - 面板当前是否可见（未隐藏且上次布局分到了非零内容区）。

- Rect Content(string id)
  - 上一次 Begin() 解析出的内容矩形。隐藏（或未激活）的
    面板得到零尺寸矩形，调用方可以用
    单个 `if (r.width <= 0) { }` 守卫跳过渲染，或直接画到空处。

- Rect ContentParked(string id)
  - 与 Content 类似，但隐藏面板得到一个停在屏幕外远处的 1x1 矩形，
    而不是零尺寸矩形。始终渲染（并注册命中区域）的即时模式内容
    可以不加守卫：它只是画在屏幕外而已。

- string Title(string id)
  - 面板标题（未注册为 ""）。

- void SetTitle(string id, string title)
  - 重命名面板标题（例如跟随 UI 语言切换）。

- void SetIcon(string id, string icon)
  - 设置面板 Ribbon 开关显示的图标。

- void SetTip(string id, string text)
  - 描述面板用途；其 Ribbon 开关悬停时显示该描述。

- void SetSwitchHints(string hideHint, string showHint)
  - 附加到每个开关提示的本地化"点击作用"说明。

- string SwitchTip(DockPanel p)
  - 开关的悬停提示：面板描述与「点击会隐藏/显示」说明按行拼接
    （缺哪段就省哪段）。

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
  - 按 key 找组的下标；不存在为 -1。

- void Detach(string id)
  - 把面板移出所在组并把它选中后的活动标签修好；组空了就解散。

- void ShowOn(string id, int side)
  - 重新停靠已关闭的面板：若 `side` 上有打开的组则并入其中，
    否则在该侧新建组。同时选中该面板。

- bool IsHidden(string id)
  - 面板是否处于关闭（隐藏）状态；未注册视为隐藏。

- void Toggle(string id)
  - 面板的显示/隐藏开关——"面板"工具栏按钮所驱动的内容。

- void Show(string id)
  - 把已关闭的面板放回关闭前的位置：原分组还在就插回原来的
    标签序号，原分组已经空掉被移除就在同侧原来的位置重建它。

- int IndexIn(DockGroup g, string id)
  - `id` 在组内的标签序号（不在组内为 0）。

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
  - 物理像素 → 逻辑像素（按 dpiScale 折算，用于序列化，跨 DPI 搬运）。

- int PersistPhysical(int logical)
  - 逻辑像素 → 物理像素（LoadLayout 恢复时按当前 DPI 折算回）。

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
  - `id` 在 `gs` 这组布局里的所在侧；找不到为 Left。

- static bool ListHas(List<string> xs, string v)
  - 列表是否含有 `v`。

- static bool HasPrefix(string s, string p)
  - 前缀测试。

- static string After(string s, int n)
  - 去掉前 n 个字符；不足 n 时为 ""。

- static List<string> SplitOn(string s, string sep)
  - 按单字符分隔符拆分，丢弃空段。

- bool SideHasVisible(int side)
  - 该侧是否有任一可见分组（决定布局时该侧是否占空间）。

- bool GroupHasVisible(DockGroup g)
  - 组内是否有任一未隐藏的面板。

- List<DockGroup> VisibleGroups(int side)
  - 该侧含可见面板的分组（按列表顺序）。

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
  - 注册命中区前先按组的横向裁剪收拢矩形（标签条可能被裁出组外）；
    完全落在裁剪外时不注册。

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
  - `zone` 所指那一半的矩形（新建组的放置提示）。

- static int SideForZone(int zone)
  - 边缘带 → 宿主侧：1=左、2=右、4=下，其余（含顶带）归中央。

- Rect SideStrip(App app, int side)
  - 某一侧将占据的条带，用作侧边停靠的放置提示。

- int SideIndexOf(DockGroup g)
  - `g` 在其所在侧各组中的位置。

- int SideCount(int side)
  - 该侧当前的分组数（新组排在末尾用）。

- int InsertPos(int side, int k)
  - 新组成为 `side` 的第 `k` 组时的全局索引。

- void ApplyDrop()
  - 松开时执行放置：dropKind=1 把面板并入目标组（目标组若因
    Detach 解散则中止）；dropKind=2 在 `dropSide` 第 `dropIndex`
    处新建组——但该侧原本只剩被拖面板自己那一个组时退化为
    重排序，不拆组。两种都会更新面板 home 并置 changed。

- int IndexOfKey(int key)
  - 按 key 找组（ApplyDrop 在 Detach 后重找目标组用）；无则 -1。

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
  - 图标默认 "grid"，初始停靠边为左侧。


## DockSide (class)

具名停靠边，布局中写 `DockSide.Left()` 而不是魔法整数。
Left/Right 是通高列，Bottom 是中央区域下方的条，
Center 是其他所有面板围绕停靠的区域。

- static int Left()
  - 左侧通高列。

- static int Right()
  - 右侧通高列。

- static int Bottom()
  - 中央区域下方的横向条。

- static int Center()
  - 其他面板围绕停靠的中央区域。


## EditorPalette (class)

代码编辑器语法高亮及其
弹出组件（右键菜单、自动补全、悬停提示、引用列表）的语义颜色。
语法与选区颜色跟随当前皮肤（亮/暗
背景）；弹出组件在任何皮肤下都保持经典暗色调色板，
确保浮层在任意编辑器背景上都清晰可读。

- int keyword;

- int type;

- int str;

- int comment;

- int number;

- int plain;

- int selection;

- int popupBg;

- int panelBg;

- int hoverBg;

- int border;

- int selBg;

- int accent;

- int signature;

- int menuText;

- int text;

- int textSel;

- int muted;

- int lineNo;

- int link;

- int iconMethod;

- int iconType;

- int iconSnippet;

- int iconEnum;

- int iconDefault;

- int iconDisabled;

- int iconGlyph;

- int iconGlyphDisabled;

- int error;

- int errorDim;

- int errTipBg;

- int errTipText;

- int warn;

- int warnLineBg;

- int changeLabel;

- int caretSecondary;

- int minimapBg;

- int minimapThumb;

- int scrollThumb;

- int shadowSoft;

- int shadow;

- int shadowMed;

- int shadowStrong;

- static EditorPalette Dark()
  - 经典暗色调色板（VS 风格）。

- static EditorPalette Light()
  - 浅色背景语法，弹出组件保持不变。

- static EditorPalette For(Gui.Theme t)
  - 根据皮肤背景亮度选择对应的调色板。

- static EditorPalette Preset(int idx)
  - 编辑器配色主题选择器使用的命名预设语法调色板，基于
    暗色 chrome。idx：0 = Dark+（默认），1 = Monokai，2 = Dracula，
    3 = Solarized Dark，4 = Night Owl，5 = High Contrast。

- static EditorPalette PresetFor(int idx, bool lightBg)
  - 命名预设随皮肤背景亮度的变体：暗色背景沿用经典暗色预设；
    浅色背景返回同色系加深、白底上可读的浅色变体。没有这一层，
    宿主把暗底预设推给编辑器后，亮色皮肤的白底上画的就是
    浅灰/亮蓝的暗底文字——刺眼且不可读。
    浅色变体的每个角色色在白底上的对比度都不低于 4.5:1（WCAG AA
    正文），因此同色系里偏亮的青/黄/绿需要压暗后才能上白底。
    idx 含义见 `Preset`。

- static EditorPalette Chrome()
  - 两种调色板共用的暗色弹出组件。


## FileFilter (class)

一个底部扩展名过滤器：显示标签和其以 ";" 分隔的模式
（"" 匹配所有文件）。用单个实体取代并行的标签/模式列表。

- string label;

- string pattern;

- FileFilter(string label, string pattern)


## FilePicker (class)

可复用的应用内路径选择器，承载于独立 GUI 窗口。

- static string lang="zh";

- [DllImport("zan_gui")]static extern string zan_gui_android_files_dir();
  - Android 上应用私有 files 目录（SDL Activity 持有，经驱动导出）。
    进程 cwd 是 "/" 且列根目录被 SELinux 拒绝，这里作为默认浏览起点。

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

- bool inlineMode;
  - Android 降级标志：SDL 只有一个 activity 窗口，第二个 OS 窗口
    创建后不可见，对话框永远弹不出来。inlineMode=true 时选择器改由
    宿主窗口内的全屏模态承载（RenderOverlay），事件也走宿主，
    OwnsWindow 恒 false。

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

- List<string> selectedPaths;
  - 多选模式（BeginOpenFileMulti / BeginOpenFileFiltersMulti）：
    Ctrl+点击把文件行加入/移出本集合，Shift+点击从锚点行扩展；
    确认后经 SelectedPaths() 读回（升序、与列表同序）。单选模式
    恒空，行为与历史版本完全一致。

- int anchorIdx;
  - Shift+点击范围选择的锚点（列表索引，-1 = 无锚点）。

- bool allowMulti;

- int maxSelect;
  - 多选数量上限（0 = 不限）：Ctrl+点击加选与 Shift+点击范围扩展
    在达到上限后拒绝新增并提示；窗口内状态区实时显示「已选 x/N」。

- string statusText;

- string lastFilter;

- string lastDir;
  - 上一次浏览的目录：Begin 未给 initial 时从这里续起，生命周期内
    连续打开不再每次回到默认目录。Begin 解析出有效目录与 Navigate
    成功时更新。

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
  - 双语文案改走键式查找（System.Globalization.Lang）：英文原文
    作键、内置中文为缺省串。宿主已 Lang.LoadDir 时语言包整句
    覆盖；未加载（code 空）回退缺省串，行为与之前一致。

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

- void BuildTree(string dir, int depth, List<FilePickerTreeNode> nodes)

- void Begin(int pickerMode, string initial, string extension, string defaultName, string dialogTitle)

- void BeginFolder(string initial, string dialogTitle)

- void BeginOpenFile(string initial, string extension, string dialogTitle)

- void BeginOpenFileFilters(string initial, List<FileFilter> items, string dialogTitle)
  - 带显式扩展名过滤器集合的打开文件对话框。

- void BeginOpenFileMulti(string initial, string extension, string dialogTitle)
  - 多选打开文件对话框：Ctrl+点击切换选中，Shift+点击范围选择，
    确认后经 SelectedPaths() 读回全部选中路径。

- void SetMaxSelect(int n)
  - 多选数量上限（0 = 不限）。达到上限后再点新行拒绝加入并在
    状态区提示；宿主（如 Upload 的 Max）用它把上限透传进弹窗。

- int MaxSelect()
  - 当前多选数量上限（0 = 不限）。测试钉语义用。

- void BeginOpenFileFiltersMulti(string initial, List<FileFilter> items, string dialogTitle)
  - 多选 + 显式扩展名过滤器集合的打开文件对话框。

- static string FirstExt(string pattern)
  - ";" 分隔模式中的第一个扩展名（模式为空则为 ""）。

- static bool ContainsText(string text, string needle)
  - 名称过滤框的不区分大小写子串测试（"" 匹配所有）。

- static string FilterTokenToSuffix(string ext)
  - 模式 token → 可做字面后缀比较的形态：剥掉通配 "*"
    （"*.png"→".png"、"png"→".png"、"."→"."）。EndsWith 是字面
    比较，"*" 留在里面会让每个文件都匹配不上——过滤器一选
    「接受的文件」列表就全空，即此根因。

- bool MatchesCurrentFilter(string name)
  - `name` 通过当前所选过滤器时为 true。空模式
    （所有文件）匹配一切；否则 ";" 分隔的
    任一扩展名匹配即可。

- bool PatternMatches(string name, string pattern)
  - ";" 分隔模式的任一 token 命中即 true；空模式匹配一切。

- void BeginSaveFile(string initial, string extension, string defaultName, string dialogTitle)

- void ApplySkin(int index)

- int SkinIndex()
  - 当前皮肤索引（0 = 默认暗色）。测试与宿主同步检查用。

- bool IsOpen()

- bool WindowModeOpen()
  - 弹窗是否以独立 OS 窗口承载。inlineMode（Android/无第二窗口环境）
    时恒 false：OS 窗口从未创建，内容随宿主帧经 RenderOverlay 绘制，
    宿主事件泵不应为它改变轮询/阻塞节奏（对照 IsOpen，后者在两种
    模式下都为 true）。

- bool NeedsRedraw()

- bool OwnsWindow(nint hwnd)

- nint PopupWindowHandle()
  - 弹窗的原生窗口句柄（未开为 0）。宿主焦点管理与路由测试用；
    非属主判定请用 OwnsWindow（0 不与任何真实句柄冲突）。

- string SelectedPath()

- List<string> SelectedPaths()
  - 多选确认后的全部选中路径（升序、与列表展示同序）。单选模式
    返回空列表；单选宿主请继续读 SelectedPath()。

- string LastDirectory()
  - 生命周期内记住的上一次浏览目录（Begin 未传 initial 时续用）。
    测试钉住「连续打开不再回到默认目录」的语义。

- string CurrentDirectory()

- void RaiseFilterChanged()

- void Finish(int action)

- bool InMultiSelection(int idx, List<FilePickerItem> items)
  - `idx` 行在多选集合里时为 true。列表行每帧重建（items
    是 RenderContent 的局部量），这里按路径字符串比较——多选集合
    存的本来就是路径。

- void RemoveSelectedPath(string path)
  - 从多选集合摘除一条路径（List<string> 无 Remove 成员，
    手写摘除保持索引连续）。

- void Close()

- bool ApplyEvent(nint evHwnd)

- void SweepClosed()
  - 程序化 Close（Finish(2)）留下的 popup 引用在这里收尾：宿主在
    TakeResult != 0 的那一轮（弹窗刚关闭）调用一次，销毁 OS 窗口并
    清空引用，OwnsWindow 随即回到 false。用户点叉的路径走
    ApplyEvent 的 kind 8 分支，不需要这步。

- int TakeResult()

- void Navigate(string dir)

- void RenderWindow()

- void RenderOverlay(App host)
  - inlineMode（Android/无第二窗口环境）的绘制入口：宿主在自己的
    渲染回调里（页面之上、每帧一次，Layer.Render 同款时机）调用。
    先铺变暗遮罩并吞掉其下命中，再复用 RenderContent 的即时模式
    主体——它按传入 app 的画布尺寸与 ContentTop 自适应，全屏承载。
    桌面（窗口模式）本函数是 no-op，宿主可以无条件调用。

- void RenderContent(App app)


## FilePickerItem (class)

列表条目实体（名称 + 路径 + 类型 0:文件 1:文件夹），保证数据一致性。

- public string name;

- public string path;

- public int kind;

- public FilePickerItem(string name, string path, int kind)


## FilePickerTreeNode (class)

目录树节点实体（路径 + 缩进层级），保证数据一致性。

- public string path;

- public int depth;

- public FilePickerTreeNode(string path, int depth)


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
  - 调用方命名的稳定 id；deps 引用与 SelectId/IndexOfId 都用它。

- string title;
  - 卡片标题文本。

- string tag;
  - 负责人之类的短标签，画在标题下方。

- string tagClass;
  - 上面那个标签的语义类（`primary`/`success`…），由皮肤取色。

- string state;
  - 状态短语（进行中 / 待办 / 阻塞…）。

- string stateClass;
  - 状态短语的语义类（`primary`/`success`…），由皮肤取色。

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
  - 构造节点：id/title 必填；progress = -1（不显示进度条），
    未完成、无前置。

- GraphNode After(string depId)
  - 声明一个前置节点。

- GraphNode WithTag(string text, string cls)
  - 设置负责人短标签及其语义类（链式）。

- GraphNode WithState(string text, string cls)
  - 设置状态短语及其语义类（链式）。

- GraphNode WithProgress(int pct)
  - 设置进度（0..100；负数隐藏进度条）。链式。

- GraphNode WithPayload(string p)
  - 附带调用方 payload（图不解释其内容）。链式。

- GraphNode Finished()
  - 标记完成（后继节点据此解锁）。链式。


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
  - 构造内部状态：空数据、未选中、默认卡片模板与四个交互事件。

- GraphView()
  - 默认构造：节点卡片用内置标准控件拼装。

- GraphView(GraphCardOf template)
  - 自定义节点卡片。

- override string Kind()
  - 控件类型标识（序列化/设计器用）。

- override string StyleType()
  - 覆写：CSS 类型选择器名（"graph"）。

- override List<PropSpec> Props()
  - 覆写：返回设计器属性清单（empty）。

- override List<string> Events()
  - 覆写：公共事件之外提供 "Select"/"Activate"/"Blocked"/"Context"。

- override void BindEvent(string evt, Action a)
  - 覆写：四个语义事件各挂对应 UiEvent，其余按名称走通用路由。

- override void BindEventS(string evt, ControlEvent a)
  - `BindEvent` 的 sender 通道（S = Sender）：
    特化事件接本控件的 UiEvent，通用事件按名路由。

- GraphView Bind(List<GraphNode> nodes)
  - 绑定调用方的节点列表；不复制任何内容。

- void Refresh()
  - 节点内容变了：下一帧重建卡片。

- GraphView BindSel(SignalInt s)
  - 共享调用方的选择信号，供在列表重建后保持选择的宿主使用。

- GraphView EmptyText(string text)
  - 设置空图提示文案（链式）。

- GraphView OnSelect(Action a)
  - 订阅选中/激活/拦截/右键事件（链式；组文档覆盖以下四个）。

- GraphView OnActivate(Action a)
  - 订阅激活事件：双击未锁定节点时触发（锁定节点走 Blocked）。

- GraphView OnBlocked(Action a)
  - 订阅拦截事件：双击锁定的节点时触发，由宿主提示还差哪一步。

- GraphView OnContext(Action a)
  - 订阅右键事件：右键按下节点时触发。

- int Count()
  - 节点总数。

- bool Has(int row)
  - 行号是否在节点列表范围内。

- GraphNode NodeAt(int row)
  - 第 row 行的节点（越界行为未定义，调用方先用 Has 判断）。

- List<GraphNode> Nodes()
  - 底层节点列表（调用方的引用，未复制）。

- int SelectedIndex()
  - 当前选中行号；未选中为 -1。

- bool HasSelection()
  - 是否有有效选中。

- GraphNode Selected()
  - 选中的节点；仅在 HasSelection() 时有效。

- int ContextRow()
  - 事件处理器涉及的节点行号与指针位置。

- int ContextX()
  - 最近一次节点点击/右键记录的指针 X（app 坐标）。

- int ContextY()
  - 最近一次节点点击/右键记录的指针 Y（app 坐标）。

- int IndexOfId(string id)
  - 按 id 查节点行号；空 id 或未命中返回 -1。

- void SelectId(string id)
  - 按 id 选中节点，并触发 Select（宿主恢复上次选择时用）。
    空 id ＝ 取消选择：宿主离开某个节点（回到全局视图）时，图上不该
    还高亮着它。

- void Deselect()
  - 取消选择。不触发 Select（没有节点可读），只清掉高亮。

- static int IndexOf(List<GraphNode> ns, string id)
  - 按 id 线性查找行号；空 id 或未命中返回 -1。

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
  - 是否使用调用方提供的自定义卡片模板。

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
  - 节点卡片间距（皮肤 graph 件的 gap，缺省 gapLarge）。

- int CardWidth(App app, int areaW)
  - 所有卡片统一宽度：图要读的是层次，宽度参差不齐会让同层
    的卡片看着像不同的东西。取最宽的那张，再收进可用宽度。

- int CardHeight(App app)
  - 所有卡片统一高度：取最高的那张（下限 48px 的缩放值）。

- int Layout(App app, int areaX, int areaY, int areaW)
  - 按层排布，写回每个节点的矩形，返回内容总高度。`areaX/areaY`
    是内容区左上角（已含滚动偏移）。

- override void OnMeasure(App app)
  - 覆写：按需重建节点卡片后量出流式布局的首选高度。

- override void OnPaint(App app)
  - 覆写：画表面、空态提示，流式摆放节点卡片与连线并在溢出时
    画纵向滚动条，处理滚动与节点命中。

- int ScrollOffset(App app, int contentH)
  - 处理滚轮并把滚动位置钳在内容范围内，返回当前偏移。

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
  - 空图提示（Empty 为空串时不画）。


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
  - 取消选择（保留滚动位置）。

- bool HasSelection()
  - 选择至少跨越一个字符时为 true。

- void Reset()
  - 回到最新一行并取消选择。


## LogView (class)

尾部滚动、可选择的文本日志（控制台视图）：最新一行位于
底部，滚轮和可拖拽的滚动条可回溯历史，
指针可跨行选择文本。行颜色由
调用方提供，视图无需知道自己在记录什么。

- static int RowHeight(App app)
  - 每行高度（逻辑像素，绘制时按 DPI 缩放）。

- static int Padding(App app)
  - 视图四周留白（逻辑像素）。

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


## PropertyGrid (class)

属性表：把一组 PropSpec 画成可编辑的属性行，值直接读写 spec 绑定
的字段 / signal。行的标签、编辑器、滚动、裁剪都由控件自己完成，
宿主只提供 spec 列表：

grid.Bind("Field:" + sel.fname, this.FieldSpecs(sel));
grid.RenderInside(app, new Rect(x, y, w, h));

编辑器按 PropSpec.kind 选：0/4 文本 Input，1 整数 Input，2 Switch，
3 SelectBox，5 分组标题（PropSpec.Section，只有文字没有编辑器）。

- List<PropSpec> data;
  - 当前绑定的属性行（Bind 每帧灌入，不拷贝）。

- List<Input> texts;
  - 每行一个编辑器（按行索引对齐，未用到的槽为 null）。
    编辑器由属性协议驱动（GetProp/SetProp/GetText/SetText），
    不再持有任何外部信号——动态宿主没有编译期字段可绑。

- List<Switch> flags;
  - 开关行的编辑器（按行索引对齐，非开关行为 null）。

- List<SelectBox> picks;
  - 下拉行的编辑器（按行索引对齐，非下拉行为 null）。

- List <List<int>> pickSegIds;
  - 选项少于等于 4 个时的分段单选按钮稳定控件 id。

- List<int> minusIds;
  - 整数属性行的 -/+ 步进按钮稳定控件 id（按行索引对齐，
    非整数行也占位）。

- List<int> plusIds;

- List<string> shadow;
  - 上一帧的值：用来判断这一帧是外部改了模型还是用户改了编辑器。

- string key;
  - 当前绑定对象的标识，变化时重建编辑器。

- SignalInt scroll;
  - 纵向滚动偏移（像素）。

- public int LabelWidth;
  - 自定义标签列宽（逻辑像素，0 = 自动根据内容自适应）。

- string Empty;
  - 没有属性时显示的文本（"" 不画）。

- UiEvent Change;
  - 某一行被用户改动后触发（读 ChangedKey 得知是哪一个）。

- int hitRow;
  - 最近被用户改动的行号（-1 = 尚无改动；Rebuild 后复位）。

- PropertyGrid()
  - 构造空属性表：未绑定任何行，空态文本为 ""（不画）。

- override string Kind()
  - 控件类型标识（"PropertyGrid"）。

- PropertyGrid OnChange(Action a)
  - 订阅属性改动事件（回调里读 ChangedKey/ChangedRow）。

- int ChangedRow()
  - 最近被用户改动的行号 / 属性键。

- string ChangedKey()
  - 最近被用户改动的属性键（无效时为 ""）。

- void Bind(string k, List<PropSpec> specs)
  - 绑定一组属性。`k` 标识当前对象（如 "Field:btn0"）：它变化时
    才重建编辑器，否则沿用现有的，输入焦点和光标不会每帧丢失。

- void Rebuild()
  - 强制丢弃现有编辑器并按当前 specs 重建（切对象后 Bind 会自动调）。

- int IndexOfValue(PropSpec p, string val)
  - 枚举值（可能存的是序号，也可能是选项文本）对应的选项下标。

- string PickValue(PropSpec p, int idx)
  - 枚举行当前选择在模型里的字符串形式：绑定到 int 的存序号，
    绑定到字符串的存选项文本。

- static bool IsNumber(string s)
  - 整数属性编辑器判可解析（允许前导 -）。

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
  - 单行高度（逻辑像素，绘制时按 DPI 缩放）。

- int ContentHeight(App app)
  - 全部行的高度（宿主据此在自己的滚动区里给出槽位）。

- override void OnMeasure(App app)
  - 布局测量：首选高度 = 全部行高之和。

- override void OnPaint(App app)
  - 绘制：处理滚轮并钳制滚动偏移，逐行画标签与编辑器
    （整数行带 -/+ 步进按钮），每行做一次双向同步；
    悬停行有说明时请求气泡提示，内容溢出时画纵向滚动条。

- override List<string> Events()
  - 控件事件清单：公共事件之外提供 "Change"。

- override void BindEvent(string evt, Action a)
  - 绑定事件："Change" 接 UiEvent，其余按名称走通用路由。

- override void BindEventS(string evt, ControlEvent a)
  - `BindEvent` 的 sender 通道（S = Sender）：
    特化事件接本控件的 UiEvent，通用事件按名路由。


## Ribbon (class)

彩色低多边形飘带背景动画 —— 经典网页彩带（ribbon.js）的移植，
作为 Gui 组件库的一员供应用直接声明使用：

Ribbon ribbon = new Ribbon(3);
// 每帧一次：
ribbon.Render(app, x, y, w, h, 40);

每条飘带预生成一串三角形分段（p1/p2/p3 顶点），整体从矩形
一侧流向另一侧；每个分段按 sin(phase) 淡入淡出，相位追上后
整条飘带结束、补一条新飘带，保持恒定条数。组件自包含：
按 App 帧时钟推进相位（与调用频率无关），绘制始终裁剪在
矩形内部（顶点越界安全），尺寸变化时按新尺寸重新生成，
并自行重排动画帧——调用方不需要管理计时或重绘。
alphaPercent 建议取 26~90（对应原版 colorAlpha 0.1~0.35）。

- class Sec
  - 单个分段：三角形 p1->p2->p3，phase 0..2π 驱动 sin 透明度。

- List <List<Sec>> ribbons;

- Random rng;

- int ribbonCount=3;

- int colorSat=80;

- int colorLum=60;

- int colorCycle=6;

- double alpha=0.25;

- double horizSpeed=200;

- double speed=1.6;

- int w;

- int h;

- int lastMs=-1;

- public Ribbon(int bandCount)
  - 构造：bandCount 条飘带。

- public void SetSpeed(double factor)
  - 选择飘带速度倍率（1 = 原版网页步进；默认 1.6 更快）。

- public void Render(App app, int x, int y, int w, int h, int alphaPercent)
  - 每帧声明一次：推进动画并绘制在 [x,y,w,h] 内部，
    随后自行续接动画帧。调用方先铺好自己的不透明底色。

- List<Sec> NewRibbon()
  - 预生成一条完整飘带（从左/右边缘流向另一侧）。

- bool RibbonDone(List<Sec> ribbon)
  - 飘带是否全部淡出。

- void DrawSection(Canvas c, Sec s, int ox, int oy)
  - 画一个分段：纯绘制（相位推进在 Render 中按时间统一做）。

- static void FillTriangle(Canvas c, int x0, int y0, int x1, int y1, int x2, int y2, int color)
  - 扫描线填充三角形（半透明色逐行 FillRect，跨行重心插值）。

- static int Hsl(int hh, int ss, int ll)
  - HSL(0..360, 0..100, 0..100) -> 0xRRGGBB。


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
  - 顶部搜索框（文本同时供宿主过滤自己的数据用）。

- SignalInt scroll;
  - 列表滚动偏移（像素）。

- Button newBtn;
  - 顶部"新建"按钮。

- string emptyText;
  - 无行匹配当前搜索时居中绘制的文本（"" = 不显示）。

- bool showArchived;
  - 底部开关：归档行在开启前保持隐藏。

- string archiveTip;
  - 归档框的悬停提示（"" = 无）。

- string deleteTip;
  - 删除叉号的悬停提示（"" = 无）。

- string archivedLabel;
  - 底部归档开关的标签（如 "Archived"）。

- string renameTip;
  - 重命名框的悬停提示（"" = 无）。

- int editing;
  - 行内重命名：正在编辑的行（-1 表示无）及其字段。从第一条消息
    推导出的标题只是猜测；用户有权决定
    聊天叫什么。

- Input rename;
  - 行内重命名输入框。

- bool takeFocus;
  - 重命名框刚打开的那一帧把焦点交给它。

- SessionList(string searchHint, string newLabel)
  - `searchHint` 搜索框占位文本；`newLabel` "新建"按钮文案。

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
  - 每行高度（逻辑像素，绘制时按 DPI 缩放）。

- static int Fold(int b)
  - 对 UTF-8 字节的不区分 ASCII 大小写的子串测试：只有 A-Z 折叠，
    因此中文标题精确匹配，拉丁查询忽略大小写。

- static bool CiContains(string hay, string needle)
  - ASCII 大小写不敏感的子串测试（`Fold` 折叠后比对）。

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
  - 无任何交互发生（所有索引 -1、created=false）。

- bool Any()
  - 本帧有交互需要宿主处理时为 true。


## ChatSpeaker (delegate)

把成员 id 解析成外观；返回 null 表示「不认识这个 id，按默认助手画」。

`delegate ChatSpeaker ChatSpeakerOf(string agent);`


## Control (delegate)

为一个节点构建卡片控件，供节点长相由调用方决定的图使用。

`delegate Control GraphCardOf(GraphNode node);`


## string (delegate)

把协议节拍（`ChatMessage.kind`）翻成头行右侧的状态标签。

`delegate string ChatKindLabel(string kind);`


## string (delegate)

把消息时刻翻成要显示的文本（气泡模式下是分隔时刻与气泡尾巴上的小字）。
给了它就由宿主决定格式（含时区与本地化）；不给时组件用本机时区的 HH:MM。

`delegate string ChatClock(long at);`


## void (delegate)

气泡头像被点击：告诉宿主命中的是 `data` 里的第几条消息
（宿主据此弹出发言人的资料面板之类的名片界面）。

`delegate void ChatAvatarClick(int msgIndex);`


## void (delegate)

一条消息被「引用」：宿主拿到消息下标与要引用的正文（用户选中了
一段就是那一段，未选中就是整条）。IM 客户端据此把引用块塞进
输入框，发送时把被引消息 id 一并带给服务端，对端才看得到引用关系。

`delegate void ChatQuote(int msgIndex, string text);`


## void (delegate)

气泡上按下右键：宿主弹自己的菜单（复制 / 引用 / 转发…）。
组件不带任何菜单文案——文案与业务动作都是宿主的事，
这里只把「右键了哪一条、当前选中了什么」告诉宿主。

`delegate void ChatBubbleMenu(int msgIndex, string selText);`
