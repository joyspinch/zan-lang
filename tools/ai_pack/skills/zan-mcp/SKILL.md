---
name: zan-mcp
description: How to drive the Zan SDK through its MCP server (stdio or hosted HTTP) — the tool catalog, reading and editing without re-reading, skills over MCP, the safety flags, deploying one shared server, and how to reach zan-lsp / zan-dap directly. Use it when connecting an AI client to Zan or when an MCP call misbehaves.
---

# Using the Zan MCP server

## Start it — or rather, let the client start it

stdio servers are **spawned by the MCP client**, not run by hand: it launches
the command from its config as a child process on project open and tears it
down with the session. If no config exists in this project yet, write it
yourself — no human needed:

```
<ZAN_MCP> --init-agent .    # writes .mcp.json / .cursor\mcp.json / .vscode\mcp.json
                            # (+ AGENTS.md, .agents\skills\) into the project;
                            # existing files are kept; --force overwrites
```

Then reopen the project and the client starts the server itself. Diagnostics
why a session did not come up: the config's `command` must be the **expanded**
SDK path (`<ZAN_MCP>` placeholders are only valid inside the SDK's own `ai\`
templates), and the trailing argument is the workspace root (`.` = the launch
directory).

For a one-off probe you can still run it yourself:

```
<ZAN_MCP> --stdio .         # newline-delimited JSON-RPC 2.0 on stdin/stdout
```

stdout carries protocol messages only — one JSON object per line, no banner —
so anything you want logged must go to stderr.

Config templates ship in the SDK under `ai\`: `.mcp.json` (Claude Code and
most clients), `.cursor\mcp.json`, `.vscode\mcp.json` — install them with
`--init-agent`, not by hand-copying (the `<ZAN_MCP>` placeholder would stay
literal and the client would fail to launch the server). Full guide:
`docs\AI_ONBOARDING.md`.

HTTP is the same API on `POST /`, `/mcp` or `/rpc`:

```
<ZAN_MCP> . --port 18848
<ZAN_MCP> . --host 0.0.0.0 --token-env ZAN_MCP_TOKEN
```

A non-loopback host without a token is refused a clean bill of health for good
reason: pass `--token-env` (the client then sends
`Authorization: Bearer <token>`) and never write the token into a file you
commit.

Hosted, one server for every project and client: `scripts\serve-mcp.ps1` /
`scripts\serve-mcp.sh` in the SDK wrap the flags that matter
(`--frozen-tools`, `--token-env`, optionally `--read-only` / `--no-exec`) —
see `docs\MCP_HOSTING.md`.

## Handshake

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"probe","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
```

`initialize` also returns `instructions` telling you to call `zan_start_here`
first. Do that — it is one round trip and it replaces reading the tree.

## Tools, and what each one is for

Ask-the-toolchain tools (use these before file tools):

| Tool | Answers |
|---|---|
| `zan_start_here` | Layout, entry point, build/run/test commands, the Zan coding rules, the tool catalog, resources, and which optional tools this installation actually has (`available`). |
| `zan_api_search` | `{query, limit?}` → exact stdlib signature, file, line. Use it before writing any call you are not certain of. |
| `zan_example` | `{topic?}` → the files of a shipped, build-verified program; no topic → the catalog (GUI, games, console, HTTP/MVC servers, IoT, DLL libraries). |
| `zan_compile` | `{content, filename?}` → compiles that snippet in an isolated directory and returns `{ok, exitCode, diagnostics[], raw}` with file/line/column. The cheapest way to settle a language question. |
| `zan_build_project` | `{entry?}` (defaults to `src/App.zan` / `src/main.zan`) → builds the workspace project, same structured diagnostics. |

Workspace tools: `list_dir`, `read_file`, `write_file`, `edit_file`, `mkdir`,
`delete_path`, `move_path`, `copy_path`, `stat_path`, `find_files`,
`search_text`, `run_command`, `health_check`, `skills_list`, `skill_read`.

### Reading and editing without reading twice

`read_file` has two modes, and for source code you want the first:

| Call | Returns |
|---|---|
| `{path, from_line, max_lines}` | that line window, `412\| code` numbered, plus `lines`, `to_line`, `truncated`. Default `max_lines` 120, `numbered` off with `numbered:false`. |
| `{path, offset, limit}` | raw bytes from an offset; `encoding=base64` for binaries. |
| `{path}` | the whole file, up to the server's `maxReadBytes`. The expensive one. |

So: `search_text` for the line, `read_file` for that window, `edit_file` for the
change, compile. The numbers in the window are there so you can quote
`path:line` later instead of reading it again.

`edit_file` takes `{path, old, new, all?}` and replaces an exact snippet in
place, so a three-line fix costs three lines of payload instead of the file
twice. It refuses rather than guesses:

* `old` missing from the file → your copy is not byte-exact (whitespace,
  indentation). Re-read that window; do not fall back to `write_file`.
* `old` found more than once → extend it with the surrounding lines until it is
  unique, or pass `all=true` if replacing every occurrence is what you mean.
* Success returns `{replacements, line, bytes}` — the line is where it landed,
  no verification read needed.

`write_file` stays the right tool for a new file or a wholesale replacement.

### Skills over MCP

`skills_list` returns each skill's `name`, `summary`, `bytes` and `scope`
(`project` = `.agents/skills` in the workspace, `installed` = shipped with the
SDK or given by `--skills`) — never the bodies. `skill_read({name, file?})`
returns one body, `SKILL.md` by default, or another file inside that skill's
directory. Read the one that matches the task at hand, once.

Resources (whole-project context without walking directories): `repo://map`,
`repo://symbols`, `repo://routes` — the `tools/repomap` output under `.zanmap/`.
Refresh them after you add or move files.

## Rules the server enforces

* Paths are relative to the workspace root. Absolute paths, drive letters and
  `..` segments are rejected — this is not negotiable, so do not try to reach
  outside; ask for the file to be added to the workspace instead.
* `--read-only` fails every mutating tool (`write_file`, `edit_file`, …);
  `--no-exec` removes `run_command`. If a call is refused for one of these, say
  so instead of working around it.
* An optional `zan_*` tool is absent when its backing file is: `knowledge\symbols.json`
  (api search), `knowledge\gallery.json` (examples), `toolchain\zanc.exe`
  (compile/build). Check `available` from `zan_start_here` before blaming the call.
* `--frozen-tools` (what a hosted server runs) advertises the full catalog
  whatever this workspace happens to have, so `tools/list` — and with it the
  cached prefix of your prompt — is identical for every project. A tool that is
  advertised but unbacked still answers with a plain `error:` explaining what is
  missing, so `available` from `zan_start_here` remains the thing to check
  before calling an optional tool, not the catalog.

## Semantic navigation and debugging are separate services

There is **no** MCP wrapper for them; the editor speaks the protocols directly,
both over stdio with `Content-Length` framing:

* `toolchain\zan-lsp.exe` — diagnostics, completion, hover, signature help,
  definition, references, rename, document/workspace symbols, code actions,
  execute command. Language id `zan`.
* `toolchain\zan-dap.exe` — launch/attach, breakpoints (conditional, hit-count,
  logpoints, exception, function), threads, stack traces, scopes, variables,
  `evaluate`, `setVariable`, continue/step/pause/terminate. It drives gdb:
  bundled `toolchain\debugger\bin\gdb.exe`, else `ZAN_GDB`, else system gdb.

If you need a fact that only these can give (a hover signature, a live variable),
either register them in the editor or get it another way — `zan_api_search` for
signatures, a `zan_compile` probe for semantics. Do not claim you queried the LSP
through MCP.

## When something looks wrong

1. `health_check` — is the server the one you think, and which workspace?
2. `zan_start_here` → `available` — is the tool you want even registered?
3. stdio silent? Something wrote non-JSON to stdout, or the request was not a
   single line. Send one compact JSON object per line.
4. HTTP 401 → token missing/mismatched; connection refused → wrong port or the
   server bound to loopback only.
