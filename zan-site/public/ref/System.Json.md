# System.Json

> 源码: `stdlib/System/Json/Json.zan`, `stdlib/System/Json/JsonValue.zan`


## JsonBuilder (class)

用于生成 JSON 字符串的简单 JSON 构建器。

- static string ObjectStart()

- static string ObjectEnd(string json)

- static string AddString(string json, string key, string val)

- static string AddInt(string json, string key, int val)

- static string AddBool(string json, string key, bool val)

- static string AddNull(string json, string key)


## JsonParser (class)

用纯 Zan 实现的最小 JSON 解析器和序列化器。
支持字符串、数字、布尔、null、数组和对象。

- [DllImport("crt")]static extern long strlen(string str);

- static string GetString(string json, string key)
  - 从 JSON 对象字符串中按 key 提取 JSON 字符串值。

- static int GetInt(string json, string key)
  - 按 key 提取 JSON 整数值。

- static double GetDouble(string json, string key)
  - 按 key 提取 JSON 数字（double）值。

- static bool HasKey(string json, string key)
  - 检查 JSON 中是否存在指定 key。

- static bool GetBool(string json, string key)
  - 检查指定 key 的 JSON 布尔值是否为 true。

- static string GetValueRaw(string json, string key)
  - 获取 key 的原始值字符串（不带引号）。


## JsonReader (class)

驱动 JsonValue.Parse 的源字符串游标（Zan 没有
ref/out 参数，所以位置保存在该对象上）。

- string s;

- int pos;

- int len;

- static int MaxDepth=512;
  - 递归解析/校验的最大嵌套深度。深层嵌套文档会耗尽
    调用栈（约 20-40 万层时解析器崩溃），必须显式限制。

- bool strict;
  - 严格模式（JsonValue.Parse）：边建树边校验，首个错误位置
    记在 errPos 上；宽松模式（ParseLenient）行为保持不变。

- int errPos;

- static JsonReader Of(string src)

- static JsonReader OfStrict(string src)

- void Fail()
  - 记录首个错误位置（后来的错误不覆盖它）。

- bool Failed()

- int CurB()
  - 游标处的字节，输入结束时为 -1。

- void SkipWs()

- JsonValue ReadValue()

- JsonValue ReadValueAt(int depth)

- JsonValue ReadObjectAt(int depth)

- JsonValue ReadArrayAt(int depth)

- string ReadString()
  - 从起始引号读取带引号的字符串；解码常见
    转义。结束后 pos 位于闭合引号之后。无转义的字符串
    （最常见的情况）作为单个子串返回；
    含转义的字符串则回退到 StringBuilder，批量复制
    转义之间的普通片段。

- static int Hex4(string src, int at, int limit)
  - 从 `at` 开始的 4 位十六进制值；不足 4 位或含非十六进制
    字符时返回 -1。

- JsonValue ReadBool()

- JsonValue ReadNumber()

- bool ValidLiteral(string lit)

- bool ValidNumber()


## JsonValue (class)

解析后的 JSON 值树（object / array / string / number / bool / null）。

与 JsonParser 的扁平 key 扫描器不同，这是真正的递归模型，
可以遍历嵌套文档（UI 布局树、状态实体）。用
`Parse` 构建；用 As*/类型化字段助手读取标量；
用 Get/Has 遍历对象、At/Count 遍历数组。

kind：0 null，1 bool，2 number，3 string，4 array，5 object。

- int kind;

- bool boolVal;

- string numRaw;

- string strVal;

- List<JsonValue> items;

- List<string> keys;

- List<JsonValue> vals;

- static JsonValue NewNull()

- static JsonValue NewBool(bool b)

- static JsonValue NewNum(string raw)

- static JsonValue NewStr(string s)

- static JsonValue NewArray()

- static JsonValue NewObject()

- bool IsNull()

- bool IsBool()

- bool IsNumber()

- bool IsString()

- bool IsArray()

- bool IsObject()

- void Put(string key, JsonValue v)
  - 追加键值对（对象）。不去重。

- bool Has(string key)

- JsonValue Get(string key)
  - `key` 对应的值；不存在或不是对象时返回 null。

- void Set(string key, JsonValue v)
  - 若 `key` 已存在则替换其值，否则追加（对象）。

- JsonValue PathGet(string path)
  - 从该对象解析点分路径（"user.name"）；任一段
    缺失或不是对象时返回 null。用于双向 `bind` 查找。

- void PathSet(string path, string val)
  - 沿点分路径写入标量字符串，按需创建中间对象
    和叶子节点，使绑定的控件能把编辑内容回写到状态。

- void PathSetValue(string path, JsonValue leaf)
  - 与 PathSet 类似，但写入任意 JsonValue 叶子，使绑定的控件
    能在回写时保持值的 JSON 类型（bool / number / string），
    而不是全部转成字符串。

- void Append(JsonValue v)

- int Count()
  - 元素数量（数组元素或对象条目数；其他情况为 0）。

- JsonValue At(int i)

- string KeyAt(int i)
  - 第 i 个对象的 key（用于遍历对象）。

- JsonValue ValAt(int i)

- string ToJson()
  - 将该值序列化为紧凑 JSON 字符串。字符串和
    对象 key 经 Encoding.JsonEscape 转义；数字输出原始
    字面量（空 -> 0）。是 Parse 对值模型的逆操作。

- void WriteTo(StringBuilder sb)

- string PrettyToJson()
  - 序列化为便于阅读的多行 JSON 字符串（两空格
    缩进，每个 key 一行），用于手工编辑的交换文件，如
    .zform / .zscene。与 ToJson 一样可通过 Parse 往返。

- void WritePretty(StringBuilder sb, int depth)

- static string PrettyIndent(int depth)

- static int Digit(string ch)

- static int IntOf(string t)
  - 解析数字 token 的前导（可选符号）整数部分，
    在 '.'/'e' 处停止。避免 "60.0"/"1e3" 触发 Convert 异常。

- static long LongOf(string t)
  - 与 IntOf 类似，但按 64 位累加，使 `long`/`ulong` 字段能保存
    超出 32 位范围的值。

- static double DoubleOf(string t)
  - 无需 Convert 解析数字 token（符号、小数、指数），
    避免异常；非数字前缀时返回 0.0。

- string AsString(string dflt)

- int AsInt(int dflt)

- long AsLong(long dflt)

- double AsDouble(double dflt)

- bool AsBool(bool dflt)

- string Str(string key, string dflt)

- int Int(string key, int dflt)

- string PathStr(string path, string dflt)
  - 点分路径的类型化读取：路径上任何一段缺失（PathGet 返回 null）都
    退回 `dflt`，而不是让调用方对 null 调 AsString/AsInt 而崩溃。

- int PathInt(string path, int dflt)
  - 同 PathStr，整数版。

- long Long(string key, long dflt)

- bool Bool(string key, bool dflt)

- double Double(string key, double dflt)

- static JsonValue Parse(string src)
  - 将 JSON 文档解析为值树。格式错误时抛出
    JsonException（与 C# 一致）。校验与建树在同一遍扫描里
    完成：出错只记录位置并停止，抛异常前先丢掉部分树，
    所以错误路径依然不持有存活分配。

- static JsonValue ParseLenient(string src)
  - 尽力而为的解析：输入格式错误时返回已解析的部分
    而不抛异常（用于手工编写/工具生成的 UI 文档，部分
    结果树比异常更合适）。
