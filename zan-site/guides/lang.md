# 语言参考（Zan）

> 本文档基于 zanc 0.2.3 实测语义编写：所有能力均以 `tests/conformance/` 用例、
> `src/compiler/` 源码或 stdlib 实测为准。标 ✅ 表示已实现；标 ⚠️ 表示与 C# 习惯
> 有差异或有已知限制；标 ❌ 表示未实现（不要使用）。

## 快速开始

把下面代码存成 `main.zan`，编译运行：

```zan
using System;

class Program {
    static void Main() {
        Console.WriteLine("Hello, Zan!");
    }
}
```

命令行：

```bash
zanc main.zan --auto-stdlib -o main.exe     # 编译并链接
./main.exe                                  # 运行
zanc main.zan --auto-stdlib --publish -o app   # 发布构建（-Os、剥离调试、链接原生驱动）
```

- 没有 `project.zan` 项目文件：项目就是一组 `.zan` 源文件，全部传给 `zanc` 即可
  （或交给 IDE 管理）。
- 入口是某个类里的 `static void Main()`（也支持 `static int Main()`、
  `static void Main(string[] args)`、`static async int Main()`）。
- 不支持顶层语句（顶层只接受 `using` / `namespace` / 类型声明）。

各部分索引：词法 → 类型系统 → 声明 → 成员 → 语句 → 模式匹配 → 运算符 →
泛型 → 委托与闭包 → 初始化器 → 属性 → 预处理 → 异步 → 并发 → 内存与 ARC →
FFI → 工具链 → 与 C# 的差异。

## 词法

### 注释

```zan
// 单行注释（lexer 直接跳过）
/* 多行
   注释（可嵌套） */

/// 文档注释：词法上等价于 //（都会被跳过），
/// 但 zandoc 与标准库参考页把它当作文档注释提取，
/// 支持 <summary> / <returns> 等标签（见 src/doc/zandoc.zan）。
```

### 标识符

`[a-zA-Z_][a-zA-Z0-9_]*`，区分大小写，允许 Unicode 字母。源码 UTF-8（容忍 BOM）。

### 关键字（80 个)

`abstract as async await base bool break byte case catch char class const
continue default delegate do double else enum extern false finally fixed float
goto for foreach get if in int interface internal is let lock long namespace
new not nint null object operator out override private protected public
readonly ref return sbyte sealed set short sizeof static string struct switch
this throw true try typeof uint ulong unchecked unsafe ushort using var
virtual void weak when where while`

上下文关键字（按位置识别，可作普通标识符）：`record partial yield init
implicit explicit event params value nameof`。

### 整数与浮点字面量

```zan
42          // int（32 位有符号）
0xFF        // 十六进制
0b1010      // 二进制
0o77        // 八进制
1_000_000   // 数字分隔符
42L         // long
42U         // uint / ulong（按大小）
3.14        // double
3.14f       // float
1.0e10      // 科学计数法
```

### 字符串字面量

```zan
"hello"                 // 普通字符串（\n \r \t \\ \" \' \0）
@"C:\path\file"         // 逐字字符串：无转义，"" 表示一个引号
$"Hello {name}"         // 插值字符串
$"x={v:D4} y={v:X2} f={v:F2}"   // 支持 D/X/F/G/E 与自定义 "0.00"/"00000" 格式说明符
$"outer {inner $"{v}"}"  // 支持嵌套插值（有限深度）
```

⚠️ 不支持 `\x` / `\u` / `\U` 转义、`$@"..."` 组合形式、`"""` 原始字符串。

### 字符字面量

```zan
'A'      // char：8 字节的 Unicode 码点（sizeof(char) == 8）
'\n'
```

## 类型系统

### 类型分类

```zan
// 值类型（栈分配、赋值即复制）
bool byte sbyte short ushort int uint long ulong float double char nint
struct 定义的类型 / enum

// 引用类型（堆分配、ARC 管理）
class / interface / delegate / T[] 数组

// 特殊类型
string（不可变 UTF-8）/ object（引用类型根）/ void / T?（可空包装）
```

要点：
- `int` 是 32 位，`long` 是独立的 64 位类型；`char` 8 字节（Unicode 码点）。
- `nint` 是指针宽度整数（没有 `nuint`、没有 `T*` 指针类型）。
- `decimal` 是保留关键字但没有对应类型；用 `double` 代替。

### 类型推断与变量

```zan
var x = 42;              // int，仅限带初始化的局部变量
let pi = 3.14;           // 只读声明（目前仅声明语法，无强制检查）
const int MAX = 100;     // 编译期常量（实际上降级为 static readonly）
int n = 0;               // 显式类型
```

⚠️ 函数参数与返回类型必须显式；`var` 不能用于参数/字段。

### 可空类型

```zan
int? x = null;           // 可空值类型
Point? p = null;         // 可空 struct
string s = null;         // 引用类型本身可空

if (x.HasValue) { int v = x.Value; }
int y = x ?? 0;          // 空合并
```

✅ `?.` 空条件链已实现，支持 `a?.b`、`a?.M()`、`a?.b?.c`、与 `??` 连用
（`nb?.name ?? "dflt"`）。⚠️ `?.[` 索引器空条件不支持（`?.` 后只接受成员名）。

### 数组

```zan
int[] a = new int[10];                // 一维
int[,] m = new int[2, 3];             // 多维
int[][] j = new int[3][];             // 交错
int[][] j2 = new int[3][] { new int[]{1}, new int[]{2,3} };
var v = new int[] { 1, 2, 3 };        // 初始化器
a.Length;                             // 总长度
m.GetLength(1);                       // 第 1 维长度
```

数组是引用类型（ARC 管理），foreach 直接可用；`byte[]` 常用作二进制缓冲。

### string

`string` 是不可变、owned 的 UTF-8 字符串（不是 C# 的 UTF-16）。常用操作：
`Length` `Substring(start[, len])` `IndexOf` `LastIndexOf` `Contains`
`StartsWith` `EndsWith` `Replace(a, b)` `Trim` `ToUpper` `ToLower`
`Split(sep) → List<string>` `+` 连接。

`string` 静态类提供 `Format(fmt, params object[])`、`Join(sep, List<string>)`、
`IsNullOrEmpty`、`CompareOrdinal`。

### 类型转换与类型测试

```zan
int a = (int)3.9;         // C 风格转换
int b = 3.9 as int;       // as（引用/可空之间）
object o = ...;
if (o is string) { ... }
if (o is string s) { s.Length; }        // 类型模式绑定
if (o is not null) { ... }              // is not
if (x is not int v) { ... }
```

✅ `typeof(T)`、`sizeof(T)` 可用。❌ `default(T)` 表达式未实现。

## 声明

### 命名空间

```zan
namespace MyApp.Core;     // 文件作用域（推荐）
// 或
namespace MyApp.Core {
    class Service { }
}
```

### using

```zan
using System;              // 引入命名空间
using System.IO;
using static System.Math;  // 静态引用（解析器接受）
```

❌ `using A = B;` 别名、`global using` 未实现。✅ 调用可以是命名空间限定的：
`System.IO.File.ReadAllText(path)`。

### class / struct / interface

```zan
public class Animal {
    // 字段（可带初始化器）
    public string Name;

    // 构造（支持 : base(...) / : this(...) 链）
    public Animal(string name) { Name = name; }

    // 析构：引用计数归零时调用
    ~Animal() { Console.WriteLine("freed"); }

    // 属性（get/set；init 只读初始化器；表达式体成员）
    public string Label {
        get { return Name; }
        set { Name = value; }          // value 是 setter 隐式参数
    }

    // 方法
    public virtual void Speak() { Console.WriteLine("..."); }

    // 静态方法 / 静态字段 / 静态构造
    public static Animal Create(string n) { return new Animal(n); }
    static int _count;
    static Animal() { _count = 0; }
}

class Dog : Animal {
    public Dog(string n) : base(n) { }
    public override void Speak() { Console.WriteLine("Woof"); }
}
```

```zan
// 值类型：栈分配、复制语义、可实现接口、不能继承、无无参构造（默认零初始化）
public struct Vec2 {
    public float X;
    public float Y;
    public Vec2(float x, float y) { X = x; Y = y; }
    public float Length() => Math.Sqrt(X * X + Y * Y);
}

// 接口（可含默认方法实现；支持泛型与 struct/class 约束）
interface IShape { float Area(); }
interface IComparable<T> { int CompareTo(T other); }
```

修饰符：`public private protected internal static virtual override abstract
sealed readonly extern async unsafe weak const partial`。

- ✅ `partial class/struct/interface` 跨文件合并（enum 不支持）。
- ✅ 嵌套类型（含嵌套 `static class`）。
- ✅ `readonly struct`、`ref struct`（接受，无特殊语义）。
- ❌ 无 `volatile`、`new` 遮蔽、`required`、`scoped`。

### enum

```zan
enum Color { Red, Green, Blue }           // 0,1,2
enum HttpStatus : int { OK = 200, NotFound = 404, ServerError = 500 }

Color c = Color.Green;
c.ToString();                             // "Green"
ConsoleColor.TryParse("Green", out var v); // 静态方法
```

### delegate

```zan
delegate int BinOp(int a, int b);         // 泛型委托也可以
delegate T Foo<T>(T x);

BinOp add = (a, b) => a + b;              // lambda
add(2, 3);                                // 5
BinOp mul = delegate (int a, int b) { return a * b; };  // 匿名方法
```

✅ 委托支持捕获闭包（包括可变捕获），通过函数指针调用。⚠️ 事件处理器经 UI
线程派发队列执行，可安全重建控件树（见 GUI 指南）。

### record

```zan
record Point(int x, int y);               // 位置记录：自动公有字段 + 位置构造
record Person(string name, int age);

var p = new Point(1, 2);
p.x; p.y;                                 // 字段访问
p.ToString();                             // 像 C# 的 "Point { x = 1, y = 2 }"
a == b;                                   // 值相等（op_eq）
```

⚠️ record 是 class 的语法糖：没有 `record struct`、没有 `with` 表达式、没有记录继承。

## 成员形式

### 属性与索引器

```zan
public string Title { get { return _t; } set { _t = value; } }   // 带块
public int X { get; set; } = 5;                                  // 自动属性 + 默认值
public int Y { get; init; }                                      // init 初始化器
public double Area => 3.14 * r * r;                              // 表达式体属性
public int this[int index] { get { return data[index]; } set { data[index] = value; } }  // 索引器
public long Total => (long)this[0];                              // 表达式体索引器（只读）
```

### 运算符重载

```zan
public static Vec2 operator +(Vec2 a, Vec2 b) => new Vec2(a.X + b.X, a.Y + b.Y);
public static bool operator ==(Point a, Point b) { ... }   // 也要重载 !=
public static implicit operator double(Frac f) => f.Val;   // 隐式转换
public static explicit operator int(Frac f) => (int)f.Val; // 显式转换
```

✅ 可重载二元：`+ - * / % == != < > <= >=`；✅ `implicit/explicit operator`
（类型名任意一侧）；✅ `op_call`（`f(...)` 可调用对象）。
❌ 无一元重载、无 `true/false` 运算符。

### 事件

```zan
public event Action Changed;     // event 字段
public event UiEvent Click;      // 可以重载 +=/-= 的自定义委托容器

Changed += OnChanged;            // 追加/移除处理器
```

对 GUI 控件，事件字段也支持 `+=`/`-=` 运算符重载（见 GUI 指南的事件机制）。

## 语句

### 控制流

```zan
if (x > 0) { ... } else if (x == 0) { ... } else { ... }

while (cond) { ... }
do { ... } while (cond);
for (var i = 0; i < 10; i++) { ... }
foreach (var item in items) { ... }        // 数组/List/集合
```

if/for/while 的体可以没有花括号（单语句/单声明）。

### switch 语句

```zan
switch (v) {
    case 0: ... break;
    case 1:
    case 2: ... break;                     // 空 case 穿透
    case string s when s.Length > 2: ... break;   // 类型模式 + when 守卫
    case null: ... break;
    default: ... break;
}
```

### switch 表达式

```zan
string desc = item switch {
    Circle c when c.Sides == 0 => "point",
    Shape s => "shape",
    null => "none",
    _ => "other"
};
// 无匹配无兜底时回退到零值
```

### 异常

```zan
try {
    var data = File.ReadAllText("input.txt");
} catch (FileNotFoundException e) {
    Console.WriteLine(e.Message);
} catch (Exception e) {          // 可与无类型的 catch 混用
    throw;                       // 重抛（含 async 跨帧）
} finally {
    Cleanup();
}

throw new ArgumentException("invalid value");
```

⚠️ `Console.Error` 不存在——错误输出用 `Console.WriteLine`。

### yield

```zan
static List<int> Evens(int n) {
    for (int i = 0; i < n; i++) {
        if (i % 2 == 0) { yield return i; }
    }
    yield break;
}
```

⚠️ 急切实现：yield 降级为隐藏 `List<T>` 累加器，不是惰性迭代器。

### 其它语句

```zan
lock (someObject) { ... }                  // 内置互斥
using (var w = File.OpenWrite("a.txt")) { w.Write("x"); }   // using 语句 → try/finally + Dispose
fixed (nint p = buffer) { ... }            // 作用域别名（无 GC 需要固定）
unsafe { ... }                             // 块直接放行
checked(x + 1);                            // ⚠️ no-op：溢出检查恒关闭（环绕语义）

goto done;                                 // 普通标签 goto（无 goto case）
done: ...;

var (x, y) = (1, 2);                       // 解构语句
(int a, int b) = (5, 6);
```

✅ 局部函数（含泛型、递归、前向调用，提升为私有静态方法）：

```zan
static int Sum(int n) {
    int acc(int k) { return k == 0 ? 0 : k + acc(k - 1); }
    return acc(n);
}
```

## 模式匹配

```zan
o is string s                     // 类型模式 + 绑定
o is not null                     // is not
o is null
x switch { 1 => "one", _ => "?" }  // switch 表达式模式
case T v when v.Prop == 1:        // 守卫
```

❌ 无属性模式、`or/and/not` 组合模式、位置模式、关系模式（`> 5`）、列表模式。

## 运算符（按优先级从高到低）

| 优先级 | 运算符 | 结合性 |
|---|---|---|
| 1 | `x.y` `x[i]` `f(x)` `x++` `x--` | 左 |
| 2 | `+x` `-x` `!x` `~x` `++x` `--x` `(T)x` `?.` | 右 |
| 3 | `*` `/` `%` | 左 |
| 4 | `+` `-` | 左 |
| 5 | `<<` `>>` | 左 |
| 6 | `<` `>` `<=` `>=` `is` `as` | 左 |
| 7 | `==` `!=` | 左 |
| 8 | `&` | 左 |
| 9 | `^` | 左 |
| 10 | `\|` | 左 |
| 11 | `&&` | 左 |
| 12 | `\|\|` | 左 |
| 13 | `??` | 右 |
| 14 | `? :` | 右 |
| 15 | `= += -= *= /= %= &= \|= ^= <<= >>=` | 右 |
| 16 | `=>`（lambda） | 右 |

❌ `??=`（空合并赋值）未实现。`?.` 见"可空类型"。

### 字符串插值格式

```zan
$"{v:D4}"  // 十进制 4 位
$"{v:X2}"  // 十六进制 2 位大写
$"{v:F2}"  // 浮点 2 位
$"{v:00000}" // 自定义数字格式，含 _ 圆括号负号规则
```

## 泛型

```zan
class Box<T> {
    public T Value;
    public Box(T v) { Value = v; }
}

T First<T>(List<T> xs) { return xs[0]; }

class SortedList<T> where T : IComparable<T> { ... }   // 接口约束（强制）
T Max<T>(T a, T b) where T : struct { ... }            // 值类型约束（语法接受，无特殊语义）
class Node<T> where T : Base, IShape { ... }           // 基类 + 接口（强制）
```

- 泛型类 / 接口 / 结构体 / 方法 / 委托均可用；显式实例化调用 `f<T>(...)`。
- `where T : class/new()` 语法被接受但不强制（不记录）。
- ⚠️ 实现方式：值类型单态化（每类型一份代码），引用类型类型擦除 + 虚分派。

❌ 泛型类型别名、泛型 attribute 未实现。

## 委托与闭包

```zan
delegate int BinOp(int a, int b);

// lambda：x => / (a, b) => / (int x) => / 带块体
BinOp add = (a, b) => a + b;
BinOp mul = delegate (int a, int b) { return a * b; };

// 闭包捕获（可捕获并修改外层局部变量）
int total = 0;
Action add2 = () => { total += 2; };
add2();                          // total == 2

// 内置泛型委托
Func<int, int> f = x => x * 2;
Action<string> log = s => Console.WriteLine(s);
```

✅ `Func<T..>` / `Action<..>` 已实现；✅ 命名参数 `F(b: 2, x: 10)`；✅ 默认参数值。
❌ `params` 已实现但有差异：降级为 `List<T>` 接收可变参数。

## 初始化器

```zan
var v = new Vec2 { X = 1, Y = 2 };              // 对象初始化器
var l = new List<int> { 1, 2, 3 };              // 集合初始化器
var d = new Dict<string, int> { {"a", 1}, {"b", 2} };   // 字典项 { k, v }
var arr = new int[] { 1, 2, 3 };                // 数组初始化器
var anon = new { 42, "x", Name };               // 匿名对象（new { expr } 简写）
```

## 属性（Attribute）

```zan
[DllImport("user32.dll", EntryPoint = "MessageBoxA")]
static extern int MessageBoxA(nint hwnd, string text, string caption, uint type);

[StructLayout]
struct POINT { public int X; public int Y; }

[StructLayout(LayoutKind.Explicit)]
struct Mixed { [FieldOffset(0)] public int A; [FieldOffset(0)] public byte B; }
```

- 语法：`[A, B(args), C(Name = v)]`，可多组、可命名参数；可作用于类型、
  字段、属性、方法、构造。
- ⚠️ 参数列表本身不能挂 attribute；泛型 attribute 不支持。
- `[StructLayout]` 默认已按 C 顺序布局；Explicit + `[FieldOffset]` 是
  "标签联合/类型双关" 的官方写法（没有 union 关键字）。

## 预处理

```zan
#define DEBUG
#undef DEBUG
#ifdef DEBUG
...
#elif LINUX
...
#else
...
#endif

#if defined(WINDOWS) && !defined(ZAN_DEBUG)
...
#endif
```

- 指令：`#define #undef #ifdef #ifndef #if #elif #else #endif #error #warning`。
- `#if` 表达式：标识符（未定义=0）/ `true` / `false` / 整数 / `!` `&&` `||`
  `==` `!=` / `defined(NAME)` / 括号。
- 预定义符号（随**目标平台**而非构建机）：`WINDOWS`+`WIN32`、`LINUX`、
  `MACOS`+`APPLE`、`ARM64`、`X86_64`、`RISCV64`、`WASM32`、`WASI`、`MUSL`、
  恒有 `ZAN`；命令行 `-Dname[=value]` 追加。

❌ 无 `#region` / `#pragma` / `#line`。

## 异步

```zan
async int DelayThenSum() {                    // async int / async void / async T 均可
    await Task.Delay(100);                    // 延迟
    return 42;
}

async string Fetch(string url) {
    var body = await HttpClient.GetAsync("api.example.com", 443, "/");
    return body;
}

// 并发执行
var t1 = Task.Run(() => { return Work(1); });
var t2 = Task.Run(() => { return Work(2); });
var results = await Task.WhenAll(t1, t2);

// 协程句柄（Task.Spawn 返回 long 句柄）
long h = Task.Spawn(() => DoWork());
Task.IsDone(h); Task.Cancel(h);
```

- `await` 可在任意表达式位置：方法体、`foreach` 体、`catch` 块、无花括号体。
- 入口支持 `static async int Main()`。
- `Task` 内建：`Spawn` `Run` `Delay` `WhenAll` `WhenAny` `IsDone` `Cancel`
  `IsCancellationRequested`；任务对象 `t.Result` / `t.Wait()` / `t.IsCompleted`。
- 调度：并发文件 `--async-workers` 或环境变量 `ZAN_CO_WORKERS` 控制多协程 worker。
- 错误路径：async 中 throw / 重抛 / finally 路径与同步一致（跨帧异常可捕获）。

## 并发与线程

```zan
Thread.Start(() => { ... });                  // 启动线程
Thread.Sleep(100);
await Thread.SleepAsync(100);
int id = Thread.CurrentId();

// Channel（协程间传递）
Channel ch = new Channel(16);                 // 或 CreateUnbuffered()
ch.Send("hi");                                // async
string s = await ch.Receive();                // async
ch.Close(); ch.IsClosed; ch.Count;

// 互斥 / 同步原语（System.Threading）
Mutex m = Mutex.Create();  m.Lock(); ...; m.Unlock();
Semaphore sem = Semaphore.Create(4);
LockManager lm = ...;                          // System.Web 的按 key 锁
AtomicInt at;                                  // 跨进程原子
SharedTable tbl = new SharedTable("name", 1024); // 跨进程共享表/限流，见 System.Threading
```

`lock` 语句使用对象锁；协程式同步另有 `Gate` / `AsyncGate` / `AsyncRwLock`
（见「标准库 → System.Threading」）。

## 内存与 ARC

Zan 用自动引用计数（ARC）管理引用类型的内存：编译器自动插入 retain/release，
引用计数归零时调用析构函数（确定性的）。

```zan
class Buffer {
    byte[] data;
    ~Buffer() { Console.WriteLine("freed"); }
}

void Demo() {
    var b = new Buffer(1024);   // refcount 1
    var c = b;                  // refcount 2
}                               // c 出作用域 → 1，b 出作用域 → 0 → 析构
```

### weak 引用

字段加 `weak` 修饰即可不增加引用计数，用于打破循环引用：

```zan
class Parent { public List<Child> Kids; }
class Child { public weak Parent? Parent; }    // 不阻止 Parent 释放
```

### 内存调试

- `--check-leaks`：退出时报告未释放对象（配合 `-g` 自动开启）。
- `--arc-guard`：隔离已释放对象，截获悬挂的 retain/release。
- `--no-arc-guard` / `--no-check-leaks` 关闭。

### 原生内存

没有 `T*`；用 `nint` + `NativeMemory`：

```zan
nint p = NativeMemory.Alloc(1024);
NativeMemory.Fill(p, 0, 1024);
NativeMemory.Copy(dst, src, count);
NativeMemory.Free(p);
NativeMemory.GetString(p);   // 读 C 字符串（UTF-8）
NativeMemory.PutString(p, s);
```

`Span<T>` / `AsSpan` / `Slice` 可作零拷贝切片（见 conformance span_intrinsic）。

## FFI（DllImport）

```zan
[DllImport("user32.dll", EntryPoint = "MessageBoxA")]
static extern int MessageBoxA(nint hwnd, string text, string caption, uint type);

[DllImport("kernel32.dll", EntryPoint = "GetTickCount64")]
static extern long GetTickCount64();

[StructLayout]
struct POINT { public int X; public int Y; }
```

- 库名可以是逻辑名（`"crt"` 解析为 msvcrt/libc、`"user32"` 等）或平台库名；
  标准库大量使用 `[DllImport("crt", ...)]` 调 libc/msvcrt。
- `string` marshaling：UTF-8 null 结尾临时缓冲。
- 不需要时用 `System.Interop`（LoadLibrary / GetProcAddress / dlopen / dlsym、
  delegate ↔ nint）。
- ❌ 没有 `[CImport]`（不能直接吞 C 头文件）；❌ 没有 `T*`/`->`。

## 工具链

### zanc（编译器）

```bash
zanc <files...> -o out.exe                      # 编译链接（默认 --auto-stdlib）
zanc main.zan --publish -o app.exe              # 发布：-Os + strip + 链接原生驱动
zanc main.zan -o lib.dll --emit-lib             # 共享库（.dll/.so/.dylib）或静态（.a/.lib）
zanc main.zan --target linux-x64 -o app         # 交叉编译（--list-targets 列出）
zanc main.zan --check-leaks                     # 退出时报告未释放对象
zanc main.zan --dump-ast / --emit-ir            # 调试
zanc --list-targets                             # 目标：win-x64 win-arm64 linux-x64
                                                #          linux-musl linux-arm64 macos-x64
                                                #          macos-arm64 wasm32 linux-riscv64
```

常用选项：`-o` `--publish` `--link-mode shared|static`（驱动打包进 exe 或放
旁）`--target` `-O0..-O3/-Os/-Oz` `-g` `-Dname=value` `--stdlib-path`
`--auto-stdlib` `--check-leaks` `--arc-guard` `--emit-lib` `--icon`
`--embed` `--gen-meta` `--emit-symbols` `--time` `@file`（响应文件）
`--package-install/--package-scope`（包管理）。

### 配套工具

| 工具 | 说明 |
|---|---|
| `zanfmt` | 格式化器（用 Zan 自举编写） |
| `zandoc` | 文档生成器（消费 `///` 注释） |
| `zan-lsp` | 语言服务器（补全/诊断/跳转） |
| `zan-dap` | 调试适配器（断点/单步/变量） |

### 标准库与驱动

标准库以 `.zan` 源码随编译器分发（`--auto-stdlib` 自动定位），命名空间路径
映射 `stdlib/` 目录。各模块的原生依赖以 `drivers/<target>/driver.manifest`
声明，`--publish` 时按 `--link-mode` 链接（shared 放 exe 旁 / static 并进 exe）。

包（第三方命名空间）用 `--package-install` / `--package-scope` 安装到
`.zan-packages/` + `zan.lock`。

## 与 C# 的差异（重点）

### 已实现但语义不同

| 特性 | 差异 |
|---|---|
| `char` | 8 字节 Unicode 码点，非 UTF-16 |
| `string` | UTF-8 不可变；索引访问行为不同 |
| `List<T>` | 赋值即共享引用（**非 COW**，修改即所有引用可见） |
| `params` | 降级为 `List<T>` |
| `yield return` | 急切降级为 `List<T>` 累加器（非惰性） |
| `checked/unchecked` | 关键字存在但都是 **no-op**（恒环绕） |
| record | 仅 `record Name(...);` 位置形式；无 with/继承 |
| `const` 字段 | 语法接受，实际是 static readonly |
| `fixed` | 无 GC 可钉，仅为作用域别名 |
| `new()` / `where T : class` 约束 | 语法接受但无强制语义 |
| LINQ 查询语法 | 已实现（`from x in xs where ... select`），但委托/行为同方法链 |

### 未实现（不要使用）

- 元组之外的解构？—— ✅ 元组与解构**已实现**（`(1,2)`、`var (a,b) = ...`、
  `(int,int) t`、`.Item1`，命名元素名称被丢弃）。
- ❌ `??=`；
- ❌ `in` 参数、ref 返回；`ref` 仅 `ref struct` 修饰与 `ref`/`out` 参数；
- ❌ `stackalloc`、通用 `T*`、`->`、`nuint`；
- ❌ `union`/ADT（用 `[StructLayout(Explicit)]` + `[FieldOffset]`）；
- ❌ 模式 `or/and/not` 组合、属性模式、关系模式、列表模式；
- ❌ 原始字符串 `"""`、`$@"..."`、`\u`/`\x` 转义；
- ❌ 元组/记录之外的数据类语法：无 `struct record`、无 `with`；
- ❌ `event` 以外无 C# 事件访问器（add/remove 自定义）；
- ❌ 无 `global using`、`using A = B;`、泛型别名；
- ❌ 无顶层语句、无 `project.zan`（源文件即项目）；
- ❌ 无 `[CImport]`、无 `[Export]` 等 FFI 标记；
- ❌ `default(T)` 表达式；
- ❌ `volatile`、`new`（遮蔽）、`required`、`scoped` 修饰符。

### stdlib 内建（无需 using 即可使用）

`string` `List<T>` `Dictionary<K,V>`（别名 `Dict`）`StringBuilder` `Console`
`Math` `Convert` `String` `File` `Directory` `Path` `Environment`
`NativeMemory` `Task`。这些类型的成员由编译器内建表提供（builtin_api.c），
与 stdlib `System.*` 类并存，见「标准库 → System」。

本参考的 API 细节（完整签名 + 文档注释）见「标准库」各命名空间页面，
例如 [System.IO](/ref/System.IO)（也可直接抓取
[System.IO.md](/ref/System.IO.md) 作为 AI 上下文）。
