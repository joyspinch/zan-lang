# ZanDb

嵌入式文档数据库：存储原生 Zan 值树（`JsonValue`：整数、浮点、布尔、
字符串、嵌套对象、数组），无 ORM 映射、无 SQL 字符串。底层是专门为
Zan 重写的 KV 内核 **ZanStore**——追加式日志 + 不可变有序段 +
双槽清单，崩溃安全，内存有界。

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

## 架构（ZanStore 内核）

```
写：Put/Delete ──► memtable（有序字典，有界）
                    │ 追加 CRC 帧
                    ▼
                 日志 <path>.log ── 分组提交 fsync
                    │ 日志达阈值（SetFlushThreshold，默认 64MB）
                    ▼
                 固化为不可变有序段 <path>.s<n>（稀疏索引 + 4KB 块）
                    │ MergeSegments()
                    ▼
                 多段归并为单段，掉落旧版本与墓碑
读：Get ──► memtable ──► 段（新→旧，块缓存加速）──► 命中/未命中
```

* **日志**：追加写，每帧 CRC32 校验；恢复时从尾部截掉撕裂/损坏帧。
* **段**：不可变、按键有序、4KB 分块 + 稀疏偏移；点读二分定位块，
  块内顺序扫。段内**新键值直接覆盖旧键值**（写入时合并），读侧不会
  见到同段旧版本。
* **清单**：双槽 `<path>.m0`/`.m1`（CRC 校验、交替写），记录段列表
  与日志水位 `logStartSeq`——水位之前的日志帧已固化进段，恢复时跳过。
* **块缓存**：FIFO + pin，容量有界（`SetCacheCap`，默认 32MB）；
  memtable 同样有界，整体内存不随数据量增长。
* **墓碑**：删除先记入内存墓碑集，固化时以 `op=2` 记录写入新段
  （防止旧段版本在恢复时复活），归并时彻底丢弃。
* **序号不变量**：`Open` 后 `seq` 从日志水位起算；固化推进水位，
  归并不推进——保证每次提交的日志帧至少被一种介质覆盖。

### 文件布局

| 文件          | 内容                                       |
|---------------|--------------------------------------------|
| `<path>.log`  | 追加 CRC 帧日志（未固化写入）              |
| `<path>.s<n>` | 不可变有序段（n 从 1 递增）                |
| `<path>.m0`   | 清单槽 A：段列表 + 日志水位（CRC）         |
| `<path>.m1`   | 清单槽 B（与 A 交替写，防半写）            |

### 持久化档位（`ZanDatabase.Open(path, durability)`）

| 档位                  | 语义                                                   |
|-----------------------|--------------------------------------------------------|
| `ZanStore.DUR_SYNC`   | 每次提交 fsync——最安全，最慢                          |
| `ZanStore.DUR_GROUP`（默认） | 分组提交：`SetSyncEvery(n)` 攒 n 次提交一次 fsync，攒满 512KB 提前 |
| `ZanStore.DUR_NONE`   | 只写 OS 缓冲（攒 1MB flush），进程崩溃不丢、断电可能丢 |

任何档位下**干净关闭都不丢数据**（`Close` 会 flush 未落盘帧）；
崩溃恢复由 CRC 帧 + 水位 + 双槽清单保证到最近一次落盘点。

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

### 单写者模型（与旧引擎的关键差异）

ZanStore 是**单进程单写者**：`Open` 对日志文件加非阻塞独占锁，
第二个写者打开同一库直接报错 `store is locked by another writer`
（锁由 OS 持有，写者崩溃不残留）。同进程内多线程共用一个
`ZanDatabase`（内部 `AsyncRwLock` 串行写、并发读）。

旧引擎支持跨进程多写者，代价是每次提交都走文件锁串行 + 同步落盘，
这是它并发性能不可接受的根源。需要多进程共享数据的场景请用外部
队列/服务聚合写入。

## 基准

**与内嵌 SQLite 同负载对拍**（Release 构建，Windows 10，NVMe SSD，
2026-09；20000 文档：name/age/cat/bio 四字段、bio 为 80 字符文本，
两侧均建 cat 二级索引，相同批量节奏 500 条/提交。查询场景在
**归并后的稳态**上测量——写路径固化出多段、建索引后 MergeSegments
归并为单段再查询，即 README 所述维护流程；未归并的多段扫描每行要
做多路归并，会明显慢于下表）：

| 操作 | SQLite | ZanDb | 说明 |
|------|--------|-------|------|
| 批量插入 | ~69000 行/s | **~85000 文档/s** | 同机同负载约为 SQLite 的 1.2 倍 |
| 主键点读 | ~17000 次/s | **~160000 次/s** | ZanDb 进程内块缓存，约 9 倍 |
| 全表扫描 | ~965000 行/s | ~320000 文档/s | 每行构建整棵 JsonValue 树 |
| 投影扫描（单字段） | ~1400000 行/s | ~730000 文档/s | `ScanField` 每行只解码一个字段；**类型化 `ScanStrField` ~840000（差距 1.7 倍）** |
| 索引点查（物化文档） | ~420000 行/s | ~160000 行/s | 每次命中解码整棵文档树 |
| 索引 id 查询（覆盖） | ~1670000 次/s | ~1190000 次/s | `FindIdsByField` 索引就绪时不读任何文档 |
| 无索引等值过滤 | ~9700000 文档/s | ~1100000 文档/s | `FieldEquals` 零分配逐字节比较；SQLite 侧为纯 C 列扫描 |
| Count | ~62000 行/s | **~1100000 文档/s** | 存储层直数，无回调，约 18 倍 |
| SUM（数值列） | ~10300000 行/s | ~1030000 文档/s | `SumField` 免树流式直算；SQLite 为列上 C 循环，ZanDb 已贴游标地板 |
| GROUP BY COUNT | ~33000000 行/s | ~790000 文档/s | `CountGroups` 读文档聚合；SQLite 走索引计数不碰行——模型差异 |
| 自动提交（同语义：每提交 fsync） | ~150 提交/s | ~620 提交/s | ZanDb DUR_SYNC；日志追加 vs journal 双写 |
| 自动提交（分组提交） | —（无此档） | **~11000 提交/s** | ZanDb DUR_GROUP（默认档，fsync 分组窗口敏感） |
| 磁盘占用（含二级索引） | 2.67 MB（121 B/行） | 3.05 MB（139 B/文档） | **新引擎 ≈ SQLite 的 1.14 倍** |

诚实口径：

* SQLite 数字经 stdlib `SqliteConnection`/`DbResult` 通道（每次查询
  prepare/bind + 行物化，单次点查固定开销 ~65µs），不代表 SQLite C API
  的裸上限；ZanDb 侧走进程内块缓存。两个通道都是 Zan 程序实际可用的形态。
* 与行式存储的差异是**模型选择**：每条记录要 JSON 编解码成
  `JsonValue` 树；换来的是 schema-free、原生 Zan 值、免 ORM。
* 需要整行树的操作（全表扫描/索引点查）差距在 2.2-3 倍；**只需部分
  字段时用投影 API**（`ScanField`，或免 JsonValue 的类型化
  `ScanNumField`/`ScanStrField`），每文档只解码目标字段、不建树，
  类型化单字段投影差距缩到 ~1.7 倍，覆盖 id 查询 ~1.4 倍；聚合
  （`SumField`/`CountGroups`）免树直算但已贴游标地板（与 Count 同
  量级），对 SQLite 的列式 SUM 差距即扫描机器本身的差距。剩余地板
  是扫描游标与每行委托调用，不再是文档树。
* 自动提交吞吐对 fsync 延迟与机器负载敏感（±20%）；高吞吐写入请走
  `Begin`/`Commit` 批量（设计主路径，每批一次落盘）。
* 批量写入/扫描的结构优化已内建：自增 id 的 seq 键缓存在内存
  （单写者下安全，Upsert 推进序列时同步缓存，崩溃重开作废重读）；
  事务内同一键写 N 次只在提交时落最终值一帧（日志写放大不随批次内
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
  块加载**跨块复用同一缓冲**、CRC 每块只在首次加载校验一次（段不可
  变，读时不算派生数）。微基准口径：块内记录裸解析 ~230-315ns/行，
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

`tests/conformance/`（确定性输出 + `--check-leaks` 通过）：

| 用例                 | 覆盖                                                       |
|----------------------|------------------------------------------------------------|
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
| `arr_lit_rc`         | （编译器）数组字面量元素 ARC retain——本引擎开发中发现的编译器缺陷 |
