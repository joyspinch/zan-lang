# System.Net.Modbus

> 源码: `packages/Zan.Net/src/System/Net/Modbus/ModbusClient.zan`


## ModbusClient (class)

- TcpClient conn;

- int unitId;

- int nextTid;

- bool connected;

- bool busy;

- List<AsyncGate> waiters;

- string lastError;

- ModbusClient()

- static async ModbusClient ConnectAsync(string host, int port, int unit)

- bool IsConnected()

- string LastError()

- void Close()

- async byte[]ReadBytesAsync(int need)

- async bool AcquireLock()

- void ReleaseLock()

- async byte[]TransactAsync(byte[]pdu, int pduLen)

- async byte[]TransactInternalAsync(byte[]pdu, int pduLen)

- async List<int> ReadBitsAsync(int fc, int addr, int count)

- async List<int> ReadRegsAsync(int fc, int addr, int count)

- async List<int> ReadCoilsAsync(int addr, int count)

- async List<int> ReadDiscreteAsync(int addr, int count)

- async List<int> ReadHoldingAsync(int addr, int count)

- async List<int> ReadInputAsync(int addr, int count)

- async int WriteCoilAsync(int addr, bool on)

- async int WriteRegisterAsync(int addr, int val)

- async int WriteRegistersAsync(int addr, List<int> values)
