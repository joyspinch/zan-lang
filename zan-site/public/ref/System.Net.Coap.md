# System.Net.Coap

> 源码: `stdlib/System/Net/Coap/CoapClient.zan`


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

- static CoapResponse ParseResponse(byte[]d, int n, int msgId, int tok)

- async CoapResponse RequestAsync(int code, string path, string payload)
  - 发送一个可确认请求并等待匹配的
    响应，按 RFC 7252 重传。当所有重试都超时，返回 code 为 0
    （ok=false）的响应。


## CoapCode (class)

CoAP 方法/响应码（RFC 7252 第 12.1 节）。

- static int GET=1;

- static int POST=2;

- static int PUT=3;

- static int DELETE=4;


## CoapResponse (class)

解析后的 CoAP 响应：<c>code</c> 为 class.detail 字节
（如 69 = 2.05 Content、132 = 4.04 Not Found），<c>payload</c> 为
0xFF 标记之后的字节（无则为 ""），2.xx 类时 <c>ok</c> 为 true；
<c>CodeText</c> 以带点形式输出，供日志使用。

- int code;

- string payload;

- bool ok;

- CoapResponse(int code, string payload)

- string CodeText()
  - 响应码的 "2.05" 式带点渲染。

- static string Two(int v)
