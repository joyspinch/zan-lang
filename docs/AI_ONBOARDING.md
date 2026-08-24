# Connecting an AI coding tool to Zan

Goal: your AI editor writes correct Zan **without** crawling the sources. One
command connects your project to the SDK's MCP server; from then on the model
asks the toolchain instead of guessing — signatures, working examples, real
compiler diagnostics.

Two directories, and the difference matters: the **SDK installation**
(`dist\win-x64`, referred to as `<ZAN_SDK>`) is where the IDE and the toolchain
live, and your **project** is wherever you write code. AI configuration belongs
in the project — nobody develops inside the SDK folder — so the SDK only keeps
the templates:

```
<ZAN_SDK>\tools\zan-mcp.exe  MCP server (stdio + HTTP)
<ZAN_SDK>\ai\                templates installed into a project by --init-agent:
                             AGENTS.md, skills\, mcp.json,
                             cursor.mcp.json, vscode.mcp.json
<ZAN_SDK>\knowledge\         symbols.json (API index), gallery.json (examples),
                             zform.json (.zform schema and control catalog)
<ZAN_SDK>\toolchain\         zanc, zan-lsp, zan-dap, linker, gdb
```

## 1. Connect

One command, run once per project:

```
<ZAN_SDK>\tools\zan-mcp.exe --init-agent D:\path\to\your-project   [--force]
```

In the Zan IDE the assistant panel's **“为当前项目启用 AI 接入”** button does
exactly this for the project you have open.

It installs, filling in this installation's paths and keeping any file that is
already there (`--force` overwrites):

| Into the project    | Used by                                        |
|---------------------|------------------------------------------------|
| `AGENTS.md`         | Claude Code, Cursor, Copilot, Devin            |
| `.agents\skills\`   | the same, as reusable workflows                |
| `.mcp.json`         | Claude Code and most clients                   |
| `.cursor\mcp.json`  | Cursor                                         |
| `.vscode\mcp.json`  | VS Code + Copilot agent mode                   |

Restart the editor afterwards. Other clients (Windsurf, …) take the
`mcpServers` block out of `.mcp.json` as-is.

Do not copy the files out of `ai\` by hand: they still carry the `<ZAN_MCP>` /
`<ZAN_SDK>` / `<ZAN_PROJECT>` placeholders that `--init-agent` expands, and a
client cannot launch a server whose path is a literal placeholder. If you move
the SDK, re-run `--init-agent --force`.

All clients then launch the same thing:

```
<ZAN_SDK>\tools\zan-mcp.exe --stdio <project>
```

`--stdio` speaks newline-delimited JSON-RPC 2.0 on stdin/stdout (nothing else is
written to stdout, so the stream stays clean). The trailing `.` is the
workspace: the folder the client launched the server in. Every path the model
passes is resolved inside it — absolute paths, drive letters and `..` are
rejected.

Verify by hand:

```
echo {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"probe","version":"1"}}} | <ZAN_SDK>\tools\zan-mcp.exe --stdio .
```

One JSON line back = connected.

## 2. What the installed rules are for

`AGENTS.md` is read automatically by Claude Code, Cursor, Copilot and Devin: it
is the house style of Zan code, so the model stops inventing C#-isms.
`.agents\skills\` holds the workflows worth having verbatim: `zan-development`
(look up → edit → compile → run), `zan-debugging` (diagnostics → leak check →
LSP → DAP) and `zan-mcp` (the server's own tool catalog).

The server finds `knowledge\`, `toolchain\zanc.exe` and `stdlib\` from the SDK
root above `tools\` on its own; `--sdk-root <dir>` overrides that if you moved
the executable out of the SDK.

Knowledge is generated from the sources by the Zan `GenKnowledge` tool. It only
needs to exist on the machine running the MCP server; clients do not need a
copy of `symbols.json`, `gallery.json`, or `zform.json`. A writable server can
refresh `gallery.json` and `zform.json` in process with
`zan_refresh_knowledge`; `symbols.json` remains the RepoMap index and is
regenerated separately when it is missing.

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
4. **`search_text` / `find_files`** → `path:line`, then **`read_file {path,
   from_line, max_lines}`** → that numbered window. One window, not the file,
   and not the same window twice: a tool result stays true until it is changed.
5. **`edit_file {path, old, new}`** — the edit, in place, whole change in one
   call. `old` must match byte for byte and occur once, or the call refuses
   instead of guessing. `write_file` is for a new file or a full replacement.
   Task-specific procedure needed? **`skills_list`** → names and summaries,
   **`skill_read {name}`** → the body of the one that fits.
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
zan-mcp.exe --init-agent <project> [--force] # install the pack into a project
zan-mcp.exe --stdio .                        # what the configs use
zan-mcp.exe --stdio . --read-only            # refuse every mutating tool
zan-mcp.exe --stdio . --no-exec              # keep files, drop run_command
zan-mcp.exe . --port 18848                   # HTTP transport instead of stdio
zan-mcp.exe . --host 0.0.0.0 --token-env ZAN_MCP_TOKEN   # reachable, authenticated
zan-mcp.exe . --frozen-tools                 # same tool catalog for every workspace
zan-mcp.exe . --skills <dir>                 # skills served by skills_list/skill_read
```

One shared HTTP deployment for every project and client — `scripts\serve-mcp.ps1`
or `scripts/serve-mcp.sh`, details in `docs\MCP_HOSTING.md`. `--frozen-tools`
belongs there: a catalog that does not shift per workspace is what keeps the
client's cached prompt prefix valid.

Give `--read-only` to a tool you do not want writing to disk, and always pair a
non-loopback `--host` with a token (`--token-env` keeps it out of the process
list). Never put a token in a config file you commit.

The optional `zan_*` tools register only when their backing files exist in the
SDK root: `knowledge\symbols.json` for `zan_api_search`,
`knowledge\gallery.json` for `zan_example`,
`knowledge\zform.json` for `zan_form_schema`, `toolchain\zanc.exe` for
`zan_compile` / `zan_build_project`. A writable server with the source inputs
also exposes `zan_refresh_knowledge`. `zan_start_here` reports which of them
are live under `available`, together with knowledge freshness.

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
