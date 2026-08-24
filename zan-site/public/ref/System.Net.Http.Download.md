# System.Net.Http.Download

> 源码: `stdlib/System/Net/Http/Download/DownloadJob.zan`


## DownloadItem (class)

要下载的一个文件：源地址、落盘路径，以及可选的 SHA-1
校验与解压目标。`DownloadJob` 按加入顺序逐个处理。

- string host;

- int port;

- bool tls;

- string path;
  - 请求行里的路径（含查询串），如 "/app/plus2.exe"。

- string name;
  - 界面上显示的名字，默认取 URL 的最后一段。

- string localPath;
  - 落盘路径（断点续传就以这个文件已有的字节数为断点）。

- string sha1;
  - 期望的 SHA-1（40 位十六进制），"" = 不校验。

- long size;
  - 期望的字节数，0 = 未知（只影响总体百分比的估算）。

- string extractTo;
  - 解压/安装目标目录，"" = 下载完就算完。

- DownloadInstallFn installer;
  - 自定义安装步骤，null = 按扩展名走默认解压。

- DownloadItem()

- static DownloadItem FromUrl(string url, string localPath)
  - 从 `http(s)://host[:port]/path` 建一个条目；URL 解析不出
    主机时返回 null。

- bool SetUrl(string url)
  - 解析 URL 到 host/port/tls/path，成功返回 true。

- DownloadItem WithSha1(string hex)
  - 要校验的 SHA-1（40 位十六进制）；可链式调用。

- DownloadItem WithSize(long bytes)
  - 期望的字节数，用于在第一块正文到达前就给出总体进度。
    可链式调用。

- DownloadItem WithExtractTo(string dir)
  - 下载完成后解压/安装到 <paramref name="dir"/>：归档类型按
    落盘文件的扩展名判定（.tar.bz2 / .tbz2 / .tar / .zip），其余扩展名
    直接把文件复制过去。可链式调用。

- DownloadItem WithInstaller(DownloadInstallFn fn)
  - 下载完成后由 <paramref name="fn"/> 安装，而不是按扩展名
    解压（归档里要剥顶层目录、要打就绪标记之类的，交给调用方自己做）。
    可链式调用。

- DownloadItem WithName(string n)
  - 界面上显示的名字；可链式调用。

- string LocalPath()
  - 落盘路径（自定义安装步骤要用它找下好的文件）。

- string Name()
  - 界面上显示的名字。

- string ExtractTo()
  - 解压/安装目标目录，"" = 没有。

- string ProgressFile()
  - HttpClient 写进度用的伴生文件。

- static string LastSegment(string p)


## DownloadJob (class)

一批文件的下载 + 校验 + 解压安装，跑在自己的工作线程上。

界面无关（所以它在 System 下、不在 Gui 下：无界面的调用方不必因此
拖上 GUI 驱动）：`Gui.Component.Downloader.DownloadDialog`
只是它的一个观察者，命令行更新器和后台自更新可以直接用它（<c>Start()</c> 之后轮询
`Running` / `Stage`）。传输本身复用
`HttpClient.DownloadBinaryToFileAsync`，因此断点续传、
Range、超时的语义与别处一致。

线程分工与 AiNet 一致：<c>Thread.Start</c> 收的是非捕获委托，所以任务
对象经一个受锁保护的队列交给 worker；worker 只写阶段/序号/当前文件名/
错误（受同一把锁），GUI 线程每帧读一次。单个文件的字节进度不走锁：
HttpClient 把 "已收/总计" 写进伴生进度文件，读的那一侧（GUI 或
`Poll` 的调用方）自己去读，因此外部下载器也能驱动同一个
窗口。取消走一个原子标志：装到 HttpClient 上的探针在每块正文之后读它，
因此一个 160 MB 的下载也能立刻收工，已落盘的字节留着下次续传。

- static DownloadJob sCur;
  - 正在跑的任务（同一时刻只驱动一个：更新器天然是串行的）。

- static List<DownloadJob> sPend;
  - 交给 worker 但还没被领走的任务，以及保护它和共享状态的锁。

- static nint sLock;

- static string sLock;

- static AtomicInt sCancel;
  - 取消标志。原子而非受锁，因为 HttpClient 的探针每 64 KB 问一次。

- List<DownloadItem> items;

- DownloadPrepareFn prepare;
  - 开跑前补齐条目的回调，null = 条目已经齐了。

- int stage;

- int index;

- string current;

- string error;

- string downloadError;
  - 最近一次传输失败的阶段诊断，由 FetchAsync 从 HttpClient 转存。

- int failedStage;
  - SetFail 把 stage 改成 Failed 前保存的阶段，供调用方判断失败类型。

- bool running;

- long got;

- long total;

- DownloadJob()

- static void EnsureShared()

- static void Lock()

- static void Unlock()

- DownloadJob Add(DownloadItem it)
  - 加入一个待下载条目（<c>Start()</c> 之前）。

- DownloadJob SetPrepare(DownloadPrepareFn fn)
  - 把「有哪些文件要下」的决定推迟到工作线程上：<c>Start()</c>
    之后、第一个条目开跑之前调用 <paramref name="fn"/>，它在里面
    <c>Add</c>（可以先联网问）。设了它，<c>Start()</c> 就允许条目为空。

- int Count()
  - 条目数。

- DownloadItem ItemAt(int i)

- bool Start()
  - 在工作线程上开始处理。返回 false = 线程起不来（此时任务
    状态为失败，原因见 `Error`）。

- void Cancel()
  - 请求取消：下载在下一块正文的边界退出，已下载的部分留着
    续传。不强杀线程，因此调用后任务还会“运行”一小会儿。

- static bool CancelRequested()
  - HttpClient 的取消探针（装到客户端上的那个静态委托）。

- static void Worker()
  - worker 线程入口：领一个任务并驱动到底。

- static DownloadJob Take()
  - 领走一个排队的任务（别的 worker 已经领走时返回 null）。

- static void Drive(DownloadJob job)
  - 整个任务在工作线程上的主体。每个条目：下载 → 校验 →
    解压安装。任何一步失败就带着原因收工，后面的条目不再处理。

- async long FetchAsync(DownloadItem it)
  - 一个条目的传输，断点续传由 HttpClient 负责。

- static bool Install(DownloadItem it)
  - 把下载好的文件安装到 <c>extractTo</c>：归档按扩展名解开，
    其余扩展名直接复制过去。

- void Begin(int i, DownloadItem it)

- void SetStage(int s)

- void SetFail(string msg)

- void SetError(string msg)

- void Finish()

- bool Running()
  - 任务是否还在跑（worker 线程尚未收工）。

- int Stage()
  - 当前阶段（见 `DownloadStage`）。

- int FailedStage()
  - 失败前所处的阶段；非 Failed 状态时通常为 Idle。

- string Error()
  - 失败原因，未失败时 ""。

- string Current()
  - 正在处理的条目名（界面上的“下载文件”那一行）。

- int Index()
  - 正在处理的条目序号（0 起）。

- int DoneCount()
  - 已完成的条目数：跑完时就是全部。

- bool Succeeded()
  - 成功收工。

- void Poll()
  - 从当前条目的伴生进度文件刷新字节进度。读者（界面每帧、
    或命令行的轮询循环）调用；worker 正阻塞在传输里，读文件的活只能
    由读者做。

- long Got()
  - 当前条目已收到的字节数（`Poll` 后有效）。

- long Total()
  - 当前条目的总字节数，未知时 0。

- int ItemPercent()
  - 当前条目的百分比（总量未知时按 0 报）。

- int TotalPercent()
  - 整个任务的百分比：已完成的条目算满，当前条目按其自身
    百分比折算。条目大小差得多时这只是个估算，但和界面上
    “n/m 个文件”对得上，比按字节算更不容易让人误解。

- static string Human(long bytes)
  - 把字节数写成人看的样子（"162.8 MB"）。

- static string Tenths(long tenths)
  - 十倍定点数写成一位小数。

- static string ParentDir(string path)


## DownloadStage (class)

下载任务的阶段码。
`Gui.Component.Downloader.DownloadDialog` 把它翻成一行
提示文字，无界面的调用方用它判断结局。

- static int Idle=0;

- static int Download=1;

- static int Verify=2;

- static int Extract=3;

- static int Done=4;

- static int Failed=5;

- static int Cancelled=6;

- static string Text(int stage)
  - 阶段的中文提示；宿主自己管界面语言时不用它，直接按阶段码
    出自己的文案。


## bool (delegate)

条目自己的安装步骤（在工作线程上、Extract 阶段调用）：归档
布局有讲究的调用方用它替掉按扩展名的默认解压。见
`DownloadItem.WithInstaller`。

`delegate bool DownloadInstallFn(DownloadItem it);`


## bool (delegate)

任务开跑前在工作线程上补齐条目（可以联网：CEF 要先拉索引
才知道该下哪个归档）。返回 false = 准备失败，任务以失败收工。见
`DownloadJob.SetPrepare`。

`delegate bool DownloadPrepareFn(DownloadJob job);`
