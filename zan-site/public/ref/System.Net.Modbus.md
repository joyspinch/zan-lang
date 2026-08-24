# System.Net.Modbus

> 源码: `stdlib/System/Net/Modbus/ModbusClient.zan`


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

- string lastError;

- ModbusClient()

- static async ModbusClient ConnectAsync(string host, int port, int unit)
  - 连接 Modbus TCP 端点。<paramref name="unit"/> 是
    单元/从站 id（大多数 TCP 设备为 1，
    在网关之后才有意义）。

- bool IsConnected()

- string LastError()
  - 上一次交换的失败原因（成功时为 ""）。

- void Close()

- async byte[]ReadBytesAsync(int need)

- async byte[]TransactAsync(byte[]pdu, int pduLen)

- async List<int> ReadBitsAsync(int fc, int addr, int count)

- async List<int> ReadRegsAsync(int fc, int addr, int count)

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
