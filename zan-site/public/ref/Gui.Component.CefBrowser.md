# Gui.Component.CefBrowser

> 源码: `stdlib/Gui/Component/CefBrowser/CefBackend.zan`, `stdlib/Gui/Component/CefBrowser/CefBootstrap.zan`, `stdlib/Gui/Component/CefBrowser/CefBrowser.zan`, `stdlib/Gui/Component/CefBrowser/CefBrowserBox.zan`, `stdlib/Gui/Component/CefBrowser/CefCdp.zan`, `stdlib/Gui/Component/CefBrowser/CefCookies.zan`, `stdlib/Gui/Component/CefBrowser/CefFingerprint.zan`, `stdlib/Gui/Component/CefBrowser/CefHost.zan`, `stdlib/Gui/Component/CefBrowser/CefOptions.zan`, `stdlib/Gui/Component/CefBrowser/CefPage.zan`, `stdlib/Gui/Component/CefBrowser/CefRuntime.zan`


## CefArchive (class)

CEF 官方构建索引里的一个归档条目。

- public string cefVersion;
  - 完整 CEF 版本，如
    "109.1.18+gf1c41e4+chromium-109.0.5414.120"。

- public string chromiumVersion;
  - 对应的 Chromium 版本。

- public string platform;
  - 官方平台标识，如 "windows64"。

- public string fileName;
  - 归档文件名（.tar.bz2）。

- public string sha1;
  - 官方给出的 SHA-1（小写十六进制）。

- public long size;
  - 归档字节数。

- CefArchive()

- string Path()
  - 该归档的请求路径（"+" 已按 URL 规则转义）。


## CefBackend (class)

原生 zan_cef driver 的扁平句柄 API。

和 WebViewBackend 不同，这里不能用 [DllImport]：CEF 的 C 结构体布局
按版本绑定，同一份 driver 只能配一个 CEF 分支，而分支是运行时按系统
决定的（旧 Windows → 109，其余 → 最新 stable），所以 driver 本身也要
在运行时挑：zan_cef（最新）/ zan_cef109（109 变体），用 Interop
（LoadLibrary/dlopen）解析，缺失时 IsAvailable() 为 false 而不是加载
失败——与 System.Scripting.Python 对待可选原生依赖的方式一致。

driver 自己再 dlopen CefRuntime 装好的 libcef，并在调用任何版本相关
结构体前校验 CEF API hash，因此“driver 变体和运行时对不上”是一个
明确的错误而不是内存踩踏。

- static nint mod;

- static bool resolved;

- static string lastError="";

- static string libName="";

- static nint addr_lastError;

- static nint addr_ready;

- static nint addr_executeProcess;

- static nint addr_init;

- static nint addr_work;

- static nint addr_shutdown;

- static nint addr_create;

- static nint addr_close;

- static nint addr_alive;

- static nint addr_window;

- static nint addr_setBounds;

- static nint addr_setVisible;

- static nint addr_setClip;

- static nint addr_setFocus;

- static nint addr_setPopupPolicy;

- static nint addr_takePopupUrl;

- static nint addr_setZoom;

- static nint addr_getZoom;

- static nint addr_find;

- static nint addr_stopFind;

- static nint addr_print;

- static nint addr_showDevTools;

- static nint addr_closeDevTools;

- static nint addr_navigate;

- static nint addr_back;

- static nint addr_forward;

- static nint addr_reload;

- static nint addr_stop;

- static nint addr_canGoBack;

- static nint addr_canGoForward;

- static nint addr_isLoading;

- static nint addr_navSeq;

- static nint addr_lastStatus;

- static nint addr_lastErrorCode;

- static nint addr_url;

- static nint addr_title;

- static nint addr_executeJs;

- static nint addr_cdpSend;

- static nint addr_cdpPending;

- static nint addr_cdpDropped;

- static nint addr_cdpAttached;

- static nint addr_cdpTake;

- static bool IsAvailable()
  - 能加载到匹配当前运行时分支的 zan_cef driver 时为 true。

- static string Error()
  - 最近一次失败的原因（加载/解析/原生侧），成功为 ""。

- static string LibraryName()
  - 已加载的 driver 文件名（未加载为 ""）。

- static bool Resolve(string cefVersion)
  - 加载 driver 并解析全部入口点。<paramref name="cefVersion"/>
    是运行时的 CEF 版本（""=未知，按最新变体处理），用来在 109 与最新
    两个变体之间挑选。幂等。

- static List<string> Candidates(string cefVersion)
  - 按平台命名规则和 CEF 分支列出候选 driver 路径：先按
    ZAN_CEF_DRIVER 指定的绝对路径，再是可执行文件同目录（--publish
    会把 driver 拷到这里），最后交给系统加载器按名字找。

- static string FileName(string cefVersion)
  - 当前平台上该 CEF 分支对应的 driver 文件名。

- static string Extract(string file)
  - 把随 exe 内嵌的 driver 写到 CEF 缓存目录并返回其路径；
    没有内嵌副本或写入失败时返回 ""。
    
    资源名自带构建指纹（"zan-drivers/<fp>/zan_cef.dll"，zanc 内嵌
    时按字节算出），落盘目录沿用同一指纹：换了一份 exe 就是另一
    个目录，因此无论缓存里还留着哪些旧 wrapper（旧的可能正被另一
    个进程占着、覆盖不掉），都不会被加载到。

- static string Fingerprint(string name)

- static bool ResolveSymbols()

- static string missing="";

- static nint Sym(string name)

- static int ExecuteProcess(string runtimeDir, string cefVersion, string switches)
  - 本进程是 Chromium helper（render/gpu/utility）时跑完并返回
    其退出码；是浏览器进程时返回 -1，调用方继续往下走。

- static bool Init(string runtimeDir, string cefVersion, string cachePath, string helperPath, string locale, string switches, bool windowless)
  - 初始化 CEF 浏览器进程（driver 变体按 CEF 版本选）。

- static bool Ready()

- static void Work()
  - 推一轮 CEF 消息循环（每帧从宿主 UI 线程调用）。

- static void Shutdown()

- static int Create(nint parent, int x, int y, int w, int h, string url)

- static void Close(int h)

- static bool Alive(int h)

- static nint Window(int h)
  - 浏览器的原生窗口句柄（HWND / X11 Window），无则 0。

- static void SetBounds(int h, int x, int y, int w, int hh)

- static void SetVisible(int h, bool visible)

- static void ApplyClip(int h, string spec)
  - 把本帧未被遮挡的区域下发给原生窗口。签名匹配
    Gui.NativeClipFn，由 CefBrowser.Render 登记给 App，帧末统一回调
    （委托是纯函数指针，所以这里是静态方法）。

- static void SetFocus(int h, bool focus)

- static void Navigate(int h, string url)

- static void Back(int h)

- static void Forward(int h)

- static void Stop(int h)

- static void Reload(int h, bool ignoreCache)

- static bool CanGoBack(int h)

- static bool CanGoForward(int h)

- static bool IsLoading(int h)

- static int NavSeq(int h)

- static int LastStatus(int h)

- static int LastErrorCode(int h)
  - 最近一次加载失败的 CEF 错误码（成功为 0）。

- static string GetUrl(int h)

- static string GetTitle(int h)

- static void ExecuteJs(int h, string code, string scriptUrl, int startLine)
  - 在主框架里执行 JavaScript（无返回值；要取结果走 CDP 的
    Runtime.evaluate）。

- static int CdpSend(int h, string method, string paramsJson)
  - 发一条 CDP 命令，返回其消息 id（失败 0）。

- static int CdpPending(int h)

- static int CdpDropped(int h)
  - 因为 Zan 侧没有及时取走而被丢掉的消息数（队列有界）。

- static bool CdpAttached(int h)

- static string CdpTake(int h)
  - 取走一条 CDP 消息（结果或事件的原始 JSON），空队列为 ""。

- static void SetPopupPolicy(int h, int policy)
  - window.open / target=_blank 的处理方式：0 = CEF 自己开弹窗
    窗口（默认）、1 = 拦掉、2 = 拦掉并把目标 URL 交给宿主。

- static string TakePopupUrl(int h)
  - policy=2 下待处理的弹窗目标 URL，没有则 ""。

- static void SetZoom(int h, double level)
  - Chromium 的缩放级：0 = 100%，每 +1 乘 1.2。

- static double GetZoom(int h)

- static void Find(int h, string text, bool forward, bool matchCase, bool findNext)

- static void StopFind(int h, bool clearSelection)

- static void Print(int h)

- static void ShowDevTools(int h)

- static void CloseDevTools(int h)

- static void CallVoid(nint addr, int h)

- static int CallInt(nint addr, int h)

- static string CallStr(nint addr, int h)


## CefBootstrap (class)

CEF 的启动入口：把「运行时自举（`CefRuntime`）」和「进程
初始化（`CefHost`）」接在一起，再加上 Chromium helper 子进程
的分发。

Chromium 是多进程的：除浏览器进程外还要有 render/gpu/utility 子进程。
默认让程序把自己当 helper 重新执行（browser_subprocess_path = 本程序），
因此 **Main 的第一件事** 必须是让 helper 分支跑完并退出：


async int Main() {
CefBootstrap.RunHelper();              // helper 进程在这里就退出了
if (!await CefBootstrap.StartAsync()) { // 首次会下载官方 CEF 运行时
Console.WriteLine(CefHost.Error());
return 1;
}
App app = new App("browser", 1280, 800);
CefBrowserBox box = new CefBrowserBox();
box.SetStartUrl("https://example.com");
app.root.Add(box);
app.Show();                            // 每帧由 CefBrowser.Render 推循环
CefHost.Shutdown();
return 0;
}


想避免「主程序被当 helper 再跑一遍」，用
`StartAsyncWithHelper` 指定一个独立的 helper 可执行文件。

- [DllImport("crt", EntryPoint="exit")]static extern void CrtExit(int code);

- static void RunHelper()
  - 本进程若是 Chromium helper（render/gpu/utility），跑完它的
    消息循环并直接退出进程（不返回）；是浏览器进程时什么都不做。
    必须是 Main 的第一句：Chromium 用同一个可执行文件启动子进程，
    helper 分支不能走到宿主程序自己的逻辑里。

- static string LocalRuntimeDir()
  - 不联网地找出本机已装好的运行时目录（helper 进程用；
    `CefOptions.runtimeDir` / ZAN_CEF_RUNTIME 可显式指定）。

- static async bool StartAsync()
  - 确保本机有 CEF 运行时（首次会按系统版本从官方 CDN 下载并
    校验，几百 MB），然后初始化浏览器进程。已启动时直接返回 true；
    首次需要运行时下载时默认自动显示独立的
    `DownloadDialog` 进度窗。无 GUI 宿主可通过
    `CefOptions.downloadUi` 关闭它，窗口创建失败也会回退到
    静默自举路径；失败原因见 `CefHost.Error`。

- static async bool StartAsyncWith(CefOptions options)
  - 同 `StartAsync`，但先应用一份
    `CefOptions`（运行时/缓存/profile/driver 路径、Chromium
    开关、语言、helper）。helper 子进程也要按同一份配置找运行时，
    因此应用自己拿不到默认位置时，`CefOptions.Use` 要在
    `RunHelper` 之前调，而不是只依赖这个重载。

- static async bool StartAsyncWithHelper(string helperPath)
  - 同 `StartAsync`，但用 <paramref name="helperPath"/>
    作为 Chromium 子进程的可执行文件（""=复用本程序，此时 Main 首句必须
    调用 `RunHelper`）。

- static DownloadJob NewRuntimeJob()
  - 带进度界面的自举：把运行时下载做成一个通用
    `DownloadJob`，宿主用 `DownloadDialog` 显示进度，
    任务收工后再调 `FinishStart` 起浏览器进程。已经装好时任务
    什么都不下、直接以“已完成”收工，所以这条路可以无条件走：
    
    
    DownloadJob job = CefBootstrap.NewRuntimeJob();
    job.Start();
    DownloadDialog dlg = DownloadDialog.Show(app, "正在准备浏览器运行时", job);
    // …任务 Succeeded() 之后：
    CefBootstrap.FinishStart("");
    
    
    和 `StartAsync` 的分工：这条路给界面已经在跑的宿主用，
    而 `StartAsync` 在主窗口创建前也会自动采用独立进度窗。
    首次下载几百 MB 时窗口照旧响应，关掉窗口也不打断下载；无 GUI 宿主
    可关闭 `CefOptions.downloadUi` 或依赖启动失败时的静默
    回退。

- static bool FinishStart(string helperPath)
  - `NewRuntimeJob` 的任务成功后初始化浏览器进程；
    失败原因见 `CefHost.Error`。

- static string Progress(string archiveFileName)
  - 当前下载进度（"已收/总计" 字节），未在下载时为 ""。
    首次启动要拉几百 MB，宿主可以用它做进度显示。


## CefBrowser (class)

内嵌的 Chromium 浏览器（CEF）视图。

和 Gui.Component.WebView 的分工：WebView 用系统自带引擎（Windows
WebView2 / macOS WKWebView），装机即用但能力受系统摆布；CefBrowser 自带
Chromium——完整的 H.264/AAC 媒体栈、统一的行为、CDP（Chrome DevTools
Protocol）自动化通道，代价是首次运行要下载官方 CEF 运行时
（`CefRuntime` 负责，按系统挑分支：Win7/8/8.1 用 109，
其余用最新 stable）。两者独立并存，按需选用。

原生浏览器窗口是浮在软件渲染表面之上的兄弟图层，因此要由所属区域
每帧驱动它（和 WebView 一致）：
CefBrowser web = new CefBrowser();
web.NavComplete += () => { ... };
web.Navigate("https://example.com");
// 每帧，针对可见标签：
web.Render(app, x, y, w, h);
// 每帧，针对隐藏标签：
web.Hide();

进程级初始化在 `CefBootstrap`：Main 首句
CefBootstrap.RunHelper()、之后 await CefBootstrap.StartAsync()。没启动成功时
IsSupported() 为 false，Render 画一个画布内占位符（说明原因），而不是崩掉。

- [DllImport("user32", EntryPoint="SetFocus")]static extern nint Win32SetFocus(nint hwnd);
  - 原生浏览器是宿主窗口的子 HWND，被点中时 Win32 键盘焦点就落在它上面，
    此后所有按键都进 Chromium——宿主窗口再也收不到 WM_CHAR，界面上的
    输入框（浏览器的地址栏就是一个）便无法输入。焦点交回宿主窗口要走
    Win32，CEF 自己的 set_focus 只管引擎内部。

- [DllImport("user32", EntryPoint="GetFocus")]static extern nint Win32GetFocus();

- int handle;

- bool created;

- bool supported;

- int lastNavSeq;

- string pendingUrl;

- bool guiHadFocus;
  - 上一帧界面上是否有控件持有键盘焦点（用于只在切换的那一刻搬焦点，
    而不是每帧从浏览器手里抢，否则页面里就没法打字了）。

- string lastSeenTitle;

- bool lastSeenLoading;

- CefCdp cdpRouter;
  - CDP 路由/Cookie 门面，第一次被要到时才建（没人用就没有额外开销）。

- int popupPolicy;
  - 弹窗策略与宿主回调（policy 2 时每帧从原生侧取待处理的目标 URL）。

- CefPopupFn popupSink;

- CefCookies cookieApi;

- CefPage pageApi;

- CefFingerprint fpApi;

- SignalString url;

- SignalString title;

- UiEvent NavComplete;
  - 导航完成（或失败）时触发，即当前 URL/标题可能已变化。
    响应式 Url()/Title() 会在其触发前更新。

- UiEvent NavStart;
  - 主框架开始加载时触发。

- UiEvent TitleChanged;
  - 文档标题变化时触发，包括页面内的更新。

- UiEvent LoadingChanged;
  - 加载开始或结束时触发（IsLoading() 指示是哪种）。

- CefBrowser()

- bool IsSupported()
  - 原生浏览器可用（CEF 已启动且这个视图创建成功）时为 true。
    首帧之前它是乐观的 true——创建发生在第一次 Render。

- int Handle()
  - 原生句柄（0 = 尚未创建）。

- SignalString Url()
  - 响应式当前 URL/文档标题（每次导航后更新）。

- SignalString Title()

- string CurrentUrl()

- string CurrentTitle()

- void EnsureCreated(App app, int x, int y, int w, int h)

- void Navigate(string target)
  - 导航到 URL。无协议的裸主机名视为 https://。

- void LoadHtml(string html)
  - 加载内存中的 HTML 字符串（走 data: URL，因此适合中小片段）。

- void Back()

- void Forward()

- void Stop()

- void Reload()

- void ReloadIgnoreCache()
  - 忽略缓存的强制刷新（Ctrl+F5）。

- bool CanGoBack()

- bool CanGoForward()

- bool IsLoading()

- int LastStatus()
  - 最近一次主框架响应的 HTTP 状态码（无响应或非 HTTP 时为 0）。

- int LastErrorCode()
  - 最近一次加载失败的 CEF 错误码（成功为 0）。

- void ExecuteJs(string code)
  - 在页面里执行 JavaScript（不取返回值）。要拿结果用
    `Evaluate`（走 CDP）。

- void SetFocus(bool focus)
  - 键盘焦点交给/收回浏览器。

- void SyncFocus(App app)
  - 每帧跟随界面焦点：界面上有控件拿到键盘焦点（点了地址栏）的那一刻，
    把 Win32 焦点从浏览器子窗口搬回宿主窗口；焦点被清掉（Esc / 点空白）
    的那一刻交还给浏览器。只在切换沿动作，页面内的点击因此照旧把焦点
    拿回去，网页里能正常打字。
    另外，界面持有焦点期间每帧确认 Win32 焦点确实还在宿主窗口上：
    Chromium 会自己把焦点抢回去（导航提交、页面调 focus()、子进程重启），
    只在切换沿搬一次的话，地址栏看着还在编辑态、按键却全进了页面——这正是
    “地址栏时好时坏”的成因。原生侧同时拒掉 CEF 主动要焦点的请求
    （cef_focus_handler_t::on_set_focus），这里是兜底。

- int SendCdp(string method, string paramsJson)
  - 发一条 CDP 命令，返回其消息 id（失败 0）。回复和事件从
    `TakeCdpMessage` 取，因此调用是异步的：
    int id = web.SendCdp("Page.captureScreenshot", "{}");
    // 之后的帧里
    string msg = web.TakeCdpMessage();

- int Evaluate(string expression)
  - 用 CDP 求值一个 JavaScript 表达式，返回其消息 id：结果会以
    Runtime.evaluate 的回复出现在 `TakeCdpMessage` 里。

- int CdpPending()
  - 队列里待取的 CDP 消息数。

- int CdpDropped()
  - 因为迟迟没被取走而丢掉的 CDP 消息数（队列有界，防止跑飞的页面
    事件把内存吃光）。

- bool CdpAttached()
  - CDP 通道已挂上（DevTools agent 已附着）。

- void SetZoom(double level)
  - 缩放级别：0 = 100%，每 +1 乘 1.2（Ctrl+滚轮的那一档），可为负。

- double Zoom()

- void ZoomIn()

- void ZoomOut()

- void ResetZoom()

- void Find(string text, bool forward, bool matchCase, bool findNext)
  - 页内查找并高亮。第一次调用 findNext 传 false（开一次新搜索），之后
    传 true 在同一批命中之间前后跳；`StopFind` 收尾。

- void StopFind(bool clearSelection)

- void Print()
  - 打开系统打印对话框。要无对话框地导出 PDF 用
    `Cdp().Send("Page.printToPDF", ...)`（回调里拿到 base64 的字节）。

- void ShowDevTools()
  - DevTools 开/关（CEF 自己的窗口）。

- void CloseDevTools()

- void AllowPopupWindows()
  - 弹窗（window.open / target=_blank / 中键点击）交给 CEF 自己开一个
    独立的浏览器窗口——默认行为。

- void BlockPopups()
  - 一律不开弹窗（页面里的 window.open 返回 null）。

- void OnPopup(CefPopupFn handler)
  - 弹窗不再自己开窗口，改为把目标 URL 交给宿主：标签式外壳在这里
    新建一个标签，单页外壳可以直接 `Navigate`。回调在
    `Render` 里（UI 线程）被调用。

- void SetPopupPolicy(int policy, CefPopupFn handler)

- string TakeCdpMessage()
  - 取走一条 CDP 消息（命令回复或事件的原始 JSON），空队列为 ""。

- CefCdp Cdp()
  - 按事件名订阅 CDP 事件 / 给命令挂回调的路由（首次调用时建）。
    拿到它之后，`Render` 每帧把消息队列交给它分派，
    此时不要再直接用 `TakeCdpMessage`。

- CefCookies Cookies()
  - Cookie 读写（走 CDP，第一次调用时建）。

- CefPage Page()
  - 页面级钩子：下载、JS 对话框、console、网络（走 CDP）。

- CefFingerprint Fingerprint()
  - 指纹套用：UA/Client Hints、语言、时区、屏幕、WebGL、canvas 噪声。
    填好字段后调 `CefFingerprint.Apply`；注入的补丁对下一个
    文档生效，所以对已经打开的页要再 `Reload` 一次。

- void Hide()
  - 隐藏原生窗口（对不在屏幕上的标签/面板调用）。

- void Destroy()
  - 关闭浏览器并释放原生资源。
    
    CEF 的关闭是异步的：close_browser 之后还要靠消息循环转几圈才真正
    销毁原生窗口，而这个视图从此不再 Render，没人再推循环——只 Close
    就撒手的话，关掉的标签留下的 Chromium 子窗口会一直挂在宿主窗口上
    盖住其他控件。因此原生侧先把窗口隐藏（立刻不再挡人），这里再当场
    把循环推到它消失为止（有上限，别让病态页面把 UI 线程卡死；没推完
    的收尾由后续帧或 CefHost.Shutdown 兜底）。

- int Render(App app, int x, int y, int w, int h)
  - 把原生浏览器放到给定的客户区矩形里，推一轮 CEF 消息循环，然后
    轮询引擎的变化，更新 Url()/Title() 并触发
    NavStart / LoadingChanged / TitleChanged / NavComplete。浏览器跑在
    自己的进程/线程上，因此它的事件以这种每帧差异的形式到达 UI。
    CEF 不可用时画一个占位符（说明原因）。

- static void PaintPlaceholder(App app, int x, int y, int w, int h)

- static string PlaceholderText()

- static string Normalize(string target)

- static bool HasScheme(string s)

- static string UrlEscape(string s)
  - data: URL 里必须转义的字符（其余按 UTF-8 原样传，Chromium 接受）。

- static string JsonString(string s)
  - 把字符串包成 JSON 字面量（CDP 参数拼装用）。


## CefBrowserBox (class)

可摆放的 Chromium 浏览器控件：把原生 CefBrowser 包成一个普通的保留式
Control，因此设计器 / .zform 里的 CEF 浏览器和别的控件一样，由布局给它
一块矩形、由它自己负责绘制（与 WebViewBox 同构，只是引擎换成自带的
Chromium）。

CefBrowserBox box = new CefBrowserBox();
box.SetStartUrl("https://example.com");
tabs.Page(0).Add(box);              // 标签页里放一个浏览器
box.View().NavComplete += () => { ... };

可见时按自己的已解析边界 Render，落在隐藏的标签页 / 折叠容器里时整棵
子树不参与渲染，由 NativeLayer 兜底把没登记的原生层裁空（即隐藏）。

- CefBrowser view;
  - 本控件拥有的原生浏览器（每个 CefBrowserBox 一个 Chromium 视图，
    因此多标签浏览器就是多个 CefBrowserBox）。

- string startUrl;
  - 设计期填写的起始地址，首帧导航一次。

- bool navigated;

- CefBrowserBox()

- CefBrowser View()
  - 底层原生浏览器：历史、JS、CDP、事件都在它上面。

- void SetStartUrl(string u)
  - 起始地址（设计属性 `url`，.zform 里也可写成 `placeholder`）。
    首帧之前设置只改起始地址；之后设置等同于导航。

- string StartUrl()

- void Navigate(string target)

- void Back()

- void Forward()

- void Reload()

- void Stop()

- bool CanGoBack()

- bool CanGoForward()

- bool IsLoading()

- int LastStatus()

- string CurrentUrl()

- string CurrentTitle()

- void Hide()
  - 隐藏原生浏览器（本控件不再上屏时调用；隐藏的标签页由 NativeLayer
    自动兜底，这里供宿主显式收起）。

- void Destroy()
  - 关闭原生浏览器（关闭标签页时调用），并把控件从树上摘下。

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)
  - 浏览器的语义事件挂在原生视图上，设计里的 `onNavComplete` 之类
    因此直接落到它的 UiEvent，宿主不必自己接线。

- override void OnPaint(App app)


## CefCdp (class)

CDP（Chrome DevTools Protocol）消息路由：把
`CefBrowser.TakeCdpMessage` 那条「什么都往里塞」的原始队列
分派成「按事件名订阅」和「命令回复回调」。


CefCdp cdp = web.Cdp();
cdp.Enable("Network");
cdp.On("Network.responseReceived", (json) => { Console.WriteLine(json); });
cdp.Send("Page.captureScreenshot", "{}", (result) => { ... });


队列由 `CefBrowser.Render` 每帧抽干（拿到过
`CefBrowser.Cdp` 的浏览器才会），因此不用自己调
`Pump`；不走 GUI 的宿主自己按节奏调它。

注意：路由挂上之后消息就都归它了，别再直接用
`CefBrowser.TakeCdpMessage` —— 两边会互相抢消息。

- CefBrowser web;

- List<string> names;

- List<CdpEventFn> handlers;

- List<int> ids;

- List<CdpEventFn> replies;

- CdpEventFn anyHandler;

- int dispatched;

- int unrouted;

- CefCdp(CefBrowser browser)

- void On(string eventName, CdpEventFn handler)
  - 订阅一个 CDP 事件（如 `Network.responseReceived`）。可以对同
    一个事件名订阅多次。回调拿到的是整条消息 JSON（含 `params`）。

- void Off(string eventName)
  - 取消某个事件名上的全部订阅。

- void OnAny(CdpEventFn handler)
  - 订阅所有事件（调试/日志用）。

- int Send(string method, string paramsJson, CdpEventFn onResult)
  - 发一条 CDP 命令；<paramref name="onResult"/> 在回复到达时被调
    一次，拿到的是回复里的 `result` JSON（不需要回复传 null）。返回消息 id
    （0 = 浏览器还没就绪）。

- int Enable(string domain)
  - 打开一个 CDP 域（`Network` / `Page` / `Runtime` / `Log` ...）。
    多数域的事件要先 enable 才会来。

- int Disable(string domain)
  - 关闭一个 CDP 域。

- int Dispatched()
  - 已分派的消息数 / 没有任何订阅者接手的消息数（订阅名写错时
    后者会一直涨）。

- int Unrouted()

- void Pump()
  - 抽干队列并分派。幂等，未创建/空队列时是空操作。

- void Dispatch(string msg)

- static string Raw(string json, string key)
  - 顶层键的原始值（字符串带引号、对象/数组原样），没有为 ""。

- static string Field(string json, string key)
  - 顶层字符串键的值（已去引号），没有为 ""。

- static int IntField(string json, string key)
  - 顶层整数键的值，没有/不是数字为 0。

- static int KeyStart(string json, int endQuote)
  - 从引号结束位置回溯到键名的起点（引号之后的第一个字符），
    找不到成对的起始引号返回 -1。

- static int SkipWs(string json, int i)

- static string ValueAt(string json, int start)
  - 从 <paramref name="start"/> 起读一个 JSON 值的原文。

- static string Unescape(string s)


## CefCookies (class)

Cookie 读写，走 CDP（`CefCdp`）。回复是异步到达的，所以每个
查询都带一个回调：


CefCookies ck = web.Cookies();
ck.GetAll((json) => { Console.WriteLine(json); });   // {"cookies":[...]}
ck.Set("token", "abc", ".example.com", "/", true);
ck.Delete("token", ".example.com");


值都是 CDP 的原始 JSON（Network.Cookie 结构），因为 Cookie 的字段还在随
Chromium 演进（SameParty、partitionKey…），包一层强类型只会挡住新字段。
Cookie 存在 profile 目录里（`CefOptions.profileDir`），
由 Chromium 自己落盘，不需要显式 flush。

- CefCdp cdp;

- bool networkOn;

- CefCookies(CefCdp channel)

- void EnsureNetwork()
  - Network 域的 Cookie 命令要先 enable。

- int GetAll(CdpEventFn onResult)
  - 全部 Cookie（浏览器级）。回调收到
    `{"cookies":[{"name":...,"value":...,"domain":...},...]}`。

- int GetForUrl(string url, CdpEventFn onResult)
  - 某些 URL 下可见的 Cookie；<paramref name="urls"/> 为空表示当前
    页面。

- int Set(string name, string value, string domain, string path, bool secure)
  - 按 domain/path 写一个 Cookie（会话 Cookie：不带 expires）。

- int SetPersistent(string name, string value, string domain, string path, bool secure, long expiresUnixSeconds)
  - 按 domain/path 写一个持久 Cookie，
    <paramref name="expiresUnixSeconds"/> 是绝对过期时间（UNIX 秒）。

- int SetForUrl(string url, string name, string value)
  - 按 URL 写一个 Cookie（domain/path 由 URL 推出）。

- int SetJson(string cookieParamJson)
  - 写一个自己拼的 Network.CookieParam JSON（要用上 Chromium 的新
    字段时）。

- int Delete(string name, string domain)
  - 删除匹配 name + domain 的 Cookie。

- int DeleteForUrl(string name, string url)
  - 删除某个 URL 下匹配 name 的 Cookie。

- int Clear()
  - 清空全部 Cookie。

- static string Param(string name, string value, string domain, string path, bool secure, string url, long expires)
  - 拼一条 Network.CookieParam。url 非空时用 url 形式，否则
    domain/path；expires < 0 表示会话 Cookie。


## CefFingerprint (class)

浏览器指纹套用：UA 与 Client Hints 品牌、语言、时区、屏幕与 DPR、
WebGL vendor/renderer、canvas 噪声，一次 `Apply` 全部生效。

分两条路子实现，都建在 `CefCdp` 上：
浏览器自己认的那几项（UA/Client Hints/时区/地区/屏幕/CPU 核数）走
`Emulation` 域的 override —— 页面里读到的 `navigator.userAgent`、
`Intl.DateTimeFormat().resolvedOptions().timeZone`、`screen.width` 全都跟着变，
请求头也跟着变；浏览器不给覆盖的那几项（`navigator.languages`、
`deviceMemory`、`webdriver`、WebGL 型号串、canvas 像素）靠
`Page.addScriptToEvaluateOnNewDocument` 在页面脚本之前打补丁。


CefFingerprint fp = web.Fingerprint();
fp.userAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) ...";
fp.brand = "Google Chrome";  fp.brandVersion = "151";
fp.locale = "zh-CN";  fp.acceptLanguage = "zh-CN,zh;q=0.9";
fp.timezone = "Asia/Shanghai";
fp.screenWidth = 1920;  fp.screenHeight = 1080;  fp.scalePercent = 100;
fp.webglVendor = "Google Inc. (NVIDIA)";
fp.webglRenderer = "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0)";
fp.canvasNoise = 2;
fp.Apply();
web.Reload();          // 注入脚本对「下一个」文档生效，当前页要刷一下


空字段 = 不动那一项，所以只想改 UA 就只填 `userAgent`。
改不了的：TLS/JA3（Chromium 内置栈）、真实 GPU 型号（要换
`--use-angle=`）、系统字体列表（要么注入补丁，要么换 profile 字体目录）。

- CefCdp cdp;

- string userAgent;
  - 完整 UA 串；空 = 不改。

- string acceptLanguage;
  - `Accept-Language` 请求头，如 `zh-CN,zh;q=0.9`；空 = 不改。

- string locale;
  - JS 侧地区（`navigator.language`、`Intl` 默认地区），如 `zh-CN`。

- List<string> languages;
  - `navigator.languages` 列表；空表 = 用 `locale`。

- string timezone;
  - IANA 时区名，如 `Asia/Shanghai`；空 = 不改。

- string platform;
  - Client Hints 平台名，如 `Windows`；空 = 不发 Client Hints。

- string platformVersion;
  - Client Hints 平台版本，如 `15.0.0`（Win11）。

- string brand;
  - Client Hints 品牌，如 `Google Chrome`。

- string brandVersion;
  - 品牌主版本号，如 `151`。

- string brandFullVersion;
  - 品牌完整版本号，如 `151.0.7922.138`；空 = 用主版本号。

- string architecture;
  - CPU 架构：`x86` / `arm`；空 = `x86`。

- string model;
  - 机型（移动端才有意义）。

- bool mobile;
  - 移动标志（`navigator.userAgentData.mobile`、`Sec-CH-UA-Mobile`）。

- int screenWidth;
  - 屏幕宽（CSS 像素）；0 = 不改屏幕。

- int screenHeight;
  - 屏幕高（CSS 像素）。

- int scalePercent;
  - 设备像素比 × 100（100 = 1.0，150 = 1.5）；0 = 用 100。

- int hardwareConcurrency;
  - `navigator.hardwareConcurrency`；0 = 不改。

- int deviceMemory;
  - `navigator.deviceMemory`（GB，2 的幂）；0 = 不改。

- bool hideWebdriver;
  - 把 `navigator.webdriver` 抹成 false。

- string webglVendor;
  - WebGL `UNMASKED_VENDOR_WEBGL` / `VENDOR`；空 = 不改。

- string webglRenderer;
  - WebGL `UNMASKED_RENDERER_WEBGL` / `RENDERER`；空 = 不改。

- int canvasNoise;
  - canvas 噪声幅度（每像素红色通道 ±n，1~8 足够打散哈希）；
    0 = 不加噪声。加了之后 `toDataURL`/`toBlob`/`getImageData` 拿到的像素每次
    都不同，代价是画布内容真的被改了一点。

- int seed;
  - 噪声随机种子；0 = 用固定种子（同一指纹每次运行一致）。

- string scriptId;
  - 已注入脚本的标识（`Reset` 用来撤掉它）。

- string originalUa;
  - 覆盖之前浏览器自己的 UA：CDP 没有「清除 UA 覆盖」这个命令，
    只能把原值再设回去，所以第一次 `Apply` 时先问一句
    `Browser.getVersion` 记下来。

- CefFingerprint(CefCdp channel)

- void UseWindowsChrome(string version)
  - 一份桌面 Windows Chrome 的常见取值：调完再按需改字段。
    <paramref name="version"/> 是 Chrome 完整版本号（如
    `151.0.7922.138`），拿 `CefHost.ChromeVersion` 最省事。

- void Apply()
  - 把当前字段套到浏览器上。`Emulation` 那几项立即生效，注入脚本
    对下一个文档生效（当前页要 `CefBrowser.Reload`）。

- void Reset()
  - 撤掉 `Apply` 装上的东西（屏幕/UA/时区/地区 override
    与注入脚本）。已经加载的页面同样要刷一下才回原样。

- void SendUserAgent()

- string Metadata()
  - Client Hints 元数据：品牌列表按 Chromium 的做法带上
    `Chromium` 与 `Not.A/Brand` 两个同版本条目，只报一个品牌反而扎眼。

- string Script()
  - 要注入的补丁脚本（不需要补任何东西时是 ""）。补丁都在页面
    脚本之前跑，且每个新文档都跑一遍（iframe 也算）。

- string LanguagesJs()

- string WebglJs()

- string CanvasJs()

- static string Ratio(int percent)
  - 百分数转小数串：150 → `1.5`，100 → `1`，225 → `2.25`。
    （拼 JSON 要的是字面量，浮点格式化的小数点在别的地区会变成逗号。）

- static string Major(string version)
  - 取版本号的主版本段：`151.0.7922.138` → `151`。


## CefHost (class)

CEF 的进程级生命周期：浏览器进程初始化、每帧消息循环、退出时 shutdown。
一个进程只有一份 CEF 上下文，所以这些都是静态的，多个
`CefBrowser` 共享它。

这里刻意只依赖 Interop（driver 的 dlopen）——运行时的探测/下载/解包在
`CefBootstrap`，它才会牵进 HTTP/TLS/解压那一整套。控件树
（ControlFactory → CefBrowserBox）只经过本类，所以「随便一个 GUI 程序
都被迫链上一个 HTTP 栈」不会发生。

正常用法是让 `CefBootstrap.StartAsync` 顺带把运行时装好；
已经自备一份 CEF 运行时目录时可以直接
`Start`（完全不联网）。

- static bool started;

- static bool stopped;

- static string runtimeDir="";

- static string cefVersion="";

- static string lastError="";

- static string profileDir="";

- static long profileLock=0;

- static bool IsRunning()
  - CEF 已初始化且未 shutdown。

- static string Error()
  - 最近一次失败的原因（driver / CEF 初始化 / 运行时自举），
    成功为 ""。

- static void Fail(string msg)
  - 记下失败原因（供 CefBootstrap 汇报运行时自举的失败）。

- static string RuntimeDir()
  - 已使用的运行时目录（未启动为 ""）。

- static string CefVersion()
  - 已使用的 CEF 版本（未启动为 ""）。

- static string ChromeVersion()
  - 运行时里 Chromium 的版本号（如 `151.0.7922.138`）：CEF 版本串
    形如 `151.3.18+gbeff58d+chromium-151.0.7922.138`，取 `chromium-` 之后那
    段。拿不到为 ""。UA/Client Hints 要报的是这个版本号，不是 CEF 的。

- static string ReadyMarker()
  - 就绪标记文件名（内容为已安装的 CEF 版本）。

- static string MarkerVersion(string dir)
  - 读运行时目录里的就绪标记，得到其 CEF 版本（读不到为 ""）。
    driver 变体（109 / 最新）按它来挑。

- static int MaxProfiles()
  - 能同时开的实例数（profile 槽位上限）。

- static string ProfileLockName()
  - profile 目录里的占用标记文件名：谁锁住它，谁就在用这个
    profile。带 zan- 前缀而不叫光秃的 lockfile，Chromium 自己也往
    user-data-dir 里写同名文件，抢同一个名字会把它的文件锁掉。

- static string ProfileRoot(string dir)
  - profile 槽位的公共父目录：运行时装在 `<缓存>/Zan/cef/`
    下，登录态跟着版本目录走没意义，所以放同级的
    `<缓存>/Zan/Cache/`。

- static string ProfileDir(string dir)
  - 浏览器进程的 CEF profile 目录（Cookie/缓存/localStorage），
    形如 `<缓存>/Zan/Cache/profile1`。
    `CefOptions.profileDir` / ZAN_CEF_PROFILE 指定了就照用。
    
    一个 Chromium profile 同时只能属于一个活进程：第二份副本指向同一个
    目录，`cef_initialize` 直接失败。桌面工具被开两份是常态，所以按槽位
    分配——第一份 `profile1`，同时开的第二份 `profile2`，依此类推；编号
    而不是按 pid，重启后还能找回自己那份登录态。
    
    占用判定是 profile 目录里 <c>lockfile</c> 的系统级独占锁
    （`File.TryLock`）：锁随持有进程退出（含被杀、崩溃）由内核
    释放，所以 `profile2` 一空出来，下一次启动就接着用 `profile2`，既不会
    一直往上加号，也没有需要清理的陈旧标记。

- static string Switches()
  - 追加到每个 Chromium 进程命令行的开关，取自
    `CefOptions.switches` / ZAN_CEF_SWITCHES
    （`a=b,c` 形式，如 `disable-gpu,disable-gpu-compositing`）。远程桌面 /
    无显卡的机器上 GPU 进程可能反复崩（stderr 里的 “GPU process exited
    unexpectedly”），首屏要等它超时退化，用它关掉 GPU 即可。

- static string DefaultHelper()
  - 没有显式指定时 Chromium 子进程用的可执行文件：应用包里的
    helper 包优先，否则复用本程序（此时 Main 首句必须是
    `CefBootstrap.RunHelper`）。
    
    macOS 上复用 .app 里的主程序会让每个 render/gpu/utility 子进程都按主
    包的 Info.plist 起来，于是一次启动在 Dock 上显示成好几个 app；发布出来的
    包带独立 helper 包（Contents/Frameworks/<名> Helper.app，标了
    LSUIElement），有它就走它。

- static string MacHelperFor(string exe)
  - `<X>.app/Contents/MacOS/<X>` 对应的 helper 包可执行
    文件；不在应用包里或没打 helper（IDE 里直接运行的裸可执行文件）
    时为 ""。

- static string ParentDir(string path)

- static bool Start(string dir, string helperPath)
  - 用已经装好的运行时目录初始化 CEF（不联网）。
    <paramref name="helperPath"/> 是 Chromium 子进程用的可执行文件，
    ""=复用本程序，此时 Main 首句必须调用
    `CefBootstrap.RunHelper`。

- static void Work()
  - 推一轮 CEF 消息循环。由 `CefBrowser.Render` 每帧
    调用，宿主也可以在自己的循环里调用（幂等、未启动时是空操作）。

- static void Shutdown()
  - 关闭所有浏览器并 shutdown CEF（进程退出前调用一次）。


## CefHttpEndpoint (class)

镜像基址解析结果。

- string host;

- int port;

- bool tls;

- string prefix;

- CefHttpEndpoint()


## CefOptions (class)

CEF 的路径与启动配置，代码级入口。

每一项都有环境变量对应物（运维/调试时不改代码就能改），但走
`CefBootstrap.StartAsync` / 控件树（CefBrowserBox）时进程内
传不进任何路径——只能靠环境变量，这个类就是补上那一半：


CefOptions o = new CefOptions();
o.runtimeDir = "D:/app/cef";      // 自备运行时，完全不联网
o.profileDir = "D:/app/data/web"; // Cookie/缓存/localStorage
o.switches = "disable-gpu";       // 追加 Chromium 开关
CefOptions.Use(o);                 // 之后所有 CEF 调用都按它来
await CefBootstrap.StartAsync();


优先级：显式设过的字段 > 环境变量 > 默认值。空字符串表示「没设」，
因此把某项留空不会把环境变量顶掉。CEF 一个进程只初始化一次，所以
要在 `CefBootstrap.RunHelper` 之前就 `Use`——
helper 子进程也要按同一份配置找运行时。

- string runtimeDir;
  - 已装好的 CEF 运行时目录（对应 ZAN_CEF_RUNTIME）。
    设了就不下载、不联网。

- string cacheRoot;
  - 下载/解包缓存根目录（对应 ZAN_CEF_CACHE）。运行时按
    `<root>/<平台>-<版本>` 装在它下面。

- string mirrorBase;
  - CEF 镜像基址（对应 ZAN_CEF_MIRROR）。归档与兜底索引
    都从这里获取，空字符串表示官方地址。

- string archivePath;
  - 本机已下载的 CEF 归档绝对路径（对应
    ZAN_CEF_ARCHIVE）。设了就完全离线安装，不查索引。

- string profileDir;
  - 浏览器 profile 目录：Cookie、HTTP 缓存、localStorage
    （对应 ZAN_CEF_PROFILE）。默认在运行时目录旁边，多数应用想把它放到
    自己的数据目录里。

- string driverPath;
  - zan_cef driver 的文件或所在目录（对应 ZAN_CEF_DRIVER）。
    默认在可执行文件旁边找。

- string switches;
  - 追加到每个 Chromium 进程命令行的开关，`a=b,c` 形式
    （对应 ZAN_CEF_SWITCHES），如 `disable-gpu,disable-gpu-compositing`。
    开关值里本身带逗号时（`disable-features=A,B`）改用分号分隔项：
    `disable-features=A,B;disable-gpu`。列表型开关（*-features）会合并到
    CEF/Chromium 自己那份值里，不会把它们顶掉。
    与环境变量同时存在时两者都生效（先环境变量、后这里）。

- bool disableDirectComposition;
  - 关掉 DirectComposition 呈现（对应 ZAN_CEF_NO_DCOMP=1，
    即 Chromium 的 `--disable-direct-composition`）。
    
    默认跟 Chromium 一样开着。但在装了虚拟显示适配器（向日葵/
    GameViewer 类 IDD 驱动、部分远程桌面）的机器上，DComp 交换链会让
    GPU 子进程在创建命令缓冲区时 CHECK 失败（exit_code=-2147483645
    即 STATUS_BREAKPOINT），连崩 3 次后 Chromium 退到 `--use-gl=disabled`，
    这时整个进程内 WebGL 都建不起来。关掉 DComp 后 GPU 不再崩、
    硬件 GL 照旧走真实显卡（ANGLE/D3D11）。

- string locale;
  - 界面语言（CEF settings.locale，如 `zh-CN`；对应
    ZAN_CEF_LOCALE）。""=Chromium 按系统决定。

- string helperPath;
  - Chromium 子进程用的可执行文件（对应 ZAN_CEF_HELPER）。
    ""=复用本程序，此时 Main 首句必须是
    `CefBootstrap.RunHelper`。

- bool downloadUi;
  - 是否在 CEF 首次自举时显示自动下载进度窗；默认开启。
    设为 false 或设置 ZAN_CEF_DOWNLOAD_UI=0 可用于无 GUI/无显示器
    宿主。

- static CefOptions current;

- CefOptions()

- static void Use(CefOptions options)
  - 让后续所有 CEF 调用使用这份配置。

- static CefOptions Current()
  - 当前生效的配置（没设过时是一份全空的，即纯环境变量/默认
    行为）；就地改它的字段也算生效。

- static string Pick(string configured, string envName)
  - 显式配置优先，其次环境变量，都没有则 ""。

- static string RuntimeDir()

- static string CacheRoot()

- static string MirrorBase()

- static string ArchivePath()

- static string ProfileDir()

- static string DriverPath()

- static string Locale()

- static string HelperPath()

- static bool DownloadUi()
  - 是否启用自举进度窗。配置字段优先于
    ZAN_CEF_DOWNLOAD_UI；因此代码里设为 false 后，环境变量不能重新打开
    它。未关闭配置时，环境变量为 0 才会关闭。

- static string Switches()
  - 两处开关拼在一起（环境变量在前，配置在后，后者覆盖同名
    开关——Chromium 取命令行里最后一个），并把
    `disableDirectComposition` 展开成真正的开关。任一段里出现
    分号时整串改用分号分隔，否则带逗号的开关值会被拆开。

- static bool NoDcomp()

- static string Join(string left, string right)


## CefPage (class)

页面级常用钩子：下载、JS 对话框、console 输出。都建在
`CefCdp` 上（这些能力 CDP 全都有，不需要再加原生 handler），
所以任何 CDP 域/事件都能用 `CefCdp.On` 自己接，这里只是把最常
用的三件事收成一行调用。


CefPage page = web.Page();
page.AllowDownloads("D:/app/downloads", (json) => { ... });
page.AutoDismissDialogs(true);   // alert/confirm 一律确定，避免页面卡住
page.OnConsole((json) => { Console.WriteLine(json); });

- CefCdp cdp;

- bool pageOn;

- bool dialogAccept;

- bool dialogAuto;

- CefPage(CefCdp channel)

- void EnsurePage()
  - Page 域的事件要先 enable。

- void AllowDownloads(string dir, CdpEventFn onEvent)
  - 允许下载并落到 <paramref name="dir"/>；
    <paramref name="onEvent"/> 非空时同时订阅
    `Browser.downloadWillBegin` / `Browser.downloadProgress`（带
    guid/url/文件名/进度/状态）。默认 Chromium 在无 UI 的嵌入场景里是拒绝
    下载的，所以不调这个就不会有文件落盘。

- void DenyDownloads()
  - 拒绝一切下载。

- void CancelDownload(string guid)
  - 取消一个正在进行的下载（guid 来自下载事件）。

- void AutoDismissDialogs(bool accept)
  - 自动应答 alert/confirm/prompt/beforeunload：
    <paramref name="accept"/> 为 true 时点「确定」，否则「取消」。
    嵌入式浏览器里这些对话框默认没人应答，页面会一直停在那儿。

- void AnswerDialog(bool accept, string promptText)
  - 自己应答一个打开着的对话框（配合
    `CefCdp.On` 订阅 `Page.javascriptDialogOpening` 时用）；
    <paramref name="promptText"/> 只对 prompt 有意义。

- void OnConsole(CdpEventFn onEvent)
  - 订阅页面 console：`Runtime.consoleAPICalled`（console.log 一族）
    与 `Log.entryAdded`（浏览器自己的告警，如 CSP、404）。

- void OnException(CdpEventFn onEvent)
  - 订阅未捕获异常（`Runtime.exceptionThrown`）。

- void OnNetwork(CdpEventFn onEvent)
  - 订阅请求/响应（`Network.requestWillBeSent`、
    `Network.responseReceived`、`Network.loadingFailed`）。要改写或拦截请求
    得用 Fetch 域，自己 `CefCdp.On` + `Fetch.continueRequest`。

- void SetUserAgent(string ua)
  - 覆盖 User-Agent（空串还原）。

- void SetExtraHeaders(string headersJson)
  - 附加 HTTP 头（JSON 对象，如
    `{"X-Token":"abc"}`）；`{}` 清空。


## CefRuntime (class)

本地 CEF 运行时的自举：探测平台与应当使用的 CEF 主版本、
优先按内置钉版本表选择归档，必要时从官方或镜像索引兜底、
下载（可断点续传）并按官方 SHA-1 校验归档、
解压到本地缓存并打上就绪标记，最后给出稳定的运行时目录，供原生
zan_cef driver 加载 libcef。

版本策略：Windows 7/8/8.1 只能用 CEF 109（最后一个支持它们的分支），
其他系统用当前绑定的 stable。运行时二进制不入库，只落在用户缓存目录。

用法：

string dir = await CefRuntime.EnsureAsync();
if (dir == "") { Console.WriteLine(CefRuntime.Error()); }

- static string IndexHost()
  - 官方构建索引所在主机。

- static string IndexPath()
  - 索引路径。

- static string ArchiveType()
  - 使用的归档类型：包含 include/、libcef_dll/、Release/、
    Resources/，但不含调试符号与静态库，足够构建
    libcef_dll_wrapper 并运行。

- static string ReadyMarker()
  - 就绪标记文件名，内容为已安装的 CEF 版本（与只依赖
    Interop 的 CefHost 共用同一个名字）。

- static string lastError;

- static string Error()
  - 最近一次失败的原因，成功时为 ""。

- static void Fail(string msg)

- static string Platform()
  - 官方平台标识（"windows64" / "linux64" / "macosarm64" …），
    无法映射时返回 ""。

- static bool LegacyWindows()
  - 当前系统是否只能用 CEF 109：Windows 7 / 8 / 8.1
    （内核 6.0—6.3）。

- static string CurrentBranch()
  - 当前分支的 CEF 主版本前缀。
    
    这里必须钉死而不是「取最新 stable」：CEF 的 C API 结构体布局按版本
    绑定，原生 driver 是针对某个分支的头文件编译的，运行时换成别的分支
    就是内存布局错位（driver 因此还会校验 cef_api_hash 并拒绝加载）。
    升级流程是：改这里的前缀与下面的钉版本表，然后重新编译 driver
    ——cmake/ZanCef.cmake 直接从本文件读这两处来取头文件版本，
    不再另写一份版本号（写两份就会漂成 cef api hash mismatch）。

- static string LegacyBranch()
  - 旧 Windows（7/8/8.1）能用的最后一个 CEF 分支。

- static string VersionPrefix()
  - 需要的版本前缀："109."（旧 Windows）或当前分支。

- static CefArchive Pinned(string version, string chromium, string platform, string sha1, long size)
  - 内置 stable + minimal 归档表。它跟 CurrentBranch() /
    LegacyBranch() 以及原生 driver 的 CEF 版本绑定在一起，升级 CEF
    必须同时修改这张表、分支前缀和 driver。数据来自官方 index.json
    的 stable + minimal 条目。

- static CefArchive FindPinned(string platform, string versionPrefix)
  - 按平台和版本前缀查找内置归档，未命中返回 null。

- static CefArchive FindPinnedExact(string platform, string version)
  - 按平台和完整版本查找内置归档，未命中返回 null。

- static string CacheRoot()
  - 本地缓存根目录（可用 `CefOptions.cacheRoot` /
    ZAN_CEF_CACHE 覆盖）。

- static string DownloadDir()
  - 下载目录（归档与进度文件）。

- static string RuntimeDir(string cefVersion)
  - 某个 CEF 版本解包后的运行时目录。

- static string FrameworkSubdir()
  - 运行时目录里 CEF 框架所在的子路径：Windows/Linux 是
    Release/ 里的 libcef，macOS 是 Release/ 里的 framework 目录（与
    native 侧 CefSettings.framework_dir_path 用的是同一个路径）。

- static string ResourcesDir(string dir)
  - 解包后的目录里资源（.pak/icudtl.dat）所在的位置。官方 mac
    包没有顶层 Resources/，资源在 framework 内部，所以两边的校验路径
    不同。

- static bool IsReady(string dir)
  - 该目录是否已完整安装（存在就绪标记与资源）。

- static string Installed()
  - 本地已安装且符合当前版本策略的运行时目录，没有则 ""。
    不联网。

- static bool CleanCache()
  - 删除整个 CEF 缓存（运行时与已下载归档）。

- static string BaseUrl()
  - 当前 CEF 服务基址。镜像配置会去掉末尾斜杠。

- static string BasePath(string path)
  - 把镜像路径前缀与请求路径拼成单个 HTTP 路径。

- static CefHttpEndpoint Endpoint()
  - 解析镜像的 scheme、主机、端口与路径前缀。

- static string ArchiveUrl(CefArchive a)
  - 归档完整 URL（文件名中的 "+" 继续按 URL 规则转义）。

- static string DownloadEscapeHint()

- static string Sha1EscapeHint()

- static async bool FetchIndexAsync(string destPath)
  - 把官方构建索引下载到 <paramref name="destPath"/>，
    成功返回 true。索引每次都重新拉取（不做断点续传，避免续到
    过期的半截文件上）。

- static CefArchive SelectArchive(string index, string platform, string versionPrefix)
  - 从索引文本里挑出该平台上符合前缀的最新 stable 归档。
    纯函数，便于离线测试。失败返回 null。

- static CefArchive ReadVersion(string text, string platform, string versionPrefix)
  - 解析单个版本对象，符合条件时返回归档，否则 null。

- static bool OtherPlatform(string text, string platform)
  - 版本对象里的文件名是否属于别的平台。

- static string PrefixNote(string versionPrefix)

- static CefArchive ReadLocalArchive(string path)
  - 读取本机归档并校验其平台、版本和已知摘要。

- static async CefArchive ResolveAsync()
  - 联网确定当前系统该用哪个 CEF 归档。优先使用内置表，
    只有新平台或未登记分支才下载索引兜底。

- static async string EnsureAsync()
  - 确保本地有可用的 CEF 运行时，返回其目录；失败返回 ""
    （原因见 `Error`）。已装好时不联网。

- static string pendingDir;
  - 准备阶段选中的目标目录与版本：worker 线程上先写、随后同一个线程上的
    安装步骤再读（DownloadJob 是单任务语义，见其类注释）。

- static string pendingVersion;

- static DownloadJob NewEnsureJob()
  - 把运行时自举做成一个通用下载任务，好让宿主用
    `DownloadDialog` 显示进度：
    
    
    DownloadJob job = CefRuntime.NewEnsureJob();
    job.Start();
    DownloadDialog.Show(app, "正在准备浏览器运行时", job);
    // 任务成功后：CefRuntime.EnsuredDir() 就是运行时目录
    
    
    启动阶段没有主窗口时，`CefBootstrap.StartAsync` 会自动把
    同一个任务交给独立的 `DownloadDialog`；宿主也可以自行
    组合任务与进度窗。
    
    「下哪个归档」仍然由本类决定（优先查内置表、必要时联网拉索引），
    解压/剥顶层目录/打就绪标记也仍然走本类的 <c>Install</c>——任务层只管
    传输、校验与进度。已经装好时任务不下载任何东西，直接以“已完成”收工。

- static bool PrepareJob(DownloadJob job)
  - 任务的准备回调（跑在工作线程上）：确定归档并加条目；
    本机归档模式直接安装，不产生下载条目。

- static bool InstallItem(DownloadItem it)
  - 任务的安装步骤（跑在工作线程上）：CEF 的归档要剥掉顶层
    目录、要打就绪标记，所以不能用任务层按扩展名的默认解压。

- static string EnsuredDir()
  - `NewEnsureJob` 的任务成功收工后，运行时所在
    目录；没装好时 ""。

- static string EnsureJobError(DownloadJob job)
  - 把 DownloadJob 的失败状态翻译成 CEF 自举错误；优先使用
    准备阶段写入的 CefRuntime.Error，下载器没有该上下文时回退到
    DownloadJob.Error。

- static async bool DownloadArchiveAsync(CefArchive a, string archive)
  - 下载归档（断点续传）并按官方 SHA-1 校验；校验不过会
    重下一次。

- static string Progress(string fileName)
  - 下载进度（"已收/总计" 字节），未在下载时返回 ""。

- static bool Install(string archive, string dir, string cefVersion)
  - 解压 .tar.bz2 归档到运行时目录并打上就绪标记：归档里
    只有一层 cef_binary_… 顶层目录，会被剥掉。

- static void StageResources(string dir)
  - 把 Resources/ 的内容复制到 Release/：Chromium 只按 libcef
    所在目录找 icudtl.dat，不看 resources_dir_path，所以运行时的资源
    必须和二进制同目录（macOS 的 framework 布局自带资源，跳过）。

- static void CopyTree(string src, string dst)
  - 递归复制目录内容（已存在的同名文件会被覆盖）。

- static void MarkExecutables(string dir)
  - POSIX 上给 Release/ 下的产物补上可执行位（tar 里的 mode
    已经带了，这里只是兜底 helper 进程能起来）。

- static void MarkExecutablesIn(string dir)
  - 给一个目录里（不递归）看起来是可执行文件/动态库的项补上
    可执行位。

- static string UrlEncode(string name)
  - 把归档文件名转成 URL 路径片段（"+" → "%2B"）。

- static string SafeVersion(string v)
  - 把版本串里不适合做目录名的字符换成 "-"。

- static bool VersionNewer(string a, string b)
  - 比较两个目录名里的版本，a 是否比 b 新（按点分数字段）。

- static int ParseInt(string s)

- static string Slashes(string p)

- static string ParentDir(string path)

- static int PrevBrace(string s, int from)
  - from 之前最近的一个 '{'。

- static int MatchBrace(string s, int open)
  - 与 open 处 '{' 配对的 '}'，考虑字符串字面量与转义。


## double (delegate)

`delegate double CefGetZoomFn(int h);`


## int (delegate)

`delegate int CefIntFn();`


## int (delegate)

`delegate int CefIntStrFn(string s);`


## int (delegate)

`delegate int CefExecuteFn(string runtimeDir, string switches);`


## int (delegate)

`delegate int CefInitFn(string runtimeDir, string cachePath, string helperPath, string locale, string switches, int windowless);`


## int (delegate)

`delegate int CefCreateFn(nint parent, int x, int y, int w, int h, string url);`


## int (delegate)

`delegate int CefIntIntRetFn(int h);`


## int (delegate)

`delegate int CefCdpSendFn(int h, string method, string paramsJson);`


## nint (delegate)

`delegate nint CefNintIntFn(int h);`


## string (delegate)

`delegate string CefStrFn();`


## string (delegate)

`delegate string CefStrIntFn(int h);`


## void (delegate)

`delegate void CefVoidFn();`


## void (delegate)

`delegate void CefVoidIntFn(int h);`


## void (delegate)

`delegate void CefIntIntFn(int h, int v);`


## void (delegate)

`delegate void CefIntStrVoidFn(int h, string s);`


## void (delegate)

`delegate void CefBoundsFn(int h, int x, int y, int w, int hh);`


## void (delegate)

`delegate void CefJsFn(int h, string code, string scriptUrl, int startLine);`


## void (delegate)

`delegate void CefZoomFn(int h, double level);`


## void (delegate)

`delegate void CefFindFn(int h, string text, int forward, int matchCase, int findNext);`


## void (delegate)

弹窗请求交给宿主时的回调（参数是要打开的 URL）。

`delegate void CefPopupFn(string url);`


## void (delegate)

CDP 消息回调：收到的原始 JSON（事件的整条消息，或命令回复的
`result` 部分）。

`delegate void CdpEventFn(string json);`
