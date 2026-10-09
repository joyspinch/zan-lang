# Gui.Component.WebView

> 源码: `packages/Zan.Gui.Browser/src/Gui/Component/WebView/LinkWindow.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/WebView/WebView.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/WebView/WebView2.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/WebView/WebViewBackend.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/WebView/WebViewBootstrap.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/WebView/WebViewBox.zan`


## LinkWindow (class)

- WebViewBox wv;

- string lastUrl;

- int idBase;

- LinkWindow()

- override string Title()

- override int Width()

- override int Height()

- override int IdBase()

- void Navigate(string url, App parent)

- override void AfterFrame()

- override void OnCloseRequested()


## PendingScriptItem (class)

- public string script;

- public bool atDocumentEnd;

- public PendingScriptItem(string script, bool atDocumentEnd)


## WebCookie (class)

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

- List<PendingScriptItem> pendingScripts;

- List<string> pendingStyles;

- bool pendingDevTools;

- bool devToolsEnabled;

- bool devToolsExplicit;

- bool contextMenuEnabled;

- bool contextMenuExplicit;

- SignalString url;

- SignalString title;

- UiEvent NavComplete;

- UiEvent NavStart;

- UiEvent TitleChanged;

- UiEvent LoadingChanged;

- WebView(string profileId)

- WebView():this("")

- static WebView CreateWithProfile(string profileId)

- string ProfileId()

- void EnsureCreated(App app)

- bool IsSupported()

- SignalString Url()

- SignalString Title()

- string CurrentUrl()

- string CurrentTitle()

- void Navigate(string target)

- void LoadHtml(string html, string baseUrl)

- void Back()

- void Forward()

- void Reload()

- void Stop()

- bool CanGoBack()

- bool CanGoForward()

- bool IsLoading()

- int LastStatus()

- string LastRequest()

- string Eval(string js)

- async string EvalWithResultAsync(string js, int timeoutMs=5000){ if (!supported||handle==0){ return"";}string res=await WebViewBackend.EvalWithResultAsync(handle, js, timeoutMs);return res;}async bool EnsureReadyAsync(int timeoutMs=15000){ if (!supported||handle==0){ return false;}bool ok=await WebViewBackend.EnsureReadyAsync(handle, timeoutMs);return ok;}bool SupportsMessages(){ return WebViewBackend.HasBridge();}void AddMessageHandler(string handlerName){ if (handlerName==""){ return;}if (!created||!supported||handle==0){ pendingHandlers.Add(handlerName);return;}WebViewBackend.AddHandler(handle, handlerName);}void RemoveMessageHandler(string handlerName){ if (supported&&handle!=0){ WebViewBackend.RemoveHandler(handle, handlerName);}}string TakeMessage(){ if (!supported||handle==0){ return"";}return WebViewBackend.TakeMessage(handle);}int MessagePending(){ if (!supported||handle==0){ return 0;}return WebViewBackend.PendingMessages(handle);}int MessageDropped(){ if (!supported||handle==0){ return 0;}return WebViewBackend.DroppedMessages(handle);}void InjectScript(string js, bool atDocumentEnd){ if (js==""){ return;}if (!created||!supported||handle==0){ pendingScripts.Add(new PendingScriptItem(js, atDocumentEnd));return;}WebViewBackend.AddScript(handle, js, atDocumentEnd);}void InjectStyle(string css){ if (css==""){ return;}if (!created||!supported||handle==0){ pendingStyles.Add(css);return;}WebViewBackend.AddStyle(handle, css);}void ClearInjected(){ pendingScripts=new List<PendingScriptItem>();pendingStyles=new List<string>();if (supported&&handle!=0){ WebViewBackend.RemoveScripts(handle);}}void EvalAsync(string js){ if (supported&&handle!=0){ WebViewBackend.EvalAsync(handle, js);}}string GetCookies(string forUrl){ if (!supported||handle==0){ return"";}return WebViewBackend.GetCookies(handle, forUrl);}async string GetCookiesAsync(string forUrl, int timeoutMs=5000){ if (!supported||handle==0){ return"";}string res=await WebViewBackend.GetCookiesAsync(handle, forUrl, timeoutMs);return res;}async List<WebCookie> GetCookieListAsync(string forUrl="", int timeoutMs=5000){ if (!supported||handle==0){ return new List<WebCookie>();}List<WebCookie> res=await WebViewBackend.GetCookieListAsync(handle, forUrl, timeoutMs);return res;}async string GetCookieDetailsAsync(string forUrl, int timeoutMs=5000){ if (!supported||handle==0){ return"";}string res=await WebViewBackend.GetCookieDetailsAsync(handle, forUrl, timeoutMs);return res;}void SetCookie(string forUrl, string cookieName, string cookieValue){ if (supported&&handle!=0){ WebViewBackend.SetCookie(handle, forUrl, cookieName, cookieValue);}}void ClearCookies(){ if (supported&&handle!=0){ WebViewBackend.ClearCookies(handle);}}void ClearBrowsingData(){ if (supported&&handle!=0){ WebViewBackend.ClearData(handle);}}void SetZoom(int percent){ if (!supported||handle==0){ return;}if (percent <25){ percent=25;}if (percent> 500){ percent=500;}WebViewBackend.SetZoom(handle, percent);}void OpenDevTools(){ if (!created||handle==0){ pendingDevTools=true;return;}if (supported&&handle!=0){ WebViewBackend.OpenDevTools(handle);}}void SetDevToolsEnabled(bool enabled){ devToolsEnabled=enabled;devToolsExplicit=true;if (created&&supported&&handle!=0){ WebViewBackend.SetDevToolsEnabled(handle, enabled);}}void SetContextMenuEnabled(bool enabled){ contextMenuEnabled=enabled;contextMenuExplicit=true;if (created&&supported&&handle!=0){ WebViewBackend.SetContextMenuEnabled(handle, enabled);}}void Hide(){ if (created&&supported&&handle!=0){ WebViewBackend.SetVisible(handle, false);}}void Destroy(){ if (created&&supported&&handle!=0){ NativeLayer.Forget(handle);WebViewBackend.Destroy(handle);}handle=0;created=false;}int Render(App app, int x, int y, int w, int h){ EnsureCreated(app);if (!supported){ WebView.PaintPlaceholder(app, x, y, w, h);return handle;}WebViewBackend.SetFrame(handle, x, y, w, h);NativeLayer.Register(app, handle, x, y, w, h, WebViewBackend.ApplyClip);string pendingNav=WebViewBackend.DrainWindowNav(handle);if (pendingNav!=""){ WebViewBackend.Navigate(handle, pendingNav);}bool dirty=false;string request=WebViewBackend.LastRequest(handle);if (request!=lastSeenRequest){ lastSeenRequest=request;if (request!=""){ NavStart.Raise();}dirty=true;}bool busy=WebViewBackend.IsLoading(handle);if (busy!=lastSeenLoading){ lastSeenLoading=busy;LoadingChanged.Raise();dirty=true;}int seq=WebViewBackend.NavSeq(handle);if (seq!=lastNavSeq){ lastNavSeq=seq;url.Set(WebViewBackend.GetUrl(handle));title.Set(WebViewBackend.GetTitle(handle));NavComplete.Raise();dirty=true;}string docTitle=title.Get();if (docTitle!=lastSeenTitle){ lastSeenTitle=docTitle;TitleChanged.Raise();dirty=true;}if (dirty){ app.RequestRedraw();}return handle;}static void PaintPlaceholder(App app, int x, int y, int w, int h){ Canvas c=app.canvas;Theme t=app.theme;StyleBox s=Style.Part(app, "webview", "placeholder", "", "", "", Style.SNormal());StyleBox icon=Style.Part(app, "webview", "icon", "", "", "", Style.SNormal());StyleBox label=Style.Part(app, "webview", "label", "", "", "", Style.SNormal());c.FillRect(x, y, w, h, s.BgOr(0));c.DrawRect(x, y, w, h, s.BorderOr(0), t.borderWidth);c.DrawGlyphIn("globe", x, y-app.Scale(28), w, h, t.iconSizeLarge, icon.FgOr(0));c.DrawTextCentered(x, y+app.Scale(18), w, h, "WebView needs a native backend(Windows WebView2 / macOS WKWebView / Android system WebView)", label.FgOr(0), Style.FontFallback(app, "medium"));}static string Normalize(string target){ if (target==""){ return"";}if (WebView.HasScheme(target)){ return target;}return"https://"+target;}static bool HasScheme(string s){ int n=s.Length;for (int i=0;i+2 <n;i=i+1){ if (s[i]==':'&&s[i+1]=='/'&&s[i+2]=='/'){ return true;}}return false;}


## WebView2 (class)

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

- List<WebViewHandlerEntry> handlers;

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

- static int SlotGetAreDevToolsEnabled()

- static int SlotPutAreDevToolsEnabled()

- static int SlotGetAreDefaultContextMenusEnabled()

- static int SlotPutAreDefaultContextMenusEnabled()

- static int SlotGetSource()

- static int SlotNavigate()

- static int SlotNavigateToString()

- static int SlotAddNavigationStarting()

- static int SlotRemoveNavigationStarting()

- static int SlotAddSourceChanged()

- static int SlotRemoveSourceChanged()

- static int SlotAddNavigationCompleted()

- static int SlotRemoveNavigationCompleted()

- static int SlotExecuteScript()

- static int SlotReload()

- static int SlotCanGoBack()

- static int SlotCanGoForward()

- static int SlotGoBack()

- static int SlotGoForward()

- static int SlotStop()

- static int SlotGetDocumentTitle()

- static int SlotGetCookieManager()

- static string IidWebView2_2()

- static int SlotPutIsVisible()

- static int SlotPutBounds()

- static int SlotControllerClose()

- static int SlotGetCoreWebView2()

- static int SlotCreateController()

- static int SlotCreateCookie()

- static int SlotGetCookies()

- static int SlotAddOrUpdateCookie()

- static int SlotDeleteAllCookies()

- static int SlotCookieName()

- static int SlotCookieValue()

- static int SlotCookieDomain()

- static int SlotCookiePath()

- static int SlotCookieCount()

- static int SlotCookieAt()

- static int SlotIsSuccess()

- static int SlotWebErrorStatus()

- static int SlotArgsUri()

- static int SlotAddDocScript()

- static int SlotRemoveDocScript()

- static int SlotAddWebMessage()

- static int SlotRemoveWebMessage()

- static int SlotAddNewWindow()

- static int SlotRemoveNewWindow()

- static int SlotOpenDevTools()

- static int SlotPutZoomFactor()

- static int SlotTryGetWebMessage()

- static int SlotNewWinUri()

- static int SlotNewWinHandled()

- static string IidWebView2_13()

- static int SlotGetProfile()

- static string IidProfile2()

- static int SlotClearBrowsingData()

- static int DataKindsAllSite()

- static List<WebView2> All()

- static WebView2 Get(int h)

- static bool IsAvailable()

- static string RuntimeVersion()

- static int Create(nint hwnd, string profileId)

- nint Sink(nint invoke)

- static nint CreateHost(nint parent)

- bool Start(nint hwnd, string profileId)

- void AttachCore()

- async bool EnsureReadyAsync(int timeoutMs=15000){ if (ready){ return true;}if (initFailed){ return false;}await readyGate.Wait(timeoutMs);return ready;}long Subscribe(int slot, nint sink){ nint token=NativeMemory.Alloc(8);new Span<long>(token, 1)[0]=0;Com.Call2(core, slot, sink, token);long t=new Span<long>(token, 1)[0];NativeMemory.Free(token);return t;}void Unsubscribe(int slot, long token){ nint cell=NativeMemory.Alloc(8);new Span<long>(cell, 1)[0]=token;Com.Call1(core, slot, (nint)token);NativeMemory.Free(cell);}static string ProfileDir(string profileId){ string userEnv=Interop.EnvVar("WEBVIEW2_USER_DATA_FOLDER");if (userEnv!=""){ return userEnv;}string id=profileId;if (id==""){ id="default";}string safe="";for (int i=0;i <id.Length;i=i+1){ char ch=id[i];if (WebView2.IsSafeChar(ch)){ safe=safe+id.Substring(i, 1);}else{ safe=safe+"_";}}string root=Interop.EnvVar("LOCALAPPDATA");if (root==""){ root=Interop.EnvVar("TEMP");}return root+"\\ZanGui\\WebView2\\"+safe;}static bool IsSafeChar(char ch){ if (ch>='a'&&ch <='z'){ return true;}if (ch>='A'&&ch <='Z'){ return true;}if (ch>='0'&&ch <='9'){ return true;}if (ch=='-'||ch=='_'){ return true;}return false;}static int OnQueryInterface(nint self, nint riid, nint ppv){ if (ppv==0){ return Com.Fail();}new Span<long>(ppv, 1)[0]=self;return 0;}static int OnAddRef(nint self){ return 2;}static int OnRelease(nint self){ return 1;}static int OnEnvironment(nint self, int hr, nint result){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (Com.Ok(hr)&&result!=0){ w.env=Com.Keep(result);w.ctlSink=w.Sink((nint)WebView2.OnController);int chr=Com.Call2(w.env, WebView2.SlotCreateController(), w.host, w.ctlSink);if (!Com.Ok(chr)){ w.initFailed=true;w.gate.Signal();w.readyGate.Signal();}}else{ w.initFailed=true;w.gate.Signal();w.readyGate.Signal();}return 0;}static int OnController(nint self, int hr, nint result){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (Com.Ok(hr)&&result!=0){ w.ctrl=Com.Keep(result);w.core=Com.Get(w.ctrl, WebView2.SlotGetCoreWebView2());w.AttachCore();w.ready=true;}else{ w.initFailed=true;}w.gate.Signal();w.readyGate.Signal();return 0;}static int OnNavigationStarting(nint self, nint sender, nint args){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (args!=0){ w.lastRequest=Com.GetString(args, WebView2.SlotArgsUri());}w.loading=true;return 0;}static int OnNavigationCompleted(nint self, nint sender, nint args){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (args!=0){ int ok=Com.GetInt(args, WebView2.SlotIsSuccess());int status=Com.GetInt(args, WebView2.SlotWebErrorStatus());if (ok!=0){ w.lastStatus=200;}else{ w.lastStatus=400+status;}}if (sender!=0){ w.url=Com.GetString(sender, WebView2.SlotGetSource());w.title=Com.GetString(sender, WebView2.SlotGetDocumentTitle());}w.loading=false;w.navSeq=w.navSeq+1;return 0;}static int OnSourceChanged(nint self, nint sender, nint args){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (sender!=0){ w.url=Com.GetString(sender, WebView2.SlotGetSource());}w.navSeq=w.navSeq+1;return 0;}static int OnWebMessageReceived(nint self, nint sender, nint args){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (args!=0){ string line=Com.GetString(args, WebView2.SlotTryGetWebMessage());if (line!=""){ w.PushMessage(line);}}return 0;}static int OnNewWindowRequested(nint self, nint sender, nint args){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (args!=0){ string uri=Com.GetString(args, WebView2.SlotNewWinUri());Com.Call1(args, WebView2.SlotNewWinHandled(), (nint)1);if (uri!=""){ w.pendingWindowNav=uri;}}return 0;}static int OnDocScriptAdded(nint self, int hr, nint id){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (Com.Ok(hr)&&id!=0){ string sid=Wide.Read(id);if (sid!=""){ w.pendingScriptId=sid;w.scriptIds.Add(sid);}}w.gate.Signal();return 0;}static int OnScriptCompleted(nint self, int hr, nint json){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (Com.Ok(hr)){ w.evalResult=Wide.Read(json);}w.gate.Signal();w.evalGate.Signal();return 0;}static int OnCookiesCompleted(nint self, int hr, nint list){ WebView2 w=WebView2.Get(ComVtbl.State(self));if (w==null){ return 0;}if (Com.Ok(hr)&&list!=0){ w.cookieResult=WebView2.Serialize(list);w.cookieDetailsResult=WebView2.SerializeDetails(list);w.cookieListResult=WebView2.ExtractCookies(list);}w.gate.Signal();w.cookieGate.Signal();return 0;}static string Serialize(nint list){ int n=Com.GetInt(list, WebView2.SlotCookieCount());string outp="";for (int i=0;i <n;i=i+1){ nint cell=NativeMemory.Alloc(8);new Span<long>(cell, 1)[0]=0;int hr=Com.Call2(list, WebView2.SlotCookieAt(), i, cell);nint cookie=new Span<long>(cell, 1)[0];NativeMemory.Free(cell);if (Com.Ok(hr)&&cookie!=0){ string name=Com.GetString(cookie, WebView2.SlotCookieName());string val=Com.GetString(cookie, WebView2.SlotCookieValue());if (name!=""){ if (outp!=""){ outp=outp+"; ";}outp=outp+name+"="+val;}Com.Release(cookie);}}return outp;}static string SerializeDetails(nint list){ int n=Com.GetInt(list, WebView2.SlotCookieCount());string outp="";for (int i=0;i <n;i=i+1){ nint cell=NativeMemory.Alloc(8);new Span<long>(cell, 1)[0]=0;int hr=Com.Call2(list, WebView2.SlotCookieAt(), i, cell);nint cookie=new Span<long>(cell, 1)[0];NativeMemory.Free(cell);if (Com.Ok(hr)&&cookie!=0){ string name=Com.GetString(cookie, WebView2.SlotCookieName());string val=Com.GetString(cookie, WebView2.SlotCookieValue());string dom=Com.GetString(cookie, WebView2.SlotCookieDomain());string path=Com.GetString(cookie, WebView2.SlotCookiePath());if (name!=""){ outp=outp+name+"\t"+val+"\t"+dom+"\t"+path+"\n";}Com.Release(cookie);}}return outp;}static List<WebCookie> ExtractCookies(nint list){ List<WebCookie> result=new List<WebCookie>();if (list==0){ return result;}int n=Com.GetInt(list, WebView2.SlotCookieCount());for (int i=0;i <n;i=i+1){ nint cell=NativeMemory.Alloc(8);new Span<long>(cell, 1)[0]=0;int hr=Com.Call2(list, WebView2.SlotCookieAt(), i, cell);nint cookie=new Span<long>(cell, 1)[0];NativeMemory.Free(cell);if (Com.Ok(hr)&&cookie!=0){ string name=Com.GetString(cookie, WebView2.SlotCookieName());string val=Com.GetString(cookie, WebView2.SlotCookieValue());string dom=Com.GetString(cookie, WebView2.SlotCookieDomain());string path=Com.GetString(cookie, WebView2.SlotCookiePath());if (name!=""){ result.Add(new WebCookie(name, val, dom, path));}Com.Release(cookie);}}return result;}void SetFrame(int x, int y, int w, int h){ if (ctrl==0){ return;}if (frameSet&&x==fx&&y==fy&&w==fw&&h==fh){ return;}fx=x;fy=y;fw=w;fh=h;frameSet=true;if (host!=0){ WebView2.SetWindowPos(host, 0, x, y, w, h, 4|16);}nint rect=NativeMemory.Alloc(16);new Span<int>(rect, 1)[0]=0;new Span<int>(rect+4, 1)[0]=0;new Span<int>(rect+8, 1)[0]=w;new Span<int>(rect+12, 1)[0]=h;Com.Call1(ctrl, WebView2.SlotPutBounds(), rect);NativeMemory.Free(rect);clipSet=false;}void SetVisible(bool visible){ if (ctrl==0){ return;}if (visibleSet&&visible==this.visible){ return;}this.visible=visible;visibleSet=true;int v=0;if (visible){ v=1;}Com.Call1(ctrl, WebView2.SlotPutIsVisible(), v);if (host!=0){ int cmd=0;if (visible){ cmd=4;}WebView2.ShowWindow(host, cmd);}}void SetClip(string spec){ if (ctrl==0){ return;}if (spec.Length==0){ this.SetVisible(false);return;}if (clipSet&&spec==clipSpec){ this.SetVisible(true);return;}clipSpec=spec;clipSet=true;if (host!=0){ nint rgn=0;bool whole=false;List<string> parts=spec.Split(";");for (int i=0;i <parts.Count;i=i+1){ List<string> n=parts[i].Split(", ");if (n.Count <4){ continue;}int l=Convert.ToInt32(n[0])-fx;int t=Convert.ToInt32(n[1])-fy;int r=l+Convert.ToInt32(n[2]);int b=t+Convert.ToInt32(n[3]);if (parts.Count==1&&l <=0&&t <=0&&r>=fw&&b>=fh){ whole=true;}nint piece=WebView2.CreateRectRgn(l, t, r, b);if (piece==0){ continue;}if (rgn==0){ rgn=piece;}else{ WebView2.CombineRgn(rgn, rgn, piece, 2);WebView2.DeleteObject(piece);}}if (whole){ if (rgn!=0){ WebView2.DeleteObject(rgn);}WebView2.SetWindowRgn(host, 0, true);}else if (rgn!=0){ WebView2.SetWindowRgn(host, rgn, true);}}this.SetVisible(true);}void Navigate(string target){ if (core==0){ pendingInitUrl=target;return;}nint u=Wide.Of(target);Com.Call1(core, WebView2.SlotNavigate(), u);Wide.Free(u);}void LoadHtml(string html, string baseUrl){ if (core==0){ return;}nint h=Wide.Of(html);Com.Call1(core, WebView2.SlotNavigateToString(), h);Wide.Free(h);}void Back(){ Com.Call0(core, WebView2.SlotGoBack());}void Forward(){ Com.Call0(core, WebView2.SlotGoForward());}void Reload(){ Com.Call0(core, WebView2.SlotReload());}void StopLoading(){ Com.Call0(core, WebView2.SlotStop());}bool CanGoBack(){ return Com.GetInt(core, WebView2.SlotCanGoBack())!=0;}bool CanGoForward(){ return Com.GetInt(core, WebView2.SlotCanGoForward())!=0;}bool IsLoading(){ return loading;}string LastRequest(){ return lastRequest;}int NavSeq(){ return navSeq;}int LastStatus(){ return lastStatus;}string GetUrl(){ if (core!=0){ url=Com.GetString(core, WebView2.SlotGetSource());}return url;}string GetTitle(){ if (core!=0){ title=Com.GetString(core, WebView2.SlotGetDocumentTitle());}return title;}static void IssueOnUi(Action issue){ if (!Dispatcher.Post(issue)){ issue();}}async string EvalWithResultAsync(string js, int timeoutMs=5000){ if (core==0){ return"";}evalResult="";nint sink=this.Sink((nint)WebView2.OnScriptCompleted);evalGate=new Gate();WebView2.IssueOnUi(()=>{ nint script=Wide.Of(js);int hr=Com.Call2(core, WebView2.SlotExecuteScript(), script, sink);Wide.Free(script);if (!Com.Ok(hr)){ Console.WriteLine("[WV2]ExecuteScript hr="+Convert.ToString(hr));evalGate.Signal();}});await evalGate.Wait(timeoutMs);return evalResult;}string Eval(string js){ if (core==0){ return"";}nint sink=this.Sink((nint)WebView2.OnScriptCompleted);nint script=Wide.Of(js);evalResult="";Com.Call2(core, WebView2.SlotExecuteScript(), script, sink);Wide.Free(script);return evalResult;}static nint voidSink;

- static nint VoidSink()

- static int OnVoidCompleted(nint self, int hr, nint json)

- void EvalAsync(string js)

- int HandlerIndex(string name)

- bool AddHandler(string name)

- void RemoveHandler(string name)

- void PushMessage(string line)

- string TakeMessage()

- int PendingMessages()

- int DroppedMessages()

- string AddDocScript(string js)

- bool InjectScript(string js, bool atEnd)

- bool InjectStyle(string css)

- void ClearScripts()

- static string JsQuote(string s)

- static string ShimScript(string name)

- static string DeferToDomReady(string js)

- void SetZoom(int percent)

- void OpenDevTools()

- bool ClearBrowsingData()

- string DrainWindowNav()

- void SetDevToolsEnabled(bool enabled)

- void SetContextMenuEnabled(bool enabled)

- void ApplySettings()

- nint Cookies()

- async string GetCookiesAsync(string forUrl, int timeoutMs=5000){ nint sink=this.Sink((nint)WebView2.OnCookiesCompleted);cookieResult="";cookieGate=new Gate();WebView2.IssueOnUi(()=>{ nint cm=this.Cookies();if (cm==0){ Console.WriteLine("[WV2]Cookies()returned 0!");cookieGate.Signal();return;}nint u=0;if (forUrl!=""){ u=Wide.Of(forUrl);}int hr=Com.Call2(cm, WebView2.SlotGetCookies(), u, sink);if (u!=0){ Wide.Free(u);}Com.Release(cm);if (!Com.Ok(hr)){ Console.WriteLine("[WV2]SlotGetCookies hr="+Convert.ToString(hr));cookieGate.Signal();}});await cookieGate.Wait(timeoutMs);return cookieResult;}async string GetCookieDetailsAsync(string forUrl, int timeoutMs=5000){ nint sink=this.Sink((nint)WebView2.OnCookiesCompleted);cookieResult="";cookieDetailsResult="";cookieGate=new Gate();WebView2.IssueOnUi(()=>{ nint cm=this.Cookies();if (cm==0){ Console.WriteLine("[WV2]Cookies()returned 0!");cookieGate.Signal();return;}nint u=0;if (forUrl!=""){ u=Wide.Of(forUrl);}int hr=Com.Call2(cm, WebView2.SlotGetCookies(), u, sink);if (u!=0){ Wide.Free(u);}Com.Release(cm);if (!Com.Ok(hr)){ Console.WriteLine("[WV2]SlotGetCookies hr="+Convert.ToString(hr));cookieGate.Signal();}});await cookieGate.Wait(timeoutMs);return cookieDetailsResult;}async List<WebCookie> GetCookieListAsync(string forUrl="", int timeoutMs=5000){ nint sink=this.Sink((nint)WebView2.OnCookiesCompleted);cookieResult="";cookieDetailsResult="";cookieListResult=new List<WebCookie>();cookieGate=new Gate();WebView2.IssueOnUi(()=>{ nint cm=this.Cookies();if (cm==0){ Console.WriteLine("[WV2]Cookies()returned 0!");cookieGate.Signal();return;}nint u=0;if (forUrl!=""){ u=Wide.Of(forUrl);}int hr=Com.Call2(cm, WebView2.SlotGetCookies(), u, sink);if (u!=0){ Wide.Free(u);}Com.Release(cm);if (!Com.Ok(hr)){ Console.WriteLine("[WV2]SlotGetCookies hr="+Convert.ToString(hr));cookieGate.Signal();}});await cookieGate.Wait(timeoutMs);return cookieListResult;}string GetCookies(string forUrl){ nint cm=this.Cookies();if (cm==0){ return cookieResult;}nint sink=this.Sink((nint)WebView2.OnCookiesCompleted);nint u=0;if (forUrl!=""){ u=Wide.Of(forUrl);}Com.Call2(cm, WebView2.SlotGetCookies(), u, sink);if (u!=0){ Wide.Free(u);}Com.Release(cm);return cookieResult;}void SetCookie(string forUrl, string cookieName, string cookieValue){ nint cm=this.Cookies();if (cm==0){ return;}string host=WebView2.HostOf(forUrl);if (host!=""){ nint n=Wide.Of(cookieName);nint val=Wide.Of(cookieValue);nint d=Wide.Of(host);nint p=Wide.Of("/");nint cell=NativeMemory.Alloc(8);new Span<long>(cell, 1)[0]=0;int hr=Com.Call5(cm, WebView2.SlotCreateCookie(), n, val, d, p, cell);nint cookie=new Span<long>(cell, 1)[0];NativeMemory.Free(cell);if (Com.Ok(hr)&&cookie!=0){ Com.Call1(cm, WebView2.SlotAddOrUpdateCookie(), cookie);Com.Release(cookie);}Wide.Free(n);Wide.Free(val);Wide.Free(d);Wide.Free(p);}Com.Release(cm);}void ClearCookies(){ nint cm=this.Cookies();if (cm==0){ return;}Com.Call0(cm, WebView2.SlotDeleteAllCookies());Com.Release(cm);}static string HostOf(string url){ int n=url.Length;int start=0;int i=0;while (i+2 <n){ if (url[i]==':'&&url[i+1]=='/'&&url[i+2]=='/'){ start=i+3;i=n;}else{ i=i+1;}}string host="";i=start;while (i <n){ char ch=url[i];if (ch=='/'||ch=='?'||ch=='#'||ch==':'){ return host;}host=host+url.Substring(i, 1);i=i+1;}return host;}void Dispose(){ if (core!=0){ this.Unsubscribe(WebView2.SlotRemoveNavigationCompleted(), navToken);this.Unsubscribe(WebView2.SlotRemoveNavigationStarting(), startToken);this.Unsubscribe(WebView2.SlotRemoveSourceChanged(), srcToken);this.Unsubscribe(WebView2.SlotRemoveWebMessage(), msgToken);this.Unsubscribe(WebView2.SlotRemoveNewWindow(), winToken);Com.Release(core);core=0;}if (ctrl!=0){ Com.Call0(ctrl, WebView2.SlotControllerClose());Com.Release(ctrl);ctrl=0;}if (env!=0){ Com.Release(env);env=0;}if (host!=0){ WebView2.DestroyWindow(host);host=0;}}


## WebViewBackend (class)

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

- static void Destroy(int h)

- static void SetFrame(int h, int x, int y, int w, int hh)

- static void SetVisible(int h, bool visible)

- static void ApplyClip(int h, string spec)

- static void Navigate(int h, string url)

- static void LoadHtml(int h, string html, string baseUrl)

- static void Back(int h)

- static void Forward(int h)

- static void Reload(int h)

- static void Stop(int h)

- static bool CanGoBack(int h)

- static bool CanGoForward(int h)

- static bool IsLoading(int h)

- static int NavSeq(int h)

- static int LastStatus(int h)

- static string GetUrl(int h)

- static string GetTitle(int h)

- static string LastRequest(int h)

- static string Eval(int h, string js)

- static async string EvalWithResultAsync(int h, string js, int timeoutMs=5000){ WebView2 w=WebView2.Get(h);if (w==null){ return"";}string res=await w.EvalWithResultAsync(js, timeoutMs);return res;return WebViewBackend.zan_gui_webview_eval(h, js);return"";}static async bool EnsureReadyAsync(int h, int timeoutMs=15000){ WebView2 w=WebView2.Get(h);if (w==null){ return false;}bool ok=await w.EnsureReadyAsync(timeoutMs);return ok;return true;}static string GetCookies(int h, string url){ WebView2 w=WebView2.Get(h);if (w==null){ return"";}return w.GetCookies(url);return WebViewBackend.zan_gui_webview_get_cookies(h, url);return"";}static async string GetCookiesAsync(int h, string url, int timeoutMs=5000){ WebView2 w=WebView2.Get(h);if (w==null){ return"";}string res=await w.GetCookiesAsync(url, timeoutMs);return res;return WebViewBackend.zan_gui_webview_get_cookies(h, url);return"";}static async List<WebCookie> GetCookieListAsync(int h, string url, int timeoutMs=5000){ WebView2 w=WebView2.Get(h);if (w==null){ return new List<WebCookie>();}List<WebCookie> res=await w.GetCookieListAsync(url, timeoutMs);return res;return new List<WebCookie>();}static async string GetCookieDetailsAsync(int h, string url, int timeoutMs=5000){ WebView2 w=WebView2.Get(h);if (w==null){ return"";}string res=await w.GetCookieDetailsAsync(url, timeoutMs);return res;return"";}static void SetCookie(int h, string url, string cookieName, string cookieValue){ WebView2 w=WebView2.Get(h);if (w!=null){ w.SetCookie(url, cookieName, cookieValue);}WebViewBackend.zan_gui_webview_set_cookie(h, url, cookieName, cookieValue);}static void ClearCookies(int h){ WebView2 w=WebView2.Get(h);if (w!=null){ w.ClearCookies();}WebViewBackend.zan_gui_webview_clear_cookies(h);}static nint guiMod=0;

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

- static bool HasBridge()

- static bool AddHandler(int h, string handlerName)

- static void RemoveHandler(int h, string handlerName)

- static string TakeMessage(int h)

- static int PendingMessages(int h)

- static int DroppedMessages(int h)

- static bool AddScript(int h, string js, bool atEnd)

- static bool AddStyle(int h, string css)

- static void RemoveScripts(int h)

- static void EvalAsync(int h, string js)

- static void ClearData(int h)

- static void SetZoom(int h, int percent)

- static void OpenDevTools(int h)

- static void SetDevToolsEnabled(int h, bool enabled)

- static void SetContextMenuEnabled(int h, bool enabled)

- static string DrainWindowNav(int h)


## WebViewBootstrap (class)

- static bool installed;

- static WebViewBox hostBox;

- static App hostBoxOwner;

- static LinkWindow linkWin;

- static void Install()

- static void UseAsLinkTarget(App app, WebViewBox box)

- static bool NavigateLink(App app, string url)

- static Control MakeWebViewBox(string kind)

- [DllImport("urlmon", EntryPoint="URLDownloadToFileW")]static extern int UrlDownloadToFile(nint caller, nint url, nint file, int reserved, nint callback);

- [DllImport("ntdll", EntryPoint="RtlGetVersion")]static extern int RtlGetVersion(nint info);

- [DllImport("kernel32", EntryPoint="CreateProcessW")]static extern int CreateProcess(nint app, nint cmdline, nint procAttr, nint threadAttr, int inheritHandles, int flags, nint env, nint dir, nint startupInfo, nint procInfo);

- [DllImport("kernel32", EntryPoint="WaitForSingleObject")]static extern int WaitForSingleObject(nint handle, int ms);

- [DllImport("kernel32", EntryPoint="CloseHandle")]static extern int CloseHandle(nint handle);

- [DllImport("shell32", EntryPoint="ShellExecuteW")]static extern nint ShellExecute(nint hwnd, nint verb, nint file, nint parameters, nint dir, int show);

- static string BootstrapperUrl()

- static bool EnsureRuntime()

- static bool IsWindows7Or8()

- static bool RunInstaller(string exe)

- static void OpenUrl(string url)


## WebViewBox (class)

- WebView view;

- string startUrl;

- bool navigated;

- WebViewBox():this("")

- WebViewBox(string profileId)

- WebView View()

- void SetStartUrl(string u)

- string StartUrl()

- void Navigate(string target)

- void Back()

- void Forward()

- void Reload()

- void Stop()

- void SetZoom(int percent)

- void OpenDevTools()

- bool CanGoBack()

- bool CanGoForward()

- bool IsLoading()

- int LastStatus()

- string CurrentUrl()

- string CurrentTitle()

- void Hide()

- void Destroy()

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnPaint(App app)


## WebViewHandlerEntry (class)

- public string name;

- public string shimId;

- public WebViewHandlerEntry(string name, string shimId)


## int (delegate)

`delegate int WvResultFn(nint self, int hr, nint result);`


## int (delegate)

`delegate int WvEventFn(nint self, nint sender, nint args);`


## int (delegate)

`delegate int WvQueryInterfaceFn(nint self, nint riid, nint ppv);`


## int (delegate)

`delegate int WvRefFn(nint self);`


## int (delegate)

`delegate int WvCreateEnvFn(nint browserFolder, nint userDataFolder, nint options, nint handler);`


## int (delegate)

`delegate int WvGetVersionFn(nint folder, nint version);`


## int (delegate)

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
