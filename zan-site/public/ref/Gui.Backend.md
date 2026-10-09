# Gui.Backend

> 源码: `packages/Zan.Gui/src/Gui/Backend/Native.zan`, `packages/Zan.Gui/src/Gui/Backend/UiDriver.zan`, `packages/Zan.Gui/src/Gui/Backend/Win32Shell.zan`


## Clipboard (class)

- static string last;

- [DllImport("zan_gui")]static extern int zan_gui_set_clipboard(string text);

- [DllImport("zan_gui")]static extern string zan_gui_get_clipboard();

- static bool SetText(string text)

- static string GetText()


## ControlNode (class)

- string kind;

- string name;

- int dock;

- int visible;

- int x;

- int y;

- int w;

- int h;

- int prefW;

- int prefH;

- List<ControlNode> kids;


## ControlTreeDoc (class)

- ControlNode root;


## EventKind (class)

- static int None()

- static int MouseMove()

- static int MouseDown()

- static int MouseUp()

- static int KeyDown()

- static int KeyUp()

- static int TextInput()

- static int Resize()

- static int Close()

- static int Blur()

- static int Scroll()


## HitRegionDoc (class)

- int z;

- int id;

- int x;

- int y;

- int w;

- int h;

- int type;

- string label;


## HitRegionsDoc (class)

- int count;

- List<HitRegionDoc> regions;


## Ime (class)

- [DllImport("zan_gui")]static extern void zan_gui_set_ime_pos(int x, int y);

- [DllImport("zan_gui")]static extern void zan_gui_set_ime_open(int on);

- [DllImport("zan_gui")]static extern string zan_gui_ime_composing();

- static void SetCaret(int x, int y)

- static void SetOpen(bool on)

- static string Composing()


## Keys (class)

- static int Escape()

- static int Enter()

- static int Tab()

- static int Backspace()

- static int Delete()

- static int Left()

- static int Up()

- static int Right()

- static int Down()

- static int Home()

- static int End()

- static int PageUp()

- static int PageDown()

- static int Space()

- static int F1()

- static int F2()

- static int F5()

- static int F11()

- static int A()

- static int C()

- static int V()

- static int X()

- static int Z()

- static int S()


## Modifiers (class)

- static int Ctrl()

- static int Shift()

- static int Alt()

- static bool HasCtrl(int mods)

- static bool HasShift(int mods)

- static bool HasAlt(int mods)


## SysFile (class)

- static bool Write(string path, string content)


## ThemeDoc (class)

- int scale;

- int textPrimary;

- int textSecondary;

- int textTertiary;

- int textDisabled;

- int bgPrimary;

- int bgSecondary;

- int bgTertiary;

- int borderPrimary;

- int borderSecondary;

- int divider;

- int primary;

- int shadowColor;

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

- int glass;

- int gradient;

- int neu;

- int brutal;


## UiDriver (class)

- static extern string getenv(string name);

- static bool active;

- static bool finished;

- static App app;

- static Control root;

- static List<string> cmds;

- static int pc;

- static int waitUntilUs;

- static bool waitArmed;

- static bool waitFullPresent;

- static string outDir;

- static string resultsPath;

- static int passCount;

- static int failCount;

- static List<UiProbe> probes;

- static bool Active()

- static void Begin(App a)

- static void SetRoot(Control c)

- static void NotifyPresented(App owner)

- static string Env(string name)

- static void SetProbe(string name, string val)

- static string GetProbe(string name)

- static int ParseProbeId(string s)

- static void Tick()

- static bool Exec(string line)

- static void ClickAt(int x, int y, int button)

- static void Inject(int kind, int x, int y, int button, int code, int mods)

- static HitRegion FindRegion(int id)

- static HitRegion FindRegionByLabel(string name)

- static void DoDump(List<string> t)

- static void DoAssert(List<string> t, string trimmed)

- static void Finish()

- static string HitRegionsJson()

- static string TreeJson()

- static ControlNode NodeOf(Control c)

- static string ThemeJson()

- static string JsonBool(bool b)

- static void Note(string msg)

- static string OutPath(string file)

- static int ArgI(List<string> t, int idx, int def)

- static List<string> Tok(string s)

- static string RemainderAfter(string s, int count)

- static string Unquote(string s)

- static string Trim(string s)

- static bool StartsWith(string s, string pfx)

- static bool Contains(string s, string sub)

- static string JsonEsc(string s)


## UiProbe (class)

- public string name;

- public string value;

- public UiProbe(string name, string value)


## Win32CaptionConfig (class)

- public nint hwnd;

- public int capCount;

- public int zoneWidth;

- public int dpi;

- public int titlebarLogical;

- public int titlebarH;

- public int buttonW;

- public int minTrackLogicalW;

- public int minTrackLogicalH;

- public int minTrackPhysicalW;

- public int minTrackPhysicalH;

- public bool physicalStage;

- public Win32CaptionConfig(nint hwnd, int capCount, int zoneWidth)


## Win32DirtyRect (class)

- public int x;

- public int y;

- public int w;

- public int h;

- public Win32DirtyRect(int x, int y, int w, int h)


## Win32HitGuardEntry (class)

- public nint hwnd;

- public int x;

- public int y;

- public int w;

- public int h;

- public Win32HitGuardEntry(nint hwnd, int x, int y, int w, int h)


## Win32QueuedEvent (class)

- public nint hwnd;

- public int kind;

- public int x;

- public int y;

- public int button;

- public int keycode;

- public int mods;

- public Win32QueuedEvent(nint hwnd, int kind, int x, int y, int button, int keycode, int mods)


## Win32ShapeRegion (class)

- public int kind;

- public int x;

- public int y;

- public int w;

- public int h;

- public int r;

- public Win32ShapeRegion(int k, int x, int y, int w, int h, int r)


## Win32Shell (class)

- [DllImport("zan_gui")]static extern nint zan_gui_guard_wndproc(nint proc);

- [DllImport("user32", EntryPoint="RegisterClassExW")]static extern ushort RegisterClassExW(nint wc);

- [DllImport("shell32", EntryPoint="DragAcceptFiles")]static extern void DragAcceptFiles(nint hwnd, int accept);

- [DllImport("shell32", EntryPoint="DragQueryFileW")]static extern int DragQueryFileW(nint hdrop, int index, nint buf, int cch);

- [DllImport("shell32", EntryPoint="DragQueryPoint")]static extern int DragQueryPoint(nint hdrop, nint pt);

- [DllImport("shell32", EntryPoint="DragFinish")]static extern void DragFinish(nint hdrop);

- [DllImport("shell32", EntryPoint="ShellExecuteW")]static extern nint WinShellExecuteW(nint hwnd, nint op, nint file, nint args, nint dir, int show);

- static void ShellOpenUrl(string url)

- [DllImport("user32", EntryPoint="CreateWindowExW")]static extern nint CreateWindowExW(int exStyle, nint cls, nint title, int style, int x, int y, int w, int h, nint parent, nint menu, nint inst, nint param);

- [DllImport("user32", EntryPoint="DefWindowProcW")]static extern nint DefWindowProcW(nint hwnd, int msg, nint wp, nint lp);

- [DllImport("user32", EntryPoint="ShowWindow")]static extern int ShowWindow(nint hwnd, int cmd);

- [DllImport("user32", EntryPoint="UpdateWindow")]static extern int UpdateWindow(nint hwnd);

- [DllImport("user32", EntryPoint="SetForegroundWindow")]static extern int SetForegroundWindow(nint hwnd);

- [DllImport("user32", EntryPoint="SetFocus")]static extern nint SetFocus(nint hwnd);

- [DllImport("user32", EntryPoint="DestroyWindow")]static extern int DestroyWindow(nint hwnd);

- [DllImport("user32", EntryPoint="PostMessageW")]static extern int PostMessageW(nint hwnd, int msg, nint wp, nint lp);

- [DllImport("user32", EntryPoint="PostQuitMessage")]static extern void PostQuitMessage(int code);

- [DllImport("user32", EntryPoint="PeekMessageW")]static extern int PeekMessageW(nint msg, nint hwnd, int min, int max, int remove);

- [DllImport("user32", EntryPoint="GetMessageW")]static extern int GetMessageW(nint msg, nint hwnd, int min, int max);

- [DllImport("user32", EntryPoint="TranslateMessage")]static extern int TranslateMessage(nint msg);

- [DllImport("user32", EntryPoint="DispatchMessageW")]static extern nint DispatchMessageW(nint msg);

- [DllImport("user32", EntryPoint="MsgWaitForMultipleObjects")]static extern int MsgWaitForMultipleObjects(int count, nint handles, int waitAll, int ms, int mask);

- [DllImport("user32", EntryPoint="GetClientRect")]static extern int GetClientRect(nint hwnd, nint rect);

- [DllImport("user32", EntryPoint="GetWindowRect")]static extern int GetWindowRect(nint hwnd, nint rect);

- [DllImport("user32", EntryPoint="AdjustWindowRect")]static extern int AdjustWindowRect(nint rect, int style, int menu);

- [DllImport("user32", EntryPoint="SetWindowPos")]static extern int SetWindowPos(nint hwnd, nint after, int x, int y, int w, int h, int flags);

- [DllImport("user32", EntryPoint="GetWindowPlacement")]static extern int GetWindowPlacement(nint hwnd, nint placement);

- [DllImport("user32", EntryPoint="IsIconic")]static extern int IsIconic(nint hwnd);

- [DllImport("user32", EntryPoint="IsWindowVisible")]static extern int IsWindowVisible(nint hwnd);

- [DllImport("user32", EntryPoint="GetForegroundWindow")]static extern nint GetForegroundWindow();

- [DllImport("user32", EntryPoint="SetWindowTextW")]static extern int SetWindowTextW(nint hwnd, nint title);

- [DllImport("user32", EntryPoint="LoadCursorW")]static extern nint LoadCursorW(nint inst, nint name);

- [DllImport("user32", EntryPoint="LoadIconW")]static extern nint LoadIconW(nint inst, nint name);

- [DllImport("user32", EntryPoint="SetCursor")]static extern nint SetCursor(nint cursor);

- [DllImport("user32", EntryPoint="SetCapture")]static extern nint SetCapture(nint hwnd);

- [DllImport("user32", EntryPoint="ReleaseCapture")]static extern int ReleaseCapture();

- [DllImport("user32", EntryPoint="GetKeyState")]static extern short GetKeyState(int vk);

- [DllImport("user32", EntryPoint="ScreenToClient")]static extern int ScreenToClient(nint hwnd, nint point);

- [DllImport("user32", EntryPoint="ClientToScreen")]static extern int ClientToScreen(nint hwnd, nint point);

- [DllImport("user32", EntryPoint="GetDC")]static extern nint GetDC(nint hwnd);

- [DllImport("user32", EntryPoint="ReleaseDC")]static extern int ReleaseDC(nint hwnd, nint dc);

- [DllImport("user32", EntryPoint="GetSystemMetrics")]static extern int GetSystemMetrics(int index);

- [DllImport("user32", EntryPoint="MonitorFromWindow")]static extern nint MonitorFromWindow(nint hwnd, int flags);

- [DllImport("user32", EntryPoint="GetMonitorInfoW")]static extern int GetMonitorInfoW(nint monitor, nint info);

- [DllImport("user32", EntryPoint="GetWindowLongPtrW")]static extern nint GetWindowLongPtrW(nint hwnd, int index);

- [DllImport("user32", EntryPoint="SetWindowLongPtrW")]static extern nint SetWindowLongPtrW(nint hwnd, int index, nint newLong);

- [DllImport("user32", EntryPoint="SetLayeredWindowAttributes")]static extern int SetLayeredWindowAttributes(nint hwnd, int key, byte alpha, int flags);

- [DllImport("user32", EntryPoint="UpdateLayeredWindow")]static extern int UpdateLayeredWindow(nint hwnd, nint dcDst, nint ptDst, nint size, nint dcSrc, nint ptSrc, int key, nint blend, int flags);

- [DllImport("user32", EntryPoint="BeginPaint")]static extern nint BeginPaint(nint hwnd, nint ps);

- [DllImport("user32", EntryPoint="EndPaint")]static extern int EndPaint(nint hwnd, nint ps);

- [DllImport("user32", EntryPoint="FillRect")]static extern int FillRect(nint dc, nint rect, nint brush);

- [DllImport("user32", EntryPoint="OpenClipboard")]static extern int OpenClipboard(nint hwnd);

- [DllImport("user32", EntryPoint="CloseClipboard")]static extern int CloseClipboard();

- [DllImport("user32", EntryPoint="EmptyClipboard")]static extern int EmptyClipboard();

- [DllImport("user32", EntryPoint="SetClipboardData")]static extern nint SetClipboardData(int format, nint mem);

- [DllImport("user32", EntryPoint="GetClipboardData")]static extern nint GetClipboardData(int format);

- [DllImport("user32", EntryPoint="IsClipboardFormatAvailable")]static extern int IsClipboardFormatAvailable(int format);

- [DllImport("gdi32", EntryPoint="SetDIBitsToDevice")]static extern int SetDIBitsToDevice(nint dc, int xd, int yd, int w, int h, int xs, int ys, int startScan, int scanLines, nint bits, nint info, int usage);

- [DllImport("gdi32", EntryPoint="CreateSolidBrush")]static extern nint CreateSolidBrush(int color);

- [DllImport("zan_gui")]static extern int zan_gui_gdi_present(nint hwnd, int surfaceId, nint rects, int rectCount);

- [DllImport("zan_gui")]static extern void zan_gui_gdi_present_drop(nint hwnd);

- [DllImport("gdi32", EntryPoint="DeleteObject")]static extern int DeleteObject(nint obj);

- [DllImport("gdi32", EntryPoint="CreateCompatibleDC")]static extern nint CreateCompatibleDC(nint dc);

- [DllImport("gdi32", EntryPoint="CreateDIBSection")]static extern nint CreateDIBSection(nint dc, nint info, int usage, ref nint bits, nint section, int offset);

- [DllImport("gdi32", EntryPoint="SelectObject")]static extern nint SelectObject(nint dc, nint obj);

- [DllImport("gdi32", EntryPoint="DeleteDC")]static extern int DeleteDC(nint dc);

- [DllImport("gdi32", EntryPoint="GetDeviceCaps")]static extern int GetDeviceCaps(nint dc, int index);

- [DllImport("gdi32", EntryPoint="CreateRoundRectRgn")]static extern nint CreateRoundRectRgn(int l, int t, int r, int b, int rw, int rh);

- [DllImport("gdi32", EntryPoint="CreateEllipticRgn")]static extern nint CreateEllipticRgn(int l, int t, int r, int b);

- [DllImport("gdi32", EntryPoint="CombineRgn")]static extern int CombineRgn(nint dst, nint src1, nint src2, int mode);

- [DllImport("user32", EntryPoint="SetWindowRgn")]static extern int SetWindowRgn(nint hwnd, nint rgn, bool redraw);

- [DllImport("kernel32", EntryPoint="GetModuleHandleW")]static extern nint GetModuleHandleW(nint name);

- [DllImport("kernel32", EntryPoint="Sleep")]static extern void SleepW(int ms);

- [DllImport("kernel32", EntryPoint="GetTickCount")]static extern int GetTickCount();

- [DllImport("kernel32", EntryPoint="QueryPerformanceCounter")]static extern int QueryPerformanceCounter(nint slot);

- [DllImport("kernel32", EntryPoint="QueryPerformanceFrequency")]static extern int QueryPerformanceFrequency(nint slot);

- [DllImport("winmm", EntryPoint="timeBeginPeriod")]static extern int TimeBeginPeriod(int ms);

- [DllImport("kernel32", EntryPoint="GlobalAlloc")]static extern nint GlobalAlloc(int flags, long bytes);

- [DllImport("kernel32", EntryPoint="GlobalLock")]static extern nint GlobalLock(nint mem);

- [DllImport("kernel32", EntryPoint="GlobalUnlock")]static extern int GlobalUnlock(nint mem);

- [DllImport("kernel32", EntryPoint="GlobalFree")]static extern nint GlobalFree(nint mem);

- [DllImport("kernel32", EntryPoint="CreateFileW")]static extern nint CreateFileW(nint path, int access, int share, nint sa, int disposition, int flags, nint template);

- [DllImport("kernel32", EntryPoint="WriteFile")]static extern int WriteFile(nint file, nint buf, int bytes, nint written, nint overlapped);

- [DllImport("kernel32", EntryPoint="CloseHandle")]static extern int CloseHandle(nint h);

- [DllImport("kernel32", EntryPoint="InitializeCriticalSection")]static extern void InitializeCriticalSection(nint cs);

- [DllImport("kernel32", EntryPoint="EnterCriticalSection")]static extern void EnterCriticalSection(nint cs);

- [DllImport("kernel32", EntryPoint="LeaveCriticalSection")]static extern void LeaveCriticalSection(nint cs);

- [DllImport("imm32", EntryPoint="ImmGetContext")]static extern nint ImmGetContext(nint hwnd);

- [DllImport("imm32", EntryPoint="ImmSetCompositionWindow")]static extern int ImmSetCompositionWindow(nint imc, nint form);

- [DllImport("imm32", EntryPoint="ImmSetCandidateWindow")]static extern int ImmSetCandidateWindow(nint imc, nint form);

- [DllImport("imm32", EntryPoint="ImmReleaseContext")]static extern int ImmReleaseContext(nint hwnd, nint imc);

- [DllImport("dwmapi", EntryPoint="DwmExtendFrameIntoClientArea")]static extern int DwmExtendFrameIntoClientArea(nint hwnd, nint margins);

- [DllImport("zan_gui")]static extern nint zan_gui_get_pixels(int surfaceId);

- [DllImport("zan_gui")]static extern int zan_gui_present_window(int surfaceId, nint nativeWindow);

- [DllImport("zan_gui")]static extern void zan_gui_release_window(nint nativeWindow);

- [DllImport("zan_gui")]static extern int zan_gui_surface_width(int surfaceId);

- [DllImport("zan_gui")]static extern int zan_gui_surface_height(int surfaceId);

- [DllImport("zan_gui")]static extern int zan_gui_surface_painted(int surfaceId);

- static bool ready;

- static nint instance;

- static nint className;

- static nint mainHwnd;

- static nint eventHwnd;

- static nint evHwnd;

- static List<string> dropped;

- static int evKind;

- static int evX;

- static int evY;

- static int evButton;

- static int evKeyCode;

- static int evMods;

- static bool hasEvent;

- static long evSeq;

- static List<Win32QueuedEvent> postEvents;

- static int windowWidth;

- static int windowHeight;

- static int minTrackW;

- static int minTrackH;

- static int dpi;

- static int titlebarH;

- static int buttonW;

- static List<Win32CaptionConfig> captionConfigs;

- static List<nint> fixedHwnd;

- static List<nint> noMaxHwnd;

- static int imeX;

- static int imeY;

- static bool glassOn;

- static int glassTint;

- static int shapeMode;

- static List<Win32ShapeRegion> shapeRegionList;

- static int winOpacityPercent;

- static bool hasShadowBand;

- static WindowShapeMask shapeMask;

- static List<WindowShapeRegion> shapeMaskRegions;

- static List<Win32SurfaceBinding> surfaceBindings;

- static int lastSurface;

- static List<Win32DirtyRect> dirtyRects;

- static bool dirtyOverflow;

- static nint dirtyHwnd;

- static List<nint> dirtyLost;

- static bool presentLog;

- static extern string getenv(string name);

- static bool wmPaintBlit;

- static bool forceFullUpload;

- static nint forceFullHwnd;

- static List<Win32QueuedEvent> injectEvents;

- static nint injectCs;

- static nint lwDc;

- static nint lwBitmap;

- static nint lwOld;

- static nint lwBits;

- static int lwW;

- static int lwH;

- static List<Win32HitGuardEntry> hitGuardEntries;

- static nint scratchRect;

- static nint scratchRects;

- static nint scratchMsg;

- static nint bitmapInfo;

- static int bgColor;

- static int S32(nint p, int off)

- static void Init()

- static void EnableDpiAwareness()

- static int ModsNow()

- static int LoWord(nint v)

- static int HiWord(nint v)

- static void Post(nint hwnd, int kind, int x, int y, int button, int keycode, int mods)

- static void OnDropFiles(nint hwnd, nint hdrop)

- static bool DropPending()

- static List<string> TakeDropped()

- static SizePaintBody sizePaint;

- static bool inSizeMove;

- static void SetSizePaint(SizePaintBody body)

- static nint OnMessage(nint hwnd, int msg, nint wp, nint lp)

- static nint HitTest(nint hwnd, int screenX, int screenY)

- static int ShapeBandPx()

- static void ClearHitGuards(nint hwnd)

- static void AddHitGuard(nint hwnd, int x, int y, int w, int h)

- static bool InHitGuard(nint hwnd, int x, int y)

- static bool IsMaximized(nint hwnd)

- static void ImeReposition(nint hwnd)

- static void SetImePos(int x, int y)

- static int SurfaceOf(nint hwnd)

- static void RememberSurface(nint hwnd, int surface)

- static void FillBitmapInfo(int w, int h)

- static void Blit(nint hwnd, int surface)

- static void PresentLayered(nint hwnd, int surface)

- static void AcrylicApply(nint hwnd, int state, int tintArgb)

- static nint CreateWindow(string title, int width, int height)

- static nint CreateWindowPhysical(string title, int width, int height)

- static nint CreateWindowSized(string title, int clientW, int clientH, int deviceDpi, bool stage)

- static bool WorkArea(nint hwnd, int flags)

- static bool CenterWindow(nint hwnd)

- static void CenterOnOwner(nint hwnd)

- static void ShowWindowNow(nint hwnd)

- static void SetWindowPosition(nint hwnd, int x, int y)

- static void ResizeClient(nint hwnd, int scaledW, int scaledH)

- static void ResizeClientExact(nint hwnd, int scaledW, int scaledH)

- static void Minimize(nint hwnd)

- static void ToggleMaximize(nint hwnd)

- static void CloseWindow(nint hwnd)

- static void Destroy(nint hwnd)

- static bool Maximized(nint hwnd)

- static bool Visible(nint hwnd)

- static bool Focused(nint hwnd)

- static void SetTopmost(nint hwnd, bool on)

- static void SetTitle(nint hwnd, string title)

- static int CaptionButtonsOf(nint hwnd)

- static void SetCaptionZone(nint hwnd, int width)

- static int CaptionZoneOf(nint hwnd)

- static bool ResizableOf(nint hwnd)

- static void SetResizable(nint hwnd, bool on)

- static bool MaximizableOf(nint hwnd)

- static void SetMaximizable(nint hwnd, bool on)

- static List<nint> toolHwnd;

- static void SetToolWindow(nint hwnd, bool on)

- static bool IsToolWindow(nint hwnd)

- static void SetRoundCorners(nint hwnd, bool on)

- static void SetCaptionButtons(nint hwnd, int count)

- static void ForgetCaptionButtons(nint hwnd)

- static int TitlebarHeight()

- static int TitlebarHeight(nint hwnd)

- static void SetTitlebarHeight(int devicePx)

- static void SetTitlebarHeight(nint hwnd, int devicePx)

- static int CaptionButtonWidth()

- static int CaptionButtonWidth(nint hwnd)

- static int ClientWidth(nint hwnd)

- static int ClientHeight(nint hwnd)

- static int WindowWidth()

- static int WindowHeight()

- static void SetCursorShape(int kind)

- static List<long> noLayerHosts;

- static bool IsLayerHost(nint hwnd)

- static void ForbidLayered(nint hwnd)

- static bool StripLayered(nint hwnd)

- static bool SweepLayered()

- static void EnableGlass(nint hwnd, int tintArgb)

- static void DisableGlass(nint hwnd)

- static void SetOpacity(nint hwnd, int percent)

- static void SetShape(nint hwnd, string spec)

- static void FitShapeWindow(nint hwnd, List<Win32ShapeRegion> regs)

- static bool DwmComposited()

- static void SetShapeRgn(nint hwnd, List<Win32ShapeRegion> regs)

- static byte[]ShapeMask(int sw, int sh)

- static void Present(nint hwnd, int surface)

- static void PresentDirty(nint hwnd, int x, int y, int w, int h)

- static void PresentFull(nint hwnd)

- static void NoteDirtyLost(nint hwnd)

- static bool TakeDirtyLost(nint hwnd)

- static void ClearEvent()

- static bool DrainInjected()

- static bool DrainPosted()

- static int PollEvent()

- static int WaitEvent()

- static int WaitEventTimeout(int ms)

- static void Wake()

- static void InjectEvent(nint hwnd, int kind, int x, int y, int button, int keycode, int mods)

- static int InjectPending()

- static int EventKindValue()

- static long EventSeqValue()

- static int EventXValue()

- static int EventYValue()

- static int EventButtonValue()

- static int EventKeyCodeValue()

- static int EventModsValue()

- static nint EventWindow()

- static nint WndProcWindow()

- static int TickMs()

- static int TickUs()

- static void Sleep(int ms)

- static bool OpenClipboardRetry()

- static bool SetClipboard(string text)

- static string GetClipboard()

- static int DpiScale()

- static int DpiPercent(int deviceDpi)

- static int ScaleForDpi(int logical, int deviceDpi)

- static int DpiScale(nint hwnd)

- static int WindowDpi(nint hwnd)

- static int QueryWindowDpi(nint hwnd)

- static Win32CaptionConfig DpiConfigOf(nint hwnd)

- static void UpdateWindowDpi(Win32CaptionConfig cfg, int deviceDpi)

- static nint FinishWindowDpi(nint hwnd, int width, int height, bool stage)

- static bool WriteTextFile(string path, string content)

- static void SetBackground(int color)


## Win32SurfaceBinding (class)

- public nint hwnd;

- public int surfaceId;

- public Win32SurfaceBinding(nint hwnd, int surfaceId)


## Window (class)

- static bool frozenTick;

- static int frozenTickMs;

- [DllImport("user32")]static extern nint GetModuleHandleA(nint reserved);

- [DllImport("gdi32")]static extern nint GetStockObject(int fnObject);

- [DllImport("kernel32")]static extern int GetTickCount();

- [DllImport("zan_gui")]static extern int zan_gui_guard_call(nint fn, nint arg);

- [DllImport("zan_gui")]static extern nint zan_gui_guard_wndproc(nint proc);

- [DllImport("zan_gui")]static extern int zan_gui_guard_recovered();

- [DllImport("zan_gui")]static extern nint zan_gui_create_window(string title, int width, int height);

- [DllImport("zan_gui")]static extern int zan_gui_show_window(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_wait_event();

- [DllImport("zan_gui")]static extern int zan_gui_wait_event_timeout(int ms);

- [DllImport("zan_gui")]static extern int zan_gui_poll_event();

- [DllImport("zan_gui")]static extern int zan_gui_wake();

- [DllImport("zan_gui")]static extern int zan_gui_inject_event(nint hwnd, int kind, int x, int y, int button, int keycode, int mods);

- [DllImport("zan_gui")]static extern int zan_gui_inject_pending();

- [DllImport("zan_gui")]static extern int zan_gui_event_kind();

- [DllImport("zan_gui")]static extern long zan_gui_event_seq();

- [DllImport("zan_gui")]static extern string zan_gui_drop_take();

- [DllImport("zan_gui")]static extern int zan_gui_drop_pending();

- [DllImport("zan_gui")]static extern int zan_gui_event_x();

- [DllImport("zan_gui")]static extern int zan_gui_event_y();

- [DllImport("zan_gui")]static extern int zan_gui_event_button();

- [DllImport("zan_gui")]static extern int zan_gui_event_keycode();

- [DllImport("zan_gui")]static extern int zan_gui_event_mods();

- [DllImport("zan_gui")]static extern int zan_gui_event_flag();

- [DllImport("zan_gui")]static extern int zan_gui_window_width();

- [DllImport("zan_gui")]static extern int zan_gui_window_height();

- [DllImport("zan_gui")]static extern nint zan_gui_event_hwnd();

- [DllImport("zan_gui")]static extern int zan_gui_client_width(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_client_height(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_present(nint hwnd, int surfaceId);

- [DllImport("zan_gui")]static extern int zan_gui_present_dirty_add(int x, int y, int w, int h);

- [DllImport("zan_gui")]static extern void zan_gui_present_full();

- [DllImport("zan_gui")]static extern int zan_gui_set_title(nint hwnd, string title);

- [DllImport("zan_gui")]static extern int zan_gui_set_cursor(int cursorType);

- [DllImport("zan_gui")]static extern long zan_gui_get_tick_ms();

- [DllImport("zan_gui")]static extern void zan_gui_sleep_ms(int ms);

- [DllImport("zan_gui")]static extern int zan_gui_minimize(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_toggle_maximize(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_close_window(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_destroy_window(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_is_maximized(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_window_visible(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_window_focused(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_set_topmost(nint hwnd, int on);

- [DllImport("zan_gui")]static extern int zan_gui_set_caption_buttons(nint hwnd, int count);

- [DllImport("zan_gui")]static extern int zan_gui_titlebar_height();

- [DllImport("zan_gui")]static extern int zan_gui_caption_button_width();

- [DllImport("zan_gui")]static extern int zan_gui_enable_glass(nint hwnd, int tintArgb);

- [DllImport("zan_gui")]static extern int zan_gui_disable_glass(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_set_opacity(nint hwnd, int percent);

- [DllImport("zan_gui")]static extern int zan_gui_set_window_pos(nint hwnd, int x, int y);

- [DllImport("zan_gui")]static extern int zan_gui_center_window(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_clear_hit_guards(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_add_hit_guard(nint hwnd, int x, int y, int w, int h);

- [DllImport("zan_gui")]static extern int zan_gui_adopt_sdl_window(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_scene_set_renderer(nint hwnd, nint renderer);

- [DllImport("zan_gui")]static extern int zan_gui_scene_upload(nint hwnd, string bgra, int w, int h);

- [DllImport("zan_gui")]static extern int zan_gui_scene_present(nint hwnd, int surfaceId);

- public static int AdoptSdlWindow(nint sdlWindowHandle)

- public static int AdoptSdlWindow(nint sdlWindowHandle)

- public static int SceneSetRenderer(nint sdlWindowHandle, nint sdlRendererHandle)

- public static int SceneSetRenderer(nint sdlWindowHandle, nint sdlRendererHandle)

- public static int SceneUpload(nint sdlWindowHandle, string rgba, int w, int h)

- public static int SceneUpload(nint sdlWindowHandle, string rgba, int w, int h)

- public static int ScenePresent(nint sdlWindowHandle, int surfaceId)

- public static int ScenePresent(nint sdlWindowHandle, int surfaceId)

- nint handle;

- int width;

- int height;

- int localKind;

- int localX;

- int localY;

- int localButton;

- int localKey;

- int localMods;

- int localFlag;

- long localSeq;

- Window(string title, int width, int height):this(title, width, height, false)

- Window(string title, int width, int height, bool physical)

- static Window CreatePhysical(string title, int width, int height)

- static Window Offscreen(int width, int height)

- Window(int width, int height)

- void SetLocalEvent(int kind, int x, int y, int button, int key, int mods, int flag)

- static void SetSizePaint(SizePaintBody body)

- static SizePaintBody sizePaintUnused;

- static void SetResizeBackground(int color)

- void Show()

- void SetPosition(int x, int y)

- int GetDpiScale()

- int GetTitlebarHeight()

- int GetCaptionButtonWidth()

- void SetClientSize(int w, int h)

- void SetClientSizeDev(int wDev, int hDev)

- void Center()

- int WaitEvent()

- int WaitEventTimeout(int ms)

- int PollEvent()

- void Wake()

- void InjectEvent(int kind, int x, int y, int button, int keycode, int mods)

- static int InjectPending()

- List<string> TakeDroppedFiles()

- bool DropPending()

- int EventKind()

- long EventSeq()

- static long EventSeqGlobal()

- int EventX()

- int EventY()

- int EventButton()

- int EventKeyCode()

- int EventMods()

- int EventFlag()

- int GetWidth()

- int GetHeight()

- nint GetHandle()

- static nint EventHwnd()

- static nint SizePaintWindow()

- int ClientWidth()

- int ClientHeight()

- void Present(Canvas canvas)

- void PresentDirty(int x, int y, int w, int h)

- void PresentFull()

- void SetTitle(string title)

- static void SetCursor(int cursorType)

- static bool GuardCall(nint fn, nint arg)

- static nint GuardWndProc(nint proc)

- static int GuardRecovered()

- static int RawTickMs()

- static int GetTickMs()

- static void FreezeTick(int fixedMs)

- static void UnfreezeTick()

- static int GetTickUs()

- static void SleepMs(int ms)

- void Minimize()

- void ToggleMaximize()

- void Close()

- void Destroy()

- bool IsMaximized()

- bool IsVisible()

- bool IsFocused()

- void EnableGlass(int tint)

- void DisableGlass()

- void SetOpacity(int percent)

- void SetShape(string spec)

- void SetRoundCorners(bool on)

- void SetTopmost(bool on)

- void SetResizable(bool on)

- void SetMaximizable(bool on)

- void SetToolWindow(bool on)

- bool ToolWindow()

- void SetCaptionButtons(int count)

- void ClearHitGuards()

- void AddHitGuard(int x, int y, int w, int h)

- static int TitlebarHeight()

- void SetTitlebarHeight(int devicePx)

- void SetCaptionZone(int width)

- static int CaptionButtonWidth()


## int (delegate)

`delegate int SetWinCompAttrFn(nint hwnd, nint data);`


## int (delegate)

`delegate int DwmSetWindowAttrFn(nint hwnd, int attr, nint value, int size);`


## int (delegate)

`delegate int DwmIsCompFn(nint outEnabled);`


## int (delegate)

`delegate int SetProcessDpiAwarenessFn(int level);`


## int (delegate)

`delegate int SetProcessDpiAwareFn();`


## int (delegate)

`delegate int GetDpiForWindowFn(nint hwnd);`


## int (delegate)

`delegate int GetDpiForMonitorFn(nint monitor, int dpiType, nint dpiX, nint dpiY);`


## nint (delegate)

`delegate nint ZanWndProc(nint hwnd, int msg, nint wp, nint lp);`


## void (delegate)

`delegate void SizePaintBody();`
