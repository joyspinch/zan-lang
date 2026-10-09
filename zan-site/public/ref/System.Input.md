# System.Input

> 源码: `packages/Zan.Desktop/src/System/Input/Background.zan`, `packages/Zan.Desktop/src/System/Input/Hook.zan`, `packages/Zan.Desktop/src/System/Input/Hotkey.zan`, `packages/Zan.Desktop/src/System/Input/InputTool.zan`, `packages/Zan.Desktop/src/System/Input/Keyboard.zan`, `packages/Zan.Desktop/src/System/Input/Mouse.zan`


## Background (class)

- [DllImport("user32", EntryPoint="PostMessageW")]static extern long WinPostMessage(nint hwnd, long msg, long wparam, long lparam);

- [DllImport("user32", EntryPoint="SendMessageW")]static extern long WinSendMessage(nint hwnd, long msg, long wparam, long lparam);

- [DllImport("user32", EntryPoint="FindWindowW")]static extern long WinFindWindow(string cls, string title);

- [DllImport("user32", EntryPoint="FindWindowExW")]static extern long WinFindWindowEx(nint parent, nint after, string cls, string title);

- [DllImport("user32", EntryPoint="MapVirtualKeyW")]static extern int WinMapVirtualKey(int vk, int mapType);

- static long WM_KEYDOWN()

- static long WM_KEYUP()

- static long WM_CHAR()

- static long WM_MOUSEMOVE()

- static long WM_LBUTTONDOWN()

- static long WM_LBUTTONUP()

- static long WM_LBUTTONDBLCLK()

- static long WM_RBUTTONDOWN()

- static long WM_RBUTTONUP()

- static long WM_MBUTTONDOWN()

- static long WM_MBUTTONUP()

- static long WM_MOUSEWHEEL()

- static long WM_XBUTTONDOWN()

- static long WM_XBUTTONUP()

- static int MapVkToVscEx()

- static long MK_LBUTTON()

- static long MK_RBUTTON()

- static long MK_MBUTTON()

- static long MK_XBUTTON1()

- static long MK_XBUTTON2()

- static nint FindWindowByTitle(string title)

- static nint FindWindowByClass(string cls)

- static nint FindChild(nint parent, string cls, string title)

- static bool KeyDown(nint hwnd, int vk)

- static bool KeyUp(nint hwnd, int vk)

- static bool KeyPress(nint hwnd, int vk)

- static bool Repeat(nint hwnd, int vk, int n)

- static bool SendChar(nint hwnd, string ch)

- static bool SendText(nint hwnd, string text)

- static bool SendMacro(nint hwnd, string macro)

- static bool MouseMove(nint hwnd, int x, int y)

- static bool MouseDown(nint hwnd, int button, int x, int y)

- static bool MouseUp(nint hwnd, int button, int x, int y)

- static bool MouseClick(nint hwnd, int button, int x, int y)

- static bool MouseWheel(nint hwnd, int delta, int x, int y)

- static bool SyncKeyPress(nint hwnd, int vk)

- static bool SyncMouseClick(nint hwnd, int button, int x, int y)

- static bool Post(nint hwnd, long msg, long wparam, long lparam)

- static long KeyLParam(int vk, bool up)

- static long CoordLParam(int x, int y)

- static long ButtonMsg(int button, bool down)

- static long ButtonWParam(int button)

- static int CharWidth(string ch)

- static bool SendCodePoint(nint hwnd, int cp)

- static bool IsKeyChar(string ch)

- static int CharVK(string ch)

- static int IndexOf(string hay, string needle, int from)


## Hook (class)

- [DllImport("user32", EntryPoint="SetWindowsHookExW")]static extern nint SetWindowsHookExW(int idHook, HookProc lpfn, nint hmod, int dwThreadId);

- [DllImport("user32", EntryPoint="UnhookWindowsHookEx")]static extern int UnhookWindowsHookEx(nint hhk);

- [DllImport("user32", EntryPoint="CallNextHookEx")]static extern nint CallNextHookEx(nint hhk, int code, nint wp, nint lp);

- [DllImport("user32", EntryPoint="GetMessageW")]static extern int GetMessageW(nint msg, nint hwnd, int min, int max);

- [DllImport("user32", EntryPoint="PostThreadMessageW")]static extern int PostThreadMessageW(int threadId, int msg, nint wp, nint lp);

- [DllImport("kernel32", EntryPoint="GetCurrentThreadId")]static extern int GetCurrentThreadId();

- static HookProc hookProc;

- static nint hookHandle;

- static int hookKind;

- static int hookThreadId;

- static int wantUnhook;

- static int threadFailed;

- static KeyboardHookCallback keyboardCb;

- static MouseHookCallback mouseCb;

- [DllImport("zan_gui", EntryPoint="zan_hook_install")]static extern int NativeHookInstall(int kind);

- [DllImport("zan_gui", EntryPoint="zan_hook_uninstall")]static extern int NativeHookUninstall();

- [DllImport("zan_gui", EntryPoint="zan_hook_next_event")]static extern int NativeHookNextEvent(int timeoutMs, nint p1, nint p2, nint p3, nint p4, nint p5);

- static int posixLive=0;

- static int posixPumpStop=0;

- static KeyboardHookCallback posixKeyboardCb;

- static MouseHookCallback posixMouseCb;

- static int posixKind=0;

- static bool PosixInstall(int kind, KeyboardHookCallback kcb, MouseHookCallback mcb)

- static void PosixPumpEntry()

- static bool InstallKeyboard(KeyboardHookCallback cb)

- static bool InstallMouse(MouseHookCallback cb)

- static void Uninstall()

- static async Task UninstallAsync()

- static async Task<bool> InstallAsync(int kind, KeyboardHookCallback kcb, MouseHookCallback mcb)

- static bool Install(int kind, KeyboardHookCallback kcb, MouseHookCallback mcb)

- static void HookThreadEntry()

- static nint HookProcImpl(int code, nint wp, nint lp)


## Hotkey (class)

- static int ModAlt()

- static int ModCtrl()

- static int ModShift()

- static int ModWin()

- [DllImport("user32", EntryPoint="RegisterHotKey")]static extern int RegisterHotKey(nint hwnd, int id, int mods, int vk);

- [DllImport("user32", EntryPoint="UnregisterHotKey")]static extern int UnregisterHotKey(nint hwnd, int id);

- [DllImport("user32", EntryPoint="GetMessageW")]static extern int GetMessageW(nint msg, nint hwnd, int min, int max);

- [DllImport("user32", EntryPoint="PostThreadMessageW")]static extern int PostThreadMessageW(int threadId, int msg, nint wp, nint lp);

- [DllImport("kernel32", EntryPoint="GetCurrentThreadId")]static extern int GetCurrentThreadId();

- static int hotkeyId;

- static int hotkeyMods;

- static int hotkeyVk;

- static int hotkeyThreadId;

- static int registered;

- static int wantExit;

- static int threadFailed;

- static HotkeyCallback callback;

- [DllImport("zan_gui", EntryPoint="zan_hotkey_register")]static extern int NativeRegister(int id, int mods, int vk);

- [DllImport("zan_gui", EntryPoint="zan_hotkey_unregister")]static extern int NativeUnregister(int id);

- [DllImport("zan_gui", EntryPoint="zan_hotkey_next_event")]static extern int NativeNextEvent(int timeoutMs);

- static int hotkeyId;

- static int hotkeyMods;

- static int hotkeyVk;

- static int registered;

- static int wantExit;

- static int threadFailed;

- static HotkeyCallback callback;

- static bool Register(string combo, HotkeyCallback cb)

- static async Task<bool> RegisterAsync(int vk, int mods, HotkeyCallback cb)

- static bool Register(int vk, int mods, HotkeyCallback cb)

- static void Unregister()

- static async Task UnregisterAsync()

- static void HotkeyThreadEntry()

- static int IndexOf(string hay, string needle, int from)

- static void PosixHotkeyThreadEntry()

- static int IndexOf(string hay, string needle, int from)


## InputTool (class)

- static int None()

- static int Xdotool()

- static int MacOs()

- static bool Have(string tool)

- static int Kind()

- static void Missing(string what)

- static bool Run(string cmd)

- static bool Osa(string script)

- static string Keysym(int vk)

- static int MacKeyCode(int vk)

- static string MacModifier(int vk)

- static int[]LetterCodes()

- static int[]DigitCodes()

- static int[]FnCodes()

- static string Ascii(int code)


## Keyboard (class)

- static int Ctrl()

- static int Shift()

- static int Alt()

- static int Win()

- [DllImport("user32", EntryPoint="SendInput")]static extern int WinSendInput(int count, nint inputs, int cbSize);

- [DllImport("user32", EntryPoint="GetAsyncKeyState")]static extern short WinGetAsyncKeyState(int vk);

- [DllImport("user32", EntryPoint="GetKeyState")]static extern short WinGetKeyState(int vk);

- [DllImport("user32", EntryPoint="GetKeyboardState")]static extern int WinGetKeyboardState(nint buf);

- [DllImport("user32", EntryPoint="SetKeyboardState")]static extern int WinSetKeyboardState(nint buf);

- [DllImport("user32", EntryPoint="MapVirtualKeyW")]static extern int WinMapVirtualKey(int code, int mapType);

- static int FExtended()

- static int FKeyUp()

- static int FUnicode()

- static void Down(int vk)

- static void Up(int vk)

- static void Press(int vk)

- static void Repeat(int vk, int n)

- static void SendText(string text)

- static void Send(string macro)

- static bool IsDown(int vk)

- static bool IsToggled(int vk)

- static bool CapsLock()

- static bool NumLock()

- static bool ScrollLock()

- static void SetCapsLock(bool on)

- static void SetNumLock(bool on)

- static void SetScrollLock(bool on)

- static async Task WaitUpAsync(int vk)

- static async Task WaitAsync(int vk)

- static void WaitUp(int vk)

- static void Wait(int vk)

- static int VK(string name)

- static string VKName(int vk)

- static void SendKey(int vk, int flags)

- static void SendUnicode(string ch, int flags)

- static int MapScan(int vk)

- [DllImport("zan_gui", EntryPoint="zan_keyboard_is_down")]static extern int NativeIsDown(int vk);

- [DllImport("zan_gui", EntryPoint="zan_keyboard_is_toggled")]static extern int NativeIsToggled(int vk);

- [DllImport("zan_gui", EntryPoint="zan_keyboard_post")]static extern int NativePost(int vk, int isDown);

- static void PosixKey(int vk, string action, string what)

- static string WriteTemp(string text)

- static string OsaQuote(string s)

- static int ParseHex(string s)

- static void MacSend(string macro)

- static string MacUsing(List<string> mods)

- static void MacKey(int vk, List<string> mods)

- static void MacText(string text, List<string> mods)

- static string Hex(int v)

- static void SetToggle(int vk, bool on)

- static bool IsKeyChar(string ch)

- static int CharVK(string ch)

- static int CodeOf(string ch)

- static int IndexOf(string hay, string needle, int from)

- static int ParseInt(string s)

- static string Letter(int index)

- static string Digit(int index)

- static string FromCode(int c)


## Mouse (class)

- static int Left()

- static int Middle()

- static int Right()

- static int XButton1()

- static int XButton2()

- [DllImport("user32", EntryPoint="GetCursorPos")]static extern int WinGetCursorPos(nint pt);

- [DllImport("user32", EntryPoint="SetCursorPos")]static extern int WinSetCursorPos(int x, int y);

- [DllImport("user32", EntryPoint="SendInput")]static extern int WinSendInput(int count, nint inputs, int cbSize);

- [DllImport("user32", EntryPoint="GetAsyncKeyState")]static extern short WinGetAsyncKeyState(int vk);

- static int FMove()

- static int FLeftDown()

- static int FLeftUp()

- static int FRightDown()

- static int FRightUp()

- static int FMiddleDown()

- static int FMiddleUp()

- static int FXDown()

- static int FXUp()

- static int FWheel()

- static int FAbsolute()

- static int VkLeft()

- static int VkRight()

- static int VkMiddle()

- static int VkX1()

- static int VkX2()

- static Point GetPos()

- static void SetPos(int x, int y)

- static void MoveTo(int x, int y)

- static async Task MoveToAsync(int x, int y, int steps)

- static void MoveTo(int x, int y, int steps)

- static void Down(int button)

- static void Up(int button)

- static void Click(int button)

- static void DoubleClick(int button)

- static void Drag(int fx, int fy, int tx, int ty)

- static void Drag(int fx, int fy, int tx, int ty, int button, int steps)

- static void Wheel(int delta)

- static bool IsDown(int button)

- [DllImport("zan_gui", EntryPoint="zan_mouse_is_down")]static extern int NativeIsDown(int button);

- static int XButtonOf(int button)

- static string MacButtonOf(int button)

- static bool HaveCliclick()

- static void Posix(string xargs, string mac, string what)

- static List<int> Ints(string line)

- static void SendMouse(int dx, int dy, int data, int flags)


## bool (delegate)

`delegate bool KeyboardHookCallback(int vk, int scan, int flags, bool injected, bool keyUp);`


## bool (delegate)

`delegate bool MouseHookCallback(int x, int y, int msg, int data, bool injected);`


## nint (delegate)

`delegate nint HookProc(int code, nint wp, nint lp);`


## void (delegate)

`delegate void HotkeyCallback();`
