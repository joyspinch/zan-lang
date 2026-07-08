# Zan Development Roadmap

## Milestone 1: Minimal Viable Compiler (M1)

**Goal:** Compile and run "Hello World" to native executable.

**Duration:** 2-3 weeks

### Tasks

- [ ] **M1.1 — Project scaffold**
  - CMakeLists.txt with LLVM integration
  - Directory structure (src/compiler, src/runtime, stdlib, tests, examples)
  - CI setup (GitHub Actions: build on Windows/Linux/macOS)

- [ ] **M1.2 — Lexer**
  - Tokenize: keywords, identifiers, operators, delimiters
  - Tokenize: integer literals, float literals, string literals, char literals
  - Handle: comments (single-line, multi-line)
  - Handle: string interpolation ($"...")
  - Error recovery: skip invalid characters, report location
  - Unit tests for all token types

- [ ] **M1.3 — Parser (basic)**
  - Parse: namespace declaration, using declarations
  - Parse: class declaration with methods
  - Parse: method bodies — variable declarations, assignments, function calls
  - Parse: expressions — arithmetic, comparison, string concatenation
  - Parse: control flow — if/else, while, for, return
  - Parse: string interpolation
  - AST printer (debug dump)
  - Unit tests for parser

- [ ] **M1.4 — Binder (basic)**
  - Symbol table with scope chain
  - Resolve: local variables, parameters, function calls
  - Resolve: field access on structs/classes
  - Resolve: namespace-qualified names
  - Report: undeclared identifiers, duplicate declarations

- [ ] **M1.5 — Type checker (basic)**
  - Type check: primitive types (int, float, bool, string, void)
  - Type check: assignments, arithmetic, comparisons
  - Type check: function call argument types
  - Type check: return type matching
  - Type inference: `var` declarations

- [ ] **M1.6 — LLVM IR generation (basic)**
  - Generate: function declarations and calls
  - Generate: local variables (alloca + load/store)
  - Generate: arithmetic and comparison operations
  - Generate: if/else, while, for control flow
  - Generate: string literals (as global constants)
  - Generate: extern function declarations (for Console.WriteLine)
  - Link with minimal runtime (printf wrapper)

- [ ] **M1.7 — Runtime (minimal)**
  - `rt_print_string(const char*)` — print a string
  - `rt_print_int(int64_t)` — print an integer
  - `rt_print_float(double)` — print a float
  - `rt_concat_strings(const char*, const char*)` — string concatenation
  - Link runtime as static library

- [ ] **M1.8 — End-to-end test**
  - Compile and run:
    ```csharp
    using System;
    namespace Hello;
    class Program {
        static void Main() {
            Console.WriteLine("Hello, Zan!");
            int x = 40 + 2;
            Console.WriteLine(x);
        }
    }
    ```

### Deliverable
`zanc` executable that compiles a simple `.zan` file to a native executable.

---

## Milestone 2: Type System (M2)

**Goal:** Support structs, classes, interfaces, generics, ARC.

**Duration:** 3-4 weeks

### Tasks

- [ ] **M2.1 — Struct support**
  - Parse and bind struct declarations
  - Field access and initialization (`Point { X = 1, Y = 2 }`)
  - Methods on structs
  - Stack allocation, value copy semantics
  - LLVM: struct types, GEP for field access

- [ ] **M2.2 — Class support**
  - Parse and bind class declarations
  - Constructor and destructor
  - Heap allocation via `new`
  - Single inheritance (`class Dog : Animal`)
  - Virtual methods and override
  - LLVM: pointer types, vtable generation

- [ ] **M2.3 — ARC implementation**
  - Object header: refcount + type_info
  - `zan_retain()` / `zan_release()` runtime functions
  - Compiler inserts retain/release at assignment, scope exit, parameter pass
  - Destructor called when refcount reaches 0
  - Optimization: elide redundant retain/release pairs
  - Weak references

- [ ] **M2.4 — Interface support**
  - Parse and bind interface declarations
  - Interface conformance checking
  - Interface dispatch (witness table)
  - Multiple interface implementation

- [ ] **M2.5 — Generics**
  - Parse generic type parameters and constraints
  - Monomorphization for value types
  - Type erasure for reference types
  - Generic constraint checking

- [ ] **M2.6 — Enum support**
  - Simple enums (integer-backed)
  - Tagged unions (algebraic types)
  - Pattern matching in switch

- [ ] **M2.7 — Properties**
  - Parse get/set accessors
  - Auto-properties
  - Computed properties

### Deliverable
Complete type system with structs, classes, interfaces, generics, and ARC.

---

## Milestone 3: Standard Library Core (M3)

**Goal:** Usable standard library for basic programs.

**Duration:** 2-3 weeks

### Tasks

- [ ] **M3.1 — System.Console** — formatted I/O
- [ ] **M3.2 — System.String** — string operations, StringBuilder
- [ ] **M3.3 — System.Math** — math functions (wrapping C libm)
- [ ] **M3.4 — System.Collections.List** — dynamic array with COW
- [ ] **M3.5 — System.Collections.Dict** — hash map
- [ ] **M3.6 — System.IO.File** — file read/write
- [ ] **M3.7 — System.IO.Path** — path manipulation
- [ ] **M3.8 — System.Convert** — type conversions

### Deliverable
Standard library sufficient to write file-processing utilities.

---

## Milestone 4: FFI & Native Interop (M4)

**Goal:** Seamless calling of C libraries and system APIs.

**Duration:** 2 weeks

### Tasks

- [ ] **M4.1 — DllImport attribute**
  - Parse [DllImport("lib")] declarations
  - Generate extern function declarations in LLVM IR
  - Dynamic library loading at runtime

- [ ] **M4.2 — Struct layout for C ABI**
  - [repr("C")] attribute
  - Match C struct layout (alignment, padding)
  - Verify with LLVM DataLayout

- [ ] **M4.3 — String marshaling**
  - Managed string → `const char*` (UTF-8)
  - `const char*` → managed string
  - Wide string support for Windows (`wchar_t*`)

- [ ] **M4.4 — Callback support**
  - Managed delegate → C function pointer
  - Proper ARC handling for captured variables

- [ ] **M4.5 — Platform bindings**
  - Platform/Windows.zan — core Win32 API
  - Platform/Posix.zan — core POSIX API

### Deliverable
Programs can call Windows API, POSIX, and third-party C libraries directly.

---

## Milestone 5: Advanced Language Features (M5)

**Goal:** Complete language feature set.

**Duration:** 3-4 weeks

### Tasks

- [ ] **M5.1 — Pattern matching**
  - Type patterns (`x is int n`)
  - Switch expressions
  - Exhaustiveness checking
  - Nested patterns

- [ ] **M5.2 — Lambda & closures**
  - Lambda expression parsing
  - Closure capture (by value for value types, by reference for ref types)
  - Delegate inference

- [ ] **M5.3 — Async/await**
  - Task<T> type
  - State machine transformation
  - Async method generation
  - Simple task scheduler

- [ ] **M5.4 — Exception handling**
  - Try/catch/finally
  - LLVM landingpad / Windows SEH
  - Stack unwinding with ARC cleanup

- [ ] **M5.5 — String interpolation**
  - Parse $"..." expressions
  - Generate StringBuilder calls
  - Format specifiers

- [ ] **M5.6 — Nullable types**
  - T? type wrapper
  - Null coalescing (??)
  - Null conditional (?.)
  - Flow analysis for null safety

- [ ] **M5.7 — Operator overloading**
  - Arithmetic operators
  - Comparison operators
  - Implicit/explicit conversion operators

### Deliverable
Feature-complete language suitable for real-world applications.

---

## Milestone 6: IDE (M6)

**Goal:** Lightweight integrated development environment.

**Duration:** 4-6 weeks

### Tasks

- [ ] **M6.1 — Text editor core**
  - Rope or piece-table text buffer
  - Syntax highlighting (lexer-based)
  - Line numbers, cursor, selection
  - Undo/redo
  - Search and replace
  - Multiple tabs

- [ ] **M6.2 — Rendering**
  - Window management (Win32 / X11 / Cocoa)
  - GPU-accelerated text rendering
  - Font loading (system fonts)
  - Theme support (light/dark)

- [ ] **M6.3 — Project management**
  - Project tree (file browser)
  - Open/close projects
  - New file / new project templates

- [ ] **M6.4 — Build integration**
  - F5 = compile and run
  - Output panel (compiler output, program output)
  - Error list with click-to-navigate
  - Build progress indicator

- [ ] **M6.5 — Code intelligence**
  - Autocomplete (from binder/checker)
  - Go to definition
  - Find references
  - Hover type info
  - Signature help

- [ ] **M6.6 — Debugging**
  - Breakpoints
  - Step over / step into / step out
  - Variable inspection
  - Call stack display
  - Integration with LLVM debugger (LLDB/WinDbg)

### Deliverable
Self-contained IDE bundled with the compiler (single executable).

---

## Milestone 7: Optimization & Polish (M7)

**Goal:** Production-ready compiler and tooling.

**Duration:** Ongoing

### Tasks

- [ ] **M7.1 — Compilation speed**
  - Incremental compilation
  - Parallel compilation (per-module)
  - Precompiled headers for stdlib

- [ ] **M7.2 — Code optimization**
  - Tune LLVM pass pipeline
  - Devirtualization
  - ARC optimization (more aggressive elision)
  - Escape analysis (stack-allocate non-escaping classes)

- [ ] **M7.3 — Standard library expansion**
  - System.Net (HTTP, sockets)
  - System.Threading (threads, async runtime)
  - System.Serialization (JSON, XML)
  - Graphics (2D canvas API)
  - UI (native GUI controls)

- [ ] **M7.4 — Cross-compilation**
  - Compile Windows binaries on Linux and vice versa
  - Bundle target-specific runtime

- [ ] **M7.5 — Package manager (optional)**
  - Simple dependency resolution
  - Source-based packages (git URL + version)
  - Local package cache

- [ ] **M7.6 — Self-hosting** *(in progress)*

  **Goal:** a faithful Zan re-implementation of the C compiler (`src/compiler/`),
  compiling the full language defined in `SPEC.md`, that ultimately compiles its
  own source. This supersedes the earlier staged `src/self/` demonstration suite
  (removed): that suite proved growing language expressiveness on fixed inputs but
  was not a general compiler (per-input logic, hard-coded fixed-size stack buffers,
  everything modelled as `i64`). The self-hosted compiler is a real, general
  compiler with heap-backed growable data structures and located diagnostics.

  **Location:** `src/selfhost/`, mirroring the C module layout —
  `diag / source / token / lexer / ast / parser / binder / checker / irgen /
  driver / main` (`.zan`).

  **Backend:** emit textual LLVM IR; `clang` lowers it to a native executable
  (equivalent to the C compiler driving the LLVM C API).

  **Bootstrap subset:** the compiler's own source is written in a general subset
  (namespaces, classes with methods/fields, `enum`, arrays + a growable `List`,
  `string`, full control flow incl. `switch`, recursion). This subset is enough
  to express a compiler and does not require generics/async/exceptions/lambdas;
  those are still implemented so the compiler *accepts* them per `SPEC.md`.

  **Milestones (by language level, each with acceptance tests):**

  | Level | Scope |
  |-------|-------|
  | L0 | scaffold: `driver` reads a file, parses args, emits an (empty) module; CMake + clang link skeleton |
  | L1 | lexer: full `SPEC.md` token set (keywords, literals, escapes, comments, interpolation) |
  | L2 | parser + AST: declarations, statements, Pratt expressions |
  | L3 | binder + checker: symbol table, scopes, type representation, checking/inference |
  | L4 | IR backend: classes/structs/arrays/methods, minimal ARC, control flow, strings |
  | L5 | self-host closure: output parity with the C `zanc` on the conformance suite → compiles its own source → **gen2 == gen3** byte-identical |

  **Bootstrap chain:** gen0 = C `zanc` (host) → gen1 = gen0 compiling the Zan
  compiler → gen2 = gen1 compiling itself → gen3 = gen2 compiling itself; assert
  gen2 == gen3.

---

## Milestone 8: Language Server Protocol (M8)

**Goal:** IDE-agnostic code intelligence over the Language Server Protocol.

**Status:** ✅ Implemented (`zan-lsp`)

### Tasks

- [x] **M8.1 — Transport**
  - JSON-RPC over stdio with `Content-Length` framing (`src/common/rpc.c`)
  - Dependency-free JSON parser/writer (`src/common/json.c`)

- [x] **M8.2 — Lifecycle**
  - `initialize` (advertises capabilities) / `initialized`
  - `shutdown` / `exit`

- [x] **M8.3 — Document sync & diagnostics**
  - `textDocument/didOpen` / `didChange` (full sync) / `didClose`
  - Runs lexer → parser → binder → checker and publishes
    `textDocument/publishDiagnostics` with severities and ranges

- [x] **M8.4 — Language features**
  - `textDocument/completion` (identifier + `Type.` member context)
  - `textDocument/hover` (symbol signatures)
  - `textDocument/definition`
  - `textDocument/documentSymbol`

### Deliverable
`zan-lsp` executable, usable from any LSP client (VS Code, Neovim, etc.).

---

## Milestone 9: Debugger Integration (M9)

**Goal:** Standard debugging via the Debug Adapter Protocol.

**Status:** ✅ Implemented (`zan-dap`)

### Tasks

- [x] **M9.1 — Transport & lifecycle**
  - DAP over stdio (`initialize`, `launch`, `configurationDone`, `disconnect`)
  - `initialized`, `stopped`, `continued`, `terminated`, `exited`, `output` events

- [x] **M9.2 — Breakpoints**
  - `setBreakpoints` (source breakpoints, verified, optional conditions)

- [x] **M9.3 — Execution control**
  - `continue`, `next`, `stepIn`, `stepOut`, `pause`

- [x] **M9.4 — Inspection**
  - `threads`, `stackTrace`, `scopes`, `variables`

The adapter wraps the IDE debugger engine (`src/ide/debugger.c`). Runtime
process control is delegated to that engine (Windows `CreateProcess`-based;
a simulated stepping model on other platforms).

### Deliverable
`zan-dap` executable, usable from any DAP client.

---

## Summary Timeline

| Milestone | Description | Duration | Cumulative |
|-----------|-------------|----------|------------|
| M1 | Minimal Viable Compiler | 2-3 weeks | Week 3 |
| M2 | Type System (struct, class, ARC, generics) | 3-4 weeks | Week 7 |
| M3 | Standard Library Core | 2-3 weeks | Week 10 |
| M4 | FFI & Native Interop | 2 weeks | Week 12 |
| M5 | Advanced Language Features | 3-4 weeks | Week 16 |
| M6 | Lightweight IDE | 4-6 weeks | Week 22 |
| M7 | Optimization & Polish | Ongoing | — |
| M8 | Language Server Protocol (`zan-lsp`) | done | — |
| M9 | Debugger Integration / DAP (`zan-dap`) | done | — |

---

## Success Criteria

### M1 Complete When:
- `zanc hello.zan -o hello` produces a working native executable
- "Hello, Zan!" prints to stdout

### M2 Complete When:
- Programs using structs, classes, interfaces, generics compile and run correctly
- ARC automatically manages class lifetimes (no manual free, no leaks)

### M3 Complete When:
- Can write a file-processing utility using stdlib (read file, process, write output)

### M4 Complete When:
- Can call Windows MessageBox from Zan code
- Can call POSIX open/read/write from Zan code

### M5 Complete When:
- All language features from SPEC.md are implemented
- Test suite passes with >95% coverage of language constructs

### M6 Complete When:
- IDE launches, opens a project, compiles with F5, shows errors inline
- Single executable under 20MB

### Self-hosting Complete When:
- Zan compiler written in Zan compiles itself
- gen2 and gen3 produce identical output

