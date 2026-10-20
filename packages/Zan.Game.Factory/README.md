# Zan.Game.Factory

工厂自动化 + 塔防的**纯逻辑运行时**（无 GUI 依赖）。`using Game.Factory;` 按需拉入。

这类玩法（Mindustry / 异星工厂 / 守卫萝卜）的真正难点不在画面，而在**物流与波次**：
传送带怎么送、配方怎么耗电、科技树怎么解锁、敌人一波波怎么来、存档怎么兼容旧版本。
这些规则与美术无关，因此沉在包里，模板只负责把它画出来。

配套项目：[`D:/project/games/coreforge`](../../../../project/games/coreforge)（炉心边境）——
那是这套引擎的一个完整实例（渲染 + HUD + 入口都在那个仓库外的独立项目里）。
本包留在语言仓库里继续演进——它是公共机制，不是某个游戏的私产。

## 文件

| 文件 | 内容 |
|---|---|
| `ForgeWorld.zan` | 世界状态：地形 / 建筑 / 库存 / 科技 / 波次进度 |
| `ForgeGrid.zan` | 网格坐标、格子索引、瓦片尺寸 |
| `ForgeMap.zan` | 地图生成（按种子）、巢穴坐标 |
| `ForgeCatalog.zan` | 方块 / 配方 / 科技 / 敌人 / 波次的静态目录 |
| `ForgeBuilding.zan` `ForgeUnit.zan` `ForgeBullet.zan` `ForgeEnemy.zan` | 建筑、单位、弹道、敌人实体 |
| `ForgeFactory.zan` | 传送带物流、配方合成、电力与流体结算 |
| `ForgeCombat.zan` | 射程索敌、伤害结算、弹道与溅射 |
| `ForgeSimulation.zan` | 定步推进：生产 → 物流 → 战斗 → 波次 |
| `ForgeSave.zan` | 版本化存档（v1..v5 兼容）与校验 |
| `BlockDef.zan` `RecipeDef.zan` `TechDef.zan` `WaveDef.zan` `EnemyDef.zan` `Stock.zan` … | 静态定义与库存结构 |

## 依赖

| 依赖 | 用在哪 |
|---|---|
| `Game.Core` | 空间哈希（`SpatialHash2D`）——索敌去重 |
| `Game.Pathfinding` | `DistanceField`——敌军到炉心的加权距离场（离开代价 = 该格建筑血量加权） |

寻路本身**不在本包实现**：`Zan.Game.Pathfinding` 的距离场是通用件（共享目标的
加权梯度），这里只负责把"岩/水不可通行、建筑可通行但代价高"翻译成代价数组。
改寻路语义去改公共包，别在这里再手搓一份堆。

## 用法

```zan
using System;
using Game.Factory;

// 1. 按种子建世界
ForgeWorld world = new ForgeWorld(48, 36, seed);
world.Place(world.coreCell, BlockKind.Core, 0);

// 2. 推进模拟（定步：每次一 tick）
ForgeSimulation sim = new ForgeSimulation(seed);
sim.Step();

// 3. 存档与读档（版本化，旧档按版本分支读）
if (ForgeSave.Save(world, "save.json")) {
    ForgeWorld? loaded = ForgeSave.Load("save.json");
}
```

## 设计约束

- **零 GUI 依赖**：本包只用 `System` 与 `System.IO`/`System.Json`，不碰 `Gui`，
  所以能进无头测试、服务器与回放。
- **存档按版本分支**：`ForgeSave` 对每个版本校验键集与取值区间，旧档读入走
  版本分支而不是硬套当前结构；损坏档返回 `null` 而不是半读状态。
- **整数判定用 `JsonValue.IsInt()`**：解析出来的 JSON 数字带的是原始文本，
  直接查裸字段 `numIsInt` 会把每个整数判成非整数（写档成功、读档全失败）。

## 测试

无头逻辑套件（240 条断言，覆盖物流、配方、流体、科技、存档、战斗、波次、种子确定性）
随项目一起搬到了 `D:/project/games/coreforge/client/tests/verify.py`——它编译本包 +
测试源来跑：

```powershell
$env:ZAN_HOME = 'D:/project/zan-lang'      # 公共包在本仓库的 packages/ 下
pwsh -File D:/project/games/coreforge/client/tools/test.ps1
```

搬出前的仓库内位置在 `archive/coreforge-2026-10-20` 标签上，需要时用
`scripts/export_game_project.ps1 -SourceRef archive/coreforge-2026-10-20` 取回。
