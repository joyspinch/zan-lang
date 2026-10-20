zan Gomoku — 五子棋（模板版）
====================================

玩法
----
- 点击落子，先连成五子者胜
- 启发式 AI 对弈，悬停显示落子预览
- R 重新开局，Esc 退出

编译
----
根目录下（公共包在 zan-lang 仓库的 packages/ 下，编译要带 --package-project）：

    pwsh -File templates/game/gomoku/tools/build.ps1

产物在 build/gomoku/，运行所需 DLL 与 assets/ 一并落在同目录。
手动编译（同样在根目录下）：

    build/zanc.exe templates/game/gomoku/src/main.zan --auto-stdlib --package-project . -o build/gomoku/Gomoku.exe
