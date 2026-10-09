# System.Windows

> 源码: `packages/Zan.Desktop/src/System/Windows/MessageBox.zan`, `packages/Zan.Desktop/src/System/Windows/Screen.zan`, `packages/Zan.Desktop/src/System/Windows/TrayIcon.zan`


## MessageBox (class)

- [DllImport("user32")]static extern int MessageBoxA(nint hwnd, string text, string caption, int flags);

- static int Ok()

- static int Cancel()

- static int Yes()

- static int No()

- static int Show(string text, string caption)

- static int ShowYesNo(string text, string caption)

- static int ShowOkCancel(string text, string caption)

- static bool Posix(string text, string caption, int kind)

- static bool Have(string tool)

- static string OsaQuote(string s)

- static bool RunOsa(string script)


## Screen (class)

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

- static int ScreenHeight()

- static int Dpi()

- static int GetPixel(int x, int y)

- static byte[]CapturePixels(int x, int y, int w, int h)

- static bool CaptureAllToBmp(string path)

- static bool CaptureToBmp(string path, int x, int y, int w, int h)

- static string TempBmp()

- static bool Have(string tool)

- static bool ShotToBmp(string path, int x, int y, int w, int h)

- static bool PpmToBmp(string ppm, string bmpPath)

- static byte[]ReadBmpPixels(string path, int w, int h)

- static int GetInt(byte[]b, int off)

- static int FirstInt(string line)

- static byte[]ReadBitmap(nint dc, nint bmp, int w, int h)

- static bool WriteBmp(string path, byte[]px, int w, int h)

- static void PutInt(byte[]b, int off, int v)


## TrayIcon (class)

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

- static TrayMenuCallback menuCb;

- [DllImport("zan_gui", EntryPoint="zan_tray_start_pixels")]static extern int NativeStartPixels(nint bitmap, string iconPath, string tooltip);

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

- static async Task<bool> AddAsync(string iconPath, string tooltip, TrayIconCallback cb)

- static bool Add(string iconPath, string tooltip, TrayIconCallback cb)

- static void Remove()

- static bool SetTooltip(string tooltip)

- static bool ShowBalloon(string title, string text, int iconKind, int timeoutMs)

- static void SetMenu(List<TrayMenuItem> items, TrayMenuCallback cb)

- static void ClearMenu()

- static string PackMenu(List<TrayMenuItem> items)

- static string ShellQuote(string s)

- static void PosixPumpEntry()

- static void ShowMenu()

- static void TrayThreadEntry()

- static int RegisterTaskbarCreated()

- static void LoadIcon()

- static void SetVersion()

- static bool Notify(int msg)

- static nint BuildData(string tooltip, string title, string text, int iconKind, int timeoutMs)

- static int BalloonFlags(int kind)

- static void PutWide(nint dst, string s, int units)

- static void ZeroMem(nint p, int bytes)

- static nint TrayWndProcImpl(nint hwnd, int msg, nint wp, nint lp)


## TrayMenuItem (class)

- int id;

- string text;

- bool disabled;

- bool checkMark;

- bool separator;

- TrayMenuItem(int itemId, string label)

- static TrayMenuItem Separator()

- TrayMenuItem Disabled()

- TrayMenuItem Check(bool on)


## nint (delegate)

`delegate nint TrayWndProcFn(nint hwnd, int msg, nint wp, nint lp);`


## void (delegate)

`delegate void TrayIconCallback(int action);`


## void (delegate)

`delegate void TrayMenuCallback(int id);`
