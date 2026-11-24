# Zan.Game.Arpg

动作角色扮演（ARPG）运行时：世界与角色、战斗与技能、装备词缀与背包、buff 与
成长、地图与传送门，以及配套的 UI 定义、富文本、补间、网络与存档。
`using Game.Arpg;` 即可；依赖 Zan.Gui、Zan.Net、Zan.Data（`ArpgDatabase` 走
Sqlite）、Zan.DataFrame 与 Zan.Text。

## 内容

| 领域 | 主要类型 |
|---|---|
| 世界与角色 | `ArpgWorld`（实时地图状态，所有玩法操作的入口；固定种子随机数保证可回放）、`ArpgActor`、`ArpgFaction`、`ArpgRegistry`、`ArpgProject` |
| 战斗与技能 | `ArpgCombat`、`ArpgFormula`、`ArpgSkillDefinition`、`ArpgSkillTarget`、`ArpgSkillCastStatus`、`ArpgCooldowns`、`ArpgBuffDefinition` |
| 装备与物品 | `ArpgEquipment`、`ArpgInventory`、`ArpgItemCategory`、`LootScatter`、`ArpgGrowthDefinition` / `ArpgGrowthLevel` |
| 地图与渲染辅助 | `ArpgMapDefinition`、`IsoTileMap`、`YSortLayer`、`ArpgPrefabDefinition` |
| 界面 | `ArpgWindow` / `ArpgWindowDefinition`、`ArpgControl`、`ArpgNode`、`ArpgMenu`、`ArpgUiRuntime`、`ArpgRichText*`、`ArpgTween`、`Fonts/ArpgPixelFont` |
| 脚本与数据 | `ArpgTemplate` / `ArpgTemplateParser`、`ArpgValue`、`ArpgGlobals`、`ArpgScheduler`、`ArpgConfig`、`ArpgDataSource`、`ArpgValidation`、`ArpgDiagnostics` |
| 事件 | `ArpgEvents`、`ArpgGameplayEvents`、`ArpgServerEvents`，以及 `Models/` 下的控件/节点/窗口事件处理表 |
| 网络 | `ArpgTcpClientRuntime`、`ArpgTcpServerRuntime`、`ArpgWebSocketRuntime`、`ArpgServerClient`、`ServerConfig`、`ArpgNetDefaults` |
| 存档 | `Data/ArpgDatabase`、`Data/ArpgSaveRepository`、`Data/ArpgSaveState` |
| 其他 | `ArpgMusic`、`ArpgMath`、`ArpgRandom`、`ArpgPoint` / `ArpgRect` / `ArpgColor` |

`Models/` 放定义与实例的数据类（角色/物品/技能/地图格/传送门/刷怪点/富文本
布局/补间步骤/网络与 Sqlite 组件配置等）。
