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

- **MQTT 3.1.1 broker** (`System.Net.Mqtt`): CONNECT/CONNACK, PUBLISH (QoS 0/1/2),
  SUBSCRIBE/SUBACK (支持 QoS 0/1/2), UNSUBSCRIBE/UNSUBACK, PINGREQ/PINGRESP, DISCONNECT.
- **Topic routing** with `+` (single-level) and `#` (multi-level) wildcards.
- **Retained messages (保留消息)**: 自动保存/替换最新主题状态，磁盘 WAL 恢复，后台可视化列表与一键清除。
- **Client / device management**: live session registry (client id, address,
  keepalive, per-client in/out counters), force-disconnect (kick).
- **Rule & Webhook forwarding (异步数据流转)**: 非阻塞异步 HTTP POST 转发、主题规则过滤 (`#` / `+`)、签名校验与最近 20 条投递审计日志。
- **Zan.Mvc Admin 后台系统**: 深度继承 `AdminController` 体系，支持侧边栏菜单、多标签页 (Tabs)、RBAC 权限守卫与深色科技风格看板。
- **Publish API & 在线测试台**: 支持从控制台指定 QoS (0/1/2) 和 Retain 标记注入消息，实时事件日志流。
- **Performance monitoring** (`/admin/stats`, `/iot/metrics`, `/iot/webhook/stats`): 吞吐量、连接数、订阅数、Webhook 成功率等。

## Layout

```
config/app.json                         server/mqtt/webhook/auth/database settings
views/Admin/IotAdminController.*.html   Zan.Mvc 企业级后台视图 (Index, Clients, Retained, Webhooks, Console)
src/main.zan                            Worker("mqtt") + Worker("http"), MqttWebhook 注入
src/Framework/MqttCfg.zan               配置加载器 (包含 [mqtt] 与 [webhook] 节)
src/Framework/MqttWebhook.zan           非阻塞异步 Webhook 数据转发与审计引擎
src/controller/Admin/IotAdminController.zan  Zan.Mvc 后台管理控制器 (带侧边栏与权限)
src/controller/IotController.zan        REST API (clients, kick, retained, webhook, publish)
```

## Build & run

```
# 1. 编译全部源码
zanc (all src/**/*.zan) --auto-stdlib -o app.exe

# 2. 启动服务 — MQTT 监听 :1883，管理后台监听 http://127.0.0.1:8080
app.exe
```

## Management UI & HTTP API

### 管理后台页面 (基于 Zan.Mvc AdminController)
- `/admin/iot`：IoT 核心指标看板与快速发布大屏
- `/admin/iot/clients`：在线客户端连接与一键强制下线 (Kick)
- `/admin/iot/retained`：保留消息列表查看与一键清除
- `/admin/iot/webhooks`：规则与 Webhook 转发状态及实时审计流水
- `/admin/iot/console`：在线调试控制台 (支持 QoS 0/1/2 与 Retain)

### 核心 REST API

| Method & path                 | Auth      | Purpose                          |
|-------------------------------|-----------|----------------------------------|
| `GET  /`                      | —         | 重定向至 `/admin/iot` (自动引导登录) |
| `GET  /iot/clients`           | —         | 获取在线客户端列表               |
| `GET  /iot/clients/{id}`      | —         | 获取指定客户端详情与订阅         |
| `POST /iot/clients/{id}/kick` | login     | 强制客户端下线 (断开连接)        |
| `GET  /iot/subscriptions`     | —         | 全局订阅关系列表                 |
| `GET  /iot/topics`            | —         | 主题列表与最新一条 payload       |
| `GET  /iot/metrics`           | —         | Broker 核心指标快照              |
| `GET  /iot/retained`          | —         | 当前生效的保留消息列表           |
| `POST /iot/retained/clear`    | login     | 清除指定主题 (或全部) 保留消息   |
| `GET  /iot/webhook/stats`     | —         | Webhook 运行指标与最近审计日志   |
| `POST /iot/webhook/mock`      | —         | 本地 Webhook 接收桩 (开箱即用)   |
| `POST /iot/publish`           | login     | 注入消息 (`topic`, `payload`, `qos`, `retain`) |
| `GET  /admin/stats`           | login     | Broker + HTTP 双重运行期指标     |
| `POST /auth/login`            | —         | 管理员认证鉴权获取 Token         |

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
