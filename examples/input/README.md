# System.Input & System.Management

跨平台系统能力封装:输入模拟(Windows 真实现,其他平台接口存在但抛
`PlatformNotSupportedException`)与只读系统信息(Windows/Linux 真实现)。

## System.Input — 输入模拟

Windows 通过 `SendInput` 注入(与物理设备同路径,远程会话/提权窗口也生效)。

| 模块 | 内容 |
| --- | --- |
| `System.Input.Mouse` | `GetPos/SetPos`、`MoveTo`(步进轨迹)、`Down/Up/Click/DoubleClick`(左/中/右/X1/X2)、`Drag`、`Wheel`、`IsDown` |
| `System.Input.Keyboard` | `Send`(按键宏)、`SendText`(Unicode 直发)、`Down/Up/Press/Repeat`、`IsDown/IsToggled`、`CapsLock/NumLock/ScrollLock` 读取与设置、`VK/VKName` 名称表、`Wait/WaitUp` |
| `System.Input.Hook` | 全局低级键盘/鼠标钩子(`InstallKeyboard/InstallMouse/Uninstall`),回调返回 true 可吞掉事件 |
| `System.Input.Hotkey` | 全局热键(`Register("Ctrl+Shift+A", cb)` / `Unregister`) |

### 示例(交互式,会真实移动鼠标/输入!)

- `mouse_demo.zan` — 移动、点击、双击、拖拽、滚轮
- `keyboard_demo.zan` — 按键宏、Unicode 文本、修饰键组合、锁键状态

运行前请把焦点放到草稿窗口,避免误操作正在使用的程序。

```bash
build/zanc examples/input/mouse_demo.zan --auto-stdlib -o build/mouse_demo.exe
build/mouse_demo.exe
```

### 按键宏语法

宏是花括号按键名与字面文本的序列。修饰键按下后保持,宏结束时逆序释放:

- `"{Ctrl}{C}"` — Ctrl+C(复制)
- `"{Ctrl}{Shift}{Esc}"` — 打开任务管理器
- `"hello"` — 键入 hello(单字母/数字按键,其余按 Unicode 直发)
- `"{Enter}next line"` — Enter 后输入文本

按键名大小写不敏感:`Ctrl`、`Shift`、`Alt`、`Win`、`Enter`、`Esc`、`Tab`、
`Backspace`、`Space`、`Delete`、`Insert`、`Home`、`End`、`PageUp`、`PageDown`、
`Up/Down/Left/Right`、`F1`–`F24`、单字母、单数字。

## System.Management — 系统信息

只读信息族,Windows 与 Linux 真实现,其他平台返回 0/空(纯查询,不抛异常,
`Memory.GetStatus` 与 `Power.GetStatus` 除外——它们无平台实现时抛
`PlatformNotSupportedException`)。

| 模块 | 内容 |
| --- | --- |
| `System.Management.Cpu` | `Brand/Vendor/FrequencyMHz/LogicalCores/Usage`(采样窗口 ~300ms) |
| `System.Management.Memory` | `GetStatus` → `MemoryStatus`(总/可用物理、页文件、虚拟内存、占用率) |
| `System.Management.SystemInfo` | `ComputerName/UserName/OsName/KernelVersion/UptimeSeconds/SystemDirectory/WindowsDirectory` |
| `System.Management.Storage` | `Drives()`(枚举)、`Root()`、`Usage(path)` → `DiskUsage`(总量/空闲/可用、占用率) |
| `System.Management.Display` | `ScreenWidth/ScreenHeight/RefreshRate/ColorDepth/Monitors()`(多显示器) |
| `System.Management.Power` | `GetStatus` → `PowerStatus`(电池状态/电量/续航)、`OnBattery` |
| `System.Management.Registry` | Windows 注册表:`OpenKey/CreateKey/CloseKey`、DWORD/QWORD/SZ 读写、`EnumKeys/EnumValues`、`DeleteValue/DeleteKey`(递归) |
| `System.Diagnostics.ProcessList` | 进程枚举:`List/ByName/ByPid`、`MemoryUsage/ExePath/SelfPid`(Windows Toolhelp32 / Linux /proc) |

### 示例(只读,安全)

- `sysinfo_demo.zan` — 打印 CPU/内存/系统/存储/显示器/电源信息

```bash
build/zanc examples/input/sysinfo_demo.zan --auto-stdlib -o build/sysinfo_demo.exe
build/sysinfo_demo.exe
```

## 测试

- `tests/conformance/input_keyboard_vk.zan` — VK 名称表往返(纯函数,全平台)
- `tests/conformance/input_mouse_pos.zan` — 鼠标位置只读查询(Windows 实测)
- `tests/conformance/input_hook_hotkey.zan` — 钩子/热键安装与卸载生命周期(Windows 实测)
- `tests/conformance/management_smoke.zan` — 信息族结构不变量(Windows 实测)
- `tests/conformance/process_list_smoke.zan` — 进程枚举与自身详情(Windows 实测)
- `tests/conformance/registry_roundtrip.zan` — 注册表读写往返(临时键,测完删除)
