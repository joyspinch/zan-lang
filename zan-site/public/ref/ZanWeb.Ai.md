# ZanWeb.Ai

> 源码: `packages/Zan.Mvc/src/ZanWeb/Framework/Ai/Ai.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Ai/AiEndpointPolicy.zan`


## Ai (class)

- static List<AiVendor> Vendors()

- static List<AiAgent> Agents()

- static AiAgent AgentOf(string code)

- static async bool Ready()

- static async AiReply Ask(string agentCode, string question)

- static async AiReply AskConv(string agentCode, string historyJson, string question)

- static async AiReply CompleteRaw(string systemPrompt, string question)

- static string Messages(AiAgent agent, string historyJson, string question)

- static ExternalCallPolicy CallPolicy(AiEndpoint ep, bool streaming)

- static async int WaitStreamProducer(long producer)

- static async AiReply Complete(AiEndpoint ep, string key, string model, string temp, string messages)

- static async AiReply StreamConv(HttpContext ctx, string agentCode, string historyJson, string question)

- static string Delta(string line)

- static async List<string> Models(string baseUrl, string key)

- static async AiReply ApplySchema(string sql)

- static string Number(string v)


## AiAgent (class)

- string code;

- string name;

- string hint;

- string icon;

- bool apply;

- string prompt;

- AiAgent(string code, string name, string hint, string icon, bool apply, string prompt)


## AiDeltaDoc (class)

- string text;


## AiEndpoint (class)

- string host;

- int port;

- string path;

- bool tls;

- string Host()

- int Port()

- string Path()

- bool Tls()

- bool Local()

- string Canonical()

- static AiEndpoint Parse(string url)


## AiEndpointPolicy (class)

- static bool IsAllowed(string url)

- static bool IsRemoteAllowed(string url)

- static bool Exact(string url, string expected)

- static bool IsLocal(string url)


## AiReply (class)

- bool ok;

- string text;

- static AiReply Good(string text)

- static AiReply Bad(string why)


## AiVendor (class)

- string code;

- string name;

- string baseUrl;

- string model;

- string signup;

- AiVendor(string code, string name, string baseUrl, string model, string signup)
