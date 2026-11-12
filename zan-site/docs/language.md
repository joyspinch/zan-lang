# 语法与程序结构

当前实现说明，按仓库源码与测试核实于 **2026-11-11**。已安装 SDK 可能与此仓库状态不同。本文介绍写程序时常用的语法；类型规则见[类型系统](types.md)，释放与并发分别见[内存与资源](memory.md)、[异步与并发](async.md)。

## 源文件与入口

Zan 源文件使用 UTF-8 编码，扩展名为 `.zan`。标识符区分大小写。普通语句以分号结束，花括号建立语句块和作用域。

入口是类型中的静态 `Main` 方法。常见签名为 `static void Main()`、`static int Main()`，以及接收 `string[] args` 的形式。`int` 返回值作为程序退出状态；参数数组不包含可执行文件名。

下面是完整程序“数组求和”，提取自 `tests/conformance/array_init_foreach.zan` 已覆盖的数组初始化、遍历与输出写法。预期输出为 `16`。

```zan
using System;

class Program {
    static void Main() {
        int[] nums = new int[] { 3, 5, 8 };
        int total = 0;
        foreach (int n in nums) {
            total = total + n;
        }
        Console.WriteLine(total);
    }
}
```

编译和运行步骤见[快速开始](quickstart.md)。多文件工程需要纳入全部相关源码，不能把命名空间导入等同于自动收集同目录源文件。

## 注释与字面量

单行注释使用 `//`，块注释使用 `/* ... */`，文档注释使用 `///`。块注释内部不要嵌入会提前结束它的 `*/`。

| 写法 | 含义 |
| --- | --- |
| `42`、`0xFF`、`0b1010`、`0o77` | 十进制、十六进制、二进制和八进制整数 |
| `1_000_000` | 数字分隔符 |
| `42L`、`42U` | 长整数与无符号整数字面量 |
| `3.14`、`3.14f`、`1.0e10` | 双精度、单精度和科学计数法 |
| `12.50m` | 十进制 `decimal` 字面量 |
| `'A'`、`'中'` | 字符码点字面量 |
| `"text"` | 普通字符串 |
| `@"C:\data\file"` | 逐字字符串，反斜杠不作转义 |
| `$"value={value}"` | 插值字符串 |

普通字符串支持 `\n`、`\r`、`\t`、`\\`、`\"`、`\'`、`\0`，以及 `\x` 的 1–4 位十六进制和 `\u` 的固定 4 位形式。当前不支持 `\U` 的 8 位形式，独立代理码点会被拒绝。

逐字插值形式 `$@"..."`、`@$"..."` 和三引号原始字符串均已有测试。原始字符串可跨行，其缩进处理与闭合行有关；插值原始字符串也有独立测试，复杂文本应按目标 SDK 核实。

字符串长度和索引按 UTF-8 字节计算，不能把上表中的字符字面量与字符串下标结果混为一谈。

## 局部变量、常量与赋值

以下是方法体内的声明片段，展示不同声明方式，不是独立程序。

```zan-fragment
int count = 3;
var label = "items";
let ratio = 0.5;
const int Limit = 100;
count += 1;
```

显式类型适合公开接口和需要固定数值宽度的场合。`var` 由初始化表达式推断局部变量类型，并不会把变量变成动态类型；参数和返回类型仍需明确声明。

`let` 已是合法声明语法，但当前尚未完整强制不可变性，不能用它代替可靠的只读契约。`const` 要求编译期常量；字段的 `readonly` 规则与局部 `let` 不同。

赋值支持 `=` 以及常用复合形式，例如 `+=`、`-=`、`*=`、`&=` 和移位赋值。类或集合变量的赋值共享对象，结构体赋值复制值，详见[内存与资源](memory.md)。

## 方法、参数与构造

方法属于类型，可使用块体，也可使用 `=>` 表达式体。实例方法通过对象调用，静态方法建议明确使用类型名限定。

方法可重载；重载解析考虑签名与参数类型。接口设计应让不同重载的用途明确，尤其避免依赖 lambda 的模糊推断来区分含义完全不同的调用。

参数相关能力如下。

- 普通值参数接收值的副本；引用类型参数中的副本仍指向原对象。
- `ref` 和 `out` 显式表达通过调用修改调用方位置。
- 命名实参采用 `Method(name: value)` 的形式。
- 可选参数在声明中给出默认值。
- `params T[]` 表达末尾的可变数量参数。

下列是方法声明片段，形状对应 `default_parameters.zan` 和 `params_variadic.zan` 等用例；声明应放入类型内部。

```zan-fragment
static int Add(int a, int b = 10) {
    return a + b;
}

static int Sum(params int[] values) {
    int total = 0;
    foreach (int value in values) {
        total += value;
    }
    return total;
}
```

构造器使用类型名，无返回类型。构造链写在签名后，例如 `: this(...)` 或 `: base(...)`；它与在方法体里调用一个普通方法不同。

局部函数也有实现与测试。它们便于把局部算法放在使用位置附近；若形成捕获，仍要考虑所捕获状态的生命周期。

## 条件、循环与分支

| 语法 | 用途与要点 |
| --- | --- |
| `if` / `else` | 条件必须是 `bool` |
| `while` | 每轮开始前检查条件 |
| `do` / `while` | 先执行循环体，再检查条件 |
| `for` | 初始化、条件、推进表达式组合 |
| `foreach` | 遍历数组、集合或枚举协议 |
| `break` | 退出当前循环或语句 `switch` |
| `continue` | 进入下一次循环 |
| `return` | 离开方法，可携带返回值 |
| `goto` 与标签 | 显式控制转移，跨清理作用域应谨慎设计 |

条件位置不会自动把整数当作真假。误写 `if (x = 1)` 是编译错误。

`foreach` 遍历 `List<T>` 时得到元素，遍历数组时得到对应元素，遍历内建字典时得到键。需要键值对或特定顺序时，先核对所用容器的枚举接口。

语句 `switch` 支持常量、`null`、类型模式及 `when` 守卫。一个非空分支执行结束后隐式退出，不写 `break` 也不会落入下一个非空分支。连续的空 `case` 标签可以共享后面的分支体。

`switch` 表达式写成 `value switch { ... }`，用每个分支的 `=>` 产生结果。应明确提供兜底分支；当前无匹配分支时可能返回结果类型的零值，不能依靠它自动抛“非穷尽”异常。

`&&`、`||` 用于短路逻辑，`??` 提供空值回退，`?:` 为条件表达式。复杂表达式使用括号清楚表达优先级，尤其在类型测试和条件选择组合时。

## 命名空间与访问边界

文件作用域命名空间采用 `namespace App.Core;`，也支持块形式 `namespace App.Core { ... }`。`using App.Core;` 导入其中的名字；实际命名空间由源码声明决定，不单由目录名决定。

下列是文件头声明片段，不包含入口。

```zan-fragment
using System;
namespace Sample.Core;
```

成员可见性规则与 C# 的默认值不同。

| 修饰符 | 当前成员访问范围 |
| --- | --- |
| 不写或 `public` | 公开 |
| `private` | 声明成员的类型内部 |
| `protected` | 声明类型及派生类型内部 |
| `internal` | 同一命名空间模块 |

`internal` 的模块按命名空间前两段划分，例如 `System.Data.SqlServer` 和 `System.Data.MySql` 同属 `System.Data`。它不是程序集边界。顶层类型本身的访问修饰符当前不提供与成员同等的限制。

静态字段初始化器与静态构造器在 `Main` 前执行，字段初始化先于该类型的静态构造器。初始化顺序有外部依赖时，应考虑把需要失败处理的工作明确放进入口流程。

## 异常与清理

`throw` 抛出异常；`try` / `catch` 按异常类型处理；`finally` 放置无论成功还是失败都需要执行的清理。异常类型来自当前标准库，不能凭 C# 类型名猜测全部可用集合。

下面是方法体片段，使用 `cs_b02_using.zan` 中已有的异常构造与捕获形式。

```zan-fragment
try {
    throw new Exception("oops");
} catch (Exception e) {
    Console.WriteLine(e.Message);
} finally {
    Console.WriteLine("finished");
}
```

在 `catch` 中重新抛出当前异常可用裸 `throw;`。直接等待异步调用时，跨挂起的异常与清理仍有专门的状态机支持；分离任务的错误交接是另一种契约，见[异步与并发](async.md)。

Zan 允许 `finally` 内 `return`，且会覆盖此前已确定的返回结果。它是语言差异；业务清理代码通常应避免用这种写法掩盖原来的返回或异常意图。

普通异常、编译诊断和运行时防护报告不是一回事。例如 `checked` 整数算术溢出当前采用软运行时报告后继续传递回绕值的方式，`ZAN_RT_HARD=1` 可改变为硬失败；不能默认它等同于可捕获的 `OverflowException`。

## 资源作用域与迭代器

`using (resource) { ... }` 语句用于确定性调用资源的释放协议。它与文件头的命名空间 `using` 功能不同。`lock` 用于同步访问共享对象；`unsafe`、`fixed` 与原生地址的关系见[原生互操作](interop.md)。

`yield return` 与 `yield break` 的执行方式取决于方法返回类型。

| 返回形式 | 当前行为 |
| --- | --- |
| `IEnumerable<T>` | 生成惰性迭代器，调用时不执行方法体，每次推进产生元素 |
| `List<T>` | 急切执行并物化为隐藏列表 |

当前惰性迭代器方法要求为 `static`，局部声明使用显式类型；不支持在 `foreach`、`lock`、`catch` 体内产出。局部名字需在整个方法内唯一，也不能与外层类型成员同名。

这些是特定迭代器降级路径的限制，不应推广为“Zan 不支持局部类型推断”或“不支持 `foreach`”。

## 核实来源

- `docs/SPEC.md`：§2、§4、§5、附录 A；原始字符串和转义取附录及测试的较新事实。
- `tests/conformance/array_init_foreach.zan`：完整“数组求和”程序所用语法与 API。
- `tests/conformance/main_args.zan`、`var_call_initializer.zan`：入口参数与局部推断。
- `tests/conformance/raw_string.zan`、`interpolated_raw_string.zan`、`verbatim_interpolated_string.zan`、`string_escapes.zan`、`lexer_char_multibyte.zan`：文本字面量。
- `tests/conformance/default_parameters.zan`、`params_variadic.zan`、`cs_b04_named_args.zan`、`constructor_chaining.zan`、`cs_b11_local_func.zan`：调用与构造。
- `tests/conformance/cs_b05_pattern_var.zan`、`cs_b06_switch_expr.zan`、`pattern_value_types.zan`：分支与模式。
- `tests/conformance/cs_b02_using.zan`、`async_finally_scope_transfers.zan`、`cs_b14_checked.zan`：异常、清理和溢出语义。
- `tests/conformance/yield_iterators.zan`、`iterator_lazy_semantics.zan`：两种产出模型。
