# System.Data.ZanDb

> 源码: `packages/Zan.Data/src/System/Data/ZanDb/BlockCache.zan`, `packages/Zan.Data/src/System/Data/ZanDb/Collection.zan`, `packages/Zan.Data/src/System/Data/ZanDb/DocQuery.zan`, `packages/Zan.Data/src/System/Data/ZanDb/KvEntry.zan`, `packages/Zan.Data/src/System/Data/ZanDb/LogFile.zan`, `packages/Zan.Data/src/System/Data/ZanDb/Manifest.zan`, `packages/Zan.Data/src/System/Data/ZanDb/PageFile.zan`, `packages/Zan.Data/src/System/Data/ZanDb/Segment.zan`, `packages/Zan.Data/src/System/Data/ZanDb/Store.zan`, `packages/Zan.Data/src/System/Data/ZanDb/ZanDatabase.zan`


## BlockCache (class)

段块缓存：FIFO 驱逐、字节上限、借用计数保护。
异步读共享锁下多协程并发命中，互斥锁只护元数据；
借用期间缓冲内容稳定（值解析出字符串拷贝后才 Release）。
驱逐只在 Insert 时做一轮扫描：无无 pin 受害者时允许暂时
超限——借用是短暂的，写入方下次插入会继续回收。

- Dictionary <long, CachePin> map;

- List<long> order;

- long bytes;

- long cap;

- nint mtx;

- BlockCache(long capBytes)

- void SetCap(long capBytes)
  - 调整字节上限（立即生效，下次驱逐执行）。

- CachePin Acquire(long key)
  - 取块：命中时 pin +1 返回；未命中返回 null。
    用完必须 Release。

- CachePin Insert(long key, ByteBuffer buf)
  - 放入新块并返回已 pin 的条目（接管 buf 所有权，
    含将来驱逐时的 Free）。同键已存在时释放新缓冲、返回旧条目。

- void Release(CachePin pin)
  - 归还借用的条目。

- void Close()
  - 释放全部缓冲并销毁互斥锁（Close 后不可再用）。


## CachePin (class)

缓存条目：块缓冲 + 借用计数。pins > 0 的条目
不被驱逐；借用方解析完必须 `BlockCache.Release`。

- ByteBuffer buf;

- int pins;

- int size;

- CachePin(ByteBuffer buf, int size)


## Collection (class)

`ZanDatabase` 中一组有名字的文档。文档是
JsonValue 对象树，每篇文档分配一个自增整数 _id。
存储键在 `ZanStore` 中以命名空间区分：
c:{name}:{id}                   文档正文（key-dict 编码的 JSON）
m:{name}:keys                   字段名字典（JSON 数组）
m:{name}:seq                    最后分配的 id
m:{name}:idx                    逗号分隔的索引字段列表
m:{name}:idxpos:{field}         批量索引构建游标（缺失表示已构建完成）
i:{name}:{field}:{value}\t{id}  二级索引条目 -> id
id 用零填充，因此键序即插入顺序，使 FindAll
和范围迭代变成顺序扫描。

- ZanStore kv;

- AsyncRwLock rw;

- string name;

- List<string> indexedFields;

- List<string> keyDict;

- bool keyDictDirty;

- long metaVersion;

- int nextIdCache;
  - 已缓存的下一个待分配 id；0 = 尚未从 seq 键加载。
    单写者模型下安全：序列键仍随每次分配落盘（事务覆盖层
    会在提交时去重成一帧），崩溃后经重开恢复，缓存作废重读。

- Dict <int, JsonValue> docCache;
  - 进程内热点文档 LRU 缓存池与访问序列，避免重复 I/O 与反序列化。

- List<int> docCacheOrder;

- int docCacheCapacity;

- static int INDEX_BATCH=500;
  - 批量索引构建中每个事务索引的文档数。

- [DllImport("crt", EntryPoint="memchr")]static extern nint MemChr(nint p, int c, long n);

- Collection()
  - 仅初始化空字段；真正的构造走 `Attach`。

- static Collection Attach(ZanStore kv, AsyncRwLock rw, string name)
  - 绑定到底层 ZanStore 的一个命名空间，并加载索引列表与字段名字典。

- void SyncMeta()
  - 当数据库更新到新版本（即其他进程已提交）时，重新读取缓存的元数据（字段名字典、索引
    列表）。操作文档前两者都必需：
    字典负责编解码对象键，因此过期的副本会
    用错误的字段名解码其他进程的文档，并将这些 id
    复用到不同的名称上。开销低：一次版本比较，
    两次读取只在确实有变化时
    才发生。

- string Name()
  - 集合名。

- static int ID_DIGITS=6;

- static string PadId(int id)
  - 把 id 编码为定宽 6 位 base-64 数字（'?'..'~'）：字节序即数值序，
    且不含 0x00、':' 与 '\t'，不破坏 B+Tree 顺序与键分隔。

- static int UnpadId(string s)
  - PadId 的逆操作。

- static int UnpadIdPtr(nint p)
  - `UnpadId` 的裸指针形式（定宽 ID_DIGITS 字节）。

- string DocKey(int id)
  - 文档正文键 c:{name}:{id(定宽)}。

- string SeqKey()
  - 自增 id 序列键。

- string IdxMetaKey()
  - 索引字段列表元数据键（逗号分隔）。

- string KeysMetaKey()
  - 字段名字典元数据键（JSON 数组）。

- string IdxPosKey(string field)
  - 批量索引构建游标键；键不存在即表示该索引已构建完成。

- string IdxPrefix(string field, string val)
  - 同一索引字段所有条目共享的前缀。字段用
    其字典 id 表示而非拼出全名，因此长字段名
    对每个条目零开销。

- string IdxKey(string field, string val, int id)
  - 完整二级索引条目键：`IdxPrefix` 加定宽 id。

- void LoadIndexMeta()
  - 从元数据键加载索引字段列表（逗号分隔；空则无索引）。

- void LoadKeyDict()
  - 从元数据键的 JSON 数组加载字段名字典。

- int KeyId(string k)
  - 字段名的 1 起始字典 id；首次出现时追加进字典并标记待持久化（须在 kv 事务内）。

- int KeyIdOf(string k)
  - 字段名的字典 id；若该集合从未
    存储过该字段则返回 0。与 KeyId 不同，此方法不会扩充字典，
    因此可在读取路径安全使用。

- string KeyName(int id)
  - 字典 id 反查字段名；越界时返回 "?" 加 id（不抛错）。

- void PersistKeyDict()
  - 字典有变更时把整个字典写回元数据键（须在 kv 事务内）。

- static bool IsIntStr(string s)
  - 字符串是否为纯十进制整数（可带负号，至多 18 位以留在 varint 范围内）。

- static long ParseI64(string s)
  - 解析带符号的十进制整数为 64 位。用它替代 Convert.ToInt，因为后者
    目前降级为 C 的 atoi（声明返回 i64），因此无法
    符号扩展——负数及大于 2^31 的值会以零扩展的
    垃圾数据返回。累加器必须是 long：IsIntStr 放行最长 18 位，
    epoch 毫秒时间戳（13 位，>2^31）此前在 int 里静默回绕后落盘。

- void WriteVal(ByteBuffer b, JsonValue v)
  - 按上方标签表把一个 JsonValue 写入记录；对象键写作字典 id（须在 kv 事务内）。

- static bool HasLaterKey(JsonValue doc, string key, int from)
  - doc 的 from 之后是否还存在同名键
    （JsonValue.Put 追加不去重，重复键以后写为准——与
    DecodeDoc/JSON 语义一致，编码时在这里去重）。

- string EncodeDoc(JsonValue doc)
  - 将文档序列化为紧凑的二进制记录：一个 'B'
    格式字节，随后是顶层对象的字段（字典 id 键 +
    带类型的值）。冗余的 "_id" 被省略——它是行键，读取时
    重新附加。重复键只写最后一次出现（Put 的追加语义下
    老值作废），部分读取（FieldOf）与全量解码因此一致。
    必须在 kv 事务内运行：新字段名
    会与正文一起持久化。

- static int IdOfKey(string key)
  - 键尾随数字中编码的 id。行键和
    索引条目都以定宽 id 结尾，因此无需搜索分隔符——
    id 的某个数字可能恰好像分隔符。

- static int IdOfPtr(nint baseKey, int keyLen)
  - `IdOfKey` 的裸指针形式：键尾 6 字节定宽
    id（'?'..'~'）直接还原，零分配。baseKey 指向键首字节，
    keyLen 为键长。

- static string IdFromKey(string key)
  - `IdOfKey` 的字符串形式。

- [DllImport("crt", EntryPoint="memchr")]static extern nint StrMemChr(string p, int c, long n);

- [DllImport("crt", EntryPoint="memcmp")]static extern int MemCmpRaw(nint a, string b, long n);

- static long RecVarInt(string s, int p, out int np)
  - 与 ByteBuffer.WriteVarInt 同格式的变长整数读取（显式位置传递）。

- static string UnescapeRecord(string body)
  - 记录含 0xFF 时（转义标记或载荷）还原为可直读的串。罕见路径。

- JsonValue ReadValS(string s, int p, out int np)
  - 从记录中读取 `WriteVal` 写出的一个值并还原为
    JsonValue；p 为值起始字节，np 为值结束后的位置。

- static void SkipValS(string s, int p, out int np)
  - 跳过一个编码值，而不从中构建任何内容。
    字符串和文本形式的数字按长度跳过，而非复制。

- JsonValue FieldOf(string body, int kid)
  - 直接从存储的记录中取出一个字段：其他字段
    被跳过而非解码，因此对大集合做过滤
    不会为每篇文档构建 JsonValue 树。记录中
    没有该字段时返回 null。

- bool FieldEquals(string body, int kid, string val)
  - 字段等值判定（`FindIdsByField` 与
    `CountByField` 的过滤内核）：字符串值
    （记录 tag 5）按长度 + 逐字节比较，零分配；
    其他类型才解码出值按 `JsonValue.AsString`
    比较（与投影读语义一致）。与 FieldOf 相同的记录行走，
    但命中字段后不构建 JsonValue。

- bool EqFastReject(string val)
  - 等值过滤的快速否证前提：needle 至少 2 字符、
    含「非数值渲染字符」的字符（排除数值 AsString 归一化渲染
    的等值可能——整数/浮点渲染只含 0-9 与 + - . e E）、
    不含 0x00/0xFF（排除转义编码的字节位移）、不是 "true"/
    "false"（排除布尔 AsString 等值）。满足时 needle 的原
    字节若不在记录值区出现，字段就绝不等于该值——整行免
    物化免走读直接否决。

- bool NeedleAbsent(ScanRow row, string val)
  - needle 原字节是否不在行值区出现：memchr 找首字符
    出现点、memcmp 全针验证，命中点逐个推进。找不到即整行
    否决（`EqFastReject` 前提下声音）。约 5ns 级
    原语成本，替代数百纳秒的记录走读。

- bool FieldEqualsRow(ScanRow row, int kid, string val, bool fastReject)
  - `FieldEquals` 的行视图版：满足
    `EqFastReject` 的 needle 先做针线否证（零物化），
    未否决才物化值走权威走读；不满足直接走权威走读。

- JsonValue FieldOfRow(ScanRow row, int kid)
  - `FieldOf` 的行视图版：值按需物化，
    走读落在静态字符串路径（物化实测比 ByteBuffer 有状态
    方法逐字节走读便宜）。

- bool FieldNum(string body, int kid, out long v)
  - 字段的数值形态（类型化免树）：varint(3/8/9) 直接
    还原、文本数值(4)解析；字符串/布尔/容器/缺字段返回 false
    ——聚合只认数值型字段，不做隐式强制。

- string FieldText(string body, int kid)
  - 字段的文本形态（免 JsonValue）：字符串(5/10)直接
    取载荷、数值(3/8/9/4)转十进制文本、布尔(1/2)转
    "true"/"false"——与 `JsonValue.AsString` 的
    归一化形态一致；null/容器/缺字段返回 null（调用方跳过）。

- int ScanNumField(string field, NumVisit visit)
  - 数值字段投影扫描：每条含 field 的文档以 (id, 数值)
    调用一次 visit，不构建任何 JsonValue（varint 直接还原，
    文本数值才解析）；visit 返回 false 提前结束。返回 visit
    被调用的次数。

- int ScanStrField(string field, StrVisit visit)
  - 文本字段投影扫描：每条含 field 的文档以 (id, 文本)
    调用一次 visit，只物化该字段的字符串（不建 JsonValue 包装）；
    visit 返回 false 提前结束。返回 visit 被调用的次数。

- long SumField(string field)
  - 数值字段的全文求和：单段稳态走块级聚合内核
    （免游标/ScanRow 对象，记录裸走读直累加）；记录含转义
    （0xFF）或目标字段是文本数值(4)时逐行回退
    `FieldNum` 权威走读，语义与之一致——数值型
    标签直接还原累加，非数值型文档跳过。字段不存在或全非
    数值时返回 0。

- Dictionary <string, int> CountGroups(string field)
  - 字段值的分组计数（GROUP BY field 的 COUNT）：
    一遍流式扫描直接在键控结构里聚合，每组只分配一次键字符串。
    null/容器/缺字段的文档不计入。索引就绪时零文档读：该索引
    字段的条目键序 = （值, id）序，值相同的条目天然连片——扫
    索引前缀范围、按值切段计数即可。

- JsonValue DecodeDoc(string body)
  - 把存储的二进制记录（见 `EncodeDoc`）解码回 JsonValue 对象；
    "_id" 由调用方从行键附加。

- void AddIndexEntries(int id, JsonValue doc)
  - 为每个已索引字段写入该文档的二级索引条目（须在 kv 事务内）。

- void RemoveIndexEntries(int id, JsonValue doc)
  - 删除一篇文档的全部二级索引条目；doc 为 null（文档已不存在）时无事可做。

- int NextId()
  - 读取并推进自增 id 序列，返回新分配的 id（须在 kv 事务内）。
    seq 值缓存在内存（0 = 未加载）：单写者下没有并发分配者，
    序列键本身仍随本次分配 Put 落盘，崩溃后重开时缓存作废重读。
    省掉每次分配一次 Get——实测它随段数增长，占插入路径大头。

- void SetCacheCapacity(int capacity)
  - 设置集合的文档缓存池容量（0 为关闭缓存，默认 256）。

- int GetCacheCapacity()
  - 获取当前集合文档缓存池的缓存容量。

- int CachedDocCount()
  - 获取当前集合文档缓存池中已缓存的文档数。

- void ClearCache()
  - 清空文档缓存池。

- JsonValue CacheGet(int id)
  - 从文档缓存中获取文档，若存在则更新 LRU 访问热度并返回其只读副本。

- void CachePut(int id, JsonValue doc)
  - 将文档放入 LRU 缓存池；超过容量时淘汰最久未访问的条目。

- void CacheEvict(int id)
  - 从缓存池中显式失效指定文档。

- int Insert(JsonValue doc)
  - 存储新文档，分配并返回其 _id
    （同时写入文档中）。

- int InsertBulk(List<JsonValue> docs)
  - 在单个原子提交中批量插入（整个批次
    只做一次 WAL 刷新）。返回第一个分配的 id。

- JsonValue FindById(int id)
  - 返回文档；优先走内存 LRU Buffer Pool，未命中则反序列化；不存在则返回 null。

- bool Exists(int id)
  - 文档 id 是否存在。

- bool Update(int id, JsonValue doc)
  - 替换现有文档；id 不存在时返回 false。

- void Upsert(int id, JsonValue doc)
  - 在显式 id 下插入或替换。推进自增 id
    序列越过该 id，避免后续插入冲突。

- bool DeleteById(int id)
  - 删除文档及其索引条目；返回该 id 是否原本存在。

- void PersistIndexMeta()
  - 把索引字段列表写回元数据键（须在 kv 事务内）。

- void EnsureIndex(string field)
  - 为字段创建（或重建）二级索引，按有界批次
    索引现有文档。后续写入会自动维护。
    中断的构建会继续执行，而不是
    重新开始。

- void EnsureIndex(string field, int batchSize)
  - 带显式批次大小的 EnsureIndex（每
    个事务处理的文档数）。

- bool BuildIndexStep(string field, int batchSize)
  - 索引下一批文档，每次调用一个事务，
    覆盖整个集合后返回 true。
    
    在单个事务中为大集合构建索引会把
    每个触及的页固定到提交为止，并在整个构建期间持有
    跨进程写锁；分批将两者都限制在界内。游标
    （m:{name}:idxpos:{field}）与它所覆盖的条目属于同一提交，
    因此构建中途崩溃或被杀死不会丢失任何内容，下次调用
    会从上次提交的位置继续。
    
    字段在第一批就加入索引列表，因此并发写入者
    （包括其他进程）已经在为构建尚未到达的
    文档维护条目；这些写入与构建结果一致，因为两者都
    从同一篇文档派生出条目。在构建完成前，
    IndexReady 为 false，FindByIndex 回退为扫描，因此不完整的
    索引绝不会产生不完整的结果。

- bool HasIndex(string field)
  - 字段是否已在索引列表中（含批量构建尚未完成的）。

- bool IndexReady(string field)
  - 当字段已被索引且其批次构建
    完成（即索引覆盖所有文档）时为 true。

- List<JsonValue> FindByIndex(string field, string val)
  - 索引查找：字段等于 val（字符串形式）的文档，
    通过二级索引实现——无需全表扫描。索引仍在构建时
    回退为扫描，因此结果总是完整的。

- List<JsonValue> FindByFieldScan(string field, string val)
  - 通过扫描找出字段等于 val 的文档：每条
    记录只被询问这一个字段（字符串值走零分配比较），
    只有匹配项会被完整解码，全程流式。用于字段没有
    可用索引的情况。

- int CountByField(string field, string val)
  - 字段值等于 val 的文档数，无需具体化
    任何文档：字符串值零分配比较，全程流式计数。

- int Count()
  - 文档总数（命名空间的键范围直数）。上界判定
    与存活判定都在存储层循环内，无回调、大集合不按文档数分配。

- static bool RecAccNum(nint basePtr, int valOff, int valLen, int want, NumAcc acc)
  - 裸记录走读（静态、免 this）：从记录起始位置解析
    'B' + VarInt(字段数) + (VarInt(kid) + 值)*，对每个 kid==want
    的数值字段（tag 3/8/9/4）累加进 agg。tag 语义与
    `FieldNum` 一致；basePtr 指向键起点（值在
    basePtr+valOff）。返回 false = 记录含转义（0xFF）或格式
    异常，调用方须回退权威走读。

- class NumAcc
  - 块级聚合的数值累加器：sum/count/min/max 由调用方按需取用。

- static int FieldEqualsPtr(nint basePtr, int valOff, int valLen, int kid, string val)
  - `FieldEquals` 的裸指针版（内核回调用，
    免整串物化）：记录含 0xFF（转义态）或格式异常返回 -1（调用
    方回退权威路径）；tag 5 字符串值按长度 + memcmp 零分配比较，
    数值/布尔等非字符串类型返回 -2（走权威 AsString 语义），
    字段缺失返回 0。1 = 字段值等于 val。

- static int PeekB(nint p)
  - 读取裸指针处一个字节；offset 为相对位移。

- static long ReadVarIntRaw(nint basePtr, ref int p, int limit)
  - 裸指针 LEB128 varint 读取：自 base+p 起解析，
    结束位置写回 p（引用传参），limit 为缓冲绝对终点。越界
    返回 -1。与 ByteBuffer.ReadVarIntAt 同格式。

- static bool SkipValRaw(nint basePtr, ref int p, int end)
  - 裸指针跳过一个编码值（标签已读、p 指向标签后）：
    与 `SkipValS` 同规则，数组/对象载荷含嵌套时
    返回 false 回退权威走读（数值聚合场景容器字段罕见）。
    推进失败返回 false。

- List<JsonValue> FindAll()
  - 按 id 顺序返回所有文档。

- List<JsonValue> FindRange(int fromId, int toId, int limit)
  - 满足 fromId <= _id <= toId 的文档（0 表示无界），
    limit 0 表示不限数量，按 id 顺序返回。

- int ScanField(string field, FieldVisit visit)
  - 投影式扫描：按 id 序遍历集合，每篇含 field 的文档
    以 (id, 值) 调用一次 visit；visit 返回 false 提前结束。
    每条记录只解码这一个字段——值留在块内按偏移解析，不构建
    文档树、不物化文档，单字段聚合/过滤的每文档成本与文档的
    字段数无关。返回 visit 被调用的次数。

- List<int> FindIdsByField(string field, string val)
  - 字段值等于 val 的所有文档 id，按 id 序。
    索引就绪时只扫索引条目（覆盖索引，一个文档都不读）；
    否则流式扫描、每条记录只解码这一个字段（零拷贝），
    匹配才物化键收 id——全程不构建文档树。

- List<int> FindIdsByFieldRange(string field, string lo, string hi)
  - 字段值落在 [lo, hi]（双端含）的所有文档 id。
    索引就绪时走索引键序范围扫（覆盖索引，一个文档都不读，
    成本与等值覆盖查询同量级），结果按（值, id）序；无索引则
    流式扫描按 id 序。比较语义：字段值 AsString 形态的**序数**
    比较——数字按十进制文本比（"9" > "20"），范围端点与索引
    值不应含 < \t 的控制字符。lo 为空串 = 无下界，hi 为空串 =
    无上界。

- bool NumAggregate(string field, NumAcc acc, out int fromId)
  - 数值聚合的公共骨架：单段稳态先走块级内核
    （RecAccNum 裸走读，语义同 FieldNum 的数值标签判定），
    内核路径遇到需权威处理的行（转义 0xFF/文本数值/格式
    异常/容器）时以「已聚合到 id-1」为界，剩余行回退
    `ScanRows` 字符串路径，sum 与 min/max 在两段
    之间保持一致。返回 false = 内核不可用（多源/未固化数据），
    调用方直接全量走权威路径。

- bool MinField(string field, out long v)
  - 数值字段的最小值：块级内核直算（带权威回退），
    无 JsonValue；无数值文档时返回 false。out 参数被闭包捕获
    时不回传（捕获的是副本），先累计到局部变量、扫完再写回。

- bool MaxField(string field, out long v)
  - 数值字段的最大值：块级内核直算（带权威回退），
    无 JsonValue；无数值文档时返回 false。

- double AvgField(string field)
  - 数值字段的平均值：一遍流式同时累计和与计数
    （块级内核，带权威回退）；无数值文档时返回 0。

- List<int> FindTopIds(string field, int n)
  - 数值字段 TopN：按字段值降序的前 n 个文档 id
    （等值时 id 小者在前）。容量有界的最小堆流式筛选——堆里
    只驻留 n 项，内存 O(n) 与文档数无关；非数值/缺字段文档
    不参与。n ≤ 0 返回空表。排行榜类负载免全排序。

- static bool ZanCollectionBetter(long va, int ia, long vb, int ib)
  - 候选 (va,ia) 是否优于 (vb,ib)：值大者优；等值 id 小者优。

- static void HeapPush(List<long> hv, List<int> hi, long v, int id)

- static void HeapSiftDown(List<long> hv, List<int> hi)

- DocQuery Query()
  - 对集合启动流畅的 LINQ 风格查询。

- async JsonValue FindByIdAsync(int id)
  - `FindById` 的协程版本：在共享读写锁的读侧执行。

- async int InsertAsync(JsonValue doc)
  - `Insert` 的协程版本：在共享读写锁的写侧执行。

- async bool UpdateAsync(int id, JsonValue doc)
  - `Update` 的协程版本：在共享读写锁的写侧执行。

- async bool DeleteByIdAsync(int id)
  - `DeleteById` 的协程版本：在共享读写锁的写侧执行。

- async bool UpdateAtomic(int id, DocUpdater fn)
  - 对单篇文档的原子读改写：fn 在
    写临界区内基于最新已提交版本运行，因此
    并发更新不会丢失写入（判断与写入是
    一个原子步骤）。当文档存在且 fn
    返回了替换值时返回 true。


## DocQuery (class)

针对 `Collection` 的流畅 LINQ 风格查询：类型安全的 lambda
谓词、多索引求交加速、范围索引下推、O(n log n) 稳定归并排序与 Top-K 堆优化、
Skip/Take 提前终止分页。求值推迟到 ToList/First/Count 时才进行。

List<JsonValue> adults = users.Query()
.WhereEq("city", "北京")                       // uses 索引 若存在
.Where((JsonValue u) => u.Int("age", 0) >= 18) // lambda filter
.OrderByInt((JsonValue u) => u.Int("age", 0))
.Take(10)
.ToList();

- Collection col;

- List<DocPredicate> preds;

- List<EqFilter> eqFilters;

- List<RangeFilter> rangeFilters;

- List<InFilter> inFilters;

- string idxField;

- string idxValue;

- bool hasIdxEq;

- bool sortInt;

- bool sortStr;

- bool sortDesc;

- DocIntKey intKey;

- DocStrKey strKey;

- int skipN;

- int takeN;

- DocQuery()
  - 私有构造；统一经 `Of` 创建。

- static DocQuery Of(Collection col)
  - 为集合创建一个空查询。

- DocQuery Where(DocPredicate pred)
  - Lambda 过滤器；多次调用彼此 AND。

- DocQuery WhereEq(string field, string val)
  - 字段等值约束（字符串形式）。当字段有
    二级索引时，候选集来自索引而非
    全表扫描；若有多个索引字段则自动执行双指针多索引求交。

- DocQuery WhereEqInt(string field, int val)
  - 字段的整数等值约束。

- DocQuery WhereRange(string field, string lo, string hi)
  - 字段序数范围约束 [lo, hi]（lo 为空表示无下界，hi 为空表示无上界）。
    当字段有二级索引时，自动下推至 B+Tree 范围扫描。

- DocQuery WhereIn(string field, List<string> values)
  - 字段集合约束（字符串列表形式：WHERE field IN (...)）。
    当字段有二级索引时，自动按索引合并求并集并参与求交加速；无索引时基于哈希表 O(1) 过滤。

- DocQuery WhereInInt(string field, List<int> values)
  - 字段集合约束（整数列表形式）。

- DocQuery OrderByInt(DocIntKey key)
  - 按整数键升序排序（再次调用覆盖先前排序设置）。

- DocQuery OrderByIntDesc(DocIntKey key)
  - 按整数键降序排序。

- DocQuery OrderByStr(DocStrKey key)
  - 按字符串键升序排序。

- DocQuery OrderByStrDesc(DocStrKey key)
  - 按字符串键降序排序。

- DocQuery Skip(int n)
  - 跳过前 n 条（分页）。

- DocQuery Take(int n)
  - 最多返回 n 条（0 = 不限）。

- static List<int> IntersectSortedIds(List<int> a, List<int> b)
  - 两个严格递增的整数 ID 列表的双指针有序求交集：O(A + B)。

- static List<int> UnionSortedIds(List<int> a, List<int> b)
  - 两个严格递增的整数 ID 列表的双指针有序求并集：O(A + B)。

- List<int> FindIdsByInValues(string field, List<string> vals)

- List<JsonValue> Candidates()
  - 候选集检索：
    1. 若有多个就绪二级索引约束，执行有序双指针求交（Index Intersection）；
    2. 若单索引就绪，直接索引定位；
    3. 若无索引，优先使用首个等值约束或范围约束做存储层单字段快筛扫描；
    4. 否则全表扫描。

- bool MatchesRest(JsonValue d)
  - 检查余下的等值约束、范围约束与 lambda 过滤器
    （首个等值约束已由 `Candidates` 的扫描应用）。

- bool EqAppliedByScan()
  - 当候选集已经由单字段快筛扫描应用了首个等值条件时为 true。

- bool Matches(JsonValue d)
  - 检查全部等值约束、范围约束与 lambda 过滤器。

- void SortDocs(List<JsonValue> docs)
  - 对预计算的键做自底向上归并排序：O(n log n)，稳定。

- List<JsonValue> SortTopDocsInt(List<JsonValue> docs, int k)
  - 基于二叉堆的整数键 Top-K 局部堆排序：O(N log K)，严格无死循环。

- List<JsonValue> SortTopDocsStr(List<JsonValue> docs, int k)
  - 基于二叉堆的字符串键 Top-K 局部堆排序：O(N log K)，严格无死循环。

- List<JsonValue> ToList()
  - 执行查询并具体化匹配的文档。

- JsonValue First()
  - 返回第一个匹配项，没有则返回 null。
    借助无排序提前终止特性，找到第 1 项立即返回，避免无效扫描。

- int Count()
  - 匹配数量（不应用排序/分页）。只有单个等值约束
    时无需具体化任何文档即可计数——字段已索引时
    用索引键，否则从每条记录读取
    一个字段来计数。


## EqFilter (class)

待处理的等值约束（field == val），在字段未被索引加速时
作为过滤器应用。用一个实体而非并行的
字段/值列表。

- public string field;

- public string val;

- public EqFilter(string field, string val)
  - 私有构造；经 `DocQuery.WhereEq` 创建。


## InFilter (class)

待处理的集合包含约束（field IN (val1, val2, ...)）。

- public string field;

- public List<string> values;

- public Dict <string, bool> valSet;

- public InFilter(string field, List<string> values)


## KvEntry (class)

范围或前缀扫描返回的一条键/值记录。

- string key;

- string val;

- KvEntry(string key, string val)
  - 构造一条记录。


## LogFile (class)

追加日志：定长头（载荷长度 + CRC32）+ 载荷的顺序帧序列。
写路径先把帧攒进内存缓冲，Flush 时一次性写到逻辑末尾，
Sync 落到稳定存储——一组帧因此只需一次定位写、一次 fsync。
Replay 按序校验帧，遇到第一个不完整或 CRC 不符的帧即停止；
崩溃留下的撕裂尾部被就地截断，因此重放之后日志永远以
完整帧结尾，后续追加直接覆盖垃圾区域。

帧布局（小端）：
u32 payloadLen
u32 crc32     载荷的 CRC32
payload       payloadLen 字节

- static int HEADER_LEN=8;
  - 帧头长度（字节）：u32 len + u32 crc。

- static long MAX_FRAME=67108864;
  - 单帧载荷的防护性上限：超过即视为损坏帧。

- PageFile file;

- string path;

- ByteBuffer pending;

- long logicalEnd;

- string lastError;

- LogFile()

- static LogFile Open(string filePath)
  - 打开（必要时创建）日志文件。重放由调用方
    经 `Replay` 驱动。

- bool IsOpen()
  - 文件是否成功打开。

- string GetError()
  - 最近一次操作的错误描述（无错为空串）。

- string FilePath()
  - 日志文件的磁盘路径。

- long Size()
  - 逻辑大小：已落盘的完整帧 + 缓冲中的帧。

- int PendingBytes()
  - 缓冲中尚未写入文件的字节数。

- void AppendFrame(ByteBuffer payload)
  - 追加一帧。帧先进入内存缓冲，
    `Flush` 之前不落盘。

- bool Flush()
  - 把缓冲中的全部帧一次性写到逻辑末尾。
    失败时缓冲保持原样，可重试。

- bool Sync()
  - Flush 并强制落盘到稳定存储。

- int Mark()
  - 标记缓冲当前位置（事务起点），供 `RollbackTo` 使用。

- void RollbackTo(int mark)
  - 丢弃 Mark 之后缓冲中的帧（事务回滚）。
    只对尚未 Flush 的内容有效。

- long Replay(LogFrameVisitor visit)
  - 顺序重放全部完整帧，每个载荷交给 visit；
    遇到第一个不完整或 CRC 不符的帧停止，并截断其后的
    撕裂尾部。返回重放结束处的偏移（新的逻辑末尾）。

- void Reset()
  - 清空日志（段 flush 完成后调用）：丢弃缓冲、
    截断到 0。只在清单已持久化覆盖现有全部记录之后调用——
    顺序颠倒会出现恢复缺口。

- bool TryLockExclusive()
  - 非阻塞独占锁日志文件：ZanStore.Open 用它拒绝第二
    个进程打开同一路径（单写模型）。锁随 `Close`
    的句柄由操作系统释放，崩溃的持有者不会卡死后来的打开者。

- void Close()
  - Flush 后关闭底层文件。


## Manifest (class)

双槽清单文件：段列表 + 日志水位 + 段号游标。两个槽各存一代，
CRC 有效且代数（generation）高者胜；保存写到非当前槽并 fsync
——任何时刻崩溃，两槽中总有一份完整的清单，不依赖原子改名。

布局（小端）：
"ZMAN" + u32 version + u64 gen + u64 logStartSeq
+ u64 nextSegId + u32 segCount + segCount × u64 segId
+ u32 crc32(以上全部字节)

logStartSeq 语义：日志中 seq < logStartSeq 的帧已被段覆盖，
恢复时跳过——它覆盖"manifest 已保存、日志尚未截断"的崩溃窗口。

- static string MAGIC="ZMAN";

- static int VERSION=1;

- static int FIXED=36;
  - 头部定长：magic4 + ver4 + gen8 + seq8 + nextId8 + count4。

- string basePath;

- int curSlot;

- long gen;

- long logStartSeq;

- long nextSegId;

- List<long> segIds;

- bool exists;

- string lastError;

- Manifest()

- static string SlotPath(string basePath, int slot)

- static Manifest Load(string basePath)
  - 读取双槽清单：两个槽都试，取 CRC 有效且代数最高的。
    都无效（全新库）时返回 `Exists` 为 false 的空清单。
    只读，不创建任何文件。

- bool ParseSlot(string path, out long gen, out long seq, out long nextId, List<long> ids)
  - 解析单个槽文件；任何不一致都按无效处理返回 false。

- bool Exists()
  - 是否存在有效清单（全新库为 false）。

- long LogStartSeq()
  - 日志水位：seq < 该值的日志帧已被段覆盖。

- long NextSegId()
  - 下一个新段应使用的段号。

- List<long> SegIds()
  - 当前有效段号列表（内部列表，调用方只读）。

- string GetError()
  - 最近一次错误描述。

- bool Save(long newLogStartSeq, long newNextSegId, List<long> ids)
  - 原子发布新代：payload 写到非当前槽、截断对齐、
    fsync。失败时旧代清单仍然有效。


## PageFile (class)

ZanDB 存储引擎使用的随机访问、页粒度的文件。

所有读写都是定位式的（POSIX 上为 pread/pwrite，Windows 上为
文件描述符上的 seek+read）：它们不依赖共享的文件
位置，因此多个数据库句柄——以及多个进程——可以在同一文件上
工作而不互相踩踏偏移量。stdio
FILE* 仅用于可移植地创建/打开文件；从不发出缓冲的 stdio
读写，因此 fd 级别的 IO 和 fsync 能看到一切。

PageFile f = PageFile.Open("data.zdb");
ByteBuffer page = ByteBuffer.Alloc(4096);
f.ReadAt(0, page, 4096);
f.WriteAt(4096, page.Raw(), page.Length());
f.Sync();
f.Close();

- static int LOCK_TIMEOUT_MS=60000;
  - `LockExclusive` 在报告失败前等待当前持有者
    多久。

- nint fp;

- int fd;

- string path;

- string lastError;

- [DllImport("crt")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern int fclose(nint fp);

- [DllImport("crt", EntryPoint="_fileno")]static extern int FileNo(nint fp);

- [DllImport("crt", EntryPoint="_commit")]static extern int SyncFd(int fd);

- [DllImport("crt", EntryPoint="_lseeki64")]static extern long SeekFd(int fd, long offset, int origin);

- [DllImport("crt", EntryPoint="_read")]static extern int ReadFd(int fd, nint buf, int count);

- [DllImport("crt", EntryPoint="_write")]static extern int WriteFd(int fd, nint buf, int count);

- [DllImport("crt", EntryPoint="_chsize_s")]static extern int TruncFd(int fd, long size);

- [DllImport("crt", EntryPoint="_locking")]static extern int LockFd(int fd, int mode, int length);

- [DllImport("crt", EntryPoint="fileno")]static extern int FileNo(nint fp);

- [DllImport("crt", EntryPoint="fsync")]static extern int SyncFd(int fd);

- [DllImport("crt", EntryPoint="lseek")]static extern long SeekFd(int fd, long offset, int origin);

- [DllImport("crt", EntryPoint="pread")]static extern int PReadFd(int fd, nint buf, int count, long offset);

- [DllImport("crt", EntryPoint="pwrite")]static extern int PWriteFd(int fd, nint buf, int count, long offset);

- [DllImport("crt", EntryPoint="ftruncate")]static extern int TruncFd(int fd, long size);

- [DllImport("crt", EntryPoint="flock")]static extern int FlockFd(int fd, int op);

- PageFile()
  - 私有构造；统一经 `Open` 创建。

- static PageFile Open(string path)
  - 为随机读写打开（或创建）一个文件。

- bool IsOpen()
  - 文件是否成功打开。

- string GetError()
  - 最近一次操作的错误描述（无错为空串）。

- int ReadAt(long offset, ByteBuffer buf, int count)
  - 从 offset 处读取 count 字节到 buf（先清空 buf）。
    返回实际读取的字节数。

- int ReadRaw(long offset, nint raw, int count)
  - 定位读取到原始块中。

- int WriteAt(long offset, nint raw, int count)
  - 从原始块向 offset 处写入 count 字节。
    返回写入的字节数。

- long Append(nint raw, int count)
  - 在文件末尾追加 count 字节；返回数据落盘的偏移量，
    出错时返回 -1。

- long Size()
  - 当前文件大小（字节）。

- void Flush()
  - 缓冲写入的 flush；定位 IO 无缓冲，因此该方法
    仅为 API 兼容而保留。

- bool Sync()
  - 将写入的数据强制落盘到稳定存储。成功时返回 true
    成功。

- bool Truncate(long size)
  - 把文件截断/扩展到 size 字节。日志重放后用
    它清掉撕裂尾部。

- bool LockExclusive()
  - 获取整个文件的独占锁，等待当前
    持有者释放。若进程
    退出，锁由操作系统释放，因此崩溃的写入者不会卡死数据库。

- bool TryLockExclusive()
  - 非阻塞独占锁：立即返回，抢不到即 false。用于
    「检测到别的持有者就拒绝打开」而不是排队等待。

- bool Unlock()
  - 释放 LockExclusive 取得的锁；成功返回 true。

- void Close()
  - 关闭文件。进程退出时操作系统会兜底释放，但显式关闭
    能及时让出锁与句柄。


## RangeFilter (class)

待处理的范围约束 [lo, hi]（闭区间，序数比较）。

- public string field;

- public string lo;

- public string hi;

- public RangeFilter(string field, string lo, string hi)


## ScanRow (class)

扫描行视图：`ZanStore.ScanRows` 回调参数。值默认
零拷贝——按块内偏移/长度/指针暴露，调用 Key()/Val() 才物化
字符串（同一行内缓存，推进到下一行自动失效）。多源归并退化
路径（memtable 有未固化数据）时 `Block` 为 null，
键值直接以字符串给出。行数据仅在回调返回前有效。

- SegCursor cur;

- string memKey;

- string memVal;

- void Bind(SegCursor c)
  - 绑定到段游标当前记录（零拷贝模式）。

- void BindMem(string k, string v)
  - 绑定到一对字符串（多源归并退化模式）。

- string Key()
  - 当前行的键（惰性物化，行内缓存；退化模式直接给字符串）。

- string Val()
  - 当前行的值字符串（惰性物化，行内缓存）。

- ByteBuffer Block()
  - 值所在块缓冲；退化模式下为 null，调用方须走字符串路径。

- int ValOff()
  - 值在块内的起始偏移（退化模式无意义）。

- int ValLen()
  - 值的字节长度。

- nint ValPtr()
  - 值字节的裸指针（等于 Block().Raw() + ValOff()）。


## SegCursor (class)

段内拉式游标：定位 fromKey（含）起顺序推进，块读完自动换块。
供 `ZanStore` 的多路归并扫描使用；扫描走顺序流读、
不经过块缓存（点读才占缓存，顺序扫不污染它）。键/值字符串
**惰性物化**——裸扫描路径只走偏移/长度与块内指针，命中行才
调用 Key()/Val() 分配。用完必须 `Free`。

- [DllImport("crt", EntryPoint="memcmp")]static extern int MemCmp(nint a, string b, long n);

- SegmentFile seg;

- ByteBuffer buf;

- int blockIdx;

- bool valid;

- string curKey;

- bool keyMat;

- string curVal;

- bool valMat;

- int curOp;

- int curKeyOff;

- int curKeyLen;

- int curValOff;

- int curValLen;

- SegCursor(SegmentFile seg)

- string Key()
  - 当前记录的键（Valid() 为假时无意义）。首次调用物化并缓存。

- string Val()
  - 当前记录的值（墓碑时为空串）。首次调用物化并缓存。

- int Op()
  - 当前记录类型：1=put、2=墓碑。

- bool Valid()
  - 是否还有未消费的记录。

- int KeyOff()
  - 当前键在块内的起始偏移（Valid() 为假时无意义）。

- int KeyLen()
  - 当前键的字节长度。

- int ValOff()
  - 当前值在块内的起始偏移（墓碑时无意义）。

- int ValLen()
  - 当前值的字节长度（墓碑为 0）。

- ByteBuffer Block()
  - 当前块缓冲（键/值偏移基于它；回调期间可随机读）。

- int CmpKeyRaw(string other)
  - 当前键与 other 的序数比较：块内指针 memcmp 前缀、
    长度定序——与 String.CompareOrdinal 同序（本层键为字节串，
    每字符一字节），零分配。

- static SegCursor Open(SegmentFile seg, string fromKey)
  - 打开游标并定位到第一个 key ≥ fromKey 的记录；
    段为空或全部记录都小于 fromKey 时 Valid() 为假。

- bool LoadCur()
  - 加载 blockIdx 块并裸解析其首条记录；越界/腐坏/空块置 invalid。
    块缓冲跨块复用（段不可变，重读只走 IO 不再分配）。

- void Next()
  - 推进到下一条记录（惰性缓存一并失效）；段耗尽后 Valid() 变假。

- void Free()
  - 释放当前块缓冲；之后 Valid() 恒为假。


## SegRef (class)

段句柄：段文件与其全局段号的配对。

- SegmentFile seg;

- long id;

- SegRef(SegmentFile seg, long id)


## SegmentFile (class)

不可变有序段文件的读取端：稀疏索引二分定位块（一次块读 +
CRC 校验），块内顺序解析。点查与范围扫描共用同一套块读取。

- static string MAGIC="ZSEG";

- PageFile file;

- string path;

- List<long> sparseOff;

- List<string> sparseKey;

- List<long> sparseCrc;

- List<int> blockVerified;

- long indexStart;

- long recordCount;

- string lastError;

- SegmentFile()

- static SegmentFile Open(string segPath)
  - 打开并校验一个已完成的段：读尾部定位索引区，
    加载稀疏索引与块 CRC。头部/尾部/索引损坏时报错，
    记录区损坏在读块时才发现。

- string Path()
  - 段文件路径。

- long Count()
  - 段内记录数。

- string GetError()
  - 最近一次错误描述（块 CRC 不符也记在这里）。

- int BlockFor(string key)
  - 定位 key 所在或其插入位置的块号：最后一个
    块首键 <= key 的块；key 小于所有块首键时返回 0。

- int BlockCount()
  - 稀疏索引覆盖的块数（0 = 空段）。

- ByteBuffer LoadBlockSpan(int fromBlock, int toBlock, ByteBuffer reuse)
  - 块跨度整读：把块 [fromBlock, toBlock) 的文件区间
    一次 ReadAt 读入（块在段文件里物理连续），供块级内核跨块
    连续解析——把每块一次的系统调用摊成每跨度一次。 CRC 逐块
    校验（块边界在缓冲内偏移已知；用 NativeMemory.Crc32 直接
    对缓冲内子区间算，不复制）。未固化校验的块标记为已验。
    失败返回 null 并在 GetError 留痕；缓冲归调用方 Free。

- long BlockFileOff(int i)
  - 块 i 的文件偏移（块跨度整读用）。

- int BlockLen(int i)
  - 块 i 的字节长度（块跨度整读用）。

- ByteBuffer LoadBlock(int i)
  - 加载块 i 并校验 CRC；失败（越界/腐坏/短读）返回 null
    并在 `GetError` 留痕。缓冲区归调用方 Free。

- ByteBuffer LoadBlockReuse(int i, ByteBuffer reuse)
  - `LoadBlock` 的复用形式：给定非 null 的
    reuse 缓冲时原地扩容读入（容量足够则零分配），消除每块的
    Alloc/Free。段不可变——CRC 每块只在首次加载时校验一次，
    之后同一打开会话内的重复读取免 CRC（读时不算派生数）。

- static bool NextRecord(ByteBuffer b, out string key, out string val, out int op)
  - 块内顺序解析下一条记录到 key/val/op；
    缓冲耗尽返回 false。读位置推进到下一条记录。

- static bool NextRecordRaw(ByteBuffer b, out int keyOff, out int keyLen, out int valOff, out int valLen, out int op)
  - 裸解析块内下一条记录：不物化任何字符串，只输出
    键/值在块内的绝对偏移与长度（读游标推进到记录尾）。
    块耗尽或腐坏返回 false。

- static int ScanBlock(ByteBuffer b, string key, out string val)
  - 块内顺序查找 key：1=命中（val 为值）、2=墓碑、
    0=不存在（含越过 key 的位置）。读完即止，不做全块解析。

- int Lookup(string key, out string val)
  - 点查：0=不存在，1=命中（val 为值），
    2=墓碑（键在本段被删，查找到此为止）。块腐坏按不存在
    处理并留错误痕迹。

- int Lookup(string key, out string val, BlockCache cache, long segNo)
  - 点查（带块缓存）：先借缓存 pin，未命中则加载
    块并入库。val 是字符串拷贝，pin 释放后仍然有效。
    segNo 是本段的全局段号，用于跨段区分缓存键。

- int Scan(string fromKey, SegVisit visit)
  - 从 fromKey（含）起顺序扫描全部记录，逐条交给
    visit（返回 false 早停，该条不计入）。跨块推进，直到
    记录区终点。返回 visit 接受的记录数。

- void Close()
  - 关闭底层文件句柄。


## SegmentWriter (class)

不可变有序段文件的写入端：调用方按
`String.CompareOrdinal` 升序逐条 `Add`，
`Finish` 时回读校验并落稀疏索引。

文件布局（小端）：
头部   "ZSEG" + u32 version + u64 记录数          （16 字节）
记录   u8 op（1=put 2=del）+ varint 键 + [varint 值]
（键按 CompareOrdinal 升序，由写入方保证）
索引   u32 项数 + 每项 { u64 块偏移 + varint 块首键
+ u32 块 CRC }
尾部   u64 索引偏移 + "ZSEG"                      （12 字节）

索引项覆盖的块 = 本项偏移到下一项偏移（末项到索引区起点）。
CRC 在 Finish 时回读整段算出——「创建即校验」：之后每次
块读取都能凭它发现磁盘腐坏。

- static string MAGIC="ZSEG";
  - 段文件魔数（头尾各一次）。

- static int VERSION=1;

- static int SPARSE_EVERY=16;
  - 每 N 条记录落一个稀疏索引项（第 0 条必落）。

- static int WRITE_BUF=262144;
  - 记录缓冲达到该字节数就刷盘。

- PageFile file;

- ByteBuffer buf;

- long fileLen;

- long recCount;

- List<long> sparseOff;

- List<string> sparseKey;

- bool failed;

- string lastError;

- SegmentWriter()

- static SegmentWriter Create(string path)
  - 创建段文件并写入头部占位（记录数在
    `Finish` 时回填）。

- bool Failed()
  - 写入是否已失败。

- string GetError()
  - 最近一次错误描述。

- void Add(string key, string val, int op)
  - 追加一条记录；键必须严格升序（由调用方保证，
    内存排序的结果天然满足）。op 1=put、2=del（del 不带值）。

- void FlushBuf()
  - 缓冲刷到文件当前末尾。

- bool Finish()
  - 完成段：刷记录 → 回读逐块算 CRC 写索引 →
    写尾部 → 回填头部记录数 → fsync。成功后调用方应立刻
    丢弃本写入端（段不可变）。


## ZanDatabase (class)

嵌入式文档数据库，存储原生 Zan 值树（JsonValue：
整数、浮点数、布尔、字符串、嵌套对象、数组）——无 ORM 映射、
无 SQL 字符串。底层为 `ZanStore`：追加 CRC 帧日志 +
不可变有序段 + 双槽清单，崩溃安全；memtable 与块缓存皆有界。

ZanDatabase db = ZanDatabase.Open("app.zdb");
Collection users = db.GetCollection("users");
JsonValue u = JsonValue.NewObject();
u.Put("name", JsonValue.NewStr("alice"));
u.Put("age", JsonValue.NewNum("30"));
int id = users.Insert(u);
JsonValue back = users.FindById(id);
db.Close();

- ZanStore kv;

- AsyncRwLock rw;

- List<string> colNames;

- List<Collection> cols;

- string lastError;

- ZanDatabase()

- static ZanDatabase Open(string path)
  - 以默认参数打开（不存在则创建）：分组提交档、
    64MB 固化阈值、32MB 块缓存。

- static ZanDatabase Open(string path, int durability)
  - 以显式持久化档打开（不存在则创建）：
    `ZanStore.DUR_SYNC` 每次提交 fsync、
    `ZanStore.DUR_GROUP` 分组提交、
    `ZanStore.DUR_NONE` 缓冲落盘。打开失败时
    `IsOpen` 为 false，原因见 `GetError`。

- bool IsOpen()
  - 打开是否成功（失败原因见 `GetError`）。

- string GetError()
  - 打开失败的错误文本；成功时为空串。

- void SetSyncEvery(int n)
  - fsync 频率：1 = 每次提交，N = 分组提交。

- Collection GetCollection(string name)
  - 返回指定名称的文档集合（惰性创建）。

- ZanStore Kv()
  - 直接访问底层的键/值存储。

- void Begin()
  - 将后续写入批量合并为一次原子提交。

- bool Commit()
  - 提交 `Begin` 以来的全部写入；返回是否成功落盘。

- void Checkpoint()
  - 将迄今写入的内容强制落盘到稳定存储。

- void Flush()
  - 把 memtable 中尚未固化的写入立即固化为新段
    （不归并）：归并/维护与「稳态查询」前置步骤——memtable
    空了聚合与计数才走块级内核，否则退化为归并扫描。见
    `ZanStore.FlushToSegment`。

- void MergeSegments()
  - 把全部已固化段归并为一个段：掉落被新版压住的
    旧版本与已无意义的墓碑，回收存储空间。见
    `ZanStore.MergeSegments`。

- int SegCount()
  - 当前已固化段数（观测/运维用）。

- int MergeSomeSegments(int maxMerge)
  - 增量归并：把最老 maxMerge（≥2）个段归并为一个，
    写阻塞被归并集规模封顶，反复调用收敛为单段。返回实际归并
    的段数（0 = 段不足 2 或在事务中）。见
    `ZanStore.MergeSomeSegments`。

- void SetAutoMerge(int atSegs, int mergeK)
  - 开启自动增量归并：固化新段后段数达 atSegs 就归并
    最老 mergeK 段（0 = 关闭，默认）。见
    `ZanStore.SetAutoMerge`。

- void Close()
  - 关闭底层存储与读写锁。


## ZanStore (class)

日志式键值存储：memtable + 不可变有序段 + 追加日志 + 双槽清单。

写路径：追加 CRC 帧日志（组提交）+ 更新 memtable；日志攒到
阈值（默认 64MB，可调）把 memtable 排序固化为新段、保存
manifest、截断日志。读路径：memtable → 内存索引（键 → 段号）
→ 目标段的块（有界块缓存，O(1) 点读）。恢复：逐段顺序扫描
重建索引（Bitcask 式，纯顺序读）+ 重放日志中 seq ≥ 水位的
尾部——「manifest 已存、日志未截断」的崩溃窗口由水位跳过。

内存有界：memtable ≤ 阈值，块缓存有硬上限；常驻索引每键
只有哈希项 + 4 字节段号。

记录帧、持久化三档、NUL-free 契约、单进程单写多读模型
与异步 API 的说明见各类注释（LogFile / Segment / Manifest）。

- static int DUR_SYNC=0;
  - 每次提交都 fsync。

- static int DUR_GROUP=1;
  - 分组提交：按次数/字节数窗口 fsync。

- static int DUR_NONE=2;
  - 只在 Checkpoint/Close 与缓冲攒满时落盘。

- static int GROUP_FLUSH_BYTES=524288;
  - GROUP 档攒满这么多字节提前 fsync。

- static int NONE_FLUSH_BYTES=1048576;
  - NONE 档攒满这么多字节强制 flush 进 OS 缓存。

- static long DEFAULT_FLUSH_THRESHOLD=67108864;
  - 默认 memtable/日志 flush 阈值（字节）。

- static long DEFAULT_CACHE_CAP=33554432;
  - 默认块缓存上限（字节）。

- static int KERNEL_SPAN_BYTES=262144;
  - 块级内核的读跨度（字节）：一次 ReadAt 摊掉这么多的逐块
    系统调用——扫描/聚合路径的 IO 下限由它决定。

- LogFile log;

- Dictionary <string, string> memtable;

- Dictionary <string, long> index;

- List<SegRef> segs;

- Manifest man;

- string basePath;

- BlockCache cache;

- AsyncRwLock rw;

- int durability;

- int syncEvery;

- int commitsSinceSync;

- long seq;

- long logStartSeq;

- long nextSegId;

- long flushThreshold;

- long recoverSeg;

- int txDepth;

- int txMark;

- Dictionary <string, string> txPut;

- Dictionary <string, string> txDel;

- Dictionary <string, long> tombstones;
  - 未固化墓碑：删掉的键在旧段里仍有旧版本，固化时
    必须把 op=2 记录写进新段压住它们，否则恢复扫描会让已删
    键复活（旧段版本重指索引）。

- int autoMergeAt;
  - 自动增量归并阈值：固化后段数 ≥ autoMergeAt 时就地
    归并 autoMergeK 个最老段。0 = 关闭（默认）——归并代价显式
    交还调用方，自动策略只应被明确开启。

- int autoMergeK;

- string lastError;

- ZanStore()

- static string SegPath(string basePath, long id)

- static ZanStore Open(string path)
  - 打开（必要时创建）分组提交档的存储。

- static ZanStore Open(string path, int durability)
  - 以显式持久化档打开：加载 manifest 与段、重建
    内存索引、按水位重放日志尾部。

- bool RecoverRecord(string key, string val, int op)
  - 恢复扫描回调：段内记录重指索引（段按号升序扫描，
    后段覆盖前段；墓碑摘除索引，为合并输出预留）。

- void ApplyFrame(ByteBuffer b)
  - 重放期间应用一帧；水位之下的陈旧头跳过。重放的删除同时
    进墓碑集——该删除尚未落段，下次固化必须带进新段。

- bool IsOpen()
  - 是否处于打开状态（打开无错误且底层日志可用）。

- string GetError()
  - 最近一次打开失败的错误文本；Open 失败时由它给出原因。

- long Version()
  - 该句柄已应用的记录数：单调递增，可当快照版本用。

- int Count()
  - 键值对总数：索引（已落段）+ memtable 中尚未落段的新键。

- int SegCount()
  - 已固化段的数量。

- long LogBytes()
  - 日志逻辑大小（字节）：0 表示全部数据已在段里。

- void SetDurability(int d)
  - 切换持久化档（立即生效）。

- void SetSyncEvery(int n)
  - GROUP 档的 fsync 窗口：N = 每 N 次提交至多
    一次 fsync（攒满 `GROUP_FLUSH_BYTES` 字节会提前）。

- void SetFlushThreshold(long bytes)
  - memtable/日志 flush 阈值（字节）：日志攒到该值
    即固化新段。默认 64MB；调小是合法配置（=1 即每次提交
    都固化），只拒绝非正数。

- void SetCacheCap(long bytes)
  - 块缓存字节上限。默认 32MB；调小是合法配置
    （缓存退化到直读），只拒绝非正数。

- void SetAutoMerge(int atSegs, int mergeK)
  - 开启自动增量归并：每次固化新段后若段数达到
    <paramref name="atSegs"/>，就地归并最老的
    <paramref name="mergeK"/> 个段（同
    `MergeSomeSegments`，有界停顿=写 k 段的耗时）。
    atSegs ≥ max(3, mergeK+1) 才生效——阈值太低会每次固化都
    归并、得不偿失；atSegs = 0 关闭（默认）。段数稳态在
    [atSegs-mergeK+1, atSegs] 间振荡，不再无界增长。

- string Get(string key)
  - 返回值；不存在时返回 ""（与「值为空串」不可区分，
    需要区分时用 `Contains`）。事务内可见未提交的
    本事务写入。

- bool Contains(string key)
  - 键是否存在。

- void Put(string key, string val)
  - 插入或替换一个键。除非处于 Begin()/Commit() 内，
    否则按当前持久化档立即落盘；日志攒满阈值自动固化新段。
    事务内只记覆盖层、不落帧——最外层 Commit 时每个键只落
    最终值一帧（批量写同一键 N 次只写 1 帧，日志写放大大幅
    下降；帧在提交前从未离开内存，回滚与崩溃语义不变）。

- bool Delete(string key)
  - 删除键；键存在时返回 true。事务内延迟到提交落帧。

- void Begin()
  - 开始一个批次：写入进入内存覆盖层（不落帧），
    最外层 Commit 时按覆盖层落帧、合并进 memtable 并按持久化档
    落盘。可嵌套。

- bool Commit()
  - 原子地持久化自最外层 Begin() 以来的全部写入。

- void AppendTxFrames()
  - 最外层提交时按覆盖层落帧：每个键一帧、只落最终值。
    帧只在内存缓冲中追加（磁盘写仍由 FinishCommit 统一做），
    键集在 txPut/txDel 间互斥，帧序与恢复无关。

- void Rollback()
  - 丢弃自最外层 Begin() 以来的全部写入：缓冲中的帧
    回退到事务起点，覆盖层清空。

- void MergeTxOverlay()
  - 最外层提交时把覆盖层合并进 memtable/索引/墓碑。

- void FinishCommit()
  - 按当前持久化档处理缓冲：flush / 窗口化 fsync；日志攒满
    阈值时固化新段。

- void FlushToSegment()
  - 把 memtable 固化为新段：put 与未固化墓碑按键序
    合并写段 → fsync → 保存 manifest（水位 = 当前 seq）→ 截断
    日志。墓碑必须随段持久化：被删键在旧段里仍有旧版本，没有
    新段墓碑压住，恢复扫描就会把它复活。任一步失败都保持日志
    兜底（旧 manifest + 完整日志 = 完好状态）；成功后日志归零。
    须在写侧串行环境调用（同步核心即满足；协程请走
    `FlushAsync`）。

- void MergeSegments()
  - 把全部已固化段（≥2 个）归并为一个新段：同键只留
    最新版本，墓碑直接掉落（归并集=全部段，不存在会复活的更老
    版本），索引按归并结果重建。须在写侧串行环境调用且不在事务
    中（协程请走 `MergeAsync`）；期间阻塞写入——
    在线换段优化留待后续。崩溃窗口两段皆一致：段已写清单未换
    = 孤儿段，下次同号归并覆盖之；清单已换 = 旧段成孤儿（不再
    被引用，仅占盘）。内存峰值 ≈ 存活键数（重建索引用）。

- async void MergeAsync()
  - 独占锁下的 `MergeSegments`。

- static bool IdInList(List<long> ids, long id)
  - 线性查小列表（归并集段号，k 很小）。

- int MergeSomeSegments(int maxMerge)
  - `MergeSegments` 的增量形式：把最老的
    maxMerge（≥2）个段归并为一个新段，其余较新段原样保留——
    写阻塞时长被归并集规模封顶，反复调用最终收敛为单段。索引
    原地改指：只有键索引仍指向归并集的键才重指/摘除；指向未
    归并新段的键保持原指（更新版本继续赢），且这样的键**不写
    入新段**——重开恢复按清单序「后扫覆盖前扫」时才不会让旧
    版本压住未归并段的新版本。归并集内同键由游标新→旧序去重，
    未固化墓碑压键与崩溃窗口语义同全量归并。返回实际归并的
    段数（0 = 无事可做：段不足 2 或在事务中）。

- void AppendRec(int op, string key, string val)
  - 追加一条记录帧并推进 seq。

- int Scan(string fromKey, StoreVisit visit)
  - 按键序（`String.CompareOrdinal` 升序）
    归并扫描 fromKey（含）起的全部存活键值：memtable 与各段
    多路归并，同键取最新版本（memtable > 更大段号），墓碑
    隐藏。visit 返回 false 早停。看到的是已提交视图（不含未
    提交事务的覆盖层）；memtable 部分每次扫描现排序，段部分
    顺序流读、不占用块缓存。返回 visit 接受的键数。

- int ScanBetween(string startKey, string endKey, StoreVisit visit)
  - `Scan` 的有界形式：只访问
    [startKey, endKey] 闭区间内的键，上界在归并循环内判定，
    调用方无需在回调里逐键比对。返回 visit 接受的键数。

- int ScanRows(string startKey, string endKey, ScanRowVisit visit)
  - `ScanBetween` 的零拷贝形式：交付
    `ScanRow` 而非字符串——单段且 memtable 无未固化
    数据时值不物化、上界与墓碑判定全走块内指针；memtable 有
    参与则退化为字符串路径包装。语义与 ScanBetween 完全一致，
    行数据仅在回调返回前有效。返回 visit 接受的键数。

- int CountRange(string startKey, string endKey)
  - 统计 [startKey, endKey] 闭区间内的存活键数，
    不做任何回调：单段且 memtable 无参与时走块级内核直数
    （免游标/回调对象），否则退化为归并扫描。返回键数。

- int TryScanKernel(string startKey, string endKey, KernelVisit visit)
  - `ScanKernel` 的区间形式：仅当处于
    单段稳态（memtable 空）时走块级内核；多源归并或 memtable
    有未固化数据时返回 -1（调用方须退回 `ScanRows`
    字符串路径）。endKey 为空串 = 无上界。

- int ScanKernel(SegmentFile seg, string startKey, string endKey, KernelVisit visit)
  - 块级聚合内核（单段快速路径）：把块跨度一次读入（每
    `KERNEL_SPAN_BYTES` 一次系统调用，块在段文件里物理
    连续、记录解析不依赖块边界）后循环裸解析记录，把（基址+偏移+
    长度）直接交给 visit——不经 SegCursor/ScanRow 对象、不经每行
    delegate 之外的任何抽象；op=2 的段内墓碑由这里跳过（op=2 记
    录无值）。墓碑集非空时逐键 memcmp 比对（未固化删除极少，比
    对成本可忽略）；visit 返回 false 早停。返回 visit 接受的行数。
    
    常态扫描（固化+归并后单段、memtable 空）的聚合与计数走
    这里——每行成本 = varint 解码 + 一次回调，无对象分配；每
    跨度成本 = 一次 ReadAt + 逐块 CRC（同会话首遍）。

- async int ScanAsync(string fromKey, StoreVisit visit)
  - 共享锁下的 `Scan`；可与并发写入共存。

- int ScanRange(string startKey, string endKey, int limit, List<KvEntry> output)
  - 物化式范围扫描：startKey（含）到 endKey（含，与
    旧引擎 <c>BTree.Scan</c> 语义一致）按键序收集到 output；
    limit > 0 时至多 limit 条。返回收集的条数。

- int ScanPrefix(string prefix, int limit, List<KvEntry> output)
  - 前缀扫描：所有以 prefix 开头的键值按键序收集；
    limit > 0 时至多 limit 条。返回收集的条数。

- int TryScanKeyKernel(string startKey, string endKey, KernelKeyVisit visit)
  - `ScanKernel` 的键只读形式：不解析值、
    只把（基址+键偏移+键长）交给 visit，供覆盖索引路径
    （索引条目键自带 id/值段，零文档读也零字符串物化）使用。
    单段稳态前提与 `TryScanKernel` 相同，否则返回 -1。
    返回 visit 接受的键数。

- int ScanKeyKernel(SegmentFile seg, string startKey, string endKey, KernelKeyVisit visit)
  - `ScanKernel` 的键只读内档：与行版同
    跨度整读与上界/墓碑逻辑，只是不读值、交付键区间。

- void Checkpoint()
  - 将缓冲与 OS 缓存全部强制落盘。

- List<long> SegIdList()
  - 当前全部段号列表。

- SegRef FindSeg(long id)
  - 按段号找段句柄。

- void Close()
  - 落盘并关闭日志、段、缓存与进程内读写锁。

- static void SortStrings(List<string> a)

- static void SortRange(List<string> a, int lo, int hi)

- static void Swap(List<string> a, int i, int j)

- async string GetAsync(string key)
  - 共享锁读取；可安全地与并发写入共存。

- async void PutAsync(string key, string val)
  - 独占锁写入并按当前持久化档落盘。

- async bool DeleteAsync(string key)
  - 独占锁删除一个键；返回键此前是否存在。

- async void FlushAsync()
  - 独占锁下把 memtable 固化为新段。

- async string UpdateAtomic(string key, StoreUpdater fn)
  - 原子读改写：fn 在写临界区内看到最新已提交值
    （不存在时为 ""），因此同一键上的并发 UpdateAtomic
    不会丢失更新。返回最终存储的结果。


## JsonValue (delegate)

在写临界区内根据当前文档计算新文档
（id 不存在时为 null）。返回 null 表示保持
文档不变。

`delegate JsonValue DocUpdater(JsonValue current);`


## bool (delegate)

`Collection.ScanField` 的访问器：id 为文档 id，
v 为该文档 field 字段的值（每篇文档只有这一个字段被解码）。
返回 false 结束扫描。

`delegate bool FieldVisit(int id, JsonValue v);`


## bool (delegate)

`Collection.ScanNumField` 的访问器：
id 为文档 id，v 为字段的数值形态（免 JsonValue 树）。

`delegate bool NumVisit(int id, long v);`


## bool (delegate)

`Collection.ScanStrField` 的访问器：
id 为文档 id，v 为字段的文本形态（免 JsonValue 树）。

`delegate bool StrVisit(int id, string v);`


## bool (delegate)

返回 true 时保留该文档。

`delegate bool DocPredicate(JsonValue doc);`


## bool (delegate)

扫描回调：op 1=put、2=del（del 是段内墓碑，
合并时用于压掉旧段同名键）。返回 false 停止扫描。

`delegate bool SegVisit(string key, string val, int op);`


## bool (delegate)

扫描回调：按 键序升序 收到每个存活键的最新值；
返回 false 早停。

`delegate bool StoreVisit(string key, string val);`


## bool (delegate)

裸扫描回调：收到零拷贝扫描行（值按块内偏移/长度/指针
暴露，Key()/Val() 才物化字符串）；返回 false 早停。

`delegate bool ScanRowVisit(ScanRow row);`


## bool (delegate)

聚合内核回调：块级扫描按记录粒度交付（op=1 的存活 put），
键/值以（缓冲裸指针 + 偏移 + 长度）零拷贝给出——同一缓冲跨
块复用、回调期间有效。返回 false 早停整个扫描。

`delegate bool KernelVisit(nint basePtr, int keyOff, int keyLen, int valOff, int valLen);`


## bool (delegate)

覆盖索引内核回调：索引条目键以（缓冲裸指针 + 偏移 +
长度）零拷贝交付，id 段固定在键尾 6 字节；缓冲跨跨度复用、
回调期间有效。返回 false 早停。

`delegate bool KernelKeyVisit(nint basePtr, int keyOff, int keyLen);`


## int (delegate)

从文档中提取整数排序键。

`delegate int DocIntKey(JsonValue doc);`


## string (delegate)

从文档中提取字符串排序键。

`delegate string DocStrKey(JsonValue doc);`


## string (delegate)

`delegate string StoreUpdater(string current);`


## void (delegate)

重放回调：payload 为一帧载荷，读位置在 0、长度为载荷字节数。
缓冲区归 LogFile 所有并在下一帧复用，访客不得 Free；
需要保留内容时必须自行复制。

`delegate void LogFrameVisitor(ByteBuffer payload);`
