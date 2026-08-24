# System.IO.Compression

> 源码: `stdlib/System/IO/Compression/BZip2.zan`, `stdlib/System/IO/Compression/Crc32.zan`, `stdlib/System/IO/Compression/Deflate.zan`, `stdlib/System/IO/Compression/GZip.zan`, `stdlib/System/IO/Compression/Tar.zan`, `stdlib/System/IO/Compression/Zip.zan`


## BZip2 (class)

纯 Zan 实现的 bzip2 解压（.bz2 / .tar.bz2）—— 不依赖 libbz2 或任何
外部依赖，可在所有平台运行。

只实现解压：bzip2 归档在互操作场景里（CEF 运行时、Linux 源码包）
只需要读。压缩侧请用 `Deflate` / `GZip`。

byte[] plain = BZip2.Decompress(File.ReadAllBytes("a.bz2"));
BZip2.DecompressFile("cef.tar.bz2", "cef.tar");   // 流式，内存恒定

每个块的 CRC 都会校验，因此损坏的归档会被拒绝（返回 null /
false）而不是产出垃圾数据。

- static byte[]Decompress(byte[]data)
  - 解压内存中的 .bz2 数据；损坏或不支持时返回 null。

- static bool DecompressFile(string srcPath, string dstPath)
  - 把 `srcPath`（.bz2）流式解压到 `dstPath`；
    内存占用与文件大小无关。成功返回 true。


## BZip2Decoder (class)

bzip2 解码器的内部状态机。每次解压新建一个实例。

- [DllImport("crt", EntryPoint="fopen")]static extern nint PlatFopen(string path, string mode);

- [DllImport("crt", EntryPoint="fread")]static extern long PlatFread(byte[]buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fwrite")]static extern long PlatFwrite(byte[]buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fclose")]static extern int PlatFclose(nint fp);

- static int MaxAlphaSize=258;

- static int MaxGroups=6;

- static int MaxCodeLen=23;

- static int GroupSize=50;

- nint fp;

- byte[]inBuf;

- int inLen;

- int inPos;

- byte[]memSrc;

- int bitBuf;

- int bitCnt;

- bool inEof;

- nint ofp;

- byte[]outBuf;

- int outPos;

- List <byte[]> memOut;

- long outTotal;

- int[]tt;

- int[]unzftab;

- int[]cftab;

- byte[]seqToUnseq;

- byte[]mtf;

- int[]selector;

- int[]len;

- int[]limit;

- int[]baseTab;

- int[]perm;

- int[]minLens;

- int[]crcTable;

- int blockCrc;

- int combinedCrc;

- bool failed;

- BZip2Decoder()

- static int[]BuildCrcTable()

- void CrcUpdate(int b)

- bool Refill()

- int NextByte()

- int GetBits(int n)
  - 读取 n（1..24）位；输入耗尽返回 -1。

- int GetBit()

- int GetInt32()
  - 32 位字段（CRC）分两次读，避免 (1 << 32) 溢出。

- void Emit(int b)

- void FlushOut()

- byte[]RunMemory(byte[]data)

- bool RunFile(string srcPath, string dstPath)

- bool Decode()
  - 解析流头部与所有块。

- bool DecodeBlock(int blockSize)
  - 解码一个压缩块：符号表 → Huffman → MTF/RLE2 → 逆 BWT → RLE1。

- bool BuildDecodeTables(int t, int alphaSize)
  - bzip2 的 hbCreateDecodeTables：为一个组构建 limit/base/perm。

- int NextSymbol(ref int groupNo, ref int groupPos, ref int gSel, int nSelectors)
  - 取下一个 MTF 符号；每 50 个符号换一次 Huffman 表。


## Crc32 (class)

CRC-32（IEEE 802.3，zlib 多项式 0xEDB88320）。

- static int[]table;

- static int Poly=-306674912;

- static int AllOnes=-1;

- static int[]Table()

- static int Compute(byte[]data, int offset, int len, int crc)
  - 从 `crc` 起始对 `data` 计算 CRC-32（首次计算传 0）。

- static int Compute(byte[]data)


## Deflate (class)

纯 Zan 实现的 RFC 1951 DEFLATE —— 不依赖 zlib 或任何外部
依赖，可在所有平台运行。

Inflate 支持解码 stored、固定 Huffman 和动态 Huffman 块。
Deflate 对不可压缩输入输出 stored 块；对可压缩输入，
使用带 LZ77 哈希表匹配器的固定 Huffman 块（级别
1-9 选择哈希链长度；格式与 zlib 默认输出一致，
因此可与任何标准 inflate 实现互通）。

- static int ReadBits(byte[]src, ref int pos, ref int bit, int n, int end)

- static int ReadBit(byte[]src, ref int pos, ref int bit, int end)

- static void AlignByte(ref int bit)

- static int[]LengthExtra=new int[]{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

- static int[]LengthBase=new int[]{ 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};

- static int[]DistExtra=new int[]{ 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

- static int[]DistBase=new int[]{ 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

- static int BuildHuffman(int[]count, int[]symbol, int[]lengths, int n)
  - 根据码长数组构建规范 Huffman 解码表。
    `lengths` 保存每个符号的位长；`n` 为符号总数。
    成功返回 0，码集无效时返回 -1。

- static int DecodeSymbol(int[]count, int[]symbol, byte[]src, ref int pos, ref int bit, int end)

- static void FixedTables(int[]litCount, int[]litSymbol, int[]distCount, int[]distSymbol)
  - 固定（RFC 1951 §3.2.6）字面量/长度与距离表。

- static byte[]Inflate(byte[]src, int offset, int len, int maxOut)
  - 解压从 `src[offset]` 开始的 `len` 字节 DEFLATE 数据。
    `maxOut` 限制输出大小（超出返回 null）；传 0 表示
    允许最大 256 MB 的任意大小。

- static byte[]InflateConsumed(byte[]src, int offset, int len, int maxOut, ref int consumed)
  - 与 Inflate 类似，但会报告已消耗的输入字节数
    （截至并包括最后一个块的 EOB 符号）。

- static int DynamicTables(int[]litCount, int[]litSymbol, int[]distCount, int[]distSymbol, byte[]src, ref int pos, ref int bit, int end)

- static byte[]Ensure(byte[]buf, ref int used, int need, int maxOut)

- static byte[]Deflate(byte[]src, int level)
  - 将 `src` 压缩为 DEFLATE 流。`level` 0 原样存储数据
    （最快）；1-9 使用带
    哈希链匹配器的 LZ77 固定 Huffman 压缩（级别越高链越长，压缩比越好，但越慢）。

- static byte[]Store(byte[]src, int offset, int len)

- static byte[]FixedCompress(byte[]src, int level)

- static void EmitLitFixed(byte[]out2, ref int bitPos, int sym)

- static void EmitLengthDistance(byte[]out2, ref int bitPos, int length, int dist)

- static void EmitFixed(int code, byte[]out2, ref int bitPos)

- static void WriteBits(byte[]out2, ref int bitPos, int v, int n)

- static void WriteBitsMsb(byte[]out2, ref int bitPos, int v, int n)

- static byte[]Trim(byte[]buf, int len)

- static byte[]ZlibCompress(byte[]src, int level)
  - zlib 流（2 字节头 + adler32 尾部）包裹原始 DEFLATE
    流。大多数 .NET/Java/zip 工具可直接消费。


## GZip (class)

RFC 1952 gzip 容器：10 字节头、DEFLATE 数据、CRC-32 与
ISIZE 尾部。可与 .NET 的 GZipStream、gzip、zlib 及
任何其他符合规范的读取器互通。无外部依赖。

- static byte[]Compress(byte[]data, int level)
  - 对 `data` 进行 gzip 压缩（级别 1-9；0 为原样存储）。

- static byte[]Decompress(byte[]src)
  - 解压 gzip 流。支持多成员流
    （多个 gzip 成员拼接），并跳过 FEXTRA/FNAME/FCOMMENT/FHCRC
    头字段。输入格式错误时返回 null。


## Tar (class)

POSIX ustar tar 读写器（无压缩 —— tar 只是容器；
与 GZip 组合即为 .tar.gz）。可与 bsdtar、GNU tar、
Windows 的 tar.exe、Python tarfile 和 7-Zip 互通。超过 100 字节的名称会使用
ustar 前缀字段，因此长路径也能保留。

- [DllImport("crt", EntryPoint="fopen")]static extern nint PlatFopen(string path, string mode);

- [DllImport("crt", EntryPoint="fread")]static extern long PlatFread(byte[]buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fwrite")]static extern long PlatFwrite(byte[]buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fclose")]static extern int PlatFclose(nint fp);

- static byte[]Create(List<TarEntry> entries)
  - 在内存中构建 tar 归档。目录条目应
    标记 isDirectory（或名称以 "/" 结尾）。

- static List<TarEntry> Read(byte[]tar)
  - 读取 tar 归档中的所有条目（含数据）。

- static void WriteHeader(byte[]b, int off, TarEntry e)

- static int HeaderSum(byte[]b, int off)

- static bool IsZeroBlock(byte[]b, int off)

- static void WriteStr(byte[]b, int off, string s, int maxLen)

- static string ReadStr(byte[]b, int off, int maxLen)

- static void WriteOctal(byte[]b, int off, long v, int fieldLen)

- static int ParseOctal(byte[]b, int off, int fieldLen)

- static long ParseSize(byte[]b, int off)
  - size 字段（12 位八进制）可能超过 int 范围，必须用 long
    累计，否则攻击者把 32 位截断值写进 size 就能绕过边界检查。

- static int ExtractFile(string tarPath, string destDir)
  - 把 tar 归档流式解包到目录（按 64 KiB 分块读写，不把归档
    整体读入内存，适合数百 MB 的运行时包）。支持 ustar 前缀长名与
    GNU 长名（'L'）条目；符号链接/设备等条目被跳过。返回写出的文件与
    目录数，归档损坏或路径越界时返回 -1。

- static bool SafeName(string name)

- static int LastSlash(string path)

- static bool WriteMember(nint fp, byte[]buf, string outPath, long size)

- static string ReadName(nint fp, byte[]buf, long size)

- static bool SkipBytes(nint fp, byte[]buf, long count)


## TarEntry (class)

tar 归档中的一个条目。

- public string name;
  - 归档内的路径，使用正斜杠，目录以 "/" 结尾。

- public byte[]data;
  - 文件内容（目录条目为 null）。

- public bool isDirectory;
  - 目录条目为 true。

- public int mode;
  - POSIX 权限（文件默认 0644，目录默认 0755）。

- public long mtime;
  - Unix 修改时间（秒）；0 表示未设置。

- TarEntry(string name)


## Zip (class)

ZIP 归档读写器（PKZIP，无加密，支持 stored + deflate
条目）。可与所有标准解压工具互通（Windows 资源管理器、
.NET ZipArchive、Python zipfile、7-Zip、Info-ZIP 等）。不依赖
zlib 或任何原生库。

- static int SigLocal=0x04034B50;

- static int SigCentral=0x02014B50;

- static int SigEnd=0x06054B50;

- static int MethodStored=0;

- static int MethodDeflate=8;

- static byte[]Create(List<ZipEntry> entries, int level)
  - 根据 `entries` 在内存中构建 zip 归档。目录条目是
    名称以 "/" 结尾或 isDirectory 为 true 的条目。
    压缩级别 1-9（0 = 存储）。返回的字节为完整
    归档：本地头 + 数据、中央目录、EOCD。

- static List<ZipEntry> Read(byte[]zip)
  - 读取中央目录并返回条目列表（数据未
    加载）。

- static byte[]Extract(byte[]zip, ZipEntry e)
  - 提取单个条目的内容（必要时解压）。
    条目无法定位或已损坏时返回 null。

- static int ExtractToDirectory(string zipPath, string destDir)
  - 把 zip 归档解包到目录：目录条目建目录，文件条目按归档内
    的相对路径写出（缺的中间目录会补上）。返回写出的条目数；归档读不动、
    中央目录损坏、某个条目解压失败或路径越出 <paramref name="destDir"/>
    时返回 -1。
    
    与 `Tar.ExtractFile` 对齐的一点：归档内路径必须是不含
    ".." 段的相对路径，否则整包按损坏处理——一个精心构造的归档否则能
    写到目标目录之外。

- static int FindLocal(byte[]zip, string name)

- static bool EndsWithSlash(string s)

- static string ReadName(byte[]zip, int off, int len)

- static int ReadU16(byte[]b, int off)

- static long ReadU32(byte[]b, int off)

- static void WriteU16(byte[]b, ref int off, int v)

- static void WriteU32(byte[]b, ref int off, long v)


## ZipEntry (class)

zip 归档中的一个条目。

- public string name;
  - 归档内的路径，使用正斜杠，目录以 "/" 结尾。

- public byte[]data;
  - 文件内容（目录条目为 null）。

- public bool isDirectory;
  - 目录条目为 true。

- public int method;

- public int crc32;

- public int compressedSize;

- public int uncompressedSize;

- ZipEntry(string name)
