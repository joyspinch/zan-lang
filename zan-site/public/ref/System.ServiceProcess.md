# System.ServiceProcess

> 源码: `packages/Zan.Diagnostics/src/System/ServiceProcess/ServiceProcess.zan`


## ServiceInfo (class)

One service snapshot. `state` uses the SCM state codes: 1 = stopped,
2 = start pending, 3 = stop pending, 4 = running, 5 = continue pending,
6 = pause pending, 7 = paused. `processId` is the service's PID (0 when
not running). `name` is the service name used by Start/Stop/Get.

- public string name;

- public string displayName;

- public int state;

- public int processId;


## ServiceProcess (class)

服务控制静态入口（Windows 走 sc.exe 文本接口——SCM 原生枚举布局
随系统版本脆弱；Linux 包装 systemctl(1)；其余平台抛
PlatformNotSupportedException）。所有操作失败返回 false/null，
从不抛异常。

- [DllImport("kernel32", EntryPoint="GetLastError")]static extern int WinLastError();

- static List<ServiceInfo> List()
  - Every service (name, display name, state, PID).

- static ServiceInfo Get(string name)
  - One service by name; null when it does not exist.

- static bool Start(string name)
  - Starts a service. True on success (or when already
    running).

- static bool Stop(string name)
  - Stops a running service. True on success.

- static bool Restart(string name)
  - Stops then starts a service. True when the start
    succeeded.

- static bool Delete(string name)
  - Deletes a service. Requires administrator rights. True on
    success.

- static string StateName(int state)
  - Human-readable name of a service state code.

- static bool ScOk(string cmd)
  - sc 命令是否成功：捕获 stderr 后看首行是否含 FAILED
    （成功行是 SERVICE_NAME: / STATE: 4 RUNNING 等）。

- static int ParseState(string line)
  - 从 "STATE : 4 RUNNING" 行提取数字状态码；无冒号时返回 1（stopped）。

- static string AfterColon(string line)
  - 返回第一个冒号之后的文本；无冒号时返回空串。

- static string FirstNonEmpty(List<string> lines)
  - 返回首个非空白行；没有非空行时返回 null。

- static bool StartsWith(string s, string prefix)
  - 前缀匹配。

- static string Trim(string s)
  - 去除首尾空白（空格/制表/回车/换行）。

- static int ParseInt(string s)
  - 解析前导非负整数，非数字处停止。

- static List<ServiceInfo> SystemctlList(List<ServiceInfo> list)
  - 解析 systemctl list-units 输出为服务列表；行内出现子串
    `active`（`activating` 亦匹配）记为 4（running），否则 1（stopped），
    PID 由 SystemctlPid 补齐。

- static int SystemctlPid(string name)
  - 查询服务主进程 PID（`systemctl show -p MainPID --value`）；
    服务未运行或查询无输出时返回 0。

- static bool Systemctl(string verb, string name)
  - 执行 systemctl 子命令；输出首行不含 `error` 或无输出视为成功。

- static bool EndsWith(string s, string tail)
  - 后缀匹配。

- static int IndexOf(string hay, string needle, int from)
  - 从 from 起查找子串首次出现位置，未找到返回 -1。


## ServiceState (enum)

Windows service control: enumerate, query, start, stop, restart and
delete services. Windows shells out to the built-in sc.exe (stable text
interface; the raw SCM enum layout is version-fragile), Linux wraps
systemctl(1); other platforms throw PlatformNotSupportedException.

Start/Stop/Restart/Delete need enough privileges: admin for Delete and
for starting/stopping system services, at least SERVICE_START/STOP on
the target service otherwise. Failures return false (never throw).


Win32 SERVICE_STATUS state codes: 1 = stopped, 2 = start pending,
3 = stop pending, 4 = running, 5 = continue pending, 6 = pause
pending, 7 = paused. `ServiceInfo.state` carries these codes as ints
(historical API); the names live here so `ServiceProcess.StateName`
can go through the compiler-lowered enum ToString.

- Unknown = =0

- Stopped = =1

- StartPending = =2

- StopPending = =3

- Running = =4

- ContinuePending = =5

- PausePending = =6

- Paused = =7
