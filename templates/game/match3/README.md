# Match-3 Puzzle 模板

基于 Zan.Game 三消专用底层 `Match3Board` 驱动的经典消除游戏模板。

## 特性

- **开箱即用规则**：相邻合法性验证（`ValidateSwap`）、三连以上消除扫描、重力掉落与连击级联结算（`ResolveCascades`）。目标：25 步内拿到 1500 分。
- **完整游戏流程**：开始界面（玩法说明 + 最高分/局数，点任意处开始）→ 对局 → 暂停（继续 / 重开 / 退出）→ 结算面板（过关 / 步数用尽、得分对目标、最高分、NEW BEST、再来一局 / 回菜单），不用重启程序即可连续游玩。
- **音效**：程序化合成（`SfxBank` + `SfxPresets`，零音频文件）——选中、交换、无效交换报错、消除金币声（随连击层级升调）、大消除/连锁、过关、失败、界面点击；`M` 静音并记住。
- **存档**：`GameProfile.Open("match3")` 记录最高分（`score`）、完成局数（`plays`）、过关次数（`clears`）与静音开关（`muted`）。
- **动效与手感**：`FloatingTextPool` 消除加分飘字；无效交换两格红框闪烁；剩余 5 步以内步数变红；首局显示操作提示。
- **界面全部用标准控件**：Panel / Label / Button 在 `Match3Hud.Css()` 里按宝石夜色调换肤，没有 Canvas 自绘控件。
- **高拓展性**：开发者只需替换 `gemColors` 或替换为贴图素材，即可快速制作各类商业消除游戏。

## 操作

| 按键 / 操作 | 作用 |
|---|---|
| 鼠标点一颗宝石，再点相邻宝石 | 交换（必须能连成 3 个以上，否则红框提示、不扣步数） |
| 开始界面点任意处 / `Enter` / `Space` | 开始游戏 |
| `P` / `Esc` | 对局中暂停 / 继续（暂停面板可重开或退出） |
| `M` 或右上角 SOUND 按钮 | 音效开关（持久保存） |
| 结算面板 `Enter` | 再来一局 |
| 结算面板 `Esc` | 回开始界面 |
| 开始界面 `Esc` | 退出游戏 |

## 存档位置

存档写在用户应用数据目录，不写工作目录：

- macOS：`~/Library/Application Support/ZanGames/match3/`
- Windows：`%LOCALAPPDATA%\ZanGames\match3\`
- Linux：`~/.local/share/ZanGames/match3/`

设置环境变量 `ZAN_GAME_DATA_DIR` 可改根目录（测试/便携版）。

## 回放与确定性

`src/Match3Sim.zan` + `replays/bot.replay` 是无头回归：仿真只驱动 `Match3Game`，不碰音频、存档、时钟与窗口。
音效与存档全部在界面层（`Match3Hud` / `main.zan`）里，读 `Match3Game.lastAction/lastCleared/lastCombos` 决定播什么。

## 运行与编译

```bash
zanc src/main.zan --auto-stdlib --package-project <zan-lang-root> -o match3.exe
./match3.exe
```
