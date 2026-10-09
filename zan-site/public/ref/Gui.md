# Gui

> 源码: `packages/Zan.Gui/src/Gui/Component/BandGrid.zan`, `packages/Zan.Gui/src/Gui/Core/App.zan`, `packages/Zan.Gui/src/Gui/Core/ChildWindow.zan`, `packages/Zan.Gui/src/Gui/Core/Control.zan`, `packages/Zan.Gui/src/Gui/Core/ControlBootstrap.zan`, `packages/Zan.Gui/src/Gui/Core/ControlFactory.zan`, `packages/Zan.Gui/src/Gui/Core/DamageTracker.zan`, `packages/Zan.Gui/src/Gui/Core/Device.zan`, `packages/Zan.Gui/src/Gui/Core/Element.zan`, `packages/Zan.Gui/src/Gui/Core/Event.zan`, `packages/Zan.Gui/src/Gui/Core/Focus.zan`, `packages/Zan.Gui/src/Gui/Core/HandlerRegistry.zan`, `packages/Zan.Gui/src/Gui/Core/HeavyControls.zan`, `packages/Zan.Gui/src/Gui/Core/HitTest.zan`, `packages/Zan.Gui/src/Gui/Core/Menu.zan`, `packages/Zan.Gui/src/Gui/Core/NativeLayer.zan`, `packages/Zan.Gui/src/Gui/Core/Nav.zan`, `packages/Zan.Gui/src/Gui/Core/OverlayPopup.zan`, `packages/Zan.Gui/src/Gui/Core/PropSpec.zan`, `packages/Zan.Gui/src/Gui/Core/Reactive.zan`, `packages/Zan.Gui/src/Gui/Core/Serialize.zan`, `packages/Zan.Gui/src/Gui/Core/Types.zan`, `packages/Zan.Gui/src/Gui/Core/Ui.zan`, `packages/Zan.Gui/src/Gui/Core/UiErrorLog.zan`, `packages/Zan.Gui/src/Gui/Core/UserComponents.zan`, `packages/Zan.Gui/src/Gui/Core/WindowShapeMask.zan`, `packages/Zan.Gui/src/Gui/Layout/CssGrid.zan`, `packages/Zan.Gui/src/Gui/Layout/Layout.zan`, `packages/Zan.Gui/src/Gui/Layout/LineBox.zan`, `packages/Zan.Gui/src/Gui/Layout/Scroll.zan`, `packages/Zan.Gui/src/Gui/Layout/Stack.zan`, `packages/Zan.Gui/src/Gui/Markup/Html.zan`, `packages/Zan.Gui/src/Gui/Markup/HtmlApi.zan`, `packages/Zan.Gui/src/Gui/Media/Icon.zan`, `packages/Zan.Gui/src/Gui/Media/IconSvg.zan`, `packages/Zan.Gui/src/Gui/Media/IconSvgData.zan`, `packages/Zan.Gui/src/Gui/Media/IconVector.zan`, `packages/Zan.Gui/src/Gui/Media/ImageHttp.zan`, `packages/Zan.Gui/src/Gui/Rendering/Fx.zan`, `packages/Zan.Gui/src/Gui/Rendering/Math3D.zan`, `packages/Zan.Gui/src/Gui/Rendering/Render.zan`, `packages/Zan.Gui/src/Gui/Rendering/RenderAA.zan`, `packages/Zan.Gui/src/Gui/Rendering/SpriteBatch.zan`, `packages/Zan.Gui/src/Gui/Styling/Css.zan`, `packages/Zan.Gui/src/Gui/Styling/DesignTokens.zan`, `packages/Zan.Gui/src/Gui/Styling/Effects.zan`, `packages/Zan.Gui/src/Gui/Styling/Skin.zan`, `packages/Zan.Gui/src/Gui/Styling/Style.zan`, `packages/Zan.Gui/src/Gui/Styling/StyleBox.zan`, `packages/Zan.Gui/src/Gui/Styling/StyleSheet.zan`, `packages/Zan.Gui/src/Gui/Styling/Theme.zan`, `packages/Zan.Gui/src/Gui/Text/RichText.zan`, `packages/Zan.Gui/src/Gui/Text/Text.zan`


## AnimSlot (class)

- int key;

- int cur;

- int dst;

- int src;

- int startMs;

- int dur;

- AnimSlot(int k, int c, int d, int s, int st, int du)


## App (class)

- Window window;

- Canvas canvas;

- Theme theme;

- FocusManager focus;

- HitTester hitTester;

- bool isRunning;

- bool needsRedraw;

- bool pollPending;

- int lastPollResult;

- int frameErrors;

- string lastFrameError;

- FrameBody frameBody;

- static App frameApp;

- static App FrameApp()

- static bool wndProcPainting;

- int frameCount;

- int mouseX;

- int mouseY;

- int scrollY;

- int contentHeight;

- int scrollSpeed;

- int dpiScale;

- int designW;

- int designH;

- bool physicalStage;

- int titlebarH;

- int captionBtnW;

- int[]capActionIds;

- string[]capActionLabels;

- string chromeBrandIcon;

- int splitX;

- int sidebarScrollY;

- int sidebarContentHeight;

- bool isDark;

- bool isTopmost;

- bool showThemeButton;

- bool showPinButton;

- bool showMinimizeButton;

- bool showMaximizeButton;

- bool showCloseButton;

- int capTipId;

- int capTipX;

- string themeTipText;

- string pinTipText;

- string minimizeTipText;

- string maximizeTipText;

- string restoreTipText;

- string closeTipText;

- List<string> themeNames;

- SignalInt themeMenuModel;

- SignalBool themeMenuOpen;

- SignalInt themeMenuScroll;

- int themeMenuBaseId;

- int themeMenuTriggerId;

- UiEvent themeMenuChange;

- int fxKindOverride;

- int accentOverride;

- SignalInt themeDrawerTab;

- int density;

- ThemeMetricsSnapshot densitySnapshot;

- List<int> densityBase;

- bool hasSkinDensity;

- bool autoSkins;

- int appliedSkin;

- List<string> skinPacks;

- string skinArt;

- int skinArtOpacity;

- StyleSheet sheet;

- Dict <int, StyleSheet> baseSheets;

- int baseSheetGen;

- string appCss;

- int htmlLoadSeq;

- static LinkNavigateFn linkNavigator;

- StyleSheet chartSheet;

- string chartThemeName;

- string skinName;

- StyleSheet useCssSheet;

- bool reducedMotion;

- Control styleCtx;

- bool glassNative;

- bool windowRound;

- bool windowRoundSet;

- int windowOpacity;

- bool windowResizable;

- int shapeMode;

- int shapeRadius;

- string shapeSpec;

- int shapeShadow;

- List<WindowShapeRegion> shapeRegions;

- int shapeOffX;

- int shapeOffY;

- bool showChrome;

- string chromeStatus;

- string chromeAccent;

- int wheelCapX;

- int wheelCapY;

- int wheelCapW;

- int wheelCapH;

- int wheelClaimSeq;

- int wheelCapOrder;

- int wheelOwnerOrder;

- bool wheelClaimSeen;

- bool wheelConsumed;

- bool wheelReplayArmed;

- int tabHoldId;

- bool blocksInputBelow;

- bool inputBlockPrevious;

- bool menuBlock;

- bool menuBlockPrevious;

- bool clickClaimed;

- int frameSeq;

- int clickSeenSeq;

- long clickEvSeq;

- int clickTargetId;

- int pressTargetId;

- bool pointerDown;

- bool pressOnBlocker;

- int rightClickTargetId;

- int rightPressTargetId;

- int lastClickId;

- int lastClickMs;

- bool doubleClickNow;

- int pressStartX;

- int pressStartY;

- int pressStartMs;

- int pressStartId;

- bool longPressFired;

- int swipeDir;

- int dragClaimId;

- List<OverlayPopup> overlays;

- List<NativeLayerReq> nativeLayers;

- List<int> nativeOccluders;

- List<int> hitOnlyBlockers;

- int lastResizeBg;

- List<AnimSlot> anims;

- int nowMs;

- int lastFrameMs;

- bool freezeAnimClock;

- static int perfMode;

- bool partialFrames;

- bool scrollHoverDirty;

- int renderBackendMode;

- int perfFrames;

- int perfRenderMs;

- int perfPresentMs;

- int perfPartFrames;

- int perfPartRenderMs;

- int perfPartPresentMs;

- int perfWorstMs;

- int perfSlowFrames;

- int perfStyleAt;

- int perfStyleMissAt;

- int perfPxFillAt;

- int perfPxBlendAt;

- int perfPxBlurAt;

- int perfPxGlyphAt;

- int perfPxSnapAt;

- int phaseAtUs;

- int phaseBgUs;

- int phaseMeasUs;

- int phaseArrUs;

- int phaseTreeUs;

- int scopeId;

- int scopeAtUs;

- int scopeEndUs;

- int scopeEndId;

- string phaseSecTop;

- int phaseSecTopUs;

- int markAtUs;

- string phaseMarkTop;

- int phaseMarkTopUs;

- string markNow;

- string redrawWhy;

- int redrawCount;

- string frameWhy;

- int frameWhyN;

- List<WhyTally> fullWhy;

- string phaseTopNode;

- int phaseTopUs;

- int perfBgUs;

- int perfMeasUs;

- int perfArrUs;

- int perfTreeUs;

- int perfBeginMs;

- int perfFxMs;

- int perfOverlayMs;

- int lastPresentMs;

- int lastFullFrameMs;

- int perfLoopAt;

- int perfLoops;

- int perfPollPath;

- int perfAnimPath;

- int perfWaitPath;

- int perfEvNone;

- int perfEvMove;

- int perfEvOther;

- int perfLoopMs;

- int perfFxTicks;

- int perfFxOnlyTicks;

- int perfFxTickMs;

- int perfFxTickUs;

- int perfFxRestoreUs;

- int perfFxRenderUs;

- int perfFxPresentUs;

- int lastInputMs;

- int lastInX;

- int lastInY;

- int animNextMs;

- int wakeNextMs;

- int wakeFrameNextMs;

- bool animRectValid;

- bool animRectAll;

- bool inputNeedsFrame;

- bool wheelScrollInput;

- int animRX;

- int animRY;

- int animRW;

- int animRH;

- bool animPrevValid;

- int animPX;

- int animPY;

- int animPW;

- int animPH;

- bool backdropDirty;

- int blurSlotSeq;

- List<BlurSlot> blurSlots;

- int bgThemeGen;

- int bgSnapGen;

- Dict <string, StyleBox> styleCache;

- int styleCacheGen;

- int metricsScale;

- string bgImagePath;

- int bgImageOpacity;

- string userWallpaperPath;

- int userWallpaperOpacity;

- bool bgImagePickWanted;

- bool fxEnabled;

- int fxNextMs;

- bool fxSnapValid;

- int fxFrameMs;

- bool partialPending;

- bool partialFrame;

- bool partialBlocked;

- bool forceFullNext;

- int dmgX;

- int dmgY;

- int dmgW;

- int dmgH;

- int presentX;

- int presentY;

- int presentW;

- int presentH;

- List<Rect> glassPanels;

- List<int> glassRects;

- int scrollBusyUntilMs;

- bool glassDeferred;

- bool glassRefreshOwe;

- int glassRefreshArmedAt;

- List<Rect> glassDeferredPanels;

- List<int> glassDeferredRects;

- int blurMissStreak;

- List<int> fxDamage;

- List<int> fxTouched;

- bool fxTouchedFull;

- App(string title, int width, int height):this(title, width, height, false)

- App(string title, int width, int height, bool physicalStage):this(title, width, height, physicalStage, false)

- static App CreateOffscreen(string title, int width, int height)

- void SetOffscreenViewport(int width, int height, int scale)

- App(string title, int width, int height, bool physicalStage, bool offscreen)

- static App CreateDark(string title, int width, int height)

- static App CreateDarkStage(string title, int width, int height)

- void SetDark(bool dark)

- void ApplyTheme(Theme t, bool dark)

- void ScaleThemeMetrics()

- void SetDensity(int d)

- int Density()

- string DensityLabel()

- bool UseSkin(string name)

- bool ReloadSkin()

- Control LoadHtml(string html)

- Control LoadHtmlWith(string html, HtmlHandlers handlers, string baseDir)

- static HtmlLoadFn htmlLoader;

- static void SetHtmlLoader(HtmlLoadFn fn)

- static LinkScanFn linkScanner;

- static void SetLinkScanner(LinkScanFn fn)

- static void AutoLinkTree(Control root, App app)

- static Control CloneTree(Control root)

- static CloneTreeFn cloneTree;

- static void SetCloneTree(CloneTreeFn fn)

- static List<AppHookFn> presentTails;

- static void AddPresentTail(AppHookFn fn)

- string NextLoadScope()

- void ApplyScopedTree(Control root, string scopedCss, string zs)

- void OpenLink(string url, bool newWindow)

- static void OpenInSystemBrowser(string url)

- static void SetLinkNavigator(LinkNavigateFn fn)

- void MarkScope(Control n, string zs)

- void UseAppCss(string css)

- void SetChartSheet(StyleSheet s, string pkgName)

- string SkinName()

- string AppCss()

- string ChartThemeName()

- void ApplyAppCss()

- void UseCss(string css)

- int GlassTint()

- void EnableSkins(int index)

- void EnableSkins()

- int SkinCount()

- string SkinNameAt(int i)

- int SkinIndexOf(string name)

- void ApplySkinName(string name)

- Theme SkinThemeAt(int i)

- void ApplySkin(int i)

- int SkinDensity()

- void SyncSkinDensity()

- void SyncSkin()

- void ToggleThemeMenu(int triggerId)

- void RenderThemeMenuOverlay()

- void SetWindowOpacity(int percent)

- void SetResizable(bool on)

- bool Resizable()

- int WindowOpacity()

- void SetWindowShape(string spec)

- void SetWindowShadow(int px)

- bool HasShadowBand()

- void ParseShapeSpec(string spec)

- int WindowShadow()

- string WindowShapeSpec()

- int WindowShape()

- int WindowShapeRadius()

- int EffectiveCornerRadius()

- void SetChromeVisible(bool on)

- bool ChromeVisible()

- void SetChromeStatus(string text, string accent)

- void SetChromeBrandIcon(string icon)

- void SetTitlebarHeight(int devicePx)

- void SetCaptionActions(int[]ids, string[]labels)

- string ChromeStatus()

- string ChromeAccent()

- void SetWindowTitle(string text)

- void SetNativeGlass(bool on)

- bool NativeGlass()

- void SetWindowRoundCorners(bool on)

- void ApplyRoundCorners()

- void SetToolWindow(bool on)

- bool ToolWindow()

- void SetWallpaper(string path, int opacity)

- string UserWallpaperPath()

- int UserWallpaperOpacity()

- void SetUserWallpaper(string path, int opacity)

- static int WithAlpha(int color, int alpha)

- void ApplySurfaceTranslucency()

- bool SkinArtWallpaper()

- bool HasUserWallpaper()

- bool IsSkinArtPath(string path)

- void ClearWallpaper()

- string WallpaperPath()

- int WallpaperOpacity()

- void RequestWallpaperPick()

- bool ConsumeWallpaperPick()

- void RenderWallpaperImage(int x, int y, int w, int h)

- void BackdropBlob(int cx, int cy, int r, int cr, int cg, int cb)

- void BackdropDrift(int cx, int cy, int r, int color)

- void BackdropGradientWallpaper(int ax, int ay, int aw, int ah)

- void BackdropGlassWallpaper(int ax, int ay, int aw, int ah)

- void RenderBackground(int x, int y, int w, int h, int slot)

- void PaintShapeFill()

- void FillEllipse(int x, int y, int w, int h, int col)

- static int ISqrt(int v)

- void PaintShapeShadow()

- int EffectiveFx()

- bool FxActive()

- void SetBackdropFx(bool on)

- void SetFxKind(int kind)

- bool FxPresent()

- int FxIntervalMs()

- void NoteHoverDamage(int oldId, int newId)

- bool TakeDueWheelRect()

- void BeginIdScope(int first)

- void EndIdScope()

- void PushId(string strKey)

- void PushId(int intKey)

- void PopId()

- int GetId(string strKey)

- int GetId(int intKey)

- void NoteGlassRect(int x, int y, int w, int h)

- bool WidenDamageToGlass()

- void SettleGlassRefresh()

- void NoteDamage(int x, int y, int w, int h)

- void NoteScrollDamage(int x, int y, int w, int h)

- void NotePageScrollDamage()

- void CancelPartialFrame()

- void MarkPresentDirty(List<int> rects)

- int ChromeButtonCount()

- void SetChromeButtons(bool themeButton, bool pinButton, bool minimizeButton, bool maximizeButton)

- bool CaptionTipShown(int id, int bx, int bw, int hbar)

- int CaptionTipX(int bx, int bw)

- void NoteCaptionTip(int id, int bx, int bw, int hbar)

- void SetCaptionTips(string theme, string pin, string minimize, string maximize, string restore, string close)

- void FollowCaptionTips(App src)

- UiEvent SetThemeMenu(List<string> names, int index)

- bool ThemeMenuMode()

- int ThemeMenuIndex()

- void SetThemeMenuIndex(int i)

- void SetWindowPos(int x, int y)

- void SetClientSize(int w, int h)

- void SetClientSizeDev(int wDev, int hDev)

- void CenterWindow()

- void SwapCanvas(int w, int h)

- void Show()

- void Run(string title)

- void SyncNativeDpi()

- void SyncSurface()

- void SetFrameBody(FrameBody body)

- static void PaintFromWndProc()

- void RunLoop(FrameBody body)

- static App guardApp;

- static FrameBody guardBody;

- static int guardWhat;

- static bool guardPumpAlive;

- static void GuardBody(nint arg)

- bool PumpGuarded()

- bool FrameGuarded(FrameBody body)

- static int NativeFaultCount()

- bool PumpSafe()

- bool SafeFrame(FrameBody body)

- void RefreshScrollHover()

- void NoteFrameError(string origin, string message)

- int FrameErrorCount()

- string LastFrameError()

- int ContentTop()

- int ClientWidth()

- int ClientHeight()

- void RenderChrome(string title)

- int RenderCaptionButtons(Canvas c, Theme t, int W, int hbar)

- void NoteRedrawWhy(string why)

- string RedrawWhy()

- void RequestRedraw()

- void RequestAnimationFrame(int minIntervalMs)

- void RequestWakeIn(int minIntervalMs)

- void RequestWakeAt(int dueMs)

- void RequestWakeFrameIn(int minIntervalMs)

- void RequestAnimationFrameIn(int minIntervalMs, int x, int y, int w, int h)

- bool BackdropDirty()

- bool BackdropDirtyIn(int slot, int x, int y, int w, int h)

- bool ScrollBurstActive()

- bool RectHitsDamage(int x, int y, int w, int h)

- bool RectInDamage(int x, int y, int w, int h)

- void NoteGlassDeferred(int x, int y, int w, int h)

- void ReuseBackdrop()

- bool PointerPressed()

- int NextBlurSlot()

- int BlurKeyId(int id)

- int BlurKey(int kind, int x, int y, int w, int h)

- int BlurSlotFor(int key)

- void SetImeCaret(int x, int y)

- void Post(Action handler)

- void StopLoop()

- Action closeRequest;

- bool closePending;

- void OnCloseRequest(Action handler)

- bool HandleCloseRequest()

- void RequestClose()

- void DrainPosts()

- int AnimIndex(int key)

- void AnimAdd(int key, int val, int target, int durMs)

- int StateGet(int key, int dflt)

- void StateSet(int key, int val)

- int AnimToValue(int key, int target, int durMs)

- bool AnimActive(int key)

- void AnimRestart(int key, int durMs)

- int AnimTo(int key, int target, int durMs)

- int AnimToIn(int key, int target, int durMs, int x, int y, int w, int h)

- int AnimIntroValue(int key, int durMs)

- int AnimIntro(int key, int durMs)

- int AnimIntroIn(int key, int durMs, int x, int y, int w, int h)

- int AnimPxValue(int key, int target, int tauMs)

- int AnimPx(int key, int target, int tauMs)

- int AnimPxIn(int key, int target, int tauMs, int x, int y, int w, int h)

- static int Ease(int p)

- static int Lerp(int a, int b, int p)

- static int LerpColor(int c0, int c1, int p)

- static int Darken(int color, int p)

- static int Lighten(int color, int p)

- static int ScaleAlpha(int color, int p)

- bool CaptureWheel(int x, int y, int w, int h)

- void SetContentHeight(int h)

- int ViewportHeight()

- int Scale(int v)

- int ShapeOffX()

- int ShapeOffY()

- void FillChrome(int x, int y, int w, int h, int radius, int color)

- void FillGlass(int x, int y, int w, int h, int radius, int color, int a)

- void SetTheme(Theme t)

- void FreezeAnimClock(int fixedMs)

- void UnfreezeAnimClock()

- bool TakeDueFrame(int now)

- void PumpDueFrame()

- void SetPollEventMode()

- int PollOneEvent()

- bool ProcessEvent()

- nint WindowHandle()

- bool ApplyEvent(nint evHwnd)

- void RenderFrameOverlays()

- void PresentFrame()

- void PerfLoopTick(int t0, int path)

- void PerfMaybeFlushLoop(int now)

- void PerfMark(string name)

- void NoteFullFrameWhy()

- string FullWhyLine()

- void PerfNode(string kind, string nm, int us)

- void PhaseBegin()

- void PhaseEnd(int slot)

- static string UsAvg(int us, int n)

- bool ForceFullFrames()

- bool PartialFrames()

- void SetPartialFrames(bool on)

- static bool paintHashOn;

- static long paintHashAcc;

- static long paintHashSaved;

- Dictionary <int, long> paintHashStore;

- bool PaintHashBegin()

- void PaintHashEndSelf(int id, int x, int y, int w, int h)

- static void PaintHashOp(int op, int a, int b, int c, int d)

- static void PaintHashText(int op, int a, int b, int c, int d, string text)

- static void PaintHashList(List<int> xs)

- static bool PaintHashArmed()

- int RenderBackendMode()

- string RenderBackendName()

- int SetRenderBackend(int mode)

- static int FxSnapSlot()

- static bool PerfOn()

- static string MsAvg(int total, int n)

- static string RasterLine(int frames)

- Widget.TooltipRequest offscreenTooltip;

- App offscreenFrameParent;

- bool offscreenFrameActive;

- bool offscreenWheelPending;

- int offscreenWheelX;

- int offscreenWheelY;

- int offscreenWheelKey;

- int offscreenWheelMods;

- int offscreenWheelFlag;

- long offscreenWheelSeq;

- bool ReplayOffscreenWheel()

- void ProcessOffscreenInput(int kind, int x, int y, int button, int key, int mods, int flag)

- void BeginOffscreenFrame()

- void CancelOffscreenFrame()

- void EndOffscreenFrame()

- void ForwardOffscreenFrameRequests(App host)

- void BeginFrame()

- void AddOverlay(OverlayPopup p)

- void RunOverlays()

- bool HasOverlays()

- bool PointerCaptured()

- int BlockHitsRect(int x, int y, int w, int h)

- void ReserveEdgeHit(int x, int y, int w, int h)

- void ReservePopupHit(int x, int y, int w, int h)

- int BlockHitsMenu()

- int RightHitTest(int px, int py)

- int BlockHitsBelow()

- int NormalizedKind()

- bool PressOwnsWheel()

- void ClaimDrag()

- int holdEvKind;

- nint holdEvHwnd;

- bool holdEvArmed;

- void HoldEventKind(int kind)

- int EventKind()

- bool EventIsMine()

- bool ClickFrameCurrent()

- int FrameSeq()

- bool ClickAvailable()

- void ClaimClick()

- bool ClickClaimed()

- int ClickTarget()

- int PressTarget()

- int RightClickTarget()

- int RightPressTarget()

- bool DoubleClickNow()

- void NoteClickTarget(int id)

- void NotePressStart(int id, int x, int y)

- void NotePressSentinel(int id)

- void NoteRelease(int x, int y)

- int SwipeDir()

- int SwipeTarget()

- bool TouchDevice()

- bool PollLongPress(int id)

- bool LongPressFired()

- int HeldPressMs()


## AttrCond (class)

- string name;

- string op;

- string val;

- bool nocase;


## BackdropFx (class)

- static List<int> damage;

- static int clipX;

- static int clipY;

- static int clipR;

- static int clipB;

- static List<int> TakeDamage()

- static void Mark(int x, int y, int w, int h)

- static void FR(Canvas c, int x, int y, int w, int h, int color)

- static void FC(Canvas c, int cx, int cy, int r, int color)

- static void DT(Canvas c, int x, int y, string s, int color, int fs)

- static int KindStars()

- static int KindAurora()

- static int KindLightfall()

- static int KindInk()

- static int KindPetals()

- static int KindRain()

- static int KindBokeh()

- static int KindMesh()

- static int KindFortune()

- static int KindDarkGold()

- static int KindDreamy()

- static int IntervalMs(int kind)

- static int Hash(int i, int salt)

- static int Sin1000(int deg)

- static void Render(App app, Canvas c, int kind, int x, int y, int w, int h, int now, Theme t, int dpi)

- static int Px(int v, int dpi)

- static void Stars(Canvas c, int x, int y, int w, int h, int now, int color, int dpi)

- static void AuroraGlow(Canvas c, int cx, int cy, int r, int cr, int cg, int cb)

- static void Aurora(Canvas c, int x, int y, int w, int h, int now, int dpi)

- static void Lightfall(Canvas c, int x, int y, int w, int h, int now, int colorA, int colorB, int colorC, int dpi)

- static void Ink(Canvas c, int x, int y, int w, int h, int now, int dpi)

- static void Petals(Canvas c, int x, int y, int w, int h, int now, Theme t, int dpi)

- static void BokehOrb(Canvas c, int cx, int cy, int r, int color, int alpha)

- static void Bokeh(Canvas c, int x, int y, int w, int h, int now, int colorA, int colorB, int colorC, int dpi)

- static void Mesh(Canvas c, int x, int y, int w, int h, int now, int colorA, int colorB, int dpi)

- static void Rain(Canvas c, int x, int y, int w, int h, int now, Theme t, int dpi)

- static void Fortune(Canvas c, int x, int y, int w, int h, int now, int dpi)

- static void DarkGold(Canvas c, int x, int y, int w, int h, int now, int dpi)

- static void Dreamy(Canvas c, int x, int y, int w, int h, int now, int dpi)


## BandGrid (class)

- int rows;

- int cols;

- List<BandGridCell> cells;

- List<string> rowLabels;

- List<string> bandLabels;

- int bandSpan;

- BandGridColorOf colorOf;

- int cellW;

- int cellH;

- int hoverR;

- int hoverC;

- int anchorR;

- int anchorC;

- bool dragSet;

- int ctxR;

- int ctxC;

- int actR;

- int actC;

- int barMax;

- int accent;

- int heatDeep;

- App lastApp;

- UiEvent Changed;

- UiEvent Context;

- UiEvent CellActivate;

- BandGrid()

- override string Kind()

- BandGrid Bind(int r, int c)

- BandGrid BindInit(int r, int c, int initNum)

- BandGrid RowLabels(List<string> labels)

- BandGrid BandLabels(List<string> labels, int span)

- BandGrid ColorOf(BandGridColorOf d)

- BandGrid Heat(int deepColor)

- BandGrid CellSize(int w, int h)

- BandGrid BarMax(int v)

- BandGrid Accent(int argb)

- BandGrid OnChanged(Action a)

- BandGrid OnContext(Action a)

- BandGrid OnCellActivate(Action a)

- int ContextRow()

- int ContextCol()

- int CellRow()

- int CellCol()

- string TextAt(int r, int c)

- void SetText(int r, int c, string s)

- int NumAt(int r, int c)

- void SetNum(int r, int c, int v)

- void SetAllNum(int v)

- int SelectedCount()

- void SelectAll()

- void ClearMarks()

- void SetSelectedNum(int v)

- void SetSelectedText(string s)

- void Poke()

- override void OnMeasure(App app)

- int BandH(App app)

- int LabelW(App app)

- void Damage(App app)

- override List<PropSpec> Props()

- override void OnPaint(App app)


## BandGridCell (class)

- public string text;

- public int num;

- public int mark;

- BandGridCell(string text, int num, int mark)


## BlurSlot (class)

- int key;

- int lastUse;

- int lastNew;

- BlurSlot()


## Canvas (class)

- [DllImport("zan_gui")]static extern int zan_gui_create_surface(int width, int height);

- [DllImport("zan_gui")]static extern int zan_gui_destroy_surface(int id);

- [DllImport("zan_gui")]static extern int zan_gui_surface_width(int id);

- [DllImport("zan_gui")]static extern int zan_gui_surface_height(int id);

- [DllImport("zan_gui")]static extern int zan_gui_write_pixels(int id, string path, int x, int y, int w, int h);

- [DllImport("zan_gui")]static extern void zan_gui_clear(int surfaceId, int color);

- [DllImport("zan_gui")]static extern void zan_gui_clear_rect(int surfaceId, int x, int y, int w, int h, int color);

- [DllImport("zan_gui")]static extern void zan_gui_fill_rect(int surfaceId, int x, int y, int w, int h, int color);

- [DllImport("zan_gui")]static extern int zan_gui_stat_read(int idx, int kind);

- [DllImport("zan_gui")]static extern int zan_gui_stat_top(int rank, int field);

- [DllImport("zan_gui")]static extern int zan_gui_stat_top_fill(int rank, int field);

- [DllImport("zan_gui")]static extern void zan_gui_draw_rect(int surfaceId, int x, int y, int w, int h, int color, int thickness);

- [DllImport("zan_gui")]static extern void zan_gui_fill_rounded_rect(int surfaceId, int x, int y, int w, int h, int radius, int color);

- [DllImport("zan_gui")]static extern void zan_gui_draw_rounded_rect(int surfaceId, int x, int y, int w, int h, int radius, int color, int thickness);

- [DllImport("zan_gui")]static extern void zan_gui_surface_rounded_rect(int surfaceId, int x, int y, int w, int h, int radius, int fill, int border, int thickness);

- [DllImport("zan_gui")]static extern void zan_gui_surface_rounded_rect_mask(int surfaceId, int x, int y, int w, int h, int radius, int corners, int fill, int border, int thickness);

- [DllImport("zan_gui")]static extern void zan_gui_fill_rounded_rect_mask(int surfaceId, int x, int y, int w, int h, int radius, int corners, int color);

- [DllImport("zan_gui")]static extern void zan_gui_draw_rounded_rect_mask(int surfaceId, int x, int y, int w, int h, int radius, int corners, int color, int thickness);

- [DllImport("zan_gui")]static extern void zan_gui_shadow_rounded_rect(int surfaceId, int x, int y, int w, int h, int radius, int blur, int color);

- [DllImport("zan_gui")]static extern void zan_gui_blur_rect(int surfaceId, int x, int y, int w, int h, int radius);

- [DllImport("zan_gui")]static extern void zan_gui_blur_rect_cached(int surfaceId, int x, int y, int w, int h, int radius, int slot, int dirty);

- [DllImport("zan_gui")]static extern void zan_gui_blur_round_cached(int surfaceId, int x, int y, int w, int h, int radius, int slot, int dirty, int cornerRadius, int cornerMask);

- [DllImport("zan_gui")]static extern int zan_gui_blur_partial_miss_take();

- [DllImport("zan_gui")]static extern void zan_gui_snapshot_rect(int surfaceId, int x, int y, int w, int h, int slot);

- [DllImport("zan_gui")]static extern void zan_gui_snapshot_patch_rect(int surfaceId, int x, int y, int w, int h, int slot);

- [DllImport("zan_gui")]static extern int zan_gui_restore_rect(int surfaceId, int x, int y, int w, int h, int slot);

- [DllImport("zan_gui")]static extern int zan_gui_restore_sub_rect(int surfaceId, int x, int y, int w, int h, int slot);

- [DllImport("zan_gui")]static extern void zan_gui_surface_release(int surfaceId, int slot);

- [DllImport("zan_gui")]static extern int zan_gui_surface_dump(int surfaceId, string path);

- [DllImport("zan_gui")]static extern void zan_gui_fill_vgrad(int surfaceId, int x, int y, int w, int h, int colorTop, int colorBottom);

- [DllImport("zan_gui")]static extern void zan_gui_fill_vgrad_mask(int surfaceId, int x, int y, int w, int h, int radius, int mask, int colorTop, int colorBottom);

- [DllImport("zan_gui")]static extern void zan_gui_fill_grad_mask(int surfaceId, int x, int y, int w, int h, int radius, int mask, int dir, int colorFrom, int colorVia, int colorTo);

- [DllImport("zan_gui")]static extern void zan_gui_fill_circle(int surfaceId, int cx, int cy, int radius, int color);

- [DllImport("zan_gui")]static extern void zan_gui_draw_circle(int surfaceId, int cx, int cy, int radius, int color, int thickness);

- [DllImport("zan_gui")]static extern void zan_gui_fill_radial(int surfaceId, int cx, int cy, int radius, int color, int innerAlpha);

- [DllImport("zan_gui")]static extern void zan_gui_draw_line(int surfaceId, int x0, int y0, int x1, int y1, int color, int thickness);

- [DllImport("zan_gui")]static extern void zan_gui_draw_polyline(int surfaceId, nint pts, int n, int color, int thickness);

- [DllImport("zan_gui")]static extern void zan_gui_draw_polyline_fx(int surfaceId, nint pts, int n, int color, int thickness);

- [DllImport("zan_gui")]static extern void zan_gui_draw_polybatch(int surfaceId, nint pts, nint counts, int nPaths, int color, int thickness);

- [DllImport("zan_gui")]static extern void zan_gui_fill_sector(int surfaceId, int cx, int cy, int rInner, int rOuter, int a0Deg, int a1Deg, int color);

- [DllImport("zan_gui")]static extern void zan_gui_draw_text(int surfaceId, int x, int y, string text, int color, int fontSize);

- [DllImport("zan_gui")]static extern void zan_gui_draw_text_bold(int surfaceId, int x, int y, string text, int color, int fontSize);

- [DllImport("zan_gui")]static extern void zan_gui_draw_text_rot(int surfaceId, int x, int y, string text, int color, int fontSize, int angle);

- [DllImport("zan_gui")]static extern int zan_gui_measure_text(string text, int fontSize);

- [DllImport("zan_gui")]static extern int zan_gui_font_height(int fontSize);

- [DllImport("zan_gui")]static extern int zan_gui_font_ascent(int fontSize);

- [DllImport("zan_gui")]static extern void zan_gui_text_stat_enable(int enabled);

- [DllImport("zan_gui")]static extern int zan_gui_text_stat_read(int idx);

- [DllImport("zan_gui")]static extern int zan_gui_get_dpi_scale();

- [DllImport("zan_gui")]static extern void zan_gui_blit_pixels(int surfaceId, nint bitmap, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh);

- [DllImport("zan_game")]static extern int zan_game_sprite_handle(string key);

- [DllImport("zan_game")]static extern int zan_game_bake_sprite(string key, int surfaceId, int x, int y, int w, int h);

- [DllImport("zan_game")]static extern void zan_game_sprite_batch(int surfaceId, int handle, nint quads, int count);

- [DllImport("zan_gui")]static extern int zan_gui_read_pixel(int surfaceId, int x, int y);

- [DllImport("zan_gui")]static extern nint zan_gui_get_pixels(int surfaceId);

- [DllImport("zan_image")]static extern int zan_image_register_argb(string key, nint pixels, int width, int height, int stride);

- [DllImport("zan_gui")]static extern void zan_gui_push_clip(int surfaceId, int x, int y, int w, int h);

- [DllImport("zan_gui")]static extern void zan_gui_pop_clip(int surfaceId);

- [DllImport("zan_gui")]static extern void zan_gui_reset_clip(int surfaceId);

- [DllImport("zan_gui")]static extern int zan_gui_set_render_backend(int mode);

- [DllImport("zan_gui")]static extern string zan_gui_render_backend();

- static int SetBackend(int mode)

- static string Backend()

- int surfaceId;

- int SurfaceId()

- Canvas(int width, int height)

- void Destroy()

- int Width()

- int Height()

- int WritePixels(string path, int x, int y, int w, int h)

- int DumpRaw(string path, int x, int y, int w, int h)

- void Clear(int color)

- void ClearRect(int x, int y, int w, int h, int color)

- static int RasterStat(int idx, int kind)

- static void TextStatEnable(bool enabled)

- static int TextStat(int idx)

- static int RasterTopBlend(int rank, int field)

- static int RasterTopFill(int rank, int field)

- [DllImport("zan_gui")]static extern string zan_gui_mem_report();

- static string MemReport()

- void FillRect(int x, int y, int w, int h, int color)

- void PushClip(int x, int y, int w, int h)

- void PopClip()

- void ResetClip()

- void DrawRect(int x, int y, int w, int h, int color, int thickness)

- void FillRoundRect(int x, int y, int w, int h, int radius, int color)

- void DrawRoundRect(int x, int y, int w, int h, int radius, int color, int thickness)

- void FillRoundRectIn(int x, int y, int w, int h, int radius, int corners, int color)

- void DrawRoundRectIn(int x, int y, int w, int h, int radius, int corners, int color, int thickness)

- void SurfaceRoundRect(int x, int y, int w, int h, int radius, int fill, int border, int borderThickness)

- void SurfaceRoundRectIn(int x, int y, int w, int h, int radius, int corners, int fill, int border, int borderThickness)

- void ShadowRoundRect(int x, int y, int w, int h, int radius, int blur, int color)

- void BlurRect(int x, int y, int w, int h, int radius)

- void BlurRectCached(int x, int y, int w, int h, int radius, int slot, bool dirty)

- void BlurRoundCached(int x, int y, int w, int h, int radius, int slot, bool dirty, int cornerRadius, int cornerMask)

- static int TakeBlurPartialMiss()

- void SnapshotRect(int x, int y, int w, int h, int slot)

- void SnapshotPatchRect(int x, int y, int w, int h, int slot)

- bool RestoreRect(int x, int y, int w, int h, int slot)

- bool RestoreSubRect(int x, int y, int w, int h, int slot)

- bool Dump(string path)

- void ReleaseSlot(int slot)

- void FillVGrad(int x, int y, int w, int h, int colorTop, int colorBottom)

- void FillVGradMask(int x, int y, int w, int h, int radius, int mask, int colorTop, int colorBottom)

- void FillGradMask(int x, int y, int w, int h, int radius, int mask, int dir, int colorFrom, int colorVia, int colorTo)

- void FillCircle(int cx, int cy, int radius, int color)

- void DrawCircle(int cx, int cy, int radius, int color, int thickness)

- void FillRadial(int cx, int cy, int radius, int color, int innerAlpha)

- void DrawLine(int x0, int y0, int x1, int y1, int color, int thickness)

- void DrawPolyline(List<int> xs, List<int> ys, int color, int thickness)

- void DrawPolylineFx(List<int> xs, List<int> ys, int color, int thickness)

- void DrawPolyBatch(List<int> xs, List<int> counts, int color, int thickness)

- void FillSector(int cx, int cy, int rInner, int rOuter, int a0Deg, int a1Deg, int color)

- void DrawArc(int cx, int cy, int rInner, int rOuter, int a0Deg, int a1Deg, int color, int thickness)

- void DrawText(int x, int y, string text, int color, int fontSize)

- void DrawTextBold(int x, int y, string text, int color, int fontSize)

- void DrawTextRot(int x, int y, string text, int color, int fontSize, int angleDeg)

- void DrawTextCentered(int rx, int ry, int rw, int rh, string text, int color, int fontSize)

- static int MeasureText(string text, int fontSize)

- static int FontHeight(int fontSize)

- static int FontAscent(int fontSize)

- static int FontPadTop(int fontSize)

- static int CenterTextY(int rectY, int rectH, int fontSize)

- static int FontLeadTop(int fontSize)

- static int CenterTextYAt(int centerY, int fontSize)

- void DrawIcon(int x, int y, int box, int color, int codepoint)

- void DrawStyledText(StyleBox s, int x, int y, string text, int fallbackColor, int fallbackFont)

- static string GlyphSvg(string name)

- void DrawGlyph(string name, int x, int y, int size, int color)

- void DrawGlyphIn(string name, int rx, int ry, int rw, int rh, int size, int color)

- static int ImageWidth(string path)

- static int ImageHeight(string path)

- static void EvictImage(string path)

- static int ImageLoadMem(string key, string data, int len)

- static int ImageLoadMem(string key, byte[]data, int len)

- static int ImageLoadSvg(string key, string svg, int rasterW, int rasterH)

- static int ImageLoadSvg(string key, byte[]svg, int len, int rasterW, int rasterH)

- int GetPixel(int x, int y)

- bool SnapshotWindowShape(string key, WindowShapeMask mask, List<WindowShapeRegion> regions, int dpiScale)

- bool SnapshotWindowShape(string key, WindowShapeMask mask, List<WindowShapeRegion> regions, int dpiScale, int opacityPercent)

- void BlitImage(string path, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh)

- public int SpriteHandle(string key)

- public int BakeSprite(string key, int x, int y, int w, int h)

- public int BakeSprite(string key)

- public void DrawSprites(int handle, nint quads, int count)

- void DrawImage(string path, int x, int y)

- void DrawDivider(int x, int y, int width, int color)

- void FillRectColor(int x, int y, int w, int h, Color col)

- [DllImport("zan_gui")]static extern void zan_gui_fill_rects(int surfaceId, nint data, int count);

- [DllImport("zan_gui")]static extern void zan_gui_fill_circles(int surfaceId, nint data, int count);

- [DllImport("zan_gui")]static extern void zan_gui_fill_radials(int surfaceId, nint data, int count);

- [DllImport("zan_gui")]static extern void zan_gui_blit_surface(int dstId, int srcId, int dstX, int dstY, int srcX, int srcY, int w, int h);

- [DllImport("zan_gui")]static extern void zan_gui_blit_surface_scaled(int dstId, int srcId, int dstX, int dstY, int dstW, int dstH, int srcX, int srcY, int srcW, int srcH);

- void PaintHashBatch(int op, nint data, int count, int stride)

- void FillRects(nint data, int count)

- void FillCircles(nint data, int count)

- void FillRadials(nint data, int count)

- void BlitSurface(Canvas src, int dstX, int dstY, int srcX, int srcY, int w, int h)

- void BlitSurfaceScaled(Canvas src, int dstX, int dstY, int dstW, int dstH, int srcX, int srcY, int srcW, int srcH)

- void DrawTextColor(int x, int y, string text, Color col, int fontSize)

- static int GetDpiScale()

- [DllImport("zan_game")]static extern int zan_game_mesh_create(int surfaceId, nint verts, int count, nint indices, int indexCount);

- [DllImport("zan_game")]static extern int zan_game_draw3d(int surfaceId, int mesh, nint mvp, int color, string texture);

- public int MeshUpload(Mesh3D mesh)

- public int DrawMesh3D(int mesh, float[]mvpColumnMajor, int color, string texture)


## ChildBoundSnapshot (class)

- public Control control;

- public string snapshot;

- ChildBoundSnapshot(Control control, string snapshot)


## ChildExpandedRow (class)

- public Control rowRoot;

- public Control template;

- ChildExpandedRow(Control rowRoot, Control template)


## ChildScopeBinding (class)

- public Control control;

- public JsonValue item;

- ChildScopeBinding(Control control, JsonValue item)


## ChildTemplateMeta (class)

- public Control template;

- public int lastCount;

- ChildTemplateMeta(Control template, int lastCount)


## ChildWindow (class)

- App host;

- Control root;

- HandlerRegistry handlers;

- JsonValue model;

- bool open;

- bool destroyed;

- int idBase;

- Action renderCb;

- List<ChildBoundSnapshot> boundSnapshots;

- List<ChildTemplateMeta> tmplMetas;

- List<ChildExpandedRow> expandedRows;

- List<ChildScopeBinding> scopeBindings;

- ChildWindow()

- virtual string Title()

- virtual int Width()

- virtual int Height()

- virtual bool ShowMinimize()

- virtual bool ShowMaximize()

- virtual int IdBase()

- App Host()

- void SetRender(Action a)

- void SetRoot(Control tree, JsonValue m)

- void Handle(string name, Action a)

- void HandleArg(string name, Action<string> a)

- void HandleSender(string name, ControlEvent a)

- void Wire()

- void WireNode(Control c)

- void SyncFromModel()

- void SyncFromNode(Control c)

- void SyncChangedNode(Control c)

- JsonValue ValueFor(Control c, string path)

- void RecordSnapshot(Control c, string v)

- string SnapshotOf(Control c)

- string BoundValue(Control c)

- void WriteBoundValue(Control c, string s)

- void RegisterTemplates(Control c)

- void EnsureExpanded()

- void RemoveRows(Control t)

- void DetachRow(Control row)

- void ScopeRow(Control c, JsonValue item)

- void ScopePut(Control c, JsonValue item)

- JsonValue ScopeOf(Control c)

- void ScopeDropRow(Control c)

- void ScopeDropOne(Control c)

- static bool Truthy(JsonValue v)

- virtual bool Pending()

- virtual void AfterFrame()

- virtual void OnLanguageChanged(string lang)

- void OpenHost(App parent)

- void OpenStandalone()

- virtual string StandaloneSkin()

- void PumpStandaloneUntil(ChildWindowStop stop)

- void PumpStandaloneUntilKeepOpen(ChildWindowStop stop)

- void PumpStandaloneUntilMode(ChildWindowStop stop, bool closeWhenDone)

- void Render()

- bool NeedsRedraw()

- bool IsOpen()

- bool OwnsWindow(nint hwnd)

- void ApplyEvent(nint hwnd)

- virtual void OnCloseRequested()

- void Close()

- void Teardown()

- void RequestRedraw()

- void ApplySkin(int i)

- void SetWindowOpacity(int percent)

- void SetUserWallpaper(string path, int opacity)

- nint WindowHandle()

- void FollowSkin(int i)

- void FollowCaptionTips(App src)

- void FollowOpacity(int percent)

- void FollowWallpaper(string path, int opacity)

- int AnimNextMs()

- int FxNextMs()


## ChildWindows (class)

- static List<ChildWindow> wins;

- static FilePicker picker;

- static long routedSeq;

- static bool routedHandled;

- static List<FilePicker> pickers;

- static List<ChildWindow> All()

- static void Register(ChildWindow w)

- static void ApplyLanguage(string lang)

- static void RegisterPicker(FilePicker p)

- static bool RegisterComponentPicker(FilePicker p)

- static void Prune()

- static bool AnyOpen()

- static bool Wants()

- static bool AnyPickerWants()

- static bool AnyPickerOpen()

- static int Deadline(int deadline)

- static bool PumpPickers(App main)

- static void PrunePickers()

- static bool TickDue(int now)

- static bool RouteEvent(nint hwnd)

- static bool PaintForResize(nint hwnd)

- static void PumpAll()

- static void FoldDeadlines(App main)

- static void FollowSkin(int i)

- static void FollowOpacity(int percent)

- static void FollowCaptionTips(App main)

- static void FollowWallpaper(string path, int opacity)

- static void RequestRedrawAll()


## Control (class)

- string name;

- int dock;

- int layout;

- bool grow;

- int prefW;

- int prefH;

- int mx;

- int my;

- int padL;

- int padT;

- int padR;

- int padB;

- bool padSet;

- bool prefSet;

- bool fillW;

- bool fillH;

- int logW;

- int logH;

- int logGap;

- int logPad;

- int logX;

- int logY;

- bool logPlaceSet;

- bool designFloat;

- int designAnchor;

- string designCss;

- int gap;

- int flexWrapMain;

- int flexMeasMain;

- int hintWrapW;

- List<FloatIntrusion> hostFloats;

- bool visible;

- int bx;

- int by;

- int bw;

- int bh;

- int scrollY;

- int scrollExtent;

- int scrollX;

- int scrollExtentX;

- ScrollState scroll;

- weak Control parent;

- List<Control> children;

- List<EventBinding> events;

- WidgetEvents On;

- int commonId;

- bool wiresOwnEvents;

- UiEvent Paint;

- UiEvent Resize;

- UiEvent Show;

- UiEvent Hide;

- int lastBw;

- int lastBh;

- bool lastVisible;

- bool everLaidOut;

- string bindPath;

- string bindProp;

- string bindIf;

- string handlerArg;

- string Class;

- Binding<bool> Disabled;

- StyleBox computedStyle;

- int computedStyleGen;

- int computedStyleScale;

- string computedStyleType;

- string computedStyleClass;

- string computedStyleId;

- int computedStyleState;

- string computedStyleCtx;

- string styleTypeLower;

- bool styleSurfaceOwned;

- int styleBg;

- int styleBgTo;

- int styleRadius;

- int styleCorners;

- int styleBorderColor;

- int styleBorderWidth;

- int styleShadow;

- int styleShadowDy;

- int styleColor;

- int styleFontPx;

- int styleTransitionMs;

- string inlineStyleCss;

- void InitControl(string n, int d)

- Control Bg(int c)

- Control Gradient(int top, int bottom)

- Control Radius(int r)

- Control Corners(int m)

- int Corners()

- Control Border(int color, int w)

- Control Shadow(int color, int dy)

- Control TextColor(int c)

- Control FontPx(int px)

- Control Transition(int ms)

- Control OnClick(Action a)

- Control OnDoubleClick(Action a)

- Control OnRightClick(Action a)

- Control OnChange(Action a)

- Control OnEnter(Action a)

- Control OnLeave(Action a)

- Control OnMouseDown(Action a)

- Control OnMouseUp(Action a)

- Control OnWheel(Action a)

- Control OnFocus(Action a)

- Control OnBlur(Action a)

- Control OnKeyDown(Action a)

- Control OnKeyUp(Action a)

- Control OnLongPress(Action a)

- Control OnSwipe(Action a)

- Control OnDrag(Action a)

- Control OnDrop(Action a)

- Control OnResize(Action a)

- Control OnShow(Action a)

- Control OnHide(Action a)

- Control OnClickS(ControlEvent h)

- Control OnDoubleClickS(ControlEvent h)

- Control OnRightClickS(ControlEvent h)

- Control OnChangeS(ControlEvent h)

- Control OnEnterS(ControlEvent h)

- Control OnLeaveS(ControlEvent h)

- Control OnMouseDownS(ControlEvent h)

- Control OnMouseUpS(ControlEvent h)

- Control OnWheelS(ControlEvent h)

- Control OnFocusS(ControlEvent h)

- Control OnBlurS(ControlEvent h)

- Control OnKeyDownS(ControlEvent h)

- Control OnKeyUpS(ControlEvent h)

- Control OnLongPressS(ControlEvent h)

- Control OnSwipeS(ControlEvent h)

- Control OnDragS(ControlEvent h)

- Control OnDropS(ControlEvent h)

- Control OnResizeS(ControlEvent h)

- Control OnShowS(ControlEvent h)

- Control OnHideS(ControlEvent h)

- bool IsDisabled()

- int CommonId()

- void FireCommon(App app)

- int StyleTextColor(int fallback)

- int StyleFontSize(int fallback)

- int TransitionMs(int fallback)

- virtual string StyleType()

- virtual void OnChildEvent(Control child, string evt)

- virtual string CssAttr(string key)

- virtual bool CssHasAttr(string key)

- virtual StyleBox ResolveStyle(App app, int state)

- StyleBox ResolveStyleAs(App app, string type, string cls, int state)

- StyleBox ResolveStyleCached(App app, string type, string cls, int state)

- StyleBox ResolveEasedStyleAs(App app, int id, string type, string cls, bool disabled, bool selected)

- StyleBox ResolveEasedStyleAsIn(App app, int id, string type, string cls, bool disabled, bool selected, int x, int y, int w, int h)

- StyleBox ResolveEasedLatchedStyleAs(App app, int id, string type, string cls, bool disabled, int latched, int progress)

- StyleBox ResolveEasedLatchedStyleAsIn(App app, int id, string type, string cls, bool disabled, int latched, int progress, int x, int y, int w, int h)

- StyleBox ResolvePart(App app, string part, string fallbackType, int state)

- StyleBox ResolvePartAs(App app, string type, string part, string fallbackType, string cls, int state)

- StyleBox ResolveEasedPartAs(App app, int id, string type, string part, string fallbackType, string cls, bool disabled, bool selected)

- StyleBox ResolveEasedPartAsIn(App app, int id, string type, string part, string fallbackType, string cls, bool disabled, bool selected, int x, int y, int w, int h)

- StyleBox ResolveEasedLatchedPartAs(App app, int id, string type, string part, string fallbackType, string cls, bool disabled, int latched, int progress)

- StyleBox ResolveEasedLatchedPartAsIn(App app, int id, string type, string part, string fallbackType, string cls, bool disabled, int latched, int progress, int x, int y, int w, int h)

- bool ComputedStyleCurrent(App app)

- int StyleWidth()

- int StyleHeight()

- int StylePadL()

- virtual int StylePadT()

- int StylePadR()

- int StylePadB()

- int StyleInsetL()

- int StyleInsetT()

- int StyleInsetR()

- int StyleInsetB()

- int StyleDisplay()

- int StyleOverflowX()

- int StyleOverflowY()

- bool IsScrollContainer()

- void SetScrollTop(int v)

- int ScrollTop()

- int ScrollExtent()

- void SetScrollLeft(int v)

- int ScrollLeft()

- int ScrollExtentX()

- int StyleWidthIn(int avail)

- int StyleHeightIn(int avail)

- bool StyleDeclaresWidth()

- bool StyleDeclaresHeight()

- bool StyleDeclaresWidthAbs()

- bool StyleDeclaresHeightAbs()

- int StyleGrow()

- int StyleWrap()

- int StyleAlignContent()

- int StyleAlignSelf()

- int StyleShrink()

- int StyleBasisIn(int avail)

- int StyleAspect()

- int StylePosition()

- int StyleZIndex()

- int StyleOrder()

- int StyleInlineLevel()

- int StyleFloat()

- int StyleClear()

- virtual List<FlowEntry> FlowEntries()

- int StyleMarL()

- int StyleMarT()

- int StyleMarR()

- int StyleMarB()

- int StyleGap()

- int StyleRowGap(int fb)

- int StyleColGap(int fb)

- int StyleColumns(int fb)

- int StyleLineHeight()

- bool StyleVisible()

- void PaintStyleBox(App app)

- void FireOn(App app, int id)

- Control Dock(int d)

- Control Prefer(int w, int h)

- Control Place(int x, int y)

- Control Pad(int p)

- Control Padding(int top, int right, int bottom, int left)

- Control Gap(int g)

- Control SetShown(bool v)

- Control HintWrapWidth(int px)

- Control Add(Control c)

- Control With(Control c)

- bool Adoptable(Control c)

- void Adopt(Control c)

- Form HostForm()

- Form FindForm()

- App FindApp()

- virtual void OnHostForm(Form f)

- void SpreadHostForm(Form f)

- int DockSize(int avail, bool horizontal)

- Control Grow()

- Control DockTop()

- Control DockBottom()

- Control DockLeft()

- Control DockRight()

- Control DockFill()

- Control DockManual()

- Control Float(int left, int top)

- Control AnchorTop()

- Control AnchorBottom()

- Control AnchorLeft()

- Control AnchorRight()

- Control Named(string n)

- int IndexOf(Control c)

- void Remove(Control c)

- void RemoveAll()

- void InsertAt(int idx, Control c)

- void MoveChild(Control c, int to)

- Control HitTest(int x, int y)

- Control Find(string n)

- int ChildCount()

- ControlChildren Children { get }

- string GetHandler(string evt)

- void SetHandler(string evt, string h)

- void ApplyDeclaredUnits(App app)

- void MeasureTree(App app)

- void PropagateWrapHint()

- static int CollapseMargins(int a, int b)

- bool FlowSepT()

- bool FlowSepB()

- int FlowMarT(Control c)

- int FlowMarB(Control c)

- bool IsEmptyFlowBlock(Control c)

- virtual string FlowText()

- virtual void InlineRunsBegin()

- virtual void InlineRunPlace(int x, int topY, int w, int h, int drawOff, int fs, string text)

- void MeasureFlow(App app)

- void ArrangeFlow(int cx, int cy, int cw, int ch, int gapPx)

- void SetHostFloats(Control c, List<FloatIntrusion> src, int dx, int dy)

- void PlaceFloatKid(List<FloatIntrusion> floats, Control c, int frameW, int yFlow, int cx, int cy, bool place)

- StyleBox InheritText(StyleBox parent, StyleBox own)

- FlowSegment FlowLayoutSegment(List<FlowEntry> seg, int wrapW, int wAvail, List<FloatIntrusion> floats, int segY)

- void FlowSegmentPlace(FlowSegment fs2, int cx, int y0, int availW, int align, List<FlowEntry> seg)

- List<Control> GridItems()

- int GridOuterW(Control c)

- int GridOuterH(Control c)

- void MeasureGridContent(App app)

- void ArrangeGrid(int cx, int cy, int cw, int ch)

- void MeasureDocked(App app)

- void MeasureFlexContent(App app)

- static int FitSize(int requested, int available)

- static int FitGap(int requested, int available)

- void ArrangeScrollTail(int clientH)

- void UpdateScroll(int clientH, int padB)

- void ShiftTree(int dx, int dy)

- virtual void Arrange(int px, int py, int pw, int ph)

- void ArrangePositioned(int cx, int cy, int cw, int ch)

- static bool overlapEnvRead;

- static bool overlapEnvOn;

- static bool DebugOverlap;

- static int overlapHits;

- static Dict <string, bool> overlapSeen;

- static bool OverlapActive()

- bool overlapExempt;

- Control NoOverlapCheck()

- static int OverlapHits()

- static void ResetOverlapHits()

- void CheckOverlapKids()

- void ReportOverlap(Control a, Control b, int ix, int iy)

- static bool lintEnvRead;

- static bool lintEnvOn;

- static bool DebugLayoutLint;

- static int lintHits;

- static Dict <string, bool> lintSeen;

- static bool LintActive()

- Control FreeLayout()

- virtual bool LintLeafSize()

- void CheckKidsLayout()

- static int LintHits()

- static void ResetLintHits()

- void CheckKidsSizes()

- void ReportLint(Control c, string rule, string detail)

- List<Control> FlexKids()

- void ArrangeFlex(int cx, int cy, int cw, int ch, int gapPx)

- void ArrangeFlexLine(List<Control> items, int cx, int cy, int cw, int ch, int gapPx, bool row, int crossOff, int crossBox)

- int StyleClampW(int avail, int v)

- int StyleClampH(int avail, int v)

- int MarMain(bool row)

- int MarCross(bool row)

- virtual void OnPaint(App app)

- virtual void OnPaintOverlay(App app)

- virtual void OnMeasure(App app)

- virtual string Kind()

- virtual List<PropSpec> Props()

- virtual bool Fired()

- Control FiredIn()

- static List<string> CommonEvents()

- virtual List<string> Events()

- virtual void BindEvent(string evt, Action a)

- virtual void BindEventS(string evt, ControlEvent h)

- PropSpec PropOf(string key)

- virtual string GetExtra(string key)

- virtual bool SetExtra(string key, string val)

- virtual Control SlotHost(int slot)

- virtual string GetProp(string key)

- virtual void SetProp(string key, string val)

- void SetDesignText(string txt)

- void AddClass(string cls)

- void SetClassIn(string group, string cls)

- static string SizeClass(string size)

- string SizeCls()

- static string StatusSuccess()

- static string StatusWarning()

- static string StatusError()

- static string StatusSanitize(string v)

- virtual Binding<string> SizeOf()

- virtual void SyncBinding()

- void RenderInside(App app, Rect area)

- void RenderAt(App app, int x, int y)

- void RenderAt(App app, int x, int y, int w)

- void RenderTree(App app)

- void RenderTreeInner(App app)


## ControlBootstrap (class)

- static bool installed;

- static List<string> Names()

- static void Install()

- static Control Make(string kind)


## ControlChildren (class)

- weak Control owner;

- ControlChildren(Control o)

- void Add(Control c)


## ControlFactory (class)

- static List<string> Kinds()

- static Control Create(string kind)


## Corner (class)

- static int TL()

- static int TR()

- static int BR()

- static int BL()

- static int All()

- static int Top()

- static int Right()

- static int Bottom()

- static int Left()


## Css (class)

- static StyleSheet Parse(string src)

- static StyleSheet ParseWith(string src, List<CssVarDecl> extraVars)

- static StyleSheet ParseWith(string src, List<string> extraNames, List<string> extraVals)

- static string ResolveImports(string path)

- static string ResolveImportsDepth(string path, List<string> seen, int depth)

- static int IndexOfImport(string s)

- static int MatchImportEnd(string s, int at)

- static string NormalizeKey(string path)

- static StyleSheet ParseWithSources(string src, List<CssVarDecl> extraVars, int physicalCount)

- static StyleSheet ParseWithSources(string src, List<string> extraNames, List<string> extraVals, int physicalCount)

- static void ParseRulesInto(StyleSheet sheet, string s, List<CssVarDecl> allVars, int externalCount)

- static void ParseRulesInto(StyleSheet sheet, string s, List<string> varNames, List<string> varVals, int externalCount)

- static void ParseRulesIntoM(StyleSheet sheet, string s, List<CssVarDecl> allVars, int externalCount, MediaCond outer)

- static void ParseRulesIntoM(StyleSheet sheet, string s, List<string> varNames, List<string> varVals, int externalCount, MediaCond outer)

- static string AtName(string prelude)

- static string AtPrelude(string prelude)

- static MediaCond ParseMedia(string prelude)

- static MediaAlt ParseMediaAlt(string alt)

- static bool ParseMediaFeatInto(string part, List<MediaFeat> acc)

- static string MinFeatName(string name)

- static string MaxFeatName(string name)

- static bool IsMediaName(string name)

- static List<string> SplitKeyword(string s, string kw)

- static bool SupportsCondition(string cond)

- static int FindTopKeyword(string s, string kw)

- static int TopToken(string s, string tok)

- static bool StartsWithStr(string s, string prefix)

- static int MatchBrace(string s, int open)

- static JsonValue ParseBlock(string body, List<CssVarDecl> vars)

- static JsonValue ParseBlock(string body, List<string> varNames, List<string> varVals)

- static JsonValue ParseBlockWithSources(string body, List<CssVarDecl> vars, int externalCount)

- static JsonValue ParseBlockWithSources(string body, List<string> varNames, List<string> varVals, int externalCount)

- static bool IsPhysicalThemeMetric(string name)

- static string PhysicalMetric(string val)

- static string ExpandVarsWithSources(string val, List<CssVarDecl> vars, int externalCount)

- static bool IsPrescaledValue(string val, List<CssVarDecl> vars, int externalCount)

- static bool IsPrescaledValue(string val, List<string> names, List<string> vals, int externalCount)

- static string StripImportant(string val)

- static bool HasImportant(string val)

- static string Lower(string s)

- static string ImportantOf(JsonValue block, string k)

- static void MergeInto(StyleSheet sheet, string sel, JsonValue block)

- static List<CssVarDecl> CollectVarDecls(string s)

- static void CollectVars(string s, List<string> names, List<string> vals)

- static string ExpandVars(string val, List<CssVarDecl> vars)

- static string ExpandVars(string val, List<string> names, List<string> vals)

- static int MatchParen(string s, int open)

- static int TopComma(string s)

- static string StripComments(string src)

- static List<string> SplitTrim(string s, string sep)

- static List<string> SplitTopTrim(string s, string sep)

- static List<string> SelectorTokens(string s)

- static int IndexFrom(string s, string needle, int start)

- static bool IsSpaceByte(int c)

- static bool Space(string ch)

- static string Trim(string s)

- static string PseudoContent(string raw, Control host)

- static int HexValByte(int b)

- static bool IsHexByte(int b)


## CssGrid (class)

- static GridLine ParseLine(string v)

- static List<GridTrack> ParseTracks(string v)

- static GridTrack ParseOne(string w)

- static List<string> SplitTop(string v)

- static bool StartsWith(string s, string pre)

- static bool EndsWith(string s, string suf)

- static GridPlace Place(List<Control> items, int expCols, int expRows)

- static bool Free(List<bool> occ, int nCols, int c, int r, int cs, int rs)

- static void Mark(List<bool> occ, int nCols, int c, int r, int cs, int rs)

- static int FindFreeInRow(List<bool> occ, int nCols, int r, int cs)

- static int FindFreeInCol(List<bool> occ, int nCols, int capRows, int c, int rs)

- static List<bool> Widen(List<bool> occ, int oldCols, int rows, int newCols)

- static List<int> SizeTracks(List<GridTrack> defs, List<GridTrack> auto, int n, List<int> span1At, List<int> span1Sz, int avail, int gap)

- static int SpanSum(List<int> size, int gap, int start, int span)

- static int TrackOffset(List<int> size, int gap, int start)


## CssVarDecl (class)

- public string name;

- public string val;

- public CssVarDecl(string name, string val)

- public static CssVarDecl Of(string name, string val)


## Cursor (class)

- static int Arrow()

- static int Hand()

- static int IBeam()

- static int ResizeH()

- static int ResizeV()


## DamageTracker (class)

- int x;

- int y;

- int w;

- int h;

- bool hasDamage;

- int debtCount;

- DamageTracker()

- int X()

- int Y()

- int Width()

- int Height()

- bool HasDamage()

- void Reset()

- void Set(int nx, int ny, int nw, int nh)

- void Union(int ox, int oy, int ow, int oh)

- void Pad(int padX, int padY)

- void ClampTo(int boundW, int boundH)

- int Area()

- bool IsFullWindow(int totalW, int totalH)

- void NoteDebt()

- bool HasDebt()

- void ClearDebt()


## DeviceProfile (class)

- string id;

- string en;

- string zh;

- int pw;

- int ph;

- int lw;

- int lh;

- bool locked;

- bool rotatable;

- bool round;

- DeviceProfile(string i, string e, string c, int pw0, int ph0, int lw0, int lh0, bool lk, bool rot, bool rnd)

- string Id()

- string Label(string lang)

- bool Locked()

- bool Rotatable()

- bool Round()

- int CanvasW(int orient)

- int CanvasH(int orient)

- static List<DeviceProfile> All()

- static int Count()

- static DeviceProfile At(int i)

- static DeviceProfile ById(string deviceId)

- static int IndexOfId(string deviceId)


## Dispatcher (class)

- [DllImport("crt", EntryPoint="zan_dispatch_init")]static extern void PlatInit();

- [DllImport("crt", EntryPoint="zan_dispatch_post")]static extern int PlatPost(Action handler);

- [DllImport("crt", EntryPoint="zan_dispatch_take")]static extern Action PlatTake();

- [DllImport("crt", EntryPoint="zan_dispatch_clear")]static extern void PlatClear();

- [DllImport("crt", EntryPoint="zan_ui_thread_set")]static extern void PlatSetUiThread();

- [DllImport("crt", EntryPoint="zan_ui_thread_check")]static extern int PlatCheckUiThread();

- [DllImport("crt", EntryPoint="zan_ui_thread_assert")]static extern void PlatAssertUiThread(string msg);

- static void Init()

- static void SetUiThread()

- static bool CheckUiThread()

- static void AssertUiThread(string msg)

- static bool Post(Action handler)

- static Action Take()

- static void Clear()


## Dock (class)

- static int Manual()

- static int Top()

- static int Bottom()

- static int Left()

- static int Right()

- static int Fill()


## Element (class)

- string elTag;

- string elText;

- List<ElementRun> elRuns;

- bool elRunsFresh;

- List<FlowEntry> elOrder;

- Dict <string, string> elAttrs;

- string elBefore;

- string elAfter;

- string elTitle;

- delegate void

- TipPaintFn(Control el, string title);

- static TipPaintFn tipPainter;

- static void SetTipPainter(TipPaintFn fn)

- void InitElement(string tag, string nodeName)

- Element SetAttr(string k, string v)

- override string CssAttr(string key)

- override bool CssHasAttr(string key)

- override string GetExtra(string key)

- override bool SetExtra(string key, string val)

- Element CopyAttrsFrom(Element src)

- override StyleBox ResolveStyle(App app, int state)

- override string Kind()

- Element SetText(string t)

- string Text()

- Element AddText(string t)

- Element AddKid(Control c)

- void DropKid(Control c)

- FlowEntry TextEntry(string t)

- override List<FlowEntry> FlowEntries()

- int ElFontSize()

- override string FlowText()

- override void InlineRunsBegin()

- override void InlineRunPlace(int x, int topY, int w, int h, int drawOff, int fs, string text)

- override void OnPaint(App app)


## ElementRun (class)

- string text;

- int x;

- int topY;

- int w;

- int h;

- int drawOff;

- int fs;


## EventBinding (class)

- string key;

- string val;

- EventBinding(string key, string val)


## EventHub (class)

- List<Subscription> subs;

- EventHub()

- void On(int key, Action handler)

- void Off(int key)

- int Count(int key)

- void Raise(int key)

- void RaiseIf(int key, bool fire)


## FlexNode (class)

- FlexStyle style;

- List<FlexNode> children;

- int resultX;

- int resultY;

- int resultW;

- int resultH;

- int contentW;

- int contentH;

- FlexNode(FlexStyle s)

- FlexNode AddChild(FlexNode child)

- void Compute(int containerW, int containerH)


## FlexStyle (class)

- int direction;

- int justify;

- int alignItems;

- int flexGrow;

- int flexShrink;

- int width;

- int height;

- int minWidth;

- int minHeight;

- int maxWidth;

- int maxHeight;

- int padTop;

- int padRight;

- int padBottom;

- int padLeft;

- int marginTop;

- int marginRight;

- int marginBottom;

- int marginLeft;

- int gap;

- int position;

- int left;

- int top;

- int right;

- int bottom;

- int anchor;

- int zIndex;

- static FlexStyle Default()

- static FlexStyle Row()

- static FlexStyle Column()

- FlexStyle SetPadding(int top, int right, int bottom, int left)

- FlexStyle SetPaddingAll(int p)

- FlexStyle SetGap(int g)

- FlexStyle SetSize(int w, int h)

- FlexStyle SetGrow(int g)

- FlexStyle SetJustify(int j)

- FlexStyle SetAlign(int a)

- FlexStyle SetPosition(int pos)

- FlexStyle SetOffsets(int l, int t, int r, int b)

- FlexStyle SetFloat(int l, int t)

- FlexStyle SetAnchor(int a)

- FlexStyle SetZIndex(int z)


## FloatIntrusion (class)

- int side;

- int x0;

- int x1;

- int yTop;

- int yBot;


## FlowEntry (class)

- string text;

- Control kid;


## FlowSegment (class)

- List<InlineLine> lines;

- int height;

- int width;


## FocusManager (class)

- int focusedId;

- int hoveredId;

- int pressedId;

- int hoverStartMs;

- int prevFocusedId;

- int prevHoveredId;

- int prevPressedId;

- int nextId;

- List<int> tabOrder;

- List<int> scopeReturn;

- List<int> textIds;

- int imeSessionId;

- List<int> idStack;

- int scopeAutoSeq;

- FocusManager()

- int CurrentScopeId()

- int GetId(string strKey)

- int GetId(int intKey)

- void PushId(string strKey)

- void PushId(int intKey)

- void PopId()

- int AllocId()

- void PushIds(int first)

- void PopIds()

- void ResetIds()

- void RegisterFocusable(int id)

- void RegisterTextEditable(int id)

- int IndexOf(int id)

- void FocusStep(int dir)

- bool IsFocused(int id)

- bool IsHovered(int id)

- bool IsPressed(int id)

- bool WasFocused(int id)

- bool WasHovered(int id)

- bool WasPressed(int id)

- void RollFrame()

- void UpdateImeSession()

- void SetFocused(int id)

- void SetHovered(int id)

- int HoveredMs()

- void SetPressed(int id)

- void ClearHover()

- void ClearPress()

- void ClearFocus()


## FontScale (class)

- const int Caption=10;

- const int Small=11;

- const int Body=12;

- const int Subhead=14;

- const int Title=16;

- const int Hero=20;

- const int Display=26;


## Form (class)

- App app;

- string title;

- List<FormTask> tasks;

- HandlerRegistry handlers;

- Form(string title, int width, int height)

- Form(string title, int width, int height, bool offscreen)

- void InitForm(string title, int width, int height, bool offscreen)

- static Form CreateOffscreen(string title, int width, int height)

- static Form Create(string title, int width, int height)

- static Form CreateDark(string title, int width, int height)

- App GetApp()

- void SetTitle(string text)

- string Title()

- override string StyleType()

- Action frameHook;

- void FrameHook(Action a)

- Action exitHook;

- void OnExit(Action a)

- FormTask Every(int ms, Action a)

- void PumpTasks()

- Control Ctl(string name)

- T Get<T>(string name)

- void On(string name, Action a)

- Action Handle(string name)

- void Call(string name)

- void RenderFrame(App a)

- void Run()

- Canvas CaptureSnapshot()


## FormTask (class)

- int periodMs;

- int dueMs;

- Action work;

- FormTask(int ms, Action a)

- int Period()

- int Due()

- void Schedule(int atMs)

- void Fire()


## Fx (class)

- static int Saw(App app, int periodMs)

- static int Pulse(App app, int periodMs)

- static int Isqrt(int n)

- static void PointGlow(Canvas c, int cx, int cy, int r, int color, int maxAlpha)

- static void BorderGlow(App app, int x, int y, int w, int h, int radius, int color, int periodMs, int intensity)

- static void Emboss(App app, int x, int y, int w, int h, int radius, bool pressed)

- static void DashedBorder(App app, int x, int y, int w, int h, int color)

- static void Specular(App app, int id, int x, int y, int w, int h, int radius, int color)

- static void SpecularRun(App app, int x, int y, int w, int h, int radius, int color, int periodMs)

- static void SpecularBorderHv(App app, int x, int y, int w, int h, int radius, int color, int hv)

- static void SpotlightHv(App app, int x, int y, int w, int h, int color, int reach, int maxAlpha, int hv)

- static void CardSheen(App app, int x, int y, int w, int h, int color, int periodMs)

- static void CardAurora(App app, int x, int y, int w, int h, int color)

- static void CardBreath(App app, int x, int y, int w, int h, int color, int periodMs)

- static void CardMotes(App app, int x, int y, int w, int h, int color)

- static void Apply(App app, int x, int y, int w, int h, int radius, FxOptions o)

- static void BgLightPillar(App app, int x, int y, int w, int h, int color)

- static void BgFloatingLines(App app, int x, int y, int w, int h, int color)


## FxOptions (class)

- bool ripple;

- bool specular;

- bool borderGlow;

- bool sheen;

- bool aurora;

- bool breath;

- bool motes;

- bool spotlight;

- int glowColor;

- int glowPeriodMs;

- FxOptions()


## GridItemSlot (class)

- int colStart;

- int colSpan;

- int rowStart;

- int rowSpan;

- GridItemSlot(int cs, int cp, int rs, int rp)


## GridLine (class)

- int start;

- int end;

- int span;


## GridPlace (class)

- List<Control> items;

- List<GridItemSlot> slots;

- int nCols;

- int nRows;


## GridTrack (class)

- int kind;

- int val;

- int minKind;

- int minVal;

- int maxKind;

- int maxVal;


## HandlerEntry (class)

- string name;

- Action action;

- Action<string> argAction;

- ControlEvent senderAction;

- HandlerEntry(string name, Action action)


## HandlerRegistry (class)

- List<HandlerEntry> entries;

- HandlerRegistry()

- void Set(string name, Action a)

- bool Has(string name)

- Action Get(string name)

- void SetArg(string name, Action<string> a)

- Action<string> GetArg(string name)

- void SetSender(string name, ControlEvent a)

- ControlEvent GetSender(string name)


## HasCond (class)

- Selector inner;

- int lead;


## HeavyControlAliasEntry (class)

- string from;

- string to;

- HeavyControlAliasEntry(string from, string to)


## HeavyControlFactoryEntry (class)

- string kind;

- ControlFactoryFn fn;

- HeavyControlFactoryEntry(string kind, ControlFactoryFn fn)


## HeavyControls (class)

- static List<HeavyControlFactoryEntry> entries;

- static List<HeavyControlAliasEntry> aliases;

- static void Register(string kind, ControlFactoryFn fn)

- static void RegisterAlias(string from, string to)

- static bool Has(string kind)

- static List<string> HeavyKinds()

- static Control Create(string kind)


## HitAnchor (class)

- int id;

- int x;

- int y;

- int width;

- int height;

- int widgetType;

- int isSet;

- HitAnchor()

- void Set(int id, int x, int y, int w, int h, int wtype)

- void Clear()


## HitRegion (class)

- int id;

- int x;

- int y;

- int width;

- int height;

- int widgetType;

- string label;

- HitRegion(int id, int x, int y, int w, int h, int wtype)

- bool Contains(int px, int py)

- void Set(int id, int x, int y, int w, int h, int wtype)


## HitSnapshotItem (class)

- int id;

- int x;

- int y;

- int width;

- int height;

- int widgetType;

- HitSnapshotItem(int id, int x, int y, int w, int h, int wtype)

- void Set(int id, int x, int y, int w, int h, int wtype)


## HitTester (class)

- List<HitRegion> regions;

- List<HitRegion> blockers;

- List<HitRegion> prevBlockers;

- List<HitRegion> poolA;

- List<HitRegion> poolB;

- bool useA;

- int used;

- List<HitAnchor> anchors;

- List<HitSnapshotItem> snapshots;

- int snapUsed;

- FocusManager focus;

- HitTester()

- void BindFocus(FocusManager f)

- void KeepFullSnapshot()

- void Clear()

- int AnchorAt(int k, int px, int py, bool below)

- void ClearAnchor(int k)

- int AnchorTypeOf(int k)

- int AnchorIdOf(int k)

- int RegionCount()

- HitRegion RegionAt(int i)

- void RegisterRect(int id, int x, int y, int w, int h, int wtype)

- void RegisterRectL(int id, int x, int y, int w, int h, int wtype, string label)

- void Register(HitRegion region)

- bool BlockerAt(int px, int py)

- bool PrevBlockerAt(int px, int py)

- int HitTest(int px, int py)

- int HitTestBelowBlockers(int px, int py)

- int HitTestFrom(int px, int py, int from)

- List<int> RectOf(int id)

- int GetWidgetType(int id)


## Html (class)

- static bool loaderInstalled;

- static void Install()

- static Control LoadForApp(App app, string html, HtmlHandlers handlers, string baseDir)

- static HtmlDoc Parse(string html, string baseDir, HtmlHandlers handlers)

- static Control Build(WDoc wd, WNode n, HtmlDoc doc, HtmlHandlers handlers)

- static void PaintTip(Control c, string tip)

- static void AddChild(Control parent, Control c)

- static Control Clone(Control c)

- static void Wire(HtmlHandlers handlers, Control c, string evt, string name)

- static void WireArg(HtmlHandlers handlers, Control c, string evt, string name, string arg)

- static void AutoLink(Control c, App app)

- static void AutoLinkTree(Control root, App app)

- static bool Navigable(string href)


## HtmlDoc (class)

- Control root;

- string css;

- int gen;


## HtmlHandlers (class)

- Dict <string, Action> map;

- Dict <string, Action<string>> argMap;

- void Add(string name, Action a)

- void AddArg(string name, Action<string> a)

- Action Find(string name)

- Action<string> FindArg(string name)


## Icon (class)

- static int Codepoint(string name)

- static string SvgName(string name)

- static List<string> Names()


## IconSvg (class)

- static string Alias(string name)

- static void Draw(Canvas c, string name, int x, int y, int box, int color)

- static void DrawIn(Canvas c, int rx, int ry, int rw, int rh, string name, int color)

- static int Count()

- static List<string> Names()

- static string HexColor(int color)

- static string AlphaAttrs(int color)


## IconSvgData (class)

- static List<IconSvgItem> items;

- static void Ensure()

- static void Merge(List<IconSvgItem> list, string json)

- static List<string> Packages()

- static void CollectDir(string dir, List<string> outp)

- static string Leaf(string path)

- static string ReadFile(string path)

- static void SortByNames(List<IconSvgItem> list)

- [DllImport("crt")]static extern string getenv(string name);

- static string Env(string name)

- static string ExeDir()

- [DllImport("crt")]static extern int zan_embed_has(string name);

- [DllImport("crt")]static extern string zan_embed_read(string name);

- [DllImport("crt")]static extern string zan_embed_list(string prefix);

- static int Count()

- static List<string> Names()

- static int IndexOf(string name)

- static string Body(int index)


## IconSvgItem (class)

- string name;

- string body;

- IconSvgItem(string name, string body)


## IconVector (class)

- static double Pi()

- static int Iabs(int v)

- static void Circle(Canvas s, int cx, int cy, int radius, int color, int thickness)

- static void Arrow(Canvas s, int x0, int y0, int x1, int y1, int color, int thickness)

- static List<int> StarPoints(int cx, int cy, int radius)

- static void Star(Canvas s, int cx, int cy, int radius, int color, int thickness)

- static bool PtInPoly(List<int> pts, int n, int x, int y)

- static void FillStar(Canvas s, int cx, int cy, int radius, int color)

- static void Draw(Canvas s, int x, int y, int box, int color, int codepoint)


## IdHash (class)

- static int HashString(string key, int seed)

- static int HashInt(int key, int seed)


## ImageHttp (class)

- static bool installed;

- static void Install()

- class Req

- class Slot

- static nint lockHandle;

- static List<Req> pending;

- static bool workerStarted;

- static Dict <string, Slot> urlSlots;

- static int slotSeq;

- static string EnsureUrl(App app, string url)

- static void Fetch(App app, Image img, string url, string key)

- static void WorkerLoop()

- static Req Take()

- static async int FetchOne(Req r)

- static void Apply(Req r, byte[]bytes, string body, int len, string err)


## InlineLine (class)

- List<InlinePiece> pieces;

- int width;

- int asc;

- int desc;

- int xOff;

- int availW;

- int yOff;


## InlinePiece (class)

- Control owner;

- bool isStrut;

- bool isBox;

- bool isSpace;

- bool hardBreak;

- string text;

- StyleBox st;

- int fs;

- int w;

- int lh;

- int asc;

- int desc;

- int ascOff;

- int boxH;

- int ml;

- int mr;

- int x;


## Insets (class)

- int top;

- int right;

- int bottom;

- int left;

- Insets(int top, int right, int bottom, int left)

- static Insets Uniform(int val)

- static Insets Symmetric(int vertical, int horizontal)

- int Horizontal()

- int Vertical()


## LineBox (class)

- static InlinePiece MakeText(Control owner, StyleBox st, string text, bool space)

- static InlinePiece MakeBox(Control kid, int w, int h, int ml, int mr, int mt, int mb, StyleBox st, int xhHalf)

- static InlinePiece MakeStrut(StyleBox st)

- static LineSpan LineAvail(List<FloatIntrusion> floats, int availW, int yTop, int yBot)

- static int ClearY(List<FloatIntrusion> floats, int side, int y)

- static int NextShelf(List<FloatIntrusion> floats, int y)

- static void PushText(List<InlinePiece> sink, Control owner, StyleBox st, string text)

- static void Layout(List<InlinePiece> items, InlinePiece strut, int availW, List<InlineLine> sink)

- static void LayoutF(List<InlinePiece> items, InlinePiece strut, int availW, List<InlineLine> sink, List<FloatIntrusion> floats, int yStart)

- static int Flush(InlineLine cur, int width, InlinePiece strut, List<InlineLine> sink, int yOff)


## LineSpan (class)

- int xOff;

- int availW;


## Mat4 (class)

- public float[]m;

- public Mat4()

- public static Mat4 Identity()

- public static Mat4 Translate(float x, float y, float z)

- public static Mat4 Scale(float x, float y, float z)

- public static Mat4 RotateAxis(float ax, float ay, float az, float rad)

- public static Mat4 Mul(Mat4 a, Mat4 b)

- public Mat4 Mul(Mat4 b)

- public void TransformPoints(float[]inXyz, int inOffset, float[]outXyz, int outOffset, int count)

- public void TransformPoints4(float[]inXyzw, int inOffset, float[]outXyzw, int outOffset, int count)

- public static Mat4 Perspective(float fovYRad, float aspect, float nearZ, float farZ)

- public static Mat4 Ortho(float left, float right, float bottom, float top, float nearZ, float farZ)

- public static Mat4 LookAt(float ex, float ey, float ez, float tx, float ty, float tz, float ux, float uy, float uz)

- public float[]ToColumnMajor()


## MathCursor (class)

- List<string> toks;

- int pos;


## MediaAlt (class)

- bool neg;

- List<MediaFeat> feats;

- bool AltTrue(StyleBox b)

- static MediaAlt And(MediaAlt x, MediaAlt y)


## MediaCond (class)

- List<MediaAlt> alts;

- bool True(StyleBox b)

- static MediaCond And(MediaCond x, MediaCond y)


## MediaFeat (class)

- string name;

- string val;

- bool True(StyleBox b)


## MediaOp (class)

- public int at;

- public string tx;

- public MediaOp(int at, string tx)


## MenuItem (class)

- int kind;

- string label;

- string icon;

- string shortcut;

- string key;

- int action;

- bool disabled;

- bool danger;

- int swatch;

- int swatch2;

- List<MenuItem> children;

- static MenuItem Item(string label, string icon, int action)

- static MenuItem SwatchItem(string label, string icon, int action, int c0, int c1)

- static MenuItem Shortcut(string label, string icon, string sc, int action)

- static MenuItem Disabled(string label, string icon, int action)

- static MenuItem Danger(string label, string icon, int action)

- static MenuItem DangerShortcut(string label, string icon, string sc, int action)

- static MenuItem Separator()

- static MenuItem Header(string label)

- static MenuItem Submenu(string label, string icon, List<MenuItem> children)


## Mesh3D (class)

- List<float> v;

- List<ushort> idx;

- public Mesh3D()

- public int Vertex(float px, float py, float pz, float nx, float ny, float nz, float u, float vv)

- public void Triangle(int a, int b, int c)

- public void Quad(int a, int b, int c, int d)

- public int VertexCount()

- public int IndexCount()

- public float[]VertexArray()

- public ushort[]IndexArray()

- public void AddCube()

- public void AddGroundDisc(int n, float radius)


## NativeLayer (class)

- static int MaxRects()

- static List<NativeLayerReq> tracked;

- static void Register(App app, int handle, int x, int y, int w, int h, NativeClipFn apply)

- static void Track(NativeLayerReq req)

- static void Forget(int handle)

- static bool Registered(App app, int handle)

- static void Occlude(App app, int x, int y, int w, int h)

- static void Flush(App app)

- static void HideUnregistered(App app)

- static List<int> Occluders(App app)

- static bool HitOnly(App app, int id)

- static List<int> Subtract(List<int> rects, int ox, int oy, int ow, int oh)

- static string Spec(List<int> rects)


## NativeLayerReq (class)

- App owner;

- int handle;

- int x;

- int y;

- int w;

- int h;

- NativeClipFn apply;

- NativeLayerReq(App owner, int handle, int x, int y, int w, int h, NativeClipFn apply)


## Nav (class)

- static List<NavRoute> routes;

- static List<NavWindow> openStack;

- static List<NavEmbed> embeds;

- static int nextIdBase;

- static NavRoute Define(string name, NavPageFactory make)

- static NavRoute DefineTitle(string name, string title, NavPageFactory make)

- static void OnEnter(string name, NavEnterHandler h)

- static void SetKeepAlive(string name, bool on)

- static void SetWindowSize(string name, int width, int height)

- static NavRoute Find(string name)

- static void Open(string name, App parent)

- static void OpenArgs(string name, App parent, JsonValue args)

- static NavWindow FindOpen(string name)

- static bool Back()

- static void CloseAll()

- static void Embed(Tabs tabs, string routeCsv)

- static void EmbedChanged()

- static void TryAddEmbedTab(NavEmbed e, string name)

- static void EnsureEmbedPage(NavEmbed e, int i)

- static void Prune()

- static void Activate(NavWindow w)

- static int NextIdBase()


## NavEmbed (class)

- Tabs tabs;

- List<string> names;


## NavRoute (class)

- string name;

- string title;

- NavPageFactory make;

- NavEnterHandler enter;

- bool keepAlive;

- int winW;

- int winH;


## NavWindow (class)

- NavRoute rt;

- int w;

- int h;

- int idBase;

- NavWindow()

- override string Title()

- override int Width()

- override int Height()

- override int IdBase()

- void OpenFor(NavRoute r, App parent, JsonValue args)

- void Replay(JsonValue args)


## OverlayPopup (class)

- static int lastRichHover;

- static int occlAx;

- static int occlAy;

- static int occlAw;

- static int occlAh;

- static int occlBx;

- static int occlBy;

- static int occlBw;

- static int occlBh;

- static int subGraceRow;

- static int subGraceMs;

- static int subGraceRow2;

- static int subGraceMs2;

- int x;

- int y;

- int w;

- List<string> options;

- int baseId;

- SignalInt model;

- int triggerId;

- SignalBool openFlag;

- SignalInt scrollModel;

- UiEvent onChange;

- bool showCheck;

- bool rich;

- bool themeDrawerMode;

- List<MenuItem> menuItems;

- SignalInt result;

- SignalInt subOpen;

- SignalInt subOpen2;

- bool richSub2;

- SignalInt richScroll;

- int richMaxRows;

- int richMinW;

- Control host;

- static OverlayPopup RichMenu(int x, int y, List<MenuItem> items, int baseId, SignalInt result, SignalBool openFlag, SignalInt subOpen)

- static OverlayPopup RichMenu(int x, int y, List<MenuItem> items, int baseId, SignalInt result, SignalBool openFlag, SignalInt subOpen, SignalInt scroll, int maxRows, int minW)

- static OverlayPopup RichMenu(int x, int y, List<MenuItem> items, int baseId, SignalInt result, SignalBool openFlag, SignalInt subOpen, SignalInt scroll, int maxRows, int minW, SignalInt subOpen2)

- static OverlayPopup ThemeDrawer(int baseId, int triggerId, SignalBool openFlag, SignalInt scrollModel)

- static OverlayPopup Host(Control h)

- static int FitX(App app, int x, int w)

- static int FitH(App app, int h)

- static int FitY(App app, int anchorY, int trigH, int h)

- static OverlayPopup OptionList(int x, int y, int w, List<string> options, int baseId, SignalInt model, int triggerId, SignalBool openFlag, SignalInt scrollModel, UiEvent onChange)

- static OverlayPopup Menu(int x, int y, int w, List<string> options, int baseId, SignalInt model, int triggerId, SignalBool openFlag, SignalInt scrollModel, UiEvent onChange)

- void Render(App app)

- void RenderThemeDrawer(App app)

- static List<int> DrawerAccents()

- static List<int> DrawerOpLevels()

- static List<string> DrawerKinds()

- int DrawerContentH(App app, int tab)

- int DrawDensityRow(App app, int yy)

- int DrawSkinGrid(App app, int yy)

- int DrawAccentRow(App app, int yy)

- int DrawOpacityChips(App app, int yy)

- int DrawWallpaperSection(App app, int yy)

- void DrawMotionTab(App app, int yy)

- void HandleDrawerInput(App app)

- int DrawerSection(App app, int dx, int yy, int dw, string label)

- static int RichWidth(App app, List<MenuItem> items)

- static int MenuRowH(App app)

- static int MenuPadV(App app)

- static int MenuPadH(App app)

- static int MenuSepH(App app)

- static int MenuHdrH(App app)

- static int RichViewH(App app, List<MenuItem> items, int maxRows)

- static int RichHeight(App app, List<MenuItem> items)

- static int RichRowTop(App app, List<MenuItem> items, int upto, int startY)

- static void SetOccluders(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)

- static bool PointOccluded(int mx, int my)

- static int PaintRichPanel(App app, List<MenuItem> items, int px, int py, int pw, int ph, int idBase, int scrollY, int barW)

- void RenderRich(App app)


## Palette (class)

- const int BgDark=unchecked((int)0xFF0F172A);

- const int SurfaceDark=unchecked((int)0xFF1E293B);

- const int SurfaceHover=unchecked((int)0xFF334155);

- const int BorderDark=unchecked((int)0xFF334155);

- const int TextHigh=unchecked((int)0xFFF8FAFC);

- const int TextMed=unchecked((int)0xFF94A3B8);

- const int TextLow=unchecked((int)0xFF64748B);

- const int PrimaryBrand=unchecked((int)0xFF3B82F6);

- const int PrimaryHover=unchecked((int)0xFF2563EB);

- const int Success=unchecked((int)0xFF10B981);

- const int Warning=unchecked((int)0xFFF59E0B);

- const int Danger=unchecked((int)0xFFEF4444);


## ParseCursor (class)

- int pos;

- ParseCursor()


## Point (class)

- public int x;

- public int y;

- public Point(int x, int y)


## PropSpec (class)

- string key;

- string alias;

- string label;

- int kind;

- List<string> options;

- Binding<string> str;

- Binding<int> num;

- Binding<bool> flag;

- SignalString sstr;

- SignalInt snum;

- SignalBool sflag;

- bool syncName;

- string tip;

- int step;

- int lo;

- int hi;

- bool hasRange;

- PropSpec(string k, string lbl, int kd)

- bool IsBound()

- bool Answers(string k)

- PropSpec AsName()

- PropSpec WithTip(string t)

- PropSpec Step(int s, int min, int max)

- PropSpec StepBy(int s)

- int Clamp(int v)

- PropSpec Also(string k)

- string Read()

- void Write(string val)

- static string Flag(bool on)

- static bool IsOn(string val)

- static PropSpec Text(string k, string lbl)

- static PropSpec Int(string k, string lbl)

- static PropSpec Bool(string k, string lbl)

- static PropSpec Color(string k, string lbl)

- static List<string> Sizes()

- static List<string> Sizes3()

- static PropSpec Section(string lbl)

- static PropSpec Enum(string k, string lbl, List<string> opts)

- PropSpec Option(string o)


## Radius (class)

- const int None=0;

- const int XS=2;

- const int S=4;

- const int M=8;

- const int L=12;

- const int XL=16;

- const int Full=9999;


## Rect (class)

- int x;

- int y;

- int width;

- int height;

- Rect(int x, int y, int w, int h)

- int Right()

- int Bottom()

- bool Contains(int px, int py)

- bool Intersects(int ox, int oy, int ow, int oh)

- static Rect Inflate(int rx, int ry, int rw, int rh, int amount)


## RenderAA (class)

- static int WithAlpha(int packed, int a)

- static void FillColumnAA(Canvas c, int px, int yyF, int baseY, int color, bool gradient, int topA, int botA, int solidA)

- static void FillBandColumnAA(Canvas c, int px, int topF, int botF, int color, bool gradient, int topA, int botA, int solidA)


## RenderBackend (class)

- static int Cpu()

- static int Gpu()

- static int Auto()


## RichTextDocument (class)

- string sourceText;

- List<RichTextRun> runs;

- RichTextDocument(string sourceText)

- string SourceText()

- int RunCount()

- RichTextRun RunAt(int index)

- void Add(RichTextRun run)


## RichTextLink (class)

- string raw;

- int style;

- int normalColor;

- int hoverColor;

- int pressedColor;

- RichTextLink(string marker)

- string Raw()

- int Count()

- string At(int index)

- int Style()

- int NormalColor()

- int HoverColor()

- int PressedColor()


## RichTextParser (class)

- string input;

- int position;

- RichTextDocument document;

- RichTextStyle style;

- RichTextLink link;

- RichTextParser(string input, RichTextStyle style, RichTextLink link)

- static RichTextDocument ParseText(string input)

- static RichTextDocument ParseText(string input, int defaultColor)

- static int FindIn(string text, string token, int start)

- static int ArgCount(string text)

- static string ArgAt(string text, int index)

- static bool IsSpace(string ch)

- static string TrimText(string s)

- static bool StartsAt(string text, int pos, string token)

- static int HexDigit(string digit)

- static int ParseInt(string text)

- static int ParseColor(string text)

- static int ParseColorArgs(string args)

- bool Starts(string token)

- string Parenthesized(int prefixLength)

- void AddText(string text)

- void AddSimple(int kind)

- void AddNested(string text, RichTextLink nestedLink)

- bool ParseColorShortcut()

- bool ParseTag()

- RichTextDocument Parse()


## RichTextRun (class)

- int kind;

- string text;

- string resource;

- string action;

- int quantity;

- int offsetX;

- int offsetY;

- int width;

- int height;

- double scale;

- RichTextStyle style;

- RichTextLink link;

- RichTextRun(int kind, RichTextStyle style, RichTextLink link)

- static RichTextRun CreateText(string text)

- int Kind()

- string Text()

- string Resource()

- string Action()

- int Quantity()

- int OffsetX()

- int OffsetY()

- int Width()

- int Height()

- double Scale()

- RichTextStyle Style()

- RichTextLink Link()

- bool IsLink()

- void SetText(string newValue)

- void SetResource(string newValue)

- void SetAction(string newValue)

- void SetQuantity(int newValue)

- void SetOffset(int x, int y)

- void SetSize(int width, int height)

- void SetScale(double newValue)


## RichTextRunKind (class)

- static int Text()

- static int Image()

- static int Animation()

- static int Spacer()

- static int Item()

- static int LineBreak()

- static int WrapWidth()


## RichTextStyle (class)

- int color;

- int background;

- string font;

- int alignment;

- RichTextStyle(int color, int background, string font, int alignment)

- static RichTextStyle Default()

- RichTextStyle Clone()

- int Color()

- int Background()

- string Font()

- int Alignment()

- void SetColor(int newValue)

- void SetBackground(int newValue)

- void SetFont(string newValue)

- void SetAlignment(int newValue)


## ScrollState (class)

- bool dragging;

- int dragMouseY0;

- int dragOffset0;

- int dragMo0;

- bool draggingX;

- int dragMouseX0;

- int dragOffsetX0;

- int dragMoX0;

- int barW;

- ScrollState()

- bool WheelXY(App app, int x, int y, int w, int h, Control host, int oxMode, int oyMode)

- bool WantsBar(int extent, int client, int mode)

- int Clamp(int offset, int extent, int client)

- int Bar(App app, int x, int y, int w, int h, int offset, int extent, int mode)

- int BarX(App app, int x, int y, int w, int h, int offset, int extent, int mode)


## Selector (class)

- string type;

- bool universal;

- string classes;

- List<string> classList;

- string id;

- string part;

- string state;

- string clsContains;

- List<AttrCond> attrs;

- int reqMask;

- int forbidMask;

- List<Selector> nots;

- List<Selector> ises;

- List<Selector> wheres;

- List<HasCond> hasList;

- Selector up;

- int combo;

- int structCode;

- int anbA;

- int anbB;

- bool never;

- string why;

- static Selector Parse(string sel)

- static Selector ParseCompound(string sel)

- int Pseudo(string s, int at)

- static int StructCode(string name)

- bool SetAnB(string arg)

- static bool SignedInt(string s, out int v)

- bool MatchAnB(int idx)

- static List<Selector> ParseList(string text)

- bool AddAttr(string inner)

- static int AttrState(string name)

- static bool IsPseudoElement(string name)

- static bool IsGenerated(string name)

- bool MatchNode(string nodeType, List<string> classes, string cls, string nodeId, string nodePart, int stateBits)

- bool MatchNodeCtx(string nodeType, List<string> classes, string cls, string nodeId, string nodePart, int stateBits, Control node)

- bool MatchChain(Selector up, Control node)

- static Control SiblingAt(Control par, Control node, int back)

- static bool HasMatch(Control host, HasCond hc)

- static bool ScanSubtree(Selector inner, Control host, int depth)

- static bool SelHit(Selector s, Control c)

- bool MatchStruct(Control node)

- bool Relational()

- static bool MatchAttr(AttrCond a, List<string> classes, string cls, Control node)

- static bool HayOp(string op, string hay, string val)

- static bool HoldsCI(List<string> classes, string name)

- int Specificity()

- static bool NameChar(string ch)


## SelectorTokenChunk (class)

- public string chunk;

- public int combinator;

- public SelectorTokenChunk(string chunk, int combinator)


## Serialize (class)

- static string Esc(string s)

- static string Unesc(string s)

- static void WriteNode(Control c, List<string> lines)

- static string Save(Control root)

- static List<string> SplitLines(string s)

- static List<string> SplitFields(string line)

- static Control ReadNode(List<string> lines, ParseCursor cur)

- static Control Load(string text)


## SignalBool (class)

- bool val;

- int version;

- SignalBool(bool initial)

- bool Get()

- void Set(bool v)

- void Toggle()

- int Version()


## SignalInt (class)

- int val;

- int version;

- SignalInt(int initial)

- int Get()

- void Set(int v)

- int Version()


## SignalString (class)

- string val;

- int version;

- SignalString(string initial)

- string Get()

- void Set(string v)

- int Version()


## Size (class)

- public int width;

- public int height;

- public Size(int w, int h)


## Skin (class)

- string name;

- string dir;

- StyleSheet sheet;

- Theme theme;

- bool dark;

- string art;

- int artOpacity;

- bool loaded;

- static Dict <string, Skin> cache;

- Skin(string skinName)

- static Skin Load(string name)

- static Skin Reload(string name)

- bool IsLoaded()

- string ArtPath()

- Theme NewTheme()

- void Apply(App app)

- static List<string> Roots()

- static string ExeDir()

- static string FindDir(string name)

- static List<string> Available()

- static string EmbedSkinName(string line)

- static string KindOf(string name)

- static List<string> Names()

- static int OrderOf(string name)

- static string LabelOf(string name)

- static List<string> Labels(List<string> names)

- static string FindArt(string dir)

- static Theme ThemeOf(StyleSheet sheet)

- static Theme BaseOf(StyleSheet sheet)

- static int DensityOf(StyleSheet sheet)

- static bool DarkOf(StyleSheet sheet)

- static string TokenName(string key)

- static string Unquote(string v)

- static string ReadIfExists(string path)

- static string BaseCss()

- static string Leaf(string path)

- static bool Contains(List<string> list, string v)

- static extern string getenv(string name);

- static string Env(string name)

- static extern string zan_embed_read(string name);

- static extern int zan_embed_has(string name);

- static extern string zan_embed_list(string prefix);

- static string EmbedRead(string name)

- static bool EmbedHas(string name)

- static string EmbedList(string prefix)


## Space (class)

- const int None=0;

- const int XXS=2;

- const int XS=4;

- const int S=8;

- const int SM=12;

- const int M=16;

- const int L=24;

- const int XL=32;

- const int XXL=48;


## SpriteBatch (class)

- nint buf;

- int capSprites;

- int count;

- SpriteBatch()

- public static SpriteBatch Create(int capSprites)

- public void Begin()

- public int Count()

- public void Add(float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh, int tint)

- public void Draw(Canvas c, int handle)

- public void Dispose()


## Stack (class)

- int ox;

- int oy;

- int ow;

- int oh;

- int dir;

- int gap;

- int cursor;

- int placed;

- static Stack Of(int x, int y, int w, int h, int direction, int gap)

- static Stack Column(int x, int y, int w, int h)

- static Stack Row(int x, int y, int w, int h)

- static Stack ColumnIn(Rect r)

- static Stack RowIn(Rect r)

- Stack SetGap(int g)

- Stack SetPad(int p)

- Stack SetPadding(int top, int right, int bottom, int left)

- int MainSize()

- int CrossSize()

- int Remaining()

- bool HasRoom(int mainSize)

- void Space(int mainSize)

- Rect Slot(int mainSize)

- Rect Fill()

- Rect SlotFraction(int permille)

- Rect SlotFractionClamped(int permille, int minSize, int maxSize)

- List<Rect> SlotsGrown(List<int> naturalSizes)


## State (class)

- T val;

- int version;

- App hostApp;

- State(T initial)

- void BindApp(App app)

- T Get()

- void Set(T v)

- int Version()


## Style (class)

- static int SNormal()

- static int SHover()

- static int SActive()

- static int SFocus()

- static int SDisabled()

- static int SSelected()

- static int SChecked()

- static int AnimNone()

- static int AnimSpin()

- static int AnimPulse()

- static int AnimBreath()

- static int AnimShimmer()

- static int AnimFloat()

- static int AnimGlow()

- static int AnimAurora()

- static int AnimMotes()

- static int AnimKind(string name)

- static string StateName(int bit)

- static int StateBit(string name)

- static int StateOf(App app, int id, bool disabled)

- static int statResolve;

- static int statHit;

- static int StatPeek(int idx)

- static int StatRead(int idx)

- static StyleBox CacheGet(App app, string key)

- static void CachePut(App app, string key, StyleBox box)

- static StyleBox Of(App app, string type, string cls, int state)

- static StyleBox Full(App app, string type, string cls, string id, int state)

- static StyleBox Part(App app, string type, string part, string fallbackType, string cls, string id, int state)

- static void ScaleLayout(App app, StyleBox b)

- static void ScaleLayout(App app, StyleBox b, StyleBox themed)

- static void ScaleBox(StyleBox b)

- static void ApplySheet(StyleSheet sheet, StyleBox b, string type, string cls, string id, int state)

- static void ApplySheetCtx(StyleSheet sheet, StyleBox b, string type, string cls, string id, int state, Control node)

- static string SheetAlias(string type)

- static void ApplyPartSheet(StyleSheet sheet, StyleBox b, string type, string part, string cls, string id, int state)

- static void ApplyPartSheetCtx(StyleSheet sheet, StyleBox b, string type, string part, string cls, string id, int state, Control node)

- static int NodeState(Control c)

- static string MediaSig(App app)

- static bool AnyMedia(StyleSheet basef, StyleSheet chartf, StyleSheet sheet)

- static string CtxSig(Control node)

- static List<string> Classes(string cls)

- static bool Has(string cls, string name)

- static StyleBox Eased(App app, int id, string type, string cls, bool disabled)

- static StyleBox EasedId(App app, int id, string type, string cls, string name, bool disabled, bool selected)

- static StyleBox EasedIdIn(App app, int id, string type, string cls, string name, bool disabled, bool selected, int x, int y, int w, int h)

- static StyleBox EasedStateId(App app, int id, string type, string cls, string name, bool disabled, int latched)

- static StyleBox EasedStateIdIn(App app, int id, string type, string cls, string name, bool disabled, int latched, int x, int y, int w, int h)

- static StyleBox EasedPartId(App app, int id, string type, string part, string fallbackType, string cls, string name, bool disabled, bool selected)

- static StyleBox EasedPartIdIn(App app, int id, string type, string part, string fallbackType, string cls, string name, bool disabled, bool selected, int x, int y, int w, int h)

- static StyleBox EasedPartStateId(App app, int id, string type, string part, string fallbackType, string cls, string name, bool disabled, int latched)

- static StyleBox EasedPartStateIdIn(App app, int id, string type, string part, string fallbackType, string cls, string name, bool disabled, int latched, int x, int y, int w, int h)

- static int Curve(int easing, int p)

- static int RoleColor(Theme t, string cls, int state)

- static int RoleBase(Theme t, string cls)

- static void Defaults(App app, StyleBox b, string type, string cls, int state)

- static void UnitEnvironment(App app, StyleBox b)

- static void Shell(App app, StyleBox b, string type, string cls, int state)

- static StyleSheet BaseSheet(App app)

- static string RootFromTheme(Theme t)

- static string VariantTokens(string name, int tint, int surface)

- static string Tok(string name, int v)

- static string TokN(string name, int v)

- static string UaCss()

- static int FontFallback(App app, string size)

- static string Hex8(int v)

- static string HexDigit(int n)

- static bool FlatType(string type)

- static void SizeDefaults(App app, StyleBox b, string cls)

- static string TypeClass(int type)

- static string RoleClass(string cls)

- static string FillClass(int fill)

- static string SizeClass(int size)

- static string StateClass(int index, int current, int errorIndex)

- static int Fade(int color, int a)

- static int TypeColor(Theme t, int type)

- static int TypeFill(Theme t, int type, int style, bool hovered, bool pressed)

- static int TypeFg(Theme t, int type, int style)

- static void ButtonDefaults(App app, StyleBox b, string cls, bool active, bool disabled)

- static void SurfaceDefaults(App app, StyleBox b, string cls)

- static void TabDefaults(App app, StyleBox b, string cls)

- static void Inline(StyleBox b, Control c)


## StyleBox (class)

- long prescaled;

- int envDpiScale;

- bool envRemPhysical;

- Theme envTheme;

- StyleSheet envSheet;

- bool lengthDevice;

- bool lengthMath;

- bool lengthPhysical;

- bool declPrescaled;

- int bg;

- int bgTo;

- int bgVia;

- int bgDir;

- int opacity;

- int blur;

- int fg;

- int accent;

- int fontPx;

- int fontWeight;

- int lineHeight;

- int letterSpacing;

- int align;

- int valign;

- int textCase;

- int ellipsis;

- int nowrap;

- int tsColor;

- int tsDx;

- int tsDy;

- string contentRaw;

- int tsOutline;

- int borderColor;

- int borderW;

- int borderTopW;

- int borderTopColor;

- int borderRightW;

- int borderRightColor;

- int borderBottomW;

- int borderBottomColor;

- int borderLeftW;

- int borderLeftColor;

- int radius;

- int radiusTL;

- int radiusTR;

- int radiusBR;

- int radiusBL;

- int shadow;

- int shadowDx;

- int shadowDy;

- int shadowBlur;

- int shadowInset;

- int borderStyle;

- int emboss;

- int specular;

- int fxId;

- int sheen;

- int sheenColor;

- int padL;

- int padT;

- int padR;

- int padB;

- int marL;

- int marT;

- int marR;

- int marB;

- int width;

- int height;

- int minW;

- int minH;

- int maxW;

- int maxH;

- int widthPm;

- int heightPm;

- int minWPm;

- int minHPm;

- int maxWPm;

- int maxHPm;

- int gap;

- int rowGap;

- int colGap;

- int columns;

- string gridCols;

- string gridRows;

- string gridAutoCols;

- string gridAutoRows;

- int gColStart;

- int gColEnd;

- int gColSpan;

- int gRowStart;

- int gRowEnd;

- int gRowSpan;

- int justifyItems;

- int display;

- int flexDir;

- int justify;

- int alignItems;

- int wrap;

- int alignContent;

- int alignSelf;

- int grow;

- int shrink;

- int basis;

- int basisPm;

- int aspect;

- int order;

- int position;

- int posT;

- int posR;

- int posB;

- int posL;

- int overflow;

- int overflowCss;

- int overflowX;

- int overflowY;

- int zIndex;

- int cursor;

- int ccMask;

- int visible;

- int inlineLevel;

- int floatSide;

- int clearSide;

- int boxSizing;

- int whiteSpace;

- int lineHeightKind;

- int bfc;

- int vaInline;

- int transitionMs;

- int easing;

- int anim;

- int animMs;

- int tx;

- int ty;

- int scale;

- int rotate;

- int envRemPx;

- int envRootFontPx;

- int envVw;

- int envVh;

- bool mediaDark;

- bool mediaReduced;

- static int Unset()

- static long SourceFont()

- static long SourceWidth()

- static long SourceHeight()

- static long SourceGap()

- static long SourceRadius()

- static long SourcePadL()

- static long SourceIcon()

- static long SourceMinW()

- static long SourceMinH()

- static long SourceMaxW()

- static long SourceMaxH()

- static long SourceBasis()

- static long SourceRowGap()

- static long SourceColGap()

- static long SourceRadiusTL()

- static long SourceRadiusTR()

- static long SourceRadiusBR()

- static long SourceRadiusBL()

- static long SourcePadT()

- static long SourcePadR()

- static long SourcePadB()

- static long SourceMarL()

- static long SourceMarT()

- static long SourceMarR()

- static long SourceMarB()

- static long SourceLineHeight()

- static long SourceLetterSpacing()

- static long SourceBorder()

- static long SourceBorderTop()

- static long SourceBorderRight()

- static long SourceBorderBottom()

- static long SourceBorderLeft()

- static long SourcePosT()

- static long SourcePosR()

- static long SourcePosB()

- static long SourcePosL()

- static long SourceTx()

- static long SourceTy()

- static long SourceShadowDx()

- static long SourceShadowDy()

- static long SourceShadowBlur()

- static long SourceBlur()

- static long SourcePadding()

- bool IsPrescaled(long source)

- void SetPrescaled(long source, bool value)

- void MarkThemeLengths()

- int ScaleLogical(int value)

- int ResolveLength(int value, long source)

- int PaintLength(App app, int value, long source)

- StyleBox()

- StyleBox Clone()

- int BorderTopPx()

- int BorderRightPx()

- int BorderBottomPx()

- int BorderLeftPx()

- int BgOr(int fb)

- int FgOr(int fb)

- int AccentOr(int fb)

- int FontOr(int fb)

- int RadiusOr(int fb)

- int BorderWOr(int fb)

- int BorderOr(int fb)

- int GapOr(int fb)

- int RowGapOr(int fb)

- int ColGapOr(int fb)

- int ColumnsOr(int fb)

- int HeightOr(int fb)

- int WidthOr(int fb)

- int Corners()

- void SetCorners(int m)

- static int MetricIn(int pm, int abs, int avail, int fb)

- int WidthIn(int avail, int fb)

- int HeightIn(int avail, int fb)

- int BasisIn(int avail)

- int ClampWIn(int avail, int v)

- int ClampHIn(int avail, int v)

- int TransitionOr(int fb)

- int PadLOr(int fb)

- int PadTOr(int fb)

- int PadROr(int fb)

- int PadBOr(int fb)

- int ClampW(int w)

- int ClampH(int h)

- void SetPad(int v)

- void SetMargin(int v)

- void SetRadius(int v)

- void SetBorder(int w, int color)

- static StyleBox Blend(StyleBox a, StyleBox b, int p)

- static int MixColor(int c0, int c1, int p)

- static int MixMetric(int v0, int v1, int p)

- static int AlphaOf(int color)

- int Fade(int color)

- void Paint(App app, int x, int y, int w, int h)

- void PaintShadow(App app, int x, int y, int w, int h, int r)

- void PaintSheen(App app, int x, int y, int w, int h, int r)

- void PaintGradient(App app, int x, int y, int w, int h, int r)

- void PaintBorders(App app, int x, int y, int w, int h, int r)

- void PaintSideArc(App app, int x, int y, int w, int h, int r, int on, int cx, int cy, int hw, int vw, int hcol, int vcol)

- int SideColor(int col)

- void PaintAnim(App app, int x, int y, int w, int h, int r)

- int FloatOffset(App app, int x, int y, int w, int h)

- int SpinDeg(App app, int x, int y, int w, int h)

- int LineHeightPx(int fs)

- int DrawLabel(App app, int x, int y, int w, int h, string label)

- void DrawRun(Canvas c, int x, int y, string s, int color, int fs)

- void DrawTextShadow(Canvas c, int x, int y, string s, int fs)

- static int MeasureSpaced(string s, int fs, int spacing)

- static string Truncate(string s, int fs, int spacing, int avail)


## StyleMediaEntry (class)

- public Selector selector;

- public JsonValue block;

- public JsonValue important;

- public MediaCond condition;

- public StyleMediaEntry(Selector selector, JsonValue block, JsonValue important, MediaCond condition)


## StyleRuleEntry (class)

- public string text;

- public Selector selector;

- public JsonValue block;

- public JsonValue important;

- public StyleRuleEntry(string text, Selector selector, JsonValue block, JsonValue important)


## StyleSheet (class)

- JsonValue rules;

- JsonValue vars;

- string state;

- List<StyleRuleEntry> ruleEntries;

- int indexedCount;

- Dict <string, List<int>> byType;

- List<int> anyType;

- int atRulesDropped;

- Dict <string, int> atRuleKinds;

- int atGuardsSkipped;

- Dict <string, int> atRuleGuards;

- List<string> lintDropped;

- List<string> lintNever;

- List<string> lintUnused;

- List<string> lintValue;

- List<string> lintInert;

- int lintGen;

- bool relational;

- List<StyleMediaEntry> mediaEntries;

- int mediaCount;

- StyleSheet()

- void NoteAtRule(string name)

- void NoteGuard(string cond)

- static StyleSheet FromCss(string src)

- void AddMediaRule(string sel, JsonValue block, MediaCond cond)

- bool HasMedia()

- void ApplyMedia(StyleBox b, string type, List<string> want, string cls, string id, string part, int stateBits, Control node, bool importantPass)

- void MergeSheet(StyleSheet other)

- static StyleSheet FromJson(string src)

- bool IsEmpty()

- List<string> Audit()

- string Var(string name)

- JsonValue Block(string selector)

- void ApplyBox(StyleBox b, string selector)

- void ApplyMatch(StyleBox b, string type, string cls, string id, string part, int stateBits)

- void ApplyMatchCtx(StyleBox b, string type, string cls, string id, string part, int stateBits, Control node)

- void ApplyCascade(StyleBox b, string type, string cls, string id, string part, int stateBits)

- void ApplyCascadeCtx(StyleBox b, string type, string cls, string id, string part, int stateBits, Control node)

- void ApplyImportant(StyleBox b, string type, string cls, string id, string part, int stateBits)

- void ApplyImportantCtx(StyleBox b, string type, string cls, string id, string part, int stateBits, Control node)

- void ApplyDecls(StyleBox b, JsonValue block)

- bool Matches(int i, string type, List<string> classes, string id, string part, int stateBits, string cls)

- bool MatchesCtx(int i, string type, List<string> classes, string id, string part, int stateBits, string cls, Control node)

- bool HasRelational()

- static bool Holds(List<string> list, string name)

- void Index()

- List<string> Lint()

- void CollectLint()

- static bool Known(string key)

- static bool SupportsDecl(string prop, string val)

- static bool GuardValueKnown(string prop, string val)

- static string AtRuleSummary(Dict <string, int> kinds)

- static string GuardSummary(Dict <string, int> conds)

- static bool ValueUnresolved(string key, string val)

- static bool PctCoerced(string k)

- static bool HasUnresolvedUnit(string val)

- static bool IsAlpha(string ch)

- static string ValStr(JsonValue v)

- static bool InternalKey(string key)

- static bool IsPrescaled(JsonValue block, string key)

- static bool IsPrescaled(JsonValue block, string key, bool important)

- static bool DeclBlock(StyleBox b, JsonValue block, string key, bool important)

- static bool Decl(StyleBox b, string key, string val)

- static int CcBits(string key)

- static void ResolveCurrentColor(StyleBox b)

- static string StripVendor(string k)

- static bool DeclSource(StyleBox b, string key, string val, bool prescaled)

- static long SourceFor(string key)

- static bool DeclFill(StyleBox b, string k, string v)

- static void LineHeightOf(StyleBox b, string v)

- static bool DeclText(StyleBox b, string k, string v)

- static bool DeclBorderBox(StyleBox b, string k, string v)

- static bool DeclBoxMetrics(StyleBox b, string k, string v)

- static void DeclMetric(StyleBox b, int which, string v)

- static bool DeclLayout(StyleBox b, string k, string v)

- static bool Inert(string key)

- static int AlignItemCode(string v)

- static int JustifyCode(string v)

- static int AlignContentCode(string v)

- static void DeclFlex(StyleBox b, string val)

- static void DeclBasis(StyleBox b, string val)

- static void DeclAspect(StyleBox b, string val)

- static bool DeclMotion(StyleBox b, string k, string v)

- static void DeclBackground(StyleBox b, string val)

- static int GradientDir(string tok)

- static bool IsLeftward(string tok)

- static void DeclTextShadow(StyleBox b, string val)

- static string StopColor(string stop)

- static void DeclBorder(StyleBox b, string val, int side)

- static void DeclRadius(StyleBox b, string val)

- static void CornerRadius(StyleBox b, int which, int v)

- static void DeclShadow(StyleBox b, string val)

- static void DeclSheen(StyleBox b, string val)

- static int SideVal(StyleBox b, string tok, bool margin)

- static string SideToken(List<string> toks, int side)

- static int SideLength(StyleBox b, string tok, bool margin, long source)

- static void DeclSides(StyleBox b, string val, int which)

- static void DeclTransition(StyleBox b, string val)

- static void DeclTransform(StyleBox b, string val)

- static void DeclAnimation(StyleBox b, string val)

- static int BlurArg(StyleBox b, string val)

- static int Easing(string val)

- static int Weight(string val)

- static int CursorCode(string val)

- void Apply(Control c, string type, string cls, string id)

- void ApplyImportantSelector(Control c, string selector)

- void ApplySelector(Control c, string selector)

- static void ApplyBlock(Control c, JsonValue block)

- static void ApplyInline(Control c, string decls)

- static void ReplayInline(StyleBox b, Control c, int resolvedFont, bool fontPhysical)

- static void CopyToControl(StyleBox b, Control c)

- static void ApplyBackground(Control c, string val)

- static void ApplyBorder(Control c, string val)

- static int ParseColor(string val)

- static int ParseRgb(string val)

- static List<string> FuncArgs(string val)

- static int Alpha255(string tok)

- static double DblChan(string tok, double pctDiv)

- static double ParseDbl(string tok)

- static int Hue360(string tok)

- static void Hexcone(int h, int c, out int r, out int g, out int b)

- static int ParseHsl(string val)

- static int ParseHwb(string val)

- static int ParseOklabLike(string val, bool polar)

- static int OklabToRgb(double L, double ax, double bx, int alpha)

- static int ParseCielabLike(string val, bool polar)

- static int LabToRgb(double L, double ax, double bx, int alpha)

- static int ByteOf(int c, int shift)

- static int Gamma255(double c)

- static double CosD(double deg)

- static double SinD(double deg)

- static int ParseColorMix(string val)

- static string TakePct(string s, out int pct)

- static int Chan(string tok)

- static int Clamp255(int v)

- static int Pack(int a, int r, int g, int b)

- static int NamedColor(string name)

- static int HexToInt(string h)

- static int HexDigit(string ch)

- static bool Digit(string ch)

- static bool IsNumeric(string tok)

- static int Num(string val)

- static int Perm(string val)

- static int Ms(string val)

- static int Len(StyleBox b, string val)

- static int LenField(StyleBox b, string val, long source)

- static int LenStored(StyleBox b, string val, long source)

- static int UnitResult(StyleBox b, int n, bool physical)

- static int FontUnit(StyleBox b, bool root)

- static bool HasMath(string v)

- static int Round1000(int perm)

- static int NumPerm(string numStr)

- static int UnitPx(StyleBox b, string tok)

- static int CssMath(StyleBox b, string val)

- static List<string> MathTokens(string s)

- static int MathExpr(StyleBox b, MathCursor c)

- static int MathTerm(StyleBox b, MathCursor c)

- static int MathFactor(StyleBox b, MathCursor c)

- static int ParseInt(string s)

- static List<string> Tokens(string val)

- static bool StartsWith(string s, string prefix)

- static int IndexOf(string s, string ch)

- static bool EndsWith(string s, string suffix)

- static int LastIndexOf(string s, string ch)

- static string Lower(string s)

- static string Trim(string s)

- static int DockValue(string text)

- static string Scalar(JsonValue v)


## Subscription (class)

- int key;

- Action handler;

- Subscription(int key, Action handler)


## Text (class)

- static string ByteChar(int b)

- static string CharStr(int code)

- static bool IsPrintable(int code)

- static int SeqLen(int b)

- static int PrevCharStart(string s, int pos)

- static int NextCharEnd(string s, int pos)

- static int ClampCharStart(string s, int pos)

- static string DropLast(string s)

- static string Left(string s, int pos)

- static string Right(string s, int pos)

- static string InsertAt(string s, int pos, string ins)

- static string RemoveAt(string s, int pos)

- static string DeleteBefore(string s, int pos)

- static int Chars(string s)

- static int CpAt(string s, int pos)

- static bool IsExtend(int cp)

- static int Graphemes(string s)


## TextWrap (class)

- static string Trim(string s)

- static WrappedText LinesAt(string s, int maxW, int fs)

- static List<string> Lines(string s, int maxW, int fs)

- static List<string> SplitLines(string s)

- static int ColFromX(string s, int px, int fontSize)


## Theme (class)

- int primary;

- int primaryHover;

- int primaryPressed;

- int info;

- int infoHover;

- int infoPressed;

- int success;

- int successHover;

- int successPressed;

- int warning;

- int warningHover;

- int warningPressed;

- int error;

- int errorHover;

- int errorPressed;

- int textPrimary;

- int textSecondary;

- int textTertiary;

- int textDisabled;

- int textInverse;

- int bgPrimary;

- int bgSecondary;

- int bgTertiary;

- int bgHover;

- int bgActive;

- int bgDisabled;

- int borderPrimary;

- int borderSecondary;

- int borderHover;

- int borderFocus;

- int divider;

- int overlay;

- int glass;

- int glassBlur;

- int glassTint;

- int glassChromeTint;

- int glassSheen;

- int glassRadius;

- int glassShadowDy;

- int glassShadowBlur;

- int gradient;

- int bgGradTop;

- int bgGradBottom;

- int neu;

- int brutal;

- int fx;

- int scrollbar;

- int scrollbarHover;

- int tooltipBg;

- int tooltipBorder;

- int tooltipText;

- int tooltipTextMuted;

- int scrim;

- int scrimChip;

- int scrimChipHover;

- int onScrim;

- int statusDebugBg;

- int chart1;

- int chart2;

- int chart3;

- int chart4;

- int chart5;

- int chart6;

- int chart7;

- int chart8;

- int borderRadiusSmall;

- int borderRadiusMedium;

- int borderRadiusLarge;

- int borderWidth;

- int fontSizeTiny;

- int fontSizeSmall;

- int fontSizeMedium;

- int fontSizeLarge;

- int fontSizeHuge;

- int heightTiny;

- int heightSmall;

- int heightMedium;

- int heightLarge;

- int paddingTiny;

- int paddingSmall;

- int paddingMedium;

- int paddingLarge;

- int gapSmall;

- int gapMedium;

- int gapLarge;

- int iconSizeSmall;

- int iconSizeMedium;

- int iconSizeLarge;

- int iconSizeCaption;

- int shadowColor;

- int animFastMs;

- int animMediumMs;

- int animSlowMs;

- static Theme Light()

- static Theme Dark()

- static int Rgb(int r, int g, int b)

- static int Rgba(int r, int g, int b, int a)

- int GlassTint()

- bool IsLightBg()

- void ApplyAccent(int color)

- void MakeSurfacesOpaque()

- void ApplyTranslucency(int surface, int chrome, int rail)

- static int TokenColor(JsonValue v)

- static void SetToken(Theme t, string key, JsonValue v)

- static Theme BaseTheme(JsonValue b)

- static void ApplyTokens(Theme t, JsonValue obj)

- static Theme FromTokens(string json)

- void ScaleByDpi(int dpiPercent)

- ThemeMetricsSnapshot Snapshot()

- List<int> SnapshotMetrics()

- void ApplySnapshotScaled(ThemeMetricsSnapshot bm, int num, int den)

- void ApplyMetricsScaled(List<int> bm, int num, int den)

- static int DensityPermille(int density)

- static string DensityName(int density)

- static int DensityOf(string name)

- void ApplyDensitySnapshot(ThemeMetricsSnapshot bm, int density)

- void ApplyDensityScaled(List<int> bm, int density)


## ThemeMetricsSnapshot (class)

- public int fontSizeTiny;

- public int fontSizeSmall;

- public int fontSizeMedium;

- public int fontSizeLarge;

- public int fontSizeHuge;

- public int heightTiny;

- public int heightSmall;

- public int heightMedium;

- public int heightLarge;

- public int paddingTiny;

- public int paddingSmall;

- public int paddingMedium;

- public int paddingLarge;

- public int gapSmall;

- public int gapMedium;

- public int gapLarge;

- public int iconSizeSmall;

- public int iconSizeMedium;

- public int iconSizeLarge;

- public int iconSizeCaption;

- public int borderRadiusSmall;

- public int borderRadiusMedium;

- public int borderRadiusLarge;

- public int borderWidth;

- public ThemeMetricsSnapshot()


## Ui (class)

- static bool Clicked(App app, int id)

- static bool OwnsClick(App app, int id)

- static bool PressedDown(App app, int id)

- static bool PressedOutside(App app, int id)

- static bool Hovered(App app, int id)

- static bool Over(App app, int x, int y, int w, int h)

- static bool ClickedIn(App app, int x, int y, int w, int h)

- static bool Pressed(App app, int id)

- static bool Focused(App app, int id)

- static bool Active(App app, int id)

- static int HotId(App app)

- static bool MouseReleased(App app)

- static bool MousePressed(App app)

- static bool Entered(App app, int id)

- static bool Left(App app, int id)

- static bool FocusGained(App app, int id)

- static bool FocusLost(App app, int id)

- static bool DoubleClicked(App app, int id)

- static int KeyDown(App app)

- static bool KeyPressed(App app, int code)

- static bool Tapped(App app, int id)

- static int Swipe(App app)

- static bool SwipedOn(App app, int id, int dir)

- static bool LongPressed(App app, int id)

- static bool MouseUpOn(App app, int id)

- static bool MovedOver(App app, int id)

- static bool Wheeled(App app, int id)

- static int WheelDelta(App app)

- static bool KeyDownOn(App app, int id)

- static int KeyUp(App app)

- static bool KeyReleased(App app, int code)

- static bool KeyUpOn(App app, int id)

- static bool RightClicked(App app, int id)

- static bool RightPressedDown(App app, int id)

- static bool SwipedAny(App app, int id)

- static bool Dragging(App app, int id)

- static bool Dropped(App app, int id)

- static int ThemeMs(App app, int kind)

- static int HoverLevel(App app, int id)

- static int HoverLevelMs(App app, int id, int ms)

- static int HoverLevelMsIn(App app, int id, int ms, int x, int y, int w, int h)

- static bool HoldTipIn(App app, int id, int x, int y, int w, int h)

- static int PressLevel(App app, int id)

- static int PressLevelMs(App app, int id, int ms)

- static int PressLevelMsIn(App app, int id, int ms, int x, int y, int w, int h)

- static int FocusLevel(App app, int id)

- static int FocusLevelMs(App app, int id, int ms)

- static int FocusLevelMsIn(App app, int id, int ms, int x, int y, int w, int h)

- static int LiftLevel(App app, int id, int maxPx)

- static bool Activate(App app, int id, int x, int y, int w, int h)

- static void Ripple(App app, int id, int x, int y, int w, int h, int color)


## UiErrorLog (class)

- static int keep=100;

- static List<string> recent;

- static int total;

- static string lastBody="";

- static int repeats;

- static int Record(string origin, string message)

- static int Count()

- static List<string> Recent()

- static string LogPath()

- static void Clear()

- static void Append(string line)


## UiEvent (class)

- List<Action> handlers;

- List<ControlEvent> senderHandlers;

- UiEvent()

- static UiEvent op_add(UiEvent self, Action handler)

- static UiEvent op_sub(UiEvent self, Action handler)

- void Add(Action handler)

- void AddS(ControlEvent h)

- void RemoveS(ControlEvent h)

- int CountS()

- int CountAll()

- void Clear()

- int Count()

- void Raise()

- void RaiseS(Control sender)

- void RaiseIf(bool fire)

- void RaiseIfS(bool fire, Control sender)

- void Post()

- void PostS(Control sender)


## UserComponentRegistry (class)

- static List<UserComponent> components;

- static void EnsureInit()

- static void Register(string name, string json)

- static void SetAll(List<UserComponent> comps)

- static string JsonOf(string name)

- static Control Build(string name, JsonValue instanceProps)

- static Control Build(string name)

- static void ApplyProps(Control root, JsonValue compRoot, JsonValue instanceProps)


## WhyTally (class)

- string why;

- int n;

- WhyTally(string why)


## WidgetEvents (class)

- UiEvent Click;

- UiEvent DoubleClick;

- UiEvent RightClick;

- UiEvent MouseDown;

- UiEvent MouseUp;

- UiEvent Enter;

- UiEvent Leave;

- UiEvent Move;

- UiEvent Wheel;

- UiEvent Focus;

- UiEvent Blur;

- UiEvent KeyDown;

- UiEvent KeyUp;

- UiEvent KeyPress;

- UiEvent Tap;

- UiEvent LongPress;

- UiEvent Swipe;

- UiEvent DragStart;

- UiEvent Drag;

- UiEvent Drop;

- WidgetEvents()

- bool AddByName(string evt, Action a)

- bool AddByNameS(string evt, ControlEvent a)

- bool Any()

- void Fire(App app, int id, Control sender)


## WidgetId (class)

- static int seq;

- static int frameBase;

- static int Next()

- static void Mark()

- static void ResetFrame()

- static int SeqValue()

- static void SetSeq(int v)

- static int Block(int n)


## WidgetSize (class)

- const int Mini=24;

- const int Small=28;

- const int Medium=32;

- const int Large=40;


## WindowShapeMask (class)

- int cachedWidth;

- int cachedHeight;

- int cachedDpiScale;

- List<WindowShapeRegion> cachedRegions;

- byte[]cachedCoverage;

- bool cachedShadowBand;

- WindowShapeMask()

- bool Matches(int width, int height, int dpiScale, List<WindowShapeRegion> regions)

- byte[]Coverage(int width, int height, int dpiScale, List<WindowShapeRegion> regions)

- bool HasShadowBand()

- static long SdfFp(List<WindowShapeRegion> regions, long sx, long sy, int dpiScale)

- static long ISqrt64(long v)

- static int SnapshotStraightArgb(int argb, int coverage, bool shadowBand)

- static int LayeredArgb(int argb, int coverage, bool shadowBand)


## WindowShapeRegion (class)

- public int kind;

- public int x;

- public int y;

- public int width;

- public int height;

- public int radius;

- public WindowShapeRegion()

- public WindowShapeRegion(int kind, int x, int y, int width, int height, int radius)


## WrappedLine (class)

- public string text;

- public int start;

- public WrappedLine(string text, int start)


## WrappedText (class)

- List<WrappedLine> items;

- List<string> lines;

- List<int> starts;

- WrappedText()

- void Add(string line, int start)


## Control (delegate)

`delegate Control HtmlLoadFn(App app, string html, HtmlHandlers handlers, string baseDir);`


## Control (delegate)

`delegate Control CloneTreeFn(Control root);`


## Control (delegate)

`delegate Control ControlFactoryFn(string kind);`


## Control (delegate)

`delegate Control NavPageFactory();`


## bool (delegate)

`delegate bool LinkNavigateFn(App app, string url);`


## bool (delegate)

`delegate bool ChildWindowStop();`


## int (delegate)

`delegate int BandGridColorOf(int row, int col, string text, int num);`


## void (delegate)

`delegate void FrameBody();`


## void (delegate)

`delegate void AppHookFn(App app);`


## void (delegate)

`delegate void LinkScanFn(Control root, App app);`


## void (delegate)

`delegate void Action();`


## void (delegate)

`delegate void ControlEvent(Control sender);`


## void (delegate)

`delegate void NativeClipFn(int handle, string spec);`


## void (delegate)

`delegate void NavEnterHandler(NavWindow win, JsonValue args);`


## Color (struct)

- uint argb;

- static Color FromArgb(uint packed)

- static Color Parse(string css)

- static Color FromRGBA(int r, int g, int b, int a)

- static Color FromRGB(int r, int g, int b)

- static Color FromHex(int hex)

- static Color None()

- bool IsNone()

- int A()

- int R()

- int G()

- int B()

- Color WithAlpha(int a)

- uint ToArgb()

- int ToARGB()

- string ToCss()

- static string Hex2(int v)

- static Color White()

- static Color Black()

- static Color Red()

- static Color Green()

- static Color Blue()

- static Color Yellow()

- static Color Gray()
