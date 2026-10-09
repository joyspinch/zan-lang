# System.Net.Http.Client

> 源码: `packages/Zan.Net/src/System/Net/Http/Client/CookieJar.zan`, `packages/Zan.Net/src/System/Net/Http/Client/HttpClient.zan`, `packages/Zan.Net/src/System/Net/Http/Client/SseSink.zan`


## Cookie (class)

- string name;

- string cookieValue;

- string domain;

- string path;

- bool hostOnly;

- bool secure;

- long expires;

- Cookie(string name, string cookieValue)

- string Name()

- string Value()

- string Domain()

- string Path()

- bool Secure()

- long Expires()


## CookieJar (class)

- List<Cookie> items;

- CookieJar()

- int Count()

- string Get(string name)

- bool Remove(string name)

- void Clear()

- void SetCookie(string host, string requestPath, string setCookieValue)

- string HeaderValue(string host, string requestPath, bool secureChannel)

- void AbsorbHead(string host, string requestPath, string head)

- void DropExpired()

- static bool DomainMatches(string host, string domain, bool hostOnly)

- static bool DomainAttributeOk(string host, string domain)

- static bool IsIpHost(string host)

- static bool PathMatches(string path, string cookiePath)

- static string DefaultPath(string requestPath)

- static string PathOnly(string requestPath)

- static long ParseHttpDate(string s)

- static List<string> Tokens(string s)

- static List<string> SplitColon(string s)

- static int MonthOf(string t)

- static bool AllDigits(string s)

- static long ParseLong(string s)

- static int IndexOfChar(string s, int ch, int from)

- static string Trim(string s)

- static string Lower(string s)

- static bool NameEquals(string head, int at, string lower)


## HttpClient (class)

- string host;

- int port;

- int timeout;

- bool useTls;

- bool verifyTls;

- string clientCertificateFile;

- string clientPrivateKeyFile;

- List<string> defaultHeaders;

- List<string> tlsPins;

- bool pinningEnabled;

- CertPolicyFn certPolicy;

- CookieJar jar;

- HttpCancelFn cancelProbe;

- HttpUploadProgressFn uploadProbe;

- ExternalCallPolicy callPolicy;

- string lastDownloadError;

- long totalDeadlineUs;

- HttpFramer ka;

- TcpClient kaConn;

- TlsStream kaTls;

- TlsContext kaCtx;

- bool kaBusy;

- bool kaEnabled;

- [DllImport("crt")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern int fputs(string str, nint fp);

- [DllImport("crt")]static extern int fclose(nint fp);

- [DllImport("crt")]static extern long fwrite(string buf, long size, long count, nint fp);

- [DllImport("crt")]static extern long fread(byte[]buf, long size, long count, nint fp);

- [DllImport("crt")]static extern int fseek(nint fp, int offset, int origin);

- [DllImport("crt")]static extern int ftell(nint fp);

- HttpClient(string host, int port)

- static HttpClient CreateHttps(string host, int port)

- HttpClient UseTls()

- HttpClient DisableTlsVerify()

- HttpClient SetCertPolicy(CertPolicyFn fn)

- TlsContext ClientTlsContext()

- HttpClient UseCookies()

- CookieJar Cookies()

- HttpClient SetCancelProbe(HttpCancelFn f)

- HttpClient SetUploadProbe(HttpUploadProgressFn f)

- void UploadTick(long sentBytes, long totalBytes)

- string LastDownloadError()

- void SetDownloadError(string error)

- bool CancelRequested()

- HttpClient PinPublicKey(string spkiSha256Base64)

- HttpClient DisablePinning()

- void ApplyPinning(TlsContext ctx)

- HttpClient SetClientCertificate(string certFile, string keyFile)

- HttpClient SetHeader(string name, string headerValue)

- HttpClient SetTimeout(int ms)

- HttpClient SetCallPolicy(ExternalCallPolicy policy)

- ExternalCallPolicy CallPolicy()

- void CheckTargetPolicy()

- void CheckRequestLimit(long bodyBytes)

- int ResponseLimit()

- int BudgetMin(int phaseMs)

- int ConnectBudget()

- int WriteBudget()

- int ReadBudget()

- int IdleBudget()

- int HandshakeBudget()

- int TotalBudget()

- void ArmTotalBudget()

- void ResetTotalBudget()

- void Close()

- HttpClient DisableKeepAlive()

- void DropAlive()

- bool HasHeaderName(string name)

- static bool AsciiNameEquals(string hay, string name, int n)

- static bool IsRequestTargetSafe(string path)

- static bool IsMethodSafe(string method)

- static bool IsHostSafe(string host)

- string BuildRequestHead(string method, string path, long bodyLen, bool close)

- string BuildRequest(string method, string path, string body, bool close)

- string BuildDownloadRequest(string path, long have)

- void AbsorbCookies(string path, string raw)

- void FailConnect()

- void FailTimeout()

- async TcpClient ConnectTarget()

- async string RequestAsync(string method, string path, string body)

- string RedirectHost()

- string CanonicalRequestTarget(string requestPath)

- bool IsRedirectHeader(string line, bool sameOrigin)

- bool IsRedirectHeader(string line, bool sameOrigin, bool dropEntity)

- bool RedirectWanted(HttpResponse response, int hops)

- bool RedirectReplay(string method, int code, out string nextMethod)

- HttpClient RedirectNextClient(HttpClient current, ExternalTarget target, bool sameOrigin, bool dropEntityHeaders)

- async HttpResponse FollowBytesRedirects(string method, string path, string body, HttpBytesDriver driver)

- async HttpResponse FollowBytesFromResponse(string method, string path, string body, HttpBytesDriver driver, HttpResponse resp)

- void ValidateTextResponse(string raw, string method)

- async string RequestOnceAsync(string method, string path, string body)

- async string RequestTlsAsync(string method, string path, string body)

- async TlsStream ConnectTls(TlsContext ctx, nint sock)

- async string RequestAliveAsync(string method, string path, string body)

- async string RequestAliveCore(string method, string path, string body)

- async bool ConnectAlive()

- static bool HeadHasContentLength(string head)

- static bool HeadSaysClose(string head)

- async HttpResponse SendAsync(string method, string path, string body)

- async HttpResponse SendBytesAsync(string method, string path, string body)

- async HttpResponse SendBytesAsync(string method, string path, string body, int bodyLen)

- async HttpResponse SendBytesAsync(string method, string path, byte[]body, int bodyLen)

- async HttpResponse UploadFileRawAsync(string method, string path, string localPath, string contentType)

- static async HttpResponse SendBytesOnceAsync(HttpClient client, string method, string path, string body)

- static async HttpResponse SendBytesOnceWithRawBytesAsync(HttpClient client, string method, string path, byte[]body, int bodyLen)

- static async HttpResponse SendBytesOnceWithLenAsync(HttpClient client, string method, string path, string body, int bodyLen)

- async HttpResponse ReadFramedResponse(TcpClient conn, string path, string method, HttpDeadlineToken watch, TlsStream stream, TlsContext ctx)

- void CloseChannel(TcpClient conn, HttpFramer fr, TlsStream stream, TlsContext ctx)

- async HttpResponse SendBytesTlsAsync(string method, string path, string body)

- async HttpResponse SendBytesTlsWithLenAsync(string method, string path, string body, int bodyLen)

- async HttpResponse SendBytesTlsWithRawBytesAsync(string method, string path, byte[]body, int bodyLen)

- async HttpResponse SendBytesBodyAsync(string method, string path, string head, string localPath, long fileLen, string tail, long total)

- static async HttpResponse UploadBytesOnceAsync(HttpClient client, string method, string path, string body)

- async HttpResponse SendBytesBodyTlsAsync(string method, string path, string head, string localPath, long fileLen, string tail, long total)

- async string GetAsync(string path)

- async string PostAsync(string path, string body)

- async string PutAsync(string path, string body)

- async string DeleteAsync(string path)

- static async string GetHttpsAsync(string host, int port, string path)

- static async string GetAsync(string host, int port, string path)

- static async string PostAsync(string host, int port, string path, string body)

- static async void DownloadFileAsync(string host, int port, string path, string localPath)

- static int IndexOf(string s, string needle, int from)

- static int ParseHex(string s)

- static void AppendText(string file, string s)

- static void TruncateFile(string file)

- static bool HeadIsChunked(string head)

- static int HeadStatus(string head)

- async int PostSseToFileAsync(string path, string body, string outFile, string doneFile)

- async int PostSseToSinkAsync(string path, string body, SseSink sink)

- static string KeepHead(string acc, string piece)

- static bool EndsWithStr(string s, string suf)

- static string OneLine(string s)

- static bool EndsWithCi(string s, string suf)

- static void ValidateMultipartToken(string what, string value)

- static string GuessMime(string name)

- static string HeadHeader(string head, string lowerName)

- static long HeaderLong(string head, string lowerName)

- static int HeaderInt(string head, string lowerName)

- static long RangeTotal(string head)

- static bool RangeAlreadyComplete(string head, long have)

- async HttpResponse UploadFileAsync(string path, string field, string localPath, string fileName)

- async HttpResponse UploadFileBytesAsync(string path, string field, string localPath, string fileName)

- async int DownloadRangeToFileAsync(string path, string localPath, string progressFile)

- async int DownloadRangeOnceAsync(string path, string localPath, string progressFile, List<string> visited, int hops)

- async long DownloadBinaryToFileAsync(string path, string localPath)

- async long DownloadBinaryToFileAsync(string path, string localPath, string progressFile)

- async long DownloadBinaryOnceAsync(string path, string localPath, string progressFile, List<string> visited, int hops)

- static string NewBuffer(int size)


## SseDelivery (class)

- string data;

- bool finished;


## SseSink (class)

- string path;

- string donePath;

- nint fp;

- List<string> parts;

- int cut;

- string attempt;

- string done;

- int total;

- bool aborted;

- bool overflowed;

- int maxBytes;

- int responseStatus;

- string responseHeaders;

- string responseErrorBody;

- Gate ready;

- nint lockHandle;

- nint lockHandle;

- [DllImport("crt", EntryPoint="fopen")]static extern nint fopen(string path, string mode);

- [DllImport("crt", EntryPoint="fclose")]static extern int fclose(nint fp);

- [DllImport("crt", EntryPoint="fwrite")]static extern long fwrite(string buf, long size, long count, nint fp);

- static SseSink ToFile(string outFile, string doneFile)

- static SseSink ToMemory()

- void Begin()

- void SetMaxBytes(int bytes)

- void Write(string piece)

- bool TryWrite(string piece)

- void Abort()

- void Overflow()

- bool Aborted()

- bool Overflowed()

- void SetResponse(int status, string headers, string errorBody)

- int ResponseStatus()

- string ResponseHeaders()

- string ResponseErrorBody()

- string Join(int from)

- void Finish(string marker)

- string Attempt()

- void Publish(string marker)

- string All()

- string Since()

- string Take()

- async SseDelivery TakeCo()

- async SseDelivery TakeCo(int timeoutMs)

- string Done()

- int Total()

- void Dispose()


## HttpResponse (delegate)

`delegate HttpResponse HttpBytesDriver(HttpClient client, string method, string path, string body);`


## bool (delegate)

`delegate bool HttpCancelFn();`


## void (delegate)

`delegate void HttpUploadProgressFn(int sentBytes, int totalBytes);`
