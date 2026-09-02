# Zan Language Specification

> 本文档描述 Zan 语言的**当前实测语义**（zanc v0.2.1，2026-08-08 复核）。
> 所有声称的特性均在 `tests/conformance/` 有对应用例或经编译探针验证；
> 未实现/行为不标准的特性不在此列，见文末附录 A。
> 本文件由 `docs/DOCS_MAINTENANCE.md` 的分层规则维护：只写现状，不写设计意图。

## 1. Overview

Zan is a statically-typed, AOT-compiled systems programming language with C# syntax. It compiles to native machine code via LLVM and uses Automatic Reference Counting (ARC) for memory management.

**Design goals:**
- C# familiar syntax — no lifetime annotations, no cryptic symbols
- Native performance — zero-cost abstractions, stack allocation for value types
- Memory safety — ARC with compile-time cycle detection (via `weak`), no dangling pointers
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

/// XML doc comment (consumed by zandoc)
```

### 2.3 Identifiers

```
identifier := [a-zA-Z_][a-zA-Z0-9_]*
```

Identifiers are case-sensitive. Unicode letters are allowed in identifiers.

### 2.4 Keywords

```
abstract   as         async      await      base       bool
break      byte       case       catch      char       checked
class      const      continue   decimal    default    delegate
do         double     else       enum       extern     false
finally    fixed      float      for        foreach    get
goto       if         in         int        interface  internal
is         let        lock       long       namespace  new
nint       null       object     operator   out        override
private    protected  public     readonly   ref        return
sbyte      sealed     set        short      sizeof     static
string     struct     switch     this       throw      true
try        typeof     uint       ulong      unchecked  unsafe
ushort     using      var        virtual    void       weak
when       where      while
```

`checked` / `unchecked` 为 no-op（溢出检查始终关闭，见附录 A）。
`value` 是上下文关键字：仅在属性 setter 体内表示隐式传入值，其余位置可作普通标识符。

### 2.5 Literals

#### Integer Literals
```
42          // decimal
0xFF        // hexadecimal
0b1010      // binary
0o77        // octal
1_000_000   // digit separator
```

Integer literals are `int` (32-bit signed) by default. Suffixes:
- `L` or `l` — `long` (64-bit; `int` is 32-bit, `long` is a distinct type)
- `u` or `U` — unsigned (`uint`/`ulong` per literal size)

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
@"C:\path\to\file"          // verbatim string (no escapes)
$"Hello {name}"             // interpolated string
```

Escape sequences: `\n \r \t \\ \" \' \0`（不支持 `\x` / `\u` / `\U` 十六进制与码点转义，也不支持 `$@"..."` 组合形式与 `"""` 原始字符串）。

#### Character Literals
```
'A'         // Unicode character
'\n'        // escape sequence
```

`char` 是 8 字节的 Unicode 码点字（存储宽度对齐 64 位，`sizeof(char) == 8`）。

### 2.6 Operators (by precedence, high to low)

| Precedence | Operators | Associativity | Description |
|------------|-----------|---------------|-------------|
| 1 | `x.y` `x[i]` `f(x)` `x++` `x--` | Left | Member access, indexing, call, postfix |
| 2 | `+x` `-x` `!x` `~x` `++x` `--x` `(T)x` | Right | Unary, cast |
| 3 | `*` `/` `%` | Left | Multiplicative |
| 4 | `+` `-` | Left | Additive |
| 5 | `<<` `>>` | Left | Shift |
| 6 | `<` `>` `<=` `>=` `is` `as` | Left | Relational, type test |
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

Note: `?.`（null-conditional）**已实现**（2026-08-27 实测更正，此前本节写"未实现"）：
`x?.y` 在 `x` 为 null 时求值为无值的可空类型，非 null 时取成员，与 C# 一致
（`tests/conformance/nullable_reference_types.zan`）。

---

## 3. Type System

### 3.1 Type Categories

```
Type
├── Value Types (stack allocated, copy semantics)
│   ├── Primitive Types
│   │   ├── bool           (1 byte)
│   │   ├── byte           (1 byte, unsigned)
│   │   ├── sbyte          (1 byte, signed)
│   │   ├── short          (2 bytes, signed)
│   │   ├── ushort         (2 bytes, unsigned)
│   │   ├── int            (4 bytes, signed, 32-bit)
│   │   ├── uint           (4 bytes, unsigned)
│   │   ├── long           (8 bytes, signed, 64-bit — distinct from int)
│   │   ├── ulong          (8 bytes, unsigned)
│   │   ├── float          (4 bytes, IEEE 754)
│   │   ├── double         (8 bytes, IEEE 754)
│   │   ├── char           (8 bytes, Unicode scalar)
│   │   └── nint           (pointer-sized integer)
│   ├── struct             (user-defined value type)
│   └── enum               (named constants, integer-backed)
│
├── Reference Types (heap allocated, ARC managed)
│   ├── class              (user-defined reference type)
│   ├── interface           (abstract contract)
│   ├── delegate           (function pointer type)
│   └── array T[]          (managed array)
│
└── Special Types
    ├── string             (immutable UTF-8)
    ├── object             (root type for reference types)
    ├── void               (no value)
    └── T?                 (nullable wrapper)
```

`nint` 是普通有符号整数类型（指针宽度），原生内存访问通过 `NativeMemory` / `[DllImport]` 完成，见 §7。`decimal` 是保留关键字，尚无对应类型。

### 3.2 Type Inference

The `var` keyword enables local type inference:

```csharp
var x = 42;                 // inferred as int
var name = "hello";         // inferred as string
var list = new List<int>(); // inferred as List<int>
```

`var` is only allowed for local variables with initializers. Function parameter types and return types must be explicit.

### 3.3 Nullable Types

Value types can be made nullable with `?`（引用类型本身可空，无需 `?`）:

```csharp
int? x = null;              // nullable int
Point? p = null;            // nullable struct

// Null checking via HasValue / Value
if (x.HasValue) {
    int value = x.Value;    // safe access after check
}

// Null coalescing
int y = x ?? 0;
```

注意：`?.` 已可用（见 §2.6 的更正）；`int? len = s?.Length;` 会得到一个可空值，
用 `HasValue` / `.Value` 读取。

### 3.4 Generics

```csharp
class List<T> {
    public void Add(T item) { ... }
    public T this[int index] { get; }
}

// Interface constraint
class SortedList<T> where T : IComparable<T> {
    ...
}

// Value-type constraint
T Max<T>(T a, T b) where T : struct {
    return a.CompareTo(b) > 0 ? a : b;
}
```

**Constraint types:** `where T : struct`（值类型）、`where T : BaseClass`（基类）、`where T : IInterface`（接口）。

**Implementation:** Generics are monomorphized for value types (separate code per type) and use type erasure with virtual dispatch for reference types.

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
```

### 4.3 Struct Declaration

```csharp
[StructLayout]              // Optional: C-compatible (sequential) layout
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
    // Fields (initializers allowed)
    private string _title = "";
    private int _width;
    private int _height;

    // Constructor (overloads, `: this(...)` / `: base(...)` chaining supported)
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
        get { return _title; }
        set {
            _title = value;     // `value` is the implicit setter parameter
            Invalidate();
        }
    }

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
}

public interface IComparable<T> {
    int CompareTo(T other);
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
```

枚举是纯整数常量集合，**不支持** tagged union（代数数据类型）语法。

### 4.7 Delegate Declaration

```csharp
public delegate int Comparison<T>(T x, T y);
public delegate void Action<T>(T item);
public delegate TResult Func<T, TResult>(T arg);
```

支持捕获的 lambda（闭包）与无捕获 lambda。

### 4.8 Access Control

Class/struct members（字段、属性、方法、构造器）可以标记四种访问修饰符；
检查器在编译期强制执行。**未加修饰符的成员完全公开**——这是默认，整个
标准库都依赖它：

| 修饰符       | 可访问范围                                                     |
|--------------|----------------------------------------------------------------|
| （无）       | 任何代码                                                       |
| `private`    | 仅声明该成员的类型自身的方法体                                  |
| `protected`  | 声明类型自身 + 派生类型的方法体                                 |
| `internal`   | 同一"模块"内的所有类型的代码                                    |

模块以命名空间前缀界定：取命名空间第二个 `.` 之前的前缀
（`System.Data.SqlServer` 与 `System.Data.MySql` 同属模块
`System.Data`；单点命名空间如 `Acc.Inner` 自成一个模块）。这与物理布局
一致——`stdlib/` 下一个目录就是一个封装边界。

```csharp
public class Base {
    private int seed = 1;         // 只有 Base 的方法体能碰
    protected int guard = 2;      // Base 及其派生类可碰
    internal int tag = 3;         // 同模块内的类型可碰
}

public class Child : Base {
    public int Read() { return guard; }      // OK：派生类
    public int Fail(Sibling s) {
        // return s.otherPrivate;             // error: '...' is private
        return tag;                            // OK：internal 同模块
    }
}
```

访问被拒绝时编译器报告：
`'Owner.member' is private and cannot be accessed from 'Ctx'`
（protected/internal 类似，internal 会指出所属模块）。构造器同样受控，
`private` 构造器阻止在类型之外 `new`（工厂模式）。顶级类型本身的修饰符
目前解析后不参与限制。

---

## 5. Statements

### 5.1 Variable Declaration

```csharp
int x = 42;
var name = "hello";         // type inferred
let pi = 3.14;              // inferred; let 是声明语法（见下）
const int MAX = 100;        // compile-time constant
```

- `var` — type inferred, mutable
- `let` — type inferred；目前仅是声明语法（AST 带只读标记），**尚无可变性强制检查**
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

// Switch
switch (value) {
    case 0:
        ...
        break;
    case 1:
    case 2:
        ...                 // fallthrough is explicit
        break;
    default:
        ...
        break;
}
```

switch 有语句与表达式两种形态（2026-08-27 复核更正，此前本节写"表达式形态与
`when` 守卫未实现"）：语句形态支持常量 case、`case null:`、类型模式 `case T x:`
与 `when` 守卫；表达式形态 `value switch { <模式> when <守卫> => <结果>, ... }`
支持常量/类型模式、`_` discard、`default`，无匹配时取零值
（`cs_b06_switch_expr.zan`、`cs_b05_pattern_var.zan`、`pattern_value_types.zan`）。
值类型的类型模式按静态类型判定（`case int n:` 对 int 判别式成立）。

### 5.3 Exception Handling

```csharp
try {
    var data = File.ReadAllText("input.txt");
    Process(data);
} catch (FileNotFoundException e) {
    Console.WriteLine("File not found");
} catch (Exception e) {
    Console.WriteLine("Error: " + e.Message);
} finally {
    Cleanup();
}

// Throw
throw new ArgumentException("invalid value");
```

`Console.Error` 不存在——错误输出用 `Console.WriteLine`。

---

## 6. Memory Model

### 6.1 Value Types — Stack Allocation

Value types (primitives, structs, enums) are allocated on the stack. They are copied by value on assignment and parameter passing. No heap allocation, no reference counting.

```csharp
struct Point { public float X; public float Y; }

var a = new Point { X = 1, Y = 2 };  // stack allocated
var b = a;                           // copied (independent)
b.X = 99;                            // a.X is still 1
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

### 6.5 Native Memory (no raw pointers)

Zan 没有 `T*` 指针类型。低层内存操作经 `nint` 与 `NativeMemory` 完成：

```csharp
nint ptr = NativeMemory.Alloc(1024);
NativeMemory.Fill(ptr, 0, 1024);
NativeMemory.Free(ptr);
```

`unsafe { }` 块与 `fixed (T name = expr)` 语句是合法语法（`fixed` 仅作作用域别名，无 GC 需要固定），但原生访问仍走 `nint`。

---

## 7. FFI (Foreign Function Interface)

### 7.1 DllImport

```csharp
[DllImport("user32.dll", EntryPoint = "MessageBoxA")]
static extern int MessageBox(nint hwnd, string text, string caption, uint type);

[DllImport("kernel32.dll", EntryPoint = "GetTickCount64")]
static extern long GetTickCount();
```

### 7.2 Struct Layout for Interop

```csharp
[StructLayout]
struct POINT {
    public int X;
    public int Y;
}
```

`[StructLayout]` 使结构体按 C 顺序布局（默认已是 sequential；无显式 `[StructLayout]` 的 struct 编译器也可能按需重排）。

### 7.3 Function Pointers

原生函数指针经 delegate 与 `nint` 互转（`System.Interop` 提供 `LoadLibrary`/`GetProcAddress`/`dlopen`/`dlsym` 封装）：

```csharp
delegate int CompareFunc(nint a, nint b);

// Load a native function by name, cast the nint to the delegate type, and call it
```

### 7.4 String Marshaling

| Zan Type | Native Type | Marshaling |
|----------|-------------|------------|
| `string` | `const char*` | UTF-8, null-terminated, temporary |

`[CImport]` 不存在；C 头文件不能直接导入，通过 `[DllImport]` + `System.Interop` 加载。

---

## 8. Standard Library Structure

标准库是 Zan 源码（`stdlib/`），与编译器一同发布；`--auto-stdlib` 让编译器自动定位。

```
stdlib/
├── System/        # 语言核心：集合、IO、文本、网络、线程、JSON 等
├── Gui/           # 自托管 IDE 使用的 UI 框架（纯 Zan，标准库组件）
├── Game/          # 游戏/图形库
├── Sdk/           # 平台 SDK 绑定（生成代码为主）
├── SDL3/          # SDL3 绑定层
└── Platform/      # 平台占位
```

`System/` 下按子命名空间组织：`Collections`、`Compiler`（Zan 脚本化代码生成器）、`Data`、`Diagnostics`、`Drawing`、`Globalization`、`IO`、`Input`、`Json`、`Linq`、`Management`、`Net`、`Resources`、`Scripting`（Python/Lua 嵌入）、`Security`、`ServiceProcess`、`Text`（正则等）、`Threading`、`Web`、`Windows`。

以下类型由编译器内建（`src/compiler/builtin_api.c`），不必写实现即可用：

| 类型 | 说明 |
|------|------|
| `string` | 不可变 UTF-8 字符串 |
| `List<T>` / `Dictionary<K,V>` | 动态数组 / 哈希表 |
| `StringBuilder` | 可变字符串构造器 |
| `Console` | `WriteLine`/`Write`/`PrintLine`/`ReadLine`/`Read`/`ReadKey`/`Clear`/`ResetColor` + `ForegroundColor`/`BackgroundColor`/`Title` 属性 |
| `Math` | `Abs`/`Max`/`Min`/`Pow`/`Sqrt`/`Round`/`Floor`/`Ceiling` |
| `Convert` | `ToDouble`/`ToInt32`/`ToInt64` |
| `String` | `Format`/`Join`/`IsNullOrEmpty`/`CompareOrdinal` |
| `File` / `Directory` / `Path` | 文件系统 |
| `Environment` | `ArgCount`/`ArgAt`/`ExeDir` |
| `NativeMemory` | `Alloc`/`Free`/`Copy`/`Fill`/`Compare`/`GetString`/`PutString` |
| `Task` | 协程：`Spawn`/`Run`/`Delay`/`WhenAll`/`WhenAny`/`IsDone`/`Cancel` |

原生依赖（如 `libcrypto`、平台驱动 DLL/SO）以 driver bundle 形式随各模块发布，`--publish` 时按 `--link-mode shared|static` 链接到可执行文件旁或并入 exe。

### 8.1 Import Resolution

`using System.Collections.Generic;` 按命名空间路径解析到 `stdlib/` 下的目录或文件。编译器选项 `--stdlib-path <dir>` 指定根目录，`--auto-stdlib` 自动查找。包（第三方命名空间）经 `--package-install` / `--package-scope` 安装到项目作用域或全局作用域。

### 8.2 Non-Intrusive Design

- Standard library modules are loaded **only when imported** (not preloaded)
- Each module is self-contained — its driver bundle holds all required DLLs/SOs
- User code and stdlib use the same module system — no special treatment

---

## 9. Building Projects

没有 `project.zan` 项目文件——项目就是一组 `.zan` 源文件，用 `zanc` 直接编译：

```bash
zanc main.zan --auto-stdlib -o app.exe          # build and link
zanc main.zan --auto-stdlib --publish -o app.exe # optimized release
zanc main.zan --auto-stdlib -o lib.dll --emit-lib # shared library
```

常用选项（完整见 `zanc --help`）：

- `-o <output>` — 输出文件
- `--publish` — 发布构建（`-Os`、剥离调试信息、链接原生驱动）
- `--link-mode shared|static` — 驱动链接方式
- `--target <name>` — 交叉编译（`--list-targets` 列出）
- `-O0..-O3/-Os/-Oz` — 优化级别
- `-g` — DWARF 调试信息
- `--check-leaks` — 退出时报告未释放对象
- `--package-*` — 包管理（安装/清单/API 地址）

配套工具：`zanfmt`（格式化）、`zandoc`（文档生成）、`zan-lsp`（语言服务器）、`zan-dap`（调试适配器）。

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
                | primary | lambda ;
primary         = literal | IDENT | "this" | "base" | "(" expr ")"
                | "new" type [ "(" arg_list ")" ] [ "{" init_list "}" ]
                | "typeof" "(" type ")" | "sizeof" "(" type ")"
                | "default" "(" type ")" ;

literal         = INT_LIT | FLOAT_LIT | STRING_LIT | CHAR_LIT
                | "true" | "false" | "null" ;

qualified_name  = IDENT { "." IDENT } ;
```

---

## Appendix A: 已实现与保留

以下特性已实现（有 conformance 用例）：`yield return` / `yield break`（急切降级为隐藏 `List<T>` 累加器，非惰性迭代器）、`fixed`、`lock`、`goto`、`record`、属性 `get`/`set`、运算符重载、可空值类型（`int?` 的 `.HasValue`/`.Value`）、命名空间限定调用、`--check-leaks`。

> 2026-08-27 复核：下面这份清单有六项已经过时（`using` / `init` / 元组与解构 /
> `?.` / LINQ 查询语法 / switch 表达式与 `when` 守卫都已实现并有 conformance
> 用例），已移入"已实现"一节。清单只保留实测仍不可用的项。

以下 C# 特性**尚未实现**，不要在 Zan 代码中使用：

- `checked` / `unchecked` — 关键字存在但均为 **no-op**：溢出检查始终关闭（环绕语义），`checked(x + 1)` 与 `x + 1` 行为相同（`tests/conformance/cs_b14_checked.zan`）
- lazy iterators (`IEnumerable<T>` protocol) — `yield` currently materializes a list
- tagged union 枚举（代数数据类型）
- 命名实参之外的实参特性：`params` 之后的可选实参组合未系统验证
- `$@"..."` 组合字符串、`"""` 原始字符串
- COW（copy-on-write）集合——`List` 赋值是引用共享，修改即共享
- `[CImport]`、`project.zan` 项目文件、`zan` CLI（实际为 `zanc`）

以下曾列为未实现、实测已可用（各带 conformance 用例）：

- `using (res) { }` 确定性释放（`cs_b02_using.zan`）
- `init` 只写访问器（`cs_b18_init.zan`）
- 元组 `(int, string)` 与 `var (a, b) = ...` 解构（`cs_b03_tuple.zan`；嵌套解构仍未实现）
- `?.` null-conditional（`nullable_reference_types.zan`；见 §2.6 的更正）
- LINQ 查询语法 `from x in xs where ... select`（`linq_query.zan`）；`orderby ... descending`、`let`、三种 `join` 形态（裸 / join+orderby / join…into）（`linq_query_clauses.zan`，TASKS.md **A54** 关闭）
- 命名实参 `F(b: 2)`（`cs_b04_named_args.zan`）、索引器 `this[int i]`（`cs_b09_indexer.zan`）
- switch 表达式 `x switch { ... }` 与 `when` 守卫、值类型的类型模式（`cs_b06_switch_expr.zan`、`pattern_value_types.zan`）
- `\x`（贪婪 1-4 位 hex）与 `\u`（固定 4 位 hex）字符转义，UTF-8 编码写入字符串/插值/char 三路径，代理区独立出现即编译错误（`string_escapes.zan`；`\U` 8 位形式仍不支持）
