zan Dou Dizhu — 斗地主（模板版）
====================================

玩法
----
- 与两家 AI 叫地主、抢地主
- 点击选牌，支持单张/对子/三带/顺子/炸弹等牌型
- 出完手牌即获胜，动画发牌与结算

编译
----
根目录下（公共包在 zan-lang 仓库的 packages/ 下，编译要带 --package-project）：

    pwsh -File templates/game/ddz/tools/build.ps1

产物在 build/ddz/，运行所需 DLL 与 assets/ 一并落在同目录。
手动编译（同样在根目录下）：

    build/zanc.exe templates/game/ddz/src/main.zan --auto-stdlib --package-project . -o build/ddz/Ddz.exe
