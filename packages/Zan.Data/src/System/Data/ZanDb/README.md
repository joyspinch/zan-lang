# ZanDb

文档数据库与知识库存储：存储原生 Zan 值树（`JsonValue`：整数、浮点、布尔、
字符串、嵌套对象、数组），提供中文分词、倒排 BM25、HNSW 向量与混合检索。
可以嵌入一个进程，也可以由 `ZanDbServer` 持库、多个客户端进程访问。
底层 KV 内核 **ZanStore** 使用原子批次 WAL、不可变有序段和双槽清单。
块缓存和 memtable 有容量配置；常驻倒排表、近邻图与键索引随数据量增长。

> 2026-09 全面重写。旧引擎（内存 B+Tree + CoW Pager + WAL，跨进程
> 多写者）因并发性能与存储体积不可接受而被整体替换，无兼容层；
> 旧数据文件不迁移。

## 快速上手

```zan
using System.Data.ZanDb;
using System.Json;

ZanDatabase db = ZanDatabase.Open("app.zdb");
Collection users = db.GetCollection("users");

JsonValue u = JsonValue.NewObject();
u.Put("name", JsonValue.NewStr("alice"));
u.Put("age", JsonValue.NewNum("30"));
u.Put("cat", JsonValue.NewStr("admin"));
int id = users.Insert(u);

JsonValue back = users.FindById(id);          // 主键点读
users.EnsureIndex("cat");                     // 二级索引（惰性/分批构建）
List<JsonValue> admins = users.FindByIndex("cat", "admin");

// 投影式查询：只解码用到的字段，不构建整棵文档树
int n = users.ScanField("cat", (int id, JsonValue v) => {   // 逐文档回调
    return true;                              // 返回 false 提前结束
});
List<int> ids = users.FindIdsByField("cat", "admin");       // 只取 id（索引就绪时不读文档）
List<int> range = users.FindIdsByFieldRange("cat", "a", "m"); // 范围 [a,m] 双端含（序数语义）

// 类型化投影与聚合：免 JsonValue 树，扫描直出 long/string
long total = users.SumField("age");           // 数值列求和（流式直算）
Dictionary<string, int> byCat = users.CountGroups("cat");  // GROUP BY COUNT
bool hasMin = users.MinField("age", out mn);  // MIN/MAX（无数值文档返回 false）
double avg = users.AvgField("age");           // AVG（一遍同时累计和与计数）
List<int> top = users.FindTopIds("score", 10); // TopN：有界最小堆，等值 id 小者在前
int hits2 = users.ScanStrField("cat", (int id, string v) => { // 免树投影
    return true;
});

db.Begin();                                   // 批量写入合并为一次原子提交
for (int i = 0; i < 1000; i = i + 1) { users.Insert(MakeDoc(i)); }
db.Commit();

db.MergeSegments();                           // 可选：回收空间、压掉旧版本
db.MergeSomeSegments(4);                      // 或增量归并最老 4 段（写阻塞有界）
db.SetAutoMerge(6, 4);                        // 或自动挡：固化后段数达 6 就归并最老 4 段
db.Close();
```

## 中文知识库检索

```zan
using System;
using System.Data.ZanDb;
using System.Json;
using System.Text;

ZanDatabase db = ZanDatabase.Open("knowledge.zdb", ZanStore.DUR_SYNC);
if (!db.IsOpen()) { throw new System.IO.IOException(db.GetError()); }
Collection chunks = db.GetCollection("chunks");
TextTokenizer tokenizer = new TextTokenizer();
tokenizer.AddWord("领域术语");
// tokenizer.LoadDictionary("dictionary.txt"); // UTF-8，一行一个词
chunks.EnsureFullTextIndex("text", tokenizer);
VectorIndexOptions options = new VectorIndexOptions(768);
options.efConstruction = 160;
options.efSearch = 256;
chunks.EnsureVectorIndex("embedding", options);

JsonValue chunk = JsonValue.NewObject();
chunk.PutStr("text", "中文知识库支持全文与向量检索");
chunk.PutStr("source", "manual");
JsonValue embedding = JsonValue.NewArray();
float[] coordinates = new float[768];          // 此例仅演示格式；实际由模型输出
coordinates[0] = 1;
for (int i = 0; i < coordinates.Length; i = i + 1) {
    embedding.Append(JsonValue.NewDouble((double)coordinates[i]));
}
chunk.Put("embedding", embedding);
int id = chunks.Insert(chunk);

List<Bm25Hit> text = chunks.SearchText("text", "知识库", 10);
List<VectorHit> vector = chunks.SearchVector("embedding", coordinates, 10, 256);
List<VectorHit> exact = chunks.SearchVectorExact("embedding", coordinates, 10);
List<HybridHit> mixed = chunks.SearchHybrid("text", "知识库", "embedding", coordinates, 10, 256);
List<Bm25Hit> filtered = chunks.SearchTextFiltered("text", "知识库", 10, (int candidate) => {
    JsonValue doc = chunks.FindById(candidate);
    if (doc == null) { return false; }
    JsonValue source = doc.Get("source");
    return source != null && source.AsString("") == "manual";
});
chunks.CheckpointSearchIndexes();
db.Close();
```

- 中文最长词典匹配，ASCII 大小写与全角 ASCII 归一化，支持四字节汉字。
  未知词有单字及双字辅助召回；辅助分数权重为 0.2，不增加主词 BM25 文档长度。
  基础词表为项目原创的小词表，随项目许可证分发；领域词典由调用方补充。
- `Bm25Index` 维护词元 posting、文档反向词频、df 和总长度。查询只访问相关
  posting，并用有界堆取 top-k；高频词仍可能覆盖全库。更新和删除会摘除旧 posting。
  原有 `Search(string)` 返回 ID 的接口仍可用，同分按原始插入顺序排序。
- HNSW 包含多层下降、候选/结果堆、邻居多样性裁剪和双向连接。向量保存为 float32；
  `metric=0` 为余弦距离，`metric=1` 为平方欧氏距离，距离越小越近。同距按 ID 排序。
  维度不匹配、非有限值、float32 溢出和余弦零向量在写入前拒绝。缺字段或 null 不入索引。
- embedding 由调用方提供。同一个字段应使用相同模型和维度，查询向量也必须一致。
  检索接口提供候选结果；分块、模型调用、提示词和生成回答由上层应用负责。
- `SearchVectorFiltered` / `SearchVectorExactFiltered` 的 `VectorFilter(int id)`
  在返回 top-k 前生效。HNSW 过滤只排除结果，不阻断图遍历；选择性很强时可增大 ef
  或使用精确检索。`SearchHybridFiltered` 接受两通道过滤器，应给它们同一过滤条件。
  混合检索每通道取 `4*k` 候选，以 `1/(60+rank)` 做 RRF，平局按 ID；这是一种候选融合。
- 集合写入前消除顶层重复键，保留最后一个值，并在调用方对象中附加 `_id`。
  类型验证、文档缓存、字段索引及检索输入均使用同一份规范化值，重开不会改变字段语义。
- 正文、检索输入和 journal 在同一个 WAL 批次提交；索引只重放完整提交，回滚不污染图。
  每个索引使用独立 revision，其他集合的写入不会触发无关重放。
- 初建即保存词频和完整近邻图。检查点按不超过 256 行、约 4 MiB 一批写新代，最后原子发布
  header；旧代和 journal 在发布后分批回收。重开加载已有图及词频，再重放尾部变更。
  未发布新代不参与检索。正常构建失败会清理本次新代；崩溃或 I/O 故障留下的孤儿在下次成功维护时回收。
  常驻句柄按检查点标识失效，其他句柄发布并清理 journal 后仍可加载完整新检查点。
- `RebuildSearchIndexes()` 从正文重建全部检索索引，可以修复坏检查点并回收向量墓碑。
  修改词典后重新调用 `EnsureFullTextIndex(field, tokenizer)`，配置指纹变化会新建一代。
  缺失、损坏或格式不兼容的检查点明确报错，不能返回部分结果。
- 检索和检索维护只能在显式事务之外调用。创建、重建、检查点包含多个内部提交，需由
  持库线程独占执行。直接使用 `VectorIndex` 时查询可并行，Add/Remove/Rebuild/ExportState
  需与所有查询排他；Collection 的读取也会更新缓存，应统一串行同步。
- 常驻图和倒排表需要内存，快照导入/导出也会物化 JSON 状态。当前检查点格式优先保证
  可恢复性；大库需测量内存峰值与文件体积，尚无 mmap 图或流式快照 API。

## 架构（ZanStore 内核）

```
写：Put/Delete ──► 事务 overlay ──► 完整 CRC 批次 ──► 依档位持久化
                                                         │ 成功后发布
                                                         ▼
                                                      memtable
                    日志 <path> ──► fsync / 分组同步
                    │ 日志达阈值（SetFlushThreshold，默认 64MB）
                    ▼
                 固化为不可变有序段 <path>.s<n>（稀疏索引 + 4KB 块）
                    │ MergeSegments()
                    ▼
                 多段归并为单段，掉落旧版本与墓碑
读：Get ──► memtable ──► 段（新→旧，块缓存加速）──► 命中/未命中
```

* **日志**：每个非空最外层提交是一帧，CRC32 外层包裹版本、记录数和全部操作，
  最大 payload 为 64 MiB。恢复先校验完整批次再整批应用，撕裂/CRC 错误尾部截掉；
  CRC 正确但事务结构非法则拒绝开库。支持旧 op1/op2 帧读取，后续写 op3/v1；
  新版本写过的库不能由旧版本打开（旧恢复器不识别新批次）。升级前应备份并统一持库版本。
* **段**：不可变、按键有序、4KB 分块 + 稀疏偏移；点读二分定位块，
  块内顺序扫。段内**新键值直接覆盖旧键值**（写入时合并），读侧不会
  见到同段旧版本。
* **清单**：双槽 `<path>.m0`/`.m1`（CRC 校验、交替写），记录段列表
  与日志水位 `logStartSeq`——水位之前的日志帧已固化进段，恢复时跳过。
* **稀疏密度**：每 16 条记录一个索引项（`SegmentWriter.SPARSE_EVERY`，
  约 430 B/块）。实测 64/128 档全表扫只快 5-15%，点读反而掉一半以上
  （块内线性走读平均半块，块大了直接多扫 3-7 倍字节）——16 是对
  点读友好的甜点，不调。
* **块缓存**：FIFO + pin，容量配置为 `SetCacheCap`（默认 32MB）。memtable
  按日志阈值固化；键目录及检索图/倒排表仍随数据量增长，事务 overlay 受批次大小影响。
* **墓碑**：删除先记入内存墓碑集，固化时以 `op=2` 记录写入新段
  （防止旧段版本在恢复时复活），归并时彻底丢弃。
* **序号不变量**：`Open` 后 `seq` 从日志水位起算；固化推进水位，
  归并不推进——保证每次提交的日志帧至少被一种介质覆盖。

### 文件布局

| 文件          | 内容                                       |
|---------------|--------------------------------------------|
| `<path>`      | 追加 CRC 批次 WAL（未固化写入）             |
| `<path>.lock` | 持库进程的稳定锁文件，开 WAL 之前取得       |
| `<path>.s<n>` | 不可变有序段（n 从 1 递增）                |
| `<path>.m0`   | 清单槽 A：段列表 + 日志水位（CRC）         |
| `<path>.m1`   | 清单槽 B（与 A 交替写，防半写）            |

### 持久化档位（`ZanDatabase.Open(path, durability)`）

| 档位                  | 语义                                                   |
|-----------------------|--------------------------------------------------------|
| `ZanStore.DUR_SYNC`   | 每个提交刷新并同步 WAL，成功确认达到同步持久化边界；服务默认使用此档 |
| `ZanStore.DUR_GROUP`（嵌入式默认） | 每次提交刷新到 OS，`SetSyncEvery(n)` 次或累积 512KB 同步；断电可丢最近未同步提交 |
| `ZanStore.DUR_NONE`   | 先缓存在进程内，攒到 1MB 再刷新；进程崩溃和断电均可能丢最近提交 |

无 I/O 错误的正常关闭会同步尚未落盘的完整帧。`Commit()` 返回 bool，Collection
写入失败抛 `IOException`，`GetError()` 保留错误。非空最外层事务令 `GetRevision()`
增加一次，嵌套提交和空事务不增加；`Rollback()` 清掉整个事务并推进缓存 epoch。

如果 I/O 失败发生在已写帧或已发布清单之后，失败请求可能已经提交；该库停止接受
后续操作，需关闭、重开恢复后查询结果。服务使用相同 request ID 重试可查询持久化的
结果，避免重复写入。不能把 false 或断线直接解释成“肯定没写”。

### 归并维护（全量与增量）

`MergeSegments()` 全量归并所有段为单段；`MergeSomeSegments(k)`
增量归并最老 k 段（≥2），写阻塞时长被归并集规模封顶——大库常驻
场景可在后台周期调用（如每写入若干段后 `MergeSomeSegments(4)`），
反复调用最终收敛为单段。增量归并只重指「键索引仍指向归并集」的
键，指向未归并新段的键原样保留且不写入新段，段清单按数据年龄序
维护，重开恢复（按清单序后扫覆盖）与多路扫描（新→旧游标裁决）
都不会让旧版本复活。两形态的崩溃窗口一致：段已写清单未换 = 孤儿
段（下次同号覆盖）；清单已换 = 旧段成孤儿（仅占盘）。

不想自己排班的用自动挡：`SetAutoMerge(atSegs, mergeK)` 开启后每次
固化新段（flush）收尾时检查段数，达到 `atSegs` 就地归并最老
`mergeK` 段——停顿上界仍是"写 mergeK 段"的耗时，段数稳态在
`[atSegs-mergeK+1, atSegs]` 振荡、不再无界增长。`atSegs ≥
mergeK+1` 才生效（阈值太低会每次固化都归并、得不偿失），默认关闭
（0）：归并代价是否引入写路径应由使用方决定。

### 多进程访问与持库范围

一个 `ZanDbServer` 进程持有数据库，多个客户端进程通过 `ZanDbClient` 或
长度前缀 JSON 协议访问。服务默认监听 loopback，访问令牌由配置提供。请求在同一个
owner gate 内执行，包含读取、检索、写入和维护；网络等待在 gate 外。
写 batch 仅允许 Insert/Upsert/Update/Delete，正文及请求 ID/fingerprint/结果
同事务保存，相同 ID 的相同请求重试返回原结果，不同请求重用 ID 拒绝。
请求 ID 记录当前没有自动保留期，应由使用方按数据量评估磁盘成本。

检索维护先分批构建并原子发布新代，成功后再保存去重结果；中途失败可以安全重试，
它不属于 CRUD batch 的原子范围。服务不会让另一请求看到正在构建的代。
配置、完整客户端例子和真实进程测试见 [服务使用说明](../../../../../../examples/db/zandb_service_README.md)。

数据库文件继续只允许一个持有者。`Open` 先取得 `<path>.lock` 稳定锁，
然后打开 WAL 并取得兼容旧版本的 WAL 锁，最后加载清单、段和恢复日志。
第二个持有者立即拒绝；进程退出由 OS 释放锁，留下 `.lock` 文件不代表仍被锁住。
不要删除仍在使用中的锁文件。路径应使用同一规范绝对路径；`Path.Normalize`
只归一化分隔符，不统一硬链接、符号链接或文件别名，这些别名不在持库保证范围内。

直接嵌入时调用者必须串行管理事务、Collection 缓存和索引维护。`AsyncRwLock`
是显式协调工具，直接 CRUD 方法不会自动持有它；不能仅因对象含有锁就并发调用。
现有异步 CRUD 包装在共享锁内串行执行，包括会维护 LRU 的 `FindByIdAsync`；异常和提前返回都会释放锁。
同步方法、显式事务及检索维护仍需调用者在同一个排他边界中管理。
旧的“多个进程直接并发写同一套文件”不适用于当前引擎。

## 检索实测

2026-10，Windows 10，固定种子有界数据集，单进程原生查询；这些是复现样本，
不能直接推断实际知识库的吞吐。网络、embedding 和正文读取不计入独立索引查询时间。

| 项目 | 数据/参数 | 实测 |
|------|-----------|------|
| 中文 BM25 构建 | 10,000 文档，`--publish` | 102.8 ms |
| 中文 BM25 稀疏查询 | 50 次，top-5；每次 90 posting | 平均 19 µs |
| 独立 BM25 全扫参照：稀疏查询 | 20 次；每次两遍扫描共 20,000 文档访问 | 平均 109.76 ms |
| 中文 BM25 高频查询 | 1 次，10,000 posting | 3.515 ms |
| 独立 BM25 全扫参照：高频查询 | 1 次，两遍扫描 | 12.346 ms |
| HNSW 余弦构建 | 10,000 × 128，M=16，efConstruction=160 | 8.68 s |
| HNSW 余弦查询 | efSearch=64，20 次 | recall@10=87.5%，平均 0.186 ms，472 次距离计算 |
| HNSW 余弦查询 | efSearch=256，20 次 | recall@10=95.5%，平均 0.593 ms，1,399 次距离计算 |
| 精确余弦参照 | 同样数据与查询 | 平均 3.842 ms，10,000 次距离计算 |
| HNSW 平方 L2 查询 | efSearch=64，20 次 | recall@10=100%，平均 0.170 ms，424 次距离计算 |
| 精确平方 L2 参照 | 同样数据与查询 | 平均 4.055 ms，10,000 次距离计算 |

BM25 扫描参照预先分词并保存各文档词频，查询独立全扫计算 df 与分数，没有使用倒排结构；
两个查询的 top-5 ID 和分数均对拍一致。这是本次独立朴素参照，不能视为旧版实现的实测加速比。
高频词仍访问全部 posting，基准中的高频查询各测一次，数字对机器负载敏感。

余弦 ef=256 在这个数据集达到 95% 目标；默认 ef=64 更快但召回较低。
需用自己的 embedding、过滤比例和数据分布选择参数，精确接口可用于对拍。
`zandb_vector` 用独立标量距离与全排序验证两种 metric，1,200 条混合分布数据上的
初建/更新/删除/重建均通过 recall≥95% 检查。坐标先从 float32 扩宽，距离以 double 累加，
以覆盖极大值和次正规数；当前未实现或测量显式 SIMD 距离内核，上表是此数值契约下的测量。

持久化基准使用 2,000 文档 × 64 维、同步持久化、256 文档/插入批次，`--publish` 构建。
全文与近邻图的构建时间包含初始检查点，加载时间包含首次查询：

| 操作 | 实测 |
|------|------|
| 批量插入 | 316.3 ms |
| 全文构建及初始检查点 | 170.3 ms |
| 向量构建及初始检查点（efConstruction=160） | 3.456 s |
| 热全文查询 100 次（高频“知识库”） | 共 159.4 ms，平均 1.594 ms |
| 热向量查询 100 次（efSearch=256） | 共 32.7 ms，平均 0.327 ms |
| 两种索引检查点 | 2.959 s |
| 固化后重开存储 | 24.1 ms |
| 全文检查点加载及查询 | 146.9 ms |
| 图检查点加载及查询 | 119.4 ms |
| 图加载、两条尾部变更重放及查询 | 110.3 ms |
| 从正文重建两种索引及检查点 | 4.650 s |

重开检索各返回 10 个结果。检查点与重建当前会物化完整 JSON 状态，维护开销明显高于热查询；
这些数字用于选择维护时机，尚未包含大库内存峰值和网络端吞吐。

复现：`examples/db/zandb_search_bench.zan`、`zandb_vector_bench.zan`（建议
`--publish`）、`zandb_persistence_bench.zan`（含初建检查点、重开加载和尾部重放）。
产物应写入 `_scratch/`。向量基准的 `ZAN_VECTOR_BENCH_N/DIMS/QUERIES/METRIC/EF`
环境变量选择规模和参数；持久化基准默认 2,000 × 64。

## 既有 CRUD 基准

**与内嵌 SQLite 同负载对拍**（Release 构建，Windows 10，NVMe SSD，
2026-09；20000 文档：name/age/cat/bio 四字段、bio 为 80 字符文本，
两侧均建 cat 二级索引，相同批量节奏 500 条/提交。查询场景在
**归并后的稳态**上测量——写路径固化出多段、建索引后 MergeSegments
归并为单段再查询，即 README 所述维护流程；未归并的多段扫描每行要
做多路归并，会明显慢于下表）：

| 操作 | SQLite | ZanDb | 说明 |
|------|--------|-------|------|
| 批量插入 | ~57000 行/s | **~90000 文档/s** | 同机同负载约为 SQLite 的 1.6 倍 |
| 主键点读 | ~16000 次/s | **~150000 次/s** | ZanDb 进程内块缓存，约 9 倍 |
| 全表扫描 | ~900000 行/s | ~340000 文档/s | 每行构建整棵 JsonValue 树（模型价，差距 2.6 倍） |
| 投影扫描（单字段） | ~1470000 行/s | ~920000 文档/s | `ScanField` 每行只解码一个字段（差距 1.6 倍）；**类型化 `ScanStrField` ~870000（差距 1.7 倍）** |
| 索引点查（物化文档） | ~410000 行/s | ~160000 行/s | 每次命中解码整棵文档树（模型价，差距 2.5 倍） |
| 索引 id 查询（覆盖） | ~1750000 次/s | ~1340000 次/s | `FindIdsByField` 索引就绪时不读任何文档（差距 1.3 倍） |
| 无索引等值过滤 | ~7200000 文档/s | ~6500000 文档/s | 内核裸走读 + `FieldEqualsPtr` 零分配比较 + 针线否证；SQLite 侧为纯 C 列扫描，差距缩到 1.1 倍 |
| Count | ~61000 行/s | **~13100000 文档/s** | 块级内核直数，无回调无物化，约 214 倍 |
| SUM（数值列） | ~10500000 行/s | ~7500000 文档/s | `SumField` 块级内核免树流式直算；SQLite 为列上 C 循环，差距 1.4 倍 |
| GROUP BY COUNT | ~33500000 行/s | ~11500000 文档/s | `CountGroups` 走覆盖索引内核，零文档读；SQLite 走索引计数不碰行——差距 2.9 倍 |
| 自动提交（同语义：每提交 fsync） | ~160 提交/s | ~660 提交/s | ZanDb DUR_SYNC；日志追加 vs journal 双写 |
| 自动提交（分组提交） | —（无此档） | **~11000 提交/s** | ZanDb DUR_GROUP（默认档，fsync 分组窗口敏感） |
| 磁盘占用（含二级索引） | 2.67 MB（121 B/行） | 3.04 MB（139 B/文档） | **新引擎 ≈ SQLite 的 1.14 倍** |

诚实口径：

* SQLite 数字经 stdlib `SqliteConnection`/`DbResult` 通道（每次查询
  prepare/bind + 行物化，单次点查固定开销 ~65µs），不代表 SQLite C API
  的裸上限；ZanDb 侧走进程内块缓存。两个通道都是 Zan 程序实际可用的形态。
* 与行式存储的差异是**模型选择**：每条记录要 JSON 编解码成
  `JsonValue` 树；换来的是 schema-free、原生 Zan 值、免 ORM。
* 需要整行树的操作（全表扫描/索引点查）差距在 2.5-2.6 倍，这是
  **模型价**：每条记录要解码成 `JsonValue` 树，树构建本身（对象分配 +
  字段填装）占大头，存储扫描已不是瓶颈；**只需部分字段时用投影 API**
  （`ScanField`，或免 JsonValue 的类型化 `ScanNumField`/`ScanStrField`），
  每文档只解码目标字段、不建树，差距缩到 1.6-1.7 倍；覆盖 id 查询
  1.3 倍；等值过滤走内核 + `FieldEqualsPtr` 后与 SQLite 的纯 C 列扫描
  只差 1.1 倍。聚合（`SumField`/`CountGroups`）在块级内核上免树直算：
  SUM 1.4 倍（SQLite 列上 C 循环，剩余是逐行委托 + 值区字段走读），
  GROUP BY 2.9 倍（覆盖索引内核上每条目做键内组值定界与同组 memcmp，
  粒度比 SQLite 的索引页计数细；字典/物化只在换组时发生）。Count 在
  内核上已是纯 varint 走读，领先两个数量级。
* 自动提交吞吐对 fsync 延迟与机器负载敏感（±20%）；高吞吐写入请走
  `Begin`/`Commit` 批量（设计主路径，每批一次落盘）。
* 批量写入/扫描的结构优化已内建：自增 id 的 seq 键缓存在内存
  （单写者下安全，Upsert 推进序列时同步缓存，崩溃重开作废重读）；
  事务内同一键写 N 次只在整批提交帧中记录一次最终值（日志写放大不随批次内
  重复写增长）；编码在记录不含 0x00/0xFF 时免转义直通（文本型文档
  几乎总命中）；解码走**字符串零拷贝直读**（不经 ByteBuffer，VarInt
  与字段在原串上按显式位置读取，memchr 判转义，含 0xFF 的罕见记录才
  反转义一次）；扫描走单来源快速路径 + 有界流式扫描（上界在存储层
  判定，墓碑集为空时免逐键哈希），Count 存储层直数无回调，等值过滤
  走零分配 `FieldEquals`。
* 扫描层的机器级细节：游标**惰性物化**键值（`ScanRows` 交付零拷贝
  `ScanRow`，命中行才分配置换为字符串；上界与跳读用块内指针
  memcmp）；等值过滤带**针线否证**——needle 原字节不在记录值区出现
  时（数值/布尔 AsString 归一化除外）整行免物化免走读直接否决；
  普通块加载**跨块复用同一缓冲**、CRC 每块只在首次加载校验一次（段
  不可变，读时不算派生数）；内核按 **`KERNEL_SPAN_BYTES`（256 KB）
  块跨度**一次 `ReadAt` 读入连续多块、逐块就地 CRC，聚合路径的系统
  调用数随段大小恒定。微基准口径：块内记录裸解析 ~230-315ns/行，
  字符串物化一次 ~37ns，memcmp/memchr 原语 ~5ns。
* **块级聚合内核**（`ZanStore.ScanKernel`）：单段稳态（归并后收敛、
  memtable 空——固化/归并维护后的常态）下，Count/CountRange、
  `SumField`、`MinField`/`MaxField`/`AvgField` 走块级裸走读——块
  缓冲读入后循环裸解析记录，把（基址+偏移+长度）直接交给回调，不经
  SegCursor/ScanRow 对象、无字符串物化，每行成本 = varint 解码 +
  一次委托调用；CountRange 连回调里的聚合状态都不需要。多源态
  （memtable 未固化数据参与、多段未归并）自动退回 `ScanRows` 权威
  路径；内核回调遇到需要权威处理的行（记录含 0xFF 转义、文本数值
  tag 4、容器字段）时以「已聚合到 id-1」为界，剩余行（含当行）从
  `ScanRows` 续扫，两段结果无缝拼接。`CountGroups` 在索引就绪时走
  **覆盖索引路径**——索引条目键序 =（值, id）序，扫索引前缀范围、
  按值切段计数，零文档读。
* 关于「语言层地板」的实测更正：Zan **有**裸内存解引用——
  `Span<byte> sp = new Span<byte>(nint, len)` 后 `sp[i]` 即裸读
  （~2.2ns/字节，比字符串 `s[i]` 走读快 4.6 倍，可写，逐次重构造
  免费）；ByteBuffer 有状态方法（ReadU8 每次构造 Span + 游标推进）
  反而最慢（~17ns/字节）。扫描到目标字段只需走读几十字节，各场景
  瓶颈在游标机器/块 IO/树构建而非走读，故当前数字未改；未来大字段
  （KB 级 blob）扫描应把走读落在 `Span<byte>` 上。
* 已修复的缺陷：MergeSegments 重建键索引时未剔除仍在内存墓碑集里
  的键，导致「put 已入段、删除未固化」的键在归并后经索引点读复活
  （含二级索引构建游标自身——归并后索引查询悄悄退化为全表扫描）。
  `zandb_merge_tomb` 用例钉死该回归。

单引擎（不含 SQLite 对照）补充：重开恢复 22000 文档 ~85ms；原始 KV 层
批量 Put ~260000 键/s、点读 ~230000 次/s、合并后 **41 B/键**（~50B 值）；
文档层合并后 **157 B/文档**（含二级索引与 JSON 编码）。作为对照，旧引擎
CoW B+Tree + WAL 对同等负载体积大数倍（页开销 + 写放大碎片 + WAL 未回收）。

复现：`examples/db/zandb_vs_sqlite.zan`（对拍）与
`examples/db/zandb_bench.zan`（单引擎），
`build/zanc <文件> --auto-stdlib -o bench.exe && ./bench.exe`
（数字依赖机器）。

## 测试

`tests/conformance/` 使用确定性输出金标。本轮已逐项编译运行原子提交 56 项、集合检索 68 项、
异步异常 29 项、向量 35 项及 PASS、服务 32 行金标，并运行中文分词/BM25/UTF-8 与受影响的既有存储用例。
编译器 lock 非空保护的 10 个定向正负用例也通过。真实多进程测试位于
`tests/integration/zandb_service_test.py`，实际执行结果见[服务使用说明](../../../../../../examples/db/zandb_service_README.md)。本轮未运行整档测试。

| 用例                 | 覆盖                                                       |
|----------------------|------------------------------------------------------------|
| `zandb_atomic`       | 完整/撕裂/非法批次、嵌套版本、回滚 epoch、I/O 失败、双槽清单损坏及发布失败 |
| `zandb_search`       | 中文/向量/混合/过滤、事务回滚、分批代发布、失败代清理、重复键、跨句柄检查点失效和重建 |
| `zandb_vector`       | 独立精确距离参照、两种 metric、HNSW 召回、更新/删除/重建、图快照及 float32 边界 |
| `zandb_async_errors` | 异步 CRUD 验证/回调异常及提前返回后锁释放 |
| `zandb_service`      | 请求验证、CRUD/batch 去重、错误返回、检索过滤与维护 |
| `text_tokenizer_zh` / `bm25_postings` | 中文/全角/混排/未知词、词典配置、倒排分数独立对拍与增删 |
| `encoding_utf8_scalar` | UTF-8 代理区、超出 Unicode 标量范围的非法输入 |
| `null_guard_lock`   | 编译器对 lock 内外非空保护的正例及六种非法访问负例 |
| `zandb_store_flush`  | 阈值固化、memtable 清空、重开恢复、水位跳帧、撕裂尾         |
| `zandb_scan`         | 归并扫描跨段/内存源、范围含端点、前缀扫描                   |
| `zandb_docs`         | 文档层 CRUD、索引、重开、归并后墓碑不复活                   |
| `zandb_proj`         | 投影 API：`ScanField` 回调/计数/提前停、`FindIdsByField` 扫描路径与覆盖索引路径一致、删除后两路径同步 | 
| `zandb_idxrange`     | `FindIdsByFieldRange`：索引端点 `\t` 上界（"c1" 不放进 "c10"）、无界端点、序数语义（"9"&gt;"20"）、扫描回退与覆盖索引路径一致、删除/重开同步 |
| `zandb_typed`        | 类型化聚合：`SumField` 只认数值标签（负数/文本数值计入、字符串跳过）、`ScanNumField`/`ScanStrField` 免 JsonValue 投影与提前停、`CountGroups` 分组计数、`MinField`/`MaxField`/`AvgField`/`FindTopIds`（平局 id 小者先、堆替换路径）、Update/重开跟随 |
| `zandb_mergeincr`    | 增量归并：最老 k 段归并段数递减收敛、索引原地改指、未归并新段的更新赢（含重开后）、墓碑压键不复活、`MergeSomeSegments` 无事可做返回 0；`SetAutoMerge` 非法参数拒绝、开启后段数稳态有界、关闭恢复无界增长、重开一致 |
| `zandb_merge_tomb`   | 归并墓碑回归：跨段「put+删除」经归并/重归并/重开不复活，点读/Count/索引查询/覆盖查询全路径一致 |
| `zandb_kernel`       | 块级聚合内核：单段稳态下 Sum/Min/Max/Avg/Count 与删除（固化墓碑）、未固化墓碑回退、0xFF 转义记录回退、文本数值字段回退、多段权威路径对拍、重开、索引覆盖 CountGroups 一致性 |
| `zandb_fuzz`         | 300 步随机操作对拍内存参照模型（固定种子 LCG），跨持久化档重开 |
| `zandb_lock`         | 单写锁：第二打开者被拒、关闭后可重开                        |
| `tcp_listener_cancel` | 监听器取消：空闲 accept 停止、描述符保留与复用竞态、停止后不隐式重开、显式重启 |
| `timer_stop_drain`   | 定时器排空：冷停/重复停、周期回调、回调内自停与重启、`StopAsync` 排空后无泄漏退出 |
| `arr_lit_rc`         | （编译器）数组字面量元素 ARC retain——本引擎开发中发现的编译器缺陷 |
