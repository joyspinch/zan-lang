---
name: zan-debugging
description: How to diagnose a failing Zan program with the Zan SDK — compiler diagnostics, ARC leak reports, the language server (zan-lsp) and the debug adapter (zan-dap) over stdio. Use it when a build fails, a program misbehaves, or memory looks wrong.
---

# Debugging Zan

Escalate in this order; most problems die at step 1 or 2.

## 1. Compiler diagnostics

```
zan_build_project()        # MCP: {ok, exitCode, diagnostics[], raw}
zan_compile(content)       # MCP: same, for one snippet
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib -o build/app
```

Fix the **first** diagnostic first — the rest are often its fallout. Each one
carries file, line, column and message. When a diagnostic surprises you, cut the
construct down to a few lines and `zan_compile` that: a minimal snippet tells you
whether your assumption or the code is wrong.

Common ones:

* "can return null; accessing … faults at runtime" → store the value, check for
  null (or `?.`) before using it.
* unknown member → the API does not exist as written: `zan_api_search` it.

## 2. Runtime behaviour

Build and run it: `run_command("build/app")`. Print state at the boundary you
suspect (`Console.WriteLine`) — cheap and decisive. Note that `Console.Error`
does not exist; error text also goes through `Console.WriteLine`.

## 3. Memory / ARC

```
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib --check-leaks -o build/app
build/app
```

At exit it reports objects still reachable and the `file:line:col` of the `new`
that allocated each one. An unbroken reference cycle is the usual cause — ARC
does not collect cycles.

Runtime guards are on by default (e.g. integer division by zero traps with a
source location); `--no-runtime-checks` turns them off, so keep them on while
debugging.

## 4. Semantic questions: `zan-lsp`

`<ZAN_SDK>/toolchain/zan-lsp` speaks **LSP over stdio with `Content-Length`
framing**. Point your editor's language client at it (`command: zan-lsp`,
`transport: stdio`, language id `zan`) and use hover / go-to-definition /
references / completion instead of reading the standard library by hand.
It advertises: diagnostics (on open/change/save), completion, hover, signature
help, definition, references, rename, workspace and document symbols, code
actions, `workspace/executeCommand`.

There is no MCP wrapper around it: it is a protocol server, so it is the editor
(or a client you drive yourself) that talks to it.

## 5. Stepping: `zan-dap`

`<ZAN_SDK>/toolchain/zan-dap` speaks **DAP over stdio with `Content-Length`
framing**, and drives a real native session through gdb: the bundled
`toolchain/debugger/bin/gdb`, else `ZAN_GDB`, else a system gdb. If the bundle
omitted gdb (it is not always relocatable), set `ZAN_GDB` to a gdb you have.

Supported: `initialize`, `launch`/`attach`, `configurationDone`,
`setBreakpoints` (conditional, hit-count, logpoints), exception and function
breakpoints, `threads`, `stackTrace`, `scopes`, `variables`, `setVariable`,
`evaluate`, `exceptionInfo`, `continue`, `next`, `stepIn`, `stepOut`, `pause`,
`terminate`, `disconnect`.

VS Code launch config:

```jsonc
{ "type": "zan", "request": "launch", "name": "Debug Zan program",
  "program": "${workspaceFolder}/build/app.exe" }
```

Build with debug info (default build, i.e. no `--publish`) before stepping.

## Honesty rule

Report what you observed — the diagnostic text, the leak report, the variable
value. A hypothesis you did not check is a hypothesis, and must be labelled as
one.
