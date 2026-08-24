# System.Management

> 源码: `stdlib/System/Management/Cpu.zan`, `stdlib/System/Management/Device.zan`, `stdlib/System/Management/Display.zan`, `stdlib/System/Management/Memory.zan`, `stdlib/System/Management/Power.zan`, `stdlib/System/Management/Registry.zan`, `stdlib/System/Management/Storage.zan`, `stdlib/System/Management/SystemInfo.zan`, `stdlib/System/Management/TaskScheduler.zan`


## Cpu (class)

CPU 标识与负载。Windows 从注册表读取品牌/频率
（即 Windows 缓存在
HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor 下的 CPUID 值），核心数来自
GetSystemInfo，负载来自 GetSystemTimes。Linux 读取
/proc/cpuinfo 和 /proc/stat。其他平台抛出
PlatformNotSupportedException.

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
  - 枚举所有安装类中当前存在的设备。

- static List<DeviceInfo> All()
  - 枚举所有安装类中存在和不存在的设备。

- static List<DeviceInfo> List(bool presentOnly)
  - 枚举所有安装类，可选择仅返回
    当前存在的设备。

- static List<DeviceInfo> SysfsList()

- static string SysfsUevent(string dir, string key)

- static string SysfsFirst(string dir, string names)

- static List<string> ParseMultiString(nint payload, int bytes)

- [DllImport("setupapi", EntryPoint="SetupDiGetClassDevsW")]static extern nint SetupDiGetClassDevsW(nint classGuid, nint enumerator, nint parent, int flags);

- [DllImport("setupapi", EntryPoint="SetupDiEnumDeviceInfo")]static extern int SetupDiEnumDeviceInfo(nint devs, int index, nint data);

- [DllImport("setupapi", EntryPoint="SetupDiGetDeviceInstanceIdW")]static extern int SetupDiGetDeviceInstanceIdW(nint devs, nint data, nint buffer, int chars, nint requiredChars);

- [DllImport("setupapi", EntryPoint="SetupDiGetDeviceRegistryPropertyW")]static extern int SetupDiGetDeviceRegistryPropertyW(nint devs, nint data, int property, nint textType, nint buffer, int bytes, nint requiredBytes);

- [DllImport("setupapi", EntryPoint="SetupDiDestroyDeviceInfoList")]static extern int SetupDiDestroyDeviceInfoList(nint devs);

- static List<DeviceInfo> WinList(bool presentOnly)

- static string InstanceId(nint devs, nint data)

- static string StringProperty(nint devs, nint data, int property)

- static List<string> MultiStringProperty(nint devs, nint data, int property)

- static nint PropertyBuffer(nint devs, nint data, int property)


## DeviceInfo (class)

单个设备的只读信息（Windows 即插即用设备或
Linux sysfs 设备节点）。

- public string instanceId;

- public string classGuid;

- public string className;

- public string friendlyName;

- public string description;

- public string manufacturer;

- public List<string> hardwareIds;


## DiskUsage (class)

单个磁盘/挂载点的容量信息。`name` 为盘符（"C:"）
（Windows）或挂载点（"/"、"/home"）（Linux）；大小单位为
字节。

- public string name;

- public long totalBytes;

- public long freeBytes;

- public long availBytes;
  - 普通用户可用的字节数（不含保留块）。

- public int UsagePercent()


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

- static List<int> Ints(string line)
  - 一行里的所有十进制整数（按出现顺序）。

- static int FirstInt(string line)

- [DllImport("user32", EntryPoint="GetSystemMetrics")]static extern int WinGetSystemMetrics(int index);

- [DllImport("user32", EntryPoint="EnumDisplaySettingsW")]static extern int WinEnumDisplaySettingsW(nint name, int mode, nint devMode);

- [DllImport("user32", EntryPoint="EnumDisplayMonitors")]static extern int WinEnumDisplayMonitors(nint hdc, nint clip, MonitorEnumProc proc, nint data);

- [DllImport("user32", EntryPoint="GetMonitorInfoW")]static extern int WinGetMonitorInfoW(nint monitor, nint info);

- [DllImport("user32", EntryPoint="MonitorFromPoint")]static extern nint WinMonitorFromPoint(nint pt, int flags);

- static MonitorEnumProc monitorProc;

- static List<DisplayInfo> monitorResults;

- static int WinMonitorProc(nint monitor, nint hdc, nint rect, nint data)

- static int WinMode(int mode, int offset)
  - 读取主屏当前 DEVMODEW 的 DWORD 字段。


## DisplayInfo (class)

单个显示器：索引（0 = 主屏）、名称、像素尺寸、刷新率和
颜色深度。

- public int index;

- public string name;

- public int width;

- public int height;

- public int x;
  - 该显示器在虚拟桌面里的左上角坐标。

- public int y;

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

- static MemoryStatus LinuxStatus()

- static long KbField(string txt, string key)
  - 从 /proc/meminfo 文本中取第一个 `key` 的 kB 值，缺失时为 0。

- static int IndexOf(string hay, string needle, int from)

- static long ParseLong(string s)

- static int CodeOf(string ch)


## MemoryStatus (class)

物理/虚拟内存快照，所有大小均以字节为单位。Linux 的值
来自 /proc/meminfo；Windows 来自 GlobalMemoryStatusEx。

- public long totalPhys;

- public long availPhys;

- public long totalPageFile;

- public long availPageFile;

- public long totalVirtual;

- public long availVirtual;

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

- static PowerStatus LinuxStatus()

- static string SysField(string field)
  - 第一个 BAT* 电源：从 /sys/class/power_supply 读取 `field`。

- static string SysClass(string field)

- static string SysField2(string field)
  - BAT0 不存在时依次尝试 BAT1..BAT9。

- static int SysInt(string field)

- static int ParseInt(string s)

- static int CodeOf(string ch)


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
  - 根键句柄（advapi32 预定义键）。

- static nint Hkcu()

- static nint Hklm()

- static nint Hkusers()

- static nint HkcurrentConfig()

- static int TypeNone()
  - 值类型常量（reg.h）。

- static int TypeSz()

- static int TypeExpandSz()

- static int TypeBinary()

- static int TypeDword()

- static int TypeMultiSz()

- static int TypeQword()

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

- public int type;

- public byte[]data;


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

- static int WideLen(nint p)

- static DiskUsage WinUsage(string root)

- [DllImport("crt", EntryPoint="statvfs")]static extern int StatvfsCall(nint path, nint stat);

- static List<DiskUsage> LinuxDrives()

- static bool IsRealFs(string fstype)
  - 对具有真实磁盘容量的文件系统返回 True。

- static DiskUsage Statvfs(string path)

- static int IndexOf(string hay, string needle, int from)


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

- static string WinKernelVersion()

- static string OsRelease(string key)
  - /etc/os-release 中第一个 `key="value"` 条目（已去除引号）。

- static string ProcText(string path)

- static int IndexOf(string hay, string needle, int from)

- static long ParseLong(string s)

- static int CodeOf(string ch)


## TaskInfo (class)

单个计划任务快照。`name` 为完整任务路径（如
"\Microsoft\Windows\Defrag\ScheduledDefrag"）。`status` 为操作系统状态
文本（"Ready"、"Running"、"Disabled"...）。`command` 为任务运行的
程序；仅由 Get() 填充（列表视图省略它）。

- public string name;

- public string nextRun;

- public string status;

- public string command;


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
  - 停止正在运行的任务。停止被接受时返回 True。

- static string Marker(string name)

- static string MarkerName(string line)

- static List<string> CrontabLines()

- static bool WriteCrontab(List<string> lines)

- static List<string> WithoutTask(List<string> lines, string name)

- static string CronExpr(string trigger, string time)

- static int TimePart(string time, int index)

- static int ParseInt(string s)

- static string Upper(string s)

- static string CronSchedule(string line)

- static string CronCommand(string line)

- static string FieldSplit(string line, int count, bool head)

- static bool IsSpace(int ch)

- static string ShellQuote(string s)

- static bool SchtasksOk(string cmd)

- static string Quote(string s)

- static string AfterColon(string line)

- static List<string> ParseCsv(string line)

- static string FirstNonEmpty(List<string> lines)

- static bool StartsWith(string s, string prefix)

- static string Trim(string s)

- static int IndexOf(string hay, string needle, int from)


## int (delegate)

原生显示器枚举回调（WINAPI）。

`delegate int MonitorEnumProc(nint monitor, nint hdc, nint rect, nint data);`
