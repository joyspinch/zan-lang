{{NAME}} — 即时战略引擎（模板版）
====================================

玩法
----
- 开局是**作战简报**：目标是 3 分钟内歼灭裂谷右岸的全部红军守备部队。
- 蓝军（玩家）在左岸，红军守右岸：发现靠近的蓝军就迎击，但不越过裂谷右沿
  （画面上的红色防线），脱战后回驻防点。双方单位自动向射程内最近的敌人开火
  （步兵近射程速射，坦克远射程重炮）。
- 征召装甲营（10 辆坦克）花 400 晶矿；每击毁一个敌军单位得 20 晶矿赏金。
  兵力不足硬冲峡口会全军覆没——先征召，再集中突破。
- 红军全灭 = 胜利；蓝军全灭或时限到 = 失败。战报显示用时、我军损失、歼敌数、
  最快胜利与胜场；刷新最快胜利时标出“新纪录”。

操作
----

| 输入 | 作用 |
|---|---|
| 左键点击单位 | 点选（点空地清空选择） |
| 左键拖拽 | 框选整队；按住 Shift 追加到现有选择 |
| A | 全选我军 |
| 右键地面 | 选中单位向该点行军（没选中单位会响“无效”提示音） |
| ➕ 装甲营 按钮 | 征召 10 辆坦克（晶矿不足 / 人口满时按钮置灰） |
| P / Esc | 暂停 / 继续（窗口失焦时自动暂停） |
| R | 暂停或战报中重新开始 |
| Enter / Space | 简报里开始作战、战报里再来一局、暂停中继续 |
| M | 静音开关（顶栏也有按钮） |

暂停弹层有“继续 / 重新开始 / 退出”；Esc 不再直接退出游戏。

音效与存档
----------
- 音效由 `Game.Foundation.SfxBank` 程序化合成（不带音频文件）：点选 `select`、
  下达行军 `click`、开火 `hit`（压低音量并限频，一帧几十发也只响一下）、单位
  被毁 `explode`、胜利 `success`、失败 `fail`、无效操作 `error`。
- 档案用 `GameProfile.Open("rts")`，存在用户应用数据目录下的
  `ZanGames/rts/`（macOS `~/Library/Application Support`、Windows
  `%LOCALAPPDATA%`、Linux `~/.local/share`），**从不写工作目录**；设
  `ZAN_GAME_DATA_DIR` 可改根目录（测试/便携版）。内容：最快胜利
  `fastest_win_ms`、局数 `plays`、胜场 `wins`、静音偏好 `muted`。
- 第一次玩（`plays == 0`）简报里多一行新兵提示。

技术要点
--------
- **流场寻路**（`Game.Tactics.FlowField`）：宏观路径按格子代价场展开，
  裂谷格子代价置 255，单位自然绕行，不必逐单位做 A*。每道行军令一张流场，
  先派出的分队不会因为后一道命令改道。
- **RVO 群体避障**（`Game.Tactics.RvoSimulator`）：每单位一个 RVO 槽位，
  编队推进时互相让位，不会叠在一起；阵亡单位退还槽位；战场边缘是墙。
- **确定性步进**：固定 50ms 一帧推进模拟，帧率变化不影响战局；暂停不走表，
  时限按仿真帧计。
- **逻辑与表现分离**：`RtsBattle` 只记录本帧射击/阵亡事件，曳光、爆炸与音效
  都在界面层（`Views/BattleFx.zan`、`main.zan`）。

回放与回归
----------
`src/RtsSim.zan` 把 `RtsBattle` 包成无头仿真（输入编码见文件头：右键行军、
征召、全选、框选），`[Invariant]` 钉住规则（人口上限、伤亡账平、阵亡当帧
出场、单位不出界、晶矿非负、每个单位有避障槽位）；`replays/bot.replay` 是
机器人录下的一局。改了规则（数值、AI、寻路参数）回放会分叉，按
`Game.Foundation` README「Replays are regression tests」重录：

    ReplayResult r = Replay.Fuzz(new RtsSim(), "RtsSim", 20261125, 1200, RtsSim.Bot, 0);
    if (r.ok) { r.log.Save("replays/bot.replay"); }

美术
----
`assets/` 下是随模板分发的美术素材：

| 文件 | 用途 |
|---|---|
| `bg_field.jpg` | 战场底图（暗色科幻战术地形） |
| `unit_infantry.png` | 步兵（俯视） |
| `unit_tank.png` | 坦克（俯视） |
| `unit_miner.png` | 采矿车（俯视） |

素材用 `Game.Kit.Assets.Find` 定位（相对运行目录找不到就退回 exe 旁与
内嵌资源），**找不到素材时整套渲染自动退回矢量图形**——所以这份模板
在没有美术资源的机器上照样能跑、能看。阵营用单位脚下的环区分（蓝/红，
红军多一层底色），血条我军绿、敌军红；贴图本身不带配色。

界面全部是标准 Gui 控件（顶栏 Label/Button、简报/暂停/战报是标准 `Modal`），
走 graphite 皮肤与语义类（yellow/cyan/green/red），战场画布只画战场本身。

编译
----
根目录下（公共包在 zan-lang 仓库的 packages/ 下，编译要带 --package-project）。
传 `src` 目录即可带上其下全部 .zan：

    build/zanc templates/game/rts/src/main.zan templates/game/rts/src ^
               --auto-stdlib --package-project . ^
               --embed templates/game/rts/assets=assets -o build/rts/Rts.exe

仓库门禁 `scripts/gate_templates.ps1` 会自动枚举这些源文件并逐模板编译。

自动化：模板调用了 `UiDriver.Enable`，`ZAN_UI_SCRIPT` 会话里可读探针
`rts.state`（0 简报 / 1 交战 / 2 暂停 / 3 战报）、`rts.ticks`、`rts.selected`、
`rts.blue`、`rts.red`、`rts.muted`、`rts.focused`；脚本会话不跟随桌面焦点自动暂停。

文件
----
- src/main.zan                    入口 + 对局流程（简报/交战/暂停/战报）+ 顶栏 HUD + 战场渲染
- src/RtsBattle.zan               战场仿真：两军、行军令、交火、伤亡、胜负判定（无界面）
- src/RtsSim.zan                  无头回放仿真 + 规则不变量 + 模糊测试机器人
- src/Entities/RtsUnit.zan        单位数据（类型/阵营/半径/血量/火力/行军令/选中态）
- src/Views/BattlefieldView.zan   战场画布控件（绘制 + 点选/框选/右键行军手势）
- src/Views/BattleFx.zan          曳光与爆炸特效（纯表现层）
- src/Views/RtsModal.zan          流程弹层（标准 Modal，按键转给全局快捷键）
- src/Views/RtsRoot.zan           窗口根容器（全局快捷键冒泡入口）
- assets/                         美术素材
- replays/bot.replay              机器人回放（zan-check 重放比对）
- zan.proj                        IDE 项目文件（IDE「新建项目 → 游戏」可直接套用）
- template.manifest               模板清单（IDE 新建项目对话框展示）
