# System.Test.Fixtures

`System.Test.Fixtures` 是**假数据生成器**：种子化的随机源 + 语义化数据（人名、邮箱、地址、句子……）+ 集合假数据 + 显式装配的取值器。

它补的是 Bogus（9,747★）/ AutoFixture（3,539★）那一档。测试里造
`test@test.com` 当邮箱，格式化与校验的分支永远走不到；造 `15000000000` 当
手机号，运营商规则永远走不到。假数据的价值不在"随机"，在"**像真的**"。

## 设计取舍

- **无反射，对象图必须显式装配**：AutoFixture 靠反射扫字段自动填，Zan 没有
  反射。本包提供另一半——**按名字取值**：

  ```zan
  FixtureBuilder fb = new FixtureBuilder(new FixtureRandom(42));
  fb.WithString("name").WithInt("age", 18, 60).WithEmail("mail");
  User u = new User();
  u.name = fb.String("name");
  u.age = fb.Int("age");
  u.mail = fb.String("mail");
  ```

  装配是显式的，但每个字段的来源都看得见。

- **登记时就把值定下来**，不是取值时现生成：同一键两次取到**同一个值**。
  否则"对象里的字段"与"断言里的期望值"会拿到两份不同的数据，断言随机失败。

- **未登记的键当场报错**（`FixtureError.KeyNotRegistered`），不现编一个值。
  "忘了登记"和"名字打错"都当场暴露，而不是变成一个看起来合法的假值。

- **确定性优先**：`new FixtureRandom(seed)` 同种子必然同序列。失败样本把种子
  记进测试名就能复现。UUID 也自己拼字节（不用操作系统的随机源），只把版本位
  与变体位摆正——形态合法，但仍可复现。

- **不生成"看起来能用"的敏感数据**：卡号走保留测试段（`4242…`）、电话走保留
  测试号段（`+1-555-…`），避免假数据被误当成真数据发出去。

- **唯一值有重试上限，取尽报错**：重复的"唯一"字段会让测试以莫名其妙的方式
  失败（去重逻辑、去重后计数不符），宁可当场炸。

- **词表内联且不缓存**：词表做成静态数组会在进程退出时留下一份不释放的 rc
  集合（见路线图 §7 的已知缺陷），所以改成每次调用现造数组——测试数据生成
  不在热路径上。

- **非线程安全**：内部有状态。要并行就每个线程各建一个。

## 能力

| 能力 | 入口 |
|---|---|
| 随机源 | `FixtureRandom`：`Int` / `Long` / `Double` / `Float` / `Bool` / `String` / `Alpha` / `Alphanumeric` / `Digits` / `Hex` / `Bytes` / `Uuid` / `DateTime` / `TimeSpan` / `Pick` / `Shuffle` |
| 语义化数据 | `FixtureText`：`FirstName` / `LastName` / `FullName` / `UserName` / `Email` / `Domain` / `Url` / `PhoneNumber` / `StreetAddress` / `City` / `Country` / `ZipCode` / `Company` / `Sentence` / `Paragraph` / `Words` / `Slug` / `ColorHex` / `SemanticVersion` / `CreditCardNumber` |
| 集合 | `FixtureCollection`：`Of` / `Array` / `Strings` / `Ints` / `Map` / `Unique` / `UniqueStrings` / `Chunks` / `Sample` |
| 装配 | `FixtureBuilder`：`WithString` / `WithCustom` / `WithInt` / `WithDouble` / `WithBool` / `WithDateTime` / `WithEmail` / `WithFullName` / `WithUuid` + 对应的取值方法 + `Keys` |
| 错误 | `FixtureException` + `FixtureError`（`KeyNotRegistered` / `ConstraintViolated` / `InvalidArgument`） |

## 用法

```zan
using System;
using System.Collections.Generic;
using System.Test.Fixtures;

// 1. 固定种子（失败样本把种子记进测试名就能复现）
FixtureRandom r = new FixtureRandom(42);
Console.WriteLine(r.Int(1, 100));
Console.WriteLine(r.Uuid());

// 2. 语义化数据
FixtureText t = new FixtureText(r);
Console.WriteLine(t.Email());        // 8fq3kd1a@example.org
Console.WriteLine(t.FullName());     // 王伟
Console.WriteLine(t.StreetAddress());// 217 Maple St

// 3. 集合假数据
FixtureCollection c = new FixtureCollection(r);
List<string> names = c.Strings(5);
List<string> unique = c.UniqueStrings(10);   // 互不相同；取尽会报错
List<int> nums = c.Of<int>(3, i => i * 10);

// 4. 显式装配
FixtureBuilder fb = new FixtureBuilder(new FixtureRandom(7));
fb.WithString("name").WithInt("age", 18, 60);
User u = new User();
u.name = fb.String("name");
u.age = fb.Int("age");
```

## 已知边界

- **没有反射式自动装配**：字段要一行行装配（Zan 没有反射）。
- **没有属性 / 字段级的"忽略"规则**：不装配就是不装配。
- **没有日期序列**（工作日 / 节假日 / 过去 N 天）。
- **词表是内联小表**，不是多语言语料库；不同 locale 之间没有对应词表。
- **不保证跨语言一致**：同一字段在中文与英文语境下不会自动换一套数据。

## 验证

`tests/conformance/test_fixtures.zan` 覆盖：随机源的区间边界与反向区间、
字符串长度、字母 / 十六进制字符集、UUID 形态（版本位与变体位）、字节数、
日期与时长区间、空集合取样报错；同种子同序列与异种子异序列；邮箱 / URL /
地址 / 电话 / 颜色 / 版本号 / 卡号的形态；集合的定长生成与下标传递、数组与
字典、唯一性、取尽报错、分块、抽样不重复；装配器的取值稳定、类型隔离、
未登记键报错、自定义生成器只跑一次、键名清单、登记区间生效。

## 放置

测试支撑包约定：`packages/Zan.Test.Fixtures/src/System/Test/Fixtures/*.zan`。
与 `Zan.Test.Mocks`（替身与行为校验）配套：本包造数据，那个包验行为。
