# System.Json

> 源码: `stdlib/System/Json/JsonTape.zan`, `stdlib/System/Json/JsonValue.zan`


## JsonDoc (class)

- public List<JsonSlot> slots;

- public List<JsonPair> pairs;

- public List<string> escPool;

- public string src;

- public byte[]buf;

- public int root;

- static JsonDoc Parse(string src)

- static int ParseCore(List<JsonSlot> sl, List<JsonPair> pr, List<string> pool, string src, byte[]buf, int len)

- static int ScanKey(List<JsonSlot> sl, List<JsonPair> pr, List<string> pool, string src, byte[]buf, int len, int pos, int pbase, int[]fr)

- static int EscStringInto(List<string> pool, string src, byte[]buf, int len, int start, int esc)

- static double DoubleInRange(byte[]buf, int from, int to)

- static int Hex4(string src, int at, int limit)

- static int MaxDepth=512;

- int SlotCount()

- int NextSibling(int at)

- int FirstChild(int at)

- int KindAt(int at)

- int At(int at, int i)

- void ForEachChild(int at, JsonChildVisitor fn)

- int Count(int at)

- int Get(int at, string key)

- bool Has(int at, string key)

- string StrOfSlot(int at)

- int IntAt(int at, int dflt)

- long LongAt(int at, long dflt)

- double DoubleAt(int at, double dflt)

- string StrAt(int at, string dflt)

- bool BoolAt(int at, bool dflt)

- string KeyAt(int at, int i)

- int ValAt(int at, int i)

- string ToJson()

- void WriteSlot(StringBuilder sb, int at)


## JsonProperty (class)

- public string key;

- public JsonValue val;

- public JsonProperty(string key, JsonValue val)


## JsonReader (class)

- string s;

- byte[]buf;

- int pos;

- int len;

- static int MaxDepth=512;

- bool strict;

- int errPos;

- static JsonReader Of(string src)

- static JsonReader OfStrict(string src)

- void Fail()

- bool Failed()

- int CurB()

- void SkipWs()

- JsonValue ReadValue()

- JsonValue ReadValueAt(int depth)

- JsonValue ReadObjectAt(int depth)

- JsonValue ReadArrayAt(int depth)

- string ReadString()

- static int Hex4(string src, int at, int limit)

- JsonValue ReadBool()

- JsonValue ReadNumber()

- bool ValidLiteral(string lit)

- bool ValidNumberAt(int from, int to)


## JsonValue (class)

- int kind;

- bool boolVal;

- long numI;

- double numD;

- bool numIsInt;

- string numRaw;

- string strVal;

- List<JsonValue> items;

- List<string> keys;

- List<JsonValue> vals;

- Dict <string, int> keyIndex;

- static int MaxDepth=512;

- static JsonValue NewNull()

- static JsonValue NewBool(bool b)

- static JsonValue NewInt(long n)

- static JsonValue NewDouble(double d)

- static JsonValue NewNum(string raw)

- static JsonValue NewStr(string s)

- static JsonValue NewArray()

- static JsonValue NewObject()

- JsonValue Clone()

- bool IsNull()

- bool IsBool()

- bool IsNumber()

- bool IsInt()

- bool IsString()

- bool IsArray()

- bool IsObject()

- void Put(string key, JsonValue v)

- bool Has(string key)

- JsonValue Get(string key)

- void Set(string key, JsonValue v)

- bool Remove(string key)

- void RemoveAt(int index)

- JsonValue PathGet(string path)

- void PathSet(string path, string val)

- void PathSetValue(string path, JsonValue leaf)

- void PutStr(string key, string v)

- void PutInt(string key, long v)

- void PutDouble(string key, double v)

- void PutBool(string key, bool v)

- void PutNull(string key)

- void PutJson(string key, string jsonText)

- void AppendStr(string v)

- void AppendInt(long v)

- void AppendDouble(double v)

- void AppendBool(bool v)

- void AppendJson(string jsonText)

- static JsonValue ParseOrStr(string jsonText)

- void Append(JsonValue v)

- int Count()

- JsonValue At(int i)

- void SetAt(int i, JsonValue val)

- string KeyAt(int i)

- JsonValue ValAt(int i)

- string ToJson()

- void WriteTo(StringBuilder sb)

- void WriteToAt(StringBuilder sb, int depth)

- static string NumText(JsonValue v)

- string PrettyToJson()

- void WritePretty(StringBuilder sb, int depth)

- static string PrettyIndent(int depth)

- static int Digit(string ch)

- static int IntOf(string t)

- static long LongOf(string t)

- static double[]Pow10Tab=new double[]{ 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

- static double DoubleOf(string t)

- string AsString(string dflt)

- int AsInt(int dflt)

- long AsLong(long dflt)

- double AsDouble(double dflt)

- bool AsBool(bool dflt)

- string Str(string key, string dflt)

- int Int(string key, int dflt)

- string PathStr(string path, string dflt)

- int PathInt(string path, int dflt)

- long Long(string key, long dflt)

- bool Bool(string key, bool dflt)

- double Double(string key, double dflt)

- static JsonValue ParseNumberToken(string t, int from, int to)

- static JsonValue Parse(string src)

- static JsonValue ParseLenient(string src)


## void (delegate)

`delegate void JsonChildVisitor(int childSlot);`


## JsonPair (struct)

- public int keyStart;

- public int keyLen;

- public int nextPair;

- public int valueSlot;


## JsonSlot (struct)

- public long a;

- public long b;
