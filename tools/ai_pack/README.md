# ai\ — templates, not configuration

Everything in this folder is a **template for your own project**, not live
configuration for the SDK folder. `AGENTS.md`, `.mcp.json`, `.cursor\` and
`.vscode\` only mean something inside the project an AI client has open, and
nobody develops inside the SDK install directory — so the SDK keeps the
originals here and installs copies where they work.

## Install into a project

```
tools\zan-mcp.exe --init-agent D:\path\to\your-project   [--force]
```

In the IDE: assistant panel → “为当前项目启用 AI 接入” (same implementation, one
click on the project you have open).

It writes, skipping files that already exist unless `--force` is given:

| from here            | into the project        |
| -------------------- | ----------------------- |
| `AGENTS.md`          | `AGENTS.md`             |
| `skills\`            | `.agents\skills\`       |
| `mcp.json`           | `.mcp.json`             |
| `cursor.mcp.json`    | `.cursor\mcp.json`      |
| `vscode.mcp.json`    | `.vscode\mcp.json`      |

The placeholders are expanded for this installation on the way in:

* `<ZAN_MCP>` → the full path of `tools\zan-mcp.exe`
* `<ZAN_SDK>` → this SDK's install root
* `<ZAN_PROJECT>` → the project directory

That is why copying these files by hand does not work: the placeholders would
stay literal and the client would fail to launch the server.

## What the client then does

The config starts `zan-mcp.exe --stdio <project>` (newline-delimited JSON-RPC on
stdin/stdout, the transport Claude Code / Cursor / Copilot / Windsurf launch by
default). The first call should be `zan_start_here`, which reports the project
layout, entry point, build/test commands, coding rules and the tool catalog in
one response — no tree crawling, no reading the SDK sources.

Full guide, including `zan-lsp` / `zan-dap`: `docs\AI_ONBOARDING.md`.

## One hosted server instead of one process per project

The same binary serves HTTP, and then every client and project shares one
deployment — one place to update the skills and the rules, and one tool catalog
that no longer changes per workspace (`--frozen-tools`), which is what keeps the
client's cached prompt prefix valid:

```
scripts\serve-mcp.ps1 -Workspace D:\project\foo -BindHost 0.0.0.0   # $env:ZAN_MCP_TOKEN
scripts/serve-mcp.sh  --workspace /srv/projects/foo --host 0.0.0.0  # $ZAN_MCP_TOKEN
```

Tokens come from the environment (`--token-env`), never from these template
files. Endpoints, client-side config and what is still missing before a public
multi-tenant endpoint: `docs\MCP_HOSTING.md`.

## Skills are served, not pasted

`skills\` here is installed into `.agents\skills\` for clients that read skill
files directly, and the server also exposes it: `skills_list` returns names and
one-line summaries, `skill_read(name)` returns one body. An agent then spends
context on the skill it is about to use instead of all of them.
