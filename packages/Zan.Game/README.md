# Zan.Game - Zan 官方游戏开发引擎与组件库

`Zan.Game` 是面向 2D 游戏、棋盘策略、卡牌对战、塔防模拟及 ARPG/RPG 开发的分层框架体系。所有模块均遵循**渲染与逻辑分离、确定性步进、快照回放**原则。

---

## 模块分层与架构

```text
Game.Arpg        (ARPG/RPG 专业运行时)
Game.Zgm         (ZanGameMaker 组件模型运行时)
  ▲
  │
Game.Board  ----+ (棋盘/战棋/网格/A*寻路/回放日志)
Game.Cards  ----+ (卡牌/牌库/手牌区/对战结算引擎)  ───► Game.Foundation (固定步长/确定性随机数/输入/场景)
Game.Arcade2D  -+ (2D碰撞/实体池/补间/几何流形)
```

---

## 各子模块文档入口

- 🎮 **基础核心**：[`src/Game/Foundation/README.md`](src/Game/Foundation/README.md) - 定步循环、随机数种子与场景栈。
- ♟️ **棋盘与战棋**：[`src/Game/Board/README.md`](src/Game/Board/README.md) - 方格/六边形网格、命令验证、快照恢复。
- 🃏 **卡牌与构筑**：[`src/Game/Cards/README.md`](src/Game/Cards/README.md) - 牌堆流转、洗牌、结算堆栈与战斗规则。
- 👾 **2D 街机与碰撞**：[`src/Game/Arcade2D/README.md`](src/Game/Arcade2D/README.md) - 实体对象池、AABB碰撞、弹道与射线检测。
- ⚔️ **动作角色扮演**：[`src/Game/Arpg/README.md`](src/Game/Arpg/README.md) - 地图、技能冷却、Buff系统与实体调度。
- 📖 **全模块总览**：[`src/Game/README.md`](src/Game/README.md) - 完整架构设计白皮书。
