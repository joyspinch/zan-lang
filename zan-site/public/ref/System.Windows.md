# System.Windows

> 源码: `stdlib/System/Windows/Screen.zan`, `stdlib/System/Windows/TrayIcon.zan`


## Screen (class)

屏幕捕获、像素探测与显示器信息。Windows 通过
GDI（GetDC/BitBlt/GetDIBits）实现。Linux/macOS 调用平台自带的截图
工具（X11 用 ImageMagick import，Wayland 用 grim，macOS 用
screencapture）写出 BMP 后再读回像素；工具不存在时抛出
PlatformNotSupportedException 而不是返回黑图。

像素以 BGRA 字节数组返回（stride = 宽度 * 4，行序
自上而下）。颜色为 0xAARRGGBB。

byte[] px = Screen.CapturePixels(0, 0, 100, 80);
Screen.CaptureAllToBmp("screen.bmp");
int c = Screen.GetPixel(10, 10);

- [DllImport("user32", EntryPoint="GetDC")]static extern nint WinGetDC(nint hwnd);

- [DllImport("user32", EntryPoint="ReleaseDC")]static extern int WinReleaseDC(nint hwnd, nint dc);

- [DllImport("user32", EntryPoint="GetSystemMetrics")]static extern int WinGetSystemMetrics(int index);

- [DllImport("gdi32", EntryPoint="CreateCompatibleDC")]static extern nint WinCreateCompatibleDC(nint dc);

- [DllImport("gdi32", EntryPoint="CreateCompatibleBitmap")]static extern nint WinCreateCompatibleBitmap(nint dc, int w, int h);

- [DllImport("gdi32", EntryPoint="SelectObject")]static extern nint WinSelectObject(nint dc, nint obj);

- [DllImport("gdi32", EntryPoint="DeleteObject")]static extern int WinDeleteObject(nint obj);

- [DllImport("gdi32", EntryPoint="DeleteDC")]static extern int WinDeleteDC(nint dc);

- [DllImport("gdi32", EntryPoint="BitBlt")]static extern int WinBitBlt(nint dst, int x, int y, int w, int h, nint src, int sx, int sy, int rop);

- [DllImport("gdi32", EntryPoint="GetDIBits")]static extern int WinGetDIBits(nint dc, nint bmp, int start, int lines, nint bits, nint bmi, int usage);

- [DllImport("gdi32", EntryPoint="GetDeviceCaps")]static extern int WinGetDeviceCaps(nint dc, int index);

- [DllImport("crt", EntryPoint="fopen")]static extern nint PlatFopen(string path, string mode);

- [DllImport("crt", EntryPoint="fwrite")]static extern long PlatFwrite(byte[]buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fclose")]static extern int PlatFclose(nint fp);

- static int ScreenWidth()
  - 主屏幕宽度（像素）。

- static int ScreenHeight()
  - 主屏幕高度（像素）。

- static int Dpi()
  - 主屏幕 DPI（96 = 100%）。

- static int GetPixel(int x, int y)
  - 屏幕坐标 (x, y) 处像素的颜色，格式为
    0xAARRGGBB；捕获失败返回 -1。通过 1x1 BitBlt 实现
    （GDI GetPixel 在 DWM 合成桌面上不可靠）。

- static byte[]CapturePixels(int x, int y, int w, int h)
  - 将屏幕矩形捕获为 BGRA 像素（自上而下，
    stride = 宽度 * 4）。捕获失败返回 null。

- static bool CaptureAllToBmp(string path)
  - 捕获主屏幕并写入 32 位 BMP 文件。
    成功返回 true。

- static bool CaptureToBmp(string path, int x, int y, int w, int h)
  - 捕获屏幕矩形并写入 32 位 BMP 文件。
    成功返回 true。

- static string TempBmp()

- static bool Have(string tool)

- static bool ShotToBmp(string path, int x, int y, int w, int h)
  - 用平台截图工具把屏幕矩形写成 BMP。w/h 为 0 表示整屏。
    一个可用工具都没有时抛 PlatformNotSupportedException——静默返回
    false 会让调用方以为是运行时失败而不停重试。

- static bool PpmToBmp(string ppm, string bmpPath)
  - P6 PPM -> 24 位 BMP（grim 不会写 BMP）。

- static byte[]ReadBmpPixels(string path, int w, int h)
  - 读回 BMP 的像素为自上而下的 BGRA。24/32 位无压缩
    （BI_RGB）的 BMP 才支持——截图工具写出的正是这种。
    w/h 传 0 表示按文件里的尺寸；尺寸不符时返回 null。

- static int GetInt(byte[]b, int off)

- static int FirstInt(string line)

- static byte[]ReadBitmap(nint dc, nint bmp, int w, int h)
  - 用 GetDIBits 将选中的位图读入自上而下的 BGRA 字节数组。

- static bool WriteBmp(string path, byte[]px, int w, int h)
  - 将 32 位 BGRA 像素缓冲写入自下而上 (bottom-up) 的 BMP 文件。

- static void PutInt(byte[]b, int off, int v)


## TrayIcon (class)

系统托盘（通知区域）图标，支持气泡通知。
完全自包含：自行创建隐藏消息窗口和消息
循环并运行在专用线程上，因此控制台程序和 GUI
程序都可使用，且不依赖 Gui 框架。

TrayIconCallback cb = Demo.OnTray;      // 静态方法
TrayIcon.Add("myicon.ico", "MyApp", cb);
TrayIcon.ShowBalloon("Done", "Task finished", 1, 3000);
TrayIcon.SetMenu(items, Demo.OnTrayMenu);   // 右键菜单
TrayIcon.Remove();

每个进程仅一个图标（常见情形）。

Windows 走 Shell_NotifyIconW；Linux 走 freedesktop 系统托盘
（XEmbed）协议，图标支持 .ico（未压缩 DIB 或 PNG 子图）与
PNG/BMP/JPEG，气泡通知走 libnotify 的 notify-send，右键菜单
由托盘后端自绘。没有运行的托盘（例如无桌面环境、
无 DISPLAY）时 Add() 返回 false。macOS 需要在主线程 run loop 上
驱动 NSStatusItem，与本 API 的专用线程模型不兼容，仍抛出
PlatformNotSupportedException。

- [DllImport("user32", EntryPoint="RegisterClassExW")]static extern ushort WinRegisterClassExW(nint wc);

- [DllImport("user32", EntryPoint="CreateWindowExW")]static extern nint WinCreateWindowExW(int exStyle, nint cls, nint title, int style, int x, int y, int w, int h, nint parent, nint menu, nint inst, nint param);

- [DllImport("user32", EntryPoint="DefWindowProcW")]static extern nint WinDefWindowProcW(nint hwnd, int msg, nint wp, nint lp);

- [DllImport("user32", EntryPoint="GetMessageW")]static extern int WinGetMessageW(nint msg, nint hwnd, int min, int max);

- [DllImport("user32", EntryPoint="TranslateMessage")]static extern int WinTranslateMessage(nint msg);

- [DllImport("user32", EntryPoint="DispatchMessageW")]static extern nint WinDispatchMessageW(nint msg);

- [DllImport("user32", EntryPoint="DestroyWindow")]static extern int WinDestroyWindow(nint hwnd);

- [DllImport("user32", EntryPoint="PostThreadMessageW")]static extern int WinPostThreadMessageW(int threadId, int msg, nint wp, nint lp);

- [DllImport("user32", EntryPoint="LoadImageW")]static extern nint WinLoadImageW(nint inst, nint name, int type, int w, int h, int flags);

- [DllImport("user32", EntryPoint="LoadIconW")]static extern nint WinLoadIconW(nint inst, nint name);

- [DllImport("user32", EntryPoint="CreatePopupMenu")]static extern nint WinCreatePopupMenu();

- [DllImport("user32", EntryPoint="AppendMenuW")]static extern int WinAppendMenuW(nint menu, int flags, nint id, nint item);

- [DllImport("user32", EntryPoint="DestroyMenu")]static extern int WinDestroyMenu(nint menu);

- [DllImport("user32", EntryPoint="TrackPopupMenuEx")]static extern int WinTrackPopupMenuEx(nint menu, int flags, int x, int y, nint hwnd, nint parms);

- [DllImport("user32", EntryPoint="SetForegroundWindow")]static extern int WinSetForegroundWindow(nint hwnd);

- [DllImport("user32", EntryPoint="GetCursorPos")]static extern int WinGetCursorPos(nint pt);

- [DllImport("user32", EntryPoint="PostMessageW")]static extern int WinPostMessageW(nint hwnd, int msg, nint wp, nint lp);

- [DllImport("user32", EntryPoint="DestroyIcon")]static extern int WinDestroyIcon(nint icon);

- [DllImport("user32", EntryPoint="RegisterWindowMessageW")]static extern int WinRegisterWindowMessageW(nint name);

- [DllImport("kernel32", EntryPoint="GetCurrentThreadId")]static extern int WinGetCurrentThreadId();

- [DllImport("shell32", EntryPoint="Shell_NotifyIconW")]static extern int WinShellNotifyIcon(int msg, nint data);

- static TrayIconCallback userCb;

- static nint trayHwnd;

- static int trayThreadId;

- static nint trayIcon;

- static int threadFailed;

- static int taskbarCreatedMsg;

- static string trayIconPath;

- static TrayWndProcFn wndProc;

- static List<TrayMenuItem> menuItems;
  - 右键菜单模型。菜单在每次弹出时从这份列表重建，
    因此调用方可以随时改完后重新 SetMenu（例如切换勾选）。

- static TrayMenuCallback menuCb;

- [DllImport("zan_gui", EntryPoint="zan_tray_start")]static extern int NativeStart(string iconPath, string tooltip);

- [DllImport("zan_gui", EntryPoint="zan_tray_stop")]static extern int NativeStop();

- [DllImport("zan_gui", EntryPoint="zan_tray_set_tooltip")]static extern int NativeSetTooltip(string tooltip);

- [DllImport("zan_gui", EntryPoint="zan_tray_set_menu")]static extern int NativeSetMenu(string packed);

- [DllImport("zan_gui", EntryPoint="zan_tray_next_event")]static extern int NativeNextEvent(int timeoutMs);

- static TrayIconCallback userCb;

- static TrayMenuCallback menuCb;

- static List<TrayMenuItem> menuItems;

- static int posixLive;

- static int posixPumpStop;

- static bool Add(string iconPath, string tooltip, TrayIconCallback cb)
  - 添加托盘图标。`iconPath` 是 .ico 文件（"" 使用默认
    应用图标）。`tooltip` 是悬停文本。图标
    成功显示后返回 true。若已添加过图标（需先移除）
    或原生调用失败，则返回 false。

- static void Remove()
  - 移除托盘图标并停止消息循环线程。
    未添加任何图标时调用也是安全的。

- static bool SetTooltip(string tooltip)
  - 修改当前图标的悬停提示文本。

- static bool ShowBalloon(string title, string text, int iconKind, int timeoutMs)
  - 在图标上方显示气泡通知。`iconKind`：0 = 无图标，
    1 = 信息，2 = 警告，3 = 错误。`timeoutMs` 为显示时长
    （新版 Windows 会忽略超过 30 秒的值）。

- static void SetMenu(List<TrayMenuItem> items, TrayMenuCallback cb)
  - 设置右键托盘菜单。右键（或菜单键）点击图标时在
    托盘线程上弹出菜单，选中后以选项 `id` 调用 `cb`（回调
    在托盘线程上执行，要动 UI 请投递到自己的窗口）。弹出时
    每次重建菜单，因此只需改完列表再调一次就能切换勾选/置灰。
    传 null 或空列表等于 ClearMenu()。

- static void ClearMenu()
  - 移除右键菜单，右键回到只通知 TrayIconCallback。

- static string PackMenu(List<TrayMenuItem> items)
  - 把菜单模型打包成后端的行格式："id\tflags\ttext\n"，
    flags 位：1 = 置灰，2 = 勾选，4 = 分隔线。

- static string ShellQuote(string s)

- static void PosixPumpEntry()
  - 托盘事件泵：把原生队列里的动作变成 Zan 回调。回调在这个
    线程上执行（与 Windows 一致）。

- static void ShowMenu()
  - 在鼠标处弹出右键菜单并同步等待选择。必须在托盘
    线程（拥有隐藏窗口的那个）上调用。先 SetForegroundWindow
    再 TrackPopupMenuEx，否则菜单不能失焦关闭（经典 Win32 坑）。

- static void TrayThreadEntry()
  - 在托盘线程上运行：注册窗口类，创建
    隐藏窗口，添加图标，然后泵送消息直到 WM_QUIT。

- static int RegisterTaskbarCreated()

- static void LoadIcon()
  - 只加载一次图标：给定 `trayIconPath`（.ico 文件）时从文件加载，
    否则加载默认应用图标。句柄缓存在
    trayIcon 中，并在线程清理时销毁。

- static void SetVersion()

- static bool Notify(int msg)
  - 以当前图标状态发送 Shell_NotifyIconW 消息。

- static nint BuildData(string tooltip, string title, string text, int iconKind, int timeoutMs)
  - 分配一个清零的 NOTIFYICONDATAW（984 字节，x64 布局），
    并填好固定字段：cbSize、hWnd、uID、uFlags（NIF_MESSAGE|NIF_ICON
    |NIF_TIP）、uCallbackMessage、hIcon 以及气泡字段。

- static int BalloonFlags(int kind)

- static void PutWide(nint dst, string s, int units)
  - 将 Zan 字符串复制到固定大小的 WCHAR 字段（units 含结尾 NUL）。

- static void ZeroMem(nint p, int bytes)

- static nint TrayWndProcImpl(nint hwnd, int msg, nint wp, nint lp)
  - 隐藏宿主窗口的窗口过程 (window proc)。只有托盘回调
    消息和 TaskbarCreated 消息值得关注。


## TrayMenuItem (class)

托盘右键菜单的一项。`id` 是选中时传给回调的编号，
必须大于 0（0 是“未选择”）；分隔线用 `TrayMenuItem.Separator()`。

List<TrayMenuItem> m = new List<TrayMenuItem>();
m.Add(new TrayMenuItem(1, "显示主窗口"));
m.Add(TrayMenuItem.Separator());
m.Add(new TrayMenuItem(2, "退出"));
TrayIcon.SetMenu(m, Demo.OnTrayMenu);

- int id;

- string text;

- bool disabled;
  - 置灰（不可选），仍然显示。

- bool checkMark;
  - 左侧勾选标记，用于“开机启动”这类开关项。

- bool separator;
  - 分隔线无文本也不可选中。

- TrayMenuItem(int itemId, string label)

- static TrayMenuItem Separator()
  - 一条分隔线。

- TrayMenuItem Disabled()
  - 置灰此项（链式）。

- TrayMenuItem Check(bool on)
  - 勾选此项（链式）。


## nint (delegate)

隐藏宿主窗口的窗口过程 (window proc)（WINAPI 调用约定）。

`delegate nint TrayWndProcFn(nint hwnd, int msg, nint wp, nint lp);`


## void (delegate)

托盘图标回调动作码：1 = 左键单击，2 = 左键双击，
3 = 右键单击，4 = 中键单击。回调必须是普通静态方法
（委托无法捕获状态）；请把每个图标的状态放在静态字段中，
与 Thread.Start 工作线程和 Input.Hook 回调的做法一致。

`delegate void TrayIconCallback(int action);`


## void (delegate)

托盘菜单选中回调：参数是被选项的 `id`。与
TrayIconCallback 一样必须是普通静态方法。

`delegate void TrayMenuCallback(int id);`
