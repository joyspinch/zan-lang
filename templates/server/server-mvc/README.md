# {{NAME}} — ZanWeb Web MVC Framework

Enterprise web application skeleton modeled on a production swoole (ZxPHP)
framework, rebuilt on Zan's coroutine runtime — now with a layered
controller/model/framework structure, an external config file, and the ORM and
cache wired in by default.

## Layout

```
config/app.json         runtime config (host/port/limits/db/cache) — NOT compiled in
src/main.zan            bootstrap only — routes come from Routes.gen.zan
src/controller/         request handlers, grouped per resource
  Controller.zan          shared response helpers (Ok / Fail envelopes)
  HomeController.zan       HTML landing page
  ApiController.zan        status / echo / login / me / gather / upload
  UserController.zan       ORM + cache demo (/users, /user/{id})
  AdminController.zan      /admin/menu — menu built from [Menu] routes
src/model/              data access on System.Data.Orm
  User.zan                example model (auto-creates + seeds its table)
src/framework/          reusable plumbing
  Config.zan              loads config/app.json (System.Json)
  App.zan                 global accessor for the running WebApp
  Db.zan                  DbConnection bootstrap from [database]
  Cache.zan               in-memory TTL cache (Redis optional)
  Security.zan            request-lock manager + rank/RBAC resolver
  Filter.zan              unified input sanitising (Clean / Html / ToInt)
  Menu.zan                admin menu JSON built from the route table
  Routes.gen.zan          GENERATED route table (do not edit by hand)
  Router / Hooks / View / WebApp / HttpContext / Validate / StrMap
tools/routegen/         RouteGen.zan — scans attributes -> Routes.gen.zan
views/                  in-memory HTML templates
```

## Attribute-driven routes

Controllers declare routing with **attributes** instead of hand-wiring in
`main.zan` (the Zan equivalent of the PHP `@title/@auth/@rank` docblocks). A
build-time generator (`tools/routegen`) scans the controllers and emits
`src/framework/Routes.gen.zan`, which `main.zan` registers with a single
`Routes.Map(app)`.

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

**Routes follow the directory structure.** The path is
`<folders under src/controller> + controller name (minus "Controller") + action`,
lower-cased — `ApiController.Status` → `/api/status`, `Admin/UserController.List`
→ `/admin/user/list`. `Index` maps to the folder root. `[Route("...")]` overrides
the convention when you need an exception.

Regenerate after editing controller attributes:

```
zanc tools/routegen/RouteGen.zan --auto-stdlib -o routegen.exe
./routegen.exe        # run from the project root; rewrites Routes.gen.zan
```

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
| External config | `config/app.json`, `framework/Config.zan` | runtime-loaded, not compiled in |
| High-performance routing | `framework/Router.zan` | static-first match + `{param}`, 404/405 |
| Rate limiting | `framework/Hooks.zan` | global + per-route fixed windows, 429 |
| Auth / sessions | `WebApp.AuthUser`, `Sessions` | Bearer token or session cookie, `.Auth()` guard |
| Lifecycle hooks | `Hooks` | typed delegates, run in order |
| In-memory views | `framework/View.zan` | templates loaded ONCE at startup |
| Streaming uploads | `.Upload()` routes | body streamed to disk in 64KB chunks |
| Validation | `framework/Validate.zan` | Require/MaxLen/IsInt/OneOf + SafeFileName |

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
`Server.Run(app)` (see `src/framework/Server.zan`), which is now a thin wrapper
over the standard library: PID/exe discovery, daemonization (`setsid`) and
`SO_REUSEPORT` worker spawning all live in `System.Diagnostics.ProcessHost`, so
the same capability is available to any server, not just this template.

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

Build & run from the IDE (output streams into the terminal panel), or:

```
zanc src/main.zan src/**/*.zan --stdlib-path <stdlib> -o app.exe
./app.exe        # reads ./config/app.json, http://127.0.0.1:8080
```

## Endpoints

- `GET /` — HTML page from the in-memory template cache
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

## Template syntax (views/*.html)

```
{{name}}                   escaped variable
{{{name}}}                 raw variable
{{#if name}}...{{/if}}     conditional
{{#each rows}}...{{/each}} loop over ViewData.AddList("rows")
layout.html + {{content}}  page wrapper
```
