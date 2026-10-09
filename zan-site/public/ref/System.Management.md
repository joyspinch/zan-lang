# System.Management

> 源码: `packages/Zan.Desktop/src/System/Management/Cpu.zan`, `packages/Zan.Desktop/src/System/Management/Device.zan`, `packages/Zan.Desktop/src/System/Management/Display.zan`, `packages/Zan.Desktop/src/System/Management/Memory.zan`, `packages/Zan.Desktop/src/System/Management/Power.zan`, `packages/Zan.Desktop/src/System/Management/Registry.zan`, `packages/Zan.Desktop/src/System/Management/Storage.zan`, `packages/Zan.Desktop/src/System/Management/SystemInfo.zan`, `packages/Zan.Desktop/src/System/Management/TaskScheduler.zan`


## Cpu (class)

- [DllImport("kernel32", EntryPoint="Sleep")]static extern void SleepW(int ms);

- [DllImport("crt", EntryPoint="usleep")]static extern int Usleep(int us);

- static string Brand()

- static string Vendor()

- static int FrequencyMHz()

- static int LogicalCores()

- static int Usage()

- [DllImport("kernel32", EntryPoint="GetSystemInfo")]static extern void WinGetSystemInfo(nint info);

- [DllImport("kernel32", EntryPoint="GetSystemTimes")]static extern int WinGetSystemTimes(nint idleTime, nint kernelTime, nint userTime);

- [DllImport("advapi32", EntryPoint="RegOpenKeyExW")]static extern int RegOpenKeyExW(nint hKey, nint subKey, int options, int access, nint result);

- [DllImport("advapi32", EntryPoint="RegQueryValueExW")]static extern int RegQueryValueExW(nint hKey, nint name, nint reserved, nint type, nint data, nint dataSize);

- [DllImport("advapi32", EntryPoint="RegCloseKey")]static extern int RegCloseKey(nint hKey);

- static string RegString(nint root, string subKey, string name)

- static int RegDword(nint root, string subKey, string name)

- [DllImport("crt", EntryPoint="sysctlbyname")]static extern int SysctlByName(string name, nint oldp, nint oldlenp, nint newp, int newlen);

- static string SysctlString(string name)

- static int SysctlInt32(string name)

- static long SysctlInt64(string name)

- static long[]CpTime()

- static string ProcField(string path, string field)

- static int CountCpuLines()

- static long[]CpuJiffies()

- static int IndexOf(string hay, string needle, int from)

- static long ParseLong(string s)

- static int ParseInt(string s)

- static int CodeOf(string ch)


## Device (class)

- static List<DeviceInfo> Present()

- static List<DeviceInfo> All()

- static bool HasIndirectDisplayAdapter()

- static string DisplayClassGuid="{ 4D36E968E32511CEBFC108002BE10318}";

- static List<string> IddMarkers()

- static bool WinHasIddAdapter()

- static List<DeviceInfo> List(bool presentOnly)

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

- public string instanceId;

- public string classGuid;

- public string className;

- public string friendlyName;

- public string description;

- public string manufacturer;

- public List<string> hardwareIds;


## DiskUsage (class)

- public string name;

- public long totalBytes;

- public long freeBytes;

- public long availBytes;

- public int UsagePercent()


## Display (class)

- static int ScreenWidth()

- static int ScreenHeight()

- static int RefreshRate()

- static int ColorDepth()

- static DisplayInfo Primary()

- static List<DisplayInfo> Monitors()

- static int MacLogicalDpi()

- static JsonValue MacSnapshot()

- static void ReadXrandr(List<DisplayInfo> list)

- static void ReadWlrRandr(List<DisplayInfo> list)

- static void ReadXdpyinfo(List<DisplayInfo> list)

- static bool Geometry(string line, DisplayInfo d)

- static int RateOf(string line)

- static bool IsDigit(int c)

- static List<int> Ints(string line)

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


## DisplayInfo (class)

- public int index;

- public string name;

- public int width;

- public int height;

- public int x;

- public int y;

- public bool primary;

- public int refreshHz;

- public int bitsPerPixel;


## Memory (class)

- static MemoryStatus GetStatus()

- [DllImport("kernel32", EntryPoint="GlobalMemoryStatusEx")]static extern int GlobalMemoryStatusEx(nint buf);

- static MemoryStatus WinStatus()

- [DllImport("crt", EntryPoint="sysctlbyname")]static extern int SysctlByName(string name, nint oldp, nint oldlenp, nint newp, int newlen);

- static MemoryStatus MacStatus()

- static string SysctlString(string name)

- static int SysctlI32(string name)

- static long SysctlU64(string name)

- static long SwapField(string txt, string key)

- static int IndexOf(string hay, string needle, int from)

- static MemoryStatus LinuxStatus()

- static long KbField(string txt, string key)

- static int IndexOf(string hay, string needle, int from)

- static long ParseLong(string s)

- static int CodeOf(string ch)


## MemoryStatus (class)

- public long totalPhys;

- public long availPhys;

- public long totalPageFile;

- public long availPageFile;

- public long totalVirtual;

- public long availVirtual;

- public int usagePercent;

- public bool IsLow()


## Power (class)

- static PowerStatus GetStatus()

- static bool OnBattery()

- [DllImport("kernel32", EntryPoint="GetSystemPowerStatus")]static extern int WinGetSystemPowerStatus(nint buf);

- static PowerStatus WinStatus()

- static PowerStatus LinuxStatus()

- static string SysField(string field)

- static string SysClass(string field)

- static string SysField2(string field)

- static int SysInt(string field)

- static int ParseInt(string s)

- static int CodeOf(string ch)

- static PowerStatus MacStatus()

- static int IndexOf(string hay, string needle, int from)


## PowerStatus (class)

- public bool onBattery;

- public int batteryPercent;

- public long batteryLifeSeconds;


## Registry (class)

- static nint Hkcr()

- static nint Hkcu()

- static nint Hklm()

- static nint Hkusers()

- static nint HkcurrentConfig()

- static int TypeNone()

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

- static nint CreateKey(nint root, string subKey)

- static void CloseKey(nint key)

- static int GetDword(nint key, string name, int fallback)

- static void SetDword(nint key, string name, int val)

- static long GetQword(nint key, string name)

- static void SetQword(nint key, string name, long val)

- static string GetString(nint key, string name)

- static void SetString(nint key, string name, string val)

- static void DeleteValue(nint key, string name)

- static int DeleteKey(nint root, string subKey)

- static List<string> EnumKeys(nint key)

- static List<RegistryValue> EnumValues(nint key)

- static int WideLen(nint p)


## RegistryValue (class)

- public string name;

- public int type;

- public byte[]data;


## Storage (class)

- static List<DiskUsage> Drives()

- static DiskUsage Root()

- static DiskUsage Usage(string path)

- [DllImport("kernel32", EntryPoint="GetLogicalDriveStringsW")]static extern int WinGetLogicalDriveStringsW(int len, nint buf);

- [DllImport("kernel32", EntryPoint="GetDriveTypeW")]static extern int WinGetDriveTypeW(nint root);

- [DllImport("kernel32", EntryPoint="GetDiskFreeSpaceExW")]static extern int WinGetDiskFreeSpaceExW(nint dir, nint avail, nint total, nint free);

- static List<DiskUsage> WinDrives()

- static int WideLen(nint p)

- static DiskUsage WinUsage(string root)

- [DllImport("crt", EntryPoint="statvfs")]static extern int StatvfsCall(nint path, nint stat);

- static DiskUsage Statvfs(string path)

- static int IndexOf(string hay, string needle, int from)

- static List<DiskUsage> LinuxDrives()

- static bool IsRealFs(string fstype)

- static List<DiskUsage> MacDrives()

- static bool IsMacDiskFs(string fstype)


## SystemInfo (class)

- static string ComputerName()

- static string UserName()

- static string OsName()

- static string KernelVersion()

- static long UptimeSeconds()

- static string SystemDirectory()

- static string WindowsDirectory()

- [DllImport("kernel32", EntryPoint="GetComputerNameW")]static extern int WinGetComputerNameW(nint buf, nint size);

- [DllImport("advapi32", EntryPoint="GetUserNameW")]static extern int WinGetUserNameW(nint buf, nint size);

- [DllImport("kernel32", EntryPoint="GetSystemDirectoryW")]static extern int WinGetSystemDirectoryW(nint buf, int size);

- [DllImport("kernel32", EntryPoint="GetWindowsDirectoryW")]static extern int WinGetWindowsDirectoryW(nint buf, int size);

- [DllImport("kernel32", EntryPoint="GetTickCount64")]static extern long WinGetTickCount64();

- [DllImport("ntdll", EntryPoint="RtlGetVersion")]static extern int WinRtlGetVersion(nint info);

- static string WinOsName()

- static string WinKernelVersion()

- [DllImport("crt", EntryPoint="sysctlbyname")]static extern int SysctlByName(string name, nint oldp, nint oldlenp, nint newp, int newlen);

- [DllImport("crt", EntryPoint="time")]static extern long NowTime(nint tloc);

- static string SysctlString(string name)

- static string OsRelease(string key)

- static string ProcText(string path)

- static int IndexOf(string hay, string needle, int from)

- static long ParseLong(string s)

- static int CodeOf(string ch)


## TaskInfo (class)

- public string name;

- public string nextRun;

- public string status;

- public string command;


## TaskScheduler (class)

- static List<TaskInfo> List()

- static TaskInfo Get(string name)

- static bool Exists(string name)

- static bool ValidName(string name)

- static bool ValidTrigger(string trigger)

- static bool ValidTime(string time)

- static int TwoDigit(string s, int at)

- static string RandomToken()

- static bool Create(string name, string command, string trigger, string time)

- static bool Delete(string name)

- static bool Run(string name)

- static bool End(string name)

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

- static bool SchtasksOk(List<string> args)

- static string Quote(string s)

- static string AfterColon(string line)

- static List<string> ParseCsv(string line)

- static string FirstNonEmpty(List<string> lines)

- static bool StartsWith(string s, string prefix)

- static string Trim(string s)

- static int IndexOf(string hay, string needle, int from)


## int (delegate)

`delegate int MonitorEnumProc(nint monitor, nint hdc, nint rect, nint data);`
