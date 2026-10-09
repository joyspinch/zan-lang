# Zan.AppUpdate

> 源码: `packages/Zan.AppUpdate/src/Zan/AppUpdate/AppUpdater.zan`, `packages/Zan.AppUpdate/src/Zan/AppUpdate/ReleasePackager.zan`, `packages/Zan.AppUpdate/src/Zan/AppUpdate/TamperDetector.zan`, `packages/Zan.AppUpdate/src/Zan/AppUpdate/UpdateFileEntry.zan`, `packages/Zan.AppUpdate/src/Zan/AppUpdate/UpdateManifest.zan`, `packages/Zan.AppUpdate/src/Zan/AppUpdate/UpdateProgress.zan`


## AppUpdater (class)

- static string signerModulusHex="";

- static string signerExponentHex="010001";

- static void SetTrustedSigner(string modulusHex, string exponentHex)

- static bool VerifyManifestSignature(UpdateManifest manifest, UpdateProgressCallback cb)

- static bool HasNewVersion(string currentVersion, string remoteVersion)

- static List<string> SplitVersion(string v)

- static int ParseIntSafe(string s)

- static bool VerifyZipPackage(string zipPath, string expectedMd5, UpdateProgressCallback cb)

- static bool ApplyPackage(string zipPath, string targetAppDir, bool backupOld, UpdateProgressCallback cb)

- static bool ApplyPackage(string zipPath, string targetAppDir, UpdateProgressCallback cb, bool backupOld)

- static IntegrityReport VerifyAndSelfHeal(string targetAppDir, UpdateManifest manifest, string recoveryZipPath, UpdateProgressCallback cb)

- static bool UpdateTo(string currentVersion, string manifestPath, string localZipPath, string targetAppDir, bool backupOld, UpdateProgressCallback cb)

- static void RestoreExecutablePermissionIfNeeded(string filePath, byte[]content)


## ConsoleProgressRenderer (class)

- static void Print(UpdateProgressInfo info)

- static void Render(UpdateProgressInfo info)


## FileIntegrityStatus (class)

- static const int Ok=0;

- static const int Missing=1;

- static const int Tampered=2;

- static string StatusText(int s)


## IntegrityReport (class)

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

- static void CollectFiles(string baseDir, string relDir, List<string> outList)

- static UpdateManifest Pack(string appDir, string outputZipPath, string outputManifestPath, string appName, string version, string changelog, string packageUrl, UpdateProgressCallback cb)

- static string SignManifest(UpdateManifest manifest, string modulusHex, string privateExponentHex)


## TamperDetector (class)

- static IntegrityReport Check(string appDir, UpdateManifest manifest, UpdateProgressCallback cb)

- static int RestoreFromZip(string appDir, IntegrityReport report, string zipPackagePath, UpdateProgressCallback cb)


## TamperFileIssue (class)

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

- string path;

- long size;

- string md5;

- bool required;

- UpdateFileEntry()

- static UpdateFileEntry Create(string path, long size, string md5, bool required)

- static string NormalizePath(string p)

- static bool SafeRelPath(string p)

- JsonValue ToJsonValue()

- static UpdateFileEntry FromJsonValue(JsonValue obj)


## UpdateManifest (class)

- string appName;

- string version;

- string releaseDate;

- string packageUrl;

- long packageSize;

- string packageMd5;

- string changelog;

- List<UpdateFileEntry> files;

- string signature;

- UpdateManifest()

- string Signature()

- void SetSignature(string sigHex)

- string SigningPayload()

- void AddFile(UpdateFileEntry entry)

- UpdateFileEntry FindFile(string relPath)

- string ToJson(bool pretty)

- string ToJsonCore(bool pretty, string sigValue)

- void SaveToFile(string path)

- static UpdateManifest Parse(string jsonStr)

- static UpdateManifest LoadFromFile(string path)


## UpdatePhase (class)

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


## UpdateProgressInfo (class)

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

`delegate void UpdateProgressCallback(UpdateProgressInfo info);`
