zan Legend Idle — 传奇放置挂机（模板版）
====================================

玩法
----
- 自动战斗、自动拾取，装备分品级
- 世界频道广播战况，离线收益结算
- 无头自测：LEGEND_SIM=1（平衡）、LEGEND_TOUR=1（面板巡游）、
  LEGEND_SHOT=1（截图模式）

架构
----
三文件拆分：src/Data.zan（数值与表）、src/Game.zan（战斗与状态机）、
src/main.zan（Host 循环与全部渲染）。完整设计见 DESIGN.md。

编译
----
    zanc src/main.zan src/Game.zan src/Data.zan --auto-stdlib -o legend.exe
