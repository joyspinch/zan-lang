# System.Net.Modbus

> 源码: `packages/Zan.Net/src/System/Net/Modbus/ModbusClient.zan`


## ModbusClient (class)

Modbus TCP 客户端——用于 PLC、仪表与工业传感器的
主流现场总线协议，默认端口 502。

与 MqttClient 一样对协程友好：每个操作都 await 非阻塞套接字，
响应依据 MBAP 长度前缀重新组装，
无论设备如何分段写入，解析都能保持帧同步。

寄存器值为 16 位（0..65535）；地址为基于零的协议地址
（文档中 "40001" 的约定对应地址 0）。

用法：
ModbusClient m = await ModbusClient.ConnectAsync("192.168.1.10", 502, 1);
List<int> regs = await m.ReadHoldingAsync(0, 4);   // 4 registers from 0
int ok = await m.WriteRegisterAsync(10, 1234);
List<int> bits = await m.ReadCoilsAsync(0, 8);
m.Close();

- TcpClient conn;

- int unitId;

- int nextTid;

- bool connected;

- bool busy;

- List<AsyncGate> waiters;

- string lastError;
  - 成功交换后为空 ""；失败后为简短原因，
    以便调用方区分异常应答与链路断开。

- ModbusClient()
  - 私有构造：由 `ConnectAsync` 使用。

- static async ModbusClient ConnectAsync(string host, int port, int unit)
  - 连接 Modbus TCP 端点。<paramref name="unit"/> 是
    单元/从站 id（大多数 TCP 设备为 1，
    在网关之后才有意义）。

- bool IsConnected()
  - 连接是否仍可用（对端关闭或帧错误后为 false）。

- string LastError()
  - 上一次交换的失败原因（成功时为 ""）。

- void Close()
  - 关闭连接并安全唤醒所有排队等待的协程（幂等）。

- async byte[]ReadBytesAsync(int need)
  - 在 IO reactor 上挂起，跨多次 recv 精确读取
    <paramref name="need"/> 个字节；对端关闭或出错时置 connected=false
    并返回已到手的（残缺）缓冲。

- async bool AcquireLock()

- void ReleaseLock()

- async byte[]TransactAsync(byte[]pdu, int pduLen)
  - 一次请求/响应往返：发送 MBAP 头 + PDU，接收帧化的
    应答 PDU（功能字节 + 数据）。带并发排队门控，确保单连接上
    事务互斥单飞，杜绝并发请求在 TCP 流上相互踩踏。

- async byte[]TransactInternalAsync(byte[]pdu, int pduLen)

- async List<int> ReadBitsAsync(int fc, int addr, int count)
  - 线圈（fc 1）与离散输入（fc 2）共用的读取器：
    每个位对应一个 0/1 int；失败时返回空列表（见 LastError）。

- async List<int> ReadRegsAsync(int fc, int addr, int count)
  - 保持（fc 3）与输入（fc 4）寄存器共用的读取器：
    每个寄存器为 16 位大端值；失败时返回空列表（见 LastError）。

- async List<int> ReadCoilsAsync(int addr, int count)
  - 读取线圈（功能码 1）：每个线圈对应一个 0/1 int。

- async List<int> ReadDiscreteAsync(int addr, int count)
  - 读取离散输入（功能码 2）：每个输入对应一个 0/1 int。

- async List<int> ReadHoldingAsync(int addr, int count)
  - 读取保持寄存器（功能码 3）。

- async List<int> ReadInputAsync(int addr, int count)
  - 读取输入寄存器（功能码 4）。

- async int WriteCoilAsync(int addr, bool on)
  - 写入单个线圈（功能码 5）。成功返回 1，失败返回 0
    （详见 LastError）。

- async int WriteRegisterAsync(int addr, int val)
  - 写入单个保持寄存器（功能码 6）。成功返回 1，
    失败返回 0（详见 LastError）。

- async int WriteRegistersAsync(int addr, List<int> values)
  - 批量写入保持寄存器（功能码 16）。成功返回 1，
    失败返回 0（详见 LastError）。
