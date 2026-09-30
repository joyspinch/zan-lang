# System.IO

> 源码: `stdlib/System/IO/ByteBuffer.zan`, `stdlib/System/IO/Directory.zan`, `stdlib/System/IO/DirectoryTree.zan`, `stdlib/System/IO/File.zan`, `stdlib/System/IO/FileAccess.zan`, `stdlib/System/IO/FileInfo.zan`, `stdlib/System/IO/FileInfoEx.zan`, `stdlib/System/IO/FileMode.zan`, `stdlib/System/IO/FileStream.zan`, `stdlib/System/IO/IniFile.zan`, `stdlib/System/IO/KnownFolders.zan`, `stdlib/System/IO/MemoryMappedFile.zan`, `stdlib/System/IO/MemoryStream.zan`, `stdlib/System/IO/Path.zan`, `stdlib/System/IO/PathEx.zan`, `stdlib/System/IO/SeekOrigin.zan`, `stdlib/System/IO/Shortcut.zan`, `stdlib/System/IO/Stream.zan`, `stdlib/System/IO/StreamReader.zan`, `stdlib/System/IO/StreamWriter.zan`


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
  - 构造零容量缓冲区；实例经 Alloc/FromRaw/FromStr 创建。

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
  - 确保还能再写 extra 字节（必要时扩容）。

- ByteBuffer WriteU8(int v)
  - 追加 1 个字节。

- ByteBuffer WriteU16(int v)
  - 追加小端 16 位。

- ByteBuffer WriteU32(long v)
  - 追加小端 32 位。

- ByteBuffer WriteU64(long v)
  - 追加小端 64 位。

- ByteBuffer WriteVarInt(long v)
  - 无符号 LEB128 变长整数（0-127 占 1 字节）。

- ByteBuffer WriteBytes(nint src, int count)
  - 从原始块或另一个缓冲区的 Raw() 复制 count 字节。

- ByteBuffer WriteRaw(string src, int count)
  - 追加 <paramref name="src"/> 开头的 count 个字节，不写
    长度前缀。长度由调用方给出，因此二进制安全：字符串本身
    以 NUL 结尾，<c>s.Length</c> 只数到第一个 NUL，而网络收到的
    字节（HTTP 二进制请求体、上传内容）本就可能含 NUL。

- ByteBuffer WriteBytes(byte[]src, int offset, int count)
  - 追加字节数组切片，不写长度前缀。零额外中间拷贝，完全二进制安全。

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
  - 读取 1 个字节（0-255）；越界抛 InvalidOperationException。

- int ReadU16()
  - 读取小端 16 位；越界抛 InvalidOperationException。

- long ReadU32()
  - 读取小端 32 位（以 long 返回，无符号）；越界抛 InvalidOperationException。

- long ReadU64()
  - 读取小端 64 位；越界抛 InvalidOperationException。

- long ReadVarInt()
  - 读取 LEB128 变长整数；数据截断或超过 64 位时抛
    InvalidOperationException。

- int PeekU8(int pos)
  - 显式位置读取一个字节（不移动读游标）；越界返回 -1。
    供裸解析循环按位置消费，免除 SeekRead 往返。

- long ReadVarIntAt(int pos, out int np)
  - 显式位置读取 LEB128 变长整数：解析自 pos 起，
    结束后的位置写入 np（返回值即数值）。越界/截断返回 -1。
    与 `ReadVarInt` 同格式，但完全不碰读游标。

- int CmpRangeRaw(int off, int len, string other)
  - 缓冲内 [off, off+len) 与 other 的序数比较：memcmp
    前缀、长度定序（字节串语义，与 String.CompareOrdinal 同序）。
    零分配；越界区间被钳制。

- nint ReadBytes(int count)
  - 将 count 字节读入新分配的原始块
    （调用方用 NativeMemory.Free 释放，或包装进 ByteBuffer）。

- string ReadString()
  - 读取由 WriteString 写入的长度前缀字符串。

- long Crc32(int count)
  - 对前 count 字节计算 CRC32（IEEE，反射）。

- long Crc32C(int count)
  - 对前 count 字节计算 CRC32C（Castagnoli，poly 0x82F63B78）。
    优先走 SSE4.2/ARMv8 CRC 指令内核，无硬件回退逐字节内建。


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

- [DllImport("kernel32", EntryPoint="GetFileAttributesW")]static extern int WinGetFileAttributes(nint path);

- [DllImport("kernel32", EntryPoint="CreateDirectoryW")]static extern int WinCreateDirectory(nint path, nint secAttrs);

- [DllImport("kernel32", EntryPoint="RemoveDirectoryW")]static extern int WinRemoveDirectory(nint path);

- [DllImport("kernel32", EntryPoint="GetCurrentDirectoryW")]static extern int WinGetCurrentDirectory(int bufLen, nint buf);

- [DllImport("kernel32", EntryPoint="SetCurrentDirectoryW")]static extern int WinSetCurrentDirectory(nint path);

- [DllImport("kernel32", EntryPoint="FindFirstFileW")]static extern nint WinFindFirstFile(nint pattern, nint findData);

- [DllImport("kernel32", EntryPoint="FindNextFileW")]static extern int WinFindNextFile(nint handle, nint findData);

- [DllImport("kernel32", EntryPoint="FindClose")]static extern int WinFindClose(nint handle);

- [DllImport("kernel32", EntryPoint="GetLogicalDrives")]static extern int WinGetLogicalDrives();

- [DllImport("kernel32", EntryPoint="MultiByteToWideChar")]static extern int WinToWide(int codePage, int flags, nint source, int sourceLength, nint output, int outputLength);

- [DllImport("kernel32", EntryPoint="WideCharToMultiByte")]static extern int WinToUtf8(int codePage, int flags, nint source, int sourceLength, nint output, int outputLength, nint defaultChar, nint usedDefault);

- [DllImport("crt", EntryPoint="opendir")]static extern nint opendir(string name);

- [DllImport("crt", EntryPoint="readdir")]static extern string readdir(nint dirp);

- [DllImport("crt", EntryPoint="closedir")]static extern int closedir(nint dirp);

- [DllImport("crt", EntryPoint="mkdir")]static extern int mkdir(string path, int mode);

- [DllImport("crt", EntryPoint="rmdir")]static extern int rmdir(string path);

- [DllImport("crt", EntryPoint="chdir")]static extern int chdir(string path);

- [DllImport("crt", EntryPoint="getcwd")]static extern string getcwd(string buf, int size);

- static nint winWide(string text)
  - UTF-8 字符串转 UTF-16 缓冲区（供 W 系列 API）；text 为 null 时返回 0。

- static string winUtf8(nint wide)
  - 读取以 NUL 结尾的 UTF-16 缓冲区为 UTF-8 字符串；wide 为 0 时返回 ""。

- static int winAttributesUtf8(string path)
  - 按 UTF-8 路径取 Win32 文件属性；路径转换失败时返回 -1
    （属性本身失败时是 GetFileAttributesW 的 0xFFFFFFFF）。

- static bool ExistsUtf8(string path)
  - 检查 UTF-8 路径是否存在（不使用 ANSI Windows API）。

- static bool FileExistsUtf8(string path)
  - 检查 UTF-8 路径是否指向普通文件。

- static void CreateDirectoryUtf8(string path)
  - 创建路径以 UTF-8 编码的目录。

- static List<string> GetRootsUtf8()
  - 文件系统根列表：Windows 为每个存在的盘符（"C:/" 形式），POSIX 为 "/"。

- static bool Exists(string path)
  - 检查目录是否存在。工作目录下找不到时，与 GetFiles/
    GetDirectories 一样回退到程序随附的那一份（单文件包的解包目录、
    可执行文件所在目录），否则发布后的程序会在枚举之前就判定
    views/、wwwroot/ 不存在。

- static bool ExistsOnDisk(string path)
  - 目录是否真实存在于磁盘（不走随附资源与内嵌副本回退）。

- static void CreateDirectory(string path)
  - 在指定路径创建目录。

- static void Delete(string path)
  - 删除空目录。

- static string GetCurrentDirectory()
  - 获取当前工作目录。

- static void SetCurrentDirectory(string path)
  - 设置当前工作目录。

- static string winName(nint findData)
  - 从 WIN32_FIND_DATAW 缓冲区读取 cFileName（UTF-16，位于字节偏移 +44）。

- static List<string> winList(string path, bool wantDirs, bool includeHidden)
  - FindFirstFile/FindNextFile 枚举一层：wantDirs 决定收文件还是目录，
    includeHidden=false 时跳过隐藏属性项。返回条目名（不含路径）。

- static int direntTypeOffset()
  - struct dirent 的 d_type 字节偏移（wasi-libc 布局）。

- static int direntNameOffset()
  - struct dirent 的 d_name 字节偏移（wasi-libc 布局）。

- static int direntDirType()
  - wasi-libc 的 d_type 字节沿用 WASI filetype：DT_DIR 判据改为 3。

- static int direntTypeOffset()
  - struct dirent 的 d_type 字节偏移（Darwin 布局）。

- static int direntNameOffset()
  - struct dirent 的 d_name 字节偏移（Darwin 布局）。

- static int direntDirType()
  - POSIX DT_DIR。

- static int direntTypeOffset()
  - struct dirent 的 d_type 字节偏移（glibc/Linux 布局）。

- static int direntNameOffset()
  - struct dirent 的 d_name 字节偏移（glibc/Linux 布局）。

- static int direntDirType()
  - POSIX DT_DIR。

- static string posixName(string ent)
  - 从 struct dirent* 读取以 NUL 结尾的 d_name。

- static List<string> posixList(string path, bool wantDirs, bool includeHidden)
  - opendir/readdir 枚举一层：wantDirs 决定收文件还是目录，
    includeHidden=false 时跳过 "." 开头的条目。d_type 未知或为
    符号链接时用 opendir 探测真身，返回条目名（不含路径）。

- static List<string> ToPaths(List<string> names, string baseDir)
  - 把枚举出的裸名字拼成完整路径（与 C# GetFiles 的返回
    形态一致）。拼接基准用磁盘枚举时 ReadPath 解析到的目录：随附
    资源回退后路径才真实存在。

- static List<string> GetPaths(string path)
  - 列出目录中的文件（不递归、含隐藏），返回完整路径——
    与 C# Directory.GetFiles 的语义对齐（审计 D22：本库 GetFiles
    返回裸文件名，是 C# 移植陷阱，但几十处既有调用都依赖裸名，
    不能直接改签名）。找不到磁盘目录时回退到随附资源并合并内嵌
    子项（内嵌项只有原始 path 可拼，基准取原始 path）。

- static List<string> GetDirectoryPaths(string path)
  - 列出目录中的子目录（不递归、含隐藏），返回完整路径。
    语义同 `GetPaths`：磁盘项拼 ReadPath 解析目录，
    内嵌项拼原始 path。

- static List<string> GetFiles(string path)
  - 列出目录中的文件（不递归、含隐藏）。找不到磁盘目录时
    回退到随附资源并合并内嵌子项。注意：返回裸文件名（不含路径），
    C# 形态的完整路径用 `GetPaths`。

- static List<string> GetDirectories(string path)
  - 列出目录中的子目录（不递归、含隐藏）。回退与合并
    语义同 `GetFiles`。注意：返回裸目录名（不含路径），
    C# 形态的完整路径用 `GetDirectoryPaths`。

- static List<string> GetFilesFiltered(string path, bool includeHidden)
  - GetFiles 的可控版本：includeHidden=false 时
    （Windows 属性位 / POSIX 点前缀）跳过隐藏项。

- static List<string> GetDirectoriesFiltered(string path, bool includeHidden)
  - GetDirectories 的可控版本：includeHidden=false 时
    （Windows 属性位 / POSIX 点前缀）跳过隐藏项。

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
  - 递归收集匹配文件的完整路径。

- static void FlatInto(List<string> outp, string dirPath, PathFilter f)
  - 单层文件的完整路径（不递归）。

- static List<string> Flat(string dirPath, PathFilter f)
  - 单层匹配文件的完整路径（不递归）。

- static List<string> Names(string dirPath, PathFilter f)
  - 单层文件名（不递归），按过滤条件筛选。

- static List<string> SubDirs(string dirPath, PathFilter f)
  - 可进入的子目录完整路径（不递归）。

- static List<string> SubNames(string dirPath, PathFilter f)
  - 子目录名（不递归），按过滤条件筛选。


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

- [DllImport("crt")]static extern long zan_file_length(string path);

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

- static string StripBom(string s)
  - 去掉开头的 UTF-8 BOM（EF BB BF）。BOM 是编码标记而不是
    内容，.NET 的 File.ReadAllText 会据此识别编码并把它排除在结果之外；
    这里也必须去掉，否则以 BOM 保存的文本首字符是三个不可见字节，
    任何"看第一个字符"的解析都会整段失效（CSS 的 `:root` 变量块就是
    一例：选择器变成 \uFEFF:root，不等于 ":root"，变量全部丢弃）。

- static string ReadAllText(string path)
  - 以字符串形式读取整个文件内容。
    文件无法打开时抛出 FileNotFoundException。

- static async string ReadAllTextAsync(string path)
  - 异步读取文件内容（完全对齐 C# .NET 异步 TAP 契约）。

- static void WriteAllText(string path, string content)
  - 将字符串写入文件，不存在则创建，已存在则覆盖。
    文件无法以写入方式打开时抛出 IOException。
    
    以二进制模式按显式长度写出：文本模式（"w"）会在 Windows 上
    把 '\n' 翻成 "\r\n"，而 fputs 会在首个 NUL 处停止 —— 两者都会让
    写回的内容与传入的字符串不一致（写文件再读回不等于原文）。

- static async void WriteAllTextAsync(string path, string content)
  - 异步写入文件内容（完全对齐 C# .NET 异步 TAP 契约）。

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

- static long GetSize64(string path)
  - 获取文件大小（64 位长整型字节数）。
    文件无法打开时抛出 FileNotFoundException。

- static List<string> ReadAllLines(string path)
  - 将文件所有行读入 List。识别 LF 与 CRLF，
    末尾空行不计入结果。
    每行只做一次 Substring 切片，不逐字符复制，
    因此十万行级别的索引文件也能秒级读入。

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

- [DllImport("crt", EntryPoint="memcpy")]static extern nint EmbedCopyIn(byte[]dst, nint src, int n);

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

- [DllImport("crt")]static extern long zan_file_set_time(string path, int which, long unixSec);

- [DllImport("kernel32", EntryPoint="CreateHardLinkW")]static extern int CreateHardLinkW(nint link, nint target, nint secAttrs);

- [DllImport("kernel32", EntryPoint="GetFileAttributesW")]static extern int GetFileAttributesW(nint path);

- [DllImport("crt")]static extern int link(string existing, string newLink);

- static bool SetLastWriteTime(string path, long unixSec)
  - 将最后写入时间设置为 Unix 时间戳（秒）。
    文件不存在或平台无法设置时返回 false。

- static bool SetLastAccessTime(string path, long unixSec)
  - 将最后访问时间设置为 Unix 时间戳（秒）。

- static bool SetCreationTime(string path, long unixSec)
  - 设置创建时间（仅 Windows；其他平台返回 false）。

- static bool CreateHardLink(string link, string target)
  - 在 link 创建指向已有文件 target 的硬链接。
    链接创建失败时返回 false（目标不存在、跨卷、权限不足）。

- static bool IsReparsePoint(string path)
  - 当 path 是 reparse point（目录 junction、符号链接或 OneDrive 占位文件）时返回 true。
    仅 Windows；其他平台返回 false。

- static string GetMimeType(string path)
  - 根据文件扩展名（转小写）猜测 MIME 类型。未知扩展名回退为 application/octet-stream。


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
  - 构造描述 `p` 处文件/目录的元数据对象（不访问磁盘）。

- string FullName()
  - 该实例所描述的路径。

- bool Exists()
  - 路径是否存在（文件或目录）。

- long Length()
  - 文件大小（字节），路径不存在时为 -1。

- long LastWriteTime()
  - 最后修改时间（Unix 秒；不存在为 0）。增量构建的主要依据。

- long CreationTime()
  - 创建时间（Unix 秒；不存在为 0）。

- long LastAccessTime()
  - 最后访问时间（Unix 秒；不存在为 0）。

- bool IsReadOnly()
  - 只读属性。路径不存在时返回 false。

- bool IsHidden()
  - 隐藏属性。路径不存在时返回 false。

- bool IsDirectory()
  - 是否为目录。路径不存在时返回 false。

- bool SetReadOnly(bool on)
  - 将文件设为只读或可写。无法修改时返回 false
    （文件不存在、无权限）。

- bool SetLastWriteTime(long unixSec)
  - 设置最后写入时间（Unix 秒）。

- bool SetLastAccessTime(long unixSec)
  - 设置最后访问时间（Unix 秒）。

- bool SetCreationTime(long unixSec)
  - 设置创建时间（仅 Windows；Unix 秒）。

- bool NewerThan(string other)
  - 当 `this` 比 `other` 新时返回 true，即 `other` 需要重新构建。
    `other` 不存在时视为过期。

- static long GetLastWriteTime(string path)
  - 静态快捷方法：不经实例直接取路径的元数据。
    语义同同名实例方法（不存在时时间戳为 0、长度为 -1）。

- static long GetCreationTime(string path)
  - 创建时间（Unix 秒；不存在为 0）的静态版。

- static long GetLastAccessTime(string path)
  - 最后访问时间（Unix 秒；不存在为 0）的静态版。

- static long GetLength(string path)
  - 文件大小（字节；不存在为 -1）的静态版。

- static long GetAttributes(string path)
  - 原始属性位（位 0 只读、位 1 隐藏、位 2 目录）；路径不存在时为 -1。


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
  - FILE_ATTRIBUTE_REPARSE_POINT（0x400）：junction/符号链接属性位。

- static int ShgfiIcon()
  - SHGFI_ICON：取图标句柄。

- static int ShgfiSmallIcon()
  - SHGFI_SMALLICON：取小图标。

- static int ShgfiUseFileAttributes()
  - SHGFI_USEFILEATTRIBUTES：按文件名扩展名猜测而非打开文件。

- static string Version(string path)
  - 读取 Windows exe/dll 的版本信息资源，返回
    “major.minor.build.revision”格式（取 VS_FIXEDFILEINFO 中产品/文件版本
    较新者）。文件无版本资源或平台非 Windows 时返回
    “”。

- static string JoinVersion(int ms, int ls)
  - 把 VS_FIXEDFILEINFO 的高/低双字拼成 "major.minor.build.revision"。

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
  - s 中最后出现 chars 里任一字符的下标；没有时返回 -1。

- static long GetLastWriteTime(string path)
  - 最后写入的 Unix 时间戳（秒），路径不存在时返回 0。
    另见 `FileInfo.LastWriteTime`。

- static long GetCreationTime(string path)
  - 创建时间（Unix 秒；不存在为 0）的静态透传。

- static long GetLastAccessTime(string path)
  - 最后访问时间（Unix 秒；不存在为 0）的静态透传。

- static bool SetLastWriteTime(string path, long unixSec)
  - 将最后写入时间设置为 Unix 时间戳。
    建议优先使用 `File.SetLastWriteTime`。

- static bool SetLastAccessTime(string path, long unixSec)
  - 将最后访问时间设置为 Unix 时间戳。
    建议优先使用 `File.SetLastAccessTime`。

- static bool SetCreationTime(string path, long unixSec)
  - 设置创建时间（仅 Windows；其他平台返回 false）。
    建议优先使用 `File.SetCreationTime`。

- static bool CreateHardLink(string link, string target)
  - 在 `link` 创建指向已有文件 `target` 的硬链接。
    链接创建失败时返回 false（目标不存在、跨卷、权限不足）。
    建议优先使用 `File.CreateHardLink`。

- static bool IsReparsePoint(string path)
  - 当 `path` 是 reparse point（目录 junction、符号
    链接或 OneDrive 占位文件）时返回 true。仅 Windows；其他平台返回 false。
    建议优先使用 `File.IsReparsePoint`。


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
  - 构造未打开的流；实例经 Open/OpenRead/OpenWrite/Append 创建。

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
  - 以可读模式打开且未关闭时为 true。

- override bool CanWrite()
  - 以可写模式打开且未关闭时为 true。

- override bool CanSeek()
  - 未关闭时为 true（磁盘流总是可 seek）。

- override long Length()
  - 文件长度（字节）。Length 每次调用都 seek 到尾部再回跳，
    频繁调用有开销；已关闭时返回 -1。

- override long Position()
  - 当前读写位置（字节）；已关闭时返回 -1。

- override long Seek(long offset, int origin)
  - 移动读写位置：origin 0=文件头 1=当前位置 2=文件尾，
    返回新位置；已关闭时返回 -1。

- override int ReadInto(nint buf, int count)
  - 实际读到的字节数（0 表示文件尾或已关闭）。

- override int WriteFrom(nint buf, int count)
  - 从 buf 写出最多 count 字节，返回实际写入数；已关闭或 count<=0 返回 0。

- bool EndOfFile()
  - 读取到达文件末尾后为 true。

- override void Flush()
  - 把用户态缓冲刷到操作系统。

- override void Close()
  - 关闭文件句柄；重复调用安全。


## IniEntry (class)

一对 INI 键/值。

- public string key;
  - 键（去除首尾空白）。

- public string text;
  - 值（去除首尾空白，可含 "="）。


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
  - 查找节中的键；节或键不存在时返回 null。

- static IniSection FindSection(IniFile ini, string name)
  - 只查找不创建：节不存在时返回 null。

- static IniSection Section(IniFile ini, string name)
  - 返回节，不存在时创建（追加到末尾）。

- static List<string> SplitLines(string text)
  - 按 \n 拆行并去掉行尾 \r；末尾无换行符的行也计入。

- static int IndexOf(string hay, string needle, int from)
  - 从 from 起查找子串；没有时返回 -1。

- static int ParseInt(string s)
  - 解析非负十进制整数；空串或含非数字字符返回 -1。


## IniSection (class)

一个 INI 节：名称加有序的键/值条目。

- public string name;
  - 节名（无节头的条目归入空串节）。

- public List<IniEntry> entries=new List<IniEntry>();
  - 节内条目（按文件顺序）。


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
  - 从 from 起查找子串；没有时返回 -1。

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

- void Dispose()
  - 释放映射句柄（实现 IDisposable，等同于 Close）。

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
  - 构造空流；实例经 Alloc/FromStr/FromRaw 创建。

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
  - 扩容到至少 needed 字节（倍增策略）；分配失败抛 IOException。

- override bool CanRead()
  - 未 Close 时为 true。

- override bool CanWrite()
  - 未 Close 时为 true。

- override bool CanSeek()
  - 未 Close 时为 true。

- override long Length()
  - 已写入的最高水位字节数。

- override long Position()
  - 当前读写位置。

- override long Seek(long offset, int origin)
  - origin 0=开头 1=当前位置 2=末尾；目标钳制到不小于 0，
    返回新位置。

- void Clear()
  - 清空内容并回到开头；保留缓冲区。

- override int ReadInto(nint buf, int count)
  - 读取最多 count 字节到 buf，返回实际读取数（末尾为 0）。

- override int WriteFrom(nint buf, int count)
  - 从 buf 追加 count 字节（必要时扩容），推进位置与最高水位；
    成功时总是返回 count。

- override void Close()
  - 释放缓冲区；重复调用安全。


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

- static string GetRelativePath(string relativeTo, string path)
  - 返回从 relativeTo 目录到 path 的相对路径（等同于现代 C# 的 Path.GetRelativePath）。
    两者按段拆分；公共前缀被去掉，剩余部分用 "../" 跳转连接。Windows 盘符不区分大小写比较。

- static string Clean(string path)
  - 规范化路径并解析 . 与 .. 段（"a/./b/../c" -> "a/c"）。
    无法解析的前导 .. 会保留。分隔符按输入原样保留。

- static bool IsMatch(string pattern, string path)
  - 当 path 匹配 glob 模式 pattern 时为真。两者都接受
    / 或 \ 分隔符。* 匹配单个路径段内，? 匹配一个字符，** 匹配零个或多个路径段。

- static bool IsUnder(string dir, string path)
  - 当 path 是 dir 的目录前缀（或等于 dir）时为真，
    在 Windows 上按段不区分大小写比较。

- static bool MatchSegments(List<string> pat, int pi, List<string> pth, int ti)
  - 按段递归匹配；"**" 消耗零个或多个路径段。

- static bool SegmentMatch(string pattern, string segment)
  - 单段通配符匹配：'*' 匹配任意字符序列，'?' 匹配单个字符。
    Windows 上不区分大小写。

- static bool SegMatch(string pat, int pi, string seg, int si)
  - 单段匹配的回溯实现；连续 '*' 合并后尝试任意长度。

- static List<string> SplitSegments(string path)
  - 按 '/' 或 '\' 拆分路径（根路径保留空段）。

- static bool Same(string a, string b)
  - 段比较；Windows 上不区分大小写。

- static bool CharEqual(int a, int b)
  - 单字符比较；Windows 上不区分大小写。

- static int IndexOf(string hay, string needle, int from)
  - 从 from 起查找子串；没有时返回 -1。


## PathEx (class)

历史路径扩展辅助工具（已收敛至 `Path`）。
本类完整保留全部公开方法以保证 100% 向后兼容，内部直接转发至 `Path`。
新代码建议直接使用 `Path.IsMatch`、`Path.IsUnder`、
`Path.Clean` 与 `Path.GetRelativePath`。

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
  - 从 `from`（目录）到 `to`（其下或其旁的路径）的相对路径。
    两者按段拆分；公共前缀被去掉，剩余部分用 "../" 跳转连接。
    Windows 盘符不区分大小写比较。


## PathFilter (class)

目录遍历的过滤条件：保留哪些后缀、隐藏哪些后缀、跳过哪些子目录。
后缀表和目录表都是空格分隔的字符串，因此调用点一行写完：

PathFilter src = PathFilter.Only(".zan .html .zscene")
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
  - 跳过以 "." 开头的文件（默认保留）。

- PathFilter()
  - 构造默认过滤条件（全部文件、跳点开头目录）。

- static PathFilter Any()
  - 全部文件。

- static PathFilter Only(string suffixes)
  - 只要这些后缀的文件。

- PathFilter Hide(string suffixes)
  - 隐藏这些后缀（即使命中 exts），链式。

- PathFilter Skip(string dirs)
  - 不进入这些名字的子目录，链式。

- PathFilter Dots(bool dirs, bool files)
  - 是否跳过点开头的目录 / 文件（默认跳目录、留文件）。

- bool KeepFile(string name)
  - 该文件名是否应收录（点开头/隐藏后缀的判断优先于 exts）。

- bool EnterDir(string name)
  - 是否进入该子目录（点开头目录与 skipDirs 命中的不进）。

- static bool Dotted(string name)
  - 名字以 "." 开头时为 true。

- static bool AnySuffix(string list, string name)
  - 空格分隔的表里有一项是 `name` 的结尾时为 true。

- static bool AnyWord(string list, string name)
  - 空格分隔的表里有一项等于 `name` 时为 true。

- static List<string> Words(string list)
  - 按空格拆分列表（忽略连续空格）。


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
  - CLSID_ShellLink：COM 快捷方式对象的类标识。

- static string IidShellLinkW()
  - IID_IShellLinkW：IShellLinkW（宽字符）接口标识。

- static string IidPersistFile()
  - IID_IPersistFile：IPersistFile 接口标识（.lnk 文件的加载/保存）。

- static int SlotGetPath()
  - IShellLinkW vtable 槽位 3：GetPath（取目标路径）。

- static int SlotGetDescription()
  - IShellLinkW vtable 槽位 6：GetDescription（取说明）。

- static int SlotSetDescription()
  - IShellLinkW vtable 槽位 7：SetDescription（设说明）。

- static int SlotGetWorkingDir()
  - IShellLinkW vtable 槽位 8：GetWorkingDirectory（取工作目录）。

- static int SlotSetWorkingDir()
  - IShellLinkW vtable 槽位 9：SetWorkingDirectory（设工作目录）。

- static int SlotGetArguments()
  - IShellLinkW vtable 槽位 10：GetArguments（取参数）。

- static int SlotSetArguments()
  - IShellLinkW vtable 槽位 11：SetArguments（设参数）。

- static int SlotGetIconLocation()
  - IShellLinkW vtable 槽位 16：GetIconLocation（取图标路径与序号）。

- static int SlotSetIconLocation()
  - IShellLinkW vtable 槽位 17：SetIconLocation（设图标路径与序号）。

- static int SlotSetPath()
  - IShellLinkW vtable 槽位 20：SetPath（设目标路径）。

- static int SlotLoad()
  - IPersistFile vtable 槽位 5：Load（打开 .lnk 文件）。

- static int SlotSave()
  - IPersistFile vtable 槽位 6：Save（写出 .lnk 文件）。

- static bool comReady=false;

- static void InitCom()
  - 首次使用时初始化 COM（进程内只做一次）。

- static bool Create(string lnkPath, string target, string arguments, string workingDir, string iconPath, int iconIndex, string description)
  - 在 `lnkPath` 处创建（或覆盖）一个指向
    `target` 的 .lnk。可选：参数、工作目录、图标路径+
    图标索引、说明。成功返回 true。不需要的字段传空串/0。

- static bool WriteDesktopEntry(string path, string target, string arguments, string workingDir, string iconPath, string description)
  - freedesktop Desktop Entry Specification：一个带 [Desktop Entry] 头的
    INI 文件，Type=Application 时 Exec 是要执行的命令行。

- static string EntryName(string path)
  - Desktop Entry 的 Name 默认用文件名（去掉 .desktop 后缀）。

- static string QuoteArg(string s)
  - Exec 行里带空白的可执行路径需要引号（规范 1.5 章）。

- static string ExecTarget(string exec)
  - Exec 的第一个 token 是目标程序，其余是参数；首 token 允许带引号。

- static string ExecArguments(string exec)
  - Exec 行去掉目标程序后的剩余参数串（原样保留）；无参数时为空串。

- static string EntryValue(string text, string key)
  - 在 Desktop Entry 文本中取 `key=` 行 "=" 之后的值；找不到返回空串。

- static ShortcutInfo Read(string lnkPath)
  - 解析 .lnk（Linux 上为 .desktop）文件，返回各字段。
    文件不存在或解析失败时返回全空字段，不抛异常。
    快捷方式未定义的字段返回空值（iconIndex 为 0）。

- static string GetPath(nint shell)
  - IShellLinkW::GetPath(LPWSTR, int cch, WIN32_FIND_DATAW*, DWORD flags).

- static string GetText(nint shell, int slot)
  - 形状为 (LPWSTR, int cch) 的 getter。

- static string GetIconPath(nint shell)
  - GetIconLocation 的路径部分；失败返回空串。

- static int GetIconIndex(nint shell)
  - GetIconLocation 的图标序号；失败返回 0。


## ShortcutInfo (class)

由
`Shortcut.Read` 返回的结构化 .lnk 内容。字段可能为空串
（快捷方式未定义时），iconIndex 默认 0。

- public string target;
  - 快捷方式指向的目标程序路径。

- public string arguments;
  - 目标程序的命令行参数（原样保存，未做转义还原）。

- public string workingDir;
  - 目标程序的工作目录；未设置时为空。

- public string description;
  - 快捷方式的说明文字。

- public string iconPath;
  - 图标文件路径（.ico/.exe/.dll）；为空表示用目标程序自带图标。

- public int iconIndex;
  - 图标文件内的图标序号（从 0 起）。

- ShortcutInfo()
  - 构造各字段为空串、iconIndex 为 0 的信息。


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

- virtual long Seek(long offset, SeekOrigin origin)
  - 按指定参考点设置当前流中的位置（对齐 C# .NET 标准的 Seek 重载）。

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

- virtual void Dispose()
  - 执行与释放、释放或重置非托管资源关联的应用程序定义的任务（实现 IDisposable）。

- string Read(int count)
  - 读取最多 `count` 字节；流结束时返回 ""。

- int WriteFromAll(nint buf, int count)
  - 从裸地址 `buf` 循环写出 `count` 字节，直到全部写出或发生错误。

- int Write(string data)
  - 写入 `data` 的全部字节。返回实际写入的字节数。

- int Write(string data, int count)
  - 写入 `data` 的前 `count` 个字节（二进制安全，可含 NUL）。

- int WriteBytes(byte[]data, int offset, int count)
  - 写入字节数组的一部分（二进制安全）。

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

- StreamReader(string path):this()
  - 打开文件用于读取。若文件不存在则抛出 FileNotFoundException。

- StreamReader(Stream s):this()
  - 从现有流读取文本（不由本 reader 关闭）。

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

- void Dispose()
  - 释放 reader 占用的资源（实现 IDisposable，等同于 Close）。


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

- void Dispose()
  - 释放 writer 占用的资源（实现 IDisposable，等同于 Close）。


## int (delegate)

SetEnvironmentVariableW 的函数指针签名（经 Interop.Entry 取用）。

`delegate int SetEnvironmentVariableWFn(nint name, nint val);`


## FileAccess (enum)

定义文件的读取、写入或读/写访问权限（完全对齐 C# .NET 的 FileAccess）。

- Read = =1

- Write = =2

- ReadWrite = =3


## FileMode (enum)

指定操作系统打开文件的方式（完全对齐 C# .NET 的 FileMode）。

- CreateNew = =1

- Create = =2

- Open = =3

- OpenOrCreate = =4

- Truncate = =5

- Append = =6


## SeekOrigin (enum)

指定在流中查找的参考点（完全对齐 C# .NET 的 SeekOrigin）。

- Begin = =0

- Current = =1

- End = =2
