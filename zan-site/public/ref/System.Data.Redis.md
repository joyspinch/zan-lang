# System.Data.Redis

> 源码: `packages/Zan.Data/src/System/Data/Redis/RedisClient.zan`, `packages/Zan.Data/src/System/Data/Redis/RedisPool.zan`, `packages/Zan.Data/src/System/Data/Redis/RedisReply.zan`, `packages/Zan.Data/src/System/Data/Redis/RedisTypes.zan`


## NoticeEventArgs (class)

- public string Log;

- public long ElapsedMs;

- public Exception Exception;

- public NoticeEventArgs(string log, long elapsedMs, Exception ex)


## RedisClient (class)

- nint sock;

- string host;

- int port;

- bool connected;

- string lastError;

- bool busy;

- List<AsyncGate> waiters;

- byte[]inbuf;

- int incap;

- int inhead;

- int intail;

- byte[]tmp;

- int tmpcap;

- TlsStream tls;

- RedisPool pool;

- public RedisSerializer Serialize;

- public RedisDeserializer Deserialize;

- public NoticeEventHandler Notice;

- static bool verifyTls=true;

- static void SetTlsVerify(bool verify)

- [DllImport("crt")]static extern long strlen(string str);

- public RedisClient(string connectionString)

- public RedisClient(RedisOptions options)

- public RedisClient(RedisPool pool)

- void initMembers()

- void FireNotice(string logText, long elapsedMs, Exception ex)

- string formatCmdLog(List<string> args)

- public string SerializeObject(object obj)

- public T DeserializeObject<T>(string val)

- void initPool(RedisOptions opt)

- RedisClient()

- static async RedisClient ConnectAsync(string host, int port)

- static async RedisClient ConnectAsync(string host, int port, int timeoutMs)

- static async RedisClient ConnectAsync(string connectionString)

- static async RedisClient ConnectAsync(RedisOptions opt)

- static async RedisClient ConnectSecureAsync(string host, int port, int timeoutMs)

- static async RedisClient doConnect(string host, int port, int timeoutMs, bool secure)

- async bool AcquireLock()

- void ReleaseLock()

- void Close()

- string sliceBytes(int start, int len)

- void ensureRoom(int extra)

- async int fillMore()

- async bool ensureAvailable(int need)

- async string readLine()

- async string readBulk(int n)

- async RedisReply readReply()

- async int sendArgs(List<string> args)

- async RedisReply CommandAsync(List<string> args)

- async RedisReply CommandInternalAsync(List<string> args)

- async RedisReply CommandBytesAsync(List<string> prefixArgs, byte[]binaryArg)

- async RedisReply CommandBytesInternalAsync(List<string> prefixArgs, byte[]binaryArg)

- async RedisReply CommandAsync1(string a0)

- async RedisReply CommandAsync2(string a0, string a1)

- async RedisReply CommandAsync3(string a0, string a1, string a2)

- async bool PingAsync()

- async bool AuthAsync(string password)

- async bool AuthAsync(string username, string password)

- async bool SelectAsync(int index)

- async string EchoAsync(string message)

- async string GetAsync(string key)

- async T GetObjectAsync<T>(string key)

- async T GetAsync<T>(string key)

- async byte[]GetBytesAsync(string key)

- async RedisReply GetReplyAsync(string key)

- async string GetSetAsync(string key, string val)

- async bool SetAsync(string key, string val)

- async bool SetAsync(string key, string val, int expireSeconds)

- async bool SetAsync(string key, string val, int expireSeconds, bool nx, bool xx)

- async bool SetExAsync(string key, int seconds, string val)

- async bool SetNxAsync(string key, string val)

- async bool SetNxAsync(string key, string val, int expireSeconds)

- async bool SetXxAsync(string key, string val)

- async bool SetAsync<T>(string key, T val)

- async bool SetAsync<T>(string key, T val, int expireSeconds)

- async bool SetAsync<T>(string key, T val, int expireSeconds, bool nx, bool xx)

- async bool SetExAsync<T>(string key, int seconds, T val)

- async bool SetNxAsync<T>(string key, T val)

- async bool SetNxAsync<T>(string key, T val, int expireSeconds)

- async bool SetBytesAsync(string key, byte[]val)

- async List<string> MGetAsync(params string[]keys)

- async List<string> MGetAsync(List<string> keys)

- async bool MSetAsync(List<RedisEntry> entries)

- async bool MSetAsync(params string[]keyValues)

- async bool MSetAsync(Dictionary <string, string> dict)

- async bool MSetAsync(List<string> keys, List<string> vals)

- async bool MSetEntriesAsync(List<RedisEntry> entries)

- async bool MSetNxAsync(List<RedisEntry> entries)

- async int IncrAsync(string key)

- async long Incr64Async(string key)

- async long IncrByAsync(string key, long amount)

- async double IncrByFloatAsync(string key, double amount)

- async int DecrAsync(string key)

- async long Decr64Async(string key)

- async long DecrByAsync(string key, long amount)

- async long StrLenAsync(string key)

- async long AppendAsync(string key, string value)

- async string GetRangeAsync(string key, long start, long end)

- async long SetRangeAsync(string key, long offset, string value)

- async int DelAsync(string key)

- async int DelMultipleAsync(List<string> keys)

- async int ExistsAsync(string key)

- async int ExistsMultipleAsync(List<string> keys)

- async string TypeAsync(string key)

- async int ExpireAsync(string key, int seconds)

- async int ExpireAtAsync(string key, long timestamp)

- async int PExpireAsync(string key, long ms)

- async long TtlAsync(string key)

- async long PTtlAsync(string key)

- async int PersistAsync(string key)

- async List<string> KeysAsync(string pattern)

- async bool RenameAsync(string key, string newKey)

- async bool RenameNxAsync(string key, string newKey)

- async string RandomKeyAsync()

- async byte[]DumpAsync(string key)

- async RedisScanResult ScanAsync(long cursor, string pattern, int count)

- async long HSetAsync(string key, string field, string value)

- async long HSetAsync<T>(string key, string field, T value)

- async bool HSetNxAsync(string key, string field, string value)

- async bool HSetNxAsync<T>(string key, string field, T value)

- async string HGetAsync(string key, string field)

- async T HGetObjectAsync<T>(string key, string field)

- async T HGetAsync<T>(string key, string field)

- async byte[]HGetBytesAsync(string key, string field)

- async bool HMSetAsync(string key, Dictionary <string, string> dict)

- async bool HMSetAsync(string key, List<RedisEntry> entries)

- async bool HMSetAsync(string key, List<string> fields, List<string> values)

- async List<string> HMGetAsync(string key, params string[]fields)

- async List<string> HMGetAsync(string key, List<string> fields)

- async long HIncrByAsync(string key, string field, long amount)

- async double HIncrByFloatAsync(string key, string field, double amount)

- async bool HExistsAsync(string key, string field)

- async int HDelAsync(string key, string field)

- async int HDelMultipleAsync(string key, List<string> fields)

- async long HLenAsync(string key)

- async List<string> HKeysAsync(string key)

- async List<string> HValsAsync(string key)

- async Dictionary <string, string> HGetAllAsync(string key)

- async long HStrLenAsync(string key, string field)

- async RedisScanResult HScanAsync(string key, long cursor, string pattern, int count)

- async long LPushAsync(string key, string value)

- async long LPushAsync<T>(string key, T value)

- async long LPushMultipleAsync(string key, List<string> values)

- async long RPushAsync(string key, string value)

- async long RPushAsync<T>(string key, T value)

- async long RPushMultipleAsync(string key, List<string> values)

- async long LPushXAsync(string key, string value)

- async long RPushXAsync(string key, string value)

- async string LPopAsync(string key)

- async T LPopObjectAsync<T>(string key)

- async T LPopAsync<T>(string key)

- async byte[]LPopBytesAsync(string key)

- async string RPopAsync(string key)

- async T RPopObjectAsync<T>(string key)

- async T RPopAsync<T>(string key)

- async byte[]RPopBytesAsync(string key)

- async long LLenAsync(string key)

- async string LIndexAsync(string key, long index)

- async List<string> LRangeAsync(string key, long start, long stop)

- async List<T> LRangeObjectsAsync<T>(string key, long start, long stop)

- async List<T> LRangeAsync<T>(string key, long start, long stop)

- async bool LSetAsync(string key, long index, string value)

- async long LRemAsync(string key, long count, string value)

- async bool LTrimAsync(string key, long start, long stop)

- async string RPopLPushAsync(string source, string destination)

- async List<string> BLPopAsync(string key, int timeoutSeconds)

- async List<string> BRPopAsync(string key, int timeoutSeconds)

- async long SAddAsync(string key, string member)

- async long SAddMultipleAsync(string key, List<string> members)

- async long SRemAsync(string key, string member)

- async long SRemMultipleAsync(string key, List<string> members)

- async List<string> SMembersAsync(string key)

- async bool SIsMemberAsync(string key, string member)

- async long SCardAsync(string key)

- async string SPopAsync(string key)

- async string SRandMemberAsync(string key)

- async List<string> SRandMemberMultipleAsync(string key, int count)

- async List<string> SDiffAsync(List<string> keys)

- async List<string> SInterAsync(List<string> keys)

- async List<string> SUnionAsync(List<string> keys)

- async long SDiffStoreAsync(string destination, List<string> keys)

- async long SInterStoreAsync(string destination, List<string> keys)

- async long SUnionStoreAsync(string destination, List<string> keys)

- async bool SMoveAsync(string source, string destination, string member)

- async RedisScanResult SScanAsync(string key, long cursor, string pattern, int count)

- async long ZAddAsync(string key, double score, string member)

- async long ZAddMultipleAsync(string key, List<ZMember> members)

- async double ZScoreAsync(string key, string member)

- async string ZScoreStringAsync(string key, string member)

- async double ZIncrByAsync(string key, double increment, string member)

- async long ZCardAsync(string key)

- async long ZCountAsync(string key, string min, string max)

- async List<string> ZRangeAsync(string key, long start, long stop)

- async List<ZMember> ZRangeWithScoresAsync(string key, long start, long stop)

- async List<string> ZRevRangeAsync(string key, long start, long stop)

- async List<ZMember> ZRevRangeWithScoresAsync(string key, long start, long stop)

- async List<string> ZRangeByScoreAsync(string key, string min, string max)

- async List<string> ZRangeByScoreAsync(string key, string min, string max, int offset, int count)

- async List<ZMember> ZRangeByScoreWithScoresAsync(string key, string min, string max)

- async long ZRankAsync(string key, string member)

- async long ZRevRankAsync(string key, string member)

- async long ZRemAsync(string key, string member)

- async long ZRemMultipleAsync(string key, List<string> members)

- async long ZRemRangeByRankAsync(string key, long start, long stop)

- async long ZRemRangeByScoreAsync(string key, string min, string max)

- async RedisScanResult ZScanAsync(string key, long cursor, string pattern, int count)

- async int SetBitAsync(string key, long offset, int value)

- async int GetBitAsync(string key, long offset)

- async long BitCountAsync(string key)

- async long BitCountAsync(string key, long start, long end)

- async long BitOpAndAsync(string destKey, List<string> keys)

- async long BitOpOrAsync(string destKey, List<string> keys)

- async long BitOpXorAsync(string destKey, List<string> keys)

- async long BitOpNotAsync(string destKey, string srcKey)

- async long bitOp(string op, string destKey, List<string> keys)

- async long BitPosAsync(string key, int bit)

- async bool PfAddAsync(string key, string element)

- async bool PfAddMultipleAsync(string key, List<string> elements)

- async long PfCountAsync(string key)

- async long PfCountMultipleAsync(List<string> keys)

- async bool PfMergeAsync(string destKey, List<string> sourceKeys)

- async long PublishAsync(string channel, string message)

- async long DbSizeAsync()

- async bool FlushDbAsync()

- async bool FlushAllAsync()

- async List<string> TimeAsync()

- async string InfoAsync()

- async string InfoAsync(string section)

- async string BgSaveAsync()

- async long LastSaveAsync()

- string GetLastError()

- bool IsConnected()


## RedisEntry (class)

- public string key;

- public string value;

- public RedisEntry(string key, string value)


## RedisOptions (class)

- public string host;

- public int port;

- public string password;

- public string user;

- public int database;

- public int poolSize;

- public bool ssl;

- public int connectTimeoutMs;

- public RedisOptions()

- public static RedisOptions Parse(string connectionString)

- void parseHostPort(string hp)

- void parseUri(string uri)


## RedisPool (class)

- string host;

- int port;

- int connectTimeoutMs;

- RedisOptions options;

- PoolCore<RedisClient> core;

- public RedisSerializer Serialize;

- public RedisDeserializer Deserialize;

- public NoticeEventHandler Notice;

- public RedisPool(string connectionString):this(RedisOptions.Parse(connectionString))

- public RedisPool(RedisOptions options)

- public RedisPool(string host, int port, int maxSize):this(host, port, maxSize, 3000)

- public RedisPool(string host, int port, int maxSize, int connectTimeoutMs)

- async RedisClient OpenOne()

- void NoteOpenEpisode(string detail)

- async RedisClient AcquireAsync()

- void Release(RedisClient client)

- int IdleCount()

- int LiveCount()

- int WaitingCount()

- int MaxSize()

- bool IsClosed()

- void Close()

- public async RedisReply CommandAsync(List<string> args)

- public async RedisReply CommandBytesAsync(List<string> prefixArgs, byte[]binaryArg)

- public async bool PingAsync()

- public async string EchoAsync(string message)

- public async string GetAsync(string key)

- public async T GetObjectAsync<T>(string key)

- public async T GetAsync<T>(string key)

- public async byte[]GetBytesAsync(string key)

- public async RedisReply GetReplyAsync(string key)

- public async string GetSetAsync(string key, string val)

- public async bool SetAsync(string key, string val)

- public async bool SetAsync<T>(string key, T val)

- public async bool SetAsync(string key, string val, int expireSeconds)

- public async bool SetAsync<T>(string key, T val, int expireSeconds)

- public async bool SetAsync(string key, string val, int expireSeconds, bool nx, bool xx)

- public async bool SetAsync<T>(string key, T val, int expireSeconds, bool nx, bool xx)

- public async bool SetExAsync(string key, int seconds, string val)

- public async bool SetExAsync<T>(string key, int seconds, T val)

- public async bool SetNxAsync(string key, string val)

- public async bool SetNxAsync<T>(string key, T val)

- public async bool SetNxAsync(string key, string val, int expireSeconds)

- public async bool SetNxAsync<T>(string key, T val, int expireSeconds)

- public async bool SetXxAsync(string key, string val)

- public async bool SetBytesAsync(string key, byte[]val)

- public async List<string> MGetAsync(params string[]keys)

- public async List<string> MGetAsync(List<string> keys)

- public async bool MSetAsync(List<RedisEntry> entries)

- public async bool MSetAsync(params string[]keyValues)

- public async bool MSetAsync(Dictionary <string, string> dict)

- public async bool MSetAsync(List<string> keys, List<string> vals)

- public async bool MSetNxAsync(List<RedisEntry> entries)

- public async int IncrAsync(string key)

- public async long Incr64Async(string key)

- public async long IncrByAsync(string key, long amount)

- public async double IncrByFloatAsync(string key, double amount)

- public async int DecrAsync(string key)

- public async long Decr64Async(string key)

- public async long DecrByAsync(string key, long amount)

- public async long StrLenAsync(string key)

- public async long AppendAsync(string key, string value)

- public async string GetRangeAsync(string key, long start, long end)

- public async long SetRangeAsync(string key, long offset, string value)

- public async int DelAsync(string key)

- public async int DelMultipleAsync(List<string> keys)

- public async int ExistsAsync(string key)

- public async int ExistsMultipleAsync(List<string> keys)

- public async string TypeAsync(string key)

- public async int ExpireAsync(string key, int seconds)

- public async int ExpireAtAsync(string key, long timestamp)

- public async int PExpireAsync(string key, long ms)

- public async long TtlAsync(string key)

- public async long PTtlAsync(string key)

- public async int PersistAsync(string key)

- public async List<string> KeysAsync(string pattern)

- public async bool RenameAsync(string key, string newKey)

- public async bool RenameNxAsync(string key, string newKey)

- public async string RandomKeyAsync()

- public async byte[]DumpAsync(string key)

- public async RedisScanResult ScanAsync(long cursor, string pattern, int count)

- public async long HSetAsync(string key, string field, string value)

- public async long HSetAsync<T>(string key, string field, T value)

- public async bool HSetNxAsync(string key, string field, string value)

- public async bool HSetNxAsync<T>(string key, string field, T value)

- public async string HGetAsync(string key, string field)

- public async T HGetObjectAsync<T>(string key, string field)

- public async T HGetAsync<T>(string key, string field)

- public async byte[]HGetBytesAsync(string key, string field)

- public async bool HMSetAsync(string key, Dictionary <string, string> dict)

- public async bool HMSetAsync(string key, List<RedisEntry> entries)

- public async bool HMSetAsync(string key, List<string> fields, List<string> values)

- public async List<string> HMGetAsync(string key, params string[]fields)

- public async List<string> HMGetAsync(string key, List<string> fields)

- public async long HIncrByAsync(string key, string field, long amount)

- public async double HIncrByFloatAsync(string key, string field, double amount)

- public async bool HExistsAsync(string key, string field)

- public async int HDelAsync(string key, string field)

- public async int HDelMultipleAsync(string key, List<string> fields)

- public async long HLenAsync(string key)

- public async List<string> HKeysAsync(string key)

- public async List<string> HValsAsync(string key)

- public async Dictionary <string, string> HGetAllAsync(string key)

- public async long HStrLenAsync(string key, string field)

- public async RedisScanResult HScanAsync(string key, long cursor, string pattern, int count)

- public async long LPushAsync(string key, string value)

- public async long LPushAsync<T>(string key, T value)

- public async long LPushMultipleAsync(string key, List<string> values)

- public async long RPushAsync(string key, string value)

- public async long RPushAsync<T>(string key, T value)

- public async long RPushMultipleAsync(string key, List<string> values)

- public async long LPushXAsync(string key, string value)

- public async long RPushXAsync(string key, string value)

- public async string LPopAsync(string key)

- public async T LPopObjectAsync<T>(string key)

- public async T LPopAsync<T>(string key)

- public async byte[]LPopBytesAsync(string key)

- public async string RPopAsync(string key)

- public async T RPopObjectAsync<T>(string key)

- public async T RPopAsync<T>(string key)

- public async byte[]RPopBytesAsync(string key)

- public async long LLenAsync(string key)

- public async string LIndexAsync(string key, long index)

- public async List<string> LRangeAsync(string key, long start, long stop)

- public async List<T> LRangeObjectsAsync<T>(string key, long start, long stop)

- public async List<T> LRangeAsync<T>(string key, long start, long stop)

- public async bool LSetAsync(string key, long index, string value)

- public async long LRemAsync(string key, long count, string value)

- public async bool LTrimAsync(string key, long start, long stop)

- public async string RPopLPushAsync(string source, string destination)

- public async List<string> BLPopAsync(string key, int timeoutSeconds)

- public async List<string> BRPopAsync(string key, int timeoutSeconds)

- public async long SAddAsync(string key, string member)

- public async long SAddMultipleAsync(string key, List<string> members)

- public async long SRemAsync(string key, string member)

- public async long SRemMultipleAsync(string key, List<string> members)

- public async List<string> SMembersAsync(string key)

- public async bool SIsMemberAsync(string key, string member)

- public async long SCardAsync(string key)

- public async string SPopAsync(string key)

- public async string SRandMemberAsync(string key)

- public async List<string> SRandMemberMultipleAsync(string key, int count)

- public async List<string> SDiffAsync(List<string> keys)

- public async List<string> SInterAsync(List<string> keys)

- public async List<string> SUnionAsync(List<string> keys)

- public async long SDiffStoreAsync(string destination, List<string> keys)

- public async long SInterStoreAsync(string destination, List<string> keys)

- public async long SUnionStoreAsync(string destination, List<string> keys)

- public async bool SMoveAsync(string source, string destination, string member)

- public async RedisScanResult SScanAsync(string key, long cursor, string pattern, int count)

- public async long ZAddAsync(string key, double score, string member)

- public async long ZAddMultipleAsync(string key, List<ZMember> members)

- public async double ZScoreAsync(string key, string member)

- public async string ZScoreStringAsync(string key, string member)

- public async double ZIncrByAsync(string key, double increment, string member)

- public async long ZCardAsync(string key)

- public async long ZCountAsync(string key, string min, string max)

- public async List<string> ZRangeAsync(string key, long start, long stop)

- public async List<ZMember> ZRangeWithScoresAsync(string key, long start, long stop)

- public async List<string> ZRevRangeAsync(string key, long start, long stop)

- public async List<ZMember> ZRevRangeWithScoresAsync(string key, long start, long stop)

- public async List<string> ZRangeByScoreAsync(string key, string min, string max)

- public async List<string> ZRangeByScoreAsync(string key, string min, string max, int offset, int count)

- public async List<ZMember> ZRangeByScoreWithScoresAsync(string key, string min, string max)

- public async long ZRankAsync(string key, string member)

- public async long ZRevRankAsync(string key, string member)

- public async long ZRemAsync(string key, string member)

- public async long ZRemMultipleAsync(string key, List<string> members)

- public async long ZRemRangeByRankAsync(string key, long start, long stop)

- public async long ZRemRangeByScoreAsync(string key, string min, string max)

- public async RedisScanResult ZScanAsync(string key, long cursor, string pattern, int count)

- public async int SetBitAsync(string key, long offset, int value)

- public async int GetBitAsync(string key, long offset)

- public async long BitCountAsync(string key)

- public async long BitCountAsync(string key, long start, long end)

- public async long BitOpAndAsync(string destKey, List<string> keys)

- public async long BitOpOrAsync(string destKey, List<string> keys)

- public async long BitOpXorAsync(string destKey, List<string> keys)

- public async long BitOpNotAsync(string destKey, string srcKey)

- public async long BitPosAsync(string key, int bit)

- public async bool PfAddAsync(string key, string element)

- public async bool PfAddMultipleAsync(string key, List<string> elements)

- public async long PfCountAsync(string key)

- public async long PfCountMultipleAsync(List<string> keys)

- public async bool PfMergeAsync(string destKey, List<string> sourceKeys)

- public async long PublishAsync(string channel, string message)

- public async long DbSizeAsync()

- public async bool FlushDbAsync()

- public async bool FlushAllAsync()

- public async List<string> TimeAsync()

- public async string InfoAsync()

- public async string InfoAsync(string section)

- public async string BgSaveAsync()

- public async long LastSaveAsync()


## RedisReply (class)

- static int STR=1;

- static int ERROR=2;

- static int INT=3;

- static int BULK=4;

- static int NIL=5;

- static int ARRAY=6;

- int kind;

- string str;

- int len;

- int integer;

- long integer64;

- List<RedisReply> items;

- RedisReply()

- bool IsNil()

- bool IsError()

- bool IsString()

- byte[]AsBytes()

- string AsString()

- int AsInt()

- long AsLong()

- double AsDouble()

- bool AsBool()

- List<string> AsListString()

- Dictionary <string, string> AsDictionary()

- List<ZMember> AsZMemberList()

- int Count()

- RedisReply At(int i)


## RedisScanResult (class)

- public long cursor;

- public List<string> items;

- public RedisScanResult(long cursor, List<string> items)


## ZMember (class)

- public string member;

- public double score;

- public ZMember(string member, double score)


## object (delegate)

`delegate object RedisDeserializer(string json, TypeInfo type);`


## string (delegate)

`delegate string RedisSerializer(object obj);`


## void (delegate)

`delegate void NoticeEventHandler(object sender, NoticeEventArgs e);`
