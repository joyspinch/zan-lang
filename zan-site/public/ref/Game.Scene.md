# Game.Scene

> 源码: `packages/Zan.Game/src/Game/Scene/SceneDesigner.Prepared.zan`, `packages/Zan.Game/src/Game/Scene/SceneDesigner.zan`, `packages/Zan.Game/src/Game/Scene/SceneDoc.zan`, `packages/Zan.Game/src/Game/Scene/SceneView.zan`


## Anchor (class)

- static int Min()

- static int Mid()

- static int Max()


## ScaleMode (class)

- static int Fit()

- static int Fill()

- static int Stretch()

- static int None()


## SceneActionBinding (class)

- string event;

- string action;

- SceneActionBinding(string evt, string act)


## SceneAlignCommand (class)

- public string icon;

- public string label;

- public int group;

- public int index;

- public SceneAlignCommand(string icon, string label, int group, int index)


## SceneBarDispatch (class)

- public int group;

- public int index;

- public SceneBarDispatch(int group, int index)


## SceneDesigner (class)

- static ScenePreparedDocument PrepareJson(string text)

- bool ApplyPreparedDocument(ScenePreparedDocument prepared)

- SceneDoc CaptureDocumentSnapshot()

- bool DocumentMatchesSnapshot(SceneDoc snapshot)


## SceneDesigner (class)

- static string lang="zh";

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

- int inpIdLayer;

- int inpIdTitle;

- int inpIdClick;

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

- bool ctxOpen;

- int ctxX;

- int ctxY;

- int ctxHover;

- bool showGrid;

- bool snap;

- int grid;

- string msg;

- string editLayer;

- bool layerMenuOpen;

- bool showGhost;

- Input layerIn;

- List<string> DocLayers()

- void Select(SceneElement e)

- bool InEditLayer(SceneElement e)

- Input nameIn;

- Input imgIn;

- Input textIn;

- Input bgIn;

- Input itemsIn;

- Input titleIn;

- Input clickIn;

- Input classIn;

- int inpIdClass;

- static string T(string en, string zh)

- static SceneDesigner current;

- bool loadError;

- static void ResourcePicked()

- SceneDesigner()

- void LoadJson(string json)

- string SaveJson()

- void PushUndo()

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

- SceneElement CloneEl(SceneElement s)

- bool UsedName(string nn)

- bool UsedNameBut(string nn, SceneElement skip)

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

- void SnapSelectionToGrid()

- void SnapElementToGrid(SceneElement e)

- void SelectAll()

- void ToggleLockSel()

- void ClearSel()

- void NudgeSel(int dx, int dy)

- void Render(App app, int x, int y, int w, int h)

- void RenderHostPanels(App app, Rect toolRect, Rect propRect)

- void RenderPinned(App app, int x, int y, int w, int h)

- void RenderLayerMenu(App app, Canvas c, Theme t, int x, int y, int w, int h)

- bool DocHasLayer(string ly)

- static string AsciiLower(string s)

- static bool EndsWith(string s, string suf)

- static bool IsImageFile(string nm)

- void RenderResPicker(App app, Canvas c, Theme t, int x, int y, int w, int h)

- int barClickGroup;

- int barClickIndex;

- void AddBarItem(List<RibbonGroup> groups, List<SceneBarDispatch> dispatches, RibbonGroup g, RibbonItem it, int group, int index)

- int BarH(App app)

- void RenderLayoutBar(App app, Canvas c, Theme t, int x, int y, int w, int h)

- void RenderContextMenu(App app, Canvas c, Theme t)

- void ApplyContextAction(int idx, string itemText)

- static string KindIcon(string kind)

- int PalHeadH(App app)

- int PalRowH(App app)

- int PalRowStep(App app)

- bool PalFolded(string title)

- void PalToggleFold(string title)

- int PaletteHeader(App app, Canvas c, Theme t, string title, int ix, int iy, int iw)

- int PaletteRow(App app, Canvas c, Theme t, string kind, int ix, int iy, int iw)

- int PaletteSection(App app, Canvas c, Theme t, int x, int y, int w, string title, List<string> kinds)

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

- SceneDoc CaptureSnapshot()

- bool SnapshotEquals(SceneDoc s)

- static bool SnapshotJsonEqual(JsonValue a, JsonValue b)


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

- string winTitle;

- int winResizable;

- int winChrome;

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

- string WinTitle()

- void SetWinTitle(string s)

- bool WinResizable()

- void SetWinResizable(bool v)

- bool WinChrome()

- void SetWinChrome(bool v)

- string openLayers;

- bool layersActive;

- List<SceneHandlerBinding> handlers;

- void SetHandler(string name, Action a)

- void CallHandler(string name)

- void SetLayersActive(bool v)

- bool ElementHit(SceneElement e)

- bool LayerOpen(string layer)

- void OpenLayer(string layer)

- void CloseLayer(string layer)

- bool ToggleLayer(string layer)

- void CloseAllLayers()

- string pendingScene;

- string PendingScene()

- void ClearPendingScene()

- int actionsRun;

- string lastHandler;

- void RunActions(string actions, string selfLayer)

- void RunOneAction(string act, string selfLayer)

- void SetMode(int m)

- void SetBackground(string img)

- void SetClearColor(int r, int g, int b)

- int ClearR()

- int ClearG()

- int ClearB()

- SceneMetrics Metrics(int winW, int winH)

- SceneRect Resolve(SceneElement e, int winW, int winH)

- static string IntStr(int v)

- static SceneDoc Parse(string json)

- static string lastError;

- static bool TryParse(string json, SceneDoc outDoc)

- static string LastError()

- void adopt(SceneDoc parsed)

- string OpenLayers()

- static SceneDoc FromRoot(JsonValue root)

- static JsonValue UnmodeledKeys(JsonValue o)

- static JsonValue OnKeysOf(JsonValue o)

- static bool IsModeledKey(string k)

- static bool IsModeledDocKey(string k)

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

- string layer;

- string cssClass;

- int position;

- int flexGrow;

- List<SceneActionBinding> actionList;

- JsonValue actions;

- JsonValue extra;

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

- SceneElement SetLayer(string layer)

- SceneElement SetCssClass(string cls)

- SceneElement SetPosition(int pos)

- SceneElement SetFlexGrow(int g)

- SceneElement On(string evt, string actions)

- string OnAction(string evt)

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

- string Layer()

- string CssClass()

- int Position()

- int FlexGrow()

- bool IsGrid()

- int TotalW()

- int TotalH()


## SceneHandlerBinding (class)

- string name;

- Action fn;

- SceneHandlerBinding(string name, Action fn)


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


## ScenePreparedDocument (class)

- SceneDoc document=new SceneDoc("", 1280, 720);

- bool loadError;

- string lastError="";

- bool applied;


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

- static void HandleClicks(App app, SceneDoc doc)

- static void DrawElement(App app, Canvas c, Theme t, SceneElement e, int ex, int ey, int exw, int exh, int psMilli)


## SceneAnchor (enum)

- None = =0

- TopLeft = =1

- TopCenter = =2

- TopRight = =3

- CenterLeft = =4

- Center = =5

- CenterRight = =6

- BottomLeft = =7

- BottomCenter = =8

- BottomRight = =9

- Stretch = =10


## SceneScaleMode (enum)

- Fit = =0

- Fill = =1

- Stretch = =2

- None = =3
