# System.Data.Redis

> 源码: `packages/Zan.Data/src/System/Data/Redis/RedisClient.zan`, `packages/Zan.Data/src/System/Data/Redis/RedisPool.zan`, `packages/Zan.Data/src/System/Data/Redis/RedisReply.zan`, `packages/Zan.Data/src/System/Data/Redis/RedisTypes.zan`

全面对标 FreeRedis 规范设计的高性能、全异步协程化 Redis 客户端库。
支持连接字符串解析、全自动池化借还透明路由，以及覆盖 String、Key、Hash、List、Set、ZSet、Bitmap、HyperLogLog、PubSub、Server 的全套强类型命令，彻底告别手拼底层命令。

---

## 快速上手 (FreeRedis 风格)

### 1. 自动池化客户端 (推荐，多协程高并发安全)
```zan
using System.Data.Redis;

// 像 FreeRedis 一样通过连接字符串初始化，内部自动池化管理
RedisClient r = new RedisClient("127.0.0.1:6379,password=123456,defaultDatabase=1,poolsize=20");

// 字符串操作
await r.SetAsync("name", "Alice", 60);       // 带过期时间原子设置
string name = await r.GetAsync("name");

// 哈希操作 (直接强类型调用，告别 CommandAsync 手拼)
await r.HSetAsync("user:1", "age", "25");
string age = await r.HGetAsync("user:1", "age");
Dictionary<string, string> userMap = await r.HGetAllAsync("user:1");

// 列表操作
await r.RPushAsync("queue:jobs", "task-1");
List<string> jobs = await r.LRangeAsync("queue:jobs", 0, -1);

// 有序集合操作 (排行榜)
await r.ZAddAsync("leaderboard", 100.0, "player-1");
List<ZMember> top = await r.ZRevRangeWithScoresAsync("leaderboard", 0, 10);

r.Close();
```

### 2. 单物理连接模式
```zan
RedisClient r = await RedisClient.ConnectAsync("127.0.0.1", 6379);
await r.SetAsync("k", "v");
r.Close();
```

---

## RedisTypes 数据结构

### RedisOptions (class)
- `static RedisOptions Parse(string connectionString)`
  - 支持标准属性串：`"127.0.0.1:6379,password=xxx,defaultDatabase=0,poolsize=20,ssl=true,connectTimeout=3000"`
  - 支持简单格式：`"127.0.0.1:6379"` / `"localhost"`
  - 支持 URI 格式：`"redis://:pwd@127.0.0.1:6379/1"` / `"rediss://..."` (SSL)

### RedisEntry (class)
- `public string key;`
- `public string value;`
- 实体化键值对（用于 MSET、HMSET 批量操作）。

### ZMember (class)
- `public string member;`
- `public double score;`
- 有序集合 (ZSet) 的成员与分数实体。

### RedisScanResult (class)
- `public long cursor;`
- `public List<string> items;`
- SCAN / HSCAN / SSCAN / ZSCAN 迭代游标与结果集。

---

## RedisClient 强类型命令家族

### 1. 字符串 (Strings)
- `GetAsync(key)` -> `string`
- `GetBytesAsync(key)` -> `byte[]`
- `GetSetAsync(key, value)` -> `string`
- `SetAsync(key, val)` -> `bool`
- `SetAsync(key, val, expireSeconds)` -> `bool`
- `SetAsync(key, val, expireSeconds, nx, xx)` -> `bool`
- `SetExAsync(key, seconds, val)` -> `bool`
- `SetNxAsync(key, val)` -> `bool`
- `SetNxAsync(key, val, expireSeconds)` -> `bool`
- `SetXxAsync(key, val)` -> `bool`
- `SetBytesAsync(key, byte[])` -> `bool`
- `MGetAsync(List<string> keys)` -> `List<string>`
- `MSetAsync(List<RedisEntry> entries)` -> `bool`
- `MSetAsync(Dictionary<string, string> dict)` -> `bool`
- `MSetNxAsync(List<RedisEntry> entries)` -> `bool`
- `IncrAsync(key)` -> `int`
- `Incr64Async(key)` -> `long`
- `IncrByAsync(key, amount)` -> `long`
- `IncrByFloatAsync(key, amount)` -> `double`
- `DecrAsync(key)` -> `int`
- `Decr64Async(key)` -> `long`
- `DecrByAsync(key, amount)` -> `long`
- `StrLenAsync(key)` -> `long`
- `AppendAsync(key, value)` -> `long`
- `GetRangeAsync(key, start, end)` -> `string`
- `SetRangeAsync(key, offset, value)` -> `long`

### 2. 键管理 (Keys)
- `DelAsync(key)` -> `int`
- `DelMultipleAsync(List<string> keys)` -> `int`
- `ExistsAsync(key)` -> `int`
- `ExistsMultipleAsync(List<string> keys)` -> `int`
- `TypeAsync(key)` -> `string` ("string", "hash", "list", "set", "zset", "none")
- `ExpireAsync(key, seconds)` -> `int`
- `ExpireAtAsync(key, timestamp)` -> `int`
- `PExpireAsync(key, ms)` -> `int`
- `TtlAsync(key)` -> `long`
- `PTtlAsync(key)` -> `long`
- `PersistAsync(key)` -> `int`
- `KeysAsync(pattern)` -> `List<string>`
- `RenameAsync(key, newKey)` -> `bool`
- `RenameNxAsync(key, newKey)` -> `bool`
- `RandomKeyAsync()` -> `string`
- `DumpAsync(key)` -> `byte[]`
- `ScanAsync(cursor, pattern, count)` -> `RedisScanResult`

### 3. 哈希 (Hashes)
- `HSetAsync(key, field, value)` -> `long`
- `HSetNxAsync(key, field, value)` -> `bool`
- `HGetAsync(key, field)` -> `string`
- `HGetBytesAsync(key, field)` -> `byte[]`
- `HMSetAsync(key, Dictionary<string, string> dict)` -> `bool`
- `HMSetAsync(key, List<RedisEntry> entries)` -> `bool`
- `HMGetAsync(key, List<string> fields)` -> `List<string>`
- `HIncrByAsync(key, field, amount)` -> `long`
- `HIncrByFloatAsync(key, field, amount)` -> `double`
- `HExistsAsync(key, field)` -> `bool`
- `HDelAsync(key, field)` -> `int`
- `HDelMultipleAsync(key, List<string> fields)` -> `int`
- `HLenAsync(key)` -> `long`
- `HKeysAsync(key)` -> `List<string>`
- `HValsAsync(key)` -> `List<string>`
- `HGetAllAsync(key)` -> `Dictionary<string, string>`
- `HStrLenAsync(key, field)` -> `long`
- `HScanAsync(key, cursor, pattern, count)` -> `RedisScanResult`

### 4. 列表 (Lists)
- `LPushAsync(key, value)` / `LPushMultipleAsync(key, values)` -> `long`
- `RPushAsync(key, value)` / `RPushMultipleAsync(key, values)` -> `long`
- `LPushXAsync(key, value)` / `RPushXAsync(key, value)` -> `long`
- `LPopAsync(key)` / `LPopBytesAsync(key)` -> `string` / `byte[]`
- `RPopAsync(key)` / `RPopBytesAsync(key)` -> `string` / `byte[]`
- `LLenAsync(key)` -> `long`
- `LIndexAsync(key, index)` -> `string`
- `LRangeAsync(key, start, stop)` -> `List<string>`
- `LSetAsync(key, index, value)` -> `bool`
- `LRemAsync(key, count, value)` -> `long`
- `LTrimAsync(key, start, stop)` -> `bool`
- `RPopLPushAsync(source, destination)` -> `string`
- `BLPopAsync(key, timeoutSeconds)` -> `List<string>`
- `BRPopAsync(key, timeoutSeconds)` -> `List<string>`

### 5. 集合 (Sets)
- `SAddAsync(key, member)` / `SAddMultipleAsync(key, members)` -> `long`
- `SRemAsync(key, member)` / `SRemMultipleAsync(key, members)` -> `long`
- `SMembersAsync(key)` -> `List<string>`
- `SIsMemberAsync(key, member)` -> `bool`
- `SCardAsync(key)` -> `long`
- `SPopAsync(key)` -> `string`
- `SRandMemberAsync(key)` / `SRandMemberMultipleAsync(key, count)` -> `string` / `List<string>`
- `SDiffAsync(keys)` / `SInterAsync(keys)` / `SUnionAsync(keys)` -> `List<string>`
- `SDiffStoreAsync(dest, keys)` / `SInterStoreAsync(dest, keys)` / `SUnionStoreAsync(dest, keys)` -> `long`
- `SMoveAsync(source, destination, member)` -> `bool`
- `SScanAsync(key, cursor, pattern, count)` -> `RedisScanResult`

### 6. 有序集合 (Sorted Sets / ZSet)
- `ZAddAsync(key, score, member)` / `ZAddMultipleAsync(key, List<ZMember> members)` -> `long`
- `ZScoreAsync(key, member)` -> `double`
- `ZScoreStringAsync(key, member)` -> `string`
- `ZIncrByAsync(key, increment, member)` -> `double`
- `ZCardAsync(key)` -> `long`
- `ZCountAsync(key, min, max)` -> `long`
- `ZRangeAsync(key, start, stop)` -> `List<string>`
- `ZRangeWithScoresAsync(key, start, stop)` -> `List<ZMember>`
- `ZRevRangeAsync(key, start, stop)` -> `List<string>`
- `ZRevRangeWithScoresAsync(key, start, stop)` -> `List<ZMember>`
- `ZRangeByScoreAsync(key, min, max)` / `ZRangeByScoreWithScoresAsync(key, min, max)`
- `ZRankAsync(key, member)` / `ZRevRankAsync(key, member)` -> `long`
- `ZRemAsync(key, member)` / `ZRemMultipleAsync(key, members)` -> `long`
- `ZRemRangeByRankAsync(key, start, stop)` / `ZRemRangeByScoreAsync(key, min, max)` -> `long`
- `ZScanAsync(key, cursor, pattern, count)` -> `RedisScanResult`

### 7. 位图 (Bitmaps)
- `SetBitAsync(key, offset, value)` -> `int`
- `GetBitAsync(key, offset)` -> `int`
- `BitCountAsync(key)` / `BitCountAsync(key, start, end)` -> `long`
- `BitOpAndAsync(destKey, keys)` / `BitOpOrAsync` / `BitOpXorAsync` / `BitOpNotAsync` -> `long`
- `BitPosAsync(key, bit)` -> `long`

### 8. HyperLogLog
- `PfAddAsync(key, element)` / `PfAddMultipleAsync(key, elements)` -> `bool`
- `PfCountAsync(key)` / `PfCountMultipleAsync(keys)` -> `long`
- `PfMergeAsync(destKey, sourceKeys)` -> `bool`

### 9. 服务端与管理 (Server)
- `PingAsync()` -> `bool`
- `EchoAsync(message)` -> `string`
- `SelectAsync(index)` -> `bool`
- `DbSizeAsync()` -> `long`
- `FlushDbAsync()` / `FlushAllAsync()` -> `bool`
- `TimeAsync()` -> `List<string>`
- `InfoAsync()` / `InfoAsync(section)` -> `string`
- `BgSaveAsync()` -> `string`
- `LastSaveAsync()` -> `long`

---

## RedisPool 连接池

在 `RedisPool` 上同样提供上述完整的全套命令 API。
调用 `await pool.GetAsync(key)` 等方法时，内部自动从连接池借出连接、执行命令，并在 `finally` 块中安全归还，无并发冲突，实现零心智负担的高性能 Redis 访问。
