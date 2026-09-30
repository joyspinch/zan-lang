# Game.Scene

> 源码: `packages/Zan.Game/src/Game/Scene/SceneDesigner.zan`, `packages/Zan.Game/src/Game/Scene/SceneDoc.zan`, `packages/Zan.Game/src/Game/Scene/SceneView.zan`


## Anchor (class)

以千分比（0..1000）表示的九宫格锚点预设，500 为中心。
元素的锚点同时决定画布上的参考点和元素自身的轴心：
(0,0)=左上角、(1000,1000)=右下角、(500,500)=与分辨率
无关的完美居中。

- static int Min()
  - 锚点常量：最小值（左/上边缘），返回 0。

- static int Mid()
  - 锚点常量：中心，返回 500（千分比）。

- static int Max()
  - 锚点常量：最大值（右/下边缘），返回 1000。


## ScaleMode (class)

作者定义的设计分辨率如何映射到实际窗口/后备缓冲区。
取值：Fit 等比缩放信箱式（保持宽高比，可能黑边）、Fill 等比
缩放覆盖式（保持宽高比，可能裁边）、Stretch 非等比完全填充
（可能变形）、None 1:1 不缩放（设计像素等于屏幕像素）。

- static int Fit()
  - 缩放模式常量：Fit（等比信箱式），返回 0。

- static int Fill()
  - 缩放模式常量：Fill（等比覆盖式），返回 1。

- static int Stretch()
  - 缩放模式常量：Stretch（非等比拉伸填满），返回 2。

- static int None()
  - 缩放模式常量：None（1:1 不缩放），返回 3。


## SceneDesigner (class)

`.zscene` 游戏窗口/场景文档的可视化编辑器，由
Zan IDE 承载，方式与 Gui.Designer 承载 `.zform` 相同。与表单设计器
（面向工具的停靠/锚点/流式布局）不同，这是自由画布：元素按
设计空间绝对坐标放置，可选背景
参考图，并相对于九宫格锚点定位，使 HUD 在不同
分辨率下保持布局。游戏 UI 位于一个主窗口内，因此
整个场景是单一画布而非多个子窗口。

面板：游戏控件（标签、按钮、图片/动画框、进度条、
以行 x 列网格组出现的格子族、头像、复选框、选项卡、容器、
小地图、滑块、滚动条、下拉框、输入框、列表框）以及绘制
组件（文本、美术文本、精灵、矩形、圆形、动画）。拖动移动，
拖角落手柄缩放，吸附网格，在
检查器中编辑属性、调整 z 顺序、复制，然后保存写出 `.zscene` JSON。

- static string lang="zh";
  - UI 语言码（"zh" / "en"）；IDE 根据其界面语言设置。

- static string assetsDir;
  - 项目的资源目录；IDE 设置它，使检查器的资源选择器能浏览打包文件。"" = 未打开项目。

- SceneDoc doc;
  - 正在编辑的场景文档（设计器的数据源与保存目标）。

- SceneElement sel;
  - 当前主选中元素；null 表示无选中。

- SceneElement lastSel;
  - 上一次的主选中元素，用于检测选中变化以同步检查器。

- bool hostPanels;
  - 宿主（IDE）把元素面板和属性面板挂在自己的 dock 里时置位：设计器本体只留功能区 + 画布。

- bool dragging;
  - 交互状态标记：正在拖动元素（resizing 为正在缩放）。

- bool resizing;

- int resizeH;
  - 当前缩放手柄 0-7（TL、T、TR、R、BR、B、BL、L）。

- string palDrag;
  - 正在从面板拖出的元素种类（"" = 无）。

- bool palMoved;
  - 面板按下后已移出芯片（真正的拖拽）。

- int dragDx;
  - 元素左上角到光标的拖拽偏移（设计空间），dx/dy 为水平/垂直分量。

- int dragDy;

- int guideX;
  - 设计空间中的智能参考线位置（-1 = 无）：guideX 为垂直线，guideY 为水平线。

- int guideY;

- int inpIdName;
  - 检查器输入控件 id（其中一个获得焦点时键盘被忽略）。

- int inpIdImg;

- int inpIdText;

- int inpIdBg;

- int inpIdItems;

- int inpIdLayer;

- int inpIdTitle;

- int inpIdClick;

- List<SceneElement> selExtra;
  - 额外的多选元素（Ctrl+点击 / 框选），不含主选中。

- bool marquee;
  - 正在进行的橡皮筋框选拖拽；mqSX/mqSY 为框选起点（屏幕空间）。

- int mqSX;

- int mqSY;

- bool showTags;
  - 在元素上绘制种类/名称徽标。

- int zoom;
  - 画布缩放千分比；0 = 适应视口（首次渲染后固化为具体值）。

- int panX;
  - 画布平移（屏幕像素，右/下为正，渲染时钳制保画布可见）。

- int panY;

- bool hand;
  - 抓手模式：左键拖动即平移画布（panning 为拖拽进行中，panSX/panSY 为按下位置，panOX/panOY 为按下时平移量）。

- bool panning;

- int panSX;

- int panSY;

- int panOX;

- int panOY;

- List<string> palFolded;
  - 折叠起来的面板分组标题列表。

- int palScroll;
  - 面板内部滚动偏移（像素，0 = 顶部）。

- List<string> undoStack;
  - 撤销/重做历史：整份文档的 JSON 快照。

- List<string> redoStack;

- string clipJson;
  - 复制的元素（单元素场景 JSON，粘贴时重新解析）。

- int resPick;
  - 资源选择弹窗：0 关闭，1 图片，2 背景。

- ListView<string> resList;
  - 资源选择列表：绑定到文件名，拥有其选中项。

- bool ctxOpen;

- int ctxX;

- int ctxY;

- int ctxHover;

- bool showGrid;

- bool snap;

- int grid;

- string msg;

- string editLayer;
  - 正在编辑的层名；"" = 基础层（永远渲染）。其它层元素以幽灵显示且不参与命中。

- bool layerMenuOpen;
  - 层下拉菜单展开状态。

- bool showGhost;
  - 非编辑层幽灵显示开关。

- Input layerIn;
  - 检查器"层"行输入框。

- List<string> DocLayers()
  - 文档里出现过的层名列表（保持首次出现顺序；"" 不含）。

- bool InEditLayer(SceneElement e)
  - e 是否属于当前编辑层（基础层元素只在编辑基础层时可见可点）。

- Input nameIn;
  - 检查器各字段输入框：名称 / 图片 / 文本 / 背景 / 选项 / 窗口标题 / 点击动作。

- Input imgIn;

- Input textIn;

- Input bgIn;

- Input itemsIn;

- Input titleIn;

- Input clickIn;

- static string T(string en, string zh)
  - 选取当前界面语言对应的字符串：键式查找（System.Globalization.Lang），
    英文原文作键、内置中文为缺省串；语言包未加载时回退缺省串。

- static SceneDesigner current;
  - 资源选择器处于打开状态的设计器，使列表处理函数（以方法绑定）能够访问它。

- bool loadError;
  - 上一次 LoadJson 是否失败（文档损坏被拒绝）。宿主据此额外提示；设计器状态栏的 msg 也带原因。

- static void ResourcePicked()
  - 选中的资源写入选择器为之打开的字段；
    第一行表示"无资源"。

- SceneDesigner()
  - 构造设计器：新建 1280x720 的空白场景文档，网格 8px、显示网格并吸附，编辑基础层。

- void LoadJson(string json)
  - 从 JSON 装载场景文档：解析失败时保持空白文档并置 loadError（原因见 SceneDoc.LastError）；成功与否都会清空选中、撤销历史与编辑层。

- string SaveJson()
  - 将当前文档序列化为 `.zscene` JSON 字符串（保存时由宿主写盘）。

- void PushUndo()
  - 记录当前文档；在任何修改前调用。快照栈上限 60 条（超出丢最旧），并清空重做栈。

- void Undo()
  - 撤销：恢复上一个快照；无历史时仅在状态栏提示，不改文档。撤销会清空选中。

- void Redo()
  - 重做：恢复被撤销前的状态；无可重做时仅在状态栏提示。会清空选中。

- static List<string> ControlKinds()
  - 游戏控件种类名列表（label/button/imagebox/skillslot 等 20 种）：构建游戏 HUD/窗口所用的交互控件，用于元素面板与遍历。

- static List<string> ComponentKinds()
  - 绘制组件种类名列表（text/arttext/sprite/rect/circle/anim）：叠加在画布上的非交互图元。

- static string KindLabel(string kind)
  - 种类 kind 的本地化显示名（按 UI 语言返回中文或英文）；未知种类原样返回。

- static bool IsSlotKind(string kind)
  - kind 是否为网格槽位类（skillslot/quickslot/bagslot/itemslot），转交给 SceneView.IsSlotKind 判断。

- static void ApplyDefaults(SceneElement e, string kind)
  - 每种元素给出合理的初始尺寸/着色/网格，使新建元素在画布上立即可辨。

- static bool InRect(int mx, int my, int x, int y, int w, int h)
  - 点 (mx,my) 是否位于矩形 (x,y,w,h) 内（含左/上边界，不含右/下边界）。

- static int PackColor(int r, int g, int b, int a)
  - 打包 RGBA 四分量（各 0-255）为单个颜色整数。

- static List<string> SplitItems(string items)
  - 按 ';' 拆分选项字符串为列表（转交 SceneView.SplitItems）。

- int DesignLeft(SceneElement e)
  - 元素在设计空间的左上角 X（Y 见 DesignTop）。锚点按千分比对照设计画布解析，与 SceneDoc.Resolve 的 None 模式/无缩放一致。

- int DesignTop(SceneElement e)
  - 元素在设计空间的左上角 Y（X 见 DesignLeft）。

- void AddElement(string kind)
  - 在画布中心添加一个 kind 元素：应用默认样式、z 排到最后、加入当前编辑层并选中之；自动入撤销栈。

- void AddElementAt(string kind, int dx, int dy)
  - 在设计空间点 (dx, dy) 处居中添加一个 `kind` 元素——
    面板拖拽的放置目标。

- SceneElement CloneEl(SceneElement s)
  - `s` 的深拷贝（位置偏移两个网格单元，z 为下一个，名字保证唯一，落到当前编辑层）。

- bool UsedName(string nn)
  - nn 是否已被文档里其它元素占用（重名会让编译期投影
    生成重复字段）。空名不算占用（尚未命名）。skip 为
    克隆来源（它自己不算冲突）。

- bool UsedNameBut(string nn, SceneElement skip)
  - nn 是否已被文档里其它元素占用（重名会让编译期投影生成重复字段）。空名不算占用（尚未命名）。skip 为克隆来源（它自己不算冲突）。

- void DuplicateSel()
  - 复制当前选中元素（作为新元素加入并选中之）；无选中时无操作。自动入撤销栈。

- void CopySel()
  - 将选中元素复制到内部剪贴板（单元素场景文档，因此能经受撤销/重做的重新解析）；无选中时仅提示。

- void PasteClip()
  - 粘贴剪贴板中的元素（加入文档、z 排到最后并选中之）；剪贴板为空时仅提示。自动入撤销栈。

- void DeleteSel()
  - 删除选中元素及多选集合中的全部元素并清空选中；无选中时无操作。自动入撤销栈。

- void RaiseSel(int dir)
  - 调整选中元素的 z 顺序：dir 为 +1 上移一层、-1 下移一层；无选中时无操作。自动入撤销栈。

- void SelToTop()
  - 将选中元素的 z 顺序移到最前（SelToTop）/最后（SelToBottom）；无选中时无操作。自动入撤销栈。

- void SelToBottom()
  - 将选中元素的 z 顺序移到最后（SelToTop 的反向操作）；无选中时无操作。自动入撤销栈。

- bool IsExtraSel(SceneElement e)
  - e 是否属于额外多选集合（不含主选中）。

- void ToggleExtraSel(SceneElement e)
  - 将 e 加入/移出额外多选集合（切换）；e 为主选中时无操作。

- void SetDesignPos(SceneElement e, int left, int top)
  - 放置 `e`，使其设计空间左上角位于 (left, top)（按锚点反算 x/y，锚定元素不漂移）。

- void SetTotalSize(SceneElement e, int tw, int th)
  - 调整 `e` 的尺寸，使总占地为 (tw, th)（网格单元数由此得出），并保持设计空间左上角不变；单元尺寸下限 4px。

- void AlignSel(int op)
  - 对多选元素进行快速布局，以主选中元素为基准。op：0 左对齐，1 右对齐，2 上对齐，3 下对齐，4 水平居中，5 垂直居中，6 等宽，7 等高。锁定元素被跳过；无多选时仅提示。自动入撤销栈。

- void Render(App app, int x, int y, int w, int h)
  - 渲染整个设计器（功能区 + 画布 + 面板/检查器/状态栏）到矩形 (x,y,w,h)；使用独立的控件 id 序号段，不污染宿主 id。宿主面板开启时只画功能区与画布。

- void RenderHostPanels(App app, Rect toolRect, Rect propRect)
  - 宿主（IDE）把元素面板和属性面板画进自己的 dock 面板里，
    设计器本体就只剩功能区 + 画布。id 基值和 Render 分开，
    免得两趟互相错位。
    宿主（IDE）把元素面板和属性面板画进自己的 dock 面板里，
    设计器本体就只剩功能区 + 画布。id 基值和 Render 分开，
    免得两趟互相错位。

- void RenderPinned(App app, int x, int y, int w, int h)
  - 渲染设计器本体（Render 的别名入口），沿用宿主传入的同一矩形；宿主面板由 IDE 自行调用 RenderHostPanels 绘制。

- void RenderLayerMenu(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - 层下拉菜单：列出基础层 + 文档里出现过的层名，选择后画布切到那一层编辑。菜单浮在功能区"编辑：xx"按钮下；点击菜单外关闭。

- static string AsciiLower(string s)
  - 将 s 中的 ASCII 大写字母转为小写；其余字符原样保留（用于扩展名等不区分大小写的比较）。

- static bool EndsWith(string s, string suf)
  - s 是否以 suf 结尾（区分大小写；s 短于 suf 时返回 false）。

- static bool IsImageFile(string nm)
  - nm 是否为图片资源文件名（.png/.jpg/.jpeg/.bmp/.gif/.tga 之一，忽略大小写）。

- void RenderResPicker(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - 居中的弹出窗口，列出项目打包的图片资源；选择一个即可填充图片/背景输入框（仍可手动输入路径）。首行为"（无）"。

- int barClickGroup;

- int barClickIndex;

- void AddBarItem(List<RibbonGroup> groups, List<int> grp, List<int> idx, RibbonGroup g, RibbonItem it, int group, int index)
  - 顶部功能区：和 .zform 设计器同一个 Ribbon 控件、
    同一套分组和同一种命令外观（小图标 + 一行文字，
    每组两行）。命令的登记顺序决定它的全局序号，
    grp/idx 两张表把序号映射回原来的分派逻辑。
    登记一个功能区命令：把条目 it 加入组 g，并在 grp/idx 两张表里记录其分派码（group 组号 + index 命令号），渲染点击时按登记顺序反查。

- int BarH(App app)
  - 功能区高度（像素），画布工具栏与层菜单定位使用。

- void RenderLayoutBar(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - 渲染画布上方的快速布局功能区（排版/编辑/网格/缩放/层五组）并分派点击：右侧同时显示只读的缩放与多选状态。

- void RenderContextMenu(App app, Canvas c, Theme t)
  - 针对选中画布元素的浮动右键菜单（复制/删除/z 顺序/锁定）；Esc 或点击菜单项后关闭，无选中时自动收起。

- void ApplyContextAction(int idx)
  - 执行右键菜单项：0 复制，1 删除，3 上移一层，4 下移一层，5 置顶，6 置底，8 锁定/解锁。

- static string KindIcon(string kind)
  - 每个元素种类的工具箱图标。图标字体里没有专门的
    游戏控件字形，所以按"这东西长什么样/干什么"就近取。

- int PalHeadH(App app)
  - 面板度量（像素）：分组标题行高 / 元素行高 / 元素行步进。

- int PalRowH(App app)
  - 元素行高（像素）。

- int PalRowStep(App app)
  - 元素行步进（像素，含行间距，决定面板内容总高）。

- bool PalFolded(string title)
  - 分组标题 title 当前是否处于折叠状态。

- void PalToggleFold(string title)
  - 切换分组标题的折叠/展开状态。

- int PaletteHeader(App app, Canvas c, Theme t, string title, int ix, int iy, int iw)
  - 分组标题行：折叠箭头 + 标题，点整行折叠/展开。返回下一行的 y。

- int PaletteRow(App app, Canvas c, Theme t, string kind, int ix, int iy, int iw)
  - 一行元素种类（小图标 + 名称，无边框，悬停底色）。
    点击在画布中心添加，按住拖到画布可指定位置。

- int PaletteSection(App app, Canvas c, Theme t, int x, int y, int w, string title, List<string> kinds)
  - 一个可折叠分组，返回其后的 y。

- int PaletteSectionH(App app, string title, int count)
  - 一个分组占用的总高度（含标题行、count 个行与间距；折叠时只有标题），供面板内容总高与滚动钳制计算。

- void RenderPalette(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - 渲染元素面板：两个可折叠分组（控件/组件），支持内部滚轮滚动与右侧滚动条；指针悬停行时占用滚轮。

- void RenderCanvas(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - 渲染设计画布：信箱式适配/缩放/平移、网格与背景参考图、元素绘制与选中手柄、智能参考线、拖拽/缩放/框选交互、面板拖放放置与右键菜单触发；最后处理键盘快捷键。

- static int HandleX(int hnum, int ex, int exw)
  - 缩放手柄 hnum 的屏幕空间 X 坐标（0=TL 1=T 2=TR 3=R 4=BR 5=B 6=BL 7=L；ex/exw 为元素屏幕矩形）。

- static int HandleY(int hnum, int ey, int exh)
  - 缩放手柄 hnum 的屏幕空间 Y 坐标（编号同 HandleX；ey/exh 为元素屏幕矩形）。

- int SnapAxisX(int left, int tw)
  - 将提议的设计空间 left 吸附到邻近元素或画布中心的对齐线上（容差 6 设计像素，自身 left/centre/right 三种候选）。返回吸附后的值；若无任何线在范围内则返回 -100000；匹配的参考线记录到 guideX 供绘制。

- int SnapAxisY(int top, int th)
  - 垂直方向的智能吸附（逻辑同 SnapAxisX），匹配的参考线记录到 guideY。

- bool AnyInputFocused(App app)
  - 当设计器自身的某个文本输入框持有键盘焦点时返回 true（宿主据此让出快捷键）。

- void HandleKeys(App app)
  - 键盘编辑：方向键按一个网格步长微调选中项（Shift = 1px），Delete 删除，Ctrl+Z/Y 撤销重做，Ctrl+C/V 复制粘贴，Ctrl+D 复制。检查器输入框获得焦点时忽略全部按键。

- List<SceneElement> OrderedByZ()
  - 文档元素的 z 顺序副本（按 Z 升序稳定插入排序，元素数少）。

- int Stepper(App app, Canvas c, Theme t, int x, int y, int w, string label, int cur, int step)
  - 检查器整数步进器行（标签 + -/按钮 + 数值框）；返回点击后的新值（未点击为 cur），步长 step。

- int StepperHalf(App app, Canvas c, Theme t, int x, int y, int w, string label, int cur, int step)
  - 同一行上并排的紧凑步进器（如 rows/cols 的标签对），返回逻辑同 Stepper。

- void RenderInspector(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - 渲染属性检查器：上半为场景属性（设计尺寸/缩放模式/窗口位置标题与开关/背景），下半为选中元素的属性（名称/层/点击动作/图片/文本/选项/几何/网格组/z 与值/锚点/操作按钮）；无选中时只显示场景区与提示。

- string VisLabel()
  - 可见性按钮的文本：选中元素可见时"显示"，否则"隐藏"（无选中按隐藏处理）。

- int ModeType(int m)
  - 缩放模式选择器按钮的类型码：当前文档模式返回 1（高亮），否则 0。

- int CenterType(int want)
  - 窗口位置按钮的类型码：want 与文档当前 WinCenter 一致返回 1（高亮），否则 0。

- void RenderStatus(App app, Canvas c, Theme t, int x, int y, int w)
  - 渲染底部状态栏：`.zscene`、元素计数与最近一条操作提示 msg。


## SceneDoc (class)

`.zscene` 文档：设计分辨率画布、背景，以及一个扁平的、
按 z 排序的绝对定位 HUD/场景元素列表。这是 Zan IDE 场景
设计器读写所依据的运行时契约；同时承载运行时的层（弹窗/
菜单页）激活状态与声明式动作解释。

- string sname;

- int designWidth;

- int designHeight;

- int scaleMode;

- string background;

- int bgR;

- int bgG;

- int bgB;

- int winCenter;

- int winPosX;

- int winPosY;

- string winTitle;

- int winResizable;

- int winChrome;

- List<SceneElement> elements;

- SceneDoc(string name, int designWidth, int designHeight)

- SceneElement Add(SceneElement e)
  - 追加一个元素到文档末尾；返回该元素（便于链式配置）。

- SceneElement Find(string name)
  - 按名字查找元素；不存在时返回 null。

- int Count()
  - 元素数量。

- SceneElement ElementAt(int i)
  - 返回第 i 个元素（按添加顺序；i 越界行为未定义，调用方保证 0 <= i < Count）。

- string Name()
  - 场景名（.zscene 的 name 字段）。

- int DesignWidth()
  - 设计画布宽（设计像素）。

- int DesignHeight()
  - 设计画布高（设计像素）。

- int Mode()
  - 缩放模式（0 Fit / 1 Fill / 2 Stretch / 3 None，见 ScaleMode）。

- int WinCenter()
  - 窗口显示时是否居中：1 = 居中（默认），0 = 用手动位置。

- void SetWinCenter(int v)
  - 设置窗口是否居中：1 = 居中，0 = 用 SetWinPos 设置的手动位置。

- int WinPosX()
  - 手动窗口位置 X（仅 winCenter == 0 时生效）。

- int WinPosY()
  - 手动窗口位置 Y（仅 winCenter == 0 时生效）。

- void SetWinPos(int x, int y)
  - 设置手动窗口位置（仅 winCenter == 0 时生效）。

- string Background()
  - 背景图片资源 id 或路径（"" = 无背景图）。

- string WinTitle()
  - 窗口标题；空 = 用场景名。

- void SetWinTitle(string s)
  - 设置窗口标题（写入 .zscene 的 winTitle；空 = 运行时用场景名）。

- bool WinResizable()
  - 窗口可调整大小（默认 true）。

- void SetWinResizable(bool v)
  - 设置窗口可否拖边框缩放（默认 true）。

- bool WinChrome()
  - 显示标题栏（默认 true）；关闭后窗口无边框，由场景自绘。

- void SetWinChrome(bool v)
  - 设置是否显示系统标题栏（默认 true）；关闭后窗口无边框，由场景自绘。

- string openLayers;

- bool layersActive;

- List<string> handlerNames;

- List<Action> handlerFns;

- void SetHandler(string name, Action a)
  - 注册（或替换）动作方法名 `name` 的 Action，供元素
    on* 绑定里的非内置动作触发。

- void CallHandler(string name)
  - 触发为 `name` 注册的 Action；未注册的名字是空操作。

- void SetLayersActive(bool v)
  - 打开交互模式：LayerHit 开始按激活层过滤。纯渲染
    （SceneView.Render）无需开启——渲染本来就按层过滤。

- bool ElementHit(SceneElement e)
  - 命中测试语义下元素是否可交互：visible 且其层处于
    打开状态。设计器/运行时点击分发共用这一判定。

- bool LayerOpen(string layer)
  - layer 的元素是否参与本帧渲染：基础层永远 true，其它层
    需要被 OpenLayer 打开过。

- void OpenLayer(string layer)
  - 打开一个层（弹窗/菜单页）；重复打开是幂等的。

- void CloseLayer(string layer)
  - 关闭一个层；本来就没开是幂等的。

- bool ToggleLayer(string layer)
  - 层开关切换；返回切换后是否打开。

- void CloseAllLayers()
  - 关闭所有已打开的浮层（回基础层）。

- string pendingScene;

- string PendingScene()
  - 待切换的目标场景名（"OpenScene:xxx" 排队的目标）；
    "" = 无待处理切换。生成的主循环读它换场景。

- void ClearPendingScene()
  - 清除待处理场景切换（主循环消费后调用）。

- int actionsRun;
  - 最近一次 RunActions 执行的内置动作数（测试观测点）。

- string lastHandler;
  - 上一帧（每次 HandleClicks）触发的方法名处理器记录的最后
    一个名字；测试观测点，"" = 未触发方法名。

- void RunActions(string actions, string selfLayer)
  - 执行一个动作串：内置动作立即生效，方法名查 handlers 表。
    `selfLayer` 是发起元素所属层，供 "HideLayer:self" /
    "ToggleLayer:self" 相对引用。多个动作用 ';' 串接按序执行。

- void RunOneAction(string act, string selfLayer)
  - 执行单个动作（RunActions 的内部分派）：识别 "动作:参数"
    与 "动作"（无参）两种形态；未识别的名字按代码后置方法名
    查 handlers 表，未注册时为空操作。

- void SetMode(int m)
  - 设置缩放模式（0 Fit / 1 Fill / 2 Stretch / 3 None，见 ScaleMode）。

- void SetBackground(string img)
  - 设置背景图片资源 id 或路径（"" = 无背景图）。

- void SetClearColor(int r, int g, int b)
  - 设置清除色 RGB（各 0..255），运行时窗口的背景清除色。

- int ClearR()
  - 清除色红分量（0..255）。

- int ClearG()
  - 清除色绿分量（0..255）。

- int ClearB()
  - 清除色蓝分量（0..255）。

- SceneMetrics Metrics(int winW, int winH)
  - 计算当前窗口/后备缓冲区尺寸下的画布布局与缩放（坐标换算
    的唯一依据）。winW/winH 为窗口尺寸（设备像素）；设计尺寸
    非 正 时按 1 处理。返回的 SceneMetrics 含偏移、画布尺寸与
    两轴缩放系数。

- SceneRect Resolve(SceneElement e, int winW, int winH)
  - 将单个元素解析为给定窗口尺寸下的真实设备像素矩形
    （含网格组总占地）。坐标换算遵循 Metrics 的缩放模式；
    返回矩形以元素锚点对齐，适合直接渲染与命中测试。

- static string IntStr(int v)
  - int 的十进制字符串（"" 情况不会出现：值已被钳制）。

- static SceneDoc Parse(string json)
  - 宽松解析 `.zscene` JSON 文档（尽力而为，面向工具生成的
    输入）：解析失败或根非对象时返回字段取默认值的文档。
    严格校验请用 TryParse（失败原因见 LastError）。

- static string lastError;

- static bool TryParse(string json, SceneDoc outDoc)
  - 严格解析：文档必须是完整合法的 JSON 对象，否则返回
    false 且 SceneDoc.LastError() 给出原因。设计器打开文档
    走这条路径——截断/损坏的设计稿被静默降级成半份文档、
    再在用户下一次编辑时覆盖回盘，比直接拒绝打开糟糕得多。
    返回的文档经 FromRoot 构建，调用方拿到后照常使用。

- static string LastError()
  - 最近一次 Parse/TryParse 的失败原因（"" = 成功或尚未解析）。
    Parse 走宽松路径不会置位；TryParse 失败时携带原因。

- void adopt(SceneDoc parsed)
  - 用 parsed 的内容替换 this 的全部字段（TryParse 的
    out 参数语义：Zan 没有 out 参数，调用方先 new 一个
    空文档传入，成功后它就是解析结果）。

- string OpenLayers()
  - 当前激活层集合的快照（';' 分隔，供 SaveJson 之外的
    调用方检查）；运行时状态，不进 .zscene 文档。

- static SceneDoc FromRoot(JsonValue root)
  - 从已解析的 JSON 根对象填充文档字段（Parse / TryParse 共用
    的构建路径）。各字段带缺省值：name ""、designWidth 1280、
    designHeight 720、scaleMode 0（Fit）、winCenter 1、
    winResizable/winChrome 1、clearColor (18,20,32)。

- static JsonValue UnmodeledKeys(JsonValue o)
  - 收集元素对象里运行时未建模的键（含未来版本的扩展声明），
    供 ToJson 原样写回（旧运行时打开新设计稿不丢数据）。
    没有未建模键时返回 null。

- static JsonValue OnKeysOf(JsonValue o)
  - 提取元素对象里的 on* 键为 {事件名: 动作串} 对象（"onClick"
    存为键 "Click"）；非字符串值跳过，没有时返回 null。
    与 Gui.Designer 的 on<Event> 动态键同一约定。

- static bool IsModeledKey(string k)
  - k 是否为 SceneElement 建模的键（新增建模键时同步加上，
    否则会被 UnmodeledKeys 收集导致 ToJson 重复写出）；
    on* 动作键也按已建模处理（经 OnKeysOf 单独往返）。

- static bool IsModeledDocKey(string k)
  - k 是否为文档级（根对象）已建模的键；ToJson 全量写出，
    因此无需参与 UnmodeledKeys，但 FromRoot 之外的手写工具
    可据此区分建模键与扩展键。

- string ToJson()
  - 序列化为稳定、便于人工 diff 的 `.zscene` JSON（Pretty 格式）。
    全量写出文档级字段；元素级省略缺省项（网格/值/条目/锁定/
    层为默认时不写），未建模键与 on* 动作键原样往返。


## SceneElement (class)

单个放置的元素。位置/尺寸以设计像素编写；anchorX/anchorY
为千分比（0..1000），x/y 是相对锚点的偏移，因此锚定左上角
的血条在任何分辨率下都保持角偏移，而居中的准星（锚点
500/500，x=0，y=0）始终居中。

种类 kind 为设计器与运行时渲染器可识别的名字：控件有
label/button/imagebox/animbox/textbox/progressbar/skillslot/
quickslot/bagslot/itemslot/avatarbox/checkbox/tabbox/container/
minimap/slider/scrollbar/combobox/input/listbox，绘制组件有
text/arttext/sprite/rect/circle/anim；旧种类（"bar"、"panel"）
仍可解析。格子类元素可组成网格组：rows x cols 个 w x h 单元，
间距为 (gapX, gapY)。

- string kind;

- string ename;

- int x;

- int y;

- int w;

- int h;

- int anchorX;

- int anchorY;

- int z;

- string image;

- string text;

- int cr;

- int cg;

- int cb;

- int ca;

- bool visible;

- int rows;

- int cols;

- int gapX;

- int gapY;

- int val;

- string items;

- bool locked;

- string layer;

- JsonValue actions;

- JsonValue extra;

- SceneElement(string kind, string name)

- SceneElement At(int x, int y)
  - 设置设计空间位置（x/y 为相对锚点的偏移），返回自身以链式调用。

- SceneElement Size(int w, int h)
  - 设置单个单元的设计像素尺寸，返回自身以链式调用。

- SceneElement AnchorTo(int ax, int ay)
  - 设置九宫格锚点（ax/ay 千分比 0..1000），返回自身以链式调用。

- SceneElement Order(int z)
  - 设置绘制顺序 z（小者先画），返回自身以链式调用。

- SceneElement Image(string img)
  - 设置精灵/按钮/面板的资源 id 或路径，返回自身以链式调用。

- SceneElement Caption(string t)
  - 设置标签/按钮等的标题文本，返回自身以链式调用。

- SceneElement Tint(int r, int g, int b, int a)
  - 设置着色 RGBA（各分量 0..255），作用于填充与精灵染色，返回自身以链式调用。

- SceneElement SetVisible(bool v)
  - 设置可见性（false 的元素不渲染也不参与命中），返回自身以链式调用。

- SceneElement Grid(int rows, int cols, int gapX, int gapY)
  - 设置网格组行列数与单元间距（设计像素）；rows/cols 小于 1 时钳制为 1，返回自身以链式调用。

- SceneElement SetValue(int v)
  - 设置值：进度条/滑块/滚动条为 0..100，复选框为 0/1，返回自身以链式调用。

- SceneElement SetItems(string s)
  - 设置下拉框/列表框/选项卡的 ';' 分隔条目串，返回自身以链式调用。

- SceneElement SetLocked(bool v)
  - 设置设计器锁定标记（锁定后不可拖动/缩放），返回自身以链式调用。

- SceneElement SetLayer(string layer)
  - 设置归属层名（"" = 基础层），返回自身以链式调用。

- SceneElement On(string evt, string actions)
  - 绑定一个 on* 动作串（如 On("onClick", "ShowLayer:bag")）。
    空值清除该事件的绑定。

- string OnAction(string evt)
  - 绑定到 `evt` 的动作串（如 "onClick" -> "ShowLayer:bag"），
    无绑定为 ""。空串视为未绑定。

- string Kind()
  - 元素种类名（如 "label"、"sprite"），与文档里的 kind 字段一致。

- string Name()
  - 元素名（文档内唯一；重名会导致编译期投影生成重复字段）。

- int X()
  - 相对锚点的水平偏移（设计像素）。

- int Y()
  - 相对锚点的垂直偏移（设计像素）。

- int W()
  - 单个单元宽（设计像素；总占地见 TotalW）。

- int H()
  - 单个单元高（设计像素；总占地见 TotalH）。

- int AnchorX()
  - 水平锚点千分比（0..1000）。

- int AnchorY()
  - 垂直锚点千分比（0..1000）。

- int Z()
  - 绘制顺序（小者先画）。

- string ImageId()
  - 精灵/按钮/面板的资源 id 或路径（"" = 无）。

- string Text()
  - 标签/按钮等的标题文本。

- int R()
  - 着色红分量（0..255）。

- int G()
  - 着色绿分量（0..255）。

- int B()
  - 着色蓝分量（0..255）。

- int A()
  - 着色 alpha 分量（0..255；设计器的幽灵元素也用它近似半透明）。

- bool Visible()
  - 是否可见（false 不渲染且不参与命中）。

- int Rows()
  - 网格组行数（>=1；1 = 单行）。

- int Cols()
  - 网格组列数（>=1；1 = 单列）。

- int GapX()
  - 网格单元水平间距（设计像素）。

- int GapY()
  - 网格单元垂直间距（设计像素）。

- int Value()
  - 值：进度条/滑块/滚动条 0..100，复选框 0/1。

- string Items()
  - 下拉框/列表框/选项卡的 ';' 分隔条目串。

- bool Locked()
  - 设计器锁定标记（锁定后不可拖动/缩放）。

- string Layer()
  - 归属层名（"" = 基础层；命名层需被打开才渲染/命中）。

- bool IsGrid()
  - 是否为网格组（rows 或 cols 大于 1）。

- int TotalW()
  - 网格组总占地宽（设计像素）：cols 个单元加列间间距；单个元素等于 w。

- int TotalH()
  - 网格组总占地高（设计像素）：rows 个单元加行间间距；单个元素等于 h。


## SceneMetrics (class)

当前窗口的缩放画布布局：设计画布渲染在 (offX, offY)，
尺寸为 (canvasW, canvasH)，各轴缩放为 (scaleX, scaleY)。
由 SceneDoc.Metrics 依据缩放模式计算，单位为设备像素。

- double offX;

- double offY;

- double canvasW;

- double canvasH;

- double scaleX;

- double scaleY;

- double OffX()
  - 画布左上角在窗口内的设备像素偏移 X。

- double OffY()
  - 画布左上角在窗口内的设备像素偏移 Y。

- double CanvasW()
  - 画布缩放后的实际宽度（设备像素）。

- double CanvasH()
  - 画布缩放后的实际高度（设备像素）。

- double ScaleX()
  - 水平缩放系数（设计像素 -> 设备像素；Stretch 模式下两轴可不同）。

- double ScaleY()
  - 垂直缩放系数（设计像素 -> 设备像素）。


## SceneRect (class)

解析到屏幕上的矩形，单位为真实设备像素（非设计像素）。
由 SceneDoc.Resolve 依据当前缩放模式计算得出。

- double x;

- double y;

- double w;

- double h;

- static SceneRect Of(double x, double y, double w, double h)
  - 构造矩形：x/y 为左上角设备像素坐标，w/h 为宽高。

- double X()
  - 左上角设备像素 X（浮点）。

- double Y()
  - 左上角设备像素 Y（浮点）。

- double W()
  - 宽（设备像素，浮点）。

- double H()
  - 高（设备像素，浮点）。

- int Xi()
  - 左上角 X 的整数截断（向零取整）。

- int Yi()
  - 左上角 Y 的整数截断（向零取整）。

- int Wi()
  - 宽的整数截断（向零取整）。

- int Hi()
  - 高的整数截断（向零取整）。


## SceneView (class)

`.zscene` 文档的运行时渲染器。IDE 场景设计器在创作时预览
SceneDoc；SceneView 在运行时把完全相同的元素绘制到真实窗口
的内容区域，遵循设计分辨率、缩放模式与逐元素锚点（通过
SceneDoc.Metrics / SceneDoc.Resolve）。生成的 Name.g.zan
代码正是用它，使设计好的场景按设计运行。

- static int PackColor(int r, int g, int b, int a)
  - 打包 RGBA 四分量（各 0..255）为单个颜色整数（A<<24 | R<<16 | G<<8 | B）。

- static bool IsSlotKind(string kind)
  - kind 是否为网格槽位类（skillslot/quickslot/bagslot/itemslot）；这类元素按 rows x cols 网格绘制与命中。

- static string sSplitKey;

- static List<string> sSplitVal;

- static List<string> SplitItems(string items)
  - 按 ';' 拆分 items 串为条目列表；带单条缓存（同串重复调用直接命中，服务每帧渲染）。末尾无 ';' 时收最后一段。

- static List<SceneElement> OrderByZ(SceneDoc doc)
  - 元素按 z 升序的稳定排序副本（z 相同保持文档顺序），渲染按此从底到顶绘制；不修改文档本身。

- static void Render(App app, SceneDoc doc)
  - 将整个场景渲染到窗口内容区域（标题栏之下），适配当前后备
    缓冲区尺寸：清除色打底，元素按 z 升序绘制，跳过不可见、
    所在层未打开与屏幕外的元素。

- static void HandleClicks(App app, SceneDoc doc)
  - 把本帧落在场景元素上的主键点击分发给它们的 on* 动作串。
    在 Render 之后、PresentFrame 之前调用（生成的主循环与
    手写循环都遵守这个次序）。命中语义与渲染一致：visible、
    层打开、几何经 Resolve（含网格组占地），z 大的先吃——
    点在多个元素重叠处命中最上层。层过滤要生效需先
    SetLayersActive(true)（交互模式）；纯渲染无需开启。

- static void DrawElement(App app, Canvas c, Theme t, SceneElement e, int ex, int ey, int exw, int exh, int psMilli)
  - 绘制单个元素（Render 与设计器共用）：ex/ey/exw/exh 为已
    换算的屏幕矩形，psMilli 为千分比缩放。按 kind 分派外观；
    未知种类按通用盒状绘制。填充色取元素 Tint，文本为
    Text（空则回退元素名）。
