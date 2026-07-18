# Zan Project Structure Specification

## 1. Repository Layout

```
zan-lang/
│
├── docs/                               # All project documentation
│   ├── SPEC.md                         # Language specification
│   ├── ARCHITECTURE.md                 # Compiler architecture
│   ├── STDLIB.md                       # Standard library design
│   ├── CONCURRENCY.md                  # Concurrency model
│   ├── SECURITY.md                     # Security specification
│   ├── PERFORMANCE.md                  # Performance targets
│   ├── CODING_STANDARDS.md             # Development standards
│   ├── IDE.md                          # IDE architecture
│   ├── ABI.md                          # Binary interface spec
│   ├── ERROR_CATALOG.md                # Compiler error codes
│   ├── PROJECT_STRUCTURE.md            # This document
│   ├── ROADMAP.md                      # Development milestones
│   └── DESIGN.md                       # High-level overview
│
├── src/                                # All source code
│   ├── compiler/                       # Compiler (C11)
│   │   ├── CMakeLists.txt
│   │   ├── main.c                      # Entry point (CLI driver)
│   │   ├── zan.h                       # Common types and forward decls
│   │   ├── arena.h / arena.c           # Arena allocator
│   │   ├── intern.h / intern.c         # String interning
│   │   ├── diag.h / diag.c             # Diagnostics (errors/warnings)
│   │   ├── source.h / source.c         # Source file management
│   │   ├── lexer.h / lexer.c           # Tokenizer
│   │   ├── token.h                     # Token kinds enum
│   │   ├── ast.h / ast.c               # AST node types and utilities
│   │   ├── parser.h / parser.c         # Recursive descent parser
│   │   ├── binder.h / binder.c         # Name resolution / symbol table
│   │   ├── checker.h / checker.c       # Type checker
│   │   ├── irgen.h / irgen.c           # LLVM IR generation
│   │   ├── arc.h / arc.c               # ARC insertion pass
│   │   └── driver.h / driver.c         # Compilation orchestration
│   │
│   ├── runtime/                        # Runtime library (C11)
│   │   ├── CMakeLists.txt
│   │   ├── rt.h                        # Runtime public API
│   │   ├── rt_arc.c                    # ARC retain/release
│   │   ├── rt_string.c                 # String operations
│   │   ├── rt_array.c                  # Dynamic array (COW)
│   │   ├── rt_dict.c                   # Hash map
│   │   ├── rt_io.c                     # File I/O wrappers
│   │   ├── rt_console.c               # Console I/O
│   │   ├── rt_math.c                   # Math functions (libm wrapper)
│   │   ├── rt_task.c                   # Task scheduler
│   │   ├── rt_channel.c               # Typed channels
│   │   └── rt_panic.c                  # Panic handler (OOB, overflow)
│   │
│   ├── lsp/                            # Language Server (LSP over stdio)
│   │   ├── lsp_main.c                  # LSP server entry point
│   │   └── intellisense.c/h            # Completion / analysis engine
│   │
│   ├── dap/                            # Debug Adapter (DAP over stdio)
│   │   ├── dap_main.c                  # DAP server entry point
│   │   └── debugger.c/h                # Debugger engine
│   │
│   └── ide_zan/                        # IDE — self-hosted, written in Zan
│       ├── ZanIDE.zan                  # IDE application (compiled by zanc)
│       └── components/                 # Reusable Zan UI components
│
├── stdlib/                             # Standard library (Zan source)
│   ├── System/
│   │   ├── Console.zan
│   │   ├── Math.zan
│   │   ├── String.zan
│   │   ├── Convert.zan
│   │   ├── DateTime.zan
│   │   ├── Environment.zan
│   │   ├── IO/
│   │   │   ├── File.zan
│   │   │   ├── Directory.zan
│   │   │   ├── Path.zan
│   │   │   ├── Stream.zan
│   │   │   └── native/
│   │   │       ├── win/
│   │   │       │   └── fileapi.zan
│   │   │       └── unix/
│   │   │           └── unistd.zan
│   │   ├── Collections/
│   │   │   ├── List.zan
│   │   │   ├── Dict.zan
│   │   │   ├── Set.zan
│   │   │   ├── Queue.zan
│   │   │   └── Stack.zan
│   │   ├── Text/
│   │   │   ├── Encoding.zan
│   │   │   ├── StringBuilder.zan
│   │   │   └── Regex.zan
│   │   ├── Net/
│   │   │   ├── Http.zan
│   │   │   ├── Socket.zan
│   │   │   └── native/
│   │   │       ├── win/ ...
│   │   │       └── unix/ ...
│   │   ├── Threading/
│   │   │   ├── Task.zan
│   │   │   ├── Channel.zan
│   │   │   ├── Mutex.zan
│   │   │   └── Atomic.zan
│   │   ├── Runtime/
│   │   │   ├── ARC.zan
│   │   │   ├── Memory.zan
│   │   │   └── Platform.zan
│   │   └── Serialization/
│   │       ├── Json.zan
│   │       └── Xml.zan
│   │
│   ├── GUI/                            # GUI standard library
│   │   ├── App.zan                     # Application lifecycle, event loop
│   │   ├── Types.zan                   # Rect, Point, Size, Color
│   │   ├── Theme.zan                   # NaiveUI design tokens (light/dark)
│   │   ├── Reactive.zan                # Signal<T>, Computed, Effect (Vue-style)
│   │   ├── Layout.zan                  # CSS Flexbox layout engine (pure Zan)
│   │   ├── Render.zan                  # 2D software rendering (像素级绘制)
│   │   ├── Event.zan                   # Hit test, focus, event dispatch
│   │   ├── Text.zan                    # Text editing (cursor, selection, IME, undo)
│   │   ├── Animation.zan              # Easing, transitions, springs
│   │   ├── WebView.zan                 # Embedded browser (WebView2/WebKitGTK)
│   │   ├── Widget/                     # NaiveUI-style controls (40+ components)
│   │   │   ├── Button.zan
│   │   │   ├── Input.zan
│   │   │   ├── Label.zan
│   │   │   ├── Select.zan
│   │   │   ├── Table.zan
│   │   │   ├── Tabs.zan
│   │   │   ├── Modal.zan
│   │   │   ├── Checkbox.zan
│   │   │   ├── Switch.zan
│   │   │   ├── Card.zan
│   │   │   ├── Menu.zan
│   │   │   ├── Tree.zan
│   │   │   ├── Icon.zan
│   │   │   ├── Scrollbar.zan
│   │   │   ├── Ribbon.zan
│   │   │   └── ...                     # 40+ components
│   │   └── native/                     # 平台窗口和像素显示（仅 OS API）
│   │       ├── gui_runtime.c           # 软件渲染核心 + 窗口管理
│   │       ├── win/
│   │       │   ├── window.zan          # Win32 窗口/消息循环
│   │       │   ├── render.zan          # GDI SetDIBitsToDevice 显示
│   │       │   └── webview2.zan        # WebView2 COM bindings
│   │       ├── linux/
│   │       │   ├── x11.zan             # X11 bindings
│   │       │   └── wayland.zan         # Wayland bindings
│   │       └── macos/
│   │           └── cocoa.zan           # Cocoa bindings
│   │
│   └── Platform/
│       ├── Windows.zan
│       ├── Posix.zan
│       └── Darwin.zan
│
├── tests/                              # Test suite
│   ├── lexer/                          # Lexer unit tests
│   │   ├── keywords.zan
│   │   ├── literals.zan
│   │   ├── operators.zan
│   │   └── strings.zan
│   ├── parser/                         # Parser snapshot tests
│   │   ├── class_decl.zan
│   │   ├── expressions.zan
│   │   ├── control_flow.zan
│   │   └── generics.zan
│   ├── checker/                        # Type checker tests
│   │   ├── type_errors.zan
│   │   ├── inference.zan
│   │   └── generics.zan
│   ├── codegen/                        # Code generation tests
│   │   ├── arithmetic.zan
│   │   ├── control_flow.zan
│   │   └── classes.zan
│   ├── integration/                    # End-to-end compile-and-run
│   │   ├── hello_world.zan
│   │   ├── fibonacci.zan
│   │   ├── classes.zan
│   │   └── concurrency.zan
│   ├── negative/                       # Expected errors
│   │   ├── undeclared_var.zan
│   │   ├── type_mismatch.zan
│   │   └── null_safety.zan
│   ├── benchmarks/                     # Performance benchmarks
│   │   ├── fibonacci.zan
│   │   ├── mandelbrot.zan
│   │   └── binary_trees.zan
│   └── snapshots/                      # Golden file outputs
│       ├── parser/
│       └── checker/
│
├── examples/                           # Example programs
│   ├── hello.zan
│   ├── classes.zan
│   ├── collections.zan
│   ├── ffi_demo.zan
│   ├── echo_server.zan
│   └── gui_demo.zan
│
├── tools/                              # Build and development tools
│   ├── test_runner.py                  # Test harness
│   └── gen_tokens.py                   # Generate token.h from spec
│
├── third_party/                        # Vendored dependencies (if any)
│   └── README.md                       # Notes on external deps
│
├── CMakeLists.txt                      # Top-level build file
├── README.md
├── LICENSE
├── .gitignore
└── .github/
    └── workflows/
        └── ci.yml                      # CI pipeline
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
| Module file | `PascalCase.zan` | `Console.zan`, `HttpClient.zan` |
| Sub-module dir | `PascalCase/` | `Collections/`, `IO/` |
| Native bindings | `native/platform/` | `native/win/`, `native/unix/` |

**Rules:**
- One public type per file (primary type matches filename)
- `mod.zan` as namespace entry point (if directory-based)
- Native bindings always in `native/` subdirectory

### 2.3 Tests

| Type | Convention | Example |
|------|-----------|---------|
| Test file | `snake_case.zan` | `class_decl.zan` |
| Test directory | maps to compiler module | `tests/parser/` |
| Snapshot | `.expected` suffix | `class_decl.expected` |
| Benchmark | descriptive name | `fibonacci.zan` |

**Test file header:**
```csharp
// TEST: descriptive test name
// EXPECT: expected stdout (for integration tests)
// ERROR: expected error message (for negative tests)
// SKIP: platform_name (if platform-specific)
```

---

## 3. Build Artifacts

```
build/                                  # Out-of-source build dir
├── Debug/
│   ├── bin/
│   │   ├── zanc                        # Compiler executable
│   │   ├── zan-lsp                     # Language server
│   │   ├── zan-dap                     # Debug adapter
│   │   └── ZanIDE                      # IDE executable (from src/ide_zan)
│   └── lib/
│       └── libzan_rt.a                 # Runtime static library
└── Release/
    ├── bin/
    │   ├── zanc
    │   ├── zan-lsp
    │   ├── zan-dap
    │   └── ZanIDE
    └── lib/
        └── libzan_rt.a
```

**Rules:**
- Never commit build artifacts
- Build directory is always `build/` (in `.gitignore`)
- Debug builds include debug info and sanitizers
- Release builds are optimized

---

## 4. User Project Layout

When a Zan user creates a project:

```
my_app/
├── project.zan                         # Project configuration
├── src/
│   └── main.zan                        # Entry point
├── lib/                                # Local library overrides
│   └── MyLib/
│       └── Utils.zan
├── assets/                             # Non-code resources
│   ├── icon.png
│   └── config.json
└── build/                              # Compiler output (gitignored)
    └── my_app.exe
```

**project.zan:**
```
project MyApp {
    version = "1.0.0"
    target = "executable"
    entry = "src/main.zan"

    dependencies {
        // Source-based dependency
        some_lib {
            path = "lib/SomeLib"
        }
    }

    build {
        optimize = "release"        // debug | release | size
        target_os = "windows"       // windows | linux | macos | auto
        target_arch = "x64"         // x64 | arm64 | auto
    }
}
```

---

## 5. Module Search Order

When resolving `using System.IO.File`:

```
1. <project_root>/lib/System/IO/File.zan        # Project-local override
2. <project_root>/lib/System/IO/File/mod.zan     # Directory override
3. <stdlib_path>/System/IO/File.zan              # Standard library
4. <stdlib_path>/System/IO/File/mod.zan          # Directory module
```

**Priority: project lib → stdlib → error**

---

## 6. Third-Party / External Dependencies

**Zero external rendering dependencies.** GUI uses pure software rendering + OS APIs only.

```
External dependencies:
├── LLVM 17+                # System-installed (not vendored)
│                           # Used by compiler for IR → machine code
└── OS APIs only            # No Skia, no Qt, no SDL
    ├── Win32: user32, gdi32, ole32, shlwapi (window + GDI text + SetDIBitsToDevice)
    ├── Linux: X11 / Wayland + fontconfig + freetype (window + text)
    └── macOS: Cocoa + CoreText (window + text)
```

**Rules:**
- LLVM is the ONLY significant external dependency (system-installed)
- GUI rendering is pure software pixel manipulation — no rendering library needed
- Text rendering uses OS-native APIs (GDI DrawTextW on Windows, FreeType on Linux, CoreText on macOS)
- Window management uses OS-native APIs (Win32, X11/Wayland, Cocoa)
- Optional: WebView2 (Windows) / WebKitGTK (Linux) for embedded browser component
