# Zan Concurrency Model

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

```csharp
using System;
using System.Threading;

class Program {
    static void Main() {
        // Spawn a lightweight task (like Go's goroutine)
        Task.Run(() => {
            Console.WriteLine("Hello from task!");
        });

        // Spawn with return value
        var result = Task.Run(() => {
            return ComputeExpensiveValue();
        });

        // Await the result
        int value = await result;
        Console.WriteLine($"Result: {value}");
    }
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

```csharp
var mutex = new Mutex();
var counter = 0;

await TaskGroup.Run(async group => {
    for (int i = 0; i < 100; i++) {
        group.Spawn(async () => {
            using (await mutex.Lock()) {
                counter++;
            }
        });
    }
});
// counter == 100 (guaranteed)
```

### 4.2 RWLock (Read-Write Lock)

```csharp
var rwlock = new RWLock();
var cache = new Dict<string, string>();

// Multiple concurrent readers
Task.Run(async () => {
    using (await rwlock.ReadLock()) {
        var value = cache["key"];
    }
});

// Exclusive writer
Task.Run(async () => {
    using (await rwlock.WriteLock()) {
        cache["key"] = "new_value";
    }
});
```

### 4.3 Semaphore

```csharp
// Limit concurrent connections to 10
var sem = new Semaphore(10);

async Task FetchUrl(string url) {
    await sem.Acquire();
    try {
        return await Http.Get(url);
    } finally {
        sem.Release();
    }
}
```

### 4.4 Atomic Operations

```csharp
var counter = new Atomic<int>(0);

Task.Run(() => {
    counter.Increment();                    // atomic ++
    counter.Add(5);                         // atomic +=
    int old = counter.CompareExchange(5, 10); // CAS
    int val = counter.Load();               // atomic read
});
```

### 4.5 Once (One-time Initialization)

```csharp
var once = new Once();
Connection? sharedConn = null;

async Task<Connection> GetConnection() {
    await once.Do(async () => {
        sharedConn = await Connection.Open("db://localhost");
    });
    return sharedConn!;
}
```

---

## 5. Runtime Scheduler Architecture

### 5.1 M:N Scheduling

```
┌──────────────────────────────────────────┐
│              User Tasks (M)              │
│  [T1] [T2] [T3] [T4] [T5] ... [Tn]    │
└──────────────┬───────────────────────────┘
               │ multiplexed onto
               ▼
┌──────────────────────────────────────────┐
│           Worker Threads (N)             │
│  [W1: T1,T3] [W2: T2,T5] [W3: T4]     │
│  Each has a local run queue              │
│  Work-stealing from other workers        │
└──────────────────────────────────────────┘
```

N = number of CPU cores (configurable via `Runtime.SetMaxWorkers(n)`)

### 5.2 Work-Stealing

Each worker thread has a local double-ended queue (deque):
- Push/pop from the tail (LIFO — cache locality)
- Other workers steal from the head (FIFO — large tasks first)
- Global queue for overflow and external submissions

### 5.3 Task States

```
    ┌────────┐
    │ Created│
    └───┬────┘
        │ schedule
        ▼
    ┌────────┐    preempt     ┌──────────┐
    │Running │───────────────▶│  Queued   │
    └───┬────┘                └────┬─────┘
        │                          │ schedule
        │ await/channel            │
        ▼                          │
    ┌────────┐    wake up          │
    │Blocked │─────────────────────┘
    └───┬────┘
        │ complete
        ▼
    ┌────────┐
    │  Done  │
    └────────┘
```

### 5.4 Preemption

Tasks are cooperatively scheduled at:
- `await` points
- Channel send/receive
- Function call prologues (periodic check, ~10μs intervals)

This prevents long-running compute tasks from starving other tasks.

### 5.5 Stack Management

Tasks use segmented stacks:
- Initial stack: 4KB (configurable)
- Grows on demand by allocating new segments
- Shrinks when segments are no longer needed
- Maximum stack size: 1MB (configurable, prevents stack overflow)

---

## 6. Async/Await Integration

### 6.1 Async Functions

```csharp
async Task<string> FetchData(string url) {
    var response = await Http.Get(url);       // yields to scheduler
    var body = await response.ReadString();   // yields again
    return body;
}
```

The compiler transforms async functions into state machines:

```
// Conceptual transformation:
// async Task<string> FetchData(string url)
// →
// struct FetchData_StateMachine {
//     int state;
//     string url;
//     HttpResponse response;
//     string body;
//     Task<string> result;
//
//     void MoveNext() {
//         switch (state) {
//             case 0:
//                 state = 1;
//                 Http.Get(url).ContinueWith(this);
//                 return;
//             case 1:
//                 response = awaiter.GetResult();
//                 state = 2;
//                 response.ReadString().ContinueWith(this);
//                 return;
//             case 2:
//                 body = awaiter.GetResult();
//                 result.SetResult(body);
//                 return;
//         }
//     }
// }
```

### 6.2 Async Iterators

```csharp
async IAsyncEnumerable<string> ReadLines(string path) {
    var stream = await File.OpenRead(path);
    var reader = new TextReader(stream);
    while (await reader.ReadLine() is string line) {
        yield return line;
    }
}

// Usage
await foreach (var line in ReadLines("data.txt")) {
    Console.WriteLine(line);
}
```

---

## 7. Thread Safety Model

### 7.1 Sendable Types

Types that can be safely shared between tasks:

| Type | Sendable? | Notes |
|------|-----------|-------|
| Value types (struct, int, etc.) | Yes | Copied by value |
| Immutable reference types | Yes | No mutation possible |
| `class` with `Mutex` protection | Yes | Developer responsibility |
| `class` without synchronization | No | Compiler warning |
| Channels | Yes | Thread-safe by design |
| Atomic<T> | Yes | Lock-free operations |

### 7.2 Compiler Checks

The compiler provides warnings (not errors, for pragmatism) when:
- A non-sendable type is captured by a task closure
- A mutable reference type is shared without synchronization
- A channel carries a non-sendable type

```csharp
class Counter {
    public int Value;       // mutable field
}

var c = new Counter();
Task.Run(() => {
    c.Value++;              // WARNING: sharing mutable state without synchronization
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
var counter = new Atomic<int>(0);
Task.Run(() => counter.Increment());

// Pattern 4: Immutable data
let config = new AppConfig { Port = 8080, Host = "localhost" };
// config is immutable (let), safe to share without locks
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
| Linux | io_uring / epoll | Kernel async I/O |
| Windows | IOCP | I/O Completion Ports |
| macOS | kqueue | BSD event notification |

The runtime abstracts platform differences. User code is identical across platforms.

---

## 9. Examples

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
