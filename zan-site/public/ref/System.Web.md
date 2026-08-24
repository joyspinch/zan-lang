# System.Web

> 源码: `stdlib/System/Web/ApiDocs.zan`, `stdlib/System/Web/Attributes.zan`, `stdlib/System/Web/Controller.zan`, `stdlib/System/Web/HttpContext.zan`, `stdlib/System/Web/Menu.zan`, `stdlib/System/Web/Router.zan`, `stdlib/System/Web/Security.zan`, `stdlib/System/Web/StaticFiles.zan`, `stdlib/System/Web/Validate.zan`, `stdlib/System/Web/View.zan`, `stdlib/System/Web/WebApp.zan`


## ApiDocs (class)

The framework's built-in API reference: an OpenAPI 3 document plus an offline
UI, both generated from the routes the compiler already registered. Mount it
once and every attribute-routed action appears with its verb, path, title,
auth requirements and parameters:

ApiDocs.Mount(app);            // GET /api/docs and /api/docs.json

Nothing here is configured by hand. Titles come from [Description], the auth
badges from the same [Custom(Authorization=...)] the dispatcher enforces, and
the parameters from the action body -- so a documented endpoint cannot
disagree with the endpoint that runs.

- static string title="API";
  - Document title shown in the UI and the OpenAPI info block.

- static void Title(string name)
  - Names the documented service (defaults to "API").

- static void Mount(WebApp app, string path)
  - Registers GET <path> (UI) and <path>.json (OpenAPI).
    Both are public: they describe the surface, not the data.

- static void Mount(WebApp app)

- static void MountSpec(WebApp app, string path)
  - Registers only GET <path>.json, for applications that
    render the reference themselves (an admin screen, a docs site) and want
    the machine-readable document without a second UI in the menu.

- static async void Spec(HttpContext ctx)

- static string Q(string s)
  - A JSON string literal, quotes included.

- static async void Page(HttpContext ctx)

- static string SpecJson(WebApp host)
  - The OpenAPI 3.0 document for every registered route.

- static string OpenApiPath(string pattern)
  - "/user/{id}" is already OpenAPI's own syntax; this keeps the
    conversion in one place in case the router's syntax ever diverges.

- static void Operation(StringBuilder sb, Route rt)

- static void Responses(StringBuilder sb, Route rt)
  - What the endpoint answers with, per status.
    
    The shapes are the framework's own and therefore knowable: an HTML page,
    the {"code","msg","data"} envelope every JSON action writes, or an event
    stream. Emitting them here means /api/docs.json describes the response as
    precisely as it describes the request, instead of the bare "200 ok" a
    generator produces when nobody tells it anything.

- static void EnvelopeSchema(StringBuilder sb)

- static void ErrorResponse(StringBuilder sb, string status, string desc, string code, string msg)

- static void ParamJson(StringBuilder sb, ApiParam p)

- static string Tag(Route rt)
  - Tag = the controller half of "Controller.Action", falling back to
    the first path segment, so the UI groups endpoints the way the code is
    organised.

- static string Html()
  - The offline UI: one page, no CDN, no bundler. It fetches the
    spec next door and renders it, because a docs page that needs the network
    is useless exactly where docs matter (an air-gapped deployment).

- static string Shell()

- static string Css()

- static string Js()


## ApiEnvelope (class)

The uniform API envelope body. `data` is a JsonValue passthrough: the
caller supplies a JSON literal (object/array/string) that is parsed back
into the tree before serialization.

- string code;

- string msg;

- JsonValue data;


## ApiError (class)

Aborts an action with a uniform API answer. `Need("title", "标题")` throws this
when the parameter is absent, which is how a required parameter can read as
one expression in the middle of an action:

string title = this.Need("title", "标题");

The generated trampoline catches it and writes {"code":..,"msg":..} with the
carried status, so no action has to describe the same 400 twice.

- int status;

- string code;

- ApiError(int status, string code, string message)

- int Status()

- string Code()

- static string Reason(int status)
  - The standard reason phrase for a status code. The status line is
    ASCII by protocol, so a localized message belongs in the JSON body and
    never in the status text.


## ApiErrorBody (class)

The uniform error body {"code","msg"} used by WebApp.ErrorJson.

- string code;

- string msg;


## ApiParam (class)

One documented request parameter. The compiler fills these in from the
`In*`/`Need*`/`Param` calls in the action body -- the code that reads a
parameter is the declaration of that parameter, so there is no second place
to keep in sync and no way for the docs to drift from the handler.

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
  - OpenAPI JSON type for this parameter.


## Attribute (class)

Attribute plumbing for the attribute-driven router.  These are ordinary
classes deriving from `Attribute`; the compiler's compile-time
attr-route pass reads the ones placed on controllers/actions, evaluates
their constructor defaults / property initializers / named arguments at
compile time, merges class-level defaults with method-level overrides
(method wins), and generates the startup route table -- no reflection, no
generated file to maintain, no manual wiring.  Define your own attributes
the same way (derive from Attribute) and read them off the route metadata.


## Controller (class)

Rich per-request controller base. The attribute-driven router creates ONE
instance per request, binds the `HttpContext`, runs the
controller-level hook, invokes the matched action, then runs the after hook.
Actions are instance methods that read/write through <c>this</c>:

[HttpPost]
[Description("登录")]
void Login() {
string user = this.In("user");
...
this.Ok("{\"token\":\"" + token + "\"}");
}

Cross-cutting auth (login / permission / rank) is declared with attributes
([Custom(Authorization = ...)]) and enforced centrally by the dispatcher
before the action runs. Override `OnBefore` / `OnAfter`
for controller-scoped setup / teardown (the equivalent of the reference
project's per-controller hooks).

- HttpContext ctx;

- string uid;

- string __viewKey;

- void __Bind(HttpContext c)
  - Bind the request context (called by the generated trampoline).

- bool __Before()
  - Runs before the action; return false to short-circuit.
    Delegates to the overridable `OnBefore`.

- async bool __BeforeAsync()
  - Setup on the request coroutine, so it can await: a controller
    that hands out table accessors leases this request's connection here,
    before the action runs, because an accessor is a plain property and
    cannot await. The generated trampoline awaits this one; it defaults to
    the synchronous `OnBefore` hook so existing controllers keep
    working.

- virtual async bool OnBeforeAsync()
  - Awaitable controller setup. Override this (instead of
    `OnBefore`) when the preparation performs I/O.

- void __After()
  - Runs after the action (only when __Before returned true).

- async void __AfterAsync()
  - Teardown on the request coroutine, so it can await: releasing a
    pooled connection may have to roll back a transaction first, and on a
    non-blocking driver that rollback is itself an awaited round trip. The
    generated trampoline awaits this one; it defaults to the synchronous
    `OnAfter` hook so existing controllers keep working.

- virtual async void OnAfterAsync()
  - Awaitable controller teardown. Override this (instead of
    `OnAfter`) when cleanup performs I/O.

- virtual async bool __TxBegin()
  - [Tx] trampoline hook: open the request-scoped transaction.
    Return false when the store is unavailable, so the request is answered
    503 rather than pretending the write happened. The default is a no-op
    success so plain controllers compile unchanged; a DB-backed base class
    (the templates' AppController) overrides it with its lease.

- virtual async void __TxCommit()
  - [Tx] trampoline hook: commit after the action succeeded.
    No-op by default.

- virtual async void __TxRollback()
  - [Tx] trampoline hook: roll back on the failure paths. Must be
    safe to call twice and safe to call after commit; no-op by default.

- virtual bool OnBefore()
  - Controller-level hook; override to guard/prepare every action
    in this controller. Return false to stop the request.

- virtual void OnAfter()
  - Controller-level teardown hook; override as needed.

- HttpContext Ctx()

- bool IsLogin()

- string In(string name)

- string InText(string name)

- int InInt(string name, int def)

- long InLong(string name, long def)

- double InDouble(string name, double def)

- bool InBool(string name, bool def)

- bool HasIn(string name)

- string Param(string name)

- string Body()

- string Need(string name, string label)
  - Required text parameter; aborts with 400/0003 when empty.

- string NeedText(string name, string label)
  - Required HTML-escaped text parameter.

- int NeedInt(string name, string label)
  - Required integer parameter. A missing value and a non-numeric one
    are the same client mistake, so both answer the same way.

- long NeedLong(string name, string label)
  - Required long parameter (ids and timestamps outgrow int).

- double NeedDouble(string name, string label)
  - Required number parameter (aborts with 400/0003 when missing
    or not numeric).

- bool NeedBool(string name, string label)
  - Required flag parameter (aborts only when missing; the value
    words are the same ones <c>InBool</c> accepts).

- bool WantsJson()
  - True when this request came from script rather than from a
    navigation -- htmx sends `HX-Request`, fetch/XHR ask for JSON. It matters
    for "you are not signed in": a navigation should land on the sign-in
    form, but redirecting a background request hands the caller the form's
    HTML with a 200, which reads like the write succeeded.

- ListQuery Paged(string defOrder, int defLimit)
  - The list parameters of this request -- page, limit, order, dir,
    kw -- with the page size clamped to `defLimit`..200. One call replaces the
    five reads every list action would otherwise repeat, and the compiler
    documents all five in /api/docs from this one call site.

- void Ok(string data)
  - {"code":"0000","msg":"ok","data": <data>} -- data must
    already be a JSON value.

- void Fail(int status, string code, string msg)
  - Ends the action with {"code":..,"msg":..,"data":null} and the
    matching status. It throws: the generated trampoline (and WebApp for a
    hand-mapped route) turns the exception into that response, so a guard is
    one line and no action carries `...; return;` error tails. The status
    line keeps the standard ASCII reason phrase; the localized message goes
    in the body, where it belongs.

- void Json(string json)

- void Text(string text)

- void Html(string html)

- void __SetView(string key)

- void View(ViewData data)
  - Render this action's co-located template.

- void Fragment(ViewData data)
  - Render this action's template WITHOUT the layout: the answer to
    an in-page request, where the browser already has the shell and only the
    panel is being replaced.

- void FragmentOf(string name, ViewData data)
  - Render an explicitly named template, layout and all.

- void ViewOf(string name, ViewData data)
  - Render an explicitly named template instead of the convention one.


## CustomAttribute (class)

Application metadata attribute (the equivalent of the reference project's
CustomAttribute). Put it on a controller for defaults and/or on an action to
override. All values are evaluated at compile time:
[Custom(Authorization = CustomAuthorization.Login, IsMenu = true)]
[Custom(Authorization = CustomAuthorization.ApiAuth, ApiMax = 30)]
Authorization drives the auth gate; IsMenu surfaces the action in the admin
menu (its section and text come from the URL and [Description]); ApiMax /
Component / Icon / ContentType are carried on the route metadata for the
menu builder and response layer. Lock / Upload are route-runtime conveniences (request lock
scope / streamed upload).

- bool IsMenu{ get set}
  - Surface this action in the admin menu. Its text is the action's
    [Description] and its place in the sidebar is its URL, so this flag is
    the whole declaration.

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
  - Which button this action is (see PermBit). Declared per action, never
    derived from its name: adding a screen or renaming a method must not
    change what a role is allowed to do.

- =PermBit.Unknown;

- CustomAttribute(CustomAuthorization customAuthorization=CustomAuthorization.None, bool customMenu=false, string component="vlist/index.vue", string icon="mdi:antenna", int apiMax=1000, ContentTypes contentType=ContentTypes.ApplicationJson){ this.IsMenu=customMenu;this.Authorization=customAuthorization;this.Component=component;this.Icon=icon;this.ApiMax=apiMax;this.ContentType=contentType;}


## DescriptionAttribute (class)

Human label (menu text / docs); method-level overrides class-level.

- string text;

- DescriptionAttribute(string text)


## Filter (class)

Unified input filtering. Every frontend parameter fetched through
HttpContext.In* runs through here, so sanitising is centralised instead of
being repeated (and forgotten) in each controller:
Clean  -- trim + strip CR/LF/TAB control chars (safe default)
Html   -- Clean + HTML-entity escape (safe to echo into a page)
ToInt  -- tolerant integer parse with a fallback

Implemented with Substring / Length / char comparison only (no string.Trim /
Replace / IndexOf), matching the rest of the framework, so it stays correct
on binary-safe, byte-length strings.

- static bool IsSpace(string c)

- static string Clean(string s)
  - Trim surrounding whitespace and drop CR/LF (TAB -> space) so a
    value cannot smuggle header/log-injection control characters.

- static string Html(string s)
  - Clean + escape the five HTML-significant characters. Use this
    for any value that will be rendered back into HTML.

- static int DigitValue(int c)

- static int ToInt(string s, int def)
  - Parses a (cleaned) decimal integer, returning def on any
    non-numeric input. Accepts an optional leading '-'.

- static long ToLong(string s, long def)
  - Parses a (cleaned) decimal integer into a long, returning def on
    any non-numeric input. Ids and timestamps outgrow int; parsing them as
    int and widening afterwards would wrap silently.

- static double ToDouble(string s, double def)
  - Parses a decimal number (optional sign, optional fraction),
    returning def on anything else.

- static bool ToBool(string s, bool def)
  - Reads a checkbox/flag parameter. "1", "true", "on", "yes" are
    true; "0", "false", "off", "no", "" are false; anything else is def.
    Browsers send "on" for a checked box, APIs send true/1 -- both are the
    same intent and must not depend on which client is calling.


## Hooks (class)

Explicit, typed replacement for runtime `include`-style plugin hooks:
hooks are registered once at startup and run in registration order.
Everything is compile-time checked -- no reflection, no string dispatch.

- List<HookFn> before;

- List<AfterFn> after;

- Hooks()

- void Before(HookFn fn)

- void After(AfterFn fn)

- bool RunBefore(HttpContext ctx)
  - Runs before-hooks in order; false = request short-circuited.

- void RunAfter(HttpContext ctx, long elapsedUs)


## HttpContext (class)

Per-request context: parsed request data (query / form / route params /
cookies) plus the response the handler builds (status / headers / body).
One HttpContext is created per request and dropped when it is answered,
so ARC reclaims everything without a request-scoped pool.

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
  - buffering (the initial parse only saw the first packet).

- string Param(string name)

- string Query(string name)

- string Form(string name)

- string Body()

- string Header(string name)

- string Cookie(string name)
  - Reads one cookie from the Cookie header, or "".

- bool IsAjax()

- string InRaw(string name)
  - Raw, UNFILTERED value (form -> query -> route param). Only use
    when you deliberately need the untouched bytes.

- string In(string name)
  - Filtered value: trimmed, control chars stripped. The default
    safe way to read a parameter.

- string InText(string name)
  - Filtered + HTML-escaped value, safe to render into a page.

- int InInt(string name, int def)
  - Filtered integer with a fallback for missing/invalid input.

- long InLong(string name, long def)
  - Filtered long with a fallback for missing/invalid input.

- double InDouble(string name, double def)
  - Filtered number with a fallback for missing/invalid input.

- bool InBool(string name, bool def)
  - Flag parameter: "1"/"true"/"on"/"yes" against
    "0"/"false"/"off"/"no", so a checkbox and an API client read alike.

- bool HasIn(string name)
  - True when the parameter is present in form, query or route.

- HttpContext Status(int code, string text)

- static string HeaderSafe(string s)
  - A header name or value with CR, LF and NUL removed. Applied at
    the sink rather than left to the caller: a value that reaches a header
    is often a redirect target, a filename or a token read straight from the
    request, and one CRLF in it ends the header block and lets the client
    write the rest of the response (header injection / response
    splitting).

- HttpContext SetHeader(string name, string val)

- HttpContext SetCookie(string name, string val, int maxAgeSeconds)

- void Html(string html)

- void Text(string text)

- void Binary(string mime, byte[]data, int len)
  - Answers with `len` raw bytes of `data`. The only way to serve
    binary content correctly: a byte array plus its length, never a string
    whose length stops at the first NUL.

- bool IsBinary()
  - True when the response body is bytes: the server sends the
    header block and then <c>bodyBytes</c> by count.

- int BodyLength()
  - Response body size in bytes, whichever form it takes.

- void Json(string json)

- void Download(string fileName, string mime, string body)
  - Answers with a file the browser saves rather than renders. The
    name is sanitised here (quotes, newlines and path separators dropped) so
    a caller cannot inject a header line through it.

- void Api(string code, string msg, string data)
  - Uniform API envelope: {"code":...,"msg":...,"data":...}.
    data must already be a JSON value (object/array/string literal); it is
    parsed back into the tree so the envelope is built by the JSON writer,
    never by string concatenation.

- void Redirect(string url)

- void __Attach(TcpClient client)
  - Serializes the response into a raw HTTP/1.1 message.

- bool Hijacked()
  - Whether this request took the connection over.

- bool MetricsExcluded()
  - Whether this connection was EVER hijacked (SSE/upgrade). Stays
    true after the stream ends, so latency metrics stay excluded.

- long Frames()
  - Frames pushed over this connection (SSE events, keep-alives),
    and their total size: the traffic measure that replaces latency for a
    connection that is meant to stay open.

- long FrameBytes()

- async bool SseOpen()
  - Answers the `text/event-stream` handshake and takes the
    connection over. Everything after this is written by the handler with
    SseSend / SsePing; the loop it runs ends when the subscriber goes away
    (a send fails). false = no connection to stream on.

- async bool SseSend(string name, string data)
  - Pushes one event. `name` "" sends the default "message" event.
    Each newline in `data` becomes its own data: line, as the wire format
    requires. false = the subscriber is gone; stop the loop.

- async bool SsePing()
  - Comment line, used as a keep-alive so idle proxies do not drop
    the stream.

- string BuildResponse(bool keepAlive)

- static string Itoa(int v)
  - Decimal formatting for the response line without going
    through Convert.ToString (which formats via printf). Digits are
    string literals, so the whole conversion is allocation-free apart
    from the builder append.

- static string Digit(int d)

- static bool StartsWith(string s, string prefix)

- static void ParsePairs(string s, StrMap into)
  - Parses "a=1&b=2" pairs (URL-decoded) into a StrMap.


## HttpDeleteAttribute (class)


## HttpGetAttribute (class)

HTTP verb selectors (default GET when none is present).


## HttpPatchAttribute (class)


## HttpPostAttribute (class)


## HttpPutAttribute (class)


## ListQuery (class)

The read half of a list screen, taken straight from the request: page, limit,
order, dir, kw. Every list in an application answers the same five parameters
with the same names, so this is one class rather than a convention each
controller re-implements slightly differently.

The bounds are the server's, not the client's: `page` is at least 1 and
`limit` is clamped to [1, max], so `?limit=1000000` cannot turn a list into a
table scan. `order` is a bare sort key the caller chooses from -- the query
itself is still typed, because the controller maps the key to an
OrderBy(a => a.column) that the compiler checks.

- int page;

- int limit;

- string order;
  - Sort key as asked for, already defaulted; the controller decides which
    keys exist.

- bool desc;
  - True for descending -- `?dir=asc` is the only way to get ascending.

- string kw;
  - Search text, trimmed; "" when the caller did not search.

- ListQuery()

- int Page()

- int Limit()

- string Order()

- bool Desc()

- string Kw()

- int Skip()
  - Rows to skip for this page, for Skip(q.Skip()).

- int Pages(int total)
  - Number of pages `total` rows make at this limit (at least 1, so
    an empty list still reads as "第 1 / 1 页").

- string QueryString()
  - The query string that reproduces this listing minus the page, so
    a pager only has to append `&page=N`.

- static ListQuery From(HttpContext ctx, string defOrder, int defLimit, int maxLimit)
  - Reads the five parameters off the request. `defOrder` is the sort
    key used when the caller names none, `defLimit` the page size, `maxLimit`
    the largest page size this endpoint will serve.


## Listing (class)

The write half: the response shape every list endpoint returns, so a client
(or the CRUD pages) can render any list without knowing which one it is:
{"items":[...],"total":N,"page":P,"limit":L,"pages":K}. `items` is already
serialized JSON -- the rows are typed models, and serializing them is the
caller's business.

- static string Json(string itemsJson, int total, ListQuery q)


## LockLease (class)

Request-lock registry: the typed replacement for the PHP "接口请求锁" pattern.
A route annotated with [Lock("user")] / [Lock("global")] may only run one
request at a time for the given key; overlapping requests get 429 instead of
racing (double-submit / duplicate-write protection). The HTTP server is a
single-threaded event loop per worker, so a plain in-flight key set is race
free -- no OS mutex needed.

- string key;

- long owner;

- LockLease(string key, long owner)


## LockManager (class)

- SharedTable table;

- long ownerSequence;

- long leaseMs;

- LockManager()

- static SharedTable NewTable(string name)

- static LockManager Named(string name)
  - Locks in a NAMED shared table: created by whichever process gets
    there first and opened by the rest, and reachable by name from outside
    the server.

- static LockManager Owned()
  - Locks in anonymous shared memory this process owns: no name for
    another server on the machine to collide with, and workers reach the same
    leases through `OsHandle` / `Attach`.

- static LockManager Attach(long osHandle)
  - Locks in the anonymous table another process created and handed
    down. Handle 0 (nothing was handed down) leaves the manager closed, and a
    closed manager grants no lease -- a locked route is refused rather than
    let through unprotected.

- long OsHandle()
  - The handle a worker needs to map this manager's table, or 0 when
    the table is named (found by name instead) or was never created.

- static string KeyFor(Route route, string principal)

- LockLease TryAcquire(string key)

- void Release(LockLease lease)


## MenuBuilder (class)

Builds the admin navigation out of the route table and the URLs themselves.

An entry is one attribute: [Custom(IsMenu = true)]. Everything else follows
from what the route already says -- its text is the action's [Description],
its section is the first segment of its URL (/admin/system/users sits under
"system", /admin/monitor/history under "monitor"), its position is the URL
itself, and whether it is offered at all is decided by the same resolver the
dispatcher uses to refuse the request.

So there is no menu to write and nothing to keep in step: moving a
controller moves its entry, renaming its [Description] renames it, and a new
screen appears the moment it compiles. The menu is two levels because URLs
are -- a section and its pages -- and a deeper URL is simply a page of that
section.

`Section` is the only place a URL segment gets a display name,
and the order those names are registered in is the order the sections
appear.

- static StrMap labels=new StrMap();
  - Display name per URL segment ("system" -> "系统").

- static List<string> sections=new List<string>();
  - Registered segments, in the order the sections should appear.

- static void Section(string segment, string label)
  - Names a URL segment and places its section. Called once at
    startup, in the order the sidebar should read; a segment never named
    still gets a section, headed by the segment itself.

- static async List<MenuNode> ForUser(WebApp app, string uid, string activePath)
  - The entries `uid` may actually reach, in URL order, with the
    entry serving `activePath` marked. Only GET routes qualify: a menu entry
    is a link, and a POST route sharing the controller's attributes would
    render as one the browser cannot follow.
    
    Visibility asks the installed resolvers, not a second permission list:
    a route whose [Custom(Authorization)] requires a permission is shown only
    when the permission resolver allows it, so an entry can never lead to a
    403 the menu did not predict. `uid` is "" for an anonymous visitor, which
    drops every route that needs a session.

- static async string JsonFor(WebApp app, string uid, string activePath)
  - The menu `uid` may reach, as JSON:
    [{"title","path","group","icon","active"}]. This is what a SPA front end
    should render its navigation from -- the server decides what is
    reachable, so the client cannot show a link the API would refuse.

- static void Fill(List<StrMap> rows, List<MenuNode> nodes)
  - Flattens the menu into template rows: a `group` row opens a
    section and the rows after it carry title/path/icon/active. One flat list
    because the view engine has no nested loops -- and because a section and
    its pages are all the depth a URL gives.

- static string GroupOf(string path)
  - The section a URL belongs to: the name of its first segment
    below /admin. A page directly under /admin ("/admin", "/admin/profile")
    belongs to no section and is listed on its own at the top; a section's
    own index page ("/admin/monitor") belongs to that section like its other
    pages do.

- static int OrderOf(string path)
  - Where that section sits: its registered position, or after every
    registered one. A sectionless page comes first.

- static List<string> Body(string pattern)
  - Path segments below /admin (or /api), placeholders dropped.

- static string Label(string s)
  - A URL segment as a heading: its registered name, or the segment
    with its first letter upper-cased.

- static bool Matches(string pattern, string path)
  - True when a request path is served by this route pattern, so
    "/admin/posts/edit/7" highlights the "/admin/posts" entry. Compared per
    segment, with {placeholder} segments matching anything.

- static void Sort(List<MenuNode> list)
  - Groups the entries by section, sections in registered order and
    entries by URL, so a sub-page follows the page it hangs off. Insertion
    sort: a menu is a handful of entries, so this costs nothing and leaves
    equal entries where they were.

- static bool Before(MenuNode a, MenuNode b)


## MenuNode (class)

One navigation entry, already decided for a particular principal.

- string title;

- string path;

- string group;
  - Section heading: the name of the first URL segment below /admin.

- string icon;

- bool active;

- int groupOrder;
  - Where the section sits: its registered position, or after every
    registered one.

- string order;
  - Position within the section: its URL, so a sub-page follows the page it
    belongs to. Non-routed entries pass a sort key of their own.

- MenuNode()

- string Title()

- string Path()

- string Group()

- string Icon()

- bool Active()


## MenuNodeDoc (class)

One entry of the per-user menu JSON (MenuBuilder.JsonFor).

- string title;

- string path;

- string group;

- string icon;

- bool active;


## NonActionAttribute (class)

Marks a public method that must NOT become a route.


## RateLimiter (class)

Fixed-window rate limiter, the in-process analogue of a swoole_table
counter: one slot per key (route / ip / token), counting requests in the
current window and resetting when the window rolls over. Windows are
wall-clock seconds. Single-threaded event loop => no locking needed.

- SharedTable table;

- int windowMs;

- RateLimiter(int windowMs)

- static SharedTable NewTable(string name)

- static RateLimiter Named(string name, int windowMs)
  - A limiter on a NAMED shared table: created by whichever process
    gets there first and opened by the rest, and reachable by name from
    outside the server.

- static RateLimiter Owned(int windowMs)
  - A limiter on anonymous shared memory this process owns: no name
    for another server on the machine to collide with, and workers reach it
    through `OsHandle` / `Attach`.

- static RateLimiter Attach(long osHandle, int windowMs)
  - A limiter on the anonymous table another process created and
    handed down. Handle 0 (nothing was handed down) leaves the limiter
    closed, which refuses the requests it is asked about rather than
    silently letting a limited route run unlimited.

- long OsHandle()
  - The handle a worker needs to map this limiter's table, or 0 when
    the table is named (found by name instead) or was never created.

- bool Allow(string key, int limit)

- int CountOf(string key)


## Route (class)

One registered route. The pattern is pre-split into segments at
registration time, so matching a request never re-parses the pattern:
"/user/{id}" -> ["user", "{id}"].

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
  - Declares one request parameter (emitted by the compiler from the
    action body). `source` is "path" for a {segment} and "query" otherwise.

- string MetaGet(string key)

- string Key()


## RouteAttribute (class)

Route template. Supports [controller] / [action] tokens, e.g.
[Route("SysAdmin/Users/[controller]/[action]")]. A class-level template is
the prefix; a method-level [Route] combines with (or, if absolute, replaces)
it. Without a method template the action name fills [action].

- string template;

- RouteAttribute(string template)


## RouteHitDoc (class)

One routed endpoint's shared-memory counters in Router.StatsJson. Keys
(route/count/avg_us) match the documented JSON array shape; the average is
in microseconds, the unit every duration the server records is kept in.

- string route;

- long count;

- long avg_us;


## Router (class)

HTTP router with static-first matching and {param} segments. Routes are
bucketed by method and static routes are checked with a direct string
compare before parameterized ones, so the hot path ("/", "/api/x") is a
handful of string compares with zero allocation.

- List<Route> statics;

- List<Route> dynamics;

- Dictionary <string, Route> staticIndex;

- List<int> hashSlots;

- int hashMask;

- List<Route> flat;

- List<string> flatKeys;

- List<long> flatHashes;

- SharedTable routeStats;

- Router()

- int Count()
  - Registered routes, in registration order.

- Route Add(string method, string pattern, HttpHandler handler)

- void HashRouteKeys()

- SharedTable NewStatsTable(string name)

- void SeedStats(SharedTable t)

- void InitStats(string name)
  - Create (master/single-process) or open (worker) a NAMED shared
    route stats table -- for a deployment that wants the table reachable by
    name. Call after all routes are registered and before the first request;
    name must be unique per server instance (WebApp builds one with
    SharedName). Every process calls this, and Create falls back to Open when
    the master has already made the table.

- void InitStatsShared()
  - The default: create the stats table as anonymous shared memory
    owned by this server. It has no name for anything on the machine to
    collide with or read, and the workers get it from the master through
    `StatsOsHandle` / `AttachStats`, so it must be
    created before the first worker is spawned.

- long StatsOsHandle()
  - The handle a worker needs to map the anonymous stats table, or 0
    when there is no such table (none created, or a named one).

- void AttachStats(long osHandle)
  - Worker side: map the anonymous stats table the master created
    and handed down. A handle of 0 leaves this process without route stats,
    which costs it nothing but its share of the counters.

- void RecordHit(int idx, long us)
  - Record one served request. Two atomic increments into the
    shared stats table -- no Route object touched, no per-process
    computation.

- List<Route> All()
  - Every registered route (statics then dynamics) -- used by the
    admin menu builder and route diagnostics.

- List<RouteHitDoc> Stats()
  - Per-route request stats (only routes with at least one hit),
    read from the cross-process shared table. Empty when InitStats has not
    been called yet. Callers that embed this in a larger document take the
    list rather than StatsJson, so the numbers are written out once instead
    of being serialised, parsed back and serialised again.

- string StatsJson()
  - Stats() as a JSON array.

- Route Get(string pattern, HttpHandler handler)

- Route Post(string pattern, HttpHandler handler)

- Route Put(string pattern, HttpHandler handler)

- Route Delete(string pattern, HttpHandler handler)

- static int HashKey(string method, string path)
  - FNV-1a over "method path" without building the key string.
    Folded to 30 bits so the multiply never overflows.

- void RebuildIndex()
  - Rebuilds the open-addressed static-route index. Runs once
    after registration (and again if a route is added later), never per
    request. Slots hold indices into `statics`; -1 is empty.

- Route FindStatic(string method, string path)
  - Exact (method, path) static-route lookup: one hash and one
    string compare in the common case, zero allocations.

- Route Match(HttpContext ctx)
  - Finds the route for method+path, filling ctx.routeParams.
    Returns null when nothing matches. `pathMatched` is set when some route
    has the path but not the method (405 vs 404).

- bool PathExists(string path)
  - True when some route matches the path with a different method
    (drives a 405 Method Not Allowed instead of 404).

- static bool IsStatic(string pattern)

- static bool MatchSegs(List<string> pat, List<string> segs, StrMap into)

- static List<string> SplitPath(string path)


## RowList (class)

Named values for a render: string variables plus named lists of row maps
(for {{#each}} blocks).

- string name;

- List<StrMap> rows;

- RowList(string name)


## Sessions (class)

Minimal in-memory session/auth store: token -> user id. A before-hook
checks `route.needsAuth` against this store; swap the storage for Redis
or a database without touching the hook contract.

- StrMap tokens;

- Sessions()

- string Issue(string uid, int nowSeconds)
  - Issues a token. Locked because the map is shared by every
    request in the process and those run on several worker threads: a Set
    that rehashes while another thread is reading corrupts the table, and
    the count that makes the token unique must be read under the same lock
    as the insert.

- string UserOf(string token)
  - Returns the uid for a bearer token / cookie, or "".


## StaticFiles (class)

Serves files from a directory on disk (CSS/JS/fonts/images: everything the
views reference but no controller should own).

It is a Before hook rather than a route: assets must be answered before
authentication, rate limiting and the router's pattern matching, and they
must not appear in the route table (nothing to secure, nothing to put in a
menu). Returning false from the hook short-circuits the request with the
response already built.


StaticFiles.Mount(app, "/static", "public");   // GET /static/css/app.css


The URL path is resolved against the mounted directory only: a request is
rejected unless every byte is an unreserved file character, so "..", "//",
backslashes, NUL and query-smuggled separators cannot walk out of the root.

Bodies are cached per worker after the first hit (assets are immutable in a
deployment; restart or bump the file name to publish a new one) and served
with a long max-age. Files are read as bytes and written back untouched, so
images and fonts survive the trip.

- static string prefix="";

- static string root="";

- static int maxAge=86400;

- static List<string> paths=null;

- static List <byte[]> blobs=null;

- static List<int> sizes=null;

- static List<string> types=null;

- static void Mount(WebApp app, string urlPrefix, string dir)
  - Mounts `dir` under `urlPrefix` and registers the hook on the
    app. `urlPrefix` is matched literally ("/static" answers
    "/static/css/app.css"); `dir` is relative to the process working
    directory.

- static void MaxAge(int seconds)
  - How long browsers may cache an asset (seconds). Set it to 0
    while developing so an edited file is picked up by a reload -- the
    per-worker body cache still needs a restart.

- static bool Serve(HttpContext ctx)
  - Before hook: answers asset requests, passes everything else on
    (true = keep going).

- static int Cached(string rel)
  - Index of an already-read asset, or -1. The table holds one
    entry per file served since start-up, so the scan is over a handful of
    deployed assets, not over anything a request can grow.

- static string Relative(string path)
  - The path below the mount point, or null when the request is not
    for this mount.

- static bool IsSafe(string rel)
  - Only unreserved path characters, and no empty or dot-leading
    segment: that rules out "..", absolute paths, backslashes, NUL bytes and
    hidden files in one pass.

- static string ContentType(string rel)

- static string Extension(string rel)


## StrMap (class)

Ordered string-to-string map on parallel lists. Small and predictable:
route params / query / form / headers per request stay tiny, so linear
lookup beats a hash table while keeping zero extra allocations.

The backing lists are created on the first Set: most requests never touch
their route params, query, form or state map, and an empty map that costs
nothing keeps the per-request allocation count down.

- List<string> keys;

- List<string> vals;

- StrMap()

- int Count()

- string KeyAt(int i)

- string ValAt(int i)

- int IndexOfKey(string key)
  - Position of `key`, or -1. Callers that would otherwise do
    Has() followed by Get() use this to scan the map once (template
    rendering resolves every variable through here).

- void Set(string key, string val)

- bool Has(string key)

- string Get(string key)

- string GetOr(string key, string def)

- void Clear()


## VNode (class)

One node of a compiled template: templates are parsed once (View.LoadDir)
into a tree, so rendering is a walk that only appends -- no scanning for
tags and no substring copies on the request path.

- int kind;

- string text;

- List<VNode> body;

- VNode(int kind, string text)


## Validator (class)

Request parameter validation. Chain rules, then check Ok():

Validator v = new Validator();
v.Require(ctx.Form("user"), "user");
v.MaxLen(ctx.Form("user"), 32, "user");
v.IsInt(ctx.Param("id"), "id");
if (!v.Ok()) { ctx.Status(422, "Unprocessable Entity");
ctx.Api("1003", "validation failed", v.ErrorsJson()); return; }

Also carries the upload-safety helpers: client-supplied file names are
never trusted as paths -- SafeFileName strips directories and dangerous
characters, ExtAllowed enforces an extension whitelist.

- List<string> errors;

- Validator()

- Validator Require(string val, string field)

- bool Ok()

- Validator MaxLen(string val, int max, string field)

- Validator MinLen(string val, int min, string field)

- Validator IsInt(string val, string field)
  - Digits only (optional leading minus).

- Validator OneOf(string val, string allowedCsv, string field)
  - Value must be one of the comma-separated whitelist entries.

- static bool ContainsStr(string s, string pat)
  - Multi-character substring search (string.Contains only
    supports single characters reliably).

- bool Ok()

- string ErrorsJson()

- static string SafeFileName(string name)
  - Reduces a client-supplied file name to a safe base name:
    strips any directory components (both separators), rejects "..",
    and keeps only [A-Za-z0-9._-]. Returns "file" if nothing survives.

- static bool ExtAllowed(string name, string allowedCsv)
  - True if the file's extension is in the comma-separated
    whitelist (e.g. "jpg,png,mp4"). Case-insensitive is NOT applied;
    pass lowercase names.


## View (class)

In-memory template engine. All templates under the views directory are
read from disk and compiled ONCE at startup (View.LoadDir) and rendered
from memory afterwards -- request handling never touches the filesystem
nor re-parses the template text, mirroring the "templates live in memory"
design of the original swoole framework.

Syntax:
{{name}}                  HTML-escaped variable
{{{name}}}                raw (unescaped) variable
{{#if name}}...{{/if}}    emitted when var is non-empty and not "0"
{{#each name}}...{{/each}} repeated per row; {{key}} reads row fields
layout.html + {{content}} wraps every RenderPage() body

- Dictionary <string, List<VNode>> compiled;

- string dir;

- bool devReload;

- View()

- static View LoadDir(string dir)
  - Loads every .html under `dir` (recursively) into memory,
    keyed by file name without extension. Views live next to their
    controller as <c><Module>/View/<Controller>.<Action>.html</c>,
    so the key matches the "<Module>.<Controller>.<Action>"
    view id the router injects into each controller instance (the view
    follows the controller, mirroring the reference project layout).

- void LoadAll()

- void Put(string key, string body)

- void LoadRec(string d, string prefix)

- string Render(string name, ViewData data)
  - Renders a template by name from the in-memory cache.

- string RenderPage(string name, ViewData data)
  - Renders `name` wrapped in the nearest layout.html:
    1. "<Module>.layout"  (e.g. "Admin.layout") -- module-specific
    2. "layout"               -- root-level global fallback
    Each module can therefore have its own look-and-feel while sharing a
    global default when no per-module layout is present.

- static string RenderStr(string tpl, ViewData data, StrMap row)
  - Renders template text that has no entry in the cache -- an
    inline snippet, a mail body, a fragment built at runtime. Compiling on
    every call is the price of not having a name to cache under, so views
    that live on disk should go through `Render`.
    <paramref name="row"/> supplies the fields an enclosing {{#each}} row
    would (null outside a loop).

- static List<VNode> Compile(string tpl)
  - Parses template text into nodes. Called once per template at
    load time; the scanning and slicing here never happens on a render.

- static void Emit(List<VNode> nodes, ViewData data, StrMap row, List<VNode> content, StringBuilder sb)

- static string Lookup(string key, ViewData data, StrMap row)

- static string HtmlEscape(string s)
  - Escapes < > & " so variables are XSS-safe by default.

- static int Find(string hay, string needle, int from)

- static int FindTag(string tpl, string closeTag, int from)
  - Finds a closing tag, skipping nested blocks of the same kind.

- static string Trim(string s)


## ViewData (class)

- StrMap vars;

- List<RowList> lists;

- ViewData()

- ViewData Set(string key, string val)

- List<StrMap> AddList(string name)

- List<StrMap> ListOf(string name)


## WebApp (class)

The application server: coroutine-per-connection HTTP/1.1 event loop
(the zan analogue of the swoole worker), wired to the Router, Hooks,
RateLimiter, Sessions and the in-memory View engine.

Memory-safety rules baked in:
- request bodies larger than maxBodyBytes are rejected with 413 before
they are buffered, so an uploader cannot balloon the heap;
- header blocks larger than 64KB are rejected with 431;
- all per-request state lives on the HttpContext and is ARC-reclaimed
when the request completes.

- [DllImport("crt")]static extern long time(nint ptr);

- [DllImport("crt")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern long fwrite(string buf, long size, long count, nint fp);

- [DllImport("crt")]static extern int fclose(nint fp);

- string host;

- int port;

- bool running;

- TcpListener listener;

- string scope;

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
  - How long one request may take to arrive and be served before
    the connection is closed. Applies per request, not per connection, so a
    keep-alive client that keeps sending is never cut off.

- WebApp MaxConnections(int n)
  - How many connections may be served at once. Past the cap a
    connection is answered 503 and closed instead of being queued: every
    live connection owns a coroutine and a 64KB buffer, and an unbounded
    number of them is how a server dies rather than sheds load. 0 removes
    the cap.

- WebApp UploadDir(string dir)

- WebApp GlobalLimit(int perSecond)

- WebApp LoginPath(string path)
  - The sign-in page a navigation is sent to when the route needs a
    session it does not have. Unset (the default) answers every refusal with
    the JSON body, which is what an API-only server wants.

- WebApp ReusePort(bool on)

- WebApp Scope(string name)
  - Names this app instance for shared memory: every table it names
    is then "zan.web.<name>.<part>", and the framework's own rate,
    lock and route tables become named tables too instead of the anonymous
    ones the master hands down to its workers. Only a deployment that wants
    those tables reachable by name needs this. Set it before starting the
    server, and to the same value in every process of one server; use a
    distinct name per deployed server on the machine.

- string Scope()

- string SharedName(string part)
  - The shared-memory name of one part of this app:
    "zan.web.shop.perm.roles". The single place these names are built, so an
    application's own tables are namespaced with the framework's instead of
    inventing a prefix. Without `Scope` the identity of the
    executable stands in (Worker.AppId): two copies in two directories are
    two servers and share nothing, the same copy started again is the same
    server, and moving the service to another port changes none of it.

- string ScopeName()

- void InitStats(string name)
  - Initialise cross-process route stats. Call after all routes are
    registered and before starting the server or running workers. Safe to
    call in every process: the first call creates the shared table; subsequent
    calls (workers) open it. name must be unique per server instance --
    `SharedName` is what produces one.

- void InitStats()

- static const string ShareRate="WEB_RATE";

- static const string ShareLock="WEB_LOCK";

- static const string ShareRoute="WEB_ROUTE";

- void InitShared()
  - Creates every shared-memory table of this app (rate limiter,
    request locks, route stats). Called by WebServer in the master before it
    spawns any worker, which is what lets the workers inherit them: a worker
    runs this too and maps the master's tables instead of making a second
    set. Idempotent.

- bool AttachShared()

- RateLimiter Limiter()

- LockManager Locks()

- WebApp Before(HookFn fn)

- WebApp After(AfterFn fn)

- WebApp AuthResolver(AuthFn fn)

- WebApp PermissionResolver(PermissionFn fn)

- Route Map(string method, string pattern, HttpHandler h)
  - Generic route registration used by the compiler-generated
    __AttrRoutes.Register. Returns the Route so attribute metadata can be
    chained.

- Route Get(string pattern, HttpHandler h)

- Route Post(string pattern, HttpHandler h)

- Route Put(string pattern, HttpHandler h)

- Route Delete(string pattern, HttpHandler h)

- string RenderPage(string name, ViewData data)

- string RenderFragment(string name, ViewData data)
  - Renders a template WITHOUT its layout. This is what an
    in-page update answers with: the browser already has the shell, so
    sending it again would replace the page instead of the panel.

- static int NowSeconds()

- static string Redact(string s)
  - Trims a message before it reaches the error log so one runaway
    exception cannot bloat the log or the in-memory ring. Kept conservative:
    the log never carries request bodies, headers or credentials, only the
    handler's own exception text.

- ServerBanner Banner(int procs)
  - The start-up screen for this app: the listener, then every
    service that is actually switched on (routes, views, sessions, uploads,
    rate limit). `procs` is how many processes serve it.

- async int Start()
  - Runs the accept loop forever. Await this from Main so the
    coroutine scheduler drives the server: `int r = await app.Start();`

- void Stop()

- static void SetActive(WebApp app)
  - Registers the app that ServeSock hands connections to. Called
    in every process (master and each worker) before Worker.RunAll.

- static async void ServeSock(nint clientSock)
  - Worker raw-connection callback for multi-process serving: the
    master (or POSIX SO_REUSEPORT worker) hands each accepted socket here;
    it runs the full request pipeline via the active app's HandleConnection.
    Static so it can be assigned to Worker.onRawConnection without needing
    an instance-method delegate.

- async bool RefuseOverCapacity(nint clientSock)
  - Sheds a connection the server has no capacity for: 503 and
    close, rather than a coroutine and a buffer it cannot afford. True when
    the connection was refused and is already closed.
    
    The slot is CLAIMED here (and released by HandleConnection), not counted
    by the caller afterwards: with accepts landing on several worker threads,
    a check followed by a separate increment lets every thread pass the cap
    at once. Claim first, then hand the slot back if it was over the line.

- async void HandleConnection(nint clientSock)

- async string StreamBodyToFile(nint sock, HttpFramer framer, HttpRequest request)
  - Streams the request body into a server-named file under
    uploadDir (the client's filename is never used, so path traversal is
    impossible). Every write uses explicit socket-level byte counts +
    fwrite, so binary payloads with NUL bytes land intact, and only one
    64KB buffer is alive regardless of upload size. Returns "" on IO
    failure.

- static byte[]MakeBuffer(int size)
  - Preallocates a reusable ARC-managed receive buffer.

- async string Dispatch(HttpContext ctx, Route route, bool keepAlive)
  - Full request pipeline: hooks -> rate limit -> auth -> route.
    Times the whole pipeline with a monotonic MICROSECOND clock and feeds the
    shared ServerMetrics (throughput / latency / slow requests) for
    /admin/stats. Microseconds because a request served in 200us is invisible
    to the millisecond clock (~15.6ms per tick on Windows), which reported
    every fast endpoint as "0 ms".

- async string DispatchInner(HttpContext ctx, Route route, bool keepAlive, long startUs)

- bool WantsPage(HttpContext ctx)
  - Whether this request is a browser navigation that should be sent
    to the sign-in page rather than answered with a JSON refusal: a GET that
    did not come from script (htmx / XHR / fetch asking for JSON), with a
    sign-in page configured.

- async bool Allow(string uid, string action)
  - Whether `uid` may run `action`: the installed PermissionResolver,
    or "any authenticated principal" when none is set. The dispatcher's 403
    and the menu builder both go through here, so a hidden entry and a
    refused request are one decision rather than two that can disagree.

- async string AuthUser(HttpContext ctx)
  - Resolves the current user from Authorization: Bearer or the
    session cookie; "" when anonymous.

- static string ErrorJson(string code, string msg)
  - Uniform error body {"code","msg"}, built by the JSON writer.

- string StatusJson()
  - Status snapshot for /api/status.

- string MetricsJson()
  - Full runtime metrics snapshot (CPU, memory, request + query
    throughput/latency, slow-request and slow-query rings) for /admin/stats.

- string MetricsSeriesJson(int seconds)
  - The last `seconds` one-second buckets (requests, errors, average
    and p95 latency, queries, CPU%, RSS) for charts. Recorded on the request
    path itself, so there is no sampling coroutine to schedule and nothing to
    start or stop.

- static string SimpleResponse(int code, string text, string msg)

- static int FindHeaderEnd(string raw)


## WebAppStatusDoc (class)

The /api/status snapshot: two counters plus the per-route stats array.

- int total_requests;

- int active_connections;

- List<RouteHitDoc> routes;


## WebHost (class)

Global accessor for the running WebApp instance so static controller actions
can reach shared services (views, sessions, request stats) without threading
the app through every call. Set once at startup by main. Named WebHost (not
App) so it does not collide with Gui.App.

- static WebApp instance;

- static void Use(WebApp app)

- static WebApp Current()


## WebServer (class)

Boots a `WebApp`: one process running the coroutine event loop,
or a master supervising `count` worker processes (System.Net.Worker), which
is where the listener handoff, respawn-on-crash and the
start/stop/restart/reload/status commands come from.

Single process is the development default -- every connection already runs
in its own coroutine, so it serves thousands of concurrent connections and
logs stream straight into the terminal. With count > 1 the same binary runs
as master and workers; each worker gets connections from the kernel
(SO_REUSEPORT on Linux/macOS) or from the master over a duplicate-socket
channel (Windows).

- static async int Run(WebApp app, int count, bool daemon)
  - Runs the app in the foreground: single process when count is 1,
    otherwise a master plus `count` workers. daemon detaches on Linux.

- static async int RunCommand(WebApp app, int count)
  - Same as `Run`, but the process also answers the
    command line: `start`, `start -d`, `stop`, `restart`, `reload`,
    `status`. Use this from Main when the app is deployed as a service.

- static async int RunCommand(WebApp app, int count, bool daemon)
  - Same as `RunCommand`, with <paramref name="daemon"/>
    standing in for `start -d`: config can ask for a background start
    without the command line repeating it.

- static void Prepare(WebApp app, int count, bool daemon)


## bool (delegate)

`delegate bool PermissionFn(string uid, string action);`


## bool (delegate)

Lifecycle hook: return false to short-circuit the request
(the hook must have written a response into ctx first).

`delegate bool HookFn(HttpContext ctx);`


## string (delegate)

The two things the dispatcher asks the application: who a token
belongs to (AuthFn) and whether that principal may run a route action
(PermissionFn).

Both are asynchronous: they run on the request coroutine, so a user store
that is a database or Redis can be awaited instead of blocking the worker.
A resolver that needs no I/O simply never awaits.

`delegate string AuthFn(string token);`


## void (delegate)

Request handler signature: fill the response on ctx. Async so an
action can await I/O (Cache/Redis/async services) without blocking the
connection coroutine; WebApp.Dispatch awaits it.

`delegate void HttpHandler(HttpContext ctx);`


## void (delegate)

Post-request hook (logging, metrics); cannot short-circuit.
The elapsed time is MICROSECONDS, and 64-bit: a connection held open for an
hour is 3.6 billion of them, past what an int can hold.

`delegate void AfterFn(HttpContext ctx, long elapsedUs);`


## ContentTypes (enum)

Response content type carried by [Custom].

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

Authorization requirement carried by [Custom]. Mirrors the
reference project's CustomAuthorization.

- Unknown

- None

- Auth

- Login

- Grant

- ApiAuth = 同 Grant，用于以 token 调用的接口


## PermBit (enum)

Which button a route action is: the bit it occupies in a screen's
permission mask, declared per action with [Custom(Perm = PermBit.Update)].
The values are the bits themselves, so a screen's rights are one int and an
action's right is one AND. Unknown means the action never declared one --
authorization then has nothing to check and must refuse.

- Unknown = =0

- View = =1

- Create = =2

- Update = =4

- Delete = =8

- Export = =16
