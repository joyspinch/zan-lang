# AGENTS.md — working in a Zan project

Rules for any AI agent (Claude Code, Cursor, Copilot, Windsurf, Devin, …) that
writes Zan code in this project. They are short on purpose: follow them and the
compiler will agree with you on the first or second try.

## 0. Start with one call, not with a file tour

This SDK ships an MCP server (`zan-mcp`, stdio). If it is connected, call
**`zan_start_here` first**. One call returns the project layout, the entry
point, the build/test commands, the tool catalog and these rules, resolved
against *this* installation. Do not crawl the tree to find out what the project
is.

Not connected? Connect it yourself, no human setup needed: run
`<ZAN_MCP> --init-agent .` from the project root. It writes the client config
files into this project (existing files are kept), and the client then starts
the server automatically the next time the project opens — stdio MCP servers
are spawned by the client, not run by hand. Until it is connected, work from
`llms.txt` in the SDK root and compile with `<ZAN_SDK>/toolchain/zanc`.

## 0a. Read once, precisely, then edit

The expensive habit is not writing the wrong code, it is reading the same code
four times. A tool result stays true until you change it, so treat every read as
permanent knowledge:

1. `search_text("Foo.Bar")` → `path:line`. Do not open files to look for it.
2. `read_file(path, from_line, max_lines)` → one numbered window around that
   line, wide enough to edit from (say ±40 lines). Not the whole file.
3. `edit_file(path, old, new)` → the *entire* change in one call per site. Not
   one line, re-read, next line.
4. Compile. Only now read again, and only what a diagnostic points at.

Do not re-read a range you already have, do not re-run a search you already ran,
and never read a file just so you can `write_file` it back — `edit_file` needs
only the snippet. If the file is open in the user's editor, its contents are
already in the conversation; use them. `write_file` is for creating a file or
replacing it wholesale, nothing else.

`edit_file` refuses a fuzzy edit instead of guessing: `old` must occur exactly
once (extend it with neighbouring lines until it does, or pass `all=true` on
purpose). A refusal means your snippet does not match the file byte for byte —
re-read that one window rather than switching to a whole-file rewrite.

## 0b. Skills: list, then read the one that fits

`skills_list()` returns names and one-line summaries only; `skill_read(name)`
returns a body. Read the body of the skill you are about to use, not all of
them — and not again later in the same session.

## 1. Never guess an API

Zan is not C#, and looking similar is exactly the trap. Before you write a call
you have not personally verified in this project:

* `zan_api_search("Http.Post")` → exact signature, file, line.
* `zan_example("<topic>")` → a shipped, build-verified program to copy from
  (call it with no topic for the catalog).
* Still unsure? `zan_compile` a five-line snippet and read the diagnostic.

Inventing a method that "should" exist is the single most expensive mistake in
this language: it compiles in your head and nowhere else.

## 2. Compile before you claim

`zan_build_project` (whole project) or `zan_compile` (one snippet) returns
`{ok, exitCode, diagnostics[], raw}` with file/line/column per diagnostic. Fix
diagnostics from the top; a later error is often a consequence of the first.

Without MCP:

```
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib -o build/app      # debug build
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib --publish -o app  # release
```

Say what you actually ran. "Should work" is not a result.

## 3. Language facts worth knowing before the first edit

* Static types, C#-like syntax, **ARC** memory management — no GC, no manual
  `free`. Cycles need care, not a collector.
* `async`/`await` with a real scheduler; `await` only inside `async` members.
* A member is reached through its class: `McpServer.Helper(...)`, not a bare
  `Helper(...)`, for static members of the enclosing class.
* Nullability is checked: calling a member on something that may be null is an
  error. Store it, test it, then use it.
* `#if WINDOWS` / `#else` selects platform code; `[DllImport("crt")]` binds a
  native symbol.
* One compilation entry pulls in what it uses; `--auto-stdlib` finds the
  standard library shipped next to the compiler.

## 4. Configuration is not code

Hosts, ports, credentials, limits, prices, feature switches and anything a
deployment may want to change belong in the project's config file (server
projects: `config/app.json`, read at startup), never in a source literal. If
you need a new knob, add it to the config file *and* give it a default.

## 5. GUI code uses the component library

Windows and controls come from the standard library's GUI components (designer
`.zform` + code-behind, or the immediate-mode loop — `zan_example("gui-window")`
shows both). Do not hand-draw widgets, do not roll your own event loop.

## 6. Server projects: routes and permissions are attributes

The `server-mvc` template generates its route table at compile time from
attributes — there is no wiring file to maintain:

```zan
[Route("api/[controller]/[action]")]          // class-level prefix
[Custom(Authorization = CustomAuthorization.ApiAuth)]   // class-level default
class Orders : ApiController {
    [HttpPost]
    [Description("下单")]
    async void Create() { … }                 // POST /api/orders/create

    [HttpGet]
    [Custom(Authorization = CustomAuthorization.None)]   // method overrides class
    void Public() { … }
}
```

`CustomAuthorization` is the whole auth gate: `None` (public), `Auth` (optional
sign-in), `Login` (must be signed in), `Grant` (signed in **and** granted this
action), `ApiAuth` (same as Grant, for token callers). Grants live in the
permission tables, not in `if` statements inside handlers. Keep the template's
layout (`src/Controller`, `src/Model`, `src/Dao`, `src/Feature`,
`src/Framework`, `views/`, `config/app.json`) rather than inventing your own —
and read `zan_example("server-mvc")` before the first endpoint.

## 7. Keep the project clean

Probes, scratch builds, logs and one-off scripts go in `_scratch/`. Build output
goes in `build/`. Nothing throwaway in the project root.

## 8. Be honest about what you verified

Report the command you ran and its outcome. Distinguish "compiled" from
"tested" from "assumed". If something is unverified, say so — an unflagged
assumption costs more than an admitted gap.
