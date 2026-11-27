zan Roguelike Deckbuilder — 肉鸽爬塔（模板版）
==============================================

玩法
----
- 爬塔三层，每层二选一：**战斗**换牌，**宝箱**回血换牌，第 3 层是 Boss
- 战斗是卡牌构筑：每回合 3 能量，手牌上限 5，打出的牌进入弃牌堆，弃牌堆空了自动洗回抽牌堆
- 敌人每回合造成固定伤害，格挡在回合开始清零，易伤让受伤 +50%
- 每场胜利三选一加牌，牌组一路滚大；死亡或登顶即结算

操作
----
| 操作 | 说明 |
|------|------|
| 鼠标点「进入」 | 选路线节点（宝箱直接回血拿牌） |
| 鼠标点手牌 | 出牌；变暗、费用宝石转灰并标「能量不足」的牌打不出 |
| 「结束回合」 | 弃牌、敌人按意图反击、抽新牌 |
| `Esc` | 局中弹出"放弃本局？"确认条（再按一次 Esc 收起）；结算页回主菜单；不会一键退出 |
| `M` | 音效静音开关（顶栏/菜单也有按钮），偏好会记住 |

回合制，不需要暂停：局中离开窗口不会发生任何事。第一次打开（档案里还没开过局）
菜单会展开一块"新手指引"石板，之后换成"攀登要诀"。

战绩与音效
----------
- **战绩**（`GameProfile.Open("roguelike")`）：开局次数、登顶次数、到过的最高层、
  最快登顶（全局累计回合数，越少越好）。菜单页常驻显示；结算页显示本局回合数，
  刷新纪录时出现 **NEW BEST**。
- **存在哪**：用户应用数据目录下 `ZanGames/roguelike/`（macOS
  `~/Library/Application Support`、Windows `%LOCALAPPDATA%`、Linux
  `~/.local/share`）；设 `ZAN_GAME_DATA_DIR` 可改根目录。不写工作目录。
  单局进度本身不存档（肉鸽：一局一命）。
- **音效**（程序化合成，零音频文件）：出牌 select、造成伤害 hit、获得格挡
  block（本作自配的下滑闷响）、击败敌人 explode、拿奖励/开宝箱 coin、
  阵亡 fail、登顶 success、界面按钮 click、出牌失败 error。没有声卡时静默。
- **确定性**：外壳（`RogueShell`）只在界面层调用，`RunFlow` / `RoguelikeSim`
  不碰声音、档案和时钟，所以同种子同一局、`replays/bot.replay` 不受影响。

种子
----
- 顶栏和结算页都显示本局**种子**
- 同一颗种子必然给出：同一张地图、同一批敌人、同一组奖励三选一
- 种子写在菜单的输入框里，也可以点"随机种子"；结算页可"同种子再来一局"

这正是肉鸽的地基：一次种子 = 一次可复盘的局，可以分享、可以做每日挑战、
可以做"这局我能打通吗"的挑战。

编译
----
把入口和整个 `src/` 目录一起交给 zanc：

    zanc src/main.zan src --auto-stdlib --embed assets=assets -o roguelike.exe

在仓库里（带仓库内的包）：

    zanc templates/game/roguelike/src/main.zan templates/game/roguelike/src --auto-stdlib \
        --package-project . --embed templates/game/roguelike/assets=assets -o roguelike.exe

美术素材
--------
`assets/` 下是四张暗黑奇幻风格的图（统一调子，分类共享）：

| 文件 | 用途 |
|------|------|
| `assets/cards/attack.png` | 攻击牌卡面（打击 / 重击 / 汲取） |
| `assets/cards/skill.png` | 技能牌卡面（防御 / 破甲 / 治疗 / 疾风） |
| `assets/cards/power.png` | 能力牌卡面（蓄力 / 狂怒） |
| `assets/title.png` | 主菜单标题图 |

接线只有三步，模板不碰绘制：

1. `CardLibrary` 给每条 `CardDefinition` 挂 `SetArt("assets/cards/xxx.png")`；
2. `Program.ArtPath()` 用 `Game.Kit.Assets.Find` 定位文件（以 exe 目录为锚向上找 4 级）；
3. `Program.SizedImage()` 建 `Gui.Widget.Image` 控件，**用内联 CSS 声明宽高**——
   行/列容器的默认 `align-items` 是 stretch，没声明宽高的图片会被拉满整行
   （标题图曾被拉成 1920 宽的一条）。

**找不到素材就当没有美术**：定位失败返回空串，界面退回纯文字牌，游戏照常能玩。

源码分工
--------
| 文件 | 职责 |
|------|------|
| `src/main.zan` | 入口：起窗，把控制权交给 Program |
| `src/Program.zan` | 阶段状态机 + 全部界面（菜单/路线/战斗/奖励/结算）+ 快捷键与音效触发 |
| `src/RunFlow.zan` | 一局的流程与规则（无界面，回放与界面共用） |
| `src/RoguelikeSim.zan` | 无头回放/机器人模糊测试（`replays/bot.replay`） |
| `src/RogueShell.zan` | 外壳：玩家档案（战绩、静音）+ 音效库，只给界面层用 |
| `src/RogueSkin.zan` | 游戏皮肤：暗石 + 熔金 + 余烬 + 深红的 CSS 与语义类（`rg-*`），`app.UseCss` 内嵌进 exe |
| `src/RunState.zan` | 一局的状态：种子、层数、生命、牌组、当前层候选节点 |
| `src/CardLibrary.zan` | 全部牌面定义 + 起始牌组 + 奖励池 |
| `src/MapNode.zan` | 一个路线节点（普通/精英/宝箱/Boss） |

引擎要点（`Game.Cards` / `Game.Foundation`）
--------------------------------------------
- **卡牌结算全部交给引擎**：`CardBattle` 管能量、抽牌、弃牌、格挡、胜负；
  `CardDeck` 管抽牌堆/手牌/弃牌堆/消耗堆四区与弃牌堆回收；`CardDefinition`
  是共享牌面模板，`CardInstance` 是牌组里的一张具体牌（带升级位）。
- **牌面只定义一次**：`CardDefinition` 不可变，一份定义可以实例化多张；
  牌组里存的是**卡 id**（纯数据），于是"存档"只要存 id 串 + 种子 + 层数 + 生命。
- **随机只有一处**：全用 `DeterministicRandom`（`Game.Foundation`），
  地图、敌人、奖励三选一、牌堆洗牌都出自它，所以同种子必然同一局。

扩展点
------
- **加一张牌**：在 `CardLibrary.Build()` 里加一条 `CardDefinition`，再决定要不要
  放进 `rewardPool`（不放 = 只有起手有）/ 放进 `starterDeck`（= 起手就有）。
  牌面文案自动跟着效果走，不用改界面。
- **加一层**：`RunState.BuildFloor()` 里加一条 `MapNode` 分支即可，
  候选顺序由种子决定，不用额外写随机。
- **加一种敌人**：填 `MapNode` 的名字/血量/每回合伤害即可；
  想要更复杂的敌人行为，得自己接管 `CardBattle`（引擎允许替换敌方意图选择）。
- **换界面**：`Program` 的五个 `Build*` 方法各自负责一屏，全部用标准库控件
  拼装（Panel / Label / Button / InputNumber），不画布自绘。

已知简化
--------
- 敌人只有"每回合固定伤害"一种意图，没有复杂 AI 或攻击前摇
- 没有卡牌升级与遗物系统（`CardInstance` 已经预留了升级位）
- 战斗结束时的残留格挡折算进生命，不跨战斗保留
