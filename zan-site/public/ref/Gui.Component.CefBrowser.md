# Gui.Component.CefBrowser

> 源码: `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefBackend.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefBootstrap.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefBrowser.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefBrowserBox.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefCdp.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefControlBootstrap.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefCookies.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefFingerprint.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefHost.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefOptions.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefPage.zan`, `packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefRuntime.zan`


## CdpPendingReply (class)

- public int id;

- public CdpEventFn reply;

- public CdpPendingReply(int id, CdpEventFn reply)


## CdpSubEntry (class)

- public string name;

- public CdpEventFn handler;

- public CdpSubEntry(string name, CdpEventFn handler)


## CefArchive (class)

- public string cefVersion;

- public string chromiumVersion;

- public string platform;

- public string fileName;

- public string sha1;

- public long size;

- CefArchive()

- string Path()


## CefBackend (class)

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

- static string Error()

- static string LibraryName()

- static bool Resolve(string cefVersion)

- static List<string> Candidates(string cefVersion)

- static string FileName(string cefVersion)

- static string Extract(string file)

- static string Fingerprint(string name)

- static bool ResolveSymbols()

- static string missing="";

- static nint Sym(string name)

- static int ExecuteProcess(string runtimeDir, string cefVersion, string switches)

- static bool Init(string runtimeDir, string cefVersion, string cachePath, string helperPath, string locale, string switches, bool windowless)

- static bool Ready()

- static void Work()

- static void Shutdown()

- static int Create(nint parent, int x, int y, int w, int h, string url)

- static void Close(int h)

- static bool Alive(int h)

- static nint Window(int h)

- static void SetBounds(int h, int x, int y, int w, int hh)

- static void SetVisible(int h, bool visible)

- static void ApplyClip(int h, string spec)

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

- static string GetUrl(int h)

- static string GetTitle(int h)

- static void ExecuteJs(int h, string code, string scriptUrl, int startLine)

- static int CdpSend(int h, string method, string paramsJson)

- static int CdpPending(int h)

- static int CdpDropped(int h)

- static bool CdpAttached(int h)

- static string CdpTake(int h)

- static void SetPopupPolicy(int h, int policy)

- static string TakePopupUrl(int h)

- static void SetZoom(int h, double level)

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

- [DllImport("crt", EntryPoint="exit")]static extern void CrtExit(int code);

- static void RunHelper()

- static string LocalRuntimeDir()

- static async bool StartAsync()

- static async bool StartAsyncWith(CefOptions options)

- static async bool StartAsyncWithHelper(string helperPath)

- static DownloadJob NewRuntimeJob()

- static bool FinishStart(string helperPath)

- static string Progress(string archiveFileName)


## CefBrowser (class)

- [DllImport("user32", EntryPoint="SetFocus")]static extern nint Win32SetFocus(nint hwnd);

- [DllImport("user32", EntryPoint="GetFocus")]static extern nint Win32GetFocus();

- int handle;

- bool created;

- bool supported;

- int lastNavSeq;

- string pendingUrl;

- bool guiHadFocus;

- string lastSeenTitle;

- bool lastSeenLoading;

- CefCdp cdpRouter;

- int popupPolicy;

- CefPopupFn popupSink;

- CefCookies cookieApi;

- CefPage pageApi;

- CefFingerprint fpApi;

- SignalString url;

- SignalString title;

- UiEvent NavComplete;

- UiEvent NavStart;

- UiEvent TitleChanged;

- UiEvent LoadingChanged;

- CefBrowser()

- bool IsSupported()

- int Handle()

- SignalString Url()

- SignalString Title()

- string CurrentUrl()

- string CurrentTitle()

- void EnsureCreated(App app, int x, int y, int w, int h)

- void Navigate(string target)

- void LoadHtml(string html)

- void Back()

- void Forward()

- void Stop()

- void Reload()

- void ReloadIgnoreCache()

- bool CanGoBack()

- bool CanGoForward()

- bool IsLoading()

- int LastStatus()

- int LastErrorCode()

- void ExecuteJs(string code)

- void SetFocus(bool focus)

- void SyncFocus(App app)

- int SendCdp(string method, string paramsJson)

- int Evaluate(string expression)

- int CdpPending()

- int CdpDropped()

- bool CdpAttached()

- void SetZoom(double level)

- double Zoom()

- void ZoomIn()

- void ZoomOut()

- void ResetZoom()

- void Find(string text, bool forward, bool matchCase, bool findNext)

- void StopFind(bool clearSelection)

- void Print()

- void ShowDevTools()

- void CloseDevTools()

- void AllowPopupWindows()

- void BlockPopups()

- void OnPopup(CefPopupFn handler)

- void SetPopupPolicy(int policy, CefPopupFn handler)

- string TakeCdpMessage()

- CefCdp Cdp()

- CefCookies Cookies()

- CefPage Page()

- CefFingerprint Fingerprint()

- void Hide()

- void Destroy()

- int Render(App app, int x, int y, int w, int h)

- static void PaintPlaceholder(App app, int x, int y, int w, int h)

- static string PlaceholderText()

- static string Normalize(string target)

- static bool HasScheme(string s)

- static string UrlEscape(string s)


## CefBrowserBox (class)

- CefBrowser view;

- string startUrl;

- bool navigated;

- CefBrowserBox()

- CefBrowser View()

- void SetStartUrl(string u)

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

- void Destroy()

- override string Kind()

- override List<PropSpec> Props()

- override List<string> Events()

- override void BindEvent(string evt, Action a)

- override void BindEventS(string evt, ControlEvent a)

- override void OnPaint(App app)


## CefCdp (class)

- CefBrowser web;

- List<CdpSubEntry> subs;

- List<CdpPendingReply> pending;

- CdpEventFn anyHandler;

- int dispatched;

- int unrouted;

- CefCdp(CefBrowser browser)

- void On(string eventName, CdpEventFn handler)

- void Off(string eventName)

- void OnAny(CdpEventFn handler)

- int Send(string method, string paramsJson, CdpEventFn onResult)

- int Enable(string domain)

- int Disable(string domain)

- int Dispatched()

- int Unrouted()

- void Pump()

- void Dispatch(string msg)

- static string Raw(string json, string key)

- static string Field(string json, string key)

- static int IntField(string json, string key)

- static int KeyStart(string json, int endQuote)

- static int SkipWs(string json, int i)

- static string ValueAt(string json, int start)

- static string Unescape(string s)


## CefControlBootstrap (class)

- static bool installed;

- static void Install()

- static Control MakeCefBrowserBox(string kind)


## CefCookies (class)

- CefCdp cdp;

- bool networkOn;

- CefCookies(CefCdp channel)

- void EnsureNetwork()

- int GetAll(CdpEventFn onResult)

- int GetForUrl(string url, CdpEventFn onResult)

- int Set(string name, string value, string domain, string path, bool secure)

- int SetPersistent(string name, string value, string domain, string path, bool secure, long expiresUnixSeconds)

- int SetForUrl(string url, string name, string value)

- int SetJson(string cookieParamJson)

- int Delete(string name, string domain)

- int DeleteForUrl(string name, string url)

- int Clear()

- static string Param(string name, string value, string domain, string path, bool secure, string url, long expires)


## CefFingerprint (class)

- CefCdp cdp;

- string userAgent;

- string acceptLanguage;

- string locale;

- List<string> languages;

- string timezone;

- string platform;

- string platformVersion;

- string brand;

- string brandVersion;

- string brandFullVersion;

- string architecture;

- string model;

- bool mobile;

- int screenWidth;

- int screenHeight;

- int scalePercent;

- int hardwareConcurrency;

- int deviceMemory;

- bool hideWebdriver;

- string webglVendor;

- string webglRenderer;

- int canvasNoise;

- int seed;

- string scriptId;

- string originalUa;

- CefFingerprint(CefCdp channel)

- void UseWindowsChrome(string version)

- void Apply()

- void Reset()

- void SendUserAgent()

- string Metadata()

- string Script()

- string LanguagesJs()

- string WebglJs()

- string CanvasJs()

- static string Ratio(int percent)

- static string Major(string version)


## CefHost (class)

- static bool started;

- static bool stopped;

- static string runtimeDir="";

- static string cefVersion="";

- static string lastError="";

- static string profileDir="";

- static long profileLock=0;

- static bool IsRunning()

- static string Error()

- static void Fail(string msg)

- static string RuntimeDir()

- static string CefVersion()

- static string ChromeVersion()

- static string ReadyMarker()

- static string MarkerVersion(string dir)

- static int MaxProfiles()

- static string ProfileLockName()

- static string ProfileRoot(string dir)

- static string ProfileDir(string dir)

- static long TryTake(string dir)

- static int PreferredSlot(int max)

- static string Switches()

- static string DefaultHelper()

- static string MacHelperFor(string exe)

- static string ParentDir(string path)

- static bool Start(string dir, string helperPath)

- static void Work()

- static void Shutdown()


## CefHttpEndpoint (class)

- string host;

- int port;

- bool tls;

- string prefix;

- CefHttpEndpoint()


## CefOptions (class)

- string runtimeDir;

- string cacheRoot;

- string mirrorBase;

- string archivePath;

- string profileDir;

- string driverPath;

- string switches;

- bool disableDirectComposition;

- string locale;

- string helperPath;

- bool downloadUi;

- static CefOptions current;

- CefOptions()

- static void Use(CefOptions options)

- static CefOptions Current()

- static string Pick(string configured, string envName)

- static string RuntimeDir()

- static string CacheRoot()

- static string MirrorBase()

- static string ArchivePath()

- static string ProfileDir()

- static string DriverPath()

- static string Locale()

- static string HelperPath()

- static bool DownloadUi()

- static string Switches()

- static bool NoDcomp()

- static string Join(string left, string right)


## CefPage (class)

- CefCdp cdp;

- bool pageOn;

- bool dialogAccept;

- bool dialogAuto;

- CefPage(CefCdp channel)

- void EnsurePage()

- void AllowDownloads(string dir, CdpEventFn onEvent)

- void DenyDownloads()

- void CancelDownload(string guid)

- void AutoDismissDialogs(bool accept)

- void AnswerDialog(bool accept, string promptText)

- void OnConsole(CdpEventFn onEvent)

- void OnException(CdpEventFn onEvent)

- void OnNetwork(CdpEventFn onEvent)

- void SetUserAgent(string ua)

- void SetExtraHeaders(string headersJson)


## CefRuntime (class)

- static string IndexHost()

- static string IndexPath()

- static string ArchiveType()

- static string ReadyMarker()

- static string lastError;

- static string Error()

- static void Fail(string msg)

- static string Platform()

- static bool LegacyWindows()

- static string CurrentBranch()

- static string LegacyBranch()

- static string VersionPrefix()

- static CefArchive Pinned(string version, string chromium, string platform, string sha1, long size)

- static CefArchive FindPinned(string platform, string versionPrefix)

- static CefArchive FindPinnedExact(string platform, string version)

- static string CacheRoot()

- static string DownloadDir()

- static string RuntimeDir(string cefVersion)

- static string FrameworkSubdir()

- static string ResourcesDir(string dir)

- static bool IsReady(string dir)

- static string Installed()

- static bool CleanCache()

- static string BaseUrl()

- static string BasePath(string path)

- static CefHttpEndpoint Endpoint()

- static string ArchiveUrl(CefArchive a)

- static ExternalCallPolicy NetworkPolicy(int responseBytes, int totalMs)

- static ExternalCallPolicy IndexPolicy()

- static ExternalCallPolicy ArchivePolicy()

- static bool IsSecureEndpoint(CefHttpEndpoint endpoint)

- static string DownloadEscapeHint()

- static string Sha1EscapeHint()

- static async bool FetchIndexAsync(string destPath)

- static CefArchive SelectArchive(string index, string platform, string versionPrefix)

- static CefArchive ReadVersion(string text, string platform, string versionPrefix)

- static bool OtherPlatform(string text, string platform)

- static string PrefixNote(string versionPrefix)

- static CefArchive ReadLocalArchive(string path)

- static async CefArchive ResolveAsync()

- static async string EnsureAsync()

- static string pendingDir;

- static string pendingVersion;

- static DownloadJob NewEnsureJob()

- static bool PrepareJob(DownloadJob job)

- static bool InstallItem(DownloadItem it)

- static string EnsuredDir()

- static string EnsureJobError(DownloadJob job)

- static async bool DownloadArchiveAsync(CefArchive a, string archive)

- static string Progress(string fileName)

- static bool Install(string archive, string dir, string cefVersion)

- static void StageResources(string dir)

- static void CopyTree(string src, string dst)

- static void MarkExecutables(string dir)

- static void MarkExecutablesIn(string dir)

- static string UrlEncode(string name)

- static string SafeVersion(string v)

- static bool VersionNewer(string a, string b)

- static int ParseInt(string s)

- static string Slashes(string p)

- static string ParentDir(string path)

- static int PrevBrace(string s, int from)

- static int MatchBrace(string s, int open)


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

`delegate void CefPopupFn(string url);`


## void (delegate)

`delegate void CdpEventFn(string json);`
