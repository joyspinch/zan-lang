# System.IO

> 源码: `packages/Zan.IO/src/System/IO/DirectoryTree.zan`, `packages/Zan.IO/src/System/IO/FileAccess.zan`, `packages/Zan.IO/src/System/IO/FileMode.zan`, `packages/Zan.IO/src/System/IO/IniFile.zan`, `packages/Zan.IO/src/System/IO/KnownFolders.zan`, `packages/Zan.IO/src/System/IO/MemoryMappedFile.zan`, `packages/Zan.IO/src/System/IO/MemoryStream.zan`, `packages/Zan.IO/src/System/IO/Path.zan`, `packages/Zan.IO/src/System/IO/PathEx.zan`, `packages/Zan.IO/src/System/IO/Shortcut.zan`, `packages/Zan.IO/src/System/IO/StreamReader.zan`, `packages/Zan.IO/src/System/IO/StreamWriter.zan`, `stdlib/System/IO/ByteBuffer.zan`, `stdlib/System/IO/Directory.zan`, `stdlib/System/IO/File.zan`, `stdlib/System/IO/FileInfo.zan`, `stdlib/System/IO/FileInfoEx.zan`, `stdlib/System/IO/FileStream.zan`, `stdlib/System/IO/SeekOrigin.zan`, `stdlib/System/IO/Stream.zan`


## ByteBuffer (class)

- nint data;

- int capacity;

- int length;

- int rpos;

- ByteBuffer()

- static ByteBuffer Alloc(int initialCapacity)

- static ByteBuffer FromRaw(nint src, int count)

- static ByteBuffer FromStr(string s)

- string ToStr()

- byte[]ToBytes()

- void Free()

- int Length()

- int ReadPos()

- int Remaining()

- nint Raw()

- ByteBuffer SeekRead(int pos)

- ByteBuffer Clear()

- ByteBuffer EnsureCapacity(int cap)

- ByteBuffer SetLength(int n)

- void EnsureRoom(int extra)

- ByteBuffer WriteU8(int v)

- ByteBuffer WriteU16(int v)

- ByteBuffer WriteU32(long v)

- ByteBuffer WriteU64(long v)

- ByteBuffer WriteVarInt(long v)

- ByteBuffer WriteBytes(nint src, int count)

- ByteBuffer WriteRaw(string src, int count)

- ByteBuffer WriteBytes(byte[]src, int offset, int count)

- int ByteAt(int i)

- int IndexOfByte(int from, int b)

- string Str(int off, int count)

- ByteBuffer Discard(int count)

- ByteBuffer WriteString(string s)

- bool CanRead(int n)

- int ReadU8()

- int ReadU16()

- long ReadU32()

- long ReadU64()

- long ReadVarInt()

- int PeekU8(int pos)

- long ReadVarIntAt(int pos, out int np)

- int CmpRangeRaw(int off, int len, string other)

- nint ReadBytes(int count)

- string ReadString()

- long Crc32(int count)

- long Crc32C(int count)


## Directory (class)

- [DllImport("crt")]static extern string zan_file_read_path(string path);

- static string ReadPath(string path)

- [DllImport("crt")]static extern string zan_embed_list(string prefix);

- static List<string> EmbedChildren(string path, bool dirs)

- static bool EmbedExists(string path)

- static List<string> MergeEmbed(List<string> disk, string path, bool dirs)

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

- [DllImport("crt", EntryPoint="readdir")]static extern nint readdir(nint dirp);

- [DllImport("crt", EntryPoint="readlink")]static extern long ReadLink(string path, byte[]buf, int size);

- [DllImport("crt", EntryPoint="closedir")]static extern int closedir(nint dirp);

- [DllImport("crt", EntryPoint="mkdir")]static extern int mkdir(string path, int mode);

- [DllImport("crt", EntryPoint="rmdir")]static extern int rmdir(string path);

- [DllImport("crt", EntryPoint="chdir")]static extern int chdir(string path);

- [DllImport("crt", EntryPoint="getcwd")]static extern string getcwd(string buf, int size);

- static nint winWide(string text)

- static string winUtf8(nint wide)

- static int winAttributesUtf8(string path)

- static bool ExistsUtf8(string path)

- static bool FileExistsUtf8(string path)

- static void CreateDirectoryUtf8(string path)

- static List<string> GetRootsUtf8()

- static bool Exists(string path)

- static bool ExistsOnDisk(string path)

- static void CreateDirectory(string path)

- static void Delete(string path)

- static string GetCurrentDirectory()

- static void SetCurrentDirectory(string path)

- static string winName(nint findData)

- static List<string> winList(string path, bool wantDirs, bool includeHidden)

- static int direntTypeOffset()

- static int direntNameOffset()

- static int direntDirType()

- static int direntTypeOffset()

- static int direntNameOffset()

- static int direntDirType()

- static int direntTypeOffset()

- static int direntNameOffset()

- static int direntDirType()

- static string posixName(nint ent)

- static bool IsSymlink(string path)

- static List<string> posixList(string path, bool wantDirs, bool includeHidden)

- static List<string> ToPaths(List<string> names, string baseDir)

- static List<string> GetPaths(string path)

- static List<string> GetDirectoryPaths(string path)

- static List<string> GetFiles(string path)

- static List<string> GetDirectories(string path)

- static List<string> GetFilesFiltered(string path, bool includeHidden)

- static List<string> GetDirectoriesFiltered(string path, bool includeHidden)

- static void CreateDirectoryRecursive(string path)

- static bool DeleteRecursive(string path)


## DirectoryTree (class)

- static bool EndsWith(string s, string suffix)

- static void FilesInto(List<string> outp, string dirPath, PathFilter f)

- static List<string> Files(string dirPath, PathFilter f)

- static void FlatInto(List<string> outp, string dirPath, PathFilter f)

- static List<string> Flat(string dirPath, PathFilter f)

- static List<string> Names(string dirPath, PathFilter f)

- static List<string> SubDirs(string dirPath, PathFilter f)

- static List<string> SubNames(string dirPath, PathFilter f)


## File (class)

- [DllImport("crt", EntryPoint="zan_file_fopen")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern nint zan_pkg_fopen(string path, string mode);

- [DllImport("crt")]static extern int fclose(nint fp);

- [DllImport("crt")]static extern int fseek(nint fp, int offset, int origin);

- [DllImport("crt", EntryPoint="_ftelli64")]static extern long ftell64(nint fp);

- [DllImport("crt", EntryPoint="ftello")]static extern long ftell64(nint fp);

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

- static List<string> EmbeddedNames(string prefix)

- static bool EmbedExists(string path)

- static string StripBom(string s)

- static string ReadAllText(string path)

- static async string ReadAllTextAsync(string path)

- static void WriteAllText(string path, string content)

- static async void WriteAllTextAsync(string path, string content)

- static void AppendAllText(string path, string content)

- static void AppendAllTextAsync(string path, string content)

- static bool Exists(string path)

- [DllImport("crt")]static extern long zan_file_try_lock(string path);

- [DllImport("crt")]static extern long zan_file_unlock(long handle);

- static long TryLock(string path)

- static bool Unlock(long handle)

- [DllImport("crt")]static extern int chmod(string path, int mode);

- [DllImport("crt")]static extern int access(string path, int mode);

- static bool SetExecutable(string path, bool on)

- static bool IsExecutable(string path)

- static int AttrsOfUtf8(string path)

- static void Delete(string path)

- static void Move(string source, string dest)

- static void Copy(string source, string dest)

- static int GetSize(string path)

- static long GetSize64(string path)

- static List<string> ReadAllLines(string path)

- static void WriteAllLines(string path, List<string> lines)

- static void WriteBytes(string path, string data, int count)

- static byte[]ReadBytes(string path, int offset, int count)

- static byte[]ReadAllBytes(string path)

- [DllImport("crt", EntryPoint="memcpy")]static extern nint EmbedCopyIn(byte[]dst, nint src, int n);

- static void WriteAllBytes(string path, byte[]data)

- static bool ExtractEmbedded(string path, string dest)

- static void AppendAllBytes(string path, byte[]data)

- [DllImport("crt")]static extern long zan_file_set_time(string path, int which, long unixSec);

- [DllImport("kernel32", EntryPoint="CreateHardLinkW")]static extern int CreateHardLinkW(nint link, nint target, nint secAttrs);

- [DllImport("kernel32", EntryPoint="GetFileAttributesW")]static extern int GetFileAttributesW(nint path);

- [DllImport("kernel32", EntryPoint="DeleteFileW")]static extern int WinDeleteFile(nint path);

- [DllImport("kernel32", EntryPoint="RemoveDirectoryW")]static extern int WinRemoveDirectory(nint path);

- [DllImport("crt")]static extern int link(string existing, string newLink);

- static bool SetLastWriteTime(string path, long unixSec)

- static bool SetLastAccessTime(string path, long unixSec)

- static bool SetCreationTime(string path, long unixSec)

- static bool CreateHardLink(string link, string target)

- static bool IsReparsePoint(string path)

- static string GetMimeType(string path)


## FileInfo (class)

- [DllImport("crt")]static extern long zan_file_time(string path, int which);

- [DllImport("crt")]static extern long zan_file_length(string path);

- [DllImport("crt")]static extern long zan_file_attributes(string path);

- [DllImport("crt")]static extern long zan_file_set_readonly(string path, int on);

- string path;

- FileInfo(string p)

- string FullName()

- bool Exists()

- long Length()

- long LastWriteTime()

- long CreationTime()

- long LastAccessTime()

- bool IsReadOnly()

- bool IsHidden()

- bool IsDirectory()

- bool SetReadOnly(bool on)

- bool SetLastWriteTime(long unixSec)

- bool SetLastAccessTime(long unixSec)

- bool SetCreationTime(long unixSec)

- bool NewerThan(string other)

- static long GetLastWriteTime(string path)

- static long GetCreationTime(string path)

- static long GetLastAccessTime(string path)

- static long GetLength(string path)

- static long GetAttributes(string path)


## FileInfoEx (class)

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

- static string JoinVersion(int ms, int ls)

- static nint Icon(string path)

- static void DestroyIcon(nint icon)

- static string Mime(string path)

- static string Extension(string path)

- static int LastIndexOfAny(string s, string chars)

- static long GetLastWriteTime(string path)

- static long GetCreationTime(string path)

- static long GetLastAccessTime(string path)

- static bool SetLastWriteTime(string path, long unixSec)

- static bool SetLastAccessTime(string path, long unixSec)

- static bool SetCreationTime(string path, long unixSec)

- static bool CreateHardLink(string link, string target)

- static bool IsReparsePoint(string path)


## FileStream (class)

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

- static FileStream OpenRead(string path)

- static FileStream OpenWrite(string path)

- static FileStream Append(string path)

- string Path()

- bool IsClosed()

- override bool CanRead()

- override bool CanWrite()

- override bool CanSeek()

- override long Length()

- override long Position()

- override long Seek(long offset, int origin)

- override int ReadInto(nint buf, int count)

- override int WriteFrom(nint buf, int count)

- bool EndOfFile()

- override void Flush()

- override void Close()


## IniEntry (class)

- public string key;

- public string text;


## IniFile (class)

- List<IniSection> sections=new List<IniSection>();

- static IniFile Parse(string text)

- static IniFile Load(string path)

- string GetString(string section, string key, string fallback)

- int GetInt(string section, string key, int fallback)

- void SetString(string section, string key, string val)

- void SetInt(string section, string key, int val)

- bool RemoveKey(string section, string key)

- bool RemoveSection(string section)

- List<string> EnumSections()

- List<string> EnumKeys(string section)

- string ToText()

- void Save(string path)

- static IniEntry Find(IniFile ini, string section, string key)

- static IniSection FindSection(IniFile ini, string name)

- static IniSection Section(IniFile ini, string name)

- static List<string> SplitLines(string text)

- static int IndexOf(string hay, string needle, int from)

- static int ParseInt(string s)


## IniSection (class)

- public string name;

- public List<IniEntry> entries=new List<IniEntry>();


## KnownFolders (class)

- static string Home()

- static string Desktop()

- static string Documents()

- static string Downloads()

- static string AppData()

- static string LocalAppData()

- private static string HomeChild(string name)

- private static string XdgBaseDirectory(string variable, string fallback)

- private static string XdgUserDirectory(string variable, string fallback)

- private static string XdgUserValue(string text, string home)

- static string Temp()

- static string UserName()

- static string ComputerName()

- static string GetEnv(string name)

- static void SetEnv(string name, string val)

- static string Expand(string text)

- static int IndexOf(string hay, string needle, int from)

- [DllImport("crt", EntryPoint="setenv")]static extern int PosixSetenv(string name, string value, int overwrite);

- [DllImport("crt", EntryPoint="unsetenv")]static extern int PosixUnsetenv(string name);


## MemoryMappedFile (class)

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

- static MemoryMappedFile OpenExisting(string name, long size)

- static MemoryMappedFile CreateFromFile(string path, long size)

- nint Map(long size)

- bool Unmap(nint view, long size)

- bool Flush(nint view, long size)

- void Close()

- void Dispose()

- static bool Unlink(string name)


## MemoryStream (class)

- nint data;

- int capacity;

- int length;

- int pos;

- MemoryStream()

- static MemoryStream Alloc(int initialCapacity)

- static MemoryStream FromStr(string s)

- static MemoryStream FromRaw(nint src, int count)

- string ToStr()

- nint Raw()

- void Grow(int needed)

- override bool CanRead()

- override bool CanWrite()

- override bool CanSeek()

- override long Length()

- override long Position()

- override long Seek(long offset, int origin)

- void Clear()

- override int ReadInto(nint buf, int count)

- override int WriteFrom(nint buf, int count)

- override void Close()


## Path (class)

- [DllImport("kernel32", EntryPoint="GetTempPathW")]static extern int WinGetTempPath(int bufLen, nint buf);

- [DllImport("kernel32", EntryPoint="GetFullPathNameW")]static extern int WinGetFullPathName(nint path, int bufLen, nint buf, nint filePart);

- static int AfterLastSeparator(string path)

- static string GetFileName(string path)

- static string GetExtension(string path)

- static string Separator()

- static string Combine(string path1, string path2)

- static bool IsPathRooted(string path)

- private static bool IsDirectorySeparator(int c)

- static string GetFullPath(string path)

- static string GetDirectoryName(string path)

- static string GetFileNameWithoutExtension(string path)

- static string ChangeExtension(string path, string extension)

- static bool HasExtension(string path)

- static string GetTempPath()

- static string Normalize(string path)

- static string GetRelativePath(string relativeTo, string path)

- static string Clean(string path)

- static bool IsMatch(string pattern, string path)

- static bool IsUnder(string dir, string path)

- static bool MatchSegments(List<string> pat, int pi, List<string> pth, int ti)

- static bool SegmentMatch(string pattern, string segment)

- static bool SegMatch(string pat, int pi, string seg, int si)

- static List<string> SplitSegments(string path)

- static bool Same(string a, string b)

- static bool CharEqual(int a, int b)

- static int IndexOf(string hay, string needle, int from)


## PathEx (class)

- static bool IsMatch(string pattern, string path)

- static bool IsUnder(string dir, string path)

- static string Clean(string path)

- static string GetRelativePath(string from, string to)


## PathFilter (class)

- string exts;

- string hideExts;

- string skipDirs;

- bool skipDotDirs;

- bool skipDotFiles;

- PathFilter()

- static PathFilter Any()

- static PathFilter Only(string suffixes)

- PathFilter Hide(string suffixes)

- PathFilter Skip(string dirs)

- PathFilter Dots(bool dirs, bool files)

- bool KeepFile(string name)

- bool EnterDir(string name)

- static bool Dotted(string name)

- static bool AnySuffix(string list, string name)

- static bool AnyWord(string list, string name)

- static List<string> Words(string list)


## Shortcut (class)

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

- [DllImport("crt", EntryPoint="symlink")]static extern int PosixSymlink(string target, string linkpath);

- [DllImport("crt", EntryPoint="readlink")]static extern long PosixReadlink(string path, byte[]buf, int size);

- static bool WriteDesktopEntry(string path, string target, string arguments, string workingDir, string iconPath, string description)

- static string EntryName(string path)

- static string QuoteArg(string s)

- static string ExecTarget(string exec)

- static string ExecArguments(string exec)

- static string EntryValue(string text, string key)

- static ShortcutInfo Read(string lnkPath)

- static string GetPath(nint shell)

- static string GetText(nint shell, int slot)

- static string GetIconPath(nint shell)

- static int GetIconIndex(nint shell)


## ShortcutInfo (class)

- public string target;

- public string arguments;

- public string workingDir;

- public string description;

- public string iconPath;

- public int iconIndex;

- ShortcutInfo()


## Stream (class)

- virtual bool CanRead()

- virtual bool CanWrite()

- virtual bool CanSeek()

- virtual long Length()

- virtual long Position()

- virtual long Seek(long offset, int origin)

- virtual long Seek(long offset, SeekOrigin origin)

- virtual int ReadInto(nint buf, int count)

- virtual int WriteFrom(nint buf, int count)

- virtual void Flush()

- virtual void Close()

- virtual void Dispose()

- string Read(int count)

- int WriteFromAll(nint buf, int count)

- int Write(string data)

- int Write(string data, int count)

- int WriteBytes(byte[]data, int offset, int count)

- int ReadByte()

- void WriteByte(int b)

- string ReadToEnd()

- long CopyTo(Stream dest)

- long SeekTo(long position)

- long Remaining()


## StreamReader (class)

- Stream stream;

- bool owns;

- string buf;

- int pos;

- bool eof;

- int chunk;

- StreamReader()

- StreamReader(string path):this()

- StreamReader(Stream s):this()

- static StreamReader Wrap(Stream s)

- static StreamReader Open(string path)

- static StreamReader FromStr(string text)

- Stream BaseStream()

- bool EndOfStream()

- void Fill()

- string ReadChar()

- string ReadBlock(int count)

- string ReadLine()

- List<string> ReadAllLines()

- string ReadToEnd()

- void Close()

- void Dispose()


## StreamWriter (class)

- Stream stream;

- bool owns;

- StringBuilder buf;

- int buffered;

- int limit;

- string newline;

- StreamWriter()

- static StreamWriter Wrap(Stream s)

- StreamWriter(string path):this()

- static StreamWriter Append(string path)

- Stream BaseStream()

- StreamWriter SetNewLine(string nl)

- StreamWriter Write(string text)

- StreamWriter WriteLine(string text)

- StreamWriter WriteEmptyLine()

- void Flush()

- void Close()

- void Dispose()


## int (delegate)

`delegate int SetEnvironmentVariableWFn(nint name, nint val);`


## FileAccess (enum)

- Read = =1

- Write = =2

- ReadWrite = =3


## FileMode (enum)

- CreateNew = =1

- Create = =2

- Open = =3

- OpenOrCreate = =4

- Truncate = =5

- Append = =6


## SeekOrigin (enum)

- Begin = =0

- Current = =1

- End = =2
