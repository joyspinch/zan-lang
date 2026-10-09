/* builtin_api.c: 编译器内建类型成员表与签名描述定义 */

#include "builtin_api.h"

#include <string.h>

static const zan_builtin_member_t members_string[] = {
    { "Length",      'P', "int Length" },
    { "Substring",   'M', "string Substring(int start[, int length])" },
    { "IndexOf",     'M', "int IndexOf(string needle[, int startIndex])" },
    { "LastIndexOf", 'M', "int LastIndexOf(string needle)" },
    { "Contains",    'M', "bool Contains(string needle)" },
    { "StartsWith",  'M', "bool StartsWith(string prefix)" },
    { "EndsWith",    'M', "bool EndsWith(string suffix)" },
    { "Replace",     'M', "string Replace(string from, string to)" },
    { "Trim",        'M', "string Trim()" },
    { "ToUpper",     'M', "string ToUpper()" },
    { "ToLower",     'M', "string ToLower()" },
    { "Split",       'M', "List<string> Split(string separator)" },
    { "Equals",      'M', "bool Equals(string other)" },
    { "ToString",    'M', "string ToString()" },
};

static const zan_builtin_member_t members_list[] = {
    { "Count",       'P', "int Count" },
    { "Add",         'M', "void Add(T item)" },
    { "Reserve",     'M', "void Reserve(int n)" },
    { "AddRange",    'M', "void AddRange(List<T> items)" },
    { "Insert",      'M', "void Insert(int index, T item)" },
    { "RemoveAt",    'M', "void RemoveAt(int index)" },
    { "Clear",       'M', "void Clear()" },
    { "Contains",    'M', "bool Contains(T item)" },
    { "IndexOf",     'M', "int IndexOf(T item)" },
    { "LastIndexOf", 'M', "int LastIndexOf(T item)" },
    { "Reverse",     'M', "void Reverse()" },
    { "ToArray",     'M', "T[] ToArray()" },
};

static const zan_builtin_member_t members_dict[] = {
    { "Count",       'P', "int Count" },
    { "Keys",        'P', "List<K> Keys" },
    { "Values",      'P', "List<V> Values" },
    { "Add",         'M', "void Add(K key, V value)" },
    { "Remove",      'M', "void Remove(K key)" },
    { "Clear",       'M', "void Clear()" },
    { "ContainsKey", 'M', "bool ContainsKey(K key)" },
    { "TryGetValue", 'M', "bool TryGetValue(K key, out V value)" },
};

static const zan_builtin_member_t members_sb[] = {
    { "Append",     'M', "void Append(object value)" },
    { "AppendLine", 'M', "void AppendLine(object value)" },
    { "Clear",      'M', "void Clear()" },
    { "Length",     'P', "int Length" },
    { "ToString",   'M', "string ToString()" },
};

static const zan_builtin_member_t members_console[] = {
    { "WriteLine",       'M', "void WriteLine(object value)" },
    { "Write",           'M', "void Write(object value)" },
    { "PrintLine",       'M', "void PrintLine(object value)" },
    { "ReadLine",        'M', "string ReadLine()" },
    { "Read",            'M', "int Read()" },
    { "ReadKey",         'M', "int ReadKey([bool intercept])" },
    { "Clear",           'M', "void Clear()" },
    { "ResetColor",      'M', "void ResetColor()" },
    { "ForegroundColor", 'P', "ConsoleColor ForegroundColor" },
    { "BackgroundColor", 'P', "ConsoleColor BackgroundColor" },
    { "Title",           'P', "string Title" },
};

static const zan_builtin_member_t members_math[] = {
    { "Abs",     'M', "double Abs(double value)" },
    { "Max",     'M', "double Max(double a, double b)" },
    { "Min",     'M', "double Min(double a, double b)" },
    { "Pow",     'M', "double Pow(double x, double y)" },
    { "Sqrt",    'M', "double Sqrt(double value)" },
    { "Round",   'M', "double Round(double value)" },
    { "Floor",   'M', "double Floor(double value)" },
    { "Ceiling", 'M', "double Ceiling(double value)" },
    { "Sin",     'M', "double Sin(double value)" },
    { "Cos",     'M', "double Cos(double value)" },
    { "Tan",     'M', "double Tan(double value)" },
    { "Atan2",   'M', "double Atan2(double y, double x)" },
    { "Atan",    'M', "double Atan(double value)" },
    { "Asin",    'M', "double Asin(double value)" },
    { "Acos",    'M', "double Acos(double value)" },
};

static const zan_builtin_member_t members_convert[] = {
    { "ToDouble", 'M', "double ToDouble(string text)" },
    { "ToInt32",  'M', "int ToInt32(string text)" },
    { "ToInt64",  'M', "long ToInt64(string text)" },
};

static const zan_builtin_member_t members_stringcls[] = {
    { "Format",           'M', "string Format(string format, params object[] args)" },
    { "Join",             'M', "string Join(string separator, List<string> values)" },
    { "IsNullOrEmpty",    'M', "bool IsNullOrEmpty(string value)" },
    { "CompareOrdinal",   'M', "int CompareOrdinal(string a, string b)" },
};

static const zan_builtin_member_t members_file[] = {
    { "Exists",        'M', "bool Exists(string path)" },
    { "ReadAllText",   'M', "string ReadAllText(string path)" },
    { "WriteAllText",  'M', "void WriteAllText(string path, string text)" },
    { "AppendAllText", 'M', "void AppendAllText(string path, string text)" },
    { "Delete",        'M', "void Delete(string path)" },
    { "Copy",          'M', "void Copy(string from, string to)" },
    { "Move",          'M', "void Move(string from, string to)" },
    { "GetSize",       'M', "long GetSize(string path)" },
};

static const zan_builtin_member_t members_dir[] = {
    { "Exists",              'M', "bool Exists(string path)" },
    { "CreateDirectory",     'M', "void CreateDirectory(string path)" },
    { "Delete",              'M', "void Delete(string path)" },
    { "ListNames",           'M', "string ListNames(string globPattern)" },
    { "GetCurrentDirectory", 'M', "string GetCurrentDirectory()" },
    { "SetCurrentDirectory", 'M', "void SetCurrentDirectory(string path)" },
};

static const zan_builtin_member_t members_path[] = {
    { "Combine",                    'M', "string Combine(string a, string b)" },
    { "GetFileName",                'M', "string GetFileName(string path)" },
    { "GetFileNameWithoutExtension",'M', "string GetFileNameWithoutExtension(string path)" },
    { "GetDirectoryName",           'M', "string GetDirectoryName(string path)" },
    { "GetExtension",               'M', "string GetExtension(string path)" },
    { "HasExtension",               'M', "bool HasExtension(string path)" },
    { "GetTempPath",                'M', "string GetTempPath()" },
};

static const zan_builtin_member_t members_env[] = {
    { "ArgCount", 'M', "int ArgCount()" },
    { "ArgAt",    'M', "string ArgAt(int index)" },
    { "ExeDir",   'M', "string ExeDir()" },
};

static const zan_builtin_member_t members_nativemem[] = {
    { "Alloc",     'M', "nint Alloc(long size)" },
    { "Free",      'M', "void Free(nint ptr)" },
    { "Copy",      'M', "void Copy(nint dst, nint src, long size)" },
    { "Copy2D",    'M', "void Copy2D(nint dst, long dstStride, nint src, long srcStride, long rowBytes, long height)" },
    { "Fill",      'M', "void Fill(nint ptr, int value, long size)" },
    { "Compare",   'M', "int Compare(nint a, nint b, long size)" },
    { "ScanNotByte", 'M', "long ScanNotByte(nint ptr, long offset, int byte, long limit)" },
    { "ScanNotAnyOf", 'M', "long ScanNotAnyOf(nint ptr, long offset, string acceptSet, long limit)" },
    { "FindNotAnyOf", 'M', "long FindNotAnyOf(nint ptr, long offset, string rejectSet, long limit)" },
    { "Load64", 'M', "long Load64(nint ptr, long offset)" },
    { "AsI64",  'M', "long AsI64(double value)" },
    { "AsF64",  'M', "double AsF64(long bits)" },
    { "GetString", 'M', "string GetString(nint ptr)" },
    { "PutString", 'M', "void PutString(nint ptr, string text)" },
    { "Sha256", 'M', "string Sha256(nint ptr, long size)" },
    { "Sha1",   'M', "string Sha1(nint ptr, long size)" },
    { "Sha512", 'M', "string Sha512(nint ptr, long size)" },
    { "Crc32",  'M', "long Crc32(nint ptr, long size)" },
    { "Crc32C", 'M', "long Crc32C(nint ptr, long size)" },
    { "Crc32CUpdate", 'M', "long Crc32CUpdate(long crc, nint ptr, long size)" },
    { "AesCbcEncrypt", 'M', "long AesCbcEncrypt(nint dst, nint src, long size, nint key, int keybits, nint iv)" },
    { "AesCbcDecrypt", 'M', "long AesCbcDecrypt(nint dst, nint src, long size, nint key, int keybits, nint iv)" },
    { "AesEcbBlock",   'M', "long AesEcbBlock(nint key, int keybits, nint in16, nint out16)" },
    { "AesCtrCrypt",   'M', "long AesCtrCrypt(nint dst, nint src, long size, nint key, int keybits, nint counter)" },
    { "AesGcmEncrypt", 'M', "long AesGcmEncrypt(nint key, int keybits, nint iv, nint aad, long aadLen, nint inBuf, long inLen, nint outBuf, nint tag16)" },
    { "AesGcmDecrypt", 'M', "long AesGcmDecrypt(nint key, int keybits, nint iv, nint aad, long aadLen, nint inBuf, long inLen, nint tag16, nint outBuf)" },
    { "AesGcmInit",       'M', "long AesGcmInit(nint ctxBuf, long ctxLen, nint key, int keybits)" },
    { "AesGcmEncryptCtx", 'M', "long AesGcmEncryptCtx(nint ctxBuf, nint iv, nint aad, long aadLen, nint inBuf, long inLen, nint outBuf, nint tag16)" },
    { "AesGcmDecryptCtx", 'M', "long AesGcmDecryptCtx(nint ctxBuf, nint iv, nint aad, long aadLen, nint inBuf, long inLen, nint tag16, nint outBuf)" },
    { "GhashBlock",    'M', "long GhashBlock(nint h, nint x, nint y)" },
    { "GhashUpdate",   'M', "long GhashUpdate(nint h, nint data, long len, nint y)" },
    { "RsaModPow",     'M', "long RsaModPow(nint baseVal, long bLen, nint exp, long eLen, nint mod, long mLen, nint outBuf)" },
    { "RsaCrtModPow",  'M', "long RsaCrtModPow(nint msg, long mLen, nint p, long pLen, nint q, long qLen, nint dp, long dpLen, nint dq, long dqLen, nint qinv, long qinvLen, nint outBuf, long outLen)" },
    { "Sm3",              'M', "string Sm3(nint ptr, long size)" },
    { "Sm4CbcEncrypt",    'M', "long Sm4CbcEncrypt(nint dst, nint src, long size, nint key, nint iv)" },
    { "Sm4CbcDecrypt",    'M', "long Sm4CbcDecrypt(nint dst, nint src, long size, nint key, nint iv)" },
    { "Base64Encode",     'M', "string Base64Encode(nint ptr, long size)" },
    { "Base64Decode",     'M', "long Base64Decode(nint dst, nint src, long size)" },
    { "JsonSkipWhitespace", 'M', "long JsonSkipWhitespace(nint ptr, long pos, long len)" },
    { "JsonScanString",     'M', "long JsonScanString(nint ptr, long pos, long len)" },
    { "X25519",             'M', "long X25519(nint scalar, nint point, nint outBuf)" },
};

static const zan_builtin_member_t members_x86_aes[] = {
    { "Encrypt",            'M', "Vector128 Encrypt(Vector128 value, Vector128 roundKey)" },
    { "EncryptLast",        'M', "Vector128 EncryptLast(Vector128 value, Vector128 roundKey)" },
    { "Decrypt",            'M', "Vector128 Decrypt(Vector128 value, Vector128 roundKey)" },
    { "DecryptLast",        'M', "Vector128 DecryptLast(Vector128 value, Vector128 roundKey)" },
    { "KeygenAssist",       'M', "Vector128 KeygenAssist(Vector128 value, byte rcon)" },
    { "InverseMixColumns",  'M', "Vector128 InverseMixColumns(Vector128 value)" },
    { "IsSupported",        'P', "bool IsSupported" },
};

static const zan_builtin_member_t members_x86_sse2[] = {
    { "Xor",            'M', "Vector128 Xor(Vector128 left, Vector128 right)" },
    { "And",            'M', "Vector128 And(Vector128 left, Vector128 right)" },
    { "Or",             'M', "Vector128 Or(Vector128 left, Vector128 right)" },
    { "CompareEqual",   'M', "Vector128 CompareEqual(Vector128 left, Vector128 right)" },
    { "MoveMask",       'M', "int MoveMask(Vector128 value)" },
    { "LoadVector128",  'M', "Vector128 LoadVector128(nint address)" },
    { "Store",          'M', "void Store(nint address, Vector128 source)" },
    { "IsSupported",    'P', "bool IsSupported" },
};

static const zan_builtin_member_t members_vector128[] = {
    { "Create",                       'M', "Vector128 Create(byte value)" },
    { "Load",                         'M', "Vector128 Load(nint address)" },
    { "Load",                         'M', "Vector128 Load(byte[] source)" },
    { "Load",                         'M', "Vector128 Load(byte[] source, int offset)" },
    { "Load",                         'M', "Vector128 Load(float[] source, int offset)" },
    { "Store",                        'M', "void Store(nint address, Vector128 source)" },
    { "Store",                        'M', "void Store(byte[] dest, Vector128 source)" },
    { "Store",                        'M', "void Store(byte[] dest, int offset, Vector128 source)" },
    { "Store",                        'M', "void Store(float[] dest, int offset, Vector128 source)" },
    { "And",                          'M', "Vector128 And(Vector128 left, Vector128 right)" },
    { "Or",                           'M', "Vector128 Or(Vector128 left, Vector128 right)" },
    { "Xor",                          'M', "Vector128 Xor(Vector128 left, Vector128 right)" },
    { "AndNot",                       'M', "Vector128 AndNot(Vector128 left, Vector128 right)" },
    { "Equals",                       'M', "Vector128 Equals(Vector128 left, Vector128 right)" },
    { "ExtractMostSignificantBits",   'M', "int ExtractMostSignificantBits(Vector128 value)" },
    { "Shuffle",                      'M', "Vector128 Shuffle(Vector128 value, Vector128 mask)" },
    { "Add",                          'M', "Vector128 Add(Vector128 left, Vector128 right)" },
    { "Subtract",                     'M', "Vector128 Subtract(Vector128 left, Vector128 right)" },
    { "AddSaturate",                  'M', "Vector128 AddSaturate(Vector128 left, Vector128 right)" },
    { "SubtractSaturate",             'M', "Vector128 SubtractSaturate(Vector128 left, Vector128 right)" },
    { "Min",                          'M', "Vector128 Min(Vector128 left, Vector128 right)" },
    { "Max",                          'M', "Vector128 Max(Vector128 left, Vector128 right)" },
    { "Average",                      'M', "Vector128 Average(Vector128 left, Vector128 right)" },
    { "ConditionalSelect",            'M', "Vector128 ConditionalSelect(Vector128 condition, Vector128 left, Vector128 right)" },
    { "UnpackLow",                    'M', "Vector128 UnpackLow(Vector128 left, Vector128 right)" },
    { "UnpackHigh",                   'M', "Vector128 UnpackHigh(Vector128 left, Vector128 right)" },
    { "GreaterThan",                  'M', "Vector128 GreaterThan(Vector128 left, Vector128 right)" },
    { "LessThan",                     'M', "Vector128 LessThan(Vector128 left, Vector128 right)" },
    { "Prefetch",                     'M', "void Prefetch(nint address)" },
    { "Prefetch",                     'M', "void Prefetch(byte[] source, int offset)" },
    { "Create",                       'M', "Vector128 Create(float value)" },
    { "Create",                       'M', "Vector128 Create(float e0, float e1, float e2, float e3)" },
    { "AddFloat",                     'M', "Vector128 AddFloat(Vector128 left, Vector128 right)" },
    { "SubtractFloat",                'M', "Vector128 SubtractFloat(Vector128 left, Vector128 right)" },
    { "Multiply",                     'M', "Vector128 Multiply(Vector128 left, Vector128 right)" },
    { "MultiplyAdd",                  'M', "Vector128 MultiplyAdd(Vector128 a, Vector128 b, Vector128 c)" },
    { "Sqrt",                         'M', "Vector128 Sqrt(Vector128 value)" },
    { "ReciprocalSqrt",               'M', "Vector128 ReciprocalSqrt(Vector128 value)" },
    { "Zero",                         'P', "Vector128 Zero" },
    { "AllBitsSet",                   'P', "Vector128 AllBitsSet" },
};

static const zan_builtin_member_t members_vector256[] = {
    { "Create",                       'M', "Vector256 Create(byte value)" },
    { "Load",                         'M', "Vector256 Load(nint address)" },
    { "Load",                         'M', "Vector256 Load(byte[] source)" },
    { "Load",                         'M', "Vector256 Load(byte[] source, int offset)" },
    { "Load",                         'M', "Vector256 Load(float[] source, int offset)" },
    { "Store",                        'M', "void Store(nint address, Vector256 source)" },
    { "Store",                        'M', "void Store(byte[] dest, Vector256 source)" },
    { "Store",                        'M', "void Store(byte[] dest, int offset, Vector256 source)" },
    { "Store",                        'M', "void Store(float[] dest, int offset, Vector256 source)" },
    { "And",                          'M', "Vector256 And(Vector256 left, Vector256 right)" },
    { "Or",                           'M', "Vector256 Or(Vector256 left, Vector256 right)" },
    { "Xor",                          'M', "Vector256 Xor(Vector256 left, Vector256 right)" },
    { "AndNot",                       'M', "Vector256 AndNot(Vector256 left, Vector256 right)" },
    { "Equals",                       'M', "Vector256 Equals(Vector256 left, Vector256 right)" },
    { "ExtractMostSignificantBits",   'M', "int ExtractMostSignificantBits(Vector256 value)" },
    { "Add",                          'M', "Vector256 Add(Vector256 left, Vector256 right)" },
    { "Subtract",                     'M', "Vector256 Subtract(Vector256 left, Vector256 right)" },
    { "AddSaturate",                  'M', "Vector256 AddSaturate(Vector256 left, Vector256 right)" },
    { "SubtractSaturate",             'M', "Vector256 SubtractSaturate(Vector256 left, Vector256 right)" },
    { "Min",                          'M', "Vector256 Min(Vector256 left, Vector256 right)" },
    { "Max",                          'M', "Vector256 Max(Vector256 left, Vector256 right)" },
    { "Average",                      'M', "Vector256 Average(Vector256 left, Vector256 right)" },
    { "ConditionalSelect",            'M', "Vector256 ConditionalSelect(Vector256 condition, Vector256 left, Vector256 right)" },
    { "Prefetch",                     'M', "void Prefetch(nint address)" },
    { "Prefetch",                     'M', "void Prefetch(byte[] source, int offset)" },
    { "Zero",                         'P', "Vector256 Zero" },
    { "AllBitsSet",                   'P', "Vector256 AllBitsSet" },
};

static const zan_builtin_member_t members_x86_sse42[] = {
    { "Crc32",          'M', "uint Crc32(uint crc, byte data)" },
    { "Crc32",          'M', "uint Crc32(uint crc, ushort data)" },
    { "Crc32",          'M', "uint Crc32(uint crc, uint data)" },
    { "Crc32",          'M', "ulong Crc32(ulong crc, ulong data)" },
    { "IsSupported",    'P', "bool IsSupported" },
};

static const zan_builtin_member_t members_bitops[] = {
    { "PopCount",           'M', "int PopCount(int value)" },
    { "LeadingZeroCount",   'M', "int LeadingZeroCount(int value)" },
    { "TrailingZeroCount",  'M', "int TrailingZeroCount(int value)" },
    { "RotateLeft",         'M', "int RotateLeft(int value, int offset)" },
    { "RotateRight",        'M', "int RotateRight(int value, int offset)" },
    { "ReverseEndianness",  'M', "int ReverseEndianness(int value)" },
    { "Log2",               'M', "int Log2(int value)" },
    { "IsPow2",             'M', "bool IsPow2(int value)" },
    { "RoundUpToPowerOf2",  'M', "int RoundUpToPowerOf2(int value)" },
    { "ResetLowestSetBit",  'M', "int ResetLowestSetBit(int value)" },
    { "ExtractLowestSetBit",'M', "int ExtractLowestSetBit(int value)" },
};

static const zan_builtin_member_t members_cpu[] = {
    { "HasPopcnt", 'P', "bool HasPopcnt" },
    { "HasLzcnt",  'P', "bool HasLzcnt" },
    { "HasSse42",  'P', "bool HasSse42" },
    { "HasAvx2",   'P', "bool HasAvx2" },
    { "HasAesNi",  'P', "bool HasAesNi" },
    { "HasNeon",   'P', "bool HasNeon" },
    { "CpuFeature",'M', "int CpuFeature(int id)" },
};

static const zan_builtin_member_t members_task[] = {
    { "Spawn",  'M', "long Spawn(asyncCall)" },
    { "Run",    'M', "long Run(asyncCall)" },
    { "Delay",  'M', "await Task.Delay(long ms)" },
    { "Yield",  'M', "await Task.Yield()" },
    { "WhenAll",'M', "async int WhenAll(List<long> handles)" },
    { "WhenAny",'M', "async int WhenAny(List<long> handles)" },
    { "IsDone", 'M', "int IsDone(long handle)" },
    { "Cancel", 'M', "void Cancel(long handle)" },
    { "IsCancellationRequested", 'M', "int IsCancellationRequested()" },
    { "JoinNew",   'M', "long JoinNew(int npairs, int any)" },
    { "JoinBind",  'M', "int JoinBind(long entry, long handle, int idx)" },
    { "JoinWait",  'M', "await int JoinWait(long entry)" },
    { "JoinCancel",'M', "void JoinCancel(long entry)" },
};

static const zan_builtin_member_t members_pixelops[] = {
    { "BlendOver",           'M', "void BlendOver(nint dst, nint src, int count)" },
    { "SwapRB",              'M', "void SwapRB(nint dst, nint src, int count)" },
    { "FillRect",            'M', "void FillRect(nint dst, int dstStride, int x, int y, int w, int h, int color)" },
    { "ResampleBilinearRow", 'M', "void ResampleBilinearRow(nint dst, nint src0, nint src1, nint xIndices, nint xWeights, int weightY, int width)" },
};

static const zan_builtin_member_t members_scalar[] = {
    { "ToString",            'M', "string ToString()" },
};

#define BT(name, pub, disp, stat, arr) \
    { name, pub, disp, stat, arr, (int)(sizeof(arr) / sizeof((arr)[0])) }

static const zan_builtin_type_t builtin_types[] = {
    BT("int", "int", "int", 0, members_scalar),
    BT("long", "long", "long", 0, members_scalar),
    BT("short", "short", "short", 0, members_scalar),
    BT("byte", "byte", "byte", 0, members_scalar),
    BT("sbyte", "sbyte", "sbyte", 0, members_scalar),
    BT("uint", "uint", "uint", 0, members_scalar),
    BT("ulong", "ulong", "ulong", 0, members_scalar),
    BT("ushort", "ushort", "ushort", 0, members_scalar),
    BT("float", "float", "float", 0, members_scalar),
    BT("double", "double", "double", 0, members_scalar),
    BT("bool", "bool", "bool", 0, members_scalar),
    BT("char", "char", "char", 0, members_scalar),
    BT("nint", "nint", "nint", 0, members_scalar),
    BT("object", "object", "object", 0, members_scalar),
    BT("string", "string", "string", 0, members_string),
    BT("List", "List", "List<T>", 0, members_list),
    BT("Dict", "Dictionary", "Dictionary<K,V>", 0, members_dict),
    BT("StringBuilder", "StringBuilder", "StringBuilder", 0, members_sb),
    BT("Console", "Console", "Console", 1, members_console),
    BT("Math", "Math", "Math", 1, members_math),
    BT("Convert", "Convert", "Convert", 1, members_convert),
    BT("String", "String", "String", 1, members_stringcls),
    BT("File", "File", "File", 1, members_file),
    BT("Directory", "Directory", "Directory", 1, members_dir),
    BT("Path", "Path", "Path", 1, members_path),
    BT("Environment", "Environment", "Environment", 1, members_env),
    BT("NativeMemory", "NativeMemory", "NativeMemory", 1, members_nativemem),
    BT("Task", "Task", "Task", 1, members_task),
    BT("PixelOps", "PixelOps", "PixelOps", 1, members_pixelops),
    BT("BitOperations", "BitOperations", "BitOperations", 1, members_bitops),
    BT("Cpu", "Cpu", "Cpu", 1, members_cpu),
    BT("Aes", "Aes", "Aes", 1, members_x86_aes),
    BT("Sse2", "Sse2", "Sse2", 1, members_x86_sse2),
    BT("Sse42", "Sse42", "Sse42", 1, members_x86_sse42),
    BT("Vector128", "Vector128", "Vector128", 1, members_vector128),
    BT("Vector256", "Vector256", "Vector256", 1, members_vector256),
};

const zan_builtin_type_t *zan_builtin_types(int *count) {
    if (count) *count = (int)(sizeof(builtin_types) / sizeof(builtin_types[0]));
    return builtin_types;
}

const zan_builtin_type_t *zan_builtin_find(const char *type) {
    if (!type) return NULL;
    for (size_t i = 0; i < sizeof(builtin_types) / sizeof(builtin_types[0]); i++) {
        if (strcmp(builtin_types[i].type, type) == 0) return &builtin_types[i];
    }
    return NULL;
}

/* 获取内建成员的返回值类型名称（解析方法/属性签名中首个空格前的类型名） */
const char *zan_builtin_member_result(const char *type, const char *name,
                                      int name_len) {
    static const char *results[] = {
        "string", "int", "long", "double", "bool", "void", "nint",
        "List<string>", "List<K>", "List<V>", "ConsoleColor", "T[]",
        "Vector128", "Vector256",
    };
    const zan_builtin_type_t *bt = zan_builtin_find(type);
    if (!bt || !name || name_len <= 0) return NULL;
    for (int i = 0; i < bt->member_count; i++) {
        const char *m = bt->members[i].name;
        if ((int)strlen(m) != name_len || memcmp(m, name, (size_t)name_len) != 0)
            continue;
        const char *sig = bt->members[i].sig;
        const char *sp = strchr(sig, ' ');
        if (!sp) return NULL;
        size_t len = (size_t)(sp - sig);
        for (size_t r = 0; r < sizeof(results) / sizeof(results[0]); r++) {
            if (strlen(results[r]) == len && memcmp(results[r], sig, len) == 0)
                return results[r];
        }
        return NULL;
    }
    return NULL;
}

char zan_builtin_member_kind(const char *type, const char *name, int name_len) {
    const zan_builtin_type_t *bt = zan_builtin_find(type);
    if (!bt || !name || name_len <= 0) return 'M';
    for (int i = 0; i < bt->member_count; i++) {
        const char *m = bt->members[i].name;
        if ((int)strlen(m) == name_len && memcmp(m, name, (size_t)name_len) == 0)
            return bt->members[i].kind;
    }
    return 'M';
}

int zan_builtin_has_member(const char *type, const char *name, int name_len) {
    const zan_builtin_type_t *bt = zan_builtin_find(type);
    if (!bt) return 1;
    if (!name || name_len <= 0) return 1;
    for (int i = 0; i < bt->member_count; i++) {
        const char *m = bt->members[i].name;
        if ((int)strlen(m) == name_len && memcmp(m, name, (size_t)name_len) == 0)
            return 1;
    }
    return 0;
}
