# Zan.Game.Board

棋盘类游戏的通用机制：网格、回合、规则契约、寻路与棋类 AI 搜索。纯 Zan，
无原生依赖。`using Game.Board;` 即可。

## 内容

| 类型 | 作用 |
|---|---|
| `GridBoard` | 整数网格，棋类、战棋、推箱子类解谜与确定性地图模拟共用 |
| `GridPathfinder` | 未加权网格上的 BFS 最短路径（指定值视为障碍） |
| `IBoardRules` | 规则契约：`Validate` 校验指令，`Apply` 执行结算 |
| `BoardMatch` | 回合制对局核心：棋盘 + 回合状态 + 规则 + 指令日志，非当前玩家或不合规的指令不生效 |
| `ISearchGame` / `BoardSearch` | 棋类 AI：negamax 写法的 alpha-beta 搜索，迭代加深 |
| `GoBoard` | 围棋规则：落子、提子、打劫/自杀禁着、气数、中国规则数子（`GoProbe` / `GoScore`） |
| `Models/` | `BoardCommand`、`BoardCommandLog`、`BoardReplayEntry`、`BoardSnapshot`、`GridPath`、`GridPoint`、`TurnState` |

规则类只管规则不管表现；界面层读状态、提交指令即可。
