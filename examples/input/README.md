# System.Input & System.Management

跨平台系统能力封装:输入模拟(Windows 真实现,其他平台接口存在但抛
`PlatformNotSupportedException`)与只读系统信息(Windows/Linux 真实现)。

## System.Input — 输入模拟

Windows 上的输入模拟按实现方式分两类,都在 `System.Input` 下:

| 分类 | 原理 | 影响 | 适用 |
| --- | --- | --- | --- |
| 前台模拟 `Mouse` / `Keyboard` | `SendInput` 注入,与物理设备同路径 | 真实移动鼠标、占用键盘,需要目标处于前台 | 游戏、DirectInput 程序、远程会话 |
| 后台模拟 `Background` | `PostMessage`/`SendMessage` 向指定窗口投递 WM_ 消息 | 不动真实鼠标、不占真实键盘,可边运行边用电脑 | 普通 Win32 / UI 框架程序(记事本、浏览器、聊天工具...) |

后台模拟的局限:消息级注入只对"读窗口消息"的程序生效,DirectInput 类
游戏通常不读窗口消息,对它们无效。

### 前台模拟(SendInput)

| 模块 | 内容 |
| --- | --- |
| `System.Input.Mouse` | `GetPos/SetPos`、`MoveTo`(步进轨迹)、`Down/Up/Click/DoubleClick`(左/中/右/X1/X2)、`Drag`、`Wheel`、`IsDown` |
| `System.Input.Keyboard` | `Send`(按键宏)、`SendText`(Unicode 直发)、`Down/Up/Press/Repeat`、`IsDown/IsToggled`、`CapsLock/NumLock/ScrollLock` 读取与设置、`VK/VKName` 名称表、`Wait/WaitUp` |
| `System.Input.Hook` | 全局低级键盘/鼠标钩子(`InstallKeyboard/InstallMouse/Uninstall`),回调返回 true 可吞掉事件 |
| `System.Input.Hotkey` | 全局热键(`Register("Ctrl+Shift+A", cb)` / `Unregister`) |

### 后台模拟(消息级,不影响鼠标键盘)

| 模块 | 内容 |
| --- | --- |
| `System.Input.Background` | 窗口查找(`FindWindowByTitle/ByClass/FindChild`);后台键盘(`KeyDown/Up/Press/Repeat`、`SendChar/SendText/SendMacro`);后台鼠标(`MouseMove/Down/Up/Click/Wheel`,客户区坐标);同步变体(`SyncKeyPress/SyncMouseClick`,SendMessage 等目标处理完) |

### 示例

- 前台(交互式,会真实移动鼠标/输入!):`mouse_demo.zan` — 移动、点击、
  双击、拖拽、滚轮;`keyboard_demo.zan` — 按键宏、Unicode 文本、修饰键
  组合、锁键状态。运行前请把焦点放到草稿窗口,避免误操作正在使用的程序。
- 后台(安全,不抢焦点):`background_demo.zan` — 打开记事本后运行,向
  记事本后台发送文本、按键宏、鼠标点击与滚轮,期间你的鼠标键盘不受影响。

```bash
build/zanc examples/input/mouse_demo.zan --auto-stdlib -o build/mouse_demo.exe
build/zanc examples/input/background_demo.zan --auto-stdlib -o build/background_demo.exe
build/background_demo.exe
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
| `System.Diagnostics.Privileges` | `IsAdmin/UserName/UserSid`(跨平台)、`EnablePrivilege`(Windows)、`RunAs/RunAsWait`(UAC 提权,Windows) |
| `System.Diagnostics.ProcessControl` | 进程控制:`Suspend/Resume/Terminate/Kill/IsRunning/WaitForExit/GetExitCode/GetPriority/SetPriority`(Windows + POSIX 信号) |
| `System.Windows.TrayIcon` | 托盘图标:`Add/Remove/SetTooltip/ShowBalloon`,回调区分左/右/双击(自带隐藏窗口+消息循环,不依赖 Gui) |
| `System.Windows.Screen` | 屏幕:`ScreenWidth/Height/Dpi`、`GetPixel`(0xAARRGGBB)、`CapturePixels`(BGRA)、`CaptureToBmp/CaptureAllToBmp`(Windows GDI) |
| `System.ServiceProcess` | Windows 服务:`List/Get/Start/Stop/Restart/Delete/StateName`(sc.exe 封装,输出编码自动归一) |
| `System.Management.TaskScheduler` | 计划任务:`List/Get/Exists/Create/Delete/Run/End`(schtasks.exe 封装,中英双语字段解析) |

### 示例(只读,安全)

- `sysinfo_demo.zan` — 打印 CPU/内存/系统/存储/显示器/电源信息
- `process_control_demo.zan` — 打印权限/身份与自身进程详情、进程列表前几名(只读,
  不操作其他进程)
- `tray_screen_demo.zan` — 托盘图标+气泡通知(8 秒后自动移除),全屏截图存 BMP、
  屏幕取色(截屏后立即删除文件)
- `service_task_demo.zan` — 打印服务总数/运行数与计划任务总数/就绪数,以及
  前几个条目详情(只读,不创建/启停任何服务或任务)

```bash
build/zanc examples/input/sysinfo_demo.zan --auto-stdlib -o build/sysinfo_demo.exe
build/sysinfo_demo.exe
```

## 测试

- `tests/conformance/input_keyboard_vk.zan` — VK 名称表往返(纯函数,全平台)
- `tests/conformance/input_mouse_pos.zan` — 鼠标位置只读查询(Windows 实测)
- `tests/conformance/input_hook_hotkey.zan` — 钩子/热键安装与卸载生命周期(Windows 实测)
- `tests/conformance/input_background.zan` — 后台模拟:窗口查找/0 句柄容错/桌面窗口 PostMessage 链路(Windows 实测)
- `tests/conformance/management_smoke.zan` — 信息族结构不变量(Windows 实测)
- `tests/conformance/process_list_smoke.zan` — 进程枚举与自身详情(Windows 实测)
- `tests/conformance/process_control_smoke.zan` — 权限查询与自身进程控制不变量
  (Windows 实测,不触碰其他进程)
- `tests/conformance/win_tray_screen_smoke.zan` — 托盘图标生命周期 + 屏幕
  尺寸/捕获/BMP 头不变量(Windows 实测,无人值守可跑)
- `tests/conformance/win_serviceprocess_smoke.zan` — 服务枚举/查询/状态码
  不变量(只读,不启停服务)
- `tests/conformance/win_taskscheduler_smoke.zan` — 计划任务枚举/查询 +
  创建→查询→删除往返(唯一任务名,测完删除)
- `tests/conformance/registry_roundtrip.zan` — 注册表读写往返(临时键,测完删除)
