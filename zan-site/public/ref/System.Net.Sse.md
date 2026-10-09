# System.Net.Sse

> 源码: `packages/Zan.Net/src/System/Net/Sse/Sse.zan`


## SseClient (class)

- TcpClient client;

- string buf;

- bool connected;

- static async SseClient ConnectAsync(string host, int port, string path)

- bool IsConnected()

- async SseEvent NextAsync()

- SseEvent ParseBlock(string block)

- void Close()


## SseConnection (class)

- TcpClient client;

- string path;

- bool open;

- static SseConnection Of(TcpClient client, string path)

- string Path()

- bool IsOpen()

- async int SendAsync(string eventName, string data)

- async int SendBytesAsync(string eventName, byte[]data, int dataOffset, int dataLen)

- async int SendDataAsync(string data)

- async int PingAsync()

- void Close()


## SseEvent (class)

- string name;

- string data;

- string id;

- SseEvent()

- string Name()

- string Data()

- string Id()


## SseText (class)

- static int Find(string s, string needle, int from)
