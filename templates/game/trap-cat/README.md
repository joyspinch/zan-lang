# 围住神经猫

一个回合制小游戏：点击浅绿色圆点封路，每封一个点，猫沿最短逃生路线走一步。猫到达棋盘边缘就逃脱；切断所有通向边缘的路线即可获胜，即使猫身边仍有空点。

- 9×9 错列圆点棋盘，每点有六个相邻方向。
- 棋盘铺一张薄荷绿毡布底图（`assets/board.jpg`），圆点改成半透明压在上面——底图是模板自带的美术，缺图时自动藏掉，圆点照旧。
- 轻松、经典、挑战分别预置 18、12、6 个随机障碍。开局保留猫身边的六个点，并保证存在出口。
- 悔一步同时撤销封路和猫的移动，胜负结束后也可以悔棋。
- 点击猫、已封点或结束后的棋盘不会消耗回合。“再来一局”保留当前难度。
- 棋盘和菜单使用标准 GUI 控件与 HTML/CSS，适配窄窗口。
- 小猫由 SVG 绘制：待机时摇身、举爪、踏步、甩尾和眨眼；走格时播放 240 毫秒的小跳，被围住时闭眼坐下。连续动画跟随显示节奏，Android 使用显示帧回调；绘制范围覆盖精灵当前及上一位置，姿势缓存只保留当前状态和尺寸。
- 小跳期间仍接受下一次封路，精灵从当前视觉位置继续移动，快速点击不会被动画吞掉。

在 ZanIDE 的“新建项目 → 游戏 → 围住神经猫”中创建项目后运行。项目配置不固定操作系统目标。

生成项目中的命令行构建：

```powershell
New-Item -ItemType Directory -Force build | Out-Null
zanc src/TrapCat.html src/{{NAME}}.zan src/TrapCatBoard.zan src/TrapCatSprite.zan --auto-stdlib -o build/{{NAME}}.exe
.\build\{{NAME}}.exe
```

Android ARM64 APK 构建（需要 Android SDK、NDK 和 JDK）：

```powershell
zanc src/TrapCat.html src/{{NAME}}.zan src/TrapCatBoard.zan src/TrapCatSprite.zan --auto-stdlib --publish --target android-arm64 --emit-apk build/{{NAME}}.apk --apk-package com.zan.trapcat --apk-label "围住神经猫" -o build/{{NAME}}.so
```

Android 使用竖屏实际客户区，隐藏桌面标题栏；字体跟随设备 DPI，棋盘按屏幕逻辑宽高切换尺寸。`Form.Create` 的 600×760 用于桌面初始尺寸和 Android 竖屏方向，不会把手机表面限制为 600×760。APK 使用工具链自动生成的调试签名。

HTML 是第一个编译输入，兄弟 `.zan` 文件须一起传入。`TrapCat.html` 保留固定文件名，使生成的 `TrapCatHtml` 类不受项目名影响。

**底图怎么接的**（`src/App.zan` 的 `SetupBoardArt`）：`assets/board.jpg` 用
`Game.Kit.Assets.Find` 定位（以 exe 目录为锚向上找 4 级），找到就写进
`<img id="BoardBg">` 的 `Src`，找不到就把这个 `img` 藏掉。**引擎的 CSS 背景
不支持 `url()`**（只有颜色与线性渐变），所以底图只能用 `<img>` 元素叠在
棋盘底下，不能写 `background-image`。

`src/TrapCatBoard.zan` 只负责规则、BFS 寻路与撤销记录；`src/App.zan` 在生成时改名为项目名，负责事件与状态显示；`src/TrapCat.html` 负责布局和样式；`src/TrapCatSprite.zan` 负责小猫的矢量造型和动画。等长出口采用随机选择，悔棋后重复同一步会保持猫的走法。

可通过环境变量 `ZAN_TRAP_CAT_SEED` 指定整数种子，复现随机开局。仓库内规则测试在 `tests/templates/trap-cat/board_test.zan`，GUI 点击回归在同目录的 `gameplay.ui`；模板创建、编译与发布验收使用：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/e2e_pipeline.ps1 -Only trap-cat
```
