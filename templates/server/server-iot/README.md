# {{NAME}} — EMQX-style MQTT IoT Server

An MQTT 3.1.1 message broker **plus** an attribute-driven web management console,
in one Zan binary. Devices speak MQTT on `:1883`; operators use the HTTP console
and JSON API on `:8080`. The web half comes from the **Zan.Mvc package**:
attribute routing is generated at compile time (`__AttrRoutes`), auth/sessions
use the package services (`AuthToken`/`AuthUser`/`Cfg`), and the console
account is the package Schema's bootstrap admin — the same stack the other
server templates consume. The broker itself is `System.Net.Mqtt.MqttBroker`.
The template holds config, the `[mqtt]` config shim, controllers and views.

## Features

- **MQTT 3.1.1 broker** (`System.Net.Mqtt`): CONNECT/CONNACK, PUBLISH (QoS 0 & 1),
  SUBSCRIBE/SUBACK, UNSUBSCRIBE/UNSUBACK, PINGREQ/PINGRESP, DISCONNECT.
- **Topic routing** with `+` (single-level) and `#` (multi-level) wildcards.
- **Client / device management**: live session registry (client id, address,
  keepalive, per-client in/out counters), force-disconnect (kick).
- **Subscription & topic management**: enumerate every subscription and every
  topic seen, with last payload + message counts.
- **Publish API**: inject a message onto any topic from the console (fans out to
  all matching subscribers).
- **Performance monitoring** (`/admin/stats`, `/iot/metrics`): uptime, connected
  vs. cumulative clients, subscription/topic counts, message + byte throughput,
  and the full HTTP runtime metrics snapshot.
- **Attribute-driven routes** (`[HttpGet]`/`[HttpPost]`/`[Route]`/
  `[Custom(Authorization = ...)]`/`[Lock]`/`[Description]`) generated into
  `__AttrRoutes` at compile time — same convention as the Zan.Mvc package.

## Layout

```
config/app.json            server/mqtt/auth/database settings (edit, no recompile)
views/Home/HomeController.Index.html   dashboard (polls the /iot/ JSON APIs)
src/main.zan               package wiring + Worker("mqtt") + Worker("http"), Worker.RunAll
src/controller/            HTTP actions (Iot / Auth / Home), package ApiController base
src/Framework/MqttCfg.zan  template-only [mqtt] config section over package Cfg
```

## Build & run

```
# 1. compile everything
zanc (all src/**/*.zan) --auto-stdlib -o app.exe

# 2. run — MQTT on :1883, console on http://127.0.0.1:8080
app.exe
```

## HTTP API

| Method & path                 | Auth      | Purpose                          |
|-------------------------------|-----------|----------------------------------|
| `GET  /`                      | —         | dashboard                        |
| `GET  /health`                | —         | liveness probe (`?deep=1` adds a database roundtrip; the broker itself does not depend on the database) |
| `GET  /iot/clients`           | —         | connected clients                |
| `GET  /iot/clients/{id}`      | —         | one client + its subscriptions   |
| `POST /iot/clients/{id}/kick` | login     | force-disconnect (global lock)   |
| `GET  /iot/subscriptions`     | —         | all subscriptions                |
| `GET  /iot/topics`            | —         | all topics + last payload        |
| `GET  /iot/metrics`           | —         | broker performance snapshot      |
| `POST /iot/publish`           | login     | inject a message (`topic`,`payload`) |
| `GET  /admin/stats`           | login     | broker + HTTP metrics            |
| `POST /auth/login`            | —         | `admin` / `[auth].bootstrapPassword` (default `admin-bootstrap-2026`) → session cookie + bearer token |

Read frontend params through `this.In(...)` / `this.InRaw(...)` on the package
controller base; JSON API replies use the package envelope `{"code":0,"msg","data","traceId"}`.

## Try it

```
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'sensors/#'
mosquitto_pub -h 127.0.0.1 -p 1883 -t sensors/room1/temp -m 22.5
```

Then open the console and watch clients, subscriptions, topics and throughput
update live. An end-to-end raw-MQTT + HTTP smoke test lives in `test_iot.py`.

## Scope

This is a compact broker aimed at management/console use cases. It implements the
MQTT 3.1.1 control packets above at QoS 0/1 (PUBACK is sent; delivery to
subscribers is QoS 0 fan-out). Retained messages, persistent sessions, QoS 2,
TLS, and `$SYS` topics are intentionally left as extension points.
