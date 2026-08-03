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
| `System.IO.Compression.Deflate` | RFC 1951 DEFLATE:`Deflate(data, level)` / `Inflate(src, offset, len, maxOut)`(fixed+dynamic Huffman、stored block、LZ77,纯 Zan 手写,不依赖 zlib) |
| `System.IO.Compression.Crc32` | CRC-32(IEEE 802.3,查表法,`Compute(data, offset, len, crc)`) |
| `System.IO.Compression.GZip` | RFC 1952 gzip:`Compress(data, level)` / `Decompress(src)`(支持多成员流与 FEXTRA/FNAME/FCOMMENT/FHCRC 头) |
| `System.IO.Compression.Zip` | PKZIP zip:`Zip.Create(entries, level)` / `Read(bytes)` / `Extract(bytes, entry)`(stored+deflate、目录项、中央目录;`ZipEntry{name,data,isDirectory,method,crc32,compressedSize,uncompressedSize}`) |
| `System.IO.Compression.Tar` | POSIX ustar tar:`Tar.Create(entries)` / `Read(bytes)`(长路径 prefix 字段;与 GZip 组合即 .tar.gz;`TarEntry{name,data,isDirectory,mode}`) |
| `System.Automation.Window` | 窗口自动化(winex 对应):枚举/查找/等待(`EnumTopLevel/EnumChildren/FindAll/FindWindow/FindEx/WaitForWindow/WaitForChild/WaitForClose`)、文本(`GetText/SetText`)、菜单(`GetMenu/FindMenuItem/ClickMenu/ClickCommand`)、坐标(`GetRect/GetClientRect/FromPoint/ToScreen/ToClient`)、样式(`GetStyle/HasStyle/ModifyStyle`)、状态(`IsVisible/IsEnabled/IsHung/IsCloaked/Show/Minimize/Maximize/Close/Flash`) |
| `System.Automation.UiElement` | MSAA 无障碍自动化:从窗口/屏幕坐标取元素、遍历/递归查找、名称/值/描述/角色/状态/矩形/快捷键/默认动作、Invoke/Select/Focus/SetValue；元素持有 COM 引用，调用方用 `Dispose` 释放 |
| `System.Net.Ping` / `NetworkInterface` | Windows ICMP IPv4 Echo；网卡名称/描述/状态/MAC/IPv4/IPv6 枚举 |
| `System.Management.Device` | Windows SetupAPI 只读设备枚举：实例 ID、类、友好名、描述、厂商、硬件 ID |
| `System.Drawing.Printing` | Windows 打印机枚举、默认打印机查询和 RAW 文档发送 |
| `System.Security.Cryptography.Otp` | RFC 4226 HOTP / RFC 6238 TOTP，SHA-1/256/512、6–8 位与漂移窗口验证 |
| `System.Security.Cryptography.Jwt` | 严格 Base64Url；HS256/RS256 创建和验证；exp/nbf/iss/aud 校验；拒绝 none、重复键与超限输入 |
| `System.Net.WebDav` | WebDAV 客户端：PROPFIND 207 解析及 MKCOL/GET/PUT/DELETE/COPY/MOVE |
| `System.Text.Markdown` | 标题/段落/列表/引用/代码/强调/链接转安全紧凑 HTML，URL 协议白名单 |

### 示例(只读,安全)

- `sysinfo_demo.zan` — 打印 CPU/内存/系统/存储/显示器/电源信息
- `process_control_demo.zan` — 打印权限/身份与自身进程详情、进程列表前几名(只读,
  不操作其他进程)
- `tray_screen_demo.zan` — 托盘图标+气泡通知(8 秒后自动移除),全屏截图存 BMP、
  屏幕取色(截屏后立即删除文件)
- `service_task_demo.zan` — 打印服务总数/运行数与计划任务总数/就绪数,以及
  前几个条目详情(只读,不创建/启停任何服务或任务)
- `compression_demo.zan` — gzip 一段文本、构建 zip(文本+5 万字节重复数据+
  目录项)并读回提取、构建 tar 并读回;把 .gz/.zip/.tar 写到程序旁边再删除
  (全程内存,无需外部工具)
- `automation_demo.zan` — 启动记事本，展示窗口查找/状态/坐标/菜单能力，并
  遍历 MSAA 无障碍树，打印前几个元素的角色、名称和值，最后关闭进程

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
- `tests/conformance/win_compression_smoke.zan` — Deflate/GZip/Zip/Tar 往返
  与内容校验 + CRC32 已知向量("123456789" → 0xCBF43926)、gzip magic、zip "PK"
  签名(纯算法,全平台)
- `tests/conformance/registry_roundtrip.zan` — 注册表读写往返(临时键,测完删除)
