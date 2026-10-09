# Gui.Designer

> 源码: `packages/Zan.Gui/src/Gui/Designer/DesignExport.zan`, `packages/Zan.Gui/src/Gui/Designer/Designer.Form.zan`, `packages/Zan.Gui/src/Gui/Designer/Designer.Html.zan`, `packages/Zan.Gui/src/Gui/Designer/Designer.Inspector.zan`, `packages/Zan.Gui/src/Gui/Designer/Designer.Prepared.zan`, `packages/Zan.Gui/src/Gui/Designer/Designer.Preview.zan`, `packages/Zan.Gui/src/Gui/Designer/Designer.zan`


## ColumnConfig (class)

- int width;

- int align;

- int type;

- bool sortable;

- bool resizable;

- bool editable;

- int decimals;

- string money;

- bool percent;

- string prefix;

- string suffix;

- bool grouped;

- int summary;

- int pin;

- string band;

- string click;

- ColumnConfig()

- static ColumnConfig FromJson(JsonValue e)

- JsonValue ToJsonObject()


## CompPropDecl (class)

- public string key;

- public string label;

- public string def;

- CompPropDecl(string key, string label, string def)


## CompPropEditRow (class)

- public string key;

- public string label;

- public bool isBool;

- public Input input;

- public Switch sw;

- public string shadow;

- CompPropEditRow(string key, string label, bool isBool, Input input, Switch sw, string shadow)


## DesignExport (class)

- static string Export(Control root, string name, int winW, int winH)

- static string ExportHtml(Control root, string name, int winW, int winH)

- static JsonValue NodeJson(Control c)

- static string StyleJson(Control c)

- static JsonValue EnumProps(Control c)


## DesignMoveStart (class)

- FormField field;

- int x;

- int y;

- DesignMoveStart(FormField field, int x, int y)


## DesignPreviewEvents (class)

- string last;

- void Note(string evt, string handler, Control sender)


## DesignPreviewNode (class)

- FormField field;

- Control control;

- int originX;

- int originY;

- int insetX;

- int insetY;

- bool visible;

- DesignPreviewNode(FormField field, Control control)


## Designer (class)

- int RibbonH(App app)

- void RenderRibbon(App app, Canvas c, Theme t, int x, int y, int w)

- void RunRibbon(App app, string act)

- void ZoomStep(int dir)

- List<int> ZoomPresets()

- List<string> ZoomMenuLabels()

- void AlignSelGroup(int mode)

- void AlignSel(int mode)

- void RenderForm(App app, Canvas c, Theme t, int cx, int cy, int cw, int ch)

- int DesignDpi()

- int DesignLogical(int px)

- int DesignDevice(int logical)

- int DesignViewPx(int px, int psMilli)

- int DesignPointerPx(int px, int origin, int psMilli)

- bool DesignVisible(FormField f)

- Rect DesignViewRect(FormField f, int ox, int oy, int psMilli)

- bool DesignCanContain(FormField f)

- FormField DesignOwnerIn(List<FormField> lst, FormField target)

- FormField DesignOwnerOf(FormField f)

- bool DesignInSubtree(FormField owner, FormField inner)

- int DesignActiveSlot(FormField f)

- Control DesignSlot(FormField cont, int slot)

- Rect DesignContentRect(Control host)

- int DesignSlotOriginX(Control host)

- int DesignSlotOriginY(Control host)

- Control DesignParentHost(FormField f)

- int DesignEditOriginX(FormField f)

- int DesignEditOriginY(FormField f)

- int DesignWorldX(FormField f)

- int DesignWorldY(FormField f)

- void DesignMakeFloat(FormField f)

- void DesignDeclareRect(FormField f)

- int designResizeLeft;

- int designResizeTop;

- int designResizeRight;

- int designResizeBottom;

- int designResizeMouseX;

- int designResizeMouseY;

- bool designResizeMoved;

- int designDropSlot;

- int designDragLeft;

- int designDragTop;

- int designDragWidth;

- int designDragHeight;

- List<DesignMoveStart> designMoves;

- void DesignBeginMove()

- void FlowDropTarget(App app, Canvas c, Theme t, int ox, int oy, int psMilli, int dw)

- FormField TopLevelOf(FormField f)

- void DropField()

- void ApplyDesignSize(int newW, int newH)

- void RenderDesignOverlay(App app, Canvas c, FormField f, int ox, int oy, int psMilli)

- static int TabPageCount(FormField f)

- static int TabActivePage(FormField f)

- static string TabPageTitle(FormField f, int i)

- void DesignTabHit(FormField f, int page, int x, int y, int w, int h, int ox, int oy, int psMilli)

- void RegisterDesignTabs(int ox, int oy, int psMilli)

- TabHeaderHit PickTabHeader(int mx, int my, int ox, int oy, int psMilli)

- FormField DesignPickDevice(int x, int y)

- FormField DesignPickView(int x, int y, int ox, int oy, int psMilli)

- FormField FreePick(List<FormField> lst, int mx, int my, int px, int py)

- int AbsOriginX(FormField f)

- int AbsOriginY(FormField f)

- void SyncFreeSelBase()

- bool IsAncestorOf(FormField anc, FormField f)

- int ContOriginX(FormField cont)

- int ContOriginY(FormField cont)

- void FreeReparentSel(FormField cont)

- void DrawFreeDropHi(App app, Canvas c, int ox, int oy, int psMilli, FormField hi)

- void MarqueeCollect(List<FormField> lst, int selL, int selT, int selR, int selB, List<FormField> got)

- bool HasAncestorIn(FormField f, List<FormField> got)

- static int FreeHandleX(int hn, int ex, int ew)

- static int FreeHandleY(int hn, int ey, int eh)

- int FreeSnapTol(App app, int psMilli)

- List<int> FreeSnapLines(bool horiz, FormField self)

- static List<int> SnapAxis(int start, int size, List<int> lines, int tol)

- void RenderDesignCanvas(App app, Canvas c, Theme t, int cx, int cy, int cw, int ch)

- Control DesignDropHit(Control ctl, int x, int y)

- FormField FreeDropContainerDevice(int x, int y)

- void PlaceFree(FormField f, FormField cont, int lx, int ly)

- void DropFreePalette(int dx, int dy, int deviceX, int deviceY)

- static int DefaultFreeW(FormField f)

- static int DefaultFreeH(FormField f)

- void DrawLabel(App app, Canvas c, Theme t, FormField f, int lx, int ly, int lw, int align)

- int PvS(App app, int v)

- int PvFont(int fsz)

- void ApplyPreviewFormSize(Control ctl, int size)

- void PreviewControl(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail)

- void PreviewDisplay(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail, int line)

- void PvCtrlAuto(App app, Control ctl, int x, int y, int w, int h)

- void PvCtrl(App app, Control ctl, int x, int y, int w, int h)

- void PvCtrlPrepared(App app, Control ctl, int x, int y, int w, int h)

- void BeginCtrlZoom(App app)

- void EndCtrlZoom(App app)

- bool PvBasic(App app, FormField f, int x, int y, int w, int avail, int line)

- static string ChartKindAlias(int k)

- void PreviewChart(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail)

- void PreviewTable(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail)

- void PreviewChoice(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int line)

- void Box(App app, Canvas c, Theme t, int x, int y, int w, int h)


## Designer (class)

- string SaveHtml()

- void LoadHtmlText(string s)


## Designer (class)

- void RenderInspector(App app, Canvas c, Theme t, int x, int y, int w, int h)

- string undoSession;

- bool undoSessionPushed;

- void GateInspectorUndo(string sessionKey)

- void BarrierInspectorUndo()

- void SyncEditors(FormField f)

- static string SuggestHandler(FormField f, string ev)

- List<PropSpec> FieldSpecs(FormField f)

- int RenderFieldProps(App app, Canvas c, Theme t, FormField f, int ix, int iy, int iw)

- void RemoveOptionFixup(FormField f, int oi)

- static string ColTitle(string opt)

- static string ColField(string opt)

- static int IndexOfColon(string s)

- static List<string> SplitDecls(string s)

- List<PropSpec> WindowSpecs()

- int RenderWindowProps(App app, Canvas c, Theme t, int ix, int iy, int iw)

- void ApplyDevice(DeviceProfile dp)

- int RenderEventsTab(App app, Canvas c, Theme t, int ix, int iy, int iw)

- static List<string> WindowEventNames()

- string WinGetHandler(string ev)

- void WinSetHandler(string ev, string h)

- static string EventCategory(string ev)

- static string GroupLabel(string grp)

- static List<string> EventGroups()

- static bool Contains(string hay, string needle)

- void RenderStatus(App app, Canvas c, Theme t, int x, int y, int w)

- string SaveJson()

- JsonValue ShapeJson()

- string ShapeSpecFromJson(JsonValue arr)

- static string PreparedShapeSpec(JsonValue arr)

- JsonValue RemapDesignContent(JsonValue content, JsonValue sourceKids, List<FormField> currentKids)

- static JsonValue PreparedRemapContent(JsonValue content, JsonValue sourceKids, List<FormField> currentKids)

- void ReorderDesignContent(FormField owner, List<FormField> before, List<FormField> after)

- JsonValue FieldJson(FormField f)

- static JsonValue PreparedFieldJson(FormField f)

- void ApplyJson(string s)

- void LoadJson(string s)

- void ResetHistoryLocked()

- bool loadError;

- string lastError;

- List<FormField> ParseComponentFields(string s)

- void BuildFieldsFromJson(JsonValue arr, List<FormField> dst)

- static void BuildPreparedFields(JsonValue arr, List<FormField> dst)

- static bool RetargetDoc(JsonValue doc, int devW, int devH)

- static bool RetargetDoc(JsonValue doc, int devW, int devH, string devId)

- static void FitFieldsToCanvas(JsonValue fields, int boxW, int boxH, int margin)

- FormField FieldFromJson(JsonValue o)

- static FormField PreparedFieldFromJson(JsonValue o)

- static JsonValue UnmodeledKeys(JsonValue o)

- static bool IsModeledKey(string k)

- int CountFields(List<FormField> lst)

- static int CountPreparedFields(List<FormField> lst)

- bool UsedFieldName(FormField except, string nn)

- bool FieldNameIn(List<FormField> lst, FormField except, string nn)


## Designer (class)

- DesignerDocumentSnapshot CaptureDocumentOptions()

- DesignerDocumentSnapshot CaptureDocumentSnapshot()

- static JsonValue CopyDocumentSource(JsonValue source, List<DesignerSourceCopy> copies)

- static List<FormField> CopyDocumentFields(List<FormField> source, List<DesignerSourceCopy> copies)

- static DesignerDocumentSnapshot PrepareJson(string text, DesignerDocumentSnapshot options)

- bool ApplyPreparedDocument(DesignerDocumentSnapshot s)

- bool DocumentMatchesSnapshot(DesignerDocumentSnapshot s)

- static bool DocumentJsonEqual(JsonValue a, JsonValue b)

- static bool DocumentFieldsEqual(List<FormField> a, List<FormField> b)


## Designer (class)

- DesignPreviewEvents pvEvents;

- long pvInputSeq;

- bool pvInputSeen;

- bool pvInputFocused;

- WindowShapeMask pvShapeMask;

- string pvShapeImage;

- bool pvShapeReady;

- void BindDesignPreviewEvents(Control root, DesignPreviewEvents events)

- void RenderDesignPreview()

- void ProcessDesignPreviewInput(App host, int x, int y, bool inside)

- DesignPreviewNode PreviewNode(FormField field)

- bool PreviewHasLayout()

- void CollectDesignPreview(JsonValue arr, List<FormField> model, List<Control> prepared, List<DesignPreviewNode> nodes)

- int DesignTargetDpi(App host)

- List<int> DpiPresets()

- List<string> DpiMenuLabels()

- void SetDesignTargetDpi(App host, int dpi)

- bool PrepareDesignPreview(App host)

- void UpdateDesignPreviewLayout()

- string DesignPreviewProbe(int x, int y, int width, int height)

- void PaintDesignPreview(Canvas canvas, int x, int y, int width, int height)

- ~Designer()

- void DisposeDesignPreview()


## Designer (class)

- static string lang="en";

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

- List<PaletteItem> palItems;

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

- int targetDpi;

- bool dpiMenuOpen;

- int dpiMenuX;

- int dpiMenuY;

- bool dpiMenuArm;

- bool freeDragging;

- bool freeResizing;

- int freeResizeH;

- int freeDragDx;

- int freeDragDy;

- int freeDragStartX;

- int freeDragStartY;

- bool freeDragHasMoved;

- bool canvasMenuOpen;

- int canvasMenuX;

- int canvasMenuY;

- bool canvasMenuArm;

- FormField canvasMenuHit;

- int freeGuideX;

- int freeGuideY;

- FormField hoverField;

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

- List<string> undoStack;

- List<string> redoStack;

- List<string> clipFields;

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

- ThemeMetricsSnapshot czSnapshot;

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

- Input optInput;

- Input optFieldInput;

- Input evtInput;

- string evtEditing;

- Input formNameInput;

- Input submitInput;

- string winTitle;

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

- int winShadow;

- string docRole;

- int winPosX;

- int winPosY;

- string device;

- int devSelIdx;

- int devOrient;

- List<EventBinding> winEvents;

- string winEvtEditing;

- Input filterInput;

- int inspScroll;

- int inspContentH;

- List<string> bindVars;

- bool bindPickOpen;

- TextArea jsonArea;

- bool jsonOpen;

- bool evtRootPrev;

- FormField evtSelPrev;

- List<UserComponent> userComps;

- bool saveCompRequested;

- List<PreviewControlCache> pvCompCache;

- int pvCompGen;

- JsonValue docSource;

- string pvDocKey;

- Control pvDocRoot;

- List<DesignPreviewNode> pvDocNodes;

- Form pvDocForm;

- int pvDocDpi;

- bool pvDocDirty;

- void MarkDocDirty()

- string compPropFor;

- List<CompPropDecl> compPropDecls;

- string compPropEditFor;

- List<CompPropEditRow> compPropEditRows;

- string styleEditFor;

- List<Input> styleInputs;

- FormField rowsEdField;

- FormField propsEdField;

- int rowsEdSel;

- int rowsEdSelPrev;

- int propsEdSel;

- int propsEdSelPrev;

- int rowsEdGen;

- int propsEdGen;

- Input rowsFilter;

- Input propsFilter;

- int rowsScroll;

- int propsScroll;

- string rowEdTitle;

- string rowEdField;

- bool rowEdRight;

- bool rowEdSep;

- string propEdKey;

- string propEdVal;

- int colEdWidth;

- int colEdAlign;

- int colEdType;

- bool colEdSortable;

- bool colEdResizable;

- bool colEdEditable;

- int colEdDecimals;

- string colEdMoney;

- bool colEdPercent;

- string colEdPrefix;

- string colEdSuffix;

- bool colEdGrouped;

- int colEdSummary;

- int colEdPin;

- string colEdBand;

- string colEdClick;

- string ofEd;

- PropertyGrid edGridRows;

- PropertyGrid edGridProps;

- Input iconHolder;

- Input iconPickTarget;

- Input iconPickSearch;

- LayerState rowsWin;

- LayerState propsWin;

- LayerState iconWin;

- bool openCompRequested;

- string openCompName;

- void SetLayoutMode(int mode)

- void SeedFreeBounds()

- static string T(string en, string zh)

- Designer()

- void ScrollPaletteToEnd()

- void Seed(App app)

- bool FieldAlive(List<FormField> lst, FormField f)

- void DropDeadRefs()

- void AddField(int ftype)

- void SetBindVars(List<string> vars)

- void SetUserComponents(List<UserComponent> comps)

- string UserCompJson(string name)

- bool ConsumeSaveComponent()

- string FormNameText()

- string ConsumeOpenComponent()

- void AddUserComponent(int k)

- void UnpackUserComponent(FormField f)

- void EnsureCompPropDecls(string compName)

- static string CompPropValue(FormField f, string key, string def)

- static void SetCompPropValue(FormField f, string key, string val)

- static int AlertLevelOf(string spec)

- static bool RenameCompProp(FormField f, string from, string to)

- static void DropCompProp(FormField f, string key)

- static bool IsBoolProp(string key, string label, string val)

- static string CleanPropLabel(string rawLabel)

- static List<string> BuiltinPropRows(string kind)

- static void ApplyFieldProps(Control ctl, FormField f)

- void ApplyCompProps(Control root, JsonValue compRoot, FormField f)

- Control PvCompCtl(App app, FormField f, int w, int h)

- void WirePreviewEvents(App app, Control root)

- void PvFlash(App app, string evt, string hname, Control sender)

- void AddCustomField(string kind)

- bool InSubtree(FormField owner, FormField inner)

- bool ContainsRef(List<FormField> lst, FormField target)

- int IndexIn(List<FormField> lst, FormField f)

- FormField ParentContainerOf(FormField target)

- FormField FindOwner(List<FormField> lst, FormField target)

- List<FormField> ListRemove(List<FormField> lst, FormField target)

- List<FormField> ListMove(List<FormField> lst, int from, int to)

- void MoveSel(int delta)

- void InsertFieldAt(int at, FormField nf)

- bool IsExtraSel(FormField f)

- void ToggleExtraSel(FormField f)

- void ClearExtraSel()

- void SizeSelGroup(int mode)

- void SelToEdge(int top)

- void DuplicateSel()

- void PushUndo()

- void PopUndoIfUnchanged()

- void Undo()

- void Redo()

- void CopySel()

- void RenameFresh(FormField f)

- void PasteClip()

- void NudgeSel(int dx, int dy)

- void NudgeOne(FormField f, int dx, int dy)

- void ToggleLockSel()

- void DeleteSelGroup()

- void DeleteSel()

- void CutSel()

- void BringToFront()

- void SendToBack()

- void SetSelLayoutMode(int posMode, int anchorMode)

- List<string> CanvasMenuLabels()

- void HandleCanvasMenu(App app, int cc)

- void Render(App app, int x, int y, int w, int h)

- void RenderHostPanels(App app, Rect toolRect, Rect propRect)

- void RenderPinned(App app, int x, int y, int w, int h)

- void RenderJsonPanel(App app, Canvas c, Theme t, int x, int y, int w, int h)

- void RenderPalette(App app, Canvas c, Theme t, int px, int py, int pw, int ph)

- void EnsurePalette()

- void BuildPalette(string q)

- void PalGroup(string title, int lo, int hi)

- void PalDir(string title)

- void PalLeaf(string label, string icon, int kind, string custom, int depth)

- int PalLeafRow()

- int UserCompIndex(string name)

- void PalActivate()

- void PalClickRelease()

- void PalPress()

- void PalFoldNote(bool folded)

- bool MatchesFilter(int ft, string q)

- bool PalFolded(string title)

- void PalToggleFold(string title)

- static List<string> ListRemoveStr(List<string> src, int idx)

- bool InRect(int mx, int my, int x, int y, int w, int h)

- void RenderTray(App app, Canvas c, Theme t, int x, int y, int w, int h)

- int ClampSpan(int sp)

- string FieldDesc(int ft)

- string FieldIcon(int ft)

- int SmallBtn(App app, string text, int type, int x, int y, int w)

- int SmallIconBtn(App app, string iconName, int type, int x, int y, int w)

- bool OpenRowsEditor(App app, FormField f)

- void OpenPropsEditor(App app, FormField f)

- LayerState NewEditorWindow(App app, int w, int h, string title)

- bool BigEditorOpen()

- void CloseBigEditors()

- void LoadRowScratch(FormField f, int i)

- string JoinRowOption(FormField f)

- List<PropSpec> RowSpecs(FormField f)

- List<PropSpec> PropSpecs()

- void LoadPropScratch(FormField f, int i)

- void WriteBackProps(App app, FormField f)

- void EnsureSelVisible(int count, int slot, int viewH)

- void EnsurePropSelVisible(int count, int slot, int viewH)

- static JsonValue ColEntry(FormField f, int i)

- static JsonValue ColsArray(FormField f)

- static void DropKey(JsonValue o, string key)

- void LoadColScratch(FormField f, int i)

- static bool ColBool(JsonValue e, string key)

- List<JsonProperty> ColDesired()

- void WriteBackCol(App app, FormField f)

- static void MoveColEntry(FormField f, int from, int to)

- static void RemoveColEntry(FormField f, int i)

- static void CopyColEntry(FormField f, int from, int to)

- static JsonValue NormalizedColumns(FormField f, JsonValue cols)

- void RenderRowsEditor(App app, Canvas c, Theme t)

- void OpenIconPicker(App app, Input target)

- void RenderIconPicker(App app, Canvas c, Theme t)

- static bool NameHas(string hay, string sub)

- static string RowMainText(FormField f, int i)

- static string RowSubText(FormField f, int i)

- static bool RowIsSep(FormField f, int i)

- static bool HasCompProp(FormField f, string key)

- void RenderPropsEditor(App app, Canvas c, Theme t)


## DesignerDocumentSnapshot (class)

- string name;

- string submit;

- string docRole;

- string device;

- int labelPos;

- int formSize;

- int labelWidth;

- bool hideStar;

- bool showSubmit;

- bool showReset;

- int winW;

- int winH;

- string winTitle;

- bool winResizable;

- bool winTool;

- bool winCenter;

- int winPosX;

- int winPosY;

- int layoutMode;

- int winZoom;

- bool winRound;

- int winShape;

- string winShapeSpec;

- int winShapeRadius;

- bool winChrome;

- bool winGlass;

- int winOpacity;

- int winShadow;

- JsonValue docSource;

- List<FormField> fields=new List<FormField>();

- int sequence;

- bool loadError;

- string lastError="";

- bool applied;

- string ToJson()

- string ToHtml()

- DesignerDocumentSnapshot Copy()


## DesignerSourceCopy (class)

- JsonValue source;

- JsonValue copy;

- DesignerSourceCopy(JsonValue source, JsonValue copy)


## PaletteItem (class)

- int kind;

- string custom;

- PaletteItem(int kind, string custom)


## PreviewControlCache (class)

- string key;

- Control control;

- PreviewControlCache(string key, Control control)


## RibbonAlignCommand (class)

- public string icon;

- public string label;

- public string tip;

- public string act;

- public bool requiresFree;

- public RibbonAlignCommand(string icon, string label, string tip, string act, bool requiresFree)


## TabHeaderHit (class)

- FormField tabs;

- int page;

- int x;

- int y;

- int w;

- int h;

- TabHeaderHit(FormField tabs, int page, int x, int y, int w, int h)


## UserComponent (class)

- string name;

- string json;

- UserComponent(string name, string json)
