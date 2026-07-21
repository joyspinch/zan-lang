# {{NAME}} — Gateway-Worker WebSocket Service

A horizontally-scalable WebSocket service skeleton in the Gateway/Worker style.

## Layout

```
config/app.json           host / port (+ scale bus settings) — runtime, not compiled in
src/main.zan              reads config, starts the gateway
src/gateway/
  Gateway.zan               handshake + connection registry + Broadcast/Push
  Connection.zan            per-client state
src/worker/
  ChatWorker.zan            business logic (OnOpen / OnMessage / OnClose)
src/framework/Config.zan  loads config/app.json
client.html               open in a browser to test (chat room)
```

## Why gateway + worker

The **Gateway** only moves bytes: it accepts sockets, does the RFC 6455
handshake, tracks every connection, and exposes `Broadcast()` / `Push()`. The
**Worker** (`ChatWorker`) holds the business logic and never touches sockets —
it reacts to `OnOpen` / `OnMessage` / `OnClose` and calls the gateway's fan-out
API. This separation is what lets you evolve either side independently.

## Run & test

Run from the IDE (output streams into the terminal panel), then open
`client.html` in a browser (or several tabs) and chat — every tab sees each
other's messages via `Gateway.Broadcast`.

```
zanc src/main.zan src/**/*.zan --stdlib-path <stdlib> -o app.exe
./app.exe        # reads ./config/app.json, ws://127.0.0.1:8090
```

## Scaling across processes / machines

A single gateway already serves thousands of concurrent sockets on one core via
the coroutine event loop. For multiple gateway processes (or machines), the
clients of gateway A are invisible to gateway B, so a cross-gateway broadcast
needs a shared bus — the **Register** role in the Gateway/Worker/Register model:

1. each gateway subscribes to a Redis channel (`[scale].channel`);
2. `Broadcast` publishes the message to Redis instead of (or in addition to)
   its local clients;
3. every gateway receives the published message and fans it out to its LOCAL
   connections.

`System.Data.Redis` provides the async pub/sub client for that bridge. It is the
documented extension point and is intentionally left out of this single-process
skeleton so the template compiles and runs with zero external dependencies.
