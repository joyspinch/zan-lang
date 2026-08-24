# Game.Scene

> 源码: `stdlib/Game/Scene/SceneDesigner.zan`, `stdlib/Game/Scene/SceneDoc.zan`, `stdlib/Game/Scene/SceneView.zan`


## Anchor (class)

- static int Min()

- static int Mid()

- static int Max()


## ScaleMode (class)

- static int Fit()

- static int Fill()

- static int Stretch()

- static int None()


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

- static int lang=1;

- static string assetsDir;

- SceneDoc doc;

- SceneElement sel;

- SceneElement lastSel;

- bool hostPanels;

- bool dragging;

- bool resizing;

- int resizeH;

- string palDrag;

- bool palMoved;

- int dragDx;

- int dragDy;

- int guideX;

- int guideY;

- int inpIdName;

- int inpIdImg;

- int inpIdText;

- int inpIdBg;

- int inpIdItems;

- List<SceneElement> selExtra;

- bool marquee;

- int mqSX;

- int mqSY;

- bool showTags;

- int zoom;

- int panX;

- int panY;

- bool hand;

- bool panning;

- int panSX;

- int panSY;

- int panOX;

- int panOY;

- List<string> palFolded;

- int palScroll;

- List<string> undoStack;

- List<string> redoStack;

- string clipJson;

- int resPick;

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

- Input nameIn;

- Input imgIn;

- Input textIn;

- Input bgIn;

- Input itemsIn;

- static string T(string en, string zh)
  - 选取当前界面语言对应的字符串。

- static SceneDesigner current;
  - 资源选择器处于打开状态的设计器，使列表处理函数（以方法绑定）
    能够访问它。

- static void ResourcePicked()
  - 选中的资源写入选择器为之打开的字段；
    第一行表示"无资源"。

- SceneDesigner()

- void LoadJson(string json)

- string SaveJson()

- void PushUndo()
  - 记录当前文档；在任何修改前调用。

- void Undo()

- void Redo()

- static List<string> ControlKinds()

- static List<string> ComponentKinds()

- static string KindLabel(string kind)

- static bool IsSlotKind(string kind)

- static void ApplyDefaults(SceneElement e, string kind)

- static bool InRect(int mx, int my, int x, int y, int w, int h)

- static int PackColor(int r, int g, int b, int a)

- static List<string> SplitItems(string items)

- int DesignLeft(SceneElement e)

- int DesignTop(SceneElement e)

- void AddElement(string kind)

- void AddElementAt(string kind, int dx, int dy)
  - 在设计空间点 (dx, dy) 处居中添加一个 `kind` 元素——
    面板拖拽的放置目标。

- SceneElement CloneEl(SceneElement s)

- void DuplicateSel()

- void CopySel()

- void PasteClip()

- void DeleteSel()

- void RaiseSel(int dir)

- void SelToTop()

- void SelToBottom()

- bool IsExtraSel(SceneElement e)

- void ToggleExtraSel(SceneElement e)

- void SetDesignPos(SceneElement e, int left, int top)

- void SetTotalSize(SceneElement e, int tw, int th)

- void AlignSel(int op)

- void Render(App app, int x, int y, int w, int h)

- void RenderHostPanels(App app, Rect toolRect, Rect propRect)
  - 宿主（IDE）把元素面板和属性面板画进自己的 dock 面板里，
    设计器本体就只剩功能区 + 画布。id 基值和 Render 分开，
    免得两趟互相错位。

- void RenderPinned(App app, int x, int y, int w, int h)

- static string AsciiLower(string s)

- static bool EndsWith(string s, string suf)

- static bool IsImageFile(string nm)

- void RenderResPicker(App app, Canvas c, Theme t, int x, int y, int w, int h)

- int barClickGroup;

- int barClickIndex;

- void AddBarItem(List<RibbonGroup> groups, List<int> grp, List<int> idx, RibbonGroup g, RibbonItem it, int group, int index)
  - 顶部功能区：和 .zform 设计器同一个 Ribbon 控件、
    同一套分组和同一种命令外观（小图标 + 一行文字，
    每组两行）。命令的登记顺序决定它的全局序号，
    grp/idx 两张表把序号映射回原来的分派逻辑。

- int BarH(App app)

- void RenderLayoutBar(App app, Canvas c, Theme t, int x, int y, int w, int h)

- void RenderContextMenu(App app, Canvas c, Theme t)

- void ApplyContextAction(int idx)

- static string KindIcon(string kind)
  - 每个元素种类的工具箱图标。图标字体里没有专门的
    游戏控件字形，所以按"这东西长什么样/干什么"就近取。

- int PalHeadH(App app)

- int PalRowH(App app)

- int PalRowStep(App app)

- bool PalFolded(string title)

- void PalToggleFold(string title)

- int PaletteHeader(App app, Canvas c, Theme t, string title, int ix, int iy, int iw)
  - 分组标题行：折叠箭头 + 标题，点整行折叠/展开。

- int PaletteRow(App app, Canvas c, Theme t, string kind, int ix, int iy, int iw)
  - 一行元素种类（小图标 + 名称，无边框，悬停底色）。
    点击在画布中心添加，按住拖到画布可指定位置。

- int PaletteSection(App app, Canvas c, Theme t, int x, int y, int w, string title, List<string> kinds)
  - 一个可折叠分组，返回其后的 y。

- int PaletteSectionH(App app, string title, int count)

- void RenderPalette(App app, Canvas c, Theme t, int x, int y, int w, int h)

- void RenderCanvas(App app, Canvas c, Theme t, int x, int y, int w, int h)

- static int HandleX(int hnum, int ex, int exw)

- static int HandleY(int hnum, int ey, int exh)

- int SnapAxisX(int left, int tw)

- int SnapAxisY(int top, int th)

- bool AnyInputFocused(App app)

- void HandleKeys(App app)

- List<SceneElement> OrderedByZ()

- int Stepper(App app, Canvas c, Theme t, int x, int y, int w, string label, int cur, int step)

- int StepperHalf(App app, Canvas c, Theme t, int x, int y, int w, string label, int cur, int step)

- void RenderInspector(App app, Canvas c, Theme t, int x, int y, int w, int h)

- string VisLabel()

- int ModeType(int m)

- int CenterType(int want)

- void RenderStatus(App app, Canvas c, Theme t, int x, int y, int w)


## SceneDoc (class)

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

- List<SceneElement> elements;

- SceneDoc(string name, int designWidth, int designHeight)

- SceneElement Add(SceneElement e)

- SceneElement Find(string name)

- int Count()

- SceneElement ElementAt(int i)

- string Name()

- int DesignWidth()

- int DesignHeight()

- int Mode()

- int WinCenter()

- void SetWinCenter(int v)

- int WinPosX()

- int WinPosY()

- void SetWinPos(int x, int y)

- string Background()

- void SetMode(int m)

- void SetBackground(string img)

- void SetClearColor(int r, int g, int b)

- int ClearR()

- int ClearG()

- int ClearB()

- SceneMetrics Metrics(int winW, int winH)

- SceneRect Resolve(SceneElement e, int winW, int winH)

- static string IntStr(int v)
  - int 的十进制字符串（"" 情况不会出现：值已被钳制）。

- static SceneDoc Parse(string json)

- string ToJson()


## SceneElement (class)

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

- SceneElement(string kind, string name)

- SceneElement At(int x, int y)

- SceneElement Size(int w, int h)

- SceneElement AnchorTo(int ax, int ay)

- SceneElement Order(int z)

- SceneElement Image(string img)

- SceneElement Caption(string t)

- SceneElement Tint(int r, int g, int b, int a)

- SceneElement SetVisible(bool v)

- SceneElement Grid(int rows, int cols, int gapX, int gapY)

- SceneElement SetValue(int v)

- SceneElement SetItems(string s)

- SceneElement SetLocked(bool v)

- string Kind()

- string Name()

- int X()

- int Y()

- int W()

- int H()

- int AnchorX()

- int AnchorY()

- int Z()

- string ImageId()

- string Text()

- int R()

- int G()

- int B()

- int A()

- bool Visible()

- int Rows()

- int Cols()

- int GapX()

- int GapY()

- int Value()

- string Items()

- bool Locked()

- bool IsGrid()

- int TotalW()

- int TotalH()


## SceneMetrics (class)

- double offX;

- double offY;

- double canvasW;

- double canvasH;

- double scaleX;

- double scaleY;

- double OffX()

- double OffY()

- double CanvasW()

- double CanvasH()

- double ScaleX()

- double ScaleY()


## SceneRect (class)

- double x;

- double y;

- double w;

- double h;

- static SceneRect Of(double x, double y, double w, double h)

- double X()

- double Y()

- double W()

- double H()

- int Xi()

- int Yi()

- int Wi()

- int Hi()


## SceneView (class)

- static int PackColor(int r, int g, int b, int a)

- static bool IsSlotKind(string kind)

- static string sSplitKey;

- static List<string> sSplitVal;

- static List<string> SplitItems(string items)

- static List<SceneElement> OrderByZ(SceneDoc doc)

- static void Render(App app, SceneDoc doc)

- static void DrawElement(App app, Canvas c, Theme t, SceneElement e, int ex, int ey, int exw, int exh, int psMilli)
