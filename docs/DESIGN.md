# Zan V2 — Language & Compiler Design

## Vision

A modern systems programming language with C# syntax, compiling directly to native machine code via LLVM. Combines the safety of Swift/C# with the performance of C/C++, and the developer experience of aardio.

**Core principles:**
- C# familiar syntax — no cryptic symbols, no lifetime annotations
- AOT compilation to native machine code (via LLVM)
- ARC (Automatic Reference Counting) for deterministic memory management
- Lightweight self-contained IDE
- Single-file portable deployment
- Fast compilation, instant feedback
- Easy system API / DLL interop

---

## 1. Language Syntax (C# Style)

### 1.1 Basic Types

```csharp
// Value types (stack allocated, zero overhead)
int x = 42;              // 64-bit integer
float y = 3.14;          // 64-bit float
bool ok = true;
char ch = 'A';           // Unicode char
string name = "hello";   // Immutable string (interned)

// Nullable
int? maybe = null;
string? text = null;
```

### 1.2 Functions

```csharp
int Add(int a, int b) {
    return a + b;
}

// Expression body
int Square(int x) => x * x;

// Multiple returns via tuples
(int, string) GetInfo() => (42, "answer");

// Default parameters
void Log(string msg, int level = 0) { ... }
```

### 1.3 Structs (Value Types)

```csharp
struct Point {
    public float X;
    public float Y;
    
    public float Length() => Math.Sqrt(X * X + Y * Y);
}

// Stack allocated, copied by value
var p = Point { X = 1.0, Y = 2.0 };
```

### 1.4 Classes (Reference Types, ARC Managed)

```csharp
class Animal {
    public string Name;
    public int Age;
    
    public Animal(string name, int age) {
        Name = name;
        Age = age;
    }
    
    public virtual string Speak() => $"{Name} says hello";
}

class Dog : Animal {
    public Dog(string name, int age) : base(name, age) { }
    
    public override string Speak() => $"{Name} barks!";
}
```

### 1.5 Interfaces

```csharp
interface IDrawable {
    void Draw(Canvas canvas);
    float Area();
}

struct Circle : IDrawable {
    public float Radius;
    
    public void Draw(Canvas canvas) { ... }
    public float Area() => Math.PI * Radius * Radius;
}
```

### 1.6 Generics

```csharp
class List<T> {
    public void Add(T item) { ... }
    public T this[int index] { get; }
    public int Count { get; }
}

T Max<T>(T a, T b) where T : IComparable<T> {
    return a.CompareTo(b) > 0 ? a : b;
}
```

### 1.7 Properties

```csharp
class Window {
    private string _title;
    
    public string Title {
        get => _title;
        set {
            _title = value;
            Invalidate();
        }
    }
    
    // Auto property
    public int Width { get; set; }
}
```

### 1.8 Pattern Matching

```csharp
string Describe(object obj) => obj switch {
    int n when n > 0 => "positive",
    int n => "non-positive",
    string s => $"text: {s}",
    null => "nothing",
    _ => "unknown"
};
```

### 1.9 Enums

```csharp
// Simple enum
enum Color { Red, Green, Blue }

// Enum with values
enum HttpStatus : int {
    OK = 200,
    NotFound = 404,
    ServerError = 500
}

// Tagged union (algebraic type)
enum Result<T> {
    Ok(T value),
    Error(string message)
}
```

### 1.10 Error Handling

```csharp
// Result-based (preferred)
Result<int> ParseInt(string text) {
    if (TryParse(text, out int value))
        return Result.Ok(value);
    return Result.Error("invalid number");
}

// Try-catch (for interop with system exceptions)
try {
    var file = File.Open("data.txt");
} catch (IOException e) {
    Log.Error(e.Message);
}
```

### 1.11 Namespaces & Modules

```csharp
namespace MyApp.Core;

using System;
using System.IO;

public class App {
    public static void Main(string[] args) {
        Console.WriteLine("Hello Zan!");
    }
}
```

### 1.12 FFI / Native Interop

```csharp
// Direct DLL import (like aardio/C# P/Invoke)
[DllImport("user32.dll")]
static extern int MessageBox(nint hwnd, string text, string caption, uint type);

// Direct C function call
[CImport("math.h")]
static extern double sin(double x);

// Inline native code block (like aardio)
unsafe {
    nint ptr = NativeAlloc(1024);
    NativeMemory.Copy(src, ptr, 1024);
    NativeFree(ptr);
}
```

### 1.13 String Interpolation

```csharp
string name = "World";
int count = 42;
string msg = $"Hello {name}, count={count:D4}";
```

### 1.14 Lambda & Closures

```csharp
var nums = new List<int> { 3, 1, 4, 1, 5 };
var sorted = nums.OrderBy(x => x);
var evens = nums.Where(x => x % 2 == 0);

// Multi-line lambda
var result = nums.Aggregate(0, (sum, x) => {
    Log($"Adding {x}");
    return sum + x;
});
```

### 1.15 Async/Await

```csharp
async Task<string> FetchData(string url) {
    var response = await Http.Get(url);
    return await response.ReadString();
}
```

---

## 2. Memory Model — ARC + Value Semantics

### 2.1 Design Philosophy

| Category | Mechanism | Overhead |
|----------|-----------|----------|
| Value types (struct, enum, tuple) | Stack/inline allocation | Zero |
| Reference types (class) | Heap allocation + ARC | Minimal (atomic refcount) |
| Strings | Interned + COW | Near-zero for typical use |
| Arrays/Collections | Value semantics + COW | Copy-on-write |
| Raw pointers | `unsafe` blocks only | Zero |

### 2.2 ARC (Automatic Reference Counting)

```csharp
class Connection {
    private nint handle;
    
    public Connection(string addr) {
        handle = NativeConnect(addr);  // refcount = 1
    }
    
    ~Connection() {                     // Destructor: called when refcount → 0
        NativeDisconnect(handle);
    }
}

// Usage:
var conn = new Connection("localhost");  // refcount = 1
var alias = conn;                        // refcount = 2 (compiler inserts retain)
// alias goes out of scope                // refcount = 1 (compiler inserts release)
// conn goes out of scope                 // refcount = 0 → destructor called
```

### 2.3 Weak References (Break Cycles)

```csharp
class Node {
    public Node? Next;           // Strong reference
    public weak Node? Parent;    // Weak reference (won't keep alive)
}
```

### 2.4 Copy-on-Write Collections

```csharp
var a = new List<int> { 1, 2, 3 };  // One allocation
var b = a;                           // Shares storage (COW)
b.Add(4);                            // Triggers copy, now independent
// a = [1,2,3], b = [1,2,3,4]
```

---

## 3. Compiler Architecture

```
 Source (.zan)
    │
    ▼
┌─────────┐    ┌─────────┐    ┌─────────┐    ┌──────────┐    ┌──────────┐
│  Lexer  │───▶│ Parser  │───▶│ Binder  │───▶│ Checker  │───▶│ IRGen    │
│ (tokens)│    │  (AST)  │    │(symbols)│    │ (types)  │    │(LLVM IR) │
└─────────┘    └─────────┘    └─────────┘    └──────────┘    └──────────┘
                                                                  │
                                                                  ▼
                                                            ┌──────────┐
                                                            │   LLVM   │
                                                            │ Backend  │
                                                            │(.obj/.o) │
                                                            └──────────┘
                                                                  │
                                                                  ▼
                                                            ┌──────────┐
                                                            │  Linker  │
                                                            │(.exe/.so)│
                                                            └──────────┘
```

### 3.1 Implementation Language

The compiler is written in **C** (C11 standard):
- Maximum portability
- No dependency on C++ runtime
- Fast compilation of the compiler itself
- Future: self-hosting (compiler compiles itself)

### 3.2 Key Components

| Component | File | Responsibility |
|-----------|------|----------------|
| Lexer | `lexer.c/h` | Source → tokens |
| Parser | `parser.c/h` | Tokens → AST |
| AST | `ast.c/h` | Syntax tree nodes |
| Binder | `binder.c/h` | Name resolution, symbol table |
| Checker | `checker.c/h` | Type checking, inference |
| IRGen | `irgen.c/h` | AST → LLVM IR |
| Driver | `driver.c/h` | Compilation pipeline |
| Diagnostics | `diag.c/h` | Error/warning reporting |

### 3.3 LLVM Integration

- Use **LLVM C API** (`llvm-c/`) for IR generation
- Target: LLVM 17+ (stable C API)
- Passes: mem2reg, instcombine, SROA, GVN, loop opts
- Targets: x86-64, AArch64 (ARM64)
- Debug info: DWARF on Linux/macOS, PDB on Windows

---

## 4. Runtime Library

### 4.1 Core Runtime (`rt_core`)

- ARC retain/release (atomic refcount operations)
- Object header layout: `[refcount:i64][type_info:ptr][data...]`
- Type descriptors for RTTI and virtual dispatch
- Stack unwinding for exception handling

### 4.2 String Runtime (`rt_string`)

- Immutable strings, interned small strings
- UTF-8 internal encoding
- COW for string builder
- String interpolation expansion

### 4.3 Collection Runtime (`rt_collections`)

- `List<T>`: Dynamic array with COW
- `Dict<K,V>`: Hash map
- `Set<T>`: Hash set
- All use proper refcounting, no leaked headers

### 4.4 FFI Runtime (`rt_ffi`)

- Dynamic library loading (dlopen/LoadLibrary)
- Function pointer marshaling
- Struct layout for C ABI compatibility
- Automatic string marshaling (managed ↔ native)

---

## 5. IDE — Lightweight Integrated Environment

### 5.1 Design Goals

- **Single executable**: compiler + IDE + runtime bundled
- **< 20MB** total size
- **Instant startup**: < 500ms
- **Cross-platform**: Windows (native), Linux (X11/Wayland), macOS

### 5.2 Architecture

- Custom rendering: Direct2D (Windows), OpenGL/Vulkan (Linux/macOS)
- Or: Dear ImGui for cross-platform simplicity
- Built-in code editor with syntax highlighting
- Integrated terminal / output panel
- Project tree browser
- F5 = compile + run (instant feedback)
- Error list with click-to-navigate
- Autocomplete powered by the compiler's binder/checker

### 5.3 aardio-style Features

- Code snippets library
- Visual form designer (future phase)
- Built-in API documentation browser
- One-click deployment (build + package)

---

## 6. Build & Deployment

### 6.1 Build System

```
zan build                    # Build current project
zan run                      # Build + run
zan build --release          # Optimized build
zan build --target linux-x64 # Cross-compile
```

### 6.2 Project Structure

```
MyApp/
├── project.zan              # Project config (like .csproj)
├── src/
│   ├── Main.zan
│   └── Utils.zan
├── lib/                     # Dependencies
└── bin/                     # Build output
```

### 6.3 project.zan

```
project MyApp {
    type = "exe"              // exe, lib, dll
    version = "1.0.0"
    target = "windows-x64"    // or linux-x64, macos-arm64
    
    dependencies {
        std = "builtin"
    }
    
    native {
        link "user32.dll"
        link "gdi32.dll"
    }
}
```

---

## 7. Implementation Roadmap

### Phase 1: Foundation (Week 1-2)
- [x] Design document
- [ ] Project scaffold + CMake build system
- [ ] Lexer: tokenize C# syntax subset
- [ ] Parser: expressions, statements, functions, structs
- [ ] AST printer (debug output)
- [ ] Compile and run "Hello World" via LLVM → native

### Phase 2: Type System (Week 3-4)
- [ ] Binder: name resolution, scopes
- [ ] Type checker: basic types, structs, functions
- [ ] Generics (monomorphization)
- [ ] Interface dispatch
- [ ] Error diagnostics with source locations

### Phase 3: Memory & Runtime (Week 5-6)
- [ ] ARC: automatic retain/release insertion
- [ ] Object layout with type descriptors
- [ ] String runtime (interning, COW)
- [ ] Array/List runtime (COW, proper refcount)
- [ ] Weak references

### Phase 4: Code Generation (Week 7-8)
- [ ] Full LLVM IR generation for all constructs
- [ ] Optimization passes integration
- [ ] Debug info (DWARF/PDB)
- [ ] Cross-compilation support

### Phase 5: FFI & Interop (Week 9-10)
- [ ] DllImport / CImport attributes
- [ ] Struct layout for C ABI
- [ ] String marshaling
- [ ] Callback support (managed → native → managed)

### Phase 6: IDE (Week 11-14)
- [ ] Code editor with syntax highlighting
- [ ] Project management
- [ ] Build integration (F5 = run)
- [ ] Error navigation
- [ ] Autocomplete (basic)

### Phase 7: Standard Library (Week 15+)
- [ ] System.IO (file operations)
- [ ] System.Text (string utilities)
- [ ] System.Collections (List, Dict, Set)
- [ ] System.Net (HTTP, sockets)
- [ ] System.Math

---

## 8. Comparison

| Feature | Zan V2 | Beef | C# | Rust | aardio |
|---------|--------|------|-----|------|--------|
| Syntax | C# | C# | C# | Rust | Custom |
| Compilation | AOT/LLVM | AOT/LLVM | JIT/AOT | AOT/LLVM | JIT |
| Memory | ARC | Manual+GC | GC | Ownership | GC |
| IDE | Built-in | Built-in | VS/Rider | External | Built-in |
| Deploy size | <20MB | ~100MB | ~100MB+ | N/A | <10MB |
| Impl language | C | C++ | C# | Rust | C++ |
| Cross-platform | Yes | Partial | Yes | Yes | Windows |

---

## 9. Example Program

```csharp
using System;

namespace HelloWorld;

struct Point {
    public float X;
    public float Y;
    
    public float Length() => Math.Sqrt(X * X + Y * Y);
    
    public override string ToString() => $"({X}, {Y})";
}

class Program {
    static void Main(string[] args) {
        var p = Point { X = 3.0, Y = 4.0 };
        Console.WriteLine($"Point: {p}, Length: {p.Length()}");
        
        var nums = new List<int> { 1, 2, 3, 4, 5 };
        var sum = nums.Where(x => x > 2).Sum();
        Console.WriteLine($"Sum of numbers > 2: {sum}");
        
        // FFI: call system API directly
        MessageBox(0, "Hello from Zan!", "Zan V2", 0);
    }
    
    [DllImport("user32.dll")]
    static extern int MessageBox(nint hwnd, string text, string caption, uint type);
}
```
