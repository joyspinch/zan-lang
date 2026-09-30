# Zan.AppUpdate

> 源码: `packages/Zan.AppUpdate/src/Zan/AppUpdate/AppUpdater.zan`, `packages/Zan.AppUpdate/src/Zan/AppUpdate/ReleasePackager.zan`, `packages/Zan.AppUpdate/src/Zan/AppUpdate/TamperDetector.zan`, `packages/Zan.AppUpdate/src/Zan/AppUpdate/UpdateFileEntry.zan`, `packages/Zan.AppUpdate/src/Zan/AppUpdate/UpdateManifest.zan`, `packages/Zan.AppUpdate/src/Zan/AppUpdate/UpdateProgress.zan`


## AppUpdater (class)

客户端版本更新总控引擎。
提供版本比对、更新包校验、旧版备份、Zip 解压覆盖与防篡改自检自愈的一体化工作流。

- static bool HasNewVersion(string currentVersion, string remoteVersion)
  - 比较本地版本号与远程版本号。若 remoteVersion 比 currentVersion 新返回 true。

- static List<string> SplitVersion(string v)

- static int ParseIntSafe(string s)

- static bool VerifyZipPackage(string zipPath, string expectedMd5, UpdateProgressCallback cb)
  - 校验下载得到的 Zip 更新包 MD5 是否与清单一致。

- static bool ApplyPackage(string zipPath, string targetAppDir, bool backupOld, UpdateProgressCallback cb)
  - 解压 Zip 更新包并应用到目标应用程序目录（支持先备份旧版本）。

- static bool ApplyPackage(string zipPath, string targetAppDir, UpdateProgressCallback cb, bool backupOld)
  - 解压 Zip 更新包并应用到目标应用程序目录（参数重载）。

- static IntegrityReport VerifyAndSelfHeal(string targetAppDir, UpdateManifest manifest, string recoveryZipPath, UpdateProgressCallback cb)
  - 执行更新后防篡改检测并自动还原被篡改文件。

- static bool UpdateTo(string currentVersion, string manifestPath, string localZipPath, string targetAppDir, bool backupOld, UpdateProgressCallback cb)
  - 执行完整更新流程：版本检查 -> 整包 MD5 校验 -> 解压覆盖 -> MD5 防篡改比对与自愈还原。

- static void RestoreExecutablePermissionIfNeeded(string filePath, byte[]content)
  - 若写入的文件属于可执行文件（ELF、Mach-O、脚本 shebang、.sh 等），在 POSIX 平台上自动赋予可执行权限 (0755)。


## ConsoleProgressRenderer (class)

控制台简易进度条渲染器，适合命令行工具及无头运行环境。

- static void Print(UpdateProgressInfo info)

- static void Render(UpdateProgressInfo info)
  - 渲染进度信息（同 Print）。


## FileIntegrityStatus (class)

文件篡改状态枚举。

- static const int Ok=0;

- static const int Missing=1;

- static const int Tampered=2;

- static string StatusText(int s)


## IntegrityReport (class)

程序完整性自检报表。

- bool isClean;

- int totalChecked;

- int okCount;

- int missingCount;

- int tamperedCount;

- int restoredCount;

- List<TamperFileIssue> issues;

- IntegrityReport()

- void AddIssue(TamperFileIssue issue)

- string Summary()


## ReleasePackager (class)

版本发布打包器：扫描程序目录、计算所有文件 MD5、生成更新清单、压缩为 Zip 包。

- static void CollectFiles(string baseDir, string relDir, List<string> outList)
  - 递归收集目录下的所有相对路径文件。

- static UpdateManifest Pack(string appDir, string outputZipPath, string outputManifestPath, string appName, string version, string changelog, string packageUrl, UpdateProgressCallback cb)
  - 将指定应用程序目录打包为版本更新 Zip 包，并生成 Manifest 清单。
    
    应用程序所在根目录
    输出的 Zip 压缩包完整路径
    输出的 Manifest.json 路径（传空则放在 Zip 同级目录）
    应用程序名称
    新发布的版本号（如 1.2.0）
    更新日志说明
    远程部署时的下载 URL
    进度回调委托（可传 null）
    构建完成的 UpdateManifest 对象


## TamperDetector (class)

防篡改检测与自愈还原引擎。
通过 MD5 哈希比对检验程序完整性，并在发现损坏/被篡改时从官方 Zip 包自动还原。

- static IntegrityReport Check(string appDir, UpdateManifest manifest, UpdateProgressCallback cb)
  - 对本地应用程序目录进行全量 MD5 防篡改比对自检。

- static int RestoreFromZip(string appDir, IntegrityReport report, string zipPackagePath, UpdateProgressCallback cb)
  - 根据防篡改自检报表，从官方 Zip 更新包中提取正版文件，自动还原被篡改或缺失的文件。
    
    目标应用程序根目录
    自检报表
    官方原始 Zip 压缩包路径
    进度回调
    修复成功的数量


## TamperFileIssue (class)

单个异常文件的检测项详情。

- string path;

- int status;

- string expectedMd5;

- string actualMd5;

- long expectedSize;

- long actualSize;

- bool restored;

- TamperFileIssue()

- static TamperFileIssue Create(string path, int status, string expMd5, string actMd5, long expSize, long actSize)


## UpdateFileEntry (class)

版本更新中的单个文件条目元信息。

- string path;

- long size;

- string md5;

- bool required;

- UpdateFileEntry()

- static UpdateFileEntry Create(string path, long size, string md5, bool required)

- static string NormalizePath(string p)
  - 统一路径分隔符为正斜杠，去除开头的斜杠。

- JsonValue ToJsonValue()
  - 转换为 Json 节点。

- static UpdateFileEntry FromJsonValue(JsonValue obj)
  - 从 Json 节点反序列化。


## UpdateManifest (class)

应用程序版本发布清单与更新描述。

- string appName;

- string version;

- string releaseDate;

- string packageUrl;

- long packageSize;

- string packageMd5;

- string changelog;

- List<UpdateFileEntry> files;

- UpdateManifest()

- void AddFile(UpdateFileEntry entry)
  - 添加一个文件条目。

- UpdateFileEntry FindFile(string relPath)
  - 根据相对路径查找文件条目。

- string ToJson(bool pretty)
  - 序列化为 JSON 字符串。

- void SaveToFile(string path)
  - 保存清单到本地文件。

- static UpdateManifest Parse(string jsonStr)
  - 从 JSON 字符串反序列化清单。

- static UpdateManifest LoadFromFile(string path)
  - 从本地文件加载清单。


## UpdatePhase (class)

更新生命周期阶段常量。

- static const int Idle=0;

- static const int Checking=1;

- static const int Downloading=2;

- static const int Verifying=3;

- static const int Extracting=4;

- static const int SelfHealing=5;

- static const int Completed=6;

- static const int Failed=7;

- static const int Cancelled=8;

- static string PhaseName(int phase)
  - 获取阶段的中文描述文本。


## UpdateProgressInfo (class)

更新过程中的进度状态快照。

- int phase;

- int percent;

- long bytesCurrent;

- long bytesTotal;

- string currentFile;

- string message;

- string error;

- UpdateProgressInfo()

- static UpdateProgressInfo Create(int phase, int percent, string currentFile, string message)

- static UpdateProgressInfo Progress(int phase, int percent, long currentBytes, long totalBytes, string file, string msg)

- static UpdateProgressInfo Fail(int phase, string errMsg)


## void (delegate)

更新进度回调委托。

`delegate void UpdateProgressCallback(UpdateProgressInfo info);`
