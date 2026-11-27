# 围住神经猫

一个回合制小游戏：点击浅绿色圆点封路，每封一个点，猫沿最短逃生路线走一步。猫到达棋盘边缘就逃脱；切断所有通向边缘的路线即可获胜，即使猫身边仍有空点。

- 9×9 错列圆点棋盘，每点有六个相邻方向。
- 棋盘铺一张薄荷绿毡布底图（`assets/cell.png`），圆点改成半透明压在上面——底图是模板自带的美术，缺图时自动藏掉，圆点照旧。`assets/` 里另有 `icon.png`（模板图标）和 `cat.png`（小猫原画参考；运行时小猫由 SVG 绘制，不读这张图）。
- 轻松、经典、挑战分别预置 18、12、6 个随机障碍。开局保留猫身边的六个点，并保证存在出口。
- 悔一步同时撤销封路和猫的移动，胜负结束后也可以悔棋。
- 点击猫、已封点或结束后的棋盘不会消耗回合。“再来一局”保留当前难度。
- 棋盘和菜单使用标准 GUI 控件与 HTML/CSS，适配窄窗口。
- 小猫由 SVG 绘制：待机时摇身、举爪、踏步、甩尾和眨眼；走格时播放 240 毫秒的小跳，被围住时闭眼坐下。连续动画跟随显示节奏，Android 使用显示帧回调；绘制范围覆盖精灵当前及上一位置，姿势缓存只保留当前状态和尺寸。
- **纪录**：每个难度分别记胜场、当前连胜/最佳连胜、最少几步围住猫，显示在步数左侧；刷新最少步数（或最佳连胜 ≥2）时结果区出现 NEW BEST。每局只结算一次——胜负后悔棋可以复盘，但不会重复计分；已经落子却没分胜负就换局/换难度算放弃，连胜清零。
- **音效**（程序化合成，无音频文件）：封路、点到已封点或猫（无效操作提示）、猫跳一格、围住、逃脱、悔棋、开新局各有一声。没有音频设备时静默。
- **静音**：按 `M` 或点难度按钮右侧的“音效 开/关”，状态随档案保存，下次启动沿用。
- 小跳期间仍接受下一次封路，精灵从当前视觉位置继续移动，快速点击不会被动画吞掉。

| 操作 | 按键/点击 |
|---|---|
| 封住一个圆点 | 点击浅绿圆点 |
| 悔一步 | “悔一步”按钮 |
| 再来一局 / 换难度 | “再来一局”、轻松/经典/挑战 |
| 静音开关 | `M` 或“音效”按钮 |

回合制，没有暂停（不需要：猫只在你落子后才动）。

**存档位置**：`Game.Foundation.GameProfile.Open("trap-cat")`，即用户应用数据目录下的
`ZanGames/trap-cat/`（macOS `~/Library/Application Support`、Windows `%LOCALAPPDATA%`、Linux
`~/.local/share`）；设 `ZAN_GAME_DATA_DIR` 可改根目录。不写工作目录。UI 自动化脚本
（`ZAN_UI_SCRIPT`）在未指定 `ZAN_GAME_DATA_DIR` 时使用内存档案，回归不会改写玩家纪录。

在 ZanIDE 的“新建项目 → 游戏 → 围住神经猫”中创建项目后运行。项目配置不固定操作系统目标。

生成项目中的命令行构建：

```powershell
New-Item -ItemType Directory -Force build | Out-Null
zanc src/{{NAME}}.zan src --auto-stdlib -o build/{{NAME}}.exe
.\build\{{NAME}}.exe
```

Android ARM64 APK 构建（需要 Android SDK、NDK 和 JDK）：

```powershell
zanc src/{{NAME}}.zan src --auto-stdlib --publish --embed src/TrapCat.html=src/TrapCat.html --target android-arm64 --emit-apk build/{{NAME}}.apk --apk-package com.zan.trapcat --apk-label "围住神经猫" -o build/{{NAME}}.so
```

Android 使用竖屏实际客户区，隐藏桌面标题栏；字体跟随设备 DPI，棋盘按屏幕逻辑宽高切换尺寸。`Form.Create` 的 600×760 用于桌面初始尺寸和 Android 竖屏方向，不会把手机表面限制为 600×760。APK 使用工具链自动生成的调试签名。

入口 `.zan` 排在首位，`src` 递归纳入其余 Zan 源码。`TrapCat.html` 在开发时从项目目录读取，发布时以程序读取的逻辑名 `src/TrapCat.html` 内嵌。

**底图怎么接的**（`src/App.zan` 的 `SetupBoardArt`）：`assets/cell.png` 用
`Game.Kit.Assets.Find` 定位（以 exe 目录为锚向上找 4 级），找到就写进
`<img id="BoardBg">` 的 `Src`，找不到就把这个 `img` 藏掉。**引擎的 CSS 背景
不支持 `url()`**（只有颜色与线性渐变），所以底图只能用 `<img>` 元素叠在
棋盘底下，不能写 `background-image`。

`src/TrapCatBoard.zan` 只负责规则、BFS 寻路与撤销记录；`src/App.zan` 在生成时改名为项目名，负责事件与状态显示；`src/TrapCat.html` 负责布局和样式；`src/TrapCatSprite.zan` 负责小猫的矢量造型和动画。等长出口采用随机选择，悔棋后重复同一步会保持猫的走法。

可通过环境变量 `ZAN_TRAP_CAT_SEED` 指定整数种子，复现随机开局。仓库内规则测试在 `tests/templates/trap-cat/board_test.zan`，GUI 点击回归在同目录的 `gameplay.ui`；模板创建、编译与发布验收使用：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/e2e_pipeline.ps1 -Only trap-cat
```
