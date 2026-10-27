# Zan Standard Library Specification

## 1. Design Principles

### 1.1 Source Distribution

The standard library is distributed as **Zan source code** (`.zan` files). It is compiled together with user code — no precompiled binaries, no separate installation step.

### 1.2 Non-Intrusive Architecture

Following aardio's model:

- **No global state pollution** — each module is self-contained
- **Lazy loading** — modules are loaded only when `using` imports them
- **Bundled native deps** — DLLs/SOs live in `drivers/<platform>/` directories next to the module, listed in a `driver.manifest` and bundled on `--publish`
- **Package-extensible** — namespaces can also be supplied by installed packages (`.zan-packages/` + `zan.lock`, see §4)
- **No package manager required for stdlib** — the stdlib ships with the compiler as source; files exist, they work

### 1.3 Platform Abstraction

There is no `native/{win,linux,macos}/` tree. Native driver binaries live in a
`drivers/` subdirectory of the module that owns them, one subdirectory per
target (named after the cross-compile toolchain, see `zan_driver_subdir` in
`src/compiler/main.c`):

```
packages/Zan.Gui/src/Gui/drivers/           # GUI native driver (zan_gui)
├── driver.manifest           # one `-l` basename per line, e.g. "zan_gui"
├── win-x64/                  # zan_gui.dll + import lib + WebView2 loader
├── win-arm64/
├── linux-x64/
├── linux-arm64/
├── macos-x64/
└── macos-arm64/
```

`zanc` discovers every `drivers/driver.manifest` under the stdlib root at
compile time (`zan_discover_drivers`, `src/compiler/main.c`); on `--publish`
the listed libraries are copied next to the executable (default
`--link-mode shared`; `--link-mode static` folds a driver into the exe where
a static archive exists).

Cross-platform code is written with conditional compilation. The predefined
symbols are `WINDOWS`, `WIN32`, `LINUX`, `MACOS`, `APPLE`, `ARM64`, `X86_64`,
`RISCV64`, `WASM32`, `WASI`, `MUSL` and `ZAN` (defined in `src/compiler/
main.c`); stdlib code tests them directly, e.g.:

```csharp
using System;

class PlatformProbe {
    static string Os() {
        #if WINDOWS
        return "windows";
        #elif MACOS
        return "macos";
        #elif WASI
        return "wasi";
        #else
        return "linux";
        #endif
    }
}
```

(`#if WINDOWS` / `#if LINUX` / `#if MACOS` are the forms actually used across
`stdlib/`, e.g. `System/Interop.zan` and `Gui/Backend/Window.zan`.)

---

## 2. Directory Structure

Layout rule: the `System/` root holds **core primitives only** (Array,
DateTime, Guid, Random…). Thematic groups — exceptions, FFI/interop,
extension methods — live in same-namespace subdirectories (`Exceptions/`,
`Interop/`, `Extensions/`, like the pre-existing `Models/`): their files
still declare `namespace System`, and the compiler pulls them in exactly
like root files (first-file namespace probe in `glob_stdlib_dir`,
`src/compiler/main.c` — one level deep, every file in such a subdirectory
must declare the parent namespace). Keep new non-primitive files out of
the root: everything loose there gets compiled into every `using System;`
program.

```
stdlib/
├── System/                          # namespace System
│   ├── ConsoleColor.zan             # ConsoleColor enum
│   ├── DateTime.zan                 # Date and time
│   ├── Guid.zan                     # UUID generation
│   ├── Random.zan                   # Random numbers (non-cryptographic)
│   ├── RandomNumberGenerator.zan    # OS CSPRNG bytes — a root primitive:
│   │                                # keeping it under Security/Cryptography
│   │                                # made `using System;` pull 29 files
│   ├── Stopwatch.zan                # monotonic clock + elapsed timing
│   │                                # (QPC / clock_gettime); also the static
│   │                                # GetMilliseconds/GetMicroseconds readings
│   ├── TimeSpan.zan                 # Time span
│   ├── GC.zan                       # Allocation tracking + memory metrics (A464)
│   ├── Binding.zan                  # late binding / dynamic dispatch helpers
│   ├── TaskJoin.zan                 # async task join helpers
│   ├── ZanVersion.zan               # version constants (generated from .in)
│   │
│   ├── Exceptions/                  # namespace System — Exception base +
│   │                                # the concrete exception types
│   ├── Interop/                     # namespace System — FFI/marshaling:
│   │                                # Interop ([DllImport] wrappers),
│   │                                # NativeMemory, Marshal, Com/ComVtbl, Wide
│   ├── Extensions/                  # namespace System — extension methods:
│   │                                # ListExtensions, StringExtensions,
│   │                                # MemoryExtensions
│   │
│   ├── IO/                          # System.IO
│   │   ├── File.zan                 # file read/write (static helpers)
│   │   ├── Directory.zan            # directory operations
│   │   ├── Path.zan                 # path manipulation
│   │   ├── Stream.zan               # base stream class
│   │   ├── StreamReader.zan         # text reading
│   │   ├── StreamWriter.zan         # text writing
│   │   ├── FileStream.zan           # file stream
│   │   ├── MemoryStream.zan         # in-memory stream
│   │   ├── ByteBuffer.zan           # growable byte buffer
│   │   ├── FileInfo.zan / FileInfoEx.zan
│   │   ├── DirectoryTree.zan        # directory tree enumeration
│   │   ├── Watch/                   # System.IO.Watch — DirectoryWatcher
│   │   │                            # (separate namespace: it needs a
│   │   │                            #  background thread, and pulling
│   │   │                            #  System.Threading into every reader
│   │   │                            #  of a file cost +34 files / +150KB)
│   │   ├── IniFile.zan              # INI parsing
│   │   ├── Shortcut.zan             # .lnk / .desktop shortcuts
│   │   ├── KnownFolders.zan         # OS known-folder lookup
│   │   ├── MemoryMappedFile.zan     # memory-mapped files
│   │   └── Compression/             # Deflate / GZip / Zip / Tar / Crc32
│   │
│   ├── Collections/                 # System.Collections
│   │   ├── Generic/
│   │   │   └── KeyValuePair.zan     # KVP(K, V) key/value pair
│   │   ├── HashSet.zan              # hash set
│   │   ├── LinkedList.zan           # doubly linked list
│   │   ├── Queue.zan                # FIFO queue
│   │   └── Stack.zan                # LIFO stack
│   │
│   ├── Text/                        # System.Text (encoding only; text
│   │   │                            # processing lives in packages/Zan.Text,
│   │   │                            # CSV in packages/Zan.Csv)
│   │   └── Encoding.zan             # UTF-8/16/32 encoding
│   │
│   ├── Net/                         # System.Net
│   │   ├── IPAddress.zan / NetworkInterface.zan / ServerBanner.zan
│   │   ├── Ping.zan                 # ICMP ping
│   │   ├── Worker.zan               # background worker helpers
│   │   ├── Http/                    # Client/ (HttpClient, CookieJar, SseSink),
│   │   │                            # HttpServer.zan, HttpRequest.zan,
│   │   │                            # HttpResponse.zan, Proxy/ (HttpForwarder)
│   │   ├── Https/                   # HttpsServer.zan
│   │   ├── Sockets/                 # Socket, TcpClient, TcpListener,
│   │   │                            # UdpClient, AsyncSocket
│   │   ├── Tls/                     # TlsStream.zan (+ drivers/)
│   │   ├── WebSocket/               # WebSocketClient.zan (+ Secure/)
│   │   ├── Sse/                     # server-sent events
│   │   ├── Mqtt/                    # MqttClient, MqttBroker
│   │   ├── Coap/                    # CoapClient
│   │   ├── Modbus/                  # ModbusClient
│   │   ├── Sip/                     # SipClient, SipMessage
│   │   ├── Ntp/                     # NtpClient
│   │   ├── WebDav/                  # WebDavClient
│   │   └── Rpc/                     # RpcClient/RpcServer, codecs, transports
│   │
│   ├── Threading/                   # System.Threading
│   │   ├── Thread.zan / Mutex.zan / Semaphore.zan
│   │   │                            # AtomicInt.zan / SharedTable.zan
│   │   ├── AsyncGate.zan / AsyncRwLock.zan / BlockingQueue.zan
│   │   ├── Gate.zan / SemaphoreSlim.zan / Timer.zan
│   │
│   ├── Diagnostics/                 # Process, ProcessList, ProcessHost,
│   │   │                            # ProcessControl, Privileges,
│   │   │                            # ServerMetrics
│   ├── Json/                        # Json.zan, JsonValue.zan
│   ├── Linq/                        # Enumerable.zan, Expression.zan
│   │
│   └── (Automation, Compiler, Data/ — 含 Excel/（XlsxBook 纯 Zan
│       写 .xlsx：SaveToBytes 整表内存 / SaveStreaming 流式落盘
│       —— XlsxRowSource 逐行供给、超 1048576 行自动分表、可取消）,
│       Drawing/, Globalization, Input,
│       Management, Resources, Scripting/, Security/, ServiceProcess,
│       Web/, Windows/)
│
├── Gui/                             # self-hosted GUI framework (Zan source)
│   ├── App.zan / Point.zan / Color.zan / Theme.zan / Signal.zan / FlexStyle.zan / FlexNode.zan
│   ├── Canvas.zan / EventHub.zan / Text.zan / Icon.zan / Css.zan / Style*.zan
│   ├── Backend/                     # Window.zan, UiDriver.zan, Win32Shell.zan
│   ├── Component/                   # Chart, CodeEditor, DataTable, WebView, ...
│   ├── Widget/                      # 60 controls (Button, Input, Label, ...)
│   ├── Designer/ / Hmi/
│   ├── drivers/                     # driver.manifest + win-x64/... (zan_gui)
│   └── skins/                       # CSS theme skins (light, dark, ...)
│
├── Game/                            # Arcade2D, Arpg, Board, Cards, Core,
│   │                                # Foundation, Render, Scene
├── Sdk/                             # Jd, Wechat
└── Platform/
    └── Runtime.zan                  # platform runtime hooks (#if WINDOWS, ...)
```

**Compiler builtins are not files.** `Console`, `Math`, `String`, `Convert`,
`Environment`, `List<T>`, `Dict<K,V>`, `StringBuilder` and the array/string
types are implemented inside the compiler (`src/compiler/builtin_api.c`), so
there are no `System/Console.zan`, `System/Math.zan`, `System/String.zan`,
`System/Convert.zan`, `System/Environment.zan`, `Collections/List.zan` or
`Text/StringBuilder.zan` files. There are no `mod.zan` entry points anywhere:
a namespace is simply a directory (see §4).

---

## 3. Core Modules API

### 3.1 System.Console

`Console` is a **compiler builtin** (`src/compiler/builtin_api.c`) — there is
no `System/Console.zan` file. Its members are:

```csharp
// Output
static void WriteLine(object value);
static void Write(object value);
static void PrintLine(object value);    // alias of WriteLine (same lowering)

// Input
static string ReadLine();
static int Read();                      // reads one character
static int ReadKey([bool intercept]);   // waits for a keypress

// Appearance
static void Clear();
static void ResetColor();
static ConsoleColor ForegroundColor;    // get/set property
static ConsoleColor BackgroundColor;    // get/set property
static string Title;                    // get/set property (console window title)
```

`ConsoleColor` comes from `stdlib/System/ConsoleColor.zan`. There is no
`Error`, `SetCursorPosition` or `SetColor` member.

### 3.2 System.Math

`Math` is also a **compiler builtin**. Its members are:

```csharp
static double Abs(double value);
static double Max(double a, double b);
static double Min(double a, double b);
static double Pow(double x, double y);
static double Sqrt(double value);
static double Round(double value);
static double Floor(double value);
static double Ceiling(double value);
static double Sin(double value);
static double Cos(double value);
static double Tan(double value);
static double Atan(double value);
static double Atan2(double y, double x);
static double Asin(double value);
static double Acos(double value);
```

There are no `PI`/`E` constants, no logarithms (`Log`/`Log2`/`Log10`), no
`Clamp`, and no generic `Min`/`Max`. The trigonometry family is available, but
`Math.PI` is **not** — use `System.Numerics.MathUtil.Pi` instead.

### 3.2b System.Numerics（数值层扩展）

`System.Numerics` lives in `stdlib/System/Numerics/`. The geometry types are
`float` (so they convert to/from the matrix types without precision loss);
scalar and sampling helpers are `double` (the built-in `Math` is `double` too,
and game-side math in this repository is `double`-dominant).

| File | Type | What it gives you |
|---|---|---|
| `Vector2/3/4.zan` | `float` struct | Existing geometry primitives (`Add`/`Subtract`/`Dot`/`Cross`/`Normalize`/`Lerp`/…). **No operators** — `a + b` does not compile; use the static methods. |
| `Matrix4x4.zan` | `float` struct | `Identity`/`CreateTranslation`/`CreateScale`/`Multiply`/`Transpose`/`Transform` (row-vector convention: `Transform(v, m)` is `v * m`). |
| `BitOperations.zan` | — | `PopCount`/`LeadingZeroCount`/`TrailingZeroCount`/`RotateLeft`/`ReverseEndianness`/`Log2`/… |
| `MathUtil.zan` | `double` | `Pi`/`Tau`/`E`/`DegToRad`/`RadToDeg`/`Epsilon`, `Clamp`/`Clamp01`/`ClampInt`, `Lerp`/`LerpUnclamped`/`InverseLerp`/`Remap`, `SmoothStep`/`SmootherStep`, `Sign`, `Approximately`, `Wrap`/`PingPong`, `MoveTowards`, `DeltaAngle`/`LerpAngle`, `DegreesToRadians`/`RadiansToDegrees`, `Step`, `Hermite`, `RoundTo`, and **`Log`/`Exp`** (the built-in `Math` has neither). |
| `Quaternion.zan` | `float` struct | `Identity`/`FromAxisAngle`/`FromEuler(Degrees|Radians)`/`ToEuler(Degrees|Radians)`/`Multiply`/`Rotate`/`Slerp`/`Lerp`/`Angle`/`Normalize`/`Inverse`/`Conjugate`/`ToMatrix`/`LookRotation`. `q1*q2` means "apply `q2` first"; euler order is YXZ; angle arguments always carry a `Degrees`/`Radians` suffix. `Matrix4x4.Transform(v, q.ToMatrix())` equals `Quaternion.Rotate(v, q)`. |
| `Noise.zan` | `double` class | Seeded, reproducible: `Value2D`/`Value3D`/`Perlin2D`/`Perlin3D`/`Fbm2D`/`Ridged2D`. Values stay in `[-1, 1]`. |
| `RandomExt.zan` | `double` class | xorshift64\* PRNG: `NextDouble`/`NextRange`/`NextInt`/`NextBool`/`NextNormal`/`NextExponential`, plus `Pick<T>`/`WeightedPick<T>`/`Shuffle<T>`/`Shuffled<T>`. Same seed ⇒ same sequence. Not cryptographically safe (use `System.RandomNumberGenerator`). |
| `Spline.zan` | `float` class | Catmull-Rom (`Evaluate`), cubic Bezier (`EvaluateBezier`), Hermite (`EvaluateHermite`), `ApproximateLength`, `Resample` (uniform arc length). Control points are `List<Vector3>`. |

### 3.3 System.Collections.List<T>

`List<T>` is a **compiler builtin** (dynamic array with copy-on-write
semantics; `src/compiler/builtin_api.c`). Its built-in members are:

```csharp
// Properties
int Count { get; }
T this[int index] { get; set; }         // indexer

// Modification
void Add(T item);
void AddRange(List<T> items);
void Insert(int index, T item);
void RemoveAt(int index);
void Clear();

// Query
bool Contains(T item);
int IndexOf(T item);
int LastIndexOf(T item);

// Order
void Reverse();
```

There is no `Capacity`, `Find`, `FindAll`, `Map`, `Aggregate`, `ToArray` or
`Slice` on `List<T>` itself.

**Extensions.** Additional operations come from extension methods:

- `System.ListExtensions` (`stdlib/System/ListExtensions.zan`):
  `Remove(item)`, `GetRange(start, count)`, and `Sort()` overloads for
  `List<int>`, `List<double>` and `List<string>`.
- `System.Linq.Enumerable` (`packages/Zan.Linq/src/System/Linq/Enumerable.zan`, see §3.6):
  `Where`, `Select`, `SelectMany`, `Any`, `All`, `Count`, `First(OrDefault)`,
  `Last(OrDefault)`, `Single(OrDefault)`, `Take`, `Skip`, `Reverse`,
  `OrderBy*`, `GroupBy`, `Contains`, `Distinct`, `In`, `Like`, `Aggregate`,
  `Sum`, `Min`, `Max`, `Average`, `ToList`.

So `list.Remove(x)`, `list.Sort()` and `list.Where(...)` resolve to the
extensions, not to built-in members (this is why `ListExtensions`/`Enumerable`
need a `using` — see §3.6).

### 3.4 System.IO.File

`File` is a static helper class in `stdlib/System/IO/File.zan`:

```csharp
static string ReadAllText(string path);
static string ReadAllTextAsync(string path);   // synchronous wrapper
static byte[] ReadAllBytes(string path);
static string[] ReadAllLines(string path);
static void WriteAllText(string path, string content);
static void WriteAllTextAsync(string path, string content);  // synchronous wrapper
static void WriteAllBytes(string path, byte[] data);
static void AppendAllText(string path, string content);
static void AppendAllTextAsync(string path, string content); // synchronous wrapper
static bool Exists(string path);
static void Delete(string path);
static void Copy(string source, string dest);
static void Move(string source, string dest);
static long GetSize(string path);
```

There is no `Stream Open(path, FileMode)` — there is no `FileMode` enum; use
`FileStream` (`System.IO`) for stream-style access. The `*Async` variants are
plain synchronous wrappers around the sync methods (no actual `async` IO), as
noted in their doc comments.

**Error model (C#-aligned).** Failures throw instead of silently returning
empty/default values:

- `ReadAllText` / `ReadAllLines` / `ReadAllBytes` / `GetSize` / `Copy` (source)
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
namespace System.Net.Http;

class HttpClient {                       // packages/Zan.Net/src/System/Net/Http/Client/HttpClient.zan
    HttpClient(string host, int port);   // host:port base; no base-URL form

    // configuration (fluent, return this)
    HttpClient UseTls();                 // HTTPS client
    HttpClient DisableTlsVerify();       // self-signed / dev certificates
    HttpClient SetClientCertificate(string certFile, string keyFile);
    HttpClient SetHeader(string name, string value);   // default request header
    HttpClient SetTimeout(int milliseconds);
    HttpClient PinPublicKey(string spkiSha256Base64);
    HttpClient UseCookies();

    // requests — verb helpers return the body string only
    async HttpResponse SendAsync(string method, string path, string body);
    async string GetAsync(string path);
    async string PostAsync(string path, string body);
    async string PutAsync(string path, string body);
    async string DeleteAsync(string path);

    // static one-shot helpers (no client instance needed)
    static async string GetAsync(string host, int port, string path);
    static async string PostAsync(string host, int port, string path, string body);
    static async string GetHttpsAsync(string host, int port, string path);
}

class HttpResponse {                     // builder + parser, no public properties
    // state lives in plain fields (readable within the System.Net.Http
    // namespace): statusCode, statusText, body, bodyBytes, bodyBytesLen,
    // contentType, headers, keepAlive

    // static factories
    static HttpResponse Ok(string body);
    static HttpResponse Json(string jsonBody);
    static HttpResponse Text(string text);
    static HttpResponse Bytes(byte[] data, int len, string ct);
    static HttpResponse Redirect(string url);
    static HttpResponse NotFound();
    static HttpResponse ServerError(string message);
    static HttpResponse WithStatus(int code, string text, string body);
    static HttpResponse Parse(string raw);

    // fluent setters (return this)
    HttpResponse SetHeader(string name, string value);
    HttpResponse SetContentType(string ct);
    HttpResponse SetKeepAlive(bool keep);
}
```

`HttpResponse` has **no public properties** — `StatusCode`, `StatusText`,
`Body`, `Headers`, `IsSuccess` etc. do not exist as accessors. Its state is
held in plain fields (visible inside the `System.Net.Http` namespace, so
`HttpClient` reads `resp.body` directly) and is normally populated through
the static factories, or from a raw wire response via `HttpResponse.Parse`.
The verb helpers `GetAsync`/`PostAsync`/`PutAsync`/`DeleteAsync` return the
body string only; use `SendAsync(method, path, body)` when you need the
parsed `HttpResponse` (`statusCode` / `statusText` / headers — see "Status
codes" below).

**Error model (C#-aligned).** Connection failures throw instead of silently
returning empty responses:

- `TcpClient.ConnectAsync` returns `null` when the connection fails
  (refused / unreachable); `Socket.ConnectAsync` returns the socket error
  code (0 on success).
- `HttpClient` request methods throw `HttpRequestException` when the TCP
  connect fails, the TLS context cannot be created, or the TLS handshake
  fails. `HttpRequestException` derives from `Exception`.

**Timeouts.** `SetTimeout(ms)` is enforced, not advisory: the connect phase is
bounded by a deadline-polled non-blocking connect and the request/response
phase by the `HttpDeadline` sweeper, which hangs the socket up so a peer that
accepts and then goes quiet cannot park the coroutine forever. Either phase
running out throws `HttpRequestException` with a "timed out after Nms" message.
The sweeper ticks once a second, so the effective granularity is ~1s;
`SetTimeout(0)` disables deadlines.

**Status codes.** The verb helpers (`GetAsync` / `PostAsync` / ...) return the
body only, which makes a 502 error page look like a payload. `SendAsync(method,
path, body)` returns the parsed `HttpResponse` instead, so callers can see
`statusCode` / `statusText` / headers.

**JSON error model (System.Json).** `JsonValue.Parse` throws `JsonException`
("Malformed JSON at position N") on malformed input; the document is
validated with an allocation-free scan before the value tree is built.
`JsonValue.ParseLenient` keeps the previous best-effort behaviour (returns
whatever parsed so far) for authored/tool-emitted documents. The typed
accessors (`Str`/`Int`/`Bool`/`Double`, `As*`) still return the caller's
explicit default when a key is absent or of the wrong kind.

**Streaming forwarder (System.Net.Http.Proxy).** `HttpClient` returns a fully
parsed `HttpResponse`, so a forwarder built on it buffers the whole upstream
response: the downstream client sees nothing until the upstream is done, which
makes streaming chat (SSE / chunked token streams) look frozen and leaves
CONNECT / Upgrade unusable. `HttpForwarder` is the streaming path — it moves
bytes with socket primitives only, never materialising a response:

```csharp
namespace System.Net.Http.Proxy;

class HttpForwarder {
    HttpForwarder(string listenHost, int listenPort, string upstream);
    // upstream: "https://host[:port]" | "http://host[:port]" | "host:port"

    HttpForwarder SetTimeout(int ms);          // idle timeout, default 5 min
    HttpForwarder SetChunkBytes(int bytes);    // relay block size
    HttpForwarder SetMaxHeadBytes(int bytes);  // head-block cap
    HttpForwarder SetMaxConnections(int max);
    HttpForwarder DisableTlsVerify();          // self-signed upstream
    HttpForwarder DenyConnect();               // refuse CONNECT tunnels
    HttpForwarder SetUpstreamKeepAlive(int maxIdle, int idleMs);
    HttpForwarder DisableUpstreamKeepAlive();  // back to one link per request
    FwdPool UpstreamPool();                    // Hits()/Misses()/IdleCount()

    async void Start();                        // accept loop; one coroutine per conn
    void Stop();
}
```

- Response heads are relayed the moment they arrive, then every body block is
  written downstream as it is read: chunked frames pass through untouched
  (`Transfer-Encoding` is preserved), a `Content-Length` body is relayed
  exactly, and a body with neither is relayed until the upstream closes —
  which is what SSE / `text/event-stream` responses need.
- Request bodies stream the same way, so large uploads are not buffered
  either.
- `CONNECT` and a `101 Switching Protocols` response become raw bidirectional
  byte tunnels (TLS through CONNECT is passed through, not decrypted); either
  direction ending closes both, so no half-open tunnel is left parked.
- Hop-by-hop headers (RFC 7230 6.1) are stripped, `Host` is rewritten to the
  upstream authority and `X-Forwarded-For` is appended.
- Upstream links are pooled and reused (`Connection: keep-alive`), which is
  what makes forwarding a remote API affordable: otherwise every request pays
  a TCP round trip plus, over TLS, a full handshake. A link only goes back to
  the pool when the response end is framed (`Content-Length`, chunked, or a
  by-definition empty 204/304/HEAD body) and nothing is left unread on it, so
  no response tail can leak into the next request; close-delimited streams,
  `101` upgrades and `CONNECT` tunnels still end with the connection. A
  body-less request whose pooled link turns out to be dead is retried once on a
  fresh link. `DisableUpstreamKeepAlive()` restores one-link-per-request.
- https upstreams go through `TlsStream`, so the forwarder itself needs no
  extra TLS plumbing.

See `examples/net/http_forwarder.zan` (token-by-token stream through the
forwarder) and the `http_forwarder_stream` / `http_forwarder_tunnel`
conformance cases.

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
`docs/archive/bugs/generics-uniform-repr.md`):

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

### 3.7 System.Threading — tasks, channels & cancellation (implemented surface)

`Task` is a compiler-builtin type (`src/compiler/builtin_api.c`, `members_task`),
not a library class. A task handle is a `long`; an `async` method is lowered to
a coroutine state machine (see `docs/ASYNC_CPS_DESIGN.md`).

| Form | Meaning |
|------|---------|
| `long h = Task.Spawn(Worker(x))` | run `Worker` as a detached coroutine, return its handle |
| `long h = Task.Run(Worker(x))` | same lowering, but a **value-returning** async call keeps its result — the handle is then a `Task<T>` |
| `long h = Task.Run(() => { ... })` | lambda/`Action` form; allowed only for a parameterless `void` body (a result needs an async call) |
| `await Task.Delay(ms)` / `await Task.Yield()` | suspend this coroutine |
| `await Task.WhenAll(handles)` | suspend until every handle in a `List<long>` has completed |
| `await Task.WhenAny(handles)` | suspend until one has completed; yields its index |
| `Task.IsDone(h)` | 1 once that coroutine finished (also 1 for a reaped frame) |
| `Task.Cancel(h)` / `Task.IsCancellationRequested()` | cooperative cancellation, propagated down the await chain |
| `await t.Result` / `t.Wait()` / `t.IsCompleted` | `t.Result` needs a `Task<T>`; `t.Wait()` / `t.IsCompleted` work on any task |

`Task.Spawn` never keeps a result, so its handle observes *completion only*: no
result, no exception hand-off. A spawned coroutine publishes its result through
shared state (a static field, a queue, `BlockingQueue<T>`) that the joiner reads
after the join. `TaskGroup` does not exist; the join helpers live in
`TaskJoin` (`stdlib/System/TaskJoin.zan`): `WhenAll` / `WhenAny` (plus the
`WhenAllPoll` / `WhenAnyPoll` busy-poll variants), `CancelAll(List<long>)` and
`DoneCount(List<long>)`.

> **Two task shapes, two handle types** — the checker types `Task.Run` /
> `Task.Spawn` by argument shape, so mixing them up is a compile-time error
> (not a runtime abort):
>
> | Argument | Handle type | Result |
> |---|---|---|
> | `Task.Run(lambda)` / `Task.Run(actionDelegate)` | `Task` (no result) | none — the action frame is released by the completion path |
> | `Task.Run(asyncCall)` with a non-void result | `Task<T>` | yes — the frame keeps the result, the reader releases it |
> | `Task.Run(asyncCall)` with a void result, or `Task.Spawn(...)` | `Task` (no result) | none — detached |
>
> `Task<int> t = Task.Run(() => { ... });` is rejected: *"a task started from a
> lambda/Action carries no result"* — that handle has no result to read, and
> reading one would release the frame twice. The supported form is
> `Task t = Task.Run(() => { ... }); t.Wait();` — a wait on a result-less task
> observes completion only; it never releases the frame (the completion path
> does that, exactly once). A `Task` / `Task<T>` handle is a `long` frame
> pointer underneath, so it can be handed to the `long`-taking APIs
> (`Task.IsDone(h)`, `TaskJoin.WhenAll(handles)`). The runtime still has no
> idempotent-reap backstop for that frame; tracked as TASKS.md A380.

**Channels** — `Channel<T>` (`stdlib/System/Threading/Channel.zan`) is generic:
`CreateUnbounded()` / `CreateBounded(capacity)`, `TryWrite(T)` /
`async bool Write(T)`, `TryRead(out T)` / `async bool Read(out T)`, `Count()`,
`IsCompleted()`, `Complete()`, `Close()`. A closed-and-drained channel reports
failure instead of blocking. `StringChannel` is the string-only variant kept
for compatibility (`Send(string)` / `async string Receive()`).

**Cancellation** — `CancellationToken`, `CancellationTokenSource` and
`CancellationTokenRegistration` (`stdlib/System/Threading/`). A source offers
`CreateAfter(ms)`, `CreateLinkedTokenSource(...)`, `Cancel()`, `CancelAfter(ms)`;
a token offers `Register(callback)` / `Unregister(id)`,
`ThrowIfCancellationRequested()` and `await token.WaitHandle()`. The compiler
has **no** special knowledge of tokens: a `CancellationToken` handed to an
async method is a plain parameter and is *not* auto-cancelled with the await
chain — propagate it yourself, or bridge it through
`TaskCompletionSource<T>.AttachCancellationToken(token)`.

**Results and exceptions** — `TaskCompletionSource<T>`
(`stdlib/System/Threading/TaskCompletionSource.zan`) is the result-bearing
awaitable: `SetResult` / `TrySetResult`, `SetException` / `TrySetException`,
`SetCanceled` / `TrySetCanceled`, and `await tcs.Task` to consume the outcome
(a stored exception is rethrown at the await point).

**Synchronization & runtime knobs** — `Mutex` (handle-style), `Gate` /
`AsyncGate` (async wait), `AsyncRwLock`, `Semaphore` / `SemaphoreSlim`,
`AtomicInt`, `SharedTable`, `BlockingQueue<T>`, `ThreadLocal<T>`, `Thread`,
`Timer`, `TaskScheduler`, and `AsyncRuntime` (`SetWorkers` / `SetIoShards` /
`SetSyncFast`). The M:N work-stealing scheduler is turned on with
`ZAN_SCHED_WORKERS`.

`Span<T>` / `ReadOnlySpan<T>` are compiler-builtin slice structs (pointer +
length) rather than library types — they are the only zero-copy view over
native memory; there is no managed `ArraySegment<T>`.

The async I/O stack is implemented end to end and verified on Linux, macOS and
Windows: one readiness/completion reactor (`src/runtime/rt_io.c`) with `epoll`
(Linux), `kqueue` (macOS/BSD), `IOCP` (Windows) and `select` (fallback)
backends; `Socket.*Async` / `AsyncSocket` / `TcpClient` / `TcpListener` /
`UdpClient` suspend on it. Lowering details: `docs/ASYNC_CPS_DESIGN.md`.

### 3.8 System.Configuration — 配置 provider 体系

`System.Configuration` lives in `stdlib/System/Configuration/`. A
configuration is a **chain of providers**; the root reads them in reverse
order (the **last added wins**) and every provider answers
`TryGet(key) / Set(key, value) / Load() / GetKeys()`.

**Key space (one rule for every source).** A key is a sequence of segments.
`:` and `__` are equivalent separators, and each segment is normalized to
"first letter upper case, the rest lower case":

| written as | canonical |
|---|---|
| `database:port` | `Database:Port` |
| `DATABASE__PORT` | `Database:Port` |
| `Database:Port` | `Database:Port` |
| `myKey` | `Mykey` |

Canonicalization is idempotent and is applied on **both** write and read, so
`Get("database:port")`, `Get("DATABASE__PORT")` and `Get("Database:Port")`
all hit the same entry. `ConfigurationKeys.Canonical` / `ToEnvironmentName` /
`FromEnvironmentName` expose the mapping.

**Providers.**

| provider | source |
|---|---|
| `MemoryConfigurationProvider` | an in-process dictionary (`AddInMemory` / `AddInMemoryValue`) |
| `EnvironmentConfigurationProvider` | process environment variables |
| `JsonFileConfigurationProvider` | a JSON file (`AddJsonFile`) |
| `EnvFileConfigurationProvider` | a dotenv-style file (`AddEnvFile`) |

**Environment variables.** The variable name is `prefix` + `__` + the
upper-cased segments joined by `__`; a trailing `_` on the prefix is
normalized away (`APP` / `APP_` / `APP__` are the same prefix). So with
prefix `APP`, the key `Database:Port` reads `APP__DATABASE__PORT`, and
`APP__DATABASE__PORT` maps back to `Database:Port`. Only prefixed variables
are visible to a prefixed provider. Known limit: the standard library has
no cross-platform way to *enumerate* environment variables (only `getenv`
point lookups), so this provider answers by key only and `GetKeys()` does
not list environment-backed keys.

**JSON files.** The document is flattened into keys: objects become
`Section:Key`, arrays become `Section:0`, `Section:1`, and scalars keep
their text form (numbers as decimal text, booleans as `true`/`false`);
`null` does not produce a key. `AddJsonFile(path, optional, rootSection)`
puts every key of the file under `rootSection`.

**dotenv files.** One `KEY=VALUE` per line; `#` comments, an optional
`export ` prefix, double-quoted values with `\n`/`\t`/`\r`/`\\`/`\"`
escapes, single-quoted values taken literally, and trailing comments
(` # ...`) stripped from unquoted values. `__` in a key means a section
separator, so `DATABASE__PORT=5432` is the key `Database:Port`.

**Typed reads are strict.** `GetInt` / `GetLong` / `GetBool` parse the text
themselves and fall back to the caller's default when the text is not a
valid value — `Convert.ToInt32` is deliberately *not* used here, because it
returns `0` for `"abc"` instead of failing, which would silently turn a
typo in a config file into a zero.

**Missing files.** A file provider throws `FileNotFoundException` when the
file is missing unless it was added with `optional: true`.

## 4. Module Resolution Rules

### 4.1 Search Order

When the compiler encounters `using System.IO`, it **auto-includes every
`*.zan` file in the `System/IO/` directory** of the stdlib root
(`scan_using_tokens` → `auto_include_namespace` → `glob_stdlib_dir` in
`src/compiler/main.c`):

1. All `*.zan` files directly under `<stdlib_path>/System/IO/` (one level —
   subdirectories are separate namespaces that need their own `using`).
2. Any additional `*.zan` files found under the same namespace in installed
   packages (`.zan-packages/`, resolved via `zan_pkg_find_namespace`).

There is **no `<project_root>/lib/` override** and no `mod.zan` entry point:
a `using` maps directly to a directory glob. `using System;` imports the
compiler/runtime core names (builtin types like `Console`, `List<T>`) rather
than a directory.

### 4.2 Module Entry Point

There is no entry-point file. A namespace is a directory; every `.zan` file in
it is compiled together when the namespace is `using`-ed, and the file names
are irrelevant to the API surface (all types in the directory are visible).

### 4.3 Native Driver Resolution

Native binaries are owned by the module that uses them, in a `drivers/`
subdirectory:

1. `<module>/drivers/driver.manifest` lists one `-l` basename per line
   (e.g. `zan_gui`, `sqlite3`); blank lines and `#` comments are ignored.
   On macOS, a versioned driver may provide `<lib>.bundle`; when
   `lib<lib>.dylib` is absent, the compiler links the first safe filename in
   that manifest.
2. `zan_discover_drivers` (`src/compiler/main.c`) scans the stdlib root for
   every manifest and indexes the per-target driver directories
   (`win-x64`, `win-arm64`, `linux-x64`, `linux-arm64`, `linux-riscv64`,
   `macos-x64`, `macos-arm64` — matching the cross-compile toolchain names).
3. Native code is called from Zan through `[DllImport]`/`extern` declarations
   (e.g. `packages/Zan.Gui/src/Gui/Backend/Win32Shell.zan`); the driver DLL/SO is loaded at
   runtime.
4. On `--publish`, the manifest's libraries are copied next to the output
   executable (`--link-mode shared`, the default) or folded in where a static
   archive exists (`--link-mode static`).

### 4.4 Conditional Compilation

Platform-specific code uses `#if` directives with the predefined symbols
`WINDOWS`, `WIN32`, `LINUX`, `MACOS`, `APPLE`, `ARM64`, `X86_64`, `RISCV64`,
`WASM32`, `WASI`, `MUSL` and `ZAN` (defined in `src/compiler/main.c`; the
stdlib itself is written against these, e.g. `#if WINDOWS`):

```csharp
#if WINDOWS
    [DllImport("kernel32", EntryPoint = "GetCurrentProcessId")]
    static extern int GetCurrentProcessId();
#elif LINUX
    [DllImport("crt")]
    static extern int getpid();
#elif MACOS
    [DllImport("crt")]
    static extern int getpid();
#endif
```

Note the `[DllImport]` conventions used across the stdlib: the library name
has **no `.dll`/`.so` suffix** (`[DllImport("kernel32")]`, not
`"kernel32.dll"`; the C runtime is imported as `[DllImport("crt")]`), and
renamed functions spell out the exported symbol with `EntryPoint = "..."`.
There is no `[CImport("unistd.h")]` attribute.

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

The **`zandoc`** tool generates documentation from these comments. It is a
standalone program written in Zan (`src/doc/zandoc.zan`, built to
`build/zandoc.exe` by CMake): it extracts the `/// <summary>` / `/// <returns>`
comments and the declarations they precede, and renders them as Markdown or
HTML. There is no `zan doc` compiler subcommand.
