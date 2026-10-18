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
