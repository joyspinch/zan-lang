# Zan Project Structure Specification

## 1. Repository Layout

```
zan-lang/
│
├── docs/                               # Documentation
│   ├── SPEC.md                         # Language specification
│   ├── ARCHITECTURE.md                 # Compiler architecture
│   ├── STDLIB.md                       # Standard library design
│   ├── PROJECT_STRUCTURE.md            # This document
│   ├── ABI.md, CONCURRENCY.md, SECURITY.md, PERFORMANCE.md,
│   │   CODING_STANDARDS.md, TOOLING.md, WORKSPACE_CONVENTIONS.md,
│   │   BOOTSTRAP.md, RELEASE.md, ...   # (see the docs/ directory itself)
│
├── src/                                # All source code
│   ├── compiler/                       # Compiler (C11)
│   │   ├── CMakeLists.txt
│   │   ├── main.c                      # Entry point (CLI driver: stdlib glob,
│   │   │                               #   driver discovery, --publish, packages)
│   │   ├── zan.h                       # Common types and forward decls
│   │   ├── arena.h / arena.c           # Arena allocator
│   │   ├── diag.h / diag.c             # Diagnostics (errors/warnings)
│   │   ├── lexer.h / lexer.c / token.h # Tokenizer
│   │   ├── ast.h / ast.c               # AST node types and utilities
│   │   ├── parser.h / parser.c         # Recursive descent parser
│   │   ├── binder.h / binder.c         # Name resolution / symbol table
│   │   ├── symbols.h / symbols.c       # Symbol table
│   │   ├── checker.h / checker.c       # Type checker
│   │   ├── nsresolve.h / nsresolve.c   # Namespace resolution
│   │   ├── irgen.h / irgen.c           # IR generation driver
│   │   │   ├── irgen_abi.c             #   calling-convention / ABI lowering
│   │   │   ├── irgen_arc.c             #   ARC retain/release insertion
│   │   │   ├── irgen_async.c           #   async / coroutine lowering
│   │   │   ├── irgen_builtins.c        #   builtin type/member lowering
│   │   │   ├── irgen_call.c            #   call lowering
│   │   │   ├── irgen_emit.c            #   IR emission
│   │   │   ├── irgen_expr.c / irgen_expr_core.c
│   │   │   ├── irgen_generics.c        #   generic instantiation
│   │   │   └── irgen_stmt.c            #   statement lowering
│   │   ├── builtin_api.h / builtin_api.c  # Builtin types/members (Console,
│   │   │                               #   Math, List<T>, File, Environment, ...)
│   │   ├── stdlib_ext.h / stdlib_ext.c # Stdlib integration hooks
│   │   ├── optimizer.h / optimizer.c   # IR optimizations
│   │   ├── crosscomp.h / crosscomp.c   # Cross-compilation support
│   │   ├── genmeta.h / genmeta.c       # Metadata generation
│   │   ├── genrun.h / genrun.c         # Runtime/link selection
│   │   ├── incremental.h / incremental.c  # Incremental compilation
│   │   ├── package.h / package.c       # zan.proj / zan.lock / .zan-packages
│   │   └── winres.h / winres.c         # Windows resource embedding
│   │
│   ├── runtime/                        # Runtime library (C11)
│   │   ├── rt_co.c / rt_co.h           # Coroutine / fiber core
│   │   ├── rt_io.c / rt_io.h           # Async socket reactor (epoll/kqueue/IOCP)
│   │   ├── rt_mem.c                    # Memory allocator / ARC support
│   │   ├── rt_sched.c / rt_sched.h     # Task scheduler
│   │   ├── rt_sync.c / rt_sync.h       # Atomic / shared-memory primitives
│   │   ├── rt_timer.c / rt_timer.h     # Unified timer runtime
│   │   ├── rt_wasm.c                   # WebAssembly backend support
│   │   ├── zan_embed_api.c             # Embedding API
│   │   └── gui_runtime.c, gui_runtime_font.c, gui_runtime_text.c,
│   │       gui_runtime_tray.c, gui_runtime_x11.c, gui_runtime_sdl.c,
│   │       gui_runtime_mac.m, gui_runtime_shims.c   # GUI backends
│   │
│   ├── lsp/                            # Language Server (LSP over stdio)
│   │   ├── lsp_main.c                  # LSP server entry point
│   │   └── intellisense.c/h            # Completion / analysis engine
│   │
│   ├── dap/                            # Debug Adapter (DAP over stdio)
│   │   ├── dap_main.c                  # DAP server entry point
│   │   └── debugger.c/h                # Debugger engine
│   │
│   ├── ide_zan/                        # IDE — self-hosted, written in Zan
│   │   ├── ZanIDE.zan                  # IDE application (compiled by zanc)
│   │   ├── zan.proj                    # Project manifest (name/type/target/entry)
│   │   └── components/                 # Reusable Zan UI components
│   │
│   ├── doc/                            # Documentation generator
│   │   └── zandoc.zan                  # zandoc tool (built to build/zandoc.exe)
│   ├── fmt/                            # Formatter
│   │   └── zanfmt.zan                  # zanfmt tool (built to build/zanfmt.exe)
│   ├── common/                         # Shared C utilities (json.c/h, rpc.c/h)
│   └── selfhost/                       # Self-hosted compiler sources (.zan)
│
├── stdlib/                             # Standard library (Zan source; see docs/STDLIB.md)
│   ├── System/                         # Console/Math/List/Dict/... are compiler
│   │   │                               #   builtins, not files
│   │   ├── ConsoleColor.zan, DateTime.zan, Guid.zan, Random.zan,
│   │   │   TimeSpan.zan, Interop.zan, NativeMemory.zan, ...
│   │   ├── IO/  Collections/  Text/  Net/  Threading/
│   │   ├── Diagnostics/  Json/  Linq/  ...
│   │   └── (Automation, Data/, Drawing/, Scripting/, Security/, ...)
│   ├── Gui/                            # Self-hosted GUI framework (Zan source)
│   │   ├── App.zan, Types.zan, Theme.zan, Reactive.zan, Layout.zan,
│   │   │   Render.zan, Event.zan, Text.zan, Icon.zan, Css.zan, ...
│   │   ├── Backend/                    # Native.zan, UiDriver.zan, Win32Shell.zan
│   │   ├── Widget/                     # 60 controls: Button, Input, Label,
│   │   │                               #   SelectBox, TreeView, Table, Tabs,
│   │   │                               #   Prompt, Layer, Result, ...
│   │   ├── Component/                  # Chart, CodeEditor, DataTable, WebView, ...
│   │   ├── Designer/  Hmi/
│   │   ├── drivers/                    # driver.manifest + win-x64/... (zan_gui.dll)
│   │   └── skins/                      # CSS theme skins (light, dark, ...)
│   ├── Game/  Sdk/                     # Game framework, SDK integrations
│   ├── SDL3/                           # SDL3 bindings (+ drivers/ with SDL3.dll)
│   └── Platform/
│       └── Runtime.zan
│
├── tests/                              # Test suite (see §2.3)
│   ├── conformance/                    # .zan + .out pairs (compile, run, compare)
│   ├── diag/                           # Expected compile diagnostics
│   ├── abi/  runtime/  gui/  ide/  lsp/  dap/    # Per-area runtime tests
│   ├── emit_lib/  fmt/  doc/  gen/  golden/      # Emission/tool/golden cases
│   ├── fuzz/  leakprobe/  selfhost/  unit/       # Fuzz, leak checks,
│   │                                           #   self-hosting, C unit tests
│   └── run_*.cmake                     # Test drivers (ctest -L smoke|standard|full)
│
├── examples/                           # Example programs (one topic dir each)
│   ├── audio/  db/  game/  gamepad/  gpu/
│   ├── gui_browser/  gui_charts/  gui_gallery/  input/
│   └── lua/  net/  python/  sdk/  touch/
│
├── tools/                              # Helper tools (written in Zan)
│   ├── mcp_server/                     # MCP server
│   ├── mge/                            # Mge tool
│   └── repomap/                        # RepoMap tool
│
├── templates/                          # IDE "New Project" templates
│   ├── console/  gui/  library/  server/
│   └── (each: template.manifest + src/)
│
├── toolchain/                          # Cross-compilation toolchains
│   ├── win-x64/  win-arm64/  linux-x64/  linux-arm64/  linux-musl/
│   └── linux-riscv64/  macos/  wasm32/
│
├── assets/                             # Build resources (zan.ico, zan.rc)
│
├── cmake/  scripts/                    # Build system + maintained scripts
├── AGENTS.md  TASKS.md  VERSION
├── CMakeLists.txt                      # Top-level build file
├── README.md
├── LICENSE
├── .gitignore
└── build/  dist/                       # Out-of-source output (git-ignored)
```

---

## 2. File Naming Conventions

### 2.1 Compiler Source (C11)

| Type | Convention | Example |
|------|-----------|---------|
| Header | `module_name.h` | `lexer.h`, `parser.h` |
| Source | `module_name.c` | `lexer.c`, `parser.c` |
| Common header | `zan.h` | Shared types/macros |
| Token definitions | `token.h` | Token kind enum |

**Rules:**
- One `.h` + one `.c` per module
- No file exceeds 2000 lines (split into sub-modules if needed)
- Header guards: `#ifndef ZAN_MODULE_H` / `#define ZAN_MODULE_H`

### 2.2 Standard Library (Zan)

| Type | Convention | Example |
|------|-----------|---------|
| Module file | `PascalCase.zan` | `ConsoleColor.zan`, `HttpClient.zan` |
| Namespace dir | `PascalCase/` | `System/IO/`, `Gui/Widget/` |
| Native drivers | `drivers/<platform>/` + `driver.manifest` | `Gui/drivers/win-x64/` |

**Rules:**
- One public type per file (primary type matches filename)
- A namespace is a directory — there are **no `mod.zan` entry points** (see
  §5: `using` globs every `*.zan` in the directory)
- Native binaries live in a `drivers/` subdirectory of the owning module, one
  subdir per target (`win-x64`, `linux-x64`, ...), listed in
  `driver.manifest` and bundled on `--publish` — **no `native/` subdirectory
  convention**

### 2.3 Tests

| Type | Convention | Example |
|------|-----------|---------|
| Conformance case | `snake_case.zan` + `snake_case.out` | `tests/conformance/accept_after_close.zan` / `.out` |
| Diagnostics case | `.zan` case with expected compile error | `tests/diag/` |
| Golden case | `cases/` + `expected/` directories | `tests/golden/` |
| C unit test | `snake_case_test.c` | `tests/unit/json_oom_test.c` |
| Driver | `run_*.cmake` (registered with `ctest -L smoke\|standard\|full`) | `run_case.cmake`, `run_zandoc.cmake` |

There is **no `// TEST:` comment header**: a conformance case is simply a
`.zan` program paired with a `.out` file holding its expected stdout; the test
driver compiles, runs and compares the output.

---

## 3. Build Artifacts

```
build/                                  # Out-of-source build dir (single level)
├── zanc.exe                            # Compiler executable
├── zan-lsp / zan-dap                   # Language server, debug adapter
├── zanfmt.exe                          # Formatter (built from src/fmt/zanfmt.zan)
├── zandoc.exe                          # Doc generator (built from src/doc/zandoc.zan)
├── ZanIDE.exe                          # IDE executable (from src/ide_zan/ZanIDE.zan)
├── zanrt_io.o / zanrt_io_mt.o          # Socket-async reactor objects (epoll/kqueue/IOCP)
├── zanrt_timer.o                       # Timer runtime object
├── zanrt_sync.o / zanrt_mem.o          # Sync / memory runtime objects
├── zanrt_wasm.o                        # Wasm backend object
└── ...                                 # test binaries, CMake files
```

**Rules:**
- Never commit build artifacts (`build/` and `dist/` are git-ignored)
- There is **no `Debug/` / `Release/` double-level layout** — one flat
  `build/` directory, and `zanc.exe` sits at its root
- There is **no `libzan_rt.a`**: the runtime is a set of small objects
  (`zanrt_io.o`, `zanrt_timer.o`, ...) that `zanc` links per program, adding
  only the ones the program needs
- `zanfmt` and `zandoc` are compiled from their Zan sources by `zanc` itself
  during the build (see `CMakeLists.txt`)
- Debug/optimized builds are selected per invocation (`-g`/`-O0`..`-O3`,
  `--publish`), not by choosing a build subdirectory

---

## 4. User Project Layout

When a Zan user creates a project (via the IDE "New Project" flow or by hand,
using a template from `templates/`):

```
my_app/
├── zan.proj                         # Project manifest (key = value)
├── src/
│   └── main.zan                     # Entry point (matches `entry`)
├── assets/                          # Non-code resources
│   ├── icon.png
│   └── config.json
├── .zan-packages/                   # Installed package cache (gitignored)
└── zan.lock                         # Resolved dependency versions
```

**zan.proj** is a flat `key = value` manifest (see
`src/ide_zan/zan.proj`):

```
name = MyApp
type = console              # console | gui | library | server
target = exe
entry = src/main.zan
assets = icon.png,config.json
```

**Dependencies.** There is no `dependencies { path = ... }` block. Packages
are installed with `zanc`'s package flags (`--package-install <dir>`,
`--package-name`, `--package-project`, `--package-scope project|global`) or
through the IDE, cached under `.zan-packages/`, and recorded in `zan.lock`
under a `[deps]` / `[dependencies]` section as `name{key=value}` entries. The
compiler resolves `using` namespaces against the stdlib and these installed
packages (see §5).

**Templates.** `templates/<kind>/` holds the IDE's project templates; each is
a `template.manifest` (key = value: `name`, `type`, `target`, `entry`, ...)
plus a `src/` directory that gets copied into the new project.

---

## 5. Module Search Order

When resolving `using System.IO`, the compiler (`auto_include_namespace` /
`glob_stdlib_dir` in `src/compiler/main.c`) auto-includes **every `*.zan`
file directly under** `stdlib/System/IO/` — the namespace maps to a directory
glob, so there is no per-file lookup and no `<project_root>/lib/` override:

```
1. <stdlib_path>/System/IO/*.zan                # standard library (one level)
2. <package>/System/IO/*.zan                    # installed packages (.zan-packages/)
```

`using System;` imports the compiler/runtime core names (builtin types) and
never triggers a package lookup. Subdirectories are separate namespaces and
need their own `using` (e.g. `using System.Net.Http;` includes
`stdlib/System/Net/Http/*.zan`, not the `Client/` subdirectory).

---

## 6. Third-Party / External Dependencies

**Zero external rendering dependencies.** The GUI is pure software rendering
+ OS APIs; no Skia, no Qt.

```
External dependencies:
├── LLVM 17+                # System-installed (not vendored)
│                           # Used by compiler for IR → machine code
├── SDL3 (optional)         # stdlib/SDL3 bindings; SDL3.dll ships as a
│                           # driver in stdlib/SDL3/drivers/ (zan_sdl3)
└── OS APIs                 # primary GUI driver (no Skia, no Qt)
    ├── Win32: user32, gdi32, ole32, shlwapi (window + GDI text + SetDIBitsToDevice)
    ├── Linux: X11 / Wayland + fontconfig + freetype (window + text)
    └── macOS: Cocoa + CoreText (window + text)
```

**Rules:**
- LLVM is the ONLY significant external dependency (system-installed)
- GUI rendering is pure software pixel manipulation — no rendering library needed
- Text rendering uses OS-native APIs (GDI DrawTextW on Windows, FreeType on Linux, CoreText on macOS)
- Window management uses OS-native APIs (Win32, X11/Wayland, Cocoa)
- Optional embedded browser: WebView2 on Windows (`stdlib/Gui/Component/WebView`), with a native macOS backend; other platforms return no webview
