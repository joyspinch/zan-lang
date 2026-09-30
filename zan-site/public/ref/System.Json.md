# System.Json

> 源码: `stdlib/System/Json/JsonTape.zan`, `stdlib/System/Json/JsonValue.zan`


## JsonDoc (class)

平面 tape 形式的 JSON 文档：解析是单遍熔合状态机（见
`ParseCore`），位置/缓冲全部参数线程化，错误用
-(位置+1) 返回值直接上抛。字符串 token 解析期不物化——槽里只记
(start,len) 源串偏移，`StrAt` 等访问器首次读取才物化
（代价是 doc 持有 src/buf 引用使其存活）。
树形 API 见 `ToJson` / `At` / `Get`。

- public List<JsonSlot> slots;
  - tape 槽位（外部走访问器）。

- public List<JsonPair> pairs;
  - 全部对象的键值对表（对象槽 a 高位记首对下标）。

- public List<string> escPool;
  - 含转义的字符串/键在此物化（无转义的走 src 零拷贝）。

- public string src;
  - 源串。零拷贝 token 指向它，因此它随 doc 存活。

- public byte[]buf;
  - 源字节缓冲，下标 < len 与字符串下标一一对应；尾部多 32 字节
    零填充供 Load64 裸读兜越界。键比较走它——数组索引比 string
    索引快约 7 倍。

- public int root;
  - 根槽位。

- static JsonDoc Parse(string src)

- static int ParseCore(List<JsonSlot> sl, List<JsonPair> pr, List<string> pool, string src, byte[]buf, int len)
  - 熔合解析核心：单函数显式状态机。错误 -(位置+1) 语义与旧递归
    实现逐点一致（字面量首错、容器收口、尾随内容、EOF 均同位）。
    深度语义：容器嵌套仍以 MaxDepth 封顶（推栈时检查）；熔合后无
    递归，标量不再单独检查深度（旧实现对 512 层数组的直接标量
    元素报错、对同层对象成员不报，本身不对称，统一为不报）。
    熔合解析核心 v2：无状态变量。值完成后就地记账并级联处理分隔
    符与逐层收口（不再回主循环重新分发——状态分发是间接跳转，
    交替模式预测器吃亏，v1 实测比旧递归还慢），仅对象"要键"时
    切 wantKey 一个布尔。空容器免推栈快路径。MaxDepth 进寄存器。
    错误 -(位置+1) 语义与旧递归实现逐点一致。

- static int ScanKey(List<JsonSlot> sl, List<JsonPair> pr, List<string> pool, string src, byte[]buf, int len, int pos, int pbase, int[]fr)
  - 对象键 token 扫描（起始引号在 pos）：SIMD 向量查找闭引号与转义符。

- static int EscStringInto(List<string> pool, string src, byte[]buf, int len, int start, int esc)
  - 转义 token 物化：前缀 (start..esc) 原文进池，再从 esc（首个
    反斜杠位置）扫到闭引号，解码全部转义（含 \uXXXX 与代理对）。
    返回闭引号后位置或 -(错误位置+1)；调用方以 pool.Count-1 取本
    串下标。值与键共用，冷路径。

- static double DoubleInRange(byte[]buf, int from, int to)
  - 就地解析 [from, to) 的浮点 token。运算顺序与
    JsonValue.DoubleOf 完全一致（整数部分累乘、小数按 0.1 递减
    权、指数逐次 ×/÷10），结果位级相同。

- static int Hex4(string src, int at, int limit)
  - 从 `at` 开始的 4 位十六进制值；不足 4 位或含非十六进制
    字符时返回 -1。

- static int MaxDepth=512;
  - 递归深度上限（与 JsonValue 一致）。

- int SlotCount()

- int NextSibling(int at)

- int FirstChild(int at)

- int KindAt(int at)

- int At(int at, int i)
  - 注意：At 是从首个孩子跳 i 次 NextSibling 的链式访问，取第 i 个
    孩子要 O(i) 次；把它当随机索引用在顺序循环里整体退化为 O(n²)
    （215 倍实测差距）。顺序遍历请走 FirstChild/NextSibling 链或
    `ForEachChild`，只有真正的随机访问才用 At。

- void ForEachChild(int at, JsonChildVisitor fn)
  - 按 tape 链遍历 <paramref name="at"/> 的全部孩子，逐个
    调用 <paramref name="fn"/>（at 是孩子槽位；对象成员是值槽位，
    键用 `KeyAt` 取）。顺序与源文档一致。这是 O(孩子数)
    的正确遍历姿势：FirstChild/NextSibling 各走一步，不像
    `At` 顺序循环那样累计 O(n²)。非容器（标量）与
    负槽位没有孩子，回调一次都不触发；fn 为 null 时静默返回。

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


## JsonReader (class)

驱动 JsonValue.Parse 的源字符串游标（Zan 没有
ref/out 参数，所以位置保存在该对象上）。

- string s;
  - 源字符串。

- byte[]buf;
  - 源字节缓冲（解析期间固定，避免反复 ToBytes）。

- int pos;
  - 当前字节偏移。

- int len;
  - 源长度。

- static int MaxDepth=512;
  - 递归解析/校验的最大嵌套深度。深层嵌套文档会耗尽
    调用栈（约 20-40 万层时解析器崩溃），必须显式限制。

- bool strict;
  - 严格模式（JsonValue.Parse）：边建树边校验，首个错误位置
    记在 errPos 上；宽松模式（ParseLenient）行为保持不变。

- int errPos;
  - 首个语法错误的字节位置；无错误为 -1。

- static JsonReader Of(string src)
  - 创建宽松模式游标。

- static JsonReader OfStrict(string src)
  - 创建严格模式游标（Parse 用）。

- void Fail()
  - 记录首个错误位置（后来的错误不覆盖它）。

- bool Failed()
  - 是否已记录过错误。

- int CurB()
  - 游标处的字节，输入结束时为 -1。

- void SkipWs()
  - 跳过空白（空格/制表/换行/回车）。实测 JSON 文档里空白
    游程通常 0-2 字节，跨 libc 的 strspn 调用开销（PLT + 参数
    布置 ~40ns）比逐字节循环更贵。字节循环处理常见短游程，
    长游程也只是多转几圈，分支预测器对"还是空白"方向的命中
    率极高，实测无惩罚。

- JsonValue ReadValue()
  - 从游标读一个任意 JSON 值（顶层入口）。

- JsonValue ReadValueAt(int depth)
  - ReadValue 的递归实现；`depth` 用于 MaxDepth 保护，超限时
    严格模式记错误，宽松模式返回 null 占位。

- JsonValue ReadObjectAt(int depth)
  - 游标处为 '{' 时读对象；strict 下逐 token 校验，
    错误语义同 ReadValueAt。

- JsonValue ReadArrayAt(int depth)
  - 游标处为 '[' 时读数组；错误语义同 ReadObjectAt。

- string ReadString()
  - 从起始引号读取带引号的字符串；解码常见
    转义。结束后 pos 位于闭合引号之后。无转义的字符串
    （最常见的情况）先 memchr 定位闭合引号/反斜杠，
    再单次 Substring；含转义的字符串则回退到 StringBuilder，
    批量复制转义之间的普通片段。

- static int Hex4(string src, int at, int limit)
  - 从 `at` 开始的 4 位十六进制值；不足 4 位或含非十六进制
    字符时返回 -1。

- JsonValue ReadBool()
  - 读取字面量 true/false（宽松模式）并构造布尔值。

- JsonValue ReadNumber()
  - 读数字 token：数字游程短（中位 2-4 字节），strspn 的调用开销
    反而更贵，用字节循环吃 token，然后就地解析——纯整数存 numI，
    含 . / e 的存 numD。不再保留字面量字符串（AsString/ToJson
    的输出文本按需生成），每 token 少一次分配、读取侧免二次解析。

- bool ValidLiteral(string lit)
  - 校验游标处是字面量 `lit`（"true"/"false"/"null"）并推进；
    不匹配时不推进返回 false。

- bool ValidNumberAt(int from, int to)
  - 校验 [from, to) 是 JSON 数字语法（可选负号、整数、
    可选小数、可选指数）；语法不符返回 false（不推进——
    游标已由 ReadNumber 的字节循环吃满 token 游程）。


## JsonValue (class)

解析后的 JSON 值树（object / array / string / number / bool / null）。

递归模型，可以遍历嵌套文档（UI 布局树、状态实体）。用
`Parse` 构建；用 As*/类型化字段助手读取标量；
用 Get/Has 遍历对象、At/Count 遍历数组。

kind：0 null，1 bool，2 number，3 string，4 array，5 object。

性能设计（3MB 文档实测 ~280 MB/s，同文档历史实现 28 MB/s）：
逐字节扫描全部下沉到 NativeMemory.ScanNotByte / Find（strspn/memchr，
向量化）；数字在扫描循环里就地解析成 numI/numD，不再保留字面量
字符串（每 token 少一次分配、读取侧少一次重解析）；字符串无转义时
一次 Substring（比逐字节 Zan 循环快一个数量级）；对象键值对
keys/vals 并置（同一次键查找碰一个 Cache 行，不用两趟）。

- int kind;
  - 值类别：0 null，1 bool，2 number，3 string，4 array，5 object。

- bool boolVal;
  - kind 1 时的布尔值。

- long numI;
  - kind 2 时的整数值（无小数/指数部分的 token 直接存这里；
    浮点 token 存 numD，numI 置 0；读取时按 token 形态取，
    没有二次解析）。

- double numD;
  - kind 2 时的浮点值（带 . / e 的 token）。

- bool numIsInt;
  - kind 2 时该 token 是否为纯整数（决定 Int/Long 是否免转换）。

- string numRaw;
  - kind 2 时数字的十进制文本（AsString/ToJson 用）。

- string strVal;
  - kind 3 时的字符串值。

- List<JsonValue> items;
  - kind 4 时的数组元素。

- List<string> keys;
  - kind 5 时的键列表。

- List<JsonValue> vals;
  - kind 5 时与 keys 并置的值列表。

- Dict <string, int> keyIndex;
  - kind 5 时的键索引哈希表（键数 > 8 且被频繁查找时懒加载构建，加速宽对象属性访问）。

- static int MaxDepth=512;
  - 组树（WriteTo/WritePretty）与解析（ReadValueAt）共用的
    嵌套深度上限。序列化同样是递归的：无界深度的树会在
    WriteTo 里耗尽调用栈直接段错误，所以与解析对称地限制。

- static JsonValue NewNull()
  - 构造 null 值（kind 0）。

- static JsonValue NewBool(bool b)
  - 构造布尔值（kind 1）。

- static JsonValue NewInt(long n)
  - 构造整数值（kind 2，numIsInt）。

- static JsonValue NewDouble(double d)
  - 构造浮点值（kind 2）。

- static JsonValue NewNum(string raw)
  - 构造数字值（kind 2），保留原始字面量避免浮点往返损失。

- static JsonValue NewStr(string s)
  - 构造字符串值（kind 3）。

- static JsonValue NewArray()
  - 构造空数组（kind 4）。

- static JsonValue NewObject()
  - 构造空对象（kind 5）。

- JsonValue Clone()
  - 深拷贝当前 JsonValue 及其所有子节点（对象和数组递归克隆）。

- bool IsNull()
  - 是否为 null 值（kind 0）。

- bool IsBool()
  - 是否为布尔值（kind 1）。

- bool IsNumber()
  - 是否为数字值（kind 2）。

- bool IsString()
  - 是否为字符串值（kind 3）。

- bool IsArray()
  - 是否为数组（kind 4）。

- bool IsObject()
  - 是否为对象（kind 5）。

- void Put(string key, JsonValue v)
  - 追加键值对（对象）。不去重。

- bool Has(string key)
  - 对象是否含该 key；不是对象时返回 false。

- JsonValue Get(string key)
  - `key` 对应的值；不存在或不是对象时返回 null。
    键数量 <= 8 时走连续内存线性扫描（零额外内存、缓存行友好）；
    键数量 > 8 时自动激活懒加载哈希索引，O(1) 访问。

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

- void PutStr(string key, string v)
  - 追加字符串成员（null 值写成 JSON null，与字符串 "null" 区分）。

- void PutInt(string key, long v)
  - 追加整数成员。

- void PutDouble(string key, double v)
  - 追加浮点成员。

- void PutBool(string key, bool v)
  - 追加布尔成员。

- void PutNull(string key)
  - 追加 null 成员。

- void PutJson(string key, string jsonText)
  - 把一段已序列化的 JSON 文本作为成员值嵌入：能解析就挂
    解析出的子树（对象/数组/数字原样保型），解析失败则整段按字符串
    存——任何输入都不可能产出非法 JSON。用于收编「片段拼接」：把
    上游序列化好的 items/errors 等挂进外层报文，不再 Substring(1)
    之类按文本外科手术拼。

- void AppendStr(string v)
  - 数组末尾追加字符串元素（null 按空串存，数组元素无 null 语义）。

- void AppendInt(long v)
  - 数组末尾追加整数元素。

- void AppendDouble(double v)
  - 数组末尾追加浮点元素。

- void AppendBool(bool v)
  - 数组末尾追加布尔元素。

- void AppendJson(string jsonText)
  - 数组末尾嵌入一段已序列化 JSON 文本（语义同 PutJson）。

- static JsonValue ParseOrStr(string jsonText)
  - ParseLenient 的无异常形态：能解析返回子树；解析失败或
    结果是 null 字面量时返回字符串形态——用于 PutJson/AppendJson 的
    「片段嵌入」语义，保证任何输入都得到一个合法 JSON 值。

- void Append(JsonValue v)
  - 数组末尾追加元素（数组）。

- int Count()
  - 元素数量（数组元素或对象条目数；其他情况为 0）。

- JsonValue At(int i)
  - 第 i 个数组元素。

- string KeyAt(int i)
  - 第 i 个对象的 key（用于遍历对象）。

- JsonValue ValAt(int i)
  - 第 i 个对象的值（与 KeyAt 配对遍历）。

- string ToJson()
  - 将该值序列化为紧凑 JSON 字符串。字符串和
    对象 key 经 Encoding.JsonEscape 转义；数字按 token 形态
    输出（整数走整型格式化，浮点走最短 double 格式化）。
    是 Parse 对值模型的逆操作。嵌套超过 JsonValue.MaxDepth
    层时抛出 JsonException——WriteTo 是递归的，无界深度的树
    会耗尽调用栈直接段错误（与 Parse 的 MaxDepth 对称）。

- void WriteTo(StringBuilder sb)
  - WriteToAt 的入口（depth 从 0 起）。

- void WriteToAt(StringBuilder sb, int depth)
  - WriteTo 的递归实现：`depth` 用于 MaxDepth 保护。

- static string NumText(JsonValue v)
  - 数字的输出文本：解析时就地解析的整数/浮点走格式化，
    NewNum 手工构造的值保留原始字面量。

- string PrettyToJson()
  - 序列化为便于阅读的多行 JSON 字符串（两空格
    缩进，每个 key 一行），用于手工编辑的交换文件，如
    .zscene 设计文档。与 ToJson 一样可通过 Parse 往返。
    嵌套超限时与 ToJson 一样抛 JsonException。

- void WritePretty(StringBuilder sb, int depth)
  - PrettyToJson 的递归实现；嵌套超限时抛 JsonException。

- static string PrettyIndent(int depth)
  - 生成 depth 层两空格缩进串。

- static int Digit(string ch)
  - 十进制数字字符的值，非数字返回 -1。

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
  - AsString：字符串原样返回；数字输出 NumText 同源文本；布尔返回
    "true"/"false"；其他类型退回 `dflt`。

- int AsInt(int dflt)
  - AsInt：数字按 token 形态取（纯整数免解析）；布尔 1/0；
    字符串解析整数前缀；其余退回 `dflt`。

- long AsLong(long dflt)
  - AsLong：同 AsInt 但按 64 位。

- double AsDouble(double dflt)
  - AsDouble：数字按 token 形态取；布尔 1/0；字符串解析；
    其余退回 `dflt`。

- bool AsBool(bool dflt)
  - AsBool：布尔原样；数字按非 0 为真；字符串仅 "true" 为真；
    其余退回 `dflt`。

- string Str(string key, string dflt)
  - `key` 的字符串值，缺失或类型不符时退回 `dflt`。

- int Int(string key, int dflt)
  - `key` 的整数值，缺失或类型不符时退回 `dflt`。

- string PathStr(string path, string dflt)
  - 点分路径的类型化读取：路径上任何一段缺失（PathGet 返回 null）都
    退回 `dflt`，而不是让调用方对 null 调 AsString/AsInt 而崩溃。

- int PathInt(string path, int dflt)
  - 同 PathStr，整数版。

- long Long(string key, long dflt)
  - `key` 的 64 位整数值，缺失或类型不符时退回 `dflt`。

- bool Bool(string key, bool dflt)
  - `key` 的布尔值，缺失或类型不符时退回 `dflt`。

- double Double(string key, double dflt)
  - `key` 的浮点值，缺失或解析不出时退回 `dflt`。

- static JsonValue ParseNumberToken(string t, int from, int to)
  - 就地解析 [from, to) 的 JSON 数字 token：无 . e 且能安全装进
    long 的走整数快路径，否则走浮点。

- static JsonValue Parse(string src)
  - 将 JSON 文档解析为值树。格式错误时抛出
    JsonException（与 C# 一致）。校验与建树在同一遍扫描里
    完成：出错只记录位置并停止，抛异常前先丢掉部分树，
    所以错误路径依然不持有存活分配。

- static JsonValue ParseLenient(string src)
  - 尽力而为的解析：输入格式错误时返回已解析的部分
    而不抛异常（用于手工编写/工具生成的 UI 文档，部分
    结果树比异常更合适）。


## void (delegate)

`JsonDoc.ForEachChild` 的回调：收到每个孩子的槽位
（数组元素或对象成员值）。

`delegate void JsonChildVisitor(int childSlot);`


## JsonPair (struct)

对象键值对：键为 doc.src 里的零拷贝 token（keyStart >= 0）或
escPool 里物化的转义串（keyStart == -1，keyLen 是 pool 下标），
加同对象内的下一对与值槽位。键查找走这张紧凑表（16B 连续），
不碰槽带。（刻意全是 int：List<struct> 每次 indexer 读都会按位
拷贝，带 ARC 指针字段就会每次 retain/release，纯 POD 才能当数组用。）

- public int keyStart;

- public int keyLen;

- public int nextPair;

- public int valueSlot;


## JsonSlot (struct)

一个 JSON 槽位（tape 上的一个节点）。JsonDoc 的 slots 列表就是
解析出的整棵树，深度优先连续排布：标量内联在槽里，孩子紧随
容器槽之后。字段编码（16 字节，`JsonSlot.a` 低 8 位
是值类别，其余位按类别复用；b 是唯一的载荷字段）：
- null/bool（kind 0/1/6）：a 即 kind，一位信息折进编码，b = 0；
- 整数（kind 2，纯整数 token）：完整 64 位值放 `JsonSlot.b`；
- 浮点（kind 7，带 . / e 的 token）：IEEE 754 位型经
NativeMemory.AsI64 折进 `JsonSlot.b`（AsF64 取回）；
- 字符串（kind 3）：无转义 token 零拷贝——a 高位记 src 起始偏移、
b 记长度；含转义（无法零拷贝）a 高位记 -1，b 记 escPool 下标；
- 数组（kind 4）/ 对象（kind 5）：b = 元素数 | (跳过偏移 << 31)，
跳过偏移是"容器整体之后第一个槽位"的相对距离。
`JsonDoc.KindAt` 把 6/7 归一化回 0..5 对外契约。

- public long a;
  - 低 8 位 kind：0 null，1 bool(true)，2 number(整数 token)，
    3 string，4 array，5 object，6 bool(false)，7 number(浮点 token)。
    高位：kind 3 无转义串的 src 起始偏移（含转义 -1）；
    kind 5 首个键值对在 pairs 表里的下标。

- public long b;
  - 唯一载荷字段：kind 2 整数值；kind 7 浮点的 IEEE 754 位型；
    kind 3 串长或 escPool 下标；kind 4/5 元素数 | (跳过偏移 << 31)。
