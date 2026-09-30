# System.Management

> 源码: `stdlib/System/Management/Cpu.zan`, `stdlib/System/Management/Device.zan`, `stdlib/System/Management/Display.zan`, `stdlib/System/Management/Memory.zan`, `stdlib/System/Management/Power.zan`, `stdlib/System/Management/Registry.zan`, `stdlib/System/Management/Storage.zan`, `stdlib/System/Management/SystemInfo.zan`, `stdlib/System/Management/TaskScheduler.zan`


## Cpu (class)

CPU 标识与负载。Windows 从注册表读取品牌/频率
（即 Windows 缓存在
HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor 下的 CPUID 值），核心数来自
GetSystemInfo，负载来自 GetSystemTimes。Linux 读取
/proc/cpuinfo 和 /proc/stat。其他平台抛出
PlatformNotSupportedException.

- [DllImport("kernel32", EntryPoint="Sleep")]static extern void SleepW(int ms);

- [DllImport("crt", EntryPoint="usleep")]static extern int Usleep(int us);

- static string Brand()
  - CPU 品牌字符串，如 "Intel(R) Core(TM) i7-10750H CPU @
    2.60GHz"。读取失败时为空。

- static string Vendor()
  - CPU 厂商，如 "GenuineIntel" / "AuthenticAMD"。无法读取时
    为空。

- static int FrequencyMHz()
  - 标称时钟频率（MHz），未知时为 0。

- static int LogicalCores()
  - 操作系统可见的逻辑处理器数量。

- static int Usage()
  - 约 300 ms 采样窗口内的总体 CPU 使用率（0..100）；
    首次调用会采样两次并休眠。

- [DllImport("kernel32", EntryPoint="GetSystemInfo")]static extern void WinGetSystemInfo(nint info);

- [DllImport("kernel32", EntryPoint="GetSystemTimes")]static extern int WinGetSystemTimes(nint times);

- [DllImport("advapi32", EntryPoint="RegOpenKeyExW")]static extern int RegOpenKeyExW(nint hKey, nint subKey, int options, int access, nint result);

- [DllImport("advapi32", EntryPoint="RegQueryValueExW")]static extern int RegQueryValueExW(nint hKey, nint name, nint reserved, nint type, nint data, nint dataSize);

- [DllImport("advapi32", EntryPoint="RegCloseKey")]static extern int RegCloseKey(nint hKey);

- static string RegString(nint root, string subKey, string name)
  - 读取 HKLM\<subKey> 下的 REG_SZ 值，缺失时返回 ""。

- static int RegDword(nint root, string subKey, string name)
  - 读取 HKLM\<subKey> 下的 REG_DWORD 值，缺失时返回 0。

- static string ProcField(string path, string field)
  - /proc 的 key: value 文件中（冒号后）第一个 `field` 的值。

- static int CountCpuLines()
  - 统计 /proc/cpuinfo 中 processor 行数；读取失败时至少返回 1。

- static long[]CpuJiffies()
  - [0] = 总 jiffies，[1] = 空闲 jiffies（来自 /proc/stat 第一行）。

- static int IndexOf(string hay, string needle, int from)
  - `hay` 中从 `from` 开始首次出现 `needle` 的索引，找不到返回 -1。

- static long ParseLong(string s)
  - 解析十进制 long，格式错误返回 -1。

- static int ParseInt(string s)
  - 解析十进制 int，格式错误返回 -1。

- static int CodeOf(string ch)
  - 单字符字符串的码点（ASCII 快速路径）。


## Device (class)

设备的只读枚举。Windows 上走 SetupAPI：Present() 返回当前
存在的设备，All() 还包括不存在的设备节点，每次查询覆盖
所有安装类。Linux 上走 sysfs（/sys/bus/<bus>/devices）：
className 是总线名，hardwareIds 是 MODALIAS，classGuid 总为 ""
（Linux 没有设备类 GUID）；sysfs 只暴露当前存在的设备，
因此 All() 与 Present() 结果相同。macOS 没有对应实现（需要
IOKit/ioreg），仍抛出 PlatformNotSupportedException。

- static List<DeviceInfo> Present()
  - 枚举所有安装类中当前存在的设备。Windows 上失败或无
    设备时返回空列表；Linux 同 All()；macOS 抛
    PlatformNotSupportedException。

- static List<DeviceInfo> All()
  - 枚举所有安装类中存在和不存在的设备。仅 Windows 有
    区别（All() 还包含历史幽灵设备节点）；Linux sysfs 只暴露当前
    存在的设备，All() 与 Present() 结果相同。

- static bool HasIndirectDisplayAdapter()
  - 当前机器是否装了 IDD 间接显示适配器（向日葵
    OrayIddDriver、GameViewer 虚拟显示适配器等远程控屏/虚拟屏
    驱动）。判定依据：Display 类设备的 hardwareIds/实例 ID 含
    IDD 标识或已知厂商根。CEF 的 DirectComposition 交换链在这类
    机器上会让 GPU 子进程连崩（A50-4），宿主可据此自动降级。
    非 Windows 恒 false。

- static string DisplayClassGuid="{ 4D36E968E32511CEBFC108002BE10318}";
  - Display 类安装 GUID
    {4d36e968-e325-11ce-bfc1-08002be10318}（含虚拟显示适配器）。

- static List<string> IddMarkers()
  - 已知 IDD/虚拟显示适配器标识（小写比较）。

- static bool WinHasIddAdapter()

- static List<DeviceInfo> List(bool presentOnly)
  - 枚举所有安装类。presentOnly=true 等价 Present()，
    false 等价 All()。macOS 抛 PlatformNotSupportedException。

- static List<DeviceInfo> SysfsList()
  - 枚举 /sys/bus/<bus>/devices/*。每个节点的 uevent 是内核向
    udev 报告的 KEY=VALUE 列表（SUBSYSTEM/DRIVER/MODALIAS 等），
    另外 vendor/device/product/manufacturer 等属性是单行文本文件。

- static string SysfsUevent(string dir, string key)
  - 读 uevent 里 `key=` 的值；不存在时返回 ""。

- static string SysfsFirst(string dir, string names)
  - 按逗号分隔的候选顺序读第一个存在的属性文件。

- static List<string> ParseMultiString(nint payload, int bytes)
  - 解析 REG_MULTI_SZ 负载，不越界读取。格式错误的值
    （字节数为奇数、缺少字符串终止符或末尾列表终止符）整体拒绝、
    返回空列表，而不是返回部分结果。

- [DllImport("setupapi", EntryPoint="SetupDiGetClassDevsW")]static extern nint SetupDiGetClassDevsW(nint classGuid, nint enumerator, nint parent, int flags);

- [DllImport("setupapi", EntryPoint="SetupDiEnumDeviceInfo")]static extern int SetupDiEnumDeviceInfo(nint devs, int index, nint data);

- [DllImport("setupapi", EntryPoint="SetupDiGetDeviceInstanceIdW")]static extern int SetupDiGetDeviceInstanceIdW(nint devs, nint data, nint buffer, int chars, nint requiredChars);

- [DllImport("setupapi", EntryPoint="SetupDiGetDeviceRegistryPropertyW")]static extern int SetupDiGetDeviceRegistryPropertyW(nint devs, nint data, int property, nint textType, nint buffer, int bytes, nint requiredBytes);

- [DllImport("setupapi", EntryPoint="SetupDiDestroyDeviceInfoList")]static extern int SetupDiDestroyDeviceInfoList(nint devs);

- static List<DeviceInfo> WinList(bool presentOnly)
  - Windows 实现：SetupDiGetClassDevsW 一次枚举所有安装类；
    句柄无效（失败）返回空列表。

- static string InstanceId(nint devs, nint data)
  - 两段式读取设备实例 ID（先取所需字符数再读缓冲）；失败返回 ""。

- static string StringProperty(nint devs, nint data, int property)
  - 读 REG_SZ / REG_EXPAND_SZ 型设备属性；属性缺失或类型不符返回 ""。

- static List<string> MultiStringProperty(nint devs, nint data, int property)
  - 读 REG_MULTI_SZ 型设备属性；属性缺失或类型不符返回空列表。

- static nint PropertyBuffer(nint devs, nint data, int property)
  - 返回一个分配块，包含 [type:int, byteCount:int, payload...]，
    属性不存在时返回 0。SetupAPI 在首次零缓冲区调用时
    报告精确的负载大小，避免固定大小截断。


## DeviceInfo (class)

单个设备的只读信息（Windows 即插即用设备或
Linux sysfs 设备节点）。各字段可能为空串（属性不存在时），
不抛异常。

- public string instanceId;
  - 设备实例标识。Windows 为即插即用实例 ID
    （如 "USB\VID_046D&PID_C52B\..."）；Linux 为 "总线/节点名"
    （如 "usb/1-1.2"）。

- public string classGuid;
  - Windows 设备安装类 GUID 字符串（如
    "{4d36e967-...}"）；Linux 没有对应概念，恒为 ""。

- public string className;
  - Windows 设备安装类名（如 "DiskDrive"）；Linux 为
    sysfs 总线名（如 "usb"、"pci"、"platform"）。

- public string friendlyName;
  - 人类可读名称。Windows 为 FriendlyName；Linux 依次取
    product/name/model/of_node name 属性，全缺时用 sysfs 节点名。

- public string description;
  - 设备描述。Windows 为 DeviceDesc；Linux 为驱动名
    （uevent DRIVER，退而取 DEVTYPE），全缺时退回 friendlyName。

- public string manufacturer;
  - 厂商。Windows 为 Mfg 属性；Linux 取 manufacturer 或
    vendor 属性文件。

- public List<string> hardwareIds;
  - 硬件标识。Windows 为 HARDWAREID 多串列表（可多条）；
    Linux 为 uevent 的 MODALIAS 单元素列表。


## DiskUsage (class)

单个磁盘/挂载点的容量信息。`name` 为盘符（"C:"）
（Windows）或挂载点（"/"、"/home"）（Linux）；大小单位为
字节。

- public string name;
  - 盘符（"C:"）（Windows）或挂载点（"/"）（Linux）。

- public long totalBytes;
  - 文件系统总容量（字节）；查询失败时为 0。

- public long freeBytes;
  - 全部空闲字节（含 root 保留块）。

- public long availBytes;
  - 普通用户可用的字节数（不含保留块）。

- public int UsagePercent()
  - 已用百分比（0-100，向下取整）；容量未知或已用为负时钳制为 0。


## Display (class)

显示器拓扑。Windows 通过 GetSystemMetrics 查询主屏，
并通过 EnumDisplayMonitors/EnumDisplaySettingsW 查询每台显示器。
Linux 读 xrandr（X11）或 wlr-randr / swaymsg（wlroots 系 Wayland），
macOS 读 system_profiler。拿不到拓扑时返回 0/空值而不抛异常：
布局代码更希望拿到一个可回退的值。

- static int ScreenWidth()
  - 主屏宽度（像素）。

- static int ScreenHeight()
  - 主屏高度（像素）。

- static int RefreshRate()
  - 主屏刷新率（Hz；未知时为 0）。

- static int ColorDepth()
  - 主屏颜色深度（每像素位数）。

- static DisplayInfo Primary()
  - 主显示器；拿不到拓扑时为 null。

- static List<DisplayInfo> Monitors()
  - 所有显示器（索引 0 = 主屏）。枚举不可用时
    返回空。

- static void ReadXrandr(List<DisplayInfo> list)
  - xrandr --current 里的已连接输出：
    eDP-1 connected primary 1920x1080+0+0 (normal ...) 344mm x 193mm
    1920x1080     60.02*+  59.97
    带 '*' 的那行是当前模式，刷新率从那里取。

- static void ReadWlrRandr(List<DisplayInfo> list)
  - wlr-randr（wlroots 系 Wayland）的输出：
    DP-1 "..."
    Position: 0,0
    Enabled: yes
    Modes:
    1920x1080 px, 60.000000 Hz (current)

- static void ReadXdpyinfo(List<DisplayInfo> list)
  - 没有 xrandr 时的下限：xdpyinfo 至少给出整个屏幕的尺寸。

- static bool Geometry(string line, DisplayInfo d)
  - 从 "1920x1080+0+0" 取尺寸与位置；没有几何信息（输出已连接但未启用）
    时返回 false。

- static int RateOf(string line)
  - 一行里的刷新率（四舍五入到整数 Hz；拿不到时为 0）。

- static bool IsDigit(int c)
  - 是否 ASCII 数字。

- static List<int> Ints(string line)
  - 一行里的所有十进制整数（按出现顺序）。

- static int FirstInt(string line)
  - 一行里的第一个整数；没有则 0。

- [DllImport("user32", EntryPoint="GetSystemMetrics")]static extern int WinGetSystemMetrics(int index);

- [DllImport("user32", EntryPoint="EnumDisplaySettingsW")]static extern int WinEnumDisplaySettingsW(nint name, int mode, nint devMode);

- [DllImport("user32", EntryPoint="EnumDisplayMonitors")]static extern int WinEnumDisplayMonitors(nint hdc, nint clip, MonitorEnumProc proc, nint data);

- [DllImport("user32", EntryPoint="GetMonitorInfoW")]static extern int WinGetMonitorInfoW(nint monitor, nint info);

- [DllImport("user32", EntryPoint="MonitorFromPoint")]static extern nint WinMonitorFromPoint(nint pt, int flags);

- static MonitorEnumProc monitorProc;

- static List<DisplayInfo> monitorResults;

- static int WinMonitorProc(nint monitor, nint hdc, nint rect, nint data)
  - 枚举回调：读 MONITORINFOEXW 与该屏当前 DEVMODEW 组装 DisplayInfo；
    返回 1 继续枚举。结果写入 monitorResults。

- static int WinMode(int mode, int offset)
  - 读取主屏当前 DEVMODEW 的 DWORD 字段。


## DisplayInfo (class)

单个显示器：索引（0 = 主屏）、名称、像素尺寸、刷新率和
颜色深度。

- public int index;
  - 枚举序号（0 = 主屏）。

- public string name;
  - 系统设备名（Windows 为 "\\.\DISPLAY1" 等；其他平台为描述性名称）。

- public int width;
  - 宽度（像素）。

- public int height;
  - 高度（像素）。

- public int x;
  - 该显示器在虚拟桌面里的左上角坐标。

- public int y;
  - 该显示器在虚拟桌面里的左上角 Y 坐标。

- public bool primary;
  - 是否为主显示器。

- public int refreshHz;
  - 刷新率（Hz；未知时为 0）。

- public int bitsPerPixel;
  - 颜色深度（每像素位数；未知时为 0）。


## Memory (class)

内存统计。跨平台：Windows 使用 GlobalMemoryStatusEx，
Linux 解析 /proc/meminfo；其他平台抛出
PlatformNotSupportedException.

- static MemoryStatus GetStatus()
  - 当前内存快照。

- [DllImport("kernel32", EntryPoint="GlobalMemoryStatusEx")]static extern int GlobalMemoryStatusEx(nint buf);

- static MemoryStatus WinStatus()
  - Windows 实现：GlobalMemoryStatusEx；调用失败时各字段保持 0。

- static MemoryStatus LinuxStatus()
  - Linux 实现：解析 /proc/meminfo；/proc/meminfo 读不到时抛
    PlatformNotSupportedException。totalVirtual/availVirtual 为
    物理 + 交换的估算值（Linux 无直接对应）。

- static long KbField(string txt, string key)
  - 从 /proc/meminfo 文本中取第一个 `key` 的 kB 值，缺失时为 0。

- static int IndexOf(string hay, string needle, int from)
  - 从 `from` 起查找 `needle` 的首次出现，找不到返回 -1。

- static long ParseLong(string s)
  - 十进制解析为 long；空串或含非数字返回 0（注意与其他文件解析器的 -1 约定不同）。

- static int CodeOf(string ch)
  - 取 `ch` 编码后首字节的值（用于判断 ASCII 数字）。


## MemoryStatus (class)

物理/虚拟内存快照，所有大小均以字节为单位。Linux 的值
来自 /proc/meminfo；Windows 来自 GlobalMemoryStatusEx。

- public long totalPhys;
  - 物理内存总量（字节）。

- public long availPhys;
  - 可用物理内存（字节）。

- public long totalPageFile;
  - 提交上限（物理 + 页面文件）（字节）。

- public long availPageFile;
  - 当前可提交量（字节）。

- public long totalVirtual;
  - 本进程可用虚拟地址空间总量的系统视图（字节；Linux 为物理+交换的估算）。

- public long availVirtual;
  - 剩余虚拟内存（字节）。

- public int usagePercent;
  - 物理内存使用率（0..100）。

- public bool IsLow()
  - 系统物理内存不足时为 True。


## Power (class)

电源/电池状态。Windows 调用 GetSystemPowerStatus；Linux 读取
/sys/class/power_supply（BAT* 条目）。其他平台抛出
PlatformNotSupportedException.

- static PowerStatus GetStatus()
  - 当前电源状态快照。

- static bool OnBattery()
  - 机器正在使用电池供电时为 True。

- [DllImport("kernel32", EntryPoint="GetSystemPowerStatus")]static extern int WinGetSystemPowerStatus(nint buf);

- static PowerStatus WinStatus()
  - Windows 实现：解析 GetSystemPowerStatus 的 SYSTEM_POWER_STATUS；
    无电池或数值未知时 percent/life 为 -1。

- static PowerStatus LinuxStatus()
  - Linux 实现：读 /sys/class/power_supply 的第一个 BAT* 电源；
    没有电池时 onBattery=false 且 percent/life 为 -1。

- static string SysField(string field)
  - 第一个 BAT* 电源：从 /sys/class/power_supply 读取 `field`。

- static string SysClass(string field)
  - 读第一个可用电池（BAT0 优先）的 `field` 属性；全缺时返回 ""。

- static string SysField2(string field)
  - BAT0 不存在时依次尝试 BAT1..BAT9。

- static int SysInt(string field)
  - 读数字属性；缺失或非法时为 -1。

- static int ParseInt(string s)
  - 十进制解析为 int；空串或含非数字返回 -1。

- static int CodeOf(string ch)
  - 取 `ch` 编码后首字节的值（用于判断 ASCII 数字）。


## PowerStatus (class)

机器及其电池的电源状态。

- public bool onBattery;
  - 使用电池供电（未接交流电）时为 True。

- public int batteryPercent;
  - 电池电量百分比（0..100；无电池时为 -1）。

- public long batteryLifeSeconds;
  - 剩余电池续航时间（秒；未知/无限时为 -1）。


## Registry (class)

Windows 注册表访问：打开/创建键、读写常用值类型
（DWORD/QWORD/SZ/EXPAND_SZ/MULTI_SZ/BINARY）、枚举子键和值、
删除键和值。根键以 HKLM/HKCU/... 常量传入。
该模块仅支持 Windows；其他平台抛出
PlatformNotSupportedException.

int k = Registry.OpenKey(Registry.Hkcu(), "Software\\MyApp", true);
Registry.SetDword(k, "Count", 3);
int n = Registry.GetDword(k, "Count");
Registry.CloseKey(k);

- static nint Hkcr()
  - 根键句柄 HKEY_CLASSES_ROOT（advapi32 预定义键）。

- static nint Hkcu()
  - 根键句柄 HKEY_CURRENT_USER。

- static nint Hklm()
  - 根键句柄 HKEY_LOCAL_MACHINE。

- static nint Hkusers()
  - 根键句柄 HKEY_USERS。

- static nint HkcurrentConfig()
  - 根键句柄 HKEY_CURRENT_CONFIG。

- static int TypeNone()
  - 值类型常量 REG_NONE（reg.h）。

- static int TypeSz()
  - 值类型常量 REG_SZ。

- static int TypeExpandSz()
  - 值类型常量 REG_EXPAND_SZ。

- static int TypeBinary()
  - 值类型常量 REG_BINARY。

- static int TypeDword()
  - 值类型常量 REG_DWORD。

- static int TypeMultiSz()
  - 值类型常量 REG_MULTI_SZ。

- static int TypeQword()
  - 值类型常量 REG_QWORD。

- [DllImport("advapi32", EntryPoint="RegOpenKeyExW")]static extern int RegOpenKeyExW(nint hKey, nint subKey, int options, int access, nint result);

- [DllImport("advapi32", EntryPoint="RegCreateKeyExW")]static extern int RegCreateKeyExW(nint hKey, nint subKey, int reserved, nint classBuf, int options, int access, nint secAttrs, nint result, nint disp);

- [DllImport("advapi32", EntryPoint="RegCloseKey")]static extern int RegCloseKey(nint hKey);

- [DllImport("advapi32", EntryPoint="RegQueryValueExW")]static extern int RegQueryValueExW(nint hKey, nint name, nint reserved, nint type, nint data, nint dataSize);

- [DllImport("advapi32", EntryPoint="RegSetValueExW")]static extern int RegSetValueExW(nint hKey, nint name, int reserved, int type, nint data, int dataSize);

- [DllImport("advapi32", EntryPoint="RegDeleteValueW")]static extern int RegDeleteValueW(nint hKey, nint name);

- [DllImport("advapi32", EntryPoint="RegDeleteKeyW")]static extern int RegDeleteKeyW(nint hKey, nint subKey);

- [DllImport("advapi32", EntryPoint="RegDeleteTreeW")]static extern int RegDeleteTreeW(nint hKey, nint subKey);

- [DllImport("advapi32", EntryPoint="RegEnumKeyExW")]static extern int RegEnumKeyExW(nint hKey, int index, nint nameBuf, nint nameSize, nint reserved, nint classBuf, nint classSize, nint lastWrite);

- [DllImport("advapi32", EntryPoint="RegEnumValueW")]static extern int RegEnumValueW(nint hKey, int index, nint nameBuf, nint nameSize, nint reserved, nint type, nint data, nint dataSize);

- static nint OpenKey(nint root, string subKey, bool write)
  - 以读权限打开已有键（`write` 为 true 时为写权限）。
    返回键句柄（非零）；当键
    不存在或访问被拒绝时返回 0。用 `CloseKey` 关闭。

- static nint CreateKey(nint root, string subKey)
  - 创建（或打开）一个键，缺失的父键会自动创建。
    返回键句柄（非零），失败时返回 0。

- static void CloseKey(nint key)
  - 关闭 OpenKey/CreateKey 打开的键句柄。

- static int GetDword(nint key, string name, int fallback)
  - 读取 DWORD 值。当值缺失或类型不同时
    返回 `fallback`。

- static void SetDword(nint key, string name, int val)
  - 写入 DWORD 值（不存在则创建）。

- static long GetQword(nint key, string name)
  - 读取 QWORD（64 位）值。缺失或类型错误时为 0。

- static void SetQword(nint key, string name, long val)
  - 写入 QWORD（64 位）值。

- static string GetString(nint key, string name)
  - 读取 REG_SZ 值。缺失时返回 ""。

- static void SetString(nint key, string name, string val)
  - 写入 REG_SZ 值。

- static void DeleteValue(nint key, string name)
  - 删除一个值（不存在时无操作）。

- static int DeleteKey(nint root, string subKey)
  - 删除子键及其下所有内容。成功返回 0，
    失败返回非零（键仍被打开，或不存在）。
    使用 RegDeleteTreeW，因此含嵌套子键的键树
    可一次调用删除（仅 RegDeleteKeyW 在
    Vista+ 上会因 ERROR_ACCESS_DENIED 拒绝非空键树）。

- static List<string> EnumKeys(nint key)
  - `key` 的所有子键名称。无子键时为空。

- static List<RegistryValue> EnumValues(nint key)
  - `key` 的所有值（名称/类型/原始字节）。无值时为空。

- static int WideLen(nint p)
  - 宽缓冲区中 NUL 终止符之前的 UTF-16 码元数
    （即 RegSetValueExW 所需的字节数，含终止符）。


## RegistryValue (class)

一个注册表值：`name`（默认值为 ""）、`type`（
`Registry` 的类型常量之一）以及原始字节负载。
用带类型的 getter 进行解码。

- public string name;
  - 值名；键的默认（未命名）值为 ""。

- public int type;
  - 值类型（`Registry` 的 Type* 常量之一）。

- public byte[]data;
  - 原始字节负载，按 `type` 解释。


## Storage (class)

磁盘/分区枚举与容量。Windows 遍历逻辑驱动器
（GetLogicalDriveStringsW + GetDiskFreeSpaceExW）；Linux 解析
/proc/mounts，并对每个挂载点调用 statvfs。其他平台抛出
PlatformNotSupportedException.

- static List<DiskUsage> Drives()
  - 所有固定/可移动磁盘（Windows）或已挂载文件系统
    （Linux）。Linux 会跳过伪文件系统（proc、sysfs、tmpfs、devpts、cgroup、
    overlay...）。

- static DiskUsage Root()
  - 系统盘（Windows）或根文件系统（Linux）。
    读取失败时返回空的 usage 对象。

- static DiskUsage Usage(string path)
  - 单个盘符（"C:"）或挂载点（"/"）的容量。

- [DllImport("kernel32", EntryPoint="GetLogicalDriveStringsW")]static extern int WinGetLogicalDriveStringsW(int len, nint buf);

- [DllImport("kernel32", EntryPoint="GetDriveTypeW")]static extern int WinGetDriveTypeW(nint root);

- [DllImport("kernel32", EntryPoint="GetDiskFreeSpaceExW")]static extern int WinGetDiskFreeSpaceExW(nint dir, nint avail, nint total, nint free);

- static List<DiskUsage> WinDrives()
  - Windows 实现：枚举逻辑驱动器，只保留固定/可移动盘中有容量的。

- static int WideLen(nint p)
  - 宽缓冲区中 NUL 终止符之前的 UTF-16 码元数（不含终止符）。

- static DiskUsage WinUsage(string root)
  - Windows 实现：GetDiskFreeSpaceExW；调用失败时三个字节数保持 0。

- [DllImport("crt", EntryPoint="statvfs")]static extern int StatvfsCall(nint path, nint stat);

- static List<DiskUsage> LinuxDrives()
  - Linux 实现：解析 /proc/mounts，对每个真实文件系统挂载点调 statvfs；
    /proc/mounts 读不到返回空列表。

- static bool IsRealFs(string fstype)
  - 对具有真实磁盘容量的文件系统返回 True。

- static DiskUsage Statvfs(string path)
  - Linux 实现：statvfs 读取挂载点容量；调用失败时三个字节数保持 0。

- static int IndexOf(string hay, string needle, int from)
  - 从 `from` 起查找 `needle` 的首次出现，找不到返回 -1。


## SystemInfo (class)

机器与操作系统的静态身份信息。Windows 从 Win32 API 读取
计算机/用户名和版本块；Linux 从
/etc/os-release、/proc/sys/kernel/* 和 USER 环境变量读取。
其他平台返回空/0 而非抛异常——这些都是纯查询，
安静的返回空结果比抛异常更友好。

- static string ComputerName()
  - 计算机（主机）名称。

- static string UserName()
  - 当前用户名。

- static string OsName()
  - 操作系统名称，如 "Windows 11 Pro" 或 "Ubuntu 22.04.3 LTS"。

- static string KernelVersion()
  - 内核版本，如 "10.0.22631" 或 "6.5.0-14-generic"。

- static long UptimeSeconds()
  - 系统运行时间（秒）。

- static string SystemDirectory()
  - 系统目录（Windows 下为 C:\Windows\System32）。

- static string WindowsDirectory()
  - Windows 目录（Windows 下为 C:\Windows）。

- [DllImport("kernel32", EntryPoint="GetComputerNameW")]static extern int WinGetComputerNameW(nint buf, nint size);

- [DllImport("advapi32", EntryPoint="GetUserNameW")]static extern int WinGetUserNameW(nint buf, nint size);

- [DllImport("kernel32", EntryPoint="GetSystemDirectoryW")]static extern int WinGetSystemDirectoryW(nint buf, int size);

- [DllImport("kernel32", EntryPoint="GetWindowsDirectoryW")]static extern int WinGetWindowsDirectoryW(nint buf, int size);

- [DllImport("kernel32", EntryPoint="GetTickCount64")]static extern long WinGetTickCount64();

- [DllImport("ntdll", EntryPoint="RtlGetVersion")]static extern int WinRtlGetVersion(nint info);

- static string WinOsName()
  - Windows 实现：RtlGetVersion 版本块映射为产品名（按 build 区分
    Win10/11），后接 Service Pack 名。

- static string WinKernelVersion()
  - Windows 实现："major.minor.build" 形式；RtlGetVersion 失败返回 ""。

- static string OsRelease(string key)
  - /etc/os-release 中第一个 `key="value"` 条目（已去除引号）。

- static string ProcText(string path)
  - 读取文本文件；读不到（路径不存在、权限不足）返回 ""。

- static int IndexOf(string hay, string needle, int from)
  - 从 `from` 起查找 `needle` 的首次出现，找不到返回 -1。

- static long ParseLong(string s)
  - 十进制解析为 long；空串或含非数字返回 -1。

- static int CodeOf(string ch)
  - 取 `ch` 编码后首字节的值（用于判断 ASCII 数字）。


## TaskInfo (class)

单个计划任务快照。`name` 为完整任务路径（如
"\Microsoft\Windows\Defrag\ScheduledDefrag"）。`status` 为操作系统状态
文本（"Ready"、"Running"、"Disabled"...）。`command` 为任务运行的
程序；仅由 Get() 填充（列表视图省略它）。

- public string name;
  - 完整任务路径（Windows）或任务名 / "cron:<行号>"（POSIX）。

- public string nextRun;
  - 下次运行时间的操作系统文本表示。

- public string status;
  - 操作系统状态文本（"Ready"、"Running"、"Disabled"...）。

- public string command;
  - 任务运行的程序（含参数）；仅 Get() 填充，List() 留空。


## TaskScheduler (class)

计划任务：枚举、查询、创建、删除、运行和停止。
Windows 上基于内置的 schtasks.exe（其 /fo CSV 与 /fo LIST /v
输出跨版本稳定）；Linux/macOS 上基于当前用户的 crontab，
每个由本类创建的条目前面带一行标记注释
`# zan-task: <name>`，以便按名字查询/删除；其他人手写的
cron 条目以 "cron:<行号>" 的名字只读列出，不会被改写。

创建或删除任务需要管理员权限（POSIX 上只需当前用户的
crontab 可写）；Run/End 仅需访问该特定任务。失败返回 false
（从不抛异常）。

触发器字符串遵循 schtasks /sc 值："ONLOGON"（登录时）、"ONSTART"
（系统启动时）、"ONIDLE"、"DAILY"、"WEEKLY"、"ONCE"、"MONTHLY"。DAILY、
WEEKLY、ONCE 和 MONTHLY 还需要时间 "HH:MM"（以及可选的日期
"MM/DD/YYYY"）。cron 没有等价的 ONLOGON/ONIDLE/ONCE 触发器，
在 POSIX 上传这三个会返回 false（不会静默建一个永不触发的条目）。

- static List<TaskInfo> List()
  - 当前用户可见的所有计划任务。

- static TaskInfo Get(string name)
  - 按名称获取单个任务；不存在时返回 null。

- static bool Exists(string name)
  - 存在同名任务时返回 True。

- static bool Create(string name, string command, string trigger, string time)
  - 创建任务。`command` 为要运行的程序（及参数）；
    含空格的路径请自行加引号，例如
    "\"C:\\Program Files\\x\\y.exe\" --flag"。`trigger` 为 schtasks
    /sc 值；DAILY/WEEKLY/ONCE/MONTHLY 需要 `time`（"HH:MM"）
    ，其余情况忽略。成功返回 True。

- static bool Delete(string name)
  - 删除任务（需管理员权限）。成功返回 True。

- static bool Run(string name)
  - 立即运行任务。运行被接受时返回 True。

- static bool End(string name)
  - 停止正在运行的任务。POSIX 上按命令行 pkill 匹配；
    任务未在运行也返回 true（与 schtasks /end 语义一致）。

- static string Marker(string name)
  - 本类写入的条目前的标记行。

- static string MarkerName(string line)
  - 行是标记行时返回任务名，否则返回 ""。

- static List<string> CrontabLines()
  - 当前用户的 crontab 全部行；没有 crontab 时返回空列表。

- static bool WriteCrontab(List<string> lines)
  - 把行列表整体写回当前用户的 crontab（经临时文件加
    `crontab <file>`）；任一步失败返回 false。

- static List<string> WithoutTask(List<string> lines, string name)
  - 从 crontab 行列表中移除任务 `name`：删掉标记行与紧随的 cron 行，其余行保留。

- static string CronExpr(string trigger, string time)
  - schtasks 的 /sc 值映射到 cron 表达式；无等价表达
    （ONLOGON/ONIDLE/ONCE 或时间非法）返回 ""。

- static int TimePart(string time, int index)
  - 从 "HH:MM" 取第 `index` 个字段（0=小时，1=分钟）；无效时返回 -1。

- static int ParseInt(string s)
  - 十进制解析为 int；空串或含非数字返回 -1。

- static string Upper(string s)
  - ASCII 字母转大写，其余字符原样保留。

- static string CronSchedule(string line)
  - cron 行的调度字段：要么 "@reboot" 这类 @形式，要么前五个时间字段。

- static string CronCommand(string line)
  - cron 行的命令部分（时间字段之后的剩余文本）。

- static string FieldSplit(string line, int count, bool head)
  - 取前 `count` 个空白分隔字段（head=true），或它们后面的剩余部分。

- static bool IsSpace(int ch)
  - 是否空白字符（空格或制表）。

- static string ShellQuote(string s)
  - 单引号引用：内部的 ' 拆成 '\'' 以保持字面量。

- static bool SchtasksOk(string cmd)
  - 运行 schtasks 命令并按输出判断成败（含 "ERROR"/"Error" 行即失败）。

- static string Quote(string s)
  - 双引号包裹（schtasks /tn /tr 参数用）。

- static string AfterColon(string line)
  - 第一个冒号之后的所有内容，去掉两端空白。

- static List<string> ParseCsv(string line)
  - 解析一行 CSV："a","b","c" -> [a, b, c]。带引号的字段可能
    包含逗号；成对引号（""）表示字面引号。

- static string FirstNonEmpty(List<string> lines)
  - 返回第一条非空行；全是空行时返回 null。

- static bool StartsWith(string s, string prefix)
  - `s` 是否以 `prefix` 开头（区分大小写）。

- static string Trim(string s)
  - 去掉两端空白（空格/制表/回车/换行）。

- static int IndexOf(string hay, string needle, int from)
  - 从 `from` 起查找 `needle` 的首次出现，找不到返回 -1。


## int (delegate)

原生显示器枚举回调（WINAPI）。

`delegate int MonitorEnumProc(nint monitor, nint hdc, nint rect, nint data);`
