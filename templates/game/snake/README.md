zan Snake Party — 蛇蛇乐（模板版）
====================================

玩法
----
- 鼠标控制方向，吞豆成长，撞到对手身体即出局
- 按住左键/空格冲刺（纯加速），猎杀 AI 蛇得分；分数越高猎手越凶（拦截、横切、提速）
- 吃豆积攒狂热槽，狂热时刻双倍得分
- F11 全屏，Esc 退出

编译
----
在当前主仓根目录运行以下 PowerShell 命令，直接编译模板原始源码，不复制模板树：

```powershell
Set-Location D:/project/zan-lang
& ./scripts/build_gate.ps1 -Name build -Script {
    build/zanc.exe templates/game/snake/src/main.zan templates/game/snake/src --auto-stdlib --package-project . --embed templates/game/snake/assets=assets --publish --subsystem windows -o build/snake.exe
}
build/snake.exe
```

首个参数指定 `Main` 所在文件，随后 `src` 目录递归纳入游戏、Entities、Scenes 和 HUD 的全部 `.zan`，已列入口不会重复纳入。`--package-project .` 使用当前主仓的 `packages/`。`--embed 磁盘路径=内嵌前缀` 将图片和 `assets/scenes/*.html` 一起内嵌到 `assets/`；HTML 是运行期资源，不作为编译输入。生成的原生驱动文件应与程序保持在同一目录。
