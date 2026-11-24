# Zan.Game.Cards

卡牌游戏机制：牌堆与区域、卡牌定义与效果、构筑战斗、中式牌型识别、手牌
排布与动作演播。`using Game.Cards;` 即可；依赖 Zan.Game（`Game.Foundation`）。

## 内容

| 类型 | 作用 |
|---|---|
| `CardDefinition` / `CardCatalog` | 卡牌模板（id/名称/费用/分类 + 效果链）与按 id 唯一的注册表 |
| `CardEffectKind` / `CardTarget` | 效果类型与目标常量 |
| `CardZone` / `CardDeck` | 有序卡牌区域；抽牌堆/手牌/弃牌堆/消耗堆三区运行时，抽空自动回洗 |
| `CardBattle` | 肉鸽构筑类战斗运行时，敌方意图选择可替换 |
| `ComboType` / `ComboRuleSet` / `ComboRules` / `Combo` | 斗地主系牌型：识别、比较、候选枚举；规则集可配（默认 54 张中式规则） |
| `HandFanLayout` | 手牌按张数自适应的扇形排布 |
| `CardZoneManager` | 卡牌容器与拖拽磁吸判定 |
| `ActionQueue` | 动作演播队列，把回合逻辑与动画节奏解耦 |
| `Models/` | `CardInstance`、`CardEffect`、`CardActor`、`CardPlayResult`、`GameAction`、`ActionKind`、`BoardSlot` |
