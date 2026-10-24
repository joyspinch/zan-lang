# System.Bench

微基准测试：预热 + 重复测量（单调时钟）+ 中位数主导的统计 + 参数化用例 +
文本/JSON 报告 + 基线对比。纯 Zan，无反射，无原生依赖。

对标 BenchmarkDotNet（11,512★）的**时间基准核心子集**：用例注册、预热与迭代
控制、统计量、报告输出、与上次基线的对比。**不做** BenchmarkDotNet 的
进程隔离、GC 停顿收集、诊断器、硬件计数器联动——那些依赖 .NET 运行时设施。
分配量统计通过 `System.GC.GetAllocatedBytesForCurrentThread()` 支持。

## 设计取舍

### 1. 中位数是主指标，均值只作参考

基准最常见的失真不是"测不准"，而是**被偶发尖峰带偏**：一次调度抖动、一次后台
进程抢占，就会把均值抬高一大截，看起来像"代码变慢了"。中位数对这类噪声不敏感。

所以：`BenchResult.medianNs` 是主指标，基线比的是中位数，报告里中位数排在中位数
该在的位置（第一数值列），均值只是参考。

### 2. 预热是显式的一步，不靠"多跑几轮自然就热了"

第一次调用某段代码往往要走冷路径（首次进入的分支、页表未命中、数据还没进
缓存）。基准如果把这些算进去，测的是"冷启动"而不是"这个操作本身"。

`BenchOptions.warmupIterations` 单独控制预热轮数，**预热的轮次不计入统计**：
`r.iterations` 是测量轮数，`r.samples` 里也只有测量样本。

### 3. 三个旋钮：轮数下限、目标时长、轮数上限

基准测试最容易出的错是"测的东西不对"，而不是"测不准"：

- 迭代太少 → 噪声压过信号；
- 迭代太多 → 测到的是机器过热与频率下降；
- 不设下限 → 目标时长一达成就只剩一两个样本，统计量没有意义。

所以 `BenchOptions` 同时给 `minIterations`（样本下限）、`targetMilliseconds`
（目标总时长）、`maxIterations`（轮数上限）。测量循环跑到**满足下限且达到目标
时长**就停，但永不超过上限。判定器与 CI 用 `BenchOptions.Quick()`（预热 1 轮、
固定 3 轮、不按时间跑），轮数确定、可复现。

### 4. 委托调用开销不掩饰

`BenchCase` 的被测体是一个 `Action`，每次测量都要经过一次委托调用（几十纳秒
量级）。对极快的操作，这层开销会主导结果。

本包**不假装自己没有这层开销**：源码注释里写明、README 里写明。要测更细的东西，
就把操作内联进你自己的循环体（此时用 `Stopwatch` 手写计时），别拿一个测不出
纳秒级差异的委托式基准下结论。

### 5. 参数化用例用泛型保类型，不用 `object`

基准的输入类型各异（要排序的数组、要解析的文本、要哈希的键）。用 `object`
装起来就得装箱——Zan 没有装箱，于是这条路直接不通。

正确做法是把参数类型保留在泛型里：`BenchCaseT<TArgs>` 持有一个
`ActionT<TArgs>` 和一个 `TArgs`，泛型实例化时按实参类型特化，调用点零开销。

套件层面把参数化用例**擦除成无参委托**（参数在注册时固定——这是基准的常态：
测"对这份输入重复操作"），这样两类用例能一起跑、一起进报告。泛型实参类型保留
在闭包里，擦除不引入装箱。

> **命名注意**：Zan 不允许**同名泛型与非泛型类型共存**——写 `BenchCase` +
> `BenchCase<TArgs>` 会报 `duplicate type declaration 'BenchCase'`。所以泛型版
> 另起名为 `BenchCaseT`。

### 6. 同名用例注册直接抛错，不静默覆盖

套件里注册了两个同名用例，静默覆盖的后果是"以为测的是 A、其实测的是 B"——
基准报告里这种错误几乎无法被发现。所以 `BenchSuite.Add` 遇到重名抛
`BenchError.DuplicateCase`。

### 7. 基线对比带噪声阈值

基准本身有噪声，±3% 以内一律判 `same`。把"变慢了 2%"报成回归，只会让人养成
忽略告警的习惯——**告警一旦不可信就等于没有告警**。

`BenchBaseline.Compare` 的四种结论：`faster` / `slower` / `same` / `new`
（基线里没有这个用例）。阈值可按需覆盖。

## 能力

| 能力 | 说明 |
|---|---|
| 测量循环 | 预热（不计入统计）→ 反复测量 → 统计；轮数下限 / 目标时长 / 轮数上限三个旋钮 |
| 计时源 | `Stopwatch.NowTicks()` 单调时钟（不受墙钟调整影响），换算到纳秒 |
| 统计量 | 中位数（主指标）/ 均值 / 最小 / 最大 / 样本标准差 / 每秒操作数 / 相对波动 |
| 无参用例 | `BenchCase`：名字 + `Action` |
| 参数化用例 | `BenchCaseT<TArgs>`：名字 + `ActionT<TArgs>` + 参数（泛型保类型，零装箱） |
| 套件门面 | `BenchSuite`：注册 / 跑 / 出报告；重名注册抛 `DuplicateCase` |
| 文本报告 | `BenchReport.Text`：按列宽对齐的表格，列 = 用例名 / 轮数 / 中位数 / 均值 / 最小 / 最大 / 标准差 / ops/s |
| JSON 报告 | `BenchReport.Json`：可直接存成基线文件；字符串转义齐全（引号 / 反斜杠 / 换行 / 制表） |
| 基线对比 | `BenchBaseline.Compare`：比中位数，输出 `faster` / `slower` / `same` / `new` + 变化百分比 |
| 可区分错误码 | `BenchError`：用例非法 / 名字重复 / 基线损坏 / 基线缺项 / 选项非法 / 被测体抛异常 |

## 用法

```zan
using System;
using System.Bench;

BenchSuite suite = new BenchSuite();
suite.SetOptions(BenchOptions.Default());     // 预热 3 / 至少 10 轮 / 目标 200ms / 最多 1000 轮

// 1) 无参用例
suite.Add(new BenchCase("parse-int", () => {
    int v = 0;
    for (int i = 0; i < 1000; i++) { v = v + i; }
}));

// 2) 参数化用例（参数类型保留在泛型里）
int[] data = new int[1024];
suite.Add(new BenchCaseT<int[]>("sort-1k", (a) => {
    BenchStats.Sort(a);
}, data));

// 3) 跑 + 出报告
List<BenchResult> results = suite.Run();
Console.WriteLine(BenchReport.Text(results));

// 4) 存基线（JSON）
string baselineJson = suite.ToJson();
File.WriteAllText("bench.baseline.json", baselineJson);

// 5) 下次跟基线比
List<BenchDelta> deltas = BenchBaseline.Compare(results, baselineJson);
Console.WriteLine(BenchBaseline.Describe(deltas));
```

只关心单个用例时不用套件：

```zan
BenchRunner runner = new BenchRunner(BenchOptions.Quick());
BenchResult r = runner.Run(new BenchCase("noop", () => { }));
Console.WriteLine(r.Describe());
```

## 并发边界

- **单线程使用**。`BenchSuite` / `BenchRunner` / `BenchResult` 都不是线程安全的：
  `BenchSuite` 内部持有一个用例列表，测量循环往 `BenchResult.samples` 里追加
  样本，同时跑会互相踩。要并发跑多个用例，**开多个 `BenchSuite` 实例**
  （各自持自己的列表），或者自己加锁。
- **同一个用例不要并发测**。两个线程同时测同一个用例，除了数据竞争，还会让
  两次测量的缓存状态互相干扰，数字失真。
- **测出来的数字是单机结论**。跨机器、跨进程、跨运行时刻的数字不能直接比；
  基线文件要跟机器绑定（基线对比是"同一台机器上这次 vs 上次"）。
- 计时用 `Stopwatch` 单调时钟，**不受系统墙钟调整影响**（NTP 校时、夏令时不会
  让某几轮突然变成负数或暴涨）；`BenchRunner` 仍对负的 tick 差做了兜底。

## 验证

conformance `bench_suite`（74 条断言）：统计原语（中位数奇偶取值 / 均值 /
最小最大 / 有序副本不改原数组 / 标准差 / 平方根 / 空样本）· 选项口径（默认值 /
`Quick` 固定轮数 / `Copy` 不改原件）· 测量循环语义（轮数确定 / 预热不计入
统计 / 样本数 / 被测体调用次数 / 中位数落在最快与最慢之间 / ops/s 为正）·
参数化用例（拿到参数、保留参数类型）· 套件（两类用例共存 / 保持注册顺序）·
文本报告（表头 / 用例名 / 行数）· JSON 报告（结构 / 名字 / 转义）· 基线往返
（生成的 JSON 能被自己解析回来）· 基线对比（变快 / 变慢 / 噪声 / 新用例 /
自定义阈值）· 错误可区分（重名 / 空名 / null 用例 / 基线损坏 / 空基线 /
基线缺 benchmarks 数组）。

判定器**只断言结构与关系，不断言具体耗时**——耗时依赖机器，绝对值不是契约。

## 已知边界

- **单次操作分配量统计已支持**：基于 `System.GC.GetAllocatedBytesForCurrentThread()`（A464），文本表格（`allocated(B)` 列）和 JSON（`allocatedBytes` 属性）均记录单次操作平均分配的托管堆字节数。
- **没有 GC 停顿度量**：Zan 基于 ARC（自动引用计数），对象生命周期确定性终结，不存在分代 GC 收集器的暂停时间。

  在这之前，**不要用本包的数字推断内存行为**——它只回答"多快"，不回答"占了
  多少"。
- **不做进程隔离**。BenchmarkDotNet 每个用例跑在独立进程里，避免 JIT / 缓存 /
  静态状态互相污染；Zan 侧本包所有用例跑在同一个进程里，**用例之间会互相
  预热**（一个用例把数据打进缓存，会让后面的用例变快）。要隔离就自己起多个
  进程跑。
- **没有 GC 相关的统计**。Zan 用 ARC，**没有 GC**，所以 BenchmarkDotNet 里
  `Gen0/Gen1/Gen2` 那几列在本包没有对应物（这是"没有"，不是"漏了"）。
- **委托调用开销**（见设计取舍 §4）无法在包内消掉。
- **文本报告的浮点格式**走 `Convert.ToString`，不同平台的尾数位数可能不同；
  报告给人看没问题，要做机器比对请用 JSON。
- **不做统计显著性检验**。`stdDevNs` / `VarianceRatio` 给你判断"机器稳不稳"
  的原料，但本包不替你判"这次差异是不是显著"。

## 放置

`packages/Zan.Bench` —— 命名空间 `System.Bench`，不依赖任何其它包
（只用到 `System` / `System.Collections.Generic` / `System.Json` / `System.Text`
这些 stdlib 既有能力）。
