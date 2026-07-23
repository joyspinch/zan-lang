# Zan Standard Library Specification

## 1. Design Principles

### 1.1 Source Distribution

The standard library is distributed as **Zan source code** (`.zan` files). It is compiled together with user code — no precompiled binaries, no separate installation step.

### 1.2 Non-Intrusive Architecture

Following aardio's model:

- **No global state pollution** — each module is self-contained
- **Lazy loading** — modules are loaded only when `using` imports them
- **Bundled native deps** — DLLs/SOs live in `native/` subdirectories alongside the module
- **Override-friendly** — users can shadow stdlib modules with local versions
- **No package manager required** — files exist, they work

### 1.3 Platform Abstraction

Platform-specific code is isolated in `native/` directories:

```
System/Net/
├── Http.zan                # Cross-platform Zan API
├── Socket.zan              # Cross-platform Zan API
└── native/
    ├── win/
    │   └── ws2_32.zan      # Windows socket bindings
    ├── linux/
    │   └── socket.zan      # Linux socket bindings
    └── macos/
        └── socket.zan      # macOS socket bindings
```

The cross-platform module (`Http.zan`) uses conditional compilation:

```csharp
using System.Runtime;

public class HttpClient {
    public async Task<HttpResponse> Get(string url) {
        #if PLATFORM_WINDOWS
        return await WindowsHttp.Get(url);
        #elif PLATFORM_LINUX
        return await LinuxHttp.Get(url);
        #elif PLATFORM_MACOS
        return await MacosHttp.Get(url);
        #endif
    }
}
```

---

## 2. Directory Structure

```
stdlib/
├── System/
│   ├── mod.zan                     # System namespace entry point
│   ├── Console.zan                 # Console I/O
│   ├── Math.zan                    # Math functions
│   ├── String.zan                  # String type extensions
│   ├── Convert.zan                 # Type conversions
│   ├── Environment.zan             # Environment variables, platform info
│   ├── DateTime.zan                # Date and time
│   ├── Guid.zan                    # UUID generation
│   │
│   ├── IO/
│   │   ├── mod.zan                 # IO namespace entry
│   │   ├── File.zan                # File read/write
│   │   ├── Directory.zan           # Directory operations
│   │   ├── Path.zan                # Path manipulation
│   │   ├── Stream.zan              # Stream abstractions
│   │   ├── BinaryReader.zan        # Binary reading
│   │   ├── BinaryWriter.zan        # Binary writing
│   │   ├── TextReader.zan          # Text reading
│   │   ├── TextWriter.zan          # Text writing
│   │   └── native/
│   │       ├── win/
│   │       │   └── fileapi.zan     # Win32 file API bindings
│   │       └── unix/
│   │           └── unistd.zan      # POSIX file API bindings
│   │
│   ├── Collections/
│   │   ├── mod.zan
│   │   ├── List.zan                # Dynamic array (COW)
│   │   ├── Dict.zan                # Hash map
│   │   ├── Set.zan                 # Hash set
│   │   ├── Queue.zan               # FIFO queue
│   │   ├── Stack.zan               # LIFO stack
│   │   ├── SortedList.zan          # Sorted list (binary search)
│   │   ├── SortedDict.zan          # Sorted map (red-black tree)
│   │   └── LinkedList.zan          # Doubly linked list
│   │
│   ├── Text/
│   │   ├── mod.zan
│   │   ├── Encoding.zan            # UTF-8/16/32 encoding
│   │   ├── StringBuilder.zan       # Mutable string builder
│   │   ├── Regex.zan               # Regular expressions
│   │   └── native/
│   │       └── re2/                # RE2 regex engine (bundled)
│   │
│   ├── Net/
│   │   ├── mod.zan
│   │   ├── Http.zan                # HTTP client
│   │   ├── HttpServer.zan          # Simple HTTP server
│   │   ├── Socket.zan              # TCP/UDP sockets
│   │   ├── Dns.zan                 # DNS resolution
│   │   ├── Url.zan                 # URL parsing
│   │   └── native/
│   │       ├── win/
│   │       │   ├── winhttp.zan     # WinHTTP bindings
│   │       │   └── ws2_32.zan      # Winsock bindings
│   │       └── unix/
│   │           └── curl.zan        # libcurl bindings
│   │
│   ├── Threading/
│   │   ├── mod.zan
│   │   ├── Task.zan                # Async task / future
│   │   ├── Thread.zan              # OS thread
│   │   ├── Mutex.zan               # Mutual exclusion lock
│   │   ├── Semaphore.zan           # Counting semaphore
│   │   ├── Channel.zan             # Typed message passing
│   │   ├── Atomic.zan              # Atomic operations
│   │   └── ThreadPool.zan          # Thread pool
│   │
│   ├── Diagnostics/
│   │   ├── mod.zan
│   │   ├── Stopwatch.zan           # High-resolution timer
│   │   ├── Debug.zan               # Debug assertions
│   │   └── Trace.zan               # Tracing/logging
│   │
│   ├── Runtime/
│   │   ├── mod.zan
│   │   ├── ARC.zan                 # ARC runtime hooks
│   │   ├── Memory.zan              # Low-level memory operations
│   │   ├── Interop.zan             # FFI helpers
│   │   ├── TypeInfo.zan            # Runtime type information
│   │   └── Platform.zan            # Platform detection
│   │
│   └── Serialization/
│       ├── mod.zan
│       ├── Json.zan                # JSON serialization
│       ├── Xml.zan                 # XML parsing
│       └── Binary.zan              # Binary serialization
│
├── Graphics/
│   ├── mod.zan
│   ├── Canvas.zan                  # 2D drawing API
│   ├── Color.zan                   # Color types
│   ├── Image.zan                   # Image loading/saving
│   ├── Font.zan                    # Font rendering
│   └── native/
│       ├── win/
│       │   ├── d2d.zan             # Direct2D bindings
│       │   └── gdi.zan             # GDI bindings
│       └── unix/
│           └── cairo.zan           # Cairo bindings
│
├── UI/
│   ├── mod.zan
│   ├── Window.zan                  # Window management
│   ├── Control.zan                 # Base UI control
│   ├── Button.zan                  # Button control
│   ├── TextBox.zan                 # Text input
│   ├── Label.zan                   # Text label
│   ├── Panel.zan                   # Container panel
│   ├── Layout.zan                  # Layout managers
│   └── native/
│       ├── win/
│       │   └── user32.zan          # Win32 UI bindings
│       └── unix/
│           └── gtk.zan             # GTK bindings
│
└── Platform/
    ├── Windows.zan                 # Win32 API (comprehensive)
    ├── Posix.zan                   # POSIX API
    └── Darwin.zan                  # macOS-specific API
```

---

## 3. Core Modules API

### 3.1 System.Console

```csharp
namespace System;

public static class Console {
    // Output
    public static void Write(string text);
    public static void Write(object value);
    public static void WriteLine(string text);
    public static void WriteLine();
    public static void Error(string text);

    // Input
    public static string ReadLine();
    public static int ReadKey();

    // Formatting
    public static void SetColor(ConsoleColor fg);
    public static void SetColor(ConsoleColor fg, ConsoleColor bg);
    public static void ResetColor();

    // Cursor
    public static void SetCursorPosition(int x, int y);
    public static void Clear();
}
```

### 3.2 System.Math

```csharp
namespace System;

public static class Math {
    public const double PI = 3.14159265358979323846;
    public const double E = 2.71828182845904523536;

    public static double Sqrt(double x);
    public static double Pow(double base, double exp);
    public static double Sin(double x);
    public static double Cos(double x);
    public static double Tan(double x);
    public static double Log(double x);
    public static double Log2(double x);
    public static double Log10(double x);
    public static double Abs(double x);
    public static int Abs(int x);
    public static double Floor(double x);
    public static double Ceil(double x);
    public static double Round(double x, int digits = 0);
    public static T Min<T>(T a, T b) where T : IComparable<T>;
    public static T Max<T>(T a, T b) where T : IComparable<T>;
    public static T Clamp<T>(T value, T min, T max) where T : IComparable<T>;
}
```

### 3.3 System.Collections.List<T>

```csharp
namespace System.Collections;

public class List<T> {
    // Constructors
    public List();
    public List(int capacity);
    public List(T[] items);

    // Properties
    public int Count { get; }
    public int Capacity { get; }

    // Indexer
    public T this[int index] { get; set; }

    // Modification
    public void Add(T item);
    public void AddRange(List<T> items);
    public void Insert(int index, T item);
    public bool Remove(T item);
    public void RemoveAt(int index);
    public void Clear();

    // Query
    public bool Contains(T item);
    public int IndexOf(T item);
    public T? Find(Func<T, bool> predicate);
    public List<T> FindAll(Func<T, bool> predicate);
    public bool Any(Func<T, bool> predicate);
    public bool All(Func<T, bool> predicate);

    // Transformation
    public List<U> Map<U>(Func<T, U> transform);
    public List<T> Where(Func<T, bool> predicate);
    public T Aggregate<U>(U seed, Func<U, T, U> func);
    public void Sort();
    public void Sort(Comparison<T> comparison);
    public List<T> Slice(int start, int count);
    public T[] ToArray();

    // LINQ-style
    public T First();
    public T? FirstOrDefault();
    public T Last();
    public int Sum() where T : int;
    public List<T> OrderBy<K>(Func<T, K> keySelector) where K : IComparable<K>;
    public List<T> Distinct();
}
```

### 3.4 System.IO.File

```csharp
namespace System.IO;

public static class File {
    public static string ReadAllText(string path);
    public static byte[] ReadAllBytes(string path);
    public static string[] ReadAllLines(string path);
    public static void WriteAllText(string path, string content);
    public static void WriteAllBytes(string path, byte[] data);
    public static void AppendAllText(string path, string content);
    public static bool Exists(string path);
    public static void Delete(string path);
    public static void Copy(string source, string dest);
    public static void Move(string source, string dest);
    public static long GetSize(string path);
    public static Stream Open(string path, FileMode mode);
}
```

**Error model (C#-aligned).** Failures throw instead of silently returning
empty/default values:

- `ReadAllText` / `ReadAllLines` / `ReadBytes` / `GetSize` / `Copy` (source)
  throw `FileNotFoundException` when the file cannot be opened.
- `WriteAllText` / `WriteAllLines` / `AppendAllText` / `Copy` (destination)
  throw `IOException` when the file cannot be opened for writing.
- `Exists` remains a non-throwing boolean probe.
- `FileNotFoundException` derives from `IOException`, which derives from
  `Exception`, so `catch (IOException e)` and `catch (Exception e)` both work.

These semantics live in the stdlib source (`stdlib/System/IO/File.zan`).
When compiling **without** `--auto-stdlib` (no `File` class in the compile
unit), the compiler's builtin `File.*` lowering is used instead, which aborts
the process with `cannot read file` / `cannot write file` on failure.

### 3.5 System.Net.Http

```csharp
namespace System.Net;

public class HttpClient {
    public HttpClient();
    public HttpClient(string baseUrl);

    public async Task<HttpResponse> Get(string url);
    public async Task<HttpResponse> Post(string url, string body);
    public async Task<HttpResponse> Put(string url, string body);
    public async Task<HttpResponse> Delete(string url);

    public void SetHeader(string name, string value);
    public void SetTimeout(int milliseconds);
}

public class HttpResponse {
    public int StatusCode { get; }
    public string StatusText { get; }
    public string Body { get; }
    public byte[] BodyBytes { get; }
    public Dict<string, string> Headers { get; }
    public bool IsSuccess { get; }
}
```

**Error model (C#-aligned).** Connection failures throw instead of silently
returning empty responses:

- `TcpClient.ConnectAsync` returns `null` when the connection fails
  (refused / unreachable); `Socket.ConnectAsync` returns the socket error
  code (0 on success).
- `HttpClient` request methods throw `HttpRequestException` when the TCP
  connect fails, the TLS context cannot be created, or the TLS handshake
  fails. `HttpRequestException` derives from `Exception`.

**JSON error model (System.Json).** `JsonValue.Parse` throws `JsonException`
("Malformed JSON at position N") on malformed input; the document is
validated with an allocation-free scan before the value tree is built.
`JsonValue.ParseLenient` keeps the previous best-effort behaviour (returns
whatever parsed so far) for authored/tool-emitted documents. The typed
accessors (`Str`/`Int`/`Bool`/`Double`, `As*`) still return the caller's
explicit default when a key is absent or of the wrong kind.

---

### 3.6 System.Linq.Enumerable

Eager, typed LINQ-style operators over `List<T>` (extension methods; the
static form `Enumerable.Where(list, pred)` also works). Every operator
materialises its result into a new `List`.

```csharp
namespace System.Linq;

class Enumerable {
    // filtering / projection
    static List<T> Where<T>(this List<T> src, Predicate<T> pred);
    static List<R> Select<T, R>(this List<T> src, Selector<T, R> sel);
    static List<R> SelectMany<T, R>(this List<T> src, Selector<T, List<R>> sel);

    // quantifiers / counting
    static bool Any<T>(this List<T> src);                      // + (pred)
    static bool All<T>(this List<T> src, Predicate<T> pred);
    static int  Count<T>(this List<T> src);                    // + (pred)

    // element access — First/Last/Single throw InvalidOperationException
    // on empty/no-match (Single also on >1); *OrDefault return the default
    static T First<T>(this List<T> src);                       // + (pred)
    static T FirstOrDefault<T>(this List<T> src);              // + (pred)
    static T Last<T>(this List<T> src);                        // + (pred)
    static T LastOrDefault<T>(this List<T> src);               // + (pred)
    static T Single<T>(this List<T> src);                      // + (pred)
    static T SingleOrDefault<T>(this List<T> src);             // + (pred)

    // partitioning / ordering
    static List<T> Take<T>(this List<T> src, int n);
    static List<T> TakeWhile<T>(this List<T> src, Predicate<T> pred);
    static List<T> Skip<T>(this List<T> src, int n);
    static List<T> SkipWhile<T>(this List<T> src, Predicate<T> pred);
    static List<T> Reverse<T>(this List<T> src);
    static List<T> OrderBy<T>(this List<T> src, KeySelector<T> key);      // int key
    static List<T> OrderBy<T>(this List<T> src, KeySelector<T> k1,
                              KeySelector<T> k2);                          // 2-level
    static List<T> OrderByDescending<T>(this List<T> src, KeySelector<T> key);
    static List<T> OrderByStr<T>(this List<T> src, StrKeySelector<T> key); // string key
    static List<T> OrderByStrDescending<T>(this List<T> src, StrKeySelector<T> key);
    static List<T> OrderByNum<T>(this List<T> src, NumKeySelector<T> key); // double key
    static List<T> OrderByNumDescending<T>(this List<T> src, NumKeySelector<T> key);

    // grouping (order of groups follows first occurrence)
    static List<Grouping<T>> GroupBy<T>(this List<T> src, KeySelector<T> key);
    static List<Grouping<T>> GroupByStr<T>(this List<T> src, StrKeySelector<T> key);

    // set / membership (== semantics; use the *Str forms for string values,
    // or the EqualityComparer overloads for structural equality)
    static bool Contains<T>(this List<T> src, T target);
    static bool Contains<T>(this List<T> src, T target, EqualityComparer<T> eq);
    static bool ContainsStr(this List<string> src, string target);
    static List<T> Distinct<T>(this List<T> src);
    static List<T> Distinct<T>(this List<T> src, EqualityComparer<T> eq);
    static List<string> DistinctStr(this List<string> src);
    static List<T> In<T>(this List<T> src, List<T> values);      // SQL IN
    static List<T> In<T>(this List<T> src, List<T> values, EqualityComparer<T> eq);
    static List<string> InStr(this List<string> src, List<string> values);
    static List<string> Like(this List<string> src, string pattern); // SQL LIKE

    // aggregation
    static A Aggregate<T, A>(this List<T> src, A seed, Accumulator<T, A> f);
    static int Sum(this List<int> src);
    static int Sum<T>(this List<T> src, KeySelector<T> key);
    static double SumNum(this List<double> src);
    static double SumNum<T>(this List<T> src, NumKeySelector<T> key);
    static int Min(this List<int> src);                          // + (key)
    static double MinNum(this List<double> src);
    static int Max(this List<int> src);                          // + (key)
    static double MaxNum(this List<double> src);
    static double Average(this List<int> src);                   // + (key)
    static double AverageNum(this List<double> src);
    static List<T> ToList<T>(this List<T> src);
}
```

Differences from C# (by design or pending compiler work — see
`docs/bugs/generics-uniform-repr.md`):

- **Eager, not lazy**: every operator runs immediately and returns a `List`;
  there is no `IEnumerable<T>` pipeline.
- **Typed sort keys**: `OrderBy` takes an int key; string and double keys use
  the `OrderByStr` / `OrderByNum` names. Overloads that differ only in the
  delegate's return type would silently mis-bind an untyped lambda.
- **`*Num` aggregate names** for `List<double>` receivers (`SumNum`,
  `MinNum`, `MaxNum`, `AverageNum`) for the same overload-resolution reason.
- **String membership uses the `*Str` forms** (`ContainsStr`, `DistinctStr`,
  `InStr`): the generic forms compare references for strings. For other
  reference types, pass an `EqualityComparer<T>` delegate
  (`delegate bool EqualityComparer<T>(T a, T b)`) to `Contains`/`Distinct`/
  `In` for structural (C# `Equals`-style) semantics.
- **No `ToDictionary`** yet (blocked by a Dictionary generic-value bug).
- `Grouping<T>` exposes `Key` (string), `IntKey` (int) and `Items`.

## 4. Module Resolution Rules

### 4.1 Search Order

When the compiler encounters `using System.IO`:

1. **Project local**: `<project_root>/lib/System/IO.zan` or `<project_root>/lib/System/IO/mod.zan`
2. **Standard library**: `<stdlib_path>/System/IO.zan` or `<stdlib_path>/System/IO/mod.zan`

Project-local modules take priority (allowing stdlib overrides).

### 4.2 Module Entry Point

A module can be either:
- **Single file**: `System/IO.zan` — the file IS the module
- **Directory**: `System/IO/mod.zan` — `mod.zan` is the entry point, other `.zan` files in the directory are sub-modules

### 4.3 Native Dependency Resolution

When a module has a `native/` subdirectory:

1. Compiler checks target platform
2. Selects appropriate subdirectory (`win/`, `linux/`, `macos/`)
3. Compiles native binding `.zan` files
4. Links native libraries (`.dll`, `.so`, `.dylib`) at link time
5. Copies native libraries to output directory

### 4.4 Conditional Compilation

Platform-specific code uses `#if` directives:

```csharp
#if PLATFORM_WINDOWS
    [DllImport("kernel32.dll")]
    static extern int GetCurrentProcessId();
#elif PLATFORM_LINUX
    [CImport("unistd.h")]
    static extern int getpid();
#elif PLATFORM_MACOS
    [CImport("unistd.h")]
    static extern int getpid();
#endif
```

Predefined symbols:
- `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `PLATFORM_MACOS`
- `ARCH_X64`, `ARCH_ARM64`
- `DEBUG`, `RELEASE`

---

## 5. Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Namespace | PascalCase | `System.Collections` |
| Class | PascalCase | `HttpClient` |
| Struct | PascalCase | `Vector3` |
| Interface | IPascalCase | `IComparable` |
| Method | PascalCase | `ToString()` |
| Property | PascalCase | `Count` |
| Field (public) | PascalCase | `Width` |
| Field (private) | _camelCase | `_count` |
| Parameter | camelCase | `fileName` |
| Local variable | camelCase | `result` |
| Constant | PascalCase or UPPER_CASE | `MaxValue`, `PI` |
| Enum member | PascalCase | `Color.Red` |
| File name | PascalCase | `HttpClient.zan` |

---

## 6. Documentation Comments

Standard library modules use XML doc comments:

```csharp
/// <summary>
/// Reads all text from a file.
/// </summary>
/// <param name="path">The file path to read.</param>
/// <returns>The file contents as a string.</returns>
/// <exception cref="FileNotFoundException">If the file does not exist.</exception>
/// <example>
/// var text = File.ReadAllText("data.txt");
/// Console.WriteLine(text);
/// </example>
public static string ReadAllText(string path) {
    ...
}
```

The `zan doc` command generates HTML documentation from these comments.
