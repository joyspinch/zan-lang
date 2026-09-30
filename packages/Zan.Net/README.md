# Zan.Net

Pure-Zan networking, extracted from the standard library (`stdlib/System/Net`)
with namespaces preserved — `using System.Net;` and friends keep working with
zero source changes; zanc resolves the namespaces across the stdlib and
package source roots automatically.

## Contents (namespace → what lives there)

- `System.Net` — root types: `Net.zan` (DNS/endpoint helpers), `NetworkInterface`,
  `Ping`, `ServerBanner`, the `Worker`/`Worker.Mqtt`/`Worker.Sse`/`Worker.Ws`
  background-worker family
- `System.Net.Sockets` — `TcpClient`, `TcpListener`, `UdpClient`
- `System.Net.Tls` — the managed TLS stack (record layer, handshake, trust,
  policy) riding on the sockets above; consumers: Zan.Data drivers (MySQL
  TLS), AppUpdate, Commercial, redis/sqlserver TLS paths
- `System.Net.Http` + `.Client` + `.Download` + `.Proxy` — HTTP/1.1 and
  HTTP/2 client (`HttpClient`, `CookieJar`, `SseSink`), download jobs, the
  reverse proxy/forwarder with tunnel support
- `System.Net.Https` — HTTPS server plumbing
- `System.Net.WebSocket` + `.Secure` — WS and WSS client/server
- `System.Net.Sse` — server-sent events server side
- `System.Net.Mqtt`, `System.Net.Coap`, `System.Net.Modbus`, `System.Net.Ntp`,
  `System.Net.Sip`, `System.Net.WebDav` — application protocols
- `System.Net.External` — adapters to OS/native network facilities

## Consumers

Zan.Mvc (HTTP server), Zan.Data drivers (socket transport for MySQL/Postgres/
SqlServer/TDengine/Firebird), Zan.AppUpdate, Zan.Commercial, the IDE
(AI/HTTP clients), and any program spelling `using System.Net.*`.

## Notes

- Pure Zan; the only native interaction is via the runtime's socket syscalls —
  no native driver bundles.
- `RandomNumberGenerator` deliberately stays in the stdlib root (namespace
  `System`): `Guid` needs it and `using System;` must not drag crypto files in.
