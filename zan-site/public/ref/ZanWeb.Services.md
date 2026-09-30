# ZanWeb.Services

> 源码: `packages/Zan.Mvc/src/ZanWeb/Framework/Services/Cache.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Services/CacheContext.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Services/ClusterBus.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Services/JobHost.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Services/Mailer.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Services/Metrics.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Services/Presence.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Services/Settings.zan`


## Cache (class)

- static CacheContext Current()

- static async string GetAsync(string key)

- static async void SetAsync(string key, string text)

- static async void SetTtlAsync(string key, string text, int seconds)

- static async void DelAsync(string key)

- static bool LocalHas(string key)

- static string LocalGet(string key)

- static string LocalGetOr(string key, string fallback)

- static void LocalSet(string key, string text)

- static void LocalSetTtl(string key, string text, int seconds)

- static void LocalDel(string key)


## CacheContext (class)

- StrMap store;

- StrMap expiry;

- RedisPool redis;

- CacheContext(RedisPool redis)

- static CacheContext Of(RedisPool redis)

- bool Shared()

- bool Alive(string key)

- bool LocalHas(string key)

- string LocalGet(string key)

- string LocalGetOr(string key, string fallback)

- void LocalSet(string key, string text)

- void LocalSetTtl(string key, string text, int seconds)

- void LocalDel(string key)

- async string GetAsync(string key)

- async void SetAsync(string key, string text)

- async void SetTtlAsync(string key, string text, int seconds)

- async void DelAsync(string key)

- async int IssueCodeAsync(string key, string cooldownKey, string ipKey, string code, int seconds, int cooldownSeconds, int windowSeconds, int maxSends)

- static int CodeMaxTries=5;

- static int CodeTriesTtl=660;

- async bool SetCodeAsync(string key, string code, int seconds)

- async bool ConsumeCodeAsync(string key, string code)

- async bool Warmup()

- void Close()


## ClusterBus (class)

- static bool enabled=false;

- static string channel="zanweb:cluster:events";

- static RedisPool pool=null;

- static bool listening=false;

- static void Init(ClusterCfg cfg, RedisPool redisPool)

- static bool Enabled()

- static void Start()

- static void Stop()

- static async void ListenLoop()

- static void Dispatch(string msg)

- static async void PublishEvictUser(string uid)

- static async void PublishEvictAll()


## JobHost (class)

- static List<SysJob> jobs=new List<SysJob>();

- static bool dirty=true;

- static bool armed=false;

- static int ticks=0;

- static int KeepLogs=200;

- static void Touch()

- static void Arm()

- static async void Loop()

- static async void Tick()

- static async void Reload()

- static async void Fire(int jobId, long prev, long now)

- static void RefreshMirror(int jobId, long now)

- static async string Run(IDbConnection db, SysJob j, long startedAt)

- static async string RunNow(IDbConnection db, SysJob j)

- static async string Execute(IDbConnection db, SysJob j)

- static string CleanupLogs(SysJob j)

- static List<string> Kinds()

- static bool Known(string kind)

- static string KindName(string kind)

- static string NormalizeParam(string kind, string param)

- static string IntervalText(int sec)


## LiveRow (class)

- int worker;

- long age;

- string json;


## MailResult (class)

- bool ok;

- string message;

- MailResult()

- static MailResult Fail(string message)

- static MailResult Good()

- bool Ok()

- string Message()


## MailSession (class)

- TcpClient tcp;

- TlsStream tls;

- string buffer;

- MailSession()

- async int Write(string line)

- async string Read()

- async string Say(string line)

- void Close()


## Mailer (class)

- static async bool Ready()

- static async string From()

- static async MailResult Send(string to, string subject, string body)

- static async MailResult Talk(MailSession s, string host, string from, string fromName, string user, string pass, string to, string subject, string body, bool startTls)

- static bool Is(string reply, string code)

- static string Why(string reply, string fallback)


## MetricsCluster (class)

- static int PublishSeconds=2;

- static int FreshSeconds=30;

- static async void Loop()

- static async void Publish()

- static async List<LiveRow> LiveRows(string col, int maxAgeSec)

- static async string SnapshotJson()

- static JsonValue ProcessesJson(List<LiveRow> rows, List<JsonValue> ms)

- static string MergeSnapshots(List<JsonValue> ms, int live)

- static void MergeTop(JsonValue merged, JsonValue add, string key)

- static void Reavg(JsonValue row)

- static JsonValue SortTop(JsonValue arr)

- static async string SeriesJson(int seconds)

- static JsonValue Num(long v)


## MetricsStore (class)

- static List<MinuteBucket> buckets=new List<MinuteBucket>();

- static List<MetricError> errors=new List<MetricError>();

- static List<MetricSql> sqlBuckets=new List<MetricSql>();

- static List<MinuteBucket> retryBuckets=new List<MinuteBucket>();

- static List<MetricError> retryErrors=new List<MetricError>();

- static List<MetricSql> retrySqlBuckets=new List<MetricSql>();

- static object batchLock=new object();

- static bool flushActive=false;

- static int MaxBuckets=8192;

- static int MaxErrors=4096;

- static int MaxSqlShapes=512;

- static string OtherSql="(其他语句)";

- static DbPool pool=null;

- static bool ready=false;

- static bool flushing=false;

- static long lastSweep=0;

- static bool Ready()

- static async bool Start()

- static async IDbConnection Lease()

- static void Give(IDbConnection db)

- static long Minute(long sec)

- static long Day(long sec)

- static async void RetireLegacyMinutes(IDbConnection db)

- static void Arm()

- static MinuteBucket Bucket(string method, string path)

- static void Record(string method, string path, int status, long us)

- static void Pushed(string method, string path, long frames, long bytes)

- static void RecordSql(string norm, long us, bool slow)

- static MetricSql SqlEntry(string norm)

- static MetricSql SqlEntryIn(List<MetricSql> rows, long day, string norm)

- static void Failed(long ts, string origin, string detail)

- static long HistP95(long[]hist, long calls, long maxUs)

- static async void Loop()

- static void MergeRetry()

- static async int Flush()

- static async void FlushAtExit()

- static async void Sweep(IDbConnection db)

- static async List<MetricSql> SqlTotals(long from, long to, int limit, string rank)

- static async MetricSql SqlOne(long from, long to, string shape)

- static async List<MetricMinute> HistoryBuckets(long from, long to, string method, string path, long stepSec, int limit)

- static async List<MetricMinute> RouteTotals(long from, long to, string method, string path, int limit)

- static async List<MetricMinute> RouteTotalsBy(long from, long to, string method, string path, int limit, string rank)

- static async MetricMinute WindowTotals(long from, long to, string method, string path)

- static async List<MetricError> Errors(long from, long to, int limit)


## MinuteBucket (class)

- MetricMinute row;

- long[]hist;


## Presence (class)

- static int Ttl=20;

- static int MaxWorkers=32;

- static int MaxIps=120;

- static int pages=0;

- static List<string> ips=new List<string>();

- static void Enter(string ip)

- static void Leave(string ip)

- static async void Publish()

- static async int Pages()

- static async int Visitors()

- static int Local()

- static int Head(string slot)

- static string Key(int worker)


## Settings (class)

- static int CacheSeconds=60;

- static StrMap values;

- static long expires;

- static async StrMap All()

- static void Forget()

- static async string Get(string name, string def)

- static async bool On(string name, bool def)

- static async void Apply(ViewData d)
