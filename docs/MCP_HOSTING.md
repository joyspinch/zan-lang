# Hosting one shared Zan MCP server

`zan-mcp` is the same binary in two transports. Local clients launch it over
stdio, one process per project; a hosted deployment binds HTTP once and every
client and project talks to that. This page is the second case.

Why bother, given stdio already works: a hosted server is the only place where
the **tool catalog is the same for everyone**. Clients put `tools/list` near the
front of the prompt, and the prompt cache only pays off on a byte-identical
prefix — one catalog that never shifts per project (`--frozen-tools`) keeps that
prefix cacheable, and one deployment means one place to update skills and rules.

## Run it

```powershell
$env:ZAN_MCP_TOKEN = '<secret>'                 # never commit this
scripts\serve-mcp.ps1 -Workspace D:\project\foo -BindHost 0.0.0.0
```

```sh
export ZAN_MCP_TOKEN=<secret>
scripts/serve-mcp.sh --workspace /srv/projects/foo --host 0.0.0.0
```

The scripts are thin: they locate the binary, refuse a network bind without a
token, and pass the flags a hosted server wants. The equivalent bare command:

```
zan-mcp <workspace> --host 0.0.0.0 --port 18848 --token-env ZAN_MCP_TOKEN \
        --frozen-tools --skills <sdk>/tools/ai_pack/skills
```

Build the binary first if the release did not ship it:

```
build\zanc.exe --stdlib-path stdlib tools\mcp_server\mcp_server.zan -o tools\zan-mcp.exe
```

Flags that matter here:

| Flag | Why |
|---|---|
| `--token-env VAR` | Token read from the environment, so it is not in the process list or a config file. A non-loopback bind without one is a mistake the server warns about. |
| `--frozen-tools` | Advertise the whole catalog regardless of what this deployment can back, so `tools/list` is identical everywhere. A call to an unbacked tool answers with the reason; `zan_start_here` still reports `available`. |
| `--skills <dir>` | Skills served by `skills_list` / `skill_read`, on top of the workspace's own `.agents/skills`. |
| `--read-only`, `--no-exec` | Drop mutating tools / `run_command`. Use both unless the callers are people you would hand a shell to. |
| `--workers <n>` | Serve from n processes. |
| `--max-read <bytes>` | Per-read cap, default 8 MiB. |

## Client configuration

Local stdio (what `--init-agent` writes) stays the default and needs no
credentials. For the hosted server, point the client at the HTTP endpoint and
let it send the bearer token; the exact key names differ per client, so check
its own docs before copying:

* Claude Code / most `.mcp.json` clients: an entry with `"type": "http"`,
  `"url": "https://mcp.example.com/mcp"` and an `Authorization` header.
* Cursor (`.cursor/mcp.json`): a `url` entry with `headers`.
* VS Code (`.vscode/mcp.json`): a `"type": "http"` server; use an `inputs`
  prompt or `${env:ZAN_MCP_TOKEN}` for the token rather than a literal.

Endpoints: `POST /`, `/mcp` or `/rpc` carry JSON-RPC (trailing slash is fine),
`GET /health` is the liveness probe, `DELETE /mcp` ends a session. A client that
opens `GET /mcp` with `Accept: text/event-stream` gets `405` — this server
initiates no messages, so there is no stream to hold open. Clients that insist
on server-sent events are not supported; they must use the POST transport.

## Before you expose it publicly

The current design serves **one workspace per process** and treats every
authenticated caller as trusted inside it. That is fine for one team on an
internal network and not fine for a public multi-tenant endpoint. What is
missing for that, and would have to be built:

* per-tenant workspaces and per-token authorization (today: one `--token`,
  one root),
* rate limiting and audit logging,
* TLS. The server speaks plain HTTP; terminate TLS in front of it (nginx,
  Caddy, a cloud load balancer, or a tunnel) and keep the port itself bound to
  the private interface.

A safe first deployment: internal host, `--read-only --no-exec` unless a caller
genuinely needs to write, TLS terminated by a reverse proxy, token in the
process environment from your secret store, one instance per project workspace
on distinct ports.
