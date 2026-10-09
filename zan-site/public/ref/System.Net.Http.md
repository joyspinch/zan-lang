# System.Net.Http

> 源码: `packages/Zan.Net/src/System/Net/Http/Http2Frame.zan`, `packages/Zan.Net/src/System/Net/Http/HttpFramer.Tls.zan`, `packages/Zan.Net/src/System/Net/Http/HttpFramer.zan`, `packages/Zan.Net/src/System/Net/Http/HttpRequest.zan`, `packages/Zan.Net/src/System/Net/Http/HttpResponse.zan`, `packages/Zan.Net/src/System/Net/Http/HttpServer.zan`


## Http2Error (class)

- public const int NO_ERROR=0;

- public const int PROTOCOL_ERROR=1;

- public const int INTERNAL_ERROR=2;

- public const int FLOW_CONTROL_ERROR=3;

- public const int SETTINGS_TIMEOUT=4;

- public const int STREAM_CLOSED=5;

- public const int FRAME_SIZE_ERROR=6;

- public const int REFUSED_STREAM=7;

- public const int CANCEL=8;

- public const int COMPRESSION_ERROR=9;

- public const int CONNECT_ERROR=10;

- public const int ENHANCE_YOUR_CALM=11;

- public const int INADEQUATE_SECURITY=12;

- public const int HTTP_1_1_REQUIRED=13;


## Http2Flags (class)

- public const int END_STREAM=1;

- public const int END_HEADERS=4;

- public const int PADDED=8;

- public const int PRIORITY=32;

- public const int ACK=1;


## Http2Frame (class)

- public static const string PREFACE="PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

- public int length;

- public int type;

- public int flags;

- public int streamId;

- public byte[]payload;

- public Http2Frame()

- public static byte[]EncodeHeader(int length, int type, int flags, int streamId)

- public static Http2Frame DecodeHeader(byte[]buf, int offset)

- public static byte[]BuildSettingsAck()

- public static byte[]BuildRstStream(int streamId, int errorCode)

- public static byte[]BuildGoAway(int lastStreamId, int errorCode)

- public static byte[]BuildWindowUpdate(int streamId, int windowSizeIncrement)

- public static byte[]BuildPing(byte[]opaqueData8, bool ack)


## Http2FrameType (class)

- public const int DATA=0;

- public const int HEADERS=1;

- public const int PRIORITY=2;

- public const int RST_STREAM=3;

- public const int SETTINGS=4;

- public const int PUSH_PROMISE=5;

- public const int PING=6;

- public const int GOAWAY=7;

- public const int WINDOW_UPDATE=8;

- public const int CONTINUATION=9;


## Http2Settings (class)

- public const int HEADER_TABLE_SIZE=1;

- public const int ENABLE_PUSH=2;

- public const int MAX_CONCURRENT_STREAMS=3;

- public const int INITIAL_WINDOW_SIZE=4;

- public const int MAX_FRAME_SIZE=5;

- public const int MAX_HEADER_LIST_SIZE=6;


## HttpDeadline (class)

- static List<HttpDeadlineSlot> slots=new List<HttpDeadlineSlot>();

- static List<int> freeSlots=new List<int>();

- static bool sweeping=false;

- static int activeCount=0;

- static long nowMs=0;

- static HttpDeadlineToken Arm(nint sock, int timeoutMs)

- static HttpDeadlineToken Arm(nint sock, int timeoutMs, int totalMs)

- static void Touch(HttpDeadlineToken token, int timeoutMs)

- static bool Expired(HttpDeadlineToken token)

- static void Disarm(HttpDeadlineToken token)

- static bool SweepOnce()

- static async void Sweep()


## HttpDeadlineSlot (class)

- nint sock;

- long deadline;

- int generation;

- bool active;

- bool fired;

- long totalDeadline;

- HttpDeadlineSlot(nint sock, long deadline)


## HttpDeadlineToken (class)

- int slot;

- int generation;

- bool disarmed;

- HttpDeadlineToken(int slot, int generation)

- ~HttpDeadlineToken()


## HttpFramer (class)

- nint sock;

- FramerRecvFn recv;

- string buf;

- int bufCap;

- ByteBuffer pend;

- int scanned;

- bool closed;

- int wireBytes;

- HttpFramer()

- static HttpFramer Create(nint sock)

- void Prime(string bytes, int len)

- void PrimeBytes(byte[]bytes, int offset, int len)

- int Pending()

- bool Closed()

- async int Fill()

- async int FillTo(int maxPending)

- int HeaderEnd()

- async int FillHead(int maxHeaderBytes)

- async int ReadHead(int maxHeaderBytes)

- string Head(int headerLen)

- string Slice(int off, int count)

- async int ReadBody(HttpRequest req, int maxBodyBytes)

- async int ReadResponseBody(HttpResponse response, int headerLen, int maxBodyBytes)

- async int SaveBodyToFile(HttpRequest req, string filePath, int maxBytes)

- async int SaveChunkedBodyToFile(HttpRequest req, string filePath, int maxBytes)

- int RequestBytes()

- void Consume(int count)

- async int ReadChunkedBody(HttpRequest req, HttpResponse response, int he, int maxBodyBytes)

- async int SkipTrailer(int from)

- int IndexOfLf(int from)

- static int HexVal(int c)

- void Dispose()


## HttpFramerTls (class)

- static HttpFramer Create(TlsStream stream)


## HttpRequest (class)

- string method;

- string path;

- string version;

- string body;

- string rawHeaders;

- string queryString;

- string host;

- string contentType;

- string authorization;

- string cookie;

- string userAgent;

- int contentLength;

- bool keepAlive;

- int bodyLength;

- bool chunked;

- int bodyStart;

- int parseStatus;

- HttpRequest()

- static bool IsOws(int c)

- static int TrimOwsStart(string raw, int start, int stop)

- static int TrimOwsStop(string raw, int start, int stop)

- static HttpRequest Parse(string raw)

- static HttpRequest Parse(string raw, int rawLen)

- static bool RegionIs(string raw, int pos, int stop, string name)

- static bool NameIs(string name, string lower)

- static bool RegionIsCi(string raw, int pos, int stop, string name)

- string GetHeader(string name)

- static bool RegionExact(string raw, int pos, int stop, string str)

- string GetQueryParam(string name)

- string GetFormField(string name)

- static string Build(string method, string path, string host, string body)

- static string BuildGet(string path, string host)

- static string BuildPost(string path, string host, string body)

- string UserAgent()

- Dict <string, string> GetQueryParams()


## HttpResponse (class)

- int statusCode;

- string statusText;

- string body;

- byte[]bodyBytes;

- int bodyBytesLen;

- string contentType;

- List<string> headers;

- bool keepAlive;

- int framingLength;

- bool framingHasLength;

- bool framingChunked;

- HttpResponse()

- static HttpResponse Ok(string body)

- static HttpResponse Json(string jsonBody)

- static HttpResponse Text(string text)

- static HttpResponse Bytes(byte[]data, int len, string ct)

- int BodyLength()

- bool IsBinary()

- static bool HasCrlf(string s)

- static bool HeaderFramingSafe(bool isName, string s)

- static bool IsValidHeader(string name, string value)

- static HttpResponse Redirect(string url)

- static HttpResponse NotFound()

- static HttpResponse ServerError(string message)

- static string EscapeHtmlText(string text)

- static HttpResponse WithStatus(int code, string text, string body)

- void Dispose()

- HttpResponse SetHeader(string name, string headerValue)

- string GetHeader(string name)

- HttpResponse SetContentType(string ct)

- HttpResponse SetKeepAlive(bool keep)

- StringBuilder BuildHeadersSb()

- string BuildHeaders()

- string Build()

- static bool IsOws(int c)

- static int TrimOwsStart(string s, int start, int stop)

- static int TrimOwsStop(string s, int start, int stop)

- bool ParseFraming()

- static bool ValidStatusLine(string line)

- static HttpResponse Parse(string raw)


## HttpRoute (class)

- string method;

- string path;

- string response;

- HttpRoute(string method, string path, string response)


## HttpRouter (class)

- List<HttpRoute> routes;

- HttpRouter()

- HttpRouter Get(string path, string responseBody)

- HttpRouter Post(string path, string responseBody)

- HttpRouter Route(string method, string path, string responseBody)

- HttpResponse Match(HttpRequest request)


## HttpServer (class)

- TcpListener listener;

- string host;

- int port;

- bool running;

- int maxConnections;

- int activeConnections;

- int requestTimeout;

- int maxRequestBytes;

- int maxHeaderBytes;

- int workerCount;

- HttpRequestHandler requestHandler;

- HttpServer(string host, int port)

- HttpServer SetMaxConnections(int max)

- HttpServer SetTimeout(int ms)

- HttpServer SetMaxRequestBytes(int bytes)

- HttpServer SetMaxHeaderBytes(int bytes)

- HttpServer Workers(int count)

- static int FindHeaderEnd(string buf)

- HttpServer OnRequest(HttpRequestHandler handler)

- async void Start()

- static async void RejectOverloaded(nint clientSock)

- async void HandleConnection(nint clientSock)

- static async void ServeConnection(nint clientSock, HttpRequestHandler handler, int maxHeaderBytes, int maxRequestBytes, int timeoutMs)

- static async void ServePrimed(nint clientSock, HttpRequestHandler handler, string pending, int maxHeaderBytes, int maxRequestBytes, int timeoutMs)

- static async void ServePrimedBytes(nint clientSock, HttpRequestHandler handler, byte[]pending, int pendingLen, int maxHeaderBytes, int maxRequestBytes, int timeoutMs)

- static async void ServeFramed(nint clientSock, HttpRequestHandler handler, HttpFramer framer, int maxHeaderBytes, int maxRequestBytes, int timeoutMs)

- static async int ServeFramedCore(nint clientSock, HttpRequestHandler handler, HttpFramer framer, int maxHeaderBytes, int maxRequestBytes, int timeoutMs, HttpDeadlineToken watch)

- static async int ServeFramedInner(nint clientSock, HttpRequestHandler handler, HttpFramer framer, int maxHeaderBytes, int maxRequestBytes, int timeoutMs, HttpDeadlineToken watch, TcpClient client)

- void Stop()

- int GetActiveConnections()

- bool IsRunning()


## HttpResponse (delegate)

`delegate HttpResponse HttpRequestHandler(HttpRequest request);`


## int (delegate)

`delegate int FramerRecvFn(string buf, int cap);`
