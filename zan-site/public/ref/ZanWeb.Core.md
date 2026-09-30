# ZanWeb.Core

> 源码: `packages/Zan.Mvc/src/ZanWeb/Framework/Core/Boot.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Core/Cfg.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Core/Db.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Core/DbContext.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Core/Gen.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Core/Schema.zan`


## AuthCfg (class)

- string secret;

- string bootstrapPassword;

- int iter;

- static AuthCfg Defaults()


## Boot (class)

- static RouteFn routes;

- static int slowMs=1000;

- static long slowSqlUs=200000;

- static void OnRoutes(RouteFn fn)

- static async int Run()

- static void SqlTrace(string sql, long us)

- static bool OriginGuard(HttpContext ctx)

- static string HttpOrigin(string url)

- static bool SameOriginHost(string origin, string host)

- static bool ValidCorsOrigin()

- static bool CorsFilter(HttpContext ctx)

- static int traceSeq=0;

- static bool AccessLog(HttpContext ctx)

- static void Metrics(HttpContext ctx, long elapsedUs)


## CacheCfg (class)

- string driver;

- string redisHost;

- int redisPort;

- int poolSize;

- int connectTimeoutMs;

- static CacheCfg Defaults()


## Cfg (class)

- static ServerCfg Server=ServerCfg.Defaults();

- static DatabaseCfg Db=DatabaseCfg.Defaults();

- static WorkerCfg Worker=WorkerCfg.Defaults();

- static LogCfg Log=LogCfg.Defaults();

- static CacheCfg Cache=CacheCfg.Defaults();

- static AuthCfg Auth=AuthCfg.Defaults();

- static MetricsCfg Metrics=MetricsCfg.Defaults();

- static JobCfg Job=JobCfg.Defaults();

- static ClusterCfg Cluster=ClusterCfg.Defaults();

- static CorsCfg Cors=CorsCfg.Defaults();

- static bool loaded=false;

- static void Load(string relPath)

- static void ApplyEnv()

- static bool EnvBool(string name, bool fallback)

- static string EnvStr(string name, string fallback)

- static int EnvInt(string name, int fallback)

- static JsonValue Sec(JsonValue root, string name)

- static void Apply(JsonValue root)


## ClusterCfg (class)

- bool enabled;

- string bus;

- string channel;

- static ClusterCfg Defaults()


## CorsCfg (class)

- bool enabled;

- string allowOrigin;

- string allowMethods;

- string allowHeaders;

- bool allowCredentials;

- int maxAge;

- static CorsCfg Defaults()


## DatabaseCfg (class)

- string driver;

- string sqlitePath;

- string host;

- int port;

- string name;

- string user;

- string password;

- int poolSize;

- DatabaseReadOnlyCfg readOnly;

- static DatabaseCfg Defaults()


## DatabaseReadOnlyCfg (class)

- bool enabled;

- string driver;

- string sqlitePath;

- string host;

- int port;

- string name;

- string user;

- string password;

- int poolSize;

- static DatabaseReadOnlyCfg Defaults()


## Db (class)

- static DbPool OpenPool()

- static DbPool OpenReadOnlyPool()

- static void EnsureFileDir(string path)

- static RedisPool OpenRedis()


## DbContext (class)

- DbPool pool;

- DbPool readPool;

- DbContext(DbPool pool)

- DbContext(DbPool writePool, DbPool readPool)

- static DbContext Of(DbPool pool)

- static DbContext Of(DbPool writePool, DbPool readPool)

- async IDbConnection Acquire()

- void Release(IDbConnection db)

- void Discard(IDbConnection db)

- async IDbConnection AcquireRead()

- void ReleaseRead(IDbConnection db)

- bool HasSeparateReadPool()

- async int Warmup(int count)

- int IdleCount()

- int LiveCount()

- int WaitingCount()

- void Close()


## Gen (class)

- static bool Safe(string name)

- static bool SafeCode(string name)

- static string Literal(string text)

- static string Html(string text)

- static string Comment(string text)

- static bool Valid(SysGenTable t, List<SysGenColumn> cols)

- static List<DbColumnDef> Columns(List<SysGenColumn> cols)

- static async int Migrate(IDbConnection db, SysGenTable t, List<SysGenColumn> cols)

- static string ZanType(string kind)

- static string ColKind(string kind)

- static string ModelSource(SysGenTable t, List<SysGenColumn> cols)

- static int Size(SysGenColumn c)

- static string Route(SysGenTable t)

- static string OrderCol(SysGenTable t, List<SysGenColumn> cols)

- static string RelValue(SysGenColumn c)

- static string RelLabel(SysGenColumn c)

- static bool RelDesigned(SysGenColumn c, List<SysGenTable> designed)

- static SysGenTable DesignedOf(List<SysGenTable> designed, string tableName)

- static string LabelCol(SysGenTable t, List<SysGenColumn> cols)

- static bool HasStringCol(List<SysGenColumn> cols, string name)

- static string FilterOpOf(SysGenColumn c, List<SysGenTable> designed)

- static SysGenColumn TabColOf(SysGenTable t, List<SysGenColumn> cols)

- static string ColWidth(SysGenColumn c)

- static string WidthOf(string w)

- static int PageSizeOf(SysGenTable t)

- static string Stamp()

- static string ZanHeader()

- static string HtmlHeader()

- static string ControllerSource(SysGenTable t, List<SysGenColumn> cols, List<SysGenTable> designed)

- static string LabelOf(SysGenColumn c)

- static string ListViewSource(SysGenTable t, List<SysGenColumn> cols)

- static string FormViewSource(SysGenTable t, List<SysGenColumn> cols, List<SysGenTable> designed)

- static List<string> TabGroups(List<SysGenColumn> cols)

- static void FormFields(StringBuilder b, List<SysGenColumn> cols, string group, List<SysGenTable> designed, string pad)

- static string DaoSource(SysGenTable t, List<SysGenColumn> cols)

- static string FilterEmit(StringBuilder b, List<SysGenColumn> cols, List<SysGenTable> designed, string tabColumn)

- static string ApiSource(SysGenTable t, List<SysGenColumn> cols, List<SysGenTable> designed)

- static void ApiPuts(StringBuilder b, List<SysGenColumn> cols, string target, string pad)


## JobCfg (class)

- bool enabled;

- static JobCfg Defaults()


## LogCfg (class)

- bool access;

- int slowMs;

- int slowSqlMs;

- string dir;

- string pattern;

- string errorLog;

- bool debug;

- static LogCfg Defaults()


## MetricsCfg (class)

- bool enabled;

- string path;

- int flushSeconds;

- int flushRows;

- int keepDays;

- int alertErrPct;

- int alertErrCalls;

- int alertP95Ms;

- static MetricsCfg Defaults()


## Schema (class)

- static int LeaseSec=900;

- static int WaitMs=60000;

- static async bool Ensure(DbContext ctx)

- static async bool Apply(IDbConnection db)

- static string leaseHolder="";

- static string Holder()

- static async void CreateLockTable(IDbConnection db)

- static async bool AcquireLease(IDbConnection db)

- static async void ReleaseLease(IDbConnection db)

- static async bool BackfillGenFlags(IDbConnection db)

- static async bool SeedAdmin(IDbConnection db)

- static async bool SeedRoles(IDbConnection db)

- static async int AddRole(IDbConnection db, string name, string code, int rank, string description, int dataScope)

- static async bool Grant(IDbConnection db, int roleId, string screen, int mask, long now)

- static async bool SeedDepartments(IDbConnection db)

- static async bool AddDept(IDbConnection db, int parentId, string name, string code, int sortOrder)

- static async bool SeedDicts(IDbConnection db)

- static async bool AddDictType(IDbConnection db, string code, string name)

- static async bool AddDictItem(IDbConnection db, string typeCode, string value, string label, int sortOrder)

- static async bool SeedSettings(IDbConnection db)

- static async bool SeedFeatureSettings(IDbConnection db)

- static async bool EnsureSetting(IDbConnection db, string groupName, string name, string label, string value, string kind, string tip, int sortOrder)

- static async bool AddSetting(IDbConnection db, string groupName, string name, string label, string value, string kind, string tip, int sortOrder)

- static SeedFn onSeed;

- static void OnSeed(SeedFn fn)

- static async bool RunSeed(IDbConnection db)

- static async bool MigrateGrants(IDbConnection db)

- static async bool MigrateGrantKeys(IDbConnection db)

- static async bool SyncRoleGrants(WebApp app)

- static bool IsContent(string pattern)

- static async bool SeedJobs(IDbConnection db)


## ServerCfg (class)

- string name;

- string mode;

- string host;

- int port;

- int maxBodyMB;

- int uploadBodyMB;

- int globalLimitPerSec;

- string trustedProxies;

- static ServerCfg Defaults()


## WorkerCfg (class)

- int count;

- bool daemon;

- static WorkerCfg Defaults()


## bool (delegate)

`delegate bool SeedFn(IDbConnection db);`


## void (delegate)

`delegate void RouteFn(WebApp app);`
