# System.Yaml

> 源码: `packages/Zan.Yaml/src/System/Yaml/Yaml.zan`, `packages/Zan.Yaml/src/System/Yaml/YamlException.zan`, `packages/Zan.Yaml/src/System/Yaml/YamlParser.zan`


## Yaml (class)

- public static JsonValue Parse(string src)

- public static JsonValue Load(string path)

- public static string ToYaml(JsonValue v)

- public static void Save(JsonValue v, string path)

- static void WriteMap(StringBuilder sb, JsonValue m, int indent, bool firstInline)

- static void WriteSeq(StringBuilder sb, JsonValue a, int indent, bool firstInline)

- static void WriteValueAfter(StringBuilder sb, JsonValue val, int indent)

- static void AppendIndent(StringBuilder sb, int indent)

- static string EmitKey(string k)

- static string ScalarText(JsonValue v)

- static string DoubleText(double d)

- static bool NeedsQuote(string s)

- static string EncodeDoubleQuoted(string s)

- static string HexChar(int v)


## YamlException (class)

- public YamlException(string message)


## YamlLine (class)

- public int indent;

- public string text;

- public int lineNo;

- YamlLine(int indent, string text, int lineNo)


## YamlParser (class)

- List<YamlLine> lines;

- Dict <string, JsonValue> anchors;

- int cur;

- int lineCount;

- int lastLineNo;

- void Advance()

- public YamlParser(string src)

- void SplitLines(string src)

- void PushLine(StringBuilder sb, int lineNo)

- void SkipPrologue()

- public JsonValue ParseDocument()

- void SkipIgnorable()

- bool AtDocMarker()

- JsonValue ParseBlock(int minIndent, int depth)

- JsonValue ParseMap(int indent, int depth)

- JsonValue ParseSeq(int indent, int depth)

- JsonValue ParseBareNode(int depth)

- JsonValue ScalarNode(string rest)

- JsonValue ResolveAlias(string rest)

- JsonValue ReadBlockScalar(string header, int parentIndent)

- JsonValue ParseJoinedFlow(string first)

- int BracketDepthFrom(string s, int depthIn, ref int quoteIn)

- JsonValue FlowValue(string s, ref int at, int depth)

- JsonValue ResolveAliasFlow(string s, ref int at)

- JsonValue FlowScalar(string s, ref int at)

- void SkipFlowSpace(string s, ref int at)

- int FindKeyColon(string t)

- string DecodeKey(string part)

- bool IsCommentOnly(string rest)

- bool IsSeqLine(string t)

- string StripComment(string s)

- string DecodeDoubleQuoted(string t)

- string DecodeSingleQuoted(string t)

- void ReadEscape(string s, ref int at, StringBuilder sb)

- int Hex4(string s, int at)

- int Hex2(string s, int at)

- static int HexDigit(int c)

- public static JsonValue TypeScalar(string s)

- static double Pow2(int e)

- static double DoubleOf(string s)

- static bool IsIntShape(string s)

- static bool IsFloatShape(string s)
