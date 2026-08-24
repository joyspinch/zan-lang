# System.Diagnostics

> 源码: `stdlib/System/Diagnostics/Log.zan`, `stdlib/System/Diagnostics/Privileges.zan`, `stdlib/System/Diagnostics/Process.zan`, `stdlib/System/Diagnostics/ProcessControl.zan`, `stdlib/System/Diagnostics/ProcessHost.zan`, `stdlib/System/Diagnostics/ProcessList.zan`, `stdlib/System/Diagnostics/ServerMetrics.zan`, `stdlib/System/Diagnostics/Stopwatch.zan`


## ErrEntry (class)

One recorded server-side error: when it happened (epoch seconds),
where ("METHOD path"), and the message. Kept so an operator can see recent
failures in the backend even when the console is off.

- long ts;

- string origin;

- string detail;

- ErrEntry(long ts, string origin, string detail)


## Log (class)

进程级滚动文件日志。写文件是日志的默认去处：服务跑在后台（worker、
守护进程、Windows 上没有控制台）时，只写 stdout 等于什么都没记，
出事后无从查起。

文件名是一个**模式**，每次写入时展开，展开结果变化即为一次滚动：
{yyyy} 年   {MM} 月   {dd} 日   {HH} 时
{pid}  进程号        {wid} worker 序号（master 为 0）
模式里可以带目录分隔符，缺的目录在写入前自动递归创建，因此默认的
`"{yyyy}{MM}/{dd}.log"` 会按月分目录、按日分文件（logs/202608/13.log）。
例如 `"{yyyy}-{MM}-{dd}.log"` 每天一个文件（平铺），
`"{yyyy}{MM}/{dd}-{HH}.log"` 按月分目录、每小时一个。
另外可设上限字节数，超过就切到 `名字.1`、`名字.2` …（见 MaxFileSize）。

同一个文件可以被多个进程共写：每条记录都是「打开→追加→关闭」，不长期
持有句柄，所以 master 与全部 worker 写同一个日期文件即可，无需按进程
分文件，跨天也能各自滚到新文件。

用法：

Log.Configure("logs", "{yyyy}{MM}/{dd}.log", Log.INFO);
Log.MaxFileSize(64 * 1024 * 1024);
Log.Info("server listening on :8080");
Log.Error("db connect failed: " + err);

- static int TRACE=0;
  - 级别常量，供 Configure / Write 使用。

- static int DEBUG=1;

- static int INFO=2;

- static int WARN=3;

- static int ERROR=4;

- static int FATAL=5;

- static string dir="";

- static string pattern="{ yyyy}{ MM}/{ dd}.log";

- static int minLevel=2;

- static long maxBytes=0;

- static bool toConsole=true;

- static bool enabled=false;

- static string lastPath="";

- static void Configure(string dir, string pattern, int minLevel)
  - 启用文件日志：`dir` 目录（不存在则创建），`pattern`
    见类说明，`minLevel` 以下的记录被丢弃。

- static bool IsEnabled()
  - 是否已经配置过文件日志（调用过 Configure）。库代码用它
    判断该不该给出一个默认去处，而不覆盖应用自己选的目录。

- static void MaxFileSize(long bytes)
  - 单个文件的字节上限，超过后滚动到 `名字.1`、`名字.2` …
    传 0 关闭按大小滚动（只按文件名模式滚动）。

- static void EchoToConsole(bool on)
  - 是否同时写到控制台（默认写）。后台服务应关掉。

- static string CurrentPath()
  - 当前生效的日志文件完整路径。

- static void Trace(string msg)

- static void Debug(string msg)

- static void Info(string msg)

- static void Warn(string msg)

- static void Error(string msg)

- static void Fatal(string msg)

- static void Write(int level, string msg)
  - 写一条记录：`时间 [级别] pid/wid 正文`。

- static string RollBySize(string path)
  - 把已写满的 `path` 改名为第一个空闲的 `path.N`，返回仍然
    可写的原路径。改名失败（文件被占用）时原样返回，宁可让文件超限
    也不能丢日志。

- static string LevelName(int level)

- static void EnsureDirFor(string path)
  - 建出 `path` 的父目录。模式带目录时（按月分目录）新的一个月
    就是一个还不存在的目录，Configure 那次创建管不到它。

- static string Sep()

- static string Expand(string pattern, DateTime t)
  - 展开文件名模式中的 {yyyy} {MM} {dd} {HH} {pid} {wid}。

- static string ExpandFor(string pattern, DateTime t, int wid)
  - 同 `Expand`，但 {wid} 用给定的序号展开：
    master 要为**即将派生**的 worker 算出文件名，那不是它自己的序号。

- static string Replace(string text, string from, string to)


## Privileges (class)

当前进程的身份与权限状态，以及 UAC 提权启动。

Windows 上映射到进程 token API（提权检查、SID 查询、
特权启用、“runas”提权）；POSIX 上映射到 euid/root
检查与数字 uid。提权启动（RunAs / RunAsWait）在 Linux 上走
polkit 的 pkexec，在 macOS 上走 osascript 的
"with administrator privileges"；找不到提权工具时返回
false / -1。EnablePrivilege 无 POSIX 对应物，仍抛出
PlatformNotSupportedException，而不是静默失败。

- [DllImport("kernel32", EntryPoint="GetCurrentProcess")]static extern nint WinGetCurrentProcess();

- [DllImport("kernel32", EntryPoint="OpenProcessToken")]static extern int WinOpenProcessToken(nint proc, int access, nint token);

- [DllImport("advapi32", EntryPoint="GetTokenInformation")]static extern int WinGetTokenInformation(nint token, int cls, nint info, int infoLen, nint retLen);

- [DllImport("advapi32", EntryPoint="GetUserNameW")]static extern int WinGetUserNameW(nint buf, nint size);

- [DllImport("advapi32", EntryPoint="LookupPrivilegeValueW")]static extern int WinLookupPrivilegeValueW(nint system, nint name, nint luid);

- [DllImport("advapi32", EntryPoint="AdjustTokenPrivileges")]static extern int WinAdjustTokenPrivileges(nint token, int disableAll, nint newState, int bufLen, nint prev, nint retLen);

- [DllImport("advapi32", EntryPoint="ConvertSidToStringSidW")]static extern int WinConvertSidToStringSidW(nint sid, nint str);

- [DllImport("advapi32", EntryPoint="LocalFree")]static extern nint WinLocalFree(nint mem);

- [DllImport("kernel32", EntryPoint="GetLastError")]static extern int WinGetLastError();

- [DllImport("shell32", EntryPoint="ShellExecuteExW")]static extern int WinShellExecuteExW(nint sei);

- [DllImport("kernel32", EntryPoint="WaitForSingleObject")]static extern int WinWaitForSingleObject(nint handle, int ms);

- [DllImport("kernel32", EntryPoint="GetExitCodeProcess")]static extern int WinGetExitCodeProcess(nint proc, nint code);

- [DllImport("kernel32", EntryPoint="CloseHandle")]static extern int WinCloseHandle(nint handle);

- static nint OpenToken(int access)

- [DllImport("crt", EntryPoint="geteuid")]static extern int GetEuid();

- static bool IsAdmin()
  - 当前进程以提权 token 运行时返回 true
    （Windows：token 已提权；POSIX：euid == 0）。

- static string UserName()
  - 运行本进程的用户名（Windows 账户名；
    POSIX：USER 环境变量）。未知时返回 ""。

- static string UserSid()
  - 当前用户的稳定标识：Windows 上为 SID 字符串
    （"S-1-5-21-..."），POSIX 上为数字 uid。

- static bool EnablePrivilege(string name)
  - 在当前进程 token 上启用指定特权（如 "SeDebugPrivilege"）。
    特权成功启用时返回 true。仅 Windows；
    POSIX 抛出 PlatformNotSupportedException。

- static bool RunAs(string file, string args)
  - 以提权方式启动 `file`：Windows 弹 UAC 提示，Linux
    走 pkexec（由 polkit 代理弹框），macOS 走 osascript 的管理员
    提示。启动被接受时返回 true；用户拒绝或系统没有提权工具
    时返回 false。
    <remarks>会调 shell；切勿传入不可信的 args（注入风险）。</remarks>

- static int RunAsWait(string file, string args)
  - 以提权方式启动 `file` 并等待退出，返回
    其退出码（启动失败、被拒绝或本机无提权工具时为 -1）。

- static string ElevateCommand(string file, string args)

- static bool HasTool(string name)

- static string ShellQuote(string s)

- static int ShellElevated(string file, string args, bool wait)


## Process (class)

跨平台进程启动器，支持捕获输出。
Windows 上使用 _popen/_pclose，POSIX（Linux/macOS）上使用 popen/pclose。

安全提示：RunCapture / Run / PipeOpen 通过
系统 shell（cmd.exe 或 /bin/sh -c）执行其参数，因此命令中的 shell 元字符
会被解释。切勿用不可信或未经清理的
输入构造这些命令——否则会招致命令注入。只传入可信、完全
可控的命令字符串。（不经 shell、基于 argv 的启动器才是处理不可信参数的
安全之选，也是计划中的新增功能。）

- [DllImport("crt", EntryPoint="_popen")]static extern nint plat_popen(string cmd, string mode);

- [DllImport("crt", EntryPoint="_pclose")]static extern int plat_pclose(nint fp);

- [DllImport("kernel32", EntryPoint="CreateProcessW")]static extern int WinCreateProcess(nint appName, nint cmdLine, nint pa, nint ta, int inherit, int flags, nint env, nint curDir, string si, string pi);

- [DllImport("kernel32", EntryPoint="WaitForSingleObject")]static extern int WinWaitForSingleObject(nint handle, int ms);

- [DllImport("kernel32", EntryPoint="GetExitCodeProcess")]static extern int WinGetExitCodeProcess(nint handle, string codeBuf);

- [DllImport("kernel32", EntryPoint="CloseHandle")]static extern int WinCloseHandle(nint handle);

- [DllImport("kernel32", EntryPoint="GetTickCount")]static extern int WinGetTickCount();

- [DllImport("kernel32", EntryPoint="GetCurrentProcessId")]static extern int WinGetCurrentProcessId();

- [DllImport("kernel32", EntryPoint="WideCharToMultiByte")]static extern int WinWideToMulti(int page, int flags, nint wide, int wideLen, nint mb, int mbLen, nint defChar, nint usedDef);

- [DllImport("kernel32", EntryPoint="MultiByteToWideChar")]static extern int WinMultiToWide(int page, int flags, nint mb, int mbLen, nint wide, int wideLen);

- [DllImport("kernel32", EntryPoint="CreateFileW")]static extern nint WinCreateFile(nint path, int access, int share, byte[]sa, int disposition, int flags, nint template);

- [DllImport("crt", EntryPoint="popen")]static extern nint plat_popen(string cmd, string mode);

- [DllImport("crt", EntryPoint="pclose")]static extern int plat_pclose(nint fp);

- [DllImport("crt")]static extern long fread(string buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fopen")]static extern nint plat_fopen(string path, string mode);

- [DllImport("crt", EntryPoint="fseek")]static extern int plat_fseek(nint fp, int offset, int origin);

- [DllImport("crt", EntryPoint="ftell")]static extern long plat_ftell(nint fp);

- [DllImport("crt", EntryPoint="fread")]static extern long plat_fread(byte[]buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fclose")]static extern int plat_fclose(nint fp);

- [DllImport("crt")]static extern int system(string cmd);

- [DllImport("crt")]static extern int fputs(string s, nint fp);

- [DllImport("crt")]static extern int fflush(nint fp);

- static nint PipeOpen(string cmd, string mode)
  - 打开进程管道。

- static int PipeClose(nint fp)
  - 关闭进程管道。

- static nint ShellOpen(string cmd)
  - 打开一个持久交互式 shell 会话，其 stdin 即此管道。
    stdout/stderr 重定向请写在 `cmd` 里（管道只
    负责喂入输入），例如 Windows 上 `cmd /q /k >>"session.log" 2>&1`
    或 POSIX 上 `sh >>"session.log" 2>&1`，再流式读取日志文件
    以获取输出。返回供 ShellSend/ShellClose 使用的管道句柄，出错时为 0。
    安全提示：命令及发送的每一行都会经系统 shell 执行。

- static void ShellSend(nint fp, string line)
  - 向交互式 shell 会话发送一条命令行。

- static int ShellClose(nint fp)
  - 关闭交互式 shell 会话（发送 EOF 并等待）。

- static nint WinHandleAt(byte[]pi, int off)

- static void WinPutHandle(byte[]buf, int off, nint h)

- static void WinPutInt(byte[]buf, int off, int v)

- static int WinSpawn(string command, bool waitFor)

- static string WinReadText(string path)

- static List<string> RunCapture(string cmd)
  - 运行命令并将 stdout+stderr 按行捕获。

- static ProcessResult Capture(string cmd)
  - 运行命令，捕获 stdout+stderr 并保留退出码。
    
    RunCapture 会丢掉退出码，调用方只能去猜命令是否成功
    （"输出里有没有 error 字样"），因此凡是需要判断成功/失败的
    场景都应改用本方法。启动失败时 exitCode 为 -1。

- static List<string> RunCaptureAsync(string cmd)
  - 运行命令并捕获输出（包装）。

- static int Run(string cmd)
  - 通过系统 shell 运行命令并返回退出码。
    
    会阻塞调用线程直到子进程退出（Windows 上为 INFINITE 等待）。
    因此 GUI 线程绝不能用它启动程序、文件
    管理器或 shell 打开操作——它会停止处理消息，
    在子进程存活期间窗口一直显示“未响应”。启动请使用
    RunDetached 用于启动；Run 只留给短命令——
    那种需要先拿到退出码才能继续的场景。
    
    安全提示：会调用 shell；切勿传入不可信输入（有注入风险）。

- static int RunDetached(string cmd)
  - 启动命令但不等待其结束，返回
    立即返回（启动成功时为 0）。适用于长时间运行或模态
    子进程（如原生文件/文件夹选择器），以便调用方 UI 能
    持续处理消息而不至于冻结到子进程退出。
    安全提醒：会调用 shell；切勿传入不可信输入（存在注入风险）。

- static int StartProgram(string exe, string logPath)
  - 直接启动一个程序，不经过 shell，可选把
    stdout/stderr 追加重定向到 `logPath`（为 "" 时丢弃输出）。
    启动成功返回 0，失败返回 -1。
    
    与 `RunDetached` 的区别：不产生 cmd.exe /bin/sh 中间进程。
    长期运行的子进程（如 worker）必须用它：`cmd.exe /c app.exe` 会让
    每个 worker 多挂一个 cmd.exe 加一个 conhost.exe，在任务管理器里堆成
    一片“Windows 命令处理程序 / 控制台窗口主机”，崩溃重启时更甚。
    参数不经 shell 解释，因此路径中的空格由本方法自己加引号处理。

- static List<string> SplitLines(string text)
  - 按换行符拆分文本。


## ProcessControl (class)

对其他进程的控制操作：挂起/恢复、终止、等待
及优先级。Windows 使用 Win32 进程 API（NtSuspendProcess 用于
挂起，TerminateProcess 用于终止，优先级类）；POSIX 映射到
kill(2) 信号（SIGSTOP/SIGCONT/SIGTERM/SIGKILL）和 setpriority(2)。

优先级是跨平台的 0..5 等级（0 = 空闲，1 = 低于正常，
2 = 正常，3 = 高于正常，4 = 高，5 = 实时）。Windows 映射到
优先级类，POSIX 映射到 nice 值。

- [DllImport("kernel32", EntryPoint="OpenProcess")]static extern nint WinOpenProcess(int access, int inherit, int pid);

- [DllImport("kernel32", EntryPoint="CloseHandle")]static extern int WinCloseHandle(nint handle);

- [DllImport("ntdll", EntryPoint="NtSuspendProcess")]static extern int WinNtSuspendProcess(nint proc);

- [DllImport("ntdll", EntryPoint="NtResumeProcess")]static extern int WinNtResumeProcess(nint proc);

- [DllImport("kernel32", EntryPoint="TerminateProcess")]static extern int WinTerminateProcess(nint proc, int code);

- [DllImport("kernel32", EntryPoint="WaitForSingleObject")]static extern int WinWaitForSingleObject(nint handle, int ms);

- [DllImport("kernel32", EntryPoint="GetExitCodeProcess")]static extern int WinGetExitCodeProcess(nint proc, nint code);

- [DllImport("kernel32", EntryPoint="GetPriorityClass")]static extern int WinGetPriorityClass(nint proc);

- [DllImport("kernel32", EntryPoint="SetPriorityClass")]static extern int WinSetPriorityClass(nint proc, int cls);

- static nint Open(int pid, int access)

- [DllImport("crt", EntryPoint="kill")]static extern int KillSignal(int pid, int sig);

- [DllImport("crt", EntryPoint="getpriority")]static extern int GetPriority(int which, int pid);

- [DllImport("crt", EntryPoint="setpriority")]static extern int SetPriority(int which, int pid, int value);

- static int SigStop()

- static int SigCont()

- static int SigStop()

- static int SigCont()

- static bool Suspend(int pid)
  - 挂起 `pid` 的所有线程。成功返回 true。（POSIX：
    SIGSTOP —— 整个进程暂停；用 Resume 恢复。）

- static bool Resume(int pid)
  - 恢复被挂起的进程。成功返回 true。

- static bool Terminate(int pid)
  - 请求 `pid` 退出（POSIX 上为 SIGTERM；Windows 上为
    TerminateProcess）。信号送达时返回 true。进程可能仍在
    关闭中——可与 WaitForExit 配合使用。

- static bool Kill(int pid)
  - 强制 `pid` 立即退出（POSIX 上为 SIGKILL）。信号送达时
    返回 true。

- static bool IsRunning(int pid)
  - 只要存在 `pid` 对应的进程就返回 true。

- static bool WaitForExit(int pid, int timeoutMs)
  - 阻塞直到 `pid` 退出或 `timeoutMs` 超时。进程在超时内
    退出则返回 true。超时为 0 时仅轮询一次。

- static int GetExitCode(int pid)
  - 已结束进程的退出码；仍在运行（或退出码不可读）时返回 -1。
    POSIX 上，非子进程的退出码内核不对外暴露，
    因此已退出的进程也一律返回 -1——
    请改用 WaitForExit + IsRunning 判断。

- static int GetPriority(int pid)
  - `pid` 在 0..5 等级上的优先级（不可读时返回 -1）。

- static bool SetPriority(int pid, int level)
  - 将 `pid` 的优先级设为 0..5 等级。
    成功返回 true。

- static int LevelToNice(int level)

- static int NiceToLevel(int nice)


## ProcessEntry (class)

单个进程快照条目。`name` 为不含扩展名的镜像名
（Windows）或可执行文件基名（Linux）。`exePath` 与 `memoryBytes`
由 `ProcessList.ByPid` 填充（需要打开句柄，
因此批量接口 `ProcessList.List` 省略它们）。

- public int pid;

- public string name;

- public string exePath;

- public int parentPid;

- public int threads;

- public long memoryBytes;
  - 工作集（字节）（Windows）/ 常驻集（Linux）。无法读取时
    返回 -1（拒绝访问、僵尸进程、竞态）。


## ProcessHost (class)

以标准库能力形式提供的进程/守护进程管理。这是
“workerman 风格”服务端背后可移植、无依赖的核心：识别
正在运行的进程、定位自身可执行文件、以 Unix 守护进程方式分离，并
通过 SO_REUSEPORT 共享监听端口，启动自身的 worker 副本。

它刻意只负责进程/操作系统层面的机制。绑定、SO_REUSEPORT 与
accept 循环位于 socket/server 层；由服务端框架把两者
组合起来（参见服务端模板的 Server.Run）。崩溃重启交由
外部监督程序处理（systemd `Restart=always`、Docker
`restart: unless-stopped`、Kubernetes）——这是保持
横向扩展服务存活的常规做法，而非在此重新实现。

平台支持：
- Pid / Env / SelfExe / WorkerId / IsWorker：所有平台。
- Daemonize（setsid 分离）：仅 Linux；其他平台为 no-op，返回 false，
调用方继续在前台运行。
- SpawnWorkers（SO_REUSEPORT 多进程）：Linux/macOS。Windows 没有
SO_REUSEPORT 连接负载均衡，因此 SpawnWorkers 在 Windows 上是 no-op，
MultiProcessSupported() 返回 false；调用方应运行单进程
（必要时改用 IIS/NSSM/Windows 服务托管）。

- [DllImport("crt")]static extern string getenv(string name);

- [DllImport("kernel32", EntryPoint="GetCurrentProcessId")]static extern int GetCurrentProcessId();

- [DllImport("kernel32", EntryPoint="GetModuleFileNameW")]static extern int GetModuleFileNameW(nint hModule, nint buf, int size);

- [DllImport("kernel32", EntryPoint="GetEnvironmentVariableW")]static extern int GetEnvironmentVariableW(nint name, nint buf, int size);

- [DllImport("crt")]static extern int getpid();

- [DllImport("crt")]static extern int readlink(string path, string buf, int size);

- [DllImport("crt")]static extern int getpid();

- [DllImport("crt", EntryPoint="_NSGetExecutablePath")]static extern int NSGetExecutablePath(nint buf, nint size);

- static int Pid()
  - 当前进程的 PID。

- static string Env(string name)
  - 读取环境变量，未设置时返回 ""（而不是
    空指针），以便调用处能安全地与 "" 比较。

- static string SelfExe()
  - 尽力获取正在运行的可执行文件的绝对路径，用于
    重新启动自身的 worker 副本。无法确定时返回 ""
    （macOS/其他平台），此时调用方按单进程运行。

- static string AppDir()
  - 程序自带资源（config/、views/、wwwroot/ …）所在目录：
    可执行文件所在目录；若可执行文件在 macOS 应用包里，则是
    <c><名称>.app/Contents/Resources</c>——发布时资源就写在那里，
    也是 NSBundle 查找资源的地方。无法确定时返回 ""。

- static bool UseAppDir(string marker)
  - 当 AppDir() 下确实放着 `marker`（相对路径，文件或目录）时，
    把当前工作目录切到 AppDir()，并返回 true。
    
    发布出来的程序用相对路径读自己的配置、视图和静态资源，而工作目录
    由启动方式决定：Finder 双击 .app 时是 /，从别的目录敲命令时是那个
    目录——两种情况下相对路径都指不到程序自己的文件（症状就是
    “no config file found”“view not found”）。开发时可执行文件旁边没有
    这些资源，marker 不存在，于是这里什么都不做，行为不变。

- static int WorkerId()
  - 本进程的 worker 索引：master 为 0，被派生的 worker 为 N
    （通过 SpawnWorkers 设置的 ZAN_WORKER_ID 环境变量）。

- static bool IsWorker()
  - 本进程由 SpawnWorkers 作为 worker 启动时返回 true
    （即设置了 ZAN_WORKER_ID）；master/独立进程返回 false。

- static bool MultiProcessSupported()
  - 当前平台是否支持 SO_REUSEPORT 多进程 worker
    （Linux/macOS 支持，Windows 不支持）。

- static bool Daemonize()
  - 在 Linux 上通过 setsid 分离为后台守护进程。
    在原始前台进程中返回 TRUE，随后应退出；
    分离出的子进程带 ZAN_DAEMON=1 重新运行程序，使此调用
    第二次成为 no-op。在非 Linux 平台，或可执行文件路径
    无法找到时，返回 FALSE，调用方继续正常运行。

- static bool DaemonizeTo(string logPath, string logDir)
  - 同 `Daemonize`，但后台进程的 stdout/stderr 追加重定向到
    `logPath`（为 "" 时丢弃），并把 `logDir` 作为 ZAN_LOG_DIR 传给它，
    供原生崩溃记录写在同一个目录下。
    
    守护进程脱离终端后，输出默认进 /dev/null：真出事时（子进程反复退出、
    未捕获的致命信号）现场什么都不留，所以后台模式应当给出一个文件。

- static int SpawnWorkers(int count)
  - 额外启动 (count-1) 个 worker 进程，重新运行当前
    可执行文件并设置 ZAN_WORKER_ID=1..count-1。配合用 SO_REUSEPORT
    绑定的监听器，操作系统内核会将入站连接
    在 master（worker 0）与每个派生的 worker 之间负载均衡。返回实际
    派生的 worker 数量（不支持、count<=1 或可执行文件路径未知时为 0，
    此时调用方按单进程运行）。


## ProcessList (class)

进程枚举。Windows 遍历 Toolhelp32 快照；Linux 扫描
/proc。两者都为每个进程填充 pid/name/parentPid/threads，并可
解析单个 pid 的可执行文件路径和内存占用。其他
平台抛出 PlatformNotSupportedException。

- static List<ProcessEntry> List()
  - 所有可见进程的快照（pid、name、parent、
    线程数）。开销低——不逐个进程打开句柄。

- static List<ProcessEntry> ByName(string name)
  - 名称等于 `name` 的所有进程（不区分大小写，
    Windows 上带或不带 .exe 扩展名均可）。没有则返回空列表。

- static ProcessEntry ByPid(int pid)
  - 单个 pid 的详细信息：额外填充 exePath 与 memoryBytes。进程不存在
    （或拒绝访问）时返回 null。

- static int SelfPid()
  - 当前进程的 PID。

- static long MemoryUsage(int pid)
  - `pid` 的工作集（Windows）/ 常驻集（Linux），单位为
    字节；不可读时返回 -1。

- static string ExePath(int pid)
  - 进程镜像的完整路径；不可读时返回 ""。

- [DllImport("kernel32", EntryPoint="CreateToolhelp32Snapshot")]static extern nint WinCreateSnapshot(int flags, int pid);

- [DllImport("kernel32", EntryPoint="Process32FirstW")]static extern int WinProcess32FirstW(nint snap, nint entry);

- [DllImport("kernel32", EntryPoint="Process32NextW")]static extern int WinProcess32NextW(nint snap, nint entry);

- [DllImport("kernel32", EntryPoint="CloseHandle")]static extern int WinCloseHandle(nint handle);

- [DllImport("kernel32", EntryPoint="GetCurrentProcessId")]static extern int WinGetCurrentProcessId();

- [DllImport("kernel32", EntryPoint="OpenProcess")]static extern nint WinOpenProcess(int access, int inherit, int pid);

- [DllImport("psapi", EntryPoint="GetProcessMemoryInfo")]static extern int WinGetProcessMemoryInfo(nint proc, nint counters, int cb);

- [DllImport("kernel32", EntryPoint="QueryFullProcessImageNameW")]static extern int WinQueryFullProcessImageNameW(nint proc, int flags, nint buf, nint size);

- static List<ProcessEntry> WinList()

- static long WinMemory(int pid)

- static string WinExePath(int pid)

- static List<ProcessEntry> LinuxList()

- static ProcessEntry LinuxEntry(int pid)

- static int StatField(string s, int index)
  - `s` 中按空格分隔的令牌的第 `index` 个（从 0 起算）。

- static int TokenLen(string s, int from)

- static long LinuxMemory(int pid)

- static string LinuxExePath(int pid)

- static string BaseName(string path)
  - 路径的最后一段（"C:\a\b.exe" -> "b.exe"，"/a/b" -> "b"）。

- static int IndexOf(string hay, string needle, int from)

- static int LastIndexOf(string hay, string needle)

- static bool EndsWith(string s, string tail)

- static int ParseInt(string s)

- static long ParseLong(string s)

- static int CodeOf(string ch)


## ProcessResult (class)

一次已完成的进程捕获：退出码加上按行拆分的
stdout+stderr。见 `Process.Capture`。

- int exitCode;

- List<string> lines;

- ProcessResult()

- bool Ok()
  - 命令是否以 0 退出。

- string Text()
  - 把捕获的各行重新拼成一段文本（以 '\n' 分隔）。


## ReqAgg (class)

One aggregate row of the request leaderboard, keyed by the endpoint
("METHOD /path" with numeric path segments folded to :id) so that every hit
of the same interface -- /admin/users/1 and /admin/users/2 -- counts as one
entry with its own count / average / maximum, instead of a flat list of
individual samples dominated by long-lived connections.

- string method;

- string path;

- string req;

- long count;

- long totalUs;

- long maxUs;

- long errors;

- long failed;

- long rejected;

- ReqAgg(string method, string path)


## ServerErrorDoc (class)

- long ts;

- string origin;

- string detail;


## ServerMetrics (class)

- [DllImport("kernel32", EntryPoint="GetTickCount64")]static extern long GetTickCount64();

- [DllImport("kernel32", EntryPoint="GetCurrentProcess")]static extern nint GetCurrentProcess();

- [DllImport("kernel32", EntryPoint="GetProcessTimes")]static extern int GetProcessTimes(nint h, string cre, string ex, string kern, string usr);

- [DllImport("psapi", EntryPoint="GetProcessMemoryInfo")]static extern int GetProcessMemoryInfo(nint h, string counters, int cb);

- [DllImport("crt", EntryPoint="gettimeofday")]static extern int gettimeofday(string tv, nint tz);

- [DllImport("crt")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern long fwrite(string buf, long size, long count, nint fp);

- [DllImport("crt")]static extern int fclose(nint fp);

- [DllImport("crt", EntryPoint="zan_monotonic_us")]static extern long NativeMonotonicUs();

- int reqCount;

- int reqErrors;

- int reqFailed;

- int reqRejected;

- int reqUnmatched;

- long reqTotalUs;

- long reqMaxUs;

- int slowReqMs;

- List<SlowEntry> slowReqs;

- int queryCount;

- long queryTotalUs;

- long queryMaxUs;

- int slowQueryMs;

- List<SlowEntry> slowQueries;

- List<SqlAgg> sqlAgg;

- int sqlAggCap;

- List<ReqAgg> reqAgg;

- int reqAggCap;

- List<ReqAgg> routeSlots;

- Dictionary <string, ReqAgg> reqIdx;

- Dictionary <string, SqlAgg> sqlIdx;

- List<ErrEntry> errors;

- int errCap;

- long errTotal;

- string errLogPath;

- bool errDebug;

- long startMillis;

- long lastWallMs;

- long lastCpuMs;

- long lastCpuPct;

- int ringCap;

- int seriesCap;

- long[]sSec;

- int[]sReq;

- int[]sErr;

- long[]sTotalUs;

- long[]sMaxUs;

- int[]sQuery;

- long[]sQueryUs;

- int[]sCpu;

- long[]sRss;

- int[]sHist;

- long seriesLastWall;

- long seriesLastCpu;

- long concurrent;

- long concurrentPeak;

- long streams;

- long streamFrames;

- long streamBytes;

- ErrorSink errSink;

- ServerMetrics()

- void Entered()
  - A request started. Paired with Left() in the dispatcher, so the
    counter is exact rather than sampled.

- void Left()

- void StreamOpened()
  - A long-lived connection opened / closed, and what it pushed
    while it was open. Frames and bytes, never a duration.

- void StreamClosed(long frames, long bytes)

- static const int HIST_BINS=13;

- static long HistEdge(int bin)

- static int HistBin(long us)

- int SeriesSlot(long sec)
  - The bucket for `sec`, cleared and CPU/RSS-sampled if this is the
    first write to it. Locked like every other bucket access: two workers
    recording in the same second must not both clear the slot.
    Sampling here (rather than on a timer) keeps the
    series free of a background coroutine: a second with no traffic has
    nothing worth plotting anyway, and SeriesJson fills those gaps.

- static ServerMetrics inst;

- static byte[]monoBuf;

- static ServerMetrics Global()

- ServerMetrics SlowRequestMs(int ms)

- ServerMetrics SlowQueryMs(int ms)

- ServerMetrics ErrorLog(string path, bool debugConsole)
  - Where errors are appended and whether they also echo to the
    console. The log always persists; the console is a debug convenience the
    operator can turn off. Empty path disables the file sink.
    
    The path is a pattern expanded per write by `Log.Expand`:
    {yyyy} {MM} {dd} {HH} {pid} {wid}, and it may contain directories --
    missing ones are created. `logs/{yyyy}{MM}/{dd}.log` is the file the
    master and every worker already log into, so an error and whatever
    happened to the process that raised it are read in one place; each record
    is open-append-close, so sharing the file needs no {wid} of its own. A
    path without tokens behaves as before: one file that grows
    forever.

- ServerMetrics DebugConsole(bool on)

- bool DebugConsoleOn()

- void AppendErrLine(string line)

- void RecordError(long ts, string origin, string detail)
  - Records one server-side error: kept in a bounded in-memory ring
    for the backend view and appended to the log file. `ts` is epoch seconds
    from the caller so this class needs no clock of its own. The detail must
    already be redacted by the caller -- no keys/tokens/passwords.

- ServerMetrics OnError(ErrorSink fn)
  - Where errors are persisted, set once at startup. Kept as a
    delegate so this class stays free of a database.

- long ErrorTotal()

- List<ServerErrorDoc> RecentErrors(int limit)
  - Recent errors, most-recent first, for a backend log screen.

- static long LeInt(byte[]buf, int off, int n)

- static long MonoMicros()
  - Monotonic MICROSECONDS: the clock every duration recorded here
    is measured with, resolving a single request instead of rounding it to a
    15ms tick. Use deltas, not the absolute value.

- static long MonoMillis()
  - Monotonic wall-clock milliseconds (not affected by clock resets
    the way a seconds epoch is); use deltas, not the absolute value. Coarse
    on Windows (~15ms): for anything request-sized use MonoMicros.

- static long CpuMillis()
  - Cumulative process CPU time in ms (user+kernel), or -1 if the
    current platform cannot report it.

- static long MemoryRssBytes()
  - Resident memory of this process in bytes, or -1 if unavailable
    on the current platform.

- static int LastIndexOf(string s, string ch)

- static List<string> SplitWs(string s)

- void RecordRoute(int slot, string method, string pattern, int status, long us)
  - Records one completed request that the server actually served,
    keyed by what served it: `pattern` is a route's registration pattern
    ("/user/{id}") or a mounted asset's path ("/static/js/app.js") -- a
    fixed string the server chose, never the URL the caller typed, so the
    leaderboard has one row per interface and none per probe.
    
    `slot` is the route's index (Route.idx), which makes the per-endpoint
    aggregate an array read rather than a scan over every endpoint; -1 for
    anything that has no route index (an asset) and is looked up by name
    over the handful of such rows.
    
    `us` is the elapsed time in MICROSECONDS (see MonoMicros): milliseconds
    cannot express a request this server serves in 200us.

- void RecordUnmatched(int status, long us)
  - A request nothing claimed: a 404, a 405, a vulnerability scan.
    It counts in the totals and the series -- a server being probed is not
    an idle server -- but it gets no row of its own, because the only name
    it has is the one the caller invented.

- void RecordRequest(string method, string path, int status, long us)
  - Records one completed HTTP request by raw path, folding numeric
    segments to :id. For a caller outside the dispatcher (a proxy or a hand
    written server) that has no route to name: the served path is the only
    key available. The dispatcher itself uses RecordRoute, which needs no
    normalization at all.

- void CountRequest(int status, long us)
  - The counters and the chart series every request feeds,
    whatever served it -- no strings, no lookups. Callers hold the
    lock.

- void FoldEndpoint(int slot, string method, string path, long us, int status)
  - Folds one hit into the aggregate row for its endpoint, created
    on the first hit. A route (`slot` >= 0) has a direct slot in a table
    sized by the route table itself, so a request costs one array read and
    no string comparison; anything without a route index (a mounted asset, a
    caller with only a raw path) is found by name and bounded to
    `reqAggCap` rows, past which a never-seen endpoint is left out of the
    leaderboard rather than growing the process -- it still counts in the
    totals. Callers hold the lock.

- void AddAgg(ReqAgg a)
  - Appends a row to the leaderboard and to the by-name index. The
    index keeps the FIRST row registered under a name, which is what the
    scan it replaced found. Callers hold the lock.

- static bool IsAllDigits(string s)
  - True when s is non-empty and every character is a digit; such a
    path segment is a record id and is folded to :id for grouping.

- static bool RangeIsDigits(string s, int start, int end)
  - IsAllDigits over a half-open range, so folding a path needs no
    substring per segment.

- static string NormalizePath(string method, string path)
  - Endpoint key for aggregation: "METHOD " plus the folded path.
    Only for a caller that has nothing but a raw path -- a routed request is
    named by its pattern and never comes through here.

- static string FoldPath(string path)
  - The path an aggregate is keyed by when the raw URL is all there
    is: the query string dropped and numeric segments folded to :id, so
    /admin/users/1 and /admin/users/2 map to the same /admin/users/:id.

- void RecordQuery(string sql, long us)
  - Records one completed database query. `us` is microseconds.

- void RecordSqlAgg(string norm, long us)
  - Folds one execution into the per-shape aggregate that backs the
    slow-SQL leaderboard. Bounded to `sqlAggCap` distinct shapes: once full,
    a never-before-seen shape is dropped rather than growing without limit --
    a monitor should not become a memory leak under a query it cannot fold.

- static string NormalizeSql(string sql)
  - Reduces a statement to its "net" form: single-quoted string
    literals and numeric literals become `?`, runs of whitespace collapse to
    one space, and a comma-separated run of two or more placeholders collapses
    to a single `?` -- so `WHERE id IN (1,2,3)` and `WHERE id IN (1,2,3,4,5)`
    normalize to the identical `WHERE id IN (?)`.

- static string CollapsePlaceholders(string s)
  - Collapses a comma-separated run of two or more `?` into one `?`.
    A lone placeholder (e.g. `name = ?`) is left untouched so unrelated
    columns do not get merged; only genuine value lists shrink.

- JsonValue TopSqlJson(int limit)
  - The leaderboard: up to `limit` normalized statements ranked by
    total time spent, most expensive first. A selection scan rather than a
    sort -- the shape set is small and bounded, and `limit` is tiny.

- JsonValue TopReqJson(int limit)
  - The request leaderboard: endpoints ranked by total time served,
    highest first, capped at `limit` rows. Selection-sort over a copied pool
    (the list is tiny and this avoids mutating the live aggregates).

- JsonValue SlowReqJson()

- JsonValue SlowQueryJson()

- long SeriesPercentile(int idx, int pct)
  - Estimated latency percentile (0-100) for one bucket, read off
    the fixed histogram: the upper edge of the bin the rank falls in, which
    is an upper bound on the true value rather than an interpolation that
    would read more precise than the data is.

- string SeriesJson(int seconds)
  - The last `seconds` one-second buckets as JSON, oldest first, for
    plotting. Every second in the window is present: a second with no traffic
    is a real zero on the chart, not a missing point that would stretch the
    line across the gap. CPU/RSS carry the last known sample forward, since a
    process that served nothing still occupies memory.
    
    The series is per PROCESS, like every other counter here: with several
    workers each answers for the requests it served.

- static int CpuWindowMs=200;
  - Full metrics snapshot as JSON. Recomputes CPU% from the delta
    since the previous snapshot, so poll it on a fixed interval for a
    meaningful "cpu_percent".
    JsonValue leaf builders for the snapshot (number text goes through
    Convert.ToString, exactly like the generated __JsonBind tree writers).
    The shortest interval a CPU percentage may be computed over.

- static JsonValue DocNum(long v)

- static JsonValue DocStr(string s)

- string SnapshotJson()


## SqlAgg (class)

One aggregate row of the slow-SQL leaderboard, keyed by the
NORMALIZED ("net") statement so that the same query with different literal
arguments -- `in (1,2,3)` and `in (1,2,3,4,5)` -- counts as one entry.

- string sql;

- long count;

- long totalUs;

- long maxUs;

- SqlAgg(string sql)


## Stopwatch (class)

高分辨率计时器。Windows 上使用 QueryPerformanceCounter，
POSIX 上使用 clock_gettime(CLOCK_MONOTONIC) —— 两者都不受墙钟时间
变化（NTP 同步、手动改时）影响，计时结果保持准确。

Stopwatch sw = new Stopwatch();
sw.Start();
... work ...
sw.Stop();
long ms = sw.ElapsedMilliseconds();

- [DllImport("kernel32", EntryPoint="QueryPerformanceFrequency")]static extern int PlatQueryPerformanceFrequency(nint freq);

- [DllImport("kernel32", EntryPoint="QueryPerformanceCounter")]static extern int PlatQueryPerformanceCounter(nint count);

- [DllImport("crt", EntryPoint="zan_monotonic_ns")]static extern long ZanMonotonicNs();

- long startTicks;

- long accumulated;

- bool running;

- static Stopwatch StartNew()
  - 开始（或继续）计时，可安全重复调用。

- void Start()
  - 开始（或继续）计时。

- void Stop()
  - 停止计时；多次 Start/Stop 的耗时会累加。

- void Reset()
  - 重置累计时间（运行中则同时停止）。

- void Restart()
  - 重新开始：清零并启动。

- double ElapsedMilliseconds()
  - 总耗时（毫秒，双精度）。
    计时器可能仍在运行；读数包含当前累计值。

- double ElapsedSeconds()
  - 总耗时（秒，双精度）。

- long ElapsedTicks()
  - 以原始频率单位表示的耗时（参见 `Frequency`）。

- bool IsRunning()
  - 计时器运行中返回 true。

- static long Frequency()
  - 高分辨率时钟每秒的 tick 数。

- static long NowTicks()
  - 当前单调时钟读数（频率单位）。


## void (delegate)

Sink for a caught server error: the application persists it (the
backend error screen reads what this wrote). The detail is already
redacted.

`delegate void ErrorSink(long ts, string origin, string detail);`


## LogLevel (enum)

日志级别名的唯一来源，`Log.LevelName` 走编译器内建的
枚举 ToString。级别值本身仍是 `Log` 上的 int 常量（历史 API）。

- TRACE = =0

- DEBUG = =1

- INFO = =2

- WARN = =3

- ERROR = =4

- FATAL = =5
