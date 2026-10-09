# System.Security.Cryptography

> 源码: `packages/Zan.Security/src/System/Security/Cryptography/Aes.zan`, `packages/Zan.Security/src/System/Security/Cryptography/AesGcm.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Base64.zan`, `packages/Zan.Security/src/System/Security/Cryptography/BigInt.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Bits.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Crc32C.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Curve25519.zan`, `packages/Zan.Security/src/System/Security/Cryptography/EcKey.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Ecdsa.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Hex.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Hkdf.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Hmac.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Jwt.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Md5.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Otp.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Pbkdf2.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Rsa.zan`, `packages/Zan.Security/src/System/Security/Cryptography/RsaKey.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sha1.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sha256.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sha512.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sm2.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sm3.zan`, `packages/Zan.Security/src/System/Security/Cryptography/Sm4.zan`, `packages/Zan.Security/src/System/Security/Cryptography/X509Certificate.zan`


## Aes (class)

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

- static bool RequireHardware=false;

- static int Xtime(int a)

- static int MulGF(int a, int b)

- static int RotR32(int v, int bits)

- static void InitTables()

- static int[]ExpandKey(string key, int keyLen, List<int> nrOut)

- static void EncryptCore(int[]rk, int nr, string inb, int inOff, byte[]outb, int outOff)

- static void DecryptCore(int[]rk, int nr, string inb, int inOff, byte[]outb, int outOff)

- static byte[]EncryptBlockEcb(string key, int keyLen, string in16)

- static byte[]DecryptBlockEcb(string key, int keyLen, string in16)

- static byte[]EncryptCbc(string key, int keyLen, string iv, string data, int len, List<int> outLen)

- static byte[]DecryptCbc(string key, int keyLen, string iv, string data, int len, List<int> outLen)

- static byte[]CryptCtr(string key, int keyLen, string iv16, string data, int len)


## AesGcm (class)

- static long load64(string buf, int off)

- static void store64(byte[]buf, int off, long v)

- static List<long> gfmul(List<long> X, List<long> Y)

- static List<long> ghashBlock(List<long> Y, string buf, int off, int avail, List<long> H)

- static List<long> hwGhash(List<long> H, string aad, int aadLen, string data, int dataLen)

- static List<long> ghash(List<long> H, string aad, int aadLen, string data, int dataLen)

- static void inc32(byte[]ctr)

- static void gctr(string key, int keyLen, string ctr, string data, int dataLen, byte[]outb)

- static byte[]Encrypt(string key, int keyLen, string iv, string aad, int aadLen, string pt, int ptLen, string tagOut)

- static byte[]Decrypt(string key, int keyLen, string iv, string aad, int aadLen, string ct, int ctLen, string tag, List<int> ok)


## Base64 (class)

- static string ALPHA()

- static string Encode(string buf, int len)

- static int dec(int c)

- static byte[]Decode(string s, List<int> outLen)


## BigInt (class)

- List<long> d;

- static BigInt make(List<long> limbs)

- void trim()

- int count()

- long limb(int i)

- bool isZero()

- static BigInt zero()

- static BigInt one()

- static BigInt FromBytesBE(string buf, int len)

- byte[]ToBytesBE(int outLen)

- static int cmp(BigInt a, BigInt b)

- static BigInt add(BigInt a, BigInt b)

- static BigInt sub(BigInt a, BigInt b)

- static BigInt mul(BigInt a, BigInt b)

- static long montInv(long n0)

- static BigInt montMul(BigInt a, BigInt b, BigInt n, long n0inv)

- static BigInt pow2mod(int bits, BigInt n)

- int bitLength()

- int getBit(int idx)

- static BigInt ModPow(BigInt g, BigInt exp, BigInt n)

- static BigInt ModInverse(BigInt a, BigInt m)

- static BigInt subSigned(BigInt a, bool aneg, BigInt b, bool bneg, List<bool> outNeg)

- static List<BigInt> divmod(BigInt a, BigInt b)

- static BigInt mod(BigInt a, BigInt m)

- static BigInt setBit(BigInt x, int idx)


## Bits (class)

- static int Rotl32(int x, int n)

- static int Rotr32(int x, int n)

- static long Shr64(long x, int n)

- static long Rotr64(long x, int n)


## Crc32C (class)

- static int[]table;

- static int Poly=-2097792136;

- static int AllOnes=-1;

- static int[]Table()

- public static int Compute(byte[]data, int offset, int len, int crc)

- public static int Compute(byte[]data)


## Curve25519 (class)

- static byte[]BasePoint=new byte[]{ 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

- static byte[]ScalarMult(byte[]scalar, byte[]point)

- static byte[]GetPublicKey(byte[]privateKey)

- static byte[]DeriveSharedSecret(byte[]privateKey, byte[]peerPublicKey)

- static byte[]GenerateKeyPair(out byte[]publicKey)


## EcKey (class)

- byte[]d;

- byte[]Scalar32()

- byte[]SignSha256Det(string msg, int msgLen)

- byte[]PublicKey()

- static int ReadLen(string d, List<int> c)

- static int Enter(string d, List<int> c, int tag)

- static EcKey ParseEcPrivateKey(string der, int offset)

- static EcKey FromPrivatePem(string pem)


## Ecdsa (class)

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

- static void init()

- static BigInt fromInt(int v)

- static BigInt sliceBE(byte[]src, int off, int len)

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

- static List<BigInt> jadd(List<BigInt> Pa, List<BigInt> Qa)

- static List<BigInt> scalarMul(BigInt k, List<BigInt> Pt)

- static List<BigInt> toAffine(List<BigInt> Pt)

- static List<BigInt> gPoint()

- static byte[]PublicKeyOf(byte[]d32){ init}

- static bool ValidPublicKey(byte[]point, int len){ init}

- static bool ParseSigDer(byte[]sig, int sigLen, List<BigInt> outRs){ init}

- static int writeInt(byte[]outb, int off, BigInt v)

- static byte[]EncodeSigDer(BigInt r, BigInt s, List<int> outLen)

- static byte[]hmac256(byte[]key, byte[]p1, byte[]p2, byte[]p3, byte[]p4)

- static byte[]SignSha256Det(string msg, int msgLen, byte[]d32, List<int> outLen){ init}

- static bool VerifySha256(string msg, int msgLen, byte[]sig, int sigLen, byte[]px32, byte[]py32){ init}


## Hex (class)

- static string Encode(string buf, int len)

- static string Encode(byte[]buf, int len)

- static string Encode(byte[]buf)

- static int nibble(int c)

- static byte[]Decode(string hex)


## Hkdf (class)

- static byte[]Extract(string salt, int saltLen, string ikm, int ikmLen)

- static byte[]Expand(string prk, string info, int infoLen, int outLen)

- static byte[]Derive(string salt, int saltLen, string ikm, int ikmLen, string info, int infoLen, int outLen)

- static byte[]Extract(byte[]salt, byte[]ikm)

- static byte[]Expand(byte[]prk, byte[]info, int outLen)

- static byte[]ExpandLabel(byte[]secret, string label, byte[]context, int length)

- static byte[]Tls12Prf(byte[]secret, string label, byte[]seed, int outLen)


## Hmac (class)

- static int SHA256=0;

- static int SHA1=1;

- static int SM3=2;

- static int MD5=3;

- static int digestLen(int algo)

- static byte[]hashOf(int algo, string buf, int len)

- static byte[]Compute(int algo, string key, int keyLen, string msg, int msgLen)

- static byte[]Sha256Mac(string key, int keyLen, string msg, int msgLen)

- static byte[]Sha256(byte[]key, byte[]msg)

- static byte[]Sha256(byte[]key, int keyLen, byte[]msg, int msgLen)


## Jwt (class)

- static int maxTokenSize()

- static int maxHeaderSize()

- static int maxPayloadSize()

- static string urlAlphabet()

- static int urlDigit(int c)

- static string Base64UrlEncode(string bytes, int len)

- static byte[]Base64UrlDecode(string text, List<int> outLen)

- static int dotAt(string token, int from)

- static bool fixedEquals(string actual, int actualLen, string expected, int expectedLen)

- static bool hasDuplicateKeys(JsonValue node)

- static bool readNumericDate(JsonValue node, List<long> result)

- static JwtToken fail(string error)

- static JwtToken parseParts(string token, string algorithm)

- static string validateClaims(JwtToken token, long now, string issuer, string audience)

- static string CreateHs256(string payloadJson, string secret, int secretLen)

- static JwtToken ParseHs256(string token, string secret, int secretLen, long now, string issuer, string audience)

- static string CreateRs256(string payloadJson, RsaKey privateKey)

- static JwtToken ParseRs256(string token, RsaKey publicKey, long now, string issuer, string audience)


## JwtToken (class)

- bool valid;

- string error;

- string headerJson;

- string payloadJson;

- JsonValue header;

- JsonValue payload;

- bool IsValid()

- string Error()

- string HeaderJson()

- string PayloadJson()

- JsonValue Header()

- JsonValue Payload()


## Md5 (class)

- static int[]K;

- static int[]S;

- static bool inited=false;

- static void InitTables()

- static int RotL32(int x, int n)

- static byte[]Hash(string msg, int len)

- static byte[]Hash(byte[]msg, int len)

- static byte[]Hash(byte[]msg)

- static string HashToHex(byte[]msg)

- static string HashFile(string path)

- static void wLE(byte[]b, int o, int v)


## Otp (class)

- static int SHA1=1;

- static int SHA256=2;

- static int SHA512=3;

- static int MAX_VERIFICATION_WINDOW=1000;

- static int digestLen(int algorithm)

- static int blockLen(int algorithm)

- static byte[]hash(int algorithm, string buf, int length)

- static byte[]hmac(int algorithm, string key, int keyLen, string message, int messageLen)

- static int modulus(int digits)

- static string decimalCode(int number, int digits)

- static void validate(int digits, int algorithm)

- static void validateKey(string key, int keyLen)

- static void validateCode(string code, int digits)

- static void validateWindow(int window, string name)

- static string Hotp(string key, int keyLen, long counter, int digits, int algorithm)

- static string Totp(string key, int keyLen, long unixTime, int period, int digits, int algorithm)

- static bool fixedTimeEquals(string expected, string supplied)

- static bool VerifyHotp(string key, int keyLen, long counter, int digits, int algorithm, string code, int lookAhead)

- static bool VerifyTotp(string key, int keyLen, long unixTime, int period, int digits, int algorithm, string code, int window)


## Pbkdf2 (class)

- static string Sha256Of(nint p, int len)

- static string Derive(string password, int passLen, string salt, int saltLen, int iterations, int dkLen)

- static string Hex(string password, string salt, int iterations)


## Rsa (class)

- static bool RequireHardware=false;

- static int hLen(int algo)

- static byte[]hashOf(int algo, string buf, int len)

- static byte[]ModExp(string msg, int mLen, string n, int nLen, string exp, int eLen)

- static byte[]mgf1(string seed, int seedLen, int maskLen, int algo)

- static byte[]EncryptOaep(string msg, int mLen, string n, int k, string e, int eLen, int algo, string seed)

- static byte[]DecryptOaep(string cph, string n, int k, string d, int dLen, int algo, List<int> outLen)

- static byte[]EncryptPkcs1(string msg, int mLen, string n, int k, string e, int eLen, string rnd)

- static byte[]DecryptPkcs1(string cph, string n, int k, string d, int dLen, List<int> outLen)

- static byte[]sha256DigestInfo()

- static byte[]emsaPkcs1Sha256(string msg, int mLen, int k)

- static byte[]SignPkcs1Sha256(string msg, int mLen, string n, int k, string d, int dLen)

- static byte[]SignPkcs1Sha256Crt(string msg, int mLen, string n, int k, string d, int dLen, string p, int pLen, string q, int qLen, string dp, int dpLen, string dq, int dqLen, string qinv, int qinvLen)

- static byte[]EmsaPssSha256(string msg, int mLen, string n, int k, byte[]salt)

- static byte[]SignPssSha256(string msg, int mLen, string n, int k, string d, int dLen, string p, int pLen, string q, int qLen, string dp, int dpLen, string dq, int dqLen, string qinv, int qinvLen, string e, int eLen)

- static bool VerifyPkcs1Sha256(string msg, int mLen, string sig, string n, int k, string e, int eLen)


## RsaKey (class)

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

- bool HasPrivateKey()

- bool HasCrt()

- static int Find(string hay, string needle, int from)

- static byte[]PemToDer(string pem, List<int> outLen)

- static int ReadLength(string der, List<int> cursor)

- static int Enter(string der, List<int> cursor, int tag)

- static byte[]ReadIntegerSafe(string der, List<int> cursor, List<int> outLen)

- static RsaKey ParsePkcs1Private(string der, int offset)

- static int SkipElement(string der, List<int> cursor)

- static RsaKey ParseSpkiPublic(string der, int offset)

- static RsaKey ParseCertificatePublic(string der)

- static RsaKey ParsePkcs1Public(string der, int offset)

- static RsaKey FromPrivatePem(string pem)

- static RsaKey FromPublicPem(string pem)

- byte[]SignSha256Raw(string message)

- byte[]SignSha256Raw(string message, int messageLength)

- byte[]SignPssSha256Raw(byte[]message, int messageLength)

- string SignSha256(string message)

- bool VerifySha256(string message, string signatureBase64)

- string EncryptOaepSha1(string plaintext)


## Sha1 (class)

- [DllImport("crt", EntryPoint="fopen")]static extern nint PlatFopen(string path, string mode);

- [DllImport("crt", EntryPoint="fread")]static extern long PlatFread(byte[]buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fclose")]static extern int PlatFclose(nint fp);

- static byte[]Hash(string msg, int len)

- static byte[]HashBytes(byte[]data, int len)

- static byte[]HashFile(string path)

- static string HashFileHex(string path)

- static string ToHex(byte[]digest)

- static bool VerifyFile(string path, string expectedHex)

- static int[]NewState()

- static byte[]Finish(int[]h, byte[]tail, int tailLen, long totalBytes)

- static void Block(int[]h, byte[]m, int off)

- static void wBE(byte[]b, int o, int v)


## Sha256 (class)

- static int[]K;

- static bool inited=false;

- static void InitK()

- static byte[]Hash(string msg, int len)

- static void wBE(byte[]buf, int off, int v)


## Sha512 (class)

- static string KHEX="428a2f98d728ae227137449123ef65cdb5c0fbcfec4d3b2fe9b5dba58189dbbc3956c25bf348b53859f111f1b605d019923f82a4af194f9bab1c5ed5da6d8118d807aa98a303024212835b0145706fbe243185be4ee4b28c550c7dc3d5ffb4e272be5d74f27b896f80deb1fe3b1696b19bdc06a725c71235c19bf174cf692694e49b69c19ef14ad2efbe4786384f25e30fc19dc68b8cd5b5240ca1cc77ac9c652de92c6f592b02754a7484aa6ea6e4835cb0a9dcbd41fbd476f988da831153b5983e5152ee66dfaba831c66d2db43210b00327c898fb213fbf597fc7beef0ee4c6e00bf33da88fc2d5a79147930aa72506ca6351e003826f142929670a0e6e7027b70a8546d22ffc2e1b21385c26c9264d2c6dfc5ac42aed53380d139d95b3df650a73548baf63de766a0abb3c77b2a881c2c92e47edaee692722c851482353ba2bfe8a14cf10364a81a664bbc423001c24b8b70d0f89791c76c51a30654be30d192e819d6ef5218d69906245565a910f40e35855771202a106aa07032bbd1b819a4c116b8d2d0c81e376c085141ab532748774cdf8eeb9934b0bcb5e19b48a8391c0cb3c5c95a634ed8aa4ae3418acb5b9cca4f7763e373682e6ff3d6b2b8a3748f82ee5defb2fc78a5636f43172f6084c87814a1f0ab728cc702081a6439ec90befffa23631e28a4506cebde82bde9bef9a3f7b2c67915c67178f2e372532bca273eceea26619cd186b8c721c0c207eada7dd6cde0eb1ef57d4f7fee6ed17806f067aa72176fba0a637dc5a2c898a6113f9804bef90dae1b710b35131c471b28db77f523047d8432caab7b40c724933c9ebe0a15c9bebc431d67c49c100d4c4cc5d4becb3e42b6597f299cfc657e2a5fcb6fab3ad6faec6c44198c4a475817";

- static string HHEX="6a09e667f3bcc908bb67ae8584caa73b3c6ef372fe94f82ba54ff53a5f1d36f1510e527fade682d19b05688c2b3e6c1f1f83d9abfb41bd6b5be0cd19137e2179";

- static List<long> K;

- static List<long> H0;

- static bool ready=false;

- static long word64(string h, int idx)

- static void init()

- static long s0(long x)

- static long s1(long x)

- static long bigS0(long x)

- static long bigS1(long x)

- static byte[]Hash(string msg, int len)


## Sm2 (class)

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

- static void init()

- static BigInt fromInt(int v)

- static BigInt fadd(BigInt x, BigInt y)

- static BigInt fsub(BigInt x, BigInt y)

- static BigInt fmul(BigInt x, BigInt y)

- static BigInt fsqr(BigInt x)

- static BigInt finv(BigInt x)

- static BigInt nmod(BigInt x)

- static BigInt nsub(BigInt x, BigInt y)

- static BigInt nmul(BigInt x, BigInt y)

- static BigInt ninv(BigInt x)

- static List<BigInt> pt(BigInt x, BigInt y, BigInt z)

- static List<BigInt> inf()

- static bool isInf(List<BigInt> Pt)

- static List<BigInt> jdouble(List<BigInt> Pt)

- static List<BigInt> jadd(List<BigInt> Pa, List<BigInt> Qa)

- static List<BigInt> scalarMul(BigInt k, List<BigInt> Pt)

- static List<BigInt> toAffine(List<BigInt> Pt)

- static List<BigInt> gPoint()

- static List<BigInt> PublicKey(BigInt d){ init}

- static void writeBE32(string dst, int off, BigInt v)

- static BigInt computeE(string id, int idLen, string msg, int msgLen, BigInt px, BigInt py){ init}

- static void Sign(BigInt e, BigInt d, BigInt k, string outSig){ init}

- static bool Verify(BigInt e, BigInt px, BigInt py, BigInt r, BigInt s){ init}

- static byte[]kdf(string z, int zLen, int klen)

- static byte[]Encrypt(string msg, int msgLen, BigInt px, BigInt py, BigInt k, List<int> outLen){ init}

- static byte[]Decrypt(string cph, int cphLen, BigInt d, List<int> outLen){ init}

- static byte[]sliceBE(string src, int off)


## Sm3 (class)

- static int RotL32(int x, int n)

- static int P0(int x)

- static int P1(int x)

- static byte[]Hash(string msg, int len)

- static void wBE(byte[]b, int o, int v)


## Sm4 (class)

- static int[]Sbox;

- static int[]CK;

- static bool inited=false;

- static int RotL32(int x, int n)

- static void InitTables()

- static int Tau(int a)

- static int LTrans(int b)

- static int LpTrans(int b)

- static int[]KeySchedule(string key, bool forDecrypt)

- static void CryptBlock(string in16, int inOff, byte[]out16, int outOff, int[]rk)

- static byte[]EncryptBlock(string key, string in16)

- static byte[]DecryptBlock(string key, string in16)

- static byte[]EncryptCbc(string key, string iv, string data, int len, List<int> outLen)

- static byte[]DecryptCbc(string key, string iv, string data, int len, List<int> outLen)


## X509Certificate (class)

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

- int RawDerLen()

- string Subject()

- string Issuer()

- string SubjectCN()

- string IssuerCN()

- List<string> DnsNames()

- List<string> IpAddresses()

- byte[]Spki()

- int SpkiLength()

- string SpkiSha256Pin()

- static bool StrEqualsIgnoreCase(string a, string b)

- static string DnsCanonical(string s)

- static bool MatchPattern(string pattern, string host)

- static string IpSanText(byte[]der, int off, int len)

- static string HexGroup(int v)

- static string FormatIPv6Canonical(byte[]b, int off)

- static bool ParseIPv4Into(string h, int start, int len, byte[]out4)

- static bool ParseIPv6ToBytes(string h, byte[]out16)

- static string CanonicalizeHostIp(string host)

- bool VerifyHost(string host)

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

- int PathLenConstraint()

- bool HasEcKey()

- bool SigIsEcdsa()

- static X509Certificate Parse(byte[]der, int len)

- static X509Certificate ParseDisplay(byte[]der, int len)

- static X509Certificate FromPem(string pem)

- static X509Certificate FromPemFile(string path)
