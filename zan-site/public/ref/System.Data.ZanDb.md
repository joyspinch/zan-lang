# System.Data.ZanDb

> 源码: `stdlib/System/Data/ZanDb/BTree.zan`, `stdlib/System/Data/ZanDb/Collection.zan`, `stdlib/System/Data/ZanDb/DbRegistry.zan`, `stdlib/System/Data/ZanDb/DocQuery.zan`, `stdlib/System/Data/ZanDb/KvStore.zan`, `stdlib/System/Data/ZanDb/PageFile.zan`, `stdlib/System/Data/ZanDb/Pager.zan`, `stdlib/System/Data/ZanDb/Wal.zan`, `stdlib/System/Data/ZanDb/ZanDatabase.zan`


## BLeafEntry (class)

一条已解码的叶子记录。内联值（inline value）的 overflowPage 为 0。

- string key;

- string val;

- int overflowPage;

- BLeafEntry(string key, string val, int overflowPage)


## BNode (class)

单个 B+Tree 节点（叶子或内部）的解码形式。节点被编码为
固定大小的 pager 页；修改作用于解码后的列表，然后
重新编码节点，放不下时分裂。

- bool leaf;

- List<BLeafEntry> entries;

- List<string> keys;

- List<int> children;

- BNode(bool isLeaf)

- static BNode Leaf()

- static BNode Internal()

- BNode Copy()
  - 独立副本。从已提交页面解码出的节点
    会与节点缓存以及该快照的并发读者共享，
    因此写入方修改副本并存放到新页面。


## BTree (class)

基于 `Pager` 的写时复制磁盘 B+Tree：字符串键有序，
值为字符串，支持基于游标的范围扫描。

页面一旦属于已提交快照就绝不就地修改：
更新时把叶子复制到新页面，并重写直至根节点的路径，
随后 pager 在 meta 页中发布新根。这正是
读者无需加锁也安全的原因——它遍历的页面不会在脚下改变——
也正是不需要 redo log 提交也原子的原因。

因为页面的父节点反正会被重写，叶子不带兄弟指针
（写时复制下不修改左邻节点就没法维护它）；
范围扫描改用显式的游标栈。

大于 INLINE_MAX 的值存放在链式 overflow 页面中，因此值的
大小没有上限；键必须能放进一个节点。

- static int PAGE_SIZE=4096;

- static int MATCH_RANGE=0;

- static int MATCH_PREFIX=1;

- static int TYPE_LEAF=1;

- static int TYPE_INTERNAL=2;

- static int TYPE_LEAF2=3;

- static int INLINE_MAX=1024;

- static int OVF_CHUNK=4000;

- Pager pager;

- string lastError;

- NodeCache nodeCache;

- BTree()

- static BTree Open(Pager pager)
  - 打开 pager 中存储的树；新数据库会创建空根节点
    。

- string GetError()

- int RootId()

- void Sync()
  - 当 pager 采纳了另一进程的提交时丢弃已解码的节点：
    页面 id 现在可能存放了不同的内容。

- static void InsEntry(List<BLeafEntry> l, int idx, BLeafEntry v)

- static void InsStr(List<string> l, int idx, string v)

- static void InsInt(List<int> l, int idx, int v)

- static int Compare(string a, string b)

- string ReadChain(int firstPage)
  - 将整条 overflow 链读回为一个字符串。

- int WriteChain(string val)
  - 把 val 写入一串新分配的 overflow 页面，
    返回第一个页面的 id。

- void FreeChain(int firstPage)
  - 把一条 overflow 链交给 free list。这些页面在
    没有快照引用前仍保持可读。

- BNode ReadNode(int pageId)

- static int EncodedSize(BNode n)

- void WriteNode(int pageId, BNode n)
  - 把节点编码进属于当前事务的页面
    。

- int WriteNodeCow(int oldPageId, BNode n)
  - 存储修改后的节点：页面由当前事务创建时原地写入，
    否则写入新页面并返回其 id，
    旧页面进入 free list（写时复制）。

- BNode NodeForWrite(int pageId)
  - 返回要修改的节点：当页面属于当前事务时返回缓存的节点
    （没有别人能看到它），否则返回
    副本。

- static int ChildIndex(BNode n, string key)
  - 给定 key 要下钻到的子节点索引：二分查找
    第一个严格大于 key 的分隔键。

- static int LowerBound(BNode n, string key)
  - 二分查找第一个键 >= key 的叶子条目。

- static int LeafFind(BNode n, string key)
  - key 在叶子中的位置，未找到返回 -1。

- bool Get(string key, List<string> outVal)
  - 查找键。找到时返回 true 并把值存入 outVal
    （单元素列表）。

- bool Contains(string key)

- void Put(string key, string val)
  - 在当前进行中的写入事务里插入或替换 key -> val
    （pager 提交后即持久化）。

- void ReplaceUp(List<int> path, List<int> pathIdx, int level, int newChildId)
  - 重写被复制节点之上的路径，让它指向新页面，
    直到根节点（pager 会重新发布根）。

- void InsertUp(List<int> path, List<int> pathIdx, int level, int leftId, string sep, int rightId)
  - 向上传播分裂：把（可能被复制的）左子节点、
    分隔键和新的右子节点插入父节点。

- bool Delete(string key)
  - 删除一个键，存在时返回 true。空叶子节点
    留在原位，由以后压缩回收（暂无合并）。

- void FreeLeafPage(int pageId)
  - 释放一个叶子页面及其引用的 overflow 链，
    不解码其中的值：批量删除不应为读取
    即将丢弃的数据付出代价。

- int DeleteRange(string startKey, string endKey)
  - 删除满足 startKey <= key <= endKey 的所有键
    （endKey 为 "" 表示直到末尾），返回删除的数量。
    
    这是批量路径：完全落在范围内的叶子页会整页交给
    free list——不解码、不重写——沿途每个
    父节点只重写一次，所以删除一批的开销约为
    每层一次页面写入加两个边界叶子，而不是
    每个键重写一次路径。因此同一批的键必须共享一个
    前缀（例如 "kw:<planId>:"），使整批成为单个范围。

- int DeletePrefix(string prefix)
  - 删除所有以 prefix 开头的键——“丢弃这一批”——
    并返回被删除的键数量。

- int DeleteMatching(string startKey, string endKey, int mode)

- static bool InRange(string key, string startKey, string endKey, int mode)

- int PruneNode(int pageId, string startKey, string endKey, int mode, List<int> removed)
  - 删除一个子树内的区间。返回（可能是
    复制后的）页 id；若整个子树都落在区间内则返回 0，且
    其页面进入了空闲列表。

- int Scan(string startKey, string endKey, int limit, List<KvEntry> output)
  - 收集最多 limit 条满足 startKey <= key <= endKey 的条目
    （endKey "" 表示无上限）写入 output。用游标栈遍历树：
    在写时复制下，叶子节点无法保留指向兄弟节点的指针，
    因为在那里插入就必须同时重写左侧邻居。

- int Count()
  - 键的总数。遍历整棵树，但只读取每个叶子头部的
    条目计数，因此值永远不会被解码。

- int CountNode(int pageId)


## Collection (class)

`ZanDatabase` 中一组有名字的文档。文档是
JsonValue 对象树，每篇文档分配一个自增整数 _id。
存储键在共享 B+Tree 中以命名空间区分：
c:{name}:{id}                   文档正文（key-dict 编码的 JSON）
m:{name}:keys                   字段名字典（JSON 数组）
m:{name}:seq                    最后分配的 id
m:{name}:idx                    逗号分隔的索引字段列表
m:{name}:idxpos:{field}         批量索引构建游标（缺失表示已构建完成）
i:{name}:{field}:{value}\t{id}  二级索引条目 -> id
id 用零填充，因此 B+Tree 顺序即插入顺序，使 FindAll
和范围迭代变成顺序的页读取。

- KvStore kv;

- AsyncRwLock rw;

- string name;

- List<string> indexedFields;

- List<string> keyDict;

- bool keyDictDirty;

- long metaVersion;

- static int INDEX_BATCH=500;
  - 批量索引构建中每个事务索引的文档数。

- [DllImport("crt", EntryPoint="memchr")]static extern nint MemChr(nint p, int c, long n);

- Collection()

- static Collection Attach(KvStore kv, AsyncRwLock rw, string name)

- void SyncMeta()
  - 当数据库更新到新版本（即其他进程已提交）时，重新读取缓存的元数据（字段名字典、索引
    列表）。操作文档前两者都必需：
    字典负责编解码对象键，因此过期的副本会
    用错误的字段名解码其他进程的文档，并将这些 id
    复用到不同的名称上。开销低：一次版本比较，
    两次读取只在确实有变化时
    才发生。

- string Name()

- static int ID_DIGITS=6;

- static string PadId(int id)

- static int UnpadId(string s)
  - PadId 的逆操作。

- string DocKey(int id)

- string SeqKey()

- string IdxMetaKey()

- string KeysMetaKey()

- string IdxPosKey(string field)

- string IdxPrefix(string field, string val)
  - 同一索引字段所有条目共享的前缀。字段用
    其字典 id 表示而非拼出全名，因此长字段名
    对每个条目零开销。

- string IdxKey(string field, string val, int id)

- void LoadIndexMeta()

- void LoadKeyDict()

- int KeyId(string k)

- int KeyIdOf(string k)
  - 字段名的字典 id；若该集合从未
    存储过该字段则返回 0。与 KeyId 不同，此方法不会扩充字典，
    因此可在读取路径安全使用。

- string KeyName(int id)

- void PersistKeyDict()

- static bool IsIntStr(string s)

- static int ParseI64(string s)

- void WriteVal(ByteBuffer b, JsonValue v)

- string EncodeDoc(JsonValue doc)
  - 将文档序列化为紧凑的二进制记录：一个 'B'
    格式字节，随后是顶层对象的字段（字典 id 键 +
    带类型的值）。冗余的 "_id" 被省略——它是行键，读取时
    重新附加。必须在 kv 事务内运行：新字段名
    会与正文一起持久化。

- JsonValue ReadVal(ByteBuffer b)

- static int IdOfKey(string key)
  - 键尾随数字中编码的 id。行键和
    索引条目都以定宽 id 结尾，因此无需搜索分隔符——
    id 的某个数字可能恰好像分隔符。

- static string IdFromKey(string key)

- ByteBuffer OpenRecord(string body)
  - 将二进制记录正文（见 EncodeDoc）解码回
    JsonValue 对象。"_id" 不存储在正文中；调用方从
    行键取出并附加。
    包装存储的记录以供读取，仅在记录
    确实包含转义字节时才反转义。EncodeDoc
    写出的记录很少包含（标签 9 和 10 让常见单元格不含 0x00），
    因此通常只需一个缓冲区，无需逐字节复制。

- void SkipVal(ByteBuffer b)
  - 跳过一个编码值，而不从中构建任何内容。
    字符串和文本形式的数字按长度跳过，而非
    复制。

- JsonValue FieldOf(string body, int kid)
  - 直接从存储的记录中取出一个字段：其他字段
    被跳过而非解码，因此对大集合做过滤
    不会为每篇文档构建 JsonValue 树。记录中
    没有该字段时返回 null。

- JsonValue DecodeDoc(string body)

- void AddIndexEntries(int id, JsonValue doc)

- void RemoveIndexEntries(int id, JsonValue doc)

- int NextId()

- int Insert(JsonValue doc)
  - 存储新文档，分配并返回其 _id
    （同时写入文档中）。

- int InsertBulk(List<JsonValue> docs)
  - 在单个原子提交中批量插入（整个批次
    只做一次 WAL 刷新）。返回第一个分配的 id。

- JsonValue FindById(int id)
  - 返回文档；不存在则返回 null。

- bool Exists(int id)

- bool Update(int id, JsonValue doc)
  - 替换现有文档；id 不存在时返回 false。

- void Upsert(int id, JsonValue doc)
  - 在显式 id 下插入或替换。推进自增 id
    序列越过该 id，避免后续插入冲突。

- bool DeleteById(int id)

- void PersistIndexMeta()

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

- bool IndexReady(string field)
  - 当字段已被索引且其批次构建
    完成（即索引覆盖所有文档）时为 true。

- List<JsonValue> FindByIndex(string field, string val)
  - 索引查找：字段等于 val（字符串形式）的文档，
    通过二级索引实现——无需全表扫描。索引仍在构建时
    回退为扫描，因此结果总是完整的。

- List<JsonValue> FindByFieldScan(string field, string val)
  - 通过扫描找出字段等于 val 的文档：每条
    记录只被询问这一个字段，只有匹配项会被完整解码。
    用于字段没有可用索引的情况。

- int CountByField(string field, string val)
  - 字段值等于 val 的文档数，无需具体化
    任何文档。

- int Count()
  - 文档总数（对命名空间做范围扫描）。

- List<JsonValue> FindAll()
  - 按 id 顺序返回所有文档。

- List<JsonValue> FindRange(int fromId, int toId, int limit)
  - 满足 fromId <= _id <= toId 的文档（0 表示无界），
    limit 0 表示不限数量，按 id 顺序返回。

- DocQuery Query()
  - 对集合启动流畅的 LINQ 风格查询。

- async JsonValue FindByIdAsync(int id)

- async int InsertAsync(JsonValue doc)

- async bool UpdateAsync(int id, JsonValue doc)

- async bool DeleteByIdAsync(int id)

- async bool UpdateAtomic(int id, DocUpdater fn)
  - 对单篇文档的原子读改写：fn 在
    写临界区内基于最新已提交版本运行，因此
    并发更新不会丢失写入（判断与写入是
    一个原子步骤）。当文档存在且 fn
    返回了替换值时返回 true。


## DbRegistry (class)

单个数据库文件的跨进程协调，分三部分：

* 写入互斥锁——对侧文件（"<db>-lock"）的独占锁。
持有者死亡时操作系统会自动释放，因此崩溃的
写入者绝不会卡死数据库（共享内存互斥锁则会）；
* 最后提交的事务 id，发布在共享内存中，因此
读取者可在约 100 纳秒内判断其缓存的页面是否仍然有效，
而不必在每次查找时重新从磁盘读取 meta 页；
* 读取者表：每个进程发布其当前正在读取的
最旧快照，这样写入者就知道哪些已释放的页仍对
某些进程可见、尚不能复用（MVCC）。

所有内容都通过哈希寻址（SharedTable 的 ByHash API），因此
热路径上不会格式化键，也不会分配字符串。

- static int MAX_READERS=512;

- SharedTable table;

- string dbName;

- PageFile lockFile;

- int slot;

- long slotHash;

- long wtxnHash;

- long slotsHash;

- long procsHash;

- int pid;

- bool locked;

- [DllImport("crt", EntryPoint="_getpid")]static extern int GetPid();

- [DllImport("kernel32", EntryPoint="OpenProcess")]static extern nint OpenProcess(int access, int inherit, int pid);

- [DllImport("kernel32", EntryPoint="CloseHandle")]static extern int CloseHandle(nint handle);

- [DllImport("crt", EntryPoint="getpid")]static extern int GetPid();

- [DllImport("crt", EntryPoint="kill")]static extern int KillSignal(int pid, int sig);

- DbRegistry()

- static string NameOf(string path)
  - 从数据库路径派生共享内存名称（只取文件名，
    这样打开同一数据库的所有进程都会在同一个
    注册表中相遇）。

- static DbRegistry Attach(string path)
  - 附加到（首次使用时创建）某个
    数据库文件的注册表。

- bool IsShared()

- int Processes()
  - 当前打开该数据库的进程数。
    单进程数据库可以完全跳过快照发布。

- long CommittedTxn()
  - 写入者发布的最后已提交事务 id
    （注册表出现后未提交过任何内容时为 0）。

- void PublishTxn(long txnid)

- bool LockWrite()
  - 阻塞直到本进程获得唯一的写入者槽位。

- void UnlockWrite()

- long HashOfSlot(int index)

- bool Alive(int otherPid)

- void ClaimSlot()
  - 为本进程认领一个读取者槽位，复用
    已死亡进程的槽位。每个数据库句柄调用一次。

- void PublishSnapshot(long txnid)
  - 发布本进程正在读取的快照（0 表示空闲）。
    单个共享内存存储，无需加锁。

- long OldestSnapshot()
  - 当前存活的进程正在读取的最旧快照；若没有进程
    在读取则返回 0。比它更新的事务释放的页面必须保持
    不动；更旧的页面则可复用。

- void Close()


## DocQuery (class)

针对 `Collection` 的流畅 LINQ 风格查询：类型安全的 lambda
谓词、索引加速的等值比较、O(n log n) 归并排序、
Skip/Take 分页。求值推迟到 ToList/First/Count 时才进行。

List<JsonValue> adults = users.Query()
.WhereEq("city", "北京")                       // uses 索引 若存在
.Where((JsonValue u) => u.Int("age", 0) >= 18) // lambda filter
.OrderByInt((JsonValue u) => u.Int("age", 0))
.Take(10)
.ToList();

- Collection col;

- List<DocPredicate> preds;

- List<EqFilter> eqFilters;

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

- static DocQuery Of(Collection col)

- DocQuery Where(DocPredicate pred)
  - Lambda 过滤器；多次调用彼此 AND。

- DocQuery WhereEq(string field, string val)
  - 字段等值约束（字符串形式）。当字段有
    二级索引时，候选集来自索引而非
    全表扫描；否则作为过滤器应用。

- DocQuery WhereEqInt(string field, int val)
  - 字段的整数等值约束。

- DocQuery OrderByInt(DocIntKey key)

- DocQuery OrderByIntDesc(DocIntKey key)

- DocQuery OrderByStr(DocStrKey key)

- DocQuery OrderByStrDesc(DocStrKey key)

- DocQuery Skip(int n)

- DocQuery Take(int n)

- List<JsonValue> Candidates()

- bool MatchesRest(JsonValue d)

- bool EqAppliedByScan()
  - 当候选集已经应用了第一个等值
    过滤器时为 true，重复检查将浪费工作。

- bool Matches(JsonValue d)

- void SortDocs(List<JsonValue> docs)
  - 对预计算的键做自底向上归并排序：O(n log n)，稳定。

- List<JsonValue> ToList()
  - 执行查询并具体化匹配的文档。

- JsonValue First()
  - 返回第一个匹配项，没有则返回 null。

- int Count()
  - 匹配数量（不应用排序/分页）。只有单个等值约束
    时无需具体化任何文档即可计数——字段已索引时
    用索引键，否则从每条记录读取
    一个字段来计数。


## EqFilter (class)

待处理的等值约束（field == val），在字段未被索引加速时
作为过滤器应用。用一个实体而非并行的
字段/值列表。

- string field;

- string val;

- EqFilter(string field, string val)


## KvEntry (class)

范围或前缀扫描返回的一条键/值记录。

- string key;

- string val;

- KvEntry(string key, string val)


## KvStore (class)

嵌入式键值存储：单文件、写时复制 B+Tree 索引、LRU 页
缓存（有硬性内存上限）、通过交替 meta 页实现原子提交。
打开即可使用的门面：

KvStore db = KvStore.Open("app.zdb");
db.Put("user:1", "alice");
string v = db.Get("user:1");
db.Close();

并发模型为单写多读：读取者从不阻塞
也不加锁，因为它们所读快照的页面
绝不会被原地修改；写入者通过跨进程锁串行化，因此
多个进程（例如 Web 服务器的工作进程）可以安全地
打开同一个文件。

写入自动提交。把多次写入包进 Begin()/Commit() 可使其成为
一个原子事务——也快得多，对 SSD 也更友好：
在一个事务内，无论页中有多少键改变，每页只复制一次。
协程安全的异步版本（GetAsync/PutAsync/UpdateAtomic）
还会额外串行化同一进程内的协程。

- Pager pager;

- BTree tree;

- AsyncRwLock rw;

- int txDepth;

- string lastError;

- KvStore()

- static KvStore Open(string path)
  - 打开（必要时创建）带 256 页（1 MB）
    缓存、每次提交都 fsync 的存储。

- static KvStore Open(string path, int cachePages)
  - 以显式页缓存容量打开（页大小为 4 KB）。

- bool IsOpen()

- string GetError()

- void SetSyncEvery(int n)
  - 数据页 fsync 频率：1 = 每次提交（默认），N = 每
    N 次提交。meta 页总是在数据页之后写入，因此
    更大的 N 最多丢失最近几次提交，绝不会损坏数据库。

- long Version()
  - 该句柄上次读取的快照的事务 id。

- int PageCount()
  - 文件中的页数，以及空闲列表中的页数——衡量
    空间增长的这两个数字。

- int FreePages()

- int PageWrites()
  - 自该句柄打开以来写入磁盘的页数：写放大
    测量的输入（写入的页数 vs. 实际变更的数据字节数）
    。

- void BeginSnapshot()
  - 固定一个快照供多次读取：直到
    EndSnapshot() 为止读到的都是同一版本的数据库，即使其他
    进程在提交。读取者不加锁；长快照的代价是
    其仍可见的页面在此期间不会被复用。

- void EndSnapshot()

- string Get(string key)
  - 返回值；不存在时返回 ""（参见 Contains）。

- bool Contains(string key)

- void Put(string key, string val)
  - 插入或替换一个键。除非处于
    Begin()/Commit() 内，否则自动提交。

- bool Delete(string key)
  - 删除键；键存在时返回 true。

- int DeleteRange(string startKey, string endKey)
  - 在单个事务中删除所有满足 startKey <= key <= endKey
    （endKey "" 表示一直到末尾）的键；返回删除的
    数量。完全落在区间内的叶子节点按整页
    释放，而非逐键重写，因此删除一批数据
    大约每个树层级一次页写入——当一批记录
    （一个方案、一个租户、一天）被整体替换时应使用此路径。

- int DeletePrefix(string prefix)
  - 删除所有以 prefix 开头的键（参见 DeleteRange）。

- void Begin()
  - 开始一个批次：取得写锁，累积写入，并在
    Commit() 时原子提交。可嵌套：只有最外层
    的 Commit 才会持久化。保持事务打开会占用写锁，因此
    批次要控制规模。

- bool Commit()
  - 原子地持久化自最外层 Begin() 以来的所有内容。

- void Rollback()
  - 丢弃自最外层 Begin() 以来写入的所有内容；
    数据库保持在之前的版本。

- int Scan(string startKey, string endKey, int limit, List<KvEntry> output)
  - 范围扫描：startKey <= key <= endKey（endKey "" 表示到末尾），
    limit 0 表示不限。返回追加的条目数。

- int ScanPrefix(string prefix, int limit, List<KvEntry> output)
  - 所有带给定前缀的键（例如 "user:"）。

- int Count()

- void Checkpoint()
  - 将迄今写入的所有内容强制落盘到稳定存储。

- void Close()

- async string GetAsync(string key)
  - 共享锁读取；可安全地与并发写入共存。

- async void PutAsync(string key, string val)
  - 独占锁写入并自动提交。

- async bool DeleteAsync(string key)

- async string UpdateAtomic(string key, KvUpdater fn)
  - 原子读改写：fn 在写临界区内看到
    最新的已提交值（不存在时为 ""），因此
    同一键上的并发 UpdateAtomic 调用不会丢失更新。返回
    最终存储的结果。


## NodeCache (class)

以页面 id 为键的固定容量解码节点缓存，热门查找
不必每次访问都重新把页面解码成键/值。条目描述的是
不可变的页面内容：被复制、释放或复用的页面会从
缓存中移除，快照推进时整个缓存也会清空。

- int slots;

- int capacity;

- int count;

- int dead;

- List<int> ids;

- List<BNode> nodes;

- NodeCache(int cap)

- int FindSlot(int pageId)

- BNode Get(int pageId)
  - 缓存的节点，没有则返回 null。

- void Put(int pageId, BNode n)

- void Remove(int pageId)
  - 忘记某个页面：它的 id 即将存放不同的内容
    （写时复制或 free-list 复用），过期的解码结果不能继续存在。

- void Clear()


## PageCache (class)

固定容量的 LRU 缓存，映射 page id -> ByteBuffer。开放寻址
哈希表，带侵入式双向链表维护 LRU 顺序。
运行中事务写入的页会被钉住（不驱逐），直到提交。

- int capacity;

- int slots;

- List<int> keys;

- List<ByteBuffer> vals;

- List<bool> pinned;

- List<int> lruPrev;

- List<int> lruNext;

- int lruHead;

- int lruTail;

- int count;

- PageCache(int cap)

- int Count()

- int FindSlot(int pageId)

- int FindInsertSlot(int pageId)

- void LruUnlink(int slot)

- void LruPushFront(int slot)

- bool Has(int pageId)

- ByteBuffer Get(int pageId)

- void SetPinned(int pageId, bool p)

- void UnpinAll()
  - 解除全部钉住（事务结束时调用）。

- void Remove(int pageId)
  - 丢弃一页（其 id 即将用于不同
    内容，旧的副本不能残留）。

- void Clear()
  - 丢弃所有页（另一进程已提交，缓存的页
    内容可能属于已不存在的版本）。

- int EvictableSlot()

- void Grow()

- void Put(int pageId, ByteBuffer buf, bool pin)
  - 插入一页并接管 buf 的所有权；满了时驱逐 LRU
    中未钉住的页。


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

- [DllImport("crt", EntryPoint="_locking")]static extern int LockFd(int fd, int mode, int length);

- [DllImport("crt", EntryPoint="fileno")]static extern int FileNo(nint fp);

- [DllImport("crt", EntryPoint="fsync")]static extern int SyncFd(int fd);

- [DllImport("crt", EntryPoint="lseek")]static extern long SeekFd(int fd, long offset, int origin);

- [DllImport("crt", EntryPoint="pread")]static extern int PReadFd(int fd, nint buf, int count, long offset);

- [DllImport("crt", EntryPoint="pwrite")]static extern int PWriteFd(int fd, nint buf, int count, long offset);

- [DllImport("crt", EntryPoint="flock")]static extern int FlockFd(int fd, int op);

- PageFile()

- static PageFile Open(string path)
  - 为随机读写打开（或创建）一个文件。

- bool IsOpen()

- string GetError()

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

- bool LockExclusive()
  - 获取整个文件的独占锁，等待当前
    持有者释放。若进程
    退出，锁由操作系统释放，因此崩溃的写入者不会卡死数据库。

- bool Unlock()

- void Close()


## PageIdSet (class)

小型开放寻址页 id 集合：写入者触及的每个节点都要
回答“该页是否由当前运行中的事务分配？”这个问题，
因此不能是线性扫描。

- int slots;

- int count;

- List<int> keys;

- PageIdSet()

- int Count()

- int SlotOf(int pageId)

- bool Has(int pageId)

- void Grow()

- void Add(int pageId)

- void Clear()


## Pager (class)

页管理器，单写者/多读者 MVCC（LMDB 模型）。

第 0 页和第 1 页是交替写入的 meta 页；每页包含
事务 id、B+Tree 根、空闲链表头、页数及
CRC。打开数据库时选择事务 id 最高且有效的 meta 页，
从而无需 redo log 即可原子提交：崩溃时
新 meta 页要么完整（新版本），要么不完整（旧版本，
因页面从不原地修改而保持原样）。

写事务从不覆盖在用页：`PageForWrite`
会将其复制到新分配的页，旧页交给
带提交事务 id 标记的空闲链表。页只有在
没有任何读者仍查看比释放它的事务更旧的快照时，
才能复用——这正是 `DbRegistry` 所跟踪的。

pager.BeginWrite();
int np = pager.PageForWrite(oldPage);   // copy-on-write
...mutate pager.GetPage(np)...
pager.SetRoot(newRoot);
pager.Commit();                          // fsync data, then meta

- static int PAGE_SIZE=4096;

- static int MAGIC=859980122;

- static int META_FIELDS=48;

- static int FREE_PER_PAGE=255;

- static int PageIdOf(long stored)

- PageFile file;

- PageCache cache;

- DbRegistry registry;

- string lastError;

- long txnid;

- int rootId;

- int freeHead;

- int pageCount;

- PageIdSet fresh;

- List<int> freshIds;

- List<int> freeIds;

- List<long> freeTxns;

- List<int> reusable;

- List<int> pendingFree;

- List<int> freeChain;

- int writeDepth;

- int readDepth;

- int pageWrites;

- int syncEvery;

- int commitsSinceSync;

- bool freeLoaded;

- bool cacheDirty;

- Pager()

- static Pager Open(string path, int cacheCapacity)
  - 打开数据库文件（不存在则创建）。

- bool IsOpen()

- string GetError()

- int PageCount()

- long CommitId()

- int RootId()

- void SetRoot(int id)

- int FreePages()
  - 空闲链表上的页数加上已可复用的页数
    （文件增长前即可复用的空间）。

- int PageWrites()
  - 自该句柄打开以来写入磁盘的页数（含 meta 页）：
    用它除以工作负载实际修改的页数，可得到
    写放大倍数。

- long OldestReader()
  - 仍被任意进程读取的最旧快照；比它更新的已释放页
    还不能再复用。0 = 无人读取。

- void SetSyncEvery(int n)
  - 提交时数据页的 fsync 频率：1 = 完全
    持久性（默认），N = 每 N 次提交 fsync 一次数据。meta 页
    总是最后写入，因此更低的频率是用“最近 N 次提交可能
    丢失”换取吞吐量——但绝不会损坏数据库。

- void WriteMeta(long newTxn)

- long ReadMetaSlot(int slot, ByteBuffer h)
  - 读取一个 meta 页；返回其事务 id，若页
    不存在、非本库或 CRC 损坏（撕裂的提交）则返回 0。

- void LoadMeta()
  - 将最新且有效的 meta 页采纳为该句柄的快照。

- bool MaybeStale()
  - 自该句柄上次读取 meta 页以来，另一进程
    是否可能已提交。事务 id 由共享的
    registry 分配，在整库内唯一：若已发布 id 等于
    我们的 id，说明我们的快照就是最新版本。存活进程数
    在此说明不了什么——已提交后退出的进程
    会把其版本留给本进程采纳。

- bool Refresh()
  - 当另一进程已提交时重新读取 meta 页，并
    丢弃旧版本的缓存页。若快照移动了
    则返回 true。

- bool TakeInvalidation()
  - 缓存中的已解码状态（B+Tree 节点）是否需要丢弃，
    因为快照已移动。并清除该标志。

- void LoadFreeList()

- void ReclaimFree()
  - 将无存活读者可达的空闲页移入
    可复用池。

- int PersistFreeList(long newTxn)
  - 将空闲链表写为全新的页链并返回其
    头（空时为 0）。在 Commit 内、新事务 id
    确定后调用。

- void BeginRead()
  - 注册该句柄读取的快照，防止另一进程的写入者
    回收其下的页。开销很小：
    仅一个共享内存存储；数据库在单进程内打开时，
    什么都不做。

- void EndRead()

- bool BeginWrite()
  - 占用唯一的写者槽位（阻塞）并在最新已提交快照上
    开始写事务。可嵌套。

- bool InWrite()

- bool IsFresh(int pageId)
  - 当页属于当前运行中的写事务时为 true
    （因此可原地修改）。

- int AllocPage()
  - 分配一页：有可复用的空闲页就复用，
    否则文件增长。该页在提交前保持钉住。

- int PageForWrite(int pageId)
  - 写时复制：返回一个可修改的页 id。
    已提交快照的页会复制到新页，旧页
    进入空闲链表队列；本事务创建的页
    原样返回。

- void FreeLater(int pageId)
  - 将页排队，待无读者可见时复用。
    当前事务创建的页立即回收——
    从没有人见过它们。

- ByteBuffer GetPage(int pageId)
  - 返回页缓冲区（带缓存；未命中时从文件读取）。
    缓冲区归 pager 所有，只有当
    页属于当前运行中的事务时才可写。

- void MarkDirty(int pageId)
  - 为兼容 API 而保留：写时复制下，每个被修改的
    页都是新分配的，因此无需标记。

- bool Commit()
  - 发布事务：先数据页（fsync），再
    meta 页（fsync）。meta 写入前崩溃会保留旧版本
    完好无损，因此无需 redo log 即可实现提交的原子性。

- void Rollback()
  - 丢弃事务写入的一切：未写入任何
    持久化内容，因此只需丢弃新页，meta 页仍
    指向旧版本。

- void Checkpoint()
  - 将迄今写入的所有内容强制刷入稳定存储。

- void Close()


## PendingWalPage (class)

- int id;

- ByteBuffer data;

- PendingWalPage(int id, ByteBuffer data)


## Wal (class)

预写日志（WAL）：按事务追加的顺序帧，带 CRC 保护，
以提交记录收尾并 fsync。对 SSD 友好（仅追加），是
提交的唯一持久化点：缺少
有效提交记录的事务页帧在恢复时被忽略，从而保证原子性——
要么全部提交，要么全不提交。

帧布局（小端）：
u32 type      1 = 页帧，2 = 提交记录
u64 pageId    (frames) / commitId (commit records)
u32 payloadLen
u32 crc32     载荷的 CRC32
payload       载荷，payloadLen 字节（页镜像；提交记录没有）

- static int FRAME_PAGE=1;

- static int FRAME_COMMIT=2;

- static int HEADER_LEN=20;

- PageFile file;

- string path;

- int pageSize;

- Wal()

- static Wal Open(string path, int pageSize)

- bool IsOpen()

- long Size()
  - 当前 WAL 文件大小（字节）。

- bool AppendFrame(int pageId, ByteBuffer page)
  - 追加一页镜像；此时尚未持久化（见 AppendCommit）。

- bool AppendCommit(long commitId)
  - 追加提交记录并 fsync：这是持久化点。

- int Recover(PageFile db)
  - 将已提交事务重放到主数据库文件。
    未提交的尾部帧（撕裂写入、事务中途崩溃）会被
    丢弃。幂等：重放已检查点的帧只是
    重写相同的页镜像。之后重置 WAL。

- void Reset()
  - 截断 WAL（主文件检查点成功后调用）。

- void Close()


## ZanDatabase (class)

嵌入式文档数据库，存储原生 Zan 值树（JsonValue：
整数、浮点数、布尔、字符串、嵌套对象、数组）——无 ORM 映射、
无 SQL 字符串。单文件 + WAL、崩溃安全的原子提交、LRU 页
缓存，且内存有硬上限。

ZanDatabase db = ZanDatabase.Open("app.zdb");
Collection users = db.GetCollection("users");
JsonValue u = JsonValue.NewObject();
u.Put("name", JsonValue.NewStr("alice"));
u.Put("age", JsonValue.NewNum("30"));
int id = users.Insert(u);
JsonValue back = users.FindById(id);
db.Close();

- KvStore kv;

- AsyncRwLock rw;

- List<string> colNames;

- List<Collection> cols;

- string lastError;

- ZanDatabase()

- static ZanDatabase Open(string path)
  - 以默认参数打开（不存在则创建）：256 页缓存、
    每次提交都 fsync。

- static ZanDatabase Open(string path, int cachePages)

- bool IsOpen()

- string GetError()

- void SetSyncEvery(int n)
  - fsync 频率：1 = 每次提交，N = 分组提交。

- Collection GetCollection(string name)
  - 返回指定名称的文档集合（惰性创建）。

- KvStore Kv()
  - 直接访问底层的键/值存储。

- void Begin()
  - 将后续写入批量合并为一次原子提交。

- bool Commit()

- void Checkpoint()

- void Close()


## JsonValue (delegate)

在写临界区内根据当前文档计算新文档
（id 不存在时为 null）。返回 null 表示保持
文档不变。

`delegate JsonValue DocUpdater(JsonValue current);`


## bool (delegate)

返回 true 时保留该文档。

`delegate bool DocPredicate(JsonValue doc);`


## int (delegate)

从文档中提取整数排序键。

`delegate int DocIntKey(JsonValue doc);`


## string (delegate)

从文档中提取字符串排序键。

`delegate string DocStrKey(JsonValue doc);`


## string (delegate)

在写临界区内根据当前值计算新值
（键不存在时 current 为 ""）。

`delegate string KvUpdater(string current);`
