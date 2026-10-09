# System.Net.Http.Proxy

> 源码: `packages/Zan.Net/src/System/Net/Http/Proxy/HttpForwarder.zan`


## FwdChannel (class)

- nint sock;

- TlsStream tls;

- TlsContext ctx;

- bool closed;

- int idleMs;

- FwdChannel(nint sock, TlsStream tls, TlsContext ctx, int idleMs)

- static FwdChannel OfSocket(nint sock, int idleMs)

- static FwdChannel OfTls(nint sock, TlsStream tls, TlsContext ctx, int idleMs)

- async string RecvAsync(int max)

- async int SendAllAsync(string data)

- async int RecvBytesAsync(byte[]data, int max)

- async int SendAllBytesAsync(byte[]data, int offset, int len)

- void Close()

- bool IsClosed()

- void SetIdleMs(int ms)

- void Shutdown()

- nint Sock()


## FwdPool (class)

- List<FwdPooledItem> items;

- int maxIdle;

- int idleMs;

- long hits;

- long misses;

- FwdPool(int maxIdle, int idleMs)

- FwdChannel Take()

- void Give(FwdChannel ch)

- void CloseAll()

- long Hits()

- long Misses()

- int IdleCount()


## FwdPooledItem (class)

- FwdChannel ch;

- long expiry;

- FwdPooledItem(FwdChannel ch, long expiry)


## FwdReader (class)

- FwdChannel ch;

- ByteBuffer buf;

- byte[]recv;

- bool eof;

- int chunkBytes;

- FwdReader(FwdChannel ch, int chunkBytes)

- ~FwdReader()

- async bool FillAsync()

- async string ReadHeadAsync(int cap)

- async string ReadLineAsync(int cap)

- async int ReadSomeBytesAsync(byte[]data, int max)

- int TakeBufferedBytes(byte[]data, int max)

- bool AtEof()

- async int ReadByteAsync()

- bool Drained()


## HttpForwarder (class)

- string listenHost;

- int listenPort;

- string upHost;

- int upPort;

- bool upTls;

- bool verifyTls;

- bool allowConnect;

- int timeoutMs;

- int maxHeadBytes;

- int chunkBytes;

- int maxConnections;

- int active;

- bool running;

- TcpListener listener;

- int recvIdleMs;

- FwdPool pool;

- int maxIdleUp;

- int idleUpMs;

- HttpForwarder(string listenHost, int listenPort, string upstream)

- void ParseUpstream(string upstream)

- static bool IsAuthorityByte(string h)

- static int PortOf(string authority, int at)

- HttpForwarder SetTimeout(int ms)

- HttpForwarder SetChunkBytes(int bytes)

- HttpForwarder SetMaxHeadBytes(int bytes)

- HttpForwarder SetMaxConnections(int max)

- HttpForwarder SetUpstreamKeepAlive(int maxIdle, int idleMs)

- HttpForwarder DisableUpstreamKeepAlive()

- FwdPool UpstreamPool()

- HttpForwarder DisableTlsVerify()

- HttpForwarder DenyConnect()

- bool UpstreamTls()

- void Stop()

- async void Start()

- static async void RejectBusy(nint clientSock)

- async void Serve(nint clientSock)

- async void ServeOnce(nint clientSock)

- async void ServeConnect(FwdChannel client, FwdReader creader)

- async FwdChannel ConnectUpstream()

- string RewriteRequestHead(string head, string peer)

- static bool HasRequestBody(string head)

- static bool RespHasNoBody(string method, int status)

- static bool RespSelfDelimited(string head)

- static bool RespKeepsAlive(string head)

- static string RewriteResponseHead(string head, bool switched)

- static async long PumpBody(FwdReader src, FwdChannel dst, string head, bool isRequest, int chunkBytes)

- static async long PumpExact(FwdReader src, FwdChannel dst, long n, int chunkBytes)

- static async long PumpToEof(FwdReader src, FwdChannel dst, int chunkBytes)

- static async long PumpChunked(FwdReader src, FwdChannel dst, string head, int chunkBytes)

- static async void Tunnel(FwdChannel a, FwdReader ar, FwdChannel b, FwdReader br, int chunkBytes)

- static async void PumpUntilClose(FwdChannel src, FwdChannel dst, int chunkBytes)

- static List<string> HeadLines(string head)

- static string MethodOf(string head)

- static int StatusOf(string head)

- static bool IsResponseHeadValid(string head)

- static string LowerName(string line)

- static string HeaderValueOfLine(string line)

- static string HeaderValue(string head, string name)

- static bool IsTokenByte(int b)

- static bool IsToken(string s)

- static bool IsHeaderLineValid(string line)

- static bool IsTrailerLineValid(string line, string head)

- static bool IsHeadSyntaxValid(string head)

- static bool HasHeaderToken(string head, string field, string token)

- static bool AreConnectionTokensValid(string head)

- static bool IsRequestValid(string head)

- static bool IsHopByHop(string name)

- static bool HeadIsChunked(string head)

- static long ContentLengthOf(string head)

- static bool IsBodyFramingValid(string head, bool isRequest)

- static List<string> SplitCsv(string s)

- static long ParseChunkSize(string line)

- static string Lower(string s)

- static string Trim(string s)

- static bool StartsWith(string s, string prefix)

- static int IndexOf(string hay, string needle, int from)

- static int LastIndexOf(string hay, string needle)
