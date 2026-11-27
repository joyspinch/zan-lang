# 3D First-Person Target Tag (第一人称 3D 标记挑战模板)

基于 Zan.Game.Render3D 工业级 3D 渲染管线驱动的第一人称漫游与 3D 空间拾取母版，
带一个最小但完整的玩法闭环：**用准星点中（标记）场景里全部 8 个发光方块，用时越短越好。**

## 特性

- **玩法闭环**：开始页（目标 + 操作说明 + 最佳成绩）→ 计时对局 → 暂停卡 → 结算卡
  （用时、最佳、NEW BEST、再来一局 / 回菜单），不重启程序即可重开。
- **3D 渲染与光照**：
  - 基于 `Primitives3D` 自动生成立体地面与目标方块网格（立方体按边长缩放复用一份网格）。
  - 基于 `RenderPlan` 实现自动视锥裁剪（Frustum Culling）与 GPU 批次渲染（`Renderer3D`）。
  - 动态方向日光照与环境光混合；未标记方块自发光，标记后变暗为石板灰。
  - GPU 不可用时 CPU 透视线框兜底，同样区分已/未标记。
- **第一人称控制器 (`FpsController3D`)**：WASD + Space/Shift 平滑行走与升降，
  鼠标转视角，严格 ±89° 俯仰锁定。
- **3D 空间实时射线求交 (`Raycast3D`)**：屏幕正中准星发射世界射线，与目标 AABB 求交；
  30 m 内未标记的目标准星变绿，点击即标记，HUD 显示距离与“已标记 / 太远”反馈。
- **音效**（`SfxBank` + 程序化 `SfxPresets`，零音频文件）：标记成功、无效点击、
  全部完成、新纪录、UI 点击、暂停/继续、贴地行走脚步（限频 360 ms）。无音频设备时静默。
- **玩家档案**（`GameProfile.Open("fps-3d")`）：最佳用时（`SubmitLowest("time", ms)`）、
  局数 `plays`、通关数 `clears`、静音开关 `muted`。
- **暂停**：P / Esc 暂停并释放视角与按键；窗口失焦或最小化自动暂停（`GuiHost.WindowActive()`）。
- **界面**：全部为标准 Gui 控件（Panel / Label / Button）+ 本游戏的深色玻璃 + 青色霓虹皮肤，
  按 1280x720 设计稿随画布等比缩放；新手操作提示前两局全程显示，之后只在开局 8 秒显示。

## 操作

| 按键 | 作用 |
|---|---|
| W A S D / 方向键 | 前后左右移动 |
| Space / Shift | 升高 / 降低 |
| 按住鼠标左键或右键拖动 | 转动视角 |
| F 或右键单击 | 切换自由视角（无需按住） |
| 鼠标左键 | 标记准星对准的方块（30 m 内） |
| P / Esc | 暂停 / 继续（Esc 不会直接退出游戏） |
| R | 暂停或结算时重开一局 |
| Enter / Space | 开始页开始、结算页再来一局 |
| M | 静音 / 取消静音（会被记住） |

退出：开始页 QUIT（或 Esc）、暂停卡与结算卡上的 QUIT 按钮。

## 存档位置

档案写在用户数据目录，不写工作目录：

- macOS：`~/Library/Application Support/ZanGames/fps-3d/`
- Windows：`%LOCALAPPDATA%\ZanGames\fps-3d\`
- Linux：`~/.local/share/ZanGames/fps-3d/`

设置环境变量 `ZAN_GAME_DATA_DIR=<目录>` 可改根目录（测试 / 便携版）。

## 回放与回归

`src/FpsSim.zan` 以固定 1/60 秒一帧无头推进 `FpsGame`（不上传网格、不画、不碰音频与档案）。
输入编码见文件头注释；`200` = 左键标记。校验和覆盖位置、视角、准星命中、标记位图、
完成标志与计时；不变量检查俯仰钳制、命中距离、标记计数一致与计时有效。
`replays/bot.replay` 由 `Replay.Fuzz(new FpsSim(), "FpsSim", 20261125, 1200, FpsSim.Bot, 0)` 录制；
改了目标布局、移动或视角参数导致回放分叉时，按 `packages/Zan.Game/src/Game/Foundation/README.md`
重新录制。

## 运行与编译

在当前主仓根目录运行以下 PowerShell 命令，直接编译模板原始源码，不复制模板树：

```powershell
Set-Location D:/project/zan-lang
& ./scripts/build_gate.ps1 -Name build -Script {
    build/zanc.exe templates/game/fps-3d/src/main.zan templates/game/fps-3d/src --auto-stdlib --package-project . --publish --subsystem windows -o build/fps3d.exe
}
build/fps3d.exe
```

`main.zan` 必须排在首位，随后 `src` 目录递归纳入 `FpsGame.zan`、`FpsHud.zan`、`FpsCrosshair.zan` 等全部源码，已列入口不会重复纳入。`--package-project .` 使用当前主仓的 `packages/`；当前模板没有需要额外内嵌的素材目录。生成的原生驱动文件应与程序保持在同一目录。
