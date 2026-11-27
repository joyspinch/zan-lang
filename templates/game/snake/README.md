zan Snake Party — 蛇蛇乐（模板版）
====================================

玩法
----
- 鼠标控制方向，吞豆成长，撞到对手身体即出局
- 按住左键/空格冲刺（纯加速），猎杀 AI 蛇得分；分数越高猎手越凶（拦截、横切、提速）
- 吃豆积攒狂热槽，狂热时刻双倍得分
- 主菜单 → 对局 → 结算（显示本局得分、最高分、NEW BEST），回车/空格直接再来一局
- 暂停面板（继续 / 重新开局 / 回到菜单）；窗口失焦或最小化时自动暂停
- 程序化音效（无音频素材）：吞豆、狂热开启、冲刺、击败 AI 蛇、阵亡、按钮点击、新纪录
- 首局自动显示完整操作说明，之后开局短暂提示暂停/静音键

操作
----

| 按键 | 作用 |
|---|---|
| 鼠标 | 转向（蛇头追随光标方向） |
| 左键 / 空格（按住） | 冲刺 |
| P / Esc | 对局中暂停；暂停面板里继续 |
| R | 暂停面板里重新开局 |
| M | 音效开关（任意场景，记入档案） |
| 回车 / 空格 | 菜单开始、结算再来一局 |
| Esc | 结算回菜单；主菜单里退出游戏 |
| F11 | 全屏 |

存档
----
档案由 `Game.Foundation.GameProfile.Open("snake")` 管理（`src/Entities/Profile.zan` 是薄适配层），
写在用户应用数据目录下的 `ZanGames/snake/`（macOS `~/Library/Application Support`、
Windows `%LOCALAPPDATA%`、Linux `~/.local/share`），原子写 + `.bak` 回退；设置环境变量
`ZAN_GAME_DATA_DIR` 可改根目录（测试/便携版）。记录：最高分 `score`、最长长度 `length`、
总局数 `games`、累计击败 `kills`，以及静音偏好 `muted`。不再写工作目录（旧版的
`snake_save.txt` 不会被读取）。无头回放（`SnakeSim`）用内存档案，不碰文件、音频与窗口。

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
