# System.Text.RegularExpressions

> 源码: `stdlib/System/Text/RegularExpressions/Match.zan`, `stdlib/System/Text/RegularExpressions/Regex.zan`, `stdlib/System/Text/RegularExpressions/RegexProgram.zan`


## Match (class)

一次正则匹配的结果：起始位置、覆盖范围，
以及各捕获组捕获的文本。

Match m = re.Find("order 42");
if (m.Success()) {
Console.WriteLine(m.Value() + " at " + m.Index());
Console.WriteLine(m.Group(1));
}

组 0 为整个匹配。未参与的组
返回 Index -1，值为 ""。

- bool success;

- string input;

- List<int> caps;

- int groups;

- Match()

- static Match Failure()
  - 构造失败的匹配（Find 在无匹配时返回它）。

- static Match FromCaps(string input, List<int> caps, int groups)
  - 从原始捕获偏移构造匹配。

- bool Success()
  - 模式匹配成功时返回 true。

- int Index()
  - 整个匹配的起始偏移，无匹配时为 -1。

- int Length()
  - 整个匹配的长度，无匹配时为 0。

- string Value()
  - 匹配的文本，无匹配时为 ""。

- int GroupCount()
  - 组的总数，含组 0。

- int GroupIndex(int n)
  - 组 `n` 的起始偏移；组未
    参与时返回 -1。

- int GroupLength(int n)
  - 组 `n` 的长度，未参与时为 0。

- string Group(int n)
  - 组 `n` 的文本，未参与时为 ""。

- bool GroupSuccess(int n)
  - 组 `n` 参与了匹配时返回 true。


## Regex (class)

正则表达式：搜索、提取、替换和分割。

Regex re = Regex.Compile("(\\w+)=(\\d+)");
Match m = re.Find("port=8080");
string key = m.Group(1);
string text = re.Replace("port=8080", "$2:$1");

模式只编译一次（指令程序，见 RegexProgram），
之后可对多个输入匹配。匹配采用最左优先
回溯，与 Perl/.NET 相同：`a|ab` 优先 `a`，贪婪量词尽量多取，
需要时再让出；其惰性 `?` 形式
则相反。

匹配以 **UTF-8 码点** 为单位：`.` 吃掉整个字符（"中" 而不是它的
第一个字节），字符类里可以直接写非 ASCII 字符（`[中文]`）。

支持的语法：字面量、`.`、字符类 `[a-z]` / `[^a-z]`（类内可用
`\d \D \w \W \s \S` 与 POSIX `[[:digit:]]` 等）、
`\d \D \w \W \s \S \b \B \A \z \Z \G`、
转义 `\n \r \t \f \v \a \e \0 \xHH \x{...} \uXXXX \cX`、
分组 `( )` `(?: )` `(?<name> )`、原子组 `(?> )`、注释 `(?# )`、
环视 `(?= ) (?! ) (?<= ) (?<! )`（回顾须定长）、交替 `|`、
量词 `* + ? {n} {n,} {n,m}`（均有惰性形式）、锚点 `^ $`、
反向引用 `\1`-`\99` 与 `\k<name>`，以及只能写在模式开头的
内联标志 `(?i)`（ASCII 大小写）、`(?m)`（`^ $` 匹配行边界）、
`(?s)`（`.` 也匹配换行）。

大小写折叠与 `\d \w \s` 都只覆盖 ASCII；`\p{...}`、占有量词、
八进制转义、作用域内联标志 `(?i:...)` 等未实现的语法在
**编译期报错**，不会被当成普通字符"接受但永不匹配"。

格式错误的模式在编译期抛出 ArgumentException。为避免病态模式
失控，匹配器设有回溯步骤预算：耗尽时抛出
`RegexBudgetException`，而不是把超时伪装成"无匹配"。

- RegexProgram prog;

- string pattern;

- Regex()

- static Regex Compile(string pattern)
  - 编译 `pattern`，格式
    非法时抛出 ArgumentException。

- static Regex CompileOptions(string pattern, bool ignoreCase)
  - 编译 `pattern`，可选择对 ASCII 字母
    不区分大小写。

- static string Validate(string pattern)
  - `pattern` 非法的原因，编译通过则为 ""。
    让调用方无需捕获异常即可检查用户输入的模式。

- string Pattern()
  - 此实例编译所用的模式。

- int GroupCount()
  - 捕获组数量，含组 0。

- int GroupNumber(string name)
  - 命名组 `name` 的组号，没有该名字时返回 -1。

- List<string> GroupNames()
  - 所有命名组的名字。

- bool IsMatch(string input)
  - 模式在 `input` 任意位置匹配时返回 true。

- Match Find(string input)
  - 最左侧的匹配；无匹配则返回失败的 Match。

- Match FindFrom(string input, int start)
  - 从 `start` 处或之后开始的最左侧匹配。

- int ScanFrom(nint buf, int len, int start, List<int> caps)
  - 在已经准备好的字节缓冲区上从 `start` 起找最左匹配。
    返回 0 表示成功（`caps` 已填好），-1 无匹配，-2 预算耗尽。
    Matches/Replace 共用同一个缓冲区，避免每找到一个匹配就把整个
    输入重新分配加拷贝一遍（原先是 O(n²)）。

- void Overflow(int start)
  - 回溯预算耗尽：把它作为错误报出去，调用方才能
    区分"这个模式在这段输入上代价失控"和"确实没有匹配"。

- List<Match> Matches(string input)
  - 从左到右的所有不重叠匹配。空匹配会前进一个码点，
    保证扫描必然终止。

- int Count(string input)
  - 不重叠匹配的数量。

- string Replace(string input, string replacement)
  - 替换所有匹配。`replacement` 里 `$1` / `${1}` /
    `${name}` 是捕获组，`$&` 是整个匹配，`$$` 是字面量 '$'。

- string ReplaceFirst(string input, string replacement)
  - 仅替换第一个匹配。

- string Expand(string replacement, Match m)
  - 按 `replacement` 里的 `$1` / `${12}` / `${name}` /
    `$&` / `$$` 展开替换文本。供"先匹配、再单独构建替换串"的场景
    使用（例如 FindBar 逐个替换时只取展开后的文本喂给编辑器），规则与
    Replace / ReplaceFirst 内部的展开完全一致。

- string ExpandFor(string replacement, Match m)
  - 在替换字符串中展开 `$1` / `${12}` / `${name}` /
    `$&` / `$$`。无法解析的 `$` 原样保留。

- int RefNumber(string body)
  - `${...}` 里的组号或组名对应的组号，无法解析时 -1。

- static int DigitValue(string d)

- static bool IsDigit(string d)

- List<string> Split(string input)
  - 在每个匹配处分割 `input`。空匹配会被跳过，
    因此能匹配空串的模式不会让结果爆炸。

- static bool Matched(string input, string pattern)
  - 一次性匹配，不保留编译后的模式。

- static string Replaced(string input, string pattern, string replacement)
  - 一次性替换，不保留编译后的模式。

- static string Escape(string text)
  - 转义 `text` 中的元字符，使其
    按字面匹配。


## RegexProgram (class)

模式的编译形态及其执行引擎：模式被解析为小型指令程序
（Thompson 风格：Char / Any / Class / Split / Jmp / Save / 断言），
由回溯解释器对输入执行。

指令存放于四个并行的 int 列表（op / x / y / z），无需为模式的
每个字符分配对象；解释器通过字节缓冲区而非 Substring 读取输入。
匹配的单位是 **UTF-8 码点**：`.`、字符类和字面量都按整个码点
前进，因此 `^.$` 能匹配 "中"、`[中文]` 是合法的类。

这是 `Regex` 的实现细节，请直接使用 Regex。

支持的语法：字面量、`.`、带范围/取反的字符类（类内可用
`\d \D \w \W \s \S` 与 POSIX `[:alpha:]` 等）、`\b \B \A \z \Z \G`、
转义 `\n \r \t \f \v \a \e \0 \xHH \x{HH..} \uXXXX \cX`、
分组 `( )` `(?: )` `(?<name> )`、原子组 `(?> )`、注释 `(?# )`、
环视 `(?= ) (?! ) (?<= ) (?<! )`（回顾须定长）、交替 `|`、
量词 `* + ? {n} {n,} {n,m}`（均有惰性 `?` 形式）、
锚点 `^ $`、反向引用 `\1`-`\99` 与 `\k<name>`，
以及只能出现在模式开头的内联标志 `(?i)(?m)(?s)`。

不支持的语法一律在编译期报错，绝不"接受但永不匹配"。

- static int OpChar()

- static int OpAny()

- static int OpClass()

- static int OpMatch()

- static int OpJmp()

- static int OpSplit()

- static int OpSave()

- static int OpBol()

- static int OpEol()

- static int OpWordB()

- static int OpBackref()

- static int OpBos()

- static int OpEos()

- static int OpGpos()

- static int OpMark()

- static int OpEmptyChk()

- static int OpLook()

- static int OpLookEnd()

- static int LookAhead()

- static int LookAheadNeg()

- static int LookBehind()

- static int LookBehindNeg()

- static int LookAtomic()

- static int MemRange()

- static int MemNotSet()

- List<int> op;

- List<int> ax;

- List<int> ay;

- List<int> az;

- List<int> clsKind;

- List<int> clsLo;

- List<int> clsHi;

- List<int> clsStart;

- List<int> clsCount;

- List<int> clsNeg;

- List<string> groupNames;

- List<int> groupNums;

- int groups;

- int marks;

- bool ignoreCase;

- bool multiline;

- bool dotAll;

- string pattern;

- string error;

- nint pbuf;

- int plen;

- int ppos;

- RegexProgram()

- static RegexProgram Compile(string pattern, bool ignoreCase)
  - 解析 `pattern`。之后检查 `Error`：
    可用时为空字符串，否则为失败原因。

- string Error()
  - 模式解析成功时返回 ""，否则返回失败原因。

- int GroupCount()
  - 捕获组数量，含组 0。

- string Pattern()
  - 此程序编译所用的模式。

- int GroupNumber(string name)
  - 命名组 `name` 的组号，没有该名字时返回 -1。

- List<string> GroupNames()
  - 所有命名组的名字（顺序与出现顺序一致）。

- static int DecodeAt(nint buf, int len, int sp)
  - 解码 `buf + sp` 处的码点，返回 `(码点 << 3) | 字节数`；
    sp 到达末尾时返回 -1。非法字节按 U+FFFD、1 字节处理，这样
    引擎在任意字节串上都能前进而不会卡住。

- static int StepWidth(nint buf, int len, int sp)
  - 从 `sp` 向后推进一个码点所需的字节数（至少 1）。
    扫描下一个起点时用它，避免停在多字节字符的中间。

- static int Utf8Len(int cp)
  - 码点的 UTF-8 字节数。

- int Emit(int o, int x, int y, int z)

- int Here()

- static bool XIsPc(int o)
  - 指令 `o` 的 x 字段是否是指令地址。

- static bool YIsPc(int o)
  - 指令 `o` 的 y 字段是否是指令地址。

- int CopyRange(int from, int to)
  - 复制 [from, to) 区间的指令并追加，重定位跳转目标。
    用于把 `{n,m}` 展开为重复副本。

- int PeekAt(int i)

- int Peek()

- int Next()

- int NextCp()
  - 读取一个码点（多字节字面量按整体处理）。

- void ParseInlineFlags()
  - 只识别位于模式最开头的 `(?imsx...)`。作用域内联标志
    （`(?i:...)`）没有实现，会在 ParseGroup 里明确报错，而不是
    被当成普通组"接受但永不匹配"。

- void ParseAlt()
  - alt := seq ('|' seq)*  ->  split left, right ; left ; jmp end ;
    right ; end。重复的备选分支向左嵌套。

- void ParseSeq()
  - seq := term*

- void ParseTerm()
  - term := atom quantifier?

- void ParseQuantifier(int start)

- int ParseInt()

- void ApplyQuantifier(int start, int min, int max, bool lazy)
  - 把已发射到 [start, end) 的原子改写成其重复形式。

- void AppendBody(List<int> bo, List<int> bx, List<int> by, List<int> bz, int delta)

- void Truncate(int n)

- void StarAt(int start, int len, bool lazy)
  - 将 [start, start+len) 处的主体转换为 `body*`：
    split body,out ; mark ; body ; emptychk out ; jmp split
    mark/emptychk 这一对负责空循环检查：主体这一轮没有前进
    （`(a*)*`）就直接跳出循环，
    否则会无限迭代下去。

- void PlusAt(int start, int len, bool lazy)
  - 将 [start, start+len) 处的主体转换为 `body+`：
    mark ; body ; emptychk out ; split back,out

- void OptionalAt(int start, int len, bool lazy)
  - 将 [start, start+len) 处的主体转换为 `body?`。

- void Shift(int start, int len, int n)
  - 在 [start, start+len) 处的主体前为 `n` 条指令腾出空间，
    并重定位 start 处及之后的跳转目标。

- void ParseAtom()

- void ParseGroup()
  - '(' 已消耗。处理捕获组、`(?:`、`(?#`、`(?=` `(?!`
    `(?<=` `(?<!`、`(?>` 和 `(?<name>`。

- string CharText(int c)

- static string Hex2(int v)

- int NewGroup(string name)
  - 登记一个捕获组，返回组号。

- string ParseGroupName(int closer)

- void ParseCaptureBody(int gi)

- void ParseGroupTail(int gi)
  - 解析组体与 ')'；gi >= 0 时补上收尾的 Save。

- void ParseLook(int kind)
  - 环视 / 原子组：`look ; body ; lookend ; 继续`。

- int FixedWidth(int from, int to)
  - [from, to) 匹配的固定字节数；宽度不定时返回 -1。
    定长回顾靠它算出起点，做不到就编译期报错，而不是
    悄悄给个错答案。

- bool ClassIsAscii(int ci)
  - 类 `ci` 是否只覆盖 ASCII（定长回顾用）。

- int BeginClass(bool negate)
  - 开始一个字符类并返回其索引。

- void AddMember(int ci, int kind, int lo, int hi)

- void AddRange(int ci, int lo, int hi)

- void AddShorthand(int ci, int kind)
  - 添加 `\d \w \s` 简写的范围。`kind` 用小写字母的
    码值表示。

- bool AddPosixClass(int ci, string name, bool negate)
  - POSIX 类 `[:name:]` 的范围。未知名字返回 false。

- void ParseClass()
  - `[` 已消耗。类成员按码点收集，因此 `[中文]` 合法。

- bool ParsePosixInClass(int ci)
  - 解析类内的 `[:name:]` / `[:^name:]`。

- int EscapeValue(int e)
  - 转义序列代表的码点（`\n` -> 10，`\x41` -> 65）。
    反斜杠已消耗，`e` 是其后的那个字节。

- static int HexDigit(int c)

- int ParseHexEscape()

- int ParseFixedHex(int digits)

- int ParseControlEscape()

- void ParseEscape()

- static int Fold(int c)
  - ASCII 大小写折叠。非 ASCII 码点原样返回：Unicode
    折叠需要码表，这里明确只做 ASCII（文档已说明）。

- static bool IsWordCp(int c)

- static bool InShorthand(int kind, int c)

- bool ClassHas(int ci, int c)

- int RunAt(nint input, int len, int start, int origin, List<int> caps)
  - 从 `start` 运行程序。成功后 `caps` 保存
    2 * GroupCount() 个偏移（组未参与时为 -1），返回值为匹配
    结束位置；-1 表示此处无匹配，-2 表示回溯预算耗尽
    （调用方应把它与"无匹配"区分开）。

- bool Step(RegexRun run, int pc, int sp)
  - 回溯解释器：每走一个分支递归调用一次。`budget`
    限制总步数；耗尽时置 `overflow` 并一路返回失败，让调用方
    能报告"预算耗尽"而不是把超时伪装成"无匹配"。


## RegexRun (class)

一次进行中的匹配：输入、正在填充的捕获槽、空循环
检查用的槽位和剩余步骤预算。与 RegexProgram 分离，使编译后的
模式保持不可变，可对多个输入进行匹配。

- nint input;

- int len;

- List<int> caps;

- List<int> marks;

- int budget;

- int end;

- int origin;

- int subEnd;

- bool overflow;

- RegexRun()
