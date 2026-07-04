# Zan Application Binary Interface (ABI)

## 1. Overview

This document defines the binary interface for compiled Zan programs: object layout, calling conventions, vtable format, ARC operations, and FFI compatibility. Stability of this ABI is critical for interop with native libraries.

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
| `bool` | 1 byte | 1 | `i8` |
| `byte` | 1 byte | 1 | `i8` |
| `short` | 2 bytes | 2 | `i16` |
| `int` | 8 bytes | 8 | `i64` |
| `float` | 8 bytes | 8 | `double` |
| `char` | 4 bytes | 4 | `i32` (Unicode scalar) |
| `nint` | 8 bytes | 8 | `i64` (raw pointer) |

Note: `int` is 64-bit by default for safety and simplicity. 32-bit variants available as `int32`.

### 3.2 Struct Layout (Value Types)

Structs are laid out sequentially with natural alignment:

```csharp
struct Point {
    public float X;     // offset 0, size 8, align 8
    public float Y;     // offset 8, size 8, align 8
}
// total size: 16, alignment: 8
```

```csharp
struct Mixed {
    public byte A;      // offset 0, size 1
                        // 7 bytes padding
    public int B;       // offset 8, size 8
    public bool C;      // offset 16, size 1
                        // 7 bytes padding
}
// total size: 24, alignment: 8
```

`[repr("C")]` structs guarantee C ABI compatibility (same layout as C compiler would produce).

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
| `int` | `int64_t` | Direct |
| `int32` | `int32_t` | Direct |
| `float` | `double` | Direct |
| `float32` | `float` | Direct |
| `bool` | `int32_t` | 0/1 |
| `byte` | `uint8_t` | Direct |
| `string` (param) | `const char *` | Pin, get UTF-8 pointer |
| `string` (return) | `const char *` | Copy to managed string |
| `nint` | `void *` | Direct |
| `struct` (repr C) | C struct | Direct (by value or by pointer) |
| `delegate` | Function pointer | Pin callback delegate |

### 6.2 Struct Passing

| Size | Method |
|------|--------|
| ≤ 8 bytes | Register (by value) |
| 9-16 bytes | Register pair (by value) |
| > 16 bytes | Hidden pointer (caller allocates) |

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
