# Sdk.Steam

> 源码: `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamAchievement.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamClient.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamException.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamOwnedGame.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamPlayerSummary.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamResponse.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamTicketResult.zan`


## SteamAchievement (class)

GetSchemaForGame 返回的单条成就定义。

- public string Name;
  - 成就的 API 名称（SetAchievement / 探针统计用）。

- public string DisplayName;
  - 展示名称。

- public string Description;
  - 描述；隐藏成就未解锁时 Steam 可能不给。

- public bool Hidden;
  - 1 = 解锁前隐藏。

- public string Icon;
  - 解锁后的图标 URL。

- public string IconGray;
  - 未解锁的灰色图标 URL。

- SteamAchievement(string name, string displayName, string description, bool hidden, string icon, string iconGray)

- static SteamAchievement From(JsonValue v)
  - 从 availableGameStats.achievements[] 的一个元素解析。

- static string Text(JsonValue v, string key)


## SteamClient (class)

Steam Web API 客户端（steamworks Web API，api.steampowered.com 的
HTTP/JSON 门面），用于游戏对接 Steam 的服务端与工具链场景：

- 服务器校验客户端票据（AuthenticateUserTicket，接
steam_api.GetAuthTicketForWebApi 拿到的 hex 票据）；
- 查玩家资料 / 在线状态 / 拥有游戏；
- 查在线人数、成就 Schema；
- 上报成就与统计（SetUserStatsForGame，成就即 1/0 的 int 统计）。

用法（一行可用）：

SteamClient client = new SteamClient("your-webapi-key", 480);
SteamResponse r = await client.GetNumberOfCurrentPlayersAsync();
int count = r.Int("response.player_count", 0);


可选配置一律链式，均有合理默认值：
`client.Timeout(15000).Server("api.steampowered.com", 443);`

接口版本与封装范围（方法名 = Steam 接口名，与官方文档一一对应）：
ISteamUser/GetPlayerSummaries/v0002
ISteamUserAuth/AuthenticateUserTicket/v1（须发行商 key，仅服务端调用）
ISteamUserStats/GetNumberOfCurrentPlayers/v1
ISteamUserStats/GetSchemaForGame/v2
ISteamUserStats/SetUserStatsForGame/v1
IPlayerService/GetOwnedGames/v0001

失败一律抛 `SteamException`；key 无效时 Steam 返回 HTTP 403。
Web API key 在 https://steamcommunity.com/dev/apikey 申请；
AuthenticateUserTicket 需要在 Steamworks 后台另外生成发行商 Web API key。

线程/协程安全：单个实例的配置在构造后只读，可被多个协程并发调用；
Server/Timeout 等 setter 应在并发调用前完成。

- static string DEFAULT_HOST="api.steampowered.com";

- static int DEFAULT_PORT=443;

- string key;

- int appId;

- string host;

- int port;

- int timeoutMs;

- bool plainHttp;

- ExternalCallPolicy callPolicy;

- public SteamClient(string key, int appId)
  - 用 Web API key 与自家游戏 AppId 创建客户端，指向正式网关。

- SteamClient PlainHttpMode()
  - 明文 HTTP 模式：不再对目标发 TLS。正式网关 api.steampowered.com
    永远是 HTTPS，默认关闭；给指向自建代理 / 本地假网关（如测试里的
    HttpServer 回放）的场景用。它只是关掉 TLS，目标仍要过
    ExternalCallPolicy 的地址策略——访问 loopback 必须配
    <c>ExternalCallPolicy.Default().AllowLocalHttp()</c>。

- SteamClient Server(string host, int port)
  - 指向另一个网关（自建代理 / 抓包调试 / 本地假网关测试）。

- SteamClient Timeout(int ms)
  - 请求超时（毫秒），默认 30000。

- SteamClient WithCallPolicy(ExternalCallPolicy policy)
  - 替换统一外部调用预算（测试里用它放行本地 HTTP）。

- ExternalCallPolicy CallPolicy()

- async SteamResponse GetNumberOfCurrentPlayersAsync()
  - 查询当前在线人数：ISteamUserStats/GetNumberOfCurrentPlayers/v1。
    结果读 <c>response.player_count</c>（response.result == 1 表示成功）。
    此接口无需 key。

- async SteamResponse GetSchemaForGameAsync(string language)
  - 查游戏的成就与统计 Schema：ISteamUserStats/GetSchemaForGame/v2。
    成就数组在 <c>game.availableGameStats.achievements</c>，统计在
    <c>...stats</c>；language 传语言（english 等），空串表示默认。

- async SteamResponse SetUserStatsForGameAsync(JsonValue stats, int count)
  - 上报一批成就 / 统计：ISteamUserStats/SetUserStatsForGame/v1。
    stats 形如 <c>{"achieved":1,"kills":42}</c>——成就按 1/0 的 int
    统计上报，键为成就的 API name；count 为该对象键数，超出部分忽略。
    Steam 只接受表单参数（stats[name]=value），此调用固定 POST。

- async SteamResponse GetPlayerSummariesAsync(List<string> steamIds)
  - 批量查玩家资料：ISteamUser/GetPlayerSummaries/v0002。
    steamIds 为 64 位 SteamID（字符串形式即可），Steam 上限一次 100 个，
    超出会抛 SteamException("client.paramError")。

- async SteamTicketResult AuthenticateUserTicketAsync(string ticket, string identity)
  - 服务端校验客户端票据：ISteamUserAuth/AuthenticateUserTicket/v1。
    
    ticket 是客户端 steam_api 的 GetAuthTicketForWebApi(identity) 拿到的
    票据字节的 hex 字符串；identity 必须与客户端生成时用的一致。
    需要 **发行商** Web API key（普通 key 会 403），且只能从安全的服务端
    调用。结果用 `SteamTicketResult` 承载；
    result（EResult）非 1 时抛 SteamException("auth.<EResult>")。

- async SteamResponse GetOwnedGamesAsync(string steamId, bool includeAppInfo, bool includeFreeGames, List<int> appIds)
  - 查玩家拥有的游戏：IPlayerService/GetOwnedGames/v0001。
    includeAppInfo=true 时返回游戏名与图片；includeFreeGames=true 时
    包含免费游玩的游戏。appIds 非空时只查这些 AppId
    （Steam 以 appids_filter[0]=..&appids_filter[1]=.. 形式接收）。
    注意：玩家资料隐私为"私密"时 Steam 返回 game_count=0 的空集。

- async List<SteamPlayerSummary> GetPlayersAsync(List<string> steamIds)
  - GetPlayerSummaries 的结果直接解成列表（response.players[]）。

- async List<SteamAchievement> GetAchievementsSchemaAsync(string language)
  - GetSchemaForGame 的成就定义直接解成列表（缺失时为空表）。

- async List<SteamOwnedGame> GetOwnedGameListAsync(string steamId, bool includeAppInfo, bool includeFreeGames)
  - GetOwnedGames 的游戏列表直接解成列表（response.games[]）。

- async SteamResponse GetAsync(string path)

- async SteamResponse PostFormAsync(string path, string formBody)

- HttpClient NewHttp()


## SteamException (class)

Steam Web API 调用失败时抛出的异常。

与把错误塞进返回对象、靠调用方检查 IsError 的做法不同：这里一律抛异常，
失败无法被静默忽略。Code 取值：
- "client.paramError"   本地参数校验失败；
- "client.networkError" 网络失败（建连/握手/读超时）；
- "client.badResponse"  HTTP 200 但正文不是 JSON；
- "http.403" 等         HTTP 状态码非 2xx（key 无效、限流 429 等）；
- "auth.<EResult>"      AuthenticateUserTicket 业务校验未通过。

- public string Code;
  - 错误码：本地 client.* 码、"http.<状态>" 或 EResult 数字文本。

- public string Body;
  - 原始响应报文，便于排查；本地失败时为空串。

- public SteamException(string code, string message, string body)


## SteamOwnedGame (class)

GetOwnedGames 返回的单款游戏。

- public int AppId;
  - AppId。

- public string Name;
  - 游戏名（include_appinfo=false 或隐私受限时为空串）。

- public int PlaytimeForeverMinutes;
  - 累计游玩分钟数。

- public int Playtime2WeeksMinutes;
  - 最近两周游玩分钟数；长期未玩缺失，为 0。

- public long RTimeLastPlayed;
  - 最后一次游玩的 Unix 时间（秒）；旧响应或隐私受限时为 0。

- SteamOwnedGame(int appId, string name, int playtimeForever, int playtime2Weeks, long rtimeLastPlayed)

- static SteamOwnedGame From(JsonValue v)
  - 从 response.games[] 的一个元素解析。

- static string Text(JsonValue v, string key)

- static int Num(JsonValue v, string key)

- static long Num64(JsonValue v, string key)


## SteamPlayerSummary (class)

GetPlayerSummaries 返回的单个玩家资料。

字段可用性取决于对方隐私设置：私密资料里 personastate 恒为 0，
realname / timecreated / loccountrycode 等可选字段可能整体缺失，
一律用空串 / 0 / 1970 兜底，不做二次猜测。

- public string SteamId;
  - 64 位 SteamID（字符串形式）。

- public string PersonaName;
  - 玩家昵称。

- public string ProfileUrl;
  - 资料页 URL。

- public string Avatar;
  - 32x32 头像 URL。

- public string AvatarFull;
  - 184x184 头像 URL。

- public int PersonaState;
  - 在线状态：0 离线 / 1 在线 / 2 忙碌 / 3 离开 / 4 打盹 / 5 想交易 / 6 想玩。

- public int CommunityVisibilityState;
  - 资料可见性：3 公开，其余为受限（1 私密 / 2 好友可见等）。

- public long LastLogOff;
  - 最后一次离线的 Unix 时间（秒）；从未上线过为 0。

- public long TimeCreated;
  - 注册时间 Unix 时间（秒）；隐私受限时为 0。

- public string GameExtraInfo;
  - 正在游玩的游戏名（不在游戏中为空串）。

- public long GameId;
  - 正在游玩的游戏 AppId（不在游戏中为 0）。

- public string LocCountryCode;
  - ISO 3166 国家码（未知为空串）。

- SteamPlayerSummary()

- static SteamPlayerSummary From(JsonValue v)
  - 从 players[] 的一个元素解析。

- static string Text(JsonValue v, string key)

- static int Num(JsonValue v, string key)

- static long Num64(JsonValue v, string key)


## SteamResponse (class)

一次成功调用的返回结果。

Steam Web API 的 JSON 信封各接口不一致（多数包在 <c>response</c> 里，
AuthenticateUserTicket 包在 <c>response.params</c> 里），因此本类不预置
业务模型——成功时 `Value` 是已解析的完整 JSON 树，用
<c>PathStr("response.personaname", "")</c> 等点分路径直接取值。

失败不会走到这里：HTTP 非 2xx、JSON 解析失败或票据校验 result != 1
都直接抛 `SteamException`。

- public string Data;
  - 完整响应 JSON 文本。

- public int StatusCode;
  - HTTP 状态码（成功时为 200）。

- public JsonValue Value;
  - 已解析的响应树，需要动态取值时用。

- static string lastErrCode="";

- static string lastErrMsg="";

- SteamResponse(string data, int statusCode, JsonValue v)

- static SteamResponse Parse(int statusCode, string body)
  - 解析 HTTP 200 的 Steam 响应。正文必须是合法 JSON 对象，否则抛
    SteamException("client.badResponse")。

- static SteamResponse Inspect(int statusCode, string body)

- static SteamResponse Failure(string code, string msg)

- string Str(string path, string dflt)
  - 从已解析结果中按点分路径读字符串（"response.personaname"）。

- int Int(string path, int dflt)
  - 从已解析结果中按点分路径读整数。

- long Long(string path, long dflt)
  - 从已解析结果中按点分路径读 64 位整数。

- bool Bool(string path, bool dflt)
  - 从已解析结果中按点分路径读布尔。


## SteamTicketResult (class)

AuthenticateUserTicket 的校验结果。

Steam 约定：result 的 EResult 值为 1（OK）才算通过；ownersteamid 与
steamid 不同说明该授权来自共享库 / 家庭共享，做防作弊与时长统计时
应以 ownersteamid 为准。

- public string SteamId;
  - 通过校验的玩家 64 位 SteamID（字符串形式原样保留）。

- public string OwnerSteamId;
  - 拥有该游戏许可的账号 64 位 SteamID（家庭共享时与 SteamId 不同）。

- public bool VacBanned;
  - 该账号是否被 VAC 封禁。

- public bool PublisherBanned;
  - 该账号是否被发行商封禁。

- SteamTicketResult(string steamId, string ownerSteamId, bool vacBanned, bool publisherBanned)

- static SteamTicketResult Parse(JsonValue root)
  - 从 AuthenticateUserTicket 的 JSON 树解析；result != 1 时抛 SteamException。

- static int ResultCode(JsonValue root)

- static string IdText(JsonValue root, string path)

- static bool Banned(JsonValue root, string path)
