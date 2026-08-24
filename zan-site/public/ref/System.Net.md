# System.Net

> 源码: `stdlib/System/Net/Net.zan`, `stdlib/System/Net/NetworkInterface.zan`, `stdlib/System/Net/Ping.zan`, `stdlib/System/Net/ServerBanner.zan`, `stdlib/System/Net/Worker.zan`


## BannerService (class)

横幅服务表中的一行：运行什么、监听哪里、
几个进程在服务、是否已启动。

- string name;

- string listen;

- string procs;

- string status;

- BannerService(string name, string listen, string procs, string status)


## Connection (class)

One accepted client connection, passed to Worker callbacks. Send pushes
data to the peer from any (including non-async) context; Close marks the
connection for shutdown after the current callback returns.

- int id;

- nint sock;

- string protocol;

- string userData;

- bool closed;

- Connection(int id, nint sock, string protocol)

- void SetData(string data)
  - Arbitrary per-connection state for the application.

- string GetData()

- int GetId()

- nint GetSocket()

- string GetProtocol()

- void Send(string data)
  - Queues raw bytes to the peer (fire-and-forget; usable from
    synchronous callbacks). For "websocket" workers, prefer returning the
    reply from onMessage so it is framed; Send writes raw bytes.

- async int SendAsync(string data)
  - Sends data and suspends until it is written.

- void Close()
  - Marks the connection to be closed by its worker loop.

- bool IsClosed()


## Dns (class)

DNS 解析工具。

- static string Resolve(string hostname)
  - 将主机名解析为 IP 地址。


## IPAddress (class)

IP 地址工具。

- static string Any="0.0.0.0";

- static string Loopback="127.0.0.1";

- static string Broadcast="255.255.255.255";

- static bool IsIPv4(string str)
  - 检查字符串是否为 IPv4 地址格式。


## NetworkInterface (class)

单个网络适配器及其单播地址。Windows 上由
GetAdaptersAddresses 支撑（直接解析原生 x64 ABI 布局）；
Linux/macOS 上由 getifaddrs 支撑（运行时 zan_plat_net_interfaces）。

- public string name;

- public string description;

- public int index;

- public bool up;

- public string macAddress;

- public List<string> ipAddresses;

- public void Dispose()
  - 主动释放适配器持有的字符串和地址条目。

- static List<NetworkInterface> GetAllNetworkInterfaces()
  - 返回本机已知的所有适配器。

- [DllImport("crt", EntryPoint="zan_plat_net_interfaces")]static extern string NativeInterfaces();

- static List<NetworkInterface> ReadPosix()

- [DllImport("iphlpapi", EntryPoint="GetAdaptersAddresses")]static extern int GetAdaptersAddresses(int family, int flags, nint reserved, nint addresses, nint size);

- static List<NetworkInterface> ReadWindows()

- static string ReadMac(nint bytes, int length)

- static string ReadAddress(nint sockaddr, int length)

- static string HexByte(int number)

- static string HexWord(int number)

- static string HexDigit(int number)


## Ping (class)

ICMP echo 客户端，不经过命令行 shell 或外部 ping 可执行程序。
Windows 上直接调 iphlpapi；Linux/macOS 上走运行时的 ICMP socket
（Linux 优先用无需提权的 SOCK_DGRAM/IPPROTO_ICMP “ping socket”，
内核不开放时回退到需 root/CAP_NET_RAW 的 SOCK_RAW）。

- static PingReply Send(string address)
  - 发送 IPv4 echo 请求，超时 4 秒。

- static PingReply Send(string address, int timeoutMs)
  - 发送 IPv4 echo 请求并返回类型化回复。

- [DllImport("crt", EntryPoint="zan_plat_icmp_ping")]static extern int NativeIcmpPing(string address, int timeoutMs);

- [DllImport("iphlpapi", EntryPoint="IcmpCreateFile")]static extern nint IcmpCreateFile();

- [DllImport("iphlpapi", EntryPoint="IcmpSendEcho")]static extern int IcmpSendEcho(nint handle, int destination, nint request, ushort requestSize, nint options, nint reply, int replySize, int timeoutMs);

- [DllImport("iphlpapi", EntryPoint="IcmpCloseHandle")]static extern int IcmpCloseHandle(nint handle);

- static PingStatus MapStatus(int status)

- static int ParseIPv4(string text)

- static int Digit(string ch)

- static string FormatIPv4(int packed)


## PingReply (class)

一条 ICMP echo 结果。

- public PingStatus status;

- public string address;

- public int roundtripTime;

- public int dataSize;

- public bool IsSuccess()

- public void Dispose()
  - 主动释放 reply 持有的文本。


## ServerBanner (class)

常驻服务器打印的启动画面：带标题的分隔线、
runtime/version/pid/mode 行、刚启动的服务表
和它响应的命令——workerman 风格的报告，由
标准库持有，让每个服务器（Worker、WebApp、用户自己的守护进程）
显示同样的内容：

new ServerBanner("ZAN WEB", "SERVICES")
.Mode("single process")
.Info("routes", "12 registered")
.Service("http", "http://0.0.0.0:8080", "1", "[OK]")
.Commands("app.exe [start [-d] | stop | restart | reload | status]")
.Print();

- string title;

- string section;

- string mode;

- string commands;

- string footer;

- List<string> infos;

- List<BannerService> services;

- static int Width()

- ServerBanner(string title, string section)
  - 带标题的服务器启动报告横幅。

- ServerBanner Mode(string m)
  - 进程的布局方式，如 "single process" 或 "master + 4
    workers"。

- ServerBanner Service(string name, string listen, string procs, string status)

- ServerBanner Info(string name, string detail)
  - 一项配置设置而非服务（路由数、会话存储、
    上传目录、限流）：随运行时头部打印，位于
    监听表上方——SERVICES 仍只列实际监听的项。

- ServerBanner Commands(string usage)
  - 服务器响应的命令行，打印在表格下方。

- ServerBanner Footer(string text)
  - 结尾行，如 "Press Ctrl+C to stop."。

- static string Pad(string s, int width)
  - 右对齐填充到列宽，使表格对齐。

- static string Rule(string title)
  - 整宽分隔线，给定 `title` 时以它为中心。

- void Print()


## SocketHttpClient (class)

保留在 System.Net 下的旧版纯套接字 HTTP 客户端。受维护的
客户端位于 System.Net.Http.HttpClient；本类更名为
SocketHttpClient，以避免 System.Net 与 System.Net.Http
模块一起编译时（例如通过
同时引入两者的 Worker 模块）出现重复类型声明。

- static string Get(string host, int port, string path)
  - 执行简单的 GET 请求并返回响应体。

- static string Post(string host, int port, string path, string body)
  - 执行简单的 POST 请求。


## TcpSocket (class)

跨平台 TCP 套接字封装。
取代仅支持 Windows 的 Winsock2 TcpSocket。

- static void Initialize()
  - 初始化套接字子系统。

- static nint Create()
  - 创建 TCP 套接字，返回套接字句柄。

- static int Send(nint sock, string data)
  - 在已连接的套接字上发送数据。

- static string Receive(nint sock, int bufSize)
  - 从套接字接收数据到缓冲区。

- static void Close(nint sock)
  - 关闭套接字。

- static void Cleanup()
  - 清理套接字子系统。


## Worker (class)

Workerman-style multi-protocol server: one Worker per listen address,
per-event callbacks, and a master + N worker-processes runtime that
load-balances accepted connections across CPU cores.

Worker w = new Worker("http", "0.0.0.0", 8080);
w.count = 4;                        // 4 worker processes
w.onHttpRequest = App.Handle;       // HttpRequest -> HttpResponse
Worker.RunAll();                    // never returns

Protocols: "tcp", "udp", "http", "websocket" (or "ws"), "sse", "mqtt".
Callbacks (assign what the protocol needs); the second name is an alias
reading the way that protocol usually does:
onConnect / onOpen                  -- tcp, websocket: connection up
onMessage / onReceive               -- tcp, websocket: data in, reply out
onClose                             -- tcp, websocket: connection gone
onHttpRequest / onRequest           -- http
onUdpMessage / onPacket             -- udp
onSseSubscriber                     -- sse
(none)                              -- mqtt: connections are served by
the built-in MqttBroker.Global()

An "http" worker with onOpen/onMessage set also serves WebSocket on that
same port: a request asking to upgrade becomes a frame loop, everything else
is answered by onRequest.

MULTI-PROCESS MODEL (count > 1). The initially launched process becomes the
MASTER: it spawns `count` worker copies of this executable and supervises
them (a crashed worker is respawned). Accepted connections are spread
across workers, fully event-driven (no polling anywhere):
- Windows: the master binds and accepts every TCP listener on its own
IOCP, then hands each ACCEPTED socket to a worker round-robin via
WSADuplicateSocket over a dedicated per-listener channel connection.
(AcceptEx on a listener shared across processes loses completions --
a documented WSADuplicateSocket/IOCP limitation -- so workers never
accept themselves.) UDP has no accept; UDP Workers are served in the
master process on Windows.
- Linux/macOS: each worker binds its own listener with SO_REUSEPORT and
the kernel hashes new connections across the sockets.
Workers watch the control connection to the master and exit when it dies,
so no orphan processes outlive the master.

SINGLE-PROCESS MODEL (count == 1, default): RunAll runs every Worker's
event loop on this process's coroutine scheduler -- no child processes.

- string protocol;

- string host;

- int port;

- string name;

- bool running;

- AtomicInt connectionCount;

- int maxConnections;

- int maxHeaderBytes;

- int maxRequestBytes;

- int requestTimeout;

- int count;
  - Number of worker processes serving this Worker's port. The
    largest count of any registered Worker decides how many worker
    processes RunAll launches; 1 (default) = serve in-process.

- ConnectionHandler onConnect;

- MessageHandler onMessage;

- ConnectionCloseHandler onClose;

- HttpRequestHandler onHttpRequest;

- DatagramHandler onUdpMessage;

- SseSubscriberHandler onSseSubscriber;

- RawConnHandler onRawConnection;

- ConnectionHandler onOpen;

- MessageHandler onReceive;

- DatagramHandler onPacket;

- HttpRequestHandler onRequest;

- static List<Worker> workers=new List<Worker>();

- static bool daemonize=false;

- static bool cliMode=false;

- static bool stopping=false;

- static List<TcpClient> ctlConn=new List<TcpClient>();

- static List<int> ctlWid=new List<int>();

- static List<TcpClient> wkChannels=new List<TcpClient>();

- static int controlPort=0;

- static bool ctlPortFixed=false;

- static int nextConnId=1;

- static string logDir="logs";

- static string logPattern="{ yyyy}{ MM}/{ dd}.log";

- static AtomicInt statRequests=new AtomicInt(0);

- static void BumpStat()

- static AtomicInt statSent=new AtomicInt(0);

- static void BumpSent()

- static int LiveConnections()

- static int statusInterval=10;

- static SharedTable statTable=null;

- static List<long> stPrevReq=new List<long>();

- static List<long> stPrevCpuMs=new List<long>();

- [DllImport("crt")]static extern void exit(int code);

- static bool debugLog=false;

- static void Dbg(string msg)

- [DllImport("crt")]static extern int _flushall();

- static void SetDebug(bool on)
  - Enables verbose master/worker handoff tracing.

- [DllImport("ws2_32", EntryPoint="WSADuplicateSocketA")]static extern int WSADuplicateSocketA(nint s, int pid, string info);

- [DllImport("ws2_32", EntryPoint="WSASocketA")]static extern nint WSASocketA(int af, int type, int protocol, string info, int g, int flags);

- [DllImport("kernel32", EntryPoint="SetEnvironmentVariableW")]static extern int SetEnvironmentVariableW(nint name, nint val);

- Worker(string protocol, string host, int port)

- ConnectionHandler OpenHandler()

- MessageHandler MessageHandlerOf()

- DatagramHandler PacketHandler()

- HttpRequestHandler RequestHandler()

- bool HasWsHandlers()

- static Worker Listen(string address)
  - Creates a Worker from an address like "http://0.0.0.0:8080".

- Worker SetName(string name)
  - Sets the worker name (used in log lines).

- Worker SetMaxConnections(int max)
  - Sets max concurrent connections for this Worker (per process).
    Connections over the limit are accepted and immediately closed.

- Worker SetHttpLimits(int maxHeaderBytes, int maxRequestBytes, int timeoutMs)
  - Sets the HTTP limits enforced per connection: header block
    size (431 past it), whole-request size (413), and the time a single
    request may take before the connection is hung up (0 disables).

- Worker SetCount(int count)
  - Sets the number of worker processes (like Workerman's
    $worker->count). The process-wide worker count is the max over all
    registered Workers.

- static void SetDaemonize(bool on)
  - Requests daemon mode: on Linux the master detaches from the
    terminal (setsid) before serving. No-op elsewhere.

- static void SetControlPort(int port)
  - Overrides the loopback control port used by the master/worker
    handshake (default: first Worker's port + 10000, and the master moves
    off that default if something else holds it). A port named here is
    used as given: the master fails instead of moving.

- static void SetStatusInterval(int seconds)
  - Seconds between status lines printed by the foreground
    master/single-process server (like watching workerman's status).
    0 disables printing; stats are still collected.

- static void SetLogDir(string dir)
  - Directory the master and every worker log into -- their
    stdout/stderr (like Workerman's stdout_file) plus the supervision
    records -- all in one file per day; defaults to `logs`, created on
    demand. The file name comes from `SetLogPattern`. Pass "" to
    discard worker output instead -- then a worker that dies of a fatal
    signal leaves no record anywhere, so only do that when the application
    already routes its own logging elsewhere.

- static string LogDir()
  - The configured worker output directory ("" when disabled).

- static void SetLogPattern(string pattern)
  - File-name pattern for that shared file, expanded by
    `Log.Expand`: {yyyy} {MM} {dd} {HH} {pid} {wid}, and it may
    contain directories (missing ones are created). The default
    `{yyyy}{MM}/{dd}.log` gives `logs/202608/13.log`: a new file each day,
    grouped by month, so a long-running service neither ends up with one
    unbounded log nor with a directory of thousands of entries.

- static string LogPath()
  - The file master and workers write to right now, or "" when
    logging to a file is disabled.

- static void EnsureMasterLog()

- static void EnsureLogDir()

- static int MaxCount()

- static int ControlPort()

- static string appId="";
  - What identifies this server among everything else on the machine: its
    own executable, the way workerman keys a server by its entry script. Two
    copies in two directories are two servers and share nothing; the same
    copy started again is the same server, so it finds what its previous run
    left behind. NOT the port -- a port is configuration, it changes, and
    the control port even moves at startup when something else holds it.

- static string AppId()

- static string CtlPortFile()
  - The default control port is derived from the service port, so an
    unrelated program holding it used to be fatal even though nothing
    outside the process group ever talks to it. The master may move to
    another port and records the one it claimed here, so stop/status/reload
    still find it.

- static void RecordControlPort()

- static void ForgetControlPort()

- static int RecordedControlPort()
  - The port a running instance recorded, or 0 when there is no usable
    record. Only for the CLI side: the master writes the file.

- static string ctlToken="";

- static string CtlToken()

- static string RecordedCtlToken()
  - The token of the instance that recorded the control port, or "" when
    there is no record.

- static string TrimEol(string s)

- static int ctlProbeMs=1500;

- static int ctlCmdMs=10000;

- static string lastCtlErr="";

- static nint BindCtl(int port, bool reuse)

- static async nint ClaimControlPort()

- static nint MoveOffControlPort(int want, string wantErr)

- static async void RunCommand()
  - Runs the process-management command line, workerman-style:
    
    app                 -- start in the foreground (debug)
    app start           -- same
    app start -d        -- start as a daemon
    app stop            -- stop the running master and its workers
    app restart [-d]    -- stop, then start
    app reload          -- replace the workers one at a time, letting the
    old ones finish what they are serving
    app status          -- print the running server's worker table
    
    The running instance is addressed through its control port, so no pid
    file has to be kept in sync, and the same commands work on Windows,
    where signals are not available.
    
    Start commands never return; the others exit when the master answers.

- static async void SendCommand(string cmd)

- static async void RunAll()
  - Runs every registered Worker. Never returns. In the master
    process with count > 1 this spawns and supervises worker processes;
    in a worker process (or with count == 1) it serves connections.

- static string Pad(string s, int width)

- static string Rule(string title)

- static void PrintBanner(int procs)
  - Prints the workerman-style start-up screen: version line, then
    one row per listener with protocol, address, process count and
    state.

- static long statOsHandle=0;

- static string StatsKey(int wid)

- static SharedTable StatsTable(int procs)

- static void StatsSeed(SharedTable t, int procs)

- static void StatsCreate(int procs)
  - Master (or single process): create the stats table, one row per process.
    Must run before the first worker is spawned -- a worker can only be given
    a handle that already exists.

- static void StatsDestroy()

- static void StatsAttach()

- static List<string> shareNames=new List<string>();

- static List<long> shareHandles=new List<long>();

- static string ShareEnv(string name)

- static void ShareHandle(string name, long osHandle)
  - Hands an anonymous shared table down to every worker this
    master spawns, under <paramref name="name"/>. Must be called before the
    first worker starts: a child only inherits handles that already existed.
    A handle of 0 -- a table that is named rather than anonymous, or one that
    could not be created -- shares nothing, which leaves the worker to open
    the table by name as it otherwise would.

- static long InheritedHandle(string name)
  - Worker side: the handle the master shared under this name, or 0
    in a process that was not spawned as a worker of such a master.

- static void StatsPublish()
  - Publishes this process's counters into its row.

- static async void StatsPublishLoop()

- static long StatsRead(int wid, string column)

- static string Usage()
  - The command line a CLI-driven server answers.

- static void StopAll()
  - Stops all workers in this process (single-process mode).

- void Stop()

- void AddConn()

- void DropConn()

- int GetConnectionCount()

- static void SetWorkerEnv(string name, string val)
  - Sets inherited worker settings without narrowing UTF-8 paths through the Windows ANSI code page.

- static int totalSpawned=0;

- static int maxTotalSpawns=512;

- static void SpawnChild(int wid)

- static List<int> chIdx=new List<int>();

- static List<int> chPid=new List<int>();

- static List<TcpClient> chConn=new List<TcpClient>();

- static int rrCounter=0;

- static string SocketErr()
  - The platform error text for the socket call that just failed. Read it
    immediately: any later socket call -- even a successful close --
    overwrites the platform error.

- static string lastBindErr;

- static string BindFailure(Worker w)

- static bool lastBindErrInUse;

- static bool IsAddrInUse(int err)

- static const int CTL_FREE=0;
  - True when a live Zan master answers PING on the control port.
    
    Accepting the connection is not enough: a listening socket outlives the
    process that created it whenever a handle to it was inherited or
    duplicated (Windows worker handoff), and the kernel completes the
    handshake from the backlog with nobody left to accept it. Treating that
    ghost as a running master made every later start fail with "served by a
    running master" until the leftover handle was found and killed -- a
    reboot being the usual cure. So talk to it: only something that answers
    PONG owns the port as a master.
    
    And only a master of THIS instance is one this process must stand down
    for: the answer carries the token from the record file (see CtlToken),
    so an unrelated server reachable on the same loopback port -- one inside
    WSL, whose ports Windows mirrors -- is reported as foreign instead of
    stopping this app from starting.
    
    Returns CTL_FREE (nothing answers, the port is reclaimable), CTL_OURS
    (this app's master is running -- it proved it by echoing the token from
    the record file), CTL_FOREIGN (a master of some other instance) or
    CTL_UNKNOWN (something answered PONG but named no token, which is how a
    build from before the token answers).

- static const int CTL_OURS=1;

- static const int CTL_FOREIGN=2;

- static const int CTL_UNKNOWN=3;

- static async int ProbeControlPort()

- static async void RunAsMaster(int procs)

- static async void MasterAcceptLoop(int idx, nint sock)

- static int PickChannel(int idx)

- static void DropChannel(int slot)

- static async void HandoffClient(int idx, nint c)

- static async void HandleWorkerControl(nint sock)

- static void DropCtl(TcpClient conn)

- static async void HandleCliCommand(TcpClient conn, string cmd)

- static async void StopWorkersAndExit()

- static async void ReloadWorkers()

- static string StatusText()

- static string SharedTablesText()

- static void PrintSharedTables()

- static string StatusRow(string label, int wid)

- static async void SingleProcessControlLoop()

- static async void HandleSelfCommand(nint sock)

- static async void DrainThenExit()

- static async void RunAsWorkerProcess()

- static async void ChannelLoop(TcpClient ch, Worker w)

- static List<string> SplitWords(string s)

- static async void MasterStatusLoop()

- static async void SelfStatusLoop()

- static async bool RecvExact(TcpClient conn, byte[]dest, int need)
  - Receives exactly `need` bytes into `dest` (binary-safe).

- static int IndexOfByte(string s, int b)

- static async void RunAllLoopsOnSockets(List<nint> socks)
  - Runs all registered workers concurrently on this process's
    scheduler, on sockets the caller has already bound (one per registered
    worker, in order) -- binding first is what lets a bind failure be
    reported before anything is printed about the server having started.
    Every worker but the last is spawned as its own coroutine; the last is
    awaited so this call drives the scheduler forever.

- nint BindSocket(bool reusePort)
  - Binds this worker's socket. For "udp" that is a bound datagram
    socket; otherwise a listening TCP socket. reusePort=true additionally
    sets SO_REUSEPORT before bind (POSIX multi-worker). Returns the socket
    or -1.

- async void RunOnSocket(nint sock)
  - Serves this worker's protocol on an already-bound socket.

- void Dispatch(nint clientSock)

- async void RunRaw(nint clientSock)

- Connection NewConnection(nint sock)

- async void HandleTcp(nint clientSock)

- async void HandleHttp(nint clientSock)

- async void HandleWebSocket(nint clientSock)

- async void HandleSse(nint clientSock)

- async void HandleMqtt(nint clientSock)

- async void RunUdp(nint sock)


## string (delegate)

Invoked for every complete inbound message on a TCP or WebSocket
connection. The returned string (when non-empty) is sent back to the peer
as the reply; return "" to send nothing. Async: may await I/O.

`delegate string MessageHandler(Connection conn, string data);`


## string (delegate)

Invoked per UDP datagram. The returned string (when non-empty) is
sent back to the datagram's sender; return "" to send nothing. Async.

`delegate string DatagramHandler(string data);`


## void (delegate)

Invoked when a client connection is established. Async so the
handler can await I/O (Db/Redis/etc.) without blocking the event loop.

`delegate void ConnectionHandler(Connection conn);`


## void (delegate)

Invoked after a connection is closed (peer disconnect or
conn.Close()). Async: may await I/O.

`delegate void ConnectionCloseHandler(Connection conn);`


## void (delegate)

Invoked when an SSE subscriber completes its handshake. The
application keeps the SseConnection and pushes events through it. Async.

`delegate void SseSubscriberHandler(SseConnection conn);`


## void (delegate)

Invoked with a freshly accepted (and, on Windows, handed-off)
client socket. When set it overrides the built-in per-protocol handling:
the callback owns the socket and drives the whole connection itself. Lets
an application server (e.g. an MVC framework) reuse the master/worker
accept + WSADuplicateSocket handoff + supervision while keeping its own
request pipeline. Async so it can await I/O.

`delegate void RawConnHandler(nint sock);`


## PingStatus (enum)

Ping 返回的强类型结果状态。

- Success

- TimedOut

- DestinationUnreachable

- PacketTooBig

- BadRequest

- Error
