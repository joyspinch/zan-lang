# System.Net.WebSocket.Secure

> 源码: `packages/Zan.Net/src/System/Net/WebSocket/Secure/WssClient.zan`, `packages/Zan.Net/src/System/Net/WebSocket/Secure/WssServer.zan`


## WssClient (class)

- TcpClient conn;

- TlsContext context;

- TlsStream stream;

- string host;

- string path;

- WsReader reader;

- WsAssembler asm;

- bool connected;

- bool sendBusy;

- List<AsyncGate> sendWaiters;

- int lastOpcode;

- int lastLen;

- byte[]lastBinary;

- WssClient()

- static async WssClient ConnectAsync(string host, int port, string path)

- bool IsConnected()

- async void SendText(string text)

- async void SendBinary(string bytes, int len)

- async void SendBinary(byte[]bytes, int offset, int len)

- async void SendBinary(byte[]bytes)

- async void Ping()

- int LastOpcode()

- int LastLength()

- async string RecvText()

- async byte[]RecvBinary()

- async void Close()

- async void AcquireSend()

- void ReleaseSend()

- async void SendFrame(int opcode, string payload, int payloadLen)

- async void SendFrameBytes(int opcode, byte[]payload, int offset, int payloadLen)

- void CloseNow()

- static string Header(string headers, string name)

- static bool AsciiEquals(string a, string b)

- static int IndexOf(string hay, string needle, int from)


## WssServer (class)

- TcpListener listener;

- TlsContext tls;

- string host;

- int port;

- bool running;

- int activeConnections;

- int maxConnections;

- int requestTimeoutMs;

- WssMessageHandler messageHandler;

- WssServer(string host, int port)

- static WssServer Create(string host, int port, string certFile, string keyFile)

- WssServer OnMessage(WssMessageHandler handler)

- WssServer SetMaxConnections(int max)

- WssServer SetTimeout(int ms)

- async void Start()

- void Stop()

- async void HandleConnection(nint clientSock)

- async void HandleConnectionInner(nint clientSock)

- async string OnMessageInternal(string message)

- async string OnMessage(string message)


## string (delegate)

`delegate string WssMessageHandler(string message);`
