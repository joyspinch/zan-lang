---
name: zan-mcp
description: How to drive the Zan SDK through its MCP server (stdio or HTTP) — the tool catalog, what each tool answers, the resources, the safety flags, and how to reach zan-lsp / zan-dap directly. Use it when connecting an AI client to Zan or when an MCP call misbehaves.
---

# Using the Zan MCP server

## Start it

```
<ZAN_MCP> --stdio .        # newline-delimited JSON-RPC 2.0 on stdin/stdout
```

The trailing argument is the workspace root (`.` = the launch directory). stdout
carries protocol messages only — one JSON object per line, no banner — so
anything you want logged must go to stderr.

Client configs ship ready-made in the SDK: `.mcp.json` (Claude Code and most
clients), `.cursor\mcp.json`, `.vscode\mcp.json`. Copy the matching one into the
project. Full guide: `docs\AI_ONBOARDING.md`.

HTTP is the same API on `POST /`, `/mcp` or `/rpc`:

```
<ZAN_MCP> . --port 18848
<ZAN_MCP> . --host 0.0.0.0 --token-env ZAN_MCP_TOKEN
```

A non-loopback host without a token is refused a clean bill of health for good
reason: pass `--token-env` (the client then sends
`Authorization: Bearer <token>`) and never write the token into a file you
commit.

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

Workspace tools: `list_dir`, `read_file` (`encoding=base64` for binary),
`write_file`, `mkdir`, `delete_path`, `move_path`, `copy_path`, `stat_path`,
`find_files`, `search_text`, `run_command`, `health_check`.

Resources (whole-project context without walking directories): `repo://map`,
`repo://symbols`, `repo://routes` — the `tools/repomap` output under `.zanmap/`.
Refresh them after you add or move files.

## Rules the server enforces

* Paths are relative to the workspace root. Absolute paths, drive letters and
  `..` segments are rejected — this is not negotiable, so do not try to reach
  outside; ask for the file to be added to the workspace instead.
* `--read-only` fails every mutating tool; `--no-exec` removes `run_command`.
  If a call is refused for one of these, say so instead of working around it.
* An optional `zan_*` tool is absent when its backing file is: `knowledge\symbols.json`
  (api search), `knowledge\gallery.json` (examples), `toolchain\zanc.exe`
  (compile/build). Check `available` from `zan_start_here` before blaming the call.

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
