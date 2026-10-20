# templates/game — 游戏模板目录

这里是**真游戏**的模板库：每个目录一个可直接新建项目的完整游戏。
非游戏的演示（能力样板、架构范式）请放 `templates/gui/` 或 `templates/server/`。

模板目录约定：**固定两层** `templates/<族>/<模板>/template.manifest`。
IDE 的"新建项目"与模板门禁都按两层扫描（`LoadDiskTemplates` / `run_templates.cmake`），
再往里塞一层就会既不被 IDE 收录、也不进门禁。

## 品类一览

分类标签写在各模板的 `template.manifest` 里（`category` / `category.zh`），
IDE 的"新建项目"按这个标签分组。

| 品类 | 模板 | 引擎模块 | 说明 |
|------|------|----------|------|
| 街机 Arcade | [breakout](breakout/) | `Game.Arcade2D` | 打砖块：发光砖块、道具掉落、多球 |
| 街机 Arcade | [snake](snake/) | `Game.Arcade2D` | 无限画布贪吃蛇竞技场 + AI 蛇 |
| 街机 Arcade | [goldminer](goldminer/) | `Game.Arcade2D` | 黄金矿工：摆钩抓金、连锁与 QTE |
| 街机 Arcade | [pin-circle](pin-circle/) | `Game.Arcade2D` | 见缝插针：旋转插针、变速与反向回合 |
| 棋类 Board | [gomoku](gomoku/) | `Game.Board` | 15×15 五子棋：启发式 AI、悬停预览、胜利高亮 |
| 棋类 Board | [weiqi](weiqi/) | `Game.Board` | 19×19 围棋：目数评估 AI、提子动画、打劫 |
| 棋类 Board | [xiangqi](xiangqi/) | `Game.Board` | 中国象棋：极小化 AI、合法着提示、将帅警告 |
| 卡牌 Card | [ddz](ddz/) | `Game.Cards` | 斗地主：叫地主、组合出牌、牌面动画 |
| 卡牌 Card | [roguelike](roguelike/) | `Game.Cards` + `Game.Foundation` | 肉鸽爬塔：种子决定地图/敌人/奖励三选一 |
| 策略塔防 Strategy | [rts](rts/) | `Game.Tactics` | 即时战术：流场寻路 + RVO 群体避障 + 小地图 |
| 策略塔防 Strategy | [coreforge](coreforge/) | `Zan.Game.Factory` + `Game.Core` | 炉心边境：传送带工厂自动化 + 20 波塔防（逻辑层已抽成包） |
| 益智解谜 Puzzle | [trap-cat](trap-cat/) | `Game.Board` | 围住神经猫：逐点封堵、撤销、最短路 AI |
| 动作平台 Action | [platformer](platformer/) | `Game.Arcade2D` | 2D 横版平台：零分配循环、土狼时间、跳跃缓冲 |
| 放置挂机 Idle | [idle](idle/) | `Game.Idle` | 放置矿工：Gui + SpriteBatch + 弹性反馈 |

## 仓库外的大型游戏项目

产品级的游戏（几十 MB 素材、上千文件、自带工具链与服务端）**不进模板位**——
它们有自己的版本历史与发布节奏，放进仓库只会拖慢语言仓库、也会被 IDE 当成
"可新建的模板"误导用户。当前已搬出的项目：

| 项目 | 类型 | 说明 |
|---|---|---|
| `D:/project/games/legend` | 2.5D 动作 RPG + 配套服务端 | 传奇放置挂机：CSV 数据驱动、离线收益、存档；项目内 `client/` 与 `server/` 分目录，规则表单向同步 |
| `D:/project/games/wuwei` | 数据驱动修仙 RPG | 无为修仙传：水墨修仙，HTML 设计稿 + 代码隐藏 |

搬迁用 [`scripts/export_game_project.ps1`](../../scripts/export_game_project.ps1)
（复制 + 改写仓库路径引用 + 退模板身份 + `git init`）。仓库里若还需要留一份
"示例级"的同类玩法，就另做一个精简模板，而不是把产品级项目留在模板位。

## 引擎模块在哪

游戏引擎在 `packages/Zan.Game`（按品类分模块）：

| 模块 | 职责 | 谁在用 |
|------|------|--------|
| `Game.Core` | Scene / Entity / GameClock / Camera2D / SceneManager | 全品类 |
| `Game.Foundation` | 确定性随机、对象池、Tween、输入映射、场景路由 | 肉鸽、放置 |
| `Game.Arcade2D` | 几何、碰撞、动画、实体池 | 街机、平台 |
| `Game.Board` | 网格、回合、命令校验、快照 | 棋类、解谜 |
| `Game.Cards` | 牌目录、三区牌堆、战斗结算、演播队列 | 斗地主、肉鸽 |
| `Game.Tactics` | 流场寻路、RVO 避障、炮塔、波次、弹道池 | 塔防、RTS |
| `Game.Idle` | 大数、飘字池、离线收益、钱包、放置状态机（IdleState/IdleBuilding） | 放置 |
| `Game.Arpg` | 地图、战斗、装备、成长、存档、服务器 | 传奇、无为 |

`packages/Zan.Game/src/Game/README.md` 有模块全景图。

## 模板与包的边界

- **模板 = 一个游戏怎么做**：玩法规则、界面、入口、资源。
- **包 = 一类游戏共用的机制**：与具体玩法/美术无关的通用子系统。
  已经抽出去的：`Zan.Game.Factory`（工厂自动化 + 塔防的物流/配方/科技/波次/存档，
  模板 `coreforge` 只剩渲染与 HUD）。

判断口径：**这段代码换个游戏还成立吗？** 成立 → 进包；只对这个游戏成立 → 留模板。

## 加一个游戏模板

1. 目录 `templates/game/<名字>/`，放 `template.manifest` + `zan.proj` + `src/`
   （`zan.proj` 的 `entry` 要指向真实入口；`{{NAME}}` 会被 IDE 替换成项目名）
2. `template.manifest` 必填 `name` / `category` / `category.zh` / `type` / `entry` /
   `order`；`category` 用已有品类标签，别自造近义分类
3. `README.md` 写清玩法、编译命令、源码分工与扩展点
4. 跑门禁：`scripts/gate_templates.ps1 -Filter game/<名字>`
