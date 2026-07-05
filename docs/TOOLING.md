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

## Testing without an editor

Both tools are plain stdio programs, so they can be driven with framed JSON
messages. See `tests/` (or the harnesses referenced in the PR) for example
`initialize` → `didOpen` → `completion` and `setBreakpoints` → `launch` →
`stackTrace` flows.
