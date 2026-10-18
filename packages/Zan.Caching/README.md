# Zan.Caching

进程内 + 二层缓存，纯 Zan 实现，无原生依赖。`using System.Caching;` 即可。

## 内容

| 类型 | 作用 |
|---|---|
| `MemoryCache<TValue>` | 进程内 LRU + TTL 缓存。O(1) 命中、O(1) 淘汰、稳态零堆分配 |
| `CacheStats` | 命中/未命中/淘汰/过期计数快照（值拷贝，可安全异步携带） |
| `IStringCache` | 二层缓存后端契约（Redis / Memcached / 自建远端 KV 实现它） |
| `LayeredCache` | L1 进程内 + L2 远端：读穿透（命中回填 L1）、写穿透、失效两层同删 |

## 用法

```zan
using System.Caching;

MemoryCache<string> tokens = MemoryCache<string>.Synchronized(4096, 60000);
tokens.Set("user:42", "token-abc");            // 按默认 TTL 写入
string t = tokens.Get("user:42");              // 未命中返回 null
tokens.Set("user:43", "token-xyz", 300000);    // 该条单独指定 TTL
tokens.Remove("user:42");

CacheStats s = tokens.Stats();
Log.Info(s.Line());                            // cache hits=… misses=… evict=… expire=…
```

结构化的值用泛型形态直接放进来，缓存不关心类型：

```zan
MemoryCache<JsonValue> docs = new MemoryCache<JsonValue>();
docs.SetCapacity(512);
docs.SetSlidingTtl(120000);                    // 命中续期
docs.Set("/pages/home", pageJson);
JsonValue p = docs.Get("/pages/home");
```

二层缓存：

```zan
// RedisCache 由 Zan.Data 的 Redis 客户端薄封装实现 IStringCache（值形态即 bulk string）
IStringCache remote = new RedisCache(redisClient);
LayeredCache cache = new LayeredCache(remote, 8192, 30000);  // L1: 8192 条 / 30s

string v = cache.Get("cfg:site");              // L1 → L2 → 未命中
if (v == null) {
    v = LoadFromDb();
    cache.Set("cfg:site", v, 600000);          // 写穿 L2(10min) + L1(默认 30s)
}
cache.Remove("cfg:site");                      // 两层同删
cache.InvalidateLocal("cfg:site");             // 只失效本地（远端交给 TTL）
```

## 语义契约（写代码前先对一遍）

- **`TValue` 必须是引用类型**（`string` / 类 / `JsonValue`）。未命中以 `null`
  表示，值类型（`int` 等）请用 `MemoryCache<string>` 存文本形态——Zan 的
  `object` 不接受值类型装箱（编译期报 `cannot convert 'int' to 'object'`）。
- **线程安全是显式选项**。默认不加锁，单线程/协程内最快。跨线程共享用
  `MemoryCache<TValue>.Synchronized(capacity, ttlMs)` 构造，或先 `UseLock()`；
  锁一旦打开不可关闭，且必须在任何并发访问之前调用。
- **TTL 用 `Stopwatch` 单调微秒**，系统时钟回拨不影响过期判定。
  `Set(key, value, ttlMs)` 中 `ttlMs <= 0` 表示该条不过期。
- **过期是惰性的**：`Get`/`Contains` 命中时发现自己过期会就地清除并按未命中
  记账；不访问的过期项由 `Sweep()` 回收（进程内缓存不必为此开定时器）。
- **`Contains` 不计命中、不续期滑动 TTL、不动 LRU 顺序**，只回答"在不在"。
- **LRU 只在 `capacity > 0` 时生效**；`capacity == 0` 表示不限条目数（仍清
  TTL 过期项）。淘汰顺序由 `Get`/`Set` 命中把条目提到 LRU 头决定。
- **`LayeredCache` 写顺序是先 L2 后 L1**（write-through），避免"本地以为写
  成功、远端其实没有"的裂缝。
- `CacheStats` 返回的是快照拷贝，拿到后随你怎么用都不会被缓存活动改写。

## 性能取向

- 槽位复用 + 空闲链表：容量内反复写入不新分配条目对象（稳态零堆分配）。
- LRU 是侵入式双向链表（槽位下标，`int`），命中/淘汰/移动全是 O(1)，无闭包、
  无中间集合。
- 未加锁路径只有 `Dict` 查找 + 数组访问；加锁路径是一次 `Mutex.Lock/Unlock`。
- `Stats()` 才进行对象分配（返回快照），热路径零分配。

## 跨平台与依赖

无原生依赖、无平台分支：只用到 `System.Collections.Generic`、`System.Threading`
与 `Stopwatch`，Windows / Linux / macOS / 嵌入式目标同一份代码。

## 边界

- 不提供持久化：进程退出缓存即消失（这是"缓存"的语义，不是存储）。
- 不做跨进程一致性：多层次场景下远端被其他进程改写时，靠 `InvalidateLocal`
  或 L1 短 TTL 收敛，不做主动失效广播。
- `IStringCache` 只定义最小契约（Get/Set/Remove/Contains）；批量、扫描、管道
  等扩展能力留给具体后端，不上提到本包接口。
