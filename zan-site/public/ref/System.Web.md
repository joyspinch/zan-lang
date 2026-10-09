# System.Web

> 源码: `packages/Zan.Web/src/System/Web/ApiDocs.zan`, `packages/Zan.Web/src/System/Web/Attributes.zan`, `packages/Zan.Web/src/System/Web/Controller.zan`, `packages/Zan.Web/src/System/Web/HttpContext.zan`, `packages/Zan.Web/src/System/Web/Menu.zan`, `packages/Zan.Web/src/System/Web/Router.zan`, `packages/Zan.Web/src/System/Web/Security.zan`, `packages/Zan.Web/src/System/Web/StaticFiles.zan`, `packages/Zan.Web/src/System/Web/Validate.zan`, `packages/Zan.Web/src/System/Web/View.zan`, `packages/Zan.Web/src/System/Web/WebApp.zan`, `packages/Zan.Web/src/System/Web/WebWs.zan`, `packages/Zan.Web/src/System/Web/WsSession.zan`, `stdlib/System/Web/DesignerHtml.zan`, `stdlib/System/Web/Html.zan`, `stdlib/System/Web/HtmlScope.zan`


## ApiDocs (class)

- static string title="API";

- static void Title(string name)

- static void Mount(WebApp app, string path)

- static void Mount(WebApp app)

- static void MountSpec(WebApp app, string path)

- static async void Spec(HttpContext ctx)

- static async void Page(HttpContext ctx)

- static string SpecJson(WebApp host)

- static string OpenApiPath(string pattern)

- static JsonValue Operation(Route rt)

- static JsonValue Responses(Route rt)

- static JsonValue EnvelopeSchema()

- static JsonValue ErrorResponse(string desc, string code, string msg)

- static JsonValue ParamJson(ApiParam p)

- static string Tag(Route rt)

- static string Html()

- static string Shell()

- static string Css()

- static string Js()


## ApiEnvelope (class)

- string code;

- string msg;

- JsonValue data;


## ApiError (class)

- int status;

- string code;

- ApiError(int status, string code, string message)

- int Status()

- string Code()

- static string Reason(int status)


## ApiErrorBody (class)

- string code;

- string msg;


## ApiParam (class)

- string name;

- string type;

- bool required;

- string def;

- string desc;

- string source;

- ApiParam(string name, string type, bool required, string def, string desc, string source)

- string Name()

- string Type()

- bool Required()

- string Default()

- string Desc()

- string Source()

- string JsonType()


## Attribute (class)


## Controller (class)

- HttpContext ctx;

- string uid;

- string __viewKey;

- void __Bind(HttpContext c)

- bool __Before()

- async bool __BeforeAsync()

- virtual void __UseReadOnly()

- virtual async bool OnBeforeAsync()

- void __After()

- async void __AfterAsync()

- virtual async void OnAfterAsync()

- virtual async bool __TxBegin()

- virtual async void __TxCommit()

- virtual async void __TxRollback()

- virtual bool OnBefore()

- virtual void OnAfter()

- HttpContext Ctx()

- bool IsLogin()

- string In(string name)

- string InRaw(string name)

- string InText(string name)

- int InInt(string name, int def)

- long InLong(string name, long def)

- double InDouble(string name, double def)

- bool InBool(string name, bool def)

- bool HasIn(string name)

- string Param(string name)

- string Body()

- string Need(string name, string label)

- string NeedText(string name, string label)

- int NeedInt(string name, string label)

- long NeedLong(string name, string label)

- double NeedDouble(string name, string label)

- bool NeedBool(string name, string label)

- bool WantsJson()

- ListQuery Paged(string defOrder, int defLimit)

- void Ok(string data)

- void Fail(int status, string code, string msg)

- void Json(string json)

- void Text(string text)

- void Html(string html)

- void __SetView(string key)

- void View(ViewData data)

- void Fragment(ViewData data)

- void FragmentOf(string name, ViewData data)

- void ViewOf(string name, ViewData data)


## Csrf (class)

- static string CookieName="csrf_token";

- static string HeaderName="X-CSRF-Token";

- static string FormName="_csrf";

- static List<string> skips;

- static void Skip(string prefix)

- static bool Skipped(string path)

- static bool Guard(HttpContext ctx)

- static string NewToken()

- static bool ConstantTimeEquals(string a, string b)

- static bool OriginGuard(HttpContext ctx)

- static bool OriginCheck(HttpContext ctx)

- static bool SameAuthority(string origin, string host)

- static string TrimDefaultPort(string auth, bool isHttps)


## CustomAttribute (class)

- bool IsMenu{ get set}

- =false;

- CustomAuthorization Authorization{ get set}

- string Component{ get set}

- string Icon{ get set}

- int ApiMax{ get set}

- =1000;

- ContentTypes ContentType{ get set}

- =ContentTypes.ApplicationJson;

- string Lock{ get set}

- ="";

- bool Upload{ get set}

- =false;

- PermBit Perm{ get set}

- =PermBit.Unknown;

- CustomAttribute(CustomAuthorization customAuthorization=CustomAuthorization.None, bool customMenu=false, string component="vlist/index.vue", string icon="mdi:antenna", int apiMax=1000, ContentTypes contentType=ContentTypes.ApplicationJson){ this.IsMenu=customMenu;this.Authorization=customAuthorization;this.Component=component;this.Icon=icon;this.ApiMax=apiMax;this.ContentType=contentType;}


## DescriptionAttribute (class)

- string text;

- DescriptionAttribute(string text)


## DesignerHtml (class)

- static bool IsDesignDoc(string html)

- static string FromJsonDoc(string json)

- static bool IsDefaultHead(List<WNode> list, string docName)

- static string ToJsonDoc(string html)

- static bool IsPlacementKey(string key)

- static bool HasNodeAttr(WNode n, string name)

- static bool HasPlacementAttrs(WNode n)

- static bool HasPlacementKeys(JsonValue o)

- static void AppendContent(StringBuilder b, JsonValue content, JsonValue kids, int depth)

- static string FieldTag(JsonValue o)

- static bool HasInlineKids(JsonValue kids)

- static void FieldHtml(StringBuilder b, JsonValue o, int depth)

- static void FieldHtml(StringBuilder b, JsonValue o, int depth, bool compact)

- static JsonValue FieldJson(WDoc doc, WNode n)

- static void PutAttr(JsonValue o, string suffix, string v)

- static void AppendKey(StringBuilder b, JsonValue v, string key, int mode)

- static string KebabKey(string s)

- static string Unkebab(string s)

- static bool IsNumeric(string s)

- static string EscapeAttr(string s)

- static bool TextIsHtmlSafe(string s)

- static bool IsTagName(string s)

- static string KindTag(string kind)

- static string TagKind(string tag)

- static string InputTypeKind(string ty)

- static string KindInputType(string kind)


## Filter (class)

- static bool IsSpace(string c)

- static string Clean(string s)

- static string Html(string s)

- static int DigitValue(int c)

- static int ToInt(string s, int def)

- static long ToLong(string s, long def)

- static double ToDouble(string s, double def)

- static bool ToBool(string s, bool def)


## Hooks (class)

- List<HookFn> before;

- List<AfterFn> after;

- Hooks()

- void Before(HookFn fn)

- void After(AfterFn fn)

- bool RunBefore(HttpContext ctx)

- void RunAfter(HttpContext ctx, long elapsedUs)


## HtmlParser (class)

- static JsonValue pendingHeadAttr;

- static bool inHead;

- static WDoc Parse(string html, string baseDir)

- static WOpenResult OpenTag(string html, int i, List<string> an, List<string> av)

- static void HandleOpen(List<WFrame> st, WDoc doc, string name, List<string> an, List<string> av, bool selfClose, string baseDir)

- static WFrame Frame(WDoc doc, int rec, string tag, int mode)

- static int PushNode(WDoc doc, string tag, int parent, List<string> an, List<string> av)

- static void CloseTag(List<WFrame> st, WDoc doc, string name)

- static void CloseTop(List<WFrame> st, WDoc doc)

- static void FeedText(List<WFrame> st, string raw)

- static void Flush(WDoc doc, WFrame f, bool nextInline)


## HtmlScope (class)

- static string Styles(string css, string zs)

- static int ScopeAt(string css, int i, string zs, StringBuilder outp)

- static string ScopePrelude(string sel, string zs)

- static string ScopeOne(string piece, string zs)

- static string ScopeCompound(string chunk, string zs)

- static int SkipNoise(string s, int i)

- static int MatchChunk(string s, int open)

- static int MatchBrace(string s, int open)

- static bool HasClsToken(string chunk, string zs)

- static int FindFrom(string s, string needle, int start)

- static bool StartsWithAt(string s, int at, string needle)

- static bool NameChar(string c)

- static bool Alnum(string c)


## HtmlText (class)

- static string EventKind(string suffix)

- static string Attr(List<string> an, List<string> av, string name)

- static string NodeAttr(List<WAttr> attrs, string name)

- static List<string> SplitClass(string v)

- static bool StartsWith(string s, string p)

- static string CollapseSpace(string s)

- static string TrimLead(string s)

- static string TrimTrail(string s)

- static string DecodeEntities(string s)

- static int ParseIntOr(string s, int dflt)

- static int ParseHex(string s)

- static bool IsAlpha(int c)

- static bool IsNameChar(int c)

- static bool IsSpaceChar(int c)

- static int FindFrom(string s, string sub, int from)

- static int FindCloseTag(string s, string tag, int from)

- static bool IsInlineTag(string t)

- static bool IsBlockTag(string t)

- static bool IsVoidTag(string t)


## HttpContext (class)

- HttpRequest request;

- string method;

- string path;

- string remoteIp;

- StrMap routeParams;

- StrMap query;

- StrMap form;

- string uid;

- string routeAction;

- string routePattern;

- int routeSlot;

- string uploadPath;

- long uploadSize;

- JsonValue jsonBody;

- bool jsonParsed;

- int status;

- string statusText;

- string contentType;

- string body;

- byte[]bodyBytes;

- int bodyBytesLen;

- List<string> headers;

- bool ended;

- TcpClient conn;

- bool hijacked;

- bool wasHijacked;

- long frames;

- long framesBytes;

- HttpContext(HttpRequest req, string remoteIp)

- string RemoteIp()

- long RemoteIpLong()

- static void ParseFormBody(HttpRequest req, HttpContext ctx)

- static void ParseMultipartFields(HttpRequest req, HttpContext ctx)

- static string DispositionName(string head)

- static int FindFrom(string hay, string needle, int from)

- static int FindNoCaseFrom(string hay, string needle, int from)

- string Param(string name)

- string Query(string name)

- string Form(string name)

- string Body()

- JsonValue JsonBody()

- string InputString(string name)

- int InputInt(string name, int dflt)

- long InputLong(string name, long dflt)

- bool InputBool(string name, bool dflt)

- static int ParseIntStrict(string v, int dflt)

- static bool AsciiEq(string a, string b)

- static long ParseLongStrict(string v, long dflt)

- string Header(string name)

- string Cookie(string name)

- bool IsAjax()

- string InRaw(string name)

- string In(string name)

- string InText(string name)

- int InInt(string name, int def)

- long InLong(string name, long def)

- double InDouble(string name, double def)

- bool InBool(string name, bool def)

- bool HasIn(string name)

- HttpContext Status(int code, string text)

- static string HeaderSafe(string s)

- HttpContext SetHeader(string name, string val)

- HttpContext SetCookie(string name, string val, int maxAgeSeconds)

- HttpContext SetCookieJs(string name, string val, int maxAgeSeconds)

- static string SecureFlag(HttpContext ctx)

- void Html(string html)

- void Text(string text)

- void Binary(string mime, byte[]data, int len)

- bool IsBinary()

- bool StatusForbidsBody()

- int BodyLength()

- void Json(string json)

- void Download(string fileName, string mime, string body)

- void Api(string code, string msg, string data)

- void Redirect(string url)

- void __Attach(TcpClient client)

- nint ClientSock()

- HttpDeadlineToken deadlineSlot;

- int streamIdleMs;

- HttpFramer framer;

- HttpDeadlineToken DeadlineSlot()

- void __SetDeadlineSlot(HttpDeadlineToken slot, int idleMs)

- void __SetFramer(HttpFramer framer)

- void TouchDeadline(int timeoutMs)

- bool Hijacked()

- bool MetricsExcluded()

- long Frames()

- long FrameBytes()

- async bool SseOpen()

- async bool SendAllTcp(string data)

- async bool SseSend(string name, string data)

- async bool SsePing()

- bool IsWebSocketRequest()

- string BuildResponse(bool keepAlive)

- static string Itoa(int v)

- static string Digit(int d)

- static bool StartsWith(string s, string prefix)

- static void ParsePairs(string s, StrMap into)


## HttpDeleteAttribute (class)


## HttpGetAttribute (class)


## HttpPatchAttribute (class)


## HttpPostAttribute (class)


## HttpPutAttribute (class)


## ListQuery (class)

- int page;

- int limit;

- string order;

- bool desc;

- string kw;

- ListQuery()

- int Page()

- int Limit()

- string Order()

- bool Desc()

- string Kw()

- int Skip()

- int Pages(int total)

- string QueryString()

- static ListQuery From(HttpContext ctx, string defOrder, int defLimit, int maxLimit)


## Listing (class)

- static string Json(string itemsJson, int total, ListQuery q)


## LockLease (class)

- string key;

- long owner;

- LockLease(string key, long owner)


## LockManager (class)

- SharedTable table;

- long ownerSequence;

- long leaseMs;

- LockManager()

- static SharedTable NewTable()

- static LockManager Owned()

- static LockManager Attach(long osHandle)

- long OsHandle()

- static string KeyFor(Route route, string principal)

- LockLease TryAcquire(string key)

- void Release(LockLease lease)


## MenuBuilder (class)

- static StrMap labels=new StrMap();

- static List<string> sections=new List<string>();

- static void Section(string segment, string label)

- static async List<MenuNode> ForUser(WebApp app, string uid, string activePath)

- static async string LandingFor(WebApp app, string uid)

- static async string JsonFor(WebApp app, string uid, string activePath)

- static void Fill(List<StrMap> rows, List<MenuNode> nodes)

- static string GroupOf(string path)

- static int OrderOf(string path)

- static List<string> Body(string pattern)

- static string Label(string s)

- static bool Matches(string pattern, string path)

- static void Sort(List<MenuNode> list)

- static bool Before(MenuNode a, MenuNode b)


## MenuNode (class)

- string title;

- string path;

- string group;

- string icon;

- bool active;

- int groupOrder;

- string order;

- MenuNode()

- string Title()

- string Path()

- string Group()

- string Icon()

- bool Active()


## MenuNodeDoc (class)

- string title;

- string path;

- string group;

- string icon;

- bool active;


## NonActionAttribute (class)


## RateLimiter (class)

- SharedTable table;

- int windowMs;

- RateLimiter(int windowMs)

- static SharedTable NewTable()

- static RateLimiter Owned(int windowMs)

- static RateLimiter Attach(long osHandle, int windowMs)

- long OsHandle()

- bool Allow(string key, int limit)

- int CountOf(string key)


## ReadOnlyAttribute (class)


## Route (class)

- string method;

- string pattern;

- List<string> segs;

- HttpHandler handler;

- string key;

- string action;

- string title;

- int rateLimit;

- string rateScope;

- bool needsLogin;

- bool needsAuth;

- string lockScope;

- bool streamUpload;

- bool inMenu;

- List<ApiParam> docParams;

- StrMap meta;

- int idx;

- Route(string method, string pattern, HttpHandler handler)

- Route Named(string action)

- Route Title(string title)

- Route Limit(int maxPerWindow)

- Route LimitBy(int maxPerWindow, string scope)

- Route RateBy(string scope)

- Route Login()

- Route Auth()

- Route Lock(string scope)

- Route Upload()

- Route Menu()

- Route Meta(string key, string val)

- Route Param(string name, string type, bool required, string def, string desc, string source)

- string MetaGet(string key)

- string Key()


## RouteAttribute (class)

- string template;

- RouteAttribute(string template)


## RouteHitDoc (class)

- string route;

- long count;

- long avg_us;


## RouteStatEntry (class)

- public string key;

- public long hash;

- public RouteStatEntry(string key, long hash)


## Router (class)

- List<Route> statics;

- List<Route> dynamics;

- Dictionary <string, Route> staticIndex;

- List<int> hashSlots;

- int hashMask;

- List<Route> flat;

- List<RouteStatEntry> statEntries;

- SharedTable routeStats;

- Router()

- int Count()

- Route Add(string method, string pattern, HttpHandler handler)

- void HashRouteKeys()

- SharedTable NewStatsTable()

- void SeedStats(SharedTable t)

- void InitStatsShared()

- long StatsOsHandle()

- void AttachStats(long osHandle)

- void RecordHit(int idx, long us)

- List<Route> All()

- List<RouteHitDoc> Stats()

- string StatsJson()

- Route Get(string pattern, HttpHandler handler)

- Route Post(string pattern, HttpHandler handler)

- Route Put(string pattern, HttpHandler handler)

- Route Delete(string pattern, HttpHandler handler)

- static int HashKey(string method, string path)

- void RebuildIndex()

- Route FindStatic(string method, string path)

- Route Match(HttpContext ctx)

- bool PathExists(string path)

- static bool IsStatic(string pattern)

- static bool MatchSegs(List<string> pat, List<string> segs, StrMap into)

- static List<string> SplitPath(string path)


## RowList (class)

- string name;

- List<StrMap> rows;

- RowList(string name)


## Sessions (class)

- StrMap tokens;

- Sessions()

- string Issue(string uid, int nowSeconds)

- string UserOf(string token)


## StaticFiles (class)

- static string prefix="";

- static string root="";

- static int maxAge=86400;

- static const int MaxFileBytes=4*1024*1024;

- static List<StaticMount> mounts=new List<StaticMount>();

- static void Mount(WebApp app, string urlPrefix, string dir)

- static void MaxAge(int seconds)

- static bool Serve(HttpContext ctx)

- static bool ServeFile(HttpContext ctx, string rel, string rootDir, string urlPrefix, int ageSeconds)

- static void ServeStream(HttpContext ctx, string rel, Stream file)

- static void ServeStreamMount(HttpContext ctx, string rel, Stream file, string urlPrefix, int ageSeconds)

- static bool TooLarge(HttpContext ctx, long length)

- static void Answer(HttpContext ctx, string rel, byte[]data, int size)

- static void AnswerMount(HttpContext ctx, string urlPrefix, int ageSeconds, string rel, byte[]data, int size)

- static string Relative(string path)

- static string RelativeTo(string path, string urlPrefix)

- static bool IsSafe(string rel)

- static string ContentType(string rel)

- static string Extension(string rel)


## StaticMount (class)

- string prefix;

- string root;

- int maxAge;

- StaticMount(string prefix, string root, int maxAge)


## StrMap (class)

- Dictionary <string, string> map;

- StrMap()

- int Count()

- void Set(string key, string val)

- bool Has(string key)

- string Get(string key)

- string GetOr(string key, string def)

- void Clear()


## VNode (class)

- int kind;

- string text;

- List<VNode> body;

- List<VNode> elseBody;

- VNode(int kind, string text)


## Validator (class)

- List<string> errors;

- Validator()

- Validator Require(string val, string field)

- bool Ok()

- Validator MaxLen(string val, int max, string field)

- Validator MinLen(string val, int min, string field)

- Validator IsInt(string val, string field)

- Validator OneOf(string val, string allowedCsv, string field)

- static bool ContainsStr(string s, string pat)

- string ErrorsJson()

- static string SafeFileName(string name)

- static bool ExtAllowed(string name, string allowedCsv)


## View (class)

- Dictionary <string, List<VNode>> compiled;

- string dir;

- bool devReload;

- View()

- static View LoadDir(string dir)

- void LoadAll()

- void Put(string key, string body)

- void LoadRec(string d, string prefix)

- string Render(string name, ViewData data)

- string RenderPage(string name, ViewData data)

- static string RenderStr(string tpl, ViewData data, StrMap row)

- static List<VNode> Compile(string tpl)

- static void Emit(List<VNode> nodes, ViewData data, StrMap row, List<VNode> content, StringBuilder sb)

- static string Lookup(string key, ViewData data, StrMap row)

- static bool HasVar(string key, ViewData data, StrMap row)

- static string HtmlEscape(string s)

- static int Find(string hay, string needle, int from)

- static int FindTag(string tpl, string closeTag, int from)

- static int FindElse(string tpl, int from, int end)

- static string Trim(string s)


## ViewData (class)

- StrMap vars;

- List<RowList> lists;

- ViewData()

- ViewData Set(string key, string val)

- List<StrMap> AddList(string name)

- List<StrMap> ListOf(string name)


## WAttr (class)

- string name;

- string val;

- WAttr(string name, string val)


## WDoc (class)

- List<WNode> nodes;

- string css;

- List<WNode> headNodes;


## WFrame (class)

- int rec;

- string tag;

- int mode;

- string pending;

- bool prevInline;

- string label;


## WItem (class)

- int kid;

- string text;


## WNode (class)

- string tag;

- int parent;

- List<WAttr> attrs;

- List<WItem> items;

- string text;


## WOpenResult (class)

- string name;

- int next;

- bool selfClose;


## WebApp (class)

- [DllImport("crt")]static extern long time(nint ptr);

- [DllImport("crt")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern long fwrite(string buf, long size, long count, nint fp);

- [DllImport("crt")]static extern int fclose(nint fp);

- string host;

- int port;

- bool running;

- TcpListener listener;

- Router router;

- Hooks hooks;

- View views;

- Sessions sessions;

- RateLimiter limiter;

- LockManager locks;

- AuthFn authResolver;

- PermissionFn permissionResolver;

- ServerMetrics metrics;

- int maxBodyBytes;

- int maxUploadBytes;

- int requestTimeoutMs;

- int maxConnections;

- string uploadDir;

- AtomicInt uploadSeq;

- string loginPath;

- int globalLimit;

- AtomicInt totalRequests;

- AtomicInt activeConnections;

- bool reusePort;

- static WebApp active;

- WebApp(string host, int port)

- string Host()

- int Port()

- WebApp Views(string dir)

- WebApp MaxBody(int bytes)

- WebApp MaxUpload(int bytes)

- WebApp Timeout(int ms)

- WebApp MaxConnections(int n)

- WebApp UploadDir(string dir)

- WebApp GlobalLimit(int perSecond)

- WebApp LoginPath(string path)

- WebApp ReusePort(bool on)

- void InitStats()

- static const string ShareRate="WEB_RATE";

- static const string ShareLock="WEB_LOCK";

- static const string ShareRoute="WEB_ROUTE";

- void InitShared()

- bool AttachShared()

- RateLimiter Limiter()

- LockManager Locks()

- WebApp Before(HookFn fn)

- WebApp After(AfterFn fn)

- WebApp AuthResolver(AuthFn fn)

- WebApp PermissionResolver(PermissionFn fn)

- Route Map(string method, string pattern, HttpHandler h)

- Route Get(string pattern, HttpHandler h)

- Route Post(string pattern, HttpHandler h)

- Route Put(string pattern, HttpHandler h)

- Route Delete(string pattern, HttpHandler h)

- string RenderPage(string name, ViewData data)

- string RenderFragment(string name, ViewData data)

- static int NowSeconds()

- static string Redact(string s)

- ServerBanner Banner(int procs)

- async int Start()

- void Stop()

- static void SetActive(WebApp app)

- static async void ServeSock(nint clientSock)

- async bool RefuseOverCapacity(nint clientSock)

- async void HandleConnection(nint clientSock)

- async void HandleConnectionInner(nint clientSock)

- async string StreamBodyToFile(nint sock, HttpFramer framer, HttpRequest request)

- static byte[]MakeBuffer(int size)

- async string Dispatch(HttpContext ctx, Route route, bool keepAlive)

- async bool LimitExceeded(HttpContext ctx, Route route)

- async string DispatchInner(HttpContext ctx, Route route, bool keepAlive, long startUs)

- bool WantsPage(HttpContext ctx)

- async bool Allow(string uid, string action)

- async string AuthUser(HttpContext ctx)

- static string ErrorJson(string code, string msg)

- string StatusJson()

- string MetricsJson()

- string MetricsSeriesJson(int seconds)

- static string SimpleResponse(int code, string text, string msg)

- static int FindHeaderEnd(string raw)


## WebAppStatusDoc (class)

- int total_requests;

- int active_connections;

- List<RouteHitDoc> routes;


## WebHost (class)

- static WebApp instance;

- static void Use(WebApp app)

- static WebApp Current()


## WebServer (class)

- static async int Run(WebApp app, int count, bool daemon)

- static async int RunCommand(WebApp app, int count)

- static async int RunCommand(WebApp app, int count, bool daemon)

- static void Prepare(WebApp app, int count, bool daemon)


## WebWs (class)

- static List<string> extraOrigins;

- static void AllowOrigin(string origin)

- static async WsSession Upgrade(HttpContext ctx)

- static bool OriginOk(HttpContext ctx)


## WsSession (class)

- TcpClient client;

- WsReader reader;

- WsWriter writer;

- WsAssembler asm;

- HttpContext ctx;

- bool open;

- int lastOpcode;

- WsSession(TcpClient client, WsReader reader, WsWriter writer, WsAssembler asm, HttpContext ctx)

- bool Open()

- int LastOpcode()

- async string Recv()

- async string Recv(int timeoutMs)

- async bool SendText(string message)

- async bool SendBinary(string data, int len)

- async bool Ping()

- async void Close(int code)

- async bool FlushOut()

- void Touch()


## bool (delegate)

`delegate bool PermissionFn(string uid, string action);`


## bool (delegate)

`delegate bool HookFn(HttpContext ctx);`


## string (delegate)

`delegate string AuthFn(string token);`


## void (delegate)

`delegate void HttpHandler(HttpContext ctx);`


## void (delegate)

`delegate void AfterFn(HttpContext ctx, long elapsedUs);`


## ContentTypes (enum)

- TextPlain

- TextHtml

- TextXml

- ApplicationJson

- ApplicationXml

- ApplicationPdf

- ApplicationZip

- ApplicationGzip

- ApplicationOctetStream


## CustomAuthorization (enum)

- Unknown

- None

- Auth

- Login

- Grant

- ApiAuth = 同 Grant，用于以 token 调用的接口


## PermBit (enum)

- Unknown = =0

- View = =1

- Create = =2

- Update = =4

- Delete = =8

- Export = =16

- Import = =32

- Audit = =64

- Publish = =128
