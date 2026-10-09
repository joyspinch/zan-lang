# System.Resources

> 源码: `packages/Zan.Resources/src/System/Resources/ResourcePack.zan`


## PackIndexEntry (class)

- byte[]hash;

- int off;

- int size;

- byte[]iv;

- byte[]tag;

- PackIndexEntry(byte[]hash, int off, int size, byte[]iv, byte[]tag)


## ResourceEntry (class)

- string name;

- string data;

- int len;

- ResourceEntry(string name, string data, int len)


## ResourcePack (class)

- string path;

- string masterKey;

- byte[]packId;

- int count;

- List<PackIndexEntry> index;

- static void writeMagic(byte[]hdr)

- static bool checkMagic(string hdr)

- static void storeBE32(byte[]buf, int off, int v)

- static int loadBE32(string buf, int off)

- static void storeBE64(byte[]buf, int off, int v)

- static int loadBE64(string buf, int off)

- static byte[]nameHash(string name)

- static byte[]indexKey(string masterKey, byte[]packId)

- static byte[]entryKey(string masterKey, byte[]packId, byte[]nh)

- static byte[]MixKey(string a, string b, int len)

- static ResourcePack Open(string path, string masterKey, string nHex, string eHex, List<int> err)

- int Count()

- int find(string name)

- bool Contains(string name)

- int SizeOf(string name)

- byte[]Read(string name, List<int> outLen)


## ResourcePackWriter (class)

- [DllImport("crt")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern long fwrite(string buf, long size, long count, nint fp);

- [DllImport("crt")]static extern int fclose(nint fp);

- List<ResourceEntry> entries;

- ResourcePackWriter()

- void Add(string name, string data, int len)

- bool Write(string path, string masterKey, string nHex, string dHex)
