# System.Net.WebDav

> 源码: `stdlib/System/Net/WebDav/WebDavClient.zan`


## WebDavClient (class)

极简 WebDAV 客户端：PROPFIND（列目录）、MKCOL/GET/PUT/DELETE/
COPY/MOVE。每次调用新建一个底层 HttpClient（无连接复用）；
传输层失败抛 HttpRequestException，WebDAV 状态码由调用方检查
（PROPFIND 非 207 直接抛）。XML 解析是手写的容错标签扫描，
Multi-Status 上限 4 MiB / 10000 个资源，超限抛 ArgumentException。

- string host;

- int port;

- bool useTls;

- bool verifyTls;

- int timeout;

- List<string> headers;

- WebDavClient()
  - 内部构造：默认明文 80 端口、校验 TLS、30s 超时；经
    WebDavClient(host, port) / CreateHttps 使用。

- WebDavClient(string host, int port)
  - 构造指向 host:port 的明文 HTTP WebDAV 客户端。

- static WebDavClient CreateHttps(string host, int port)
  - 构造 HTTPS WebDAV 客户端（证书校验保持开启）。

- WebDavClient SetHeader(string name, string headerValue)
  - 为所有请求添加默认头（名字/值经 ValidateHeader 校验，非法抛
    ArgumentException）；可链式调用。

- WebDavClient SetTimeout(int milliseconds)
  - 设置请求超时（毫秒），转发给底层 HttpClient；可链式调用。

- WebDavClient DisableTlsVerify()
  - 禁用服务器证书校验（自签名服务器用）；同时隐含启用 TLS。

- static string BuildPropFindXml()
  - PROPFIND 请求体：请求 resourcetype/getcontentlength/
    getcontenttype/getetag/getlastmodified 五个属性。

- static string EscapeXml(string text)
  - XML 五个预定义实体的转义。

- static string UnescapeXml(string text)
  - XML 五个预定义实体的反转义；未知实体原样保留 "&"。

- static List<WebDavResource> ParseMultiStatus(string xml)
  - 解析 207 Multi-Status 响应体为资源列表（按文档顺序）；
    超 4 MiB 或 10000 个资源抛 ArgumentException。

- async List<WebDavResource> PropFindAsync(string path, string depth)
  - PROPFIND 列出 <paramref name="path"/>：depth 为 "0"（自身）、
    "1"（直接子级）或 "infinity"。响应非 207 抛 HttpRequestException
    （响应已释放），成功返回解析后的资源列表。

- async HttpResponse MkColAsync(string path)
  - 创建集合（目录）；WebDAV 状态码（201/405 等）在返回的响应里。

- async HttpResponse GetAsync(string path)
  - 下载资源；状态码/正文在返回的响应里。

- async HttpResponse GetBinaryAsync(string path)
  - 二进制安全下载资源（状态码/正文字节数组在返回的响应里）。

- async HttpResponse PutAsync(string path, string body)
  - 上传资源（Content-Type: application/octet-stream）；
    201/204 等状态码在返回的响应里。

- async HttpResponse PutBytesAsync(string path, byte[]data)
  - 上传二进制资源（Content-Type: application/octet-stream）；
    零中转字符串拷贝，201/204 等状态码在返回的响应里。

- async HttpResponse PutFileAsync(string path, string localPath)
  - 流式上传本地文件（PUT application/octet-stream）；
    单句柄 64KB 流式直发，零整文件内存缓冲，支持超 2GB 大文件，201/204 等状态码在返回的响应里。

- async long GetToFileAsync(string path, string localPath)
  - 流式下载 WebDAV 资源到本地文件，支持断点续传与超 2GB 大文件。成功返回已接收字节数。

- async HttpResponse DeleteAsync(string path)
  - 删除资源或集合；状态码在返回的响应里。

- async HttpResponse CopyAsync(string path, string destination, bool overwrite)
  - 复制资源到 destination（绝对路径或完整 http(s) URI；
    非法抛 ArgumentException），overwrite 选 Overwrite: T/F 头。

- async HttpResponse MoveAsync(string path, string destination, bool overwrite)
  - 移动资源到 destination（校验与 Overwrite 头同 CopyAsync）。

- HttpClient NewHttpClient()
  - 每次调用新建的底层 HttpClient：按 useTls/verifyTls 建连，
    套用 timeout，并把默认头逐条搬过去。

- static void ValidateDepth(string depth)
  - Depth 头只接受 "0"、"1"、"infinity"，否则抛 ArgumentException。

- static void ValidateDestination(string destination)
  - Destination 必须是绝对路径或 http(s) URI 且不含控制字符，
    否则抛 ArgumentException。

- static void ValidateHeader(string name, string headerValue)
  - 头名字非空、不含冒号，名字与值都不含控制字符，否则抛
    ArgumentException（头注入防线）。

- static bool HasControl(string text)
  - 文本是否含控制字符（< 32 或 DEL）。

- static string TagValue(string xml, string localName)
  - 第一个 localName 标签的去转义文本内容；不存在为 ""。

- static string LastTagValue(string xml, string localName)
  - 最后一个 localName 标签的去转义文本内容；不存在为 ""。
    （status 行可能在一个 response 里出现多次，取最后一个为准。）

- static string TagInner(string xml, string localName)
  - 第一个 localName 标签的原始内部文本（不去转义、不去空白）；
    自闭合或不存在为 ""。resourcetype 里找 <collection/> 用。

- static int FindOpen(string xml, string localName, int from)
  - 从 from 起找第一个名为 localName 的开始标签（忽略命名空间
    前缀，跳过注释/处理指令/结束标签）；没有返回 -1。

- static int FindClose(string xml, string localName, int from)
  - 从 from 起找第一个 localName 结束标签（忽略命名空间前缀）；
    没有返回 -1。

- static int IndexOf(string haystack, string needle, int from)
  - 从 from 起的子串搜索；没有返回 -1。

- static int TagEnd(string xml, int start)
  - 标签开始处的 "<" 的下标 → 结束 ">" 的下标（跳过引号里的
    ">"，属性值因此可含该字符）；没有返回 -1。

- static string Trim(string text)
  - 去掉两侧 XML 空白（空格/制表/CR/LF）。

- static bool IsSpaceByte(int c)
  - 该单字符字节是否为 XML 空白。

- static bool IsSpace(string ch)
  - 该单字符是否为 XML 空白。

- static int ParseStatus(string text)
  - 从 "HTTP/1.1 207 Multi-Status" 一类文本里取第一段三位数字；
    没有返回 0。

- static long ParseLong(string text)
  - 解析无符号十进制整数；含非数字或空串返回 0。


## WebDavResource (class)

PROPFIND 207 Multi-Status 里解析出的一个资源：
href、HTTP 状态，以及 resourcetype/getcontentlength/getcontenttype/
getetag/getlastmodified 五个属性（服务器未返回的为默认值）。

- string href;

- int status;

- bool collection;

- long contentLength;

- string contentType;

- string etag;

- string lastModified;

- WebDavResource()
  - 内部构造：全默认值，字段由 ParseMultiStatus 填充。
