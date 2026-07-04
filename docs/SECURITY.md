# Zan Security Specification

## 1. Overview

Zan is designed for **memory safety by default** with an explicit `unsafe` escape hatch. The compiler enforces safety guarantees at compile time wherever possible, with minimal runtime checks where necessary.

**Security principles:**
- Safe by default — memory safety without developer effort
- Unsafe is explicit — raw pointer ops require `unsafe` blocks
- No undefined behavior — all safe code has defined semantics
- Defense in depth — multiple layers of protection

---

## 2. Memory Safety

### 2.1 ARC Guarantees

| Guarantee | Mechanism |
|-----------|-----------|
| No use-after-free | ARC ensures objects live as long as references exist |
| No double-free | Refcount reaches 0 exactly once → destructor called once |
| No dangling pointers | Weak references return null when target is deallocated |
| No memory leaks* | ARC + weak references break cycles |
| No buffer overflow | Array bounds checking at runtime |
| No null dereference | Nullable types require explicit null checks |

*Note: Reference cycles without `weak` can leak. The compiler emits warnings for likely cycles.

### 2.2 Bounds Checking

All array and collection access is bounds-checked:

```csharp
var arr = new int[] { 1, 2, 3 };
int x = arr[5];    // Runtime error: IndexOutOfRangeException
```

In performance-critical code, unchecked access is available via `unsafe`:

```csharp
unsafe {
    int x = arr.UnsafeGet(5);   // No bounds check — developer responsibility
}
```

### 2.3 Null Safety

Nullable types enforce null checking:

```csharp
string? name = GetName();

// Compile error: cannot use nullable without check
// int len = name.Length;

// Must check first
if (name != null) {
    int len = name.Length;      // OK — compiler knows name is non-null here
}

// Or use null-conditional
int? len = name?.Length;
int safeLen = name?.Length ?? 0;
```

### 2.4 Integer Overflow

By default, integer arithmetic wraps on overflow (like C#/Go). In debug builds, overflow is checked:

```csharp
int x = int.MaxValue;
x++;                    // Debug: OverflowException
                        // Release: wraps to int.MinValue

// Explicit checked/unchecked (future feature)
checked {
    x++;                // Always throws on overflow
}
```

### 2.5 Uninitialized Variables

The compiler enforces definite assignment:

```csharp
int x;
Console.WriteLine(x);  // Compile error: use of uninitialized variable 'x'

int y;
if (condition) {
    y = 42;
} else {
    y = 0;
}
Console.WriteLine(y);  // OK — all paths assign y
```

---

## 3. Unsafe Code

### 3.1 Unsafe Scope

The `unsafe` keyword creates a scope where safety checks are relaxed:

```csharp
unsafe {
    // Allowed in unsafe blocks:
    nint ptr = NativeAlloc(1024);       // Raw pointer allocation
    byte* data = (byte*)ptr;             // Pointer casting
    data[0] = 0xFF;                      // Pointer arithmetic
    NativeFree(ptr);                     // Manual deallocation

    // Calling unsafe functions
    UnsafeSort(array.GetRawPointer(), array.Count);
}
```

### 3.2 Unsafe Functions

Functions that require unsafe context must be marked:

```csharp
unsafe static void FastCopy(nint dst, nint src, int size) {
    byte* d = (byte*)dst;
    byte* s = (byte*)src;
    for (int i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

// Can only be called from unsafe context
unsafe {
    FastCopy(dest, source, 1024);
}
```

### 3.3 Unsafe Restrictions

Even in `unsafe` blocks, certain operations are still prevented:
- Cannot modify ARC refcount directly
- Cannot access private fields of other classes
- Cannot bypass type system (no reinterpret_cast between unrelated types)
- Cannot disable the scheduler or corrupt task state

### 3.4 Module-Level Unsafe

Modules that extensively use unsafe code can declare themselves unsafe:

```csharp
#[unsafe_allow("ptr", "native")]
namespace System.Runtime.Internal;

// All functions in this module can use unsafe operations
// without individual unsafe blocks
```

---

## 4. FFI Safety

### 4.1 String Marshaling Safety

When passing strings to native code:
- Temporary `const char*` is valid only for the duration of the call
- Compiler prevents storing native string pointers beyond the call
- UTF-8 encoding is verified before marshaling

```csharp
[DllImport("native.dll")]
static extern void ProcessString(string text);
// Compiler generates: pin string, get UTF-8 ptr, call, unpin

// UNSAFE: returning a native string requires explicit marshaling
[DllImport("native.dll")]
static extern unsafe nint GetString();

unsafe {
    nint ptr = GetString();
    string result = Marshal.PtrToString(ptr);
    Marshal.FreeNative(ptr);    // Developer must know ownership
}
```

### 4.2 Callback Safety

When passing managed callbacks to native code:
- Callback delegate is pinned for the duration of the native call
- If callback outlives the call, developer must explicitly pin:

```csharp
var callback = new CompareFunc((a, b) => { ... });
var handle = GCHandle.Pin(callback);    // Prevent GC/ARC collection

NativeSort(data, count, callback);

handle.Free();                           // Unpin when done
```

### 4.3 Struct Layout Verification

`[repr("C")]` structs are verified at compile time:
- Field alignment matches C ABI
- No managed references in repr(C) structs (use `nint` instead)
- Size matches expected C struct size (optional `[StructSize(N)]` assertion)

```csharp
[repr("C")]
[StructSize(16)]    // Compile error if actual size != 16
struct POINT {
    public long X;
    public long Y;
}
```

---

## 5. Concurrency Safety

### 5.1 Data Race Prevention

The compiler warns about potential data races:

```csharp
var shared = new List<int>();

Task.Run(() => {
    shared.Add(1);      // WARNING ZAN2001: potential data race on 'shared'
});

Task.Run(() => {
    shared.Add(2);      // WARNING ZAN2001: potential data race on 'shared'
});
```

**Fix:** Use synchronization:

```csharp
var mutex = new Mutex();
var shared = new List<int>();

Task.Run(async () => {
    using (await mutex.Lock()) {
        shared.Add(1);  // OK — mutex protected
    }
});
```

### 5.2 Channel Safety

Channels provide safe cross-task communication:
- Send/receive are atomic operations
- Channel close is safe (subsequent sends return error)
- No data races on channel internals

### 5.3 ARC Thread Safety

Reference counting uses atomic operations:
- `retain`: `atomic_fetch_add(&refcount, 1)`
- `release`: `atomic_fetch_sub(&refcount, 1)` → if 0, deallocate
- Destructor runs on the releasing thread

---

## 6. Compiler Security Checks

### 6.1 Error Codes

| Code | Category | Description |
|------|----------|-------------|
| ZAN1001 | Memory | Use of uninitialized variable |
| ZAN1002 | Memory | Potential null dereference |
| ZAN1003 | Memory | Potential use-after-free (weak ref without check) |
| ZAN1004 | Memory | Potential reference cycle (strong refs both ways) |
| ZAN1005 | Memory | Buffer size mismatch in FFI call |
| ZAN2001 | Concurrency | Potential data race on shared mutable state |
| ZAN2002 | Concurrency | Non-sendable type in task closure |
| ZAN2003 | Concurrency | Potential deadlock (lock ordering) |
| ZAN3001 | Type | Unsafe cast without explicit conversion |
| ZAN3002 | Type | Lossy numeric conversion |
| ZAN3003 | Type | Implicit nullable-to-non-nullable conversion |
| ZAN4001 | FFI | Missing null check on native return value |
| ZAN4002 | FFI | String lifetime exceeds native call |
| ZAN4003 | FFI | Managed reference in repr(C) struct |

### 6.2 Warning Levels

```bash
zan build --warn-level=0    # Errors only
zan build --warn-level=1    # + important warnings (default)
zan build --warn-level=2    # + informational warnings
zan build --warn-level=3    # All warnings (pedantic)
zan build --warnings-as-errors  # Treat all warnings as errors
```

### 6.3 Suppressing Warnings

```csharp
#pragma warning disable ZAN2001
var shared = state;         // I know what I'm doing
#pragma warning restore ZAN2001
```

---

## 7. Supply Chain Security

### 7.1 Dependency Verification

When using external libraries:
- Source-based distribution (auditable)
- SHA256 hash verification for downloaded sources
- No binary-only dependencies in safe mode
- Dependency tree is explicit in `project.zan`

### 7.2 Sandboxing

Future feature: module-level capability restrictions:

```
project MyApp {
    dependencies {
        untrusted_lib {
            source = "https://..."
            permissions {
                allow_net = false       // No network access
                allow_fs = false        // No filesystem access
                allow_unsafe = false    // No unsafe code
                allow_ffi = false       // No native calls
            }
        }
    }
}
```

---

## 8. Security Checklist for Library Authors

- [ ] Mark all functions that use raw pointers as `unsafe`
- [ ] Validate all input sizes before native calls
- [ ] Use `weak` references to break potential cycles
- [ ] Pin callbacks that outlive native call duration
- [ ] Verify `[repr("C")]` struct sizes match native expectations
- [ ] Document ownership semantics for native resources
- [ ] Provide safe wrappers around unsafe operations
- [ ] Test with address sanitizer (`zan build --sanitize=address`)

