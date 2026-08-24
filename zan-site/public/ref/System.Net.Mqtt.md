# System.Net.Mqtt

> 源码: `stdlib/System/Net/Mqtt/MqttBroker.zan`, `stdlib/System/Net/Mqtt/MqttClient.zan`


## MqttBroker (class)

MQTT v3.1.1 broker（`MqttClient` 的服务端）。

支持 CONNECT/CONNACK、QoS 0 与 1 的 PUBLISH（PUBACK）、SUBSCRIBE/SUBACK、
UNSUBSCRIBE/UNSUBACK、PINGREQ/PINGRESP 与 DISCONNECT，支持 `+` 和 `#`
主题过滤器通配符。QoS 2 会降级为 1。未实现保留消息与
持久会话。

独立运行：
MqttBroker b = new MqttBroker("0.0.0.0", 1883);
await b.RunAsync();                      // accept loop; never returns

在 Worker 下（协议 "mqtt"），由 Worker 接受连接并为每个客户端
调用 `HandleConnection`，因此 broker 也能用于
主/多 worker 模式。注意多进程模式下每个 worker 持有
自己的会话表：发布者只能触达同一 worker 接受的订阅者，
因此需要全局广播的 broker 应使用 count = 1。

- string host;

- int port;

- List<MqttSession> sessions;

- List<MqttTopicStat> topics;

- bool running;

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

- static int Now()
  - 墙上时钟秒；用于管理快照中的时间戳。

- static MqttBroker inst;

- static MqttBroker Global()

- static void Use(MqttBroker b)
  - 将此 broker 发布为进程级实例，使 HTTP
    控制器无需层层传递即可读取其注册表。

- static MqttBroker Instance()

- int Count()

- async void RunAsync()
  - 绑定、监听并服务客户端。永不返回。

- async int ReadByteAsync(nint sock)

- async byte[]ReadBytesAsync(nint sock, int need)

- async int ReadRemainingLenAsync(nint sock)

- static List<string> SplitTopic(string s)

- static bool TopicMatches(string filter, string topic)

- static byte[]BuildPublish(string topic, string payload, int payloadLen)

- int PublishPacketLen(string topic, int payloadLen)

- async int PublishToSubscribers(string topic, string payload, int payloadLen)
  - 将消息投递给所有过滤器匹配
    <paramref name="topic"/> 的存活会话。返回接收者数量。

- void Touch(string topic, string payload)
  - 将消息记录到其主题下（消息计数 + 最后一条
    负载），首次使用时创建主题条目。

- int Publish(string topic, string payload)
  - 管理端注入：以 broker 自身身份发布消息，
    并返回送达的订阅者数量。刻意保持同步——
    它会向其他客户端写入，而这些协程正停靠在可读性上，
    因此使用直接的非阻塞 send，而非可 await 的路径。

- bool Matches(MqttSession sess, string topic)

- bool Kick(string clientId)
  - 按 MQTT 客户端 id 强制断开客户端。

- int SubsTotal()

- string ClientsJson()

- string ClientDetailJson(string clientId)
  - 返回一个客户端及其订阅，未知时返回 ""。

- string SubscriptionsJson()

- string TopicsJson()

- string MetricsJson()
  - broker 吞吐量快照。

- async void HandleConnection(nint sock)
  - 在已接受的套接字上服务一个 MQTT 客户端，直到其
    断开。RunAsync 与 "mqtt" Worker 都会使用。

- void Stop()


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

- string clientId;

- string host;

- int port;

- bool connected;

- int nextPacketId;

- int keepAlive;

- [DllImport("crt")]static extern long strlen(string str);

- MqttClient()

- async int ReadByteAsync()
  - 精确读取一个字节，对端关闭时返回 -1。

- async byte[]ReadBytesAsync(int need)
  - 在 IO reactor 上挂起，精确读取 <paramref name="need"/> 字节，
    直到所需的多次 recv 全部完成。

- async int ReadRemainingLenAsync()
  - 解码 MQTT "remaining length" varint（1-4 字节）。

- async int DrainPacketAsync()
  - 读取并丢弃一个完整控制报文（如 SUBACK / PUBACK），
    返回其报文类型以保持流帧同步。

- static async MqttClient ConnectAsync(string host, int port, string clientId)
  - 连接 broker 并执行 MQTT CONNECT/CONNACK
    握手，每个网络步骤都在 IO reactor 上挂起。

- async int PublishAsync(string topic, string message, int qos)
  - 发布消息，在 IO reactor 上挂起。QoS 1 时
    还会等待 PUBACK。返回已发送的字节数。

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

- async int PingAsync()
  - 发送 PINGREQ 保活，在 IO reactor 上挂起。
    返回已发送的字节数。

- async int DisconnectAsync()
  - 发送 DISCONNECT 并关闭连接，在 IO
    reactor 上等待发送完成。返回已发送的字节数。

- bool IsConnected()

- byte[]BuildConnectPacket()

- int CalcConnectLen()


## MqttClientDetailDoc (class)

- int id;

- string client_id;

- string addr;

- int connected_at;

- int msgs_in;

- int msgs_out;

- List<MqttSubDoc> subscriptions;


## MqttClientDoc (class)

- int id;

- string client_id;

- string addr;

- int connected_at;

- int keepalive;

- int subs;

- int msgs_in;

- int msgs_out;


## MqttMetricsDoc (class)

- int uptime_sec;

- int clients_connected;

- int clients_total;

- int subscriptions;

- int topics;

- int messages_in;

- int messages_out;

- int bytes_in;

- int bytes_out;


## MqttPacketType (class)

- static int CONNECT=1;

- static int CONNACK=2;

- static int PUBLISH=3;

- static int PUBACK=4;

- static int SUBSCRIBE=8;

- static int SUBACK=9;

- static int UNSUBSCRIBE=10;

- static int UNSUBACK=11;

- static int PINGREQ=12;

- static int PINGRESP=13;

- static int DISCONNECT=14;


## MqttQos (class)

- static int AtMostOnce=0;

- static int AtLeastOnce=1;

- static int ExactlyOnce=2;


## MqttSession (class)

broker 持有的单个已连接 MQTT 客户端会话：其套接字、客户端 id
与当前生效的主题过滤器。

- int id;

- nint sock;

- string clientId;

- string addr;

- List<string> filters;

- List<int> qos;

- bool alive;

- int connectedAt;

- int keepAlive;

- int msgsIn;

- int msgsOut;

- MqttSession(nint sock)


## MqttSubDoc (class)

- string filter;

- int qos;


## MqttSubEntryDoc (class)

- string client_id;

- string filter;

- int qos;


## MqttTopicDoc (class)

- string topic;

- int messages;

- string last;


## MqttTopicStat (class)

为管理 API 维护的按主题计数器。

- string name;

- int messages;

- string lastPayload;

- MqttTopicStat(string name)
