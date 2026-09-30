# System.Net

> 源码: `stdlib/System/Net/Net.zan`, `stdlib/System/Net/NetworkInterface.zan`, `stdlib/System/Net/Ping.zan`, `stdlib/System/Net/ServerBanner.zan`, `stdlib/System/Net/Worker.Mqtt.zan`, `stdlib/System/Net/Worker.Sse.zan`, `stdlib/System/Net/Worker.Ws.zan`, `stdlib/System/Net/Worker.zan`


## BannerService (class)

横幅服务表中的一行：运行什么、监听哪里、
几个进程在服务、是否已启动。

- string name;

- string listen;

- string procs;

- string status;

- BannerService(string name, string listen, string procs, string status)
  - 私有构造：由 `ServerBanner.Service` 创建。


## Connection (class)

One accepted client connection, passed to Worker callbacks. Send pushes
data to the peer from any (including non-async) context; Close marks the
connection for shutdown after the current callback returns.

- [DllImport("crt")]static extern long strlen(string str);

- int id;

- nint sock;

- string protocol;

- string userData;

- bool closed;

- Connection(int id, nint sock, string protocol)

- void SetData(string data)
  - Arbitrary per-connection state for the application.

- string GetData()
  - 取该连接的自定义应用数据（SetData 设置的值）。

- int GetId()
  - 连接 id（进程内自增，唯一）。

- nint GetSocket()
  - 底层 socket 句柄。

- string GetProtocol()
  - 连接所属协议名（如 "http"、"websocket"、"tcp"）。

- void Send(string data)
  - Queues raw bytes to the peer (fire-and-forget; usable from
    synchronous callbacks). On a WebSocket connection prefer WorkerWs.Push
    (framed) or returning the reply from onMessage; Send writes raw
    bytes.

- async int SendAsync(string data)
  - Sends data and suspends until it is written.

- void Close()
  - Marks the connection to be closed by its worker loop.

- bool IsClosed()
  - 连接是否已被标记关闭（实际关闭由 worker 循环执行）。


## IPAddress (class)

IP 地址工具。

- static const string Any="0.0.0.0";

- static const string Loopback="127.0.0.1";

- static const string Broadcast="255.255.255.255";

- static bool IsIPv4(string str)
  - 检查字符串是否为 IPv4 地址格式。


## NetworkInterface (class)

单个网络适配器及其单播地址。Windows 上由
GetAdaptersAddresses 支撑（直接解析原生 x64 ABI 布局）；
Linux/macOS 上由 getifaddrs 支撑（运行时 zan_plat_net_interfaces）。

- public string name;
  - 适配器名（Windows 为 FriendlyName）。

- public string description;
  - 适配器描述；POSIX 上与 name 相同（运行时只报一列）。

- public int index;
  - 接口索引。

- public bool up;
  - 接口是否处于 up（OperStatus 1）。

- public string macAddress;
  - MAC 地址（冒号分隔十六进制；无则为空）。

- public List<string> ipAddresses;
  - 单播地址列表（IPv4 点分十进制 / IPv6 十六进制组）。

- public void Dispose()
  - 主动释放适配器持有的字符串和地址条目。

- static List<NetworkInterface> GetAllNetworkInterfaces()
  - 返回本机已知的所有适配器。

- [DllImport("crt", EntryPoint="zan_plat_net_interfaces")]static extern string NativeInterfaces();
  - 运行时接口枚举，每行一个适配器：
    name \t index \t up \t mac \t addr[,addr...]。

- static List<NetworkInterface> ReadPosix()
  - 解析 getifaddrs 文本输出为适配器列表；
    列数不足或空行跳过，地址为空时 ipAddresses 为空列表。

- [DllImport("iphlpapi", EntryPoint="GetAdaptersAddresses")]static extern int GetAdaptersAddresses(int family, int flags, nint reserved, nint addresses, nint size);

- static List<NetworkInterface> ReadWindows()
  - GetAdaptersAddresses 直读：缓冲不足（rc 111）按返回的
    实际大小重试最多 4 次；非 0 结局返回已收集的空表。

- static string ReadMac(nint bytes, int length)
  - 物理地址字节序列格式化为冒号分隔的大写十六进制
    （最多 8 字节）。

- static string ReadAddress(nint sockaddr, int length)
  - sockaddr 格式化为地址文本：AF_INET（2）为点分 IPv4，
    AF_INET6（23）为 8 组冒号分隔十六进制；其余家族返回空串。

- static string HexByte(int number)
  - 字节为两位大写十六进制。

- static string HexWord(int number)
  - 16 位值为去前导零的大写十六进制（全零输出 "0"）。

- static string HexDigit(int number)
  - 0-15 为单个大写十六进制字符。


## Ping (class)

ICMP echo 客户端，不经过命令行 shell 或外部 ping 可执行程序。
Windows 上直接调 iphlpapi；Linux/macOS 上走运行时的 ICMP socket
（Linux 优先用无需提权的 SOCK_DGRAM/IPPROTO_ICMP “ping socket”，
内核不开放时回退到需 root/CAP_NET_RAW 的 SOCK_RAW）。

- static PingReply Send(string address)
  - 发送 IPv4 echo 请求，超时 4 秒。

- static PingReply Send(string address, int timeoutMs)
  - 发送 IPv4 echo 请求并返回类型化回复。
    发送 IPv4 echo 请求并等待应答，最多
    <paramref name="timeoutMs"/> 毫秒（负值按 0 处理）。地址不是
    合法 IPv4 时抛 ArgumentException（Windows 侧对广播地址
    "255.255.255.255" 网开一面）；非 Windows 下 socket/权限错误
    映射为 `PingStatus.Error`。

- [DllImport("crt", EntryPoint="zan_plat_icmp_ping")]static extern int NativeIcmpPing(string address, int timeoutMs);
  - 平台 ICMP 实现：返回往返毫秒（>= 0），或
    -1 超时 / -2 不可达 / -3 socket 或权限错误 / -4 地址无法解析。

- [DllImport("iphlpapi", EntryPoint="IcmpCreateFile")]static extern nint IcmpCreateFile();

- [DllImport("iphlpapi", EntryPoint="IcmpSendEcho")]static extern int IcmpSendEcho(nint handle, int destination, nint request, ushort requestSize, nint options, nint reply, int replySize, int timeoutMs);

- [DllImport("iphlpapi", EntryPoint="IcmpCloseHandle")]static extern int IcmpCloseHandle(nint handle);

- static PingStatus MapStatus(int status)
  - 把 Windows IP_STATUS 码映射为 `PingStatus`。

- static int ParseIPv4(string text)
  - 点分 IPv4 解析为 Windows IPAddr——网络序字节按小端
    解释的 DWORD（因此 127.0.0.1 变成 0x0100007f）。非法格式返回 -1。

- static int Digit(string ch)
  - 单字符十进制数字；"0" 与非 1-9 字符都返回 0。

- static string FormatIPv4(int packed)
  - IPAddr DWORD 还原为点分十进制。


## PingReply (class)

一条 ICMP echo 结果。

- public PingStatus status;
  - 应答状态。

- public string address;
  - 应答方地址（Windows 取自应答，可能异于请求串）。

- public int roundtripTime;
  - 往返时间（毫秒）。

- public int dataSize;
  - 应答携带的数据字节数。

- public bool IsSuccess()
  - 是否成功收到应答。

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
  - 横幅总宽度（列）。

- ServerBanner(string title, string section)
  - 带标题的服务器启动报告横幅。

- ServerBanner Mode(string m)
  - 进程的布局方式，如 "single process" 或 "master + 4
    workers"。

- ServerBanner Service(string name, string listen, string procs, string status)
  - 在 SERVICES 表登记一行：服务名、监听地址、进程数、状态。

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
  - 把横幅整块打印到标准输出（启动时调用一次）。


## Worker (class)

Workerman-style multi-protocol server: one Worker per listen address,
per-event callbacks, and a master + N worker-processes runtime that
load-balances accepted connections across CPU cores.

Worker w = new Worker("http", "0.0.0.0", 8080);
w.count = 4;                        // 4 worker processes
w.onHttpRequest = App.Handle;       // HttpRequest -> HttpResponse
Worker.RunAll();                    // never returns

Protocols: "tcp", "udp", "http", "websocket" (or "ws"), "sse", "mqtt".
The ws/sse/mqtt protocol machinery compiles in only when its shard is
installed -- call once before RunAll: WorkerWs.Install() (websocket +
ws-upgrade on http ports), WorkerSse.Install(), WorkerMqtt.Install().
RunAll fails fast with a message when a dedicated protocol worker has no
shard installed. Callbacks (assign what the protocol needs); the second
name is an alias reading the way that protocol usually does:
onConnect / onOpen                  -- tcp, websocket: connection up
onMessage / onReceive               -- tcp, websocket: data in, reply out
onClose                             -- tcp, websocket: connection gone
onHttpRequest / onRequest           -- http
onUdpMessage / onPacket             -- udp
WorkerSse.onSseSubscriber           -- sse (shard static, not a field)
(none)                              -- mqtt: connections are served by
the built-in MqttBroker.Global()

An "http" worker with onOpen/onMessage set also serves WebSocket on that
same port once WorkerWs.Install() has run: a request asking to upgrade
becomes a frame loop, everything else is answered by onRequest.

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
  - TCP 连接建立后调用（tcp 协议；可 await I/O）。

- MessageHandler onMessage;
  - 收到一条完整入站消息后调用；返回串作为应答回发，返回 "" 不回发。

- ConnectionCloseHandler onClose;
  - 连接关闭（对端断开或 conn.Close() 生效）后调用。

- HttpRequestHandler onHttpRequest;
  - 每个解析完成的 HTTP 请求调用，返回响应对象。

- DatagramHandler onUdpMessage;
  - 每个完整 UDP 数据报调用；返回串回发给发送方，返回 "" 不回发。

- RawConnHandler onRawConnection;
  - 设置后接管该 Worker 的整条连接：socket 交给回调自主驱动，
    绕过内建协议处理与连接数闸门。

- ConnectionHandler onOpen;
  - onConnect 的 WebSocket 叫法（连接已建立）。

- MessageHandler onReceive;
  - onMessage 的 TCP 叫法（收到字节）。

- DatagramHandler onPacket;
  - onUdpMessage 的 UDP 叫法（收到数据报）。

- HttpRequestHandler onRequest;
  - onHttpRequest 的 HTTP 叫法（请求已解析）。

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
  - 本进程累计服务的请求/消息数（workerman 风格 `status` 统计）。

- static void BumpStat()
  - 静态上下文里的计数自增；实例 async 方法中直接自增会被按值捕获而丢失。

- static AtomicInt statSent=new AtomicInt(0);
  - 本进程累计发回对端的消息/帧/数据报数。

- static void BumpSent()
  - 发送计数自增（同 BumpStat 的静态上下文约束）。

- static int LiveConnections()
  - 本进程所有 Worker 的在线连接总数。

- static int statusInterval=10;

- static SharedTable statTable=null;

- static List<long> stPrevReq=new List<long>();

- static List<long> stPrevCpuMs=new List<long>();

- [DllImport("crt")]static extern void exit(int code);

- [DllImport("crt")]static extern long strlen(string str);

- static bool debugLog=false;

- static void Dbg(string msg)
  - 调试跟踪输出：SetDebug(true) 时打印 master/worker 交接与握手轨迹。

- [DllImport("crt")]static extern int _flushall();

- static void SetDebug(bool on)
  - Enables verbose master/worker handoff tracing.

- [DllImport("crt")]static extern int chmod(string path, int mode);

- [DllImport("ws2_32", EntryPoint="WSADuplicateSocketA")]static extern int WSADuplicateSocketA(nint s, int pid, string info);

- [DllImport("ws2_32", EntryPoint="WSASocketA")]static extern nint WSASocketA(int af, int type, int protocol, string info, int g, int flags);

- [DllImport("kernel32", EntryPoint="SetEnvironmentVariableW")]static extern int SetEnvironmentVariableW(nint name, nint val);

- Worker(string protocol, string host, int port)
  - 按协议、监听地址与端口构造 Worker 并注册进全局 Worker 列表；
    经 Listen() 使用。

- ConnectionHandler OpenHandler()
  - onOpen 与 onConnect 中已设置的那个（别名归一）。

- MessageHandler MessageHandlerOf()
  - onMessage 与 onReceive 中已设置的那个。

- DatagramHandler PacketHandler()
  - onPacket 与 onUdpMessage 中已设置的那个。

- HttpRequestHandler RequestHandler()
  - onRequest 与 onHttpRequest 中已设置的那个。

- bool HasWsHandlers()
  - Whether this worker can serve WebSocket connections, i.e. whether an HTTP
    port should honour an Upgrade request instead of answering it as HTTP.

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
  - master 启动初期：应用未自配 Log 时按 logDir/logPattern 配置日志，确保监督事件落盘。

- static void EnsureLogDir()
  - 确保当前日志文件所在目录存在（每次 spawn 前调用）。

- static int MaxCount()
  - 所有注册 Worker 中最大的 worker 进程数。

- static int ControlPort()
  - 控制端口取值顺序：显式指定 > 环境变量 ZAN_CTL_PORT > 首 Worker 端口+10000（无 Worker 时 19999）。

- static string appId="";
  - What identifies this server among everything else on the machine: its
    own executable, the way workerman keys a server by its entry script. Two
    copies in two directories are two servers and share nothing; the same
    copy started again is the same server, so it finds what its previous run
    left behind. NOT the port -- a port is configuration, it changes, and
    the control port even moves at startup when something else holds it.

- static string AppId()
  - 服务器实例标识：自身可执行文件路径的哈希（同副本重启即同实例，与端口无关）。

- static string CtlPortFile()
  - The default control port is derived from the service port, so an
    unrelated program holding it used to be fatal even though nothing
    outside the process group ever talks to it. The master may move to
    another port and records the one it claimed here, so stop/status/reload
    still find it.

- static void RecordControlPort()
  - master 把实际认领的控制端口与实例令牌写入记录文件，供 stop/status/reload 寻址。

- static void ForgetControlPort()
  - 停机时删除控制端口记录文件。

- static int RecordedControlPort()
  - The port a running instance recorded, or 0 when there is no usable
    record. Only for the CLI side: the master writes the file.

- static string ctlToken="";

- static string CtlToken()
  - 本实例令牌：CSPRNG 16 字节的 32 位 hex（A291②）。原先的
    "<pid>-<unix秒>" 可被本机任何进程按进程表+时间预算出来，从而在
    控制端口上伪造 PONG 冒充本实例 master（影响 start 的占用判定与
    stop/status 的寻址结论）。令牌只做身份甄别，不是控制命令的鉴权
    凭据——CSPRNG 不可用时退回旧形态，甄别仍然工作。

- static string ByteHex(byte[]b, int n)
  - 字节数组的低位在前 hex 编码（每字节两位）。

- static string RecordedCtlToken()
  - The token of the instance that recorded the control port, or "" when
    there is no record.

- static string TrimEol(string s)
  - 去除字符串尾部的换行、空格与制表符。

- static int ctlProbeMs=1500;

- static int ctlCmdMs=10000;

- static string lastCtlErr="";

- static nint BindCtl(int port, bool reuse)
  - Binds the control socket on one port. Returns the socket, or -1 with the
    reason in lastCtlErr.

- static async nint ClaimControlPort()
  - Claims a control port for this instance: the wanted one if it is free, a
    reclaimed leftover from a previous run, or -- unless the port was named
    explicitly -- the next free port after it. Returns the bound socket and
    leaves ControlPort() reporting the port actually claimed, or -1.

- static nint MoveOffControlPort(int want, string wantErr)
  - The control port is internal: only this master and its own workers ever
    connect to it, and they are told which port to use. So an unrelated
    program sitting on the derived default is no reason to refuse to start --
    move to the next free port and record it for stop/status/reload.

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
  - Sends one command line to the running master and prints its answer. The
    running instance may have moved off the derived port, so its record wins
    over the default -- unless the port was named on the command line.

- static async void RunAll()
  - Runs every registered Worker. Never returns. Command-mode
    servers always use a supervising master, including a single worker;
    direct RunAll callers keep the lightweight single-process path when
    count is 1.

- static string Pad(string s, int width)
  - 状态屏文本按宽度左对齐补空格（委托 ServerBanner.Pad）。

- static string Rule(string title)
  - 状态屏分隔线（委托 ServerBanner.Rule）。

- static void PrintBanner(int procs)
  - Prints the workerman-style start-up screen: version line, then
    one row per listener with protocol, address, process count and
    state.

- static long statOsHandle=0;

- static string StatsKey(int wid)
  - 统计表行键：wid>0 为 "w<id>"，否则 "self"。

- static SharedTable StatsTable(int procs)
  - 构造跨进程统计共享表的列布局（pid/recv/sent/conns/cpu/rss）。

- static void StatsSeed(SharedTable t, int procs)
  - Seeds the rows so a worker's first publish writes into an existing row
    instead of racing to create one.

- static void StatsCreate(int procs)
  - Master (or single process): create the stats table, one row per process.
    Must run before the first worker is spawned -- a worker can only be given
    a handle that already exists.

- static void StatsDestroy()
  - Master only, on the way out. The kernel would reclaim the memory anyway
    once the last worker holding it is gone -- there is no name and no file to
    clean up -- but dropping it here means a master that keeps running after
    its workers (a failed start) is not sitting on a mapping.

- static void StatsAttach()
  - Worker side: map the table the master created and handed down. Nothing to
    attach to (a stats table that failed to be created, or an executable run
    as a worker by something other than our master) leaves the worker without
    one, which only costs it its row in `status`.

- static List<string> shareNames=new List<string>();

- static List<long> shareHandles=new List<long>();

- static string ShareEnv(string name)
  - 共享表句柄经环境变量传递时的变量名（ZAN_SHARE_<name>）。

- static void ShareHandle(string name, long osHandle)
  - Hands an anonymous shared table down to every worker this
    master spawns, under <paramref name="name"/>. Must be called before the
    first worker starts: a child only inherits handles that already existed.
    A handle of 0 -- one that could not be created -- shares nothing.

- static long InheritedHandle(string name)
  - Worker side: the handle the master shared under this name, or 0
    in a process that was not spawned as a worker of such a master.

- static void StatsPublish()
  - Publishes this process's counters into its row.

- static async void StatsPublishLoop()
  - 后台循环：每 2 秒把本进程计数器发布进统计表。

- static long StatsRead(int wid, string column)
  - 读取指定 worker 行的统计列；统计表不可用时返回 0。

- static string Usage()
  - The command line a CLI-driven server answers.

- static void StopAll()
  - Stops all workers in this process (single-process mode);
    worker processes are managed by the master instead.

- void Stop()
  - Requests this worker's event loop to exit after the current
    iteration; the running coroutine is not interrupted.

- void AddConn()
  - 连接计数 +1（包装一层以便集中丢弃原子自增的返回值）。

- void DropConn()
  - 连接计数 -1。

- int GetConnectionCount()
  - 当前活连接数（本进程视角；多进程时其他 worker 的
    连接不计入）。

- static void SetWorkerEnv(string name, string val)
  - Sets inherited worker settings without narrowing UTF-8 paths through the Windows ANSI code page.

- static int totalSpawned=0;

- static int maxTotalSpawns=512;

- static void SpawnChild(int wid)
  - Master 派生一个 worker 子进程（wid 从 1 起）：经环境变量（Windows）或
    命令行前缀（POSIX）传递 worker id、控制端口、统计表句柄、日志目录与
    共享表句柄；总派生数受 maxTotalSpawns 硬上限保护。

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
  - 监听绑定失败的完整错误信息；端口被占用时给出可操作的两种处理建议。

- static bool lastBindErrInUse;

- static bool IsAddrInUse(int err)
  - 平台错误号是否表示"地址已被占用"（WSA 10048 / macOS 48 / Linux 98）。

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
  - 向控制端口发 PING 并按应答（含令牌核对）返回 CTL_FREE/CTL_OURS/CTL_FOREIGN/CTL_UNKNOWN。

- static async void RunAsMaster(int procs)
  - master 主流程：先认领控制端口（fork 炸弹防护），再绑定监听、建统计表、派生并监督 procs 个 worker。

- static async void MasterAcceptLoop(int idx, nint sock)
  - Windows：listener idx 的 master 接受循环，事件驱动 accept 后把连接轮流移交 worker。

- static int PickChannel(int idx)
  - Round-robin pick of a live channel serving listener `idx`; -1 if none.

- static void DropChannel(int slot)
  - 关闭并移除一个失效的 handoff 通道（worker 已死/ACK 失败）。

- static async void HandoffClient(int idx, nint c)
  - Duplicates accepted socket `c` into a worker process and closes the
    master's copy once the worker ACKs that it rebuilt the socket.

- static async void HandleWorkerControl(nint sock)
  - 处理一条控制端口连接。首行是握手："HELLO <pid> <wid>"（控制/存活
    连接）、"CHAN <pid> <listenerIdx>"（Windows 交接通道），CLI 的单词
    命令 PING/STOP/RELOAD/STATUS 也在此分流。HELLO 连接随后作为存活
    监视长驻：对端关闭即视为 worker 退出，延迟 1s 后重启（stopping
    置位时不重启，避免停机后复活）。

- static void DropCtl(TcpClient conn)
  - worker 退出后将其控制连接从存活表中移除。

- static async void HandleCliCommand(TcpClient conn, string cmd)
  - Master side of `app stop|reload|status`.
    
    stop:   ask every worker to quit (each finishes what it is serving),
    wait for their control connections to close, then exit.
    reload: ask them to quit one at a time; the supervisor respawns each one,
    so the port is served throughout.
    status: send back the same table the foreground master prints.

- static async void StopWorkersAndExit()
  - 依次向所有 worker 发 QUIT，等待它们的控制连接关闭（上限 30s）后
    清理端口记录与统计表并 exit(0)。

- static async void ReloadWorkers()
  - Replaces the workers one at a time: each is asked to quit, finishes what
    it is serving and exits, the supervisor respawns it, and only then is the
    next one touched -- so the listener is served throughout.

- static string StatusText()
  - The status table `app status` prints, built from the workers' last
    reports (master) or this process's own counters (single process).

- static string SharedTablesText()
  - What this process is holding in shared memory. Reserved is the whole
    mapping a table sets aside for its capacity; "in memory" is the part of
    it that is actually resident -- the number that belongs in a memory
    report, and usually a small fraction of the first.

- static void PrintSharedTables()
  - 启动屏打印本进程共享内存表占用情况。

- static string StatusRow(string label, int wid)
  - One process row of the status table, read from the shared stats table.

- static async void SingleProcessControlLoop()
  - Control listener for a single-process server started through RunCommand:
    the same stop/reload/status commands, answered without a master.

- static async void HandleSelfCommand(nint sock)
  - 单进程服务器的控制命令处理：与 master 相同的 PING/STATUS/STOP/
    RELOAD；RELOAD 回复提示改用 restart（没有可替换的 worker 进程）。

- static async void DrainThenExit()
  - Stops accepting and leaves once the connections in flight are done (or
    the grace period is over), so a stop/reload does not cut off a response
    that is halfway out.

- static async void RunAsWorkerProcess()
  - worker 进程入口：向 master 的控制端口发 HELLO 注册。Windows 上为每个
    TCP listener 开一条 CHAN 交接通道并服务 master 递来的 socket；POSIX 上
    等 GO 后自行以 SO_REUSEPORT 绑定各 listener。随后挂载统计上报，并长驻
    监视控制连接：master 关闭即退出，收到 QUIT 则排空在途请求后退出。

- static async void ChannelLoop(TcpClient ch, Worker w)
  - Worker side of a handoff channel: each inbound 372-byte
    WSAPROTOCOL_INFOA block is one accepted client connection. Rebuild the
    socket, ACK so the master can close its copy, and serve it.

- static List<string> SplitWords(string s)
  - 按空白字符（空格/制表/回车/换行）切分为单词列表。

- static async void MasterStatusLoop()
  - Foreground master: print one status block per interval, computing each
    worker's qps and cpu%% from the deltas between its last two reports.

- static async void SelfStatusLoop()
  - Single-process server: same status line from in-process counters.

- static async bool RecvExact(TcpClient conn, byte[]dest, int need)
  - Receives exactly `need` bytes into `dest` (binary-safe).

- static int IndexOfByte(string s, int b)
  - 返回字节 b 在字符串中首次出现的下标，未找到返回 -1。

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

- static ProtoEntryFn wsEntry;

- static ProtoEntryFn sseEntry;

- static ProtoEntryFn mqttEntry;

- async void RunOnSocket(nint sock)
  - Serves this worker's protocol on an already-bound socket.

- void Dispatch(nint clientSock)
  - 分发一个已接受的客户端 socket：先设 NoDelay；onRawConnection 在场时
    整条连接交给它（回调自主计数，不计入连接数闸门）；否则超过
    maxConnections 的连接直接关闭，余者按协议路由到 HTTP/WebSocket/
    SSE/MQTT/TCP 处理协程。

- async void RunProto(ProtoEntryFn entry, nint clientSock)
  - Spawnable wrapper around a protocol-shard entry. Task.Spawn needs a
    concrete async method frame (same reason as RunRaw); the shard entry
    itself arrives as a delegate. 槽为空（分片未装）时关连接归还计数，
    不 NRE——专用协议在 RunAll 已先行报错，这里是兜底。

- static void CheckProtoEntries()
  - RunAll 前的协议分片检查：专用协议（websocket/sse/mqtt）的入口槽为
    空说明对应分片没装（WorkerWs/WorkerSse/WorkerMqtt.Install()），
    直接报错退出——比第一个连接进来时静默断开可诊断得多。http 端口
    设了 ws 回调但没装分片只告警：明文 HTTP 语义不受影响。这里只读
    槽字段，不拼分片类型名（拼了会把分片拉回编译图，槽就白拆了）。

- async void RunRaw(nint clientSock)
  - Spawnable async wrapper around the raw-connection delegate. Task.Spawn
    needs a concrete async method frame; awaiting the delegate here uses the
    proven indirect async-call path (spawning the delegate call directly does
    not build a task frame).

- Connection NewConnection(nint sock)
  - 分配自增连接 id 并构造 Connection。

- async void HandleTcp(nint clientSock)
  - TCP 连接生命周期：先触发 onConnect，再循环收取数据交给 onMessage
    （未设回调时默认回显），非空返回串回发；对端断开后触发 onClose 并
    归还连接计数。回调抛出的异常会跳过顺序清理（Close/DropConn），
    计数漂移会让服务器在 maxConnections 处永久拒绝新连接，因此
    清理在 finally 中执行。

- async void HandleHttp(nint clientSock)
  - HTTP framing, limits and dispatch live in exactly one place --
    HttpServer.ServeConnection -- which this protocol runs as-is, so the
    Worker path and a bare HttpServer cannot drift apart. DropConn 在
    finally 中：ServeConnection 抛出时计数同样必须归还。

- async void RunUdp(nint sock)
  - UDP 事件循环：每收到一个数据报调用 onPacket（未设回调时默认回显），
    非空返回串回发给发送方；Stop 置位退出循环后关闭 socket。


## WorkerMqtt (class)

MQTT 协议分片（A332 肥边③）：把 mqtt 端口的连接交给内置
MqttBroker.Global() 处理，从 Worker.zan 外迁为独立类。应用调用一次
WorkerMqtt.Install() 本文件才进编译图（并连带拉入 Mqtt 全家——
broker + client 约 1300 行），纯 HTTP 服务不背 MQTT。mqtt 协议没有
应用回调。槽反转与独立类（不写 partial Worker）的理由见
Worker.ProtoEntryFn 的文档。

- static void Install()
  - Installs the MQTT shard into the Worker entry slot. Call once
    before Worker.RunAll().

- static void Uninstall()
  - 进程退出前的拆卸入口：解除协议分片并停机/清空
    进程级 broker。静态 mqttEntry 与 MqttBroker.inst 是泄漏
    探测里的保持根，不拆就恒报"退出仍可达"（A352）。

- static async void HandleMqtt(Worker w, nint clientSock)
  - 把连接交给内置的 MqttBroker.Global() 处理。


## WorkerSse (class)

SSE 协议分片（A332 肥边③）：订阅握手/事件流处理，从 Worker.zan 外迁
为独立类。应用调用一次 WorkerSse.Install() 本文件才进编译图（并连带
拉入 Sse.zan），纯 HTTP 服务不背 SSE；订阅回调从原来的 Worker 字段
改为这里的静态字段 WorkerSse.onSseSubscriber（签名引用 SseConnection，
放在核心会把 SSE 拖回编译图）。槽反转与独立类（不写 partial Worker）
的理由见 Worker.ProtoEntryFn 的文档。

- static SseSubscriberHandler onSseSubscriber;
  - SSE 订阅者握手完成后调用；应用持有 SseConnection 主动推事件。

- static List<SseConnection> conns=new List<SseConnection>();

- static nint sseLock=0;

- static WsSharedBus sharedBus=null;

- static WorkerSseConsumer busConsumer=null;

- static void SseInit()

- static void EnableSharedBus(string busName)
  - 启用基于 SharedTable 的跨进程 SSE 广播总线。

- static void Install()
  - Installs the SSE shard into the Worker entry slot. Call once
    before Worker.RunAll().

- static void DisableSharedBus()
  - 停掉跨进程总线轮询并解除静态持有。sharedBus/
    busConsumer 是 WsSharedBus→SharedTable 链在泄漏探测里的
    保持根，不拆就恒报"退出仍可达"（A352）。

- static void Uninstall()
  - 进程退出前的拆卸入口：解除协议分片、订阅者回调
    并停总线，与 Install() 对称。

- static void Attach(SseConnection conn)
  - 登记本进程活跃的 SSE 连接。

- static void Detach(SseConnection conn)
  - 注销 SSE 连接。

- static int ConnCount()
  - 本 Worker 进程在线 SSE 连接数。

- static int ClusterConnCount()
  - 获取集群跨 Worker 实时在线 SSE 连接总数。

- static async int BroadcastLocal(string eventName, string data)
  - 向本进程内所有活跃 SSE 连接本地广播事件。

- static async int Broadcast(string eventName, string data)
  - 跨 Worker 向全集群所有在线 SSE 客户端广播具名事件。

- static async int Broadcast(string data)
  - 跨 Worker 向全集群所有在线 SSE 客户端广播无名事件。

- public static async void OnSharedMessage(int targetType, string channel, int targetId, string payload, int payloadLen)

- static async void HandleSse(Worker w, nint clientSock)
  - SSE 订阅：读取 GET 请求头后回 200 text/event-stream（带 no-cache 与
    CORS 通配头），把连接包成 SseConnection 交给 onSseSubscriber（未设
    回调时立即关闭）；此后连接由应用持有。请求头读取与 HTTP 路径
    一样武装 requestTimeout（慢速滴字客户端不能永久钉住连接），
    清理在 finally 中执行——onSseSubscriber 抛出时 DropConn 不能丢。


## WorkerSseConsumer (class)

- public void OnSharedBusMessage(int targetType, string channel, int targetId, string payload, int payloadLen)


## WorkerWs (class)

WebSocket 协议分片（A332 肥边③）：握手/帧循环/连接注册表/Push 的实现，
从 Worker.zan 外迁为独立类。Worker 核心只经静态槽分发（Worker.wsEntry），
应用调用一次 WorkerWs.Install() 本文件才进编译图——纯 HTTP 服务不背
ws 编解码（约 870 行），正如不背 SSE/MQTT。槽反转与独立类（不写
partial Worker）的理由见 Worker.ProtoEntryFn 的文档。

- [DllImport("crt")]static extern long strlen(string str);

- static void Install()
  - Installs the WebSocket shard into the Worker entry slot.
    Call once before Worker.RunAll(); a worker-process copy re-runs it
    through its own Main.

- static void DisableSharedBus()
  - 停掉跨进程总线轮询并解除静态持有。sharedBus/
    busConsumer 是 WsSharedBus→SharedTable 链在泄漏探测里的
    保持根，不拆就恒报"退出仍可达"（A352）。

- static void Uninstall()
  - 进程退出前的拆卸入口：解除协议分片并停总线，
    与 Install() 对称。

- static WsSharedBus sharedBus=null;

- static WorkerWsConsumer busConsumer=null;

- static Dict <string, List<Connection>> channels=new Dict <string, List<Connection>>();

- static nint chanLock=0;

- static void ChanInit()

- static void EnableSharedBus(string busName)
  - 启用基于 SharedTable 共享内存的跨 Worker 进程 WebSocket 消息总线。

- static void Join(string channel, Connection conn)
  - 订阅者加入频道/房间。

- static void Leave(string channel, Connection conn)
  - 订阅者离开频道/房间。

- static void LeaveAllChannels(Connection conn)
  - 断开时从所有频道中自动清理该连接。

- static Dict <int, WsLink> conns=new Dict <int, WsLink>();

- static nint regLock=0;

- static void RegInit()
  - 惰性创建注册表用的互斥锁。

- static void Attach(Connection conn, WsWriter writer, TcpClient client)
  - Upgrade 成功后由 HandleWebSocket 调用（单线程 accept 路径）：
    出站 writer/client 挂到连接的注册项上，Push/Broadcast 经 id 找回。

- static void ConnRemove(Connection conn)
  - 连接结束后由 HandleWebSocket 调用（O(1) 删除）。

- static WsLink LinkOf(int id)
  - 按连接 id 找注册项；不存在（已断开）返回 null（O(1) 哈希查找）。

- static int ConnCount()
  - 本进程当前在线 WebSocket 连接数。

- static int ClusterConnCount()
  - 获取集群跨 Worker 实时在线 WebSocket 连接总数。

- static Connection ConnById(int id)
  - 按连接 id 取在线连接；不存在（已断开）返回 null。
    用于定时器等无引用上下文里找回连接后 WorkerWs.Push(conn, ...)。

- static async int BroadcastLocal(string data)
  - 向本进程内所有在线 WebSocket 连接广播一条 Text 帧，返回成功投递数。

- static async int Broadcast(string data)
  - 向所有 Worker 进程的所有在线 WebSocket 连接全局广播一条 Text 帧。
    本进程连接立即本地推送，其他 Worker 进程经 SharedTable 环形总线接收后推送。

- static async int BroadcastToLocal(string channel, string data)
  - 向本进程内加入指定频道/房间的所有连接广播一条 Text 帧。

- static async int BroadcastTo(string channel, string data)
  - 跨 Worker 向加入指定频道/房间的所有在线连接广播 Text 帧。

- static async bool PushTo(int connId, string data)
  - 精准向指定连接 ID 发送消息。若该连接在本进程则立即发送；
    若在集群其他 Worker 进程，则经 SharedTable 路由至目标进程发送。

- public static async void OnSharedMessage(int targetType, string channel, int targetId, string payload, int payloadLen)
  - 接收来自其他 Worker 进程发来的跨进程共享总线消息。

- static async bool Push(Connection c, string data)
  - WebSocket push：把一条 Text 帧排进该连接的出站缓冲并立即
    写出（服务端帧不掩码）。可从任何回调上下文调用——包括 onOpen 里
    主动欢迎、定时器里广播等没有入站消息先行的场景，这正是与"从
    onMessage 返回应答"的区别。连接已关或未升级返回 false。
    帧序与帧循环共用同一缓冲，Append 到 Flush 之间不让出，天然一致。

- static async bool PushBinary(Connection c, string data, int len)
  - WebSocket 二进制 push：`data` 可含 NUL，`len` 为精确
    字节数（绝不 strlen）。语义同 Push。

- static async void HandleWebSocket(Worker w, nint clientSock)
  - A WebSocket port is an HTTP port until the client asks to upgrade, so the
    request head is read and parsed first: an Upgrade request becomes a frame
    loop, anything else is served as HTTP (swoole's WebSocket\Server does the
    same). The bytes consumed here are handed to whichever path takes over.


## WorkerWsConsumer (class)

- public void OnSharedBusMessage(int targetType, string channel, int targetId, string payload, int payloadLen)


## WsLink (class)

一条在线 WebSocket 连接的注册项：连接对象 + 出站 writer + 底层
socket。原先是 Connection 上的 wsWriter/wsClient 字段——字段类型
引用 ws 家族会把编解码拖回核心文件，故随分片外迁、按连接 id 关联。

- Connection conn;

- WsWriter writer;

- TcpClient client;

- bool busy;

- List<AsyncGate> waiters;

- async bool LockAsync()

- void Unlock()


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

Invoked when an SSE subscriber completes its handshake. The
application keeps the SseConnection and pushes events through it. Async.
（原 Worker.zan 顶层委托——签名引用 SseConnection，随 SSE 分片外迁，
否则核心文件会把 SSE 家族拖进一切 Worker 程序。）

`delegate void SseSubscriberHandler(SseConnection conn);`


## void (delegate)

Invoked when a client connection is established. Async so the
handler can await I/O (Db/Redis/etc.) without blocking the event loop.

`delegate void ConnectionHandler(Connection conn);`


## void (delegate)

Invoked after a connection is closed (peer disconnect or
conn.Close()). Async: may await I/O.

`delegate void ConnectionCloseHandler(Connection conn);`


## void (delegate)

Invoked with a freshly accepted (and, on Windows, handed-off)
client socket. When set it overrides the built-in per-protocol handling:
the callback owns the socket and drives the whole connection itself. Lets
an application server (e.g. an MVC framework) reuse the master/worker
accept + WSADuplicateSocket handoff + supervision while keeping its own
request pipeline. Async so it can await I/O.

`delegate void RawConnHandler(nint sock);`


## void (delegate)

Per-connection entry of a protocol shard (ws/sse/mqtt). The
implementations live in Worker.Ws/Sse/Mqtt.zan as independent classes
(WorkerWs/WorkerSse/WorkerMqtt) that install themselves into the static
entry slots on Worker from their Install(); until an app calls those, the
shard files never join the compile graph at all (A332 lazy pull-in: a
stdlib file is pulled only when a type it declares is spelled in type
position, so a pure-HTTP server compiles without the ws codec, the SSE
helper or the MQTT broker). Async so it can await I/O.

`delegate void ProtoEntryFn(Worker w, nint sock);`


## PingStatus (enum)

Ping 返回的强类型结果状态。

- Success

- TimedOut

- DestinationUnreachable

- PacketTooBig

- BadRequest

- Error
