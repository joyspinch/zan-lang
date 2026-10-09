# System.Net.External

> 源码: `packages/Zan.Net/src/System/Net/External/ExternalCallPolicy.zan`, `packages/Zan.Net/src/System/Net/External/ExternalTarget.zan`, `packages/Zan.Net/src/System/Net/External/ExternalTargetPolicy.zan`


## ExternalCallPolicy (class)

- int connectTimeoutMs;

- int writeTimeoutMs;

- int readTimeoutMs;

- int idleTimeoutMs;

- int totalTimeoutMs;

- int maxRequestBytes;

- int maxResponseBytes;

- int maxErrorBytes;

- int maxStreamBytes;

- int maxRedirects;

- int retryAttempts;

- int retryBackoffMs;

- bool allowHttp;

- bool allowLoopback;

- bool localHttpOnly;

- bool retryNetworkErrors;

- bool idempotent;

- string idempotencyKey;

- ExternalCallPolicy()

- static ExternalCallPolicy Default()

- ExternalCallPolicy ConnectTimeout(int ms)

- ExternalCallPolicy WriteTimeout(int ms)

- ExternalCallPolicy ReadTimeout(int ms)

- ExternalCallPolicy IdleTimeout(int ms)

- ExternalCallPolicy TotalTimeout(int ms)

- ExternalCallPolicy RequestLimit(int bytes)

- ExternalCallPolicy ResponseLimit(int bytes)

- ExternalCallPolicy ErrorLimit(int bytes)

- ExternalCallPolicy StreamLimit(int bytes)

- ExternalCallPolicy Redirects(int count)

- ExternalCallPolicy RetryNetwork(bool enabled)

- ExternalCallPolicy RetryAttempts(int count)

- ExternalCallPolicy RetryBackoff(int ms)

- ExternalCallPolicy AllowLocalHttp()

- ExternalCallPolicy AllowLoopback()

- ExternalCallPolicy Idempotent(string key)

- int ConnectTimeoutMs()

- int WriteTimeoutMs()

- int ReadTimeoutMs()

- int IdleTimeoutMs()

- int TotalTimeoutMs()

- int MaxRequestBytes()

- int MaxResponseBytes()

- int MaxErrorBytes()

- int MaxStreamBytes()

- int MaxBodyBytes()

- int MaxRedirects()

- int RetryAttempts()

- int RetryBackoffMs()

- bool AllowsHttp()

- bool AllowsLoopback()

- bool LocalHttpOnly()

- bool IsIdempotent()

- string IdempotencyKey()

- bool CanRetry(string method, bool requestStarted, int statusCode)


## ExternalTarget (class)

- string scheme;

- string host;

- string path;

- string query;

- string canonical;

- int port;

- bool literal;

- bool safeLiteral;

- bool loopback;

- ExternalTarget()

- static ExternalTarget Parse(string url)

- string Scheme()

- string Host()

- int Port()

- string Path()

- string Query()

- string Canonical()

- bool IsTls()

- bool IsLiteral()

- bool IsSafeLiteral()

- bool IsLoopback()

- static bool ValidText(string s)

- static int PortOf(string authority, int at)

- static bool ValidDnsName(string h)

- static bool ValidIPv4(string h)

- static int HexValue(int c)

- static int ParseHex(string s)

- static bool ValidIPv6(string h)

- static bool IsLoopbackHost(string h)

- static bool SafeLiteral(string h)


## ExternalTargetPolicy (class)

- static bool IsAllowed(string url)

- static bool IsLocalAllowed(string url)

- static bool IsLocalTarget(ExternalTarget t, bool allowHttp)

- static bool AllowExact(string url, string expected)

- static bool AllowExactLocal(string url, string expected)

- static bool AllowHostPort(string url, string host, int port, bool tls)

- static bool IsAllowedTarget(ExternalTarget t, bool allowLoopback, bool allowHttp)

- static bool IsPublicIPv4(string host)

- static bool IsClientAllowed(string host, int port, bool tls, bool allowLoopback, bool allowHttp, bool localHttpOnly)

- static bool IsLoopback(string host)
