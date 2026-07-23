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
| Document sync (full)             | `didOpen` / `didChange` / `didClose` / `didSave` |
| Autocomplete                     | `textDocument/completion` (with resolve) |
| Hover type info                  | `textDocument/hover`            |
| Signature help                   | `textDocument/signatureHelp`    |
| Go to definition                 | `textDocument/definition`       |
| Find references                  | `textDocument/references`       |
| Document symbols (outline)       | `textDocument/documentSymbol`   |
| Code actions / quick fixes       | `textDocument/codeAction`       |
| Commands                         | `workspace/executeCommand`      |

Not yet implemented: `rename`, `workspace/symbol`, semantic tokens, inlay
hints, incremental document sync.

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
`setBreakpoints` (conditional / hit-count / logpoints), `setExceptionBreakpoints`,
function breakpoints, `threads`, `stackTrace`, `scopes`, `variables`,
`setVariable`, `evaluate` (watch / hover), `exceptionInfo`, `continue`, `next`,
`stepIn`, `stepOut`, `pause`, `terminate`, `disconnect`. Emits `initialized`,
`stopped`, `terminated`, `exited`, and `output` events.

Runtime process control is delegated to the debugger engine
(`src/dap/debugger.c`), which drives a real native debug session through
**gdb** — resolved first as the bundled `toolchain\debugger\bin\gdb.exe`
shipped by `scripts/publish_ide.ps1`, then `ZAN_GDB`, then a system gdb —
while `zan-dap` provides the protocol surface.

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
is free of false positives. See `tests/leakprobe/` and `tests/runtime/` for
runnable examples.

## Test suite & deterministic codegen

`ctest` runs three regression families over every build configuration:

- **conformance** — each `tests/conformance/*.zan` is compiled, run, and its
  stdout diffed against a golden `.out`.
- **selfhost** — the C host compiles the entire self-hosted compiler
  (`src/selfhost/*.zan`) into gen1, which then lowers a real program
  (`tests/selfhost/prog1.zan`) to LLVM IR (`tests/run_selfhost.cmake`).
- **determinism** — every conformance and self-host program is compiled to LLVM
  IR *twice*, and the two emissions must be byte-for-byte identical.

The determinism check (`tests/run_determinism.cmake`) is the reproducible-build
correctness net: because `zanc` is written in C rather than in Zan, a classic
self-hosting `gen2 == gen3` diff does not apply, so instead we assert that
codegen is a pure function of the source. It catches nondeterminism and
undefined behaviour in the backend — iteration over unordered containers,
uninitialised memory, or raw pointer values leaking into the output — any of
which would otherwise silently break reproducible builds.

```
$ ctest --test-dir build --output-on-failure
...
100% tests passed, 0 tests failed out of 47
```

## Testing without an editor

Both tools are plain stdio programs, so they can be driven with framed JSON
messages. See `tests/` (or the harnesses referenced in the PR) for example
`initialize` → `didOpen` → `completion` and `setBreakpoints` → `launch` →
`stackTrace` flows.
