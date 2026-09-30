# System.Net.WebSocket

> 源码: `packages/Zan.Net/src/System/Net/WebSocket/WebSocket.zan`, `packages/Zan.Net/src/System/Net/WebSocket/WsSharedBus.zan`


## WebSocketClient (class)

WebSocket 客户端。

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
  - 内部构造：未连接；经 ConnectAsync 使用。

- static async WebSocketClient ConnectAsync(string host, int port, string path)
  - 发起 ws://host:port/path 握手（随机 Sec-WebSocket-Key，校验
    Sec-WebSocket-Accept）。绝不返回 null：连接/握手失败时返回
    一个 connected=false 的对象，用 IsConnected() 判断成败（失败
    路径已顺手关闭底层连接）。

- static async WebSocketClient ConnectAsyncHeaders(string host, int port, string path, List<string> headers)
  - 带自定义握手头的连接。`headers` 里每一项是一个完整的
    头行（如 "Authorization: Bearer xxx"），原样追加在标准握手头
    之后——授权在升级前生效的 MVC 路由（needsAuth 检查先于
    WsUpgrade）没有它就永远 401。行内出现 CR/LF/NUL/DEL 一律
    拒绝并按握手失败返回（头注入会伪造额外头/截断握手块），
    其余字节不校验——值里写什么由调用方负责。

- async void AcquireSend()

- void ReleaseSend()

- async bool SendFrame(int opcode, string payload, int payloadLen)
  - 客户端方向统一发送入口：掩码 + 正确的帧长 + 协程互斥串行发送，
    彻底消除心跳与并发业务数据帧交叉交错损坏。返回 false 表示连接已不可用。

- async bool SendFrameBytes(int opcode, byte[]payload, int offset, int payloadLen)
  - 客户端方向原生字节数组发送入口：掩码 + 帧长 + 协程互斥串行发送。

- async void SendText(string text)
  - 发送一条 Text 消息（fire-and-forget，失败仅置 disconnected）。

- async void SendBinary(string data, int len)
  - 发送一条二进制消息（data 可含 NUL，len 为精确字节数）。

- async void SendBinary(byte[]data, int offset, int len)
  - 发送一条二进制消息（byte[] 原生切片，无需经过字符串中转）。

- async void SendBinary(byte[]data)
  - 发送一条完整二进制消息。

- async void Ping()
  - 发送空 Ping 帧（对端的 Pong 由 RecvText 静默消费）。

- int LastOpcode()
  - 上一次成功接收并交付的消息操作码（WsOpcode.Text 或 WsOpcode.Binary）。

- int LastLength()
  - 上一次成功接收并交付的消息精确字节数。

- async string RecvText()
  - 挂起直到下一个文本/二进制消息。Ping 以 Pong 应答后
    继续循环；分片消息重组到 fin 后整条交付；Close 或断开
    返回 ""。

- async string RecvText(int timeoutMs)
  - 带空闲窗口的 RecvText：时限内没有任何帧到达时返回 ""
    ——连接未断，IsConnected() 仍为 true，已收到的半截帧留在
    缓冲区里下次接着读；这正是心跳调用方的判据（超时 → Ping()
    → 继续 RecvText(timeout)）。真正的断开（对端关闭帧/EOF/协议
    错误）会 CloseNow，IsConnected() 变 false。timeoutMs 非正表示
    不限时。空数据消息与空闲超时都返回 ""，无法区分——协议上
    不发空消息（发心跳用 Ping）就没有歧义。

- async byte[]RecvBinary()
  - 挂起直到下一个文本/二进制消息。返回未截断的原生 byte[]，
    彻底消除二进制消息中 NUL 字符被字符串截断的隐患；Close 或断开返回 null。

- async byte[]RecvBinary(int timeoutMs)
  - 带空闲窗口的原生二进制消息接收。

- void CloseNow()
  - 立即断开（不发关闭帧）；RecvText 之后返回 ""。

- async void Close()
  - 协礼关闭：先发空关闭帧再断开。

- bool IsConnected()
  - 握手是否成功且连接未断。


## WsAssembler (class)

RFC 6455 §5.4 分片消息重组器：fin=0 的数据帧开始一条消息，
opcode 0 的 continuation 帧追加负载，fin=1 交付。此前三条
服务器/客户端路径都把 continuation 帧静默丢弃——大消息被
发送方分片后内容直接丢失且无任何报错。

负载按显式长度累积（帧字节可能含 NUL），累计超过 maxLen
时返回 -2，由调用方按 1009 关闭连接。

- int opcode;

- int dataLen;

- int maxLen;

- byte[]dataBuf;

- int bufCap;

- WsAssembler(int maxLen)
  - 内部构造：状态复位，消息上限由调用方给定；经 WebSocketClient
    / 服务器各路径使用。

- int Feed(WsFrame frame)
  - 喂入一个已解码的完整数据帧（控制帧不进这里）。
    返回：0 = 分片进行中；1 = 消息完整，用 Message/MessageLen/
    MessageOpcode 取出；-1 = 协议违规（无起始帧的 continuation
    或分片中途插入新数据帧）；-2 = 累计超过 maxLen。

- bool ValidComplete()
  - 完成消息前校验文本消息的 UTF-8；二进制消息不做编码约束。

- string Complete()
  - 取出重组完成的消息并复位状态。按显式长度一次转换，
    内嵌 NUL 仍是消息的一部分；保留缓冲容量供下一条消息复用。

- byte[]CompleteBytes()
  - 取出重组完成的消息原始字节并复位状态。

- int MessageOpcode()
  - 重组完成消息的 opcode（Text 或 Binary）。

- int MessageLen()
  - 重组负载的字节数（Complete 复位后为 0）。


## WsFrame (class)

WebSocket 帧构建与解析器（RFC 6455）。

- int opcode;

- string payload;

- byte[]payloadBytes;

- int payloadOffset;

- int payloadLen;

- bool fin;

- bool masked;

- int rsv;

- WsFrame()
  - 内部构造：Text 帧、fin、不掩码；字段由各工厂方法填。

- static WsFrame TextFrame(string data)
  - 载荷为整串的 Text 帧。

- static WsFrame BinaryFrame(string data, int len)
  - 二进制帧：data 可含 NUL，len 为精确字节数。

- static WsFrame CloseFrame()
  - 不带状态码的空关闭帧。

- static WsFrame CloseFrameWithCode(int code)
  - 带状态码的关闭帧（RFC 6455 §7.1.5 的 2 字节
    负载）。协议错误 1002、消息过大 1009 等场景下应告知
    对端原因，而不是发空关闭帧。

- static WsFrame PingFrame()
  - 空 Ping 帧。

- static WsFrame PongFrame()
  - 空 Pong 帧。

- static WsFrame RawFrame(int opcode, string payload, int payloadLen)
  - 任意 opcode 的出站帧（客户端统一发送入口用）。

- static WsFrame RawBytes(int opcode, byte[]payload, int offset, int payloadLen)
  - 任意 opcode 的原生字节数组出站帧（客户端统一发送入口用）。

- static int HeaderLen(int payloadLen)
  - 帧头字节数（RFC 6455 长度编码）。发送方据它
    计算真实写出长度——此前发送方硬编码 2，载荷超过 125 字节时
    帧头被截断、帧流从此错位。

- byte[]Encode()
  - 服务端方向编码：FIN + opcode + RFC 6455 长度编码 + 载荷
    （不掩码；客户端须用 EncodeMasked）。

- byte[]EncodeMasked()
  - 客户端方向的帧：RFC 6455 §5.1 要求浏览器之外的客户端
    也必须掩码，合规的服务器与代理会直接拒绝未掩码帧。每帧用
    CSPRNG 掩码键（与 WssClient 相同的方案）。

- static WsFrame DecodeAt(string data, int offset0, int frameLen)
  - 解码从 `offset` 开始的帧；`frameLen` 为其
    总字节长度。原地读取让读取方能在同一缓冲区中持有多个管道帧，
    而不必在每帧之后搬移剩余数据。
    
    所有扩展长度/掩码读取都先对照 `frameLen` 判界，64 位长度
    的高 32 位必须为零（RFC 6455 要求 MSB=0 且连接失败）——
    此前截断帧会读到越界字节、声明超长载荷会负向 Substring，
    都是 rt_crash 边界陷阱级的进程死亡。违规时返回 opcode=Close
    的空帧，调用方按关闭处理。
    
    调用方必须保证底层缓冲覆盖 `offset0 + frameLen` 字节——
    WsReader.FrameSize 返回正数时恰好给出这一保证。这里的
    缓冲实参是按 string 传入的 byte[]：其 `.Length` 退化为
    strlen，而帧字节里随时可能出现 NUL（continuation 帧的
    opcode 就是 0），因此绝不能拿 `data.Length` 当边界用——
    旧的按 strlen 钳制会提前截断 dataEnd，让解码器把完整帧
    误判为空帧并静默丢弃。

- static bool ValidInbound(WsFrame frame, bool requireMasked)
  - RFC 6455 入站帧门禁。服务端要求客户端帧带掩码；客户端要求
    服务端帧不带掩码。未协商扩展时 RSV 必须为零，控制帧必须完整
    且不超过 125 字节。

- static bool ValidControlPayload(WsFrame frame)

- static bool ValidUtf8(string text, int len)
  - 严格校验一段显式长度的 UTF-8。过短、过长、代理项和超范围码点
    均拒绝；调用方只在完整消息/关闭原因上调用，避免拆分码点被误判。


## WsHandshake (class)

Shared server-side RFC 6455 upgrade gate for plain and TLS ports.

- static bool HasToken(string value, string token)

- static bool Valid(HttpRequest req)

- static bool SafeRequestPart(string value, bool allowEmpty)

- static bool ValidResponse(string head, string expectedAccept)


## WsOpcode (class)

WebSocket 帧操作码（opcode）。

- static const int Continuation=0;
  - 分片消息的后续帧（RFC 6455 §5.4）。

- static const int Text=1;
  - UTF-8 文本消息。

- static const int Binary=2;
  - 二进制消息。

- static const int Close=8;
  - 连接关闭（含 2 字节状态码负载）。

- static const int Ping=9;
  - 心跳探测（负载原样由 Pong 回）。

- static const int Pong=10;
  - 心跳应答。


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

- TlsStream tlsSrc;

- byte[]buf;

- int len;

- int cap;

- byte[]tmp;

- int start;

- int maxLen;

- WsReader(nint sock)
  - 内部构造：64KiB 缓冲、消息上限 16 MiB；经 new / WrapTls 使用。

- static WsReader WrapTls(TlsStream stream)
  - 以 TLS 明文流为字节源的读取器（wss 连接）。

- void SetMaxLen(int max)
  - 调整单条消息的缓冲上限（字节）。

- int MaxLen()
  - 单条消息的缓冲上限（字节）。

- int Buffered()
  - 缓冲中未读字节数。

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

- async int FillMore(int timeoutMs)
  - 带空闲窗口的 FillMore：时限内没有任何字节到达时返回
    -1（连接未断，帧缓冲里已收到的半截字节原样保留，下次接着
    读）；数据/对端关闭的语义与 FillMore 相同（>0 = 字节数，
    0 = EOF）。timeoutMs 非正表示不限时，等价于 FillMore()。
    
    实现走 Socket.RecvToOv——把接收与截止时间一起交给 reactor
    竞速（超时交付 -1，不 shutdown 套接字），而不是 HttpDeadline
    的手法：后者靠 ShutdownBoth 挂断唤醒，用一次连接就废一次，
    撑不起「每 15 秒空闲窗口到期就 Ping 一下再继续收」的长连接
    心跳循环。TLS 源分支没有对等的带截止时间原语，退化为不限时
    （wss 心跳调用方应改用上层 Ping/Pong 探活）。

- int FrameSize()
  - 缓冲区头部帧的总字节长度；若完整帧尚未全部缓冲
    则返回 -1。返回 -2 = 协议违规（64 位长度的高 32 位非零，
    RFC 要求连接失败）；-3 = 帧总长超过 maxLen（消息过大）。
    调用方对 -2/-3 都必须关闭而非继续等待。

- WsFrame TakeFrame(int total)
  - 解码游标处的帧（长度由 FrameSize 给出），并
    越过该帧。

- int FindHeaderEnd()
  - 在缓冲的未读字节里扫描 HTTP 头部结束标记
    （CRLFCRLF）。借助 AVX2 向量化 IndexOf 快速跳过普通字节。
    返回相对游标的偏移；未找到返回 -1。
    握手与首帧常在同一段到达，而帧字节可能含 NUL，
    因此扫描必须按字节下标进行，不能走字符串长度。

- string Slice(int n)
  - 取出游标处 n 个字节，作为显式长度的字符串
    （不推进游标）。零额外堆分配拷贝。

- void Append(string data, int n)
  - 把外部收到的字节（例如 TLS 流解密出的明文）
    追加到累积缓冲区，绕过套接字。

- void Consume(int total)
  - 跳过游标处的帧以丢弃它。


## WsSharedBus (class)

基于 SharedTable 共享内存环形缓冲区的跨进程 WebSocket/长连接消息总线。
零外部依赖，基于系统共享内存（Windows MMF / POSIX shm）提供极高吞吐的
跨 Worker 进程事件分发：
targetType = 0: 全局广播（向所有 worker 的所有连接广播）
targetType = 1: 频道/房间广播（向所有 worker 中加入指定 channel 的连接广播）
targetType = 2: 定向投递（向特定 connection ID 所在 worker 精准单播）
并提供跨进程实时的集群连接总数原子计数（ClusterConnCount）。

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
  - 发布跨进程全局广播消息。

- public void PublishChannel(string channel, string payload, int payloadLen)
  - 发布跨进程频道/房间广播消息。

- public void PublishDirect(int targetId, string payload, int payloadLen)
  - 发布跨进程针对指定连接 ID 的定向投递消息。

- void PublishInternal(int targetType, string channel, int targetId, string payload, int payloadLen)

- public void OnConnChange(int delta)
  - 增减全局在线连接数统计。

- public int GetTotalConnCount()
  - 获取集群跨 Worker 实时在线连接总数。

- public void StartPolling(IWsSharedBusConsumer consumer)
  - 启动后台轮询协程，监听其他 Worker 发来的跨进程消息。
    <remarks>必须是同步方法：Zan 的 async 方法体在首次调度泵时才启动，
    若把快照写进 async 体，调用点与首次泵之间发布的消息会被当旧序号跳过。
    快照必须在调用点急切完成，循环体晚一点启动无所谓。</remarks>

- private async void PollLoop(IWsSharedBusConsumer consumer)

- public void Stop()


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
  - 内部构造：64KiB 输出缓冲。

- void Ensure(int need)
  - 扩展输出缓冲到至少 need 字节。

- void Append(string data, int n)
  - 追加编码后帧的 `n` 个字节。

- bool Pending()
  - 缓冲中是否有待发字节。

- async int Flush(TcpClient client)
  - 把缓冲内容作为一次 send 全部写出并重置。
    返回写出字节数；-1 表示连接已死。Socket.SendAsync 内部
    已循环补发，返回值小于请求长度只会是对端重置/中止等
    致命错误——旧实现无视这点直接清零缓冲，把没发出去的
    帧当已发，帧流从此错位且无任何上报。


## IWsSharedBusConsumer (interface)

跨进程 WebSocket 共享消息总线事件接收接口。

- void OnSharedBusMessage(int targetType, string channel, int targetId, string payload, int payloadLen);
