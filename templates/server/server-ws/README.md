# {{NAME}} — Gateway-Worker WebSocket Service

A horizontally-scalable WebSocket service skeleton in the Gateway/Worker style,
hosted on the standard-library `Worker` (workerman-style): the runtime owns the
listener, the RFC 6455 handshake, frame codec, ping/pong and fragmentation —
the template only supplies callbacks.

## Layout

```
config/app.json           host / port (+ worker count) — runtime, not compiled in
src/main.zan              reads config, registers a Worker("websocket"), assigns callbacks
src/gateway/
  Gateway.zan             connection registry + Broadcast/Push over Worker.Connection
  ClientConn.zan          per-client wrapper state
src/worker/
  ChatWorker.zan          business logic (OnOpen / OnMessage / OnClose)
src/framework/Config.zan  loads config/app.json
wwwroot/client.html       open in a browser to test (chat room)
```

## Why gateway + worker

The **Worker** (stdlib) moves the bytes: accept, handshake, frame encode/decode,
close handling. The template's **Gateway** tracks the live `Worker.Connection`
list and exposes `Broadcast()` / `Push()`. The **ChatWorker** holds business
logic and never touches sockets — it reacts to `OnOpen` / `OnMessage` /
`OnClose` and returns the text to fan out. This separation is what lets you
evolve either side independently.

## Run & test

Run from the IDE (output streams into the terminal panel), then open
`wwwroot/client.html` in a browser (or several tabs) and chat — every tab sees
each other's messages via `Gateway.Broadcast`.

```
zanc src/main.zan src/**/*.zan --stdlib-path <stdlib> -o app.exe
./app.exe        # reads ./config/app.json, ws://127.0.0.1:8090
```

## Scaling across processes

Set `[worker].count` in `config/app.json` to N: the same binary becomes a
supervising master plus N worker processes (crash respawn, kernel-level accept
spread — `SO_REUSEPORT` on Linux/macOS, duplicate-socket handoff on Windows).
No code changes.

Clients of process A are invisible to process B, so a cross-gateway broadcast
on multiple machines still needs a shared bus — the **Register** role in the
Gateway/Worker/Register model:

1. each gateway subscribes to a Redis channel;
2. `Broadcast` publishes the message to Redis instead of (or in addition to)
   its local clients;
3. every gateway receives the published message and fans it out to its LOCAL
   connections.

`System.Data.Redis` provides the async pub/sub client for that bridge. It is
the documented extension point and is intentionally left out of this skeleton
so the template compiles and runs with zero external dependencies.
