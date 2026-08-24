# System.Text

> 源码: `stdlib/System/Text/Bm25Index.zan`, `stdlib/System/Text/Csv.zan`, `stdlib/System/Text/Encoding.zan`, `stdlib/System/Text/FuzzyMatching.zan`, `stdlib/System/Text/Markdown.zan`, `stdlib/System/Text/Pinyin.zan`, `stdlib/System/Text/Template.zan`, `stdlib/System/Text/TextTable.zan`


## Bm25Document (class)

- int id;

- Dictionary <string, int> terms;

- int length;

- Bm25Document(int id, Dictionary <string, int> terms, int length)


## Bm25Hit (class)

- int id;

- double score;

- Bm25Hit(int id, double score)


## Bm25Index (class)

一个轻量的内存 BM25（Best Matching 25）文本索引。文档按
分词（转小写、按非字母数字字符切分），并对照查询
用标准参数 k1=1.2、b=0.75 打分；返回结果
为按分数降序排列的文档 id。

Bm25Index idx = new Bm25Index();
idx.Add(1, "the quick brown fox jumps");
idx.Add(2, "lazy dog sleeps all day");
List<int> hits = idx.Search("quick fox");   // [1]

纯算法实现，无平台依赖；按 IDE 级语料规模设计
（数千文档、小词元）——每次查询都会扫描全部文档。

- List<Bm25Document> documents=new List<Bm25Document>();

- Dictionary <string, int> df=new Dictionary <string, int>();

- int totalLen;

- double k1;

- double b;

- Bm25Index()

- void Add(int docId, string text)
  - 添加文档。重复添加同一 id 会替换其内容。

- void Remove(int docId)
  - 删除一个文档 id；不存在时不作任何操作。

- int Count()
  - 已索引的文档数量。

- List<int> Search(string query)
  - 对所有文档按 `query` 打分；分数非零的 id 才会被返回，
    分数从高到低。空查询返回空结果。

- static Dictionary <string, int> Tokenize(string text)

- static List<string> UniqueTerms(string query)

- static int GetSeen(Dictionary <string, int> seen, string key)

- static void AddTerm(Dictionary <string, int> terms, string term)

- int DfOf(string term)

- void IncDf(string term)

- void DecDf(string term)


## Csv (class)

极简 CSV 读写器。字段可用引号包围；被引字段中的引号
会写成两个（<c>""</c>）。引号只在字段开头才有特殊含义
（RFC 4180）。可指定自定义分隔符（如 TSV 用制表符）。

List<List<string>> rows = Csv.Parse(text);        // 逗号分隔
List<List<string>> tsv = Csv.Parse(text, "\t");
string out = Csv.Serialize(rows);                 // 逗号分隔，自动加引号

- static List <List<string>> Parse(string text)
  - 将 CSV 文本解析为字段行。末尾的空行
    会被忽略；格式错误的引号按字面处理。

- static List <List<string>> ParseSep(string text, string separator)
  - 使用自定义分隔符（单个字符）解析。

- static string Serialize(List <List<string>> rows)
  - 将行序列化为 CSV 文本，对包含分隔符、引号或
    换行符的字段加引号。末尾以换行符结束。

- static string SerializeSep(List <List<string>> rows, string separator)
  - 使用自定义分隔符序列化。

- static string Escape(string field, string separator)


## Encoding (class)

跨平台文本编码与转换工具。
使用各平台均可用的 C 运行时函数。

- [DllImport("crt")]static extern long strlen(string str);

- static string IntToString(int val)
  - 将整数转换为字符串。逐位生成
    而不用 sprintf：变参 C 函数在 Darwin arm64 上采用不同 ABI（参数在栈上，
    而非寄存器中），因此 FFI 变参调用会在该平台
    破坏参数。

- static string DoubleToString(double val)
  - 将 double 转换为字符串。

- static int ParseInt(string text)
  - 将字符串解析为整数，与 C 的 atoi 一致：跳过前导
    空白、可选正负号，然后读取十进制数字直到遇到首个
    非数字字符。无数字时返回 0。

- static double ParseDouble(string text)
  - 将字符串解析为 double，与 C 的 atof 一致：可选符号、
    整数和小数部分，以及可选十进制指数
    （e/E）。无数字时返回 0。

- static int GetByteCount(string text)
  - 获取 UTF-8 字符串的字节数。

- static string ByteToHex(int b)
  - 将字节值转换为十六进制字符串（两个字符）。

- static int HexDigit(int c)
  - 单个十六进制数字的值（0-9、a-f、A-F），非法时返回 -1。

- static string CharFromCode(int code)
  - 构造包含给定字节值的单字符字符串。

- static string Utf8FromCodePoint(int code)
  - 把一个 Unicode 码点编码为 UTF-8 字节序列（1-4 字节）。
    码点越界或落在代理区时用替换字符 U+FFFD，因此结果
    始终是合法 UTF-8。JSON 的 <c>\uXXXX</c> 解码走这里。

- static string UrlEncode(string text)
  - 对字符串进行 URL 编码。

- static string UrlDecode(string text)
  - 对 application/x-www-form-urlencoded 字符串进行 URL 解码：
    '+' 变为空格，每个 %XX 转义解码为其对应字节。
    格式错误的转义原样保留。

- static string HtmlEncode(string text)
  - 对文本转义，以便安全嵌入 HTML 元素或属性
    内容（< > & " '）。

- static string JsonEscape(string text)
  - 转义文本以用于 JSON 字符串字面量内部（引号由调用方
    提供）。0x20 以下的控制字符按 RFC 8259 转义：
    \n \r \t \b \f 用短形式，其余用 \u00xx，因此转义是无损的。

- static string Base64Encode(string text)
  - 对字符串进行 Base64 编码。

- static string Base64EncodeBytes(string data, int len)
  - 对 <paramref name="data"/> 中的前 <paramref name="len"/> 个原始字节做 Base64 编码。
    二进制安全：长度显式给出，因此嵌入的
    NUL 字节（如 SHA-1 摘要）会被编码，而不会像
    基于 strlen 的 `Base64Encode` 那样被截断。

- static int Base64Digit(int c)
  - 单个 base64 字符的 6 位值；'=' 为 -2，
    其他非 base64 字符为 -1。

- static byte[]Base64Decode(string text)
  - 解码 base64 文本为原始字节。返回 <c>byte[]</c> 而非
    字符串：Zan 字符串以 NUL 结尾，任何含 0x00 的负载装进字符串
    都会被后续 strlen 截断。空白被忽略，标准与 URL 安全字母表都
    接受；遇到非法字符或残缺尾部时抛出 ArgumentException，
    避免静默截断成半个负载。

- static long Rotl32(long x, int n)
  - 32 位循环左移。值保存在 <c>long</c> 中，
    掩码到 32 位，因此循环右移的部分以零补入，
    而不是 <c>int</c> 会带入的符号位。

- static void Sha1(string data, int len, byte[]digest)
  - 计算 <paramref name="data"/> 前 <paramref
    name="len"/> 个字节的 20 字节 SHA-1 摘要，写入
    调用方持有的 20 字节缓冲区 <paramref name="digest"/>。消息长度
    最高支持 2^29 字节（64 位长度字段由 32 位
    位计数写出），足以覆盖此处的握手/哈希用途。

- static string WebSocketAccept(string key)
  - 计算 RFC 6455 <c>Sec-WebSocket-Accept</c> 的值，用于客户端的
    <c>Sec-WebSocket-Key</c>：base64(SHA1(key + GUID))。这是
    WebSocket 服务器必须回应的握手响应，标准浏览器与
    库客户端才会接受升级。


## Fuzzy (class)

模糊字符串匹配：用于符号/文件搜索的子序列打分，外加
按查询词对文档排序的 BM25 索引。纯算法实现，
不依赖平台。

double score = Fuzzy.Score("flst", "FileList");   // 0.75
bool hit = Fuzzy.Matches("zanc", "build/zanc.exe");  // true

Bm25Index idx = new Bm25Index();
idx.Add(1, "the quick brown fox");
List<string> hits = idx.Search("quick fox");     // ["1"]

- static bool Matches(string query, string candidate)
  - 当 `query` 作为 `candidate` 的子序列出现时返回 true
    （字符按顺序、忽略大小写）。

- static double Score(string query, string candidate)
  - [0,1] 的相似度：完全匹配（忽略大小写）为 1.0，
    有间隔则递减。连续片段、词首和驼峰边界有加分
    （因此对 "FileList"，"flst" 优于 "flts"）。
    `query` 不是子序列时返回 0。

- static bool IsBoundary(string s, int i)

- static bool IsLetter(string ch)

- static bool IsUpper(string ch)

- static bool IsLower(string ch)

- static bool SameChar(string a, string b)

- static bool SameStr(string a, string b)

- static bool SameStrIgnoreCase(string a, string b)
  - 忽略大小写的相等：完全匹配的定义（Score 承诺 1.0）
    与 `Matches` 一样不区分大小写。


## Markdown (class)

面向应用文本的小型确定性 Markdown 转 HTML 渲染器。

<c>Markdown.ToHtml(text)</c> 支持 ATX 标题、段落、块级
引用、有序和无序列表、三反引号围栏代码、行内
代码、斜体、粗体、链接及反斜杠转义。输出
紧凑：块级元素以换行分隔，段落的软换行
变为空格。

刻意不兼容 CommonMark：块不嵌套，列表项和
引用各占一行源码，围栏信息字符串被忽略，行内
定界符取第一个匹配的闭合符。未闭合的定界符原样输出，
未闭合的围栏把剩余输入当作代码。
源文本、代码、标签和链接属性均做 HTML 转义。链接 URL
必须是相对路径，或使用 <c>http</c>、<c>https</c>、<c>mailto</c>
协议；含 ASCII 控制字符的 URL 只渲染为标签文本，
不生成锚点链接。

- static string ToHtml(string text)
  - 将受支持的 Markdown 语法渲染为紧凑的转义 HTML。

- static bool Block(StringBuilder html, bool first, string block)

- static string Inline(string text)

- static string InlineAt(string text, int depth)

- static bool SafeUrl(string url)

- static string EscapeHtml(string text)

- static bool IsBlockStart(string line)

- static int HeadingLevel(string line)

- static bool IsQuote(string line)

- static int ListKind(string line)

- static int ListBody(string line, int kind)

- static List<string> Lines(string text)

- static string Trim(string text)

- static bool IsSpace(string ch)

- static bool StartsWith(string text, string prefix)

- static int Find(string text, string needle, int from)


## Pinyin (class)

Converts Chinese characters to pinyin (the official romanization) for
search, sorting and abbreviation matching — the same job the IDE's
symbol/file search does. Covers every GB2312 hanzi (level 1 + level 2,
6763 characters); polyphones use their most common reading, which is what
search/ordering consumers want.

string py = Pinyin.ToPinyin("中文");     // "zhongwen"
string ab = Pinyin.Initials("中文");     // "ZW"
string py = Pinyin.ToPinyin("Zan 语言"); // "Zan  yuyan"

Non-hanzi characters pass through unchanged. The lookup table is built
lazily on first use and cached.

- static Dictionary <string, string> cache;

- static string ToPinyin(string text)
  - The full pinyin of `text`: every hanzi becomes its pinyin,
    everything else (letters, digits, spaces, punctuation) stays as-is.
    Returns "" for empty input.

- static string Initials(string text)
  - The first letter of each hanzi's pinyin, uppercased; other
    characters pass through. "中文" -> "ZW", "hello 世界" -> "hello S".

- static string ToPinyinChar(string ch)
  - The pinyin of a single hanzi (pass any one-character string).
    Returns "" when `ch` is not a hanzi in the table.

- static Dictionary <string, string> Table()

- static Dictionary <string, string> Build()

- static string Lookup(string han)

- static int Utf8Width(string ch)
  - UTF-8 byte width of the first character: 1 for ASCII,
    2/3/4 for multi-byte sequences.

- static string Data()


## Template (class)

基于 Dictionary<string,string> 的简单字符串模板替换。

Dictionary<string,string> vars = new Dictionary<string,string>();
vars["name"] = "**zan**";
string out = Template.Render("Hi {{name}}!", vars);   // "Hi **zan**!"
string raw = Template.RenderRaw("Hi {{name}}!", vars);// "Hi **zan**!"

语法：<c>{{key}}</c> 对值做 HTML 转义，<c>{{{key}}}</c>
原样插入；未知键展开为 ""。同一替换核心
支撑 System.Web.View（后者在其上增加对行列表的 #if/#each）。

- static string Render(string text, Dictionary <string, string> vars)
  - 渲染 `text`，对每个 <c>{{key}}</c> 的值做 HTML 转义；
    <c>{{{key}}}</c> 原样插入。

- static string RenderRaw(string text, Dictionary <string, string> vars)
  - 渲染 `text` 且不做任何转义（<c>{{key}}</c> 和
    <c>{{{key}}}</c> 均原样插入值）。

- static string EscapeHtml(string s)
  - 对字符串做 HTML 转义以便安全插值：
    <c>& < > " '</c>。

- static string Expand(string text, Dictionary <string, string> vars, bool escape)

- static string Lookup(string key, Dictionary <string, string> vars)

- static string Trim(string s)

- static int Find(string hay, string needle, int from)


## TextTable (class)

将行对齐成文本表格，用于控制台/CLI 输出。

TextTable t = new TextTable(new string[] { "Name", "Count" });
t.AddRow(new string[] { "a", "1" });
t.AddRow(new string[] { "longer", "42" });
Console.WriteLine(t.Render());

渲染结果：
+--------+-------+
| Name   | Count |
+--------+-------+
| a      | 1     |
| longer | 42    |
+--------+-------+

- List<string> headers;

- List <List<string>> rows=new List <List<string>>();

- int[]widths;

- TextTable(List<string> headers)

- void AddRow(List<string> row)
  - 追加一行。比表头短的行
    以空单元格补齐；更长的行则被截断。

- string Render()
  - 渲染对齐后的表格（ASCII 边框）。

- static string Border(int[]widths)

- static string Line(List<string> cells, int[]widths)
