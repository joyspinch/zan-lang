# System.MsgPack

> 源码: `packages/Zan.MsgPack/src/System/MsgPack/MsgPack.zan`, `packages/Zan.MsgPack/src/System/MsgPack/MsgPackException.zan`, `packages/Zan.MsgPack/src/System/MsgPack/MsgPackReader.zan`, `packages/Zan.MsgPack/src/System/MsgPack/MsgPackWriter.zan`


## MsgPack (class)

- static int MaxDepth=512;

- static byte[]Pack(JsonValue v)

- static JsonValue Unpack(byte[]data)

- static JsonValue Unpack(byte[]data, int offset, int count)

- static JsonValue Load(string path)

- static void Save(JsonValue v, string path)

- static void EncodeValue(MsgPackWriter w, JsonValue v, int depth)


## MsgPackException (class)

- public MsgPackException(string message)


## MsgPackReader (class)

- byte[]buf;

- int pos;

- int len;

- public MsgPackReader(byte[]data)

- public MsgPackReader(byte[]data, int offset, int count)

- int U8()

- int U16BE()

- long U32BE()

- long U64BE()

- byte[]Take(int n)

- int S8()

- int S16()

- JsonValue ReadValue(int depth)

- JsonValue ArrayOf(int n, int depth)

- JsonValue MapOf(int n, int depth)

- JsonValue ExtValue(int type, byte[]body)

- long U32Of(byte[]b, int at)

- long U64Of(byte[]b, int at)

- static string IsoFromUnix(long sec)

- static string IsoFromUnixNanos(long sec, long nanos)

- static string Pad2(int v)

- static string Pad4(int v)

- static double BitsDouble(long bits)

- static double BitsSingle(int bits)

- static double Pow2(int e)

- static string DecodedText(byte[]b)


## MsgPackWriter (class)

- static long MaxTotalBytes=536870912;

- byte[]buf;

- int len;

- public MsgPackWriter()

- int Length()

- void EnsureRoom(int extra)

- void RawU8(int b)

- void RawU16BE(int v)

- void RawU32BE(long v)

- void RawU64BE(long v)

- void WriteNil()

- void WriteBool(bool b)

- void WriteInt(long v)

- void WriteDouble(double d)

- void WriteString(string s)

- void WriteStrBytes(byte[]b)

- void WriteBin(byte[]b)

- void WriteArrayHeader(int n)

- void WriteMapHeader(int n)

- byte[]ToBytes()

- static long DoubleBits(double v)
