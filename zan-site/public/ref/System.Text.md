# System.Text

> 源码: `packages/Zan.Text/src/System/Text/Bm25Index.zan`, `packages/Zan.Text/src/System/Text/Csv.zan`, `packages/Zan.Text/src/System/Text/FuzzyMatching.zan`, `packages/Zan.Text/src/System/Text/Markdown.zan`, `packages/Zan.Text/src/System/Text/Pinyin.zan`, `packages/Zan.Text/src/System/Text/Template.zan`, `packages/Zan.Text/src/System/Text/TextTable.zan`, `stdlib/System/Text/Encoding.zan`


## Bm25Document (class)

索引内的一篇文档：id、词频表与总词元长度。

- int id;
  - 文档 id（调用方指定）。

- Dictionary <string, int> terms;
  - 词元 -> 词频。

- int length;
  - 词元总数（词频之和），BM25 长度归一用。

- Bm25Document(int id, Dictionary <string, int> terms, int length)
  - 构造文档记录。


## Bm25Hit (class)

一次命中的内部记分记录。

- int id;
  - 文档 id。

- double score;
  - BM25 分数。

- Bm25Hit(int id, double score)
  - 构造命中记录。


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
  - 全部已索引文档。

- Dictionary <string, int> df=new Dictionary <string, int>();
  - 词元 -> 含它的文档数（文档频率）。

- int totalLen;
  - 全部文档的词元长度之和。

- double k1;
  - BM25 饱和参数 k1（1.2）。

- double b;
  - BM25 长度归一参数 b（0.75）。

- Bm25Index()
  - 创建空索引，参数取标准值 k1=1.2、b=0.75。

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
  - 分词：转小写、按非字母数字字符切分，返回词元 -> 词频。

- static List<string> UniqueTerms(string query)
  - 查询分词并去重（保持首次出现顺序）。

- static int GetSeen(Dictionary <string, int> seen, string key)
  - seen 字典的取值，缺失为 0。

- static void AddTerm(Dictionary <string, int> terms, string term)
  - 词频表累加一次该词元。

- int DfOf(string term)
  - 词元的文档频率，缺失为 0。

- void IncDf(string term)
  - 文档频率 +1（不存在则置 1）。

- void DecDf(string term)
  - 文档频率 -1，减到 0 时移除。


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
  - 按需给单个字段加引号：含分隔符、引号或换行时用引号包围，
    字段内的引号写成两个；其余原样返回。


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

- static Encoding UTF8 { get }
  - UTF-8 编码入口（C# Encoding.UTF8 的对应形态）。
    Zan 字符串本身就是 UTF-8 字节串，因此编码即原样拷贝，
    解码即校验/清洗后原样还原。

- static byte[]GetBytes(string text)
  - 把字符串编码为 UTF-8 字节（等价 s.ToBytes()，null 收敛为
    空串）。返回的 byte[] 长度即字节数。

- static string GetString(byte[]bytes, int offset, int count)
  - 把 UTF-8 字节解码为字符串：从 <paramref name="offset"/>
    起取 <paramref name="count"/> 个字节。非法字节序列（截断的多字节
    头、10xxxxxx 续字节落单）逐个替换为 U+FFFD，与 C#
    Encoding.UTF8.GetString 的宽松替换行为一致，绝不产生越界读取。
    count 会被夹进 [0, 剩余字节]，负 offset 收敛为 0。

- static string GetString(byte[]bytes)
  - GetString(bytes, 0, bytes.Length) 的便捷形态。

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

- static int DecodeCodePoint(string s, int byteIndex, int[]cpOut)
  - 从字节偏移 <paramref name="byteIndex"/> 起解码一个
    Unicode 码点，写入 <paramref name="cpOut"/>（长度 ≥1 的 int 数组
    ——Zan 无出参，借用数组回传）并返回该码点占用的字节数
    （1-4）。byteIndex 越界返回 0，cpOut[0] 不动。非法序列
    （截断的多字节头、落单续字节、过长编码）返回 1、
    cpOut[0] = U+FFFD，与 `GetString` 的替换行为一致。

- static string RuneAt(string s, int byteIndex)
  - 从字节偏移 <paramref name="byteIndex"/> 起解码一个码点
    并直接编码回 1-4 字节 UTF-8 字符串（等价 DecodeCodePoint 后接
    Utf8FromCodePoint）。非法序列收敛为 U+FFFD 单字符。

- static int CodePointCount(string s)
  - 字符串中的码点个数（≠ Length 字节数，非 ASCII 时更
    小）。非法序列按 1 码点计。

- static string RuneFromIndex(string s, int runeIndex)
  - 取第 <paramref name="runeIndex"/> 个码点（0 起），
    编码为 UTF-8 字符串返回；越界返回空串。O(n) 顺序扫描。

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

- static bool SameByte(int a, int b)

- static bool Matches(string query, string candidate)
  - 当 `query` 作为 `candidate` 的子序列出现时返回 true
    （字符按顺序、忽略大小写）。

- static double Score(string query, string candidate)
  - [0,1] 的相似度：完全匹配（忽略大小写）为 1.0，
    有间隔则递减。连续片段、词首和驼峰边界有加分
    （因此对 "FileList"，"flst" 优于 "flts"）。
    `query` 不是子序列时返回 0。

- static bool IsBoundary(string s, int i)
  - 判断 s[i] 是否落在词边界上：边界指紧跟非字母的字母，
    或紧邻大写字母的小写字母（驼峰）。i 为 0 视为词首，返回 true。

- static bool IsLetter(string ch)
  - 判断是否为 ASCII 字母（A-Z、a-z），只看首字符的低 8 位。

- static bool IsUpper(string ch)
  - 判断是否为 ASCII 大写字母（A-Z），只看首字符的低 8 位。

- static bool IsLower(string ch)
  - 判断是否为 ASCII 小写字母（a-z），只看首字符的低 8 位。

- static bool SameChar(string a, string b)
  - 忽略大小写比较两个单字符（转小写后判等）。

- static bool SameStr(string a, string b)
  - 区分大小写的字符串相等（`==` 的具名包装）。

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
  - 追加一个块级元素；非首块前置 "\n"。返回后续调用的
    first 值（恒 false）。

- static string Inline(string text)
  - 行内语法（代码/粗体/斜体/链接/转义）渲染入口。

- static string InlineAt(string text, int depth)
  - Inline 的递归实现：`depth` 限制嵌套层数，超限时整段只做
    HTML 转义。未闭合的定界符按字面输出。

- static bool SafeUrl(string url)
  - 链接 URL 是否可生成锚点：相对路径，或 http/https/mailto
    协议，且不含 ASCII 控制字符。

- static string EscapeHtml(string text)
  - HTML 转义：< > & " '。

- static bool IsBlockStart(string line)
  - 该行是否开始一个新块（围栏代码/标题/列表/引用），
    段落累积据此停止。

- static int HeadingLevel(string line)
  - ATX 标题级别（1-6，"#" 后须有空格）；非标题返回 0。

- static bool IsQuote(string line)
  - 该行是否为块级引用（以 ">" 开头）。

- static int ListKind(string line)
  - 列表行类别：1 无序（"- "/"* "/"+ "），2 有序（"N. "），
    非列表行返回 0。

- static int ListBody(string line, int kind)
  - 列表行中项正文（"" 内容）的起始下标。

- static List<string> Lines(string text)
  - 按 CR/CRLF/LF 把文本切成行；结尾无换行也补最后一行。

- static string Trim(string text)
  - 去除两端空白（空格、制表、换行）。

- static bool IsSpace(string ch)
  - 字符是否为空白（空格/制表/CR/LF）。

- static bool StartsWith(string text, string prefix)
  - 前缀匹配。

- static int Find(string text, string needle, int from)
  - 从 from 起查找子串，找不到返回 -1。


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

The dictionary itself lives in a data file (`pinyin.txt`, one
"汉字拼音" line per GB2312 hanzi), not in compiled code — like the Gui
icon packs it is resolved at run time through a discovery chain: env
`ZAN_PINYIN_DATA` (file), `pinyin.txt` beside the exe, embedded resource
`text/pinyin.txt` (zanc bakes the stdlib copy into every published
program carrying this module), then the stdlib source tree for source
checkouts. A replacement file overrides the baked-in table without
recompiling anything. Deliberately no System.IO dependency here: this
module must stay tiny for every program that merely touches pinyin.

- static Dictionary <string, string> cache;
  - Lazily built "hanzi -> pinyin" table (null until first lookup).

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
  - The cached table, built on first use.

- static Dictionary <string, string> Build()
  - Parse the "汉字拼音" dictionary text (one hanzi+pinyin per line)
    into the table; short or malformed lines are skipped.

- static string Lookup(string han)
  - Pinyin of one hanzi, or "" when it is not in the table.

- static int Utf8ByteWidth(int c0)
  - UTF-8 byte width of the first byte: 1 for ASCII,
    2/3/4 for multi-byte sequences.

- static int Utf8Width(string ch)
  - UTF-8 byte width of the first character: 1 for ASCII,
    2/3/4 for multi-byte sequences.

- [DllImport("crt")]static extern string getenv(string name);

- [DllImport("crt", EntryPoint="zan_file_fopen")]static extern nint zan_pkg_fopen(string path, string mode);

- [DllImport("crt")]static extern int fclose(nint fp);

- [DllImport("crt")]static extern long fread(byte[]buf, long size, long count, nint fp);

- [DllImport("crt")]static extern int zan_embed_has(string name);

- [DllImport("crt")]static extern string zan_embed_read(string name);

- static string Env(string name)
  - getenv wrapper returning "" instead of null.

- static string ReadSmallFile(string path)
  - Read a whole small text file via raw CRT handles — the same
    primitives File.ReadAllText uses, minus the System.IO dependency.

- static string ExeDir()
  - Directory of the running executable ("" when unknown);
    resolved through the same primitives ProcessHost.SelfExe uses.

- [DllImport("kernel32", EntryPoint="GetModuleFileNameW")]static extern int GetModuleFileNameW(nint hModule, nint buf, int size);

- [DllImport("kernel32", EntryPoint="WideCharToMultiByte")]static extern int ToMulti(int page, int flags, nint wide, int wideLen, nint mb, int mbLen, nint defChar, nint usedDef);

- static string WideToStr(nint p)
  - NUL-terminated UTF-16 buffer -> Zan string (mirrors
    Wide.Read; kept local to avoid a System dependency).

- [DllImport("crt")]static extern int readlink(string path, byte[]buf, int size);

- static bool ReadableFile(string path)
  - True when the path opens as a readable file. (fopen on a
    directory succeeds on POSIX, but the discovery chain only ever asks
    for concrete file names, so a handle check is enough here.)

- static bool EmbedExists(string path)
  - True when the named embedded resource exists.

- static string LoadData()
  - Load the "汉字拼音" dictionary text through the same kind of
    discovery chain the Gui icon packs use: env override, a file beside
    the exe, embedded resources, the stdlib source tree. Returns "" when
    nothing is found — the table then stays empty and lookups pass hanzi
    through unchanged.


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
  - 替换核心：扫描 <c>{{key}}</c> / <c>{{{key}}}</c> 占位符，
    键两侧的空白会被剥掉。<c>{{{key}}}</c> 无论 escape 与否都原样插入；
    <c>{{key}}</c> 在 escape 为 true 时对值做 HTML 转义。
    占位符未闭合（找不到对应的 <c>}}</c> / <c>}}}</c>）时渲染在
    该处截断，剩余文本与占位符一并丢弃。

- static string Lookup(string key, Dictionary <string, string> vars)
  - 查占位符的值：vars 为 null 或键不存在时返回 ""（渲染不报错）。

- static string Trim(string s)
  - 剥掉 s 首尾的空白（空格、制表符、换行、回车）。

- static int Find(string hay, string needle, int from)
  - 返回 needle 在 hay 中自 from（含）起首次出现的下标，找不到返回 -1。


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
  - 渲染分隔线：每列按 widths[i]+2 个 `-` 输出，形如 `+----+---+`。

- static string Line(List<string> cells, int[]widths)
  - 渲染一行：单元格左对齐、按 widths 补空格；cells 缺列时以空单元格补齐。
