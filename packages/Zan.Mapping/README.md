# System.Mapping

`System.Mapping` 是**对象映射**：把一个对象按成员拷到另一个对象上。

它对标 AutoMapper（10,188★）/ Mapster（5,182★）那一档，但只做**显式映射**：
运行期用 `MapProfile<TSource, TTarget>` 逐成员规则映，生成期用 `GenMapping` 读
编译期导出的实体元数据、按约定配对成员、生成**普通 Zan 源码**。

纯 Zan 实现，无原生依赖，**无反射**。

## 设计取舍

- **不做运行期反射式自动映射**：AutoMapper 的"按约定自动映射"靠运行期反射——
  运行期得知道目标类型有哪些成员。Zan 没有反射（没有 `typeof`、没有成员枚举），
  所以自动匹配只能放在**生成期**：`zanc --gen-meta out.json` 导出实体元数据，
  `GenMapping` 按约定配对，生成显式的 `dst.x = src.x;`。生成物是编译期常量：
  没有反射、没有装箱、没有运行期开销，也没有"约定悄悄变了"的可能（生成物进
  版本库，diff 得出来）。
- **生成期与运行期是两条互补的路，不是二选一**：要"每条规则都能单独写、单独
  报错"的复杂映射用 `MapProfile`；要"几十个 DTO 一次性对齐"的机械映射用
  `GenMapping` 生成代码后直接用。两条路都产出同样的显式代码。
- **规则交还新值而不是就地改**：`MapRule` 的函数签名是 `(源, 目标) → 新目标`。
  这样类目标和**结构体目标**都对——结构体是值语义，就地改会改在副本上（改完
  丢掉）。
- **目标工厂必填**：Zan 目前不能 `new T()`（类型参数实例化会得到 null，静默
  错值，见 `TASKS.md` 记的编译器缺陷）。目标对象由调用方造，于是"用哪个构造
  器"（无参 / 带参 / 工厂方法）都是调用方的事。
- **错误可区分**：失败有错误码（`MappingError`）+ 成员名，两条出口——`Map`
  抛 `MappingException`，`TryMap` 返回 `MappingResult`（不抛）。错误码区分
  "源是 null"/"某条成员规则抛了"/"值转换失败"/"工厂没给或给了 null"/"档案一条
  规则都没有"，排查时能直接分头去查。
- **约定只覆盖"能确定"的那部分，配不上的如实报出**：同名同类型直拷；扁平化
  （一级字段名 + 二级字段名大小写无关相等，如 `customer.name` →
  `customerName`）。类型对不上、需要显式转换的**不猜、不静默跳过**——
  `UnmatchedByConvention` 把它们列出来，由人决定。
- **非线程安全**：规则表与转换器表是逐线程读写的状态；一个档案只在一个线程
  里用。

## 能力

| 能力 | 入口 |
|---|---|
| 运行期映射档案 | `MapProfile<TSource, TTarget>`：`Member`（链式）/ `Map` / `TryMap` / `MapList` / `TryMapList` / `RuleCount` / `MemberName` / `MemberNames` |
| 成员规则 | `MapRule<TSource, TTarget>`：`Name` + `Apply`（`(源, 目标) → 新目标`） |
| 值转换器 | `ValueConverter<TSource, TTarget>`：`Apply`（失败抛）/ `TryConvert`（不抛） |
| 生成期映射 | `GenMapping`：`Emit` / `TryEmit` / `TryEmitFromEntities` / `MatchByConvention` / `UnmatchedByConvention` |
| 生成规格 | `MappingSpec` + `MappingMember`（源成员路径 → 目标成员名） |
| 错误 | `MappingException` + `MappingError`（`None` / `SourceNull` / `MemberFailed` / `ConvertFailed` / `InvalidFactory` / `InvalidSourceList` / `NoRules` / `InvalidSpec`） |

## 用法

运行期（显式规则，逐成员可归因）：

```zan
using System;
using System.Mapping;

class Order {
    public string code;
    public int amount;
    public Customer customer;
}

class OrderDto {
    public string code;
    public int amount;
    public string customerName;
}

MapProfile<Order, OrderDto> profile = new MapProfile<Order, OrderDto>(
    () => new OrderDto());                        // 目标工厂必填
profile.Member("code", (Order s, OrderDto d) => { d.code = s.code; return d; });
profile.Member("amount", (Order s, OrderDto d) => { d.amount = s.amount; return d; });
profile.Member("customerName", (Order s, OrderDto d) => {
    d.customerName = s.customer.name; return d; });

OrderDto dto = profile.Map(order);                // 失败抛 MappingException
```

不抛的那条路（按错误码分支）：

```zan
MappingResult<OrderDto> r = profile.TryMap(order);
if (r.IsError) {
    Console.WriteLine(r.Error.ToString());         // SourceNull / MemberFailed / ...
    Console.WriteLine(r.Member);                   // 哪条成员规则出的问题
}
```

值转换器（源与目标字段类型不同时）：

```zan
ValueConverter<int, string> toText = new ValueConverter<int, string>(
    "int2text", (int v) => v.ToString());

string s = toText.Apply(42);                      // 失败抛 ConvertFailed
bool ok = toText.TryConvert(1, out s);             // 不抛
```

生成期（按约定配对 → 生成显式源码）：

```bash
zanc --gen-meta entities.json src/Order.zan       # 导出实体元数据
```

```zan
JsonValue entities = JsonSerializer.Deserialize(jsonText);
string code = "";
string error = "";
if (GenMapping.TryEmitFromEntities(entities, "Order", "OrderDto",
                                   "OrderMapper", "Demo", true,
                                   out code, out error)) {
    File.WriteAllText("OrderMapper.zan", code);    // 生成物进版本库，diff 得出来
}
```

生成物长这样（**没有反射，就是普通源码**）：

```zan
public static class OrderMapper {
    public static OrderDto Map(Order src) {
        OrderDto dst = new OrderDto();
        dst.code = src.code;
        dst.amount = src.amount;
        dst.customerName = src.customer.name;
        return dst;
    }
}
```

配不上的成员会单独列出来（不静默跳过）：

```zan
List<string> unmatched = GenMapping.UnmatchedByConvention(entities, "Order", "OrderDto");
// ["noteText"] —— 类型对不上，需要显式转换或改名字，由人决定
```

## 验证

`tests/conformance/mapping_profile.zan` 覆盖：链式注册与规则计数、成员名列表、
类目标映射、列表映射、源为 null（`SourceNull`）、成员规则抛异常时的错误归因
（`MemberFailed` + 成员名）、抛的那条路（`MappingException` 带错误码与成员名）、
值转换器（成功 / 失败抛 / 失败不抛）、**结构体目标**（值语义，规则交还新值）。

`tests/conformance/mapping_gen.zan` 覆盖：约定一（同名同类型）、约定二（扁平化）、
配不上的如实报出、生成源码的形状（显式字段拷贝、不碰私有字段、不拷配不上的
成员）、目标类型没有无参构造器时报错、源类型不在元数据里时报错、非法规格抛
`InvalidSpec`。

ARC 守卫（`run_arcguard`）干净，`--check-leaks` 无泄漏。

## 已知边界

- **没有运行期反射式自动映射**：Zan 没有反射，自动匹配只在生成期（见上）。
- **不能从运行期拿类型名**：配对靠编译期导出的元数据，运行期拿不到"这个类型
  有哪些成员"。
- **生成器按名字配对，不按类型身份**：`zanc --gen-meta` 导出的是类型名，重名
  类型（同名的类族）会让配对挑花眼——生成前先看一眼配对结果。
- **结构体目标要求规则交还新值**：就地改的写法在结构体上会改在副本上（改完
  丢掉），这是语言的值语义，不是库的 bug。
- **档案没有并发保护**：规则表是逐线程读写的状态。
- **没有嵌套映射、集合映射、循环引用检测**：集合映射只覆盖"列表的列表元素逐个
  映"（`MapList`），嵌套对象靠规则里手写。

## 放置

包约定：`packages/Zan.Mapping/src/System/Mapping/*.zan`。
非游戏领域能力走独立包并单独版本化，不进 stdlib。
