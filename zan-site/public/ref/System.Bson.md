# System.Bson

> 源码: `packages/Zan.Bson/src/System/Bson/Bson.zan`, `packages/Zan.Bson/src/System/Bson/BsonException.zan`, `packages/Zan.Bson/src/System/Bson/BsonReader.zan`, `packages/Zan.Bson/src/System/Bson/BsonWriter.zan`


## Bson (class)

- static int MaxDepth=512;

- static byte[]Pack(JsonValue v)

- static JsonValue Unpack(byte[]data)

- static JsonValue Unpack(byte[]data, int offset, int count)

- static JsonValue Load(string path)

- static void Save(JsonValue v, string path)

- static void EncodeElement(BsonWriter w, string key, JsonValue v, int depth)


## BsonException (class)

- public BsonException(string message)


## BsonReader (class)

- byte[]buf;

- int pos;

- int len;

- public BsonReader(byte[]data)

- public BsonReader(byte[]data, int offset, int count)

- int U8()

- long U32LE()

- long U64LE()

- int S32LE()

- byte[]Take(int n)

- JsonValue ReadDocument(int depth, bool asArray)

- string CStr(int bodyEnd)

- JsonValue ReadElement(int t, int depth)

- byte[]StringBody()

- static string HexOf(byte[]b)

- static string HexByte(int b)

- static string IsoFromMs(long ms)

- static string Pad2(int v)

- static string Pad4(int v)

- static double BitsDouble(long bits)

- static string DecodedText(byte[]b)


## BsonWriter (class)

- static long MaxTotalBytes=536870912;

- byte[]buf;

- int len;

- public BsonWriter()

- int Length()

- void EnsureRoom(int extra)

- void RawU8(int b)

- void RawU32LE(long v)

- void RawU64LE(long v)

- void RawBytes(byte[]b)

- void RawType(int t)

- void CStr(string s)

- void WriteString(string s)

- void WriteDouble(double d)

- void WriteBool(bool b)

- void WriteInt32(int v)

- void WriteInt64(long v)

- byte[]FinishDocument()
