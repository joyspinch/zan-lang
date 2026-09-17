# Gui.Component.WebView

> 源码: `stdlib/Gui/Component/WebView/WebView.zan`, `stdlib/Gui/Component/WebView/WebView2.zan`, `stdlib/Gui/Component/WebView/WebViewBackend.zan`, `stdlib/Gui/Component/WebView/WebViewBox.zan`


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
    ""）。用于读取请求参数、DOM 状态或
    从页面获取的响应体。

- bool SupportsMessages()
  - 这个运行时上能不能收页面发来的消息。三端同形：页面侧都是
    window.webkit.messageHandlers.<name>.postMessage(x)（Windows 由注入
    shim 把 WebView2 的 window.chrome.webview 桥包装成同形），消息按
    `TakeMessage` 逐条取。

- void AddMessageHandler(string handlerName)
  - 让页面可以用
    window.webkit.messageHandlers.<name>.postMessage(x)
    给宿主发消息；消息按 `TakeMessage` 逐条取（和 CEF 那边的
    CDP 队列同一套用法）。可在原生视图创建前调用。

- void RemoveMessageHandler(string handlerName)
  - 撤掉 AddMessageHandler 注册的消息通道（不支持的后端为空操作）。

- string TakeMessage()
  - 取走一条页面消息，格式 "<handler>\t<body>"（body 是字符串原文，
    其他类型的 postMessage 参数是 JSON）；空队列为 ""。

- int MessagePending()
  - 队列里待取的消息数。

- int MessageDropped()
  - 因为迟迟没被取走而丢掉的消息数（队列有界，防止跑飞的页面把内存
    吃光）。

- void InjectScript(string js, bool atDocumentEnd)
  - 每次导航都注入的脚本：atDocumentEnd=true 在文档解析完后跑（能用
    document/window），false 则在页面自己的脚本之前跑（适合装桥）。

- void InjectStyle(string css)
  - 每次导航都注入的 CSS。

- void ClearInjected()
  - 撤掉此前注入的所有脚本/样式（下一次导航起生效）。

- void EvalAsync(string js)
  - Native→JS，但不等结果：`Eval` 要等返回值，会自旋事件
    循环，从绘制/事件回调里调不合适。

- string GetCookies(string forUrl)
  - `url` 的 Cookie，序列化为 "name=value; name2=value2"（无则为 ""）。
    传 "" 获取存储中的所有 Cookie。

- void SetCookie(string forUrl, string cookieName, string cookieValue)
  - 为 `url` 的主机在存储中设置 Cookie（路径默认为 "/"）。

- void ClearCookies()
  - 清除共享存储中的所有 Cookie。

- void ClearBrowsingData()
  - 清掉这个配置文件的全部网站数据：Cookie、缓存、localStorage、
    IndexedDB…（“退出登录并忘记我”）。Windows 运行时带 Profile2 接口
    时走 ClearBrowsingData(ALL_SITE)，否则退化为 `ClearCookies`。

- void SetZoom(int percent)
  - 整页缩放，percent 为百分数（100 = 原大），立即生效；每次导航后
    保持。越界取 25..500。后端不支持时为空操作。

- void OpenDevTools()
  - 为当前页打开浏览器 DevTools 窗口（调试用）。Windows 直接弹出
    DevTools 窗口；macOS 无程序化入口，创建时已启用 "Inspect
    Element"（右键菜单），因此此调用为空操作。

- void Hide()
  - 隐藏原生视图（对不在屏幕上的标签/面板调用）。

- void Destroy()
  - 销毁原生视图并释放其资源。销毁后再调用其他方法均为
    空操作；如需重用应新建实例。

- int Render(App app, int x, int y, int w, int h)
  - 将原生视图放到给定的客户区矩形中并显示，然后
    轮询引擎的变化，更新 Url()/Title() 并触发
    NavStart / LoadingChanged / TitleChanged / NavComplete。浏览器运行在
    自己的线程上，因此其事件以这种每帧差异的形式到达 UI。
    在没有原生 Web 视图的平台上，它绘制一个覆盖相同矩形的
    画布内占位符。
    每帧驱动：摆放原生视图、登记裁剪占位并轮询引擎差异
    事件（NavStart/LoadingChanged/TitleChanged/NavComplete）。浏览器
    运行在自己的线程上，事件以这种每帧差异的形式到达 UI，因此
    隐藏的实例必须有人替它调 Hide()（WebViewBox 自动处理）。
    返回原生句柄（不支持时为 0 并画占位符）。

- static void PaintPlaceholder(App app, int x, int y, int w, int h)
  - 无原生后端时的画布内占位：图标加一行说明文字。

- static string Normalize(string target)
  - 导航目标补全：没有协议前缀的裸地址加 `https://`。

- static bool HasScheme(string s)
  - 是否带协议前缀（"://"）。


## WebViewBootstrap (class)

HeavyControls/链接路由注册入口：宿主启动时调一次 `Install()`，之后
设计稿里的 WebViewBox 可用、`<a href>` 走内嵌导航器。不调用的程序
整个家族被 DCE 删光，发布不携带浏览器 DLL。

- static bool EnsureRuntime()
  - 确保系统装着 WebView2 运行时，可直接内嵌网页时返回 true。没装则
    下载微软官方 bootstrapper（约 1.8MB）静默安装（弹一次 UAC 提权），
    装成功返回 true——之后要**新建 WebView/WebViewBox 实例**才会重试
    创建。Win7/8.1 上 bootstrapper 已不派发兼容版（109 为最后版本），
    会打开官方下载页并返回 false（用户装 109 离线包，或发布带 CEF 的
    zan_cef109 驱动）。macOS 返回 true（WKWebView 系统自带）。建议在
    `IsSupported()==false` 时询问用户后再调，不要默认静默下载执行
    安装程序。

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

- int navSeq;

- int lastStatus;

- bool loading;

- string url;

- string title;

- string lastRequest;

- string evalResult;

- string cookieResult;

- PumpGate gate;

- int fx;

- int fy;

- int fw;

- int fh;

- bool frameSet;

- string clipSpec;

- bool clipSet;

- bool visible;

- bool visibleSet;

- static int SlotGetSource()
  - ICoreWebView2 的 vtable 槽。
    槽 4：get_Source，读取当前页面 URI。

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
  - ICoreWebView2_2（新增 Cookie 管理器）的 vtable 槽。

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

- static List<WebView2> All()
  - 全部存活视图的静态表（惰性创建；下标 0 对应句柄 1）。

- static WebView2 Get(int h)
  - 句柄 1..n 寻址静态视图列表；0 或越界返回 null。

- static bool IsAvailable()
  - 存在 WebView2Loader.dll 和运行时环境时为 true。

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
  - 启动序列：建宿主子窗口 → 创建 Environment（泵消息至多 8s）→
    创建 Controller（泵至多 12s，成功时顺带取 core）→ 订阅
    NavigationStarting/SourceChanged/NavigationCompleted 三个事件 →
    以隐藏的零尺寸空白状态收尾。任一步失败返回 false。

- long Subscribe(int slot, nint sink)
  - add_* 接受处理器和一个 EventRegistrationToken 输出参数；
    稍后 remove_* 需要的就是这个 token。

- void Unsubscribe(int slot, long token)
  - remove_* 撤销订阅：token 装进 8 字节临时单元按地址传给 COM。

- static string ProfileDir(string profileId)
  - %LOCALAPPDATA%\ZanGui\WebView2\<profile>；WebView2 按此文件夹
    划分 Cookie 和存储，这正是配置文件隔离的原理。

- static bool IsSafeChar(char ch)
  - 保证配置文件文件夹是单个安全的路径段。

- static int OnQueryInterface(nint self, nint riid, nint ppv)
  - WebView2 只索取它拿到的接口，因此对任何 riid 都返回
    `this` 是完成处理器惯常的应答方式。

- static int OnAddRef(nint self)
  - 回调对象与视图同生命周期，因此引用计数只是名义上的。

- static int OnRelease(nint self)
  - Release 回调：恒返回 1；回调对象随视图一起释放，不单独回收。

- static int OnEnvironment(nint self, int hr, nint result)
  - Environment 创建完成回调（COM）：成功则保留 env 引用，
    并唤醒 Start 里的 gate。

- static int OnController(nint self, int hr, nint result)
  - Controller 创建完成回调（COM）：成功则保留 ctrl 并取 core，
    唤醒 Start 里的 gate。

- static int OnNavigationStarting(nint self, nint sender, nint args)
  - 导航开始回调（COM）：记录目标 URL 到 lastRequest，置 loading
    （完成时由 OnNavigationCompleted 解除）。

- static int OnNavigationCompleted(nint self, nint sender, nint args)
  - 导航完成回调（COM）：记录近似 HTTP 状态（成功 200，失败
    400+WebView2 错误枚举）、URL/标题，navSeq 自增解除 loading。

- static int OnSourceChanged(nint self, nint sender, nint args)
  - SPA 导航无需完整加载即可改变 URL；此回调保持 Url()
    的真实性。

- static int OnScriptCompleted(nint self, int hr, nint json)
  - Eval 完成回调（COM）：成功则记下 JSON 结果，唤醒 Eval 的 gate。

- static int OnCookiesCompleted(nint self, int hr, nint list)
  - GetCookies 完成回调（COM）：序列化 Cookie 列表到 cookieResult，
    唤醒 gate。

- static string Serialize(nint list)
  - 将 Cookie 列表呈现为 "name=value; name2=value2"。

- void SetFrame(int x, int y, int w, int h)
  - 把视图摆到客户区的 [x,y,w,h]：宿主子窗口移到该位置，
    controller 在宿主内部铺满。每帧都会调，所以矩形未变时不
    重复下发（put_Bounds 会触发一次重布局）。

- void SetVisible(bool visible)
  - 显示或隐藏：put_IsVisible 加宿主子窗口 SW_SHOWNOACTIVATE/SW_HIDE
    （出现不抢焦点）。状态未变时不重复下发。

- void SetClip(string spec)
  - 把本帧未被遮挡的区域（"x,y,w,h;..."，客户区坐标）下发给
    宿主子窗口。区域为空则隐藏；刚好盖满自身矩形则去掉区域
    （SetWindowRgn(0)），没有弹层时不给系统多余的剪裁负担。

- void Navigate(string target)
  - 导航到 target；core 未就绪时忽略。

- void LoadHtml(string html, string baseUrl)
  - WebView2 没有带 base-url 的 NavigateToString 形式，因此 `baseUrl`
    被接受但忽略。

- void Back()
  - 历史后退一页（core 未就绪为空操作）。

- void Forward()
  - 历史前进一页。

- void Reload()
  - 重新加载当前页。

- void StopLoading()
  - 停止当前加载。

- bool CanGoBack()
  - 能否后退。

- bool CanGoForward()
  - 能否前进。

- bool IsLoading()
  - 是否正在加载（NavigationStarting 置位、Completed 复位）。

- string LastRequest()
  - 最近一次 OnNavigationStarting 捕获的目标 URL（导航发起即记录，
    供挂起导航消费逻辑使用）。

- int NavSeq()
  - 导航序号与近似 HTTP 状态（事件域，不经 COM 往返）。

- int LastStatus()
  - 近似 HTTP 状态：成功 200，失败 400+WebView2 错误枚举；无导航为 0。

- string GetUrl()
  - 实时从核心对象读取当前 URL/标题（同时刷新事件缓存值）。

- string GetTitle()
  - 实时从核心对象读取当前页面标题（同时刷新事件缓存值）。

- string Eval(string js)
  - 运行 JavaScript 并等待其 JSON 结果（出错/超时返回 ""）。

- nint Cookies()
  - ICoreWebView2_2 带有 Cookie 管理器；较旧的运行时
    则不产生任何 Cookie。

- string GetCookies(string forUrl)
  - `forUrl` 的 Cookie，形如 "name=value; ..."；"" 表示整个存储。

- void SetCookie(string forUrl, string cookieName, string cookieValue)
  - WebView2 按主机和路径而非 URL 存储 Cookie，因此
    从 `forUrl` 提取主机，并把 Cookie 写到站点根路径。

- void ClearCookies()
  - 清空该 profile 数据仓的全部 Cookie（运行时无 Cookie 管理器
    时为空操作）。

- static string HostOf(string url)
  - URL 的主机部分：去掉协议、路径、查询和端口。

- void Dispose()
  - 释放 COM 引用、关闭控制器并销毁宿主子窗口与回调对象。


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
  - 同步执行 JS 并取返回值（macOS 上会自旋 runloop 等待；
    不需要返回值时优先用 EvalAsync）。失败或无引擎返回 ""。

- static string GetCookies(int h, string url)
  - 取 `url` 匹配域名的 Cookie（"name=value; ..." 形式；无/失败为 ""）。

- static void SetCookie(int h, string url, string cookieName, string cookieValue)
  - 为 `url` 所在域写一枚 Cookie；无效句柄被忽略。

- static void ClearCookies(int h)
  - 清掉该视图数据仓的全部 Cookie；无效句柄被忽略。

- static nint guiMod=0;

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

- static void ResolveBridge()
  - 解析 zan_gui 里可选的 WKWebView 桥接入口。zan_gui 已被主程序加载，
    dlopen 只是拿到同一个镜像的句柄：先按可执行文件同目录找（发布包把
    dylib 放在 exe 旁边），再交给加载器按名字找。Android 上同一组入口
    由 libzan_gui.so 提供（系统 WebView 后端，gui_runtime_android.c）。

- static bool HasBridge()
  - 当前平台/运行时是否支持 JS→原生 的消息桥。仅 macOS
    运行时带桥接入口时为 true；Windows 走 WebView2 自身通道。

- static bool AddHandler(int h, string handlerName)
  - 注册 window.webkit.messageHandlers.<name> 消息通道；
    页面 postMessage 的内容随后用 TakeMessage 逐条取。运行时不
    支持时返回 false。

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
  - 不等待结果的 Native→JS 调用（Eval 会自旋 runloop 等
    返回值）；运行时没有这个入口时退回同步 Eval。

- static void ClearData(int h)
  - 清掉该视图数据仓的全部网站数据（Cookie、缓存、
    localStorage…）；运行时不支持时退化为只清 Cookie。


## WebViewBox (class)

可摆放的网页视图控件：把原生 WebView 包成一个普通的保留式
Control，因此设计器 / .zform 里的网页视图和别的控件一样，
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
  - 起始地址（设计属性 `url`，.zform 里也可写成
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
