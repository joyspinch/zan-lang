# Zan.Sdk.Steam - Steamworks Web API 客户端

`Zan.Sdk.Steam` 是面向 Zan 游戏服务端与辅助工具链的官方 Steam Web API SDK。

用于游戏服务端直接与 Steam 官方服务器通信（如票据验证、防作弊与防盗版核验、云端成就与统计上报、玩家资料查询、当前在线玩家数同步等）。

---

## 包含能力

- **用户身份与票据核验 (`ISteamUserAuth`)**：
  - `AuthenticateUserTicketAsync`：服务端核验客户端发来的 Hex Auth Ticket，杜绝假冒登录与私服盗版。
- **玩家资料与状态 (`ISteamUser`)**：
  - `GetPlayerSummariesAsync`：批量查询 SteamID 对应的昵称、头像、封禁状态、在线状态等。
- **成就与统计数据 (`ISteamUserStats`)**：
  - `GetNumberOfCurrentPlayersAsync`：实时查询游戏当前在线玩家总数。
  - `GetSchemaForGameAsync`：拉取游戏全量成就与统计字段元数据。
  - `SetUserStatsForGameAsync`：从服务端为指定玩家解锁成就或更新排行榜计数值。
- **游戏库资产查询 (`IPlayerService`)**：
  - `GetOwnedGamesAsync`：核验玩家账户库中是否真实拥有指定游戏及 DLC。

---

## 快速上手

```zan
using System;
using Sdk.Steam;

class SteamServer {
    static async void Main() {
        // 初始化 Steam 客户端 (Web API Key, 游戏的 Steam AppId)
        SteamClient steam = new SteamClient("YOUR_STEAM_WEB_API_KEY", 480);
        steam.Timeout(10000); // 10秒超时

        // 1. 查询游戏当前全网在线人数
        SteamResponse r = await steam.GetNumberOfCurrentPlayersAsync();
        int onlineCount = r.Int("response.player_count", 0);
        Console.WriteLine("当前在线玩家: " + onlineCount);

        // 2. 玩家登录时：核验客户端发来的登录票据
        string clientTicketHex = "1400000028e357...";
        SteamTicketResult auth = await steam.AuthenticateUserTicketAsync(clientTicketHex);
        if (auth.Success) {
            Console.WriteLine("登录成功，SteamID: " + auth.SteamId);
        } else {
            Console.WriteLine("鉴权失败，票据无效或已过期！");
        }
    }
}
```
