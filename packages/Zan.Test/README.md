# Zan.Test

轻量测试框架，纯 Zan 实现，无原生依赖、无反射、无 codegen。
`using System.Test;` 即可——交叉编译到嵌入式目标也能跑（只依赖 `Console`
与 `Stopwatch`）。

## 内容

| 类型 | 作用 |
|---|---|
| `Assert` | 非中断断言：失败立刻打一行 `FAIL …` 并计数，用例体继续跑完 |
| `Test` | 运行器：显式注册用例、setup/teardown、分组、skip、结果记账、汇总与退出码 |
| `TestResult` | 单用例结果（名字/是否通过/原因/耗时微秒） |
| `Fixture` | pytest 式 fixture：注册 / 依赖 / 作用域 / autouse / 拆毁 |
| `TestCase` | 带标签的用例（参数化的通用形态）+ `ParamInt/ParamString/…` 委托 |

## 用法

```zan
using System;
using System.Test;

class Tests {
    static MemoryCache<string> cache;

    static void Main() {
        Test.verbose = true;                       // CI 想看每个用例就打开
        Test.Setup(() => { cache = new MemoryCache<string>(); });
        Test.Teardown(() => { cache.Clear(); });

        Test.Run("cache.set-get", () => {
            cache.Set("k", "v");
            Assert.Equal("v", cache.Get("k"), "roundtrip");
            Assert.Null(cache.Get("missing"), "miss-is-null");
        });

        Test.Run("cache.ttl", () => {
            cache.Set("k", "v", 5);
            Thread.Sleep(40);
            Assert.Null(cache.Get("k"), "expired");
        });

        Test.Finish();                             // 汇总 + 以失败数作退出码
    }
}
```

输出契约（三行式，便于 grep 与金样比对）：

```
ok cache.set-get
FAIL cache.ttl: expired
FAIL cache.set-get: expected v, got x
skip slow-path: not implemented
tests: 4 passed, 1 failed, 1 skipped
```

## fixture 与参数化

### fixture

```zan
using System;
using System.Collections.Generic;
using System.Test;

class FakeDb {
    public string name;
    public FakeDb(string name) { this.name = name; }
}

class Tests {
    static void Main() {
        // 注册：类型化（推荐）。类型在注册时就钉死，取值时能校验。
        Fixture.Register<FakeDb>("db", () => { return new FakeDb("main"); });
        // 模块作用域：跨用例复用同一份，直到 Test.Reset()。
        Fixture.RegisterModule<string>("conn", () => { return "conn-1"; });
        // 依赖：db 先建 conn。
        Fixture.Depends("db", "conn");
        // 每个用例体之前自动建好。
        Fixture.Autouse("conn");
        // 拆毁回调：实例拆掉前调用。
        Fixture.OnTeardown("db", (object v) => {
            Console.WriteLine("close " + ((FakeDb)v).name);
        });

        Test.Run("uses-db", () => {
            FakeDb db = Fixture.Get<FakeDb>("db");   // 第一次取即创建
            Assert.Equal("main", db.name, "db name");
            Assert.Equal("conn-1", Fixture.Get<string>("conn"), "conn");
        });

        Test.Finish();
    }
}
```

| API | 作用 |
|---|---|
| `Register<T>(name, factory)` | 注册（函数作用域）。`factory` 的返回类型就是 fixture 的类型 |
| `RegisterUntyped(name, factory)` | 无类型注册：类型在首次创建时按值推断 |
| `RegisterModule<T>(name, factory)` | 模块作用域：跨用例复用，`Test.Reset()` 时才拆 |
| `RegisterModuleUntyped(name, factory)` | 模块作用域 + 无类型 |
| `Depends(name, dep)` / `Depends(name, [deps])` | 显式声明依赖，框架按声明顺序递归创建 |
| `Autouse(name)` | 标记为每个用例自动创建（依赖也随之解析） |
| `OnTeardown(name, fn)` | 挂拆毁回调，参数是工厂产出的实例 |
| `Get<T>(name)` / `GetRaw(name)` | 取值；未注册或类型不符抛 `FixtureException` |
| `Exists(name)` / `Created()` / `Live()` / `CurrentTest()` | 观察注册表与当前用例 |
| `Reset()` / `Clear()` | 拆掉模块作用域实例（定义保留）/ 连定义一起清空 |

### 参数化

```zan
List<int> nums = new List<int>();
nums.Add(1);
nums.Add(2);
Test.ParamInts("adds", nums, (int v) => {
    Assert.True(v > 0, "positive");
});
// → 用例 id：adds[1]、adds[2]，各自独立记账

Test.ParamIntPairs("sum", left, right, (int a, int b) => { /* … */ });
// → 用例 id：sum[1,10]

List<TestCase> cases = new List<TestCase>();
cases.Add(new TestCase("first", () => { /* … */ }));
Test.RunCases("scenarios", cases);
// → 用例 id：scenarios[first]
```

| API | 作用 |
|---|---|
| `ParamInts(name, values, body)` | int 参数，id 形如 `name[1]` |
| `ParamStrings(name, values, body)` | string 参数（空白与方括号在 id 里换成 `_`） |
| `ParamDoubles(name, values, body)` | double 参数 |
| `ParamBools(name, values, body)` | bool 参数 |
| `ParamIntPairs(name, a, b, body)` | 两个 int 参数的笛卡尔积，id 形如 `name[1,10]` |
| `RunCases(name, cases)` | 一组带标签的用例（多参数/异构走这条） |

## 语义契约

- **断言不抛异常**。失败的断言打行并累加 `Assert.Failures()`；`Test.Run` 用
  调用前后的差值判定该用例通过与否。因此一个用例里的多个问题**一次运行全部
  暴露**，不必"改一个跑一次"。需要"首个失败即中断"的场合用 `Assert.Throws`
  包住断言块，或在业务侧自己 `throw`。
- **用例体抛异常也算失败**：`Exception` 对象取 `Message`，`throw "字符串"`
  记为 `threw non-exception`，都在 `FAIL <name>: <原因>` 行里给出。
- **teardown 一定会执行**（用例体抛异常也执行），并且**不覆盖**用例体原本的
  失败原因——原失败优先。
- **失败原因行由谁打是有分工的**：断言失败由 `Assert` 打（`FAIL <what>: …`，
  `what` 是断言自己的名字），异常与 teardown 失败由 `Test.Run` 打
  （`FAIL <用例名>: <原因>`），两者不重复。
- **默认只打失败与汇总**；`Test.verbose = true` 才打通过用例的 `ok` 行，
  并可在 `Test.slowUs` 设置耗时红线后标注 `(slow <n>us)`。
- **`Report()` 返回失败数**、`Finish()` = `Report()` + 以失败数作退出码
  （注意：Zan 的 `static int Main()` 返回值**不会**变成进程退出码，要非零
  退出必须走 `Finish()`/`Environment.Exit`）。
- **`Reset()` 清空全部状态与计数器**，同一进程里可以跑多轮（适用于把测试嵌进
  长驻进程或在同一个可执行文件里跑多套用例）。
- 断言按类型重载：`Equal(int|long|double|bool|string, …)`。Zan 泛型里
  **不能比较 `T`**，所以不提供 `Equal<T>`——这是语言事实，不是遗漏；
  `NotNull`/`Null` 收 `object`，而 Zan 的 `object` 不接受值类型装箱，值类型
  用对应的 `Equal` 重载。
- **fixture 的值必须是引用类型**。Zan 的 `object` 不装箱值类型，`int` /
  `bool` 这类值类型装不进 fixture，也放不进 `List<object>`——参数化因此按
  类型特化（`ParamInt` / `ParamString` / …），而不是一个泛型的 `Param<T>`。
- **fixture 的类型在注册时就定下**。`Register<T>` 的类型参数既约束工厂的
  返回类型，也决定 `Get<T>` 怎么校验：类型对不上抛 `FixtureException`，
  不返回默认值。原因是 Zan 的**转型失败不抛异常**——`(string)某个非字符串`
  静默得到 `""`，靠转型失败来发现类型不对会把错误藏起来。
- **拆毁是逆序**（后建的先拆），函数作用域的 fixture 在用例结束时拆，
  模块作用域的等 `Test.Reset()`；用例体抛异常也照拆。拆毁回调抛异常的
  原因会报出来，但不覆盖用例体原本的失败原因——原失败优先。
- **依赖成环会被挡住**（抛 `FixtureException`），不会栈溢出。

## 与仓库 `ctest` 的关系

本包是给**Zan 项目自己写测试**用的（应用/库/模板），不是 `tests/conformance`
的替代：conformance 是 CTest 驱动的金样回归，覆盖面是编译器与 stdlib 契约；
`Zan.Test` 面向业务代码，跑在被测程序自己的进程里。

## 跨平台与依赖

只用 `System`（`Console`/`Exception`/`Convert`/`Stopwatch`）与
`System.Collections.Generic`，无平台分支，Windows / Linux / macOS / 嵌入式
同一份代码。

## 边界

- 不做自动发现（无反射/无注解扫描）：用例显式 `Test.Run` 注册，行为确定、
  可交叉编译。想要"注解即用例"需要编译器 codegen，本包刻意不做。
- 不做并行执行与资源池：并发留给调用方（各用例自己开 `Thread`/`Task`），
  避免运行器持有共享状态影响被测代码的时序。
- 不做 mock/替身：动态代理与 ARC 模型不匹配，替身请手写实现接口的假对象
  （见 `tests/conformance/cache_memory.zan` 里的 `FakeRemote`）。
- fixture 不做 pytest 的"间接参数化"（`indirect=True`）与 fixture 工厂的
  作用域覆盖：作用域只有函数级与模块级两种，依赖必须显式声明。
- 参数化不提供 `ids=` 覆盖与多参数笛卡尔积以外的组合：多参数异构走
  `RunCases`（自带 label）。
