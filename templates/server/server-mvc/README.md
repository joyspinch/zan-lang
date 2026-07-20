# {{NAME}} — ZanWeb Web MVC Framework

Enterprise web application skeleton modeled on a production swoole (ZxPHP)
framework, rebuilt on Zan's coroutine runtime.

## Features

| Capability | Where | Notes |
|---|---|---|
| High-performance routing | `src/framework/Router.zan` | static-first match + `{param}` segments, 404/405 |
| Rate limiting | `src/framework/Hooks.zan` | global + per-route fixed windows, 429 + Retry-After |
| Auth / sessions | `WebApp.AuthUser`, `Sessions` | Bearer token or session cookie, `.Auth()` route guard |
| Lifecycle hooks | `Hooks` | typed delegates, registered at startup, run in order |
| In-memory views | `src/framework/View.zan` | templates loaded ONCE at startup; zero per-request IO |
| Request stats | `RouteStats` | count/avg/min/max per route (`GET /api/status`) |
| Streaming uploads | `.Upload()` routes | body streamed to disk in 64KB chunks, flat memory, binary-safe |
| Validation | `src/framework/Validate.zan` | Require/MaxLen/IsInt/OneOf + SafeFileName/ExtAllowed |
| Memory safety | `WebApp.HandleConnection` | 2MB body cap (413), 64KB header cap (431) |

## Run

Build & run from the IDE, or:

```
zanc src/main.zan src/framework/*.zan -o app.exe
./app.exe        # http://127.0.0.1:8080
```

## Endpoints

- `GET /` — HTML page rendered from the in-memory template cache
- `GET /user/{id}` — route parameter demo (JSON envelope)
- `GET /api/status` — request statistics
- `POST /api/echo` — rate-limited (100 req/s) echo
- `POST /api/login` — `user=admin&pass=admin` issues a session token
- `GET /api/me` — requires `Authorization: Bearer <token>` or session cookie
- `POST /upload` — streaming upload (tested with 100MB binary: byte-exact, ~4MB flat RSS)

## Template syntax (views/*.html)

```
{{name}}                   escaped variable
{{{name}}}                 raw variable
{{#if name}}...{{/if}}     conditional
{{#each rows}}...{{/each}} loop over ViewData.AddList("rows")
layout.html + {{content}}  page wrapper
```

Measured on the reference machine: ~50,000 req/s at 200 concurrent
connections, flat ~5MB working set (no leaks across 150k requests).
