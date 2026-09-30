# ZanWeb.Security

> 源码: `packages/Zan.Mvc/src/ZanWeb/Framework/Security/Auth.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Security/DataScope.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Security/Keys.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Security/LoginThrottle.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Security/Perm.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Security/PermTable.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Security/VerifyCode.zan`


## AuthToken (class)

- static string Secret()

- static bool Ready()

- static bool Equal(string actual, string expected)

- static string EncodeUrl(string raw, int len)

- static byte[]DecodeUrl(string text, List<int> outLen)

- static byte[]Key()

- static string Issue(string uid, int tokenVersion, int now)

- static async string Resolve(string token)


## AuthUser (class)

- static int Active=1;

- static async IDbConnection Lease()

- static void Give(IDbConnection db)

- static string PasswordHash(string password, string salt)

- static string NewHash(string password)

- static bool Verify(string password, string saltCol, string stored)

- static async SysUser ByUsername(string username)

- static async SysUser ById(string uid)

- static async string Login(string username, string password)

- static async void UpgradeHash(int uid, string password)

- static int VerTtlSec=30;

- static List<string> verUids=new List<string>();

- static List<int> verVals=new List<int>();

- static List<int> verGoodUntil=new List<int>();

- static int CachedVersion(string uid, int now)

- static void StoreVersion(string uid, int version, int now)

- static void DropCachedVersion(string uid)

- static async int TokenVersion(string uid)

- static int ToInt(string s)

- static void Forget(string uid)

- static void ForgetLocal(string uid)

- static void ForgetAll()

- static void ForgetAllLocal()

- static async void RolesChanged(IDbConnection db)

- static async void RolesChanged(List<SysRole> roles, List<SysRoleGrant> grants)

- static async bool Allow(string uid, string action)

- static async bool Allow(IDbConnection db, string uid, string action)

- static async int MaskOf(string uid, string screen)

- static async int MaskOf(IDbConnection db, string uid, string screen)

- static async string RolesOf(string uid)

- static async string RolesOf(IDbConnection db, string uid)

- static async string RoleIds(IDbConnection db, SysUser me)

- static void AddId(List<int> list, int id)

- static bool HasId(List<int> list, int id)

- static bool Holds(string haystack, string needle)


## Codes (class)

- static string Ok="0000";

- static string NoAuth="0001";

- static string NoPerm="0403";

- static string BadInput="0003";

- static string NotFound="0404";

- static string Conflict="0409";

- static string Failed="0500";


## DataScope (class)

- static int RoleAll=1;

- static int RoleDept=2;

- static int RoleDeptTree=3;

- static int RoleSelf=4;

- static int RoleCustom=5;

- static int All=1;

- static int ByDepts=2;

- static int Self=4;

- int mode;

- int userId;

- List<int> depts;

- DataScope()

- bool Any()

- bool ByDept()

- bool Mine()

- List<int> Depts()

- int UserId()

- bool Covers(int rowUserId, int rowDepartmentId)

- static DataScope None()

- static DataScope Everything(int userId)

- static async DataScope Of(SysUser me, List<SysDepartment> departments)

- static async DataScope Resolve(List<SysDepartment> departments, SysUser me, string roleIds)

- static void Descend(List<SysDepartment> all, int parent, List<int> outp)

- static DataScope Restore(int userId, int mode, string depts)

- static string Join(List<int> ids)

- static void Add(List<int> list, int id)

- static bool Has(List<int> list, int id)

- static string Label(int roleScope)


## LoginThrottle (class)

登录爆破限流：按账号（大小写/空白归一）记失败次数，窗口内达到阈值
即拒绝后续尝试，直到窗口整体滑出。进程内固定窗实现——每 worker
独立计数，阈值按 worker 数放大即实际成本，窗口+阈值组合已把在线
爆破压到不可行量级；跨 worker 共享计数是 SharedTable 的后续题。

语义要点（与 AuthUser.Login 配合）：
- 被限流与密码错误同答空串，不向客户端区分原因（无账号枚举 oracle）；
Blocked() 供调用方渲染"稍后再试"文案，锁状态本就来自攻击者自己的
失败次数，不构成新信息。
- 不存在的账号也记失败：否则"账号不存在"路径不受限流，计时侧信道
可枚举有效用户名。
- 成功登录清零；键上限封内存，超出淘汰最旧（攻击者换账号扫描时
最多冲掉自己的限流记录）。

- static int MaxFails=10;

- static int WindowSec=900;

- static int MaxKeys=4096;

- static List<string> names=new List<string>();

- static List<int> counts=new List<int>();

- static List<int> windowStart=new List<int>();

- static string Norm(string user)

- static bool Allowed(string user)

- static void Fail(string user)

- static void Clear(string user)

- static bool Blocked(string user)

- static int IndexOf(string key)

- static void RemoveAt(int i)


## Perm (class)

- static Dict <string, int> bits=null;

- static Dict <string, int> masks=null;

- static List<string> screens=null;

- static Dict <string, string> titles=null;

- static List<string> unmarked=null;

- static int Full=31;

- static void Index(WebApp app)

- static bool Ready()

- static int BitOf(string action)

- static string ScreenOf(string action)

- static int MaskOf(string screen)

- static void Title(string screen, Route r)

- static string TitleOf(string screen)

- static List<string> Screens()

- static string Qualify(string stored)

- static List<string> Unmarked()

- static int BitValue(string name)

- static List<int> Order()

- static string BitLabel(int bit)

- static string BitName(int bit)

- static int MaskIn(string stored, string screen)

- static void Append(StringBuilder sb, string screen, int mask)

- static string Join(List<string> items, int max)


## PermTable (class)

- static const string ShareRoles="WEB_PERM_ROLES";

- static const string ShareUsers="WEB_PERM_USERS";

- static long UserLeaseMs=300000;

- static SharedTable roles=null;

- static SharedTable users=null;

- static bool Open()

- static bool Ready()

- static async void LoadRoles(IDbConnection db)

- static async void LoadRoles(List<SysRole> rows, List<SysRoleGrant> grants)

- static int RoleScope(string roleId)

- static string RoleDepts(string roleId)

- static bool RolesAllow(string roleIds, string action)

- static int RolesMask(string roleIds, string screen)

- static string UserRoles(string uid)

- static int UserScope(string uid)

- static string UserDepts(string uid)

- static void SetUserScope(string uid, int mode, string depts)

- static void SetUser(string uid, bool isSuper, string roleIds)

- static void Forget(string uid)

- static void ForgetAll()

- static List<string> Split(string joined)


## Routes (class)

- static string Login="/admin/login";

- static string Logout="/admin/logout";

- static string Admin="/admin";

- static string Data="/admin/monitor/data";

- static string Home="/";

- static string SignIn="/login";

- static string SignUp="/register";

- static string DataIndex="Data.Index";


## SettingKeys (class)

- static string SiteTitle="site.title";

- static string SiteSubtitle="site.subtitle";

- static string SiteDescription="site.description";

- static string SiteKeywords="site.keywords";

- static string SiteCopyright="site.copyright";

- static string SiteIcp="site.icp";

- static string SitePolice="site.police";

- static string SiteAnalytics="site.analytics";

- static string SiteTimezone="site.timezone";

- static string SiteLanguage="site.language";

- static string SiteUrl="site.url";

- static string RegisterEnabled="auth.register.enabled";

- static string RegisterVerify="auth.register.verify";

- static string ResetEnabled="auth.reset.enabled";

- static string CommentEnabled="blog.comment.enabled";

- static string CommentReview="blog.comment.review";

- static string MailHost="mail.host";

- static string MailPort="mail.port";

- static string MailSsl="mail.ssl";

- static string MailUser="mail.user";

- static string MailPass="mail.pass";

- static string MailFrom="mail.from";

- static string MailFromName="mail.fromName";

- static string AiEnabled="ai.enabled";

- static string AiBaseUrl="ai.baseUrl";

- static string AiApiKey="ai.apiKey";

- static string AiModel="ai.model";

- static string AiTemperature="ai.temperature";

- static string GenRoot="gen.root";


## VerifyCode (class)

- static int TtlSeconds=600;

- static int ResendSeconds=60;

- static string Key(string purpose, string address)

- static CacheContext Cache()

- static async bool TooSoon(string purpose, string address)

- static string CooldownKey(string address)

- static async string Issue(string purpose, string address)

- static async VerifyCodeIssue IssueForIp(string purpose, string address, string ip)

- static async bool Accept(string purpose, string address, string code)

- static string Digits(int n)


## VerifyCodeIssue (class)

- int status=-1;

- string code="";
