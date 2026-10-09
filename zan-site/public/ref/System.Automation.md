# System.Automation

> 源码: `packages/Zan.Desktop/src/System/Automation/UiElement.zan`, `packages/Zan.Desktop/src/System/Automation/Window.zan`


## UiElement (class)

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

- static nint ChildVariant(int id)

- UiElement(nint acc, int childId)

- static UiElement FromWindow(nint hwnd)

- static UiElement FromPoint(int x, int y)

- static void Shutdown()

- void Dispose()

- bool IsValid()

- string Name()

- string Value()

- string Description()

- int Role()

- int State()

- bool HasState(int bits)

- string DefaultAction()

- string KeyboardShortcut()

- WindowRect Rect()

- int ChildCount()

- List<UiElement> Children()

- UiElement Parent()

- UiElement HitTest(int x, int y)

- List<UiElement> FindAll(string name, int role)

- UiElement FindFirst(string name, int role)

- static void Walk(UiElement el, string name, int role, int depth, List<UiElement> outList)

- bool Invoke()

- bool Select()

- bool Focus()

- bool SetValue(string v)

- static int RoleTitleBar()

- static int RoleMenuBar()

- static int RoleMenuPopup()

- static int RoleMenuItem()

- static int RoleAlert()

- static int RoleWindow()

- static int RoleClient()

- static int RoleApplication()

- static int RoleDocument()

- static int RolePane()

- static int RoleDialog()

- static int RoleGrouping()

- static int RoleTable()

- static int RoleCell()

- static int RoleLink()

- static int RoleList()

- static int RoleListItem()

- static int RoleOutline()

- static int RoleOutlineItem()

- static int RolePageTab()

- static int RoleGraphic()

- static int RoleStaticText()

- static int RoleText()

- static int RolePushButton()

- static int RoleCheckButton()

- static int RoleRadioButton()

- static int RoleComboBox()

- static int RoleDropList()

- static int RoleProgressBar()

- static int RoleSlider()

- static int RoleSpinButton()

- static int RolePageTabList()

- static int RoleSplitButton()

- static string RoleName(int role)

- static int StateUnavailable()

- static int StateSelected()

- static int StateFocused()

- static int StatePressed()

- static int StateChecked()

- static int StateReadOnly()

- static int StateExpanded()

- static int StateCollapsed()

- static int StateBusy()

- static int StateInvisible()

- static int StateOffscreen()

- static int StateFocusable()

- static int StateSelectable()


## Window (class)

- [DllImport("kernel32", EntryPoint="Sleep")]static extern void SleepW(int ms);

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

- static bool SetText(nint hwnd, string text)

- static string GetClassName(nint hwnd)

- static int GetId(nint hwnd)

- static bool IsWindow(nint hwnd)

- static bool IsVisible(nint hwnd)

- static bool IsReallyVisible(nint hwnd)

- static bool IsCloaked(nint hwnd)

- static bool IsEnabled(nint hwnd)

- static bool IsHung(nint hwnd, int timeoutMs)

- static bool IsMinimized(nint hwnd)

- static bool IsMaximized(nint hwnd)

- static bool IsUnicode(nint hwnd)

- static bool IsChild(nint parent, nint hwnd)

- static int GetStyle(nint hwnd)

- static int GetStyleEx(nint hwnd)

- static bool HasStyle(nint hwnd, int bits)

- static bool HasStyleEx(nint hwnd, int bits)

- static bool ModifyStyle(nint hwnd, int remove, int add)

- static bool ModifyStyleEx(nint hwnd, int remove, int add)

- static nint GetParent(nint hwnd)

- static nint GetRoot(nint hwnd)

- static nint GetRootOwner(nint hwnd)

- static nint GetOwner(nint hwnd)

- static WindowThreadInfo GetThreadProcess(nint hwnd)

- static int GetThreadId(nint hwnd)

- static int GetProcessId(nint hwnd)

- static bool Match(nint hwnd, string cls, string title, int id)

- static List<nint> EnumTopLevel()

- static List<nint> EnumChildren(nint parent, bool recursive)

- static void CollectChildren(nint parent)

- static nint FindWindow(string cls, string title, int pid, int tid)

- static nint FindWindowByTitle(string title)

- static nint FindWindowByClass(string cls)

- static nint FindEx(nint parent, string cls, string title, int id, int index)

- static List<nint> FindAll(string cls, string title, int pid, int tid)

- static nint WaitForWindow(string cls, string title, int pid, int tid, int timeoutMs)

- static nint WaitForChild(nint parent, string cls, string title, int id, int timeoutMs)

- static bool WaitForClose(nint hwnd, int timeoutMs)

- static bool WaitForVisible(nint hwnd, bool visible, int timeoutMs)

- static bool WaitForEnabled(nint hwnd, bool enabled, int timeoutMs)

- static nint FromPoint(int x, int y)

- static nint FromClientPoint(nint parent, int x, int y, int flags)

- static bool Show(nint hwnd, int cmd)

- static bool ShowNormal(nint hwnd)

- static bool Minimize(nint hwnd)

- static bool Maximize(nint hwnd)

- static bool Restore(nint hwnd)

- static bool Hide(nint hwnd)

- static bool SetForeground(nint hwnd)

- static bool SetTopmost(nint hwnd, bool top)

- static bool Close(nint hwnd)

- static bool Quit(nint hwnd)

- static bool Flash(nint hwnd, int count, int timeoutMs)

- static bool Enable(nint hwnd, bool enable)

- static WindowRect GetRect(nint hwnd)

- static WindowRect GetClientRect(nint hwnd)

- static WindowRect GetWorkArea(nint hwnd)

- static bool Move(nint hwnd, int x, int y, int w, int h, bool repaint)

- static bool Resize(nint hwnd, int w, int h)

- static bool CenterOnScreen(nint hwnd)

- static WindowPoint ToClient(nint hwnd, int x, int y)

- static WindowPoint ToScreen(nint hwnd, int x, int y)

- static WindowRect ClientRectToScreen(nint hwnd, int x, int y, int w, int h)

- static nint GetForeground()

- static nint GetDesktop()

- static nint GetFocus(nint hwnd)

- static bool Click(nint hwnd)

- static bool ClickCommand(nint hwnd, int cmdId)

- static nint GetMenu(nint hwnd)

- static int FindMenuItem(nint hMenu, string label)

- static int WalkMenu(nint hMenu, List<string> path, int level)

- static bool ClickMenu(nint hwnd, string path)

- static long SendMessage(nint hwnd, int msg, long wp, long lp)

- static bool PostMessage(nint hwnd, int msg, long wp, long lp)


## WindowPoint (class)

- public int x;

- public int y;

- WindowPoint(int x, int y)


## WindowRect (class)

- public int x;

- public int y;

- public int width;

- public int height;

- WindowRect(int x, int y, int w, int h)

- int Right()

- int Bottom()

- bool Contains(int px, int py)


## WindowThreadInfo (class)

- public int tid;

- public int pid;

- WindowThreadInfo(int tid, int pid)


## int (delegate)

`delegate int AccFromWindowFn(nint hwnd, int dwId, nint riid, nint outAcc);`


## int (delegate)

`delegate int AccFromPointFn(long pt, nint outAcc, nint outVarChild);`


## int (delegate)

`delegate int AccChildrenFn(nint container, int start, int count, nint variants, nint obtained);`


## int (delegate)

`delegate int WindowFromAccFn(nint accessible, nint outHwnd);`


## int (delegate)

`delegate int WndEnumProc(nint hwnd, nint lparam);`


## int (delegate)

`delegate int DwmGetAttrFn(nint hwnd, int attr, nint outBuf, int size);`


## nint (delegate)

`delegate nint SysAllocStringFn(nint wide);`


## void (delegate)

`delegate void SysFreeStringFn(nint bstr);`
