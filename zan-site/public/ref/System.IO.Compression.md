# System.IO.Compression

> 源码: `packages/Zan.IO/src/System/IO/Compression/BZip2.zan`, `packages/Zan.IO/src/System/IO/Compression/Crc32.zan`, `packages/Zan.IO/src/System/IO/Compression/Deflate.zan`, `packages/Zan.IO/src/System/IO/Compression/GZip.zan`, `packages/Zan.IO/src/System/IO/Compression/Tar.zan`, `packages/Zan.IO/src/System/IO/Compression/Zip.zan`


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
  - 构造解码器：分配固定大小的输入/输出缓冲并建 CRC 表；
    实际解压经 RunMemory / RunFile 驱动。

- static int[]BuildCrcTable()
  - 构建 CRC-32/MPEG（MSB-first，多项式 0x04C11DB7）查表。

- void CrcUpdate(int b)
  - 把一个字节滚入当前块的 CRC。

- bool Refill()
  - 文件模式下从输入文件填满读缓冲；内存模式或读不到数据返回 false。

- int NextByte()
  - 读下一个输入字节；输入耗尽返回 -1。

- int GetBits(int n)
  - 读取 n（1..24）位；输入耗尽返回 -1。

- int GetBit()
  - 读 1 位。

- int GetInt32()
  - 32 位字段（CRC）分两次读，避免 (1 << 32) 溢出。

- void Emit(int b)
  - 输出一个字节（输出缓冲写满时自动 FlushOut）。

- void FlushOut()
  - 冲出输出缓冲：文件模式写盘（短写标记失败），内存模式累积成块。

- byte[]RunMemory(byte[]data)
  - 内存内解压：解析整个流并拼接输出；数据无效、解码失败或
    输出超过约 2 GB 时返回 null。

- bool RunFile(string srcPath, string dstPath)
  - 流式解压 srcPath 到 dstPath；源或目标打不开、解码失败时
    返回 false（目标文件可能已写出部分内容）。

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
  - 惰性构建 256 项查表；首次调用后缓存到 table。

- static int Compute(byte[]data, int offset, int len, int crc)
  - 从 `crc` 起始对 `data` 计算 CRC-32（首次计算传 0）。

- static int Compute(string data, int offset, int len, int crc)
  - 字符串重载：逐字符取低 8 位后走同一查表。
    
    早期编译器里 string 与 byte[] 布局兼容，把 string 直接传给 byte[]
    形参可用；编译器迭代后该隐式转换会误索引（每个下标都读到首元素，
    且越界），库必须显式拷贝，调用方才不会再踩。

- static int Compute(byte[]data)
  - 对整个 `data` 计算 CRC-32（等价于从 0 起始的完整缓冲区版本）。


## Deflate (class)

纯 Zan 实现的 RFC 1951 DEFLATE —— 不依赖 zlib 或任何外部
依赖，可在所有平台运行。

Inflate 支持解码 stored、固定 Huffman 和动态 Huffman 块。
Deflate 对不可压缩输入输出 stored 块；对可压缩输入，
使用带 LZ77 哈希表匹配器的固定 Huffman 块（级别
1-9 选择哈希链长度；格式与 zlib 默认输出一致，
因此可与任何标准 inflate 实现互通）。

- static int ReadBits(byte[]src, ref int pos, ref int bit, int n, int end)
  - 从位流读 n 位（LSB 优先）；输入耗尽返回 -1，绝不越过 end 读取。

- static int ReadBit(byte[]src, ref int pos, ref int bit, int end)
  - 从位流读 1 位；输入耗尽返回 -1。

- static void AlignByte(ref int bit)
  - 丢弃字节内的位偏移，对齐到下一字节边界（stored 块用）。

- static int[]LengthExtra=new int[]{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  - 长度码 257..285 -> 附加位。

- static int[]LengthBase=new int[]{ 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  - 各长度码的基准长度。

- static int[]DistExtra=new int[]{ 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  - 距离码 0..29 -> 附加位。

- static int[]DistBase=new int[]{ 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  - 各距离码的基准距离。

- static int BuildHuffman(int[]count, int[]symbol, int[]lengths, int n)
  - 根据码长数组构建规范 Huffman 解码表。
    `lengths` 保存每个符号的位长；`n` 为符号总数。
    成功返回 0，码集无效时返回 -1。

- static int DecodeSymbol(int[]count, int[]symbol, byte[]src, ref int pos, ref int bit, int end)
  - 从规范表中解码一个符号。`count`/`symbol` 来自
    BuildHuffman；`src`/`pos`/`bit` 为位流。输入耗尽时
    返回 -1（ReadBit 的 -1 绝不能被当作位参与索引计算）。

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
  - 为当前块构建动态 Huffman 表。成功返回 0，码集无效返回 -1。

- static byte[]Ensure(byte[]buf, ref int used, int need, int maxOut)
  - 确保输出缓冲区能容纳 used+need 字节：超出 maxOut 返回 null，
    否则倍增扩容并拷贝旧内容。

- static byte[]Deflate(byte[]src, int level)
  - 将 `src` 压缩为 DEFLATE 流。`level` 0 原样存储数据
    （最快）；1-9 使用带
    哈希链匹配器的 LZ77 固定 Huffman 压缩（级别越高链越长，压缩比越好，但越慢）。

- static byte[]Store(byte[]src, int offset, int len)
  - 单个 stored 块（BFINAL=1）——最简单合法的流。

- static byte[]FixedCompress(byte[]src, int level)
  - LZ77 + 固定 Huffman。输出一个最终块；字面量和长度/
    距离对用固定表编码。

- static int ChainLength(int level)
  - 压缩级别对应的哈希链最大匹配长度（级别越高链越长）。

- static int BlockInto(byte[]src, int n, int maxChain, byte[]out2, int bitPos)
  - 固定 Huffman 块体：LZ77 哈希链匹配 + 块结束符。从
    `bitPos` 起按位续写（调用方已写好块头），返回结束时的
    位位置。head/prev 每次调用新建，因此匹配从不跨块——
    分块压缩（`DeflateChunker`）正依赖这一点。

- static void EmitLitFixed(byte[]out2, ref int bitPos, int sym)
  - 按固定表编码一个字面量符号（MSB 优先）。

- static void EmitLengthDistance(byte[]out2, ref int bitPos, int length, int dist)
  - 按固定表编码一个长度/距离对（含附加位）。

- static void EmitFixed(int code, byte[]out2, ref int bitPos)
  - 长度符号 `code`（257-285）的固定表编码，MSB 优先。

- static void WriteBits(byte[]out2, ref int bitPos, int v, int n)
  - 从低位起写 n 位（LSB 优先；块头、附加位等非 Huffman 字段用）。
    按整字节打包：寄存器里一次对齐后按字节 OR 进缓冲，替代
    逐位读-改-写——每输出字节 8 次数组往返是本实现的主要 CPU
    去向。写入的位序列与逐位版本完全一致，压缩结果逐字节相同。

- static void WriteBitsMsb(byte[]out2, ref int bitPos, int v, int n)
  - 写入 `v` 的低 `n` 位，最高有效位在前。RFC 1951 中
    Huffman 码按 MSB 优先，而其他字段（块头、附加位、
    LEN/NLEN）按 LSB 优先 —— 因此编码必须走这个函数。
    位序倒装先在寄存器里完成（首个写出的位 = v 的最高
    有效位），再走统一的整字节 LSB 打包。

- static byte[]Trim(byte[]buf, int len)
  - 返回 buf 前 len 字节的拷贝。

- static byte[]ZlibCompress(byte[]src, int level)
  - zlib 流（2 字节头 + adler32 尾部）包裹原始 DEFLATE
    流。大多数 .NET/Java/zip 工具可直接消费。


## DeflateChunker (class)

DEFLATE 分块压缩器：大条目按块压成一条合法的流，内存占用
与数据总量无关。DEFLATE 的块在流里是位连续的，不能按字节
拼接，因此每块从上一块遗留的部分字节继续写；哈希链每块
重建、匹配从不跨块，对 1MB 量级的块压缩比损失可以忽略。
`Push` 每次返回可直接落盘的完整字节（残位留在
内部状态里随下一块续写）；数据写完后调 `Finish`
发射收尾块并冲出最后的残位。与 Inflate 互通：Push+Finish
的输出整体是一次合法的 DEFLATE 流。

- int pending;

- int pendingBits;

- DeflateChunker()
  - 构造从零残位开始的分块压缩器。

- byte[]Push(byte[]chunk, int level)
  - 压缩一块（非收尾块）。返回可落盘的完整字节。

- byte[]Finish()
  - 收尾：发射 BFINAL=1 的空块并冲出最后的残位。

- static byte[]TakeComplete(byte[]out2, int bitPos, DeflateChunker ch)
  - 把位流切成完整字节返回；末尾残位存回 chunker 状态。


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
    标记 isDirectory（或名称以 "/" 结尾）。mode 为 0 时文件取 0644、
    目录取 0755；超过 100 字节的路径按 ustar 规则拆入 prefix 字段，
    无法拆分时截断到 100 字节。归档以两个 512 字节零块结尾。

- static List<TarEntry> Read(byte[]tar)
  - 读取 tar 归档中的所有条目（含数据）。逐块校验头部
    校验和；归档损坏（校验和不符、size 越界）返回 null。普通文件
    与目录照常解析，符号链接/设备等特殊条目 data 为 null、
    isDirectory 为 false。

- static void WriteHeader(byte[]b, int off, TarEntry e)
  - 写出 512 字节 ustar 头部：长路径按「最靠前的 '/' 且
    名称部分 ≤100、前缀 ≤155」拆入 prefix 字段（无法拆分时截断）；
    目录 typeflag '5'、普通文件 '0'；uid/gid/devmajor/devminor 恒 0；
    末尾写校验和。mode 为 0 时按条目类型取默认权限。

- static int HeaderSum(byte[]b, int off)
  - 头部校验和：512 字节求和，校验和字段本身按 8 个
    空格（0x20）计入——与读取端 ParseOctal 的期望一致。

- static bool IsZeroBlock(byte[]b, int off)
  - off 处起 512 字节是否全为零（归档结尾的两个零块）。

- static void WriteStr(byte[]b, int off, string s, int maxLen)
  - 把字符串写入定长字段：超出截断，尾部以 NUL 补齐。

- static string ReadStr(byte[]b, int off, int maxLen)
  - 读取定长字段中 NUL 之前的字符串。

- static void WriteOctal(byte[]b, int off, long v, int fieldLen)
  - 按 POSIX 写八进制字段：右对齐、末字节为 NUL 终止符、
    其余高位补空格；值为 0 时写单个 '0'。

- static int ParseOctal(byte[]b, int off, int fieldLen)
  - 解析八进制字段，忽略非八进制字符（兼容空格填充与
    GNU 的 base-256 形态首字节被当作无效跳过——仅适用于 mode 等
    int 范围内的字段；size 用 `ParseSize`）。

- static long ParseSize(byte[]b, int off)
  - size 字段（12 位八进制）可能超过 int 范围，必须用
    long 累计，否则攻击者把 32 位截断值写进 size 就能绕过边界
    检查。mtime 等大数值字段同样经此解析。

- static int ExtractFile(string tarPath, string destDir)
  - 把 tar 归档流式解包到目录（按 64 KiB 分块读写，不把归档
    整体读入内存，适合数百 MB 的运行时包）。支持 ustar 前缀长名与
    GNU 长名（'L'）条目；符号链接/设备等条目被跳过。返回写出的文件与
    目录数，归档损坏或路径越界时返回 -1。

- static bool SafeName(string name)
  - 归档内路径防逃逸检查：必须是相对路径、无盘符、
    不含 ".." 段。注意 "\\" 仅在段边界处当分隔符参与 ".." 判定，
    普通段内的反斜杠不拒绝——Windows 上会原样成为文件名的一部分。
    非法时返回 false。

- static int LastSlash(string path)
  - 路径中最后一个 '/' 或 '\' 的下标；没有时为 -1。

- static bool WriteMember(nint fp, byte[]buf, string outPath, long size)
  - 把条目数据（size 字节）按 64 KiB 分块写到 outPath；
    打不开目标文件或读写出错返回 false（目标文件可能已截断）。

- static string ReadName(nint fp, byte[]buf, long size)
  - 读取 GNU 长名条目的数据（NUL 结尾的路径）；异常时返回 null。

- static bool SkipBytes(nint fp, byte[]buf, long count)
  - 顺序读入并丢弃 count 字节（含 512 对齐填充）；读到文件尾返回 false。


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
  - 构造文件条目（isDirectory 默认 false，mode/mtime 为 0）。


## Zip (class)

ZIP 归档读写器（PKZIP，无加密，支持 stored + deflate
条目）。可与所有标准解压工具互通（Windows 资源管理器、
.NET ZipArchive、Python zipfile、7-Zip、Info-ZIP 等）。不依赖
zlib 或任何原生库。

- static int SigLocal=0x04034B50;
  - 本地文件头签名 PK\x03\x04

- static int SigCentral=0x02014B50;
  - 中央目录文件头签名 PK\x01\x02

- static int SigEnd=0x06054B50;
  - 中央目录结束签名 PK\x05\x06

- static int MethodStored=0;
  - 存储（不压缩）。

- static int MethodDeflate=8;
  - DEFLATE 压缩。

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
  - 从头扫描本地文件头签名找名称匹配的条目，返回其偏移；找不到返回 -1。

- static bool EndsWithSlash(string s)
  - 名称以 '/' 结尾（目录条目）时为 true。

- static string ReadName(byte[]zip, int off, int len)
  - 从归档缓冲区读取 len 字节的条目名。

- static int ReadU16(byte[]b, int off)
  - 小端读取 16 位无符号。

- static long ReadU32(byte[]b, int off)
  - 小端读取 32 位并以 long 返回（保留无符号值，避免误判为负）。

- static void WriteU16(byte[]b, ref int off, int v)
  - 小端写入 16 位并把 off 前移 2。

- static void WriteU32(byte[]b, ref int off, long v)
  - 小端写入低 32 位并把 off 前移 4。


## ZipEntry (class)

zip 归档中的一个条目。

- public string name;
  - 归档内的路径，使用正斜杠，目录以 "/" 结尾。

- public byte[]data;
  - 文件内容（目录条目为 null）。

- public bool isDirectory;
  - 目录条目为 true。

- public int method;
  - 由 Zip.Read / Zip.Extract 填充：
    压缩方法：0 = stored，8 = deflate。

- public int crc32;
  - 未压缩数据的 CRC-32。

- public int compressedSize;
  - 压缩后字节数。

- public int uncompressedSize;
  - 未压缩字节数。

- ZipEntry(string name)
  - 构造文件条目（isDirectory 默认 false，数据待填）。


## ZipWriter (class)

流式 ZIP 写出器：条目边生成边压缩、边落盘，内存占用与条目
大小无关（分块压缩走 `DeflateChunker`）。与
`Zip` 互通——这里写出的归档 Zip.Read/Extract/
ExtractToDirectory 以及标准解压工具都能读。单条目与整个
归档都 ≤ 4GB（头部是 u32 字段，ZIP64 暂不支持）。用法：

ZipWriter zw = ZipWriter.Open("out.zip");
zw.AddEntry("a.txt", smallBytes, 6);   // 小条目一次写入
zw.BeginEntry("big.xml", 6);           // 大条目分块写入
zw.WriteData(chunk1, chunk1.Length);
zw.WriteData(chunk2, chunk2.Length);
zw.EndEntry();                          // 回填 CRC/尺寸
zw.Close();

中途放弃时调 `Abort` 删掉半成品文件。

- [DllImport("crt", EntryPoint="zan_file_fopen")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern int fclose(nint fp);

- [DllImport("crt")]static extern int fseek(nint fp, int offset, int origin);

- [DllImport("crt")]static extern int ftell(nint fp);

- [DllImport("crt")]static extern long fwrite(string buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="zan_file_remove")]static extern int remove(string path);

- nint fp;
  - 归档文件的 FILE* 句柄；Close/Abort 后为 0。

- string path;
  - 归档文件路径（Abort 删除半成品用）。

- List<long> offsets;

- List<string> names;

- List<int> methods;

- List<long> crcs;

- List<long> csizes;

- List<long> usizes;

- string curName;

- int curMethod;

- int curLevel;

- int curCrc;

- long curCsize;

- long curUsize;

- long curHdrPos;

- DeflateChunker curChunker;

- ZipWriter()
  - 构造空写出器；实例经 `Open` 创建。

- static ZipWriter Open(string path)
  - 创建/截断归档文件。打不开时抛 IOException。

- void AddEntry(string name, byte[]data, int level)
  - 一次性写入整个条目（内容已在内存）。级别 0 存储，
    1-9 deflate；内容 < 64 字节时自动转为存储。

- void BeginEntry(string name, int level)
  - 开始一个分块条目：写出本地头（CRC/尺寸先占位，
    `EndEntry` 时回填）。之后用 `WriteData`
    喂数据。级别 0 表示存储（原样写盘），1-9 分块 deflate。

- void WriteData(byte[]chunk, int len)
  - 向进行中的分块条目追加一块数据。

- void EndEntry()
  - 结束进行中的分块条目：冲出压缩尾块、回填本地头的
    CRC/尺寸、登记中央目录。

- void Close()
  - 写中央目录与 EOCD，关闭文件。之后归档可读。

- void Abort()
  - 关闭并删除半成品归档（导出被取消/出错时用）。

- void WriteHeader(string name, int method, int crc, long csize, long usize)
  - 30 字节本地文件头（不含名称）。签名+版本+标志+方法+时间
    日期+CRC+两尺寸+两长度。

- void BuildHeader(byte[]hdr, ref int off, string name, int method, int crc, long csize, long usize)
  - 把 30 字节本地头字段写进 hdr 自 off 起，并把 off 前移 30（EndEntry 回填时复用）。

- long Tell()
  - 当前写入位置（距文件头的字节偏移）。

- void Seek(int pos)
  - 定位到绝对偏移（回填本地头用）。

- void SeekEnd()
  - 定位到文件尾（继续追加条目）。

- void WriteRaw(byte[]data, int len)
  - 写入 len 字节；写不满抛 IOException。
