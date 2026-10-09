# Sdk.Steam

> 源码: `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamAchievement.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamClient.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamException.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamOwnedGame.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamPlayerSummary.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamResponse.zan`, `packages/Zan.Sdk.Steam/src/Sdk/Steam/SteamTicketResult.zan`


## SteamAchievement (class)

- public string Name;

- public string DisplayName;

- public string Description;

- public bool Hidden;

- public string Icon;

- public string IconGray;

- SteamAchievement(string name, string displayName, string description, bool hidden, string icon, string iconGray)

- static SteamAchievement From(JsonValue v)

- static string Text(JsonValue v, string key)


## SteamClient (class)

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

- SteamClient PlainHttpMode()

- SteamClient Server(string host, int port)

- SteamClient Timeout(int ms)

- SteamClient WithCallPolicy(ExternalCallPolicy policy)

- ExternalCallPolicy CallPolicy()

- async SteamResponse GetNumberOfCurrentPlayersAsync()

- async SteamResponse GetSchemaForGameAsync(string language)

- async SteamResponse SetUserStatsForGameAsync(JsonValue stats, int count)

- async SteamResponse GetPlayerSummariesAsync(List<string> steamIds)

- async SteamTicketResult AuthenticateUserTicketAsync(string ticket, string identity)

- async SteamResponse GetOwnedGamesAsync(string steamId, bool includeAppInfo, bool includeFreeGames, List<int> appIds)

- async List<SteamPlayerSummary> GetPlayersAsync(List<string> steamIds)

- async List<SteamAchievement> GetAchievementsSchemaAsync(string language)

- async List<SteamOwnedGame> GetOwnedGameListAsync(string steamId, bool includeAppInfo, bool includeFreeGames)

- async SteamResponse GetAsync(string path)

- async SteamResponse PostFormAsync(string path, string formBody)

- HttpClient NewHttp()


## SteamException (class)

- public string Code;

- public string Body;

- public SteamException(string code, string message, string body)


## SteamOwnedGame (class)

- public int AppId;

- public string Name;

- public int PlaytimeForeverMinutes;

- public int Playtime2WeeksMinutes;

- public long RTimeLastPlayed;

- SteamOwnedGame(int appId, string name, int playtimeForever, int playtime2Weeks, long rtimeLastPlayed)

- static SteamOwnedGame From(JsonValue v)

- static string Text(JsonValue v, string key)

- static int Num(JsonValue v, string key)

- static long Num64(JsonValue v, string key)


## SteamPlayerSummary (class)

- public string SteamId;

- public string PersonaName;

- public string ProfileUrl;

- public string Avatar;

- public string AvatarFull;

- public int PersonaState;

- public int CommunityVisibilityState;

- public long LastLogOff;

- public long TimeCreated;

- public string GameExtraInfo;

- public long GameId;

- public string LocCountryCode;

- SteamPlayerSummary()

- static SteamPlayerSummary From(JsonValue v)

- static string Text(JsonValue v, string key)

- static int Num(JsonValue v, string key)

- static long Num64(JsonValue v, string key)


## SteamResponse (class)

- public string Data;

- public int StatusCode;

- public JsonValue Value;

- static string lastErrCode="";

- static string lastErrMsg="";

- SteamResponse(string data, int statusCode, JsonValue v)

- static SteamResponse Parse(int statusCode, string body)

- static SteamResponse Inspect(int statusCode, string body)

- static SteamResponse Failure(string code, string msg)

- string Str(string path, string dflt)

- int Int(string path, int dflt)

- long Long(string path, long dflt)

- bool Bool(string path, bool dflt)


## SteamTicketResult (class)

- public string SteamId;

- public string OwnerSteamId;

- public bool VacBanned;

- public bool PublisherBanned;

- SteamTicketResult(string steamId, string ownerSteamId, bool vacBanned, bool publisherBanned)

- static SteamTicketResult Parse(JsonValue root)

- static int ResultCode(JsonValue root)

- static string IdText(JsonValue root, string path)

- static bool Banned(JsonValue root, string path)
