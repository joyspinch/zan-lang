# System.Net.WebSocket

> 源码: `stdlib/System/Net/WebSocket/WebSocket.zan`


## WebSocketClient (class)

WebSocket 客户端。

- TcpClient conn;

- bool connected;

- string host;

- int port;

- string path;

- WebSocketClient()

- static async WebSocketClient ConnectAsync(string host, int port, string path)

- async void SendText(string text)

- async void SendBinary(string data, int len)

- async void Ping()

- async string RecvText()

- async void Close()

- bool IsConnected()


## WebSocketServer (class)

WebSocket 服务器。

- TcpListener listener;

- string host;

- int port;

- bool running;

- int activeConnections;

- WebSocketServer(string host, int port)

- async void Start()

- async void HandleConnection(nint clientSock)

- async string OnMessage(string message)

- void Stop()


## WsFrame (class)

WebSocket 帧构建与解析器（RFC 6455）。

- int opcode;

- string payload;

- int payloadLen;

- bool fin;

- bool masked;

- WsFrame()

- static WsFrame TextFrame(string data)

- static WsFrame BinaryFrame(string data, int len)

- static WsFrame CloseFrame()

- static WsFrame PingFrame()

- static WsFrame PongFrame()

- byte[]Encode()

- static WsFrame Decode(string data, int dataLen)

- static WsFrame DecodeAt(string data, int offset0, int frameLen)
  - 解码从 `offset` 开始的帧；`frameLen` 为其
    总字节长度。原地读取让读取方能在同一缓冲区中持有多个管道帧，
    而不必在每帧之后搬移剩余数据。


## WsOpcode (class)

WebSocket 帧操作码（opcode）。

- static int Continuation=0;

- static int Text=1;

- static int Binary=2;

- static int Close=8;

- static int Ping=9;

- static int Pong=10;


## WsReader (class)

带缓冲、二进制安全的 WebSocket 帧读取器。

修复了朴素的“一次 recv == 一整
帧”模型中的两个正确性/吞吐量缺陷：
1. 大于单次 recv 的帧（或被 TCP 分段拆开的帧）会
通过不断累积字节、直到声明的帧长度
全部到达才完成重组。
2. 一次 recv 收到多帧（管道化/合并发送）时，
逐帧取出处理，而不是只解码第一帧。

它还避免了基于字符串 recv 的 NUL 截断风险：字节由 <c>Socket.Recv</c>
接收（它返回真实字节数），
有效长度显式记录在 <c>len</c> 中，绝不用 <c>strlen</c>。

- nint sock;

- byte[]buf;

- int len;

- int cap;

- byte[]tmp;

- int start;

- WsReader(nint sock)

- void Compact()
  - 把未读字节移到开头，使游标归零。

- void Prime(string data, int n)
  - 用已从套接字读到的字节初始化缓冲区。
    同时服务 HTTP 与 WebSocket 的端口会自行读取握手，可能连带读入
    首批帧，这些字节不能丢失。

- void Ensure(int need)
  - 扩展累积缓冲区，使其至少容纳 `need` 个字节。

- async int FillMore()
  - 挂起在 reactor 上直到可读，然后把收到的字节
    追加进缓冲区。返回读取的字节数；0 表示对端已关闭（EOF）。

- int FrameSize()
  - 缓冲区头部帧的总字节长度；若完整帧尚未全部缓冲
    则返回 -1。

- WsFrame TakeFrame(int total)
  - 解码游标处的帧（长度由 FrameSize 给出），并
    越过该帧。

- void Append(string data, int n)
  - 把外部收到的字节（例如 TLS 流解密出的明文）
    追加到累积缓冲区，绕过套接字。

- void Consume(int total)
  - 跳过游标处的帧以丢弃它。


## WsWriter (class)

供连续应答帧使用的二进制安全输出缓冲区。

每帧一次 send() 比编码一帧更贵：每 recv 携带 64 个管道帧时，
echo 路径把时间全花在系统调用上（满载内核约 13 万消息/秒，
而 tcp 路径一次写入回显整个 recv，达 680 万/秒）。
在更多完整帧已缓冲时累积响应，
待读取方无帧可读时再一次性刷出。

- byte[]buf;

- int len;

- int cap;

- WsWriter()

- void Ensure(int need)

- void Append(string data, int n)
  - 追加编码后帧的 `n` 个字节。

- bool Pending()

- async int Flush(TcpClient client)
  - 把缓冲内容作为一次 send 全部写出并重置。
