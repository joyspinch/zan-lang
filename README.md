# Zan Programming Language

Modern systems programming language with **C# syntax**, **LLVM backend**, and **ARC memory management**.

## Features

- **Familiar syntax** — C#/Java style, no cryptic symbols or lifetime annotations
- **AOT compilation** — compiles directly to native machine code via LLVM
- **ARC memory** — automatic reference counting with deterministic destruction
- **Value semantics** — structs on stack, copy-on-write collections
- **Easy FFI** — direct DllImport for system APIs and native libraries
- **Lightweight IDE** — self-hosted development environment written in Zan with integrated visual designer, LSP, and DAP
- **Source-based stdlib** — standard library distributed as .zan source files

## Quick Example

```csharp
using System;

namespace HelloWorld;

struct Point {
    public float X;
    public float Y;

    public float Length() => Math.Sqrt(X * X + Y * Y);
}

class Program {
    static void Main(string[] args) {
        var p = Point { X = 3.0, Y = 4.0 };
        Console.WriteLine($"Point: ({p.X}, {p.Y}), Length: {p.Length()}");
    }
}
```

## Building

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Requires LLVM 17+ with development libraries.

## Project Structure

```
src/
├── compiler/          # Compiler front/back end (C11: lexer, parser, binder, checker, irgen)
├── runtime/           # Runtime library (C11: ARC, async reactor, scheduler, gui backends)
├── lsp/               # Language server (zan-lsp) & IntelliSense engine
├── dap/               # Debug adapter (zan-dap) & debugger engine
├── ide_zan/           # Integrated development environment (self-hosted, written in Zan)
├── common/            # Shared C utilities (json, rpc)
└── selfhost/          # Self-hosted compiler sources (.zan)
stdlib/                # Standard library (.zan source + native driver bundles)
templates/             # Project wizard templates (console, gui, game, server, library)
examples/              # Curated, runnable example programs (each with its own README)
tests/                 # Comprehensive test suite (conformance, abi, runtime, gui, lsp, dap)
docs/                  # Language specifications & architectural documentation
```

## License

MIT

