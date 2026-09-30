# System.Web

> 源码: `stdlib/System/Web/ApiDocs.zan`, `stdlib/System/Web/Attributes.zan`, `stdlib/System/Web/Controller.zan`, `stdlib/System/Web/DesignerHtml.zan`, `stdlib/System/Web/Html.zan`, `stdlib/System/Web/HtmlScope.zan`, `stdlib/System/Web/HttpContext.zan`, `stdlib/System/Web/Menu.zan`, `stdlib/System/Web/Router.zan`, `stdlib/System/Web/Security.zan`, `stdlib/System/Web/StaticFiles.zan`, `stdlib/System/Web/Validate.zan`, `stdlib/System/Web/View.zan`, `stdlib/System/Web/WebApp.zan`, `stdlib/System/Web/WebWs.zan`, `stdlib/System/Web/WsSession.zan`


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
  - 等价 Mount(app, "/api/docs")。

- static void MountSpec(WebApp app, string path)
  - Registers only GET <path>.json, for applications that
    render the reference themselves (an admin screen, a docs site) and want
    the machine-readable document without a second UI in the menu.

- static async void Spec(HttpContext ctx)
  - GET <path>.json 处理器：no-store 输出 OpenAPI 文档。

- static async void Page(HttpContext ctx)
  - GET <path> 处理器：输出离线 UI 页面。

- static string SpecJson(WebApp host)
  - The OpenAPI 3.0 document for every registered route.
    全程 JsonValue 树构建 + ToJson 序列化——标题/描述等文本经
    Encoding.JsonEscape 转义，任何路由名/参数名都毁不掉文档结构。

- static string OpenApiPath(string pattern)
  - "/user/{id}" is already OpenAPI's own syntax; this keeps the
    conversion in one place in case the router's syntax ever diverges.

- static JsonValue Operation(Route rt)
  - 把一条路由写成一个 OpenAPI operation：summary/operationId/tags、
    x-login/x-auth/x-menu 徽标、parameters 与 requestBody（写请求的
    非 path 参数归入表单体）、responses。

- static JsonValue Responses(Route rt)
  - What the endpoint answers with, per status.
    
    The shapes are the framework's own and therefore knowable: an HTML page,
    the {"code","msg","data"} envelope every JSON action writes, or an event
    stream. Emitting them here means /api/docs.json describes the response as
    precisely as it describes the request, instead of the bare "200 ok" a
    generator produces when nobody tells it anything.

- static JsonValue EnvelopeSchema()
  - {"code","msg","data"} 统一信封的 schema。

- static JsonValue ErrorResponse(string desc, string code, string msg)
  - 一个错误状态响应（信封 schema + 示例）。

- static JsonValue ParamJson(ApiParam p)
  - 一个 OpenAPI parameter 对象（in 取 path/query）。

- static string Tag(Route rt)
  - Tag = the controller half of "Controller.Action", falling back to
    the first path segment, so the UI groups endpoints the way the code is
    organised.

- static string Html()
  - The offline UI: one page, no CDN, no bundler. It fetches the
    spec next door and renders it, because a docs page that needs the network
    is useless exactly where docs matter (an air-gapped deployment).

- static string Shell()
  - 文档页 HTML 外壳（内联 CSS 与 JS，无外部依赖）。

- static string Css()
  - 内联样式（亮/暗双色，随系统偏好）。

- static string Js()
  - 内联脚本：fetch docs.json，收集/排序/渲染接口卡片，搜索框即时过滤。


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
  - 业务错误码（"0003" = 缺失或非法参数）。

- ApiError(int status, string code, string message)
  - 构造受控中止：status 为应答状态码，code 为业务错误码。

- int Status()
  - 应答用的 HTTP 状态码。

- string Code()
  - 业务错误码。

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
  - 源码中书写的字面默认值（无默认值时为 ""）。

- string desc;
  - Need(label, ...) 传入的标签；In() 时为 ""。

- string source;

- ApiParam(string name, string type, bool required, string def, string desc, string source)

- string Name()
  - 参数名。

- string Type()
  - 参数类型（"string" | "int" | "long" | "number" | "bool"）。

- bool Required()
  - 是否必填。

- string Default()
  - 默认值（无则 ""）。

- string Desc()
  - 参数说明（Need 的标签）。

- string Source()
  - 参数来源（"path" | "query"，query 覆盖 form/query/body 字段）。

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
JsonValue body = JsonValue.NewObject();
body.PutStr("token", token);
this.Ok(body.ToJson());
}

Cross-cutting auth (login / permission / rank) is declared with attributes
([Custom(Authorization = ...)]) and enforced centrally by the dispatcher
before the action runs. Override `OnBefore` / `OnAfter`
for controller-scoped setup / teardown (the equivalent of the reference
project's per-controller hooks).

- HttpContext ctx;

- string uid;
  - 已解析的登录主体 id（匿名时为 ""）。

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

- virtual void __UseReadOnly()
  - [ReadOnly] request intent, set by the generated route before
    OnBeforeAsync acquires a connection. Plain controllers ignore it.

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
  - 本实例绑定的请求上下文。

- bool IsLogin()
  - 本次请求是否已解析出登录用户。

- string In(string name)
  - 过滤后的参数（trim + 去控制符；route → query → form 合并）。

- string InRaw(string name)
  - 原始未过滤值：多行语义字段专用，渲染转义由调用方负责。

- string InText(string name)
  - 过滤并 HTML 转义的参数（可直接渲染进页面）。

- int InInt(string name, int def)
  - 过滤后的整型；缺失/非法取 def（严格整串解析）。

- long InLong(string name, long def)
  - 过滤后的 64 位整型；规则同 InInt。

- double InDouble(string name, double def)
  - 过滤后的数值；缺失/非法取 def。

- bool InBool(string name, bool def)
  - 布尔参数："1/true/on/yes" 真，"0/false/off/no" 假，其余 def。

- bool HasIn(string name)
  - 参数是否在 form/query/route 任一处出现。

- string Param(string name)
  - 路由参数（{param} 段）。

- string Body()
  - 原始请求正文。

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
  - 以 application/json 应答。

- void Text(string text)
  - 以 text/plain 应答。

- void Html(string html)
  - 以 text/html 应答。

- void __SetView(string key)
  - The router injects this action's view id ("<Module>.<Controller>.<Action>") via
    __SetView before the action runs, so View(data) renders the .html that
    sits next to this controller (<Module>/View/<Controller>.<Action>.html)
    with zero wiring -- no per-action template name to pass.

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


## Csrf (class)

CSRF 防护（opt-in）：double-submit cookie。注册一行：
app.Before(Csrf.Guard);

语义：
- 安全方法（GET/HEAD/OPTIONS）不拦截；首次访问补发随机 token
cookie（不带 HttpOnly——浏览器脚本必须读它、回显到
X-CSRF-Token 头或 _csrf 表单字段；SameSite=Lax 保留，本身就是
第一道防线）。
- 不安全方法要求 X-CSRF-Token（或 _csrf 字段）与 cookie 逐字节
相等；缺失或不匹配 → 403 并短路（返回 false，路由不再执行）。
- 比较走常时路径（按最大长度累积异或），不因首个差异字节提前
返回。
- token 无状态（16 随机字节的 hex），重启/多 worker 之间均有效，
服务端不需要存储与过期清扫。
- 豁免：自带签名校验的回调路由用 `Csrf.Skip` 登记
前缀。

- static string CookieName="csrf_token";

- static string HeaderName="X-CSRF-Token";

- static string FormName="_csrf";

- static List<string> skips;

- static void Skip(string prefix)
  - 登记豁免前缀：路径以其开头即不拦截。用于支付回调、
    webhook 等自带签名校验、调用方不可能持有 cookie 的路由。

- static bool Skipped(string path)
  - 路径是否命中任一豁免前缀。

- static bool Guard(HttpContext ctx)
  - Before 钩子本体。返回 false 表示已写好 403 响应，
    管线短路。前哨先做 Origin/Sec-Fetch-Site 站点校验（浏览器自动
    附带、免客户端配合），double-submit token 是第二道闸。

- static string NewToken()
  - 新 token：16 随机字节的 hex（128 位熵）。

- static bool ConstantTimeEquals(string a, string b)
  - 常时比较：按较长一方的长度累积异或，长度不等直接计入差异位，
    不因首个差异字节提前返回。

- static bool OriginGuard(HttpContext ctx)
  - 站点校验前哨（Before 钩子形态）：不安全方法上依据
    浏览器自动附带的证据拒绝跨站请求，无需页面/脚本配合——
    Sec-Fetch-Site: cross-site 直接拒；带 Origin 头且其 authority
    与 Host 不符也拒。两样都缺（老浏览器、非浏览器客户端）放行，
    此时 SameSite=Lax 会话 cookie 仍是底线。安全方法一律放行。

- static bool OriginCheck(HttpContext ctx)
  - 前哨的纯校验半口：true 放行，false 跨站（应答由钩子写）。

- static bool SameAuthority(string origin, string host)
  - Origin 的 authority（冒号后段，去默认端口、小写）是否与 Host
    一致。大小写不敏感按 RFC 3986；Host 缺失视为不一致（保守）。

- static string TrimDefaultPort(string auth, bool isHttps)
  - 去掉明确的默认端口后缀（https 的 :443 / 明文的 :80）；
    其余端口保留参与比较。


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
  - 鉴权级别；未赋值即 Unknown（按 None 处理）。

- string Component{ get set}
  - 前端组件名（构造默认 "vlist/index.vue"）。

- string Icon{ get set}
  - 菜单图标（构造默认 "mdi:antenna"）。

- int ApiMax{ get set}
  - 每秒限流上限（默认 1000）。

- =1000;

- ContentTypes ContentType{ get set}
  - 响应内容类型（默认 ApplicationJson）。

- =ContentTypes.ApplicationJson;

- string Lock{ get set}
  - 请求锁维度："" 无 | "user" | "global"（同 Route.Lock）。

- ="";

- bool Upload{ get set}
  - 请求体流式落盘（同 Route.Upload）。

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


## DesignerHtml (class)

设计器文档的 HTML 存取编解码器（WEB_GUI_ROADMAP P7）：把设计器
的 JSON 文档模型与 HTML 文本互相转换——HTML 从此是设计器
的存取格式。纯字符串层，不依赖 Gui：设计器
(Gui.Designer)、编译期生成器 (GenForm/GenHtml) 两端共用。

编码规则（全键保真往返）：
- 文档级键（name/winW/role/...）→ <body> 的 data-<kebab> 属性；
name 另发 id 便于阅读；body 带 data-zan-design 标记（IsDesignDoc
据此区分设计稿与普通 HTML UI 文档）。
- 字段对象 → 元素：kind 决定 tag，能原生就原生（Panel→div、
Label→label、Button→button、Input→input、TextArea→textarea、
Image→img、Divider→hr、Progress→progress、SelectBox→select、
Typography→p、Checkbox/Radio/Slider/ColorPicker/InputNumber/
DatePicker→input+type=…，其余 div），kind 本身存 data-kind
（自定义组件/共享控件的权威标记，读端优先于 tag 兜底）；
name → id；class → class；style → style；kids → 子元素。
- 原生属性/内容直进原生通道：<img src/alt>、<input/textarea
placeholder/value>、表单族 min/max/step、<progress value/max>、
checkbox/radio 的 value(=label)/checked(=defOn)、<select> 的
<option> 子元素（含 selected）、button/label/textarea/p 的
文本内容——与 data-* 通道等价，读端不分来源。
- 其余标量键 → data-<kebab>="值"（camelCase 键名 kebab 化——
解析器把属性名整体小写，kebab 保大小写信息；false/""/0 跳过，
与 HTML 布尔属性同趣，读端有缺省）。
- 事件键 on<Event> → data-on-<kebab 事件名>：与 P5 运行时事件
协议 data-on-<evt>="名" 完全同形（LoadHtml 与设计稿一致）。
- 复合值（props 直通表、DataGrid columns、winShape 区域数组…）
→ data-x-<kebab>='<紧凑 JSON>'，读端原样解析回对象/数组——
字符串值不作数字/布尔嗅探，props 的 "min":"0" 不会漂成数字。
- options（字符串数组）→ data-options="a|b|c"（与 JoinOpts 同
分隔，| 内嵌限制相同）。
- 属性值统一实体转义（& < > "）；原生语义发原生形态——img 的
src/alt、input/textarea 的 placeholder/value 发真属性，
button/label/textarea 的 text 发元素文本（读端与 data-* 通道
等价），其余键全在属性里，规避 HTML 空白塌缩。

- static bool IsDesignDoc(string html)
  - 是否设计器文档：body 带 data-zan-design 标记。标记是裸属性
    （值为空串），不能用 Attr(...) != "" 判——那样存在与缺失
    不可区分，永远返回 false。

- static string FromJsonDoc(string json)
  - 设计器 JSON 文档 → HTML 文本。

- static string ToJsonDoc(string html)
  - HTML 文本 → 设计器 JSON 文档（PrettyToJson 字符串）。

- static void FieldHtml(StringBuilder b, JsonValue o, int depth)

- static JsonValue FieldJson(WDoc doc, WNode n)

- static void PutAttr(JsonValue o, string suffix, string v)
  - 一个 data- 后缀属性折进对象：on-<kebab> → "on"+Pascal 事件键；
    x-<kebab> → 复合 JSON 或字符串的未建模键；其余 → Unkebab 键 +
    标量嗅探（true/false → 布尔，整数/小数字面 → 数字，其余字符串）。

- static void AppendKey(StringBuilder b, JsonValue v, string key, int mode)
  - 键值 → 属性写出发射（标量 false/""/0 跳过；复合值走 x- JSON）。
    零值/空串有义的键豁免跳过：几何四键 fx/fy/fw/fh（矩形边界，
    FieldJson 无条件写出，丢了会在解码端回落到 32 的缺省高）、
    winZoom（0 = 适应视口，模型缺省 1000）、layoutMode（0 = 吸附/
    流式布局，丢了读端回落 1 变自由布局）、pad/gap（0 = 显式无
    内边距/间距，压过样式表缺省）、label/placeholder（"" = 显式
    清空，缺了会回落 kind 缺省文案）。其余键 0/"" == 读端缺省，
    跳过保持文档干净；
    新键是否入名单由模型级往返等价校验兜底。

- static string KebabKey(string s)
  - "winW" → "win-w"；"onClick" → "on-click" 由调用方拆 on 前缀后
    调用（这里只处理纯 Pascal/camel 段）。

- static string Unkebab(string s)
  - "win-w" → "winW"；"row-click" → "RowClick"（事件端用）。

- static bool IsNumeric(string s)

- static string EscapeAttr(string s)
  - 属性值实体转义。

- static string KindTag(string kind)
  - kind → 存储 tag。有 HTML 原生等价物的走原生标签；Panel 走裸
    div（布局兜底形态，kind 由 TagKind("div") 恢复）；其余组件走
    `zan-<kebab>` 自定义元素（HTML5 合法标签名，tokenizer 的
    IsNameChar 收连字符）——组件化写法，不再骑在 div 上打标记。

- static string TagKind(string tag)
  - tag → kind（读端兜底；data-kind 缺失时）。zan-<kebab> 自定义
    元素反解回 Pascal kind，与 KindTag 的默认分支互逆。

- static string InputTypeKind(string ty)
  - input 的 type 属性 → 组件 kind（<input type=checkbox> 是 Checkbox
    而不是 Input）。只在 data-kind 缺失、kind 从标签兜底而来时细化：
    data-kind 是权威标记，写了就不改写。

- static string KindInputType(string kind)
  - input 变体组件导出时发哪个 type 属性。


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
  - 是否空白符：空格 / TAB / CR / LF。

- static string Clean(string s)
  - Trim surrounding whitespace and drop CR/LF (TAB -> space) so a
    value cannot smuggle header/log-injection control characters.

- static string Html(string s)
  - Clean + escape the five HTML-significant characters. Use this
    for any value that will be rendered back into HTML.

- static int DigitValue(int c)
  - '0'–'9' 映射 0–9，其余 -1。

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
  - 注册 before 钩子。

- void After(AfterFn fn)
  - 注册 after 钩子。

- bool RunBefore(HttpContext ctx)
  - Runs before-hooks in order; false = request short-circuited.

- void RunAfter(HttpContext ctx, long elapsedUs)
  - 按注册顺序执行全部 after 钩子（不可短路）。


## HtmlParser (class)

- static JsonValue pendingHeadAttr;
  - head 的 data-* 属性暂存（head 是透明容器；P7 设计器文档把
    文档级元数据放 body 属性，head 侧只透传 data- 键）。

- static bool inHead;
  - 解析游标是否在 <head> 区内（head 透明，子元素按 head 语义
    处理：不建节点、不触发隐式 body；</head> 之后当正文）。

- static WDoc Parse(string html, string baseDir)
  - 解析 `html`（整文档或片段）为声明记录。`baseDir` 是 `<link>`
    相对路径的基目录（空 = 不解析 link）。

- static WOpenResult OpenTag(string html, int i, List<string> an, List<string> av)
  - 扫开标签：名字 + 属性表 + 自闭合标记，返回游标。

- static void HandleOpen(List<WFrame> st, WDoc doc, string name, List<string> an, List<string> av, bool selfClose, string baseDir)

- static WFrame Frame(WDoc doc, int rec, string tag, int mode)

- static int PushNode(WDoc doc, string tag, int parent, List<string> an, List<string> av)

- static void CloseTag(List<WFrame> st, WDoc doc, string name)

- static void CloseTop(List<WFrame> st, WDoc doc)

- static void FeedText(List<WFrame> st, string raw)

- static void Flush(WDoc doc, WFrame f, bool nextInline)
  - 结算 pending 文本。`nextInline`：紧随其后的节点是否行内级——
    尾随空格只在后面还是行内内容时保留（HTML 空白语义）。


## HtmlScope (class)

HtmlScope.zan -- HTML 文档 CSS 的编译/装载期作用域收窄（Vue
<style scoped> 同语义），GenHtml 生成通道与 App.LoadHtml 运行期
装载通道共用这一份实现。给定作用域类 zs，把样式表里每条规则的
每个复合块补上该类——同名类跨文档互不串、裸 body 只作用自己的
根。@media/@supports 内层递归；@keyframes/@font-face 等块原样；
注释与字符串不参与结构判定；已含 zs 的块跳过（幂等）。
作用域类由调用方造：编译通道取文档基名（zs-<基名>，与生成类名
同源），运行期装载取序号（zs-load-<n>）。运行期挂载件（列表行
模板等 Build 之外长出的控件）不携作用域类，其样式放皮肤层。

- static string Styles(string css, string zs)
  - 样式表 scoping 主入口：逐顶层段（注释 / at 规则 / 普通规则）
    走一遍，规则 prelude 改写、块体原样。

- static int ScopeAt(string css, int i, string zs, StringBuilder outp)
  - at 规则：返回处理后的下一个下标。@media/@supports 递归内层；
    其余（@keyframes 的百分比段、@font-face、未知块）整块原样。

- static string ScopePrelude(string sel, string zs)
  - 规则 prelude（选择器列表）：顶层逗号切分，逐片 scoping 后拼回。

- static string ScopeOne(string piece, string zs)
  - 一片选择器（可能带组合器链）：逐复合块补 zs。注释剥离、空白
    归一；显式组合器（>/+/~）覆盖待定后代空格。

- static string ScopeCompound(string chunk, string zs)
  - 单复合块补 zs：插到主体名/类列之后、首个伪类/属性之前——
    类列顺序无关语义，且保证类在 `::before`/`[attr]` 前的语法序。
    已含该类则原样（幂等）；无主体（裸 `:hover`）补 `*` 主体，
    否则规则会泄成全局。

- static int SkipNoise(string s, int i)
  - 越过 i 处的注释/字符串（可连续），返回首个"实"字符下标。

- static int MatchChunk(string s, int open)
  - 括号/引号块的配对下标（引号支持 \\ 转义，括号计嵌套，
    注释/内层字符串不参与配对）；找不到返回 -1。

- static int MatchBrace(string s, int open)
  - '{' 的配对 '}'（注释/字符串感知）。

- static bool HasClsToken(string chunk, string zs)
  - 复合块里是否已有 zs 这个类词（".zs-x" 后跟非名字字符）。

- static int FindFrom(string s, string needle, int start)

- static bool StartsWithAt(string s, int at, string needle)

- static bool NameChar(string c)

- static bool Alnum(string c)


## HtmlText (class)

data-on-* 解析出的逻辑事件名映射（"click" → "Click"）独立于
Gui 的事件类型，供生成器与运行时共用。

- static string EventKind(string suffix)
  - data-on-* 后缀 → AddByName 事件名。未映射的后缀返回空
    （静默忽略：HTML 引用宿主没接的事件不该炸）。

- static string Attr(List<string> an, List<string> av, string name)

- static List<string> SplitClass(string v)

- static bool StartsWith(string s, string p)

- static string CollapseSpace(string s)
  - 连续空白塌缩成单空格（HTML 文本语义），不 trim 首尾。

- static string TrimLead(string s)

- static string TrimTrail(string s)

- static string DecodeEntities(string s)
  - 命名实体（amp/lt/gt/quot/apos/nbsp）+ 数字实体（' '）。

- static int ParseIntOr(string s, int dflt)
  - 十进制整数（容 '-'）；非数字串返回 dflt。

- static int ParseHex(string s)

- static bool IsAlpha(int c)

- static bool IsNameChar(int c)

- static bool IsSpaceChar(int c)

- static int FindFrom(string s, string sub, int from)

- static int FindCloseTag(string s, string tag, int from)
  - 大小写不敏感地找 `</tag`（原文模式收尾）。

- static bool IsInlineTag(string t)
  - 行内级 tag 表：块表用白名单（UA 样式表 display:block 的集合
    + 原文/元数据 tag），其余一律按 Chrome 缺省 inline 处理。

- static bool IsBlockTag(string t)

- static bool IsVoidTag(string t)


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
  - 已登录的主体 id（匿名时为 ""）。

- string routeAction;
  - 命中路由的动作名（"Users.Save"）。

- string routePattern;

- int routeSlot;
  - 命中路由的下标（O(1) 按路由记账）；非路由（如资源）为 -1。

- string uploadPath;
  - 已落盘的 multipart 正文路径（无则为 ""）。

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
  - 构造请求上下文：解析 query，并对 POST/PUT/PATCH 的 urlencoded 或
    multipart 正文做一次表单解析。连接循环里正文是构造之后才缓冲齐
    的，那里会经 ParseFormBody 再解析一次；空正文不会重复解析。

- string RemoteIp()
  - 客户端 IP 文本（点分 IPv4）。

- long RemoteIpLong()
  - 对端 IPv4 的整数形式（Socket.Ipv4ToLong）。

- static void ParseFormBody(HttpRequest req, HttpContext ctx)
  - Parses the urlencoded form body again, now that the body has
    finished
    buffering (the initial parse only saw the first packet).

- static void ParseMultipartFields(HttpRequest req, HttpContext ctx)
  - multipart/form-data 的文本字段进 form 表。浏览器在同一张
    表单里混排文本输入与文件输入，此前只有 urlencoded 的字段被解析，
    multipart 请求里的文本字段在服务端不可见。文件部件不在这里展开：
    流式上传路径的正文已整体落盘（WebApp.StreamBodyToFile），内存路径
    的文件部件由处理器按需取用——只有带 filename= 的部件被跳过，
    其余部件的值原样进入 form（multipart 值不做百分号解码）。
    与 ParseFormBody 一样先 Clear：构造函数可能已按首个 TCP 分段的
    部分正文解析过一次，正文缓冲齐后的二次解析必须整体重放，
    不能把不完整的字段值留在表里。

- static string DispositionName(string head)
  - 部件 Content-Disposition 里的 name 值；带 filename= 的
    部件是文件，返回空串让调用方跳过（文件字节不能混进 form 表）。

- static int FindFrom(string hay, string needle, int from)
  - 从 from 起查找子串首次出现，返回下标；无则 -1。

- static int FindNoCaseFrom(string hay, string needle, int from)
  - 同 FindFrom，但 ASCII 大小写不敏感。

- string Param(string name)
  - 路由参数（{param} 段匹配到的值）。

- string Query(string name)
  - query string 参数。

- string Form(string name)
  - 表单字段（urlencoded 或 multipart 的文本字段）。

- string Body()
  - 原始请求正文。

- JsonValue JsonBody()
  - Request body as JSON, parsed once and cached. Returns null
    for an empty body. Lenient parse: a malformed body yields whatever
    partial tree the reader recovered rather than an exception — callers
    validating user input should still check the fields they read.

- string InputString(string name)
  - 按 route → query → form 顺序取原始字符串（未过滤）；
    三处都缺时返回空串。

- int InputInt(string name, int dflt)
  - 按合并顺序取整型；缺失、非整数或溢出时返回
    `dflt`。"42"、"-7"、"+7" 合法；"12abc"、"2.5"、"" 不合法。

- long InputLong(string name, long dflt)
  - 按合并顺序取 64 位整型；规则同 InputInt。

- bool InputBool(string name, bool dflt)
  - 按合并顺序取布尔："1"/"true"/"on"/"yes"
    （大小写不敏感）为真，"0"/"false"/"off"/"no" 为假，
    其余返回 `dflt`。

- static int ParseIntStrict(string v, int dflt)
  - 严格十进制解析：可选 +/- 前缀、其余必须全为数字、
    整串消费、int 范围校验；任何一条不满足返回 `dflt`。

- static bool AsciiEq(string a, string b)
  - ASCII 大小写不敏感等值（布尔词表用）。

- static long ParseLongStrict(string v, long dflt)
  - 严格 64 位解析，规则同 ParseIntStrict；累加中途变负（64 位回绕）
    视为解析失败。

- string Header(string name)
  - 按名取请求头；不存在返回 ""。

- string Cookie(string name)
  - Reads one cookie from the Cookie header, or "".

- bool IsAjax()
  - X-Requested-With 恰为 XMLHttpRequest（仅认这两种大小写写法）时为真。

- string InRaw(string name)
  - Raw, UNFILTERED value (route param -> query -> form). Only use
    when you deliberately need the untouched bytes.

- string In(string name)
  - Filtered value: trimmed, control chars stripped. The default
    safe way to read a parameter.

- string InText(string name)
  - Filtered + HTML-escaped value, safe to render into a page.

- int InInt(string name, int def)
  - Filtered integer with a fallback for missing/invalid input.
    Full-consumption strict parse with range check: "12abc", "2.5" and
    out-of-range values all fall to `def` (an atoi-style prefix parse
    would accept "12abc" as 12, and an unchecked accumulator would wrap).

- long InLong(string name, long def)
  - Filtered long with a fallback for missing/invalid input.
    Same strict rules as InInt, widened to 64 bits.

- double InDouble(string name, double def)
  - Filtered number with a fallback for missing/invalid input.

- bool InBool(string name, bool def)
  - Flag parameter: "1"/"true"/"on"/"yes" against
    "0"/"false"/"off"/"no", so a checkbox and an API client read alike.

- bool HasIn(string name)
  - True when the parameter is present in form, query or route.

- HttpContext Status(int code, string text)
  - 设置状态码与原因短语（可链式）。

- static string HeaderSafe(string s)
  - A header name or value with CR, LF and NUL removed. Applied at
    the sink rather than left to the caller: a value that reaches a header
    is often a redirect target, a filename or a token read straight from the
    request, and one CRLF in it ends the header block and lets the client
    write the rest of the response (header injection / response
    splitting).

- HttpContext SetHeader(string name, string val)
  - 追加响应头；名称与值都经 HeaderSafe 清洗（可链式）。

- HttpContext SetCookie(string name, string val, int maxAgeSeconds)
  - 设置 HttpOnly + SameSite=Lax 的会话 cookie（可链式）。请求经
    TLS 反代（X-Forwarded-Proto: https）时追加 Secure——明文直连
    不加，避免本地开发/无 TLS 部署拿不到 cookie。

- HttpContext SetCookieJs(string name, string val, int maxAgeSeconds)
  - Sets a JS-readable cookie (no HttpOnly). The only
    legitimate use is the CSRF double-submit token: the browser script
    must read the cookie to echo it in X-CSRF-Token, so it cannot be
    HttpOnly. SameSite=Lax stays, which is itself the first line of
    defense. Everything else should use `SetCookie`.

- static string SecureFlag(HttpContext ctx)
  - TLS 反代协议（X-Forwarded-Proto: https）→ "; Secure"，否则空串。

- void Html(string html)
  - 以 text/html; charset=utf-8 应答并结束本请求。

- void Text(string text)
  - 以 text/plain; charset=utf-8 应答并结束本请求。

- void Binary(string mime, byte[]data, int len)
  - Answers with `len` raw bytes of `data`. The only way to serve
    binary content correctly: a byte array plus its length, never a string
    whose length stops at the first NUL.

- bool IsBinary()
  - True when the response body is bytes: the server sends the
    header block and then <c>bodyBytes</c> by count.

- bool StatusForbidsBody()
  - Statuses whose response ends at the header terminator,
    regardless of any body the handler prepared (RFC 9112 §6.3).

- int BodyLength()
  - Response body size in bytes. HEAD retains the corresponding
    GET length; bodyless statuses return zero so the separate binary send
    path cannot put forbidden bytes after the header block.

- void Json(string json)
  - 以 application/json 应答并结束本请求。

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
  - 302 跳转：Location 经 HeaderSafe 清洗，正文为空。

- void __Attach(TcpClient client)
  - Serializes the response into a raw HTTP/1.1 message.

- nint ClientSock()
  - The live connection's socket, for handlers that must manage
    their own deadlines (an SSE stream that legitimately stays silent for
    minutes while the upstream model thinks). Returns 0 before __Attach.

- HttpDeadlineToken deadlineSlot;
  - The per-request deadline slot this connection rides on. A
    long-lived stream handler must Touch it periodically (see
    ClientStreamWatch): the connection sweeper shuts sockets down at the
    deadline even mid-stream, which used to cut every SSE response that
    ran past the server's request timeout (default 30s) — the exact
    "upstream never finished but the client got dropped" signature.
    null when the connection predates deadline tracking.

- int streamIdleMs;

- HttpFramer framer;

- HttpDeadlineToken DeadlineSlot()
  - 本连接挂载的期限槽（长流处理器须定期 Touch，见 deadlineSlot 字段说明）。

- void __SetDeadlineSlot(HttpDeadlineToken slot, int idleMs)
  - 由连接循环注入期限槽与空闲窗口（应用的 requestTimeoutMs）。

- void __SetFramer(HttpFramer framer)
  - 由连接循环注入定界缓冲（WS 升级时从中切出管线其后的帧字节）。

- void TouchDeadline(int timeoutMs)
  - Re-arms this connection's request deadline for another full
    window. A streaming handler calls this every time it has evidence the
    stream is alive — data relayed, or a keep-alive ping accepted. Without
    it the connection sweeper shuts the socket down at requestTimeoutMs
    (default 30s) regardless of stream health, which is why long SSE
    relays used to die mid-answer with "client disconnected".
    No-op when the connection carries no deadline.

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
  - 该连接累计推出的帧字节数（流式连接的流量度量）。

- async bool SseOpen()
  - Answers the `text/event-stream` handshake and takes the
    connection over. Everything after this is written by the handler with
    SseSend / SsePing; the loop it runs ends when the subscriber goes away
    (a send fails). false = no connection to stream on.

- async bool SendAllTcp(string data)
  - 循环直到整段写出。TcpClient.SendAsync 是单次非阻塞
    send：TCP 背压下（慢订阅者 + 快上游——繁忙转发的常态）它经常
    短写，此前按 frame.Length 记账等于把没发出去的事件当已送达，
    下一事件从半帧中间开始——浏览器收到的补全是坏 JSON。
    false = 对端已断开。

- async bool SseSend(string name, string data)
  - Pushes one event. `name` "" sends the default "message" event.
    Each newline in `data` becomes its own data: line, as the wire format
    requires. false = the subscriber is gone; stop the loop.

- async bool SsePing()
  - Comment line, used as a keep-alive so idle proxies do not drop
    the stream.

- bool IsWebSocketRequest()
  - Whether this request asks to upgrade to WebSocket: an
    Upgrade header whose token list contains "websocket" (case
    insensitive) plus a non-empty Sec-WebSocket-Key. Anything else is an
    ordinary request and goes down the normal response path.

- string BuildResponse(bool keepAlive)
  - 序列化为完整 HTTP/1.1 报文：状态行、Content-Type/Length、依
    keepAlive 的 Connection 头、Server 头，再接已清洗的自定义头。
    字节正文不并入（防 NUL 截断），由调用方按 BodyLength 另行发送。

- static string Itoa(int v)
  - Decimal formatting for the response line without going
    through Convert.ToString (which formats via printf). Digits are
    string literals, so the whole conversion is allocation-free apart
    from the builder append.

- static string Digit(int d)
  - 单个十进制数字的字面量（d 取 0–9）。

- static bool StartsWith(string s, string prefix)
  - 前缀判断（大小写敏感）。

- static void ParsePairs(string s, StrMap into)
  - Parses "a=1&b=2" pairs (URL-decoded) into a StrMap.


## HttpDeleteAttribute (class)

Restricts the action to DELETE requests.


## HttpGetAttribute (class)

HTTP verb selectors (default GET when none is present).


## HttpPatchAttribute (class)

Restricts the action to PATCH requests.


## HttpPostAttribute (class)

Restricts the action to POST requests.


## HttpPutAttribute (class)

Restricts the action to PUT requests.


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
  - 页码（从 1 起）。

- int Limit()
  - 每页行数（钳制在 [1, max]）。

- string Order()
  - 排序键（由控制器映射到具体列）。

- bool Desc()
  - 是否降序（仅 `?dir=asc` 得到升序）。

- string Kw()
  - 搜索文本（未搜索时为 ""）。

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
  - 列表响应 JSON（{"items","total","page","limit","pages"}）；
    items 是已序列化的行 JSON，经 PutJson 挂树（能解析就保型嵌入，
    解析不出按字符串存——无论如何不会产出非法 JSON），空时输出 []。


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

请求锁的租约表（匿名共享内存）：一个键同一时刻只有一个持有者，拿
不到租约的请求由调用方回答 429。经 SharedTable.TryAcquireLease 加锁
（30 秒租约窗口）、ReleaseLease 释放；表未就绪的管理器不发放租约。

- SharedTable table;

- long ownerSequence;

- long leaseMs;

- LockManager()

- static SharedTable NewTable()
  - 新建锁表：65536 键位，单列 owner。

- static LockManager Owned()
  - Locks in anonymous shared memory this process owns: no name for
    another server on the machine to collide with or reach into, and workers
    get the same leases through `OsHandle` / `Attach`.

- static LockManager Attach(long osHandle)
  - Locks in the anonymous table another process created and handed
    down. Handle 0 (nothing was handed down) leaves the manager closed, and a
    closed manager grants no lease -- a locked route is refused rather than
    let through unprotected.

- long OsHandle()
  - The handle a worker needs to map this manager's table, or 0
    when no table was created.

- static string KeyFor(Route route, string principal)
  - 依 [Lock] 维度构锁键："user" → "u:<principal>:<路由键>"
    （匿名者记 _anon_），"global" → "g:<路由键>"；未声明锁维度
    返回空串。

- LockLease TryAcquire(string key)
  - 取租约：键为空、表未就绪或键已被持有都返回 null。

- void Release(LockLease lease)
  - 释放租约（须为本持有者）；lease 为 null 或表未就绪时静默返回。


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
  - 排序键：节注册序 → 节名字典序 → URL 字典序。


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
  - Menu text (the action's [Description]).

- string Path()
  - Entry URL.

- string Group()
  - Section segment this entry sits under ("system").

- string Icon()
  - Icon name ([Custom(Icon=...)]), "" when none.

- bool Active()
  - Whether the current request URL is this entry.


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
  - 构造限流器；windowMs 为固定窗口宽度（毫秒），非正数忽略。

- static SharedTable NewTable()
  - 新建计数表：65536 个键位，每键 count 与 window_start 两列。

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
  - The handle a worker needs to map this limiter's table, or 0
    when no table was created.

- bool Allow(string key, int limit)
  - 本窗口内 key 是否仍允许通过：limit<=0 恒允许；表未就绪一律拒绝
    （fail closed，被限流的路由不会悄悄放开）。

- int CountOf(string key)
  - key 当前窗口已计的请求数；表未就绪返回 0。


## ReadOnlyAttribute (class)

Opt in one pure read action to a read replica before the request
lease is acquired. Never infer this from GET: a GET can write, and actions
that require read-after-write consistency must stay on the primary.


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
  - [Title] 人类可读标题（菜单文案 / 文档）。

- int rateLimit;
  - [Custom(ApiMax=n)] 每秒请求上限；0（默认）不限——限流须显式声明。

- string rateScope;

- bool needsLogin;

- bool needsAuth;

- string lockScope;

- bool streamUpload;

- bool inMenu;
  - [Menu] 在管理菜单展示本路由；节与文案来自 URL 和 [Description]。

- List<ApiParam> docParams;

- StrMap meta;
  - 自由属性表（Rank/ApiMax/Component/Icon/ContentType/...）。

- int idx;

- Route(string method, string pattern, HttpHandler handler)
  - 构造路由：模式立即按 '/' 预切分为段。

- Route Named(string action)
  - 动作名（"Controller.Method"，文档/菜单/诊断用）。

- Route Title(string title)
  - 标题（菜单文案 / 文档 summary）。

- Route Limit(int maxPerWindow)
  - 每秒限流上限；0 不限流。

- Route LimitBy(int maxPerWindow, string scope)
  - 同时设限流上限与维度（"route"/"uid"/"ip"）。

- Route RateBy(string scope)
  - 只设限流维度不改上限。

- Route Login()
  - 要求已解析的登录用户（未登录 401）。

- Route Auth()
  - 要求登录 + 对 `action` 的权限（无权 403）；蕴含 Login。

- Route Lock(string scope)
  - 请求锁维度："" 无 | "user" 用户级 | "global" 全局。

- Route Upload()
  - 请求体流式写盘而非驻留内存（大文件上传）。

- Route Menu()
  - 在管理菜单中展示本路由（节与文案来自 URL 和 [Description]）。

- Route Meta(string key, string val)
  - 写入自由属性（Rank/ApiMax/Component/Icon/ContentType/...）。

- Route Param(string name, string type, bool required, string def, string desc, string source)
  - Declares one request parameter (emitted by the compiler from the
    action body). `source` is "path" for a {segment} and "query" otherwise.

- string MetaGet(string key)
  - 读自由属性；缺失返回空串。

- string Key()
  - 预计算的 "METHOD pattern"（每请求统计/锁键）。


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
  - 注册一条路由并返回 Route（可继续 fluent 配置）。
    无 {param} 段的路由进静态桶并建哈希索引，其余进动态桶。

- void HashRouteKeys()
  - Rows are addressed by the hash of the route key, so hash every key once
    here instead of on the request path.

- SharedTable NewStatsTable()
  - 新建统计表：每路由一行，hits 与 total_us 两列（容量为路由数+4）。

- void SeedStats(SharedTable t)
  - The process that creates the table seeds every route row, so a worker can
    Increment on a pre-existing row without triggering a capacity-limited
    auto-create under concurrent first requests.

- void InitStatsShared()
  - Create the stats table as anonymous shared memory
    owned by this server. It has no name for anything on the machine to
    collide with or read, and the workers get it from the master through
    `StatsOsHandle` / `AttachStats`, so it must be
    created before the first worker is spawned.

- long StatsOsHandle()
  - The handle a worker needs to map the anonymous stats table, or 0
    when there is no such table (none was created).

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
  - GET 快捷（= Add("GET", ...)）。

- Route Post(string pattern, HttpHandler handler)
  - POST 快捷。

- Route Put(string pattern, HttpHandler handler)
  - PUT 快捷。

- Route Delete(string pattern, HttpHandler handler)
  - DELETE 快捷。

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
  - 模式不含 {param} 段（即无 '{'）时为静态路由。

- static bool MatchSegs(List<string> pat, List<string> segs, StrMap into)
  - 段数相等且逐段相等；{param} 段匹配任意段并把值写入 into。

- static List<string> SplitPath(string path)
  - 按 '/' 切分并去掉空段（"/a//b/" -> ["a","b"]）。


## RowList (class)

Named values for a render: string variables plus named lists of row maps
(for {{#each}} blocks).

具名行集合：一个 {{#each}} 列表的数据源。

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
    
    Tokens are 32 CSPRNG bytes (RtlGenRandom / /dev/urandom) hex-encoded:
    256 bits of entropy an attacker cannot guess or forge. The previous
    scheme ("t" + wall-clock seconds + uid + map count) had zero entropy --
    anyone who knew a user's uid and could guess the second their session
    started owned that session. On CSPRNG failure we refuse to issue
    rather than mint something predictable; when the map grows past its
    flood cap it is cleared instead of growing without bound.

- string UserOf(string token)
  - Returns the uid for a bearer token / cookie, or "".


## StaticFiles (class)

- static string prefix="";

- static string root="";

- static int maxAge=86400;

- static const int MaxFileBytes=4*1024*1024;

- static List<StaticMount> mounts=new List<StaticMount>();

- static void Mount(WebApp app, string urlPrefix, string dir)
  - Mounts `dir` under `urlPrefix` and registers the hook on the
    app. `urlPrefix` is matched literally ("/static" answers
    "/static/css/app.css"); `dir` is relative to the process working
    directory.

- static void MaxAge(int seconds)
  - How long browsers may cache an asset (seconds). Set it to 0
    while developing so an edited file is picked up by a reload. Existing
    mounts follow: Mount snapshots maxAge into the mount record, so a
    MaxAge call that only touched the static default would be silently
    swallowed by every earlier Mount — the usual Mount-then-MaxAge order
    answered assets with the 86400 default (static_files_bounded caught
    it).

- static bool Serve(HttpContext ctx)
  - Before hook: answers asset requests, passes everything else on
    (true = keep going).

- static bool ServeFile(HttpContext ctx, string rel, string rootDir, string urlPrefix, int ageSeconds)

- static void ServeStream(HttpContext ctx, string rel, Stream file)
  - Buffered static response from an opened stream. The caller
    owns the stream. Reads use the checked length plus a one-byte EOF probe.

- static void ServeStreamMount(HttpContext ctx, string rel, Stream file, string urlPrefix, int ageSeconds)

- static bool TooLarge(HttpContext ctx, long length)

- static void Answer(HttpContext ctx, string rel, byte[]data, int size)

- static void AnswerMount(HttpContext ctx, string urlPrefix, int ageSeconds, string rel, byte[]data, int size)

- static string Relative(string path)
  - The path below the mount point, or null when the request is not
    for this mount.

- static string RelativeTo(string path, string urlPrefix)

- static bool IsSafe(string rel)
  - Only unreserved path characters, and no empty or dot-leading
    segment: that rules out "..", absolute paths, backslashes, NUL bytes and
    hidden files in one pass.

- static string ContentType(string rel)
  - 按扩展名（小写化后）映射 Content-Type；未知类型为
    application/octet-stream。

- static string Extension(string rel)
  - 末段最后一个 '.' 之后的扩展名（小写）；无扩展名或以 '.' 结尾为空串。


## StaticMount (class)

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

Bodies are not cached in workers: a public upload directory can contain an
unbounded number of files. HttpContext supports buffered binary responses,
not file streaming, so files above MaxFileBytes (4 MiB) return 413, including
HEAD. Larger downloads need a streaming file server. Upload acceptance is
separate and is not changed by this limit. Each in-flight GET owns at most
one bounded body; total memory still depends on connection concurrency.
Browser caching follows MaxAge. Binary bytes, including NUL, are preserved.
Trusted build-time embedded resources retain the runtime's own lazy decode
cache; this limit bounds their response copy, not runtime decompression.

- string prefix;

- string root;

- int maxAge;

- StaticMount(string prefix, string root, int maxAge)


## StrMap (class)

String-to-string map backed by the builtin Dictionary (a hash index over
insertion-ordered parallel buffers): Set/Has/Get/GetOr are O(1) amortized
where the old parallel-lists version scanned linearly. Route params /
query / form per request stay tiny, but Sessions.tokens and the template
caches grow with load, and that is where the O(n) scans showed up.

The backing dict is created on the first Set: most requests never touch
their route params, query, form or state map, and a null map that costs
nothing keeps the per-request allocation count down.

- Dictionary <string, string> map;

- StrMap()

- int Count()
  - 键数；尚未写入过为 0。

- void Set(string key, string val)
  - 写入键值（首次写入时才创建底层字典）。

- bool Has(string key)
  - 是否含该键。

- string Get(string key)
  - 取值；缺失返回空串。

- string GetOr(string key, string def)
  - 取值；缺失返回 def。

- void Clear()
  - 释放底层字典（回到未创建状态）。


## VNode (class)

One node of a compiled template: templates are parsed once (View.LoadDir)
into a tree, so rendering is a walk that only appends -- no scanning for
tags and no substring copies on the request path.

- int kind;

- string text;

- List<VNode> body;

- List<VNode> elseBody;

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
  - 值为空记一条 "<field> is required"（可链式）。

- bool Ok()
  - 尚无任何错误时为真。

- Validator MaxLen(string val, int max, string field)
  - 长度超过 max 记错误（可链式）。

- Validator MinLen(string val, int min, string field)
  - 非空且长度不足 min 记错误；空值跳过（可链式）。

- Validator IsInt(string val, string field)
  - Digits only (optional leading minus).

- Validator OneOf(string val, string allowedCsv, string field)
  - Value must be one of the comma-separated whitelist entries.

- static bool ContainsStr(string s, string pat)
  - Multi-character substring search.

- string ErrorsJson()
  - 错误列表的 JSON 数组。

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
{{#if name}}...{{else}}...{{/if}}   else branch when var is falsy
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
  - 清缓存并重读视图目录下全部模板（devReload 打开时每次渲染前调用）。

- void Put(string key, string body)
  - Store a template under `key`, compiled once so renders never re-parse.

- void LoadRec(string d, string prefix)
  - Recursively load templates, keying each by its module-qualified path so
    that same-named controllers in different modules don't collide. A file
    <Module>/View/<Controller>.<Action>.html becomes the key
    "<Module>.<Controller>.<Action>" (the "View" path segment is skipped),
    which matches the key the compiler-generated router injects via
    __SetView. layout.html stays reachable under the bare "layout" key.

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
  - Walk a compiled template, appending to `sb`. `content` are the page nodes
    that fill a layout's {{content}} hole; without them (a plain Render) the
    placeholder resolves by precedence: a same-named variable (row field or
    ViewData) interpolates escaped; only when nothing resolves is it emitted
    unchanged, matching the old text-substitution behavior (Bug#2).

- static string Lookup(string key, ViewData data, StrMap row)
  - Row fields shadow the global ViewData. Each map is probed once
    (Dictionary lookup is a hash probe, so there is no double walk to
    avoid any more).

- static bool HasVar(string key, ViewData data, StrMap row)
  - 该名字当前是否可解析（行字段或 ViewData 里有它）；只判存在，不取值。

- static string HtmlEscape(string s)
  - Escapes < > & " so variables are XSS-safe by default.

- static int Find(string hay, string needle, int from)
  - 从 from 起查找子串（原生 IndexOf；from<=0 从头找）。

- static int FindTag(string tpl, string closeTag, int from)
  - Finds a closing tag, skipping nested blocks of the same kind.

- static int FindElse(string tpl, int from, int end)
  - Finds a same-level {{else}} within [from, end): nested
    if/each blocks are skipped by depth counting, so an {{else}} inside
    an inner {{#if}}/{{#each}} belongs to that inner block, not to the
    caller's. Returns the position of the opening brace, or -1.

- static string Trim(string s)
  - 去除首尾 ASCII 空格。


## ViewData (class)

一次渲染的命名值集合：字符串变量（{{name}}）加具名行列表
（{{#each name}}）。

- StrMap vars;

- List<RowList> lists;

- ViewData()

- ViewData Set(string key, string val)
  - 设置字符串变量（可链式）。

- List<StrMap> AddList(string name)
  - 新增具名行列表并返回其行（直接追加行即为填充数据）。

- List<StrMap> ListOf(string name)
  - 取具名行列表；没有则 null。


## WDoc (class)

解析产物：声明记录 + 收集的样式（<style> 与 <link>）。

- List<WNode> nodes;

- string css;


## WFrame (class)

建栈帧。mode：0 普通容器，1 捕获文本（button/textarea），
2 原文收集（style），3 原文跳过（script/title）。

- int rec;

- string tag;

- int mode;

- string pending;

- bool prevInline;

- string label;


## WItem (class)

容器内容条目：kid >= 0 是子节点下标，否则是文本片 text。

- int kid;

- string text;


## WOpenResult (class)

开标签扫描结果（无 out 参数，用返回类承载游标）。

- string name;

- int next;

- bool selfClose;


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
  - 内存请求体上限（字节），超出回答 413。

- int maxUploadBytes;
  - 流式上传上限（字节），只作用于 [Upload] 路由。

- int requestTimeoutMs;

- int maxConnections;
  - 并发连接上限（0 = 不设上限；超限回答 503 并关闭）。

- string uploadDir;

- AtomicInt uploadSeq;

- string loginPath;

- int globalLimit;
  - 全局每秒请求上限（0 = 不限；超限 429）。

- AtomicInt totalRequests;

- AtomicInt activeConnections;

- bool reusePort;

- static WebApp active;

- WebApp(string host, int port)
  - 构造应用服务器并填入默认配置：请求体上限 2MB、流式上传上限
    512MB、请求超时 30s、并发连接上限 10000、上传目录 uploads；
    共享表（限流/锁/路由统计）由 InitShared 在派生 worker 之前创建，
    不在这里。

- string Host()
  - 监听主机地址。

- int Port()
  - 监听端口。

- WebApp Views(string dir)
  - 加载视图目录（编译进内存）并返回自身。

- WebApp MaxBody(int bytes)
  - 内存请求体上限（字节），超出回答 413。

- WebApp MaxUpload(int bytes)
  - 流式上传上限（字节），只作用于 [Upload] 路由，超出回答 413。

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
  - 流式上传的落盘目录（默认 uploads）。

- WebApp GlobalLimit(int perSecond)
  - 全局每秒请求上限，按 uid（匿名时按对端 IP）计；0 不限，超限 429。

- WebApp LoginPath(string path)
  - The sign-in page a navigation is sent to when the route needs a
    session it does not have. Unset (the default) answers every refusal with
    the JSON body, which is what an API-only server wants.

- WebApp ReusePort(bool on)
  - 多进程部署时以 SO_REUSEPORT 监听，由内核把连接分发给各 worker。

- void InitStats()
  - Initialise cross-process route stats. Called by InitShared;
    safe to call in every process -- a worker maps the master's table via
    `AttachShared` instead of creating one.

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
  - Worker 侧：映射主进程创建并下发的各表。主进程与单进程服务没有
    可继承句柄，返回 false，由它们自己建表。

- RateLimiter Limiter()
  - 按需创建；拥有各表的进程已在 InitShared 里提前建好。

- LockManager Locks()
  - 按需创建请求锁管理器；worker 进程由 AttachShared 映射主进程下发的表。

- WebApp Before(HookFn fn)
  - 注册 before 钩子（按注册顺序执行；返回 false 短路请求）。

- WebApp After(AfterFn fn)
  - 注册 after 钩子（请求结束后按注册顺序执行，收微秒耗时）。

- WebApp AuthResolver(AuthFn fn)
  - 安装 token→用户解析器；未安装时用内置 Sessions 表解析。

- WebApp PermissionResolver(PermissionFn fn)
  - 安装权限解析器；未安装时"任何已登录用户"即被允许。

- Route Map(string method, string pattern, HttpHandler h)
  - Generic route registration used by the compiler-generated
    __AttrRoutes.Register. Returns the Route so attribute metadata can be
    chained.

- Route Get(string pattern, HttpHandler h)
  - 注册 GET 路由（= Map("GET", ...)）。

- Route Post(string pattern, HttpHandler h)
  - 注册 POST 路由。

- Route Put(string pattern, HttpHandler h)
  - 注册 PUT 路由。

- Route Delete(string pattern, HttpHandler h)
  - 注册 DELETE 路由。

- string RenderPage(string name, ViewData data)
  - 渲染模板并套最近的布局；未加载视图时返回空串。

- string RenderFragment(string name, ViewData data)
  - Renders a template WITHOUT its layout. This is what an
    in-page update answers with: the browser already has the shell, so
    sending it again would replace the page instead of the panel.

- static int NowSeconds()
  - 墙钟秒（CRT time），上传文件名与错误日志时间戳用。

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
  - 置停标志并关闭监听 socket，accept 循环随之退出。

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
  - 一条 keep-alive 连接的完整生命周期：读入并定界请求头（超 64KB 回
    431）→ 解析（定界失败按 parseStatus 回 400/501）→ 流式上传落盘或
    缓冲内存正文（超限 413）→ 解析表单 → Dispatch → 写回应答，直到
    对端断开或非 keep-alive 为止。每个请求重置一次空闲期限（防
    slowloris）；SSE/WS 劫持的连接由 handler 接管，循环随即结束。
    清理走 finally：即便管线里有未被 catch 的异常展开到这一层，
    deadline 槽位、framer 缓冲、套接字与连接计数也必须归位。

- async void HandleConnectionInner(nint clientSock)
  - HandleConnection 的主体：watch/framer/client 三个局部资源
    在这里创建，正常结束路径统一清理；套接字本身的 Close 由
    HandleConnection 的 finally 负责（ShutdownBoth 无法替代——
    劫持连接之后的收尾也要真正释放描述符）。

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

- async bool LimitExceeded(HttpContext ctx, Route route)
  - 全局与路由限流的唯一判定点，在正文缓冲/解析之前调用。
    键语义与键序与原 DispatchInner 内联判定一致：全局限流按 uid（匿名
    回落 IP，再回落 _unknown_），路由限流按 rateScope 拼 uid/ip。返回
    true 表示超限，应答由调用方写出并断连。ctx.uid 顺带在这里解析好
    （AuthUser 只看请求头），DispatchInner 复用、不再重付会话查询。

- async string DispatchInner(HttpContext ctx, Route route, bool keepAlive, long startUs)
  - 认证之后的请求管线，按序：before 钩子 → 404/405 → 填路由元数据 →
    登录/权限门 → [Lock] 请求锁 → handler。全局限流与路由限流已前移到
    正文读取之前（HandleConnectionInner 的 LimitExceeded——被限请求不再
    花服务器的带宽与解析 CPU），uid 亦由门处解析好放在 ctx.uid。
    ApiError 变成它携带的应答；其余异常记录后回答 500，handler 结束
    时仍未写应答的也兜底 500。

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
  - 构造一条完整的 HTTP/1.1 JSON 应答字符串（Connection: close），
    用于 HttpContext 尚不存在的早期拒绝（413/431/503、定界失败）。

- static int FindHeaderEnd(string raw)
  - 报文中头部块的结束位置（"\n\n" 或 "\n\r\n" 之后的首下标）；
    没有空行返回 -1。


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
  - 登记当前应用（main 启动时调用一次）。

- static WebApp Current()
  - 当前应用；未设置时为 null。


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
    Command mode always keeps a supervising master, even with one
    worker.

- static async int RunCommand(WebApp app, int count, bool daemon)
  - Same as `RunCommand`, with <paramref name="daemon"/>
    standing in for `start -d`: config can ask for a background start
    without the command line repeating it.

- static void Prepare(WebApp app, int count, bool daemon)
  - 多进程路径的公共装配：初始化共享表、登记活动应用，再建一个
    `count` worker 的 Worker，把 ServeSock 接为原始连接回调，使
    路由/钩子/上传管线与单进程完全一致；daemon 时以守护进程运行。


## WebWs (class)

Web 层 WebSocket 升级装配（A332 肥边⑥）：把 HttpContext.WsUpgrade 的
帧编解码依赖（WsReader/WsWriter/WsAssembler/WsSession，连带 WebSocket
家族 866 行）从核心 HttpContext 外迁为独立类。HttpContext 是一切
WebApp 程序的必经类型——它的方法签名引用 WsSession 就会把 ws 编解码
拉进每一个纯 HTTP 服务。应用调用 <c>WebWs.Upgrade(ctx)</c> 即 opt-in，
升级前路由/权限已生效的语义不变（ctx 的握手/劫持状态经公开成员操作）。

- static async WsSession Upgrade(HttpContext ctx)
  - Completes the RFC 6455 handshake and hands the connection to
    a `WsSession`. The upgrade runs AFTER routing and
    authorization, so a WebSocket session on the MVC port inherits the
    route's full permission semantics — the part a standalone WS port
    cannot give you.
    
    Session lifetime follows the SSE hijack contract: the handler that
    called Upgrade owns the connection until it returns; the connection
    loop then sees hijacked and tears down without writing a response.
    null = not an upgrade request (answer normally) or the 101 write
    failed (peer gone).
    
    An idle session is cut by the connection sweeper at the request
    timeout. Recv/Send re-arm the deadline automatically on activity; a
    session that may go quiet should be kept alive with periodic Ping()
    pushes.


## WsSession (class)

Server side of a WebSocket session on the MVC port, created by
`WebWs.Upgrade`. One object owns the upgraded
connection: Recv() is the only way to read (ping/pong/close/fragment
reassembly are handled inside, so a handler never sees protocol
noise), Send* push to the peer without any inbound message first —
the server-push shape a notification channel needs and the echo-style
frame loops cannot express.

Concurrency model is one handler loop: Recv parks on the reactor until
a complete message arrives; pushes happen between Recv calls. Two
tasks pushing one session would interleave frame bytes — keep a
session on one loop (a hub that fans out to many sessions pushes each
session from one place).

Deadline: every Recv/Send re-arms the connection deadline, so an
active session lives as long as it talks. A session that may go quiet
(a notification bell waiting for events) must push Ping()
periodically or the connection sweeper cuts it at the request timeout.

Send failures set Open() to false and the loop exits; the handler
returning ends the session the same way the SSE hijack does.

- TcpClient client;

- WsReader reader;

- WsWriter writer;

- WsAssembler asm;

- HttpContext ctx;

- bool open;

- int lastOpcode;

- WsSession(TcpClient client, WsReader reader, WsWriter writer, WsAssembler asm, HttpContext ctx)

- bool Open()
  - Whether the session can still carry frames. False after a
    Close handshake, a protocol error, or a failed send.

- int LastOpcode()
  - The opcode (WsOpcode.Text / WsOpcode.Binary) of the message
    Recv returned last.

- async string Recv()
  - Reads the next complete data message (fragmented messages
    are reassembled first). Control frames are answered inside: Ping
    becomes a queued Pong, Pong is dropped, Close is echoed and ends
    the session. Protocol violations and oversized messages close with
    1002/1009. Returns null when the session is over (peer close, EOF,
    protocol error) — stop the loop.

- async string Recv(int timeoutMs)
  - Recv with an idle window: when no frame arrives within
    timeoutMs the call returns "" while Open() stays true and any
    partially buffered bytes survive for the next call — the handler's
    cue to Ping() (which doubles as the keep-alive that re-arms the
    connection deadline) and park again. A null return still means the
    session is over. timeoutMs non-positive waits indefinitely.
    
    An empty data message is indistinguishable from the idle window
    (both return "") — keep protocol payloads non-empty (heartbeats are
    Ping frames, not empty texts) and there is no ambiguity. This also
    lets a handler enforce its own session lifetime: a loop parked on
    Recv now wakes every window even for sessions that only ever see
    Pong traffic, so a wall-clock cap is checked on every tick instead
    of only when a real message happens to arrive.

- async bool SendText(string message)
  - Pushes one text message. false = the peer is gone; stop the
    loop (Open() is now false too).

- async bool SendBinary(string data, int len)
  - Pushes one binary message. `data` is a byte string that may
    contain NUL bytes; `len` is its exact length (never strlen).

- async bool Ping()
  - Pushes a Ping. Doubles as the keep-alive for sessions that
    may go quiet: the write both probes the peer (a dead socket fails
    here) and re-arms the connection deadline.

- async void Close(int code)
  - Closes politely with a status code (1000 normal, 1002
    protocol error, 1009 too big). Idempotent; Open() is false after.

- async bool FlushOut()
  - 把写缓冲里挂起的帧推到对端；失败即关闭会话并返回 false。

- void Touch()
  - 经升级来源的 HttpContext 续期连接空闲窗口；独立构造的会话无操作。


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
