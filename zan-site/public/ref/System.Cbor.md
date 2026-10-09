# System.Cbor

> 源码: `packages/Zan.Cbor/src/System/Cbor/Cbor.zan`, `packages/Zan.Cbor/src/System/Cbor/CborException.zan`, `packages/Zan.Cbor/src/System/Cbor/CborReader.zan`, `packages/Zan.Cbor/src/System/Cbor/CborWriter.zan`


## Cbor (class)

- static byte[]Encode(JsonValue v)

- static JsonValue Decode(byte[]data)

- static JsonValue Decode(byte[]data, int offset, int count)

- static JsonValue Load(string path)

- static void Save(JsonValue v, string path)

- static void EncodeValue(CborWriter w, JsonValue v, int depth)


## CborException (class)

- public CborException(string message)


## CborReader (class)

- byte[]buf;

- int pos;

- int len;

- public CborReader(byte[]data)

- public CborReader(byte[]data, int offset, int count)

- int U8()

- int U16BE()

- long U32BE()

- long U64BE()

- long Arg(int ai)

- int ArgLen(long v)

- byte[]Take(int n)

- JsonValue ReadValue(int depth)

- void PutEntry(JsonValue obj, int depth)

- byte[]TakeChunks(int wantMajor)

- bool AtBreak()

- static JsonValue Tagged(long tag, JsonValue inner)

- JsonValue SimpleValue(int ai)

- static string TextOf(byte[]b)

- static string IsoFromUnixNanos(long sec, long nanos)

- static string Pad2(int v)

- static string Pad4(int v)

- static double U64ToDouble(long v)

- static double BitsHalf(int bits)

- static double BitsSingle(int bits)

- static double BitsDouble(long bits)

- static double Pow2(int e)


## CborWriter (class)

- static long MaxTotalBytes=536870912;

- static long ChunkBytes=67108864;

- byte[]buf;

- int len;

- public CborWriter()

- int Length()

- void EnsureRoom(int extra)

- void RawU8(int b)

- void RawU16BE(int v)

- void RawU32BE(long v)

- void RawU64BE(long v)

- void Head(int major, long arg)

- void WriteUint(long v)

- void WriteNeg(long v)

- void WriteString(string s)

- void WriteBytes(byte[]b)

- void Chunked(int major, byte[]b)

- void WriteArrayHeader(int n)

- void WriteMapHeader(int n)

- void WriteBool(bool b)

- void WriteNull()

- void WriteDouble(double d)

- void WriteTag(long tag)

- byte[]ToBytes()

- static long DoubleBits(double v)
