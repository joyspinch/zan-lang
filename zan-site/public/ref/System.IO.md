# System.IO

> 源码: `stdlib/System/IO/ByteBuffer.zan`, `stdlib/System/IO/Directory.zan`, `stdlib/System/IO/DirectoryTree.zan`, `stdlib/System/IO/DirectoryWatcher.zan`, `stdlib/System/IO/File.zan`, `stdlib/System/IO/FileInfo.zan`, `stdlib/System/IO/FileInfoEx.zan`, `stdlib/System/IO/FileStream.zan`, `stdlib/System/IO/IniFile.zan`, `stdlib/System/IO/KnownFolders.zan`, `stdlib/System/IO/MemoryMappedFile.zan`, `stdlib/System/IO/MemoryStream.zan`, `stdlib/System/IO/Path.zan`, `stdlib/System/IO/PathEx.zan`, `stdlib/System/IO/Shortcut.zan`, `stdlib/System/IO/Stream.zan`, `stdlib/System/IO/StreamReader.zan`, `stdlib/System/IO/StreamWriter.zan`


## ByteBuffer (class)

可增长的二进制缓冲区，显式跟踪长度，小端序
编码基础类型。底层存储是原始堆外内存，以
nint 地址持有（绝不是 ARC 字符串），因此内容可包含 NUL 字节，
并可通过 `Raw` 直接传给 FFI 调用。

写入在写位置处追加；读取则从独立的
读位置消费：
ByteBuffer b = ByteBuffer.Alloc(64);
b.WriteU32(1234).WriteVarInt(5).WriteString("hello");
b.SeekRead(0);
long v = b.ReadU32();
用完后调用 `Free` 释放。

- nint data;

- int capacity;

- int length;

- int rpos;

- ByteBuffer()

- static ByteBuffer Alloc(int initialCapacity)
  - 分配指定初始容量的缓冲区。

- static ByteBuffer FromRaw(nint src, int count)
  - 包装从现有原始块复制出的 count 字节。

- static ByteBuffer FromStr(string s)
  - 将字符串的原始字节复制到新缓冲区以供读取
    （跟踪长度；二进制安全，可含 NUL）。用 ReadU8 /
    ReadVarInt / ReadString 从位置 0 开始读取。

- string ToStr()
  - 将已写入的字节以字符串形式返回（二进制安全：字符串
    携带显式长度，因此内嵌的 NUL 字节得以保留）。

- void Free()
  - 释放底层内存分配。

- int Length()
  - 目前已写入的字节数。

- int ReadPos()
  - 当前读位置。

- int Remaining()
  - 剩余未读字节数。

- nint Raw()
  - 供 FFI 调用的原始底层地址（Free/扩容前有效）。

- ByteBuffer SeekRead(int pos)
  - 移动读游标（越界时钳制到 [0, length]）。

- ByteBuffer Clear()
  - 重置写读位置，保留内存分配。

- ByteBuffer EnsureCapacity(int cap)
  - 扩容内存，确保至少能容纳 cap 字节。

- ByteBuffer SetLength(int n)
  - 外部代码（如 FFI 读取）直接填满底层存储后，
    设置逻辑长度。不得超过容量。

- void EnsureRoom(int extra)

- ByteBuffer WriteU8(int v)

- ByteBuffer WriteU16(int v)

- ByteBuffer WriteU32(long v)

- ByteBuffer WriteU64(long v)

- ByteBuffer WriteVarInt(long v)
  - 无符号 LEB128 变长整数（0-127 占 1 字节）。

- ByteBuffer WriteBytes(nint src, int count)
  - 从原始块或另一个缓冲区的 Raw() 复制 count 字节。

- ByteBuffer WriteRaw(string src, int count)
  - 追加 <paramref name="src"/> 开头的 count 个字节，不写
    长度前缀。长度由调用方给出，因此二进制安全：字符串本身
    以 NUL 结尾，<c>s.Length</c> 只数到第一个 NUL，而网络收到的
    字节（HTTP 二进制请求体、上传内容）本就可能含 NUL。

- int ByteAt(int i)
  - 已写入字节中下标 i 处的字节（0-255）；越界返回 -1。
    供扫描分隔符（HTTP 头部空行、分块长度行）时逐字节查看，
    无需先把缓冲区复制成字符串。

- int IndexOfByte(int from, int b)
  - 从下标 from 起第一个等于 b 的字节的下标，没有则 -1。
    分隔符扫描（HTTP 头部空行、分块长度行）走 memchr：逐字节
    `ByteAt` 每个字节都要付一次调用、一次视图构造和一次
    边界检查，而这本来是整块缓冲区上的向量化循环。

- string Str(int off, int count)
  - 已写入字节中 [off, off+count) 的切片，以字符串返回
    （二进制安全：长度显式给出，内嵌 NUL 得以保留）。
    越界的区间被钳制到已写入范围内。

- ByteBuffer Discard(int count)
  - 丢弃开头的 count 个字节，将其余字节左移。保留内存
    分配，供“消费完一个消息、把流水线剩余字节留到下轮”的
    拆包循环复用同一块缓冲区。

- ByteBuffer WriteString(string s)
  - 写入长度前缀（varint）UTF-8 字符串。

- bool CanRead(int n)
  - 从当前读位置起还能读取 n 字节时为真
    （读取前先用它判断，防止越过缓冲区末尾）。

- int ReadU8()

- int ReadU16()

- long ReadU32()

- long ReadU64()

- long ReadVarInt()

- nint ReadBytes(int count)
  - 将 count 字节读入新分配的原始块
    （调用方用 NativeMemory.Free 释放，或包装进 ByteBuffer）。

- string ReadString()
  - 读取由 WriteString 写入的长度前缀字符串。

- long Crc32(int count)
  - 对前 count 字节计算 CRC32（IEEE，反射）。


## Directory (class)

目录操作。Windows 上使用 Win32 API，Linux 与 macOS 上使用 POSIX
opendir/readdir/mkdir。readdir 读取的 struct dirent 字段偏移
在 glibc 与 Darwin 之间不同，因此按目标平台选择
（见 direntTypeOffset/direntNameOffset）。

- [DllImport("crt")]static extern string zan_file_read_path(string path);

- static string ReadPath(string path)
  - 把只读枚举用的相对路径解析到程序随附的那一份。

- [DllImport("crt")]static extern string zan_embed_list(string prefix);

- static List<string> EmbedChildren(string path, bool dirs)
  - 内嵌资源里 `path` 下的直接子项：dirs 为真取子目录名，
    否则取文件名。磁盘上的同名目录仍然优先，这里只补上磁盘里没有的
    那部分，发布成单个可执行文件后 views/ 之类照样能枚举。

- static bool EmbedExists(string path)
  - 内嵌资源里是否有这个目录（即有资源以它为前缀）。

- static List<string> MergeEmbed(List<string> disk, string path, bool dirs)
  - 把内嵌资源里的子项并进磁盘枚举结果（磁盘优先，不重复）。

- [DllImport("kernel32", EntryPoint="GetFileAttributesW")]static extern int WinGetFileAttributes(string path);

- [DllImport("kernel32", EntryPoint="CreateDirectoryW")]static extern int WinCreateDirectory(string path, nint secAttrs);

- [DllImport("kernel32", EntryPoint="RemoveDirectoryW")]static extern int WinRemoveDirectory(string path);

- [DllImport("kernel32", EntryPoint="GetCurrentDirectoryW")]static extern int WinGetCurrentDirectory(int bufLen, string buf);

- [DllImport("kernel32", EntryPoint="SetCurrentDirectoryW")]static extern int WinSetCurrentDirectory(string path);

- [DllImport("kernel32", EntryPoint="FindFirstFileW")]static extern nint WinFindFirstFile(string pattern, string findData);

- [DllImport("kernel32", EntryPoint="FindNextFileW")]static extern int WinFindNextFile(nint handle, string findData);

- [DllImport("kernel32", EntryPoint="FindClose")]static extern int WinFindClose(nint handle);

- [DllImport("kernel32", EntryPoint="GetLogicalDrives")]static extern int WinGetLogicalDrives();

- [DllImport("kernel32", EntryPoint="MultiByteToWideChar")]static extern int WinToWide(int codePage, int flags, string source, int sourceLength, string output, int outputLength);

- [DllImport("kernel32", EntryPoint="WideCharToMultiByte")]static extern int WinToUtf8(int codePage, int flags, string source, int sourceLength, string output, int outputLength, string defaultChar, string usedDefaultChar);

- [DllImport("crt", EntryPoint="opendir")]static extern nint opendir(string name);

- [DllImport("crt", EntryPoint="readdir")]static extern string readdir(nint dirp);

- [DllImport("crt", EntryPoint="closedir")]static extern int closedir(nint dirp);

- [DllImport("crt", EntryPoint="mkdir")]static extern int mkdir(string path, int mode);

- [DllImport("crt", EntryPoint="rmdir")]static extern int rmdir(string path);

- [DllImport("crt", EntryPoint="chdir")]static extern int chdir(string path);

- [DllImport("crt", EntryPoint="getcwd")]static extern string getcwd(string buf, int size);

- static byte[]winWide(string text)

- static string winUtf8(string wide)

- static int winAttributesUtf8(string path)

- static bool ExistsUtf8(string path)
  - 检查 UTF-8 路径是否存在（不使用 ANSI Windows API）。

- static bool FileExistsUtf8(string path)
  - 检查 UTF-8 路径是否指向普通文件。

- static void CreateDirectoryUtf8(string path)
  - 创建路径以 UTF-8 编码的目录。

- static List<string> GetRootsUtf8()

- static bool Exists(string path)
  - 检查目录是否存在。工作目录下找不到时，与 GetFiles/
    GetDirectories 一样回退到程序随附的那一份（单文件包的解包目录、
    可执行文件所在目录），否则发布后的程序会在枚举之前就判定
    views/、wwwroot/ 不存在。

- static bool ExistsOnDisk(string path)

- static void CreateDirectory(string path)
  - 在指定路径创建目录。

- static void Delete(string path)
  - 删除空目录。

- static string GetCurrentDirectory()
  - 获取当前工作目录。

- static void SetCurrentDirectory(string path)
  - 设置当前工作目录。

- static string winName(byte[]findData)

- static List<string> winList(string path, bool wantDirs, bool includeHidden)

- static int direntTypeOffset()

- static int direntNameOffset()

- static int direntTypeOffset()

- static int direntNameOffset()

- static string posixName(string ent)

- static List<string> posixList(string path, bool wantDirs, bool includeHidden)

- static List<string> GetFiles(string path)
  - 列出目录中的文件（不递归）。

- static List<string> GetDirectories(string path)
  - 列出目录中的子目录。

- static List<string> GetFilesFiltered(string path, bool includeHidden)

- static List<string> GetDirectoriesFiltered(string path, bool includeHidden)

- static void CreateDirectoryRecursive(string path)
  - 递归创建路径中的所有目录。

- static bool DeleteRecursive(string path)
  - 递归删除目录及其全部内容。目录本来就不存在时返回 true；
    任何一项删不掉（占用/权限）时返回 false，其余项仍会尽力删除。


## DirectoryTree (class)

目录遍历的唯一实现：递归收集匹配的文件路径。Directory 只给出单层的
名字列表，所以每个调用点以前都要自己写一遍「递归 + 后缀判断 + 跳过
build/bin/. 开头目录」；改成：

List<string> sources = DirectoryTree.Files(root,
PathFilter.Only(".zan").Skip("build bin obj"));

返回的都是可直接打开的完整路径（以 "/" 连接）。

- static bool EndsWith(string s, string suffix)
  - `s` 以 `suffix` 结尾时为 true。

- static void FilesInto(List<string> outp, string dirPath, PathFilter f)
  - 递归收集匹配的文件完整路径（不含目录）。

- static List<string> Files(string dirPath, PathFilter f)

- static void FlatInto(List<string> outp, string dirPath, PathFilter f)
  - 单层文件的完整路径（不递归）。

- static List<string> Flat(string dirPath, PathFilter f)

- static List<string> Names(string dirPath, PathFilter f)
  - 单层文件名（不递归），按过滤条件筛选。

- static List<string> SubDirs(string dirPath, PathFilter f)
  - 可进入的子目录完整路径（不递归）。

- static List<string> SubNames(string dirPath, PathFilter f)
  - 子目录名（不递归），按过滤条件筛选。


## DirectoryWatcher (class)

监听目录并上报文件变化（新增/删除/修改）。
后台线程约每 250 ms 轮询一次目录列表，并与
上一份快照按名称 + 最后写入时间比较，因此
所有平台行为一致（无需 ReadDirectoryChangesW / inotify 相关代码），
代价是最多约 250 ms 的延迟。不递归子目录。

DirectoryWatcher.Watch("C:\\data", OnChange);
...
DirectoryWatcher.Stop();

回调收到条目名与一个 `FileChange` 类型，
且必须是非捕获（non-capturing）的。

- static List<FileSnapshot> lastSnapshot=new List<FileSnapshot>();

- static bool watching=false;

- static bool Watch(string path, FileChangeFn callback)
  - 开始监听 `path`。以下情况返回 false：
    目录不存在或已有监听器在运行。

- static void Stop()
  - 停止监听。当前这轮轮询结束后不再
    投递任何事件。

- static void Poll()

- static List<FileSnapshot> Snapshot()

- static void Compare(List<FileSnapshot> current)

- static string dir="";

- static FileChangeFn cb;


## File (class)

基于 C 运行时（Windows 为 msvcrt，POSIX 为 libc）的跨平台文件操作。
所有方法均支持 Windows 与 Linux/macOS。

- [DllImport("crt", EntryPoint="zan_file_fopen")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern nint zan_pkg_fopen(string path, string mode);
  - fopen() for READS that also looks inside a single-file program's
    unpacked payload (ZAN_PKG_DIR). A packaged program runs with the
    launcher's own directory as its working directory, so its bundled
    read-only resources are not reachable relative to it; writes stay on
    plain fopen so they land next to the program, never in the cache.

- [DllImport("crt")]static extern int fputs(string str, nint fp);

- [DllImport("crt")]static extern int fclose(nint fp);

- [DllImport("crt")]static extern int fseek(nint fp, int offset, int origin);

- [DllImport("crt")]static extern int ftell(nint fp);

- [DllImport("crt")]static extern long fread(string buf, long size, long count, nint fp);

- [DllImport("crt")]static extern long fwrite(string buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="zan_file_remove")]static extern int remove(string path);

- [DllImport("crt", EntryPoint="zan_file_rename")]static extern int rename(string oldname, string newname);

- [DllImport("crt")]static extern int fputc(int ch, nint fp);

- [DllImport("crt")]static extern long zan_file_attributes(string path);

- [DllImport("crt")]static extern int zan_embed_has(string name);

- [DllImport("crt")]static extern string zan_embed_read(string name);

- [DllImport("crt")]static extern nint zan_embed_bytes(string name, byte[]outLen);

- [DllImport("crt")]static extern string zan_embed_list(string prefix);

- static string EmbedName(string path)
  - 把路径规范成内嵌资源名：内嵌资源以项目相对路径命名，
    "./config/app.json" 与 "config\\app.json" 都指向
    "config/app.json"。

- static List<string> EmbeddedNames(string prefix)
  - 列出以 <paramref name="prefix"/> 开头的内嵌资源名（"" 为
    全部）。返回的名字可直接交给 `ExtractEmbedded` 或
    `ReadAllText`。

- static bool EmbedExists(string path)
  - 该路径是否有内嵌副本。

- static string ReadAllText(string path)
  - 以字符串形式读取整个文件内容。
    文件无法打开时抛出 FileNotFoundException。

- static string ReadAllTextAsync(string path)
  - 读取文件内容（ReadAllText 的包装）。

- static void WriteAllText(string path, string content)
  - 将字符串写入文件，不存在则创建，已存在则覆盖。
    文件无法以写入方式打开时抛出 IOException。
    
    以二进制模式按显式长度写出：文本模式（"w"）会在 Windows 上
    把 '\n' 翻成 "\r\n"，而 fputs 会在首个 NUL 处停止 —— 两者都会让
    写回的内容与传入的字符串不一致（写文件再读回不等于原文）。

- static void WriteAllTextAsync(string path, string content)
  - 写入文件内容（WriteAllText 的包装）。

- static void AppendAllText(string path, string content)
  - 将字符串追加到文件末尾。
    文件无法以追加方式打开时抛出 IOException。
    与 WriteAllText 同理，用二进制模式按长度写出。

- static void AppendAllTextAsync(string path, string content)
  - 追加文本（AppendAllText 的包装）。

- static bool Exists(string path)
  - 检查路径是否存在且为文件。目录返回 false
    （用 Directory.Exists 判断目录）。
    
    不能用 fopen 判存在：POSIX 上 <c>fopen(dir, "rb")</c> 会成功，
    于是目录会被当成文件（调用方紧接着就会去 remove/读它），
    而 Windows 上同一调用失败 —— 同一份代码在两个平台行为相反。

- [DllImport("crt")]static extern long zan_file_try_lock(string path);

- [DllImport("crt")]static extern long zan_file_unlock(long handle);

- static long TryLock(string path)
  - 抢占 <paramref name="path"/> 的跨进程独占锁（文件不存在
    就创建），拿到返回非 0 句柄，已被别的进程占用返回 0，不等待。
    
    锁就是那个打开的系统句柄：进程退出（含被杀、崩溃）时由内核释放，
    因此不像 pid 文件那样会留下需要清理的陈旧标记。用途是「同一个程序
    的第几份副本占用哪个数据目录」这类判断。

- static bool Unlock(long handle)
  - 释放 `TryLock` 拿到的锁；本来就没锁住返回
    false。进程退出时无需显式调用。

- [DllImport("crt")]static extern int chmod(string path, int mode);

- [DllImport("crt")]static extern int access(string path, int mode);

- static bool SetExecutable(string path, bool on)
  - 设置或清除文件的可执行位。POSIX 上按 owner/group/other
    的当前可读位加上（或去掉）对应的 x 位；Windows 没有可执行位
    （可执行性由扩展名决定），返回 true 且不做修改。

- static bool IsExecutable(string path)
  - 文件存在且当前用户可执行时返回 true。
    Windows 上退化为按扩展名判断（.exe/.bat/.cmd/.com）。

- static void Delete(string path)
  - 删除指定路径的文件。

- static void Move(string source, string dest)
  - 移动（重命名）文件。

- static void Copy(string source, string dest)
  - 将文件从源路径复制到目标路径。
    源文件无法打开时抛出 FileNotFoundException，
    目标无法写入时抛出 IOException。

- static int GetSize(string path)
  - 获取文件大小（字节）。
    文件无法打开时抛出 FileNotFoundException。

- static List<string> ReadAllLines(string path)
  - 将文件所有行读入 List。

- static void WriteAllLines(string path, List<string> lines)
  - 将字符串列表逐行写入文件。
    文件无法以写入方式打开时抛出 IOException。

- static void WriteBytes(string path, string data, int count)
  - 精确写入 <paramref name="count"/> 个字节，保留
    内嵌的 NUL 字节。创建或覆盖目标文件。

- static byte[]ReadBytes(string path, int offset, int count)
  - 从文件读取最多 <paramref name="count"/> 个字节。
    返回的缓冲区由 ARC 管理并自带长度，因此内嵌 NUL 字节
    得以保留，调用者也无需手动释放。文件比 count 短小时
    返回实际读取的字节数；文件无法打开时抛出
    FileNotFoundException。

- static byte[]ReadAllBytes(string path)
  - 读取整个文件为字节数组（二进制安全：内嵌 NUL
    会被保留，因此可用于图片、可执行文件等任意内容）。
    文件无法打开时抛出 FileNotFoundException。

- static void WriteAllBytes(string path, byte[]data)
  - 将原始字节写入文件（二进制安全，不同于
    接收字符串、无法携带 NUL 字节的 WriteBytes）。

- static bool ExtractEmbedded(string path, string dest)
  - 把内嵌资源原样落盘（二进制安全），返回是否落盘成功。
    没有这份内嵌资源返回 false。目标已存在且大小一致时直接成功，
    不重复写——同一份资源每次进程启动都会走到这里。
    
    用于必须以「真文件」形式存在才能用的资源：动态库要交给系统加载器
    （LoadLibrary/dlopen 只认路径），子进程可执行文件要交给 exec。

- static void AppendAllBytes(string path, byte[]data)
  - 将原始字节追加到文件末尾（二进制安全）。
    文件不存在时创建。


## FileChange (class)

`DirectoryWatcher` 上报的文件系统变化类型。

- static int Added()

- static int Removed()

- static int Modified()


## FileInfo (class)

单个文件或目录的元数据：大小、时间戳以及
只读/隐藏/目录属性。File 本身只整体读写内容，
因此“上次何时修改？”这类问题在此回答——
这也是增量构建、监听器与热重载所依赖的信息。
时间戳为 Unix 秒（文件不存在时为 0）。

- [DllImport("crt")]static extern long zan_file_time(string path, int which);

- [DllImport("crt")]static extern long zan_file_length(string path);

- [DllImport("crt")]static extern long zan_file_attributes(string path);

- [DllImport("crt")]static extern long zan_file_set_readonly(string path, int on);

- string path;

- FileInfo(string p)

- string FullName()
  - 该实例所描述的路径。

- bool Exists()

- long Length()
  - 文件大小（字节），路径不存在时为 -1。

- long LastWriteTime()

- long CreationTime()

- long LastAccessTime()

- bool IsReadOnly()

- bool IsHidden()

- bool IsDirectory()

- bool SetReadOnly(bool on)
  - 将文件设为只读或可写。无法修改时返回 false
    （文件不存在、无权限）。

- bool NewerThan(string other)
  - 当 `this` 比 `other` 新时返回 true，即 `other` 需要重新构建。
    `other` 不存在时视为过期。

- static long GetLastWriteTime(string path)

- static long GetCreationTime(string path)

- static long GetLastAccessTime(string path)

- static long GetLength(string path)

- static long GetAttributes(string path)


## FileInfoEx (class)

超出 `FileInfo` 的扩展文件元数据与操作：
读取 Windows 版本信息资源（exe/dll 的产品/文件版本）、
提取图标、按扩展名猜测 MIME、写入时间戳、
创建硬链接、检测 reparse point（junction/符号链接）。

FileInfoEx.Version("C:\\Windows\\System32\\notepad.exe"); // "11.2402.23.0"
string mime = FileInfoEx.Mime("archive.zip");            // "application/zip"
FileInfoEx.SetLastWriteTime("out.bin", unixSeconds);
FileInfoEx.CreateHardLink("link.txt", "target.txt");

除 Version()/Icon()/IsReparsePoint() 外均跨平台。

- [DllImport("version", EntryPoint="GetFileVersionInfoSizeW")]static extern int GetFileVersionInfoSizeW(nint path, nint handle);

- [DllImport("version", EntryPoint="GetFileVersionInfoW")]static extern int GetFileVersionInfoW(nint path, int handle, int len, nint data);

- [DllImport("version", EntryPoint="VerQueryValueW")]static extern int VerQueryValueW(nint data, nint subBlock, nint buffer, nint size);

- [DllImport("shell32", EntryPoint="SHGetFileInfoW")]static extern nint SHGetFileInfoW(nint path, int attrs, nint sfi, int sfiSize, int flags);

- [DllImport("user32", EntryPoint="DestroyIcon")]static extern int DestroyIconNative(nint icon);

- [DllImport("kernel32", EntryPoint="GetFileAttributesW")]static extern int GetFileAttributesW(nint path);

- [DllImport("kernel32", EntryPoint="CreateHardLinkW")]static extern int CreateHardLinkW(nint link, nint target, nint secAttrs);

- [DllImport("crt")]static extern long zan_file_set_time(string path, int which, long unixSec);

- static int ReparsePointAttr()

- static int ShgfiIcon()

- static int ShgfiSmallIcon()

- static int ShgfiUseFileAttributes()

- static string Version(string path)
  - 读取 Windows exe/dll 的版本信息资源，返回
    “major.minor.build.revision”格式（取 VS_FIXEDFILEINFO 中产品/文件版本
    较新者）。文件无版本资源或平台非 Windows 时返回
    “”。

- static string JoinVersion(int ms, int ls)

- static nint Icon(string path)
  - 提取 `path` 的小型文件类型图标（exe 自带图标，或文件不存在时
    系统按扩展名提供的图标），返回
    其 HICON 句柄。调用方必须用 `DestroyIcon` 释放。
    提取失败或平台非 Windows 时返回 0。

- static void DestroyIcon(nint icon)
  - 释放从 `Icon` 获取的图标。
    跨平台；非 Windows 上为空操作。

- static string Mime(string path)
  - 根据文件扩展名（转小写）猜测 MIME 类型。
    未知扩展名回退为“application/octet-stream”。

- static string Extension(string path)
  - `path` 的扩展名（含点、转小写），没有时返回“”
    （结尾是点或根本没有点）。

- static int LastIndexOfAny(string s, string chars)

- static long GetLastWriteTime(string path)
  - 最后写入的 Unix 时间戳（秒），路径不存在时返回 0。
    另见 `FileInfo.LastWriteTime`。

- static long GetCreationTime(string path)

- static long GetLastAccessTime(string path)

- static bool SetLastWriteTime(string path, long unixSec)
  - 将最后写入时间设置为 Unix 时间戳。
    文件不存在或平台无法设置时返回 false。

- static bool SetLastAccessTime(string path, long unixSec)
  - 将最后访问时间设置为 Unix 时间戳。

- static bool SetCreationTime(string path, long unixSec)
  - 设置创建时间（仅 Windows；其他平台返回 false）。

- static bool CreateHardLink(string link, string target)
  - 在 `link` 创建指向已有文件 `target` 的硬链接。
    链接创建失败时返回 false（目标不存在、
    跨卷、权限不足）。

- [DllImport("crt")]static extern int link(string existing, string newLink);

- static int LinkPosix(string link, string target)

- static bool IsReparsePoint(string path)
  - 当 `path` 是 reparse point（目录 junction、符号
    链接或 OneDrive 占位文件）时返回 true。仅 Windows；其他平台返回 false。


## FileSnapshot (class)

- string name;

- long lastWriteTime;

- long size;

- FileSnapshot(string name, long lastWriteTime, long size)


## FileStream (class)

基于磁盘文件的 `Stream`。偏移量为 64 位，
因此大于 2 GB 的文件也能在所有平台正确 seek。

FileStream fs = FileStream.Open("out.bin", "w+b");
fs.Write("header");
fs.Close();

通过 `OpenRead`（只读）、`OpenWrite`（创建或
截断，亦可读）、`Append`
或 `Open`（显式 stdio 模式字符串）打开。打开失败
会抛异常，因此返回的 FileStream 必定可用。

- [DllImport("crt")]static extern long zan_file_open(string path, string mode);

- [DllImport("crt")]static extern long zan_file_read(long handle, nint buf, long count);

- [DllImport("crt")]static extern long zan_file_write(long handle, nint buf, long count);

- [DllImport("crt")]static extern long zan_file_seek(long handle, long offset, int origin);

- [DllImport("crt")]static extern long zan_file_tell(long handle);

- [DllImport("crt")]static extern long zan_file_flush(long handle);

- [DllImport("crt")]static extern long zan_file_close(long handle);

- [DllImport("crt")]static extern long zan_file_eof(long handle);

- long handle;

- string path;

- bool readable;

- bool writable;

- FileStream()

- static FileStream Open(string path, string mode)
  - 以 stdio 模式字符串（“rb”、“wb”、“r+b”、
    “ab”）打开 `path`。打开失败时抛出 FileNotFoundException/IOException。

- static FileStream OpenRead(string path)
  - 以只读方式打开已有文件。

- static FileStream OpenWrite(string path)
  - 创建或截断文件用于写入（也可读）。使用
    模式“w+b”的 `Open`——已存在的文件会被
    清空，因此写短内容不会残留旧数据。

- static FileStream Append(string path)
  - 以追加方式打开文件，不存在时创建。

- string Path()
  - 该流打开时的路径。

- bool IsClosed()
  - 调用过 `Close` 后为 true。

- override bool CanRead()

- override bool CanWrite()

- override bool CanSeek()

- override long Length()

- override long Position()

- override long Seek(long offset, int origin)

- override int ReadInto(nint buf, int count)

- override int WriteFrom(nint buf, int count)

- bool EndOfFile()
  - 读取到达文件末尾后为 true。

- override void Flush()

- override void Close()


## IniEntry (class)

一对 INI 键/值。

- public string key;

- public string text;


## IniFile (class)

INI 文件读写器。节为 <c>[Name]</c>，条目为
<c>key=value</c>，<c>;</c> 和 <c>#</c> 开头为注释，空行
被忽略。键和值会去除首尾空白；值中可包含 <c>=</c>。
可从文本（内存中）或文件解析，也可保存回两者。

IniFile ini = IniFile.Load("app.ini");
int port = ini.GetInt("server", "port", 8080);
ini.SetString("server", "name", "dev");
ini.Save("app.ini");

- List<IniSection> sections=new List<IniSection>();

- static IniFile Parse(string text)
  - 将给定文本解析为内存中的 INI。

- static IniFile Load(string path)
  - 读取并解析 `path` 处的文件。文件不存在时返回
    空的 INI。

- string GetString(string section, string key, string fallback)
  - 在节中查找键。节或键不存在时返回
    `fallback`。

- int GetInt(string section, string key, int fallback)
  - 数值查找；缺失或非数值时返回 `fallback`。

- void SetString(string section, string key, string val)
  - 设置字符串值，必要时创建节/键。
    新节追加在末尾；新键追加在所属
    节内。已有键保持原有位置。

- void SetInt(string section, string key, int val)
  - 设置整数值（以十进制文本存储）。

- bool RemoveKey(string section, string key)
  - 删除键。键存在时返回 true。

- bool RemoveSection(string section)
  - 删除整个节（含其所有键）。节存在时返回 true。

- List<string> EnumSections()
  - 所有节的名称（按文件顺序）。

- List<string> EnumKeys(string section)
  - 节的键列表（按文件顺序）；节不存在时为空。

- string ToText()
  - 将 INI 序列化为文本。

- void Save(string path)
  - 将 INI 写回 `path`。

- static IniEntry Find(IniFile ini, string section, string key)

- static IniSection FindSection(IniFile ini, string name)

- static IniSection Section(IniFile ini, string name)

- static List<string> SplitLines(string text)

- static int IndexOf(string hay, string needle, int from)

- static int ParseInt(string s)


## IniSection (class)

一个 INI 节：名称加有序的键/值条目。

- public string name;

- public List<IniEntry> entries=new List<IniEntry>();


## KnownFolders (class)

常见的用户/系统文件夹，以及环境变量访问。

文件夹查找通过环境变量解析（Windows：%USERPROFILE%、
%APPDATA% 等；POSIX：$HOME、$XDG_CONFIG_HOME 等），因此
同一份代码在所有平台可用，并遵循操作系统设置中
对单个用户的目录重定向。无法解析时返回“”。

string cfg = KnownFolders.AppData() + Path.Separator() + "myapp";
string home = KnownFolders.Home();

- static string Home()
  - 用户主目录（Windows：%USERPROFILE%；POSIX：$HOME）。

- static string Desktop()
  - 用户桌面文件夹。

- static string Documents()
  - 用户文档文件夹。

- static string Downloads()
  - 用户下载文件夹。

- static string AppData()
  - 按用户漫游的应用数据（Windows：%APPDATA%；
    POSIX：$XDG_CONFIG_HOME 或 ~/.config）。

- static string LocalAppData()
  - 按用户的本地（非漫游）应用数据
    （Windows：%LOCALAPPDATA%；POSIX：$XDG_DATA_HOME 或 ~/.local/share）。

- static string Temp()
  - 系统临时目录（Windows：%TEMP%/%TMP%；
    POSIX：$TMPDIR 或 /tmp）。

- static string UserName()
  - 当前用户名（Windows：%USERNAME%；POSIX：$USER 或 $LOGNAME）。

- static string ComputerName()
  - 计算机名（Windows：%COMPUTERNAME%；POSIX：$HOSTNAME）。

- static string GetEnv(string name)
  - 读取环境变量；未设置时返回“”。

- static void SetEnv(string name, string val)
  - 为当前进程（及其子进程）设置环境变量。
    空值表示删除该变量（Windows 的 SetEnvironmentVariableW
    对空串指针是设成空串，真正删除必须传 NULL；POSIX
    用 unsetenv，两平台行为一致）。

- static string Expand(string text)
  - 从进程环境展开 <c>%NAME%</c>（Windows）或 <c>$NAME</c>
    （POSIX）标记。未知标记
    原样保留。Windows 下与 cmd.exe 语义一致（ExpandEnvironmentStringsW）。

- static int IndexOf(string hay, string needle, int from)

- [DllImport("crt", EntryPoint="setenv")]static extern int PosixSetenv(string name, string value, int overwrite);

- [DllImport("crt", EntryPoint="unsetenv")]static extern int PosixUnsetenv(string name);


## MemoryMappedFile (class)

内存映射文件与具名共享内存区域。一个映射是一块具名
字节区域（或对已有文件的视图），可映射到进程
地址空间并通过 Span 读写，由操作系统负责
内容的换入换出（file-backed 或共享内核对象），
（具名区域——打开同名区域的其他进程可见）。

// 写入进程：
MemoryMappedFile mmf = MemoryMappedFile.CreateNew("MyApp.Shared", 4096);
nint view = mmf.Map(4096);
new Span<int>(view, 1024)[0] = 42;
mmf.Unmap(view, 4096);
mmf.Close();

// 读取进程：
MemoryMappedFile mmf2 = MemoryMappedFile.OpenExisting("MyApp.Shared", 4096);
nint view2 = mmf2.Map(4096);
int value = new Span<int>(view2, 1024)[0];

名称按机器区分；Windows 上以“Global\”前缀开头可使区域
对其他会话可见。文件型映射为读写共享（MAP_SHARED），
因此并发读取同一文件的读者能看到写入。

- [DllImport("crt")]static extern long zan_mmap_create(string name, long size);

- [DllImport("crt")]static extern long zan_mmap_open(string name, long size);

- [DllImport("crt")]static extern long zan_mmap_from_file(string path, long size);

- [DllImport("crt")]static extern long zan_mmap_map(long handle, long size);

- [DllImport("crt")]static extern long zan_mmap_unmap(long ptr, long size);

- [DllImport("crt")]static extern long zan_mmap_flush(long ptr, long size);

- [DllImport("crt")]static extern long zan_mmap_close(long handle);

- [DllImport("crt")]static extern long zan_mmap_unlink(string name);

- long handle;

- string name;

- static MemoryMappedFile CreateNew(string name, long size)
  - 创建一块大小为 `size` 字节的全新具名共享内存区域。
    同名区域已存在时抛出 IOException。
    区域会一直保留，直到创建者关闭（POSIX 上只有
    创建者关闭才释放名称；其他客户端关闭不影响它）。

- static MemoryMappedFile OpenExisting(string name, long size)
  - 打开已存在的具名共享内存区域。
    区域不存在时抛出 FileNotFoundException。

- static MemoryMappedFile CreateFromFile(string path, long size)
  - 对已有文件创建映射。`size` <= 0 时映射
    整个文件；否则文件会被扩展/截断为 `size`
    字节。映射为读写且共享。

- nint Map(long size)
  - 将 `size` 字节映射进地址空间，返回视图
    地址。用完需按同样大小 Unmap。失败时返回 0。

- bool Unmap(nint view, long size)
  - 取消映射视图。`size` 必须与映射时一致。
    视图无法取消映射时返回 false。

- bool Flush(nint view, long size)
  - 将映射视图刷写回磁盘（文件型映射）。

- void Close()
  - 关闭映射句柄。由此映射出的视图必须先
    取消映射。POSIX 上只有创建者关闭才释放名称；
    打开者关闭只释放自己的句柄。

- static bool Unlink(string name)
  - 显式移除具名区域，即使还有打开的句柄。
    POSIX 上立即释放名称；Windows 上具名对象是引用计数的
    内核对象，名称随最后一个句柄关闭而消失，此调用为空操作。
    返回是否成功移除。


## MemoryStream (class)

基于可增长的堆外缓冲区的 `Stream`——测试中
用作 FileStream 的内存替身，也是用流式代码构造
字节载荷、再交给 socket 或文件的方式：
MemoryStream ms = MemoryStream.Alloc(64);
ms.Write("hello");
string all = ms.ToStr();
ms.Close();

读写共用同一个游标（与 System.IO.MemoryStream 相同）：写入
越过末尾会扩展缓冲区，Length 记录最高水位。
`Close` 释放缓冲区。

- nint data;

- int capacity;

- int length;

- int pos;

- MemoryStream()

- static MemoryStream Alloc(int initialCapacity)
  - 给定初始容量的空可写流。

- static MemoryStream FromStr(string s)
  - 基于 `s` 字节副本的流，位置为 0
    （二进制安全：内嵌 NUL 字节会保留）。

- static MemoryStream FromRaw(nint src, int count)
  - 基于原始地址处 `count` 字节副本的流。

- string ToStr()
  - 目前已写入的字节，以二进制安全字符串返回。

- nint Raw()
  - 底层缓冲区地址，用于零拷贝 FFI。写入导致缓冲区增长后
    以及 Close 之后失效。

- void Grow(int needed)

- override bool CanRead()

- override bool CanWrite()

- override bool CanSeek()

- override long Length()

- override long Position()

- override long Seek(long offset, int origin)

- void Clear()
  - 清空内容并回到开头；保留缓冲区。

- override int ReadInto(nint buf, int count)

- override int WriteFrom(nint buf, int count)

- override void Close()


## Path (class)

路径操作工具。纯字符串处理：唯一剩下的平台调用
是向操作系统询问临时目录位置的那一个。

- [DllImport("kernel32", EntryPoint="GetTempPathA")]static extern int WinGetTempPath(int bufLen, string buf);

- [DllImport("crt")]static extern string getenv(string name);

- static int AfterLastSeparator(string path)
  - 最后一个分隔符之后的索引，没有分隔符时为 0。

- static string GetFileName(string path)
  - 从路径中返回文件名（含扩展名）。

- static string GetExtension(string path)
  - 返回扩展名（含点）。

- static string Separator()
  - 平台目录分隔符。

- static string Combine(string path1, string path2)
  - 用平台分隔符拼接两个路径字符串。

- static string GetDirectoryName(string path)
  - 返回路径的目录部分。

- static string GetFileNameWithoutExtension(string path)
  - 返回不含扩展名的文件名。

- static string ChangeExtension(string path, string extension)
  - 更改路径字符串的扩展名。

- static bool HasExtension(string path)
  - 检查路径是否带扩展名。

- static string GetTempPath()
  - 获取系统临时目录路径。

- static string Normalize(string path)
  - 将路径分隔符规范化为平台分隔符。


## PathEx (class)

基础之外的路径辅助功能：glob 匹配（<c>*</c>、<c>?</c>、
<c>**</c>）、相对路径解析，以及
<c>.</c>/<c>..</c> 段的规范化清理。纯字符串逻辑，与平台无关。

bool hit = PathEx.IsMatch("src/**/*.zan", "src/a/b/c.zan");   // true
string rel = PathEx.GetRelativePath("C:\\a\\b", "C:\\a\\b\\c\\d.txt");

- static bool IsMatch(string pattern, string path)
  - 当 `path` 匹配 glob 模式 `pattern` 时为真。两者都接受
    `/` 或 `\` 分隔符。<c>*</c> 匹配单个路径段内，
    <c>?</c> 匹配一个字符，<c>**</c> 匹配零个或多个路径段。

- static bool IsUnder(string dir, string path)
  - 当 `path` 是 `dir` 的目录前缀（或等于 `dir`）时为真，
    在 Windows 上按段不区分大小写比较。

- static string Clean(string path)
  - 移除 <c>.</c> 并解析 <c>..</c> 段
    （"a/./b/../c" -> "a/c"）。无法解析的前导 <c>..</c> 会
    保留。分隔符按输入原样保留。

- static string GetRelativePath(string from, string to)
  - 从 `from`（目录）到 `to`（其下或其旁的路径）的相对路径
    两者按段拆分；公共前缀
    被去掉，剩余部分用 "../" 跳转连接。
    Windows 盘符不区分大小写比较。

- static bool MatchSegments(List<string> pat, int pi, List<string> pth, int ti)

- static bool SegmentMatch(string pattern, string segment)

- static bool SegMatch(string pat, int pi, string seg, int si)

- static List<string> SplitSegments(string path)

- static bool Same(string a, string b)

- static bool CharEqual(string a, string b)

- static int IndexOf(string hay, string needle, int from)


## PathFilter (class)

目录遍历的过滤条件：保留哪些后缀、隐藏哪些后缀、跳过哪些子目录。
后缀表和目录表都是空格分隔的字符串，因此调用点一行写完：

PathFilter src = PathFilter.Only(".zan .zform .zscene")
.Skip("publish build bin obj dist tools");

- string exts;
  - 只保留这些后缀（"" 表示全部文件）。

- string hideExts;
  - 即使命中 exts 也要隐藏的后缀（如资源管理器里的 ".proj"）。

- string skipDirs;
  - 不进入的子目录名。

- bool skipDotDirs;
  - 跳过以 "." 开头的目录 / 文件。

- bool skipDotFiles;

- PathFilter()

- static PathFilter Any()
  - 全部文件。

- static PathFilter Only(string suffixes)
  - 只要这些后缀的文件。

- PathFilter Hide(string suffixes)

- PathFilter Skip(string dirs)

- PathFilter Dots(bool dirs, bool files)
  - 是否跳过点开头的目录 / 文件（默认跳目录、留文件）。

- bool KeepFile(string name)

- bool EnterDir(string name)

- static bool Dotted(string name)

- static bool AnySuffix(string list, string name)
  - 空格分隔的表里有一项是 `name` 的结尾时为 true。

- static bool AnyWord(string list, string name)
  - 空格分隔的表里有一项等于 `name` 时为 true。

- static List<string> Words(string list)


## Shortcut (class)

创建和解析桌面快捷方式：目标路径、参数、工作目录、
说明和图标。Windows 上是 IShellLink COM 接口写的 .lnk；
Linux 上是 freedesktop 的 `[Desktop Entry]` 文件（.desktop），
字段一一对应：Exec=目标+参数、Path=工作目录、Icon=图标、
Comment=说明。macOS 没有等价的用户级快捷方式格式，会抛出
PlatformNotSupportedException 而不是静默写一个不能用的文件。

Shortcut.Create("C:\\Users\\me\\Desktop\\app.lnk",
"C:\\app\\app.exe", "--fast", "C:\\app",
"C:\\app\\app.exe", 0);
ShortcutInfo info = Shortcut.Read("app.lnk");
string t = info.target;

- static string ClsidShellLink()

- static string IidShellLinkW()

- static string IidPersistFile()

- static int SlotGetPath()

- static int SlotGetDescription()

- static int SlotSetDescription()

- static int SlotGetWorkingDir()

- static int SlotSetWorkingDir()

- static int SlotGetArguments()

- static int SlotSetArguments()

- static int SlotGetIconLocation()

- static int SlotSetIconLocation()

- static int SlotSetPath()

- static int SlotLoad()

- static int SlotSave()

- static bool comReady=false;

- static void InitCom()

- static bool Create(string lnkPath, string target, string arguments, string workingDir, string iconPath, int iconIndex, string description)
  - 在 `lnkPath` 处创建（或覆盖）一个指向
    `target` 的 .lnk。可选：参数、工作目录、图标路径+
    图标索引、说明。成功返回 true。

- static bool WriteDesktopEntry(string path, string target, string arguments, string workingDir, string iconPath, string description)

- static string EntryName(string path)

- static string QuoteArg(string s)

- static string ExecTarget(string exec)

- static string ExecArguments(string exec)

- static string EntryValue(string text, string key)

- static ShortcutInfo Read(string lnkPath)
  - 解析 .lnk 文件。快捷方式未定义的字段
    返回空值（iconIndex 为 0）。

- static string GetPath(nint shell)

- static string GetText(nint shell, int slot)

- static string GetIconPath(nint shell)

- static int GetIconIndex(nint shell)


## ShortcutInfo (class)

由
`Shortcut.Read` 返回的结构化 .lnk 内容。

- public string target;

- public string arguments;

- public string workingDir;

- public string description;

- public string iconPath;

- public int iconIndex;

- ShortcutInfo()


## Stream (class)

面向字节的流：`FileStream` 和
`MemoryStream` 的公共基类，也是 `StreamReader` /
`StreamWriter` 读写所经由的对象。

子类只需覆写四个原语方法（ReadInto / WriteFrom / Seek /
Close）以及能力与长度查询；其余所有方法都基于它们实现，
因此新流类型可免费获得整套辅助方法；
无需额外编码：
FileStream fs = FileStream.OpenRead("data.bin");
string head = fs.Read(16);
fs.Close();

字节数据以 `string` 形式传递：zan 字符串带有显式长度，
因此内嵌的 NUL 字节也能保留。裸地址（nint，来自 NativeMemory 或
ByteBuffer.Raw）同样被接受，用于零拷贝路径。

- virtual bool CanRead()
  - 流可读时为真。

- virtual bool CanWrite()
  - 流可写时为真。

- virtual bool CanSeek()
  - 流支持 Seek/Position 时为真。

- virtual long Length()
  - 流中的总字节数，未知时为 -1。

- virtual long Position()
  - 当前字节偏移，未知时为 -1。

- virtual long Seek(long offset, int origin)
  - 移动游标。`origin`：0 = 开头，1 = 当前，2 = 末尾。
    返回新的绝对位置，不可定位时返回 -1。

- virtual int ReadInto(nint buf, int count)
  - 读取最多 `count` 字节到裸地址 `buf`。
    返回读取的字节数；0 表示流结束。

- virtual int WriteFrom(nint buf, int count)
  - 从裸地址 `buf` 写入 `count` 字节。
    返回写入的字节数。

- virtual void Flush()
  - 将缓冲的写入推送到底层设备。

- virtual void Close()
  - 释放流。可安全地重复调用。

- string Read(int count)
  - 读取最多 `count` 字节；流结束时返回 ""。

- int Write(string data)
  - 写入 `data` 的全部字节。返回实际写入的字节数。

- int ReadByte()
  - 读取一个字节，流结束时返回 -1。

- void WriteByte(int b)
  - 写入一个字节（`b` 的低 8 位）。

- string ReadToEnd()
  - 读取流中剩余的全部内容（二进制安全）。

- long CopyTo(Stream dest)
  - 把本流中剩余的内容复制到 `dest`。
    返回复制的字节数。

- long SeekTo(long position)
  - 定位到绝对偏移（Seek(pos, 0) 的简写）。

- long Remaining()
  - 游标到末尾之间的字节数，未知时为 -1。


## StreamReader (class)

基于 `Stream` 的缓冲文本读取：逐行读取，
避免像 File.ReadAllText 那样把整个文件读入内存，
（这正是 File.ReadAllText 目前的做法）。

StreamReader r = StreamReader.Open("log.txt");
string line = r.ReadLine();
while (line != null) {
Console.WriteLine(line);
line = r.ReadLine();
}
r.Close();

`ReadLine` 在输入结束时返回 null，并去掉行
终止符（"\n" 和 "\r\n" 都适用）。字节按原样传递（UTF-8
输入仍保持 UTF-8）。若此 reader 打开了底层流，Close 会将其关闭；
若 reader 包装的是调用方传入的流，则不会动它。

- Stream stream;

- bool owns;

- string buf;

- int pos;

- bool eof;

- int chunk;

- StreamReader()

- static StreamReader Wrap(Stream s)
  - 从现有流读取文本（不由本 reader
    关闭）。

- static StreamReader Open(string path)
  - 打开文件用于读取。若文件
    不存在则抛出 FileNotFoundException。

- static StreamReader FromStr(string text)
  - 从字符串读取文本，用于测试解析器。

- Stream BaseStream()
  - 正在读取的流。

- bool EndOfStream()
  - 缓冲区为空且流已耗尽时为真。

- void Fill()
  - 缓冲区耗尽时再拉取一块数据。

- string ReadChar()
  - 读取一个字节作为单字符字符串，结束时返回 ""。

- string ReadBlock(int count)
  - 读取最多 `count` 字节；输入结束时返回 ""。

- string ReadLine()
  - 读取下一行（不含终止符），或返回 null（输入
    结束）。

- List<string> ReadAllLines()
  - 读取剩余的所有行。

- string ReadToEnd()
  - 读取剩余全部内容（含缓冲区）。

- void Close()
  - 关闭 reader；若该 reader 打开了底层流，也一并关闭
    该流。


## StreamWriter (class)

基于 `Stream` 的缓冲文本写入：可向文件追加行，
而不必像 File.AppendAllText 那样重写整个文件（这正是它目前的代价）。

StreamWriter w = new StreamWriter("log.txt");
w.WriteLine("started");
w.Close();

写入内容先在内存中累积，当缓冲区填满、
调用 `Flush` 或 `Close` 时才真正写入流——因此必须
关闭（或刷新）writer 后输出才会真正存在。若本 writer 打开了底层
流，则 Close 会将其一并关闭。

- Stream stream;

- bool owns;

- StringBuilder buf;

- int buffered;

- int limit;

- string newline;

- StreamWriter()

- static StreamWriter Wrap(Stream s)
  - 写入现有流（不由本 writer 关闭）。

- StreamWriter(string path):this()
  - 创建或截断文件用于写入。

- static StreamWriter Append(string path)
  - 打开文件用于追加，不存在则创建。

- Stream BaseStream()
  - 正在写入的流。

- StreamWriter SetNewLine(string nl)
  - 设置 WriteLine 追加的行终止符（默认
    为 "\n"）。

- StreamWriter Write(string text)
  - 追加文本。

- StreamWriter WriteLine(string text)
  - 追加文本，并附带行终止符。

- StreamWriter WriteEmptyLine()
  - 仅追加一个行终止符。

- void Flush()
  - 将所有缓冲内容交给流。

- void Close()
  - 刷新后，若本 writer 打开了底层
    流，则关闭该流。


## int (delegate)

`delegate int SetEnvironmentVariableWFn(nint name, nint val);`


## void (delegate)

`DirectoryWatcher.Watch` 的变化回调：
条目名加一个 `FileChange` 类型。

`delegate void FileChangeFn(string name, int change);`
