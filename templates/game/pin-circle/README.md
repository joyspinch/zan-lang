# 见缝插针 · 星环机关

点击发射针球，把本关所有针插入旋转机关。碰到已有针球或针杆即失败；重试从当前关开始。

## 关卡与进度

`src/PinLevels.zan` 是手工编排的 40 关曲线，分四章，每章第 10 关是首领关：

| 章节 | 关卡 | 引入的机制 |
| --- | --- | --- |
| 第一章 · 初转 | 1–10 | 匀速与逆旋、预置针阵（均布 / 针墙 / 半月扇面），第 4 关起**变速脉冲**，第 7 关起**骤停反转** |
| 第二章 · 变奏 | 11–20 | 更急的脉冲与反转、**往返摆动**（来回摆但整体前进，所有缺口都会转到发射口） |
| 第三章 · 摇摆 | 21–30 | **摇摆机关**：转盘在发射轴两侧左右漂移，入针角度随之偏移；两侧黄铜卡尺标出摆幅 |
| 第四章 · 星环 | 31–40 | 全部机制叠加，最大针数 |

- **首领关**（10/20/30/40）有 2–3 个“环”：转盘外圈每个黄铜环代表一个待破的阶段；插满本环后旧针炸飞、铜环碎裂，约 0.9 秒换环间歇（发射口金色进度弧）后下一环带着新针阵与新机制登场。
- **骤停反转**提前约 0.5 秒预警：轨道弧与外环闪红、机制标签变红并有提示音。
- **星级**：通关 ★；未重试一次过 +★（重试、R 键重开都算重试）；在限时内通关 +★。对局顶部实时显示两项可得星状态与剩余限时。限时由无头机器人校准（见下）。
- **解锁**：通关第 N 关解锁第 N+1 关。主菜单圆形机关“继续”第一个未通关的已解锁关卡；“选择关卡”打开选关网格（标准按钮皮肤：已通关显示星级，下一关青光描边，首领关金色描边并带“首领”标，未解锁虚线框），可点击或用方向键 + Enter 选择。

## 手感与反馈

插针时转盘被顶起回弹、针座迸出火花环、黄铜清响随填充升调；贴着已有针插入（间隙 < 0.2 弧度）弹出“CLOSE!”并有清脆提示；通关时针依次闪光、转盘加速旋转后所有针向外飞散、中央“通关”字样弹出，结算卡三颗星依次弹入并各有升调音；失败时 70ms 顿帧 + 约 0.2 倍慢动作，撞针翻滚弹落，屏幕震动并泛红，再回到实时。全部效果在 `src/PinFx.zan`，只读世界状态，仿真不依赖它。

深青金属背景、黄铜机关与青玉针球贯穿主菜单、选关、对局、暂停和结算。默认舞台为 600×900 竖屏；调整窗口后等比缩放游戏几何，绘制和点击使用同一套坐标换算。青玉表示玩家插针，黄铜表示预置针，红色表示碰撞。

| 操作 | 功能 |
| --- | --- |
| 菜单圆形机关 / Enter / 空格 | 继续第一个未通关关卡 |
| 菜单“选择关卡” / L | 打开选关网格（暂停与结算页也可按 L 或点“选择关卡”） |
| 选关页 方向键 / Enter / 点击格子 / Esc | 移动选择 / 开始所选关 / 直接开始 / 返回菜单 |
| 点击游戏空白区域或底部针球 / 空格 | 发射一根针 |
| 右上暂停图标 / P / Esc | 暂停（窗口失焦或最小化时自动暂停） |
| 暂停页圆形机关 / P / 空格 | 恢复，恢复这一击不会发射 |
| R | 重试当前关 |
| 结算圆形机关 / Enter / 空格 | 通关后下一关，失败后重试本关 |
| 暂停或结算页“返回主菜单” / 左上返回图标 / 暂停或结算页 Esc | 返回菜单；菜单中 Esc 退出 |
| 右上音符图标 / M | 音效开关（任意页面可用，跨次启动保留） |

空格按住不会自动连发，飞针在空中时新发射会被忽略。暂停层遮住的游戏区域不响应点击。对局中按 Esc 先进入暂停页，不会直接丢掉本局；再按一次 Esc 才返回菜单。前两局和每关第一针落定前，底部显示操作提示。

## 音效与存档

- 音效全部由 `Game.Foundation.SfxPresets` 程序化合成（`.WithSweep` 调音），无音频素材：发射呼啸、入隙黄铜清响（越接近满盘音高越高）、险中“CLOSE!”清音、反转预警滴答、变速加速提示、首领换环碎裂声、碰撞闷响 + 慢动作下沉音、过关上扬、三颗星依次升调、刷新纪录与按钮点按。没有音频设备时静默运行。
- 存档用 `GameProfile.Open("pin-circle")`：最高通关关卡（`level`，决定解锁）、每关最佳星级（`stars<N>`）、开局次数（`plays`）、累计入隙针数（`pins`）、累计险中次数（`closes`）与静音开关（`muted`）。主菜单与选关页显示总星数；结算卡显示用时/限时、本关最佳星级，刷新纪录时标题变为 `NEW BEST`。
- 调试开关（默认关闭）：环境变量 `PIN_UNLOCK_ALL=1` 解锁全部关卡，用于截图与 QA。
- 存档位置：Windows `%LOCALAPPDATA%\ZanGames\pin-circle\`、macOS `~/Library/Application Support/ZanGames/pin-circle/`、Linux `~/.local/share/ZanGames/pin-circle/`；设置环境变量 `ZAN_GAME_DATA_DIR` 可改根目录（测试/便携版）。从不写工作目录。
- 无头回放（`src/PinSim.zan` + `replays/bot.replay`）只驱动 `PinWorld`，不接触音频与存档。输入 4 让机器人按帧号跳到 1–40 任一关，覆盖变速、反转、摇摆与首领多环；`[Invariant]` 额外检查关卡/环序号、盘上针数守恒、换环间歇不能有飞针、摇摆不超幅。改关卡表后按 Game.Foundation README 重录回放。

## 平衡

关卡限时与难度曲线用无头仿真校准：三种机器人在每关 24 个初始角度上打（“专家”每次都在安全窗口发射；“熟练”约 130ms 反应、±3 帧抖动、两针间犹豫 10–20 帧；“手潮”±5 帧抖动更保守）。熟练机器人胜率大致为第一章 ~95%、第二章 ~75–90%、第三章开头回落再爬升、第四章 ~40–80%，首领关略低于同章平均；专家 100% 通关全部 40 关（每关可解）。限时 = 熟练机器人平均通关用时 × 1.5（取整到 0.5 秒，个别离群关手调）。

## 文件

- `src/PinWorld.zan`、`src/PinLevels.zan`、`src/Entities/`：独立固定步长仿真、40 关关卡表（多环、变速、反转、摆动、摇摆）、针球与针杆碰撞、险中判定。
- `src/PinFx.zan`：表现层反馈（火花、回弹、横幅、通关飞散、失败慢动作），不进仿真。
- `src/PinShell.zan`：应用层外壳——玩家档案与音效库，仿真不引用。
- `src/PinView.zan`、`src/PinHud.zan`：素材绘制、标准组件 HUD、动效、舞台坐标与点击几何；HUD 在共享测量前配置当前区域。
- `src/main.zan`、`src/Scenes/`：程序入口、菜单、选关、游戏和结算场景，以及鼠标与键盘输入。
- `assets/`：背景、机关、三种针球和标题图片。
- `template.manifest`、`zan.proj`：IDE 模板发现与工程入口，无固定平台。

## 编译运行

在当前主仓根目录运行以下 PowerShell 命令，直接编译模板原始源码，不复制模板树。安装版 SDK 使用其 `toolchain/zanc.exe` 替换编译器路径，并将包工程根设为对应 SDK 根目录。

```powershell
Set-Location D:/project/zan-lang
& ./scripts/build_gate.ps1 -Name build -Script {
    New-Item -ItemType Directory -Force build/pin-circle | Out-Null
    build/zanc.exe templates/game/pin-circle/src/main.zan templates/game/pin-circle/src --auto-stdlib --package-project . --embed templates/game/pin-circle/assets=assets --publish --subsystem windows -o build/pin-circle/PinCircle.exe
}
build/pin-circle/PinCircle.exe
```

入口 `main.zan` 排在首位，随后 `src` 目录递归纳入 `PinWorld`、`PinView`、Entities、Scenes 和 HUD 的全部源码，已列入口不会重复纳入。`--package-project .` 使用当前主仓的 `packages/`。`--embed 磁盘路径=内嵌前缀` 把模板素材放到程序内的 `assets/`，运行时不需要另找模板目录。生成的原生驱动文件应与程序保持在同一目录。

IDE 新建项目会复制 `assets/`，普通运行从项目资源目录读取；IDE 发布会按工程的单文件设置内嵌或复制资源。新建项目后也可在项目根目录直接编译；以下仍使用当前主仓编译器和包（新建项目是产品功能，不用于复制模板做验证）：

```powershell
& D:/project/zan-lang/scripts/build_gate.ps1 -Name build -Script {
    New-Item -ItemType Directory -Force build | Out-Null
    D:/project/zan-lang/build/zanc.exe src/main.zan src --auto-stdlib --package-project D:/project/zan-lang --embed assets=assets --publish --subsystem windows -o build/game.exe
}
build/game.exe
```

上述 `--subsystem windows` 适用于 Windows。其他平台由新建项目向导或 `--target` 选择。

### Android ARM64

在仓库根目录发布适用于 Android 9（API 28）及以上 ARM64 设备的 APK：

```powershell
Set-Location D:/project/zan-lang
& ./scripts/build_gate.ps1 -Name build -Script {
    New-Item -ItemType Directory -Force build/pin-circle | Out-Null
    build/zanc.exe templates/game/pin-circle/src/main.zan templates/game/pin-circle/src --auto-stdlib --package-project . --embed templates/game/pin-circle/assets=assets --publish --target android-arm64 --emit-apk build/pin-circle/PinCircle-arm64.apk --apk-package com.zan.pincircle --apk-label "见缝插针·星环机关" -o build/pin-circle/PinCircle-arm64.so
}
```

`--emit-apk` 后面必须紧跟 APK 路径，`-o` 指定中间原生库。SDK 须带有 Android ARM64 运行时和 GUI、图片、游戏驱动，`JAVA_HOME` 指向可用于签名的 JDK。素材和原生驱动随 APK 打包，安装后无需复制外置资源。默认使用 SDK 的测试证书签名，可侧载安装；商店发布须换成自己的正式签名。

手机上轻触开始机关进入对局，轻触空白处发射，右上角暂停。暂停与结算页中的“返回主菜单”结束本局，返回后可重新开始。Android 系统返回键在对局中先暂停，在暂停和结算页返回主菜单，在主菜单退出应用。

安卓返回流程可在专用设备或隔离模拟器上回归（需要 `adb` 和 Python Pillow，`--serial` 必须明确指定设备）：

```powershell
python tests/templates/pin-circle/android_return.py --serial emulator-5560 --apk build/pin-circle/PinCircle-arm64.apk
```

该脚本检查真实失败结算的按钮返回、系统返回键、重新开局、后台恢复与菜单退出，并保存截图和日志。可用 `--cleared-apk` 传入以 `tests/templates/pin-circle/scene_test.zan` 为首个源码、包名为 `com.zan.pincircle.probe` 的 Android 夹具，额外验证真实通关结算返回；该夹具复用产品场景和模型，不增加产品测试入口。

## 验证

仓库内可运行单游戏回归，不会启动全仓测试档：

```powershell
python tests/templates/pin-circle/run.py
```

脚本重新编译实际世界模型、发布程序和场景夹具，覆盖：

- 针球与针杆碰撞边界、暂停、重复发射、反转和大步长防穿透。
- 20 关 × 4 个初始角度的可解性，以及 120 局不同连点节奏。
- 真实窗口中的菜单、鼠标发射、空格长按、暂停遮挡、失败与重试，以及失败结算直接返回菜单后重新开始。
- 通关结算直接返回菜单、重新开始与点击进入下一关；场景夹具仅给第一关注入安全发射，仍复用实际场景和物理模型。
- 发布程序从没有外置素材的目录启动，以及窗口放大为 900×1350 后的绘制和点击。

日志与 PNG 截图写入 `build/test-out/pin-circle-tests/`，可执行程序写入 `build/cases/`，最终输出应以 `PASS pin-circle:` 结尾。回归入口直接复用主树的模型和场景源码，不复制模板树。窗口测试需要 Windows；单独的模型回归不依赖窗口，只纳入模型源码，不传含产品 `Main` 的整个 `src` 目录：

```powershell
Set-Location D:/project/zan-lang
& ./scripts/build_gate.ps1 -Name build -Script {
    New-Item -ItemType Directory -Force build/cases | Out-Null
    build/zanc.exe tests/templates/pin-circle/world_test.zan templates/game/pin-circle/src/PinWorld.zan templates/game/pin-circle/src/PinLevels.zan templates/game/pin-circle/src/Entities --auto-stdlib --package-project . -o build/cases/pin_circle_world-test.exe
}
build/cases/pin_circle_world-test.exe
```
