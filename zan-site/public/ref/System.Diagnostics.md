# System.Diagnostics

> 源码: `packages/Zan.Diagnostics/src/System/Diagnostics/Log.zan`, `packages/Zan.Diagnostics/src/System/Diagnostics/Privileges.zan`, `packages/Zan.Diagnostics/src/System/Diagnostics/Process.zan`, `packages/Zan.Diagnostics/src/System/Diagnostics/ProcessControl.zan`, `packages/Zan.Diagnostics/src/System/Diagnostics/ProcessHost.zan`, `packages/Zan.Diagnostics/src/System/Diagnostics/ProcessList.zan`, `packages/Zan.Diagnostics/src/System/Diagnostics/ServerMetrics.zan`


## ErrEntry (class)

- long ts;

- string origin;

- string detail;

- ErrEntry(long ts, string origin, string detail)


## Log (class)

- static const int TRACE=0;

- static const int DEBUG=1;

- static const int INFO=2;

- static const int WARN=3;

- static const int ERROR=4;

- static const int FATAL=5;

- static string dir="";

- static string pattern="{ yyyy}{ MM}/{ dd}.log";

- static int minLevel=2;

- static long maxBytes=0;

- static bool toConsole=true;

- static bool enabled=false;

- static string lastPath="";

- static void Configure(string dir, string pattern, int minLevel)

- static bool IsEnabled()

- static void MaxFileSize(long bytes)

- static void EchoToConsole(bool on)

- static string CurrentPath()

- static void Trace(string msg)

- static void Debug(string msg)

- static void Info(string msg)

- static void Warn(string msg)

- static void Error(string msg)

- static void Fatal(string msg)

- static void Write(int level, string msg)

- static string RollBySize(string path)

- static string LevelName(int level)

- static void EnsureDirFor(string path)

- static string Sep()

- static string Expand(string pattern, DateTime t)

- static string ExpandFor(string pattern, DateTime t, int wid)

- static string Replace(string text, string from, string to)


## Privileges (class)

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

- static string UserName()

- static string UserSid()

- static bool EnablePrivilege(string name)

- static bool RunAs(string file, string args)

- static int RunAsWait(string file, string args)

- static string ElevateCommand(string file, string args)

- static bool HasTool(string name)

- static string ShellQuote(string s)

- static int ShellElevated(string file, string args, bool wait)


## Process (class)

- static bool OpenUrl(string url)

- static bool OpenPath(string path)

- private static bool ValidUri(string url)

- private static bool OpenTarget(string target)

- [DllImport("shell32", EntryPoint="ShellExecuteW")]private static extern nint WinShellExecute(nint window, nint operation, nint target, nint parameters, nint directory, int show);

- [DllImport("crt", EntryPoint="_popen")]static extern nint plat_popen(string cmd, string mode);

- [DllImport("crt", EntryPoint="_pclose")]static extern int plat_pclose(nint fp);

- [DllImport("kernel32", EntryPoint="CreateProcessW")]static extern int WinCreateProcess(nint appName, nint cmdLine, nint pa, nint ta, int inherit, int flags, nint env, nint curDir, string si, string pi);

- [DllImport("kernel32", EntryPoint="WaitForSingleObject")]static extern int WinWaitForSingleObject(nint handle, int ms);

- [DllImport("kernel32", EntryPoint="GetExitCodeProcess")]static extern int WinGetExitCodeProcess(nint handle, string codeBuf);

- [DllImport("kernel32", EntryPoint="CloseHandle")]static extern int WinCloseHandle(nint handle);

- [DllImport("kernel32", EntryPoint="GetTickCount")]static extern int WinGetTickCount();

- [DllImport("kernel32", EntryPoint="GetCurrentProcessId")]static extern int WinGetCurrentProcessId();

- [DllImport("kernel32", EntryPoint="CreatePipe")]static extern int WinCreatePipe(byte[]hRead, byte[]hWrite, byte[]sa, int nSize);

- [DllImport("kernel32", EntryPoint="SetHandleInformation")]static extern int WinSetHandleInformation(nint handle, int mask, int flags);

- [DllImport("kernel32", EntryPoint="ReadFile")]static extern int WinReadFile(nint handle, byte[]buf, int nBytes, byte[]numRead, nint overlapped);

- [DllImport("crt", EntryPoint="zan_proc_run_safe")]static extern int PlatProcRunSafe(string exe, nint argsPtr, int argc);

- [DllImport("crt", EntryPoint="zan_proc_start_detached_safe")]static extern int PlatProcStartDetachedSafe(string exe, nint argsPtr, int argc);

- [DllImport("crt", EntryPoint="zan_proc_start_program_safe")]static extern int PlatProcStartProgramSafe(string exe, string logPath);

- [DllImport("crt", EntryPoint="zan_proc_capture_safe")]static extern int PlatProcCaptureSafe(string exe, nint argsPtr, int argc, byte[]outBufPtr, byte[]outLen, byte[]exitCode);

- [DllImport("crt", EntryPoint="zan_proc_free_buf")]static extern void PlatProcFreeBuf(nint buf);

- [DllImport("crt", EntryPoint="malloc")]static extern nint PlatMalloc(long size);

- [DllImport("crt", EntryPoint="free")]static extern void PlatFree(nint ptr);

- [DllImport("kernel32", EntryPoint="WideCharToMultiByte")]static extern int WinWideToMulti(int page, int flags, nint wide, int wideLen, nint mb, int mbLen, nint defChar, nint usedDef);

- [DllImport("kernel32", EntryPoint="MultiByteToWideChar")]static extern int WinMultiToWide(int page, int flags, nint mb, int mbLen, nint wide, int wideLen);

- [DllImport("kernel32", EntryPoint="CreateFileW")]static extern nint WinCreateFile(nint path, int access, int share, byte[]sa, int disposition, int flags, nint template);

- [DllImport("crt", EntryPoint="zan_proc_run_safe")]static extern int PlatProcRunSafe(string exe, nint argsPtr, int argc);

- [DllImport("crt", EntryPoint="zan_proc_start_detached_safe")]static extern int PlatProcStartDetachedSafe(string exe, nint argsPtr, int argc);

- [DllImport("crt", EntryPoint="zan_proc_start_program_safe")]static extern int PlatProcStartProgramSafe(string exe, string logPath);

- [DllImport("crt", EntryPoint="zan_proc_capture_safe")]static extern int PlatProcCaptureSafe(string exe, nint argsPtr, int argc, byte[]outBufPtr, byte[]outLen, byte[]exitCode);

- [DllImport("crt", EntryPoint="zan_proc_free_buf")]static extern void PlatProcFreeBuf(nint buf);

- [DllImport("crt", EntryPoint="malloc")]static extern nint PlatMalloc(long size);

- [DllImport("crt", EntryPoint="free")]static extern void PlatFree(nint ptr);

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

- static int PipeClose(nint fp)

- static nint ShellOpen(string cmd)

- static void ShellSend(nint fp, string line)

- static int ShellClose(nint fp)

- static nint WinHandleAt(byte[]pi, int off)

- static void WinPutHandle(byte[]buf, int off, nint h)

- static void WinPutInt(byte[]buf, int off, int v)

- static int WinSpawn(string command, bool waitFor)

- static string WinReadText(string path)

- static List<string> RunCapture(string cmd)

- static ProcessResult Capture(string cmd)

- static async Task <List<string>> RunCaptureAsync(string cmd)

- static int Run(string cmd)

- static int RunDetached(string cmd)

- static int StartProgram(string exe, string logPath)

- static string QuoteArg(string arg)

- static string BuildCommandLine(string exe, List<string> args)

- static int RunSafe(string exe, List<string> args)

- static int StartDetachedSafe(string exe, List<string> args)

- static ProcessResult CaptureSafe(string exe, List<string> args)

- static async Task<ProcessResult> CaptureSafeAsync(string exe, List<string> args)

- static List<string> SplitLines(string text)


## ProcessControl (class)

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

- [DllImport("crt", EntryPoint="usleep")]static extern int Usleep(int us);

- static int SigStop()

- static int SigCont()

- static int SigStop()

- static int SigCont()

- static bool Suspend(int pid)

- static bool Resume(int pid)

- static bool Terminate(int pid)

- static bool Kill(int pid)

- static bool IsRunning(int pid)

- static bool WaitForExit(int pid, int timeoutMs)

- static int GetExitCode(int pid)

- static int GetPriority(int pid)

- static bool SetPriority(int pid, int level)

- static int LevelToNice(int level)

- static int NiceToLevel(int nice)


## ProcessEntry (class)

- public int pid;

- public string name;

- public string exePath;

- public int parentPid;

- public int threads;

- public long memoryBytes;


## ProcessHost (class)

- [DllImport("crt")]static extern string getenv(string name);

- [DllImport("kernel32", EntryPoint="GetCurrentProcessId")]static extern int GetCurrentProcessId();

- [DllImport("kernel32", EntryPoint="GetModuleFileNameW")]static extern int GetModuleFileNameW(nint hModule, nint buf, int size);

- [DllImport("kernel32", EntryPoint="GetEnvironmentVariableW")]static extern int GetEnvironmentVariableW(nint name, nint buf, int size);

- [DllImport("crt")]static extern int getpid();

- [DllImport("crt")]static extern int readlink(string path, string buf, int size);

- [DllImport("crt")]static extern int getpid();

- [DllImport("crt", EntryPoint="_NSGetExecutablePath")]static extern int NSGetExecutablePath(nint buf, nint size);

- static int Pid()

- static string Env(string name)

- static string SelfExe()

- static string AppDir()

- static bool UseAppDir(string marker)

- static int WorkerId()

- static bool IsWorker()

- static bool MultiProcessSupported()

- static bool Daemonize()

- static bool DaemonizeTo(string logPath, string logDir)

- static int SpawnWorkers(int count)


## ProcessList (class)

- static List<ProcessEntry> List()

- static List<ProcessEntry> ByName(string name)

- static ProcessEntry ByPid(int pid)

- static int SelfPid()

- static long MemoryUsage(int pid)

- static string ExePath(int pid)

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

- static int TokenLen(string s, int from)

- static long LinuxMemory(int pid)

- static string LinuxExePath(int pid)

- static List<ProcessEntry> MacList()

- static long MacMemory(int pid)

- static string MacExePath(int pid)

- static string BaseName(string path)

- static int IndexOf(string hay, string needle, int from)

- static int LastIndexOf(string hay, string needle)

- static bool EndsWith(string s, string tail)

- static int ParseInt(string s)

- static long ParseLong(string s)

- static int CodeOf(string ch)


## ProcessResult (class)

- int exitCode;

- List<string> lines;

- ProcessResult()

- bool Ok()

- string Text()


## ReqAgg (class)

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

- void Left()

- void StreamOpened()

- void StreamClosed(long frames, long bytes)

- static const int HIST_BINS=13;

- static long HistEdge(int bin)

- static int HistBin(long us)

- int SeriesSlot(long sec)

- static ServerMetrics inst;

- static byte[]monoBuf;

- static ServerMetrics Global()

- ServerMetrics SlowRequestMs(int ms)

- ServerMetrics SlowQueryMs(int ms)

- ServerMetrics ErrorLog(string path, bool debugConsole)

- ServerMetrics DebugConsole(bool on)

- bool DebugConsoleOn()

- void AppendErrLine(string line)

- void RecordError(long ts, string origin, string detail)

- ServerMetrics OnError(ErrorSink fn)

- long ErrorTotal()

- List<ServerErrorDoc> RecentErrors(int limit)

- static long LeInt(byte[]buf, int off, int n)

- static long MonoMicros()

- static long MonoMillis()

- static long CpuMillis()

- static long MemoryRssBytes()

- static int LastIndexOf(string s, string ch)

- static List<string> SplitWs(string s)

- void RecordRoute(int slot, string method, string pattern, int status, long us)

- void RecordUnmatched(int status, long us)

- void RecordRequest(string method, string path, int status, long us)

- void CountRequest(int status, long us)

- void FoldEndpoint(int slot, string method, string path, long us, int status)

- void AddAgg(ReqAgg a)

- static bool IsAllDigits(string s)

- static bool RangeIsDigits(string s, int start, int end)

- static string NormalizePath(string method, string path)

- static string FoldPath(string path)

- void RecordQuery(string sql, long us)

- void RecordSqlAgg(string norm, long us)

- static string NormalizeSql(string sql)

- static string CollapsePlaceholders(string s)

- public JsonValue TopSqlJson(int limit)

- public JsonValue TopReqJson(int limit)

- public JsonValue SlowReqJson()

- public JsonValue SlowQueryJson()

- long SeriesPercentile(int idx, int pct)

- string SeriesJson(int seconds)

- static int CpuWindowMs=200;

- static JsonValue DocNum(long v)

- static JsonValue DocStr(string s)

- string SnapshotJson()


## SlowEntry (class)

- string text;

- long us;

- SlowEntry(string text, long us)


## SqlAgg (class)

- string sql;

- long count;

- long totalUs;

- long maxUs;

- SqlAgg(string sql)


## void (delegate)

`delegate void ErrorSink(long ts, string origin, string detail);`


## LogLevel (enum)

- TRACE = =0

- DEBUG = =1

- INFO = =2

- WARN = =3

- ERROR = =4

- FATAL = =5
