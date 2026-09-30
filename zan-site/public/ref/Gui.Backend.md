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

`dump tree` 输出的根：只含保留模式根节点；未通过 SetRoot
登记根时序列化为空树。

- ControlNode root;


## EventKind (class)

与运行时事件编码一致的事件类型常量。

- static int None()
  - 无事件/哨兵值（0）。

- static int MouseMove()
  - 鼠标移动（1）；坐标为客户端像素。

- static int MouseDown()
  - 鼠标按下（2）；button 0 左键、1 右键。

- static int MouseUp()
  - 鼠标释放（3）。

- static int KeyDown()
  - 按键按下（4）；keycode 为虚拟键码。

- static int KeyUp()
  - 按键释放（5）。

- static int TextInput()
  - 文本输入（6）；keycode 为字符码。

- static int Resize()
  - 窗口尺寸变化（7）；x/y 为新的客户区宽高。

- static int Close()
  - 窗口关闭请求（8）。

- static int Blur()
  - 窗口失焦（9）；兼作文件拖放事件，用 DropPending 区分二者。

- static int Scroll()
  - 滚轮滚动（13）；keycode 携带滚动 delta。


## HitRegionDoc (class)

驱动 `dump hitregions` 输出中的一个命中区域。键（z/id/x/y/
w/h/type）与旧版 dump 格式一致，保证现有脚本有效；`label`
是后加的语义标签（控件自报的按钮文字/列名/操作名，未标注
为空串）——纯附加键，旧的严格解析方不受影响。

- int z;

- int id;

- int x;

- int y;

- int w;

- int h;

- int type;

- string label;


## HitRegionsDoc (class)

`dump hitregions` 输出的根：区域数量与按注册顺序排列的区域列表。

- int count;

- List<HitRegionDoc> regions;


## Ime (class)

输入法（IME）集成。文本控件报告
光标位置，使组合/候选窗口跟随光标而非
固定在窗口原点。

- [DllImport("zan_gui")]static extern void zan_gui_set_ime_pos(int x, int y);

- [DllImport("zan_gui")]static extern void zan_gui_set_ime_open(int on);

- [DllImport("zan_gui")]static extern string zan_gui_ime_composing();

- static void SetCaret(int x, int y)
  - 将 IME 组合 + 候选窗口定位到给定的
    客户区像素坐标（通常是光标底部）。

- static void SetOpen(bool on)
  - 打开/关闭文本输入会话。Android 上软键盘
    随会话显隐：文本控件取得焦点时开、失焦时关——
    会话常开会让键盘在没有任何可输入目标时也占住
    半屏。桌面端会话在建窗时已常开且跟随硬件焦点，
    这里保持空操作（其他平台的驱动重出后再放开）。

- static string Composing()
  - 输入法正在组合、尚未上屏的串（拼音等 CJK
    输入的组词预览）。随帧轮询：组合中非空，commit
    或取消即清空。组合窗口由系统键盘自己显示，这里
    供文本控件在光标处补一份应用内的预览；非 Android
    平台（桌面 IME 自带组合 UI）恒返回 ""。


## Keys (class)

虚拟键码常量（Windows 上的 VK_*）。

- static int Escape()
  - Esc 键（27）。

- static int Enter()
  - Enter 键（13）。

- static int Tab()
  - Tab 键（9）。

- static int Backspace()
  - Backspace 键（8）。

- static int Delete()
  - Delete 键（46）。

- static int Left()
  - 左方向键（37）。

- static int Up()
  - 上方向键（38）。

- static int Right()
  - 右方向键（39）。

- static int Down()
  - 下方向键（40）。

- static int Home()
  - Home 键（36）。

- static int End()
  - End 键（35）。

- static int PageUp()
  - PageUp 键（33）。

- static int PageDown()
  - PageDown 键（34）。

- static int Space()
  - 空格键（32）。

- static int F1()
  - F1 键（112）。

- static int F2()
  - F2 键（113）。

- static int F5()
  - F5 键（116）。

- static int F11()
  - F11 键（122）。

- static int A()
  - 字母 A 键（65）。

- static int C()
  - 字母 C 键（67）。

- static int V()
  - 字母 V 键（86）。

- static int X()
  - 字母 X 键（88）。

- static int Z()
  - 字母 Z 键（90）。

- static int S()
  - 字母 S 键（83）。


## Modifiers (class)

来自 EventMods() 的修饰键标志。

- static int Ctrl()
  - Ctrl 修饰位（1）。

- static int Shift()
  - Shift 修饰位（2）。

- static int Alt()
  - Alt 修饰位（4）。

- static bool HasCtrl(int mods)
  - `mods` 是否含 Ctrl 位。

- static bool HasShift(int mods)
  - `mods` 是否含 Shift 位。

- static bool HasAlt(int mods)
  - `mods` 是否含 Alt 位。


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
clickid @<name> — 经宿主 probe "id.<name>" 当场解析后点击
（widget id 逐进程分配、跨运行不稳定，脚本应优先用 @name 形式）
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

- static List<UiProbe> probes;

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
  - 读取环境变量；未设置（null）归一为 ""。

- static void SetProbe(string name, string val)
  - 宿主钩子：发布（或覆盖）一个命名的内省 probe。

- static string GetProbe(string name)
  - 读取已发布的 probe 值；未发布过返回 ""。

- static int ParseProbeId(string s)
  - `clickid @name` 的数值解析：非纯十进制正整数（含空串）
    一律返回 -1，由调用方记入结果日志。

- static void Tick()
  - 推进脚本。每个事件循环迭代调用一次（从
    App.ProcessEvent 和 IDE 的自定义循环）。注入下一个事件
    或跳过即时（dump/assert/log）命令。在注入的批次之间
    让出，使每批完全消费并渲染后再进行下一批。

- static bool Exec(string line)
  - 执行一行命令。注入事件或设置了
    等待时返回 true（调用方应在下一条命令前让循环先处理）。

- static void ClickAt(int x, int y, int button)
  - 注入移动+按下+释放三个合成事件，等价一次点击；
    button 0 左键、1 右键。

- static void Inject(int kind, int x, int y, int button, int code, int mods)
  - 向绑定窗口注入一个原始合成事件（编码同 Window.InjectEvent）。

- static HitRegion FindRegion(int id)
  - 将命中区域 id 解析为区域（后注册者优先，与
    HitTester 的逆序命中优先级一致）。未注册时返回 null。

- static void DoDump(List<string> t)
  - 执行 `dump` 命令：hitregions/tree/theme/pixels/probe 写出
    JSON 或像素文件（相对路径落到输出目录）。参数缺失或目标
    未知记入结果日志。

- static void DoAssert(List<string> t, string trimmed)
  - 执行 `assert` 命令：按 contains/equals 比较 probe 值与期望，
    计入 pass/fail 统计并把判定写入结果日志（失败附实际值，
    超 200 字符截断）。

- static void Finish()
  - 结束会话：向结果日志写入 DONE pass=N fail=M 汇总（幂等）。

- static string HitRegionsJson()
  - 全部命中区域序列化为 JSON：z 为注册序号（后注册者命中
    优先），type 为 HitTester 的 widgetType。

- static string TreeJson()
  - 当前窗口的控件树（形状 + 布局结果）。根由 Form.Run
    登记；没有保留模式根时写出一棵空树。

- static ControlNode NodeOf(Control c)
  - 把控件子树递归转成 dump 节点：kind/名称/停靠/可见性/
    解析矩形/测量偏好/子节点。

- static string ThemeJson()
  - 当前皮肤解析后的样式 token，外加 DPI 缩放，调用方可
    将 token 转换为命中区域所用的设备像素。

- static string JsonBool(bool b)
  - 布尔值的 JSON 字面量（"true"/"false"）。公开，
    构建 probe JSON 的宿主（如 IDE 的编辑器快照）可复用。

- static void Note(string msg)
  - 向结果日志追加一行。

- static string OutPath(string file)
  - dump 目标路径：绝对路径（以 / 开头或含盘符）原样返回，
    否则拼到输出目录下。

- static int ArgI(List<string> t, int idx, int def)
  - 取第 `idx` 个 token 的整数值；越界或缺失返回 `def`。

- static List<string> Tok(string s)
  - 按空白（空格/制表符）把一行切成 token 列表。

- static string RemainderAfter(string s, int count)
  - 前 `count` 个空白分隔 token 之后的所有内容，去除首尾空白。

- static string Unquote(string s)
  - 去除首尾成对的双引号（不配对则原样返回）。

- static string Trim(string s)
  - 去除首尾空白（空格/制表/回车/换行）。

- static bool StartsWith(string s, string pfx)
  - `s` 是否以 `pfx` 开头。

- static bool Contains(string s, string sub)
  - `s` 是否包含子串 `sub`（空子串恒为真）。

- static string JsonEsc(string s)
  - 对字符串做 JSON 转义（引号、反斜杠、控制字符）。公开，
    构建 probe JSON 的宿主（如 IDE 的编辑器快照）可复用。


## UiProbe (class)

- public string name;

- public string value;

- public UiProbe(string name, string value)


## Win32Shell (class)

Windows 窗口外壳——类注册、窗口过程、
事件队列、DIB 呈现、亚克力玻璃、IME 定位、剪贴板
和自动化注入队列——用 Zan 直接调用 user32/gdi32 编写，
而非 C 运行时文件。像素仍来自光栅化表面
（Canvas），通过 zan_gui_get_pixels 交出缓冲区。

控件层看到的一切都经由 Gui.Window，因此本类是
该抽象的 Windows 半边，与其他平台后端提供的
扁平导出逐一对应。

- [DllImport("zan_gui")]static extern nint zan_gui_guard_wndproc(nint proc);
  - 把窗口过程交给护栏包一层，返回真正注册的入口（见 gui_runtime.c）。

- [DllImport("user32", EntryPoint="RegisterClassExW")]static extern ushort RegisterClassExW(nint wc);

- [DllImport("shell32", EntryPoint="DragAcceptFiles")]static extern void DragAcceptFiles(nint hwnd, int accept);

- [DllImport("shell32", EntryPoint="DragQueryFileW")]static extern int DragQueryFileW(nint hdrop, int index, nint buf, int cch);

- [DllImport("shell32", EntryPoint="DragQueryPoint")]static extern int DragQueryPoint(nint hdrop, nint pt);

- [DllImport("shell32", EntryPoint="DragFinish")]static extern void DragFinish(nint hdrop);

- [DllImport("shell32", EntryPoint="ShellExecuteW")]static extern nint WinShellExecuteW(nint hwnd, nint op, nint file, nint args, nint dir, int show);

- static void ShellOpenUrl(string url)
  - 用系统关联程序打开 url/文件（`<a target="_blank">` 的系统
    浏览器通道）。直接调 shell32 的 ShellExecuteW，不经
    cmd.exe——URL 里的 & ? # = 等字符不会被 shell 元字符拆散。

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
  - GDI 呈现（含对上一呈现帧的差分增量上传），见 gui_runtime.c。
    rects 为 [x,y,w,h]* 扁平数组的原生缓冲，rectCount 是矩形个数；
    rectCount < 0 强制整窗上传（WM_PAINT：重定向表面可能已失效）；
    rectCount == 0 走瓦片差分。返回实际上传的矩形数，-1 失败。

- [DllImport("zan_gui")]static extern void zan_gui_gdi_present_drop(nint hwnd);
  - 窗口销毁时释放它的呈现阴影（见 zan_gui_gdi_present）。

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

- static long evSeq;

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

- static List<nint> zoneHwnd;

- static List<int> zoneWidth;

- static List<nint> fixedHwnd;

- static List<nint> noMaxHwnd;

- static int imeX;

- static int imeY;

- static bool glassOn;

- static int glassTint;

- static int shapeMode;

- static List<int> shapeRegions;

- static int winOpacityPercent;

- static bool hasShadowBand;

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

- static bool presentLog;

- static extern string getenv(string name);

- static bool wmPaintBlit;

- static bool forceFullUpload;

- static nint forceFullHwnd;

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

- static nint scratchRects;

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
  - 当前按下的修饰键位组合：1=Ctrl、2=Shift、4=Alt（可叠加）。

- static int LoWord(nint v)
  - LPARAM 坐标对的低/高有符号 16 位半部。

- static int HiWord(nint v)
  - LPARAM 高半部的有符号 16 位值（与 LoWord 配对）。

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

- static bool DropPending()
  - 拖放后是否有尚未被 TakeDropped 取走的路径（只看不删）。
    kind 9 兼作 blur，应用据此区分真拖放与失焦。

- static List<string> TakeDropped()
  - 交出（并清除）上次拖放的路径。

- static SizePaintBody sizePaint;
  - 系统模态尺寸调整/移动循环里就地重绘一帧的回调，
    由 Window.SetSizePaint 注册；未注册时为 null。

- static bool inSizeMove;
  - 是否正处在系统模态尺寸调整/移动循环中
    （WM_ENTERSIZEMOVE .. WM_EXITSIZEMOVE）。

- static void SetSizePaint(SizePaintBody body)
  - 注册尺寸循环就地重绘回调（窗口拖动/缩放期间保持画面）。

- static nint OnMessage(nint hwnd, int msg, nint wp, nint lp)
  - 窗口过程：把 Win32 消息翻译成事件六元组投进队列（见 Post），
    并承载无边框外壳的全部平台行为——WM_NCHITTEST 命中测试、
    WM_SIZE/WM_PAINT 呈现、IME 重定位、文件拖放、模态尺寸循环
    与最小尺寸约束；未识别的消息交给 DefWindowProcW。
    经护栏包装注册（见 Init），单条消息内的硬故障只丢这条消息。

- static nint HitTest(nint hwnd, int screenX, int screenY)
  - 无边框外壳：边缘为边框尺寸手柄，标题条
    除应用绘制标题按钮处外均可拖拽。

- static int ShapeBandPx()
  - 异形阴影带把内容轮廓推离窗口客户区原点的偏移（逻辑
    像素）。SetWindowShadow 外扩时整份规格被平移 (px,px)——
    类型 3 阴影带从 (0,0) 起、内容区域右移下移——因此非带
    区域的最小 x/y 就是这个偏移（横竖同值）。规格不含阴影带
    （普通异形）或未启用异形时返回 0：命中测试按 0 换算，
    条带行为与普通窗口一致。

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
  - 窗口处于最大化（GetWindowPlacement 的 showCmd 为 SW_SHOWMAXIMIZED）
    时为 true；查询失败为 false。

- static void ImeReposition(nint hwnd)
  - 把 IME 组合窗口与候选窗定位到最近 SetImePos 记录的客户端
    坐标（CFS_POINT + CFS_CANDIDATEPOS）；窗口没有输入上下文时
    空操作。由 WM_IME_* 消息与 SetImePos 触发。

- static void SetImePos(int x, int y)
  - 记录输入光标的客户端坐标 (x, y) 并立即对当前事件窗口
    重定位 IME；此后每次 WM_IME_* 消息都按该坐标重定位。

- static int SurfaceOf(nint hwnd)
  - `hwnd` 最近一次 Present 使用的表面 id；从未呈现过返回 -1。

- static void RememberSurface(nint hwnd, int surface)
  - 记录 `hwnd` 与其当前表面 id 的对应关系，使 WM_PAINT/WM_SIZE
    重绘属于本窗口的帧，而非最后呈现的那个窗口的帧。

- static void FillBitmapInfo(int w, int h)
  - 描述所呈现表面的自上而下 32bpp BI_RGB 头。

- static void Blit(nint hwnd, int surface)
  - 把表面像素呈现到窗口：客户区不大于表面时优先 GPU 直通；
    玻璃（分层）窗口走 PresentLayered；否则经 zan_gui_gdi_present
    上传——有本窗口声明的脏矩形按矩形上传，应用声明整窗
    （PresentFull）、WM_PAINT 或窗口大于表面时强制整窗，其余走
    瓦片差分。表面还没画过任何帧时丢弃脏矩形等应用画完；
    窗口比表面大的部分用 bgColor（SetBackground）补边。

- static void PresentLayered(nint hwnd, int surface)
  - 玻璃呈现：将直通 alpha 表面预乘进缓存的 DIB
    节并交给 DWM，由它在 alpha 低于 255 处
    合成模糊背景。

- static void AcrylicApply(nint hwnd, int state, int tintArgb)
  - 调用未文档化的 SetWindowCompositionAttribute 设置亚克力
    accent 策略：`state` 为 accent 状态（4 = 启用 Acrylic，
    3 = 模态尺寸循环中的过渡态，0 = 关闭），`tintArgb` 为 ARGB
    着色。入口不存在（Win7）时空操作。

- static nint CreateWindow(string title, int width, int height)
  - 创建一个无边框顶层窗口并返回其句柄（失败返回 0）。
    width/height 为 96 DPI 逻辑像素，按显示器 DPI 放大，上限夹到
    所在显示器工作区的 82%，下限只保证画得下标题栏；首个窗口
    居中并成为主框架，后续窗口归主框架所有并在其上方居中。
    窗口接受文件拖放（WM_DROPFILES），并带 1px DWM 框架扩展以
    恢复系统投影与圆角。

- static nint CreateWindowPhysical(string title, int width, int height)
  - 游戏舞台变体（统一 DPI 契约）：width/height 是内容区（舞台）
    的物理像素，不做显示器 DPI 放大；窗口客户区 = 舞台 + 标准标
    题条（App.RenderChrome 把 chrome 画在条里，内容从 App.
    ContentTop() 之下开始；全屏收起标题栏后舞台即整个客户区）。
    ① 不参与工作区 82% 与最小尺寸钳制——舞台分辨率是模板的契约，
    被钳小后画布小于请求值（实测副屏工作区 82% 钳制把
    1280x720 压成 868x517，模板画满全屏的右/下边整块出界）；
    ② 不做 AdjustWindowRect 框架补偿——本壳 WM_NCCALCSIZE 把客
    户区扩成整个窗口矩形，普通路径的补偿只是给客户区添一圈
    死边（150% 屏实测右 22px/下 56px 白条）。
    高 DPI 屏上窗口视觉尺寸变小（物理 1:1），同 SDL 时代的行为。

- static nint CreateWindowSized(string title, int clientW, int clientH, int deviceDpi, bool stage)

- static bool WorkArea(nint hwnd, int flags)
  - 窗口所在显示器的（可用）工作区矩形，写入 scratchRect。

- static bool CenterWindow(nint hwnd)
  - 把窗口居中到所在显示器工作区（不会小于工作区左上角）。
    成功返回 true；取不到窗口矩形或工作区时返回 false。

- static void CenterOnOwner(nint hwnd)
  - 次要窗口（对话框）在主窗口上方居中打开，
    并保持在显示器工作区内。

- static void ShowWindowNow(nint hwnd)
  - 显示并激活窗口（SW_SHOW + UpdateWindow + 前台 + 焦点）。

- static void SetWindowPosition(nint hwnd, int x, int y)
  - 把窗口左上角移动到屏幕工作区坐标 (x, y)，不改尺寸与
    Z 序；hwnd 为 0 时空操作。

- static void ResizeClient(nint hwnd, int scaledW, int scaledH)
  - 把窗口客户区调整为 (scaledW, scaledH) 设备像素（外框随之
    伸缩，位置不变）。WM_SIZE 走正常路径，应用的画布与布局
    下一帧自动跟上。登录小窗 → 主窗口这类形态切换用。

- static void ResizeClientExact(nint hwnd, int scaledW, int scaledH)
  - 把窗口客户区调整为**精确**的 (scaledW, scaledH) 设备像素。
    与 ResizeClient 的区别是**不做 AdjustWindowRect 补偿**：本壳的
    WM_NCCALCSIZE（wp!=0 时直接返回 0）已经把客户区扩成整个窗口矩形，
    再按标准框架补偿就等于凭空加一圈死边（150% 屏实测右 22 / 下 55），
    客户区变成 562x633 而不是请求的 540x578——照参考图量出来的小窗
    会「比图大一圈、内容贴左上」。舞台路径（CreateWindowSized stage=true）
    出于同样理由跳过补偿，这里补上普通路径的精确版本。

- static void Minimize(nint hwnd)
  - 最小化窗口（SW_MINIMIZE）。

- static void ToggleMaximize(nint hwnd)
  - 最大/还原切换（SW_MAXIMIZE 3 / SW_RESTORE 9）。

- static void CloseWindow(nint hwnd)
  - 投递 WM_CLOSE：走正常关闭流程——窗口过程把它翻译成
    kind-8 事件，是否真正销毁由应用决定（见 Destroy）。

- static void Destroy(nint hwnd)
  - 彻底拆除窗口：清理标题按钮/工具窗口登记，释放光栅化器与
    GDI 呈现阴影挂在该句柄上的资源，再销毁 OS 窗口（触发
    WM_DESTROY → kind-8 事件）。

- static bool Maximized(nint hwnd)
  - 窗口是否最大化（IsMaximized 的公开别名）。

- static bool Visible(nint hwnd)
  - 窗口任意部分可见（未最小化且 IsWindowVisible）时为 true。

- static bool Focused(nint hwnd)
  - 窗口是前台窗口（持有键盘焦点）时为 true；hwnd 为 0 恒为 false。

- static void SetTopmost(nint hwnd, bool on)
  - 置顶（HWND_TOPMOST）/取消置顶（HWND_NOTOPMOST），
    不改位置、尺寸与激活状态。

- static void SetTitle(nint hwnd, string title)
  - 设置 OS 窗口标题（任务栏 / Alt-Tab 显示的文本）。

- static int CaptionButtonsOf(nint hwnd)
  - 为 `hwnd` 注册的标题按钮数量（在设置前默认为 5）。

- static void SetCaptionZone(nint hwnd, int width)
  - 为窗口登记标题栏右侧客户区保护带宽度（设备像素）：标题栏
    高度以内、窗口右缘向左 width 的条带 [w-width, w) 内的按下
    交给客户区（应用按钮/胶囊命中区），不当作窗口拖动；
    0 = 缺省（只有右侧系统按钮区交给客户区）。

- static int CaptionZoneOf(nint hwnd)
  - `hwnd` 的标题栏客户区保护带宽度（未登记默认 0）。

- static bool ResizableOf(nint hwnd)
  - 窗口是否允许用户拖边框 / 最大化改变尺寸（默认允许）。

- static void SetResizable(nint hwnd, bool on)
  - 登记窗口是否允许用户改变尺寸；false 时 WM_NCHITTEST 不再
    报告边缘/角手柄，双击标题栏与 SC_MAXIMIZE/SC_SIZE 也被吞掉。
    重复设置同值无副作用。

- static bool MaximizableOf(nint hwnd)
  - 窗口是否允许最大化（默认允许）。关闭了最大化按钮的窗口
    必须登记 false，否则双击标题栏 / SC_MAXIMIZE 照样放大窗口。

- static void SetMaximizable(nint hwnd, bool on)
  - 登记窗口是否允许最大化；false 时双击标题栏与
    SC_MAXIMIZE（Win+Up、系统菜单）不再放大窗口，双击
    已最大的窗口仍可还原。重复设置同值无副作用。

- static List<nint> toolHwnd;

- static void SetToolWindow(nint hwnd, bool on)
  - 工具窗口样式：细标题栏、任务栏与 Alt-Tab 列表不显示。
    幂等：重复设置同值不重写样式。

- static bool IsToolWindow(nint hwnd)
  - hwnd 是否被 SetToolWindow(true) 标记为工具窗口。

- static void SetRoundCorners(nint hwnd, bool on)
  - DwmSetWindowAttribute（dwmapi，Vista+）：Win11 22000+ 认识
    DWMWA_WINDOW_CORNER_PREFERENCE(33)，旧系统返回错误码——懒解析
    加调用失败忽略，等价于安全空操作。

- static void SetCaptionButtons(nint hwnd, int count)
  - 为窗口登记客户区自绘标题按钮数（0..8，越界忽略），
    WM_NCHITTEST 据此在标题栏右侧保留条带（未登记默认 5，
    见 CaptionButtonsOf）。

- static void ForgetCaptionButtons(nint hwnd)
  - 遗忘已关闭窗口的标题按钮条带。

- static int TitlebarHeight()
  - 自绘标题栏保留高度（设备像素，按 DPI 缩放，默认 32@96）。

- static void SetTitlebarHeight(int devicePx)
  - 运行期加高自绘标题栏保留带（设备像素，只增不减）：命中
    测试的拖动条带与标题按钮布局读到的是同一个 titlebarH。

- static int CaptionButtonWidth()
  - 单个标题按钮的保留宽度（设备像素，默认 46@96）。

- static int ClientWidth(nint hwnd)
  - 窗口客户区宽（设备像素）；hwnd 为 0 或查询失败返回 0。

- static int ClientHeight(nint hwnd)
  - 窗口客户区高（设备像素）；hwnd 为 0 或查询失败返回 0。

- static int WindowWidth()
  - 最近一次 WM_SIZE 上报的窗口客户区宽（设备像素）。外壳全局
    只记一个“当前窗口”，多窗口时以最后调整者为准；按句柄精确
    查询用 ClientWidth。

- static int WindowHeight()
  - 最近一次 WM_SIZE 上报的窗口客户区高（设备像素），同 WindowWidth。

- static void SetCursorShape(int kind)
  - 设置鼠标光标形状：0 默认箭头，1 手型，2 I-beam（文本），
    3/4 水平/垂直缩放，5 十字，6 四向移动，7/8 两种对角缩放。

- static List<long> noLayerHosts;
  - 宿主了原生子窗口（CEF / WebView2 的子 HWND）的窗口句柄。分层
    窗口以 UpdateLayeredWindow 呈现时 OS 永远不合成其子窗口——子
    HWND 的表面全零透明，网页内容一帧都上不了屏。这类窗口必须
    一直走 GDI/GL 直通呈现，并拒绝任何把 WS_EX_LAYERED 加回来的
    尝试（呈现路径、玻璃或系统工具都可能写这个位）。

- static bool IsLayerHost(nint hwnd)
  - `hwnd` 是否登记为原生子窗口宿主。

- static void ForbidLayered(nint hwnd)
  - 声明 `hwnd` 宿主了原生子窗口：立刻清掉分层位并登记，之后
    每帧经 SweepLayered 继续拒绝。

- static bool StripLayered(nint hwnd)
  - 清掉 `hwnd` 的分层位。返回是否真的清了（外面有人加回来过），
    调用方据此请求一次全帧重绘——被 ULW 呈现过的那一帧子窗口
    没有上屏，不能留着。

- static bool SweepLayered()
  - 每帧清扫一次：宿主原生子窗口的窗口若又被加上分层位则清掉。
    返回是否清过（供调用方请求重绘）。

- static void EnableGlass(nint hwnd, int tintArgb)
  - 启用系统亚克力玻璃并把窗口转入分层呈现路径：此后 Present
    经 PresentLayered 预乘 alpha 交给 DWM 合成模糊。`tintArgb`
    为 ARGB 着色。宿主原生子窗口的窗口保持 GDI 呈现，仅设置
    accent（分层窗的 ULW 呈现不合成子 HWND）。

- static void DisableGlass(nint hwnd)
  - 关闭亚克力 accent（状态 0）并摘掉 WS_EX_LAYERED，恢复普通
    GDI/直通呈现；hwnd 为 0 时空操作。

- static void SetOpacity(nint hwnd, int percent)
  - 设置整窗不透明度：percent 钳制到 10..100（100 = 不透明），
    经 SetLayeredWindowAttributes 实现，因此窗口必须分层；
    percent 为 100 且未开玻璃时恢复不透明并摘掉分层位。
    宿主原生子窗口的窗口不支持（分层会连子窗口一起藏掉），
    空操作。

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
  - 呈现一帧：登记 hwnd 与 surface 的对应关系并记住 lastSurface
    后，异形窗口走 PresentLayered（逐像素 alpha 合成），普通
    窗口走 Blit。表面尚无像素时忽略。

- static void PresentDirty(nint hwnd, int x, int y, int w, int h)
  - 为 `hwnd` 的下一次呈现声明一个脏矩形（客户端像素）；
    w/h <= 0 忽略，超过 2048 个后置溢出标志（该窗口下次整窗
    上传）。声明同一时刻只属于一个窗口：换窗口声明时，旧窗口
    未上传的矩形记入欠账（NoteDirtyLost），它下次整窗补齐。

- static void PresentFull(nint hwnd)
  - 整窗帧的呈现声明：这一帧重画了窗口的每个像素，下一次 Blit
    对它整窗上传（rectCount=-1），不做任何差分。见 forceFullUpload。

- static void NoteDirtyLost(nint hwnd)
  - 记下一个窗口有一批声明过却没上传成的脏矩形：它下一次呈现
    整窗上传，而不是只上传那一次声明的那几块。

- static bool TakeDirtyLost(nint hwnd)
  - 这个窗口是否欠着一次整窗上传（并消费掉该记录）。

- static void ClearEvent()
  - 清空当前事件槽为“无事件”状态（kind 0，句柄 0）。
    每次 PollEvent/WaitEvent 取事件前调用。

- static bool DrainInjected()
  - 若自动化驱动留有排队的合成事件，则取出一个处理。

- static bool DrainPosted()
  - 取出窗口过程排队的一个事件（队列空则 false）。

- static int PollEvent()
  - 不阻塞取事件：优先自动化注入队列，再是窗口过程队列，最后
    泵尽线程队列里的全部 Win32 消息（其间窗口过程会补充事件）。
    返回 0 = 取到事件（EventKindValue 等读取）、1 = 无事件、
    -1 = WM_QUIT。

- static int WaitEvent()
  - 阻塞直到下一个消息到达（GetMessageW，内核空闲）。
    返回 0 = 取到事件、-1 = WM_QUIT 或错误。

- static int WaitEventTimeout(int ms)
  - 阻塞到事件到达或 `ms` 毫秒过去（PeekMessage +
    MsgWaitForMultipleObjects，内核空闲）。返回 0 = 取到事件、
    1 = 超时、-1 = 退出；ms < 0 按 0。

- static void Wake()
  - 向主窗口投递 WM_NULL，唤醒阻塞在消息等待中的 UI 线程
    （App.Post 从任意线程调用）。

- static void InjectEvent(nint hwnd, int kind, int x, int y, int button, int keycode, int mods)
  - 把一个合成事件排队（临界区保护，可从任意线程调用），编码
    与窗口过程 Post 的事件六元组一致；hwnd 为 0 时投给主窗口。
    入队后 Wake 主线程。

- static int InjectPending()
  - 尚未被取走的合成事件数量。

- static int EventKindValue()
  - 最近取出事件的类型（EventKind 编码）。

- static long EventSeqValue()
  - 事件单调序号：每取到一个新事件 +1，没有新事件保持不变，
    用于区分“新事件”与“陈旧回读”。

- static int EventXValue()
  - 最近事件 X（客户端像素；键盘类为 0）。

- static int EventYValue()
  - 最近事件 Y（客户端像素；键盘类为 0）。

- static int EventButtonValue()
  - 最近事件的鼠标键：0 左键、1 右键；非鼠标事件为 0。

- static int EventKeyCodeValue()
  - 最近事件的键码：KeyDown/KeyUp 为虚拟键码，WM_CHAR 为字符码，
    滚轮为滚动 delta。

- static int EventModsValue()
  - 最近事件按下时的修饰键组合（1 Ctrl、2 Shift、4 Alt）。

- static nint EventWindow()
  - 最近取出事件所属的窗口句柄（Post 时记录；无事件为 0）。

- static nint WndProcWindow()
  - 窗口过程当前正在处理的消息所属的窗口。与 EventWindow()
    不同：那一个只在应用泵出事件时更新，而模态尺寸调整循环
    期间应用拿不到事件，sizePaint 回调（窗口过程里触发）必须
    用这一个才能知道"正在缩放的是哪个窗口"。

- static int TickMs()
  - 来自性能计数器的单调毫秒：GetTickCount 仅每约 15 ms
    变化一次，这会使每帧动画步长抖动。

- static int TickUs()
  - 来自同一个性能计数器的单调微秒，用于分相位计时：一帧只有几
    毫秒，按毫秒取整时每个相位都被截成 0 或 1，看不出谁贵。
    返回低 32 位（约 71.6 分钟回绕）：回绕周期整除 int 的模长，
    跨回绕的带符号差值仍然精确（±35.7 分钟内）。不能换成任意
    周期（比如 1000 秒）——跨点的差值会错出整整一圈，UiDriver
    的 wait 就要多等一整圈才超时。

- static void Sleep(int ms)
  - 让调用线程休眠 `ms` 毫秒；ms <= 0 不休眠。

- static bool OpenClipboardRetry()
  - 剪贴板同一时刻只能被一个进程打开，而剪贴板查看器、
    输入法、浏览器都会在内容变化后瞬间打开它。只试一次就
    放弃，就是“复制粘贴时灵时不灵”的来源：短暂重试几次，
    对方用完就轮到我们。

- static bool SetClipboard(string text)
  - 把 UTF-16 文本以 CF_UNICODETEXT（GMEM_MOVEABLE）放入剪贴板：
    成功后内存所有权归剪贴板；打不开剪贴板（被其他进程占用）时
    短暂重试，仍失败返回 false。

- static string GetClipboard()
  - 读取剪贴板 CF_UNICODETEXT 文本；格式不存在、打不开剪贴板
    或取不到数据时返回 ""。

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

- [DllImport("zan_gui")]static extern int zan_gui_guard_call(nint fn, nint arg);
  - 故障护栏：把一段工作（一次事件分发、一帧绘制）交给运行时执行，
    期间发生硬故障（空指针访问、越界读写、除零、生成代码的运行时
    检查失败）时记录一条崩溃记录并放弃这段工作，而不是让整个进程
    消失。参数是函数指针（Zan 的 `static void f(nint)`）与它的实参。

- [DllImport("zan_gui")]static extern nint zan_gui_guard_wndproc(nint proc);
  - 把窗口过程交给护栏包一层：返回应当注册进 WNDCLASSEX 的地址，
    某条消息处理中发生硬故障时在回调内部恢复（丢掉这条消息、按默认
    处理返回），而不是跨过 DispatchMessageW 往外跳。

- [DllImport("zan_gui")]static extern int zan_gui_guard_recovered();
  - 本次运行被护栏吞掉的硬故障次数。

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

- [DllImport("zan_gui")]static extern int zan_gui_adopt_sdl_window(nint hwnd);

- [DllImport("zan_gui")]static extern int zan_gui_scene_set_renderer(nint hwnd, nint renderer);

- [DllImport("zan_gui")]static extern int zan_gui_scene_upload(nint hwnd, string bgra, int w, int h);

- [DllImport("zan_gui")]static extern int zan_gui_scene_present(nint hwnd, int surfaceId);

- public static int AdoptSdlWindow(nint sdlWindowHandle)
  - 把游戏宿主自建的 SDL 窗口交给 GUI 运行时，此后 `App` 可在它
    之上驱动控件（HUD 层）。返回 0 表示成功（或该窗口已被接管）。
    仅 SDL 后端有效；Win32 外壳构建返回非 0。

- public static int SceneSetRenderer(nint sdlWindowHandle, nint sdlRendererHandle)
  - 把宿主的 SDL 渲染器交给 GUI 运行时（同一窗口第二个渲染器在
    D3D11 等后端会创建失败，故复用宿主的）。在 AdoptSdlWindow 之后、
    首次 ScenePresent 之前调用一次。

- public static int SceneUpload(nint sdlWindowHandle, string rgba, int w, int h)
  - 上传一帧 RGBA 场景图像（w×h，每像素 4 字节，行优先，保留
    alpha）——SdlRenderer.ReadPixels 的输出可直接传入；
    ScenePresent 时合成在 HUD 表面之下。

- public static int ScenePresent(nint sdlWindowHandle, int surfaceId)
  - 把场景纹理（下）与 HUD 表面（上）合成呈现到已接管的窗口。

- nint handle;

- int width;

- int height;

- Window(string title, int width, int height):this(title, width, height, false)
  - 创建新的平台窗口（96 DPI 逻辑像素；Windows 上按显示
    器 DPI 放大客户区，其余平台原样）。

- Window(string title, int width, int height, bool physical)
  - physical=true 时 width/height 为内容区物理像素，任何
    平台都不做 DPI 放大（Windows 上客户区额外加标准标题条高度，
    内容区从 App.ContentTop() 之下开始）。

- static Window CreatePhysical(string title, int width, int height)
  - physical=true 时 width/height 为内容区（游戏舞台）的
    物理像素，运行时不做 DPI 放大；Windows 上客户区 = 舞台 + 标准
    标题条（内容从 App.ContentTop() 之下开始）——游戏舞台契约：
    画布舞台区尺寸恒等于请求值，与显示器 DPI 无关，输入坐标 1:1。
    非 Windows 后端原样创建（全屏表面，无标题条预留）。

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

- void SetClientSize(int w, int h)
  - 把窗口客户区调整为 (w, h) 逻辑像素，外框随之伸缩、
    位置不变。登录小窗 → 主窗口这类形态切换用；WM_SIZE 走正常
    路径，画布与布局下一帧自动跟上。

- void SetClientSizeDev(int wDev, int hDev)
  - 把客户区调整为**精确**的 (wDev, hDev) **设备**像素，不加
    OS 框架补偿（本壳客户区本就等于窗口矩形）。登录小窗这类「照参考图
    定尺寸」的窗口用它：SetClientSize 的补偿会让实际客户区比请求值大
    一圈（150% 屏 540x578 → 562x633），内容看着贴左上、四周留白不匀；
    且设备像素入参没有 `logical*144/96` 的截断（578 不是 1.5 的整数倍，
    走逻辑像素只能落在 577 或 579 上）。只放大窗口的形态切换
    （登录小窗 → 主窗）继续用 SetClientSize——主窗那套 Spec 尺寸是
    按补偿后的 1888x993 校准的，换成本函数会把整窗布局打偏。

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

- bool DropPending()
  - 拖放后队列中是否有尚未被 TakeDroppedFiles 取走的路径
    （只看不删）。kind 9 兼作 blur，应用据此在当前事件帧上
    区分真拖放与失焦，避免 EventKind 被后续事件覆盖而漏收。

- int EventKind()
  - 返回最近一次事件类型（0=无、1=鼠标移动、2=鼠标按下、3=鼠标释放、4=按键按下、5=按键释放、6=文本输入、7=调整大小、8=关闭、9=文件拖放、13=滚动、14=表面还原/重露（移动端：表面被系统重建，需整帧重画））。

- long EventSeq()
  - 最近投递事件的单调序号：每投递一个新事件 +1，
    没有新事件时保持不变。EventKind()/EventX() 等在没有新
    事件的循环迭代里返回上一次的旧值（动画分支不泵就退出），
    应用要区分"新事件"与"陈旧回读"时比对这里。

- static long EventSeqGlobal()
  - 最近投递事件的进程级序号（不依赖某个窗口实例）。多顶层
    窗口的事件路由需要一个「这件事件处理过没有」的幂等键，
    而路由器（ChildWindows.Route）手里只有 hwnd、没有窗口对象。

- int EventX()
  - 最近事件的 X 坐标：鼠标/滚轮/拖放为客户端像素，键盘类为 0。

- int EventY()
  - 最近事件的 Y 坐标：鼠标/滚轮/拖放为客户端像素，键盘类为 0。

- int EventButton()
  - 最近事件的鼠标键：0 左键、1 右键；非鼠标事件为 0。

- int EventKeyCode()
  - 最近事件的键码：KeyDown/KeyUp 为虚拟键码，TextInput 为
    字符码，Scroll 为滚动 delta。

- int EventMods()
  - 最近事件按下时的修饰键组合（Modifiers 位掩码）。

- int EventFlag()
  - 事件标记通道：1 = 本次释放是一次触摸拖拽的收尾（手指落点
    随拖动走到哪算哪），App 据此不把它当作点击分发。Windows
    外壳经系统合成鼠标自带同坐标 down/up，无此标记恒为 0。

- int GetWidth()
  - 最近一次收到调整大小事件的窗口客户区宽（设备像素）。外壳
    全局只记一个“当前窗口”，多窗口应用要按句柄精确查询时用
    ClientWidth/ClientHeight。

- int GetHeight()
  - 最近一次收到调整大小事件的窗口客户区高（设备像素），同 GetWidth。

- nint GetHandle()
  - 此窗口的原生句柄（不透明；用于事件路由）。

- static nint EventHwnd()
  - 产生最近一次事件的窗口句柄，
    拥有多个顶层窗口的应用可据此正确路由事件。

- static nint SizePaintWindow()
  - 窗口过程当前正在处理的消息所属的窗口句柄。
    模态尺寸调整循环期间应用不泵事件（EventHwnd 停留在旧值），
    sizePaint 回调须用这一个路由"正在缩放的窗口"。

- int ClientWidth()
  - 本窗口客户区宽/高（设备像素），按窗口查询
    （不同于反映最近调整大小的窗口的 GetWidth）。

- int ClientHeight()
  - 本窗口客户区高（设备像素），按窗口查询（同 ClientWidth）。

- void Present(Canvas canvas)
  - 将 Canvas（表面）呈现到窗口。

- void PresentDirty(int x, int y, int w, int h)
  - 将下一次 Present 限制在声明的像素上：
    只重绘了几个小矩形的帧只需几次小
    上传，而非整个表面。声明每个变化的矩形
    ——遗漏的部分保留先前呈现的像素。
    不声明（默认）则呈现全部。

- void PresentFull()
  - 整窗帧的呈现声明：本帧重画了窗口的每个像素，
    下一次 Present 对整个窗口上传，不做任何差分。
    整窗帧若不声明，呈现范围就交给运行时的差分影子——
    影子与屏幕失步时旧帧内容会永久留在屏上（见
    Win32Shell.PresentFull）。

- void SetTitle(string title)
  - 设置窗口标题。

- static void SetCursor(int cursorType)
  - 设置鼠标光标类型。

- static bool GuardCall(nint fn, nint arg)
  - 在故障护栏下执行 `fn(arg)`：正常跑完返回 true；
    期间的硬故障（越界、空指针、除零……）已记入
    <exe 目录>\zan_crash.log 并放弃这段工作时返回 false，
    进程继续运行。`fn` 必须是 `static void f(nint)` 的函数指针。

- static nint GuardWndProc(nint proc)
  - 用护栏包住窗口过程 `proc`（`static nint f(nint, int, nint, nint)`），
    返回改为注册的地址。一条消息内的硬故障在回调内部就地恢复，只丢这
    一条消息；窗口消息状态由 Win32 正常收尾，不会卡住消息循环。

- static int GuardRecovered()
  - 本次运行被护栏吞掉的硬故障次数。

- static int RawTickMs()
  - 未冻结的真实单调毫秒时钟：FreezeTick 只影响 GetTickMs，
    经此读取的时钟照走。

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

- void SetRoundCorners(bool on)
  - 窗口外轮廓圆角（Windows 11 22000+ 由 DWM 合成：
    一致、抗锯齿、与系统投影匹配；旧系统安全空操作）。`on`
    为 false 时恢复系统默认方角/策略。无边框窗口需要保持
    1px DWM 框架扩展（创建时已加）圆角才可见。

- void SetTopmost(bool on)
  - 启用或禁用窗口置顶（topmost）。

- void SetResizable(bool on)
  - 窗口是否可由用户改变大小。关闭后边框不再
    充当尺寸手柄，双击标题栏与最大化也不再放大窗口。
    仅限 Windows；其他平台为安全空操作。

- void SetMaximizable(bool on)
  - 窗口是否允许最大化（默认允许）。标题栏不画最大化
    按钮的窗口应关闭它，否则双击标题栏与 Win+Up 仍会放大窗口。
    仅限 Windows；其他平台为安全空操作。

- void SetToolWindow(bool on)
  - 工具窗口样式（WS_EX_TOOLWINDOW）：细标题栏、
    不占任务栏与 Alt-Tab 列表。仅限 Windows；
    其他平台为安全空操作。

- bool ToolWindow()
  - 本窗口此前是否被 SetToolWindow(true) 标记过。

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

- void SetTitlebarHeight(int devicePx)
  - 运行期加高自绘标题栏保留带（设备像素，只增不减）：
    原生拖动条带与标题按钮布局同步加高。仅 Windows 后端支持
    运行期加高；其余后端维持启动时的缺省保留带。

- void SetCaptionZone(int width)
  - 登记标题栏右侧客户区保护带宽度（设备像素）：标题栏
    高度以内、窗口右缘向左 width 的条带 [w-width, w) 内的按下
    交给客户区（应用按钮/胶囊命中区），不当作窗口拖动。
    仅 Windows 后端实现。

- static int CaptionButtonWidth()
  - 单个标题按钮的宽度（设备像素）。


## int (delegate)

SetWindowCompositionAttribute（未文档化的 user32 入口，懒解析）。

`delegate int SetWinCompAttrFn(nint hwnd, nint data);`


## int (delegate)

DwmSetWindowAttribute（dwmapi，Vista+）：Win11 22000+ 认识
DWMWA_WINDOW_CORNER_PREFERENCE(33)，旧系统返回错误码——懒解析
加调用失败忽略，等价于安全空操作。

`delegate int DwmSetWindowAttrFn(nint hwnd, int attr, nint value, int size);`


## int (delegate)

DwmIsCompositionEnabled（dwmapi，Vista+）：Win7 关闭 DWM 时为 false。

`delegate int DwmIsCompFn(nint outEnabled);`


## int (delegate)

SetProcessDpiAwareness (Shcore) / SetProcessDPIAware (user32) 回退方案。

`delegate int SetProcessDpiAwarenessFn(int level);`


## int (delegate)

SetProcessDPIAware（user32，Vista+）：Shcore 不存在时的
系统 DPI 感知回退。

`delegate int SetProcessDpiAwareFn();`


## nint (delegate)

Win32 窗口过程，用 Zan 实现并交给 RegisterClassExW。

`delegate nint ZanWndProc(nint hwnd, int msg, nint wp, nint lp);`


## void (delegate)

在系统模态尺寸调整循环里就地绘制的一帧（见
Window.SetSizePaint）。

`delegate void SizePaintBody();`
