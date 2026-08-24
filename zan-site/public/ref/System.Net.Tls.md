# System.Net.Tls

> 源码: `stdlib/System/Net/Tls/TlsStream.zan`


## TlsContext (class)

TLS 上下文：封装 OpenSSL 的 SSL_CTX。每个服务器（带
证书 + 私钥）或每个客户端创建一个，再据此为每个连接派生
`TlsStream`。

服务器端：
TlsContext tls = TlsContext.CreateServer("cert.pem", "key.pem");
TlsStream s = await TlsStream.AcceptAsync(tls, clientSock);

客户端：
TlsContext tls = TlsContext.CreateClient();
TlsStream s = await TlsStream.ConnectAsync(tls, sock, "example.com");

- [DllImport("ssl")]static extern string TLS_server_method();

- [DllImport("ssl")]static extern string TLS_client_method();

- [DllImport("ssl")]static extern string SSL_CTX_new(string method);

- [DllImport("ssl")]static extern void SSL_CTX_free(string ctx);

- [DllImport("ssl")]static extern int SSL_CTX_use_certificate_chain_file(string ctx, string file);

- [DllImport("ssl")]static extern int SSL_CTX_use_PrivateKey_file(string ctx, string file, int type);

- [DllImport("ssl")]static extern int SSL_CTX_check_private_key(string ctx);

- [DllImport("ssl")]static extern void SSL_CTX_set_verify(string ctx, int mode, string callback);

- [DllImport("ssl")]static extern int SSL_CTX_set_default_verify_paths(string ctx);

- [DllImport("ssl")]static extern int SSL_CTX_load_verify_locations(string ctx, string file, string path);

- [DllImport("ssl")]static extern string SSL_CTX_get_cert_store(string ctx);

- [DllImport("crypto")]static extern long ERR_get_error();

- [DllImport("crypto")]static extern string d2i_X509(string px, byte[]pp, int len);

- [DllImport("crypto")]static extern int X509_STORE_add_cert(string store, string x);

- [DllImport("crypto")]static extern void X509_free(string x);

- [DllImport("crt", EntryPoint="memcpy")]static extern nint PlatMemCopyIn(byte[]dst, nint src, long n);

- [DllImport("crt", EntryPoint="memcpy")]static extern nint PlatMemCopyOut(nint dst, string src, long n);

- [DllImport("crypt32", EntryPoint="CertOpenSystemStoreA")]static extern nint CertOpenSystemStore(nint prov, string name);

- [DllImport("crypt32")]static extern nint CertEnumCertificatesInStore(nint store, nint prev);

- [DllImport("crypt32")]static extern int CertCloseStore(nint store, int flags);

- [DllImport("crt", EntryPoint="zan_crypto_cert_encoded")]static extern nint CertEncoded(nint cert, nint outLen);

- string ctx;

- bool server;

- string error;

- bool verify;

- string trust;

- List<string> pins;

- bool pinRequired;

- TlsContext()

- void AddPin(string spkiSha256Base64)
  - 添加一个可接受的服务器公钥 pin：即
    DER SubjectPublicKeyInfo 的 base64 SHA-256（即由
    `openssl ... -pubkey | openssl pkey -pubin -outform der
    | openssl dgst -sha256 -binary | base64` 命令得到的值）。

- void RequirePinning(bool on)
  - 开关 pin 强制校验。发布构建始终开启，保证
    发布的二进制始终校验配置的 pin；HTTP 客户端中的开发开关
    是唯一可以放宽它的途径。

- bool PinningActive()
  - 当握手必须满足至少一个已配置的
    pin 时为 true。

- bool PinMatches(string got)
  - `got`（base64 的 SPKI SHA-256）与任一 pin 匹配时为 true。

- static TlsContext CreateServer(string certFile, string keyFile)
  - 从 PEM 证书链
    和 PEM 私钥创建服务器端 TLS 上下文。失败（路径错误/私钥
    不匹配）时返回 null；原因见 LastError()。

- static TlsContext CreateClient()
  - 创建客户端 TLS 上下文。证书校验
    使用系统默认信任库；如需自签名开发服务器，可调用
    DisableVerify()。

- static TlsContext CreateClientWithCertificate(string certFile, string keyFile)
  - 创建带 PEM 证书链和对应 PEM 私钥的
    客户端 TLS 上下文，用于双向 TLS 认证。

- void DisableVerify()
  - 禁用对端证书校验（仅限开发）。
    0 = SSL_VERIFY_NONE。

- static nint PtrAt(nint ptr, int off)

- static int IntAt(nint ptr, int off)

- static void LoadSystemRootsWindows(string sslctx)

- static string LoadSystemRootsUnix(string sslctx)

- string TrustHint()
  - 链校验失败时补充的定位提示：本机一个系统 CA
    信任库都没装上时点名该原因，否则为 ""。

- static byte[]PemBodyToDer(string pem, int expectedLen)

- bool AddTrustedCert(string pemFile)
  - 把 PEM 格式的 CA（或自签名）证书加入本上下文的信任库，
    使其签发的对端证书在链校验中被接受。返回 true 表示成功。
    用于私有 CA 与测试（自签名开发服务器）。

- string Handle()

- bool IsServer()

- void Free()
  - 释放底层的 SSL_CTX。


## TlsStream (class)

非阻塞套接字上的一条 TLS 连接，由 OpenSSL 内存
BIO 驱动，因此所有套接字 IO 都经过协程 reactor（await
Socket.ReadReady / Socket.SendAsync），TLS 不会阻塞线程。

加密字节在 socket <-> rbio/wbio 间流动；SSL_read/SSL_write 使
明文出入 SSL 引擎。WANT_READ 时向 rbio 灌入一次套接字读取；
wbio 中待发的字节则在每次引擎步骤后
刷回套接字。

- [DllImport("ssl")]static extern string SSL_new(string ctx);

- [DllImport("ssl")]static extern void SSL_free(string ssl);

- [DllImport("ssl")]static extern void SSL_set_bio(string ssl, string rbio, string wbio);

- [DllImport("ssl")]static extern void SSL_set_accept_state(string ssl);

- [DllImport("ssl")]static extern void SSL_set_connect_state(string ssl);

- [DllImport("ssl")]static extern int SSL_do_handshake(string ssl);

- [DllImport("ssl")]static extern int SSL_read(string ssl, string buf, int num);

- [DllImport("ssl")]static extern int SSL_write(string ssl, string buf, int num);

- [DllImport("ssl")]static extern int SSL_get_error(string ssl, int ret);

- [DllImport("ssl")]static extern int SSL_shutdown(string ssl);

- [DllImport("ssl")]static extern int SSL_ctrl(string ssl, int cmd, int larg, string parg);

- [DllImport("ssl")]static extern string SSL_get0_param(string ssl);

- [DllImport("ssl")]static extern long SSL_get_verify_result(string ssl);

- [DllImport("crypto")]static extern int X509_VERIFY_PARAM_set1_host(string param, string name, long len);

- [DllImport("crypto")]static extern int X509_VERIFY_PARAM_set1_ip_asc(string param, string ipasc);

- [DllImport("crypto")]static extern void X509_VERIFY_PARAM_set_hostflags(string param, long flags);

- [DllImport("crypto")]static extern string X509_VERIFY_PARAM_get0_name(string param);

- [DllImport("crypto")]static extern string BIO_new(string method);

- [DllImport("crypto")]static extern string BIO_s_mem();

- [DllImport("crypto")]static extern int BIO_write(string bio, string data, int dlen);

- [DllImport("crypto")]static extern int BIO_read(string bio, string data, int dlen);

- [DllImport("crypto")]static extern int BIO_ctrl_pending(string bio);

- [DllImport("ssl")]static extern string SSL_get1_peer_certificate(string ssl);

- [DllImport("crypto")]static extern string X509_get_X509_PUBKEY(string x);

- [DllImport("crypto")]static extern int i2d_X509_PUBKEY(string pubkey, byte[]pp);

- [DllImport("crypto")]static extern void X509_free(string x);

- [DllImport("crypto", EntryPoint="CRYPTO_free")]static extern void OpenSslFree(nint p, string file, int line);

- [DllImport("crt", EntryPoint="memcpy")]static extern nint PlatMemCopyIn(byte[]dst, nint src, long n);

- string ssl;

- string rbio;

- string wbio;

- nint sock;

- bool open;

- byte[]scratch;

- static int SCRATCH=17408;

- static string lastError="";

- static int Norm(int v)

- static string LastError()

- static void SetLastError(string error)

- TlsStream()

- static TlsStream Setup(TlsContext ctx, nint sock)

- static async TlsStream AcceptAsync(TlsContext ctx, nint sock)
  - 服务器端：包装已接受的套接字并执行 TLS
    握手。握手失败时返回 null。

- static async TlsStream ConnectAsync(TlsContext ctx, nint sock, string host)
  - 客户端：包装已连接的套接字，发送
    <paramref name="host"/> 的 SNI 并执行 TLS 握手。握手失败时
    返回 null。

- static bool IsIpLiteral(string host)

- static string SpkiPinOf(string ssl)
  - 对端证书 DER
    SubjectPublicKeyInfo 的 base64 SHA-256（即通过 TlsContext.AddPin 固定的值），
    没有对端证书/编码时返回 ""。

- async int FlushOutAsync()

- async int PumpInAsync()

- async int HandshakeAsync()
  - 驱动 TLS 握手直至完成。成功返回 1。

- async string RecvAsync(int max)
  - 接收解密后的应用字节（最多
    <paramref name="max"/>，最多一个暂存缓冲区）。正常关闭或出错时
    返回 ""。

- async int RecvIntoAsync(string buf, int max)
  - 接收解密后的字节到调用方提供的缓冲区
    （二进制安全：返回字节数，不做 NUL 截断）。
    正常关闭返回 0，出错返回 -1。

- async int SendAsync(string data, int len)
  - 加密并发送 <paramref name="len"/> 字节。成功返回
    明文字节数，失败返回 -1。

- async int SendStringAsync(string data)
  - 发送整个字符串（其 .Length 个字节）。

- void Close()
  - 发送 TLS close_notify 警告并释放 SSL 引擎。
    底层套接字仍归调用方所有（且必须由其关闭）。
