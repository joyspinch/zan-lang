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
| 街机 Arcade | [snake](snake/) | `Game.Arcade2D` | 无限画布贪吃蛇竞技场 + AI 蛇 |
| 街机 Arcade | [pin-circle](pin-circle/) | `Game.Arcade2D` | 见缝插针：旋转插针、变速与反向回合 |
| 卡牌 Card | [roguelike](roguelike/) | `Game.Cards` + `Game.Foundation` | 肉鸽爬塔：种子决定地图/敌人/奖励三选一 |
| 策略塔防 Strategy | [rts](rts/) | `Game.Tactics` | 即时战术：流场寻路 + RVO 群体避障 + 小地图 |
| 益智解谜 Puzzle | [trap-cat](trap-cat/) | `Game.Board` | 围住神经猫：逐点封堵、撤销、最短路 AI |
| 休闲益智 Puzzle | [match3](match3/) | `Game.Match3` + `Game.Kit` | 经典三消：滑块交换、重力掉落填充、连击消除与飘字计分 |
| 动作平台 Action | [platformer](platformer/) | `Game.Arcade2D` | 2D 横版平台：零分配循环、土狼时间、跳跃缓冲 |
| 肉鸽动作 Action | [survivor](survivor/) | `Game.Kit` + `Game.Graphics` | 幸存者割草：自动瞄准弹幕、怪物蜂拥波次、深度遮挡排序、打击顿帧与震屏 |
| 3D 漫游交互 Action3D | [fps-3d](fps-3d/) | `Game.Render3D` | 3D 第一人称漫游：GPU 网格批次渲染、视锥裁剪、平滑漫游与 3D 射线拾取 |
| 放置挂机 Idle | [idle](idle/) | `Game.Idle` | 放置矿工：Gui + SpriteBatch + 弹性反馈 |

## 仓库外的大型游戏项目

产品级的游戏（几十 MB 素材、上千文件、自带工具链与服务端）**不进模板位**——
它们有自己的版本历史与发布节奏，放进仓库只会拖慢语言仓库、也会被 IDE 当成
"可新建的模板"误导用户。当前已搬出的项目：

| 项目 | 类型 | 说明 |
|---|---|---|
| `D:/project/games/legend` | 2.5D 动作 RPG + 配套服务端 | 传奇放置挂机：CSV 数据驱动、离线收益、存档；项目内 `client/` 与 `server/` 分目录，规则表单向同步 |
| `D:/project/games/wuwei` | 数据驱动修仙 RPG | 无为修仙传：水墨修仙，HTML 设计稿 + 代码隐藏 |
| `D:/project/games/coreforge` | 工厂自动化 + 塔防 | 炉心边境：传送带物流 + 熔炼链 + 20 波塔防；逻辑层在仓库的 `Zan.Game.Factory` 包里（项目通过 `$env:ZAN_HOME` 吃仓库的公共包） |
| `D:/project/games/xiangqi` | 中国象棋 | 极小化 AI、合法着提示、将帅警告；素材约 9MB |
| `D:/project/games/weiqi` | 围棋 | 19 路、目数评估 AI、提子动画、打劫 |
| `D:/project/games/gomoku` | 五子棋 | 15×15、启发式 AI、悬停预览、连珠高亮 |
| `D:/project/games/ddz` | 斗地主 | 叫地主、牌型组合出牌、牌面动画，与两家 AI 对战 |
| `D:/project/games/goldminer` | 黄金矿工 | 摆钩抓金、连击 FEVER、收线/拆弹 QTE、TNT 连锁 |
| `D:/project/games/breakout` | 打砖块 | 发光砖块、道具掉落、多球乱斗 |

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
  实例在仓库外的 `D:/project/games/coreforge`，那边只剩渲染与 HUD）。
- **共享美术层 = 跨模板通用的资源**：[`assets/game/`](../../assets/game/style.md)
  放色板、绘制规则、可平铺纹理与 UI 底板；通用纹理**拷贝**进各模板自己的
  `assets/`，拷贝是复制不是引用——这样每个模板项目仍然独立自包含、可单独发布。

判断口径：**这段代码换个游戏还成立吗？** 成立 → 进包；只对这个游戏成立 → 留模板。

## 共享什么，不共享什么（硬约束）

**共享的是机制与通用资源；不共享模板本身。**

- ✅ 共享：`packages/` 里的通用机制、`assets/game/` 里的色板/纹理/UI 底板、
  `scripts/gen_game_art.ps1` 的统一风格串。
- ❌ 不共享：**不存在"公共模板"**。每个模板是**独立项目**：自己的 `src/`、
  自己的 `assets/`、自己的入口与玩法。模板之间不许互相引用文件，
  `templates/` 下也不许出现 `_common` / `base` / `shared` 这类公共骨架目录。

为什么：模板是"怎么做一个游戏"的示范，被当成公共骨架后，每个项目都要改一遍
公共代码，改动互相打架，修复也无法回流。要复用就抽包（`packages/`），
项目里 `using` 调包，而不是"复制公共模板再改"。

这两条由门禁自动检查（`scripts/check_game_templates.py`，已接进
`scripts/gate_templates.ps1`）：

1. **资源引用完整性** —— 源码里写死的 `assets/xxx.png` 必须存在，
   否则运行时静默退化成色块；
2. **模板独立性** —— 模板之间无交叉引用、无公共模板目录；
3. **素材不是色块** —— 唯一颜色数 / 尺寸 / 体积下限。

## 美术基线

美术规范、色板与共享纹理在 [`assets/game/style.md`](../../assets/game/style.md)。
新素材用共享风格串生成（`scripts/gen_game_art.ps1` 默认自动带上），
这样不同品类的插画也成一套，但**素材本身仍是各模板自己的**——
不共用同一张图，不做公共美术模板。

## 加一个游戏模板

1. 目录 `templates/game/<名字>/`，放 `template.manifest` + `zan.proj` + `src/`
   （`zan.proj` 的 `entry` 要指向真实入口；`{{NAME}}` 会被 IDE 替换成项目名）
2. `template.manifest` 必填 `name` / `category` / `category.zh` / `type` / `entry` /
   `order`；`category` 用已有品类标签，别自造近义分类
3. `README.md` 写清玩法、编译命令、源码分工与扩展点
4. 素材：按 `assets/game/style.md` 的规则生成真美术（不是色块），
   放自己的 `assets/`；通用纹理从 `assets/game/textures/` 拷一份进来
5. 跑门禁：`scripts/gate_templates.ps1 -Filter game/<名字>`
