# System.Test.Mocks

`System.Test.Mocks` 是**接口替身 + 行为校验**层：调用记录、参数匹配、次数
期望、桩件、异步桩。

它对应 C# 生态里的 Moq / NSubstitute（6,410★），但走的是**另一条路**：
Moq 靠动态代理凭空造一个接口实现（代码生成 + 表达式树），Zan 这边没有
那套东西——**替身必须手写**。本包提供的是手写替身缺的那一半：**记录调用
与事后校验**。

## 设计取舍

- **没有运行时造 mock**：不生成代码、不用反射。`Mock<T>()` 这种写法在
  Zan 里不存在，替身是手写类。代价是每个接口要写一个替身；收益是没有
  代码生成、没有代理开销、行为完全可读可断言。
- **实参存规范化文本**：替身拿不到实参的静态类型（没有反射），只能存
  文本。所有实参经 `Arg.Text(...)` 规范化后记录，`Arg.Eq(3)` 与实际传入的
  `3` 才能对上。
- **匹配器只有六种**：`Any` / `Eq` / `NotEq` / `Contains` / `Prefix` /
  `Between`。Moq 的 `It.Is<int>(x => x > 3)` 靠表达式树求值，这里用区间
  （`Between`）覆盖最常见的数值范围断言。**实参个数必须相同**才可能匹配
  ——不匹配器不"通吃"。
- **断言与本包解耦**：校验返回 `bool`，失败描述由 `Describe()` 给出，
  用哪个断言库由调用方决定（本包不依赖 `Zan.Test`）。
- **记录与断言分开**：替身只记录，不在方法里断言。同一个替身可以先跑
  一遍，再断言好几条不同的事实。
- **桩件显式化**：`StubValue<T>`（固定返回值）、`Sequence<T>`（按顺序
  返回，用完重复末值）、`ActionSpy`（回调探针）、`TaskStub`（已完成 /
  已失败 / 永不完成的任务）。
- **序列为空时抛异常**：没有默认值可返回（Zan 没有 `default(T)`），
  而且"空序列"一定是配置错误。

## 能力

| 能力 | 入口 |
|---|---|
| 实参规范化 | `Arg.Text(int/long/double/bool/string)`、`Arg.ToDouble` |
| 参数匹配 | `Arg.Any` / `Eq` / `NotEq` / `Contains` / `Prefix` / `Between` / `AnyArgs` |
| 次数期望 | `Times.Exactly` / `Once` / `Never` / `AtLeast` / `AtMost` |
| 调用记录 | `CallRecord`、`CallLog`（`Record` / `Total` / `CountOf` / `CallsOf` / `LastOf` / `Clear`） |
| 行为校验 | `CallLog.Verify(method, matchers, times)` / `Never` / `Called`、`Describe()` |
| 替身基类 | `MockBase`（`Record0..Record3` / `Calls()` / `CallCount` / `ResetCalls`） |
| 桩件 | `StubValue<T>`、`Sequence<T>`、`ActionSpy` |
| 异步桩 | `TaskStub.Completed` / `Faulted` / `Pending` |

## 用法

写一个替身：

```zan
using System.Test.Mocks;

public class FakeUserRepo : MockBase, IUserRepo {
    private StubValue<int> nextId = new StubValue<int>(1);

    public int Save(string name) {
        this.Record1("Save", Arg.Text(name));   // 记录调用
        return this.nextId.Value();             // 桩返回值
    }
}
```

断言行为：

```zan
FakeUserRepo repo = new FakeUserRepo();
repo.Save("张三");

List<ArgMatcher> name = new List<ArgMatcher>();
name.Add(Arg.Eq("张三"));
Assert.True(repo.Calls().Verify("Save", name, Times.Once()), repo.Calls().Describe());
Assert.True(repo.Calls().Verify("Save", Times.AtLeast(1)), repo.Calls().Describe());
```

异步桩：

```zan
int value = await TaskStub.Completed<int>(7);      // 假装成功
int bad = await TaskStub.Faulted<int>("boom");      // 假装失败（await 抛错）
Task<int> never = TaskStub.Pending<int>();         // 永不完成（测超时分支）
```

## 已知边界

- **没有运行时生成 mock**：替身必须手写（这是设计取舍，不是缺陷）。
- **没有属性 setter 记录**：`MockBase` 只记录方法调用；属性读写要手写
  记录或用 `CallLog.Record` 手动补。
- **没有参数化匹配**（`It.Is<T>(predicate)`）：只有六种固定匹配方式；
  需要自定义判定时，把判定做进被测逻辑或用 `Between` 近似。
- **double 的相等依赖格式化**：两边都走 `Arg.Text` 才可比；跨平台比较
  同一份 double 时要注意格式化差异。
- **`Sequence<T>` 用完重复末值**：想要"用完即错"自己看 `Remaining()`。
- **`TaskStub.Pending` 会留下挂起协程**：测试进程里用一两个即可，跑多了
  会攒下永远不完成的协程。
- **`null` 字面量在重载调用里会解析到值类型重载**（Zan 语言行为）：
  `Arg.Text(null)` 走的是 `Text(int)` 返回 `"0"`，要传 null 请先声明成
  `string` 局部变量。

## 验证

`tests/conformance/test_mocks.zan` 覆盖：实参规范化（含 null 归一）、六种
匹配器、调用日志的记录与计数与查询与清空、行为校验（恰好 / 至少 / 至多 /
从未 / 带参数 / 参数个数不匹配）、`MockBase` 派生替身的记录与计数与重置、
桩件（`StubValue` 改值与零初始化、`Sequence` 顺序与重复末值与复位与空序列
抛错、`ActionSpy`）、异步桩（`Completed` 成功、`Faulted` 抛错）。

## 放置

测试工具包：`packages/Zan.Test.Mocks/src/System/Test/Mocks/*.zan`
（与 `packages/Zan.Test`（命名空间 `System.Test`）并列：前者是断言与
运行器，本包是替身与行为校验）。本包**不依赖** `Zan.Test`——
校验返回 `bool`，断言库由调用方选。
