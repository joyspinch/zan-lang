# System.Linq

> 源码: `packages/Zan.Linq/src/System/Linq/Enumerable.zan`, `packages/Zan.Linq/src/System/Linq/Expression.zan`, `packages/Zan.Linq/src/System/Linq/Stream.zan`


## Enumerable (class)

- static Stream<T> AsStream<T>(this List<T> src)

- static List<T> Where<T>(this List<T> src, Predicate<T> pred)

- static List<R> Select <T, R>(this List<T> src, Selector <T, R> selector)

- static List<R> SelectMany <T, R>(this List<T> src, Selector <T, List<R>> selector)

- static bool Any<T>(this List<T> src, Predicate<T> pred)

- static bool Any<T>(this List<T> src)

- static bool All<T>(this List<T> src, Predicate<T> pred)

- static int Count<T>(this List<T> src, Predicate<T> pred)

- static int Count<T>(this List<T> src)

- static T First<T>(this List<T> src)

- static T First<T>(this List<T> src, Predicate<T> pred)

- static T FirstOrDefault<T>(this List<T> src)

- static T FirstOrDefault<T>(this List<T> src, Predicate<T> pred)

- static T Last<T>(this List<T> src)

- static T Last<T>(this List<T> src, Predicate<T> pred)

- static T LastOrDefault<T>(this List<T> src)

- static T LastOrDefault<T>(this List<T> src, Predicate<T> pred)

- static T Single<T>(this List<T> src)

- static T Single<T>(this List<T> src, Predicate<T> pred)

- static T SingleOrDefault<T>(this List<T> src)

- static T SingleOrDefault<T>(this List<T> src, Predicate<T> pred)

- static bool Contains<T>(this List<T> src, T target)

- static bool Contains<T>(this List<T> src, T target, EqualityComparer<T> eq)

- static bool Contains(this List<string> src, string target)

- static bool ContainsStr(this List<string> src, string target)

- static List<T> ToList<T>(this List<T> src)

- static List<T> Take<T>(this List<T> src, int count)

- static List<T> TakeWhile<T>(this List<T> src, Predicate<T> pred)

- static List<T> Skip<T>(this List<T> src, int count)

- static List<T> SkipWhile<T>(this List<T> src, Predicate<T> pred)

- static List<T> Reverse<T>(this List<T> src)

- static List<T> Distinct<T>(this List<T> src)

- static List<T> Distinct<T>(this List<T> src, EqualityComparer<T> eq)

- static List<string> Distinct(this List<string> src)

- static List<string> DistinctStr(this List<string> src)

- static List<T> OrderBy<T>(this List<T> src, StrKeySelector<T> key)

- static List<T> OrderBy<T>(this List<T> src, NumKeySelector<T> key)

- static List<T> OrderByDescending<T>(this List<T> src, StrKeySelector<T> key)

- static List<T> OrderByDescending<T>(this List<T> src, NumKeySelector<T> key)

- static List<T> OrderBy<T>(this List<T> src, KeySelector<T> key)

- static List<T> OrderBy<T>(this List<T> src, KeySelector<T> key1, KeySelector<T> key2)

- static List<T> OrderByDescending<T>(this List<T> src, KeySelector<T> key)

- static List<T> OrderByStr<T>(this List<T> src, StrKeySelector<T> key)

- static List<T> OrderByStrDescending<T>(this List<T> src, StrKeySelector<T> key)

- static List<T> OrderByNum<T>(this List<T> src, NumKeySelector<T> key)

- static List<T> OrderByNumDescending<T>(this List<T> src, NumKeySelector<T> key)

- static List<T> MergeSortInt<T>(List<T> src, KeySelector<T> key, int sign)

- static List<T> MergeSortInt2<T>(List<T> src, KeySelector<T> key1, KeySelector<T> key2)

- static List<T> MergeSortStr<T>(List<T> src, StrKeySelector<T> key, bool desc)

- static List<T> MergeSortNum<T>(List<T> src, NumKeySelector<T> key, bool desc)

- static List<T> OrderByKeysInt<T>(this List<T> src, List<int> keys)

- static List<T> OrderByKeysIntDescending<T>(this List<T> src, List<int> keys)

- static List<T> OrderByKeysLong<T>(this List<T> src, List<long> keys)

- static List<T> OrderByKeysLongDescending<T>(this List<T> src, List<long> keys)

- static List<T> OrderByKeysNum<T>(this List<T> src, List<double> keys)

- static List<T> OrderByKeysNumDescending<T>(this List<T> src, List<double> keys)

- static List<T> OrderByKeysStr<T>(this List<T> src, List<string> keys)

- static List<T> OrderByKeysStrDescending<T>(this List<T> src, List<string> keys)

- static List<T> MergeSortKeysInt<T>(List<T> src, List<int> keys, int sign)

- static List<T> MergeSortKeysLong<T>(List<T> src, List<long> keys, int sign)

- static List<T> MergeSortKeysStr<T>(List<T> src, List<string> keys, bool desc)

- static List<T> MergeSortKeysNum<T>(List<T> src, List<double> keys, bool desc)

- static List <Grouping<T>> GroupBy<T>(this List<T> src, KeySelector<T> key)

- static List <Grouping<T>> GroupBy<T>(this List<T> src, StrKeySelector<T> key)

- static List <Grouping<T>> GroupByStr<T>(this List<T> src, StrKeySelector<T> key)

- static List <Grouping<R>> GroupByKeysInt <T, R>(this List<T> src, List<int> keys, List<R> items)

- static List <Grouping<R>> GroupByKeysStr <T, R>(this List<T> src, List<string> keys, List<R> items)

- static A Aggregate <T, A>(this List<T> src, A seed, Accumulator <T, A> folder)

- static int Sum(this List<int> src)

- static double Sum(this List<double> src)

- static double Sum<T>(this List<T> src, NumKeySelector<T> key)

- static double SumNum(this List<double> src)

- static int Sum<T>(this List<T> src, KeySelector<T> key)

- static double SumNum<T>(this List<T> src, NumKeySelector<T> key)

- static int Min(this List<int> src)

- static double Min(this List<double> src)

- static double MinNum(this List<double> src)

- static int Min<T>(this List<T> src, KeySelector<T> key)

- static int Max(this List<int> src)

- static double Max(this List<double> src)

- static double MaxNum(this List<double> src)

- static int Max<T>(this List<T> src, KeySelector<T> key)

- static double Average(this List<int> src)

- static double Average(this List<double> src)

- static double AverageNum(this List<double> src)

- static double Average<T>(this List<T> src, KeySelector<T> key)

- static List<string> Like(this List<string> src, string pattern)

- static bool LikeMatch(string text, string pattern)

- static bool LikeAt(string t, int ti, string p, int pi)

- static List<T> In<T>(this List<T> src, List<T> values)

- static List<T> In<T>(this List<T> src, List<T> values, EqualityComparer<T> eq)

- static List<string> In(this List<string> src, List<string> values)

- static List<string> InStr(this List<string> src, List<string> values)

- static List<T> TakeTop<T>(this List<T> src, KeySelector<T> key, int k)

- static List<T> TakeTopDescending<T>(this List<T> src, KeySelector<T> key, int k)

- static List<T> Union<T>(this List<T> first, List<T> second)

- static List<string> UnionStr(this List<string> first, List<string> second)

- static List<T> Intersect<T>(this List<T> first, List<T> second)

- static List<string> IntersectStr(this List<string> first, List<string> second)

- static List<T> Except<T>(this List<T> first, List<T> second)

- static List<string> ExceptStr(this List<string> first, List<string> second)

- static Dict <int, T> ToDictInt<T>(this List<T> src, KeySelector<T> keySel)

- static Dict <string, T> ToDictStr<T>(this List<T> src, StrKeySelector<T> keySel)

- static Dict <int, V> ToDictInt <T, V>(this List<T> src, KeySelector<T> keySel, Selector <T, V> valSel)

- static Dict <string, V> ToDictStr <T, V>(this List<T> src, StrKeySelector<T> keySel, Selector <T, V> valSel)


## Expr (class)

- public ExprNode Root;

- public Expr()

- static Expr<T> From(ExprNode root)


## ExprNode (class)

- public int Kind;

- public string Name;

- public ExprNode A;

- public ExprNode B;

- public int TVal;

- public int IVal;

- public double DVal;

- public string SVal;

- public bool BVal;

- public ExprNode()

- static ExprNode Member(string n)

- static ExprNode Const(int v)

- static ExprNode Const(double v)

- static ExprNode Const(string s)

- static ExprNode Const(bool v)

- static ExprNode Binary(string op, ExprNode l, ExprNode r)

- static ExprNode Call(string fn, ExprNode recv, ExprNode arg)

- static ExprNode Lambda(string p, ExprNode body)

- static ExprNode Not(ExprNode x)


## Grouping (class)

- public string Key;

- public int IntKey;

- public List<T> Items;

- public Grouping()


## Stream (class)

- List<T> src;

- List <Predicate<T>> filters;

- Predicate<T> takeWhilePred;

- int skipN;

- int takeN;

- bool distinct;

- public Stream(List<T> src)

- static Stream<T> Of(List<T> src)

- Stream<T> Where(Predicate<T> pred)

- Stream<T> Skip(int n)

- Stream<T> Take(int n)

- Stream<T> TakeWhile(Predicate<T> pred)

- Stream<T> Distinct()

- Stream<R> Select<R>(Selector <T, R> selector)

- List<T> ToList()

- T First()

- T FirstOrDefault(T dflt)

- bool Any()

- bool Any(Predicate<T> pred)

- bool All(Predicate<T> pred)

- int Count()

- void ForEach(Action<T> action)


## A (delegate)

`delegate A Accumulator <T, A>(A acc, T item);`


## R (delegate)

`delegate R Selector <T, R>(T item);`


## bool (delegate)

`delegate bool Predicate<T>(T item);`


## bool (delegate)

`delegate bool EqualityComparer<T>(T a, T b);`


## double (delegate)

`delegate double NumKeySelector<T>(T item);`


## int (delegate)

`delegate int KeySelector<T>(T item);`


## string (delegate)

`delegate string StrKeySelector<T>(T item);`


## void (delegate)

`delegate void Action<T>(T item);`
