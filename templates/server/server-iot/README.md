# {{NAME}} — EMQX-style MQTT IoT Server

An MQTT 3.1.1 message broker **plus** an attribute-driven web management console,
in one Zan binary. Devices speak MQTT on `:1883`; operators use the HTTP console
and JSON API on `:8080`. Built on the same `ZanWeb` framework as `server-mvc`
(convention routing, unified input filtering, auth/rank/lock, in-memory views).

## Features

- **MQTT 3.1.1 broker** (`src/broker/`): CONNECT/CONNACK, PUBLISH (QoS 0 & 1),
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
- **Attribute-driven routes** (`[Get]`/`[Post]`/`[Route]`/`[Auth]`/`[Rank]`/
  `[Lock]`/`[Menu]`/`[Title]`) generated into `src/framework/Routes.gen.zan`.
  These can also be collapsed into one combined attribute, e.g.
  `[Api(post, route="/iot/clients/{id}/kick", title="踢下线", auth, rank=9, lock="global")]`
  (bare flags + `key=value`; the single attributes still work and can be mixed).

## Layout

```
config/app.json            server + mqtt ports (edit, no recompile)
views/index.html           dashboard (polls the /iot/* APIs)
src/main.zan               boots the broker (Task.Spawn) + HTTP console
src/broker/Mqtt.zan        packet codec, framing reader, topic matcher
src/broker/Client.zan      connected session + subscriptions
src/broker/Broker.zan      registries, routing, metrics, mgmt snapshots
src/controller/            HTTP actions (Iot / Auth / Home)
src/framework/             ZanWeb framework (shared with server-mvc)
tools/routegen/            attribute scanner -> Routes.gen.zan
```

## Build & run

```
# 1. (re)generate the route table from controller attributes
zanc tools/routegen/RouteGen.zan --auto-stdlib -o routegen.exe
routegen.exe                      # run from the project root

# 2. compile everything
zanc (all src/**/*.zan) --auto-stdlib -o app.exe

# 3. run — MQTT on :1883, console on http://127.0.0.1:8080
app.exe
```

## HTTP API

| Method & path                 | Auth      | Purpose                          |
|-------------------------------|-----------|----------------------------------|
| `GET  /`                      | —         | dashboard                        |
| `GET  /iot/clients`           | —         | connected clients                |
| `GET  /iot/clients/{id}`      | —         | one client + its subscriptions   |
| `POST /iot/clients/{id}/kick` | rank ≥ 9  | force-disconnect (global lock)   |
| `GET  /iot/subscriptions`     | —         | all subscriptions                |
| `GET  /iot/topics`            | —         | all topics + last payload        |
| `GET  /iot/metrics`           | —         | broker performance snapshot      |
| `POST /iot/publish`           | rank ≥ 3  | inject a message (`topic`,`payload`) |
| `GET  /admin/stats`           | rank ≥ 9  | broker + HTTP metrics            |
| `POST /auth/login`            | —         | demo admin/admin → bearer token  |

Read frontend params through `ctx.In / ctx.InInt / ctx.InText` (central filter);
`ctx.InRaw` when you deliberately need the unfiltered value (e.g. publish payload).

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
