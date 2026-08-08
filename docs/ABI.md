# Zan Application Binary Interface (ABI)

## 1. Overview

This document defines the binary interface for compiled Zan programs: object layout, calling conventions, vtable format, ARC operations, and FFI compatibility. Stability of this ABI is critical for interop with native libraries.

### 1.1 Document status (layering)

This document is layered into **current implementation** and **target design**:

- **Current implementation** — §2 (calling convention), §3.1 (primitive layout), §3.2
  (struct layout, incl. `[StructLayout]` explicit layout with `[FieldOffset(n)]`),
  and §6 (FFI marshaling / struct passing / sret+byval). These were re-verified
  against `src/compiler/{irgen.c,irgen_abi.c,binder.c}` and by compiling probes
  (2026-07-29). Notable facts that differ from older drafts: **`int` is 32-bit**
  (`i32`; use `long` for 64-bit), `float` is **32-bit** (`float`; `double` is
  64-bit), `char` lowers to a 64-bit word, and the SysV/Win64 struct-passing +
  `sret`/`byval` classification is fully implemented in `irgen_abi.c`.
- **Target design (not yet all true)** — §3.3–§3.5 (reference/string/array object
  headers), §4 (vtable / witness tables), §5 (ARC pseudocode), §7 (task/channel).
  These describe the intended runtime object model and were **not** re-verified in
  this pass; treat offsets/fields as design intent, not a guaranteed layout.

> The object header actually emitted today (16 bytes in front of every payload:
> `[i64 rc / array count @ obj-16][i64 leak-site / STRING_MAGIC @ obj-8]`, with
> the program pointer pointing at the payload) is defined once, as constants, in
> `src/common/zan_abi.h` — that header is the source of truth for the current
> implementation; §3.3–§3.5 are the longer-term design.

Remaining ABI capability work is tracked in `TASKS.md` (A0-2 FFI bit-width, A0-3
struct alignment corner cases, A2 variadic `DllImport` + `signext`/`zeroext`).

---

## 2. Calling Convention

Zan uses the **platform-native C calling convention** for all exported functions:

| Platform | Convention | Register Args | Stack Cleanup |
|----------|-----------|--------------|---------------|
| Windows x64 | Microsoft x64 | RCX, RDX, R8, R9 (int); XMM0-3 (float) | Caller |
| Linux x64 | System V AMD64 | RDI, RSI, RDX, RCX, R8, R9 (int); XMM0-7 (float) | Caller |
| ARM64 | AAPCS64 | X0-X7 (int); D0-D7 (float) | Caller |

Internal Zan functions also use the native convention (no custom ABI).

**Return values:**
- Primitives: register (RAX / XMM0)
- Small structs (≤ 16 bytes): register pair
- Large structs: caller allocates, pointer passed as hidden first argument

---

## 3. Type Layout

### 3.1 Primitive Types

| Zan Type | Size | Alignment | LLVM Type |
|----------|------|-----------|-----------|
| `bool` | 1 byte | 1 | `i1` (value); 1-byte storage |
| `byte` / `sbyte` | 1 byte | 1 | `i8` |
| `short` / `ushort` | 2 bytes | 2 | `i16` |
| `int` / `uint` | 4 bytes | 4 | `i32` |
| `long` / `ulong` | 8 bytes | 8 | `i64` |
| `float` | 4 bytes | 4 | `float` |
| `double` / `decimal` | 8 bytes | 8 | `double` |
| `char` | 8 bytes | 8 | `i64` (Unicode scalar) |
| `nint` | 8 bytes | 8 | `i64` (raw pointer width) |

Notes (`src/compiler/binder.c`, `irgen.c` `map_type`):
- `int` is **32-bit**; use `long` for 64-bit. There is **no** `int32` / `float32`
  type — the width-specific numeric types are the C# set above.
- `float` is **32-bit**; `double` is 64-bit. `decimal` is currently an alias of
  `double`.
- `char` currently lowers to a 64-bit word (wider than C#'s 16-bit `char`); it
  still holds a single Unicode scalar value.
  Note: the binder records a size of 4 for `char` (`src/compiler/binder.c`,
  `make_type(..., "char", 4)`), which conflicts with the 8-byte LLVM storage
  that `irgen` actually emits. **The storage width (8 bytes) is authoritative**
  for layout and ABI purposes; treat the binder's 4 as internal bookkeeping.

### 3.2 Struct Layout (Value Types)

Structs are laid out sequentially with natural alignment:

```csharp
struct Point {
    public float X;     // offset 0, size 4, align 4
    public float Y;     // offset 4, size 4, align 4
}
// total size: 8, alignment: 4
```

```csharp
struct Mixed {
    public byte A;      // offset 0, size 1
                        // 3 bytes padding
    public int B;       // offset 4, size 4 (int is 32-bit)
    public bool C;      // offset 8, size 1
                        // 3 bytes padding
}
// total size: 12, alignment: 4
```

(Use `long`/`double` fields for 8-byte members.)

Layout control (`src/compiler/irgen.c` `register_struct_type`):

- **Sequential (default)** — fields are laid out in declaration order and LLVM
  applies natural C padding/alignment, so a plain struct already matches the
  layout a C compiler would produce (this is also what `[StructLayout(Sequential)]`
  asks for). Base fields are flattened in first, so a derived instance stays
  layout-compatible with its base.
  **Exception**: a struct containing `char` fields (mapped to 8-byte LLVM
  words, whereas C's `char` is 1 byte) or a 64-bit enum (lowered to `i64`,
  whereas C enums are typically `int`) does **not** match the layout a C
  compiler would produce.
- **Explicit** — a `[StructLayout(Explicit)]` (explicit-layout) type places every
  field at its own `[FieldOffset(n)]`. Offsets are validated for the field's
  natural alignment; two fields may share an offset (that is how a union is
  written). An explicit-layout type may **not** have virtual methods (it has no
  vtable slot to place).

### 3.3 Reference Type Object Layout

All reference types (class instances) have this header:

```
┌────────────────────────────────────────────┐
│  Byte Offset  │  Field         │  Size     │
├───────────────┼────────────────┼───────────┤
│  0            │  refcount      │  8 bytes  │  atomic i64
│  8            │  type_info_ptr │  8 bytes  │  ptr to TypeDescriptor
│  16           │  weak_ref_ptr  │  8 bytes  │  ptr to WeakRefSlot (or NULL)
│  24           │  fields...     │  varies   │  instance fields
└────────────────────────────────────────────┘
```

**Total header: 24 bytes** before first user field.

A reference (pointer to an object) always points to the `refcount` field (byte 0 of the allocation).

### 3.4 String Layout

Strings are immutable reference-counted objects:

```
┌────────────────────────────────────────────┐
│  0   │  refcount       │  8 bytes          │  atomic i64
│  8   │  type_info_ptr  │  8 bytes          │  → String TypeDescriptor
│  16  │  weak_ref_ptr   │  8 bytes          │  NULL (strings don't need weak)
│  24  │  length         │  8 bytes          │  byte count (UTF-8)
│  32  │  hash           │  8 bytes          │  cached hash (0 = not computed)
│  40  │  data[0..len]   │  length+1 bytes   │  UTF-8 bytes + NUL terminator
└────────────────────────────────────────────┘
```

Small String Optimization (SSO): strings ≤ 23 bytes stored inline in a tagged union, no heap allocation.

### 3.5 Array Layout

Dynamic arrays use Copy-on-Write:

```
┌────────────────────────────────────────────┐
│  0   │  refcount       │  8 bytes          │  atomic i64 (COW sharing)
│  8   │  length         │  8 bytes          │  element count
│  16  │  capacity       │  8 bytes          │  allocated capacity
│  24  │  elem_size      │  8 bytes          │  size of each element
│  32  │  data[0..]      │  length*elem_size │  element storage
└────────────────────────────────────────────┘
```

---

## 4. Virtual Dispatch

### 4.1 TypeDescriptor

Every reference type has a static `TypeDescriptor`:

```
┌────────────────────────────────────────────┐
│  0   │  type_id        │  8 bytes          │  unique type identifier
│  8   │  type_name      │  ptr              │  → type name string
│  16  │  parent_type    │  ptr              │  → parent TypeDescriptor (or NULL)
│  24  │  instance_size  │  8 bytes          │  total object size
│  32  │  destructor     │  ptr              │  → destructor function (or NULL)
│  40  │  vtable_count   │  8 bytes          │  number of virtual methods
│  48  │  vtable[0]      │  ptr              │  → first virtual method
│  56  │  vtable[1]      │  ptr              │  → second virtual method
│  ... │  ...            │  ...              │
│  48+8n│ iface_table    │  ptr              │  → interface witness tables
└────────────────────────────────────────────┘
```

### 4.2 Virtual Method Call

```c
// obj->SomeVirtualMethod(arg1, arg2)
// →
TypeDescriptor *td = *(TypeDescriptor **)(((char *)obj) + 8);
void (*method)(void *, int, int) = (void (*)(void *, int, int))td->vtable[slot_index];
method(obj, arg1, arg2);
```

### 4.3 Interface Witness Table

Each (type, interface) pair has a witness table:

```
┌────────────────────────────────────────────┐
│  0   │  interface_id   │  8 bytes          │
│  8   │  method_count   │  8 bytes          │
│  16  │  methods[0]     │  ptr              │  → concrete implementation
│  24  │  methods[1]     │  ptr              │
│  ... │  ...            │                   │
└────────────────────────────────────────────┘
```

Interface dispatch:
```c
// IDrawable shape = ...;
// shape.Draw();
// →
WitnessTable *wt = find_witness(obj_type_info, IDrawable_id);
void (*draw)(void *) = (void (*)(void *))wt->methods[Draw_slot];
draw(obj);
```

---

## 5. ARC Operations

### 5.1 Retain

```c
void zan_retain(void *obj) {
    if (obj == NULL) return;
    int64_t *rc = (int64_t *)obj;  // refcount is at offset 0
    atomic_fetch_add(rc, 1);
}
```

### 5.2 Release

```c
void zan_release(void *obj) {
    if (obj == NULL) return;
    int64_t *rc = (int64_t *)obj;
    if (atomic_fetch_sub(rc, 1) == 1) {
        // refcount was 1, now 0 → destroy
        TypeDescriptor *td = *(TypeDescriptor **)((char *)obj + 8);
        if (td->destructor) {
            td->destructor(obj);
        }
        // Release weak ref slot
        WeakRefSlot *ws = *(WeakRefSlot **)((char *)obj + 16);
        if (ws) {
            atomic_store(&ws->target, NULL);
        }
        free(obj);
    }
}
```

### 5.3 Weak Reference

```c
typedef struct {
    _Atomic(void *) target;     // NULL when target deallocated
    _Atomic(int64_t) refcount;  // weak ref slot refcount
} WeakRefSlot;

void *zan_weak_load(WeakRefSlot *slot) {
    void *obj = atomic_load(&slot->target);
    if (obj != NULL) {
        zan_retain(obj);        // try to retain
        // Double-check: object might have been freed between load and retain
        if (atomic_load((int64_t *)obj) <= 0) {
            // Object was freed, undo retain
            return NULL;
        }
    }
    return obj;                 // NULL or retained strong reference
}
```

---

## 6. FFI ABI Compatibility

### 6.1 DllImport Marshaling

| Zan Type | C ABI Type | Marshaling |
|----------|-----------|------------|
| `int` / `uint` | `int32_t` / `uint32_t` | Direct |
| `long` / `ulong` | `int64_t` / `uint64_t` | Direct |
| `short` / `ushort` | `int16_t` / `uint16_t` | Direct |
| `byte` / `sbyte` | `uint8_t` / `int8_t` | Direct |
| `float` | `float` | Direct |
| `double` | `double` | Direct |
| `bool` | `i1` | 0/1 |
| `char` | 64-bit word | Direct (Unicode scalar) |
| `string` (param) | `const char *` | Pass UTF-8 data pointer (NUL-terminated) |
| `string` (return) | `const char *` | The returned pointer is adopted as the managed string's bytes (**not** a deep copy) |
| `nint` | `void *` | Direct |
| `struct` (by value) | C struct | Per SysV/Win64 classification (see §6.2) |
| `delegate` | Function pointer | Pass callback thunk |

There is no `int32` / `float32` marshaling type; the width-specific numeric
types are those listed above (`int` = 32-bit, `long` = 64-bit, `float` = 32-bit,
`double` = 64-bit).

### 6.2 Struct Passing

Implemented per target platform in `src/compiler/irgen_abi.c`:

| Size | Method (summary) |
|------|--------|
| ≤ 8 bytes | Register (by value) |
| 9-16 bytes | Register pair (by value) |
| > 16 bytes | Indirect: caller-allocated copy passed by pointer (`byval` on SysV); returns via `sret` |

On **Linux/macOS (SysV AMD64)** the first 16 bytes are classified per-eightbyte
into INTEGER/SSE/MEMORY and memory arguments carry the `byval(ty)` attribute; on
**Windows x64** there is no `byval`, so a large struct is passed as a pointer to a
caller-made copy and returned through an `sret` pointer. Extern functions that
pass certain structs by value where no stable C ABI mapping exists are
diagnosed at compile time.

### 6.3 String Lifetime in FFI

```csharp
[DllImport("lib.dll")]
static extern void ProcessString(string text);
// Generated:
// 1. Pin managed string
// 2. Get UTF-8 const char* pointer
// 3. Call native function
// 4. Unpin
// The const char* is ONLY valid during the call
```

---

## 7. Task ABI

### 7.1 Task Stack Frame

```
┌────────────────────────────────────────────┐
│  0   │  stack_bottom    │  ptr             │  start of stack segment
│  8   │  stack_top       │  ptr             │  current stack pointer
│  16  │  stack_limit     │  ptr             │  end of current segment
│  24  │  state           │  i32             │  0=created,1=running,2=blocked,3=done
│  28  │  padding         │  i32             │
│  32  │  context         │  ptr             │  saved CPU registers
│  40  │  result          │  ptr             │  result value (when done)
│  48  │  continuation    │  ptr             │  next task to schedule
└────────────────────────────────────────────┘
```

### 7.2 Channel Buffer

```
┌────────────────────────────────────────────┐
│  0   │  buffer          │  ptr             │  circular buffer
│  8   │  capacity        │  i64             │  buffer size
│  16  │  head            │  atomic i64      │  read position
│  24  │  tail            │  atomic i64      │  write position
│  32  │  elem_size       │  i64             │  size per element
│  40  │  closed          │  atomic i32      │  0=open, 1=closed
│  44  │  padding         │  i32             │
│  48  │  send_waiters    │  ptr             │  blocked senders list
│  56  │  recv_waiters    │  ptr             │  blocked receivers list
│  64  │  mutex           │  platform_mutex  │  synchronization
└────────────────────────────────────────────┘
```
