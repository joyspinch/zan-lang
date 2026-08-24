# System.Input

> 源码: `stdlib/System/Input/Background.zan`, `stdlib/System/Input/Hook.zan`, `stdlib/System/Input/Hotkey.zan`, `stdlib/System/Input/InputTool.zan`, `stdlib/System/Input/Keyboard.zan`, `stdlib/System/Input/Mouse.zan`


## Background (class)

后台输入模拟:向目标窗口的消息队列投递 WM_ 消息,不移动真实鼠标、
不占用真实键盘,用户照常操作其他程序。Windows 实现,其他平台抛
PlatformNotSupportedException。

// 向记事本发送文本与按键(不抢焦点、不动鼠标)
nint hwnd = Background.FindWindowByTitle("无标题 - 记事本");
Background.SendText(hwnd, "后台输入,不影响鼠标键盘");
Background.KeyPress(hwnd, Keyboard.VK("Enter"));
Background.MouseClick(hwnd, Mouse.Left(), 200, 150);

实现分类(前台 vs 后台):
- 前台模拟(System.Input.Mouse / Keyboard):SendInput 注入,事件进入
系统输入队列,会真实移动鼠标、占用键盘;
- 后台模拟(本类):PostMessage 投递 WM_KEYDOWN / WM_CHAR /
WM_LBUTTONDOWN 等到指定窗口。事件不进系统输入队列,目标窗口收到
消息时按收到标准窗口消息处理(普通 Win32 / 控件 / UI 框架都响应)。
注意:DirectInput / 低层输入的游戏通常不读窗口消息,后台模拟对它们
无效;这是所有消息级注入的固有限制。

窗口查找:按标题 / 类名 / 子窗口定位,坐标一律是目标窗口客户区坐标。

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
  - 顶层窗口句柄,标题匹配(大小写不敏感,子串/全串看系统);
    0 = 未找到。标题为中文也可(内部转 UTF-16)。

- static nint FindWindowByClass(string cls)
  - 顶层窗口句柄,按窗口类名匹配;0 = 未找到。
    常用类名:"Notepad"、"ConsoleWindowClass"、"Chrome_WidgetWin_1"。

- static nint FindChild(nint parent, string cls, string title)
  - `parent` 的直接子窗口(类名与标题都匹配才返回;传 "" 表示
    不限定该项);0 = 未找到。

- static bool KeyDown(nint hwnd, int vk)
  - 向 `hwnd` 发送按键按下(WM_KEYDOWN,带扫描码)。
    返回消息是否成功投递。

- static bool KeyUp(nint hwnd, int vk)
  - 向 `hwnd` 发送按键抬起(WM_KEYUP)。

- static bool KeyPress(nint hwnd, int vk)
  - 向 `hwnd` 发送一次按下+抬起。

- static bool Repeat(nint hwnd, int vk, int n)
  - 向 `hwnd` 连续发送 `n` 次按下+抬起。

- static bool SendChar(nint hwnd, string ch)
  - 向 `hwnd` 发送一个字符(WM_CHAR,Unicode 码点)。
    支持中文等多字节 UTF-8 字符;一次一个字符。

- static bool SendText(nint hwnd, string text)
  - 向 `hwnd` 发送整段文本(逐字 WM_CHAR;BMP 单发,代理对拆
    两个 UTF-16 单元)。返回是否全部投递成功。

- static bool SendMacro(nint hwnd, string macro)
  - 后台版按键宏,语法与 Keyboard.Send 相同:花括号按键名
    (修饰键保持按下直到宏结束,逆序释放)与字面文本;单字符可打印按键
    按键发,其余文本按 WM_CHAR 发。

- static bool MouseMove(nint hwnd, int x, int y)
  - 向 `hwnd` 客户区坐标 (x, y) 发送鼠标移动(WM_MOUSEMOVE)。

- static bool MouseDown(nint hwnd, int button, int x, int y)
  - 向 `hwnd` 客户区坐标 (x, y) 发送按键按下(WM_*BUTTONDOWN)。
    `按钮` 用 Mouse.Left()/Middle()/Right()/XButton1()/XButton2()。

- static bool MouseUp(nint hwnd, int button, int x, int y)
  - 向 `hwnd` 客户区坐标 (x, y) 发送按键抬起(WM_*BUTTONUP)。

- static bool MouseClick(nint hwnd, int button, int x, int y)
  - 向 `hwnd` 客户区坐标 (x, y) 发送一次按下+抬起。

- static bool MouseWheel(nint hwnd, int delta, int x, int y)
  - 向 `hwnd` 发送滚轮(WM_MOUSEWHEEL)。`delta` 是滚动量,
    正数向上,通常 ±120 或倍数;位置可选,0,0 用当前客户区(0,0)。

- static bool SyncKeyPress(nint hwnd, int vk)
  - 同步发送一次按键(按下+抬起),等待目标窗口过程处理完。
    适合需要确保目标先收到再继续的脚本;比 PostMessage 慢。

- static bool SyncMouseClick(nint hwnd, int button, int x, int y)
  - 同步发送一次鼠标点击(按下+抬起),等待目标处理完。

- static bool Post(nint hwnd, long msg, long wparam, long lparam)

- static long KeyLParam(int vk, bool up)
  - WM_KEYDOWN/UP 的 lParam:低位重复计数,扫描码(扩展键带
    0x100 前缀,来自 MAPVK_VK_TO_VSC_EX),抬起置位 30/31 位。

- static long CoordLParam(int x, int y)
  - 客户区坐标 (x, y) → lParam(低 16 位 x,高 16 位 y)。

- static long ButtonMsg(int button, bool down)
  - 鼠标按钮 → WM_*BUTTONDOWN/UP 消息;未知按钮返回 0。

- static long ButtonWParam(int button)
  - 鼠标按钮 → 按下状态 wParam(修饰键位;X1/X2 高 16 位)。

- static int CharWidth(string ch)
  - UTF-8 首字符字节宽度:ASCII 1,双字节 2,汉字 3,四字节 4。

- static bool SendCodePoint(nint hwnd, int cp)
  - 码点 → WM_CHAR:BMP 单发,代理对拆两个 UTF-16 单元。

- static bool IsKeyChar(string ch)
  - 单字符是否是可打印按键(a-z/A-Z/0-9)。

- static int CharVK(string ch)
  - 可打印字符 → VK(字母大写化)。

- static int IndexOf(string hay, string needle, int from)
  - `needle` 在 `hay` 中从 `从` 起的首次出现,无则 -1。


## Hook (class)

全局低级键盘/鼠标钩子（WH_KEYBOARD_LL / WH_MOUSE_LL）。
由专用后台线程安装钩子并运行 GetMessage
循环，操作系统在此分发低级钩子回调；
回调即运行在该线程上。回调返回 true 可吞掉
事件（例如不应同时送达当前活动应用的热键）。

同一时刻只能安装一个钩子（键盘或鼠标）；需先
Uninstall 后再安装另一个。回调是普通静态方法——
delegate 无法捕获状态，因此请将回调数据保存在静态
字段中，与 Thread.Start 工作线程的做法相同。

非 Windows 版本暴露相同 API 并抛出
PlatformNotSupportedException。

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

- static bool InstallKeyboard(KeyboardHookCallback cb)
  - 安装低级键盘钩子。钩子已
    生效时返回 true；若已有钩子安装则返回 false。

- static bool InstallMouse(MouseHookCallback cb)
  - 安装低级鼠标钩子。钩子已
    生效时返回 true；若已有钩子安装则返回 false。

- static void Uninstall()
  - 卸载钩子并等待钩子线程结束；
    未安装任何钩子时调用也安全。

- static bool Install(int kind, KeyboardHookCallback kcb, MouseHookCallback mcb)

- static void HookThreadEntry()
  - 运行在钩子线程上：安装后一直泵取 GetMessage。

- static nint HookProcImpl(int code, nint wp, nint lp)
  - 由操作系统调用的 hook proc（运行在钩子线程上）。


## Hotkey (class)

通过 RegisterHotKey 实现全局热键。后台线程注册
组合键（hwnd = 0，因此 WM_HOTKEY 消息会投递到该线程的队列），
并泵取 GetMessage；回调在该线程上运行。

Hotkey.Register("Ctrl+Shift+A", Demo.OnHotkey);
...
Hotkey.Unregister();

组合键语法：修饰键用 '+' 连接，后跟一个来自
`Keyboard.VK` 的键名（不区分大小写），如 "Ctrl+Alt+F2"、
"Shift+Esc"。同一时刻只能有一个热键生效；注册另一个之前先 Unregister。
回调必须是静态方法（不能捕获状态）。

非 Windows 版本暴露相同 API 并抛出
PlatformNotSupportedException。

- static int ModAlt()
  - Register(int, int, ...) 接受的修饰键位。

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

- static bool Register(string combo, HotkeyCallback cb)
  - 从 "Ctrl+Shift+A" 形式的组合键注册热键。组合键生效时
    返回 true；已注册其他热键或组合键格式错误
    （或已被其他程序占用）时返回 false。

- static bool Register(int vk, int mods, HotkeyCallback cb)
  - 从 VK 码和修饰键位注册热键（参见
    `ModCtrl` 等）。组合键生效时返回 true。

- static void Unregister()
  - 注销当前生效的热键并等待其线程退出；
    未注册任何热键时调用也安全。

- static void HotkeyThreadEntry()
  - 运行在热键线程上：注册后泵取消息，直到 WM_HOTKEY 触发
    回调；退出时注销热键。

- static int IndexOf(string hay, string needle, int from)
  - `needle` 在 `hay` 中从 `from` 起的第一个索引，找不到返回 -1。


## InputTool (class)

POSIX 输入注入后端：把 VK 码/文本翻译成平台工具能懂的形式。

X11 下用 xdotool（走 XTEST，和物理设备同一条路径），macOS 走
System Events（osascript）、鼠标需要 cliclick。纯 Wayland 下合成器
不接受任意客户端注入事件，而基于 uinput 的 ydotool 用的是 Linux 输入
子系统的 keycode、不是 keysym，映射不能直接复用，因此目前明确不支持
——伪装成功会让自动化脚本对着一个什么都没发生的桌面继续跑。

所有要注入的文本都写进临时 AppleScript/参数文件而不拼进命令行，
因此文本里的引号和 shell 元字符不会被解释。

- static int None()

- static int Xdotool()

- static int MacOs()

- static bool Have(string tool)

- static int Kind()
  - 本机可用的注入后端。

- static void Missing(string what)
  - 没有可用后端时抛出，附带该平台的安装提示。

- static bool Run(string cmd)
  - 跑一条后端命令；命令由本文件拼装，不含外部文本。

- static bool Osa(string script)
  - 把 AppleScript 写进临时文件后执行——脚本正文不进命令行。

- static string Keysym(int vk)
  - Windows VK 码 -> X11 keysym 名（xdotool/ydotool 的 key 名）。
    不认识的码返回 ""，调用方据此明确报错而不是发一个错的键。

- static int MacKeyCode(int vk)
  - Windows VK 码 -> macOS 虚拟键码（System Events 的 key code）；
    不认识返回 -1。

- static string MacModifier(int vk)
  - System Events 的修饰键名；不是修饰键返回 ""。

- static int[]LetterCodes()

- static int[]DigitCodes()

- static int[]FnCodes()

- static string Ascii(int code)


## Keyboard (class)

键盘输入：按键宏、Unicode 文本、按下/释放/重复、锁定键
状态和 VK 名称表。Windows 通过 SendInput 注入，因此这些
事件在每一层看来都像物理键盘输入（游戏、远程
会话、提权提示）。X11 下走 xdotool（XTEST），macOS 走 System
Events；后端缺失或某个能力无等价物时抛出
PlatformNotSupportedException，而不是默默不发按键。

Keyboard.Send("{Ctrl}{C}");        // 宏：修饰键按下、按键、释放
Keyboard.SendText("你好, Zan");      // literal Unicode 文本
Keyboard.Press(Keyboard.VK("Enter"));

宏是花括号中的按键名序列（修饰键保持按下直到
结束）以及括号外的字面文本。单个字母和数字按
按键按下；其余可打印文本按 Unicode 输入。宏结束时
修饰键按相反顺序释放。

- static int Ctrl()
  - 修饰键，可用于任何接受 VK 码的地方。

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
  - 按住 `vk`。

- static void Up(int vk)
  - 释放 `vk`。

- static void Press(int vk)
  - 按下并释放 `vk` 一次。

- static void Repeat(int vk, int n)
  - 连续按下并释放 `vk` `n` 次。

- static void SendText(string text)
  - 将 `text` 作为字面 Unicode 文本输入——与键盘布局无关，
    对 CJK 和符号安全（使用 KEYEVENTF_UNICODE 而非 VK 码）。

- static void Send(string macro)
  - 播放按键宏；格式见类文档。

- static bool IsDown(int vk)
  - `vk` 被物理按住时为真。

- static bool IsToggled(int vk)
  - 切换键（`CapsLock`/`NumLock`/`ScrollLock`）
    当前处于开启状态时为真。

- static bool CapsLock()
  - CapsLock 开启时为真。

- static bool NumLock()
  - NumLock 开启时为真。

- static bool ScrollLock()
  - ScrollLock 开启时为真。

- static void SetCapsLock(bool on)
  - 设置 CapsLock 状态（0x14）。

- static void SetNumLock(bool on)
  - 设置 NumLock 状态（0x90）。

- static void SetScrollLock(bool on)
  - 设置 ScrollLock 状态（0x91）。

- static void WaitUp(int vk)
  - 阻塞直到 `vk` 被释放。以 1 ms 间隔轮询。

- static void Wait(int vk)
  - 阻塞直到 `vk` 被按下。以 1 ms 间隔轮询。

- static int VK(string name)
  - 键名转 VK 码。名称不区分大小写。可识别
    修饰键、功能键、导航键以及单个字母/数字；
    未知名称返回 0。

- static string VKName(int vk)
  - VK 码转显示名称（"Ctrl"、"F5"、"A" 等）。没有名称的码
    返回 "VK-0xNN"。

- static void SendKey(int vk, int flags)
  - 发送一个键盘 INPUT 事件（x64 下 INPUT 布局：type + padding +
    KEYBDINPUT；wVk/wScan 共享偏移 8 处的 32 位字）。

- static void SendUnicode(string ch, int flags)
  - 为 `ch` 发送一个 Unicode（KEYEVENTF_UNICODE）按键事件。

- static int MapScan(int vk)
  - MapVirtualKey(MAPVK_VK_TO_VSC, 0)：VK 码转硬件扫描码。

- static void PosixKey(int vk, string action, string what)
  - 通过 xdotool 发送一次 keydown/keyup。keysym 未知时明确报错，
    而不是发一个碰巧能编出来的错键。

- static string WriteTemp(string text)
  - 将文本写进临时文件（xdotool type --file 从这里读），返回路径。

- static string OsaQuote(string s)
  - AppleScript 字符串字面量转义（反斜杠和双引号）。

- static int ParseHex(string s)
  - 解析十六进制串（xset 的 LED 掩码）；非法字符处停止。

- static void MacSend(string macro)
  - macOS 的宏播放：System Events 没有 keydown/keyup，因此修饰键
    只能作为每次 keystroke/key code 的 `using` 子句一起发出。

- static string MacUsing(List<string> mods)

- static void MacKey(int vk, List<string> mods)

- static void MacText(string text, List<string> mods)

- static string Hex(int v)
  - `v` 的小写十六进制（无前导零），用于 VK-0x 显示名称。

- static void SetToggle(int vk, bool on)
  - 仅当锁定键处于错误状态时才按下它，将其切换为 `on`（
    避免无条件按下导致状态反了）。

- static bool IsKeyChar(string ch)
  - 单个字母或数字时为真（在宏中作为 VK 按下输入）。

- static int CharVK(string ch)
  - 单个字母/数字对应的 VK 码，否则为 0。

- static int CodeOf(string ch)
  - 单字符字符串的码点（首个字节的 UTF-8 解码；
    ASCII 快速路径）。空字符串返回 0。

- static int IndexOf(string hay, string needle, int from)
  - `needle` 在 `hay` 中从 `from` 起的第一个索引，找不到返回 -1。

- static int ParseInt(string s)
  - 解析非负十进制整数；格式错误返回 -1。

- static string Letter(int index)
  - 0..25 对应 "A".."Z"。

- static string Digit(int index)
  - 0..9 对应 "0".."9"。

- static string FromCode(int c)
  - ASCII 码点转单字符字符串。


## Mouse (class)

鼠标输入：位置、移动（可选分步轨迹）、
点击、拖拽和滚轮。Windows 下通过 SendInput 驱动真实光标
（与物理设备相同的路径，因此在 SetCursorPos 单独使用会被忽略的
提权/远程会话中也有效）。X11 下走 xdotool（XTEST），Wayland 需
ydotool，macOS 需 cliclick；后端缺失时抛出
PlatformNotSupportedException，而不是默默不发事件。

Mouse.SetPos(100, 100);
Mouse.MoveTo(200, 200, 12);     // 12 步缓动轨迹
Mouse.Click(Mouse.Left());

按钮常量：`Left`、`Middle`、
`Right`、`XButton1`、`XButton2`。

- static int Left()
  - 主（左）按钮。

- static int Middle()
  - 中间（滚轮）按钮。

- static int Right()
  - 次（右）按钮。

- static int XButton1()
  - 第一个附加按钮（浏览器"后退"）。

- static int XButton2()
  - 第二个附加按钮（浏览器"前进"）。

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
  - 当前光标位置（屏幕坐标）。

- static void SetPos(int x, int y)
  - 将光标移动到绝对屏幕位置。

- static void MoveTo(int x, int y)
  - 将光标一步移动到 `x`,`y`。

- static void MoveTo(int x, int y, int steps)
  - 将光标分 `steps` 步插值移动到 `x`,`y`，
    每步之间短暂休眠。steps 为 0 或 1 时直接跳过去。
    分步轨迹看起来像人类的拖拽/滑动，而不是瞬间传送，
    某些目标（游戏、远程桌面会话防护）
    对此有要求。

- static void Down(int button)
  - 按下 `button` 不释放。

- static void Up(int button)
  - 释放 `button`。

- static void Click(int button)
  - 在当前位置单击 `button`。

- static void DoubleClick(int button)
  - 在当前位置快速双击。

- static void Drag(int fx, int fy, int tx, int ty)
  - 按住 `button` 从 `fx`,`fy` 拖到 `tx`,`ty`：移动到
    起点、按下、分步滑动、释放。

- static void Drag(int fx, int fy, int tx, int ty, int button, int steps)
  - 指定按钮和轨迹步数进行拖拽。

- static void Wheel(int delta)
  - 滚动滚轮。`delta` 为正向上滚动，为负
    向下；一档为 120（WHEEL_DELTA），整档需传
    120 的倍数。

- static bool IsDown(int button)
  - 当 `button` 被物理按下时返回 true。

- static int XButtonOf(int button)
  - X11 按钮号（1 左 2 中 3 右，8/9 = 后退/前进）。

- static string MacButtonOf(int button)
  - cliclick 的按钮名（只有左/右两个）。

- static bool HaveCliclick()

- static void Posix(string xargs, string mac, string what)
  - 将一个动作发给当前平台后端：`xargs` 是 xdotool/ydotool 的参数，
    `mac` 是 cliclick 的命令。两者都只含本文件生成的数字和字面量。

- static List<int> Ints(string line)
  - 一行里的十进制整数（用于解析 cliclick 的 "x,y"）。

- static void SendMouse(int dx, int dy, int data, int flags)
  - 发送一次鼠标 INPUT 事件（x64 INPUT 布局：4 字节 type + 填充
    + 32 字节 MOUSEINPUT；dwExtraInfo 在偏移 32 处按 ULONG_PTR 对齐）。


## bool (delegate)

返回 true 表示低级键盘事件已被消费（事件被吞掉，
下游不会看到）。返回 false 则放行该事件。
`vk`/`scan` 是键码和硬件扫描码，`flags` 为 LLKHF_*
标志位，`injected` 用于区分合成（SendInput）事件与物理
按键，`keyUp` 为 true 表示按键释放。

`delegate bool KeyboardHookCallback(int vk, int scan, int flags, bool injected, bool keyUp);`


## bool (delegate)

返回 true 表示低级鼠标事件已被消费。`x`/`y` 是屏幕
坐标，`msg` 为 WM_* 鼠标消息 ID（如 WM_LBUTTONDOWN），
`data` 为滚轮增量或 X 键索引，`injected` 标记合成
事件。

`delegate bool MouseHookCallback(int x, int y, int msg, int data, bool injected);`


## nint (delegate)

原生低级钩子过程（WINAPI 调用约定）。

`delegate nint HookProc(int code, nint wp, nint lp);`


## void (delegate)

注册的组合键
按下时，在热键线程上调用。

`delegate void HotkeyCallback();`
