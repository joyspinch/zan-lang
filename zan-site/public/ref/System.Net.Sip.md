# System.Net.Sip

> 源码: `packages/Zan.Net/src/System/Net/Sip/SipClient.zan`, `packages/Zan.Net/src/System/Net/Sip/SipMessage.zan`


## SipClient (class)

- string host;

- int port;

- string localTag;

- int cseq;

- SipClient(string host, int port, string tag)

- async SipMessage OptionsAsync()

- async SipMessage RegisterAsync(string user, int expires)

- void AddCommon(SipMessage req, string aor, string method)

- async SipMessage TransactAsync(SipMessage req)


## SipHeader (class)

- string name;

- string value;

- SipHeader(string name, string value)


## SipMessage (class)

- bool isRequest;

- string method;

- string uri;

- int status;

- string reason;

- List<SipHeader> headers;

- string body;

- SipMessage()

- static SipMessage Request(string method, string uri)

- public bool GetIsRequest()

- public string GetMethod()

- public string GetUri()

- public int GetStatus()

- public string GetReason()

- public string GetBody()

- SipMessage Add(string name, string headerValue)

- string Header(string name)

- static bool NameEq(string a, string b)

- string Serialize()

- static int ParsePositiveInt(string s)

- static SipMessage Parse(string data)

- static int FindChar(string s, int c, int from)

- static int FindCrlf(string s, int from)
