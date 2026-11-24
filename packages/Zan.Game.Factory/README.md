# Zan.Game.Factory

自动化工厂 + 塔防的模拟内核（"Forge"）：网格地图、传送带/配方生产、科技、
敌巢与波次战斗、存档。`using Game.Factory;` 即可；依赖 Zan.Game
（`Game.Core`、`Game.Pathfinding`）与 Zan.Json（存档）。

## 内容

| 类型 | 作用 |
|---|---|
| `ForgeWorld` | 世界状态：网格、单位、特效、震屏；所有模拟从这里进入 |
| `ForgeSimulation` | 每帧推进（`Step` / `TickEffects`） |
| `ForgeMap` / `ForgeGrid` / `ForgeTile` / `TerrainKind` | 地图生成、网格方向与邻接、地块与地形 |
| `ForgeFactory` / `ForgeBuilding` / `RecipeDef` / `Stock` / `ItemKind` / `BlockDef` / `BlockKind` | 建筑、配方生产与物品库存 |
| `TechDef` / `ForgeCatalog` | 科技与定义目录 |
| `ForgeCombat` / `ForgeEnemy` / `EnemyDef` / `ForgeNest` / `WaveDef` / `ForgeUnit` / `ForgeBullet` | 敌巢、波次、单位与弹道战斗 |
| `ForgeEvents` / `ForgeEvent` / `ForgeEventKind` / `ForgeEffect` | 事件与视觉特效 |
| `ForgeSave` | JSON 存档（带版本号与大小上限校验） |

注意：`ForgeCombat` 等处的波次间隔、射程、子弹速度是写死的数值，目前更像
一个具体游戏的内核，而不是可复用机制（见 WC §14）。
