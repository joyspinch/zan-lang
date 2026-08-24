# Gui.Backend

> 源码: `stdlib/Gui/Backend/Native.zan`, `stdlib/Gui/Backend/UiDriver.zan`, `stdlib/Gui/Backend/Win32Shell.zan`


## Clipboard (class)

系统剪贴板访问（UTF-8 文本）。

- static string last;
  - 本进程最后一次复制的文本。系统剪贴板可能瞬时被
    别的进程占着（剪贴板管理器、远程桌面……），那时候写入和
    读取都会失败；应用内部的复制粘贴不应该因此丢内容，
    所以读不到时退回这份副本。

- [DllImport("zan_gui")]static extern int zan_gui_set_clipboard(string text);

- [DllImport("zan_gui")]static extern string zan_gui_get_clipboard();

- static bool SetText(string text)
  - 将文本复制到系统剪贴板。成功返回 true。

- static string GetText()
  - 返回系统剪贴板文本（UTF-8），无文本时返回
    ""。


## ControlNode (class)

一个控件节点，由 `dump tree` 写出：控件树的形状和布局
结果。截图只能看到“这里是空的”，节点才能说出是控件
没挂上、高度算成了 0，还是被制成了不可见。

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

与运行时事件编码一致的事件类型常量。

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

驱动 `dump hitregions` 输出中的一个命中区域。键（z/id/x/y/
w/h/type）与旧版 dump 格式一致，保证现有脚本有效。

- int z;

- int id;

- int x;

- int y;

- int w;

- int h;

- int type;


## HitRegionsDoc (class)

- int count;

- List<HitRegionDoc> regions;


## Ime (class)

输入法（IME）集成。文本控件报告
光标位置，使组合/候选窗口跟随光标而非
固定在窗口原点。

- [DllImport("zan_gui")]static extern void zan_gui_set_ime_pos(int x, int y);

- static void SetCaret(int x, int y)
  - 将 IME 组合 + 候选窗口定位到给定的
    客户区像素坐标（通常是光标底部）。


## Keys (class)

虚拟键码常量（Windows 上的 VK_*）。

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

来自 EventMods() 的修饰键标志。

- static int Ctrl()

- static int Shift()

- static int Alt()

- static bool HasCtrl(int mods)

- static bool HasShift(int mods)

- static bool HasAlt(int mods)


## SysFile (class)

用于导出的最小文件写入（UTF-8，Windows 上加 BOM 以便
记事本和 Excel 按 UTF-8 读取导出）。

- static bool Write(string path, string content)
  - 将内容写入 path，覆盖已有文件。成功返回 true。


## ThemeDoc (class)

当前皮肤解析后的样式 token，由 `dump theme` 写出。

仅靠几何（hitregions）不足以评判 UI："文本没有
垂直居中"、"这张卡片多了个外框"和"代码框
与其他面板不一致"，这些都是关于像素背后
样式值的判断。导出 token 后，自动化检查可将
控件的实际矩形与皮肤真正要求的 padding/高度/字体
对比，而无需从坐标猜测。

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

Gui 框架的 AI 可操作 UI 自动化驱动。

驱动脚本不移动系统鼠标、也不做屏幕像素抓取，而是
注入合成输入事件（见 Window.InjectEvent / 原生
zan_gui_inject_event），这些事件由真实 OS 输入流经的
同一个轮询/等待事件循环取出。所有命中测试、焦点变化、点击目标和按键
处理程序因此与真人操作时完全相同——应用
通过真实事件分发"从内部"被驱动，而非从外部模拟。

可选启用：将环境变量 ZAN_UI_SCRIPT 设为脚本文本路径。
未设置时驱动完全静止，正常 GUI 行为
不受影响。输出（命中区域/编辑器 dump、断言结果日志）
写入 ZAN_UI_OUT（默认 "_scratch/uidrv"）。

脚本语法（每行一条命令；空行和 '#' 注释忽略）：
wait <ms>                     在下一条命令前暂停 <ms> 毫秒
move <x> <y> — 合成鼠标移动
click <x> <y> — 在 x,y 处移动+按下+释放（左键）
clickid <hitId> — 点击指定 id 命中区域的中心
rclick <x> <y> — 在 x,y 处右键点击
scroll <x> <y> <delta> — 在 x,y 处滚轮滚动
char  — 一个文本输入字符（Tab=9/Enter=13/Esc=27）
type "<text>" — 逐个输入字面文本的每个字符
keydown  [mods] — 原始 KeyDown（方向键、F 键、App Tab 焦点）
keyup  [mods] — 原始 KeyUp（例如 IDE 中的 F5）
ev <kind> <x> <y> <btn>  <mods> — 原始事件（逃生通道）
dump hitregions <file> — 将所有注册的命中区域写入为 JSON
dump theme <file> — 将当前皮肤的样式 token 写入为 JSON
dump tree <file> — 将保留模式控件树写入为 JSON
dump pixels <file> [x y w h] — 将客户区或指定矩形的原始像素写入文件
redraw full — 强制下一帧整窗重绘，并等待该帧呈现
freeze clock / unfreeze clock — 冻结或恢复测试专用动画时钟
dump probe <name> <file> — 写入指定 probe 的当前值
assert probe <name> contains <text> — 对照 probe 值记录 PASS/FAIL
assert probe <name> equals <text>
log <text> — 向结果日志追加一条备注
quit — 关闭窗口（结束应用）

宿主每帧通过 SetProbe(name, value) 推送命名 probe；
IDE 发布 "editor"（缓冲区/光标/补全状态）和 "log"。

- static extern string getenv(string name);

- static bool active;

- static bool finished;

- static App app;

- static Control root;
  - 保留模式的根（Form.Run 登记），供 `dump tree` 遍历。

- static List<string> cmds;

- static int pc;

- static int waitUntilUs;

- static bool waitArmed;

- static bool waitFullPresent;

- static string outDir;

- static string resultsPath;

- static int passCount;

- static int failCount;

- static List<string> probeNames;

- static List<string> probeVals;

- static bool Active()
  - ZAN_UI_SCRIPT 驱动的会话运行中时为真（可设置 probe）。

- static void Begin(App a)
  - 读取 ZAN_UI_SCRIPT；已设置时加载脚本并启动驱动。
    绑定到给定应用的窗口以注入事件。
    环境变量未设置时为空操作（驱动保持静止）。可多次调用。

- static void SetRoot(Control c)
  - 宿主钩子：登记保留模式的根，供 `dump tree`（Form.Run 调用）。

- static void NotifyPresented(App owner)
  - 宿主钩子：释放 `redraw full` 在下一次脚本命令前的呈现屏障。

- static string Env(string name)

- static void SetProbe(string name, string val)
  - 宿主钩子：发布（或覆盖）一个命名的内省 probe。

- static string GetProbe(string name)

- static void Tick()
  - 推进脚本。每个事件循环迭代调用一次（从
    App.ProcessEvent 和 IDE 的自定义循环）。注入下一个事件
    或跳过即时（dump/assert/log）命令。在注入的批次之间
    让出，使每批完全消费并渲染后再进行下一批。

- static bool Exec(string line)
  - 执行一行命令。注入事件或设置了
    等待时返回 true（调用方应在下一条命令前让循环先处理）。

- static void ClickAt(int x, int y, int button)

- static void Inject(int kind, int x, int y, int button, int code, int mods)

- static HitRegion FindRegion(int id)
  - 将命中区域 id 解析为区域（后注册者优先，与
    HitTester 的逆序命中优先级一致）。未注册时返回 null。

- static void DoDump(List<string> t)

- static void DoAssert(List<string> t, string trimmed)

- static void Finish()

- static string HitRegionsJson()

- static string TreeJson()
  - 当前窗口的控件树（形状 + 布局结果）。根由 Form.Run
    登记；没有保留模式根时写出一棵空树。

- static ControlNode NodeOf(Control c)

- static string ThemeJson()
  - 当前皮肤解析后的样式 token，外加 DPI 缩放，调用方可
    将 token 转换为命中区域所用的设备像素。

- static string JsonBool(bool b)
  - 布尔值的 JSON 字面量（"true"/"false"）。公开，
    构建 probe JSON 的宿主（如 IDE 的编辑器快照）可复用。

- static void Note(string msg)

- static string OutPath(string file)

- static int ArgI(List<string> t, int idx, int def)

- static List<string> Tok(string s)

- static string RemainderAfter(string s, int count)
  - 前 `count` 个空白分隔 token 之后的所有内容，去除首尾空白。

- static string Unquote(string s)

- static string Trim(string s)

- static bool StartsWith(string s, string pfx)

- static bool Contains(string s, string sub)

- static string JsonEsc(string s)
  - 对字符串做 JSON 转义（引号、反斜杠、控制字符）。公开，
    构建 probe JSON 的宿主（如 IDE 的编辑器快照）可复用。


## Win32Shell (class)

Windows 窗口外壳——类注册、窗口过程、
事件队列、DIB 呈现、亚克力玻璃、IME 定位、剪贴板
和自动化注入队列——用 Zan 直接调用 user32/gdi32 编写，
而非 C 运行时文件。像素仍来自光栅化表面
（Canvas），通过 zan_gui_get_pixels 交出缓冲区。

控件层看到的一切都经由 Gui.Window，因此本类是
该抽象的 Windows 半边，与其他平台后端提供的
扁平导出逐一对应。

- [DllImport("user32", EntryPoint="RegisterClassExW")]static extern ushort RegisterClassExW(nint wc);

- [DllImport("shell32", EntryPoint="DragAcceptFiles")]static extern void DragAcceptFiles(nint hwnd, int accept);

- [DllImport("shell32", EntryPoint="DragQueryFileW")]static extern int DragQueryFileW(nint hdrop, int index, nint buf, int cch);

- [DllImport("shell32", EntryPoint="DragQueryPoint")]static extern int DragQueryPoint(nint hdrop, nint pt);

- [DllImport("shell32", EntryPoint="DragFinish")]static extern void DragFinish(nint hdrop);

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
  - 光栅化器持有像素；外壳仅负责呈现。

- [DllImport("zan_gui")]static extern int zan_gui_present_window(int surfaceId, nint nativeWindow);
  - GPU 直通呈现：光栅化器把帧直接交换到该窗口上，返回 1 表示
    屏幕已经是这一帧、外壳不要再贴位图；返回 0 表示照旧走位图
    （CPU 后端、拿不到 GL 的机器、以及分层玻璃窗都是 0）。

- [DllImport("zan_gui")]static extern void zan_gui_release_window(nint nativeWindow);
  - 窗口即将销毁：让光栅化器释放挂在这个句柄上的东西。

- [DllImport("zan_gui")]static extern int zan_gui_surface_width(int surfaceId);

- [DllImport("zan_gui")]static extern int zan_gui_surface_height(int surfaceId);

- [DllImport("zan_gui")]static extern int zan_gui_surface_painted(int surfaceId);
  - 该表面上是否已经画过一帧（整表面清屏）。缩放时表面被销毁重建，
    而表面 id 会立即被回收复用：WM_PAINT/WM_SIZE 拿着 SurfaceOf(hwnd)
    记下的旧 id 去呈现，正好命中那块尚未绘制的新表面——里面是分配器
    交还的上一块表面内容，按旧 stride 排列，于是整窗都是斜切重影。

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

- static List<int> postQ;

- static List<nint> postHwnd;

- static int windowWidth;

- static int windowHeight;

- static int minTrackW;

- static int minTrackH;

- static int dpi;

- static int titlebarH;

- static int buttonW;

- static List<nint> capHwnd;

- static List<int> capCount;

- static List<nint> fixedHwnd;

- static int imeX;

- static int imeY;

- static bool glassOn;

- static int glassTint;

- static int shapeMode;

- static List<int> shapeRegions;

- static List<byte> shapeMask;

- static int shapeMaskW;

- static int shapeMaskH;

- static int shapeMaskGen;

- static List<nint> surfHwnd;

- static List<int> surfId;

- static int lastSurface;

- static List<int> dirty;

- static bool dirtyOverflow;

- static nint dirtyHwnd;

- static List<nint> dirtyLost;

- static List<int> injectQ;

- static List<nint> injectHwnd;

- static nint injectCs;

- static nint lwDc;

- static nint lwBitmap;

- static nint lwOld;

- static nint lwBits;

- static int lwW;

- static int lwH;

- static List<int> hitGuards;

- static List<nint> hitGuardWins;

- static nint scratchRect;

- static nint scratchMsg;

- static nint bitmapInfo;

- static int bgColor;

- static int S32(nint p, int off)
  - 读取 32 位字段。Win32 RECT / POINT 坐标在
    多显示器布局下会为负，因此该值保持有符号。

- static void Init()
  - 一次性类注册、DPI 感知和共享临时缓冲区。

- static void EnableDpiAwareness()
  - 有 Shcore 时按显示器感知，否则按系统感知，
    保证二进制在 Windows 7 上仍可加载。

- static int ModsNow()

- static int LoWord(nint v)
  - LPARAM 坐标对的低/高有符号 16 位半部。

- static int HiWord(nint v)

- static void Post(nint hwnd, int kind, int x, int y, int button, int keycode, int mods)
  - 发布一个输入事件供循环消费。`hwnd` 与事件
    一起存储：产生此事件的窗口，这正是
    多窗口路由所需的。仅 `eventHwnd` 记录的是窗口过程
    最后一次运行所属的窗口，而发布之后（或嵌套其中）
    分发的消息——如兄弟窗口的 WM_NCHITTEST/WM_SETCURSOR/WM_PAINT，
    都不会发布——会把事件重定向到错误的窗口，
    因此子窗口一旦存在，父级点击就被当作"不是我的"丢弃。

- static void OnDropFiles(nint hwnd, nint hdrop)
  - 收集文件拖放的路径，并在
    拖放点（客户端像素）发布为 kind-9 事件，
    应用可将其路由到光标下的任意控件。路径在 `dropped` 中等待被取走。

- static List<string> TakeDropped()
  - 交出（并清除）上次拖放的路径。

- static SizePaintBody sizePaint;
  - 系统模态尺寸调整/移动循环里就地重绘一帧的回调，
    由 Window.SetSizePaint 注册；未注册时为 null。

- static bool inSizeMove;
  - 是否正处在系统模态尺寸调整/移动循环中
    （WM_ENTERSIZEMOVE .. WM_EXITSIZEMOVE）。

- static void SetSizePaint(SizePaintBody body)

- static nint OnMessage(nint hwnd, int msg, nint wp, nint lp)

- static nint HitTest(nint hwnd, int screenX, int screenY)
  - 无边框外壳：边缘为边框尺寸手柄，标题条
    除应用绘制标题按钮处外均可拖拽。

- static void ClearHitGuards(nint hwnd)
  - 清除上一帧的客户端优先条带。在渲染重新注册
    仍在屏幕上的条带之前，每帧调用一次。

- static void AddHitGuard(nint hwnd, int x, int y, int w, int h)
  - 以客户端坐标在 (`x`, `y`) 处为客户端区域抢占 `w` x `h`，
    使边框边缘手柄不遮挡绘制在该处的控件。

- static bool InHitGuard(nint hwnd, int x, int y)
  - 只有 `hwnd` 自己的条带有效：每个窗口以自身的
    客户端坐标注册矩形，否则子窗口的条带会以相同的偏移
    遮蔽父窗口的边框。

- static bool IsMaximized(nint hwnd)

- static void ImeReposition(nint hwnd)

- static void SetImePos(int x, int y)

- static int SurfaceOf(nint hwnd)

- static void RememberSurface(nint hwnd, int surface)

- static void FillBitmapInfo(int w, int h)
  - 描述所呈现表面的自上而下 32bpp BI_RGB 头。

- static void Blit(nint hwnd, int surface)

- static void PresentLayered(nint hwnd, int surface)
  - 玻璃呈现：将直通 alpha 表面预乘进缓存的 DIB
    节并交给 DWM，由它在 alpha 低于 255 处
    合成模糊背景。

- static void AcrylicApply(nint hwnd, int state, int tintArgb)

- static nint CreateWindow(string title, int width, int height)

- static bool WorkArea(nint hwnd, int flags)
  - 窗口所在显示器的（可用）工作区矩形，写入 scratchRect。

- static bool CenterWindow(nint hwnd)

- static void CenterOnOwner(nint hwnd)
  - 次要窗口（对话框）在主窗口上方居中打开，
    并保持在显示器工作区内。

- static void ShowWindowNow(nint hwnd)

- static void SetWindowPosition(nint hwnd, int x, int y)

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
  - 为 `hwnd` 注册的标题按钮数量（在设置前默认为 5）。

- static bool ResizableOf(nint hwnd)
  - 窗口是否允许用户拖边框 / 最大化改变尺寸（默认允许）。

- static void SetResizable(nint hwnd, bool on)

- static void SetCaptionButtons(nint hwnd, int count)

- static void ForgetCaptionButtons(nint hwnd)
  - 遗忘已关闭窗口的标题按钮条带。

- static int TitlebarHeight()

- static int CaptionButtonWidth()

- static int ClientWidth(nint hwnd)

- static int ClientHeight(nint hwnd)

- static int WindowWidth()

- static int WindowHeight()

- static void SetCursorShape(int kind)

- static void EnableGlass(nint hwnd, int tintArgb)

- static void DisableGlass(nint hwnd)

- static void SetOpacity(nint hwnd, int percent)

- static void SetShape(nint hwnd, string spec)
  - 窗口轮廓为形状区域的并集。`spec` 格式：
    "t,x,y,w,h,r;t,x,y,w,h,r;..."（逻辑像素；type 1 = 圆角矩形，
    r 为圆角半径；type 2 = 椭圆，w/h 为包围盒；r 忽略）。
    "" 恢复为普通不透明矩形。两条路径都兼容 Windows 7：
    DWM 合成开启时窗口为分层并逐像素合成
    （抗锯齿边缘，外部像素点击穿透）；DWM 关闭时
    回退为合并的 SetWindowRgn（硬边，同样点击穿透）。
    不涉及 SetWindowCompositionAttribute（仅 Win8+）。

- static void FitShapeWindow(nint hwnd, List<int> regs)
  - 缩小（或放大）窗口，使客户区与形状区域的
    包围盒精确匹配（缩放到物理像素）。没有这一步，
    小的异形设计会被填充到主框架 800x600 的最小值，
    轮廓尺寸也会出错。保持窗口居中。
    区域预期为窗口相对坐标（最小 x/y >= 0）；
    超出窗口上方/左方的形状保持不变。

- static bool DwmComposited()
  - DWM 桌面合成启用时为真（Vista+；Win7 通常返回 true，
    仅当用户关闭时才返回 false）。

- static void SetShapeRgn(nint hwnd, List<int> regs)
  - 经典区域回退（Win7 无 DWM）：圆角矩形与
    椭圆区域的并集。SetWindowRgn 之后窗口拥有合并区域。

- static long ShapeSdfFp(List<int> regs, long sx, long sy, int scale)
  - 从亚像素采样点 (sx, sy)（同样为 1/16 像素单位）到
    异形轮廓最近边缘的有符号距离（1/16 像素定点，内部为负）。
    圆角矩形使用标准 SDF；椭圆使用
    精确 SDF。所有运算为 64 位，大窗口不会
    使距离平方溢出。

- static long ISqrt64(long v)
  - 通过牛顿迭代求 64 位整数平方根（向下取整）——
    无论量级如何仅需几步即收敛，而非线性扫描。

- static List<byte> ShapeMask(int sw, int sh)
  - 为当前形状列表以物理尺寸 sw x sh 重建逐像素覆盖掩码
    （0..255），除非存在匹配的缓存掩码。
    每个像素 4x4 超采样：十六个亚像素样本（1/16 像素
    分辨率，即样本中心位于像素的 1/8、3/8、5/8、7/8 处）
    经 SDF 测试后取平均。单一中心样本会把
    圆角变成 1px 阶梯；超采样均值呈现为
    平滑抗锯齿边缘，与渲染器自身的
    双精度圆角弧对齐。

- static void Present(nint hwnd, int surface)

- static void PresentDirty(nint hwnd, int x, int y, int w, int h)

- static void NoteDirtyLost(nint hwnd)
  - 记下一个窗口有一批声明过却没上传成的脏矩形：它下一次呈现
    整窗上传，而不是只上传那一次声明的那几块。

- static bool TakeDirtyLost(nint hwnd)
  - 这个窗口是否欠着一次整窗上传（并消费掉该记录）。

- static void ClearEvent()

- static bool DrainInjected()
  - 若自动化驱动留有排队的合成事件，则取出一个处理。

- static bool DrainPosted()
  - 取出窗口过程排队的一个事件（队列空则 false）。

- static int PollEvent()

- static int WaitEvent()

- static int WaitEventTimeout(int ms)

- static void Wake()

- static void InjectEvent(nint hwnd, int kind, int x, int y, int button, int keycode, int mods)

- static int InjectPending()

- static int EventKindValue()

- static int EventXValue()

- static int EventYValue()

- static int EventButtonValue()

- static int EventKeyCodeValue()

- static int EventModsValue()

- static nint EventWindow()

- static int TickMs()
  - 来自性能计数器的单调毫秒：GetTickCount 仅每约 15 ms
    变化一次，这会使每帧动画步长抖动。

- static int TickUs()
  - 来自同一个性能计数器的单调微秒，用于分相位计时：一帧只有几
    毫秒，按毫秒取整时每个相位都被截成 0 或 1，看不出谁贵。
    只保留秒的低三位（每 1000 秒回绕一次），免得计数乘到微秒时
    把 64 位撑爆——它只用来算同一帧内的差值。

- static void Sleep(int ms)

- static bool OpenClipboardRetry()
  - 剪贴板同一时刻只能被一个进程打开，而剪贴板查看器、
    输入法、浏览器都会在内容变化后瞬间打开它。只试一次就
    放弃，就是“复制粘贴时灵时不灵”的来源：短暂重试几次，
    对方用完就轮到我们。

- static bool SetClipboard(string text)

- static string GetClipboard()

- static int DpiScale()
  - 屏幕 DPI 相对于 96 的百分比（144 dpi 显示器上为 150）。

- static bool WriteTextFile(string path, string content)
  - 用 UTF-8 文本覆盖文件，加 BOM 前缀，
    使电子表格应用识别导出 CJK CSV 的编码。

- static void SetBackground(int color)
  - 用于填充实时调整大小暴露出的边距区域的背景色，
    在应用重绘前生效，由画布清除保持同步。


## Window (class)

跨平台抽象的窗口与事件管理。
委托给处理 Win32/X11/Cocoa 的 zan_gui 运行时。

- static bool frozenTick;

- static int frozenTickMs;

- [DllImport("user32")]static extern nint GetModuleHandleA(nint reserved);

- [DllImport("gdi32")]static extern nint GetStockObject(int fnObject);

- [DllImport("kernel32")]static extern int GetTickCount();

- [DllImport("zan_gui")]static extern nint zan_gui_create_window(string title, int width, int height);

- [DllImport("zan_gui")]static extern int zan_gui_show_window(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_wait_event();

- [DllImport("zan_gui")]static extern int zan_gui_wait_event_timeout(int ms);

- [DllImport("zan_gui")]static extern int zan_gui_poll_event();

- [DllImport("zan_gui")]static extern int zan_gui_wake();

- [DllImport("zan_gui")]static extern int zan_gui_inject_event(nint hwnd, int kind, int x, int y, int button, int keycode, int mods);

- [DllImport("zan_gui")]static extern int zan_gui_inject_pending();

- [DllImport("zan_gui")]static extern int zan_gui_event_kind();

- [DllImport("zan_gui")]static extern string zan_gui_drop_take();

- [DllImport("zan_gui")]static extern int zan_gui_event_x();

- [DllImport("zan_gui")]static extern int zan_gui_event_y();

- [DllImport("zan_gui")]static extern int zan_gui_event_button();

- [DllImport("zan_gui")]static extern int zan_gui_event_keycode();

- [DllImport("zan_gui")]static extern int zan_gui_event_mods();

- [DllImport("zan_gui")]static extern int zan_gui_window_width();

- [DllImport("zan_gui")]static extern int zan_gui_window_height();

- [DllImport("zan_gui")]static extern nint zan_gui_event_hwnd();

- [DllImport("zan_gui")]static extern int zan_gui_client_width(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_client_height(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_present(nint hwnd, int surfaceId);

- [DllImport("zan_gui")]static extern int zan_gui_present_dirty_add(int x, int y, int w, int h);

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
  - 在窗口后启用系统原生半透明玻璃（Win11 亚克力、
    macOS NSVisualEffectView、Linux 合成器模糊），
    凡表面透明处生效。tint 为打包的 ARGB 颜色；alpha 控制磨砂强度。
    在没有原生背景的平台上安全地空操作。

- [DllImport("zan_gui")]static extern int zan_gui_disable_glass(nint hwnd);
  - 将窗口恢复不透明（移除原生半透明合成）。

- [DllImport("zan_gui")]static extern int zan_gui_set_opacity(nint hwnd, int percent);
  - 以百分比（10..100）设置整个窗口的不透明度；100 恢复不透明。

- [DllImport("zan_gui")]static extern int zan_gui_set_window_pos(nint hwnd, int x, int y);
  - 将窗口左上角移动到屏幕工作区的 (x, y) 像素位置。

- [DllImport("zan_gui")]static extern int zan_gui_center_window(nint hwnd);
  - 在显示器工作区上重新居中窗口。

- [DllImport("zan_gui")]static extern int zan_gui_clear_hit_guards(nint hwnd);
  - 清除上一帧注册的所有客户端优先条带。

- [DllImport("zan_gui")]static extern int zan_gui_add_hit_guard(nint hwnd, int x, int y, int w, int h);
  - 为客户端像素矩形抢占边框的边缘缩放区域。

- nint handle;

- int width;

- int height;

- Window(string title, int width, int height)
  - 创建新的平台窗口。

- static void SetSizePaint(SizePaintBody body)
  - 注册一个在系统模态尺寸调整循环里被调用的帧体。
    拖动窗口边框时消息循环停在 OS 内部，应用自己的循环拿不到
    控制权，表面会一直停在旧尺寸上，新露出的一条是空白，直到
    松手为止；窗口过程收到 WM_SIZE 时回调它，就能就地画出
    新尺寸的一帧。

- static SizePaintBody sizePaintUnused;

- static void SetResizeBackground(int color)
  - 实时缩放时用来填充新露出边距的颜色。窗口先被
    OS 放大、应用才画出新尺寸的一帧，这段空隙里那条边距按此色
    填充；默认 0（黑）会在拖动边框时闪黑边，因此每帧清屏色
    变化时都要同步过来。

- void Show()
  - 在屏幕上显示窗口。

- void SetPosition(int x, int y)
  - 将窗口左上角移动到屏幕工作区的 (x, y) 像素位置。

- void Center()
  - 在显示器工作区上居中窗口。

- int WaitEvent()
  - 阻塞直到下一个 OS 事件到达。成功返回 0，退出返回 -1。

- int WaitEventTimeout(int ms)
  - 阻塞直到 OS 事件到达或 `ms` 毫秒过去。
    返回 0=收到事件、1=超时、-1=退出。在内核中空闲，
    等待下一帧截止时间的动画循环不消耗 CPU。

- int PollEvent()
  - 不阻塞地轮询事件。返回 0=收到事件、1=无事件、-1=退出。

- void Wake()
  - 从任意线程唤醒阻塞在 WaitEvent 中的 UI 线程，使其能排空
    App 分发队列。线程安全。由 App.Post 使用。

- void InjectEvent(int kind, int x, int y, int button, int keycode, int mods)
  - 为此窗口排队一个合成输入事件，由
    下一次 PollEvent/WaitEvent 与真实 OS 事件一样取出。
    UI 自动化驱动（Gui.UiDriver）用它驱动应用，
    无需移动系统鼠标或合成系统按键。`kind`/`button`/`keycode`/`mods`
    与 EventKind()/EventButton()/EventKeyCode()/
    EventMods() 使用相同的编码。

- static int InjectPending()
  - 仍排队的合成事件数量（事件循环
    完全消费驱动器最后一批后为 0）。

- List<string> TakeDroppedFiles()
  - 上次文件拖放事件（kind 9）携带的文件路径，
    取出即从队列移除：应用看到该事件时调用一次。
    拖放位置为 EventX()/EventY()（客户端像素），应用可将
    文件路由到光标下的控件。

- int EventKind()
  - 返回最近一次事件类型（0=无、1=鼠标移动、2=鼠标按下、3=鼠标释放、4=按键按下、5=按键释放、6=文本输入、7=调整大小、8=关闭、9=文件拖放、13=滚动）。

- int EventX()

- int EventY()

- int EventButton()

- int EventKeyCode()

- int EventMods()

- int GetWidth()

- int GetHeight()

- nint GetHandle()
  - 此窗口的原生句柄（不透明；用于事件路由）。

- static nint EventHwnd()
  - 产生最近一次事件的窗口句柄，
    拥有多个顶层窗口的应用可据此正确路由事件。

- int ClientWidth()
  - 本窗口客户区宽/高（设备像素），按窗口查询
    （不同于反映最近调整大小的窗口的 GetWidth）。

- int ClientHeight()

- void Present(Canvas canvas)
  - 将 Canvas（表面）呈现到窗口。

- void PresentDirty(int x, int y, int w, int h)
  - 将下一次 Present 限制在声明的像素上：
    只重绘了几个小矩形的帧只需几次小
    上传，而非整个表面。声明每个变化的矩形
    ——遗漏的部分保留先前呈现的像素。
    不声明（默认）则呈现全部。

- void SetTitle(string title)
  - 设置窗口标题。

- static void SetCursor(int cursorType)
  - 设置鼠标光标类型。

- static int RawTickMs()

- static int GetTickMs()
  - 返回当前时间（毫秒）。

- static void FreezeTick(int fixedMs)
  - 测试专用：冻结所有经过 Window.GetTickMs() 的时间读取。

- static void UnfreezeTick()
  - 测试专用：恢复真实单调时钟。

- static int GetTickUs()
  - 返回当前时间（微秒），用于一帧之内的分相位计时。
    每 1000 秒回绕一次，只可用来算差值。

- static void SleepMs(int ms)
  - 让调用线程休眠指定的毫秒数（帧节奏控制）。

- void Minimize()
  - 最小化窗口。

- void ToggleMaximize()
  - 在最大化与还原之间切换。

- void Close()
  - 请求关闭窗口。

- void Destroy()
  - 彻底拆除窗口（销毁 OS 窗口）。
    在响应关闭事件后调用，避免对话框作为
    孤儿滞留；在主窗口上为空操作。

- bool IsMaximized()
  - 窗口当前处于最大化状态时为真。

- bool IsVisible()
  - 窗口任意部分可见时（未最小化
    且未被其他窗口完全遮挡）为真。环境动画
    在该值为假时暂停。

- bool IsFocused()
  - 此窗口持有输入焦点时为真。
    此时环境动画会减速至心跳节奏。

- void EnableGlass(int tint)
  - 在此窗口后启用系统原生半透明玻璃，
    由打包的 ARGB `tint` 着色。渲染器必须将表面清为
    透明，效果才能透出。

- void DisableGlass()
  - 恢复为不透明窗口。

- void SetOpacity(int percent)
  - 整个窗口的不透明度（百分比），限制在 10..100。100 = 完全不透明。

- void SetShape(string spec)
  - 窗口轮廓为形状区域的并集，`spec` 格式：
    "t,x,y,w,h,r;t,x,y,w,h,r;..."（逻辑像素；1 = 圆角矩形、2 =
    椭圆）。"" 恢复为普通不透明矩形。DWM 开启时以逐像素
    alpha（分层窗口）合成，DWM 关闭时以合并区域
    合成，因此异形窗口在 Windows 7 上也能工作。轮廓
    具有抗锯齿边缘（DWM），外部区域对
    绘制和鼠标输入都透明。不应用亚克力模糊。仅限 Windows；
    其他平台为安全空操作。

- void SetTopmost(bool on)
  - 启用或禁用窗口置顶（topmost）。

- void SetResizable(bool on)
  - 窗口是否可由用户改变大小。关闭后边框不再
    充当尺寸手柄，双击标题栏与最大化也不再放大窗口。
    仅限 Windows；其他平台为安全空操作。

- void SetCaptionButtons(int count)
  - 告知运行时框架绘制多少个标题按钮，
    使可拖拽标题栏区域排除它们。

- void ClearHitGuards()
  - 清除上一帧声明的客户端优先条带（见
    `AddHitGuard`）。由应用每帧调用一次。

- void AddHitGuard(int x, int y, int w, int h)
  - 为客户端区域抢占一个矩形（客户端像素），
    使边框边缘缩放手柄不遮挡紧贴窗口边缘绘制的控件。
    角手柄保持优先。

- static int TitlebarHeight()
  - 为自定义标题栏保留的高度（设备像素）。

- static int CaptionButtonWidth()
  - 单个标题按钮的宽度（设备像素）。


## int (delegate)

SetWindowCompositionAttribute（未文档化的 user32 入口，懒解析）。

`delegate int SetWinCompAttrFn(nint hwnd, nint data);`


## int (delegate)

DwmIsCompositionEnabled（dwmapi，Vista+）：Win7 关闭 DWM 时为 false。

`delegate int DwmIsCompFn(nint outEnabled);`


## int (delegate)

SetProcessDpiAwareness (Shcore) / SetProcessDPIAware (user32) 回退方案。

`delegate int SetProcessDpiAwarenessFn(int level);`


## int (delegate)

`delegate int SetProcessDpiAwareFn();`


## nint (delegate)

Win32 窗口过程，用 Zan 实现并交给 RegisterClassExW。

`delegate nint ZanWndProc(nint hwnd, int msg, nint wp, nint lp);`


## void (delegate)

在系统模态尺寸调整循环里就地绘制的一帧（见
Window.SetSizePaint）。

`delegate void SizePaintBody();`
