# Gui.Component.WebView

> 源码: `packages/Zan.Gui.Browser/src/Gui/Component/WebView/LinkWindow.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/WebView/WebView.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/WebView/WebView2.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/WebView/WebViewBackend.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/WebView/WebViewBootstrap.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/WebView/WebViewBox.zan`


## LinkWindow (class)

`<a href>` 缺省路由的链接窗口（App.OpenLink 经 WebViewBootstrap
注册的导航器懒建，每个 App 全程复用一个）：整棵保留树就是一枚
WebViewBox，根控件直接拿满窗口客户区；页面标题经 TitleChanged
同步到 OS 窗口标题。运行时 WebView 不可用（缺 WebView2/WKWebView
运行时，首次 Render 创建失败）时首帧后自动把这次导航转交系统
浏览器并关窗——链接不能表现为"点了没反应"。
本类随 WebView 家族整体住在 Component/WebView：App 编译图不背
它（auto-stdlib 按 using 整目录拉入），装了 WebView 家族的程序
经 WebViewBootstrap.Install 拿到全部链接路由。

- WebViewBox wv;

- string lastUrl;
  - 最近一次导航目标（WebView 不可用时的系统浏览器回落对象；
    回落完成后清空防重复打开）。

- int idBase;

- LinkWindow()

- override string Title()

- override int Width()

- override int Height()

- override int IdBase()
  - 每个窗口一枚固定的 WidgetId 基线（主窗口不回卷进程级计数，
    子窗口不钉基线则控件 hover/press 每帧错位）。

- void Navigate(string url, App parent)
  - 导航：首次调用建窗口（跟随 parent 的皮肤/标题栏约定），
    之后只 Navigate（已开的窗口再次点链接 = 原地换页）。

- override void AfterFrame()
  - 首帧之后原生视图必然已创建：IsSupported 翻 false 即创建失败
    （平台没有 WebView 运行时），这次导航转交系统浏览器并关窗。

- override void OnCloseRequested()
  - 用户点标题栏 X：先销毁原生视图（每个 WebViewBox 一个浏览器
    实例，不随 Teardown 的窗口/画布销毁自动释放）。


## WebCookie (class)

强类型 Web Cookie 实体对象（完全对齐现代化强类型设计，避免字符串序列化/拆解）。

- public string name;

- public string value;

- public string domain;

- public string path;

- public double expires;

- public bool isHttpOnly;

- public bool isSecure;

- public WebCookie()

- public WebCookie(string n, string v, string d="", string p="/"){ this.name=n;this.value=v;this.domain=d;this.path=p;this.expires=0.0;this.isHttpOnly=false;this.isSecure=false;}


## WebView (class)

内嵌的原生 Web 视图（浏览器控件）。

由平台浏览器引擎（WebViewBackend）支撑：Windows 上是 Edge WebView2
控制器，由 Zan 直接通过其 COM 接口驱动，
macOS 上是 WKWebView，其他平台为占位符（没有原生引擎的后端
返回无句柄，因此 IsSupported() 为 false，Render 绘制
画布内提示而不是嵌入实时浏览器）。

原生视图是浮在软件渲染表面之上的兄弟图层，
因此要由所属区域每帧驱动它：
WebView web = WebView.CreateWithProfile("");
web.NavComplete += () => { ... };   // 加载完成时触发
web.Navigate("https://example.com");
// 每帧，针对可见标签：
web.Render(app, x, y, w, h);
// 每帧，针对隐藏标签：
web.Hide();

暴露常用浏览器功能：历史（Back/Forward/Reload/Stop）、
URL 和标题监控（响应式 Url()/Title() 信号 + NavComplete）、
最近一次请求的 URL 和 HTTP 状态、JavaScript 求值（Eval 同步取结果，
EvalAsync 发射后不管）、JS↔native 消息桥（AddMessageHandler/
TakeMessage，三端页面侧 API 同为 window.webkit.messageHandlers，
Windows 由 shim 对齐）、每次导航的脚本/CSS 注入（InjectScript/
InjectStyle）、整页缩放（SetZoom）、新窗口就地接管（window.open /
target=_blank 不弹窗，落在本视图）、Cookie 与站点数据
（GetCookies/SetCookie/ClearCookies/ClearBrowsingData）以及
DevTools（OpenDevTools）。

- int handle;

- string profileId;

- bool created;

- bool supported;

- int lastNavSeq;

- string pendingUrl;

- string pendingHtml;

- string pendingBase;

- string lastSeenRequest;

- string lastSeenTitle;

- bool lastSeenLoading;

- List<string> pendingHandlers;

- List<string> pendingScripts;

- List<string> pendingStyles;

- bool pendingDevTools;

- bool devToolsEnabled;

- bool devToolsExplicit;

- bool contextMenuEnabled;

- bool contextMenuExplicit;

- SignalString url;

- SignalString title;

- UiEvent NavComplete;
  - 导航完成（或失败）时触发，即当前 URL/标题
    可能已变化。响应式 Url()/Title() 会在其触发前更新。

- UiEvent NavStart;
  - 请求新导航时触发（LastRequest() 即其 URL）。

- UiEvent TitleChanged;
  - 文档标题变化时触发，包括页面内的更新。

- UiEvent LoadingChanged;
  - 加载开始或结束时触发（IsLoading() 指示是哪种）。

- WebView(string profileId)
  - 创建绑定到隔离配置文件的 WebView。共享同一
    非空 profileId 的 WebView 共享 Cookie/localStorage/会话（同一
    账户）；不同 profileId 完全隔离，因此多个账户
    可同时保持登录。空的 profileId 使用共享默认
    存储（旧行为）。配置文件在 Windows（每配置文件一个
    WebView2 用户数据文件夹）和 macOS 上生效；其他平台接受并忽略
    该 id。

- WebView():this("")
  - 使用共享的默认存储创建 WebView。

- static WebView CreateWithProfile(string profileId)
  - 同构造：绑定到指定隔离配置文件的 WebView（"" = 共享默认存储）。

- string ProfileId()
  - 此视图创建时使用的隔离配置文件 id（"" = 共享）。

- void EnsureCreated(App app)
  - 首次 Render 时创建原生视图，并把创建前挂起的内容按序补上：
    消息 handler → 注入脚本 → 注入样式 → 挂起的导航（或 HTML）。
    创建失败则永久回退占位符。

- bool IsSupported()
  - 当前平台有可用的原生 Web 视图时为 true；false 时
    Render 画占位符，其余方法皆为安全空操作。

- SignalString Url()
  - 响应式当前 URL/文档标题（每次导航后更新）；非响应式
    快照用 CurrentUrl/CurrentTitle。

- SignalString Title()
  - 响应式当前文档标题（每次导航后更新）。

- string CurrentUrl()
  - 当前 URL 的字符串形式（非响应式快照）。

- string CurrentTitle()
  - 当前文档标题的字符串形式（非响应式快照）。

- void Navigate(string target)
  - 导航到 URL。无协议的裸主机名视为 https://。原生视图
    创建前调用会把地址挂起，创建时自动消费（后调的 LoadHtml 会
    覆盖挂起的 Navigate，反之亦然）。

- void LoadHtml(string html, string baseUrl)
  - 加载内存中的 HTML 字符串（baseUrl 解析相对链接；可为
    ""）。原生视图创建前调用同样挂起，创建时消费。

- void Back()
  - 历史后退一页（未创建/不支持为空操作）。

- void Forward()
  - 历史前进一页。

- void Reload()
  - 重新加载当前页。

- void Stop()
  - 停止当前加载。

- bool CanGoBack()
  - 能否后退（未创建/不支持为 false）。

- bool CanGoForward()
  - 能否前进（未创建/不支持为 false）。

- bool IsLoading()
  - 是否正在加载（未创建/不支持为 false）。

- int LastStatus()
  - 最近一次响应的 HTTP 状态码（无响应或非 HTTP 时为 0）。

- string LastRequest()
  - 视图最近一次看到的导航请求 URL。

- string Eval(string js)
  - 在页面中运行 JavaScript 并返回字符串化的结果（出错/无结果时为
    同步读取 JS 执行结果（非阻塞，禁止在 GUI 线程跑消息泵）。
    如需异步等待计算结果，请优先使用 `EvalWithResultAsync`。

- async string EvalWithResultAsync(string js, int timeoutMs=5000){ if (!supported||handle==0){ return"";}string res=await WebViewBackend.EvalWithResultAsync(handle, js, timeoutMs);return res;}async bool EnsureReadyAsync(int timeoutMs=15000){ if (!supported||handle==0){ return false;}bool ok=await WebViewBackend.EnsureReadyAsync(handle, timeoutMs);return ok;}bool SupportsMessages(){ return WebViewBackend.HasBridge();}void AddMessageHandler(string handlerName){ if (handlerName==""){ return;}if (!created||!supported||handle==0){ pendingHandlers.Add(handlerName);return;}WebViewBackend.AddHandler(handle, handlerName);}void RemoveMessageHandler(string handlerName){ if (supported&&handle!=0){ WebViewBackend.RemoveHandler(handle, handlerName);}}string TakeMessage(){ if (!supported||handle==0){ return"";}return WebViewBackend.TakeMessage(handle);}int MessagePending(){ if (!supported||handle==0){ return 0;}return WebViewBackend.PendingMessages(handle);}int MessageDropped(){ if (!supported||handle==0){ return 0;}return WebViewBackend.DroppedMessages(handle);}void InjectScript(string js, bool atDocumentEnd){ if (js==""){ return;}if (!created||!supported||handle==0){ string tag="0";if (atDocumentEnd){ tag="1";}pendingScripts.Add(tag+js);return;}WebViewBackend.AddScript(handle, js, atDocumentEnd);}void InjectStyle(string css){ if (css==""){ return;}if (!created||!supported||handle==0){ pendingStyles.Add(css);return;}WebViewBackend.AddStyle(handle, css);}void ClearInjected(){ pendingScripts=new List<string>();pendingStyles=new List<string>();if (supported&&handle!=0){ WebViewBackend.RemoveScripts(handle);}}void EvalAsync(string js){ if (supported&&handle!=0){ WebViewBackend.EvalAsync(handle, js);}}string GetCookies(string forUrl){ if (!supported||handle==0){ return"";}return WebViewBackend.GetCookies(handle, forUrl);}async string GetCookiesAsync(string forUrl, int timeoutMs=5000){ if (!supported||handle==0){ return"";}string res=await WebViewBackend.GetCookiesAsync(handle, forUrl, timeoutMs);return res;}async List<WebCookie> GetCookieListAsync(string forUrl="", int timeoutMs=5000){ if (!supported||handle==0){ return new List<WebCookie>();}List<WebCookie> res=await WebViewBackend.GetCookieListAsync(handle, forUrl, timeoutMs);return res;}async string GetCookieDetailsAsync(string forUrl, int timeoutMs=5000){ if (!supported||handle==0){ return"";}string res=await WebViewBackend.GetCookieDetailsAsync(handle, forUrl, timeoutMs);return res;}void SetCookie(string forUrl, string cookieName, string cookieValue){ if (supported&&handle!=0){ WebViewBackend.SetCookie(handle, forUrl, cookieName, cookieValue);}}void ClearCookies(){ if (supported&&handle!=0){ WebViewBackend.ClearCookies(handle);}}void ClearBrowsingData(){ if (supported&&handle!=0){ WebViewBackend.ClearData(handle);}}void SetZoom(int percent){ if (!supported||handle==0){ return;}if (percent <25){ percent=25;}if (percent> 500){ percent=500;}WebViewBackend.SetZoom(handle, percent);}void OpenDevTools(){ if (!created||handle==0){ pendingDevTools=true;return;}if (supported&&handle!=0){ WebViewBackend.OpenDevTools(handle);}}void SetDevToolsEnabled(bool enabled){ devToolsEnabled=enabled;devToolsExplicit=true;if (created&&supported&&handle!=0){ WebViewBackend.SetDevToolsEnabled(handle, enabled);}}void SetContextMenuEnabled(bool enabled){ contextMenuEnabled=enabled;contextMenuExplicit=true;if (created&&supported&&handle!=0){ WebViewBackend.SetContextMenuEnabled(handle, enabled);}}void Hide(){ if (created&&supported&&handle!=0){ WebViewBackend.SetVisible(handle, false);}}void Destroy(){ if (created&&supported&&handle!=0){ NativeLayer.Forget(handle);WebViewBackend.Destroy(handle);}handle=0;created=false;}int Render(App app, int x, int y, int w, int h){ EnsureCreated(app);if (!supported){ WebView.PaintPlaceholder(app, x, y, w, h);return handle;}WebViewBackend.SetFrame(handle, x, y, w, h);NativeLayer.Register(app, handle, x, y, w, h, WebViewBackend.ApplyClip);string pendingNav=WebViewBackend.DrainWindowNav(handle);if (pendingNav!=""){ WebViewBackend.Navigate(handle, pendingNav);}bool dirty=false;string request=WebViewBackend.LastRequest(handle);if (request!=lastSeenRequest){ lastSeenRequest=request;if (request!=""){ NavStart.Raise();}dirty=true;}bool busy=WebViewBackend.IsLoading(handle);if (busy!=lastSeenLoading){ lastSeenLoading=busy;LoadingChanged.Raise();dirty=true;}int seq=WebViewBackend.NavSeq(handle);if (seq!=lastNavSeq){ lastNavSeq=seq;url.Set(WebViewBackend.GetUrl(handle));title.Set(WebViewBackend.GetTitle(handle));NavComplete.Raise();dirty=true;}string docTitle=title.Get();if (docTitle!=lastSeenTitle){ lastSeenTitle=docTitle;TitleChanged.Raise();dirty=true;}if (dirty){ app.RequestRedraw();}return handle;}static void PaintPlaceholder(App app, int x, int y, int w, int h){ Canvas c=app.canvas;Theme t=app.theme;StyleBox s=Style.Part(app, "webview", "placeholder", "", "", "", Style.SNormal());StyleBox icon=Style.Part(app, "webview", "icon", "", "", "", Style.SNormal());StyleBox label=Style.Part(app, "webview", "label", "", "", "", Style.SNormal());c.FillRect(x, y, w, h, s.BgOr(0));c.DrawRect(x, y, w, h, s.BorderOr(0), t.borderWidth);c.DrawGlyphIn("globe", x, y-app.Scale(28), w, h, t.iconSizeLarge, icon.FgOr(0));c.DrawTextCentered(x, y+app.Scale(18), w, h, "WebView needs a native backend(Windows WebView2 / macOS WKWebView / Android system WebView)", label.FgOr(0), Style.FontFallback(app, "medium"));}static string Normalize(string target){ if (target==""){ return"";}if (WebView.HasScheme(target)){ return target;}return"https://"+target;}static bool HasScheme(string s){ int n=s.Length;for (int i=0;i+2 <n;i=i+1){ if (s[i]==':'&&s[i+1]=='/'&&s[i+2]=='/'){ return true;}}return false;}
  - 异步运行 JavaScript 并等待返回值（协程挂起，零消息泵，不阻塞 GUI 线程）。


## WebView2 (class)

Edge WebView2，由 Zan 直接驱动。

WebView2 是 COM API：除加载器唯一的扁平导出外，其余都是
vtable 分发，其异步调用把结果交给调用方实现的
COM 对象。这两者都可用本语言表达——`Com.Call*`
调用 vtable 槽，`ComVtbl` 用 Zan 方法构建回调对象——
因此该后端不需要任何原生垫片。

实例通过小整数句柄寻址：回调必须是静态的
（它们必须是普通函数地址），每个回调把句柄放在其 COM 对象的
状态字中，事件正是借此找回它的视图。

- [DllImport("user32", EntryPoint="CreateWindowExW")]static extern nint CreateWindowExW(int exStyle, nint cls, nint title, int style, int x, int y, int w, int h, nint parent, nint menu, nint inst, nint param);

- [DllImport("user32", EntryPoint="DestroyWindow")]static extern int DestroyWindow(nint hwnd);

- [DllImport("user32", EntryPoint="SetWindowPos")]static extern int SetWindowPos(nint hwnd, nint after, int x, int y, int w, int h, int flags);

- [DllImport("user32", EntryPoint="ShowWindow")]static extern int ShowWindow(nint hwnd, int cmd);

- [DllImport("user32", EntryPoint="SetWindowRgn")]static extern int SetWindowRgn(nint hwnd, nint rgn, bool redraw);

- [DllImport("gdi32", EntryPoint="CreateRectRgn")]static extern nint CreateRectRgn(int l, int t, int r, int b);

- [DllImport("gdi32", EntryPoint="CombineRgn")]static extern int CombineRgn(nint dst, nint src1, nint src2, int mode);

- [DllImport("gdi32", EntryPoint="DeleteObject")]static extern int DeleteObject(nint obj);

- static nint CreateWindowExW(int exStyle, nint cls, nint title, int style, int x, int y, int w, int h, nint parent, nint menu, nint inst, nint param)

- static int DestroyWindow(nint hwnd)

- static int SetWindowPos(nint hwnd, nint after, int x, int y, int w, int h, int flags)

- static int ShowWindow(nint hwnd, int cmd)

- static int SetWindowRgn(nint hwnd, nint rgn, bool redraw)

- static nint CreateRectRgn(int l, int t, int r, int b)

- static int CombineRgn(nint dst, nint src1, nint src2, int mode)

- static int DeleteObject(nint obj)

- static List<WebView2> views;

- static nint loader;

- static nint createEnv;

- static bool comReady;

- int handle;

- nint host;
  - 本视图独占的宿主子窗口（WebView2 controller 的父窗口）。

- nint env;

- nint ctrl;

- nint core;

- nint envSink;

- nint ctlSink;

- nint navSink;

- nint startSink;

- nint srcSink;

- long navToken;

- long startToken;

- long srcToken;

- long msgToken;

- long winToken;

- int navSeq;

- int lastStatus;

- bool loading;

- string url;

- string title;

- string lastRequest;

- string evalResult;

- string cookieResult;

- string cookieDetailsResult;

- List<WebCookie> cookieListResult;

- List<string> msgQ;

- int msgDrops;

- List<string> handlerNames;

- List<string> shimIds;

- List<string> scriptIds;

- string pendingScriptId;

- string pendingWindowNav;

- nint msgSink;

- nint winSink;

- PumpGate gate;

- bool ready;

- bool initFailed;

- Gate readyGate;

- Gate evalGate;

- Gate cookieGate;

- string pendingInitUrl;

- int fx;

- int fy;

- int fw;

- int fh;

- bool frameSet;

- string clipSpec;

- bool clipSet;

- bool visible;

- bool visibleSet;

- bool devToolsEnabled;

- bool contextMenuEnabled;

- bool devToolsSet;

- bool contextMenuSet;

- static int SlotGetSettings()
  - ICoreWebView2 的 vtable 槽。
    槽 3：get_Settings，读取当前环境设置接口 ICoreWebView2Settings。

- static int SlotGetAreDevToolsEnabled()
  - ICoreWebView2Settings 的 vtable 槽。
    槽 11：get_AreDevToolsEnabled

- static int SlotPutAreDevToolsEnabled()
  - 槽 12：put_AreDevToolsEnabled

- static int SlotGetAreDefaultContextMenusEnabled()
  - 槽 13：get_AreDefaultContextMenusEnabled

- static int SlotPutAreDefaultContextMenusEnabled()
  - 槽 14：put_AreDefaultContextMenusEnabled

- static int SlotGetSource()
  - 槽 4：get_Source，读取当前页面 URI。

- static int SlotNavigate()
  - 槽 5：Navigate，导航到指定 URI。

- static int SlotNavigateToString()
  - 槽 6：NavigateToString，导航到 HTML 字符串。

- static int SlotAddNavigationStarting()
  - 槽 7：add_NavigationStarting，订阅导航开始事件，返回订阅令牌。

- static int SlotRemoveNavigationStarting()
  - 槽 8：remove_NavigationStarting，按令牌退订导航开始事件。

- static int SlotAddSourceChanged()
  - 槽 11：add_SourceChanged，订阅源地址变化事件，返回订阅令牌。

- static int SlotRemoveSourceChanged()
  - 槽 12：remove_SourceChanged，按令牌退订源地址变化事件。

- static int SlotAddNavigationCompleted()
  - 槽 15：add_NavigationCompleted，订阅导航完成事件，返回订阅令牌。

- static int SlotRemoveNavigationCompleted()
  - 槽 16：remove_NavigationCompleted，按令牌退订导航完成事件。

- static int SlotExecuteScript()
  - 槽 29：ExecuteScript，在页面执行 JavaScript，结果经回调异步返回。

- static int SlotReload()
  - 槽 31：Reload，重新加载当前页。

- static int SlotCanGoBack()
  - 槽 38：CanGoBack，查询历史记录能否后退。

- static int SlotCanGoForward()
  - 槽 39：CanGoForward，查询历史记录能否前进。

- static int SlotGoBack()
  - 槽 40：GoBack，后退一条历史记录。

- static int SlotGoForward()
  - 槽 41：GoForward，前进一条历史记录。

- static int SlotStop()
  - 槽 43：Stop，停止当前正在进行的导航。

- static int SlotGetDocumentTitle()
  - 槽 48：get_DocumentTitle，读取页面标题。

- static int SlotGetCookieManager()
  - ICoreWebView2_2 接口虚函数 get_CookieManager（继承 ICoreWebView2，槽位 66）。

- static string IidWebView2_2()
  - ICoreWebView2_2 的接口 IID，QueryInterface 取 Cookie 管理器时使用。

- static int SlotPutIsVisible()
  - ICoreWebView2Controller 的 vtable 槽。
    槽 4：put_IsVisible，设置网页视图是否可见。

- static int SlotPutBounds()
  - 槽 6：put_Bounds，设置渲染区域矩形。

- static int SlotControllerClose()
  - 槽 24：Close，关闭控制器并拆掉网页宿主窗口。

- static int SlotGetCoreWebView2()
  - 槽 25：get_CoreWebView2，取核心 ICoreWebView2 指针。

- static int SlotCreateController()
  - ICoreWebView2Environment 的 vtable 槽。
    槽 3：CreateCoreWebView2Controller，为宿主 HWND 异步创建控制器。

- static int SlotCreateCookie()
  - ICoreWebView2CookieManager 的 vtable 槽。
    槽 3：CreateCookie，按名称/值/域/路径异步构造 Cookie 对象。

- static int SlotGetCookies()
  - 槽 5：GetCookies，按 URI 异步取 Cookie 列表。

- static int SlotAddOrUpdateCookie()
  - 槽 6：AddOrUpdateCookie，把 Cookie 写入容器（不存在则新增）。

- static int SlotDeleteAllCookies()
  - 槽 10：DeleteAllCookies，清空全部 Cookie。

- static int SlotCookieName()
  - ICoreWebView2Cookie / CookieList 的 vtable 槽。
    ICoreWebView2Cookie 槽 3：get_Name，读 Cookie 名。

- static int SlotCookieValue()
  - ICoreWebView2Cookie 槽 4：get_Value，读 Cookie 值。

- static int SlotCookieDomain()
  - ICoreWebView2Cookie 槽 6：get_Domain，读所属域名。

- static int SlotCookiePath()
  - ICoreWebView2Cookie 槽 7：get_Path，读路径。

- static int SlotCookieCount()
  - ICoreWebView2CookieList 槽 3：get_Count，读列表长度。

- static int SlotCookieAt()
  - ICoreWebView2CookieList 槽 4：按下标异步取 Cookie。

- static int SlotIsSuccess()
  - ICoreWebView2NavigationCompletedEventArgs 的 vtable 槽。
    槽 3：get_IsSuccess，导航是否成功完成。

- static int SlotWebErrorStatus()
  - 槽 4：get_WebErrorStatus，读取失败时的错误码。

- static int SlotArgsUri()
  - ICoreWebView2NavigationStartingEventArgs 的 vtable 槽。
    槽 3：get_Uri，读取本次导航的目标 URI。

- static int SlotAddDocScript()
  - ICoreWebView2 的 vtable 槽（27..51 一段，官方 WebView2.h 实数）。
    槽 27：AddScriptToExecuteOnDocumentCreated，注册每次导航（文档任何
    脚本之前）执行的脚本，完成回调回传脚本 id。

- static int SlotRemoveDocScript()
  - 槽 28：RemoveScriptToExecuteOnDocumentCreated，按 id 撤掉注入脚本。

- static int SlotAddWebMessage()
  - 槽 34：add_WebMessageReceived，订阅页面 window.chrome.webview
    .postMessage 事件。

- static int SlotRemoveWebMessage()
  - 槽 35：remove_WebMessageReceived，按令牌退订页面消息事件。

- static int SlotAddNewWindow()
  - 槽 44：add_NewWindowRequested，订阅新开窗口/ target=_blank 请求。

- static int SlotRemoveNewWindow()
  - 槽 45：remove_NewWindowRequested，按令牌退订新开窗口事件。

- static int SlotOpenDevTools()
  - 槽 51：OpenDevToolsWindow，为当前页打开 DevTools 窗口。

- static int SlotPutZoomFactor()
  - ICoreWebView2Controller 的 vtable 槽。
    槽 8：put_ZoomFactor，整页缩放（1.0 = 100%）。

- static int SlotTryGetWebMessage()
  - ICoreWebView2WebMessageReceivedEventArgs 的 vtable 槽。
    槽 5：TryGetWebMessageAsString，取消息正文（字符串原文或 JSON）。

- static int SlotNewWinUri()
  - ICoreWebView2NewWindowRequestedEventArgs 的 vtable 槽。
    槽 3：get_Uri，读取新窗口要加载的目标 URI。

- static int SlotNewWinHandled()
  - 槽 6：put_Handled，声明宿主已接管新窗口请求（阻止弹原生窗口）。

- static string IidWebView2_13()
  - ICoreWebView2_13 的 IID，取 ICoreWebView2Profile 时用。

- static int SlotGetProfile()
  - ICoreWebView2_13 的 vtable 槽 3：get_Profile。

- static string IidProfile2()
  - ICoreWebView2Profile2 的 IID，取 ClearBrowsingData 时用。

- static int SlotClearBrowsingData()
  - ICoreWebView2Profile2 的 vtable 槽 3：ClearBrowsingData(kinds, handler)。

- static int DataKindsAllSite()
  - COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_SITE：Cookie、各类 DOM 存储、
    磁盘缓存与 Service Worker（"退出登录并忘记我"的取值）。

- static List<WebView2> All()
  - 全部存活视图的静态表（惰性创建；下标 0 对应句柄 1）。

- static WebView2 Get(int h)
  - 句柄 1..n 寻址静态视图列表；0 或越界返回 null。

- static bool IsAvailable()
  - 存在 WebView2Loader.dll 和运行时环境时为 true。

- static string RuntimeVersion()
  - 系统已安装的 WebView2 运行时版本号；无加载器或无运行时
    为 ""。注意方向：这是同步方法调用的出参（CoTaskMemAlloc），要
    释放——与完成回调参数"只读"的规矩相反。

- static int Create(nint hwnd, string profileId)
  - 创建以 `hwnd` 为父窗口的视图，按 `profileId` 隔离（共享同一 id 的视图
    共享 Cookie 和存储）。WebView2 缺失或创建失败时返回 0，
    调用方回退到占位符。

- nint Sink(nint invoke)
  - 构建这些 API 所需的四槽 IUnknown+Invoke 回调对象之一，
    并打上本视图句柄的标签。

- static nint CreateHost(nint parent)
  - 本视图的宿主子窗口：一个不做事的 STATIC 子窗口，WebView2
    的可视层就长在它里。多一层宿主主要为了裁剪：子窗口可以用
    SetWindowRgn 按区域露出/遮住，而 put_Bounds 只能给一个矩形；
    同时也让多个标签各自拥有一个能单独寻址的 HWND。
    创建时不带 WS_VISIBLE：第一帧结算出可见区域后才显示。

- bool Start(nint hwnd, string profileId)
  - 启动序列：建宿主子窗口 → 异步发起 Environment 创建流水线（非阻塞，零消息泵）→
    待 OnController 回调触发后由主消息循环自然挂载 core 并应用几何。

- void AttachCore()
  - 在 Controller 回调就绪后挂载事件并应用排队的几何与状态。

- async bool EnsureReadyAsync(int timeoutMs=15000){ if (ready){ return true;}if (initFailed){ return false;}await readyGate.Wait(timeoutMs);return ready;}long Subscribe(int slot, nint sink){ nint token=NativeMemory.Alloc(8);new Span<long>(token, 1)[0]=0;Com.Call2(core, slot, sink, token);long t=new Span<long>(token, 1)[0];NativeMemory.Free(token);return t;}void Unsubscribe(int slot, long token){ nint cell=NativeMemory.Alloc(8);new Span<long>(cell, 1)[0]=token;Com.Call1(core, slot, (nint)token);NativeMemory.Free(cell);}static string ProfileDir(string profileId){ string userEnv=Interop.EnvVar("WEBVIEW2_USER_DATA_FOLDER");if (userEnv!=""){ return userEnv;}string id=profileId;if (id==""){ id="default";}string safe="";for (int i=0;i <id.Length;i=i+1){ char ch=id[i];if (WebView2.IsSafeChar(ch)){ safe=safe+id.Substring(i, 1);}else{ safe=safe+"_";}}string root=Interop.EnvVar("LOCALAPPDATA");if (root==""){ root=Interop.EnvVar("TEMP");}return root+"\\ZanGui\\WebView2\\"+safe;}static bool IsSafeChar(char ch){ if (ch>='a'&&ch <='z'){ return true;}if (ch>='A'&&ch <='Z'){ return true;}if (ch>='0'&&ch <='9'){ return true;}if (ch=='-'||ch=='_'){ return true;}return false;}static int OnQueryInterface(nint self, nint riid, nint ppv){ if (ppv==0){ return Com.Fail();}new Span<long>(ppv, 1)[0]=self;return 0;}static int OnAddRef(nint self){ return 2;}static int OnRelease(nint self){ return 1;}static int OnEnvironment(nint self, int hr, nint result){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (Com.Ok(hr)&&result!=0){ w.env=Com.Keep(result);w.ctlSink=w.Sink((nint)WebView2.OnController);int chr=Com.Call2(w.env, WebView2.SlotCreateController(), w.host, w.ctlSink);if (!Com.Ok(chr)){ w.initFailed=true;w.gate.Signal();w.readyGate.Signal();}}else{ w.initFailed=true;w.gate.Signal();w.readyGate.Signal();}return 0;}static int OnController(nint self, int hr, nint result){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (Com.Ok(hr)&&result!=0){ w.ctrl=Com.Keep(result);w.core=Com.Get(w.ctrl, WebView2.SlotGetCoreWebView2());w.AttachCore();w.ready=true;}else{ w.initFailed=true;}w.gate.Signal();w.readyGate.Signal();return 0;}static int OnNavigationStarting(nint self, nint sender, nint args){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (args!=0){ w.lastRequest=Com.GetString(args, WebView2.SlotArgsUri());}w.loading=true;return 0;}static int OnNavigationCompleted(nint self, nint sender, nint args){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (args!=0){ int ok=Com.GetInt(args, WebView2.SlotIsSuccess());int status=Com.GetInt(args, WebView2.SlotWebErrorStatus());if (ok!=0){ w.lastStatus=200;}else{ w.lastStatus=400+status;}}if (sender!=0){ w.url=Com.GetString(sender, WebView2.SlotGetSource());w.title=Com.GetString(sender, WebView2.SlotGetDocumentTitle());}w.loading=false;w.navSeq=w.navSeq+1;return 0;}static int OnSourceChanged(nint self, nint sender, nint args){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (sender!=0){ w.url=Com.GetString(sender, WebView2.SlotGetSource());}w.navSeq=w.navSeq+1;return 0;}static int OnWebMessageReceived(nint self, nint sender, nint args){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (args!=0){ string line=Com.GetString(args, WebView2.SlotTryGetWebMessage());if (line!=""){ w.PushMessage(line);}}return 0;}static int OnNewWindowRequested(nint self, nint sender, nint args){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (args!=0){ string uri=Com.GetString(args, WebView2.SlotNewWinUri());Com.Call1(args, WebView2.SlotNewWinHandled(), (nint)1);if (uri!=""){ w.pendingWindowNav=uri;}}return 0;}static int OnDocScriptAdded(nint self, int hr, nint id){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (Com.Ok(hr)&&id!=0){ string sid=Wide.Read(id);if (sid!=""){ w.pendingScriptId=sid;w.scriptIds.Add(sid);}}w.gate.Signal();return 0;}static int OnScriptCompleted(nint self, int hr, nint json){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (Com.Ok(hr)){ w.evalResult=Wide.Read(json);}w.gate.Signal();w.evalGate.Signal();return 0;}static int OnCookiesCompleted(nint self, int hr, nint list){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (Com.Ok(hr)&&list!=0){ w.cookieResult=WebView2.Serialize(list);w.cookieDetailsResult=WebView2.SerializeDetails(list);w.cookieListResult=WebView2.ExtractCookies(list);}w.gate.Signal();w.cookieGate.Signal();return 0;}static string Serialize(nint list){ int n=Com.GetInt(list, WebView2.SlotCookieCount());string outp="";for (int i=0;i <n;i=i+1){ nint cell=NativeMemory.Alloc(8);new Span<long>(cell, 1)[0]=0;int hr=Com.Call2(list, WebView2.SlotCookieAt(), i, cell);nint cookie=new Span<long>(cell, 1)[0];NativeMemory.Free(cell);if (Com.Ok(hr)&&cookie!=0){ string name=Com.GetString(cookie, WebView2.SlotCookieName());string val=Com.GetString(cookie, WebView2.SlotCookieValue());if (name!=""){ if (outp!=""){ outp=outp+"; ";}outp=outp+name+"="+val;}Com.Release(cookie);}}return outp;}static string SerializeDetails(nint list){ int n=Com.GetInt(list, WebView2.SlotCookieCount());string outp="";for (int i=0;i <n;i=i+1){ nint cell=NativeMemory.Alloc(8);new Span<long>(cell, 1)[0]=0;int hr=Com.Call2(list, WebView2.SlotCookieAt(), i, cell);nint cookie=new Span<long>(cell, 1)[0];NativeMemory.Free(cell);if (Com.Ok(hr)&&cookie!=0){ string name=Com.GetString(cookie, WebView2.SlotCookieName());string val=Com.GetString(cookie, WebView2.SlotCookieValue());string dom=Com.GetString(cookie, WebView2.SlotCookieDomain());string path=Com.GetString(cookie, WebView2.SlotCookiePath());if (name!=""){ outp=outp+name+"\t"+val+"\t"+dom+"\t"+path+"\n";}Com.Release(cookie);}}return outp;}static List<WebCookie> ExtractCookies(nint list){ List<WebCookie> result=new List<WebCookie>();if (list==0){ return result;}int n=Com.GetInt(list, WebView2.SlotCookieCount());for (int i=0;i <n;i=i+1){ nint cell=NativeMemory.Alloc(8);new Span<long>(cell, 1)[0]=0;int hr=Com.Call2(list, WebView2.SlotCookieAt(), i, cell);nint cookie=new Span<long>(cell, 1)[0];NativeMemory.Free(cell);if (Com.Ok(hr)&&cookie!=0){ string name=Com.GetString(cookie, WebView2.SlotCookieName());string val=Com.GetString(cookie, WebView2.SlotCookieValue());string dom=Com.GetString(cookie, WebView2.SlotCookieDomain());string path=Com.GetString(cookie, WebView2.SlotCookiePath());if (name!=""){ result.Add(new WebCookie(name, val, dom, path));}Com.Release(cookie);}}return result;}void SetFrame(int x, int y, int w, int h){ if (ctrl==0){ return;}if (frameSet&&x==fx&&y==fy&&w==fw&&h==fh){ return;}fx=x;fy=y;fw=w;fh=h;frameSet=true;if (host!=0){ WebView2.SetWindowPos(host, 0, x, y, w, h, 4|16);}nint rect=NativeMemory.Alloc(16);new Span<int>(rect, 1)[0]=0;new Span<int>(rect+4, 1)[0]=0;new Span<int>(rect+8, 1)[0]=w;new Span<int>(rect+12, 1)[0]=h;Com.Call1(ctrl, WebView2.SlotPutBounds(), rect);NativeMemory.Free(rect);clipSet=false;}void SetVisible(bool visible){ if (ctrl==0){ return;}if (visibleSet&&visible==this.visible){ return;}this.visible=visible;visibleSet=true;int v=0;if (visible){ v=1;}Com.Call1(ctrl, WebView2.SlotPutIsVisible(), v);if (host!=0){ int cmd=0;if (visible){ cmd=4;}WebView2.ShowWindow(host, cmd);}}void SetClip(string spec){ if (ctrl==0){ return;}if (spec.Length==0){ this.SetVisible(false);return;}if (clipSet&&spec==clipSpec){ this.SetVisible(true);return;}clipSpec=spec;clipSet=true;if (host!=0){ nint rgn=0;bool whole=false;List<string> parts=spec.Split(";");for (int i=0;i <parts.Count;i=i+1){ List<string> n=parts[i].Split(", ");if (n.Count <4){ continue;}int l=Convert.ToInt32(n[0])-fx;int t=Convert.ToInt32(n[1])-fy;int r=l+Convert.ToInt32(n[2]);int b=t+Convert.ToInt32(n[3]);if (parts.Count==1&&l <=0&&t <=0&&r>=fw&&b>=fh){ whole=true;}nint piece=WebView2.CreateRectRgn(l, t, r, b);if (piece==0){ continue;}if (rgn==0){ rgn=piece;}else{ WebView2.CombineRgn(rgn, rgn, piece, 2);WebView2.DeleteObject(piece);}}if (whole){ if (rgn!=0){ WebView2.DeleteObject(rgn);}WebView2.SetWindowRgn(host, 0, true);}else if (rgn!=0){ WebView2.SetWindowRgn(host, rgn, true);}}this.SetVisible(true);}void Navigate(string target){ if (core==0){ pendingInitUrl=target;return;}nint u=Wide.Of(target);Com.Call1(core, WebView2.SlotNavigate(), u);Wide.Free(u);}void LoadHtml(string html, string baseUrl){ if (core==0){ return;}nint h=Wide.Of(html);Com.Call1(core, WebView2.SlotNavigateToString(), h);Wide.Free(h);}void Back(){ Com.Call0(core, WebView2.SlotGoBack());}void Forward(){ Com.Call0(core, WebView2.SlotGoForward());}void Reload(){ Com.Call0(core, WebView2.SlotReload());}void StopLoading(){ Com.Call0(core, WebView2.SlotStop());}bool CanGoBack(){ return Com.GetInt(core, WebView2.SlotCanGoBack())!=0;}bool CanGoForward(){ return Com.GetInt(core, WebView2.SlotCanGoForward())!=0;}bool IsLoading(){ return loading;}string LastRequest(){ return lastRequest;}int NavSeq(){ return navSeq;}int LastStatus(){ return lastStatus;}string GetUrl(){ if (core!=0){ url=Com.GetString(core, WebView2.SlotGetSource());}return url;}string GetTitle(){ if (core!=0){ title=Com.GetString(core, WebView2.SlotGetDocumentTitle());}return title;}static void IssueOnUi(Action issue){ if (!Dispatcher.Post(issue)){ issue();}}async string EvalWithResultAsync(string js, int timeoutMs=5000){ if (core==0){ return"";}evalResult="";nint sink=this.Sink((nint)WebView2.OnScriptCompleted);evalGate=new Gate();WebView2.IssueOnUi(()=>{ nint script=Wide.Of(js);int hr=Com.Call2(core, WebView2.SlotExecuteScript(), script, sink);Wide.Free(script);if (!Com.Ok(hr)){ Console.WriteLine("[WV2]ExecuteScript hr="+Convert.ToString(hr));evalGate.Signal();}});await evalGate.Wait(timeoutMs);return evalResult;}string Eval(string js){ if (core==0){ return"";}nint sink=this.Sink((nint)WebView2.OnScriptCompleted);nint script=Wide.Of(js);evalResult="";Com.Call2(core, WebView2.SlotExecuteScript(), script, sink);Wide.Free(script);return evalResult;}static nint voidSink;
  - 异步等待 WebView2 完全就绪（协程挂起，零消息泵，不阻塞 GUI 线程）。

- static nint VoidSink()

- static int OnVoidCompleted(nint self, int hr, nint json)
  - 空结果回调（COM）：结果串归引擎所有（见 OnDocScriptAdded 注），
    不释放、无状态。

- void EvalAsync(string js)
  - 发射后不管的 Native→JS：不泵消息等返回值，绘制/事件回调里也
    能安全调用。

- bool AddHandler(string name)
  - 注册消息通道（原生视图创建前后皆可调用；创建前由 WebView 控件
    挂起，创建时补上）。同一通道重复注册为幂等成功。

- void RemoveHandler(string name)
  - 撤掉消息通道：移除 shim 脚本（对之后的页面生效），名字出活跃表
    （当前页残留的 shim 再 postMessage 也会被名字检查挡下）。

- void PushMessage(string line)

- string TakeMessage()

- int PendingMessages()

- int DroppedMessages()

- string AddDocScript(string js)
  - AddScriptToExecuteOnDocumentCreated 的一次调用：非阻塞向引擎注册（零消息泵），
    回调到达后自动记录脚本 id。

- bool InjectScript(string js, bool atEnd)
  - 每次导航都注入的脚本：atEnd=true 包一层 readyState 门，文档解析完
    再跑（WebView2 只有"任何脚本之前"一档，document-end 语义由此
    仿真；脚本包在闭包里，页面可见的全局需显式挂到 window 上）。

- bool InjectStyle(string css)
  - 每次导航都注入的 CSS：包成 <style> 追加脚本（与 macOS 后端同构）。

- void ClearScripts()
  - 撤掉全部 InjectScript/InjectStyle 注入（消息桥 shim 不受影响，
    与 macOS 的 removeAllUserScripts 语义一致）。

- static string JsQuote(string s)
  - JS 字符串字面量（带双引号）：转义反斜杠、引号与控制字符，防注入
    进的文本破坏脚本结构。

- static string ShimScript(string name)
  - <name> 通道的 shim：把 WebView2 的 window.chrome.webview 桥包装成
    与 macOS 同形的 window.webkit.messageHandlers.<name>.postMessage。
    字符串原样传递，其他类型 JSON 序列化，两端正文语义一致。

- static string DeferToDomReady(string js)
  - document-end 语义仿真：解析未完挂 DOMContentLoaded，否则立即执行。

- void SetZoom(int percent)
  - 整页缩放，percent 为百分数（100 = 原大）。旧运行时缺 ZoomFactor
    的情形不需处理：槽位自首个公开版即存在。

- void OpenDevTools()
  - 为当前页打开 DevTools 窗口（发布给最终用户的程序不必携带）。

- bool ClearBrowsingData()
  - 清掉本视图数据仓的全部网站数据（Cookie、DOM 存储、缓存、
    Service Worker）。需要较新的运行时（ICoreWebView2Profile2）；
    不可用时返回 false，调用方退化为只清 Cookie。

- string DrainWindowNav()
  - 取走上一帧新窗口请求记下的就地加载目标（无则为 ""）。由每帧的
    轮询调用，把 window.open/target=_blank 落到本视图导航。

- void SetDevToolsEnabled(bool enabled)
  - 设置是否允许开发者工具（F12、快捷键及检查元素）

- void SetContextMenuEnabled(bool enabled)
  - 设置是否允许默认右键上下文菜单

- void ApplySettings()
  - 同步设置到底层 ICoreWebView2Settings

- nint Cookies()
  - ICoreWebView2_2 带有 Cookie 管理器；较旧的运行时
    则不产生任何 Cookie。

- async string GetCookiesAsync(string forUrl, int timeoutMs=5000){ nint sink=this.Sink((nint)WebView2.OnCookiesCompleted);cookieResult="";cookieGate=new Gate();WebView2.IssueOnUi(()=>{ nint cm=this.Cookies();if (cm==0){ Console.WriteLine("[WV2]Cookies()returned 0!");cookieGate.Signal();return;}nint u=0;if (forUrl!=""){ u=Wide.Of(forUrl);}int hr=Com.Call2(cm, WebView2.SlotGetCookies(), u, sink);if (u!=0){ Wide.Free(u);}Com.Release(cm);if (!Com.Ok(hr)){ Console.WriteLine("[WV2]SlotGetCookies hr="+Convert.ToString(hr));cookieGate.Signal();}});await cookieGate.Wait(timeoutMs);return cookieResult;}async string GetCookieDetailsAsync(string forUrl, int timeoutMs=5000){ nint sink=this.Sink((nint)WebView2.OnCookiesCompleted);cookieResult="";cookieDetailsResult="";cookieGate=new Gate();WebView2.IssueOnUi(()=>{ nint cm=this.Cookies();if (cm==0){ Console.WriteLine("[WV2]Cookies()returned 0!");cookieGate.Signal();return;}nint u=0;if (forUrl!=""){ u=Wide.Of(forUrl);}int hr=Com.Call2(cm, WebView2.SlotGetCookies(), u, sink);if (u!=0){ Wide.Free(u);}Com.Release(cm);if (!Com.Ok(hr)){ Console.WriteLine("[WV2]SlotGetCookies hr="+Convert.ToString(hr));cookieGate.Signal();}});await cookieGate.Wait(timeoutMs);return cookieDetailsResult;}async List<WebCookie> GetCookieListAsync(string forUrl="", int timeoutMs=5000){ nint sink=this.Sink((nint)WebView2.OnCookiesCompleted);cookieResult="";cookieDetailsResult="";cookieListResult=new List<WebCookie>();cookieGate=new Gate();WebView2.IssueOnUi(()=>{ nint cm=this.Cookies();if (cm==0){ Console.WriteLine("[WV2]Cookies()returned 0!");cookieGate.Signal();return;}nint u=0;if (forUrl!=""){ u=Wide.Of(forUrl);}int hr=Com.Call2(cm, WebView2.SlotGetCookies(), u, sink);if (u!=0){ Wide.Free(u);}Com.Release(cm);if (!Com.Ok(hr)){ Console.WriteLine("[WV2]SlotGetCookies hr="+Convert.ToString(hr));cookieGate.Signal();}});await cookieGate.Wait(timeoutMs);return cookieListResult;}string GetCookies(string forUrl){ nint cm=this.Cookies();if (cm==0){ return cookieResult;}nint sink=this.Sink((nint)WebView2.OnCookiesCompleted);nint u=0;if (forUrl!=""){ u=Wide.Of(forUrl);}Com.Call2(cm, WebView2.SlotGetCookies(), u, sink);if (u!=0){ Wide.Free(u);}Com.Release(cm);return cookieResult;}void SetCookie(string forUrl, string cookieName, string cookieValue){ nint cm=this.Cookies();if (cm==0){ return;}string host=WebView2.HostOf(forUrl);if (host!=""){ nint n=Wide.Of(cookieName);nint val=Wide.Of(cookieValue);nint d=Wide.Of(host);nint p=Wide.Of("/");nint cell=NativeMemory.Alloc(8);new Span<long>(cell, 1)[0]=0;int hr=Com.Call5(cm, WebView2.SlotCreateCookie(), n, val, d, p, cell);nint cookie=new Span<long>(cell, 1)[0];NativeMemory.Free(cell);if (Com.Ok(hr)&&cookie!=0){ Com.Call1(cm, WebView2.SlotAddOrUpdateCookie(), cookie);Com.Release(cookie);}Wide.Free(n);Wide.Free(val);Wide.Free(d);Wide.Free(p);}Com.Release(cm);}void ClearCookies(){ nint cm=this.Cookies();if (cm==0){ return;}Com.Call0(cm, WebView2.SlotDeleteAllCookies());Com.Release(cm);}static string HostOf(string url){ int n=url.Length;int start=0;int i=0;while (i+2 <n){ if (url[i]==':'&&url[i+1]=='/'&&url[i+2]=='/'){ start=i+3;i=n;}else{ i=i+1;}}string host="";i=start;while (i <n){ char ch=url[i];if (ch=='/'||ch=='?'||ch=='#'||ch==':'){ return host;}host=host+url.Substring(i, 1);i=i+1;}return host;}void Dispose(){ if (core!=0){ this.Unsubscribe(WebView2.SlotRemoveNavigationCompleted(), navToken);this.Unsubscribe(WebView2.SlotRemoveNavigationStarting(), startToken);this.Unsubscribe(WebView2.SlotRemoveSourceChanged(), srcToken);this.Unsubscribe(WebView2.SlotRemoveWebMessage(), msgToken);this.Unsubscribe(WebView2.SlotRemoveNewWindow(), winToken);Com.Release(core);core=0;}if (ctrl!=0){ Com.Call0(ctrl, WebView2.SlotControllerClose());Com.Release(ctrl);ctrl=0;}if (env!=0){ Com.Release(env);env=0;}if (host!=0){ WebView2.DestroyWindow(host);host=0;}}
  - 异步读取 `forUrl` 匹配域名的 Cookie 并在回调到达时恢复（协程挂起，
    零消息泵，不卡死 GUI 线程）。发起经 IssueOnUi 投回 UI 线程（下同）。


## WebViewBackend (class)

WebView 控件驱动的扁平句柄 API，映射到平台
所拥有的任何引擎上。

Windows 上的引擎是 Edge WebView2，在 Zan（WebView2.zan）中
直接针对其 COM 接口实现——无需原生垫片。macOS 上由 zan_gui
运行时提供 WKWebView；Android 上同一组 zan_gui_webview_*
入口由 libzan_gui.so 提供，落在系统 android.webkit.WebView
上（gui_runtime_android.c + APK shell 里的 ZanWeb Java 桥）。
没有可嵌入引擎的平台走
下面的回退路径：Create 返回 0，控件绘制自己的
占位符，因此完全不引用任何原生符号。

- [DllImport("zan_gui")]static extern int zan_gui_webview_create(nint hwnd, string profileId);

- [DllImport("zan_gui")]static extern void zan_gui_webview_destroy(int h);

- [DllImport("zan_gui")]static extern void zan_gui_webview_set_frame(int h, int x, int y, int w, int hh);

- [DllImport("zan_gui")]static extern void zan_gui_webview_set_visible(int h, int visible);

- [DllImport("zan_gui")]static extern void zan_gui_webview_navigate(int h, string url);

- [DllImport("zan_gui")]static extern void zan_gui_webview_load_html(int h, string html, string baseUrl);

- [DllImport("zan_gui")]static extern void zan_gui_webview_back(int h);

- [DllImport("zan_gui")]static extern void zan_gui_webview_forward(int h);

- [DllImport("zan_gui")]static extern void zan_gui_webview_reload(int h);

- [DllImport("zan_gui")]static extern void zan_gui_webview_stop(int h);

- [DllImport("zan_gui")]static extern int zan_gui_webview_can_go_back(int h);

- [DllImport("zan_gui")]static extern int zan_gui_webview_can_go_forward(int h);

- [DllImport("zan_gui")]static extern int zan_gui_webview_is_loading(int h);

- [DllImport("zan_gui")]static extern int zan_gui_webview_nav_seq(int h);

- [DllImport("zan_gui")]static extern int zan_gui_webview_last_status(int h);

- [DllImport("zan_gui")]static extern string zan_gui_webview_get_url(int h);

- [DllImport("zan_gui")]static extern string zan_gui_webview_get_title(int h);

- [DllImport("zan_gui")]static extern string zan_gui_webview_last_request(int h);

- [DllImport("zan_gui")]static extern string zan_gui_webview_eval(int h, string js);

- [DllImport("zan_gui")]static extern string zan_gui_webview_get_cookies(int h, string url);

- [DllImport("zan_gui")]static extern void zan_gui_webview_set_cookie(int h, string url, string cookieName, string cookieValue);

- [DllImport("zan_gui")]static extern void zan_gui_webview_clear_cookies(int h);

- [DllImport("zan_gui")]static extern void zan_gui_webview_set_devtools_enabled(int h, int enabled);

- [DllImport("zan_gui")]static extern void zan_gui_webview_set_context_menu_enabled(int h, int enabled);

- static int Create(nint hwnd, string profileId)
  - 创建以窗口句柄为父的视图，返回句柄。profileId 隔离
    Cookie/缓存（""=默认配置）。平台没有可嵌入引擎时返回 0，
    后续所有调用均为安全空操作；无效句柄同样被忽略。

- static void Destroy(int h)
  - 销毁视图并释放原生资源；无效句柄被忽略。

- static void SetFrame(int h, int x, int y, int w, int hh)
  - 把原生视图摆到客户区坐标 (x,y,w,hh)（逻辑像素，平台自行按 DPI
    折算）；无效句柄被忽略。

- static void SetVisible(int h, bool visible)
  - 显示或隐藏原生视图；无效句柄被忽略。

- static void ApplyClip(int h, string spec)
  - 把本帧未被遮挡的区域下发给原生视图。`spec` 是
    "x,y,w,h;..." 的矩形并集（客户区坐标），"" = 完全被盖住，
    应该隐藏。签名匹配 Gui.NativeClipFn，由 WebView.Render 登记给
    App，帧末统一回调（委托是纯函数指针，所以这里是静态方法）。

- static void Navigate(int h, string url)
  - 导航到 URL；无效句柄被忽略。

- static void LoadHtml(int h, string html, string baseUrl)
  - 以 `baseUrl` 解析相对地址加载一段 HTML；无效句柄被忽略。

- static void Back(int h)
  - 后退一页；无效句柄被忽略。

- static void Forward(int h)
  - 前进一页；无效句柄被忽略。

- static void Reload(int h)
  - 重新加载当前页；无效句柄被忽略。

- static void Stop(int h)
  - 停止当前加载；无效句柄被忽略。

- static bool CanGoBack(int h)
  - 能否后退（无历史/无效句柄/无引擎为 false）。

- static bool CanGoForward(int h)
  - 能否前进（无历史/无效句柄/无引擎为 false）。

- static bool IsLoading(int h)
  - 是否正在加载（无效句柄/无引擎为 false）。

- static int NavSeq(int h)
  - 每次导航/源变化时递增的计数器；控件轮询它
    以判断 URL 和标题何时可能已变化。无引擎平台恒为 0。

- static int LastStatus(int h)
  - 最近一次导航完成的 HTTP 状态码；无导航或无引擎时为 0。

- static string GetUrl(int h)
  - 当前 URL（无页面/无效句柄/无引擎为 ""）。

- static string GetTitle(int h)
  - 当前页面标题（无页面/无效句柄/无引擎为 ""）。

- static string LastRequest(int h)
  - 最近一次导航请求的 URL（导航开始时记录；无/无效句柄为 ""）。

- static string Eval(int h, string js)
  - 同步执行 JS 并取返回值（非阻塞，禁止在 GUI 线程跑消息泵）。
    如需异步等待计算结果，请优先使用 `EvalWithResultAsync`。失败或无引擎返回 ""。

- static async string EvalWithResultAsync(int h, string js, int timeoutMs=5000){ WebView2 w=WebView2.Get(h);if (w==null){ return"";}string res=await w.EvalWithResultAsync(js, timeoutMs);return res;return WebViewBackend.zan_gui_webview_eval(h, js);return"";}static async bool EnsureReadyAsync(int h, int timeoutMs=15000){ WebView2 w=WebView2.Get(h);if (w==null){ return false;}bool ok=await w.EnsureReadyAsync(timeoutMs);return ok;return true;}static string GetCookies(int h, string url){ WebView2 w=WebView2.Get(h);if (w==null){ return"";}return w.GetCookies(url);return WebViewBackend.zan_gui_webview_get_cookies(h, url);return"";}static async string GetCookiesAsync(int h, string url, int timeoutMs=5000){ WebView2 w=WebView2.Get(h);if (w==null){ return"";}string res=await w.GetCookiesAsync(url, timeoutMs);return res;return WebViewBackend.zan_gui_webview_get_cookies(h, url);return"";}static async List<WebCookie> GetCookieListAsync(int h, string url, int timeoutMs=5000){ WebView2 w=WebView2.Get(h);if (w==null){ return new List<WebCookie>();}List<WebCookie> res=await w.GetCookieListAsync(url, timeoutMs);return res;return new List<WebCookie>();}static async string GetCookieDetailsAsync(int h, string url, int timeoutMs=5000){ WebView2 w=WebView2.Get(h);if (w==null){ return"";}string res=await w.GetCookieDetailsAsync(url, timeoutMs);return res;return"";}static void SetCookie(int h, string url, string cookieName, string cookieValue){ WebView2 w=WebView2.Get(h);if (w!=null){ w.SetCookie(url, cookieName, cookieValue);}WebViewBackend.zan_gui_webview_set_cookie(h, url, cookieName, cookieValue);}static void ClearCookies(int h){ WebView2 w=WebView2.Get(h);if (w!=null){ w.ClearCookies();}WebViewBackend.zan_gui_webview_clear_cookies(h);}static nint guiMod=0;
  - 异步执行 JS 并等待其返回值（协程挂起，零消息泵，不阻塞 GUI 线程）。

- static bool guiTried=false;

- static nint addr_addHandler=0;

- static nint addr_removeHandler=0;

- static nint addr_takeMessage=0;

- static nint addr_msgPending=0;

- static nint addr_msgDropped=0;

- static nint addr_addScript=0;

- static nint addr_addStyle=0;

- static nint addr_removeScripts=0;

- static nint addr_evalAsync=0;

- static nint addr_clearData=0;

- static nint addr_setClip=0;

- static nint addr_setZoom=0;

- static void ResolveBridge()
  - 解析 zan_gui 里可选的 WKWebView 桥接入口。zan_gui 已被主程序加载，
    dlopen 只是拿到同一个镜像的句柄：先按可执行文件同目录找（发布包把
    dylib 放在 exe 旁边），再交给加载器按名字找。Android 上同一组入口
    由 libzan_gui.so 提供（系统 WebView 后端，gui_runtime_android.c）。

- static bool HasBridge()
  - 当前平台/运行时是否支持 JS→native 的消息桥。Windows 走
    WebView2 的 WebMessageReceived（shim 对齐 window.webkit 语义）；
    macOS/Android 由 zan_gui 运行时提供桥接入口时为 true。

- static bool AddHandler(int h, string handlerName)
  - 注册 window.webkit.messageHandlers.<name> 消息通道；
    页面 postMessage 的内容随后用 TakeMessage 逐条取。运行时
    不支持时返回 false。

- static void RemoveHandler(int h, string handlerName)
  - 撤掉 AddHandler 注册的消息通道；不支持时为空操作。

- static string TakeMessage(int h)
  - 队列里最早的一条消息，格式 "<handler>\t<body>"；空队列为 ""。

- static int PendingMessages(int h)
  - 消息队列里还没取走的条数（不支持为 0）。

- static int DroppedMessages(int h)
  - 因队列满而被丢掉的消息数（宿主没及时取走）。

- static bool AddScript(int h, string js, bool atEnd)
  - 每页可注入脚本：每次导航都重新执行；atEnd=true 在
    文档解析完后跑（DOM 就绪），false 在文档开始前跑。注册失败
    （含运行时不支持）返回 false。

- static bool AddStyle(int h, string css)
  - 为当前页注入一段 CSS（每次导航后仍生效）；注册失败（含运行时
    不支持）返回 false。

- static void RemoveScripts(int h)
  - 撤掉全部注入脚本与样式；不支持时为空操作。

- static void EvalAsync(int h, string js)
  - 不等待结果的 Native→JS 调用（Eval 会泵消息等
    返回值，从绘制/事件回调里调不合适）；运行时没有这个入口时
    退回同步 Eval。

- static void ClearData(int h)
  - 清掉该视图数据仓的全部网站数据（Cookie、缓存、
    localStorage…）。Windows 上运行时带 Profile2 接口时走
    ClearBrowsingData(ALL_SITE)，否则退化为只清 Cookie；macOS/Android
    运行时不支持时同样退化。

- static void SetZoom(int h, int percent)
  - 整页缩放，percent 为百分数（100 = 原大）。macOS/Android
    需运行时带 set_zoom 入口（旧库没有时为空操作）。

- static void OpenDevTools(int h)
  - 为当前页打开 DevTools。Windows 直接开窗口；macOS 由
    创建时的 developerExtrasEnabled 支持右键检查，无程序化入口，
    因而为空操作。

- static void SetDevToolsEnabled(int h, bool enabled)
  - 设置是否允许开发者工具（F12、快捷键及检查元素）。
    Windows 走 ICoreWebView2Settings，macOS/Android 走原生桥接设置。

- static void SetContextMenuEnabled(int h, bool enabled)
  - 设置是否允许默认右键上下文菜单。
    Windows 走 ICoreWebView2Settings，macOS/Android 走原生桥接设置。

- static string DrainWindowNav(int h)
  - 取走上一帧记下的新窗口就地加载目标（无则为 ""）。每帧
    由 Render 轮询：window.open/target=_blank 被接管后在下一帧落到
    本视图导航，避免在事件回调内重入导航。


## WebViewBootstrap (class)

WebView 家族的 HeavyControls/链接路由注册入口。IDE、浏览器示例
等真正用到 WebViewBox 的宿主在启动时调用一次 `Install`；
不调用的程序里整个 WebView 家族被裁掉，发布不再携带
WebView2Loader.dll（bundle 的 "if WebView2_" 前缀条件）。

Install 同时向 App 注册 <a href> 内嵌导航器（宿主就地图优先，
没有就懒建 App 级链接窗口）：LinkWindow/WebViewBox 只住在本目录，
App 编译图不背 WebView（auto-stdlib 按 using 整目录拉入），
未安装的程序链接直开系统浏览器。原 App.UseWebview 的业务入口
由 `UseAsLinkTarget` 承接。WebView2Loader.dll 也按
组件驱动存放（本目录 drivers/，清单以 "if WebView" 前缀门控），
不用 WebView 的程序发布不携带。

- static bool installed;

- static WebViewBox hostBox;
  - 宿主注册的内嵌就地图（UseAsLinkTarget）：非空且属于发起
    导航的 App 时链接就地 Navigate 它，不开链接窗口。

- static App hostBoxOwner;

- static LinkWindow linkWin;
  - App 级链接窗口（原 App.linkWin）：首次点链接懒建，之后
    复用；关掉后再点重建。

- static void Install()

- static void UseAsLinkTarget(App app, WebViewBox box)
  - 业务侧注册宿主自有的内嵌网页视图（如应用自带的浏览器标签，
    原 App.UseWebview）：之后 <a href> 的缺省路由就地 Navigate 它
    而不开链接窗口。传 null 注销。WebView 运行时不可用
    （IsSupported false）的注册会被导航忽略并走链接窗口/系统浏览器。

- static bool NavigateLink(App app, string url)
  - App.linkNavigator 的实现（见 Gui.LinkNavigateFn）：总是返回
    true——WebView 不可用的回落在 LinkWindow 首帧内部完成。

- static Control MakeWebViewBox(string kind)

- [DllImport("urlmon", EntryPoint="URLDownloadToFileW")]static extern int UrlDownloadToFile(nint caller, nint url, nint file, int reserved, nint callback);

- [DllImport("ntdll", EntryPoint="RtlGetVersion")]static extern int RtlGetVersion(nint info);

- [DllImport("kernel32", EntryPoint="CreateProcessW")]static extern int CreateProcess(nint app, nint cmdline, nint procAttr, nint threadAttr, int inheritHandles, int flags, nint env, nint dir, nint startupInfo, nint procInfo);

- [DllImport("kernel32", EntryPoint="WaitForSingleObject")]static extern int WaitForSingleObject(nint handle, int ms);

- [DllImport("kernel32", EntryPoint="CloseHandle")]static extern int CloseHandle(nint handle);

- [DllImport("shell32", EntryPoint="ShellExecuteW")]static extern nint ShellExecute(nint hwnd, nint verb, nint file, nint parameters, nint dir, int show);

- static string BootstrapperUrl()
  - 微软官方 Evergreen Bootstrapper 直链（fwlink 永久重定向）。

- static bool EnsureRuntime()
  - 确保系统装着 WebView2 运行时，可直接内嵌网页时返回
    true。没装则下载官方 bootstrapper 静默安装（会弹一次 UAC 提权，
    视网速等半分钟到几分钟），装成功返回 true——之后要**新建
    WebView/WebViewBox 实例**才会重试创建（旧实例 created 标志是
    一次性的）。Win7/8.1 打开官方下载页并返回 false（由用户装
    109 离线包，程序里 <c>IsSupported()</c> 为 false 时引导即可）。
    macOS 返回 true（WKWebView 系统自带），其他平台返回 false。
    建议在首帧发现 <c>IsSupported()==false</c> 时询问用户后再调，
    不要默认静默下载执行安装程序。

- static bool IsWindows7Or8()
  - Win7/8.1 判定：ntdll RtlGetVersion 的主版本号 < 10。

- static bool RunInstaller(string exe)
  - 以 `exe` 为命令行首起进程并等它退出（至多 10 分钟）：
    bootstrapper 的 manifest 自带提权，UAC 弹窗由它自己触发。

- static void OpenUrl(string url)


## WebViewBox (class)

可摆放的网页视图控件：把原生 WebView 包成一个普通的保留式
Control，因此设计器 / .html 设计稿里的网页视图和别的控件一样，
由布局给它一块矩形、由它自己负责绘制。

WebViewBox box = new WebViewBox();
box.SetStartUrl("https://example.com");
tabs.Page(0).Add(box);              // 标签页里放一个浏览器
box.View().NavComplete += () => { ... };

与直接用 WebView 的区别在于「谁驱动它」：WebView 是原生兄弟层，
必须每帧被告知矩形，隐藏时还要有人替它调 Hide()。这个控件把
两件事都接了过来——可见时按自己的已解析边界 Render，落在隐藏
的标签页 / 折叠容器里时整棵子树不参与渲染，由 NativeLayer 兜底
把没登记的原生层裁空（即隐藏）。

- WebView view;
  - 本控件拥有的原生视图（每个 WebViewBox 一个浏览器实例，
    因此多标签浏览器就是多个 WebViewBox）。

- string startUrl;
  - 设计期填写的起始地址，首帧导航一次。

- bool navigated;

- WebViewBox():this("")
  - 默认构造：使用共享默认存储（"" profile）。

- WebViewBox(string profileId)
  - 绑定到隔离配置文件的网页视图（见 WebView 的 profileId）。

- WebView View()
  - 底层原生视图：历史、Cookie、Eval、事件都在它上面。
    本控件唯一拥有的 WebView 实例。

- void SetStartUrl(string u)
  - 起始地址（设计属性 `url`，设计文档里也可写成
    `placeholder`）。首帧之前设置只记录起始地址，待首次绘制时
    导航一次；首帧之后设置立即等同于 Navigate。

- string StartUrl()
  - 当前设置的起始地址。

- void Navigate(string target)
  - 立即导航到 target（与 SetStartUrl 不同，不等首帧）。

- void Back()
  - 历史后退一页（转发到底层视图）。

- void Forward()
  - 历史前进一页。

- void Reload()
  - 重新加载当前页。

- void Stop()
  - 停止当前加载。

- void SetZoom(int percent)
  - 整页缩放（百分数，100 = 原大；25..500 截断），转发底层视图。

- void OpenDevTools()
  - 为当前页打开 DevTools（见 WebView.OpenDevTools）。

- bool CanGoBack()
  - 能否后退。

- bool CanGoForward()
  - 能否前进。

- bool IsLoading()
  - 是否正在加载。

- int LastStatus()
  - 最近一次响应的 HTTP 状态码（无为 0）。

- string CurrentUrl()
  - 当前 URL 快照。

- string CurrentTitle()
  - 当前文档标题快照。

- void Hide()
  - 隐藏原生视图（本控件不再上屏时调用；隐藏的标签页由
    NativeLayer 自动兜底，这里供宿主显式收起）。

- void Destroy()
  - 销毁原生视图（关闭标签页时调用），并把控件从树上
    摘下。销毁后不要再 Render 本控件。

- override string Kind()
  - 控件类型标识（"WebViewBox"）。

- override List<PropSpec> Props()
  - 设计器属性：起始地址（"url"）。

- override List<string> Events()
  - 控件事件清单：公共事件外加四个导航语义事件。

- override void BindEvent(string evt, Action a)
  - 浏览器的语义事件挂在原生视图上，设计里的 `onNavComplete`
    之类因此直接落到它的 UiEvent，宿主不必自己接线。

- override void BindEventS(string evt, ControlEvent a)
  - `BindEvent` 的 sender 通道（S = Sender）：
    特化事件接本控件的 UiEvent，通用事件按名路由。

- override void OnPaint(App app)
  - 绘制：零尺寸时隐藏原生视图；首帧前先消费起始地址（原生视图
    在 Render 里才创建，创建时会消费这次挂起的导航），然后每帧
    驱动底层视图。


## int (delegate)

ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler::Invoke 及其
控制器/脚本孪生签名：(this, HRESULT, result)。

`delegate int WvResultFn(nint self, int hr, nint result);`


## int (delegate)

事件处理器：(this, sender, args)。

`delegate int WvEventFn(nint self, nint sender, nint args);`


## int (delegate)

IUnknown::QueryInterface：(this, riid, ppv)，返回 HRESULT。

`delegate int WvQueryInterfaceFn(nint self, nint riid, nint ppv);`


## int (delegate)

IUnknown::AddRef/Release 孪生：(this)，返回名义引用计数。

`delegate int WvRefFn(nint self);`


## int (delegate)

CreateCoreWebView2EnvironmentWithOptions，加载器唯一的扁平导出。

`delegate int WvCreateEnvFn(nint browserFolder, nint userDataFolder, nint options, nint handler);`


## int (delegate)

GetAvailableCoreWebView2BrowserVersionString 的调用形式：查系统
已安装运行时的版本号。

`delegate int WvGetVersionFn(nint folder, nint version);`


## int (delegate)

ICoreWebView2Controller::put_ZoomFactor：双精度缩放系数按值传递
（x64 ABI 走 xmm 寄存器，不能用指针型委托代替）。

`delegate int WvPutZoomFn(nint self, double zoom);`


## int (delegate)

`delegate int WvNameFn(int h, string name);`


## int (delegate)

`delegate int WvIntIntFn(int h);`


## int (delegate)

`delegate int WvScriptFn(int h, string js, int atEnd);`


## string (delegate)

`delegate string WvStrIntFn(int h);`


## void (delegate)

`delegate void WvNameVoidFn(int h, string name);`


## void (delegate)

`delegate void WvVoidIntFn(int h);`


## void (delegate)

`delegate void WvZoomIntFn(int h, int percent);`
