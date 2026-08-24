# System.Security

> 源码: `stdlib/System/Security/Guard.zan`


## Guard (class)

面向已发布 Zan 程序的简单跨平台用户态防篡改层
（不仅限游戏）。启动时调用一次 <c>Guard.Arm()</c>：它会尽可能隐藏
主线程，使调试器无法附加；执行一次性的反附加
检查，并启动低开销的后台看门狗（每秒一次），
检测之后附加的调试器以及明显的时钟篡改（变速外挂），
触发时静默终止进程。

局限——这里刻意做成"简单防御"。它能挡住随手的 Cheat-Engine
调试器附加和变速工具，但挡不住坚决的逆向工程师
或只读写内存、不附加调试器的内存扫描器。
真正阻止其他进程读取你的内存需要
内核驱动（仅 Windows，EV 签名），不在范围内；在 macOS/Linux 上
该途径基本不可用。对于扫描器可能瞄准的值，
请用 `Protected` 包装，使篡改能被检测并拒绝。

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
  - 静默终止进程；检测到任何篡改时调用。

- static void Arm()
  - 启动防护：一次性的反附加、计时基准以及后台
    看门狗。可安全调用一次；重复调用会被忽略。

- static void CheckOnce()

- static void Check()
  - 公开的逐次检查。开销低，可选。检测调试器附加。

- static void Watch()


## Protected (class)

面向内存扫描器可能针对的值的防篡改整数（分数、
金币、黄金）。明文从不存储：它以 XOR 掩码形式与
冗余的反向副本一起存放，因此工具在内存中找到并覆写该数值时，
会破坏这一不变式，`Get` 将触发防护。请
只用于少数值得保护的值，而不是每帧的热点计算。

- int mask;

- int chk;

- static int Key()

- static Protected Of(int v)
  - 创建一个初始化为 <paramref name="v"/> 的受保护值。

- void Set(int v)
  - 存储新值并刷新完整性校验。

- int Get()
  - 返回存储的值；若两份副本不一致则触发防护
    （即内存被外部修改）。

- void Add(int d)
  - 将 <paramref name="d"/> 加到存储的值上。
