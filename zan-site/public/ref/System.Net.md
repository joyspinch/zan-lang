# System.Net

> 源码: `packages/Zan.Net/src/System/Net/Net.zan`, `packages/Zan.Net/src/System/Net/NetworkInterface.zan`, `packages/Zan.Net/src/System/Net/Ping.zan`, `packages/Zan.Net/src/System/Net/ServerBanner.zan`, `packages/Zan.Net/src/System/Net/Worker.Mqtt.zan`, `packages/Zan.Net/src/System/Net/Worker.Sse.zan`, `packages/Zan.Net/src/System/Net/Worker.Ws.zan`, `packages/Zan.Net/src/System/Net/Worker.zan`


## BannerService (class)

- string name;

- string listen;

- string procs;

- string status;

- BannerService(string name, string listen, string procs, string status)


## Connection (class)

- [DllImport("crt")]static extern long strlen(string str);

- int id;

- nint sock;

- string protocol;

- string userData;

- bool closed;

- Connection(int id, nint sock, string protocol)

- void SetData(string data)

- string GetData()

- int GetId()

- nint GetSocket()

- string GetProtocol()

- void Send(string data)

- async int SendAsync(string data)

- void Close()

- bool IsClosed()


## IPAddress (class)

- static const string Any="0.0.0.0";

- static const string Loopback="127.0.0.1";

- static const string Broadcast="255.255.255.255";

- static bool IsIPv4(string str)


## NetworkInterface (class)

- public string name;

- public string description;

- public int index;

- public bool up;

- public string macAddress;

- public List<string> ipAddresses;

- public void Dispose()

- static List<NetworkInterface> GetAllNetworkInterfaces()

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

- static PingReply Send(string address)

- static async Task<PingReply> SendAsync(string address)

- static async Task<PingReply> SendAsync(string address, int timeoutMs)

- static PingReply Send(string address, int timeoutMs)

- [DllImport("crt", EntryPoint="zan_plat_icmp_ping")]static extern int NativeIcmpPing(string address, int timeoutMs);

- [DllImport("iphlpapi", EntryPoint="IcmpCreateFile")]static extern nint IcmpCreateFile();

- [DllImport("iphlpapi", EntryPoint="IcmpSendEcho")]static extern int IcmpSendEcho(nint handle, int destination, nint request, ushort requestSize, nint options, nint reply, int replySize, int timeoutMs);

- [DllImport("iphlpapi", EntryPoint="IcmpCloseHandle")]static extern int IcmpCloseHandle(nint handle);

- static PingStatus MapStatus(int status)

- static int ParseIPv4(string text)

- static int Digit(string ch)

- static string FormatIPv4(int packed)


## PingReply (class)

- public PingStatus status;

- public string address;

- public int roundtripTime;

- public int dataSize;

- public bool IsSuccess()

- public void Dispose()


## ServerBanner (class)

- string title;

- string section;

- string mode;

- string commands;

- string footer;

- List<string> infos;

- List<BannerService> services;

- static int Width()

- ServerBanner(string title, string section)

- ServerBanner Mode(string m)

- ServerBanner Service(string name, string listen, string procs, string status)

- ServerBanner Info(string name, string detail)

- ServerBanner Commands(string usage)

- ServerBanner Footer(string text)

- static string Pad(string s, int width)

- static string Rule(string title)

- void Print()


## Worker (class)

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

- ConnectionHandler onConnect;

- MessageHandler onMessage;

- ConnectionCloseHandler onClose;

- HttpRequestHandler onHttpRequest;

- DatagramHandler onUdpMessage;

- RawConnHandler onRawConnection;

- ConnectionHandler onOpen;

- MessageHandler onReceive;

- DatagramHandler onPacket;

- HttpRequestHandler onRequest;

- static List<Worker> workers=new List<Worker>();

- static bool daemonize=false;

- static bool cliMode=false;

- static bool stopping=false;

- static List<ShutdownHook> shutdownHooks=new List<ShutdownHook>();

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

- [DllImport("crt")]static extern long strlen(string str);

- static bool debugLog=false;

- static void Dbg(string msg)

- [DllImport("crt")]static extern int _flushall();

- static void SetDebug(bool on)

- [DllImport("crt")]static extern int chmod(string path, int mode);

- [DllImport("crt")]static extern int setenv(string name, string val, int overwrite);

- [DllImport("crt")]static extern int unsetenv(string name);

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

- Worker SetName(string name)

- Worker SetMaxConnections(int max)

- Worker SetHttpLimits(int maxHeaderBytes, int maxRequestBytes, int timeoutMs)

- Worker SetCount(int count)

- static void SetDaemonize(bool on)

- static void SetControlPort(int port)

- static void SetStatusInterval(int seconds)

- static void SetLogDir(string dir)

- static string LogDir()

- static void SetLogPattern(string pattern)

- static string LogPath()

- static void EnsureMasterLog()

- static void EnsureLogDir()

- static int MaxCount()

- static int ControlPort()

- static string appId="";

- static string AppId()

- static string CtlPortFile()

- static void RecordControlPort()

- static void ForgetControlPort()

- static int RecordedControlPort()

- static string ctlToken="";

- static string CtlToken()

- static string ByteHex(byte[]b, int n)

- static string RecordedCtlToken()

- static string TrimEol(string s)

- static int ctlProbeMs=1500;

- static int ctlCmdMs=10000;

- static string lastCtlErr="";

- static nint BindCtl(int port, bool reuse)

- static async nint ClaimControlPort()

- static nint MoveOffControlPort(int want, string wantErr)

- static async void RunCommand()

- static async void SendCommand(string cmd)

- static async void RunAll()

- static string Pad(string s, int width)

- static string Rule(string title)

- static void PrintBanner(int procs)

- static long statOsHandle=0;

- static string StatsKey(int wid)

- static SharedTable StatsTable(int procs)

- static void StatsSeed(SharedTable t, int procs)

- static void StatsCreate(int procs)

- static void StatsDestroy()

- static void StatsAttach()

- static List<string> shareNames=new List<string>();

- static List<long> shareHandles=new List<long>();

- static string ShareEnv(string name)

- static void ShareHandle(string name, long osHandle)

- static long InheritedHandle(string name)

- static void StatsPublish()

- static async void StatsPublishLoop()

- static long StatsRead(int wid, string column)

- static string Usage()

- static void StopAll()

- void Stop()

- void AddConn()

- void DropConn()

- int GetConnectionCount()

- static void SetWorkerEnv(string name, string val)

- static int totalSpawned=0;

- static int maxTotalSpawns=512;

- static void SpawnChild(int wid)

- static List<int> chIdx=new List<int>();

- static List<int> chPid=new List<int>();

- static List<TcpClient> chConn=new List<TcpClient>();

- static int rrCounter=0;

- static string SocketErr()

- static string lastBindErr;

- static string BindFailure(Worker w)

- static bool lastBindErrInUse;

- static bool IsAddrInUse(int err)

- static const int CTL_FREE=0;

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

- static async void RunShutdownHooks()

- static void OnShutdown(ShutdownHook hook)

- static async void RunAsWorkerProcess()

- static async void ChannelLoop(TcpClient ch, Worker w)

- static List<string> SplitWords(string s)

- static async void MasterStatusLoop()

- static async void SelfStatusLoop()

- static async bool RecvExact(TcpClient conn, byte[]dest, int need)

- static int IndexOfByte(string s, int b)

- static async void RunAllLoopsOnSockets(List<nint> socks)

- nint BindSocket(bool reusePort)

- static ProtoEntryFn wsEntry;

- static ProtoEntryFn sseEntry;

- static ProtoEntryFn mqttEntry;

- async void RunOnSocket(nint sock)

- void Dispatch(nint clientSock)

- async void RunProto(ProtoEntryFn entry, nint clientSock)

- static void CheckProtoEntries()

- async void RunRaw(nint clientSock)

- Connection NewConnection(nint sock)

- async void HandleTcp(nint clientSock)

- async void HandleHttp(nint clientSock)

- async void RunUdp(nint sock)


## WorkerMqtt (class)

- static void Install()

- static void Uninstall()

- static async void HandleMqtt(Worker w, nint clientSock)


## WorkerSse (class)

- static SseSubscriberHandler onSseSubscriber;

- static List<SseConnection> conns=new List<SseConnection>();

- static nint sseLock=0;

- static WsSharedBus sharedBus=null;

- static WorkerSseConsumer busConsumer=null;

- static void SseInit()

- static void EnableSharedBus(string busName)

- static void Install()

- static void DisableSharedBus()

- static void Uninstall()

- static void Attach(SseConnection conn)

- static void Detach(SseConnection conn)

- static int ConnCount()

- static int ClusterConnCount()

- static async int BroadcastLocal(string eventName, string data)

- static async int Broadcast(string eventName, string data)

- static async int Broadcast(string data)

- public static async void OnSharedMessage(int targetType, string channel, int targetId, string payload, int payloadLen)

- static async void HandleSse(Worker w, nint clientSock)


## WorkerSseConsumer (class)

- public void OnSharedBusMessage(int targetType, string channel, int targetId, string payload, int payloadLen)


## WorkerWs (class)

- [DllImport("crt")]static extern long strlen(string str);

- static void Install()

- static void DisableSharedBus()

- static void Uninstall()

- static WsSharedBus sharedBus=null;

- static WorkerWsConsumer busConsumer=null;

- static Dict <string, List<Connection>> channels=new Dict <string, List<Connection>>();

- static nint chanLock=0;

- static void ChanInit()

- static void EnableSharedBus(string busName)

- static void Join(string channel, Connection conn)

- static void Leave(string channel, Connection conn)

- static void LeaveAllChannels(Connection conn)

- static Dict <int, WsLink> conns=new Dict <int, WsLink>();

- static nint regLock=0;

- static void RegInit()

- static void Attach(Connection conn, WsWriter writer, TcpClient client)

- static void ConnRemove(Connection conn)

- static WsLink LinkOf(int id)

- static int ConnCount()

- static int ClusterConnCount()

- static Connection ConnById(int id)

- static async int BroadcastLocal(string data)

- static async int Broadcast(string data)

- static async int BroadcastToLocal(string channel, string data)

- static async int BroadcastTo(string channel, string data)

- static async bool PushTo(int connId, string data)

- public static async void OnSharedMessage(int targetType, string channel, int targetId, string payload, int payloadLen)

- static async bool Push(Connection c, string data)

- static async bool PushBinary(Connection c, string data, int len)

- static async bool PushBinary(Connection c, byte[]data, int offset, int len)

- static async bool PushBinary(Connection c, byte[]data)

- static async void HandleWebSocket(Worker w, nint clientSock)


## WorkerWsConsumer (class)

- public void OnSharedBusMessage(int targetType, string channel, int targetId, string payload, int payloadLen)


## WsLink (class)

- Connection conn;

- WsWriter writer;

- TcpClient client;

- bool busy;

- List<AsyncGate> waiters;

- async bool LockAsync()

- void Unlock()


## string (delegate)

`delegate string MessageHandler(Connection conn, string data);`


## string (delegate)

`delegate string DatagramHandler(string data);`


## void (delegate)

`delegate void SseSubscriberHandler(SseConnection conn);`


## void (delegate)

`delegate void ConnectionHandler(Connection conn);`


## void (delegate)

`delegate void ConnectionCloseHandler(Connection conn);`


## void (delegate)

`delegate void RawConnHandler(nint sock);`


## void (delegate)

`delegate void ShutdownHook();`


## void (delegate)

`delegate void ProtoEntryFn(Worker w, nint sock);`


## PingStatus (enum)

- Success

- TimedOut

- DestinationUnreachable

- PacketTooBig

- BadRequest

- Error
