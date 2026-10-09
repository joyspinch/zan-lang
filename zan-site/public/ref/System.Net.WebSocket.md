# System.Net.WebSocket

> 源码: `packages/Zan.Net/src/System/Net/WebSocket/WebSocket.zan`, `packages/Zan.Net/src/System/Net/WebSocket/WsSharedBus.zan`


## WebSocketClient (class)

- TcpClient conn;

- bool connected;

- string host;

- int port;

- string path;

- WsReader reader;

- WsAssembler asm;

- bool sendBusy;

- List<AsyncGate> sendWaiters;

- int lastOpcode;

- int lastLen;

- byte[]lastBinary;

- WebSocketClient()

- static async WebSocketClient ConnectAsync(string host, int port, string path)

- static async WebSocketClient ConnectAsyncHeaders(string host, int port, string path, List<string> headers)

- async void AcquireSend()

- void ReleaseSend()

- async bool SendFrame(int opcode, string payload, int payloadLen)

- async bool SendFrameBytes(int opcode, byte[]payload, int offset, int payloadLen)

- async void SendText(string text)

- async void SendBinary(string data, int len)

- async void SendBinary(byte[]data, int offset, int len)

- async void SendBinary(byte[]data)

- async void Ping()

- int LastOpcode()

- int LastLength()

- async string RecvText()

- async string RecvText(int timeoutMs)

- async byte[]RecvBinary()

- async byte[]RecvBinary(int timeoutMs)

- void CloseNow()

- async void Close()

- bool IsConnected()


## WsAssembler (class)

- int opcode;

- int dataLen;

- int maxLen;

- byte[]dataBuf;

- int bufCap;

- WsAssembler(int maxLen)

- int Feed(WsFrame frame)

- bool ValidComplete()

- string Complete()

- byte[]CompleteBytes()

- int MessageOpcode()

- int MessageLen()


## WsFrame (class)

- int opcode;

- string payload;

- byte[]payloadBytes;

- int payloadOffset;

- int payloadLen;

- bool fin;

- bool masked;

- int rsv;

- WsFrame()

- static WsFrame TextFrame(string data)

- static WsFrame BinaryFrame(string data, int len)

- static WsFrame CloseFrame()

- static WsFrame CloseFrameWithCode(int code)

- static WsFrame PingFrame()

- static WsFrame PongFrame()

- static WsFrame RawFrame(int opcode, string payload, int payloadLen)

- static WsFrame RawBytes(int opcode, byte[]payload, int offset, int payloadLen)

- static int HeaderLen(int payloadLen)

- byte[]Encode()

- byte[]EncodeMasked()

- static WsFrame DecodeAt(string data, int offset0, int frameLen)

- static bool ValidInbound(WsFrame frame, bool requireMasked)

- static bool ValidControlPayload(WsFrame frame)

- static bool ValidUtf8(string text, int len)


## WsHandshake (class)

- static bool HasToken(string value, string token)

- static bool Valid(HttpRequest req)

- static bool SafeRequestPart(string value, bool allowEmpty)

- static bool ValidResponse(string head, string expectedAccept)


## WsOpcode (class)

- static const int Continuation=0;

- static const int Text=1;

- static const int Binary=2;

- static const int Close=8;

- static const int Ping=9;

- static const int Pong=10;


## WsReader (class)

- nint sock;

- TlsStream tlsSrc;

- byte[]buf;

- int len;

- int cap;

- byte[]tmp;

- int start;

- int maxLen;

- WsReader(nint sock)

- static WsReader WrapTls(TlsStream stream)

- void SetMaxLen(int max)

- int MaxLen()

- int Buffered()

- void Compact()

- void Prime(string data, int n)

- void Ensure(int need)

- async int FillMore()

- async int FillMore(int timeoutMs)

- int FrameSize()

- WsFrame TakeFrame(int total)

- int FindHeaderEnd()

- string Slice(int n)

- void Append(string data, int n)

- void Consume(int total)


## WsSharedBus (class)

- SharedTable busTable;

- string name;

- int ringCapacity;

- long lastReadSeq;

- bool running;

- int pid;

- long busNonce;

- static long nonceCounter=0;

- public WsSharedBus(string name, int ringCapacity)

- void InitTable()

- public bool IsAvailable()

- public void PublishBroadcast(string payload, int payloadLen)

- public void PublishChannel(string channel, string payload, int payloadLen)

- public void PublishDirect(int targetId, string payload, int payloadLen)

- void PublishInternal(int targetType, string channel, int targetId, string payload, int payloadLen)

- public void OnConnChange(int delta)

- public int GetTotalConnCount()

- public void StartPolling(IWsSharedBusConsumer consumer)

- private async void PollLoop(IWsSharedBusConsumer consumer)

- public void Stop()


## WsWriter (class)

- byte[]buf;

- int len;

- int cap;

- WsWriter()

- void Ensure(int need)

- void Append(string data, int n)

- bool Pending()

- async int Flush(TcpClient client)


## IWsSharedBusConsumer (interface)

- void OnSharedBusMessage(int targetType, string channel, int targetId, string payload, int payloadLen);
