# Zan Tooling: Language Server (M8) & Debug Adapter (M9)

Two standalone executables provide editor-agnostic IDE features. Both speak
their respective JSON-based protocols over **stdio** using `Content-Length`
framing, so they work with any compliant client (VS Code, Neovim, Emacs, …).

Neither tool depends on LLVM: `zan-lsp` reuses the compiler front-end
(lexer/parser/binder/checker) plus the intellisense engine, and `zan-dap`
wraps the IDE debugger engine.

```
build/zan-lsp      # Language Server  (LSP)
build/zan-dap      # Debug Adapter    (DAP)
```

## zan-lsp — Language Server Protocol

Capabilities advertised on `initialize`:

| Feature                          | Method                          |
|----------------------------------|---------------------------------|
| Diagnostics                      | `textDocument/publishDiagnostics` (push) |
| Document sync (full)             | `didOpen` / `didChange` / `didClose` |
| Autocomplete                     | `textDocument/completion`       |
| Hover type info                  | `textDocument/hover`            |
| Go to definition                 | `textDocument/definition`       |
| Document symbols (outline)       | `textDocument/documentSymbol`   |

Diagnostics are produced by running the real compiler front-end
(lexer → parser → binder → checker) and mapping each `zan_diag_t` entry to an
LSP `Diagnostic` with a severity and range. Completion understands both a bare
identifier prefix and `Type.member` context (trigger character `.`).

### VS Code client stub

```jsonc
// contributes.configuration / a minimal extension activate()
const serverOptions = { command: "build/zan-lsp", transport: TransportKind.stdio };
const clientOptions = { documentSelector: [{ scheme: "file", language: "zan" }] };
new LanguageClient("zan", "Zan Language Server", serverOptions, clientOptions).start();
```

## zan-dap — Debug Adapter Protocol

Supported requests: `initialize`, `launch`/`attach`, `configurationDone`,
`setBreakpoints` (with optional conditions), `threads`, `stackTrace`, `scopes`,
`variables`, `continue`, `next`, `stepIn`, `stepOut`, `pause`, `terminate`,
`disconnect`. Emits `initialized`, `stopped`, `terminated`, `exited`, and
`output` events.

Runtime process control is delegated to the IDE debugger engine
(`src/ide/debugger.c`) — Windows `CreateProcess`-based, with a simulated
stepping model on other platforms — while `zan-dap` provides the full
protocol surface.

### VS Code launch config

```jsonc
{
  "type": "zan",
  "request": "launch",
  "name": "Debug Zan program",
  "program": "${workspaceFolder}/build/app.exe"
}
```

## Runtime diagnostics & leak detection (`zanc`)

The code generator inserts source-location-aware runtime guards and can track
heap allocations so leaks are reported at program exit.

| `zanc` flag           | Effect                                                            |
|-----------------------|-------------------------------------------------------------------|
| *(default)*           | Runtime guards on: integer division/modulo by zero traps with a source location. |
| `--no-runtime-checks` | Disable the guards (no div-by-zero check) for maximum throughput. |
| `--check-leaks`       | Register an at-exit report of ARC objects still live at shutdown. |

### Source-located runtime errors

Class instances are heap-allocated through the ARC runtime, and dangerous
operations are guarded. A failing guard prints the offending
`file:line:col` plus the specific error, then exits with status `70`:

```
$ zanc app.zan -o app && ./app
10
app.zan:8:19: runtime error: division by zero
```

The location is taken from the AST node's `zan_loc_t`, the same mechanism the
compiler front-end uses for compile-time diagnostics.

### Memory-leak detection

`zanc` allocates class instances via `zan_rt_alloc`, which maintains a net
live-object counter (`+1` on alloc, `-1` when a reference count hits zero and
the object is freed). Each object also carries a 16-byte header recording the
**allocation site** (a stable index whose `file:line:col` descriptor is kept in
a side table), so leaks can be attributed to the exact `new` that produced them.

With `--check-leaks`, an `atexit` handler prints the total imbalance followed by
a per-allocation-site breakdown — e.g. an unbroken reference cycle:

```
$ zanc cycle.zan -o cycle --check-leaks && ./cycle
0
zan: memory leak detected: 2 object(s) still reachable at exit
  1 object(s) leaked, allocated at cycle.zan:22:18
  1 object(s) leaked, allocated at cycle.zan:23:18
```

Correct programs whose objects are all released report nothing, so the check
is free of false positives. See `src/self/rt_divzero.zan` and
`src/self/rt_leak.zan` for runnable examples.

## Testing without an editor

Both tools are plain stdio programs, so they can be driven with framed JSON
messages. See `tests/` (or the harnesses referenced in the PR) for example
`initialize` → `didOpen` → `completion` and `setBreakpoints` → `launch` →
`stackTrace` flows.
