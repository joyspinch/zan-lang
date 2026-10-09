# System.Net.Tls

> 源码: `packages/Zan.Net/src/System/Net/Tls/TlsHandshake.zan`, `packages/Zan.Net/src/System/Net/Tls/TlsRecord.zan`, `packages/Zan.Net/src/System/Net/Tls/TlsStream.zan`


## TlsByteReader (class)

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

- byte[]Encrypt(int contentType, byte[]data, int offset, int len)

- byte[]Decrypt(byte[]header, byte[]payload, int pOffset, int pLen, List<int> outContentType)

- int EncryptTls12ToBuf(int contentType, nint dataPtr, int len, byte[]outBuf, int outOffset)

- byte[]DecryptTls12(byte[]header, byte[]payload, int pOffset, int pLen, List<int> outContentType)

- int EncryptTls13ToBuf(int contentType, nint dataPtr, int len, byte[]outBuf, int outOffset)

- byte[]DecryptTls13(byte[]header, byte[]payload, int pOffset, int pLen, List<int> outContentType)


## TlsContext (class)

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

- static List<X509Certificate> ReadCertChainFile(string path)

- static List<X509Certificate> TrustFromCandidates(string[]paths)

- bool AddTrustedCert(string pemFile)

- static List<X509Certificate> SystemTrustRoots()

- void SetCertPolicy(CertPolicyFn fn)

- bool PolicyActive()

- void AddPin(string spkiSha256Base64)

- void RequirePinning(bool on)

- bool PinningActive()

- bool PinMatches(string got)

- static TlsContext CreateServer(string certFile, string keyFile)

- static TlsContext CreateClient()

- static TlsContext CreateClientWithCertificate(string certFile, string keyFile)

- void DisableVerify()

- void DisableTls13()

- string Handle()

- bool IsServer()

- string TrustHint()

- void Free()


## TlsEngine (class)

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

- List<X509Certificate> SendChain()

- int OutNetLength()

- byte[]OutNetBuffer()

- int OutNetHead()

- void DropOutNet(int count)

- bool IsConnected()

- bool IsClosed()

- string LastError()

- long VerifyResult()

- int OsTrustDiag()

- string PeerCertificateText()

- X509Certificate PeerCertificate()

- int NegotiatedVersion()

- static byte[]RandomBytes(int n)

- byte[]DeriveX25519Shared(byte[]privateKey, byte[]peerPublic, string error)

- static byte[]GenerateX25519(out byte[]publicKey)

- void StartHandshake()

- void FeedNetwork(byte[]data, int offset, int len)

- byte[]DrainNetwork()

- int Step()

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

- int SendAppData(byte[]data, int offset, int len)

- int SendAppData(string data, int offset, int len)

- int RecvAppData(byte[]outBuf, int offset, int maxLen)

- int RecvAppData(string outBuf, int offset, int maxLen)

- int AvailableAppData()

- void Fail(string msg)

- byte[]BuildClientHello()


## TlsRecord (class)

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

- static void WriteSeq64(byte[]buf, int offset, long seq)


## TlsStream (class)

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

- static async TlsStream AcceptAsync(TlsContext ctx, nint sock, int timeoutMs)

- static async TlsStream ConnectAsync(TlsContext ctx, nint sock, string host)

- static async TlsStream ConnectAsync(TlsContext ctx, nint sock, string host, int timeoutMs)

- static TlsStream ConnectStepped(TlsContext ctx, nint sock, string host)

- static TlsStream AcceptStepped(TlsContext ctx, nint sock)

- static TlsStream SetupStepped(TlsContext ctx, nint sock, string host)

- int HandshakePump()

- string HandshakeDrain()

- void HandshakeFeed(string data)

- void EnsurePending(int extra)

- string PeerCertificateText()

- static string SpkiPinOf(TlsStream stream)

- async int FlushOutAsync()

- async int PumpInAsync()

- async int HandshakeAsync(int timeoutMs)

- int EndHandshakeWatch(int result)

- async string RecvAsync(int max)

- async int RecvIntoAsync(string buf, int max)

- async int RecvBytesAsync(byte[]buf, int offset, int max)

- async int SendAsync(string data, int len)

- async int SendAsync(byte[]data, int len)

- async int SendBytesAsync(byte[]data, int len)

- async int SendBytesAsync(byte[]data, int offset, int len)

- async int SendBytesAsync(byte[]data)

- async int SendStringAsync(string data)

- void Close()


## TlsTranscript (class)

- TlsByteWriter writer;

- TlsTranscript()

- void Update(byte[]msg, int offset, int len)

- void Update(byte[]msg)

- byte[]CurrentHash()


## bool (delegate)

`delegate bool CertPolicyFn(long verifyResult, string certText);`
