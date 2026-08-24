# System.Net.WebDav

> 源码: `stdlib/System/Net/WebDav/WebDavClient.zan`


## WebDavClient (class)

- string host;

- int port;

- bool useTls;

- bool verifyTls;

- int timeout;

- List<string> headers;

- WebDavClient()

- WebDavClient(string host, int port)

- static WebDavClient CreateHttps(string host, int port)

- WebDavClient SetHeader(string name, string headerValue)

- WebDavClient SetTimeout(int milliseconds)

- WebDavClient DisableTlsVerify()

- static string BuildPropFindXml()

- static string EscapeXml(string text)

- static string UnescapeXml(string text)

- static List<WebDavResource> ParseMultiStatus(string xml)

- async List<WebDavResource> PropFindAsync(string path, string depth)

- async HttpResponse MkColAsync(string path)

- async HttpResponse GetAsync(string path)

- async HttpResponse PutAsync(string path, string body)

- async HttpResponse DeleteAsync(string path)

- async HttpResponse CopyAsync(string path, string destination, bool overwrite)

- async HttpResponse MoveAsync(string path, string destination, bool overwrite)

- HttpClient NewHttpClient()

- static void ValidateDepth(string depth)

- static void ValidateDestination(string destination)

- static void ValidateHeader(string name, string headerValue)

- static bool HasControl(string text)

- static string TagValue(string xml, string localName)

- static string LastTagValue(string xml, string localName)

- static string TagInner(string xml, string localName)

- static int FindOpen(string xml, string localName, int from)

- static int FindClose(string xml, string localName, int from)

- static int IndexOf(string haystack, string needle, int from)

- static int TagEnd(string xml, int start)

- static string Trim(string text)

- static bool IsSpace(string ch)

- static int ParseStatus(string text)

- static long ParseLong(string text)


## WebDavResource (class)

- string href;

- int status;

- bool collection;

- long contentLength;

- string contentType;

- string etag;

- string lastModified;

- WebDavResource()
