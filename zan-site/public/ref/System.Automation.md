# System.Automation

> 源码: `stdlib/System/Automation/UiElement.zan`, `stdlib/System/Automation/Window.zan`


## UiElement (class)

MSAA (IAccessible) 无障碍元素:从窗口或屏幕坐标取 UI 元素,遍历子元素,
按名称/角色查找,读名称/值/角色/状态/矩形,执行默认动作/选中/聚焦/设值。

UiElement root = UiElement.FromWindow(wnd);
string name = root.Name();
int role    = root.Role();
List<UiElement> kids = root.Children();
UiElement btn = root.FindFirst("保存", UiElement.RolePushButton());
btn.Invoke();            // 模拟点击
btn.Dispose();

引用语义:每个 UiElement 拥有一个 IAccessible 引用,用完必须 Dispose();
Children/Parent/HitTest/FindAll 返回的元素都是新引用,各自 Dispose。
简单元素(无独立 IAccessible 的文本等)借用父对象,仍持有独立引用,安全。
Windows 仅;其他平台抛 PlatformNotSupportedException。

- static int SlotParent()

- static int SlotChildCount()

- static int SlotName()

- static int SlotValue()

- static int SlotDescription()

- static int SlotRole()

- static int SlotState()

- static int SlotKeyboardShortcut()

- static int SlotDefaultAction()

- static int SlotSelect()

- static int SlotLocation()

- static int SlotHitTest()

- static int SlotDoDefaultAction()

- static int SlotPutValue()

- static string IID_IAccessible()

- static int ObjClient()

- static int ObjWindow()

- static int VtI4()

- static int ChildSelf()

- static nint oleacc;

- static nint fnFromWindow;

- static nint fnFromPoint;

- static nint fnChildren;

- static nint fnWindowFromAcc;

- static nint fnSysFree;

- static nint fnSysAlloc;

- nint acc;

- int childId;

- static nint OleAcc()

- static bool EnsureCom()

- static nint FromWindowFn()

- static nint FromPointFn()

- static nint ChildrenFn()

- static nint WindowFromAccFn()

- static nint Bstr(string s)

- static void FreeBstr(nint bstr)

- static string TakeBstr(nint bstr)
  - 读取并释放一个 BSTR out 参数。

- static nint ChildVariant(int id)

- UiElement(nint acc, int childId)
  - 接管引用:acc 必须已是调用方拥有的引用(AddRef 过);Dispose 时释放。
    acc==0 表示空元素。

- static UiElement FromWindow(nint hwnd)
  - 从窗口句柄取客户区无障碍根元素(OBJID_CLIENT,失败时回退
    OBJID_WINDOW)。窗口无无障碍对象时返回空元素。

- static UiElement FromPoint(int x, int y)
  - 从屏幕坐标取该点的无障碍元素;该点无元素时返回空元素。

- static void Shutdown()
  - 结束当前线程由 UiElement 隐式建立的 COM apartment。
    必须在该线程所有 UiElement 都 Dispose 后、线程退出前调用。

- void Dispose()
  - 释放持有的 IAccessible 引用。与每个工厂/Children/Parent/
    HitTest/FindAll 返回值配对。

- bool IsValid()
  - 是否有底层无障碍对象。

- string Name()
  - 无障碍名称(通常就是控件文本)。

- string Value()
  - 无障碍值(编辑框文本、滚动条位置等);无值返回 ""。

- string Description()
  - 无障碍描述;无则返回 ""。

- int Role()
  - 角色码(ROLE_SYSTEM_*);无对象返回 0。

- int State()
  - 状态位(STATE_SYSTEM_* 的组合);无对象返回 0。

- bool HasState(int bits)
  - 状态位测试:bits 全部置位时为 true。

- string DefaultAction()
  - 默认动作描述(按钮的"Press"等);无则返回 ""。

- string KeyboardShortcut()
  - 快捷键描述(如 "Alt+F");无则返回 ""。

- WindowRect Rect()
  - 屏幕矩形(get_accLocation)。

- int ChildCount()
  - 直接子元素数量。

- List<UiElement> Children()
  - 直接子元素列表(调用方 Dispose 每个元素)。

- UiElement Parent()
  - 父元素;已是根时返回空元素。

- UiElement HitTest(int x, int y)
  - 命中测试:返回该坐标处的子元素;无命中返回空元素。

- List<UiElement> FindAll(string name, int role)
  - 递归查找名称(完全相等;"" 表示任意)与角色(0 表示任意)
    都匹配的元素。返回的元素各自持有独立引用,调用方 Dispose。
    深度上限 64,防环。

- UiElement FindFirst(string name, int role)
  - 递归查找第一个匹配元素;未找到返回空元素。返回的元素是
    新引用,调用方 Dispose。

- static void Walk(UiElement el, string name, int role, int depth, List<UiElement> outList)

- bool Invoke()
  - 执行默认动作(按钮点击、链接跳转等)。

- bool Select()
  - 选中并聚焦(SELFLAG_TAKEFOCUS | SELFLAG_TAKESELECTION)。

- bool Focus()
  - 聚焦(SELFLAG_TAKEFOCUS)。

- bool SetValue(string v)
  - 设置值(put_accValue;编辑框等可写元素生效)。

- static int RoleTitleBar()
  - ROLE_SYSTEM_TITLEBAR(1)。

- static int RoleMenuBar()
  - ROLE_SYSTEM_MENUBAR(2)。

- static int RoleMenuPopup()
  - ROLE_SYSTEM_MENUPOPUP(11)。

- static int RoleMenuItem()
  - ROLE_SYSTEM_MENUITEM(12)。

- static int RoleAlert()
  - ROLE_SYSTEM_ALERT(8)。

- static int RoleWindow()
  - ROLE_SYSTEM_WINDOW(9)。

- static int RoleClient()
  - ROLE_SYSTEM_CLIENT(10)。

- static int RoleApplication()
  - ROLE_SYSTEM_APPLICATION(14)。

- static int RoleDocument()
  - ROLE_SYSTEM_DOCUMENT(15)。

- static int RolePane()
  - ROLE_SYSTEM_PANE(16)。

- static int RoleDialog()
  - ROLE_SYSTEM_DIALOG(18)。

- static int RoleGrouping()
  - ROLE_SYSTEM_GROUPING(20)。

- static int RoleTable()
  - ROLE_SYSTEM_TABLE(24)。

- static int RoleCell()
  - ROLE_SYSTEM_CELL(29)。

- static int RoleLink()
  - ROLE_SYSTEM_LINK(30)。

- static int RoleList()
  - ROLE_SYSTEM_LIST(33)。

- static int RoleListItem()
  - ROLE_SYSTEM_LISTITEM(34)。

- static int RoleOutline()
  - ROLE_SYSTEM_OUTLINE(35)。

- static int RoleOutlineItem()
  - ROLE_SYSTEM_OUTLINEITEM(36)。

- static int RolePageTab()
  - ROLE_SYSTEM_PAGETAB(37)。

- static int RoleGraphic()
  - ROLE_SYSTEM_GRAPHIC(40)。

- static int RoleStaticText()
  - ROLE_SYSTEM_STATICTEXT(41)。

- static int RoleText()
  - ROLE_SYSTEM_TEXT(42)。

- static int RolePushButton()
  - ROLE_SYSTEM_PUSHBUTTON(43)。

- static int RoleCheckButton()
  - ROLE_SYSTEM_CHECKBUTTON(44)。

- static int RoleRadioButton()
  - ROLE_SYSTEM_RADIOBUTTON(45)。

- static int RoleComboBox()
  - ROLE_SYSTEM_COMBOBOX(46)。

- static int RoleDropList()
  - ROLE_SYSTEM_DROPLIST(47)。

- static int RoleProgressBar()
  - ROLE_SYSTEM_PROGRESSBAR(48)。

- static int RoleSlider()
  - ROLE_SYSTEM_SLIDER(51)。

- static int RoleSpinButton()
  - ROLE_SYSTEM_SPINBUTTON(52)。

- static int RolePageTabList()
  - ROLE_SYSTEM_PAGETABLIST(60)。

- static int RoleSplitButton()
  - ROLE_SYSTEM_SPLITBUTTON(62)。

- static string RoleName(int role)
  - 角色码 -> 可读名称;未知角色返回 "role:N"。

- static int StateUnavailable()
  - STATE_SYSTEM_UNAVAILABLE(1)。

- static int StateSelected()
  - STATE_SYSTEM_SELECTED(2)。

- static int StateFocused()
  - STATE_SYSTEM_FOCUSED(4)。

- static int StatePressed()
  - STATE_SYSTEM_PRESSED(8)。

- static int StateChecked()
  - STATE_SYSTEM_CHECKED(16)。

- static int StateReadOnly()
  - STATE_SYSTEM_READONLY(64)。

- static int StateExpanded()
  - STATE_SYSTEM_EXPANDED(512)。

- static int StateCollapsed()
  - STATE_SYSTEM_COLLAPSED(1024)。

- static int StateBusy()
  - STATE_SYSTEM_BUSY(2048)。

- static int StateInvisible()
  - STATE_SYSTEM_INVISIBLE(32768)。

- static int StateOffscreen()
  - STATE_SYSTEM_OFFSCREEN(65536)。

- static int StateFocusable()
  - STATE_SYSTEM_FOCUSABLE(1048576)。

- static int StateSelectable()
  - STATE_SYSTEM_SELECTABLE(2097152)。


## Window (class)

Win32 窗口自动化：枚举、查找、等待窗口和子控件，
（class + title + id 多条件匹配，带超时），
读取/设置文本、点击控件和菜单命令、显示/隐藏/置顶/
前置/最小化、屏幕与客户区坐标互转、所属
线程/进程 ID 及卡死检测。

nint wnd = Window.FindWindowByTitle("无标题 - 记事本");
Window.SetText(wnd, "hello");
string t = Window.GetText(wnd);
Window.ClickMenu(wnd, "文件/保存");
Window.Close(wnd);

匹配方式为子串匹配（class 和 title），区分大小写；"" 或 0 表示
"任意"。标题通过 SendMessageTimeout 跨进程读取，卡死的
窗口会匹配失败而非阻塞调用方。仅支持 Windows；其他
平台会抛出 PlatformNotSupportedException。

- [DllImport("user32", EntryPoint="IsWindow")]static extern int WinIsWindow(nint hwnd);

- [DllImport("user32", EntryPoint="IsWindowVisible")]static extern int WinIsWindowVisible(nint hwnd);

- [DllImport("user32", EntryPoint="IsWindowEnabled")]static extern int WinIsWindowEnabled(nint hwnd);

- [DllImport("user32", EntryPoint="IsIconic")]static extern int WinIsIconic(nint hwnd);

- [DllImport("user32", EntryPoint="IsZoomed")]static extern int WinIsZoomed(nint hwnd);

- [DllImport("user32", EntryPoint="IsHungAppWindow")]static extern int WinIsHungAppWindow(nint hwnd);

- [DllImport("user32", EntryPoint="IsWindowUnicode")]static extern int WinIsWindowUnicode(nint hwnd);

- [DllImport("user32", EntryPoint="IsChild")]static extern int WinIsChild(nint parent, nint hwnd);

- [DllImport("user32", EntryPoint="GetClassNameW")]static extern int WinGetClassNameW(nint hwnd, nint buf, int max);

- [DllImport("user32", EntryPoint="GetWindowThreadProcessId")]static extern int WinGetWindowThreadProcessId(nint hwnd, nint pidOut);

- [DllImport("user32", EntryPoint="GetParent")]static extern nint WinGetParent(nint hwnd);

- [DllImport("user32", EntryPoint="GetAncestor")]static extern nint WinGetAncestor(nint hwnd, int flags);

- [DllImport("user32", EntryPoint="GetWindow")]static extern nint WinGetWindow(nint hwnd, int cmd);

- [DllImport("user32", EntryPoint="GetWindowLongPtrW")]static extern nint WinGetWindowLongPtrW(nint hwnd, int index);

- [DllImport("user32", EntryPoint="SetWindowLongPtrW")]static extern nint WinSetWindowLongPtrW(nint hwnd, int index, nint newValue);

- [DllImport("user32", EntryPoint="GetDlgCtrlID")]static extern int WinGetDlgCtrlID(nint hwnd);

- [DllImport("user32", EntryPoint="FindWindowExW")]static extern nint WinFindWindowExW(nint parent, nint after, nint cls, nint title);

- [DllImport("user32", EntryPoint="SendMessageTimeoutW")]static extern nint WinSendMessageTimeoutW(nint hwnd, int msg, nint wp, nint lp, int flags, int timeout, nint resultOut);

- [DllImport("user32", EntryPoint="SendMessageW")]static extern long WinSendMessageW(nint hwnd, int msg, long wp, long lp);

- [DllImport("user32", EntryPoint="PostMessageW")]static extern int WinPostMessageW(nint hwnd, int msg, long wp, long lp);

- [DllImport("user32", EntryPoint="PostThreadMessageW")]static extern int WinPostThreadMessageW(int tid, int msg, long wp, long lp);

- [DllImport("user32", EntryPoint="ShowWindow")]static extern int WinShowWindow(nint hwnd, int cmd);

- [DllImport("user32", EntryPoint="SetForegroundWindow")]static extern int WinSetForegroundWindow(nint hwnd);

- [DllImport("user32", EntryPoint="GetForegroundWindow")]static extern nint WinGetForegroundWindow();

- [DllImport("user32", EntryPoint="GetDesktopWindow")]static extern nint WinGetDesktopWindow();

- [DllImport("user32", EntryPoint="SetFocus")]static extern nint WinSetFocus(nint hwnd);

- [DllImport("user32", EntryPoint="EnableWindow")]static extern int WinEnableWindow(nint hwnd, int enable);

- [DllImport("user32", EntryPoint="SetWindowPos")]static extern int WinSetWindowPos(nint hwnd, nint after, int x, int y, int cx, int cy, int flags);

- [DllImport("user32", EntryPoint="MoveWindow")]static extern int WinMoveWindow(nint hwnd, int x, int y, int w, int h, int repaint);

- [DllImport("user32", EntryPoint="GetWindowRect")]static extern int WinGetWindowRect(nint hwnd, nint rect);

- [DllImport("user32", EntryPoint="GetClientRect")]static extern int WinGetClientRect(nint hwnd, nint rect);

- [DllImport("user32", EntryPoint="SystemParametersInfoW")]static extern int WinSystemParametersInfoW(int action, int param, nint data, int fwinini);

- [DllImport("user32", EntryPoint="ClientToScreen")]static extern int WinClientToScreen(nint hwnd, nint pt);

- [DllImport("user32", EntryPoint="ScreenToClient")]static extern int WinScreenToClient(nint hwnd, nint pt);

- [DllImport("user32", EntryPoint="WindowFromPoint")]static extern nint WinWindowFromPoint(long pt);

- [DllImport("user32", EntryPoint="ChildWindowFromPointEx")]static extern nint WinChildWindowFromPointEx(nint parent, long pt, int flags);

- [DllImport("user32", EntryPoint="MapWindowPoints")]static extern int WinMapWindowPoints(nint from, nint to, nint pts, int count);

- [DllImport("user32", EntryPoint="GetGUIThreadInfo")]static extern int WinGetGUIThreadInfo(int tid, nint info);

- [DllImport("user32", EntryPoint="FlashWindowEx")]static extern int WinFlashWindowEx(nint fwi);

- [DllImport("user32", EntryPoint="GetMenu")]static extern nint WinGetMenu(nint hwnd);

- [DllImport("user32", EntryPoint="GetSubMenu")]static extern nint WinGetSubMenu(nint hMenu, int pos);

- [DllImport("user32", EntryPoint="GetMenuItemCount")]static extern int WinGetMenuItemCount(nint hMenu);

- [DllImport("user32", EntryPoint="GetMenuItemID")]static extern int WinGetMenuItemID(nint hMenu, int pos);

- [DllImport("user32", EntryPoint="GetMenuStringW")]static extern int WinGetMenuStringW(nint hMenu, int item, nint buf, int max, int flags);

- static List<nint> enumBuf=new List<nint>();

- static nint dwmModule=0;

- static DwmGetAttrFn dwmGetAttr;

- static string GetText(nint hwnd)
  - 窗口标题/文本。跨进程安全:通过 SendMessageTimeout 读取
    (WM_GETTEXT),目标无响应时返回 "" 而不是卡死调用方。

- static bool SetText(nint hwnd, string text)
  - 设置窗口标题/文本(WM_SETTEXT)。返回是否成功;目标无响应时
    返回 false 而不是卡死。

- static string GetClassName(nint hwnd)
  - 窗口类名(GetClassNameW)。

- static int GetId(nint hwnd)
  - 控件 ID(GetDlgCtrlID);非子控件返回 0。

- static bool IsWindow(nint hwnd)
  - 句柄是否仍是一个有效窗口。

- static bool IsVisible(nint hwnd)
  - 窗口是否可见(WS_VISIBLE,不含 Cloaked 判断)。

- static bool IsReallyVisible(nint hwnd)
  - 窗口是否真正可见:可见且未被 DWM Cloaked 隐藏(UWP 后台
    标签页等)。

- static bool IsCloaked(nint hwnd)
  - DWM 是否把窗口标记为 Cloaked(存在但不可见,如 UWP 后台页)。
    Dwmapi 缺失时返回 false。

- static bool IsEnabled(nint hwnd)
  - 窗口是否启用(未置 WS_DISABLED)。

- static bool IsHung(nint hwnd, int timeoutMs)
  - 窗口是否失去响应:IsHungAppWindow 或 WM_NULL 在超时内
    未被处理。

- static bool IsMinimized(nint hwnd)
  - 窗口是否最小化(IsIconic)。

- static bool IsMaximized(nint hwnd)
  - 窗口是否最大化(IsZoomed)。

- static bool IsUnicode(nint hwnd)
  - 窗口是否 Unicode 窗口(IsWindowUnicode)。

- static bool IsChild(nint parent, nint hwnd)
  - hwnd 是否 parent 的(直接或间接)子窗口。

- static int GetStyle(nint hwnd)
  - 窗口样式 GetWindowLongPtr(GWL_STYLE)。

- static int GetStyleEx(nint hwnd)
  - 扩展样式 GetWindowLongPtr(GWL_EXSTYLE)。

- static bool HasStyle(nint hwnd, int bits)
  - 样式位是否全部置位(GetStyle & bits == bits)。

- static bool HasStyleEx(nint hwnd, int bits)
  - 扩展样式位是否全部置位。

- static bool ModifyStyle(nint hwnd, int remove, int add)
  - 增删样式位(GWL_STYLE)并应用 SWP_FRAMECHANGED。

- static bool ModifyStyleEx(nint hwnd, int remove, int add)
  - 增删扩展样式位(GWL_EXSTYLE)。

- static nint GetParent(nint hwnd)
  - 直接父窗口(GetParent)。

- static nint GetRoot(nint hwnd)
  - 根窗口(最上层祖先,GetAncestor GA_ROOT)。

- static nint GetRootOwner(nint hwnd)
  - 根所有者窗口(GA_ROOTOWNER)。

- static nint GetOwner(nint hwnd)
  - 窗口所有者(GetWindow GW_OWNER,即 owner 而非 parent)。

- static WindowThreadInfo GetThreadProcess(nint hwnd)
  - 窗口所属线程与进程 ID。

- static int GetThreadId(nint hwnd)
  - 窗口所属线程 ID。

- static int GetProcessId(nint hwnd)
  - 窗口所属进程 ID。

- static bool Match(nint hwnd, string cls, string title, int id)
  - 句柄是否满足条件:类名/标题为子串匹配(空串不限),ID 大于 0
    时须相等。

- static List<nint> EnumTopLevel()
  - 枚举所有顶层窗口(Z 序)。

- static List<nint> EnumChildren(nint parent, bool recursive)
  - 深度枚举 parent 的所有下级窗口(含直接与间接子窗口)。
    `recursive` 为 false 时只列直接子窗口。顺序:先子后父(后序),
    与 aardio winex.enum 一致。

- static void CollectChildren(nint parent)
  - 后序收集 parent 的全部子孙窗口(子先于父,左先于右)。

- static nint FindWindow(string cls, string title, int pid, int tid)
  - 查找第一个满足条件的顶层窗口(类名+标题子串匹配,可限定
    进程/线程 ID)。未找到返回 0。

- static nint FindWindowByTitle(string title)
  - 按标题查找顶层窗口(子串匹配)。

- static nint FindWindowByClass(string cls)
  - 按类名查找顶层窗口(子串匹配)。

- static nint FindEx(nint parent, string cls, string title, int id, int index)
  - 在 parent 的后代窗口中查找第 `索引`(0 起)个满足条件的
    窗口(后序遍历,子先于父)。未找到返回 0。

- static List<nint> FindAll(string cls, string title, int pid, int tid)
  - 查找所有满足条件的顶层窗口。

- static nint WaitForWindow(string cls, string title, int pid, int tid, int timeoutMs)
  - 轮询等待满足条件的顶层窗口出现,每 100ms 一次,超时返回 0。

- static nint WaitForChild(nint parent, string cls, string title, int id, int timeoutMs)
  - 轮询等待 parent 的后代中出现满足条件的子窗口。

- static bool WaitForClose(nint hwnd, int timeoutMs)
  - 等待窗口销毁(句柄失效),超时返回 false。

- static bool WaitForVisible(nint hwnd, bool visible, int timeoutMs)
  - 等待窗口可见/隐藏(WS_VISIBLE),超时返回 false。

- static bool WaitForEnabled(nint hwnd, bool enabled, int timeoutMs)
  - 等待窗口启用/禁用,超时返回 false。

- static nint FromPoint(int x, int y)
  - 取屏幕上坐标 (x, y) 处的窗口(WindowFromPoint)。

- static nint FromClientPoint(nint parent, int x, int y, int flags)
  - 取 parent 客户区 (x, y) 处的子窗口(ChildWindowFromPointEx)。
    flags: 0 全部, 1 跳过不可见, 2 跳过禁用, 4 跳过透明。

- static bool Show(nint hwnd, int cmd)
  - ShowWindow。常用命令:0=隐藏,1=正常,2=最小化,3=最大化,
    6=最小化,9=还原。返回句柄有效即成功(ShowWindow 的返回值是
    "之前是否可见",对调用方没有意义)。

- static bool ShowNormal(nint hwnd)
  - 显示窗口并还原到正常大小。

- static bool Minimize(nint hwnd)
  - 最小化窗口。

- static bool Maximize(nint hwnd)
  - 最大化窗口。

- static bool Restore(nint hwnd)
  - 还原最小化/最大化的窗口。

- static bool Hide(nint hwnd)
  - 隐藏窗口。

- static bool SetForeground(nint hwnd)
  - 把窗口带到前台(SetForegroundWindow)。前台锁限制下可能
    失败,返回系统调用结果。

- static bool SetTopmost(nint hwnd, bool top)
  - 置顶/取消置顶(HWND_TOPMOST)。

- static bool Close(nint hwnd)
  - 发送 WM_CLOSE 请求关闭窗口。

- static bool Quit(nint hwnd)
  - 向窗口所属线程投递 WM_QUIT(要求线程退出消息循环)。
    注意:这会让整个线程的消息循环退出,不只关闭该窗口。

- static bool Flash(nint hwnd, int count, int timeoutMs)
  - 闪烁窗口标题栏吸引注意(FlashWindowEx)。数量=0 表示闪烁
    到窗口被激活为止。

- static bool Enable(nint hwnd, bool enable)
  - 启用/禁用窗口(EnableWindow)。返回句柄有效即成功
    (EnableWindow 的返回值是"之前是否启用")。

- static WindowRect GetRect(nint hwnd)
  - 窗口屏幕坐标(含边框)。

- static WindowRect GetClientRect(nint hwnd)
  - 窗口客户区大小(相对窗口原点,一般 x=y=0)。

- static WindowRect GetWorkArea(nint hwnd)
  - 窗口所在显示器的工作区(不含任务栏)。

- static bool Move(nint hwnd, int x, int y, int w, int h, bool repaint)
  - 移动/调整窗口(MoveWindow)。

- static bool Resize(nint hwnd, int w, int h)
  - 只调整大小,位置不变。

- static bool CenterOnScreen(nint hwnd)
  - 把窗口移动到其显示器工作区中央(位置变化,大小不变)。

- static WindowPoint ToClient(nint hwnd, int x, int y)
  - 屏幕坐标 → 客户区坐标(ScreenToClient)。

- static WindowPoint ToScreen(nint hwnd, int x, int y)
  - 客户区坐标 → 屏幕坐标(ClientToScreen)。

- static WindowRect ClientRectToScreen(nint hwnd, int x, int y, int w, int h)
  - 客户区矩形 → 屏幕矩形(MapWindowPoints)。

- static nint GetForeground()
  - 当前前台窗口。

- static nint GetDesktop()
  - 桌面窗口。

- static nint GetFocus(nint hwnd)
  - hwnd 所在线程当前拥有焦点的窗口(GetGUIThreadInfo)。
    hwnd 为 0 时查前台窗口的线程。

- static bool Click(nint hwnd)
  - 点击按钮控件(投递 BM_CLICK)。

- static bool ClickCommand(nint hwnd, int cmdId)
  - 向窗口发送 WM_COMMAND 命令(点击菜单项、按钮 ID 等)。

- static nint GetMenu(nint hwnd)
  - 窗口主菜单句柄(GetMenu)。

- static int FindMenuItem(nint hMenu, string label)
  - 在菜单中按文本(子串,忽略 "&")查找项,返回其位置;找不到
    返回 -1。`标签` 不含 "&" 时两边都去掉 "&" 再比。

- static int WalkMenu(nint hMenu, List<string> path, int level)
  - 沿菜单路径 `路径`(从 level 起)逐层下钻,返回叶项的命令
    ID;任何一层找不到返回 -1。子菜单用 GetSubMenu 进入。

- static bool ClickMenu(nint hwnd, string path)
  - 点击菜单命令,路径用 "/" 分隔,如 "文件/打开"。按文本
    (子串,忽略 "&")逐层查找后投递 WM_COMMAND。找不到返回 false。

- static long SendMessage(nint hwnd, int msg, long wp, long lp)
  - 同步发送消息(SendMessageW),等待目标处理完成。

- static bool PostMessage(nint hwnd, int msg, long wp, long lp)
  - 异步投递消息(PostMessageW)。


## WindowPoint (class)

屏幕坐标或客户区坐标中的一个点。

- public int x;

- public int y;

- WindowPoint(int x, int y)


## WindowRect (class)

屏幕坐标或客户区坐标中的窗口矩形。

- public int x;

- public int y;

- public int width;

- public int height;

- WindowRect(int x, int y, int w, int h)

- int Right()

- int Bottom()

- bool Contains(int px, int py)


## WindowThreadInfo (class)

窗口所属的线程与进程。

- public int tid;

- public int pid;

- WindowThreadInfo(int tid, int pid)


## int (delegate)

AccessibleObjectFromWindow — MSAA 入口,按窗口取无障碍对象。

`delegate int AccFromWindowFn(nint hwnd, int dwId, nint riid, nint outAcc);`


## int (delegate)

AccessibleObjectFromPoint — 按屏幕坐标取无障碍对象。POINT 按值传
(x 低 32 位、y 高 32 位)。

`delegate int AccFromPointFn(long pt, nint outAcc, nint outVarChild);`


## int (delegate)

AccessibleChildren — 标准 MSAA 子元素枚举。

`delegate int AccChildrenFn(nint container, int start, int count, nint variants, nint obtained);`


## int (delegate)

WindowFromAccessibleObject — 从窗口代理还原 HWND。

`delegate int WindowFromAccFn(nint accessible, nint outHwnd);`


## int (delegate)

Win32 EnumWindows/EnumChildWindows 回调（WINAPI 调用
约定）。返回 0 停止枚举。

`delegate int WndEnumProc(nint hwnd, nint lparam);`


## int (delegate)

DwmGetWindowAttribute 延迟解析，这样即使 DWM 被禁用，模块
也仍能正常工作。

`delegate int DwmGetAttrFn(nint hwnd, int attr, nint outBuf, int size);`


## nint (delegate)

SysAllocString — 创建 COM BSTR。

`delegate nint SysAllocStringFn(nint wide);`


## void (delegate)

SysFreeString — 释放 MSAA 返回的 BSTR。

`delegate void SysFreeStringFn(nint bstr);`
