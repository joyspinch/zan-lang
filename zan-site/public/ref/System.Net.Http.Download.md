# System.Net.Http.Download

> 源码: `packages/Zan.Net/src/System/Net/Http/Download/DownloadJob.zan`


## DownloadItem (class)

- string host;

- int port;

- bool tls;

- string targetUrl;

- string path;

- string name;

- string localPath;

- string sha1;

- long size;

- string extractTo;

- DownloadInstallFn installer;

- DownloadItem()

- static DownloadItem FromUrl(string url, string localPath)

- bool SetUrl(string url)

- DownloadItem WithSha1(string hex)

- DownloadItem WithSize(long bytes)

- DownloadItem WithExtractTo(string dir)

- DownloadItem WithInstaller(DownloadInstallFn fn)

- DownloadItem WithName(string n)

- string LocalPath()

- string Name()

- string ExtractTo()

- string ProgressFile()

- static string LastSegment(string p)


## DownloadJob (class)

- static DownloadJob sCur;

- static List<DownloadJob> sPend;

- static nint sLock;

- static nint sLock;

- static AtomicInt sCancel;

- List<DownloadItem> items;

- ExternalCallPolicy callPolicy;

- DownloadPrepareFn prepare;

- int stage;

- int index;

- string current;

- string error;

- string downloadError;

- int failedStage;

- bool running;

- long got;

- long total;

- DownloadJob()

- static void EnsureShared()

- static void Lock()

- static void Unlock()

- DownloadJob Add(DownloadItem it)

- DownloadJob SetCallPolicy(ExternalCallPolicy policy)

- DownloadJob SetPrepare(DownloadPrepareFn fn)

- int Count()

- DownloadItem ItemAt(int i)

- bool Start()

- void Cancel()

- static bool CancelRequested()

- static void Worker()

- static DownloadJob Take()

- static void Drive(DownloadJob job)

- static ExternalCallPolicy DefaultCallPolicy()

- static ExternalCallPolicy SnapshotPolicy(ExternalCallPolicy source)

- async long FetchAsync(DownloadItem it)

- static bool Install(DownloadItem it)

- void Begin(int i, DownloadItem it)

- void SetStage(int s)

- void SetFail(string msg)

- void SetError(string msg)

- void Finish()

- bool Running()

- int Stage()

- int FailedStage()

- string Error()

- string Current()

- int Index()

- int DoneCount()

- bool Succeeded()

- void Poll()

- long Got()

- long Total()

- int ItemPercent()

- int TotalPercent()

- static string Human(long bytes)

- static string Tenths(long tenths)

- static string ParentDir(string path)


## DownloadStage (class)

- static const int Idle=0;

- static const int Download=1;

- static const int Verify=2;

- static const int Extract=3;

- static const int Done=4;

- static const int Failed=5;

- static const int Cancelled=6;

- static string Text(int stage)


## bool (delegate)

`delegate bool DownloadInstallFn(DownloadItem it);`


## bool (delegate)

`delegate bool DownloadPrepareFn(DownloadJob job);`
