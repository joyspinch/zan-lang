# System.Net.Sockets

> 源码: `packages/Zan.Net/src/System/Net/Sockets/AsyncSocket.zan`, `packages/Zan.Net/src/System/Net/Sockets/Socket.zan`, `packages/Zan.Net/src/System/Net/Sockets/TcpClient.zan`, `packages/Zan.Net/src/System/Net/Sockets/TcpListener.zan`, `packages/Zan.Net/src/System/Net/Sockets/UdpClient.zan`


## AsyncSocket (class)

- [DllImport("crt")]static extern long strlen(string str);

- static async string Receive(nint sock, int bufSize)

- static async int ReceiveInto(nint sock, string buf, int bufSize)

- static async int Send(nint sock, string data, int len)

- static async int SendString(nint sock, string data)

- static async nint Accept(nint sock)

- static async int Connect(nint sock, string ip, int port)


## Socket (class)

- [DllImport("crt", EntryPoint="zan_io_socket_send")]static extern long ReactorSend(nint sock, string buf, long len, int flags);

- [DllImport("crt", EntryPoint="zan_io_socket_send")]static extern long ReactorSendPtr(nint sock, nint buf, long len, int flags);

- [DllImport("crt", EntryPoint="zan_io_socket_recv")]static extern long ReactorRecv(nint sock, string buf, long len, int flags);

- [DllImport("crt", EntryPoint="zan_io_socket_ready")]static extern int ReactorReady(nint sock, int writeReady);

- [DllImport("crt", EntryPoint="zan_io_connect_status")]static extern int ConnectStatus(nint sock);

- [DllImport("crt", EntryPoint="zan_io_socket_alive")]static extern int SocketAlive(nint sock);

- [DllImport("crt", EntryPoint="zan_io_close_notify")]static extern void ReactorCloseNotify(nint sock);

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

- [DllImport("crt", EntryPoint="__errno")]static extern nint NativeErrnoLocation();

- [DllImport("crt", EntryPoint="__errno_location")]static extern nint NativeErrnoLocation();

- [DllImport("crt", EntryPoint="__error")]static extern nint NativeErrnoLocation();

- [DllImport("crt", EntryPoint="__errno_location")]static extern nint NativeErrnoLocation();

- static int SysClose(nint s)

- static int SysShutdown(nint s, int how)

- static int SysBind(nint s, string addr, int addrlen)

- static int SysListen(nint s, int backlog)

- static nint SysAccept(nint s, string addr, string addrlen)

- static int SysConnect(nint s, string addr, int addrlen)

- static int SysSetSockOpt(nint s, int level, int optname, string optval, int optlen)

- static int SysSendTo(nint s, string buf, int len, int flags, string to, int tolen)

- static int SysRecvFrom(nint s, string buf, int len, int flags, string from, string fromlen)

- static int SysClose(nint s)

- static int SysShutdown(nint s, int how)

- static int SysBind(nint s, string addr, int addrlen)

- static int SysListen(nint s, int backlog)

- static nint SysAccept(nint s, string addr, string addrlen)

- static int SysConnect(nint s, string addr, int addrlen)

- static int SysSetSockOpt(nint s, int level, int optname, string optval, int optlen)

- static int SysSendTo(nint s, string buf, int len, int flags, string to, int tolen)

- static int SysRecvFrom(nint s, string buf, int len, int flags, string from, string fromlen)

- [DllImport("crt")]static extern ushort htons(ushort hostshort);

- static int InetAddr(string cp)

- [DllImport("crt", EntryPoint="zan_io_resolve_ipv4")]static extern int NativeResolveIpv4(string hostname);

- [DllImport("crt", EntryPoint="zan_io_resolve_sa")]static extern int NativeResolveSockAddr(string name, int port, byte[]buf, int cap);

- [DllImport("crt", EntryPoint="zan_io_resolve_all")]static extern int NativeResolveAll(string name, int port, byte[]buf, int cap);

- [DllImport("crt", EntryPoint="zan_io_resolve_all_async")]static extern long NativeResolveAllAsync(nint name, int port, nint buf, int cap);

- [DllImport("crt", EntryPoint="zan_io_sockaddr_family")]static extern int NativeSockAddrFamily(nint sa, int len);

- [DllImport("crt", EntryPoint="zan_io_sockaddr_is_safe")]static extern int NativeSockAddrIsSafe(nint sa, int len, int allowLoopback);

- [DllImport("crt", EntryPoint="zan_io_connect_sa")]static extern long NativeConnectSockAddr(nint sock, nint sa, int len, int timeoutMs);

- [DllImport("crt", EntryPoint="zan_io_sockaddr_ip_str")]static extern string NativeSockAddrIp(byte[]sa);

- [DllImport("crt", EntryPoint="zan_io_sockaddr_ip_str_into")]static extern int NativeSockAddrIpInto(byte[]sa, nint buf, int cap);

- [DllImport("crt", EntryPoint="zan_io_socket_peer_ip_into")]static extern int NativePeerIpInto(nint sock, nint buf, int cap);

- static string IpTextFromNative(nint buf, int n)

- static string SockAddrIpText(byte[]sa)

- [DllImport("crt")]static extern long strlen(string str);

- [DllImport("crt", EntryPoint="zan_monotonic_us")]static extern long NativeMonotonicUs();

- static void Initialize()

- static void Cleanup()

- static nint CreateTcp()

- static nint CreateTcp6()

- static nint CreateUdp()

- static nint CreateUdp6()

- static void Close(nint sock)

- static void CancelPendingIo(nint sock)

- static void ShutdownBoth(nint sock)

- static int SetNonBlocking(nint sock)

- static void SetReuseAddr(nint sock)

- static void SetReusePort(nint sock)

- static void SetNoDelay(nint sock)

- static void SetBroadcast(nint sock)

- static byte[]BuildSockAddr(string ip, int port)

- static bool IsNumericHost(string ip)

- static async byte[]BuildSockAddrAsync(string ip, int port)

- static int ResolveAll(string host, int port, byte[]records)

- static async int ResolveAllAsync(string host, int port, byte[]records)

- static int SockAddrFamily(byte[]record, int len)

- static bool SockAddrIsSafe(byte[]record, int len, bool allowLoopback)

- static bool IsV6Family(int family)

- static async int ConnectSockAddrAsync(nint sock, byte[]record, int len, int timeoutMs)

- static byte[]BuildSockAddrResolved(int ipAddr, int port)

- static int Bind(nint sock, string ip, int port)

- static int Listen(nint sock, int backlog)

- static nint Accept(nint sock)

- static async nint AcceptAsync(nint sock)

- static async nint AcceptAsync(nint sock, CancellationToken cancellation)

- static bool IsTransientAcceptError(int error)

- static int Connect(nint sock, string ip, int port)

- static async int AsyncResolveIp(string ip)

- static async int ConnectAsync(nint sock, string ip, int port)

- static async int ConnectAsync(nint sock, string ip, int port, int timeoutMs)

- static int Send(nint sock, string data, int len)

- static async int SendAsync(nint sock, string data, int len)

- static async int SendAsync(nint sock, byte[]data, int len)

- static async int SendBytesAsync(nint sock, byte[]data, int offset, int len)

- static int SendString(nint sock, string data)

- static async int SendStringAsync(nint sock, string data)

- static int Recv(nint sock, string buf, int bufSize)

- static async int RecvBytesAsync(nint sock, byte[]buf, int maxLen)

- static async string RecvAsync(nint sock, int bufSize)

- static async string RecvAsync(nint sock, int bufSize, int timeoutMs)

- static bool LastRecvTimedOut()

- static bool recvTimedOut=false;

- static void MarkRecvTimeout(bool timedOut)

- static async int RecvIntoAsync(nint sock, string buf, int bufSize)

- static string Receive(nint sock, int bufSize)

- static int SendTo(nint sock, string data, int len, string ip, int port)

- static async int SendToAsync(nint sock, string data, int len, string ip, int port)

- static string RecvFrom(nint sock, int bufSize)

- static int RecvFromInto(nint sock, byte[]buf, int bufSize)

- static async string RecvFromAsync(nint sock, int bufSize)

- static string lastPeerIp="";

- static int lastPeerPort=0;

- static string PeerIp()

- static int PeerPort()

- static string RecvFromPeer(nint sock, int bufSize)

- static async string RecvFromPeerAsync(nint sock, int bufSize)

- [DllImport("crt", EntryPoint="memcpy")]static extern nint PlatMemCopyIn(byte[]dst, nint src, long n);

- static int ResolveIpv4(string hostname)

- static async int ResolveAsync(string hostname)

- static string Resolve(string hostname)

- static string RemoteIp(nint sock)

- static int LastSocketError()

- static int lastError;

- static bool resolveFailed;

- static void CaptureLastError()

- static bool LastResolveFailed()

- static void MarkResolveFailed(bool failed)

- static long Ipv4ToLong(string ip)

- static string LongToIpv4(long ipValue)

- static bool IsOpen(nint sock)

- static bool IsReadable(nint sock)

- static bool IsWritable(nint sock)


## SocketAcceptCancellation (class)

- private object sync;

- private nint socket;

- private CancellationToken cancellation;

- private bool active;

- SocketAcceptCancellation(nint socket, CancellationToken cancellation)

- void Check()

- void Finish()


## TcpClient (class)

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

- static TcpClient FromSocket(nint sock)

- static async TcpClient ConnectAsync(string host, int port)

- static async TcpClient ConnectAsync(string host, int port, int timeoutMs)

- static async TcpClient ConnectAsync(string host, int port, int timeoutMs, bool allowLoopback)

- static bool LastConnectTimedOut()

- static bool connectTimedOut=false;

- static bool connectResolveFailed=false;

- static void MarkConnectTimeout(bool timedOut)

- static bool LastConnectResolveFailed()

- static void MarkConnectResolveFailed(bool failed)

- static TcpClient Connect(string host, int port)

- void EnsureNonBlocking()

- async int SendAsync(string data)

- async int SendBytesAsync(string data, int len)

- async int SendBytesAsync(byte[]data, int len)

- async int SendBytesAsync(byte[]data, int offset, int len)

- async string RecvAsync(int bufSize)

- async string RecvAsync(int bufSize, int timeoutMs)

- async string RecvAsync()

- async string RecvReuseAsync()

- static List<string> recvPool=new List<string>();

- static int recvPoolSize=65536;

- static int recvPoolMax=1024;

- static string AllocBuffer(int size)

- static string RentRecvBuffer(int size)

- static void ReturnRecvBuffer(string buf, int size)

- int Send(string data)

- string Recv(int bufSize)

- void Close()

- void Dispose()

- bool IsConnected()

- nint GetSocket()

- void SetRecvBufferSize(int size)


## TcpListener (class)

- nint sock;

- string host;

- int port;

- bool running;

- int backlog;

- private object sync;

- private bool stopped;

- private int activeAccepts;

- private CancellationTokenSource acceptCancellation;

- TcpListener(string host, int port)

- static TcpListener CreateAndStart(string host, int port, int backlog)

- void Start()

- void StartReusePort()

- private void StartOwned(bool reusePort)

- private nint BeginAccept()

- private void EndAccept()

- private nint CompleteAccept(nint client, bool nonBlocking)

- async nint AcceptAsync()

- async TcpClient AcceptTcpClientAsync()

- nint Accept()

- void EnsureStarted()

- void Stop()

- private void FinishStopOwned()

- private int PendingAccepts()

- async void StopAsync()

- bool IsRunning()

- nint GetSocket()

- int GetPort()

- string GetHost()


## UdpClient (class)

- nint sock;

- string host;

- int port;

- bool bound;

- [DllImport("crt")]static extern long strlen(string str);

- UdpClient()

- static UdpClient Bind(string host, int port)

- int SendTo(string data, string ip, int port)

- async int SendToAsync(string data, string ip, int port)

- int SendBytesTo(string data, int len, string ip, int port)

- int SendBytesTo(byte[]data, int len, string ip, int port)

- async int SendBytesToAsync(string data, int len, string ip, int port)

- async int SendBytesToAsync(byte[]data, int len, string ip, int port)

- string RecvFrom(int bufSize)

- int RecvBytesFrom(byte[]buf, int bufSize)

- async string RecvFromAsync(int bufSize)

- async string RecvFromAsync()

- void Close()

- nint GetSocket()

- bool IsBound()

- void EnableBroadcast()
