# 手柄示例

`gamepad_demo.zan` 演示 `SDL3` 模块的手柄（游戏控制器）输入：

- `SdlGamepad.Init()`：启用手柄子系统并打开事件投递；
- `SdlGamepad.Count()` / `IdAt(i)` / `Open(instanceId)` / `OpenFirst()`：
  枚举与打开手柄，`Name()` 读名字、`IsConnected()` 查是否还连着；
- 事件路径：和键鼠一样从 `SdlEvent.Poll()` 拿，类型见 `SdlEventType`
  （`GamepadAdded` / `GamepadRemoved` / `GamepadButtonDown` / `GamepadButtonUp`
  / `GamepadAxisMotion`），细节用 `SdlEvent.GamepadWhich()` /
  `GamepadButton()` / `GamepadAxis()` / `GamepadAxisValue()`；
- 轮询路径：`pad.IsDown(button)`、`pad.Axis(axis)`、`pad.AxisNormalized(axis)`
  每帧直接读状态（多数游戏这么用）；
- `pad.Rumble(low, high, ms)`：双马达震动，手柄不支持时返回 false。

按键/轴编号见 `SdlGamepadButton`（South/East/West/North 对应 A/B/X/Y，
面向 Xbox 命名）与 `SdlGamepadAxis`（左右摇杆 X/Y、左右扳机）。

在仓库根目录编译运行：

```powershell
build\zanc.exe examples\gamepad\gamepad_demo.zan --auto-stdlib -o build\gamepad_demo.exe
build\gamepad_demo.exe
```

程序打开后等 5 秒：插入或按动手柄会实时打印事件；没有手柄时会提示等待。
无显示环境下可设 `SDL_VIDEODRIVER=dummy`（本例只用手柄+事件子系统，不建窗口）。
手柄事件由 SDL 的控制器数据库映射，因此 Xbox / PlayStation / Switch Pro
等常见手柄都统一成上面的标准按键与轴。
