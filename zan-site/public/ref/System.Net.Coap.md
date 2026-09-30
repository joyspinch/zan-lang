# System.Net.Coap

> 源码: `packages/Zan.Net/src/System/Net/Coap/CoapClient.zan`


## CoapClient (class)

基于 UDP 的 CoAP 客户端（RFC 7252）——标准的请求/响应协议
用于资源受限的 IoT 设备，默认端口 5683。

请求为可确认（CON）类型，遵循规范的重传计划
（初始超时 2 秒，每次重试翻倍，共 4 次重传），因此丢失的数据报
会被重试，失效的端点会干净地报错而不是挂死。响应
通过 message id（ACK 捎带）或 token（独立应答）进行匹配。

用法：
CoapClient c = new CoapClient("192.168.1.50", 5683);
CoapResponse r = await c.GetAsync("sensors/temp");
if (r.ok) { Console.WriteLine(r.payload); }
CoapResponse w = await c.PutAsync("actuators/led", "on");

- string host;

- int port;

- int nextMsgId;

- int ackTimeoutMs;

- int maxRetransmit;

- CoapClient(string host, int port)
  - 为一个 CoAP 端点创建客户端。

- CoapClient SetTimeout(int timeoutMs, int retries)
  - 设置初始 ACK 超时（每次重传翻倍）以及
    重传次数。最坏等待时间为 timeoutMs * (2^retries - 1)。

- async CoapResponse GetAsync(string path)
  - GET 一个资源路径，如 "sensors/temp"。

- async CoapResponse PostAsync(string path, string payload)
  - 向资源路径 POST 一个负载。

- async CoapResponse PutAsync(string path, string payload)
  - 向资源路径 PUT 一个负载。

- async CoapResponse DeleteAsync(string path)
  - DELETE 一个资源路径。

- byte[]BuildRequest(int code, string path, string payload, int msgId, int tok, List<int> outLen)
  - 构建一个 CON 请求数据报：4 字节头部、2 字节 token、
    Uri-Path 选项（按 '/' 分隔的每个段一个），然后是 0xFF + payload。
    实际长度写入 <paramref name="outLen"/>[0]。

- static CoapResponse ParseResponse(byte[]d, int n, int msgId, int tok)
  - 解析响应数据报（<paramref name="d"/> 中有
    <paramref name="n"/> 个有效字节）。数据报不属于本次请求
    （message id 与 token 均不匹配）或只是空 ACK 时返回 null。

- async CoapResponse RequestAsync(int code, string path, string payload)
  - 发送一个可确认请求并等待匹配的
    响应，按 RFC 7252 重传。当所有重试都超时，返回 code 为 0
    （ok=false）的响应。


## CoapCode (class)

CoAP 方法/响应码（RFC 7252 第 12.1 节）。

- static const int GET=1;
  - GET（0.01）。

- static const int POST=2;
  - POST（0.02）。

- static const int PUT=3;
  - PUT（0.03）。

- static const int DELETE=4;
  - DELETE（0.04）。


## CoapResponse (class)

解析后的 CoAP 响应：<c>code</c> 为 class.detail 字节
（如 69 = 2.05 Content、132 = 4.04 Not Found），<c>payload</c> 为
0xFF 标记之后的字节（无则为 ""），2.xx 类时 <c>ok</c> 为 true；
<c>CodeText</c> 以带点形式输出，供日志使用。

- int code;

- string payload;

- bool ok;

- CoapResponse(int code, string payload)
  - 私有构造：ok 按 code 是否属于 2.xx 类判定。

- string CodeText()
  - 响应码的 "2.05" 式带点渲染。

- static string Two(int v)
  - 两位十进制渲染（detail 部分不足两位补零）。
