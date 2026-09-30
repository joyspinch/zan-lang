# System.Linq

> 源码: `stdlib/System/Linq/Enumerable.zan`, `stdlib/System/Linq/Expression.zan`, `stdlib/System/Linq/Stream.zan`


## Enumerable (class)

对 List<T> 的类型化、即时求值的 LINQ 风格查询操作符。

操作符是扩展方法（`this List<T>` 接收者），可像 C# 一样链式调用；
lambda 参数类型从接收者的元素类型推断。
静态调用形式（Enumerable.Where(nums, pred)）同样可用。
结果物化为新的 List，因此可以复用并
多次枚举。

using System.Linq;
List<int> evens = nums.Where(x => x % 2 == 0);
List<User> adults = users.Where(u => u.age >= 18).OrderBy(u => u.age);
List<User> byName = users.OrderByStr(u => u.name);
bool ok = nums.Any(x => x > 100);

排序键是类型化的：OrderBy 接受 int 键，OrderByStr 接受 string 键，
OrderByNum 接受 double 键（分开命名是因为仅 delegate 返回类型不同的重载
无法从无类型 lambda 解析）。
OrderBy(k1, k2) 按主、次两个 int 键排序。出于同样
的原因，double 序列聚合使用 *Num 命名
（SumNum/MinNum/MaxNum/AverageNum）。

First/Last/Single 在空序列（Single 对不明确的序列）时抛出
InvalidOperationException，与 C# 一致；*OrDefault 变体
则返回类型默认值。

- static Stream<T> AsStream<T>(this List<T> src)
  - 为序列开启零分配惰性流水线（Stream Pipeline）。
    中间操作（Where/Skip/Take/TakeWhile/Distinct/Select）不分配临时列表，
    终结操作（ToList/First/Count/Any/All/ForEach）进行单趟遍历并短路提前终止。

- static List<T> Where<T>(this List<T> src, Predicate<T> pred)
  - 保留 pred 返回 true 的元素。

- static List<R> Select <T, R>(this List<T> src, Selector <T, R> selector)
  - 用 selector 投影每个元素。

- static List<R> SelectMany <T, R>(this List<T> src, Selector <T, List<R>> selector)
  - 将每个元素投影为 List 并展平结果。

- static bool Any<T>(this List<T> src, Predicate<T> pred)
  - 任一元素满足 pred 时返回 true。

- static bool Any<T>(this List<T> src)
  - 序列至少有一个元素时返回 true。

- static bool All<T>(this List<T> src, Predicate<T> pred)
  - 所有元素都满足 pred 时返回 true。

- static int Count<T>(this List<T> src, Predicate<T> pred)
  - 满足 pred 的元素个数。

- static int Count<T>(this List<T> src)
  - 元素总数。

- static T First<T>(this List<T> src)
  - 第一个元素；为空时抛出 InvalidOperationException。

- static T First<T>(this List<T> src, Predicate<T> pred)
  - 第一个满足 pred 的元素；无匹配时抛出。

- static T FirstOrDefault<T>(this List<T> src)
  - 第一个元素；为空时返回类型默认值。

- static T FirstOrDefault<T>(this List<T> src, Predicate<T> pred)
  - 第一个满足 pred 的元素，否则返回类型默认值。

- static T Last<T>(this List<T> src)
  - 最后一个元素；为空时抛出 InvalidOperationException。

- static T Last<T>(this List<T> src, Predicate<T> pred)
  - 最后一个满足 pred 的元素；无匹配时抛出。

- static T LastOrDefault<T>(this List<T> src)
  - 最后一个元素；为空时返回类型默认值。

- static T LastOrDefault<T>(this List<T> src, Predicate<T> pred)
  - 最后一个满足 pred 的元素，否则返回类型默认值。

- static T Single<T>(this List<T> src)
  - 唯一的元素；序列不恰为单个元素时抛出。

- static T Single<T>(this List<T> src, Predicate<T> pred)
  - 唯一满足 pred 的元素；零个或多个时抛出。

- static T SingleOrDefault<T>(this List<T> src)
  - 唯一的元素；为空时返回类型默认值，多个时抛出。

- static T SingleOrDefault<T>(this List<T> src, Predicate<T> pred)
  - 唯一匹配的元素，否则返回类型默认值；多个时抛出。

- static bool Contains<T>(this List<T> src, T target)
  - target 存在时返回 true（对元素类型使用 ==；
    对引用类型是同一性比较，而非结构相等）。

- static bool Contains<T>(this List<T> src, T target, EqualityComparer<T> eq)
  - 在给定的相等比较器下 target 存在时返回 true
    （对引用类型采用 Equals 风格语义）。

- static bool Contains(this List<string> src, string target)
  - 按字符串值比较，target 存在时返回 true
    （ContainsStr 的 C# 风格重载）。

- static bool ContainsStr(this List<string> src, string target)
  - 按字符串值比较，target 存在时返回 true。

- static List<T> ToList<T>(this List<T> src)
  - 序列的浅拷贝，作为新 List。

- static List<T> Take<T>(this List<T> src, int count)
  - 前 count 个元素（不足则全部）。

- static List<T> TakeWhile<T>(this List<T> src, Predicate<T> pred)
  - 取 pred 成立时的前导元素。

- static List<T> Skip<T>(this List<T> src, int count)
  - 跳过前 count 个元素后的所有元素。

- static List<T> SkipWhile<T>(this List<T> src, Predicate<T> pred)
  - pred 成立的前导段之后的元素。

- static List<T> Reverse<T>(this List<T> src)
  - 反转顺序后的元素。

- static List<T> Distinct<T>(this List<T> src)
  - 去重元素，保留首次出现（使用 ==；
    对引用类型是同一性比较，而非结构相等）。基于哈希表 O(N) 实现。

- static List<T> Distinct<T>(this List<T> src, EqualityComparer<T> eq)
  - 在给定相等比较器下去重，保留
    首次出现（对引用类型采用 Equals 风格语义）。

- static List<string> Distinct(this List<string> src)
  - 按值比较去重字符串（DistinctStr 的 C# 风格重载；
    由接收者的元素类型选择该重载）。

- static List<string> DistinctStr(this List<string> src)
  - 按值比较去重字符串，保留首次出现。基于哈希表 O(N) 实现。

- static List<T> OrderBy<T>(this List<T> src, StrKeySelector<T> key)
  - 按字符串键升序排序（OrderByStr 的 C# 风格重载；
    由 lambda 的键类型选择该重载）。

- static List<T> OrderBy<T>(this List<T> src, NumKeySelector<T> key)
  - 按 double 键升序排序（OrderByNum 的 C# 风格重载；
    OrderByNum）。

- static List<T> OrderByDescending<T>(this List<T> src, StrKeySelector<T> key)
  - 按字符串键降序排序。

- static List<T> OrderByDescending<T>(this List<T> src, NumKeySelector<T> key)
  - 按 double 键降序排序。

- static List<T> OrderBy<T>(this List<T> src, KeySelector<T> key)
  - 按整数键升序排序（稳定归并排序）。

- static List<T> OrderBy<T>(this List<T> src, KeySelector<T> key1, KeySelector<T> key2)
  - 按主、次两个整数键升序排序（稳定）。

- static List<T> OrderByDescending<T>(this List<T> src, KeySelector<T> key)
  - 按整数键降序排序（稳定归并排序）。

- static List<T> OrderByStr<T>(this List<T> src, StrKeySelector<T> key)
  - 按字符串键升序排序（稳定归并排序）。

- static List<T> OrderByStrDescending<T>(this List<T> src, StrKeySelector<T> key)
  - 按字符串键降序排序（稳定归并排序）。

- static List<T> OrderByNum<T>(this List<T> src, NumKeySelector<T> key)
  - 按 double 键升序排序（稳定归并排序）。

- static List<T> OrderByNumDescending<T>(this List<T> src, NumKeySelector<T> key)
  - 按 double 键降序排序（稳定归并排序）。

- static List<T> MergeSortInt<T>(List<T> src, KeySelector<T> key, int sign)

- static List<T> MergeSortStr<T>(List<T> src, StrKeySelector<T> key, bool desc)

- static List<T> MergeSortNum<T>(List<T> src, NumKeySelector<T> key, bool desc)

- static List<T> OrderByKeysInt<T>(this List<T> src, List<int> keys)
  - 按预先计算的整数键列表稳定排序（升序）。

- static List<T> OrderByKeysIntDescending<T>(this List<T> src, List<int> keys)
  - 按预先计算的整数键列表稳定排序（降序）。

- static List<T> OrderByKeysLong<T>(this List<T> src, List<long> keys)
  - 按预先计算的 long 键列表稳定排序（升序）。

- static List<T> OrderByKeysLongDescending<T>(this List<T> src, List<long> keys)
  - 按预先计算的 long 键列表稳定排序（降序）。

- static List<T> OrderByKeysNum<T>(this List<T> src, List<double> keys)
  - 按预先计算的 double 键列表稳定排序（升序）。

- static List<T> OrderByKeysNumDescending<T>(this List<T> src, List<double> keys)
  - 按预先计算的 double 键列表稳定排序（降序）。

- static List<T> OrderByKeysStr<T>(this List<T> src, List<string> keys)
  - 按预先计算的字符串键列表稳定排序（升序）。

- static List<T> OrderByKeysStrDescending<T>(this List<T> src, List<string> keys)
  - 按预先计算的字符串键列表稳定排序（降序）。

- static List<T> MergeSortKeysInt<T>(List<T> src, List<int> keys, int sign)

- static List<T> MergeSortKeysLong<T>(List<T> src, List<long> keys, int sign)

- static List<T> MergeSortKeysStr<T>(List<T> src, List<string> keys, bool desc)

- static List<T> MergeSortKeysNum<T>(List<T> src, List<double> keys, bool desc)

- static List <Grouping<T>> GroupBy<T>(this List<T> src, KeySelector<T> key)
  - 按整数键对元素分组。Grouping.IntKey 保存
    键；Grouping.Key 保存其字符串形式。分组顺序按首次
    出现排序。基于哈希表 O(N) 实现。

- static List <Grouping<T>> GroupBy<T>(this List<T> src, StrKeySelector<T> key)
  - 按字符串键分组（GroupByStr 的 C# 风格重载；
    由 lambda 的键类型选择该重载）。

- static List <Grouping<T>> GroupByStr<T>(this List<T> src, StrKeySelector<T> key)
  - 按字符串键（按值比较）分组。分组
    顺序按首次出现排列。基于哈希表 O(N) 实现。

- static List <Grouping<R>> GroupByKeysInt <T, R>(this List<T> src, List<int> keys, List<R> items)
  - 按预先计算的整数键分组（键与元素列表并行；
    Grouping.Items 为投影后的元素）。基于哈希表 O(N) 实现。

- static List <Grouping<R>> GroupByKeysStr <T, R>(this List<T> src, List<string> keys, List<R> items)
  - 按预先计算的字符串键分组（键与元素列表并行；
    Grouping.Items 为投影后的元素）。基于哈希表 O(N) 实现。

- static A Aggregate <T, A>(this List<T> src, A seed, Accumulator <T, A> folder)
  - 从 seed 开始把序列折叠进累加器。

- static int Sum(this List<int> src)
  - 整数序列的和。

- static double Sum(this List<double> src)
  - double 序列的和（SumNum 的 C# 风格重载；
    由接收者的元素类型选择该重载）。

- static double Sum<T>(this List<T> src, NumKeySelector<T> key)
  - 序列上 double 键之和。

- static double SumNum(this List<double> src)
  - double 序列的和。

- static int Sum<T>(this List<T> src, KeySelector<T> key)
  - 序列上整数键之和。

- static double SumNum<T>(this List<T> src, NumKeySelector<T> key)
  - 序列上 double 键之和。

- static int Min(this List<int> src)
  - 整数序列的最小值（为空时为 0）。

- static double Min(this List<double> src)
  - double 序列的最小值（MinNum 的 C# 风格重载；
    由接收者的元素类型选择该重载）。

- static double MinNum(this List<double> src)
  - double 序列的最小值（为空时为 0）。

- static int Min<T>(this List<T> src, KeySelector<T> key)
  - 序列上最小整数键（为空时为 0）。

- static int Max(this List<int> src)
  - 整数序列的最大值（为空时为 0）。

- static double Max(this List<double> src)
  - double 序列的最大值（MaxNum 的 C# 风格重载；
    MaxNum）。

- static double MaxNum(this List<double> src)
  - double 序列的最大值（为空时为 0）。

- static int Max<T>(this List<T> src, KeySelector<T> key)
  - 序列上最大整数键（为空时为 0）。

- static double Average(this List<int> src)
  - 整数序列的平均值；为空时抛出。

- static double Average(this List<double> src)
  - double 序列的平均值（AverageNum 的 C# 风格重载；
    AverageNum）。

- static double AverageNum(this List<double> src)
  - double 序列的平均值；为空时抛出。

- static double Average<T>(this List<T> src, KeySelector<T> key)
  - 序列上整数键的平均值；为空时抛出。

- static List<string> Like(this List<string> src, string pattern)
  - 对字符串序列执行 SQL LIKE：'%' 匹配任意连续
    字符，'_' 恰好匹配一个（如 "a%"、"%.log"、"b_b"）。

- static bool LikeMatch(string text, string pattern)
  - text 匹配 SQL LIKE 模式（'%'/'_'）时返回 true。

- static bool LikeAt(string t, int ti, string p, int pi)
  - LikeMatch 的递归体：'%' 尝试消耗任意长度，'_' 匹配单个字符。

- static List<T> In<T>(this List<T> src, List<T> values)
  - SQL IN：保留 values 中存在的元素（使用 ==）。基于哈希加速。

- static List<T> In<T>(this List<T> src, List<T> values, EqualityComparer<T> eq)
  - 在给定相等比较器下的 SQL IN（对引用类型
    采用 Equals 风格语义）。

- static List<string> In(this List<string> src, List<string> values)
  - 对字符串执行 SQL IN，按值比较（InStr 的 C# 风格重载；
    InStr）。

- static List<string> InStr(this List<string> src, List<string> values)
  - 对字符串执行 SQL IN，按值比较。基于哈希加速。

- static List<T> TakeTop<T>(this List<T> src, KeySelector<T> key, int k)
  - 基于优先队列（堆）求整数键最小的前 k 个元素（升序排序，O(N log K)）。
    当 k 远小于序列长度时，显著优于全量排序后再 Take。

- static List<T> TakeTopDescending<T>(this List<T> src, KeySelector<T> key, int k)
  - 基于优先队列（堆）求整数键最大的前 k 个元素（降序排序，O(N log K)）。

- static List<T> Union<T>(this List<T> first, List<T> second)
  - 并集并去重（保留元素首次出现的顺序，O(N + M)）。

- static List<string> UnionStr(this List<string> first, List<string> second)
  - 字符串并集并去重（保留元素首次出现的顺序，O(N + M)）。

- static List<T> Intersect<T>(this List<T> first, List<T> second)
  - 交集并去重（保留在 first 中首次出现的顺序，O(N + M)）。

- static List<string> IntersectStr(this List<string> first, List<string> second)
  - 字符串交集并去重（保留在 first 中首次出现的顺序，O(N + M)）。

- static List<T> Except<T>(this List<T> first, List<T> second)
  - 差集并去重（属于 first 且不属于 second 的元素，保留首次出现的顺序，O(N + M)）。

- static List<string> ExceptStr(this List<string> first, List<string> second)
  - 字符串差集并去重（属于 first 且不属于 second 的元素，保留首次出现的顺序，O(N + M)）。

- static Dict <int, T> ToDictInt<T>(this List<T> src, KeySelector<T> keySel)
  - 将序列转换为 Dict<int, T> 映射表（整数键，值为元素本身）。

- static Dict <string, T> ToDictStr<T>(this List<T> src, StrKeySelector<T> keySel)
  - 将序列转换为 Dict<string, T> 映射表（字符串键，值为元素本身）。

- static Dict <int, V> ToDictInt <T, V>(this List<T> src, KeySelector<T> keySel, Selector <T, V> valSel)
  - 将序列转换为 Dict<int, V> 映射表（整数键，自定义值投影）。

- static Dict <string, V> ToDictStr <T, V>(this List<T> src, StrKeySelector<T> keySel, Selector <T, V> valSel)
  - 将序列转换为 Dict<string, V> 映射表（字符串键，自定义值投影）。


## Expr (class)

目标类型表达式包装器。类型参数 T 为 lambda 遍历的实体或
元素类型；声明了
<c>Expr<T></c> 参数的方法会以表达式树形式接收 lambda。

- public ExprNode Root;
  - 表达式树的根节点；未构造时为 null。

- public Expr()
  - 空包装：Root 为 null（经 `From` 装入树）。

- static Expr<T> From(ExprNode root)
  - 以给定的根节点包装成一个 Expr<T>。


## ExprNode (class)

可检查表达式树的节点。当 lambda 传给参数声明为
<c>Expr<T></c> 的方法时，编译器
（dbgen 阶段）会将其降级为 <c>ExprNode</c> 构造代码，而非
delegate，因此纯 Zan 代码可在运行时遍历该树——这是
ORM 查询构建、LINQ-to-objects 求值、校验规则和
权限表达式的基础。

节点类型：
0 = 成员访问（Name = 字段名）
1 = 常量（IVal / DVal / SVal / BVal，匹配的那个）
2 = 二元运算（Name = "==","!=","<",">","<=",">=","+","-","*","/","%","&&","||"；A、B 为操作数）
3 = 调用（Name = 方法名；A = 接收方树，当
lambda 参数本身作为接收方时为 null；B = 参数）
4 = lambda（Name = 参数名；A = 函数体）
5 = 非（A = 操作数）

- public int Kind;
  - 节点类型（见类注释的 0-5 编号）。

- public string Name;
  - 成员/运算符/方法/参数名（按 Kind 取义）。

- public ExprNode A;
  - 第一个操作数（接收方/函数体等）。

- public ExprNode B;
  - 第二个操作数（右操作数/调用参数）。

- public int TVal;
  - 常量负载类型：0 = int，1 = double，2 = string，3 = bool。

- public int IVal;
  - TVal=0 时的整数值。

- public double DVal;
  - TVal=1 时的浮点值。

- public string SVal;
  - TVal=2 时的字符串值。

- public bool BVal;
  - TVal=3 时的布尔值。

- public ExprNode()
  - 空节点：全字段取默认值（等 Static 工厂逐个赋值）。

- static ExprNode Member(string n)
  - 构造成员访问节点（Kind 0）。

- static ExprNode Const(int v)
  - 构造 int 常量节点（Kind 1，TVal 0）。

- static ExprNode Const(double v)
  - 构造 double 常量节点（Kind 1，TVal 1）。

- static ExprNode Const(string s)
  - 构造 string 常量节点（Kind 1，TVal 2）。

- static ExprNode Const(bool v)
  - 构造 bool 常量节点（Kind 1，TVal 3）。

- static ExprNode Binary(string op, ExprNode l, ExprNode r)
  - 构造二元运算节点（Kind 2；op 见类注释的运算符表）。

- static ExprNode Call(string fn, ExprNode recv, ExprNode arg)
  - 3 = 调用（Name = 方法名；A = 接收方树，当
    lambda 参数本身作为接收方时为 null；B = 单个参数树）。

- static ExprNode Lambda(string p, ExprNode body)
  - 构造 lambda 节点（Kind 4；p 为参数名）。

- static ExprNode Not(ExprNode x)
  - 构造逻辑非节点（Kind 5）。


## Grouping (class)

GroupBy 产生的一组结果：key 及其元素。

- public string Key;
  - 分组键的字符串形式（始终设置）。

- public int IntKey;
  - 使用 GroupBy(KeySelector) 分组时的整数键。

- public List<T> Items;
  - 组内元素（按出现顺序）。

- public Grouping()
  - 空组：键为空串、元素列表为空。


## Stream (class)

针对 `List<T>` 的零分配惰性求值流水线（Stream Pipeline）。

核心特性：
1. 零中间分配（Zero Allocation）：Where/Skip/Take/TakeWhile 不创建任何中间 List；
2. 短路提前终止（Short-Circuiting）：Take(k)、First()、Any() 在满足条件时立即 break，不再扫描后续元素；
3. 单趟遍历（Single Pass）：所有谓词在一个内联循环中流水线式过滤，CPU 缓存行高度亲和。

示例：
List<int> page = users.AsStream()
.Where((User u) => u.age >= 18)
.Skip(20)
.Take(10)
.Select<int>((User u) => u.id)
.ToList();

- List<T> src;

- List <Predicate<T>> filters;

- Predicate<T> takeWhilePred;

- int skipN;

- int takeN;

- bool distinct;

- public Stream(List<T> src)

- static Stream<T> Of(List<T> src)
  - 从 List 创建流。

- Stream<T> Where(Predicate<T> pred)
  - 保留满足谓词的元素（中间操作，惰性追加）。多次调用按 AND 组合。

- Stream<T> Skip(int n)
  - 跳过前 n 个满足过滤条件的元素（中间操作）。

- Stream<T> Take(int n)
  - 截取最多 n 个元素（中间操作，在终结操作时支持极速短路）。

- Stream<T> TakeWhile(Predicate<T> pred)
  - 当谓词返回 true 时持续提取，遇到首个 false 立即终结整条流（中间操作）。

- Stream<T> Distinct()
  - 流式去重（中间操作，基于哈希表保证唯一性）。

- Stream<R> Select<R>(Selector <T, R> selector)
  - 使用 selector 变换元素类型并开启新子流（中间操作）。

- List<T> ToList()
  - 物化为新的 List<T>（单趟遍历，按需提前终止）。

- T First()
  - 返回第一个匹配元素。匹配到立即退出循环（短路）。空序列抛异常。

- T FirstOrDefault(T dflt)
  - 返回第一个匹配元素，无匹配时返回给定的默认值。匹配到立即退出循环（短路）。

- bool Any()
  - 是否存在至少一个满足流过滤条件的元素（找到首个匹配项即短路退出）。

- bool Any(Predicate<T> pred)
  - 流中是否存在满足特定谓词的元素（短路）。

- bool All(Predicate<T> pred)
  - 流中是否所有满足现有过滤的元素均满足给定谓词（短路）。

- int Count()
  - 统计匹配元素总数（尊重 Take/Skip 约束）。

- void ForEach(Action<T> action)
  - 遍历消费流中每一个有效元素。


## A (delegate)

将累加器与下一个元素合并。

`delegate A Accumulator <T, A>(A acc, T item);`


## R (delegate)

将 T 类型的元素投影为 R 类型的值。

`delegate R Selector <T, R>(T item);`


## bool (delegate)

当元素应被保留时返回 true。

`delegate bool Predicate<T>(T item);`


## bool (delegate)

两个元素之间的结构相等性（对引用类型
而言 == 是同一性比较）。

`delegate bool EqualityComparer<T>(T a, T b);`


## double (delegate)

从元素提取浮点排序键。

`delegate double NumKeySelector<T>(T item);`


## int (delegate)

从元素提取整数排序键。

`delegate int KeySelector<T>(T item);`


## string (delegate)

从元素提取字符串排序键。

`delegate string StrKeySelector<T>(T item);`


## void (delegate)

对流中每个元素执行的操作。

`delegate void Action<T>(T item);`
