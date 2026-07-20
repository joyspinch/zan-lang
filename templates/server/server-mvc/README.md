# {{NAME}} — ZanWeb Web MVC Framework

Enterprise web application skeleton modeled on a production swoole (ZxPHP)
framework, rebuilt on Zan's coroutine runtime — now with a layered
controller/model/framework structure, an external config file, and the ORM and
cache wired in by default.

## Layout

```
config/app.json         runtime config (host/port/limits/db/cache) — NOT compiled in
src/main.zan            bootstrap + route wiring only
src/controller/         request handlers, grouped per resource
  Controller.zan          shared response helpers (Ok / Fail envelopes)
  HomeController.zan       HTML landing page
  ApiController.zan        status / echo / login / me / upload
  UserController.zan       ORM + cache demo (/users, /user/{id})
src/model/              data access on System.Data.Orm
  User.zan                example model (auto-creates + seeds its table)
src/framework/          reusable plumbing
  Config.zan              loads config/app.json (System.Json)
  App.zan                 global accessor for the running WebApp
  Db.zan                  DbConnection bootstrap from [database]
  Cache.zan               in-memory TTL cache (Redis optional)
  Router / Hooks / View / WebApp / HttpContext / Validate / StrMap
views/                  in-memory HTML templates
```

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
`Server.Run(app)` (see `src/framework/Server.zan`).

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
- `POST /api/echo` — rate-limited (100 req/s) echo
- `POST /api/login` — `user=admin&pass=admin` issues a session token
- `GET /api/me` — requires `Authorization: Bearer <token>` or session cookie
- `POST /upload` — streaming upload

## Template syntax (views/*.html)

```
{{name}}                   escaped variable
{{{name}}}                 raw variable
{{#if name}}...{{/if}}     conditional
{{#each rows}}...{{/each}} loop over ViewData.AddList("rows")
layout.html + {{content}}  page wrapper
```
