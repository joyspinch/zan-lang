# Gui.Component

> 源码: `packages/Zan.Gui.CodeEditor/src/Gui/Component/ChatView.zan`, `packages/Zan.Gui.CodeEditor/src/Gui/Component/ConsoleView.zan`, `packages/Zan.Gui/src/Gui/Component/Dock.zan`, `packages/Zan.Gui/src/Gui/Component/EditorPalette.zan`, `packages/Zan.Gui/src/Gui/Component/FilePicker.zan`, `packages/Zan.Gui/src/Gui/Component/FileTree.zan`, `packages/Zan.Gui/src/Gui/Component/GraphView.zan`, `packages/Zan.Gui/src/Gui/Component/LogView.zan`, `packages/Zan.Gui/src/Gui/Component/PropertyGrid.zan`, `packages/Zan.Gui/src/Gui/Component/Ribbon.zan`, `packages/Zan.Gui/src/Gui/Component/SessionList.zan`


## ChatBubble (class)

- int idx;

- int top;

- int h;

- int divH;

- string divText;

- int nameH;

- string name;

- int quoteH;

- string quoteWho;

- string quoteText;

- string quoteShown;

- List<string> lines;

- WrappedText linesAt;

- int textW;

- int imgW;

- int imgH;

- string imgPath;

- string initial;

- string avaCls;

- bool mine;

- int bw;

- int bh;

- bool waiting;

- ChatBubble()


## ChatHit (class)

- int msg;

- int off;

- ChatHit()


## ChatMessage (class)

- string role;

- string text;

- string reason;

- string agent;

- string to;

- string kind;

- string taskId;

- int progress;

- string call;

- long at;

- string img;

- string msgId;

- int mark;

- string quoteWho;

- string quoteText;

- string quoteId;

- bool card;

- static ChatMessage Create(string role, string text)

- static ChatMessage CreateImage(string role, string path, string text)

- static ChatMessage CreateWithReason(string role, string text, string reason)

- static ChatMessage CreateFrom(string role, string text, string agent, string to, string kind, string taskId, int progress)


## ChatSpeaker (class)

- string name;

- int tint;

- string tagCls;

- static ChatSpeaker Of(string name, int tint, string tagCls)


## ChatStrings (class)

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


## ChatStyle (class)

- static int Log()

- static int Bubble()


## ChatToggleTarget (class)

- int position;

- int msgIndex;

- ChatToggleTarget(int position, int msgIndex)


## ChatView (class)

- List<ChatMessage> data;

- ChatStrings str;

- ChatSpeakerOf speakerOf;

- ChatKindLabel kindLabelOf;

- int style;

- ChatClock clockOf;

- ChatAvatarClick avatarClick;

- ChatBubbleMenu bubbleMenu;

- ChatBubbleMenu bubbleActivate;

- int actMsg;

- int actX;

- int actY;

- App uiApp;

- List<TextRun> cache;

- int cacheTurns;

- int cachePrefix;

- string cacheKey;

- string cacheSig;

- List<TextRun> show;

- bool showStale;

- List<int> toolOpen;

- SignalInt thinkOpen;

- SignalInt scrollSig;

- LogState sel;

- bool busy;

- StyledText grid;

- Label emptyLbl;

- List<ChatBubble> bub;

- string bubKey;

- int bubH;

- bool bubStale;

- SignalInt bscr;

- int bubWMille;

- string bubSig;

- bool showNames;

- int selMsg0;

- int selOff0;

- int selMsg1;

- int selOff1;

- bool selDrag;

- ChatQuote quoteCb;

- ChatView()

- override string Kind()

- override string StyleType()

- ChatView Bind(List<ChatMessage> msgs)

- ChatView WithStrings(ChatStrings s)

- ChatView WithSpeakers(ChatSpeakerOf who, ChatKindLabel kindLabel)

- ChatView WithStyle(int st)

- ChatView WithClock(ChatClock c)

- ChatView WithBubbleWidth(int mille)

- int Style()

- ChatView WithSpeakerNames(bool on)

- string Clock(long at)

- static string ClockText(long at)

- void SetBusy(bool on)

- LogState Selection()

- SignalInt Scroll()

- SignalInt BubbleScroll()

- void Reset()

- void Invalidate()

- string SelectedText()

- bool HasSelection()

- void ClearSelection()

- override void OnPaint(App app)

- ChatView OnAvatarClick(ChatAvatarClick cb)

- void AvatarHit()

- ChatHit HitBubble(App app, int mx, int my, bool clamp)

- void SelRange(out int m0, out int o0, out int m1, out int o1)

- string BubbleSelectionText()

- bool HasBubbleSelection()

- void ClearBubbleSelection()

- ChatHit SnapHit(ChatHit h)

- void BubbleMouse(App app)

- void BubbleDoubleClick(App app)

- ChatView OnBubbleMenu(ChatBubbleMenu cb)

- ChatView OnBubbleActivate(ChatBubbleMenu cb)

- ChatView OnQuote(ChatQuote cb)

- string NameOf(int idx)

- void TakeBubbleWheel(App app)

- void TakeWheel(App app)

- void TakeCopy(App app)

- void TakeBubbleCopy(App app)

- void Build(App app)

- static int CodeFoldLines()

- static int BuildBudgetMs()

- static int BuildChunk()

- static bool HasInt(List<int> xs, int v)

- static int RoleCode(string role)

- static void WrapInto(string s, int maxW, int fontSize, List<string> outLines)

- void WrapRows(string s, int maxW, int fontSize, int color, int kind)

- void MarkBlockEnd()

- bool SkipBlank(string s)

- void TrimTrailBlank(int floorIdx)

- void OwnRows(int start, int owner)

- static string MdStrip(string ln)

- static bool HasLongCode(List<string> src)

- void BuildRange(App app, int from, int to, int textW)

- static int BubbleTimeGap()

- static int BubbleFont(App app)

- string BubbleLayoutSig(App app)

- void BuildBubble(App app)

- string SpeakerName(ChatMessage msg)

- string SpeakerInitial(ChatMessage msg)

- string InitialOf(string name)

- void PaintBubbles(App app)

- static SignalInt bubbleBar;

- static SignalInt BubbleBarSig()

- static Avatar bubbleAva;

- static Avatar BubbleAvatar()

- static string SelectionText(List<TextRun> rows, LogState st)


## ConsoleView (class)

- List<LogLine> items;

- LogState st;

- string Empty;

- bool live;

- ConsoleView()

- override string Kind()

- override string StyleType()

- ConsoleView BindItems(List<LogLine> lines)

- ConsoleView Bind(List<string> lines)

- ConsoleView WithColors(List<int> cols)

- ConsoleView EmptyText(string text)

- ConsoleView Passive()

- LogState State()

- void Reset()

- string SelectedText()

- override List<PropSpec> Props()

- override void OnPaint(App app)


## DockGroup (class)

- int key;

- static int nextKey;

- int side;

- List<string> ids;

- int active;

- int extent;

- int weight;

- Rect bounds;

- Rect header;

- Rect body;

- DockGroup(int side, int weight)


## DockHost (class)

- List<DockPanel> panels;

- List<DockGroup> groups;

- int leftW;

- int rightW;

- int bottomH;

- bool sized;

- int dpiScale;

- bool logicalLayoutPending;

- bool dragging;

- bool dragArmed;

- string dragId;

- int dropKind;

- int dropGroup;

- int dropSide;

- int dropIndex;

- Rect dropRect;

- bool menuOpen;

- int menuX;

- int menuY;

- string emptyMenuLabel;

- int stripSentinelSeq;

- List<string> switchIds;

- int switchBase;

- string switchHideHint;

- string switchShowHint;

- string lastClosed;

- bool changed;

- bool pendingResize;

- Rect area;

- int headerH;

- DockHost()

- DockPanel Find(string id)

- int GroupOf(string id)

- void Register(string id, string title, int side, bool closable, bool newGroup)

- void RegisterFixed(string id, string title)

- DockGroup LastGroupOf(int side)

- void SetWeight(string id, int weight)

- void SetSideExtent(int side, int px)

- bool Visible(string id)

- Rect Content(string id)

- Rect ContentParked(string id)

- string Title(string id)

- void SetTitle(string id, string title)

- void SetIcon(string id, string icon)

- void SetTip(string id, string text)

- void SetSwitchHints(string hideHint, string showHint)

- string SwitchTip(DockPanel p)

- int AddSwitches(List<RibbonCmd> cmds)

- void AddSwitchGroup(List<RibbonGroup> groups, string title)

- void ClearSwitches()

- void SyncSwitches(List<RibbonCmd> cmds)

- bool HandleRibbon(int pos)

- void Close(string id)

- void NoteHome(DockPanel p)

- int SideIndexOf(int gi)

- int InsertPosFor(int side, int at)

- int GroupWithKey(int key)

- void Detach(string id)

- void ShowOn(string id, int side)

- bool IsHidden(string id)

- void Toggle(string id)

- void Show(string id)

- int IndexIn(DockGroup g, string id)

- List<string> HiddenIds()

- void Focus(string id)

- string TakeClosed()

- bool TakeChanged()

- int PersistLogical(int px)

- int PersistPhysical(int logical)

- string SaveLayout()

- bool LoadLayout(string text)

- static int SideOfIn(List<DockGroup> gs, string id)

- static bool ListHas(List<string> xs, string v)

- static bool HasPrefix(string s, string p)

- static string After(string s, int n)

- static List<string> SplitOn(string s, string sep)

- bool SideHasVisible(int side)

- bool GroupHasVisible(DockGroup g)

- List<DockGroup> VisibleGroups(int side)

- void Begin(App app, Rect a)

- int Handle(App app, int x, int y, int w, int h, bool vertical)

- void LayoutSide(App app, List<DockGroup> gs, Rect r, bool vertical, int hs)

- void RenderGroup(App app, DockGroup g)

- void RenderMenu(App app)

- void RegisterClipped(App app, int id, int rx, int ry, int rw, int rh, int clipX, int clipW)

- void End(App app)

- void ResolveDrop(App app)

- static int EdgeZone(Rect r, int mx, int my)

- static Rect HalfRect(Rect r, int zone)

- static int SideForZone(int zone)

- Rect SideStrip(App app, int side)

- int SideIndexOf(DockGroup g)

- int SideCount(int side)

- int InsertPos(int side, int k)

- void ApplyDrop()

- int IndexOfKey(int key)

- bool Dragging()


## DockPanel (class)

- string id;

- string title;

- string icon;

- string tip;

- bool closable;

- bool headerless;

- bool hidden;

- int home;

- int homeKey;

- int homeAt;

- int homeSideIdx;

- Rect body;

- DockPanel(string id, string title, bool closable)


## DockSide (class)

- static int Left()

- static int Right()

- static int Bottom()

- static int Center()


## EditorPalette (class)

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

- static EditorPalette Light()

- static EditorPalette For(Gui.Theme t)

- static EditorPalette Preset(int idx)

- static EditorPalette PresetFor(int idx, bool lightBg)

- static EditorPalette Chrome()


## FileFilter (class)

- string label;

- string pattern;

- FileFilter(string label, string pattern)


## FilePicker (class)

- static string lang="zh";

- [DllImport("zan_gui")]static extern string zan_gui_android_files_dir();

- Input pathInput;

- Input nameInput;

- Input folderInput;

- Input searchInput;

- SignalInt treeScroll;

- SignalInt listScroll;

- SignalBool showHidden;

- App popup;

- bool inlineMode;

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

- int anchorIdx;

- bool allowMulti;

- int maxSelect;

- string statusText;

- string lastFilter;

- string lastDir;

- List<string> expanded;

- List<FileFilter> filters;

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

- Input SearchBox()

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

- bool IsExpanded(string path)

- void Expand(string path)

- void Collapse(string path)

- void ExpandAncestors(string dir)

- void BuildTree(string dir, int depth, List<FilePickerTreeNode> nodes)

- void Begin(int pickerMode, string initial, string extension, string defaultName, string dialogTitle)

- void BeginFolder(string initial, string dialogTitle)

- void BeginOpenFile(string initial, string extension, string dialogTitle)

- void BeginOpenFileFilters(string initial, List<FileFilter> items, string dialogTitle)

- void BeginOpenFileMulti(string initial, string extension, string dialogTitle)

- void SetMaxSelect(int n)

- int MaxSelect()

- void BeginOpenFileFiltersMulti(string initial, List<FileFilter> items, string dialogTitle)

- static string FirstExt(string pattern)

- static bool ContainsText(string text, string needle)

- static string FilterTokenToSuffix(string ext)

- bool MatchesCurrentFilter(string name)

- bool PatternMatches(string name, string pattern)

- void BeginSaveFile(string initial, string extension, string defaultName, string dialogTitle)

- void ApplySkin(int index)

- int SkinIndex()

- bool IsOpen()

- bool WindowModeOpen()

- bool NeedsRedraw()

- bool OwnsWindow(nint hwnd)

- nint PopupWindowHandle()

- string SelectedPath()

- List<string> SelectedPaths()

- string LastDirectory()

- string CurrentDirectory()

- void RaiseFilterChanged()

- void Finish(int action)

- bool InMultiSelection(int idx, List<FilePickerItem> items)

- void RemoveSelectedPath(string path)

- void Close()

- bool ApplyEvent(nint evHwnd)

- void SweepClosed()

- int TakeResult()

- void Navigate(string dir)

- void RenderWindow()

- void RenderOverlay(App host)

- void RenderContent(App app)


## FilePickerItem (class)

- public string name;

- public string path;

- public int kind;

- public FilePickerItem(string name, string path, int kind)


## FilePickerTreeNode (class)

- public string path;

- public int depth;

- public FilePickerTreeNode(string path, int depth)


## FileTree (class)

- static void Into(List<TreeNode> nodes, string dirPath, string dirName, int depth, bool expanded, PathFilter f, bool withRoot, bool expandAll)

- static List<TreeNode> Nodes(string dirPath, string dirName, PathFilter f, bool withRoot, bool expanded)


## GraphCardSlot (class)

- public Control card;

- public Tag lockMark;

- GraphCardSlot(Control card, Tag lockMark)


## GraphNode (class)

- string id;

- string title;

- string tag;

- string tagClass;

- string state;

- string stateClass;

- int progress;

- bool done;

- List<string> deps;

- string payload;

- int depth;

- int lx;

- int ly;

- int lw;

- int lh;

- static GraphNode Of(string id, string title)

- GraphNode After(string depId)

- GraphNode WithTag(string text, string cls)

- GraphNode WithState(string text, string cls)

- GraphNode WithProgress(int pct)

- GraphNode WithPayload(string p)

- GraphNode Finished()


## GraphView (class)

- List<GraphNode> data;

- SignalInt sel;

- SignalInt scroll;

- GraphCardOf tpl;

- List<GraphCardSlot> slots;

- bool cardsStale;

- int builtFor;

- string Empty;

- int ctxRow;

- int ctxX;

- int ctxY;

- UiEvent Select;

- UiEvent Activate;

- UiEvent Blocked;

- UiEvent Context;

- void InitGraph()

- GraphView()

- GraphView(GraphCardOf template)

- override string Kind()

- override string StyleType()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- GraphView Bind(List<GraphNode> nodes)

- void Refresh()

- GraphView BindSel(SignalInt s)

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

- int ContextRow()

- int ContextX()

- int ContextY()

- int IndexOfId(string id)

- void SelectId(string id)

- void Deselect()

- static int IndexOf(List<GraphNode> ns, string id)

- List<string> PendingDeps(int row)

- bool LockedAt(int row)

- void ComputeDepth()

- bool Templated()

- void BuildCards(App app)

- static Card DefaultCard(GraphNode n, Tag mark)

- static string TagClass(string cls)

- static Tag LockMark()

- int Gap(App app)

- int CardWidth(App app, int areaW)

- int CardHeight(App app)

- int Layout(App app, int areaX, int areaY, int areaW)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- int ScrollOffset(App app, int contentH)

- void PaintEdges(App app)

- void PaintNodes(App app)

- static string NodeClass(bool selected, bool hovered, bool locked)

- int NodeInteract(App app, int index, GraphNode n)

- void NoteSelfDamage(App app)

- void PaintEmpty(App app)


## LogLine (class)

- public string text;

- public int color;

- public LogLine(string text, int color)

- public static LogLine Of(string text, int color)

- public static LogLine OfText(string text)


## LogState (class)

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

- void Reset()


## LogView (class)

- static int RowHeight(App app)

- static int Padding(App app)

- static int VisibleRows(App app, int h)

- static int FirstRow(App app, int h, int count, int scroll)

- static int RowAt(App app, int y, int h, int count, int scroll, int mouseY)

- static int ColAt(string line, int textX, int fontSize, int mouseX)

- static string SelectionTextItems(List<LogLine> lines, LogState st)

- static string SelectionText(List<string> lines, LogState st)

- static void RenderItems(App app, int x, int y, int w, int h, List<LogLine> lines, LogState st, string emptyText, bool interactive)

- static void Render(App app, int x, int y, int w, int h, List<string> lines, List<int> colors, LogState st, string emptyText, bool interactive)

- static void HandleInputItems(App app, int x, int y, int w, int h, List<LogLine> lines, LogState st)

- static void HandleInput(App app, int x, int y, int w, int h, List<string> lines, LogState st)


## PropertyGrid (class)

- List<PropSpec> data;

- List<PropertyGridRow> rows;

- string key;

- SignalInt scroll;

- public int LabelWidth;

- string Empty;

- UiEvent Change;

- Action<string> BeforeChange;

- int hitRow;

- PropertyGrid()

- override string Kind()

- PropertyGrid OnChange(Action a)

- PropertyGrid OnBeforeChange(Action<string> a)

- int ChangedRow()

- string ChangedKey()

- void Bind(string k, List<PropSpec> specs)

- void Rebuild()

- int IndexOfValue(PropSpec p, string val)

- string PickValue(PropSpec p, int idx)

- static bool IsNumber(string s)

- string Editor(int i)

- void Load(int i, string val)

- void Sync(App app, int i)

- void Step(App app, int i, int delta)

- bool StepBtn(App app, int id, int x, int y, int sz, string glyph, StyleBox normalStyle, StyleBox hoverStyle)

- int RowHeight(App app)

- int ContentHeight(App app)

- override void OnMeasure(App app)

- override void OnPaint(App app)

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)


## PropertyGridRow (class)

- public Input text;

- public Switch flag;

- public SelectBox pick;

- public List<int> pickSegIds;

- public int minusId;

- public int plusId;

- public string shadow;

- PropertyGridRow()


## Ribbon (class)

- class Sec

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

- public void SetSpeed(double factor)

- public void Render(App app, int x, int y, int w, int h, int alphaPercent)

- List<Sec> NewRibbon()

- bool RibbonDone(List<Sec> ribbon)

- void DrawSection(Canvas c, Sec s, int ox, int oy)

- static void FillTriangle(Canvas c, int x0, int y0, int x1, int y1, int x2, int y2, int color)

- static int Hsl(int hh, int ss, int ll)


## SessionList (class)

- Input search;

- SignalInt scroll;

- Button newBtn;

- string emptyText;

- bool showArchived;

- string archiveTip;

- string deleteTip;

- string archivedLabel;

- string renameTip;

- int editing;

- Input rename;

- bool takeFocus;

- SessionList(string searchHint, string newLabel)

- SessionList Labels(string archive, string delete, string archived)

- SessionList RenameLabel(string rename2)

- void CancelRename()

- SessionList EmptyText(string text)

- string Query()

- static int RowH(App app)

- static int Fold(int b)

- static bool CiContains(string hay, string needle)

- SessionListAction Render(App app, int x, int y, int w, int h, List<string> titles, int cur)

- SessionListAction RenderEx(App app, int x, int y, int w, int h, List<string> titles, int cur, List<int> archived)


## SessionListAction (class)

- int selected;

- int deleted;

- int archived;

- bool created;

- int renamed;

- string title;

- static SessionListAction None()

- bool Any()


## ChatSpeaker (delegate)

`delegate ChatSpeaker ChatSpeakerOf(string agent);`


## Control (delegate)

`delegate Control GraphCardOf(GraphNode node);`


## string (delegate)

`delegate string ChatKindLabel(string kind);`


## string (delegate)

`delegate string ChatClock(long at);`


## void (delegate)

`delegate void ChatAvatarClick(int msgIndex);`


## void (delegate)

`delegate void ChatQuote(int msgIndex, string text);`


## void (delegate)

`delegate void ChatBubbleMenu(int msgIndex, string selText);`
