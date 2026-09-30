# System.Net.Tls

> 源码: `stdlib/System/Net/Tls/TlsHandshake.zan`, `stdlib/System/Net/Tls/TlsRecord.zan`, `stdlib/System/Net/Tls/TlsStream.zan`


## TlsByteReader (class)

字节数组大端序安全读取器。

- byte[]data;

- int pos;

- int limit;

- TlsByteReader(byte[]data, int offset, int length)

- int Remaining()

- int Offset()

- bool Has(int count)

- int ReadU8()

- int ReadU16()

- int ReadU24()

- byte[]ReadBytes(int count)

- void Skip(int count)


## TlsByteWriter (class)

动态字节数组构建器。

动态字节数组构建器（带双指针滑动窗口，消除高频前缀消费带来的 O(N) 内存拷贝开销）。

- byte[]buf;

- int head;

- int len;

- TlsByteWriter()

- void EnsureCap(int extra)

- void WriteU8(int v)

- void WriteU16(int v)

- void WriteU24(int v)

- void WriteBytes(byte[]b, int offset, int count)

- void WriteBytes(byte[]b)

- int Length()

- int Head()

- byte[]Buffer()

- void Advance(int count)

- int PeekU8(int off)

- int PeekU16(int off)

- byte[]ToArray()

- void RemovePrefix(int count)


## TlsCipherState (class)

TLS 记录层 AEAD (AES-128-GCM) 密码状态机。
支持 TLS 1.2 与 TLS 1.3 双版本单向记录加密与解密。
零 C 语言与零 OpenSSL 依赖，100% Zan 原生实现。

- int version;

- byte[]key;

- byte[]iv;

- long seq;

- byte[]gcmCtx;

- bool hasCtx;

- byte[]nonceBuf;

- byte[]seqBuf;

- byte[]aadBuf;

- byte[]decBuf;

- TlsCipherState(int version, byte[]key, byte[]iv)

- int Version()

- long SequenceNumber()

- int RecordLen(int plainLen)

- int EncryptToBuf(int contentType, nint dataPtr, int len, byte[]outBuf, int outOffset)
  - 直写加密并封装为完整 TLS 记录（含 5 字节记录头）。返回写入的记录总字节数，失败返回 -1。

- byte[]Encrypt(int contentType, byte[]data, int offset, int len)
  - 加密明文负载并封装为完整 TLS 记录（含 5 字节记录头）。

- byte[]Decrypt(byte[]header, byte[]payload, int pOffset, int pLen, List<int> outContentType)
  - 解密 TLS 记录密文负载，剥离认证标签与填充，产出原始明文与真实 ContentType。

- int EncryptTls12ToBuf(int contentType, nint dataPtr, int len, byte[]outBuf, int outOffset)

- byte[]DecryptTls12(byte[]header, byte[]payload, int pOffset, int pLen, List<int> outContentType)

- int EncryptTls13ToBuf(int contentType, nint dataPtr, int len, byte[]outBuf, int outOffset)

- byte[]DecryptTls13(byte[]header, byte[]payload, int pOffset, int pLen, List<int> outContentType)


## TlsContext (class)

纯 Zan 原生 TLS 上下文。
记录与握手无 OpenSSL 依赖；Windows 客户端系统信任经 Crypt32 SSL 链策略验证。

- bool server;

- string error;

- bool verify;

- bool disableTls13;

- string certFile;

- string keyFile;

- string trust;

- List<string> pins;

- bool pinRequired;

- CertPolicyFn certPolicy;

- List<X509Certificate> trustedCerts;

- List<X509Certificate> cachedChain;

- X509Certificate cachedCert;

- RsaKey cachedKey;

- EcKey cachedEcKey;

- static int MAX_TRUST_BUNDLE_BYTES=4194304;

- static int MAX_TRUST_CERT_BYTES=32768;

- static int MAX_TRUST_CERTS=2048;

- static int MAX_CHAIN_DER_BYTES=131072;

- static string PEM_BEGIN="-----BEGIN CERTIFICATE-----";

- static string PEM_END="-----END CERTIFICATE-----";

- TlsContext()

- static List<X509Certificate> ParseTrustBundle(string pem, bool strict)

- static List<X509Certificate> ReadTrustBundle(string path, bool strict)

- static List<X509Certificate> ParseCertChain(string pem)
  - 解析服务端证书 PEM 链束：全部证书按出现顺序保留（叶子在
    前），数量与 DER 总长受限；任何一张无法解析即整体拒绝。

- static List<X509Certificate> ReadCertChainFile(string path)

- static List<X509Certificate> TrustFromCandidates(string[]paths)
  - 按顺序尝试每个候选 bundle 路径：存在但损坏/不可解析的
    候选只被跳过，不阻断后续候选；全部候选都不可用才返回 null
    （调用方随之 fail-closed）。

- bool AddTrustedCert(string pemFile)
  - Add every supported CA certificate in a bounded PEM bundle, in addition to defaults.

- static List<X509Certificate> SystemTrustRoots()

- void SetCertPolicy(CertPolicyFn fn)

- bool PolicyActive()

- void AddPin(string spkiSha256Base64)

- void RequirePinning(bool on)

- bool PinningActive()

- bool PinMatches(string got)

- static TlsContext CreateServer(string certFile, string keyFile)
  - 创建服务器端 TLS 上下文。证书文件可以是 PEM 链束
    （叶子在前，其后为中间证书）；单证书文件行为不变。链束任何一环
    无法解析即整体拒绝，不得静默降级为只发叶子。

- static TlsContext CreateClient()
  - 创建客户端 TLS 上下文；Windows 使用系统 SSL 链策略，Linux 使用发行版 CA bundle。

- static TlsContext CreateClientWithCertificate(string certFile, string keyFile)
  - 创建带客户端证书与私钥的 TLS 上下文（双向认证）。

- void DisableVerify()
  - 禁用对端证书与主机名校验（仅用于本地自签名开发调试）。

- void DisableTls13()
  - 禁用 TLS 1.3，将协商上限限制为 TLS 1.2。

- string Handle()

- bool IsServer()

- string TrustHint()

- void Free()


## TlsEngine (class)

纯 Zan 原生 TLS 1.2 / TLS 1.3 客户端与服务端流引擎。
TLS 记录与握手为原生 Zan；Windows 客户端证书链使用系统 Crypt32 SSL 策略。

- [DllImport("crt", EntryPoint="zan_io_crypto_windows_ssl_policy")]static extern int WindowsSslPolicy(nint certs, int totalLen, int count, nint host, int hostLen);

- static int StateInitial=0;

- static int StateClientHelloSent=1;

- static int StateTls13Handshake=2;

- static int StateTls12Handshake=3;

- static int StateTls12WaitingFinished=4;

- static int StateServerWaitingClientHello=5;

- static int StateServerTls13WaitingFinished=6;

- static int StateServerTls12WaitingClientKeyExchange=7;

- static int StateServerTls12WaitingFinished=8;

- static int StateConnected=9;

- static int StateClosed=10;

- static int StateFailed=11;

- static int MaxPreAuthHandshakeBytes=262144;

- string host;

- bool disableVerify;

- bool disableTls13;

- List<string> pins;

- bool pinRequired;

- CertPolicyFn certPolicy;

- List<X509Certificate> trustedCerts;

- bool isServer;

- string certFile;

- string keyFile;

- X509Certificate cachedCert;

- List<X509Certificate> certChain;

- RsaKey cachedKey;

- EcKey cachedEcKey;

- bool clientAdvertised0805;

- bool clientAdvertised0403;

- static int MaxServerChainCerts=8;

- static int MaxServerChainDerBytes=131072;

- int state;

- string lastError;

- long verifyResult;

- string peerCertText;

- int osTrustDiag;

- X509Certificate peerCert;

- List<X509Certificate> peerChain;

- bool peerAuthenticated;

- bool handshakeSignatureVerified;

- bool receivedEncryptedExtensions;

- int preAuthHandshakeBytes;

- int negotiatedVersion;

- int negotiatedCipherSuite;

- byte[]clientRandom;

- byte[]serverRandom;

- byte[]sessionId;

- byte[]clientX25519Priv;

- byte[]clientX25519Pub;

- byte[]serverX25519Pub;

- byte[]handshakeSecret;

- byte[]clientHsTraffic;

- byte[]serverHsTraffic;

- byte[]clientAppTrafficSecret;

- byte[]clientAppKeyPending;

- byte[]clientAppIvPending;

- byte[]tls12MasterSecret;

- byte[]transcriptHashBeforeFinished;

- byte[]tls12ClientWriteKey;

- byte[]tls12ClientWriteIv;

- byte[]tls12ServerWriteKey;

- byte[]tls12ServerWriteIv;

- TlsTranscript transcript;

- TlsCipherState readCipher;

- TlsCipherState writeCipher;

- TlsByteWriter inNet;

- TlsByteWriter outNet;

- TlsByteWriter appReadBuf;

- TlsByteWriter handshakeBuf;

- byte[]recHeader;

- List<int> recType;

- TlsEngine(string host, bool disableVerify, bool disableTls13, List<string> pins, bool pinRequired, CertPolicyFn certPolicy, List<X509Certificate> trustedCerts, bool isServer, string certFile, string keyFile)

- void SetCredentials(X509Certificate cert, RsaKey key, EcKey ecKey)

- void SetCertChain(List<X509Certificate> chain)
  - 服务端证书链：叶子在前、中间证书随后。链中每张 DER 都
    有效且总长受限；为空或不合法时保留 SetCredentials 的单证书回退。

- List<X509Certificate> SendChain()
  - 待发送的服务端证书列表（叶子在前）；无链时退回单证书。

- int OutNetLength()

- byte[]OutNetBuffer()

- int OutNetHead()

- void DropOutNet(int count)

- bool IsConnected()

- bool IsClosed()

- string LastError()

- long VerifyResult()

- int OsTrustDiag()
  - Windows 系统信任桥接的诊断码（1=信任，0=输入/环境拒绝，负值=失败分类）

- string PeerCertificateText()

- X509Certificate PeerCertificate()

- int NegotiatedVersion()

- static byte[]RandomBytes(int n)

- byte[]DeriveX25519Shared(byte[]privateKey, byte[]peerPublic, string error)

- static byte[]GenerateX25519(out byte[]publicKey)

- void StartHandshake()
  - 启动 TLS 握手。客户端产出 ClientHello 记录，服务端置待接收状态。

- void FeedNetwork(byte[]data, int offset, int len)
  - 向引擎灌入网络层读到的原始数据字节。

- byte[]DrainNetwork()
  - 从引擎取出待发送至网络的加密数据字节。

- int Step()
  - 驱动握手与记录处理状态机前进。返回 1 表示握手已完成，0 表示需继续从网络读入数据，-1 表示出错。

- int ProcessRecord(int ctype, byte[]header, byte[]inBuf, int pOffset, int pLen)

- int ProcessHandshakePayload(byte[]data)

- int ProcessOneHandshakeMessage(int msgType, byte[]fullMsg, byte[]body)

- int HandleServerHello(byte[]fullMsg, byte[]body)

- int HandleCertificate(byte[]fullMsg, byte[]body)

- bool WindowsTrustedPeer()

- int VerifyCertificate()

- int HandleCertificateVerify(byte[]fullMsg, byte[]body)

- int HandleFinished(byte[]fullMsg, byte[]body)

- int HandleFinishedTls13(byte[]fullMsg, byte[]body)

- int HandleServerKeyExchange(byte[]fullMsg, byte[]body)

- int HandleServerHelloDone(byte[]fullMsg, byte[]body)

- int HandleFinishedTls12(byte[]fullMsg, byte[]body)

- int HandleClientHello(byte[]fullMsg, byte[]body)

- int ServerStartTls13(byte[]clientShare)

- int ServerStartTls12()

- int HandleClientKeyExchange(byte[]fullMsg, byte[]body)

- int HandleServerFinished(byte[]fullMsg, byte[]body)

- int SendAppData(nint dataPtr, int len)
  - 加密应用数据指针并零拷贝直写到待发网络缓冲区，按 16KB 分片封装为 TLS 记录。

- int SendAppData(byte[]data, int offset, int len)
  - 加密应用数据字节数组并放入待发网络缓冲区。

- int SendAppData(string data, int offset, int len)
  - 加密应用数据字符串并放入待发网络缓冲区。

- int RecvAppData(byte[]outBuf, int offset, int maxLen)
  - 从已解密的应用缓冲区提取明文数据到字节数组。

- int RecvAppData(string outBuf, int offset, int maxLen)
  - 从已解密的应用缓冲区提取明文数据到原生字符串/内存切片。

- int AvailableAppData()

- void Fail(string msg)

- byte[]BuildClientHello()


## TlsRecord (class)

TLS 记录层协议常量。

- static int ContentTypeChangeCipherSpec=20;

- static int ContentTypeAlert=21;

- static int ContentTypeHandshake=22;

- static int ContentTypeApplicationData=23;

- static int VersionTls10=769;

- static int VersionTls11=770;

- static int VersionTls12=771;

- static int VersionTls13=772;

- static int HandshakeHelloRequest=0;

- static int HandshakeClientHello=1;

- static int HandshakeServerHello=2;

- static int HandshakeNewSessionTicket=4;

- static int HandshakeEndOfEarlyData=5;

- static int HandshakeEncryptedExtensions=8;

- static int HandshakeCertificate=11;

- static int HandshakeServerKeyExchange=12;

- static int HandshakeCertificateRequest=13;

- static int HandshakeServerHelloDone=14;

- static int HandshakeCertificateVerify=15;

- static int HandshakeClientKeyExchange=16;

- static int HandshakeFinished=20;

- static int HandshakeKeyUpdate=24;

- static int AlertLevelWarning=1;

- static int AlertLevelFatal=2;

- static int AlertCloseNotify=0;

- static int AlertUnexpectedMessage=10;

- static int AlertBadRecordMac=20;

- static int AlertHandshakeFailure=40;

- static int AlertBadCertificate=42;

- static int AlertIllegalParameter=47;

- static int AlertDecodeError=50;

- static int AlertDecryptError=51;

- static int AlertInternalError=80;

- static int AlertUnrecognizedName=112;

- static byte[]FormatPlaintext(int contentType, int version, byte[]data, int offset, int len)
  - 构造明文 TLS 记录包（用于握手未加密阶段）。

- static void WriteSeq64(byte[]buf, int offset, long seq)
  - 将 64 位无符号序列号按大端写入缓冲区。


## TlsStream (class)

非阻塞套接字上的纯 Zan 原生 TLS 1.2 / TLS 1.3 流。
原生 AES-128-GCM、X25519、HKDF 与 X.509 握手；Windows 客户端另用 Crypt32 SSL 链策略。

- [DllImport("crt", EntryPoint="zan_monotonic_us")]static extern long MonotonicUs();

- TlsEngine engine;

- nint sock;

- bool open;

- HttpDeadlineToken watch;

- byte[]scratch;

- static int SCRATCH=65536;

- byte[]pendingOut;

- int pendingLen;

- static string lastError="";

- static int Norm(int v)

- static string LastError()

- static void SetLastError(string error)

- static bool ValidReceiveRange(int bufferLength, int offset, int max)

- TlsStream()

- static TlsStream Setup(TlsContext ctx, nint sock, string host)

- static async TlsStream AcceptAsync(TlsContext ctx, nint sock)
  - 服务器端：接受客户端连接并执行 TLS 握手。

- static async TlsStream AcceptAsync(TlsContext ctx, nint sock, int timeoutMs)

- static async TlsStream ConnectAsync(TlsContext ctx, nint sock, string host)
  - 客户端：连接对端服务器并执行 TLS 握手。

- static async TlsStream ConnectAsync(TlsContext ctx, nint sock, string host, int timeoutMs)

- static TlsStream ConnectStepped(TlsContext ctx, nint sock, string host)

- static TlsStream AcceptStepped(TlsContext ctx, nint sock)

- static TlsStream SetupStepped(TlsContext ctx, nint sock, string host)

- int HandshakePump()

- string HandshakeDrain()

- void HandshakeFeed(string data)

- void EnsurePending(int extra)

- string PeerCertificateText()
  - 获取当前连接对端证书的描述文本（形如 issuer=... subject=...）。

- static string SpkiPinOf(TlsStream stream)

- async int FlushOutAsync()

- async int PumpInAsync()

- async int HandshakeAsync(int timeoutMs)

- int EndHandshakeWatch(int result)

- async string RecvAsync(int max)
  - 接收解密后的应用字节（最多 max）。正常关闭或出错时返回空串。

- async int RecvIntoAsync(string buf, int max)
  - 接收解密后的字节到调用方提供的缓冲区。正常关闭返回 0，出错返回 -1。

- async int RecvBytesAsync(byte[]buf, int offset, int max)
  - 接收解密后的字节到调用方提供的字节数组。正常关闭返回 0，出错返回 -1。

- async int SendAsync(string data, int len)
  - 加密并发送 len 字节字符串数据。零中间分配直达底层网络缓冲。成功返回明文字节数，失败返回 -1。

- async int SendAsync(byte[]data, int len)
  - 加密并发送字节数组前 len 字节。零中间分配直达底层网络缓冲。成功返回明文字节数，失败返回 -1。

- async int SendBytesAsync(byte[]data, int len)
  - 加密并发送字节数组前 len 字节。零中间分配直达底层网络缓冲。成功返回明文字节数，失败返回 -1。

- async int SendBytesAsync(byte[]data, int offset, int len)
  - 加密并发送字节数组切片。零中间分配直达底层网络缓冲。成功返回明文字节数，失败返回 -1。

- async int SendBytesAsync(byte[]data)
  - 加密并发送完整字节数组。

- async int SendStringAsync(string data)
  - 发送整个字符串（其 .Length 个字节）。

- void Close()
  - 关闭 TLS 连接。


## TlsTranscript (class)

TLS 握手消息转录摘要（Transcript Hash），记录自 ClientHello 起所有握手层消息。

- TlsByteWriter writer;

- TlsTranscript()

- void Update(byte[]msg, int offset, int len)

- void Update(byte[]msg)

- byte[]CurrentHash()


## bool (delegate)

证书策略委托：客户端握手后若链/主机名校验不通过（verifyResult != 0），
以 (verifyResult, 对端证书文本) 调用；返回 true 放行本次连接，false 拒绝。
证书文本形如 "issuer=/CN=.../O=... subject=/CN=example.com"。

`delegate bool CertPolicyFn(long verifyResult, string certText);`
