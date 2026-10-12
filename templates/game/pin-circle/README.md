# 见缝插针 · 星环机关

点击发射针球，把本关所有针插入旋转机关。碰到已有针球或针杆即失败；重试从当前关开始。第 4 关起加入变速，第 7 关起加入平滑反转，更高关卡增加针数与转速。

深青金属背景、黄铜机关与青玉针球贯穿主菜单、对局、暂停和结算。发射带光迹，成功入隙有火花与光环，碰撞有红色反馈，结算带入场动效。默认舞台为 600×900 竖屏；调整窗口后等比缩放游戏几何，绘制和点击使用同一套坐标换算。青玉表示玩家插针，黄铜表示预置针，红色表示碰撞。

| 操作 | 功能 |
| --- | --- |
| 菜单圆形机关 / Enter / 空格 | 开始挑战 |
| 点击游戏空白区域或底部针球 / 空格 | 发射一根针 |
| 右上暂停图标 / P | 暂停 |
| 暂停页圆形机关 / P / 空格 | 恢复，恢复这一击不会发射 |
| R | 重试当前关 |
| 结算圆形机关 / Enter / 空格 | 通关后下一关，失败后重试本关 |
| 暂停或结算页“返回主菜单” / 左上返回图标 / Esc | 返回菜单；菜单中 Esc 退出 |

空格按住不会自动连发，飞针在空中时新发射会被忽略。暂停层遮住的游戏区域不响应点击。最高关卡只记录当前运行期间。

## 文件

- `src/World.zan`：独立固定步长仿真、关卡参数、针球与针杆碰撞。
- `src/View.zan`：素材绘制、动效、舞台坐标与点击几何。
- `src/main.zan`：菜单、游戏和结算场景，以及鼠标与键盘输入。
- `assets/`：背景、机关、三种针球和标题图片。
- `template.manifest`、`zan.proj`：IDE 模板发现与工程入口，无固定平台。

## 编译运行

在仓库根目录运行以下 PowerShell 命令。安装版 SDK 使用其 `toolchain/zanc.exe` 替换编译器路径。

```powershell
New-Item -ItemType Directory -Force build/pin-circle | Out-Null
build/zanc.exe templates/game/pin-circle/src/main.zan templates/game/pin-circle/src/World.zan templates/game/pin-circle/src/View.zan --auto-stdlib --embed assets=templates/game/pin-circle/assets --publish --subsystem windows -o build/pin-circle/PinCircle.exe
build/pin-circle/PinCircle.exe
```

`--embed` 把游戏素材放入程序，运行时不需要另找模板目录。生成的原生驱动文件应与程序保持在同一目录。

IDE 新建项目会复制 `assets/`，普通运行从项目资源目录读取；IDE 发布会按工程的单文件设置内嵌或复制资源。新建项目后也可在项目根目录直接编译：

```powershell
New-Item -ItemType Directory -Force build | Out-Null
zanc src/main.zan src/World.zan src/View.zan --auto-stdlib --embed assets=assets --publish --subsystem windows -o build/game.exe
build/game.exe
```

上述 `--subsystem windows` 适用于 Windows。其他平台由新建项目向导或 `--target` 选择。

### Android ARM64

在仓库根目录发布适用于 Android 9（API 28）及以上 ARM64 设备的 APK：

```powershell
build/zanc.exe templates/game/pin-circle/src/main.zan templates/game/pin-circle/src/World.zan templates/game/pin-circle/src/View.zan --auto-stdlib --embed assets=templates/game/pin-circle/assets --publish --target android-arm64 --emit-apk build/pin-circle/PinCircle-arm64.apk --apk-package com.zan.pincircle --apk-label "见缝插针·星环机关" -o build/pin-circle/PinCircle-arm64.so
```

`--emit-apk` 后面必须紧跟 APK 路径，`-o` 指定中间原生库。SDK 须带有 Android ARM64 运行时和 GUI、图片、游戏驱动，`JAVA_HOME` 指向可用于签名的 JDK。素材和原生驱动随 APK 打包，安装后无需复制外置资源。默认使用 SDK 的测试证书签名，可侧载安装；商店发布须换成自己的正式签名。

手机上轻触开始机关进入对局，轻触空白处发射，右上角暂停。暂停与结算页中的“返回主菜单”结束本局，返回后可重新开始。Android 系统返回键在对局、暂停和结算页返回主菜单，在主菜单退出应用。

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

日志与 PNG 截图写入 `_scratch/pin-circle-tests/`，最终输出应以 `PASS pin-circle:` 结尾。窗口测试需要 Windows；单独的模型回归不依赖窗口：

```powershell
New-Item -ItemType Directory -Force _scratch/pin-circle-tests | Out-Null
build/zanc.exe tests/templates/pin-circle/world_test.zan templates/game/pin-circle/src/World.zan --auto-stdlib -o _scratch/pin-circle-tests/world-test.exe
_scratch/pin-circle-tests/world-test.exe
```
