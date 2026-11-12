# 类型系统

当前实现说明，按仓库源码与测试核实于 **2026-11-11**。已安装 SDK 可能与此仓库状态不同。本文描述语言层的类型与组合方式；集合 API 的完整清单见[标准库与包](libraries.md)。

Zan 是静态类型语言。表达式在编译时具有类型，`var` 只推断局部声明的类型，不会启用动态分派或允许随意改变变量类型。

## 基础类型与数值宽度

| 类型 | 当前表示与用途 |
| --- | --- |
| `bool` | 布尔值，存储通常占 1 字节 |
| `byte` / `sbyte` | 8 位无符号 / 有符号整数 |
| `ushort` / `short` | 16 位无符号 / 有符号整数 |
| `uint` / `int` | 32 位无符号 / 有符号整数 |
| `ulong` / `long` | 64 位无符号 / 有符号整数 |
| `float` | 32 位 IEEE 754 二进制浮点 |
| `double` | 64 位 IEEE 754 二进制浮点 |
| `decimal` | 128 位十进制表示，96 位整数尾数与符号、精度信息 |
| `char` | Unicode 码点，当前主编译器存储为 8 字节 |
| `nint` | 原生地址和句柄使用的整数，当前主编译器降级为 64 位整数 |
| `void` | 没有返回值 |

`int` 和 `long`、`float` 和 `double` 是不同类型。浮点字面量默认是 `double`，`f` 后缀产生 `float`；`m` 后缀产生 `decimal`。

`decimal` 不是 `double` 的别名。当前实现有独立布局与算术运行时，标准库 `System.Decimal` 提供相应转换与数值操作。需要验证外部输入时，应核对 `Parse` 和 `TryParse` 各自的失败契约。

`nint` 的用途不能代替 ABI 验证，尤其不能由名字推导它在所有目标上都等于 C 的指针宽度。原生函数的地址、长度和整数宽度见[原生互操作](interop.md)。

## 文本、对象与可空值

`string` 是不可变 UTF-8 文本。`Length` 和下标按字节计算，`"中文".Length` 为 6。`char` 是码点类型；字节、码点、字素簇和显示宽度分别回答不同的问题。

引用类型本身允许 `null`。在引用类型上写 `?` 不代表存在另一套与 C# 可空引用分析完全相同的保证。使用可能为空的对象前，仍须检查其来源与生命周期。

值类型可写成 `T?`，例如 `int?`、`Point?`。常用操作包括 `HasValue`、`Value`、`GetValueOrDefault()` 与 `??`。先确认有值，再读取 `Value`。

以下是方法体片段，对应可空值类型测试。

```zan-fragment
int? missing = null;
int value = missing ?? 7;
int? present = 5;
if (present.HasValue) {
    Console.WriteLine(present.Value);
}
```

`object` 用于承载引用对象，也支持当前实现允许装箱的用户结构体。它不是可以无条件容纳所有基础数值的万能容器，详见本页“转换与装箱”。

## 结构体与类

结构体具有值语义：赋值与普通参数传递复制结构体值。它可以位于局部存储中，也可以内嵌在对象、数组或集合元素中；“值类型”不等于“永远只在栈上”。

结构体可以包含引用字段。复制结构体会复制这些引用的持有关系，不会递归复制它们指向的对象。相关字段仍需要正确的 ARC 管理。

以下是类型声明片段，可放在命名空间中。

```zan-fragment
struct Point {
    public int X;
    public int Y;
}

class Counter {
    public int Value;
}
```

类具有引用语义，支持一个基类和多个接口。`abstract`、`virtual`、`override`、`sealed` 用于表达继承和重写关系。构造器可使用 `: base(...)` 或 `: this(...)` 进行构造链调用。

类型可以有字段、属性、方法、构造器和静态成员。未指定访问修饰符的成员默认公开，具体访问边界见[语言基础](language.md)。

属性支持访问器与自动属性，`init` 有针对性用例。索引器用 `this[...]` 声明。读取属性可能执行代码，不能将任意属性访问都视为无副作用的字段读取。

## 枚举、记录与嵌套类型

枚举定义命名整数常量，允许显式值。它不提供带载荷的代数数据类型。当前主编译器将枚举映射为 64 位整数，因此原生接口不能默认它等同于 C 的 `int` 枚举。

`record` 的位置参数形式已有实现，可产生字段访问、相等比较和字符串表示；`with` 表达式支持基于现有记录修改成员的写法。

下面是声明与表达式片段，来自记录类型测试的形状。

```zan-fragment
record Point(int x, int y);
```

```zan-fragment
Point first = new Point(1, 2);
Point second = first with { x = 3 };
```

记录相等与普通类的引用身份不是同一概念。引用字段的内部对象是否共享仍需按字段类型判断，不能把 `with` 当成任意对象图的深复制。

嵌套类、结构体等类型形状已有测试，嵌套类型作为数组元素也有专门回归用例。跨文件访问时应使用可解析的限定名，并确认类型所属命名空间。

## 接口

接口声明调用契约，类和结构体可以实现接口，接口之间可以继承。通过接口变量调用方法时，由实际实现提供行为。

默认接口方法已有实现：接口可以提供方法体，实现类型可继承它或提供自己的实现。这不意味着接口可以承担普通实例字段的存储职责。

以下是接口声明片段，形状对应 `interface_default_methods.zan`。

```zan-fragment
interface Named {
    string Name();
    string Label() { return "[" + this.Name() + "]"; }
}
```

接口参数也可包含 `ref` 或 `out` 这类显式传递方式。设计接口时应将“是否找到”“返回对象是否为空”“调用方是否负责关闭”分别写清楚。

## 泛型与约束

泛型类型和泛型方法用类型参数表达可复用算法，类型实参可以是值类型或引用类型。泛型字段、结构体元素与泛型方法均有当前回归测试，不应沿用旧版本的通用禁用建议。

以下是泛型声明片段，依据 `generic_new_constraint.zan` 中的工厂形式。

```zan-fragment
class Factory {
    public static T Produce<T>() where T : new() {
        return new T();
    }
}
```

类型实参可显式写出，例如 `Factory.Produce<Counter>()`。部分调用可根据实参推断类型；当上下文不足或重载难以区分时，应明确写出类型实参。

当前基类约束、接口约束及 `new()` 约束有检查与测试。`new()` 约束用于要求可以创建对应类型，不能把任意无约束 `T` 当成可实例化的具体类型。

`where T : class` 与 `where T : struct` 的语法也会被解析，但当前检查器没有为这两个标志提供与命名约束同等的强制检查。不要用它们单独承担关键正确性保证。

泛型接口和委托支持 `out` 协变与 `in` 逆变，已有接口转换和方法组适配测试。普通可变容器不能据此推断成任意协变容器。

泛型的内部特化和表示属于编译器实现细节；公开接口应依赖类型规则，不依赖猜测的字段槽位或代码共享方式。

## 委托、lambda 与事件

委托描述可调用对象的参数与返回类型。可使用具名委托，也可使用 `Action`、`Action<T...>`、`Func<T..., TResult>` 等常见形式。

以下是声明与表达式片段，不包含入口。

```zan-fragment
delegate int Comparison<T>(T left, T right);
```

```zan-fragment
Func<int, int> twice = (int value) => value * 2;
```

静态方法组、实例方法组、表达式 lambda、块体 lambda 和匿名 `delegate` 方法都有相应调用路径。返回值必须与目标委托签名兼容。

捕获局部状态的闭包会保留它需要的数据。捕获同一个局部变量的闭包可以共享状态，不能把捕获默认理解为创建时的值快照。

事件与委托组合支持订阅回调。订阅关系可能延长订阅者寿命；适时解除订阅或使用弱关系，见[内存与资源](memory.md)。原生回调另有 ABI 限制，见[原生互操作](interop.md)。

## 数组、集合与视图

数组支持一维 `T[]`、交错数组 `T[][]` 和矩形多维数组 `T[,]`、`T[,,]`。矩形数组可使用 `GetLength(dimension)` 查询指定维度，`Length` 为元素总数。

数组初始化可写 `new int[] { 1, 2, 3 }`。`List<T>` 用于可变长度序列，`Dictionary<K,V>` 用于键值映射；它们的变量赋值共享同一容器。

`List<T>.Count` 与字典的 `Count` 是属性。标准库其他容器可能使用 `Count()` 方法，应核对真实签名。

`Span<T>` 与 `ReadOnlySpan<T>` 是带地址和长度的视图。只读视图限制通过该视图写入，不代表底层缓冲区不会被其他别名修改，也不自动延长外部缓冲区寿命。

## 元组与解构

元组用 `(T1, T2)` 等形式组合值，可作为局部类型或方法返回类型。元素可通过 `Item1`、`Item2` 等成员读取。

以下是方法体片段，对应 `cs_b03_tuple.zan`。

```zan-fragment
var pair = (42, "answer");
var (number, text) = pair;
Console.WriteLine(number);
Console.WriteLine(text);
```

类型声明中可附元素名，但通用文档示例使用 `ItemN` 或局部解构名最明确。当前嵌套解构仍有边界，不应由简单二元组测试推导任意嵌套模式都可用。

## 类型判断、转换与装箱

`is` 进行类型测试，可同时绑定模式变量；`as` 尝试转换引用对象，失败返回 `null`。显式引用下转或结构体拆箱 `(T)value` 失败会抛 `InvalidCastException`，不再采用历史上的静默默认值行为。

当前支持可装箱用户结构体向 `object` 的显式和隐式转换，以及结构体向所实现接口的转换。拆箱得到结构体值；普通整数等基础标量向 `object` 的转换仍会被检查器拒绝。

数值转换与引用转换是不同机制。收窄整数可能丢失高位，浮点转整数可能丢失小数；需要溢出检查时还应核对当前 `checked` 语义。

类型还可声明运算符和用户转换。它们应遵循清楚的参数、结果和副作用契约，不能仅凭运算符外观推断成本。

## 模式匹配与类型信息

已核实的模式包括常量、`null`、`is not null`、类型与绑定变量，以及 `switch` 分支的 `when` 守卫。`switch` 表达式支持 `_` 兜底。

值类型的类型模式可按静态类型匹配。模式能力应以实际语法和测试为界，不能从 C# 的完整模式列表反推关系模式、列表模式或递归属性模式都已实现。

`typeof`、`nameof` 与库中的类型信息机制有专门测试。需要动态成员调用时，先核对反射 API 的支持面和发布配置，不把类型名字当成任意反射能力的保证。

## 核实来源

- `src/compiler/irgen/irgen_type_map.c`：`map_type`、`get_decimal_type` 的当前表示。
- `stdlib/System/Decimal.zan`、`tests/conformance/decimal_arithmetic.zan`、`decimal_divide_wide.zan`：十进制类型。
- `tests/conformance/nullable_value_types.zan`、`nullable_reference_types.zan`：可空值与引用。
- `tests/conformance/property_accessors.zan`、`cs_b09_indexer.zan`、`cs_b18_init.zan`、`record_types.zan`、`record_with_expr.zan`、`nested_type_array.zan`：成员与复合类型。
- `tests/conformance/interface_default_methods.zan`、`generic_constraints.zan`、`generic_new_constraint.zan`、`generic_method_where_constraint.zan`、`cs_b13_variance_upcast.zan`：接口与泛型。
- `src/compiler/parser/parser_member.c`、`src/compiler/checker/checker_util.c`、`checker_call.c`：泛型约束解析与检查边界。
- `tests/conformance/cs_b01_func_action.zan`、`closure_capture_by_ref.zan`、`event_delegate.zan`：委托与捕获。
- `tests/conformance/cs_b08_arrays.zan`、`cs_b03_tuple.zan`、`span_intrinsic.zan`：数组、元组和视图。
- `tests/conformance/as_semantics.zan`、`cast_invalid_catch.zan`、`struct_boxing_implicit.zan`，以及 `src/compiler/checker/checker_type.c`：转换与装箱。
- `tests/conformance/pattern_value_types.zan`、`cs_b05_pattern_var.zan`、`typeof_nameof.zan`、`operator_overload.zan`：模式、类型信息和运算符。
