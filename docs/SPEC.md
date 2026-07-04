# Zan Language Specification v0.1

## 1. Overview

Zan is a statically-typed, AOT-compiled systems programming language with C# syntax. It compiles to native machine code via LLVM and uses Automatic Reference Counting (ARC) for memory management.

**Design goals:**
- C# familiar syntax — no lifetime annotations, no cryptic symbols
- Native performance — zero-cost abstractions, stack allocation for value types
- Memory safety — ARC with compile-time cycle detection, no dangling pointers
- Practical — easy FFI, source-based stdlib, lightweight tooling

---

## 2. Lexical Structure

### 2.1 Source Encoding

Source files are UTF-8 encoded with `.zan` extension.

### 2.2 Comments

```csharp
// Single-line comment

/* Multi-line
   comment */

/// XML doc comment (for API documentation)
```

### 2.3 Identifiers

```
identifier := [a-zA-Z_][a-zA-Z0-9_]*
```

Identifiers are case-sensitive. Unicode letters are allowed in identifiers.

### 2.4 Keywords

```
abstract   as         async      await      base       bool
break      byte       case       catch      char       class
const      continue   default    do         double     else
enum       extern     false      finally    float      for
foreach    get        if         in         int        interface
internal   is         let        long       namespace  new
nint       null       object     out        override   private
protected  public     readonly   ref        return     sealed
set        short      sizeof     static     string     struct
switch     this       throw      true       try        typeof
unsafe     using      value      var        virtual    void
weak       when       where      while
```

### 2.5 Literals

#### Integer Literals
```
42          // decimal
0xFF        // hexadecimal
0b1010      // binary
0o77        // octal
1_000_000   // digit separator
```

Integer literals are `int` (64-bit signed) by default. Suffixes:
- `L` or `l` — `long` (same as int, 64-bit)
- `u` — unsigned

#### Floating-Point Literals
```
3.14        // double (64-bit)
3.14f       // float (32-bit)
1.0e10      // scientific notation
```

#### String Literals
```csharp
"hello"                     // regular string
"line1\nline2"              // escape sequences
@"C:\path\to\file"         // verbatim string (no escapes)
$"Hello {name}"            // interpolated string
$@"Path: {dir}\{file}"    // verbatim + interpolated
"""
multi-line
raw string
"""                         // raw string literal
```

Escape sequences: `\n \r \t \\ \" \' \0 \x41 \u0041 \U00010000`

#### Character Literals
```
'A'         // Unicode character
'\n'        // escape sequence
'\x41'      // hex
```

### 2.6 Operators (by precedence, high to low)

| Precedence | Operators | Associativity | Description |
|------------|-----------|---------------|-------------|
| 1 | `x.y` `x?.y` `x[i]` `f(x)` `x++` `x--` | Left | Member access, indexing, call, postfix |
| 2 | `+x` `-x` `!x` `~x` `++x` `--x` `(T)x` | Right | Unary, cast |
| 3 | `*` `/` `%` | Left | Multiplicative |
| 4 | `+` `-` | Left | Additive |
| 5 | `<<` `>>` | Left | Shift |
| 6 | `<` `>` `<=` `>=` `is` `as` | Left | Relational |
| 7 | `==` `!=` | Left | Equality |
| 8 | `&` | Left | Bitwise AND |
| 9 | `^` | Left | Bitwise XOR |
| 10 | `\|` | Left | Bitwise OR |
| 11 | `&&` | Left | Logical AND |
| 12 | `\|\|` | Left | Logical OR |
| 13 | `??` | Right | Null coalescing |
| 14 | `? :` | Right | Conditional |
| 15 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | Right | Assignment |
| 16 | `=>` | Right | Lambda |

---

## 3. Type System

### 3.1 Type Categories

```
Type
├── Value Types (stack allocated, copy semantics)
│   ├── Primitive Types
│   │   ├── bool           (1 byte)
│   │   ├── byte           (1 byte, unsigned)
│   │   ├── short          (2 bytes, signed)
│   │   ├── int            (8 bytes, signed, 64-bit)
│   │   ├── long           (8 bytes, signed, alias for int)
│   │   ├── float          (4 bytes, IEEE 754)
│   │   ├── double         (8 bytes, IEEE 754)
│   │   ├── char           (4 bytes, Unicode scalar)
│   │   └── nint           (pointer-sized integer)
│   ├── struct             (user-defined value type)
│   ├── enum               (named constants or tagged union)
│   └── tuple              (anonymous product type)
│
├── Reference Types (heap allocated, ARC managed)
│   ├── class              (user-defined reference type)
│   ├── interface           (abstract contract)
│   ├── delegate           (function pointer type)
│   └── array T[]          (managed array)
│
├── Special Types
│   ├── string             (immutable UTF-8, interned)
│   ├── object             (root type for reference types)
│   ├── void               (no value)
│   └── T?                 (nullable wrapper)
│
└── Unsafe Types
    └── nint               (raw pointer, unsafe context only)
```

### 3.2 Type Inference

The `var` keyword enables local type inference:

```csharp
var x = 42;                 // inferred as int
var name = "hello";         // inferred as string
var list = new List<int>(); // inferred as List<int>
```

`var` is only allowed for local variables with initializers. Function parameter types and return types must be explicit.

### 3.3 Nullable Types

Any type can be made nullable with `?`:

```csharp
int? x = null;              // nullable int
string? name = null;        // nullable string (reference type)
Point? p = null;            // nullable struct

// Null checking
if (x != null) {
    int value = x.Value;    // safe access after check
}

// Null coalescing
int y = x ?? 0;

// Null-conditional
int? len = name?.Length;
```

### 3.4 Generics

```csharp
class List<T> {
    public void Add(T item) { ... }
    public T this[int index] { get; }
}

// Constraints
class SortedList<T> where T : IComparable<T> {
    ...
}

// Multiple constraints
T Max<T>(T a, T b) where T : struct, IComparable<T> {
    return a.CompareTo(b) > 0 ? a : b;
}
```

**Constraint types:**
- `where T : struct` — T must be a value type
- `where T : class` — T must be a reference type
- `where T : new()` — T must have a parameterless constructor
- `where T : BaseClass` — T must derive from BaseClass
- `where T : IInterface` — T must implement IInterface

**Implementation:** Generics are monomorphized for value types (separate code per type) and use type erasure with virtual dispatch for reference types.

### 3.5 Tuples

```csharp
// Anonymous tuple
(int, string) pair = (42, "hello");
int x = pair.Item1;

// Named tuple
(int Count, string Name) info = (42, "hello");
int x = info.Count;

// Destructuring
var (count, name) = GetInfo();
```

---

## 4. Declarations

### 4.1 Namespaces

```csharp
namespace MyApp.Core;       // File-scoped namespace (preferred)

// or block-scoped:
namespace MyApp.Core {
    ...
}
```

### 4.2 Using Declarations

```csharp
using System;               // Import namespace
using System.IO;
using static Math;          // Import static members
using Pos = System.Drawing.Point;  // Type alias
```

### 4.3 Struct Declaration

```csharp
[repr("C")]                 // Optional: C-compatible layout
public struct Vector3 {
    public float X;
    public float Y;
    public float Z;

    public Vector3(float x, float y, float z) {
        X = x; Y = y; Z = z;
    }

    public float Length() => Math.Sqrt(X*X + Y*Y + Z*Z);

    public static Vector3 operator +(Vector3 a, Vector3 b) {
        return new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    }
}
```

Structs:
- Allocated on the stack (or inline in containing type)
- Copied by value on assignment
- Cannot inherit from other structs (but can implement interfaces)
- Cannot have a parameterless constructor (zero-initialized by default)
- No ARC overhead

### 4.4 Class Declaration

```csharp
public class Window {
    // Fields
    private string _title;
    private int _width;
    private int _height;

    // Constructor
    public Window(string title, int width, int height) {
        _title = title;
        _width = width;
        _height = height;
    }

    // Destructor (called when refcount reaches 0)
    ~Window() {
        NativeDestroyWindow(_handle);
    }

    // Properties
    public string Title {
        get => _title;
        set {
            _title = value;
            Invalidate();
        }
    }

    // Auto-property
    public bool Visible { get; set; } = true;

    // Methods
    public virtual void Show() { ... }

    // Static method
    public static Window CreateDefault() {
        return new Window("Untitled", 800, 600);
    }
}
```

Classes:
- Allocated on the heap
- Passed by reference (pointer)
- ARC managed (automatic retain/release)
- Support single inheritance + multiple interfaces
- Support virtual methods and override

### 4.5 Interface Declaration

```csharp
public interface ISerializable {
    byte[] Serialize();
    static ISerializable Deserialize(byte[] data);
}

public interface IComparable<T> {
    int CompareTo(T other);
}

// Default interface methods (optional)
public interface ILogger {
    void Log(string message);

    void LogError(string message) {
        Log($"ERROR: {message}");    // default implementation
    }
}
```

### 4.6 Enum Declaration

```csharp
// Simple enum (integer backed)
public enum Color {
    Red,        // 0
    Green,      // 1
    Blue        // 2
}

// Explicit values
public enum HttpStatus : int {
    OK = 200,
    NotFound = 404,
    ServerError = 500
}

// Tagged union (algebraic data type)
public enum Option<T> {
    Some(T value),
    None
}

public enum Result<T, E> {
    Ok(T value),
    Error(E error)
}
```

### 4.7 Delegate Declaration

```csharp
public delegate int Comparison<T>(T x, T y);
public delegate void Action<T>(T item);
public delegate TResult Func<T, TResult>(T arg);
```

---

## 5. Statements

### 5.1 Variable Declaration

```csharp
int x = 42;
var name = "hello";         // type inferred
let pi = 3.14;              // immutable (like C# readonly local)
const int MAX = 100;        // compile-time constant
```

- `var` — mutable, type inferred
- `let` — immutable after initialization
- `const` — compile-time constant (must be computable at compile time)

### 5.2 Control Flow

```csharp
// If-else
if (condition) {
    ...
} else if (other) {
    ...
} else {
    ...
}

// While
while (condition) {
    ...
}

// Do-while
do {
    ...
} while (condition);

// For
for (int i = 0; i < 10; i++) {
    ...
}

// Foreach
foreach (var item in collection) {
    ...
}

// Switch (with pattern matching)
switch (value) {
    case 0:
        ...
        break;
    case int n when n > 0:
        ...
        break;
    case string s:
        ...
        break;
    default:
        ...
        break;
}

// Switch expression
var desc = value switch {
    0 => "zero",
    int n when n > 0 => "positive",
    _ => "negative"
};
```

### 5.3 Exception Handling

```csharp
try {
    var data = File.ReadAll("input.txt");
    Process(data);
} catch (FileNotFoundException e) {
    Console.Error($"File not found: {e.Path}");
} catch (Exception e) {
    Console.Error($"Error: {e.Message}");
} finally {
    Cleanup();
}

// Throw
throw new ArgumentException("invalid value");
```

---

## 6. Memory Model

### 6.1 Value Types — Stack Allocation

Value types (primitives, structs, enums, tuples) are allocated on the stack. They are copied by value on assignment and parameter passing. No heap allocation, no reference counting.

```csharp
struct Point { public float X; public float Y; }

var a = Point { X = 1, Y = 2 };    // stack allocated
var b = a;                           // copied (independent)
b.X = 99;                           // a.X is still 1
```

### 6.2 Reference Types — ARC

Reference types (classes) are heap-allocated and managed by ARC. The compiler automatically inserts `retain` (increment refcount) and `release` (decrement refcount) calls.

```csharp
class Buffer {
    private byte[] data;
    public Buffer(int size) { data = new byte[size]; }
    ~Buffer() { Console.WriteLine("Buffer freed"); }
}

void Example() {
    var buf = new Buffer(1024);     // refcount = 1
    var alias = buf;                 // refcount = 2
    // alias goes out of scope       // refcount = 1
    // buf goes out of scope         // refcount = 0 → destructor called
}
```

### 6.3 ARC Optimizations

The compiler applies several optimizations to minimize ARC overhead:

1. **Elision** — retain/release pairs that cancel out are removed
2. **Move semantics** — last use of a variable transfers ownership instead of retain+release
3. **Inline refcount** — small objects embed refcount in the object header
4. **Immortal objects** — string literals and constants skip refcounting

### 6.4 Weak References

To break reference cycles, use `weak`:

```csharp
class Parent {
    public Child? Child;
}

class Child {
    public weak Parent? Parent;     // weak — won't prevent Parent deallocation
}
```

Accessing a weak reference returns `null` if the object has been deallocated.

### 6.5 Copy-on-Write (COW)

Collections (List, Dict, etc.) use COW semantics:

```csharp
var a = new List<int> { 1, 2, 3 };
var b = a;              // b shares a's storage (refcount 2)
b.Add(4);               // triggers copy — b gets independent storage
// a = [1,2,3], b = [1,2,3,4]
```

### 6.6 Unsafe Context

For low-level operations, use `unsafe` blocks:

```csharp
unsafe {
    nint ptr = NativeAlloc(1024);
    byte* data = (byte*)ptr;
    data[0] = 0xFF;
    NativeFree(ptr);
}
```

In `unsafe` context:
- Raw pointer arithmetic is allowed
- Manual memory management
- No bounds checking
- No ARC for raw pointers

---

## 7. FFI (Foreign Function Interface)

### 7.1 DllImport

```csharp
[DllImport("user32.dll")]
static extern int MessageBox(nint hwnd, string text, string caption, uint type);

[DllImport("kernel32.dll", EntryPoint = "GetTickCount64")]
static extern long GetTickCount();
```

### 7.2 C Header Import

```csharp
[CImport("math.h")]
static extern double sin(double x);

[CImport("stdio.h")]
static extern int printf(string format, ...);
```

### 7.3 Struct Layout for Interop

```csharp
[repr("C")]
struct POINT {
    public int X;
    public int Y;
}

[repr("C")]
struct RECT {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
}
```

### 7.4 Callback (Managed → Native → Managed)

```csharp
delegate int CompareFunc(nint a, nint b);

[DllImport("libc")]
static extern void qsort(nint base, int count, int size, CompareFunc cmp);

// Usage:
qsort(ptr, count, sizeof(int), (a, b) => {
    return *(int*)a - *(int*)b;
});
```

### 7.5 String Marshaling

| Zan Type | Native Type | Marshaling |
|----------|-------------|------------|
| `string` | `const char*` | UTF-8, null-terminated, temporary |
| `string` | `const wchar_t*` | UTF-16 on Windows |
| `byte[]` | `void*` + length | Pin and pass pointer |

---

## 8. Standard Library Structure

The standard library is distributed as Zan source files. Native dependencies are bundled alongside in `native/` subdirectories.

```
stdlib/
├── System/
│   ├── Console.zan            # Console I/O
│   ├── Math.zan               # Math functions
│   ├── String.zan             # String extensions
│   ├── Convert.zan            # Type conversions
│   ├── IO/
│   │   ├── File.zan           # File operations
│   │   ├── Path.zan           # Path manipulation
│   │   ├── Stream.zan         # Stream abstractions
│   │   └── native/            # Platform-specific native code
│   ├── Collections/
│   │   ├── List.zan           # Dynamic array
│   │   ├── Dict.zan           # Hash map
│   │   ├── Set.zan            # Hash set
│   │   ├── Queue.zan          # Queue
│   │   └── Stack.zan          # Stack
│   ├── Text/
│   │   ├── Encoding.zan       # UTF-8/16/32 encoding
│   │   ├── Regex.zan          # Regular expressions
│   │   └── StringBuilder.zan  # Mutable string builder
│   ├── Net/
│   │   ├── Http.zan           # HTTP client
│   │   ├── Socket.zan         # TCP/UDP sockets
│   │   └── native/
│   │       ├── win/            # Windows native (ws2_32.dll)
│   │       └── unix/           # Unix native (libcurl.so)
│   ├── Threading/
│   │   ├── Task.zan           # Async task
│   │   ├── Thread.zan         # OS threads
│   │   ├── Mutex.zan          # Synchronization
│   │   └── Channel.zan        # Message passing
│   └── Runtime/
│       ├── GC.zan             # ARC runtime interface
│       ├── Memory.zan         # Low-level memory ops
│       └── Interop.zan        # FFI helpers
├── Graphics/
│   ├── Canvas.zan
│   └── native/
└── Platform/
    ├── Windows.zan             # Win32 API bindings
    └── Posix.zan               # POSIX API bindings
```

### 8.1 Import Resolution

When the compiler encounters `using System.IO`, it searches:

1. `<stdlib_path>/System/IO.zan` (single file module)
2. `<stdlib_path>/System/IO/` directory (multi-file module, entry = `mod.zan`)

Native dependencies in `native/` are automatically linked based on target platform.

### 8.2 Non-Intrusive Design

- Standard library modules are loaded **only when imported** (not preloaded)
- Each module is self-contained — its `native/` directory holds all required DLLs/SOs
- User code and stdlib use the same module system — no special treatment
- Users can override stdlib modules by placing files in their project's `lib/` directory

---

## 9. Project Configuration

### 9.1 project.zan

```
project MyApp {
    type = "exe"                    // exe, lib, dll
    version = "1.0.0"
    authors = ["Author Name"]
    target = "windows-x64"         // or linux-x64, macos-arm64, auto

    dependencies {
        std = "builtin"            // standard library
    }

    native {
        link "user32.dll"          // native libraries to link
        link "gdi32.dll"
        include "vendor/include"   // C header search paths
    }

    build {
        optimize = "release"       // debug, release, size
        debug_info = true
        warnings_as_errors = false
    }
}
```

### 9.2 Build Commands

```bash
zan build                   # Build project
zan run                     # Build and run
zan test                    # Run test suite
zan check                   # Type check without building
zan fmt                     # Format source code
zan doc                     # Generate documentation
zan build --release         # Optimized build
zan build --target linux-x64  # Cross-compile
```

---

## 10. Grammar Summary (EBNF)

```ebnf
program         = { using_decl } namespace_decl { type_decl } ;
using_decl      = "using" [ "static" ] qualified_name [ "=" type ] ";" ;
namespace_decl  = "namespace" qualified_name ( ";" | "{" { type_decl } "}" ) ;

type_decl       = { attribute } { modifier } ( class_decl | struct_decl
                | interface_decl | enum_decl | delegate_decl ) ;

class_decl      = "class" IDENT [ type_params ] [ ":" type_list ]
                  "{" { member_decl } "}" ;
struct_decl     = "struct" IDENT [ type_params ] [ ":" type_list ]
                  "{" { member_decl } "}" ;
interface_decl  = "interface" IDENT [ type_params ] [ ":" type_list ]
                  "{" { member_decl } "}" ;
enum_decl       = "enum" IDENT [ ":" type ] "{" enum_members "}" ;
delegate_decl   = "delegate" type IDENT [ type_params ] "(" param_list ")" ";" ;

member_decl     = field_decl | method_decl | constructor_decl
                | destructor_decl | property_decl | indexer_decl
                | operator_decl ;

field_decl      = { modifier } type IDENT [ "=" expr ] ";" ;
method_decl     = { modifier } type IDENT [ type_params ] "(" param_list ")"
                  [ where_clauses ] ( block | "=>" expr ";" ) ;
constructor_decl = { modifier } IDENT "(" param_list ")"
                   [ ":" ( "base" | "this" ) "(" arg_list ")" ] block ;
destructor_decl = "~" IDENT "(" ")" block ;
property_decl   = { modifier } type IDENT "{" accessors "}" ;

type            = qualified_name [ type_args ] [ "?" ] [ "[]" ] ;
type_params     = "<" IDENT { "," IDENT } ">" ;
type_args       = "<" type { "," type } ">" ;
type_list       = type { "," type } ;

param_list      = [ param { "," param } ] ;
param           = { "ref" | "out" | "in" } type IDENT [ "=" expr ] ;

modifier        = "public" | "private" | "protected" | "internal"
                | "static" | "virtual" | "override" | "abstract"
                | "sealed" | "readonly" | "extern" | "async"
                | "unsafe" | "weak" ;

block           = "{" { statement } "}" ;
statement       = var_decl | expr_stmt | if_stmt | while_stmt | for_stmt
                | foreach_stmt | switch_stmt | return_stmt | break_stmt
                | continue_stmt | throw_stmt | try_stmt | block ;

expr            = assignment | conditional | binary | unary | postfix
                | primary | lambda | switch_expr ;
primary         = literal | IDENT | "this" | "base" | "(" expr ")"
                | "new" type [ "(" arg_list ")" ] [ "{" init_list "}" ]
                | "typeof" "(" type ")" | "sizeof" "(" type ")"
                | "default" "(" type ")" ;

literal         = INT_LIT | FLOAT_LIT | STRING_LIT | CHAR_LIT
                | "true" | "false" | "null" ;

qualified_name  = IDENT { "." IDENT } ;
```

---

## Appendix A: Reserved for Future

- `yield` — iterator/generator support
- `fixed` — pin managed object in memory
- `checked` / `unchecked` — overflow checking
- `lock` — monitor-based synchronization
- `using` statement — deterministic disposal (like C# IDisposable)
- `record` — immutable reference type with value equality
- `init` — init-only property setter
