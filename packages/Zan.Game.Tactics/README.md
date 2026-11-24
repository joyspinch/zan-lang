# Zan.Game.Tactics

大规模单位与塔防机制：流场寻路、RVO 互斥避障、塔索敌、波次与子弹池。
`using Game.Tactics;` 即可；依赖 Zan.Game（`Game.Core`）。

## 内容

| 类型 | 作用 |
|---|---|
| `FlowField` | 流场寻路：一次求场，海量单位按场方向移动，避免逐单位寻路 |
| `RvoSimulator` | RVO2 / ORCA 互斥速度障碍避障，集群移动互不穿插 |
| `WaveSpawner` | 波次控制：定时刷怪、难度递增、通关奖励触发 |
| `Tower` | 防御塔与索敌策略（`TargetPriority`） |
| `BulletPool` | 子弹对象池（`BulletType`） |
