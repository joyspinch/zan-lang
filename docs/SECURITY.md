# Zan Security Specification

> ⚠️ **本文档部分章节描述的是设计目标而非当前实现，且引用了不存在的 API。**
> 2026-08-08 审计后修正如下，未逐节重写的部分请勿当作现状引用。

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
int x = arr[5];    // Runtime hard error: "array index out of bounds"
                   // (there is no IndexOutOfRangeException type; the guard
                   // prints the source location and aborts with status 70)
```

There is no unchecked access path: `unsafe` blocks are a no-op scope and there
is no `UnsafeGet` method (see §3).

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

Integer arithmetic wraps on overflow (two's complement) in every build
configuration — there is no overflow checking. There is no `int.MaxValue`
constant. The `checked`/`unchecked` keywords are accepted for C# source
compatibility but are **no-ops** (`src/compiler/parser.c`: "Overflow checking
is always off in this runtime (wrapping semantics), so both forms lower to a
plain expression"); `checked(x + 1)` wraps exactly like `x + 1`
(`tests/conformance/cs_b14_checked.zan`).

### 2.5 Uninitialized Variables

There is no definite-assignment analysis. An unassigned local simply reads as
its zero value (`0` / `false` / `""` for numeric, bool and string locals);
using it before assignment does not error.

```csharp
int x;
Console.WriteLine(x);  // prints 0 (no compile error)
```

---

## 3. Unsafe Code

### 3.1 Unsafe Scope

The language has **no pointer types** — the only raw-address type is `nint`
(an integer of pointer width). Heap access goes through the `NativeMemory`
facade (`stdlib/System/NativeMemory.zan`): `Alloc`/`Free`/`Copy`/`Fill`/
`Compare`/`GetString`/`PutString`/`Crc32`. The old `NativeAlloc`/`NativeFree`/
`GetRawPointer` names do not exist.

```csharp
unsafe {
    // `unsafe { }` is accepted but is a no-op scope — everything is treated
    // as "unsafe" already (src/compiler/parser.c).
    nint buf = NativeMemory.Alloc(1024);     // raw allocation
    NativeMemory.Fill(buf, 0, 1024);         // zero it
    NativeMemory.PutString(buf, 0, "hi", 2); // write bytes
    string s = NativeMemory.GetString(buf, 0, 2);
    NativeMemory.Free(buf);                  // manual deallocation
}
```

There is no pointer arithmetic, no `byte*` syntax, and no pointer casting.

### 3.2 Unsafe Functions

The `unsafe` modifier is accepted on functions, but — like `unsafe { }`
blocks — it is a no-op: there is no separate unsafe context to require or
enforce, and no raw pointer types to expose (`parser.c`: *everything is
"unsafe" here*).

```csharp
unsafe static void FastCopy(nint dst, nint src, int size) {
    NativeMemory.Copy(dst, src, size);
}
```

### 3.3 Unsafe Restrictions

The language-wide rules that apply regardless of `unsafe`:

- Cannot modify ARC refcount directly
- Cannot access private fields of other classes
- Cannot bypass the type system (no reinterpret_cast between unrelated types)
- Cannot disable the scheduler or corrupt task state

(There is no module-level `#[unsafe_allow]` attribute; module-level unsafe
declarations do not exist.)

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
static extern nint GetString();

// There is no Marshal class (no Marshal.PtrToString / Marshal.FreeNative).
// Copy the bytes out with NativeMemory.GetString(ptr, 0, len) — you must know
// the byte length — and call NativeMemory.Free(ptr) if ownership is yours.
nint ptr = GetString();
string result = NativeMemory.GetString(ptr, 0, len);
```

### 4.2 Callback Safety

When passing managed callbacks to native code:
- A delegate is marshaled as a plain C function pointer
- There is no `GCHandle`/GC pinning — keep a managed reference to the callback
  (ARC) alive for the duration of the native call; if the native side stores
  the callback beyond the call, you must ensure a managed reference outlives
  it, or the delegate may be freed

### 4.3 Struct Layout Verification

C-compatible layout is controlled with `[StructLayout]` (Sequential or
Explicit) plus `[FieldOffset(n)]` — there is no `[repr("C")]` attribute and no
`[StructSize(N)]` assertion.

```csharp
[StructLayout(Sequential)]   // default; plain structs already match C layout
struct POINT {
    public long X;
    public long Y;
}
```

Note: a struct containing `char` fields or 64-bit enums does **not** match the
C layout (see `docs/ABI.md` §3.2).

---

## 5. Concurrency Safety

### 5.1 Data Race Prevention

There is **no static data-race analysis** and no `ZAN2001`-style warning: the
compiler does not analyze sharing across tasks. Synchronizing access to shared
mutable state is the developer's responsibility. The shipped primitive is the
static, handle-style `Mutex` (`stdlib/System/Threading/Threading.zan`):

```csharp
nint mtx = Mutex.Create();
Mutex.Lock(mtx);
shared.Add(1);          // protected
Mutex.Unlock(mtx);
Mutex.Destroy(mtx);     // when no longer needed
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

There are **no numbered diagnostic codes** (no `ZAN1001`/`ZAN2001` family), no
`--warn-level` flag, and no `#pragma warning` directives: none of the tables
in the original draft of this section exist. Diagnostics are plain
`file:line:col: message` compiler messages, and the binary is **`zanc`** —
there is no `zan build` subcommand.

---

## 7. Supply Chain Security

### 7.1 Dependency Verification

When using external libraries:
- Source-based distribution (auditable)
- SHA256 hash verification for downloaded sources
- No binary-only dependencies in safe mode
- Dependency tree is explicit in `zan.proj`

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

- [ ] Mark all functions that use raw pointers as `unsafe` (`nint`-based APIs)
- [ ] Validate all input sizes before native calls
- [ ] Use `weak` references to break potential cycles
- [ ] Keep managed references alive for callbacks that outlive native call duration
- [ ] Verify `[StructLayout]` struct sizes match native expectations (mind the `char`/enum layout exception in `docs/ABI.md`)
- [ ] Document ownership semantics for native resources
- [ ] Provide safe wrappers around `NativeMemory` operations
- [ ] Run the runtime/leak checks: compile with `--check-leaks` and run the leakcheck test family (`scripts/test.ps1`)

