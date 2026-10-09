# System.Net.Mqtt

> 源码: `packages/Zan.Net/src/System/Net/Mqtt/MqttBroker.zan`, `packages/Zan.Net/src/System/Net/Mqtt/MqttClient.zan`, `packages/Zan.Net/src/System/Net/Mqtt/MqttReader.zan`, `packages/Zan.Net/src/System/Net/Mqtt/MqttSharedBus.zan`


## MqttBroker (class)

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

- static int Now()

- static MqttBroker inst;

- static MqttBroker Global()

- static void Use(MqttBroker b)

- static MqttBroker Instance()

- static void TeardownGlobal()

- void Shutdown()

- int Count()

- async int ReadByteAsync(nint sock)

- async byte[]ReadBytesAsync(nint sock, int need)

- async int ReadRemainingLenAsync(nint sock)

- static List<string> SplitTopic(string s)

- static bool TopicMatches(string filter, string topic)

- static byte[]BuildPublish(string topic, string payload, int payloadLen)

- static byte[]BuildPublishBytes(string topic, byte[]payload, int pOffset, int payloadLen)

- static byte[]BuildPublishRetain(string topic, string payload, int payloadLen)

- void SaveRetained(string topic, string payload, int payloadLen, int qos)

- int PublishPacketLen(string topic, int payloadLen)

- public void EnableSharedBus(string busName)

- public async int PublishLocal(string topic, string payload, int payloadLen)

- async int PublishToSubscribers(string topic, string payload, int payloadLen)

- void Touch(string topic, string payload)

- int Publish(string topic, string payload)

- bool Matches(MqttSession sess, string topic)

- bool Kick(string clientId)

- int SubsTotal()

- string ClientsJson()

- string ClientDetailJson(string clientId)

- string SubscriptionsJson()

- string TopicsJson()

- string MetricsJson()

- async void EnsureKeepAliveInspector()

- async void HandleConnection(nint sock)

- async void HandleConnectionInner(nint sock)


## MqttClient (class)

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

- public void SetWill(string topic, string message, int qos, bool retain)

- async int ReadByteAsync()

- async byte[]ReadBytesAsync(int need)

- async int ReadRemainingLenAsync()

- static int RemainLenBytes(int remainLen)

- static int WriteRemainLen(byte[]packet, int offset, int remainLen)

- async int DrainPacketAsync()

- static async MqttClient ConnectAsync(string host, int port, string clientId)

- static async MqttClient ConnectAsync(string host, int port, string clientId, string willTopic, string willMessage, int willQos, bool willRetain)

- static async MqttClient ConnectAsync(string host, int port, string clientId, int keepAlive, string willTopic, string willMessage, int willQos, bool willRetain)

- async int PublishAsync(string topic, string message, int qos)

- async int PublishAsync(string topic, string message, int qos, bool retain)

- async int PublishBytesAsync(string topic, byte[]payload, int offset, int count, int qos, bool retain)

- async int PublishBytesAsync(string topic, byte[]payload, int qos, bool retain)

- async int SubscribeAsync(string topic, int qos)

- async int UnsubscribeAsync(string topic)

- async string ReceiveAsync()

- async byte[]ReceiveBytesAsync()

- async int PingAsync()

- async int DisconnectAsync()

- public void Abort()

- bool IsConnected()

- byte[]BuildConnectPacket()


## MqttPacket (class)

- public int packetType;

- public int flags;

- public byte[]body;

- public int bodyLen;


## MqttPacketType (class)

- static const int CONNECT=1;

- static const int CONNACK=2;

- static const int PUBLISH=3;

- static const int PUBACK=4;

- static const int PUBREC=5;

- static const int PUBREL=6;

- static const int PUBCOMP=7;

- static const int SUBSCRIBE=8;

- static const int SUBACK=9;

- static const int UNSUBSCRIBE=10;

- static const int UNSUBACK=11;

- static const int PINGREQ=12;

- static const int PINGRESP=13;

- static const int DISCONNECT=14;


## MqttQos (class)

- static const int AtMostOnce=0;

- static const int AtLeastOnce=1;

- static const int ExactlyOnce=2;


## MqttReader (class)

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

- public async MqttPacket ReadPacketAsync()


## MqttRetainedMessage (class)

- public string topic;

- public string payload;

- public int payloadLen;

- public int qos;

- public MqttRetainedMessage(string topic, string payload, int payloadLen, int qos)


## MqttSession (class)

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


## MqttSharedBus (class)

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

- public async void StartPolling()

- public void Stop()


## MqttSubscription (class)

- string filter;

- int qos;

- MqttSubscription(string filter, int qos)


## MqttTopicStat (class)

- string name;

- int messages;

- string lastPayload;

- MqttTopicStat(string name)
