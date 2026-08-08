# Zan Concurrency Model

> ⚠️ **本文档为设计目标与当前实现混写。** 2026-08-08 审计：§2.1 与 §8.3 描述现状，
> 其余章节多为设计目标（其中引用的部分 API 不存在）。修正见文内，未逐节重写。

## 1. Overview

Zan provides **Go-style native concurrency** with lightweight tasks (coroutines) and typed channels, combined with C#-style async/await syntax. The runtime includes a work-stealing scheduler for efficient parallel execution.

**Design goals:**
- Native support — concurrency is a first-class language feature, not a library bolt-on
- Lightweight — millions of concurrent tasks on modest hardware
- Safe — no data races, channel-based communication
- Familiar — async/await syntax from C#, goroutine/channel concepts from Go
- Performant — work-stealing scheduler, lock-free queues

---

## 2. Task Model (Lightweight Coroutines)

### 2.1 Spawning Tasks

> **Implemented surface.** The rest of this document is the design target; the
> shipped task API is exactly the following, and it takes an *async call* rather
> than a lambda (a spawned coroutine is a state machine the compiler lowers from
> a named `async` method):
>
> | Form | Meaning |
> |------|---------|
> | `long h = Task.Spawn(Worker(x))` | run `Worker` as a detached coroutine, return its handle |
> | `long h = Task.Run(Worker(x))` | same lowering under this document's name |
> | `await Task.Delay(ms)` | suspend this coroutine for `ms` (sleeps at a non-async root) |
> | `await Task.WhenAll(handles)` | suspend until every handle in a `List<long>` has completed |
> | `await Task.WhenAny(handles)` | suspend until one has completed; yields its index |
> | `Task.IsDone(h)` | 1 once that coroutine finished (also 1 for a handle whose frame was reaped) |
> | `Task.Cancel(h)` / `Task.IsCancellationRequested()` | cooperative cancellation, propagated down the await chain |
> | `TaskJoin.CancelAll(handles)` | cancel every handle not yet finished |
>
> A handle is not a `Task<T>`: the joins observe *completion only*: no result,
> no exception hand-off. A spawned coroutine publishes its result through shared
> state (a static field, a queue, `System.Threading.BlockingQueue`) that the
> joiner reads after the join. `TaskGroup`, lambda-form `Task.Run` and
> `CancellationToken` parameters below are not implemented yet.
>
> `Channel` **is** implemented, but it is **string-only** (non-generic):
> `Send(string)` / `async string Receive()`; a closed-and-drained channel
> returns `""` (`stdlib/System/Threading/Threading.zan:738`).

```csharp
using System;

class Program {
    static async int Work(int ms, string tag) {
        await Task.Delay(ms);
        Console.WriteLine("done: " + tag);
        return ms;
    }

    static async int Run() {
        // fan out: each spawn is an independent lightweight coroutine
        List<long> tasks = new List<long>();
        tasks.Add(Task.Spawn(Program.Work(30, "a")));
        tasks.Add(Task.Spawn(Program.Work(10, "b")));

        // aggregate: suspends until both have finished
        await Task.WhenAll(tasks);
        return 0;
    }

    static async void Main() { await Program.Run(); }
}
```

### 2.2 Task vs Thread

| Feature | Task | Thread |
|---------|------|--------|
| Creation cost | ~200 bytes stack | ~1MB stack |
| Context switch | ~100ns (userspace) | ~1μs (kernel) |
| Max concurrent | Millions | Thousands |
| Scheduling | M:N (runtime) | 1:1 (OS) |
| Stack | Segmented/growable | Fixed size |

Tasks are multiplexed onto a pool of OS threads (M:N scheduling). The runtime manages scheduling, so creating millions of tasks is practical.

> The stack/creation-cost figures are design targets; the shipped model is
> stackless CPS frames on the heap (see §5).

### 2.3 Structured Concurrency

```csharp
// TaskGroup ensures all child tasks complete before continuing
await TaskGroup.Run(async group => {
    group.Spawn(async () => await FetchUserData());
    group.Spawn(async () => await FetchOrderHistory());
    group.Spawn(async () => await FetchRecommendations());
});
// All three tasks complete before this line
Console.WriteLine("All data fetched");

// With results
var results = await TaskGroup.Run<string>(async group => {
    group.Spawn(async () => await DownloadPage("page1.html"));
    group.Spawn(async () => await DownloadPage("page2.html"));
    group.Spawn(async () => await DownloadPage("page3.html"));
});
// results = ["page1 content", "page2 content", "page3 content"]
```

### 2.4 Cancellation

```csharp
var cts = new CancellationTokenSource();

var task = Task.Run(async (CancellationToken ct) => {
    while (!ct.IsCancellationRequested) {
        await ProcessNextItem();
    }
}, cts.Token);

// Cancel after 5 seconds
await Task.Delay(5000);
cts.Cancel();
await task;
```

---

## 3. Channels (Go-style Communication)

> **Design target.** The shipped `Channel` (`stdlib/System/Threading/Threading.zan:738`)
> is **string-only** (non-generic): `Send(string)` / `async string Receive()`,
> closed-and-drained receive returns `""`. The generic `Channel<T>`,
> capacity-constructor generics, `Channel.Select` and tuple returns in this
> section are **not implemented**.

### 3.1 Unbuffered Channel

```csharp
// Unbuffered: sender blocks until receiver is ready
var ch = new Channel<int>();

Task.Run(async () => {
    await ch.Send(42);          // blocks until someone receives
    await ch.Send(100);
    ch.Close();
});

// Receive
while (await ch.Receive() is (true, var value)) {
    Console.WriteLine(value);   // prints 42, then 100
}
```

### 3.2 Buffered Channel

```csharp
// Buffered: sender blocks only when buffer is full
var ch = new Channel<string>(capacity: 10);

// Producer
Task.Run(async () => {
    for (int i = 0; i < 100; i++) {
        await ch.Send($"message {i}");
    }
    ch.Close();
});

// Consumer
await foreach (var msg in ch) {
    Console.WriteLine(msg);
}
```

### 3.3 Select (Multi-channel Wait)

```csharp
var dataCh = new Channel<byte[]>();
var errCh = new Channel<string>();
var doneCh = new Channel<bool>();

while (true) {
    switch (await Channel.Select(dataCh, errCh, doneCh)) {
        case (0, byte[] data):
            ProcessData(data);
            break;
        case (1, string error):
            Console.Error(error);
            break;
        case (2, _):
            Console.WriteLine("Done!");
            return;
    }
}
```

### 3.4 Channel Patterns

> **Design target.** The `Task.Run(lambda)` / `await foreach` forms below do
> **not** compile: `Task.Run` lowering in `irgen` (`src/compiler/irgen_call.c`)
> only accepts a *named async call* as its single argument, not a lambda, and
> there are no async iterators.

```csharp
// Fan-out: one producer, multiple consumers
static async Task FanOut<T>(Channel<T> input, int workers, Func<T, Task> handler) {
    var tasks = new List<Task>();
    for (int i = 0; i < workers; i++) {
        tasks.Add(Task.Run(async () => {
            await foreach (var item in input) {
                await handler(item);
            }
        }));
    }
    await Task.WhenAll(tasks);
}

// Pipeline: chain of processing stages
static Channel<U> Pipeline<T, U>(Channel<T> input, Func<T, U> transform) {
    var output = new Channel<U>(capacity: 10);
    Task.Run(async () => {
        await foreach (var item in input) {
            await output.Send(transform(item));
        }
        output.Close();
    });
    return output;
}

// Usage
var urls = new Channel<string>(100);
var pages = Pipeline(urls, url => Http.Get(url));
var parsed = Pipeline(pages, page => ParseHtml(page));
```

---

## 4. Synchronization Primitives

### 4.1 Mutex

`Mutex` is **static and handle-based** — there is no `new Mutex()` and no
awaitable `Lock()`:

```csharp
nint mtx = Mutex.Create();
Mutex.Lock(mtx);
counter++;
Mutex.Unlock(mtx);
Mutex.Destroy(mtx);     // when no longer needed
```

(`stdlib/System/Threading/Threading.zan`; the handle is `nint` on Windows,
`string` on POSIX.)

### 4.2 RWLock (Read-Write Lock)

There is no plain `RWLock`. The shipped read-write lock is **`AsyncRwLock`**
(`stdlib/System/Threading/AsyncRwLock.zan`), writer-preferring and genuinely
async (waiters suspend on `AsyncGate`, no polling):

```csharp
var rwlock = new AsyncRwLock();
await rwlock.EnterRead();
...
rwlock.ExitRead();
// exclusive write side:
await rwlock.EnterWrite();
...
rwlock.ExitWrite();
```

The `ReadLock()`/`WriteLock()` + `using` forms in the original draft are a
design target.

### 4.3 Semaphore

`Semaphore` is **static and handle-based** — there is no `new Semaphore(n)`
and no instance `Acquire()`:

```csharp
// Limit concurrent connections to 10
nint sem = Semaphore.Create(10, 10);   // (initial count, max count)
Semaphore.Wait(sem);                   // decrement; blocks at 0
try {
    ...
} finally {
    Semaphore.Release(sem);
}
```

(`stdlib/System/Threading/Threading.zan`.)

### 4.4 Atomic Operations

```csharp
var counter = new AtomicInt(0);

Task.Run(() => {
    counter.Increment();                    // atomic ++
    counter.Add(5);                         // atomic +=
    int old = counter.CompareExchange(5, 10); // replace 5 with 10, return observed value
    int val = counter.Load();               // atomic read
});
```

`AtomicInt` is a sequentially-consistent 64-bit integer shared by tasks running
on different scheduler workers in the same process.

### 4.5 Cross-Process SharedTable

```csharp
var table = new SharedTable("server-stats", 1024);
table.ColumnInt("requests");
table.ColumnFloat("load");
table.ColumnString("status", 32);
if (!table.Create()) {
    table = SharedTable.Open("server-stats");
}

table.Increment("worker-1", "requests", 1);
table.SetFloat("worker-1", "load", 0.75);
table.SetString("worker-1", "status", "ready");
```

`SharedTable` uses named shared memory, a fixed int/float/string schema, and
row-level locking. Updates to different rows can proceed concurrently, while
each operation remains atomic across threads and processes. The creator calls
`Destroy()` when the shared table is no longer needed; other processes attach
with `SharedTable.Open(name)` and call `Close()` when done.

### 4.6 Once (One-time Initialization)

There is no `Once` type. One-time initialization is done with a plain `bool`
guarded by a `Mutex`/`Gate`.

---

## 5. Runtime Scheduler Architecture

### 5.1 Coroutine driver (current implementation)

The shipped runtime is a **stackless coroutine driver** (`src/runtime/rt_co.h`):
each coroutine is a heap-allocated CPS frame (the compiler emits a `ramp`
entry and a `$resume` continuation per async function; see
`docs/ASYNC_CPS_DESIGN.md`). `rt_co` keeps a cooperative ready queue; IO
awaits register one-shot watchers with the `rt_io` reactor (`src/runtime/rt_io.c`),
which resumes the coroutine when its fd is ready. The default driver is
single-threaded; OS-worker parallelism is opt-in via the **`--async-workers`**
compiler flag, which links `zanrt_io_mt` (an OS worker pool) instead.

There is **no `Runtime.SetMaxWorkers`** — the worker count is controlled by
the `--async-workers` CLI flag, not at runtime.

### 5.2 Work-Stealing / Preemption / Segmented Stacks (design targets)

The original draft's M:N work-stealing picture is **not** the shipped
implementation:

- **No per-worker local deque / work-stealing**: the single-threaded driver
  uses one cooperative ready queue; with `--async-workers`, `zanrt_io_mt`
  provides the same ABI on an OS worker pool.
- **No ~10μs preemption checks**: scheduling is cooperative at `await` points
  only; there is no periodic preemption of compute-bound coroutines.
- **No segmented stacks (4KB → 1MB)**: coroutines are stackless — their state
  lives in heap frames, so there are no stack segments to grow or shrink.

Task states (Created → Running → Blocked on IO/await → Done) do exist, but the
transitions are cooperative (await/wake) rather than preemptive.

---

## 6. Async/Await Integration

### 6.1 Async Functions

```csharp
async string FetchData(string url) {
    var response = await Http.Get(url);       // yields to scheduler
    var body = await response.ReadString();   // yields again
    return body;
}
```

The actual model is **not** a `Task<T>` state machine with `ContinueWith`
(see `docs/ASYNC_CPS_DESIGN.md`): an async function must return `int`,
`string` or `void`, and the compiler lowers it to a heap **CPS frame** — a
`ramp` entry that allocates the frame, plus a `$resume` continuation, with the
body split at each `await` into resume blocks. There is no `Task<T>` type, no
`ContinueWith`, and no `.Result`-style access; async results are returned by
the CPS frame's result slot and joined through `Task.IsDone`/`Task.WhenAll`/
`Task.WhenAny` (completion-only, see §2.1).

### 6.2 Async Iterators

> **Design target.** There are no async iterators: no `IAsyncEnumerable`, no
> `await foreach`, and `yield` inside an `async` function is not supported.
> The example below is not compilable.

---

## 7. Thread Safety Model

> **Design target.** There is no sendability analysis and no compiler warning
> about sharing mutable state across tasks (§7.1–§7.2 describe an analysis
> that does not exist); synchronizing shared state is the developer's
> responsibility (see §4).

### 7.1 Sendable Types

Types that can be safely shared between tasks:

| Type | Sendable? | Notes |
|------|-----------|-------|
| Value types (struct, int, etc.) | Yes | Copied by value |
| Immutable reference types | Yes | No mutation possible |
| `class` with `Mutex` protection | Yes | Developer responsibility |
| `class` without synchronization | No | Compiler warning |
| Channels | Yes | Thread-safe by design |
| AtomicInt | Yes | Lock-free 64-bit operations |
| SharedTable | Yes | Fixed-schema, cross-process atomic operations |

### 7.2 Compiler Checks

> **Design target.** The warnings described in the original draft (non-sendable
> type captured by a task closure, mutable reference type shared without
> synchronization, channel carrying a non-sendable type) are **not emitted** —
> there is no such analysis.

```csharp
class Counter {
    public int Value;       // mutable field
}

var c = new Counter();
Task.Run(() => {
    c.Value++;              // no warning is emitted today
});
```

### 7.3 Safe Patterns

```csharp
// Pattern 1: Channel-based (preferred)
var ch = new Channel<int>();
Task.Run(async () => {
    await ch.Send(ComputeResult());
});
var result = await ch.Receive();

// Pattern 2: Mutex-protected
var mutex = new Mutex();
var shared = new List<int>();
Task.Run(async () => {
    using (await mutex.Lock()) {
        shared.Add(42);
    }
});

// Pattern 3: Atomic
var counter = new AtomicInt(0);
Task.Run(() => counter.Increment());

// Pattern 4: Immutable data
let config = new AppConfig { Port = 8080, Host = "localhost" };
// Note: `let` is parsed and recorded (var_decl.is_let) but NOT enforced —
// there is no immutability checking, so `config` is not actually guaranteed
// immutable; treat sharing as Mutex/Atomic-protected like any other state.
```

---

## 8. I/O Integration

### 8.1 Non-blocking I/O

All I/O operations are async and integrate with the scheduler:

```csharp
// File I/O (non-blocking via thread pool)
var data = await File.ReadAllText("large_file.txt");

// Network I/O (non-blocking via epoll/IOCP/kqueue)
var response = await Http.Get("https://api.example.com/data");

// Timer
await Task.Delay(1000);    // yields for 1 second
```

### 8.2 I/O Backends

| Platform | Backend | Mechanism |
|----------|---------|-----------|
| Linux | epoll | readiness notifications (`select` as fallback) |
| Windows | IOCP | I/O Completion Ports |
| macOS/BSD | kqueue | BSD event notification |

There is **no io_uring** backend. The runtime abstracts platform differences
(`src/runtime/rt_io.c`); user code is identical across platforms.

### 8.3 Implementation status

The async I/O stack is implemented end to end and verified on Linux, macOS and
Windows (CI runs the loopback-echo conformance tests on all three):

- **IO reactor** (`src/runtime/rt_io.c`): one readiness/completion reactor with
  an `epoll` (Linux), `kqueue` (macOS/BSD), `IOCP` (Windows) and `select`
  (fallback) backend. The stackless coroutine driver (`rt_co.c`) drains its
  ready queue and then blocks in the reactor (`zan_io_pump`), resuming a
  coroutine when its fd becomes ready.
- **Socket await primitives**: `await Socket.ReadReady(fd)` /
  `await Socket.WriteReady(fd)` genuinely suspend to the reactor. Reference
  locals (buffers) stay live across a suspension because they are frame-resident.
- **Real-async Net stack**: `Socket.*Async`, `AsyncSocket`, `TcpClient`,
  `TcpListener` and `UdpClient` suspend on the reactor (non-blocking + retry on
  `EWOULDBLOCK`); `Http`, `WebSocket`, `Mqtt` and `Worker` mark their I/O paths
  `async` and `await` down into the socket layer.
- **Cross-platform correctness**: `sockaddr_in` is built with host-order
  `sin_family` and network-order `sin_port` on Linux/Windows, and the BSD
  `sin_len`/`sin_family` layout (plus BSD `O_NONBLOCK`/`SO_REUSEADDR`) on macOS.
- **Method overloading**: same-named methods are resolved by argument count at
  the call site, which the async Net APIs (e.g. `RecvAsync()` vs `RecvAsync(n)`)
  rely on.

---

## 9. Examples

> **以下为设计目标示例，当前不可编译。** They use lambda-form `Task.Run`,
> `async Task Main`, generic `Channel<T>`, tuple destructuring and
> `await foreach` — none of which the current compiler accepts (see §2.1, §3
> and §6).

### 9.1 Concurrent Web Scraper

```csharp
using System;
using System.Net;
using System.Threading;
using System.Collections;

class Program {
    static async Task Main(string[] args) {
        var urls = new List<string> {
            "https://example.com/page1",
            "https://example.com/page2",
            "https://example.com/page3",
        };

        var results = new Channel<(string, string)>(urls.Count);
        var sem = new Semaphore(5);  // max 5 concurrent requests

        foreach (var url in urls) {
            Task.Run(async () => {
                await sem.Acquire();
                try {
                    var body = await Http.GetString(url);
                    await results.Send((url, body));
                } finally {
                    sem.Release();
                }
            });
        }

        for (int i = 0; i < urls.Count; i++) {
            var (url, body) = await results.Receive();
            Console.WriteLine($"{url}: {body.Length} bytes");
        }
    }
}
```

### 9.2 Producer-Consumer Pipeline

```csharp
using System;
using System.Threading;

class Program {
    static async Task Main() {
        var jobs = new Channel<int>(100);
        var results = new Channel<int>(100);

        // Start 4 workers
        for (int w = 0; w < 4; w++) {
            Task.Run(async () => {
                await foreach (var job in jobs) {
                    await results.Send(job * job);
                }
            });
        }

        // Send 100 jobs
        Task.Run(async () => {
            for (int i = 0; i < 100; i++) {
                await jobs.Send(i);
            }
            jobs.Close();
        });

        // Collect results
        for (int i = 0; i < 100; i++) {
            var result = await results.Receive();
            Console.WriteLine(result);
        }
    }
}
```

### 9.3 TCP Echo Server

```csharp
using System;
using System.Net;
using System.Threading;

class Program {
    static async Task Main() {
        var listener = await Socket.Listen("0.0.0.0:8080");
        Console.WriteLine("Listening on :8080");

        while (true) {
            var conn = await listener.Accept();
            // Each connection handled by its own task
            Task.Run(async () => {
                var buf = new byte[4096];
                while (true) {
                    int n = await conn.Read(buf);
                    if (n == 0) break;
                    await conn.Write(buf, 0, n);  // echo back
                }
                conn.Close();
            });
        }
    }
}
```
