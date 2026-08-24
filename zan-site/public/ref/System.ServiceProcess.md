# System.ServiceProcess

> 源码: `stdlib/System/ServiceProcess/ServiceProcess.zan`


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

- static int ParseState(string line)

- static string AfterColon(string line)

- static string FirstNonEmpty(List<string> lines)

- static bool StartsWith(string s, string prefix)

- static string Trim(string s)

- static int ParseInt(string s)

- static List<ServiceInfo> SystemctlList(List<ServiceInfo> list)

- static int SystemctlPid(string name)

- static bool Systemctl(string verb, string name)

- static bool EndsWith(string s, string tail)

- static int IndexOf(string hay, string needle, int from)


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
