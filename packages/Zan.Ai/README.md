# Zan.Ai

AI 应用管线的**组装线**。Zan 原本已有零件（`Zan.Text/TextTokenizer`
分词器、`Zan.Text/Bm25Index` 全文检索、`Zan.Data/ZanDb/VectorIndex`
HNSW 向量索引、`Zan.Net` 的 `HttpClient`），但没有把它们串成"文档 →
分块 → 向量化 → 检索 → 拼提示词 → 生成"的链条——零件全有，组装线
没有。本包补的就是这条组装线。

**不依赖 `Zan.Mvc`**：`Zan.Mvc/Framework/Ai` 那 7 个文件是 Web 框架内的
一小块（绑死在 `HttpContext` 与 `Settings` 上），桌面/游戏/CLI 场景
用不了。本包是独立的，Web 侧可以薄壳转调。

## 内容（namespace → 内容）

- `System.Ai` — `AiClient`（统一 LLM 客户端：多厂商端点、流式/非流式、
  重试、超时预算、Token 计数与成本核算）
- `System.Ai` — `Embeddings`（OpenAI 兼容 `/v1/embeddings`：批量、
  按 index 归位、向量归一化、维度校验）
- `System.Ai` — `Chunking`（文档分块：递归字符分割 / token 窗口 /
  句子 / 段落）
- `System.Ai` — `AiEndpoint`（base URL → scheme/host/port/path；
  本地端点识别）
- `System.Ai` — 实体（`src/System/Ai/Models/`）：`AiConfig` /
  `AiUsage` / `AiPricing` / `AiChunk` / `AiEmbedding`

## 快速上手

```zan
using System.Ai;

AiConfig cfg = new AiConfig("https://api.deepseek.com/v1", "sk-...", "deepseek-chat");
AiClient client = new AiClient(cfg);

// 1) 文档分块（带原文偏移，检索到后可还原上下文）
List<AiChunk> chunks = Chunking.Recursive(doc, 800, 120);
Console.WriteLine("块数=" + chunks.Count + " 首块偏移=" + chunks[0].start);

// 2) 向量化
AiEmbedding vecs = await Embeddings.EmbedChunks(client, chunks, true);
Console.WriteLine("维度=" + vecs.dimensions);

// 3) 余弦检索（配合 Zan.Data 的 VectorIndex 存储）
double sim = AiEmbedding.Cosine(vecs.At(0), queryVector);

// 4) 补全
string answer = await client.Complete("你是助手", "问题");
if (answer == null) { Console.WriteLine(client.lastError); }
```

## 设计要点

- **实体化，不用平行数组**：向量、分块、请求/响应都是具名实体
  （`AiEmbedding`/`AiChunk`/`AiUsage`），不靠下标对齐。跨边界传输的
  JSON 尽早解析成实体。
- **失败用返回值不用异常**：网络失败是常态（超时、断网、5xx），
  `Complete` 返回 `null` + `client.lastError` 给出可读原因。
- **4xx 不重试；POST 补全默认不重试**（`maxAttempts` 默认 1）：
  补全没有幂等键，重试可能重复计费。5xx 与网络错误才重试
  （指数退避，上限 2 秒）。
- **嵌入响应按 `index` 归位**，不靠数组顺序——某个网关重排了数组，
  靠顺序就是全部错位。维度不一致直接报错，**不截断和稀泥**。
- **token 估算单一口径**：`Chunking.TokenCost`（4 倍刻度：CJK 4、
  ASCII 1、其它 2）与 `AiClient.EstimateTokens` 是同一把尺子。
  两处口径不一致会让"切出来说每块 100 token，一算却是 25"。
  估算是估算，`AiUsage.estimated` 标出来。
- **成本核算用模型单价表**：`AiPricing` 存每百万 token 的价，
  `AiUsage.Cost(pricing)` 算钱。**价格由调用方传入**——本包不内置
  会过期的价格。
- **偏移单位是字节不是字符**：Zan 的 `string` 是 UTF-8 字节序列，
  `AiChunk.start/end` 与 `string.Substring` 同口径。切分点落在字符
  边界上（劈开多字节字符会产出替换字符）。
- **端点解析不带白名单**：白名单是 Web 框架的出网策略
  （`Zan.Mvc` 的 `AiEndpointPolicy`），本包是通用库，调用方自己决定
  连哪里。

## 消费者

- 任何做 RAG / 文档问答 / 语义检索的 Zan 程序
- `Zan.Mvc/Framework/Ai` 可作为薄壳转调本包（不破坏现有调用）

## 已知边界

- 无 RAG 编排（检索→重排→拼提示词→生成的全链条）、无向量库客户端
  （只有本包内的 `Embeddings` 产出向量，存与检索要靠 `Zan.Data` 的
  `VectorIndex` 自己接）、无 Tool calling、无会话记忆、无评估与
  可观测——这些是后续的档位。
- Embeddings 只支持 OpenAI 兼容的 `/v1/embeddings` 协议（DeepSeek、
  OpenAI、Ollama、vLLM、LM Studio 等都兼容这个协议）；不做各厂商
  的私有嵌入协议。
- 成本是估算（单价表由调用方提供，token 数是启发式估算）；无 Token
  精确计数（要接厂商的 tokenizer）。
- 流式没有背压：回调返回 false 只能停在这一帧读完之后。
