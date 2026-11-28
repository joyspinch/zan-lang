# Zan.Game.Extraction

搜打撤（extraction shooter）玩法层：数据驱动的物品目录、网格背包与装备、
校验过的背包操作、掉落表、护甲穿透、撤离点，以及弹道/肢体/条件撤离区。
`using Game.Extraction;` 即可；只依赖 Zan.Json。渲染、网络、AI 不在本包。

## 内容

| 领域 | 类型 |
|---|---|
| 物品目录（JSON） | `ItemCatalog`（`LoadJson` / `Get` / `Make` / `AmmoFor` / `Fill`）、`ItemDef`、`ItemCategory`、`ContainerDef`、`LootTable` |
| 背包 | `ItemStack`（数量、耐久、弹匣、朝向、内部格子、是否已搜出）、`GridInventory`（放置/旋转/堆叠/取弹）、`Loadout`（6 装备槽 + 口袋 + 安全箱 + 胸挂/背包格子） |
| 操作 | `InventoryOps.Move` / `Quick` / `Detach` / `Locate`，目标编码 `InvTarget` |
| 战斗结算 | `ArmorModel.Resolve` → `ArmorHit`（防护等级 × 耐久 vs 穿透；钝伤） |
| 撤离 | `ExtractPoint`、`ExtractKind`（常开/随机/条件）、`ExtractProgress`（每人读条）；2D 条件撤离状态机 `ExtractZone` |
| 其他 | `BallisticSolver`、`LimbBody` |

所有判定都是纯数据运算：权威侧（主机/单机）执行，客户端只发操作，便于联机。

## 数据格式

```json
{
  "calibers": ["9mm", "5.8mm"],
  "items": [
    {"id": 1, "name": "手枪", "short": "P", "w": 2, "h": 1, "cat": "weapon",
     "value": 400, "weapon": 1, "sidearm": true, "mag": 13, "caliber": 0,
     "icon": "icons/1.png", "model": "service_pistol"},
    {"id": 10, "name": "9mm 普通弹", "cat": "ammo", "stack": 60, "caliber": 0, "tier": 0, "pen": 14},
    {"id": 20, "name": "警用防弹衣", "w": 3, "h": 3, "cat": "armor", "tier": 2, "durability": 45},
    {"id": 33, "name": "帆布挎包", "w": 2, "h": 2, "cat": "bag", "gridW": 3, "gridH": 3},
    {"id": 41, "name": "急救包", "h": 2, "cat": "med", "heal": 45, "stopBleed": true, "useTime": 3.5}
  ],
  "lootTables": {"tool": [[10, 10], [41, 5]]},
  "containers": [{"key": "tool", "name": "工具箱", "w": 4, "h": 2, "table": "tool", "min": 1, "max": 3, "empty": 0.12}]
}
```

- `cat`：weapon / ammo / armor / helmet / rig / bag / med / loot / key。
- `icon` 图标图片路径、`model` 3D 模型名（都可省略，由游戏决定怎么用：常见做法是
  用 `Game.Render3D.ModelThumbnail` 把模型离线烘成图标）。
- 武器 `weapon` 是游戏侧武器表索引；`sidearm` 决定能否进副武器槽。
- `lootTables` 每项 `[物品 id, 权重]`；`Fill(grid, 容器key, rng, tierBoost)` 中
  `tierBoost` 按稀有度放大权重（高价值区用）。新掉落物 `Found = false`，需要搜索。

## 测试

`tests/conformance/extraction_inventory.zan`（目录加载、放置/旋转/堆叠、槽位规则、
取弹、背包操作、固定种子掉落、护甲、撤离读条）、`game_extraction_tycoon.zan`。
