# Zan Performance Specification

## 1. Performance Targets

### 1.1 Compilation Speed

| Metric | Target | Comparable |
|--------|--------|------------|
| Lexing throughput | > 100 MB/s | Faster than Clang lexer |
| Parsing throughput | > 50 MB/s | On par with Go compiler |
| Full pipeline (10K LOC) | < 1 second | Faster than Rust, similar to Go |
| Full pipeline (100K LOC) | < 10 seconds | Faster than C++ with templates |
| Incremental (1 file change) | < 500 ms | On par with Go |

### 1.2 Generated Code Performance

| Benchmark | Target | Baseline |
|-----------|--------|----------|
| Integer arithmetic | Within 5% of C | -O2 clang |
| Floating-point math | Within 5% of C | -O2 clang |
| Struct operations | Equal to C | Zero overhead |
| Virtual dispatch | Within 10% of C++ | vtable call |
| ARC overhead | < 5% vs manual | Compared to raw malloc/free |
| Array iteration | Within 10% of C | Bounds-checked |
| String operations | Within 20% of C | UTF-8, interned |
| Channel send/recv | < 200ns per op | Similar to Go |
| Task spawn | < 1μs per task | Similar to Go goroutine |
| Task switch | < 100ns | Userspace context switch |

### 1.3 Memory Usage

| Metric | Target |
|--------|--------|
| Compiler memory (10K LOC) | < 50 MB |
| Compiler memory (100K LOC) | < 500 MB |
| Runtime overhead per object | 16 bytes (refcount + type_info) |
| Task stack (initial) | 4 KB |
| Task stack (max) | 1 MB |
| Channel buffer (per element) | sizeof(T) + 8 bytes |
| Empty program binary | < 100 KB |

### 1.4 Binary Size

| Component | Target |
|-----------|--------|
| Compiler only (`zanc`) | < 5 MB |
| Compiler + IDE | < 20 MB |
| Runtime library | < 500 KB |
| Minimal "Hello World" binary | < 100 KB |
| Typical application (no GUI) | < 1 MB |

---

## 2. Optimization Strategy

### 2.1 LLVM Pass Pipeline

**Debug builds (`-O0`):**
```
- No optimization passes
- Full debug info
- Bounds checking enabled
- ARC retain/release calls preserved
- Integer overflow checking
```

**Default builds (`-O1`):**
```
- mem2reg (promote stack to registers)
- SROA (scalar replacement of aggregates)
- instcombine (instruction combining)
- simplifycfg (control flow simplification)
- ARC retain/release elision (basic)
```

**Release builds (`-O2`):**
```
- All -O1 passes
- GVN (global value numbering)
- loop-rotate, loop-unswitch, loop-reduce
- inline (function inlining, threshold 225)
- devirtualization (convert virtual calls to direct)
- ARC retain/release elision (aggressive)
- dead store elimination
- tail call optimization
```

**Aggressive (`-O3`):**
```
- All -O2 passes
- vectorization (SLP and loop vectorizer)
- higher inline threshold (375)
- polly (polyhedral optimization, if available)
```

**Size-optimized (`-Os`):**
```
- All -O2 passes
- lower inline threshold (75)
- merge functions
- strip debug info
```

### 2.2 ARC Optimizations

#### 2.2.1 Retain/Release Elision

```csharp
// Before optimization:
var x = new Foo();      // retain
var y = x;              // retain x, retain y
DoSomething(y);         // retain y (param), release y (param end)
// y scope end           // release y
// x scope end           // release x

// After optimization:
var x = new Foo();      // retain (initial)
var y = x;              // retain (shared)
DoSomething(y);         // NO retain/release (y still alive)
// y scope end           // release (last use)
// x scope end           // release → destroy
```

#### 2.2.2 Move Optimization

```csharp
// Before:
var result = CreateObject();  // retain
return result;                // retain for return, release local

// After (move):
var result = CreateObject();  // retain
return result;                // transfer ownership (no extra retain/release)
```

#### 2.2.3 Stack Promotion (Escape Analysis)

```csharp
// Heap allocated (escapes):
var list = new List<int>();
SaveReference(list);        // escapes — must be heap allocated

// Stack promoted (doesn't escape):
var temp = new StringBuilder();
temp.Append("hello");
temp.Append(" world");
var result = temp.ToString();
// temp doesn't escape — compiler allocates on stack, no ARC needed
```

### 2.3 Monomorphization

Generic types over value types are monomorphized (specialized per type):

```csharp
// Source:
T Max<T>(T a, T b) where T : IComparable<T> => a.CompareTo(b) > 0 ? a : b;

Max(1, 2);          // generates Max_int(int a, int b)
Max(3.14, 2.71);    // generates Max_double(double a, double b)
```

Benefits:
- No boxing of value types
- Direct calls instead of virtual dispatch
- Enables further inlining and optimization

### 2.4 Devirtualization

The compiler converts virtual calls to direct calls when the concrete type is known:

```csharp
var circle = new Circle(5.0);   // concrete type known
circle.Area();                   // direct call (not virtual dispatch)

IDrawable shape = GetShape();   // concrete type unknown
shape.Area();                    // virtual dispatch (vtable)
```

### 2.5 String Optimizations

- **Small String Optimization (SSO):** strings ≤ 23 bytes stored inline (no heap allocation)
- **Interning:** identical string literals share the same memory
- **Copy-on-Write:** string builder uses COW for efficient concatenation
- **Format caching:** `$"Hello {name}"` reuses format template

---

## 3. Benchmarking

### 3.1 Benchmark Suite

Located in `tests/benchmarks/`:

| Benchmark | What it measures |
|-----------|-----------------|
| `fibonacci.zan` | Function call overhead, recursion |
| `mandelbrot.zan` | Floating-point computation |
| `nbody.zan` | Struct operations, math |
| `binary_trees.zan` | ARC allocation/deallocation |
| `spectral_norm.zan` | Array access, floating-point |
| `fannkuch.zan` | Integer computation, array |
| `echo_server.zan` | Channel throughput, task scheduling |
| `producer_consumer.zan` | Concurrent pipeline performance |
| `json_parse.zan` | String processing, collections |
| `sort_large.zan` | Generic algorithm performance |

### 3.2 Benchmark Protocol

```bash
# Run all benchmarks
zan bench

# Run specific benchmark
zan bench tests/benchmarks/fibonacci.zan

# Compare with previous results
zan bench --compare baseline.json

# Export results
zan bench --output results.json
```

**Rules:**
- Run 5 iterations minimum, report median
- Warm up for 1 iteration before measuring
- Report: time (ms), peak memory (MB), binary size (KB)
- Compare against: C (clang -O2), Go, C# (NativeAOT)

### 3.3 Benchmark Format

```csharp
// BENCH: fibonacci
// ITERATIONS: 1000000
using System;

class Benchmark {
    static int Fib(int n) {
        if (n <= 1) return n;
        return Fib(n - 1) + Fib(n - 2);
    }

    static void Main() {
        int result = 0;
        for (int i = 0; i < 1000000; i++) {
            result += Fib(20);
        }
        Console.WriteLine(result);
    }
}
```

---

## 4. Profiling Tools

### 4.1 Compiler Profiling

```bash
zan build --time-report
# Phase            Time (ms)   Memory (MB)
# Lexer            12          2.1
# Parser           45          15.3
# Binder           23          8.7
# Checker          67          22.4
# IRGen            89          35.1
# LLVM Opt         156         48.2
# LLVM Codegen     234         52.0
# Linking          45          -
# Total            671         52.0 (peak)
```

### 4.2 Runtime Profiling

```bash
# Built-in sampling profiler
zan run --profile myapp.zan
# Writes profile.json (Chrome DevTools format)

# Memory profiling
zan run --mem-profile myapp.zan
# Reports: allocations, peak memory, ARC retain/release counts

# Concurrency profiling
zan run --task-profile myapp.zan
# Reports: task count, channel operations, scheduler stats
```

### 4.3 Sanitizers

```bash
# Address sanitizer (detect buffer overflow, use-after-free)
zan build --sanitize=address

# Thread sanitizer (detect data races)
zan build --sanitize=thread

# Memory sanitizer (detect uninitialized reads)
zan build --sanitize=memory

# Undefined behavior sanitizer
zan build --sanitize=undefined
```

---

## 5. Performance Regression Prevention

### 5.1 CI Benchmarks

Every PR runs the benchmark suite. Regressions > 5% block merge.

### 5.2 Compilation Speed Tracking

Track compilation speed of the compiler itself (self-compile time) and standard benchmark programs across commits.

### 5.3 Binary Size Tracking

Track output binary sizes. Increases > 10% require justification.

### 5.4 Memory Usage Tracking

Track peak memory during compilation of standard test programs. Increases > 20% require investigation.

