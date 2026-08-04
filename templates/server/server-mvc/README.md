# {{NAME}} — ZanWeb Web MVC Framework

Enterprise web application skeleton modeled on a production swoole (ZxPHP)
framework, rebuilt on Zan's coroutine runtime — a layered controller/model
structure, an external config file, and the ORM and cache wired in by default.

The framework itself is **not** part of the template: routing, request context,
hooks, views, filters, validation, sessions, rate limiting, request locks and
the server bootstrap live in the standard library under `System.Web`
(`WebApp`, `Router`, `HttpContext`, `Controller`, `View`, `WebServer`, …) next
to `RouteTable` / `RouteStats` / `PermTable`. The template keeps only what is
its own: config, DB, cache, controllers, models and views.

## Layout

```
config/app.json         runtime config (host/port/limits/db/cache) — NOT compiled in
src/main.zan            bootstrap only — routes come from controller attributes
src/Controller/         request handlers, one directory per module
  layout.html             the site-wide page wrapper (global {{content}} layout)
  Index/Index.zan         HTML landing page
  Blog/Posts.zan          list / detail / publish (ORM + cache + views)
  Account/Login.zan       GET/POST /admin/login, /admin/logout (own bare layout)
  Admin/Dashboard.zan     GET /admin — metrics dashboard
  Admin/Users.zan         GET /admin/users + enable/disable, force logout
  Admin/Posts.zan         GET /admin/posts + publish/unpublish
  Api/Auth.zan            POST /api/auth/login, GET /api/auth/me
  User/Users.zan          /users, /user/{id}
  <Module>/View/*.html    that module's views; a module's own layout.html
                          overrides the global one for that module only
src/Dao/<Module>/       every query and write for that module
src/Model/<Module>/     entities only: table structure, no queries
src/Framework/          application wiring that belongs to this app
  AppController.zan       request-scoped connection + transactions
  AdminController.zan     admin base: anonymous -> redirect to /admin/login
  Cfg.zan                 loads config/app.json into the typed Cfg entity
  Db.zan                  builds the DB pool + Redis from [database]/[cache]
  DbContext.zan           per-request connection lease
  Schema.zan              CodeFirst DDL + seed, once at startup
  Auth.zan                token issue/verify against sys_user
wwwroot/                the ONLY web-reachable directory, served at /static
```

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
string name = ctx.In("name");       // trimmed, CR/LF/TAB control chars stripped
string html = ctx.InText("bio");    // In + HTML-entity escaped (safe to render)
int    page = ctx.InInt("page", 1); // tolerant integer parse with a fallback
string raw  = ctx.InRaw("blob");    // UNFILTERED — only when you need raw bytes
bool   has  = ctx.HasIn("name");
```

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
  "database": { "driver": "sqlite", "sqlitePath": "app.db",
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
`WebServer.Run(app, count, daemon)` from the standard library; swapping it for
`WebServer.RunCommand(app, count)` turns the binary into a service that answers
`start`, `start -d`, `stop`, `restart`, `reload` (rolling worker replacement)
and `status` on the command line. Listener handoff, respawn-on-crash,
daemonization and the control port live in `System.Net.Worker` /
`System.Diagnostics.ProcessHost`, so any server gets them, not just this
template.

## Observability (`GET /admin/stats`)

The request lifecycle and database queries are instrumented into the shared
`System.Diagnostics.ServerMetrics` singleton, exposed as JSON at
`/admin/stats`:

```json
{
  "uptime_ms": 2015, "pid": 31084, "worker_id": 0,
  "cpu_ms": 31, "cpu_percent": 1, "mem_rss_bytes": 6852608,
  "requests": {"count":4,"errors":2,"avg_ms":3,"max_ms":15,
               "slow_threshold_ms":500,"slow_count":0},
  "queries":  {"count":2,"avg_ms":6,"max_ms":8,
               "slow_threshold_ms":200,"slow_count":0},
  "slow_requests": [{"req":"GET /slow -> 200","ms":750}],
  "slow_queries":  [{"sql":"SELECT * FROM huge_join","ms":320}]
}
```

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

## Run

Build & run from the IDE (output streams into the terminal panel), or from a
shell — **build into `build/`, run from the project root**:

```
zanc src/main.zan src/**/*.zan --auto-stdlib -o build/app.exe
build/app.exe          # cwd = project root, so ./config/app.json and ./wwwroot resolve
```

The working directory matters more than where the binary sits: the server reads
`config/app.json`, serves `wwwroot/` and opens the SQLite file by RELATIVE path, so
run it from the project root (or from a deploy directory that has those three
next to the executable) — never `cd build && ./app.exe`.

**Keep generated files out of the source tree.** `-o build/app.exe` exists so the
executable and the driver DLLs the linker copies beside it land in one throwaway
directory. Everything the running app produces is likewise disposable and
git-ignored, but it does land in the working directory:

| Path | What | Keep? |
|---|---|---|
| `build/` | compiler output: `app.exe` + driver DLLs | no — delete freely |
| `publish/<platform>/` | the IDE's Publish output | no |
| `app.db` | SQLite database (`database.sqlitePath`) | it *is* your data in dev |
| `uploads/` | streamed upload target | runtime state |
| `*.log`, `*.err` | whatever you redirected stdout/stderr to | no |

Point `database.sqlitePath` at a directory of your own (e.g. `var/app.db`, created
beforehand) if you would rather not have the database file in the project root.

## Endpoints

- `GET /` — HTML landing page from the in-memory template cache
- `GET /blog`, `GET /blog/{id}`, `POST /blog/create` — server-rendered blog (author = signed-in account)
- `GET /admin/login`, `POST /admin/login`, `GET /admin/logout` — admin sign-in (seed: `admin` / `admin1234`)
- `GET /admin` — admin dashboard (uptime, requests, CPU/RSS, slow requests, pool/cache)
- `GET /admin/users`, `POST /admin/users/status`, `POST /admin/users/logout` — account administration
- `GET /admin/posts`, `POST /admin/posts/state` — content administration (drafts included)
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
`src/`, `config/app.json`, `app.db` and every log sit outside it, so no path
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

The two vendor files are committed on purpose: no CDN at runtime, versions
pinned, works offline. To upgrade, download the new file over the old one and
update the version in this list. There is no Node toolchain and nothing to
compile -- `app.css` is hand-written CSS (variables, grid, a 900px breakpoint
that turns the admin sidebar into a drawer, and tables that become cards under
720px), so a page needs no build step to look right on phone or desktop.

## Template syntax (views/*.html)

```
{{name}}                   escaped variable
{{{name}}}                 raw variable
{{#if name}}...{{/if}}     conditional
{{#each rows}}...{{/each}} loop over ViewData.AddList("rows")
layout.html + {{content}}  page wrapper
```
