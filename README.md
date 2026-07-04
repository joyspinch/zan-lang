# Zan Programming Language

Modern systems programming language with **C# syntax**, **LLVM backend**, and **ARC memory management**.

## Features

- **Familiar syntax** — C#/Java style, no cryptic symbols or lifetime annotations
- **AOT compilation** — compiles directly to native machine code via LLVM
- **ARC memory** — automatic reference counting with deterministic destruction
- **Value semantics** — structs on stack, copy-on-write collections
- **Easy FFI** — direct DllImport for system APIs and native libraries
- **Lightweight IDE** — self-contained development environment (planned)
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
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Requires LLVM 17+ with development libraries.

## Project Structure

```
src/
├── compiler/          # Compiler sources (C11)
│   ├── lexer.c/h      # Tokenizer
│   ├── parser.c/h     # Recursive descent parser
│   ├── ast.c/h        # AST node definitions
│   ├── binder.c/h     # Name resolution & symbol table
│   ├── checker.c/h    # Type checking & inference
│   ├── irgen.c/h      # LLVM IR generation
│   ├── diag.c/h       # Error/warning reporting
│   └── driver.c/h     # Compilation pipeline
├── runtime/           # Runtime library
└── ide/               # Integrated development environment
stdlib/                # Standard library (.zan source)
tests/                 # Test suite
examples/              # Example programs
docs/                  # Documentation
```

## License

MIT

