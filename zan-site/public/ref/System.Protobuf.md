# System.Protobuf

> 源码: `packages/Zan.Protobuf/src/System/Protobuf/ProtoException.zan`, `packages/Zan.Protobuf/src/System/Protobuf/ProtoReader.zan`, `packages/Zan.Protobuf/src/System/Protobuf/ProtoWriter.zan`


## ProtoException (class)

- public ProtoException(string message)


## ProtoReader (class)

- byte[]buf;

- int pos;

- int len;

- int curField;

- int curWire;

- public ProtoReader(byte[]data)

- public ProtoReader(byte[]data, int offset, int count)

- int FieldNumber()

- int WireType()

- bool Next()

- static int FieldOf(long tag)

- int LenOf(long v)

- long ReadVarint()

- void Need(int n)

- long ReadVarintField()

- int ReadInt32()

- long ReadInt64()

- long ReadUint32()

- long ReadUint64()

- bool ReadBool()

- int ReadEnum()

- int ReadSint32()

- long ReadSint64()

- long ReadFixed64()

- long ReadFixed32()

- double ReadDouble()

- string ReadString()

- byte[]ReadBytes()

- byte[]ReadMessage()

- void Skip()

- void SkipDepth(int depth)

- void ExpectWire(int want, string what)

- static double BitsDouble(long bits)

- static double Pow2(int e)


## ProtoWriter (class)

- static long MaxTotalBytes=536870912;

- byte[]buf;

- int len;

- List<int> openFields;

- List<int> openMarks;

- public ProtoWriter()

- int Length()

- void EnsureRoom(int extra)

- static long Tag(int field, int wire)

- void RawU8(int b)

- void RawVarint(long v)

- void RawFixed64(long v)

- void RawFixed32(long v)

- void WriteInt32(int field, int v)

- void WriteInt64(int field, long v)

- void WriteUint32(int field, long v)

- void WriteUint64(int field, long v)

- void WriteBool(int field, bool v)

- void WriteEnum(int field, int v)

- void WriteVarint(int field, long v)

- void WriteSint32(int field, int v)

- void WriteSint64(int field, long v)

- void WriteString(int field, string s)

- void WriteBytes(int field, byte[]v)

- void WriteBytes(int field, byte[]v, int offset, int count)

- void WriteMessage(int field, byte[]payload)

- void WriteBytesField(int field, byte[]v, int offset, int count)

- void WriteFixed64(int field, long v)

- void WriteFixed32(int field, long v)

- void WriteDouble(int field, double v)

- int BeginMessage(int field)

- void EndMessage()

- byte[]ToBytes()

- void Reset()

- static long DoubleBits(double v)
