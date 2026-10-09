# System.IO.Compression

> 源码: `packages/Zan.IO/src/System/IO/Compression/BZip2.zan`, `packages/Zan.IO/src/System/IO/Compression/Crc32.zan`, `packages/Zan.IO/src/System/IO/Compression/Deflate.zan`, `packages/Zan.IO/src/System/IO/Compression/GZip.zan`, `packages/Zan.IO/src/System/IO/Compression/Tar.zan`, `packages/Zan.IO/src/System/IO/Compression/Zip.zan`


## BZip2 (class)

- static byte[]Decompress(byte[]data)

- static bool DecompressFile(string srcPath, string dstPath)


## BZip2Decoder (class)

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

- int GetBit()

- int GetInt32()

- void Emit(int b)

- void FlushOut()

- byte[]RunMemory(byte[]data)

- bool RunFile(string srcPath, string dstPath)

- bool Decode()

- bool DecodeBlock(int blockSize)

- bool BuildDecodeTables(int t, int alphaSize)

- int NextSymbol(ref int groupNo, ref int groupPos, ref int gSel, int nSelectors)


## Crc32 (class)

- static int[]table;

- static int Poly=-306674912;

- static int AllOnes=-1;

- static int[]Table()

- static int Compute(byte[]data, int offset, int len, int crc)

- static int Compute(string data, int offset, int len, int crc)

- static int Compute(byte[]data)


## Deflate (class)

- static int ReadBits(byte[]src, ref int pos, ref int bit, int n, int end)

- static int ReadBit(byte[]src, ref int pos, ref int bit, int end)

- static void AlignByte(ref int bit)

- static int[]LengthExtra=new int[]{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

- static int[]LengthBase=new int[]{ 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};

- static int[]DistExtra=new int[]{ 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

- static int[]DistBase=new int[]{ 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

- static int BuildHuffman(int[]count, int[]symbol, int[]lengths, int n)

- static int DecodeSymbol(int[]count, int[]symbol, byte[]src, ref int pos, ref int bit, int end)

- static void FixedTables(int[]litCount, int[]litSymbol, int[]distCount, int[]distSymbol)

- static byte[]Inflate(byte[]src, int offset, int len, int maxOut)

- static byte[]InflateConsumed(byte[]src, int offset, int len, int maxOut, ref int consumed)

- static int DynamicTables(int[]litCount, int[]litSymbol, int[]distCount, int[]distSymbol, byte[]src, ref int pos, ref int bit, int end)

- static byte[]Ensure(byte[]buf, ref int used, int need, int maxOut)

- static byte[]Deflate(byte[]src, int level)

- static byte[]Store(byte[]src, int offset, int len)

- static byte[]FixedCompress(byte[]src, int level)

- static int ChainLength(int level)

- static int BlockInto(byte[]src, int n, int maxChain, byte[]out2, int bitPos)

- static void EmitLitFixed(byte[]out2, ref int bitPos, int sym)

- static void EmitLengthDistance(byte[]out2, ref int bitPos, int length, int dist)

- static void EmitFixed(int code, byte[]out2, ref int bitPos)

- static void WriteBits(byte[]out2, ref int bitPos, int v, int n)

- static void WriteBitsMsb(byte[]out2, ref int bitPos, int v, int n)

- static byte[]Trim(byte[]buf, int len)

- static byte[]ZlibCompress(byte[]src, int level)


## DeflateChunker (class)

- int pending;

- int pendingBits;

- DeflateChunker()

- byte[]Push(byte[]chunk, int level)

- byte[]Finish()

- static byte[]TakeComplete(byte[]out2, int bitPos, DeflateChunker ch)


## GZip (class)

- static byte[]Compress(byte[]data, int level)

- static byte[]Decompress(byte[]src)


## Tar (class)

- [DllImport("crt", EntryPoint="fopen")]static extern nint PlatFopen(string path, string mode);

- [DllImport("crt", EntryPoint="fread")]static extern long PlatFread(byte[]buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fwrite")]static extern long PlatFwrite(byte[]buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fclose")]static extern int PlatFclose(nint fp);

- static byte[]Create(List<TarEntry> entries)

- static List<TarEntry> Read(byte[]tar)

- static void WriteHeader(byte[]b, int off, TarEntry e)

- static int HeaderSum(byte[]b, int off)

- static bool IsZeroBlock(byte[]b, int off)

- static void WriteStr(byte[]b, int off, string s, int maxLen)

- static string ReadStr(byte[]b, int off, int maxLen)

- static void WriteOctal(byte[]b, int off, long v, int fieldLen)

- static int ParseOctal(byte[]b, int off, int fieldLen)

- static long ParseSize(byte[]b, int off)

- static int ExtractFile(string tarPath, string destDir)

- static bool SafeName(string name)

- static int LastSlash(string path)

- static bool WriteMember(nint fp, byte[]buf, string outPath, long size)

- static string ReadName(nint fp, byte[]buf, long size)

- static bool SkipBytes(nint fp, byte[]buf, long count)


## TarEntry (class)

- public string name;

- public byte[]data;

- public bool isDirectory;

- public int mode;

- public long mtime;

- TarEntry(string name)


## Zip (class)

- static int SigLocal=0x04034B50;

- static int SigCentral=0x02014B50;

- static int SigEnd=0x06054B50;

- static int MethodStored=0;

- static int MethodDeflate=8;

- static byte[]Create(List<ZipEntry> entries, int level)

- static List<ZipEntry> Read(byte[]zip)

- static byte[]Extract(byte[]zip, ZipEntry e)

- static int ExtractToDirectory(string zipPath, string destDir)

- static int FindLocal(byte[]zip, string name)

- static bool EndsWithSlash(string s)

- static string ReadName(byte[]zip, int off, int len)

- static int ReadU16(byte[]b, int off)

- static long ReadU32(byte[]b, int off)

- static void WriteU16(byte[]b, ref int off, int v)

- static void WriteU32(byte[]b, ref int off, long v)


## ZipEntry (class)

- public string name;

- public byte[]data;

- public bool isDirectory;

- public int method;

- public int crc32;

- public int compressedSize;

- public int uncompressedSize;

- ZipEntry(string name)


## ZipWriter (class)

- [DllImport("crt", EntryPoint="zan_file_fopen")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern int fclose(nint fp);

- [DllImport("crt")]static extern int fseek(nint fp, int offset, int origin);

- [DllImport("crt")]static extern int ftell(nint fp);

- [DllImport("crt")]static extern long fwrite(string buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="zan_file_remove")]static extern int remove(string path);

- nint fp;

- string path;

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

- static ZipWriter Open(string path)

- void AddEntry(string name, byte[]data, int level)

- void BeginEntry(string name, int level)

- void WriteData(byte[]chunk, int len)

- void EndEntry()

- void Close()

- void Abort()

- void WriteHeader(string name, int method, int crc, long csize, long usize)

- void BuildHeader(byte[]hdr, ref int off, string name, int method, int crc, long csize, long usize)

- long Tell()

- void Seek(int pos)

- void SeekEnd()

- void WriteRaw(byte[]data, int len)
