# System.Data.ZanDb

> 源码: `packages/Zan.Data/src/System/Data/ZanDb/BlockCache.zan`, `packages/Zan.Data/src/System/Data/ZanDb/Collection.zan`, `packages/Zan.Data/src/System/Data/ZanDb/CollectionSearch.zan`, `packages/Zan.Data/src/System/Data/ZanDb/DocQuery.zan`, `packages/Zan.Data/src/System/Data/ZanDb/KvEntry.zan`, `packages/Zan.Data/src/System/Data/ZanDb/LogFile.zan`, `packages/Zan.Data/src/System/Data/ZanDb/Manifest.zan`, `packages/Zan.Data/src/System/Data/ZanDb/PageFile.zan`, `packages/Zan.Data/src/System/Data/ZanDb/Segment.zan`, `packages/Zan.Data/src/System/Data/ZanDb/Store.zan`, `packages/Zan.Data/src/System/Data/ZanDb/VectorIndex.zan`, `packages/Zan.Data/src/System/Data/ZanDb/ZanDatabase.zan`, `packages/Zan.Data/src/System/Data/ZanDb/ZanDbClient.zan`, `packages/Zan.Data/src/System/Data/ZanDb/ZanDbProtocol.zan`, `packages/Zan.Data/src/System/Data/ZanDb/ZanDbServer.zan`


## BlockCache (class)

- Dictionary <long, CachePin> map;

- List<long> order;

- long bytes;

- long cap;

- nint mtx;

- BlockCache(long capBytes)

- void SetCap(long capBytes)

- CachePin Acquire(long key)

- CachePin Insert(long key, ByteBuffer buf)

- void Release(CachePin pin)

- void Close()


## CachePin (class)

- ByteBuffer buf;

- int pins;

- int size;

- CachePin(ByteBuffer buf, int size)


## Collection (class)

- ZanStore kv;

- AsyncRwLock rw;

- string name;

- List<string> indexedFields;

- List<string> keyDict;

- bool keyDictDirty;

- long metaVersion;

- int nextIdCache;

- Dict <int, JsonValue> docCache;

- List<int> docCacheOrder;

- int docCacheCapacity;

- CollectionSearch search;

- static int INDEX_BATCH=500;

- [DllImport("crt", EntryPoint="memchr")]static extern nint MemChr(nint p, int c, long n);

- Collection()

- static Collection Attach(ZanStore kv, AsyncRwLock rw, string name)

- void SyncMeta()

- void CommitWrite()

- string Name()

- static int ID_DIGITS=6;

- static string PadId(int id)

- static int UnpadId(string s)

- static int UnpadIdPtr(nint p)

- string DocKey(int id)

- string SeqKey()

- string IdxMetaKey()

- string KeysMetaKey()

- string IdxPosKey(string field)

- string IdxPrefix(string field, string val)

- string IdxKey(string field, string val, int id)

- void LoadIndexMeta()

- void LoadKeyDict()

- int KeyId(string k)

- int KeyIdOf(string k)

- string KeyName(int id)

- void PersistKeyDict()

- static bool IsIntStr(string s)

- static long ParseI64(string s)

- void WriteVal(ByteBuffer b, JsonValue v)

- static bool HasLaterKey(JsonValue doc, string key, int from)

- void PrepareDoc(JsonValue doc)

- string EncodeDoc(JsonValue doc)

- static int IdOfKey(string key)

- static int IdOfPtr(nint baseKey, int keyLen)

- static string IdFromKey(string key)

- [DllImport("crt", EntryPoint="memchr")]static extern nint StrMemChr(string p, int c, long n);

- [DllImport("crt", EntryPoint="memcmp")]static extern int MemCmpRaw(nint a, string b, long n);

- static long RecVarInt(string s, int p, out int np)

- static string UnescapeRecord(string body)

- JsonValue ReadValS(string s, int p, out int np)

- static void SkipValS(string s, int p, out int np)

- JsonValue FieldOf(string body, int kid)

- bool FieldEquals(string body, int kid, string val)

- bool EqFastReject(string val)

- bool NeedleAbsent(ScanRow row, string val)

- bool FieldEqualsRow(ScanRow row, int kid, string val, bool fastReject)

- JsonValue FieldOfRow(ScanRow row, int kid)

- bool FieldNum(string body, int kid, out long v)

- string FieldText(string body, int kid)

- int ScanNumField(string field, NumVisit visit)

- int ScanStrField(string field, StrVisit visit)

- long SumField(string field)

- Dictionary <string, int> CountGroups(string field)

- JsonValue DecodeDoc(string body)

- void AddIndexEntries(int id, JsonValue doc)

- void RemoveIndexEntries(int id, JsonValue doc)

- int NextId()

- void SetCacheCapacity(int capacity)

- int GetCacheCapacity()

- int CachedDocCount()

- void ClearCache()

- JsonValue CacheGet(int id)

- void CachePut(int id, JsonValue doc)

- void CacheEvict(int id)

- int Insert(JsonValue doc)

- int InsertBulk(List<JsonValue> docs)

- JsonValue FindById(int id)

- bool Exists(int id)

- bool Update(int id, JsonValue doc)

- void Upsert(int id, JsonValue doc)

- bool DeleteById(int id)

- void EnsureFullTextIndex(string field)

- void EnsureFullTextIndex(string field, TextTokenizer tokenizer)

- void EnsureVectorIndex(string field, VectorIndexOptions options)

- List<Bm25Hit> SearchText(string field, string query, int k)

- List<Bm25Hit> SearchTextFiltered(string field, string query, int k, Bm25Filter filter)

- List<VectorHit> SearchVector(string field, float[]query, int k, int efSearch)

- List<VectorHit> SearchVectorFiltered(string field, float[]query, int k, int efSearch, VectorFilter filter)

- List<VectorHit> SearchVectorExact(string field, float[]query, int k)

- List<VectorHit> SearchVectorExactFiltered(string field, float[]query, int k, VectorFilter filter)

- List<HybridHit> SearchHybrid(string textField, string query, string vectorField, float[]vector, int k, int efSearch)

- List<HybridHit> SearchHybridFiltered(string textField, string query, string vectorField, float[]vector, int k, int efSearch, Bm25Filter textFilter, VectorFilter vectorFilter)

- void RebuildSearchIndexes()

- void CheckpointSearchIndexes()

- JsonValue ListSearchIndexes()

- void PersistIndexMeta()

- void EnsureIndex(string field)

- void EnsureIndex(string field, int batchSize)

- bool BuildIndexStep(string field, int batchSize)

- bool HasIndex(string field)

- bool IndexReady(string field)

- List<JsonValue> FindByIndex(string field, string val)

- List<JsonValue> FindByFieldScan(string field, string val)

- int CountByField(string field, string val)

- int Count()

- static bool RecAccNum(nint basePtr, int valOff, int valLen, int want, NumAcc acc)

- class NumAcc

- static int FieldEqualsPtr(nint basePtr, int valOff, int valLen, int kid, string val)

- static int PeekB(nint p)

- static long ReadVarIntRaw(nint basePtr, ref int p, int limit)

- static bool SkipValRaw(nint basePtr, ref int p, int end)

- List<JsonValue> FindAll()

- List<JsonValue> FindRange(int fromId, int toId, int limit)

- int ScanField(string field, FieldVisit visit)

- List<int> FindIdsByField(string field, string val)

- List<int> FindIdsByFieldRange(string field, string lo, string hi)

- bool NumAggregate(string field, NumAcc acc, out int fromId)

- bool MinField(string field, out long v)

- bool MaxField(string field, out long v)

- double AvgField(string field)

- List<int> FindTopIds(string field, int n)

- static bool ZanCollectionBetter(long va, int ia, long vb, int ib)

- static void HeapPush(List<long> hv, List<int> hi, long v, int id)

- static void HeapSiftDown(List<long> hv, List<int> hi)

- DocQuery Query()

- async JsonValue FindByIdAsync(int id)

- async int InsertAsync(JsonValue doc)

- async bool UpdateAsync(int id, JsonValue doc)

- async bool DeleteByIdAsync(int id)

- async bool UpdateAtomic(int id, DocUpdater fn)


## CollectionEntry (class)

- public string name;

- public Collection collection;

- CollectionEntry(string name, Collection collection)


## CollectionSearch (class)

- static int BATCH_ROWS=256;

- static long BATCH_BYTES=4194304;

- ZanStore kv;

- string prefix;

- Dictionary <string, CollectionSearchState> states;

- string catalogBody;

- List<JsonValue> catalog;

- CollectionSearch(ZanStore kv, string name)

- static JsonValue Member(JsonValue value, string key)

- static long Number(JsonValue value, string key)

- static string RevisionKey(long revision)

- static void CheckField(string field)

- string IndexPrefix(string kind, string field)

- string DefinitionKey(string kind, string field)

- string DataPrefix(CollectionSearchState state)

- static void CheckDefinition(JsonValue def)

- List<JsonValue> Definitions()

- JsonValue ListIndexes()

- void Finish()

- void WriteRows(List<KvEntry> rows)

- void Cleanup(CollectionSearchState state, string snapshotGeneration, long throughRevision)

- void DiscardGeneration(CollectionSearchState state, string generation, bool inputs)

- CollectionSearchState Create(string field, string kind, string definition, string generation, JsonValue config)

- void EnsureText(Collection collection, string field, TextTokenizer tokenizer)

- void EnsureVector(Collection collection, string field, VectorIndexOptions options)

- void Ensure(Collection collection, string field, string kind, JsonValue config)

- void BuildInputs(Collection collection, CollectionSearchState state)

- static float[]ReadVector(JsonValue value, VectorIndexOptions options)

- void Validate(JsonValue doc)

- void Stage(int id, JsonValue doc)

- void Apply(CollectionSearchState state, int id, JsonValue value)

- CollectionSearchState Load(string kind, string field)

- void RequireCommitted()

- List<Bm25Hit> Text(string field, string query, int k, Bm25Filter filter)

- List<VectorHit> Vector(string field, float[]query, int k, int efSearch, bool exact, VectorFilter filter)

- List<HybridHit> Hybrid(string textField, string query, string vectorField, float[]vector, int k, int efSearch, Bm25Filter textFilter, VectorFilter vectorFilter)

- static bool Better(HybridHit a, HybridHit b)

- static void SortHits(List<HybridHit> hits, int lo, int hi)

- JsonValue StageCheckpoint(CollectionSearchState state, string generation)

- void Checkpoint()

- void Rebuild(Collection collection)


## CollectionSearchState (class)

- string field;

- string kind;

- string definition;

- string generation;

- string checkpoint;

- Bm25Index text;

- VectorIndex vector;

- VectorIndexOptions options;

- long applied;

- CollectionSearchState(string field, string kind, string definition, string generation)


## DocQuery (class)

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

- static DocQuery Of(Collection col)

- DocQuery Where(DocPredicate pred)

- DocQuery WhereEq(string field, string val)

- DocQuery WhereEqInt(string field, int val)

- DocQuery WhereRange(string field, string lo, string hi)

- DocQuery WhereIn(string field, List<string> values)

- DocQuery WhereInInt(string field, List<int> values)

- DocQuery OrderByInt(DocIntKey key)

- DocQuery OrderByIntDesc(DocIntKey key)

- DocQuery OrderByStr(DocStrKey key)

- DocQuery OrderByStrDesc(DocStrKey key)

- DocQuery Skip(int n)

- DocQuery Take(int n)

- static List<int> IntersectSortedIds(List<int> a, List<int> b)

- static List<int> UnionSortedIds(List<int> a, List<int> b)

- List<int> FindIdsByInValues(string field, List<string> vals)

- List<JsonValue> Candidates()

- bool MatchesRest(JsonValue d)

- bool EqAppliedByScan()

- bool Matches(JsonValue d)

- void SortDocs(List<JsonValue> docs)

- List<JsonValue> SortTopDocsInt(List<JsonValue> docs, int k)

- List<JsonValue> SortTopDocsStr(List<JsonValue> docs, int k)

- List<JsonValue> ToList()

- JsonValue First()

- int Count()


## EqFilter (class)

- public string field;

- public string val;

- public EqFilter(string field, string val)


## HybridHit (class)

- int id;

- double score;

- HybridHit(int id, double score)


## InFilter (class)

- public string field;

- public List<string> values;

- public Dict <string, bool> valSet;

- public InFilter(string field, List<string> values)


## KvEntry (class)

- string key;

- string val;

- KvEntry(string key, string val)


## LogFile (class)

- static int HEADER_LEN=8;

- static long MAX_FRAME=67108864;

- PageFile file;

- string path;

- ByteBuffer pending;

- long logicalEnd;

- string lastError;

- LogFile()

- static LogFile Open(string filePath)

- bool IsOpen()

- string GetError()

- string FilePath()

- long Size()

- int PendingBytes()

- bool Ready()

- bool AppendFrame(ByteBuffer payload)

- bool Flush()

- bool Sync()

- int Mark()

- void RollbackTo(int mark)

- long Replay(LogFrameVisitor visit)

- bool Reset()

- bool TryLockExclusive()

- void Close()

- void Close(bool flushPending)


## Manifest (class)

- static string MAGIC="ZMAN";

- static int VERSION=1;

- static int FIXED=36;

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

- bool ParseSlot(string path, out long gen, out long seq, out long nextId, List<long> ids)

- static bool ValidState(long seq, long nextId, List<long> ids)

- bool Exists()

- long LogStartSeq()

- long NextSegId()

- List<long> SegIds()

- string GetError()

- bool Save(long newLogStartSeq, long newNextSegId, List<long> ids)


## PageFile (class)

- static int LOCK_TIMEOUT_MS=60000;

- nint fp;

- int fd;

- string path;

- string lastError;

- [DllImport("crt", EntryPoint="zan_file_fopen")]static extern nint fopen(string path, string mode);

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

- static PageFile Open(string path)

- static PageFile OpenExisting(string path)

- static PageFile OpenCore(string path, bool create)

- bool IsOpen()

- string GetError()

- int ReadAt(long offset, ByteBuffer buf, int count)

- int ReadRaw(long offset, nint raw, int count)

- int WriteAt(long offset, nint raw, int count)

- long Append(nint raw, int count)

- long Size()

- void Flush()

- bool Sync()

- bool Truncate(long size)

- bool LockExclusive()

- bool TryLockExclusive()

- bool Unlock()

- void Close()


## RangeFilter (class)

- public string field;

- public string lo;

- public string hi;

- public RangeFilter(string field, string lo, string hi)


## ScanRow (class)

- SegCursor cur;

- string memKey;

- string memVal;

- void Bind(SegCursor c)

- void BindMem(string k, string v)

- string Key()

- string Val()

- ByteBuffer Block()

- int ValOff()

- int ValLen()

- nint ValPtr()


## SegCursor (class)

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

- string Val()

- int Op()

- bool Valid()

- int KeyOff()

- int KeyLen()

- int ValOff()

- int ValLen()

- ByteBuffer Block()

- int CmpKeyRaw(string other)

- static SegCursor Open(SegmentFile seg, string fromKey)

- bool LoadCur()

- void Next()

- void Free()


## SegRef (class)

- SegmentFile seg;

- long id;

- SegRef(SegmentFile seg, long id)


## SegmentFile (class)

- static string MAGIC="ZSEG";

- PageFile file;

- string path;

- List<SegmentIndexEntry> entries;

- long indexStart;

- long recordCount;

- string lastError;

- SegmentFile()

- static SegmentFile Open(string segPath)

- string Path()

- long Count()

- string GetError()

- int BlockFor(string key)

- int BlockCount()

- ByteBuffer LoadBlockSpan(int fromBlock, int toBlock, ByteBuffer reuse)

- long BlockFileOff(int i)

- int BlockLen(int i)

- ByteBuffer LoadBlock(int i)

- ByteBuffer LoadBlockReuse(int i, ByteBuffer reuse)

- static bool NextRecord(ByteBuffer b, out string key, out string val, out int op)

- static bool NextRecordRaw(ByteBuffer b, out int keyOff, out int keyLen, out int valOff, out int valLen, out int op)

- static int ScanBlock(ByteBuffer b, string key, out string val)

- int Lookup(string key, out string val)

- int Lookup(string key, out string val, BlockCache cache, long segNo)

- int Scan(string fromKey, SegVisit visit)

- void Close()


## SegmentIndexEntry (class)

- public long offset;

- public string key;

- public long crc;

- public int verified;

- SegmentIndexEntry(long offset, string key, long crc)


## SegmentSparseWriteEntry (class)

- public long offset;

- public string key;

- SegmentSparseWriteEntry(long offset, string key)


## SegmentWriter (class)

- static string MAGIC="ZSEG";

- static int VERSION=1;

- static int SPARSE_EVERY=16;

- static int WRITE_BUF=262144;

- PageFile file;

- ByteBuffer buf;

- long fileLen;

- long recCount;

- List<SegmentSparseWriteEntry> sparseEntries;

- bool failed;

- string lastError;

- SegmentWriter()

- static SegmentWriter Create(string path)

- bool Failed()

- string GetError()

- void Add(string key, string val, int op)

- void FlushBuf()

- bool Finish()


## VectorChunk (class)

- float[]values;

- VectorChunk(int size)


## VectorHeap (class)

- int[]slots;

- int[]ids;

- double[]distances;

- int count;

- bool maximum;

- VectorHeap(int capacity, bool maximum)

- static bool Before(double ad, int ai, int av, double bd, int bi, int bv)

- bool Higher(double ad, int ai, int av, double bd, int bi, int bv)

- void Grow()

- void Push(int slot, int id, double distance)

- void Pop()

- int NearestSlot()


## VectorHit (class)

- int id;

- double distance;

- VectorHit()

- VectorHit(int id, double distance)


## VectorIndex (class)

- VectorIndexOptions options;

- List<VectorChunk> chunks;

- List<VectorNode> nodes;

- Dict <int, int> idSlots;

- int chunkRows;

- int entry;

- int maxLevel;

- long randomState;

- AtomicInt lastDistanceCount;

- VectorIndex(VectorIndexOptions options)

- VectorIndexOptions Options()

- int Count()

- long LastDistanceCount()

- int NextLevel()

- void ValidateDimensions(float[]vector)

- double ValidateCoordinates(float[]vector, int offset)

- double ValidateVector(float[]vector)

- void CopyIntoSlot(int slot, float[]vector)

- float[]CopyVector(int slot)

- double DistanceTo(VectorWorkspace work, int slot)

- double DistanceBetween(int a, int b)

- bool Current(int slot)

- bool Admit(int slot, bool activeOnly, VectorFilter filter)

- int Greedy(VectorWorkspace work, int start, int layer)

- VectorHeap SearchLayer(VectorWorkspace work, int start, int layer, int ef, bool activeOnly, VectorFilter filter)

- VectorNeighbors SelectNeighbors(int center, VectorHeap candidates, int limit)

- void Link(int from, int to, int layer)

- void Add(int id, float[]vector)

- bool Remove(int id)

- VectorWorkspace QueryWorkspace(float[]query)

- int QueryLimit(int k)

- List<VectorHit> Hits(VectorHeap nearest, int k)

- List<VectorHit> Search(float[]query, int k, int efSearch)

- List<VectorHit> SearchFiltered(float[]query, int k, int efSearch, VectorFilter filter)

- List<VectorHit> SearchExact(float[]query, int k)

- List<VectorHit> SearchExactFiltered(float[]query, int k, VectorFilter filter)

- void Rebuild()

- JsonValue ExportState()

- static VectorIndex ImportState(JsonValue state)


## VectorIndexOptions (class)

- int dimensions;

- int metric;

- int m;

- int efConstruction;

- int efSearch;

- int seed;

- VectorIndexOptions()

- VectorIndexOptions(int dimensions)

- VectorIndexOptions(int dimensions, int metric)

- VectorIndexOptions Clone()

- void Validate()

- JsonValue ToJson()

- static VectorIndexOptions FromJson(JsonValue obj)


## VectorNeighbors (class)

- int[]slots;

- int count;

- VectorNeighbors(int capacity)

- bool Contains(int slot)


## VectorNode (class)

- int id;

- int active;

- int level;

- double inverseNorm;

- List<VectorNeighbors> layers;

- VectorNode(int id, int level, int m, double inverseNorm)


## VectorStateJson (class)

- static void RequireObject(JsonValue obj)

- static JsonValue Field(JsonValue obj, string key)

- static int IntValue(JsonValue value)

- static int IntField(JsonValue obj, string key)

- static JsonValue ArrayField(JsonValue obj, string key)


## VectorWorkspace (class)

- float[]query;

- int offset;

- double inverseNorm;

- int[]visited;

- int epoch;

- long distanceCount;

- VectorWorkspace(float[]query, int offset, double inverseNorm, int count)


## ZanDatabase (class)

- ZanStore kv;

- AsyncRwLock rw;

- List<CollectionEntry> entries;

- string lastError;

- ZanDatabase()

- static ZanDatabase Open(string path)

- static ZanDatabase Open(string path, int durability)

- bool IsOpen()

- string GetError()

- void SetSyncEvery(int n)

- Collection GetCollection(string name)

- ZanStore Kv()

- void Begin()

- bool Commit()

- void Rollback()

- bool InTransaction()

- long GetRevision()

- long CacheVersion()

- void Checkpoint()

- void Flush()

- void MergeSegments()

- int SegCount()

- int MergeSomeSegments(int maxMerge)

- void SetAutoMerge(int atSegs, int mergeK)

- void Close()


## ZanDbClient (class)

- private string host;

- private int port;

- private string token;

- private string clientId;

- private int timeoutMs;

- private object sync;

- private List<TcpClient> active;

- private long readId;

- private int pending;

- private bool closed;

- ZanDbClient(string host, int port, string token, string clientId)

- void SetTimeout(int timeoutMs)

- private JsonValue Prepare(JsonValue request)

- private int Timeout()

- private void Track(TcpClient client)

- private void Finish(TcpClient client)

- private static int Remaining(long deadline, CancellationToken cancellation)

- async JsonValue SendAsync(JsonValue request)

- async JsonValue SendAsync(JsonValue request, CancellationToken cancellation)

- async JsonValue SendWriteAsync(JsonValue request, string requestId)

- async JsonValue RetryWriteAsync(JsonValue request, string requestId, int attempts, CancellationToken cancellation)

- private static JsonValue Operation(string action, string collection)

- private static JsonValue VectorJson(float[]vector)

- async JsonValue PingAsync()

- async JsonValue ListIndexesAsync(string collection)

- async JsonValue EnsureIndexAsync(string collection, string field, string requestId)

- async JsonValue EnsureFullTextIndexAsync(string collection, string field, string requestId)

- async JsonValue EnsureVectorIndexAsync(string collection, string field, VectorIndexOptions options, string requestId)

- async JsonValue CheckpointSearchIndexesAsync(string collection, string requestId)

- async JsonValue RebuildSearchIndexesAsync(string collection, string requestId)

- async JsonValue InsertAsync(string collection, JsonValue document, string requestId)

- async JsonValue UpsertAsync(string collection, int id, JsonValue document, string requestId)

- async JsonValue UpdateAsync(string collection, int id, JsonValue document, string requestId)

- async JsonValue DeleteAsync(string collection, int id, string requestId)

- async JsonValue GetAsync(string collection, int id)

- async JsonValue QueryAsync(string collection, JsonValue filter, int limit, int skip)

- async JsonValue SearchTextAsync(string collection, string field, string query, int k)

- async JsonValue SearchTextAsync(string collection, string field, string query, int k, JsonValue filter)

- private static JsonValue VectorOperation(string action, string collection, string field, float[]vector, int k, int ef)

- async JsonValue SearchVectorAsync(string collection, string field, float[]vector, int k, int ef)

- async JsonValue SearchVectorAsync(string collection, string field, float[]vector, int k, int ef, JsonValue filter)

- async JsonValue SearchVectorExactAsync(string collection, string field, float[]vector, int k)

- async JsonValue SearchVectorExactAsync(string collection, string field, float[]vector, int k, JsonValue filter)

- async JsonValue SearchHybridAsync(string collection, string textField, string query, string vectorField, float[]vector, int k, int ef)

- async JsonValue SearchHybridAsync(string collection, string textField, string query, string vectorField, float[]vector, int k, int ef, JsonValue filter)

- async JsonValue BatchAsync(JsonValue operations, string requestId)

- void Close()

- private int Pending()

- async void CloseAsync()

- void Dispose()


## ZanDbIoLease (class)

- ZanDbIoState state;

- Timer timer;

- ZanDbIoLease(TcpClient client, int timeoutMs, CancellationToken cancellation)

- async void FinishAsync()

- void ThrowIfAborted()


## ZanDbIoState (class)

- object sync;

- nint socket;

- long deadline;

- CancellationToken cancellation;

- bool active;

- bool expired;

- bool canceled;

- ZanDbIoState(TcpClient client, int timeoutMs, CancellationToken cancellation)

- void Check()

- void Finish()

- void ThrowIfAborted()


## ZanDbProtocol (class)

- static int VERSION=1;

- static int MAX_PAYLOAD=8388608;

- static int DEFAULT_TIMEOUT_MS=30000;

- static int MAX_BATCH=1000;

- static int MAX_RESULTS=1000;

- static int MAX_VECTOR_DIMENSIONS=65536;

- static int MAX_ID=128;

- static int MAX_CLIENT=64;

- static JsonValue Member(JsonValue obj, string key)

- static string RequireString(JsonValue obj, string key)

- static int IntOption(JsonValue obj, string key, int fallback, int lo, int hi)

- static int DocumentId(JsonValue obj)

- static bool SafeId(string value, int maxBytes)

- static string Name(JsonValue obj, string key)

- static bool IsWrite(string action)

- static bool IsMaintenance(string action)

- static bool IsAction(string action)

- static JsonValue Canonical(JsonValue value, int depth)

- static string Hash(string text)

- static string Fingerprint(JsonValue request)

- static JsonValue Response(string id, long revision, JsonValue result)

- static JsonValue Error(string id, long revision, string code, string message)

- static byte[]Header(int length)

- static int Length(byte[]header)

- static async bool ReadExact(TcpClient client, byte[]destination, int length, bool allowEof)

- static async JsonValue ReadAsync(TcpClient client, int timeoutMs, CancellationToken cancellation)

- static async void WriteExact(TcpClient client, byte[]bytes, int length)

- static async void WriteAsync(TcpClient client, JsonValue value, int timeoutMs, CancellationToken cancellation)


## ZanDbRequestException (class)

- public string code;

- ZanDbRequestException(string code, string message)


## ZanDbServer (class)

- private ZanDatabase db;

- private object ownerGate;

- private object lifecycleGate;

- private TcpListener listener;

- private List<ZanDbServiceConnection> connections;

- private List<long> tasks;

- private string token;

- private string tokenScope;

- private string host;

- private int port;

- private int timeoutMs;

- private int maxConnections;

- private bool started;

- private bool stopping;

- private bool accepting;

- private bool closed;

- ZanDbServer(ZanDbServerOptions options)

- void Start()

- private bool BeginAccepting()

- private bool IsStopping()

- private void Register(TcpClient client)

- async void RunAsync()

- private bool BeginRequest(ZanDbServiceConnection connection)

- private void EndRequest(ZanDbServiceConnection connection)

- private void Remove(ZanDbServiceConnection connection)

- private long CurrentRevision()

- private async void ConnectionLoop(ZanDbServiceConnection connection)

- void RequestStop()

- private List<long> DrainSnapshot()

- private bool IsAccepting()

- async void StopAsync()

- async void Stop()

- private bool Authenticated(string candidate)

- private static void ValidateFilter(JsonValue operation)

- private static bool Matches(Collection collection, int id, JsonValue filter)

- private void Validate(JsonValue operation, bool batchStep)

- private static float[]Vector(JsonValue value)

- private static VectorIndexOptions VectorOptions(JsonValue value)

- private JsonValue Dispatch(JsonValue operation)

- private JsonValue ExecuteOwned(JsonValue original)

- JsonValue Execute(JsonValue request)


## ZanDbServerOptions (class)

- public string path;

- public string host;

- public int port;

- public string token;

- public int timeoutMs;

- public int maxConnections;

- ZanDbServerOptions()


## ZanDbServiceConnection (class)

- public TcpClient client;

- public long task;

- public bool busy;

- ZanDbServiceConnection(TcpClient client)


## ZanStore (class)

- static int DUR_SYNC=0;

- static int DUR_GROUP=1;

- static int DUR_NONE=2;

- static int GROUP_FLUSH_BYTES=524288;

- static int NONE_FLUSH_BYTES=1048576;

- static long DEFAULT_FLUSH_THRESHOLD=67108864;

- static long DEFAULT_CACHE_CAP=33554432;

- static int KERNEL_SPAN_BYTES=262144;

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

- long bytesSinceSync;

- long seq;

- long cacheEpoch;

- long lockHandle;

- bool closed;

- long logStartSeq;

- long nextSegId;

- long flushThreshold;

- long recoverSeg;

- int txDepth;

- static int BATCH_VERSION=1;

- static int BATCH_HEADER_LEN=17;

- Dictionary <string, string> txPut;

- Dictionary <string, string> txDel;

- Dictionary <string, long> tombstones;

- int autoMergeAt;

- int autoMergeK;

- string lastError;

- ZanStore()

- static string SegPath(string basePath, long id)

- static ZanStore Open(string path)

- static ZanStore Open(string path, int durability)

- bool RecoverRecord(string key, string val, int op)

- static bool SkipWalString(ByteBuffer b)

- static bool ValidFrame(ByteBuffer b)

- bool ApplyFrame(ByteBuffer b)

- void ApplyRecord(int op, string key, string val)

- bool IsOpen()

- string GetError()

- long Version()

- long GetRevision()

- bool InTransaction()

- long CacheVersion()

- int Count()

- int SegCount()

- long LogBytes()

- void SetDurability(int d)

- void SetSyncEvery(int n)

- void SetFlushThreshold(long bytes)

- void SetCacheCap(long bytes)

- void SetAutoMerge(int atSegs, int mergeK)

- string Get(string key)

- bool Contains(string key)

- void Put(string key, string val)

- bool Delete(string key)

- void Begin()

- bool Commit()

- static int VarIntBytes(long n)

- ByteBuffer BuildTxFrame()

- void Rollback()

- void MergeTxOverlay()

- bool PersistCommit(int bytes)

- void FlushToSegment()

- bool PublishBoth(long logSeq, long nextId, List<long> ids)

- static void CloseWriter(SegmentWriter wtr)

- void MergeSegments()

- async void MergeAsync()

- static bool IdInList(List<long> ids, long id)

- int MergeSomeSegments(int maxMerge)

- bool WriteReady()

- int Scan(string fromKey, StoreVisit visit)

- int ScanBetween(string startKey, string endKey, StoreVisit visit)

- int ScanRows(string startKey, string endKey, ScanRowVisit visit)

- int CountRange(string startKey, string endKey)

- int TryScanKernel(string startKey, string endKey, KernelVisit visit)

- int ScanKernel(SegmentFile seg, string startKey, string endKey, KernelVisit visit)

- async int ScanAsync(string fromKey, StoreVisit visit)

- int ScanRange(string startKey, string endKey, int limit, List<KvEntry> output)

- int ScanPrefix(string prefix, int limit, List<KvEntry> output)

- int TryScanKeyKernel(string startKey, string endKey, KernelKeyVisit visit)

- int ScanKeyKernel(SegmentFile seg, string startKey, string endKey, KernelKeyVisit visit)

- void Checkpoint()

- List<long> SegIdList()

- SegRef FindSeg(long id)

- void Fail(string error)

- void CloseResources()

- void Close()

- static void SortStrings(List<string> a)

- static void SortRange(List<string> a, int lo, int hi)

- static void Swap(List<string> a, int i, int j)

- async string GetAsync(string key)

- async void PutAsync(string key, string val)

- async bool DeleteAsync(string key)

- async void FlushAsync()

- async string UpdateAtomic(string key, StoreUpdater fn)


## JsonValue (delegate)

`delegate JsonValue DocUpdater(JsonValue current);`


## bool (delegate)

`delegate bool FieldVisit(int id, JsonValue v);`


## bool (delegate)

`delegate bool NumVisit(int id, long v);`


## bool (delegate)

`delegate bool StrVisit(int id, string v);`


## bool (delegate)

`delegate bool DocPredicate(JsonValue doc);`


## bool (delegate)

`delegate bool LogFrameVisitor(ByteBuffer payload);`


## bool (delegate)

`delegate bool SegVisit(string key, string val, int op);`


## bool (delegate)

`delegate bool StoreVisit(string key, string val);`


## bool (delegate)

`delegate bool ScanRowVisit(ScanRow row);`


## bool (delegate)

`delegate bool KernelVisit(nint basePtr, int keyOff, int keyLen, int valOff, int valLen);`


## bool (delegate)

`delegate bool KernelKeyVisit(nint basePtr, int keyOff, int keyLen);`


## bool (delegate)

`delegate bool VectorFilter(int id);`


## int (delegate)

`delegate int DocIntKey(JsonValue doc);`


## string (delegate)

`delegate string DocStrKey(JsonValue doc);`


## string (delegate)

`delegate string StoreUpdater(string current);`
