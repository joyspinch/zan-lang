# System.Net.Coap

> 源码: `packages/Zan.Net/src/System/Net/Coap/CoapClient.zan`


## CoapClient (class)

- string host;

- int port;

- int nextMsgId;

- int ackTimeoutMs;

- int maxRetransmit;

- CoapClient(string host, int port)

- CoapClient SetTimeout(int timeoutMs, int retries)

- async CoapResponse GetAsync(string path)

- async CoapResponse PostAsync(string path, string payload)

- async CoapResponse PutAsync(string path, string payload)

- async CoapResponse DeleteAsync(string path)

- byte[]BuildRequest(int code, string path, string payload, int msgId, int tok, List<int> outLen)

- static CoapResponse ParseResponse(byte[]d, int n, int msgId, int tok)

- async CoapResponse RequestAsync(int code, string path, string payload)


## CoapCode (class)

- static const int GET=1;

- static const int POST=2;

- static const int PUT=3;

- static const int DELETE=4;


## CoapResponse (class)

- int code;

- string payload;

- bool ok;

- CoapResponse(int code, string payload)

- string CodeText()

- static string Two(int v)
