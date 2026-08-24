# Gui.Component.Downloader

> 源码: `stdlib/Gui/Component/Downloader/DownloadDialog.zan`


## DownloadDialog (class)

通用的“下载 + 安装（解压）”进度窗：一行总体进度（n/m 个
文件）、一行当前文件进度、一行阶段提示，外加取消。

它只是 `DownloadJob` 的观察者：任务跑在自己的工作线程上，
关掉窗口不会打断它（标题下那句提示就是这个意思），因此同一个任务可以
有窗口、也可以没有。CEF 运行时下载是第一个调用方，程序自更新、包管理
器下载复用同一个类：

DownloadJob job = new DownloadJob();
job.Add(DownloadItem.FromUrl(url, dest).WithSha1(sha).WithExtractTo(dir));
job.Start();
DownloadDialog dlg = DownloadDialog.Show(app, "系统检查更新", job);

没有主窗口的启动阶段可用 `OpenStandalone` + `RunStandalone`：它自己
驱动唯一的顶层窗口，用户关窗不取消后台任务，关窗后任务静默跑完。

刷新走 <c>App.RequestAnimationFrame(200)</c>：任务在跑时每 200ms 出一帧，
跑完就不再武装截止时间，因此空闲时零 CPU（直接让 <c>Pending()</c> 恒真
会让主循环满速忙渲染）。字节进度由 `DownloadJob.Poll` 从
伴生进度文件读，所以外部下载器写同一个文件也能驱动这个窗口。

- static DownloadDialog current;
  - 当前窗口。设计里的事件处理器是非捕获的静态方法，取消按钮通过
    它找到自己的窗口（与 ZanIDE 各子窗口一致的做法）。

- DownloadJob job;

- string caption;

- string intro;

- string hint;

- string tFileProgress;
  - 界面文案，默认中文；换语言的宿主用 SetTexts 覆盖。

- string tDownloadFile;

- string tFilesUnit;

- string tCancel;

- string tClose;

- Label introLbl;

- Label hintLbl;

- Label fileProgressLbl;

- Label downloadFileLbl;

- Label countLbl;

- Label nameLbl;

- Label stageLbl;

- ScrollColumn introBox;

- Progress overall;

- Progress single;

- Button cancelBtn;

- DownloadDialog()

- static DownloadDialog Show(App parent, string title, DownloadJob j)
  - 开一个跟着 <paramref name="j"/> 的进度窗。任务可以先
    <c>Start()</c> 也可以后 <c>Start()</c>：窗口只读状态。

- static DownloadDialog OpenStandalone(string title, DownloadJob j)
  - 在还没有主窗口时显示独立的下载进度窗。调用方负责在
    <c>RunStandalone</c> 前启动任务；窗口创建失败会把异常交给调用方，
    方便无显示器宿主回退到静默路径。

- async bool RunStandalone()
  - 驱动独立窗口直到任务成功或用户关窗，并返回是否成功。
    失败/取消时窗口保留，调用方可以先更新说明文字，再调用
    `WaitStandaloneClose` 等用户关窗。用户关窗不会取消任务；
    关窗后不再绘制界面，剩余工作静默等到收工。

- void WaitStandaloneClose()
  - 失败/取消后继续驱动窗口，直到用户关窗。

- void SetIntro(string text)
  - 标题下面那段说明文字（"" = 不显示）。<c>Show</c> 之前
    或之后都能调用。

- void SetHint(string text)
  - 说明下面那行弱化提示（默认是“本窗消失不影响更新进度”）。

- void SetTexts(string fileProgress, string downloadFile, string filesUnit, string cancel, string close)
  - 覆盖界面文案（宿主自己管界面语言时用）。

- void Open(App parent)

- void BuildUi()

- static Panel HeaderRow(Label title, Label value)
  - 一行“标题 …… 右对齐的值”。

- override string Title()

- override int Width()

- override int Height()

- override bool ShowMaximize()

- override int IdBase()

- override void OnCloseRequested()
  - 关窗不取消任务：worker 线程照旧把更新装完（标题下那句
    提示承诺的正是这一点）。要真的停下来请按取消。

- static void OnCancel()
  - 取消按钮：请求任务在下一个块边界收工。已下载的部分留在
    磁盘上，下次进来从断点续传。

- override void AfterFrame()
  - 每帧把任务状态搬到控件上，并在任务还在跑时武装下一次
    限速重绘。

- string CountText()
  - "1/2 个文件"。

- string NameText()
  - 当前文件名 + 已收/总量。

- string StatusText()
  - 阶段一行。失败原因去说明区（可滚动），这里只留阶段，
    免得同一段长文案在两处各自换行、把布局撑破。
