zan 红警与星际 — 即时战略引擎（模板版）
====================================

玩法
----
- 左键点选单位、拖拽框选整队，右侧集结点击选目标点
- 蓝红两军编队推进，遇中央裂谷分流绕行，接触即开火
- 右下角雷达小地图常驻，矿工单位采矿换晶矿，晶矿换人口

技术要点
--------
- **流场寻路**（`Game.Tactics.FlowField`）：宏观路径按格子代价场展开，
  裂谷格子代价置 255，单位自然绕行，不必逐单位做 A*。
- **RVO 群体避障**（`Game.Tactics.RvoSimulator`）：每单位一个 RVO 槽位，
  编队推进时互相让位，不会叠在一起。
- **确定性步进**：固定步长推进模拟，帧率变化不影响战局。

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
在没有美术资源的机器上照样能跑、能看。阵营用单位脚下的环区分（蓝/红），
贴图本身不带配色。

编译
----
根目录下（公共包在 zan-lang 仓库的 packages/ 下，编译要带 --package-project）。
多文件模板要把 src/ 下的 .zan 逐个列出来——zanc 不会自动带上同目录的其它文件：

    build/zanc.exe templates/game/rts/src/main.zan ^
                   templates/game/rts/src/Entities/RtsUnit.zan ^
                   templates/game/rts/src/Views/BattlefieldView.zan ^
                   --auto-stdlib --package-project . -o build/rts/Rts.exe

仓库门禁 `scripts/gate_templates.ps1` 会自动枚举这些源文件并逐模板编译。

文件
----
- src/main.zan        入口 + 指挥台 UI + 战场渲染（素材与矢量的双路）
- src/Entities/RtsUnit.zan  单位数据（类型/阵营/半径/血量/选中态）
- src/Views/BattlefieldView.zan  战场画布控件（把 Program 的绘制挂进 GUI 树）
- assets/             美术素材
- zan.proj            IDE 项目文件（IDE「新建项目 → 游戏」可直接套用）
- template.manifest   模板清单（IDE 新建项目对话框展示）
