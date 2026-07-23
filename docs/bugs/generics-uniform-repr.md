# Generic instantiation bugs (uniform-representation lowering)

Status: OPEN — found 2026-07-23 while extending System.Linq. Each item below
was reproduced with a minimal program against `build/zanc` (Windows, O0
default flags, `--auto-stdlib`).

The common theme: inside a generic function/class, values of an unbound type
parameter appear to be handled as raw machine words, so type-specific
semantics (string/double comparison, ARC releases, overload preference) are
lost. These are the root causes that forced the typed-key API shape of
`System.Linq.Enumerable` (OrderByStr/OrderByNum/SumNum/ContainsStr instead of
plain C#-style overloads).

## 1. `<` / `>` on a generic key compares raw bits

```csharp
static T MinBy<T, R>(this List<T> src, Sel<T, R> key) {
    R mk = key(src[0]);
    ... if (k < mk) ...            // R=string → pointer compare; R=double → int-bit compare
}
```
`MinBy(u => u.name)` and `List<double>.MinBy(x => x)` both return wrong
elements. Statically-typed `string < string` and `double < double` work.

## 2. `==` on generic operands compares identity for strings

```csharp
static bool Same<T>(T a, T b) { return a == b; }
Same("he" + "llo", "hello")       // false (pointer compare); Same(3, 3) → true
```
Affects `Enumerable.Contains/Distinct/In` for `List<string>` whenever the
strings are not interned literals. Workarounds: ContainsStr/DistinctStr/InStr.

## 3. Overloads differing only in delegate return type mis-bind untyped lambdas

```csharp
static string Pick<T>(this List<T> s, IK<T> k);   // delegate int IK<T>(T)
static string Pick<T>(this List<T> s, SK<T> k);   // delegate string SK<T>(T)
us.Pick(x => x.name)              // silently binds the IK overload → garbage int
```
The binder does not use the lambda body's type to choose; it picks the first
candidate. Same-arity overloads on delegate types are therefore unsafe.

## 4. A typed overload is not preferred over a generic one

```csharp
static bool Has<T>(this List<T> src, T v);          // generic
static bool Has(this List<string> src, string v);    // typed, value compare
xs.Has("hello")                    // xs: List<string> → generic overload wins
```
Also applies to `Sum(this List<int>)` vs `Sum(this List<double>)`: a
`List<double>` receiver resolves to the `List<int>` overload and prints raw
bit patterns. Hence the SumNum/MinNum/MaxNum/AverageNum names.

## 5. Dictionary with a generic-class value returns corrupt values

```csharp
Dictionary<string, Box<int>> d = ...; d.Add("x", b);   // b.V = 7
d["x"].V                          // 0 (corrupt)
```
`Dictionary<string, int>` Add/indexer read+write work.

## 6. `foreach` over `Dictionary.Keys` fails LLVM verification

```
LLVM verification failed: Load operand must be a pointer.
  %data = load ptr, i64 getelementptr inbounds nuw (%List, i64 0, i32 0, i32 2)
```
Any `foreach (string k in d.Keys)` reproduces it.

## 7. Reassigning a generic accumulator leaks the old value

```csharp
static A Aggregate<T, A>(this List<T> src, A seed, Accumulator<T, A> f) {
    A acc = seed;
    ... acc = f(acc, src[i]); ...   // A=string: the replaced string is never released
}
```
`users.Aggregate("", (string acc, User u) => acc + u.name)` leaks
`src.Count - 1` intermediate strings (`--check-leaks`). Int accumulators are
fine. This is why `tests/conformance/linq_extended.zan` uses an int
accumulator for its Aggregate case.
