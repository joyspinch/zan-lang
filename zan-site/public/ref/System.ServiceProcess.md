# System.ServiceProcess

> 源码: `packages/Zan.Diagnostics/src/System/ServiceProcess/ServiceProcess.zan`


## ServiceInfo (class)

- public string name;

- public string displayName;

- public int state;

- public int processId;


## ServiceProcess (class)

- [DllImport("kernel32", EntryPoint="GetLastError")]static extern int WinLastError();

- static List<ServiceInfo> List()

- static ServiceInfo Get(string name)

- static bool Start(string name)

- static bool Stop(string name)

- static bool Restart(string name)

- static async Task<bool> RestartAsync(string name)

- static bool Delete(string name)

- static string StateName(int state)

- static bool ScOk(string verb, string name)

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

- static List<ServiceInfo> LaunchctlList(List<ServiceInfo> list)

- static bool Launchctl(string verb, string label)

- static int IndexOf(string hay, string needle, int from)


## ServiceState (enum)

- Unknown = =0

- Stopped = =1

- StartPending = =2

- StopPending = =3

- Running = =4

- ContinuePending = =5

- PausePending = =6

- Paused = =7
