# System.Net.Rpc

> 源码: `stdlib/System/Net/Rpc/IRpcCodec.zan`, `stdlib/System/Net/Rpc/IRpcTransport.zan`, `stdlib/System/Net/Rpc/JsonRpcCodec.zan`, `stdlib/System/Net/Rpc/RpcClient.zan`, `stdlib/System/Net/Rpc/RpcError.zan`, `stdlib/System/Net/Rpc/RpcRequest.zan`, `stdlib/System/Net/Rpc/RpcResponse.zan`, `stdlib/System/Net/Rpc/RpcServer.zan`, `stdlib/System/Net/Rpc/TcpRpcTransport.zan`, `stdlib/System/Net/Rpc/XmlRpcCodec.zan`


## JsonRpcCodec (class)

JSON-RPC 2.0 codec。对中立的 RpcRequest/RpcResponse 模型进行编解码
为标准 `{"jsonrpc":"2.0", ...}` 封装。批处理请求超出
本 codec 范围（每帧一条消息）；上层可自行循环。

- string Name()

- string EncodeRequest(RpcRequest req)

- RpcResponse DecodeResponse(string frame)

- RpcRequest DecodeRequest(string frame)

- string EncodeResponse(RpcResponse resp)


## RpcClient (class)

RPC 核心：负责 request/response 生命周期与关联 id
分配，并把这两个正交关注点委托给可插拔组件——
线上格式交给 `IRpcCodec`，字节/帧交给
`IRpcTransport`。它对 TCP、JSON、XML 一无所知，因此
不同协议只是不同 codec，不同介质只是不同
transport。通知即发即弃（无 id、无回复）。

用法：
RpcClient c = await RpcClient.ConnectAsync("127.0.0.1", 4000, new JsonRpcCodec());
JsonValue p = JsonValue.NewArray(); p.Append(JsonValue.NewNum("2"));
RpcResponse r = await c.CallAsync("add", p);
if (!r.IsError()) { int sum = r.result.AsInt(0); }
c.Close();

- IRpcTransport transport;

- IRpcCodec codec;

- int nextId;

- RpcClient(IRpcTransport transport, IRpcCodec codec)
  - 在任意 transport + codec 之上构建客户端（最通用的形式）。

- static async RpcClient ConnectAsync(string host, int port, IRpcCodec codec)
  - 便捷方法：使用给定 codec 通过换行帧 TCP 连接。

- async RpcResponse CallAsync(string method, JsonValue args)
  - 发送请求并等待其关联回复。id 在此分配，
    调用方无需自行管理关联。

- async void NotifyAsync(string method, JsonValue args)
  - 发送即发即弃的通知（无 id、不等待回复）。

- bool IsOpen()

- void Close()


## RpcError (class)

与 transport/协议无关的 RPC 错误。`code == 0` 表示"无错误"；
已知的负错误码与 JSON-RPC 2.0 对应，任何 codec 都可映射到它们
（XML-RPC faults、DMTP 状态码等）。这是核心和每个适配器
共享的唯一错误模型，调用方可统一处理失败。

- int code;

- string message;

- RpcError(int c, string m)

- static RpcError None()
  - 标准的"无错误"值。

- static RpcError ParseError(string m)

- static RpcError InvalidRequest(string m)

- static RpcError MethodNotFound(string m)

- static RpcError InvalidParams(string m)

- static RpcError Internal(string m)

- static RpcError Transport(string m)

- static RpcError Timeout(string m)

- bool IsError()


## RpcMethod (class)

已注册的方法：名称与要调用的处理函数。建模为
实体（而非平行的名称/处理函数列表），以保证注册表一致。

- string name;

- RpcHandler fn;

- RpcMethod(string name, RpcHandler fn)


## RpcRequest (class)

与协议无关的 RPC 请求：方法名、JSON 参数负载和
关联 id。`notify == true` 表示即发即弃调用（无 id、无
预期响应）。每个 codec（JSON-RPC / XML-RPC / DMTP）都从这一形态
编码和解码，核心因此从不依赖具体线上格式。
参数负载使用 `JsonValue`，因为它是标准库中通用
的结构化值模型；XML codec 遍历的是同一棵
树。

- string method;

- JsonValue args;

- int id;

- bool notify;

- RpcRequest(string method, JsonValue args, int id, bool notify)

- static RpcRequest Call(string method, JsonValue args, int id)
  - 期望响应的请求（`notify == false`）。

- static RpcRequest Notification(string method, JsonValue args)
  - 即发即弃的通知（无 id、无响应）。


## RpcResponse (class)

与请求通过 `id` 关联、协议无关的 RPC 响应。成功时
`result` 携带返回负载，`error` 为 `RpcError.None`；
失败时 `result` 为 null，`error` 描述错误。每个 codec
都把线上回复解码为该形态，调用方只需判断一次 `IsError()`
即可，与协议无关。

- int id;

- JsonValue result;

- RpcError error;

- RpcResponse()

- static RpcResponse Ok(int id, JsonValue result)

- static RpcResponse Fail(int id, RpcError error)

- bool IsError()


## RpcServer (class)

与协议无关的 RPC 服务器：接受连接，并用客户端相同的
`IRpcTransport` + `IRpcCodec` 层做帧化/解码
，按方法名分发到已注册的 `RpcHandler`，
并写回对应的响应（通知无响应）。每个对端
在各自协程上运行（Task.Spawn），慢客户端不会阻塞 accept
循环——与 HttpServer 相同的并发模型。更换 codec 即可
在相同分发核心上提供 XML-RPC / DMTP 服务。

用法：
RpcServer s = new RpcServer("0.0.0.0", 4000, new JsonRpcCodec());
s.On("add", AddHandler);
await s.Start();

- TcpListener listener;

- IRpcCodec codec;

- List<RpcMethod> methods;

- string host;

- int port;

- bool running;

- RpcServer(string host, int port, IRpcCodec codec)

- RpcServer On(string name, RpcHandler fn)
  - 为 `name` 注册处理函数（可链式调用）。

- RpcHandler Lookup(string name)

- async void Start()
  - 持续接受连接；每个连接在各自协程上服务。

- async void HandlePeerAsync(nint client)
  - 服务一个对端：读帧 -> 解码 -> 分发 -> 回复，直到关闭。

- async RpcResponse Dispatch(RpcRequest req)
  - 把请求路由到处理函数，找不到时返回 MethodNotFound 错误。

- void Stop()


## TcpRpcTransport (class)

TCP 连接上的换行分隔帧——文本 RPC 协议（JSON-RPC/XML-RPC）
在原始套接字上常见的行帧。每条消息
后跟 '\n' 发送；接收时，完整帧之外的字节会缓冲
供下次读取，同一包中的连续帧也能正确处理。
因此负载不能包含字面换行（标准库 codec 生成的紧凑 JSON/XML
从不包含）。

- TcpClient conn;

- string rbuf;

- TcpRpcTransport()

- static async TcpRpcTransport ConnectAsync(string host, int port)

- static TcpRpcTransport FromConn(TcpClient conn)
  - 包装已连接的 TcpClient（如服务器接受的对端）。

- async int SendFrameAsync(string payload)

- async string RecvFrameAsync()

- static int IndexOfNewline(string s)

- bool IsOpen()

- void Close()


## XmlRpcCodec (class)

XML-RPC codec。把中立的 RpcRequest/RpcResponse 模型映射到
经典的 `<methodCall>` / `<methodResponse>` 封装，复用 JsonValue 作为
结构化值模型（int/double/boolean/string/array/struct <->
number/bool/string/array/object）。XML-RPC 线上无请求 id，
因此响应以 id 0 解码；核心按连接上的请求/响应顺序关联。
这证明了 codec 维度的设计：同一 RpcClient/RpcServer
仅更换 codec 即可讲 XML-RPC。

- string Name()

- static bool IsDouble(string raw)

- static string XmlEscape(string s)

- static void WriteValue(StringBuilder sb, JsonValue v)

- string EncodeRequest(RpcRequest req)

- string EncodeResponse(RpcResponse resp)

- RpcResponse DecodeResponse(string frame)

- RpcRequest DecodeRequest(string frame)


## XmlRpcReader (class)

XML-RPC 文档上的游标。刻意限定在 XML-RPC 值
语法内（并非通用 XML 解析器）：遍历 value/type/array/struct 节点
并容忍真实服务器在标签之间输出的空白。

- string s;

- int pos;

- int len;

- static XmlRpcReader Of(string src)

- int CurB()

- void SkipWs()

- bool SeekOpen(string tag)
  - 把 pos 移到下一个 `<tag` 开元素的 '<' 处，找不到返回 false。

- bool ConsumeHead()
  - 消费当前标签头直到并包括 '>'；若标签
    为自闭合（"<tag/>"）则返回 true。

- string ReadElementOpen()
  - pos 必须在 '<' 处；读取元素名并消费标签头。

- string ReadTextUntilLt()
  - 从 pos 读取原始文本直到下一个 '<'。

- bool LookingAtCloseOf(string name)

- bool LookingAtOpenOf(string name)

- void ConsumeClose(string name)
  - 消费 pos 处的闭合标签 `</name>`（可先跳过空白）。

- static string Unescape(string s)

- JsonValue ReadValue()
  - pos 必须在 '<value'。读取完整 XML-RPC 值，使 pos 越过
    闭合的 `</value>`。

- JsonValue ReadTyped()
  - pos 位于类型元素（int/double/boolean/string/array/struct）的 '<' 处。


## RpcResponse (delegate)

处理一条已解码的请求并生成要发送的响应。
异步以便处理函数可等待 I/O 而不阻塞对端协程。

`delegate RpcResponse RpcHandler(RpcRequest req);`


## IRpcCodec (interface)

RPC 栈的线上格式维度：把中立的 request/response
模型与带帧字符串相互转换。JSON-RPC、XML-RPC、DMTP 各提供一个实现；
核心（`RpcClient` / 服务器循环）
只依赖这一契约，因此新协议只需新 codec，无需新客户端
。Codec 是纯同步的——所有 IO 都在 transport 层。

- string Name();
  - 人类可读的协议名（如 "json-rpc-2.0"）。

- string EncodeRequest(RpcRequest req);
  - 把请求序列化为线上文本形式（不含帧字节）。

- RpcResponse DecodeResponse(string frame);
  - 把回复帧解析为 response（错误通过 RpcError 抛出）。

- RpcRequest DecodeRequest(string frame);
  - 服务端：解析入站请求帧。

- string EncodeResponse(RpcResponse resp);
  - 服务端：把响应序列化为线上文本形式。


## IRpcTransport (interface)

RPC 栈的字节/帧维度：一次投递一条完整消息
到某个连接上，隐藏底层流（TCP 套接字、命名
管道等）和消息的分隔方式（换行、长度前缀等）。
核心和 codec 从不会看到半截读或帧字节——它们把
负载交给 `SendFrameAsync`，再从
`RecvFrameAsync` 拿回完整负载。新介质（NamedPipe）或二进制帧
（DMTP）只需新实现该接口。

- async int SendFrameAsync(string payload);
  - 发送一条完整消息负载（帧由内部添加）。

- async string RecvFrameAsync();
  - 接收下一条完整消息负载；对端关闭时返回 ""。

- bool IsOpen();

- void Close();
