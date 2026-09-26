# Zan.Mvc — ZanWeb Web MVC Framework

Enterprise web application skeleton modeled on a production swoole (ZxPHP)
framework, rebuilt on Zan's coroutine runtime — a layered controller/model
structure, an external config file, and the ORM and cache wired in by default.

Two layers sit under the application code:

- **`Zan.Mvc` package** (this package, `src/ZanWeb/…`) — the application
  framework:
  bootstrap (`ZanWeb.Boot`), config/DB/cache contexts, auth, RBAC, settings,
  schema + seed hook, metrics, job host, the sys entities and their DAOs, and
  the three controller base classes. Referenced by `using ZanWeb;` and pulled
  in automatically.
- **`System.Web` standard library** — the web kernel itself: `WebApp`,
  `Router`, `HttpContext`, `Controller`, `View`, `WebServer`, next to
  `RouteTable` / `RouteStats` / `PermTable`.

The template keeps only what is its own: the composition root, business
controllers/models/DAOs, the seed, views, static assets and config. Framework
fixes land in the package — a package refresh updates this template without
touching a line of its application code.

## Layout

```
config/app.json         runtime config (host/port/limits/db/cache) — NOT compiled in
src/main.zan            composition root only: menu sections, route hook, seed
                        registration, Boot.Run()
src/Seed/BlogSeed.zan   business seed — registered via Schema.OnSeed hook
src/Controller/         request handlers, one directory per module
  Index/Index.zan         HTML landing page
  Blog/Posts.zan          list / detail / comments / rss / sitemap (ORM + cache + views)
  Account/Login.zan       GET/POST /admin/login, /admin/logout (own bare layout)
  Admin/Dashboard.zan     GET /admin — metrics dashboard
  Admin/Users.zan         GET /admin/system/users + enable/disable, force logout
  Admin/Posts.zan         GET /admin/content/posts + publish/unpublish
  Api/Auth.zan            POST /api/auth/login, GET /api/auth/me
  User/Users.zan          /users, /user/{id}
src/Dao/Blog/           every query and write for the blog module
src/Model/Blog/         blog entities only: table structure, no queries
views/                  templates, in the module structure of the controllers
  layout.html             the site-wide page wrapper (global {{content}} layout)
  <Module>/*.html         that module's views; a module's own layout.html
                          overrides the global one for that module only
wwwroot/                the ONLY web-reachable directory, served at /static
```

The framework code (`ZanWeb` core, `Model.Sys`, `Dao.Sys`, the controller base
classes) lives in this package under `src/ZanWeb/…`. Business models/DAOs follow
the same per-module layout in the application (`src/Model/Blog`,
`src/Dao/Blog`) — same convention, app-owned.

`views/` and `wwwroot/` sit next to `src/`, not inside it, because both are read
at run time: a release is a copy of the executable plus `config/`, `views/` and
`wwwroot/` — `src/` is a build input and never ships. The view keys are
unchanged by the split: `views/Admin/Users.Index.html` is still
`Admin.Users.Index`, since `View.LoadRec` derives the key from the directory
path and the module directories are the same ones the controllers use.

## Attribute-driven routes

Controllers declare routing with **attributes** instead of hand-wiring in
`main.zan` (the Zan equivalent of the PHP `@title/@auth/@rank` docblocks). A
compiler pass scans the controllers and synthesizes `__AttrRoutes.Register(app)`,
which `main.zan` calls once — no generated file to maintain, no reflection.

```zan
class ApiController {
    [Post]                 // HTTP verb: [Get] [Post] [Put] [Delete] [Patch]
    [Title("收集数据")]     // human label (admin menu / docs)
    [Auth]                 // permission check required (implies login)
    [Rank(3)]              // minimum principal rank
    [Lock("user")]         // per-user request lock (double-submit guard)
    static void Gather(HttpContext ctx) { ... }   // -> POST /api/gather
}
```

| Attribute | Effect |
|---|---|
| `[Get] [Post] [Put] [Delete] [Patch]` | HTTP verb (required to mark a method as a route) |
| `[Route("/custom")]` | override the convention path |
| `[Title("...")]` | label for the admin menu / docs |
| `[Login]` | a logged-in principal is required (401 otherwise) |
| `[Auth]` | RBAC permission check (implies `[Login]`, 403 on failure) |
| `[Rank(n)]` | minimum principal rank |
| `[Lock("user"\|"global")]` | request lock; second concurrent call gets 429 |
| `[Limit(n)]` | per-route rate limit (requests/sec) |
| `[Upload]` | stream the request body to disk |
| `[Menu]` | surface this route in the admin menu (`/admin/menu`) |

**Shorthand: one combined `[Api(...)]` attribute.** Instead of stacking many
lines you can declare everything in one, mixing bare flags and `key=value`:

```zan
[Api(post, route="/api/gather", title="收集数据", auth, rank=3, lock="user")]
static void Gather(HttpContext ctx) { ... }
```

Keys: `get/post/put/delete/patch` (or `method="POST"`), `route="/x"`,
`title="…"`, `login`, `auth`, `rank=n`, `lock="user"|"global"`, `limit=n`,
`upload`, `menu` — booleans may be bare (`menu`) or explicit (`menu=true`). The
single attributes above still work and can be mixed with `[Api(...)]`.

**Routes follow the directory structure.** The path is
`<folders under src/controller> + controller name (minus "Controller") + action`,
lower-cased — `ApiController.Status` → `/api/status`, `Admin/UserController.List`
→ `/admin/user/list`. `Index` maps to the folder root. `[Route("...")]` overrides
the convention when you need an exception.

Custom rank/RBAC logic plugs in without touching the framework:
`app.RankResolver(fn)` where `fn(uid) -> int` (default: uid "1" is rank 9).

## Safe request input

Read every frontend parameter through the unified, filtered accessors on
`HttpContext` — resolved from the same place (form → query → route param) and
passed through the central `Filter` so sanitising is uniform:

```zan
string name = this.In("name");        // trimmed, CR/LF/TAB control chars stripped
string html = this.InText("bio");     // In + HTML-entity escaped (safe to render)
int    page = this.InInt("page", 1);  // tolerant integer parse with a fallback
long   since= this.InLong("since", 0);
double rate = this.InDouble("rate", 1.0);
bool   draft= this.InBool("draft", false);   // 1/true/on/yes vs 0/false/off/no
string raw  = this.InRaw("blob");      // UNFILTERED — only when you need raw bytes
bool   has  = this.HasIn("name");
```

**Required parameters read as one expression.** `Need*` is `In*` for a value the
action cannot proceed without: it aborts with the uniform
`{"code":"0003","msg":"标题不能为空"}` answer instead of repeating the same guard
in every handler. The label is the human name used in that message *and* in the
generated API docs.

```zan
string title = this.Need("title", "标题");      // 400/0003 when absent
string body  = this.NeedText("content", "内容"); // + HTML escaped
int    id    = this.NeedInt("article_id", "文章"); // 400 when absent or not an int
this.Abort(403, "1004", "不是作者本人");          // same uniform answer, on demand
```

## Built-in API documentation

`main.zan` calls `ApiDocs.Mount(app)`; nothing else is written or generated by
hand:

- `GET /api/docs` — offline reference UI (no CDN, no bundler, dark mode)
- `GET /api/docs.json` — OpenAPI 3.0 document

Both are built from what the compiler already knows: the route, verb,
`[Description]` title and auth/rank/menu flags come from the attributes that
*enforce* them, and the parameter list comes from the `In*`/`Need*` calls in the
action body. The read **is** the declaration — there is no parameter schema to
keep in sync, so an endpoint and its documentation cannot disagree:

```zan
[HttpPost]
[Description("保存文章")]
async void Save() {
    int    id    = this.InInt("article_id", 0);       // int, optional, default 0
    string title = this.Need("title", "标题");         // string, required, 标题
    string body  = this.NeedText("content", "内容");   // string, required, 内容
    ...
}
```

`{id}` segments in the route are documented as path parameters automatically.

`In*` centralises input handling; it is **not** a universal security layer.
Output/context escaping (HTML via `InText`, JSON via `JsonStr.Escape`),
parameterised SQL (the ORM binds values), and authorisation remain separate
concerns — filtering input does not replace them.

## Configuration (no recompile)

`config/app.json` is read at startup and is **not** compiled into the binary —
edit it and restart to change settings. Ship it next to the executable.

```json
{
  "server":   { "host": "127.0.0.1", "port": 8080, "maxBodyMB": 2, "globalLimitPerSec": 0 },
  "database": { "driver": "sqlite", "sqlitePath": "data/app.db",
                "host": "127.0.0.1", "port": 3306, "name": "app", "user": "root", "password": "" },
  "worker":   { "count": 1, "daemon": false },
  "cache":    { "driver": "memory", "redisHost": "127.0.0.1", "redisPort": 6379 }
}
```

`driver` accepts `sqlite` (default), `mysql`/`mariadb`, `postgres`. The DB
connection is opened lazily and tolerantly: if it is not configured/reachable,
the server still runs and DB-backed routes return a clear 503 instead of
crashing.

## Features

| Capability | Where | Notes |
|---|---|---|
| Layered controllers | `src/controller/` | thin `main.zan`, one class per resource |
| Default ORM | `src/model/`, `framework/Db.zan` | `System.Data.Orm` models, config-driven engine |
| Default cache | `framework/Cache.zan` | in-memory TTL; Redis via `System.Data.Redis` on async path |
| External config | `config/app.json`, `framework/Cfg.zan` | runtime-loaded, not compiled in |
| High-performance routing | `System.Web.Router` | static-first match + `{param}`, 404/405 |
| Rate limiting | `System.Web.Hooks` | global + per-route fixed windows, 429 |
| Auth / sessions | `WebApp.AuthUser`, `Sessions` | Bearer token or session cookie, `.Auth()` guard |
| Lifecycle hooks | `System.Web.Hooks` | typed delegates, run in order |
| In-memory views | `System.Web.View` | templates loaded ONCE at startup |
| Streaming uploads | `.Upload()` routes | body streamed to disk in 64KB chunks |
| Validation | `System.Web.Validate` | Require/MaxLen/IsInt/OneOf + SafeFileName |
| Scheduled jobs | `src/Feature/JobHost.zan`, `/admin/system/jobs` | in-process scheduler: second-granularity intervals, DB optimistic-lock claim (multi-worker safe), built-in kinds (`ping`, `log.cleanup`), run history with pruning |
| Shared-memory tables | `System.Web.RouteTable` / `RouteStats` / `PermTable` | route attributes, per-route timings, role×route rights across workers |

## Workers & scaling

Set `worker.count` in `config/app.json`:

- **`count: 1` (default)** — one process running the coroutine event loop. Each
  connection is handled in its own coroutine, so a single worker already serves
  thousands of concurrent connections. Best for development: logs stream into
  the IDE terminal.
- **`count: N` (Linux/macOS)** — the process binds with `SO_REUSEPORT` and
  re-launches itself as `N-1` extra worker processes bound to the **same** port;
  the kernel load-balances accepted connections across all workers, using every
  CPU core (this is how Workerman/swoole scale). Windows has no `SO_REUSEPORT`
  load-balancing, so it runs a single process there.
- **`daemon: true` (Linux)** — detaches from the terminal (`setsid`) and runs in
  the background.

Restart-on-crash is delegated to the process supervisor (systemd
`Restart=always`, Docker `restart: unless-stopped`, Kubernetes), the standard
way to supervise horizontally scaled services. `main.zan` boots via
`WebServer.RunCommand(app, count, daemon)` from the standard library, so the
binary is a service that answers `start`, `start -d`, `stop`, `restart`,
`reload` (rolling worker replacement) and `status` on the command line; the
running instance is addressed through its control port (the HTTP port +
10000, or `--ctl-port N`). `WebServer.Run(app, count, daemon)` is the plain
variant that only ever starts in the foreground. `-d` / `daemon: true` detach
on Linux only -- on Windows the process stays in the foreground (use NSSM or a
Windows service to run it in the background). Listener handoff,
respawn-on-crash, daemonization and the control port live in
`System.Net.Worker` / `System.Diagnostics.ProcessHost`, so any server gets
them, not just this template.

## Observability (`GET /admin/stats`)

The request lifecycle and database queries are instrumented into the shared
`System.Diagnostics.ServerMetrics` singleton, exposed as JSON at
`/admin/stats`:

```json
{
  "uptime_ms": 2015, "pid": 31084, "worker_id": 0,
  "cpu_ms": 31, "cpu_percent": 1, "mem_rss_bytes": 6852608,
  "requests": {"count":4,"errors":2,"total_us":13120,"avg_us":3280,
               "max_us":15400,"slow_threshold_us":500000,"slow_count":0},
  "queries":  {"count":2,"total_us":12800,"avg_us":6400,"max_us":8100,
               "slow_threshold_us":200000,"slow_count":0},
  "slow_requests": [{"req":"GET /slow -> 200","us":750000}],
  "slow_queries":  [{"sql":"SELECT * FROM huge_join","us":320000}]
}
```

- Every duration is MICROSECONDS (`*_us`). A request this server serves in
  200us is not measurable in milliseconds -- the Windows millisecond clock steps
  ~15.6ms -- so a millisecond average of a fast endpoint read as a flat `0`.
  The slow thresholds are still *configured* in ms (`SlowRequestMs`,
  `[log].slowMs`) and reported here converted.

- `cpu_ms` / `cpu_percent` / `mem_rss_bytes` are read from the OS
  (Windows `GetProcessTimes`/`GetProcessMemoryInfo`, Linux `/proc/self`). On a
  platform where a probe is unavailable the field reports `-1` (not a fake
  zero). `cpu_percent` is a delta between successive polls — poll on a fixed
  interval for a meaningful value.
- Request throughput/latency/errors and the last 32 **slow requests** are
  recorded for every request; database query throughput/latency and the last
  32 **slow queries** are recorded around model DB calls. Adjust thresholds via
  `ServerMetrics.Global().SlowRequestMs(...)` / `.SlowQueryMs(...)`.
- **Security:** `/admin/stats` leaks internal timing/paths. Guard it with
  `.Auth()`, an IP allow-list, or a separate admin bind before exposing it.

## Monitoring screens (`/admin/monitor/*`)

Four screens read the persisted history (`metrics.db`, `[metrics]` config):
**运行监控** (live SSE snapshot + today's totals + per-route leaderboard),
**历史统计** (time-bucketed windows, per-route ranking, CSV/JSON export),
**错误日志** (persisted, sanitized error events), **SQL 统计**
(`/admin/monitor/sql` — per-day per-normalized-statement aggregates from
`metrics_sql_day`: calls, slow count, avg/max/total time; rank by calls,
total time or slow count; CSV export). Slow SQL and slow requests are also
logged at the `[log].slowSqlMs` / `[log].slowMs` thresholds.

- **P95 is persisted.** `metrics_minute` carries `p95_us` per route per
  minute, computed at flush time from a fixed-bin latency histogram (the same
  bins as the live series, so live and stored P95 read the same scale).
  Percentiles are not additive: multiple flushes/workers merge with MAX, so a
  stored P95 is an upper bound of the true one — good enough to see "this
  route's tail got slower" across hours and restarts, which avg/max cannot.
- **Health probe** `GET /health` — no authentication (LB/watchdog/supervisor
  probes cannot log in). Shallow answer is process facts only (status,
  uptime_ms, requests, pid, worker). `GET /health?deep=1` also runs `SELECT 1`
  on the main pool on the request's existing lease and answers **503** when
  the database is unreachable.
- **Alert bell** — the admin top bar polls `/admin/monitor/alerts` every 30s
  (visible only to accounts granted the monitor screen) and shows a red badge
  when something is wrong. Three signals, evaluated statelessly per poll over
  a 5-minute window: error rate (`[metrics].alertErrPct` %, only once
  `[metrics].alertErrCalls` calls have accumulated — avoids 1-of-2 = 100%
  false alarms at night), worst per-minute P95
  (`[metrics].alertP95Ms`), and workers below `[worker].count`. Set any
  threshold to 0 to disable its check. There is deliberately no alert
  history: history is the error log's and the metrics screens' job; the bell
  only answers "is anything wrong right now".

## Run

Build & run from the IDE (output streams into the terminal panel), or from a
shell — **build into `build/`, run from the project root**:

```
zanc src/main.zan src/**/*.zan --auto-stdlib -o build/app.exe
build/app.exe          # cwd = project root, so ./config/app.json and ./wwwroot resolve
```

The working directory matters more than where the binary sits: the server reads
`config/app.json`, loads `views/`, serves `wwwroot/` and opens the SQLite file by
RELATIVE path, so run it from the project root (or from a deploy directory that
has those next to the executable) — never `cd build && ./app.exe`.

### Contract e2e (`tools/e2e_mvc.py`)

A self-contained Python-stdlib suite that manages the whole lifecycle itself:
it builds a sandbox under `_scratch/mvc_e2e/` (fresh DB, port 8299, memory
cache, worker 1, views/wwwroot copied from the template), boots the server,
runs ~120 HTTP/sqlite contract checks (auth, content, inline `data-quick`
editing, table designer incl. real DDL migration, monitor, wiki, jobs,
i18n, media, the full password-reset mail chain against a local SMTP catcher
on port 8725, and the public-site discoverability pack: rss/sitemap/robots,
tag archive, meta/OG, `site.url` roundtrip), stops the server via its
control port and deletes the sandbox. It never kills by image name and
touches nothing outside `_scratch/`.

```
python tools/e2e_mvc.py                 # build with zanc if no sandbox exe yet
python tools/e2e_mvc.py --build         # force rebuild
python tools/e2e_mvc.py --exe path/to/app.exe   # reuse an existing build
                                        # (its sibling *.dll are staged too)
python tools/e2e_mvc.py --keep          # keep the sandbox for inspection
```

Exit code is 0 only when every check passes. Re-run it after any controller,
view or admin.js change — it is the template's regression gate.

### Deploying

Only `.zan` code is compiled into the executable. Views, config and assets are
read at run time, so a deploy directory is four things — and `src/` is not one
of them:

```
app.exe                 + the driver DLLs the linker put beside it in build/
config/app.json
views/                  every .html, in the module structure it has in the repo
wwwroot/
```

Of the DLLs, only the driver for the database in use is needed: `libsqlite3-0.dll`
for SQLite, `libpq.dll` + `libssl-3-x64.dll` + `libcrypto-3-x64.dll` +
`libiconv-2.dll` + `libintl-8.dll` for PostgreSQL (MySQL speaks its protocol
without a client library). `data/app.db` is not copied — the directory and the
database are created on first start, and `Schema` fills in the tables and the
seed account. Set the session key — `[auth].secret` in `config/app.json`, or the
`ZAN_AUTH_SECRET` environment variable which overrides it — to 32+ characters,
or sign-in fails with a configuration error.

**Keep generated files out of the source tree.** `-o build/app.exe` exists so the
executable and the driver DLLs the linker copies beside it land in one throwaway
directory. Everything the running app produces is likewise disposable and
git-ignored, but it does land in the working directory:

| Path | What | Keep? |
|---|---|---|
| `build/` | compiler output: `app.exe` + driver DLLs | no — delete freely |
| `publish/<platform>/` | the IDE's Publish output | no |
| `data/` | SQLite database + metrics history (`database.sqlitePath`, `metrics.path`) | it *is* your data in dev |
| `uploads/` | streamed upload target | runtime state |
| `*.log`, `*.err` | whatever you redirected stdout/stderr to | no |

Point `database.sqlitePath` anywhere else (e.g. `var/app.db`) if `data/` does not
suit you: the directory is created for you, but keep the path RELATIVE — an
absolute `/data/app.db` is the root of the drive, where the file cannot be
created, and the server then runs with no database at all (schema unapplied,
every sign-in "wrong password").

## Endpoints

- `GET /` — HTML landing page from the in-memory template cache
- `GET /blog`, `GET /blog/{id}`, `POST /blog/create` — server-rendered blog (author = signed-in account)
- `GET /blog?tag=X` — tag archive (whole-tag match, not substring)
- `GET /rss.xml` — RSS 2.0 feed of the latest 20 published posts
- `GET /sitemap.xml` — home, blog index and every published post
- `GET /robots.txt` — allows the public site, denies `/admin/`, points at the sitemap
- `GET /admin/login`, `POST /admin/login`, `GET /admin/logout` — admin sign-in (seed: `admin` / `admin1234`)
- `GET /admin` — admin dashboard (uptime, requests, CPU/RSS, slow requests, pool/cache)
- `GET /admin/system/users`, `POST /admin/system/users/status`, `POST /admin/system/users/logout` — account administration
- `GET /admin/content/posts`, `POST /admin/content/posts/state` — content administration (drafts included)
- `GET /users` — ORM + cache demo (503 until a database is configured)
- `GET /user/{id}` — route parameter demo (JSON envelope)
- `GET /api/status` — request statistics
- `GET /admin/stats` — runtime diagnostics (CPU, memory, requests, slow requests/queries) — **protect before exposing**
- `POST /api/echo` — rate-limited (100 req/s) echo
- `POST /api/gather` — `[Auth] [Rank(3)] [Lock("user")]` permissioned + locked demo
- `POST /api/login` — `user=admin&pass=admin` issues a session token
- `GET /api/me` — requires `Authorization: Bearer <token>` or session cookie
- `GET /admin/menu` — admin menu built from `[Menu]` routes (`[Auth] [Rank(9)]`)
- `POST /upload` — streaming upload

## Database & ORM (FreeSQL-style, bidirectional)

`System.Data.Orm.Model` maps both directions:

- **Model -> table** (code first): `Model.Define("users").Column(...).CreateTable(db)`
  (see `src/model/User.zan`).
- **Table -> model** (database first): `Model.FromTable(db, "users")` reflects an
  existing table's schema (SQLite `PRAGMA table_info`, MySQL/MariaDB
  `SHOW COLUMNS`, otherwise the ANSI `information_schema` catalog) into a Model,
  auto-detecting the provider. `Model.FromTable(db, "users").ToDefineCode()`
  emits the `Model.Define(...)` source to scaffold a model file from a table.

```
Model users = Model.FromTable(Db.Conn(), "users");   // reflect schema
Console.WriteLine(users.ToDefineCode());               // print model source
```

## Static assets

`main.zan` mounts `wwwroot/` at `/static` before auth and routing:

```zan
StaticFiles.Mount(app, "/static", "wwwroot");   // GET /static/css/app.css
```

**`wwwroot/` is the whole public surface.** One mounted directory, nothing else:
`src/`, `config/app.json`, `data/` and every log sit outside it, so no path
trick reaches them even if a check were wrong — the files simply are not under
the served root. A file becomes downloadable by being moved into `wwwroot/`,
which makes "is this public?" a question about the directory rather than about
the path parser.

Bodies are read once per worker and then served from memory with
`Cache-Control: public, max-age=86400`; a path is rejected unless every segment
is plain `[A-Za-z0-9._-]`, so `..`, backslashes and dotfiles cannot escape the
directory. `StaticFiles.MaxAge(0)` while developing.

```
wwwroot/css/app.css          the whole design system (no framework, no build)
wwwroot/js/app.js            htmx glue: CSRF header, error/flash toasts
wwwroot/vendor/htmx.min.js   htmx 1.9.12    -- unpkg.com/htmx.org@1.9.12/dist/htmx.min.js
wwwroot/vendor/alpine.min.js Alpine 3.14.1  -- unpkg.com/alpinejs@3.14.1/dist/cdn.min.js
```

The vendor files are committed on purpose: no CDN at runtime, versions
pinned, works offline. To upgrade, download the new file over the old one and
update the version in this list. There is no Node toolchain and nothing to
compile -- `app.css` is hand-written CSS (variables, grid, a 900px breakpoint
that turns the admin sidebar into a drawer, and tables that become cards under
720px), so a page needs no build step to look right on phone or desktop.

## Admin JSON API

The server exposes clean REST/JSON endpoints for administration:
- **Endpoints** (all `Accept: application/json`, cookie session):
  - `POST /api/auth/login`, `GET /api/auth/me` -- existing auth
  - `GET /api/admin/menu` -- the sidebar as JSON, filtered by the same
    permission resolver the dispatcher uses (MenuBuilder.JsonFor), so the
    client cannot render a link the API would refuse
  - `GET /api/admin/summary` -- dashboard counters
  - `GET /api/admin/posts/list|get`, `POST .../create|update|publish|delete`
    -- article CRUD as JSON (src/Controller/Api/Posts.zan)
- **Optimistic concurrency**: post rows carry `version` (= updatedAt). The
  form sends it back on save; when it no longer matches the stored row the
  update is refused with `409 {"code":"1005"}` instead of silently
  overwriting the other editor's changes. Two people editing the same article
  cannot clobber each other.
- **Auth shape**: a JSON request (Accept: application/json) that is not
  signed in gets `401 {"code":"401"}`, never the sign-in redirect -- the
  dispatcher picks the response shape once (WebApp.WantsPage).

## Template syntax (views/*.html)

```
{{name}}                   escaped variable
{{{name}}}                 raw variable
{{#if name}}...{{/if}}     conditional
{{#each rows}}...{{/each}} loop over ViewData.AddList("rows")
layout.html + {{content}}  page wrapper
```

## Table designer (`/admin/dev/coder`)

Three one-click steps with different costs: **save design** (touches only
`sys_gen_*`), **同步数据库** (DDL — creates the table or adds missing columns),
**生成代码** (writes `.zan` sources, needs a rebuild). The designer only adds;
nothing is dropped or rewritten. Every designed table is immediately usable in
the generic data manager (`/admin/monitor/data?t=...`) before any code
generation.

**生成代码 writes 8 files** — model (`src/Model/Gen/<Entity>.zan`), admin
controller (`src/Controller/Admin/<Entity>.zan`), Dao
(`src/Dao/Gen/<Entity>Dao.zan`), admin list + form views, and a public
front-facing trio: controller (`src/Controller/Front/<Entity>Front.zan`,
anonymous-readable list + detail shaped like `Blog.Posts` — writes stay in the
admin area) plus its list and detail views. Output root follows the
`gen.root` site setting (default `..`, i.e. the sources of the running
server); all directories are created recursively. Preview
(`/admin/dev/coder/preview`) renders the eight sources in tabs before
writing anything. Generated code follows the hand-written module shape
(attribute routing, `AppController` lease, view keys
`<Module>.<Controller>.<Action>`), so it composes with layout, i18n and the
permission index unchanged.

**AI 生成字段** — on the field-design screen, describe the business in one
sentence and the configured AI provider drafts the whole column list (names,
Chinese labels, kinds, widgets, dict/relation suggestions, list/filter/edit
**Inline editing** — on the field-design table the cheap knobs are edited in
place: the list/filter/create/edit/required checkboxes toggle on click, and the
label, kind, widget and sort order are cell-level inputs/selects. Each change
POSTs to `columnquick` (allow-listed fields only, one UPDATE per change) and a
failed save reverts the control and toasts; deep attributes (size, default,
dict, relation) still use the edit dialog. Read-only accounts get disabled
controls.

**AI 生成字段** — on the field-design screen, describe the business in one
operator's real dict-type codes and designed tables → model answers strict
JSON → **every column is re-validated server-side** (name legality, kind/widget
whitelists, relation tables must actually exist, duplicate/id rejection) →
human reviews a checklist and applies only the wanted rows → the browser
round-trip is re-validated by the same sanitizer before insert. AI output is a
draft, never trusted, never auto-saved. Requires `[站点设置 → AI]` enabled;
the endpoint must be one of the allow-listed providers (or local Ollama).

## Wiki knowledge base (`/admin/wiki`)

Department-scoped knowledge bases with Markdown documents, AI curation and
full revision history.

- **Spaces** (`wiki_space`) — one knowledge base per department (bound to
  `sys_department`, or generic with `departmentId = 0`); a space with docs
  refuses deletion.
- **Docs** (`wiki_doc`) — Markdown source stored verbatim, rendered to HTML
  at read time by a small server-side renderer (headings / lists / quotes /
  code fences / **bold** / `code`; everything is HTML-escaped before
  markup is applied, so stored source can never inject HTML). Reading bumps
  the view counter.
- **Revisions** (`wiki_revision`) — every save (human edit, AI organize,
  rollback) snapshots the whole doc and bumps `rev`. History page lists
  snapshots with the operator's note; any revision can be viewed read-only
  or rolled back — rollback itself creates a new revision, history is never
  rewritten.
- **AI 整理** — sends the current body through `Ai.CompleteRaw` with a
  curation system prompt and stores the result as a new revision
  (`note = AI 整理`); the same allow-list / ready-check rules as the table
  designer apply, and people can keep editing or roll the AI version back.

Markdown source must be read with `InRaw` — the default `In`/`InText`
accessors run `Filter.Clean`, which strips CR/LF (header-injection defense)
and would silently flatten multiline text into one line.

## Media library (`/admin/media`)

Streamed file uploads with a deny-by-default extension whitelist.

- **Upload** — the action is marked `[Custom(Upload = true)]`: the whole
  request body streams to disk in 64 KB chunks (memory stays flat regardless
  of size) into a server-named spool file under `uploads/`, so the client's
  file name never touches a path. The real name travels as `?name=`, is
  reduced through `Validator.SafeFileName`, its extension is checked against
  a whitelist (`png/jpg/pdf/docx/zip/mp4/...`; **svg is deliberately
  excluded** — an SVG served same-origin can carry script), and only then is
  the file moved into `wwwroot/media/` and recorded in the `media` table.
  Duplicate names get a timestamp prefix instead of overwriting; rejected
  uploads delete their spool file.
- **Serving** — `StaticFiles.Mount` has a single slot (a second call
  replaces the prefix), so media lives **inside** the static root:
  `wwwroot/media/<file>`, publicly served as `/static/media/<file>`. The
  `uploads/` spool directory is never mounted.
- **Cache note** — `StaticFiles` caches asset bodies per worker after the
  first hit, so a deleted file may still be served from worker memory until
  the next restart. Fine for an internal library; call it out if you build
  user-facing deletion on top.
- **Size cap** — `[server].uploadBodyMB` (default 64) feeds
  `app.MaxUpload()`; over-cap requests are refused with 413 before the body
  is read.
- **Delete** — removes the row and the disk file; the path is rebuilt from
  the stored server-generated file name, never trusted as a full path.
- The admin page's 上传文件 button posts the raw bytes with
  `?name=` (`data-upload` in `admin.js`) — no multipart machinery needed.

## Public site discoverability (RSS / sitemap / robots / tags)

Three machine-facing surfaces served by `Blog/Posts.zan` next to the human
pages, all built from published posts only:

- **`GET /rss.xml`** — RSS 2.0, latest 20 published posts (`application/rss+xml`).
  Item links and GUIDs are absolute; `<pubDate>`/`<lastBuildDate>` are RFC-822
  in the site timezone (`site.timezone`, the same offset the page views use).
  Every text is XML-escaped and control characters are folded to spaces.
- **`GET /sitemap.xml`** — the sitemap-protocol URL set: `/`, `/blog`, and one
  `<url>` per published post with `<lastmod>` from the row's update date.
- **`GET /robots.txt`** — `User-agent: *` / `Allow: /` / `Disallow: /admin/` /
  `Sitemap: <base>/sitemap.xml`. No database involved, so it answers even when
  the DB is down.
- **Absolute base** — `site.url` (`站点地址` in 站点设置) is the source of truth
  when configured (the only reliable choice behind a reverse proxy or custom
  domain); left empty the base falls back to the request's `Host` header,
  restricted to URL-safe characters. Saving the setting takes effect on the
  next request (the settings cache is dropped per worker).
- **Tag archive** — post tags are plain comma text, so a tag link is a
  whole-word match over the split list (`Prose.HasTag`), never a SQL `LIKE`:
  a search for `ar` must not surface the `arc` post. `/blog?tag=X` filters,
  paginates and feeds the pager; tag chips on a post page link into the
  archive.
- **Head metadata** — the public layout renders an `<link rel="alternate">`
  for the feed on every page; a post page overrides `<meta name="description">`
  with its summary (body excerpt as fallback) and emits Open Graph tags
  (`og:title` / `og:description` / `og:url` / `og:image` when a cover exists).

## Multi-language UI (`site.language` / `i18n`)

The admin shell is translatable through a lightweight, Chinese-as-key layer
(`src/Feature/Lang.zan`):

- The UI language is the `site.language` site setting (`zh-CN` default,
  `en-US` shipped). `zh-CN` renders the source strings directly — zero
  overhead, zero behavior change.
- Any other registered language loads a flat `{chinese: translated}` JSON
  pack from `wwwroot/i18n/{lang}.json` at first render; a missing entry
  falls back to the Chinese source, so a partially filled pack can never
  break the screen. Packs ship with `wwwroot` (so `--publish` carries them)
  and adding a language = one JSON file + one entry in `Lang.Known`.
- Covered today: sidebar groups, menu titles, screen headings/titles, and
  the common inline action labels. Views reference the common labels as
  `{{iEdit}}` / `{{iSave}}` / `{{iOps}}` … injected into every page and
  dialog by `AdminController.I18n`. Server messages (toasts, errors) and
  front-site pages stay Chinese in v1 — translate by wrapping the literal
  with `Lang.T(...)` or adding a key as needed.
- Switching the setting takes effect on the next request (settings cache is
  forgotten on save); unknown values fall back to `zh-CN`.

## Inline cell editing (generic, `data-quick`)

Any admin list can opt its table into in-place editing without page-specific
scripts: put `data-quick="<POST url>"` on the `<table>` and mark each editable
control with `data-quick-field="<field>"` and `data-id`. Controls are plain
checkboxes / text / number inputs / selects (select initial value via
`data-value`). `admin.js` (`applyFragmentWidgets`, so panels, fragments and
dialogs all pick it up) delegates `change` to one POST `{id, field, value}` per
edit; a failed save reverts the control and toasts, so the UI never shows a
state the server refused. The `.cell-edit` styles keep the table looking like a
table until a cell is hovered/focused.

What is editable is a **per-resource server allowlist** — each controller
implements a `Quick` action whose branches whitelist exactly the fields it
accepts and update exactly those typed columns (the column name never comes
from the request). Reference implementations: `Coder.ColumnQuick`
(designer columns), `Categories.Quick`, `Dicts.ItemQuick`. Keep out of the
allowlist: id, passwords/tokens, created/updated timestamps, computed columns
(counts), and identity codes that other data references (category `slug`, dict
item `value`) — those stay dialog-edited so the operator sees the warning.
Read-only accounts render plain text instead of controls (`{{#if canUpdate}}`).

## Table component (wwwroot/js/zan-table.js)


`zan-table.js` progressively enhances the admin tables: the server template
keeps emitting a plain `<table class="table">` with static rows, and the
component wires itself onto any table that carries a `data-table` attribute
(wired by `applyFragmentWidgets`, so fragments, dialogs and the first
server-rendered panel all pick it up). No DOM re-rendering: the rows stay the
same nodes, so `data-post`/`data-dialog` delegation and inline `<a>` keep
working untouched.

```html
<table class="table" data-table="sort filter select">   <!-- empty = all -->
  <thead><tr>
    <th>标题</th>                    <!-- sortable, type auto-detected -->
    <th class="num">阅读</th>         <!-- numeric sort -->
    <th class="ops" data-nosort>操作</th>  <!-- ops columns are excluded anyway -->
  </tr></thead>
```

What it adds, all client-side on the current page's data (zero requests):

- **Column sort** — click a header; numeric/date/text kind is detected from
  the first non-empty cell, arrows show the direction.
- **Instant filter** — a 在当前页内筛选 box in the status bar; row
  visibility toggles as you type, with a hit counter.
- **Row selection** — a checkbox column with select-all/indeterminate head;
  selected count, row highlight (`.on`), and a 取消选择 button.
- **Density toggle** — 紧凑/舒适 per table, remembered in localStorage.
- **Column show/hide** — a 列 menu in the status bar toggles each column;
  hidden columns still sort and filter normally. Persisted per table identity
  (`data-table-key` attribute, or `pathname#tableIndex`) in localStorage; the
  last visible column cannot be hidden.
- **Sticky header** — the header row pins to the top of the scroll port while
  the table scrolls (CSS only; an inset shadow replaces the collapsed border
  that would otherwise scroll away).

Cross-page sort/filter belongs to the server (the pager is a link, the query
is a form) — the component deliberately only manages the current page and
resets on panel reload. Column visibility survives reloads; sort/filter state
does not.
