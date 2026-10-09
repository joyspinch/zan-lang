# System.Security

> 源码: `packages/Zan.Security/src/System/Security/Guard.zan`


## Guard (class)

- [DllImport("kernel32", EntryPoint="IsDebuggerPresent")]static extern int WinIsDebuggerPresent();

- [DllImport("kernel32", EntryPoint="GetCurrentProcess")]static extern nint WinGetCurrentProcess();

- [DllImport("kernel32", EntryPoint="CheckRemoteDebuggerPresent")]static extern int WinCheckRemoteDebuggerPresent(nint hProc, string pbOut);

- [DllImport("kernel32", EntryPoint="QueryPerformanceCounter")]static extern int WinQpc(string outCount);

- [DllImport("kernel32", EntryPoint="QueryPerformanceFrequency")]static extern int WinQpf(string outFreq);

- [DllImport("kernel32", EntryPoint="GetTickCount64")]static extern long WinGetTickCount64();

- [DllImport("kernel32", EntryPoint="Sleep")]static extern void WinSleep(int ms);

- [DllImport("crt", EntryPoint="ptrace")]static extern int PosixPtrace(int request, int pid, nint addr, nint data);

- [DllImport("crt", EntryPoint="exit")]static extern void CrtExit(int code);

- [DllImport("crt", EntryPoint="calloc")]static extern string GuardCalloc(long count, long size);

- [DllImport("crt", EntryPoint="free")]static extern void GuardFree(string ptr);

- static bool armed;

- static int strikes;

- static long baseTick;

- static long baseQpc;

- static long qpcFreq;

- static long Le64(string b)

- static void Trip()

- static void Arm()

- static void CheckOnce()

- static void Check()

- static void Watch()


## Protected (class)

- int mask;

- int chk;

- static int Key()

- static Protected Of(int v)

- void Set(int v)

- int Get()

- void Add(int d)
