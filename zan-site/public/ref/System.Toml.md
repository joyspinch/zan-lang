# System.Toml

> 源码: `packages/Zan.Toml/src/System/Toml/Toml.zan`, `packages/Zan.Toml/src/System/Toml/TomlException.zan`, `packages/Zan.Toml/src/System/Toml/TomlParser.zan`


## Toml (class)

- static int MaxDepth=512;

- static JsonValue Parse(string src)

- static JsonValue Load(string path)

- static string ToToml(JsonValue root)

- static void Save(string path, JsonValue root)

- static void WriteTable(StringBuilder sb, JsonValue table, List<string> path, int depth)

- static void WriteValue(StringBuilder sb, JsonValue v, int depth)

- static string Header(List<string> path, bool arrayTable)

- static string EncodeKey(string key)

- static void EncodeString(StringBuilder sb, string t)

- static void Separate(StringBuilder sb)

- static List<string> ExtendPath(List<string> path, string key)

- static bool AllObjects(JsonValue arr)

- static string Hex2(int c)


## TomlException (class)

- public TomlException(string message)


## TomlParser (class)

- string s;

- int pos;

- int len;

- static int MaxDepth=512;

- static long MaxLong=9223372036854775807;

- JsonValue root;

- JsonValue cur;

- Dict <string, bool> headers;

- bool intOverflow;

- bool intBadDigit;

- static JsonValue Parse(string src)

- JsonValue ReadDocument()

- void ReadTableHeader()

- void ReadKeyValue(JsonValue owner, bool needLineEnd)

- List<string> ReadKeyPath()

- string ReadKey()

- JsonValue ReadValue(int depth)

- JsonValue ReadArray(int depth)

- JsonValue ReadInlineTable(int depth)

- JsonValue ReadNumberOrDatetime()

- string ReadBasicString()

- string ReadMultilineBasic()

- string ReadLiteralString()

- string ReadMultilineLiteral()

- void SetValue(JsonValue owner, List<string> path, JsonValue v)

- JsonValue NavigateHeader(List<string> path, bool arrayTable)

- void EndOfLine(string what)

- void SkipWsNewlinesComments()

- void SkipH()

- int CurB()

- bool Has(string lit)

- void ExpectWord(string word)

- void ReadEscape(StringBuilder sb)

- void AppendRun(StringBuilder sb, int firstByte)

- void Err(string msg)

- static bool DateLike(string t)

- static bool Digits(string t, int from, int n)

- static bool IntShape(string t)

- long ParseIntBase(string t)

- static bool FloatShapeStrict(string t)

- static string Strip(string t, string cut)

- static int HexVal(int c)

- static int IndexOf(string s, string needle, int from)

- static bool LineBackslash(string s, int pos, int len)

- static string JoinPath(List<string> path)
