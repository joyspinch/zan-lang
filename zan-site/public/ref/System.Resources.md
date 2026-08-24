# System.Resources

> 源码: `stdlib/System/Resources/ResourcePack.zan`


## PackIndexEntry (class)

一条已解析的 index 记录：条目名哈希、负载偏移
与大小，以及每条目 IV 和 GCM tag。用一个实体代替五个
按 index 对齐的并行列表。

- byte[]hash;

- int off;

- int size;

- byte[]iv;

- byte[]tag;

- PackIndexEntry(byte[]hash, int off, int size, byte[]iv, byte[]tag)


## ResourceEntry (class)

一个已入队的 pack 条目：名称、原始字节负载与
负载长度。用一个实体代替并行的 name/data/len 列表。

- string name;

- string data;

- int len;

- ResourceEntry(string name, string data, int len)


## ResourcePack (class)

加密资源包读取器。格式见 ResourcePackWriter。
Open() 在提供条目前先校验 index 签名与真实性；
Read() 会重新派生每条目密钥，并通过 GCM tag 拒绝
被篡改的负载。

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
  - 条目名的截断 SHA-256（16 字节）。pack 中只存
    这个哈希，因此无法从文件列出条目名。

- static byte[]indexKey(string masterKey, byte[]packId)

- static byte[]entryKey(string masterKey, byte[]packId, byte[]nh)

- static byte[]MixKey(string a, string b, int len)
  - 由两份 XOR 分片重组密钥，使真实密钥永不
    以单一字面量出现在二进制中。

- static ResourcePack Open(string path, string masterKey, string nHex, string eHex, List<int> err)
  - 打开 pack 并认证其 index。<paramref name="nHex"/>
    与 <paramref name="eHex"/> 是 RSA 公钥模数/指数（十六进制），
    用于校验 index 签名；传 null 则接受未签名的 pack。
    <paramref name="err"/>[0]：0 正常，1 IO/magic 错误，2 认证失败，3 签名错误。

- int Count()

- int find(string name)

- bool Contains(string name)

- int SizeOf(string name)

- byte[]Read(string name, List<int> outLen)
  - 按需解密一个条目（只从磁盘读取该条目）。
    返回明文并设置 <paramref name="outLen"/>[0]；
    若名称未知或数据认证失败，
    则返回 null（outLen 为 -1）。


## ResourcePackWriter (class)

加密资源包（.zrp）写入器。

布局（私有格式，没有主密钥则完全不可读）：
[0..3]   magic（4 个不透明字节，非 ASCII）
[4]      version（混淆后的版本号）
[5..20]  packId —— 16 个随机字节，每个 pack 唯一
[21..32] index 的 IV（12 字节）
[33..48] index 的 GCM tag（16 字节）
[49..52] index 密文长度（BE32）
[53..56] 签名长度（BE32，0 表示无签名）
[...]    index 密文，然后是 RSA 签名，最后是各条目负载

index（条目表）由主密钥与 packId 经 HKDF 派生的密钥做 AES-256-GCM 加密，
再签名（RSASSA-PKCS1-v1_5 / SHA-256），
因此即使攻击者拿到主密钥、却拿不到私钥，
重打包或调换条目的文件也会被拒绝。
条目名只存截断的 SHA-256 哈希；每个条目
用各自 HKDF 派生的密钥加密，因此一个条目密钥泄露
不会波及其他条目或主密钥。

- [DllImport("crt")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern long fwrite(string buf, long size, long count, nint fp);

- [DllImport("crt")]static extern int fclose(nint fp);

- List<ResourceEntry> entries;

- ResourcePackWriter()

- void Add(string name, string data, int len)
  - 入队一个条目。<paramref name="data"/> 是原始字节，
    长度显式传入（Zan 字符串就是字节缓冲区）。

- bool Write(string path, string masterKey, string nHex, string dHex)
  - 写入 pack。<paramref name="masterKey"/> 为 32 字节。
    <paramref name="nHex"/>/<paramref name="dHex"/> 是 RSA 模数与
    私钥指数（十六进制），用于 index 签名；传 null 可跳过签名。
    成功返回 true。
