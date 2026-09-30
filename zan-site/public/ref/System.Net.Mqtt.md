# System.Net.Mqtt

> 源码: `stdlib/System/Net/Mqtt/MqttBroker.zan`, `stdlib/System/Net/Mqtt/MqttClient.zan`, `stdlib/System/Net/Mqtt/MqttReader.zan`, `stdlib/System/Net/Mqtt/MqttSharedBus.zan`


## MqttBroker (class)

MQTT v3.1.1 broker（`MqttClient` 的服务端）。

支持 CONNECT/CONNACK、QoS 0 与 1 的 PUBLISH（PUBACK）、SUBSCRIBE/SUBACK、
UNSUBSCRIBE/UNSUBACK、PINGREQ/PINGRESP 与 DISCONNECT，支持 `+` 和 `#`
主题过滤器通配符。QoS 2 会降级为 1。未实现保留消息与
持久会话。

独立的监听循环已退役——唯一的宿主是 Worker：协议 "mqtt" 的
worker 由 Worker 接受连接并为每个客户端调用
`HandleConnection`。注意多进程模式下每个 worker 持有
自己的会话表：发布者只能触达同一 worker 接受的订阅者，
因此需要全局广播的 broker 应使用 count = 1。

- List<MqttSession> sessions;

- List<MqttTopicStat> topics;

- List<MqttRetainedMessage> retainedMessages;

- bool keepAliveRunning;

- MqttSharedBus sharedBus;

- int nextId;

- int startedAt;

- int totalConnections;

- int msgsIn;

- int msgsOut;

- int bytesIn;

- int bytesOut;

- [DllImport("crt")]static extern long strlen(string str);

- [DllImport("crt")]static extern long time(nint ptr);

- MqttBroker(string host, int port)
  - 内部构造：空会话表与主题表；经 Global()/Use() 使用。

- static int Now()
  - 墙上时钟秒；用于管理快照中的时间戳。

- static MqttBroker inst;
  - 供 Worker 集成使用的共享实例（"mqtt" 协议的 worker 会将
    每个接受的连接路由到这里）；首次调用时惰性创建。

- static MqttBroker Global()
  - 进程级共享实例的取用入口：未 Use 时惰性创建默认 broker。

- static void Use(MqttBroker b)
  - 将此 broker 发布为进程级实例，使 HTTP
    控制器无需层层传递即可读取其注册表。

- static MqttBroker Instance()
  - 进程级共享实例（未 Use 时返回惰性创建的默认实例）。

- static void TeardownGlobal()
  - 进程退出前的拆卸入口：停掉共享总线轮询并清空
    注册表，再解除进程级实例。静态 inst 是 broker 及其
    sessions/topics/retained 列表在泄漏探测里的保持根，
    不拆就恒报"退出仍可达"（A352）。

- void Shutdown()
  - 停机：停 KeepAlive 巡检协程（其帧持有 this，不置
    停止标志它就永生，broker 及列表在退出泄漏探测里恒可达）、
    停总线轮询、断开并清空全部会话/统计/保留消息。可随后
    TeardownGlobal 一并解除静态实例。

- int Count()
  - 当前连接的客户端数。

- async int ReadByteAsync(nint sock)
  - 精确读一个字节；对端关闭或套接字已死时返回 -1——两种
    情况都必须结束会话。用真正的重叠接收（RecvOv）而非
    ReadReady + Recv：后者在数据未到达时把 would-block 误判
    为对端关闭，而在对端 RST 后死套接字永远"可读"，重试只会
    让协程原地空转。RecvOv 只有读到数据或对端关闭才完成。

- async byte[]ReadBytesAsync(nint sock, int need)
  - 循环读满 `need` 字节；对端中途关闭时返回已读部分。

- async int ReadRemainingLenAsync(nint sock)
  - 读 MQTT 变长（varint）剩余长度；套接字关闭返回 -1，
    超过 4 字节编码上限返回 -2（协议错误）。

- static List<string> SplitTopic(string s)
  - 按 "/" 切分主题/过滤器为层。

- static bool TopicMatches(string filter, string topic)
  - 过滤器是否匹配具体主题：`#` 匹配剩余所有层，`+` 匹配单层，
    其余按层相等；层数必须一致。

- static byte[]BuildPublish(string topic, string payload, int payloadLen)
  - 构建 QoS 0 的 PUBLISH 报文（固定头 + varint 剩余长度 +
    2 字节主题长度 + 主题 + 载荷；载荷可含 NUL，按 len 原生内存精确拷贝）。

- static byte[]BuildPublishBytes(string topic, byte[]payload, int pOffset, int payloadLen)
  - 构建 QoS 0 的 PUBLISH 报文（原生 byte[] 载荷，零中转字符串拷贝）。

- static byte[]BuildPublishRetain(string topic, string payload, int payloadLen)
  - 构建带 Retain 标记的 PUBLISH 报文。

- void SaveRetained(string topic, string payload, int payloadLen, int qos)
  - 存储或清除保留消息（payloadLen == 0 时按规范删除对应主题的保留消息）。

- int PublishPacketLen(string topic, int payloadLen)
  - 同一报文的线上总长（1 固定头 + varint 长度 + 剩余），
    供发送方统计字节用，不必实际构建报文。

- public void EnableSharedBus(string busName)
  - 启用跨进程共享内存消息总线（基于 SharedTable）。
    在 Worker 多进程模型下，消息不仅推送到当前 Worker 的本地订阅者，
    还会通过跨进程共享内存总线广播给其他 Worker 进程中的订阅者。

- public async int PublishLocal(string topic, string payload, int payloadLen)
  - 将消息投递给本进程内所有过滤器匹配
    <paramref name="topic"/> 的存活会话。返回接收者数量。
    采用非阻塞优先直接发送 + 异步慢客户端容错快速隔离机制，
    防止单连接慢速或挂起导致全局广播路径被卡死。

- async int PublishToSubscribers(string topic, string payload, int payloadLen)
  - 分发消息：先投递给本进程内匹配的会话，若启用了跨进程共享总线，
    则同时广播至总线同步到其他 Worker 进程。

- void Touch(string topic, string payload)
  - 将消息记录到其主题下（消息计数 + 最后一条
    负载），首次使用时创建主题条目。主题表设上限：不设的话
    恶意客户端发布无限多不同的主题即可让 broker 内存无界
    增长（每个条目还保留整条 lastPayload）。达到上限后新主题
    不再建条目，已有主题的计数照常累加。

- int Publish(string topic, string payload)
  - 管理端注入：以 broker 自身身份发布消息，
    并返回送达的订阅者数量。刻意保持同步——
    它会向其他客户端写入，而这些协程正停靠在可读性上，
    因此使用直接的非阻塞 send，而非可 await 的路径。

- bool Matches(MqttSession sess, string topic)
  - 该会话的任一过滤器是否匹配主题。

- bool Kick(string clientId)
  - 按 MQTT 客户端 id 强制断开客户端。

- int SubsTotal()
  - 全部会话的订阅总数。

- string ClientsJson()
  - 已连接客户端列表的管理 JSON。

- string ClientDetailJson(string clientId)
  - 返回一个客户端及其订阅，未知时返回 ""。

- string SubscriptionsJson()
  - 全部订阅（按客户端平铺）的管理 JSON。

- string TopicsJson()
  - 已见主题列表（名称/计数/最后负载）的管理 JSON。

- string MetricsJson()
  - broker 吞吐量快照。

- async void EnsureKeepAliveInspector()
  - 启动 KeepAlive 心跳定时巡检协程（MQTT-3.1.2.10）。
    每 1 秒轮询一次所有已连接的存活会话；若客户端声明的 keepAlive > 0，
    且距上次收到任何报文已超过 1.5 * keepAlive 秒，则判定对端假死/异常离线，
    主动调用 Socket.ShutdownBoth 唤醒并终止挂起的读取协程，
    触发会话自动清理与 LWT 遗嘱广播。

- async void HandleConnection(nint sock)
  - 在已接受的套接字上服务一个 MQTT 客户端，直到其
    断开。"mqtt" 协议的 Worker 用它服务每个客户端连接。
    清理走 finally：钩子代码（PUBLISH/SUBSCRIBE 分支）抛出时
    也不能把会话留在表里或泄漏套接字。

- async void HandleConnectionInner(nint sock)
  - HandleConnection 的主体；注意此时套接字的关闭统一由
    HandleConnection 的 finally 负责，本方法不自行 Close。


## MqttClient (class)

MQTT v3.1.1 客户端。

对协程友好：每个网络操作都是 <c>async</c> 方法，
通过 `TcpClient` await 底层非阻塞套接字，因此
协程内使用的 MQTT 客户端会在 IO reactor 上挂起（将 worker
让给其他协程），而不会在等待 broker 应答或传入 PUBLISH 时
阻塞操作系统线程。

用法：
MqttClient c = await MqttClient.ConnectAsync("broker", 1883, "client-1");
int s = await c.SubscribeAsync("sensors/temp", MqttQos.AtLeastOnce);
int p = await c.PublishAsync("sensors/temp", "21.5", MqttQos.AtLeastOnce);
string msg = await c.ReceiveAsync();   // suspends until a PUBLISH arrives
int d = await c.DisconnectAsync();

- TcpClient conn;

- MqttReader reader;

- string clientId;

- string host;

- int port;

- bool connected;

- int nextPacketId;

- int keepAlive;

- string willTopic;

- string willMessage;

- int willQos;

- bool willRetain;

- bool hasWill;

- [DllImport("crt")]static extern long strlen(string str);

- MqttClient()
  - 构造未连接的客户端；连接通过 `ConnectAsync` 建立。

- public void SetWill(string topic, string message, int qos, bool retain)
  - 设置客户端遗嘱（LWT）属性，在连接建立前配置。

- async int ReadByteAsync()
  - 精确读取一个字节，对端关闭时返回 -1。

- async byte[]ReadBytesAsync(int need)
  - 在 IO reactor 上挂起，精确读取 <paramref name="need"/> 字节，
    直到所需的多次 recv 全部完成。

- async int ReadRemainingLenAsync()
  - 解码 MQTT "remaining length" varint（1-4 字节）。

- static int RemainLenBytes(int remainLen)
  - MQTT "remaining length" varint 编码所需的字节数（1-4）。

- static int WriteRemainLen(byte[]packet, int offset, int remainLen)
  - 把 "remaining length" 编码为 MQTT varint 写入
    <paramref name="packet"/> 的 <paramref name="offset"/> 处，
    返回写入的字节数（1-4）。此前各报文构建器把 remainLen
    截进单字节，报文一旦超过 127 字节就违反协议、让 broker
    端的 varint 解码从此错位。

- async int DrainPacketAsync()
  - 读取并丢弃一个完整控制报文（如 SUBACK / PUBACK），
    返回其报文类型以保持流帧同步。

- static async MqttClient ConnectAsync(string host, int port, string clientId)
  - 连接 broker 并执行 MQTT CONNECT/CONNACK
    握手，每个网络步骤都在 IO reactor 上挂起。

- static async MqttClient ConnectAsync(string host, int port, string clientId, string willTopic, string willMessage, int willQos, bool willRetain)
  - 带遗嘱（LWT）配置连接 broker 并执行 MQTT CONNECT/CONNACK 握手。

- static async MqttClient ConnectAsync(string host, int port, string clientId, int keepAlive, string willTopic, string willMessage, int willQos, bool willRetain)
  - 带遗嘱（LWT）与自定义 KeepAlive 秒数连接 broker 并执行 MQTT CONNECT/CONNACK 握手。

- async int PublishAsync(string topic, string message, int qos)
  - 发布消息，在 IO reactor 上挂起。QoS 1 时
    还会等待 PUBACK。返回已发送的字节数。

- async int PublishAsync(string topic, string message, int qos, bool retain)
  - 发布消息并指定 retain 标志，在 IO reactor 上挂起。

- async int PublishBytesAsync(string topic, byte[]payload, int offset, int count, int qos, bool retain)
  - 以原生字节数组切片发布二进制消息，彻底杜绝包含 NUL 字节时被截断。在 IO reactor 上挂起。

- async int PublishBytesAsync(string topic, byte[]payload, int qos, bool retain)
  - 以原生字节数组发布完整二进制消息。

- async int SubscribeAsync(string topic, int qos)
  - 订阅主题并等待 SUBACK，在 IO
    reactor 上挂起。返回已发送的字节数。

- async int UnsubscribeAsync(string topic)
  - 取消订阅主题并等待 UNSUBACK，在
    IO reactor 上挂起。返回已发送的字节数。

- async string ReceiveAsync()
  - 等待下一个传入的 PUBLISH 并返回其负载，
    在 IO reactor 上挂起直到数据到达。对 broker 的
    PINGREQ 透明响应，并继续等待真正的消息。

- async byte[]ReceiveBytesAsync()
  - 等待下一个传入的 PUBLISH 并返回其原生二进制负载，彻底杜绝包含 NUL 字节时被截断。
    在 IO reactor 上挂起直到数据到达。对端关闭或出错时返回 null。

- async int PingAsync()
  - 发送 PINGREQ 保活，在 IO reactor 上挂起。
    返回已发送的字节数。

- async int DisconnectAsync()
  - 发送 DISCONNECT 并关闭连接，在 IO
    reactor 上等待发送完成。返回已发送的字节数。

- public void Abort()
  - 强制断开底层连接（不发送 DISCONNECT 报文），用于模拟客户端异常断开并触发遗嘱消息（LWT）。

- bool IsConnected()
  - 连接是否仍存活：CONNACK 返回码为 0 后为 true，
    对端关闭或 `DisconnectAsync` 后为 false。

- byte[]BuildConnectPacket()
  - 编码 CONNECT 报文：协议名 "MQTT"、协议级别 4（3.1.1）、
    clean session 标志、遗嘱标志与载荷、keepAlive 秒数与 client id。


## MqttClientDetailDoc (class)

单个客户端的完整快照（含订阅列表）。

- int id;

- string client_id;

- string addr;

- int connected_at;

- int msgs_in;

- int msgs_out;

- List<MqttSubDoc> subscriptions;


## MqttClientDoc (class)

已连接客户端的管理快照条目。

- int id;

- string client_id;

- string addr;

- int connected_at;

- int keepalive;

- int subs;

- int msgs_in;

- int msgs_out;


## MqttMetricsDoc (class)

broker 吞吐量快照（对应 MetricsJson 输出结构）。

- int uptime_sec;

- int clients_connected;

- int clients_total;

- int subscriptions;

- int topics;

- int messages_in;

- int messages_out;

- int bytes_in;

- int bytes_out;


## MqttPacket (class)

表示一个解码完成的完整 MQTT 报文。

- public int packetType;

- public int flags;

- public byte[]body;

- public int bodyLen;


## MqttPacketType (class)

MQTT v3.1.1 控制报文类型（固定头第一个字节的高 4 位）。

- static const int CONNECT=1;
  - 连接请求。

- static const int CONNACK=2;
  - 连接确认。

- static const int PUBLISH=3;
  - 发布消息。

- static const int PUBACK=4;
  - QoS 1 发布确认。

- static const int PUBREC=5;
  - QoS 2 发布已收到（第二阶段）。

- static const int PUBREL=6;
  - QoS 2 发布释放（第三阶段）。

- static const int PUBCOMP=7;
  - QoS 2 发布完成（第四阶段）。

- static const int SUBSCRIBE=8;
  - 订阅请求。

- static const int SUBACK=9;
  - 订阅确认。

- static const int UNSUBSCRIBE=10;
  - 退订请求。

- static const int UNSUBACK=11;
  - 退订确认。

- static const int PINGREQ=12;
  - 保活请求。

- static const int PINGRESP=13;
  - 保活响应。

- static const int DISCONNECT=14;
  - 断开连接。


## MqttQos (class)

MQTT QoS 服务等级（编码进 PUBLISH 固定头）。

- static const int AtMostOnce=0;
  - 至多一次：不确认、不重传。

- static const int AtLeastOnce=1;
  - 至少一次：发送后等待 PUBACK。

- static const int ExactlyOnce=2;
  - 恰好一次（协议 QoS 2；本客户端发送后并不等待确认，
    实际处理与 QoS 0 相同）。


## MqttReader (class)

纯内存滑动窗口 MQTT 报文预读与帧解析器。
彻底消除逐字节 RecvOv(1) 系统调用，一次 RecvOv 批量读入多字节，并在纯内存中
状态机解码变长剩余长度（Remaining Length）与报文帧边界。

- nint sock;

- byte[]buf;

- int len;

- int cap;

- int start;

- byte[]tmp;

- int maxPacketLen;

- public MqttReader(nint sock)

- public void SetSocket(nint sock)

- public void Compact()

- public void Ensure(int need)

- public async int FillMore()

- public int PacketSize()
  - 检测当前缓冲中是否已经包含完整的下一个 MQTT 报文。
    返回值：
    > 0 : 完整报文总字节数（固定头 + 剩余长度字节 + 载荷）；
    -1  : 数据未收全，需要等待更多数据；
    -2  : 畸形协议：变长长度超过 4 字节；
    -3  : 报文长度超过 maxPacketLen。

- public async MqttPacket ReadPacketAsync()
  - 读取下一个完整报文。返回 null 表示流结束或协议错误。


## MqttRetainedMessage (class)

为主题保留的最新消息（Retained Message）。

- public string topic;

- public string payload;

- public int payloadLen;

- public int qos;

- public MqttRetainedMessage(string topic, string payload, int payloadLen, int qos)


## MqttSession (class)

broker 持有的单个已连接 MQTT 客户端会话：其套接字、客户端 id
与当前生效的主题过滤器。

- int id;

- nint sock;

- string clientId;

- string addr;

- List<MqttSubscription> subscriptions;

- bool alive;

- bool connected;

- int connectedAt;

- int keepAlive;

- int lastSeen;
  - 最近一次收到该会话任何字节的时间（墙上时钟秒）；
    keepAlive > 0 时超过 1.5 倍即按协议挂断。

- int msgsIn;

- int msgsOut;

- bool cleanSession;

- bool willFlag;

- int willQos;

- bool willRetain;

- string willTopic;

- string willMessage;

- bool receivedDisconnect;

- MqttSession(nint sock)
  - 内部构造：未 CONNECT 的裸会话，id/clientId 在握手时填。


## MqttSharedBus (class)

基于 SharedTable 共享内存环形缓冲区的跨进程 MQTT 消息总线。
在 Worker 多进程模型下无需部署外部中间件即可实现零拷贝/极低延迟的跨进程主题广播。

- SharedTable busTable;

- string name;

- int ringCapacity;

- long lastReadSeq;

- bool running;

- MqttBroker broker;

- int pid;

- public MqttSharedBus(MqttBroker broker, string name, int ringCapacity)

- void InitTable()

- public bool IsAvailable()

- public void Broadcast(string topic, string payload, int payloadLen)
  - 向跨进程总线广播消息，由各 Worker 进程异步抓取并下发给各自连接的本地订阅者。

- public async void StartPolling()
  - 启动跨进程总线轮询协程。

- public void Stop()
  - 停掉轮询协程：只翻标志，下一跳 10ms 轮询帧自行
    完成并释放（与 WsSharedBus.Stop 同一收尾定式）。


## MqttSubDoc (class)

订阅条目（管理快照用）。

- string filter;

- int qos;


## MqttSubEntryDoc (class)

订阅平铺条目（客户端 id + 过滤器 + QoS）。

- string client_id;

- string filter;

- int qos;


## MqttSubscription (class)

会话订阅的主题过滤器及授予的 QoS 实体记录。

- string filter;

- int qos;

- MqttSubscription(string filter, int qos)


## MqttTopicDoc (class)

已见主题：名称、消息计数、最后一条负载。

- string topic;

- int messages;

- string last;


## MqttTopicStat (class)

为管理 API 维护的按主题计数器。

- string name;

- int messages;

- string lastPayload;

- MqttTopicStat(string name)
  - 内部构造：计数为 0、无负载。
