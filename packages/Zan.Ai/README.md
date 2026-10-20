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
- `System.Ai` — `VectorStore`（**内存**向量存储：embedding →
  `VectorIndex` 适配、入库/检索、导出导入往返）
- `System.Ai` — `Retriever`（**内存**混合检索：向量语义 + BM25 字面，
  RRF 融合）
- `System.Ai` — `ZanDbRetriever`（**持久化**混合检索：文档与索引存在
  `ZanDb` 里，走事务 + 检查点 + 原生混合检索。**生产路径用这个**）
- `System.Ai` — `RemoteZanDbRetriever`（**远端**混合检索：文档与索引在
  另一台机器的 `ZanDbServer` 上，客户端走 `ZanDbClient` 的 TCP 协议。
  **多机部署用这个**）
- `System.Ai` — `Rag`（编排：分块 → 向量化 → 入库 → 检索 → 拼提示词 →
  生成；答案带引用溯源）
- `System.Ai` — 实体（`src/System/Ai/Models/`）：`AiConfig` /
  `AiUsage` / `AiPricing` / `AiChunk` / `AiEmbedding` / `RetrieverHit`

## 快速上手

```zan
using System;
using System.Ai;
using System.Text;

AiConfig cfg = new AiConfig("https://api.deepseek.com/v1", "sk-...", "deepseek-chat");
AiClient client = new AiClient(cfg);

// ---- 建库（离线索引）----
VectorStore store = new VectorStore(1024, true);   // 维度按你的 embedding 模型
Bm25Index keyword = new Bm25Index(new TextTokenizer());

Rag rag = new Rag(client, new Retriever(store, keyword));
rag.SetChunking(800, 120);
int chunks = await rag.Index(store, keyword, documentText);
Console.WriteLine("入库块数=" + chunks);

// ---- 问答（在线）----
RagAnswer answer = await rag.Ask("退款要多久？", store);
if (answer == null) {
    Console.WriteLine(rag.lastError);
} else {
    Console.WriteLine(answer.text);
    for (int i = 0; i < answer.citations.Count; i = i + 1) {
        // 引用能指回原文——这是 RAG 与"整篇塞进提示词"的本质差别
        RagCitation c = answer.citations[i];
        Console.WriteLine("[" + c.id + "] " + c.text);
    }
}
```

不想要 RAG 这层编排时，三个零件也能单独用：

```zan
// 1) 文档分块（带原文偏移，检索到后可还原上下文）
List<AiChunk> chunks = Chunking.Recursive(doc, 800, 120);
Console.WriteLine("块数=" + chunks.Count + " 首块偏移=" + chunks[0].start);

// 2) 向量化
AiEmbedding vecs = await Embeddings.EmbedChunks(client, chunks, true);
Console.WriteLine("维度=" + vecs.dimensions);

// 3) 余弦检索（配合 Zan.Data 的 VectorIndex 存储）
double sim = AiEmbedding.Cosine(vecs.At(0), queryVector);
```

## 三档后端：怎么选

| | `VectorStore` + `Retriever`（内存） | `ZanDbRetriever`（本地） | `RemoteZanDbRetriever`（远端） |
|---|---|---|---|
| 存储 | 进程内存 | `ZanDb` collection（本地文件） | 远端 `ZanDbServer` |
| 并发写 | **不支持** | 支持（`AsyncRwLock` + 事务 + journal） | 支持（服务端的锁） |
| 崩溃恢复 | 无（索引全丢） | 有（重开即恢复，`RebuildSearchIndexes` 可重建） | 有（服务端负责） |
| 文档与索引一致性 | 手动维护 | 同一事务，不会脱节 | 同一事务（服务端） |
| 网络往返 | 无 | 无 | 每次操作一次往返 |
| 适合 | 小规模、单写者、一次性脚本、测试夹具 | 单机生产 | 多机共享一份知识库 |

**为什么内存版不能并发写**：`VectorIndex.Add` 会改 `chunks`/`nodes`/
`idSlots` 三个共享结构且**没有锁**——并发写会撕裂 HNSW 图（不是"结果
不准"，是内存结构损坏）。ZanDb 那侧由 `AsyncRwLock` 串行化写、并
通过 **journal + 检查点**让索引变更与文档写入落在同一事务里。

**三档语义一致**：用同一个 RRF 公式（k=60）融合向量路与字面路，命中
实体都是 `RetrieverHit`。conformance 里有一条断言专门盯这个——三个
后端对同一批数据给出**完全相同的融合分数**（0.032787），所以从内存
切到本地、再切到远端，检索结果不会悄悄变样。

本地档的用法：

```zan
using System.Data.ZanDb;

ZanDatabase db = ZanDatabase.Open("docs.zz", ZanStore.DUR_SYNC);
ZanDbRetriever store = new ZanDbRetriever(db, "docs", 1024);  // 维度按模型
store.SetNormalized(true);
store.EnsureIndexes();                     // 先建索引再写数据

// 一篇文档一个块：正文进 BM25，向量进 HNSW
store.AddAll(texts, vectors);              // 批量写（一次事务）

// 混合检索
List<RetrieverHit> hits = store.Search(queryVector, "退款", 5, 64);
for (int i = 0; i < hits.Count; i = i + 1) {
    Console.WriteLine("[" + hits[i].id + "] " + store.TextOf(hits[i].id));
}

// 要两路的原始排名与分数时用 SearchSplit
List<RetrieverHit> split = store.SearchSplit(queryVector, "退款", 5, 64);
// split[i].vectorRank / bm25Rank / vectorDistance / bm25Score
```

远端档的用法（服务端跑 `ZanDbServer`，客户端跑 `ZanDbClient`）：

```zan
using System.Data.ZanDb;

ZanDbClient client = new ZanDbClient("10.0.0.5", 19740, token, "my-app");
RemoteZanDbRetriever store = new RemoteZanDbRetriever(client, "docs", 1024);
store.SetNormalized(true);
await store.EnsureIndexesAsync("init-1");  // 两个 ensure 内部派生出两个 id

await store.AddAllAsync(texts, vectors, "batch-1");

List<RetrieverHit> hits = await store.SearchAsync(queryVector, "退款", 5, 64);
for (int i = 0; i < hits.Count; i = i + 1) {
    Console.WriteLine("[" + hits[i].id + "] " + await store.TextOfAsync(hits[i].id));
}
```

**远端写入的 request id 语义**（服务端按 id 绑定请求指纹）：
- **一个 id 只能配一个请求**——同一个 id 配不同请求体会被判
  `id_conflict`（不是静默覆盖，也不是当成两次独立写入）。所以
  `EnsureIndexesAsync` 内部给文本索引和向量索引派生了两个 id。
- **同一个请求重试要用同一个 id**——这才是幂等：网络超时后重试不会
  写重。所以 `NextRequestId` 要在重试循环**外面**取一次。

## 设计要点

- **实体化，不用平行数组**：向量、分块、请求/响应、检索命中都是具名
  实体（`AiEmbedding`/`AiChunk`/`AiUsage`/`RetrieverHit`），不靠下标
  对齐。跨边界传输的 JSON 尽早解析成实体。
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
- **混合检索 + RRF 融合**：纯向量认不出专有名词，纯 BM25 认不出同义
  表达。RRF 只看排名不看分数（`Σ 1/(k+rank)`），对量纲免疫——向量距离
  与 BM25 分数本来就不在同一量纲，加权求和必须先归一化，而每种归一化
  方式都有坑。两路的原始排名与分数都保留在 `RetrieverHit` 上。
- **端点解析不带白名单**：白名单是 Web 框架的出网策略
  （`Zan.Mvc` 的 `AiEndpointPolicy`），本包是通用库，调用方自己决定
  连哪里。
- **`double` → `float` 的精度代价**：向量存储用 `VectorIndex` 的
  `float[]`（单精度，因为索引要常驻内存、量大）。实测 embedding 分量
  相对误差约 1e-8、余弦偏差约 1e-9，对检索排序没有可观察影响；但要
  与服务端原始向量逐位一致时**别经过这层**，直接用
  `AiEmbedding.vectors`。

## 消费者

- 任何做 RAG / 文档问答 / 语义检索的 Zan 程序
- `Zan.Mvc/Framework/Ai` 可作为薄壳转调本包（不破坏现有调用）

## 已知边界

- 无重排层（cross-encoder 二次排序）、无 Tool calling、无会话记忆、
  无评估与可观测——这些是后续的档位。
- 远端档每次操作一次网络往返，**没有连接池化、没有批量检索**
  （`ZanDbClient` 每次 `SendAsync` 新建连接）。高 QPS 场景要在调用方
  做批量与缓存。
- `Rag` 编排层目前只接**内存档**的检索器（`Rag(client, Retriever)`）；
  接本地/远端档要自己把检索结果喂进提示词——`Rag` 还没有后端无关的
  检索器接口。
- Embeddings 只支持 OpenAI 兼容的 `/v1/embeddings` 协议（DeepSeek、
  OpenAI、Ollama、vLLM、LM Studio 等都兼容这个协议）；不做各厂商
  的私有嵌入协议。
- 成本是估算（单价表由调用方提供，token 数是启发式估算）；无 Token
  精确计数（要接厂商的 tokenizer）。
- 流式没有背压：回调返回 false 只能停在这一帧读完之后。
