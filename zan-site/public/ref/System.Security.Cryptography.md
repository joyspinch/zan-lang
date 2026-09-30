# System.Security.Cryptography

> 源码: `packages/Zan.Security/src/System/Security/Cryptography/Aes.zan`, `packages/Zan.Security/src/System/Security/Cryptography/AesGcm.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Base64.zan`, `packages/Zan.Security/src/System/Security/Cryptography/BigInt.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Bits.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Crc32C.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Curve25519.zan`, `packages/Zan.Security/src/System/Security/Cryptography/EcKey.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Ecdsa.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Hex.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Hkdf.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Hmac.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Jwt.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Md5.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Otp.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Pbkdf2.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Rsa.zan`, `packages/Zan.Security/src/System/Security/Cryptography/RsaKey.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sha1.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sha256.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sha512.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sm2.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sm3.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sm4.zan`, `packages/Zan.Security/src/System/Security/Cryptography/X509Certificate.zan`


## Aes (class)

AES（FIPS-197）高级加密标准分组密码。
100% 纯 Zan 自举实现，支持 CBC、ECB 与 CTR 模式，支持 128/192/256 位密钥。
核心循环采用工业级 T-Table（4KB 复合查找表）与寄存器轮转优化，
实现零 C 语言、零 OpenSSL 依赖、零热点循环堆分配的高性能纯原生加密。

- static int[]Sbox;

- static int[]InvSbox;

- static int[]T0;

- static int[]T1;

- static int[]T2;

- static int[]T3;

- static int[]InvT0;

- static int[]InvT1;

- static int[]InvT2;

- static int[]InvT3;

- static bool inited=false;

- static int Xtime(int a)

- static int MulGF(int a, int b)

- static int RotR32(int v, int bits)

- static void InitTables()

- static int[]ExpandKey(string key, int keyLen, List<int> nrOut)

- static void EncryptCore(int[]rk, int nr, string inb, int inOff, byte[]outb, int outOff)

- static void DecryptCore(int[]rk, int nr, string inb, int inOff, byte[]outb, int outOff)

- static byte[]EncryptBlockEcb(string key, int keyLen, string in16)
  - AES-ECB 单块加密（16 字节 → 16 字节）。

- static byte[]DecryptBlockEcb(string key, int keyLen, string in16)
  - AES-ECB 单块解密（16 字节 → 16 字节）。

- static byte[]EncryptCbc(string key, int keyLen, string iv, string data, int len, List<int> outLen)
  - AES-CBC 加密，PKCS#7 填充。返回恰好 outLen[0] 字节的密文。

- static byte[]DecryptCbc(string key, int keyLen, string iv, string data, int len, List<int> outLen)
  - AES-CBC 解密，去除 PKCS#7 填充。常数时间填充校验，防 Padding Oracle 攻击。

- static byte[]CryptCtr(string key, int keyLen, string iv16, string data, int len)
  - AES-CTR 流加密/解密（对称）。计数器按 128 位大端自增。


## AesGcm (class)

100% 纯 Zan 自举实现的 AES-GCM（NIST SP 800-38D）认证加密。
支持 96 位 IV（J0 = IV || 0^31 || 1），支持 128/192/256 位密钥。
严格遵循常数时间认证校验，认证失败绝不产出明文。
零 C 语言、零 OpenSSL 依赖。

- static long load64(string buf, int off)
  - 从 buf[off..] 读取 8 个大端字节到一个 64 位字

- static void store64(byte[]buf, int off, long v)
  - 64 位字按大端写入 buf 的 off 处。

- static List<long> gfmul(List<long> X, List<long> Y)
  - GF(2^128) 乘法：X、Y 按 [hi,lo] 表示；返回 [hi,lo]

- static List<long> ghashBlock(List<long> Y, string buf, int off, int avail, List<long> H)
  - Y = (Y xor block) * H，其中 block 是 buf[off..] 处 16 字节，不足补零

- static List<long> hwGhash(List<long> H, string aad, int aadLen, string data, int dataLen)
  - GHASH 硬件快路径（PCLMUL/PMULL 流式内核）：AAD 段、密文段
    再接长度块在 C 端 SIMD 寄存器流水线一次性完成，避免逐块跨语言与内存拷贝。

- static List<long> ghash(List<long> H, string aad, int aadLen, string data, int dataLen)
  - GHASH：AAD 块、密文块再接长度块，在 GF(2^128) 上逐块累乘 H。

- static void inc32(byte[]ctr)
  - 计数器块的低 32 位（第 12..15 字节，大端）加一。

- static void gctr(string key, int keyLen, string ctr, string data, int dataLen, byte[]outb)
  - GCTR：由 AES(counter) 生成密钥流，与数据异或（原地写入 out）

- static byte[]Encrypt(string key, int keyLen, string iv, string aad, int aadLen, string pt, int ptLen, string tagOut)
  - AES-GCM 加密，使用 96 位（12 字节）IV。密文（与明文等长）写入返回值，
    16 字节 tag 写入 <paramref name="tagOut"/>。参数不合法时返回 null。

- static byte[]Decrypt(string key, int keyLen, string iv, string aad, int aadLen, string ct, int ctLen, string tag, List<int> ok)
  - AES-GCM 解密，使用 96 位 IV。校验通过时返回明文并将 <paramref name="ok"/>[0] 置 -1；
    tag/aad/密文被篡改时返回 null 并将 <paramref name="ok"/>[0] 置 0。
    常数时间防时序攻击，认证失败绝不产出明文。


## Base64 (class)

对原始字节缓冲区的标准 Base64（RFC 4648）编解码。

- static string ALPHA()
  - RFC 4648 标准 Base64 字母表（A–Z、a–z、0–9、`+`、`/`）。

- static string Encode(string buf, int len)
  - 将 <paramref name="len"/> 字节编码为 Base64 文本。

- static int dec(int c)
  - 单个 Base64 字符转 6 位值；非法字符返回 -1。

- static byte[]Decode(string s, List<int> outLen)
  - 将 Base64 文本解码为字节缓冲区。
    <paramref name="outLen"/>[0] 接收解码后的字节数；
    遇到非法字符或填充错误时置为 -1 并返回 null。


## BigInt (class)

任意精度非负整数，使用 32 位 limb 存储
以小端序（limb 0 为最低有效位）存放在 List<long> 中。足以支持
RSA 与 SM2 的比较、加减乘、Montgomery 模幂运算和
模逆。每个 limb 取值在 [0, 2^32) 内，两个 limb 的乘积
作为位模式可放入有符号 64 位 long，所有高字
提取均通过 Bits.Shr64 使用逻辑（而非算术）移位。

- List<long> d;

- static BigInt make(List<long> limbs)
  - 由 limb 列表构造并修剪高位零。

- void trim()
  - 去掉高位零 limb（至少保留一位）。

- int count()
  - limb 个数。

- long limb(int i)
  - 第 i 个 limb（越界返回 0；按 32 位无符号解释）。

- bool isZero()
  - 是否为零。

- static BigInt zero()
  - 值 0。

- static BigInt one()
  - 值 1。

- static BigInt FromBytesBE(string buf, int len)
  - 从大端序字节缓冲区构建 BigInt。
    大端序字节缓冲区 → BigInt（RSA/SM2 整数编码的标准字节序）。

- byte[]ToBytesBE(int outLen)
  - 序列化为恰好 <paramref name="outLen"/> 字节的大端序缓冲区
    （左侧补零；过小时截断高位字节）。
    序列化为无符号大端整数（0 ≤ v < 256^outLen）；过小时高位补零，
    过大时静默截断高位——调用方需保证 outLen 足够容纳。

- static int cmp(BigInt a, BigInt b)
  - 比较：a > b 返回 1，a < b 返回 -1，相等返回 0。

- static BigInt add(BigInt a, BigInt b)
  - 无符号加法。

- static BigInt sub(BigInt a, BigInt b)
  - 无符号减法 a - b；要求 a >= b（否则借位下溢）。

- static BigInt mul(BigInt a, BigInt b)
  - 无符号乘法（教科书 O(n²)，limb 基 2^32）。

- static long montInv(long n0)
  - -n[0]^{-1} mod 2^32（Montgomery 参数 n'，Newton 迭代求出）。

- static BigInt montMul(BigInt a, BigInt b, BigInt n, long n0inv)
  - Montgomery 乘积：a * b * R^{-1} mod n，R = 2^(32k)。要求 a、b < n。

- static BigInt pow2mod(int bits, BigInt n)
  - 通过反复翻倍计算 2^(bits) mod n

- int bitLength()
  - 有效位长（0 计为 1 位）。

- int getBit(int idx)
  - 第 idx 位的值（0 起，低位在前）。

- static BigInt ModPow(BigInt g, BigInt exp, BigInt n)
  - 模幂 base^exp mod n（Montgomery 阶梯，n 必须为奇数——RSA/SM2
    的模数均满足）。要求 base < n；返回值已约减到 [0, n)。

- static BigInt ModInverse(BigInt a, BigInt m)
  - 用二进制扩展欧几里得算法求模逆 a^{-1} mod m（m 无需为素数）。
    不可逆（gcd(a,m) ≠ 1）时返回零。

- static BigInt subSigned(BigInt a, bool aneg, BigInt b, bool bneg, List<bool> outNeg)
  - 有符号减法 a(符号 aneg) - b(符号 bneg)；返回绝对值，符号写入 out[0]

- static List<BigInt> divmod(BigInt a, BigInt b)
  - 教科书式长除法：返回 [商, 余数]

- static BigInt mod(BigInt a, BigInt m)
  - a mod m（经 divmod 取余数）。

- static BigInt setBit(BigInt x, int idx)
  - 返回第 idx 位置 1 的副本。


## Bits (class)

哈希/加密原语共用的位操作辅助函数。
底层直接映射至 CPU 硬件单周期循环移位指令（ROL/ROR）。

- static int Rotl32(int x, int n)
  - 32 位循环左移。

- static int Rotr32(int x, int n)
  - 32 位循环右移。

- static long Shr64(long x, int n)
  - 64 位值的逻辑（无符号）右移。Zan 的 <c>>></c>
    是算术右移，因此需将符号扩展出的高位掩掉。

- static long Rotr64(long x, int n)
  - 64 位循环右移（SHA-512 使用）。


## Crc32C (class)

CRC-32C（Castagnoli，多项式 0x82F63B78）。
工业级现代标准（ZanDB、RocksDB、Kafka、iSCSI、SCTP）。
当输入为连续内存或字节数组时，优先使用底层 NativeMemory.Crc32C 硬件加速。

- static int[]table;

- static int Poly=-2097792136;

- static int AllOnes=-1;

- static int[]Table()

- public static int Compute(byte[]data, int offset, int len, int crc)
  - 对 byte[] 计算 CRC-32C。若硬件支持 SSE4.2 则自动走单周期硬件加速。

- public static int Compute(byte[]data)
  - 对整个 byte[] 计算 CRC-32C。


## Curve25519 (class)

RFC 7748 Curve25519 (X25519) 常数时间 Diffie-Hellman 密钥协商。
采用常数时间 Montgomery 梯子运算，零堆分配、抗侧信道攻击，
为 TLS 1.2 ECDHE 及 TLS 1.3 现代安全密钥协商提供基石。

- static byte[]BasePoint=new byte[]{ 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  - 曲线基点 u = 9 (32 字节小端序)。

- static byte[]ScalarMult(byte[]scalar, byte[]point)
  - X25519 标量乘法：outPoint = scalar * point。
    输入与输出均为 32 字节小端序。标量在内部自动完成 RFC 7748 规范夹紧（Clamping）。

- static byte[]GetPublicKey(byte[]privateKey)
  - 根据 32 字节私钥计算对应的 32 字节 X25519 公钥。

- static byte[]DeriveSharedSecret(byte[]privateKey, byte[]peerPublicKey)
  - 协商共享密钥：计算 privateKey * peerPublicKey。

- static byte[]GenerateKeyPair(out byte[]publicKey)
  - 生成安全 CSPRNG 随机 32 字节私钥并派生公钥。


## EcKey (class)

P-256 EC 私钥（TLS 服务端签名用）：持有 32 字节大端标量 d。
PEM 支持 PKCS#8（BEGIN PRIVATE KEY，AlgorithmIdentifier 为
id-ecPublicKey + prime256v1 命名曲线）与 SEC1（BEGIN EC PRIVATE KEY）。
其他曲线或结构一律拒绝，解析失败返回 null。

- byte[]d;

- byte[]Scalar32()
  - 32 字节私钥标量（拷贝）；未加载返回 null。

- byte[]SignSha256Det(string msg, int msgLen)
  - DER ECDSA-SHA256 签名（RFC 6979 确定性 nonce）。
    无私钥或输入非法返回 null。

- byte[]PublicKey()
  - 派生非压缩公钥点 0x04||X||Y（65 字节）；无私钥返回 null。

- static int ReadLen(string d, List<int> c)

- static int Enter(string d, List<int> c, int tag)

- static EcKey ParseEcPrivateKey(string der, int offset)
  - RFC 5915 ECPrivateKey：SEQUENCE { INTEGER 1, OCTET STRING 私钥,
    [1] 公钥可选 }。仅接受 1 ≤ 私钥 ≤ 32 字节（左侧补零到 32）。

- static EcKey FromPrivatePem(string pem)
  - 从 PEM 私钥加载 P-256：PKCS#8 与 SEC1。非 EC 密钥、
    非 prime256v1 命名曲线或任何结构不符一律返回 null。


## Ecdsa (class)

NIST P-256（secp256r1，RFC 5480）上的 ECDSA，构建于 BigInt
之上，点运算与 SM2 共用同一套 Jacobian 公式（两条曲线均满足
a ≡ -3 mod p）。验签按 FIPS 186-4：e 为 SHA-256 摘要的 256 位整数，
R = u1*G + u2*Q，比较 v = x_R mod n 与 r。签名为 RFC 6979 确定性
k（HMAC-SHA256），不依赖随机源，避免弱 nonce 重放私钥。签名以
DER SEQUENCE{r INTEGER, s INTEGER} 传输（RFC 5480 / TLS 语义），
解析严格：长度必须精确耗尽、INTEGER 定长、1 ≤ r,s < n。

- static string PHEX="FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF";

- static string BHEX="5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B";

- static string NHEX="FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551";

- static string GXHEX="6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296";

- static string GYHEX="4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5";

- static BigInt P;

- static BigInt N;

- static BigInt GX;

- static BigInt GY;

- static bool ready=false;

- static BigInt hb(string h)
  - 十六进制串 -> 大端字节 -> BigInt。

- static void init()
  - 惰性装载 P-256 曲线参数（幂等）。

- static BigInt fromInt(int v)
  - 小整数 -> BigInt（大端 4 字节）。

- static BigInt sliceBE(byte[]src, int off, int len)
  - 从 src 的 off 起取 len 字节切片并按大端读入 BigInt。

- static BigInt fadd(BigInt x, BigInt y)

- static BigInt fsub(BigInt x, BigInt y)

- static BigInt fmul(BigInt x, BigInt y)

- static BigInt fsqr(BigInt x)

- static BigInt finv(BigInt x)

- static BigInt nmod(BigInt x)

- static BigInt nmul(BigInt x, BigInt y)

- static BigInt ninv(BigInt x)

- static List<BigInt> pt(BigInt x, BigInt y, BigInt z)

- static List<BigInt> inf()

- static bool isInf(List<BigInt> Pt)

- static List<BigInt> jdouble(List<BigInt> Pt)
  - Jacobian 倍点（a ≡ -3 (mod P) 的倍点公式，P-256 适用）。

- static List<BigInt> jadd(List<BigInt> Pa, List<BigInt> Qa)
  - Jacobian 点加；退化情形回退倍点或无穷远点。

- static List<BigInt> scalarMul(BigInt k, List<BigInt> Pt)
  - 二进制阶梯标量乘 k*Pt（高位在前）。

- static List<BigInt> toAffine(List<BigInt> Pt)
  - 返回仿射坐标 [x, y, 1]。

- static List<BigInt> gPoint()

- static byte[]PublicKeyOf(byte[]d32){ init}
  - 从 32 字节大端私钥标量派生非压缩公钥 0x04||X||Y（65 字节）。
    标量不在 [1, n-1] 时返回 null。

- static bool ValidPublicKey(byte[]point, int len){ init}
  - 校验非压缩公钥点 0x04||X||Y 确实落在曲线上（y² ≡ x³ - 3x + b
    (mod p)）且坐标 < p。错误格式、无穷远点一律拒绝。

- static bool ParseSigDer(byte[]sig, int sigLen, List<BigInt> outRs){ init}
  - 严格解析 DER ECDSA 签名 SEQUENCE{r INTEGER, s INTEGER}：
    长度精确耗尽、INTEGER 至少 1 字节且不超过 33 字节（32 字节值最多
    一个补符号零）、1 ≤ r,s < n。成功时 r/s 追加到 outRs。

- static int writeInt(byte[]outb, int off, BigInt v)
  - 最小长度 DER INTEGER 编码（ECDSA r/s 恒正），高位为 1 时补 0x00，
    写入 outb 的 off 偏移，返回写入的字节数。

- static byte[]EncodeSigDer(BigInt r, BigInt s, List<int> outLen)
  - r/s BigInt 编码为 DER SEQUENCE 签名（调用方已保证 1 ≤ r,s < n）。

- static byte[]hmac256(byte[]key, byte[]p1, byte[]p2, byte[]p3, byte[]p4)
  - HMAC-SHA256 的 byte[] 便捷封装（消息为若干段一次性拼接）。

- static byte[]SignSha256Det(string msg, int msgLen, byte[]d32, List<int> outLen){ init}
  - RFC 6979（HMAC-SHA256）确定性 nonce 直接完成 ECDSA-P256-SHA256
    签名。msg 为待签数据，d32 为 32 字节大端私钥标量。返回 DER 签名，
    outLen[0] 接收长度。私钥越界返回 null。

- static bool VerifySha256(string msg, int msgLen, byte[]sig, int sigLen, byte[]px32, byte[]py32){ init}
  - 用 P-256 公钥（px32/py32，各 32 字节大端）校验消息的
    DER ECDSA-SHA256 签名。任何格式错误都按无效处理。


## Hex (class)

对原始字节缓冲区进行小写十六进制编解码
（Zan 字符串当作字节数组使用）。

- static string Encode(string buf, int len)
  - 将 <paramref name="buf"/> 的 <paramref name="len"/> 字节
    编码为小写十六进制字符串。预分配 2×len 缓冲按 Span 0 分配直写。

- static string Encode(byte[]buf, int len)
  - 将 byte[] 缓冲区的指定前缀编码为小写十六进制字符串。

- static string Encode(byte[]buf)
  - 将整个 byte[] 缓冲区编码为小写十六进制字符串。

- static int nibble(int c)
  - 单个十六进制字符转数值（0-9、a-f、A-F）；其余字符返回 0（不报错）。

- static byte[]Decode(string hex)
  - 将十六进制字符串解码为
    <c>hex.Length / 2</c> 字节的缓冲区。


## Hkdf (class)

基于 HMAC-SHA256 的 HKDF（RFC 5869）：先提取后扩展的密钥
派生。用于从单一主密钥派生各用途的子密钥，这样
泄漏的子密钥永远不会暴露主密钥或兄弟子密钥。

- static byte[]Extract(string salt, int saltLen, string ikm, int ikmLen)
  - HKDF-Extract：PRK = HMAC-SHA256(salt, ikm)。返回 32 字节。

- static byte[]Expand(string prk, string info, int infoLen, int outLen)
  - HKDF-Expand：派生 <paramref name="outLen"/> 字节（最多 8160），
    输入为 32 字节 PRK 和 context/info 字符串。

- static byte[]Derive(string salt, int saltLen, string ikm, int ikmLen, string info, int infoLen, int outLen)
  - 一步调用的 HKDF：先 Extract 再 Expand。

- static byte[]Extract(byte[]salt, byte[]ikm)
  - HKDF-Extract（字节数组重载）。

- static byte[]Expand(byte[]prk, byte[]info, int outLen)
  - HKDF-Expand（字节数组重载）。

- static byte[]ExpandLabel(byte[]secret, string label, byte[]context, int length)
  - RFC 8446 TLS 1.3 HKDF-Expand-Label。
    标签自动追加 "tls13 " 前缀并按 uint16 length + opaque label + opaque context 编码。

- static byte[]Tls12Prf(byte[]secret, string label, byte[]seed, int outLen)
  - RFC 5246 TLS 1.2 PRF-SHA256 伪随机函数：
    P_SHA256(secret, label + seed)，派生 <paramref name="outLen"/> 字节。


## Hmac (class)

基于本命名空间中 SHA/MD5/SM3 原语的 HMAC（RFC 2104）。
所有支持的哈希均使用 64 字节分组。

- static int SHA256=0;

- static int SHA1=1;

- static int SM3=2;

- static int MD5=3;

- static int digestLen(int algo)
  - 返回算法对应的摘要字节数（SHA256/SM3 为 32，SHA1 为 20，MD5 为 16）。

- static byte[]hashOf(int algo, string buf, int len)
  - 按算法编号分发到底层哈希的 `Hash`（未知编号按 SHA-256 处理）。

- static byte[]Compute(int algo, string key, int keyLen, string msg, int msgLen)
  - 按所选算法计算 HMAC（0=SHA256、1=SHA1、
    2=SM3、3=MD5）。摘要长度等于底层哈希的输出
    长度。

- static byte[]Sha256Mac(string key, int keyLen, string msg, int msgLen)
  - 以 SHA-256 计算 HMAC 的便捷封装。

- static byte[]Sha256(byte[]key, byte[]msg)
  - 以 SHA-256 计算 HMAC（字节数组重载）。

- static byte[]Sha256(byte[]key, int keyLen, byte[]msg, int msgLen)
  - 以 SHA-256 计算 HMAC（带显式长度）。


## Jwt (class)

紧凑 JWS 令牌的 JSON Web Token 辅助函数。支持 HS256 和 RS256；
不安全（`none`）令牌及一切未被请求的算法都会被拒绝。解析
使用调用方提供的 Unix 时间戳，因此 exp/nbf 校验结果是确定性的。
预期的 issuer/audience 为空字符串时，仅禁用各自的校验。

- static int maxTokenSize()
  - 令牌总长上限（128 KiB）。

- static int maxHeaderSize()
  - header 段解码后字节上限（8 KiB）。

- static int maxPayloadSize()
  - payload 段解码后字节上限（64 KiB）。

- static string urlAlphabet()
  - Base64Url 字母表（RFC 4648 §5，`-_` 代替 `+/`，无填充）。

- static int urlDigit(int c)
  - Base64Url 字符 -> 6 位值；非法字符返回 -1。

- static string Base64UrlEncode(string bytes, int len)
  - 将恰好 <paramref name="len"/> 字节编码为无填充的 RFC 4648 Base64Url。

- static byte[]Base64UrlDecode(string text, List<int> outLen)
  - 严格解码无填充的 Base64Url。outLen[0] 为字节数，输入格式错误时为 -1。

- static int dotAt(string token, int from)
  - 从 `from` 起第一个 '.' 的下标；没有则 -1。

- static bool fixedEquals(string actual, int actualLen, string expected, int expectedLen)
  - 常量时间比较（长度差也并入累积差），防签名校验的时序侧信道。

- static bool hasDuplicateKeys(JsonValue node)
  - JSON 树中对象层是否存在重复键（重复键会改变语义，按非法拒绝）。

- static bool readNumericDate(JsonValue node, List<long> result)
  - 读 JWT NumericDate（纯数字串）到 result[0]；非数字或溢出 long 时
    返回 false。

- static JwtToken fail(string error)
  - 构造校验失败的 JwtToken：valid=false，error 为固定文案，无内容。

- static JwtToken parseParts(string token, string algorithm)
  - 拆分三段式令牌并解码：限长、Base64Url、JSON 对象、重复键与
    header.alg 必须恰为 `algorithm`；失败返回带错误文案的 token。

- static string validateClaims(JwtToken token, long now, string issuer, string audience)
  - 按调用方提供的 now 校验 exp/nbf（NumericDate）与 iss/aud
    （空串关闭对应校验）；通过返回 ""，否则返回错误文案。

- static string CreateHs256(string payloadJson, string secret, int secretLen)
  - 由对象形式的 payload JSON 和带字节数的密钥创建 HS256 紧凑 JWT。

- static JwtToken ParseHs256(string token, string secret, int secretLen, long now, string issuer, string audience)
  - 仅解析并校验 HS256，然后对照显式输入验证 exp、nbf、iss 和 aud。

- static string CreateRs256(string payloadJson, RsaKey privateKey)
  - 用 RSA 私钥创建 RS256 紧凑 JWT；密钥无法签名时返回空。

- static JwtToken ParseRs256(string token, RsaKey publicKey, long now, string issuer, string audience)
  - 仅解析并校验 RS256，然后对照显式输入验证 exp、nbf、iss 和 aud。


## JwtToken (class)

已解码的 JWT 及其签名与声明校验结果。

- bool valid;

- string error;

- string headerJson;

- string payloadJson;

- JsonValue header;

- JsonValue payload;

- bool IsValid()
  - 仅当结构、算法、签名、JSON 及所请求的声明全部有效时才为 true。

- string Error()
  - 令牌有效时返回空字符串，否则返回固定的校验错误信息。

- string HeaderJson()
  - 解码后的紧凑 header JSON，解码失败时为空字符串。

- string PayloadJson()
  - 解码后的紧凑 payload JSON，解码失败时为空字符串。

- JsonValue Header()
  - 解析后的 header 对象，解码或 JSON 解析失败时为 null。

- JsonValue Payload()
  - 解析后的声明对象，解码或 JSON 解析失败时为 null。


## Md5 (class)

RFC 1321 MD5 哈希算法。
100% 纯 Zan 自举实现，消除循环内部堆分配，采用原位分块与小端寄存器轮转。
仅供遗留/互操作场景使用；不具有抗碰撞性。

- static int[]K;

- static int[]S;

- static bool inited=false;

- static void InitTables()

- static int RotL32(int x, int n)

- static byte[]Hash(string msg, int len)
  - 计算 MD5；返回新分配的 16 字节摘要。
    MD5 没有任何硬件指令引擎（x86/ARM 均无），纯 Zan 是唯一实现。

- static byte[]Hash(byte[]msg, int len)
  - 计算 byte[] 数据的 MD5；返回新分配的 16 字节摘要。

- static byte[]Hash(byte[]msg)
  - 计算整个 byte[] 的 MD5；返回新分配的 16 字节摘要。

- static string HashToHex(byte[]msg)
  - 计算 byte[] 数据的 MD5 并直接返回 32 位小写十六进制字符串。

- static string HashFile(string path)
  - 计算文件的 MD5 并返回 32 位小写十六进制字符串。若文件不存在抛异常。

- static void wLE(byte[]b, int o, int v)
  - 按小端序把 32 位字 v 写入 b 偏移 o 处的 4 个字节。


## Otp (class)

HOTP（RFC 4226）和 TOTP（RFC 6238）一次性密码。

- static int SHA1=1;
  - 哈希算法常量：SHA-1（摘要 20 字节）。

- static int SHA256=2;
  - 哈希算法常量：SHA-256（摘要 32 字节）。

- static int SHA512=3;
  - 哈希算法常量：SHA-512（摘要 64 字节）。

- static int MAX_VERIFICATION_WINDOW=1000;
  - 验证窗口上限，防止把校验接口变成穷举放大器。

- static int digestLen(int algorithm)
  - 算法对应的摘要长度（字节）。

- static int blockLen(int algorithm)
  - HMAC 分组长度（字节；SHA-512 为 128，其余 64）。

- static byte[]hash(int algorithm, string buf, int length)
  - 按算法常量分发到对应 Sha*.Hash。

- static byte[]hmac(int algorithm, string key, int keyLen, string message, int messageLen)
  - 通用 HMAC（RFC 2104）；超长密钥先哈希归一化到分组长度。
    本文件内部的独立实现，不经 `Hmac`。

- static int modulus(int digits)
  - 10^digits（动态截断模）。

- static string decimalCode(int number, int digits)
  - 验证码左补零到 `digits` 位。

- static void validate(int digits, int algorithm)
  - 校验位数（6-8）与算法常量，非法即抛 ArgumentException。

- static void validateKey(string key, int keyLen)
  - 校验密钥非空且 keyLen 在 [0, key.Length]，非法即抛 ArgumentException。

- static void validateCode(string code, int digits)
  - 校验验证码非空、长度等于 digits 且全为十进制数字，非法即抛 ArgumentException。

- static void validateWindow(int window, string name)
  - 校验验证窗口在 [0, MAX_VERIFICATION_WINDOW]，非法即抛 ArgumentException。

- static string Hotp(string key, int keyLen, long counter, int digits, int algorithm)
  - 为显式的 64 位计数器生成 HOTP 验证码。
    <paramref name="digits"/> 必须在 6 到 8 之间。

- static string Totp(string key, int keyLen, long unixTime, int period, int digits, int algorithm)
  - 当前 Unix 时间与周期的关系即时生成 TOTP 验证码；
    常用 30 秒周期。digits 6-8。

- static bool fixedTimeEquals(string expected, string supplied)
  - 校验失败恒定时间比较（长度差参与累积，不提前返回），
    防止按比较耗时逐位猜码。

- static bool VerifyHotp(string key, int keyLen, long counter, int digits, int algorithm, string code, int lookAhead)
  - 以恒定时间校验当前计数器及其后
    <paramref name="lookAhead"/> 个计数器的 HOTP。

- static bool VerifyTotp(string key, int keyLen, long unixTime, int period, int digits, int algorithm, string code, int window)
  - 以恒定时间在对称漂移窗口内校验 TOTP。
    窗口为 1 时校验前、当前、后三个时间段。


## Pbkdf2 (class)

PBKDF2（RFC 2898 / RFC 8018）口令派生，基于 HMAC-SHA256。

与 `Hmac` 的纯托管路径不同，这里把 iPad/oPad 键块在堆外
备好，之后每轮只有两次 `NativeMemory.Sha256` 原生调用
（内层 64+msgLen、外层 96 字节），中间字节全部经 Span 直读直写，
零托管堆分配——10 万轮约 0.2s，登录专用路径可承受。六万轮是
登录体验与离线爆破成本的折中；调参无需迁移：盐与轮数随结果落库。

- static string Sha256Of(nint p, int len)
  - NativeMemory.Sha256 的可空安全包装：本机无 SHA 硬件路径时经
    GetString 拷出缓冲、走纯 Zan 摘要（仅回退路径多一次 memcpy）。

- static string Derive(string password, int passLen, string salt, int saltLen, int iterations, int dkLen)
  - 派生 dkLen 字节（任意正值，按 32 字节块补齐截取），
    返回原始字节串（GetString 语义，可含任意字节）。

- static string Hex(string password, string salt, int iterations)
  - 口令存取的标准出口：派生 32 字节并以小写 hex 返回
    （64 字符）。


## Rsa (class)

RSA（RFC 8017 / PKCS#1）：原始公/私钥运算以及 PKCS#1
v1.5 和 OAEP 加密填充。大整数运算来自 BigInt；哈希
（OAEP 的 MGF1 用）来自本命名空间。所有缓冲区都显式携带长度，
因为 String.Length 基于 strlen，对二进制数据不安全。

- static int hLen(int algo)
  - 哈希分发：0=SHA-256，1=SHA-1，2=SM3

- static byte[]hashOf(int algo, string buf, int len)
  - 按算法常量分发到对应摘要函数（0=SHA-256，1=SHA-1，2=SM3）。

- static byte[]ModExp(string msg, int mLen, string n, int nLen, string exp, int eLen)
  - 原始 RSA：base^exp mod n。返回 k 字节大端序缓冲区，
    其中 k = <paramref name="nLen"/>。优先走硬件/运行时 Montgomery 模幂加速，
    失败或不支持时平滑回退纯 Zan BigInt。

- static byte[]mgf1(string seed, int seedLen, int maskLen, int algo)
  - MGF1 掩码生成（RFC 8017）

- static byte[]EncryptOaep(string msg, int mLen, string n, int k, string e, int eLen, int algo, string seed)
  - RSA-OAEP 加密（RFC 8017 §7.1.1）。<paramref name="seed"/> 必须
    提供 hLen 个全新随机字节。label 为空。返回 k 字节密文；
    若消息对 k 而言过长（mLen > k - 2*hLen - 2）或 k 过小
    （k < 2*hLen + 2），返回 null，绝不写入越界。

- static byte[]DecryptOaep(string cph, string n, int k, string d, int dLen, int algo, List<int> outLen)
  - RSA-OAEP 解密（RFC 8017 §7.1.2）。返回恢复出的消息；
    <paramref name="outLen"/>[0] 接收其长度，编码结构非法（Y!=0、
    lHash 不符、缺 0x01 分隔符、k 过小）时为 -1。所有失败路径走
    同一条返回，不因错误位置不同而泄露 padding oracle。

- static byte[]EncryptPkcs1(string msg, int mLen, string n, int k, string e, int eLen, string rnd)
  - RSA PKCS#1 v1.5 type-2 加密。<paramref name="rnd"/> 提供
    填充串所需的 (k - mLen - 3) 个非零随机字节。若消息过长
    （mLen > k - 11）或随机数不足，返回 null。

- static byte[]DecryptPkcs1(string cph, string n, int k, string d, int dLen, List<int> outLen)
  - RSA PKCS#1 v1.5 解密。<paramref name="outLen"/>[0] 接收
    消息长度（填充错误时为 -1）。

- static byte[]sha256DigestInfo()
  - SHA-256 的 DigestInfo 前缀（RFC 8017 §9.2 注 1）

- static byte[]emsaPkcs1Sha256(string msg, int mLen, int k)
  - 将 SHA-256(msg) 按 EMSA-PKCS1-v1_5 编码为 k 字节

- static byte[]SignPkcs1Sha256(string msg, int mLen, string n, int k, string d, int dLen)
  - 对 SHA-256(msg) 计算 RSASSA-PKCS1-v1_5 签名。返回
    k 字节的签名（k = 模数字节数）。

- static byte[]SignPkcs1Sha256Crt(string msg, int mLen, string n, int k, string d, int dLen, string p, int pLen, string q, int qLen, string dp, int dpLen, string dq, int dqLen, string qinv, int qinvLen)
  - 基于中国剩余定理 (CRT) 加速计算 RSASSA-PKCS1-v1_5 签名。
    较传统 2048 位单路模幂获得约 4 倍性能提升。

- static byte[]EmsaPssSha256(string msg, int mLen, string n, int k, byte[]salt)
  - RFC 8017 §9.1.1 EMSA-PSS-ENCODE with SHA-256, MGF1-SHA256 and sLen=32.
    emBits is the actual modulus bit length minus one.

- static byte[]SignPssSha256(string msg, int mLen, string n, int k, string d, int dLen, string p, int pLen, string q, int qLen, string dp, int dpLen, string dq, int dqLen, string qinv, int qinvLen, string e, int eLen)
  - RSASSA-PSS signing with a fresh 32-byte CSPRNG salt; null on RNG/key failure.

- static bool VerifyPkcs1Sha256(string msg, int mLen, string sig, string n, int k, string e, int eLen)
  - 校验 RSASSA-PKCS1-v1_5 SHA-256 签名。


## RsaKey (class)

RSA 密钥材料，从 PEM（PKCS#1、PKCS#8 或 SubjectPublicKeyInfo）加载。
现有 `Rsa` 原语使用模数/指数的字节
缓冲区；本类提供可复用的 PEM/DER 解码层，供
签名 HTTP 协议和数据库客户端使用。

- byte[]modulus;

- byte[]publicExponent;

- byte[]privateExponent;

- byte[]prime1;

- byte[]prime2;

- byte[]exponent1;

- byte[]exponent2;

- byte[]coefficient;

- int modulusLength;

- int publicExponentLength;

- int privateExponentLength;

- int prime1Length;

- int prime2Length;

- int exponent1Length;

- int exponent2Length;

- int coefficientLength;

- bool hasCrt;

- RsaKey()

- int ModulusLength()
  - 模数长度（字节），即签名/密文长度。

- bool HasPrivateKey()
  - 是否含私钥指数（public key 时为 false，不能 Sign/Decrypt）。

- bool HasCrt()
  - 是否具有完整 CRT 分量（p, q, dp, dq, qinv），可走 4 倍硬件加速模幂。

- static int Find(string hay, string needle, int from)
  - 从 `from` 起查找子串首次出现，找不到返回 -1。

- static byte[]PemToDer(string pem, List<int> outLen)
  - 剥掉 PEM 封装（BEGIN/END 行与软换行）后按 Base64 解码为 DER；
    找不到 PEM 边界时返回 null 且 outLen[0] = 0。

- static int ReadLength(string der, List<int> cursor)
  - 读 DER TLV 的长度字段（短/长形式），cursor 前移过它。

- static int Enter(string der, List<int> cursor, int tag)
  - 期望 `tag` 并进入其内容，返回内容结束下标；tag 不符返回 -1。

- static byte[]ReadIntegerSafe(string der, List<int> cursor, List<int> outLen)
  - 读 DER INTEGER，去掉前导 0x00 符号字节；非 INTEGER 返回 null。

- static RsaKey ParsePkcs1Private(string der, int offset)
  - 解析 PKCS#1 RSAPrivateKey（包含 version/n/e/d 及 CRT 分量 p/q/dp/dq/qinv）。
    结构不符返回 null。

- static int SkipElement(string der, List<int> cursor)
  - 跳过一个 DER 元素（标签+长度+内容），cursor 停在其末尾。

- static RsaKey ParseSpkiPublic(string der, int offset)
  - 解析 SubjectPublicKeyInfo：跳过 AlgorithmIdentifier，取 BIT STRING
    内的 PKCS#1 公钥。结构不符返回 null。

- static RsaKey ParseCertificatePublic(string der)
  - 解析 X.509 证书：定位 tbsCertificate 的 SubjectPublicKeyInfo 并取其
    RSA 公钥。结构不符返回 null。

- static RsaKey ParsePkcs1Public(string der, int offset)
  - 解析 PKCS#1 RSAPublicKey（n, e）；结构不符返回 null。

- static RsaKey FromPrivatePem(string pem)
  - 从 PEM 私钥加载：支持 PKCS#1（BEGIN RSA PRIVATE KEY）与
    PKCS#8（BEGIN PRIVATE KEY）。解析失败返回 null。

- static RsaKey FromPublicPem(string pem)
  - 从 PEM 公钥加载：支持 SubjectPublicKeyInfo（BEGIN PUBLIC KEY）、
    PKCS#1 公钥及 X.509 证书（BEGIN CERTIFICATE，取其 SubjectPublicKeyInfo）。
    解析失败或非 RSA 密钥返回 null。

- byte[]SignSha256Raw(string message)
  - RSA PKCS#1 v1.5 SHA-256 签名，返回原始字节数组。无私钥时返回 null。
    当私钥包含 CRT 分量时自动走 4 倍硬件加速模幂。

- byte[]SignSha256Raw(string message, int messageLength)

- byte[]SignPssSha256Raw(byte[]message, int messageLength)
  - TLS 1.3 rsa_pss_rsae_sha256: hashes exactly messageLength binary bytes.
    Each signature uses a fresh 32-byte CSPRNG salt; null on failure.

- string SignSha256(string message)
  - RSA PKCS#1 v1.5 SHA-256 签名，返回 Base64。无私钥时返回空串。

- bool VerifySha256(string message, string signatureBase64)
  - 校验 PKCS#1 v1.5 SHA-256 签名（Base64 编码，长度须等于模数长度）。

- string EncryptOaepSha1(string plaintext)
  - RSA-OAEP(SHA-1) 公钥加密，返回 Base64 密文（长度=模数长度）。
    输出上限约 modLen - 42 字节，超长会失败——常规做法是用本方法
    包一个对称密钥再加密正文。


## Sha1 (class)

FIPS 180-4 SHA-1。仅供遗留/互操作场景（如 HMAC-SHA1）使用；
不建议用于新的安全场景。

- [DllImport("crt", EntryPoint="fopen")]static extern nint PlatFopen(string path, string mode);

- [DllImport("crt", EntryPoint="fread")]static extern long PlatFread(byte[]buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fclose")]static extern int PlatFclose(nint fp);

- static byte[]Hash(string msg, int len)
  - 计算 SHA-1；返回 20 字节摘要。

- static byte[]HashBytes(byte[]data, int len)
  - 对字节缓冲区的前 <paramref name="len"/> 字节计算 SHA-1。

- static byte[]HashFile(string path)
  - 流式计算文件的 SHA-1（不把文件整体读入内存），返回
    20 字节摘要；打开失败返回 null。

- static string HashFileHex(string path)
  - 流式计算文件的 SHA-1 并返回小写十六进制串；失败返回空串。

- static string ToHex(byte[]digest)
  - 把摘要编码为小写十六进制串。

- static bool VerifyFile(string path, string expectedHex)
  - 按十六进制摘要校验文件（大小写不敏感）。

- static int[]NewState()
  - 初始链接变量 h0..h4（FIPS 180-4 §5.3.1）。

- static byte[]Finish(int[]h, byte[]tail, int tailLen, long totalBytes)
  - 追加 FIPS 填充（0x80、零、64 位大端比特长度）并返回摘要；
    tail 保存最后一个不完整块（tailLen < 64）。

- static void Block(int[]h, byte[]m, int off)
  - 单个 64 字节块的压缩函数，原地更新 h[0..4]。

- static void wBE(byte[]b, int o, int v)
  - 32 位整数按大端写入 b 的 o 处。


## Sha256 (class)

FIPS 180-4 SHA-256 安全哈希算法。
100% 纯 Zan 自举实现，零热点循环堆分配，原地 64 字节分块与寄存器轮转，
吞吐量对标工业级纯软件实现（-O3 模式下可达 270+ MB/s）。

- static int[]K;

- static bool inited=false;

- static void InitK()

- static byte[]Hash(string msg, int len)
  - 对 <paramref name="len"/> 字节计算 SHA-256，返回 32 字节摘要。

- static void wBE(byte[]buf, int off, int v)
  - 按大端序将 32 位整数写入缓冲区。


## Sha512 (class)

纯 Zan 实现的 SHA-512（FIPS 180-4）。操作以 Zan 有符号 64 位 long
存放的 64 位数据；加法按 mod 2^64 回绕，所有右移都使用
逻辑位移辅助函数 Bits.Shr64 / Bits.Rotr64。消息长度限制为
64 位比特数（128 位长度字段的高 64 位始终为零）。

- static string KHEX="428a2f98d728ae227137449123ef65cdb5c0fbcfec4d3b2fe9b5dba58189dbbc3956c25bf348b53859f111f1b605d019923f82a4af194f9bab1c5ed5da6d8118d807aa98a303024212835b0145706fbe243185be4ee4b28c550c7dc3d5ffb4e272be5d74f27b896f80deb1fe3b1696b19bdc06a725c71235c19bf174cf692694e49b69c19ef14ad2efbe4786384f25e30fc19dc68b8cd5b5240ca1cc77ac9c652de92c6f592b02754a7484aa6ea6e4835cb0a9dcbd41fbd476f988da831153b5983e5152ee66dfaba831c66d2db43210b00327c898fb213fbf597fc7beef0ee4c6e00bf33da88fc2d5a79147930aa72506ca6351e003826f142929670a0e6e7027b70a8546d22ffc2e1b21385c26c9264d2c6dfc5ac42aed53380d139d95b3df650a73548baf63de766a0abb3c77b2a881c2c92e47edaee692722c851482353ba2bfe8a14cf10364a81a664bbc423001c24b8b70d0f89791c76c51a30654be30d192e819d6ef5218d69906245565a910f40e35855771202a106aa07032bbd1b819a4c116b8d2d0c81e376c085141ab532748774cdf8eeb9934b0bcb5e19b48a8391c0cb3c5c95a634ed8aa4ae3418acb5b9cca4f7763e373682e6ff3d6b2b8a3748f82ee5defb2fc78a5636f43172f6084c87814a1f0ab728cc702081a6439ec90befffa23631e28a4506cebde82bde9bef9a3f7b2c67915c67178f2e372532bca273eceea26619cd186b8c721c0c207eada7dd6cde0eb1ef57d4f7fee6ed17806f067aa72176fba0a637dc5a2c898a6113f9804bef90dae1b710b35131c471b28db77f523047d8432caab7b40c724933c9ebe0a15c9bebc431d67c49c100d4c4cc5d4becb3e42b6597f299cfc657e2a5fcb6fab3ad6faec6c44198c4a475817";

- static string HHEX="6a09e667f3bcc908bb67ae8584caa73b3c6ef372fe94f82ba54ff53a5f1d36f1510e527fade682d19b05688c2b3e6c1f1f83d9abfb41bd6b5be0cd19137e2179";

- static List<long> K;

- static List<long> H0;

- static bool ready=false;

- static long word64(string h, int idx)
  - 把十六进制串中第 idx 个 64 位字（16 个 hex 字符）解析为 long。

- static void init()
  - 惰性装载 80 个轮常量 K 与初始哈希 H0（幂等）。

- static long s0(long x)
  - 消息调度 σ0(x) = ROTR1 ^ ROTR8 ^ SHR7。

- static long s1(long x)
  - 消息调度 σ1(x) = ROTR19 ^ ROTR61 ^ SHR6。

- static long bigS0(long x)
  - 压缩函数 Σ0(a) = ROTR28 ^ ROTR34 ^ ROTR39。

- static long bigS1(long x)
  - 压缩函数 Σ1(e) = ROTR14 ^ ROTR18 ^ ROTR41。

- static byte[]Hash(string msg, int len)
  - 对 <paramref name="msg"/> 的前
    <paramref name="len"/> 字节计算 64 字节 SHA-512 摘要。


## Sm2 (class)

SM2 公钥密码（GB/T 32918 / GM/T 0003），基于
标准 256 位素数曲线，构建于 BigInt 之上。提供密钥派生
（d*G）、SM2 数字签名（用 ZA 用户哈希签名/验签）以及
C1C3C2 形式的 SM2 公钥加密。点以 Jacobian 坐标存储，
因此每次标量乘法只需一次模逆。

- static string PHEX="FFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFFFFFFFFFF";

- static string AHEX="FFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFFFFFFFFFC";

- static string BHEX="28E9FA9E9D9F5E344D5A9E4BCF6509A7F39789F515AB8F92DDBCBD414D940E93";

- static string NHEX="FFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFF7203DF6B21C6052B53BBF40939D54123";

- static string GXHEX="32C4AE2C1F1981195F9904466A39C9948FE30BBFF2660BE1715A4589334C74C7";

- static string GYHEX="BC3736A2F4F6779C59BDCEE36B692153D0A9877CC62A474002DF32E52139F0A0";

- static BigInt P;

- static BigInt A;

- static BigInt B;

- static BigInt N;

- static BigInt GX;

- static BigInt GY;

- static bool ready=false;

- static BigInt hb(string h)
  - 十六进制串 -> 大端字节 -> BigInt。

- static void init()
  - 惰性装载 SM2 推荐曲线参数（幂等）。

- static BigInt fromInt(int v)
  - 小整数 -> BigInt（大端 4 字节）。

- static BigInt fadd(BigInt x, BigInt y)
  - (x + y) mod P。

- static BigInt fsub(BigInt x, BigInt y)
  - (x - y) mod P。

- static BigInt fmul(BigInt x, BigInt y)
  - (x * y) mod P。

- static BigInt fsqr(BigInt x)
  - x² mod P。

- static BigInt finv(BigInt x)
  - 模逆 x⁻¹ mod P。

- static BigInt nmod(BigInt x)
  - x mod N。

- static BigInt nsub(BigInt x, BigInt y)
  - (x - y) mod N。

- static BigInt nmul(BigInt x, BigInt y)
  - (x * y) mod N。

- static BigInt ninv(BigInt x)
  - 模逆 x⁻¹ mod N。

- static List<BigInt> pt(BigInt x, BigInt y, BigInt z)
  - 构造 Jacobian 点 [X, Y, Z]。

- static List<BigInt> inf()
  - 无穷远点（Z = 0）。

- static bool isInf(List<BigInt> Pt)
  - 是否无穷远点。

- static List<BigInt> jdouble(List<BigInt> Pt)
  - Jacobian 倍点（本曲线 a ≡ -3 (mod P) 的倍点公式）。

- static List<BigInt> jadd(List<BigInt> Pa, List<BigInt> Qa)
  - Jacobian 点加；X 相同且 Y 相同回退为倍点，Y 相反回退为无穷远点。

- static List<BigInt> scalarMul(BigInt k, List<BigInt> Pt)
  - 二进制阶梯标量乘 k*Pt（从无穷远点开始，高位在前）。

- static List<BigInt> toAffine(List<BigInt> Pt)
  - 返回仿射坐标 [x, y]；若为无穷远点（Z==0）则返回类似 null 的 inf()

- static List<BigInt> gPoint()
  - 基点 G（仿射，Z = 1）。

- static List<BigInt> PublicKey(BigInt d){ init}
  - 从私钥标量派生公钥点（d*G）。
    返回仿射坐标 [x, y]，各 32 字节 BigInt。

- static void writeBE32(string dst, int off, BigInt v)
  - 把 v 的大端 32 字节写入 dst 的 off 处（Zan string 按字节写入）。

- static BigInt computeE(string id, int idLen, string msg, int msgLen, BigInt px, BigInt py){ init}
  - SM2 签名使用的基于 SM3 的 ZA||M 摘要 e（默认 id
    "1234567812345678" 由调用方以字节形式提供）。

- static void Sign(BigInt e, BigInt d, BigInt k, string outSig){ init}
  - SM2 签名。e 为 ZA||M 摘要；k 为每次签名使用的秘密数，
    取值 [1, n-1]。将 r 写入 out[0..31]，s 写入 out[32..63]。

- static bool Verify(BigInt e, BigInt px, BigInt py, BigInt r, BigInt s){ init}
  - 用公钥 (px,py) 校验摘要 e 上的 SM2 签名 (r,s)。
    有效时返回 true。

- static byte[]kdf(string z, int zLen, int klen)
  - GB/T 32918.4 中的 KDF

- static byte[]Encrypt(string msg, int msgLen, BigInt px, BigInt py, BigInt k, List<int> outLen){ init}
  - SM2 公钥加密（C1C3C2，C1 带 0x04 前缀）。
    k 为每条消息的秘密数。outLen[0] 接收密文长度。

- static byte[]Decrypt(string cph, int cphLen, BigInt d, List<int> outLen){ init}
  - 对 C1C3C2 密文（C1 带 0x04 前缀）进行 SM2 解密。返回
    明文；outLen[0] 接收其长度（C3 校验失败时为 -1）。

- static byte[]sliceBE(string src, int off)
  - 从 src 的 off 起读 32 字节（供 BigInt.FromBytesBE）。


## Sm3 (class)

GM/T 0004-2012 SM3（中国国家商用密码哈希算法），256 位输出。
100% 纯 Zan 自举实现，消除循环内堆分配，采用原位分块与布尔置换优化。

- static int RotL32(int x, int n)

- static int P0(int x)
  - 布尔置换 P0(x) = x ^ ROTL9(x) ^ ROTL17(x)

- static int P1(int x)
  - 布尔置换 P1(x) = x ^ ROTL15(x) ^ ROTL23(x)

- static byte[]Hash(string msg, int len)
  - 计算 SM3；返回新分配的 32 字节摘要。

- static void wBE(byte[]b, int o, int v)
  - 按大端序把 32 位字 v 写入 b 偏移 o 处的 4 个字节。


## Sm4 (class)

GB/T 32907-2016 SM4（中国国家分组密码标准）：128 位分组，128 位密钥，32 轮迭代。
100% 纯 Zan 自举实现，提供单分组 ECB 以及 CBC 模式。
消除所有堆分配，采用原位寄存器轮转与零 C 依赖。

- static int[]Sbox;

- static int[]CK;

- static bool inited=false;

- static int RotL32(int x, int n)

- static void InitTables()

- static int Tau(int a)
  - 非线性变换 τ：32 位字逐字节过 S 盒

- static int LTrans(int b)
  - 轮函数线性变换 L：B ^ B<<<2 ^ B<<<10 ^ B<<<18 ^ B<<<24

- static int LpTrans(int b)
  - 密钥扩展线性变换 L'：B ^ B<<<13 ^ B<<<23

- static int[]KeySchedule(string key, bool forDecrypt)
  - 派生 32 个轮密钥

- static void CryptBlock(string in16, int inOff, byte[]out16, int outOff, int[]rk)
  - 单分组 32 轮加解密核心（零堆分配）

- static byte[]EncryptBlock(string key, string in16)
  - 加密单个 16 字节分组。返回新的 16 字节缓冲区。

- static byte[]DecryptBlock(string key, string in16)
  - 解密单个 16 字节分组。返回新的 16 字节缓冲区。

- static byte[]EncryptCbc(string key, string iv, string data, int len, List<int> outLen)
  - 带 PKCS#7 填充的 SM4-CBC 加密。

- static byte[]DecryptCbc(string key, string iv, string data, int len, List<int> outLen)
  - SM4-CBC 解密，去除 PKCS#7 填充。常数时间校验填充。


## X509Certificate (class)

纯 Zan 原生实现的 X.509 证书解析与验证器。
基于 RFC 5280 ASN.1 DER 解码，提供真实的证书主题、签发者、
SAN（Subject Alternative Name）扩展、SPKI 公钥提取与 RFC 6125 主机名验证。
零 C 语言外部依赖，拒绝任何形式的硬编码与 fake mock。

- string subject;

- string issuer;

- string subjectCN;

- string issuerCN;

- List<string> dnsNames;

- List<string> ipAddresses;

- byte[]spki;

- int spkiLen;

- byte[]rawDer;

- int rawDerLen;

- X509Certificate()

- byte[]RawDer()
  - 原始 DER 字节数据

- int RawDerLen()

- string Subject()
  - 证书主题（Subject oneline 形如 /CN=.../O=...）

- string Issuer()
  - 证书签发者（Issuer oneline 形如 /CN=.../O=...）

- string SubjectCN()
  - 证书主题通用名（CN）

- string IssuerCN()
  - 签发者通用名（CN）

- List<string> DnsNames()
  - 主题备用名称（SAN）中的 DNS 列表

- List<string> IpAddresses()
  - 主题备用名称（SAN）中的 IP 列表

- byte[]Spki()
  - 原始 DER 编码的 SubjectPublicKeyInfo（SPKI）字节缓冲

- int SpkiLength()
  - SPKI 长度（字节数）

- string SpkiSha256Pin()
  - 计算 SPKI 的 SHA-256 摘要并编码为 Base64（即公钥固定 Pinning）。

- static bool StrEqualsIgnoreCase(string a, string b)

- static string DnsCanonical(string s)
  - DNS 主机名/SAN 模式的受限规范化（IDNA 边界）：仅接受 LDH+点 的
    ASCII 域名。非 ASCII（U-label）拒绝——Unicode 归一与同形异义字符
    防护超出本栈能力，应用层必须先 to-ASCII 成 A-label（xn--）再传入；
    空标签（前导点/连续点/孤立点）拒绝；嵌入 NUL 由调用方拒绝；恰好
    一个尾随根点（"example.com."）剥除——它是同一 FQDN 的等价拼写，
    不是通配或子域边界。返回 null 表示拒绝。

- static bool MatchPattern(string pattern, string host)

- static string IpSanText(byte[]der, int off, int len)
  - iPAddress SAN 条目转规范文本：IPv4 点分十进制，IPv6 采用 RFC 5952
    式压缩小写十六进制。两侧（证书 SAN 与主机字面量）比较前都走同一
    规范形，文本相等即地址字节相等，压缩/大小写/前导零差异不再误判。

- static string HexGroup(int v)

- static string FormatIPv6Canonical(byte[]b, int off)
  - 16 字节 IPv6 转 RFC 5952 式规范文本：小写十六进制、前导零抑制、
    最长零串压缩（≥2 组才压，并列取最左）。IPv4-mapped 地址同样输出
    纯十六进制形态——与 v4 SAN（点分形态）不会跨族碰撞。

- static bool ParseIPv4Into(string h, int start, int len, byte[]out4)
  - 解析 [start, start+len) 的点分 IPv4（恰 4 段、每段 1-3 位十进制
    0-255）到 out4。越界、空段、多余段一律拒绝。

- static bool ParseIPv6ToBytes(string h, byte[]out16)
  - 严格解析 IPv6 文本为 16 字节：支持一处 :: 压缩与末尾嵌入点分
    IPv4；拒绝 %zone、三冒号、多个压缩点、空 group、越界段与组数
    溢出。

- static string CanonicalizeHostIp(string host)
  - 把主机名形态的 IP 字面量转成与 SAN 存储一致的规范文本；不是合法
    IP 字面量（含 %zone、畸形 v6、越界 v4）返回 null，调用方按不匹配
    处理（fail-closed）。

- bool VerifyHost(string host)
  - 依据 RFC 6125 标准验证给定的主机名（域名或 IP 字面量）是否匹配该证书。
    优先匹配 SAN（dNSName / iPAddress）；若无 SAN 则回退至 Subject CN。

- static int ReadLen(byte[]d, List<int> cur)

- static int Enter(byte[]d, List<int> cur, int tag)

- static void Skip(byte[]d, List<int> cur)

- static string ReadString(byte[]d, int start, int len)

- static string ParseName(byte[]d, List<int> cur, List<string> outCN)

- byte[]tbs;

- byte[]signature;

- byte[]rsaN;

- byte[]rsaE;

- int rsaNLen;

- int rsaELen;

- bool hasEcKey;

- byte[]ecX;

- byte[]ecY;

- bool sigIsEcdsa;

- int issuerNameStart;

- int issuerNameLen;

- int subjectNameStart;

- int subjectNameLen;

- bool isCa;

- bool hasPathLen;

- int pathLenConstraint;

- bool keyCertSign;

- bool digitalSignature;

- bool serverAuth;

- bool hasKeyUsage;

- bool hasEku;

- bool hasSan;

- long notBefore;

- long notAfter;

- static int Tlv(byte[]d, int bound, List<int> p, int tag)

- static int Digits(byte[]d, int p, int count)

- static long TimeValue(byte[]d, int bound, List<int> p)

- static bool Oid(byte[]d, int start, int len, int a, int b, int c)

- static bool SigAlgorithm(byte[]d, int bound, List<int> p, List<int> isEcdsa)

- static X509Certificate ParseForTls(byte[]der, int len)

- static string SafeCommonName(byte[]d, int start, int len)

- bool VerifySignedBy(X509Certificate issuerCert)

- bool VerifyHandshakeSignature(byte[]data, byte[]sig)

- bool VerifyEcdsaSha256(byte[]data, byte[]sig)

- bool VerifyPssSha256(byte[]data, byte[]sig)

- bool ValidNow()

- bool CanSignCertificates()

- bool CanServeTls()

- bool HasPathLen()
  - basicConstraints 是否携带 pathLenConstraint（路径构建时求值）

- int PathLenConstraint()
  - pathLenConstraint 值：该 CA 下方的非自签发中间 CA 数上限

- bool HasEcKey()
  - SPKI 是否携带 P-256 EC 公钥

- bool SigIsEcdsa()
  - 本证书签名算法是否 ecdsa-with-SHA256（否则 sha256WithRSAEncryption）

- static X509Certificate Parse(byte[]der, int len)
  - 从 DER 字节数组解析 X.509 证书。

- static X509Certificate ParseDisplay(byte[]der, int len)

- static X509Certificate FromPem(string pem)
  - 从 PEM 格式字符串加载并解析 X.509 证书。

- static X509Certificate FromPemFile(string path)
  - 从 PEM 文件读取并解析 X.509 证书。
