# System.Knowledge

> 源码: `stdlib/System/Knowledge/GalleryIndex.zan`, `stdlib/System/Knowledge/ZformSchema.zan`


## GalleryIndex (class)

gallery 索引生成：从 seed 出发扫描 templates/ 与 examples/ 补全条目（CLI 与 MCP 共用）。

- static GalleryResult Generate(string root, string seedPath)
  - 生成 gallery 索引：以 seed 的 entries 为基底，扫描 templates/ 与
    examples/ 里未被 seed 引用的模板和示例补入条目，topic 重复即抛
    异常；models 节原样保留。

- static void AddRefs(JsonValue e, List<string> refs)
  - 收集条目 files 数组里引用的全部文件路径（去重）。

- static void ScanTemplates(string dir, string root, List<string> refs, List<JsonValue> outp, GalleryResult r)
  - 扫描 templates/<family>/<template>/：带 template.manifest 的目录
    生成一条目（entry 文件 + 同名 .html 设计稿）；已由 seed 覆盖的目录跳过，
    缺 entry 键记告警。

- static void ScanExamples(string dir, string root, List<string> refs, List<JsonValue> outp, GalleryResult r)
  - 递归扫描 examples/ 下 .zan 文件（examples/game 除外）：未被 seed
    引用的生成条目；topic 取文件名（'_'、'/' 归一为 '-'），gui_ 前缀
    的 kind 为 gui-immediate，其余 sample；summary 取文件头注释或
    README 首行。

- static string Manifest(string path, string key)
  - 读 manifest 的 "key=value" 行，返回 value；缺键为空串。

- static string TemplateKind(string family, bool hasDesign)
  - 模板家族 + 是否带 .html 设计稿 → 条目 kind（gui-designer/gui-immediate/
    server/console/library，其余小写家族名）。

- static string TemplateSummary(string manifest)
  - 模板摘要：manifest 的 desc 与 desc.zh 拼接（缺一取其一，全缺取
    manifest 文件名）。

- static string Summary(string path, string dir, string fn)
  - 示例文件的摘要：文件头部第一条 /// 注释，其次第一条 // 注释
    （跳过空行与纯装饰行），再次同目录 README.md 的首个非标题行，
    最后回落文件名。

- static bool Decoration(string s)
  - 是否纯装饰行（仅 = - * _ 空白）。

- static string SameName(string path, string ext)
  - 同名不同扩展名的文件路径；不存在为空串。

- static string Rel(string root, string path)
  - 路径规范化为相对 root 的 POSIX 风格显示路径。

- static string ParentName(string path)
  - 去掉尾部斜杠后的最后一段目录/文件名。

- static string DirOf(string path)
  - 文件路径的目录部分（无目录为空串）。

- static string BaseName(string path)
  - 同 ParentName（基名）。

- static string WithoutExt(string path)
  - 去掉最后一个 '.' 之后扩展名的路径（无扩展名原样返回）。

- static void SortEntries(List<JsonValue> a)
  - 按 topic 字符串插入排序（就地升序）。

- static void CheckTopics(JsonValue a)
  - 校验条目 topic 无重复，重复即抛异常（列出重复项）。

- static List<string> Lines(string s)
  - 按 '\n' 切行（丢弃 '\r'）。

- static bool Has(List<string> a, string s)
  - 列表是否含该字符串。

- static bool HasPrefix(List<string> a, string prefix)
  - 列表里是否有该路径本身或其子路径（按目录前缀）。

- static string Trim(string s)
  - 去除首尾空格/制表符。

- static string Lower(string s)
  - 转小写（转发给 ToLower）。

- static bool Starts(string s, string p)
  - 前缀判断。

- static bool Ends(string s, string p)
  - 后缀判断。

- static int Find(string s, string p, int start)
  - 从 start 起查找子串首次出现，返回下标；无则 -1（空模式命中 start）。

- static string Replace(string s, string a, string b)
  - 全部替换（无正则）。


## GalleryResult (class)

Source driven gallery generation shared by the CLI and MCP server.
Generate 的产物：gallery JSON、条目/发现/模板计数与告警。

- string json;

- int entries;

- int discovered;

- int templates;

- List<string> warnings;

- GalleryResult()


## KnowledgeControl (class)

一个控件类的静态视图：名称、来源位置、样式 Kind、属性/事件清单与工厂标记。

- string name;

- string file;

- int line;

- string styleKind;

- bool designer;

- bool creatable;

- bool generic;

- bool inheritsBase;

- string defaultOf;

- List<KnowledgeProp> props;

- List<string> events;

- KnowledgeControl()


## KnowledgeProp (class)

一个设计器属性的静态视图：键、标签、类型、别名、选项、步长/范围等。

- string variable;

- string key;

- string label;

- string type;

- string aliases;

- string tip;

- string options;

- int step;

- int lo;

- int hi;

- bool range;

- KnowledgeProp()


## ZformResult (class)

Generate 的产物：schema JSON、控件清单文本、控件数与解析告警。

- string json;

- string controlsText;

- int controls;

- List<string> warnings;

- ZformResult()


## ZformSchema (class)

Static source reader for the .zform schema and control catalogue.

- static ZformResult Generate(string root, string stdlib, string docPath)
  - 生成 schema 文档：沿用 docPath 文档骨架的各节，扫描 stdlib 与
    root 下全部 .zan，把声明了 Props() 覆写的控件类整理成 controls
    数组；返回 JSON、按名排序的控件清单与解析告警。

- static void ScanDir(string dir, string root, string stdlib, List<KnowledgeControl> outp, ZformResult result)
  - 递归扫描目录下全部 .zan 文件。

- static void ScanFile(string path, string root, string stdlib, List<KnowledgeControl> outp, ZformResult result)
  - 扫描单个 .zan：只关心声明了 Props() 覆写（override List<PropSpec>
    那个签名）的顶层类，为每个类读出一个控件条目；花括号不平衡记告警。
    注意：本文件注释里不能出现与该签名逐字相同的文本——
    tests/run_zform_schema.cmake 逐行扫描时不排除注释行。

- static KnowledgeControl ReadControl(string name, int line, string path, string root, string stdlib, string header, string body, ZformResult result)
  - 读取一个控件类：Kind() 的样式名、Props() 里的属性声明/链式配置/
    ps.Add 顺序、Events() 里 e.Add 的事件名；是否调用 base.Props()
    记入 inheritsBase。

- static KnowledgeProp ReadProp(string line, int at)
  - 解析一行 "PropSpec.Xxx(...)" 声明：取构造器 kind、key、label 与
    左侧变量名；缺 key/label 或既无变量也非内联 ps.Add 返回 null。

- static void ReadChainText(string line, KnowledgeProp p)
  - 解析一个属性的链式配置：Also（别名，"|" 连接）、WithTip、
    Option（选项，"|" 连接）、Step(step,lo,hi)（范围）与 StepBy(step)。

- static void AddOptions(KnowledgeProp p, string opts)
  - 往 `p.options` 追加一段（"|" 连接，与 Option() 同语义）。

- static JsonValue ControlJson(KnowledgeControl c)
  - 控件条目的 JSON 视图：name/file/line/styleKind/designer/creatable/
    generic/inheritsBase/defaultOf/events（含 common 标记）/props。

- static string factoryKinds="";
  - ControlFactory 里 Kinds()/Create() 方法体的缓存。

- static string factoryCreate="";

- static void ReadFactories(string stdlib)
  - 读入 ControlFactory 的 Kinds()/Create() 方法体，供逐控件判定。

- static void ReadFactories(KnowledgeControl c)
  - 判定单个控件：名字出现在工厂 Kinds() 名单即 designer，
    Create() 里有 "new <名>(" 即 creatable。

- static string DefaultOf(string n)
  - 拥有字符串默认值概念的控件（ListView/Dropdown/DataGrid）；其余为空。

- static string PropType(string k)
  - PropSpec 构造器名 → schema 类型名（未知为空串）。

- static bool Common(string e)
  - 是否所有控件共有的通用事件（点击/键盘/手势/生命周期等固定名单）。

- static string Manifest(List<string> a)
  - 控件名排序后逐行拼接（控件清单文本）。

- static void SortStrings(List<string> a)
  - 插入排序（就地升序；名单很短）。

- static KnowledgeProp ByVar(List<KnowledgeProp> a, List<KnowledgeProp> b, string v)
  - 按变量名在已声明/已排序两份属性列表里查找属性。

- static void AddOrdered(List<KnowledgeProp> a, KnowledgeProp p)
  - 按变量名去重后追加（保持 ps.Add 的声明顺序）。

- static void Warn(ZformResult result, string path, string root, string stdlib, int classLine, int offset, string line, string reason)
  - 记一条解析告警（相对路径:行号: 行内容）。

- static string ReturnString(string body, string method)
  - 提取 "override string Xxx()" 方法体里第一个 return 的字符串字面量。

- static string MethodBody(string body, string name)
  - 按已知签名（Props/Events/Kinds/Create）提取方法体原文。

- static int MethodLineOffset(string body, string name)
  - 方法体开 '{' 在文本中的行偏移（告警定位用）。

- static bool HasProps(string body)
  - 类体是否声明了 Props() 覆写。

- static string Rel(string root, string stdlib, string path)
  - 路径规范化为显示用："stdlib/..." 或相对 root；其余原样。

- static List<int> DepthLines(string s)
  - 每行起始处的花括号深度（行内容计入前一行之后的差量）。

- static string Mask(string s)
  - 把字符串/字符字面量与注释抹成空格（保留换行），供签名查找与
    括号配对不受字面量干扰。

- static int Match(string s, int open)
  - 与 open 处 '{' 配对的 '}' 下标；无则 -1。

- static List<string> JoinBraces(List<string> ls)
  - 按 '\n' 切行（丢弃 '\r'）。
    把折行的 List 初始化器并回一行（`new List<string>{ ... }` 常
    因选项多而折行；不并回则单行扫描看不到完整选项表）。只并含
    "List<" 的起始行——控制流块（if/for 的 `{`）不并，块内逐行
    声明的属性不会因合并而丢。配对计数在 Mask 后的文本上做，
    字符串字面量里的花括号不算。

- static int BraceDelta(string s)
  - 一行文本上 '{' 与 '}' 的差值（字面量与注释抹平后计数）。

- static List<string> Lines(string s)

- static string Trim(string s)
  - 去除首尾空格/制表符。

- static bool Starts(string s, string p)
  - 前缀判断。

- static bool Ends(string s, string p)
  - 后缀判断。

- static int Find(string s, string p, int at)
  - 从 at 起查找子串首次出现，返回下标；无则 -1。

- static string Replace(string s, string a, string b)
  - 全部替换（无正则）。

- static string AfterWord(string s, string w)
  - 关键字 w 之后的标识符。

- static bool Word(string s, string w)
  - w 是否作为独立单词出现（两侧均非标识符字符）。

- static bool IsIdentChar(string c)
  - 标识符字符：字母/数字/下划线。

- static string WordAt(string s, int p)
  - 从 p 起的标识符。

- static string Identifier(string s, int p)
  - 跳过空白后从 p 起的标识符。

- static string IdentifierBack(string s, int p)
  - p 之前（跳过空白）最近的标识符。

- static string QuotedArg(string s, int open, int n)
  - 参数列表（open 为 '(' 下标）里第 n 个引号实参；不足为空串。

- static string QuotedAfter(string s, string key)
  - key 之后的第一个引号字符串。

- static List<string> SplitComma(string s)
  - 按逗号切分并去空白。

- static List<string> Split(string s, string sep)
  - 按分隔符切分并去空白。

- static int Int(string s)
  - 宽松整数解析：数字前缀即取值，其余截断。

- static string Without(string s)
  - 原样返回（历史占位）。

- static bool Has(List<string> a, string x)
  - 列表是否含该字符串。

- static int LineAt(string s, int p)
  - 下标 p 所在行号（1 起）。

- static int NextLine(string s, int p)
  - p 之后下一个换行的下标（无则文末）。
