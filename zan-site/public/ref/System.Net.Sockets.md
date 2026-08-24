# System.Net.Sockets

> 源码: `stdlib/System/Net/Sockets/AsyncSocket.zan`, `stdlib/System/Net/Sockets/Socket.zan`, `stdlib/System/Net/Sockets/TcpClient.zan`, `stdlib/System/Net/Sockets/TcpListener.zan`, `stdlib/System/Net/Sockets/UdpClient.zan`


## AsyncSocket (class)

由 reactor 驱动的异步套接字操作。

与旧版 <c>Socket.*Async</c> 辅助方法不同——它们在
<c>select()</c> 上忙等并阻塞调用线程——这些方法真正
通过 <c>await Socket.ReadReady</c> /
<c>await Socket.WriteReady</c> 挂起到无栈 IO reactor，且只在 fd 就绪后才操作它，
因此多个协程可在单线程上并行推进。

套接字应为非阻塞（<c>Socket.SetNonBlocking</c>）。
就绪唤醒后系统调用即可无阻塞完成；虚假唤醒或
暂时性 would-block（< 0）只会重新注册监视器并再次等待。

- [DllImport("crt")]static extern long strlen(string str);

- static async string Receive(nint sock, int bufSize)
  - 挂起直到 <paramref name="sock"/> 可读，然后
    接收最多 <paramref name="bufSize"/> 字节。数据以
    NUL 结尾字符串返回（对端关闭 / EOF 时为空字符串）。

- static async int ReceiveInto(nint sock, string buf, int bufSize)
  - 挂起直到可读，然后接收最多
    <paramref name="bufSize"/> 字节到调用方拥有的缓冲区，返回
    字节数（对端关闭时为 0）。无每次调用分配或拷贝——
    长连接跨消息复用同一缓冲区，而非
    `Receive` 每次调用都做的缓冲区 + <c>ToStr</c>。

- static async int Send(nint sock, string data, int len)
  - 挂起直到可写，然后发送；遇到部分写入
    会循环（重新等待），直到 <paramref name="len"/> 字节全部发送完或
    对端关闭。返回已发送字节数。二进制安全：未发送
    的剩余部分通过下标拷贝携带，而非 <c>data + total</c> 指针
    运算（后者会被编译为拼接并在第一个
    NUL 字节处截断）。

- static async int SendString(nint sock, string data)
  - 发送整个字符串（长度由 strlen 得出）。

- static async nint Accept(nint sock)
  - 挂起直到有入站连接待处理，然后接受
    连接。返回已接受的客户端 fd。

- static async int Connect(nint sock, string ip, int port)
  - 发起非阻塞连接，然后挂起直到连接确定。
    成功返回 0，失败返回非零 SO_ERROR 错误码
    （连接被拒绝/不可达/超时）。连接进行中时套接字
    同样会变为可写，仅凭 WriteReady 无法区分成败——
    必须探测 SO_ERROR，否则连接失败会被误报为成功。


## Socket (class)

跨平台的低层套接字操作。
Windows 使用 Winsock2，Linux/macOS 使用 BSD 套接字（libc）。

- [DllImport("crt", EntryPoint="zan_io_socket_send")]static extern long ReactorSend(nint sock, string buf, long len, int flags);

- [DllImport("crt", EntryPoint="zan_io_socket_recv")]static extern long ReactorRecv(nint sock, string buf, long len, int flags);

- [DllImport("crt", EntryPoint="zan_io_socket_ready")]static extern int ReactorReady(nint sock, int writeReady);

- [DllImport("crt", EntryPoint="zan_io_connect_status")]static extern int ConnectStatus(nint sock);
  - 探测非阻塞连接的结果：0 表示已连接，
    失败时为正的 SO_ERROR 码（或 -1），连接进行中为 -2。

- [DllImport("crt", EntryPoint="zan_io_socket_alive")]static extern int SocketAlive(nint sock);

- [DllImport("crt", EntryPoint="zan_io_socket_peer_ip")]static extern string NativePeerIp(nint sock);

- [DllImport("ws2_32", EntryPoint="WSAStartup")]static extern int WSAStartup(ushort version, string wsaData);

- [DllImport("crt", EntryPoint="zan_io_socket_cleanup")]static extern void ReactorCleanup();

- [DllImport("ws2_32")]static extern nint socket(int af, int type, int protocol);

- [DllImport("ws2_32")]static extern int closesocket(nint s);

- [DllImport("ws2_32", EntryPoint="send")]static extern int winsend(nint s, string buf, int len, int flags);

- [DllImport("ws2_32", EntryPoint="recv")]static extern int winrecv(nint s, string buf, int len, int flags);

- [DllImport("ws2_32")]static extern int bind(nint s, string addr, int addrlen);

- [DllImport("ws2_32")]static extern int listen(nint s, int backlog);

- [DllImport("ws2_32")]static extern nint accept(nint s, string addr, string addrlen);

- [DllImport("ws2_32")]static extern int connect(nint s, string addr, int addrlen);

- [DllImport("ws2_32")]static extern int setsockopt(nint s, int level, int optname, string optval, int optlen);

- [DllImport("ws2_32")]static extern int ioctlsocket(nint s, int cmd, string argp);

- [DllImport("ws2_32")]static extern int shutdown(nint s, int how);

- [DllImport("kernel32", EntryPoint="CancelIoEx")]static extern int CancelIoEx(nint handle, nint overlapped);

- [DllImport("ws2_32")]static extern int sendto(nint s, string buf, int len, int flags, string to, int tolen);

- [DllImport("ws2_32")]static extern int recvfrom(nint s, string buf, int len, int flags, string from, string fromlen);

- [DllImport("ws2_32")]static extern ushort htons(ushort hostshort);

- [DllImport("ws2_32")]static extern int htonl(int hostlong);

- [DllImport("ws2_32")]static extern ushort ntohs(ushort netshort);

- [DllImport("ws2_32")]static extern int inet_addr(string cp);

- [DllImport("ws2_32", EntryPoint="WSAGetLastError")]static extern int WinLastError();

- static int SysClose(nint s)

- static int SysShutdown(nint s, int how)

- static int SysBind(nint s, string addr, int addrlen)

- static int SysListen(nint s, int backlog)

- static nint SysAccept(nint s, string addr, string addrlen)

- static int SysConnect(nint s, string addr, int addrlen)

- static int SysSetSockOpt(nint s, int level, int optname, string optval, int optlen)

- static int SysSendTo(nint s, string buf, int len, int flags, string to, int tolen)

- static int SysRecvFrom(nint s, string buf, int len, int flags, string from, string fromlen)

- [DllImport("crt")]static extern int socket(int af, int type, int protocol);

- [DllImport("crt", EntryPoint="close")]static extern int closesocket(int fd);

- [DllImport("crt", EntryPoint="send")]static extern int winsend(int fd, string buf, int len, int flags);

- [DllImport("crt", EntryPoint="recv")]static extern int winrecv(int fd, string buf, int len, int flags);

- [DllImport("crt")]static extern int bind(int fd, string addr, int addrlen);

- [DllImport("crt")]static extern int listen(int fd, int backlog);

- [DllImport("crt")]static extern int accept(int fd, string addr, string addrlen);

- [DllImport("crt")]static extern int connect(int fd, string addr, int addrlen);

- [DllImport("crt")]static extern int setsockopt(int fd, int level, int optname, string optval, int optlen);

- [DllImport("crt")]static extern int shutdown(int fd, int how);

- [DllImport("crt")]static extern int sendto(int fd, string buf, int len, int flags, string to, int tolen);

- [DllImport("crt")]static extern int recvfrom(int fd, string buf, int len, int flags, string from, string fromlen);

- [DllImport("crt")]static extern ushort htons(ushort hostshort);

- [DllImport("crt")]static extern int htonl(int hostlong);

- [DllImport("crt")]static extern ushort ntohs(ushort netshort);

- [DllImport("crt")]static extern int inet_addr(string cp);

- [DllImport("crt")]static extern int fcntl(int fd, int cmd, int arg);

- [DllImport("crt", EntryPoint="__errno_location")]static extern nint NativeErrnoLocation();

- [DllImport("crt", EntryPoint="__error")]static extern nint NativeErrnoLocation();

- static int SysClose(nint s)

- static int SysShutdown(nint s, int how)

- static int SysBind(nint s, string addr, int addrlen)

- static int SysListen(nint s, int backlog)

- static nint SysAccept(nint s, string addr, string addrlen)

- static int SysConnect(nint s, string addr, int addrlen)

- static int SysSetSockOpt(nint s, int level, int optname, string optval, int optlen)

- static int SysSendTo(nint s, string buf, int len, int flags, string to, int tolen)

- static int SysRecvFrom(nint s, string buf, int len, int flags, string from, string fromlen)

- [DllImport("crt", EntryPoint="zan_io_resolve_ipv4")]static extern int NativeResolveIpv4(string hostname);

- [DllImport("crt", EntryPoint="zan_io_resolve_sa")]static extern int NativeResolveSockAddr(string name, int port, byte[]buf, int cap);

- [DllImport("crt", EntryPoint="zan_io_sockaddr_ip_str")]static extern string NativeSockAddrIp(byte[]sa);

- [DllImport("crt")]static extern long strlen(string str);

- [DllImport("crt", EntryPoint="zan_monotonic_us")]static extern long NativeMonotonicUs();

- static void Initialize()
  - 初始化套接字子系统（Windows 为 WSAStartup，Linux 无操作）。

- static void Cleanup()
  - 清理套接字子系统。

- static nint CreateTcp()
  - 创建 TCP 套接字。

- static nint CreateTcp6()
  - 创建 IPv6 TCP 套接字（AF_INET6）。IPv6 连接
    也可用 IPv4 套接字配 ::ffff: 映射地址，但原生 v6 套接字
    才能直接绑定/监听 [::1] 这类纯 v6 地址。

- static nint CreateUdp()
  - 创建 UDP 套接字。

- static nint CreateUdp6()
  - 创建 IPv6 UDP 套接字（AF_INET6）。

- static void Close(nint sock)
  - 关闭套接字。

- static void ShutdownBoth(nint sock)
  - 双向关闭连接而不释放
    描述符。IO reactor 中挂起在此套接字上的协程
    会以 0 字节读（对端关闭语义）被唤醒并正常退出；
    而直接关闭描述符不会这样：fd 会
    带着未完成事件离开 reactor 集合，协程将永远
    无法恢复。用于挂断超限的连接。

- static int SetNonBlocking(nint sock)
  - 将套接字设为非阻塞模式。

- static void SetReuseAddr(nint sock)
  - 在套接字上设置 SO_REUSEADDR。

- static void SetReusePort(nint sock)
  - 启用 SO_REUSEPORT，使多个进程能绑定同一
    address:port，由内核在它们之间负载均衡入站连接——
    这是多 worker 水平扩展的基础（Linux/macOS）。必须
    在 bind() 之前设置。Windows 无 SO_REUSEPORT，故设置 SO_REUSEADDR
    替代（那里仍是单进程独占端口）。

- static void SetNoDelay(nint sock)
  - 在接受连接上禁用 Nagle 算法（TCP_NODELAY）。
    否则小回复要等对端对前一条的 ACK，
    管线化请求的客户端会撞上约 40ms 的延迟 ACK 停顿：
    websocket echo 路径仅因此一项就测得 1.8 万条消息/秒。
    所有支持的平台上 IPPROTO_TCP 为 6、TCP_NODELAY 为 1。

- static void SetBroadcast(nint sock)
  - 在套接字上启用 SO_BROADCAST（使用各平台正确的常量）。

- static byte[]BuildSockAddr(string ip, int port)
  - 把主机名或 IP 文本（IPv4/IPv6）解析为完整的
    sockaddr 字节（IPv4 16 字节 / IPv6 28 字节），供
    connect/bind/sendto 使用。字面量 IP 和主机名统一由
    getaddrinfo 处理（C 端按返回地址族填充 sockaddr，布局
    不进 stdlib）。"0.0.0.0"/空串仍走零地址快路径；解析失败
    时回退 0.0.0.0（旧行为，随后的 connect/bind 会报错）。
    返回的数组长度即 sockaddr 实际长度。

- static bool IsNumericHost(string ip)
  - 该地址是否为数值字面量（IPv4 点分十进制或含冒号的
    IPv6），即无需 DNS 解析即可直接构建 sockaddr。

- static async byte[]BuildSockAddrAsync(string ip, int port)
  - BuildSockAddr 的异步版本：主机名通过
    `ResolveSockAddr` 在后台线程解析（不会阻塞
    事件循环），字面量 IP 走同步快路径。供 ConnectAsync /
    SendToAsync 使用。

- static byte[]BuildSockAddrResolved(int ipAddr, int port)
  - 从已解析的 IPv4 整数地址构建 sockaddr_in（字节序与
    <c>inet_addr</c> 一致）。供异步连接复用——解析已在
    ConnectAsync 中通过异步 DNS 完成，这里不再执行阻塞解析。

- static int Bind(nint sock, string ip, int port)
  - 将套接字绑定到地址和端口。

- static int Listen(nint sock, int backlog)
  - 开始监听连接。

- static nint Accept(nint sock)
  - 同步接受连接。

- static async nint AcceptAsync(nint sock)
  - 在 IO reactor 上挂起，直到有入站连接
    待处理，然后接受。要求监听套接字为非阻塞。

- static int Connect(nint sock, string ip, int port)
  - 同步连接到远程地址。

- static async int AsyncResolveIp(string ip)
  - 发起非阻塞连接，然后在 IO reactor 上挂起
    直到连接确定。成功返回 0，失败返回非零错误
    码（SO_ERROR，如拒绝/不可达；无错误码时为 -1）
    。要求套接字为非阻塞。
    
    reactor 的可写就绪唤醒可能早于挂起的连接
    确定（尤其是 Windows，零字节发送探测会提前完成，
    连接失败通过异常集合报告），因此
    每次唤醒后用 ConnectStatus 检查结果，仍在进行中时
    稍作延迟再轮询。
    把 IP 文本解析为 sockaddr 用的 IPv4 整数地址
    （0.0.0.0/空串 → 0）。字面量 IP 直接用 inet_addr；
    主机名通过异步 DNS 解析（`ResolveAsync`），
    在后台线程执行，不会阻塞事件循环。

- static async int ConnectAsync(nint sock, string ip, int port)

- static async int ConnectAsync(nint sock, string ip, int port, int timeoutMs)
  - 受 <paramref name="timeoutMs"/> 限制的 ConnectAsync：在时限内
    连接既未成功也未失败时返回 -3
    （黑洞地址永远不唤醒 reactor，因此无超时重载
    会永远挂起）。非正超时表示"无
    截止时间"，转而使用 reactor 驱动的重载。
    
    连接挂起期间用 ConnectStatus 轮询结果，
    并递增延迟，而非等待可写就绪：reactor 唤醒
    无法取消，因此截止时间只能由本协程拥有的定时器
    观察。套接字留给调用方关闭。

- static int Send(nint sock, string data, int len)
  - 同步发送数据。

- static async int SendAsync(nint sock, string data, int len)
  - 挂起直到可写，然后发送；部分写入时重新等待，
    直到 <paramref name="len"/> 字节全部发送或对端关闭。
    返回已发送字节数。要求套接字为非阻塞。
    
    二进制安全：未发送的剩余部分放在独立缓冲区中重建，
    用下标拷贝而非 <c>data + totalSent</c> 指针运算。在
    字符串上这会变成拼接，并在第一个 NUL
    字节处截断，损坏任何含 NUL 的负载（如 MQTT 包）。
    
    急切发送：先尝试非阻塞发送，仅在套接字会阻塞时
    才等待 reactor（部分写入 / 发送缓冲区已满）
    。不拥塞的套接字上，整个负载一次系统调用即发出
    且无需挂起，省去每条消息一次 reactor 往返。

- static int SendString(nint sock, string data)
  - 发送字符串（自动计算长度）。

- static async int SendStringAsync(nint sock, string data)
  - 异步发送整串字符串（长度由 strlen 计算）。

- static int Recv(nint sock, string buf, int bufSize)
  - 同步接收数据。

- static async string RecvAsync(nint sock, int bufSize)
  - 在 IO reactor 上挂起直到可读，然后最多接收
    <paramref name="bufSize"/> 字节。返回以 NUL 结尾的
    字符串（对端关闭/EOF 时为空）。需要非阻塞套接字。

- static async string RecvAsync(nint sock, int bufSize, int timeoutMs)
  - 受 <paramref name="timeoutMs"/> 限制的 RecvAsync：时限内
    没有数据到达时返回空串（用 LastRecvTimedOut 与对端关闭区分）。
    非正超时表示"不设截止时间"，转而使用 reactor 驱动的重载。
    
    用零超时的 select() 轮询可读性，而不是等 reactor 唤醒：唤醒
    无法取消，所以对一个接受了连接却永不回话的对端，不带截止
    时间的接收会永远挂起。

- static bool LastRecvTimedOut()
  - 本协程最近一次带超时的接收是因超时放弃、而非对端
    关闭时为 true。通过同步辅助函数写入，因为在 async 方法内给
    静态变量赋值会被按值捕获。

- static bool recvTimedOut=false;

- static void MarkRecvTimeout(bool timedOut)

- static async int RecvIntoAsync(nint sock, string buf, int bufSize)
  - 挂起直到可读，然后最多接收 <paramref
    name="bufSize"/> 字节到调用方提供的缓冲区，在接收
    字节数处补 NUL。返回读取的字节数（对端关闭时为 0）。让
    keep-alive 连接在多个请求间复用同一个缓冲区，而不是
    每个请求都重新分配并清零一块（最大 64KB）缓冲区。

- static string Receive(nint sock, int bufSize)
  - 接收数据到缓冲区（返回字符串）。

- static int SendTo(nint sock, string data, int len, string ip, int port)
  - 发送一个 UDP 数据报（同步解析地址，含 IPv6）。

- static async int SendToAsync(nint sock, string data, int len, string ip, int port)
  - 挂起直到可写，然后发送一个 UDP 数据报。需要
    非阻塞套接字。主机名通过异步 DNS 解析（<see
    cref="BuildSockAddrAsync"/>），不会阻塞事件循环。

- static string RecvFrom(nint sock, int bufSize)
  - 接收一个 UDP 数据报。

- static int RecvFromInto(nint sock, byte[]buf, int bufSize)
  - 接收 UDP 数据报到调用方提供的缓冲区，返回
    字节数（出错/无数据时为 0）。用于二进制数据报：
    字符串版本用 strlen 计算长度，会在第一个 NUL
    字节处截断。

- static async string RecvFromAsync(nint sock, int bufSize)
  - 在 IO reactor 上挂起直到有数据报可用，然后
    接收数据报。需要非阻塞套接字。

- static string lastPeerIp="";

- static int lastPeerPort=0;

- static string PeerIp()
  - 最近一次通过 RecvFromPeer / RecvFromPeerAsync
    收到数据报的发送方 IPv4 地址。

- static int PeerPort()
  - 最近一次通过 RecvFromPeer / RecvFromPeerAsync
    收到数据报的发送方 UDP 端口。

- static string RecvFromPeer(nint sock, int bufSize)
  - 接收一个 UDP 数据报并记录发送方地址，以便
    服务器回信（见 PeerIp/PeerPort）。返回负载（出错/关闭时
    为空 ""）。

- static async string RecvFromPeerAsync(nint sock, int bufSize)
  - 挂起直到数据报到达，接收并记录
    发送方地址（见 PeerIp/PeerPort）。需要非阻塞套接字。

- [DllImport("crt", EntryPoint="memcpy")]static extern nint PlatMemCopyIn(byte[]dst, nint src, long n);

- static int ResolveIpv4(string hostname)
  - 将主机名解析为 IPv4 地址，字节序与
    <c>inet_addr</c> 一致（第一段在低字节），无法
    解析时返回 0。通过运行时 C shim 调用 gethostbyname/AF_INET，
    避免在 Zan 代码中硬编码 hostent 布局。

- static async int ResolveAsync(string hostname)
  - 异步将主机名解析为 IPv4 地址（返回值与
    `ResolveIpv4` 相同：0 表示失败）。解析在
    后台线程执行，协程在 IO reactor 上挂起直到完成，不会
    阻塞事件循环——ConnectAsync 用它替代同步解析。

- static string Resolve(string hostname)
  - 将主机名解析为 IP 地址。若无法解析则返回 ""；
    否则返回原主机名。

- static string RemoteIp(nint sock)

- static int LastSocketError()
  - 最近一次失败的套接字系统调用的平台错误码：
    Windows 的 WSAGetLastError，POSIX 的 errno。供失败路径
    构造诊断信息——之前 CreateTcp/Bind/Listen 的失败
    被静默吞掉，调用方只知道操作失败了。

- static int lastError;

- static bool resolveFailed;

- static void CaptureLastError()

- static bool LastResolveFailed()

- static void MarkResolveFailed(bool failed)

- static long Ipv4ToLong(string ip)

- static string LongToIpv4(long ipValue)

- static bool IsOpen(nint sock)
  - 已被关闭（例如监听器被 Stop() 关闭）。

- static bool IsReadable(nint sock)
  - 用 select() 以零超时检查套接字是否可读。

- static bool IsWritable(nint sock)
  - 用 select() 以零超时检查套接字是否可写。


## TcpClient (class)

基于 Socket 层实现的 TCP 客户端，提供发送/接收操作。

- nint sock;

- string host;

- int port;

- bool connected;

- bool nonBlocking;

- int recvBufSize;

- string recvBuf;

- [DllImport("crt")]static extern long strlen(string str);

- TcpClient()

- nint Sock()
  - 返回底层套接字句柄（例如用于在
    TlsStream 中包装连接）。

- static TcpClient FromSocket(nint sock)
  - 从已连接的套接字创建 TcpClient。

- static async TcpClient ConnectAsync(string host, int port)
  - 连接远程主机，在 IO reactor 上挂起直到
    连接建立（非阻塞 connect）。连接失败时返回 null
    （例如被拒绝或不可达）。

- static async TcpClient ConnectAsync(string host, int port, int timeoutMs)
  - 带 <paramref name="timeoutMs"/> 截止时间的 ConnectAsync：
    连接失败，或在截止时间到来时仍处于等待状态，则返回
    null（用 LastConnectTimedOut 区分两种情况）。非正超时
    表示"不设截止时间"。

- static bool LastConnectTimedOut()
  - 本线程最近一次 ConnectAsync 是因超时放弃
    而非被拒绝时为 true。通过同步辅助函数写入，
    因为在 async 方法内给静态变量赋值会被按值捕获，
    更新永远到不了调用方。

- static bool connectTimedOut=false;

- static bool connectResolveFailed=false;

- static void MarkConnectTimeout(bool timedOut)

- static bool LastConnectResolveFailed()

- static void MarkConnectResolveFailed(bool failed)

- static TcpClient Connect(string host, int port)
  - 同步连接远程主机。

- void EnsureNonBlocking()
  - 把套接字切到非阻塞模式（幂等）。异步操作把描述符
    交给 IO reactor，阻塞描述符会让整个调度线程停在一次
    recv/send 上；同步 Connect 与 FromSocket 交出的套接字
    未必已经是非阻塞的，因此在每个异步入口处保证一次，而不是
    依赖调用方的构造路径。同步 Send/Recv 不经过这里，仍是
    阻塞语义。

- async int SendAsync(string data)
  - 发送数据，必要时在 IO reactor 上挂起。

- async int SendBytesAsync(string data, int len)
  - 发送原始字节，必要时在 IO reactor 上挂起。

- async string RecvAsync(int bufSize)
  - 接收数据，在 IO reactor 上挂起直到可读。

- async string RecvAsync(int bufSize, int timeoutMs)
  - 带 <paramref name="timeoutMs"/> 截止时间的接收：时限内
    没有数据到达时返回空串（用 Socket.LastRecvTimedOut 与对端关闭
    区分）。非正超时表示"不设截止时间"。

- async string RecvAsync()
  - 使用默认缓冲区大小接收。

- async string RecvReuseAsync()
  - 接收数据到本连接的复用缓冲区，在
    IO reactor 上挂起直到可读。缓冲区每条连接只分配一次，
    在 keep-alive 请求间复用，因此没有每次请求的
    分配/清零开销。返回以 NUL 结尾的接收字节
    字符串（对端关闭时为空）。返回的字符串指向共享缓冲区，
    仅在该客户端下一次接收前有效。

- static List<string> recvPool=new List<string>();

- static int recvPoolSize=65536;

- static int recvPoolMax=256;

- static string RentRecvBuffer(int size)
  - 借一个至少 <paramref name="size"/> 字节的接收缓冲区。
    只有默认大小的缓冲区进池：池里的每一项都是本方法按
    recvPoolSize 构建的，所以借出的容量总是已知且足够。

- static void ReturnRecvBuffer(string buf, int size)
  - 把缓冲区还回池中。调用方在归还后不得再读取它
    （RecvReuseAsync 的返回值本来就只在下一次接收前有效）。

- int Send(string data)
  - 同步发送数据。

- string Recv(int bufSize)
  - 同步接收数据。

- void Close()
  - 关闭连接。

- bool IsConnected()
  - 已连接时返回 true。

- nint GetSocket()
  - 获取底层套接字句柄。

- void SetRecvBufferSize(int size)
  - 设置接收缓冲区大小。


## TcpListener (class)

跨平台的异步 TCP 服务器监听器。
借鉴 Workerman 的思路：每个被接受的连接都可以
在独立的协程中处理，从而支持海量并发。

用法：
var listener = new TcpListener("0.0.0.0", 8080);
TcpListener.Start(listener);
while (true) {
nint client = await TcpListener.AcceptAsync(listener);
// 派生一个协程来处理该客户端
}

- nint sock;

- string host;

- int port;

- bool running;

- int backlog;

- TcpListener(string host, int port)

- static TcpListener CreateAndStart(string host, int port, int backlog)
  - 创建并启动一个带自定义 backlog 的监听器。

- void Start()
  - 开始监听传入的连接。任一步（创建套接字、绑定、
    监听、设为非阻塞）失败时关闭已创建的 fd 并抛
    SocketException（端口被占用等），绝不留下一个
    running=true 但实际没在监听的监听器。

- void StartReusePort()
  - 在 bind 前启用 SO_REUSEPORT 再开始监听，这样
    多个 worker 进程可共享同一地址:端口，由内核
    在它们之间负载均衡入站连接（Linux/macOS）。Windows 上行为同
    Start（无 SO_REUSEPORT）。任一步失败时关闭 fd 并抛
    SocketException。

- async nint AcceptAsync()
  - 在 IO reactor 上挂起直到客户端连接，然后接受
    连接。接受的套接字被设为非阻塞，因此其上的异步 recv/send 也会
    在 reactor 上挂起。

- nint Accept()
  - 同步接受客户端连接。

- void EnsureStarted()
  - 接受连接前的自愈：构造器只记录 host:port，真正的
    socket/bind/listen 在 `Start` 里做，所以直接对
    `new TcpListener(...)` 调用 Accept 曾经在一个 -1 的 fd 上永远
    挂起。这里补上一次 Start（失败照常抛 SocketException）。

- void Stop()
  - 停止监听器。

- bool IsRunning()
  - 返回监听器是否正在运行。

- nint GetSocket()
  - 返回底层套接字句柄。

- int GetPort()
  - 返回绑定的端口。

- string GetHost()
  - 返回绑定的主机。


## UdpClient (class)

跨平台的异步 UDP 客户端/服务器。
UDP 是无连接的：一个套接字可以向多个对端发送并从多个对端接收，
并与协程调度器集成。

用法（服务器）：
var udp = UdpClient.Bind("0.0.0.0", 9999);
while (true) {
string data = await udp.RecvFromAsync(4096);
// 处理数据
}

用法（客户端）：
var udp = new UdpClient();
await udp.SendToAsync("Hello", "127.0.0.1", 9999);

- nint sock;

- string host;

- int port;

- bool bound;

- [DllImport("crt")]static extern long strlen(string str);

- UdpClient()

- static UdpClient Bind(string host, int port)
  - 创建绑定到本地地址和端口的 UDP 套接字。

- int SendTo(string data, string ip, int port)
  - 向指定地址发送数据报。

- async int SendToAsync(string data, string ip, int port)
  - 发送数据报，在 IO reactor 上挂起直到可写。

- int SendBytesTo(string data, int len, string ip, int port)
  - 发送原始字节。

- async int SendBytesToAsync(string data, int len, string ip, int port)
  - 以显式长度发送原始字节，在 IO
    reactor 上挂起直到可写。用于二进制数据报：字符串
    重载用 strlen 计算长度，会在第一个 NUL 处停止。

- string RecvFrom(int bufSize)
  - 同步接收数据报。

- int RecvBytesFrom(byte[]buf, int bufSize)
  - 接收数据报到调用方提供的缓冲区，返回
    字节数（无数据时为 0）。用于二进制数据报：字符串
    版本用 strlen 计算长度，会在第一个 NUL 处停止。

- async string RecvFromAsync(int bufSize)
  - 接收数据报，在 IO reactor 上挂起直到有数据报
    到达。

- async string RecvFromAsync()
  - 使用默认缓冲区大小接收。

- void Close()
  - 关闭 UDP 套接字。

- nint GetSocket()
  - 返回底层套接字句柄。

- bool IsBound()
  - 返回该套接字是否已绑定。

- void EnableBroadcast()
  - 在套接字上启用广播。
