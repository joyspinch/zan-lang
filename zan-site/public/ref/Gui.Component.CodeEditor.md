# Gui.Component.CodeEditor

> 源码: `stdlib/Gui/Component/CodeEditor/CodeEditor.Completion.zan`, `stdlib/Gui/Component/CodeEditor/CodeEditor.Debug.zan`, `stdlib/Gui/Component/CodeEditor/CodeEditor.Intelli.zan`, `stdlib/Gui/Component/CodeEditor/CodeEditor.Render.zan`, `stdlib/Gui/Component/CodeEditor/CodeEditor.Symbols.zan`, `stdlib/Gui/Component/CodeEditor/CodeEditor.zan`, `stdlib/Gui/Component/CodeEditor/EditorPalette.zan`


## Caret (class)

一个次要光标的位置（行 + 列）。用单个实体取代
并行的行/列列表。

- int line;

- int col;

- Caret(int line, int col)


## CeFileIndex (class)

一份源文件的扫描预处理：抹除了字面量/注释的行、每行所在
类型体的 owner，以及每行声明的成员名（未声明为 ""）。
引用统计对每个成员都要走一遍全文，这三份数据与成员无关，
因此按文件算一次就够：CodeLens 统计一屏声明的代价原本
与「声明数 × 项目字节数」相乘，就出在这里。

- List<string> stripped;

- List<string> owners;

- List<string> decls;

- static CeFileIndex Of(List<string> ls)

- static CeFileIndex OfText(string body)
  - 源文本的索引（宿主给的项目源码是整篇文本）。


## CeHlLine (class)

一行的缓存语法高亮：计算所用的源码
及并行的 token/颜色段，未变化的行在每次重绘时不会被
重新分词（和重新分配）。`blockOpen` 记录
该行是否以未闭合的 /* 块注释结尾，
下一行继承它作为起始状态。

- string src;

- List<CodeSpan> spans;

- bool blockIn;

- bool blockOpen;


## CodeEditor (class)

CodeEditor 分部：自动补全弹窗、符号索引和
感知 using 的补全引擎（成员/类型/标识符建议）。

- static bool Has(List<string> xs, string v)
  - 列表 `xs` 已包含 `v` 时返回 true。

- static bool StartsWithP(string s, string pre)
  - `s` 以 `pre` 开头时返回 true。按字节比：扫描路径会对每行
    调用它，而 Substring 要为每次测试分配一个字符串。

- static void KeywordList(List<string> outk)
  - 把语言关键字追加到 `outk`。

- static void StdlibList(List<string> outk)
  - 追加常用标准库类型和成员用于补全。

- static void CollectWords(List<string> ls, List<string> outw)
  - 收集缓冲区中出现的唯一标识符 token（长度 >= 2），
    使编辑器能补全用户已输入过的符号。

- static void CollectWordsRange(List<string> ls, int from, int to, List<string> outw)
  - 同上，但只扫描 [from, to) 区间。两处写法差异都是为了让代价
    与规模无关：逐字节推进并且整词只分配一次字符串（原来每个字符
    一个 Substring 再逐字符拼接），去重走哈希表（原来对已收集的词
    线性查找，于是一次收集是 词数×字符数 的平方级）。

- void ApplyKindFilter()
  - 通过应用当前种类过滤器，从 acAllItems/acAllKinds 重建可见的
    acItems/acKinds，然后更新弹窗激活状态。

- bool HasKind(string kind)
  - 未过滤的候选列表至少包含一个
    指定种类条目时返回 true（用于灰显不可用的过滤按钮）。

- static int MatchScore(string cand, string pfx)
  - 根据光标左侧紧邻的字词重新计算补全弹窗；
    无前缀或无候选项时关闭弹窗。
    `cand` 对输入 `pfx` 的相关度，不匹配时为 -1。
    越高越好。排序遵循 VS 风格补全的隐含顺序：
    大小写精确前缀 > 不区分大小写前缀 >
    驼峰首字母缩写（`rdal` -> `ReadAllLines`）> 普通
    子序列（`rdll` -> `ReadAllLines`）；并列时更短者胜出，
    因此前缀 `add` 时 `Add` 排在 `AddRange` 之前。

- static bool HumpMatch(string cand, string pfx)
  - `pfx` 拼出 `cand` 的大写字母（加首字符）时返回 true：
    `rdal` 和 `RAL` 都能匹配 `ReadAllLines`。

- static bool SubseqMatchI(string cand, string pfx)
  - `pfx` 的每个字符按顺序出现在 `cand` 中时返回 true，
    忽略大小写。

- static bool EqI(string a, string b)
  - 两个单字符字符串的不区分大小写比较。

- static void SortByScore(List<string> items, List<string> kinds, List<int> scores)
  - 按相关度降序原地排序候选/种类列表。

- static void SortByScoreTop(List<string> items, List<string> kinds, List<int> scores, int limit)
  - 按相关度降序保留最好的 `limit` 个候选。弹窗只显示前几十个，而
    一次击键的候选可能上千：对全部候选做插入排序是平方级的，这里
    只维护一个长度有界的有序前缀，代价降到 候选数×limit。
    与 SortByScore 一样是稳定的（同分保持收集顺序）。

- void ForceCompletion()
  - 显式打开补全（Ctrl+Space），列出作用域内所有内容，
    即使尚未输入前缀。

- void UpdateCompletion()

- void ScoreCandidates(List<string> items, List<string> kinds, string defKind, string pfx, Dict <string, bool> taken, List<int> scores)
  - 把 `items` 中匹配 `pfx` 的候选追加到弹窗列表，`taken` 跟踪已入选的
    词（多个来源会给出同一个名字）。`kinds` 为 null 时统一用 `defKind`。

- void EnsureCandBase()
  - 补全候选中与光标位置无关的那一半：代码片段、关键字、标准库、
    项目类型与成员。它只在项目扫描或缓冲区类型表变化时才会变，而
    重建一次要走遍上千个类型和成员，因此跨击键复用。

- static string MemberKind(string name)
  - 成员名的启发式种类标签：camelCase → 字段，已知名词 →
    属性，*Changed/*Click → 事件，否则为方法。

- static bool IsControlWord(string w)
  - `w` 是看似调用的控制流关键字时返回 true。

- static string DeclIdentBefore(string trimmed, int at)
  - `trimmed` 中 `at` 位置之前声明的标识符（`at` 是其后的
    '(' 或 '{' 的位置）；没有或前面无内容时返回 ""
    —— 声明前总有类型或修饰符，
    这正是它与调用或裸代码块的区别。

- static string PropNameAt(string trimmed)
  - `line` 声明的属性名（如 `public int page { get; set; } = 1;`），未声明则返回 ""
    访问器是区分属性与类型声明
    或普通代码块的关键：'{' 前的头部不含 '('（那是
    方法或控制语句）、不含 '='（赋值）也不含 '['
    （索引器），花括号内是 get / set。

- static string DeclNameAt(string line)
  - 若 `line` 声明了成员则返回其名称，否则返回 ""。
    方法是标识符后跟参数列表，其匹配的 ')'
    后跟方法体的 '{'，且不是控制语句；
    属性是标识符后跟 get / set 访问器。

- static string StripCode(string line)
  - `line` 中注释与字符串/字符字面量被抹除后的版本（长度
    不变，偏移仍可映射回原文），扫描因此永远不会
    匹配到注释文字或字面量中的名称。

- static int FindCallSite(string code, string name, int from)
  - `code` 中从 `from` 起 `name` 第一个调用点的偏移，
    无则 -1。调用点是后跟 `(` 的整词出现，这正是
    真实引用与命名实参（`Upload = true`）、
    类型名（`class Index`）或同名成员声明的区别。

- int ScanRefs(string name, int declLine, List<int> outLines)
  - 统计 `name` 在 `declLine` 上的声明（及其他同名声明）之外的
    调用点数量，给定 `outLines` 时把每处
    行号追加到其中。

- void EnsureExternRefs()
  - 本缓冲区声明的成员的跨文件调用点计数，
    以 "Owner.member" 为键。一次性遍历其他项目源码构建
    —— 每个声明只拆分一次 —— 并保留到宿主重新提供
    源码或加载其他文件为止。

- void EnsureRefIndex()
  - 当前缓冲区的扫描索引，按 TextStamp() 记忆。跳转、CodeLens
    和引用列表都靠它，而它与被查的成员无关：一屏几十个声明
    各自重建一份时，单个声明的统计就要 50 ms。

- void BeginExternRefs()
  - 开始一轮跨文件统计：收集本缓冲区的声明，计数清零。

- void StepExternRefs(int kbBudget)
  - 跨文件统计推进至多 `kbBudget` KB 的扫描量（一个声明扫一个
    文件即该文件的字节数），<=0 表示一口气做完。一口气做完
    意味着打开文件后的第一帧要扫完整个项目：实测本 IDE 工程
    3 s，更大的工程两分钟。因此绘制路径只给预算，进度
    （文件下标、声明下标、当前文件的索引与接收者）留在帧之间；
    未算完时 CodeLens 先不显示数字，靠 lensPending 请求的重绘
    接着算下一批。预算按字节而非文件计，一个大文件也不会
    独占一帧。

- int ExternRefCount(string owner, string member)
  - 本缓冲区之外 `owner.member` 的调用点数（未知为 0）；跨文件
    统计尚未算完时返回 -1。

- int LensRefCount(string name, int declLine)
  - CodeLens 显示的计数：本缓冲区内的使用数加
    项目其他文件中的。只统计缓冲区时，调用方在其他文件的方法
    会显示 "0 references"，而这正是
    项目对外暴露内容的常态。
    跨文件统计还没算完时返回 -1，与预算耗尽同义：调用方
    不绘制未统计完的数字。

- int LensRefCountBudgeted(string name, int declLine)
  - LensRefCount() 按成员名记忆化，每帧至多重算 `lensBudget`
    个成员。预算耗尽时返回记忆的计数，
    连过期值都没有则返回 -1，调用方不绘制
    未经统计的数字。

- void BeginLensFrame(App app)
  - 开启一帧的 CodeLens 预算，缓冲区变化时
    使记忆过期。指纹在此只取一次，因为 TextStamp() 会遍历
    每一行，而一屏可容纳数十个声明。
    
    重算等待输入停止：每次击键都会使
    所有计数失效，每帧统计两个也要各遍历
    整个缓冲区。缓冲区静置片刻前，面板显示
    上次的计数，即一次击键前的值。

- bool LensPending()
  - 本帧有 CodeLens 计数未统计时返回 true，调用方
    据此请求再算一次。

- void JumpToNextRef(string name, int declLine)
  - 把光标移到 `declLine` 之后 `name` 的下一个调用点
    （循环回绕）并选中。没有则不做任何事。

- void CollectRefs(string name, int declLine, List<int> outLines)
  - 收集 `name` 每个调用点的行号（排除其在 `declLine` 上的
    声明），按文件顺序存入 `outLines`。

- void JumpToRefLine(string name, int targetLine)
  - 把光标移到 `targetLine` 上 `name` 的第一个调用点并
    选中（供引用列表弹窗使用）。

- static int FindFrom(string s, string sub, int start)
  - `s` 中从 `start` 起 `sub` 首次出现的索引，无则 -1。
    先比首字节再逐字节比，不为每个位置切一个子串。

- static string TrimStr(string s)
  - 去除 `s` 首尾的空格和制表符。整个编辑器的扫描都逐行调用它，
    因此按字节定界，且无需裁剪时直接返回原串（不分配）。

- string WordAtMouse(App app, Rect area, int gutterW, int fontSize)
  - 返回鼠标指针下的标识符，没有则 ""。

- static void KeywordListK(List<string> outk, List<string> kinds)

- static void CollectWordsK(List<string> ls, List<string> outw, List<string> kinds)

- static void CollectWordsRangeK(List<string> ls, int from, int to, List<string> outw, List<string> kinds)

- static int WordScanRadius()
  - 词补全向光标上下各看多少行。整篇扫描会让每次击键的代价随文件
    规模增长（30 万行时是秒级），而窗口外的符号本来就由项目扫描、
    缓冲区类型表和符号索引提供，这里只补“刚写过的局部标识符”。

- static int LowerByte(int b)
  - ASCII 大写字母折叠为小写，其余字节原样返回。

- static bool StartsWithI(string s, string pre)
  - 不区分大小写的 StartsWith 检查（按字节；匹配函数会对每个候选
    逐字符调用，为每个字符分配字符串太贵）。

- void AcceptCompletion(bool viaTab)
  - 用选中的候选项替换正在输入的字词。`viaTab`
    为 true 且候选项为代码片段时展开其模板。

- string ReceiverBefore(string ln, int dotPos)
  - 紧挨 `dotPos` 处点号之前书写的接收者表达式：
    `WebApp.Create` 中的 `WebApp`、`app.MaxBody` 中的 `app`，以及链中调用自身的
    接收者（`items.Where(x).Select` -> `items`，因为调用的
    实参列表会整体跳过）。没有则返回空。

- string OwnerTypeAtCaret()
  - 声明光标下成员的类型，通过其前面的接收者解析：
    `WebApp.Create` 和 `app.MaxBody`（`app` 声明为 `WebApp`）都得到 `WebApp`，
    成员为继承所得时则得到基类型。
    符号不是成员访问或接收者无法解析时返回空，
    调用方随后回退为仅按名称搜索。
    这使转到定义 / 查找引用不会命中
    无关类型的同名成员。

- string EnclosingTypeOfCaretDecl()
  - 光标正位于其声明上的成员所属的类型，
    使从声明发起的查找引用也限定在该类型内。
    光标不在成员声明上时返回 ""。

- void DotCompletion()
  - 用户输入 '.' 时触发。查看点号前的字词并
    建议该类型的已知成员，或链式调用的返回类型。

- void UsingDotCompletion()
  - `using X.` 指令的命名空间补全：列出已输入前缀的
    子命名空间（如 `using System.` -> Data、
    Net、IO、Text 等）。源自 stdlib 命名空间目录，
    绝不臆造成员，也绝不落入类型成员路径。

- static void ChildNamespaces(string prefix, List<string> out2)
  - 把 `prefix` 去重后的下一段子命名空间追加到 `out2`，
    来自 stdlib 目录（如前缀 "System.Data" -> Orm、Sqlite、
    Postgres）。

- static void AddMembersIn(List<string> out2, string typeName, int wantStatic)
  - 通过一种接收者可访问的 `typeName` 成员：
    `wantStatic` 为 1 表示类型本身（`WebApp.`），0 表示值（`app.`）。
    索引未描述的内容回退到 AddMembers，
    其手写表不区分静态/实例。

- static void AddMembers(List<string> out2, string typeName)
  - 为已知类型填充成员列表。

- static void AddLinqMembers(List<string> out2)
  - 追加 LINQ 查询运算符方法。仅当文件导入 `using System.Linq;` 时
    为集合接收者调用。

- static void AddWords(List<string> out2, string s)
  - 把空格分隔的字符串拆分为 token 追加到 `out2`。

- static void AddKindWords(List<string> cand, List<string> kinds, string s, string kind)
  - 把 `s` 中每个空格分隔的 token 追加到 `cand`（种类 `kind`），
    跳过已存在的重复项。

- static string StripModifiers(string trimmed)
  - 从去除首尾空白的行中剥离开头的声明修饰符（public/static/…），
    返回剩余部分（如从 "public class Foo {" 得 "class Foo {"）。

- static string TypeDeclName(string trimmed)
  - 若 `trimmed` 声明了 class/struct/enum/interface 则返回其名称，
    否则 ""。

- static bool DeclHeadOk(string head)
  - 当 `head`（后跟 `(` 的名称之前的文本）
    可以是声明的返回类型而非接收者表达式时返回 true。
    像 `app.MaxBody(x);` 这样的调用前面有 `app.`，因此不能
    将其报告为 `MaxBody` 的声明。

- static string MemberDeclName(string trimmed)
  - 若 `trimmed` 是直接成员声明（方法 / 字段 / 属性）
    则返回成员名，否则 ""。

- static void ScanTypesInto(string src, List<string> names, List<string> membersJoined)
  - 扫描 `src` 中每个 class/struct/enum 声明，把每个类型名
    追加到 `names`，其空格分隔的直接成员名追加到 `membersJoined`。

- static void ScanTypeLinesInto(List<string> ls, List<string> names, List<string> membersJoined)
  - 同上，但直接吃行表：编辑器自己的缓冲区本来就是行表，
    先 GetText() 拼成一个整文档字符串再 SplitLines() 拆回去，在 30 万行
    上就是每次重扫都要分配十兆级的字符串。

- void SetProjectFiles(List<string> contents)
  - 宿主 IDE 钩子：提供所有其他项目文件的源码，使
    编辑器能解析跨文件类型（如 utils.zan 中的 `Utils` 类）。

- void EnsureProjectScan()
  - 若 SetProjectFiles() 留下待处理的跨文件类型扫描
    则执行之。由补全路径惰性调用，使切换标签页
    本身保持低成本。

- static bool HasProjType(List<ProjType> ps, string name)
  - 项目类型列表包含名为 `name` 的类型时返回 true。

- void ParseUsingsInto(List<string> outNs)
  - 收集当前缓冲区 `using` 行导入的命名空间。

- string FileNamespace()
  - 当前缓冲区声明的命名空间（`namespace X;`），无则 ""。

- bool NsVisible(string ns)
  - 命名空间 `ns` 在当前缓冲区可见时返回 true：通过
    `using` 导入，或是文件自身命名空间（的祖先）。

- bool UsesLinq()
  - 文件导入 System.Linq 时返回 true（控制 LINQ 查询运算符）。

- static bool IsKnownStdlibType(string name)
  - 编辑器拥有硬编码成员数据的 stdlib 类型。

- static string NormalizeStdlibType(string name)
  - 规范化类型名以便成员查找（String -> string）。

- static bool IsCollectionType(string name)
  - 对在 System.Linq 下暴露 LINQ 的内置集合类型返回 true。

- static string StdlibNsData()
  - 完整的命名空间 -> 类型目录，用于把作用域内的 stdlib 类型加入
    补全。每行一个条目："Namespace=Type Type Type"。

- static void StdlibTypesForNs(string ns, List<string> out2)
  - 把命名空间 `ns` 中声明的类型名追加到 `out2`。

- static List<string> nsTypeNames;

- static List<string> nsTypeNs;

- static void EnsureNsTable()
  - Builds the name -> namespace table once. It used to be rebuilt as a
    dozen packed strings on every lookup, and the lookup runs for every
    capitalised identifier in the buffer each time the diagnostics rerun.

- static void AddNsGroup(List<string> names, List<string> nss, string ns, string words)
  - Appends every space-separated name in `words` with namespace `ns`.

- static string NsForStdlibType(string name)
  - 使用 stdlib 类型 `name` 必须导入的命名空间，
    `name` 是内置 / 有歧义 / 非 stdlib 类型时返回 ""。刻意只收录
    独特无歧义的名称，使缺失 using 的诊断绝不
    误报在用户自己的标识符上。

- bool IsProjectType(string name)
  - `name` 是项目（其他文件）或
    当前缓冲区中声明的类型时返回 true。

- bool FindTypeMembers(string typeName, List<string> outMem)
  - 查找用户类型 `typeName` 的声明成员（先项目文件，后
    当前缓冲区）。找到时返回 true 并填充 `outMem`。

- string ResolveVarType(string name)
  - 尽力解析局部变量的声明类型：扫描
    缓冲区中的 `Type name` 声明。返回基础类型（剥离泛型），
    无则 ""。

- void AddStdlibCandidates(List<string> cand, List<string> kinds)
  - 把作用域内的 stdlib 类型名加入补全：始终包含内置类型，
    加上每个已导入（或自身）命名空间的类型，以及仅当
    导入 System.Linq 时的 LINQ 运算符。

- static Dict <string, bool> SeenOf(List<string> cand)
  - 已在候选表里的词的哈希集。项目类型和成员动辄上千，逐个对
    候选表线性查重会把一次补全变成平方级。

- static void AddCand(List<string> cand, List<string> kinds, Dict <string, bool> seen, string w, string kind)

- void AddProjectCandidates(List<string> cand, List<string> kinds)
  - 把项目声明的类型（及其成员）加入补全。

- bool InAttributeContext()
  - 当 (curLine 行、curCol 之前) 正在输入的字词位于
    特性括号内时返回 true —— 即该行上最近的未匹配 '[' 在它之前。

- void EnsureBufTypes()
  - 扫描缓冲区中使用 `using X;` 类型却缺少所需导入之处，
    为每个出错行记录诊断 + 快速修复。
    当前缓冲区声明的类型名（及其成员）。补全与诊断每次击键、
    每次光标移动都要用到它，而重扫是一整篇解析，因此只在结构
    真的变了时重扫（见 BufTypesStructuralChange）。

- bool BufTypesStructuralChange()
  - 自上次扫描以来，缓冲区里发生了能改变类型表的变化时返回 true。
    类型表只取决于声明行和花括号层次，所以在方法体里敲一个字
    （最常见的编辑）不必重扫：只有声明形状或花括号变了才算数。

- static int BraceKey(string s)
  - 一行里 '{' 与 '}' 的个数，合成一个可比较的键。

- static bool IdentByte(int b)
  - 对构成标识符的字节（ASCII 字母、数字、
    下划线）返回 true。用字节测试替代 InSet(str, Substring(p, 1))，后者
    会为文档中每个字符分配字符串并重扫 63 字符集合，
    开销很大。

- void AnalyzeMissingUsings()
  - Missing-`using` diagnostics for the whole buffer. The scan walks every
    character of every line, so it is cached twice over: once per line (a
    line whose text is unchanged reuses its result, so typing rescans one
    line) and once per buffer (the caller reruns this on every caret line
    change, and an unchanged buffer answers from the cache).

- bool ReplayMissingUsings()
  - Rebuilds the diagnostics and quick fixes from the per-line cache when
    every line still holds the text it was scanned from. Returns false
    when the buffer moved on and a real scan is needed.

- void ScanLineMissingUsing(int li, List<string> usings, string fileNs, List<string> tn)
  - Scans one line for the first stdlib type used without its `using` and
    records the answer (namespace + type name, both "" for none) in the
    per-line cache.

- string QuickFixNs()
  - 缺失 using 的快速修复会为光标行添加的命名空间，无则 ""。

- bool HasQuickFix()
  - 光标行存在缺失 using 的快速修复时返回 true。

- void AddUsing(string ns)
  - 在最后一个现有 using 之后（或顶部）插入 `using ns;`，
    除非已存在。

- void ApplyQuickFix()
  - 若有则对光标行应用缺失 using 的快速修复。


## CodeEditor (class)

CodeEditor 分部：IntelliSense 描述与片段展开，以及
断点管理。

- static int Find(string s, string sub)
  - 子串 `sub` 在 `s` 中首次出现的索引，无则 -1。

- static string KindWord(string kind)
  - 补全种类标签的人类可读名称。

- static string SigFromLine(string trimmed)
  - 给定去除首尾空白的方法声明行，返回整洁的单行
    签名 "ReturnType Name(params)"（去修饰符、去函数体），
    该行不是方法声明则返回 ""。

- static string SigInSource(string src, string name)
  - 在给定源码行中查找名为 `name` 的方法并返回其
    真实解析签名，无则 ""。

- string RealSigFor(string name)
  - 名为 `name` 的用户/项目方法的真实签名，先
    从当前缓冲区解析，再查其他项目文件。找不到返回 ""
    —— 调用方可回退到精编的 stdlib 签名。

- string SigHelpFor(string name)
  - `name` 用于参数提示的签名，按被调用者记忆化。
    
    解析会遍历本缓冲区和所有项目源码，因此
    每个被调用者只解析一次而非每次击键：输入实参不会
    改变提示所描述的声明。缓冲区被替换（见 LoadText）
    或行数变化时丢弃记忆，
    这对签名而言已足够精细。

- string CallAtCaret(List<int> outArg)
  - 光标在其所在行内所处的调用：被调用者名称，
    光标不在 `(` 与其 `)` 之间时返回 ""。`outArg` 接收
    正在输入的实参的从零开始索引。
    
    仅一行，从光标处从右到左扫描 —— 足以覆盖
    绝大多数调用，且每帧工作量有界。字符串和
    字符字面量会被跳过，实参中的 `"("` 不会开启调用。

- static void ParamSpan(string sig, int idx, List<int> out2)
  - 参数 `idx` 在 `sig` 参数列表中的起点和长度，
    以 (start, len) 追加到 `out2`；无此参数则追加空。
    嵌套泛型和 lambda 保留逗号：只有深度 0 的逗号分隔参数。

- static string DescSig(string name)
  - 成员的单行签名：编译器符号索引存在时
    用索引的（这样提示条显示真实声明，含全部重载），
    否则用下方精编的列表。

- static string DescDoc(string name)
  - 知名成员的精编单行文档，无则 ""。

- static string DescKeyword(string name)
  - 语言关键字和内置类型的精编单行文档，
    使补全预览能解释基本语法；无则 ""。

- static void WrapText(string s, int maxW, int fontSize, List<string> outl, int maxLines)
  - 将 `s` 按 `fontSize` 换行以适配 `maxW` 像素宽，
    把结果行追加到 `outl`（最多 `maxLines` 行）。

- static void SnippetListK(List<string> outk, List<string> kinds)
  - 把内置代码片段触发器（种类 "S"）追加到候选列表。

- void ExpandSnippet(string name)
  - 在光标处展开代码片段，替换触发词
    [acStart, curCol)。把光标放在主体中的 "$C$" 标记处。

- static bool HasInt(List<int> xs, int v)
  - 整数列表 `xs` 包含 `v` 时返回 true。

- bool HasBreakpoint(int line)
  - 第 `line` 行（从 0 起）有断点时返回 true。

- void ToggleBreakpoint(int line)
  - 切换 `line` 行上的断点。

- List<int> Breakpoints()
  - 断点行号集合（从 0 起）。

- void SetExecLine(int line)
  - 把 `line` 高亮为当前暂停的执行行（-1 清除）。

- void HandleInput(App app)

- void HandleCharKey(App app)
  - kind==6：键入字符、Ctrl 快捷键和编辑键。

- void HandleNavKey(App app)
  - kind==4：导航 / 选择 / 多光标移动键。

- static int ColFromX(string s, int px, int fontSize)
  - 把像素 x 偏移（相对文本原点）映射为列索引：
    即测量前缀宽度达到 `px` 的第一个字符边界。
    前缀宽度随边界单调递增，因此对边界
    二分查找只需 O(log n) 次 MeasureText 调用，而非每字符一次
    —— 拖拽选择期间每次鼠标移动都会执行，被取代的
    二次扫描正是长行选择延迟的
    根源。


## CodeEditor (class)

CodeEditor 分部：语法高亮（按行缓存的词法分析）
和主渲染路径（装订线、文本、光标、弹窗、缩略图）。

- static bool InSet(string chars, string ch)
  - 单字符字符串 `ch` 出现在 `set` 中时返回 true。

- static bool ContainsSub(string hay, string needle)
  - `hay` 包含子串 `needle` 时返回 true。

- static bool IsKeyword(string w)
  - `w` 是 Zan/C# 风格语言关键字时返回 true。

- static bool IsLuaKeyword(string w)
  - `w` 是 Lua 关键字（含 5.4 的 goto）时返回 true。

- static bool IsPyKeyword(string w)
  - `w` 是 Python 关键字或内置软关键字时返回 true。

- static bool IsKeywordLang(string w, int lang)
  - 按语言（0=Zan、1=Lua、2=Python）判定关键字。

- static int BlockEnd(string line, int from, int lang)
  - 从 `from` 起找本语言块注释/长字符串的结束符，返回其后
    一个位置；本行内未闭合则返回 -1。Zan 用 "*/"，Lua 用
    "]]"，Python 用三引号。

- int HlThemeSig(Gui.Theme t, int defaultText)
  - 活动语法调色板的廉价签名；变化时
    整个高亮缓存被丢弃，使颜色跟随主题/覆盖切换。

- CeHlLine GetHl(int ln, string text, Theme t, int defaultText)
  - 返回 `ln` 行的缓存高亮，仅当
    行文本（或调色板）变化时重新分词。稳定帧中每个可见行
    都命中缓存，滚动时无分词、无分配。
    块注释状态自上而下传递：以未闭合的
    /* 结尾的行会把后续行着为注释色，直到出现闭合的
    */。当前一行尚未缓存时（跳转后首次绘制），
    从最近的已缓存行向前分词重新推导状态，
    直至本行。

- bool Highlight(string line, Theme t, int defaultText, bool inBlock, List<CodeSpan> spans)
  - 把 `line` 拆成彩色区间：关键字、类型（大写开头
    的标识符）、字符串、注释和数字各用不同颜色；
    其余用默认文本色。`inBlock` 继承自
    上一行，返回值是该行结束时的状态。

- static bool HighlightSpans(string line, int kwC, int tyC, int strC, int comC, int numC, int def, bool inBlock, List<CodeSpan> spans)
  - 编辑器和只读代码视图（如
    组件画廊）共用的静态分词器。用给定调色板追加彩色区间。
    `inBlock` 是继承的 /* 块注释状态；返回
    行结束时的状态。

- static bool HighlightSpansLang(string line, int kwC, int tyC, int strC, int comC, int numC, int def, bool inBlock, int lang, List<CodeSpan> spans)
  - 同 `HighlightSpans`，但按 `lang` 分词
    （0=Zan、1=Lua、2=Python）：注释引导符、块注释/长字符串
    与关键字表都随语言切换，单引号字符串只在 Lua/Python 中
    成立。

- static int SelColor(Gui.Theme t)
  - 选区高亮色调，选在当前外观下可读的颜色。

- static void DrawSelHighlight(Canvas c, App app, int aL, int aC, int cL, int cC, int ln, string text, int textX, int rowY, int lineH, int fontSize, Rect area)

- static string DiagMsg(List<Diagnostic> ds, int line)

- static bool DiagHas(List<Diagnostic> ds, int line)
  - 任何诊断位于 `line` 行时返回 true。

- static bool QuickFixHas(List<QuickFix> qs, int line)
  - 任何快速修复指向 `line` 行时返回 true。

- int Render(App app, Rect area)

- void RenderLines(App app, Rect area, int lineH, int fontSize, int gutterW, int textX, int visLines, bool focused, int hoverColor, int breakpointColor, int lineNumberColor, int caretColor, int defaultText)
  - 绘制可见行窗口：装订线、断点/执行标记、行号、
    选区高亮、语法区间、光标、错误下划线、
    行内错误提示和 CodeLens。裁剪到编辑器矩形内。

- void RenderOverviewRuler(App app, Rect area, int visLines)
  - 右侧概览标尺：视口滑块加每个诊断行一个刻度。

- void RenderAcPopup(App app, Rect area, int lineH, int textX, int fontSize, bool focused, int visLines)
  - 自动补全弹窗 + 种类过滤条 + 描述面板，
    补全激活时绘制在光标下方。

- void RenderSigHelp(App app, Rect area, int lineH, int textX, int fontSize, bool focused, int visLines)
  - 参数信息（VS 的"签名帮助"）：光标位于
    调用实参列表内时，在行上方绘制被调用者签名，
    高亮正在输入的参数，并在其下显示
    该参数的单行文档。
    
    它让位于补全弹窗 —— 两者都想要光标下方的空间，
    而那时键盘驱动的是列表 —— 成本是
    光标行的一次从右到左扫描加一次记忆化签名查找。

- static string WordAt(string line, int col)

- static bool IsIdentChar(string ch)

- static bool IsIdentByte(int b)
  - 标识符字节判定。文本扫描逐字符调用它，切一个
    单字符子串再比较字符串会为每个字符分配一次，
    因此热路径按字节问。

- static string LookupVar(List<DbgVar> vars, string name)

- static DbgVar LookupVarRaw(List<DbgVar> vars, string name)
  - 与 LookupVar 类似，但返回条目本身，调用方可渲染
    可展开的格式化提示。名称不存在时返回 null。

- static string LeadWs(string line)
  - `line` 的前导空白，使画在行上方的标注
    能与该行精确对齐缩进。

- static string Indent(int n)

- static void PrettyJson(string s, List<string> out2, int maxLines)
  - 把类 JSON 的值字符串美化打印为缩进行。忽略
    引号字符串内部的结构，限制 maxLines 行，截断超长
    行，且最多读取 8000 个输入字符（大数据安全）。

- static string Clip(string s)
  - 截断单条提示行，超长值不会撑爆提示框。

- static void BuildHoverLines(DbgVar v, List<string> out2)
  - 构建调试悬停提示中显示的行：头部
    （`name : type`）后跟值，若值看起来像
    JSON / 数组 / 对象则美化打印。


## CodeEditor (class)

CodeEditor 分部：由 `zanc --emit-symbols` 生成的符号索引。

补全此前用 CodeEditor.Completion.zan 中手写的表回答成员查询，
而该表已与编译器脱节：它提供了
File.OpenRead()、Math.Clamp() 和 Convert.ToDecimal()（均不存在），
却隐藏了 File.GetSize()。编译器写入索引的内容
优先于这些表；它们保留为尚未
建立索引的工作区的回退。

- static List<string> symOwners;

- static List<string> symNames;

- static List<string> symKinds;

- static List<string> symSigs;

- static List<string> symStatics;

- static List<string> symTypes;

- static List<string> symBases;

- static bool SymbolIndexLoaded()
  - 索引成功加载后返回 true。

- static void ClearSymbolIndex()
  - 丢弃已加载的索引（构建后重新加载前使用）。

- static bool LoadSymbolIndex(string path)
  - 加载 `zanc --emit-symbols <path>` 写入的索引：制表符分隔的
    `T kind namespace name bases` 和 `M owner name kind static signature`
    记录。文件缺失或没有记录时返回 false，
    此时补全继续使用内置表。

- static bool HasSymbolType(string typeName)
  - 索引描述了 `typeName` 时返回 true。

- static string TypeNameOn(string line)
  - `line` 上声明的类型名（`class`、`struct`、`interface`、
    `enum`，前面可带任意修饰符），无则 ""。

- static int FindWordFrom(string s, string word, int from)
  - `s` 中从 `from` 起 `word` 第一次整词出现的偏移，
    无则 -1。

- static int BraceDelta(string line)
  - `line` 贡献的净花括号嵌套量，忽略字面量和注释。

- static int BraceDeltaPre(string s)
  - 已抹除字面量/注释的行的净花括号嵌套量。谁持有
    抹除结果就由谁传入，同一行不必为了数花括号再抹一遍。

- static void OwnerPerLine(List<string> ls, List<string> outOwners)
  - 对 `ls` 的每一行，给出包含该行的类型体的类型名
    （类型外为 ""），按行序追加到 `outOwners`。

- static void StripCodeInto(List<string> ls, List<string> outStripped)
  - 按行抹除字面量/注释，结果追加到 `outStripped`。一份
    抹除结果可供 owner 划分、变量收集和引用统计共用。

- static void OwnerPerLinePre(List<string> stripped, List<string> outOwners)
  - OwnerPerLine，但行已经抹除过。

- static string EnclosingTypeAt(List<string> ls, int at)
  - `ls` 第 `at` 行所在类型体的类型名，无则 ""。

- static string IdentBefore(string s, int end)
  - `s` 中恰在偏移 `end` 之前结束的标识符
    （该处字符不属于标识符时返回 ""）。

- static void CollectTypedVars(List<string> ls, string typeName, List<string> outNames)
  - `ls` 中持有 `typeName` 类型值的名称：`Type name`
    声明，或 `name = Type.` / `name = new Type(` 赋值。这些
    就是本文件中访问该类型成员的接收者，
    `app.MaxBody` 正是借此被识别为 WebApp 的 MaxBody。

- static void CollectTypedVarsPre(List<string> stripped, string typeName, List<string> outNames)
  - CollectTypedVars，但行已经抹除过。

- static int CountMemberRefsIn(List<string> ls, string member, string owner, List<int> outLines)
  - `ls` 中属于类型 `owner` 的 `member` 出现：通过
    类型名、`this`/`base`、该类型的变量，
    或在 owner 自身函数体内不带限定地书写。
    同名声明会被跳过。给定 `outLines` 时行号追加到其中。

- static int CountMemberRefsPre(CeFileIndex idx, string member, string owner, List<int> outLines)
  - CountMemberRefsIn，但行索引已由调用方备好。统计一个
    文件里的多个成员时预处理只做一次；每个成员都重做
    一遍曾让 CodeLens 的跳转代价与声明数相乘。

- static void MemberRecvsPre(List<string> stripped, string owner, List<string> outFamily, List<string> outRecvs)
  - `owner` 成员在这些行上的可能接收者（及类型家族）。
    成员属于 `owner`，但可通过任何继承它的类型*到达*：
    写在派生类型名上，或声明为该派生类型的变量上，
    都是对同一成员的引用。

- static int CountMemberRefsWith(CeFileIndex idx, List<string> family, List<string> recvs, string member, List<int> outLines)
  - 成员引用统计的内层：抹除行、逐行 owner、类型家族与
    接收者均已备好。

- static int FindMemberDeclLineIn(List<string> ls, string owner, string member)
  - `ls` 中类型 `owner` 声明 `member` 的行索引，无则 -1。只搜索
    owner 自身函数体，同文件中其他类型的同名成员
    绝不是答案。

- static bool IsMemberDeclOf(string line, string member)
  - `line` 声明 `member` 时返回 true：方法头（`... member(`）或
    字段/属性（`Type member;` / `Type member =` / `Type member {`），
    而非对它的使用。

- static string IdentAt(string s, int at)
  - `s` 中从 `at` 开始的标识符（无则 ""）。

- static string SymbolDeclaringType(string typeName, string member)
  - `typeName` 基类链中实际声明 `member` 的类型，
    索引不知晓该成员时返回 ""。继承它的类型上的 `app.ToString()`
    必须指向声明它的基类，而非
    调用它的派生类型。

- static void SymbolMembers(string typeName, List<string> out2)
  - 追加编译器真正拥有的 `typeName` 成员，沿
    基类链展开，继承成员也会出现。

- static void SymbolMembersIn(string typeName, List<string> out2, int wantStatic)
  - 单一访问上下文下的 `typeName` 成员：`wantStatic` 为 1 只列
    静态成员（接收者为类型本身：`WebApp.`），0 只列
    实例成员（接收者为值：`app.`），-1 两者都列。混用两者
    正是此前把类型全部实例状态塞进 `TypeName.` 的原因。

- static bool SymbolTypeLacksMember(string typeName, string member)
  - 索引描述了 `typeName` 且未把 `member` 归于
    它或它的任何基类时返回 true —— 这是唯一能确定
    无关类型同名成员绝对是错误答案的情况。

- static void SymbolSubtypesOf(string typeName, List<string> out2)
  - 追加所有基类链可达 `typeName` 的已索引类型 ——
    即其成员也能到达的类型。

- static string SymbolFirstBase(string typeName)
  - `typeName` 在索引中的第一个基类（无则 ""）。

- static string SymbolSignature(string typeName, string member)
  - `typeName.member` 的声明签名（索引无
    此成员时返回 ""）。有重载时取第一个声明。

- static string SymbolSignatureAny(string member)
  - 索引对任意类型成员的第一个签名，用于
    接收者类型未知的悬停/签名条。

- static void SymbolOverloads(string typeName, string member, List<string> out2)
  - 为 `typeName.member` 声明的所有签名，即重载集。

- static string SymbolKind(string typeName, string member)
  - 补全为 `typeName.member` 显示的种类字母，
    索引未描述时返回 ""。

- static void SymbolTypesStartingWith(string prefix, List<string> out2)
  - 所有拼写以 `prefix` 开头的已索引类型名
    （不区分大小写），用于类型名补全。

- static void SymbolExtensionsFor(string typeName, List<string> out2)
  - 索引中适用于 `typeName` 的扩展方法：第一个参数
    声明为 `this <typeName>` 的静态方法。


## CodeEditor (class)

多行、带行号的纯文本代码编辑器。持有自己的缓冲区
（行列表）和光标，获得焦点时响应键盘输入自行编辑
—— 它是 IDE 的编辑界面。即时模式：
创建一个，跨帧保留，每帧调用 Render(app, area)。

CodeEditor ed = new CodeEditor();      // 只创建一次
ed.LoadText("using System;\n\nclass Main {}");
...
ed.Render(app, editorRect);               // 每帧调用

- static int lang=1;
  - 编辑器自有界面（上下文菜单）的 UI 语言：0 = 英文，
    1 = 中文。由宿主应用设置；仅影响显示。

- List<string> lines;

- int curLine;

- int curCol;

- int scrollLine;

- int ensuredCaretLine;

- int lastId;

- List<int> rowLine;

- List<int> rowTop;

- List<int> rowLensY;

- int lensRowH;
  - 透镜行高度（未显示时为 0）。

- List<string> acItems;

- List<string> acAllItems;

- List<string> acAllKinds;

- string acFilter;

- int filtX;

- int filtY;

- int filtW;

- int filtH;

- List<string> acFull;

- List<CodeLensRef> codeLens;

- bool refPopOpen;

- string refPopName;

- List<int> refPopLines;

- int refPopX;

- int refPopY;

- int refPopSel;

- int refPopGX;

- int refPopGY;

- int refPopGW;

- int refPopGH;

- int refPopRowH;

- int refPopHeadH;

- int refPopShown;

- bool gotoRequested;

- int acSel;

- bool acActive;

- int acStart;

- bool acDot;

- bool acForced;

- List<string> acKinds;

- int popX;

- int popY;

- int popW;

- int popH;

- int popItemH;

- int popFirst;

- int popShown;

- bool selActive;

- int selAnchorLine;

- int selAnchorCol;

- bool mouseSelecting;

- bool barDragging;

- int lastClickMs;

- int lastClickLine;

- int lastClickCol;

- int clickCount;

- int pageRows;

- List<Caret> carets;

- string clipboard;

- bool ateCtrlKey;
  - 剪切 / 复制 / 粘贴 / 全选已经在按键事件里做过了，后面紧跟的
    那个控制字符事件要丢掉，否则会粘贴两次。

- List<EditSnapshot> undoStack;

- List<EditSnapshot> redoStack;

- List<Diagnostic> errors;

- int lastDiagLine;

- List<Diagnostic> compErrors;

- int cErrStamp;

- int diagStaleFrame;

- List<string> muTypeNames;

- List<string> muTypeMembers;

- List<string> btLineSrc;

- List<string> cbItems;

- List<string> cbKinds;

- int cbGen;

- int candBaseGen;

- List<ProjType> projTypes;

- List<string> projFileSrc;

- bool projScanDirty;

- List<string> extRefKeys;

- List<int> extRefVals;

- bool extRefDirty;

- int extRefAt;

- List<string> extRefDeclOwner;

- List<string> extRefDeclName;

- CeFileIndex extRefFileIdx;

- int extRefDeclAt;

- string extRefOwnerCur;

- List<string> extRefFamily;

- List<string> extRefRecvs;

- CeFileIndex refIdx;

- int refStamp;

- string sigMemoName;

- string sigMemoText;

- int sigMemoStamp;

- List<string> lensKeys;

- List<int> lensVals;

- List<int> lensFresh;

- int lensStamp;

- int lensBudget;

- bool lensPending;

- int lensQuietMs;

- List<CeHlLine> hlCache;

- int hlThemeSig;

- List<QuickFix> fixes;

- List<string> muLineSrc;

- List<string> muLineNs;

- List<string> muLineWord;

- string muCtx;

- int muStamp;

- List<string> ceLineSrc;

- List<int> ceBrace;

- List<int> ceParen;

- List<int> ceUsingBad;

- List<int> bps;

- int execLine;

- List<DbgVar> dbgVars;

- DbgVar hoverTip;

- int hoverTipX;

- int hoverTipY;

- bool ctxOpen;

- int ctxX;

- int ctxY;

- string ctxCmd;

- int edFont;

- int synKw;

- int synTy;

- int synStr;

- int synCom;

- int synNum;

- int synText;

- int syntaxLang;

- void SetSyntaxLang(int lang)
  - 设置本缓冲区的语法语言（0=Zan、1=Lua、2=Python）。语言
    变化时丢弃按行高亮缓存，使已绘制的行按新语言重新分词。

- int SyntaxLang()
  - 当前语法语言（0=Zan、1=Lua、2=Python）。

- static int LangForName(string path)
  - 按文件名推断语法语言：.lua 为 Lua、.py/.pyw 为 Python，
    其余（含无扩展名）按 Zan 处理。

- static string LowerAscii(string s)
  - ASCII 小写化（扩展名比较用，不触碰多字节字节）。

- string LineComment()
  - 本语言的行注释引导符（Ctrl+/ 插入的就是它）。

- void ApplyTheme(int fontPx, int kw, int ty, int str, int com, int num, int text)
  - 设置编辑器的代码字号和语法高亮颜色。任一参数为 0
    则该方面保持内置默认值。

- int FontSize(Gui.Theme t)
  - 渲染所用的代码字号，未设置时遵循主题默认值。

- CodeEditor()

- static List<string> SplitLines(string s)
  - 按 '\n' 把字符串拆分为行（去除 '\r'）。

- void SetLines(List<string> ls)

- void LoadText(string s)

- string GetText()

- int TextStamp()
  - 廉价的内容指纹（行数 + 总字符数）；使宿主
    无需实例化完整文本即可每帧检测缓冲区变化。

- int LineCount()

- int CurLine()

- int CurCol()

- string LineAt(int i)
  - 第 `i` 行的文本（越界返回 ""）。使宿主（查找栏）
    无需每次击键实例化整个文档即可遍历缓冲区。

- int LastWidgetId()
  - 本编辑器上一帧渲染所用的焦点 id，宿主可在
    自己的覆盖层关闭时把键盘交还给它。

- bool HasSelection()
  - 选区激活期间返回 true。

- int ScrollLine()
  - 首个可见行。拥有多个缓冲区的宿主把它与
    光标一起保存，以恢复缓冲区离开时的状态。

- void SetScrollLine(int line)

- bool CompletionActive()
  - 补全弹窗显示期间返回 true。

- int CompletionCount()
  - 可见补全候选的数量。

- int CompletionSel()
  - 高亮候选的索引（从 0 起）。

- string CompletionItem(int i)
  - 索引 i 处的候选标签（越界返回 ""）。

- string CompletionKind(int i)
  - 索引 i 处的候选种类标签：M/P/T/K/S/F（越界返回 ""）。

- string WordAtCaret()
  - 返回光标下的标识符（无则为空字符串）。

- bool ConsumeGotoRequest()
  - Ctrl+点击请求转到定义后返回一次 true，
    并清除标志，使宿主恰好处理一次。

- void SetDebugVars(List<DbgVar> vars)

- void GoToLine(int line)

- int BufLen()
  - 缓冲区总大小，仅用作使编译器诊断失效的廉价变化信号
    （用户编辑文件时）。

- void SetCompilerDiagnostics(List<Diagnostic> diagnostics)
  - 用上次 Run/Build 针对本文件报告的真实错误
    替换红色下划线 + 提示显示的编译器诊断，并
    把光标跳到第一个错误，使其原因立即可见。

- bool CompilerDiagStale()
  - 设置编译器诊断后缓冲区一旦变化即返回 true，
    用户一开始编辑，过期错误即停止绘制。

- void InvalidateDiagStale()
  - 丢弃每帧的过期记忆；渲染流程在本帧
    缓冲区不再变化后调用一次。

- string CompilerErrMsg(int ln)
  - 某行的编译器诊断消息，无或已过期返回 ""。

- bool HasCompilerErr(int ln)
  - 第 `ln` 行当前携带有效编译器诊断时返回 true。

- void GoToLineCol(int line, int col)

- void InsertStr(string ins)

- void NewLine()

- void BackspaceEdit()

- void DeleteEdit()

- void ClampCol()

- void MoveLeft()

- void MoveRight()

- void MoveUp()

- void MoveDown()

- void MoveWordLeft()
  - 将光标移到上一个单词开头（Ctrl+Left）。

- void MoveWordRight()
  - 将光标移到下一个单词开头（Ctrl+Right）。

- int SmartHomeCol()
  - 智能 Home 目标：首个非空白字符列；或当光标已在此列时
    回到第 0 列（可切换，同 Visual Studio / JetBrains）。

- void MoveLineUp()
  - 将当前行与上一行交换（Alt+Up）。

- void MoveLineDown()
  - 将当前行与下一行交换（Alt+Down）。

- void IndentSelection()
  - 给选区覆盖的每一行增加一级缩进（Tab）。

- void OutdentSelection()
  - 从每个选中行移除最多一级缩进（Shift+Tab）。

- void OutdentCurrentLine()
  - 从当前行移除最多一级缩进（Shift+Tab，无
    选区时）。

- void SaveUndo()

- void Undo()

- void Redo()

- void SelectAll()

- void ClearSelection()

- void SelectRange(int aLine, int aCol, int bLine, int bCol)
  - 从 (aLine, aCol) 选中到 (bLine, bCol)，光标停在
    末尾，与拖拽选择完全一致：查找栏返回的搜索命中
    会像普通选区一样高亮并滚动入视野。

- void SelectWordCaret()
  - 选中光标下的标识符（双击）。若光标位于
    单词右缘（紧邻分隔符），则选中前一个单词，
    与 Visual Studio / JetBrains 行为一致。

- void SelectLineCaret()
  - 选中当前整行；若后面还有行则包含行尾换行符
    （三击）。

- void SelPrepare(bool shift)
  - 光标移动前准备选区状态：按住 `shift` 时
    在当前位置埋下锚点（若尚未处于选区中），
    移动会扩展选区；否则丢弃选区。

- void BuildRowLayout(Rect area, int lineH, int lensH)
  - 布局本帧各行：从 `scrollLine` 起所有能放入
    `area` 的代码行，声明了成员的代码行前加 lens 行。
    每帧在行与像素互相换算之前调用一次。

- int VisibleRowCount()
  - 本帧布局的代码行数（至少 1），作为滚动页大小。

- int LineAtY(int lineH, int yPix)
  - 绝对坐标 `yPix` 所在的行；该点不在已布局行内时返回 -1。
    lens 行返回其注解的行，因此点击声明
    上方也能命中声明行，而非落到空白处。

- int LineBottomY(App app, int ln)
  - 本帧 `ln` 行绘制位置下方的绝对 y 坐标；
    该行不在屏幕上时返回 -1。宿主面板（引用
    列表）从这里向下挂起。

- int LineTopY(int ln)
  - 本帧 `ln` 代码行的顶部 y 坐标，未布局时返回 -1。

- void CaretFromMouse(App app, Rect area, int gutterW, int fontSize)
  - 根据文本区域内的鼠标位置（像素）放置光标。

- void ClearExtraCarets()

- void AddCaretBelow()
  - 在最下方活动光标的下一行添加辅助光标。

- void AddCaretAbove()
  - 在最上方活动光标的上一行添加辅助光标。

- void McInsertStr(string ins)
  - 在主光标和所有辅助光标处插入 `ins`。
    所有光标分处不同行，各行编辑互不影响。

- void McBackspace()
  - 在每个光标处退格。当主光标
    位于第 0 列时（合并行会改变索引）退化为单光标。

- void StartSelection()

- string GetSelectedText()

- void DeleteSelection()

- void CopySelection()

- void CutSelection()

- void PasteClipboard()

- void InsertBlock(string ins)
  - 在光标处插入可能含多行的文本（替换当前
    选区），与粘贴 `ins` 完全一致。

- void DeleteAtCaret()
  - 删除当前选区；无选区时删除光标处的字符。

- void ToggleComment()
  - 在当前行切换行首行注释（本语言的引导符：Zan "// "、
    Lua "-- "、Python "# "），或对选区覆盖的每一行切换。

- void DuplicateLine()
  - 在当前行正下方复制一行。

- void DeleteLine()
  - 整行删除当前行。

- static string LTrim(string s)

- static string Uncomment(string s)
  - 删除前导空白后的第一个"// "（或"//"）。

- static string UncommentTok(string s, string tok)
  - 删除前导空白后的第一个 `tok`（及紧随的一个空格），
    `tok` 是该语言的行注释引导符。

- string TakeCommand()
  - 返回并清除右键菜单请求的待处理宿主命令
    （如"find"、"gotoline"）；无命令时返回空字符串。

- static string T(string en, string zh)
  - 本地化标签：按 `lang` 显示英文或中文。

- static List<ContextMenuItem> BuildCtxMenu()
  - 构建右键菜单项。Action 为 0 表示分隔行。

- void RunCtxAction(App app, int act)
  - 按 id 执行右键菜单动作（见 BuildCtxMenu）。

- bool CtxMenuOpen()

- void RenderCtxMenu(App app)
  - 将右键菜单绘制为顶层浮层（限制在整个
    窗口而非编辑器矩形内），并处理菜单项点击。必须在
    帧末调用，以免被其他内容覆盖。

- void CheckErrors()

- void EnsureLineScan()
  - 刷新括号扫描的按行缓存。诊断在每次光标换行时重跑，而整篇
    逐字符扫描在 30 万行上是百毫秒级；改成只重扫文本真的变了的行
    后，一次击键只重扫一行。

- void ScanLineBrackets(int li)
  - 重算一行的括号增量和 using 启发式，写回按行缓存。


## CodeLensRef (class)

一条 CodeLens "N references" 标注：声明所在行、
所标注的名称及可点击的屏幕矩形（x/y/width）。用单个实体
取代五个并行列表。

- int line;

- string name;

- int x;

- int y;

- int w;

- int count;
  - 透镜显示的内容：本缓冲区的使用数加项目其余
    部分的使用数。点击时与缓冲区可列出的比较，
    不同则请求宿主提供完整列表。

- CodeLensRef(int line, string name, int x, int y, int w, int count)


## CodeSpan (class)

一个语法高亮文本区间。

- string text;

- int color;

- CodeSpan(string text, int color)


## ContextMenuItem (class)

- string label;

- string key;

- int action;

- ContextMenuItem(string label, string key, int action)


## DbgVar (class)

调试器变量的一行（名称 / 类型 / 值），显示在 Variables
面板和编辑器调试悬停提示中。用单个实体取代旧的
制表符分隔的 "name\ttype\tvalue" 字符串。

- string name;

- string type;

- string val;

- DbgVar(string name, string type, string val)


## Diagnostic (class)

一条诊断：所在行（从 0 起）和消息。
用单个实体取代并行的行/消息列表。

- int line;

- string msg;

- Diagnostic(int line, string msg)


## EditSnapshot (class)

一条撤销/重做记录：完整的缓冲区文本及要恢复的
光标行/列。用单个实体取代并行的文本/行/列栈。

- string text;

- int line;

- int col;

- EditSnapshot(string text, int line, int col)


## EditorPalette (class)

代码编辑器语法高亮及其
弹出组件（右键菜单、自动补全、悬停提示、引用列表）的语义颜色。
语法与选区颜色跟随当前皮肤（亮/暗
背景）；弹出组件在任何皮肤下都保持经典暗色调色板，
确保浮层在任意编辑器背景上都清晰可读。

- int keyword;

- int type;

- int str;

- int comment;

- int number;

- int plain;

- int selection;

- int popupBg;

- int panelBg;

- int hoverBg;

- int border;

- int selBg;

- int accent;

- int signature;

- int menuText;

- int text;

- int textSel;

- int muted;

- int lineNo;

- int link;

- int iconMethod;

- int iconType;

- int iconSnippet;

- int iconEnum;

- int iconDefault;

- int iconDisabled;

- int iconGlyph;

- int iconGlyphDisabled;

- int error;

- int errorDim;

- int errTipBg;

- int errTipText;

- int warn;

- int warnLineBg;

- int changeLabel;

- int caretSecondary;

- int minimapBg;

- int minimapThumb;

- int scrollThumb;

- int shadowSoft;

- int shadow;

- int shadowMed;

- int shadowStrong;

- static EditorPalette Dark()
  - 经典暗色调色板（VS 风格）。

- static EditorPalette Light()
  - 浅色背景语法，弹出组件保持不变。

- static EditorPalette For(Gui.Theme t)
  - 根据皮肤背景亮度选择对应的调色板。

- static EditorPalette Preset(int idx)
  - 编辑器配色主题选择器使用的命名预设语法调色板，基于
    暗色 chrome。idx：0 = Dark+（默认），1 = Monokai，2 = Dracula，
    3 = Solarized Dark，4 = Night Owl，5 = High Contrast。

- static EditorPalette Chrome()
  - 两种调色板共用的暗色弹出组件。


## ProjType (class)

一个项目类型：名称及其空格分隔的
声明成员名列表。用单个实体取代并行的名称/成员列表。

- string name;

- string members;

- ProjType(string name, string members)


## QuickFix (class)

一条缺失 using 的快速修复：其解决错误的行以及
要导入的命名空间。用单个实体取代并行的行/命名空间列表。

- int line;

- string ns;

- QuickFix(int line, string ns)
