zan Go — 围棋（模板版）
====================================

玩法
----
- 19 路棋盘，点击落子，提子自动结算
- 形势判断 AI 对弈，支持停一手与认输
- 局终自动数子判定胜负

编译
----
根目录下（公共包在 zan-lang 仓库的 packages/ 下，编译要带 --package-project）：

    pwsh -File templates/game/weiqi/tools/build.ps1

产物在 build/weiqi/，运行所需 DLL 与 assets/ 一并落在同目录。
手动编译（同样在根目录下）：

    build/zanc.exe templates/game/weiqi/src/main.zan --auto-stdlib --package-project . -o build/weiqi/Weiqi.exe
