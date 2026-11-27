# Survivor Roguelite 割草模板

基于 Zan.Game 原生底层（深度遮挡队列、弹幕打击感顿帧、震屏与浮动伤害跳字）驱动的现代吸血鬼幸存者肉鸽割草模板。

## 特性

- **手感与视觉反馈**：
  - `GameFeel.ScreenShake`：大范围击杀/受击多阶创伤衰减震屏。
  - `GameFeel.HitStop`：暴击入肉微秒级冻结顿帧，打造强烈打击阻尼感。
  - `FloatingTextPool`：零分配伤害数字抛物线跳字与暴击弹性缩放。
  - `YSortQueue`：2.5D 零 GC 原地排序深度遮挡队列，角色与怪物前后穿插自然无遮挡撕裂。
- **开箱即用玩法闭环**：
  - WASD / 方向键平滑移动（同时按住相反方向时松开一个仍按另一个走）。
  - 自动索敌连射武器；升级后射速 +12%（下限 0.12 秒一发）。
  - 边缘波次怪物潮蜂拥，随局时加密（0.8 秒→最快 0.22 秒一只）、血量每 20 秒成长。
  - 怪物贴身造成 10 点接触伤害，之后 0.6 秒无敌（角色闪红）；血量归零即阵亡。
  - 经验宝石磁吸掉落与升级强化（回满血、最大生命 +20）。
- **完整的游戏外壳**（`main.zan` 的 `SurvivorLoop` + `SurvivorMenu.zan`）：
  - 标题页：操作说明、最好成绩、开始 / 退出。
  - 暂停：`P` / `Esc` 切换，窗口失焦或最小化时自动暂停；面板含继续 / 重开 / 回到标题。
  - 阵亡结算：存活时间、击杀、等级、历史最好、`NEW BEST` 提示、再来一局。
  - 首局开头 8 秒在底部显示操作提示。
  - 面板全部是标准 `Label` / `Button`，由 `SurvivorMenu.Css()` 换成本作的深色铁框 + 琥珀/青色皮肤。
- **音效**（`Game.Foundation.SfxBank` 程序化合成，无音频设备时静默）：射击（很轻、限频）、
  命中、击杀、宝石拾取、升级、受伤、阵亡、界面点击；`M` 静音并记入存档。

## 操作

| 按键 | 作用 |
|---|---|
| `W A S D` / 方向键 | 移动 |
| （自动） | 武器自动瞄准最近的敌人开火 |
| `Enter` / `Space` | 标题页开始；结算页再来一局 |
| `P` / `Esc` | 对局中暂停 / 继续 |
| `R` | 暂停或结算时重开一局 |
| `Q` | 暂停时回到标题 |
| `Esc` | 结算页回到标题；标题页退出游戏 |
| `M` | 音效开关（记住设置） |

## 存档

`GameProfile.Open("survivor")` 写在用户数据目录，不写工作目录：
macOS `~/Library/Application Support/ZanGames/survivor/`、Windows `%LOCALAPPDATA%\ZanGames\survivor\`、
Linux `~/.local/share/ZanGames/survivor/`；设 `ZAN_GAME_DATA_DIR` 可改根目录（测试/便携版）。
记录：最好存活秒数 `time`、最多击杀 `kills`、最高等级 `level`，计数 `plays`、`kills_total`，开关 `muted`。

## 回放与确定性

`src/SurvivorSim.zan` 以固定 1/60 秒一帧无头推进 `SurvivorGame`（宿主同样用 16ms 固定步长），
`replays/bot.replay` 由 `zan-check` 重放比对。存档、音效、窗口只在 `main.zan` 外壳层：
逻辑层只累加事件计数（`shotCount`、`hitCount`、`gemCount`、`hurtCount`），外壳逐帧比对差值播放音效。
改了数值或刷怪后按 `Game.Foundation` README「Replays are regression tests」重录回放。

## 运行与编译

在当前主仓根目录运行以下 PowerShell 命令，直接编译模板原始源码，不复制模板树：

```powershell
Set-Location D:/project/zan-lang
& ./scripts/build_gate.ps1 -Name build -Script {
    build/zanc.exe templates/game/survivor/src/main.zan templates/game/survivor/src --auto-stdlib --package-project . --publish --subsystem windows -o build/survivor.exe
}
build/survivor.exe
```

`main.zan` 必须排在首位，随后 `src` 目录递归纳入 `SurvivorGame.zan`、`SurvivorHud.zan`、`SurvivorMenu.zan` 等全部源码，已列入口不会重复纳入。`--package-project .` 使用当前主仓的 `packages/`；当前模板没有需要额外内嵌的素材目录。生成的原生驱动文件应与程序保持在同一目录。
