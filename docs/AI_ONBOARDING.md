# Connecting an AI coding tool to Zan

Goal: your AI editor writes correct Zan **without** crawling the sources. One
config file connects it to the SDK's MCP server; from then on the model asks the
toolchain instead of guessing — signatures, working examples, real compiler
diagnostics.

Everything below ships inside the published SDK (`dist\win-x64`, referred to as
`<ZAN_SDK>`):

```
<ZAN_SDK>\zan-mcp.exe        MCP server (stdio + HTTP)
<ZAN_SDK>\AGENTS.md          the rules an agent must follow in a Zan project
<ZAN_SDK>\.agents\skills\    zan-development / zan-debugging workflows
<ZAN_SDK>\.mcp.json          ready-made config (Claude Code / generic)
<ZAN_SDK>\.cursor\mcp.json   ready-made config (Cursor)
<ZAN_SDK>\.vscode\mcp.json   ready-made config (VS Code + Copilot)
<ZAN_SDK>\knowledge\         symbols.json (API index), gallery.json (examples)
<ZAN_SDK>\toolchain\         zanc, zan-lsp, zan-dap, linker, gdb
```

The shipped configs already contain the absolute path of the SDK that produced
them. Move the SDK and you must update that one `command` string.

## 1. Connect

Copy the matching file into **your project** (not the SDK) and restart the
editor:

| Tool                     | Copy from                    | To                          |
|--------------------------|------------------------------|-----------------------------|
| Claude Code              | `<ZAN_SDK>\.mcp.json`        | `<project>\.mcp.json`       |
| Cursor                   | `<ZAN_SDK>\.cursor\mcp.json` | `<project>\.cursor\mcp.json`|
| VS Code (Copilot agent)  | `<ZAN_SDK>\.vscode\mcp.json` | `<project>\.vscode\mcp.json`|
| Windsurf / other clients | `<ZAN_SDK>\.mcp.json`        | that client's `mcpServers` config |

They all launch the same thing:

```
<ZAN_SDK>\zan-mcp.exe --stdio .
```

`--stdio` speaks newline-delimited JSON-RPC 2.0 on stdin/stdout (nothing else is
written to stdout, so the stream stays clean). The trailing `.` is the
workspace: the folder the client launched the server in. Every path the model
passes is resolved inside it — absolute paths, drive letters and `..` are
rejected.

Verify by hand:

```
echo {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"probe","version":"1"}}} | <ZAN_SDK>\zan-mcp.exe --stdio .
```

One JSON line back = connected.

## 2. Copy the rules into the project

```
copy <ZAN_SDK>\AGENTS.md          <project>\AGENTS.md
xcopy /E /I <ZAN_SDK>\.agents     <project>\.agents
```

`AGENTS.md` is read automatically by Claude Code, Cursor, Copilot and Devin.
`.agents\skills\` holds the two workflows worth having verbatim:
`zan-development` (look up → edit → compile → run) and `zan-debugging`
(diagnostics → leak check → LSP → DAP).

## 3. The workflow the model should follow

1. **`zan_start_here`** — one call: layout, entry point, build/test commands,
   coding rules, tool catalog, what this installation actually has. It replaces
   browsing the tree, and the server also advertises it in
   `initialize.instructions`.
2. **`zan_api_search {query: "Http.Post"}`** — exact signature, file and line from the
   stdlib symbol index. Before writing the call, not after it fails.
3. **`zan_example {topic: "server-mvc"}`** — the files of a shipped, build-verified
   program (no topic = catalog: GUI designer windows, games, console, HTTP and
   MVC servers, IoT, DLL libraries).
4. **`read_file` / `search_text` / `find_files`** — only the part of the project
   that matters.
5. **`write_file`** — the edit.
6. **`zan_build_project {entry?}`** (defaults to `src/App.zan` / `src/main.zan`),
   or **`zan_compile {content, filename?}`** for a snippet compiled in an
   isolated directory — both return `{ok, exitCode, diagnostics[], raw}` with
   file/line/column, so the model fixes real errors instead of imagining them.
7. **`run_command`** — run the program or the tests.

Resources for whole-project context, no directory walking:
`repo://map` (file map), `repo://symbols` (symbol index), `repo://routes` (HTTP
routes) — as produced by `tools/repomap` into `.zanmap/`.

## 4. Options worth knowing

```
zan-mcp.exe --stdio .                        # what the configs use
zan-mcp.exe --stdio . --read-only            # refuse every mutating tool
zan-mcp.exe --stdio . --no-exec              # keep files, drop run_command
zan-mcp.exe . --port 18848                   # HTTP transport instead of stdio
zan-mcp.exe . --host 0.0.0.0 --token-env ZAN_MCP_TOKEN   # reachable, authenticated
```

Give `--read-only` to a tool you do not want writing to disk, and always pair a
non-loopback `--host` with a token (`--token-env` keeps it out of the process
list). Never put a token in a config file you commit.

The optional `zan_*` tools register only when their backing files exist next to
the server: `knowledge\symbols.json` for `zan_api_search`,
`knowledge\gallery.json` for `zan_example`, `toolchain\zanc.exe` for
`zan_compile` / `zan_build_project`. `zan_start_here` reports which of them are
live under `available`.

## 5. Language server and debugger

MCP covers editing and building; the semantic and debug protocols are consumed
directly by the editor:

* `<ZAN_SDK>\toolchain\zan-lsp.exe` — LSP over stdio with `Content-Length`
  framing: diagnostics, completion, hover, signature help, definition,
  references, rename, symbols, code actions. Register it for language id `zan`.
* `<ZAN_SDK>\toolchain\zan-dap.exe` — DAP over stdio with `Content-Length`
  framing: breakpoints (conditional, hit-count, logpoints), stepping, scopes,
  variables, `evaluate`. It drives gdb — the bundled
  `toolchain\debugger\bin\gdb.exe` if present, else `ZAN_GDB`, else a system gdb.

Details and a VS Code snippet for each: [`TOOLING.md`](TOOLING.md).

## 6. Without MCP

The same knowledge is on disk and greppable: `knowledge\symbols.json` (one JSON
record per line: name, kind, file, line, sig), `knowledge\gallery.json` (topic →
example files), `templates\` (project skeletons), `examples\` (runnable
programs), and the compiler itself:

```
<ZAN_SDK>\toolchain\zanc.exe <entry.zan> --auto-stdlib -o build\app.exe
<ZAN_SDK>\toolchain\zanc.exe <entry.zan> --auto-stdlib --publish -o app.exe
<ZAN_SDK>\toolchain\zanc.exe <entry.zan> --auto-stdlib --check-leaks -o build\app.exe
```
