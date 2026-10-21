---
name: zan-development
description: The fixed workflow for writing or changing Zan code with the Zan SDK — orient, look the API up, edit, compile, run — plus the language facts and the exact commands. Use it for any Zan coding task.
---

# Writing Zan code

## 0. Orient (one call)

MCP connected: `zan_start_here` → layout, entry point, build/test commands,
which optional tools this installation actually has.
No MCP: read `AGENTS.md` and `docs/AI_ONBOARDING.md` at the SDK root (shipped from this pack).

## 1. Locate before you read

* `search_text(query)` for a symbol or string; `find_files(pattern)` for a name.
  Both answer with `path:line`, which is the address you edit at.
* `read_file(path, from_line, max_lines)` — the numbered window around that
  line, not the file. (`offset`/`limit` reads bytes, for binaries.)

Reading a whole tree "to understand the project" is what `zan_start_here` and
the symbol index exist to replace — and reading the same window twice is worse
still: a tool result stays true until you change it, so use the one you have
instead of asking again.

* **搜不到 ≠ 不存在**：源码里带 NUL 字节的文件（部分生成/拼接的 C 源）会被
  ripgrep 系工具当二进制**整文件跳过**（只提示 "stopped searching binary
  file"）。搜这类文件改用 `Select-String -Path <glob>`（PowerShell）或
  `grep -a`；同时留意「实现分散在多个文件」的形态，符号所在的文件未必是
  名字最像的那个。

## 2. Look up every API you are about to call

```
zan_api_search("File.ReadAllText")   → static string ReadAllText(string path)
zan_api_search("Http")               → the surface of the HTTP client/server
zan_example                        → catalog of shipped, build-verified programs
zan_example("server-mvc")            → files of one of them, verbatim
```

Copy the shape from an example; adapt names, not structure. Guessing a
signature that "should" exist is the top cause of a broken build here.

Without MCP the same index is a file: `knowledge/symbols.json` next to the SDK
(one JSON record per line: name, kind, file, line, sig) — grep it.

## 3. Language facts that decide whether it compiles

* Static types, C#-like syntax, **ARC** — no GC and no manual free.
* Static members are reached through the class: `Foo.Bar`, including inside
  `Foo` itself.
* Nullability is enforced: `x.Get("k").Put(...)` is an error when `Get` may
  return null. Store it, check for null, then use it (or use `?.`).
* `await` only inside an `async` member; the scheduler is real, not cooperative
  sugar.
* Platform-specific code: `#if WINDOWS` / `#else`; native symbols via
  `[DllImport("crt", EntryPoint = "...")]`.
* Strings concatenate with `+`; `Convert.ToString(n)` for numbers.
* Ternary `cond ? a : b` and type tests compose: `x is T ? a : b` parses
  as the conditional (C# rule — `?` starts the true-arm when a top-level
  `:` closes the construct). The nullable marker still wins when no `:`
  follows: `x is int? i` tests against `int?`. **Unwrap patterns are not
  supported**: `maybe is int n` (int? -> int) does not compile — test
  `maybe is int?` instead. `as` on failure yields an empty string for
  value-shaped targets, not null: check `!= ""`. （the parse
  ambiguity was fixed in zanc; the old advice to write `(x is T) ? a : b`
  is obsolete.)
* Never list attribute names with `*/` inside a `/* ... */` comment —
  `data-on-*/data-if` closes the comment mid-sentence and the rest becomes
  code (209 cascading errors). Separate with `、` or spaces.
* No C#-style collection initializers: `string[] xs = { "a", "b" };` fails
  to parse (`unexpected token '{'`). Build a `List<string>` with `Add`
  calls instead. （test-probe compile)
* **Enums must be top-level** — `class Foo { public enum Bar { A } }` then
  `Foo.Bar.A` fails with `cannot resolve member 'Bar'`. Declare the enum
  at namespace scope (one file per enum) and use the bare name.
* **There is no NaN constant, but NaN is producible and detectable** —
  `0.0/0.0` yields NaN and `v != v` detects it (`double` has no
  `IsNaN`/`IsInfinity`; use `v != v` and a threshold for infinities).
  Still, don't use NaN as the "not computable" marker in a data table:
  it renders platform-specifically through `Convert.ToString` and poisons
  any downstream average. Leave the value unwritten and expose a separate
  **validity mask** (a `List<bool>` parallel to the row indices) instead.
* **Column stores truncate on type mismatch** — `DataCol.PutDouble` on an
  `Int` column casts to `long` (16.67 → 16). When a computed result is
  fractional, write it to a **new Float column**, never over an integer
  source column.
* `new T[N]` is zero-initialized — a non-zero sentinel needs an explicit
  constructor pass. Every slot reads `0` right after allocation; if an
  incremental `Clear` only resets slots it has recorded (dirty buckets,
  a used-slots list), the constructor — where nothing is recorded yet —
  must loop-initialize all N slots itself, or the first insert chains onto
  slot 0: self-loops, duplicated query hits, nearest-search hangs.
  （spatial-hash probe hang)
* TLS client trust roots differ per platform （MySQL/PG self-signed
  interop): Windows verifies via Crypt32 SSL chain policy against the system
  store; Linux auto-loads the distro CA bundle (`/etc/ssl/certs/
  ca-certificates.crt` and siblings); macOS loads the system bundle
  (`/etc/ssl/cert.pem` + Homebrew OpenSSL copies). Self-signed / private-CA
  servers should use an explicit anchor, not disabled verification: PG
  connection string `sslrootcert=`, `MySqlConnection.OpenSecureParamsAsync`
  sixth arg `sslCa` (PEM path; unreadable file fails explicitly).
  `HttpClient` has only a verify on/off switch and no CA hook yet — against
  self-signed servers its only option is disabling verification.
* **包级 static 持有 rc 集合 = 进程退出时的泄漏**（实证，2026-10-20）：编译器
  退出期只释放**主编译单元**的静态字段，`packages/*/src/**` 里的
  `static List<T>` / `static Dictionary<K,V>` 不参与回收——集合本身与它持有的
  整条对象图都留到进程结束，`--check-leaks` 逐条报出。stdlib 的
  `Activity.Current` 同理，靠 `Stop()` 显式清空。所以：**常驻可变状态放实例**
  （使用者持有，生命周期跟着走），别放包级 static；确需进程级单例就提供
  显式清空入口。证据与影响面见 `docs/ECOSYSTEM_ROADMAP.md` §7。
* **值结构体没有运算符**（实证，2026-10-20）：`System.Numerics.Vector3` 只有
  `Add`/`Subtract`/`Multiply`/`Dot`/`Cross` 等静态方法，**没有 `+ - *` 运算符**
  ——写数学库必须逐分量展开（`new Vector3(a.X*b, a.Y*b, a.Z*b)`），
  写 `a * 2.0f` 会在 codegen 阶段炸成 "Both operands to a binary operator are
  not of the same type"。同理结构体字段不可用运算符表达式赋值。
* **`new T(...)` 的值语义有坑**（实证，2026-10-20）：结构体构造临时对象在
  表达式层面是**地址**，传给按值形参 / 存进集合槽位 / 赋给结构体字段时，
  编译器需要按值取出——这条链路历史上没归一（`List<Vector3>.AddRange` 元素全 0、
  `Vector3.Add(new Vector3(...), ...)` codegen 失败）。已在 zan 编译器侧收口。
  写新代码时若又遇到「结构体值莫名其妙变全零 / codegen 报 Call parameter type
  does not match」，先怀疑这条链路，别改用户代码绕开。
* **泛型实例化：类型实参在 `new` 处必须是具体类型**（实证，2026-10-21）：
  具体类里写 `new Foo<Bar>()` 没问题；泛型类里写 `new Inner<T>()`（T 是**类级**
  类型参数）也没问题——`Base<Signup>` 实例化时 T 就具体化了。但**泛型方法内**
  `new Foo<T, V>`（V 是**方法级**类型参数）不行：变体不会被编译器记录 →
  不发射 → 调用退化成擦除版 → 取值一律 null/false/0，**不报错不抛异常**，
  规则静默失效。设计时把类型实参留在类级，或改用 `FuncT<T, V>` 形参而不是
  按 V 实例化泛型类。
* **依赖型泛型基类会算错字段偏移**（实证，2026-10-21）：`class Validator<T> :
  Plan<T>` 能编译，但特化出的 `Validator<Signup>` 读基类字段的偏移是错的——
  基类构造里字段还是好的，继承来的方法里再读就成了 null（"null reference
  where an object is required"）。具体实例化的基类（`class X : Base<Signup>`）
  正常；泛型类持有依赖型泛型**字段**（`class Base<T> { Inner<T> inner; }`）
  正常。要"泛型基类的成员"就用组合（字段），别用依赖型基类。
* **泛型类的虚方法体拿不到类型参数**（实证，2026-10-21）：`class Ch<T> : Base`
  里 `Base.Accepts(object)` 是虚方法、`Ch<T>` 覆写它并写 `return m is T;`——
  **经基类引用虚调用恒为 false**（`Base b = ch; b.Accepts(o)`），直接调用
  `ch.Accepts(o)` 才对；虚方法体里转交非虚方法也没用。**方法组转委托同样
  抹掉 `is T`**（`FuncT<object,bool> f = chan.Accepts;` → 恒 false），用
  lambda 包一层才恢复。类型判定要**定型成委托放在派生类构造器里**
  （`this.acceptsField = (object m) => { return m is T; };`），基类只调委托；
  或者把 API 设计成非泛型。
* **泛型方法在另一个编译单元里、类型实参靠推断 → 不单态化**（实证，
  2026-10-21）：库里定义的 `Register<T>(Chan<T>)`，外面按推断调用
  （`bus.Register(ch)`）→ 方法体里的 `is T` 恒假（**静默不生效**：消息没人
  收）；写显式 `bus.Register<Order>(ch)` 就对。同一形状放单一编译单元里推断
  与显式都对。**跨单元调用泛型方法时，类型实参要显式写**。
* **值类型装不进 `object`**（实证，2026-10-21）：`object boxed = (object)v;`
  （v 是 int）→ **当场 access violation**（0xC0000005），没有诊断也没有消息。
  lambda 返回类型与委托返回类型不兼容（`FuncT<Order,object> f = (Order o) =>
  o.id;`）→ **编译期不报错**，运行期给非 null 的垃圾指针，进程退出时 ARC 去
  回收它再崩一次。凡是 `object` 参与的地方只放**引用类型**；值类型结果要用
  泛型端点一路保留。
* **闭包捕获 `this` 会成引用环**（实证，2026-10-21）：把委托（闭包）存进自己
  的字段、闭包又捕获 `this` → 环 → ARC 回收不掉（`--check-leaks` 报出）。
  委托要捕获就捕获**独立的辅助对象**（如订阅表），别捕获宿主。
* **`object.ToString()` 调不通**（实证，2026-10-21）：在 `object` 静态类型的
  接收者上调 `ToString()` 报 "this call could not be resolved to any callable"。
  用 `is` + 强转（`if (v is string) { string t = (string)v; }`）。装箱进
  `List<object>` 的**值类型**在 `is` 下不认（探针里装箱 int 不匹配 `is int`），
  别把值类型存进 `object` 再取回来。
* **计算属性可写**：`public bool IsValid { get { return ...; } }`（只有 getter
  形态）；`List<T>.Count`、`Dictionary<K,V>` 的 `Count`/`Keys`/`Values` 也是属性。
* **`string` 内建成员很全**：`Length`（字节数）、`Substring`、`IndexOf`、
  `LastIndexOf`、`Contains`、`StartsWith`、`EndsWith`、`Replace`、`Trim`、
  `ToUpper`、`ToLower`、`Split`（→ `List<string>`）、`Equals`；`==` 按内容比较。
  `Dictionary<K,V>`（也可写 `Dict<K,V>`）有 `Add`/`Remove`/`Clear`/
  `ContainsKey`/`TryGetValue`。
* **lambda 一半返回 null 时**：先把局部变量按显式类型声明好再返回
  （`ValidationResult outp = null; ... return outp;`）——`return null;` 与
  `return value;` 并列时，返回类型推断可能挑中 null 那支。
* **lambda 可以隐式转成自定义委托，且能捕获闭包**（实证，2026-10-23）：
  自声明 `public delegate void CaptureFn(byte[] buf);` 之后，
  `(byte[] buf) => { world.Capture(buf); }` 可以直接当参数传，**带花括号
  的块体也支持**（不只是单表达式 `x => x`），闭包捕获局部对象同样成立。
  所以"把游戏逻辑以回调方式交给库"不需要绕开委托去写静态单例——
  委托 + 闭包就能把状态带进去（静态字段持有 rc 对象会在退出期泄漏，
  闭包是局部变量、随会话一起释放）。
* **链式调用上的空安全会被拦**：`result.FirstFailure().property` 报
  "'FirstFailure' can return null; accessing 'property' …"。先存进局部变量判空
  再用（`?.` 亦可）。
* **基类构造的调用语法是 `: base(expr)`**（写在构造签名后），不是体内的
  `super(...)`；派生类构造会隐式调用基类的无参构造。
* **字符字面量只认单字节，非 ASCII 走码点数值**（实证，2026-10-21）：
  `'中'` 报 "unterminated character literal"（只取到 0xb8）；要按码点比较
  写数值常量（`0x4E2D`）。字符串字面量里的转义支持 `\xNNNN` 与 `\uNNNN`
  （各 4 位十六进制），**没有 `\U`**——代理对（emoji）写不了转义，只能把
  字符直接写进源文件。
* **用户类可以声明 `params T[]` 尾参**（实证，2026-10-21）：`public void
  AddRow(params string[] cells)` 合法，调用处多出来的实参自动打包成数组。
* **按码点遍历字符串**：用 `Encoding.DecodeCodePoint(s, byteIndex, cpOut)`
  （返回消耗的字节数，`cpOut[0]` 是码点），别自己解 UTF-8。`string.Length`
  是**字节数**，不是字符数也不是显示宽度——显示宽度（中文 2 列、组合记号
  0 列）要另算，见 `Zan.Cli` 的 `TextWidth`。
* **右 trim 用 `StringExtensions.TrimEnd`**（`s.TrimEnd()`）：内建 `Trim()`
  只去两端；渲染表格/日志时想去掉行尾空白得用扩展方法。
* **保留字坑**：`out`、`fixed`、`base` 都不能作变量名/参数名/字段名。编译器
  现在会**直接点名**：报 "'base' is a reserved word and cannot be used as a
  name here"，而且**只报这一条**（改前是 "expected variable name" /
  "expected ')'" 这类通用消息，还跟一串级联报错，只能靠"换个名字就好了"
  反推）。写"分批解析"这类代码时容易撞上，起名用 `baseIndex` 之类。
* **泛型容器的两条现状**（实证，2026-10-21）：
  - 泛型类里的 `List<T>`（T 是类级类型参数）**装结构体元素**现在可用了：
    元素类型会按实例替换，写入读回原样（`Zan.Game.Ecs` 的组件池就靠它）。
  - 但**泛型类不能持有值类型字段**（`class Box<T> { T value; }`）：字段布局
    不随实例特化，写入会超出槽位 → 堆损坏、读回是垃圾。要在泛型类里放值
    类型，就放进 `List<T>`（或让泛型类只持有引用类型字段）。详见
    `TASKS.md` A421。
* **包级泛型委托可以自己声明**：`public delegate void EcsEach3<T1,T2,T3>(T1 a,
  T2 b, T3 c);` 合法，可作方法参数（依赖型实例化 `EcsEach3<int, T, U>` 也能用），
  写 ECS/事件这类"带类型参数的回调"不必绕开泛型。
* **运算符重载可用，标量只能在右**（实证，2026-10-22）：值类型写
  `public static V operator +(V a, V b)` 合法（同名同参的一组 `+ - * /`）。
  混合标量时**只能标量在右**：`v * 2.0` 合法，`2.0 * v` 编译器报
  "cannot apply operator to 'double' and 'V'"——语言限制，别绕。
* **`List<T>` 取结构体元素是副本**（实证，2026-10-22）：`list[i].field = x`
  改的是**副本**，不写回原元素。要就地累积（接触冲量、缓冲池槽位这类）就用
  **类**元素（`List<ContactPoint>`），别用 struct。同理 `List<T>` 没有
  `Sort` / `Remove(T)`，只有 `Count/Add/Reserve/AddRange/Insert/RemoveAt/
  Clear/Contains/IndexOf/LastIndexOf/Reverse/ToArray`。
* **结构体跨文件的字段布局曾被降级成指针**（实证，2026-10-22）：类的字段类型
  声明在**另一个文件**时，旧编译器把字段类型降级成 `i8*` → 布局整体错位 →
  **静默内存损坏**（值是错的，不是崩）。已修（编译器侧的结构体类型映射
  `map_type`）。判定器是一条**跨文件** conformance 用例——新写"跨文件共享
  结构体"的代码时先跑它，别自己判字段偏移。
* **`List<T>.Count` 是属性，不是方法**（实证，2026-10-22）：写 `list.Count()`
  编译器直接报 "'List<T>.Count' is a property, not a method: drop the '()'"。
  `Dictionary` / `Queue` 的 `Count` 同理。自己封装的类（`CellPath.Count()`）
  才是方法——**别把两者的写法混用**。
* **空安全是编译期硬检查**（实证，2026-10-22）：`JsonValue.Get(key)`、
  `List.First()` 等标为"可返回 null"的调用，其结果**直接取成员**会被编译器
  拦下（"'Get' can return null; accessing 'IsArray' on its result faults at
  runtime"）。定式：先存进局部变量判空再用，或用 `?.`。
  ——**`Has(key)` 判断过也不算数**，编译器不据此消解 null。
* **异常有两级继承**（实证，2026-10-22）：`class SubEx : BaseEx`，`BaseEx :
  Exception` 合法，`catch (BaseEx)` 能接住子类抛的实例。
* **接口可以带 `out` 参数**（实证，2026-10-22）：`interface I { bool TryGet(string
  k, out byte[] v); }` 合法，实现类与经接口类型的调用点都正常。
  接口类型可作字段、参数；实现类可显式赋给接口类型变量。
* **字节数组没有区间切片**（实证，2026-10-22）：`raw[1..3]` 语法不存在
  （报 "expected ']', got '..'"）。要切片就新建数组循环拷贝。
  `byte[].ToStr(0, len)` 给出字节的字符串视图（可含 NUL），反向用
  `Encoding.UTF8.GetBytes(text)`。
* **stdlib 的异常类型比想象中少**（实证，2026-10-22）：只有 `ArgumentException`、
  `InvalidOperationException`、`IOException`（`FileNotFoundException` 继承它）、
  `JsonException`、`NotSupportedException`、`TimeoutException` 等。
  **没有** `DirectoryNotFoundException` / `UnauthorizedAccessException`——
  写 catch 子句前先确认类型存在，否则编译报 "undefined type"。
* **`out` 是保留字**：局部变量不能叫 `out`（报 "'out' is a reserved word
  and cannot be used as a name here"）。`Math`、`String` 这类**大写标识符
  按类型/命名空间解析**——`Math.PI` 在表达式里能用，但 `Math.PI.ToString()`
  会报 "'PI' is not a known variable, type, or namespace"。
* **`double` 没有 `IsNaN` / `IsInfinity`**（实证，2026-10-22）：用
  `v != v` 判 NaN、用阈值比较判无穷（`v > 3.4028234663852886e+38`）。
* **Zan 没有 `char` 类型**（实证，2026-10-23）：字符串是 UTF-8 字节串，
  "单字符"就是长度为 1 的字符串；`s[i]` 返回**字节**（int）不是字符。
  `string.Split` 的分隔符是 **string**（`s.Split(".")`；写 `s.Split('.')`
  编译不过），**返回 `List<string>`**（不是 `string[]`），按字符拆用
  `s.ToCharList()`，拼串用 `+`。
* **`StringBuilder` 是编译器内建的**（实证，2026-10-26 复核修正）：
  `src/compiler/builtin_api.c` 的 `BT("StringBuilder", ...)`——它**不是
  stdlib 源码**，但 `using System.Text;` 后可以直接用
  （`Append`/`AppendLine`/`ToString`/`Clear`）。stdlib 源码里搜不到
  `class StringBuilder` 不代表它不存在——**内建类型要用
  `src/compiler/builtin_api.c` 核实，别只看 stdlib 目录**。
  （`List<T>` 与 `Dictionary<K,V>` 同理，都是内建。）
* **`JsonValue.Parse` 对坏 JSON 抛异常，不是返回 null**（实证，2026-10-26）：
  畸形 JSON 抛 `JsonException`（"Malformed JSON at position N"）。
  解析**外部输入**（网络响应、用户文件）必须自己兜 try/catch——
  以为它返回 null 而只写 `if (root == null)` 判断的代码，遇到畸形响应
  会把异常抛给调用方。`JsonValue.ParseLenient` 是不抛的版本（返回
  已解析的部分）。
* **包的命名空间按目录推导，`using` 要写到最深的那层**（实证，2026-10-26）：
  `packages/Zan.Net/src/System/Net/Http/Client/HttpClient.zan` 声明的是
  `namespace System.Net.Http.Client;`——`HttpClient` **不在**
  `System.Net.Http` 里，`using System.Net.Http;` 拿不到它，必须
  `using System.Net.Http.Client;`（`System.Net.Http` 里放的是
  `HttpResponse`/`HttpRequest`）。判定方法：grep 目标类型所在文件的
  `^namespace` 行，别按目录名猜。
* **泛型方法从数组实参推不出类型参数**（实证，2026-10-23）：
  `Pick(arr)`（`arr` 是 `string[]`）报 "could not be resolved to any callable"，
  必须显式 `Pick<string>(arr)`；`List<T>` 实参则能推断（`rng.Pick(list)` 正常）。
* **泛型 `T` 上调不到 `.Equals`**（实证，2026-10-23）：`List<T>` 元素上写
  `items[i].Equals(value)` 报 "could not be resolved to any callable"。要按值
  去重就用 `HashSet<T>`（相等规则：值类型按值、字符串按内容、其他引用类型
  按身份）。
* **闭包捕获 `this` 逃逸方法作用域会触发 ARC 警告**（实证，2026-10-23）：
  `return this.Of<string>(n, i => this.random.String())` 报 "returned closure
  captures 'this' and escapes method scope"。定式：先把要用的字段拷进局部
  变量（`Random r = this.random;`），lambda 里只用局部变量，警告消失。
* **`int` 的十六进制字面量超过 0x7FFFFFFF 要 `unchecked`**：
  `unchecked((int)0x80000000)`；否则溢出编译报错。
* **Zan 没有装箱：值类型装不进 `object`**（实证，2026-10-27）：`(object)42`
  编译期报错 `cannot convert 'int' to 'object': value types cannot be boxed`。
  `object` 只能装引用（类实例、字符串、数组、委托、接口）。要"擦除"一个
  值类型的结果，就把类型一路保留在泛型里（`QueryEndpoint<T, TResult>`、
  `FuncT<T, TResult>`），别指望 `object` 能兜住。
* **lambda 的返回类型必须和委托的返回类型兼容**（实证，2026-10-27）：
  `FuncT<Order, object> f = (Order o) => o.id;` 编译期报错
  `cannot convert 'int' to 'object' in lambda return`——委托返回 `object` 时，
  lambda 必须返回**引用类型**；表达式体和块体（`{ return o.id; }`）都会被拦。
  反过来 `FuncT<Order, bool>` 配 `=> o.id` 正常。
* **泛型类覆写虚方法时，类型参数在特化版里可用**（实证，2026-10-27）：
  `class Ch<T> : Base` 覆写 `Base.Accepts(object)` 并写 `return m is T;`，
  经基类引用的虚调用现在判定正确（每个实例化一张 vtable）。早年的绕行
  "把类型判定挪进派生类构造器、定型成委托再交给基类调用" 现在**不必**了，
  但保留也无害。
* **`default(T)`、顶层函数、`List<T>.At()` 都不存在**（实证，2026-10-28）：
  `default(int)` 报 "unexpected token 'default' in expression"；文件级
  函数报 "expected type declaration (class, struct, interface, enum, or
  delegate)"；`list.At(0)` 报 "'List<T>' has no member 'At'"（只有下标
  `list[0]`）。要"默认零值"就显式写 `int x = 0;`。
* **泛型里 `new T()` 静默返回 null**（实证，2026-10-28）：`class Box<T> { public
  T Make() { return new T(); } }` —— **编译期零诊断、运行期返回 null**（实测
  `Box<string>.Make()` 得到 null）。编译器不检查 T 有没有无参构造器，也不
  报"无法实例化类型参数"。凡是泛型工厂/容器要造 T，一律要求调用方传
  **工厂委托**（`FuncT<TTarget>`），调用点再判空兜底。
  （已登记 TASKS.md A462，尚未修。）
* **结构体是值语义，成员赋值改的是副本**（实证，2026-10-28）：`Pt q = h.P;
  q.X = 99;` 之后 `h.P.X` 仍是原值——`q` 是副本。凡是"变换/映射/原地更新"
  类 API，签名必须**交还新值**（`FuncT2<TSource, TTarget, TTarget>`），就地
  改成员改的是副本、调用方看不到。（`Zan.Mapping` 的成员规则按这条设计。）
* **委托家族只有四件套，没有 `FuncT3`**（实证，2026-10-29）：stdlib 提供
  `Action`（0 参）、`ActionT<T>`（1 参）、`ActionT2<T1,T2>`（2 参）、
  `FuncT<T,TResult>`（**1 参** + 返回值）、`FuncT2<T1,T2,TResult>`（**2 参** +
  返回值）。**三参数要用 `FuncT2`（它就是三个类型参数）或自己声明委托**——
  写 `FuncT<A,B,C>` 报 "requires 2 type arguments"。看签名时数清楚
  "参数个数 + 1"。
* **`Thread.Start` 是静态方法**（实证，2026-10-29）：没有 `new Thread(body)`，
  也没有实例 `Start()`——写法是 `Thread.Start(() => { ... })`（`ThreadStart`
  委托在 `System.Threading`）。它是**分离后台线程**，没有 `Join()`，要等结果
  就自己用原子变量 / 队列交接。
* **`File.TryLock(path)` 是跨进程文件锁**（实证，2026-10-29）：返回非 0 是拿到
  句柄，返回 **0 是拿不到**（别的进程或别的句柄正持有）。同进程内第二次调用
  同样返回 0——所以它能当"单写者"闸门用，但**不要在同进程里嵌套调用**。
  配 `File.Unlock(handle)` 释放。
* **`Dictionary` 是内建类型，不在 stdlib 目录里**（实证，2026-10-29）：
  `src/compiler/builtin_api.c` 的 `members_dict`（同 `List<T>` / `StringBuilder`）。
  `Count` / `Keys` / `Values` 都是**属性**不是方法，写 `map.Count()` 报
  "'Dictionary<K,V>.Count' is a property, not a method"。
* **泛型方法里可以创建捕获泛型形参的闭包**（实证，2026-10-29）：
  `void Register<T>(...)` 里把 `fromJson(argsJson)` 写进 lambda 再赋给
  `FuncT<string, R>` 能正常工作（`T` 在闭包里可用）。这条让"泛型强类型 +
  显式序列化委托"这种没有反射也能做的设计成立。
* **`File.AppendAllText` 不加 BOM**（实证，2026-10-29）：追加的内容原样写入，
  换行按传入的字符串来（自己带 `\n`），不会替你补 BOM 或转成 CRLF。
  读回来用 `File.ReadAllText` / `ReadAllLines`。
* **`var` 是保留字，不能作变量名**：局部变量写 `var x = 1;` 报 "'var' is a
  reserved word and cannot be used as a name here"——和 `out` / `base` 同一类
  坑。要类型就显式写类型。
* **同名泛型与非泛型类型不能共存**（实证，2026-10-21）：同一作用域里写
  `class Foo` 和 `class Foo<T>` 报 "duplicate type declaration 'Foo'"——绑定器
  按**名字**占位（`src/compiler/binder.c` 的类型声明分支用 `scope_find` 查名字，
  **不看类型参数个数**），同名即冲突，与泛型实参无关。要"同一概念的两种形态"
  就让泛型版另起名（如 `BenchCase` / `BenchCaseT`）。
* **嵌套类型不能做数组元素类型**（实证，2026-10-21）：`Outer.Inner[]` 报
  `undefined type 'Outer.Inner'`。但 `Outer.Inner` 作**普通类型引用**、作
  **泛型实参**都正常，**命名空间限定**的数组元素（`System.Text.Encoding[]`）
  也正常——只有"嵌套类型 + 数组"这一格是坏的。数组路径会先解析"包装节点"自己
  的名字，解析不到就早退，元素真正的解析（`array_element`）排在它后面。
  要让嵌套类型进数组/列表元素位置，只能把它提到**命名空间层**。
* **`Exception.message` 不可写**（实证，2026-10-21）：派生异常不能在构造后补
  细节——`e.message = "..."` 编译不过。细节要在 `base(...)` 里拼好：把拼接提成
  静态方法（`static string Combine(string text, string detail)`）再
  `base(Combine(text, detail))`（base 参数里只能调表达式，提成静态方法才
  拼得出来）。
* **`Convert.ToDouble` / `Convert.ToInt64` 对坏输入静默返回 0**（实证，
  2026-10-21）：`Convert.ToDouble("abc")` 给 0 且**不抛异常**——拿它当校验用，
  坏输入会被当成"值就是 0"。诊断数据（账单 / 日志 / 外部 CSV）里一个写坏的
  值因此变成"0 毫秒"而不是"这一行坏了"，把排序和统计整个带偏；诊断数据里
  **错的值比没有值更坏**（人会照着假数据去优化）。要校验就自己严格解析：只接受
  `[+-]?digits[.digits][(e|E)[+-]?digits]`，失败判失败（由调用方决定"跳过并
  计数"还是"报错"）。坑出处：DataValues 的严格数值解析记过一次，
  `Zan.Game.Profiler` 的账单解析又踩一次。
* **`JsonValue.SetAt(i, v)` 只替换已有元素，不追加**（实证，2026-10-21）：它内部
  是 `if (i < items.Count) items[i] = v;`——对 `NewArray()` 建出来的空数组调
  `SetAt(0, ...)` **静默什么也不做**，序列化出来是 `[]`。追加元素要用
  `Append(v)`（另有 `AppendStr` / `AppendInt` / `AppendDouble` / `AppendBool`）。
  症状是"JSON 报告里数组永远是空的"，而 `Set` 写对象字段是好的——同一个类里
  一半正常一半为空，很容易怀疑是自己数据结构写错了。
## 4. Edit

`edit_file(path, old, new)` (MCP) replaces an exact snippet in place — the
default, because it needs the snippet and not the file. `old` must occur once,
so extend it with neighbouring lines until it is unique; the call refuses a
fuzzy match rather than guessing. `write_file(path, content)` is for a new file
or a wholesale replacement.

Make the whole change in one call per site — edit, re-read, edit the next line
is how a five-minute fix becomes twenty. Match the surrounding style; prefer
extending an existing class over adding a parallel one.

## 5. Compile — every time, before any claim

```
zan_build_project            # whole project, structured diagnostics
zan_compile(content)           # one snippet in an isolated dir
```

Direct:

```
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib -o build/app
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib --publish -o app   # release
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib --check-leaks -o build/app
```

**多文件项目必须枚举全部 .zan 源**（实证）：`--auto-stdlib` 只自动
发现 stdlib/包命名空间，**不拉取同项目的兄弟 .zan**——单入口
`zanc src/main.zan ...` 报 `unresolved call 'X': X is not a known variable,
type, or namespace`（X 是项目内另一文件的类）时，先数源文件齐不齐：
`zanc $(find src -name "*.zan") --auto-stdlib ...`（模板 e2e 套件就是这么
枚举的）。别急着判编译器回归。

Read `diagnostics[]` (file, line, column, message) and fix from the first one
down: later errors are usually fallout.

## 6. Run / test

`run_command("build/app")`, or the project's own test entry point if it has one
(`zan_start_here` reports it when present). In the SDK repo itself the tiers are
`scripts/test.ps1 smoke | standard | full` — take the smallest tier that covers
the change.

**ctest green ≠ 当前工作区已验证**（实证）：conformance 用例的
`run_case.cmake` 有 up-to-date check——STDLIB_STAMP 不变就复用旧 exe 直接
回放，工作区**未提交**的 stdlib 改动不碰 stamp，于是出现"ctest passed 但
同一源文件手编失败"的假绿。多会话并行的仓库里，凡结论依赖"刚改过的东西
过没过"，必须手编直跑（`build/zanc.exe <case>.zan --auto-stdlib -o
_scratch/x.exe && _scratch/x.exe`）当真值，ctest 只当台账。

**同一个 OUT_EXE 连跑多个用例 = 比对的是上一个用例的程序**（实证）：手工调
`tests/run_case.cmake` 批量回归时，若多个用例共用 `-DOUT_EXE=<同一个路径>`，
up-to-date check 只看「产物比 SRC 新」——SRC 更旧就**直接复用上一个用例编出来的
exe**，于是 A 用例的输出被拿去和 B 用例的 golden 比，报出与 A 毫无关系的
"output mismatch"。每个用例必须用**各自的 exe 路径**（ctest 里是
`conf_<name>.exe`，天然唯一），并且批量跑之前先把旧 exe 删掉。

**stdout 抓不到 ≠ 程序没输出**（实证，Windows）：Zan 程序在**有控制台**时
直接写控制台，PowerShell 的 `> file` 与 `$out = & exe` 都抓不到——重定向
文件 0 字节，终端却打出了字（未改动的 conformance 用例同样如此）。要比对
输出必须走 `tests/run_case.cmake`（cmake `execute_process` 用管道，子进程
无控制台，捕获正常）或直接 `ctest -R <case>`；拿 PowerShell 重定向判"用例
没输出"会把通过判成失败。**生成 golden 也只能走这条路**：PowerShell 写文件
拿到的是 0 字节，得用一段 `cmake -P` 脚本 `execute_process(... OUTPUT_VARIABLE
...)` 捕获后再 `file(WRITE ...)` 落盘（harness 自己就是这么比对的）。

**`System.Threading.Timer` 构造后不自启**（实证）：`new Timer(50, cb)`
只是注册，必须再调 `t.Start`，否则共享泵根本不投 tick——全程无任何报错，
依赖它的轮询/消息泵整个静默失效（gui-webapp 模板的桥消息泵就因此死过，
前端 RPC 与窗口控制全无响应）。构造后紧跟 Start，并实跑验证回调确实在触发。

## 6b. Writing windows in HTML (Gui P5)

Windows can be described in plain HTML + CSS instead of hand-built control
trees — use it whenever the user says "用网页/HTML 写界面" or the layout is
web-shaped. Two paths, same parser, pixel-identical geometry:

- **Runtime**: `Control root = app.LoadHtmlWith(html, handlers, baseDir);`
  (fragments are fine — a missing body gets an implicit one).
- **Compile time**: pass the `.html` file to zanc next to your `.zan`
  sources; it expands to `UiHtml.Build(handlers)` + `UiHtml.Css` (call
  `app.UseAppCss(UiHtml.Css)` first). The shipped exe carries no HTML text
  and no parser.

Protocols: containers map to `Element` (UA stylesheet supplies web defaults
— never write `display: block` by hand), `button`/`textarea`/`input`/`img`
map to real widgets, `select` is a placeholder box. Events:
`data-on-click="save"` wires the handler registered as
`handlers.Add("save", ...)`; unknown names silently no-op by design.
Handler args: `data-arg="apple"` on the same node passes the literal
`"apple"` to an `handlers.AddArg("save", (string a) => ...)` entry
(creation-time value snapshot — row identity goes through `data-bind`,
not the arg; template rows share the prototype's arg).
`style` attributes become `.zgen-N` class rules (class-level specificity,
not browser inline specificity). Idempotent pitfalls: Button routes "Click"
to its dedicated `Click` field — assert `((Button)b).Click.Count`, not
`b.On.Click`; engine ledgers (inline x stepping ±3px, line-height rounding,
block strut font-size) apply — write explicit `line-height`/`font-size`
in fixtures. Lists and conditional visibility are declarative too
(P8): `<template data-for="items">` clones its element children per array
item (rows inserted right after the template; row binds scope to the item
first, so `<span data-bind="name">` shows `item.name` — Element's default
bind property is `text`), and `data-if="flag"` toggles `SetShown` by the
path's truthiness (null/false/0/"" are false). Both are **consumed only by
a model-hosting ChildWindow** (`SetRoot(tree, model)` + `Wire`); without
a model they are inert (prototype stays hidden, conditions stay visible).
Nested templates are not supported v1.
Full spec: `docs/HTML_UI.md` in the SDK.

## 6c. Designer documents are .html (Gui P7a)

The visual window designer's storage format is HTML — a doc whose
`<body>` carries the bare `data-zan-design` marker. Every shipped
template and all IDE-internal forms ARE .html (the legacy .html compile
channel was removed in P8-4: zanc rejects .html input with a targeted
error; convert with `DesignerHtml.FromJsonDoc`, the canonical
.html→.html converter); the JSON doc model is an internal
representation (undo/redo snapshots, the JSON drawer, the LSP
feed) — don't hand-write it, and write new designs as .html.

- **Compile channel**: zanc sends a `body[data-zan-design]` .html to the
  same GenForm projection the old .html used (typed partial class); a plain .html
  goes to the GenHtml build-tree class instead. Keep the marker intact —
  it is the routing bit.
- **Round-trip is key-faithful** (tested): doc-level keys become body
  `data-<kebab>` attrs, `on<Event>` becomes `data-on-<kebab>` (Pascal
  restored on read), pass-through objects (`props`, `columns`) ride
  `data-x-<kebab>` as compact JSON. Bare attributes mean `true` on read.
- **Designer API**: `Designer.SaveHtml` / `LoadHtmlText(text)`. Loading
  a non-design HTML imports it (tags fall back to kinds) — HTML has no
  "corrupt" form, so `loadError` is nearly unreachable for .html input.
- **Zan trap hit here**: `((char)c).ToString` returns the code point
  ("M" -> "77") — capitalize via `char + string` concatenation instead
  (`(char)(c - 32) + rest`).

## 6d. Per-control inline CSS (Gui P7b)

Any control takes inline CSS declarations via `ctl.SetProp("style",
"background:#c00; padding:8px; border-radius:6px")` — one call applies them
immediately (visual keys win over class rules; colors/functions reuse the
full CSS parser). `GetProp`-driven docs and the HTML `style` attribute land
on the same channel. Designer fields carry the same thing as the `style`
key / element `style` attr, editable line-by-line in the Inspector's STYLE
section.

- **Design geometry owns layout**: for designer-placed fields the emitted
  `logW`/`Prefer`/`logPad` (from X/Y/W/H, span, dock rows) is re-applied
  every measure, so `width`/`height`/`x`/`y`/`dock`/`gap`/`pad` inside
  `style` only stick where the design is silent. Visual keys (background/
  color/border/border-radius/box-shadow/font-size/transition) always win
  over class rules. Don't fight this by re-applying style after geometry.

## 7. Evidence discipline (the anti-rework rules)

Two real projects (a 420-file gateway port, a 634-file WinForms port) lost days
to the loops below. These rules are what closed them.

**Success must leave an artifact on disk.** A claim of "the repro passed" or
"the server responded" is false until a log file with content, a screenshot, or
a marker file proves it. One project burned hours on 8 repro variants whose
logs were all empty — the "success" was assumed, then "fixes" were built on it.
Before you treat a run as evidence, check the log is non-empty; before you fix
based on a failure, make sure the failure is the one you think.

**Compile after every coherent unit — not in batches.** A 600-file port that
compiles only once per batch spends a whole unverified epoch per batch; when a
build breaks there, every edit since the last green build is suspect at once.
Compile per module/window and fix forward immediately. If a build is blocked by
a file you must not touch (concurrent session, frozen stdlib), surface the
blocker and stop changing that batch — do not "keep editing, compile later."

**One command template, kept correct.** In one session `zanc` failed 4 times in
a row with `cannot open file 'src/App.html'` because the working directory had
drifted to `build/`. Commands that compile your project are project assets:
keep the exact working form in the project's AGENTS.md/README and always paste
it, including the `cd`, instead of retyping it from memory. Same for tool
quirks: a `File has not been read yet` error is a full wasted round trip — read
the window first, then edit.

**Search the file, not the memory of the name.** 5 consecutive greps for a
class that turned out to be named differently is 5 lost rounds. `find_files`
for the file name first, read it, and take the API shapes you need from a
sibling file that already does the same thing.

**Claiming a compiler defect has a price tag.** If code compiles silently but
is wrong (e.g. an `int` returned from a stdlib call assigned to an object
variable, then dereferenced — crashes with a poison-value pointer), the defect
itself is in scope: reduce it to a minimal snippet with `zan_compile` and
report it. Do not spend session hours tiptoeing around an unreported defect;
each later session re-pays the same discovery cost.

## 8. Report

State the command you ran and what it printed. Separate "compiled", "ran" and
"not verified". Never present an unverified change as working.

## Traps

* Do not invent APIs — look them up (step 2). A stdlib method's real return
  type is part of the API: check it before assigning to anything but `var`
  (assigning an `int` to an object variable once crashed a port on its first
  request — silent at compile time, poison-value pointer at runtime).
* `HttpClient.GetAsync/PostAsync` 只返回响应体，**拿不到 HTTP 状态码**——
  把 403/429 映射成领域异常的 SDK 必须走 `SendAsync`（返回解析好的
  `HttpResponse`，statusCode/body 一次拿全）。不要拿 body 再
  `HttpResponse.Parse` 一次：GetAsync 返回的已经是剥掉头部的正文，
  二次解析 statusCode 恒 200、body 变空。
* `JsonValue.Get`/`PathGet` 可空性是编译期强制的：`x.Get(k) != null &&
  x.Get(k).AsInt` 这种"调两次"写法直接编译错误，必须先存局部变量再判
  （每处一次 `Get` + null check）。
* `JsonValue.Put` 是**追加不去重**，`Get` 命中返回**第一个**同名键——
  更新已存在的键必须用 `Set`（替换或追加）。"整表重写后 Put 回对象"
  （如 extra["columns"] 数组重排）会留下重复键，之后所有 Get 都读到
  旧值，表现为"明明写进去了、读回来还是旧的"。Put 只用于确认首次
  创建的键。
  **坑在类型化助手**：`PutStr` / `PutInt` / `PutBool` / `PutDouble` 内部
  走的都是 `Put`，所以它们**同样不能用来更新已有键**——写第二遍就多一个
  同名成员，而读取首键胜，于是"设进去的值读回来还是第一次那个"。
  要覆盖就写 `obj.Set(key, JsonValue.NewStr(v))`（自己拼 `JsonValue.NewXxx`）。
  实测（游戏存档基座）：`SetInt("gen", 1)` 后 `SetInt("gen", 2)`，
  存盘读回得到 1——换成 `Set` 才对。
* Steam 等 64 位 ID 的 JSON 约定是**字符串形态**（"76561197960287930"），
  且个别字段文档写数字、线上回字符串（如 AuthenticateUserTicket 的
  `result:"OK"`）——解析层两种形态都要接住。
* 需要本地 HTTP 假网关自测的 SDK（微信/京东/Steam 同款套路）：
  `HttpServer` 回放官方 JSON 形态 + `ExternalCallPolicy.Default
  .AllowLocalHttp` 放行 loopback。曾有的坑（stdlib 已修，旧工具链仍会
  踩）：TLS 客户端对明文服务器握手会永久挂死——`TlsStream.PumpInAsync`
  对 `Recv<0` 不退出循环重挂 `ReadReady`，而 shutdown 后 readiness 只有一
  次；给 SDK 留 `PlainHttpMode` 之类的明文开关是防御性设计。
* `HttpClient` 请求行的 `path` 会原样进报文：调用方可控的 path 里带
  CR/LF 就能把一行撕成多行走私第二个请求（与 `SetHeader` 的头注入同一
  族）。stdlib 已修（`BuildRequestHead` 拒 CR/LF/SP/NUL/DEL，下载通道
  同步把关）；自建 HTTP 客户端或旧工具链要自己校验。
* 并行会话共享工作树时，"测试+stdlib 成对"的修复批**必须核对 stdlib 侧
  文件真的进了提交**：实测某提交只带上了三个 conformance 测试而配套的
  stdlib 半（HttpFramer/CookieJar/HttpClient 防线）全部留在工作树，TASKS
  却记"已修"——`git log -S "<新增符号>"` 全历史查一遍 + `git show
  <commit>:<stdlib文件> | grep <符号>` 是 30 秒的事，漏了就是标准库
  裸奔一个版本周期。
* Do not hard-code hosts, ports, credentials or business limits: they belong in
  the project config (`config/app.json` for server projects), read at run time.
* Do not hand-draw GUI widgets: use the standard library's components
  (`zan_example("gui-window")`, `zan_example("gui-form-components")`).
* Do not leave probes in the project: `_scratch/` for throwaway work, `build/`
  for output.
* If the compiler itself looks wrong, reduce it to a minimal snippet with
  `zan_compile` and report the snippet — do not contort the code around it.
* 给 `Binding<T>` 属性赋值（`Label.Text = ...` 同族）时，右值**别用裸字段左值**（`someObj.field`）：会合成活绑定逐帧读源对象，源是本次调用里新建的临时就活不过返回，绑定的 target 悬空，下一帧 MeasureText 读坏串崩（gui-wechat 名片页点击路径 30-50% 概率崩实测）。**先快照进局部变量再赋**——局部 → 常量绑定；右值本就被持有（`data[i]`、`cur = c`）则安全。
* `out` 实参的目标是**实例字段**时（`Fill(out v)`），编译干净但字段没被写穿，后续读它 = 空指针崩。定式：先给字段赋值，再把字段当普通实参传。
* `TryGetValue(key, out v)` **未命中时不写 out 参数**——调用后 `v` 还是调用前的旧值（未初始化则是 null），拿它当"没找到"的信号必错。定式：调用前先赋哨兵值（`string v = "";`），命中与否用返回布尔判断，别用 out 值判空。
* 闭包（delegate/lambda）捕获方法的 **`out` 参数不回传**——捕获的是副本，delegate 里的累加/赋值调用方看不见，编译不报错（ZanDb `MinField` 首版在 `ScanRows` 回调里直接写 out 累计值，调用方永远拿到初值 0）。定式：delegate 里只操作局部变量，扫描结束后一次写回 out 参数。
* `Dictionary.Keys` 返回的是**内部 List 本体**，不是副本——对它原地排序/增删会把键与值的配对打乱（排序后 `keys[i]` 对应的值还是旧槽位的）。需要排序先拷贝到新 List 再排。
* `.html` 的设计名（或文件基名）**别叫 `App`**：与 stdlib `Gui.App` 同名时 nsresolve 会把 Chart 全家的 `App` 形参引用改坏（100 个 `undefined type 'App'`，100% 复现，TASKS A311）。模板 raw 编译要把 `{{NAME}}` 占位符换成真实项目名（IDE 建项目时自动替换+重命名）。要和并行会话彻底隔离：`git archive HEAD stdlib` 解到快照目录 + 复制 `zanc.exe` 和 `build/zanrt_*.obj` 进去（stdlib 按 exe 相对定位），再把 `LOCALAPPDATA` 指到私有目录隔离生成器缓存。
