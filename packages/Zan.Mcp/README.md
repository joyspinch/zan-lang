# Zan.Mcp

MCP（Model Context Protocol）**客户端**——说 JSON-RPC 2.0 的那一半。
纯 Zan，无原生依赖。

MCP 是"让程序去用**别的程序**的工具"的一套约定：客户端握手，列出服务端
有哪些工具与资源，然后按名字调用。仓库里的 `tools/mcp_server/McpServer.zan`
是**服务端**那一半；本包是客户端。

## 文件

| 文件 | 内容 |
|---|---|
| `McpClient.zan` | 客户端本体：握手、工具枚举与调用、资源枚举与读取、ping |
| `McpTransport.zan` | 传输契约（`McpTransportFn`）+ 传输结果（`McpTransportResult`） |
| `McpHttpTransport.zan` | HTTP 传输（Streamable HTTP：POST 一段 JSON-RPC） |
| `McpEndpoint.zan` | 服务地址解析（scheme/host/port/path + 本机端点识别） |
| `Models/McpTool.zan` | 远端工具的声明（名字、描述、`inputSchema`） |
| `Models/McpToolResult.zan` | 一次工具调用的结果（文本 + `isError` + 原始内容） |
| `Models/McpServerInfo.zan` | 握手拿到的服务端信息（版本、能力、说明） |
| `Models/McpResource.zan` | 远端资源的描述（uri/名字/说明/媒体类型） |

## 快速上手

```zan
using System;
using System.Ai;
using System.Mcp;

McpClient client = new McpClient("http://127.0.0.1:8080/mcp", "token");
if (!await client.Initialize()) {
    Console.WriteLine(client.lastError);   // 握手失败的原因
    return;
}
Console.WriteLine("服务端：" + client.serverInfo.name
    + " " + client.serverInfo.version
    + "（协议 " + client.serverInfo.protocolVersion + "）");

// 远端工具接进本地工具注册表——模型看到的是**本地**工具。
ToolRegistry reg = new ToolRegistry();
int n = await McpTools.RegisterAll(client, reg, "");
Console.WriteLine("接进来 " + Convert.ToString(n) + " 个工具");

// 资源：先列后读。
List<McpResource> resources = await client.ListResources();
string text = await client.ReadResource(resources[0].uri);
```

`McpTools` 在 `Zan.Ai` 里（`System.Ai`）——它是"远端工具 → 本地注册表"
那一段翻译，需要 `ToolRegistry`。

## 协议

握手 → 就绪 → 干活：

```
→ {"jsonrpc":"2.0","id":1,"method":"initialize","params":{...}}
← {"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18",...}}
→ {"jsonrpc":"2.0","method":"notifications/initialized"}
← 202（通知没有响应体）
→ {"jsonrpc":"2.0","id":2,"method":"tools/list"}
← {"jsonrpc":"2.0","id":2,"result":{"tools":[...]}}
→ {"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"...","arguments":{...}}}
← {"jsonrpc":"2.0","id":3,"result":{"content":[{"type":"text","text":"..."}],"isError":false}}
```

也支持 `resources/list`、`resources/read`、`ping`。

## 六个必须做对的地方

1. **握手是前置条件**。协议规定 `initialize` 之后才能发别的请求，所以除
   `Initialize` 外的公开方法在没握手时直接失败（"还没有握手"），**不偷偷
   把握手补上**——那会让调用方看不出握手当初失败了。

2. **响应 id 必须对上**。id 不匹配就报错，不拿一份对不上的结果当答案
   （代理串了、服务端有 bug，都会走到这里）。

3. **通知没有 id**。`notifications/initialized` 是**通知**：没有 `id`
   字段，服务端回 202 + 空正文。客户端不解析它的正文。

4. **远端工具失败是结果，不是异常**。`tools/call` 回 `isError: true`
   表示工具自己失败了——那是**结果的一部分**，调用方照常拿到结果对象。
   只有传输失败 / 协议失败才返回 null。

5. **失败用返回值不用异常**。网络失败、服务端报错、响应坏了，一律返回
   null（或 false / 空列表）并把原因写进 `lastError`。MCP 服务端是**外部
   依赖**——连不上是常态，不是异常情况。

6. **一次一条请求**。协议 2025-06-18 已经**取消了 JSON-RPC 批处理**，所以
   客户端一次发一条、收一条，id 严格自增。

## 传输是可插拔的

`McpTransportFn` 是"把一段请求文本送出去、拿回原始响应"的委托。默认实现
是 HTTP（`McpHttpTransport`），也可以注入自己的：

```zan
McpClient client = new McpClient(myTransport);
```

**为什么留这个口子**：协议逻辑（握手、枚举、错误语义、id 对齐）不该只能拿
真服务器验证。注入一个假传输就能把请求形状与响应处理钉死——`tests/
conformance/mcp_client.zan` 就是这么做的（一个用例把整条协议链路连同七条
错误路径全钉住，不依赖任何跑着的服务）。

## 已知边界

- **只有 HTTP 传输**。MCP 还有 stdio 传输（起子进程、读写它的
  stdin/stdout），但仓库里目前**没有进程 API**（`System.AppPath
  .GetCurrentProcessId` 是唯一沾边的），起不来也读不了。协议层不受影响：
  传输是可插拔的，等进程 API 落地后换一个 `McpTransportFn` 即可。
- **不重试**。`tools/call` 不是幂等的，重试可能把一个有副作用的工具执行
  两遍。
- **没有会话管理**。MCP 的 Streamable HTTP 传输有 `Mcp-Session-Id` 那套
  会话语义，本客户端不实现（多数服务端不需要）。
- **没有 SSE 传输**。服务端可以走 `text/event-stream` 推消息（服务端主动
  发通知），本客户端只做请求-响应。
- **没有 prompts**。`prompts/list` 服务端返回空数组，本客户端不暴露它。
- **地址不带白名单**。白名单是**出网策略**（谁允许连哪里），属于部署方
  的决定；本包只做解析。本机端点（127.0.0.1）单独标出来，让传输层知道
  "这是本机服务"。
