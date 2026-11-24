# Zan.Game.Idle

放置 / 增量类游戏机制：大数显示、数值曲线、建筑产出、多资源账本、离线收益
与打击感动效。`using Game.Idle;` 即可；依赖 Zan.Game（`Game.Core`），飘字与
动效部分依赖 Zan.Gui。

## 内容

| 类型 | 作用 |
|---|---|
| `BigNum` | long 原始值驱动的大数，显示成 1.2K / 3.4M / 5.6B … |
| `Curves` | 生产/成本/收益标准公式（成本按 `base * rate^(owned - baseOwned)` 指数增长） |
| `IdleBuilding` / `IdleState` | 建筑等级、成本倍率、每秒产出；与界面无关的世界状态 |
| `Wallet` | 多资源账本：每种资源一个槽（余额 + 上限），`Add` / `TrySpend` |
| `Offline` / `OfflineRewardCalculator` | 离线收益：按上次存档时间与产出速率、封顶时长结算 |
| `FloatingTextManager` | 对象池化的飘字（伤害、暴击、金币、经验） |
| `SquashAndStretch` | 点击果冻缩放的弹簧阻尼动效 |
| `Models/` | `FloatingTextItem`、`OfflineResult`、`WalletCostItem`、`WalletSlot` |

建筑名、价格、产出等内容数据属于具体游戏，放在模板 / 项目里，不进本包。
